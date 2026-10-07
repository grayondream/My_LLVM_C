#include "sema/SemanticAnalyzer.h"
#include "sema/CompileTimeEvaluator.h"
#include "ast/Expr.h"
#include "ast/Stmt.h"
#include "ast/Decl.h"
#include "ast/Type.h"
#include "ast/Mangle.h"
#include "sema/TemplateRegistry.h"
#include "sema/TemplateInstantiator.h"
#include <algorithm>

// Strip any number of typedef/alias layers, returning the underlying type.
static Type* stripTypedef(Type* type) {
    while (type && type->kind == TypeKind::Typedef) {
        type = static_cast<TypedefType*>(type)->aliasedType;
    }
    return type;
}

// SEM-11: true when `stmt` unconditionally transfers control (return/break/
// continue). Used for the unreachable-code warning (W3003). Trailing `defer`
// statements do not clear termination — they still run on scope exit.
static bool stmtAlwaysTransfers(const StmtAST* stmt) {
    if (!stmt) return false;
    if (dynamic_cast<const ReturnStmtAST*>(stmt) ||
        dynamic_cast<const BreakStmtAST*>(stmt) ||
        dynamic_cast<const ContinueStmtAST*>(stmt)) {
        return true;
    }
    if (auto* compound = dynamic_cast<const CompoundStmtAST*>(stmt)) {
        for (auto it = compound->stmts.rbegin(); it != compound->stmts.rend(); ++it) {
            if (!*it) continue;
            if (dynamic_cast<const DeferStmtAST*>(it->get())) continue;
            return stmtAlwaysTransfers(it->get());
        }
        return false;
    }
    if (auto* ifStmt = dynamic_cast<const IfStmtAST*>(stmt)) {
        return ifStmt->thenStmt && ifStmt->elseStmt &&
               stmtAlwaysTransfers(ifStmt->thenStmt.get()) &&
               stmtAlwaysTransfers(ifStmt->elseStmt.get());
    }
    return false;
}

SemanticAnalyzer::SemanticAnalyzer()
    : globalScope(std::make_unique<Scope>(nullptr)),
      currentScope(globalScope.get()),
      currentFunction(nullptr),
      typeCtx(&TypeContext::instance()) {}

const std::vector<Diagnostic>& SemanticAnalyzer::getErrors() const {
    return errors;
}

const std::vector<Diagnostic>& SemanticAnalyzer::getWarnings() const {
    return warnings;
}

void SemanticAnalyzer::emitError(const std::string& msg, const ASTNode& node) {
    errors.emplace_back(Diagnostic::Level::Error, msg, node.sourceFile, node.sourceLine,
                        node.sourceColumn);
    if (!m_instStack.empty())
        errors.back().message +=
            " (in instantiation of template '" + m_instStack.back() + "')";
}

void SemanticAnalyzer::emitWarning(const std::string& msg, const ASTNode& node) {
    warnings.emplace_back(Diagnostic::Level::Warning, msg, node.sourceFile, node.sourceLine, node.sourceColumn);
}

void SemanticAnalyzer::emitError(DiagnosticCode code, const std::string& msg, const ASTNode& node) {
    errors.emplace_back(Diagnostic::Level::Error, code, msg, node.sourceFile, node.sourceLine, node.sourceColumn);
    if (!m_instStack.empty())
        errors.back().message +=
            " (in instantiation of template '" + m_instStack.back() + "')";
}

void SemanticAnalyzer::emitWarning(DiagnosticCode code, const std::string& msg, const ASTNode& node) {
    warnings.emplace_back(Diagnostic::Level::Warning, code, msg, node.sourceFile, node.sourceLine, node.sourceColumn);
}

void SemanticAnalyzer::enterScope() {
    currentScope = new Scope(currentScope);
}

void SemanticAnalyzer::exitScope() {
    // SEM-11: report local variables/arrays that were never referenced (W3002).
    // Parameters and globals do not opt in, and must not warn here.
    if (currentScope != globalScope.get()) {
        for (auto& entry : currentScope->symbols) {
            for (Symbol* sym : entry.second.getCandidates()) {
                if (sym->checkUnused && !sym->isUsed) {
                    warnings.emplace_back(Diagnostic::Level::Warning,
                                          DiagnosticCode::WarnUnusedVariable,
                                          "unused variable '" + sym->name + "'",
                                          sym->declFile, sym->declLine, sym->declColumn);
                }
            }
        }
    }
    Scope* parent = currentScope->parent;
    if (currentScope != globalScope.get()) {
        delete currentScope;
    }
    currentScope = parent;
}

bool SemanticAnalyzer::declare(const std::string& name, Type* type) {
    // An exported declaration of a participating module is also visible to
    // importers, so it is registered in the global scope; everything else at
    // module top level stays in the module's private scope (MOD-05/06).
    Scope* target = currentScope;
    if (activeModuleScope != nullptr && currentScope == activeModuleScope && currentDeclExported) {
        target = globalScope.get();
    }
    Symbol* sym = new Symbol(name, type);
    if (!target->declare(name, sym)) {
        delete sym;
        return false;
    }
    return true;
}

Symbol* SemanticAnalyzer::lookup(const std::string& name) {
    for (const auto& candidate : namespaceCandidates(name)) {
        if (Symbol* sym = currentScope->lookup(candidate)) {
            return sym;
        }
    }
    return nullptr;
}

// Namespace helpers ---------------------------------------------------------

std::string SemanticAnalyzer::mangleNamespaceName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == ':' && i + 1 < name.size() && name[i + 1] == ':') {
            out.push_back('_');
            ++i;
        } else if (name[i] == '.') {
            out.push_back('_');
        } else {
            out.push_back(name[i]);
        }
    }
    return out;
}

// AGG-10/DS5: qualified access to a class static member goes through the
// desugared global symbol; check the declaring class's access level here so
// private static members stay unreachable from outside (E2009, as instance
// members). `originalName` is the pre-resolution spelling, e.g. `SMP::secret`.
void SemanticAnalyzer::checkStaticMemberAccess(const std::string& originalName, ExprAST& node) {
    if (originalName.find("::") == std::string::npos) return;
    auto it = staticMemberIndex.find(mangleNamespaceName(originalName));
    if (it == staticMemberIndex.end()) return;
    ClassType* definingClass = it->second.first;
    if (!definingClass) return;
    // I1: static members are keyed "static:<name>" so a same-name instance
    // member's access level is never consulted (independent keys).
    AccessLevel level = definingClass->memberAccessLevel("static:" + it->second.second);
    if (level != AccessLevel::Public && currentClass != definingClass) {
        emitError(DiagnosticCode::SemPrivateMemberAccess,
                  "cannot access " +
                      std::string(level == AccessLevel::Private ? "private" : "protected") +
                      " member '" + it->second.second + "' of class '" + definingClass->name +
                      "' outside the class; make it public or add an accessor",
                  node);
    }
}

// Candidate symbol-table keys for `name`, innermost namespace first.
std::vector<std::string> SemanticAnalyzer::namespaceCandidates(const std::string& name) const {
    std::vector<std::string> candidates;
    if (name.find("::") != std::string::npos || name.find('.') != std::string::npos) {
        candidates.push_back(mangleNamespaceName(name));
        return candidates;
    }
    std::string prefix = namespacePrefix; // e.g. "A_B_"
    while (!prefix.empty()) {
        candidates.push_back(prefix + name);
        if (prefix.back() == '_') prefix.pop_back();
        size_t pos = prefix.rfind('_');
        prefix = (pos == std::string::npos) ? std::string() : prefix.substr(0, pos + 1);
    }
    candidates.push_back(name);
    return candidates;
}

std::string SemanticAnalyzer::scopedName(const std::string& name) const {
    if (atGlobalLevel() && !namespacePrefix.empty()) {
        return namespacePrefix + name;
    }
    return name;
}

bool SemanticAnalyzer::atGlobalLevel() const {
    return currentScope == globalScope.get() || currentScope == activeModuleScope;
}

Scope* SemanticAnalyzer::moduleScopeFor(const std::string& moduleName) {
    auto it = moduleScopes.find(moduleName);
    if (it != moduleScopes.end()) {
        return it->second.get();
    }
    auto scope = std::make_unique<Scope>(globalScope.get());
    Scope* raw = scope.get();
    moduleScopes[moduleName] = std::move(scope);
    return raw;
}

void SemanticAnalyzer::enterModuleContext(const std::string& moduleName) {
    currentModule = moduleName;
    if (moduleName.empty()) {
        activeModuleScope = nullptr;
        currentScope = globalScope.get();
    } else {
        activeModuleScope = moduleScopeFor(moduleName);
        currentScope = activeModuleScope;
    }
}

std::string SemanticAnalyzer::resolveNamespaceName(const std::string& name) const {
    for (const auto& candidate : namespaceCandidates(name)) {
        if (OverloadSet* set = currentScope->lookupOverload(candidate)) {
            if (!set->empty()) {
                return candidate;
            }
        }
    }
    return name;
}

bool SemanticAnalyzer::isIntegerType(Type* type) const {
    type = stripTypedef(type);
    if (!type) return false;
    return type->kind == TypeKind::Char || type->kind == TypeKind::Enum ||
           type->kind == TypeKind::Bool ||
           type->kind == TypeKind::Int8 || type->kind == TypeKind::Int16 ||
           type->kind == TypeKind::Int32 || type->kind == TypeKind::Int64 || type->kind == TypeKind::Int128 ||
           type->kind == TypeKind::UInt8 || type->kind == TypeKind::UInt16 ||
           type->kind == TypeKind::UInt32 || type->kind == TypeKind::UInt64 || type->kind == TypeKind::UInt128 ||
           type->kind == TypeKind::ISize || type->kind == TypeKind::USize;
}

bool SemanticAnalyzer::isFloatType(Type* type) const {
    type = stripTypedef(type);
    if (!type) return false;
    return type->kind == TypeKind::Float32 || type->kind == TypeKind::Float64 ||
           type->kind == TypeKind::Float16 || type->kind == TypeKind::Float128;
}

bool SemanticAnalyzer::isArithmeticType(Type* type) const {
    return isIntegerType(type) || isFloatType(type);
}

bool SemanticAnalyzer::isPointerOrArray(Type* type) const {
    type = stripTypedef(type);
    if (!type) return false;
    return type->kind == TypeKind::Pointer || type->kind == TypeKind::Array;
}

bool SemanticAnalyzer::isScalarType(Type* type) const {
    return isArithmeticType(type) || isPointerOrArray(type);
}

bool SemanticAnalyzer::typesCompatible(Type* left, Type* right) const {
    left = stripTypedef(left);
    right = stripTypedef(right);
    if (!left || !right) return false;
    // Enum types are strong (TYP-09 / TYP-20): only the exact same enum type is
    // implicitly compatible; enum <-> integer/float requires an explicit cast.
    if (left->kind == TypeKind::Enum || right->kind == TypeKind::Enum) {
        return left->kind == TypeKind::Enum && right->kind == TypeKind::Enum &&
               static_cast<EnumType*>(left)->name == static_cast<EnumType*>(right)->name;
    }
    // TYP-12: slices match by element type only (int32[] != float64[]);
    // arrays decay to slice views. Checked before the generic same-kind rule.
    if (left->kind == TypeKind::Slice && right->kind == TypeKind::Slice)
        return typesEqual(static_cast<SliceType*>(left)->elementType,
                          static_cast<SliceType*>(right)->elementType);
    if (left->kind == TypeKind::Slice && right->kind == TypeKind::Array)
        return typesEqual(static_cast<SliceType*>(left)->elementType,
                          static_cast<ArrayType*>(right)->elementType);
    // P1-02 (TYP-13/14): Optional/Result match strictly by their type
    // arguments (int32? != float64?) — before the generic same-kind rule.
    if (left->kind == TypeKind::Optional && right->kind == TypeKind::Optional)
        return typesEqual(static_cast<OptionalType*>(left)->elementType,
                          static_cast<OptionalType*>(right)->elementType);
    if (left->kind == TypeKind::Result && right->kind == TypeKind::Result)
        return typesEqual(left, right);
    if (left->kind == right->kind) return true;
    if (isArithmeticType(left) && isArithmeticType(right)) return true;
    if (left->kind == TypeKind::Pointer && right->kind == TypeKind::Pointer) return true;
    if (left->kind == TypeKind::Pointer && right->kind == TypeKind::Int32) return true;
    if (left->kind == TypeKind::Int32 && right->kind == TypeKind::Pointer) return true;
    // Arrays decay to a pointer to their first element.
    if (left->kind == TypeKind::Pointer && right->kind == TypeKind::Array) return true;
    if (left->kind == TypeKind::Array && right->kind == TypeKind::Pointer) return true;
    return false;
}

Type* SemanticAnalyzer::getCommonType(Type* left, Type* right) const {
    left = stripTypedef(left);
    right = stripTypedef(right);
    if (!left) return right;
    if (!right) return left;
    // TYP-22: integer promotion + usual arithmetic conversions.
    if (isArithmeticType(left) && isArithmeticType(right)) {
        return usualArithmeticType(left, right);
    }
    if (left->kind == right->kind) return left;
    return left;
}

std::string SemanticAnalyzer::typeToString(Type* type) const {
    if (!type) return "<unknown>";
    switch (type->kind) {
        case TypeKind::Void: return "void";
        case TypeKind::Char: return "char";
        case TypeKind::Bool: return "bool";
        case TypeKind::Int8: return "int8";
        case TypeKind::Int16: return "int16";
        case TypeKind::Int32: return "int32";
        case TypeKind::Int64: return "int64";
        case TypeKind::Int128: return "int128";
        case TypeKind::UInt8: return "uint8";
        case TypeKind::UInt16: return "uint16";
        case TypeKind::UInt32: return "uint32";
        case TypeKind::UInt64: return "uint64";
        case TypeKind::UInt128: return "uint128";
        case TypeKind::ISize: return "isize";
        case TypeKind::USize: return "usize";
        case TypeKind::Float32: return "float32";
        case TypeKind::Float64: return "float64";
        case TypeKind::Float16: return "float16";
        case TypeKind::Float128: return "float128";
        case TypeKind::Slice: return "slice";
        case TypeKind::Optional: return "optional";
        case TypeKind::Result: return "result";
        case TypeKind::Pointer: {
            std::string baseStr = typeToString(type->base);
            if (type->isConst) baseStr = "const " + baseStr;
            return baseStr + "*";
        }
        case TypeKind::Array: {
            auto* arrType = static_cast<ArrayType*>(type);
            return typeToString(arrType->elementType) + "[" + std::to_string(arrType->size) + "]";
        }
        case TypeKind::Struct: {
            auto* structType = static_cast<StructType*>(type);
            return "struct " + structType->name;
        }
        case TypeKind::Class: {
            auto* classType = static_cast<ClassType*>(type);
            return "class " + classType->name;
        }
        case TypeKind::Union: {
            auto* unionType = static_cast<UnionType*>(type);
            return "union " + unionType->name;
        }
        case TypeKind::Enum: {
            auto* enumType = static_cast<EnumType*>(type);
            return "enum " + enumType->name;
        }
        case TypeKind::Function: return "<function>";
        case TypeKind::Typedef: {
            auto* typedefType = static_cast<TypedefType*>(type);
            return typedefType->name;
        }
        default: return "<unknown>";
    }
}

std::string SemanticAnalyzer::binaryOpToString(BinaryOp op) const {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Sub: return "-";
        case BinaryOp::Mul: return "*";
        case BinaryOp::Div: return "/";
        case BinaryOp::Mod: return "%";
        case BinaryOp::Eq: return "==";
        case BinaryOp::NotEq: return "!=";
        case BinaryOp::Lt: return "<";
        case BinaryOp::Gt: return ">";
        case BinaryOp::Le: return "<=";
        case BinaryOp::Ge: return ">=";
        case BinaryOp::And: return "&&";
        case BinaryOp::Or: return "||";
        case BinaryOp::BitAnd: return "&";
        case BinaryOp::BitOr: return "|";
        case BinaryOp::BitXor: return "^";
        case BinaryOp::LShift: return "<<";
        case BinaryOp::RShift: return ">>";
        default: return "<unknown>";
    }
}

bool SemanticAnalyzer::isStructOrUnionType(Type* type) const {
    if (!type) return false;
    return type->kind == TypeKind::Struct || type->kind == TypeKind::Class || type->kind == TypeKind::Union;
}

std::string SemanticAnalyzer::getOperatorMangledName(BinaryOp op, Type* left, Type* right) {
    std::string opName;
    switch (op) {
        case BinaryOp::Add: opName = "operator+"; break;
        case BinaryOp::Sub: opName = "operator-"; break;
        case BinaryOp::Mul: opName = "operator*"; break;
        case BinaryOp::Div: opName = "operator/"; break;
        case BinaryOp::Eq: opName = "operator=="; break;
        case BinaryOp::NotEq: opName = "operator!="; break;
        case BinaryOp::Lt: opName = "operator<"; break;
        case BinaryOp::Gt: opName = "operator>"; break;
        case BinaryOp::Le: opName = "operator<="; break;
        case BinaryOp::Ge: opName = "operator>="; break;
        default: return "";
    }
    return mangleFunction(opName, {left, right});
}

Type* SemanticAnalyzer::checkBinaryTypes(BinaryOp op, Type* left, Type* right, ExprAST& node) {
    if (!left || !right) return nullptr;

    switch (op) {
        case BinaryOp::Add:
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
        case BinaryOp::Mod:
            if (isArithmeticType(left) && isArithmeticType(right)) {
                return getCommonType(left, right);
            }
            if (isPointerOrArray(left) && isIntegerType(right)) return left;
            if (isIntegerType(left) && isPointerOrArray(right)) return right;
            emitError("invalid operands to binary '" + binaryOpToString(op) + "': cannot apply '" 
                + binaryOpToString(op) + "' to '" + typeToString(left) + "' and '" + typeToString(right) + "'", node);
            return nullptr;

        case BinaryOp::Eq:
        case BinaryOp::NotEq:
        case BinaryOp::Lt:
        case BinaryOp::Gt:
        case BinaryOp::Le:
        case BinaryOp::Ge:
            // Arithmetic operands (including enums, via integer promotion) are
            // comparable even though enum <-> integer is not implicitly
            // *assignable* (TYP-20). Pointers compare against pointers/integers.
            if (isArithmeticType(left) && isArithmeticType(right)) {
                return typeCtx->getInt32();
            }
            if (!typesCompatible(left, right)) {
                emitError("comparison of incompatible types: '" + typeToString(left) + "' and '" 
                    + typeToString(right) + "' with '" + binaryOpToString(op) + "'", node);
                return nullptr;
            }
            return typeCtx->getInt32();

        case BinaryOp::And:
        case BinaryOp::Or:
            // 评审 I1: logical operands must be scalar — Optional/Result
            // reaching `and`/`br` produced invalid IR.
            if (!isScalarType(left) || !isScalarType(right)) {
                emitError("logical operator '" + binaryOpToString(op) +
                              "' requires scalar operands, got '" +
                              typeToString(left) + "' and '" + typeToString(right) + "'",
                          node);
                return nullptr;
            }
            return typeCtx->getInt32();

        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
        case BinaryOp::LShift:
        case BinaryOp::RShift:
            if (isIntegerType(left) && isIntegerType(right)) {
                return getCommonType(left, right);
            }
            emitError("bitwise '" + binaryOpToString(op) + "' applied to non-integer types: '" 
                + typeToString(left) + "' and '" + typeToString(right) + "'", node);
            return nullptr;

        default:
            return nullptr;
    }
}

Type* SemanticAnalyzer::checkAssignmentTypes(Type* lhs, Type* rhs, ExprAST& node) {
    if (!lhs || !rhs) return nullptr;

    Type* lhsRaw = lhs;
    Type* lhsS = stripTypedef(lhs);
    Type* rhsS = stripTypedef(rhs);

    if (lhsS->kind == TypeKind::Void || rhsS->kind == TypeKind::Void) {
        emitError("cannot assign to or from 'void' type", node);
        return nullptr;
    }

    // Enum types are strong (TYP-09 / TYP-20): assigning between an enum and an
    // integer (or between distinct enums) requires an explicit cast.
    if (lhsS->kind == TypeKind::Enum || rhsS->kind == TypeKind::Enum) {
        if (lhsS->kind == TypeKind::Enum && rhsS->kind == TypeKind::Enum &&
            static_cast<EnumType*>(lhsS)->name == static_cast<EnumType*>(rhsS)->name) {
            return lhsRaw;
        }
        emitError(DiagnosticCode::SemIncompatibleAssignment,
                  "cannot assign '" + typeToString(rhs) + "' to '" + typeToString(lhs) +
                      "' without an explicit conversion (TYP-20)", node);
        return nullptr;
    }

    if (isArithmeticType(lhsS) && isArithmeticType(rhsS)) return lhsRaw;
    // Arrays are non-modifiable lvalues (C99 6.5.16): assignment to an array
    // is rejected regardless of the RHS kind. Previously the generic
    // same-kind / pointer-or-array rules let `a = b` (even shape-mismatched)
    // and `a = p` through, where codegen stored a decayed pointer into the
    // array's storage — silent memory corruption. Copies go through brace
    // initializers, memcpy, or element assignment.
    if (lhsS->kind == TypeKind::Array) {
        emitError(DiagnosticCode::SemIncompatibleAssignment,
                  "cannot assign to array '" + typeToString(lhs) +
                      "': arrays are not modifiable lvalues (use memcpy or element assignment)",
                  node);
        return nullptr;
    }
    // TYP-12: slice targets must match by element type; arrays decay to
    // slice views (zero-copy). Tightens the generic same-kind rule that
    // silently accepted int32[] = float64[].
    if (lhsS->kind == TypeKind::Slice && rhsS->kind == TypeKind::Slice)
        return typesEqual(static_cast<SliceType*>(lhsS)->elementType,
                          static_cast<SliceType*>(rhsS)->elementType) ? lhsRaw : nullptr;
    if (lhsS->kind == TypeKind::Slice && rhsS->kind == TypeKind::Array)
        return typesEqual(static_cast<SliceType*>(lhsS)->elementType,
                          static_cast<ArrayType*>(rhsS)->elementType) ? lhsRaw : nullptr;
    if (lhsS->kind == rhsS->kind) {
        // P1-02 (TYP-13/14): Optional/Result are strict in their type
        // arguments — the generic same-kind rule would mis-accept
        // int32? = float64?.
        if (lhsS->kind == TypeKind::Optional || lhsS->kind == TypeKind::Result)
            return typesEqual(lhsS, rhsS) ? lhsRaw : nullptr;
        return lhsRaw;
    }
    if (isPointerOrArray(lhsS) && isPointerOrArray(rhsS)) return lhsRaw;
    if (isPointerOrArray(lhsS) && isIntegerType(rhsS)) return lhsRaw;

    emitError(DiagnosticCode::SemIncompatibleAssignment,
              "incompatible types in assignment: cannot assign '" + typeToString(rhs) +
                  "' to '" + typeToString(lhs) + "'", node);
    return nullptr;
}

Type* SemanticAnalyzer::checkFunctionCall(const std::string& name, const std::vector<std::unique_ptr<ExprAST>>& args, ExprAST& node, FunctionType** outFuncType) {
    OverloadSet* overloadSet = currentScope->lookupOverload(name);
    if (!overloadSet || overloadSet->empty()) {
        emitError(DiagnosticCode::SemUnresolvedCall, "use of undeclared function '" + name + "'", node);
        return nullptr;
    }

    // Collect argument types
    std::vector<Type*> argTypes;
    for (auto& arg : args) {
        Type* argType = getExprType(*arg);
        argTypes.push_back(argType);
    }

    // Resolve overload
    Symbol* resolved = overloadSet->resolve(argTypes);
    if (!resolved) {
        // Distinguish "no viable candidate" from "ambiguous".
        bool anyViable = false;
        for (auto* sym : overloadSet->getCandidates()) {
            if (sym->type->kind != TypeKind::Function) continue;
            auto* funcType = static_cast<FunctionType*>(sym->type);
            size_t fixedCount = funcType->paramTypes.size();
            if (funcType->isVarArg) {
                if (argTypes.size() < fixedCount) continue;
            } else if (funcType->paramTypes.size() != argTypes.size()) {
                continue;
            }
            bool viable = true;
            for (size_t i = 0; i < fixedCount; ++i) {
                if (conversionRank(argTypes[i], funcType->paramTypes[i]) < 0) {
                    viable = false;
                    break;
                }
            }
            if (viable) {
                anyViable = true;
                break;
            }
        }
        if (anyViable) {
            emitError(DiagnosticCode::SemAmbiguousCall, "ambiguous call to overloaded function '" + name + "'", node);
        } else {
            emitError(DiagnosticCode::SemUnresolvedCall, "no matching function for call to '" + name + "'", node);
        }
        return nullptr;
    }

    if (resolved->type->kind != TypeKind::Function) {
        emitError("cannot call non-function '" + name + "' (type: " + typeToString(resolved->type) + ")", node);
        return nullptr;
    }

    FunctionType* funcType = static_cast<FunctionType*>(resolved->type);
    if (!funcType->isVarArg && args.size() != funcType->paramTypes.size()) {
        emitError("wrong number of arguments to function '" + name + "': expected " 
            + std::to_string(funcType->paramTypes.size()) + ", got " + std::to_string(args.size()), node);
        return nullptr;
    }

    if (outFuncType) *outFuncType = funcType;
    return funcType->returnType;
}

Type* SemanticAnalyzer::typeForLiteralKind(LiteralKind kind) {
    // LEX-15: suffix/default -> fixed-width base type.
    switch (kind) {
        case LiteralKind::Int:      return typeCtx->getInt32();
        case LiteralKind::UInt:     return typeCtx->getUInt32();
        case LiteralKind::Long:     return typeCtx->getInt64();
        case LiteralKind::ULong:    return typeCtx->getUInt64();
        case LiteralKind::Float16:  return typeCtx->getFloat16();
        case LiteralKind::Float32:  return typeCtx->getFloat32();
        case LiteralKind::Float64:  return typeCtx->getFloat64();
        case LiteralKind::Float128: return typeCtx->getFloat128();
        case LiteralKind::None:     break;
    }
    return nullptr;
}

Type* SemanticAnalyzer::getExprType(ExprAST& expr) {
    visit(expr);
    return expr.type;
}

// ---- P1-04 / CT-04/05/12: compile_time 特判 ----

CompileTimeEvaluator& SemanticAnalyzer::ctEval() {
    if (!m_ctEval) {
        m_ctEval = std::make_unique<CompileTimeEvaluator>(*this);
    }
    return *m_ctEval;
}

bool SemanticAnalyzer::isCompileTimeRoot(const ExprAST* expr) {
    // 沿 MemberAccess 对象链下行，根为 VariableExpr("compile_time") 即真。
    while (expr) {
        if (auto* ma = dynamic_cast<const MemberAccessExprAST*>(expr)) {
            expr = ma->object.get();
            continue;
        }
        if (auto* var = dynamic_cast<const VariableExprAST*>(expr)) {
            if (var->name != "compile_time") return false;
            // P1-04 评审 I4（spec §1 消歧）：作用域内已声明的同名变量优先，
            // 其成员访问走普通路径，不被编译期命名空间劫持。
            if (currentScope && currentScope->lookup("compile_time")) return false;
            return true;
        }
        return false;
    }
    return false;
}

bool SemanticAnalyzer::containsCompileTimeRoot(const ExprAST* expr) {
    if (!expr) return false;
    if (isCompileTimeRoot(expr)) return true;
    if (auto* bin = dynamic_cast<const BinaryExprAST*>(expr)) {
        return containsCompileTimeRoot(bin->left.get())
            || containsCompileTimeRoot(bin->right.get());
    }
    if (auto* un = dynamic_cast<const UnaryExprAST*>(expr)) {
        return containsCompileTimeRoot(un->operand.get());
    }
    if (auto* ter = dynamic_cast<const TernaryExprAST*>(expr)) {
        return containsCompileTimeRoot(ter->cond.get())
            || containsCompileTimeRoot(ter->then.get())
            || containsCompileTimeRoot(ter->elseExpr.get());
    }
    if (auto* mc = dynamic_cast<const MethodCallExprAST*>(expr)) {
        if (containsCompileTimeRoot(mc->object.get())) return true;
        for (const auto& a : mc->args) {
            if (containsCompileTimeRoot(a.get())) return true;
        }
        return false;
    }
    if (auto* ma = dynamic_cast<const MemberAccessExprAST*>(expr)) {
        return containsCompileTimeRoot(ma->object.get());
    }
    if (auto* call = dynamic_cast<const CallExprAST*>(expr)) {
        for (const auto& a : call->args) {
            if (containsCompileTimeRoot(a.get())) return true;
        }
    }
    return false;
}

// INT 结果的节点类型映射：按值域 int32/int64（spec §4）。
static Type* ctIntTypeFor(TypeContext* typeCtx, long long v) {
    return (v < -2147483648LL || v > 2147483647LL)
        ? typeCtx->getInt64() : typeCtx->getInt32();
}

// P1-04 评审 I3: 沿 typedef/指针/数组包装层检查毒化标志，返回毒化层
// 的可读名字（聚合名或 typedef 名）。
static bool ctIsPoisonedType(Type* t, std::string& name) {
    bool poisoned = false;
    while (t) {
        if (t->ctDeadBranch) {
            poisoned = true;
            if (name.empty()) {
                switch (t->kind) {
                    case TypeKind::Struct:
                    case TypeKind::Class: name = static_cast<StructType*>(t)->name; break;
                    case TypeKind::Union: name = static_cast<UnionType*>(t)->name; break;
                    case TypeKind::Enum:  name = static_cast<EnumType*>(t)->name; break;
                    case TypeKind::Typedef: name = static_cast<TypedefType*>(t)->name; break;
                    default: break;
                }
            }
        }
        switch (t->kind) {
            case TypeKind::Typedef: t = static_cast<TypedefType*>(t)->aliasedType; continue;
            case TypeKind::Pointer: t = t->base; continue;
            case TypeKind::Array:   t = static_cast<ArrayType*>(t)->elementType; continue;
            default: return poisoned;
        }
    }
    return poisoned;
}

void SemanticAnalyzer::checkCtDeadBranchUse(Type* t, ASTNode& at) {
    if (!t) return;
    std::string name;
    if (!ctIsPoisonedType(t, name)) return;
    if (!name.empty()) {
        emitError("use of type '" + name + "' from a non-selected compile_time.if branch", at);
    } else {
        emitError("use of a type declared in a non-selected compile_time.if branch", at);
    }
}

std::optional<SemanticAnalyzer::ConstValue>
SemanticAnalyzer::evalCompileTime(ExprAST* expr, ASTNode& at) {
    return ctEval().eval(expr, at);
}

bool SemanticAnalyzer::tryAnalyzeCompileTimeChain(MemberAccessExprAST& node) {
    if (!isCompileTimeRoot(node.object.get())) {
        return false;
    }
    // 根链成员名序列（e.g. compile_time.target.os → [target, os]）。
    std::vector<std::string> members;
    {
        std::vector<const MemberAccessExprAST*> chain;
        const ExprAST* cur = &node;
        while (auto* ma = dynamic_cast<const MemberAccessExprAST*>(cur)) {
            chain.push_back(ma);
            cur = ma->object.get();
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            members.push_back((*it)->memberName);
        }
    }
    node.ctHandled = true;
    node.isLValue = false;
    auto v = ctEval().eval(&node, node);
    if (!v) {
        node.type = nullptr; // 未知成员等诊断已由求值器发出
        return true;
    }
    switch (v->type) {
        case ConstValue::INT:
            node.ctInt = v->intVal;
            // 成员值类型映射（spec §4）：build.debug → bool，其余按值域。
            node.type = (members.size() == 2 && members[0] == "build" && members[1] == "debug")
                ? typeCtx->getBool()
                : ctIntTypeFor(typeCtx, v->intVal);
            break;
        case ConstValue::DOUBLE:
            node.ctFloat = v->doubleVal;
            node.type = typeCtx->getFloat64();
            break;
        case ConstValue::CHAR:
            // CHAR 有运行时类型——防御回退（评审 I2）。
            node.type = nullptr;
            break;
        default: // STR
            // STR 逃逸：编译期上下文（static_assert/if/constexpr 初始化）
            // 整树由求值器接管、不经本钩子——凡到钩子必是运行时上下文。
            emitError("compile-time string value cannot be used in runtime context", node);
            node.type = nullptr;
            break;
    }
    return true;
}

bool SemanticAnalyzer::tryAnalyzeCompileTimeCall(MethodCallExprAST& node) {
    auto* obj = dynamic_cast<VariableExprAST*>(node.object.get());
    if (!obj || obj->name != "compile_time") {
        return false;
    }
    node.ctHandled = true;
    node.isLValue = false;

    // P1-04 / CT-02: compile_time.static_assert(cond[, msg])。
    if (node.methodName == "static_assert") {
        if (node.args.empty() || node.args.size() > 2) {
            emitError("compile_time.static_assert requires (condition) or (condition, message)", node);
            node.type = nullptr;
            return true;
        }
        auto cond = ctEval().eval(node.args[0].get(), node);
        if (!cond || cond->type != ConstValue::INT) {
            emitError("compile_time argument must be a compile-time constant", node);
            node.type = nullptr;
            return true;
        }
        if (cond->intVal == 0) {
            std::string msg = "static_assert failed";
            if (node.args.size() == 2) {
                if (auto cv = ctEval().eval(node.args[1].get(), node);
                    cv && cv->type == ConstValue::STR) {
                    msg += ": " + cv->strVal;
                }
            }
            emitError(msg, node);
        }
        node.type = nullptr;
        return true;
    }

    auto v = ctEval().eval(&node, node);
    if (v && v->type == ConstValue::INT) {
        node.ctInt = v->intVal;
        node.type = ctIntTypeFor(typeCtx, v->intVal);
    } else if (v && v->type == ConstValue::DOUBLE) {
        node.ctFloat = v->doubleVal;
        node.type = typeCtx->getFloat64();
    } else if (v && v->type == ConstValue::STR) {
        // STR 结果逃逸（理由同链形钩子）。
        emitError("compile-time string value cannot be used in runtime context", node);
        node.type = nullptr;
    } else if (v) {
        // CHAR：有运行时类型，防御回退（评审 I2）。
        node.type = nullptr;
    } else {
        // static_assert/size_of 族/if 由 CT-02/CT-06 后续任务接管；此处
        // type=nullptr，外层类型检查兜底。
        node.type = nullptr;
    }
    return true;
}


// P1-04 评审 I1: 以已折叠实参解释 constexpr 函数体（DEC-05 共享内核；
// 实参的 CT 折叠由 CompileTimeEvaluator 负责，避免整树委托互递归）。
std::optional<SemanticAnalyzer::ConstValue>
SemanticAnalyzer::evalConstexprCallCT(CallExprAST& call,
                                      const std::vector<ConstValue>& argValues) {
    std::string calleeName = call.callee;
    auto it = constexprFunctions.find(calleeName);
    if (it == constexprFunctions.end()) {
        calleeName = resolveNamespaceName(calleeName);
        it = constexprFunctions.find(calleeName);
    }
    if (it == constexprFunctions.end()) {
        return std::nullopt;
    }
    FunctionDeclAST* fn = it->second;
    if (!fn->body || argValues.size() != fn->params.size()) {
        return std::nullopt;
    }
    ConstEnv env = activeEnv ? *activeEnv : ConstEnv{};
    for (size_t i = 0; i < fn->params.size(); ++i) {
        env[fn->params[i]->name] = argValues[i];
    }
    ConstEnv* saved = activeEnv;
    activeEnv = &env;
    constexprCallDepth++;
    auto result = evalConstexprStmt(fn->body.get(), env, 1);
    constexprCallDepth--;
    activeEnv = saved;
    return result;
}

std::optional<SemanticAnalyzer::ConstValue> SemanticAnalyzer::evaluateConstexpr(ExprAST* expr) {
    if (!expr) return std::nullopt;

    // P1-04 / CT-12: 表达式树含 compile_time 根 → 整树委托编译期求值器
    // （含 target/build 值与字符串运算；DEC-05 共享内核由此对接）。
    if (containsCompileTimeRoot(expr)) {
        return evalCompileTime(expr, *expr);
    }

    if (auto* num = dynamic_cast<NumberExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::INT;
        cv.intVal = num->value;
        return cv;
    }

    if (auto* chr = dynamic_cast<CharExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::CHAR;
        cv.charVal = chr->value;
        return cv;
    }

    if (auto* flt = dynamic_cast<FloatExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::DOUBLE;
        cv.doubleVal = flt->value;
        return cv;
    }

    if (auto* bin = dynamic_cast<BinaryExprAST*>(expr)) {
        auto left = evaluateConstexpr(bin->left.get());
        auto right = evaluateConstexpr(bin->right.get());
        if (!left || !right) return std::nullopt;

        if (left->type == ConstValue::INT && right->type == ConstValue::INT) {
            ConstValue cv;
            cv.type = ConstValue::INT;
            switch (bin->op) {
                case BinaryOp::Add: cv.intVal = left->intVal + right->intVal; break;
                case BinaryOp::Sub: cv.intVal = left->intVal - right->intVal; break;
                case BinaryOp::Mul: cv.intVal = left->intVal * right->intVal; break;
                case BinaryOp::Div:
                    if (right->intVal == 0) return std::nullopt;
                    cv.intVal = left->intVal / right->intVal;
                    break;
                case BinaryOp::Mod:
                    if (right->intVal == 0) return std::nullopt;
                    cv.intVal = left->intVal % right->intVal;
                    break;
                case BinaryOp::Eq: cv.intVal = left->intVal == right->intVal; break;
                case BinaryOp::NotEq: cv.intVal = left->intVal != right->intVal; break;
                case BinaryOp::Lt: cv.intVal = left->intVal < right->intVal; break;
                case BinaryOp::Gt: cv.intVal = left->intVal > right->intVal; break;
                case BinaryOp::Le: cv.intVal = left->intVal <= right->intVal; break;
                case BinaryOp::Ge: cv.intVal = left->intVal >= right->intVal; break;
                case BinaryOp::And: cv.intVal = left->intVal && right->intVal; break;
                case BinaryOp::Or: cv.intVal = left->intVal || right->intVal; break;
                case BinaryOp::BitAnd: cv.intVal = left->intVal & right->intVal; break;
                case BinaryOp::BitOr: cv.intVal = left->intVal | right->intVal; break;
                case BinaryOp::BitXor: cv.intVal = left->intVal ^ right->intVal; break;
                case BinaryOp::LShift: cv.intVal = left->intVal << right->intVal; break;
                case BinaryOp::RShift: cv.intVal = left->intVal >> right->intVal; break;
                default: return std::nullopt;
            }
            return cv;
        }

        if (left->type == ConstValue::DOUBLE && right->type == ConstValue::DOUBLE) {
            ConstValue cv;
            cv.type = ConstValue::DOUBLE;
            switch (bin->op) {
                case BinaryOp::Add: cv.doubleVal = left->doubleVal + right->doubleVal; break;
                case BinaryOp::Sub: cv.doubleVal = left->doubleVal - right->doubleVal; break;
                case BinaryOp::Mul: cv.doubleVal = left->doubleVal * right->doubleVal; break;
                case BinaryOp::Div:
                    if (right->doubleVal == 0.0) return std::nullopt;
                    cv.doubleVal = left->doubleVal / right->doubleVal;
                    break;
                case BinaryOp::Eq: cv.intVal = left->doubleVal == right->doubleVal; cv.type = ConstValue::INT; break;
                case BinaryOp::NotEq: cv.intVal = left->doubleVal != right->doubleVal; cv.type = ConstValue::INT; break;
                case BinaryOp::Lt: cv.intVal = left->doubleVal < right->doubleVal; cv.type = ConstValue::INT; break;
                case BinaryOp::Gt: cv.intVal = left->doubleVal > right->doubleVal; cv.type = ConstValue::INT; break;
                case BinaryOp::Le: cv.intVal = left->doubleVal <= right->doubleVal; cv.type = ConstValue::INT; break;
                case BinaryOp::Ge: cv.intVal = left->doubleVal >= right->doubleVal; cv.type = ConstValue::INT; break;
                default: return std::nullopt;
            }
            return cv;
        }

        return std::nullopt;
    }

    if (auto* unary = dynamic_cast<UnaryExprAST*>(expr)) {
        auto operand = evaluateConstexpr(unary->operand.get());
        if (!operand) return std::nullopt;

        if (operand->type == ConstValue::INT) {
            ConstValue cv;
            cv.type = ConstValue::INT;
            switch (unary->op) {
                case UnaryOp::Plus: cv.intVal = operand->intVal; break;
                case UnaryOp::Minus: cv.intVal = -operand->intVal; break;
                case UnaryOp::Not: cv.intVal = !operand->intVal; break;
                case UnaryOp::BitNot: cv.intVal = ~operand->intVal; break;
                default: return std::nullopt;
            }
            return cv;
        }
        return std::nullopt;
    }

    // Explicit cast in a constant expression, e.g. `(int)SomeEnumerator`
    // (TYP-20 requires the cast for enum -> int).
    if (auto* cast = dynamic_cast<CastExprAST*>(expr)) {
        auto operand = evaluateConstexpr(cast->expr.get());
        if (!operand) return std::nullopt;
        Type* to = stripTypedef(cast->castType);
        if (!to) return std::nullopt;

        ConstValue cv;
        if (isFloatType(to)) {
            double d = 0.0;
            switch (operand->type) {
                case ConstValue::INT:    d = static_cast<double>(operand->intVal); break;
                case ConstValue::CHAR:   d = static_cast<double>(operand->charVal); break;
                case ConstValue::DOUBLE: d = operand->doubleVal; break;
            }
            cv.type = ConstValue::DOUBLE;
            cv.doubleVal = d;
            return cv;
        }
        if (isIntegerType(to)) {
            long long v = 0;
            switch (operand->type) {
                case ConstValue::INT:    v = operand->intVal; break;
                case ConstValue::CHAR:   v = operand->charVal; break;
                case ConstValue::DOUBLE: v = static_cast<long long>(operand->doubleVal); break;
            }
            if (to->kind == TypeKind::Char) {
                cv.type = ConstValue::CHAR;
                cv.charVal = static_cast<char>(v);
            } else {
                cv.type = ConstValue::INT;
                cv.intVal = static_cast<int>(v);
            }
            return cv;
        }
        return std::nullopt;
    }

    if (auto* var = dynamic_cast<VariableExprAST*>(expr)) {
        // A parameter/local of the constexpr function being interpreted shadows
        // any module-level constexpr variable.
        if (activeEnv) {
            auto envIt = activeEnv->find(var->name);
            if (envIt != activeEnv->end()) {
                return envIt->second;
            }
        }
        auto it = constexprValues.find(var->name);
        if (it != constexprValues.end()) {
            return it->second;
        }
        // Enumerator constant.
        for (const auto& candidate : namespaceCandidates(var->name)) {
            auto eit = enumConstants.find(candidate);
            if (eit != enumConstants.end()) {
                ConstValue cv;
                cv.type = ConstValue::INT;
                cv.intVal = eit->second.second;
                return cv;
            }
        }
        return std::nullopt;
    }

    if (auto* call = dynamic_cast<CallExprAST*>(expr)) {
        return evalConstexprCall(*call, 0);
    }

    return std::nullopt;
}

bool SemanticAnalyzer::constValueTruthy(const ConstValue& v) {
    switch (v.type) {
        case ConstValue::INT:    return v.intVal != 0;
        case ConstValue::CHAR:   return v.charVal != 0;
        case ConstValue::DOUBLE: return v.doubleVal != 0.0;
    }
    return false;
}

std::optional<SemanticAnalyzer::ConstValue>
SemanticAnalyzer::evalConstexprCall(CallExprAST& call, int depth) {
    if (depth > kConstexprMaxDepth || constexprCallDepth >= kConstexprMaxDepth) {
        return std::nullopt; // runaway recursion: fall back to runtime evaluation
    }
    // The call may not have been through visit(CallExprAST) yet (e.g. a
    // `constexpr` initializer is folded before its subexpressions are typed),
    // so resolve a namespace-qualified callee here as well.
    std::string calleeName = call.callee;
    auto it = constexprFunctions.find(calleeName);
    if (it == constexprFunctions.end()) {
        calleeName = resolveNamespaceName(calleeName);
        it = constexprFunctions.find(calleeName);
    }
    if (it == constexprFunctions.end()) {
        return std::nullopt; // not a constexpr function (e.g. a libc call)
    }
    FunctionDeclAST* fn = it->second;
    if (!fn->body || call.args.size() != fn->params.size()) {
        return std::nullopt;
    }

    // Evaluate arguments in the caller's environment, then bind parameters.
    ConstEnv env = activeEnv ? *activeEnv : ConstEnv{};
    for (size_t i = 0; i < fn->params.size(); ++i) {
        auto arg = evaluateConstexpr(call.args[i].get());
        if (!arg) {
            return std::nullopt;
        }
        env[fn->params[i]->name] = *arg;
    }

    ConstEnv* saved = activeEnv;
    activeEnv = &env;
    constexprCallDepth++;
    auto result = evalConstexprStmt(fn->body.get(), env, depth + 1);
    constexprCallDepth--;
    activeEnv = saved;
    return result;
}

std::optional<SemanticAnalyzer::ConstValue>
SemanticAnalyzer::evalConstexprStmt(StmtAST* stmt, ConstEnv& env, int depth) {
    if (!stmt || depth > kConstexprMaxDepth) {
        return std::nullopt;
    }
    (void)env;

    if (auto* ret = dynamic_cast<ReturnStmtAST*>(stmt)) {
        ConstValue zero;
        zero.type = ConstValue::INT;
        zero.intVal = 0;
        return ret->value ? evaluateConstexpr(ret->value.get()) : std::optional<ConstValue>(zero);
    }

    if (auto* block = dynamic_cast<CompoundStmtAST*>(stmt)) {
        for (auto& inner : block->stmts) {
            if (auto result = evalConstexprStmt(inner.get(), env, depth + 1)) {
                return result;
            }
        }
        return std::nullopt;
    }

    if (auto* ifs = dynamic_cast<IfStmtAST*>(stmt)) {
        auto cond = evaluateConstexpr(ifs->cond.get());
        if (!cond) {
            return std::nullopt;
        }
        if (constValueTruthy(*cond)) {
            return evalConstexprStmt(ifs->thenStmt.get(), env, depth + 1);
        }
        return evalConstexprStmt(ifs->elseStmt.get(), env, depth + 1);
    }

    if (auto* ds = dynamic_cast<DeclStmtAST*>(stmt)) {
        if (auto* vd = dynamic_cast<VarDeclAST*>(ds->decl.get())) {
            if (vd->initExpr) {
                if (auto value = evaluateConstexpr(vd->initExpr.get())) {
                    env[vd->name] = *value;
                }
            }
        }
        return std::nullopt;
    }

    if (auto* es = dynamic_cast<ExprStmtAST*>(stmt)) {
        evaluateConstexpr(es->expr.get());
        return std::nullopt;
    }

    return std::nullopt;
}

void SemanticAnalyzer::visit(NumberExprAST& node) {
    // LEX-15: the literal kind chooses the fixed-width type; fall back to int32.
    node.type = typeForLiteralKind(node.literalKind);
    if (!node.type) node.type = typeCtx->getInt32();
    node.isLValue = false;
}

void SemanticAnalyzer::visit(FloatExprAST& node) {
    // LEX-15: unsuffixed floats default to float64.
    node.type = typeForLiteralKind(node.literalKind);
    if (!node.type) node.type = typeCtx->getFloat64();
    node.isLValue = false;
}

void SemanticAnalyzer::visit(CharExprAST& node) {
    node.type = typeCtx->getChar();
    node.isLValue = false;
}

void SemanticAnalyzer::visit(StringExprAST& node) {
    node.type = new Type(TypeKind::Pointer, typeCtx->getChar());
    node.isLValue = false;
}

void SemanticAnalyzer::visit(VariableExprAST& node) {
    // PAR-17: static 方法内无 this——钉死诊断（非 static 方法里 `this` 是
    // 隐式插入的参数，按普通变量解析）。
    if (node.name == "this" && m_inStaticMethod) {
        emitError("'this' is not valid in a static method", node);
        node.type = nullptr;
        node.isLValue = false;
        return;
    }
    // AGG-10/DS5: qualified static-member access — access check against the
    // declaring class before the ordinary symbol resolution proceeds.
    checkStaticMemberAccess(node.name, node);

    // Enumerator (possibly namespace-qualified, e.g. `A::Red`)?
    for (const auto& candidate : namespaceCandidates(node.name)) {
        auto it = enumConstants.find(candidate);
        if (it != enumConstants.end()) {
            node.name = candidate;
            node.isEnumConstant = true;
            node.enumValue = it->second.second;
            node.type = it->second.first;
            node.isLValue = false;
            return;
        }
    }

    const std::string originalName = node.name;
    const std::string key = resolveNamespaceName(node.name);
    Symbol* sym = currentScope->lookup(key);
    if (!sym && key != originalName) {
        sym = lookup(originalName);
    }
    if (sym) {
        sym->isUsed = true; // SEM-11: any reference counts as a use (W3002).
    }
    if (!sym) {
        emitError(DiagnosticCode::SemUndeclaredIdentifier, "use of undeclared identifier '" + originalName + "'", node);
        node.type = nullptr;
    } else if (sym->type && sym->type->kind == TypeKind::Function) {
        // A function used as a value decays to a function pointer.
        auto* funcType = static_cast<FunctionType*>(sym->type);
        node.name = key;
        node.isFunctionRef = true;
        node.resolvedFunctionName = mangleFunction(node.name, funcType->paramTypes);
        node.type = new Type(TypeKind::Pointer, funcType);
        node.isLValue = false;
        return;
    } else {
        node.name = key;
        node.type = sym->type;
    }
    node.isLValue = true;
}

void SemanticAnalyzer::visit(BinaryExprAST& node) {
    // P1-04 / CT-12: 表达式树含 compile_time 根 → 优先整树编译期折叠。
    if (containsCompileTimeRoot(&node)) {
        size_t baseErrors = getErrors().size();
        auto v = evalCompileTime(&node, node);
        if (v && (v->type == ConstValue::INT || v->type == ConstValue::DOUBLE)) {
            node.ctHandled = true;
            node.isLValue = false;
            if (v->type == ConstValue::INT) {
                node.ctInt = v->intVal;
                node.type = ctIntTypeFor(typeCtx, v->intVal);
            } else {
                node.ctFloat = v->doubleVal;
                node.type = typeCtx->getFloat64();
            }
            return;
        }
        if (v && v->type == ConstValue::STR) {
            // STR 逃逸到运行时上下文（spec §2 意图为类型错误）。
            emitError("compile-time string value cannot be used in runtime context", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        // CHAR 有运行时类型：回退普通路径（子节点钩子已产常量）——评审 I2。
        if (getErrors().size() > baseErrors) {
            // 求值器已诊断（未知成员/深度超限）：就地报错，不回退。
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        // 混入运行时操作数且无诊断：回退普通路径——CT 子节点由各自钩子
        // 求值成常量，本节点作普通运行时表达式处理。
    }

    Type* leftType = getExprType(*node.left);
    Type* rightType = getExprType(*node.right);

    // Check for operator overloading on aggregate-like types (struct/union/
    // class/Optional/Result). 评审 I1: Optional/Result must take this path —
    // falling through to the generic comparison switch emitted ICmp on a
    // struct and aborted the compiler.
    auto isOverloadOperand = [](Type* t) {
        while (t && t->kind == TypeKind::Typedef)
            t = static_cast<TypedefType*>(t)->aliasedType;
        if (!t) return false;
        switch (t->kind) {
            case TypeKind::Struct:
            case TypeKind::Class:
            case TypeKind::Union:
            case TypeKind::Optional:
            case TypeKind::Result:
                return true;
            default:
                return false;
        }
    };
    if (isOverloadOperand(leftType) || isOverloadOperand(rightType)) {
        std::string mangledName = getOperatorMangledName(node.op, leftType, rightType);
        if (!mangledName.empty()) {
            // Look up by the unmangled operator name (e.g., "operator+")
            std::string opName;
            switch (node.op) {
                case BinaryOp::Add: opName = "operator+"; break;
                case BinaryOp::Sub: opName = "operator-"; break;
                case BinaryOp::Mul: opName = "operator*"; break;
                case BinaryOp::Div: opName = "operator/"; break;
                case BinaryOp::Eq: opName = "operator=="; break;
                case BinaryOp::NotEq: opName = "operator!="; break;
                case BinaryOp::Lt: opName = "operator<"; break;
                case BinaryOp::Gt: opName = "operator>"; break;
                case BinaryOp::Le: opName = "operator<="; break;
                case BinaryOp::Ge: opName = "operator>="; break;
                default: opName = ""; break;
            }
            OverloadSet* overloadSet = currentScope->lookupOverload(opName);
            if (overloadSet && !overloadSet->empty()) {
                std::vector<Type*> argTypes = {leftType, rightType};
                Symbol* resolved = overloadSet->resolve(argTypes);
                if (resolved) {
                    // Store the mangled name for codegen
                    node.mangledCallee = mangledName;
                    node.type = leftType; // Return type will be resolved during codegen
                    node.isLValue = false;
                    return;
                } else {
                    emitError("no matching " + binaryOpToString(node.op) + " operator for types '" 
                        + typeToString(leftType) + "' and '" + typeToString(rightType) + "'", node);
                    node.type = nullptr;
                    node.isLValue = false;
                    return;
                }
            } else {
                emitError("no matching " + binaryOpToString(node.op) + " operator for types '" 
                    + typeToString(leftType) + "' and '" + typeToString(rightType) + "'", node);
                node.type = nullptr;
                node.isLValue = false;
                return;
            }
        }
    }

    node.type = checkBinaryTypes(node.op, leftType, rightType, node);
    node.isLValue = false;
}

void SemanticAnalyzer::visit(UnaryExprAST& node) {
    // P1-04 / CT-12: 表达式树含 compile_time 根 → 优先整树编译期折叠。
    if (containsCompileTimeRoot(&node)) {
        size_t baseErrors = getErrors().size();
        auto v = evalCompileTime(&node, node);
        if (v && (v->type == ConstValue::INT || v->type == ConstValue::DOUBLE)) {
            node.ctHandled = true;
            node.isLValue = false;
            if (v->type == ConstValue::INT) {
                node.ctInt = v->intVal;
                node.type = ctIntTypeFor(typeCtx, v->intVal);
            } else {
                node.ctFloat = v->doubleVal;
                node.type = typeCtx->getFloat64();
            }
            return;
        }
        if (v && v->type == ConstValue::STR) {
            // STR 逃逸到运行时上下文（spec §2 意图为类型错误）。
            emitError("compile-time string value cannot be used in runtime context", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        // CHAR 有运行时类型：回退普通路径（子节点钩子已产常量）——评审 I2。
        if (getErrors().size() > baseErrors) {
            // 求值器已诊断（未知成员/深度超限）：就地报错，不回退。
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        // 混入运行时操作数且无诊断：回退普通路径——CT 子节点由各自钩子
        // 求值成常量，本节点作普通运行时表达式处理。
    }

    Type* operandType = getExprType(*node.operand);

    switch (node.op) {
        case UnaryOp::Plus:
        case UnaryOp::Minus:
            if (!isArithmeticType(operandType)) {
                emitError("invalid operand to unary '" + std::string(node.op == UnaryOp::Plus ? "+" : "-") + "': '" + typeToString(operandType) + "'", node);
                node.type = nullptr;
            } else {
                node.type = operandType;
            }
            break;
        case UnaryOp::Not:
            node.type = typeCtx->getInt32();
            break;
        case UnaryOp::BitNot:
            if (!isIntegerType(operandType)) {
                emitError("invalid operand to unary '~': '" + typeToString(operandType) + "'", node);
                node.type = nullptr;
            } else {
                node.type = operandType;
            }
            break;
        case UnaryOp::Deref:
            if (operandType && operandType->kind == TypeKind::Pointer) {
                node.type = operandType->base;
            } else {
                emitError("cannot dereference non-pointer type '" + typeToString(operandType) + "'", node);
                node.type = nullptr;
            }
            break;
        case UnaryOp::AddressOf:
            if (node.operand->isLValue) {
                node.type = new Type(TypeKind::Pointer, operandType);
            } else {
                emitError("cannot take address of non-lvalue expression", node);
                node.type = nullptr;
            }
            break;
        case UnaryOp::PreInc:
        case UnaryOp::PreDec:
            if (!isArithmeticType(operandType) && !isPointerOrArray(operandType)) {
                emitError("invalid operand to '" + std::string(node.op == UnaryOp::PreInc ? "++" : "--") + "': '" + typeToString(operandType) + "'", node);
                node.type = nullptr;
            } else {
                node.type = operandType;
            }
            break;
        case UnaryOp::Sizeof:
            node.type = typeCtx->getInt32();
            break;
    }
    // A dereference denotes the pointee and is therefore an lvalue.
    node.isLValue = (node.op == UnaryOp::Deref);
}

bool SemanticAnalyzer::lowerToString(CallExprAST& node, size_t argIndex, Type* argType) {
    Type* t = argType;
    while (t && t->kind == TypeKind::Typedef) {
        t = static_cast<TypedefType*>(t)->aliasedType;
    }

    auto returnsCString = [](FunctionType* ft) {
        return ft->returnType && ft->returnType->kind == TypeKind::Pointer &&
               ft->returnType->base && ft->returnType->base->kind == TypeKind::Char;
    };

    // Preferred: a `to_string()` method on the class.
    ClassType* classType = nullptr;
    if (t && t->kind == TypeKind::Class) {
        classType = static_cast<ClassType*>(t);
    } else if (t && t->kind == TypeKind::Pointer && t->base &&
               t->base->kind == TypeKind::Class) {
        classType = static_cast<ClassType*>(t->base);
    }
    if (classType) {
        Symbol* method = resolveMethod(classType, "to_string", {});
        if (method) {
            bool usable = method->type && method->type->kind == TypeKind::Function &&
                          returnsCString(static_cast<FunctionType*>(method->type));
            delete method;
            if (usable) {
                node.args[argIndex] = std::make_unique<MethodCallExprAST>(
                    std::move(node.args[argIndex]), "to_string",
                    std::vector<std::unique_ptr<ExprAST>>{});
                getExprType(*node.args[argIndex]);
                return true;
            }
        }
    }

    // Otherwise a free function `to_string(T)`.
    OverloadSet* overloadSet = currentScope->lookupOverload("to_string");
    if (overloadSet && !overloadSet->empty()) {
        std::vector<Type*> argTypes{argType};
        Symbol* resolved = overloadSet->resolve(argTypes);
        if (resolved && resolved->type && resolved->type->kind == TypeKind::Function &&
            returnsCString(static_cast<FunctionType*>(resolved->type))) {
            auto call = std::make_unique<CallExprAST>(
                "to_string", std::vector<std::unique_ptr<ExprAST>>{});
            call->args.push_back(std::move(node.args[argIndex]));
            node.args[argIndex] = std::move(call);
            getExprType(*node.args[argIndex]);
            return true;
        }
    }

    return false;
}

bool SemanticAnalyzer::tryAnalyzePrintCall(CallExprAST& node) {
    node.type = nullptr;
    node.isLValue = false;

    if (node.args.empty()) {
        emitError("'" + node.callee + "' requires a format string argument", node);
        return true;
    }

    auto* literal = dynamic_cast<StringExprAST*>(node.args[0].get());
    if (!literal) {
        emitError("the format argument to '" + node.callee + "' must be a string literal", node);
        return true;
    }

    const bool newline = node.callee == "println";
    std::vector<PrintArgKind> kinds;
    kinds.reserve(node.args.size() - 1);

    for (size_t k = 1; k < node.args.size(); ++k) {
        Type* argType = getExprType(*node.args[k]);
        if (!argType) {
            return true; // already reported
        }

        PrintArgKind kind;
        if (builtinPrintKind(argType, kind)) {
            kinds.push_back(kind);
            continue;
        }
        if (lowerToString(node, k, argType)) {
            kinds.push_back(PrintArgKind::ToString);
            continue;
        }

        emitError("cannot format value of type '" + typeToString(argType) + "' with '{}' in '" +
                  node.callee + "'; define a to_string for it", node);
        return true;
    }

    std::string format, error;
    if (!buildPrintFormat(literal->value, kinds, newline, format, error)) {
        emitError(error, node);
        return true;
    }

    node.isPrint = true;
    node.printNewline = newline;
    node.printCFormat = std::move(format);
    node.printArgKinds = std::move(kinds);
    node.type = typeCtx->getVoid();
    node.isLValue = false;
    return true;
}

bool SemanticAnalyzer::tryAnalyzeAssertCall(CallExprAST& node) {
    node.type = typeCtx->getVoid();
    node.isLValue = false;
    if (node.args.size() != 1) {
        emitError("'assert' takes exactly one condition", node);
        return true;
    }
    Type* condType = getExprType(*node.args[0]);
    if (!condType) {
        return true; // already reported
    }
    if (!isScalarType(condType)) {
        emitError("'assert' condition must be scalar, got '" + typeToString(condType) + "'", node);
        return true;
    }
    node.isAssert = true;
    return true;
}

bool SemanticAnalyzer::tryAnalyzePanicCall(CallExprAST& node) {
    node.type = typeCtx->getVoid();
    node.isLValue = false;
    if (node.args.size() != 1) {
        emitError("'panic' takes exactly one message", node);
        return true;
    }
    Type* msgType = getExprType(*node.args[0]);
    if (!msgType) {
        return true; // already reported
    }
    if (!isPointerOrArray(msgType)) {
        emitError("'panic' message must be a C string, got '" + typeToString(msgType) + "'", node);
        return true;
    }
    node.isPanic = true;
    return true;
}

void SemanticAnalyzer::visit(CallExprAST& node) {
    // AGG-10/DS5: qualified static-method call — access check against the
    // declaring class (uses the pre-resolution callee spelling).
    checkStaticMemberAccess(node.callee, node);

    // Resolve a namespace-qualified or namespace-local callee to its mangled key.
    node.callee = resolveNamespaceName(node.callee);

    // P1-03 / GEN-03/06: 函数模板调用（显式实参或推导）。模板命中但普通
    // 函数精确匹配可行时走普通路径（SEM-16 最小规则）；诊断型失败返回
    // true 就地结束。
    if (node.hasTemplateArgs || TemplateRegistry::instance().find(node.callee)) {
        if (tryAnalyzeTemplateCall(node)) return;
    }

    // Builtin `assert`/`panic` (only when the user has not declared them).
    // They need the call site's source location, which a library function
    // cannot see without a preprocessor (STD-27 / DEC-21).
    if (node.callee == "assert" || node.callee == "panic") {
        OverloadSet* userDefined = currentScope->lookupOverload(node.callee);
        if (!userDefined || userDefined->empty()) {
            if (node.callee == "assert") {
                tryAnalyzeAssertCall(node);
            } else {
                tryAnalyzePanicCall(node);
            }
            return;
        }
    }

    // Builtin `print`/`println` (only when the user has not declared them).
    if (node.callee == "print" || node.callee == "println") {
        OverloadSet* userDefined = currentScope->lookupOverload(node.callee);
        if (!userDefined || userDefined->empty()) {
            tryAnalyzePrintCall(node);
            return;
        }
    }

    // Indirect call through a function-pointer variable?
    if (Symbol* sym = lookup(node.callee)) {
        Type* st = sym->type;
        if (st && st->kind == TypeKind::Pointer && st->base &&
            st->base->kind == TypeKind::Function) {
            auto* funcType = static_cast<FunctionType*>(st->base);
            for (auto& arg : node.args) getExprType(*arg);
            node.isIndirect = true;
            node.resolvedParamTypes = funcType->paramTypes;
            node.type = funcType->returnType;
            node.isLValue = false;
            return;
        }
    }

    FunctionType* funcType = nullptr;
    node.type = checkFunctionCall(node.callee, node.args, node, &funcType);
    if (funcType) {
        node.resolvedParamTypes = funcType->paramTypes;
    }
    node.isLValue = false;
}

// P1-03 / GEN-03/06: 函数模板调用。
// 显式实参 → 直接映射；否则从调用实参推导（形参 `T` 取实参类型、形参
// `T*` 取去指针类型）。同参冲突 / 无法推导 → 诊断。实例化 + visit 实例
// 后把 callee 重写为实例名，走普通调用解析。返回 true = 已处理。
bool SemanticAnalyzer::tryAnalyzeTemplateCall(CallExprAST& node) {
    auto& reg = TemplateRegistry::instance();
    TemplateDeclAST* tpl = reg.find(node.callee);
    if (!tpl) return false;
    auto* fnTpl = dynamic_cast<FunctionDeclAST*>(tpl->decl.get());
    if (!fnTpl) return false; // 类模板命中——让普通路径给出诊断

    // 实参类型先解析。
    std::vector<Type*> argTypes;
    for (auto& a : node.args) argTypes.push_back(getExprType(*a));

    // SEM-16 最小规则：无显式实参时，同名普通函数有可行匹配则优先。
    if (!node.hasTemplateArgs) {
        if (OverloadSet* overloads = currentScope->lookupOverload(node.callee)) {
            if (overloads->resolve(argTypes)) return false;
        }
    }

    // 评审 I1：显式实参定位后推导跳过——显式绑定单独记录（区别于推导
    // 绑定，后者仍须做冲突检测）。
    std::unordered_set<std::string> explicitlyBound;

    // typename/value 参数按声明顺序分配。
    std::vector<TemplateDeclAST::Param*> typeParams;
    std::vector<TemplateDeclAST::Param*> valueParams;
    for (auto& p : tpl->params) {
        if (p.isType) typeParams.push_back(const_cast<TemplateDeclAST::Param*>(&p));
        else valueParams.push_back(const_cast<TemplateDeclAST::Param*>(&p));
    }

    std::unordered_map<std::string, Type*> typeArgMap;
    std::unordered_map<std::string, long long> valueArgMap;
    // 显式实参。
    if (node.hasTemplateArgs) {
        if (node.explicitTemplateArgs.size() != typeParams.size() ||
            node.explicitTemplateValues.size() != valueParams.size()) {
            emitError("wrong number of template arguments for '" + node.callee + "' (expected " +
                          std::to_string(typeParams.size()) + " type(s), " +
                          std::to_string(valueParams.size()) + " value(s))",
                      node);
            return true;
        }
        for (size_t i = 0; i < typeParams.size(); ++i) {
            typeArgMap[typeParams[i]->name] = node.explicitTemplateArgs[i];
            explicitlyBound.insert(typeParams[i]->name);
        }
        for (size_t i = 0; i < valueParams.size(); ++i)
            valueArgMap[valueParams[i]->name] = node.explicitTemplateValues[i];
    }

    // 推导：逐位置匹配形参形状。
    auto deduce = [&](const std::string& name, Type* arg) -> bool {
        auto it = typeArgMap.find(name);
        if (it != typeArgMap.end()) {
            if (it->second != arg) {
                emitError("conflicting deduction for '" + name + "': '" +
                              typeToString(it->second) + "' vs '" + typeToString(arg) + "'",
                          node);
                return false;
            }
        } else {
            typeArgMap[name] = arg;
        }
        return true;
    };
    for (size_t i = 0; i < fnTpl->params.size() && i < argTypes.size(); ++i) {
        Type* p = fnTpl->params[i]->type;
        Type* a = argTypes[i];
        if (!a) continue;
        if (p && p->kind == TypeKind::TypeVar) {
            // 评审 I1：显式实参定位后推导跳过（plan 钉死：显式优先）。
            if (explicitlyBound.count(static_cast<TypeVarType*>(p)->name)) continue;
            if (!deduce(static_cast<TypeVarType*>(p)->name, a)) return true;
        } else if (p && p->kind == TypeKind::Pointer && p->base &&
                   p->base->kind == TypeKind::TypeVar) {
            if (explicitlyBound.count(static_cast<TypeVarType*>(p->base)->name)) continue;
            Type* aStripped = a;
            while (aStripped && aStripped->kind == TypeKind::Typedef)
                aStripped = static_cast<TypedefType*>(aStripped)->aliasedType;
            if (aStripped && aStripped->kind == TypeKind::Pointer) {
                if (!deduce(static_cast<TypeVarType*>(p->base)->name, aStripped->base))
                    return true;
            }
        }
    }

    // 未定的 typename 参数 → 诊断。
    for (auto* tp : typeParams) {
        if (!typeArgMap.count(tp->name)) {
            emitError("cannot deduce template argument for '" + tp->name + "'", node);
            return true;
        }
    }
    for (auto* vp : valueParams) {
        if (!valueArgMap.count(vp->name)) {
            emitError("cannot deduce template argument for '" + vp->name + "'", node);
            return true;
        }
    }

    // 实参顺序列表（按声明顺序）。
    std::vector<Type*> typeArgs;
    for (auto* tp : typeParams) typeArgs.push_back(typeArgMap[tp->name]);
    std::vector<long long> valueArgs;
    for (auto* vp : valueParams) valueArgs.push_back(valueArgMap[vp->name]);

    std::string instName = TemplateRegistry::instanceName(node.callee, typeArgs, valueArgs);
    auto* inst = reg.instantiateFunction(node.callee, typeArgs, valueArgs, argTypes);
    if (!inst) {
        emitError("cannot instantiate template '" + node.callee + "'", node);
        return true;
    }
    if (!m_visitedInstances.count(instName)) {
        m_visitedInstances.insert(instName);
        m_instStack.push_back(node.callee + "<...>");
        // 评审 C1：实例符号必须落在全局作用域——否则同一实例的第二个调用
        // 点（另一函数/另一块作用域）找不到声明。
        Scope* savedScope = currentScope;
        currentScope = globalScope.get();
        visit(*inst);
        currentScope = savedScope;
        m_instStack.pop_back();
    }

    // 调用点重写到实例符号，交普通调用解析（类型检查/绑定/codegen 名）。
    node.callee = instName;
    return false;
}

void SemanticAnalyzer::visit(AssignmentExprAST& node) {
    Type* lhsType = getExprType(*node.lhs);
    if (lhsType && lhsType->isConst) {
        emitError("cannot assign to const variable", node);
        return;
    }
    // 评审 I5: general assignability gate — an rvalue LHS (e.g. a call
    // result's pseudo-field) must not reach codegen as a store destination.
    // Skip when lhsType is null: resolution already diagnosed (e.g. E2009).
    if (node.lhs && lhsType && !node.lhs->isLValue) {
        emitError("expression is not assignable", node);
        node.type = nullptr;
        node.isLValue = false;
        return;
    }
    Type* rhsType = getExprType(*node.rhs);
    node.type = checkAssignmentTypes(lhsType, rhsType, node);
    node.isLValue = true;
}

void SemanticAnalyzer::visit(TernaryExprAST& node) {
    // P1-04 / CT-12: 表达式树含 compile_time 根 → 优先整树编译期折叠。
    if (containsCompileTimeRoot(&node)) {
        size_t baseErrors = getErrors().size();
        auto v = evalCompileTime(&node, node);
        if (v && (v->type == ConstValue::INT || v->type == ConstValue::DOUBLE)) {
            node.ctHandled = true;
            node.isLValue = false;
            if (v->type == ConstValue::INT) {
                node.ctInt = v->intVal;
                node.type = ctIntTypeFor(typeCtx, v->intVal);
            } else {
                node.ctFloat = v->doubleVal;
                node.type = typeCtx->getFloat64();
            }
            return;
        }
        if (v && v->type == ConstValue::STR) {
            // STR 逃逸到运行时上下文（spec §2 意图为类型错误）。
            emitError("compile-time string value cannot be used in runtime context", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        // CHAR 有运行时类型：回退普通路径（子节点钩子已产常量）——评审 I2。
        if (getErrors().size() > baseErrors) {
            // 求值器已诊断（未知成员/深度超限）：就地报错，不回退。
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        // 混入运行时操作数且无诊断：回退普通路径——CT 子节点由各自钩子
        // 求值成常量，本节点作普通运行时表达式处理。
    }

    Type* condType = getExprType(*node.cond);
    Type* thenType = getExprType(*node.then);
    Type* elseType = getExprType(*node.elseExpr);

    if (condType && !isScalarType(condType)) {
        emitError("ternary condition must be scalar type, but got '" + typeToString(condType) + "'", node);
    }

    // 三元根因轮：分支类型须兼容——非算术对且 typesCompatible 不成立时
    // 拒绝（旧 getCommonType 对异 kind 静默取左，`t ? s : 5` 混型不报错）。
    // Optional/Result 同型分支在此放行：codegen 已按值 phi（根因已修）。
    auto strippedBranch = [](Type* t) {
        while (t && t->kind == TypeKind::Typedef)
            t = static_cast<TypedefType*>(t)->aliasedType;
        return t;
    };
    Type* bl = strippedBranch(thenType);
    Type* br = strippedBranch(elseType);
    if (bl && br && !(isArithmeticType(bl) && isArithmeticType(br)) &&
        !typesCompatible(bl, br)) {
        emitError("ternary branches have incompatible types: '" +
                      typeToString(thenType) + "' and '" + typeToString(elseType) + "'",
                  node);
        node.type = nullptr;
        node.isLValue = false;
        return;
    }

    node.type = getCommonType(thenType, elseType);
    node.isLValue = false;
}

void SemanticAnalyzer::visit(CastExprAST& node) {
    // AGG-11/DS5: cast targets of private nested types are E2009 (the main
    // heap-allocation path is `(Outer::Secret*)malloc(...)`).
    node.castType = resolveTypeInstance(node.castType, node);
    checkNestedTypeAccess(node.castType, node);
    // P1-04 评审 I3: cast 目标不得来自 compile_time.if 死分支。
    checkCtDeadBranchUse(node.castType, node);
    Type* exprType = getExprType(*node.expr);
    if (exprType && node.castType) {
        Type* from = stripTypedef(exprType);
        Type* to = stripTypedef(node.castType);
        bool ok = true;
        const char* what = "cast";
        switch (node.castKind) {
            case CastKind::Static:
                // Arithmetic <-> arithmetic (incl. enum, which are integers) or
                // pointer/array <-> pointer/array. Pointer <-> integer must use
                // reinterpret_cast (or a C-style cast).
                ok = (isArithmeticType(from) && isArithmeticType(to)) ||
                     (isPointerOrArray(from) && isPointerOrArray(to));
                what = "static_cast";
                break;
            case CastKind::Reinterpret:
                // Any scalar <-> scalar: bit-level reinterpretation.
                ok = isScalarType(from) && isScalarType(to);
                what = "reinterpret_cast";
                break;
            case CastKind::CStyle:
            default:
                // A C-style cast is the explicit escape hatch: arithmetic <->
                // arithmetic (incl. enum, TYP-20), pointer/array <-> pointer/
                // array, and pointer <-> integer. Anything else keeps the
                // historical "incompatible cast" warning.
                ok = (isArithmeticType(from) && isArithmeticType(to)) ||
                     (isPointerOrArray(from) && isPointerOrArray(to)) ||
                     (isPointerOrArray(from) && isIntegerType(to)) ||
                     (isIntegerType(from) && isPointerOrArray(to)) ||
                     typesCompatible(exprType, node.castType);
                what = "cast";
                break;
        }
        if (!ok) {
            std::string msg;
            if (node.castKind == CastKind::CStyle) {
                // Preserve the historical C-style cast wording.
                msg = "incompatible cast from '" + typeToString(exprType) + "' to '" +
                      typeToString(node.castType) + "'";
                emitWarning(DiagnosticCode::SemIncompatibleCast, msg, node);
            } else {
                msg = std::string(what) + ": incompatible cast from '" +
                      typeToString(exprType) + "' to '" + typeToString(node.castType) + "'";
                emitError(DiagnosticCode::SemIncompatibleCast, msg, node);
            }
        }
    }
    node.type = node.castType;
    node.isLValue = false;
}

void SemanticAnalyzer::visit(CommaExprAST& node) {
    getExprType(*node.left);
    node.type = getExprType(*node.right);
    node.isLValue = node.right->isLValue;
}

void SemanticAnalyzer::visit(PostfixIncDecExprAST& node) {
    Type* operandType = getExprType(*node.operand);
    if (!isArithmeticType(operandType) && !isPointerOrArray(operandType)) {
        emitError("invalid operand to postfix '" + std::string(node.isIncrement ? "++" : "--") + "': '" + typeToString(operandType) + "'", node);
        node.type = nullptr;
    } else {
        node.type = operandType;
    }
    node.isLValue = false;
}

void SemanticAnalyzer::visit(ArrayAccessExprAST& node) {
    Type* arrayType = getExprType(*node.array);
    Type* indexType = getExprType(*node.index);

    if (!isIntegerType(indexType)) {
        emitError("array subscript must be integer, but got '" + typeToString(indexType) + "'", node);
    }

    if (arrayType && arrayType->kind == TypeKind::Array) {
        node.type = static_cast<ArrayType*>(arrayType)->elementType;
    } else if (arrayType && arrayType->kind == TypeKind::Pointer) {
        node.type = arrayType->base;
    } else if (arrayType && arrayType->kind == TypeKind::Slice) {
        // TYP-12: subscripting a slice yields an lvalue of the element type.
        node.type = static_cast<SliceType*>(arrayType)->elementType;
    } else {
        emitError("subscripted value is neither array, slice nor pointer, but '" + typeToString(arrayType) + "'", node);
        node.type = nullptr;
    }
    node.isLValue = true;
}

void SemanticAnalyzer::visit(MemberAccessExprAST& node) {
    // P1-04 / CT-04/05: compile_time 成员链特判——先于对象解析（根标识符是
    // 编译期命名空间，不是普通变量，普通路径会误报 undeclared）。
    if (tryAnalyzeCompileTimeChain(node)) {
        return;
    }

    Type* objType = getExprType(*node.object);

    if (!objType) {
        node.type = nullptr;
        node.isLValue = false;
        return;
    }

    Type* memberBaseType = nullptr;
    // Typedefs to aggregates resolve to the underlying type (AGG-17).
    Type* strippedObj = nullptr;
    Type* strippedBase = nullptr;
    {
        Type* t = objType;
        while (t && t->kind == TypeKind::Typedef) {
            t = static_cast<TypedefType*>(t)->aliasedType;
        }
        strippedObj = t;
        if (t && t->kind == TypeKind::Pointer) {
            Type* b = t->base;
            while (b && b->kind == TypeKind::Typedef) {
                b = static_cast<TypedefType*>(b)->aliasedType;
            }
            strippedBase = b;
        }
    }
    if (!strippedObj || (strippedObj->kind == TypeKind::Pointer && !strippedBase)) {
        node.type = nullptr;
        node.isLValue = false;
        return;
    }
    if (node.accessKind == MemberAccessKind::Arrow) {
        if (strippedObj->kind != TypeKind::Pointer) {
            emitError("member access with '->' requires pointer to struct/class, but got '" + typeToString(objType) + "'", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        if (strippedBase->kind != TypeKind::Struct && strippedBase->kind != TypeKind::Class &&
            strippedBase->kind != TypeKind::Union) {
            emitError("member access with '->' requires pointer to struct/class/union, but '" + typeToString(objType) + "' points to '" + typeToString(objType->base) + "'", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        memberBaseType = strippedBase;
    } else {
        // TYP-12: `.len` is the only member of a slice (D1: no `.ptr`).
        if (strippedObj->kind == TypeKind::Slice) {
            if (node.memberName == "len") {
                node.type = TypeContext::instance().getInt64();
                node.isLValue = false;
                return;
            }
            emitError("no member named '" + node.memberName + "' in slice", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        // P1-02 (TYP-13/14): Optional/Result pseudo-fields — freely readable
        // and writable (DS3, C semantics); no runtime check is implied.
        if (strippedObj->kind == TypeKind::Optional) {
            auto* optType = static_cast<OptionalType*>(strippedObj);
            // 评审 I5: propagate object lvalue-ness — `make().value = 5` must
            // not present an rvalue slot as assignable.
            if (node.memberName == "valid") {
                node.type = TypeContext::instance().getBool();
                node.isLValue = node.object->isLValue;
                return;
            }
            if (node.memberName == "value") {
                node.type = optType->elementType;
                node.isLValue = node.object->isLValue;
                return;
            }
            emitError("no member named '" + node.memberName + "' in Optional", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        if (strippedObj->kind == TypeKind::Result) {
            auto* resType = static_cast<ResultType*>(strippedObj);
            if (node.memberName == "ok") {
                node.type = TypeContext::instance().getBool();
                node.isLValue = node.object->isLValue;
                return;
            }
            if (node.memberName == "value") {
                node.type = resType->successType;
                node.isLValue = node.object->isLValue;
                return;
            }
            if (node.memberName == "error") {
                node.type = resType->errorType;
                node.isLValue = node.object->isLValue;
                return;
            }
            emitError("no member named '" + node.memberName + "' in Result", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        // PAR-17: `this.field`/`p.field`——点号对指针自动解引用（沿用
        // methodcall decay 机制）；codegen 侧本就支持指针对象。
        if (node.accessKind == MemberAccessKind::Dot && strippedObj->kind == TypeKind::Pointer &&
            strippedBase && (strippedBase->kind == TypeKind::Struct ||
                             strippedBase->kind == TypeKind::Class ||
                             strippedBase->kind == TypeKind::Union)) {
            strippedObj = strippedBase;
        }
        if (strippedObj->kind != TypeKind::Struct && strippedObj->kind != TypeKind::Class &&
            strippedObj->kind != TypeKind::Union) {
            emitError("member access with '.' requires struct/class/union type, but got '" + typeToString(objType) + "'", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        memberBaseType = strippedObj;
    }

    if (memberBaseType->kind == TypeKind::Struct) {
        auto* structType = static_cast<StructType*>(memberBaseType);
        // INH-01: walk the base chain — inherited fields resolve at their
        // defining struct (mirrors the class branch below). Depth-capped:
        // a redefinition-shaped cycle must never hang the compiler (评审 C1).
        StructType* definingStruct = nullptr;
        int walkDepth = 0;
        for (StructType* cur = structType; cur && !definingStruct;) {
            for (auto& field : cur->fields) {
                if (field.name == node.memberName) {
                    node.type = field.type;
                    node.isLValue = true;
                    definingStruct = cur;
                    break;
                }
            }
            if (definingStruct) break;
            if (cur->baseClass.empty()) break;
            Type* baseType = cur->base;
            if (!baseType || baseType->kind != TypeKind::Struct) break;
            cur = static_cast<StructType*>(baseType);
            if (++walkDepth > 64) break;
        }
        if (definingStruct) return;
        emitError("no member named '" + node.memberName + "' in struct '" + structType->name + "'", node);
    } else if (memberBaseType->kind == TypeKind::Class) {
        auto* classType = static_cast<ClassType*>(memberBaseType);
        const std::string className = classType->name;
        ClassType* definingClass = nullptr;
        // Search this class and its base classes for the field.
        // Depth-capped: a redefinition-shaped cycle must never hang the
        // compiler (评审 C1).
        int classWalkDepth = 0;
        while (classType) {
            for (auto& field : classType->fields) {
                if (field.name == node.memberName) {
                    node.type = field.type;
                    node.isLValue = true;
                    definingClass = classType;
                    break;
                }
            }
            if (definingClass) break;
            Type* baseType = classType->base;
            if (!baseType || baseType->kind != TypeKind::Class) break;
            classType = static_cast<ClassType*>(baseType);
            if (++classWalkDepth > 64) break;
        }
        // SEM-04/DEC-01: non-public members are only reachable from inside
        // the defining class (protected ≡ private until INH lands).
        if (definingClass) {
            AccessLevel level = definingClass->memberAccessLevel(node.memberName);
            if (level != AccessLevel::Public && currentClass != definingClass) {
                emitError(DiagnosticCode::SemPrivateMemberAccess,
                          "cannot access " +
                              std::string(level == AccessLevel::Private ? "private" : "protected") +
                              " member '" + node.memberName + "' of class '" + definingClass->name +
                              "' outside the class; make it public or add an accessor",
                          node);
                node.type = nullptr;
                node.isLValue = false;
            }
            return;
        }
        emitError("no member named '" + node.memberName + "' in class '" + className + "'", node);
    } else if (memberBaseType->kind == TypeKind::Union) {
        auto* unionType = static_cast<UnionType*>(memberBaseType);
        for (auto& member : unionType->members) {
            if (member.name == node.memberName) {
                node.type = member.type;
                node.isLValue = true;
                return;
            }
        }
        emitError("no member named '" + node.memberName + "' in union '" + unionType->name + "'", node);
    }
    node.type = nullptr;
    node.isLValue = false;
}

void SemanticAnalyzer::visit(MethodCallExprAST& node) {
    // P1-04 / CT-01/02: compile_time 成员调用特判（先于对象解析，理由同链形）。
    if (tryAnalyzeCompileTimeCall(node)) {
        return;
    }

    Type* objType = getExprType(*node.object);
    if (!objType) {
        node.type = nullptr;
        node.isLValue = false;
        return;
    }

    ClassType* classType = nullptr;
    if (objType->kind == TypeKind::Class) {
        classType = static_cast<ClassType*>(objType);
    } else if (objType->kind == TypeKind::Pointer && objType->base && objType->base->kind == TypeKind::Class) {
        classType = static_cast<ClassType*>(objType->base);
    }

    if (!classType) {
        emitError("cannot call method on non-class type '" + typeToString(objType) + "'", node);
        node.type = nullptr;
        node.isLValue = false;
        return;
    }

    std::vector<Type*> argTypes;
    for (auto& arg : node.args) {
        Type* argType = getExprType(*arg);
        argTypes.push_back(argType);
    }

    // INH-06（方案乙）: the class whose table provided the method (possibly a
    // base along the chain) — access levels attribute to it.
    ClassType* definingClass = nullptr;
    Symbol* method = resolveMethod(classType, node.methodName, argTypes, &definingClass);
    if (!method || method->type->kind != TypeKind::Function) {
        emitError("no matching method '" + node.methodName + "' in class '" + classType->name + "'", node);
        node.type = nullptr;
        node.isLValue = false;
        return;
    }
    // P1-03 / INH-05 / GEN-09: 基类实例方法体惰性——首次调用触发 visit
    // （this 插入 + body 检查），此时派生类方法表已完整。
    if (definingClass) visitLazyMethodsOf(definingClass->name);

    // SEM-04/DEC-01 + INH-06: non-public methods are only callable from    // inside the DEFINING class (which may be a base along the chain); the
    // level comes from the defining class's own member-access map.
    {
        AccessLevel level = definingClass->memberAccessLevel(node.methodName);
        if (level != AccessLevel::Public && currentClass != definingClass) {
            emitError(DiagnosticCode::SemPrivateMemberAccess,
                      "cannot call " +
                          std::string(level == AccessLevel::Private ? "private" : "protected") +
                          " method '" + node.methodName + "' of class '" + definingClass->name +
                          "' outside the class; make it public or add a public wrapper",
                      node);
            node.type = nullptr;
            node.isLValue = false;
            delete method;
            return;
        }
    }

    auto* funcType = static_cast<FunctionType*>(method->type);
    // Store the declared parameter types (incl. this) so codegen mangles
    // against the definition site — required for decayed arguments.
    node.resolvedParamTypes = funcType->paramTypes;
    node.type = funcType->returnType;
    node.isLValue = false;
    delete method;
}

void SemanticAnalyzer::visit(SizeofExprAST& node) {
    // Resolve the type of a `sizeof(expr)` operand; the expression itself is
    // not evaluated, but its type is needed by codegen.
    node.sizeofType = resolveTypeInstance(node.sizeofType, node);
    if (!node.sizeofType && node.expr) {
        node.sizeofType = getExprType(*node.expr);
        if (!node.sizeofType) {
            emitError("invalid operand to sizeof", node);
        }
    }
    node.type = typeCtx->getInt32();
    node.isLValue = false;
}

void SemanticAnalyzer::visit(InitializerListExprAST& node) {
    if (!node.initializers.empty()) {
        node.type = getExprType(*node.initializers[0]);
    } else {
        node.type = typeCtx->getInt32();
    }
    node.isLValue = false;
}

void SemanticAnalyzer::visit(CompoundStmtAST& node) {
    enterScope();
    bool terminated = false;
    bool warned = false;
    for (auto& stmt : node.stmts) {
        if (!stmt) {
            continue;
        }
        // SEM-11: statements after an unconditional transfer are unreachable
        // (W3003). Warn once per block; trailing `defer`s stay legal.
        if (terminated && !warned && !dynamic_cast<DeferStmtAST*>(stmt.get())) {
            emitWarning(DiagnosticCode::WarnUnreachableCode, "unreachable code", *stmt);
            warned = true;
        }
        visit(*stmt);
        if (stmtAlwaysTransfers(stmt.get())) {
            terminated = true;
        }
    }
    exitScope();
}

void SemanticAnalyzer::visit(ExprStmtAST& node) {
    if (node.expr) {
        getExprType(*node.expr);
    }
}

void SemanticAnalyzer::visit(ReturnStmtAST& node) {
    if (node.value) {
        Type* retValType = getExprType(*node.value);
        if (currentFunction && retValType) {
            // TYP-11 D4: 返回局部数组的 slice 视图是悬垂 UB（栈帧随 return
            // 消亡）。当前所有 ArrayType 值均为局部变量，保守拒绝。
            Type* fnRet = currentFunction->returnType;
            while (fnRet && fnRet->kind == TypeKind::Typedef)
                fnRet = static_cast<TypedefType*>(fnRet)->aliasedType;
            Type* val = retValType;
            while (val && val->kind == TypeKind::Typedef)
                val = static_cast<TypedefType*>(val)->aliasedType;
            if (fnRet && val && fnRet->kind == TypeKind::Slice &&
                val->kind == TypeKind::Array) {
                emitError("cannot return a slice view of an array with local storage (dangling view)", node);
                return;
            }
            if (!typesCompatible(currentFunction->returnType, retValType)) {
                emitError("return type mismatch in function '" + std::string(currentFunction->name) + "': expected '" 
                    + typeToString(currentFunction->returnType) + "', got '" + typeToString(retValType) + "'", node);
            }
        }
    } else if (currentFunction && currentFunction->returnType->kind != TypeKind::Void) {
        emitError("non-void function '" + std::string(currentFunction->name) + "' must return a value", node);
    }
}

void SemanticAnalyzer::visit(IfStmtAST& node) {
    Type* condType = getExprType(*node.cond);
    if (condType && !isScalarType(condType)) {
        emitError("if condition must be scalar type, but got '" + typeToString(condType) + "'", node);
    }
    if (node.thenStmt) visit(*node.thenStmt);
    if (node.elseStmt) visit(*node.elseStmt);
}

void SemanticAnalyzer::visit(WhileStmtAST& node) {
    Type* condType = getExprType(*node.cond);
    if (condType && !isScalarType(condType)) {
        emitError("while condition must be scalar type, but got '" + typeToString(condType) + "'", node);
    }
    if (node.body) visit(*node.body);
}

void SemanticAnalyzer::visit(DoWhileStmtAST& node) {
    Type* condType = getExprType(*node.cond);
    if (condType && !isScalarType(condType)) {
        emitError("do-while condition must be scalar type, but got '" + typeToString(condType) + "'", node);
    }
    if (node.body) visit(*node.body);
}

void SemanticAnalyzer::visit(ForStmtAST& node) {
    enterScope();
    if (node.init) visit(*node.init);
    if (node.cond) {
        Type* condType = getExprType(*node.cond);
        if (condType && !isScalarType(condType)) {
            emitError("for condition must be scalar type, but got '" + typeToString(condType) + "'", node);
        }
    }
    if (node.inc) getExprType(*node.inc);
    if (node.body) visit(*node.body);
    exitScope();
}

void SemanticAnalyzer::visit(SwitchStmtAST& node) {
    Type* condType = getExprType(*node.cond);
    if (condType && !isIntegerType(condType)) {
        emitError("switch expression must be integer type, but got '" + typeToString(condType) + "'", node);
    }
    // Type the case labels so enumerator constants and constexpr values are
    // resolved (codegen then folds them to integer constants).
    for (auto& label : node.caseLabels) {
        if (label) getExprType(*label);
    }
    for (auto& c : node.cases) {
        if (c) visit(*c);
    }
}

void SemanticAnalyzer::visit(BreakStmtAST& node) {}

void SemanticAnalyzer::visit(ContinueStmtAST& node) {}

void SemanticAnalyzer::visit(NullStmtAST& node) {}

void SemanticAnalyzer::visit(VarDeclAST& node) {
    // AGG-11/DS5: naming a private nested type outside its class is E2009.
    node.type = resolveTypeInstance(node.type, node);
    checkNestedTypeAccess(node.type, node);
    // P1-04 / CT-03 / 评审 I3: 毒化类型（含 typedef/指针/数组包装层）拒绝。
    checkCtDeadBranchUse(node.type, node);
    node.name = scopedName(node.name);
    if (node.isConstexpr) {
        if (!node.initExpr) {
            emitError("constexpr variable '" + node.name + "' must have initializer", node);
        } else {
            auto folded = evaluateConstexpr(node.initExpr.get());
            if (!folded) {
                emitError("constexpr variable '" + node.name + "' must be initialized with a constant expression", node);
            } else {
                constexprValues[node.name] = *folded;
                FoldedValue fv;
                fv.type = static_cast<FoldedValue::Type>(folded->type);
                switch (folded->type) {
                    case ConstValue::INT: fv.intVal = folded->intVal; break;
                    case ConstValue::DOUBLE: fv.doubleVal = folded->doubleVal; break;
                    case ConstValue::CHAR: fv.charVal = folded->charVal; break;
                }
                node.foldedValue = fv;
            }
        }
    }

    if (node.initExpr) {
        if (auto* initList = dynamic_cast<InitializerListExprAST*>(node.initExpr.get())) {
            initList->type = node.type;
            initList->isLValue = false;
            for (auto& e : initList->initializers) getExprType(*e);
            // P1-02 (TYP-13/14): aggregate shape check for Optional/Result —
            // arity and per-field types (DS4 layout: flag, value[, error]).
            Type* vt = node.type;
            while (vt && vt->kind == TypeKind::Typedef)
                vt = static_cast<TypedefType*>(vt)->aliasedType;
            if (vt && (vt->kind == TypeKind::Optional || vt->kind == TypeKind::Result)) {
                size_t expected = vt->kind == TypeKind::Optional ? 2 : 3;
                std::vector<Type*> fieldTypes;
                if (vt->kind == TypeKind::Optional) {
                    auto* ot = static_cast<OptionalType*>(vt);
                    fieldTypes = {TypeContext::instance().getBool(), ot->elementType};
                } else {
                    auto* rt = static_cast<ResultType*>(vt);
                    fieldTypes = {TypeContext::instance().getBool(), rt->successType,
                                  rt->errorType};
                }
                if (initList->initializers.size() != expected) {
                    emitError("initializer list for '" + node.name + "' has " +
                                  std::to_string(initList->initializers.size()) +
                                  " elements, expected " + std::to_string(expected),
                              node);
                } else {
                    for (size_t i = 0; i < fieldTypes.size(); ++i) {
                        // Nested brace lists recurse through codegen's
                        // emitAggregateInitializer (struct precedent: no sema
                        // element-level check); only scalar slots are typed.
                        if (dynamic_cast<InitializerListExprAST*>(
                                initList->initializers[i].get())) {
                            continue;
                        }
                        // getExprType (not ->type): element types are computed
                        // lazily and the node field is not yet populated here.
                        Type* it = getExprType(*initList->initializers[i]);
                        if (!it) continue;
                        // Flag fields (valid/ok) follow the language's normal
                        // bool-assignment channel (`true` literals are int32
                        // in this language, C-style). Value/error fields are
                        // positional literal slots: exact typesEqual — else
                        // `{5, true}` would silently accept bool as the value.
                        bool okField;
                        if (i == 0) {
                            // Flag fields (valid/ok) follow the language's normal
                            // bool-assignment channel (`true` literals are int32
                            // in this language, C-style).
                            okField = typesCompatible(fieldTypes[i], it);
                        } else {
                            // Value/error slots are positional literal slots:
                            // exact match after stripping typedefs on BOTH
                            // sides (评审 I2: typesEqual does not strip —
                            // `Optional<MyInt> o = {true, 5}` was mis-rejected).
                            Type* ft = fieldTypes[i];
                            while (ft && ft->kind == TypeKind::Typedef)
                                ft = static_cast<TypedefType*>(ft)->aliasedType;
                            Type* its = it;
                            while (its && its->kind == TypeKind::Typedef)
                                its = static_cast<TypedefType*>(its)->aliasedType;
                            okField = typesEqual(ft, its);
                        }
                        if (!okField) {
                            emitError("type mismatch in initializer " +
                                          std::to_string(i) + " of '" + node.name +
                                          "': expected '" + typeToString(fieldTypes[i]) +
                                          "', got '" + typeToString(it) + "'",
                                      node);
                        }
                    }
                }
            }
        } else {
            Type* initType = getExprType(*node.initExpr);
            if (initType && !typesCompatible(node.type, initType)) {
                emitError("type mismatch in initialization of '" + node.name + "': expected '" 
                    + typeToString(node.type) + "', got '" + typeToString(initType) + "'", node);
            }
        }
    }
    if (!declare(node.name, node.type)) {
        emitError(DiagnosticCode::SemRedefinition, "redeclaration of variable '" + node.name + "' in the same scope", node);
    }
    // SEM-11: remember local variables for the unused-variable check (W3002).
    if (currentFunction) {
        if (Symbol* sym = currentScope->lookup(node.name)) {
            sym->checkUnused = true;
            sym->declFile = node.sourceFile;
            sym->declLine = node.sourceLine;
            sym->declColumn = node.sourceColumn;
        }
    }
}

void SemanticAnalyzer::visit(ArrayDeclAST& node) {
    // AGG-11 评审 I2: array declarations are var declarations (DS5) — a
    // private nested type must not slip through as the element type.
    node.elementType = resolveTypeInstance(node.elementType, node);
    checkNestedTypeAccess(node.elementType, node);
    // P1-04 评审 I3: 元素类型不得来自 compile_time.if 死分支。
    checkCtDeadBranchUse(node.elementType, node);
    node.name = scopedName(node.name);
    if (auto* initList = dynamic_cast<InitializerListExprAST*>(node.initExpr.get())) {
        if (node.size == 0) {
            node.size = static_cast<int>(initList->initializers.size());
        }
    }
    Type* arrayType = new ArrayType(node.elementType, node.size);
    if (node.initExpr) {
        if (auto* initList = dynamic_cast<InitializerListExprAST*>(node.initExpr.get())) {
            initList->type = arrayType;
            initList->isLValue = false;
            for (auto& e : initList->initializers) getExprType(*e);
        } else {
            // A non-list initializer cannot populate an array: at global scope
            // it was silently zero-filled, locally it stored a scalar into an
            // array alloca. Require a brace list ( elementType checks happen
            // per element during aggregate initialization).
            emitError("array initializer must be a brace-enclosed list", node);
        }
    }
    if (!declare(node.name, arrayType)) {
        emitError("redeclaration of array '" + node.name + "' in the same scope", node);
    }
    // SEM-11: remember local arrays for the unused-variable check (W3002).
    if (currentFunction) {
        if (Symbol* sym = currentScope->lookup(node.name)) {
            sym->checkUnused = true;
            sym->declFile = node.sourceFile;
            sym->declLine = node.sourceLine;
            sym->declColumn = node.sourceColumn;
        }
    }
}

void SemanticAnalyzer::visitNestedDecl(DeclAST& node) {
    if (auto* sd = dynamic_cast<StructDeclAST*>(&node)) {
        visit(*sd);
    } else if (auto* ed = dynamic_cast<EnumDeclAST*>(&node)) {
        visit(*ed);
    } else if (auto* ud = dynamic_cast<UnionDeclAST*>(&node)) {
        visit(*ud);
    }
}

// AGG-11/DS4: nested types are analyzed before static members and method
// bodies (same ordering rule as codegen). The caller has already pushed the
// owning class's bare-name path onto classPathPrefix, so desugared
// static-member symbols inside nested classes match what the qualified
// access spelling (`ns::Outer::Inner::v`) flattens to.
void SemanticAnalyzer::visitNestedTypeDecls(
    std::vector<std::unique_ptr<DeclAST>>& nestedTypes, ClassType* owner) {
    for (auto& nested : nestedTypes) {
        if (!nested) continue;
        std::string flatName;
        std::string bareNested;
        if (auto* sd = dynamic_cast<StructDeclAST*>(nested.get())) {
            flatName = sd->name;
            bareNested = sd->bareName;
        } else if (auto* ed = dynamic_cast<EnumDeclAST*>(nested.get())) {
            flatName = ed->name;
            bareNested = ed->bareName;
        } else if (auto* ud = dynamic_cast<UnionDeclAST*>(nested.get())) {
            flatName = ud->name;
            bareNested = ud->bareName;
        }
        if (bareNested.empty()) bareNested = flatName;
        // DS5: record the access level so E2009 checks can find it. Struct
        // and union nested types are always public (owner == null).
        if (owner) {
            nestedTypeAccess[flatName] =
                {owner, owner->memberAccessLevel("type:" + bareNested)};
        }
        visitNestedDecl(*nested);
    }
}

// AGG-11/DS5: a private nested type may only be named from inside its
// defining class — checked at var declarations and cast targets.
void SemanticAnalyzer::checkNestedTypeAccess(Type* type, const ASTNode& site) {
    if (!type) return;
    Type* t = stripTypedef(type);
    // AGG-11 评审 I2: arrays are named-type carriers too (`NTP::Secret a[3]`).
    while (t && (t->kind == TypeKind::Pointer || t->kind == TypeKind::Array)) {
        // Array types are ArrayType objects (codebase convention, cf.
        // typeToString); pointer element is `base`.
        t = stripTypedef(t->kind == TypeKind::Pointer
                             ? t->base
                             : static_cast<ArrayType*>(t)->elementType);
    }
    if (!t) return;
    std::string name;
    switch (t->kind) {
        case TypeKind::Class: name = static_cast<ClassType*>(t)->name; break;
        case TypeKind::Struct: name = static_cast<StructType*>(t)->name; break;
        case TypeKind::Union: name = static_cast<UnionType*>(t)->name; break;
        case TypeKind::Enum: name = static_cast<EnumType*>(t)->name; break;
        default: return;
    }
    auto it = nestedTypeAccess.find(name);
    if (it == nestedTypeAccess.end()) return;
    if (it->second.second == AccessLevel::Public) return;
    if (currentClass == it->second.first) return;
    emitError(DiagnosticCode::SemPrivateMemberAccess,
              "cannot access private type '" + name + "' of class '" +
                  it->second.first->name +
                  "' outside the class; make it public or add an accessor",
              site);
}

void SemanticAnalyzer::visit(StructDeclAST& node) {
    m_definingStack.push_back(node.name);
    visitStructDeclImpl(node);
    m_definingStack.pop_back();
}

void SemanticAnalyzer::visitStructDeclImpl(StructDeclAST& node) {
    // P1-03 / GEN-03: 实例字段/方法签名中的实例占位先解析。
    for (auto& f : node.fields) {
        f.type = resolveTypeInstance(f.type, node);
        // P1-04 评审 I3: 字段类型不得来自 compile_time.if 死分支。
        checkCtDeadBranchUse(f.type, node);
    }
    for (auto& m : node.methods) {
        m->returnType = resolveTypeInstance(m->returnType, node);
        for (auto& p : m->params) p->type = resolveTypeInstance(p->type, node);
    }
    // P1-03 / INH-05 / GEN-09: 实例字段值语义自嵌套（CRTP 的 `D next`）拒绝。
    checkInstanceFieldComplete(node);
    // PAR-04/DEC-01: `class` declarations are classes even without methods —
    // their members default to private and carry access levels.
    // INH-01: struct declarations with a base stay structs — only classes
    // (isClassDecl) or method-bearing declarations route to the class branch.
    bool isClass = node.isClassDecl || !node.methods.empty();

    if (isClass) {
        auto* classType = typeCtx->getOrCreateClass(node.name);
        // Redef 轮: a definition completes the class; forward declarations
        // leave the placeholder incomplete. A second definition (or a
        // definition colliding with any other completed kind) is E2004.
        if (!node.isForwardDecl) {
            if (isTypeRedefined(node.name)) {
                emitError(DiagnosticCode::SemRedefinition, "redefinition of type '" + node.name + "'", node);
            }
            classType->isComplete = true;
        }

        // Add fields if not already added
        for (auto& field : node.fields) {
            if (!classType->getFieldType(field.name)) {
                classType->addField(field.name, field.type);
            }
        }

        // P1-03 / INH-05 / GEN-09: 模板实例基类（CRTP）——baseClass 拼写为
        // `Shape<Circle>`。实参在此刻解析（派生类自身的前向占位 ClassType
        // 已由 getOrCreateClass 创建，可作实参）；实例方法体惰性——派生类
        // 方法表未就绪，延迟到首次调用（见 visit(MethodCallExprAST)）。
        if (node.baseClass.find('<') != std::string::npos) {
            size_t lt = node.baseClass.find('<');
            std::string tplName = node.baseClass.substr(0, lt);
            std::string argsSpelling = node.baseClass.substr(lt + 1, node.baseClass.size() - lt - 2);
            std::vector<Type*> baseArgs;
            std::string cur;
            auto flushArg = [&]() {
                if (cur.empty()) return;
                Type* argType = resolveTypeByName(cur);
                if (!argType) {
                    emitError("unknown template base class argument '" + cur + "' of '" +
                                  node.baseClass + "'",
                              node);
                }
                baseArgs.push_back(argType);
                cur.clear();
            };
            for (size_t i = 0; i <= argsSpelling.size(); ++i) {
                if (i == argsSpelling.size() || argsSpelling[i] == ',') flushArg();
                else if (argsSpelling[i] != ' ') cur += argsSpelling[i];
            }
            if (!baseArgs.empty() && std::all_of(baseArgs.begin(), baseArgs.end(), [](Type* t) { return t; })) {
                std::string instName = TemplateRegistry::instanceName(tplName, baseArgs, {});
                auto* inst = TemplateRegistry::instance().instantiateClass(tplName, baseArgs, {});
                if (!inst) {
                    emitError("cannot instantiate template base class '" + node.baseClass + "'", node);
                    return;
                }
                if (!m_visitedInstances.count(instName)) {
                    m_visitedInstances.insert(instName);
                    m_lazyInstanceMethods.insert(instName);
                    m_instStack.push_back(tplName + "<...>");
                    visit(*inst);
                    m_instStack.pop_back();
                }
                node.baseClass = instName;
            }
        }

        if (!node.baseClass.empty()) {
            auto* baseType = typeCtx->getClass(node.baseClass);
            if (!baseType) {
                emitError("base class '" + node.baseClass + "' of class '" + node.name + "' not found", node);
            } else if (hasCircularInheritance(node.name, node.baseClass)) {
                emitError("circular inheritance detected involving class '" + node.name + "'", node);
            } else if (!baseType->isComplete) {
                // INH-01: a forward-declared (placeholder) base has no
                // definition yet — deriving from it is rejected, mirroring
                // C++'s incomplete-type rule.
                emitError("base class '" + node.baseClass + "' of class '" + node.name +
                              "' is incomplete; define it before deriving from it",
                          node);
            } else {
                // INH-06（方案乙）: no method-table copy — resolveMethod walks
                // the base chain, so inherited methods keep their defining
                // class, access level and definition-site signature.
                classType->baseClass = node.baseClass;
                classType->base = baseType;
            }
        }

        // P1-03 / INH-05 / GEN-09: 基类实例的方法体惰性——派生类方法表未
        // 就绪，只登记签名，body 延迟到首次调用（visit(MethodCallExprAST)
        // 触发）。非实例路径不变。
        const bool lazyMethods = m_lazyInstanceMethods.count(node.name) > 0;

        for (auto& method : node.methods) {
            // AGG-10/DS1: static methods are not instance members — they
            // never enter the class method table, so instance calls reject.
            if (method->isStatic) continue;
            // Only add if not already present
            if (!classType->getMethod(method->name)) {
                std::vector<Type*> paramTypes;
                paramTypes.push_back(new Type(TypeKind::Pointer, classType));
                for (auto& param : method->params) {
                    paramTypes.push_back(param->type);
                }
                auto* methodType = new FunctionType(method->returnType, std::move(paramTypes));
                classType->addMethod(method->name, methodType);
            }
            if (lazyMethods) {
                m_pendingLazyMethods[node.name].push_back(method.get());
            }
        }

        typeCtx->addClass(node.name, classType);

        // PAR-04/DEC-01: carry the parser-recorded access levels into the type.
        for (const auto& kv : node.memberAccess) {
            classType->setMemberAccess(kv.first, kv.second);
        }

        // AGG-11/DS4: nested types first (before static members and methods).
        // classPathPrefix is extended with THIS class's bare name while its
        // nested types are visited (their members resolve against it).
        std::string savedPath = classPathPrefix;
        classPathPrefix += mangleNamespaceName(
            node.bareName.empty() ? node.name : node.bareName) + "_";
        visitNestedTypeDecls(node.nestedTypes, classType);
        classPathPrefix = savedPath;

        // AGG-10/DS2+DS3: static data members become globals under the
        // desugared key; the in-class initializer is their definition.
        // Declared BEFORE method bodies are analyzed so method bodies can
        // reference them (declaration order matters in this pipeline).
        for (auto& vd : node.staticMembers) {
            if (!vd) continue;
            std::string origName = vd->name;
            std::string clsPath = classPathPrefix +
                (node.bareName.empty() ? node.name : node.bareName);
            vd->name = mangleNamespaceName(clsPath + "::" + origName);
            staticMemberIndex[scopedName(vd->name)] = {classType, origName};
            ClassType* savedClass = currentClass;
            currentClass = classType;
            visit(*vd);
            currentClass = savedClass;
        }

        // AGG-10/I2: register all static methods BEFORE analyzing any method
        // body, so a static method may call a sibling declared later in the
        // class body (same declaration-order freedom as instance methods,
        // whose table is filled below). declare() tolerates the later
        // re-declaration inside visit() (prototype pattern); only two
        // definitions of one signature error, via definedFunctions.
        for (auto& method : node.methods) {
            if (!method || !method->isStatic) continue;
            std::string origName = method->name;
            std::string clsPath = classPathPrefix +
                (node.bareName.empty() ? node.name : node.bareName);
            method->name = mangleNamespaceName(clsPath + "::" + origName);
            staticMemberIndex[scopedName(method->name)] = {classType, origName};
            std::vector<Type*> pts;
            for (auto& param : method->params) pts.push_back(param->type);
            declare(scopedName(method->name),
                    new FunctionType(method->returnType, std::move(pts), method->isVarArg));
        }

        for (auto& method : node.methods) {
            // P1-03 / INH-05: 基类实例方法体惰性——首次调用时 visit。
            if (lazyMethods) continue;
            ClassType* savedClass = currentClass;
            currentClass = classType;
            if (method->isStatic) {
                // AGG-10/DS3+DS4: desugar to a global function symbol
                // `Class_method`; no `this` parameter is inserted. Qualified
                // call sites resolve via the existing namespace machinery.
                // The desugared name uses the BARE class name — scopedName
                // (applied inside visit) adds the namespace prefix, matching
                // what the fully qualified access spelling flattens to.
                m_inStaticMethod = true;
                visit(*method);
                m_inStaticMethod = false;
            } else {
                auto* thisType = new Type(TypeKind::Pointer, classType);
                auto thisParam = std::make_unique<ParamDeclAST>("this", thisType);
                method->params.insert(method->params.begin(), std::move(thisParam));
                visit(*method);
            }
            currentClass = savedClass;
        }
    } else {
        // INH-01: reuse the parse-time registration when present — variable
        // declarations already point at that StructType object, so the base
        // wiring must land on it (mirrors getOrCreateClass on the class path).
        auto* structType = typeCtx->getStruct(node.name);
        if (!structType) {
            structType = new StructType(node.name);
        }
        for (auto& field : node.fields) {
            if (!structType->getFieldType(field.name)) {
                structType->addField(field.name, field.type);
            }
        }
        // INH-01: single public inheritance — wire the base before layout
        // (codegen puts the base sub-object in field slot 0) and validate it
        // like the class branch does.
        // Redef 轮: mirror of the class branch — duplicate definition of the
        // same name (any kind) is E2004.
        if (!node.isForwardDecl) {
            if (isTypeRedefined(node.name)) {
                emitError(DiagnosticCode::SemRedefinition, "redefinition of type '" + node.name + "'", node);
            }
            structType->isComplete = true;
        }
        if (!node.baseClass.empty()) {
            auto* baseType = typeCtx->getStruct(node.baseClass);
            if (!baseType) {
                emitError("base struct '" + node.baseClass + "' of struct '" + node.name + "' not found", node);
            } else if (hasCircularInheritance(node.name, node.baseClass)) {
                emitError("circular inheritance detected involving struct '" + node.name + "'", node);
            } else if (!baseType->isComplete) {
                emitError("base struct '" + node.baseClass + "' of struct '" + node.name +
                              "' is incomplete; define it before deriving from it",
                          node);
            } else {
                structType->baseClass = node.baseClass;
                structType->base = baseType;
            }
        }
        typeCtx->addStruct(node.name, structType);
        // AGG-11/DS4: nested types first (structs' nested types are public).
        std::string savedPath = classPathPrefix;
        classPathPrefix += mangleNamespaceName(
            node.bareName.empty() ? node.name : node.bareName) + "_";
        visitNestedTypeDecls(node.nestedTypes, nullptr);
        classPathPrefix = savedPath;
        // AGG-10: struct static data members — same desugar; no access-level
        // index (struct members are always public).
        for (auto& vd : node.staticMembers) {
            if (!vd) continue;
            std::string clsPath = classPathPrefix +
                (node.bareName.empty() ? node.name : node.bareName);
            vd->name = mangleNamespaceName(clsPath + "::" + vd->name);
            visit(*vd);
        }
    }
}

void SemanticAnalyzer::visit(UnionDeclAST& node) {
    // Redef 轮: reuse an existing registration for named unions (mirrors the
    // struct branch) so the completion state is observable across visits.
    auto* unionType = node.name.empty() ? nullptr : typeCtx->getUnion(node.name);
    if (!unionType) {
        unionType = new UnionType(node.name);
        typeCtx->addUnion(node.name, unionType);
    }
    for (auto& member : node.members) {
        unionType->addMember(member.name, member.type);
    }
    // Redef 轮: duplicate definition of the same name (any kind) is E2004.
    if (!node.isForwardDecl && !node.name.empty()) {
        if (isTypeRedefined(node.name)) {
            emitError(DiagnosticCode::SemRedefinition, "redefinition of type '" + node.name + "'", node);
        }
        unionType->isComplete = true;
    }
    // AGG-11/DS4: nested types first (unions' nested types are public).
    std::string savedPath = classPathPrefix;
    classPathPrefix += mangleNamespaceName(
        node.bareName.empty() ? node.name : node.bareName) + "_";
    visitNestedTypeDecls(node.nestedTypes, nullptr);
    classPathPrefix = savedPath;
}

void SemanticAnalyzer::visit(EnumDeclAST& node) {
    // Reuse the enum type registered by the parser (which carries the explicit
    // underlying type) so variable references and this declaration agree.
    EnumType* enumType = node.name.empty() ? nullptr : typeCtx->getEnum(node.name);
    if (!enumType) {
        enumType = new EnumType(node.name);
        if (!node.name.empty()) {
            typeCtx->addEnum(node.name, enumType);
        }
    }
    // Redef 轮: duplicate definition of the same name (any kind) is E2004.
    if (!node.isForwardDecl && !node.name.empty()) {
        if (isTypeRedefined(node.name)) {
            emitError(DiagnosticCode::SemRedefinition, "redefinition of type '" + node.name + "'", node);
        }
        enumType->isComplete = true;
    }
    // TYP-09/TYP-25: an explicit underlying type must be an integer type.
    if (node.underlyingType) {
        if (!isIntegerType(node.underlyingType) ||
            stripTypedef(node.underlyingType)->kind == TypeKind::Enum) {
            emitError("enum '" + node.name + "' underlying type must be an integer type", node);
        } else {
            enumType->underlyingType = node.underlyingType;
        }
    }
    enumType->values.clear();
    for (auto& val : node.values) {
        enumType->addValue(val.first, val.second);
        // Enumerators live in the enclosing (namespace) scope, so `A::Red`
        // resolves to the same key as `Red` used inside namespace A.
        // AGG-11/DS4: a class-nested enum's constants register under the
        // flattened enclosing-class path (`Outer::Red` -> `Outer_Red`); the
        // bare key is NOT registered, so `Red` stays unusable outside.
        std::string key = classPathPrefix.empty()
            ? scopedName(val.first)
            : scopedName(mangleNamespaceName(classPathPrefix + val.first));
        enumConstants[key] = {enumType, val.second};
    }
}

void SemanticAnalyzer::visit(TypedefDeclAST& node) {
    typeCtx->addTypedef(node.name, node.aliasedType);
    // A typedef of an inline enum (`typedef enum { A, B } E;`) exposes its
    // enumerators in the enclosing scope (TYP-25).
    if (Type* t = stripTypedef(node.aliasedType); t && t->kind == TypeKind::Enum) {
        auto* enumType = static_cast<EnumType*>(t);
        for (auto& val : enumType->values) {
            enumConstants[scopedName(val.first)] = {enumType, val.second};
        }
    }
}

void SemanticAnalyzer::visit(ForwardDeclAST& node) {}

void SemanticAnalyzer::visit(DeclStmtAST& node) {
    if (node.decl) {
        if (auto* varDecl = dynamic_cast<VarDeclAST*>(node.decl.get())) {
            visit(*varDecl);
        } else if (auto* arrDecl = dynamic_cast<ArrayDeclAST*>(node.decl.get())) {
            visit(*arrDecl);
        } else if (auto* structDecl = dynamic_cast<StructDeclAST*>(node.decl.get())) {
            visit(*structDecl);
        } else if (auto* unionDecl = dynamic_cast<UnionDeclAST*>(node.decl.get())) {
            visit(*unionDecl);
        } else if (auto* enumDecl = dynamic_cast<EnumDeclAST*>(node.decl.get())) {
            visit(*enumDecl);
        } else if (auto* typedefDecl = dynamic_cast<TypedefDeclAST*>(node.decl.get())) {
            visit(*typedefDecl);
        } else if (auto* fwdDecl = dynamic_cast<ForwardDeclAST*>(node.decl.get())) {
            visit(*fwdDecl);
        } else if (auto* multi = dynamic_cast<MultiVarDeclAST*>(node.decl.get())) {
            for (auto& d : multi->decls) {
                if (auto* v = dynamic_cast<VarDeclAST*>(d.get())) visit(*v);
                else if (auto* a = dynamic_cast<ArrayDeclAST*>(d.get())) visit(*a);
            }
        }
    }
}

void SemanticAnalyzer::visit(FunctionDeclAST& node) {
    // Namespace members are registered/emitted under a mangled key.
    node.name = scopedName(node.name);
    // P1-03 / GEN-03: 返回类型与参数类型中的实例占位先解析。
    node.returnType = resolveTypeInstance(node.returnType, node);
    for (auto& p : node.params) p->type = resolveTypeInstance(p->type, node);
    // P1-04 评审 I3: 返回/参数类型不得来自 compile_time.if 死分支。
    checkCtDeadBranchUse(node.returnType, node);
    for (auto& p : node.params) checkCtDeadBranchUse(p->type, node);

    // Validate constexpr function constraints
    if (node.isConstexpr) {
        // Return type must be arithmetic (literal type)
        if (!isIntegerType(node.returnType) && !isFloatType(node.returnType)) {
            emitError("constexpr function '" + node.name + "' must have literal return type", node);
        }

        // All parameters must be arithmetic types
        for (auto& param : node.params) {
            if (!isIntegerType(param->type) && !isFloatType(param->type)) {
                emitError("constexpr function '" + node.name + "' parameter '" + param->name + "' must have literal type", node);
            }
        }
    }

    if (node.isConstexpr) {
        // Available to the compile-time evaluator (evaluateConstexpr).
        constexprFunctions[node.name] = &node;
    }

    std::vector<Type*> paramTypes;
    for (auto& param : node.params) {
        paramTypes.push_back(param->type);
    }
    auto* funcType = new FunctionType(node.returnType, std::move(paramTypes), node.isVarArg);
    std::string signature = mangleFunction(node.name, funcType->paramTypes);
    if (!declare(node.name, funcType)) {
        // A matching declaration already exists. That is legal for repeated
        // prototypes or a prototype followed by its definition; only two
        // definitions of the same signature are an error.
        if (node.body && definedFunctions.count(signature)) {
            emitError(DiagnosticCode::SemRedefinition, "redefinition of function '" + node.name + "'", node);
        } else if (node.body) {
            definedFunctions.insert(signature);
        }
    } else if (node.body) {
        definedFunctions.insert(signature);
    }

    FunctionDeclAST* prevFunc = currentFunction;
    currentFunction = &node;
    enterScope();

    for (auto& param : node.params) {
        if (!declare(param->name, param->type)) {
            emitError("redeclaration of parameter '" + param->name + "' in function '" + node.name + "'", *param);
        }
    }

    if (node.body) {
        visit(*node.body);
        checkInitialization(node);
    }

    exitScope();
    currentFunction = prevFunc;
}

void SemanticAnalyzer::visit(CompileTimeIfDeclAST& node) {
    // P1-04 / CT-03: 条件求值与分支选择（诊断文案 spec §3 钉死）。
    auto cond = evalCompileTime(node.cond.get(), node);
    if (!cond) {
        emitError("compile_time.if condition must be a compile-time constant", node);
        return;
    }
    if (cond->type != ConstValue::INT) {
        emitError("compile_time.if condition must be a boolean", node);
        return;
    }
    node.ctResolved = true;
    node.selectedThen = cond->intVal != 0;

    auto& deadDecls = node.selectedThen ? node.elseDecls : node.thenDecls;
    auto& liveDecls = node.selectedThen ? node.thenDecls : node.elseDecls;
    auto collectTypeNames = [](std::vector<std::unique_ptr<DeclAST>>& ds) {
        std::unordered_set<std::string> names;
        for (auto& d : ds) {
            if (auto* s = dynamic_cast<StructDeclAST*>(d.get())) names.insert(s->name);
            else if (auto* u = dynamic_cast<UnionDeclAST*>(d.get())) names.insert(u->name);
            else if (auto* e = dynamic_cast<EnumDeclAST*>(d.get())) names.insert(e->name);
        }
        return names;
    };
    auto liveNames = collectTypeNames(liveDecls);
    // 未选分支 parse 期注册的类型占位须撤销/毒化（Review Focus 1：占位不得
    // 被活代码静默使用）。类型引用在 parse 期已绑定为指针，故除 TypeContext
    // 注销外还须毒化对象；活分支的 sema 注册创建新对象，天然解毒。两分支
    // 都声明的同名类型（变体选择）不支持——毒化使使用处得到明确诊断。
    for (auto& d : deadDecls) {
        if (auto* s = dynamic_cast<StructDeclAST*>(d.get())) {
            TypeContext& tc = TypeContext::instance();
            Type* poison = s->isClassDecl
                ? static_cast<Type*>(tc.getClass(s->name))
                : static_cast<Type*>(tc.getStruct(s->name));
            if (!liveNames.count(s->name)) {
                if (s->isClassDecl) tc.removeClass(s->name);
                else tc.removeStruct(s->name);
            }
            if (poison) poison->ctDeadBranch = true;
        } else if (auto* u = dynamic_cast<UnionDeclAST*>(d.get())) {
            TypeContext& tc = TypeContext::instance();
            Type* poison = tc.getUnion(u->name);
            if (!liveNames.count(u->name)) tc.removeUnion(u->name);
            if (poison) poison->ctDeadBranch = true;
        } else if (auto* e = dynamic_cast<EnumDeclAST*>(d.get())) {
            TypeContext& tc = TypeContext::instance();
            Type* poison = tc.getEnum(e->name);
            if (!liveNames.count(e->name)) tc.removeEnum(e->name);
            if (poison) poison->ctDeadBranch = true;
        } else if (auto* t = dynamic_cast<TypedefDeclAST*>(d.get())) {
            // 评审 I3: typedef 别名毒化（别名包装层在检查中穿透）。
            if (Type* poison = TypeContext::instance().getTypedef(t->name)) {
                poison->ctDeadBranch = true;
            }
        } else if (auto* u2 = dynamic_cast<UsingDeclAST*>(d.get())) {
            if (Type* poison = TypeContext::instance().getTypedef(u2->name)) {
                poison->ctDeadBranch = true;
            }
        }
    }

    auto& decls = node.selectedThen ? node.thenDecls : node.elseDecls;
    for (auto& d : decls) {
        if (d) analyzeTopLevelDecl(*d);
    }
}

void SemanticAnalyzer::analyzeTopLevelDecl(DeclAST& decl) {
    // Visibility applies per declaration (MOD-05/06). `main` is always
    // externally visible (program entry point); an exported namespace makes
    // all of its members exported.
    bool savedExported = currentDeclExported;
    bool isEntry = false;
    if (auto* fn = dynamic_cast<FunctionDeclAST*>(&decl)) {
        if (fn->name == "main") isEntry = true;
    }
    currentDeclExported = decl.isExported || namespaceExported || isEntry;

    if (auto* funcDecl = dynamic_cast<FunctionDeclAST*>(&decl)) {
        visit(*funcDecl);
    } else if (auto* varDecl = dynamic_cast<VarDeclAST*>(&decl)) {
        visit(*varDecl);
    } else if (auto* arrDecl = dynamic_cast<ArrayDeclAST*>(&decl)) {
        visit(*arrDecl);
    } else if (auto* structDecl = dynamic_cast<StructDeclAST*>(&decl)) {
        visit(*structDecl);
    } else if (auto* unionDecl = dynamic_cast<UnionDeclAST*>(&decl)) {
        visit(*unionDecl);
    } else if (auto* enumDecl = dynamic_cast<EnumDeclAST*>(&decl)) {
        visit(*enumDecl);
    } else if (auto* typedefDecl = dynamic_cast<TypedefDeclAST*>(&decl)) {
        visit(*typedefDecl);
    } else if (auto* fwdDecl = dynamic_cast<ForwardDeclAST*>(&decl)) {
        visit(*fwdDecl);
    } else if (auto* usingDecl = dynamic_cast<UsingDeclAST*>(&decl)) {
        visit(*usingDecl);
    } else if (auto* typeDecl = dynamic_cast<TypeDeclAST*>(&decl)) {
        visit(*typeDecl);
    } else if (auto* moduleDecl = dynamic_cast<ModuleDeclAST*>(&decl)) {
        visit(*moduleDecl);
    } else if (auto* nsDecl = dynamic_cast<NamespaceDeclAST*>(&decl)) {
        visit(*nsDecl);
    } else if (auto* tplDecl = dynamic_cast<TemplateDeclAST*>(&decl)) {
        // P1-03 / GEN-01: 模板定义只注册，不在此 sema（GEN-06 定义处不检查）。
        TemplateRegistry::instance().registerTemplate(tplDecl);
    } else if (auto* multi = dynamic_cast<MultiVarDeclAST*>(&decl)) {
        for (auto& d : multi->decls) {
            if (auto* v = dynamic_cast<VarDeclAST*>(d.get())) visit(*v);
            else if (auto* a = dynamic_cast<ArrayDeclAST*>(d.get())) visit(*a);
        }
    } else if (auto* ctAssert = dynamic_cast<CompileTimeAssertDeclAST*>(&decl)) {
        // P1-04 / CT-02: 顶层 static_assert——内层 MethodCall 走钩子求值。
        if (auto* call = dynamic_cast<MethodCallExprAST*>(ctAssert->call.get())) {
            visit(*call);
        }
    } else if (auto* ctIf = dynamic_cast<CompileTimeIfDeclAST*>(&decl)) {
        // P1-04 / CT-03: 条件编译——选中分支原位展开进声明序列。
        visit(*ctIf);
    }

    currentDeclExported = savedExported;
}

void SemanticAnalyzer::visit(TranslationUnitAST& node) {
    Scope* savedScope = currentScope;
    std::string savedModule = currentModule;
    Scope* savedActive = activeModuleScope;

    for (auto& decl : node.declarations) {
        if (!decl) continue;
        // Switch to the owning module's scope for this declaration group.
        enterModuleContext(decl->moduleName);
        analyzeTopLevelDecl(*decl);
    }

    currentScope = savedScope;
    currentModule = savedModule;
    activeModuleScope = savedActive;
}

void SemanticAnalyzer::visit(NamespaceDeclAST& node) {
    std::string saved = namespacePrefix;
    bool savedExported = namespaceExported;
    if (node.isExported) {
        namespaceExported = true;
    }
    std::string segment = mangleNamespaceName(node.name);
    if (!segment.empty()) {
        namespacePrefix += segment + "_";
    }
    for (auto& decl : node.declarations) {
        if (decl) {
            analyzeTopLevelDecl(*decl);
        }
    }
    namespacePrefix = saved;
    namespaceExported = savedExported;
}

void SemanticAnalyzer::analyze(TranslationUnitAST& ast) {
    errors.clear();
    visit(ast);

    // P1-03 / GEN-05: 克隆期嵌套使用点（模板体内 `Box<f64>` 字段）产生的
    // 待检实例——补 visit 并全部追加到翻译单元尾部供 codegen 出码。惰性
    // 保证：从未使用的模板不产生任何实例或符号。
    auto& pending = TemplateRegistry::instance().pendingInstances();
    while (!pending.empty()) {
        std::unique_ptr<DeclAST> decl = std::move(pending.front());
        pending.pop_front();
        if (auto* st = dynamic_cast<StructDeclAST*>(decl.get())) {
            if (!m_visitedInstances.count(st->name)) {
                m_visitedInstances.insert(st->name);
                visit(*st);
            }
        } else if (auto* fn = dynamic_cast<FunctionDeclAST*>(decl.get())) {
            if (!m_visitedInstances.count(fn->name)) {
                m_visitedInstances.insert(fn->name);
                visit(*fn);
            }
        }
        ast.declarations.push_back(std::move(decl));
    }
}

// P1-03 / GEN-03: 解析类型树中的 TypeInstance 占位——递归解析嵌套实参、
// 触发实例化、visit 实例 decl（补全 TypeContext 中的实例类型），原地把
// 占位节点替换为具体类型。sema 结束后 TypeInstance 不复存在。
Type* SemanticAnalyzer::resolveTypeInstance(Type* t, ASTNode& at) {
    if (!t) return nullptr;
    switch (t->kind) {
        case TypeKind::Pointer:
            t->base = resolveTypeInstance(t->base, at);
            return t;
        case TypeKind::Array: {
            auto* arr = static_cast<ArrayType*>(t);
            arr->elementType = resolveTypeInstance(arr->elementType, at);
            if (!arr->sizeParam.empty()) {
                emitError("unbound non-type template parameter '" + arr->sizeParam +
                              "' outside template body",
                          at);
                arr->sizeParam.clear();
            }
            return t;
        }
        case TypeKind::Slice: {
            auto* s = static_cast<SliceType*>(t);
            s->elementType = resolveTypeInstance(s->elementType, at);
            return t;
        }
        case TypeKind::Optional: {
            auto* o = static_cast<OptionalType*>(t);
            o->elementType = resolveTypeInstance(o->elementType, at);
            return t;
        }
        case TypeKind::Result: {
            auto* r = static_cast<ResultType*>(t);
            r->successType = resolveTypeInstance(r->successType, at);
            r->errorType = resolveTypeInstance(r->errorType, at);
            return t;
        }
        case TypeKind::Typedef: {
            auto* td = static_cast<TypedefType*>(t);
            td->aliasedType = resolveTypeInstance(td->aliasedType, at);
            return t;
        }
        case TypeKind::TypeInstance: {
            auto* use = static_cast<TypeInstanceType*>(t);
            // 评审 I2：真实 parser→sema 路径上的深度上限（嵌套实参在此自底
            // 向上解析，Registry 的克隆链计不到它们）。单出口：递归深度
            // 由 lambda 外的计数器守护。
            ++m_useDepth;
            if (m_useDepth > kMaxTemplateUseDepth) {
                emitError("template instantiation depth limit exceeded (64)", at);
                --m_useDepth;
                return nullptr;
            }
            Type* result = resolveTypeInstanceUse(use, at);
            --m_useDepth;
            return result;
        }
        case TypeKind::Struct: {
            auto* st = static_cast<StructType*>(t);
            if (st->isTemplatePattern) {                emitError("use of template '" + st->name + "' requires template arguments", at);
                return nullptr;
            }
            return t;
        }
        case TypeKind::Class: {
            auto* ct = static_cast<ClassType*>(t);
            if (ct->isTemplatePattern) {
                emitError("use of template '" + ct->name + "' requires template arguments", at);
                return nullptr;
            }
            return t;
        }
        default:
            return t;
    }
}

// P1-03 / INH-05: 模板基类实参拼写 → 类型。按裸名与 scoped 名在各类型表
// 中查找；CRTP 场景下派生类自身的占位 ClassType 已由 getOrCreateClass 建。
Type* SemanticAnalyzer::resolveTypeByName(const std::string& name) {
    auto tryOne = [&](const std::string& n) -> Type* {
        if (Type* t = typeCtx->getClass(n)) return t;
        if (Type* t = typeCtx->getStruct(n)) return t;
        if (Type* t = typeCtx->getTypedef(n)) return t;
        if (Type* t = typeCtx->getEnum(n)) return t;
        if (Type* t = typeCtx->getUnion(n)) return t;
        return nullptr;
    };
    if (Type* t = tryOne(name)) return t;
    std::string scoped = scopedName(name);
    if (scoped != name) return tryOne(scoped);
    return nullptr;
}

// 别名展开产物中的实例类型：若其实例 decl 已克隆但未 visit，立即补齐
// （字段访问要求布局当场完整）。
// P1-03 / GEN-03: TypeInstance 占位的解析主体（深度计数在调用方）。
Type* SemanticAnalyzer::resolveTypeInstanceUse(TypeInstanceType* use, ASTNode& at) {
    // 嵌套实例实参先解析（Box<Box<i32>> 的内层先行完成）。
    for (auto*& arg : use->typeArgs) arg = resolveTypeInstance(arg, at);

    auto& reg = TemplateRegistry::instance();
    std::string name =
        TemplateRegistry::instanceName(use->templateName, use->typeArgs, use->valueArgs);

    // 别名模板：展开目标类型（键缓存防重复展开），产物中的实例
    // 类型立即补 visit——字段访问要求布局当场完整。
    auto* tpl = reg.find(use->templateName);
    if (tpl && dynamic_cast<UsingDeclAST*>(tpl->decl.get())) {
        if (m_aliasCache.count(name)) return m_aliasCache[name];
        std::unordered_map<std::string, Type*> typeArgMap;
        std::unordered_map<std::string, long long> valueArgMap;
        size_t ti = 0, vi = 0;
        for (auto& p : tpl->params) {
            if (p.isType) {
                if (ti < use->typeArgs.size()) typeArgMap[p.name] = use->typeArgs[ti++];
            } else {
                if (vi < use->valueArgs.size()) valueArgMap[p.name] = use->valueArgs[vi++];
            }
        }
        Type* expanded = TemplateInstantiator(typeArgMap, valueArgMap)
                             .rewrite(
                                 static_cast<UsingDeclAST*>(tpl->decl.get())->aliasedType);
        m_aliasCache[name] = expanded;
        ensureInstanceVisited(expanded, at);
        return expanded;
    }

    auto* decl = reg.instantiateClass(use->templateName, use->typeArgs, use->valueArgs);
    if (!decl) {
        emitError("cannot instantiate template '" + use->templateName + "'", at);
        return nullptr;
    }

    // 同一实例只 visit 一次（去重键已在 Registry 保证 decl 唯一）。
    if (!m_visitedInstances.count(name)) {
        m_visitedInstances.insert(name);
        m_instStack.push_back(use->templateName + "<...>");
        visit(*static_cast<StructDeclAST*>(decl));
        m_instStack.pop_back();
    }

    if (auto* ct = typeCtx->getClass(name)) return ct;
    return typeCtx->getStruct(name);
}

void SemanticAnalyzer::ensureInstanceVisited(Type* t, ASTNode& at) {
    if (!t) return;
    std::string name;
    if (t->kind == TypeKind::Struct) name = static_cast<StructType*>(t)->name;
    else if (t->kind == TypeKind::Class) name = static_cast<ClassType*>(t)->name;
    else return;
    if (name.find('$') == std::string::npos) return;
    auto* decl = TemplateRegistry::instance().instanceDeclFor(name);
    if (decl && !m_visitedInstances.count(name)) {
        m_visitedInstances.insert(name);
        m_instStack.push_back(name);
        visit(*static_cast<StructDeclAST*>(decl));
        m_instStack.pop_back();
    }
}

// P1-03 / INH-05 / GEN-09: 基类实例惰性方法的首次调用触发——this 插入与
// body 检查在此进行（与 class 分支的方法循环一致）。
void SemanticAnalyzer::visitLazyMethodsOf(const std::string& className) {
    auto it = m_pendingLazyMethods.find(className);
    if (it == m_pendingLazyMethods.end()) return;
    auto pending = std::move(it->second);
    m_pendingLazyMethods.erase(it);
    m_lazyInstanceMethods.erase(className);

    auto* classType = typeCtx->getClass(className);
    if (!classType) return;
    ClassType* savedClass = currentClass;
    currentClass = classType;
    for (auto* method : pending) {
        if (method->isStatic) {
            m_inStaticMethod = true;
            visit(*method);
            m_inStaticMethod = false;
        } else {
            auto thisType = new Type(TypeKind::Pointer, classType);
            method->params.insert(method->params.begin(),
                                  std::make_unique<ParamDeclAST>("this", thisType));
            visit(*method);
        }
    }
    currentClass = savedClass;
}

// CRTP：模板基类实例的字段若为派生类值语义自嵌套（`D next`），在实例化点
// 拒绝——类型尺寸无限。
// 评审 I3 + INH-05：类/结构体字段值语义自引用（`A x` / CRTP 的 `D next`）
// ——类型尺寸无限，诊断而非编译器崩溃。模板实例化中沿用原文案。
void SemanticAnalyzer::checkInstanceFieldComplete(StructDeclAST& node) {
    for (auto& field : node.fields) {
        Type* ft = field.type;
        while (ft && ft->kind == TypeKind::Typedef) {
            ft = static_cast<TypedefType*>(ft)->aliasedType;
        }
        if (!ft || (ft->kind != TypeKind::Class && ft->kind != TypeKind::Struct)) continue;
        std::string fname = ft->kind == TypeKind::Class
                                ? static_cast<ClassType*>(ft)->name
                                : static_cast<StructType*>(ft)->name;
        if (std::find(m_definingStack.begin(), m_definingStack.end(), fname) !=
            m_definingStack.end()) {
            if (!m_instStack.empty()) {
                emitError("recursive instantiation of template '" + m_instStack.back() +
                              "': field '" + field.name + "' has value type '" + fname + "'",
                          node);
            } else {
                emitError("field '" + field.name + "' of '" + fname +
                              "' cannot have its own type by value",
                          node);
            }
            return;
        }
    }
}

void SemanticAnalyzer::visit(ExprAST& expr) {
    if (auto* e = dynamic_cast<NumberExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<FloatExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<CharExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<StringExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<VariableExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<BinaryExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<UnaryExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<CallExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<AssignmentExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<TernaryExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<CastExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<CommaExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<PostfixIncDecExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<ArrayAccessExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<MemberAccessExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<MethodCallExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<SizeofExprAST*>(&expr)) { visit(*e); return; }
    if (auto* e = dynamic_cast<InitializerListExprAST*>(&expr)) { visit(*e); return; }
}

void SemanticAnalyzer::visit(StmtAST& stmt) {
    if (auto* s = dynamic_cast<CompoundStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<ExprStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<ReturnStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<IfStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<WhileStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<DoWhileStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<ForStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<SwitchStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<BreakStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<ContinueStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<NullStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<DeclStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<DeferStmtAST*>(&stmt)) { visit(*s); return; }
}

bool SemanticAnalyzer::isTypeRedefined(const std::string& name) const {
    // Redef 轮: all four type kinds share one name namespace — a completed
    // type of any kind blocks a (re)definition of the same name.
    if (auto* t = typeCtx->getClass(name); t && t->isComplete) return true;
    if (auto* t = typeCtx->getStruct(name); t && t->isComplete) return true;
    if (auto* t = typeCtx->getUnion(name); t && t->isComplete) return true;
    if (auto* t = typeCtx->getEnum(name); t && t->isComplete) return true;
    return false;
}

bool SemanticAnalyzer::hasCircularInheritance(const std::string& className, const std::string& baseClass) const {
    std::vector<std::string> chain;
    chain.push_back(className);

    std::string current = baseClass;
    while (!current.empty()) {
        if (std::find(chain.begin(), chain.end(), current) != chain.end()) {
            return true;
        }
        chain.push_back(current);

        // INH-01 评审 C1: walk BOTH maps — a struct chain must be followed
        // too, otherwise a redefinition-shaped cycle is accepted silently.
        std::string next;
        if (auto* type = typeCtx->getClass(current)) {
            next = type->baseClass;
        } else if (auto* stype = typeCtx->getStruct(current)) {
            next = stype->baseClass;
        }
        current = next;
    }

    return false;
}

Symbol* SemanticAnalyzer::resolveMethod(ClassType* classType, const std::string& methodName,
                                        const std::vector<Type*>& argTypes,
                                        ClassType** defining, int depth) {
    for (auto& method : classType->methods) {
        if (method.first == methodName) {
            if (method.second->paramTypes.size() - 1 == argTypes.size()) {
                bool match = true;
                for (size_t i = 0; i < argTypes.size(); ++i) {
                    if (method.second->paramTypes[i + 1] != argTypes[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    Symbol* sym = new Symbol(methodName, method.second);
                    if (defining) *defining = classType;
                    return sym;
                }
            }
        }
    }

    // Phase 2: decay/implicit conversions — first candidate (declaration
    // order) where every argument converts to the declared parameter type
    // (conversionRank >= 0). A null argument type disqualifies the candidate
    // (the error was already reported when resolving the argument).
    for (auto& method : classType->methods) {
        if (method.first != methodName) continue;
        auto* funcType = method.second;
        if (funcType->paramTypes.size() - 1 != argTypes.size()) continue;
        bool match = true;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (!argTypes[i] ||
                conversionRank(argTypes[i], funcType->paramTypes[i + 1]) < 0) {
                match = false;
                break;
            }
        }
        if (match) {
            Symbol* sym = new Symbol(methodName, funcType);
            if (defining) *defining = classType;
            return sym;
        }
    }

    if (!classType->baseClass.empty() && depth < 64) {
        auto* baseType = typeCtx->getClass(classType->baseClass);
        if (baseType) {
            Symbol* result = resolveMethod(baseType, methodName, argTypes, defining, depth + 1);
            if (result) return result;
        }
    }

    return nullptr;
}

bool SemanticAnalyzer::isMethodCall(ExprAST& expr) {
    auto* memberAccess = dynamic_cast<MemberAccessExprAST*>(&expr);
    if (!memberAccess) return false;

    Type* objType = memberAccess->object->type;
    if (!objType) return false;

    if (memberAccess->accessKind == MemberAccessKind::Arrow) {
        if (objType->kind == TypeKind::Pointer && objType->base &&
            objType->base->kind == TypeKind::Class) {
            return true;
        }
    } else {
        if (objType->kind == TypeKind::Class) {
            return true;
        }
    }

    return false;
}

// 新增AST节点的visit方法实现
void SemanticAnalyzer::visit(UsingDeclAST& node) {
    // using 声明：将类型别名添加到类型上下文
    typeCtx->addTypedef(node.name, node.aliasedType);
    declare(node.name, node.aliasedType);
}

void SemanticAnalyzer::visit(TypeDeclAST& node) {
    // type 声明：创建新类型（distinct type）
    // 目前简单实现为类型别名
    typeCtx->addTypedef(node.name, node.aliasedType);
    declare(node.name, node.aliasedType);
}

void SemanticAnalyzer::visit(ModuleDeclAST& node) {
    // 模块声明：在语义分析阶段不需要做任何事情
    // 模块系统将在后续版本中实现
}

void SemanticAnalyzer::visit(DeferStmtAST& node) {
    // defer 语句：检查表达式是否有效
    if (node.callExpr) {
        visit(*node.callExpr);
    }
}

// ===== SEM-01/02: definite-assignment analysis =====

void SemanticAnalyzer::initRead(const std::string& name, const ASTNode& node,
                                std::unordered_set<std::string>& state) {
    if (!initLocals || !initLocals->count(name)) return;   // not a local of this function
    if (state.count(name)) return;                         // definitely assigned
    if (initWarned.count(name)) return;                    // warn once per variable
    initWarned.insert(name);
    emitWarning(DiagnosticCode::WarnUninitializedVariable,
                "variable '" + name + "' may be used before it is initialized", node);
}

void SemanticAnalyzer::collectLocalNames(StmtAST* stmt, std::unordered_set<std::string>& out) {
    if (!stmt) return;
    if (auto* block = dynamic_cast<CompoundStmtAST*>(stmt)) {
        for (auto& s : block->stmts) collectLocalNames(s.get(), out);
        return;
    }
    if (auto* ds = dynamic_cast<DeclStmtAST*>(stmt)) {
        if (auto* v = dynamic_cast<VarDeclAST*>(ds->decl.get())) {
            out.insert(v->name);
        } else if (auto* a = dynamic_cast<ArrayDeclAST*>(ds->decl.get())) {
            out.insert(a->name);
        } else if (auto* m = dynamic_cast<MultiVarDeclAST*>(ds->decl.get())) {
            for (auto& d : m->decls) {
                if (auto* v = dynamic_cast<VarDeclAST*>(d.get())) out.insert(v->name);
                else if (auto* a = dynamic_cast<ArrayDeclAST*>(d.get())) out.insert(a->name);
            }
        }
        return;
    }
    if (auto* ifs = dynamic_cast<IfStmtAST*>(stmt)) {
        collectLocalNames(ifs->thenStmt.get(), out);
        collectLocalNames(ifs->elseStmt.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WhileStmtAST*>(stmt)) { collectLocalNames(w->body.get(), out); return; }
    if (auto* d = dynamic_cast<DoWhileStmtAST*>(stmt)) { collectLocalNames(d->body.get(), out); return; }
    if (auto* f = dynamic_cast<ForStmtAST*>(stmt)) {
        collectLocalNames(f->init.get(), out);
        collectLocalNames(f->body.get(), out);
        return;
    }
    if (auto* sw = dynamic_cast<SwitchStmtAST*>(stmt)) {
        for (auto& c : sw->cases) collectLocalNames(c.get(), out);
        return;
    }
}

void SemanticAnalyzer::initWalkExpr(ExprAST* expr, std::unordered_set<std::string>& state) {
    if (!expr) return;

    if (auto* var = dynamic_cast<VariableExprAST*>(expr)) {
        initRead(var->name, *var, state);
        return;
    }
    if (auto* bin = dynamic_cast<BinaryExprAST*>(expr)) {
        initWalkExpr(bin->left.get(), state);
        initWalkExpr(bin->right.get(), state);
        return;
    }
    if (auto* un = dynamic_cast<UnaryExprAST*>(expr)) {
        if (un->op == UnaryOp::AddressOf) {
            // Taking an address does not read the object; the pointee may be
            // written through it, so treat it as initialized from here on.
            if (auto* v = dynamic_cast<VariableExprAST*>(un->operand.get())) {
                state.insert(v->name);
            } else {
                initWalkExpr(un->operand.get(), state);
            }
            return;
        }
        if ((un->op == UnaryOp::PreInc || un->op == UnaryOp::PreDec)) {
            if (auto* v = dynamic_cast<VariableExprAST*>(un->operand.get())) {
                initRead(v->name, *v, state);
                state.insert(v->name);
                return;
            }
        }
        initWalkExpr(un->operand.get(), state);
        return;
    }
    if (auto* asn = dynamic_cast<AssignmentExprAST*>(expr)) {
        initWalkExpr(asn->rhs.get(), state);
        ExprAST* target = asn->lhs.get();
        if (asn->op != AssignOp::Assign && target) {
            // Compound assignment reads the target first.
            if (auto* v = dynamic_cast<VariableExprAST*>(target)) {
                initRead(v->name, *v, state);
            } else {
                initWalkExpr(target, state);
            }
        }
        // Record the write. For member/index targets, mark the base variable.
        ExprAST* base = target;
        while (auto* ma = dynamic_cast<MemberAccessExprAST*>(base)) base = ma->object.get();
        while (auto* aa = dynamic_cast<ArrayAccessExprAST*>(base)) base = aa->array.get();
        if (auto* v = dynamic_cast<VariableExprAST*>(base)) {
            state.insert(v->name);
        } else if (target && asn->op == AssignOp::Assign) {
            initWalkExpr(target, state);
        }
        return;
    }
    if (auto* post = dynamic_cast<PostfixIncDecExprAST*>(expr)) {
        if (auto* v = dynamic_cast<VariableExprAST*>(post->operand.get())) {
            initRead(v->name, *v, state);
            state.insert(v->name);
            return;
        }
        initWalkExpr(post->operand.get(), state);
        return;
    }
    if (auto* tern = dynamic_cast<TernaryExprAST*>(expr)) {
        initWalkExpr(tern->cond.get(), state);
        auto thenState = state;
        auto elseState = state;
        initWalkExpr(tern->then.get(), thenState);
        initWalkExpr(tern->elseExpr.get(), elseState);
        std::unordered_set<std::string> merged;
        for (const auto& n : thenState) {
            if (elseState.count(n)) merged.insert(n);
        }
        state = std::move(merged);
        return;
    }
    if (auto* cast = dynamic_cast<CastExprAST*>(expr)) {
        initWalkExpr(cast->expr.get(), state);
        return;
    }
    if (auto* comma = dynamic_cast<CommaExprAST*>(expr)) {
        initWalkExpr(comma->left.get(), state);
        initWalkExpr(comma->right.get(), state);
        return;
    }
    if (auto* arr = dynamic_cast<ArrayAccessExprAST*>(expr)) {
        initWalkExpr(arr->array.get(), state);
        initWalkExpr(arr->index.get(), state);
        return;
    }
    if (auto* mem = dynamic_cast<MemberAccessExprAST*>(expr)) {
        initWalkExpr(mem->object.get(), state);
        return;
    }
    if (auto* call = dynamic_cast<CallExprAST*>(expr)) {
        if (call->isIndirect) {
            initRead(call->callee, *call, state);  // function-pointer value
        }
        for (auto& a : call->args) initWalkExpr(a.get(), state);
        return;
    }
    if (auto* mc = dynamic_cast<MethodCallExprAST*>(expr)) {
        initWalkExpr(mc->object.get(), state);
        for (auto& a : mc->args) initWalkExpr(a.get(), state);
        return;
    }
    if (auto* il = dynamic_cast<InitializerListExprAST*>(expr)) {
        for (auto& e : il->initializers) initWalkExpr(e.get(), state);
        return;
    }
    // SizeofExprAST is unevaluated; number/string/char literals have no effect.
}

void SemanticAnalyzer::initWalkStmt(StmtAST* stmt, std::unordered_set<std::string>& state) {
    if (!stmt) return;

    if (auto* block = dynamic_cast<CompoundStmtAST*>(stmt)) {
        for (auto& s : block->stmts) initWalkStmt(s.get(), state);
        return;
    }
    if (auto* es = dynamic_cast<ExprStmtAST*>(stmt)) {
        initWalkExpr(es->expr.get(), state);
        return;
    }
    if (auto* ds = dynamic_cast<DeclStmtAST*>(stmt)) {
        auto handle = [&](DeclAST* d) {
            // Only an initializer makes a local definitely assigned; a bare
            // declaration leaves it may-be-uninitialized.
            if (auto* v = dynamic_cast<VarDeclAST*>(d)) {
                if (v->initExpr) {
                    initWalkExpr(v->initExpr.get(), state);
                    state.insert(v->name);
                }
            } else if (auto* a = dynamic_cast<ArrayDeclAST*>(d)) {
                if (a->initExpr) {
                    initWalkExpr(a->initExpr.get(), state);
                    state.insert(a->name);
                }
            }
        };
        if (auto* m = dynamic_cast<MultiVarDeclAST*>(ds->decl.get())) {
            for (auto& d : m->decls) handle(d.get());
        } else {
            handle(ds->decl.get());
        }
        return;
    }
    if (auto* ifs = dynamic_cast<IfStmtAST*>(stmt)) {
        initWalkExpr(ifs->cond.get(), state);
        auto thenState = state;
        auto elseState = state;
        initWalkStmt(ifs->thenStmt.get(), thenState);
        initWalkStmt(ifs->elseStmt.get(), elseState);
        // Join: keep only variables assigned on both paths (a variable first
        // assigned on both branches becomes definitely assigned).
        std::unordered_set<std::string> merged;
        for (const auto& n : thenState) {
            if (elseState.count(n)) merged.insert(n);
        }
        state = std::move(merged);
        return;
    }
    if (auto* w = dynamic_cast<WhileStmtAST*>(stmt)) {
        initWalkExpr(w->cond.get(), state);
        auto bodyState = state;                 // loop may run zero times
        initWalkStmt(w->body.get(), bodyState);
        return;
    }
    if (auto* d = dynamic_cast<DoWhileStmtAST*>(stmt)) {
        auto bodyState = state;                 // body runs at least once
        initWalkStmt(d->body.get(), bodyState);
        initWalkExpr(d->cond.get(), bodyState);
        state = bodyState;
        return;
    }
    if (auto* f = dynamic_cast<ForStmtAST*>(stmt)) {
        if (f->init) initWalkStmt(f->init.get(), state);
        if (f->cond) initWalkExpr(f->cond.get(), state);
        auto loopState = state;
        initWalkStmt(f->body.get(), loopState);
        if (f->inc) initWalkExpr(f->inc.get(), loopState);
        return;                                 // exiting the loop is possible with 0 iterations
    }
    if (auto* sw = dynamic_cast<SwitchStmtAST*>(stmt)) {
        initWalkExpr(sw->cond.get(), state);
        for (auto& c : sw->cases) {
            auto caseState = state;
            initWalkStmt(c.get(), caseState);
        }
        return;                                 // conservative: no case may run
    }
    if (auto* ret = dynamic_cast<ReturnStmtAST*>(stmt)) {
        initWalkExpr(ret->value.get(), state);
        return;
    }
    // Break/continue/goto/null: no effect on initialization state.
}

void SemanticAnalyzer::checkInitialization(FunctionDeclAST& node) {
    if (!node.body) return;

    std::unordered_set<std::string> locals;
    collectLocalNames(node.body.get(), locals);
    for (auto& p : node.params) locals.insert(p->name);
    if (locals.empty()) return;

    initLocals = &locals;
    initWarned.clear();

    std::unordered_set<std::string> state;
    for (auto& p : node.params) state.insert(p->name);   // parameters are initialized

    initWalkStmt(node.body.get(), state);

    initLocals = nullptr;
}

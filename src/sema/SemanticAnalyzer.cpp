#include "sema/SemanticAnalyzer.h"
#include "ast/Expr.h"
#include "ast/Stmt.h"
#include "ast/Decl.h"
#include "ast/Type.h"
#include "ast/Mangle.h"
#include <algorithm>

// Strip any number of typedef/alias layers, returning the underlying type.
static Type* stripTypedef(Type* type) {
    while (type && type->kind == TypeKind::Typedef) {
        type = static_cast<TypedefType*>(type)->aliasedType;
    }
    return type;
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
    errors.emplace_back(Diagnostic::Level::Error, msg, node.sourceFile, node.sourceLine, node.sourceColumn);
}

void SemanticAnalyzer::emitWarning(const std::string& msg, const ASTNode& node) {
    warnings.emplace_back(Diagnostic::Level::Warning, msg, node.sourceFile, node.sourceLine, node.sourceColumn);
}

void SemanticAnalyzer::emitError(DiagnosticCode code, const std::string& msg, const ASTNode& node) {
    errors.emplace_back(Diagnostic::Level::Error, code, msg, node.sourceFile, node.sourceLine, node.sourceColumn);
}

void SemanticAnalyzer::emitWarning(DiagnosticCode code, const std::string& msg, const ASTNode& node) {
    warnings.emplace_back(Diagnostic::Level::Warning, code, msg, node.sourceFile, node.sourceLine, node.sourceColumn);
}

void SemanticAnalyzer::enterScope() {
    currentScope = new Scope(currentScope);
}

void SemanticAnalyzer::exitScope() {
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
    return type->kind == TypeKind::Int || type->kind == TypeKind::Char || type->kind == TypeKind::Enum ||
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
    return type->kind == TypeKind::Float || type->kind == TypeKind::Double ||
           type->kind == TypeKind::Float32 || type->kind == TypeKind::Float64;
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
    if (left->kind == right->kind) return true;
    if (isArithmeticType(left) && isArithmeticType(right)) return true;
    if (left->kind == TypeKind::Pointer && right->kind == TypeKind::Pointer) return true;
    if (left->kind == TypeKind::Pointer && right->kind == TypeKind::Int) return true;
    if (left->kind == TypeKind::Int && right->kind == TypeKind::Pointer) return true;
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
    if (left->kind == right->kind) return left;
    if (left->kind == TypeKind::Double || right->kind == TypeKind::Double) return typeCtx->getDouble();
    if (left->kind == TypeKind::Float || right->kind == TypeKind::Float) return typeCtx->getFloat();
    if (left->kind == TypeKind::Int) return left;
    if (right->kind == TypeKind::Int) return right;
    return left;
}

std::string SemanticAnalyzer::typeToString(Type* type) const {
    if (!type) return "<unknown>";
    switch (type->kind) {
        case TypeKind::Void: return "void";
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Double: return "double";
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
            if (!typesCompatible(left, right)) {
                emitError("comparison of incompatible types: '" + typeToString(left) + "' and '" 
                    + typeToString(right) + "' with '" + binaryOpToString(op) + "'", node);
                return nullptr;
            }
            return typeCtx->getInt();

        case BinaryOp::And:
        case BinaryOp::Or:
            return typeCtx->getInt();

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

    if (lhs->kind == TypeKind::Void || rhs->kind == TypeKind::Void) {
        emitError("cannot assign to or from 'void' type", node);
        return nullptr;
    }

    if (isArithmeticType(lhs) && isArithmeticType(rhs)) return lhs;
    if (lhs->kind == rhs->kind) return lhs;
    if (isPointerOrArray(lhs) && isPointerOrArray(rhs)) return lhs;
    if (isPointerOrArray(lhs) && isIntegerType(rhs)) return lhs;

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

Type* SemanticAnalyzer::getExprType(ExprAST& expr) {
    visit(expr);
    return expr.type;
}

std::optional<SemanticAnalyzer::ConstValue> SemanticAnalyzer::evaluateConstexpr(ExprAST* expr) {
    if (!expr) return std::nullopt;

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
    node.type = typeCtx->getInt();
    node.isLValue = false;
}

void SemanticAnalyzer::visit(FloatExprAST& node) {
    node.type = typeCtx->getFloat();
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
    Type* leftType = getExprType(*node.left);
    Type* rightType = getExprType(*node.right);

    // Check for operator overloading on struct/union types
    if (isStructOrUnionType(leftType) || isStructOrUnionType(rightType)) {
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
            node.type = typeCtx->getInt();
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
            node.type = typeCtx->getInt();
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
    // Resolve a namespace-qualified or namespace-local callee to its mangled key.
    node.callee = resolveNamespaceName(node.callee);

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

void SemanticAnalyzer::visit(AssignmentExprAST& node) {
    Type* lhsType = getExprType(*node.lhs);
    if (lhsType && lhsType->isConst) {
        emitError("cannot assign to const variable", node);
        return;
    }
    Type* rhsType = getExprType(*node.rhs);
    node.type = checkAssignmentTypes(lhsType, rhsType, node);
    node.isLValue = true;
}

void SemanticAnalyzer::visit(TernaryExprAST& node) {
    Type* condType = getExprType(*node.cond);
    Type* thenType = getExprType(*node.then);
    Type* elseType = getExprType(*node.elseExpr);

    if (condType && !isScalarType(condType)) {
        emitError("ternary condition must be scalar type, but got '" + typeToString(condType) + "'", node);
    }

    node.type = getCommonType(thenType, elseType);
    node.isLValue = false;
}

void SemanticAnalyzer::visit(CastExprAST& node) {
    Type* exprType = getExprType(*node.expr);
    if (exprType && node.castType && !typesCompatible(exprType, node.castType)) {
        emitWarning(DiagnosticCode::SemIncompatibleCast,
                    "incompatible cast from '" + typeToString(exprType) +
                        "' to '" + typeToString(node.castType) + "'", node);
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
    } else {
        emitError("subscripted value is neither array nor pointer, but '" + typeToString(arrayType) + "'", node);
        node.type = nullptr;
    }
    node.isLValue = true;
}

void SemanticAnalyzer::visit(MemberAccessExprAST& node) {
    Type* objType = getExprType(*node.object);

    if (!objType) {
        node.type = nullptr;
        node.isLValue = false;
        return;
    }

    Type* memberBaseType = nullptr;
    if (node.accessKind == MemberAccessKind::Arrow) {
        if (objType->kind != TypeKind::Pointer) {
            emitError("member access with '->' requires pointer to struct/class, but got '" + typeToString(objType) + "'", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        if (objType->base->kind != TypeKind::Struct && objType->base->kind != TypeKind::Class &&
            objType->base->kind != TypeKind::Union) {
            emitError("member access with '->' requires pointer to struct/class/union, but '" + typeToString(objType) + "' points to '" + typeToString(objType->base) + "'", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        memberBaseType = objType->base;
    } else {
        if (objType->kind != TypeKind::Struct && objType->kind != TypeKind::Class &&
            objType->kind != TypeKind::Union) {
            emitError("member access with '.' requires struct/class/union type, but got '" + typeToString(objType) + "'", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        memberBaseType = objType;
    }

    if (memberBaseType->kind == TypeKind::Struct) {
        auto* structType = static_cast<StructType*>(memberBaseType);
        for (auto& field : structType->fields) {
            if (field.first == node.memberName) {
                node.type = field.second;
                node.isLValue = true;
                return;
            }
        }
        emitError("no member named '" + node.memberName + "' in struct '" + structType->name + "'", node);
    } else if (memberBaseType->kind == TypeKind::Class) {
        auto* classType = static_cast<ClassType*>(memberBaseType);
        const std::string className = classType->name;
        // Search this class and its base classes for the field.
        while (classType) {
            for (auto& field : classType->fields) {
                if (field.first == node.memberName) {
                    node.type = field.second;
                    node.isLValue = true;
                    return;
                }
            }
            Type* baseType = classType->base;
            if (!baseType || baseType->kind != TypeKind::Class) break;
            classType = static_cast<ClassType*>(baseType);
        }
        emitError("no member named '" + node.memberName + "' in class '" + className + "'", node);
    } else if (memberBaseType->kind == TypeKind::Union) {
        auto* unionType = static_cast<UnionType*>(memberBaseType);
        for (auto& member : unionType->members) {
            if (member.first == node.memberName) {
                node.type = member.second;
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

    Symbol* method = resolveMethod(classType, node.methodName, argTypes);
    if (!method || method->type->kind != TypeKind::Function) {
        emitError("no matching method '" + node.methodName + "' in class '" + classType->name + "'", node);
        node.type = nullptr;
        node.isLValue = false;
        return;
    }

    auto* funcType = static_cast<FunctionType*>(method->type);
    node.type = funcType->returnType;
    node.isLValue = false;
    delete method;
}

void SemanticAnalyzer::visit(SizeofExprAST& node) {
    // Resolve the type of a `sizeof(expr)` operand; the expression itself is
    // not evaluated, but its type is needed by codegen.
    if (!node.sizeofType && node.expr) {
        node.sizeofType = getExprType(*node.expr);
        if (!node.sizeofType) {
            emitError("invalid operand to sizeof", node);
        }
    }
    node.type = typeCtx->getInt();
    node.isLValue = false;
}

void SemanticAnalyzer::visit(InitializerListExprAST& node) {
    if (!node.initializers.empty()) {
        node.type = getExprType(*node.initializers[0]);
    } else {
        node.type = typeCtx->getInt();
    }
    node.isLValue = false;
}

void SemanticAnalyzer::visit(CompoundStmtAST& node) {
    enterScope();
    for (auto& stmt : node.stmts) {
        if (stmt) {
            visit(*stmt);
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
}

void SemanticAnalyzer::visit(ArrayDeclAST& node) {
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
            Type* initType = getExprType(*node.initExpr);
            if (initType && !typesCompatible(node.elementType, initType)) {
                emitError("type mismatch in initialization of array '" + node.name + "': expected '" 
                    + typeToString(node.elementType) + "', got '" + typeToString(initType) + "'", node);
            }
        }
    }
    if (!declare(node.name, arrayType)) {
        emitError("redeclaration of array '" + node.name + "' in the same scope", node);
    }
}

void SemanticAnalyzer::visit(StructDeclAST& node) {
    bool isClass = !node.methods.empty() || !node.baseClass.empty();

    if (isClass) {
        auto* classType = typeCtx->getOrCreateClass(node.name);

        // Add fields if not already added
        for (auto& field : node.fields) {
            if (!classType->getFieldType(field.first)) {
                classType->addField(field.first, field.second);
            }
        }

        if (!node.baseClass.empty()) {
            auto* baseType = typeCtx->getClass(node.baseClass);
            if (!baseType) {
                emitError("base class '" + node.baseClass + "' of class '" + node.name + "' not found", node);
            } else if (hasCircularInheritance(node.name, node.baseClass)) {
                emitError("circular inheritance detected involving class '" + node.name + "'", node);
            } else {
                classType->baseClass = node.baseClass;
                classType->base = baseType;
                for (auto& method : baseType->methods) {
                    // Only add if not already present
                    if (!classType->getMethod(method.first)) {
                        classType->addMethod(method.first, method.second);
                    }
                }
            }
        }

        for (auto& method : node.methods) {
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
        }

        typeCtx->addClass(node.name, classType);

        for (auto& method : node.methods) {
            auto* thisType = new Type(TypeKind::Pointer, classType);
            auto thisParam = std::make_unique<ParamDeclAST>("this", thisType);
            method->params.insert(method->params.begin(), std::move(thisParam));
            visit(*method);
        }
    } else {
        auto* structType = new StructType(node.name);
        for (auto& field : node.fields) {
            structType->addField(field.first, field.second);
        }
        typeCtx->addStruct(node.name, structType);
    }
}

void SemanticAnalyzer::visit(UnionDeclAST& node) {
    auto* unionType = new UnionType(node.name);
    for (auto& member : node.members) {
        unionType->addMember(member.first, member.second);
    }
    typeCtx->addUnion(node.name, unionType);
}

void SemanticAnalyzer::visit(EnumDeclAST& node) {
    auto* enumType = new EnumType(node.name);
    for (auto& val : node.values) {
        enumType->addValue(val.first, val.second);
        // Enumerators live in the enclosing (namespace) scope, so `A::Red`
        // resolves to the same key as `Red` used inside namespace A.
        enumConstants[scopedName(val.first)] = {enumType, val.second};
    }
    typeCtx->addEnum(node.name, enumType);
}

void SemanticAnalyzer::visit(TypedefDeclAST& node) {
    typeCtx->addTypedef(node.name, node.aliasedType);
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

    // Validate constexpr function constraints
    if (node.isConstexpr) {
        // Return type must be arithmetic (literal type)
        if (node.returnType->kind != TypeKind::Int &&
            node.returnType->kind != TypeKind::Float &&
            node.returnType->kind != TypeKind::Double &&
            node.returnType->kind != TypeKind::Char) {
            emitError("constexpr function '" + node.name + "' must have literal return type", node);
        }

        // All parameters must be arithmetic types
        for (auto& param : node.params) {
            if (param->type->kind != TypeKind::Int &&
                param->type->kind != TypeKind::Float &&
                param->type->kind != TypeKind::Double &&
                param->type->kind != TypeKind::Char) {
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
    } else if (auto* multi = dynamic_cast<MultiVarDeclAST*>(&decl)) {
        for (auto& d : multi->decls) {
            if (auto* v = dynamic_cast<VarDeclAST*>(d.get())) visit(*v);
            else if (auto* a = dynamic_cast<ArrayDeclAST*>(d.get())) visit(*a);
        }
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

bool SemanticAnalyzer::hasCircularInheritance(const std::string& className, const std::string& baseClass) const {
    std::vector<std::string> chain;
    chain.push_back(className);

    std::string current = baseClass;
    while (!current.empty()) {
        if (std::find(chain.begin(), chain.end(), current) != chain.end()) {
            return true;
        }
        chain.push_back(current);

        auto* type = typeCtx->getClass(current);
        if (!type) break;
        current = type->baseClass;
    }

    return false;
}

Symbol* SemanticAnalyzer::resolveMethod(ClassType* classType, const std::string& methodName, const std::vector<Type*>& argTypes) {
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
                    return sym;
                }
            }
        }
    }

    if (!classType->baseClass.empty()) {
        auto* baseType = typeCtx->getClass(classType->baseClass);
        if (baseType) {
            Symbol* result = resolveMethod(baseType, methodName, argTypes);
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

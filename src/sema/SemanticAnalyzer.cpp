#include "sema/SemanticAnalyzer.h"
#include "ast/Expr.h"
#include "ast/Stmt.h"
#include "ast/Decl.h"
#include "ast/Type.h"
#include "ast/Mangle.h"

SemanticAnalyzer::SemanticAnalyzer()
    : globalScope(std::make_unique<Scope>(nullptr)),
      currentScope(globalScope.get()),
      currentFunction(nullptr),
      typeCtx(&TypeContext::instance()) {}

const std::vector<Diagnostic>& SemanticAnalyzer::getErrors() const {
    return errors;
}

void SemanticAnalyzer::emitError(const std::string& msg, const ASTNode& node) {
    errors.emplace_back(Diagnostic::Level::Error, msg, node.sourceFile, node.sourceLine, node.sourceColumn);
}

void SemanticAnalyzer::emitWarning(const std::string& msg, const ASTNode& node) {
    errors.emplace_back(Diagnostic::Level::Warning, msg, node.sourceFile, node.sourceLine, node.sourceColumn);
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
    Symbol* sym = new Symbol(name, type);
    if (!currentScope->declare(name, sym)) {
        delete sym;
        return false;
    }
    return true;
}

Symbol* SemanticAnalyzer::lookup(const std::string& name) {
    return currentScope->lookup(name);
}

bool SemanticAnalyzer::isIntegerType(Type* type) const {
    if (!type) return false;
    return type->kind == TypeKind::Int || type->kind == TypeKind::Char || type->kind == TypeKind::Enum;
}

bool SemanticAnalyzer::isFloatType(Type* type) const {
    if (!type) return false;
    return type->kind == TypeKind::Float || type->kind == TypeKind::Double;
}

bool SemanticAnalyzer::isArithmeticType(Type* type) const {
    return isIntegerType(type) || isFloatType(type);
}

bool SemanticAnalyzer::isPointerOrArray(Type* type) const {
    if (!type) return false;
    return type->kind == TypeKind::Pointer || type->kind == TypeKind::Array;
}

bool SemanticAnalyzer::typesCompatible(Type* left, Type* right) const {
    if (!left || !right) return false;
    if (left->kind == right->kind) return true;
    if (isArithmeticType(left) && isArithmeticType(right)) return true;
    if (left->kind == TypeKind::Pointer && right->kind == TypeKind::Pointer) return true;
    if (left->kind == TypeKind::Pointer && right->kind == TypeKind::Int) return true;
    if (left->kind == TypeKind::Int && right->kind == TypeKind::Pointer) return true;
    return false;
}

Type* SemanticAnalyzer::getCommonType(Type* left, Type* right) const {
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

    emitError("incompatible types in assignment: cannot assign '" + typeToString(rhs) + "' to '" + typeToString(lhs) + "'", node);
    return nullptr;
}

Type* SemanticAnalyzer::checkFunctionCall(const std::string& name, const std::vector<std::unique_ptr<ExprAST>>& args, ExprAST& node) {
    OverloadSet* overloadSet = currentScope->lookupOverload(name);
    if (!overloadSet || overloadSet->empty()) {
        emitError("use of undeclared function '" + name + "'", node);
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
        // Check if no candidates match at all
        bool anyMatch = false;
        for (auto* sym : overloadSet->getCandidates()) {
            if (sym->type->kind == TypeKind::Function) {
                auto* funcType = static_cast<FunctionType*>(sym->type);
                if (funcType->paramTypes.size() == argTypes.size()) {
                    anyMatch = true;
                    break;
                }
            }
        }
        if (!anyMatch) {
            emitError("no matching function for call to '" + name + "'", node);
        } else {
            emitError("ambiguous call to overloaded function '" + name + "'", node);
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
                default: return std::nullopt;
            }
            return cv;
        }
        return std::nullopt;
    }

    if (auto* var = dynamic_cast<VariableExprAST*>(expr)) {
        auto it = constexprValues.find(var->name);
        if (it != constexprValues.end()) {
            return it->second;
        }
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
    Symbol* sym = lookup(node.name);
    if (!sym) {
        emitError("use of undeclared identifier '" + node.name + "'", node);
        node.type = nullptr;
    } else {
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
    node.isLValue = false;
}

void SemanticAnalyzer::visit(CallExprAST& node) {
    node.type = checkFunctionCall(node.callee, node.args, node);
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

    if (condType && !isArithmeticType(condType)) {
        emitError("ternary condition must be arithmetic type, but got '" + typeToString(condType) + "'", node);
    }

    node.type = getCommonType(thenType, elseType);
    node.isLValue = false;
}

void SemanticAnalyzer::visit(CastExprAST& node) {
    Type* exprType = getExprType(*node.expr);
    if (exprType && node.castType && !typesCompatible(exprType, node.castType)) {
        emitWarning("incompatible cast from '" + typeToString(exprType) + "' to '" + typeToString(node.castType) + "'", node);
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
        if (objType->base->kind != TypeKind::Struct && objType->base->kind != TypeKind::Class) {
            emitError("member access with '->' requires pointer to struct/class, but '" + typeToString(objType) + "' points to '" + typeToString(objType->base) + "'", node);
            node.type = nullptr;
            node.isLValue = false;
            return;
        }
        memberBaseType = objType->base;
    } else {
        if (objType->kind != TypeKind::Struct && objType->kind != TypeKind::Class) {
            emitError("member access with '.' requires struct/class type, but got '" + typeToString(objType) + "'", node);
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
        for (auto& field : classType->fields) {
            if (field.first == node.memberName) {
                node.type = field.second;
                node.isLValue = true;
                return;
            }
        }
        emitError("no member named '" + node.memberName + "' in class '" + classType->name + "'", node);
    }
    node.type = nullptr;
    node.isLValue = false;
}

void SemanticAnalyzer::visit(SizeofExprAST& node) {
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
    if (condType && !isArithmeticType(condType)) {
        emitError("if condition must be arithmetic type, but got '" + typeToString(condType) + "'", node);
    }
    if (node.thenStmt) visit(*node.thenStmt);
    if (node.elseStmt) visit(*node.elseStmt);
}

void SemanticAnalyzer::visit(WhileStmtAST& node) {
    Type* condType = getExprType(*node.cond);
    if (condType && !isArithmeticType(condType)) {
        emitError("while condition must be arithmetic type, but got '" + typeToString(condType) + "'", node);
    }
    if (node.body) visit(*node.body);
}

void SemanticAnalyzer::visit(DoWhileStmtAST& node) {
    Type* condType = getExprType(*node.cond);
    if (condType && !isArithmeticType(condType)) {
        emitError("do-while condition must be arithmetic type, but got '" + typeToString(condType) + "'", node);
    }
    if (node.body) visit(*node.body);
}

void SemanticAnalyzer::visit(ForStmtAST& node) {
    enterScope();
    if (node.init) visit(*node.init);
    if (node.cond) {
        Type* condType = getExprType(*node.cond);
        if (condType && !isArithmeticType(condType)) {
            emitError("for condition must be arithmetic type, but got '" + typeToString(condType) + "'", node);
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
    for (auto& c : node.cases) {
        if (c) visit(*c);
    }
}

void SemanticAnalyzer::visit(BreakStmtAST& node) {}

void SemanticAnalyzer::visit(ContinueStmtAST& node) {}

void SemanticAnalyzer::visit(GotoStmtAST& node) {}

void SemanticAnalyzer::visit(LabelStmtAST& node) {
    if (node.stmt) visit(*node.stmt);
}

void SemanticAnalyzer::visit(NullStmtAST& node) {}

void SemanticAnalyzer::visit(VarDeclAST& node) {
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
        Type* initType = getExprType(*node.initExpr);
        if (initType && !typesCompatible(node.type, initType)) {
            emitError("type mismatch in initialization of '" + node.name + "': expected '" 
                + typeToString(node.type) + "', got '" + typeToString(initType) + "'", node);
        }
    }
    if (!declare(node.name, node.type)) {
        emitError("redeclaration of variable '" + node.name + "' in the same scope", node);
    }
}

void SemanticAnalyzer::visit(ArrayDeclAST& node) {
    Type* arrayType = new ArrayType(node.elementType, node.size);
    if (node.initExpr) {
        Type* initType = getExprType(*node.initExpr);
        if (initType && !typesCompatible(node.elementType, initType)) {
            emitError("type mismatch in initialization of array '" + node.name + "': expected '" 
                + typeToString(node.elementType) + "', got '" + typeToString(initType) + "'", node);
        }
    }
    if (!declare(node.name, arrayType)) {
        emitError("redeclaration of array '" + node.name + "' in the same scope", node);
    }
}

void SemanticAnalyzer::visit(StructDeclAST& node) {
    auto* structType = new StructType(node.name);
    for (auto& field : node.fields) {
        structType->addField(field.first, field.second);
    }
    typeCtx->addStruct(node.name, structType);
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
        }
    }
}

void SemanticAnalyzer::visit(FunctionDeclAST& node) {
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

    std::vector<Type*> paramTypes;
    for (auto& param : node.params) {
        paramTypes.push_back(param->type);
    }
    auto* funcType = new FunctionType(node.returnType, std::move(paramTypes));
    if (!declare(node.name, funcType)) {
        emitError("redeclaration of function '" + node.name + "' in the same scope", node);
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
    }

    exitScope();
    currentFunction = prevFunc;
}

void SemanticAnalyzer::visit(TranslationUnitAST& node) {
    for (auto& decl : node.declarations) {
        if (decl) {
            if (auto* funcDecl = dynamic_cast<FunctionDeclAST*>(decl.get())) {
                visit(*funcDecl);
            } else if (auto* varDecl = dynamic_cast<VarDeclAST*>(decl.get())) {
                visit(*varDecl);
            } else if (auto* arrDecl = dynamic_cast<ArrayDeclAST*>(decl.get())) {
                visit(*arrDecl);
            } else if (auto* structDecl = dynamic_cast<StructDeclAST*>(decl.get())) {
                visit(*structDecl);
            } else if (auto* unionDecl = dynamic_cast<UnionDeclAST*>(decl.get())) {
                visit(*unionDecl);
            } else if (auto* enumDecl = dynamic_cast<EnumDeclAST*>(decl.get())) {
                visit(*enumDecl);
            } else if (auto* typedefDecl = dynamic_cast<TypedefDeclAST*>(decl.get())) {
                visit(*typedefDecl);
            } else if (auto* fwdDecl = dynamic_cast<ForwardDeclAST*>(decl.get())) {
                visit(*fwdDecl);
            }
        }
    }
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
    if (auto* s = dynamic_cast<GotoStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<LabelStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<NullStmtAST*>(&stmt)) { visit(*s); return; }
    if (auto* s = dynamic_cast<DeclStmtAST*>(&stmt)) { visit(*s); return; }
}

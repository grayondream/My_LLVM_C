#include "sema/CompileTimeEvaluator.h"

#include "ast/Expr.h"
#include "ast/Decl.h"
#include "sema/SemanticAnalyzer.h"

using ConstValue = CompileTimeEvaluator::ConstValue;

CompileTimeEvaluator::CompileTimeEvaluator(SemanticAnalyzer& sema)
    : m_sema(sema) {}

std::optional<ConstValue> CompileTimeEvaluator::eval(ExprAST* expr, ASTNode& at) {
    if (!expr) {
        return std::nullopt;
    }
    if (m_depth >= kMaxDepth) {
        if (!m_depthReported) {
            m_sema.emitError("compile_time evaluation depth limit exceeded (64)", at);
            m_depthReported = true;
        }
        return std::nullopt;
    }

    struct DepthGuard {
        CompileTimeEvaluator& ev;
        ~DepthGuard() {
            if (--ev.m_depth == 0) {
                ev.m_depthReported = false;
            }
        }
    } guard{*this};
    ++m_depth;

    if (auto* num = dynamic_cast<NumberExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::INT;
        cv.intVal = num->value;
        return cv;
    }

    if (auto* flt = dynamic_cast<FloatExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::DOUBLE;
        cv.doubleVal = flt->value;
        return cv;
    }

    if (auto* chr = dynamic_cast<CharExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::CHAR;
        cv.charVal = chr->value;
        return cv;
    }

    if (auto* str = dynamic_cast<StringExprAST*>(expr)) {
        ConstValue cv;
        cv.type = ConstValue::STR;
        cv.strVal = str->value;
        return cv;
    }

    if (auto* unary = dynamic_cast<UnaryExprAST*>(expr)) {
        auto operand = eval(unary->operand.get(), at);
        if (!operand) {
            return std::nullopt;
        }
        if (operand->type == ConstValue::INT) {
            ConstValue cv;
            cv.type = ConstValue::INT;
            switch (unary->op) {
                case UnaryOp::Plus:   cv.intVal = operand->intVal; break;
                case UnaryOp::Minus:  cv.intVal = -operand->intVal; break;
                case UnaryOp::Not:    cv.intVal = !operand->intVal; break;
                case UnaryOp::BitNot: cv.intVal = ~operand->intVal; break;
                default: return std::nullopt;
            }
            return cv;
        }
        if (operand->type == ConstValue::DOUBLE) {
            ConstValue cv;
            cv.type = ConstValue::DOUBLE;
            switch (unary->op) {
                case UnaryOp::Plus:  cv.doubleVal = operand->doubleVal; break;
                case UnaryOp::Minus: cv.doubleVal = -operand->doubleVal; break;
                default: return std::nullopt;
            }
            return cv;
        }
        return std::nullopt;
    }

    if (auto* bin = dynamic_cast<BinaryExprAST*>(expr)) {
        auto left = eval(bin->left.get(), at);
        if (!left) {
            return std::nullopt;
        }
        auto right = eval(bin->right.get(), at);
        if (!right) {
            return std::nullopt;
        }
        return evalBinary(*left, *right, static_cast<int>(bin->op));
    }

    if (auto* ternary = dynamic_cast<TernaryExprAST*>(expr)) {
        auto cond = eval(ternary->cond.get(), at);
        if (!cond || cond->type != ConstValue::INT) {
            return std::nullopt;
        }
        return cond->intVal != 0 ? eval(ternary->then.get(), at)
                                 : eval(ternary->elseExpr.get(), at);
    }

    // constexpr 变量（DEC-05：共享 constexprValues）；constexpr 函数调用
    // 委托既有解释内核（evalConstexprCall/evalConstexprStmt，含 ConstEnv）。
    if (auto* var = dynamic_cast<VariableExprAST*>(expr)) {
        const auto& values = m_sema.getConstexprValues();
        auto it = values.find(var->name);
        if (it != values.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    if (dynamic_cast<CallExprAST*>(expr)) {
        return m_sema.evaluateConstexpr(expr);
    }

    return std::nullopt; // 普通非常量节点：静默失败（spec §2 钉死）
}

std::optional<ConstValue>
CompileTimeEvaluator::evalBinary(ConstValue& left, ConstValue& right, int op) {
    const BinaryOp binOp = static_cast<BinaryOp>(op);

    if (left.type == ConstValue::STR && right.type == ConstValue::STR) {
        ConstValue cv;
        switch (binOp) {
            case BinaryOp::Add:
                cv.type = ConstValue::STR;
                cv.strVal = left.strVal + right.strVal;
                return cv;
            case BinaryOp::Eq:
                cv.type = ConstValue::INT;
                cv.intVal = left.strVal == right.strVal;
                return cv;
            case BinaryOp::NotEq:
                cv.type = ConstValue::INT;
                cv.intVal = left.strVal != right.strVal;
                return cv;
            default:
                return std::nullopt;
        }
    }

    if (left.type == ConstValue::INT && right.type == ConstValue::INT) {
        ConstValue cv;
        cv.type = ConstValue::INT;
        switch (binOp) {
            case BinaryOp::Add: cv.intVal = left.intVal + right.intVal; break;
            case BinaryOp::Sub: cv.intVal = left.intVal - right.intVal; break;
            case BinaryOp::Mul: cv.intVal = left.intVal * right.intVal; break;
            case BinaryOp::Div:
                if (right.intVal == 0) return std::nullopt;
                cv.intVal = left.intVal / right.intVal;
                break;
            case BinaryOp::Mod:
                if (right.intVal == 0) return std::nullopt;
                cv.intVal = left.intVal % right.intVal;
                break;
            case BinaryOp::Eq:     cv.intVal = left.intVal == right.intVal; break;
            case BinaryOp::NotEq:  cv.intVal = left.intVal != right.intVal; break;
            case BinaryOp::Lt:     cv.intVal = left.intVal < right.intVal; break;
            case BinaryOp::Gt:     cv.intVal = left.intVal > right.intVal; break;
            case BinaryOp::Le:     cv.intVal = left.intVal <= right.intVal; break;
            case BinaryOp::Ge:     cv.intVal = left.intVal >= right.intVal; break;
            case BinaryOp::And:    cv.intVal = left.intVal && right.intVal; break;
            case BinaryOp::Or:     cv.intVal = left.intVal || right.intVal; break;
            case BinaryOp::BitAnd: cv.intVal = left.intVal & right.intVal; break;
            case BinaryOp::BitOr:  cv.intVal = left.intVal | right.intVal; break;
            case BinaryOp::BitXor: cv.intVal = left.intVal ^ right.intVal; break;
            case BinaryOp::LShift: cv.intVal = left.intVal << right.intVal; break;
            case BinaryOp::RShift: cv.intVal = left.intVal >> right.intVal; break;
            default: return std::nullopt;
        }
        return cv;
    }

    if (left.type == ConstValue::DOUBLE && right.type == ConstValue::DOUBLE) {
        ConstValue cv;
        cv.type = ConstValue::DOUBLE;
        switch (binOp) {
            case BinaryOp::Add: cv.doubleVal = left.doubleVal + right.doubleVal; break;
            case BinaryOp::Sub: cv.doubleVal = left.doubleVal - right.doubleVal; break;
            case BinaryOp::Mul: cv.doubleVal = left.doubleVal * right.doubleVal; break;
            case BinaryOp::Div:
                if (right.doubleVal == 0.0) return std::nullopt;
                cv.doubleVal = left.doubleVal / right.doubleVal;
                break;
            case BinaryOp::Eq:    cv.intVal = left.doubleVal == right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::NotEq: cv.intVal = left.doubleVal != right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::Lt:    cv.intVal = left.doubleVal < right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::Gt:    cv.intVal = left.doubleVal > right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::Le:    cv.intVal = left.doubleVal <= right.doubleVal; cv.type = ConstValue::INT; break;
            case BinaryOp::Ge:    cv.intVal = left.doubleVal >= right.doubleVal; cv.type = ConstValue::INT; break;
            default: return std::nullopt;
        }
        return cv;
    }

    return std::nullopt;
}

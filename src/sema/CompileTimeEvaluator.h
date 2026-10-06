#pragma once

// P1-04 / CT-06 / CT-14 / DEC-05: 编译期求值器。
// 与 constexpr 机制并存且共享内核（DEC-05 裁决）：constexpr 函数调用委托
// SemanticAnalyzer::evaluateConstexpr（其函数解释种子已存在）；compile_time
// 成员（target/build/size_of 族）由本类求值。ConstValue 权威定义在此，
// SemanticAnalyzer 以 using 别名引用，既有引用点不改动。

#include <optional>
#include <string>

class SemanticAnalyzer;
class ExprAST;
class ASTNode;

class CompileTimeEvaluator {
public:
    struct ConstValue {
        enum Type { INT, DOUBLE, CHAR, STR } type;
        // LEX-15: integer constants keep the full 64-bit literal range.
        long long intVal = 0;
        double doubleVal = 0;
        char charVal = 0;
        std::string strVal; // STR 时有效（union 不能放 std::string）
    };

    explicit CompileTimeEvaluator(SemanticAnalyzer& sema);

    // 求值表达式。不可常量折叠 → nullopt 且【不】发诊断（由调用方按场景发
    // 钉死文案）；compile_time 特有错误（未知成员/未知类型/深度超限）→
    // 诊断 + nullopt。`at` 供诊断定位。
    std::optional<ConstValue> eval(ExprAST* expr, ASTNode& at);

private:
    // 递归深度上限（防编译期爆栈）；超限诊断一次（钉死文案，spec §3）。
    static constexpr int kMaxDepth = 64;

    std::optional<ConstValue> evalBinary(ConstValue& left, ConstValue& right, int op);

    SemanticAnalyzer& m_sema;
    int m_depth = 0;
    bool m_depthReported = false;
};

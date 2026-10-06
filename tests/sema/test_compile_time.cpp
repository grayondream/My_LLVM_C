// P1-04 / CT-06: 编译期求值器 CompileTimeEvaluator——字面量/算术/逻辑/
// 字符串比较与拼接、constexpr 变量与函数委托、深度上限、静默失败语义。

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/CompileTimeEvaluator.h"
#include "ast/Decl.h"
#include "ast/Expr.h"
#include "ast/Stmt.h"

#include <spdlog/spdlog.h>

using ConstValue = CompileTimeEvaluator::ConstValue;

class CompileTimeEval : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }

    // 解析 `decls + int32 f() { return expr; }`，跑 sema 后提取 return 表达式。
    // analyze 后的错误数记录在 baseErrors，供「eval 静默」断言比较。
    // 致命断言失败时返回（调用方用 ASSERT_NO_FATAL_FAILURE 包裹）。
    struct Extracted {
        ExprAST* expr = nullptr;
        size_t baseErrors = 0;
        std::unique_ptr<TranslationUnitAST> tu;
        std::unique_ptr<SemanticAnalyzer> analyzer;
    };

    void extract(Extracted& ex, const std::string& decls, const std::string& exprSrc) {
        Lexer lexer("ct_eval_test.c", decls + " int32 f() { return " + exprSrc + "; }");
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        ex.tu = parser.parse();
        ex.analyzer = std::make_unique<SemanticAnalyzer>();
        ex.analyzer->analyze(*ex.tu);
        ex.baseErrors = ex.analyzer->getErrors().size();
        ASSERT_FALSE(ex.tu->declarations.empty());
        FunctionDeclAST* fn = nullptr;
        for (auto it = ex.tu->declarations.rbegin(); it != ex.tu->declarations.rend(); ++it) {
            fn = dynamic_cast<FunctionDeclAST*>(it->get());
            if (fn) break;
        }
        ASSERT_NE(fn, nullptr);
        ASSERT_FALSE(fn->body->stmts.empty());
        auto* ret = dynamic_cast<ReturnStmtAST*>(fn->body->stmts[0].get());
        ASSERT_NE(ret, nullptr);
        ASSERT_NE(ret->value, nullptr);
        ex.expr = ret->value.get();
    }
};

TEST_F(CompileTimeEval, EvalArithmetic) {
    Extracted ex;
    ASSERT_NO_FATAL_FAILURE(extract(ex, "", "2 + 3 * 4"));
    CompileTimeEvaluator ev(*ex.analyzer);
    auto v = ev.eval(ex.expr, *ex.expr);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->type, ConstValue::INT);
    EXPECT_EQ(v->intVal, 14);
}

TEST_F(CompileTimeEval, EvalStringCompareConcat) {
    Extracted ex;
    ASSERT_NO_FATAL_FAILURE(extract(ex, "", "\"a\" + \"b\" == \"ab\""));
    CompileTimeEvaluator ev(*ex.analyzer);
    auto v = ev.eval(ex.expr, *ex.expr);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->type, ConstValue::INT);
    EXPECT_EQ(v->intVal, 1);
}

TEST_F(CompileTimeEval, EvalConstexprVar) {
    Extracted ex;
    ASSERT_NO_FATAL_FAILURE(extract(ex, "constexpr int32 k = 5;", "k * 2"));
    CompileTimeEvaluator ev(*ex.analyzer);
    auto v = ev.eval(ex.expr, *ex.expr);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->type, ConstValue::INT);
    EXPECT_EQ(v->intVal, 10);
}

TEST_F(CompileTimeEval, EvalDepthLimit) {
    // 100 层嵌套括号加法：((((1+1)+1)+1)...)
    std::string expr;
    for (int i = 0; i < 100; ++i) expr += "(";
    expr += "1";
    for (int i = 0; i < 100; ++i) expr += "+1)";
    Extracted ex;
    ASSERT_NO_FATAL_FAILURE(extract(ex, "", expr));
    CompileTimeEvaluator ev(*ex.analyzer);
    auto v = ev.eval(ex.expr, *ex.expr);
    EXPECT_FALSE(v.has_value());
    ASSERT_GT(ex.analyzer->getErrors().size(), ex.baseErrors);
    bool found = false;
    for (const auto& d : ex.analyzer->getErrors()) {
        if (d.message.find("depth limit exceeded") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(CompileTimeEval, EvalNonConstantQuiet) {
    Extracted ex;
    ASSERT_NO_FATAL_FAILURE(extract(ex, "", "x + 1")); // x 未声明：sema 已有诊断，eval 不得新增
    CompileTimeEvaluator ev(*ex.analyzer);
    auto v = ev.eval(ex.expr, *ex.expr);
    EXPECT_FALSE(v.has_value());
    EXPECT_EQ(ex.analyzer->getErrors().size(), ex.baseErrors);
}

// ---- P1-04 / CT-04/05: sema 钩子与 build 配置注入 ----

static bool analyzeOk(const std::string& source) {
    Lexer lexer("ct_sema_test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast) return false;
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    if (!parser.getErrors().empty()) return false; // parse 诊断不得被吞
    return analyzer.getErrors().empty();
}

TEST_F(CompileTimeEval, CompileTimeInsideNamespace) {
    // Review Focus 2: namespace 内的 compile_time 根链不被 namespacePrefix 劫持。
    EXPECT_TRUE(analyzeOk(R"(
constexpr int32 ok = compile_time.build.debug;
namespace N { int32 f() { return ok; } }
)"));
}

TEST_F(CompileTimeEval, StrEscapeDiagnosed) {
    // Review Focus 5: STR 值赋给运行时变量 → 类型错误诊断，不崩溃。
    Lexer lexer("ct_str_escape.c", "int32 main() { int32 x = compile_time.target.os; return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_FALSE(analyzer.getErrors().empty());
}

TEST_F(CompileTimeEval, UnknownMemberDiagnosed) {
    Lexer lexer("ct_unknown_member.c", "int32 main() { int32 x = compile_time.nope; return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_FALSE(analyzer.getErrors().empty());
    bool found = false;
    for (const auto& d : analyzer.getErrors()) {
        if (d.message.find("unknown compile_time member 'nope'") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

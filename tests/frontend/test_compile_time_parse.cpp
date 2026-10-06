// P1-04 / CT-01: `compile_time` 顶层声明解析——`compile_time.if` 条件编译
// 与 `compile_time.static_assert` parse 成专有 DeclAST 节点（非关键字，
// 成员名集合内前缀特判）。

#include <gtest/gtest.h>

#include <string>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "ast/Decl.h"
#include "ast/Expr.h"

#include <spdlog/spdlog.h>

static std::unique_ptr<TranslationUnitAST> parse(const std::string& source) {
    Lexer lexer("compile_time_parse_test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse();
}

class CompileTimeParse : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }
};

TEST_F(CompileTimeParse, CompileTimeIfParses) {
    auto tu = parse("compile_time.if (1) { int32 a; } else { int32 b; }");
    ASSERT_NE(tu, nullptr);
    ASSERT_FALSE(tu->declarations.empty());
    auto* ctIf = dynamic_cast<CompileTimeIfDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(ctIf, nullptr);
    ASSERT_NE(ctIf->cond, nullptr);
    ASSERT_EQ(ctIf->thenDecls.size(), 1u);
    ASSERT_EQ(ctIf->elseDecls.size(), 1u);
}

TEST_F(CompileTimeParse, CompileTimeIfElseOptional) {
    auto tu = parse("compile_time.if (1) { int32 a; }");
    ASSERT_NE(tu, nullptr);
    auto* ctIf = dynamic_cast<CompileTimeIfDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(ctIf, nullptr);
    EXPECT_TRUE(ctIf->elseDecls.empty());
}

TEST_F(CompileTimeParse, CompileTimeAssertParses) {
    auto tu = parse("compile_time.static_assert(1 == 1, \"x\");");
    ASSERT_NE(tu, nullptr);
    ASSERT_FALSE(tu->declarations.empty());
    auto* ctAssert = dynamic_cast<CompileTimeAssertDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(ctAssert, nullptr);
    ASSERT_NE(ctAssert->call, nullptr);
    auto* call = dynamic_cast<MethodCallExprAST*>(ctAssert->call.get());
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->methodName, "static_assert");
    ASSERT_EQ(call->args.size(), 2u);
}

TEST_F(CompileTimeParse, BranchDeclsParse) {
    auto tu = parse("compile_time.if (1) { struct S { int32 x; } int32 f() { return 0; } }");
    ASSERT_NE(tu, nullptr);
    auto* ctIf = dynamic_cast<CompileTimeIfDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(ctIf, nullptr);
    ASSERT_EQ(ctIf->thenDecls.size(), 2u);
    EXPECT_NE(dynamic_cast<StructDeclAST*>(ctIf->thenDecls[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<FunctionDeclAST*>(ctIf->thenDecls[1].get()), nullptr);
}

#include <gtest/gtest.h>
#include "sema/SemanticAnalyzer.h"
#include "ast/Expr.h"
#include "ast/Stmt.h"
#include "ast/Decl.h"
#include "ast/Type.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"

class SemanticAnalyzerTest : public ::testing::Test {
protected:
    void SetUp() override {
        analyzer = std::make_unique<SemanticAnalyzer>();
        typeCtx = &TypeContext::instance();
    }

    std::unique_ptr<SemanticAnalyzer> analyzer;
    TypeContext* typeCtx;
};

TEST_F(SemanticAnalyzerTest, ValidProgramPasses) {
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<VarDeclAST>("x", typeCtx->getInt32()));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_TRUE(analyzer->getErrors().empty());
}

TEST_F(SemanticAnalyzerTest, UndeclaredVariableDetected) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(
        std::make_unique<VariableExprAST>("y")));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
    EXPECT_EQ(analyzer->getErrors()[0].level, Diagnostic::Level::Error);
}

TEST_F(SemanticAnalyzerTest, TypeMismatchDetected) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    auto lhs = std::make_unique<VariableExprAST>("x");
    auto rhs = std::make_unique<StringExprAST>("hello");
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(
        std::make_unique<AssignmentExprAST>(
            AssignOp::Assign, std::move(lhs), std::move(rhs))));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    params.push_back(std::make_unique<ParamDeclAST>("x", typeCtx->getInt32()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
}

TEST_F(SemanticAnalyzerTest, FunctionCallArgumentMismatch) {
    auto callee = std::make_unique<VariableExprAST>("add");
    std::vector<std::unique_ptr<ExprAST>> callArgs;
    callArgs.push_back(std::make_unique<NumberExprAST>(1));
    auto callExpr = std::make_unique<CallExprAST>("add", std::move(callArgs));

    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::move(callExpr)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    params.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getInt32()));
    params.push_back(std::make_unique<ParamDeclAST>("b", typeCtx->getInt32()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "add", typeCtx->getInt32(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
    EXPECT_EQ(analyzer->getErrors()[0].level, Diagnostic::Level::Error);
}

TEST_F(SemanticAnalyzerTest, ValidFunctionCall) {
    std::vector<std::unique_ptr<ParamDeclAST>> callParams;
    std::vector<std::unique_ptr<ExprAST>> callArgs;
    callArgs.push_back(std::make_unique<NumberExprAST>(1));
    callArgs.push_back(std::make_unique<NumberExprAST>(2));
    auto callExpr = std::make_unique<CallExprAST>("add", std::move(callArgs));

    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::move(callExpr)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    params.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getInt32()));
    params.push_back(std::make_unique<ParamDeclAST>("b", typeCtx->getInt32()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "add", typeCtx->getInt32(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_TRUE(analyzer->getErrors().empty());
}

TEST_F(SemanticAnalyzerTest, ScopeNestingWorks) {
    std::vector<std::unique_ptr<StmtAST>> innerStmts;
    innerStmts.push_back(std::make_unique<ExprStmtAST>(
        std::make_unique<VariableExprAST>("x")));
    auto innerBody = std::make_unique<CompoundStmtAST>(std::move(innerStmts));

    std::vector<std::unique_ptr<StmtAST>> outerStmts;
    outerStmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getInt32())));
    outerStmts.push_back(std::move(innerBody));
    auto outerBody = std::make_unique<CompoundStmtAST>(std::move(outerStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, outerBody));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_TRUE(analyzer->getErrors().empty());
}

TEST_F(SemanticAnalyzerTest, RedefinitionDetected) {
    std::vector<std::unique_ptr<StmtAST>> stmts;
    stmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getInt32())));
    stmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getInt32())));
    auto body = std::make_unique<CompoundStmtAST>(std::move(stmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
}

TEST_F(SemanticAnalyzerTest, BinaryOpTypeCheck) {
    auto lhs = std::make_unique<StringExprAST>("hello");
    auto rhs = std::make_unique<StringExprAST>("world");
    auto binExpr = std::make_unique<BinaryExprAST>(
        BinaryOp::Add, std::move(lhs), std::move(rhs));

    std::vector<std::unique_ptr<StmtAST>> stmts;
    stmts.push_back(std::make_unique<ExprStmtAST>(std::move(binExpr)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(stmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
}

TEST_F(SemanticAnalyzerTest, BinaryOpErrorMessageIncludesTypes) {
    auto lhs = std::make_unique<StringExprAST>("hello");
    auto rhs = std::make_unique<StringExprAST>("world");
    auto binExpr = std::make_unique<BinaryExprAST>(
        BinaryOp::Add, std::move(lhs), std::move(rhs));

    std::vector<std::unique_ptr<StmtAST>> stmts;
    stmts.push_back(std::make_unique<ExprStmtAST>(std::move(binExpr)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(stmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("char*"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, AssignmentTypeMismatchErrorMessageIncludesTypes) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    auto lhs = std::make_unique<VariableExprAST>("x");
    auto rhs = std::make_unique<StringExprAST>("hello");
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(
        std::make_unique<AssignmentExprAST>(
            AssignOp::Assign, std::move(lhs), std::move(rhs))));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    params.push_back(std::make_unique<ParamDeclAST>("x", typeCtx->getInt32()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("int"), std::string::npos);
    EXPECT_NE(analyzer->getErrors()[0].message.find("char*"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, UndeclaredVariableErrorMessageIncludesName) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(
        std::make_unique<VariableExprAST>("y")));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("y"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, ReturnTypeErrorIncludesTypes) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ReturnStmtAST>(
        std::make_unique<StringExprAST>("hello")));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getFloat32(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("float"), std::string::npos);
    EXPECT_NE(analyzer->getErrors()[0].message.find("char*"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, DiagnosticHasSourceLocation) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(
        std::make_unique<VariableExprAST>("y")));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_EQ(analyzer->getErrors()[0].level, Diagnostic::Level::Error);
    EXPECT_FALSE(analyzer->getErrors()[0].message.empty());
}

TEST_F(SemanticAnalyzerTest, UndeclaredFunctionErrorMessage) {
    auto callExpr = std::make_unique<CallExprAST>("nonexistent", std::vector<std::unique_ptr<ExprAST>>());

    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::move(callExpr)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("undeclared"), std::string::npos);
    EXPECT_NE(analyzer->getErrors()[0].message.find("nonexistent"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, WrongNumberOfArgumentsErrorMessage) {
    std::vector<std::unique_ptr<ExprAST>> callArgs;
    callArgs.push_back(std::make_unique<NumberExprAST>(1));
    callArgs.push_back(std::make_unique<NumberExprAST>(2));
    callArgs.push_back(std::make_unique<NumberExprAST>(3));
    auto callExpr = std::make_unique<CallExprAST>("add", std::move(callArgs));

    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::move(callExpr)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    params.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getInt32()));
    params.push_back(std::make_unique<ParamDeclAST>("b", typeCtx->getInt32()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "add", typeCtx->getInt32(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("no matching function"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, RedeclarationErrorMessage) {
    std::vector<std::unique_ptr<StmtAST>> stmts;
    stmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getInt32())));
    stmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getFloat32())));
    auto body = std::make_unique<CompoundStmtAST>(std::move(stmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("redeclaration"), std::string::npos);
    EXPECT_NE(analyzer->getErrors()[0].message.find("x"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, NonVoidFunctionMustReturnValueErrorMessage) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ReturnStmtAST>(nullptr));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getInt32(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("must return a value"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, CastWarningFormat) {
    // Cast from int to float should produce a warning
    auto castExpr = std::make_unique<CastExprAST>(
        typeCtx->getFloat32(), std::make_unique<NumberExprAST>(42));

    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::move(castExpr)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    // int to float is compatible, so no warning expected
    // But if we cast from struct*, it should warn
}

TEST_F(SemanticAnalyzerTest, BinaryOpErrorMessageContainsOperatorSymbol) {
    auto lhs = std::make_unique<StringExprAST>("hello");
    auto rhs = std::make_unique<StringExprAST>("world");
    auto binExpr = std::make_unique<BinaryExprAST>(
        BinaryOp::Add, std::move(lhs), std::move(rhs));

    std::vector<std::unique_ptr<StmtAST>> stmts;
    stmts.push_back(std::make_unique<ExprStmtAST>(std::move(binExpr)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(stmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("+"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, AssignmentErrorMessageContainsBothTypes) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    auto lhs = std::make_unique<VariableExprAST>("x");
    auto rhs = std::make_unique<StringExprAST>("hello");
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(
        std::make_unique<AssignmentExprAST>(
            AssignOp::Assign, std::move(lhs), std::move(rhs))));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    params.push_back(std::make_unique<ParamDeclAST>("x", typeCtx->getInt32()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("int"), std::string::npos);
    EXPECT_NE(analyzer->getErrors()[0].message.find("char*"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, ReturnTypeErrorContainsFunctionName) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ReturnStmtAST>(
        std::make_unique<StringExprAST>("hello")));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "myFunc", typeCtx->getFloat32(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("myFunc"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, ConstAssignmentError) {
    auto constIntType = new Type(TypeKind::Int32);
    constIntType->isConst = true;

    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", constIntType)));
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(
        std::make_unique<AssignmentExprAST>(
            AssignOp::Assign,
            std::make_unique<VariableExprAST>("x"),
            std::make_unique<NumberExprAST>(20))));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_TRUE(analyzer->getErrors().size() >= 1);
    EXPECT_NE(analyzer->getErrors()[0].message.find("const"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, ConstexprMustHaveInitializer) {
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<VarDeclAST>("x", typeCtx->getInt32(), nullptr, true));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("constexpr"), std::string::npos);
    EXPECT_NE(analyzer->getErrors()[0].message.find("initializer"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, ConstexprConstantFolding) {
    auto initExpr = std::make_unique<BinaryExprAST>(
        BinaryOp::Add,
        std::make_unique<NumberExprAST>(2),
        std::make_unique<BinaryExprAST>(
            BinaryOp::Mul,
            std::make_unique<NumberExprAST>(3),
            std::make_unique<NumberExprAST>(4)));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<VarDeclAST>("x", typeCtx->getInt32(), std::move(initExpr), true));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_TRUE(analyzer->getErrors().empty());

    auto& values = analyzer->getConstexprValues();
    ASSERT_TRUE(values.count("x"));
    EXPECT_EQ(values.at("x").intVal, 14);
}

TEST_F(SemanticAnalyzerTest, ConstexprNonConstantError) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("y", typeCtx->getInt32())));
    bodyStmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getInt32(),
            std::make_unique<VariableExprAST>("y"), true)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "foo", typeCtx->getVoid(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("constant expression"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, ConstexprFunctionWithValidSignature) {
    // constexpr int square(int x) { return x * x; }
    // Should pass - no errors
    auto returnExpr = std::make_unique<BinaryExprAST>(
        BinaryOp::Mul,
        std::make_unique<VariableExprAST>("x"),
        std::make_unique<VariableExprAST>("x"));
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ReturnStmtAST>(std::move(returnExpr)));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    params.push_back(std::make_unique<ParamDeclAST>("x", typeCtx->getInt32()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "square", typeCtx->getInt32(), params, body, true));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_TRUE(analyzer->getErrors().empty());
}

TEST_F(SemanticAnalyzerTest, ConstexprFunctionWithNonLiteralReturnType) {
    // constexpr void bad() { return; }
    // Should emit error: constexpr function must have literal return type
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ReturnStmtAST>(nullptr));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "bad", typeCtx->getVoid(), params, body, true));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("literal return type"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, ConstexprFunctionWithNonLiteralParameter) {
    // constexpr int bad(int* p) { return *p; }
    // Should emit error: parameter must have literal type
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ReturnStmtAST>(
        std::make_unique<UnaryExprAST>(
            UnaryOp::Deref,
            std::make_unique<VariableExprAST>("p"))));
    auto body = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));

    std::vector<std::unique_ptr<ParamDeclAST>> params;
    auto pointerType = std::make_unique<Type>(TypeKind::Pointer, typeCtx->getInt32());
    params.push_back(std::make_unique<ParamDeclAST>("p", pointerType.get()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "bad", typeCtx->getInt32(), params, body, true));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("literal type"), std::string::npos);
}

// ========== Overload Resolution ==========

TEST_F(SemanticAnalyzerTest, OverloadResolutionExactMatch) {
    // Declare two overloaded functions: add(int, int) and add(float, float)
    std::vector<std::unique_ptr<ParamDeclAST>> params1;
    params1.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getInt32()));
    params1.push_back(std::make_unique<ParamDeclAST>("b", typeCtx->getInt32()));
    auto body1 = std::make_unique<CompoundStmtAST>(std::vector<std::unique_ptr<StmtAST>>());
    
    std::vector<std::unique_ptr<ParamDeclAST>> params2;
    params2.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getFloat32()));
    params2.push_back(std::make_unique<ParamDeclAST>("b", typeCtx->getFloat32()));
    auto body2 = std::make_unique<CompoundStmtAST>(std::vector<std::unique_ptr<StmtAST>>());
    
    // Call with int arguments
    std::vector<std::unique_ptr<ExprAST>> callArgs;
    callArgs.push_back(std::make_unique<NumberExprAST>(1));
    callArgs.push_back(std::make_unique<NumberExprAST>(2));
    auto callExpr = std::make_unique<CallExprAST>("add", std::move(callArgs));
    
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::move(callExpr)));
    auto callerBody = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));
    
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>("add", typeCtx->getInt32(), params1, body1));
    decls.push_back(std::make_unique<FunctionDeclAST>("add", typeCtx->getFloat32(), params2, body2));
    
    // Add caller function
    std::vector<std::unique_ptr<ParamDeclAST>> callerParams;
    decls.push_back(std::make_unique<FunctionDeclAST>("caller", typeCtx->getInt32(), callerParams, callerBody));
    
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_TRUE(analyzer->getErrors().empty());
}

TEST_F(SemanticAnalyzerTest, OverloadResolutionNoMatch) {
    std::vector<std::unique_ptr<ParamDeclAST>> params;
    params.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getInt32()));
    params.push_back(std::make_unique<ParamDeclAST>("b", typeCtx->getInt32()));
    auto body = std::make_unique<CompoundStmtAST>(std::vector<std::unique_ptr<StmtAST>>());
    
    // Call with float arguments (no matching overload)
    std::vector<std::unique_ptr<ExprAST>> callArgs;
    callArgs.push_back(std::make_unique<FloatExprAST>(1.0));
    callArgs.push_back(std::make_unique<FloatExprAST>(2.0));
    auto callExpr = std::make_unique<CallExprAST>("add", std::move(callArgs));
    
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<ExprStmtAST>(std::move(callExpr)));
    auto callerBody = std::make_unique<CompoundStmtAST>(std::move(bodyStmts));
    
    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>("add", typeCtx->getInt32(), params, body));
    
    std::vector<std::unique_ptr<ParamDeclAST>> callerParams;
    decls.push_back(std::make_unique<FunctionDeclAST>("caller", typeCtx->getInt32(), callerParams, callerBody));
    
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_FALSE(analyzer->getErrors().empty());
}

// ========== Class Support ==========

TEST(ClassSupport, SemanticAnalysisValidClass) {
    std::string source = R"(
        class Foo {
            int32 x;
            void setX(int32 v) { this->x = v; }
        };
    )";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_TRUE(analyzer.getErrors().empty());
}

TEST(ClassSupport, SemanticAnalysisInvalidBaseClass) {
    std::string source = R"(
        class Derived : public Nonexistent { public: int32 y; };
    )";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_FALSE(analyzer.getErrors().empty());
}

TEST(ClassSupport, SemanticAnalysisInheritedMethod) {
    std::string source = R"(
        class Base {
            public:
            int32 x;
            void setX(int32 v) { this->x = v; }
        };
        class Derived : public Base {
            public:
            int32 y;
        };
    )";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_TRUE(analyzer.getErrors().empty());
}

TEST(ClassSupport, SemanticAnalysisInheritedFieldAccess) {
    std::string source = R"(
        class Base {
            public:
            int32 x;
        };
        class Derived : public Base {
            public:
            int32 y;
        };
        int32 main() {
            Derived d;
            d.x = 5;
            return d.x + d.y;
        }
    )";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_TRUE(analyzer.getErrors().empty());
}

TEST(ClassSupport, DuplicateFunctionDefinitionIsAnError) {
    std::string source = R"(
        int32 f() { return 1; }
        int32 f() { return 2; }
    )";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_FALSE(analyzer.getErrors().empty());
}

TEST(ClassSupport, SemanticAnalysisCircularInheritance) {
    std::string source = R"(
        class A : public B { int32 x; };
        class B : public A { int32 y; };
    )";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_FALSE(analyzer.getErrors().empty());
}

// ========== print / println builtin ==========

namespace {
bool analyzeOk(const std::string& source) {
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast) return false;
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    return analyzer.getErrors().empty();
}
} // namespace

TEST(PrintBuiltin, BuildsTypeDirectedFormat) {
    std::string source = R"(
        int32 main() {
            print("aa {}", 1);
            return 0;
        }
    )";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_TRUE(analyzer.getErrors().empty());

    auto* fn = dynamic_cast<FunctionDeclAST*>(ast->declarations[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_NE(fn->body, nullptr);
    ASSERT_GE(fn->body->stmts.size(), 1u);
    auto* stmt = dynamic_cast<ExprStmtAST*>(fn->body->stmts[0].get());
    ASSERT_NE(stmt, nullptr);
    auto* call = dynamic_cast<CallExprAST*>(stmt->expr.get());
    ASSERT_NE(call, nullptr);
    EXPECT_TRUE(call->isPrint);
    EXPECT_FALSE(call->printNewline);
    EXPECT_EQ(call->printCFormat, "aa %d");
}

TEST(PrintBuiltin, PrintlnAppendsNewline) {
    EXPECT_TRUE(analyzeOk("int32 main() { println(\"x={}\", 1); return 0; }"));
}

TEST(PrintBuiltin, SupportsScalarTypes) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { print(\"{} {} {} {}\", 1, 1.5, 'a', \"s\"); return 0; }"));
}

TEST(PrintBuiltin, RejectsPercentStyle) {
    // `%d` has no {} placeholder, so the extra argument is reported.
    EXPECT_FALSE(analyzeOk("int32 main() { int32 x = 1; print(\"%d\", x); return 0; }"));
}

TEST(PrintBuiltin, ReportsMissingArgument) {
    EXPECT_FALSE(analyzeOk("int32 main() { print(\"{} {}\", 1); return 0; }"));
}

TEST(PrintBuiltin, UnsupportedTypeNeedsToString) {
    EXPECT_FALSE(analyzeOk(
        "struct P { int32 x; }; int32 main() { struct P p; print(\"{}\", p); return 0; }"));
}

TEST(PrintBuiltin, UsesFreeFunctionToString) {
    EXPECT_TRUE(analyzeOk(
        "struct P { int32 x; }; char* to_string(struct P p) { return \"P\"; } "
        "int32 main() { struct P p; print(\"{}\", p); return 0; }"));
}

TEST(PrintBuiltin, UsesMethodToString) {
    EXPECT_TRUE(analyzeOk(
        "class C { public: int32 x; public: char* to_string() { return \"C\"; } }; "
        "int32 main() { C c; print(\"{}\", c); return 0; }"));
}

// LEX-15: literal suffix/default kinds map to fixed-width base types.
TEST_F(SemanticAnalyzerTest, LiteralKindsMapToFixedWidthTypes) {
    auto intKind = [&](LiteralKind k) {
        NumberExprAST n(42, k);
        return analyzer->getExprType(n)->kind;
    };
    EXPECT_EQ(intKind(LiteralKind::Int), TypeKind::Int32);
    EXPECT_EQ(intKind(LiteralKind::UInt), TypeKind::UInt32);
    EXPECT_EQ(intKind(LiteralKind::Long), TypeKind::Int64);
    EXPECT_EQ(intKind(LiteralKind::ULong), TypeKind::UInt64);

    auto floatKind = [&](LiteralKind k) {
        FloatExprAST f(1.5, k);
        return analyzer->getExprType(f)->kind;
    };
    EXPECT_EQ(floatKind(LiteralKind::Float16), TypeKind::Float16);
    EXPECT_EQ(floatKind(LiteralKind::Float32), TypeKind::Float32);
    EXPECT_EQ(floatKind(LiteralKind::Float64), TypeKind::Float64);
    EXPECT_EQ(floatKind(LiteralKind::Float128), TypeKind::Float128);

    // Unsuffixed defaults: integer -> int32, float -> float64.
    NumberExprAST defInt(42);
    EXPECT_EQ(analyzer->getExprType(defInt)->kind, TypeKind::Int32);
    FloatExprAST defFloat(1.5);
    EXPECT_EQ(analyzer->getExprType(defFloat)->kind, TypeKind::Float64);
}

// LEX-05: trailing-dot float literals flow through lexer -> parser -> sema.
TEST_F(SemanticAnalyzerTest, TrailingDotFloatLiteralsAreAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { float64 x = 1.; float32 y = 1.f32; "
        "float64 z = 1.e3; return 0; }"));
}

// DEC-01/SEM-04: class members default to private; outside access is denied
// with a dedicated diagnostic (E2009), not "no member".
TEST(SemanticAnalyzerAccessTest, ClassPrivateDefaultDeniedOutside) {
    EXPECT_FALSE(analyzeOk(
        "class CA { int32 x; }; int32 main() { CA c; c.x = 1; return 0; }"));
}

TEST(SemanticAnalyzerAccessTest, PrivateAccessDiagnosticCode) {
    Lexer lexer("t.c", "class CA { int32 x; }; int32 main() { CA c; c.x = 1; return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_FALSE(analyzer.getErrors().empty());
    bool found = false;
    for (const auto& e : analyzer.getErrors()) {
        if (e.code == DiagnosticCode::SemPrivateMemberAccess) found = true;
    }
    EXPECT_TRUE(found);
}

// In-class method bodies may reach private members through `this->`.
TEST(SemanticAnalyzerAccessTest, ClassMethodAccessesPrivateViaThis) {
    EXPECT_TRUE(analyzeOk(
        "class CC { int32 x; int32 get() { return this->x; } }; "
        "int32 main() { CC c; return 0; }"));
}

// Members in an explicit `public:` section stay accessible (no truncation).
TEST(SemanticAnalyzerAccessTest, PublicSectionMemberAccessible) {
    EXPECT_TRUE(analyzeOk(
        "class CD { private: int32 s; public: int32 o; }; "
        "int32 main() { CD c; c.o = 1; return 0; }"));
}

// struct members remain public by default (DEC-01).
TEST(SemanticAnalyzerAccessTest, StructFieldsDefaultPublic) {
    EXPECT_TRUE(analyzeOk(
        "struct SE { int32 v; }; int32 main() { SE s; s.v = 1; return 0; }"));
}

// TYP-12 Task 2: subscript on slice yields the element type and is an lvalue.
TEST(SliceSemTest, SubscriptOnSliceAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 pick(int32[] s) { return s[0]; } "
        "int32 main() { return 0; }"));
}

TEST(SliceSemTest, SubscriptOnIntRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32 x = 1; return x[0]; }"));
}

// Review Focus #5: nested slices parse and type-check through one layer.
// (Explicit pipeline: analyzeOk() ignores parser errors, which masked the
// fact that `[][]` did not parse at all before TYP-12.)
TEST(SliceSemTest, NestedSliceSubscriptAccepted) {
    Lexer lexer("test.c",
        "int32 pick(int32[][] grid) { return grid[0][0]; } "
        "int32 main() { return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_TRUE(analyzer.getErrors().empty());
}

// TYP-12 Task 3: slice .len member; the only member allowed on a slice.
TEST(SliceSemTest, LenMemberAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int64 n(int32[] s) { return s.len; } "
        "int32 main() { return 0; }"));
}

TEST(SliceSemTest, LenOnNonSliceRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32 x = 1; return x.len; }"));
}

TEST(SliceSemTest, SliceOtherMemberRejected) {
    EXPECT_FALSE(analyzeOk(
        "int64 n(int32[] s) { return s.ptr; } "
        "int32 main() { return 0; }"));
}

// TYP-12 Task 4: T[N] decays to T[] at call sites (zero-copy).
TEST(SliceSemTest, ArrayDecaysToSliceParam) {
    EXPECT_TRUE(analyzeOk(
        "int32 total(int32[] s) { return 0; } "
        "int32 main() { int32 arr[3] = {1,2,3}; return total(arr); }"));
}

TEST(SliceSemTest, CrossElementDecayRejected) {
    EXPECT_FALSE(analyzeOk(
        "int64 total(float64[] s) { return 0; } "
        "int32 main() { int32 arr[3] = {1,2,3}; return total(arr); }"));
}

// TYP-12 Task 5: array->slice decay in assignment/init/return; cross-element
// slice compatibility is rejected (Review Focus #1).
TEST(SliceSemTest, ArrayToSliceAssignmentAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { int32 arr[3] = {1,2,3}; int32[] s = arr; return 0; }"));
}

TEST(SliceSemTest, SliceToArrayRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32[] s; int32 arr[3] = {1,2,3}; arr = s; return 0; }"));
}

TEST(SliceSemTest, CrossElementSliceAssignmentRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32[] a; float64[] b = a; return 0; }"));
}

// TYP-11 D4: returning a view of a local array dangles — rejected.
TEST(SliceSemTest, DanglingLocalArrayReturnRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32[] bad() { int32 a[2] = {1,2}; return a; } "
        "int32 main() { return 0; }"));
}

// TYP-11 D2: T name[N] parameter desugars to slice T[].
TEST(SliceSemTest, ArrayParamAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 APSum(int32 a[2]) { return a[0] + a[1]; } "
        "int32 main() { int32 arr[2] = {3,4}; return APSum(arr); }"));
}

TEST(SliceSemTest, CrossElementArrayParamRejected) {
    EXPECT_FALSE(analyzeOk(
        "int64 APF(float64 a[2]) { return 0; } "
        "int32 main() { int32 arr[2] = {3,4}; return APF(arr); }"));
}

// Function-pointer parameters share parseParamDecl: `(*cb)(int32 a[2])`
// desugars too. (A bare function-type parameter `int32 cb(int32 a[2])` is a
// pre-existing parser gap — not pinned here.) analyzeOk ignores parse
// errors, so use the explicit pipeline.
TEST(SliceSemTest, FunctionPointerArrayParamDesugared) {
    Lexer lexer("test.c",
        "int32 APUse(int32 (*cb)(int32 a[2])) { return 0; } "
        "int32 main() { return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_TRUE(analyzer.getErrors().empty());
}

// analyzeOk ignores parse errors — use the explicit pipeline to pin the
// parse-level rejection.
// TYP-11 2026-10-04: multi-dimensional array parameters are now supported —
// this former rejection pin became a parse-level acceptance pin (row-slice
// desugar); semantic acceptance is covered by MDPExplicitFirstDimAccepted.
TEST(SliceSemTest, MultiDimArrayParamParses) {
    Lexer lexer("test.c",
        "int32 main() { return 0; } "
        "int32 APBad(int32 a[2][3]) { return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
}

TEST(SliceSemTest, UnnamedArrayParamRejected) {
    Lexer lexer("test.c",
        "int32 main() { return 0; } "
        "int32 APAnon(int32[2]) { return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    EXPECT_FALSE(parser.getErrors().empty());
}

// Minor 3 (review): empty brackets desugar too — `T name[]` == `T name[N]`.
TEST(SliceSemTest, EmptyBracketsArrayParamDesugared) {
    EXPECT_TRUE(analyzeOk(
        "int32 APSum2(int32 a[]) { return a[0] + a[1]; } "
        "int32 main() { int32 arr[2] = {3,4}; return APSum2(arr); }"));
}

// TYP-12 legacy: a method with a slice parameter accepts an array argument
// (zero-copy decay), mirroring plain function calls.
TEST(SliceSemTest, MethodSliceParamAcceptsArray) {
    EXPECT_TRUE(analyzeOk(
        "class MCBox { public: void bump(int32[] s) { s[0] = s[0] + 100; } }; "
        "int32 main() { MCBox b; int32 arr[2] = {1,2}; b.bump(arr); return 0; }"));
}

// Exact-match resolution must stay equivalent to the previous behaviour.
TEST(SliceSemTest, MethodExactMatchUnchanged) {
    EXPECT_TRUE(analyzeOk(
        "class MCCnt { public: int32 x; void setX(int32 v) { this->x = v; } }; "
        "int32 main() { MCCnt c; c.setX(42); return 0; }"));
}

// Cross-element decay must not slip through rank-based matching.
TEST(SliceSemTest, MethodCrossElementRejected) {
    EXPECT_FALSE(analyzeOk(
        "class MCF { public: void f(float64[] s) { } }; "
        "int32 main() { MCF m; int32 arr[2] = {1,2}; m.f(arr); return 0; }"));
}

// Resolution through the inheritance chain stores the base class's declared
// parameter types (incl. base this) for codegen mangling.
TEST(SliceSemTest, MethodInheritedSliceParamAccepted) {
    EXPECT_TRUE(analyzeOk(
        "class MCB { public: void bump(int32[] s) { s[0] = s[0] + 1; } }; "
        "class MCD : public MCB { public: int32 y; }; "
        "int32 main() { MCD d; int32 arr[2] = {1,2}; d.bump(arr); return 0; }"));
}

// Missing/invalid arguments must neither crash nor resolve.
TEST(SliceSemTest, MethodNullArgGuard) {
    EXPECT_FALSE(analyzeOk(
        "class MCG { public: void g(int32[] s) { } }; "
        "int32 main() { MCG m; m.g(); return 0; }"));
}

// Review Focus 5: an argument whose type fails to resolve (undeclared
// variable) must be disqualified by the null-argType guard, not fed to
// conversionRank. Arity matches so phase 2's loop body actually runs.
TEST(SliceSemTest, MethodNullArgTypeGuardRejected) {
    EXPECT_FALSE(analyzeOk(
        "class MCG { public: void g(int32[] s) { } }; "
        "int32 main() { MCG m; m.g(nosuch); return 0; }"));
}

// TYP-11: multi-dimensional array declarations.
TEST(SliceSemTest, MultiDimDeclAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return a[1][2]; }"));
}

TEST(SliceSemTest, MultiDimFirstDimInferred) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { int32 a[][3] = {{1,2,3},{4,5,6}}; return a[1][2]; }"));
}

TEST(SliceSemTest, MultiDimSubscriptTypes) {
    EXPECT_TRUE(analyzeOk(
        "int32 MDSum(int32[] s) { return s[0] + s[1] + s[2]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDSum(a[1]); }"));
}

TEST(SliceSemTest, MultiDimWholeToSliceRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 MDSum(int32[] s) { return s[0]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDSum(a); }"));
}

TEST(SliceSemTest, MultiDimDanglingReturnRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32[] MDBad() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return a; } "
        "int32 main() { return 0; }"));
}

TEST(SliceSemTest, MultiDimMemberFieldAccepted) {
    EXPECT_TRUE(analyzeOk(
        "class MDBox { public: int32 g[2][3]; int32 last() { return this->g[1][2]; } }; "
        "int32 main() { MDBox b; return 0; }"));
}

// Review fix: C requires every dimension except the first to be present —
// `a[2][]` must not silently become a zero-length inner array (OOB GEP).
// analyzeOk ignores parse errors, so pin the parse level explicitly.
TEST(SliceSemTest, NonFirstDimDefaultRejected) {
    Lexer lexer("test.c",
        "int32 main() { int32 a[2][] = {{1,2,3},{4,5,6}}; return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    EXPECT_FALSE(parser.getErrors().empty());
}

// Review fix: a scalar (non-brace-list) initializer for an array passed sema
// before (compared against elementType) and was silently dropped or crashed.
TEST(SliceSemTest, GlobalArrayScalarInitRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 g[2] = 5; int32 main() { return 0; }"));
}

// TYP-11: multi-dimensional array parameters desugar to row slices.
// Spec: docs/superpowers/specs/2026-10-04-multidim-array-params-design.md

// D1': f(a) with a: int32[2][3], param int32 m[][3].
TEST(SliceSemTest, MDP2DParamAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 MDPF(int32 m[][3]) { return m[1][2]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDPF(a); }"));
}

// D3': row type mismatch is rejected at compile time.
TEST(SliceSemTest, MDPRowTypeMismatchRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 MDPF(int32 m[][3]) { return m[0][0]; } "
        "int32 main() { int32 b[2][4] = {{1,2,3,4},{5,6,7,8}}; return MDPF(b); }"));
}

// D2': only the first dimension may be empty — pin the parse level
// explicitly (analyzeOk ignores parse errors).
TEST(SliceSemTest, MDPNonFirstDimRejected) {
    Lexer lexer("test.c",
        "int32 MDPF(int32 m[3][]) { return 0; } int32 main() { return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    EXPECT_FALSE(parser.getErrors().empty());
}

// D2': explicit first dimension is documentation, identical to [].
TEST(SliceSemTest, MDPExplicitFirstDimAccepted) {
    EXPECT_TRUE(analyzeOk(
        "int32 MDPG(int32 m[2][3]) { return m[1][2]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDPG(a); }"));
}

// D1': chained subscript inside the callee body.
TEST(SliceSemTest, MDPParamSubscript) {
    EXPECT_TRUE(analyzeOk(
        "int32 MDPGet(int32 m[][3]) { return m[1][2] + m[0][0]; } "
        "int32 main() { int32 a[2][3] = {{1,2,3},{4,5,6}}; return MDPGet(a); }"));
}

// VLA is a deliberate non-feature (2026-10-05): runtime-length allocation
// conflicts with the safety positioning (unbounded stack growth), C23 made
// VLAs optional, and dynamic lengths belong to Slice. Pin the dedicated
// diagnostic, not just "some parse error".
TEST(SliceSemTest, VLALocalDiagnostic) {
    Lexer lexer("test.c",
        "int32 main() { int32 n = 3; int32 a[n]; return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    ASSERT_FALSE(parser.getErrors().empty());
    bool hasVLA = false;
    for (auto& e : parser.getErrors())
        if (e.message.find("variable-length") != std::string::npos)
            hasVLA = true;
    EXPECT_TRUE(hasVLA);
}

TEST(SliceSemTest, VLAParamDiagnostic) {
    Lexer lexer("test.c",
        "int32 f(int32 m[n]) { return 0; } int32 main() { return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    ASSERT_FALSE(parser.getErrors().empty());
    bool hasVLA = false;
    for (auto& e : parser.getErrors())
        if (e.message.find("variable-length") != std::string::npos)
            hasVLA = true;
    EXPECT_TRUE(hasVLA);
}

// Array-assignment defect fix (review finding 2026-10-04): arrays are
// non-modifiable lvalues (C99 6.5.16). Assignment TO an array is rejected
// regardless of RHS kind; assignment FROM an array (decay) stays allowed.
TEST(SliceSemTest, AASGArrayToArrayRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32 a[3] = {1,2,3}; int32 b[3] = {4,5,6}; a = b; return 0; }"));
}

TEST(SliceSemTest, AASGArrayFromPointerRejected) {
    EXPECT_FALSE(analyzeOk(
        "int32 main() { int32 a[3]; int32* p = 0; a = p; return 0; }"));
}

TEST(SliceSemTest, AASGPointerFromArrayOk) {
    EXPECT_TRUE(analyzeOk(
        "int32 main() { int32 a[3] = {1,2,3}; int32* p = 0; p = a; return 0; }"));
}

TEST(SliceSemTest, AASGArrayAssignDiagnostic) {
    Lexer lexer("test.c",
        "int32 main() { int32 a[3] = {1,2,3}; int32 b[3] = {4,5,6}; a = b; return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_FALSE(analyzer.getErrors().empty());
    bool hasLvalueMsg = false;
    for (auto& e : analyzer.getErrors())
        if (e.message.find("not modifiable lvalues") != std::string::npos)
            hasLvalueMsg = true;
    EXPECT_TRUE(hasLvalueMsg);
}

// AGG-10: static members desugar to class-prefixed global symbols.
// Spec: docs/superpowers/specs/2026-10-05-static-members-design.md

TEST(SliceSemTest, SMStaticMethodQualifiedCall) {
    EXPECT_TRUE(analyzeOk(
        "class SMBox { public: static int32 make() { return 7; } }; "
        "int32 main() { return SMBox::make(); }"));
}

TEST(SliceSemTest, SMStaticVarQualifiedAccess) {
    EXPECT_TRUE(analyzeOk(
        "class SMCtr { public: static int32 count = 0; }; "
        "int32 main() { SMCtr::count = 5; return SMCtr::count; }"));
}

TEST(SliceSemTest, SMPrivateStaticExternalRejected) {
    // DS5: E2009 (member access), not a parse error — pin the message.
    Lexer lexer("test.c",
        "class SMP { private: static int32 secret = 1; }; "
        "int32 main() { return SMP::secret; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_FALSE(analyzer.getErrors().empty());
    bool hasE2009 = false;
    for (auto& e : analyzer.getErrors())
        if (e.message.find("private") != std::string::npos) hasE2009 = true;
    EXPECT_TRUE(hasE2009);
}

TEST(SliceSemTest, SMInstancePathRejected) {
    // DS1: static members are not instance members — pin the "no matching
    // method" diagnostic (a parse-level rejection would be wrong here).
    Lexer lexer("test.c",
        "class SMP2 { public: static int32 make() { return 1; } }; "
        "int32 main() { SMP2 obj; return obj.make(); }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_FALSE(analyzer.getErrors().empty());
    bool hasNoMethod = false;
    for (auto& e : analyzer.getErrors())
        if (e.message.find("no matching method") != std::string::npos) hasNoMethod = true;
    EXPECT_TRUE(hasNoMethod);
}

TEST(SliceSemTest, SMStaticInstanceSameName) {
    EXPECT_TRUE(analyzeOk(
        "class SMDual { "
        "public: int32 f(int32 x) { return x; } "
        "static int32 f() { return 42; } }; "
        "int32 main() { SMDual obj; return obj.f(1) + SMDual::f(); }"));
}

TEST(SliceSemTest, SMStaticOverload) {
    EXPECT_TRUE(analyzeOk(
        "class SMOvl { public: static int32 g(int32 x) { return x; } "
        "static int32 g(int32 x, int32 y) { return x + y; } }; "
        "int32 main() { return SMOvl::g(1) + SMOvl::g(1, 2); }"));
}

TEST(SliceSemTest, SMStaticVarNoInitZero) {
    EXPECT_TRUE(analyzeOk(
        "class SMZ { public: static int32 z; }; "
        "int32 main() { return SMZ::z; }"));
}

TEST(SliceSemTest, SMStaticNotInLayout) {
    // DS2: static variables do not occupy object layout — and the member
    // must actually exist (reference it so a silent parser drop fails).
    EXPECT_TRUE(analyzeOk(
        "class SML { public: static int32 big = 0; int32 a; }; "
        "int32 main() { return (sizeof(SML) == sizeof(int32) && SML::big == 0) ? 0 : 1; }"));
}

TEST(SliceSemTest, SMStaticCollisionRejected) {
    // Review Focus 5: hand-written SMC_v collides with the desugared
    // SMC::v — a redefinition error, never silent shadowing.
    EXPECT_FALSE(analyzeOk(
        "class SMC { public: static int32 v = 0; }; "
        "int32 SMC_v = 1; int32 main() { return 0; }"));
}

TEST(SliceSemTest, SMStaticInNamespace) {
    // Review Focus 1: declaration and access sides must agree on the ns prefix.
    EXPECT_TRUE(analyzeOk(
        "namespace smns { class SMN { public: static int32 v = 3; }; } "
        "int32 main() { return smns::SMN::v; }"));
}

// Review C2: a struct whose ONLY members are static must not fall into the
// parser's forward-declaration fallback (which dropped the definition).
TEST(SliceSemTest, SMStructStaticOnly) {
    EXPECT_TRUE(analyzeOk(
        "struct SMS { static int32 v = 4; }; "
        "int32 main() { return SMS::v; }"));
}

TEST(SliceSemTest, SMStructStaticMixed) {
    EXPECT_TRUE(analyzeOk(
        "struct SMS2 { static int32 v = 4; int32 x; }; "
        "int32 main() { SMS2 s; s.x = 1; return SMS2::v + s.x; }"));
}

// Review I1: same-name static and instance members must keep independent
// access levels. Two assertions:
//  - the PUBLIC instance member is not corrupted by the private static's
//    level (no false E2009 on obj.f(1));
//  - the static member's own private level is honored (E2009 on SMI1::f()).
TEST(SliceSemTest, SMAccessLevelIndependent) {
    EXPECT_TRUE(analyzeOk(
        "class SMI1 { "
        "public: int32 f(int32 x) { return x; } "
        "private: static int32 f() { return 42; } }; "
        "int32 main() { SMI1 obj; return obj.f(1); }"));
}

TEST(SliceSemTest, SMPrivateStaticMethodRejected) {
    Lexer lexer("test.c",
        "class SMI3 { "
        "public: int32 f(int32 x) { return x; } "
        "private: static int32 f() { return 42; } }; "
        "int32 main() { return SMI3::f(); }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_TRUE(ast != nullptr);
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_FALSE(analyzer.getErrors().empty());
    bool hasE2009 = false;
    for (auto& e : analyzer.getErrors())
        if (e.message.find("private") != std::string::npos) hasE2009 = true;
    EXPECT_TRUE(hasE2009);
}

// Review I2: a static method may call a sibling static method declared
// LATER in the class body (declaration order must not matter, matching
// instance methods).
TEST(SliceSemTest, SMStaticBackwardReference) {
    EXPECT_TRUE(analyzeOk(
        "class SMI2 { "
        "public: static int32 a() { return SMI2::b(); } "
        "static int32 b() { return 7; } }; "
        "int32 main() { return SMI2::a(); }"));
}

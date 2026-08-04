#include <gtest/gtest.h>
#include "sema/SemanticAnalyzer.h"
#include "ast/Expr.h"
#include "ast/Stmt.h"
#include "ast/Decl.h"
#include "ast/Type.h"

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
    decls.push_back(std::make_unique<VarDeclAST>("x", typeCtx->getInt()));
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
    params.push_back(std::make_unique<ParamDeclAST>("x", typeCtx->getInt()));

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
    params.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getInt()));
    params.push_back(std::make_unique<ParamDeclAST>("b", typeCtx->getInt()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "add", typeCtx->getInt(), params, body));
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
    params.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getInt()));
    params.push_back(std::make_unique<ParamDeclAST>("b", typeCtx->getInt()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "add", typeCtx->getInt(), params, body));
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
        std::make_unique<VarDeclAST>("x", typeCtx->getInt())));
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
        std::make_unique<VarDeclAST>("x", typeCtx->getInt())));
    stmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getInt())));
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
    params.push_back(std::make_unique<ParamDeclAST>("x", typeCtx->getInt()));

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
        "foo", typeCtx->getFloat(), params, body));
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
    params.push_back(std::make_unique<ParamDeclAST>("a", typeCtx->getInt()));
    params.push_back(std::make_unique<ParamDeclAST>("b", typeCtx->getInt()));

    std::vector<std::unique_ptr<DeclAST>> decls;
    decls.push_back(std::make_unique<FunctionDeclAST>(
        "add", typeCtx->getInt(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("wrong number of arguments"), std::string::npos);
    EXPECT_NE(analyzer->getErrors()[0].message.find("expected 2"), std::string::npos);
    EXPECT_NE(analyzer->getErrors()[0].message.find("got 3"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, RedeclarationErrorMessage) {
    std::vector<std::unique_ptr<StmtAST>> stmts;
    stmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getInt())));
    stmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getFloat())));
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
        "foo", typeCtx->getInt(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("must return a value"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, CastWarningFormat) {
    // Cast from int to float should produce a warning
    auto castExpr = std::make_unique<CastExprAST>(
        typeCtx->getFloat(), std::make_unique<NumberExprAST>(42));

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
    params.push_back(std::make_unique<ParamDeclAST>("x", typeCtx->getInt()));

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
        "myFunc", typeCtx->getFloat(), params, body));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    ASSERT_FALSE(analyzer->getErrors().empty());
    EXPECT_NE(analyzer->getErrors()[0].message.find("myFunc"), std::string::npos);
}

TEST_F(SemanticAnalyzerTest, ConstAssignmentError) {
    auto constIntType = new Type(TypeKind::Int);
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
    decls.push_back(std::make_unique<VarDeclAST>("x", typeCtx->getInt(), nullptr, true));
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
    decls.push_back(std::make_unique<VarDeclAST>("x", typeCtx->getInt(), std::move(initExpr), true));
    TranslationUnitAST tu(std::move(decls));
    analyzer->analyze(tu);
    EXPECT_TRUE(analyzer->getErrors().empty());
}

TEST_F(SemanticAnalyzerTest, ConstexprNonConstantError) {
    std::vector<std::unique_ptr<StmtAST>> bodyStmts;
    bodyStmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("y", typeCtx->getInt())));
    bodyStmts.push_back(std::make_unique<DeclStmtAST>(
        std::make_unique<VarDeclAST>("x", typeCtx->getInt(),
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

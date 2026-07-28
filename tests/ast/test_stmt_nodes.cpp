#include <gtest/gtest.h>
#include "ast/Stmt.h"
#include "ast/Type.h"

class StmtNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        typeCtx = &TypeContext::instance();
    }

    TypeContext* typeCtx;
};

TEST_F(StmtNodeTest, IfStmtAST) {
    auto cond = std::make_unique<VariableExprAST>("x");
    auto then = std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1));
    auto else_ = std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(2));

    auto ifStmt = std::make_unique<IfStmtAST>(
        std::move(cond), std::move(then), std::move(else_));

    EXPECT_NE(ifStmt->cond, nullptr);
    EXPECT_NE(ifStmt->thenStmt, nullptr);
    EXPECT_NE(ifStmt->elseStmt, nullptr);
}

TEST_F(StmtNodeTest, IfStmtWithoutElse) {
    auto cond = std::make_unique<VariableExprAST>("x");
    auto then = std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1));

    auto ifStmt = std::make_unique<IfStmtAST>(
        std::move(cond), std::move(then));

    EXPECT_NE(ifStmt->cond, nullptr);
    EXPECT_NE(ifStmt->thenStmt, nullptr);
    EXPECT_EQ(ifStmt->elseStmt, nullptr);
}

TEST_F(StmtNodeTest, WhileStmtAST) {
    auto cond = std::make_unique<VariableExprAST>("x");
    auto body = std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1));

    auto whileStmt = std::make_unique<WhileStmtAST>(
        std::move(cond), std::move(body));

    EXPECT_NE(whileStmt->cond, nullptr);
    EXPECT_NE(whileStmt->body, nullptr);
}

TEST_F(StmtNodeTest, DoWhileStmtAST) {
    auto cond = std::make_unique<VariableExprAST>("x");
    auto body = std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1));

    auto doWhile = std::make_unique<DoWhileStmtAST>(
        std::move(cond), std::move(body));

    EXPECT_NE(doWhile->cond, nullptr);
    EXPECT_NE(doWhile->body, nullptr);
}

TEST_F(StmtNodeTest, ForStmtAST) {
    auto init = std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(0));
    auto cond = std::make_unique<VariableExprAST>("x");
    auto inc = std::make_unique<NumberExprAST>(1);
    auto body = std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(2));

    auto forStmt = std::make_unique<ForStmtAST>(
        std::move(init), std::move(cond), std::move(inc), std::move(body));

    EXPECT_NE(forStmt->init, nullptr);
    EXPECT_NE(forStmt->cond, nullptr);
    EXPECT_NE(forStmt->inc, nullptr);
    EXPECT_NE(forStmt->body, nullptr);
}

TEST_F(StmtNodeTest, SwitchStmtAST) {
    auto cond = std::make_unique<VariableExprAST>("x");
    std::vector<std::unique_ptr<StmtAST>> cases;
    cases.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1)));
    cases.push_back(std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(2)));

    auto switchStmt = std::make_unique<SwitchStmtAST>(
        std::move(cond), std::move(cases));

    EXPECT_NE(switchStmt->cond, nullptr);
    EXPECT_EQ(switchStmt->cases.size(), 2);
}

TEST_F(StmtNodeTest, BreakStmtAST) {
    auto breakStmt = std::make_unique<BreakStmtAST>();
    EXPECT_NE(breakStmt, nullptr);
}

TEST_F(StmtNodeTest, ContinueStmtAST) {
    auto continueStmt = std::make_unique<ContinueStmtAST>();
    EXPECT_NE(continueStmt, nullptr);
}

TEST_F(StmtNodeTest, GotoStmtAST) {
    auto gotoStmt = std::make_unique<GotoStmtAST>("label1");
    EXPECT_EQ(gotoStmt->label, "label1");
}

TEST_F(StmtNodeTest, LabelStmtAST) {
    auto stmt = std::make_unique<ExprStmtAST>(std::make_unique<NumberExprAST>(1));
    auto labelStmt = std::make_unique<LabelStmtAST>("myLabel", std::move(stmt));

    EXPECT_EQ(labelStmt->label, "myLabel");
    EXPECT_NE(labelStmt->stmt, nullptr);
}

TEST_F(StmtNodeTest, NullStmtAST) {
    auto nullStmt = std::make_unique<NullStmtAST>();
    EXPECT_NE(nullStmt, nullptr);
}

TEST_F(StmtNodeTest, SourceLocation) {
    auto stmt = std::make_unique<BreakStmtAST>();
    stmt->setLocation("test.c", 20, 10);

    EXPECT_EQ(stmt->sourceFile, "test.c");
    EXPECT_EQ(stmt->sourceLine, 20);
    EXPECT_EQ(stmt->sourceColumn, 10);
}

#include <gtest/gtest.h>
#include "ast/Expr.h"
#include "ast/Type.h"

class ExprNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        typeCtx = &TypeContext::instance();
    }

    TypeContext* typeCtx;
};

TEST_F(ExprNodeTest, AssignmentExprAST) {
    auto lhs = std::make_unique<VariableExprAST>("x");
    auto rhs = std::make_unique<NumberExprAST>(42);
    auto assign = std::make_unique<AssignmentExprAST>(
        AssignOp::Assign, std::move(lhs), std::move(rhs));

    EXPECT_EQ(assign->op, AssignOp::Assign);
    EXPECT_NE(assign->lhs, nullptr);
    EXPECT_NE(assign->rhs, nullptr);
}

TEST_F(ExprNodeTest, CompoundAssignmentOps) {
    auto lhs = std::make_unique<VariableExprAST>("x");
    auto rhs = std::make_unique<NumberExprAST>(1);

    auto addAssign = std::make_unique<AssignmentExprAST>(
        AssignOp::AddAssign, std::make_unique<VariableExprAST>("x"), std::make_unique<NumberExprAST>(1));
    auto subAssign = std::make_unique<AssignmentExprAST>(
        AssignOp::SubAssign, std::make_unique<VariableExprAST>("x"), std::make_unique<NumberExprAST>(1));
    auto mulAssign = std::make_unique<AssignmentExprAST>(
        AssignOp::MulAssign, std::make_unique<VariableExprAST>("x"), std::make_unique<NumberExprAST>(1));
    auto divAssign = std::make_unique<AssignmentExprAST>(
        AssignOp::DivAssign, std::make_unique<VariableExprAST>("x"), std::make_unique<NumberExprAST>(1));

    EXPECT_EQ(addAssign->op, AssignOp::AddAssign);
    EXPECT_EQ(subAssign->op, AssignOp::SubAssign);
    EXPECT_EQ(mulAssign->op, AssignOp::MulAssign);
    EXPECT_EQ(divAssign->op, AssignOp::DivAssign);
}

TEST_F(ExprNodeTest, TernaryExprAST) {
    auto cond = std::make_unique<VariableExprAST>("x");
    auto then = std::make_unique<NumberExprAST>(1);
    auto else_ = std::make_unique<NumberExprAST>(0);

    auto ternary = std::make_unique<TernaryExprAST>(
        std::move(cond), std::move(then), std::move(else_));

    EXPECT_NE(ternary->cond, nullptr);
    EXPECT_NE(ternary->then, nullptr);
    EXPECT_NE(ternary->elseExpr, nullptr);
}

TEST_F(ExprNodeTest, CastExprAST) {
    auto expr = std::make_unique<NumberExprAST>(42);
    auto cast = std::make_unique<CastExprAST>(typeCtx->getFloat(), std::move(expr));

    EXPECT_EQ(cast->castType->kind, TypeKind::Float);
    EXPECT_NE(cast->expr, nullptr);
}

TEST_F(ExprNodeTest, CommaExprAST) {
    auto left = std::make_unique<NumberExprAST>(1);
    auto right = std::make_unique<NumberExprAST>(2);
    auto comma = std::make_unique<CommaExprAST>(std::move(left), std::move(right));

    EXPECT_NE(comma->left, nullptr);
    EXPECT_NE(comma->right, nullptr);
}

TEST_F(ExprNodeTest, PostfixIncDecExprAST) {
    auto expr = std::make_unique<VariableExprAST>("i");
    auto inc = std::make_unique<PostfixIncDecExprAST>(std::make_unique<VariableExprAST>("i"), true);
    auto dec = std::make_unique<PostfixIncDecExprAST>(std::make_unique<VariableExprAST>("i"), false);

    EXPECT_TRUE(inc->isIncrement);
    EXPECT_FALSE(dec->isIncrement);
}

TEST_F(ExprNodeTest, ArrayAccessExprAST) {
    auto arr = std::make_unique<VariableExprAST>("arr");
    auto idx = std::make_unique<NumberExprAST>(0);
    auto access = std::make_unique<ArrayAccessExprAST>(std::move(arr), std::move(idx));

    EXPECT_NE(access->array, nullptr);
    EXPECT_NE(access->index, nullptr);
}

TEST_F(ExprNodeTest, MemberAccessExprAST) {
    auto obj = std::make_unique<VariableExprAST>("s");
    auto dot = std::make_unique<MemberAccessExprAST>(
        MemberAccessKind::Dot, std::make_unique<VariableExprAST>("s"), "field");
    auto arrow = std::make_unique<MemberAccessExprAST>(
        MemberAccessKind::Arrow, std::make_unique<VariableExprAST>("p"), "field");

    EXPECT_EQ(dot->accessKind, MemberAccessKind::Dot);
    EXPECT_EQ(dot->memberName, "field");
    EXPECT_EQ(arrow->accessKind, MemberAccessKind::Arrow);
}

TEST_F(ExprNodeTest, SizeofExprAST) {
    auto sizeofType = std::make_unique<SizeofExprAST>(typeCtx->getInt());
    EXPECT_EQ(sizeofType->sizeofType->kind, TypeKind::Int);
    EXPECT_EQ(sizeofType->expr, nullptr);

    auto sizeofExpr = std::make_unique<SizeofExprAST>(
        nullptr, std::make_unique<VariableExprAST>("x"));
    EXPECT_EQ(sizeofExpr->sizeofType, nullptr);
    EXPECT_NE(sizeofExpr->expr, nullptr);
}

TEST_F(ExprNodeTest, InitializerListExprAST) {
    std::vector<std::unique_ptr<ExprAST>> initList;
    initList.push_back(std::make_unique<NumberExprAST>(1));
    initList.push_back(std::make_unique<NumberExprAST>(2));
    initList.push_back(std::make_unique<NumberExprAST>(3));

    auto initListExpr = std::make_unique<InitializerListExprAST>(std::move(initList));
    EXPECT_EQ(initListExpr->initializers.size(), 3);
}

TEST_F(ExprNodeTest, SourceLocation) {
    auto expr = std::make_unique<NumberExprAST>(42);
    expr->setLocation("test.c", 10, 5);

    EXPECT_EQ(expr->sourceFile, "test.c");
    EXPECT_EQ(expr->sourceLine, 10);
    EXPECT_EQ(expr->sourceColumn, 5);
}

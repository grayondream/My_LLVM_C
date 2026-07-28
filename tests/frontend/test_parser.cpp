#include <gtest/gtest.h>
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "ast/Expr.h"
#include "ast/Type.h"
#include <spdlog/spdlog.h>

class ParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }
};

class LexerTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }
};

static std::vector<Token> lex(const std::string& source) {
    Lexer lexer("test.c", source);
    return lexer.tokenize();
}

static auto parse(const std::string& source) {
    auto tokens = lex(source);
    Parser parser(tokens);
    return parser.parse();
}

static std::unique_ptr<TranslationUnitAST> s_lastTU;

static auto parseExpr(const std::string& source) {
    std::string wrapped = "int f() { return " + source + "; }";
    s_lastTU = parse(wrapped);
    if (!s_lastTU || s_lastTU->declarations.empty()) return (ExprAST*)nullptr;
    auto func = dynamic_cast<FunctionDeclAST*>(s_lastTU->declarations[0].get());
    if (!func || !func->body || func->body->stmts.empty()) return (ExprAST*)nullptr;
    auto ret = dynamic_cast<ReturnStmtAST*>(func->body->stmts[0].get());
    if (!ret) return (ExprAST*)nullptr;
    return ret->value.get();
}

// ========== Number Literals ==========

TEST_F(ParserTest, NumberLiteral) {
    auto expr = parseExpr("42");
    ASSERT_NE(expr, nullptr);
    auto num = dynamic_cast<NumberExprAST*>(expr);
    ASSERT_NE(num, nullptr);
    EXPECT_EQ(num->value, 42);
}

TEST_F(ParserTest, ZeroLiteral) {
    auto expr = parseExpr("0");
    ASSERT_NE(expr, nullptr);
    auto num = dynamic_cast<NumberExprAST*>(expr);
    ASSERT_NE(num, nullptr);
    EXPECT_EQ(num->value, 0);
}

// ========== Identifiers ==========

TEST_F(ParserTest, Identifier) {
    auto expr = parseExpr("x");
    ASSERT_NE(expr, nullptr);
    auto var = dynamic_cast<VariableExprAST*>(expr);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "x");
}

// ========== Binary Operators - Precedence ==========

TEST_F(ParserTest, Addition) {
    auto expr = parseExpr("1 + 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Add);
    EXPECT_NE(dynamic_cast<NumberExprAST*>(bin->left.get()), nullptr);
    EXPECT_NE(dynamic_cast<NumberExprAST*>(bin->right.get()), nullptr);
}

TEST_F(ParserTest, Subtraction) {
    auto expr = parseExpr("5 - 3");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Sub);
}

TEST_F(ParserTest, Multiplication) {
    auto expr = parseExpr("2 * 3");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Mul);
}

TEST_F(ParserTest, Division) {
    auto expr = parseExpr("10 / 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Div);
}

TEST_F(ParserTest, Modulo) {
    auto expr = parseExpr("7 % 3");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Mod);
}

TEST_F(ParserTest, PrecedenceMulBeforeAdd) {
    // 1 + 2 * 3 should parse as 1 + (2 * 3)
    auto expr = parseExpr("1 + 2 * 3");
    ASSERT_NE(expr, nullptr);
    auto add = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add->op, BinaryOp::Add);

    auto rhs = dynamic_cast<BinaryExprAST*>(add->right.get());
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, BinaryOp::Mul);
}

TEST_F(ParserTest, PrecedenceAddBeforeShift) {
    // 1 << 2 + 3 should parse as 1 << (2 + 3) since + has higher prec than <<
    auto expr = parseExpr("1 << 2 + 3");
    ASSERT_NE(expr, nullptr);
    auto shift = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(shift, nullptr);
    EXPECT_EQ(shift->op, BinaryOp::LShift);

    auto rhs = dynamic_cast<BinaryExprAST*>(shift->right.get());
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, BinaryOp::Add);
}

TEST_F(ParserTest, PrecedenceShiftBeforeRelational) {
    // 1 < 2 << 3 should parse as 1 < (2 << 3) since << has higher prec than <
    auto expr = parseExpr("1 < 2 << 3");
    ASSERT_NE(expr, nullptr);
    auto lt = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(lt, nullptr);
    EXPECT_EQ(lt->op, BinaryOp::Lt);

    auto rhs = dynamic_cast<BinaryExprAST*>(lt->right.get());
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, BinaryOp::LShift);
}

TEST_F(ParserTest, ChainedAddition) {
    // 1 + 2 + 3 should parse as (1 + 2) + 3 (left-to-right)
    auto expr = parseExpr("1 + 2 + 3");
    ASSERT_NE(expr, nullptr);
    auto outer = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, BinaryOp::Add);

    auto lhs = dynamic_cast<BinaryExprAST*>(outer->left.get());
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->op, BinaryOp::Add);
}

TEST_F(ParserTest, SubtractionLeftAssoc) {
    // 5 - 3 - 1 should parse as (5 - 3) - 1
    auto expr = parseExpr("5 - 3 - 1");
    ASSERT_NE(expr, nullptr);
    auto outer = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, BinaryOp::Sub);

    auto lhs = dynamic_cast<BinaryExprAST*>(outer->left.get());
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->op, BinaryOp::Sub);
}

// ========== Relational Operators ==========

TEST_F(ParserTest, LessThan) {
    auto expr = parseExpr("1 < 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Lt);
}

TEST_F(ParserTest, GreaterThan) {
    auto expr = parseExpr("2 > 1");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Gt);
}

TEST_F(ParserTest, LessEqual) {
    auto expr = parseExpr("1 <= 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Le);
}

TEST_F(ParserTest, GreaterEqual) {
    auto expr = parseExpr("2 >= 1");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Ge);
}

// ========== Equality Operators ==========

TEST_F(ParserTest, Equal) {
    auto expr = parseExpr("1 == 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Eq);
}

TEST_F(ParserTest, NotEqual) {
    auto expr = parseExpr("1 != 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::NotEq);
}

// ========== Logical Operators ==========

TEST_F(ParserTest, LogicalAnd) {
    auto expr = parseExpr("1 && 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::And);
}

TEST_F(ParserTest, LogicalOr) {
    auto expr = parseExpr("1 || 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::Or);
}

TEST_F(ParserTest, PrecedenceAndBeforeOr) {
    // 1 || 2 && 3 should parse as 1 || (2 && 3)
    auto expr = parseExpr("1 || 2 && 3");
    ASSERT_NE(expr, nullptr);
    auto or_bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(or_bin, nullptr);
    EXPECT_EQ(or_bin->op, BinaryOp::Or);

    auto rhs = dynamic_cast<BinaryExprAST*>(or_bin->right.get());
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, BinaryOp::And);
}

// ========== Bitwise Operators ==========

TEST_F(ParserTest, BitwiseAnd) {
    auto expr = parseExpr("1 & 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::BitAnd);
}

TEST_F(ParserTest, BitwiseOr) {
    auto expr = parseExpr("1 | 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::BitOr);
}

TEST_F(ParserTest, BitwiseXor) {
    auto expr = parseExpr("1 ^ 2");
    ASSERT_NE(expr, nullptr);
    auto bin = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->op, BinaryOp::BitXor);
}

// ========== Shift Operators (need LShift/RShift BinaryOp) ==========

// TODO: Enable when LShift/RShift BinaryOp are added
// TEST_F(ParserTest, LeftShift) {
//     auto expr = parseExpr("1 << 2");
//     ASSERT_NE(expr, nullptr);
//     auto bin = dynamic_cast<BinaryExprAST*>(expr);
//     ASSERT_NE(bin, nullptr);
//     EXPECT_EQ(bin->op, BinaryOp::LShift);
// }

// TEST_F(ParserTest, RightShift) {
//     auto expr = parseExpr("4 >> 2");
//     ASSERT_NE(expr, nullptr);
//     auto bin = dynamic_cast<BinaryExprAST*>(expr);
//     ASSERT_NE(bin, nullptr);
//     EXPECT_EQ(bin->op, BinaryOp::RShift);
// }

// ========== Unary Operators ==========

TEST_F(ParserTest, UnaryNegate) {
    auto expr = parseExpr("-1");
    ASSERT_NE(expr, nullptr);
    auto unary = dynamic_cast<UnaryExprAST*>(expr);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->op, UnaryOp::Minus);
}

TEST_F(ParserTest, UnaryPlus) {
    auto expr = parseExpr("+1");
    ASSERT_NE(expr, nullptr);
    auto unary = dynamic_cast<UnaryExprAST*>(expr);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->op, UnaryOp::Plus);
}

TEST_F(ParserTest, UnaryNot) {
    auto expr = parseExpr("!1");
    ASSERT_NE(expr, nullptr);
    auto unary = dynamic_cast<UnaryExprAST*>(expr);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->op, UnaryOp::Not);
}

TEST_F(ParserTest, UnaryDeref) {
    auto expr = parseExpr("*x");
    ASSERT_NE(expr, nullptr);
    auto unary = dynamic_cast<UnaryExprAST*>(expr);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->op, UnaryOp::Deref);
}

TEST_F(ParserTest, UnaryAddressOf) {
    auto expr = parseExpr("&x");
    ASSERT_NE(expr, nullptr);
    auto unary = dynamic_cast<UnaryExprAST*>(expr);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->op, UnaryOp::AddressOf);
}

TEST_F(ParserTest, DoubleNegate) {
    // --1 is lexed as TOKEN_MINUS_MINUS (prefix decrement), not two minuses
    auto expr = parseExpr("--1");
    ASSERT_NE(expr, nullptr);
    auto unary = dynamic_cast<UnaryExprAST*>(expr);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->op, UnaryOp::PreDec);
}

// ========== Postfix Operators ==========

TEST_F(ParserTest, PostfixIncrement) {
    auto expr = parseExpr("x++");
    ASSERT_NE(expr, nullptr);
    auto postfix = dynamic_cast<PostfixIncDecExprAST*>(expr);
    ASSERT_NE(postfix, nullptr);
    EXPECT_TRUE(postfix->isIncrement);
}

TEST_F(ParserTest, PostfixDecrement) {
    auto expr = parseExpr("x--");
    ASSERT_NE(expr, nullptr);
    auto postfix = dynamic_cast<PostfixIncDecExprAST*>(expr);
    ASSERT_NE(postfix, nullptr);
    EXPECT_FALSE(postfix->isIncrement);
}

TEST_F(ParserTest, ArrayAccess) {
    auto expr = parseExpr("arr[0]");
    ASSERT_NE(expr, nullptr);
    auto arr = dynamic_cast<ArrayAccessExprAST*>(expr);
    ASSERT_NE(arr, nullptr);
    EXPECT_NE(dynamic_cast<VariableExprAST*>(arr->array.get()), nullptr);
    EXPECT_NE(dynamic_cast<NumberExprAST*>(arr->index.get()), nullptr);
}

TEST_F(ParserTest, MemberAccessDot) {
    auto expr = parseExpr("s.field");
    ASSERT_NE(expr, nullptr);
    auto member = dynamic_cast<MemberAccessExprAST*>(expr);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->accessKind, MemberAccessKind::Dot);
    EXPECT_EQ(member->memberName, "field");
}

TEST_F(ParserTest, MemberAccessArrow) {
    auto expr = parseExpr("p->field");
    ASSERT_NE(expr, nullptr);
    auto member = dynamic_cast<MemberAccessExprAST*>(expr);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->accessKind, MemberAccessKind::Arrow);
    EXPECT_EQ(member->memberName, "field");
}

// ========== Function Calls ==========

TEST_F(ParserTest, FunctionCallNoArgs) {
    auto expr = parseExpr("foo()");
    ASSERT_NE(expr, nullptr);
    auto call = dynamic_cast<CallExprAST*>(expr);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->callee, "foo");
    EXPECT_EQ(call->args.size(), 0u);
}

TEST_F(ParserTest, FunctionCallOneArg) {
    auto expr = parseExpr("foo(1)");
    ASSERT_NE(expr, nullptr);
    auto call = dynamic_cast<CallExprAST*>(expr);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->callee, "foo");
    EXPECT_EQ(call->args.size(), 1u);
}

TEST_F(ParserTest, FunctionCallMultipleArgs) {
    auto expr = parseExpr("foo(1, 2, 3)");
    ASSERT_NE(expr, nullptr);
    auto call = dynamic_cast<CallExprAST*>(expr);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->callee, "foo");
    EXPECT_EQ(call->args.size(), 3u);
}

// ========== Parenthesized Expressions ==========

TEST_F(ParserTest, ParenthesizedExpr) {
    auto expr = parseExpr("(42)");
    ASSERT_NE(expr, nullptr);
    auto num = dynamic_cast<NumberExprAST*>(expr);
    ASSERT_NE(num, nullptr);
    EXPECT_EQ(num->value, 42);
}

TEST_F(ParserTest, ParenthesizedOverridesPrecedence) {
    auto expr = parseExpr("(1 + 2) * 3");
    ASSERT_NE(expr, nullptr);
    auto mul = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(mul, nullptr);
    EXPECT_EQ(mul->op, BinaryOp::Mul);

    auto lhs = dynamic_cast<BinaryExprAST*>(mul->left.get());
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->op, BinaryOp::Add);
}

// ========== Assignment Operators ==========

TEST_F(ParserTest, SimpleAssignment) {
    auto expr = parseExpr("x = 1");
    ASSERT_NE(expr, nullptr);
    auto assign = dynamic_cast<AssignmentExprAST*>(expr);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->op, AssignOp::Assign);
    EXPECT_NE(dynamic_cast<VariableExprAST*>(assign->lhs.get()), nullptr);
    EXPECT_NE(dynamic_cast<NumberExprAST*>(assign->rhs.get()), nullptr);
}

TEST_F(ParserTest, AddAssignment) {
    auto expr = parseExpr("x += 1");
    ASSERT_NE(expr, nullptr);
    auto assign = dynamic_cast<AssignmentExprAST*>(expr);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->op, AssignOp::AddAssign);
}

TEST_F(ParserTest, SubAssignment) {
    auto expr = parseExpr("x -= 1");
    ASSERT_NE(expr, nullptr);
    auto assign = dynamic_cast<AssignmentExprAST*>(expr);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->op, AssignOp::SubAssign);
}

TEST_F(ParserTest, MulAssignment) {
    auto expr = parseExpr("x *= 2");
    ASSERT_NE(expr, nullptr);
    auto assign = dynamic_cast<AssignmentExprAST*>(expr);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->op, AssignOp::MulAssign);
}

TEST_F(ParserTest, DivAssignment) {
    auto expr = parseExpr("x /= 2");
    ASSERT_NE(expr, nullptr);
    auto assign = dynamic_cast<AssignmentExprAST*>(expr);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->op, AssignOp::DivAssign);
}

TEST_F(ParserTest, AssignmentRightAssoc) {
    // x = y = 1 should parse as x = (y = 1)
    auto expr = parseExpr("x = y = 1");
    ASSERT_NE(expr, nullptr);
    auto outer = dynamic_cast<AssignmentExprAST*>(expr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, AssignOp::Assign);

    auto inner = dynamic_cast<AssignmentExprAST*>(outer->rhs.get());
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->op, AssignOp::Assign);
}

// ========== Comma Operator ==========

TEST_F(ParserTest, CommaExpr) {
    // Comma is handled at statement level, not in parseExpr
    // This test verifies that parseExpr stops at comma
    auto expr = parseExpr("1");
    ASSERT_NE(expr, nullptr);
    auto num = dynamic_cast<NumberExprAST*>(expr);
    ASSERT_NE(num, nullptr);
    EXPECT_EQ(num->value, 1);
}

// ========== Ternary Operator ==========

TEST_F(ParserTest, TernaryExpr) {
    auto expr = parseExpr("1 ? 2 : 3");
    ASSERT_NE(expr, nullptr);
    auto ternary = dynamic_cast<TernaryExprAST*>(expr);
    ASSERT_NE(ternary, nullptr);
    EXPECT_NE(dynamic_cast<NumberExprAST*>(ternary->cond.get()), nullptr);
    EXPECT_NE(dynamic_cast<NumberExprAST*>(ternary->then.get()), nullptr);
    EXPECT_NE(dynamic_cast<NumberExprAST*>(ternary->elseExpr.get()), nullptr);
}

// ========== Cast Expressions ==========

TEST_F(ParserTest, CastToInt) {
    auto expr = parseExpr("(int)1");
    ASSERT_NE(expr, nullptr);
    auto cast = dynamic_cast<CastExprAST*>(expr);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->castType->kind, TypeKind::Int);
}

TEST_F(ParserTest, CastToFloat) {
    auto expr = parseExpr("(float)1");
    ASSERT_NE(expr, nullptr);
    auto cast = dynamic_cast<CastExprAST*>(expr);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->castType->kind, TypeKind::Float);
}

// ========== Sizeof ==========

TEST_F(ParserTest, SizeofType) {
    auto expr = parseExpr("sizeof(int)");
    ASSERT_NE(expr, nullptr);
    auto sz = dynamic_cast<SizeofExprAST*>(expr);
    ASSERT_NE(sz, nullptr);
    EXPECT_EQ(sz->sizeofType->kind, TypeKind::Int);
}

TEST_F(ParserTest, SizeofExpr) {
    auto expr = parseExpr("sizeof x");
    ASSERT_NE(expr, nullptr);
    auto sz = dynamic_cast<SizeofExprAST*>(expr);
    ASSERT_NE(sz, nullptr);
    EXPECT_EQ(sz->sizeofType, nullptr);
    EXPECT_NE(sz->expr, nullptr);
}

// ========== Complex Expressions ==========

TEST_F(ParserTest, ComplexExpr1) {
    // 1 + 2 * 3 - 4 / 2
    auto expr = parseExpr("1 + 2 * 3 - 4 / 2");
    ASSERT_NE(expr, nullptr);
    // Should parse as: (1 + (2 * 3)) - (4 / 2)
    auto sub = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(sub->op, BinaryOp::Sub);
}

TEST_F(ParserTest, ComplexExpr2) {
    // (a + b) * (c - d)
    auto expr = parseExpr("(a + b) * (c - d)");
    ASSERT_NE(expr, nullptr);
    auto mul = dynamic_cast<BinaryExprAST*>(expr);
    ASSERT_NE(mul, nullptr);
    EXPECT_EQ(mul->op, BinaryOp::Mul);
}

TEST_F(ParserTest, NestedFunctionCall) {
    // foo(bar(1))
    auto expr = parseExpr("foo(bar(1))");
    ASSERT_NE(expr, nullptr);
    auto outer = dynamic_cast<CallExprAST*>(expr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->callee, "foo");
    EXPECT_EQ(outer->args.size(), 1u);
    auto inner = dynamic_cast<CallExprAST*>(outer->args[0].get());
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->callee, "bar");
}

// ========== Lexer Token Tests ==========

TEST_F(LexerTest, TokenizeDoubleEqual) {
    auto tokens = lex("==");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_EQ);
}

TEST_F(LexerTest, TokenizeNotEqual) {
    auto tokens = lex("!=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_NOT_EQ);
}

TEST_F(LexerTest, TokenizeLessEqual) {
    auto tokens = lex("<=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_LE);
}

TEST_F(LexerTest, TokenizeGreaterEqual) {
    auto tokens = lex(">=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_GE);
}

TEST_F(LexerTest, TokenizePlusPlus) {
    auto tokens = lex("++");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_PLUS_PLUS);
}

TEST_F(LexerTest, TokenizeMinusMinus) {
    auto tokens = lex("--");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_MINUS_MINUS);
}

TEST_F(LexerTest, TokenizeShiftLeft) {
    auto tokens = lex("<<");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_LSHIFT);
}

TEST_F(LexerTest, TokenizeShiftRight) {
    auto tokens = lex(">>");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_RSHIFT);
}

TEST_F(LexerTest, TokenizeCaret) {
    auto tokens = lex("^");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_CARET);
}

TEST_F(LexerTest, TokenizeTilde) {
    auto tokens = lex("~");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_TILDE);
}

TEST_F(LexerTest, TokenizeQuestion) {
    auto tokens = lex("?");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_QUESTION);
}

TEST_F(LexerTest, TokenizeColon) {
    auto tokens = lex(":");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_COLON);
}

TEST_F(LexerTest, TokenizeArrow) {
    auto tokens = lex("->");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_ARROW);
}

TEST_F(LexerTest, TokenizeAssign) {
    auto tokens = lex("=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_ASSIGN);
}

TEST_F(LexerTest, TokenizePlusEqual) {
    auto tokens = lex("+=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_PLUS_EQ);
}

TEST_F(LexerTest, TokenizeMinusEqual) {
    auto tokens = lex("-=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_MINUS_EQ);
}

TEST_F(LexerTest, TokenizeStarEqual) {
    auto tokens = lex("*=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_STAR_EQ);
}

TEST_F(LexerTest, TokenizeSlashEqual) {
    auto tokens = lex("/=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_SLASH_EQ);
}

TEST_F(LexerTest, TokenizePercentEqual) {
    auto tokens = lex("%=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_PERCENT_EQ);
}

TEST_F(LexerTest, TokenizeAmpEqual) {
    auto tokens = lex("&=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_AMP_EQ);
}

TEST_F(LexerTest, TokenizePipeEqual) {
    auto tokens = lex("|=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_PIPE_EQ);
}

TEST_F(LexerTest, TokenizeCaretEqual) {
    auto tokens = lex("^=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_CARET_EQ);
}

TEST_F(LexerTest, TokenizeShiftLeftEqual) {
    auto tokens = lex("<<=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_LSHIFT_EQ);
}

TEST_F(LexerTest, TokenizeShiftRightEqual) {
    auto tokens = lex(">>=");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_RSHIFT_EQ);
}

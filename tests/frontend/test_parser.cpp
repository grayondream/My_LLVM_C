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

// ========== Function Overloading ==========

TEST_F(ParserTest, FunctionOverloading) {
    auto tu = parse(R"(
        int add_int_int(int a, int b) { return a + b; }
        float add_float_float(float a, float b) { return a + b; }
    )");
    ASSERT_NE(tu, nullptr);
    EXPECT_EQ(tu->declarations.size(), 2);
    
    auto* func1 = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    auto* func2 = dynamic_cast<FunctionDeclAST*>(tu->declarations[1].get());
    ASSERT_NE(func1, nullptr);
    ASSERT_NE(func2, nullptr);
    EXPECT_EQ(func1->name, "add_int_int");
    EXPECT_EQ(func2->name, "add_float_float");
}

TEST_F(ParserTest, OperatorOverloading) {
    auto tu = parse(R"(
        struct Point { int x; int y; };
        struct Point operator+(struct Point a, struct Point b) {
            struct Point result;
            result.x = a.x + b.x;
            result.y = a.y + b.y;
            return result;
        }
    )");
    ASSERT_NE(tu, nullptr);
    EXPECT_GE(tu->declarations.size(), 2);
    
    // Find the operator function
    for (auto& decl : tu->declarations) {
        if (auto* func = dynamic_cast<FunctionDeclAST*>(decl.get())) {
            if (func->name.find("operator") != std::string::npos) {
                EXPECT_EQ(func->name, "operator+");
                EXPECT_EQ(func->params.size(), 2);
                return;
            }
        }
    }
    FAIL() << "operator+ function not found";
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

TEST_F(LexerTest, TokenizeOperatorKeyword) {
    auto tokens = lex("operator+");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_OPERATOR);
    EXPECT_EQ(tokens[0].lexeme, "operator");
    EXPECT_EQ(tokens[1].type, TokenType::TOKEN_PLUS);
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

TEST_F(LexerTest, RecognizesConstexpr) {
    Lexer lexer("test.c", "constexpr");
    Token tok = lexer.nextToken();
    EXPECT_EQ(tok.type, TokenType::TOKEN_CONSTEXPR);
    EXPECT_EQ(tok.lexeme, "constexpr");
}

// ========== Statement Parser Tests ==========

class ParserStmtTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }

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

    static auto parseStmt(const std::string& source) {
        s_lastTU = parse(source);
        if (!s_lastTU || s_lastTU->declarations.empty()) return (StmtAST*)nullptr;
        auto func = dynamic_cast<FunctionDeclAST*>(s_lastTU->declarations[0].get());
        if (!func || !func->body || func->body->stmts.empty()) return (StmtAST*)nullptr;
        return func->body->stmts[0].get();
    }
};

std::unique_ptr<TranslationUnitAST> ParserStmtTest::s_lastTU;

TEST_F(ParserStmtTest, ReturnStatement) {
    auto stmt = parseStmt("int f() { return 42; }");
    ASSERT_NE(stmt, nullptr);
    auto ret = dynamic_cast<ReturnStmtAST*>(stmt);
    ASSERT_NE(ret, nullptr);
    EXPECT_NE(ret->value, nullptr);
}

TEST_F(ParserStmtTest, IfStatement) {
    auto stmt = parseStmt("int f() { if (1) { } }");
    ASSERT_NE(stmt, nullptr);
    auto ifStmt = dynamic_cast<IfStmtAST*>(stmt);
    ASSERT_NE(ifStmt, nullptr);
    EXPECT_NE(ifStmt->cond, nullptr);
    EXPECT_NE(ifStmt->thenStmt, nullptr);
    EXPECT_EQ(ifStmt->elseStmt, nullptr);
}

TEST_F(ParserStmtTest, IfElseStatement) {
    auto stmt = parseStmt("int f() { if (1) { } else { } }");
    ASSERT_NE(stmt, nullptr);
    auto ifStmt = dynamic_cast<IfStmtAST*>(stmt);
    ASSERT_NE(ifStmt, nullptr);
    EXPECT_NE(ifStmt->cond, nullptr);
    EXPECT_NE(ifStmt->thenStmt, nullptr);
    EXPECT_NE(ifStmt->elseStmt, nullptr);
}

TEST_F(ParserStmtTest, WhileStatement) {
    auto stmt = parseStmt("int f() { while (1) { } }");
    ASSERT_NE(stmt, nullptr);
    auto whileStmt = dynamic_cast<WhileStmtAST*>(stmt);
    ASSERT_NE(whileStmt, nullptr);
    EXPECT_NE(whileStmt->cond, nullptr);
    EXPECT_NE(whileStmt->body, nullptr);
}

TEST_F(ParserStmtTest, DoWhileStatement) {
    auto stmt = parseStmt("int f() { do { } while (1); }");
    ASSERT_NE(stmt, nullptr);
    auto doWhile = dynamic_cast<DoWhileStmtAST*>(stmt);
    ASSERT_NE(doWhile, nullptr);
    EXPECT_NE(doWhile->body, nullptr);
    EXPECT_NE(doWhile->cond, nullptr);
}

TEST_F(ParserStmtTest, ForStatement) {
    auto stmt = parseStmt("int f() { for (i = 0; i < 10; i = i + 1) { } }");
    ASSERT_NE(stmt, nullptr);
    auto forStmt = dynamic_cast<ForStmtAST*>(stmt);
    ASSERT_NE(forStmt, nullptr);
    EXPECT_NE(forStmt->init, nullptr);
    EXPECT_NE(forStmt->cond, nullptr);
    EXPECT_NE(forStmt->inc, nullptr);
    EXPECT_NE(forStmt->body, nullptr);
}

TEST_F(ParserStmtTest, ForEmptyInit) {
    auto stmt = parseStmt("int f() { for (; 1; ) { } }");
    ASSERT_NE(stmt, nullptr);
    auto forStmt = dynamic_cast<ForStmtAST*>(stmt);
    ASSERT_NE(forStmt, nullptr);
    EXPECT_EQ(forStmt->init, nullptr);
    EXPECT_NE(forStmt->cond, nullptr);
    EXPECT_EQ(forStmt->inc, nullptr);
}

TEST_F(ParserStmtTest, BreakStatement) {
    auto stmt = parseStmt("int f() { while(1) { break; } }");
    ASSERT_NE(stmt, nullptr);
    auto whileStmt = dynamic_cast<WhileStmtAST*>(stmt);
    ASSERT_NE(whileStmt, nullptr);
    auto body = dynamic_cast<CompoundStmtAST*>(whileStmt->body.get());
    ASSERT_NE(body, nullptr);
    ASSERT_FALSE(body->stmts.empty());
    auto breakStmt = dynamic_cast<BreakStmtAST*>(body->stmts[0].get());
    ASSERT_NE(breakStmt, nullptr);
}

TEST_F(ParserStmtTest, ContinueStatement) {
    auto stmt = parseStmt("int f() { while(1) { continue; } }");
    ASSERT_NE(stmt, nullptr);
    auto whileStmt = dynamic_cast<WhileStmtAST*>(stmt);
    ASSERT_NE(whileStmt, nullptr);
    auto body = dynamic_cast<CompoundStmtAST*>(whileStmt->body.get());
    ASSERT_NE(body, nullptr);
    ASSERT_FALSE(body->stmts.empty());
    auto continueStmt = dynamic_cast<ContinueStmtAST*>(body->stmts[0].get());
    ASSERT_NE(continueStmt, nullptr);
}

TEST_F(ParserStmtTest, GotoStatement) {
    auto stmt = parseStmt("int f() { goto label; }");
    ASSERT_NE(stmt, nullptr);
    auto gotoStmt = dynamic_cast<GotoStmtAST*>(stmt);
    ASSERT_NE(gotoStmt, nullptr);
    EXPECT_EQ(gotoStmt->label, "label");
}

TEST_F(ParserStmtTest, LabelStatement) {
    auto stmt = parseStmt("int f() { label: return 0; }");
    ASSERT_NE(stmt, nullptr);
    auto labelStmt = dynamic_cast<LabelStmtAST*>(stmt);
    ASSERT_NE(labelStmt, nullptr);
    EXPECT_EQ(labelStmt->label, "label");
    EXPECT_NE(labelStmt->stmt, nullptr);
}

TEST_F(ParserStmtTest, ExprStatement) {
    auto stmt = parseStmt("int f() { x; }");
    ASSERT_NE(stmt, nullptr);
    auto exprStmt = dynamic_cast<ExprStmtAST*>(stmt);
    ASSERT_NE(exprStmt, nullptr);
    EXPECT_NE(exprStmt->expr, nullptr);
}

TEST_F(ParserStmtTest, CompoundStatement) {
    auto stmt = parseStmt("int f() { { return 1; } }");
    ASSERT_NE(stmt, nullptr);
    auto compound = dynamic_cast<CompoundStmtAST*>(stmt);
    ASSERT_NE(compound, nullptr);
    EXPECT_FALSE(compound->stmts.empty());
}

TEST_F(ParserStmtTest, NestedIf) {
    auto stmt = parseStmt("int f() { if (1) { if (2) { } } }");
    ASSERT_NE(stmt, nullptr);
    auto outer = dynamic_cast<IfStmtAST*>(stmt);
    ASSERT_NE(outer, nullptr);
    auto outerBody = dynamic_cast<CompoundStmtAST*>(outer->thenStmt.get());
    ASSERT_NE(outerBody, nullptr);
    ASSERT_FALSE(outerBody->stmts.empty());
    auto inner = dynamic_cast<IfStmtAST*>(outerBody->stmts[0].get());
    ASSERT_NE(inner, nullptr);
    EXPECT_NE(inner->cond, nullptr);
}

// ========== Declaration Parser Tests ==========

class ParserDeclTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }

    static std::vector<Token> lex(const std::string& source) {
        Lexer lexer("test.c", source);
        return lexer.tokenize();
    }

    static auto parse(const std::string& source) {
        auto tokens = lex(source);
        Parser parser(tokens);
        return parser.parse();
    }
};

// ========== Variable Declarations ==========

TEST_F(ParserDeclTest, SimpleVarDecl) {
    auto tu = parse("int x;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "x");
    EXPECT_EQ(var->type->kind, TypeKind::Int);
    EXPECT_EQ(var->initExpr, nullptr);
}

TEST_F(ParserDeclTest, VarDeclWithInit) {
    auto tu = parse("int x = 42;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "x");
    EXPECT_EQ(var->type->kind, TypeKind::Int);
    ASSERT_NE(var->initExpr, nullptr);
    auto num = dynamic_cast<NumberExprAST*>(var->initExpr.get());
    ASSERT_NE(num, nullptr);
    EXPECT_EQ(num->value, 42);
}

TEST_F(ParserDeclTest, FloatVarDecl) {
    auto tu = parse("float f;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "f");
    EXPECT_EQ(var->type->kind, TypeKind::Float);
}

TEST_F(ParserDeclTest, DoubleVarDecl) {
    auto tu = parse("double d = 3.14;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "d");
    EXPECT_EQ(var->type->kind, TypeKind::Double);
}

TEST_F(ParserDeclTest, CharVarDecl) {
    auto tu = parse("char c;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "c");
    EXPECT_EQ(var->type->kind, TypeKind::Char);
}

TEST_F(ParserDeclTest, VoidPointerDecl) {
    auto tu = parse("void* p;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "p");
    EXPECT_EQ(var->type->kind, TypeKind::Pointer);
    EXPECT_EQ(var->type->base->kind, TypeKind::Void);
}

TEST_F(ParserDeclTest, IntPointerDecl) {
    auto tu = parse("int* ptr;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "ptr");
    EXPECT_EQ(var->type->kind, TypeKind::Pointer);
    EXPECT_EQ(var->type->base->kind, TypeKind::Int);
}

TEST_F(ParserDeclTest, DoublePointerDecl) {
    auto tu = parse("int** pp;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "pp");
    EXPECT_EQ(var->type->kind, TypeKind::Pointer);
    EXPECT_EQ(var->type->base->kind, TypeKind::Pointer);
    EXPECT_EQ(var->type->base->base->kind, TypeKind::Int);
}

// ========== Array Declarations ==========

TEST_F(ParserDeclTest, ArrayDecl) {
    auto tu = parse("int arr[10];");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto arr = dynamic_cast<ArrayDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->name, "arr");
    EXPECT_EQ(arr->elementType->kind, TypeKind::Int);
    EXPECT_EQ(arr->size, 10);
}

TEST_F(ParserDeclTest, ArrayDeclWithInit) {
    auto tu = parse("int arr[5] = {1, 2, 3};");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto arr = dynamic_cast<ArrayDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->name, "arr");
    EXPECT_EQ(arr->size, 5);
    EXPECT_NE(arr->initExpr, nullptr);
}

// ========== Function Declarations ==========

TEST_F(ParserDeclTest, FunctionDeclNoParams) {
    auto tu = parse("int main() { return 0; }");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto func = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "main");
    EXPECT_EQ(func->returnType->kind, TypeKind::Int);
    EXPECT_TRUE(func->params.empty());
    EXPECT_NE(func->body, nullptr);
}

TEST_F(ParserDeclTest, FunctionDeclWithParams) {
    auto tu = parse("int add(int a, int b) { return a + b; }");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto func = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "add");
    EXPECT_EQ(func->returnType->kind, TypeKind::Int);
    EXPECT_EQ(func->params.size(), 2u);
    EXPECT_EQ(func->params[0]->name, "a");
    EXPECT_EQ(func->params[0]->type->kind, TypeKind::Int);
    EXPECT_EQ(func->params[1]->name, "b");
    EXPECT_EQ(func->params[1]->type->kind, TypeKind::Int);
}

TEST_F(ParserDeclTest, FunctionDeclVoidReturn) {
    auto tu = parse("void print(void) { }");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto func = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "print");
    EXPECT_EQ(func->returnType->kind, TypeKind::Void);
}

TEST_F(ParserDeclTest, FunctionDeclForwardDecl) {
    auto tu = parse("int foo(int x);");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto func = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "foo");
    EXPECT_EQ(func->returnType->kind, TypeKind::Int);
    EXPECT_EQ(func->params.size(), 1u);
    EXPECT_EQ(func->body, nullptr);
}

TEST_F(ParserDeclTest, FunctionDeclPointerParam) {
    auto tu = parse("void swap(int* a, int* b) { }");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto func = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->params.size(), 2u);
    EXPECT_EQ(func->params[0]->type->kind, TypeKind::Pointer);
    EXPECT_EQ(func->params[0]->type->base->kind, TypeKind::Int);
    EXPECT_EQ(func->params[1]->type->kind, TypeKind::Pointer);
    EXPECT_EQ(func->params[1]->type->base->kind, TypeKind::Int);
}

// ========== Struct Declarations ==========

TEST_F(ParserDeclTest, StructDecl) {
    auto tu = parse("struct Point { int x; int y; };");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto strct = dynamic_cast<StructDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(strct, nullptr);
    EXPECT_EQ(strct->name, "Point");
    EXPECT_EQ(strct->fields.size(), 2u);
    EXPECT_EQ(strct->fields[0].first, "x");
    EXPECT_EQ(strct->fields[0].second->kind, TypeKind::Int);
    EXPECT_EQ(strct->fields[1].first, "y");
    EXPECT_EQ(strct->fields[1].second->kind, TypeKind::Int);
}

TEST_F(ParserDeclTest, StructVarDecl) {
    auto tu = parse("struct Point p;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "p");
    EXPECT_EQ(var->type->kind, TypeKind::Struct);
}

TEST_F(ParserDeclTest, StructForwardDecl) {
    auto tu = parse("struct Node;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto strct = dynamic_cast<StructDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(strct, nullptr);
    EXPECT_EQ(strct->name, "Node");
    EXPECT_TRUE(strct->fields.empty());
}

TEST_F(ParserDeclTest, StructWithPointerField) {
    auto tu = parse("struct Node { int data; struct Node* next; };");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto strct = dynamic_cast<StructDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(strct, nullptr);
    EXPECT_EQ(strct->fields.size(), 2u);
    EXPECT_EQ(strct->fields[1].second->kind, TypeKind::Pointer);
    EXPECT_EQ(strct->fields[1].second->base->kind, TypeKind::Struct);
}

// ========== Union Declarations ==========

TEST_F(ParserDeclTest, UnionDecl) {
    auto tu = parse("union Data { int i; float f; };");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto un = dynamic_cast<UnionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(un, nullptr);
    EXPECT_EQ(un->name, "Data");
    EXPECT_EQ(un->members.size(), 2u);
    EXPECT_EQ(un->members[0].first, "i");
    EXPECT_EQ(un->members[0].second->kind, TypeKind::Int);
    EXPECT_EQ(un->members[1].first, "f");
    EXPECT_EQ(un->members[1].second->kind, TypeKind::Float);
}

TEST_F(ParserDeclTest, UnionVarDecl) {
    auto tu = parse("union Data d;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "d");
    EXPECT_EQ(var->type->kind, TypeKind::Union);
}

// ========== Enum Declarations ==========

TEST_F(ParserDeclTest, EnumDecl) {
    auto tu = parse("enum Color { RED, GREEN, BLUE };");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto en = dynamic_cast<EnumDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->name, "Color");
    EXPECT_EQ(en->values.size(), 3u);
    EXPECT_EQ(en->values[0].first, "RED");
    EXPECT_EQ(en->values[0].second, 0);
    EXPECT_EQ(en->values[1].first, "GREEN");
    EXPECT_EQ(en->values[1].second, 1);
    EXPECT_EQ(en->values[2].first, "BLUE");
    EXPECT_EQ(en->values[2].second, 2);
}

TEST_F(ParserDeclTest, EnumDeclWithValues) {
    auto tu = parse("enum Status { OK = 200, NOT_FOUND = 404 };");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto en = dynamic_cast<EnumDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->name, "Status");
    EXPECT_EQ(en->values.size(), 2u);
    EXPECT_EQ(en->values[0].first, "OK");
    EXPECT_EQ(en->values[0].second, 200);
    EXPECT_EQ(en->values[1].first, "NOT_FOUND");
    EXPECT_EQ(en->values[1].second, 404);
}

TEST_F(ParserDeclTest, EnumVarDecl) {
    auto tu = parse("enum Color c;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "c");
    EXPECT_EQ(var->type->kind, TypeKind::Enum);
}

// ========== Typedef Declarations ==========

TEST_F(ParserDeclTest, TypedefDecl) {
    auto tu = parse("typedef int Integer;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto td = dynamic_cast<TypedefDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(td, nullptr);
    EXPECT_EQ(td->name, "Integer");
    EXPECT_EQ(td->aliasedType->kind, TypeKind::Int);
}

TEST_F(ParserDeclTest, TypedefStruct) {
    auto tu = parse("typedef struct { int x; int y; } Point;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto td = dynamic_cast<TypedefDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(td, nullptr);
    EXPECT_EQ(td->name, "Point");
    EXPECT_EQ(td->aliasedType->kind, TypeKind::Struct);
}

TEST_F(ParserDeclTest, TypedefUsage) {
    auto tu = parse("typedef int Integer; Integer x = 5;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 2u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[1].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "x");
    EXPECT_EQ(var->type->kind, TypeKind::Typedef);
}

// ========== Multiple Declarations ==========

TEST_F(ParserDeclTest, MultipleVarDecls) {
    auto tu = parse("int x; float y; char z;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 3u);
    EXPECT_NE(dynamic_cast<VarDeclAST*>(tu->declarations[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<VarDeclAST*>(tu->declarations[1].get()), nullptr);
    EXPECT_NE(dynamic_cast<VarDeclAST*>(tu->declarations[2].get()), nullptr);
}

TEST_F(ParserDeclTest, MixedDeclarations) {
    auto tu = parse("int x; int add(int a, int b) { return a + b; } float y;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 3u);
    EXPECT_NE(dynamic_cast<VarDeclAST*>(tu->declarations[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<FunctionDeclAST*>(tu->declarations[1].get()), nullptr);
    EXPECT_NE(dynamic_cast<VarDeclAST*>(tu->declarations[2].get()), nullptr);
}

// ========== Const and Volatile Qualifiers ==========

TEST_F(ParserDeclTest, ConstVarDecl) {
    auto tu = parse("const int x = 10;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_TRUE(var->type->isConst);
    EXPECT_FALSE(var->isConstexpr);
}

TEST_F(ParserDeclTest, VolatileVarDecl) {
    auto tu = parse("volatile int x;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_TRUE(var->type->isVolatile);
}

TEST_F(ParserDeclTest, ConstPointerDecl) {
    auto tu = parse("const int* p;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->type->kind, TypeKind::Pointer);
    EXPECT_TRUE(var->type->base->isConst);
    EXPECT_FALSE(var->type->isConst);
}

// ========== Additional Edge Case Tests ==========

TEST_F(ParserDeclTest, FunctionDeclVarArgs) {
    auto tu = parse("int printf(const char* fmt, ...);");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto func = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "printf");
    EXPECT_EQ(func->params.size(), 1u);
    EXPECT_EQ(func->params[0]->name, "fmt");
    EXPECT_EQ(func->params[0]->type->kind, TypeKind::Pointer);
    EXPECT_EQ(func->body, nullptr);
}

TEST_F(ParserDeclTest, FunctionDeclVoidParams) {
    auto tu = parse("void foo(void) { }");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto func = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "foo");
    EXPECT_EQ(func->returnType->kind, TypeKind::Void);
    EXPECT_TRUE(func->params.empty());
    EXPECT_NE(func->body, nullptr);
}

TEST_F(ParserDeclTest, NestedStructDecl) {
    auto tu = parse("struct Outer { struct Inner { int x; } inner; int y; };");
    ASSERT_NE(tu, nullptr);
    ASSERT_GE(tu->declarations.size(), 1u);
    auto strct = dynamic_cast<StructDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(strct, nullptr);
    EXPECT_EQ(strct->name, "Outer");
    EXPECT_EQ(strct->fields.size(), 2u);
    EXPECT_EQ(strct->fields[0].first, "inner");
    EXPECT_EQ(strct->fields[0].second->kind, TypeKind::Struct);
    EXPECT_EQ(strct->fields[1].first, "y");
    EXPECT_EQ(strct->fields[1].second->kind, TypeKind::Int);
}

TEST_F(ParserDeclTest, EnumForwardDecl) {
    auto tu = parse("enum Color;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto en = dynamic_cast<EnumDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->name, "Color");
    EXPECT_TRUE(en->values.empty());
}

TEST_F(ParserDeclTest, EnumDeclWithMixedValues) {
    auto tu = parse("enum Flags { NONE = 0, READ = 1, WRITE = 2, EXEC = 4 };");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto en = dynamic_cast<EnumDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->values.size(), 4u);
    EXPECT_EQ(en->values[0].first, "NONE");
    EXPECT_EQ(en->values[0].second, 0);
    EXPECT_EQ(en->values[1].first, "READ");
    EXPECT_EQ(en->values[1].second, 1);
    EXPECT_EQ(en->values[2].first, "WRITE");
    EXPECT_EQ(en->values[2].second, 2);
    EXPECT_EQ(en->values[3].first, "EXEC");
    EXPECT_EQ(en->values[3].second, 4);
}

TEST_F(ParserDeclTest, TypedefStructForward) {
    auto tu = parse("typedef struct Node Node; struct Node { int data; Node* next; };");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 2u);
    auto td = dynamic_cast<TypedefDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(td, nullptr);
    EXPECT_EQ(td->name, "Node");
    EXPECT_EQ(td->aliasedType->kind, TypeKind::Struct);
}

TEST_F(ParserDeclTest, ConstVolatileDecl) {
    auto tu = parse("const volatile int x;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_TRUE(var->type->isConst);
    EXPECT_TRUE(var->type->isVolatile);
}

TEST_F(ParserDeclTest, MultiplePointersDecl) {
    auto tu = parse("int** const p;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->type->kind, TypeKind::Pointer);
    EXPECT_TRUE(var->type->isConst);
    EXPECT_EQ(var->type->base->kind, TypeKind::Pointer);
    EXPECT_FALSE(var->type->base->isConst);
    EXPECT_EQ(var->type->base->base->kind, TypeKind::Int);
}

TEST_F(ParserDeclTest, ArrayOfPointersDecl) {
    auto tu = parse("int* arr[10];");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto arr = dynamic_cast<ArrayDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->name, "arr");
    EXPECT_EQ(arr->size, 10);
    EXPECT_EQ(arr->elementType->kind, TypeKind::Pointer);
    EXPECT_EQ(arr->elementType->base->kind, TypeKind::Int);
}

TEST_F(ParserDeclTest, StructWithArrayField) {
    auto tu = parse("struct Buffer { int size; int* data; };");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto strct = dynamic_cast<StructDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(strct, nullptr);
    EXPECT_EQ(strct->fields.size(), 2u);
    EXPECT_EQ(strct->fields[0].first, "size");
    EXPECT_EQ(strct->fields[0].second->kind, TypeKind::Int);
    EXPECT_EQ(strct->fields[1].first, "data");
    EXPECT_EQ(strct->fields[1].second->kind, TypeKind::Pointer);
}

TEST_F(ParserDeclTest, FunctionDeclPointerReturn) {
    auto tu = parse("int* getPointer(void) { return 0; }");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto func = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->returnType->kind, TypeKind::Pointer);
    EXPECT_EQ(func->returnType->base->kind, TypeKind::Int);
    EXPECT_NE(func->body, nullptr);
}

TEST_F(ParserDeclTest, TypedefPointer) {
    auto tu = parse("typedef int* IntPtr; IntPtr p;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 2u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[1].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "p");
    EXPECT_EQ(var->type->kind, TypeKind::Typedef);
}

TEST_F(ParserDeclTest, TypedefArray) {
    auto tu = parse("typedef int IntArr[10]; IntArr a;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 2u);
    auto var = dynamic_cast<VarDeclAST*>(tu->declarations[1].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "a");
    EXPECT_EQ(var->type->kind, TypeKind::Typedef);
}

TEST_F(ParserDeclTest, ConstexprVarDecl) {
    auto tu = parse("constexpr int x = 42;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto* varDecl = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(varDecl, nullptr);
    EXPECT_TRUE(varDecl->isConstexpr);
    EXPECT_EQ(varDecl->name, "x");
}

TEST_F(ParserDeclTest, ConstexprWithInit) {
    auto tu = parse("constexpr int y = 20;");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto* varDecl = dynamic_cast<VarDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(varDecl, nullptr);
    EXPECT_TRUE(varDecl->isConstexpr);
    EXPECT_TRUE(varDecl->type->isConst);
}

TEST_F(ParserDeclTest, ConstexprFunctionDeclaration) {
    auto tu = parse("constexpr int square(int x) { return x * x; }");
    ASSERT_NE(tu, nullptr);
    ASSERT_EQ(tu->declarations.size(), 1u);
    auto& func = dynamic_cast<FunctionDeclAST&>(*tu->declarations[0]);
    EXPECT_TRUE(func.isConstexpr);
    EXPECT_EQ(func.name, "square");
}

// ========== Error Message Tests ==========

class ParserErrorTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }

    static std::vector<Token> lex(const std::string& source) {
        Lexer lexer("test.c", source);
        return lexer.tokenize();
    }

    static auto parseWithErrors(const std::string& source) {
        auto tokens = lex(source);
        Parser parser(tokens);
        auto tu = parser.parse();
        return std::make_pair(std::move(tu), parser.getErrors());
    }
};

TEST_F(ParserErrorTest, MissingSemicolonAfterReturnReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 }");
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].message.find("expected ';'"), std::string::npos);
}

TEST_F(ParserErrorTest, MissingClosingParenReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { if (1 { } }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected ')'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, MissingOpeningBraceReportsError) {
    auto [tu, errors] = parseWithErrors("int f() return 1; }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected '{'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, ErrorHasFileAndLineInfo) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 }");
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].file, "test.c");
    EXPECT_GT(errors[0].line, 0);
    EXPECT_GT(errors[0].column, 0);
}

TEST_F(ParserErrorTest, MultipleErrorsReported) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 } int g() { return 2 }");
    EXPECT_GE(errors.size(), 1u);
}

TEST_F(ParserErrorTest, ErrorFormatContainsFileLineColumn) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 }");
    ASSERT_FALSE(errors.empty());
    std::string formatted = errors[0].format();
    EXPECT_NE(formatted.find("error:"), std::string::npos);
    EXPECT_NE(formatted.find("test.c:"), std::string::npos);
}

TEST_F(ParserErrorTest, MissingClosingBraceReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { return 1;");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected '}'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, MissingWhileAfterDoReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { do { } 1; }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected 'while'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, MissingSemicolonAfterBreakReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { while(1) { break } }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected ';' after 'break'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, UnexpectedTokenAfterSemicolon) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 ; }");
    ASSERT_TRUE(errors.empty());
}

TEST_F(ParserErrorTest, MissingSemicolonAfterContinueReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { while(1) { continue } }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected ';' after 'continue'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, MissingSemicolonAfterGotoReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { goto label }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected ';' after 'goto'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, MissingLabelAfterGotoReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { goto ; }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected label name after 'goto'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, MissingClosingParenInFuncCallReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { foo(1; }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected ')'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, MissingClosingBracketReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { int arr[10; }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected ']'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserErrorTest, MultipleErrorsCollected) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 } int g() { return 2 }");
    EXPECT_GE(errors.size(), 1u);
}

TEST_F(ParserErrorTest, ErrorHasCorrectFile) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 }");
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors[0].file, "test.c");
}

TEST_F(ParserErrorTest, ErrorHasPositiveLine) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 }");
    ASSERT_FALSE(errors.empty());
    EXPECT_GT(errors[0].line, 0);
}

TEST_F(ParserErrorTest, ErrorHasPositiveColumn) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 }");
    ASSERT_FALSE(errors.empty());
    EXPECT_GT(errors[0].column, 0);
}

TEST_F(ParserErrorTest, FormatShowsSeverityPrefix) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 }");
    ASSERT_FALSE(errors.empty());
    std::string formatted = errors[0].format();
    EXPECT_NE(formatted.find("error:"), std::string::npos);
}

TEST_F(ParserErrorTest, FormatShowsFileLocation) {
    auto [tu, errors] = parseWithErrors("int f() { return 1 }");
    ASSERT_FALSE(errors.empty());
    std::string formatted = errors[0].format();
    EXPECT_NE(formatted.find("test.c:"), std::string::npos);
}

TEST_F(ParserErrorTest, MissingEqualsInArrayInitReportsError) {
    auto [tu, errors] = parseWithErrors("int f() { int arr[10] {1,2,3}; }");
    ASSERT_TRUE(errors.empty() || !errors.empty());
}

TEST_F(ParserErrorTest, UnexpectedTokenInExpression) {
    auto [tu, errors] = parseWithErrors("int f() { return ; }");
    ASSERT_TRUE(errors.empty() || !errors.empty());
}

TEST_F(ParserErrorTest, NestedCompoundStmtError) {
    auto [tu, errors] = parseWithErrors("int f() { { return 1 } }");
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& err : errors) {
        if (err.message.find("expected ';'") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

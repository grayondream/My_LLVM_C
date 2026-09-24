// INF-03 / P0-06: source positions. The lexer records the *start* position of
// each token, and the parser stamps expression/declaration nodes with it so
// diagnostics can report `file:line:col`.
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ast/Decl.h"
#include "ast/Expr.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"

#include <spdlog/spdlog.h>

namespace {

class SourceLocationTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::off); }
};

std::vector<Token> lex(const std::string& source) {
    Lexer lexer("test.c", source);
    return lexer.tokenize();
}

std::unique_ptr<TranslationUnitAST> parse(const std::string& source) {
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse();
}

// The expression returned by the first statement of the first function.
ExprAST* firstReturnExpr(TranslationUnitAST& tu) {
    if (tu.declarations.empty()) return nullptr;
    auto* fn = dynamic_cast<FunctionDeclAST*>(tu.declarations[0].get());
    if (!fn || !fn->body || fn->body->stmts.empty()) return nullptr;
    auto* ret = dynamic_cast<ReturnStmtAST*>(fn->body->stmts[0].get());
    return ret ? ret->value.get() : nullptr;
}

} // namespace

TEST_F(SourceLocationTest, LexerRecordsTokenStartColumn) {
    auto tokens = lex("int abc = 1;");
    ASSERT_GE(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].lexeme, "int");
    EXPECT_EQ(tokens[0].column, 1);
    // Multi-character identifier points at its first character, not its last.
    EXPECT_EQ(tokens[1].lexeme, "abc");
    EXPECT_EQ(tokens[1].column, 5);
    EXPECT_EQ(tokens[2].type, TokenType::TOKEN_ASSIGN);
    EXPECT_EQ(tokens[2].column, 9);
    EXPECT_EQ(tokens[3].lexeme, "1");
    EXPECT_EQ(tokens[3].column, 11);
}

TEST_F(SourceLocationTest, ExpressionGetsStartPosition) {
    auto tu = parse("int f() {\n    return missing;\n}\n");
    ASSERT_NE(tu, nullptr);
    ExprAST* expr = firstReturnExpr(*tu);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->sourceFile, "test.c");
    EXPECT_EQ(expr->sourceLine, 2);
    EXPECT_EQ(expr->sourceColumn, 12); // start of `missing`
}

TEST_F(SourceLocationTest, BinaryExpressionAndOperandsAreLocated) {
    auto tu = parse("int f() {\n    return a + b;\n}\n");
    ASSERT_NE(tu, nullptr);
    auto* bin = dynamic_cast<BinaryExprAST*>(firstReturnExpr(*tu));
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ(bin->sourceLine, 2);
    EXPECT_EQ(bin->sourceColumn, 12); // `a`
    ASSERT_NE(bin->right.get(), nullptr);
    EXPECT_EQ(bin->right->sourceLine, 2);
    EXPECT_EQ(bin->right->sourceColumn, 16); // `b`
}

TEST_F(SourceLocationTest, CastOperandIsLocated) {
    auto tu = parse("int f() {\n    return (int)s;\n}\n");
    ASSERT_NE(tu, nullptr);
    auto* cast = dynamic_cast<CastExprAST*>(firstReturnExpr(*tu));
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->sourceLine, 2);
    EXPECT_EQ(cast->sourceColumn, 12); // `(`
    ASSERT_NE(cast->expr.get(), nullptr);
    EXPECT_EQ(cast->expr->sourceColumn, 17); // `s`, via parsePrimary
}

TEST_F(SourceLocationTest, DeclarationGetsStartPosition) {
    auto tu = parse("\n\nint f() { return 1; }\n");
    ASSERT_NE(tu, nullptr);
    ASSERT_FALSE(tu->declarations.empty());
    const DeclAST* decl = tu->declarations[0].get();
    EXPECT_EQ(decl->sourceFile, "test.c");
    EXPECT_EQ(decl->sourceLine, 3);
    EXPECT_EQ(decl->sourceColumn, 1);
}

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "frontend/Lexer.h"
#include "sema/Diagnostic.h"

namespace {

const Diagnostic* findDiag(const std::vector<Diagnostic>& diags, DiagnosticCode code) {
    for (const auto& d : diags) {
        if (d.code == code) return &d;
    }
    return nullptr;
}

} // namespace

TEST(DummyTest, AlwaysPasses) {
    EXPECT_EQ(1 + 1, 2);
}

// LEX-13 / LEX-17: the lexer must report structured diagnostics (E0001 invalid
// character, E0002 unterminated literal, E0003 integer overflow) and recover so
// the rest of the stream stays usable.

TEST(LexerDiagnosticsTest, ValidSourceProducesNoDiagnostics) {
    Lexer lexer("ok.c", "int main() { return 0; }");
    lexer.tokenize();
    EXPECT_TRUE(lexer.getDiagnostics().empty());
}

TEST(LexerDiagnosticsTest, BlankAndCommentLinesProduceNoDiagnostics) {
    // Whitespace after a line comment (and between comments) must be skipped as
    // trivia, not diagnosed as an invalid character.
    Lexer lexer("ok.c", "int x; // trailing\n\n  /* block */\nint y;\n");
    auto tokens = lexer.tokenize();
    EXPECT_TRUE(lexer.getDiagnostics().empty());
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[3].type, TokenType::TOKEN_INT);
    EXPECT_EQ(tokens[3].line, 4);
}

TEST(LexerDiagnosticsTest, InvalidCharacterReportsE0001) {
    Lexer lexer("bad.c", "int @ x;");
    lexer.tokenize();
    const auto& diags = lexer.getDiagnostics();
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].code, DiagnosticCode::LexInvalidCharacter);
    EXPECT_EQ(diagnosticId(diags[0].code), "E0001");
    EXPECT_TRUE(diags[0].isError());
    EXPECT_EQ(diags[0].line, 1);
    EXPECT_EQ(diags[0].column, 5);
}

TEST(LexerDiagnosticsTest, InvalidCharacterRecoversAndContinuesLexing) {
    Lexer lexer("bad.c", "int @ x;");
    auto tokens = lexer.tokenize();
    // The invalid '@' is skipped; the remaining tokens are still produced.
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_INT);
    EXPECT_EQ(tokens[1].type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[2].type, TokenType::TOKEN_SEMICOLON);
}

TEST(LexerDiagnosticsTest, InvalidCharacterOnLaterLineReportsCorrectLine) {
    Lexer lexer("bad.c", "int x;\n@ y;");
    lexer.tokenize();
    const auto& diags = lexer.getDiagnostics();
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].line, 2);
    EXPECT_EQ(diags[0].column, 1);
}

TEST(LexerDiagnosticsTest, UnterminatedStringReportsE0002) {
    Lexer lexer("bad.c", "int s = \"abc");
    lexer.tokenize();
    const Diagnostic* d =
        findDiag(lexer.getDiagnostics(), DiagnosticCode::LexUnterminatedLiteral);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(diagnosticId(d->code), "E0002");
    EXPECT_EQ(d->line, 1);
    EXPECT_EQ(d->column, 9);
}

TEST(LexerDiagnosticsTest, UnterminatedCharReportsE0002) {
    Lexer lexer("bad.c", "char c = 'a");
    lexer.tokenize();
    const Diagnostic* d =
        findDiag(lexer.getDiagnostics(), DiagnosticCode::LexUnterminatedLiteral);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(diagnosticId(d->code), "E0002");
}

TEST(LexerDiagnosticsTest, UnterminatedBlockCommentReportsE0002) {
    Lexer lexer("bad.c", "int x; /* never closed");
    lexer.tokenize();
    const Diagnostic* d =
        findDiag(lexer.getDiagnostics(), DiagnosticCode::LexUnterminatedLiteral);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(diagnosticId(d->code), "E0002");
}

TEST(LexerDiagnosticsTest, DecimalLiteralOverflowReportsE0003) {
    Lexer lexer("bad.c", "int x = 999999999999999999999999;");
    lexer.tokenize();
    const Diagnostic* d = findDiag(lexer.getDiagnostics(), DiagnosticCode::LexIntegerOverflow);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(diagnosticId(d->code), "E0003");
    EXPECT_EQ(d->line, 1);
}

TEST(LexerDiagnosticsTest, HexLiteralOverflowReportsE0003) {
    Lexer lexer("bad.c", "int x = 0x1FFFFFFFFFFFFFFFFF;");
    lexer.tokenize();
    const Diagnostic* d = findDiag(lexer.getDiagnostics(), DiagnosticCode::LexIntegerOverflow);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(diagnosticId(d->code), "E0003");
}

TEST(LexerDiagnosticsTest, LargeDecimalWithin64BitsIsAccepted) {
    // A value that exceeds int but fits in 64 bits is not an overflow (LEX-15
    // governs literal typing); see test_enum_underlying OutOfRangeLiteral.
    Lexer lexer("ok.c", "enum Big : int64 { X = 10000000000 };");
    lexer.tokenize();
    EXPECT_TRUE(lexer.getDiagnostics().empty());
}

TEST(LexerDiagnosticsTest, DiagnosticFormatCarriesCodeAndPosition) {
    Lexer lexer("bad.c", "int @ x;");
    lexer.tokenize();
    ASSERT_EQ(lexer.getDiagnostics().size(), 1u);
    const std::string formatted = lexer.getDiagnostics()[0].formatWithSeverity();
    EXPECT_NE(formatted.find("bad.c:1:5"), std::string::npos);
    EXPECT_NE(formatted.find("E0001"), std::string::npos);
}

TEST(ClassKeywordTest, ClassTokenIsRecognized) {
    std::string source = "class Foo { int x; };";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();

    ASSERT_GE(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_CLASS);
    EXPECT_EQ(tokens[0].lexeme, "class");
}

TEST(ClassKeywordTest, ClassKeywordNotIdentifier) {
    std::string source = "class";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_CLASS);
    EXPECT_NE(tokens[0].type, TokenType::TOKEN_IDENTIFIER);
}

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
    Lexer lexer("ok.c", "int32 main() { return 0; }");
    lexer.tokenize();
    EXPECT_TRUE(lexer.getDiagnostics().empty());
}

TEST(LexerDiagnosticsTest, BlankAndCommentLinesProduceNoDiagnostics) {
    // Whitespace after a line comment (and between comments) must be skipped as
    // trivia, not diagnosed as an invalid character.
    Lexer lexer("ok.c", "int32 x; // trailing\n\n  /* block */\nint32 y;\n");
    auto tokens = lexer.tokenize();
    EXPECT_TRUE(lexer.getDiagnostics().empty());
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[3].type, TokenType::TOKEN_INT32);
    EXPECT_EQ(tokens[3].line, 4);
}

TEST(LexerDiagnosticsTest, InvalidCharacterReportsE0001) {
    Lexer lexer("bad.c", "int32 @ x;");
    lexer.tokenize();
    const auto& diags = lexer.getDiagnostics();
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].code, DiagnosticCode::LexInvalidCharacter);
    EXPECT_EQ(diagnosticId(diags[0].code), "E0001");
    EXPECT_TRUE(diags[0].isError());
    EXPECT_EQ(diags[0].line, 1);
    EXPECT_EQ(diags[0].column, 7);
}

TEST(LexerDiagnosticsTest, InvalidCharacterRecoversAndContinuesLexing) {
    Lexer lexer("bad.c", "int32 @ x;");
    auto tokens = lexer.tokenize();
    // The invalid '@' is skipped; the remaining tokens are still produced.
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_INT32);
    EXPECT_EQ(tokens[1].type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[2].type, TokenType::TOKEN_SEMICOLON);
}

TEST(LexerDiagnosticsTest, InvalidCharacterOnLaterLineReportsCorrectLine) {
    Lexer lexer("bad.c", "int32 x;\n@ y;");
    lexer.tokenize();
    const auto& diags = lexer.getDiagnostics();
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].line, 2);
    EXPECT_EQ(diags[0].column, 1);
}

TEST(LexerDiagnosticsTest, UnterminatedStringReportsE0002) {
    Lexer lexer("bad.c", "int32 s = \"abc");
    lexer.tokenize();
    const Diagnostic* d =
        findDiag(lexer.getDiagnostics(), DiagnosticCode::LexUnterminatedLiteral);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(diagnosticId(d->code), "E0002");
    EXPECT_EQ(d->line, 1);
    EXPECT_EQ(d->column, 11);
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
    Lexer lexer("bad.c", "int32 x; /* never closed");
    lexer.tokenize();
    const Diagnostic* d =
        findDiag(lexer.getDiagnostics(), DiagnosticCode::LexUnterminatedLiteral);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(diagnosticId(d->code), "E0002");
}

TEST(LexerDiagnosticsTest, DecimalLiteralOverflowReportsE0003) {
    Lexer lexer("bad.c", "int32 x = 999999999999999999999999;");
    lexer.tokenize();
    const Diagnostic* d = findDiag(lexer.getDiagnostics(), DiagnosticCode::LexIntegerOverflow);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(diagnosticId(d->code), "E0003");
    EXPECT_EQ(d->line, 1);
}

TEST(LexerDiagnosticsTest, HexLiteralOverflowReportsE0003) {
    Lexer lexer("bad.c", "int32 x = 0x1FFFFFFFFFFFFFFFFF;");
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
    Lexer lexer("bad.c", "int32 @ x;");
    lexer.tokenize();
    ASSERT_EQ(lexer.getDiagnostics().size(), 1u);
    const std::string formatted = lexer.getDiagnostics()[0].formatWithSeverity();
    EXPECT_NE(formatted.find("bad.c:1:7"), std::string::npos);
    EXPECT_NE(formatted.find("E0001"), std::string::npos);
}

// LEX-15: literal suffixes and the default/base type mapping.
TEST(LexerNumberSuffixTest, UnsuffixedIntegersDefaultToInt) {
    Lexer lexer("n.c", "42 2147483647");
    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_NUMBER);
    EXPECT_EQ(tokens[0].literalKind, LiteralKind::Int);
    EXPECT_EQ(tokens[1].literalKind, LiteralKind::Int);
}

TEST(LexerNumberSuffixTest, UnsuffixedFloatDefaultsToFloat64) {
    Lexer lexer("n.c", "3.14");
    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_FLOAT);
    EXPECT_EQ(tokens[0].literalKind, LiteralKind::Float64);
}

TEST(LexerNumberSuffixTest, IntegerSuffixesAreRecognizedOnAllRadixes) {
    Lexer lexer("n.c", "1u 1U 1l 1L 1ul 1UL 1lu 1LU 0xFFu 0b10L 0o7u");
    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 11u);
    EXPECT_EQ(tokens[0].literalKind, LiteralKind::UInt);
    EXPECT_EQ(tokens[1].literalKind, LiteralKind::UInt);
    EXPECT_EQ(tokens[2].literalKind, LiteralKind::Long);
    EXPECT_EQ(tokens[3].literalKind, LiteralKind::Long);
    EXPECT_EQ(tokens[4].literalKind, LiteralKind::ULong);
    EXPECT_EQ(tokens[5].literalKind, LiteralKind::ULong);
    EXPECT_EQ(tokens[6].literalKind, LiteralKind::ULong);
    EXPECT_EQ(tokens[7].literalKind, LiteralKind::ULong);
    EXPECT_EQ(tokens[8].literalKind, LiteralKind::UInt);
    EXPECT_EQ(tokens[9].literalKind, LiteralKind::Long);
    EXPECT_EQ(tokens[10].literalKind, LiteralKind::UInt);
    EXPECT_EQ(std::get<int>(tokens[0].value), 1);
}

TEST(LexerNumberSuffixTest, FloatSuffixesMapToKinds) {
    Lexer lexer("n.c", "3.14f 2.0F 3.5f16 4.5f32 5.5f64 6.5f128 1.5L 2.5l");
    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].literalKind, LiteralKind::Float32);
    EXPECT_EQ(tokens[1].literalKind, LiteralKind::Float32);
    EXPECT_EQ(tokens[2].literalKind, LiteralKind::Float16);
    EXPECT_EQ(tokens[3].literalKind, LiteralKind::Float32);
    EXPECT_EQ(tokens[4].literalKind, LiteralKind::Float64);
    EXPECT_EQ(tokens[5].literalKind, LiteralKind::Float128);
    EXPECT_EQ(tokens[6].literalKind, LiteralKind::Float64);
    EXPECT_EQ(tokens[7].literalKind, LiteralKind::Float64);
    EXPECT_NEAR(std::get<double>(tokens[0].value), 3.14, 1e-9);
}

TEST(LexerNumberSuffixTest, LiteralKindNameMappingTable) {
    EXPECT_STREQ(literalKindName(LiteralKind::Int), "int");
    EXPECT_STREQ(literalKindName(LiteralKind::UInt), "uint32");
    EXPECT_STREQ(literalKindName(LiteralKind::Long), "int64");
    EXPECT_STREQ(literalKindName(LiteralKind::ULong), "uint64");
    EXPECT_STREQ(literalKindName(LiteralKind::Float16), "float16");
    EXPECT_STREQ(literalKindName(LiteralKind::Float32), "float32");
    EXPECT_STREQ(literalKindName(LiteralKind::Float64), "float64");
    EXPECT_STREQ(literalKindName(LiteralKind::Float128), "float128");
}

// LEX-17: a digit separator '_' is only valid between two digits.
TEST(LexerSeparatorTest, ValidSeparatorsProduceNoDiagnostics) {
    Lexer lexer("ok.c", "1_000 0xFF_FF 0b1010_1010 0o7_7 1_000.000_1");
    auto tokens = lexer.tokenize();
    EXPECT_TRUE(lexer.getDiagnostics().empty());
    ASSERT_EQ(tokens.size(), 5u);
}

TEST(LexerSeparatorTest, ConsecutiveSeparatorsReportE0004) {
    Lexer lexer("bad.c", "1__0");
    lexer.tokenize();
    const Diagnostic* d =
        findDiag(lexer.getDiagnostics(), DiagnosticCode::LexInvalidDigitSeparator);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(diagnosticId(d->code), "E0004");
    EXPECT_EQ(d->line, 1);
    EXPECT_EQ(d->column, 2);
}

TEST(LexerSeparatorTest, TrailingSeparatorReportsE0004) {
    Lexer lexer("bad.c", "0xFF_");
    lexer.tokenize();
    const Diagnostic* d =
        findDiag(lexer.getDiagnostics(), DiagnosticCode::LexInvalidDigitSeparator);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->column, 5);
}

TEST(LexerSeparatorTest, SeparatorAfterPrefixReportsE0004) {
    Lexer lexer("bad.c", "0x_FF");
    lexer.tokenize();
    const Diagnostic* d =
        findDiag(lexer.getDiagnostics(), DiagnosticCode::LexInvalidDigitSeparator);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->column, 3);
}

TEST(LexerSeparatorTest, SeparatorBeforeFractionReportsE0004) {
    Lexer lexer("bad.c", "1_.5");
    lexer.tokenize();
    const Diagnostic* d =
        findDiag(lexer.getDiagnostics(), DiagnosticCode::LexInvalidDigitSeparator);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->column, 2);
}

TEST(LexerSeparatorTest, RecoveryKeepsNumberTokenAndRestOfStream) {
    Lexer lexer("bad.c", "int32 x = 1__0; int32 y = 2;");
    auto tokens = lexer.tokenize();
    EXPECT_FALSE(lexer.getDiagnostics().empty());
    ASSERT_EQ(tokens.size(), 10u);
    EXPECT_EQ(tokens[3].type, TokenType::TOKEN_NUMBER);
    EXPECT_EQ(std::get<int>(tokens[3].value), 10);
}

// LEX-05: floating-point exponents (e/E, optional sign, decimal digits).
TEST(LexerFloatExponentTest, ExponentWithoutFractionBecomesFloat) {
    Lexer lexer("e.c", "1e10 0e0 2E+4 1.5e-3");
    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 4u);
    for (const auto& t : tokens) {
        EXPECT_EQ(t.type, TokenType::TOKEN_FLOAT);
        EXPECT_EQ(t.literalKind, LiteralKind::Float64);
    }
    EXPECT_DOUBLE_EQ(std::get<double>(tokens[0].value), 1e10);
    EXPECT_DOUBLE_EQ(std::get<double>(tokens[1].value), 0.0);
    EXPECT_DOUBLE_EQ(std::get<double>(tokens[2].value), 2e4);
    EXPECT_NEAR(std::get<double>(tokens[3].value), 0.0015, 1e-12);
    EXPECT_TRUE(lexer.getDiagnostics().empty());
}

TEST(LexerFloatExponentTest, ExponentCombinesWithSuffix) {
    Lexer lexer("e.c", "1e10f 1.5e3f16 2.5e2f32 3.5e4f128");
    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].literalKind, LiteralKind::Float32);
    EXPECT_EQ(tokens[1].literalKind, LiteralKind::Float16);
    EXPECT_EQ(tokens[2].literalKind, LiteralKind::Float32);
    EXPECT_EQ(tokens[3].literalKind, LiteralKind::Float128);
    EXPECT_DOUBLE_EQ(std::get<double>(tokens[0].value), 1e10);
    EXPECT_NEAR(std::get<double>(tokens[1].value), 1500.0, 1e-9);
}

TEST(LexerFloatExponentTest, HexEIsADigitNotAnExponent) {
    Lexer lexer("e.c", "0x1e10");
    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_NUMBER);
    EXPECT_EQ(tokens[0].literalKind, LiteralKind::Int);
    EXPECT_EQ(std::get<int>(tokens[0].value), 0x1e10);
}

TEST(LexerFloatExponentTest, IncompleteExponentIsLeftAlone) {
    Lexer lexer("e.c", "1e 1e+x");
    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_NUMBER);
    EXPECT_EQ(std::get<int>(tokens[0].value), 1);
    EXPECT_EQ(tokens[1].type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].lexeme, "e");
    EXPECT_EQ(tokens[2].type, TokenType::TOKEN_NUMBER);
    EXPECT_EQ(tokens[3].type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[3].lexeme, "e");
    EXPECT_EQ(tokens[4].type, TokenType::TOKEN_PLUS);
    EXPECT_EQ(tokens[5].type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[5].lexeme, "x");
}

TEST(LexerFloatExponentTest, SeparatorsWithinExponentAreValidated) {
    Lexer ok("ok.c", "1e1_0 1_0e1_0");
    ok.tokenize();
    EXPECT_TRUE(ok.getDiagnostics().empty());

    Lexer bad("bad.c", "1e_10");
    bad.tokenize();
    const Diagnostic* d =
        findDiag(bad.getDiagnostics(), DiagnosticCode::LexInvalidDigitSeparator);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->line, 1);
    EXPECT_EQ(d->column, 3);
}

TEST(ClassKeywordTest, ClassTokenIsRecognized) {
    std::string source = "class Foo { int32 x; };";
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

TEST(LexerKeywordTest, Float16AndFloat128Keywords) {
    Lexer lexer("test.c", "float16 float128");
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_FLOAT16);
    EXPECT_EQ(tokens[0].lexeme, "float16");
    EXPECT_EQ(tokens[1].type, TokenType::TOKEN_FLOAT128);
    EXPECT_EQ(tokens[1].lexeme, "float128");
}

// TYP: only fixed-width type spellings exist; `int`/`float`/`double` are no
// longer keywords and must lex as ordinary identifiers.
TEST(LexerKeywordTest, LegacyNumericKeywordsAreIdentifiers) {
    Lexer lexer("test.c", "int float double");
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[0].lexeme, "int");
    EXPECT_EQ(tokens[1].type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[1].lexeme, "float");
    EXPECT_EQ(tokens[2].type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(tokens[2].lexeme, "double");
}

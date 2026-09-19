#include <gtest/gtest.h>

#include "ast/PrintFormat.h"

namespace {

// Convenience: build a format string and return it, or the error text prefixed
// with "ERR:" so a failing assertion is readable.
std::string build(const std::string& literal,
                  const std::vector<PrintArgKind>& kinds,
                  bool newline = false) {
    std::string out, err;
    if (!buildPrintFormat(literal, kinds, newline, out, err)) {
        return "ERR:" + err;
    }
    return out;
}

} // namespace

TEST(PrintFormatTest, EmptyLiteral) {
    EXPECT_EQ(build("", {}), "");
}

TEST(PrintFormatTest, SingleIntPlaceholder) {
    EXPECT_EQ(build("aa {}", {PrintArgKind::Int32}), "aa %d");
}

TEST(PrintFormatTest, MultiplePlaceholders) {
    EXPECT_EQ(build("{}+{}={}", {PrintArgKind::Int32, PrintArgKind::Int32,
                                 PrintArgKind::Int32}),
              "%d+%d=%d");
}

TEST(PrintFormatTest, Int64UsesLongLong) {
    EXPECT_EQ(build("{}", {PrintArgKind::Int64}), "%lld");
}

TEST(PrintFormatTest, UnsignedKinds) {
    EXPECT_EQ(build("{} {}", {PrintArgKind::UInt32, PrintArgKind::UInt64}),
              "%u %llu");
}

TEST(PrintFormatTest, CharFloatStringPointer) {
    EXPECT_EQ(build("{} {} {} {}", {PrintArgKind::Char, PrintArgKind::Float,
                                    PrintArgKind::CString, PrintArgKind::Pointer}),
              "%c %g %s %p");
}

TEST(PrintFormatTest, BoolAndToStringUseString) {
    EXPECT_EQ(build("{} {}", {PrintArgKind::Bool, PrintArgKind::ToString}),
              "%s %s");
}

TEST(PrintFormatTest, EscapedBraces) {
    EXPECT_EQ(build("{{}}", {}), "{}");
    EXPECT_EQ(build("a {{ b }} c", {}), "a { b } c");
}

TEST(PrintFormatTest, LiteralPercentIsEscaped) {
    EXPECT_EQ(build("50%", {}), "50%%");
    EXPECT_EQ(build("{}%", {PrintArgKind::Int32}), "%d%%");
}

TEST(PrintFormatTest, NewlineAppended) {
    EXPECT_EQ(build("hi", {}, true), "hi\n");
    EXPECT_EQ(build("{}", {PrintArgKind::Int32}, true), "%d\n");
}

TEST(PrintFormatTest, ExplicitHexSpec) {
    EXPECT_EQ(build("{:x}", {PrintArgKind::Int32}), "%x");
}

TEST(PrintFormatTest, ExplicitSpecOnInt64InsertsLength) {
    EXPECT_EQ(build("{:x}", {PrintArgKind::Int64}), "%llx");
}

TEST(PrintFormatTest, ExplicitFloatPrecision) {
    EXPECT_EQ(build("{:.2f}", {PrintArgKind::Float}), "%.2f");
}

TEST(PrintFormatTest, TooFewArgumentsIsError) {
    EXPECT_EQ(build("{} {}", {PrintArgKind::Int32}),
              "ERR:too few arguments for print format");
}

TEST(PrintFormatTest, TooManyArgumentsIsError) {
    EXPECT_EQ(build("{}", {PrintArgKind::Int32, PrintArgKind::Int32}),
              "ERR:too many arguments for print format");
}

TEST(PrintFormatTest, UnterminatedPlaceholderIsError) {
    EXPECT_EQ(build("a { b", {PrintArgKind::Int32}),
              "ERR:unterminated '{' in print format");
}

TEST(PrintFormatTest, SingleCloseBraceIsError) {
    EXPECT_EQ(build("a } b", {}),
              "ERR:single '}' in print format; use '}}' for a literal brace");
}

TEST(PrintFormatTest, SpecOnStringIsError) {
    EXPECT_EQ(build("{:x}", {PrintArgKind::CString}),
              "ERR:format spec is not supported for this argument type");
}

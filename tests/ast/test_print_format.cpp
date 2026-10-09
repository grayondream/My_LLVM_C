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

// ===== P1-09 (FMT-08): PrintSpec 规格集 =====

TEST(PrintFormatTest, IntegerSpecsHexOctBin) {
    EXPECT_EQ(build("{:x} {:X} {:o} {:b}",
                    {PrintArgKind::Int32, PrintArgKind::Int32,
                     PrintArgKind::Int32, PrintArgKind::Int32}),
              "%x %X %o %b");
}

TEST(PrintFormatTest, BinaryNeeds64BitModifier) {
    EXPECT_EQ(build("{:b}", {PrintArgKind::Int64}), "%llb");
}

TEST(PrintFormatTest, ZeroPadWidth) {
    EXPECT_EQ(build("{:02}", {PrintArgKind::Int32}), "%02d");
    EXPECT_EQ(build("{:05x}", {PrintArgKind::UInt32}), "%05x");
}

TEST(PrintFormatTest, FloatPrecisionAndExponent) {
    EXPECT_EQ(build("{:.2f} {:.0e}", {PrintArgKind::Float, PrintArgKind::Float}),
              "%.2f %.0e");
}

TEST(PrintFormatTest, FloatWithoutTypeInfersF) {
    EXPECT_EQ(build("{:.2}", {PrintArgKind::Float}), "%.2f");
}

TEST(PrintFormatTest, IntegerWidthAlignment) {
    EXPECT_EQ(build("{:>8} {:<8}", {PrintArgKind::Int32, PrintArgKind::Int32}),
              "%8d %-8d");
}

TEST(PrintFormatTest, SignFlags) {
    EXPECT_EQ(build("{:+}", {PrintArgKind::Int32}), "%+d");
    EXPECT_EQ(build("{:-}", {PrintArgKind::Int32}), "%d");
    EXPECT_EQ(build("{: 5}", {PrintArgKind::Int32}), "% 5d");
}

TEST(PrintFormatTest, StrWidthDefaultsLeft) {
    EXPECT_EQ(build("{:6}", {PrintArgKind::Str}), "%-6.*s");
    EXPECT_EQ(build("{:<6}", {PrintArgKind::Str}), "%-6.*s");
    EXPECT_EQ(build("{:>6}", {PrintArgKind::Str}), "%6.*s");
}

TEST(PrintFormatTest, BoolWidth) {
    EXPECT_EQ(build("{:>6}", {PrintArgKind::Bool}), "%6s");
    EXPECT_EQ(build("{:6}", {PrintArgKind::Bool}), "%-6s");
}

TEST(PrintFormatTest, RenderPathOutputsS) {
    EXPECT_EQ(build("{:*<5}", {PrintArgKind::Int32}), "%s");
    EXPECT_EQ(build("{:^6}", {PrintArgKind::Str}), "%s");
}

TEST(PrintFormatTest, ExplicitDecimalRejected) {
    EXPECT_EQ(build("{:d}", {PrintArgKind::Int32}),
              "ERR:invalid format spec ':d'");
}

TEST(PrintFormatTest, SpecKindMismatches) {
    EXPECT_EQ(build("{:x}", {PrintArgKind::Float}),
              "ERR:format spec is not supported for this argument type");
    EXPECT_EQ(build("{:f}", {PrintArgKind::Int32}),
              "ERR:format spec is not supported for this argument type");
    EXPECT_EQ(build("{:s}", {PrintArgKind::Int32}),
              "ERR:format spec is not supported for this argument type");
    EXPECT_EQ(build("{:.2}", {PrintArgKind::Int32}),
              "ERR:format spec is not supported for this argument type");
    EXPECT_EQ(build("{:.2}", {PrintArgKind::Str}),
              "ERR:format spec is not supported for this argument type");
    EXPECT_EQ(build("{:x}", {PrintArgKind::Bool}),
              "ERR:format spec is not supported for this argument type");
    EXPECT_EQ(build("{:x}", {PrintArgKind::Pointer}),
              "ERR:format spec is not supported for this argument type");
}

TEST(PrintFormatTest, SpecSyntaxErrors) {
    EXPECT_EQ(build("{:z}", {PrintArgKind::Int32}),
              "ERR:invalid format spec ':z'");
    EXPECT_EQ(build("{:0<5}", {PrintArgKind::Int32}),
              "ERR:zero fill requires right alignment");
    EXPECT_EQ(build("{:0}", {PrintArgKind::Int32}),
              "ERR:'0' fill requires a width");
    EXPECT_EQ(build("{:>}", {PrintArgKind::Int32}),
              "ERR:alignment requires a width");
    EXPECT_EQ(build("{:201}", {PrintArgKind::Int32}),
              "ERR:format spec width/precision too large");
}

TEST(PrintFormatTest, EmptySpecFloatStillG) {
    EXPECT_EQ(build("{}", {PrintArgKind::Float}), "%g");
}

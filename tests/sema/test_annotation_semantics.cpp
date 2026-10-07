// P1-05 / ANN-06: 注解目标验证与冲突检测（诊断文案 spec §5 逐字）。

#include <gtest/gtest.h>

#include <string>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"

#include <spdlog/spdlog.h>

class AnnotationSemantics : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }

    // 返回 sema 是否有包含 `needle` 的错误。
    bool hasError(const std::string& source, const std::string& needle) {
        Lexer lexer("ann_sem_test.c", source);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto ast = parser.parse();
        if (!ast) return false;
        SemanticAnalyzer analyzer;
        analyzer.analyze(*ast);
        for (const auto& d : analyzer.getErrors()) {
            if (d.message.find(needle) != std::string::npos) return true;
        }
        return false;
    }
};

TEST_F(AnnotationSemantics, UnknownAnnotationE2010) {
    EXPECT_TRUE(hasError("[[bogus]] int32 f() { return 0; }",
                         "unknown annotation 'bogus'"));
}

TEST_F(AnnotationSemantics, WrongTargetE2011) {
    EXPECT_TRUE(hasError("int32 main() { [[nonnull]] int32 x; return 0; }",
                         "annotation 'nonnull' is not valid on variable"));
}

TEST_F(AnnotationSemantics, ReprCOnClassE2011) {
    EXPECT_TRUE(hasError("[[repr(C)]] class K { public: int32 x; }",
                         "annotation 'repr' is not valid on class"));
}

TEST_F(AnnotationSemantics, ReprArgMustBeC) {
    EXPECT_TRUE(hasError("[[repr(A)]] struct S { int32 x; };",
                         "repr argument must be 'C'"));
}

TEST_F(AnnotationSemantics, DuplicateE2012) {
    EXPECT_TRUE(hasError("[[inline]] [[inline]] int32 f() { return 0; }",
                         "duplicate annotation 'inline'"));
}

TEST_F(AnnotationSemantics, AlignNotPowerOfTwoE2013) {
    EXPECT_TRUE(hasError("[[align(3)]] struct S { int32 x; };",
                         "align argument must be a power of two"));
}

TEST_F(AnnotationSemantics, AlignNotConstantE2014) {
    EXPECT_TRUE(hasError("int32 main() { [[align(n)]] int32 x; return 0; }",
                         "annotation argument must be a compile-time constant"));
}

// ---- P1-05 / ANN-05: deprecated 使用警告（W3004）与 nonnull（W3005）----

static bool hasWarning(const std::string& source, const std::string& needle) {
    Lexer lexer("ann_warn_test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast) return false;
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    for (const auto& d : analyzer.getWarnings()) {
        if (d.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

TEST_F(AnnotationSemantics, DeprecatedFuncW3004) {
    EXPECT_TRUE(hasWarning(
        "[[deprecated]] int32 old() { return 1; }\n"
        "int32 main() { return old(); }",
        "is deprecated"));
}

TEST_F(AnnotationSemantics, DeprecatedFuncWithMsg) {
    EXPECT_TRUE(hasWarning(
        "[[deprecated(\"use new\")]] int32 old() { return 1; }\n"
        "int32 main() { return old(); }",
        "is deprecated: use new"));
}

TEST_F(AnnotationSemantics, DeprecatedTypeW3004) {
    EXPECT_TRUE(hasWarning(
        "[[deprecated]] struct Old { int32 x; }\n"
        "int32 main() { Old o; return 0; }",
        "is deprecated"));
}

TEST_F(AnnotationSemantics, DeprecatedVarW3004) {
    EXPECT_TRUE(hasWarning(
        "int32 main() { [[deprecated]] int32 v = 1; return v; }",
        "is deprecated"));
}

TEST_F(AnnotationSemantics, DeprecatedFieldW3004) {
    EXPECT_TRUE(hasWarning(
        "struct S { [[deprecated]] int32 x; }\n"
        "int32 main() { S s; return s.x; }",
        "is deprecated"));
}

TEST_F(AnnotationSemantics, NonNullLiteralNullW3005) {
    EXPECT_TRUE(hasWarning(
        "int32 f([[nonnull]] int32* p) { return *p; }\n"
        "int32 main() { return f(null); }",
        "null passed to nonnull parameter 'p' of 'f'"));
}

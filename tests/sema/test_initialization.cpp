// SEM-01/02: definite-assignment analysis (warnings only, never errors).
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"

#include <spdlog/spdlog.h>

namespace {

class InitializationAnalysisTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::off); }
};

struct Result {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

Result analyze(const std::string& source) {
    Lexer lexer("init.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    Result r;
    if (!ast) {
        r.errors.push_back("<parse failed>");
        return r;
    }
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    for (const auto& d : analyzer.getErrors()) r.errors.push_back(d.message);
    for (const auto& d : analyzer.getWarnings()) r.warnings.push_back(d.message);
    return r;
}

bool warnedAbout(const Result& r, const std::string& var) {
    for (const auto& w : r.warnings) {
        if (w.find("'" + var + "'") != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST_F(InitializationAnalysisTest, WarnsOnUninitializedRead) {
    auto r = analyze("int main() { int y; return y; }");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_TRUE(warnedAbout(r, "y"));
}

TEST_F(InitializationAnalysisTest, NoWarningWhenInitialized) {
    auto r = analyze("int main() { int y = 3; return y; }");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(InitializationAnalysisTest, NoWarningForParameters) {
    auto r = analyze("int f(int a) { return a + 1; } int main() { return f(2) - 3; }");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(InitializationAnalysisTest, NoWarningForGlobals) {
    auto r = analyze("int g; int main() { return g; }");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(InitializationAnalysisTest, BranchesJoinOnBothAssigned) {
    auto r = analyze(R"(
int main() {
    int a;
    int c = 1;
    if (c) { a = 1; } else { a = 2; }
    return a;
})");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_FALSE(warnedAbout(r, "a"));
}

TEST_F(InitializationAnalysisTest, WarnsWhenOnlyOneBranchAssigns) {
    auto r = analyze(R"(
int main() {
    int a;
    int c = 1;
    if (c) { a = 1; }
    return a;
})");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_TRUE(warnedAbout(r, "a"));
}

TEST_F(InitializationAnalysisTest, LoopIsConservative) {
    // The body may run zero times, so `x` is not definitely assigned.
    auto r = analyze(R"(
int main() {
    int x;
    int c = 0;
    while (c) { x = 1; }
    return x;
})");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_TRUE(warnedAbout(r, "x"));
}

TEST_F(InitializationAnalysisTest, DoWhileAssignsInBody) {
    auto r = analyze(R"(
int main() {
    int x;
    int c = 1;
    do { x = 2; c = 0; } while (c);
    return x;
})");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_FALSE(warnedAbout(r, "x"));
}

TEST_F(InitializationAnalysisTest, AddressTakenSuppressesWarning) {
    auto r = analyze(R"(
void set(int* p) { *p = 7; }
int main() {
    int x;
    set(&x);
    return x;
})");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_FALSE(warnedAbout(r, "x"));
}

TEST_F(InitializationAnalysisTest, WarnsOnUninitializedPointerDeref) {
    auto r = analyze("int main() { int* p; return *p; }");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_TRUE(warnedAbout(r, "p"));
}

TEST_F(InitializationAnalysisTest, AssignedBeforeReadIsFine) {
    auto r = analyze("int main() { int y; y = 4; return y; }");
    ASSERT_TRUE(r.errors.empty());
    EXPECT_TRUE(r.warnings.empty());
}

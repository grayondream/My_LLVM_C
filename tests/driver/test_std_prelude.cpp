// MOD-09 / STD-23: the built-in std.c libc binding layer.
#include <gtest/gtest.h>

#include "ast/Decl.h"
#include "driver/StdPrelude.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"

#include <spdlog/spdlog.h>

#include <set>
#include <string>

namespace {

class StdPreludeTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::off); }
};

std::set<std::string> functionNames(TranslationUnitAST& ast) {
    std::set<std::string> names;
    for (auto& d : ast.declarations) {
        if (auto* fn = dynamic_cast<FunctionDeclAST*>(d.get())) {
            names.insert(fn->name);
        }
    }
    return names;
}

} // namespace

TEST_F(StdPreludeTest, BuiltinPreludeParsesCleanly) {
    Lexer lexer("<std.c>", smc::builtinStdCPrelude());
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_TRUE(parser.getErrors().empty());

    auto names = functionNames(*ast);
    for (const char* expected : {"printf", "puts", "malloc", "calloc", "free",
                                 "memcpy", "memset", "strlen", "abs", "exit"}) {
        EXPECT_TRUE(names.count(expected)) << "missing binding: " << expected;
    }
}

TEST_F(StdPreludeTest, PrependDeclarationsPutsPreludeFirst) {
    auto prelude = smc::parseStdCPrelude("int pre();", "pre");
    auto target = smc::parseStdCPrelude("int usr();", "usr");
    ASSERT_NE(prelude, nullptr);
    ASSERT_NE(target, nullptr);

    smc::prependDeclarations(*target, *prelude);

    ASSERT_EQ(target->declarations.size(), 2u);
    auto* first = dynamic_cast<FunctionDeclAST*>(target->declarations[0].get());
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->name, "pre");
    auto* second = dynamic_cast<FunctionDeclAST*>(target->declarations[1].get());
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->name, "usr");
}

TEST_F(StdPreludeTest, UserCodeCanResolvePreludeSymbols) {
    // abs() is not declared by the user; the prelude supplies it.
    auto ast = smc::parseStdCPrelude("int main() { return abs(-5) - 5; }", "user");
    ASSERT_NE(ast, nullptr);
    auto prelude = smc::parseStdCPrelude(smc::builtinStdCPrelude(), "<std.c>");
    ASSERT_NE(prelude, nullptr);
    smc::prependDeclarations(*ast, *prelude);

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_TRUE(analyzer.getErrors().empty());
}

TEST_F(StdPreludeTest, UserCanRedeclarePreludeSymbol) {
    // A matching user prototype must not be reported as a redefinition.
    auto ast = smc::parseStdCPrelude(
        "extern int abs(int x);\nint main() { return abs(-3) - 3; }", "user");
    ASSERT_NE(ast, nullptr);
    auto prelude = smc::parseStdCPrelude(smc::builtinStdCPrelude(), "<std.c>");
    ASSERT_NE(prelude, nullptr);
    smc::prependDeclarations(*ast, *prelude);

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_TRUE(analyzer.getErrors().empty());
}

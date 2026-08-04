#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"

namespace fs = std::filesystem;

class ConstConstexprE2E : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
        static bool init = false;
        if (!init) {
            llvm::InitializeAllTargetInfos();
            llvm::InitializeAllTargets();
            llvm::InitializeAllTargetMCs();
            llvm::InitializeAllAsmParsers();
            llvm::InitializeAllAsmPrinters();
            init = true;
        }
    }
};

static int runSource(const std::string& source, const std::string& filename) {
    Lexer lexer(filename, source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast) return -1;

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    if (!analyzer.getErrors().empty()) return -1;

    CodegenContext ctx;
    ctx.setSourceFile(filename);
    ast->codegen(ctx);
    ctx.finalizeDebugInfo();

    std::string ve;
    llvm::raw_string_ostream vs(ve);
    bool bad = llvm::verifyModule(ctx.getModule(), &vs);
    if (bad) return -1;

    auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!jtmb) return -1;
    auto jit = llvm::orc::LLJITBuilder()
        .setJITTargetMachineBuilder(std::move(*jtmb))
        .create();
    if (!jit) return -1;

    auto ts = llvm::orc::ThreadSafeModule(ctx.takeModule(), ctx.takeContext());
    if (auto e = (*jit)->addIRModule(std::move(ts))) return -1;

    auto sym = (*jit)->lookup("main");
    if (!sym) return -1;

    auto fn = (int (*)())(intptr_t)sym->getValue();
    return fn();
}

TEST_F(ConstConstexprE2E, ConstVariable) {
    EXPECT_EQ(runSource(
        "const int x = 42; int main() { return x; }", "const_var.c"), 42);
}

TEST_F(ConstConstexprE2E, ConstexprVariable) {
    EXPECT_EQ(runSource(
        "constexpr int x = 42; int main() { return x; }", "constexpr_var.c"), 42);
}

TEST_F(ConstConstexprE2E, ConstexprTwoGlobals) {
    EXPECT_EQ(runSource(
        "constexpr int a = 10; constexpr int b = 23; int main() { return b; }",
        "constexpr_two.c"), 23);
}

TEST_F(ConstConstexprE2E, ConstexprExpression) {
    EXPECT_EQ(runSource(
        "constexpr int a = 10; constexpr int b = a * 2 + 3; int main() { return b; }",
        "constexpr_expr.c"), 23);
}

TEST_F(ConstConstexprE2E, LocalConstVariable) {
    EXPECT_EQ(runSource(
        "int main() { const int x = 42; return x; }", "local_const.c"), 42);
}

TEST_F(ConstConstexprE2E, LocalConstexprVariable) {
    EXPECT_EQ(runSource(
        "int main() { constexpr int x = 42; return x; }", "local_constexpr.c"), 42);
}

TEST_F(ConstConstexprE2E, MixedConstConstexpr) {
    EXPECT_EQ(runSource(
        "constexpr int a = 10; const int b = 20; int main() { return a + b; }",
        "mixed.c"), 30);
}

TEST_F(ConstConstexprE2E, ConstexprInFunction) {
    EXPECT_EQ(runSource(
        "int main() { constexpr int x = 5; constexpr int y = x * 3; return y; }",
        "constexpr_func.c"), 15);
}

TEST_F(ConstConstexprE2E, ConstFromResourceFile) {
    std::ifstream ifs(fs::path(TEST_INPUT_DIR) / "test_const.c");
    ASSERT_TRUE(ifs.is_open());
    std::stringstream ss;
    ss << ifs.rdbuf();
    EXPECT_EQ(runSource(ss.str(), "test_const.c"), 0);
}

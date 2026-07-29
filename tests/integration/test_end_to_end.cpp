#include <gtest/gtest.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"

namespace fs = std::filesystem;

class EndToEndTest : public ::testing::Test {
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

TEST_F(EndToEndTest, SimpleReturn42) {
    EXPECT_EQ(runSource("int main() { return 42; }", "simple.c"), 42);
}

TEST_F(EndToEndTest, LocalVar) {
    EXPECT_EQ(runSource("int main() { int a = 10; return a; }", "local.c"), 10);
}

TEST_F(EndToEndTest, TwoVars) {
    EXPECT_EQ(runSource(
        "int main() { int a = 10; int b = 20; return a + b; }", "two.c"), 30);
}

TEST_F(EndToEndTest, ExpressionArithmetic) {
    EXPECT_EQ(runSource(
        "int main() { int a = 10; int b = 20; int c = a + b * 3; return c; }", "expr.c"), 70);
}

TEST_F(EndToEndTest, ForLoop) {
    EXPECT_EQ(runSource(
        "int main() { int sum = 0; for (int i = 0; i <= 10; i = i + 1) { sum = sum + i; } return sum; }", "for.c"), 55);
}

TEST_F(EndToEndTest, SimpleFromFile) {
    std::ifstream ifs(fs::path(TEST_INPUT_DIR) / "simple.c");
    ASSERT_TRUE(ifs.is_open());
    std::stringstream ss; ss << ifs.rdbuf();
    EXPECT_EQ(runSource(ss.str(), "simple.c"), 42);
}

TEST_F(EndToEndTest, ExpressionFromFile) {
    std::ifstream ifs(fs::path(TEST_INPUT_DIR) / "expressions.c");
    ASSERT_TRUE(ifs.is_open());
    std::stringstream ss; ss << ifs.rdbuf();
    EXPECT_EQ(runSource(ss.str(), "expressions.c"), 70);
}

TEST_F(EndToEndTest, ControlFlowFromFile) {
    std::ifstream ifs(fs::path(TEST_INPUT_DIR) / "control_flow.c");
    ASSERT_TRUE(ifs.is_open());
    std::stringstream ss; ss << ifs.rdbuf();
    EXPECT_EQ(runSource(ss.str(), "control_flow.c"), 55);
}

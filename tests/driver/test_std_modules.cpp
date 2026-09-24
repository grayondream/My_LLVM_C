// P0-05 / STD-01, STD-12: the minimal std.core and std.io modules.
#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "ast/Decl.h"
#include "codegen/CodegenContext.h"
#include "driver/ModuleLoader.h"
#include "driver/StdPrelude.h"
#include "sema/SemanticAnalyzer.h"
#include "support/Log.h"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"

#include <spdlog/spdlog.h>

namespace {

class StdModulesTest : public ::testing::Test {
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

int runWithImports(const std::string& source, const std::string& filename) {
    auto ast = smc::parseStdCPrelude(source, filename);
    if (!ast) return -1;

    smc::ModuleSearchPaths paths{{STD_DIR}};
    std::set<std::string> loaded;
    std::vector<std::string> errors;
    smc::processImports(*ast, ".", paths, loaded, errors);
    if (!errors.empty()) return -1;

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    if (!analyzer.getErrors().empty()) return -1;

    CodegenContext ctx;
    ctx.setSourceFile(filename);
    ast->codegen(ctx);
    ctx.finalizeDebugInfo();

    std::string ve;
    llvm::raw_string_ostream vs(ve);
    if (llvm::verifyModule(ctx.getModule(), &vs)) return -1;

    auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!jtmb) return -1;
    auto jit = llvm::orc::LLJITBuilder().setJITTargetMachineBuilder(std::move(*jtmb)).create();
    if (!jit) return -1;

    auto ts = llvm::orc::ThreadSafeModule(ctx.takeModule(), ctx.takeContext());
    if (auto e = (*jit)->addIRModule(std::move(ts))) return -1;

    auto sym = (*jit)->lookup("main");
    if (!sym) return -1;
    return ((int (*)())(intptr_t)sym->getValue())();
}

} // namespace

TEST_F(StdModulesTest, ResolvesModulesFromStdDir) {
    smc::ModuleSearchPaths paths{{STD_DIR}};
    EXPECT_FALSE(smc::resolveImport("std.core", paths).empty());
    EXPECT_FALSE(smc::resolveImport("std.io", paths).empty());
}

TEST_F(StdModulesTest, CoreMinMax) {
    EXPECT_EQ(runWithImports(
        "import std.core;\nint main() { return std::min(3, 5) + std::max(2, 9) - 12; }\n",
        "std_core_minmax.smc"), 0);
}

TEST_F(StdModulesTest, CoreClamp) {
    EXPECT_EQ(runWithImports(
        "import std.core;\n"
        "int main() { return std::clamp(42, 0, 10) - 10; }\n",
        "std_core_clamp.smc"), 0);
}

TEST_F(StdModulesTest, CoreConstexprUse) {
    // std::min is constexpr and folds at compile time.
    EXPECT_EQ(runWithImports(
        "import std.core;\n"
        "int main() { constexpr int m = std::min(4, 7); return m - 4; }\n",
        "std_core_constexpr.smc"), 0);
}

TEST_F(StdModulesTest, IoPrintInt) {
    EXPECT_EQ(runWithImports(
        "import std.io;\nint main() { std::print_int(7); return 0; }\n",
        "std_io_print_int.smc"), 0);
}

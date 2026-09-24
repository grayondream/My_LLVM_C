// P0-04 / MOD-04: source-file import resolution and declaration splicing, and
// the file-backed std.c binding layer (MOD-09 / STD-23).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "ast/Decl.h"
#include "codegen/CodegenContext.h"
#include "driver/ModuleLoader.h"
#include "driver/StdPrelude.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"
#include "support/Log.h"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace {

class ModuleLoaderTest : public ::testing::Test {
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

int jitRun(TranslationUnitAST& ast) {
    CodegenContext ctx;
    ctx.setSourceFile("module_test.c");
    ast.codegen(ctx);
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

TEST_F(ModuleLoaderTest, ResolvesStdCFromLibsDir) {
    smc::ModuleSearchPaths paths{{STD_DIR}};
    std::string resolved = smc::resolveImport("std.c", paths);
    ASSERT_FALSE(resolved.empty());
    EXPECT_NE(resolved.find("c.smc"), std::string::npos);
}

TEST_F(ModuleLoaderTest, StdPreludeLoadsFromFile) {
    const std::string text = smc::loadStdCPrelude({STD_DIR});
    EXPECT_NE(text.find("memcpy"), std::string::npos);
    EXPECT_NE(text.find("malloc"), std::string::npos);
    EXPECT_NE(text.find("abs"), std::string::npos);
}

TEST_F(ModuleLoaderTest, UnresolvedImportReturnsEmpty) {
    smc::ModuleSearchPaths paths{{STD_DIR}};
    EXPECT_TRUE(smc::resolveImport("no_such_module_xyz", paths).empty());
}

TEST_F(ModuleLoaderTest, DottedSpecifierMapsToPath) {
    smc::ModuleSearchPaths paths{{STD_DIR}};
    // `std.c` resolves through the dotted-name rule to `<dir>/std/c.smc`.
    EXPECT_FALSE(smc::resolveImport("std.c", paths).empty());
    // A specifier with a slash is treated as a relative path.
    EXPECT_FALSE(smc::resolveImport("std/c.smc", paths).empty());
}

TEST_F(ModuleLoaderTest, ProcessImportsSplicesDeclarations) {
    const fs::path dir = fs::temp_directory_path() / "smc_module_loader_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "util.smc");
        out << "int helper() { return 5; }\n";
    }

    auto ast = smc::parseStdCPrelude(
        "import util;\nint main() { return helper() - 5; }\n",
        (dir / "main.smc").string());
    ASSERT_NE(ast, nullptr);

    std::set<std::string> loaded;
    std::vector<std::string> errors;
    smc::processImports(*ast, dir.string(), {}, loaded, errors);
    for (const auto& e : errors) ADD_FAILURE() << e;
    ASSERT_TRUE(errors.empty());
    ASSERT_FALSE(ast->declarations.empty());

    // The imported declaration must be the first declaration.
    auto* first = dynamic_cast<FunctionDeclAST*>(ast->declarations.front().get());
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->name, "helper");

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_TRUE(analyzer.getErrors().empty());

    EXPECT_EQ(jitRun(*ast), 0);
    fs::remove_all(dir);
}

TEST_F(ModuleLoaderTest, CyclicImportsTerminate) {
    const fs::path dir = fs::temp_directory_path() / "smc_module_cycle_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "a.smc");
        out << "import b;\nint fa() { return 1; }\n";
    }
    {
        std::ofstream out(dir / "b.smc");
        out << "import a;\nint fb() { return 2; }\n";
    }

    auto ast = smc::parseStdCPrelude("import a;\nint main() { return fa() + fb() - 3; }\n",
                                     (dir / "main.smc").string());
    ASSERT_NE(ast, nullptr);

    std::set<std::string> loaded;
    std::vector<std::string> errors;
    smc::processImports(*ast, dir.string(), {}, loaded, errors);
    ASSERT_TRUE(errors.empty());

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    EXPECT_TRUE(analyzer.getErrors().empty());
    EXPECT_EQ(jitRun(*ast), 0);
    fs::remove_all(dir);
}

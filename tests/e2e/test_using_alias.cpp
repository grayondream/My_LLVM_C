// PAR-24: `using` is the sole type-alias form; its RHS is a full type-id.
// Coverage: fixed-size arrays (1D/multi-dim/sizeof/slice/designated init),
// function pointers, inline enums, annotations and removed legacy aliases.

// (`B<int32> g;`) and `using` aliases placed after a record definition.

#include <gtest/gtest.h>

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

#include <spdlog/spdlog.h>

class UsingAliasE2E : public ::testing::Test {
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
    if (!ast) { ADD_FAILURE() << "Parse failed"; return -1; }
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    if (!analyzer.getErrors().empty()) {
        for (auto& err : analyzer.getErrors()) ADD_FAILURE() << "Semantic error: " << err.format();
        return -1;
    }
    CodegenContext ctx;
    ctx.setSourceFile(filename);
    ast->codegen(ctx);
    ctx.finalizeDebugInfo();
    std::string ve;
    llvm::raw_string_ostream vs(ve);
    if (llvm::verifyModule(ctx.getModule(), &vs)) {
        vs.flush(); ADD_FAILURE() << "Module verification failed: " << ve; return -1;
    }
    auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!jtmb) { ADD_FAILURE() << "detectHost failed"; return -1; }
    auto jit = llvm::orc::LLJITBuilder().setJITTargetMachineBuilder(std::move(*jtmb)).create();
    if (!jit) { ADD_FAILURE() << "LLJIT create failed"; return -1; }
    auto ts = llvm::orc::ThreadSafeModule(ctx.takeModule(), ctx.takeContext());
    if (auto e = (*jit)->addIRModule(std::move(ts))) { ADD_FAILURE() << "addIRModule failed"; return -1; }
    auto sym = (*jit)->lookup("main");
    if (!sym) { ADD_FAILURE() << "lookup main failed"; return -1; }
    auto fn = (int (*)())(intptr_t)sym->getValue();
    return fn();
}

TEST_F(UsingAliasE2E, FixedArrayAlias) {
    EXPECT_EQ(runSource(R"(
        using IntArr = int32[4];
        int32 main() { IntArr a = {1, 2, 3, 4}; return a[3] - 4; }
    )", "using_arr.c"), 0);
}

TEST_F(UsingAliasE2E, MultidimArrayAlias) {
    EXPECT_EQ(runSource(R"(
        using Mat = int32[2][3];
        int32 main() { Mat m; m[1][2] = 6; return m[1][2] - 6; }
    )", "using_mat.c"), 0);
}

TEST_F(UsingAliasE2E, ArrayAliasSizeofAndSlice) {
    EXPECT_EQ(runSource(R"(
        using IntArr = int32[10];
        int32 main() {
            IntArr a; a[9] = 9;
            int32[] s = a;
            return (sizeof(a) == 40 && s.len == 10) ? 0 : 1;
        }
    )", "using_arr_meta.c"), 0);
}

TEST_F(UsingAliasE2E, SliceAliasStillWorks) {
    EXPECT_EQ(runSource(R"(
        using Sl = int32[];
        int32 main() { int32 a[3]; a[0] = 1; Sl s = a; return s[0] - 1; }
    )", "using_slice.c"), 0);
}

TEST_F(UsingAliasE2E, ArrayAliasDesignatedZeroFill) {
    EXPECT_EQ(runSource(R"(
        using Arr = int32[4];
        int32 main() { Arr a = {[1] = 5}; return a[0]; }
    )", "using_arr_di.c"), 0);
}

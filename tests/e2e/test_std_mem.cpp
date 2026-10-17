// P1-09 (STD-05): std.mem 纯函数层端到端测试——分配包装 + 对齐工具 + 对齐分配。
// fixture 仿 tests/e2e/test_std_string_lib.cpp 的 runWithImports（完整模块加载 + JIT 执行）。

#include <gtest/gtest.h>

#include <cstdlib>
#include <set>
#include <string>

#include "ast/Decl.h"
#include "codegen/CodegenContext.h"
#include "driver/ModuleLoader.h"
#include "driver/StdPrelude.h"
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"
#include "support/Log.h"

#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <spdlog/spdlog.h>

namespace {

class StdMemTest : public ::testing::Test {
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

static int runWithImports(const std::string& source, const std::string& filename) {
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

TEST_F(StdMemTest, AllocReturnsNonNull) {
    EXPECT_EQ(runWithImports(R"(
        import std.mem;
        int32 main() {
            void* p = std::alloc(16);
            if (p == null) return 1;
            std::dealloc(p);
            return 0;
        }
    )", "mem_alloc.c"), 0);
}

TEST_F(StdMemTest, AllocZeroedClearsMemory) {
    EXPECT_EQ(runWithImports(R"(
        import std.mem;
        int32 main() {
            int32* p = (int32*)std::alloc_zeroed(2, 4);
            if (p == null) return 1;
            if (p[0] != 0 || p[1] != 0) return 2;
            std::dealloc(p);
            return 0;
        }
    )", "mem_zeroed.c"), 0);
}

TEST_F(StdMemTest, ResizePreservesData) {
    EXPECT_EQ(runWithImports(R"(
        import std.mem;
        int32 main() {
            int32* p = (int32*)std::alloc(4);
            if (p == null) return 1;
            p[0] = 42;
            int32* q = (int32*)std::resize(p, 64);
            if (q == null) return 2;
            if (q[0] != 42) return 3;
            std::dealloc(q);
            return 0;
        }
    )", "mem_resize.c"), 0);
}

TEST_F(StdMemTest, AlignUpDown) {
    EXPECT_EQ(runWithImports(R"(
        import std.mem;
        int32 main() {
            if (std::align_up(15, 8) != 16) return 1;
            if (std::align_up(16, 8) != 16) return 2;
            if (std::align_down(15, 8) != 8) return 3;
            if (std::align_down(8, 8) != 8) return 4;
            return 0;
        }
    )", "mem_alignup.c"), 0);
}

TEST_F(StdMemTest, IsAligned) {
    EXPECT_EQ(runWithImports(R"(
        import std.mem;
        int32 main() {
            void* p = std::aligned_alloc(16, 32);
            if (p == null) return 1;
            if (!std::is_aligned(p, 16)) return 2;
            // 偏移 1 字节后不再 16 对齐。
            void* off = (void*)((usize)p + 1);
            if (std::is_aligned(off, 16)) return 3;
            std::aligned_free(p);
            return 0;
        }
    )", "mem_isaligned.c"), 0);
}

TEST_F(StdMemTest, AlignedAllocIsAligned) {
    EXPECT_EQ(runWithImports(R"(
        import std.mem;
        int32 main() {
            void* p = std::aligned_alloc(64, 128);
            if (p == null) return 1;
            if ((usize)p % 64 != 0) return 2;
            std::aligned_free(p);
            return 0;
        }
    )", "mem_alignalloc.c"), 0);
}

TEST_F(StdMemTest, AlignedAllocFailsOnBadAlignment) {
    EXPECT_EQ(runWithImports(R"(
        import std.mem;
        int32 main() {
            // 非 2 的幂对齐：posix_memalign 返回 EINVAL → aligned_alloc 返回 null。
            void* p = std::aligned_alloc(24, 128);
            return p == null ? 0 : 1;
        }
    )", "mem_alignfail.c"), 0);
}

}  // namespace

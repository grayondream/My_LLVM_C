// P1-08 / CG-10: inline asm 从源码到宿主 x86-64 LLJIT 可执行。
#include <gtest/gtest.h>

#include "codegen/CodegenContext.h"
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

class InlineAsmE2E : public ::testing::Test {
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

static int runSource(const std::string& source, const std::string& file = "inline_asm.c") {
    Lexer lexer(file, source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast || !parser.getErrors().empty()) return -1;

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    if (!analyzer.getErrors().empty()) return -1;

    CodegenContext ctx;
    ctx.setSourceFile(file);
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

TEST_F(InlineAsmE2E, NoOpVolatile) {
    EXPECT_EQ(runSource("int32 main() { asm volatile(\"nop\"); return 0; }"), 0);
}

TEST_F(InlineAsmE2E, OutputRegister) {
    EXPECT_EQ(runSource("int32 main() { int32 out; asm volatile(\"mov $7, %0\" : \"=r\"(out)); return out; }"), 7);
}

TEST_F(InlineAsmE2E, ReadWriteOperand) {
    EXPECT_EQ(runSource("int32 main() { int32 x = 5; int32 y = 3; asm(\"add %1, %0\" : \"+r\"(x) : \"r\"(y)); return x; }"), 8);
}

TEST_F(InlineAsmE2E, MemoryClobber) {
    EXPECT_EQ(runSource("int32 main() { asm volatile(\"\" : : : \"memory\"); return 0; }"), 0);
}

TEST_F(InlineAsmE2E, PercentEscape) {
    EXPECT_EQ(runSource("int32 main() { int32 out = 0; asm(\"mov %%eax, %0\" : \"=r\"(out)); return out == out; }"), 1);
}

TEST_F(InlineAsmE2E, MultipleOutputs) {
    EXPECT_EQ(runSource("int32 main() { int32 a; int32 b; asm(\"mov $1, %0; mov $2, %1\" : \"=r\"(a), \"=r\"(b)); return a * 10 + b; }"), 12);
}

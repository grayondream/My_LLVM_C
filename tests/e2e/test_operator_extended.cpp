// SEM-17: binary operator overload set convergence — end-to-end tests.
// The declaration side already parses `%` `&&` `||` `&` `|` `^` `<<` `>>`;
// these tests pin the CALL side (sema lookup + codegen call). Overloaded
// `&&`/`||` are ordinary calls (both operands evaluated — no short-circuit),
// per C++ semantics.
// fixture 仿 test_overloading.cpp 的 runSource。

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

class OperatorExtendedE2E : public ::testing::Test {
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
    if (llvm::verifyModule(ctx.getModule(), &vs)) return -1;

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

TEST_F(OperatorExtendedE2E, OperatorPercent) {
    EXPECT_EQ(runSource(R"(
        struct V { int32 x; };
        struct V operator%(struct V a, struct V b) {
            struct V r;
            r.x = a.x % b.x;
            return r;
        }
        int32 main() {
            struct V a; a.x = 7;
            struct V b; b.x = 4;
            struct V r = a % b;
            return r.x - 3;
        }
    )", "op_percent.c"), 0);
}

TEST_F(OperatorExtendedE2E, OperatorShiftLeft) {
    EXPECT_EQ(runSource(R"(
        struct V { int32 x; };
        struct V operator<<(struct V a, int32 n) {
            struct V r;
            r.x = a.x << n;
            return r;
        }
        int32 main() {
            struct V a; a.x = 1;
            struct V r = a << 3;
            return r.x - 8;
        }
    )", "op_shl.c"), 0);
}

TEST_F(OperatorExtendedE2E, OperatorShiftRight) {
    EXPECT_EQ(runSource(R"(
        struct V { int32 x; };
        struct V operator>>(struct V a, int32 n) {
            struct V r;
            r.x = a.x >> n;
            return r;
        }
        int32 main() {
            struct V a; a.x = 16;
            struct V r = a >> 2;
            return r.x - 4;
        }
    )", "op_shr.c"), 0);
}

TEST_F(OperatorExtendedE2E, OperatorBitwiseAndOrXor) {
    EXPECT_EQ(runSource(R"(
        struct V { int32 x; };
        struct V operator&(struct V a, struct V b) {
            struct V r; r.x = a.x & b.x; return r;
        }
        struct V operator|(struct V a, struct V b) {
            struct V r; r.x = a.x | b.x; return r;
        }
        struct V operator^(struct V a, struct V b) {
            struct V r; r.x = a.x ^ b.x; return r;
        }
        int32 main() {
            struct V a; a.x = 6;
            struct V b; b.x = 3;
            struct V andv = a & b;
            struct V orv  = a | b;
            struct V xorv = a ^ b;
            return (andv.x - 2) + (orv.x - 7) + (xorv.x - 5);
        }
    )", "op_bitwise.c"), 0);
}

TEST_F(OperatorExtendedE2E, OperatorLogicalAndEvaluatesBothOperands) {
    EXPECT_EQ(runSource(R"(
        int32 g = 0;
        struct V { int32 x; };
        bool operator&&(struct V a, struct V b) { return true; }
        int32 main() {
            struct V a; a.x = 0;
            struct V b; b.x = 1;
            bool r = a && (g = g + 1, b);
            if (!r) { return 1; }
            return g - 1;
        }
    )", "op_logical_and.c"), 0);
}

TEST_F(OperatorExtendedE2E, OperatorLogicalOrEvaluatesBothOperands) {
    EXPECT_EQ(runSource(R"(
        int32 g = 0;
        struct V { int32 x; };
        bool operator||(struct V a, struct V b) { return false; }
        int32 main() {
            struct V a; a.x = 1;
            struct V b; b.x = 1;
            bool r = a || (g = g + 1, b);
            if (r) { return 1; }
            return g - 1;
        }
    )", "op_logical_or.c"), 0);
}

TEST_F(OperatorExtendedE2E, BuiltinBinaryStillWorks) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32 a = 7;
            int32 b = 4;
            if (a % b != 3) { return 1; }
            if ((a & b) != 4) { return 2; }
            if ((a | b) != 7) { return 3; }
            if ((a ^ b) != 3) { return 4; }
            if ((1 << 3) != 8) { return 5; }
            if ((16 >> 2) != 4) { return 6; }
            bool t = true && false;
            if (t) { return 7; }
            return 0;
        }
    )", "op_builtin_binary.c"), 0);
}

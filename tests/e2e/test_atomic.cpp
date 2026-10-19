// CON-01: atomic type qualifier — integer scalars, atomicrmw subset.
// end-to-end execution tests. fixture 仿 test_string.cpp 的 runSource。

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

class AtomicE2E : public ::testing::Test {
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
    if (!ast) {
        ADD_FAILURE() << "Parse failed";
        return -1;
    }

    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    if (!analyzer.getErrors().empty()) {
        for (auto& err : analyzer.getErrors()) {
            ADD_FAILURE() << "Semantic error: " << err.format();
        }
        return -1;
    }

    CodegenContext ctx;
    ctx.setSourceFile(filename);
    ast->codegen(ctx);
    ctx.finalizeDebugInfo();

    std::string ve;
    llvm::raw_string_ostream vs(ve);
    bool bad = llvm::verifyModule(ctx.getModule(), &vs);
    if (bad) {
        vs.flush();
        ADD_FAILURE() << "Module verification failed: " << ve;
        return -1;
    }

    auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!jtmb) {
        ADD_FAILURE() << "JITTargetMachineBuilder::detectHost failed";
        return -1;
    }
    auto jit = llvm::orc::LLJITBuilder()
        .setJITTargetMachineBuilder(std::move(*jtmb))
        .create();
    if (!jit) {
        ADD_FAILURE() << "LLJITBuilder::create failed";
        return -1;
    }

    auto ts = llvm::orc::ThreadSafeModule(ctx.takeModule(), ctx.takeContext());
    if (auto err = (*jit)->addIRModule(std::move(ts))) {
        ADD_FAILURE() << "addIRModule failed";
        return -1;
    }

    auto sym = (*jit)->lookup("main");
    if (!sym) {
        ADD_FAILURE() << "main not found";
        return -1;
    }
    return ((int (*)())(intptr_t)sym->getValue())();
}

TEST_F(AtomicE2E, AtomicLoadStoreWorks) {
    EXPECT_EQ(runSource(R"(
        atomic int32 a = 0;
        int32 main() {
            a = a + 1;
            a = a + 1;
            return a == 2 ? 0 : 1;
        }
    )", "atomic_ls.c"), 0);
}

TEST_F(AtomicE2E, AtomicIncrement) {
    EXPECT_EQ(runSource(R"(
        atomic int32 c = 0;
        int32 main() {
            c++;
            ++c;
            return c == 2 ? 0 : 1;
        }
    )", "atomic_inc.c"), 0);
}

TEST_F(AtomicE2E, AtomicCompoundAssign) {
    EXPECT_EQ(runSource(R"(
        atomic int32 v = 7;
        int32 main() {
            v += 3;
            v -= 2;
            v &= 15;
            v |= 32;
            v ^= 1;
            return v == (7+3-2)&15|32^1 ? 0 : 1;
        }
    )", "atomic_ca.c"), 0);
}

TEST_F(AtomicE2E, AtomicPointerRejected) {
    auto ast = [&]() {
        Lexer lexer("at_ptr.c", R"(
            atomic int32* p;
            int32 main() { return 0; }
        )");
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        return parser.parse();
    }();
    if (!ast) {
        ADD_FAILURE() << "Parse failed";
        return;
    }
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_FALSE(analyzer.getErrors().empty());
    EXPECT_NE(analyzer.getErrors()[0].message.find("atomic"),
              std::string::npos);
}

TEST_F(AtomicE2E, AtomicMultiplyRejected) {
    auto ast = [&]() {
        Lexer lexer("at_mul.c", R"(
            atomic int32 a = 2;
            int32 main() { a *= 3; return 0; }
        )");
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        return parser.parse();
    }();
    if (!ast) {
        ADD_FAILURE() << "Parse failed";
        return;
    }
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_FALSE(analyzer.getErrors().empty());
    EXPECT_NE(analyzer.getErrors()[0].message.find("atomic"),
              std::string::npos);
}

// C1 (review): `atomic` on a typedef base strips to the underlying scalar and
// works (must not be silently dropped nor crash).
TEST_F(AtomicE2E, AtomicTypedefBaseWorks) {
    EXPECT_EQ(runSource(R"(
        typedef int32 myint;
        atomic myint x = 5;
        int32 main() {
            x = x + 1;
            return x == 6 ? 0 : 1;
        }
    )", "at_typedef.c"), 0);
}

// I2 (review): `atomic` works in a function-local declaration too (was
// silently dropped because isTypeStart didn't recognize the qualifier).
TEST_F(AtomicE2E, AtomicLocalDeclarationWorks) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            atomic int32 a = 1;
            a = a + 1;
            return a == 2 ? 0 : 1;
        }
    )", "at_local.c"), 0);
}

// C2 (review): `atomic` on an aggregate is rejected cleanly (parse-time), not
// a crash.
TEST_F(AtomicE2E, AtomicAggregateRejectedCleanly) {
    auto tokens = [&]() {
        Lexer lexer("at_agg.c", R"(
            struct S { int32 a; };
            atomic struct S f;
            int32 main() { return 0; }
        )");
        return lexer.tokenize();
    }();
    Parser parser(tokens);
    parser.parse();
    bool parserError = !parser.getErrors().empty();
    EXPECT_TRUE(parserError);
    if (parserError) {
        EXPECT_NE(parser.getErrors()[0].message.find("atomic"),
                  std::string::npos);
    }
}

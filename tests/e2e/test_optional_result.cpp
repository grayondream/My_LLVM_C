// P1-02 (TYP-13/14): Optional/Result 端到端执行测试。
// fixture 仿 test_class_codegen.cpp 的 ClassCodegenE2E::runSource（完整
// JIT 执行，返回 main 的返回值）。

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

class OptionalResultE2E : public ::testing::Test {
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
    if (auto e = (*jit)->addIRModule(std::move(ts))) {
        ADD_FAILURE() << "addIRModule failed";
        return -1;
    }

    auto sym = (*jit)->lookup("main");
    if (!sym) {
        ADD_FAILURE() << "lookup main failed";
        return -1;
    }

    auto fn = (int (*)())(intptr_t)sym->getValue();
    return fn();
}

TEST_F(OptionalResultE2E, OptionalBranchExec) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32? o = {true, 42};
            if (o.valid) { return o.value; }
            return -1;
        }
    )", "optres1.c"), 42);
}

TEST_F(OptionalResultE2E, OptionalRvalueMember) {
    // Review Focus #3：函数返回的 rvalue Optional 直接取 .value。
    EXPECT_EQ(runSource(R"(
        int32? make(int32 flag) { int32? t = {flag != 0, 7}; return t; }
        int32 main() { return make(1).value; }
    )", "optres2.c"), 7);
}

TEST_F(OptionalResultE2E, ResultErrorPath) {
    EXPECT_EQ(runSource(R"(
        Result<int32, int32> divide(int32 a, int32 b) {
            if (b == 0) { Result<int32, int32> e = {false, 0, -1}; return e; }
            Result<int32, int32> s = {true, a / b, 0};
            return s;
        }
        int32 main() {
            Result<int32, int32> r = divide(10, 2);
            if (r.ok) { return r.value; }
            return r.error;
        }
    )", "optres3.c"), 5);
}

TEST_F(OptionalResultE2E, ResultErrorValue) {
    EXPECT_EQ(runSource(R"(
        Result<int32, int32> divide(int32 a, int32 b) {
            if (b == 0) { Result<int32, int32> e = {false, 0, -1}; return e; }
            Result<int32, int32> s = {true, a / b, 0};
            return s;
        }
        int32 main() {
            Result<int32, int32> r = divide(10, 0);
            if (r.ok) { return r.value; }
            return r.error;
        }
    )", "optres4.c"), -1);
}

TEST_F(OptionalResultE2E, OptionalParamAndCopy) {
    EXPECT_EQ(runSource(R"(
        int32 pick(int32? o) { if (o.valid) { return o.value; } return 0; }
        int32 main() {
            int32? a = {true, 11};
            int32? b = a;
            b.value = 13;
            return pick(a) * 100 + pick(b);
        }
    )", "optres5.c"), 1113);
}

TEST_F(OptionalResultE2E, GlobalZeroInit) {
    // Review Focus #5：全局零初始化 valid=false。
    EXPECT_EQ(runSource(R"(
        int32? g;
        Result<int32, int32> gr;
        int32 main() {
            if (g.valid || gr.ok) { return -1; }
            return 1;
        }
    )", "optres6.c"), 1);
}

TEST_F(OptionalResultE2E, NestedOptional) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            Optional<Optional<int32>> o = {true, {false, 0}};
            if (o.valid) { if (o.value.valid) { return o.value.value; } return 2; }
            return 3;
        }
    )", "optres7.c"), 2);
}

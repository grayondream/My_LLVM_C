// I4 收口（spec: docs/superpowers/specs/2026-10-11-string-ownership-design.md）：
// string 拥有型值语义端到端执行测试。拷贝模型 B（D2）——活值深拷贝、新鲜值
// 移动；`b = a` 赋值经 getStringAssignFn（D7，自动释放旧缓冲）。
// fixture 仿 test_string.cpp 的 StringE2E runSource（完整 JIT 执行，返回 main 的返回值）。

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

class StringOwnershipE2E : public ::testing::Test {
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

// I4 (D7): `b = a`（string 赋值）必须深拷贝——`a`/`b` 各自独立缓冲，
// 双双 destroy 不得双 free abort（当前浅拷贝 → 共享缓冲 → 双 free）。
TEST_F(StringOwnershipE2E, AssignCopyIndependent) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string a = string.new("y");
            string b = string.new("x");
            b = a;
            a.destroy();
            b.destroy();
            return 0;
        }
    )", "own_assign.c"), 0);
}

// I4 (D7, 自赋值守卫): `b = b;` 不得 free 共享缓冲（双 free abort）——
// getStringAssignFn 在 dst.ptr == src.ptr 时什么都不做。
TEST_F(StringOwnershipE2E, SelfAssignSafe) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string b = string.new("x");
            b = b;
            string_view v = b;
            if (v != "x") { return 1; }
            b.destroy();
            return 0;
        }
    )", "own_self.c"), 0);
}

// I4 (D7): `b = a;` 后 b 持有 a 的深拷贝（值正确、len 正确），a 仍有效
// 可 destroy——旧缓冲被赋值 helper 自动释放。
TEST_F(StringOwnershipE2E, AssignFreesOldBuffer) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string a = string.new("abc");
            string b = string.new("zzzzzz");
            b = a;
            a.destroy();
            string_view v = b;
            if (v != "abc") { return 1; }
            if (b.len() != 3) { return 2; }
            b.destroy();
            return 0;
        }
    )", "own_frees_old.c"), 0);
}

// I4 (D7, fresh 赋值): `b = string.new("y");` 覆盖已初始化 b——移动（free
// 旧缓冲 + 裸 store），值正确且无泄漏/双 free。
TEST_F(StringOwnershipE2E, AssignFreshValueFreesOld) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string b = string.new("x");
            b = string.new("y");
            string_view v = b;
            if (v != "y") { return 1; }
            b.destroy();
            return 0;
        }
    )", "own_fresh.c"), 0);
}

// P1-06 (FMT-01/02/03): str / string 端到端执行测试。
// fixture 仿 test_optional_result.cpp 的 runSource（完整 JIT 执行，返回 main 的返回值）。

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

class StringE2E : public ::testing::Test {
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

// ---- T2: 字面量 = str，基础互操作 ----

TEST_F(StringE2E, LiteralToCharPointer) {
    // FMT-03: str -> char* 隐式（字节视图，放弃 UTF-8 保证与长度）。
    EXPECT_EQ(runSource(R"(
        int32 main() {
            str s = "hi";
            char* p = s;
            if (p[0] != 'h') { return 1; }
            if (p[1] != 'i') { return 2; }
            return 0;
        }
    )", "str1.c"), 0);
}

TEST_F(StringE2E, PrintStrArg) {
    // print/println 的 str 实参（%.*s 路径）可编译执行。
    EXPECT_EQ(runSource(R"(
        int32 main() {
            println("{}", "lit");
            print("{}x", "ab");
            return 0;
        }
    )", "str2.c"), 0);
}

TEST_F(StringE2E, StrCompareRuntime) {
    // == / != 走运行时 memcmp 路径（非常量、经函数返回的 str）。
    // 注意：bool 字面量直接作实参是既有解析局限（与 str 无关），经变量传递。
    EXPECT_EQ(runSource(R"(
        str pick(bool which) {
            if (which) { return "abc"; }
            return "abd";
        }
        int32 main() {
            bool t = true;
            bool f = false;
            str a = pick(t);
            str b = pick(f);
            str c = pick(t);
            if (a == b) { return 1; }
            if (a != c) { return 2; }
            if (a == c) { return 0; }
            return 3;
        }
    )", "str3.c"), 0);
}

// ---- T3: str 内建方法与 str_from_c ----

TEST_F(StringE2E, StrLen) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            str s = "hello";
            if (s.len() != 5) { return 1; }
            return 0;
        }
    )", "str4.c"), 0);
}

TEST_F(StringE2E, StrCharCount) {
    // "héllo" = 6 bytes / 5 code points (é is 2 bytes).
    EXPECT_EQ(runSource(R"(
        int32 main() {
            str s = "h\xc3\xa9llo";
            if (s.len() != 6) { return 1; }
            if (s.char_count() != 5) { return 2; }
            return 0;
        }
    )", "str5.c"), 0);
}

TEST_F(StringE2E, StrCharAt) {
    // Byte indexing: byte 1 of "héllo" is the lead byte 0xC3.
    EXPECT_EQ(runSource(R"(
        int32 main() {
            str s = "h\xc3\xa9llo";
            if (s.char_at(1) != '\xC3') { return 1; }
            return 0;
        }
    )", "str6.c"), 0);
}

TEST_F(StringE2E, StrCharLenAt) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            str s = "h\xc3\xa9llo";
            if (s.char_len_at(0) != 1) { return 1; }
            if (s.char_len_at(1) != 2) { return 2; }
            return 0;
        }
    )", "str7.c"), 0);
}

TEST_F(StringE2E, StrFromCExec) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            char* p = "abc";
            str s = str_from_c(p);
            if (s.len() != 3) { return 1; }
            return 0;
        }
    )", "str8.c"), 0);
}

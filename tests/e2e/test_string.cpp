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

// ---- T4: string 内存与方法 ----

TEST_F(StringE2E, NewLenExec) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("hello");
            if (s.len() != 5) { return 1; }
            return 0;
        }
    )", "str9.c"), 0);
}

TEST_F(StringE2E, AppendExec) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("ab");
            s.append("cd");
            if (s.len() != 4) { return 1; }
            if (s[2] != 'c') { return 2; }
            return 0;
        }
    )", "str10.c"), 0);
}

TEST_F(StringE2E, AppendGrowthExec) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("");
            int32 i = 0;
            while (i < 100) {
                s.append("ab");
                i = i + 1;
            }
            if (s.len() != 200) { return 1; }
            if (s[198] != 'a' || s[199] != 'b') { return 2; }
            return 0;
        }
    )", "str11.c"), 0);
}

TEST_F(StringE2E, PushExec) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("");
            s.push('x');
            s.push('y');
            s.push('z');
            if (s.len() != 3) { return 1; }
            if (s[2] != 'z') { return 2; }
            return 0;
        }
    )", "str12.c"), 0);
}

TEST_F(StringE2E, CapacityExec) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("hi");
            if (s.capacity() < 2) { return 1; }
            s.append("abc");
            if (s.len() != 5) { return 2; }
            if (s.capacity() < 5) { return 3; }
            return 0;
        }
    )", "str13.c"), 0);
}

TEST_F(StringE2E, DestroyFrees) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("data");
            s.destroy();
            if (s.len() != 0) { return 1; }
            if (s.capacity() != 0) { return 2; }
            return 0;
        }
    )", "str14.c"), 0);
}

TEST_F(StringE2E, StringToStrView) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("ab");
            str v = s;
            if (v.len() != 2) { return 1; }
            return 0;
        }
    )", "str15.c"), 0);
}

TEST_F(StringE2E, AppendAcceptsString) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string a = string.new("x");
            string b = string.new("y");
            a.append(b);
            if (a.len() != 2) { return 1; }
            if (a[1] != 'y') { return 2; }
            return 0;
        }
    )", "str16.c"), 0);
}

TEST_F(StringE2E, SubscriptExec) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("abc");
            if (s[0] != 'a') { return 1; }
            if (s[1] != 'b') { return 2; }
            if (s[2] != 'c') { return 3; }
            return 0;
        }
    )", "str17.c"), 0);
}

// P1-06 / T5: str_from_c over a runtime buffer validates UTF-8 and panics on
// invalid input (E2015's runtime companion). Panic terminates, so we pin the
// IR shape instead of executing it.
static std::string compileStringIR(const std::string& source, const std::string& filename) {
    Lexer lexer(filename, source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast) return "";
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    if (!analyzer.getErrors().empty()) return "";
    CodegenContext ctx;
    ctx.setSourceFile(filename);
    ast->codegen(ctx);
    std::string ir;
    llvm::raw_string_ostream os(ir);
    ctx.getModule().print(os, nullptr);
    return ir;
}

TEST_F(StringE2E, StrFromCRuntimeValidates) {
    std::string ir = compileStringIR(R"(
        int32 fill(char* buf) {
            buf[0] = 'a';
            buf[1] = static_cast<char>(0xFF);
            return 2;
        }
        int32 main() {
            char buf[4];
            buf[2] = 0;
            fill(buf);
            str s = str_from_c(buf);
            return 0;
        }
    )", "str18.c");
    ASSERT_FALSE(ir.empty());
    EXPECT_NE(ir.find("smc.utf8.validate"), std::string::npos);
}

TEST_F(StringE2E, StrFromCRuntimeValidExec) {
    // Valid runtime buffer: strlen path, no panic.
    EXPECT_EQ(runSource(R"(
        int32 main() {
            char buf[4];
            buf[0] = 'a';
            buf[1] = 'b';
            buf[2] = 'c';
            buf[3] = 0;
            str s = str_from_c(buf);
            if (s.len() != 3) { return 1; }
            return 0;
        }
    )", "str19.c"), 0);
}

// ---- T6: 硬化与边界（Review Focus pin）----

TEST_F(StringE2E, DoubleDestroySafe) {
    // destroy() 清零字段 → 第二次 destroy 走 free(NULL)，不崩。
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("data");
            s.destroy();
            s.destroy();
            if (s.len() != 0) { return 1; }
            if (s.capacity() != 0) { return 2; }
            return 0;
        }
    )", "str21.c"), 0);
}

TEST_F(StringE2E, EmptyStringInvariants) {
    // 空串：len 0、cap >= 1（ptr 保证非空，str→char* 视图安全）。
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("");
            if (s.len() != 0) { return 1; }
            if (s.capacity() < 1) { return 2; }
            char* p = s;
            // malloc 不清零：p[0] 内容未定义。不变量 = p 非空（cap>=1 保证）。
            if (p == null) { return 3; }
            return 0;
        }
    )", "str22.c"), 0);
}

TEST_F(StringE2E, SubscriptCompilesWithoutBoundsCheck) {
    // DEC-06 未决：不加运行时边界检查。只编译执行合法下标；
    // 越界行为本测试不执行（结果未定义，DEC-06 落地时另行裁决）。
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string s = string.new("abc");
            if (s[2] != 'c') { return 1; }
            return 0;
        }
    )", "str23.c"), 0);
}

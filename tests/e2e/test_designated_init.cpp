// Designated initializers (full C99) — local codegen e2e (PAR-13).
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

class DesignatedInitE2E : public ::testing::Test {
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
    if (!ast || !parser.getErrors().empty()) return -1;

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
        .setJITTargetMachineBuilder(std::move(*jtmb)).create();
    if (!jit) return -1;
    if (auto e = (*jit)->addIRModule(
            llvm::orc::ThreadSafeModule(ctx.takeModule(), ctx.takeContext()))) {
        return -1;
    }
    auto sym = (*jit)->lookup("main");
    if (!sym) return -1;
    auto mainFn = sym->toPtr<int32_t(*)()>();
    return mainFn();
}

TEST_F(DesignatedInitE2E, LocalStruct) {
    EXPECT_EQ(runSource(R"(
        struct Point { int32 x; int32 y; };
        int32 main() {
            Point p = { .y = 5 };        // x 补零
            if (p.x != 0 || p.y != 5) { return 1; }
            Point q = { .x = 1, 2 };     // 混排：y==2
            if (q.x != 1 || q.y != 2) { return 2; }
            Point r = { .y = 9, .x = 3, .y = 4 };  // 乱序+重复：后覆盖
            if (r.x != 3 || r.y != 4) { return 3; }
            return 0;
        }
    )", "di_local_struct.c"), 0);
}

TEST_F(DesignatedInitE2E, LocalPartialPositionalZeroFills) {
    EXPECT_EQ(runSource(R"(
        struct Point { int32 x; int32 y; };
        int32 main() {
            Point p = { 1 };             // y 补零（既有缺口）
            if (p.x != 1 || p.y != 0) { return 1; }
            return 0;
        }
    )", "di_partial_zero.c"), 0);
}

TEST_F(DesignatedInitE2E, LocalArrayDesignators) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32 a[3] = { [1] = 7 };
            if (a[0] != 0 || a[1] != 7 || a[2] != 0) { return 1; }
            int32 b[] = { [2] = 5, 6 };  // 推断大小 4，b[3]==6
            if (b[2] != 5 || b[3] != 6) { return 2; }
            if (sizeof(b) != 16) { return 3; }
            return 0;
        }
    )", "di_local_array.c"), 0);
}

TEST_F(DesignatedInitE2E, LocalNestedAndBitfieldAndBase) {
    EXPECT_EQ(runSource(R"(
        struct Inner { int32 a; int32 b; };
        struct Outer { int32 tag; Inner inner; };
        struct BF { int32 a; int32 b : 3; int32 c : 5; };
        struct Base { int32 baseField; };
        struct Derived : Base { int32 own; };
        int32 main() {
            Outer o = { .inner = { .b = 9 } };   // 嵌套；inner.a 补零
            if (o.tag != 0 || o.inner.a != 0 || o.inner.b != 9) { return 1; }
            BF bf = { .a = 1, .c = 5 };          // 位域设计符
            if (bf.a != 1 || bf.c != 5) { return 2; }
            Derived d = { .baseField = 7, .own = 3 };  // 基类字段设计符
            if (d.baseField != 7 || d.own != 3) { return 3; }
            return 0;
        }
    )", "di_local_nested.c"), 0);
}

TEST_F(DesignatedInitE2E, LocalUnionDesignator) {
    EXPECT_EQ(runSource(R"(
        union U { int32 i; float32 f; };
        int32 main() {
            U u = { .f = 2.5f };         // 写槽 0，按指名成员类型
            if (u.f != 2.5f) { return 1; }
            U v = { .i = 42 };
            if (v.i != 42) { return 2; }
            return 0;
        }
    )", "di_local_union.c"), 0);
}

TEST_F(DesignatedInitE2E, GlobalStruct) {
    EXPECT_EQ(runSource(R"(
        struct Point { int32 x; int32 y; };
        struct Point g = { .y = 5 };
        struct Point h = { .x = 1, 2 };
        int32 main() {
            if (g.x != 0 || g.y != 5) { return 1; }
            if (h.x != 1 || h.y != 2) { return 2; }
            return 0;
        }
    )", "di_global_struct.c"), 0);
}

TEST_F(DesignatedInitE2E, GlobalArrayAndBitfield) {
    EXPECT_EQ(runSource(R"(
        struct BF { int32 a : 3; int32 b : 5; };
        struct BF gbf = { .b = 7 };
        int32 ga[4] = { [2] = 9 };
        int32 main() {
            if (gbf.b != 7) { return 1; }
            if (ga[2] != 9 || ga[0] != 0) { return 2; }
            return 0;
        }
    )", "di_global_bf.c"), 0);
}
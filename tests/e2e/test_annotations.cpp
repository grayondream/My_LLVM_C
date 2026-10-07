// P1-05 / ANN-02/03: 布局注解一致性矩阵——size_of/offset_of/align_of 值
// 即 IR 布局事实（LayoutBuilder 单源）。

#include <gtest/gtest.h>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"

#include <spdlog/spdlog.h>

class AnnotationLayoutE2E : public ::testing::Test {
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

    // 完整管线：main 返回值即断言（size_of/offset_of/align_of 经 CT 求值，
    // 与 IR 布局同源——LayoutBuilder 单源）。
    int runSource(const std::string& source, const std::string& filename) {
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
                ADD_FAILURE() << "Semantic error: " << err.message;
            }
            return -1;
        }
        CodegenContext ctx;
        ctx.setSourceFile(filename);
        ast->codegen(ctx);
        std::string ve;
        llvm::raw_string_ostream vs(ve);
        if (llvm::verifyModule(ctx.getModule(), &vs)) {
            vs.flush();
            ADD_FAILURE() << "Module verification failed: " << ve;
            return -1;
        }
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
        auto fn = (int (*)())(intptr_t)sym->getValue();
        return fn();
    }
};

TEST_F(AnnotationLayoutE2E, PackedStructE2E) {
    EXPECT_EQ(runSource(R"(
[[packed]] struct P { int32 a; int16 b; }
int32 main() {
    return (compile_time.size_of(P) == 6 && compile_time.offset_of(P, "b") == 4) ? 1 : 0;
}
)", "ann_packed.c"), 1);
}

TEST_F(AnnotationLayoutE2E, AlignStructE2E) {
    EXPECT_EQ(runSource(R"(
[[align(64)]] struct A { int32 x; }
int32 main() {
    return (compile_time.size_of(A) == 64 && compile_time.align_of(A) == 64) ? 1 : 0;
}
)", "ann_align.c"), 1);
}

TEST_F(AnnotationLayoutE2E, FieldAlignE2E) {
    EXPECT_EQ(runSource(R"(
struct F { int8 a; [[align(16)]] int32 b; }
int32 main() {
    return (compile_time.offset_of(F, "b") == 16 && compile_time.size_of(F) == 32) ? 1 : 0;
}
)", "ann_field_align.c"), 1);
}

TEST_F(AnnotationLayoutE2E, PackedAlignComboE2E) {
    EXPECT_EQ(runSource(R"(
[[packed]] [[align(8)]] struct PA { int32 a; int16 b; }
int32 main() {
    return (compile_time.size_of(PA) == 8 && compile_time.offset_of(PA, "b") == 4) ? 1 : 0;
}
)", "ann_packed_align.c"), 1);
}

TEST_F(AnnotationLayoutE2E, ReprCPinE2E) {
    EXPECT_EQ(runSource(R"(
[[repr(C)]] struct R { int32 a; float64 b; }
int32 main() {
    return (compile_time.size_of(R) == 16 && compile_time.offset_of(R, "b") == 8) ? 1 : 0;
}
)", "ann_repr.c"), 1);
}

TEST_F(AnnotationLayoutE2E, UnionAlignE2E) {
    EXPECT_EQ(runSource(R"(
[[align(16)]] union U { int32 i; }
int32 main() {
    return (compile_time.size_of(U) == 16) ? 1 : 0;
}
)", "ann_union_align.c"), 1);
}

TEST_F(AnnotationLayoutE2E, GlobalVarAlignIR) {
    // 全局变量 align(64) → IR alignment 属性。
    Lexer lexer("ann_gvar_align.c", "[[align(64)]] int32 g;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    SemanticAnalyzer analyzer;
    analyzer.analyze(*ast);
    ASSERT_TRUE(analyzer.getErrors().empty());
    CodegenContext ctx;
    ctx.setSourceFile("ann_gvar_align.c");
    ast->codegen(ctx);
    bool found = false;
    for (auto& gv : ctx.getModule().globals()) {
        if (gv.getName() == "g" && gv.getAlignment() >= 64) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

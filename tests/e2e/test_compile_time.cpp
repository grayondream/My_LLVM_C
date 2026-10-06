// P1-04 / CT-04/05/12: 目标/构建查询端到端 + LLVM 常量集成（零运行时指令）。

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

class CompileTimeE2E : public ::testing::Test {
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

// 完整管线：parse → sema（buildDebug 注入）→ codegen → JIT 执行 main。
static int runSourceImpl(const std::string& source, const std::string& filename,
                         bool buildDebug) {
    Lexer lexer(filename, source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    if (!ast) {
        ADD_FAILURE() << "Parse failed";
        return -1;
    }

    SemanticAnalyzer analyzer;
    analyzer.setBuildConfig(buildDebug, "O0");
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

static int runSource(const std::string& source, const std::string& filename) {
    return runSourceImpl(source, filename, false);
}

TEST_F(CompileTimeE2E, TargetOsIsLinux) {
    // host 上 target.os 必为 "linux"（本测试环境）；== 折叠为编译期常量。
    int r = runSource(R"(
constexpr int32 isLinux = compile_time.target.os == "linux";
int32 main() { return isLinux ? 1 : 0; }
)", "ct_target_os.c");
    EXPECT_EQ(r, 1);
}

TEST_F(CompileTimeE2E, BuildDebugSetter) {
    const char* src = R"(
int32 main() { bool d = compile_time.build.debug; return d ? 1 : 0; }
)";
    EXPECT_EQ(runSourceImpl(src, "ct_debug_true.c", true), 1);
    EXPECT_EQ(runSourceImpl(src, "ct_debug_false.c", false), 0);
}

// ---- P1-04 / CT-06: 布局查询（size_of/align_of/offset_of）----

TEST_F(CompileTimeE2E, SizeOfScalars) {
    int r = runSource(R"(
constexpr usize s32 = compile_time.size_of(int32);
constexpr usize s64 = compile_time.size_of(float64);
constexpr usize s8 = compile_time.size_of(int8);
constexpr usize sb = compile_time.size_of(bool);
constexpr usize sp = compile_time.size_of(int32*);
int32 main() {
    return (s32 == 4 && s64 == 8 && s8 == 1 && sb == 1 && sp == 8) ? 1 : 0;
}
)", "ct_sizeof_scalars.c");
    EXPECT_EQ(r, 1);
}

TEST_F(CompileTimeE2E, SizeOfClassWithBase) {
    // Review Focus 4: 基类子对象占槽 0，布局须与 codegen DataLayout 一致。
    int r = runSource(R"(
class B { public: int32 b; }
class D : B { public: float64 v; }
int32 main() {
    return (compile_time.size_of(D) == 16 && compile_time.offset_of(D, "v") == 8) ? 1 : 0;
}
)", "ct_sizeof_base.c");
    EXPECT_EQ(r, 1);
}

TEST_F(CompileTimeE2E, SizeOfNestedStructUnion) {
    int r = runSource(R"(
struct Inner { int32 a; float64 b; }
union U { int32 i; float64 f; }
struct Outer { Inner in; U u; int16 c; }
int32 main() {
    return (compile_time.size_of(Outer) == 32
         && compile_time.offset_of(Outer, "u") == 16
         && compile_time.offset_of(Outer, "c") == 24
         && compile_time.size_of(U) == 8
         && compile_time.align_of(Outer) == 8) ? 1 : 0;
}
)", "ct_sizeof_nested.c");
    EXPECT_EQ(r, 1);
}

TEST_F(CompileTimeE2E, ConstVarFromCompileTime) {
    int r = runSource(R"(
constexpr usize N = compile_time.size_of(float64);
int32 main() { return N == 8 ? 1 : 0; }
)", "ct_constvar.c");
    EXPECT_EQ(r, 1);
}

TEST_F(CompileTimeE2E, ConstExprFnInCompileTime) {
    // Review Focus 3: constexpr 函数在 compile_time 表达式内（委托路径）。
    int r = runSource(R"(
constexpr int32 twice(int32 x) { return x * 2; }
constexpr int32 r = compile_time.size_of(int32) * 0 + twice(21);
int32 main() { return r == 42 ? 1 : 0; }
)", "ct_constexpr_fn.c");
    EXPECT_EQ(r, 1);
}

TEST_F(CompileTimeE2E, StaticAssertE2E) {
    int r = runSource(R"(
compile_time.static_assert(compile_time.target.os == "linux", "this test runs on linux");
compile_time.static_assert(compile_time.size_of(int32) == 4, "int32 is 4 bytes");
int32 main() { return 0; }
)", "ct_static_assert.c");
    EXPECT_EQ(r, 0);
}

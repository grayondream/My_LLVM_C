#include <gtest/gtest.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

namespace fs = std::filesystem;

class EndToEndTest : public ::testing::Test {
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
    bool bad = llvm::verifyModule(ctx.getModule(), &vs);
    if (bad) return -1;

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

TEST_F(EndToEndTest, SimpleReturn42) {
    EXPECT_EQ(runSource("int main() { return 42; }", "simple.c"), 42);
}

TEST_F(EndToEndTest, LocalVar) {
    EXPECT_EQ(runSource("int main() { int a = 10; return a; }", "local.c"), 10);
}

TEST_F(EndToEndTest, TwoVars) {
    EXPECT_EQ(runSource(
        "int main() { int a = 10; int b = 20; return a + b; }", "two.c"), 30);
}

TEST_F(EndToEndTest, ExpressionArithmetic) {
    EXPECT_EQ(runSource(
        "int main() { int a = 10; int b = 20; int c = a + b * 3; return c; }", "expr.c"), 70);
}

TEST_F(EndToEndTest, ForLoop) {
    EXPECT_EQ(runSource(
        "int main() { int sum = 0; for (int i = 0; i <= 10; i = i + 1) { sum = sum + i; } return sum; }", "for.c"), 55);
}

TEST_F(EndToEndTest, SimpleFromFile) {
    std::ifstream ifs(fs::path(TEST_INPUT_DIR) / "simple.c");
    ASSERT_TRUE(ifs.is_open());
    std::stringstream ss; ss << ifs.rdbuf();
    EXPECT_EQ(runSource(ss.str(), "simple.c"), 42);
}

TEST_F(EndToEndTest, ExpressionFromFile) {
    std::ifstream ifs(fs::path(TEST_INPUT_DIR) / "expressions.c");
    ASSERT_TRUE(ifs.is_open());
    std::stringstream ss; ss << ifs.rdbuf();
    EXPECT_EQ(runSource(ss.str(), "expressions.c"), 70);
}

TEST_F(EndToEndTest, ControlFlowFromFile) {
    std::ifstream ifs(fs::path(TEST_INPUT_DIR) / "control_flow.c");
    ASSERT_TRUE(ifs.is_open());
    std::stringstream ss; ss << ifs.rdbuf();
    EXPECT_EQ(runSource(ss.str(), "control_flow.c"), 55);
}

TEST_F(EndToEndTest, DeferRunsAtScopeExitInReverseOrder) {
    EXPECT_EQ(runSource(R"(
        int g = 0;
        int add(int v) { g = g * 10 + v; return 0; }
        int main() {
            { defer add(1); defer add(2); defer add(3); }
            return g;
        }
    )", "defer_order.c"), 321);
}

TEST_F(EndToEndTest, DeferRunsOnReturn) {
    EXPECT_EQ(runSource(R"(
        int counter = 0;
        int tick() { counter = counter + 1; return 0; }
        int helper() { defer tick(); return 0; }
        int main() { helper(); return counter; }
    )", "defer_return.c"), 1);
}

TEST_F(EndToEndTest, DeferRunsOnBreak) {
    EXPECT_EQ(runSource(R"(
        int counter = 0;
        int tick() { counter = counter + 1; return 0; }
        int main() {
            int i = 0;
            while (i < 3) {
                defer tick();
                i = i + 1;
                if (i == 2) break;
            }
            return counter;
        }
    )", "defer_break.c"), 2);
}

TEST_F(EndToEndTest, DeferRunsOnContinue) {
    EXPECT_EQ(runSource(R"(
        int counter = 0;
        int tick() { counter = counter + 1; return 0; }
        int main() {
            for (int i = 0; i < 3; i = i + 1) {
                defer tick();
                continue;
            }
            return counter;
        }
    )", "defer_continue.c"), 3);
}

TEST_F(EndToEndTest, MultipleDeclarators) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int a = 5, b = 3;
            int c = 1, d = 2, e = 4;
            return a + b + c + d + e;
        }
    )", "multi_decl.c"), 15);
}

TEST_F(EndToEndTest, GlobalMultipleDeclarators) {
    EXPECT_EQ(runSource(R"(
        int g1 = 1, g2 = 2;
        int main() { return g1 + g2; }
    )", "multi_global.c"), 3);
}

TEST_F(EndToEndTest, PointerDeclaratorOnlyAppliesToFirst) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int x = 7;
            int* a = &x, b = 3;
            return *a + b;
        }
    )", "multi_ptr.c"), 10);
}

TEST_F(EndToEndTest, ArrayDecaysToPointer) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int arr[3];
            arr[0] = 1; arr[1] = 2; arr[2] = 3;
            int* p = arr;
            return p[2] + arr[0];
        }
    )", "array_decay.c"), 4);
}

TEST_F(EndToEndTest, ArrayArgumentDecaysToPointer) {
    EXPECT_EQ(runSource(R"(
        int sum(int* p, int n) {
            int s = 0;
            for (int i = 0; i < n; i = i + 1) { s = s + p[i]; }
            return s;
        }
        int main() {
            int arr[3];
            arr[0] = 1; arr[1] = 2; arr[2] = 3;
            return sum(arr, 3);
        }
    )", "array_arg.c"), 6);
}

TEST_F(EndToEndTest, FunctionPointerCall) {
    EXPECT_EQ(runSource(R"(
        int mul(int a, int b) { return a * b; }
        int main() {
            int (*fp)(int, int) = mul;
            return fp(2, 3);
        }
    )", "func_ptr.c"), 6);
}

TEST_F(EndToEndTest, FunctionPointerReassignment) {
    EXPECT_EQ(runSource(R"(
        int add(int a, int b) { return a + b; }
        int mul(int a, int b) { return a * b; }
        int main() {
            int (*fp)(int, int) = add;
            int x = fp(2, 3);
            fp = mul;
            return x + fp(2, 3);
        }
    )", "func_ptr_reassign.c"), 11);
}

TEST_F(EndToEndTest, DereferenceIsAssignable) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int y = 5;
            int* p = &y;
            *p = 9;
            return y;
        }
    )", "deref_assign.c"), 9);
}

TEST_F(EndToEndTest, DereferenceInExpression) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int y = 5;
            int* p = &y;
            int a = *p;
            return a + *p;
        }
    )", "deref_expr.c"), 10);
}

TEST_F(EndToEndTest, PointerToPointerDereference) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int y = 5;
            int* p = &y;
            int** pp = &p;
            **pp = 11;
            return y;
        }
    )", "deref_double.c"), 11);
}

TEST_F(EndToEndTest, AssignAddressOfToPointer) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int y = 5;
            int* q;
            q = &y;
            return *q;
        }
    )", "addr_assign.c"), 5);
}

TEST_F(EndToEndTest, StructInitializerList) {
    EXPECT_EQ(runSource(R"(
        struct Point { int x; int y; };
        int main() {
            struct Point p = {10, 20};
            return p.x + p.y;
        }
    )", "struct_init.c"), 30);
}

TEST_F(EndToEndTest, ArrayInitializerList) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int arr[5] = {1, 2, 3, 4, 5};
            int s = 0;
            for (int i = 0; i < 5; i = i + 1) { s = s + arr[i]; }
            return s;
        }
    )", "array_init.c"), 15);
}

TEST_F(EndToEndTest, UnsizedArrayInitializerList) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int arr[] = {1, 2, 3};
            return arr[0] + arr[1] + arr[2];
        }
    )", "unsized_array_init.c"), 6);
}

TEST_F(EndToEndTest, NestedAggregateInitializer) {
    EXPECT_EQ(runSource(R"(
        struct P { int x; int y; };
        struct R { struct P p; int z; };
        int main() {
            struct R r = {{1, 2}, 3};
            return r.p.x + r.p.y + r.z;
        }
    )", "nested_init.c"), 6);
}

TEST_F(EndToEndTest, GlobalAggregateInitializer) {
    EXPECT_EQ(runSource(R"(
        struct P { int x; int y; };
        struct P g = {4, 5};
        int main() { return g.x + g.y; }
    )", "global_init.c"), 9);
}

TEST_F(EndToEndTest, PartialAggregateInitializerZeroFills) {
    EXPECT_EQ(runSource(R"(
        struct P { int x; int y; };
        int main() {
            struct P p = {10};
            return p.x + p.y;
        }
    )", "partial_init.c"), 10);
}

TEST_F(EndToEndTest, PointerArithmeticInLoop) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int arr[5] = {1, 2, 3, 4, 5};
            int* p = arr;
            int s = 0;
            for (int i = 0; i < 5; i = i + 1) { s = s + *(p + i); }
            return s;
        }
    )", "ptr_arith.c"), 15);
}

TEST_F(EndToEndTest, PointerSubtraction) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int arr[5] = {1, 2, 3, 4, 5};
            int* p = arr;
            return *(p + 3) - *(p + 1);
        }
    )", "ptr_sub.c"), 2);
}

TEST_F(EndToEndTest, NegativeFloatLiteral) {
    EXPECT_EQ(runSource(
        "int main() { double d = -1.5; return (int)(d * 10.0); }", "negfloat.c"), -15);
}

TEST_F(EndToEndTest, NegativeFloatLiteralFloat) {
    EXPECT_EQ(runSource(
        "int main() { float f = -2.0f; return (int)f; }", "negfloatf.c"), -2);
}

TEST_F(EndToEndTest, PrefixIncrement) {
    EXPECT_EQ(runSource(
        "int main() { int a = 5; int k = ++a; return k * 10 + a; }", "preinc.c"), 66);
}

TEST_F(EndToEndTest, PrefixDecrement) {
    EXPECT_EQ(runSource(
        "int main() { int a = 5; int k = --a; return k * 10 + a; }", "predec.c"), 44);
}

TEST_F(EndToEndTest, PrefixIncrementThroughDeref) {
    EXPECT_EQ(runSource(
        "int main() { int y = 1; int* p = &y; int k = ++*p; return k + y; }",
        "preinc_deref.c"), 4);
}

TEST_F(EndToEndTest, SwitchDispatch) {
    EXPECT_EQ(runSource(R"(
        int pick(int n) {
            switch (n) {
                case 1: return 10;
                case 2:
                case 3: return 20;
                default: return 30;
            }
        }
        int main() { return pick(2) + pick(9); }
    )", "switch.c"), 50);
}

TEST_F(EndToEndTest, SwitchBreakStopsFallthrough) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int r = 0;
            switch (1) {
                case 1: r = r + 1; break;
                case 2: r = r + 100; break;
            }
            return r;
        }
    )", "switch_break.c"), 1);
}

TEST_F(EndToEndTest, PrintFormatsIntegers) {
    testing::internal::CaptureStdout();
    EXPECT_EQ(runSource(
        "int main() { print(\"{} + {} = {}\", 1, 2, 3); return 0; }", "print_ints.c"), 0);
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "1 + 2 = 3");
}

TEST_F(EndToEndTest, PrintlnAppendsNewline) {
    testing::internal::CaptureStdout();
    EXPECT_EQ(runSource("int main() { println(\"hi\"); return 0; }", "println.c"), 0);
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "hi\n");
}

TEST_F(EndToEndTest, PrintFormatsMixedTypes) {
    testing::internal::CaptureStdout();
    EXPECT_EQ(runSource(
        "int main() { print(\"{} {} {} {} {}\", 42, 1.5, 'A', \"str\", 7); return 0; }",
        "print_mixed.c"), 0);
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "42 1.5 A str 7");
}

TEST_F(EndToEndTest, PrintFormatsBool) {
    testing::internal::CaptureStdout();
    EXPECT_EQ(runSource(
        "int main() { bool t = 1; bool f = 0; print(\"{} {}\", t, f); return 0; }",
        "print_bool.c"), 0);
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "true false");
}

TEST_F(EndToEndTest, PrintEscapesLiteralPercent) {
    testing::internal::CaptureStdout();
    EXPECT_EQ(runSource("int main() { print(\"{}%\", 50); return 0; }", "print_pct.c"), 0);
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "50%");
}

TEST_F(EndToEndTest, PrintUsesFreeFunctionToString) {
    testing::internal::CaptureStdout();
    EXPECT_EQ(runSource(
        "struct P { int x; int y; };"
        "char* to_string(struct P p) { return \"Point\"; }"
        "int main() { struct P p; print(\"<{}>\", p); return 0; }",
        "print_to_string.c"), 0);
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "<Point>");
}

TEST_F(EndToEndTest, PrintUsesMethodToString) {
    testing::internal::CaptureStdout();
    EXPECT_EQ(runSource(
        "class C { int x; char* to_string() { return \"C!\"; } };"
        "int main() { C c; print(\"{}\", c); return 0; }",
        "print_method_to_string.c"), 0);
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "C!");
}

TEST_F(EndToEndTest, VariableCopyInitializationLoadsValue) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int a = 5;
            int b = a;
            int c = b;
            return a + b + c;
        }
    )", "copy_init.c"), 15);
}

TEST_F(EndToEndTest, UnionMemberAccess) {
    EXPECT_EQ(runSource(R"(
        union Data { int i; char str[20]; };
        int main() {
            union Data d;
            d.i = 65;
            int a = d.i;
            char c = d.str[0];
            int s = sizeof(union Data);
            return a + c + s;
        }
    )", "union_access.c"), 150);
}

TEST_F(EndToEndTest, UnionSizeIsLargestMember) {
    EXPECT_EQ(runSource(R"(
        union Data { int i; float f; char str[20]; };
        int main() { return sizeof(union Data); }
    )", "union_size.c"), 20);
}

TEST_F(EndToEndTest, UnionPointerMemberAccess) {
    EXPECT_EQ(runSource(R"(
        union Data { int i; float f; };
        int main() {
            union Data d;
            d.i = 3;
            union Data* p = &d;
            p->i = 7;
            return p->i + d.i;
        }
    )", "union_ptr.c"), 14);
}

TEST_F(EndToEndTest, StructArrayMember) {
    EXPECT_EQ(runSource(R"(
        struct S { int n; char name[3]; };
        int main() {
            struct S s;
            s.n = 5;
            s.name[0] = 1;
            s.name[1] = 2;
            s.name[2] = 3;
            return s.n + s.name[0] + s.name[1] + s.name[2];
        }
    )", "struct_array_member.c"), 11);
}

TEST_F(EndToEndTest, PrototypeThenDefinition) {
    EXPECT_EQ(runSource(R"(
        int add(int a, int b);
        int add(int a, int b) { return a + b; }
        int main() { return add(2, 3); }
    )", "prototype.c"), 5);
}

TEST_F(EndToEndTest, SizeofTypeAndExpr) {
    EXPECT_EQ(runSource(R"(
        int main() {
            int x = 10;
            int a = sizeof(int);
            int b = sizeof(x);
            int c = sizeof(x + 1);
            return a + b + c;
        }
    )", "sizeof.c"), 12);
}

TEST_F(EndToEndTest, BitwiseNot) {
    EXPECT_EQ(runSource(
        "int main() { int a = 5; return ~a; }", "bitnot.c"), -6);
}

TEST_F(EndToEndTest, ShiftOperators) {
    EXPECT_EQ(runSource(
        "int main() { int a = 1 << 4; int b = 32 >> 2; return a + b; }", "shift.c"), 24);
}

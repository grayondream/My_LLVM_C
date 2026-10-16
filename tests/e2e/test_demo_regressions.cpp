// 演示文件 examples/all_features.c 建设期间发现并修复的编译器 bug 回归测试：
//  - Bug A: CompileTimeEvaluator::toLLVMType 对 Class 用 static_cast<StructType*>
//    读 isPacked（跨类 UB，读垃圾值）→ 布局损坏，size_of(派生类) 非确定性
//    "Invalid size request on a scalable vector" 崩溃。
//  - Bug B: 无后缀整型字面量恒 int32 → int64 v = 4000000000 以有符号 int32 构造
//    APInt（Debug 断言崩溃 / Release 静默截断为错误值）。
//  - Bug C: ForStmtAST 循环变量不建独立作用域 → 兄弟 for 循环复用同名变量时
//    第二个循环绑定首个循环的 i（结束值 11），条件立即为假、循环体不执行。
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

class DemoRegressionE2E : public ::testing::Test {
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
    return ((int (*)())(intptr_t)sym->getValue())();
}

// Bug B：无后缀大整数必须按值域提升（4000000000 > INT32_MAX → int64）。
TEST_F(DemoRegressionE2E, LargeUnSuffixedLiteralPromotes) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int64 big = 4000000000;
            if (big != 4000000000) { return 1; }
            int64 sum = big + 1;
            if (sum != 4000000001) { return 2; }
            return 0;
        }
    )", "reg_lit.c"), 0);
}

// Bug C：兄弟 for 循环复用同名循环变量，第二个循环必须重新绑定（C 语义）。
TEST_F(DemoRegressionE2E, SiblingForLoopReusesVarName) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32 sum = 0;
            for (int32 i = 0; i <= 10; i = i + 1) { sum = sum + i; }
            int32 cont = 0;
            for (int32 i = 0; i < 5; i = i + 1) {
                if (i == 2) { continue; }
                if (i == 4) { break; }
                cont = cont + 1;
            }
            if (sum != 55 || cont != 3) { return 1; }
            return 0;
        }
    )", "reg_for.c"), 0);
}

// Bug A：类位域 + 派生类 size_of 组合不再损坏布局（编译 + 执行均通过）。
TEST_F(DemoRegressionE2E, ClassBitFieldWithSizeOfDerived) {
    EXPECT_EQ(runSource(R"(
        class Counter {
        public:
            int32 count;
            int32 bf : 3;
            void inc() { this->count = this->count + 1; }
        };
        class Animal {
        public:
            int32 age;
            void setAge(int32 v) { this->age = v; }
        };
        class Dog : public Animal {
        public:
            int32 legs;
            int32 bark() { return this->age * 10 + this->legs; }
        };
        int32 main() {
            Counter ctr;
            ctr.count = 0;
            ctr.inc();
            ctr.bf = 3;
            if (ctr.count != 1 || ctr.bf != 3) { return 1; }
            Dog d;
            d.setAge(4);
            d.legs = 4;
            if (d.bark() != 44) { return 2; }
            if (compile_time.size_of(Dog) != 8) { return 3; }
            return 0;
        }
    )", "reg_cls.c"), 0);
}

// Bug D：指针后缀 ++/-- 须按元素步进（GEP），不能字节 add——int32* ++ 只前进
// 1 字节时解引用读到错位值。
TEST_F(DemoRegressionE2E, PointerPostfixIncDecStepsByElement) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32 arr[3] = {10, 20, 30};
            int32* p = &arr[0];
            p++;
            if (*p != 20) { return 1; }
            p--;
            if (*p != 10) { return 2; }
            p++;
            int32* q = &arr[0];
            ++q;
            if (*q != 20) { return 3; }
            int32* r = &arr[2];
            r--;
            if (*r != 20) { return 4; }
            return 0;
        }
    )", "reg_postinc.c"), 0);
}

// Bug E：浮点复合赋值走整数指令（CreateMul 等）→ 位模式乘法垃圾值；须 F 系。
TEST_F(DemoRegressionE2E, FloatCompoundAssignmentUsesFloatOps) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            float64 fd = 1.5;
            fd *= 2.0;
            if (fd != 3.0) { return 1; }
            fd += 0.5;
            if (fd != 3.5) { return 2; }
            fd -= 1.0;
            if (fd != 2.5) { return 3; }
            fd /= 2.5;
            if (fd != 1.0) { return 4; }
            float32 h = 10.0f;
            h /= 4.0f;
            if (h != 2.5f) { return 5; }
            return 0;
        }
    )", "reg_fcmpd.c"), 0);
}

// Bug F：无符号复合赋值走有符号指令——`>>=` 算术移、`/=`/`%=` 高位错值。
TEST_F(DemoRegressionE2E, UnsignedCompoundAssignmentUsesUnsignedOps) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            uint32 w = 0x80000000;
            w >>= 4;
            if (w != 0x08000000) { return 1; }
            uint32 d = 0x80000000;
            d /= 2;
            if (d != 0x40000000) { return 2; }
            uint32 m = 0x80000000;
            m %= 3;                  // 2147483648 % 3 = 2
            if (m != 2) { return 3; }
            return 0;
        }
    )", "reg_ucomp.c"), 0);
}

// Bug G：运算符重载解析后表达式类型被设为左操作数类型而非 operator 函数
// 返回类型 → `int32 s = p + q` 类型不匹配、`p + q == 10` 比较 V2 与 int32。
TEST_F(DemoRegressionE2E, OperatorOverloadExpressionTypeIsReturnType) {
    EXPECT_EQ(runSource(R"(
        struct V2 { int32 x; int32 y; };
        int32 operator+(V2 a, V2 b) { return a.x + b.x + a.y + b.y; }
        bool operator==(V2 a, V2 b) { return a.x == b.x && a.y == b.y; }
        int32 main() {
            V2 p = {1, 2};
            V2 q = {3, 4};
            int32 s = p + q;                 // 直接赋值
            if (s != 10) { return 1; }
            if (!(p + q == 10)) { return 2; } // 复合比较
            int32 c = p + q + 1;              // 与 int 链接
            if (c != 11) { return 3; }
            V2 r = {1, 2};
            if (!(p == r)) { return 4; }
            return 0;
        }
    )", "reg_opov.c"), 0);
}

// Bug H：void 函数裸 `return;`（早退）此前解析器硬性要求表达式被拒；
// sema/codegen 早已支持（void 检查 + CreateRetVoid），仅解析放行。
TEST_F(DemoRegressionE2E, BareReturnInVoidFunction) {
    EXPECT_EQ(runSource(R"(
        int32 trace = 0;
        void early(int32 x) {
            if (x < 0) return;
            if (x == 0) return;
            trace = trace + x;
        }
        int32 main() {
            early(5); early(-1); early(0); early(7);
            if (trace != 12) { return 1; }
            return 0;
        }
    )", "reg_ret.c"), 0);
}

// Gap 1：`&fn` 取函数地址（函数设计符 &fn == fn，C 语义）。
TEST_F(DemoRegressionE2E, AddressOfFunctionDesignator) {
    EXPECT_EQ(runSource(R"(
        int32 add(int32 a, int32 b) { return a + b; }
        int32 main() {
            int32 (*fp)(int32, int32) = &add;
            if (fp(2, 3) != 5) { return 1; }
            int32 (*fp2)(int32, int32) = add;
            if (fp2(4, 5) != 9) { return 2; }
            return 0;
        }
    )", "reg_andfn.c"), 0);
}

// Gap 2：string_view 下标 s[i] 取字节（等价 char_at）。
TEST_F(DemoRegressionE2E, StringViewSubscript) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            string_view s = "hello";
            if (s[0] != 'h' || s[4] != 'o') { return 1; }
            if (s.char_at(1) != 'e') { return 2; }
            int32 sum2 = s[0] + s[1] + s[2];
            if (sum2 != 'h' + 'e' + 'l') { return 3; }
            return 0;
        }
    )", "reg_stridx.c"), 0);
}

// Gap 3：指针相减 p - q → 元素个数（ptrdiff）。
TEST_F(DemoRegressionE2E, PointerDifference) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int32 arr[5] = {10, 20, 30, 40, 50};
            int32* a = &arr[0];
            int32* b = &arr[4];
            if (b - a != 4) { return 1; }
            if (a - b != -4) { return 2; }
            int32* m = &arr[2];
            if (b - m != 2) { return 3; }
            return 0;
        }
    )", "reg_ptrdiff.c"), 0);
}

// Gap 4：数组 typedef（下标 / slice / sizeof / 多维）。
TEST_F(DemoRegressionE2E, ArrayTypedefWorks) {
    EXPECT_EQ(runSource(R"(
        typedef int32 IntArr[4];
        typedef int32 Mat[2][3];
        int32 sum(int32[] s) {
            int32 t = 0;
            for (int32 i = 0; i < s.len; i = i + 1) { t = t + s[i]; }
            return t;
        }
        int32 main() {
            IntArr arr = {1, 2, 3, 4};
            if (arr[0] != 1 || arr[3] != 4) { return 1; }
            if (sizeof(arr) != 16) { return 2; }
            int32[] s = arr;
            if (s.len != 4 || sum(arr) != 10) { return 3; }
            Mat m = {{1, 2, 3}, {4, 5, 6}};
            if (m[1][2] != 6 || sizeof(m) != 24) { return 4; }
            return 0;
        }
    )", "reg_arrtyp.c"), 0);
}

// Gap 5：函数指针 typedef + 函数返回函数指针（经 typedef 惯用法）。
TEST_F(DemoRegressionE2E, FunctionPointerTypedefAndReturn) {
    EXPECT_EQ(runSource(R"(
        typedef int32 (*BinOp)(int32, int32);
        int32 add(int32 a, int32 b) { return a + b; }
        int32 mul(int32 a, int32 b) { return a * b; }
        BinOp pick(bool useAdd) { return useAdd ? add : mul; }
        int32 main() {
            BinOp f = add;
            if (f(2, 3) != 5) { return 1; }
            BinOp g = pick(true);
            if (g(3, 4) != 7) { return 2; }
            g = pick(false);
            if (g(3, 4) != 12) { return 3; }
            return 0;
        }
    )", "reg_fnptrtyp.c"), 0);
}

// Gap 6：int128/uint128 字面量（>64 位）。
TEST_F(DemoRegressionE2E, Int128Literal) {
    EXPECT_EQ(runSource(R"(
        int32 main() {
            int128 big = 170141183460469231731687303715884105727;
            if (big != 170141183460469231731687303715884105727) { return 1; }
            if (!(big > 0)) { return 2; }
            int128 shifted = 1;
            shifted = shifted << 100;
            if (shifted != 1267650600228229401496703205376) { return 3; }
            uint128 ub = 340282366920938463463374607431768211455;
            if (!(ub > 170141183460469231731687303715884105727)) { return 4; }
            return 0;
        }
    )", "reg_i128.c"), 0);
}

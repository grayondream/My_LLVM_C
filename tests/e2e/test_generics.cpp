// P1-03 / GEN-03: 类模板端到端。fixture 仿 test_optional_result.cpp。

#include <gtest/gtest.h>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/TemplateRegistry.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"

#include <spdlog/spdlog.h>

class GenericE2E : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
        TemplateRegistry::instance().resetForTesting();
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

TEST_F(GenericE2E, BoxRoundTrip) {
    EXPECT_EQ(runSource(R"(
        template<typename T> struct Box { T value; };
        int32 main() {
            Box<int32> b;
            b.value = 42;
            return b.value;
        }
    )", "gen1.c"), 42);
}

TEST_F(GenericE2E, BoxNested) {
    EXPECT_EQ(runSource(R"(
        template<typename T> struct Box { T value; };
        int32 main() {
            Box<Box<int32>> b;
            b.value.value = 7;
            return b.value.value;
        }
    )", "gen2.c"), 7);
}

TEST_F(GenericE2E, GenericFuncRoundTrip) {
    EXPECT_EQ(runSource(R"(
        template<typename T> T tmax(T a, T b) { return a; }
        int32 main() {
            return tmax(3, 4);
        }
    )", "gen3.c"), 3);
}

TEST_F(GenericE2E, GenericFuncExplicit) {
    EXPECT_EQ(runSource(R"(
        template<typename T> T tmax(T a, T b) { return a; }
        int32 main() {
            return tmax<float64>(2.5, 1.5) == 2.5 ? 7 : 0;
        }
    )", "gen4.c"), 7);
}

TEST_F(GenericE2E, NonTemplateExactMatchWins) {
    EXPECT_EQ(runSource(R"(
        template<typename T> T tmax(T a, T b) { return a; }
        int32 tmax(int32 a, int32 b) { return 99; }
        int32 main() {
            // 普通函数精确匹配优先（SEM-16 最小规则）
            if (tmax(3, 4) == 99) return 1;
            return 0;
        }
    )", "gen5.c"), 1);
}

TEST_F(GenericE2E, FixedArrayTemplate) {
    EXPECT_EQ(runSource(R"(
        template<typename T, usize N> struct Arr { T data[N]; };
        int32 main() {
            Arr<int32, 4> a;
            a.data[0] = 1; a.data[1] = 2; a.data[2] = 3; a.data[3] = 4;
            return a.data[0] + a.data[1] + a.data[2] + a.data[3];
        }
    )", "gen6.c"), 10);
}

TEST_F(GenericE2E, AliasTemplateE2E) {
    EXPECT_EQ(runSource(R"(
        template<typename T, usize N> struct Arr { T data[N]; };
        template<typename T> using Vec = Arr<T, 2>;
        int32 main() {
            Vec<int32> v;
            v.data[0] = 5; v.data[1] = 6;
            return v.data[0] * 10 + v.data[1];
        }
    )", "gen7.c"), 56);
}

TEST_F(GenericE2E, ThisFieldAccess) {
    EXPECT_EQ(runSource(R"(
        class FieldA {
        public:
            int32 v;
            int32 get() { return this.v; }
        };
        int32 main() {
            FieldA a;
            a.v = 9;
            return a.get();
        }
    )", "gen8.c"), 9);
}

TEST_F(GenericE2E, ThisPassthrough) {
    EXPECT_EQ(runSource(R"(
        class SelfA {
        public:
            int32 v;
            SelfA* self() { return this; }
        };
        int32 main() {
            SelfA a;
            a.v = 1;
            a.self()->v = 6;
            return a.v;
        }
    )", "gen9.c"), 6);
}

// ========== P1-03 / INH-05 / GEN-09: CRTP 全链路 ==========

TEST_F(GenericE2E, CrtpStaticDispatch) {
    EXPECT_EQ(runSource(R"(
        template<typename D>
        class Shape {
        public:
            float64 twice_area() { return static_cast<D*>(this)->area() * 2.0; }
        };
        class Circle : Shape<Circle> {
        public:
            float64 r;
            float64 area() { return 3.14159 * this->r * this->r; }
        };
        int32 main() {
            Circle c;
            c.r = 1.0;
            float64 t = c.twice_area();
            return t > 6.0 && t < 6.3 ? 1 : 0;
        }
    )", "gen10.c"), 1);
}

TEST_F(GenericE2E, CrtpSameShape) {
    EXPECT_EQ(runSource(R"(
        template<typename D>
        class Shape {
        public:
            float64 twice_area() { return static_cast<D*>(this)->area() * 2.0; }
            bool same_shape(D* other) {
                return static_cast<D*>(this)->area() == other->area();
            }
        };
        class Circle2 : Shape<Circle2> {
        public:
            float64 r;
            float64 area() { return 3.14159 * this->r * this->r; }
        };
        int32 main() {
            Circle2 c1;
            Circle2 c2;
            c1.r = 1.0;
            c2.r = 1.0;
            return c1.same_shape(&c2) ? 1 : 0;
        }
    )", "gen11.c"), 1);
}

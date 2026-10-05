#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

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

class ClassCodegenE2E : public ::testing::Test {
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

TEST_F(ClassCodegenE2E, SimpleClass) {
    EXPECT_EQ(runSource(R"(
        class Foo {
            public:
            int32 x;
            void setX(int32 v) { this->x = v; }
            int32 getX() { return this->x; }
        };
        
        int32 main() {
            Foo f;
            f.setX(42);
            return f.getX() - 42;
        }
    )", "test_class_simple.c"), 0);
}

TEST_F(ClassCodegenE2E, ClassWithMultipleMethods) {
    EXPECT_EQ(runSource(R"(
        class Counter {
            public:
            int32 count;
            void init() { this->count = 0; }
            void increment() { this->count = this->count + 1; }
            int32 getCount() { return this->count; }
        };
        
        int32 main() {
            Counter c;
            c.init();
            c.increment();
            c.increment();
            c.increment();
            return c.getCount() - 3;
        }
    )", "test_class_multi_method.c"), 0);
}

TEST_F(ClassCodegenE2E, ClassWithInheritance) {
    EXPECT_EQ(runSource(R"(
        class Base {
            public:
            int32 x;
            void setX(int32 v) { this->x = v; }
            int32 getX() { return this->x; }
        };
        
        class Derived : public Base {
            public:
            int32 y;
            void setY(int32 v) { this->y = v; }
            int32 getY() { return this->y; }
        };
        
        int32 main() {
            Derived d;
            d.setX(10);
            d.setY(20);
            return d.getX() + d.getY() - 30;
        }
    )", "test_class_inherit.c"), 0);
}

TEST_F(ClassCodegenE2E, ClassMethodCallFromMain) {
    EXPECT_EQ(runSource(R"(
        class Math {
            public:
            int32 value;
            void setValue(int32 v) { this->value = v; }
            int32 doubleIt() { return this->value + this->value; }
        };
        
        int32 main() {
            Math m;
            m.setValue(21);
            return m.doubleIt() - 42;
        }
    )", "test_class_method_call.c"), 0);
}

// AGG-10: static members — global storage, class-qualified access only.
TEST_F(ClassCodegenE2E, SMCounterPersist) {
    EXPECT_EQ(runSource(R"(
        class SMCtr2 {
            private: static int32 count = 0;
            public: static int32 next() { SMCtr2::count = SMCtr2::count + 1; return SMCtr2::count; }
        };
        int32 main() {
            SMCtr2::next(); SMCtr2::next();
            return SMCtr2::next() == 3 ? 0 : 1;
        }
    )", "test_sm_counter.c"), 0);
}

TEST_F(ClassCodegenE2E, SMStaticFactory) {
    EXPECT_EQ(runSource(R"(
        class SMPoint { public: int32 x; int32 y; };
        class SMRegistry {
            public: static SMPoint origin() { SMPoint p; p.x = 0; p.y = 0; return p; }
        };
        int32 main() {
            SMPoint p = SMRegistry::origin();
            return (p.x == 0 && p.y == 0) ? 0 : 1;
        }
    )", "test_sm_factory.c"), 0);
}

TEST_F(ClassCodegenE2E, SMPrivateInternalUse) {
    // Review Focus 3: currentClass context covers static methods — no E2009.
    EXPECT_EQ(runSource(R"(
        class SMAcc {
            private: static int32 secret = 11;
            public: int32 reveal() { return SMAcc::secret; }
        };
        int32 main() {
            SMAcc obj;
            return obj.reveal() == 11 ? 0 : 1;
        }
    )", "test_sm_internal.c"), 0);
}

TEST_F(ClassCodegenE2E, SMStaticAfterForwardDecl) {
    // Review Focus 2: the type is registered by the forward declaration;
    // the later definition's staticMembers must still emit the global.
    EXPECT_EQ(runSource(R"(
        class SMFwd;
        class SMFwd { public: static int32 v = 9; };
        int32 main() { return SMFwd::v == 9 ? 0 : 1; }
    )", "test_sm_fwd.c"), 0);
}

// Review C2: struct with only static members — global storage, no fallback.
TEST_F(ClassCodegenE2E, SMStructStaticE2E) {
    EXPECT_EQ(runSource(R"(
        struct SMS3 { static int32 v = 4; };
        int32 main() { return SMS3::v == 4 ? 0 : 1; }
    )", "test_sm_struct.c"), 0);
}

// ========== AGG-11: nested types e2e ==========

TEST_F(ClassCodegenE2E, NTStructFieldE2E) {
    EXPECT_EQ(runSource(R"(
        class NTE1 { public: struct Inner { int32 x; }; };
        int32 main() {
            NTE1::Inner obj; obj.x = 3;
            return obj.x == 3 ? 0 : 1;
        }
    )", "test_nt_struct.c"), 0);
}

TEST_F(ClassCodegenE2E, NTEnumConstantE2E) {
    EXPECT_EQ(runSource(R"(
        class NTE2 { public: enum Color { Red, Green }; };
        int32 main() { NTE2::Color c = NTE2::Green; return c == 1 ? 0 : 1; }
    )", "test_nt_enum.c"), 0);
}

TEST_F(ClassCodegenE2E, NTStaticE2E) {
    // 注：嵌套带方法的类型用 class（struct 无方法通路，Task 2 Ruling）。
    EXPECT_EQ(runSource(R"(
        class NTE3 { public: class S {
            private: static int32 count = 0;
            public: static int32 next() { NTE3::S::count = NTE3::S::count + 1; return NTE3::S::count; }
        }; };
        int32 main() { NTE3::S::next(); return NTE3::S::next() == 2 ? 0 : 1; }
    )", "test_nt_static.c"), 0);
}

TEST_F(ClassCodegenE2E, NTMethodE2E) {
    EXPECT_EQ(runSource(R"(
        class NTE4 { public: class S { public: int32 f(int32 x) { return x; } }; };
        int32 main() { NTE4::S obj; return obj.f(7) == 7 ? 0 : 1; }
    )", "test_nt_method.c"), 0);
}

TEST_F(ClassCodegenE2E, NTDeepE2E) {
    EXPECT_EQ(runSource(R"(
        class NTE5 { public: struct A { struct B { int32 v; }; }; };
        int32 main() { NTE5::A::B obj; obj.v = 2; return obj.v == 2 ? 0 : 1; }
    )", "test_nt_deep.c"), 0);
}

TEST_F(ClassCodegenE2E, NTFwdWithNestedE2E) {
    // Review Focus 2：前向声明+定义时 nestedTypes 仍生成。嵌套类带方法，
    // 确保既有类型提前 return 分支也执行 nestedTypes 遍历。
    EXPECT_EQ(runSource(R"(
        class NTE6;
        class NTE6 { public: class S { public: int32 f(int32 x) { return x; } }; };
        int32 main() { NTE6::S obj; return obj.f(6) == 6 ? 0 : 1; }
    )", "test_nt_fwd.c"), 0);
}

TEST_F(ClassCodegenE2E, NTSelfRefPtrE2E) {
    // 前向声明语义钉住（DS6）：自引用指针。
    EXPECT_EQ(runSource(R"(
        struct NFNode { int32 v; struct NFNode* next; };
        int32 main() {
            struct NFNode n; n.v = 1; n.next = 0;
            return n.v == 1 ? 0 : 1;
        }
    )", "test_nt_selfref.c"), 0);
}

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
            int x;
            void setX(int v) { this->x = v; }
            int getX() { return this->x; }
        };
        
        int main() {
            Foo f;
            f.setX(42);
            return f.getX() - 42;
        }
    )", "test_class_simple.c"), 0);
}

TEST_F(ClassCodegenE2E, ClassWithMultipleMethods) {
    EXPECT_EQ(runSource(R"(
        class Counter {
            int count;
            void init() { this->count = 0; }
            void increment() { this->count = this->count + 1; }
            int getCount() { return this->count; }
        };
        
        int main() {
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
            int x;
            void setX(int v) { this->x = v; }
            int getX() { return this->x; }
        };
        
        class Derived : public Base {
            int y;
            void setY(int v) { this->y = v; }
            int getY() { return this->y; }
        };
        
        int main() {
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
            int value;
            void setValue(int v) { this->value = v; }
            int doubleIt() { return this->value + this->value; }
        };
        
        int main() {
            Math m;
            m.setValue(21);
            return m.doubleIt() - 42;
        }
    )", "test_class_method_call.c"), 0);
}

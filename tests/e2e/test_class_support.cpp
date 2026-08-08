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

class ClassE2ETest : public ::testing::Test {
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

TEST_F(ClassE2ETest, BasicClass) {
    EXPECT_EQ(runSource(R"(
        class Foo {
            int x;
            void setX(int v) { this->x = v; }
            int getX() { return this->x; }
        };
        
        int main() {
            Foo f;
            f.setX(42);
            return f.getX();
        }
    )", "test_class.c"), 42);
}

TEST_F(ClassE2ETest, ClassInheritance) {
    EXPECT_EQ(runSource(R"(
        class Base {
            int x;
            void setX(int v) { this->x = v; }
        };
        
        class Derived : public Base {
            int y;
            void setY(int v) { this->y = v; }
        };
        
        int main() {
            Derived d;
            d.setX(10);
            d.setY(20);
            return d.x + d.y;
        }
    )", "test_inherit.c"), 30);
}

TEST_F(ClassE2ETest, ClassAsFunctionParam) {
    EXPECT_EQ(runSource(R"(
        class Point {
            int x;
            int y;
        };
        
        int getSum(Point p) {
            return p.x + p.y;
        }
        
        int main() {
            Point p;
            p.x = 5;
            p.y = 10;
            return getSum(p);
        }
    )", "test_class_param.c"), 15);
}

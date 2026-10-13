// P1-07 / PAR-10: 位域（Bit-fields）——语法解析、语义校验、System V 布局、RMW 读写。
// fixture 仿 test_annotations.cpp（runSource 正向专用；错误路径走显式管线）。

#include <gtest/gtest.h>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"
#include "codegen/CodegenContext.h"
#include "support/Log.h"
#include "ast/Decl.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"

#include <spdlog/spdlog.h>

class BitFieldE2E : public ::testing::Test {
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

    // 完整管线：main 返回值即断言。parse/sema/codegen/verify 失败 → -1（正向用例专用）。
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

// ---- Task 1: 语法解析 + 数据载体 ----

// 正向测试走显式 parser 管线并精确断言 FieldInfo.bitWidth（runSource 只查
// ast!=nullptr，无法捕获"位宽被静默丢弃"——当前解析器把 `a : 3` 当普通字段 `a`）。
TEST_F(BitFieldE2E, ParseBasicDeclaration) {
    Lexer lexer("bf_parse.c",
        "struct S { int32 a : 3; uint32 b : 5; int32 : 0; int32 : 3; }; "
        "int32 main() { return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    ASSERT_TRUE(parser.getErrors().empty());
    ASSERT_GE(ast->declarations.size(), 1u);
    auto* sd = dynamic_cast<StructDeclAST*>(ast->declarations[0].get());
    ASSERT_NE(sd, nullptr);
    ASSERT_EQ(sd->fields.size(), 4u);
    EXPECT_EQ(sd->fields[0].name, "a");
    EXPECT_EQ(sd->fields[0].bitWidth, 3);
    EXPECT_EQ(sd->fields[1].name, "b");
    EXPECT_EQ(sd->fields[1].bitWidth, 5);
    EXPECT_EQ(sd->fields[2].name, "");   // 无名零宽位域
    EXPECT_EQ(sd->fields[2].bitWidth, 0);
    EXPECT_EQ(sd->fields[3].name, "");   // 无名填充位域
    EXPECT_EQ(sd->fields[3].bitWidth, 3);
}

// 位域 + 数组后缀互斥：`int32 a : 3[2]` 报错（数组后缀落在位域宽度之后）。
TEST_F(BitFieldE2E, ParseArraySuffixRejected) {
    Lexer lexer("bf_arr.c",
        "struct S { int32 a : 3[2]; }; int32 main() { return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.getErrors().empty());
}

// 位域宽度须为常量表达式：`int32 a : x;`（x 为运行时变量）报错。
TEST_F(BitFieldE2E, ParseNonConstWidthRejected) {
    Lexer lexer("bf_nc.c",
        "struct S { int32 a : x; }; int32 main() { int32 x = 3; return 0; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.getErrors().empty());
}

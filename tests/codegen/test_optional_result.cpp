// P1-02 (TYP-13/14): Optional/Result LLVM 布局单元测试。
// DS4（spec 2026-10-06-optional-result-design.md §4.2）：
//   T?           -> { i1 valid, T value }     （valid 在前）
//   Result<T,E>  -> { i1 ok, T value, E error }
// 且不同类型实参不得共享 LLVM 具名结构体。

#include "gtest/gtest.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"

#include "ast/Type.h"
#include "codegen/CodegenContext.h"
#include "frontend/Lexer.h"

#include <spdlog/spdlog.h>

TEST(OptResLayoutTest, OptionalIsFlagFirst) {
    spdlog::set_level(spdlog::level::off);
    CodegenContext ctx;
    Type* t = TypeContext::instance().getOptionalType(
        TypeContext::instance().getInt32());
    auto* st = llvm::cast<llvm::StructType>(ctx.getLLVMType(t));
    EXPECT_EQ(st->getNumElements(), 2u);
    EXPECT_EQ(st->getElementType(0), llvm::Type::getInt1Ty(ctx.getContext()));
    EXPECT_EQ(st->getElementType(1), llvm::Type::getInt32Ty(ctx.getContext()));
}

TEST(OptResLayoutTest, ResultIsFlagFirstTriple) {
    spdlog::set_level(spdlog::level::off);
    CodegenContext ctx;
    Type* t = TypeContext::instance().getResultType(
        TypeContext::instance().getInt32(),
        TypeContext::instance().getInt64());
    auto* st = llvm::cast<llvm::StructType>(ctx.getLLVMType(t));
    EXPECT_EQ(st->getNumElements(), 3u);
    EXPECT_EQ(st->getElementType(0), llvm::Type::getInt1Ty(ctx.getContext()));
    EXPECT_EQ(st->getElementType(1), llvm::Type::getInt32Ty(ctx.getContext()));
    EXPECT_EQ(st->getElementType(2), llvm::Type::getInt64Ty(ctx.getContext()));
}

TEST(OptResLayoutTest, DistinctInstancesHaveDistinctNames) {
    spdlog::set_level(spdlog::level::off);
    CodegenContext ctx;
    Type* a = TypeContext::instance().getOptionalType(
        TypeContext::instance().getInt32());
    Type* b = TypeContext::instance().getOptionalType(
        TypeContext::instance().getFloat64());
    auto* sa = llvm::cast<llvm::StructType>(ctx.getLLVMType(a));
    auto* sb = llvm::cast<llvm::StructType>(ctx.getLLVMType(b));
    EXPECT_NE(sa->getName(), sb->getName());
}

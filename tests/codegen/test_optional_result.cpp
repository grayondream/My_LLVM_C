// P1-02 (TYP-13/14): Optional/Result LLVM 布局单元测试。
// DS4（spec 2026-10-06-optional-result-design.md §4.2）：
//   T?           -> { i1 valid, T value }     （valid 在前）
//   result<T,E>  -> { i1 ok, T value, E error }
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

TEST(OptResLayoutTest, EnumUnderlyingDistinctLayouts) {
    // 评审 C1：布局身份键须单射——不同底层类型的枚举的 Optional 不得共享
    // LLVM 具名结构体（否则 8 字节 store 打进 2 字节栈槽）。
    spdlog::set_level(spdlog::level::off);
    CodegenContext ctx;
    auto* small = new EnumType("LayoutColor");
    small->underlyingType = TypeContext::instance().getUInt8();
    auto* big = new EnumType("LayoutBig");
    big->underlyingType = TypeContext::instance().getUInt64();
    auto* sa = llvm::cast<llvm::StructType>(
        ctx.getLLVMType(TypeContext::instance().getOptionalType(small)));
    auto* sb = llvm::cast<llvm::StructType>(
        ctx.getLLVMType(TypeContext::instance().getOptionalType(big)));
    EXPECT_NE(sa->getName(), sb->getName());
}

TEST(OptResLayoutTest, ResultNameCollisionInjective) {
    // 评审 C1：result<My_Err, x> 与 result<My, Err_x> 不得同名。
    spdlog::set_level(spdlog::level::off);
    CodegenContext ctx;
    Type* r1 = TypeContext::instance().getResultType(
        new StructType("My_Err"), new StructType("x"));
    Type* r2 = TypeContext::instance().getResultType(
        new StructType("My"), new StructType("Err_x"));
    auto* s1 = llvm::cast<llvm::StructType>(ctx.getLLVMType(r1));
    auto* s2 = llvm::cast<llvm::StructType>(ctx.getLLVMType(r2));
    EXPECT_NE(s1->getName(), s2->getName());
}

TEST(OptResLayoutTest, UserStructPrefixCollisionSafe) {
    // 评审 C1：用户 struct 名撞内建前缀不得复用布局。
    spdlog::set_level(spdlog::level::off);
    CodegenContext ctx;
    Type* builtin = TypeContext::instance().getOptionalType(
        TypeContext::instance().getInt32());
    Type* user = new StructType("Optional_int32");
    auto* sb = llvm::cast<llvm::StructType>(ctx.getLLVMType(builtin));
    auto* su = llvm::cast<llvm::StructType>(ctx.getLLVMType(user));
    EXPECT_NE(sb->getName(), su->getName());
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

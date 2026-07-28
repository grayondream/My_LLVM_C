#include <gtest/gtest.h>
#include "ast/Type.h"

class TypeNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        typeCtx = &TypeContext::instance();
    }

    TypeContext* typeCtx;
};

TEST_F(TypeNodeTest, ArrayType) {
    auto arrType = std::make_unique<ArrayType>(typeCtx->getInt(), 10);

    EXPECT_EQ(arrType->kind, TypeKind::Array);
    EXPECT_EQ(arrType->elementType->kind, TypeKind::Int);
    EXPECT_EQ(arrType->size, 10);
}

TEST_F(TypeNodeTest, StructType) {
    auto structType = std::make_unique<StructType>("Point");
    structType->addField("x", typeCtx->getInt());
    structType->addField("y", typeCtx->getInt());

    EXPECT_EQ(structType->kind, TypeKind::Struct);
    EXPECT_EQ(structType->name, "Point");
    EXPECT_EQ(structType->fields.size(), 2);
    EXPECT_EQ(structType->fields[0].first, "x");
    EXPECT_EQ(structType->fields[1].first, "y");
}

TEST_F(TypeNodeTest, UnionType) {
    auto unionType = std::make_unique<UnionType>("Data");
    unionType->addMember("i", typeCtx->getInt());
    unionType->addMember("f", typeCtx->getFloat());

    EXPECT_EQ(unionType->kind, TypeKind::Union);
    EXPECT_EQ(unionType->name, "Data");
    EXPECT_EQ(unionType->members.size(), 2);
}

TEST_F(TypeNodeTest, EnumType) {
    auto enumType = std::make_unique<EnumType>("Color");
    enumType->addValue("RED", 0);
    enumType->addValue("GREEN", 1);
    enumType->addValue("BLUE", 2);

    EXPECT_EQ(enumType->kind, TypeKind::Enum);
    EXPECT_EQ(enumType->name, "Color");
    EXPECT_EQ(enumType->values.size(), 3);
    EXPECT_EQ(enumType->values[0].second, 0);
    EXPECT_EQ(enumType->values[1].second, 1);
    EXPECT_EQ(enumType->values[2].second, 2);
}

TEST_F(TypeNodeTest, FunctionType) {
    std::vector<Type*> paramTypes = {typeCtx->getInt(), typeCtx->getFloat()};
    auto funcType = std::make_unique<FunctionType>(typeCtx->getVoid(), std::move(paramTypes));

    EXPECT_EQ(funcType->kind, TypeKind::Function);
    EXPECT_EQ(funcType->returnType->kind, TypeKind::Void);
    EXPECT_EQ(funcType->paramTypes.size(), 2);
    EXPECT_FALSE(funcType->isVarArg);
}

TEST_F(TypeNodeTest, FunctionTypeVarArg) {
    std::vector<Type*> paramTypes = {typeCtx->getInt()};
    auto funcType = std::make_unique<FunctionType>(
        typeCtx->getVoid(), std::move(paramTypes), true);

    EXPECT_TRUE(funcType->isVarArg);
}

TEST_F(TypeNodeTest, TypedefType) {
    auto typedefType = std::make_unique<TypedefType>("MyInt", typeCtx->getInt());

    EXPECT_EQ(typedefType->kind, TypeKind::Typedef);
    EXPECT_EQ(typedefType->name, "MyInt");
    EXPECT_EQ(typedefType->aliasedType->kind, TypeKind::Int);
}

TEST_F(TypeNodeTest, TypeConst) {
    auto type = new Type(TypeKind::Int);
    type->isConst = true;

    EXPECT_TRUE(type->isConst);
    EXPECT_FALSE(type->isVolatile);
    delete type;
}

TEST_F(TypeNodeTest, TypeVolatile) {
    auto type = new Type(TypeKind::Int);
    type->isVolatile = true;

    EXPECT_FALSE(type->isConst);
    EXPECT_TRUE(type->isVolatile);
    delete type;
}

TEST_F(TypeNodeTest, PointerType) {
    auto ptrType = new Type(TypeKind::Pointer, typeCtx->getInt());

    EXPECT_EQ(ptrType->kind, TypeKind::Pointer);
    EXPECT_EQ(ptrType->base->kind, TypeKind::Int);

    delete ptrType;
}

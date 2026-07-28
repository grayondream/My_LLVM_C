#include <gtest/gtest.h>
#include "ast/Decl.h"
#include "ast/Type.h"

class DeclNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        typeCtx = &TypeContext::instance();
    }

    TypeContext* typeCtx;
};

TEST_F(DeclNodeTest, ArrayDeclAST) {
    auto arrDecl = std::make_unique<ArrayDeclAST>(
        "arr", typeCtx->getInt(), 10);

    EXPECT_EQ(arrDecl->name, "arr");
    EXPECT_EQ(arrDecl->elementType->kind, TypeKind::Int);
    EXPECT_EQ(arrDecl->size, 10);
    EXPECT_EQ(arrDecl->initExpr, nullptr);
}

TEST_F(DeclNodeTest, ArrayDeclASTWithInit) {
    auto init = std::make_unique<NumberExprAST>(42);
    auto arrDecl = std::make_unique<ArrayDeclAST>(
        "arr", typeCtx->getInt(), 5, std::move(init));

    EXPECT_NE(arrDecl->initExpr, nullptr);
}

TEST_F(DeclNodeTest, StructDeclAST) {
    std::vector<std::pair<std::string, Type*>> fields;
    fields.push_back({"x", typeCtx->getInt()});
    fields.push_back({"y", typeCtx->getInt()});

    auto structDecl = std::make_unique<StructDeclAST>("Point", std::move(fields));

    EXPECT_EQ(structDecl->name, "Point");
    EXPECT_EQ(structDecl->fields.size(), 2);
    EXPECT_EQ(structDecl->fields[0].first, "x");
    EXPECT_EQ(structDecl->fields[1].first, "y");
}

TEST_F(DeclNodeTest, UnionDeclAST) {
    std::vector<std::pair<std::string, Type*>> members;
    members.push_back({"i", typeCtx->getInt()});
    members.push_back({"f", typeCtx->getFloat()});

    auto unionDecl = std::make_unique<UnionDeclAST>("Data", std::move(members));

    EXPECT_EQ(unionDecl->name, "Data");
    EXPECT_EQ(unionDecl->members.size(), 2);
}

TEST_F(DeclNodeTest, EnumDeclAST) {
    std::vector<std::pair<std::string, int>> values;
    values.push_back({"RED", 0});
    values.push_back({"GREEN", 1});
    values.push_back({"BLUE", 2});

    auto enumDecl = std::make_unique<EnumDeclAST>("Color", std::move(values));

    EXPECT_EQ(enumDecl->name, "Color");
    EXPECT_EQ(enumDecl->values.size(), 3);
    EXPECT_EQ(enumDecl->values[0].second, 0);
    EXPECT_EQ(enumDecl->values[1].second, 1);
    EXPECT_EQ(enumDecl->values[2].second, 2);
}

TEST_F(DeclNodeTest, TypedefDeclAST) {
    auto typedefDecl = std::make_unique<TypedefDeclAST>(
        "MyInt", typeCtx->getInt());

    EXPECT_EQ(typedefDecl->name, "MyInt");
    EXPECT_EQ(typedefDecl->aliasedType->kind, TypeKind::Int);
}

TEST_F(DeclNodeTest, ForwardDeclAST) {
    auto forwardDecl = std::make_unique<ForwardDeclAST>("MyStruct");
    EXPECT_EQ(forwardDecl->name, "MyStruct");
}

TEST_F(DeclNodeTest, SourceLocation) {
    auto decl = std::make_unique<VarDeclAST>("x", typeCtx->getInt());
    decl->setLocation("test.c", 5, 3);

    EXPECT_EQ(decl->sourceFile, "test.c");
    EXPECT_EQ(decl->sourceLine, 5);
    EXPECT_EQ(decl->sourceColumn, 3);
}

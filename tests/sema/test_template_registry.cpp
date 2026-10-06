// P1-03 / GEN-05: TemplateRegistry（去重/状态机/命名）与 TemplateInstantiator
// （TypeVar 重写 + AST 深克隆 + 非类型参数具体化）。直接测 Registry 单元。

#include <gtest/gtest.h>

#include <string>

#include "sema/TemplateRegistry.h"
#include "ast/Decl.h"
#include "ast/Type.h"

#include <spdlog/spdlog.h>

class TemplateRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }
};

// 手工构造 `template<typename T> struct Box { T value; };`
static std::unique_ptr<TemplateDeclAST> makeBoxTemplate() {
    auto* T = TypeContext::instance().getTypeVar("T");
    auto st = std::make_unique<StructDeclAST>("Box",
        std::vector<std::pair<std::string, Type*>>{{"value", T}});
    auto tpl = std::make_unique<TemplateDeclAST>();
    tpl->params.push_back({"T", true, nullptr});
    tpl->decl = std::move(st);
    return tpl;
}

TEST_F(TemplateRegistryTest, ClassInstanceNaming) {
    TemplateRegistry::instance().registerTemplate(makeBoxTemplate());
    auto* inst = TemplateRegistry::instance().instantiateClass(
        "Box", {TypeContext::instance().getInt32()}, {});
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->name, "Box$int32");
    ASSERT_EQ(inst->fields.size(), 1u);
    ASSERT_NE(inst->fields[0].second, nullptr);
    EXPECT_EQ(inst->fields[0].second->kind, TypeKind::Int32);
}

TEST_F(TemplateRegistryTest, InstanceDedup) {
    TemplateRegistry::instance().registerTemplate(makeBoxTemplate());
    auto* a = TemplateRegistry::instance().instantiateClass(
        "Box", {TypeContext::instance().getFloat64()}, {});
    auto* b = TemplateRegistry::instance().instantiateClass(
        "Box", {TypeContext::instance().getFloat64()}, {});
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
}

TEST_F(TemplateRegistryTest, NestedTypeRewrite) {
    // `template<typename T> struct H { Optional<T> o; T* p; };`
    auto* T = TypeContext::instance().getTypeVar("T");
    Type* opt = TypeContext::instance().getOptionalType(T);
    Type* ptr = new Type(TypeKind::Pointer, T);
    auto st = std::make_unique<StructDeclAST>("H",
        std::vector<std::pair<std::string, Type*>>{{"o", opt}, {"p", ptr}});
    auto tpl = std::make_unique<TemplateDeclAST>();
    tpl->params.push_back({"T", true, nullptr});
    tpl->decl = std::move(st);
    TemplateRegistry::instance().registerTemplate(std::move(tpl));

    auto* inst = TemplateRegistry::instance().instantiateClass(
        "H", {TypeContext::instance().getFloat64()}, {});
    ASSERT_NE(inst, nullptr);
    ASSERT_EQ(inst->fields.size(), 2u);
    EXPECT_EQ(inst->fields[0].second->kind, TypeKind::Optional);
    EXPECT_EQ(static_cast<OptionalType*>(inst->fields[0].second)->elementType->kind,
              TypeKind::Float64);
    EXPECT_EQ(inst->fields[1].second->kind, TypeKind::Pointer);
    EXPECT_EQ(inst->fields[1].second->base->kind, TypeKind::Float64);
}

TEST_F(TemplateRegistryTest, ValueParamSubstitution) {
    // `template<typename T, usize N> struct Arr { T data[N]; };`
    auto* T = TypeContext::instance().getTypeVar("T");
    auto* arr = new ArrayType(T, 0);
    static_cast<ArrayType*>(arr)->sizeParam = "N";
    auto st = std::make_unique<StructDeclAST>("Arr",
        std::vector<std::pair<std::string, Type*>>{{"data", arr}});
    auto tpl = std::make_unique<TemplateDeclAST>();
    tpl->params.push_back({"T", true, nullptr});
    tpl->params.push_back({"N", false, TypeContext::instance().getUSize()});
    tpl->decl = std::move(st);
    TemplateRegistry::instance().registerTemplate(std::move(tpl));

    auto* inst = TemplateRegistry::instance().instantiateClass(
        "Arr", {TypeContext::instance().getInt32()}, {4});
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->name, "Arr$int32$4");
    auto* fieldArr = static_cast<ArrayType*>(inst->fields[0].second);
    EXPECT_EQ(fieldArr->size, 4);
    EXPECT_TRUE(fieldArr->sizeParam.empty());
    EXPECT_EQ(fieldArr->elementType->kind, TypeKind::Int32);
}

TEST_F(TemplateRegistryTest, RecursiveInstantiationRejected) {
    // `template<typename T> struct Node { Node<T> next; };` 值语义自引用。
    auto* T = TypeContext::instance().getTypeVar("T");
    auto* self = new TypeInstanceType("Node");
    self->typeArgs.push_back(T);
    auto st = std::make_unique<StructDeclAST>("Node",
        std::vector<std::pair<std::string, Type*>>{{"next", self}});
    auto tpl = std::make_unique<TemplateDeclAST>();
    tpl->params.push_back({"T", true, nullptr});
    tpl->decl = std::move(st);
    TemplateRegistry::instance().registerTemplate(std::move(tpl));

    auto& reg = TemplateRegistry::instance();
    auto* inst = reg.instantiateClass("Node", {TypeContext::instance().getInt32()}, {});
    bool found = false;
    for (auto& e : reg.getErrors()) {
        if (e.find("recursive instantiation of template 'Node'") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found);
    (void)inst;
}

TEST_F(TemplateRegistryTest, DepthLimit) {
    // 65 层嵌套 `Box<Box<...<i32>>>`——循环构造实参类型。
    TemplateRegistry::instance().registerTemplate(makeBoxTemplate());
    Type* arg = TypeContext::instance().getInt32();
    for (int i = 0; i < 65; ++i) {
        auto* inner = new TypeInstanceType("Box");
        inner->typeArgs.push_back(arg);
        arg = inner;
    }
    auto& reg = TemplateRegistry::instance();
    reg.instantiateClass("Box", {arg}, {});
    bool found = false;
    for (auto& e : reg.getErrors()) {
        if (e.find("template instantiation depth limit exceeded") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found);
}

// P1-03 / GEN-01 / PAR-21: 模板声明解析——`template<typename T>` 函数/类/
// 别名模板 parse 成 TemplateDeclAST；模板参数名在模板体内解析为 TypeVarType。

#include <gtest/gtest.h>

#include <string>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "ast/Decl.h"
#include "ast/Type.h"

#include <spdlog/spdlog.h>

static std::unique_ptr<TranslationUnitAST> parse(const std::string& source) {
    Lexer lexer("template_test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse();
}

class TemplateParse : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }
};

static TemplateDeclAST* asTemplate(TranslationUnitAST& tu) {
    if (tu.declarations.empty()) return nullptr;
    return dynamic_cast<TemplateDeclAST*>(tu.declarations[0].get());
}

TEST_F(TemplateParse, FunctionTemplateParses) {
    auto tu = parse("template<typename T> T max(T a, T b) { return a; }");
    ASSERT_NE(tu, nullptr);
    auto* tpl = asTemplate(*tu);
    ASSERT_NE(tpl, nullptr);
    ASSERT_EQ(tpl->params.size(), 1u);
    EXPECT_TRUE(tpl->params[0].isType);
    EXPECT_EQ(tpl->params[0].name, "T");
    auto* fn = dynamic_cast<FunctionDeclAST*>(tpl->decl.get());
    ASSERT_NE(fn, nullptr);
    ASSERT_NE(fn->returnType, nullptr);
    EXPECT_EQ(fn->returnType->kind, TypeKind::TypeVar);
    ASSERT_EQ(fn->params.size(), 2u);
    EXPECT_EQ(fn->params[0]->type->kind, TypeKind::TypeVar);
}

TEST_F(TemplateParse, ClassTemplateParses) {
    auto tu = parse("template<typename T> struct Box { T value; };");
    ASSERT_NE(tu, nullptr);
    auto* tpl = asTemplate(*tu);
    ASSERT_NE(tpl, nullptr);
    auto* st = dynamic_cast<StructDeclAST*>(tpl->decl.get());
    ASSERT_NE(st, nullptr);
    ASSERT_EQ(st->fields.size(), 1u);
    ASSERT_NE(st->fields[0].type, nullptr);
    EXPECT_EQ(st->fields[0].type->kind, TypeKind::TypeVar);
}

TEST_F(TemplateParse, MixedParamsParse) {
    auto tu = parse("template<typename T, usize N> struct Arr { T data[N]; };");
    ASSERT_NE(tu, nullptr);
    auto* tpl = asTemplate(*tu);
    ASSERT_NE(tpl, nullptr);
    ASSERT_EQ(tpl->params.size(), 2u);
    EXPECT_TRUE(tpl->params[0].isType);
    EXPECT_FALSE(tpl->params[1].isType);
    EXPECT_EQ(tpl->params[1].name, "N");
    auto* st = dynamic_cast<StructDeclAST*>(tpl->decl.get());
    ASSERT_NE(st, nullptr);
    ASSERT_EQ(st->fields.size(), 1u);
    ASSERT_NE(st->fields[0].type, nullptr);
    EXPECT_EQ(st->fields[0].type->kind, TypeKind::Array);
}

TEST_F(TemplateParse, AliasTemplateParses) {
    auto tu = parse("template<typename T> using Vec = Array<T, 8>;");
    ASSERT_NE(tu, nullptr);
    auto* tpl = asTemplate(*tu);
    ASSERT_NE(tpl, nullptr);
    EXPECT_TRUE(tpl->isAlias);
    EXPECT_NE(dynamic_cast<UsingDeclAST*>(tpl->decl.get()), nullptr);
}

TEST_F(TemplateParse, ClassParamRejected) {
    auto tu = parse("template<class T> T max(T a) { return a; }");
    // 语法可以失败（nullptr）或带诊断，但必须有钉死的文案。
    if (tu == nullptr || true) {
        Lexer lexer("template_test.c", "template<class T> T max(T a) { return a; }");
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        parser.parse();
        bool found = false;
        for (auto& d : parser.getErrors()) {
            if (d.message.find("use 'typename' instead of 'class'") != std::string::npos)
                found = true;
        }
        EXPECT_TRUE(found);
    }
}

TEST_F(TemplateParse, TemplateOnUnionRejected) {
    Lexer lexer("template_test.c", "template<typename T> union U { T a; };");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    parser.parse();
    bool found = false;
    for (auto& d : parser.getErrors()) {
        if (d.message.find("'template' is not supported on unions/enums") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found);
}

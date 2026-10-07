// P1-05 / ANN-01 / LEX-10: `[[...]]` 注解解析——值类型、挂载点、连写。

#include <gtest/gtest.h>

#include <string>

#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "ast/Decl.h"
#include "ast/Annotation.h"

#include <spdlog/spdlog.h>

static std::unique_ptr<TranslationUnitAST> parse(const std::string& source) {
    Lexer lexer("annotation_parse_test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse();
}

class AnnotationParse : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }
};

TEST_F(AnnotationParse, DeclAnnotationParses) {
    auto tu = parse("[[inline]] int32 f() { return 0; }");
    ASSERT_NE(tu, nullptr);
    ASSERT_FALSE(tu->declarations.empty());
    auto* fn = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->annotations.size(), 1u);
    EXPECT_EQ(fn->annotations[0].name, "inline");
}

TEST_F(AnnotationParse, MultipleAnnotationsStack) {
    auto tu = parse("[[cold]] [[inline]] int32 f() { return 0; }");
    ASSERT_NE(tu, nullptr);
    auto* fn = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->annotations.size(), 2u);
    EXPECT_EQ(fn->annotations[0].name, "cold");
    EXPECT_EQ(fn->annotations[1].name, "inline");
}

TEST_F(AnnotationParse, AnnotationWithArgs) {
    auto tu = parse("[[deprecated(\"old\")]] int32 g() { return 0; }");
    ASSERT_NE(tu, nullptr);
    auto* fn = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->annotations.size(), 1u);
    ASSERT_EQ(fn->annotations[0].args.size(), 1u);
    EXPECT_EQ(fn->annotations[0].args[0].kind, AnnotationArg::Kind::String);
    EXPECT_EQ(fn->annotations[0].args[0].text, "old");
}

TEST_F(AnnotationParse, AlignArgIsExpr) {
    auto tu = parse("[[align(16)]] struct S { int32 x; }");
    ASSERT_NE(tu, nullptr);
    auto* s = dynamic_cast<StructDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->annotations.size(), 1u);
    ASSERT_EQ(s->annotations[0].args.size(), 1u);
    EXPECT_EQ(s->annotations[0].args[0].kind, AnnotationArg::Kind::Expr);
    EXPECT_NE(s->annotations[0].args[0].expr, nullptr);
}

TEST_F(AnnotationParse, ModuleAnnotationParses) {
    auto tu = parse("[[deprecated]] module m;");
    ASSERT_NE(tu, nullptr);
    ModuleDeclAST* mod = nullptr;
    for (auto& d : tu->declarations) {
        if ((mod = dynamic_cast<ModuleDeclAST*>(d.get()))) break;
    }
    ASSERT_NE(mod, nullptr);
    ASSERT_EQ(mod->annotations.size(), 1u);
    EXPECT_EQ(mod->annotations[0].name, "deprecated");
}

TEST_F(AnnotationParse, LocalVarAnnotationParses) {
    auto tu = parse("int32 main() { [[align(8)]] int32 x; return 0; }");
    ASSERT_NE(tu, nullptr);
    auto* main = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(main, nullptr);
    ASSERT_FALSE(main->body->stmts.empty());
    auto* declStmt = dynamic_cast<DeclStmtAST*>(main->body->stmts[0].get());
    ASSERT_NE(declStmt, nullptr);
    auto* var = dynamic_cast<VarDeclAST*>(declStmt->decl.get());
    ASSERT_NE(var, nullptr);
    ASSERT_EQ(var->annotations.size(), 1u);
    EXPECT_EQ(var->annotations[0].name, "align");
    ASSERT_EQ(var->annotations[0].args.size(), 1u);
    EXPECT_EQ(var->annotations[0].args[0].kind, AnnotationArg::Kind::Expr);
}

TEST_F(AnnotationParse, AnnotationNamesAreNotKeywords) {
    auto tu = parse("int32 main() { int32 packed = 1; return packed; }");
    ASSERT_NE(tu, nullptr);
    EXPECT_TRUE(tu->declarations.empty() == false);
    // parse 无错误即可（Review Focus 3）：注解名非关键字。
    Lexer lexer("ann_kw2.c", "int32 main() { int32 packed = 1; return packed; }");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto tu2 = parser.parse();
    EXPECT_TRUE(parser.getErrors().empty());
    (void)tu2;
    (void)tu;
}

TEST_F(AnnotationParse, FieldAnnotationParses) {
    auto tu = parse("struct S { [[packed]] int32 x; }");
    ASSERT_NE(tu, nullptr);
    auto* s = dynamic_cast<StructDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->fields.size(), 1u);
    ASSERT_EQ(s->fields[0].annotations.size(), 1u);
    EXPECT_EQ(s->fields[0].annotations[0].name, "packed");
}

TEST_F(AnnotationParse, ParamAnnotationParses) {
    auto tu = parse("int32 f([[nonnull]] int32* p) { return 0; }");
    ASSERT_NE(tu, nullptr);
    auto* fn = dynamic_cast<FunctionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->params.size(), 1u);
    ASSERT_EQ(fn->params[0]->annotations.size(), 1u);
    EXPECT_EQ(fn->params[0]->annotations[0].name, "nonnull");
}

TEST_F(AnnotationParse, UnionFieldAnnotationParses) {
    auto tu = parse("union U { [[deprecated]] int32 i; }");
    ASSERT_NE(tu, nullptr);
    auto* u = dynamic_cast<UnionDeclAST*>(tu->declarations[0].get());
    ASSERT_NE(u, nullptr);
    ASSERT_EQ(u->members.size(), 1u);
    ASSERT_EQ(u->members[0].annotations.size(), 1u);
    EXPECT_EQ(u->members[0].annotations[0].name, "deprecated");
}

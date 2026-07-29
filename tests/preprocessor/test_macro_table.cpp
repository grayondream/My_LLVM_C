#include <gtest/gtest.h>
#include "preprocessor/MacroTable.h"

using TokenType::TOKEN_NUMBER;
using TokenType::TOKEN_LPAREN;
using TokenType::TOKEN_RPAREN;
using TokenType::TOKEN_IDENTIFIER;

class MacroTableTest : public ::testing::Test {
protected:
    MacroTable table;
};

TEST_F(MacroTableTest, DefineObjectLikeMacro) {
    Macro m;
    m.name = "PI";
    m.body.push_back(Token(TOKEN_NUMBER, "3.14"));
    table.define(m);

    const Macro* found = table.lookup("PI");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "PI");
    EXPECT_EQ(found->params.size(), 0u);
    EXPECT_EQ(found->body.size(), 1u);
    EXPECT_EQ(found->body[0].lexeme, "3.14");
    EXPECT_FALSE(found->isVariadic);
}

TEST_F(MacroTableTest, DefineFunctionLikeMacro) {
    Macro m;
    m.name = "MAX";
    m.params = {"a", "b"};
    m.body.push_back(Token(TOKEN_LPAREN, "("));
    m.body.push_back(Token(TOKEN_IDENTIFIER, "a"));
    m.body.push_back(Token(TOKEN_RPAREN, ")"));
    m.isVariadic = false;
    table.define(m);

    const Macro* found = table.lookup("MAX");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "MAX");
    EXPECT_EQ(found->params.size(), 2u);
    EXPECT_EQ(found->params[0], "a");
    EXPECT_EQ(found->params[1], "b");
    EXPECT_EQ(found->body.size(), 3u);
    EXPECT_FALSE(found->isVariadic);
}

TEST_F(MacroTableTest, DefineVariadicMacro) {
    Macro m;
    m.name = "PRINTF";
    m.params = {"fmt"};
    m.isVariadic = true;
    m.body.push_back(Token(TOKEN_IDENTIFIER, "fmt"));
    table.define(m);

    const Macro* found = table.lookup("PRINTF");
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->isVariadic);
    EXPECT_EQ(found->params.size(), 1u);
}

TEST_F(MacroTableTest, UndefineMacro) {
    Macro m;
    m.name = "TEMP";
    table.define(m);
    EXPECT_TRUE(table.isDefined("TEMP"));

    table.undefine("TEMP");
    EXPECT_FALSE(table.isDefined("TEMP"));
    EXPECT_EQ(table.lookup("TEMP"), nullptr);
}

TEST_F(MacroTableTest, IsDefinedCheck) {
    EXPECT_FALSE(table.isDefined("NOT_DEFINED"));

    Macro m;
    m.name = "DEFINED";
    table.define(m);
    EXPECT_TRUE(table.isDefined("DEFINED"));
}

TEST_F(MacroTableTest, OverrideMacro) {
    Macro m1;
    m1.name = "X";
    m1.body.push_back(Token(TOKEN_NUMBER, "1"));
    table.define(m1);
    EXPECT_EQ(table.lookup("X")->body[0].lexeme, "1");

    Macro m2;
    m2.name = "X";
    m2.body.push_back(Token(TOKEN_NUMBER, "2"));
    table.define(m2);
    EXPECT_EQ(table.lookup("X")->body[0].lexeme, "2");
}

TEST_F(MacroTableTest, LookupNonexistentReturnsNullptr) {
    EXPECT_EQ(table.lookup("NOPE"), nullptr);
}

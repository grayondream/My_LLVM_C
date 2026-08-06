#include <gtest/gtest.h>
#include "frontend/Lexer.h"

TEST(DummyTest, AlwaysPasses) {
    EXPECT_EQ(1 + 1, 2);
}

TEST(ClassKeywordTest, ClassTokenIsRecognized) {
    std::string source = "class Foo { int x; };";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();

    ASSERT_GE(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_CLASS);
    EXPECT_EQ(tokens[0].lexeme, "class");
}

TEST(ClassKeywordTest, ClassKeywordNotIdentifier) {
    std::string source = "class";
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TokenType::TOKEN_CLASS);
    EXPECT_NE(tokens[0].type, TokenType::TOKEN_IDENTIFIER);
}

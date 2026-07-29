#include <gtest/gtest.h>
#include "preprocessor/Preprocessor.h"

using TokenType::TOKEN_NUMBER;
using TokenType::TOKEN_IDENTIFIER;
using TokenType::TOKEN_INT;

class PreprocessorTest : public ::testing::Test {
protected:
    Preprocessor pp;
};

TEST_F(PreprocessorTest, NoDirectivesPassThrough) {
    std::string source = "int x = 42;";
    Lexer lexer("test.c", source);
    auto tokens = pp.preprocess(lexer);
    
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TOKEN_INT);
}

TEST_F(PreprocessorTest, ObjectLikeMacroExpansion) {
    std::string source = "#define VAL 100\nint x = VAL;";
    Lexer lexer("test.c", source);
    auto tokens = pp.preprocess(lexer);
    
    bool found100 = false;
    for (const auto& tok : tokens) {
        if (tok.lexeme == "100" && tok.type == TOKEN_NUMBER) {
            found100 = true;
            break;
        }
    }
    EXPECT_TRUE(found100);
}

TEST_F(PreprocessorTest, FunctionLikeMacroExpansion) {
    std::string source = "#define MAX(a, b) a\nint x = MAX(1, 2);";
    Lexer lexer("test.c", source);
    auto tokens = pp.preprocess(lexer);
    
    bool foundMacro = false;
    for (const auto& tok : tokens) {
        if (tok.lexeme == "1") {
            foundMacro = true;
            break;
        }
    }
    EXPECT_TRUE(foundMacro);
}

TEST_F(PreprocessorTest, IfdefConditionalCompilation) {
    std::string source = "#ifdef FOO\nint x = 1;\n#endif\nint y = 2;";
    Lexer lexer("test.c", source);
    auto tokens = pp.preprocess(lexer);
    
    bool foundY = false;
    for (const auto& tok : tokens) {
        if (tok.lexeme == "y") {
            foundY = true;
            break;
        }
    }
    EXPECT_TRUE(foundY);
    
    bool foundX = false;
    for (const auto& tok : tokens) {
        if (tok.lexeme == "x") {
            foundX = true;
            break;
        }
    }
    EXPECT_FALSE(foundX);
}

TEST_F(PreprocessorTest, IfndefConditionalCompilation) {
    std::string source = "#ifndef BAR\nint a = 10;\n#endif\nint b = 20;";
    Lexer lexer("test.c", source);
    auto tokens = pp.preprocess(lexer);
    
    bool foundA = false;
    for (const auto& tok : tokens) {
        if (tok.lexeme == "a") {
            foundA = true;
            break;
        }
    }
    EXPECT_TRUE(foundA);
    
    bool foundB = false;
    for (const auto& tok : tokens) {
        if (tok.lexeme == "b") {
            foundB = true;
            break;
        }
    }
    EXPECT_TRUE(foundB);
}

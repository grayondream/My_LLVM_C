#include <gtest/gtest.h>
#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "sema/SemanticAnalyzer.h"

class NewFeaturesTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::off);
    }
};

TEST_F(NewFeaturesTest, NewKeywords) {
    std::string source = R"(
        int main() {
            bool x = true;
            int32 y = 10;
            uint64 z = 100;
            return 0;
        }
    )";
    
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    
    // 检查是否正确识别了新的关键字
    bool foundBool = false;
    bool foundTrue = false;
    bool foundInt32 = false;
    bool foundUInt64 = false;
    
    for (const auto& token : tokens) {
        if (token.type == TokenType::TOKEN_BOOL) foundBool = true;
        if (token.type == TokenType::TOKEN_TRUE) foundTrue = true;
        if (token.type == TokenType::TOKEN_INT32) foundInt32 = true;
        if (token.type == TokenType::TOKEN_UINT64) foundUInt64 = true;
    }
    
    EXPECT_TRUE(foundBool);
    EXPECT_TRUE(foundTrue);
    EXPECT_TRUE(foundInt32);
    EXPECT_TRUE(foundUInt64);
}

TEST_F(NewFeaturesTest, BinaryLiterals) {
    std::string source = R"(
        int main() {
            int x = 0b1010;
            return x;
        }
    )";
    
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    
    // 查找数字字面量
    bool foundBinary = false;
    for (const auto& token : tokens) {
        if (token.type == TokenType::TOKEN_NUMBER) {
            // 检查是否是二进制字面量
            std::string lexeme = token.lexeme;
            if (lexeme.find("0b") != std::string::npos || lexeme.find("0B") != std::string::npos) {
                foundBinary = true;
                // 检查值是否正确 (0b1010 = 10)
                int value = std::get<int>(token.value);
                EXPECT_EQ(value, 10);
            }
        }
    }
    
    EXPECT_TRUE(foundBinary);
}

TEST_F(NewFeaturesTest, OctalLiterals) {
    std::string source = R"(
        int main() {
            int x = 0o755;
            return x;
        }
    )";
    
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    
    // 查找数字字面量
    bool foundOctal = false;
    for (const auto& token : tokens) {
        if (token.type == TokenType::TOKEN_NUMBER) {
            // 检查是否是八进制字面量
            std::string lexeme = token.lexeme;
            if (lexeme.find("0o") != std::string::npos || lexeme.find("0O") != std::string::npos) {
                foundOctal = true;
                // 检查值是否正确 (0o755 = 493)
                int value = std::get<int>(token.value);
                EXPECT_EQ(value, 493);
            }
        }
    }
    
    EXPECT_TRUE(foundOctal);
}

TEST_F(NewFeaturesTest, UnderscoreLiterals) {
    std::string source = R"(
        int main() {
            int x = 1_000_000;
            return x;
        }
    )";
    
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    
    // 查找数字字面量
    bool foundUnderscore = false;
    for (const auto& token : tokens) {
        if (token.type == TokenType::TOKEN_NUMBER) {
            // 检查是否包含下划线
            std::string lexeme = token.lexeme;
            if (lexeme.find("_") != std::string::npos) {
                foundUnderscore = true;
                // 检查值是否正确
                int value = std::get<int>(token.value);
                EXPECT_EQ(value, 1000000);
            }
        }
    }
    
    EXPECT_TRUE(foundUnderscore);
}

TEST_F(NewFeaturesTest, TypeAlias) {
    std::string source = R"(
        using Size = int32;
        int main() {
            Size x = 10;
            return x;
        }
    )";
    
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    
    EXPECT_NE(ast, nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
}

TEST_F(NewFeaturesTest, DeferStatement) {
    std::string source = R"(
        void close(int fd);
        int main() {
            int fd = 5;
            defer close(fd);
            return 0;
        }
    )";
    
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    
    EXPECT_NE(ast, nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
}

TEST_F(NewFeaturesTest, ModuleSystem) {
    std::string source = R"(
        module mymodule;
        import stdio;
        export main;
        
        int main() {
            return 0;
        }
    )";
    
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    
    EXPECT_NE(ast, nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
}

TEST_F(NewFeaturesTest, SliceType) {
    std::string source = R"(
        int main() {
            int[] data;
            return 0;
        }
    )";
    
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    
    EXPECT_NE(ast, nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
}

TEST_F(NewFeaturesTest, OptionalType) {
    std::string source = R"(
        int main() {
            int? value;
            return 0;
        }
    )";
    
    Lexer lexer("test.c", source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    
    EXPECT_NE(ast, nullptr);
    EXPECT_TRUE(parser.getErrors().empty());
}
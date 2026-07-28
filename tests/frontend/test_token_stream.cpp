#include <gtest/gtest.h>
#include "frontend/Lexer.h"
#include "frontend/TokenStream.h"

class TokenStreamTest : public ::testing::Test {
protected:
    std::unique_ptr<Lexer> lexer;

    TokenStream makeStream(const std::string& source) {
        lexer = std::make_unique<Lexer>("test.c", source);
        return TokenStream(*lexer);
    }
};

TEST_F(TokenStreamTest, PeekDoesNotConsume) {
    auto stream = makeStream("1 + 2");
    const auto& tok1 = stream.peek();
    EXPECT_EQ(tok1.type, TokenType::TOKEN_NUMBER);
    const auto& tok2 = stream.peek();
    EXPECT_EQ(tok2.type, TokenType::TOKEN_NUMBER);
}

TEST_F(TokenStreamTest, ConsumeAdvancesPosition) {
    auto stream = makeStream("1 + 2");
    auto tok1 = stream.consume();
    EXPECT_EQ(tok1.type, TokenType::TOKEN_NUMBER);
    auto tok2 = stream.consume();
    EXPECT_EQ(tok2.type, TokenType::TOKEN_PLUS);
}

TEST_F(TokenStreamTest, ExpectValidToken) {
    auto stream = makeStream("1 + 2");
    auto tok = stream.expect(TokenType::TOKEN_NUMBER);
    EXPECT_EQ(tok.type, TokenType::TOKEN_NUMBER);
}

TEST_F(TokenStreamTest, ExpectInvalidTokenThrows) {
    auto stream = makeStream("1 + 2");
    EXPECT_THROW(stream.expect(TokenType::TOKEN_INT), std::runtime_error);
}

TEST_F(TokenStreamTest, MatchAndConsume) {
    auto stream = makeStream("1 + 2");
    EXPECT_TRUE(stream.match(TokenType::TOKEN_NUMBER));
    auto tok = stream.consume();
    EXPECT_EQ(tok.type, TokenType::TOKEN_PLUS);
}

TEST_F(TokenStreamTest, MatchFailsNoConsume) {
    auto stream = makeStream("1 + 2");
    EXPECT_FALSE(stream.match(TokenType::TOKEN_INT));
    const auto& tok = stream.peek();
    EXPECT_EQ(tok.type, TokenType::TOKEN_NUMBER);
}

TEST_F(TokenStreamTest, AtEndDetection) {
    auto stream = makeStream("");
    EXPECT_TRUE(stream.atEnd());
}

TEST_F(TokenStreamTest, AtEndWithTokens) {
    auto stream = makeStream("1");
    EXPECT_FALSE(stream.atEnd());
    stream.consume();
    EXPECT_TRUE(stream.atEnd());
}

TEST_F(TokenStreamTest, PeekWithOffset) {
    auto stream = makeStream("1 + 2");
    EXPECT_EQ(stream.peek(0).type, TokenType::TOKEN_NUMBER);
    EXPECT_EQ(stream.peek(1).type, TokenType::TOKEN_PLUS);
    EXPECT_EQ(stream.peek(2).type, TokenType::TOKEN_NUMBER);
}

TEST_F(TokenStreamTest, MultipleConsumesUntilEnd) {
    auto stream = makeStream("a b c");
    EXPECT_EQ(stream.consume().type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(stream.consume().type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_EQ(stream.consume().type, TokenType::TOKEN_IDENTIFIER);
    EXPECT_TRUE(stream.atEnd());
}

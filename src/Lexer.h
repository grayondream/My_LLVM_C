#pragma once

#include <cstdint>
#include <variant>
#include <vector>
#include <string>
#include "Token.h"

class Lexer{
public:
    Lexer(const std::string& filename, const std::string& source);
    ~Lexer();
    std::vector<Token> tokenize();

private:
    bool isEof() const;
    char peek() const;
    char peekNext() const;
    char advance();
    char advanceNextLine();
    std::string lexeme();

    bool match(const char expected);
    std::string lexname();
    Token makeToken(const TokenType type, const std::string& lexeme, const TokenValue value = std::monostate());

    void error(const std::string& msg) const;
    void skipWhitespace();
    void skipComment();
    Token scanToken();

    Token scanIdentifier();
    Token scanNumber();
    Token scanString();
    Token scanChar();
private:
    std::string m_source;
    std::string m_filename;
    size_t m_startPos{};
    size_t m_currentPos{};
    size_t m_lineNum{};
    size_t m_colNum{};
};
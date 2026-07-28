#pragma once

#include "Token.h"
#include "Lexer.h"
#include <vector>
#include <string>

class TokenStream {
    Lexer& lexer;
    std::vector<Token> buffer;
    size_t pos = 0;
    bool ended = false;

    void ensureBuffered(size_t offset);

public:
    explicit TokenStream(Lexer& lexer);

    const Token& peek(size_t offset = 0);
    Token consume();
    Token expect(TokenType type);
    bool match(TokenType type);
    bool atEnd();
    const Token& current();
};

#include "TokenStream.h"
#include <sstream>
#include <stdexcept>

TokenStream::TokenStream(Lexer& lexer) : lexer(lexer) {}

void TokenStream::ensureBuffered(size_t offset) {
    while (buffer.size() <= pos + offset && !ended) {
        auto token = lexer.nextToken();
        if (token.type == TokenType::TOKEN_EOS) {
            ended = true;
            buffer.push_back(token);
        } else {
            buffer.push_back(std::move(token));
        }
    }
}

const Token& TokenStream::peek(size_t offset) {
    ensureBuffered(offset);
    return buffer[pos + offset];
}

Token TokenStream::consume() {
    ensureBuffered(0);
    if (atEnd()) {
        return buffer.back();
    }
    return std::move(buffer[pos++]);
}

Token TokenStream::expect(TokenType type) {
    auto tok = consume();
    if (tok.type != type) {
        std::ostringstream oss;
        oss << "expected token type " << static_cast<int>(type)
            << " but got " << static_cast<int>(tok.type)
            << " at line " << tok.line << ":" << tok.column;
        throw std::runtime_error(oss.str());
    }
    return tok;
}

bool TokenStream::match(TokenType type) {
    if (peek().type == type) {
        consume();
        return true;
    }
    return false;
}

bool TokenStream::atEnd() {
    ensureBuffered(0);
    return ended && pos >= buffer.size() - 1;
}

const Token& TokenStream::current() {
    ensureBuffered(0);
    return buffer[pos];
}

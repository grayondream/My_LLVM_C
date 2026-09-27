#pragma once

#include <cstdint>
#include <variant>
#include <vector>
#include <string>
#include "frontend/Token.h"
#include "sema/Diagnostic.h"

class Lexer{
public:
    Lexer(const std::string& filename, const std::string& source);
    ~Lexer();
    std::vector<Token> tokenize();
    Token nextToken();

    // LEX-13 / LEX-17: structured diagnostics collected while scanning
    // (invalid character E0001, unterminated literal E0002, integer literal
    // overflow E0003). Scanning continues after an error so the rest of the
    // token stream stays usable.
    const std::vector<Diagnostic>& getDiagnostics() const;

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

    // LEX-13/17: record a diagnostic at an explicit source position. Scanning
    // continues afterwards (error recovery) instead of throwing.
    void report(DiagnosticCode code, const std::string& msg, size_t line, size_t col);
    void skipWhitespace();
    void skipComment();
    // Skip any interleaving of whitespace and comments (LEX-13): a line comment
    // may be followed by blank lines and another comment before the next token.
    void skipTrivia();
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
    size_t m_lineNum{1};
    size_t m_colNum{};
    // Start position of the token currently being scanned (INF-03): recorded at
    // the beginning of scanToken so diagnostics point at the token's first
    // character rather than its last.
    size_t m_tokenStartLine{1};
    size_t m_tokenStartCol{1};
    std::vector<Diagnostic> m_diagnostics;
};
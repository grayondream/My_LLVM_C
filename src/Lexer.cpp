#include "Lexer.h"
#include "Token.h"
#include <stdexcept>
        
Lexer::Lexer(const std::string& source, const std::string& filename)
    : m_source(source), m_filename(filename) {
}

Lexer::~Lexer() {
    m_source.clear();
    m_filename.clear();
}

bool Lexer::isEof() const {
    return m_currentPos >= m_source.size();
}

char Lexer::peek() const {
    return isEof() ? '\0' : m_source[m_currentPos];
}

char Lexer::peekNext() const {
    return isEof() ? '\0' : m_source[m_currentPos + 1];
}

char Lexer::advance() {
    const char ch = peek();
    if(ch == '\n') {
        m_lineNum++;
        m_colNum = 0;
    } else {
        m_colNum++;
    }

    m_currentPos++;
    return ch;
}

char Lexer::advanceNextLine() {
    while(!isEof() && peek() != '\n') {
        advance();
    }

    return advance();
}

bool Lexer::match(const char expected) {
    if(isEof()) {
        return false;
    }

    if(peek() != expected) {
        return false;
    }

    advance();
    return true;
}
        
std::string Lexer::lexname() {
    return m_source.substr(m_startPos, m_currentPos - m_startPos);
}

Token Lexer::makeToken(const TokenType type, const std::string& lexeme, const TokenValue value) {
    return Token{type, lexeme, value, m_filename, (int)m_lineNum, (int)m_colNum};
}

void Lexer::error(const std::string& msg) const {
    throw std::runtime_error(m_filename + ": " + msg + " at line " + std::to_string(m_lineNum) + ", col " + std::to_string(m_colNum));
}
        
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens{};
    while(!isEof()) {
        skipWhitespace();
        skipComment();
        m_startPos = m_currentPos;  
        if(isEof()) {
            break;
        }
        tokens.push_back(scanToken());
    }
        
    return tokens;
}

void Lexer::skipWhitespace() {
    while(true){
        const char ch = peek();
        switch(ch){
        case ' ':
        case '\t':
        case '\r':
        case '\v':
        case '\n':
            advance();
            break;
        default:
            break;
        }
    }

    return;
}
        
void Lexer::skipComment() {
    while(true){
        const char ch = peek();
        if(ch == '/' and peekNext() == '/') {
            advanceNextLine();
        } else if(ch == '/' and peekNext() == '*') {
            advance();advance();
            while(!isEof() && peek() != '*' and peekNext() != '/') {
                advance();
            }
            
            if(!isEof()) {
                advance();
                advance();
            }
        } else {
            break;
        }
    }
}
        

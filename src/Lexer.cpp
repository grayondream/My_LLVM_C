#include "Lexer.h"
#include <stdexcept>
#include <unordered_map>
#include "Token.h"
#include "Log.h"


static const std::unordered_map<std::string, TokenType> keywordMap = {
    // ===== 基本类型 =====
    {"int", TokenType::TOKEN_INT},
    {"float", TokenType::TOKEN_FLOAT},
    {"double", TokenType::TOKEN_DOUBLE},
    {"char", TokenType::TOKEN_CHAR_KW},
    {"void", TokenType::TOKEN_VOID},

    // ===== 控制流 =====
    {"if", TokenType::TOKEN_IF},
    {"else", TokenType::TOKEN_ELSE},
    {"switch", TokenType::TOKEN_SWITCH},
    {"case", TokenType::TOKEN_CASE},
    {"default", TokenType::TOKEN_DEFAULT},

    {"for", TokenType::TOKEN_FOR},
    {"while", TokenType::TOKEN_WHILE},
    {"do", TokenType::TOKEN_DO},

    {"break", TokenType::TOKEN_BREAK},
    {"continue", TokenType::TOKEN_CONTINUE},
    {"return", TokenType::TOKEN_RETURN},

    // ===== 复合类型 =====
    {"struct", TokenType::TOKEN_STRUCT},
    {"union", TokenType::TOKEN_UNION},
    {"enum", TokenType::TOKEN_ENUM},

    // ===== 存储类 / 修饰符 =====
    {"const", TokenType::TOKEN_CONST},
    {"static", TokenType::TOKEN_STATIC},
    {"extern", TokenType::TOKEN_EXTERN},
    {"volatile", TokenType::TOKEN_VOLATILE},
    {"register", TokenType::TOKEN_REGISTER},

    // ===== sizeof / 类型 =====
    {"sizeof", TokenType::TOKEN_SIZEOF},
    {"typedef", TokenType::TOKEN_TYPEDEF},

    // ===== C99 / 扩展（可选）=====
    {"inline", TokenType::TOKEN_INLINE},
    {"restrict", TokenType::TOKEN_RESTRICT},

    // ===== 布尔（如果你支持）=====
    {"bool", TokenType::TOKEN_BOOL},

    // ===== 空语句 / goto（可选）=====
    {"goto", TokenType::TOKEN_GOTO}
};

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
    auto token = Token{type, lexeme, value, m_filename, (int)m_lineNum, (int)m_colNum};
    LOGD("makeToken: {}", to_string(token));
    return token;
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

std::string Lexer::lexeme() {
    return m_source.substr(m_startPos, m_currentPos - m_startPos);
}

Token Lexer::scanIdentifier() {
    while(true){
        const char ch = peek();
        if(std::isalnum(ch) or ch == '_') {
            advance();
        } else {
            break;
        }
    }

    const auto lexName = lexeme();
    auto it = keywordMap.find(lexName);
    if(it != keywordMap.end()) {
        return makeToken(it->second, lexName);
    }

    return makeToken(TokenType::TOKEN_IDENTIFIER, lexName);
}

Token Lexer::scanNumber(){
    while(true){
        const char ch = peek();
        if(std::isdigit(ch)) {
            advance();
        } else {
            break;
        }
    }

    bool isfloat = false;
    if(peek() == '.' and std::isdigit(peekNext())) {
        isfloat = true;
        while(true){
            const char ch = peek();
            if(std::isdigit(ch)) {
                advance();
            } else {
                break;
            }
        }
    }

    const auto lexName = lexeme();
    if(isfloat) {
        return makeToken(TokenType::TOKEN_FLOAT, lexName, std::stod(lexName));
    } else {
        return makeToken(TokenType::TOKEN_NUMBER, lexName, std::stoi(lexName));
    }
}

Token Lexer::scanString() {
    while(true){
        const char ch = peek();
        if(ch == '"') {
            advance();
            break;
        } else {
            advance();
        }
    }

    return makeToken(TokenType::TOKEN_STRING, lexeme());
}

Token Lexer::scanChar() {
    while(true){
        const char ch = peek();
        if(ch == '\'') {
            advance();
            break;
        } else {
            advance();
        }
    }

    return makeToken(TokenType::TOKEN_CHAR, lexeme());
}
        
Token Lexer::scanToken() {
    const char ch = peek();
    if(std::isalpha(ch) or ch == '_') {
        return scanIdentifier();
    }else if(std::isdigit(ch)) {
        return scanNumber();
    }

    switch(ch){
    case '"':
        return scanString();
    case '\'':
        return scanChar();
    case '+':
        return makeToken(TokenType::TOKEN_PLUS, lexeme());
    case '-':
        return makeToken(TokenType::TOKEN_MINUS, lexeme());
    case '*':
        return makeToken(TokenType::TOKEN_STAR, lexeme());
    case '/':
        return makeToken(TokenType::TOKEN_SLASH, lexeme());
    case '%':
        return makeToken(TokenType::TOKEN_PERCENT, lexeme());
    case '=':
    case '!=':
    case '<':
    case '>':
    case '&&':
    case '||':
        return makeToken(TokenType::TOKEN_OPERATOR, std::string(1, ch));
    default:
        error("unknown operator");
    }
}



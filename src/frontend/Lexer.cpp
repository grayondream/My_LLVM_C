#include "Lexer.h"
#include <stdexcept>
#include <unordered_map>
#include "frontend/Token.h"
#include "support/Log.h"
#include "support/Utils.h"

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
    {"class", TokenType::TOKEN_CLASS},

    // ===== 存储类 / 修饰符 =====
    {"const", TokenType::TOKEN_CONST},
    {"constexpr", TokenType::TOKEN_CONSTEXPR},
    {"static", TokenType::TOKEN_STATIC},
    {"extern", TokenType::TOKEN_EXTERN},
    {"volatile", TokenType::TOKEN_VOLATILE},

    // ===== sizeof / 类型 =====
    {"sizeof", TokenType::TOKEN_SIZEOF},
    {"typedef", TokenType::TOKEN_TYPEDEF},
    {"operator", TokenType::TOKEN_OPERATOR},

    // ===== C99 / 扩展（可选）=====
    {"inline", TokenType::TOKEN_INLINE},
    {"restrict", TokenType::TOKEN_RESTRICT},

    // ===== 布尔（如果你支持）=====
    {"bool", TokenType::TOKEN_BOOL},

    // ===== 空语句 / goto（可选）=====
    {"goto", TokenType::TOKEN_GOTO},

    // ===== 新增关键字 =====
    {"true", TokenType::TOKEN_TRUE},
    {"false", TokenType::TOKEN_FALSE},
    {"null", TokenType::TOKEN_NULL},
    {"module", TokenType::TOKEN_MODULE},
    {"import", TokenType::TOKEN_IMPORT},
    {"export", TokenType::TOKEN_EXPORT},
    {"public", TokenType::TOKEN_PUBLIC},
    {"private", TokenType::TOKEN_PRIVATE},
    {"defer", TokenType::TOKEN_DEFER},
    {"alignof", TokenType::TOKEN_ALIGNOF},
    {"offsetof", TokenType::TOKEN_OFFSETOF},

    // ===== 新增整数类型 =====
    {"int8", TokenType::TOKEN_INT8},
    {"int16", TokenType::TOKEN_INT16},
    {"int32", TokenType::TOKEN_INT32},
    {"int64", TokenType::TOKEN_INT64},
    {"int128", TokenType::TOKEN_INT128},
    {"uint8", TokenType::TOKEN_UINT8},
    {"uint16", TokenType::TOKEN_UINT16},
    {"uint32", TokenType::TOKEN_UINT32},
    {"uint64", TokenType::TOKEN_UINT64},
    {"uint128", TokenType::TOKEN_UINT128},
    {"isize", TokenType::TOKEN_ISIZE},
    {"usize", TokenType::TOKEN_USIZE},

    // ===== 新增浮点类型 =====
    {"float32", TokenType::TOKEN_FLOAT32},
    {"float64", TokenType::TOKEN_FLOAT64}
};

Lexer::Lexer(const std::string& filename, const std::string& source)
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
    LOGI("makeToken: {}", to_string(token));
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

        auto token = scanToken();
        if(token.type != TokenType::TOKEN_UNKNOWN) {
            tokens.push_back(token);
        }
    }
        
    return tokens;
}

Token Lexer::nextToken() {
    while(!isEof()) {
        skipWhitespace();
        skipComment();
        m_startPos = m_currentPos;
        if(isEof()) {
            break;
        }

        auto token = scanToken();
        if(token.type != TokenType::TOKEN_UNKNOWN) {
            return token;
        }
    }

    return makeToken(TokenType::TOKEN_EOS, "");
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
            return;
        }
    }

    return;
}
        
void Lexer::skipComment() {
    while (true) {
        const char ch = peek();
        const char nextCh = peekNext();
        if (ch == '/' && nextCh == '/') {
            advanceNextLine();
        } else if (ch == '/' && nextCh == '*') {
            advance(); 
            advance(); 

            while (!isEof() && !(peek() == '*' && peekNext() == '/')) {
                advance();
            }

            if (isEof()) {
                return;
            }

            advance(); 
            advance(); 
        }
        else {
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
    // 检查是否是二进制、八进制或十六进制
    if(peek() == '0' && !isEof()) {
        char next = peekNext();
        if(next == 'b' || next == 'B') {
            // 二进制字面量
            advance(); // 消耗 '0'
            advance(); // 消耗 'b' 或 'B'
            while(true){
                const char ch = peek();
                if(ch == '0' || ch == '1' || ch == '_') {
                    advance();
                } else {
                    break;
                }
            }
            const auto lexName = lexeme();
            std::string binaryStr;
            for(size_t i = 2; i < lexName.size(); i++) {
                if(lexName[i] != '_') binaryStr += lexName[i];
            }
            int value = 0;
            for(char c : binaryStr) {
                value = value * 2 + (c - '0');
            }
            return makeToken(TokenType::TOKEN_NUMBER, lexName, value);
        } else if(next == 'o' || next == 'O') {
            // 八进制字面量
            advance(); // 消耗 '0'
            advance(); // 消耗 'o' 或 'O'
            while(true){
                const char ch = peek();
                if((ch >= '0' && ch <= '7') || ch == '_') {
                    advance();
                } else {
                    break;
                }
            }
            const auto lexName = lexeme();
            std::string octalStr;
            for(size_t i = 2; i < lexName.size(); i++) {
                if(lexName[i] != '_') octalStr += lexName[i];
            }
            int value = 0;
            for(char c : octalStr) {
                value = value * 8 + (c - '0');
            }
            return makeToken(TokenType::TOKEN_NUMBER, lexName, value);
        } else if(next == 'x' || next == 'X') {
            // 十六进制字面量
            advance(); // 消耗 '0'
            advance(); // 消耗 'x' 或 'X'
            while(true){
                const char ch = peek();
                if(std::isxdigit(ch) || ch == '_') {
                    advance();
                } else {
                    break;
                }
            }
            const auto lexName = lexeme();
            std::string hexStr;
            for(size_t i = 2; i < lexName.size(); i++) {
                if(lexName[i] != '_') hexStr += lexName[i];
            }
            int value = 0;
            for(char c : hexStr) {
                if(c >= '0' && c <= '9') value = value * 16 + (c - '0');
                else if(c >= 'a' && c <= 'f') value = value * 16 + (c - 'a' + 10);
                else if(c >= 'A' && c <= 'F') value = value * 16 + (c - 'A' + 10);
            }
            return makeToken(TokenType::TOKEN_NUMBER, lexName, value);
        }
    }

    // 十进制整数或浮点数
    while(true){
        const char ch = peek();
        if(std::isdigit(ch) || ch == '_') {
            advance();
        } else {
            break;
        }
    }

    bool isfloat = false;
    if(peek() == '.' and std::isdigit(peekNext())) {
        isfloat = true;
        advance(); // consume the '.'
        while(true){
            const char ch = peek();
            if(std::isdigit(ch) || ch == '_') {
                advance();
            } else {
                break;
            }
        }
    }

    // Floating-point literal suffix (e.g. 3.14f, 2.0F, 1.5L).
    if(isfloat) {
        const char suffix = peek();
        if(suffix == 'f' || suffix == 'F' || suffix == 'l' || suffix == 'L') {
            advance();
        }
    }

    const auto lexName = lexeme();
    if(isfloat) {
        std::string cleanStr;
        for(char c : lexName) {
            if(c == '_') continue;
            if(c == 'f' || c == 'F' || c == 'l' || c == 'L') break;
            cleanStr += c;
        }
        return makeToken(TokenType::TOKEN_FLOAT, lexName, std::stod(cleanStr));
    } else {
        std::string cleanStr;
        for(char c : lexName) { if(c != '_') cleanStr += c; }
        return makeToken(TokenType::TOKEN_NUMBER, lexName, std::stoi(cleanStr));
    }
}

Token Lexer::scanString() {
    // 检查是否是原始字符串 r"..."
    if(peek() == 'r' && peekNext() == '"') {
        advance(); // 消耗 'r'
        advance(); // 消耗 '"'
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
    
    // 普通字符串
    advance();
    while(true){
        const char ch = peek();
        if(ch == '"') {
            advance();
            break;
        } else if(ch == '\\' && !isEof()) {
            // 处理转义序列
            advance(); // 消耗 '\'
            char escapeChar = peek();
            if(escapeChar == 'u' && !isEof()) {
                // Unicode转义 \u{XXXX}
                advance(); // 消耗 'u'
                if(peek() == '{') {
                    advance(); // 消耗 '{'
                    while(peek() != '}' && !isEof()) {
                        advance();
                    }
                    if(peek() == '}') {
                        advance(); // 消耗 '}'
                    }
                }
            } else {
                advance(); // 消耗转义字符
            }
        } else {
            advance();
        }
    }

    return makeToken(TokenType::TOKEN_STRING, lexeme());
}

Token Lexer::scanChar() {
    advance(); // 消耗开始的引号
    while(true){
        const char ch = peek();
        if(ch == '\'') {
            advance();
            break;
        } else if(ch == '\\' && !isEof()) {
            // 处理转义序列
            advance(); // 消耗 '\'
            char escapeChar = peek();
            if(escapeChar == 'u' && !isEof()) {
                // Unicode转义 \u{XXXX}
                advance(); // 消耗 'u'
                if(peek() == '{') {
                    advance(); // 消耗 '{'
                    while(peek() != '}' && !isEof()) {
                        advance();
                    }
                    if(peek() == '}') {
                        advance(); // 消耗 '}'
                    }
                }
            } else {
                advance(); // 消耗转义字符
            }
        } else {
            advance();
        }
    }

    return makeToken(TokenType::TOKEN_CHAR, lexeme());
}
        
Token Lexer::scanToken() {
    const char ch = peek();
    if(ch == 'r' && peekNext() == '"') {
        return scanString();
    }
    if(std::isalpha(ch) or ch == '_') {
        return scanIdentifier();
    }else if(std::isdigit(ch)) {
        return scanNumber();
    }else if(ch == '"') {
        // scanString consumes the closing quote itself; return directly so the
        // trailing advance() below does not eat the following character.
        return scanString();
    }else if(ch == '\'') {
        return scanChar();
    }

    Token token;
    switch(ch){
    case '+':
        {
            if(peekNext() == '+') {
                advance();
                token = makeToken(TokenType::TOKEN_PLUS_PLUS, lexeme());
            } else if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_PLUS_EQ, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_PLUS, lexeme());
            }
        } break;
    case '-':
        {
            if(peekNext() == '-') {
                advance();
                token = makeToken(TokenType::TOKEN_MINUS_MINUS, lexeme());
            } else if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_MINUS_EQ, lexeme());
            } else if(peekNext() == '>') {
                advance();
                token = makeToken(TokenType::TOKEN_ARROW, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_MINUS, lexeme());
            }
        } break;
    case '*':
        {
            if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_STAR_EQ, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_STAR, lexeme());
            }
        } break;
    case '/':
        {
            if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_SLASH_EQ, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_SLASH, lexeme());
            }
        } break;
    case '%':
        {
            if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_PERCENT_EQ, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_PERCENT, lexeme());
            }
        } break;
    case '=':
        {
            if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_EQ, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_ASSIGN, lexeme());
            }
        } break;
    case '!':
        {
            if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_NOT_EQ, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_NOT, lexeme());
            }
        } break;
    case '<':
        {
            if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_LE, lexeme());
            } else if(peekNext() == '<') {
                advance();
                if(peekNext() == '=') {
                    advance();
                    token = makeToken(TokenType::TOKEN_LSHIFT_EQ, lexeme());
                } else {
                    token = makeToken(TokenType::TOKEN_LSHIFT, lexeme());
                }
            } else {
                token = makeToken(TokenType::TOKEN_LT, lexeme());
            }
        } break;
    case '>':
        {
            if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_GE, lexeme());
            } else if(peekNext() == '>') {
                advance();
                if(peekNext() == '=') {
                    advance();
                    token = makeToken(TokenType::TOKEN_RSHIFT_EQ, lexeme());
                } else {
                    token = makeToken(TokenType::TOKEN_RSHIFT, lexeme());
                }
            } else {
                token = makeToken(TokenType::TOKEN_GT, lexeme());
            }
        } break;
    case '&':
        {
            if(peekNext() == '&') {
                advance();
                token = makeToken(TokenType::TOKEN_AND, lexeme());
            } else if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_AMP_EQ, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_BIT_AND, lexeme());
            }
        } break;
    case '|':
        {
            if(peekNext() == '|') {
                advance();
                token = makeToken(TokenType::TOKEN_OR, lexeme());
            } else if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_PIPE_EQ, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_BIT_OR, lexeme());
            }
        } break;
    case '^':
        {
            if(peekNext() == '=') {
                advance();
                token = makeToken(TokenType::TOKEN_CARET_EQ, lexeme());
            } else {
                token = makeToken(TokenType::TOKEN_CARET, lexeme());
            }
        } break;
    case '~':
        token = makeToken(TokenType::TOKEN_TILDE, lexeme());
        break;
    case '?':
        token = makeToken(TokenType::TOKEN_QUESTION, lexeme());
        break;
    case ':':
        token = makeToken(TokenType::TOKEN_COLON, lexeme());
        break;
    case '(':
        token = makeToken(TokenType::TOKEN_LPAREN, lexeme());
        break;
    case ')':
        token = makeToken(TokenType::TOKEN_RPAREN, lexeme());
        break;
    case '{':
        token = makeToken(TokenType::TOKEN_LBRACE, lexeme());
        break;
    case '}':
        token = makeToken(TokenType::TOKEN_RBRACE, lexeme());
        break;
    case '[':
        token = makeToken(TokenType::TOKEN_LBRACKET, lexeme());
        break;
    case ']':
        token = makeToken(TokenType::TOKEN_RBRACKET, lexeme());
        break;
    case ';':
        token = makeToken(TokenType::TOKEN_SEMICOLON, lexeme());
        break;
    case ',':
        token = makeToken(TokenType::TOKEN_COMMA, lexeme());
        break;
    case '.':
        if (peekNext() == '.' && m_currentPos + 2 < m_source.size() && m_source[m_currentPos + 2] == '.') {
            advance();
            advance();
            token = makeToken(TokenType::TOKEN_ELLIPSIS, lexeme());
        } else {
            token = makeToken(TokenType::TOKEN_DOT, lexeme());
        }
        break;
    case '#':
        token = makeToken(TokenType::TOKEN_HASH, lexeme());
        break;
    default:
        token = makeToken(TokenType::TOKEN_UNKNOWN, lexeme());
        break;
    }

    advance();
    return token;
}



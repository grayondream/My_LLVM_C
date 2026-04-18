#include "Utils.h"
#include "AST.h"
#include "Token.h"

static std::string TokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::TOKEN_UNKNOWN:    return "UNKNOWN";
        case TokenType::TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TokenType::TOKEN_NUMBER:     return "NUMBER";
        case TokenType::TOKEN_STRING:     return "STRING";
        case TokenType::TOKEN_CHAR:       return "CHAR";
        case TokenType::TOKEN_INT:        return "int";
        case TokenType::TOKEN_FLOAT:      return "float";
        case TokenType::TOKEN_DOUBLE:     return "double";
        case TokenType::TOKEN_CHAR_KW:    return "char";
        case TokenType::TOKEN_VOID:       return "void";
        case TokenType::TOKEN_IF:         return "if";
        case TokenType::TOKEN_ELSE:       return "else";
        case TokenType::TOKEN_FOR:        return "for";
        case TokenType::TOKEN_WHILE:      return "while";
        case TokenType::TOKEN_DO:         return "do";
        case TokenType::TOKEN_RETURN:     return "return";
        case TokenType::TOKEN_BREAK:      return "break";
        case TokenType::TOKEN_CONTINUE:   return "continue";
        case TokenType::TOKEN_STRUCT:     return "struct";
        case TokenType::TOKEN_UNION:      return "union";
        case TokenType::TOKEN_ENUM:       return "enum";
        case TokenType::TOKEN_CONST:      return "const";
        case TokenType::TOKEN_STATIC:     return "static";
        case TokenType::TOKEN_EXTERN:     return "extern";
        case TokenType::TOKEN_VOLATILE:   return "volatile";
        case TokenType::TOKEN_REGISTER:   return "register";
        case TokenType::TOKEN_SIZEOF:     return "sizeof";
        case TokenType::TOKEN_TYPEDEF:    return "typedef";
        case TokenType::TOKEN_INLINE:     return "inline";
        case TokenType::TOKEN_RESTRICT:   return "restrict";
        case TokenType::TOKEN_BOOL:       return "bool";
        case TokenType::TOKEN_GOTO:       return "goto";
        case TokenType::TOKEN_SWITCH:     return "switch";
        case TokenType::TOKEN_CASE:       return "case";
        case TokenType::TOKEN_DEFAULT:    return "default";
        case TokenType::TOKEN_PLUS:       return "+";
        case TokenType::TOKEN_MINUS:      return "-";
        case TokenType::TOKEN_STAR:       return "*";
        case TokenType::TOKEN_SLASH:      return "/";
        case TokenType::TOKEN_PERCENT:    return "%";
        case TokenType::TOKEN_EQ:         return "==";
        case TokenType::TOKEN_NOT_EQ:     return "!=";
        case TokenType::TOKEN_ASSIGN:     return "=";
        case TokenType::TOKEN_PLUS_EQ:    return "+=";
        case TokenType::TOKEN_MINUS_EQ:   return "-=";
        case TokenType::TOKEN_STAR_EQ:    return "*=";
        case TokenType::TOKEN_SLASH_EQ:   return "/=";
        case TokenType::TOKEN_LT:         return "<";
        case TokenType::TOKEN_GT:         return ">";
        case TokenType::TOKEN_LE:         return "<=";
        case TokenType::TOKEN_GE:         return ">=";
        case TokenType::TOKEN_AND:        return "&&";
        case TokenType::TOKEN_OR:         return "||";
        case TokenType::TOKEN_NOT:        return "!";
        case TokenType::TOKEN_BIT_AND:    return "&";
        case TokenType::TOKEN_BIT_OR:     return "|";
        case TokenType::TOKEN_SEMICOLON:  return ";";
        case TokenType::TOKEN_COMMA:      return ",";
        case TokenType::TOKEN_DOT:        return ".";
        case TokenType::TOKEN_ARROW:      return "->";
        case TokenType::TOKEN_LPAREN:     return "(";
        case TokenType::TOKEN_RPAREN:     return ")";
        case TokenType::TOKEN_LBRACE:     return "{";
        case TokenType::TOKEN_RBRACE:     return "}";
        case TokenType::TOKEN_LBRACKET:   return "[";
        case TokenType::TOKEN_RBRACKET:   return "]";
        case TokenType::TOKEN_EOS:        return "EOS";
        default:                          return "UNKNOWN(" + std::to_string(static_cast<int>(type)) + ")";
    }
}

static std::string TokenValueToString(const TokenValue& value) {
    if (std::holds_alternative<std::monostate>(value)) return "null";
    if (std::holds_alternative<int>(value)) return std::to_string(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return std::to_string(std::get<double>(value));
    if (std::holds_alternative<char>(value)) return std::string(1, std::get<char>(value));
    if (std::holds_alternative<std::string>(value)) return std::get<std::string>(value);
    return "unknown";
}

std::string to_string(const Token& token) {
    return "Token(type=" + TokenTypeToString(token.type) +
           ", lexeme=\"" + token.lexeme +
           "\", value=" + TokenValueToString(token.value) +
           ", file=\"" + token.filename +
           "\", line=" + std::to_string(token.line) +
           ", column=" + std::to_string(token.column) +
           ")";
}

std::string to_string(const TranslationUnitAST& ast) {
    return "TranslationUnitAST";
}

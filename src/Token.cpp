#include "Token.h"

static std::string TokenTypeToString(TokenType type) {
    return std::to_string(static_cast<int>(type)); // 简化版，可自行扩展
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
#pragma once

#include <string>
#include <variant>

#include <cstdint>

enum class TokenType : int32_t {
    // 基本标识符与字面量
    TOKEN_IDENTIFIER,   // 变量名、函数名
    TOKEN_NUMBER,       // 数字字面量
    TOKEN_STRING,       // 字符串字面量
    TOKEN_CHAR,         // 字符字面量
  
    // 关键字
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_DOUBLE,
    TOKEN_CHAR_KW,      // 避免和 TOKEN_CHAR 冲突
    TOKEN_VOID,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_WHILE,
    TOKEN_DO,
    TOKEN_RETURN,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_STRUCT,
    TOKEN_UNION,
    TOKEN_ENUM,
    TOKEN_CONST,
    TOKEN_STATIC,
    TOKEN_EXTERN,
    TOKEN_VOLATILE,
    TOKEN_REGISTER,
    TOKEN_SIZEOF,
    TOKEN_TYPEDEF,
    TOKEN_INLINE,
    TOKEN_RESTRICT,
    TOKEN_BOOL,
    TOKEN_GOTO,
    TOKEN_SWITCH,
    TOKEN_CASE,
    TOKEN_DEFAULT,

    // 运算符
    TOKEN_PLUS,         // +
    TOKEN_MINUS,        // -
    TOKEN_STAR,         // *
    TOKEN_SLASH,        // /
    TOKEN_PERCENT,      // %
    TOKEN_EQ,           // ==
    TOKEN_NOT_EQ,       // !=
    TOKEN_ASSIGN,       // =
    TOKEN_PLUS_EQ,      // +=
    TOKEN_MINUS_EQ,     // -=
    TOKEN_STAR_EQ,      // *=
    TOKEN_SLASH_EQ,     // /=
    TOKEN_LT,           // <
    TOKEN_GT,           // >
    TOKEN_LE,           // <=
    TOKEN_GE,           // >=
    TOKEN_AND,          // &&
    TOKEN_OR,           // ||
    TOKEN_NOT,          // !

    // 分隔符
    TOKEN_SEMICOLON,    // ;
    TOKEN_COMMA,        // ,
    TOKEN_DOT,          // .
    TOKEN_ARROW,        // ->
    TOKEN_LPAREN,       // (
    TOKEN_RPAREN,       // )
    TOKEN_LBRACE,       // {
    TOKEN_RBRACE,       // }
    TOKEN_LBRACKET,     // [
    TOKEN_RBRACKET,     // ]

    // 文件结尾
    TOKEN_EOS
};

using TokenValue = std::variant<std::monostate, int, double, char, std::string>;
class Token {
public:
    TokenType type{}; 
    std::string lexeme{};      // 源文本
    TokenValue value{};        // 解析后的字面量
    std::string filename{};    // 文件名
    int line{};                // 行号
    int column{};              // 列号

    // 构造函数（通用）
    Token(TokenType type,
          const std::string& lexeme,
          TokenValue value = std::monostate{},
          const std::string& filename = "",
          int line = 0,
          int column = 0)
        : type(type), lexeme(lexeme), value(value),
          filename(filename), line(line), column(column) {}
};

std::string to_string(const Token& token);

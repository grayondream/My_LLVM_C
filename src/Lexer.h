#pragma once

#include <cstdint>

enum class LexerState : int32_t {
    START,

    IDENTIFIER,
    NUMBER,
    FLOAT,

    STRING,
    CHAR,

    OPERATOR,

    COMMENT_LINE,
    COMMENT_BLOCK,

    DONE,
    ERROR
};

class Lexer{

};
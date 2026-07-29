#pragma once
#include "MacroTable.h"
#include "frontend/Lexer.h"
#include <vector>
#include <string>

class Preprocessor {
    MacroTable macros;
    std::vector<std::string> includeStack;
    std::string sourceDir;
    std::vector<std::string> includePaths;

public:
    Preprocessor();
    void addIncludePath(const std::string& path);
    std::vector<Token> preprocess(Lexer& lexer);
};

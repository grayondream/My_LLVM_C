#pragma once
#include "frontend/Token.h"
#include <string>
#include <vector>
#include <unordered_map>

struct Macro {
    std::string name;
    std::vector<std::string> params;
    std::vector<Token> body;
    bool isVariadic = false;
};

class MacroTable {
    std::unordered_map<std::string, Macro> macros;
public:
    void define(const Macro& macro);
    void undefine(const std::string& name);
    const Macro* lookup(const std::string& name) const;
    bool isDefined(const std::string& name) const;
};

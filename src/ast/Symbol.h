#pragma once
#include <string>
#include <unordered_map>
#include "ast/Type.h"

class Symbol{
public:
    Symbol(const std::string& name, Type* type) : name(name), type(type) {}
public:
    std::string name;
    Type* type;
};

class Scope{
public:
    Scope(Scope* parent) : parent(parent) {}

    Symbol* lookup(const std::string& name) const;
    
public:
    std::unordered_map<std::string, Symbol*> symbols;
    Scope* parent;
};


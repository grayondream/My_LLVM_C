#pragma once
#include <string>
#include <unordered_map>
#include "llvm/IR/Value.h"
#include "ast/Type.h"

class Symbol{
public:
    Symbol(const std::string& name, Type* type, llvm::Value* value = nullptr)
        : name(name), type(type), value(value) {}
public:
    std::string name;
    Type* type;
    llvm::Value* value;
};

class Scope{
public:
    Scope(Scope* parent) : parent(parent) {}

    Symbol* lookup(const std::string& name) const;
    
public:
    std::unordered_map<std::string, Symbol*> symbols;
    Scope* parent;
};


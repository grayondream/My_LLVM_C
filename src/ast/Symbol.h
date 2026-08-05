#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "llvm/IR/Value.h"
#include "ast/Type.h"

class Symbol {
public:
    Symbol(const std::string& name, Type* type, llvm::Value* value = nullptr)
        : name(name), type(type), value(value) {}
public:
    std::string name;
    Type* type;
    llvm::Value* value;
};

class OverloadSet {
public:
    void add(Symbol* sym);
    Symbol* resolve(const std::vector<Type*>& argTypes) const;
    const std::vector<Symbol*>& getCandidates() const;
    size_t size() const;
    bool empty() const;

private:
    std::vector<Symbol*> candidates;
};

class Scope {
public:
    Scope(Scope* parent) : parent(parent) {}

    Symbol* lookup(const std::string& name) const;
    OverloadSet* lookupOverload(const std::string& name);
    bool declare(const std::string& name, Symbol* sym);

public:
    std::unordered_map<std::string, OverloadSet> symbols;
    Scope* parent;
};

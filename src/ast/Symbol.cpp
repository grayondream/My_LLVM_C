#include "Symbol.h"

bool typesEqual(Type* a, Type* b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case TypeKind::Pointer:
        case TypeKind::Array:
            return typesEqual(a->base, b->base);
        case TypeKind::Struct: {
            auto* sa = static_cast<StructType*>(a);
            auto* sb = static_cast<StructType*>(b);
            return sa->name == sb->name;
        }
        case TypeKind::Union: {
            auto* ua = static_cast<UnionType*>(a);
            auto* ub = static_cast<UnionType*>(b);
            return ua->name == ub->name;
        }
        case TypeKind::Typedef: {
            auto* ta = static_cast<TypedefType*>(a);
            auto* tb = static_cast<TypedefType*>(b);
            return typesEqual(ta->aliasedType, tb->aliasedType);
        }
        case TypeKind::Function: {
            auto* fa = static_cast<FunctionType*>(a);
            auto* fb = static_cast<FunctionType*>(b);
            if (!typesEqual(fa->returnType, fb->returnType)) return false;
            if (fa->paramTypes.size() != fb->paramTypes.size()) return false;
            for (size_t i = 0; i < fa->paramTypes.size(); ++i) {
                if (!typesEqual(fa->paramTypes[i], fb->paramTypes[i])) return false;
            }
            return true;
        }
        default:
            return true; // All primitive types with same kind are equal
    }
}

void OverloadSet::add(Symbol* sym) {
    candidates.push_back(sym);
}

Symbol* OverloadSet::resolve(const std::vector<Type*>& argTypes) const {
    Symbol* match = nullptr;
    for (auto* sym : candidates) {
        if (sym->type->kind != TypeKind::Function) continue;
        auto* funcType = static_cast<FunctionType*>(sym->type);
        if (funcType->isVarArg) continue; // Skip vararg for now
        if (funcType->paramTypes.size() != argTypes.size()) continue;

        bool allMatch = true;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (!typesEqual(funcType->paramTypes[i], argTypes[i])) {
                allMatch = false;
                break;
            }
        }

        if (allMatch) {
            if (match != nullptr) {
                return nullptr; // Ambiguous
            }
            match = sym;
        }
    }
    return match;
}

const std::vector<Symbol*>& OverloadSet::getCandidates() const {
    return candidates;
}

size_t OverloadSet::size() const {
    return candidates.size();
}

bool OverloadSet::empty() const {
    return candidates.empty();
}

Symbol* Scope::lookup(const std::string& name) const {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        if (!it->second.empty()) {
            return it->second.getCandidates().front();
        }
    }
    if (parent) {
        return parent->lookup(name);
    }
    return nullptr;
}

OverloadSet* Scope::lookupOverload(const std::string& name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return &it->second;
    }
    if (parent) {
        return parent->lookupOverload(name);
    }
    return nullptr;
}

bool Scope::declare(const std::string& name, Symbol* sym) {
    auto& overloadSet = symbols[name];
    // Check for exact duplicate signature or variable redefinition
    for (auto* existing : overloadSet.getCandidates()) {
        if (existing->type->kind == TypeKind::Function && sym->type->kind == TypeKind::Function) {
            auto* existingFunc = static_cast<FunctionType*>(existing->type);
            auto* newFunc = static_cast<FunctionType*>(sym->type);
            if (typesEqual(existingFunc, newFunc)) {
                return false; // Duplicate exact signature
            }
        } else if (existing->type->kind != TypeKind::Function && sym->type->kind != TypeKind::Function) {
            return false; // Duplicate variable declaration
        }
    }
    overloadSet.add(sym);
    return true;
}

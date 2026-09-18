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
        case TypeKind::Class: {
            auto* ca = static_cast<ClassType*>(a);
            auto* cb = static_cast<ClassType*>(b);
            return ca->name == cb->name;
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

static int integerWidth(TypeKind kind) {
    switch (kind) {
        case TypeKind::Bool:     return 1;
        case TypeKind::Char:     return 8;
        case TypeKind::Int8:     return 8;
        case TypeKind::UInt8:    return 8;
        case TypeKind::Int16:    return 16;
        case TypeKind::UInt16:   return 16;
        case TypeKind::Int:
        case TypeKind::Int32:    return 32;
        case TypeKind::UInt32:   return 32;
        case TypeKind::Int64:
        case TypeKind::UInt64:
        case TypeKind::ISize:
        case TypeKind::USize:    return 64;
        case TypeKind::Int128:
        case TypeKind::UInt128:  return 128;
        case TypeKind::Enum:     return 32;
        default:                 return 0;
    }
}

static int floatWidth(TypeKind kind) {
    switch (kind) {
        case TypeKind::Float:
        case TypeKind::Float32:  return 32;
        case TypeKind::Double:
        case TypeKind::Float64:  return 64;
        default:                 return 0;
    }
}

int conversionRank(Type* from, Type* to) {
    if (!from || !to) return -1;
    if (typesEqual(from, to)) return 0;

    while (from->kind == TypeKind::Typedef) from = static_cast<TypedefType*>(from)->aliasedType;
    while (to->kind == TypeKind::Typedef) to = static_cast<TypedefType*>(to)->aliasedType;
    if (!from || !to) return -1;
    if (typesEqual(from, to)) return 0;

    // Arrays decay to a pointer to their first element.
    if (from->kind == TypeKind::Array && to->kind == TypeKind::Pointer) return 1;

    int fw = integerWidth(from->kind);
    int tw = integerWidth(to->kind);
    int fb = floatWidth(from->kind);
    int tb = floatWidth(to->kind);

    if (fw > 0 && tw > 0 && tw >= fw) return 1;   // integer widening
    if (fw > 0 && tb > 0) return 1;               // integer -> floating point
    if (fb > 0 && tb > 0 && tb >= fb) return 1;   // floating point widening

    return -1;
}

void OverloadSet::add(Symbol* sym) {
    candidates.push_back(sym);
}

Symbol* OverloadSet::resolve(const std::vector<Type*>& argTypes) const {
    Symbol* best = nullptr;
    int bestRank = 0;
    bool ambiguous = false;

    for (auto* sym : candidates) {
        if (sym->type->kind != TypeKind::Function) continue;
        auto* funcType = static_cast<FunctionType*>(sym->type);
        size_t fixedCount = funcType->paramTypes.size();

        if (funcType->isVarArg) {
            if (argTypes.size() < fixedCount) continue;
        } else {
            if (argTypes.size() != fixedCount) continue;
        }

        int totalRank = 0;
        bool viable = true;
        for (size_t i = 0; i < fixedCount; ++i) {
            int rank = conversionRank(argTypes[i], funcType->paramTypes[i]);
            if (rank < 0) {
                viable = false;
                break;
            }
            totalRank += rank;
        }
        if (!viable) continue;

        // Prefer non-vararg candidates over vararg ones on equal conversions.
        if (funcType->isVarArg) totalRank += 1;

        if (best == nullptr || totalRank < bestRank) {
            best = sym;
            bestRank = totalRank;
            ambiguous = false;
        } else if (totalRank == bestRank) {
            ambiguous = true;
        }
    }

    if (ambiguous) return nullptr; // Ambiguous
    return best;
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

#pragma once
#include <unordered_map>

enum class TypeKind {
    Void,
    Int,
    Float,
    Double,
    Char,
    Pointer,
};

class Type {
public:
    ~Type() = default;
    Type(const TypeKind kind, Type* base = nullptr)
        : kind(kind), base(base) {}
public:
    TypeKind kind{};
    Type* base{};
    bool isVolatile{};
    bool isConst{};
};

class TypeContext{
public:
    static TypeContext& instance();
    ~TypeContext();

    Type* getInt();
    Type* getFloat();
    Type* getDouble();
    Type* getChar();
    Type* getVoid();

private:
    std::unordered_map<TypeKind, Type*> m_types;
};

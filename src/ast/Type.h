#pragma once
#include <unordered_map>
#include <string>
#include <vector>

enum class TypeKind {
    Void,
    Int,
    Float,
    Double,
    Char,
    Pointer,
    Array,
    Struct,
    Class,
    Union,
    Enum,
    Function,
    Typedef,
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

class ArrayType : public Type {
public:
    Type* elementType;
    int size;

    ArrayType(Type* elem, int sz)
        : Type(TypeKind::Array), elementType(elem), size(sz) {}
};

class StructType : public Type {
public:
    std::string name;
    std::vector<std::pair<std::string, Type*>> fields;

    StructType(const std::string& n)
        : Type(TypeKind::Struct), name(n) {}

    void addField(const std::string& fieldName, Type* fieldType) {
        fields.push_back({fieldName, fieldType});
    }
};

class UnionType : public Type {
public:
    std::string name;
    std::vector<std::pair<std::string, Type*>> members;

    UnionType(const std::string& n)
        : Type(TypeKind::Union), name(n) {}

    void addMember(const std::string& memberName, Type* memberType) {
        members.push_back({memberName, memberType});
    }
};

class EnumType : public Type {
public:
    std::string name;
    std::vector<std::pair<std::string, int>> values;

    EnumType(const std::string& n)
        : Type(TypeKind::Enum), name(n) {}

    void addValue(const std::string& valueName, int val) {
        values.push_back({valueName, val});
    }
};

class FunctionType : public Type {
public:
    Type* returnType;
    std::vector<Type*> paramTypes;
    bool isVarArg;

    FunctionType(Type* ret, std::vector<Type*> params, bool varArg = false)
        : Type(TypeKind::Function), returnType(ret), paramTypes(std::move(params)), isVarArg(varArg) {}
};

class TypedefType : public Type {
public:
    std::string name;
    Type* aliasedType;

    TypedefType(const std::string& n, Type* aliased)
        : Type(TypeKind::Typedef), name(n), aliasedType(aliased) {}
};

class ClassType : public Type {
public:
    std::string name;
    std::vector<std::pair<std::string, Type*>> fields;
    std::vector<std::pair<std::string, FunctionType*>> methods;
    std::string baseClass;

    ClassType(const std::string& n)
        : Type(TypeKind::Class), name(n) {}

    void addField(const std::string& fieldName, Type* fieldType) {
        fields.push_back({fieldName, fieldType});
    }

    void addMethod(const std::string& methodName, FunctionType* methodType) {
        methods.push_back({methodName, methodType});
    }
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

    void addTypedef(const std::string& name, Type* type);
    Type* getTypedef(const std::string& name) const;

    void addStruct(const std::string& name, StructType* type);
    StructType* getStruct(const std::string& name) const;

    void addUnion(const std::string& name, UnionType* type);
    UnionType* getUnion(const std::string& name) const;

    void addEnum(const std::string& name, EnumType* type);
    EnumType* getEnum(const std::string& name) const;

    void addClass(const std::string& name, ClassType* type);
    ClassType* getClass(const std::string& name) const;
    ClassType* getOrCreateClass(const std::string& name);

private:
    std::unordered_map<TypeKind, Type*> m_types;
    std::unordered_map<std::string, Type*> m_typedefs;
    std::unordered_map<std::string, StructType*> m_structs;
    std::unordered_map<std::string, UnionType*> m_unions;
    std::unordered_map<std::string, EnumType*> m_enums;
    std::unordered_map<std::string, ClassType*> m_classes;
};

#include "Type.h"

TypeContext& TypeContext::instance() {
    static TypeContext instance;
    return instance;
}

Type* TypeContext::getInt() {
    if (m_types.find(TypeKind::Int) == m_types.end()) {
        m_types[TypeKind::Int] = new Type(TypeKind::Int);
    }
    return m_types[TypeKind::Int];
}

Type* TypeContext::getFloat() {
    if (m_types.find(TypeKind::Float) == m_types.end()) {
        m_types[TypeKind::Float] = new Type(TypeKind::Float);
    }
    return m_types[TypeKind::Float];
}

Type* TypeContext::getDouble() {
    if (m_types.find(TypeKind::Double) == m_types.end()) {
        m_types[TypeKind::Double] = new Type(TypeKind::Double);
    }
    return m_types[TypeKind::Double];
}

Type* TypeContext::getChar() {
    if (m_types.find(TypeKind::Char) == m_types.end()) {
        m_types[TypeKind::Char] = new Type(TypeKind::Char);
    }
    return m_types[TypeKind::Char];
}

Type* TypeContext::getVoid() {
    if (m_types.find(TypeKind::Void) == m_types.end()) {
        m_types[TypeKind::Void] = new Type(TypeKind::Void);
    }
    return m_types[TypeKind::Void];
}

TypeContext::~TypeContext() {
    for (auto& it : m_types) {
        delete it.second;
    }
    for (auto& it : m_typedefs) {
        delete it.second;
    }
}

void TypeContext::addTypedef(const std::string& name, Type* type) {
    m_typedefs[name] = new TypedefType(name, type);
}

Type* TypeContext::getTypedef(const std::string& name) const {
    auto it = m_typedefs.find(name);
    if (it != m_typedefs.end()) {
        return it->second;
    }
    return nullptr;
}

void TypeContext::addStruct(const std::string& name, StructType* type) {
    m_structs[name] = type;
}

StructType* TypeContext::getStruct(const std::string& name) const {
    auto it = m_structs.find(name);
    if (it != m_structs.end()) {
        return it->second;
    }
    return nullptr;
}

void TypeContext::addUnion(const std::string& name, UnionType* type) {
    m_unions[name] = type;
}

UnionType* TypeContext::getUnion(const std::string& name) const {
    auto it = m_unions.find(name);
    if (it != m_unions.end()) {
        return it->second;
    }
    return nullptr;
}

void TypeContext::addEnum(const std::string& name, EnumType* type) {
    m_enums[name] = type;
}

EnumType* TypeContext::getEnum(const std::string& name) const {
    auto it = m_enums.find(name);
    if (it != m_enums.end()) {
        return it->second;
    }
    return nullptr;
}

void TypeContext::addClass(const std::string& name, ClassType* type) {
    m_classes[name] = type;
}

ClassType* TypeContext::getClass(const std::string& name) const {
    auto it = m_classes.find(name);
    if (it != m_classes.end()) {
        return it->second;
    }
    return nullptr;
}

ClassType* TypeContext::getOrCreateClass(const std::string& name) {
    auto it = m_classes.find(name);
    if (it != m_classes.end()) {
        return it->second;
    }
    auto* classType = new ClassType(name);
    m_classes[name] = classType;
    return classType;
}

// 新增类型获取方法
Type* TypeContext::getBool() {
    if (m_types.find(TypeKind::Bool) == m_types.end()) {
        m_types[TypeKind::Bool] = new Type(TypeKind::Bool);
    }
    return m_types[TypeKind::Bool];
}

Type* TypeContext::getInt8() {
    if (m_types.find(TypeKind::Int8) == m_types.end()) {
        m_types[TypeKind::Int8] = new Type(TypeKind::Int8);
    }
    return m_types[TypeKind::Int8];
}

Type* TypeContext::getInt16() {
    if (m_types.find(TypeKind::Int16) == m_types.end()) {
        m_types[TypeKind::Int16] = new Type(TypeKind::Int16);
    }
    return m_types[TypeKind::Int16];
}

Type* TypeContext::getInt32() {
    if (m_types.find(TypeKind::Int32) == m_types.end()) {
        m_types[TypeKind::Int32] = new Type(TypeKind::Int32);
    }
    return m_types[TypeKind::Int32];
}

Type* TypeContext::getInt64() {
    if (m_types.find(TypeKind::Int64) == m_types.end()) {
        m_types[TypeKind::Int64] = new Type(TypeKind::Int64);
    }
    return m_types[TypeKind::Int64];
}

Type* TypeContext::getInt128() {
    if (m_types.find(TypeKind::Int128) == m_types.end()) {
        m_types[TypeKind::Int128] = new Type(TypeKind::Int128);
    }
    return m_types[TypeKind::Int128];
}

Type* TypeContext::getUInt8() {
    if (m_types.find(TypeKind::UInt8) == m_types.end()) {
        m_types[TypeKind::UInt8] = new Type(TypeKind::UInt8);
    }
    return m_types[TypeKind::UInt8];
}

Type* TypeContext::getUInt16() {
    if (m_types.find(TypeKind::UInt16) == m_types.end()) {
        m_types[TypeKind::UInt16] = new Type(TypeKind::UInt16);
    }
    return m_types[TypeKind::UInt16];
}

Type* TypeContext::getUInt32() {
    if (m_types.find(TypeKind::UInt32) == m_types.end()) {
        m_types[TypeKind::UInt32] = new Type(TypeKind::UInt32);
    }
    return m_types[TypeKind::UInt32];
}

Type* TypeContext::getUInt64() {
    if (m_types.find(TypeKind::UInt64) == m_types.end()) {
        m_types[TypeKind::UInt64] = new Type(TypeKind::UInt64);
    }
    return m_types[TypeKind::UInt64];
}

Type* TypeContext::getUInt128() {
    if (m_types.find(TypeKind::UInt128) == m_types.end()) {
        m_types[TypeKind::UInt128] = new Type(TypeKind::UInt128);
    }
    return m_types[TypeKind::UInt128];
}

Type* TypeContext::getISize() {
    if (m_types.find(TypeKind::ISize) == m_types.end()) {
        m_types[TypeKind::ISize] = new Type(TypeKind::ISize);
    }
    return m_types[TypeKind::ISize];
}

Type* TypeContext::getUSize() {
    if (m_types.find(TypeKind::USize) == m_types.end()) {
        m_types[TypeKind::USize] = new Type(TypeKind::USize);
    }
    return m_types[TypeKind::USize];
}

Type* TypeContext::getFloat32() {
    if (m_types.find(TypeKind::Float32) == m_types.end()) {
        m_types[TypeKind::Float32] = new Type(TypeKind::Float32);
    }
    return m_types[TypeKind::Float32];
}

Type* TypeContext::getFloat64() {
    if (m_types.find(TypeKind::Float64) == m_types.end()) {
        m_types[TypeKind::Float64] = new Type(TypeKind::Float64);
    }
    return m_types[TypeKind::Float64];
}

SliceType* TypeContext::getSliceType(Type* elementType) {
    // 切片类型不需要缓存，因为每个切片类型都有不同的元素类型
    return new SliceType(elementType);
}

OptionalType* TypeContext::getOptionalType(Type* elementType) {
    // 可选类型不需要缓存，因为每个可选类型都有不同的元素类型
    return new OptionalType(elementType);
}

ResultType* TypeContext::getResultType(Type* successType, Type* errorType) {
    // 结果类型不需要缓存，因为每个结果类型都有不同的成功和错误类型
    return new ResultType(successType, errorType);
}
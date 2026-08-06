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
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
}
#include "Mangle.h"
#include "ast/Type.h"

std::string typeToMangled(Type* type) {
    if (!type) return "unknown";
    switch (type->kind) {
        case TypeKind::Void: return "void";
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Double: return "double";
        case TypeKind::Char: return "char";
        case TypeKind::Pointer: {
            std::string base = typeToMangled(type->base);
            return base + "ptr";
        }
        case TypeKind::Array: {
            auto* arr = static_cast<ArrayType*>(type);
            return typeToMangled(arr->elementType) + "arr";
        }
        case TypeKind::Struct: {
            auto* s = static_cast<StructType*>(type);
            return s->name;
        }
        case TypeKind::Class: {
            auto* c = static_cast<ClassType*>(type);
            return c->name;
        }
        case TypeKind::Union: {
            auto* u = static_cast<UnionType*>(type);
            return u->name;
        }
        case TypeKind::Enum: return "int";
        case TypeKind::Typedef: {
            auto* t = static_cast<TypedefType*>(type);
            return typeToMangled(t->aliasedType);
        }
        default: return "unknown";
    }
}

std::string mangleFunction(const std::string& name, const std::vector<Type*>& paramTypes) {
    if (paramTypes.empty()) return name;
    std::string result = name;
    for (auto* type : paramTypes) {
        result += "_" + typeToMangled(type);
    }
    return result;
}
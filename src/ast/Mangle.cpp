#include "Mangle.h"
#include "ast/Type.h"
#include <unordered_set>

static std::unordered_set<std::string>& cNames() {
    static std::unordered_set<std::string> names;
    return names;
}

void markCName(const std::string& name) {
    cNames().insert(name);
}

bool isCName(const std::string& name) {
    return cNames().count(name) != 0;
}

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
        case TypeKind::Bool: return "bool";
        case TypeKind::Int8: return "int8";
        case TypeKind::Int16: return "int16";
        case TypeKind::Int32: return "int32";
        case TypeKind::Int64: return "int64";
        case TypeKind::Int128: return "int128";
        case TypeKind::UInt8: return "uint8";
        case TypeKind::UInt16: return "uint16";
        case TypeKind::UInt32: return "uint32";
        case TypeKind::UInt64: return "uint64";
        case TypeKind::UInt128: return "uint128";
        case TypeKind::ISize: return "isize";
        case TypeKind::USize: return "usize";
        case TypeKind::Float32: return "float32";
        case TypeKind::Float64: return "float64";
        case TypeKind::Slice: {
            auto* s = static_cast<SliceType*>(type);
            return typeToMangled(s->elementType) + "slice";
        }
        case TypeKind::Optional: {
            auto* o = static_cast<OptionalType*>(type);
            return typeToMangled(o->elementType) + "opt";
        }
        case TypeKind::Result: {
            auto* r = static_cast<ResultType*>(type);
            return typeToMangled(r->successType) + "res" + typeToMangled(r->errorType);
        }
        case TypeKind::Typedef: {
            auto* t = static_cast<TypedefType*>(type);
            return typeToMangled(t->aliasedType);
        }
        default: return "unknown";
    }
}

std::string mangleFunction(const std::string& name, const std::vector<Type*>& paramTypes) {
    if (isCName(name)) return name;
    if (paramTypes.empty()) return name;
    std::string result = name;
    for (auto* type : paramTypes) {
        result += "_" + typeToMangled(type);
    }
    return result;
}
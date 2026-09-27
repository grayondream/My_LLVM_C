#include "frontend/Token.h"

const char* literalKindName(LiteralKind kind) {
    switch (kind) {
        case LiteralKind::Int:      return "int";
        case LiteralKind::UInt:     return "uint32";
        case LiteralKind::Long:     return "int64";
        case LiteralKind::ULong:    return "uint64";
        case LiteralKind::Float16:  return "float16";
        case LiteralKind::Float32:  return "float32";
        case LiteralKind::Float64:  return "float64";
        case LiteralKind::Float128: return "float128";
        case LiteralKind::None:     break;
    }
    return "none";
}

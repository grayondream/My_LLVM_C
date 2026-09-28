#pragma once

#include <cstdint>

// LEX-15: lexical kind of a numeric literal, derived from its suffix (or the
// unsuffixed default). This is lexer-level metadata; the base type is exposed
// through literalKindName() and consumed by later pipeline stages.
//
// Kept in the support layer so both frontend/ and ast/ can include it without
// introducing an ast -> frontend dependency.
enum class LiteralKind : int32_t {
    None,      // not a numeric literal
    Int,       // unsuffixed integer       -> int
    UInt,      // 'u'                      -> uint32
    Long,      // 'l'                      -> int64
    ULong,     // 'ul' / 'lu'              -> uint64
    Float16,   // 'f16'                    -> float16
    Float32,   // 'f' / 'F' / 'f32'        -> float32
    Float64,   // unsuffixed / 'f64' / 'l' -> float64
    Float128,  // 'f128'                   -> float128
};

// Base language type name for a literal kind (LEX-15 mapping table).
const char* literalKindName(LiteralKind kind);

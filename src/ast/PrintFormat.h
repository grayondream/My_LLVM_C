#pragma once

#include <string>
#include <vector>

#include "Type.h"

// How a single `{}` argument is formatted. `ToString` means the argument has
// been lowered to a `to_string(...)` call whose result is a C string.
enum class PrintArgKind {
    Int32,
    Int64,
    UInt32,
    UInt64,
    Char,
    Float,
    CString,
    Pointer,
    Bool,
    ToString,
};

// Classify a value of `type` for direct printf formatting. Returns false when
// the type has no builtin conversion (the caller should try `to_string`).
bool builtinPrintKind(Type* type, PrintArgKind& outKind);

// Build a C printf format string from a literal that uses fmt/spdlog-style
// `{}` placeholders. `kinds` describes each placeholder in order. When
// `newline` is true a trailing '\n' is appended. Returns false and fills
// `error` on malformed input.
bool buildPrintFormat(const std::string& literal,
                      const std::vector<PrintArgKind>& kinds,
                      bool newline,
                      std::string& outFormat,
                      std::string& error);

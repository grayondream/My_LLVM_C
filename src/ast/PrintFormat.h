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
    // P1-06 (FMT-03): str prints via `%.*s` — TWO printf arguments
    // ((int)len, ptr); the codegen print loop pushes both.
    Str,
};

// Classify a value of `type` for direct printf formatting. Returns false when
// the type has no builtin conversion (the caller should try `to_string`).
bool builtinPrintKind(Type* type, PrintArgKind& outKind);

// P1-09 (FMT-08): a parsed `{:...}` format spec. Grammar (after ':'):
//   [fill]align? sign? '0'? width? ('.' precision)? type?
// with fill only meaningful before an align character.
struct PrintSpec {
    char fill = ' ';
    char align = 0;      // 0 | '<' | '>' | '^'
    char sign = 0;       // 0 | '+' | '-' | ' ' ('-' is the default)
    bool zero = false;   // '0' fill flag
    int width = 0;       // 0 = none
    int precision = -1;  // -1 = none
    char type = 0;       // 0 | 'x' | 'X' | 'o' | 'b' | 'f' | 'e' | 's'
};

// Which category a buildPrintFormat failure belongs to, mapped by sema to the
// stable diagnostic codes E2020-E2022 (FMT-11).
enum class PrintFormatError {
    None,
    ArgCount,   // E2020: placeholder count != argument count
    SpecType,   // E2021: spec not valid for the argument's kind
    SpecSyntax, // E2022: malformed / unknown spec text
};

// Parse the text after ':' inside a placeholder. Returns false (SpecSyntax
// category) on malformed input.
bool parsePrintSpec(const std::string& spec, PrintSpec& out, std::string& error);

// Build the printf conversion for one placeholder against its argument kind.
// When the spec uses centering or a custom fill character the value must be
// pre-rendered and padded by codegen (outNeedsRender); the conversion is then
// "%s" and no width/fill flags are emitted. Returns false (SpecType category)
// when the spec does not apply to the kind.
bool specToPrintfConversion(PrintArgKind kind, const PrintSpec& spec,
                            std::string& outConv, bool& outNeedsRender,
                            std::string& error);

// Build a C printf format string from a literal that uses fmt/spdlog-style
// `{}` placeholders. `kinds` describes each placeholder in order. When
// `newline` is true a trailing '\n' is appended. Returns false and fills
// `error` on malformed input. `outSpecs` (optional) receives one PrintSpec per
// placeholder; `errKind` (optional) classifies the failure for diagnostics.
bool buildPrintFormat(const std::string& literal,
                      const std::vector<PrintArgKind>& kinds,
                      bool newline,
                      std::string& outFormat,
                      std::string& error,
                      std::vector<PrintSpec>* outSpecs = nullptr,
                      PrintFormatError* errKind = nullptr);

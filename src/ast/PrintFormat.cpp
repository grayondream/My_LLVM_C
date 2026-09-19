#include "PrintFormat.h"

namespace {

Type* stripTypedefs(Type* type) {
    while (type && type->kind == TypeKind::Typedef) {
        type = static_cast<TypedefType*>(type)->aliasedType;
    }
    return type;
}

// Map one placeholder to a printf conversion. `spec` is the text after ':'
// inside the braces, empty for the default conversion.
std::string conversionFor(PrintArgKind kind, const std::string& spec, std::string& error) {
    const char* def = "d";
    bool isFloat = false;
    bool is64 = false;
    bool acceptsSpec = true;

    switch (kind) {
        case PrintArgKind::Int32:  def = "d";   break;
        case PrintArgKind::Int64:  def = "lld"; is64 = true; break;
        case PrintArgKind::UInt32: def = "u";   break;
        case PrintArgKind::UInt64: def = "llu"; is64 = true; break;
        case PrintArgKind::Char:   def = "c";   acceptsSpec = false; break;
        case PrintArgKind::Float:  def = "g";   isFloat = true; break;
        case PrintArgKind::CString: def = "s";  acceptsSpec = false; break;
        case PrintArgKind::Pointer: def = "p";  acceptsSpec = false; break;
        case PrintArgKind::Bool:    def = "s";  acceptsSpec = false; break;
        case PrintArgKind::ToString: def = "s"; acceptsSpec = false; break;
    }

    if (spec.empty()) {
        return std::string("%") + def;
    }
    if (!acceptsSpec) {
        error = "format spec is not supported for this argument type";
        return "";
    }

    const char conv = spec.back();
    const bool validConv = isFloat
        ? (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
           conv == 'g' || conv == 'G' || conv == 'a' || conv == 'A')
        : (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' ||
           conv == 'x' || conv == 'X');
    if (!validConv) {
        error = "invalid format spec ':" + spec + "'";
        return "";
    }

    for (size_t i = 0; i + 1 < spec.size(); ++i) {
        const char c = spec[i];
        const bool ok = c == '+' || c == '-' || c == ' ' || c == '#' || c == '0' ||
                        c == '.' || c == '*' || (c >= '0' && c <= '9') ||
                        c == 'h' || c == 'l' || c == 'L' || c == 'z' || c == 'j' || c == 't';
        if (!ok) {
            error = "invalid format spec ':" + spec + "'";
            return "";
        }
    }

    std::string s = spec;
    // 64-bit values need a length modifier matching the promoted argument.
    if (is64) {
        const bool hasLength = s.find('l') != std::string::npos ||
                               s.find('z') != std::string::npos ||
                               s.find('j') != std::string::npos ||
                               s.find('t') != std::string::npos;
        if (!hasLength) {
            s.insert(s.size() - 1, "ll");
        }
    }
    return "%" + s;
}

} // namespace

bool builtinPrintKind(Type* type, PrintArgKind& outKind) {
    type = stripTypedefs(type);
    if (!type) return false;

    switch (type->kind) {
        case TypeKind::Bool:    outKind = PrintArgKind::Bool;    return true;
        case TypeKind::Char:    outKind = PrintArgKind::Char;    return true;
        case TypeKind::Int:
        case TypeKind::Int8:
        case TypeKind::Int16:
        case TypeKind::Int32:   outKind = PrintArgKind::Int32;   return true;
        case TypeKind::ISize:
        case TypeKind::Int64:   outKind = PrintArgKind::Int64;   return true;
        case TypeKind::UInt8:
        case TypeKind::UInt16:
        case TypeKind::UInt32:  outKind = PrintArgKind::UInt32;  return true;
        case TypeKind::UInt64:
        case TypeKind::USize:   outKind = PrintArgKind::UInt64;  return true;
        case TypeKind::Float:
        case TypeKind::Float32:
        case TypeKind::Float64:
        case TypeKind::Double:  outKind = PrintArgKind::Float;   return true;
        case TypeKind::Enum:    outKind = PrintArgKind::Int32;   return true;
        case TypeKind::Pointer:
            outKind = (type->base && type->base->kind == TypeKind::Char)
                          ? PrintArgKind::CString
                          : PrintArgKind::Pointer;
            return true;
        case TypeKind::Array:
            if (type->base && type->base->kind == TypeKind::Char) {
                outKind = PrintArgKind::CString;
                return true;
            }
            return false;
        default:
            return false;
    }
}

bool buildPrintFormat(const std::string& literal,
                      const std::vector<PrintArgKind>& kinds,
                      bool newline,
                      std::string& outFormat,
                      std::string& error) {
    outFormat.clear();
    error.clear();

    size_t slot = 0;
    for (size_t i = 0; i < literal.size();) {
        const char c = literal[i];
        if (c == '{') {
            if (i + 1 < literal.size() && literal[i + 1] == '{') {
                outFormat += '{';
                i += 2;
                continue;
            }
            ++i; // consume '{'
            std::string spec;
            if (i < literal.size() && literal[i] == ':') {
                ++i;
                while (i < literal.size() && literal[i] != '}') {
                    spec += literal[i++];
                }
            }
            if (i >= literal.size() || literal[i] != '}') {
                error = "unterminated '{' in print format";
                return false;
            }
            ++i; // consume '}'
            if (slot >= kinds.size()) {
                error = "too few arguments for print format";
                return false;
            }
            std::string conversion = conversionFor(kinds[slot++], spec, error);
            if (!error.empty()) return false;
            outFormat += conversion;
        } else if (c == '}') {
            if (i + 1 < literal.size() && literal[i + 1] == '}') {
                outFormat += '}';
                i += 2;
                continue;
            }
            error = "single '}' in print format; use '}}' for a literal brace";
            return false;
        } else if (c == '%') {
            outFormat += "%%";
            ++i;
        } else {
            outFormat += c;
            ++i;
        }
    }

    if (slot != kinds.size()) {
        error = "too many arguments for print format";
        return false;
    }
    if (newline) {
        outFormat += '\n';
    }
    return true;
}

#include "PrintFormat.h"

namespace {

Type* stripTypedefs(Type* type) {
    while (type && type->kind == TypeKind::Typedef) {
        type = static_cast<TypedefType*>(type)->aliasedType;
    }
    return type;
}

// Default conversion for a `{}` placeholder (no spec text).
static std::string defaultConversionFor(PrintArgKind kind) {
    const char* def = "d";
    switch (kind) {
        case PrintArgKind::Int32:  def = "d";   break;
        case PrintArgKind::Int64:  def = "lld"; break;
        case PrintArgKind::UInt32: def = "u";   break;
        case PrintArgKind::UInt64: def = "llu"; break;
        case PrintArgKind::Char:   def = "c";   break;
        case PrintArgKind::Float:  def = "g";   break;
        case PrintArgKind::CString: def = "s";  break;
        case PrintArgKind::Str:     def = ".*s"; break;
        case PrintArgKind::Pointer: def = "p";  break;
        case PrintArgKind::Bool:    def = "s";  break;
        case PrintArgKind::ToString: def = "s"; break;
    }
    return std::string("%") + def;
}
} // namespace

bool parsePrintSpec(const std::string& spec, PrintSpec& out, std::string& error) {
    const auto syntaxError = [&spec, &error]() {
        error = "invalid format spec ':" + spec + "'";
        return false;
    };

    out = PrintSpec{};
    size_t i = 0;

    // [fill]align? — fill only applies when directly followed by an align char
    // (and never conflicts with the '0' flag or digits).
    if (i < spec.size() &&
        (spec[i] == '<' || spec[i] == '>' || spec[i] == '^')) {
        out.align = spec[i];
        ++i;
    } else if (i + 1 < spec.size() && spec[i] != '{' && spec[i] != '}' &&
               spec[i] != ':' && !(spec[i] >= '0' && spec[i] <= '9') &&
               (spec[i + 1] == '<' || spec[i + 1] == '>' || spec[i + 1] == '^')) {
        out.fill = spec[i];
        out.align = spec[i + 1];
        i += 2;
    }

    // sign?
    if (i < spec.size() &&
        (spec[i] == '+' || spec[i] == '-' || spec[i] == ' ')) {
        out.sign = spec[i];
        ++i;
    }

    // '0'? (zero fill)
    if (i < spec.size() && spec[i] == '0') {
        out.zero = true;
        ++i;
    }

    // width?
    const size_t wStart = i;
    while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') ++i;
    if (i > wStart) out.width = std::stoi(spec.substr(wStart, i - wStart));

    // ('.' precision)?
    if (i < spec.size() && spec[i] == '.') {
        ++i;
        const size_t pStart = i;
        while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') ++i;
        if (i == pStart) return syntaxError();
        out.precision = std::stoi(spec.substr(pStart, i - pStart));
    }

    // '0' fill conflicts with left/center alignment (checked before the type
    // position so an align character here reports the real conflict).
    if (out.zero && i < spec.size() &&
        (spec[i] == '<' || spec[i] == '^')) {
        error = "zero fill requires right alignment";
        return false;
    }

    // type?
    if (i < spec.size()) {
        const char t = spec[i];
        if (t == 'x' || t == 'X' || t == 'o' || t == 'b' || t == 'f' ||
            t == 'e' || t == 's') {
            out.type = t;
            ++i;
        } else {
            return syntaxError();
        }
    }
    if (i != spec.size()) return syntaxError();

    // Cross-field conflicts (SpecSyntax).
    if (out.zero) {
        if (out.align == '<' || out.align == '^') {
            error = "zero fill requires right alignment";
            return false;
        }
        if (out.width == 0) {
            error = "'0' fill requires a width";
            return false;
        }
    }
    if (out.align != 0 && out.width == 0) {
        error = "alignment requires a width";
        return false;
    }
    if (out.width > 200 || out.precision > 200) {
        error = "format spec width/precision too large";
        return false;
    }
    return true;
}

bool specToPrintfConversion(PrintArgKind kind, const PrintSpec& spec,
                            std::string& outConv, bool& outNeedsRender,
                            std::string& error) {
    outConv.clear();
    outNeedsRender = false;
    error.clear();

    const bool isInt = kind == PrintArgKind::Int32 ||
                       kind == PrintArgKind::Int64 ||
                       kind == PrintArgKind::UInt32 ||
                       kind == PrintArgKind::UInt64;
    const bool is64 =
        kind == PrintArgKind::Int64 || kind == PrintArgKind::UInt64;
    const bool isFloat = kind == PrintArgKind::Float;
    const bool isStr = kind == PrintArgKind::Str;
    const bool isStrLike =
        isStr || kind == PrintArgKind::Bool || kind == PrintArgKind::ToString;

    // Char literals, char* buffers and raw pointers take no spec at all.
    if (kind == PrintArgKind::Char || kind == PrintArgKind::CString ||
        kind == PrintArgKind::Pointer) {
        error = "format spec is not supported for this argument type";
        return false;
    }

    // Type legality against the kind.
    if (spec.type) {
        const bool okType =
            (isInt && (spec.type == 'x' || spec.type == 'X' ||
                       spec.type == 'o' || spec.type == 'b')) ||
            (isFloat && (spec.type == 'f' || spec.type == 'e')) ||
            (isStrLike && spec.type == 's');
        if (!okType) {
            error = "format spec is not supported for this argument type";
            return false;
        }
    }

    // Precision is a floating-point-only component.
    if (spec.precision >= 0 && !isFloat) {
        error = "format spec is not supported for this argument type";
        return false;
    }

    // '0' fill applies to numeric conversions only.
    if (spec.zero && !isInt && !isFloat) {
        error = "format spec is not supported for this argument type";
        return false;
    }

    // Centering or a custom fill character is not expressible in printf:
    // the caller renders the bare value and pads it.
    if (spec.align == '^' || (spec.fill != ' ' && spec.fill != '\0')) {
        outNeedsRender = true;
        outConv = "%s";
        return true;
    }

    std::string flags;
    if (spec.sign == '+') flags += '+';
    else if (spec.sign == ' ') flags += ' ';
    if (spec.zero) flags += '0';
    // Strings default to left alignment when a width is present; numbers
    // default to printf's right alignment.
    const bool leftAlign =
        spec.align == '<' ||
        (spec.align == 0 && spec.width > 0 && isStrLike);
    if (leftAlign) flags += '-';

    std::string tail;
    if (isInt) {
        if (spec.type) {
            tail = std::string(is64 ? "ll" : "");
            tail += spec.type;
        } else {
            tail = kind == PrintArgKind::Int32   ? "d"
                   : kind == PrintArgKind::Int64   ? "lld"
                   : kind == PrintArgKind::UInt32  ? "u"
                                                   : "llu";
        }
    } else if (isFloat) {
        // Non-empty spec without a type infers 'f'; the empty `{}` default
        // ('g', FMT-06) never reaches this function.
        tail = spec.type ? std::string(1, spec.type) : "f";
    } else if (isStr) {
        tail = ".*s"; // two printf arguments: (int)len, ptr
    } else {
        tail = "s";
    }

    std::string out = "%";
    out += flags;
    if (spec.width > 0) out += std::to_string(spec.width);
    if (spec.precision >= 0) {
        out += '.';
        out += std::to_string(spec.precision);
    }
    out += tail;
    outConv = std::move(out);
    return true;
}

bool builtinPrintKind(Type* type, PrintArgKind& outKind) {
    type = stripTypedefs(type);
    if (!type) return false;

    switch (type->kind) {
        case TypeKind::Bool:    outKind = PrintArgKind::Bool;    return true;
        case TypeKind::Char:    outKind = PrintArgKind::Char;    return true;
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
        case TypeKind::Float32:
        case TypeKind::Float64:
        // TYP-04: float16/float128 print through the same path, promoted to
        // double (float16 losslessly, float128 truncated).
        case TypeKind::Float16:
        case TypeKind::Float128: outKind = PrintArgKind::Float;   return true;
        case TypeKind::Enum:    outKind = PrintArgKind::Int32;   return true;
        case TypeKind::Str:     outKind = PrintArgKind::Str;     return true;
        // P1-09: `string` projects to its str view for print/format.
        case TypeKind::String:  outKind = PrintArgKind::Str;     return true;
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
                      std::string& error,
                      std::vector<PrintSpec>* outSpecs,
                      PrintFormatError* errKind) {
    outFormat.clear();
    error.clear();
    if (outSpecs) outSpecs->clear();
    if (errKind) *errKind = PrintFormatError::None;

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
                if (errKind) *errKind = PrintFormatError::ArgCount;
                return false;
            }
            ++i; // consume '}'
            if (slot >= kinds.size()) {
                error = "too few arguments for print format";
                if (errKind) *errKind = PrintFormatError::ArgCount;
                return false;
            }
            std::string conversion;
            if (spec.empty()) {
                conversion = defaultConversionFor(kinds[slot]);
                if (outSpecs) outSpecs->push_back(PrintSpec{});
            } else {
                PrintSpec parsed;
                if (!parsePrintSpec(spec, parsed, error)) {
                    if (errKind) *errKind = PrintFormatError::SpecSyntax;
                    return false;
                }
                bool needsRender = false;
                if (!specToPrintfConversion(kinds[slot], parsed, conversion,
                                            needsRender, error)) {
                    if (errKind) *errKind = PrintFormatError::SpecType;
                    return false;
                }
                if (outSpecs) outSpecs->push_back(parsed);
            }
            outFormat += conversion;
            ++slot;
        } else if (c == '}') {
            if (i + 1 < literal.size() && literal[i + 1] == '}') {
                outFormat += '}';
                i += 2;
                continue;
            }
            error = "single '}' in print format; use '}}' for a literal brace";
            if (errKind) *errKind = PrintFormatError::ArgCount;
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
        if (errKind) *errKind = PrintFormatError::ArgCount;
        return false;
    }
    if (newline) {
        outFormat += '\n';
    }
    return true;
}

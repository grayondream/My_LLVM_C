#include "sema/Diagnostic.h"

namespace {

const DiagnosticInfo kUnknown{DiagnosticCode::None, "", "unknown diagnostic"};

const DiagnosticInfo kRegistry[] = {
    {DiagnosticCode::LexInvalidCharacter,       "E0001", "invalid character"},
    {DiagnosticCode::LexUnterminatedLiteral,     "E0002", "unterminated literal"},
    {DiagnosticCode::LexIntegerOverflow,         "E0003", "integer literal overflow"},

    {DiagnosticCode::SynUnexpectedToken,         "E1001", "unexpected token"},
    {DiagnosticCode::SynExpected,                "E1002", "expected token"},

    {DiagnosticCode::SemUndeclaredIdentifier,    "E2001", "undeclared identifier"},
    {DiagnosticCode::SemTypeMismatch,            "E2002", "type mismatch"},
    {DiagnosticCode::SemIncompatibleAssignment,  "E2003", "incompatible assignment"},
    {DiagnosticCode::SemRedefinition,            "E2004", "redefinition"},
    {DiagnosticCode::SemInvalidOperand,          "E2005", "invalid operand"},
    {DiagnosticCode::SemIncompatibleCast,        "E2006", "incompatible cast"},
    {DiagnosticCode::SemUnresolvedCall,          "E2007", "unresolved call"},
    {DiagnosticCode::SemAmbiguousCall,           "E2008", "ambiguous call"},

    {DiagnosticCode::WarnUninitializedVariable,  "W3001", "variable may be uninitialized"},
    {DiagnosticCode::WarnUnusedVariable,         "W3002", "unused variable"},
    {DiagnosticCode::WarnUnreachableCode,        "W3003", "unreachable code"},
    {DiagnosticCode::WarnDeprecated,             "W3004", "deprecated API"},
};

} // namespace

const DiagnosticInfo& diagnosticInfo(DiagnosticCode code) {
    for (const auto& info : kRegistry) {
        if (info.code == code) {
            return info;
        }
    }
    return kUnknown;
}

std::string diagnosticId(DiagnosticCode code) {
    return diagnosticInfo(code).id;
}

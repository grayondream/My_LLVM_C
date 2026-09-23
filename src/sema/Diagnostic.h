#pragma once
#include <string>
#include <sstream>
#include <vector>

// Stable diagnostic codes (TODO INF-03 / INF-13).
// Numbering: E0xxx lex, E1xxx parse, E2xxx semantic, W3xxx analysis warnings.
enum class DiagnosticCode {
    None = 0,

    LexInvalidCharacter,
    LexUnterminatedLiteral,
    LexIntegerOverflow,

    SynUnexpectedToken,
    SynExpected,

    SemUndeclaredIdentifier,
    SemTypeMismatch,
    SemIncompatibleAssignment,
    SemRedefinition,
    SemInvalidOperand,
    SemIncompatibleCast,
    SemUnresolvedCall,
    SemAmbiguousCall,

    WarnUninitializedVariable,
    WarnUnusedVariable,
    WarnUnreachableCode,
    WarnDeprecated,
};

// Human-readable metadata for a code (the registry, INF-13).
struct DiagnosticInfo {
    DiagnosticCode code;
    const char* id;          // e.g. "E2001"
    const char* description; // e.g. "undeclared identifier"
};

// Returns the registry entry for `code`; id/description are "" / "unknown"
// for DiagnosticCode::None or an unregistered value.
const DiagnosticInfo& diagnosticInfo(DiagnosticCode code);

// Shorthand for diagnosticInfo(code).id.
std::string diagnosticId(DiagnosticCode code);

struct Diagnostic {
    enum class Level { Note, Warning, Error, Fatal };

    Level level;
    DiagnosticCode code;
    std::string message;
    std::string file;
    int line;
    int column;
    std::vector<std::string> fixes; // optional fix-it suggestions

    // Back-compatible constructor (no code).
    Diagnostic(Level level, std::string msg, std::string file, int line, int col)
        : level(level), code(DiagnosticCode::None), message(std::move(msg)),
          file(std::move(file)), line(line), column(col) {}

    // Code-carrying constructor.
    Diagnostic(Level level, DiagnosticCode code, std::string msg,
               std::string file, int line, int col)
        : level(level), code(code), message(std::move(msg)),
          file(std::move(file)), line(line), column(col) {}

    void addFix(std::string fix) { fixes.push_back(std::move(fix)); }

    static const char* severityName(Level level) {
        switch (level) {
            case Level::Note:    return "note";
            case Level::Warning: return "warning";
            case Level::Error:   return "error";
            case Level::Fatal:   return "fatal";
        }
        return "error";
    }

    bool isError() const { return level == Level::Error || level == Level::Fatal; }
    bool isWarning() const { return level == Level::Warning; }

    const char* severity() const { return severityName(level); }

    // Legacy human-readable format (kept stable for existing consumers).
    std::string format() const {
        std::ostringstream oss;
        oss << severity() << ": " << message;
        if (!file.empty()) {
            oss << "\n  --> " << file << ":" << line << ":" << column;
        }
        return oss.str();
    }

    // Machine-friendly, code-tagged format (INF-13):
    //   file:line:col: error[E2001]: message
    //     fix: <suggestion>
    std::string formatWithSeverity() const {
        std::ostringstream oss;
        if (!file.empty()) {
            oss << file << ":" << line << ":" << column << ": ";
        }
        oss << severity();
        const std::string id = diagnosticId(code);
        if (!id.empty()) {
            oss << "[" << id << "]";
        }
        oss << ": " << message;
        for (const auto& fix : fixes) {
            oss << "\n  fix: " << fix;
        }
        return oss.str();
    }
};

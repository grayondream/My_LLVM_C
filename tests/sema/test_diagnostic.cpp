#include <gtest/gtest.h>
#include "sema/Diagnostic.h"

TEST(DiagnosticTest, ErrorCreation) {
    Diagnostic diag(Diagnostic::Level::Error, "undeclared variable", "test.c", 5, 10);
    EXPECT_EQ(diag.level, Diagnostic::Level::Error);
    EXPECT_EQ(diag.message, "undeclared variable");
    EXPECT_EQ(diag.file, "test.c");
    EXPECT_EQ(diag.line, 5);
    EXPECT_EQ(diag.column, 10);
}

TEST(DiagnosticTest, FormatOutput) {
    Diagnostic diag(Diagnostic::Level::Error, "type mismatch", "main.c", 10, 5);
    std::string formatted = diag.format();
    EXPECT_NE(formatted.find("error:"), std::string::npos);
    EXPECT_NE(formatted.find("main.c:10:5"), std::string::npos);
}

TEST(DiagnosticTest, WarningCreation) {
    Diagnostic diag(Diagnostic::Level::Warning, "unused variable", "test.c", 1, 1);
    EXPECT_EQ(diag.level, Diagnostic::Level::Warning);
}

TEST(DiagnosticTest, FormatContainsMessage) {
    Diagnostic diag(Diagnostic::Level::Error, "expected ';' after expression", "test.c", 15, 20);
    std::string formatted = diag.format();
    EXPECT_NE(formatted.find("expected ';' after expression"), std::string::npos);
}

TEST(DiagnosticTest, FormatWithSeverityContainsErrorCode) {
    Diagnostic diag(Diagnostic::Level::Error, "type mismatch in assignment", "main.c", 10, 5);
    std::string formatted = diag.formatWithSeverity();
    EXPECT_NE(formatted.find("error"), std::string::npos);
    EXPECT_NE(formatted.find("type mismatch in assignment"), std::string::npos);
    EXPECT_NE(formatted.find("main.c:10:5"), std::string::npos);
}

TEST(DiagnosticTest, WarningFormatContainsWarningPrefix) {
    Diagnostic diag(Diagnostic::Level::Warning, "incompatible cast", "test.c", 3, 7);
    std::string formatted = diag.format();
    EXPECT_NE(formatted.find("warning:"), std::string::npos);
    EXPECT_NE(formatted.find("incompatible cast"), std::string::npos);
    EXPECT_NE(formatted.find("test.c:3:7"), std::string::npos);
}

TEST(DiagnosticTest, EmptyFilenameStillFormats) {
    Diagnostic diag(Diagnostic::Level::Error, "parse error", "", 0, 0);
    std::string formatted = diag.format();
    EXPECT_NE(formatted.find("error:"), std::string::npos);
    EXPECT_NE(formatted.find("parse error"), std::string::npos);
}

TEST(DiagnosticTest, FormatMatchesStandardCompilerFormat) {
    Diagnostic diag(Diagnostic::Level::Error, "undeclared identifier 'x'", "src/main.c", 42, 8);
    std::string formatted = diag.format();
    std::string expected = "error: undeclared identifier 'x'\n  --> src/main.c:42:8";
    EXPECT_EQ(formatted, expected);
}

// ===== INF-13: code registry and code-tagged formatting =====

TEST(DiagnosticTest, CodeRegistryLookup) {
    EXPECT_EQ(diagnosticId(DiagnosticCode::SemUndeclaredIdentifier), "E2001");
    EXPECT_EQ(diagnosticId(DiagnosticCode::SemIncompatibleAssignment), "E2003");
    EXPECT_EQ(diagnosticId(DiagnosticCode::WarnUnusedVariable), "W3002");
    EXPECT_EQ(diagnosticId(DiagnosticCode::None), "");
    EXPECT_EQ(std::string(diagnosticInfo(DiagnosticCode::SemAmbiguousCall).description),
              "ambiguous call");
}

TEST(DiagnosticTest, FormatWithSeverityIncludesCode) {
    Diagnostic diag(Diagnostic::Level::Error, DiagnosticCode::SemTypeMismatch,
                    "type mismatch", "main.c", 10, 5);
    std::string formatted = diag.formatWithSeverity();
    EXPECT_NE(formatted.find("main.c:10:5:"), std::string::npos);
    EXPECT_NE(formatted.find("error[E2002]"), std::string::npos);
    EXPECT_NE(formatted.find("type mismatch"), std::string::npos);
}

TEST(DiagnosticTest, FormatWithSeverityOmitsUnknownCode) {
    Diagnostic diag(Diagnostic::Level::Warning, "plain warning", "test.c", 1, 1);
    std::string formatted = diag.formatWithSeverity();
    EXPECT_NE(formatted.find("warning:"), std::string::npos);
    EXPECT_EQ(formatted.find("[]"), std::string::npos);
}

TEST(DiagnosticTest, FixSuggestionsAreRendered) {
    Diagnostic diag(Diagnostic::Level::Error, DiagnosticCode::SemUndeclaredIdentifier,
                    "use of undeclared identifier 'coun'", "test.c", 3, 12);
    diag.addFix("did you mean 'count'?");
    std::string formatted = diag.formatWithSeverity();
    EXPECT_NE(formatted.find("fix: did you mean 'count'?"), std::string::npos);
}

TEST(DiagnosticTest, SeverityHelpers) {
    Diagnostic err(Diagnostic::Level::Error, "e", "f", 1, 1);
    Diagnostic warn(Diagnostic::Level::Warning, "w", "f", 1, 1);
    EXPECT_TRUE(err.isError());
    EXPECT_FALSE(err.isWarning());
    EXPECT_TRUE(warn.isWarning());
    EXPECT_FALSE(warn.isError());
}

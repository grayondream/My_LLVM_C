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

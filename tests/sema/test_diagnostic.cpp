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

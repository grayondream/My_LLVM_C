#include <gtest/gtest.h>
#include "libsafec/safec.h"

TEST(SafecPrintfTest, SprintfSimple) {
    char buf[64];
    int len = safec::sprintf(buf, "hello");
    EXPECT_STREQ(buf, "hello");
    EXPECT_EQ(len, 5);
}

TEST(SafecPrintfTest, SprintfInt) {
    char buf[64];
    int len = safec::sprintf(buf, "value=%d", 42);
    EXPECT_STREQ(buf, "value=42");
    EXPECT_EQ(len, 8);
}

TEST(SafecPrintfTest, SprintfNegativeInt) {
    char buf[64];
    safec::sprintf(buf, "%d", -123);
    EXPECT_STREQ(buf, "-123");
}

TEST(SafecPrintfTest, SprintfString) {
    char buf[64];
    safec::sprintf(buf, "str=%s", "abc");
    EXPECT_STREQ(buf, "str=abc");
}

TEST(SafecPrintfTest, SprintfChar) {
    char buf[64];
    safec::sprintf(buf, "ch=%c", 'X');
    EXPECT_STREQ(buf, "ch=X");
}

TEST(SafecPrintfTest, SprintfPercent) {
    char buf[64];
    safec::sprintf(buf, "100%%");
    EXPECT_STREQ(buf, "100%");
}

TEST(SafecPrintfTest, SprintfMixed) {
    char buf[64];
    safec::sprintf(buf, "%s=%d", "count", 7);
    EXPECT_STREQ(buf, "count=7");
}

TEST(SafecPrintfTest, SprintfUnsigned) {
    char buf[64];
    safec::sprintf(buf, "%u", 42u);
    EXPECT_STREQ(buf, "42");
}

TEST(SafecPrintfTest, SprintfEmpty) {
    char buf[64];
    int len = safec::sprintf(buf, "");
    EXPECT_STREQ(buf, "");
    EXPECT_EQ(len, 0);
}

TEST(SafecPrintfTest, SprintfNullString) {
    char buf[64];
    safec::sprintf(buf, "%s", nullptr);
    EXPECT_STREQ(buf, "(null)");
}

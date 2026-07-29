#include <gtest/gtest.h>
#include "libc/libc.h"
#include <cstring>

using namespace mylibc;

TEST(MyPrintfTest, SprintfSimple) {
    char buf[64];
    int len = my_sprintf(buf, "hello");
    EXPECT_STREQ(buf, "hello");
    EXPECT_EQ(len, 5);
}

TEST(MyPrintfTest, SprintfInt) {
    char buf[64];
    int len = my_sprintf(buf, "value=%d", 42);
    EXPECT_STREQ(buf, "value=42");
    EXPECT_EQ(len, 8);
}

TEST(MyPrintfTest, SprintfNegativeInt) {
    char buf[64];
    my_sprintf(buf, "%d", -123);
    EXPECT_STREQ(buf, "-123");
}

TEST(MyPrintfTest, SprintfString) {
    char buf[64];
    my_sprintf(buf, "str=%s", "abc");
    EXPECT_STREQ(buf, "str=abc");
}

TEST(MyPrintfTest, SprintfChar) {
    char buf[64];
    my_sprintf(buf, "ch=%c", 'X');
    EXPECT_STREQ(buf, "ch=X");
}

TEST(MyPrintfTest, SprintfPercent) {
    char buf[64];
    my_sprintf(buf, "100%%");
    EXPECT_STREQ(buf, "100%");
}

TEST(MyPrintfTest, SprintfMixed) {
    char buf[64];
    my_sprintf(buf, "%s=%d", "count", 7);
    EXPECT_STREQ(buf, "count=7");
}

TEST(MyPrintfTest, SprintfUnsigned) {
    char buf[64];
    my_sprintf(buf, "%u", 42u);
    EXPECT_STREQ(buf, "42");
}

TEST(MyPrintfTest, SprintfEmpty) {
    char buf[64];
    int len = my_sprintf(buf, "");
    EXPECT_STREQ(buf, "");
    EXPECT_EQ(len, 0);
}

TEST(MyPrintfTest, SprintfNullString) {
    char buf[64];
    my_sprintf(buf, "%s", nullptr);
    EXPECT_STREQ(buf, "(null)");
}

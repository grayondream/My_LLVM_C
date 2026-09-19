#include <gtest/gtest.h>
#include "libsafec/safec.h"

TEST(SafecStringTest, Strlen) {
    EXPECT_EQ(safec::strlen(""), 0u);
    EXPECT_EQ(safec::strlen("a"), 1u);
    EXPECT_EQ(safec::strlen("hello"), 5u);
    EXPECT_EQ(safec::strlen("hello world"), 11u);
}

TEST(SafecStringTest, Strcmp) {
    EXPECT_EQ(safec::strcmp("abc", "abc"), 0);
    EXPECT_LT(safec::strcmp("abc", "abd"), 0);
    EXPECT_GT(safec::strcmp("abd", "abc"), 0);
    EXPECT_LT(safec::strcmp("abc", "abcd"), 0);
    EXPECT_GT(safec::strcmp("abcd", "abc"), 0);
    EXPECT_EQ(safec::strcmp("", ""), 0);
    EXPECT_LT(safec::strcmp("", "a"), 0);
    EXPECT_GT(safec::strcmp("a", ""), 0);
}

TEST(SafecStringTest, Strcpy) {
    char dest[20];
    char* result = safec::strcpy(dest, "hello");
    EXPECT_STREQ(dest, "hello");
    EXPECT_EQ(result, dest);

    safec::strcpy(dest, "");
    EXPECT_STREQ(dest, "");
}

TEST(SafecStringTest, Memcpy) {
    char src[] = "abcdef";
    char dest[10];
    safec::memcpy(dest, src, 7); // include null terminator
    EXPECT_STREQ(dest, "abcdef");

    int arr1[] = {1, 2, 3};
    int arr2[3];
    safec::memcpy(arr2, arr1, sizeof(arr1));
    EXPECT_EQ(arr2[0], 1);
    EXPECT_EQ(arr2[1], 2);
    EXPECT_EQ(arr2[2], 3);
}

TEST(SafecStringTest, Memset) {
    char buf[10];
    safec::memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(buf[i], 0);

    safec::memset(buf, 'x', 5);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(buf[i], 'x');
    for (int i = 5; i < 10; ++i)
        EXPECT_EQ(buf[i], 0);
}

TEST(SafecStringTest, Memcmp) {
    char a[] = "abc";
    char b[] = "abc";
    EXPECT_EQ(safec::memcmp(a, b, 3), 0);

    char c[] = "abd";
    EXPECT_GT(safec::memcmp(c, a, 3), 0);
    EXPECT_LT(safec::memcmp(a, c, 3), 0);

    EXPECT_EQ(safec::memcmp(a, b, 0), 0);
}

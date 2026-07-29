#include <gtest/gtest.h>
#include "libc/libc.h"

using namespace mylibc;

TEST(MyStringTest, Strlen) {
    EXPECT_EQ(my_strlen(""), 0u);
    EXPECT_EQ(my_strlen("a"), 1u);
    EXPECT_EQ(my_strlen("hello"), 5u);
    EXPECT_EQ(my_strlen("hello world"), 11u);
}

TEST(MyStringTest, Strcmp) {
    EXPECT_EQ(my_strcmp("abc", "abc"), 0);
    EXPECT_LT(my_strcmp("abc", "abd"), 0);
    EXPECT_GT(my_strcmp("abd", "abc"), 0);
    EXPECT_LT(my_strcmp("abc", "abcd"), 0);
    EXPECT_GT(my_strcmp("abcd", "abc"), 0);
    EXPECT_EQ(my_strcmp("", ""), 0);
    EXPECT_LT(my_strcmp("", "a"), 0);
    EXPECT_GT(my_strcmp("a", ""), 0);
}

TEST(MyStringTest, Strcpy) {
    char dest[20];
    char* result = my_strcpy(dest, "hello");
    EXPECT_STREQ(dest, "hello");
    EXPECT_EQ(result, dest);

    my_strcpy(dest, "");
    EXPECT_STREQ(dest, "");
}

TEST(MyStringTest, Memcpy) {
    char src[] = "abcdef";
    char dest[10];
    my_memcpy(dest, src, 7); // include null terminator
    EXPECT_STREQ(dest, "abcdef");

    int arr1[] = {1, 2, 3};
    int arr2[3];
    my_memcpy(arr2, arr1, sizeof(arr1));
    EXPECT_EQ(arr2[0], 1);
    EXPECT_EQ(arr2[1], 2);
    EXPECT_EQ(arr2[2], 3);
}

TEST(MyStringTest, Memset) {
    char buf[10];
    my_memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(buf[i], 0);

    my_memset(buf, 'x', 5);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(buf[i], 'x');
    for (int i = 5; i < 10; ++i)
        EXPECT_EQ(buf[i], 0);
}

TEST(MyStringTest, Memcmp) {
    char a[] = "abc";
    char b[] = "abc";
    EXPECT_EQ(my_memcmp(a, b, 3), 0);

    char c[] = "abd";
    EXPECT_GT(my_memcmp(c, a, 3), 0);
    EXPECT_LT(my_memcmp(a, c, 3), 0);

    EXPECT_EQ(my_memcmp(a, b, 0), 0);
}

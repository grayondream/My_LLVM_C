#include <gtest/gtest.h>
#include "libc/libc.h"

using namespace mylibc;

TEST(MyMallocTest, MallocReturnsNonNull) {
    void* p = my_malloc(64);
    EXPECT_NE(p, nullptr);
}

TEST(MyMallocTest, MallocDifferentSizes) {
    void* p1 = my_malloc(16);
    void* p2 = my_malloc(32);
    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
}

TEST(MyMallocTest, FreeDoesNotCrash) {
    void* p = my_malloc(64);
    my_free(p); // should be no-op
}

TEST(MyMallocTest, CallocZeroesMemory) {
    int* p = static_cast<int*>(my_calloc(10, sizeof(int)));
    EXPECT_NE(p, nullptr);
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(p[i], 0);
}

TEST(MyMallocTest, ReallocGrows) {
    char* p = static_cast<char*>(my_malloc(8));
    EXPECT_NE(p, nullptr);
    my_strcpy(p, "hello");

    char* p2 = static_cast<char*>(my_realloc(p, 16));
    EXPECT_NE(p2, nullptr);
    EXPECT_STREQ(p2, "hello");
}

TEST(MyMallocTest, ReallocNull) {
    void* p = my_realloc(nullptr, 32);
    EXPECT_NE(p, nullptr);
}

#include <gtest/gtest.h>
#include "libsafec/safec.h"

TEST(SafecMallocTest, MallocReturnsNonNull) {
    void* p = safec::malloc(64);
    EXPECT_NE(p, nullptr);
}

TEST(SafecMallocTest, MallocDifferentSizes) {
    void* p1 = safec::malloc(16);
    void* p2 = safec::malloc(32);
    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
}

TEST(SafecMallocTest, FreeDoesNotCrash) {
    void* p = safec::malloc(64);
    safec::free(p); // should be no-op
}

TEST(SafecMallocTest, CallocZeroesMemory) {
    int* p = static_cast<int*>(safec::calloc(10, sizeof(int)));
    EXPECT_NE(p, nullptr);
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(p[i], 0);
}

TEST(SafecMallocTest, ReallocGrows) {
    char* p = static_cast<char*>(safec::malloc(8));
    EXPECT_NE(p, nullptr);
    safec::strcpy(p, "hello");

    char* p2 = static_cast<char*>(safec::realloc(p, 16));
    EXPECT_NE(p2, nullptr);
    EXPECT_STREQ(p2, "hello");
}

TEST(SafecMallocTest, ReallocNull) {
    void* p = safec::realloc(nullptr, 32);
    EXPECT_NE(p, nullptr);
}

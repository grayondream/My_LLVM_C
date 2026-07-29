#include "libc.h"

namespace mylibc {

size_t my_strlen(const char* s) {
    size_t len = 0;
    while (s[len]) ++len;
    return len;
}

int my_strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) {
        ++s1;
        ++s2;
    }
    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}

char* my_strcpy(char* dest, const char* src) {
    char* ret = dest;
    while ((*dest++ = *src++))
        ;
    return ret;
}

void* my_memcpy(void* dest, const void* src, size_t n) {
    auto* d = static_cast<char*>(dest);
    const auto* s = static_cast<const char*>(src);
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dest;
}

void* my_memset(void* s, int c, size_t n) {
    auto* p = static_cast<char*>(s);
    for (size_t i = 0; i < n; ++i)
        p[i] = static_cast<char>(c);
    return s;
}

int my_memcmp(const void* s1, const void* s2, size_t n) {
    const auto* a = static_cast<const unsigned char*>(s1);
    const auto* b = static_cast<const unsigned char*>(s2);
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i])
            return a[i] - b[i];
    }
    return 0;
}

} // namespace mylibc

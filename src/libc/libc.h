#pragma once

#include <cstddef>
#include <cstdarg>

namespace mylibc {

// printf
int my_printf(const char* format, ...);
int my_sprintf(char* str, const char* format, ...);

// malloc
void* my_malloc(size_t size);
void  my_free(void* ptr);
void* my_calloc(size_t nmemb, size_t size);
void* my_realloc(void* ptr, size_t size);

// string
size_t my_strlen(const char* s);
int    my_strcmp(const char* s1, const char* s2);
char*  my_strcpy(char* dest, const char* src);
void*  my_memcpy(void* dest, const void* src, size_t n);
void*  my_memset(void* s, int c, size_t n);
int    my_memcmp(const void* s1, const void* s2, size_t n);

} // namespace mylibc

#pragma once

#include <cstddef>
#include <cstdarg>

// libsafec: a small, self-contained implementation of the C standard library
// primitives used by the compiler and its tests. Every entry point lives in
// the `safec` namespace and mirrors the corresponding C standard name.
namespace safec {

// <stdio.h> (formatted output)
int printf(const char* format, ...);
int sprintf(char* str, const char* format, ...);

// <stdlib.h> (memory allocation)
void* malloc(size_t size);
void  free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);

// <string.h>
size_t strlen(const char* s);
int    strcmp(const char* s1, const char* s2);
char*  strcpy(char* dest, const char* src);
void*  memcpy(void* dest, const void* src, size_t n);
void*  memset(void* s, int c, size_t n);
int    memcmp(const void* s1, const void* s2, size_t n);

} // namespace safec

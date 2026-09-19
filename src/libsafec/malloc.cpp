#include "safec.h"
#include <cstdlib>
#include <cstring>

namespace safec {

static constexpr size_t POOL_SIZE = 1024 * 1024; // 1 MB
static char pool[POOL_SIZE];
static size_t bump_offset = 0;

void* malloc(size_t size) {
    if (bump_offset + size > POOL_SIZE)
        return nullptr;
    void* ptr = &pool[bump_offset];
    bump_offset += size;
    return ptr;
}

void free(void* /*ptr*/) {
    // no-op for bump allocator
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr)
        return malloc(size);
    void* new_ptr = malloc(size);
    if (new_ptr)
        memcpy(new_ptr, ptr, size);
    return new_ptr;
}

} // namespace safec

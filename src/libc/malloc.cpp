#include "libc.h"
#include <cstdlib>
#include <cstring>

namespace mylibc {

static constexpr size_t POOL_SIZE = 1024 * 1024; // 1 MB
static char pool[POOL_SIZE];
static size_t bump_offset = 0;

void* my_malloc(size_t size) {
    if (bump_offset + size > POOL_SIZE)
        return nullptr;
    void* ptr = &pool[bump_offset];
    bump_offset += size;
    return ptr;
}

void my_free(void* /*ptr*/) {
    // no-op for bump allocator
}

void* my_calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = my_malloc(total);
    if (ptr)
        my_memset(ptr, 0, total);
    return ptr;
}

void* my_realloc(void* ptr, size_t size) {
    if (!ptr)
        return my_malloc(size);
    void* new_ptr = my_malloc(size);
    if (new_ptr)
        my_memcpy(new_ptr, ptr, size);
    return new_ptr;
}

} // namespace mylibc

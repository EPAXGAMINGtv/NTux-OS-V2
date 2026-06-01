#include <stddef.h>
#include <stdlib.h>

void* operator new(size_t size) {
    if (size == 0) size = 1;
    return malloc(size);
}

void* operator new[](size_t size) {
    if (size == 0) size = 1;
    return malloc(size);
}

void operator delete(void* p) noexcept {
    if (p) free(p);
}

void operator delete[](void* p) noexcept {
    if (p) free(p);
}

void operator delete(void* p, size_t) noexcept {
    if (p) free(p);
}

void operator delete[](void* p, size_t) noexcept {
    if (p) free(p);
}

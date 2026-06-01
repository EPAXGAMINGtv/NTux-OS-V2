#include <stdlib.h>

extern "C" {
void* __dso_handle = 0;

int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
    (void)func;
    (void)arg;
    (void)dso;
    return 0;
}

void __cxa_finalize(void* dso) {
    (void)dso;
}

void __cxa_pure_virtual(void) {
    //abort();
}
}

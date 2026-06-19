#ifndef CORE_PANIC_H
#define CORE_PANIC_H

#include <drivers/framebuffer/kprint.h>

__attribute__((noreturn))
static inline void kernel_panic(void *ctx, const char *msg) {
    (void)ctx;
    kprint("[PANIC] ");
    kprint(msg);
    kprint("\n");
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

#endif

#ifndef NTUX_KUTILS_H
#define NTUX_KUTILS_H

#include <stddef.h>
#include <stdint.h>
#include <drivers/framebuffer/kprint.h>
#include <mm/kmalloc.h>
#include <lib/string.h>

static inline void* kcalloc(size_t n, size_t size) {
    size_t total = n * size;
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

static inline void* krealloc(void* ptr, size_t new_size) {
    (void)ptr;
    (void)new_size;
    return NULL;
}

static inline void serial_write(const char* str) {
    kprint_serial_only(str);
}

static inline void serial_write_num(uint64_t n) {
    char buf[24];
    int pos = 0;
    if (n == 0) {
        kprint_serial_char('0');
        return;
    }
    while (n > 0 && pos < 20) {
        buf[pos++] = '0' + (n % 10);
        n /= 10;
    }
    while (pos > 0) kprint_serial_char(buf[--pos]);
}

static inline void itoa_hex(uint64_t val, char* buf) {
    int i;
    int started = 0;
    int pos = 0;
    for (i = 60; i >= 0; i -= 4) {
        uint8_t nib = (uint8_t)((val >> i) & 0xF);
        if (nib || started || i == 0) {
            started = 1;
            buf[pos++] = nib < 10 ? '0' + nib : 'a' + nib - 10;
        }
    }
    buf[pos] = '\0';
}

static inline int atoi(const char *s) {
    int n = 0;
    int sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    return sign * n;
}

static inline void k_delay(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 100000; i++) {
        __asm__ __volatile__("pause");
    }
}

#endif

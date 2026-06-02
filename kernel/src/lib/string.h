#ifndef STRING_H
#define STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ctype.h"

typedef enum {
    OCT,
    DEC
} num_sys_t;

bool memcmp(const void *s1, const void *s2, uint64_t len);
void memset(void *addr, uint8_t val, uint64_t len);
void* memcpy(void *dest, const void *src, uint64_t len);
static inline void* memmove(void *dest, const void *src, uint64_t len) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    if (d < s) {
        for (uint64_t i = 0; i < len; i++) d[i] = s[i];
    } else {
        for (uint64_t i = len; i > 0; i--) d[i-1] = s[i-1];
    }
    return dest;
}

char *strcpy(char *__restrict dest, const char *src);
char *strncpy(char *__restrict dest, const char *src, uint64_t len);
int64_t strlen(const char *s);
int64_t strcmp(const char *a, const char *b); 
int64_t strncmp(const char *a, const char *b, uint64_t len);
int64_t strcat(char *dest, const char *src);
int64_t strncat(char *dest, const char *src, uint64_t dest_size);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
uint64_t strtol(char* s, num_sys_t type);
char *strlwr(char *s);
char *strupr(char *s);

#endif
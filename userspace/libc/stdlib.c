#include <stdlib.h>

#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <syscall.h>

typedef struct blk {
    size_t size;
    struct blk *next;
} blk_t;

/*
 * Doom needs several MiB of dynamic memory (zone allocator starts at 6 MiB).
 * Keep a larger static heap so userspace apps can run without immediate OOM.
 */
#define USER_HEAP_BYTES (32u * 1024u * 1024u)
static uint8_t g_heap[USER_HEAP_BYTES];
static size_t g_heap_break = 0;
static blk_t *g_free = 0;

static size_t align8(size_t n) {
    return (n + 7u) & ~((size_t)7u);
}

void *malloc(size_t size) {
    if (size == 0) return 0;
    size = align8(size);

    blk_t **prev = &g_free;
    blk_t *cur = g_free;
    while (cur) {
        if (cur->size >= size) {
            *prev = cur->next;
            return (void *)(cur + 1);
        }
        prev = &cur->next;
        cur = cur->next;
    }

    size_t off = align8(g_heap_break);
    size_t need = off + sizeof(blk_t) + size;
    if (need > USER_HEAP_BYTES) return 0;

    blk_t *b = (blk_t *)(void *)(g_heap + off);
    b->size = size;
    g_heap_break = need;
    return (void *)(b + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    blk_t *b = (blk_t *)ptr - 1;
    b->next = g_free;
    g_free = b;
}

void *calloc(size_t nmemb, size_t size) {
    if (!nmemb || !size) return malloc(0);
    if (nmemb > ((size_t)-1) / size) return 0;
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (!p) return 0;
    memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return 0;
    }

    blk_t *old = (blk_t *)ptr - 1;
    if (old->size >= size) return ptr;

    void *n = malloc(size);
    if (!n) return 0;
    memcpy(n, ptr, old->size);
    free(ptr);
    return n;
}

int brk(void *addr) {
    if (!addr) {
        return 0;
    }
    uintptr_t base = (uintptr_t)(void *)g_heap;
    uintptr_t ptr = (uintptr_t)addr;
    uintptr_t end = base + (uintptr_t)USER_HEAP_BYTES;
    if (ptr < base || ptr > end) {
        errno = ENOMEM;
        return -1;
    }
    g_heap_break = (size_t)(ptr - base);
    return 0;
}

void *sbrk(intptr_t increment) {
    size_t old_break = g_heap_break;
    if (increment == 0) {
        return (void *)(g_heap + old_break);
    }
    if (increment < 0) {
        size_t dec = (size_t)(-increment);
        if (dec > g_heap_break) {
            errno = ENOMEM;
            return (void *)-1;
        }
        g_heap_break -= dec;
        return (void *)(g_heap + old_break);
    }
    size_t inc = (size_t)increment;
    if (old_break + inc > USER_HEAP_BYTES) {
        errno = ENOMEM;
        return (void *)-1;
    }
    g_heap_break = old_break + inc;
    return (void *)(g_heap + old_break);
}

static long parse_long(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    long v = 0;
    while (*s >= '0' && *s <= '9') {
        v = (v * 10) + (long)(*s - '0');
        s++;
    }
    return sign * v;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static double pow10_int(int exp) {
    double result = 1.0;
    double base = 10.0;
    int e = exp < 0 ? -exp : exp;
    while (e) {
        if (e & 1) result *= base;
        base *= base;
        e >>= 1;
    }
    return exp < 0 ? (1.0 / result) : result;
}

double strtod(const char *s, char **endptr) {
    if (!s) {
        if (endptr) *endptr = (char *)s;
        return 0.0;
    }

    const char *p = s;
    while (is_space(*p)) p++;

    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    int saw_digit = 0;
    double val = 0.0;
    while (*p >= '0' && *p <= '9') {
        saw_digit = 1;
        val = val * 10.0 + (double)(*p - '0');
        p++;
    }

    if (*p == '.') {
        p++;
        double scale = 0.1;
        while (*p >= '0' && *p <= '9') {
            saw_digit = 1;
            val += (double)(*p - '0') * scale;
            scale *= 0.1;
            p++;
        }
    }

    int exp = 0;
    if ((*p == 'e' || *p == 'E') && saw_digit) {
        const char *ept = p + 1;
        int esign = 1;
        if (*ept == '-') {
            esign = -1;
            ept++;
        } else if (*ept == '+') {
            ept++;
        }
        int edig = 0;
        int eval = 0;
        while (*ept >= '0' && *ept <= '9') {
            edig = 1;
            eval = eval * 10 + (*ept - '0');
            ept++;
        }
        if (edig) {
            exp = esign * eval;
            p = ept;
        }
    }

    if (!saw_digit) {
        if (endptr) *endptr = (char *)s;
        return 0.0;
    }

    if (exp != 0) val *= pow10_int(exp);
    if (endptr) *endptr = (char *)p;
    return sign * val;
}

int atoi(const char *s) {
    return (int)parse_long(s);
}

long atol(const char *s) {
    return parse_long(s);
}

long strtol(const char *s, char **endptr, int base) {
    if (!s) {
        if (endptr) *endptr = (char *)s;
        return 0;
    }
    const char *p = s;
    while (is_space(*p)) p++;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    uint64_t v = 0;
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            p += 2;
        } else if (p[0] == '0') {
            base = 8;
            p += 1;
        } else {
            base = 10;
        }
    }
    const char *start = p;
    while (*p) {
        int c = *p;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0 || d >= base) break;
        v = v * (uint64_t)base + (uint64_t)d;
        p++;
    }
    if (endptr) *endptr = (char *)(p == start ? s : p);
    return (long)((int64_t)v * (int64_t)sign);
}

unsigned long strtoul(const char *s, char **endptr, int base) {
    long v = strtol(s, endptr, base);
    if (v < 0) return 0;
    return (unsigned long)v;
}

unsigned long long strtoull(const char *s, char **endptr, int base) {
    long v = strtol(s, endptr, base);
    if (v < 0) return 0;
    return (unsigned long long)v;
}

float strtof(const char *s, char **endptr) {
    return (float)strtod(s, endptr);
}

long double strtold(const char *s, char **endptr) {
    return (long double)strtod(s, endptr);
}

double atof(const char *s) {
    return strtod(s, 0);
}

int abs(int v) {
    return v < 0 ? -v : v;
}

long labs(long v) {
    return v < 0 ? -v : v;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    if (!key || !base || !compar || size == 0) return 0;
    size_t lo = 0;
    size_t hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const void *elem = (const unsigned char *)base + mid * size;
        int cmp = compar(key, elem);
        if (cmp == 0) return (void *)elem;
        if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return 0;
}

static void swap_bytes(uint8_t *a, uint8_t *b, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        uint8_t t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

static void qsort_rec(uint8_t *base, size_t nmemb, size_t size,
                      int (*compar)(const void *, const void *)) {
    if (nmemb < 2) return;
    size_t i = 0;
    size_t j = nmemb - 1;
    uint8_t *pivot = base + (nmemb / 2u) * size;

    for (;;) {
        while (compar(base + i * size, pivot) < 0) i++;
        while (compar(base + j * size, pivot) > 0) j--;
        if (i >= j) break;
        swap_bytes(base + i * size, base + j * size, size);
        i++;
        if (j == 0) break;
        j--;
    }
    qsort_rec(base, i, size, compar);
    qsort_rec(base + i * size, nmemb - i, size, compar);
}

__attribute__((weak)) void qsort(void *base, size_t nmemb, size_t size,
                                 int (*compar)(const void *, const void *)) {
    if (!base || !compar || size == 0) return;
    qsort_rec((uint8_t *)base, nmemb, size, compar);
}

char *getenv(const char *name) {
    if (!name) return 0;
    if (strcmp(name, "TERM") == 0) return "ansi";
    if (strcmp(name, "HOME") == 0) return "/";
    if (strcmp(name, "TMPDIR") == 0) return "/tmp";

    return 0;
}

int system(const char *cmd) {
    (void)cmd;
    return -1;
}

int putenv(const char *string) {
    (void)string;
    return 0;
}

void exit(int code) {
    sys_exit(code);
}

#include <string.h>

#include <stdlib.h>

static int ascii_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

size_t strlen(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    if (!s) return 0;
    while (n < maxlen && s[n]) n++;
    return n;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (!d || !s) return dst;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (!d || !s || n == 0) return dst;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    if (!d) return dst;
    for (size_t i = 0; i < n; ++i) d[i] = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    if (!x || !y) return (x == y) ? 0 : (x ? 1 : -1);
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    if (!p) return 0;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == (unsigned char)c) return (void *)(p + i);
    }
    return 0;
}

int strcmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strcoll(const char *a, const char *b) {
    return strcmp(a, b);
}

int strcasecmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b) {
        int ca = ascii_tolower((unsigned char)*a);
        int cb = ascii_tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return ascii_tolower((unsigned char)*a) - ascii_tolower((unsigned char)*b);
}

int strncmp(const char *a, const char *b, size_t n) {
    if (n == 0) return 0;
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return ca - cb;
        if (ca == '\0') return 0;
    }
    return 0;
}

int strncasecmp(const char *a, const char *b, size_t n) {
    if (n == 0) return 0;
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (size_t i = 0; i < n; ++i) {
        int ca = ascii_tolower((unsigned char)a[i]);
        int cb = ascii_tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (a[i] == '\0') return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    if (!dst || !src) return dst;
    char *out = dst;
    while ((*dst++ = *src++) != '\0') {
    }
    return out;
}

char *strncpy(char *dst, const char *src, size_t n) {
    if (!dst || !src) return dst;
    size_t i = 0;
    for (; i < n && src[i] != '\0'; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    if (!dst || !src) return dst;
    size_t dlen = strlen(dst);
    strcpy(dst + dlen, src);
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    if (!dst || !src) return dst;
    size_t dlen = strlen(dst);
    size_t i = 0;
    while (i < n && src[i]) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = '\0';
    return dst;
}

char *strchr(const char *s, int c) {
    if (!s) return 0;
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return ((char)c == '\0') ? (char *)s : 0;
}

char *strrchr(const char *s, int c) {
    if (!s) return 0;
    const char *last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (!needle[0]) return (char *)haystack;
    size_t nlen = strlen(needle);
    for (size_t i = 0; haystack[i]; ++i) {
        if (strncmp(haystack + i, needle, nlen) == 0) return (char *)(haystack + i);
    }
    return 0;
}

size_t strspn(const char *s, const char *accept) {
    if (!s || !accept) return 0;
    const char *p = s;
    while (*p) {
        const char *a = accept;
        int found = 0;
        while (*a) {
            if (*p == *a) {
                found = 1;
                break;
            }
            a++;
        }
        if (!found) break;
        p++;
    }
    return (size_t)(p - s);
}

char *strpbrk(const char *s, const char *accept) {
    if (!s || !accept) return 0;
    for (const char *p = s; *p; ++p) {
        for (const char *a = accept; *a; ++a) {
            if (*p == *a) return (char *)p;
        }
    }
    return 0;
}

char *strerror(int errnum) {
    (void)errnum;
    return "error";
}

char *strdup(const char *s) {
    if (!s) return 0;
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1u);
    if (!d) return 0;
    memcpy(d, s, n + 1u);
    return d;
}

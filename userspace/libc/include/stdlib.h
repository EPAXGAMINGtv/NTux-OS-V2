#ifndef USER_STDLIB_H
#define USER_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
int atoi(const char *s);
long atol(const char *s);
long strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
unsigned long long strtoull(const char *s, char **endptr, int base);
double atof(const char *s);
double strtod(const char *s, char **endptr);
int abs(int v);
long labs(long v);
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
char *getenv(const char *name);
char *realpath(const char *path, char *resolved);
int system(const char *cmd);
void abort(void);
void exit(int code);
int atexit(void (*function)(void));
int putenv(const char *string);
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));

#ifdef __cplusplus
}
#endif

#endif

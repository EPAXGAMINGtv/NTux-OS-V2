#ifndef USER_STDIO_H
#define USER_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ntux_file FILE;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define EOF (-1)
#define BUFSIZ 512

int putchar(int c);
int getchar(void);
int puts(const char *text);
size_t readline(char *buf, size_t cap);

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int snprintf(char *buf, size_t cap, const char *fmt, ...);
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...);
int sscanf(const char *s, const char *fmt, ...);
int fscanf(FILE *stream, const char *fmt, ...);

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long off, int whence);
long ftell(FILE *stream);
int fflush(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int getc(FILE *stream);
int putc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int fputc(int c, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
void rewind(FILE *stream);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);

extern FILE *stdout;
extern FILE *stderr;
extern FILE *stdin;

#ifdef __cplusplus
}
#endif

#endif

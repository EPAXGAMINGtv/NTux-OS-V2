#include <stdio.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syscall.h>
#include <window_protocol.h>

typedef struct ntux_file {
    uint8_t mode;
    uint8_t dirty;
    uint8_t eof;
    uint8_t _pad;
    uint64_t pos;
    uint64_t len;
    uint64_t cap;
    char *path;
    uint8_t *buf;
} ntux_file_t;

enum {
    NTUX_FILE_CONSOLE = 0,
    NTUX_FILE_READ = 1,
    NTUX_FILE_WRITE = 2,
};

static ntux_file_t g_stdin_obj = { NTUX_FILE_CONSOLE, 0, 0, 0, 0, 0, 0, 0, 0 };
static ntux_file_t g_stdout_obj = { NTUX_FILE_CONSOLE, 0, 0, 0, 0, 0, 0, 0, 0 };
static ntux_file_t g_stderr_obj = { NTUX_FILE_CONSOLE, 0, 0, 0, 0, 0, 0, 0, 0 };

FILE *stdin = (FILE *)&g_stdin_obj;
FILE *stdout = (FILE *)&g_stdout_obj;
FILE *stderr = (FILE *)&g_stderr_obj;

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int ascii_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static int ascii_toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

static int parse_uint(const char *s, int base, uint64_t *out, const char **endp) {
    uint64_t v = 0;
    int any = 0;
    while (*s) {
        int c = *s;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0 || d >= base) break;
        any = 1;
        v = v * (uint64_t)base + (uint64_t)d;
        s++;
    }
    if (!any) return 0;
    if (out) *out = v;
    if (endp) *endp = s;
    return 1;
}

static int parse_int(const char *s, int base, int *out, const char **endp) {
    int sign = 1;
    uint64_t v = 0;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if (!parse_uint(s, base, &v, endp)) return 0;
    if (out) *out = (int)((int64_t)v * (int64_t)sign);
    return 1;
}

static void u64_to_str(uint64_t v, unsigned base, int upper, char *out, size_t cap, size_t *out_len) {
    char tmp[32];
    size_t n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (cap == 0) {
        if (out_len) *out_len = 0;
        return;
    }
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < sizeof(tmp)) {
            tmp[n++] = digits[v % base];
            v /= base;
        }
    }
    size_t k = 0;
    while (n > 0 && k + 1 < cap) {
        out[k++] = tmp[--n];
    }
    out[k] = '\0';
    if (out_len) *out_len = k;
}

static int file_flush_write(ntux_file_t *f) {
    if (!f || !f->dirty || f->mode != NTUX_FILE_WRITE) return 0;
    long rc = sys_fs_write_file(f->path, f->buf, f->len);
    if (rc != 0) {
        const char *slash = f->path + strlen(f->path);
        while (slash > f->path && slash[-1] != '/') slash--;
        if (slash > f->path) {
            size_t parent_len = (size_t)(slash - f->path - 1u);
            size_t name_len = strlen(slash);
            if (parent_len > 0 && name_len > 0) {
                char *parent = (char *)malloc(parent_len + 1u);
                char *name = (char *)malloc(name_len + 1u);
                if (parent && name) {
                    memcpy(parent, f->path, parent_len);
                    parent[parent_len] = '\0';
                    memcpy(name, slash, name_len + 1u);
                    rc = sys_fs_create_file(parent, name, f->buf, f->len);
                }
                if (parent) free(parent);
                if (name) free(name);
            }
        }
    }
    if (rc == 0) {
        f->dirty = 0;
        return 0;
    }
    return -1;
}

static int ensure_write_cap(ntux_file_t *f, uint64_t need) {
    if (need <= f->cap) return 0;
    uint64_t new_cap = f->cap ? f->cap : 512u;
    while (new_cap < need) {
        new_cap *= 2u;
        if (new_cap > (16u * 1024u * 1024u)) return -1;
    }
    void *n = realloc(f->buf, (size_t)new_cap);
    if (!n) return -1;
    f->buf = (uint8_t *)n;
    f->cap = new_cap;
    return 0;
}

static long stdio_write(const void *buf, size_t len) {
    if (!buf || len == 0) return 0;
    const char* p = (const char*)buf;
    size_t remaining = len;
    int tid = (int)sys_get_tid();
    while (remaining > 0) {
        window_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.cmd = WINDOW_CMD_TERMINAL_WRITE;
        msg.size = sizeof(msg);
        msg.owner_tid = (uint32_t)tid;
        size_t n = remaining;
        if (n > WINDOW_MAX_TEXT - 1u) n = WINDOW_MAX_TEXT - 1u;
        memcpy(msg.text, p, n);
        msg.text[n] = '\0';
        (void)sys_deskapi_push((const char*)&msg, (uint64_t)sizeof(msg));
        p += n;
        remaining -= n;
    }
    return (long)len;
}

int putchar(int c) {
    return (int)sys_putchar((char)c);
}

int getchar(void) {
    long rc = sys_getchar();
    if (rc < 0) return -1;
    return (int)(unsigned char)rc;
}

int puts(const char *text) {
    printf(text,"\n");
}

size_t readline(char *buf, size_t cap) {
    if (!buf || cap == 0) return 0;
    size_t len = 0;
    for (;;) {
        int c = getchar();
        if (c < 0) {
            (void)sys_yield();
            continue;
        }
        if (c == '\r' || c == '\n') {
            putchar('\n');
            break;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }
        if (c >= 32 && c < 127 && (len + 1) < cap) {
            buf[len++] = (char)c;
            putchar(c);
        }
    }
    buf[len] = '\0';
    return len;
}

static int out_char(FILE *stream, char c, char **dst, size_t *rem, int *count) {
    if (stream == stdout || stream == stderr || stream == stdin) {
        if (putchar((unsigned char)c) < 0) return -1;
    } else if (dst && rem) {
        if (*rem > 1) {
            **dst = c;
            (*dst)++;
            (*rem)--;
        }
    }
    (*count)++;
    return 0;
}

static int format_to(FILE *stream, char *buf, size_t cap, const char *fmt, va_list ap) {
    char *dst = buf;
    size_t rem = cap;
    int count = 0;

    if (!fmt) return -1;

    while (*fmt) {
        if (*fmt != '%') {
            if (out_char(stream, *fmt++, stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
            continue;
        }

        fmt++;
        int zero_pad = 0;
        int width = 0;
        int precision = -1;
        int long_long_mod = 0;
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        while (is_digit(*fmt)) {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (is_digit(*fmt)) {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }
        while (*fmt == 'l') {
            long_long_mod++;
            fmt++;
        }

        char num[64];
        size_t nlen = 0;
        char sign = 0;

        switch (*fmt) {
            case 'c': {
                int v = va_arg(ap, int);
                if (out_char(stream, (char)v, stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s) {
                    if (out_char(stream, *s++, stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                }
                break;
            }
            case 'd':
            case 'i': {
                int64_t v;
                if (long_long_mod >= 2) v = va_arg(ap, long long);
                else if (long_long_mod == 1) v = va_arg(ap, long);
                else v = va_arg(ap, int);
                uint64_t uv;
                if (v < 0) {
                    sign = '-';
                    uv = (uint64_t)(-v);
                } else {
                    uv = (uint64_t)v;
                }
                u64_to_str(uv, 10u, 0, num, sizeof(num), &nlen);
                int prec_zeros = 0;
                if (precision >= 0 && (int)nlen < precision) {
                    prec_zeros = precision - (int)nlen;
                }
                int total = (int)nlen + prec_zeros + (sign ? 1 : 0);
                int pad = width - total;
                char padch = (zero_pad && precision < 0) ? '0' : ' ';
                while (pad-- > 0) {
                    if (out_char(stream, padch, stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                }
                if (sign) {
                    if (out_char(stream, sign, stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                }
                while (prec_zeros-- > 0) {
                    if (out_char(stream, '0', stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                }
                for (size_t i = 0; i < nlen; ++i) {
                    if (out_char(stream, num[i], stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                }
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'p': {
                uint64_t v;
                unsigned base = 10u;
                int upper = 0;
                if (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') {
                    base = 16u;
                    upper = (*fmt == 'X');
                }
                if (*fmt == 'p') {
                    v = (uint64_t)(uintptr_t)va_arg(ap, void *);
                } else if (long_long_mod >= 2) {
                    v = va_arg(ap, unsigned long long);
                } else if (long_long_mod == 1) {
                    v = va_arg(ap, unsigned long);
                } else {
                    v = va_arg(ap, unsigned int);
                }
                u64_to_str(v, base, upper, num, sizeof(num), &nlen);
                int prec_zeros = 0;
                if (precision >= 0 && (int)nlen < precision) {
                    prec_zeros = precision - (int)nlen;
                }
                int total = (int)nlen + prec_zeros;
                int pad = width - total;
                char padch = (zero_pad && precision < 0) ? '0' : ' ';
                while (pad-- > 0) {
                    if (out_char(stream, padch, stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                }
                while (prec_zeros-- > 0) {
                    if (out_char(stream, '0', stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                }
                for (size_t i = 0; i < nlen; ++i) {
                    if (out_char(stream, num[i], stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                }
                break;
            }
            case '%': {
                if (out_char(stream, '%', stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                break;
            }
            default: {
                if (out_char(stream, '%', stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                if (*fmt && out_char(stream, *fmt, stream ? 0 : &dst, stream ? 0 : &rem, &count) != 0) return -1;
                break;
            }
        }

        if (*fmt) fmt++;
    }

    if (!stream && cap > 0) {
        *dst = '\0';
    }
    return count;
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap) {
    va_list cp;
    va_copy(cp, ap);
    int rc = format_to(0, buf, cap, fmt, cp);
    va_end(cp);
    return rc;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return rc;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return rc;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    va_list cp;
    va_copy(cp, ap);
    int rc = format_to(stream, 0, 0, fmt, cp);
    va_end(cp);
    return rc;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = vfprintf(stream, fmt, ap);
    va_end(ap);
    return rc;
}

int vprintf(const char *fmt, va_list ap) {
    if (!fmt) return 0;
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...) {
    if (!fmt) return 0;
    va_list ap;
    va_start(ap, fmt);
    int rc = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return rc;
}

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) return 0;

    ntux_file_t *f = (ntux_file_t *)calloc(1, sizeof(ntux_file_t));
    if (!f) return 0;

    size_t plen = strlen(path);
    f->path = (char *)malloc(plen + 1u);
    if (!f->path) {
        free(f);
        return 0;
    }
    memcpy(f->path, path, plen + 1u);

    if (mode[0] == 'r') {
        f->mode = NTUX_FILE_READ;
        uint64_t len = 0;
        if (sys_fs_read_file(path, 0, 0, &len) != 0) {
            free(f->path);
            free(f);
            return 0;
        }
        f->buf = (uint8_t *)malloc((size_t)(len ? len : 1u));
        if (!f->buf) {
            free(f->path);
            free(f);
            return 0;
        }
        f->cap = len ? len : 1u;
        f->len = len;
        if (len > 0 && sys_fs_read_file(path, f->buf, len, &len) != 0) {
            free(f->buf);
            free(f->path);
            free(f);
            return 0;
        }
        f->len = len;
        return (FILE *)f;
    }

    if (mode[0] == 'w') {
        f->mode = NTUX_FILE_WRITE;
        f->buf = 0;
        f->cap = 0;
        f->len = 0;
        f->pos = 0;
        f->dirty = 1;
        return (FILE *)f;
    }

    free(f->path);
    free(f);
    return 0;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    if (stream && stream != stdin && stream != stdout && stream != stderr) {
        fclose(stream);
    }
    return fopen(path, mode);
}

FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    if (fd == 0) return stdin;
    if (fd == 1) return stdout;
    if (fd == 2) return stderr;
    errno = EBADF;
    return 0;
}

int fclose(FILE *stream) {
    ntux_file_t *f = (ntux_file_t *)stream;
    if (!f || f == &g_stdin_obj || f == &g_stdout_obj || f == &g_stderr_obj) return 0;
    int rc = file_flush_write(f);
    if (f->buf) free(f->buf);
    if (f->path) free(f->path);
    free(f);
    return rc;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    ntux_file_t *f = (ntux_file_t *)stream;
    if (!ptr || !f || size == 0 || nmemb == 0) return 0;
    if (f->mode == NTUX_FILE_CONSOLE) {
        uint8_t *out = (uint8_t *)ptr;
        uint64_t want = (uint64_t)size * (uint64_t)nmemb;
        uint64_t got = 0;
        while (got < want) {
            int c = getchar();
            if (c < 0) {
                (void)sys_yield();
                continue;
            }
            out[got++] = (uint8_t)c;
        }
        return got / size;
    }
    if (f->mode != NTUX_FILE_READ) return 0;
    uint64_t want = (uint64_t)size * (uint64_t)nmemb;
    uint64_t avail = (f->pos < f->len) ? (f->len - f->pos) : 0;
    uint64_t take = (want < avail) ? want : avail;
    if (take > 0) {
        memcpy(ptr, f->buf + f->pos, (size_t)take);
        f->pos += take;
    }
    if (take < want) f->eof = 1;
    return take / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    ntux_file_t *f = (ntux_file_t *)stream;
    if (!ptr || !f || size == 0 || nmemb == 0) return 0;
    if (f->mode == NTUX_FILE_CONSOLE) {
        uint64_t n = (uint64_t)size * (uint64_t)nmemb;
        if (stdio_write(ptr, (size_t)n) < 0) return 0;
        return nmemb;
    }
    if (f->mode != NTUX_FILE_WRITE) return 0;
    uint64_t n = (uint64_t)size * (uint64_t)nmemb;
    uint64_t end = f->pos + n;
    if (ensure_write_cap(f, end) != 0) return 0;
    memcpy(f->buf + f->pos, ptr, (size_t)n);
    f->pos = end;
    if (f->pos > f->len) f->len = f->pos;
    f->dirty = 1;
    return nmemb;
}

int fseek(FILE *stream, long off, int whence) {
    ntux_file_t *f = (ntux_file_t *)stream;
    if (!f) return -1;
    int64_t base = 0;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int64_t)f->pos;
    else if (whence == SEEK_END) base = (int64_t)f->len;
    else return -1;

    int64_t np = base + (int64_t)off;
    if (np < 0) return -1;
    if (f->mode == NTUX_FILE_READ && (uint64_t)np > f->len) return -1;
    if (f->mode == NTUX_FILE_WRITE && ensure_write_cap(f, (uint64_t)np) != 0) return -1;
    f->pos = (uint64_t)np;
    f->eof = 0;
    return 0;
}

long ftell(FILE *stream) {
    ntux_file_t *f = (ntux_file_t *)stream;
    if (!f) return -1;
    return (long)f->pos;
}

void rewind(FILE *stream) {
    (void)fseek(stream, 0, SEEK_SET);
}

int fflush(FILE *stream) {
    if (!stream || stream == stdout || stream == stderr || stream == stdin) return 0;
    return file_flush_write((ntux_file_t *)stream);
}

int feof(FILE *stream) {
    ntux_file_t *f = (ntux_file_t *)stream;
    if (!f) return 1;
    return f->eof ? 1 : 0;
}

int ferror(FILE *stream) {
    (void)stream;
    return 0;
}

int getc(FILE *stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) == 1) return (int)c;
    return EOF;
}

int fgetc(FILE *stream) {
    return getc(stream);
}

int putc(int c, FILE *stream) {
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, stream) == 1) return c;
    return EOF;
}

int fputs(const char *s, FILE *stream) {
    if (!s || !stream) return EOF;
    size_t len = strlen(s);
    if (fwrite(s, 1, len, stream) == len) return (int)len;
    return EOF;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, stream) == 1) return c;
    return EOF;
}

char *fgets(char *s, int size, FILE *stream) {
    if (!s || size <= 1 || !stream) return 0;
    ntux_file_t *f = (ntux_file_t *)stream;
    if (f->mode == NTUX_FILE_CONSOLE) {
        int i = 0;
        while (i + 1 < size) {
            int c = getchar();
            if (c < 0) {
                (void)sys_yield();
                continue;
            }
            if (c == '\r') c = '\n';
            if (c == '\b' || c == 127) {
                if (i > 0) {
                    i--;
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                }
                continue;
            }
            if (c == '\n') {
                s[i++] = (char)c;
                putchar(c);
                break;
            }
            if (c < 32 || c >= 127) {
                continue;
            }
            s[i++] = (char)c;
            putchar(c);
            if (c == '\n') break;
        }
        s[i] = '\0';
        return i > 0 ? s : 0;
    }
    if (f->mode != NTUX_FILE_READ) return 0;

    int i = 0;
    while (i + 1 < size && f->pos < f->len) {
        char c = (char)f->buf[f->pos++];
        s[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) {
        f->eof = 1;
        return 0;
    }
    s[i] = '\0';
    return s;
}

int remove(const char *path) {
    if (!path) return -1;
    return sys_fs_remove(path) == 0 ? 0 : -1;
}

int rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) return -1;
    const char *slash = new_path + strlen(new_path);
    while (slash > new_path && slash[-1] != '/') slash--;
    if (slash <= new_path) return -1;
    return sys_fs_rename(old_path, slash) == 0 ? 0 : -1;
}

static int vscan_core(const char *src, const char *fmt, va_list ap) {
    int assigned = 0;

    while (*fmt) {
        if (is_space(*fmt)) {
            while (is_space(*src)) src++;
            fmt++;
            continue;
        }

        if (*fmt != '%') {
            if (*src != *fmt) break;
            src++;
            fmt++;
            continue;
        }

        fmt++;
        int width = 0;
        while (is_digit(*fmt)) {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        if (*fmt == 's') {
            char *out = va_arg(ap, char *);
            while (is_space(*src)) src++;
            int n = 0;
            while (*src && !is_space(*src) && (width == 0 || n < width)) {
                out[n++] = *src++;
            }
            out[n] = '\0';
            if (n == 0) return assigned ? assigned : EOF;
            assigned++;
            fmt++;
            continue;
        }

        if (*fmt == '[') {
            fmt++;
            int invert = 0;
            if (*fmt == '^') {
                invert = 1;
                fmt++;
            }
            char stop = *fmt;
            while (*fmt && *fmt != ']') fmt++;
            if (*fmt == ']') fmt++;

            char *out = va_arg(ap, char *);
            int n = 0;
            while (*src && (width == 0 || n < width)) {
                int cond = (*src == stop);
                if (invert) cond = !cond;
                if (!cond) break;
                out[n++] = *src++;
            }
            out[n] = '\0';
            if (n == 0) return assigned ? assigned : EOF;
            assigned++;
            continue;
        }

        if (*fmt == 'd' || *fmt == 'i' || *fmt == 'x') {
            int *out = va_arg(ap, int *);
            int base = (*fmt == 'x') ? 16 : 10;
            while (is_space(*src)) src++;

            if (*fmt == 'i' && src[0] == '0' && (src[1] == 'x' || src[1] == 'X')) {
                src += 2;
                base = 16;
            } else if (*fmt == 'i' && src[0] == '0') {
                base = 8;
            }

            const char *endp = src;
            if (!parse_int(src, base, out, &endp)) return assigned ? assigned : EOF;
            src = endp;
            assigned++;
            fmt++;
            continue;
        }

        break;
    }

    return assigned;
}

int sscanf(const char *s, const char *fmt, ...) {
    if (!s || !fmt) return EOF;
    va_list ap;
    va_start(ap, fmt);
    int rc = vscan_core(s, fmt, ap);
    va_end(ap);
    return rc;
}

int fscanf(FILE *stream, const char *fmt, ...) {
    ntux_file_t *f = (ntux_file_t *)stream;
    if (!f || f->mode != NTUX_FILE_READ || !fmt) return EOF;

    while (f->pos < f->len && (f->buf[f->pos] == ' ' || f->buf[f->pos] == '\n' || f->buf[f->pos] == '\r' || f->buf[f->pos] == '\t')) {
        f->pos++;
    }

    if (f->pos >= f->len) {
        f->eof = 1;
        return EOF;
    }

    const char *src = (const char *)(f->buf + f->pos);
    va_list ap;
    va_start(ap, fmt);
    int rc = vscan_core(src, fmt, ap);
    va_end(ap);

    if (rc == EOF) {
        f->eof = 1;
        return EOF;
    }

    if (*fmt == '%' && strchr(fmt, '[') != 0) {
        while (f->pos < f->len && f->buf[f->pos] != '\n') f->pos++;
        if (f->pos < f->len && f->buf[f->pos] == '\n') f->pos++;
    } else {
        while (f->pos < f->len && f->buf[f->pos] != '\n') f->pos++;
        if (f->pos < f->len && f->buf[f->pos] == '\n') f->pos++;
    }

    return rc;
}

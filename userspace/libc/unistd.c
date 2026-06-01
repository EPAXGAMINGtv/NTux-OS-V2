#include <unistd.h>

#include <errno.h>
#include <ntux_path.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>

#define NTUX_PATH_MAX 256

static char g_cwd[NTUX_PATH_MAX] = "/";

const char *ntux_get_cwd(void) {
    return g_cwd;
}

static int push_segment(const char *seg, size_t seg_len, char *out, size_t *out_len, size_t cap) {
    if (seg_len == 0) return 0;
    if (*out_len > 1 && out[*out_len - 1] != '/') {
        if (*out_len + 1 >= cap) return -1;
        out[(*out_len)++] = '/';
    }
    if (*out_len == 0) {
        if (cap < 2) return -1;
        out[(*out_len)++] = '/';
    }
    if (*out_len + seg_len >= cap) return -1;
    memcpy(out + *out_len, seg, seg_len);
    *out_len += seg_len;
    out[*out_len] = '\0';
    return 0;
}

static int normalize_path(const char *in, char *out, size_t cap) {
    if (!in || cap == 0) return -1;
    size_t out_len = 0;
    out[0] = '\0';

    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t seg_len = (size_t)(p - seg);
        if (seg_len == 0) break;
        if (seg_len == 1 && seg[0] == '.') {
            continue;
        }
        if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
            if (out_len > 1) {
                out_len--;
                while (out_len > 0 && out[out_len - 1] != '/') out_len--;
                if (out_len == 0) out[out_len++] = '/';
                out[out_len] = '\0';
            }
            continue;
        }
        if (push_segment(seg, seg_len, out, &out_len, cap) != 0) return -1;
    }

    if (out_len == 0) {
        if (cap < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
    }
    return 0;
}

int ntux_resolve_path(const char *in, char *out, size_t cap) {
    if (!in || !out || cap == 0) return -1;
    if (in[0] == '/') {
        return normalize_path(in, out, cap);
    }
    char tmp[NTUX_PATH_MAX];
    size_t cwd_len = strlen(g_cwd);
    if (cwd_len == 0 || cwd_len >= sizeof(tmp)) return -1;
    if (snprintf(tmp, sizeof(tmp), "%s/%s", g_cwd, in) <= 0) return -1;
    return normalize_path(tmp, out, cap);
}

int chdir(const char *path) {
    char abspath[NTUX_PATH_MAX];
    if (ntux_resolve_path(path, abspath, sizeof(abspath)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (sys_fs_exists(abspath) != 1) {
        errno = ENOENT;
        return -1;
    }
    strncpy(g_cwd, abspath, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';
    return 0;
}

char *getcwd(char *buf, size_t size) {
    size_t len = strlen(g_cwd);
    if (!buf || size == 0 || len + 1 > size) {
        errno = EINVAL;
        return 0;
    }
    memcpy(buf, g_cwd, len + 1u);
    return buf;
}

char *realpath(const char *path, char *resolved) {
    if (!path) {
        errno = EINVAL;
        return 0;
    }
    char tmp[NTUX_PATH_MAX];
    if (ntux_resolve_path(path, tmp, sizeof(tmp)) != 0) {
        errno = EINVAL;
        return 0;
    }
    size_t len = strlen(tmp);
    if (!resolved) {
        resolved = (char *)malloc(len + 1u);
        if (!resolved) {
            errno = ENOMEM;
            return 0;
        }
    }
    memcpy(resolved, tmp, len + 1u);
    return resolved;
}

int access(const char *path, int mode) {
    (void)mode;
    char abspath[NTUX_PATH_MAX];
    if (ntux_resolve_path(path, abspath, sizeof(abspath)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (sys_fs_exists(abspath) == 1) return 0;
    errno = ENOENT;
    return -1;
}

int unlink(const char *path) {
    char abspath[NTUX_PATH_MAX];
    if (ntux_resolve_path(path, abspath, sizeof(abspath)) != 0) {
        errno = EINVAL;
        return -1;
    }
    return sys_fs_remove(abspath) == 0 ? 0 : -1;
}

int rmdir(const char *path) {
    return unlink(path);
}

int getpid(void) {
    return 1;
}

int getuid(void) {
    return (int)sys_get_uid();
}

int geteuid(void) {
    return (int)sys_get_uid();
}

int getgid(void) {
    return 0;
}

int getegid(void) {
    return 0;
}

int readlink(const char *path, char *buf, size_t bufsiz) {
    (void)path;
    (void)buf;
    (void)bufsiz;
    errno = EINVAL;
    return -1;
}

int symlink(const char *target, const char *linkpath) {
    (void)target;
    (void)linkpath;
    errno = EINVAL;
    return -1;
}

int link(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    errno = EINVAL;
    return -1;
}

int dup(int oldfd) {
    (void)oldfd;
    errno = EBADF;
    return -1;
}

int dup2(int oldfd, int newfd) {
    (void)oldfd;
    (void)newfd;
    errno = EBADF;
    return -1;
}

int execvp(const char *file, char *const argv[]) {
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

void _exit(int code) {
    sys_exit(code);
}

int getpagesize(void) {
    return 4096;
}

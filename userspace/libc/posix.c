#include <assert.h>
#include <stdint.h>
#include <sys/stat.h>
#include <syscall.h>
#include <unistd.h>
#include <window_protocol.h>

#include <string.h>
#include <ntux_io.h>
#include <ntux_path.h>

static uint64_t ntux_cached_hz = 0;

static uint64_t ntux_get_hz(void) {
    if (ntux_cached_hz) return ntux_cached_hz;
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;

    /* Calibrate against CMOS second tick once to catch VM timer drift. */
    ntux_time_t t0;
    if (sys_get_time(&t0) == 0) {
        uint8_t sec = t0.second;
        uint64_t start = sys_get_ticks();
        uint64_t deadline = start + hz * 2u;
        for (;;) {
            ntux_time_t t1;
            if (sys_get_time(&t1) == 0 && t1.second != sec) {
                uint64_t delta = sys_get_ticks() - start;
                if (delta >= 20u && delta <= 2000u) {
                    hz = delta;
                }
                break;
            }
            if (sys_get_ticks() > deadline) {
                break;
            }
            sys_yield();
        }
    }

    ntux_cached_hz = hz;
    return hz;
}

static int ntux_ticks_are_advancing(void) {
    uint64_t t0 = sys_get_ticks();
    for (int i = 0; i < 2048; ++i) {
        sys_yield();
        if (sys_get_ticks() != t0) return 1;
    }
    return 0;
}

int usleep(unsigned int usec) {
    if (usec == 0) return 0;

    /*
     * If the timer isn't ticking, sleeping would deadlock. In that case,
     * return early to avoid freezing UI apps.
     */
    if (!ntux_ticks_are_advancing()) return 0;

    uint64_t hz = ntux_get_hz();
    uint64_t usec64 = (uint64_t)usec;
    uint64_t ticks = (usec64 * hz + 1000000ull - 1ull) / 1000000ull;
    if (ticks == 0) ticks = 1;

    sys_wait_ticks(ticks);
    return 0;
}

int ntux_console_peek = -1;

int isatty(int fd) {
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

int open(const char* path, int flags, ...) {
    char abspath[256];
    const char *use = path;
    if (ntux_resolve_path(path, abspath, sizeof(abspath)) == 0) {
        use = abspath;
    }
    return (int)sys_open(use, (uint64_t)flags);
}

ssize_t read(int fd, void* buf, size_t count) {
    if (fd == 0 && buf && count > 0) {
        size_t got = 0;
        char *out = (char *)buf;
        while (got == 0) {
            if (ntux_console_peek >= 0) {
                out[got++] = (char)ntux_console_peek;
                ntux_console_peek = -1;
                break;
            }
            long ch = sys_getchar();
            if (ch < 0) {
                sys_yield();
                continue;
            }
            out[got++] = (char)ch;
        }
        while (got < count) {
            long ch = sys_getchar();
            if (ch < 0) break;
            out[got++] = (char)ch;
        }
        return (ssize_t)got;
    }
    return (ssize_t)sys_read_fd(fd, buf, (uint64_t)count);
}

ssize_t write(int fd, const void* buf, size_t count) {
    if ((fd == 1 || fd == 2) && buf && count > 0) {
        const char* p = (const char*)buf;
        size_t remaining = count;
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
        return (ssize_t)count;
    }
    return (ssize_t)sys_write_fd(fd, buf, (uint64_t)count);
}

int close(int fd) {
    return (int)sys_close(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)sys_lseek(fd, (long)offset, whence);
}

int ioctl(int fd, unsigned long request, void* arg) {
    return (int)sys_ioctl(fd, (uint64_t)request, arg);
}

int mkdir(const char *path, mode_t mode) {
    (void)mode;
    char abspath[256];
    const char *use = path;
    if (ntux_resolve_path(path, abspath, sizeof(abspath)) == 0) {
        use = abspath;
    }
    if (!use || use[0] != '/') return -1;

    const char *slash = use + strlen(use);
    while (slash > use && slash[-1] != '/') slash--;
    if (slash <= use || !slash[0]) return -1;

    char parent[256];
    size_t plen = (size_t)(slash - use - 1u);
    if (plen == 0 || plen >= sizeof(parent)) return -1;
    memcpy(parent, use, plen);
    parent[plen] = '\0';

    return sys_fs_mkdir(parent, slash) == 0 ? 0 : -1;
}

void abort(void) {
    sys_exit(1);
    for (;;) {
    }
}

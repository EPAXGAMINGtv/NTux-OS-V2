#include <drivers/tty/tty.h>
#include <drivers/framebuffer/kprint.h>
#include <drivers/input/input.h>
#include <drivers/input/console_input.h>
#include <fs/devfs.h>
#include <lib/string.h>
#include <mm/kmalloc.h>
#include <sched/thread.h>

#define TTY_TCGETS 0x5401
#define TTY_TCSETS 0x5402
#define TTY_TIOCGWINSZ 0x5413
#define TTY_ECHO 0x0008
#define TTY_ICANON 0x0002
#define TTY_ISIG 0x0001

typedef struct {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_cc[32];
} tty_termios_t;

typedef struct {
    uint8_t buf[TTY_BUF_SIZE];
    volatile int head;
    volatile int tail;
    int echo;
    int cooked;
    int isig;
    tty_termios_t termios;
} tty_t;

static tty_t g_ttys[TTY_MAX_TTYS];
static int g_tty_init_done = 0;

void tty_init(void) {
    if (g_tty_init_done) return;
    memset(g_ttys, 0, sizeof(g_ttys));
    for (int i = 0; i < TTY_MAX_TTYS; ++i) {
        g_ttys[i].echo = 1;
        g_ttys[i].cooked = 1;
        g_ttys[i].isig = 1;
        g_ttys[i].termios.c_lflag = TTY_ECHO | TTY_ICANON | TTY_ISIG;
    }
    g_tty_init_done = 1;
}

static int tty_ring_push(int tty_id, uint8_t c) {
    if (tty_id < 0 || tty_id >= TTY_MAX_TTYS) return -1;
    tty_t* tty = &g_ttys[tty_id];
    int next = (tty->head + 1) % TTY_BUF_SIZE;
    if (next == tty->tail) return -1;
    tty->buf[tty->head] = c;
    tty->head = next;
    return 0;
}

static int tty_ring_pop(int tty_id, uint8_t* out) {
    if (tty_id < 0 || tty_id >= TTY_MAX_TTYS) return -1;
    tty_t* tty = &g_ttys[tty_id];
    if (tty->tail == tty->head) return -1;
    *out = tty->buf[tty->tail];
    tty->tail = (tty->tail + 1) % TTY_BUF_SIZE;
    return 0;
}

static int tty_ring_avail(int tty_id) {
    if (tty_id < 0 || tty_id >= TTY_MAX_TTYS) return 0;
    tty_t* tty = &g_ttys[tty_id];
    int avail = tty->head - tty->tail;
    if (avail < 0) avail += TTY_BUF_SIZE;
    return avail;
}

static int read_from_input(uint8_t* out) {
    char c = 0;
    if (input_try_getchar(&c) == 0) return -1;
    *out = (uint8_t)c;
    return 0;
}

static int tty_read_cooked(int tty_id, void* buf, size_t len, size_t* out_read) {
    uint8_t* dst = (uint8_t*)buf;
    size_t pos = 0;

    while (pos < len) {
        uint8_t c = 0;
        if (tty_id == 0) {
            if (read_from_input(&c) != 0) {
                if (pos > 0) break;
                thread_yield();
                continue;
            }
        } else {
            if (tty_ring_pop(tty_id, &c) != 0) {
                if (pos > 0) break;
                thread_yield();
                continue;
            }
        }

        if (c == 3 && g_ttys[tty_id].isig) {
            continue;
        }

        if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                if (g_ttys[tty_id].echo) {
                    kprint("\b \b");
                }
            }
            continue;
        }

        if (c == '\r') c = '\n';

        if (g_ttys[tty_id].echo) {
            char s[2] = {(char)c, 0};
            kprint(s);
        }

        dst[pos++] = c;
        if (c == '\n') break;
    }

    if (out_read) *out_read = pos;
    return 0;
}

static int tty_read_raw(int tty_id, void* buf, size_t len, size_t* out_read) {
    uint8_t* dst = (uint8_t*)buf;
    size_t pos = 0;

    while (pos < len) {
        uint8_t c = 0;
        if (tty_id == 0) {
            if (read_from_input(&c) != 0) {
                if (pos > 0) break;
                thread_yield();
                continue;
            }
        } else {
            if (tty_ring_pop(tty_id, &c) != 0) {
                if (pos > 0) break;
                thread_yield();
                continue;
            }
        }

        if (g_ttys[tty_id].echo) {
            char s[2] = {(char)c, 0};
            kprint(s);
        }

        dst[pos++] = c;
        break;
    }

    if (out_read) *out_read = pos;
    return 0;
}

int tty_read(int tty_id, void* buf, size_t len, size_t* out_read) {
    if (tty_id < 0 || tty_id >= TTY_MAX_TTYS) return -1;
    if (!buf || len == 0) return -1;

    if (g_ttys[tty_id].cooked) {
        return tty_read_cooked(tty_id, buf, len, out_read);
    }
    return tty_read_raw(tty_id, buf, len, out_read);
}

int tty_write(int tty_id, const void* data, size_t len, size_t* out_written) {
    (void)tty_id;
    if (!data) return -1;

    size_t written = 0;
    for (size_t i = 0; i < len; ++i) {
        char c[2] = {((const char*)data)[i], 0};
        kprint(c);
        written++;
    }
    if (out_written) *out_written = written;
    return 0;
}

int tty_ioctl(int tty_id, uint64_t req, void* arg) {
    if (tty_id < 0 || tty_id >= TTY_MAX_TTYS) return -1;

    if (req == TTY_TCGETS) {
        if (!arg) return -1;
        tty_termios_t* out = (tty_termios_t*)arg;
        memcpy(out, &g_ttys[tty_id].termios, sizeof(tty_termios_t));
        return 0;
    }

    if (req == TTY_TCSETS) {
        if (!arg) return -1;
        tty_termios_t* in = (tty_termios_t*)arg;
        memcpy(&g_ttys[tty_id].termios, in, sizeof(tty_termios_t));
        g_ttys[tty_id].echo = (in->c_lflag & TTY_ECHO) ? 1 : 0;
        g_ttys[tty_id].cooked = (in->c_lflag & TTY_ICANON) ? 1 : 0;
        g_ttys[tty_id].isig = (in->c_lflag & TTY_ISIG) ? 1 : 0;
        return 0;
    }

    if (req == TTY_TIOCGWINSZ) {
        if (!arg) return -1;
        uint16_t* ws = (uint16_t*)arg;
        ws[0] = 25;
        ws[1] = 80;
        ws[2] = 0;
        ws[3] = 0;
        return 0;
    }

    return -1;
}

static int tty_devfs_read(void* ctx, void* out, size_t len, size_t* out_read) {
    int id = (int)(uintptr_t)ctx;
    return tty_read(id, out, len, out_read);
}

static int tty_devfs_write(void* ctx, const void* in, size_t len, size_t* out_written) {
    int id = (int)(uintptr_t)ctx;
    return tty_write(id, in, len, out_written);
}

static int tty_devfs_ioctl(void* ctx, uint64_t req, void* arg) {
    int id = (int)(uintptr_t)ctx;
    return tty_ioctl(id, req, arg);
}

static const devfs_ops_t g_tty_ops[TTY_MAX_TTYS] = {
    { .read = tty_devfs_read, .write = tty_devfs_write, .ioctl = tty_devfs_ioctl },
    { .read = tty_devfs_read, .write = tty_devfs_write, .ioctl = tty_devfs_ioctl },
    { .read = tty_devfs_read, .write = tty_devfs_write, .ioctl = tty_devfs_ioctl },
    { .read = tty_devfs_read, .write = tty_devfs_write, .ioctl = tty_devfs_ioctl },
};

int tty_register_all(void) {
    if (!g_tty_init_done) tty_init();

    for (int i = 0; i < TTY_MAX_TTYS; ++i) {
        char name[16];
        name[0] = 't'; name[1] = 't'; name[2] = 'y'; name[3] = (char)('0' + i); name[4] = '\0';
        devfs_register(name, &g_tty_ops[i], (void*)(uintptr_t)i);
    }
    return 0;
}

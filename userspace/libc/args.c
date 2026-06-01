#include <string.h>
#include <syscall.h>
#include "args.h"
#include <stdio.h>

static int g_argc = 0;
static char *g_argv[32];
static char g_buf[512];

int ntux_argc(void) {
    return g_argc;
}

char **ntux_argv(void) {
    return g_argv;
}

const char *ntux_arg(int idx) {
    if (idx < 0 || idx >= g_argc) return 0;
    return g_argv[idx];
}

static void parse_args(char *s) {
    g_argc = 0;
    for (int i = 0; i < (int)(sizeof(g_argv) / sizeof(g_argv[0])); ++i) g_argv[i] = 0;
    if (!s) return;

    char *p = s;
    while (*p && g_argc + 1 < (int)(sizeof(g_argv) / sizeof(g_argv[0]))) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        g_argv[g_argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    g_argv[g_argc] = 0;
}

void ntux_args_init(void) {
    char path[64];
    long tid = sys_get_tid();
    uint64_t len = 0;
    g_argc = 0;
    g_argv[0] = 0;
    g_buf[0] = '\0';
    if (tid <= 0) return;

    int n = snprintf(path, sizeof(path), "/tmp/args.%ld", tid);
    if (n <= 0 || (size_t)n >= sizeof(path)) return;
    /* Give the launcher a brief window to write args for this tid. */
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (sys_fs_read_file(path, 0, 0, &len) == 0) break;
        sys_wait_ticks(1);
    }
    if (sys_fs_read_file(path, 0, 0, &len) != 0) return;
    if (len == 0 || len >= sizeof(g_buf)) return;
    if (sys_fs_read_file(path, g_buf, sizeof(g_buf), &len) != 0) return;
    if (len >= sizeof(g_buf)) len = sizeof(g_buf) - 1u;
    g_buf[len] = '\0';
    (void)sys_fs_remove(path);
    parse_args(g_buf);
}

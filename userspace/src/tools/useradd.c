#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define FILE_BUF 8192
#define LINE_BUF 512

static uint64_t fnv1a64(const char *a, const char *b) {
    uint64_t h = 1469598103934665603ull;
    if (a) for (size_t i = 0; a[i]; ++i) { h ^= (uint8_t)a[i]; h *= 1099511628211ull; }
    h ^= (uint8_t)':'; h *= 1099511628211ull;
    if (b) for (size_t i = 0; b[i]; ++i) { h ^= (uint8_t)b[i]; h *= 1099511628211ull; }
    return h;
}

static void hash_hex(uint64_t h, char out[17]) {
    static const char *d = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) { out[15 - i] = d[(int)(h & 0xFu)]; h >>= 4u; }
    out[16] = '\0';
}

static int read_file(const char *path, char *buf, uint64_t cap, uint64_t *len) {
    long r = sys_fs_read_file(path, buf, cap - 1, len);
    if (r == 0) { buf[*len] = '\0'; return 0; }
    buf[0] = '\0'; *len = 0; return -1;
}

static int write_file(const char *path, const char *buf, uint64_t len) {
    if (sys_fs_exists(path) > 0) return (int)sys_fs_write_file(path, buf, len);
    char parent[256], name[64];
    const char *s = strrchr(path, '/');
    if (!s) return -1;
    size_t plen = (size_t)(s - path);
    if (plen >= sizeof(parent)) return -1;
    memcpy(parent, path, plen); parent[plen] = '\0';
    strncpy(name, s + 1, sizeof(name) - 1); name[sizeof(name) - 1] = '\0';
    return (int)sys_fs_create_file(parent, name, buf, len);
}

static int exists_in(const char *buf, const char *name) {
    const char *p = buf;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        size_t llen = (size_t)(p - line);
        if (llen > 0 && *p == '\n') p++;
        if (llen == 0) continue;
        const char *colon = (const char*)memchr(line, ':', llen);
        if (!colon) continue;
        size_t nlen = (size_t)(colon - line);
        if (strlen(name) == nlen && memcmp(line, name, nlen) == 0) return 1;
    }
    return 0;
}

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();

    if (sys_get_uid() != 0) { sys_write("useradd: must be root\n", 22); sys_exit(1); }

    uint32_t uid = 1001, gid = 1001;
    const char *home = NULL, *shell = "/bin/sh", *username = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'u' && i + 1 < argc) uid = (uint32_t)atoi(argv[++i]);
            else if (argv[i][1] == 'g' && i + 1 < argc) gid = (uint32_t)atoi(argv[++i]);
            else if (argv[i][1] == 'd' && i + 1 < argc) home = argv[++i];
            else if (argv[i][1] == 's' && i + 1 < argc) shell = argv[++i];
            else { sys_write("useradd: unknown option\n", 24); sys_exit(1); }
        } else {
            username = argv[i];
        }
    }

    if (!username) { sys_write("useradd: missing username\n", 26); sys_exit(1); }

    char homebuf[256];
    if (!home) { snprintf(homebuf, sizeof(homebuf), "/home/%s", username); home = homebuf; }

    char passwd_buf[FILE_BUF], shadow_buf[FILE_BUF], group_buf[FILE_BUF];
    uint64_t pw_len = 0, sh_len = 0, gr_len = 0;

    read_file("/etc/passwd", passwd_buf, sizeof(passwd_buf), &pw_len);
    read_file("/etc/shadow", shadow_buf, sizeof(shadow_buf), &sh_len);
    read_file("/etc/group", group_buf, sizeof(group_buf), &gr_len);

    if (exists_in(passwd_buf, username)) { sys_write("useradd: user exists\n", 21); sys_exit(1); }

    uint64_t pw_off = pw_len, sh_off = sh_len, gr_off = gr_len;
    char line[LINE_BUF];
    int n;

    n = snprintf(line, sizeof(line), "%s:%u:%u:%s:%s\n", username, uid, gid, home, shell);
    if (pw_off + (uint64_t)n + 1 < sizeof(passwd_buf)) {
        memcpy(passwd_buf + pw_off, line, (size_t)n + 1);
        pw_off += (uint64_t)n;
    }

    char hex[17];
    hash_hex(fnv1a64(username, "1234"), hex);
    n = snprintf(line, sizeof(line), "%s:%s\n", username, hex);
    if (sh_off + (uint64_t)n + 1 < sizeof(shadow_buf)) {
        memcpy(shadow_buf + sh_off, line, (size_t)n + 1);
        sh_off += (uint64_t)n;
    }

    if (!exists_in(group_buf, username)) {
        n = snprintf(line, sizeof(line), "%s:%u:\n", username, gid);
        if (gr_off + (uint64_t)n + 1 < sizeof(group_buf)) {
            memcpy(group_buf + gr_off, line, (size_t)n + 1);
            gr_off += (uint64_t)n;
        }
    }

    write_file("/etc/passwd", passwd_buf, pw_off);
    write_file("/etc/shadow", shadow_buf, sh_off);
    write_file("/etc/group", group_buf, gr_off);

    sys_write("useradd: added user ", 19);
    sys_write(username, strlen(username));
    sys_write("\n", 1);
    sys_exit(0);
}

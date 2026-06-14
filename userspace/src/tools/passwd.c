#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define FILE_BUF 4096

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

static void read_pass(char *buf, int cap) {
    int pos = 0;
    for (;;) {
        long c = sys_getchar();
        if (c < 0) continue;
        if (c == '\n' || c == '\r') break;
        if (c == 127 || c == 8) { if (pos > 0) pos--; }
        else if (c >= 32 && c < 127 && pos < cap - 1) buf[pos++] = (char)c;
    }
    buf[pos] = '\0';
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

static int replace_shadow_line(char *buf, const char *name, const char *new_hash) {
    char *p = buf;
    char tmp[FILE_BUF];
    int pos = 0;
    int found = 0;

    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        int is_last = (*p == '\0');
        if (*p == '\n') *p++ = '\0';

        if (*line) {
            const char *colon = strchr(line, ':');
            if (colon && (size_t)(colon - line) == strlen(name) && memcmp(line, name, colon - line) == 0) {
                int n = snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, "%s:%s\n", name, new_hash);
                if (n > 0) pos += n;
                found = 1;
            } else {
                size_t llen = strlen(line);
                if (pos + (int)llen + 1 < (int)sizeof(tmp)) {
                    memcpy(tmp + pos, line, llen);
                    pos += (int)llen;
                    if (!is_last) tmp[pos++] = '\n';
                }
            }
        } else if (!is_last) {
            if (pos + 1 < (int)sizeof(tmp)) tmp[pos++] = '\n';
        }
    }
    tmp[pos] = '\0';
    memcpy(buf, tmp, (size_t)pos + 1);
    return found;
}

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();

    const char *username = NULL;
    uint32_t uid = sys_get_uid();

    if (argc >= 2) {
        username = argv[1];
    } else {
        char pwdbuf[FILE_BUF];
        uint64_t len = 0;
        if (read_file("/etc/passwd", pwdbuf, sizeof(pwdbuf), &len) == 0) {
            char *p = pwdbuf;
            while (*p) {
                char *line = p;
                while (*p && *p != '\n') p++;
                if (*p == '\n') *p++ = '\0';
                if (!*line) continue;
                char *c1 = strchr(line, ':');
                if (!c1) continue;
                *c1++ = '\0';
                uint32_t u = (uint32_t)atoi(c1);
                if (u == uid) { username = line; break; }
            }
        }
        if (!username) { sys_write("passwd: cannot determine user\n", 31); sys_exit(1); }
    }

    if (uid != 0 && sys_auth_user(username, "") == 0) {
        sys_write("passwd: old password: ", 22);
        char old[64];
        read_pass(old, sizeof(old));
        sys_write("\n", 1);
        if (sys_auth_user(username, old) != 0) {
            sys_write("passwd: authentication failed\n", 31);
            sys_exit(1);
        }
    }

    sys_write("passwd: new password: ", 22);
    char new1[64];
    read_pass(new1, sizeof(new1));
    sys_write("\n", 1);

    sys_write("passwd: confirm new password: ", 31);
    char new2[64];
    read_pass(new2, sizeof(new2));
    sys_write("\n", 1);

    if (strcmp(new1, new2) != 0) {
        sys_write("passwd: passwords do not match\n", 32);
        sys_exit(1);
    }

    char shadow_buf[FILE_BUF];
    uint64_t sh_len = 0;
    if (read_file("/etc/shadow", shadow_buf, sizeof(shadow_buf), &sh_len) != 0) {
        sys_write("passwd: cannot read /etc/shadow\n", 33);
        sys_exit(1);
    }

    char hex[17];
    hash_hex(fnv1a64(username, new1), hex);

    if (!replace_shadow_line(shadow_buf, username, hex)) {
        sys_write("passwd: user not found in /etc/shadow\n", 38);
        sys_exit(1);
    }

    write_file("/etc/shadow", shadow_buf, strlen(shadow_buf));
    sys_write("passwd: password updated\n", 25);
    sys_exit(0);
}

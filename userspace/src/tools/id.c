#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define FILE_BUF 4096

static int read_file(const char *path, char *buf, uint64_t cap, uint64_t *len) {
    long r = sys_fs_read_file(path, buf, cap - 1, len);
    if (r == 0) { buf[*len] = '\0'; return 0; }
    buf[0] = '\0'; *len = 0; return -1;
}

static void print_u32(uint32_t v) {
    char buf[16];
    int p = 0;
    if (v == 0) { sys_write("0", 1); return; }
    while (v > 0 && p < 16) { buf[p++] = (char)('0' + v % 10u); v /= 10u; }
    while (p > 0) sys_write((const char*)(buf + --p), 1);
}

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();
    const char *username = NULL;

    if (argc >= 2) username = argv[1];

    uint32_t uid = 0, gid = 0;
    char name[64] = "";

    if (username) {
        char buf[FILE_BUF];
        uint64_t len = 0;
        if (read_file("/etc/passwd", buf, sizeof(buf), &len) != 0) {
            sys_write("id: cannot read /etc/passwd\n", 28); sys_exit(1);
        }
        char *p = buf;
        int found = 0;
        while (*p && !found) {
            char *line = p;
            while (*p && *p != '\n') p++;
            if (*p == '\n') *p++ = '\0';
            if (!*line) continue;
            char *c1 = strchr(line, ':');
            if (!c1 || (size_t)(c1 - line) != strlen(username) || memcmp(line, username, c1 - line) != 0) continue;
            *c1++ = '\0';
            strncpy(name, line, sizeof(name) - 1);
            uid = (uint32_t)atoi(c1);
            char *c2 = strchr(c1, ':');
            if (c2) gid = (uint32_t)atoi(c2 + 1);
            found = 1;
        }
        if (!found) { sys_write("id: unknown user\n", 17); sys_exit(1); }
    } else {
        uid = sys_get_uid();
        gid = sys_getgid();
        char buf[FILE_BUF];
        uint64_t len = 0;
        if (read_file("/etc/passwd", buf, sizeof(buf), &len) == 0) {
            char *p = buf;
            while (*p) {
                char *line = p;
                while (*p && *p != '\n') p++;
                if (*p == '\n') *p++ = '\0';
                if (!*line) continue;
                char *c1 = strchr(line, ':');
                if (!c1) continue;
                *c1++ = '\0';
                if ((uint32_t)atoi(c1) == uid) {
                    strncpy(name, line, sizeof(name) - 1);
                    break;
                }
            }
        }
        if (!name[0]) { sys_write("id: cannot find username\n", 25); sys_exit(1); }
    }

    sys_write("uid=", 4); print_u32(uid);
    sys_write("(", 1); sys_write(name, strlen(name));
    sys_write(") ", 2);
    sys_write("gid=", 4); print_u32(gid);
    sys_write("(", 1); sys_write(name, strlen(name));
    sys_write(")", 1);

    char group_buf[FILE_BUF];
    uint64_t gr_len = 0;
    if (read_file("/etc/group", group_buf, sizeof(group_buf), &gr_len) == 0) {
        sys_write(" groups=", 8);
        int first = 1;
        char *p = group_buf;
        while (*p) {
            char *line = p;
            while (*p && *p != '\n') p++;
            if (*p == '\n') *p++ = '\0';
            if (!*line) continue;
            char *c1 = strchr(line, ':');
            if (!c1) continue;
            *c1++ = '\0';
            char *c2 = strchr(c1, ':');
            if (!c2) continue;
            *c2++ = '\0';
            uint32_t gr_gid = (uint32_t)atoi(c1);
            char *users = c2;
            int found = 0;
            if (gr_gid == gid) found = 1;
            else {
                char *u = users;
                while (*u) {
                    while (*u == ',') u++;
                    if (!*u) break;
                    char *end = u;
                    while (*end && *end != ',') end++;
                    size_t ulen = (size_t)(end - u);
                    if (ulen == strlen(name) && memcmp(u, name, ulen) == 0) { found = 1; break; }
                    u = end;
                }
            }
            if (found) {
                if (!first) sys_write(",", 1);
                first = 0;
                print_u32(gr_gid);
                sys_write("(", 1);
                sys_write(line, strlen(line));
                sys_write(")", 1);
            }
        }
    }

    sys_write("\n", 1);
    sys_exit(0);
}

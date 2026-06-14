#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define FILE_BUF 4096

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();

    const char *username = NULL;
    if (argc >= 2) username = argv[1];

    if (!username) {
        uint32_t uid = sys_get_uid();
        char buf[FILE_BUF];
        uint64_t len = 0;
        if (sys_fs_read_file("/etc/passwd", buf, sizeof(buf) - 1, &len) != 0) {
            sys_write("groups: cannot read /etc/passwd\n", 32);
            sys_exit(1);
        }
        buf[len] = '\0';
        char *p = buf;
        while (*p) {
            char *line = p;
            while (*p && *p != '\n') p++;
            if (*p == '\n') *p++ = '\0';
            if (!*line) continue;
            char *c1 = strchr(line, ':');
            if (!c1) continue;
            *c1++ = '\0';
            if ((uint32_t)atoi(c1) == uid) { username = line; break; }
        }
        if (!username) { sys_write("groups: unknown user\n", 21); sys_exit(1); }
    }

    char buf[FILE_BUF];
    uint64_t len = 0;
    if (sys_fs_read_file("/etc/group", buf, sizeof(buf) - 1, &len) != 0) {
        sys_write("groups: cannot read /etc/group\n", 31);
        sys_exit(1);
    }
    buf[len] = '\0';

    sys_write(username, strlen(username));
    sys_write(" : ", 3);

    int first = 1;
    char *p = buf;
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
        char *users = c2;
        int in_group = 0;
        if (strlen(users) == 0 && strcmp(line, username) == 0) in_group = 1;
        else {
            char *u = users;
            while (*u) {
                while (*u == ',') u++;
                if (!*u) break;
                char *end = u;
                while (*end && *end != ',') end++;
                size_t ulen = (size_t)(end - u);
                if (ulen == strlen(username) && memcmp(u, username, ulen) == 0) { in_group = 1; break; }
                u = end;
            }
        }
        if (in_group) {
            if (!first) sys_write(" ", 1);
            first = 0;
            sys_write(line, strlen(line));
        }
    }
    sys_write("\n", 1);
    sys_exit(0);
}

#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define FILE_BUF 4096

void ntux_user_entry(void) {
    (void)ntux_argc();
    (void)ntux_argv();

    uint32_t uid = sys_get_uid();
    char buf[FILE_BUF];
    uint64_t len = 0;

    if (sys_fs_read_file("/etc/passwd", buf, sizeof(buf) - 1, &len) != 0) {
        sys_write("whoami: cannot read /etc/passwd\n", 32);
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
        uint32_t u = (uint32_t)atoi(c1);
        if (u == uid) {
            sys_write(line, strlen(line));
            sys_write("\n", 1);
            sys_exit(0);
        }
    }

    sys_write("whoami: cannot determine username\n", 35);
    sys_exit(1);
}

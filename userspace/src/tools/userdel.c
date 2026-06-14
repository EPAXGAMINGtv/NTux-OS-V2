#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define FILE_BUF 8192

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

static int remove_line(char *buf, const char *name) {
    char *p = buf;
    char *out = buf;
    int found = 0;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        int is_last = (*p == '\0');
        if (*p == '\n') *p++ = '\0';
        if (*line == '\0') continue;
        const char *colon = strchr(line, ':');
        if (colon && (size_t)(colon - line) == strlen(name) && memcmp(line, name, colon - line) == 0) {
            found = 1;
            continue;
        }
        size_t llen = strlen(line);
        memcpy(out, line, llen);
        out += llen;
        if (!is_last) *out++ = '\n';
    }
    *out = '\0';
    return found;
}

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();

    if (sys_get_uid() != 0) { sys_write("userdel: must be root\n", 22); sys_exit(1); }
    if (argc < 2) { sys_write("userdel: missing username\n", 26); sys_exit(1); }

    const char *username = argv[1];

    char passwd_buf[FILE_BUF], shadow_buf[FILE_BUF];
    uint64_t pw_len = 0, sh_len = 0;

    if (read_file("/etc/passwd", passwd_buf, sizeof(passwd_buf), &pw_len) != 0) {
        sys_write("userdel: cannot read /etc/passwd\n", 33); sys_exit(1);
    }
    read_file("/etc/shadow", shadow_buf, sizeof(shadow_buf), &sh_len);

    if (!remove_line(passwd_buf, username)) {
        sys_write("userdel: user not found\n", 24); sys_exit(1);
    }
    remove_line(shadow_buf, username);

    write_file("/etc/passwd", passwd_buf, strlen(passwd_buf));
    write_file("/etc/shadow", shadow_buf, strlen(shadow_buf));

    sys_write("userdel: removed user ", 22);
    sys_write(username, strlen(username));
    sys_write("\n", 1);
    sys_exit(0);
}

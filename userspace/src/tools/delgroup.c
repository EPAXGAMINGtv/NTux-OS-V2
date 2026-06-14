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

    if (sys_get_uid() != 0) { sys_write("delgroup: must be root\n", 23); sys_exit(1); }
    if (argc < 2) { sys_write("delgroup: missing group name\n", 29); sys_exit(1); }

    const char *groupname = argv[1];

    char buf[FILE_BUF];
    uint64_t len = 0;
    if (read_file("/etc/group", buf, sizeof(buf), &len) != 0) {
        sys_write("delgroup: cannot read /etc/group\n", 33);
        sys_exit(1);
    }

    if (!remove_line(buf, groupname)) {
        sys_write("delgroup: group not found\n", 26);
        sys_exit(1);
    }

    write_file("/etc/group", buf, strlen(buf));
    sys_write("delgroup: removed group ", 24);
    sys_write(groupname, strlen(groupname));
    sys_write("\n", 1);
    sys_exit(0);
}

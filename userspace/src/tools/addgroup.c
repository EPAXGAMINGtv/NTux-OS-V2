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

static int count_lines(const char *buf) {
    int n = 0;
    for (const char *p = buf; *p; p++) if (*p == '\n') n++;
    return n;
}

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();

    if (sys_get_uid() != 0) { sys_write("addgroup: must be root\n", 23); sys_exit(1); }

    uint32_t gid = 0;
    const char *groupname = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'g' && i + 1 < argc) gid = (uint32_t)atoi(argv[++i]);
            else { sys_write("addgroup: unknown option\n", 25); sys_exit(1); }
        } else {
            groupname = argv[i];
        }
    }

    if (!groupname) { sys_write("addgroup: missing group name\n", 29); sys_exit(1); }

    char buf[FILE_BUF];
    uint64_t len = 0;
    read_file("/etc/group", buf, sizeof(buf), &len);

    if (exists_in(buf, groupname)) { sys_write("addgroup: group exists\n", 23); sys_exit(1); }

    if (gid == 0) gid = 1000 + (uint32_t)count_lines(buf) + 1;

    uint64_t off = len;
    char line[256];
    int n = snprintf(line, sizeof(line), "%s:%u:\n", groupname, gid);
    if (off + (uint64_t)n + 1 < sizeof(buf)) {
        memcpy(buf + off, line, (size_t)n + 1);
        off += (uint64_t)n;
    }

    write_file("/etc/group", buf, off);
    sys_write("addgroup: added group ", 22);
    sys_write(groupname, strlen(groupname));
    sys_write("\n", 1);
    sys_exit(0);
}

#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>

#define FILE_BUF 4096

static int name_to_uid(const char *name, uint32_t *uid) {
    char buf[FILE_BUF];
    uint64_t len = 0;
    if (sys_fs_read_file("/etc/passwd", buf, sizeof(buf) - 1, &len) != 0) return -1;
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
        if (strcmp(line, name) == 0) { *uid = (uint32_t)atoi(c1); return 0; }
    }
    return -1;
}

static int name_to_gid(const char *name, uint32_t *gid) {
    char buf[FILE_BUF];
    uint64_t len = 0;
    if (sys_fs_read_file("/etc/group", buf, sizeof(buf) - 1, &len) != 0) return -1;
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
        if (strcmp(line, name) == 0) {
            char *c2 = strchr(c1, ':');
            if (!c2) continue;
            *c2++ = '\0';
            *gid = (uint32_t)atoi(c1);
            return 0;
        }
    }
    return -1;
}

void ntux_user_entry(void) {
    int argc = ntux_argc();
    char **argv = ntux_argv();

    if (argc < 3) {
        sys_write("chown: usage: chown owner:group path\n", 36);
        sys_exit(1);
    }

    const char *spec = argv[1];
    const char *path = argv[2];

    uint32_t owner = 0, group = 0;

    const char *colon = strchr(spec, ':');
    if (colon) {
        char owner_buf[64];
        size_t olen = (size_t)(colon - spec);
        if (olen > 0) {
            if (olen >= sizeof(owner_buf)) { sys_write("chown: owner too long\n", 22); sys_exit(1); }
            memcpy(owner_buf, spec, olen);
            owner_buf[olen] = '\0';
            if (name_to_uid(owner_buf, &owner) != 0) {
                owner = (uint32_t)atoi(owner_buf);
            }
        }
        const char *gname = colon + 1;
        if (*gname) {
            if (name_to_gid(gname, &group) != 0) {
                group = (uint32_t)atoi(gname);
            }
        }
    } else {
        if (name_to_uid(spec, &owner) != 0) {
            owner = (uint32_t)atoi(spec);
        }
    }

    long r = sys_chown(path, owner, group);
    if (r != 0) {
        sys_write("chown: failed\n", 15);
        sys_exit(1);
    }
    sys_exit(0);
}

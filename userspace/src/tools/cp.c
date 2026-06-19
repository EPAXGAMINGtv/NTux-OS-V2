#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <args.h>

static int split_parent_name(const char* full, char* parent, char* name, size_t cap) {
    if (!full || full[0] != '/' || !parent || !name || cap < 4) return -1;
    const char* slash = 0;
    for (const char* p = full; *p; ++p) if (*p == '/') slash = p;
    if (!slash || !slash[1]) return -1;
    size_t plen = (size_t)(slash - full);
    if (plen == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        if (plen >= cap) return -1;
        memcpy(parent, full, plen);
        parent[plen] = '\0';
    }
    strncpy(name, slash + 1, cap - 1);
    name[cap - 1] = '\0';
    return 0;
}

static int is_dir(const char* path) {
    uint64_t n = 0;
    return sys_fs_list_dir(path, 0, 0, &n) == 0;
}

static int join_path(const char* a, const char* b, char* out, size_t cap) {
    if (!a || !b || !out || cap == 0) return -1;
    int n = snprintf(out, cap, strcmp(a, "/") == 0 ? "/%s" : "%s/%s", a, b);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

static int copy_file(const char* src, const char* dst) {
    if (sys_fs_copy_fast(src, dst) == 0) return 0;
    uint64_t len = 0;
    if (sys_fs_read_file(src, 0, 0, &len) != 0) return -1;
    if (len > 64u * 1024u * 1024u) return -1;
    char* buf = len ? (char*)malloc((size_t)len) : 0;
    if (len && !buf) return -1;
    if (len && sys_fs_read_file(src, buf, len, &len) != 0) {
        free(buf);
        return -1;
    }
    long rc = sys_fs_write_file(dst, buf, len);
    if (rc != 0) {
        char parent[256], name[256];
        if (split_parent_name(dst, parent, name, sizeof(parent)) == 0) {
            rc = sys_fs_create_file(parent, name, buf ? buf : "", len);
        }
    }
    free(buf);
    return rc == 0 ? 0 : -1;
}

static int copy_tree(const char* src, const char* dst) {
    if (!is_dir(src)) return copy_file(src, dst);
    char parent[256], name[256];
    if (split_parent_name(dst, parent, name, sizeof(parent)) == 0) {
        (void)sys_fs_mkdir(parent, name);
    }
    ntux_dirent_t ents[128];
    uint64_t count = 0;
    if (sys_fs_list_dir(src, ents, 128, &count) != 0) return -1;
    if (count > 128) count = 128;
    for (uint64_t i = 0; i < count; ++i) {
        if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
        char s[256], d[256];
        if (join_path(src, ents[i].name, s, sizeof(s)) != 0) return -1;
        if (join_path(dst, ents[i].name, d, sizeof(d)) != 0) return -1;
        if (copy_tree(s, d) != 0) return -1;
    }
    return 0;
}

void ntux_user_entry(void) {
    const char* src = ntux_arg(0);
    const char* dst = ntux_arg(1);
    if (!src || !dst) {
        puts("usage: cp <src> <dst>");
        sys_exit(2);
    }
    sys_exit(copy_tree(src, dst) == 0 ? 0 : 1);
}

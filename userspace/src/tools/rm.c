#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <args.h>

static int is_dir(const char* path) {
    uint64_t n = 0;
    return sys_fs_list_dir(path, 0, 0, &n) == 0;
}

static int join_path(const char* a, const char* b, char* out, size_t cap) {
    int n = snprintf(out, cap, strcmp(a, "/") == 0 ? "/%s" : "%s/%s", a, b);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

static int remove_tree(const char* path) {
    if (!is_dir(path)) return sys_fs_remove(path) == 0 ? 0 : -1;
    ntux_dirent_t ents[128];
    uint64_t count = 0;
    if (sys_fs_list_dir(path, ents, 128, &count) != 0) return -1;
    if (count > 128) count = 128;
    for (uint64_t i = 0; i < count; ++i) {
        if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
        char child[256];
        if (join_path(path, ents[i].name, child, sizeof(child)) != 0) return -1;
        if (remove_tree(child) != 0) return -1;
    }
    return sys_fs_remove(path) == 0 ? 0 : -1;
}

void ntux_user_entry(void) {
    const char* path = ntux_arg(0);
    if (!path) {
        puts("usage: rm <path>");
        sys_exit(2);
    }
    sys_exit(remove_tree(path) == 0 ? 0 : 1);
}

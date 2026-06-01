#include <dirent.h>

#include <errno.h>
#include <ntux_path.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

typedef struct {
    ntux_dirent_t *entries;
    uint64_t count;
    uint64_t index;
} ntux_dir_t;

DIR *opendir(const char *path) {
    char abspath[256];
    if (ntux_resolve_path(path, abspath, sizeof(abspath)) != 0) {
        errno = EINVAL;
        return 0;
    }
    uint64_t count = 0;
    if (sys_fs_list_dir(abspath, 0, 0, &count) != 0) {
        errno = ENOENT;
        return 0;
    }
    ntux_dirent_t *buf = 0;
    if (count > 0) {
        buf = (ntux_dirent_t *)malloc((size_t)count * sizeof(ntux_dirent_t));
        if (!buf) {
            errno = ENOMEM;
            return 0;
        }
        if (sys_fs_list_dir(abspath, buf, count, &count) != 0) {
            free(buf);
            errno = EIO;
            return 0;
        }
    }
    ntux_dir_t *dir = (ntux_dir_t *)malloc(sizeof(ntux_dir_t));
    if (!dir) {
        free(buf);
        errno = ENOMEM;
        return 0;
    }
    dir->entries = buf;
    dir->count = count;
    dir->index = 0;
    return (DIR *)dir;
}

struct dirent *readdir(DIR *dirp) {
    static struct dirent out;
    ntux_dir_t *dir = (ntux_dir_t *)dirp;
    if (!dir || dir->index >= dir->count) return 0;
    ntux_dirent_t *ent = &dir->entries[dir->index++];
    size_t n = strnlen(ent->name, sizeof(ent->name));
    if (n >= sizeof(out.d_name)) n = sizeof(out.d_name) - 1u;
    memcpy(out.d_name, ent->name, n);
    out.d_name[n] = '\0';
    out.d_type = ent->is_dir ? DT_DIR : DT_REG;
    return &out;
}

int closedir(DIR *dirp) {
    ntux_dir_t *dir = (ntux_dir_t *)dirp;
    if (!dir) return -1;
    if (dir->entries) free(dir->entries);
    free(dir);
    return 0;
}

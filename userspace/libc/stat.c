#include <sys/stat.h>

#include <errno.h>
#include <ntux_path.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

static int split_parent(const char *path, char *parent, size_t parent_cap, const char **name_out) {
    if (!path || path[0] != '/') return -1;
    const char *slash = path + strlen(path);
    while (slash > path && slash[-1] != '/') slash--;
    if (slash <= path) return -1;
    size_t plen = (size_t)(slash - path - 1u);
    if (plen == 0) {
        if (parent_cap < 2) return -1;
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        if (plen + 1 > parent_cap) return -1;
        memcpy(parent, path, plen);
        parent[plen] = '\0';
    }
    if (name_out) *name_out = slash;
    return 0;
}

static int stat_path(const char *path, struct stat *st) {
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    char abspath[256];
    if (ntux_resolve_path(path, abspath, sizeof(abspath)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (strcmp(abspath, "/") == 0) {
        st->st_mode = S_IFDIR | 0777;
        st->st_size = 0;
        st->st_mtime = 0;
        st->st_atime = 0;
        st->st_ctime = 0;
        st->st_ino = 0;
        st->st_dev = 0;
        st->st_uid = 0;
        st->st_gid = 0;
        st->st_nlink = 1;
        return 0;
    }
    char parent[256];
    const char *name = 0;
    if (split_parent(abspath, parent, sizeof(parent), &name) != 0) {
        errno = EINVAL;
        return -1;
    }
    uint64_t count = 0;
    if (sys_fs_list_dir(parent, 0, 0, &count) != 0) {
        errno = ENOENT;
        return -1;
    }
    if (count == 0) {
        errno = ENOENT;
        return -1;
    }
    ntux_dirent_t *buf = (ntux_dirent_t *)malloc((size_t)count * sizeof(ntux_dirent_t));
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }
    if (sys_fs_list_dir(parent, buf, count, &count) != 0) {
        free(buf);
        errno = EIO;
        return -1;
    }
    int found = 0;
    for (uint64_t i = 0; i < count; ++i) {
        if (strcmp(buf[i].name, name) == 0) {
            st->st_mode = (buf[i].is_dir ? S_IFDIR : S_IFREG) | 0777;
            st->st_size = (off_t)buf[i].size;
            st->st_mtime = 0;
            st->st_atime = 0;
            st->st_ctime = 0;
            st->st_ino = 0;
            st->st_dev = 0;
            st->st_uid = 0;
            st->st_gid = 0;
            st->st_nlink = 1;
            found = 1;
            break;
        }
    }
    free(buf);
    if (!found) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int stat(const char *path, struct stat *st) {
    return stat_path(path, st);
}

int lstat(const char *path, struct stat *st) {
    return stat_path(path, st);
}

int fstat(int fd, struct stat *st) {
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    long cur = sys_lseek(fd, 0, SEEK_CUR);
    long end = sys_lseek(fd, 0, SEEK_END);
    if (end < 0) return -1;
    (void)sys_lseek(fd, cur, SEEK_SET);
    st->st_mode = S_IFREG | 0666;
    st->st_size = (off_t)end;
    st->st_mtime = 0;
    st->st_atime = 0;
    st->st_ctime = 0;
    st->st_ino = 0;
    st->st_dev = 0;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_nlink = 1;
    return 0;
}

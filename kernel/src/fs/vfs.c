#include <fs/vfs.h>

#include <lib/string.h>
#include <drivers/framebuffer/kprint.h>

static vfs_mount_t g_mounts[VFS_MAX_MOUNTS];

static size_t vfs_strnlen(const char* s, size_t cap) {
    size_t i = 0;
    while (i < cap && s[i] != '\0') {
        ++i;
    }
    return i;
}

static void vfs_copy_string(char* dst, size_t dst_cap, const char* src) {
    if (dst_cap == 0) {
        return;
    }
    size_t i = 0;
    while (i + 1 < dst_cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static int vfs_path_is_prefix(const char* prefix, const char* path) {
    size_t plen = strlen(prefix);
    if (plen == 0) {
        return 0;
    }

    if (strncmp(prefix, path, plen) != 0) {
        return 0;
    }

    if (plen == 1 && prefix[0] == '/') {
        return 1;
    }

    if (path[plen] == '\0' || path[plen] == '/') {
        return 1;
    }

    return 0;
}

static int vfs_extract_mount_child(const char* parent, const char* mount_point, char out[VFS_MAX_NAME]) {
    if (parent == NULL || mount_point == NULL || parent[0] != '/' || mount_point[0] != '/') {
        return 0;
    }

    if (strcmp(mount_point, "/") == 0) {
        return 0;
    }

    const char* rel = NULL;
    if (strcmp(parent, "/") == 0) {
        rel = mount_point + 1;
    } else {
        size_t plen = strlen(parent);
        if (strncmp(parent, mount_point, plen) != 0 || mount_point[plen] != '/') {
            return 0;
        }
        rel = mount_point + plen + 1;
    }

    if (rel == NULL || rel[0] == '\0') {
        return 0;
    }

    size_t i = 0;
    while (rel[i] != '\0' && rel[i] != '/' && i + 1 < VFS_MAX_NAME) {
        out[i] = rel[i];
        ++i;
    }
    out[i] = '\0';

    if (i == 0) {
        return 0;
    }

    if (rel[i] == '/') {
        return 0;
    }

    return 1;
}

static int vfs_entry_name_exists(const vfs_dirent_t* out, size_t count, size_t max_entries, const char* name) {
    if (out == NULL || max_entries == 0) {
        return 0;
    }

    size_t vis = count < max_entries ? count : max_entries;
    for (size_t i = 0; i < vis; ++i) {
        if (strcmp(out[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static const vfs_mount_t* vfs_find_mount(const char* path, const char** out_relative) {
    const vfs_mount_t* best = NULL;
    size_t best_len = 0;

    for (size_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!g_mounts[i].used) {
            continue;
        }
        size_t mlen = strlen(g_mounts[i].mount_point);
        if (!vfs_path_is_prefix(g_mounts[i].mount_point, path)) {
            continue;
        }
        if (mlen >= best_len) {
            best = &g_mounts[i];
            best_len = mlen;
        }
    }

    if (best == NULL) {
        return NULL;
    }

    if (out_relative != NULL) {
        const char* rel = path + best_len;
        if (best_len == 1 && best->mount_point[0] == '/') {
            rel = path;
        } else if (*rel == '\0') {
            rel = "/";
        }
        *out_relative = rel;
    }

    return best;
}

void vfs_init(void) {
    memset(g_mounts, 0, sizeof(g_mounts));
}

int vfs_mount(const char* mount_point, const vfs_backend_ops_t* ops, void* ctx) {
    if (mount_point == NULL || ops == NULL || mount_point[0] != '/') {
        return -1;
    }

    size_t len = vfs_strnlen(mount_point, VFS_MAX_PATH);
    if (len == 0 || len >= VFS_MAX_PATH) {
        return -2;
    }

    for (size_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!g_mounts[i].used) {
            g_mounts[i].used = true;
            vfs_copy_string(g_mounts[i].mount_point, sizeof(g_mounts[i].mount_point), mount_point);
            g_mounts[i].ops = ops;
            g_mounts[i].ctx = ctx;
            return 0;
        }
    }

    return -3;
}

int vfs_mkdir(const char* path) {
    const char* rel;
    const vfs_mount_t* m = vfs_find_mount(path, &rel);
    if (m == NULL || m->ops->mkdir == NULL) {
        return -1;
    }
    return m->ops->mkdir(m->ctx, rel);
}

int vfs_create_file(const char* path, const void* data, size_t len) {
    const char* rel;
    const vfs_mount_t* m = vfs_find_mount(path, &rel);
    if (m == NULL || m->ops->create_file == NULL) {
        return -1;
    }
    return m->ops->create_file(m->ctx, rel, data, len);
}

int vfs_write_file(const char* path, const void* data, size_t len) {
    const char* rel;
    const vfs_mount_t* m = vfs_find_mount(path, &rel);
    if (m == NULL || m->ops->write_file == NULL) {
        return -1;
    }
    return m->ops->write_file(m->ctx, rel, data, len);
}

int vfs_read_file(const char* path, void* out, size_t out_cap, size_t* out_len) {
    const char* rel;
    const vfs_mount_t* m = vfs_find_mount(path, &rel);
    if (m == NULL || m->ops->read_file == NULL) {
        return -1;
    }
    return m->ops->read_file(m->ctx, rel, out, out_cap, out_len);
}

int vfs_list_dir(const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count) {
    const char* rel;
    const vfs_mount_t* m = vfs_find_mount(path, &rel);
    if (m == NULL || m->ops->list_dir == NULL) {
        return -1;
    }

    size_t total = 0;
    int rc = m->ops->list_dir(m->ctx, rel, out, max_entries, &total);
    if (rc != 0) {
        if (out_count) *out_count = 0;
        return rc;
    }

    char child[VFS_MAX_NAME];
    for (size_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!g_mounts[i].used) {
            continue;
        }

        if (!vfs_extract_mount_child(path, g_mounts[i].mount_point, child)) {
            continue;
        }

        if (vfs_entry_name_exists(out, total, max_entries, child)) {
            continue;
        }

        if (out != NULL && total < max_entries) {
            memset(&out[total], 0, sizeof(vfs_dirent_t));
            vfs_copy_string(out[total].name, sizeof(out[total].name), child);
            out[total].is_dir = true;
            out[total].size = 0;
        }
        total++;
    }

    if (out_count) {
        *out_count = total;
    }
    return 0;
}

int vfs_exists(const char* path) {
    const char* rel;
    const vfs_mount_t* m = vfs_find_mount(path, &rel);
    if (m == NULL || m->ops->exists == NULL) {
        return 0;
    }
    return m->ops->exists(m->ctx, rel);
}

int vfs_remove(const char* path) {
    const char* rel;
    const vfs_mount_t* m = vfs_find_mount(path, &rel);
    if (m == NULL || m->ops->remove == NULL) {
        return -1;
    }
    return m->ops->remove(m->ctx, rel);
}

int vfs_rename(const char* old_path, const char* new_path) {
    const char* old_rel;
    const char* new_rel;
    const vfs_mount_t* mo = vfs_find_mount(old_path, &old_rel);
    const vfs_mount_t* mn = vfs_find_mount(new_path, &new_rel);
    if (mo == NULL || mn == NULL || mo != mn || mo->ops->rename == NULL) {
        return -1;
    }
    return mo->ops->rename(mo->ctx, old_rel, new_rel);
}

int vfs_get_mount(const char* path, const vfs_backend_ops_t** out_ops, void** out_ctx, const char** out_relative) {
    if (!path || path[0] != '/') return -1;
    const char* rel = NULL;
    const vfs_mount_t* m = vfs_find_mount(path, &rel);
    if (m == NULL) return -1;
    if (out_ops) *out_ops = m->ops;
    if (out_ctx) *out_ctx = m->ctx;
    if (out_relative) *out_relative = rel;
    return 0;
}

void vfs_dump_mounts(void) {
    for (size_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!g_mounts[i].used) continue;
        kprintf("  mount[");
        kprint_int((int)i);
        kprintf("] \"");
        kprint(g_mounts[i].mount_point);
        kprintf("\"\n");
    }
}

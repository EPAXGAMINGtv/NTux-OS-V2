#include <fs/vfs.h>
#include <lib/string.h>

static int union_mkdir(void* ctx, const char* path, uint16_t mode) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->mkdir) return u->ops_a->mkdir(u->ctx_a, path, mode);
    return -1;
}

static int union_create_file(void* ctx, const char* path, uint16_t mode, const void* data, size_t len) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->create_file) return u->ops_a->create_file(u->ctx_a, path, mode, data, len);
    return -1;
}

static int union_write_file(void* ctx, const char* path, const void* data, size_t len) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->write_file) return u->ops_a->write_file(u->ctx_a, path, data, len);
    return -1;
}

static int union_read_file(void* ctx, const char* path, void* out, size_t out_cap, size_t* out_len) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->read_file && u->ops_a->read_file(u->ctx_a, path, out, out_cap, out_len) == 0)
        return 0;
    if (u->ops_b->read_file) return u->ops_b->read_file(u->ctx_b, path, out, out_cap, out_len);
    return -1;
}

static int union_list_dir(void* ctx, const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;

    vfs_dirent_t a_ents[64];
    size_t a_count = 0;
    int ra = -1;
    if (u->ops_a->list_dir)
        ra = u->ops_a->list_dir(u->ctx_a, path, a_ents, 64, &a_count);

    vfs_dirent_t b_ents[64];
    size_t b_count = 0;
    int rb = -1;
    if (u->ops_b->list_dir)
        rb = u->ops_b->list_dir(u->ctx_b, path, b_ents, 64, &b_count);

    if (ra != 0 && rb != 0) {
        if (out_count) *out_count = 0;
        return -1;
    }

    size_t total = 0;

    for (size_t i = 0; i < a_count && total < max_entries; ++i) {
        if (strcmp(a_ents[i].name, ".") == 0 || strcmp(a_ents[i].name, "..") == 0) continue;
        if (out) out[total] = a_ents[i];
        total++;
    }

    for (size_t i = 0; i < b_count && total < max_entries; ++i) {
        if (strcmp(b_ents[i].name, ".") == 0 || strcmp(b_ents[i].name, "..") == 0) continue;
        int dup = 0;
        for (size_t j = 0; j < a_count; ++j) {
            if (strcmp(b_ents[i].name, a_ents[j].name) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        if (out) out[total] = b_ents[i];
        total++;
    }

    if (out_count) *out_count = total;
    return 0;
}

static int union_exists(void* ctx, const char* path) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->exists) {
        int rc = u->ops_a->exists(u->ctx_a, path);
        if (rc > 0) return rc;
    }
    if (u->ops_b->exists) return u->ops_b->exists(u->ctx_b, path);
    return 0;
}

static int union_remove(void* ctx, const char* path) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->remove && u->ops_a->remove(u->ctx_a, path) == 0) return 0;
    if (u->ops_b->remove) return u->ops_b->remove(u->ctx_b, path);
    return -1;
}

static int union_rename(void* ctx, const char* old_path, const char* new_path) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->rename && u->ops_a->rename(u->ctx_a, old_path, new_path) == 0) return 0;
    if (u->ops_b->rename) return u->ops_b->rename(u->ctx_b, old_path, new_path);
    return -1;
}

static int union_getattr(void* ctx, const char* path, uint16_t* mode, uint32_t* uid, uint32_t* gid) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->getattr && u->ops_a->getattr(u->ctx_a, path, mode, uid, gid) == 0) return 0;
    if (u->ops_b->getattr) return u->ops_b->getattr(u->ctx_b, path, mode, uid, gid);
    return -1;
}

static int union_chmod(void* ctx, const char* path, uint16_t mode) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->chmod && u->ops_a->chmod(u->ctx_a, path, mode) == 0) return 0;
    if (u->ops_b->chmod) return u->ops_b->chmod(u->ctx_b, path, mode);
    return -1;
}

static int union_chown(void* ctx, const char* path, uint32_t uid, uint32_t gid) {
    vfs_union_ctx_t* u = (vfs_union_ctx_t*)ctx;
    if (u->ops_a->chown && u->ops_a->chown(u->ctx_a, path, uid, gid) == 0) return 0;
    if (u->ops_b->chown) return u->ops_b->chown(u->ctx_b, path, uid, gid);
    return -1;
}

static const vfs_backend_ops_t g_union_ops = {
    .mkdir       = union_mkdir,
    .create_file = union_create_file,
    .write_file  = union_write_file,
    .read_file   = union_read_file,
    .list_dir    = union_list_dir,
    .exists      = union_exists,
    .remove      = union_remove,
    .rename      = union_rename,
    .getattr     = union_getattr,
    .chmod       = union_chmod,
    .chown       = union_chown,
};

const vfs_backend_ops_t* vfs_union_backend_ops(void) {
    return &g_union_ops;
}

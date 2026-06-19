#ifndef VFS_H
#define VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_MOUNTS 16
#define VFS_MAX_PATH 256
#define VFS_MAX_NAME 64

typedef struct {
    char name[VFS_MAX_NAME];
    bool is_dir;
    size_t size;
} vfs_dirent_t;

typedef struct vfs_backend_ops {
    int (*mkdir)(void* ctx, const char* path, uint16_t mode);
    int (*create_file)(void* ctx, const char* path, uint16_t mode, const void* data, size_t len);
    int (*write_file)(void* ctx, const char* path, const void* data, size_t len);
    int (*read_file)(void* ctx, const char* path, void* out, size_t out_cap, size_t* out_len);
    int (*list_dir)(void* ctx, const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count);
    int (*exists)(void* ctx, const char* path);
    int (*remove)(void* ctx, const char* path);
    int (*rename)(void* ctx, const char* old_path, const char* new_path);
    int (*getattr)(void* ctx, const char* path, uint16_t* mode, uint32_t* uid, uint32_t* gid);
    int (*chmod)(void* ctx, const char* path, uint16_t mode);
    int (*chown)(void* ctx, const char* path, uint32_t uid, uint32_t gid);
} vfs_backend_ops_t;

typedef struct {
    bool used;
    char mount_point[VFS_MAX_PATH];
    const vfs_backend_ops_t* ops;
    void* ctx;
} vfs_mount_t;

typedef struct {
    const vfs_backend_ops_t* ops_a;
    void* ctx_a;
    const vfs_backend_ops_t* ops_b;
    void* ctx_b;
} vfs_union_ctx_t;

void vfs_init(void);
int vfs_mount(const char* mount_point, const vfs_backend_ops_t* ops, void* ctx);
int vfs_set_overlay(const char* mount_path);
int vfs_get_overlay(char* out, size_t cap);
const vfs_backend_ops_t* vfs_union_backend_ops(void);

int vfs_mkdir(const char* path, uint16_t mode);
int vfs_create_file(const char* path, uint16_t mode, const void* data, size_t len);
int vfs_write_file(const char* path, const void* data, size_t len);
int vfs_read_file(const char* path, void* out, size_t out_cap, size_t* out_len);
int vfs_list_dir(const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count);
int vfs_exists(const char* path);
int vfs_remove(const char* path);
int vfs_rename(const char* old_path, const char* new_path);
int vfs_get_mount(const char* path, const vfs_backend_ops_t** out_ops, void** out_ctx, const char** out_relative);
void vfs_dump_mounts(void);

int vfs_perm_check(const char* path, int write_access);
int vfs_getattr(const char* path, uint16_t* mode, uint32_t* uid, uint32_t* gid);
int vfs_chmod(const char* path, uint16_t mode);
int vfs_chown(const char* path, uint32_t uid, uint32_t gid);

#endif

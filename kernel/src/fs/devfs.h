#ifndef DEVFS_H
#define DEVFS_H

#include <stddef.h>
#include <stdint.h>

#include <fs/vfs.h>

typedef struct devfs_ops {
    int (*read)(void* ctx, void* out, size_t len, size_t* out_read);
    int (*write)(void* ctx, const void* in, size_t len, size_t* out_written);
    int (*ioctl)(void* ctx, uint64_t req, void* arg);
} devfs_ops_t;

void devfs_init(void);
int devfs_register(const char* name, const devfs_ops_t* ops, void* ctx);
int devfs_open(const char* name, const devfs_ops_t** out_ops, void** out_ctx);
int devfs_list(const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count);
int devfs_exists(const char* path);
const vfs_backend_ops_t* devfs_backend_ops(void);

#endif

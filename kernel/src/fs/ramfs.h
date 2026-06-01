#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>

#include <fs/vfs.h>

#define RAMFS_MAX_NODES 1024
#define RAMFS_MAX_CHILDREN 64
#define RAMFS_MAX_FILE_BYTES (1024 * 1024)

typedef enum {
    RAMFS_NODE_FILE = 0,
    RAMFS_NODE_DIR,
} ramfs_node_type_t;

typedef struct {
    int used;
    ramfs_node_type_t type;
    int parent;
    char name[VFS_MAX_NAME];
    int children[RAMFS_MAX_CHILDREN];
    size_t child_count;
    uint8_t* data;
    size_t size;
    size_t capacity;
} ramfs_node_t;

typedef struct {
    ramfs_node_t nodes[RAMFS_MAX_NODES];
    uint8_t file_pool[RAMFS_MAX_FILE_BYTES];
    size_t file_pool_used;
    int root_index;
} ramfs_t;

void ramfs_init(ramfs_t* fs);
const vfs_backend_ops_t* ramfs_backend_ops(void);
int ramfs_get_file_view(ramfs_t* fs, const char* path, const uint8_t** out_data, size_t* out_len);

#endif

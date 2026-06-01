#ifndef EXT2_FS_H
#define EXT2_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fs/vfs.h>

typedef struct {
    bool mounted;
    uint8_t drive_index;
    uint64_t partition_lba;

    uint32_t block_size;
    uint32_t inode_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t first_data_block;
    uint32_t group_count;

    uint32_t blocks_count;
    uint32_t inodes_count;
    uint32_t root_inode;
} ext2_fs_t;

int ext2_fs_mount(ext2_fs_t* fs, uint8_t drive_index, uint64_t partition_lba);
const vfs_backend_ops_t* ext2_fs_backend_ops(void);
int ext2_fs_create_file_from_reader(ext2_fs_t* fs, const char* path, size_t len,
                                    int (*reader)(void* user, size_t offset, void* out, size_t len),
                                    void* user);

#endif

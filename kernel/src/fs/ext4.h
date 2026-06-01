#ifndef EXT4_FS_H
#define EXT4_FS_H

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

    bool has_journal;
    uint32_t journal_inode;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
} ext4_fs_t;

/*
 * Return codes:
 *   0  -> mounted
 *  -1  -> I/O or internal error
 *  -2  -> not an EXT filesystem
 *  -3  -> EXT filesystem, but not identified as EXT4
 */
int ext4_fs_mount(ext4_fs_t* fs, uint8_t drive_index, uint64_t partition_lba);
const vfs_backend_ops_t* ext4_fs_backend_ops(void);
int ext4_fs_create_file_from_reader(ext4_fs_t* fs, const char* path, size_t len,
                                    int (*reader)(void* user, size_t offset, void* out, size_t len),
                                    void* user);

#endif

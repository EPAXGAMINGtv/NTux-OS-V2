#ifndef FAT_FS_H
#define FAT_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fs/vfs.h>

typedef enum {
    FAT_TYPE_12 = 12,
    FAT_TYPE_16 = 16,
    FAT_TYPE_32 = 32,
} fat_type_t;

typedef struct {
    bool mounted;
    uint8_t drive_index;
    uint64_t partition_lba;
    fat_type_t type;

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;

    uint32_t root_dir_sectors;
    uint32_t first_fat_sector;
    uint32_t first_root_dir_sector;
    uint32_t first_data_sector;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t next_free_cluster;
} fat_fs_t;

typedef int (*fat_fs_reader_fn)(void* user, size_t offset, void* out, size_t len);

int fat_fs_mount(fat_fs_t* fs, uint8_t drive_index, uint64_t partition_lba);
const vfs_backend_ops_t* fat_fs_backend_ops(void);
int fat_fs_create_file_from_reader(fat_fs_t* fs, const char* path, size_t len, fat_fs_reader_fn reader, void* user);
int fat_fs_read_file_range(fat_fs_t* fs, const char* path, size_t offset, void* out, size_t len,
                           size_t* out_read, size_t* out_file_len);

#endif

#ifndef FS_H
#define FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fs/vfs.h>

typedef vfs_dirent_t fs_dirent_t;

typedef struct {
    uint8_t present;
    uint8_t is_atapi;
    uint8_t _pad[6];
    uint64_t sectors;
    char model[41];
} fs_block_device_info_t;

typedef struct {
    uint8_t index;
    uint8_t type;
    uint8_t bootable;
    uint8_t _pad;
    uint64_t lba_start;
    uint64_t sectors;
} fs_partition_info_t;

void fs_init(void);
void fs_rescan_storage(void);
uint32_t fs_mount_generation(void);

int fs_mkdir(const char* path, const char* name);
int fs_create_file(const char* path, const char* name, const void* data, size_t len);
int fs_write_file(const char* path, const void* data, size_t len);
int fs_read_file(const char* path, void* out, size_t out_cap, size_t* out_len);
int fs_read_file_range(const char* path, size_t offset, void* out, size_t len, size_t* out_read, size_t* out_file_len);
int fs_list_dir(const char* path, fs_dirent_t* out, size_t max_entries, size_t* out_count);
bool fs_exists(const char* path);
int fs_remove(const char* path);
int fs_rename(const char* old_path, const char* new_name);
int fs_copy_file_fast(const char* src, const char* dst);
size_t fs_get_block_devices(fs_block_device_info_t* out, size_t max_entries);
size_t fs_list_partitions(uint8_t drive, fs_partition_info_t* out, size_t max_entries);
int fs_set_mbr_partition(uint8_t drive, uint8_t part_index, uint32_t lba_start, uint32_t sectors, uint8_t type, uint8_t bootable);
int fs_block_read(uint8_t drive, uint64_t lba, uint32_t sector_count, void* out);
int fs_block_write(uint8_t drive, uint64_t lba, uint32_t sector_count, const void* in);

void fs_get_io_stats(uint64_t* out_read_bytes, uint64_t* out_write_bytes);
int fs_mkfs_ext2(uint8_t drive, uint32_t lba_start, uint32_t sectors);
int fs_mkfs_ext4(uint8_t drive, uint32_t lba_start, uint32_t sectors);
int fs_mkfs_fat(uint8_t drive, uint32_t lba_start, uint32_t sectors, uint8_t fat_type);

#endif

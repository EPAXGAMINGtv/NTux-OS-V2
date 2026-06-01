#ifndef ISO_FS_H
#define ISO_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fs/vfs.h>

typedef struct {
    bool mounted;
    bool is_atapi;
    uint8_t drive_index;
    uint64_t partition_lba;

    uint32_t root_lba;
    uint32_t root_size;
    bool use_joliet;
} iso_fs_t;

int iso_fs_mount(iso_fs_t* fs, uint8_t drive_index, uint64_t partition_lba);
const vfs_backend_ops_t* iso_fs_backend_ops(void);
int iso_fs_read_file_range(const iso_fs_t* fs, const char* path, size_t offset, void* out, size_t len, size_t* out_read, size_t* out_file_len);

#endif

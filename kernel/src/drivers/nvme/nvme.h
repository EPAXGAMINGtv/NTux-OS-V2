#ifndef NVME_H
#define NVME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NVME_MAX_NAMESPACES 16

typedef struct {
    bool present;
    uint8_t controller_index;
    uint32_t nsid;
    uint32_t lba_size;
    uint64_t lba_count;
    uint64_t sectors_512;
    char model[41];
} nvme_namespace_t;

void nvme_init(void);
void nvme_rescan(bool verbose);
size_t nvme_namespace_count(void);
const nvme_namespace_t* nvme_get_namespace(uint8_t index);
int nvme_read_sectors(uint8_t ns_index, uint64_t lba_512, uint32_t sector_count_512, void* out_buffer);
int nvme_write_sectors(uint8_t ns_index, uint64_t lba_512, uint32_t sector_count_512, const void* in_buffer);

#endif

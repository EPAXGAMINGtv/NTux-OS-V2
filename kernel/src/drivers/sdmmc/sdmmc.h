#ifndef SDMMC_H
#define SDMMC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool present;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t high_capacity;
    uint32_t rca;
    uint64_t sectors;
    char model[48];
} sdmmc_info_t;

void sdmmc_init(void);
void sdmmc_rescan(bool verbose);
size_t sdmmc_device_count(void);
int sdmmc_get_info(size_t index, sdmmc_info_t* out);
int sdmmc_read_sectors(size_t index, uint64_t lba, uint32_t count, void* out);
int sdmmc_write_sectors(size_t index, uint64_t lba, uint32_t count, const void* in);

#endif

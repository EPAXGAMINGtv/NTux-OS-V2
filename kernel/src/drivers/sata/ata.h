#ifndef ATA_H
#define ATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATA_MAX_DRIVES 10
#define ATA_SECTOR_SIZE 512

typedef enum {
    ATA_DRIVE_NONE = 0,
    ATA_DRIVE_ATA,
    ATA_DRIVE_SATA,
    ATA_DRIVE_ATAPI,
    ATA_DRIVE_SATAPI,
} ata_drive_type_t;

typedef struct {
    bool present;
    bool ahci;
    bool lba48_supported;
    ata_drive_type_t type;
    uint8_t ahci_controller;
    uint8_t ahci_port;
    uint8_t channel;
    uint8_t device;
    uint16_t io_base;
    uint16_t ctrl_base;
    uint32_t sectors28;
    uint64_t sectors48;
    char model[41];
} ata_drive_t;

void ata_init(void);
void ata_rescan(bool verbose);
size_t ata_drive_count(void);
const ata_drive_t* ata_get_drive(uint8_t index);

int ata_read_sectors(uint8_t drive_index, uint64_t lba, uint8_t sector_count, void* out_buffer);
int ata_write_sectors(uint8_t drive_index, uint64_t lba, uint8_t sector_count, const void* in_buffer);
int ata_read_cd_sectors(uint8_t drive_index, uint32_t lba, uint8_t sector_count, void* out_buffer);

#endif

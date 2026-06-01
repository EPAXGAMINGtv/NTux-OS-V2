/**-----------------------------------------------------------------------------
 @file    ata.c
 @brief   Implementation of ATA (Advanced Technology Attachment) device handling
----------------------------------------------------------------------------- */
#include <libc/string.h>
#include <drivers/sata/sata.h>
#include <kernel_lib/io.h>
#include <drivers/fs/FAT/fat32.h>

#define ATA_SECTOR_SIZE     512


typedef struct {
    void (*read28)(ata_device_t *dev, uint32_t lba, uint8_t sector_count, uint8_t *target);
    void (*write28)(ata_device_t *dev, uint32_t lba, uint8_t sector_count, uint8_t *source);
} ata_ops_t;

void insw(uint16_t port, uint16_t *buffer, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        buffer[i] = inw(port);
    }
}

uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0x000000FF) << 24) |
           ((hostlong & 0x0000FF00) << 8)  |
           ((hostlong & 0x00FF0000) >> 8)  |
           ((hostlong & 0xFF000000) >> 24);
}

static char ata_drive_char = 'a';
static int cdrom_number = 0;

typedef union {
    uint8_t command_bytes[12];
    uint16_t command_words[6];
} atapi_command_t;



ata_device_t ata_primary_master = { .io_base = 0x1F0, .control = 0x3F6, .slave = 0 };
ata_device_t ata_primary_slave  = { .io_base = 0x1F0, .control = 0x3F6, .slave = 1 };
ata_device_t ata_secondary_master = { .io_base = 0x170, .control = 0x376, .slave = 0 };
ata_device_t ata_secondary_slave  = { .io_base = 0x170, .control = 0x376, .slave = 1 };

static lock_t ata_lock;

/* Function Definition */
static int ata_read_partition_map(ata_device_t * dev, char *devname);
static uint64_t ata_max_offset(ata_device_t * dev);

static void ata_io_wait(ata_device_t * dev)
{
    // Port wartet
    inb(dev->io_base + ATA_REG_ALTSTATUS);
    inb(dev->io_base + ATA_REG_ALTSTATUS);
    inb(dev->io_base + ATA_REG_ALTSTATUS);
    inb(dev->io_base + ATA_REG_ALTSTATUS);
}

static void ata_soft_reset(ata_device_t * dev)
{
    // Controller resetten
    outb(dev->control, 0x04);
    outb(dev->control, 0x00);
}

static void ata_poll(ata_device_t * dev, int advanced_check)
{
    ata_io_wait(dev);

    uint8_t s = inb(dev->io_base + ATA_REG_STATUS);
    while (s & ATA_SR_BSY) {
        inb(dev->io_base + ATA_REG_ALTSTATUS);
        s = inb(dev->io_base + ATA_REG_STATUS);
    }

    if (advanced_check) {
        while (true) {
            inb(dev->io_base + ATA_REG_ALTSTATUS);
            s = inb(dev->io_base + ATA_REG_STATUS);
            if ((s & ATA_SR_ERR) || (s & ATA_SR_DF)) {
                ata_io_wait(dev);
                uint8_t err = inb(dev->io_base + ATA_REG_ERROR);
                // Fehlerbehandlung statt kernel panic
                klog("ATA: Device error code %d\n", err);
            }
            if (s & ATA_SR_DRQ) {
                break;
            }
            ata_io_wait(dev);
        }
    }
}

static int ata_device_init(ata_device_t * dev)
{
    uint16_t bus = dev->io_base;
    uint8_t slave = dev->slave;

    inb(bus + ATA_REG_STATUS);  
    ata_io_wait(dev);  

    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_HDDEVSEL, 0xA0 | slave << 4);
    ata_io_wait(dev);

    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_SECCOUNT0, 0);
    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_LBA0, 0);
    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_LBA1, 0);
    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_LBA2, 0);
    inb(bus + ATA_REG_STATUS);

    outb(bus + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    ata_io_wait(dev);

    uint8_t status = inb(bus + ATA_REG_STATUS);
    if (status == 0)
        return 0;

    int timer = 0xFFFFFF;
    while (timer--) {
        status = inb(bus + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return 0;
        }
        if (!(status & ATA_SR_BSY) && status & ATA_SR_DRQ)
            goto c1;
    }
    return 0;

c1:
    {
        uint8_t mid = inb(bus + ATA_REG_LBA1);
        uint8_t hi = inb(bus + ATA_REG_LBA2);
        if (mid || hi)
            return 0;

        int timer2 = 0xFFFFFF;
        while (timer2--) {
            status = inb(bus + ATA_REG_STATUS);
            if ((status & ATA_SR_ERR))
                return 0;
            if ((status & ATA_SR_DRQ))
                goto c2;
        }
        return 0;
    }

c2:
    {
        insw(bus + ATA_REG_DATA, (uint16_t*)&dev->identity, 256);
        dev->is_atapi = 0;

        uint8_t *ptr = (uint8_t *) dev->identity.model;
        for (int i = 0; i < 39; i += 2) {
            uint8_t tmp = ptr[i + 1];
            ptr[i + 1] = ptr[i];
            ptr[i] = tmp;
        }
        ptr[39] = '\0';

        ptr = (uint8_t *) dev->identity.serial;
        for (int i = 0; i < 19; i += 2) {
            uint8_t tmp = ptr[i + 1];
            ptr[i + 1] = ptr[i];
            ptr[i] = tmp;
        }
        ptr[19] = '\0';

        outb(dev->io_base + ATA_REG_CONTROL, 0x02);

        return 1;
    }
}

static int atapi_device_init(ata_device_t * dev)
{
    dev->is_atapi = 1;

    outb(dev->io_base + 1, 1);
    outb(dev->control, 0);

    ata_io_wait(dev);

    uint16_t bus = dev->io_base;
    uint8_t slave = dev->slave;

    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_HDDEVSEL, 0xA0 | slave << 4);
    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_SECCOUNT0, 0);
    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_LBA0, 0);
    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_LBA1, 0);
    inb(bus + ATA_REG_STATUS);
    outb(bus + ATA_REG_LBA2, 0);
    inb(bus + ATA_REG_STATUS);

    outb(bus + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);

    ata_io_wait(dev);

    uint8_t status = inb(bus + ATA_REG_STATUS);
    while (status & ATA_SR_BSY) {
        for (uint32_t i = 0; i < 0x0FFFFFFF; i++) { }
        status = inb(bus + ATA_REG_STATUS);
    }

    if (status == 0)
        return 0;

    while (status & ATA_SR_BSY)
        status = inb(bus + ATA_REG_STATUS);

    uint8_t mid = inb(bus + ATA_REG_LBA1);
    uint8_t hi = inb(bus + ATA_REG_LBA2);
    if (mid || hi)
        return 0;

    while (!(status & (ATA_SR_ERR | ATA_SR_DRQ)))
        status = inb(bus + ATA_REG_STATUS);

    if (status & ATA_SR_ERR)
        return 0;

    insw(bus + ATA_REG_DATA, (uint16_t*)&dev->identity, 256);
    dev->is_atapi = 0;

    uint8_t *ptr = (uint8_t *) dev->identity.model;
    for (int i = 0; i < 39; i += 2) {
        uint8_t tmp = ptr[i + 1];
        ptr[i + 1] = ptr[i];
        ptr[i] = tmp;
    }
    ptr[39] = '\0';

    ptr = (uint8_t *) dev->identity.serial;
    for (int i = 0; i < 19; i += 2) {
        uint8_t tmp = ptr[i + 1];
        ptr[i + 1] = ptr[i];
        ptr[i] = tmp;
    }
    ptr[19] = '\0';

    /* Detect medium */
    atapi_command_t command;
    memset(&command, 0, sizeof(command));
    command.command_bytes[0] = 0x25;

    outb(bus + ATA_REG_FEATURES, 0x00);
    outb(bus + ATA_REG_LBA1, 0x08);
    outb(bus + ATA_REG_LBA2, 0x08);
    outb(bus + ATA_REG_COMMAND, ATA_CMD_PACKET);

    while (1) {
        uint8_t status = inb(dev->io_base + ATA_REG_STATUS);
        if ((status & ATA_SR_ERR))
            goto atapi_error;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY))
            break;
    }

    for (int i = 0; i < 6; ++i)
        outw(bus, command.command_words[i]);

    while (1) {
        uint8_t status = inb(dev->io_base + ATA_REG_STATUS);
        if ((status & ATA_SR_ERR))
            goto atapi_error_read;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY))
            break;
        if ((status & ATA_SR_DRQ))
            break;
    }

    uint16_t data[4];
    insw(bus, data, 4);

    uint32_t lba, blocks;
    memcpy(&lba, &data[0], 4);
    memcpy(&blocks, &data[2], 4);
    lba = htonl(lba);
    blocks = htonl(blocks);

    dev->atapi_lba = lba;
    dev->atapi_sector_size = blocks;

    if (!lba)
        return 1;

    return 1;

atapi_error_read:
    return 0;

atapi_error:
    return 0;
}

static int ata_device_detect(ata_device_t * dev)
{
    ata_soft_reset(dev);

    outb(dev->io_base + ATA_REG_HDDEVSEL, 0xA0 | (dev->slave << 4));
    ata_io_wait(dev);

    uint8_t status = inb(dev->io_base + ATA_REG_STATUS);

    if (status == 0xFF || status == 0x00)
        return 0; 
    uint8_t cl = inb(dev->io_base + ATA_REG_LBA1);
    uint8_t ch = inb(dev->io_base + ATA_REG_LBA2);

    if (cl == 0x00 && ch == 0x00)
    {
        if (!ata_device_init(dev))
            return 0;

        return 1;
    }

    if ((cl == 0x14 && ch == 0xEB) ||
        (cl == 0x69 && ch == 0x96))
    {
        if (!atapi_device_init(dev))
            return 0;

        return 2;
    }

    return 0;
}

int ata_init(void)
{
    ata_device_detect(&ata_primary_master);
    ata_device_detect(&ata_primary_slave);
    ata_device_detect(&ata_secondary_master);
    ata_device_detect(&ata_secondary_slave);

    return 1;
}

static uint64_t ata_max_offset(ata_device_t * dev)
{
    uint64_t sectors = dev->identity.sectors_48;
    if (!sectors)
        sectors = dev->identity.sectors_28;

    return sectors * ATA_SECTOR_SIZE;
}

void ata_pio_read28(ata_device_t * dev, uint32_t lba, uint8_t sector_count, uint8_t * target)
{
    uint16_t bus = dev->io_base;
    uint8_t slave = dev->slave;

    ata_io_wait(dev);

    outb(bus + ATA_REG_HDDEVSEL, 0xE0 | slave << 4 | ((lba & 0x0f000000) >> 24));
    ata_io_wait(dev);

    outb(bus + ATA_REG_ERROR, 0x00);
    outb(bus + ATA_REG_SECCOUNT0, sector_count);
    outb(bus + ATA_REG_LBA0, (lba & 0xFF));
    outb(bus + ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outb(bus + ATA_REG_LBA2, (lba >> 16) & 0xFF);
    outb(bus + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    for (uint64_t i = 0; i < sector_count; i++) {
        ata_poll(dev, 1);
        insw(bus + ATA_REG_DATA, target, 256);
        target += 512;
        ata_io_wait(dev);
    }

    ata_poll(dev, 0);
}

void ata_pio_write28(ata_device_t * dev, uint32_t lba, uint8_t sector_count, uint8_t * source)
{
    uint16_t bus = dev->io_base;
    uint8_t slave = dev->slave;

    ata_io_wait(dev);

    outb(bus + ATA_REG_HDDEVSEL, 0xE0 | slave << 4 | ((lba & 0x0f000000) >> 24));
    ata_io_wait(dev);

    outb(bus + ATA_REG_ERROR, 0x00);
    outb(bus + ATA_REG_SECCOUNT0, sector_count);
    outb(bus + ATA_REG_LBA0, (lba & 0xFF));
    outb(bus + ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outb(bus + ATA_REG_LBA2, (lba >> 16) & 0xFF);
    outb(bus + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    ata_io_wait(dev);

    for (uint64_t i = 0; i < sector_count; i++) {
        ata_poll(dev, 1);
        uint16_t *od = (uint16_t *) source;
        for (uint64_t idx = 0; idx < 256; idx++)
            outw(bus + ATA_REG_DATA, od[idx]);

        source += 512;
        ata_io_wait(dev);
    }

    ata_poll(dev, 0);
    outb(bus + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_poll(dev, 0);
}

int ata_read_partition_map(ata_device_t * dev, char *devname)
{
    mbr_t mbr;
    ata_pio_read28(dev, 0, 1, (uint8_t*)&mbr);

    if (mbr.signature[0] == 0x55 && mbr.signature[1] == 0xAA) {

        for (int i = 0; i < 4; ++i) {
            if (mbr.partitions[i].type == 0x0B
             || mbr.partitions[i].type == 0x0C
             || mbr.partitions[i].type == 0x1C) {

                char partition_name[2], partition_path[260];
                partition_name[0] = '0' + i;
                partition_name[1] = '\0';

                strcpy(partition_path, "/disk/");
                strncat(partition_path, partition_name, sizeof(partition_path));

                fat_probe(&dev->ops, 0);  // Auf Partition 0 zugreifen
                fat_mount(&dev->ops, 0, NULL, partition_path);
                break;
            }
        }

        return 0;
    }

    return 1;
}

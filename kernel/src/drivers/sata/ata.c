#include <drivers/sata/ata.h>

#include <drivers/framebuffer/kprint.h>
#include <drivers/nvme/nvme.h>
#include <drivers/pci/pci.h>
#include <arch/x86_64/io.h>
#include <mm/pmm.h>
#include <mm/hhdm.h>
#include <lib/string.h>

#define ATA_REG_DATA 0x00
#define ATA_REG_ERROR 0x01
#define ATA_REG_SECCOUNT0 0x02
#define ATA_REG_LBA0 0x03
#define ATA_REG_LBA1 0x04
#define ATA_REG_LBA2 0x05
#define ATA_REG_HDDEVSEL 0x06
#define ATA_REG_COMMAND 0x07
#define ATA_REG_STATUS 0x07

#define ATA_REG_SECCOUNT1 0x08
#define ATA_REG_LBA3 0x09
#define ATA_REG_LBA4 0x0A
#define ATA_REG_LBA5 0x0B

#define ATA_REG_ALTSTATUS 0x00

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_DF 0x20
#define ATA_SR_DRDY 0x40
#define ATA_SR_BSY 0x80

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_PACKET 0xA0
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_READ_PIO_EXT 0x24
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_WRITE_PIO_EXT 0x34
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA

#define ATA_IO_PRIMARY 0x1F0
#define ATA_CTRL_PRIMARY 0x3F6
#define ATA_IO_SECONDARY 0x170
#define ATA_CTRL_SECONDARY 0x376

#define AHCI_MAX_CONTROLLERS 4
#define AHCI_MAX_PORTS 32
#define AHCI_DMA_BOUNCE_SECTORS (4096u / ATA_SECTOR_SIZE)

#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_IDE 0x01
#define PCI_SUBCLASS_SATA 0x06
#define PCI_PROGIF_AHCI 0x01
#define PCI_COMMAND_REG 0x04
#define PCI_BAR0 0x10
#define PCI_BAR1 0x14
#define PCI_BAR2 0x18
#define PCI_BAR3 0x1C
#define PCI_BAR5 0x24

#define AHCI_PORT_DET_PRESENT 0x3
#define AHCI_PORT_IPM_ACTIVE 0x1

#define HBA_PxCMD_ST (1u << 0)
#define HBA_PxCMD_FRE (1u << 4)
#define HBA_PxCMD_FR (1u << 14)
#define HBA_PxCMD_CR (1u << 15)

#define HBA_PxIS_TFES (1u << 30)

#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08

typedef struct {
    uint16_t io;
    uint16_t ctrl;
} ata_channel_t;

static ata_channel_t g_channels[2] = {
    { ATA_IO_PRIMARY, ATA_CTRL_PRIMARY },
    { ATA_IO_SECONDARY, ATA_CTRL_SECONDARY },
};

static ata_drive_t g_drives[ATA_MAX_DRIVES];
static size_t g_drive_count;

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} hba_port_t;

typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t rsv[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    hba_port_t ports[AHCI_MAX_PORTS];
} hba_mem_t;

typedef struct __attribute__((packed)) {
    uint8_t fis_type;
    uint8_t pmport_c;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t rsv1[4];
} fis_reg_h2d_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} hba_cmd_header_t;

typedef struct __attribute__((packed)) {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc_i;
} hba_prdt_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    hba_prdt_entry_t prdt_entry[1];
} hba_cmd_tbl_t;

typedef struct {
    hba_mem_t* abar;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
} ahci_controller_t;

static ahci_controller_t g_ahci_ctrls[AHCI_MAX_CONTROLLERS];
static size_t g_ahci_ctrl_count;

#define ATA_DETECT_TIMEOUT 1000000

static inline void* ata_phys_to_virt(uintptr_t phys) {
    uint64_t off = hhdm_offset_get();
    return (void*)(uintptr_t)(phys + (uintptr_t)off);
}

static uint64_t ata_drive_total_sectors(const ata_drive_t* drive) {
    if (!drive || !drive->present) return 0;
    return drive->sectors48 ? drive->sectors48 : drive->sectors28;
}

static uint16_t ata_decode_ide_io_bar(uint32_t bar, uint16_t fallback) {
    if ((bar & 0x1u) == 0) return fallback;
    uint16_t base = (uint16_t)(bar & ~0x3u);
    return base ? base : fallback;
}

static uint16_t ata_decode_ide_ctrl_bar(uint32_t bar, uint16_t fallback) {
    if ((bar & 0x1u) == 0) return fallback;
    uint16_t base = (uint16_t)(bar & ~0x3u);
    if (base == 0) return fallback;
    return (uint16_t)(base + 2u);
}

static void ata_ide_native_scan_cb(uint32_t bus, uint32_t device, uint32_t function, uint16_t vendor, uint16_t device_id, void* extra) {
    (void)vendor;
    (void)device_id;
    ata_channel_t* channels = (ata_channel_t*)extra;
    if (!channels) return;

    uint8_t class_code = (uint8_t)pci_read_field(bus, device, function, 0x0B, 1);
    uint8_t subclass = (uint8_t)pci_read_field(bus, device, function, 0x0A, 1);
    if (class_code != PCI_CLASS_MASS_STORAGE || subclass != PCI_SUBCLASS_IDE) return;

    uint8_t prog_if = (uint8_t)pci_read_field(bus, device, function, 0x09, 1);
    uint32_t bar0 = pci_read_field(bus, device, function, PCI_BAR0, 4);
    uint32_t bar1 = pci_read_field(bus, device, function, PCI_BAR1, 4);
    uint32_t bar2 = pci_read_field(bus, device, function, PCI_BAR2, 4);
    uint32_t bar3 = pci_read_field(bus, device, function, PCI_BAR3, 4);

    if (prog_if & 0x01u) {
        channels[0].io = ata_decode_ide_io_bar(bar0, ATA_IO_PRIMARY);
        channels[0].ctrl = ata_decode_ide_ctrl_bar(bar1, ATA_CTRL_PRIMARY);
    }
    if (prog_if & 0x04u) {
        channels[1].io = ata_decode_ide_io_bar(bar2, ATA_IO_SECONDARY);
        channels[1].ctrl = ata_decode_ide_ctrl_bar(bar3, ATA_CTRL_SECONDARY);
    }
}

static void ata_configure_legacy_channels_from_pci(void) {
    g_channels[0].io = ATA_IO_PRIMARY;
    g_channels[0].ctrl = ATA_CTRL_PRIMARY;
    g_channels[1].io = ATA_IO_SECONDARY;
    g_channels[1].ctrl = ATA_CTRL_SECONDARY;
    pci_scan_ex(ata_ide_native_scan_cb, g_channels);
}

static void ata_delay(const ata_drive_t* drive) {
    (void)inb((uint16_t)(drive->ctrl_base + ATA_REG_ALTSTATUS));
    (void)inb((uint16_t)(drive->ctrl_base + ATA_REG_ALTSTATUS));
    (void)inb((uint16_t)(drive->ctrl_base + ATA_REG_ALTSTATUS));
    (void)inb((uint16_t)(drive->ctrl_base + ATA_REG_ALTSTATUS));
}

static int ata_wait_ready(const ata_drive_t* drive) {
    uint8_t status;
    for (int i = 0; i < 1000000; ++i) {
        status = inb((uint16_t)(drive->io_base + ATA_REG_STATUS));
        if ((status & ATA_SR_BSY) == 0) {
            if (status & ATA_SR_ERR) {
                return -1;
            }
            if (status & ATA_SR_DF) {
                return -2;
            }
            return 0;
        }
    }
    return -3;
}

static int ata_wait_drq(const ata_drive_t* drive) {
    uint8_t status;
    for (int i = 0; i < 1000000; ++i) {
        status = inb((uint16_t)(drive->io_base + ATA_REG_STATUS));
        if (status & ATA_SR_ERR) {
            return -1;
        }
        if (status & ATA_SR_DF) {
            return -2;
        }
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ)) {
            return 0;
        }
    }
    return -3;
}

static int ata_wait_not_busy(const ata_drive_t* drive) {
    for (int i = 0; i < 1000000; ++i) {
        uint8_t status = inb((uint16_t)(drive->io_base + ATA_REG_STATUS));
        if ((status & ATA_SR_BSY) == 0) {
            return 0;
        }
    }
    return -1;
}

static void ata_read_words(uint16_t port, uint16_t* out, size_t words) {
    for (size_t i = 0; i < words; ++i) {
        out[i] = inw(port);
    }
}

static void ata_write_words(uint16_t port, const uint16_t* in, size_t words) {
    for (size_t i = 0; i < words; ++i) {
        outw(port, in[i]);
    }
}

static void ata_decode_model(const uint16_t* identify, char out_model[41]) {
    const uint8_t* model_raw = (const uint8_t*)&identify[27];
    for (int i = 0; i < 40; i += 2) {
        out_model[i] = (char)model_raw[i + 1];
        out_model[i + 1] = (char)model_raw[i];
    }
    out_model[40] = '\0';

    for (int i = 39; i >= 0; --i) {
        if (out_model[i] == ' ' || out_model[i] == '\0') {
            out_model[i] = '\0';
            continue;
        }
        break;
    }
}

static uint32_t ata_ident_u32(const uint16_t* identify, size_t word) {
    return (uint32_t)identify[word] | ((uint32_t)identify[word + 1] << 16);
}

static uint64_t ata_ident_u64(const uint16_t* identify, size_t word) {
    uint64_t lo = ata_ident_u32(identify, word);
    uint64_t hi = ata_ident_u32(identify, word + 2);
    return lo | (hi << 32);
}

static ata_drive_type_t ata_probe_signature(uint8_t cl, uint8_t ch) {
    if (cl == 0x00 && ch == 0x00) return ATA_DRIVE_ATA;
    if (cl == 0x3C && ch == 0xC3) return ATA_DRIVE_SATA;
    if (cl == 0x14 && ch == 0xEB) return ATA_DRIVE_ATAPI;
    if (cl == 0x69 && ch == 0x96) return ATA_DRIVE_SATAPI;
    return ATA_DRIVE_NONE;
}

static ata_drive_type_t ahci_probe_signature(uint32_t sig) {
    if (sig == 0x00000101u) return ATA_DRIVE_SATA;
    if (sig == 0xEB140101u) return ATA_DRIVE_SATAPI;
    return ATA_DRIVE_NONE;
}

static int ahci_find_cmdslot(hba_port_t* port) {
    uint32_t slots = port->sact | port->ci;
    for (int i = 0; i < 32; ++i) {
        if ((slots & (1u << i)) == 0) return i;
    }
    return -1;
}

static void ahci_stop_cmd(hba_port_t* port) {
    port->cmd &= ~HBA_PxCMD_ST;
    for (int i = 0; i < ATA_DETECT_TIMEOUT; ++i) {
        if ((port->cmd & HBA_PxCMD_CR) == 0) break;
    }
    port->cmd &= ~HBA_PxCMD_FRE;
    for (int i = 0; i < ATA_DETECT_TIMEOUT; ++i) {
        if ((port->cmd & HBA_PxCMD_FR) == 0) break;
    }
}

static void ahci_start_cmd(hba_port_t* port) {
    for (int i = 0; i < ATA_DETECT_TIMEOUT; ++i) {
        if ((port->cmd & HBA_PxCMD_CR) == 0) break;
    }
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static int ahci_configure_port(hba_port_t* port) {
    ahci_stop_cmd(port);

    uintptr_t clb_phys = (uintptr_t)pmm_alloc_page();
    uintptr_t fb_phys = (uintptr_t)pmm_alloc_page();
    uintptr_t ctba_phys = (uintptr_t)pmm_alloc_page();
    if (!clb_phys || !fb_phys || !ctba_phys) return -1;

    void* clb = ata_phys_to_virt(clb_phys);
    void* fb = ata_phys_to_virt(fb_phys);
    void* ctba = ata_phys_to_virt(ctba_phys);
    if (!clb || !fb || !ctba) return -1;

    memset(clb, 0, 4096);
    memset(fb, 0, 4096);
    memset(ctba, 0, 4096);

    port->clb = (uint32_t)(clb_phys & 0xFFFFFFFFu);
    port->clbu = (uint32_t)(clb_phys >> 32);
    port->fb = (uint32_t)(fb_phys & 0xFFFFFFFFu);
    port->fbu = (uint32_t)(fb_phys >> 32);

    hba_cmd_header_t* cmdhdr = (hba_cmd_header_t*)clb;
    memset(cmdhdr, 0, sizeof(hba_cmd_header_t) * 32);
    for (int i = 0; i < 32; ++i) {
        cmdhdr[i].prdtl = 1;
        cmdhdr[i].ctba = (uint32_t)(ctba_phys & 0xFFFFFFFFu);
        cmdhdr[i].ctbau = (uint32_t)(ctba_phys >> 32);
    }

    port->serr = 0xFFFFFFFFu;
    port->is = 0xFFFFFFFFu;
    port->ci = 0;
    port->sact = 0;
    ahci_start_cmd(port);
    return 0;
}

static int ahci_issue_cmd(hba_port_t* port, bool write, uint8_t command, uint64_t lba, uint16_t sector_count, uintptr_t buffer_phys) {
    for (int i = 0; i < ATA_DETECT_TIMEOUT; ++i) {
        if ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) == 0) break;
        if (i == ATA_DETECT_TIMEOUT - 1) return -1;
    }

    int slot = ahci_find_cmdslot(port);
    if (slot < 0) return -2;

    uintptr_t clb_phys = ((uint64_t)port->clbu << 32) | port->clb;
    hba_cmd_header_t* cmdhdr = (hba_cmd_header_t*)ata_phys_to_virt(clb_phys);
    if (!cmdhdr) return -7;
    hba_cmd_header_t* hdr = &cmdhdr[slot];
    uint32_t ctba_lo = hdr->ctba;
    uint32_t ctba_hi = hdr->ctbau;
    memset(hdr, 0, sizeof(*hdr));
    hdr->ctba = ctba_lo;
    hdr->ctbau = ctba_hi;
    hdr->flags = (uint16_t)(sizeof(fis_reg_h2d_t) / sizeof(uint32_t));
    if (write) hdr->flags |= (1u << 6);
    hdr->prdtl = 1;

    uintptr_t ctba_phys = ((uint64_t)hdr->ctbau << 32) | hdr->ctba;
    if (ctba_phys == 0) return -6;
    hba_cmd_tbl_t* tbl = (hba_cmd_tbl_t*)ata_phys_to_virt(ctba_phys);
    if (!tbl) return -8;
    memset(tbl, 0, sizeof(*tbl));

    tbl->prdt_entry[0].dba = (uint32_t)(buffer_phys & 0xFFFFFFFFu);
    tbl->prdt_entry[0].dbau = (uint32_t)(buffer_phys >> 32);
    tbl->prdt_entry[0].dbc_i = ((uint32_t)sector_count * ATA_SECTOR_SIZE) - 1u;

    fis_reg_h2d_t* fis = (fis_reg_h2d_t*)(&tbl->cfis[0]);
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = 0x27;
    fis->pmport_c = 1u << 7;
    fis->command = command;
    fis->device = 1u << 6;
    fis->lba0 = (uint8_t)(lba & 0xFFu);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFFu);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFFu);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFFu);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFFu);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFFu);
    fis->countl = (uint8_t)(sector_count & 0xFFu);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xFFu);

    port->is = 0xFFFFFFFFu;
    port->ci = 1u << slot;

    for (int i = 0; i < ATA_DETECT_TIMEOUT; ++i) {
        if ((port->ci & (1u << slot)) == 0) break;
        if (port->is & HBA_PxIS_TFES) return -3;
        if (i == ATA_DETECT_TIMEOUT - 1) return -4;
    }
    if (port->is & HBA_PxIS_TFES) return -5;
    return 0;
}

static int ahci_identify(hba_port_t* port, uint16_t identify[256]) {
    uintptr_t dma_phys = (uintptr_t)pmm_alloc_page();
    if (!dma_phys) return -1;
    void* dma_virt = ata_phys_to_virt(dma_phys);
    if (!dma_virt) {
        pmm_free_page((void*)dma_phys);
        return -1;
    }
    memset(dma_virt, 0, 4096);
    int rc = ahci_issue_cmd(port, false, ATA_CMD_IDENTIFY, 0, 1, dma_phys);
    if (rc == 0) memcpy(identify, dma_virt, 512);
    pmm_free_page((void*)dma_phys);
    return rc;
}

static int ahci_rw(const ata_drive_t* drive, uint64_t lba, uint16_t sector_count, void* buffer, bool write) {
    if (!drive || !drive->ahci) return -1;
    if (drive->type != ATA_DRIVE_SATA) return -9;
    if (drive->ahci_controller >= g_ahci_ctrl_count) return -2;
    ahci_controller_t* ctrl = &g_ahci_ctrls[drive->ahci_controller];
    if (drive->ahci_port >= AHCI_MAX_PORTS) return -3;
    hba_port_t* port = &ctrl->abar->ports[drive->ahci_port];
    if (!buffer || sector_count == 0) return -4;

    uintptr_t dma_phys = (uintptr_t)pmm_alloc_page();
    if (!dma_phys) return -5;
    uint8_t* dma_virt = (uint8_t*)ata_phys_to_virt(dma_phys);
    if (!dma_virt) {
        pmm_free_page((void*)dma_phys);
        return -6;
    }

    uint8_t* io_buf = (uint8_t*)buffer;
    uint16_t done = 0;
    while (done < sector_count) {
        uint16_t chunk = (uint16_t)(sector_count - done);
        if (chunk > AHCI_DMA_BOUNCE_SECTORS) chunk = AHCI_DMA_BOUNCE_SECTORS;
        size_t bytes = (size_t)chunk * ATA_SECTOR_SIZE;
        if (write) memcpy(dma_virt, io_buf + ((size_t)done * ATA_SECTOR_SIZE), bytes);
        int rc = ahci_issue_cmd(port, write, write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT, lba + done, chunk, dma_phys);
        if (rc != 0) {
            pmm_free_page((void*)dma_phys);
            return rc;
        }
        if (!write) memcpy(io_buf + ((size_t)done * ATA_SECTOR_SIZE), dma_virt, bytes);
        done = (uint16_t)(done + chunk);
    }
    pmm_free_page((void*)dma_phys);
    return 0;
}

static int ahci_issue_atapi_packet(hba_port_t* port, const uint8_t packet[12], uintptr_t buffer_phys, uint32_t bytes) {
    if (!port || !packet || bytes == 0) return -1;

    for (int i = 0; i < ATA_DETECT_TIMEOUT; ++i) {
        if ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) == 0) break;
        if (i == ATA_DETECT_TIMEOUT - 1) return -2;
    }

    int slot = ahci_find_cmdslot(port);
    if (slot < 0) return -3;

    uintptr_t clb_phys = ((uint64_t)port->clbu << 32) | port->clb;
    hba_cmd_header_t* cmdhdr = (hba_cmd_header_t*)ata_phys_to_virt(clb_phys);
    if (!cmdhdr) return -4;
    hba_cmd_header_t* hdr = &cmdhdr[slot];
    uint32_t ctba_lo = hdr->ctba;
    uint32_t ctba_hi = hdr->ctbau;
    memset(hdr, 0, sizeof(*hdr));
    hdr->ctba = ctba_lo;
    hdr->ctbau = ctba_hi;
    hdr->flags = (uint16_t)(sizeof(fis_reg_h2d_t) / sizeof(uint32_t));
    hdr->flags |= (1u << 5); /* ATAPI */
    hdr->prdtl = 1;

    uintptr_t ctba_phys = ((uint64_t)hdr->ctbau << 32) | hdr->ctba;
    if (ctba_phys == 0) return -5;
    hba_cmd_tbl_t* tbl = (hba_cmd_tbl_t*)ata_phys_to_virt(ctba_phys);
    if (!tbl) return -6;
    memset(tbl, 0, sizeof(*tbl));

    tbl->prdt_entry[0].dba = (uint32_t)(buffer_phys & 0xFFFFFFFFu);
    tbl->prdt_entry[0].dbau = (uint32_t)(buffer_phys >> 32);
    tbl->prdt_entry[0].dbc_i = bytes - 1u;
    memcpy(tbl->acmd, packet, 12);

    fis_reg_h2d_t* fis = (fis_reg_h2d_t*)(&tbl->cfis[0]);
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = 0x27;
    fis->pmport_c = 1u << 7;
    fis->command = ATA_CMD_PACKET;
    fis->device = 0;

    port->is = 0xFFFFFFFFu;
    port->ci = 1u << slot;

    for (int i = 0; i < ATA_DETECT_TIMEOUT; ++i) {
        if ((port->ci & (1u << slot)) == 0) break;
        if (port->is & HBA_PxIS_TFES) return -7;
        if (i == ATA_DETECT_TIMEOUT - 1) return -8;
    }
    if (port->is & HBA_PxIS_TFES) return -9;
    return 0;
}

static int ahci_read_cd(const ata_drive_t* drive, uint32_t lba, uint8_t sector_count, void* out_buffer) {
    if (!drive || !drive->ahci || !out_buffer || sector_count == 0) return -1;
    if (drive->type != ATA_DRIVE_SATAPI) return -2;
    if (drive->ahci_controller >= g_ahci_ctrl_count) return -3;
    if (drive->ahci_port >= AHCI_MAX_PORTS) return -4;

    ahci_controller_t* ctrl = &g_ahci_ctrls[drive->ahci_controller];
    hba_port_t* port = &ctrl->abar->ports[drive->ahci_port];

    uintptr_t dma_phys = (uintptr_t)pmm_alloc_page();
    if (!dma_phys) return -5;
    uint8_t* dma_virt = (uint8_t*)ata_phys_to_virt(dma_phys);
    if (!dma_virt) {
        pmm_free_page((void*)dma_phys);
        return -6;
    }

    uint8_t* out = (uint8_t*)out_buffer;
    for (uint8_t s = 0; s < sector_count; ++s) {
        uint8_t pkt[12];
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 0xA8; /* SCSI READ(12) */
        uint32_t slba = lba + s;
        pkt[2] = (uint8_t)((slba >> 24) & 0xFFu);
        pkt[3] = (uint8_t)((slba >> 16) & 0xFFu);
        pkt[4] = (uint8_t)((slba >> 8) & 0xFFu);
        pkt[5] = (uint8_t)(slba & 0xFFu);
        pkt[9] = 1; /* one logical 2048-byte sector */

        memset(dma_virt, 0, 2048);
        int rc = ahci_issue_atapi_packet(port, pkt, dma_phys, 2048);
        if (rc != 0) {
            pmm_free_page((void*)dma_phys);
            return rc;
        }
        memcpy(out + ((size_t)s * 2048u), dma_virt, 2048);
    }

    pmm_free_page((void*)dma_phys);
    return 0;
}

static void ahci_pci_scan_cb(uint32_t bus, uint32_t device, uint32_t function, uint16_t vendor, uint16_t device_id, void* extra) {
    (void)vendor;
    (void)device_id;
    (void)extra;

    if (g_ahci_ctrl_count >= AHCI_MAX_CONTROLLERS) return;

    uint8_t class_code = (uint8_t)pci_read_field(bus, device, function, 0x0B, 1);
    uint8_t subclass = (uint8_t)pci_read_field(bus, device, function, 0x0A, 1);
    uint8_t prog_if = (uint8_t)pci_read_field(bus, device, function, 0x09, 1);
    if (class_code != PCI_CLASS_MASS_STORAGE || subclass != PCI_SUBCLASS_SATA || prog_if != PCI_PROGIF_AHCI) return;

    uint16_t cmd = (uint16_t)pci_read_field(bus, device, function, PCI_COMMAND_REG, 2);
    cmd |= (1u << 1) | (1u << 2);
    pci_write_field(bus, device, function, PCI_COMMAND_REG, 2, cmd);

    uint32_t bar5_lo = pci_read_field(bus, device, function, PCI_BAR5, 4);
    uint32_t bar5_hi = pci_read_field(bus, device, function, PCI_BAR5 + 4, 4);
    if ((bar5_lo & 0x1u) != 0) return; /* AHCI ABAR must be memory-mapped */
    uint64_t abar_phys = ((uint64_t)bar5_lo & ~0xFu);
    if ((bar5_lo & 0x6u) == 0x4u) {
        abar_phys |= ((uint64_t)bar5_hi << 32);
    }
    if ((bar5_lo & 0x6u) == 0x4u && bar5_hi != 0u) {
        /* Without explicit phys->virt mapping support, avoid >4GiB MMIO windows. */
        return;
    }
    if (abar_phys == 0) return;

    ahci_controller_t* ctrl = &g_ahci_ctrls[g_ahci_ctrl_count++];
    ctrl->abar = (hba_mem_t*)ata_phys_to_virt((uintptr_t)abar_phys);
    if (!ctrl->abar) {
        g_ahci_ctrl_count--;
        return;
    }
    if (ctrl->abar->pi == 0) {
        g_ahci_ctrl_count--;
        return;
    }
    ctrl->bus = (uint8_t)bus;
    ctrl->dev = (uint8_t)device;
    ctrl->func = (uint8_t)function;
}

static void ata_scan_ahci(bool verbose) {
    g_ahci_ctrl_count = 0;
    memset(g_ahci_ctrls, 0, sizeof(g_ahci_ctrls));

    pci_scan_ex(ahci_pci_scan_cb, NULL);
    if (verbose && g_ahci_ctrl_count > 0) {
        kprint("[AHCI] controllers: ");
        kprint_int((int)g_ahci_ctrl_count);
        kprint("\n");
    }

    for (size_t c = 0; c < g_ahci_ctrl_count && g_drive_count < ATA_MAX_DRIVES; ++c) {
        ahci_controller_t* ctrl = &g_ahci_ctrls[c];
        hba_mem_t* abar = ctrl->abar;
        uint32_t pi = abar->pi;

        for (uint8_t port_idx = 0; port_idx < AHCI_MAX_PORTS && g_drive_count < ATA_MAX_DRIVES; ++port_idx) {
            if ((pi & (1u << port_idx)) == 0) continue;

            hba_port_t* port = &abar->ports[port_idx];
            uint32_t ssts = port->ssts;
            uint8_t det = (uint8_t)(ssts & 0x0F);
            uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);
            if (det != AHCI_PORT_DET_PRESENT || ipm != AHCI_PORT_IPM_ACTIVE) continue;

            ata_drive_type_t type = ahci_probe_signature(port->sig);
            if (type == ATA_DRIVE_NONE) continue;

            if (ahci_configure_port(port) != 0) continue;

            ata_drive_t* d = &g_drives[g_drive_count];
            memset(d, 0, sizeof(*d));
            d->present = true;
            d->ahci = true;
            d->ahci_controller = (uint8_t)c;
            d->ahci_port = port_idx;
            d->type = type;
            d->channel = 0xFF;
            d->device = 0xFF;

            if (type == ATA_DRIVE_SATA) {
                uint16_t identify[256];
                if (ahci_identify(port, identify) == 0) {
                    d->sectors28 = ata_ident_u32(identify, 60);
                    d->sectors48 = ata_ident_u64(identify, 100);
                    d->lba48_supported = (identify[83] & (1u << 10)) != 0;
                    ata_decode_model(identify, d->model);
                } else {
                    strncpy(d->model, "AHCI SATA Drive", sizeof(d->model) - 1);
                    d->model[sizeof(d->model) - 1] = '\0';
                }
            } else {
                strncpy(d->model, "AHCI SATAPI Device", sizeof(d->model) - 1);
                d->model[sizeof(d->model) - 1] = '\0';
            }

            ++g_drive_count;
        }
    }
}

static bool ata_detect_device(uint8_t channel, uint8_t device, ata_drive_t* out_drive) {
    const ata_channel_t* ch = &g_channels[channel];
    uint16_t io = ch->io;

    outb((uint16_t)(io + ATA_REG_HDDEVSEL), (uint8_t)(0xA0 | (device << 4)));
    for (int i = 0; i < 5; ++i) {
        (void)inb((uint16_t)(io + ATA_REG_STATUS));
    }

    outb((uint16_t)(io + ATA_REG_SECCOUNT0), 0);
    outb((uint16_t)(io + ATA_REG_LBA0), 0);
    outb((uint16_t)(io + ATA_REG_LBA1), 0);
    outb((uint16_t)(io + ATA_REG_LBA2), 0);
    outb((uint16_t)(io + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);

    uint8_t status = inb((uint16_t)(io + ATA_REG_STATUS));
    if (status == 0) {
        return false;
    }

    for (int wait = 0; wait < ATA_DETECT_TIMEOUT && (status & ATA_SR_BSY) != 0; ++wait) {
        status = inb((uint16_t)(io + ATA_REG_STATUS));
    }
    if ((status & ATA_SR_BSY) != 0) {
        return false;
    }

    uint8_t cl = inb((uint16_t)(io + ATA_REG_LBA1));
    uint8_t chsig = inb((uint16_t)(io + ATA_REG_LBA2));
    ata_drive_type_t type = ata_probe_signature(cl, chsig);

    if (type == ATA_DRIVE_NONE) {
        return false;
    }

    if (type == ATA_DRIVE_ATAPI || type == ATA_DRIVE_SATAPI) {
        memset(out_drive, 0, sizeof(*out_drive));
        out_drive->present = true;
        out_drive->type = type;
        out_drive->channel = channel;
        out_drive->device = device;
        out_drive->io_base = ch->io;
        out_drive->ctrl_base = ch->ctrl;
        strncpy(out_drive->model, (type == ATA_DRIVE_ATAPI) ? "ATAPI CD/DVD" : "SATAPI CD/DVD", sizeof(out_drive->model) - 1);
        out_drive->model[sizeof(out_drive->model) - 1] = '\0';
        return true;
    }

    for (int wait = 0; wait < ATA_DETECT_TIMEOUT; ++wait) {
        status = inb((uint16_t)(io + ATA_REG_STATUS));
        if (status & ATA_SR_ERR) {
            return false;
        }
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ)) {
            break;
        }
        if (wait == ATA_DETECT_TIMEOUT - 1) {
            return false;
        }
    }

    uint16_t identify[256];
    ata_read_words((uint16_t)(io + ATA_REG_DATA), identify, 256);

    memset(out_drive, 0, sizeof(*out_drive));
    out_drive->present = true;
    out_drive->type = type;
    out_drive->channel = channel;
    out_drive->device = device;
    out_drive->io_base = ch->io;
    out_drive->ctrl_base = ch->ctrl;
    out_drive->sectors28 = ata_ident_u32(identify, 60);
    out_drive->sectors48 = ata_ident_u64(identify, 100);
    out_drive->lba48_supported = (identify[83] & (1u << 10)) != 0;
    ata_decode_model(identify, out_drive->model);

    return true;
}

void ata_rescan(bool verbose) {
    memset(g_drives, 0, sizeof(g_drives));
    g_drive_count = 0;

    ata_scan_ahci(verbose);
    ata_configure_legacy_channels_from_pci();

    for (uint8_t channel = 0; channel < 2 && g_drive_count < ATA_MAX_DRIVES; ++channel) {
        for (uint8_t device = 0; device < 2 && g_drive_count < ATA_MAX_DRIVES; ++device) {
            ata_drive_t candidate;
            if (ata_detect_device(channel, device, &candidate)) {
                g_drives[g_drive_count] = candidate;
                ++g_drive_count;
            }
        }
    }

    if (verbose) {
        kprint("[ATA] detected drives: ");
        kprint_int((int)g_drive_count);
        kprint("/10\n");
        for (size_t i = 0; i < g_drive_count; ++i) {
            kprint("[ATA] #");
            kprint_int((int)i);
            kprint(" ");
            kprint(g_drives[i].model);
            kprint("\n");
        }
    }
}

void ata_init(void) {
    ata_rescan(true);
}

size_t ata_drive_count(void) {
    return g_drive_count;
}

const ata_drive_t* ata_get_drive(uint8_t index) {
    if (index >= g_drive_count) {
        return NULL;
    }
    return &g_drives[index];
}

static int ata_select_lba28(const ata_drive_t* drive, uint32_t lba, uint8_t sector_count, uint8_t command) {
    if (ata_wait_ready(drive) != 0) {
        return -1;
    }

    outb((uint16_t)(drive->io_base + ATA_REG_HDDEVSEL),
         (uint8_t)(0xE0 | (drive->device << 4) | ((lba >> 24) & 0x0F)));
    outb((uint16_t)(drive->io_base + ATA_REG_SECCOUNT0), sector_count);
    outb((uint16_t)(drive->io_base + ATA_REG_LBA0), (uint8_t)(lba & 0xFF));
    outb((uint16_t)(drive->io_base + ATA_REG_LBA1), (uint8_t)((lba >> 8) & 0xFF));
    outb((uint16_t)(drive->io_base + ATA_REG_LBA2), (uint8_t)((lba >> 16) & 0xFF));
    outb((uint16_t)(drive->io_base + ATA_REG_COMMAND), command);

    return 0;
}

static int ata_select_lba48(const ata_drive_t* drive, uint64_t lba, uint8_t sector_count, uint8_t command) {
    if (ata_wait_ready(drive) != 0) {
        return -1;
    }

    outb((uint16_t)(drive->io_base + ATA_REG_HDDEVSEL), (uint8_t)(0x40 | (drive->device << 4)));

    outb((uint16_t)(drive->io_base + ATA_REG_SECCOUNT1), 0);
    outb((uint16_t)(drive->io_base + ATA_REG_LBA3), (uint8_t)((lba >> 24) & 0xFF));
    outb((uint16_t)(drive->io_base + ATA_REG_LBA4), (uint8_t)((lba >> 32) & 0xFF));
    outb((uint16_t)(drive->io_base + ATA_REG_LBA5), (uint8_t)((lba >> 40) & 0xFF));

    outb((uint16_t)(drive->io_base + ATA_REG_SECCOUNT0), sector_count);
    outb((uint16_t)(drive->io_base + ATA_REG_LBA0), (uint8_t)(lba & 0xFF));
    outb((uint16_t)(drive->io_base + ATA_REG_LBA1), (uint8_t)((lba >> 8) & 0xFF));
    outb((uint16_t)(drive->io_base + ATA_REG_LBA2), (uint8_t)((lba >> 16) & 0xFF));

    outb((uint16_t)(drive->io_base + ATA_REG_COMMAND), command);

    return 0;
}

int ata_read_sectors(uint8_t drive_index, uint64_t lba, uint8_t sector_count, void* out_buffer) {
    if (out_buffer == NULL || sector_count == 0) {
        return -1;
    }

    if (drive_index >= g_drive_count) {
        return nvme_read_sectors((uint8_t)(drive_index - (uint8_t)g_drive_count), lba, sector_count, out_buffer);
    }

    const ata_drive_t* drive = ata_get_drive(drive_index);
    if (drive == NULL || !drive->present) {
        return -2;
    }
    if (drive->type == ATA_DRIVE_ATAPI || drive->type == ATA_DRIVE_SATAPI) {
        return -3;
    }
    uint64_t total = ata_drive_total_sectors(drive);
    if (total > 0 && (lba >= total || lba + sector_count > total)) {
        return -4;
    }
    if (drive->ahci) {
        return ahci_rw(drive, lba, sector_count, out_buffer, false);
    }

    uint16_t* out_words = (uint16_t*)out_buffer;
    bool use_lba48 = (lba > 0x0FFFFFFFULL);
    if (use_lba48 && !drive->lba48_supported) {
        return -5;
    }
    int rc = use_lba48
        ? ata_select_lba48(drive, lba, sector_count, ATA_CMD_READ_PIO_EXT)
        : ata_select_lba28(drive, (uint32_t)lba, sector_count, ATA_CMD_READ_PIO);
    if (rc != 0) {
        return -3;
    }

    for (uint8_t s = 0; s < sector_count; ++s) {
        if (ata_wait_drq(drive) != 0) {
            return -4;
        }
        ata_read_words((uint16_t)(drive->io_base + ATA_REG_DATA), out_words + (s * 256), 256);
        ata_delay(drive);
    }

    return 0;
}

int ata_write_sectors(uint8_t drive_index, uint64_t lba, uint8_t sector_count, const void* in_buffer) {
    if (in_buffer == NULL || sector_count == 0) {
        return -1;
    }

    if (drive_index >= g_drive_count) {
        return nvme_write_sectors((uint8_t)(drive_index - (uint8_t)g_drive_count), lba, sector_count, in_buffer);
    }

    const ata_drive_t* drive = ata_get_drive(drive_index);
    if (drive == NULL || !drive->present) {
        return -2;
    }
    if (drive->type == ATA_DRIVE_ATAPI || drive->type == ATA_DRIVE_SATAPI) {
        return -3;
    }
    uint64_t total = ata_drive_total_sectors(drive);
    if (total > 0 && (lba >= total || lba + sector_count > total)) {
        return -4;
    }
    if (drive->ahci) {
        return ahci_rw(drive, lba, sector_count, (void*)in_buffer, true);
    }

    const uint16_t* in_words = (const uint16_t*)in_buffer;
    bool use_lba48 = (lba > 0x0FFFFFFFULL);
    if (use_lba48 && !drive->lba48_supported) {
        return -5;
    }
    int rc = use_lba48
        ? ata_select_lba48(drive, lba, sector_count, ATA_CMD_WRITE_PIO_EXT)
        : ata_select_lba28(drive, (uint32_t)lba, sector_count, ATA_CMD_WRITE_PIO);
    if (rc != 0) {
        return -3;
    }

    for (uint8_t s = 0; s < sector_count; ++s) {
        if (ata_wait_drq(drive) != 0) {
            return -4;
        }
        ata_write_words((uint16_t)(drive->io_base + ATA_REG_DATA), in_words + (s * 256), 256);
        ata_delay(drive);
    }

    outb((uint16_t)(drive->io_base + ATA_REG_COMMAND), use_lba48 ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
    if (ata_wait_ready(drive) != 0) {
        return -5;
    }

    return 0;
}

int ata_read_cd_sectors(uint8_t drive_index, uint32_t lba, uint8_t sector_count, void* out_buffer) {
    if (!out_buffer || sector_count == 0) return -1;
    const ata_drive_t* drive = ata_get_drive(drive_index);
    if (!drive || !drive->present) return -2;
    if (drive->type != ATA_DRIVE_ATAPI && drive->type != ATA_DRIVE_SATAPI) return -3;
    if (drive->ahci) return ahci_read_cd(drive, lba, sector_count, out_buffer);

    uint8_t* out = (uint8_t*)out_buffer;
    uint16_t io = drive->io_base;

    for (uint8_t s = 0; s < sector_count; ++s) {
        outb((uint16_t)(io + ATA_REG_HDDEVSEL), (uint8_t)(0xA0 | (drive->device << 4)));
        for (int i = 0; i < 5; ++i) (void)inb((uint16_t)(drive->ctrl_base + ATA_REG_ALTSTATUS));

        /*
         * ATAPI devices may report ERR in idle status from previous sense data.
         * For PACKET commands we only need BSY clear before command issue.
         */
        if (ata_wait_not_busy(drive) != 0) return -4;

        outb((uint16_t)(io + ATA_REG_ERROR), 0);
        outb((uint16_t)(io + ATA_REG_LBA1), 0x00);
        outb((uint16_t)(io + ATA_REG_LBA2), 0x08); /* 2048-byte transfer */
        outb((uint16_t)(io + ATA_REG_COMMAND), ATA_CMD_PACKET);

        int drq_ok = 0;
        for (int wait = 0; wait < ATA_DETECT_TIMEOUT; ++wait) {
            uint8_t st = inb((uint16_t)(io + ATA_REG_STATUS));
            if (st & ATA_SR_ERR) return -5;
            if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRQ)) {
                drq_ok = 1;
                break;
            }
        }
        if (!drq_ok) return -6;

        uint8_t pkt[12];
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 0xA8; /* SCSI READ(12) */
        uint32_t slba = lba + s;
        pkt[2] = (uint8_t)((slba >> 24) & 0xFF);
        pkt[3] = (uint8_t)((slba >> 16) & 0xFF);
        pkt[4] = (uint8_t)((slba >> 8) & 0xFF);
        pkt[5] = (uint8_t)(slba & 0xFF);
        pkt[9] = 1; /* one logical block */

        for (int i = 0; i < 6; ++i) {
            uint16_t w = (uint16_t)pkt[i * 2] | ((uint16_t)pkt[i * 2 + 1] << 8);
            outw((uint16_t)(io + ATA_REG_DATA), w);
        }

        drq_ok = 0;
        for (int wait = 0; wait < ATA_DETECT_TIMEOUT; ++wait) {
            uint8_t st = inb((uint16_t)(io + ATA_REG_STATUS));
            if (st & ATA_SR_ERR) return -7;
            if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRQ)) {
                drq_ok = 1;
                break;
            }
        }
        if (!drq_ok) return -8;

        for (int i = 0; i < 1024; ++i) {
            uint16_t w = inw((uint16_t)(io + ATA_REG_DATA));
            out[s * 2048u + i * 2u] = (uint8_t)(w & 0xFF);
            out[s * 2048u + i * 2u + 1u] = (uint8_t)((w >> 8) & 0xFF);
        }

        for (int wait = 0; wait < ATA_DETECT_TIMEOUT; ++wait) {
            uint8_t st = inb((uint16_t)(io + ATA_REG_STATUS));
            if ((st & ATA_SR_BSY) == 0) break;
            if (wait == ATA_DETECT_TIMEOUT - 1) return -9;
        }
    }

    return 0;
}

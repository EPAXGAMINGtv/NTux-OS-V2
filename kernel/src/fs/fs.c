#include <fs/fs.h>

#include <drivers/nvme/nvme.h>
#include <drivers/sdmmc/sdmmc.h>
#include <drivers/sata/ata.h>
#include <fs/ext2.h>
#include <fs/ext4.h>
#include <fs/fat.h>
#include <fs/devfs.h>
#include <fs/iso.h>
#include <fs/ramfs.h>
#include <fs/vfs.h>

#include <drivers/framebuffer/kprint.h>
#include <lib/string.h>

static ramfs_t g_root_ramfs;
static fat_fs_t g_fat_mounts[8];
static ext2_fs_t g_ext2_mounts[8];
static ext4_fs_t g_ext4_mounts[8];
static iso_fs_t g_iso_mounts[8];
static size_t g_fat_mount_count;
static size_t g_ext2_mount_count;
static size_t g_ext4_mount_count;
static size_t g_iso_mount_count;
static uint32_t g_mount_generation;
static int g_fs_persist_busy;
static int g_fs_persist_enabled;
static int g_root_score;
static uint64_t g_fs_read_bytes = 0;
static uint64_t g_fs_write_bytes = 0;

typedef struct {
    const iso_fs_t* fs;
    const char* path;
} iso_reader_ctx_t;

static int iso_reader_cb(void* user, size_t offset, void* out, size_t len) {
    if (!user || !out) return -1;
    iso_reader_ctx_t* ctx = (iso_reader_ctx_t*)user;
    size_t read = 0;
    int rc = iso_fs_read_file_range(ctx->fs, ctx->path, offset, out, len, &read, NULL);
    if (rc != 0) return -1;
    return (read == len) ? 0 : -1;
}

typedef struct {
    fat_fs_t* fs;
    const char* path;
} fat_reader_ctx_t;

static int fat_reader_cb(void* user, size_t offset, void* out, size_t len) {
    if (!user || !out) return -1;
    fat_reader_ctx_t* ctx = (fat_reader_ctx_t*)user;
    size_t read = 0;
    int rc = fat_fs_read_file_range(ctx->fs, ctx->path, offset, out, len, &read, NULL);
    if (rc != 0) return -1;
    return (read == len) ? 0 : -1;
}

typedef struct {
    bool present;
    bool is_atapi;
    uint64_t sectors;
    char model[41];
    const char* prefix;
} fs_block_dev_t;

typedef enum {
    FS_KIND_FAT = 1,
    FS_KIND_EXT2 = 2,
    FS_KIND_EXT4 = 3,
    FS_KIND_ISO = 4,
} fs_kind_t;

typedef struct {
    uint8_t drive;
    uint64_t lba;
    uint8_t kind;
} fs_mounted_part_t;

static fs_mounted_part_t g_mounted_parts[32];
static size_t g_mounted_part_count;

#define FS_PERSIST_MAGIC 0x315346505855544Eu 
#define FS_PERSIST_SECTORS 2048u

typedef struct __attribute__((packed)) {
    uint64_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t data_bytes;
    uint32_t total_bytes;
    uint32_t reserved;
} fs_persist_header_t;

typedef struct __attribute__((packed)) {
    uint8_t type; 
    uint8_t reserved[3];
    uint32_t path_len;
    uint32_t data_len;
    uint32_t data_off;
} fs_persist_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t boot[446];
    struct __attribute__((packed)) {
        uint8_t status;
        uint8_t chs_first[3];
        uint8_t type;
        uint8_t chs_last[3];
        uint32_t lba_first;
        uint32_t sectors;
    } part[4];
    uint16_t sig;
} mbr_sector_t;

static int fs_is_extended_mbr_type(uint8_t type) {
    return (type == 0x05u || type == 0x0Fu || type == 0x85u);
}

typedef struct __attribute__((packed)) {
    uint8_t signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t number_of_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_entry_array_crc32;
} gpt_header_t;

static int fs_drive_total_sectors(uint8_t drive, uint64_t* out_total);

static char fs_tolower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int fs_str_contains_icase(const char* text, const char* needle) {
    if (!text || !needle || !needle[0]) return 0;
    size_t nl = strlen(needle);
    size_t tl = strlen(text);
    if (nl > tl) return 0;
    for (size_t i = 0; i + nl <= tl; ++i) {
        int ok = 1;
        for (size_t j = 0; j < nl; ++j) {
            if (fs_tolower_ascii(text[i + j]) != fs_tolower_ascii(needle[j])) {
                ok = 0;
                break;
            }
        }
        if (ok) return 1;
    }
    return 0;
}

static size_t fs_append_u32(char* out, size_t cap, size_t pos, uint32_t value) {
    char tmp[10];
    size_t n = 0;
    if (value == 0) {
        if (pos + 1 < cap) out[pos++] = '0';
        if (pos < cap) out[pos] = '\0';
        return pos;
    }
    while (value > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n > 0 && pos + 1 < cap) out[pos++] = tmp[--n];
    if (pos < cap) out[pos] = '\0';
    return pos;
}

static const char* fs_drive_prefix_from_model(const char* model, int is_atapi) {
    if (is_atapi) return "cd";
    if (fs_str_contains_icase(model, "flash") ||
        fs_str_contains_icase(model, "removable")) {
        return "flash";
    }
    return "disk";
}

static int fs_get_block_device(uint8_t drive, fs_block_dev_t* out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    size_t ata_count = ata_drive_count();
    if (drive < ata_count) {
        const ata_drive_t* drv = ata_get_drive(drive);
        if (!drv || !drv->present) return -1;
        out->present = true;
        out->is_atapi = (drv->type == ATA_DRIVE_ATAPI || drv->type == ATA_DRIVE_SATAPI);
        out->sectors = drv->sectors48 ? drv->sectors48 : drv->sectors28;
        memcpy(out->model, drv->model, sizeof(out->model) - 1);
        out->model[sizeof(out->model) - 1] = '\0';
        out->prefix = fs_drive_prefix_from_model(out->model, out->is_atapi);
        return 0;
    }

    size_t nvme_count = nvme_namespace_count();
    if (drive < ata_count + nvme_count) {
        uint8_t ns_idx = (uint8_t)(drive - ata_count);
        const nvme_namespace_t* nv = nvme_get_namespace(ns_idx);
        if (!nv || !nv->present) return -1;
        out->present = true;
        out->is_atapi = false;
        out->sectors = nv->sectors_512;
        memcpy(out->model, nv->model, sizeof(out->model) - 1);
        out->model[sizeof(out->model) - 1] = '\0';
        out->prefix = "nvme";
        return 0;
    }

    size_t sd_count = sdmmc_device_count();
    if (drive < ata_count + nvme_count + sd_count) {
        size_t sd_idx = (size_t)(drive - ata_count - nvme_count);
        sdmmc_info_t info;
        if (sdmmc_get_info(sd_idx, &info) != 0) return -1;
        out->present = true;
        out->is_atapi = false;
        out->sectors = info.sectors;
        memcpy(out->model, info.model, sizeof(out->model) - 1);
        out->model[sizeof(out->model) - 1] = '\0';
        out->prefix = "sd";
        return 0;
    }

    return -1;
}

static int fs_device_read_sectors(uint8_t drive, uint64_t lba, uint32_t sector_count, void* out) {
    if (!out || sector_count == 0) return -1;
    size_t ata_count = ata_drive_count();
    if (drive < ata_count) {
        uint32_t done = 0;
        while (done < sector_count) {
            uint8_t chunk = (uint8_t)(((sector_count - done) > 255u) ? 255u : (sector_count - done));
            if (ata_read_sectors(drive, lba + done, chunk, (uint8_t*)out + (size_t)done * ATA_SECTOR_SIZE) != 0) return -1;
            done += chunk;
        }
        return 0;
    }
    size_t nvme_count = nvme_namespace_count();
    if (drive < ata_count + nvme_count) {
        return nvme_read_sectors((uint8_t)(drive - ata_count), lba, sector_count, out);
    }

    size_t sd_count = sdmmc_device_count();
    if (drive < ata_count + nvme_count + sd_count) {
        return sdmmc_read_sectors((uint8_t)(drive - ata_count - nvme_count), lba, sector_count, out);
    }
    return -1;
}

static int fs_device_write_sectors(uint8_t drive, uint64_t lba, uint32_t sector_count, const void* in) {
    if (!in || sector_count == 0) return -1;
    size_t ata_count = ata_drive_count();
    if (drive < ata_count) {
        uint32_t done = 0;
        while (done < sector_count) {
            uint8_t chunk = (uint8_t)(((sector_count - done) > 255u) ? 255u : (sector_count - done));
            if (ata_write_sectors(drive, lba + done, chunk, (const uint8_t*)in + (size_t)done * ATA_SECTOR_SIZE) != 0) return -1;
            done += chunk;
        }
        return 0;
    }
    size_t nvme_count = nvme_namespace_count();
    if (drive < ata_count + nvme_count) {
        return nvme_write_sectors((uint8_t)(drive - ata_count), lba, sector_count, in);
    }
    size_t sd_count = sdmmc_device_count();
    if (drive < ata_count + nvme_count + sd_count) {
        return sdmmc_write_sectors((uint8_t)(drive - ata_count - nvme_count), lba, sector_count, in);
    }
    return -1;
}

typedef struct {
    const uint8_t* data;
    size_t len;
} mem_reader_ctx_t;

static int mem_reader_cb(void* user, size_t off, void* out, size_t len) {
    mem_reader_ctx_t* c = (mem_reader_ctx_t*)user;
    if (!c || !out) return -1;
    if (off > c->len) return -1;
    if (off + len > c->len) return -1;
    if (len > 0) memcpy(out, c->data + off, len);
    return 0;
}

static size_t fs_block_device_count(void) {
    return ata_drive_count() + nvme_namespace_count() + sdmmc_device_count();
}

static void fs_build_linux_mount(char out[32], uint8_t drive, uint8_t part_no) {
    size_t p = 0;
    out[p++] = '/';
    out[p++] = 'm';
    out[p++] = 'n';
    out[p++] = 't';
    out[p++] = '/';
    out[p++] = 's';
    out[p++] = 'd';
    out[p++] = (char)('a' + (drive % 26u));
    if (part_no > 0) {
        p = fs_append_u32(out, 32, p, (uint32_t)part_no);
    }
    out[p < 32 ? p : 31] = '\0';
}

static int fs_root_score_from_kind(fs_kind_t kind) {
    if (kind == FS_KIND_EXT4) return 3;
    if (kind == FS_KIND_EXT2) return 2;
    if (kind == FS_KIND_FAT) return 1;
    return 0;
}

static void fs_ensure_linux_dirs(void) {
    const char* dirs[] = {
        "/dev", "/mnt", "/boot", "/home", "/etc", "/bin", "/sbin", "/usr", "/var", "/tmp"
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        (void)vfs_mkdir(dirs[i]);
    }
}

static int fs_partition_already_mounted(uint8_t drive, uint64_t lba) {
    for (size_t i = 0; i < g_mounted_part_count; ++i) {
        if (g_mounted_parts[i].drive == drive && g_mounted_parts[i].lba == lba) return 1;
    }
    return 0;
}

static void fs_register_mounted_partition(uint8_t drive, uint64_t lba, fs_kind_t kind) {
    if (g_mounted_part_count >= (sizeof(g_mounted_parts) / sizeof(g_mounted_parts[0]))) return;
    g_mounted_parts[g_mounted_part_count].drive = drive;
    g_mounted_parts[g_mounted_part_count].lba = lba;
    g_mounted_parts[g_mounted_part_count].kind = (uint8_t)kind;
    g_mounted_part_count++;
}

static int fs_mount_bindings(
    const vfs_backend_ops_t* ops,
    void* fs_ctx,
    uint8_t drive,
    uint8_t part_no,
    fs_kind_t kind
) {
    fs_block_dev_t dev;
    if (fs_get_block_device(drive, &dev) != 0 || !dev.present) return -1;

    char mount_path[32];
    if (kind == FS_KIND_ISO) {
        strcpy(mount_path, "/");
    } else {
        fs_build_linux_mount(mount_path, drive, part_no);
    }

    if (vfs_mount(mount_path, ops, fs_ctx) != 0) return -1;

    {
        int score = fs_root_score_from_kind(kind);
        if (score >= g_root_score) {
            (void)vfs_mount("/", ops, fs_ctx);
            g_root_score = score;
        }
    }
    g_mount_generation++;
    return 0;
}

static int fs_mount_fat(uint8_t drive, uint64_t part_lba, uint8_t part_no) {
    if (fs_partition_already_mounted(drive, part_lba)) return -2;
    if (g_fat_mount_count >= (sizeof(g_fat_mounts) / sizeof(g_fat_mounts[0]))) {
        return -1;
    }
    fat_fs_t* fs = &g_fat_mounts[g_fat_mount_count];
    if (fat_fs_mount(fs, drive, part_lba) != 0) {
        return -1;
    }

    if (fs_mount_bindings(fat_fs_backend_ops(), fs, drive, part_no, FS_KIND_FAT) != 0) return -1;
    fs_register_mounted_partition(drive, part_lba, FS_KIND_FAT);
    g_fat_mount_count++;
    return 0;
}

static int fs_mount_ext2(uint8_t drive, uint64_t part_lba, uint8_t part_no) {
    if (fs_partition_already_mounted(drive, part_lba)) return -2;
    if (g_ext2_mount_count >= (sizeof(g_ext2_mounts) / sizeof(g_ext2_mounts[0]))) {
        return -1;
    }
    ext2_fs_t* fs = &g_ext2_mounts[g_ext2_mount_count];
    if (ext2_fs_mount(fs, drive, part_lba) != 0) {
        return -1;
    }

    if (fs_mount_bindings(ext2_fs_backend_ops(), fs, drive, part_no, FS_KIND_EXT2) != 0) return -1;
    fs_register_mounted_partition(drive, part_lba, FS_KIND_EXT2);
    g_ext2_mount_count++;
    return 0;
}

static int fs_mount_ext4(uint8_t drive, uint64_t part_lba, uint8_t part_no) {
    if (fs_partition_already_mounted(drive, part_lba)) return -2;
    if (g_ext4_mount_count >= (sizeof(g_ext4_mounts) / sizeof(g_ext4_mounts[0]))) {
        return -1;
    }
    ext4_fs_t* fs = &g_ext4_mounts[g_ext4_mount_count];
    int rc = ext4_fs_mount(fs, drive, part_lba);
    if (rc != 0) {
        return rc;
    }

    if (fs_mount_bindings(ext4_fs_backend_ops(), fs, drive, part_no, FS_KIND_EXT4) != 0) return -1;
    fs_register_mounted_partition(drive, part_lba, FS_KIND_EXT4);
    g_ext4_mount_count++;
    return 0;
}

static int fs_mount_iso(uint8_t drive, uint64_t part_lba, uint8_t part_no) {
    if (fs_partition_already_mounted(drive, part_lba)) return -2;
    if (g_iso_mount_count >= (sizeof(g_iso_mounts) / sizeof(g_iso_mounts[0]))) {
        return -1;
    }
    iso_fs_t* fs = &g_iso_mounts[g_iso_mount_count];
    if (iso_fs_mount(fs, drive, part_lba) != 0) return -1;

    if (fs_mount_bindings(iso_fs_backend_ops(), fs, drive, part_no, FS_KIND_ISO) != 0) return -1;

    kprint_ok("FS: ISO mounted");
    fs_register_mounted_partition(drive, part_lba, FS_KIND_ISO);
    g_iso_mount_count++;
    return 0;
}

static int fs_guid_is_zero(const uint8_t guid[16]) {
    uint8_t acc = 0;
    for (size_t i = 0; i < 16; ++i) acc |= guid[i];
    return acc == 0;
}

static uint64_t fs_rd64_le(const uint8_t* p) {
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static uint8_t fs_gpt_type_to_mbr_compat(const uint8_t guid[16]) {
    static const uint8_t guid_efi_system[16] = {
        0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
        0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
    };
    static const uint8_t guid_ms_basic[16] = {
        0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
    };
    static const uint8_t guid_linux_fs[16] = {
        0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
        0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
    };

    if (memcmp(guid, guid_efi_system, 16) == 0) return 0xEF;
    if (memcmp(guid, guid_ms_basic, 16) == 0) return 0x0C;
    if (memcmp(guid, guid_linux_fs, 16) == 0) return 0x83;
    return 0;
}

static int fs_try_mount_partition(uint8_t drive, uint64_t lba, uint64_t sectors, uint8_t type, uint8_t part_no) {
    if (sectors == 0) return 0;
    if (fs_partition_already_mounted(drive, lba)) return 1;

    if (type == 0x01 || type == 0x04 || type == 0x06 || type == 0x0E || type == 0x0B || type == 0x0C || type == 0xEF) {
        return fs_mount_fat(drive, lba, part_no) == 0 ? 1 : 0;
    }
    if (type == 0x83) {
        int ext4_rc = fs_mount_ext4(drive, lba, part_no);
        if (ext4_rc == 0) return 1;
        return (fs_mount_ext2(drive, lba, part_no) == 0) ? 1 : 0;
    }
    if (type == 0x96) {
        return fs_mount_iso(drive, lba, part_no) == 0 ? 1 : 0;
    }

    if (fs_mount_fat(drive, lba, part_no) == 0) return 1;
    {
        int ext4_rc = fs_mount_ext4(drive, lba, part_no);
        if (ext4_rc == 0) return 1;
    }
    if (fs_mount_ext2(drive, lba, part_no) == 0) return 1;
    if (fs_mount_iso(drive, lba, part_no) == 0) return 1;
    return 0;
}

static int fs_read_gpt_entry(uint8_t drive, const gpt_header_t* hdr, uint32_t index, uint8_t out_entry[512]) {
    uint64_t entry_size = hdr->size_of_partition_entry;
    uint64_t off = (uint64_t)index * entry_size;
    uint64_t lba = hdr->partition_entry_lba + (off / ATA_SECTOR_SIZE);
    uint32_t sec_off = (uint32_t)(off % ATA_SECTOR_SIZE);
    uint8_t sec0[ATA_SECTOR_SIZE];

    if (entry_size == 0 || entry_size > 512) return -1;
    if (fs_device_read_sectors(drive, lba, 1, sec0) != 0) return -1;

    if ((uint64_t)sec_off + entry_size <= ATA_SECTOR_SIZE) {
        memset(out_entry, 0, 512);
        memcpy(out_entry, &sec0[sec_off], (size_t)entry_size);
        return 0;
    }

    uint8_t sec1[ATA_SECTOR_SIZE];
    if (fs_device_read_sectors(drive, lba + 1, 1, sec1) != 0) return -1;
    memset(out_entry, 0, 512);
    size_t first = ATA_SECTOR_SIZE - sec_off;
    memcpy(out_entry, &sec0[sec_off], first);
    memcpy(out_entry + first, sec1, (size_t)entry_size - first);
    return 0;
}

static int fs_probe_gpt_partitions(uint8_t drive, const gpt_header_t* hdr, uint64_t total, int* out_mounted_any) {
    int mounted_any = 0;
    uint32_t max_entries = hdr->number_of_partition_entries;
    if (max_entries > 256u) max_entries = 256u;

    for (uint32_t i = 0; i < max_entries; ++i) {
        uint8_t ent[512];
        if (fs_read_gpt_entry(drive, hdr, i, ent) != 0) break;

        const uint8_t* type_guid = &ent[0];
        if (fs_guid_is_zero(type_guid)) continue;

        uint64_t first_lba = fs_rd64_le(&ent[32]);
        uint64_t last_lba = fs_rd64_le(&ent[40]);
        if (first_lba == 0 || last_lba < first_lba) continue;
        uint64_t sectors = (last_lba - first_lba) + 1u;
        if (first_lba + sectors > total) continue;

        uint8_t part_no = (uint8_t)((i < 250u) ? (i + 1u) : 255u);
        uint8_t compat_type = fs_gpt_type_to_mbr_compat(type_guid);
        if (fs_try_mount_partition(drive, first_lba, sectors, compat_type, part_no)) mounted_any = 1;
    }

    if (out_mounted_any) *out_mounted_any = mounted_any;
    return 0;
}

static void fs_probe_partitions(uint8_t drive) {
    fs_block_dev_t dev;
    if (fs_get_block_device(drive, &dev) != 0 || !dev.present) {
        return;
    }

    if (dev.is_atapi) {
        (void)fs_mount_iso(drive, 0, 0);
        return;
    }

    uint8_t sec[ATA_SECTOR_SIZE];
    if (fs_device_read_sectors(drive, 0, 1, sec) != 0) {
        return;
    }

    uint64_t total = 0;
    if (fs_drive_total_sectors(drive, &total) != 0) return;

    const mbr_sector_t* mbr = (const mbr_sector_t*)sec;
    int has_mbr = (mbr->sig == 0xAA55);
    int mounted_any = 0;
    int has_gpt = 0;

    {
        uint8_t gpt_sec[ATA_SECTOR_SIZE];
        if (fs_device_read_sectors(drive, 1, 1, gpt_sec) == 0) {
            const gpt_header_t* gpt = (const gpt_header_t*)gpt_sec;
            if (memcmp(gpt->signature, "EFI PART", 8) == 0 &&
                gpt->header_size >= 92u &&
                gpt->header_size <= ATA_SECTOR_SIZE &&
                gpt->size_of_partition_entry >= 128u &&
                gpt->size_of_partition_entry <= 512u &&
                gpt->number_of_partition_entries > 0u &&
                gpt->partition_entry_lba != 0u) {
                has_gpt = 1;
                (void)fs_probe_gpt_partitions(drive, gpt, total, &mounted_any);
            }
        }
    }

    if (has_gpt) return;

    if (has_mbr) {
        for (int i = 0; i < 4; ++i) {
            uint8_t type = mbr->part[i].type;
            uint32_t lba = mbr->part[i].lba_first;
            uint32_t sectors = mbr->part[i].sectors;
            if (type == 0 || lba == 0 || sectors == 0) continue;
            if ((uint64_t)lba + sectors > total) continue;
            if (fs_is_extended_mbr_type(type)) continue;
            if (fs_try_mount_partition(drive, lba, sectors, type, (uint8_t)(i + 1))) mounted_any = 1;
        }

        /* Parse EBR chain so logical partitions are visible/mountable too. */
        for (int i = 0; i < 4; ++i) {
            uint8_t type = mbr->part[i].type;
            uint32_t ext_base = mbr->part[i].lba_first;
            if (!fs_is_extended_mbr_type(type) || ext_base == 0) continue;

            uint32_t ebr_lba = ext_base;
            uint32_t logical_idx = 0;
            for (int depth = 0; depth < 32; ++depth) {
                uint8_t ebr_sec[ATA_SECTOR_SIZE];
                if (fs_device_read_sectors(drive, ebr_lba, 1, ebr_sec) != 0) break;
                const mbr_sector_t* ebr = (const mbr_sector_t*)ebr_sec;
                if (ebr->sig != 0xAA55) break;

                const uint8_t p0_type = ebr->part[0].type;
                const uint32_t p0_rel = ebr->part[0].lba_first;
                const uint32_t p0_secs = ebr->part[0].sectors;
                if (p0_type != 0 && p0_rel != 0 && p0_secs != 0) {
                    uint64_t lba = (uint64_t)ebr_lba + p0_rel;
                    uint8_t pno = (uint8_t)((logical_idx < 250u) ? (5u + logical_idx) : 255u);
                    if ((uint64_t)lba + p0_secs <= total &&
                        fs_try_mount_partition(drive, lba, p0_secs, p0_type, pno)) mounted_any = 1;
                    logical_idx++;
                }

                const uint8_t p1_type = ebr->part[1].type;
                const uint32_t p1_rel = ebr->part[1].lba_first;
                if (!fs_is_extended_mbr_type(p1_type) || p1_rel == 0) break;
                uint32_t next_ebr = ext_base + p1_rel;
                if (next_ebr == ebr_lba || next_ebr >= total) break;
                ebr_lba = next_ebr;
            }
        }
    }

    if (!mounted_any) {
        if (fs_mount_fat(drive, 0, 0) == 0) return;
        {
            int ext4_rc = fs_mount_ext4(drive, 0, 0);
            if (ext4_rc == 0) return;
        }
        if (fs_mount_ext2(drive, 0, 0) == 0) return;
        (void)fs_mount_iso(drive, 0, 0);
    }
}

static int fs_split_path(const char* full, char* parent, size_t parent_cap, char* name, size_t name_cap) {
    if (!full || full[0] != '/') return -1;
    const char* slash = strrchr(full, '/');
    if (!slash) return -1;
    if (!slash[1]) return -1;

    size_t nlen = strlen(slash + 1);
    if (nlen + 1 > name_cap) return -1;
    memcpy(name, slash + 1, nlen + 1);

    if (slash == full) {
        if (parent_cap < 2) return -1;
        parent[0] = '/';
        parent[1] = '\0';
        return 0;
    }

    size_t plen = (size_t)(slash - full);
    if (plen + 1 > parent_cap) return -1;
    memcpy(parent, full, plen);
    parent[plen] = '\0';
    return 0;
}

static int fs_build_ramfs_path(int node_idx, char out[VFS_MAX_PATH]) {
    if (node_idx < 0 || node_idx >= RAMFS_MAX_NODES) return -1;
    if (!g_root_ramfs.nodes[node_idx].used) return -1;
    if (node_idx == g_root_ramfs.root_index) {
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    int chain[RAMFS_MAX_NODES];
    int depth = 0;
    int cur = node_idx;
    while (cur != g_root_ramfs.root_index && depth < RAMFS_MAX_NODES) {
        chain[depth++] = cur;
        int parent = g_root_ramfs.nodes[cur].parent;
        if (parent < 0 || parent >= RAMFS_MAX_NODES || !g_root_ramfs.nodes[parent].used) return -1;
        cur = parent;
    }
    if (depth <= 0) return -1;

    size_t pos = 0;
    out[pos++] = '/';
    for (int i = depth - 1; i >= 0; --i) {
        const char* n = g_root_ramfs.nodes[chain[i]].name;
        size_t nl = strlen(n);
        if (nl == 0) return -1;
        if (pos + nl + 1 >= VFS_MAX_PATH) return -1;
        memcpy(&out[pos], n, nl);
        pos += nl;
        if (i > 0) out[pos++] = '/';
    }
    out[pos] = '\0';
    return 0;
}

static int fs_persist_region(uint8_t* out_drive, uint64_t* out_lba_start, uint32_t* out_sector_count) {
    fs_block_dev_t d;
    if (fs_get_block_device(0, &d) != 0 || !d.present || d.is_atapi) return -1;
    uint64_t sectors = d.sectors;
    if (sectors <= (uint64_t)FS_PERSIST_SECTORS + 4096u) return -1;

    *out_drive = 0;
    *out_sector_count = FS_PERSIST_SECTORS;
    *out_lba_start = sectors - FS_PERSIST_SECTORS;
    return 0;
}

static int fs_persist_write_sectors(uint8_t drive, uint64_t lba, const uint8_t* data, uint32_t sectors) {
    return fs_device_write_sectors(drive, lba, sectors, data);
}

static int fs_persist_read_sectors(uint8_t drive, uint64_t lba, uint8_t* data, uint32_t sectors) {
    return fs_device_read_sectors(drive, lba, sectors, data);
}

static void fs_persist_save_ramfs(void) {
    if (!g_fs_persist_enabled) return;
    if (g_fs_persist_busy) return;
    if (fs_block_device_count() == 0) return;

    uint8_t drive;
    uint64_t lba_start;
    uint32_t sector_count;
    if (fs_persist_region(&drive, &lba_start, &sector_count) != 0) return;

    static uint8_t blob[FS_PERSIST_SECTORS * ATA_SECTOR_SIZE];
    memset(blob, 0, sizeof(blob));

    fs_persist_header_t* hdr = (fs_persist_header_t*)blob;
    size_t max = sizeof(blob);
    size_t wr = sizeof(fs_persist_header_t);
    size_t entry_count = 0;
    size_t data_off = max;

    for (int i = 0; i < RAMFS_MAX_NODES; ++i) {
        const ramfs_node_t* n = &g_root_ramfs.nodes[i];
        if (!n->used || i == g_root_ramfs.root_index) continue;

        char path[VFS_MAX_PATH];
        if (fs_build_ramfs_path(i, path) != 0) continue;
        size_t path_len = strlen(path) + 1;

        size_t data_len = (n->type == RAMFS_NODE_FILE) ? n->size : 0;
        if (data_len > data_off) continue;
        if (wr + sizeof(fs_persist_entry_t) + path_len > data_off) break;
        if (data_off < data_len || (data_off - data_len) < wr + sizeof(fs_persist_entry_t) + path_len) break;

        data_off -= data_len;
        if (data_len > 0) memcpy(&blob[data_off], n->data, data_len);

        fs_persist_entry_t ent;
        memset(&ent, 0, sizeof(ent));
        ent.type = (n->type == RAMFS_NODE_DIR) ? 1 : 2;
        ent.path_len = (uint32_t)path_len;
        ent.data_len = (uint32_t)data_len;
        ent.data_off = (uint32_t)data_off;

        memcpy(&blob[wr], &ent, sizeof(ent));
        wr += sizeof(ent);
        memcpy(&blob[wr], path, path_len);
        wr += path_len;
        entry_count++;
    }

    hdr->magic = FS_PERSIST_MAGIC;
    hdr->version = 1;
    hdr->entry_count = (uint32_t)entry_count;
    hdr->data_bytes = (uint32_t)(max - data_off);
    hdr->total_bytes = (uint32_t)max;

    (void)fs_persist_write_sectors(drive, lba_start, blob, sector_count);
}

static void fs_persist_ensure_dirs(const char* dir_path) {
    if (!dir_path || dir_path[0] != '/') return;
    if (dir_path[1] == '\0') return;

    char tmp[VFS_MAX_PATH];
    strncpy(tmp, dir_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char built[VFS_MAX_PATH];
    built[0] = '/';
    built[1] = '\0';

    const char* p = tmp;
    while (*p == '/') ++p;
    while (*p) {
        char token[VFS_MAX_NAME];
        size_t n = 0;
        while (p[n] && p[n] != '/' && n + 1 < sizeof(token)) {
            token[n] = p[n];
            ++n;
        }
        token[n] = '\0';
        p += n;
        while (*p == '/') ++p;

        if (token[0] == '\0') continue;
        if (strcmp(built, "/") == 0) {
            strncat(built, token, sizeof(built));
        } else {
            strncat(built, "/", sizeof(built));
            strncat(built, token, sizeof(built));
        }

        char parent[VFS_MAX_PATH];
        char name[VFS_MAX_NAME];
        if (fs_split_path(built, parent, sizeof(parent), name, sizeof(name)) == 0) {
            (void)fs_mkdir(parent, name);
        }
    }
}

static void fs_persist_load_ramfs(void) {
    if (!g_fs_persist_enabled) return;
    if (g_fs_persist_busy) return;
    if (fs_block_device_count() == 0) return;

    uint8_t drive;
    uint64_t lba_start;
    uint32_t sector_count;
    if (fs_persist_region(&drive, &lba_start, &sector_count) != 0) return;

    static uint8_t blob[FS_PERSIST_SECTORS * ATA_SECTOR_SIZE];
    if (fs_persist_read_sectors(drive, lba_start, blob, sector_count) != 0) return;

    fs_persist_header_t* hdr = (fs_persist_header_t*)blob;
    if (hdr->magic != FS_PERSIST_MAGIC || hdr->version != 1 || hdr->entry_count > RAMFS_MAX_NODES) return;

    g_fs_persist_busy = 1;
    size_t rd = sizeof(fs_persist_header_t);
    size_t max = sizeof(blob);
    for (uint32_t i = 0; i < hdr->entry_count; ++i) {
        if (rd + sizeof(fs_persist_entry_t) > max) break;
        fs_persist_entry_t ent;
        memcpy(&ent, &blob[rd], sizeof(ent));
        rd += sizeof(ent);

        if (ent.path_len == 0 || ent.path_len >= VFS_MAX_PATH) break;
        if (rd + ent.path_len > max) break;

        char path[VFS_MAX_PATH];
        memcpy(path, &blob[rd], ent.path_len);
        path[ent.path_len - 1] = '\0';
        rd += ent.path_len;

        char parent[VFS_MAX_PATH];
        char name[VFS_MAX_NAME];
        if (fs_split_path(path, parent, sizeof(parent), name, sizeof(name)) != 0) continue;
        fs_persist_ensure_dirs(parent);

        if (ent.type == 1) {
            (void)fs_mkdir(parent, name);
        } else if (ent.type == 2) {
            if ((size_t)ent.data_off + (size_t)ent.data_len > max) continue;
            (void)fs_create_file(parent, name, &blob[ent.data_off], ent.data_len);
        }
    }
    g_fs_persist_busy = 0;
}

static int fs_build_child_path(const char* path, const char* name, char out[VFS_MAX_PATH]) {
    if (path == NULL || name == NULL || path[0] == '\0' || path[0] != '/') {
        return -1;
    }

    size_t p = strlen(path);
    size_t n = strlen(name);
    if (n == 0) {
        return -2;
    }

    if (p + 1 + n + 1 > VFS_MAX_PATH) {
        return -3;
    }

    size_t idx = 0;
    for (size_t i = 0; i < p; ++i) {
        out[idx++] = path[i];
    }

    if (idx > 0 && out[idx - 1] != '/') {
        out[idx++] = '/';
    }

    for (size_t i = 0; i < n; ++i) {
        out[idx++] = name[i];
    }
    out[idx] = '\0';
    return 0;
}

static int fs_should_persist_path(const char* path) {
    if (!g_fs_persist_enabled || g_fs_persist_busy) return 0;
    const vfs_backend_ops_t* ops = NULL;
    if (vfs_get_mount(path, &ops, NULL, NULL) != 0) return 0;
    return (ops == ramfs_backend_ops());
}

void fs_init(void) {
    vfs_init();
    ramfs_init(&g_root_ramfs);
    memset(g_fat_mounts, 0, sizeof(g_fat_mounts));
    memset(g_ext2_mounts, 0, sizeof(g_ext2_mounts));
    memset(g_ext4_mounts, 0, sizeof(g_ext4_mounts));
    memset(g_iso_mounts, 0, sizeof(g_iso_mounts));
    g_fat_mount_count = 0;
    g_ext2_mount_count = 0;
    g_ext4_mount_count = 0;
    g_iso_mount_count = 0;
    g_mounted_part_count = 0;
    g_mount_generation = 0;
    g_fs_persist_enabled = 0;
    g_root_score = 0;

    if (vfs_mount("/", ramfs_backend_ops(), &g_root_ramfs) != 0) {
        kprint_error("FS: mount / failed");
        return;
    }
    devfs_init();
    (void)vfs_mount("/dev", devfs_backend_ops(), NULL);

    size_t drives = fs_block_device_count();
    for (size_t i = 0; i < drives; ++i) {
        fs_probe_partitions((uint8_t)i);
    }
    g_fs_persist_enabled = (g_root_score == 0 &&
                            g_fat_mount_count == 0 && g_ext2_mount_count == 0 &&
                            g_ext4_mount_count == 0 && g_iso_mount_count == 0) ? 1 : 0;
    if (g_fs_persist_enabled) {
        fs_persist_load_ramfs();
    }
    fs_ensure_linux_dirs();

    kprint_ok("FS: VFS + RAMFS initialized");
}

void fs_rescan_storage(void) {
    ata_rescan(false);
    nvme_rescan(false);
    sdmmc_rescan(false);
    size_t drives = fs_block_device_count();
    for (size_t i = 0; i < drives; ++i) {
        fs_probe_partitions((uint8_t)i);
    }
    fs_ensure_linux_dirs();
}

uint32_t fs_mount_generation(void) {
    return g_mount_generation;
}

int fs_mkdir(const char* path, const char* name) {
    char full[VFS_MAX_PATH];
    if (fs_build_child_path(path, name, full) != 0) {
        return -1;
    }
    int rc = vfs_mkdir(full);
    if (rc == 0 && fs_should_persist_path(full)) fs_persist_save_ramfs();
    return rc;
}

int fs_create_file(const char* path, const char* name, const void* data, size_t len) {
    char full[VFS_MAX_PATH];
    if (fs_build_child_path(path, name, full) != 0) {
        return -1;
    }
    int rc = vfs_create_file(full, data, len);
    if (rc == 0 && fs_should_persist_path(full)) fs_persist_save_ramfs();
    return rc;
}

int fs_write_file(const char* path, const void* data, size_t len) {
    int rc = vfs_write_file(path, data, len);
    if (rc == 0 && fs_should_persist_path(path)) fs_persist_save_ramfs();
    return rc;
}

int fs_read_file(const char* path, void* out, size_t out_cap, size_t* out_len) {
    return vfs_read_file(path, out, out_cap, out_len);
}

int fs_read_file_range(const char* path, size_t offset, void* out, size_t len, size_t* out_read, size_t* out_file_len) {
    if (!path || path[0] != '/' || !out) return -1;

    if (strncmp(path, "/iso", 4) == 0 && path[4] >= '0' && path[4] <= '7' && (path[5] == '\0' || path[5] == '/')) {
        size_t idx = (size_t)(path[4] - '0');
        if (idx >= g_iso_mount_count) return -1;
        if (!g_iso_mounts[idx].mounted) return -1;
        const char* rel = (path[5] == '\0') ? "/" : &path[5];
        return iso_fs_read_file_range(&g_iso_mounts[idx], rel, offset, out, len, out_read, out_file_len);
    }

    return -1;
}

int fs_copy_file_fast(const char* src, const char* dst) {
    if (!src || !dst || src[0] != '/' || dst[0] != '/') return -1;

    const vfs_backend_ops_t* src_ops = NULL;
    const vfs_backend_ops_t* dst_ops = NULL;
    void* src_ctx = NULL;
    void* dst_ctx = NULL;
    const char* src_rel = NULL;
    const char* dst_rel = NULL;
    if (vfs_get_mount(src, &src_ops, &src_ctx, &src_rel) != 0) return -1;
    if (vfs_get_mount(dst, &dst_ops, &dst_ctx, &dst_rel) != 0) return -1;

    if (src_ops == iso_fs_backend_ops() && dst_ops == fat_fs_backend_ops()) {
        uint8_t dummy = 0;
        size_t file_len = 0;
        if (iso_fs_read_file_range((const iso_fs_t*)src_ctx, src_rel, 0, &dummy, 0, NULL, &file_len) != 0) {
            return -1;
        }

        if (vfs_exists(dst)) {
            (void)vfs_remove(dst);
        }

        iso_reader_ctx_t rctx;
        rctx.fs = (const iso_fs_t*)src_ctx;
        rctx.path = src_rel;
        int rc = fat_fs_create_file_from_reader((fat_fs_t*)dst_ctx, dst_rel, file_len, iso_reader_cb, &rctx);
        if (rc == 0 && fs_should_persist_path(dst) && !g_fs_persist_busy) fs_persist_save_ramfs();
        return rc;
    }

    if (src_ops == iso_fs_backend_ops() &&
        (dst_ops == ext2_fs_backend_ops() || dst_ops == ext4_fs_backend_ops())) {
        uint8_t dummy = 0;
        size_t file_len = 0;
        if (iso_fs_read_file_range((const iso_fs_t*)src_ctx, src_rel, 0, &dummy, 0, NULL, &file_len) != 0) {
            return -1;
        }

        if (vfs_exists(dst)) {
            (void)vfs_remove(dst);
        }

        iso_reader_ctx_t rctx;
        rctx.fs = (const iso_fs_t*)src_ctx;
        rctx.path = src_rel;

        int rc = -1;
        if (dst_ops == ext2_fs_backend_ops()) {
            rc = ext2_fs_create_file_from_reader((ext2_fs_t*)dst_ctx, dst_rel, file_len, iso_reader_cb, &rctx);
        } else {
            rc = ext4_fs_create_file_from_reader((ext4_fs_t*)dst_ctx, dst_rel, file_len, iso_reader_cb, &rctx);
        }
        if (rc == 0 && fs_should_persist_path(dst) && !g_fs_persist_busy) fs_persist_save_ramfs();
        return rc;
    }

    if (src_ops == ramfs_backend_ops() && dst_ops == fat_fs_backend_ops()) {
        const uint8_t* data = NULL;
        size_t file_len = 0;
        if (ramfs_get_file_view((ramfs_t*)src_ctx, src_rel, &data, &file_len) != 0) {
            return -1;
        }
        if (vfs_exists(dst)) {
            (void)vfs_remove(dst);
        }
        mem_reader_ctx_t rctx = { data, file_len };
        int rc = fat_fs_create_file_from_reader((fat_fs_t*)dst_ctx, dst_rel, file_len, mem_reader_cb, &rctx);
        if (rc == 0 && fs_should_persist_path(dst) && !g_fs_persist_busy) fs_persist_save_ramfs();
        return rc;
    }

    if (src_ops == ramfs_backend_ops() &&
        (dst_ops == ext2_fs_backend_ops() || dst_ops == ext4_fs_backend_ops())) {
        const uint8_t* data = NULL;
        size_t file_len = 0;
        if (ramfs_get_file_view((ramfs_t*)src_ctx, src_rel, &data, &file_len) != 0) {
            return -1;
        }
        if (vfs_exists(dst)) {
            (void)vfs_remove(dst);
        }
        mem_reader_ctx_t rctx = { data, file_len };
        int rc = -1;
        if (dst_ops == ext2_fs_backend_ops()) {
            rc = ext2_fs_create_file_from_reader((ext2_fs_t*)dst_ctx, dst_rel, file_len, mem_reader_cb, &rctx);
        } else {
            rc = ext4_fs_create_file_from_reader((ext4_fs_t*)dst_ctx, dst_rel, file_len, mem_reader_cb, &rctx);
        }
        if (rc == 0 && fs_should_persist_path(dst) && !g_fs_persist_busy) fs_persist_save_ramfs();
        return rc;
    }

    if (src_ops == fat_fs_backend_ops() &&
        (dst_ops == fat_fs_backend_ops() || dst_ops == ext2_fs_backend_ops() || dst_ops == ext4_fs_backend_ops())) {
        uint8_t dummy = 0;
        size_t file_len = 0;
        if (fat_fs_read_file_range((fat_fs_t*)src_ctx, src_rel, 0, &dummy, 0, NULL, &file_len) != 0) {
            return -1;
        }
        if (vfs_exists(dst)) {
            (void)vfs_remove(dst);
        }

        fat_reader_ctx_t rctx;
        rctx.fs = (fat_fs_t*)src_ctx;
        rctx.path = src_rel;

        int rc = -1;
        if (dst_ops == fat_fs_backend_ops()) {
            rc = fat_fs_create_file_from_reader((fat_fs_t*)dst_ctx, dst_rel, file_len, fat_reader_cb, &rctx);
        } else if (dst_ops == ext2_fs_backend_ops()) {
            rc = ext2_fs_create_file_from_reader((ext2_fs_t*)dst_ctx, dst_rel, file_len, fat_reader_cb, &rctx);
        } else {
            rc = ext4_fs_create_file_from_reader((ext4_fs_t*)dst_ctx, dst_rel, file_len, fat_reader_cb, &rctx);
        }
        if (rc == 0 && fs_should_persist_path(dst) && !g_fs_persist_busy) fs_persist_save_ramfs();
        return rc;
    }

    return -1;
}

int fs_list_dir(const char* path, fs_dirent_t* out, size_t max_entries, size_t* out_count) {
    return vfs_list_dir(path, out, max_entries, out_count);
}

bool fs_exists(const char* path) {
    return vfs_exists(path) != 0;
}

int fs_remove(const char* path) {
    if (!path || path[0] != '/') return -1;
    int rc = vfs_remove(path);
    if (rc == 0 && fs_should_persist_path(path)) fs_persist_save_ramfs();
    return rc;
}

int fs_rename(const char* old_path, const char* new_name) {
    if (!old_path || old_path[0] != '/' || !new_name || new_name[0] == '\0') return -1;

    char parent[VFS_MAX_PATH];
    char old_leaf[VFS_MAX_NAME];
    if (fs_split_path(old_path, parent, sizeof(parent), old_leaf, sizeof(old_leaf)) != 0) return -1;

    char new_path[VFS_MAX_PATH];
    if (fs_build_child_path(parent, new_name, new_path) != 0) return -1;
    int rc = vfs_rename(old_path, new_path);
    if (rc == 0 && fs_should_persist_path(old_path)) fs_persist_save_ramfs();
    return rc;
}

size_t fs_get_block_devices(fs_block_device_info_t* out, size_t max_entries) {
    size_t total = fs_block_device_count();
    for (size_t i = 0; i < total && out && i < max_entries; ++i) {
        fs_block_dev_t d;
        memset(&out[i], 0, sizeof(out[i]));
        if (fs_get_block_device((uint8_t)i, &d) != 0) continue;
        out[i].present = d.present ? 1u : 0u;
        out[i].is_atapi = d.is_atapi ? 1u : 0u;
        out[i].sectors = d.sectors;
        memcpy(out[i].model, d.model, sizeof(out[i].model) - 1u);
        out[i].model[sizeof(out[i].model) - 1u] = '\0';
    }
    return total;
}

size_t fs_list_partitions(uint8_t drive, fs_partition_info_t* out, size_t max_entries) {
    fs_block_dev_t d;
    uint8_t sec[ATA_SECTOR_SIZE];
    if (fs_get_block_device(drive, &d) != 0 || !d.present || d.is_atapi) return 0;
    if (fs_device_read_sectors(drive, 0, 1, sec) != 0) return 0;

    const mbr_sector_t* mbr = (const mbr_sector_t*)sec;
    int has_mbr = (mbr->sig == 0xAA55);

    uint64_t total = 0;
    if (fs_drive_total_sectors(drive, &total) != 0) return 0;

    size_t count = 0;

    {
        uint8_t gpt_sec[ATA_SECTOR_SIZE];
        if (fs_device_read_sectors(drive, 1, 1, gpt_sec) == 0) {
            const gpt_header_t* gpt = (const gpt_header_t*)gpt_sec;
            if (memcmp(gpt->signature, "EFI PART", 8) == 0 &&
                gpt->header_size >= 92u &&
                gpt->header_size <= ATA_SECTOR_SIZE &&
                gpt->size_of_partition_entry >= 128u &&
                gpt->size_of_partition_entry <= 512u &&
                gpt->number_of_partition_entries > 0u &&
                gpt->partition_entry_lba != 0u) {
                uint32_t max_entries_scan = gpt->number_of_partition_entries;
                if (max_entries_scan > 256u) max_entries_scan = 256u;
                for (uint32_t i = 0; i < max_entries_scan; ++i) {
                    uint8_t ent[512];
                    if (fs_read_gpt_entry(drive, gpt, i, ent) != 0) break;
                    if (fs_guid_is_zero(&ent[0])) continue;

                    uint64_t first_lba = fs_rd64_le(&ent[32]);
                    uint64_t last_lba = fs_rd64_le(&ent[40]);
                    if (first_lba == 0 || last_lba < first_lba) continue;
                    uint64_t p_secs = (last_lba - first_lba) + 1u;
                    if (first_lba + p_secs > total) continue;

                    if (out && count < max_entries) {
                        out[count].index = (uint8_t)((i < 250u) ? (i + 1u) : 255u);
                        out[count].type = fs_gpt_type_to_mbr_compat(&ent[0]);
                        out[count].bootable = 0;
                        out[count]._pad = 0;
                        out[count].lba_start = first_lba;
                        out[count].sectors = p_secs;
                    }
                    count++;
                }
                return count;
            }
        }
    }

    if (has_mbr) {
        for (int i = 0; i < 4; ++i) {
            uint8_t type = mbr->part[i].type;
            uint32_t lba = mbr->part[i].lba_first;
            uint32_t sectors = mbr->part[i].sectors;
            if (type == 0 || lba == 0 || sectors == 0) continue;
            if ((uint64_t)lba + sectors > total) continue;
            if (fs_is_extended_mbr_type(type)) continue;
            if (out && count < max_entries) {
                out[count].index = (uint8_t)(i + 1);
                out[count].type = type;
                out[count].bootable = (mbr->part[i].status == 0x80u) ? 1u : 0u;
                out[count]._pad = 0;
                out[count].lba_start = (uint64_t)lba;
                out[count].sectors = (uint64_t)sectors;
            }
            count++;
        }

        for (int i = 0; i < 4; ++i) {
            uint8_t type = mbr->part[i].type;
            uint32_t ext_base = mbr->part[i].lba_first;
            if (!fs_is_extended_mbr_type(type) || ext_base == 0) continue;

            uint32_t ebr_lba = ext_base;
            uint32_t logical_idx = 0;
            for (int depth = 0; depth < 32; ++depth) {
                uint8_t ebr_sec[ATA_SECTOR_SIZE];
                if (fs_device_read_sectors(drive, ebr_lba, 1, ebr_sec) != 0) break;
                const mbr_sector_t* ebr = (const mbr_sector_t*)ebr_sec;
                if (ebr->sig != 0xAA55) break;

                uint8_t p_type = ebr->part[0].type;
                uint32_t p_rel = ebr->part[0].lba_first;
                uint32_t p_secs = ebr->part[0].sectors;
                if (p_type != 0 && p_rel != 0 && p_secs != 0) {
                    uint32_t p_lba = ebr_lba + p_rel;
                    if ((uint64_t)p_lba + p_secs <= total) {
                        if (out && count < max_entries) {
                            out[count].index = (uint8_t)((logical_idx < 250u) ? (5u + logical_idx) : 255u);
                            out[count].type = p_type;
                            out[count].bootable = (ebr->part[0].status == 0x80u) ? 1u : 0u;
                            out[count]._pad = 0;
                            out[count].lba_start = (uint64_t)p_lba;
                            out[count].sectors = (uint64_t)p_secs;
                        }
                        count++;
                    }
                    logical_idx++;
                }

                uint8_t next_type = ebr->part[1].type;
                uint32_t next_rel = ebr->part[1].lba_first;
                if (!fs_is_extended_mbr_type(next_type) || next_rel == 0) break;
                uint32_t next_ebr = ext_base + next_rel;
                if (next_ebr == ebr_lba || next_ebr >= total) break;
                ebr_lba = next_ebr;
            }
        }
    }
    return count;
}

static int fs_drive_total_sectors(uint8_t drive, uint64_t* out_total) {
    fs_block_dev_t d;
    if (!out_total || fs_get_block_device(drive, &d) != 0 || !d.present) return -1;
    uint64_t total = d.sectors;
    if (total == 0) return -1;
    *out_total = total;
    return 0;
}

int fs_set_mbr_partition(uint8_t drive, uint8_t part_index, uint32_t lba_start, uint32_t sectors, uint8_t type, uint8_t bootable) {
    uint64_t total = 0;
    uint8_t sec[ATA_SECTOR_SIZE];
    if (part_index < 1 || part_index > 4) return -1;
    if (fs_drive_total_sectors(drive, &total) != 0) return -1;
    if (lba_start > total || sectors > total || ((uint64_t)lba_start + (uint64_t)sectors) > total) return -1;
    if ((type == 0 && sectors != 0) || (type != 0 && sectors == 0)) return -1;

    fs_block_dev_t d;
    if (fs_get_block_device(drive, &d) != 0 || !d.present || d.is_atapi) return -1;
    if (fs_device_read_sectors(drive, 0, 1, sec) != 0) return -1;
    mbr_sector_t* mbr = (mbr_sector_t*)sec;
    if (mbr->sig != 0xAA55) {
        memset(mbr, 0, sizeof(*mbr));
        mbr->sig = 0xAA55;
    }

    size_t i = (size_t)(part_index - 1u);
    mbr->part[i].status = (type != 0 && bootable) ? 0x80u : 0x00u;
    mbr->part[i].chs_first[0] = 0;
    mbr->part[i].chs_first[1] = 0;
    mbr->part[i].chs_first[2] = 0;
    mbr->part[i].type = type;
    mbr->part[i].chs_last[0] = 0;
    mbr->part[i].chs_last[1] = 0;
    mbr->part[i].chs_last[2] = 0;
    mbr->part[i].lba_first = (type == 0) ? 0u : lba_start;
    mbr->part[i].sectors = (type == 0) ? 0u : sectors;

    return fs_device_write_sectors(drive, 0, 1, sec);
}

int fs_block_read(uint8_t drive, uint64_t lba, uint32_t sector_count, void* out) {
    uint64_t total = 0;
    if (!out || sector_count == 0) return -1;
    if (fs_drive_total_sectors(drive, &total) != 0) return -1;
    if (lba >= total || sector_count > total || lba + (uint64_t)sector_count > total) return -1;
    int rc = fs_device_read_sectors(drive, lba, sector_count, out);
    if (rc == 0) {
        g_fs_read_bytes += (uint64_t)sector_count * 512ull;
    }
    return rc;
}

int fs_block_write(uint8_t drive, uint64_t lba, uint32_t sector_count, const void* in) {
    uint64_t total = 0;
    if (!in || sector_count == 0) return -1;
    if (fs_drive_total_sectors(drive, &total) != 0) return -1;
    if (lba >= total || sector_count > total || lba + (uint64_t)sector_count > total) return -1;
    int rc = fs_device_write_sectors(drive, lba, sector_count, in);
    if (rc == 0) {
        g_fs_write_bytes += (uint64_t)sector_count * 512ull;
    }
    return rc;
}

void fs_get_io_stats(uint64_t* out_read_bytes, uint64_t* out_write_bytes) {
    if (out_read_bytes) *out_read_bytes = g_fs_read_bytes;
    if (out_write_bytes) *out_write_bytes = g_fs_write_bytes;
}

typedef struct __attribute__((packed)) {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t r_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev_level;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;
    uint32_t first_ino;
    uint16_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t uuid[16];
    char volume_name[16];
    char last_mounted[64];
    uint32_t algo_bitmap;
    uint8_t prealloc_blocks;
    uint8_t prealloc_dir_blocks;
    uint16_t reserved_gdt_blocks;
    uint8_t journal_uuid[16];
    uint32_t journal_inum;
    uint32_t journal_dev;
    uint32_t last_orphan;
} fs_ext_super_t;

typedef struct __attribute__((packed)) {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint8_t reserved[12];
} fs_ext_bg_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[15];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t faddr;
    uint8_t osd2[12];
} fs_ext_inode_t;

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} fs_ext_dirent_t;

typedef struct __attribute__((packed)) {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} fs_ext_extent_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} fs_ext_extent_t;

static void fs_set_bit(uint8_t* map, uint32_t bit) {
    map[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
}

static int fs_write_block_4k(uint8_t drive, uint32_t part_lba, uint32_t block, const void* data) {
    return fs_block_write(drive, (uint64_t)part_lba + (uint64_t)block * 8u, 8u, data);
}

static int fs_mkfs_ext_like(uint8_t drive, uint32_t lba_start, uint32_t sectors, int ext4_mode) {
    uint64_t total = 0;
    uint32_t block_size = 4096u;
    uint32_t blocks = sectors / (block_size / ATA_SECTOR_SIZE);
    uint32_t inodes = 4096u;
    uint32_t inode_table_blocks;
    uint32_t root_block;
    uint32_t used_blocks;
    uint32_t free_blocks;
    uint32_t used_inodes;
    uint32_t free_inodes;
    if (fs_drive_total_sectors(drive, &total) != 0) return -1;
    if (sectors < 4096u || ((uint64_t)lba_start + sectors) > total) return -1;
    if (blocks < 512u || blocks > 32768u) return -1;

    if (inodes > blocks * 2u) inodes = blocks * 2u;
    if (inodes < 256u) inodes = 256u;
    inode_table_blocks = (uint32_t)(((uint64_t)inodes * sizeof(fs_ext_inode_t) + (uint64_t)block_size - 1u) / (uint64_t)block_size);
    root_block = 4u + inode_table_blocks;
    used_blocks = root_block + 1u;
    if (used_blocks >= blocks) return -1;

    free_blocks = blocks - used_blocks;
    used_inodes = 10u; /* reserve 1..10; inode 2 is root */
    if (inodes <= used_inodes) return -1;
    free_inodes = inodes - used_inodes;

    uint8_t zero4k[4096];
    uint8_t map4k[4096];
    uint8_t itbl4k[4096];
    memset(zero4k, 0, sizeof(zero4k));
    memset(map4k, 0, sizeof(map4k));
    memset(itbl4k, 0, sizeof(itbl4k));

    for (uint32_t i = 0; i < used_blocks; ++i) fs_set_bit(map4k, i);
    for (uint32_t i = 0; i < used_inodes; ++i) fs_set_bit(itbl4k, i);

    for (uint32_t i = 0; i < used_blocks; ++i) {
        if (fs_write_block_4k(drive, lba_start, i, zero4k) != 0) return -1;
    }

    fs_ext_super_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.inodes_count = inodes;
    sb.blocks_count = blocks;
    sb.r_blocks_count = 0;
    sb.free_blocks_count = free_blocks;
    sb.free_inodes_count = free_inodes;
    sb.first_data_block = 0;
    sb.log_block_size = 2;
    sb.log_frag_size = 2;
    sb.blocks_per_group = blocks;
    sb.frags_per_group = blocks;
    sb.inodes_per_group = inodes;
    sb.magic = 0xEF53;
    sb.state = 1;
    sb.errors = 1;
    sb.rev_level = 1;
    sb.first_ino = 11;
    sb.inode_size = (uint16_t)sizeof(fs_ext_inode_t);
    sb.feature_incompat = 0x0002u; /* filetype */
    if (ext4_mode) {
        sb.feature_incompat |= 0x0040u; /* extents */
    }

    uint8_t sb_sec[ATA_SECTOR_SIZE];
    memset(sb_sec, 0, sizeof(sb_sec));
    memcpy(sb_sec, &sb, sizeof(sb));
    if (fs_block_write(drive, (uint64_t)lba_start + 2u, 1u, sb_sec) != 0) return -1;

    fs_ext_bg_desc_t bg;
    memset(&bg, 0, sizeof(bg));
    bg.block_bitmap = 2;
    bg.inode_bitmap = 3;
    bg.inode_table = 4;
    bg.free_blocks_count = (uint16_t)free_blocks;
    bg.free_inodes_count = (uint16_t)free_inodes;
    bg.used_dirs_count = 1;

    uint8_t bgdt4k[4096];
    memset(bgdt4k, 0, sizeof(bgdt4k));
    memcpy(bgdt4k, &bg, sizeof(bg));
    if (fs_write_block_4k(drive, lba_start, 1u, bgdt4k) != 0) return -1;
    if (fs_write_block_4k(drive, lba_start, 2u, map4k) != 0) return -1;
    if (fs_write_block_4k(drive, lba_start, 3u, itbl4k) != 0) return -1;

    fs_ext_inode_t root;
    memset(&root, 0, sizeof(root));
    root.mode = (uint16_t)(0x4000u | 0755u);
    root.links_count = 2;
    root.size = block_size;
    root.blocks = block_size / 512u;
    if (ext4_mode) {
        root.flags = 0x00080000u;
        fs_ext_extent_hdr_t* eh = (fs_ext_extent_hdr_t*)root.block;
        fs_ext_extent_t* ex = (fs_ext_extent_t*)(eh + 1);
        eh->eh_magic = 0xF30Au;
        eh->eh_entries = 1;
        eh->eh_max = 4;
        eh->eh_depth = 0;
        ex->ee_block = 0;
        ex->ee_len = 1;
        ex->ee_start_hi = 0;
        ex->ee_start_lo = root_block;
    } else {
        root.block[0] = root_block;
    }

    uint8_t inode_blk[4096];
    memset(inode_blk, 0, sizeof(inode_blk));
    memcpy(inode_blk + sizeof(fs_ext_inode_t), &root, sizeof(root)); /* inode #2 */
    if (fs_write_block_4k(drive, lba_start, 4u, inode_blk) != 0) return -1;

    uint8_t root_dir[4096];
    memset(root_dir, 0, sizeof(root_dir));
    fs_ext_dirent_t* dot = (fs_ext_dirent_t*)&root_dir[0];
    dot->inode = 2;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = 2;
    dot->name[0] = '.';
    fs_ext_dirent_t* dotdot = (fs_ext_dirent_t*)&root_dir[12];
    dotdot->inode = 2;
    dotdot->rec_len = (uint16_t)(block_size - 12u);
    dotdot->name_len = 2;
    dotdot->file_type = 2;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    if (fs_write_block_4k(drive, lba_start, root_block, root_dir) != 0) return -1;

    return 0;
}

int fs_mkfs_ext2(uint8_t drive, uint32_t lba_start, uint32_t sectors) {
    return fs_mkfs_ext_like(drive, lba_start, sectors, 0);
}

int fs_mkfs_ext4(uint8_t drive, uint32_t lba_start, uint32_t sectors) {
    return fs_mkfs_ext_like(drive, lba_start, sectors, 1);
}

static void fs_wr16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void fs_wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t fs_fat_spf_from_clusters(uint32_t clusters, fat_type_t type) {
    uint64_t bytes = 0;
    uint64_t entries = (uint64_t)clusters + 2u;
    if (type == FAT_TYPE_12) {
        bytes = (entries * 3u + 1u) / 2u;
    } else if (type == FAT_TYPE_16) {
        bytes = entries * 2u;
    } else {
        bytes = entries * 4u;
    }
    uint32_t spf = (uint32_t)((bytes + (ATA_SECTOR_SIZE - 1u)) / ATA_SECTOR_SIZE);
    return spf ? spf : 1u;
}

static int fs_fat_try_layout(uint32_t total_sectors, fat_type_t type, uint8_t spc, uint16_t reserved,
                             uint16_t root_entries, uint32_t* out_spf, uint32_t* out_clusters) {
    if (spc == 0) return -1;
    uint32_t root_dir_sectors = (uint32_t)(((uint64_t)root_entries * 32u + (ATA_SECTOR_SIZE - 1u)) / ATA_SECTOR_SIZE);
    uint32_t spf = 1;
    for (int iter = 0; iter < 8; ++iter) {
        if (total_sectors <= reserved + (2u * spf) + root_dir_sectors) return -1;
        uint32_t data = total_sectors - reserved - (2u * spf) - root_dir_sectors;
        uint32_t clusters = data / spc;
        if (clusters == 0) return -1;
        uint32_t needed = fs_fat_spf_from_clusters(clusters, type);
        if (needed == spf) break;
        spf = needed;
    }
    if (total_sectors <= reserved + (2u * spf) + root_dir_sectors) return -1;
    uint32_t data = total_sectors - reserved - (2u * spf) - root_dir_sectors;
    uint32_t clusters = data / spc;
    if (clusters == 0) return -1;
    if (out_spf) *out_spf = spf;
    if (out_clusters) *out_clusters = clusters;
    return 0;
}

static int fs_fat_pick_layout(uint32_t total_sectors, fat_type_t wanted, fat_type_t* out_type,
                              uint8_t* out_spc, uint16_t* out_reserved, uint16_t* out_root_entries,
                              uint32_t* out_spf, uint32_t* out_clusters) {
    const uint8_t spc_choices[] = {1,2,4,8,16,32,64,128};
    fat_type_t types[3];
    int type_count = 0;
    if (wanted == 0) {
        types[type_count++] = FAT_TYPE_32;
        types[type_count++] = FAT_TYPE_16;
        types[type_count++] = FAT_TYPE_12;
    } else {
        types[type_count++] = wanted;
    }

    int have_best = 0;
    fat_type_t best_type = 0;
    uint8_t best_spc = 0;
    uint16_t best_reserved = 0;
    uint16_t best_root_entries = 0;
    uint32_t best_spf = 0;
    uint32_t best_clusters = 0;

    for (int ti = 0; ti < type_count; ++ti) {
        fat_type_t type = types[ti];
        uint16_t reserved = (type == FAT_TYPE_32) ? 32u : 1u;
        uint16_t root_entries = (type == FAT_TYPE_32) ? 0u : 512u;

        for (size_t i = 0; i < sizeof(spc_choices); ++i) {
            uint8_t spc = spc_choices[i];
            uint32_t spf = 0;
            uint32_t clusters = 0;
            if (fs_fat_try_layout(total_sectors, type, spc, reserved, root_entries, &spf, &clusters) != 0) continue;
            if (type == FAT_TYPE_12 && clusters >= 4085u) continue;
            if (type == FAT_TYPE_16 && (clusters < 4085u || clusters >= 65525u)) continue;
            if (type == FAT_TYPE_32 && clusters < 65525u) continue;
            if (!have_best || spc > best_spc) {
                have_best = 1;
                best_type = type;
                best_spc = spc;
                best_reserved = reserved;
                best_root_entries = root_entries;
                best_spf = spf;
                best_clusters = clusters;
            }
        }
        if (have_best && wanted != 0) break;
        if (have_best && wanted == 0 && best_type == FAT_TYPE_32) {
            /* Prefer FAT32 when auto */
            break;
        }
    }
    if (!have_best) return -1;
    if (out_type) *out_type = best_type;
    if (out_spc) *out_spc = best_spc;
    if (out_reserved) *out_reserved = best_reserved;
    if (out_root_entries) *out_root_entries = best_root_entries;
    if (out_spf) *out_spf = best_spf;
    if (out_clusters) *out_clusters = best_clusters;
    return 0;
}

int fs_mkfs_fat(uint8_t drive, uint32_t lba_start, uint32_t sectors, uint8_t fat_type) {
    uint64_t total = 0;
    if (fs_drive_total_sectors(drive, &total) != 0) return -1;
    if (lba_start >= total) return -1;
    if (sectors == 0 || (uint64_t)lba_start + (uint64_t)sectors > total) return -1;

    fs_block_dev_t d;
    if (fs_get_block_device(drive, &d) != 0 || !d.present || d.is_atapi) return -1;

    fat_type_t type = (fat_type == 12) ? FAT_TYPE_12 : (fat_type == 16) ? FAT_TYPE_16 : (fat_type == 32) ? FAT_TYPE_32 : 0;
    uint8_t spc = 0;
    uint16_t reserved = 0;
    uint16_t root_entries = 0;
    uint32_t spf = 0;
    uint32_t clusters = 0;
    if (fs_fat_pick_layout(sectors, type, &type, &spc, &reserved, &root_entries, &spf, &clusters) != 0) return -1;

    uint8_t bs[ATA_SECTOR_SIZE];
    memset(bs, 0, sizeof(bs));
    bs[0] = 0xEB;
    bs[1] = 0x3C;
    bs[2] = 0x90;
    memcpy(&bs[3], "NTUXFAT ", 8);
    fs_wr16(&bs[11], ATA_SECTOR_SIZE);
    bs[13] = spc;
    fs_wr16(&bs[14], reserved);
    bs[16] = 2;
    fs_wr16(&bs[17], root_entries);
    if (type != FAT_TYPE_32 && sectors < 0x10000u) fs_wr16(&bs[19], (uint16_t)sectors);
    else fs_wr16(&bs[19], 0);
    bs[21] = 0xF8;
    fs_wr16(&bs[22], (type == FAT_TYPE_32) ? 0u : (uint16_t)spf);
    fs_wr16(&bs[24], 63);
    fs_wr16(&bs[26], 255);
    fs_wr32(&bs[28], lba_start);
    if (type == FAT_TYPE_32 || sectors >= 0x10000u) fs_wr32(&bs[32], sectors);

    if (type == FAT_TYPE_32) {
        fs_wr32(&bs[36], spf);
        fs_wr16(&bs[40], 0);
        fs_wr16(&bs[42], 0);
        fs_wr32(&bs[44], 2u);
        fs_wr16(&bs[48], 1u);
        fs_wr16(&bs[50], 6u);
        bs[64] = 0x80;
        bs[66] = 0x29;
        fs_wr32(&bs[67], 0x12345678u);
        memcpy(&bs[71], "NTUX OS    ", 11);
        memcpy(&bs[82], "FAT32   ", 8);
    } else {
        bs[36] = 0x80;
        bs[38] = 0x29;
        fs_wr32(&bs[39], 0x12345678u);
        memcpy(&bs[43], "NTUX OS    ", 11);
        if (type == FAT_TYPE_12) memcpy(&bs[54], "FAT12   ", 8);
        else memcpy(&bs[54], "FAT16   ", 8);
    }
    bs[510] = 0x55;
    bs[511] = 0xAA;

    if (fs_device_write_sectors(drive, lba_start, 1, bs) != 0) return -1;

    uint8_t zero[ATA_SECTOR_SIZE];
    memset(zero, 0, sizeof(zero));

    if (type == FAT_TYPE_32) {
        uint8_t fsinfo[ATA_SECTOR_SIZE];
        memset(fsinfo, 0, sizeof(fsinfo));
        fs_wr32(&fsinfo[0], 0x41615252u);
        fs_wr32(&fsinfo[484], 0x61417272u);
        fs_wr32(&fsinfo[488], 0xFFFFFFFFu);
        fs_wr32(&fsinfo[492], 0xFFFFFFFFu);
        fs_wr16(&fsinfo[510], 0xAA55u);
        if (fs_device_write_sectors(drive, lba_start + 1u, 1, fsinfo) != 0) return -1;
        if (fs_device_write_sectors(drive, lba_start + 6u, 1, bs) != 0) return -1;
        for (uint32_t s = 2; s < reserved; ++s) {
            if (s == 6u) continue;
            if (fs_device_write_sectors(drive, lba_start + s, 1, zero) != 0) return -1;
        }
    } else {
        for (uint32_t s = 1; s < reserved; ++s) {
            if (fs_device_write_sectors(drive, lba_start + s, 1, zero) != 0) return -1;
        }
    }

    uint32_t fat_base = lba_start + reserved;
    for (uint8_t fi = 0; fi < 2; ++fi) {
        for (uint32_t s = 0; s < spf; ++s) {
            uint8_t sec[ATA_SECTOR_SIZE];
            memset(sec, 0, sizeof(sec));
            if (s == 0) {
                if (type == FAT_TYPE_12) {
                    sec[0] = 0xF8;
                    sec[1] = 0xFF;
                    sec[2] = 0xFF;
                } else if (type == FAT_TYPE_16) {
                    sec[0] = 0xF8;
                    sec[1] = 0xFF;
                    sec[2] = 0xFF;
                    sec[3] = 0xFF;
                } else {
                    fs_wr32(&sec[0], 0x0FFFFFF8u);
                    fs_wr32(&sec[4], 0x0FFFFFFFu);
                    fs_wr32(&sec[8], 0x0FFFFFFFu);
                }
            }
            if (fs_device_write_sectors(drive, fat_base + (uint32_t)fi * spf + s, 1, sec) != 0) return -1;
        }
    }

    uint32_t root_dir_sectors = (uint32_t)(((uint64_t)root_entries * 32u + (ATA_SECTOR_SIZE - 1u)) / ATA_SECTOR_SIZE);
    uint32_t first_root = fat_base + 2u * spf;
    if (type != FAT_TYPE_32) {
        for (uint32_t s = 0; s < root_dir_sectors; ++s) {
            if (fs_device_write_sectors(drive, first_root + s, 1, zero) != 0) return -1;
        }
    } else {
        uint32_t first_data = first_root + root_dir_sectors;
        for (uint32_t s = 0; s < spc; ++s) {
            if (fs_device_write_sectors(drive, first_data + s, 1, zero) != 0) return -1;
        }
    }

    (void)clusters;
    return 0;
}

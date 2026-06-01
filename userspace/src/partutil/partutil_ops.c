#include "partutil_app.h"
#include <string.h>
#include <syscall.h>

static int wipe_range(uint64_t drive, uint64_t start, uint64_t sectors) {
    uint8_t buf[512 * 8];
    memset(buf, 0, sizeof(buf));
    uint64_t left = sectors;
    uint64_t at = start;
    while (left > 0) {
        uint64_t step = (left > 8u) ? 8u : left;
        if (sys_block_write(drive, at, step, buf) != 0) {
            for (uint64_t s = 0; s < step; ++s) {
                if (sys_block_write(drive, at + s, 1, buf) != 0) return -1;
            }
        }
        at += step;
        left -= step;
    }
    return 0;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    uint32_t c = ~crc;
    for (size_t i = 0; i < len; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k) {
            uint32_t mask = (uint32_t)-(int)(c & 1u);
            c = (c >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~c;
}

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sectors;
} mbr_part_t;

typedef struct __attribute__((packed)) {
    uint8_t boot[446];
    mbr_part_t part[4];
    uint16_t sig;
} mbr_sector_t;

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

static void gpt_write_le_guid(uint8_t out[16], const uint8_t in[16]) {
    out[0] = in[3];
    out[1] = in[2];
    out[2] = in[1];
    out[3] = in[0];
    out[4] = in[5];
    out[5] = in[4];
    out[6] = in[7];
    out[7] = in[6];
    for (int i = 8; i < 16; ++i) out[i] = in[i];
}

static int write_gpt_single(uint64_t drive, uint64_t total, uint64_t* out_lba, uint64_t* out_secs) {
    if (total < 2048u + 34u) return -1;
    uint64_t first_usable = 34u;
    uint64_t last_usable = total - 34u;
    uint64_t start = 2048u;
    if (start < first_usable) start = first_usable;
    if (start > last_usable) start = first_usable;
    uint64_t end = last_usable;
    if (end <= start) return -1;

    mbr_sector_t pmbr;
    memset(&pmbr, 0, sizeof(pmbr));
    pmbr.part[0].type = 0xEE;
    pmbr.part[0].lba_first = 1u;
    pmbr.part[0].sectors = (total - 1u > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)(total - 1u);
    pmbr.sig = 0xAA55u;
    if (sys_block_write(drive, 0, 1, &pmbr) != 0) return -1;

    uint8_t part_array[512 * 32];
    memset(part_array, 0, sizeof(part_array));

    const uint8_t basic_data_guid[16] = {0xEB,0xD0,0xA0,0xA2,0xB9,0xE5,0x44,0x33,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7};
    uint8_t* ent = part_array;
    gpt_write_le_guid(ent, basic_data_guid);
    uint8_t uniq_guid[16] = {0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0,0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0};
    gpt_write_le_guid(ent + 16, uniq_guid);
    memcpy(ent + 32, &start, sizeof(start));
    memcpy(ent + 40, &end, sizeof(end));
    const char* name = "NTux System";
    for (int i = 0; name[i] && i < 36; ++i) {
        ent[56 + i * 2] = (uint8_t)name[i];
        ent[56 + i * 2 + 1] = 0;
    }

    uint32_t part_crc = crc32_update(0, part_array, sizeof(part_array));

    gpt_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.signature, "EFI PART", 8);
    hdr.revision = 0x00010000u;
    hdr.header_size = 92u;
    hdr.current_lba = 1u;
    hdr.backup_lba = total - 1u;
    hdr.first_usable_lba = first_usable;
    hdr.last_usable_lba = last_usable;
    const uint8_t disk_guid[16] = {0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0xA0};
    gpt_write_le_guid(hdr.disk_guid, disk_guid);
    hdr.partition_entry_lba = 2u;
    hdr.number_of_partition_entries = 128u;
    hdr.size_of_partition_entry = 128u;
    hdr.partition_entry_array_crc32 = part_crc;
    hdr.header_crc32 = 0;
    hdr.header_crc32 = crc32_update(0, (const uint8_t*)&hdr, hdr.header_size);

    uint8_t hdr_sector[512];
    memset(hdr_sector, 0, sizeof(hdr_sector));
    memcpy(hdr_sector, &hdr, sizeof(hdr));
    if (sys_block_write(drive, 1, 1, hdr_sector) != 0) return -1;
    if (sys_block_write(drive, 2, 32, part_array) != 0) return -1;

    uint64_t backup_part_lba = total - 33u;
    if (sys_block_write(drive, backup_part_lba, 32, part_array) != 0) return -1;

    gpt_header_t bhdr = hdr;
    bhdr.current_lba = total - 1u;
    bhdr.backup_lba = 1u;
    bhdr.partition_entry_lba = backup_part_lba;
    bhdr.header_crc32 = 0;
    bhdr.header_crc32 = crc32_update(0, (const uint8_t*)&bhdr, bhdr.header_size);
    memset(hdr_sector, 0, sizeof(hdr_sector));
    memcpy(hdr_sector, &bhdr, sizeof(bhdr));
    if (sys_block_write(drive, total - 1u, 1, hdr_sector) != 0) return -1;

    if (out_lba) *out_lba = start;
    if (out_secs) *out_secs = end - start + 1u;
    return 0;
}

static int write_gpt_dual(uint64_t drive, uint64_t total,
                          uint64_t* out_esp_lba, uint64_t* out_esp_secs,
                          uint64_t* out_sys_lba, uint64_t* out_sys_secs) {
    if (total < 2048u + 34u + 2048u) return -1;
    uint64_t first_usable = 34u;
    uint64_t last_usable = total - 34u;
    uint64_t start = 2048u;
    if (start < first_usable) start = first_usable;
    if (start > last_usable) start = first_usable;
    if (last_usable <= start + 2048u) return -1;

    uint64_t esp_secs = 65536u;
    if (esp_secs + start > last_usable) {
        esp_secs = (last_usable - start) / 4u;
    }
    if (esp_secs < 8192u) return -1;
    uint64_t esp_start = start;
    uint64_t esp_end = esp_start + esp_secs - 1u;

    uint64_t sys_start = esp_end + 1u;
    if (sys_start < start) sys_start = start;
    if (sys_start > last_usable) return -1;
    uint64_t sys_end = last_usable;

    mbr_sector_t pmbr;
    memset(&pmbr, 0, sizeof(pmbr));
    pmbr.part[0].type = 0xEE;
    pmbr.part[0].lba_first = 1u;
    pmbr.part[0].sectors = (total - 1u > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)(total - 1u);
    pmbr.sig = 0xAA55u;
    if (sys_block_write(drive, 0, 1, &pmbr) != 0) return -1;

    uint8_t part_array[512 * 32];
    memset(part_array, 0, sizeof(part_array));

    const uint8_t guid_esp[16] = {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
    const uint8_t guid_linux[16] = {0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4};

    uint8_t* ent0 = part_array;
    gpt_write_le_guid(ent0, guid_esp);
    uint8_t uniq0[16] = {0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE};
    gpt_write_le_guid(ent0 + 16, uniq0);
    memcpy(ent0 + 32, &esp_start, sizeof(esp_start));
    memcpy(ent0 + 40, &esp_end, sizeof(esp_end));
    const char* name0 = "NTux ESP";
    for (int i = 0; name0[i] && i < 36; ++i) {
        ent0[56 + i * 2] = (uint8_t)name0[i];
        ent0[56 + i * 2 + 1] = 0;
    }

    uint8_t* ent1 = part_array + 128;
    gpt_write_le_guid(ent1, guid_linux);
    uint8_t uniq1[16] = {0x21,0x43,0x65,0x87,0xA9,0xCB,0xED,0x0F,0x21,0x43,0x65,0x87,0xA9,0xCB,0xED,0x0F};
    gpt_write_le_guid(ent1 + 16, uniq1);
    memcpy(ent1 + 32, &sys_start, sizeof(sys_start));
    memcpy(ent1 + 40, &sys_end, sizeof(sys_end));
    const char* name1 = "NTux System";
    for (int i = 0; name1[i] && i < 36; ++i) {
        ent1[56 + i * 2] = (uint8_t)name1[i];
        ent1[56 + i * 2 + 1] = 0;
    }

    uint32_t part_crc = crc32_update(0, part_array, sizeof(part_array));

    gpt_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.signature, "EFI PART", 8);
    hdr.revision = 0x00010000u;
    hdr.header_size = 92u;
    hdr.current_lba = 1u;
    hdr.backup_lba = total - 1u;
    hdr.first_usable_lba = first_usable;
    hdr.last_usable_lba = last_usable;
    const uint8_t disk_guid[16] = {0xB1,0xC2,0xD3,0xE4,0xF5,0x06,0x17,0x28,0x39,0x4A,0x5B,0x6C,0x7D,0x8E,0x9F,0xA0};
    gpt_write_le_guid(hdr.disk_guid, disk_guid);
    hdr.partition_entry_lba = 2u;
    hdr.number_of_partition_entries = 128u;
    hdr.size_of_partition_entry = 128u;
    hdr.partition_entry_array_crc32 = part_crc;
    hdr.header_crc32 = 0;
    hdr.header_crc32 = crc32_update(0, (const uint8_t*)&hdr, hdr.header_size);

    uint8_t hdr_sector[512];
    memset(hdr_sector, 0, sizeof(hdr_sector));
    memcpy(hdr_sector, &hdr, sizeof(hdr));
    if (sys_block_write(drive, 1, 1, hdr_sector) != 0) return -1;
    if (sys_block_write(drive, 2, 32, part_array) != 0) return -1;

    uint64_t backup_part_lba = total - 33u;
    if (sys_block_write(drive, backup_part_lba, 32, part_array) != 0) return -1;

    gpt_header_t bhdr = hdr;
    bhdr.current_lba = total - 1u;
    bhdr.backup_lba = 1u;
    bhdr.partition_entry_lba = backup_part_lba;
    bhdr.header_crc32 = 0;
    bhdr.header_crc32 = crc32_update(0, (const uint8_t*)&bhdr, bhdr.header_size);
    memset(hdr_sector, 0, sizeof(hdr_sector));
    memcpy(hdr_sector, &bhdr, sizeof(bhdr));
    if (sys_block_write(drive, total - 1u, 1, hdr_sector) != 0) return -1;

    if (out_esp_lba) *out_esp_lba = esp_start;
    if (out_esp_secs) *out_esp_secs = esp_end - esp_start + 1u;
    if (out_sys_lba) *out_sys_lba = sys_start;
    if (out_sys_secs) *out_sys_secs = sys_end - sys_start + 1u;
    return 0;
}

static int write_mbr_single(uint64_t drive, uint64_t total_sectors) {
    ntux_mbr_part_req_t req;
    memset(&req, 0, sizeof(req));
    req.drive = (uint8_t)drive;
    for (uint8_t p = 1; p <= 4; ++p) {
        req.part_index = p;
        if (sys_block_set_mbr_partition(&req) != 0) return -1;
    }
    uint32_t start = (total_sectors > 4096u) ? 2048u : 1u;
    if (total_sectors <= start + 8u) return -1;
    uint32_t usable = (total_sectors > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)total_sectors;
    uint32_t secs = (usable > start) ? (usable - start) : 0u;
    req.part_index = 1;
    req.type = 0x0Cu;
    req.bootable = 0;
    req.lba_start = start;
    req.sectors = secs;
    if (req.sectors < 2048u) return -1;
    return (sys_block_set_mbr_partition(&req) == 0) ? 0 : -1;
}

void partutil_rescan(partutil_state_t* st) {
    if (!st) return;
    st->drive_count = 0;
    st->part_count = 0;

    ntux_block_device_info_t all[PARTUTIL_MAX_DRIVES];
    uint64_t count = 0;
    if (sys_block_list(all, PARTUTIL_MAX_DRIVES, &count) != 0) return;
    if (count > PARTUTIL_MAX_DRIVES) count = PARTUTIL_MAX_DRIVES;

    for (uint64_t i = 0; i < count; ++i) {
        if (!all[i].present || all[i].is_atapi) continue;
        if (st->drive_count >= PARTUTIL_MAX_DRIVES) break;
        st->drives[st->drive_count] = all[i];
        st->drive_ids[st->drive_count] = i;
        st->drive_count++;
    }

    if (st->drive_count == 0) {
        st->sel_drive = 0;
        st->part_count = 0;
        return;
    }
    if (st->sel_drive < 0 || st->sel_drive >= (int)st->drive_count) st->sel_drive = 0;

    uint64_t drive = st->drive_ids[st->sel_drive];
    uint64_t part_count = 0;
    if (sys_block_partitions(drive, st->parts, PARTUTIL_MAX_PARTS, &part_count) != 0) {
        st->part_count = 0;
    } else {
        if (part_count > PARTUTIL_MAX_PARTS) part_count = PARTUTIL_MAX_PARTS;
        st->part_count = part_count;
    }
    if (st->sel_part < 0 || st->sel_part >= (int)st->part_count) st->sel_part = 0;
}

int partutil_do_action(partutil_state_t* st, partutil_action_t act) {
    if (!st || st->drive_count == 0) {
        partutil_set_status(st, "No drive available.");
        return -1;
    }

    uint64_t drive = st->drive_ids[st->sel_drive];
    uint64_t sectors = st->drives[st->sel_drive].sectors;

    switch (act) {
        case PARTUTIL_ACT_RESCAN:
            partutil_rescan(st);
            partutil_set_status(st, "Rescanned devices.");
            return 0;
        case PARTUTIL_ACT_WIPE: {
            uint64_t wipe = (sectors > 2048u) ? 2048u : sectors;
            if (wipe_range(drive, 0, wipe) != 0) {
                partutil_set_status(st, "Wipe failed.");
                return -1;
            }
            partutil_set_status(st, "Wipe done (first 2048 sectors)." );
            partutil_rescan(st);
            return 0;
        }
        case PARTUTIL_ACT_GPT_SINGLE: {
            uint64_t lba = 0, secs = 0;
            if (write_gpt_single(drive, sectors, &lba, &secs) != 0) {
                partutil_set_status(st, "GPT create failed.");
                return -1;
            }
            (void)sys_fs_rescan();
            partutil_set_status(st, "GPT single partition created.");
            partutil_rescan(st);
            return 0;
        }
        case PARTUTIL_ACT_GPT_DUAL: {
            uint64_t esp_lba = 0, esp_secs = 0, sys_lba = 0, sys_secs = 0;
            if (write_gpt_dual(drive, sectors, &esp_lba, &esp_secs, &sys_lba, &sys_secs) != 0) {
                partutil_set_status(st, "GPT+ESP create failed.");
                return -1;
            }
            (void)sys_fs_rescan();
            partutil_set_status(st, "GPT ESP + System created.");
            partutil_rescan(st);
            return 0;
        }
        case PARTUTIL_ACT_MBR_SINGLE: {
            if (write_mbr_single(drive, sectors) != 0) {
                partutil_set_status(st, "MBR create failed.");
                return -1;
            }
            (void)sys_fs_rescan();
            partutil_set_status(st, "MBR single partition created.");
            partutil_rescan(st);
            return 0;
        }
        case PARTUTIL_ACT_MKFS_FAT:
        case PARTUTIL_ACT_MKFS_EXT2:
        case PARTUTIL_ACT_MKFS_EXT4: {
            if (st->part_count == 0) {
                partutil_set_status(st, "No partition selected.");
                return -1;
            }
            ntux_partition_info_t p = st->parts[st->sel_part];
            long rc = -1;
            if (act == PARTUTIL_ACT_MKFS_FAT) {
                rc = sys_mkfs_fat(drive, p.lba_start, p.sectors, 0);
            } else if (act == PARTUTIL_ACT_MKFS_EXT2) {
                rc = sys_mkfs_ext2(drive, p.lba_start, p.sectors);
            } else {
                rc = sys_mkfs_ext4(drive, p.lba_start, p.sectors);
            }
            if (rc != 0) {
                partutil_set_status(st, "mkfs failed.");
                return -1;
            }
            (void)sys_fs_rescan();
            partutil_set_status(st, "mkfs completed.");
            partutil_rescan(st);
            return 0;
        }
        default:
            break;
    }
    return -1;
}

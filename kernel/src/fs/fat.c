#include <fs/fat.h>

#include <drivers/sata/ata.h>
#include <mm/kmalloc.h>
#include <lib/string.h>

typedef struct __attribute__((packed)) {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t first_cluster_hi;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t first_cluster_lo;
    uint32_t size;
} fat_dir_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t first_cluster_lo;
    uint16_t name3[2];
} fat_lfn_entry_t;

typedef struct {
    uint32_t sector;
    uint32_t offset;
} fat_dir_loc_t;

#define FAT_ATTR_DIR 0x10
#define FAT_ATTR_LFN 0x0F
#define FAT_ATTR_VOLUME_ID 0x08

#define FAT_LFN_LAST 0x40
#define FAT_LFN_ORD_MASK 0x1F
#define FAT_LFN_CHARS_PER_ENTRY 13

typedef struct {
    char name[VFS_MAX_NAME];
    uint8_t expected_parts;
    uint32_t seen_parts_mask;
    int active;
} fat_lfn_accum_t;

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t fat_eoc_value(const fat_fs_t* fs) {
    if (fs->type == FAT_TYPE_12) return 0x0FFFu;
    if (fs->type == FAT_TYPE_16) return 0xFFFFu;
    return 0x0FFFFFFFu;
}

static int fat_is_power_of_two_u32(uint32_t v) {
    return v != 0u && (v & (v - 1u)) == 0u;
}

static uint32_t fat_fat_entry_count(const fat_fs_t* fs) {
    if (!fs) return 0;
    uint32_t fat_bytes = fs->sectors_per_fat * fs->bytes_per_sector;
    if (fs->type == FAT_TYPE_12) return (fat_bytes * 2u) / 3u;
    if (fs->type == FAT_TYPE_16) return fat_bytes / 2u;
    return fat_bytes / 4u;
}

static uint32_t fat_max_valid_cluster(const fat_fs_t* fs) {
    if (!fs) return 1u;
    uint32_t by_data = fs->cluster_count + 1u;
    uint32_t entries = fat_fat_entry_count(fs);
    if (entries <= 2u) return 1u;
    uint32_t by_fat = entries - 1u;
    return (by_data < by_fat) ? by_data : by_fat;
}

static int fat_read_sector(const fat_fs_t* fs, uint32_t rel_sector, uint8_t out[ATA_SECTOR_SIZE]) {
    return ata_read_sectors(fs->drive_index, (uint64_t)fs->partition_lba + rel_sector, 1, out);
}

static int fat_write_sector(const fat_fs_t* fs, uint32_t rel_sector, const uint8_t in[ATA_SECTOR_SIZE]) {
    return ata_write_sectors(fs->drive_index, (uint64_t)fs->partition_lba + rel_sector, 1, in);
}

static int fat_write_sectors(const fat_fs_t* fs, uint32_t rel_sector, uint8_t count, const void* in) {
    return ata_write_sectors(fs->drive_index, (uint64_t)fs->partition_lba + rel_sector, count, in);
}

static uint32_t fat_cluster_to_sector(const fat_fs_t* fs, uint32_t cluster) {
    return fs->first_data_sector + ((cluster - 2u) * fs->sectors_per_cluster);
}

static int fat_read_fat_entry(const fat_fs_t* fs, uint32_t cluster, uint32_t* out_next) {
    uint8_t sec[ATA_SECTOR_SIZE];
    if (fs->type == FAT_TYPE_12) {
        uint32_t fat_offset = cluster + (cluster / 2u);
        uint32_t sector = fs->first_fat_sector + (fat_offset / fs->bytes_per_sector);
        uint32_t off = fat_offset % fs->bytes_per_sector;
        if (fat_read_sector(fs, sector, sec) != 0) return -1;

        uint8_t b0 = sec[off];
        uint8_t b1;
        if (off == fs->bytes_per_sector - 1) {
            uint8_t sec2[ATA_SECTOR_SIZE];
            if (fat_read_sector(fs, sector + 1, sec2) != 0) return -1;
            b1 = sec2[0];
        } else {
            b1 = sec[off + 1];
        }

        uint16_t v = (uint16_t)b0 | ((uint16_t)b1 << 8);
        if (cluster & 1u) v >>= 4;
        else v &= 0x0FFFu;
        *out_next = v;
        return 0;
    }

    if (fs->type == FAT_TYPE_16) {
        uint32_t fat_offset = cluster * 2u;
        uint32_t sector = fs->first_fat_sector + (fat_offset / fs->bytes_per_sector);
        uint32_t off = fat_offset % fs->bytes_per_sector;
        if (fat_read_sector(fs, sector, sec) != 0) return -1;
        *out_next = rd16(&sec[off]);
        return 0;
    }

    uint32_t fat_offset = cluster * 4u;
    uint32_t sector = fs->first_fat_sector + (fat_offset / fs->bytes_per_sector);
    uint32_t off = fat_offset % fs->bytes_per_sector;
    if (fat_read_sector(fs, sector, sec) != 0) return -1;
    *out_next = rd32(&sec[off]) & 0x0FFFFFFFu;
    return 0;
}

static int fat_write_fat12_single(const fat_fs_t* fs, uint32_t fat_base, uint32_t cluster, uint16_t value12) {
    uint32_t fat_offset = cluster + (cluster / 2u);
    uint32_t sector = fat_base + (fat_offset / fs->bytes_per_sector);
    uint32_t off = fat_offset % fs->bytes_per_sector;

    uint8_t s0[ATA_SECTOR_SIZE];
    uint8_t s1[ATA_SECTOR_SIZE];
    if (fat_read_sector(fs, sector, s0) != 0) return -1;
    int has_second = (off == (fs->bytes_per_sector - 1));
    if (has_second && fat_read_sector(fs, sector + 1, s1) != 0) return -1;

    uint8_t b0 = s0[off];
    uint8_t b1 = has_second ? s1[0] : s0[off + 1];
    uint16_t old = (uint16_t)b0 | ((uint16_t)b1 << 8);

    uint16_t nv;
    if (cluster & 1u) {
        nv = (uint16_t)((old & 0x000F) | ((value12 & 0x0FFF) << 4));
    } else {
        nv = (uint16_t)((old & 0xF000) | (value12 & 0x0FFF));
    }

    s0[off] = (uint8_t)(nv & 0xFF);
    if (has_second) s1[0] = (uint8_t)((nv >> 8) & 0xFF);
    else s0[off + 1] = (uint8_t)((nv >> 8) & 0xFF);

    if (fat_write_sector(fs, sector, s0) != 0) return -1;
    if (has_second && fat_write_sector(fs, sector + 1, s1) != 0) return -1;
    return 0;
}

static int fat_write_fat_entry(const fat_fs_t* fs, uint32_t cluster, uint32_t value) {
    for (uint8_t fi = 0; fi < fs->fat_count; ++fi) {
        uint32_t fat_base = fs->first_fat_sector + (uint32_t)fi * fs->sectors_per_fat;

        if (fs->type == FAT_TYPE_12) {
            if (fat_write_fat12_single(fs, fat_base, cluster, (uint16_t)(value & 0x0FFFu)) != 0) return -1;
            continue;
        }

        uint8_t sec[ATA_SECTOR_SIZE];
        uint32_t fat_off = (fs->type == FAT_TYPE_16) ? (cluster * 2u) : (cluster * 4u);
        uint32_t sector = fat_base + (fat_off / fs->bytes_per_sector);
        uint32_t off = fat_off % fs->bytes_per_sector;

        if (fat_read_sector(fs, sector, sec) != 0) return -1;

        if (fs->type == FAT_TYPE_16) {
            wr16(&sec[off], (uint16_t)(value & 0xFFFFu));
        } else {
            uint32_t old = rd32(&sec[off]);
            uint32_t nv = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
            wr32(&sec[off], nv);
        }

        if (fat_write_sector(fs, sector, sec) != 0) return -1;
    }

    return 0;
}

static int fat_is_eoc(const fat_fs_t* fs, uint32_t value) {
    if (fs->type == FAT_TYPE_12) return value >= 0x0FF8u;
    if (fs->type == FAT_TYPE_16) return value >= 0xFFF8u;
    return value >= 0x0FFFFFF8u;
}

static int fat_sfn_to_name(const fat_dir_entry_t* e, char out[VFS_MAX_NAME]) {
    int p = 0;
    for (int i = 0; i < 8; ++i) {
        uint8_t c = e->name[i];
        if (c == ' ') break;
        if (p + 1 >= VFS_MAX_NAME) return -1;
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
        out[p++] = (char)c;
    }

    int has_ext = 0;
    for (int i = 8; i < 11; ++i) {
        if (e->name[i] != ' ') {
            has_ext = 1;
            break;
        }
    }

    if (has_ext && p + 1 < VFS_MAX_NAME) {
        out[p++] = '.';
        for (int i = 8; i < 11; ++i) {
            uint8_t c = e->name[i];
            if (c == ' ') break;
            if (p + 1 >= VFS_MAX_NAME) return -1;
            if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
            out[p++] = (char)c;
        }
    }

    out[p] = '\0';
    return 0;
}

static void fat_lfn_reset(fat_lfn_accum_t* a) {
    memset(a, 0, sizeof(*a));
}

static void fat_lfn_put_char(fat_lfn_accum_t* a, size_t pos, uint16_t ch) {
    if (pos >= VFS_MAX_NAME - 1) return;
    if (ch == 0x0000) return;
    if (ch == 0xFFFF) return;
    a->name[pos] = (char)((ch >= 32 && ch <= 126) ? ch : '?');
}

static int fat_lfn_push(fat_lfn_accum_t* a, const fat_dir_entry_t* e) {
    const fat_lfn_entry_t* le = (const fat_lfn_entry_t*)e;
    uint8_t ord = le->order;
    uint8_t idx = (uint8_t)(ord & FAT_LFN_ORD_MASK);

    if (idx == 0 || idx > 20) {
        fat_lfn_reset(a);
        return -1;
    }

    if (ord & FAT_LFN_LAST) {
        fat_lfn_reset(a);
        a->active = 1;
        a->expected_parts = idx;
    } else if (!a->active || a->expected_parts == 0 || idx > a->expected_parts) {
        fat_lfn_reset(a);
        return -1;
    }

    size_t base = (size_t)(idx - 1) * FAT_LFN_CHARS_PER_ENTRY;
    size_t p = 0;
    fat_lfn_put_char(a, base + p++, le->name1[0]);
    fat_lfn_put_char(a, base + p++, le->name1[1]);
    fat_lfn_put_char(a, base + p++, le->name1[2]);
    fat_lfn_put_char(a, base + p++, le->name1[3]);
    fat_lfn_put_char(a, base + p++, le->name1[4]);
    fat_lfn_put_char(a, base + p++, le->name2[0]);
    fat_lfn_put_char(a, base + p++, le->name2[1]);
    fat_lfn_put_char(a, base + p++, le->name2[2]);
    fat_lfn_put_char(a, base + p++, le->name2[3]);
    fat_lfn_put_char(a, base + p++, le->name2[4]);
    fat_lfn_put_char(a, base + p++, le->name2[5]);
    fat_lfn_put_char(a, base + p++, le->name3[0]);
    fat_lfn_put_char(a, base + p++, le->name3[1]);

    a->seen_parts_mask |= (1u << (idx - 1));
    return 0;
}

static int fat_lfn_complete(const fat_lfn_accum_t* a) {
    if (!a->active || a->expected_parts == 0 || a->expected_parts > 20) return 0;
    uint32_t full_mask = (1u << a->expected_parts) - 1u;
    if (a->seen_parts_mask != full_mask) return 0;
    return a->name[0] != '\0';
}

static int fat_entry_to_name(const fat_dir_entry_t* e, fat_lfn_accum_t* lfn, char out[VFS_MAX_NAME]) {
    if (fat_lfn_complete(lfn)) {
        strncpy(out, lfn->name, VFS_MAX_NAME - 1);
        out[VFS_MAX_NAME - 1] = '\0';
        fat_lfn_reset(lfn);
        return 0;
    }
    fat_lfn_reset(lfn);
    return fat_sfn_to_name(e, out);
}

static int fat_name_equals_ci(const char* a, const char* b) {
    size_t ia = 0;
    while (a[ia] && b[ia]) {
        char ca = a[ia];
        char cb = b[ia];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        ++ia;
    }
    return a[ia] == '\0' && b[ia] == '\0';
}

static int fat_make_sfn(const char* name, uint8_t out11[11]);

static int fat_name_equals(const char* a, const char* b) {
    if (fat_name_equals_ci(a, b)) return 1;
    uint8_t sfn[11];
    if (fat_make_sfn(b, sfn) == 0) {
        fat_dir_entry_t e;
        memset(&e, 0, sizeof(e));
        memcpy(e.name, sfn, 11);
        char sname[VFS_MAX_NAME];
        if (fat_sfn_to_name(&e, sname) == 0) {
            if (fat_name_equals_ci(a, sname)) return 1;
        }
    }
    return 0;
}

static uint8_t fat_lfn_checksum(const uint8_t sfn[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; ++i) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + sfn[i]);
    }
    return sum;
}

static size_t fat_lfn_entry_count_for_name(const char* name) {
    size_t len = name ? strlen(name) : 0;
    return (len + (FAT_LFN_CHARS_PER_ENTRY - 1u)) / FAT_LFN_CHARS_PER_ENTRY;
}

static void fat_lfn_write_entry(fat_lfn_entry_t* e, uint8_t order, uint8_t checksum,
                                const char* name, size_t name_len, size_t start_idx) {
    memset(e, 0, sizeof(*e));
    e->order = order;
    e->attr = FAT_ATTR_LFN;
    e->type = 0;
    e->checksum = checksum;
    e->first_cluster_lo = 0;

    uint16_t chars[FAT_LFN_CHARS_PER_ENTRY];
    for (size_t i = 0; i < FAT_LFN_CHARS_PER_ENTRY; ++i) {
        size_t idx = start_idx + i;
        if (idx < name_len) {
            chars[i] = (uint16_t)(uint8_t)name[idx];
        } else if (idx == name_len) {
            chars[i] = 0x0000;
        } else {
            chars[i] = 0xFFFF;
        }
    }

    for (int i = 0; i < 5; ++i) e->name1[i] = chars[i];
    for (int i = 0; i < 6; ++i) e->name2[i] = chars[5 + i];
    for (int i = 0; i < 2; ++i) e->name3[i] = chars[11 + i];
}

static int fat_char_valid_sfn(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return 1;
    if (c == '_' || c == '-' || c == '$' || c == '~') return 1;
    return 0;
}

static int fat_make_sfn(const char* name, uint8_t out11[11]) {
    for (int i = 0; i < 11; ++i) out11[i] = ' ';
    if (!name || !name[0]) return -1;

    const char* dot = strrchr(name, '.');
    int base_len = dot ? (int)(dot - name) : (int)strlen(name);
    int ext_len = dot ? (int)strlen(dot + 1) : 0;

    if (base_len <= 0) return -1;

    int long_or_invalid = 0;
    if (base_len > 8 || ext_len > 3) long_or_invalid = 1;

    for (int i = 0; i < base_len; ++i) {
        char c = name[i];
        if (!fat_char_valid_sfn(c)) {
            long_or_invalid = 1;
            break;
        }
    }
    for (int i = 0; i < ext_len; ++i) {
        char c = dot[1 + i];
        if (!fat_char_valid_sfn(c)) {
            long_or_invalid = 1;
            break;
        }
    }

    int base_out = base_len;
    if (base_out > 8) base_out = 8;
    if (long_or_invalid && base_out > 6) base_out = 6;

    for (int i = 0; i < base_out; ++i) {
        char c = name[i];
        if (!fat_char_valid_sfn(c)) c = '_';
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out11[i] = (uint8_t)c;
    }

    if (long_or_invalid) {
        out11[6] = '~';
        out11[7] = '1';
    }

    int ext_out = ext_len;
    if (ext_out > 3) ext_out = 3;
    for (int i = 0; i < ext_out; ++i) {
        char c = dot[1 + i];
        if (!fat_char_valid_sfn(c)) c = '_';
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out11[8 + i] = (uint8_t)c;
    }

    return 0;
}

static uint32_t fat_entry_cluster(const fat_dir_entry_t* e) {
    return ((uint32_t)e->first_cluster_hi << 16) | e->first_cluster_lo;
}

static void fat_set_entry_cluster(fat_dir_entry_t* e, uint32_t cluster) {
    e->first_cluster_hi = (uint16_t)((cluster >> 16) & 0xFFFFu);
    e->first_cluster_lo = (uint16_t)(cluster & 0xFFFFu);
}

static int fat_iterate_dir(
    fat_fs_t* fs,
    uint32_t dir_cluster,
    int (*visitor)(fat_fs_t*, const fat_dir_entry_t*, const char*, void*),
    void* user
) {
    uint8_t sec[ATA_SECTOR_SIZE];
    fat_lfn_accum_t lfn;
    fat_lfn_reset(&lfn);

    if (fs->type != FAT_TYPE_32 && dir_cluster == 0) {
        uint32_t total = fs->root_dir_sectors;
        for (uint32_t s = 0; s < total; ++s) {
            if (fat_read_sector(fs, fs->first_root_dir_sector + s, sec) != 0) return -1;
            for (size_t i = 0; i < ATA_SECTOR_SIZE / sizeof(fat_dir_entry_t); ++i) {
                fat_dir_entry_t* e = (fat_dir_entry_t*)&sec[i * sizeof(fat_dir_entry_t)];
                if (e->name[0] == 0x00) return 0;
                if (e->name[0] == 0xE5) {
                    fat_lfn_reset(&lfn);
                    continue;
                }
                if (e->attr == FAT_ATTR_LFN) {
                    (void)fat_lfn_push(&lfn, e);
                    continue;
                }
                if (e->attr & FAT_ATTR_VOLUME_ID) {
                    fat_lfn_reset(&lfn);
                    continue;
                }
                char name[VFS_MAX_NAME];
                if (fat_entry_to_name(e, &lfn, name) != 0) continue;
                int rc = visitor(fs, e, name, user);
                if (rc != 0) return rc;
            }
        }
        return 0;
    }

    uint32_t cluster = (fs->type == FAT_TYPE_32 && dir_cluster == 0) ? fs->root_cluster : dir_cluster;
    uint32_t guard = fs->cluster_count + 2u;
    while (cluster >= 2 && guard-- > 0) {
        if (cluster > fs->cluster_count + 1u) return -1;
        uint32_t first = fat_cluster_to_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
            if (fat_read_sector(fs, first + s, sec) != 0) return -1;
            for (size_t i = 0; i < ATA_SECTOR_SIZE / sizeof(fat_dir_entry_t); ++i) {
                fat_dir_entry_t* e = (fat_dir_entry_t*)&sec[i * sizeof(fat_dir_entry_t)];
                if (e->name[0] == 0x00) return 0;
                if (e->name[0] == 0xE5) {
                    fat_lfn_reset(&lfn);
                    continue;
                }
                if (e->attr == FAT_ATTR_LFN) {
                    (void)fat_lfn_push(&lfn, e);
                    continue;
                }
                if (e->attr & FAT_ATTR_VOLUME_ID) {
                    fat_lfn_reset(&lfn);
                    continue;
                }
                char name[VFS_MAX_NAME];
                if (fat_entry_to_name(e, &lfn, name) != 0) continue;
                int rc = visitor(fs, e, name, user);
                if (rc != 0) return rc;
            }
        }

        uint32_t next = 0;
        if (fat_read_fat_entry(fs, cluster, &next) != 0) return -1;
        if (fat_is_eoc(fs, next)) break;
        if (next == cluster) return -1;
        cluster = next;
    }

    if (cluster >= 2 && guard == 0) return -1;

    return 0;
}

typedef struct {
    const char* name;
    fat_dir_entry_t entry;
    int found;
} fat_find_ctx_t;

static int fat_find_visitor(fat_fs_t* fs, const fat_dir_entry_t* e, const char* name, void* user) {
    (void)fs;
    fat_find_ctx_t* ctx = (fat_find_ctx_t*)user;
    if (fat_name_equals(name, ctx->name)) {
        ctx->entry = *e;
        ctx->found = 1;
        return 1;
    }
    return 0;
}

static int fat_resolve_path(fat_fs_t* fs, const char* path, fat_dir_entry_t* out, uint32_t* out_parent_cluster) {
    if (path == NULL || path[0] != '/') return -1;
    if (path[1] == '\0') {
        if (out) {
            memset(out, 0, sizeof(*out));
            out->attr = FAT_ATTR_DIR;
        }
        if (out_parent_cluster) *out_parent_cluster = 0;
        return 0;
    }

    uint32_t dir_cluster = 0;
    const char* p = path;
    char token[VFS_MAX_NAME];

    while (*p == '/') ++p;
    while (*p) {
        size_t n = 0;
        while (p[n] && p[n] != '/' && n + 1 < sizeof(token)) {
            token[n] = p[n];
            ++n;
        }
        token[n] = '\0';
        p += n;
        while (*p == '/') ++p;

        fat_find_ctx_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.name = token;

        int rc = fat_iterate_dir(fs, dir_cluster, fat_find_visitor, &ctx);
        if (rc < 0 || !ctx.found) return -2;

        if (*p == '\0') {
            if (out) *out = ctx.entry;
            if (out_parent_cluster) *out_parent_cluster = dir_cluster;
            return 0;
        }

        uint32_t next_cluster = fat_entry_cluster(&ctx.entry);
        if ((ctx.entry.attr & FAT_ATTR_DIR) == 0) return -3;
        dir_cluster = next_cluster;
    }

    return -4;
}

static int fat_read_file_data(fat_fs_t* fs, const fat_dir_entry_t* entry, void* out, size_t out_cap, size_t* out_len) {
    uint32_t file_size = entry->size;
    if (out_len) *out_len = file_size;
    if (out == NULL) return 0;
    if (out_cap < file_size) return -1;

    uint8_t* dst = (uint8_t*)out;
    uint32_t remaining = file_size;
    uint32_t cluster = fat_entry_cluster(entry);
    uint32_t guard = fs->cluster_count + 2u;

    if (cluster < 2 && file_size == 0) return 0;

    uint8_t sec[ATA_SECTOR_SIZE];
    while (remaining > 0 && cluster >= 2 && guard-- > 0) {
        if (cluster > fs->cluster_count + 1u) return -4;
        uint32_t first_sector = fat_cluster_to_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster && remaining > 0; ++s) {
            if (fat_read_sector(fs, first_sector + s, sec) != 0) return -2;
            uint32_t take = remaining < ATA_SECTOR_SIZE ? remaining : ATA_SECTOR_SIZE;
            memcpy(dst, sec, take);
            dst += take;
            remaining -= take;
        }

        if (remaining == 0) break;
        uint32_t next = 0;
        if (fat_read_fat_entry(fs, cluster, &next) != 0) return -3;
        if (fat_is_eoc(fs, next)) break;
        if (next == cluster) return -3;
        cluster = next;
    }

    if (remaining > 0 && guard == 0) return -4;

    return remaining == 0 ? 0 : -4;
}

static int fat_read_file_range_data(fat_fs_t* fs, const fat_dir_entry_t* entry, size_t offset,
                                    void* out, size_t len, size_t* out_read) {
    if (!fs || !entry || (!out && len > 0)) return -1;
    if (out_read) *out_read = 0;

    size_t file_size = entry->size;
    if (offset >= file_size || len == 0) return 0;
    if (offset + len > file_size) len = file_size - offset;

    uint8_t* dst = (uint8_t*)out;
    size_t remaining = len;
    size_t skip = offset;
    size_t bytes_per_cluster = (size_t)fs->sectors_per_cluster * ATA_SECTOR_SIZE;
    uint32_t cluster = fat_entry_cluster(entry);
    uint32_t guard = fs->cluster_count + 2u;

    if (cluster < 2 && file_size == 0) return 0;

    while (skip >= bytes_per_cluster && cluster >= 2 && guard-- > 0) {
        skip -= bytes_per_cluster;
        uint32_t next = 0;
        if (fat_read_fat_entry(fs, cluster, &next) != 0) return -3;
        if (fat_is_eoc(fs, next)) return -4;
        if (next == cluster) return -3;
        cluster = next;
    }

    uint8_t sec[ATA_SECTOR_SIZE];
    while (remaining > 0 && cluster >= 2 && guard-- > 0) {
        if (cluster > fs->cluster_count + 1u) return -4;
        uint32_t first_sector = fat_cluster_to_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster && remaining > 0; ++s) {
            if (fat_read_sector(fs, first_sector + s, sec) != 0) return -2;
            if (skip >= ATA_SECTOR_SIZE) {
                skip -= ATA_SECTOR_SIZE;
                continue;
            }
            size_t avail = ATA_SECTOR_SIZE - skip;
            size_t take = remaining < avail ? remaining : avail;
            memcpy(dst, sec + skip, take);
            dst += take;
            remaining -= take;
            skip = 0;
        }

        if (remaining == 0) break;
        uint32_t next = 0;
        if (fat_read_fat_entry(fs, cluster, &next) != 0) return -3;
        if (fat_is_eoc(fs, next)) break;
        if (next == cluster) return -3;
        cluster = next;
    }

    size_t read = len - remaining;
    if (out_read) *out_read = read;
    return remaining == 0 ? 0 : -4;
}

int fat_fs_read_file_range(fat_fs_t* fs, const char* path, size_t offset, void* out, size_t len,
                           size_t* out_read, size_t* out_file_len) {
    if (!fs || !path || path[0] != '/') return -1;
    fat_dir_entry_t e;
    if (fat_resolve_path(fs, path, &e, NULL) != 0) return -1;
    if (e.attr & FAT_ATTR_DIR) return -2;
    if (out_file_len) *out_file_len = e.size;
    if (!out || len == 0) return 0;
    return fat_read_file_range_data(fs, &e, offset, out, len, out_read);
}

static int fat_zero_cluster(fat_fs_t* fs, uint32_t cluster) {
    uint8_t zero[ATA_SECTOR_SIZE];
    memset(zero, 0, sizeof(zero));
    uint32_t first = fat_cluster_to_sector(fs, cluster);
    for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
        if (fat_write_sector(fs, first + s, zero) != 0) return -1;
    }
    return 0;
}

static int fat_alloc_cluster(fat_fs_t* fs, uint32_t* out_cluster, int zero) {
    uint32_t max_cluster = fat_max_valid_cluster(fs);
    if (max_cluster < 2u) return -1;
    uint32_t start = fs->next_free_cluster;
    if (start < 2u || start > max_cluster) start = 2u;
    for (int pass = 0; pass < 2; ++pass) {
        uint32_t begin = (pass == 0) ? start : 2u;
        uint32_t end = (pass == 0) ? max_cluster : (start > 2u ? start - 1u : 1u);
        for (uint32_t c = begin; c <= end; ++c) {
            uint32_t v = 0;
            if (fat_read_fat_entry(fs, c, &v) != 0) return -1;
            if (v == 0) {
                if (fat_write_fat_entry(fs, c, fat_eoc_value(fs)) != 0) return -1;
                if (zero && fat_zero_cluster(fs, c) != 0) return -1;
                *out_cluster = c;
                fs->next_free_cluster = (c + 1u <= max_cluster) ? (c + 1u) : 2u;
                return 0;
            }
        }
    }
    return -1;
}

static int fat_free_chain(fat_fs_t* fs, uint32_t first_cluster) {
    uint32_t c = first_cluster;
    uint32_t max_cluster = fat_max_valid_cluster(fs);
    while (c >= 2) {
        uint32_t next = 0;
        if (fat_read_fat_entry(fs, c, &next) != 0) return -1;
        if (fat_write_fat_entry(fs, c, 0) != 0) return -1;
        if (fat_is_eoc(fs, next)) break;
        if (next < 2 || next > max_cluster) break;
        c = next;
    }
    return 0;
}

static int fat_split_parent_leaf(const char* path, char* parent, size_t parent_cap, char* leaf, size_t leaf_cap) {
    if (!path || path[0] != '/') return -1;
    const char* slash = strrchr(path, '/');
    if (!slash || !slash[1]) return -1;

    size_t ll = strlen(slash + 1);
    if (ll + 1 > leaf_cap) return -1;
    memcpy(leaf, slash + 1, ll + 1);

    if (slash == path) {
        if (parent_cap < 2) return -1;
        parent[0] = '/';
        parent[1] = '\0';
        return 0;
    }

    size_t pl = (size_t)(slash - path);
    if (pl + 1 > parent_cap) return -1;
    memcpy(parent, path, pl);
    parent[pl] = '\0';
    return 0;
}

static int fat_resolve_parent_dir(fat_fs_t* fs, const char* parent_path, uint32_t* out_dir_cluster) {
    if (strcmp(parent_path, "/") == 0) {
        *out_dir_cluster = 0;
        return 0;
    }

    fat_dir_entry_t e;
    if (fat_resolve_path(fs, parent_path, &e, NULL) != 0) return -1;
    if ((e.attr & FAT_ATTR_DIR) == 0) return -1;
    *out_dir_cluster = fat_entry_cluster(&e);
    return 0;
}

static int fat_write_entry_at(fat_fs_t* fs, const fat_dir_loc_t* loc, const fat_dir_entry_t* in) {
    uint8_t sec[ATA_SECTOR_SIZE];
    if (fat_read_sector(fs, loc->sector, sec) != 0) return -1;
    memcpy(&sec[loc->offset], in, sizeof(*in));
    if (fat_write_sector(fs, loc->sector, sec) != 0) return -1;
    return 0;
}

static int fat_write_entries_at(fat_fs_t* fs, const fat_dir_loc_t* loc, const void* entries, uint32_t count) {
    fat_dir_loc_t cur = *loc;
    const uint8_t* p = (const uint8_t*)entries;
    for (uint32_t i = 0; i < count; ++i) {
        if (fat_write_entry_at(fs, &cur, (const fat_dir_entry_t*)p) != 0) return -1;
        p += sizeof(fat_dir_entry_t);
        cur.offset += sizeof(fat_dir_entry_t);
        if (cur.offset >= ATA_SECTOR_SIZE) {
            cur.offset = 0;
            cur.sector += 1;
        }
    }
    return 0;
}

static int fat_dir_find_free_slots(fat_fs_t* fs, uint32_t dir_cluster, uint32_t count, fat_dir_loc_t* out_loc) {
    if (count == 0) return -1;
    uint32_t entries_per_sector = ATA_SECTOR_SIZE / sizeof(fat_dir_entry_t);
    if (count > entries_per_sector) return -1;

    uint8_t sec[ATA_SECTOR_SIZE];

    if (fs->type != FAT_TYPE_32 && dir_cluster == 0) {
        for (uint32_t s = 0; s < fs->root_dir_sectors; ++s) {
            uint32_t sector = fs->first_root_dir_sector + s;
            if (fat_read_sector(fs, sector, sec) != 0) return -1;
            uint32_t run = 0;
            uint32_t run_start = 0;
            for (uint32_t off = 0; off < ATA_SECTOR_SIZE; off += sizeof(fat_dir_entry_t)) {
                fat_dir_entry_t* e = (fat_dir_entry_t*)&sec[off];
                if (e->name[0] == 0x00 || e->name[0] == 0xE5) {
                    if (run == 0) run_start = off;
                    run++;
                    if (run >= count) {
                        out_loc->sector = sector;
                        out_loc->offset = run_start;
                        return 0;
                    }
                } else {
                    run = 0;
                }
            }
        }
        return -1;
    }

    uint32_t cluster = (fs->type == FAT_TYPE_32 && dir_cluster == 0) ? fs->root_cluster : dir_cluster;
    uint32_t last_cluster = cluster;
    uint32_t guard = fs->cluster_count + 2u;
    while (cluster >= 2 && guard-- > 0) {
        if (cluster > fs->cluster_count + 1u) return -1;
        uint32_t first = fat_cluster_to_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
            uint32_t sector = first + s;
            if (fat_read_sector(fs, sector, sec) != 0) return -1;
            uint32_t run = 0;
            uint32_t run_start = 0;
            for (uint32_t off = 0; off < ATA_SECTOR_SIZE; off += sizeof(fat_dir_entry_t)) {
                fat_dir_entry_t* e = (fat_dir_entry_t*)&sec[off];
                if (e->name[0] == 0x00 || e->name[0] == 0xE5) {
                    if (run == 0) run_start = off;
                    run++;
                    if (run >= count) {
                        out_loc->sector = sector;
                        out_loc->offset = run_start;
                        return 0;
                    }
                } else {
                    run = 0;
                }
            }
        }

        last_cluster = cluster;
        uint32_t next = 0;
        if (fat_read_fat_entry(fs, cluster, &next) != 0) return -1;
        if (fat_is_eoc(fs, next)) break;
        if (next == cluster) return -1;
        cluster = next;
    }
    if (cluster >= 2 && guard == 0) return -1;

    uint32_t new_cluster = 0;
    if (fat_alloc_cluster(fs, &new_cluster, 1) != 0) return -1;
    if (fat_write_fat_entry(fs, last_cluster, new_cluster) != 0) return -1;
    if (fat_write_fat_entry(fs, new_cluster, fat_eoc_value(fs)) != 0) return -1;

    out_loc->sector = fat_cluster_to_sector(fs, new_cluster);
    out_loc->offset = 0;
    return 0;
}

static int fat_dir_find_entry(fat_fs_t* fs, uint32_t dir_cluster, const char* name, fat_dir_entry_t* out, fat_dir_loc_t* out_loc) {
    uint8_t sec[ATA_SECTOR_SIZE];
    fat_lfn_accum_t lfn;
    fat_lfn_reset(&lfn);

    if (fs->type != FAT_TYPE_32 && dir_cluster == 0) {
        for (uint32_t s = 0; s < fs->root_dir_sectors; ++s) {
            uint32_t sector = fs->first_root_dir_sector + s;
            if (fat_read_sector(fs, sector, sec) != 0) return -1;
            for (uint32_t off = 0; off < ATA_SECTOR_SIZE; off += sizeof(fat_dir_entry_t)) {
                fat_dir_entry_t* e = (fat_dir_entry_t*)&sec[off];
                if (e->name[0] == 0x00) return -2;
                if (e->name[0] == 0xE5) {
                    fat_lfn_reset(&lfn);
                    continue;
                }
                if (e->attr == FAT_ATTR_LFN) {
                    (void)fat_lfn_push(&lfn, e);
                    continue;
                }
                if (e->attr & FAT_ATTR_VOLUME_ID) {
                    fat_lfn_reset(&lfn);
                    continue;
                }
                char n[VFS_MAX_NAME];
                if (fat_entry_to_name(e, &lfn, n) != 0) continue;
                if (fat_name_equals(n, name)) {
                    *out = *e;
                    if (out_loc) {
                        out_loc->sector = sector;
                        out_loc->offset = off;
                    }
                    return 0;
                }
            }
        }
        return -2;
    }

    uint32_t cluster = (fs->type == FAT_TYPE_32 && dir_cluster == 0) ? fs->root_cluster : dir_cluster;
    uint32_t guard = fs->cluster_count + 2u;
    while (cluster >= 2 && guard-- > 0) {
        if (cluster > fs->cluster_count + 1u) return -1;
        uint32_t first = fat_cluster_to_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
            uint32_t sector = first + s;
            if (fat_read_sector(fs, sector, sec) != 0) return -1;
            for (uint32_t off = 0; off < ATA_SECTOR_SIZE; off += sizeof(fat_dir_entry_t)) {
                fat_dir_entry_t* e = (fat_dir_entry_t*)&sec[off];
                if (e->name[0] == 0x00) return -2;
                if (e->name[0] == 0xE5) {
                    fat_lfn_reset(&lfn);
                    continue;
                }
                if (e->attr == FAT_ATTR_LFN) {
                    (void)fat_lfn_push(&lfn, e);
                    continue;
                }
                if (e->attr & FAT_ATTR_VOLUME_ID) {
                    fat_lfn_reset(&lfn);
                    continue;
                }
                char n[VFS_MAX_NAME];
                if (fat_entry_to_name(e, &lfn, n) != 0) continue;
                if (fat_name_equals(n, name)) {
                    *out = *e;
                    if (out_loc) {
                        out_loc->sector = sector;
                        out_loc->offset = off;
                    }
                    return 0;
                }
            }
        }

        uint32_t next = 0;
        if (fat_read_fat_entry(fs, cluster, &next) != 0) return -1;
        if (fat_is_eoc(fs, next)) break;
        if (next == cluster) return -1;
        cluster = next;
    }

    if (cluster >= 2 && guard == 0) return -1;

    return -2;
}

static int fat_write_file_payload(fat_fs_t* fs, const void* data, size_t len, uint32_t* out_first_cluster) {
    if (len == 0) {
        *out_first_cluster = 0;
        return 0;
    }

    const uint8_t* src = (const uint8_t*)data;
    uint32_t cluster_size = (uint32_t)fs->sectors_per_cluster * ATA_SECTOR_SIZE;
    uint32_t needed = (uint32_t)((len + cluster_size - 1) / cluster_size);

    uint32_t first = 0;
    uint32_t prev = 0;
    uint32_t* allocated = (uint32_t*)kmalloc(sizeof(uint32_t) * needed);
    if (!allocated) return -1;

    for (uint32_t i = 0; i < needed; ++i) {
        uint32_t c = 0;
        if (fat_alloc_cluster(fs, &c, 0) != 0) {
            if (first >= 2) (void)fat_free_chain(fs, first);
            kfree(allocated);
            return -1;
        }
        allocated[i] = c;
        if (i == 0) first = c;
        if (prev >= 2 && fat_write_fat_entry(fs, prev, c) != 0) {
            (void)fat_free_chain(fs, first);
            kfree(allocated);
            return -1;
        }
        prev = c;
    }

    if (prev >= 2 && fat_write_fat_entry(fs, prev, fat_eoc_value(fs)) != 0) {
        (void)fat_free_chain(fs, first);
        kfree(allocated);
        return -1;
    }

    size_t remaining = len;
    uint32_t cluster_bytes = (uint32_t)fs->sectors_per_cluster * ATA_SECTOR_SIZE;
    uint8_t* cluster_buf = NULL;
    for (uint32_t i = 0; i < needed; ++i) {
        uint32_t c = allocated[i];
        uint32_t first_sec = fat_cluster_to_sector(fs, c);
        if (remaining >= cluster_bytes) {
            if (fat_write_sectors(fs, first_sec, fs->sectors_per_cluster, src) != 0) {
                (void)fat_free_chain(fs, first);
                kfree(allocated);
                return -1;
            }
            src += cluster_bytes;
            remaining -= cluster_bytes;
            continue;
        }

        if (!cluster_buf) {
            cluster_buf = (uint8_t*)kmalloc(cluster_bytes);
        }
        if (cluster_buf) {
            memset(cluster_buf, 0, cluster_bytes);
            if (remaining > 0) {
                memcpy(cluster_buf, src, remaining);
                src += remaining;
                remaining = 0;
            }
            if (fat_write_sectors(fs, first_sec, fs->sectors_per_cluster, cluster_buf) != 0) {
                if (cluster_buf) kfree(cluster_buf);
                (void)fat_free_chain(fs, first);
                kfree(allocated);
                return -1;
            }
        } else {
            uint8_t sec[ATA_SECTOR_SIZE];
            for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
                memset(sec, 0, sizeof(sec));
                size_t take = remaining < ATA_SECTOR_SIZE ? remaining : ATA_SECTOR_SIZE;
                if (take > 0) {
                    memcpy(sec, src, take);
                    src += take;
                    remaining -= take;
                }
                if (fat_write_sector(fs, first_sec + s, sec) != 0) {
                    (void)fat_free_chain(fs, first);
                    kfree(allocated);
                    return -1;
                }
            }
        }
    }

    if (cluster_buf) kfree(cluster_buf);
    kfree(allocated);
    *out_first_cluster = first;
    return 0;
}

static int fat_write_file_payload_reader(fat_fs_t* fs, size_t len, uint32_t* out_first_cluster, fat_fs_reader_fn reader, void* user) {
    if (len == 0) {
        *out_first_cluster = 0;
        return 0;
    }
    if (!reader) return -1;

    uint32_t cluster_size = (uint32_t)fs->sectors_per_cluster * ATA_SECTOR_SIZE;
    uint32_t needed = (uint32_t)((len + cluster_size - 1) / cluster_size);
    uint32_t* allocated = (uint32_t*)kmalloc(sizeof(uint32_t) * needed);
    if (!allocated) return -1;

    uint32_t first = 0;
    uint32_t prev = 0;
    for (uint32_t i = 0; i < needed; ++i) {
        uint32_t c = 0;
        if (fat_alloc_cluster(fs, &c, 0) != 0) {
            if (first >= 2) (void)fat_free_chain(fs, first);
            kfree(allocated);
            return -1;
        }
        allocated[i] = c;
        if (i == 0) first = c;
        if (prev >= 2 && fat_write_fat_entry(fs, prev, c) != 0) {
            (void)fat_free_chain(fs, first);
            kfree(allocated);
            return -1;
        }
        prev = c;
    }

    if (prev >= 2 && fat_write_fat_entry(fs, prev, fat_eoc_value(fs)) != 0) {
        (void)fat_free_chain(fs, first);
        kfree(allocated);
        return -1;
    }

    size_t remaining = len;
    size_t file_off = 0;
    uint32_t cluster_bytes = (uint32_t)fs->sectors_per_cluster * ATA_SECTOR_SIZE;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_bytes);
    for (uint32_t i = 0; i < needed; ++i) {
        uint32_t c = allocated[i];
        uint32_t first_sec = fat_cluster_to_sector(fs, c);
        if (cluster_buf) {
            memset(cluster_buf, 0, cluster_bytes);
            size_t want = remaining < cluster_bytes ? remaining : cluster_bytes;
            if (want > 0) {
                if (reader(user, file_off, cluster_buf, want) != 0) {
                    kfree(cluster_buf);
                    (void)fat_free_chain(fs, first);
                    kfree(allocated);
                    return -1;
                }
                file_off += want;
                remaining -= want;
            }
            if (fat_write_sectors(fs, first_sec, fs->sectors_per_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                (void)fat_free_chain(fs, first);
                kfree(allocated);
                return -1;
            }
        } else {
            uint8_t sec[ATA_SECTOR_SIZE];
            for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s) {
                memset(sec, 0, sizeof(sec));
                size_t take = remaining < ATA_SECTOR_SIZE ? remaining : ATA_SECTOR_SIZE;
                if (take > 0) {
                    if (reader(user, file_off, sec, take) != 0) {
                        (void)fat_free_chain(fs, first);
                        kfree(allocated);
                        return -1;
                    }
                    file_off += take;
                    remaining -= take;
                }
                if (fat_write_sector(fs, first_sec + s, sec) != 0) {
                    (void)fat_free_chain(fs, first);
                    kfree(allocated);
                    return -1;
                }
            }
        }
    }

    if (cluster_buf) kfree(cluster_buf);
    kfree(allocated);
    *out_first_cluster = first;
    return 0;
}

int fat_fs_create_file_from_reader(fat_fs_t* fs, const char* path, size_t len, fat_fs_reader_fn reader, void* user) {
    if (!fs || !path || path[0] != '/') return -1;

    char parent[VFS_MAX_PATH];
    char leaf[VFS_MAX_NAME];
    if (fat_split_parent_leaf(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) return -1;

    uint32_t dir_cluster = 0;
    if (fat_resolve_parent_dir(fs, parent, &dir_cluster) != 0) return -1;

    fat_dir_entry_t existing;
    if (fat_dir_find_entry(fs, dir_cluster, leaf, &existing, NULL) == 0) return -1;

    uint8_t sfn[11];
    if (fat_make_sfn(leaf, sfn) != 0) return -1;

    fat_dir_loc_t loc;
    size_t lfn_count = fat_lfn_entry_count_for_name(leaf);
    uint32_t total_entries = (uint32_t)lfn_count + 1u;
    if (fat_dir_find_free_slots(fs, dir_cluster, total_entries, &loc) != 0) return -1;

    fat_dir_entry_t ne;
    memset(&ne, 0, sizeof(ne));
    memcpy(ne.name, sfn, 11);
    ne.attr = 0;

    uint32_t first_cluster = 0;
    if (fat_write_file_payload_reader(fs, len, &first_cluster, reader, user) != 0) return -1;
    fat_set_entry_cluster(&ne, first_cluster);
    ne.size = (uint32_t)len;

    if (lfn_count > 0) {
        if (lfn_count > 8) return -1;
        fat_lfn_entry_t lfn_entries[8];
        uint8_t sum = fat_lfn_checksum(sfn);
        size_t name_len = strlen(leaf);
        for (size_t seg = 0; seg < lfn_count; ++seg) {
            uint8_t order = (uint8_t)(seg + 1u);
            if (seg == lfn_count - 1u) order |= FAT_LFN_LAST;
            fat_lfn_write_entry(&lfn_entries[seg], order, sum, leaf, name_len, seg * FAT_LFN_CHARS_PER_ENTRY);
        }
        for (size_t i = 0; i < lfn_count / 2; ++i) {
            fat_lfn_entry_t tmp = lfn_entries[i];
            lfn_entries[i] = lfn_entries[lfn_count - 1u - i];
            lfn_entries[lfn_count - 1u - i] = tmp;
        }
        if (fat_write_entries_at(fs, &loc, lfn_entries, (uint32_t)lfn_count) != 0) return -1;

        fat_dir_loc_t sfn_loc = loc;
        sfn_loc.offset += (uint32_t)lfn_count * sizeof(fat_dir_entry_t);
        if (sfn_loc.offset >= ATA_SECTOR_SIZE) {
            sfn_loc.sector += sfn_loc.offset / ATA_SECTOR_SIZE;
            sfn_loc.offset = sfn_loc.offset % ATA_SECTOR_SIZE;
        }
        return fat_write_entry_at(fs, &sfn_loc, &ne);
    }

    return fat_write_entry_at(fs, &loc, &ne);
}

int fat_fs_mount(fat_fs_t* fs, uint8_t drive_index, uint64_t partition_lba) {
    uint8_t bs[ATA_SECTOR_SIZE];
    memset(fs, 0, sizeof(*fs));

    if (ata_read_sectors(drive_index, partition_lba, 1, bs) != 0) return -1;
    if (bs[510] != 0x55 || bs[511] != 0xAA) return -2;

    fs->drive_index = drive_index;
    fs->partition_lba = partition_lba;

    fs->bytes_per_sector = rd16(&bs[11]);
    fs->sectors_per_cluster = bs[13];
    fs->reserved_sectors = rd16(&bs[14]);
    fs->fat_count = bs[16];
    fs->root_entry_count = rd16(&bs[17]);

    uint16_t total16 = rd16(&bs[19]);
    uint32_t total32 = rd32(&bs[32]);
    fs->total_sectors = total16 ? total16 : total32;

    uint16_t spf16 = rd16(&bs[22]);
    uint32_t spf32 = rd32(&bs[36]);
    fs->sectors_per_fat = spf16 ? spf16 : spf32;

    if (fs->bytes_per_sector != ATA_SECTOR_SIZE || fs->sectors_per_cluster == 0 || fs->fat_count == 0 || fs->sectors_per_fat == 0) {
        return -3;
    }
    if (!fat_is_power_of_two_u32(fs->sectors_per_cluster) || fs->reserved_sectors == 0 || fs->total_sectors == 0) {
        return -3;
    }

    fs->root_dir_sectors = ((uint32_t)fs->root_entry_count * 32u + (fs->bytes_per_sector - 1u)) / fs->bytes_per_sector;
    fs->first_fat_sector = fs->reserved_sectors;
    fs->first_root_dir_sector = fs->first_fat_sector + ((uint32_t)fs->fat_count * fs->sectors_per_fat);
    fs->first_data_sector = fs->first_root_dir_sector + fs->root_dir_sectors;
    if (fs->first_data_sector >= fs->total_sectors) return -3;

    uint32_t data_sectors = fs->total_sectors - fs->first_data_sector;
    fs->cluster_count = data_sectors / fs->sectors_per_cluster;
    if (fs->cluster_count == 0) return -3;

    if (fs->cluster_count < 4085u) fs->type = FAT_TYPE_12;
    else if (fs->cluster_count < 65525u) fs->type = FAT_TYPE_16;
    else fs->type = FAT_TYPE_32;

    uint32_t fat_entries = 0;
    {
        uint32_t fat_bytes = fs->sectors_per_fat * fs->bytes_per_sector;
        if (fs->type == FAT_TYPE_12) {
            fat_entries = (fat_bytes * 2u) / 3u;
        } else if (fs->type == FAT_TYPE_16) {
            fat_entries = fat_bytes / 2u;
        } else {
            fat_entries = fat_bytes / 4u;
        }
    }
    if (fat_entries <= 2u) return -3;
    if (fs->cluster_count > fat_entries - 2u) {
        fs->cluster_count = fat_entries - 2u;
    }
    if (fs->cluster_count == 0) return -3;

    fs->root_cluster = (fs->type == FAT_TYPE_32) ? rd32(&bs[44]) : 0;
    fs->next_free_cluster = 2u;
    if (fs->type == FAT_TYPE_32) {
        if (fs->root_cluster < 2 || fs->root_cluster > fs->cluster_count + 1u) return -3;
    } else {
        if (fs->root_dir_sectors == 0) return -3;
        if (fs->first_root_dir_sector + fs->root_dir_sectors > fs->total_sectors) return -3;
    }

    {
        uint8_t sec[ATA_SECTOR_SIZE];
        uint32_t root_sector = (fs->type == FAT_TYPE_32)
            ? fat_cluster_to_sector(fs, fs->root_cluster)
            : fs->first_root_dir_sector;
        if (fat_read_sector(fs, root_sector, sec) != 0) return -3;
    }

    fs->mounted = true;
    return 0;
}

static int fat_create_entry(fat_fs_t* fs, const char* path, uint8_t attr, const void* data, size_t len) {
    char parent[VFS_MAX_PATH];
    char leaf[VFS_MAX_NAME];
    if (fat_split_parent_leaf(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) return -1;

    uint32_t dir_cluster = 0;
    if (fat_resolve_parent_dir(fs, parent, &dir_cluster) != 0) return -1;

    fat_dir_entry_t existing;
    if (fat_dir_find_entry(fs, dir_cluster, leaf, &existing, NULL) == 0) return -1;

    uint8_t sfn[11];
    if (fat_make_sfn(leaf, sfn) != 0) return -1;

    fat_dir_loc_t loc;
    size_t lfn_count = fat_lfn_entry_count_for_name(leaf);
    uint32_t total_entries = (uint32_t)lfn_count + 1u;
    if (fat_dir_find_free_slots(fs, dir_cluster, total_entries, &loc) != 0) return -1;

    fat_dir_entry_t ne;
    memset(&ne, 0, sizeof(ne));
    memcpy(ne.name, sfn, 11);
    ne.attr = attr;

    if (attr & FAT_ATTR_DIR) {
        uint32_t new_dir_cluster = 0;
        if (fat_alloc_cluster(fs, &new_dir_cluster, 1) != 0) return -1;
        fat_set_entry_cluster(&ne, new_dir_cluster);
        ne.size = 0;

        uint8_t sec[ATA_SECTOR_SIZE];
        memset(sec, 0, sizeof(sec));
        fat_dir_entry_t dot;
        memset(&dot, 0, sizeof(dot));
        memset(dot.name, ' ', 11);
        dot.name[0] = '.';
        dot.attr = FAT_ATTR_DIR;
        fat_set_entry_cluster(&dot, new_dir_cluster);

        fat_dir_entry_t dotdot;
        memset(&dotdot, 0, sizeof(dotdot));
        memset(dotdot.name, ' ', 11);
        dotdot.name[0] = '.';
        dotdot.name[1] = '.';
        dotdot.attr = FAT_ATTR_DIR;
        uint32_t parent_ref = 0;
        if (dir_cluster == 0 && fs->type == FAT_TYPE_32) parent_ref = fs->root_cluster;
        else parent_ref = dir_cluster;
        fat_set_entry_cluster(&dotdot, parent_ref);

        memcpy(&sec[0], &dot, sizeof(dot));
        memcpy(&sec[32], &dotdot, sizeof(dotdot));

        uint32_t first_sector = fat_cluster_to_sector(fs, new_dir_cluster);
        if (fat_write_sector(fs, first_sector, sec) != 0) return -1;
        for (uint32_t s = 1; s < fs->sectors_per_cluster; ++s) {
            memset(sec, 0, sizeof(sec));
            if (fat_write_sector(fs, first_sector + s, sec) != 0) return -1;
        }
    } else {
        uint32_t first_cluster = 0;
        if (fat_write_file_payload(fs, data, len, &first_cluster) != 0) return -1;
        fat_set_entry_cluster(&ne, first_cluster);
        ne.size = (uint32_t)len;
    }

    if (lfn_count > 0) {
        if (lfn_count > 8) return -1;
        fat_lfn_entry_t lfn_entries[8];
        uint8_t sum = fat_lfn_checksum(sfn);
        size_t name_len = strlen(leaf);
        for (size_t seg = 0; seg < lfn_count; ++seg) {
            uint8_t order = (uint8_t)(seg + 1u);
            if (seg == lfn_count - 1u) order |= FAT_LFN_LAST;
            fat_lfn_write_entry(&lfn_entries[seg], order, sum, leaf, name_len, seg * FAT_LFN_CHARS_PER_ENTRY);
        }
        for (size_t i = 0; i < lfn_count / 2; ++i) {
            fat_lfn_entry_t tmp = lfn_entries[i];
            lfn_entries[i] = lfn_entries[lfn_count - 1u - i];
            lfn_entries[lfn_count - 1u - i] = tmp;
        }
        if (fat_write_entries_at(fs, &loc, lfn_entries, (uint32_t)lfn_count) != 0) return -1;

        fat_dir_loc_t sfn_loc = loc;
        sfn_loc.offset += (uint32_t)lfn_count * sizeof(fat_dir_entry_t);
        if (sfn_loc.offset >= ATA_SECTOR_SIZE) {
            sfn_loc.sector += sfn_loc.offset / ATA_SECTOR_SIZE;
            sfn_loc.offset = sfn_loc.offset % ATA_SECTOR_SIZE;
        }
        return fat_write_entry_at(fs, &sfn_loc, &ne);
    }

    return fat_write_entry_at(fs, &loc, &ne);
}

static int fat_op_mkdir(void* ctx, const char* path) {
    fat_fs_t* fs = (fat_fs_t*)ctx;
    return fat_create_entry(fs, path, FAT_ATTR_DIR, NULL, 0);
}

static int fat_op_create_file(void* ctx, const char* path, const void* data, size_t len) {
    fat_fs_t* fs = (fat_fs_t*)ctx;
    return fat_create_entry(fs, path, 0, data, len);
}

static int fat_op_write_file(void* ctx, const char* path, const void* data, size_t len) {
    fat_fs_t* fs = (fat_fs_t*)ctx;

    char parent[VFS_MAX_PATH];
    char leaf[VFS_MAX_NAME];
    if (fat_split_parent_leaf(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) return -1;

    uint32_t dir_cluster = 0;
    if (fat_resolve_parent_dir(fs, parent, &dir_cluster) != 0) return -1;

    fat_dir_entry_t e;
    fat_dir_loc_t loc;
    if (fat_dir_find_entry(fs, dir_cluster, leaf, &e, &loc) != 0) {
        return fat_create_entry(fs, path, 0, data, len);
    }

    if (e.attr & FAT_ATTR_DIR) return -1;

    uint32_t old_first = fat_entry_cluster(&e);
    if (old_first >= 2) (void)fat_free_chain(fs, old_first);

    uint32_t first_cluster = 0;
    if (fat_write_file_payload(fs, data, len, &first_cluster) != 0) return -1;

    fat_set_entry_cluster(&e, first_cluster);
    e.size = (uint32_t)len;
    return fat_write_entry_at(fs, &loc, &e);
}

static int fat_op_read_file(void* ctx, const char* path, void* out, size_t out_cap, size_t* out_len) {
    fat_fs_t* fs = (fat_fs_t*)ctx;
    fat_dir_entry_t e;
    if (fat_resolve_path(fs, path, &e, NULL) != 0) return -1;
    if (e.attr & FAT_ATTR_DIR) return -2;
    return fat_read_file_data(fs, &e, out, out_cap, out_len);
}

typedef struct {
    vfs_dirent_t* out;
    size_t max_entries;
    size_t count;
} fat_list_ctx_t;

static int fat_list_visitor(fat_fs_t* fs, const fat_dir_entry_t* e, const char* name, void* user) {
    (void)fs;
    fat_list_ctx_t* ctx = (fat_list_ctx_t*)user;
    if (ctx->out && ctx->count < ctx->max_entries) {
        memset(&ctx->out[ctx->count], 0, sizeof(vfs_dirent_t));
        strncpy(ctx->out[ctx->count].name, name, VFS_MAX_NAME - 1);
        ctx->out[ctx->count].name[VFS_MAX_NAME - 1] = '\0';
        ctx->out[ctx->count].is_dir = (e->attr & FAT_ATTR_DIR) != 0;
        ctx->out[ctx->count].size = e->size;
    }
    ctx->count++;
    return 0;
}

static int fat_op_list_dir(void* ctx, const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count) {
    fat_fs_t* fs = (fat_fs_t*)ctx;
    uint32_t dir_cluster = 0;

    if (!(path[0] == '/' && path[1] == '\0')) {
        fat_dir_entry_t e;
        if (fat_resolve_path(fs, path, &e, NULL) != 0) return -1;
        if ((e.attr & FAT_ATTR_DIR) == 0) return -2;
        dir_cluster = fat_entry_cluster(&e);
    }

    fat_list_ctx_t lc = { .out = out, .max_entries = max_entries, .count = 0 };
    int rc = fat_iterate_dir(fs, dir_cluster, fat_list_visitor, &lc);
    if (out_count) *out_count = lc.count;
    return rc;
}

static int fat_op_exists(void* ctx, const char* path) {
    fat_fs_t* fs = (fat_fs_t*)ctx;
    fat_dir_entry_t e;
    return fat_resolve_path(fs, path, &e, NULL) == 0 ? 1 : 0;
}

typedef struct {
    int found_non_dot;
} fat_non_dot_ctx_t;

static int fat_non_dot_visitor(fat_fs_t* fs, const fat_dir_entry_t* e, const char* name, void* user) {
    (void)fs;
    (void)e;
    fat_non_dot_ctx_t* ctx = (fat_non_dot_ctx_t*)user;
    if (!(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)) {
        ctx->found_non_dot = 1;
        return 1;
    }
    return 0;
}

static int fat_dir_is_empty(fat_fs_t* fs, uint32_t dir_cluster) {
    fat_non_dot_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    int rc = fat_iterate_dir(fs, dir_cluster, fat_non_dot_visitor, &ctx);
    if (rc < 0) return 0;
    return ctx.found_non_dot ? 0 : 1;
}

static int fat_op_remove(void* ctx, const char* path) {
    fat_fs_t* fs = (fat_fs_t*)ctx;
    if (!path || path[0] != '/' || path[1] == '\0') return -1;

    char parent[VFS_MAX_PATH];
    char leaf[VFS_MAX_NAME];
    if (fat_split_parent_leaf(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0) return -1;

    uint32_t dir_cluster = 0;
    if (fat_resolve_parent_dir(fs, parent, &dir_cluster) != 0) return -1;

    fat_dir_entry_t e;
    fat_dir_loc_t loc;
    if (fat_dir_find_entry(fs, dir_cluster, leaf, &e, &loc) != 0) return -1;

    uint32_t first_cluster = fat_entry_cluster(&e);
    if (e.attr & FAT_ATTR_DIR) {
        if (first_cluster >= 2 && !fat_dir_is_empty(fs, first_cluster)) return -1;
        if (first_cluster >= 2 && fat_free_chain(fs, first_cluster) != 0) return -1;
    } else {
        if (first_cluster >= 2 && fat_free_chain(fs, first_cluster) != 0) return -1;
    }

    uint8_t sec[ATA_SECTOR_SIZE];
    if (fat_read_sector(fs, loc.sector, sec) != 0) return -1;
    sec[loc.offset] = 0xE5;
    if (fat_write_sector(fs, loc.sector, sec) != 0) return -1;
    return 0;
}

static int fat_op_rename(void* ctx, const char* old_path, const char* new_path) {
    fat_fs_t* fs = (fat_fs_t*)ctx;
    if (!old_path || !new_path || old_path[0] != '/' || new_path[0] != '/') return -1;
    if (old_path[1] == '\0' || new_path[1] == '\0') return -1;

    char old_parent[VFS_MAX_PATH], old_leaf[VFS_MAX_NAME];
    char new_parent[VFS_MAX_PATH], new_leaf[VFS_MAX_NAME];
    if (fat_split_parent_leaf(old_path, old_parent, sizeof(old_parent), old_leaf, sizeof(old_leaf)) != 0) return -1;
    if (fat_split_parent_leaf(new_path, new_parent, sizeof(new_parent), new_leaf, sizeof(new_leaf)) != 0) return -1;
    if (strcmp(old_parent, new_parent) != 0) return -1;

    uint32_t dir_cluster = 0;
    if (fat_resolve_parent_dir(fs, old_parent, &dir_cluster) != 0) return -1;

    fat_dir_entry_t e;
    fat_dir_loc_t loc;
    if (fat_dir_find_entry(fs, dir_cluster, old_leaf, &e, &loc) != 0) return -1;

    fat_dir_entry_t tmp;
    if (fat_dir_find_entry(fs, dir_cluster, new_leaf, &tmp, NULL) == 0) return -1;

    uint8_t sfn[11];
    if (fat_make_sfn(new_leaf, sfn) != 0) return -1;
    memcpy(e.name, sfn, 11);
    return fat_write_entry_at(fs, &loc, &e);
}

static const vfs_backend_ops_t g_fat_ops = {
    .mkdir = fat_op_mkdir,
    .create_file = fat_op_create_file,
    .write_file = fat_op_write_file,
    .read_file = fat_op_read_file,
    .list_dir = fat_op_list_dir,
    .exists = fat_op_exists,
    .remove = fat_op_remove,
    .rename = fat_op_rename,
};

const vfs_backend_ops_t* fat_fs_backend_ops(void) {
    return &g_fat_ops;
}

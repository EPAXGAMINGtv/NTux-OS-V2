#include <fs/iso.h>

#include <drivers/framebuffer/kprint.h>
#include <drivers/sata/ata.h>
#include <lib/ctype.h>
#include <lib/string.h>

#define ISO_SECTOR_SIZE 2048u
#define ISO_PVD_SECTOR 16u
#define ISO_FLAG_DIRECTORY 0x02u

typedef struct {
    uint32_t lba;
    uint32_t size;
    uint8_t flags;
} iso_node_t;

static uint32_t iso_rd32_le(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int iso_name_eq_ci(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int iso_read_sector(const iso_fs_t* fs, uint32_t lba, uint8_t out[ISO_SECTOR_SIZE]) {
    if (!fs || !out) return -1;

    if (fs->is_atapi) {
        return ata_read_cd_sectors(fs->drive_index, lba, 1, out);
    }

    uint64_t phys_lba = (uint64_t)fs->partition_lba + ((uint64_t)lba * 4u);
    return ata_read_sectors(fs->drive_index, phys_lba, 4, out);
}

static int iso_read_sectors_batch(const iso_fs_t* fs, uint32_t lba, uint32_t count, uint8_t* out) {
    if (!fs || !out) return -1;
    uint32_t remaining = count;
    uint8_t* dst = out;
    while (remaining > 0) {
        uint32_t step = remaining;
        if (fs->is_atapi) {
            if (step > 255u) step = 255u;
            if (ata_read_cd_sectors(fs->drive_index, lba, (uint8_t)step, dst) != 0) return -1;
        } else {
            if (step > 63u) step = 63u; /* 63 ISO sectors = 252 ATA sectors */
            uint64_t phys_lba = (uint64_t)fs->partition_lba + ((uint64_t)lba * 4u);
            uint8_t ata_count = (uint8_t)(step * 4u);
            if (ata_read_sectors(fs->drive_index, phys_lba, ata_count, dst) != 0) return -1;
        }
        lba += step;
        dst += step * ISO_SECTOR_SIZE;
        remaining -= step;
    }
    return 0;
}

static int iso_decode_record_name(const iso_fs_t* fs, const uint8_t* rec, char out[VFS_MAX_NAME]) {
    if (!rec || !out) return -1;

    uint8_t name_len = rec[32];
    const uint8_t* id = &rec[33];

    if (name_len == 1 && id[0] == 0x00) {
        out[0] = '.';
        out[1] = '\0';
        return 0;
    }
    if (name_len == 1 && id[0] == 0x01) {
        out[0] = '.';
        out[1] = '.';
        out[2] = '\0';
        return 0;
    }

    size_t w = 0;
    if (fs && fs->use_joliet) {
        for (size_t i = 0; i + 1 < name_len && w + 1 < VFS_MAX_NAME; i += 2) {
            uint8_t hi = id[i];
            uint8_t lo = id[i + 1];
            if (lo == ';' || hi == ';') break;
            char c = (hi == 0) ? (char)lo : ((lo == 0) ? (char)hi : '?');
            out[w++] = c;
        }
    } else {
        for (size_t i = 0; i < name_len && w + 1 < VFS_MAX_NAME; ++i) {
            char c = (char)id[i];
            if (c == ';') break;
            out[w++] = c;
        }
    }

    while (w > 0 && out[w - 1] == '.') {
        --w;
    }

    out[w] = '\0';
    return w > 0 ? 0 : -1;
}

static int iso_next_token(const char** p, char out[VFS_MAX_NAME]) {
    if (!p || !*p || !out) return 0;

    const char* s = *p;
    while (*s == '/') ++s;
    if (*s == '\0') {
        *p = s;
        return 0;
    }

    size_t i = 0;
    while (*s != '\0' && *s != '/' && i + 1 < VFS_MAX_NAME) {
        out[i++] = *s++;
    }
    out[i] = '\0';

    while (*s == '/') ++s;
    *p = s;
    return i > 0 ? 1 : 0;
}

static int iso_scan_dir(const iso_fs_t* fs,
                        const iso_node_t* dir,
                        int (*on_entry)(const uint8_t* rec, void* user),
                        void* user) {
    if (!fs || !dir || !on_entry) return -1;
    if ((dir->flags & ISO_FLAG_DIRECTORY) == 0) return -1;

    uint8_t sec[ISO_SECTOR_SIZE];
    uint32_t remaining = dir->size;
    uint32_t cur_lba = dir->lba;

    while (remaining > 0) {
        if (iso_read_sector(fs, cur_lba, sec) != 0) return -1;

        uint32_t off = 0;
        while (off < ISO_SECTOR_SIZE) {
            uint8_t len = sec[off];
            if (len == 0) break;
            if ((uint32_t)off + len > ISO_SECTOR_SIZE) break;
            if (len < 34) break;

            int cb = on_entry(&sec[off], user);
            if (cb != 0) return cb;

            off += len;
        }

        cur_lba++;
        remaining = (remaining > ISO_SECTOR_SIZE) ? (remaining - ISO_SECTOR_SIZE) : 0;
    }

    return 0;
}

typedef struct {
    const iso_fs_t* fs;
    const char* name;
    iso_node_t out;
    int found;
} iso_find_ctx_t;

static int iso_find_cb(const uint8_t* rec, void* user) {
    iso_find_ctx_t* ctx = (iso_find_ctx_t*)user;
    char n[VFS_MAX_NAME];

    if (iso_decode_record_name(ctx->fs, rec, n) != 0) return 0;
    if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) return 0;

    if (!iso_name_eq_ci(n, ctx->name)) return 0;

    ctx->out.lba = iso_rd32_le(&rec[2]);
    ctx->out.size = iso_rd32_le(&rec[10]);
    ctx->out.flags = rec[25];
    ctx->found = 1;
    return 1;
}

static int iso_find_in_dir(const iso_fs_t* fs, const iso_node_t* dir, const char* name, iso_node_t* out) {
    iso_find_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fs = fs;
    ctx.name = name;

    int rc = iso_scan_dir(fs, dir, iso_find_cb, &ctx);
    if (rc == 1 && ctx.found) {
        if (out) *out = ctx.out;
        return 0;
    }
    return -1;
}

static int iso_resolve_path(const iso_fs_t* fs, const char* path, iso_node_t* out) {
    if (!fs || !path || path[0] != '/') return -1;

    iso_node_t cur;
    cur.lba = fs->root_lba;
    cur.size = fs->root_size;
    cur.flags = ISO_FLAG_DIRECTORY;

    const char* p = path;
    char tok[VFS_MAX_NAME];
    int has = iso_next_token(&p, tok);
    if (!has) {
        if (out) *out = cur;
        return 0;
    }

    while (has) {
        if ((cur.flags & ISO_FLAG_DIRECTORY) == 0) return -1;

        iso_node_t next;
        if (iso_find_in_dir(fs, &cur, tok, &next) != 0) return -1;
        cur = next;

        has = iso_next_token(&p, tok);
    }

    if (out) *out = cur;
    return 0;
}

typedef struct {
    const iso_fs_t* fs;
    vfs_dirent_t* out;
    size_t max_entries;
    size_t count;
} iso_list_ctx_t;

static int iso_list_cb(const uint8_t* rec, void* user) {
    iso_list_ctx_t* ctx = (iso_list_ctx_t*)user;
    char name[VFS_MAX_NAME];

    if (iso_decode_record_name(ctx->fs, rec, name) != 0) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;

    if (ctx->out && ctx->count < ctx->max_entries) {
        memset(&ctx->out[ctx->count], 0, sizeof(vfs_dirent_t));
        strncpy(ctx->out[ctx->count].name, name, VFS_MAX_NAME - 1);
        ctx->out[ctx->count].name[VFS_MAX_NAME - 1] = '\0';
        ctx->out[ctx->count].is_dir = (rec[25] & ISO_FLAG_DIRECTORY) != 0;
        ctx->out[ctx->count].size = iso_rd32_le(&rec[10]);
    }

    ctx->count++;
    return 0;
}

static int iso_op_mkdir(void* ctx, const char* path) {
    (void)ctx;
    (void)path;
    return -1;
}

static int iso_op_create_file(void* ctx, const char* path, const void* data, size_t len) {
    (void)ctx;
    (void)path;
    (void)data;
    (void)len;
    return -1;
}

static int iso_op_write_file(void* ctx, const char* path, const void* data, size_t len) {
    (void)ctx;
    (void)path;
    (void)data;
    (void)len;
    return -1;
}

static int iso_op_read_file(void* ctx, const char* path, void* out, size_t out_cap, size_t* out_len) {
    iso_fs_t* fs = (iso_fs_t*)ctx;
    if (!fs || !fs->mounted) return -1;

    iso_node_t node;
    if (iso_resolve_path(fs, path, &node) != 0) return -1;
    if ((node.flags & ISO_FLAG_DIRECTORY) != 0) return -2;

    if (out_len) *out_len = node.size;

    if (out == NULL) {
        return 0;
    }

    if (out_cap < node.size) {
        return -3;
    }

    uint8_t* dst = (uint8_t*)out;
    uint32_t copied = 0;
    uint32_t lba = node.lba;

    if (node.size >= ISO_SECTOR_SIZE) {
        uint32_t full = node.size / ISO_SECTOR_SIZE;
        if (iso_read_sectors_batch(fs, lba, full, dst) != 0) return -4;
        copied += full * ISO_SECTOR_SIZE;
        lba += full;
    }
    if (copied < node.size) {
        uint8_t sec[ISO_SECTOR_SIZE];
        if (iso_read_sector(fs, lba, sec) != 0) return -4;
        uint32_t tail = node.size - copied;
        memcpy(dst + copied, sec, tail);
        copied += tail;
    }

    return 0;
}

int iso_fs_read_file_range(const iso_fs_t* fs, const char* path, size_t offset, void* out, size_t len, size_t* out_read, size_t* out_file_len) {
    if (!fs || !path || !out) return -1;

    iso_node_t node;
    if (iso_resolve_path(fs, path, &node) != 0) return -1;
    if ((node.flags & ISO_FLAG_DIRECTORY) != 0) return -2;

    if (out_file_len) *out_file_len = node.size;
    if (offset >= node.size) {
        if (out_read) *out_read = 0;
        return 0;
    }

    size_t remaining = node.size - offset;
    size_t to_copy = len < remaining ? len : remaining;
    size_t copied = 0;

    uint32_t lba = node.lba + (uint32_t)(offset / ISO_SECTOR_SIZE);
    size_t sec_off = offset % ISO_SECTOR_SIZE;

    if (sec_off != 0) {
        uint8_t sec[ISO_SECTOR_SIZE];
        if (iso_read_sector(fs, lba, sec) != 0) return -4;
        size_t avail = ISO_SECTOR_SIZE - sec_off;
        size_t chunk = (to_copy < avail) ? to_copy : avail;
        memcpy((uint8_t*)out + copied, sec + sec_off, chunk);
        copied += chunk;
        lba++;
        sec_off = 0;
    }

    if (to_copy > copied) {
        size_t remaining = to_copy - copied;
        uint32_t full = (uint32_t)(remaining / ISO_SECTOR_SIZE);
        if (full > 0) {
            if (iso_read_sectors_batch(fs, lba, full, (uint8_t*)out + copied) != 0) return -4;
            copied += (size_t)full * ISO_SECTOR_SIZE;
            lba += full;
        }
        remaining = to_copy - copied;
        if (remaining > 0) {
            uint8_t sec[ISO_SECTOR_SIZE];
            if (iso_read_sector(fs, lba, sec) != 0) return -4;
            memcpy((uint8_t*)out + copied, sec, remaining);
            copied += remaining;
        }
    }

    if (out_read) *out_read = copied;
    return 0;
}

static int iso_op_list_dir(void* ctx, const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count) {
    iso_fs_t* fs = (iso_fs_t*)ctx;
    if (!fs || !fs->mounted) return -1;

    iso_node_t dir;
    if (iso_resolve_path(fs, path, &dir) != 0) return -1;
    if ((dir.flags & ISO_FLAG_DIRECTORY) == 0) return -2;

    iso_list_ctx_t lctx;
    memset(&lctx, 0, sizeof(lctx));
    lctx.fs = fs;
    lctx.out = out;
    lctx.max_entries = max_entries;

    if (iso_scan_dir(fs, &dir, iso_list_cb, &lctx) != 0) return -1;

    if (out_count) *out_count = lctx.count;
    return 0;
}

static int iso_op_exists(void* ctx, const char* path) {
    iso_fs_t* fs = (iso_fs_t*)ctx;
    if (!fs || !fs->mounted) return 0;

    iso_node_t node;
    return iso_resolve_path(fs, path, &node) == 0 ? 1 : 0;
}

static int iso_op_remove(void* ctx, const char* path) {
    (void)ctx;
    (void)path;
    return -1;
}

static int iso_op_rename(void* ctx, const char* old_path, const char* new_path) {
    (void)ctx;
    (void)old_path;
    (void)new_path;
    return -1;
}

int iso_fs_mount(iso_fs_t* fs, uint8_t drive_index, uint64_t partition_lba) {
    if (!fs) return -1;

    memset(fs, 0, sizeof(*fs));

    const ata_drive_t* drv = ata_get_drive(drive_index);
    if (!drv || !drv->present) return -1;

    fs->drive_index = drive_index;
    fs->partition_lba = partition_lba;
    fs->is_atapi = (drv->type == ATA_DRIVE_ATAPI || drv->type == ATA_DRIVE_SATAPI);

    uint8_t vd[ISO_SECTOR_SIZE];
    int found_pvd = 0;
    int found_joliet = 0;
    uint32_t pvd_lba = 0, pvd_size = 0;
    uint32_t joliet_lba = 0, joliet_size = 0;

    for (uint32_t i = 0; i < 64; ++i) {
        int sec_rc = iso_read_sector(fs, ISO_PVD_SECTOR + i, vd);
        if (sec_rc != 0) {
            kprintf("[iso] read sector 16+");
            kprint_uint(i);
            kprintf(" failed rc=");
            kprint_int(sec_rc);
            kprintf("\n");
            return -1;
        }
        if (memcmp(&vd[1], "CD001", 5) != 0) break;

        uint8_t type = vd[0];
        if (type == 0x01) {
            if (vd[6] != 0x01) return -1;
            const uint8_t* root = &vd[156];
            if (root[0] < 34) return -1;
            pvd_lba = iso_rd32_le(&root[2]);
            pvd_size = iso_rd32_le(&root[10]);
            found_pvd = 1;
        } else if (type == 0x02) {
            // Joliet supplementary volume descriptor
            if (vd[88] == '%' && vd[89] == '/' && (vd[90] == '@' || vd[90] == 'C' || vd[90] == 'E')) {
                const uint8_t* root = &vd[156];
                if (root[0] >= 34) {
                    joliet_lba = iso_rd32_le(&root[2]);
                    joliet_size = iso_rd32_le(&root[10]);
                    found_joliet = 1;
                }
            }
        } else if (type == 0xFF) {
            break;
        }
    }

    if (!found_pvd && !found_joliet) return -1;
    if (found_joliet) {
        fs->root_lba = joliet_lba;
        fs->root_size = joliet_size;
        fs->use_joliet = true;
    } else {
        fs->root_lba = pvd_lba;
        fs->root_size = pvd_size;
        fs->use_joliet = false;
    }
    fs->mounted = true;
    return 0;
}

static const vfs_backend_ops_t g_iso_ops = {
    .mkdir = iso_op_mkdir,
    .create_file = iso_op_create_file,
    .write_file = iso_op_write_file,
    .read_file = iso_op_read_file,
    .list_dir = iso_op_list_dir,
    .exists = iso_op_exists,
    .remove = iso_op_remove,
    .rename = iso_op_rename,
};

const vfs_backend_ops_t* iso_fs_backend_ops(void) {
    return &g_iso_ops;
}

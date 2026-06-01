#include <fs/ext4.h>

#include <drivers/sata/ata.h>
#include <lib/string.h>

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
} ext4_superblock_t;

typedef struct __attribute__((packed)) {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint8_t reserved[12];
} ext4_bg_desc_t;

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
} ext4_inode_t;

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} ext4_dir_entry_t;

#define EXT4_S_IFDIR 0x4000
#define EXT4_S_IFREG 0x8000
#define EXT4_FT_REG_FILE 1
#define EXT4_FT_DIR 2
#define EXT4_EXTENTS_FL 0x00080000u
#define EXT4_EXT_MAGIC 0xF30Au

typedef struct __attribute__((packed)) {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} ext4_extent_header_t;

typedef struct __attribute__((packed)) {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} ext4_extent_t;

typedef struct __attribute__((packed)) {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} ext4_extent_idx_t;

static int ext4_read_block(const ext4_fs_t* fs, uint32_t block, uint8_t* out);

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

static uint16_t ext4_align4(uint16_t v) {
    return (uint16_t)((v + 3u) & ~3u);
}

static int ext4_inode_uses_extents(const ext4_inode_t* inode) {
    return (inode->flags & EXT4_EXTENTS_FL) != 0;
}

static ext4_extent_header_t* ext4_inode_extent_hdr(ext4_inode_t* inode) {
    return (ext4_extent_header_t*)inode->block;
}

static const ext4_extent_header_t* ext4_inode_extent_hdr_const(const ext4_inode_t* inode) {
    return (const ext4_extent_header_t*)inode->block;
}

static void ext4_extent_init(ext4_inode_t* inode) {
    memset(inode->block, 0, sizeof(inode->block));
    ext4_extent_header_t* eh = ext4_inode_extent_hdr(inode);
    eh->eh_magic = EXT4_EXT_MAGIC;
    eh->eh_entries = 0;
    eh->eh_max = 4; /* 60 bytes i_block -> 12 bytes hdr + 4 extents */
    eh->eh_depth = 0;
    eh->eh_generation = 0;
}

static int ext4_extent_get_phys_block(const ext4_inode_t* inode, uint32_t lblock, uint32_t* out_block) {
    const ext4_extent_header_t* eh = ext4_inode_extent_hdr_const(inode);
    if (eh->eh_magic != EXT4_EXT_MAGIC) return -1;
    if (eh->eh_depth != 0) return -1;

    const ext4_extent_t* exts = (const ext4_extent_t*)(eh + 1);
    for (uint16_t i = 0; i < eh->eh_entries; ++i) {
        uint32_t start = exts[i].ee_block;
        uint32_t len = (uint32_t)(exts[i].ee_len & 0x7FFFu);
        if (len == 0) continue;
        if (lblock < start || lblock >= start + len) continue;
        uint64_t pstart = ((uint64_t)exts[i].ee_start_hi << 32) | exts[i].ee_start_lo;
        *out_block = (uint32_t)(pstart + (lblock - start));
        return 0;
    }
    return -1;
}

static int ext4_inode_get_data_block(const ext4_fs_t* fs, const ext4_inode_t* inode, uint32_t lblock, uint32_t* out_block) {
    if (ext4_inode_uses_extents(inode)) {
        return ext4_extent_get_phys_block(inode, lblock, out_block);
    }

    if (lblock < 12) {
        uint32_t b = inode->block[lblock];
        if (b == 0) return -1;
        *out_block = b;
        return 0;
    }

    uint32_t ind_idx = lblock - 12u;
    uint32_t ptrs = fs->block_size / 4u;
    if (ind_idx < ptrs) {
        if (inode->block[12] == 0) return -1;
        uint8_t ind[4096];
        if (ext4_read_block(fs, inode->block[12], ind) != 0) return -1;
        uint32_t b = rd32(&ind[ind_idx * 4]);
        if (b == 0) return -1;
        *out_block = b;
        return 0;
    }

    ind_idx -= ptrs;
    uint32_t ptrs2 = ptrs * ptrs;
    if (ind_idx >= ptrs2 || inode->block[13] == 0) return -1;

    uint8_t ind2[4096];
    if (ext4_read_block(fs, inode->block[13], ind2) != 0) return -1;
    uint32_t i = ind_idx / ptrs;
    uint32_t j = ind_idx % ptrs;
    uint32_t ind_block = rd32(&ind2[i * 4]);
    if (ind_block == 0) return -1;
    uint8_t ind[4096];
    if (ext4_read_block(fs, ind_block, ind) != 0) return -1;
    uint32_t b = rd32(&ind[j * 4]);
    if (b == 0) return -1;
    *out_block = b;
    return 0;
}

static int ext4_extent_append_single_block(ext4_inode_t* inode, uint32_t lblock, uint32_t pblock) {
    ext4_extent_header_t* eh = ext4_inode_extent_hdr(inode);
    if (eh->eh_magic != EXT4_EXT_MAGIC || eh->eh_depth != 0) return -1;
    if (eh->eh_entries >= eh->eh_max) return -1;

    ext4_extent_t* exts = (ext4_extent_t*)(eh + 1);
    ext4_extent_t* e = &exts[eh->eh_entries];
    e->ee_block = lblock;
    e->ee_len = 1;
    e->ee_start_hi = 0;
    e->ee_start_lo = pblock;
    eh->eh_entries++;
    return 0;
}

static int ext4_split_parent_leaf(const char* path, char* parent, size_t parent_cap, char* leaf, size_t leaf_cap) {
    if (!path || path[0] != '/') return -1;
    const char* slash = strrchr(path, '/');
    if (!slash || !slash[1]) return -1;

    size_t ll = strlen(slash + 1);
    if (ll == 0 || ll + 1 > leaf_cap) return -1;
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

static int ext4_read_super(ext4_fs_t* fs, ext4_superblock_t* out_sb) {
    uint8_t sec[ATA_SECTOR_SIZE];
    if (ata_read_sectors(fs->drive_index, (uint64_t)fs->partition_lba + 2, 1, sec) != 0) return -1;
    memcpy(out_sb, sec, sizeof(ext4_superblock_t));
    return 0;
}

static int ext4_write_super(ext4_fs_t* fs, const ext4_superblock_t* sb) {
    uint8_t sec[ATA_SECTOR_SIZE];
    if (ata_read_sectors(fs->drive_index, (uint64_t)fs->partition_lba + 2, 1, sec) != 0) return -1;
    memcpy(sec, sb, sizeof(ext4_superblock_t));
    if (ata_write_sectors(fs->drive_index, (uint64_t)fs->partition_lba + 2, 1, sec) != 0) return -1;
    return 0;
}

static int ext4_read_block(const ext4_fs_t* fs, uint32_t block, uint8_t* out) {
    uint32_t sectors = fs->block_size / ATA_SECTOR_SIZE;
    uint64_t lba = (uint64_t)fs->partition_lba + ((uint64_t)block * sectors);
    return ata_read_sectors(fs->drive_index, lba, (uint8_t)sectors, out);
}

static int ext4_write_block(const ext4_fs_t* fs, uint32_t block, const uint8_t* in) {
    uint32_t sectors = fs->block_size / ATA_SECTOR_SIZE;
    uint64_t lba = (uint64_t)fs->partition_lba + ((uint64_t)block * sectors);
    return ata_write_sectors(fs->drive_index, lba, (uint8_t)sectors, in);
}

static int ext4_read_bg_desc(const ext4_fs_t* fs, uint32_t group, ext4_bg_desc_t* out_desc) {
    uint32_t desc_table_block = (fs->block_size == 1024) ? 2 : 1;
    uint8_t buf[4096];
    if (ext4_read_block(fs, desc_table_block, buf) != 0) return -1;

    size_t off = group * sizeof(ext4_bg_desc_t);
    if (off + sizeof(ext4_bg_desc_t) > fs->block_size) return -2;
    memcpy(out_desc, &buf[off], sizeof(*out_desc));
    return 0;
}

static int ext4_write_bg_desc(const ext4_fs_t* fs, uint32_t group, const ext4_bg_desc_t* in_desc) {
    uint32_t desc_table_block = (fs->block_size == 1024) ? 2 : 1;
    uint8_t buf[4096];
    if (ext4_read_block(fs, desc_table_block, buf) != 0) return -1;

    size_t off = group * sizeof(ext4_bg_desc_t);
    if (off + sizeof(ext4_bg_desc_t) > fs->block_size) return -2;
    memcpy(&buf[off], in_desc, sizeof(*in_desc));
    if (ext4_write_block(fs, desc_table_block, buf) != 0) return -3;
    return 0;
}

static int ext4_read_inode(const ext4_fs_t* fs, uint32_t inode_no, ext4_inode_t* out_inode) {
    if (inode_no == 0) return -1;

    uint32_t idx = inode_no - 1;
    uint32_t group = idx / fs->inodes_per_group;
    uint32_t index_in_group = idx % fs->inodes_per_group;

    ext4_bg_desc_t bg;
    if (ext4_read_bg_desc(fs, group, &bg) != 0) return -2;

    uint32_t inode_off = index_in_group * fs->inode_size;
    uint32_t block_off = inode_off / fs->block_size;
    uint32_t inner_off = inode_off % fs->block_size;

    uint8_t buf[4096];
    if (ext4_read_block(fs, bg.inode_table + block_off, buf) != 0) return -3;
    if (inner_off + sizeof(ext4_inode_t) > fs->block_size) return -4;

    memcpy(out_inode, &buf[inner_off], sizeof(ext4_inode_t));
    return 0;
}

static int ext4_write_inode(const ext4_fs_t* fs, uint32_t inode_no, const ext4_inode_t* inode) {
    if (inode_no == 0) return -1;

    uint32_t idx = inode_no - 1;
    uint32_t group = idx / fs->inodes_per_group;
    uint32_t index_in_group = idx % fs->inodes_per_group;

    ext4_bg_desc_t bg;
    if (ext4_read_bg_desc(fs, group, &bg) != 0) return -2;

    uint32_t inode_off = index_in_group * fs->inode_size;
    uint32_t block_off = inode_off / fs->block_size;
    uint32_t inner_off = inode_off % fs->block_size;

    uint8_t buf[4096];
    if (ext4_read_block(fs, bg.inode_table + block_off, buf) != 0) return -3;
    if (inner_off + sizeof(ext4_inode_t) > fs->block_size) return -4;

    memcpy(&buf[inner_off], inode, sizeof(ext4_inode_t));
    if (ext4_write_block(fs, bg.inode_table + block_off, buf) != 0) return -5;
    return 0;
}

static int ext4_test_bit(const uint8_t* map, uint32_t bit) {
    return (map[bit / 8u] >> (bit % 8u)) & 1u;
}

static void ext4_set_bit(uint8_t* map, uint32_t bit) {
    map[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
}

static void ext4_clear_bit(uint8_t* map, uint32_t bit) {
    map[bit / 8u] &= (uint8_t)~(1u << (bit % 8u));
}

static int ext4_alloc_inode(ext4_fs_t* fs, uint32_t* out_ino, int is_dir) {
    ext4_superblock_t sb;
    if (ext4_read_super(fs, &sb) != 0) return -1;

    uint32_t first_ino = sb.first_ino ? sb.first_ino : 11;
    uint8_t map[4096];

    for (uint32_t g = 0; g < fs->group_count; ++g) {
        ext4_bg_desc_t bg;
        if (ext4_read_bg_desc(fs, g, &bg) != 0) return -1;
        if (bg.free_inodes_count == 0) continue;

        if (ext4_read_block(fs, bg.inode_bitmap, map) != 0) return -1;

        for (uint32_t b = 0; b < fs->inodes_per_group; ++b) {
            uint32_t ino = g * fs->inodes_per_group + b + 1u;
            if (ino > fs->inodes_count) break;
            if (ino < first_ino && ino != 2u) continue;
            if (ext4_test_bit(map, b)) continue;

            ext4_set_bit(map, b);
            if (ext4_write_block(fs, bg.inode_bitmap, map) != 0) return -1;

            if (bg.free_inodes_count > 0) bg.free_inodes_count--;
            if (is_dir) bg.used_dirs_count++;
            if (ext4_write_bg_desc(fs, g, &bg) != 0) return -1;

            if (sb.free_inodes_count > 0) sb.free_inodes_count--;
            if (ext4_write_super(fs, &sb) != 0) return -1;

            *out_ino = ino;
            return 0;
        }
    }

    return -1;
}

static int ext4_alloc_block(ext4_fs_t* fs, uint32_t* out_block) {
    ext4_superblock_t sb;
    if (ext4_read_super(fs, &sb) != 0) return -1;

    uint8_t map[4096];

    for (uint32_t g = 0; g < fs->group_count; ++g) {
        ext4_bg_desc_t bg;
        if (ext4_read_bg_desc(fs, g, &bg) != 0) return -1;
        if (bg.free_blocks_count == 0) continue;

        if (ext4_read_block(fs, bg.block_bitmap, map) != 0) return -1;

        for (uint32_t b = 0; b < fs->blocks_per_group; ++b) {
            uint32_t blk = fs->first_data_block + g * fs->blocks_per_group + b;
            if (blk >= fs->blocks_count) break;
            if (ext4_test_bit(map, b)) continue;

            ext4_set_bit(map, b);
            if (ext4_write_block(fs, bg.block_bitmap, map) != 0) return -1;

            if (bg.free_blocks_count > 0) bg.free_blocks_count--;
            if (ext4_write_bg_desc(fs, g, &bg) != 0) return -1;

            if (sb.free_blocks_count > 0) sb.free_blocks_count--;
            if (ext4_write_super(fs, &sb) != 0) return -1;

            uint8_t zero[4096];
            memset(zero, 0, fs->block_size);
            if (ext4_write_block(fs, blk, zero) != 0) return -1;

            *out_block = blk;
            return 0;
        }
    }

    return -1;
}

static int ext4_free_block(ext4_fs_t* fs, uint32_t block) {
    if (block == 0) return 0;
    if (block >= fs->blocks_count) return -1;

    uint32_t rel = block - fs->first_data_block;
    uint32_t g = rel / fs->blocks_per_group;
    uint32_t b = rel % fs->blocks_per_group;

    ext4_bg_desc_t bg;
    if (ext4_read_bg_desc(fs, g, &bg) != 0) return -1;

    uint8_t map[4096];
    if (ext4_read_block(fs, bg.block_bitmap, map) != 0) return -1;
    if (!ext4_test_bit(map, b)) return 0;

    ext4_clear_bit(map, b);
    if (ext4_write_block(fs, bg.block_bitmap, map) != 0) return -1;

    bg.free_blocks_count++;
    if (ext4_write_bg_desc(fs, g, &bg) != 0) return -1;

    ext4_superblock_t sb;
    if (ext4_read_super(fs, &sb) != 0) return -1;
    sb.free_blocks_count++;
    if (ext4_write_super(fs, &sb) != 0) return -1;

    return 0;
}

static int ext4_free_inode(ext4_fs_t* fs, uint32_t ino, int was_dir) {
    if (ino == 0 || ino > fs->inodes_count) return -1;

    uint32_t idx = ino - 1;
    uint32_t g = idx / fs->inodes_per_group;
    uint32_t b = idx % fs->inodes_per_group;

    ext4_bg_desc_t bg;
    if (ext4_read_bg_desc(fs, g, &bg) != 0) return -1;

    uint8_t map[4096];
    if (ext4_read_block(fs, bg.inode_bitmap, map) != 0) return -1;
    if (!ext4_test_bit(map, b)) return 0;

    ext4_clear_bit(map, b);
    if (ext4_write_block(fs, bg.inode_bitmap, map) != 0) return -1;

    bg.free_inodes_count++;
    if (was_dir && bg.used_dirs_count > 0) bg.used_dirs_count--;
    if (ext4_write_bg_desc(fs, g, &bg) != 0) return -1;

    ext4_superblock_t sb;
    if (ext4_read_super(fs, &sb) != 0) return -1;
    sb.free_inodes_count++;
    if (ext4_write_super(fs, &sb) != 0) return -1;

    return 0;
}

static int ext4_name_equals(const char* a, const char* b, size_t blen) {
    size_t alen = strlen(a);
    if (alen != blen) return 0;
    for (size_t i = 0; i < blen; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int ext4_dir_find(const ext4_fs_t* fs, const ext4_inode_t* dir, const char* name, uint32_t* out_ino, uint8_t* out_ftype) {
    uint8_t buf[4096];
    uint32_t blocks = (dir->size + fs->block_size - 1u) / fs->block_size;
    for (uint32_t lbi = 0; lbi < blocks; ++lbi) {
        uint32_t block = 0;
        if (ext4_inode_get_data_block(fs, dir, lbi, &block) != 0) continue;
        if (ext4_read_block(fs, block, buf) != 0) return -1;

        size_t off = 0;
        while (off + 8 <= fs->block_size) {
            ext4_dir_entry_t* d = (ext4_dir_entry_t*)&buf[off];
            if (d->rec_len < 8) break;
            if (d->inode != 0 && d->name_len > 0) {
                if (ext4_name_equals(name, d->name, d->name_len)) {
                    if (out_ino) *out_ino = d->inode;
                    if (out_ftype) *out_ftype = d->file_type;
                    return 0;
                }
            }
            off += d->rec_len;
        }
    }

    return -2;
}

static int ext4_dir_find_slot(
    const ext4_fs_t* fs,
    const ext4_inode_t* dir,
    const char* name,
    uint32_t* out_block,
    uint16_t* out_off,
    uint16_t* out_prev_off,
    ext4_dir_entry_t* out_entry
) {
    uint8_t buf[4096];
    uint32_t blocks = (dir->size + fs->block_size - 1u) / fs->block_size;
    for (uint32_t lbi = 0; lbi < blocks; ++lbi) {
        uint32_t block = 0;
        if (ext4_inode_get_data_block(fs, dir, lbi, &block) != 0) continue;
        if (ext4_read_block(fs, block, buf) != 0) return -1;

        uint16_t off = 0;
        uint16_t prev = 0xFFFFu;
        while (off + 8 <= fs->block_size) {
            ext4_dir_entry_t* d = (ext4_dir_entry_t*)&buf[off];
            if (d->rec_len < 8) break;
            if (d->inode != 0 && d->name_len > 0 && ext4_name_equals(name, d->name, d->name_len)) {
                if (out_block) *out_block = block;
                if (out_off) *out_off = off;
                if (out_prev_off) *out_prev_off = prev;
                if (out_entry) *out_entry = *d;
                return 0;
            }
            prev = off;
            off = (uint16_t)(off + d->rec_len);
        }
    }
    return -2;
}

static int ext4_resolve_path(const ext4_fs_t* fs, const char* path, ext4_inode_t* out_inode, uint32_t* out_ino) {
    if (!path || path[0] != '/') return -1;

    uint32_t cur_ino = fs->root_inode;
    ext4_inode_t cur;
    if (ext4_read_inode(fs, cur_ino, &cur) != 0) return -2;

    if (path[1] == '\0') {
        if (out_inode) *out_inode = cur;
        if (out_ino) *out_ino = cur_ino;
        return 0;
    }

    const char* p = path;
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

        if ((cur.mode & EXT4_S_IFDIR) == 0) return -3;

        uint32_t next_ino = 0;
        if (ext4_dir_find(fs, &cur, token, &next_ino, NULL) != 0) return -4;
        if (ext4_read_inode(fs, next_ino, &cur) != 0) return -5;
        cur_ino = next_ino;
    }

    if (out_inode) *out_inode = cur;
    if (out_ino) *out_ino = cur_ino;
    return 0;
}

static int ext4_read_file_data(const ext4_fs_t* fs, const ext4_inode_t* inode, void* out, size_t out_cap, size_t* out_len) {
    if ((inode->mode & EXT4_S_IFREG) == 0) return -1;

    uint32_t size = inode->size;
    if (out_len) *out_len = size;
    if (out == NULL) return 0;
    if (out_cap < size) return -2;

    uint8_t* dst = (uint8_t*)out;
    uint32_t remain = size;
    uint32_t lbi = 0;
    uint8_t buf[4096];

    while (remain > 0) {
        uint32_t block = 0;
        if (ext4_inode_get_data_block(fs, inode, lbi++, &block) != 0) return -3;
        if (ext4_read_block(fs, block, buf) != 0) return -4;
        uint32_t take = remain < fs->block_size ? remain : fs->block_size;
        memcpy(dst, buf, take);
        dst += take;
        remain -= take;
    }

    return remain == 0 ? 0 : -6;
}

static int ext4_inode_clear_data(ext4_fs_t* fs, ext4_inode_t* inode) {
    if (ext4_inode_uses_extents(inode)) {
        const ext4_extent_header_t* eh = ext4_inode_extent_hdr_const(inode);
        if (eh->eh_magic != EXT4_EXT_MAGIC || eh->eh_depth != 0) return -1;
        const ext4_extent_t* exts = (const ext4_extent_t*)(eh + 1);
        for (uint16_t i = 0; i < eh->eh_entries; ++i) {
            uint32_t len = (uint32_t)(exts[i].ee_len & 0x7FFFu);
            uint64_t pstart = ((uint64_t)exts[i].ee_start_hi << 32) | exts[i].ee_start_lo;
            for (uint32_t j = 0; j < len; ++j) {
                if (ext4_free_block(fs, (uint32_t)(pstart + j)) != 0) return -1;
            }
        }
        ext4_extent_init(inode);
        inode->size = 0;
        inode->blocks = 0;
        return 0;
    }

    for (int i = 0; i < 12; ++i) {
        if (inode->block[i] != 0) {
            if (ext4_free_block(fs, inode->block[i]) != 0) return -1;
            inode->block[i] = 0;
        }
    }

    if (inode->block[12] != 0) {
        uint8_t ind[4096];
        if (ext4_read_block(fs, inode->block[12], ind) != 0) return -1;
        uint32_t ptrs = fs->block_size / 4;
        for (uint32_t i = 0; i < ptrs; ++i) {
            uint32_t b = rd32(&ind[i * 4]);
            if (b != 0) {
                if (ext4_free_block(fs, b) != 0) return -1;
            }
        }
        if (ext4_free_block(fs, inode->block[12]) != 0) return -1;
        inode->block[12] = 0;
    }

    if (inode->block[13] != 0) {
        uint8_t ind2[4096];
        if (ext4_read_block(fs, inode->block[13], ind2) != 0) return -1;
        uint32_t ptrs = fs->block_size / 4;
        for (uint32_t i = 0; i < ptrs; ++i) {
            uint32_t ind_block = rd32(&ind2[i * 4]);
            if (ind_block == 0) continue;
            uint8_t ind[4096];
            if (ext4_read_block(fs, ind_block, ind) != 0) return -1;
            for (uint32_t j = 0; j < ptrs; ++j) {
                uint32_t b = rd32(&ind[j * 4]);
                if (b != 0) {
                    if (ext4_free_block(fs, b) != 0) return -1;
                }
            }
            if (ext4_free_block(fs, ind_block) != 0) return -1;
        }
        if (ext4_free_block(fs, inode->block[13]) != 0) return -1;
        inode->block[13] = 0;
    }

    inode->size = 0;
    inode->blocks = 0;
    return 0;
}

static int ext4_inode_write_data(ext4_fs_t* fs, ext4_inode_t* inode, const void* data, size_t len) {
    size_t original_len = len;
    if (ext4_inode_clear_data(fs, inode) != 0) return -1;

    if (len == 0) {
        inode->size = 0;
        inode->blocks = 0;
        return 0;
    }

    const uint8_t* src = (const uint8_t*)data;
    uint32_t needed = (uint32_t)((len + fs->block_size - 1u) / fs->block_size);
    uint32_t ptrs = fs->block_size / 4u;
    uint32_t max_blocks = 12u + ptrs + (ptrs * ptrs);
    if (needed > max_blocks) return -1;

    inode->flags &= ~EXT4_EXTENTS_FL;

    uint32_t total_blocks_alloc = 0;
    uint8_t buf[4096];

    uint32_t i = 0;
    for (; i < needed && i < 12u; ++i) {
        uint32_t b = 0;
        if (ext4_alloc_block(fs, &b) != 0) return -1;
        inode->block[i] = b;
        total_blocks_alloc++;

        memset(buf, 0, fs->block_size);
        size_t take = len < fs->block_size ? len : fs->block_size;
        memcpy(buf, src, take);
        src += take;
        len -= take;
        if (ext4_write_block(fs, b, buf) != 0) return -1;
    }

    if (needed > 12u) {
        uint32_t ind_block = 0;
        if (ext4_alloc_block(fs, &ind_block) != 0) return -1;
        inode->block[12] = ind_block;
        total_blocks_alloc++;

        memset(buf, 0, fs->block_size);
        uint32_t extra = needed - 12u;
        uint32_t single = (extra > ptrs) ? ptrs : extra;
        for (uint32_t j = 0; j < single; ++j) {
            uint32_t b = 0;
            if (ext4_alloc_block(fs, &b) != 0) return -1;
            wr32(&buf[j * 4], b);
            total_blocks_alloc++;

            uint8_t dbuf[4096];
            memset(dbuf, 0, fs->block_size);
            size_t take = len < fs->block_size ? len : fs->block_size;
            memcpy(dbuf, src, take);
            src += take;
            len -= take;
            if (ext4_write_block(fs, b, dbuf) != 0) return -1;
        }

        if (ext4_write_block(fs, ind_block, buf) != 0) return -1;
    }

    if (needed > 12u + ptrs) {
        uint32_t ind2_block = 0;
        if (ext4_alloc_block(fs, &ind2_block) != 0) return -1;
        inode->block[13] = ind2_block;
        total_blocks_alloc++;

        uint8_t ind2[4096];
        memset(ind2, 0, fs->block_size);
        uint32_t remaining = needed - 12u - ptrs;
        uint32_t idx = 0;
        while (remaining > 0) {
            uint32_t ind_block = 0;
            if (ext4_alloc_block(fs, &ind_block) != 0) return -1;
            wr32(&ind2[idx * 4], ind_block);
            total_blocks_alloc++;

            uint8_t ind[4096];
            memset(ind, 0, fs->block_size);
            uint32_t take_blocks = remaining > ptrs ? ptrs : remaining;
            for (uint32_t j = 0; j < take_blocks; ++j) {
                uint32_t b = 0;
                if (ext4_alloc_block(fs, &b) != 0) return -1;
                wr32(&ind[j * 4], b);
                total_blocks_alloc++;

                uint8_t dbuf[4096];
                memset(dbuf, 0, fs->block_size);
                size_t take = len < fs->block_size ? len : fs->block_size;
                memcpy(dbuf, src, take);
                src += take;
                len -= take;
                if (ext4_write_block(fs, b, dbuf) != 0) return -1;
            }

            if (ext4_write_block(fs, ind_block, ind) != 0) return -1;
            remaining -= take_blocks;
            idx++;
        }

        if (ext4_write_block(fs, ind2_block, ind2) != 0) return -1;
    }

    inode->size = (uint32_t)original_len;
    inode->blocks = total_blocks_alloc * (fs->block_size / 512u);
    return 0;
}

static int ext4_fill_block_reader(int (*reader)(void* user, size_t offset, void* out, size_t len),
                                  void* user, size_t* offset, size_t* remaining,
                                  uint8_t* out, size_t block_size) {
    if (!out || !offset || !remaining) return -1;
    memset(out, 0, block_size);
    size_t take = (*remaining < block_size) ? *remaining : block_size;
    if (take == 0) return 0;
    if (!reader) return -1;
    if (reader(user, *offset, out, take) != 0) return -1;
    *offset += take;
    *remaining -= take;
    return 0;
}

static int ext4_inode_write_data_reader(ext4_fs_t* fs, ext4_inode_t* inode, size_t len,
                                        int (*reader)(void* user, size_t offset, void* out, size_t len),
                                        void* user) {
    size_t original_len = len;
    if (ext4_inode_clear_data(fs, inode) != 0) return -1;

    if (len == 0) {
        inode->size = 0;
        inode->blocks = 0;
        return 0;
    }

    if (!reader) return -1;

    uint32_t needed = (uint32_t)((len + fs->block_size - 1u) / fs->block_size);
    uint32_t ptrs = fs->block_size / 4u;
    uint32_t max_blocks = 12u + ptrs + (ptrs * ptrs);
    if (needed > max_blocks) return -1;

    inode->flags &= ~EXT4_EXTENTS_FL;

    uint32_t total_blocks_alloc = 0;
    uint8_t buf[4096];
    uint8_t dbuf[4096];
    size_t offset = 0;
    size_t remaining = len;

    uint32_t i = 0;
    for (; i < needed && i < 12u; ++i) {
        uint32_t b = 0;
        if (ext4_alloc_block(fs, &b) != 0) return -1;
        inode->block[i] = b;
        total_blocks_alloc++;

        if (ext4_fill_block_reader(reader, user, &offset, &remaining, dbuf, fs->block_size) != 0) return -1;
        if (ext4_write_block(fs, b, dbuf) != 0) return -1;
    }

    if (needed > 12u) {
        uint32_t ind_block = 0;
        if (ext4_alloc_block(fs, &ind_block) != 0) return -1;
        inode->block[12] = ind_block;
        total_blocks_alloc++;

        memset(buf, 0, fs->block_size);
        uint32_t extra = needed - 12u;
        uint32_t single = (extra > ptrs) ? ptrs : extra;
        for (uint32_t j = 0; j < single; ++j) {
            uint32_t b = 0;
            if (ext4_alloc_block(fs, &b) != 0) return -1;
            wr32(&buf[j * 4], b);
            total_blocks_alloc++;

            if (ext4_fill_block_reader(reader, user, &offset, &remaining, dbuf, fs->block_size) != 0) return -1;
            if (ext4_write_block(fs, b, dbuf) != 0) return -1;
        }

        if (ext4_write_block(fs, ind_block, buf) != 0) return -1;
    }

    if (needed > 12u + ptrs) {
        uint32_t ind2_block = 0;
        if (ext4_alloc_block(fs, &ind2_block) != 0) return -1;
        inode->block[13] = ind2_block;
        total_blocks_alloc++;

        uint8_t ind2[4096];
        memset(ind2, 0, fs->block_size);
        uint32_t remaining_blocks = needed - 12u - ptrs;
        uint32_t idx = 0;
        while (remaining_blocks > 0) {
            uint32_t ind_block = 0;
            if (ext4_alloc_block(fs, &ind_block) != 0) return -1;
            wr32(&ind2[idx * 4], ind_block);
            total_blocks_alloc++;

            uint8_t ind[4096];
            memset(ind, 0, fs->block_size);
            uint32_t take_blocks = remaining_blocks > ptrs ? ptrs : remaining_blocks;
            for (uint32_t j = 0; j < take_blocks; ++j) {
                uint32_t b = 0;
                if (ext4_alloc_block(fs, &b) != 0) return -1;
                wr32(&ind[j * 4], b);
                total_blocks_alloc++;

                if (ext4_fill_block_reader(reader, user, &offset, &remaining, dbuf, fs->block_size) != 0) return -1;
                if (ext4_write_block(fs, b, dbuf) != 0) return -1;
            }

            if (ext4_write_block(fs, ind_block, ind) != 0) return -1;
            remaining_blocks -= take_blocks;
            idx++;
        }

        if (ext4_write_block(fs, ind2_block, ind2) != 0) return -1;
    }

    inode->size = (uint32_t)original_len;
    inode->blocks = total_blocks_alloc * (fs->block_size / 512u);
    return 0;
}

static int ext4_dir_add_entry(ext4_fs_t* fs, uint32_t parent_ino, ext4_inode_t* parent_inode, uint32_t child_ino, uint8_t file_type, const char* name) {
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > 255) return -1;
    uint16_t need = ext4_align4((uint16_t)(8u + (uint16_t)name_len));

    uint8_t buf[4096];

    uint32_t blocks = (parent_inode->size + fs->block_size - 1u) / fs->block_size;
    for (uint32_t lbi = 0; lbi < blocks; ++lbi) {
        uint32_t block = 0;
        if (ext4_inode_get_data_block(fs, parent_inode, lbi, &block) != 0) continue;
        if (ext4_read_block(fs, block, buf) != 0) return -1;

        uint16_t off = 0;
        while (off + 8 <= fs->block_size) {
            ext4_dir_entry_t* d = (ext4_dir_entry_t*)&buf[off];
            if (d->rec_len < 8) break;

            if (d->inode == 0 && d->rec_len >= need) {
                d->inode = child_ino;
                d->name_len = (uint8_t)name_len;
                d->file_type = file_type;
                memcpy(d->name, name, name_len);
                if (ext4_write_block(fs, block, buf) != 0) return -1;
                return 0;
            }

            uint16_t used = ext4_align4((uint16_t)(8u + d->name_len));
            if (d->rec_len >= used + need) {
                uint16_t old = d->rec_len;
                d->rec_len = used;

                ext4_dir_entry_t* n = (ext4_dir_entry_t*)&buf[off + used];
                n->inode = child_ino;
                n->rec_len = old - used;
                n->name_len = (uint8_t)name_len;
                n->file_type = file_type;
                memcpy(n->name, name, name_len);

                if (ext4_write_block(fs, block, buf) != 0) return -1;
                return 0;
            }

            off += d->rec_len;
        }
    }

    uint32_t new_block = 0;
    if (ext4_alloc_block(fs, &new_block) != 0) return -1;

    if (ext4_inode_uses_extents(parent_inode)) {
        if (ext4_extent_append_single_block(parent_inode, blocks, new_block) != 0) {
            (void)ext4_free_block(fs, new_block);
            return -1;
        }
    } else {
        if (blocks >= 12u) {
            (void)ext4_free_block(fs, new_block);
            return -1;
        }
        parent_inode->block[blocks] = new_block;
    }
    parent_inode->size += fs->block_size;
    parent_inode->blocks += fs->block_size / 512u;

    memset(buf, 0, fs->block_size);
    ext4_dir_entry_t* d = (ext4_dir_entry_t*)buf;
    d->inode = child_ino;
    d->rec_len = (uint16_t)fs->block_size;
    d->name_len = (uint8_t)name_len;
    d->file_type = file_type;
    memcpy(d->name, name, name_len);

    if (ext4_write_block(fs, new_block, buf) != 0) return -1;
    if (ext4_write_inode(fs, parent_ino, parent_inode) != 0) return -1;
    return 0;

    return -1;
}

static int ext4_dir_remove_entry(
    ext4_fs_t* fs,
    const ext4_inode_t* parent_inode,
    const char* name,
    uint32_t* out_removed_ino,
    uint8_t* out_removed_ftype
) {
    uint32_t block = 0;
    uint16_t off = 0;
    uint16_t prev = 0xFFFFu;
    ext4_dir_entry_t e;
    if (ext4_dir_find_slot(fs, parent_inode, name, &block, &off, &prev, &e) != 0) return -1;

    uint8_t buf[4096];
    if (ext4_read_block(fs, block, buf) != 0) return -1;
    ext4_dir_entry_t* cur = (ext4_dir_entry_t*)&buf[off];

    if (out_removed_ino) *out_removed_ino = cur->inode;
    if (out_removed_ftype) *out_removed_ftype = cur->file_type;

    if (prev == 0xFFFFu) {
        cur->inode = 0;
    } else {
        ext4_dir_entry_t* p = (ext4_dir_entry_t*)&buf[prev];
        p->rec_len = (uint16_t)(p->rec_len + cur->rec_len);
    }
    return ext4_write_block(fs, block, buf);
}

static int ext4_dir_is_empty(const ext4_fs_t* fs, const ext4_inode_t* dir) {
    uint8_t buf[4096];
    uint32_t blocks = (dir->size + fs->block_size - 1u) / fs->block_size;
    for (uint32_t lbi = 0; lbi < blocks; ++lbi) {
        uint32_t block = 0;
        if (ext4_inode_get_data_block(fs, dir, lbi, &block) != 0) continue;
        if (ext4_read_block(fs, block, buf) != 0) return 0;

        size_t off = 0;
        while (off + 8 <= fs->block_size) {
            ext4_dir_entry_t* d = (ext4_dir_entry_t*)&buf[off];
            if (d->rec_len < 8) break;
            if (d->inode != 0 && d->name_len > 0) {
                if (!((d->name_len == 1 && d->name[0] == '.') ||
                      (d->name_len == 2 && d->name[0] == '.' && d->name[1] == '.'))) {
                    return 0;
                }
            }
            off += d->rec_len;
        }
    }
    return 1;
}

int ext4_fs_mount(ext4_fs_t* fs, uint8_t drive_index, uint64_t partition_lba) {
    memset(fs, 0, sizeof(*fs));
    fs->drive_index = drive_index;
    fs->partition_lba = partition_lba;

    ext4_superblock_t sb;
    if (ext4_read_super(fs, &sb) != 0) return -1;
    if (sb.magic != 0xEF53) return -2;
    if ((sb.feature_incompat & 0x40u) == 0 && (sb.feature_incompat & 0x80u) == 0 && (sb.feature_incompat & 0x200u) == 0 && (sb.feature_compat & 0x0004u) == 0) {
        return -3;
    }

    fs->block_size = 1024u << sb.log_block_size;
    if (fs->block_size > 4096u || fs->block_size < 1024u) return -3;

    fs->inode_size = sb.inode_size ? sb.inode_size : 128;
    fs->blocks_per_group = sb.blocks_per_group;
    fs->inodes_per_group = sb.inodes_per_group;
    fs->first_data_block = sb.first_data_block;
    fs->blocks_count = sb.blocks_count;
    fs->inodes_count = sb.inodes_count;
    fs->root_inode = 2;
    fs->has_journal = (sb.feature_compat & 0x0004u) != 0;
    fs->journal_inode = sb.journal_inum;
    fs->feature_compat = sb.feature_compat;
    fs->feature_incompat = sb.feature_incompat;
    fs->feature_ro_compat = sb.feature_ro_compat;

    if (fs->blocks_per_group == 0 || fs->inodes_per_group == 0) return -4;
    fs->group_count = (fs->blocks_count + fs->blocks_per_group - 1u) / fs->blocks_per_group;
    fs->mounted = true;
    return 0;
}

static int ext4_op_mkdir(void* ctx, const char* path) {
    ext4_fs_t* fs = (ext4_fs_t*)ctx;

    char parent_path[VFS_MAX_PATH];
    char leaf[VFS_MAX_NAME];
    if (ext4_split_parent_leaf(path, parent_path, sizeof(parent_path), leaf, sizeof(leaf)) != 0) return -1;

    ext4_inode_t parent_inode;
    uint32_t parent_ino;
    if (ext4_resolve_path(fs, parent_path, &parent_inode, &parent_ino) != 0) return -1;
    if ((parent_inode.mode & EXT4_S_IFDIR) == 0) return -1;

    uint32_t existing = 0;
    if (ext4_dir_find(fs, &parent_inode, leaf, &existing, NULL) == 0) return -1;

    uint32_t new_ino = 0;
    if (ext4_alloc_inode(fs, &new_ino, 1) != 0) return -1;

    uint32_t new_block = 0;
    if (ext4_alloc_block(fs, &new_block) != 0) {
        (void)ext4_free_inode(fs, new_ino, 1);
        return -1;
    }

    ext4_inode_t diri;
    memset(&diri, 0, sizeof(diri));
    diri.mode = (uint16_t)(EXT4_S_IFDIR | 0755);
    diri.flags = EXT4_EXTENTS_FL;
    ext4_extent_init(&diri);
    diri.links_count = 2;
    diri.size = fs->block_size;
    diri.blocks = fs->block_size / 512u;
    if (ext4_extent_append_single_block(&diri, 0, new_block) != 0) {
        (void)ext4_free_block(fs, new_block);
        (void)ext4_free_inode(fs, new_ino, 1);
        return -1;
    }
    if (ext4_write_inode(fs, new_ino, &diri) != 0) {
        (void)ext4_free_block(fs, new_block);
        (void)ext4_free_inode(fs, new_ino, 1);
        return -1;
    }

    uint8_t buf[4096];
    memset(buf, 0, fs->block_size);

    ext4_dir_entry_t* dot = (ext4_dir_entry_t*)&buf[0];
    dot->inode = new_ino;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = EXT4_FT_DIR;
    dot->name[0] = '.';

    ext4_dir_entry_t* dotdot = (ext4_dir_entry_t*)&buf[12];
    dotdot->inode = parent_ino;
    dotdot->rec_len = (uint16_t)(fs->block_size - 12u);
    dotdot->name_len = 2;
    dotdot->file_type = EXT4_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    if (ext4_write_block(fs, new_block, buf) != 0) {
        (void)ext4_free_block(fs, new_block);
        (void)ext4_free_inode(fs, new_ino, 1);
        return -1;
    }

    if (ext4_dir_add_entry(fs, parent_ino, &parent_inode, new_ino, EXT4_FT_DIR, leaf) != 0) {
        (void)ext4_free_block(fs, new_block);
        (void)ext4_free_inode(fs, new_ino, 1);
        return -1;
    }

    parent_inode.links_count++;
    if (ext4_write_inode(fs, parent_ino, &parent_inode) != 0) return -1;

    return 0;
}

static int ext4_create_or_write_file(ext4_fs_t* fs, const char* path, const void* data, size_t len, int create_if_missing) {
    ext4_inode_t file_inode;
    uint32_t file_ino = 0;

    if (ext4_resolve_path(fs, path, &file_inode, &file_ino) != 0) {
        if (!create_if_missing) return -1;

        char parent_path[VFS_MAX_PATH];
        char leaf[VFS_MAX_NAME];
        if (ext4_split_parent_leaf(path, parent_path, sizeof(parent_path), leaf, sizeof(leaf)) != 0) return -1;

        ext4_inode_t parent_inode;
        uint32_t parent_ino;
        if (ext4_resolve_path(fs, parent_path, &parent_inode, &parent_ino) != 0) return -1;
        if ((parent_inode.mode & EXT4_S_IFDIR) == 0) return -1;

        uint32_t existing = 0;
        if (ext4_dir_find(fs, &parent_inode, leaf, &existing, NULL) == 0) return -1;

        if (ext4_alloc_inode(fs, &file_ino, 0) != 0) return -1;
        memset(&file_inode, 0, sizeof(file_inode));
        file_inode.mode = (uint16_t)(EXT4_S_IFREG | 0644);
        file_inode.flags = 0;
        file_inode.links_count = 1;
        if (ext4_write_inode(fs, file_ino, &file_inode) != 0) {
            (void)ext4_free_inode(fs, file_ino, 0);
            return -1;
        }

        if (ext4_dir_add_entry(fs, parent_ino, &parent_inode, file_ino, EXT4_FT_REG_FILE, leaf) != 0) {
            (void)ext4_free_inode(fs, file_ino, 0);
            return -1;
        }
    }

    if ((file_inode.mode & EXT4_S_IFREG) == 0) return -1;

    if (ext4_inode_write_data(fs, &file_inode, data, len) != 0) return -1;
    if (ext4_write_inode(fs, file_ino, &file_inode) != 0) return -1;
    return 0;
}

static int ext4_create_or_write_file_reader(ext4_fs_t* fs, const char* path, size_t len,
                                            int (*reader)(void* user, size_t offset, void* out, size_t len),
                                            void* user, int create_if_missing) {
    ext4_inode_t file_inode;
    uint32_t file_ino = 0;

    if (ext4_resolve_path(fs, path, &file_inode, &file_ino) != 0) {
        if (!create_if_missing) return -1;
        char parent_path[VFS_MAX_PATH];
        char leaf[VFS_MAX_NAME];
        if (ext4_split_parent_leaf(path, parent_path, sizeof(parent_path), leaf, sizeof(leaf)) != 0) return -1;

        ext4_inode_t parent_inode;
        uint32_t parent_ino = 0;
        if (ext4_resolve_path(fs, parent_path, &parent_inode, &parent_ino) != 0) return -1;
        uint32_t existing = 0;
        if (ext4_dir_find(fs, &parent_inode, leaf, &existing, NULL) == 0) return -1;

        if (ext4_alloc_inode(fs, &file_ino, 0) != 0) return -1;
        memset(&file_inode, 0, sizeof(file_inode));
        file_inode.mode = 0x81A4u;
        file_inode.links_count = 1;
        if (ext4_write_inode(fs, file_ino, &file_inode) != 0) {
            (void)ext4_free_inode(fs, file_ino, 0);
            return -1;
        }

        if (ext4_dir_add_entry(fs, parent_ino, &parent_inode, file_ino, EXT4_FT_REG_FILE, leaf) != 0) {
            (void)ext4_free_inode(fs, file_ino, 0);
            return -1;
        }

        if (ext4_write_inode(fs, parent_ino, &parent_inode) != 0) return -1;
    }

    if (ext4_inode_write_data_reader(fs, &file_inode, len, reader, user) != 0) return -1;
    if (ext4_write_inode(fs, file_ino, &file_inode) != 0) return -1;
    return 0;
}

static int ext4_op_create_file(void* ctx, const char* path, const void* data, size_t len) {
    return ext4_create_or_write_file((ext4_fs_t*)ctx, path, data, len, 1);
}

static int ext4_op_write_file(void* ctx, const char* path, const void* data, size_t len) {
    return ext4_create_or_write_file((ext4_fs_t*)ctx, path, data, len, 1);
}

int ext4_fs_create_file_from_reader(ext4_fs_t* fs, const char* path, size_t len,
                                    int (*reader)(void* user, size_t offset, void* out, size_t len),
                                    void* user) {
    if (!fs || !path || path[0] == '\0') return -1;
    return ext4_create_or_write_file_reader(fs, path, len, reader, user, 1);
}

static int ext4_op_read_file(void* ctx, const char* path, void* out, size_t out_cap, size_t* out_len) {
    ext4_fs_t* fs = (ext4_fs_t*)ctx;
    ext4_inode_t ino;
    if (ext4_resolve_path(fs, path, &ino, NULL) != 0) return -1;
    return ext4_read_file_data(fs, &ino, out, out_cap, out_len);
}

static int ext4_op_list_dir(void* ctx, const char* path, vfs_dirent_t* out, size_t max_entries, size_t* out_count) {
    ext4_fs_t* fs = (ext4_fs_t*)ctx;
    ext4_inode_t dir;
    if (ext4_resolve_path(fs, path, &dir, NULL) != 0) return -1;
    if ((dir.mode & EXT4_S_IFDIR) == 0) return -2;

    size_t count = 0;
    uint8_t buf[4096];

    uint32_t blocks = (dir.size + fs->block_size - 1u) / fs->block_size;
    for (uint32_t lbi = 0; lbi < blocks; ++lbi) {
        uint32_t block = 0;
        if (ext4_inode_get_data_block(fs, &dir, lbi, &block) != 0) continue;
        if (ext4_read_block(fs, block, buf) != 0) return -3;

        size_t off = 0;
        while (off + 8 <= fs->block_size) {
            ext4_dir_entry_t* d = (ext4_dir_entry_t*)&buf[off];
            if (d->rec_len < 8) break;
            if (d->inode != 0 && d->name_len > 0 && d->name_len < VFS_MAX_NAME) {
                if (!(d->name_len == 1 && d->name[0] == '.') &&
                    !(d->name_len == 2 && d->name[0] == '.' && d->name[1] == '.')) {
                    if (out && count < max_entries) {
                        memset(&out[count], 0, sizeof(vfs_dirent_t));
                        memcpy(out[count].name, d->name, d->name_len);
                        out[count].name[d->name_len] = '\0';
                        out[count].is_dir = (d->file_type == EXT4_FT_DIR);
                        out[count].size = 0;
                    }
                    count++;
                }
            }
            off += d->rec_len;
        }
    }

    if (out_count) *out_count = count;
    return 0;
}

static int ext4_op_exists(void* ctx, const char* path) {
    ext4_fs_t* fs = (ext4_fs_t*)ctx;
    ext4_inode_t ino;
    return ext4_resolve_path(fs, path, &ino, NULL) == 0 ? 1 : 0;
}

static int ext4_op_remove(void* ctx, const char* path) {
    ext4_fs_t* fs = (ext4_fs_t*)ctx;
    if (!path || path[0] != '/' || path[1] == '\0') return -1;

    char parent_path[VFS_MAX_PATH];
    char leaf[VFS_MAX_NAME];
    if (ext4_split_parent_leaf(path, parent_path, sizeof(parent_path), leaf, sizeof(leaf)) != 0) return -1;

    ext4_inode_t parent_inode;
    uint32_t parent_ino = 0;
    if (ext4_resolve_path(fs, parent_path, &parent_inode, &parent_ino) != 0) return -1;
    if ((parent_inode.mode & EXT4_S_IFDIR) == 0) return -1;

    uint32_t node_ino = 0;
    uint8_t ftype = 0;
    if (ext4_dir_find(fs, &parent_inode, leaf, &node_ino, &ftype) != 0) return -1;

    ext4_inode_t node_inode;
    if (ext4_read_inode(fs, node_ino, &node_inode) != 0) return -1;
    int is_dir = ((node_inode.mode & EXT4_S_IFDIR) != 0);
    if (is_dir && !ext4_dir_is_empty(fs, &node_inode)) return -1;

    if (ext4_dir_remove_entry(fs, &parent_inode, leaf, NULL, NULL) != 0) return -1;

    if (is_dir) {
        if (parent_inode.links_count > 0) parent_inode.links_count--;
        if (ext4_write_inode(fs, parent_ino, &parent_inode) != 0) return -1;
    }

    if (node_inode.links_count > 0) node_inode.links_count--;
    if (node_inode.links_count == 0) {
        if (ext4_inode_clear_data(fs, &node_inode) != 0) return -1;
        if (ext4_write_inode(fs, node_ino, &node_inode) != 0) return -1;
        if (ext4_free_inode(fs, node_ino, is_dir) != 0) return -1;
    } else {
        if (ext4_write_inode(fs, node_ino, &node_inode) != 0) return -1;
    }
    return 0;
}

static int ext4_op_rename(void* ctx, const char* old_path, const char* new_path) {
    ext4_fs_t* fs = (ext4_fs_t*)ctx;
    if (!old_path || !new_path || old_path[0] != '/' || new_path[0] != '/') return -1;
    if (old_path[1] == '\0' || new_path[1] == '\0') return -1;

    char old_parent_path[VFS_MAX_PATH];
    char old_leaf[VFS_MAX_NAME];
    char new_parent_path[VFS_MAX_PATH];
    char new_leaf[VFS_MAX_NAME];
    if (ext4_split_parent_leaf(old_path, old_parent_path, sizeof(old_parent_path), old_leaf, sizeof(old_leaf)) != 0) return -1;
    if (ext4_split_parent_leaf(new_path, new_parent_path, sizeof(new_parent_path), new_leaf, sizeof(new_leaf)) != 0) return -1;

    ext4_inode_t old_parent;
    uint32_t old_parent_ino = 0;
    if (ext4_resolve_path(fs, old_parent_path, &old_parent, &old_parent_ino) != 0) return -1;
    if ((old_parent.mode & EXT4_S_IFDIR) == 0) return -1;

    ext4_inode_t new_parent;
    uint32_t new_parent_ino = 0;
    if (ext4_resolve_path(fs, new_parent_path, &new_parent, &new_parent_ino) != 0) return -1;
    if ((new_parent.mode & EXT4_S_IFDIR) == 0) return -1;

    uint32_t node_ino = 0;
    uint8_t file_type = 0;
    if (ext4_dir_find(fs, &old_parent, old_leaf, &node_ino, &file_type) != 0) return -1;
    if (ext4_dir_find(fs, &new_parent, new_leaf, NULL, NULL) == 0) return -1;

    if (ext4_dir_add_entry(fs, new_parent_ino, &new_parent, node_ino, file_type, new_leaf) != 0) return -1;
    if (ext4_dir_remove_entry(fs, &old_parent, old_leaf, NULL, NULL) != 0) return -1;

    if (file_type == EXT4_FT_DIR && old_parent_ino != new_parent_ino) {
        if (old_parent.links_count > 0) old_parent.links_count--;
        new_parent.links_count++;
        if (ext4_write_inode(fs, old_parent_ino, &old_parent) != 0) return -1;
        if (ext4_write_inode(fs, new_parent_ino, &new_parent) != 0) return -1;
    }
    return 0;
}

static const vfs_backend_ops_t g_ext4_ops = {
    .mkdir = ext4_op_mkdir,
    .create_file = ext4_op_create_file,
    .write_file = ext4_op_write_file,
    .read_file = ext4_op_read_file,
    .list_dir = ext4_op_list_dir,
    .exists = ext4_op_exists,
    .remove = ext4_op_remove,
    .rename = ext4_op_rename,
};

const vfs_backend_ops_t* ext4_fs_backend_ops(void) {
    return &g_ext4_ops;
}

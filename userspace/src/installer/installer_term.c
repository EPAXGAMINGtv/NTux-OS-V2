#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <syscall.h>
#define INSTALLER_CONSOLE 1
#include <args.h>

#define INST_WIN_W 960
#define INST_WIN_H 600

#define INS_PATH_MAX 256
#define INS_LINE_MAX 64
#define INS_LS_MAX 64
#define INS_TARGET_MAX 16
#define INS_FILE_MAX (64u * 1024u * 1024u)
#define INS_LOG_LINES 14

static ntux_dirent_t g_dirents[INS_LS_MAX];
static ntux_block_device_info_t g_devs[INS_TARGET_MAX];
static uint64_t g_dev_ids[INS_TARGET_MAX];
static int g_dev_count = 0;
static int g_selected = -1;

static char g_source[INS_PATH_MAX];

static char g_logs[INS_LOG_LINES][96];
static int g_log_count = 0;
static char g_status[96];

static uint64_t g_copy_total = 0;
static uint64_t g_copy_done = 0;
static uint64_t g_copy_total_bytes = 0;
static uint64_t g_copy_done_bytes = 0;
static int g_copy_last_percent = -1;
static int g_progress_percent = 0;

static void* g_copy_buf = 0;
static uint64_t g_copy_buf_cap = 0;

static int g_part_scheme = 0; /* 0=MBR, 1=GPT */
static int g_fs_choice = 0;   /* 0=FAT auto, 32=FAT32, 2=EXT2, 4=EXT4 */
static int g_console_mode = 0;

static void inst_log_line(const char* line) {
    if (!line) return;
    if (g_console_mode) {
        puts(line);
    }
    if (g_log_count < INS_LOG_LINES) {
        strncpy(g_logs[g_log_count], line, 95);
        g_logs[g_log_count][95] = '\0';
        g_log_count++;
        return;
    }
    for (int i = 1; i < INS_LOG_LINES; ++i) {
        memcpy(g_logs[i - 1], g_logs[i], 96);
    }
    strncpy(g_logs[INS_LOG_LINES - 1], line, 95);
    g_logs[INS_LOG_LINES - 1][95] = '\0';
}

static void inst_status(const char* line) {
    if (!line) return;
    if (g_console_mode) {
        puts(line);
    }
    strncpy(g_status, line, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
}

static int starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s != *prefix) return 0;
        ++s;
        ++prefix;
    }
    return 1;
}

static int ends_with(const char* s, const char* suffix) {
    size_t sl;
    size_t tl;
    if (!s || !suffix) return 0;
    sl = strlen(s);
    tl = strlen(suffix);
    if (tl > sl) return 0;
    return strcmp(s + (sl - tl), suffix) == 0;
}

static int should_skip_on_install(const char* src_path) {
    if (!src_path) return 0;
    if (ends_with(src_path, "/boot/modules/installer.elf")) return 1;
    if (ends_with(src_path, "/boot/modules/INSTALLER.ELF")) return 1;
    if (ends_with(src_path, "/boot/modules/installer_term.elf")) return 1;
    if (ends_with(src_path, "/boot/modules/INSTALLER_TERM.ELF")) return 1;
    if (ends_with(src_path, "/boot/boot/modules/installer.elf")) return 1;
    if (ends_with(src_path, "/boot/boot/modules/INSTALLER.ELF")) return 1;
    if (ends_with(src_path, "/boot/boot/modules/installer_term.elf")) return 1;
    if (ends_with(src_path, "/boot/boot/modules/INSTALLER_TERM.ELF")) return 1;
    if (ends_with(src_path, "/boot/limine/limine-uefi-cd.bin")) return 1;
    if (ends_with(src_path, "/boot/boot/limine/limine-uefi-cd.bin")) return 1;
    return 0;
}

static __attribute__((noinline)) int copy_tree(const char* src, const char* dst);

static int dir_exists(const char* path) {
    uint64_t n = 0;
    return sys_fs_list_dir(path, 0, 0, &n) == 0;
}

static void progress_update(int force_newline) {
    (void)force_newline;
    uint64_t total = g_copy_total_bytes ? g_copy_total_bytes : g_copy_total;
    uint64_t done = g_copy_total_bytes ? g_copy_done_bytes : g_copy_done;
    if (total == 0) return;
    int percent = (int)((done * 100u) / total);
    if (percent == g_copy_last_percent) return;
    g_copy_last_percent = percent;
    g_progress_percent = percent;
}

static int is_iso_name(const char* name) {
    return starts_with(name, "iso") || starts_with(name, "cd");
}

static int make_child_path(const char* base, const char* name, char out[INS_PATH_MAX]) {
    size_t bl = strlen(base);
    size_t nl = strlen(name);
    size_t need = bl + (strcmp(base, "/") == 0 ? 0u : 1u) + nl + 1u;
    if (need > INS_PATH_MAX) return -1;
    if (strcmp(base, "/") == 0) {
        out[0] = '/';
        memcpy(out + 1, name, nl);
        out[1 + nl] = '\0';
        return 0;
    }
    memcpy(out, base, bl);
    out[bl] = '/';
    memcpy(out + bl + 1u, name, nl);
    out[bl + 1u + nl] = '\0';
    return 0;
}

static int split_parent_name(const char* full, char parent[INS_PATH_MAX], char name[INS_PATH_MAX]) {
    const char* slash = 0;
    if (!full || full[0] != '/') return -1;
    for (const char* p = full; *p; ++p) {
        if (*p == '/') slash = p;
    }
    if (!slash || !slash[1]) return -1;

    size_t plen = (size_t)(slash - full);
    size_t nlen = strlen(slash + 1);
    if (nlen == 0 || nlen >= INS_PATH_MAX) return -1;

    if (plen == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        if (plen >= INS_PATH_MAX) return -1;
        memcpy(parent, full, plen);
        parent[plen] = '\0';
    }
    memcpy(name, slash + 1, nlen + 1u);
    return 0;
}

static int mkdir_full_path(const char* path) {
    char parent[INS_PATH_MAX];
    char name[INS_PATH_MAX];
    if (sys_fs_exists(path) > 0) return 0;
    if (split_parent_name(path, parent, name) != 0) return -1;
    return (sys_fs_mkdir(parent, name) == 0) ? 0 : -1;
}

static __attribute__((noinline)) int remove_tree(const char* path) {
    uint64_t count = 0;
    int rc = -1;
    char (*names)[64] = 0;
    uint8_t* kinds = 0;

    long list_rc = sys_fs_list_dir(path, g_dirents, INS_LS_MAX, &count);
    if (list_rc != 0) return -1;
    if (count > INS_LS_MAX) count = INS_LS_MAX;

    names = (char (*)[64])malloc(INS_LS_MAX * 64u);
    kinds = (uint8_t*)malloc(INS_LS_MAX);
    if (!names || !kinds) goto cleanup;

    for (uint64_t i = 0; i < count; ++i) {
        strncpy(names[i], g_dirents[i].name, sizeof(names[i]) - 1);
        names[i][sizeof(names[i]) - 1] = '\0';
        kinds[i] = g_dirents[i].is_dir ? 1u : 0u;
    }
    for (uint64_t i = 0; i < count; ++i) {
        char child[INS_PATH_MAX];
        if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0) {
            continue;
        }
        if (make_child_path(path, names[i], child) != 0) goto cleanup;
        if (kinds[i]) {
            if (remove_tree(child) != 0) goto cleanup;
        }
        if (sys_fs_remove(child) != 0) goto cleanup;
    }
    rc = 0;

cleanup:
    if (kinds) free(kinds);
    if (names) free(names);
    return rc;
}

static int copy_file(const char* src, const char* dst, uint64_t size_hint, int size_known) {
    uint64_t len = size_hint;
    char parent[INS_PATH_MAX];
    char name[INS_PATH_MAX];
    void* buf = 0;

    if (!size_known) {
        if (sys_fs_read_file(src, 0, 0, &len) != 0) return -1;
    }
    if (len > INS_FILE_MAX) {
        inst_log_line("[err] file too large");
        return -1;
    }

    if (sys_fs_copy_fast(src, dst) == 0) {
        g_copy_done++;
        g_copy_done_bytes += len;
        progress_update(0);
        return 0;
    }

    if (split_parent_name(dst, parent, name) != 0) return -1;
    if (len > 0) {
        if (len <= g_copy_buf_cap && g_copy_buf) {
            buf = g_copy_buf;
        } else {
            void* next = malloc((size_t)len);
            if (!next) return -1;
            if (g_copy_buf) free(g_copy_buf);
            g_copy_buf = next;
            g_copy_buf_cap = len;
            buf = g_copy_buf;
        }
        if (sys_fs_read_file(src, buf, len, &len) != 0) {
            return -1;
        }
    }

    if (sys_fs_exists(dst) > 0) {
        long rc = sys_fs_write_file(dst, buf, len);
        if (rc == 0) {
            g_copy_done++;
            g_copy_done_bytes += len;
            progress_update(0);
        }
        return (rc == 0) ? 0 : -1;
    }

    long rc = sys_fs_create_file(parent, name, buf, len);
    if (rc == 0) {
        g_copy_done++;
        g_copy_done_bytes += len;
        progress_update(0);
    }
    return (rc == 0) ? 0 : -1;
}

static int copy_tree_if_exists(const char* src, const char* dst) {
    if (!dir_exists(src)) return 0;
    if (sys_fs_exists(dst) <= 0) {
        if (mkdir_full_path(dst) != 0) return -1;
    }
    return copy_tree(src, dst);
}

static int copy_file_simple(const char* src, const char* dst) {
    uint64_t len = 0;
    if (sys_fs_read_file(src, 0, 0, &len) != 0) return -1;
    if (len == 0 || len > INS_FILE_MAX) return -1;
    void* buf = malloc((size_t)len);
    if (!buf) return -1;
    if (sys_fs_read_file(src, buf, len, &len) != 0) {
        free(buf);
        return -1;
    }
    long rc = 0;
    if (sys_fs_exists(dst) > 0) rc = sys_fs_write_file(dst, buf, len);
    else {
        char parent[INS_PATH_MAX];
        char name[INS_PATH_MAX];
        if (split_parent_name(dst, parent, name) != 0) {
            free(buf);
            return -1;
        }
        rc = sys_fs_create_file(parent, name, buf, len);
    }
    free(buf);
    return (rc == 0) ? 0 : -1;
}

static int patch_limine_conf_remove_installer(const char* target_root) {
    char path[INS_PATH_MAX];
    if (make_child_path(target_root, "boot/limine/limine.conf", path) != 0) return -1;

    uint64_t len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0) return -1;
    if (len == 0 || len > INS_FILE_MAX) return -1;

    char* buf = (char*)malloc((size_t)len + 1u);
    if (!buf) return -1;
    if (sys_fs_read_file(path, buf, len, &len) != 0) {
        free(buf);
        return -1;
    }
    buf[len] = '\0';

    char* out = (char*)malloc((size_t)len + 1u);
    if (!out) {
        free(buf);
        return -1;
    }

    size_t out_len = 0;
    char* line = buf;
    while (*line) {
        char* nl = strchr(line, '\n');
        size_t line_len = nl ? (size_t)(nl - line + 1u) : strlen(line);
        int keep = 1;
        if (strstr(line, "installer.elf") != 0 ||
            strstr(line, "installer_term.elf") != 0 ||
            strstr(line, "INSTALLER_TERM.ELF") != 0) {
            keep = 0;
        }
        if (keep) {
            if (out_len + line_len < INS_FILE_MAX) {
                memcpy(out + out_len, line, line_len);
                out_len += line_len;
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    if (out_len == 0 || out_len >= INS_FILE_MAX) {
        free(out);
        free(buf);
        return -1;
    }

    if (sys_fs_write_file(path, out, out_len) == 0) {
        inst_log_line("[ok] limine.conf patched");
    } else {
        inst_log_line("[warn] limine.conf patch failed");
    }

    free(out);
    free(buf);
    return 0;
}

static void remove_installer_on_target(const char* target_root) {
    char path[INS_PATH_MAX];
    if (make_child_path(target_root, "boot/modules/installer.elf", path) == 0) {
        sys_fs_remove(path);
    }
    if (make_child_path(target_root, "boot/modules/INSTALLER.ELF", path) == 0) {
        sys_fs_remove(path);
    }
    if (make_child_path(target_root, "boot/modules/installer_term.elf", path) == 0) {
        sys_fs_remove(path);
    }
    if (make_child_path(target_root, "boot/modules/INSTALLER_TERM.ELF", path) == 0) {
        sys_fs_remove(path);
    }
}

static __attribute__((noinline)) int scan_tree(const char* src, uint64_t* out_files, uint64_t* out_bytes) {
    uint64_t count = 0;
    int rc = -1;
    char (*names)[64] = 0;
    uint8_t* kinds = 0;
    uint64_t* sizes = 0;

    long list_rc = sys_fs_list_dir(src, g_dirents, INS_LS_MAX, &count);
    if (list_rc != 0) return -1;
    if (count > INS_LS_MAX) count = INS_LS_MAX;

    names = (char (*)[64])malloc(INS_LS_MAX * 64u);
    kinds = (uint8_t*)malloc(INS_LS_MAX);
    sizes = (uint64_t*)malloc(sizeof(uint64_t) * INS_LS_MAX);
    if (!names || !kinds || !sizes) goto cleanup;

    for (uint64_t i = 0; i < count; ++i) {
        strncpy(names[i], g_dirents[i].name, sizeof(names[i]) - 1);
        names[i][sizeof(names[i]) - 1] = '\0';
        kinds[i] = g_dirents[i].is_dir ? 1u : 0u;
        sizes[i] = g_dirents[i].size;
    }
    for (uint64_t i = 0; i < count; ++i) {
        char src_child[INS_PATH_MAX];
        if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0) {
            continue;
        }
        if (make_child_path(src, names[i], src_child) != 0) goto cleanup;

        if (kinds[i]) {
            if (scan_tree(src_child, out_files, out_bytes) != 0) goto cleanup;
        } else {
            if (should_skip_on_install(src_child)) continue;
            if (out_files) (*out_files)++;
            if (out_bytes) (*out_bytes) += sizes[i];
        }
    }
    rc = 0;

cleanup:
    if (sizes) free(sizes);
    if (kinds) free(kinds);
    if (names) free(names);
    return rc;
}

static __attribute__((noinline)) int copy_tree(const char* src, const char* dst) {
    uint64_t count = 0;
    int rc = -1;
    char (*names)[64] = 0;
    uint8_t* kinds = 0;
    uint64_t* sizes = 0;

    long list_rc = sys_fs_list_dir(src, g_dirents, INS_LS_MAX, &count);
    if (list_rc != 0) return -1;
    if (count > INS_LS_MAX) count = INS_LS_MAX;

    names = (char (*)[64])malloc(INS_LS_MAX * 64u);
    kinds = (uint8_t*)malloc(INS_LS_MAX);
    sizes = (uint64_t*)malloc(sizeof(uint64_t) * INS_LS_MAX);
    if (!names || !kinds || !sizes) goto cleanup;

    for (uint64_t i = 0; i < count; ++i) {
        strncpy(names[i], g_dirents[i].name, sizeof(names[i]) - 1);
        names[i][sizeof(names[i]) - 1] = '\0';
        kinds[i] = g_dirents[i].is_dir ? 1u : 0u;
        sizes[i] = g_dirents[i].size;
    }
    for (uint64_t i = 0; i < count; ++i) {
        char src_child[INS_PATH_MAX];
        char dst_child[INS_PATH_MAX];
        if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0) {
            continue;
        }
        if (make_child_path(src, names[i], src_child) != 0) goto cleanup;
        if (make_child_path(dst, names[i], dst_child) != 0) goto cleanup;

        if (kinds[i]) {
            if (mkdir_full_path(dst_child) != 0) goto cleanup;
            if (copy_tree(src_child, dst_child) != 0) goto cleanup;
        } else {
            if (should_skip_on_install(src_child)) {
                continue;
            }
            if (copy_file(src_child, dst_child, sizes[i], 1) != 0) {
                inst_log_line("[err] copy failed");
                goto cleanup;
            }
        }
    }
    rc = 0;

cleanup:
    if (sizes) free(sizes);
    if (kinds) free(kinds);
    if (names) free(names);
    return rc;
}

static __attribute__((noinline)) int detect_source(char source_out[INS_PATH_MAX]) {
    uint64_t count = 0;
    int have_source = 0;
    long rc = sys_fs_list_dir("/", g_dirents, INS_LS_MAX, &count);
    if (rc != 0) return -1;
    if (count > INS_LS_MAX) count = INS_LS_MAX;

    for (uint64_t i = 0; i < count; ++i) {
        if (!g_dirents[i].is_dir) continue;
        if (!have_source && is_iso_name(g_dirents[i].name)) {
            if (make_child_path("/", g_dirents[i].name, source_out) == 0) {
                have_source = 1;
            }
        }
    }
    if (!have_source) {
        ntux_dirent_t probe[1];
        uint64_t n = 0;
        if (sys_fs_list_dir("/boot/boot", probe, 1, &n) == 0) {
            strncpy(source_out, "/boot/boot", INS_PATH_MAX - 1);
            source_out[INS_PATH_MAX - 1] = '\0';
            have_source = 1;
        } else if (sys_fs_list_dir("/boot", probe, 1, &n) == 0) {
            strncpy(source_out, "/boot", INS_PATH_MAX - 1);
            source_out[INS_PATH_MAX - 1] = '\0';
            have_source = 1;
        }
    }
    if (!have_source) {
        uint64_t n = 0;
        if (sys_fs_list_dir("/media", g_dirents, INS_LS_MAX, &n) == 0) {
            if (n > INS_LS_MAX) n = INS_LS_MAX;
            for (uint64_t i = 0; i < n; ++i) {
                if (!g_dirents[i].is_dir) continue;
                char cand[INS_PATH_MAX];
                char probe_path[INS_PATH_MAX];
                ntux_dirent_t probe[1];
                uint64_t pn = 0;
                if (make_child_path("/media", g_dirents[i].name, cand) != 0) continue;
                if (make_child_path(cand, "boot/limine", probe_path) == 0 &&
                    sys_fs_list_dir(probe_path, probe, 1, &pn) == 0) {
                    strncpy(source_out, cand, INS_PATH_MAX - 1);
                    source_out[INS_PATH_MAX - 1] = '\0';
                    have_source = 1;
                    break;
                }
                if (make_child_path(cand, "boot/boot/limine", probe_path) == 0 &&
                    sys_fs_list_dir(probe_path, probe, 1, &pn) == 0) {
                    strncpy(source_out, cand, INS_PATH_MAX - 1);
                    source_out[INS_PATH_MAX - 1] = '\0';
                    have_source = 1;
                    break;
                }
                if (make_child_path(cand, "boot", probe_path) == 0 &&
                    sys_fs_list_dir(probe_path, probe, 1, &pn) == 0) {
                    strncpy(source_out, cand, INS_PATH_MAX - 1);
                    source_out[INS_PATH_MAX - 1] = '\0';
                    have_source = 1;
                    break;
                }
            }
        }
    }
    return have_source ? 0 : -1;
}

static int parse_drive_part_name(const char* name, uint64_t* out_drive, uint64_t* out_part) {
    if (!name || name[0] == '\0') return -1;
    if (name[0] != 's' || name[1] != 'd') return -1;
    char letter = name[2];
    if (letter < 'a' || letter > 'z') return -1;
    uint64_t drive = (uint64_t)(letter - 'a');
    const char* p = name + 3;
    if (*p < '0' || *p > '9') return -1;
    uint64_t part = 0;
    while (*p >= '0' && *p <= '9') {
        part = part * 10u + (uint64_t)(*p - '0');
        ++p;
    }
    if (*p != '\0') return -1;
    if (out_drive) *out_drive = drive;
    if (out_part) *out_part = part;
    return 0;
}

static int find_mount_for_drive_part(uint64_t drive, uint64_t part, char out[INS_PATH_MAX]) {
    uint64_t count = 0;
    char media_root[INS_PATH_MAX];
    char letter = (char)('a' + (drive % 26u));
    int rc = snprintf(media_root, sizeof(media_root), "/media/sd%c%llu", letter, (unsigned long long)part);
    if (rc > 0 && (size_t)rc < sizeof(media_root)) {
        ntux_dirent_t probe[1];
        if (sys_fs_list_dir(media_root, probe, 1, &count) == 0) {
            strncpy(out, media_root, INS_PATH_MAX - 1);
            out[INS_PATH_MAX - 1] = '\0';
            return 0;
        }
    }
    rc = sys_fs_list_dir("/media", g_dirents, INS_LS_MAX, &count);
    if (rc != 0) return -1;
    if (count > INS_LS_MAX) count = INS_LS_MAX;
    for (uint64_t i = 0; i < count; ++i) {
        if (!g_dirents[i].is_dir) continue;
        uint64_t d = 0;
        uint64_t p = 0;
        if (parse_drive_part_name(g_dirents[i].name, &d, &p) == 0) {
            if (d == drive && p == part) {
                return make_child_path("/media", g_dirents[i].name, out);
            }
        }
    }
    return -1;
}

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

static int write_gpt_single(uint64_t drive, uint64_t total, uint64_t* out_lba, uint64_t* out_sectors) {
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
    if (out_sectors) *out_sectors = end - start + 1u;
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

static int scan_drives(void) {
    uint64_t count = 0;
    if (sys_block_list(g_devs, INS_TARGET_MAX, &count) != 0) return -1;
    if (count > INS_TARGET_MAX) count = INS_TARGET_MAX;
    g_dev_count = 0;
    for (uint64_t i = 0; i < count; ++i) {
        if (!g_devs[i].present || g_devs[i].is_atapi) continue;
        if (g_dev_count >= INS_TARGET_MAX) break;
        g_dev_ids[g_dev_count] = i;
        g_dev_count++;
    }
    return (g_dev_count > 0) ? 0 : -1;
}

static int run_install(void) {
    uint64_t drive = (g_selected >= 0) ? g_dev_ids[g_selected] : 0;
    ntux_block_device_info_t dev;

    if (g_selected < 0 || g_selected >= g_dev_count) {
        inst_log_line("[err] no drive selected");
        return -1;
    }
    memcpy(&dev, &g_devs[drive], sizeof(dev));

    int gpt_mode = g_part_scheme;
    uint8_t fs_type = (uint8_t)g_fs_choice;
    int use_esp = 0;

    if (fs_type == 2 || fs_type == 4) {
        use_esp = 1;
        if (!gpt_mode) {
            inst_log_line("[info] EXT -> GPT + ESP");
            gpt_mode = 1;
        }
    } else {
        use_esp = 0;
    }

    inst_status("Detecting source...");
    if (detect_source(g_source) != 0) {
        inst_log_line("[err] no source found");
        return -1;
    }

    inst_status("Wiping...");
    inst_log_line("[1/4] wipe");
    if (wipe_range(drive, 0, (dev.sectors > 2048u) ? 2048u : dev.sectors) != 0) {
        inst_log_line("[err] wipe failed");
        return -1;
    }

    uint64_t part_lba = 0;
    uint64_t part_secs = 0;
    uint64_t esp_lba = 0;
    uint64_t esp_secs = 0;
    inst_status("Partitioning...");
    inst_log_line("[2/4] partition");

    if (gpt_mode) {
        if (use_esp) {
            if (write_gpt_dual(drive, dev.sectors, &esp_lba, &esp_secs, &part_lba, &part_secs) != 0) {
                inst_log_line("[err] GPT create failed");
                return -1;
            }
        } else {
            if (write_gpt_single(drive, dev.sectors, &part_lba, &part_secs) != 0) {
                inst_log_line("[err] GPT create failed");
                return -1;
            }
        }
    } else {
        ntux_mbr_part_req_t req;
        memset(&req, 0, sizeof(req));
        req.drive = (uint8_t)drive;
        for (uint8_t p = 1; p <= 4; ++p) {
            req.part_index = p;
            if (sys_block_set_mbr_partition(&req) != 0) {
                inst_log_line("[err] MBR clear failed");
                return -1;
            }
        }
        uint32_t start = (dev.sectors > 4096u) ? 2048u : 1u;
        if (dev.sectors <= start + 8u) {
            inst_log_line("[err] drive too small");
            return -1;
        }
        uint32_t usable = (dev.sectors > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)dev.sectors;
        uint32_t secs = (usable > start) ? (usable - start) : 0u;
        uint8_t type = 0x0Cu;
        if (fs_type == 12) type = 0x01u;
        else if (fs_type == 16) type = 0x06u;
        else if (fs_type == 32 || fs_type == 0) type = 0x0Cu;
        else if (fs_type == 2 || fs_type == 4) type = 0x83u;
        req.part_index = 1;
        req.type = type;
        req.bootable = 0;
        req.lba_start = start;
        req.sectors = secs;
        if (req.sectors < 2048u || sys_block_set_mbr_partition(&req) != 0) {
            inst_log_line("[err] MBR create failed");
            return -1;
        }
        part_lba = req.lba_start;
        part_secs = req.sectors;
    }

    inst_status("Formatting...");
    inst_log_line("[3/4] format");
    if (use_esp) {
        if (esp_secs > 0xFFFFFFFFu) {
            inst_log_line("[err] ESP too large");
            return -1;
        }
        if (sys_mkfs_fat(drive, esp_lba, esp_secs, 0) != 0) {
            inst_log_line("[err] mkfs.fat (ESP) failed");
            return -1;
        }
    }

    if (fs_type == 2) {
        if (sys_mkfs_ext2(drive, part_lba, part_secs) != 0) {
            inst_log_line("[err] mkfs.ext2 failed");
            return -1;
        }
    } else if (fs_type == 4) {
        if (sys_mkfs_ext4(drive, part_lba, part_secs) != 0) {
            inst_log_line("[err] mkfs.ext4 failed");
            return -1;
        }
    } else {
        if (part_secs > 0xFFFFFFFFu) {
            inst_log_line("[err] partition too large");
            return -1;
        }
        if (sys_mkfs_fat(drive, part_lba, part_secs, fs_type) != 0) {
            inst_log_line("[warn] mkfs.fat retry auto");
            if (sys_mkfs_fat(drive, part_lba, part_secs, 0) != 0) {
                uint64_t shrink = (part_secs > 32u) ? 32u : 1u;
                if (part_secs > shrink) {
                    if (sys_mkfs_fat(drive, part_lba, part_secs - shrink, 0) == 0) {
                        part_secs -= shrink;
                    } else {
                        inst_log_line("[err] mkfs.fat failed");
                        return -1;
                    }
                } else {
                    inst_log_line("[err] mkfs.fat failed");
                    return -1;
                }
            }
        }
    }
    if (sys_fs_rescan() != 0) {
        inst_log_line("[err] fs rescan failed");
        return -1;
    }

    char target[INS_PATH_MAX];
    char esp_target[INS_PATH_MAX];
    if (use_esp) {
        if (find_mount_for_drive_part(drive, 1, esp_target) != 0) {
            inst_log_line("[err] ESP mount not found");
            return -1;
        }
        if (find_mount_for_drive_part(drive, 2, target) != 0) {
            inst_log_line("[err] target mount not found");
            return -1;
        }
    } else {
        if (find_mount_for_drive_part(drive, 1, target) != 0) {
            inst_log_line("[err] target mount not found");
            return -1;
        }
    }
    if (strcmp(g_source, target) == 0 || (use_esp && strcmp(g_source, esp_target) == 0)) {
        inst_log_line("[err] source == target");
        return -1;
    }

    inst_status("Copying...");
    inst_log_line("[4/4] copy");
    if (remove_tree(target) != 0) {
        inst_log_line("[err] delete failed");
        return -1;
    }
    if (use_esp) {
        if (remove_tree(esp_target) != 0) {
            inst_log_line("[err] ESP delete failed");
            return -1;
        }
    }
    g_copy_total = 0;
    g_copy_done = 0;
    g_copy_total_bytes = 0;
    g_copy_done_bytes = 0;
    g_copy_last_percent = -1;
    g_progress_percent = 0;
    if (scan_tree(g_source, &g_copy_total, &g_copy_total_bytes) != 0) {
        inst_log_line("[err] scan failed");
        return -1;
    }
    if (g_copy_total == 0) g_copy_total = 1;
    if (g_copy_total_bytes == 0) g_copy_total_bytes = 1;
    progress_update(0);
    if (copy_tree(g_source, target) != 0) {
        inst_log_line("[err] copy failed");
        return -1;
    }
    progress_update(1);

    if (use_esp) {
        const char* esp_src = g_source;
        char src_efi[INS_PATH_MAX];
        char src_lim[INS_PATH_MAX];
        char dst_efi[INS_PATH_MAX];
        char dst_lim[INS_PATH_MAX];
        char src_kernel[INS_PATH_MAX];
        char dst_kernel[INS_PATH_MAX];
        char src_modules[INS_PATH_MAX];
        char dst_modules[INS_PATH_MAX];
        char src_recovery[INS_PATH_MAX];
        char dst_recovery[INS_PATH_MAX];
        if (make_child_path(esp_src, "EFI", src_efi) == 0 &&
            make_child_path(esp_target, "EFI", dst_efi) == 0) {
            (void)copy_tree_if_exists(src_efi, dst_efi);
        }
        if (make_child_path(esp_src, "boot/limine", src_lim) == 0 &&
            make_child_path(esp_target, "boot/limine", dst_lim) == 0) {
            (void)copy_tree_if_exists(src_lim, dst_lim);
        }
        if (make_child_path(esp_src, "kernel", src_kernel) == 0 &&
            make_child_path(esp_target, "boot/kernel", dst_kernel) == 0) {
            (void)copy_file_simple(src_kernel, dst_kernel);
        }
        if (make_child_path(esp_src, "modules", src_modules) == 0 &&
            make_child_path(esp_target, "boot/modules", dst_modules) == 0) {
            (void)copy_tree_if_exists(src_modules, dst_modules);
        }
        if (make_child_path(esp_src, "recovery", src_recovery) == 0 &&
            make_child_path(esp_target, "boot/recovery", dst_recovery) == 0) {
            (void)copy_tree_if_exists(src_recovery, dst_recovery);
        }
        {
            char src_conf[INS_PATH_MAX];
            char dst_conf_root[INS_PATH_MAX];
            char dst_conf_boot[INS_PATH_MAX];
            char dst_conf_limine[INS_PATH_MAX];
            if (make_child_path(esp_src, "boot/limine/limine.conf", src_conf) == 0) {
                if (make_child_path(esp_target, "limine.conf", dst_conf_root) == 0) {
                    (void)copy_file_simple(src_conf, dst_conf_root);
                }
                if (make_child_path(esp_target, "boot/limine/limine.conf", dst_conf_boot) == 0) {
                    (void)copy_file_simple(src_conf, dst_conf_boot);
                }
                if (make_child_path(esp_target, "limine", dst_conf_limine) == 0) {
                    (void)mkdir_full_path(dst_conf_limine);
                    if (make_child_path(esp_target, "limine/limine.conf", dst_conf_limine) == 0) {
                        (void)copy_file_simple(src_conf, dst_conf_limine);
                    }
                }
            }
        }
        (void)patch_limine_conf_remove_installer(esp_target);
    }
    (void)patch_limine_conf_remove_installer(target);
    remove_installer_on_target(target);

    inst_status("Done");
    return 0;
}


static void trim_ascii(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') s[--n] = '\0';
        else break;
    }
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
    if (i > 0) memmove(s, s + i, strlen(s + i) + 1u);
}

static int parse_u64_simple(const char* s, uint64_t* out) {
    if (!s || !s[0] || !out) return -1;
    uint64_t v = 0;
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] < '0' || s[i] > '9') return -1;
        uint64_t d = (uint64_t)(s[i] - '0');
        if (v > (((uint64_t)~0ull) - d) / 10u) return -1;
        v = v * 10u + d;
    }
    *out = v;
    return 0;
}

static void print_drive_list_console(void) {
    puts("Available drives:");
    for (int i = 0; i < g_dev_count; ++i) {
        uint64_t id = g_dev_ids[i];
        uint64_t mib = g_devs[id].sectors / 2048u;
        printf("  [%d] drive %llu  %llu MiB  %s\n",
               i,
               (unsigned long long)id,
               (unsigned long long)mib,
               g_devs[id].model);
    }
}

static int read_line_console(char* buf, size_t cap) {
    if (!buf || cap == 0) return -1;
    buf[0] = '\0';
    if (!fgets(buf, (int)cap, stdin)) return -1;
    trim_ascii(buf);
    return (buf[0] != '\0') ? 0 : -1;
}

static int run_console_installer(void) {
    g_console_mode = 1;
    puts("NTux Terminal Installer");
    puts("-----------------------");

    if (sys_console_claim() != 0) {
        puts("[err] console busy (input not available).");
        puts("Close the terminal or run from console boot.");
        return -1;
    }

    if (detect_source(g_source) != 0) {
        puts("[err] no source found");
        sys_console_release();
        return -1;
    }
    if (scan_drives() != 0) {
        puts("[err] no drives found");
        sys_console_release();
        return -1;
    }
    print_drive_list_console();

    char line[64];
    uint64_t choice = 0;
    for (;;) {
        printf("Select drive index (0-%d): ", g_dev_count - 1);
        if (read_line_console(line, sizeof(line)) != 0) continue;
        if (parse_u64_simple(line, &choice) == 0 && choice < (uint64_t)g_dev_count) {
            g_selected = (int)choice;
            break;
        }
        puts("Invalid selection.");
    }

    for (;;) {
        puts("Partition table: type 'mbr' or 'gpt'");
        printf("> ");
        if (read_line_console(line, sizeof(line)) != 0) continue;
        if (strcmp(line, "mbr") == 0) { g_part_scheme = 0; break; }
        if (strcmp(line, "gpt") == 0) { g_part_scheme = 1; break; }
        puts("Invalid option.");
    }

    for (;;) {
        puts("Filesystem: fat-auto | fat12 | fat16 | fat32 | ext2 | ext4");
        printf("> ");
        if (read_line_console(line, sizeof(line)) != 0) continue;
        if (strcmp(line, "fat-auto") == 0) { g_fs_choice = 0; break; }
        if (strcmp(line, "fat12") == 0) { g_fs_choice = 12; break; }
        if (strcmp(line, "fat16") == 0) { g_fs_choice = 16; break; }
        if (strcmp(line, "fat32") == 0) { g_fs_choice = 32; break; }
        if (strcmp(line, "ext2") == 0) { g_fs_choice = 2; break; }
        if (strcmp(line, "ext4") == 0) { g_fs_choice = 4; break; }
        puts("Invalid option.");
    }

    {
        uint64_t id = g_dev_ids[g_selected];
        const char* scheme = g_part_scheme ? "GPT" : "MBR";
        const char* fat = (g_fs_choice == 0) ? "FAT Auto" :
                          (g_fs_choice == 12) ? "FAT12" :
                          (g_fs_choice == 16) ? "FAT16" :
                          (g_fs_choice == 32) ? "FAT32" :
                          (g_fs_choice == 2) ? "EXT2" : "EXT4";
        printf("\nTarget drive: %llu (%s)\n", (unsigned long long)id, g_devs[id].model);
        printf("Partition: %s\n", scheme);
        printf("Filesystem: %s\n", fat);
        puts("Type INSTALL to continue or anything else to abort.");
    }

    printf("> ");
    if (read_line_console(line, sizeof(line)) != 0 || strcmp(line, "INSTALL") != 0) {
        puts("Aborted.");
        sys_console_release();
        return 0;
    }

    int rc = run_install();
    if (rc == 0) {
        puts("[ok] Installation completed.");
        long hz = sys_get_timer_hz();
        if (hz <= 0) hz = 200;
        for (int sec = 10; sec > 0; --sec) {
            printf("Rebooting in %d...\n", sec);
            sys_wait_ticks((uint64_t)hz);
        }
        puts("Rebooting now.");
        (void)sys_reboot();
    } else {
        puts("[err] Installation failed.");
    }
    sys_console_release();
    return rc;
}

void ntux_user_entry(void) {
    int rc = run_console_installer();
    sys_exit((rc == 0) ? 0 : 1);
}

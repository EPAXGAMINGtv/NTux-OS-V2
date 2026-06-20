#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <font8x8_basic.h>
#include "desktop_defs.h"
#include "desktop_internal.h"
#include "terminal.h"

#define DESK_TERM_ROUTE_MAX 64

extern int g_desktop_dirty;
extern uint64_t g_last_key_tick;
extern char g_last_key_char;

extern int str_append(char* out, size_t cap, const char* s);
extern int str_append_char(char* out, size_t cap, char c);
extern int str_append_u64(char* out, size_t cap, uint64_t v);
extern int str_append_u32_2d(char* out, size_t cap, uint32_t v);
extern int str_append_i32(char* out, size_t cap, int32_t v);
extern int normalize_path(const char* cwd, const char* in, char* out, size_t cap);
extern int split_parent_name(const char* full, char* parent, char* name, size_t cap);
extern void desktop_mark_input(void);
extern int desktop_wants_console_input(void);
extern void open_console_window(void);
extern void open_explorer_window(void);
extern void open_clock_window(void);
extern long desktop_launch_target_tid(const char* path);
extern long desktop_launch_by_basename_tid(const char* base);
extern long launch_settings_tid(void);
extern void desktop_rescan_icons(void);
extern void desktop_wait_ticks(uint64_t ticks);
extern void update_focus_after_visibility_change(void);
extern int set_bg_from_image(const char* path);
extern int set_bg_from_builtin(void);
extern int desktop_conf_save_layout(void);
extern const char* image_failure_reason(void);
extern void start_menu_clear_query(void);
extern void start_power_action(int action);
extern int users_add_account(const char* name, const char* pass);
extern void bg_gradient(void);

extern uint32_t* g_bg;
extern uint8_t* g_wallpaper_file;
extern size_t g_wallpaper_file_cap;
extern char g_wallpaper_pref[320];
extern int g_wallpaper_custom;
extern int g_wallpaper_builtin_enabled;
extern uint64_t g_current_uid;
extern char g_current_user[32];

void term_print_banner(void) {
    term_push_line("+---------------------------- NTux Shell ----------------------------+");
    term_push_line("| profile: NTux-OS          renderer: desktop window     status: live|");
    term_push_line("+--------------------------------------------------------------------+");
    term_push_line(" help clear ls cd run adduser whoami mkfs.ext4 lsblk fdisk dd shutdown");
    term_push_line("+--------------------------------------------------------------------+");
}

void term_make_prompt(const desk_term_state_t* ts, char* out, size_t cap) {
    ntux_time_t t;
    if (!out || cap < 4) return;
    out[0] = '\0';
    if (!ts) {
        (void)str_append(out, cap, "+--ntux @ / :: --:--:--");
        return;
    }
    (void)str_append(out, cap, "+--ntux @ ");
    (void)str_append(out, cap, ts->cwd);
    (void)str_append(out, cap, " :: ");
    if (sys_get_time(&t) == 0) {
        (void)str_append_u32_2d(out, cap, (uint32_t)t.hour);
        (void)str_append_char(out, cap, ':');
        (void)str_append_u32_2d(out, cap, (uint32_t)t.minute);
        (void)str_append_char(out, cap, ':');
        (void)str_append_u32_2d(out, cap, (uint32_t)t.second);
    } else {
        (void)str_append(out, cap, "tick ");
        (void)str_append_u64(out, cap, sys_get_ticks());
    }
}

void term_push_line_state(desk_term_state_t* ts, const char* s, uint32_t color) {
    if (!ts || !s) return;
    if (ts->line_count < DESK_TERM_LINES) {
        strncpy(ts->lines[ts->line_count], s, DESK_TERM_COLS);
        ts->lines[ts->line_count][DESK_TERM_COLS] = '\0';
        ts->line_colors[ts->line_count] = color;
        ts->line_count++;
        return;
    }
    for (int i = 1; i < DESK_TERM_LINES; ++i) {
        memcpy(ts->lines[i - 1], ts->lines[i], DESK_TERM_COLS + 1);
        ts->line_colors[i - 1] = ts->line_colors[i];
    }
    strncpy(ts->lines[DESK_TERM_LINES - 1], s, DESK_TERM_COLS);
    ts->lines[DESK_TERM_LINES - 1][DESK_TERM_COLS] = '\0';
    ts->line_colors[DESK_TERM_LINES - 1] = color;
}

void term_push_line(const char* s) {
    desk_term_state_t* ts = g_term_exec_state ? g_term_exec_state : term_state_active();
    term_push_line_state(ts, s, 0xFFBFD0FFu);
}

void term_push_line_color(const char* s, uint32_t color) {
    desk_term_state_t* ts = g_term_exec_state ? g_term_exec_state : term_state_active();
    term_push_line_state(ts, s, color);
}

void term_push_multiline(const char* s) {
    char line[DESK_TERM_COLS + 1];
    size_t li = 0;
    if (!s) return;
    for (size_t i = 0;; ++i) {
        char c = s[i];
        if (c == '\n' || c == '\0') {
            line[li] = '\0';
            term_push_line(line);
            li = 0;
            if (c == '\0') break;
            continue;
        }
        if (li + 1 >= (size_t)sizeof(line)) {
            line[li] = '\0';
            term_push_line(line);
            li = 0;
        }
        if (c == '\r') continue;
        line[li++] = c;
    }
}

void term_push_multiline_color(const char* s, uint32_t color) {
    char line[DESK_TERM_COLS + 1];
    size_t li = 0;
    if (!s) return;
    for (size_t i = 0;; ++i) {
        char c = s[i];
        if (c == '\n' || c == '\0') {
            line[li] = '\0';
            term_push_line_color(line, color);
            li = 0;
            if (c == '\0') break;
            continue;
        }
        if (li + 1 >= (size_t)sizeof(line)) {
            line[li] = '\0';
            term_push_line_color(line, color);
            li = 0;
        }
        if (c == '\r') continue;
        line[li++] = c;
    }
}

void term_push_num_u64(uint64_t v, char* out, size_t cap) {
    char tmp[32];
    int p = 0;
    size_t i = 0;
    if (!out || cap < 2) return;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0 && p < (int)sizeof(tmp)) {
        tmp[p++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (p > 0 && i + 1 < cap) out[i++] = tmp[--p];
    out[i] = '\0';
}

static int split_args(char* line, char* argv[], int maxc) {
    int argc = 0;
    char* p = line;
    while (*p && argc < maxc) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (!*p) break;
        *p++ = '\0';
    }
    return argc;
}

void term_cmd_ls(const char* cwd, const char* arg) {
    char path[256];
    ntux_dirent_t ents[DESK_LS_MAX];
    uint64_t count = 0;
    if (!arg) arg = ".";
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0) {
        term_push_line("ls: invalid path");
        return;
    }
    if (sys_fs_list_dir(path, ents, DESK_LS_MAX, &count) != 0) {
        term_push_line("ls: failed");
        return;
    }
    if (count > DESK_LS_MAX) count = DESK_LS_MAX;
    for (uint64_t i = 0; i < count; ++i) {
        char line[128];
        line[0] = '\0';
        if (ents[i].is_dir) {
            (void)str_append(line, sizeof(line), "[D] ");
            (void)str_append(line, sizeof(line), ents[i].name);
        } else {
            (void)str_append(line, sizeof(line), "[F] ");
            (void)str_append(line, sizeof(line), ents[i].name);
            (void)str_append(line, sizeof(line), " (");
            (void)str_append_u64(line, sizeof(line), ents[i].size);
            (void)str_append_char(line, sizeof(line), ')');
        }
        term_push_line(line);
    }
}

void term_push_multiline_state(desk_term_state_t* ts, const char* s, uint32_t color) {
    char line[DESK_TERM_COLS + 1];
    size_t li = 0;
    if (!s || !ts) return;
    for (size_t i = 0;; ++i) {
        char c = s[i];
        if (c == '\n' || c == '\0') {
            line[li] = '\0';
            term_push_line_state(ts, line, color);
            li = 0;
            if (c == '\0') break;
            continue;
        }
        if (li + 1 >= sizeof(line)) {
            line[li] = '\0';
            term_push_line_state(ts, line, color);
            li = 0;
        }
        if (c == '\r') continue;
        line[li++] = c;
    }
}

void desk_term_write_for_tid(int tid, const char* s) {
    if (!s || tid <= 0) return;
    int term_idx = term_route_find(tid);
    if (term_idx < 0) {
        if (g_focus_index >= 0 && g_focus_index < g_window_count && g_windows[g_focus_index].terminal) {
            term_idx = g_focus_index;
        } else {
            open_console_window();
            term_idx = g_focus_index;
        }
        if (term_idx >= 0 && term_idx < g_window_count && g_windows[term_idx].terminal) {
            term_route_register(tid, term_idx);
        }
    }
    if (term_idx < 0 || term_idx >= g_window_count) return;
    desk_window_t* w = &g_windows[term_idx];
    desk_term_state_t* ts = term_state_for_window(w);
    if (!ts) return;
    term_push_multiline_state(ts, s, 0xFFD4BFFFu);
    g_desktop_dirty = 1;
}

void term_cmd_cat(const char* cwd, const char* arg) {
    char path[256];
    char buf[DESK_CAT_MAX + 1];
    uint64_t out_len = 0;
    if (!arg) {
        term_push_line("usage: cat <path>");
        return;
    }
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0) {
        term_push_line("cat: invalid path");
        return;
    }
    if (sys_fs_read_file(path, buf, DESK_CAT_MAX, &out_len) != 0) {
        term_push_line("cat: failed");
        return;
    }
    if (out_len > DESK_CAT_MAX) out_len = DESK_CAT_MAX;
    buf[out_len] = '\0';
    term_push_multiline(buf);
}

void term_cmd_cd(char* cwd, const char* arg) {
    char path[256];
    ntux_dirent_t probe[1];
    uint64_t n = 0;
    if (!arg) arg = "/";
    if (normalize_path(cwd, arg, path, 256) != 0) {
        term_push_line("cd: invalid path");
        return;
    }
    if (sys_fs_list_dir(path, probe, 1, &n) != 0) {
        term_push_line("cd: no such directory");
        return;
    }
    strncpy(cwd, path, 127);
    cwd[127] = '\0';
}

void term_cmd_mkdir(const char* cwd, const char* arg) {
    char path[256], parent[256], name[256];
    if (!arg) {
        term_push_line("usage: mkdir <path>");
        return;
    }
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0 || split_parent_name(path, parent, name, sizeof(parent)) != 0) {
        term_push_line("mkdir: invalid path");
        return;
    }
    if (sys_fs_mkdir(parent, name) != 0) term_push_line("mkdir: failed");
}

void term_cmd_touch(const char* cwd, const char* arg) {
    char path[256], parent[256], name[256];
    if (!arg) {
        term_push_line("usage: touch <path>");
        return;
    }
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0 || split_parent_name(path, parent, name, sizeof(parent)) != 0) {
        term_push_line("touch: invalid path");
        return;
    }
    if (sys_fs_exists(path) > 0) {
        if (sys_fs_write_file(path, "", 0) != 0) term_push_line("touch: failed");
        return;
    }
    if (sys_fs_create_file(parent, name, "", 0) != 0) term_push_line("touch: failed");
}

void term_cmd_rm(const char* cwd, const char* arg) {
    char path[256];
    if (!arg) {
        term_push_line("usage: rm <path>");
        return;
    }
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0) {
        term_push_line("rm: invalid path");
        return;
    }
    if (sys_fs_remove(path) != 0) term_push_line("rm: failed");
}

void term_cmd_mv(const char* cwd, const char* old_arg, const char* new_arg) {
    char old_path[256], new_path[256];
    if (!old_arg || !new_arg) {
        term_push_line("usage: mv <old> <new>");
        return;
    }
    if (normalize_path(cwd, old_arg, old_path, sizeof(old_path)) != 0 ||
        normalize_path(cwd, new_arg, new_path, sizeof(new_path)) != 0) {
        term_push_line("mv: invalid path");
        return;
    }
    if (sys_fs_rename(old_path, new_path) != 0) term_push_line("mv: failed");
}

int parse_u64(const char* s, uint64_t* out) {
    uint64_t v = 0;
    if (!s || !s[0] || !out) return -1;
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10u + (uint64_t)(s[i] - '0');
    }
    *out = v;
    return 0;
}

void term_task_list(void) {
    ntux_task_info_t tasks[32];
    uint64_t count = 0;
    if (sys_task_list(tasks, 32u, &count) != 0) {
        term_push_line("[err] task list failed");
        return;
    }
    if (count > 32u) count = 32u;
    term_push_line("id uid state core aff");
    for (uint64_t i = 0; i < count; ++i) {
        if (!tasks[i].active) continue;
        char line[96];
        line[0] = '\0';
        (void)str_append_u64(line, sizeof(line), tasks[i].id);
        (void)str_append_char(line, sizeof(line), ' ');
        (void)str_append_u64(line, sizeof(line), (uint64_t)tasks[i].uid);
        (void)str_append_char(line, sizeof(line), ' ');
        (void)str_append_u64(line, sizeof(line), (uint64_t)tasks[i].state);
        (void)str_append_char(line, sizeof(line), ' ');
        (void)str_append_i32(line, sizeof(line), (int32_t)tasks[i].running_core);
        (void)str_append_char(line, sizeof(line), ' ');
        (void)str_append_i32(line, sizeof(line), (int32_t)tasks[i].affinity_core);
        term_push_line(line);
    }
}

static int parse_hex_u64(const char* s, uint64_t* out) {
    uint64_t v = 0;
    size_t i = 0;
    if (!s || !s[0] || !out) return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    if (!s[i]) return -1;
    for (; s[i]; ++i) {
        char c = s[i];
        uint64_t d = 0;
        if (c >= '0' && c <= '9') d = (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (uint64_t)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (uint64_t)(c - 'A');
        else return -1;
        v = (v << 4u) | d;
    }
    *out = v;
    return 0;
}

static int parse_drive_part(const char* s, uint64_t* out_drive, uint64_t* out_part) {
    const char* p = 0;
    const char* base = s;
    if (!s || !out_drive || !out_part) return -1;
    if (strncmp(base, "blk", 3) == 0) base += 3;
    for (size_t i = 0; base[i]; ++i) {
        if (base[i] == 'p') {
            p = &base[i];
            break;
        }
    }
    if (!p) return -1;
    char d[32];
    size_t dl = (size_t)(p - base);
    if (dl == 0 || dl >= sizeof(d)) return -1;
    memcpy(d, base, dl);
    d[dl] = '\0';
    if (parse_u64(d, out_drive) != 0) return -1;
    if (parse_u64(p + 1, out_part) != 0) return -1;
    if (*out_part < 1 || *out_part > 4) return -1;
    return 0;
}

static int find_partition(uint64_t drive, uint64_t part, ntux_partition_info_t* out_part) {
    ntux_partition_info_t parts[8];
    uint64_t count = 0;
    if (!out_part) return -1;
    if (sys_block_partitions(drive, parts, 8u, &count) != 0) return -1;
    if (count > 8u) count = 8u;
    for (uint64_t i = 0; i < count; ++i) {
        if (parts[i].index == (uint8_t)part) {
            *out_part = parts[i];
            return 0;
        }
    }
    return -1;
}

void term_cmd_lsblk(void) {
    ntux_block_device_info_t devs[8];
    uint64_t dev_count = 0;
    if (sys_block_list(devs, 8u, &dev_count) != 0) {
        term_push_line("lsblk: failed");
        return;
    }
    if (dev_count > 8u) dev_count = 8u;
    term_push_line("NAME      TYPE   SIZE(MiB)   INFO");
    for (uint64_t i = 0; i < dev_count; ++i) {
        if (!devs[i].present) continue;
        char line[160];
        line[0] = '\0';
        (void)str_append(line, sizeof(line), "blk");
        (void)str_append_u64(line, sizeof(line), i);
        (void)str_append(line, sizeof(line), "      ");
        (void)str_append(line, sizeof(line), devs[i].is_atapi ? "rom   " : "disk  ");
        (void)str_append_u64(line, sizeof(line), devs[i].sectors / 2048u);
        (void)str_append(line, sizeof(line), "        ");
        (void)str_append(line, sizeof(line), devs[i].model);
        term_push_line(line);

        ntux_partition_info_t parts[8];
        uint64_t part_count = 0;
        if (sys_block_partitions(i, parts, 8u, &part_count) != 0) continue;
        if (part_count > 8u) part_count = 8u;
        for (uint64_t j = 0; j < part_count; ++j) {
            char pline[160];
            pline[0] = '\0';
            (void)str_append(pline, sizeof(pline), "blk");
            (void)str_append_u64(pline, sizeof(pline), i);
            (void)str_append(pline, sizeof(pline), "p");
            (void)str_append_u64(pline, sizeof(pline), (uint64_t)parts[j].index);
            (void)str_append(pline, sizeof(pline), "    part  ");
            (void)str_append_u64(pline, sizeof(pline), (uint64_t)parts[j].sectors / 2048u);
            (void)str_append(pline, sizeof(pline), "        type=0x");
            char hx[8];
            const char* dig = "0123456789ABCDEF";
            hx[0] = dig[(parts[j].type >> 4) & 0xFu];
            hx[1] = dig[parts[j].type & 0xFu];
            hx[2] = '\0';
            (void)str_append(pline, sizeof(pline), hx);
            (void)str_append(pline, sizeof(pline), " lba=");
            (void)str_append_u64(pline, sizeof(pline), parts[j].lba_start);
            term_push_line(pline);
        }
    }
}

void term_cmd_blkrescan(void) {
    if (sys_fs_rescan() != 0) term_push_line("blkrescan: failed");
    else term_push_line("[ok] storage rescanned");
}

void term_cmd_fdisk(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "-l") == 0) {
        term_cmd_lsblk();
        return;
    }
    if (argc >= 4 && strcmp(argv[1], "delete") == 0) {
        uint64_t drive = 0;
        uint64_t part = 0;
        if (parse_u64(argv[2], &drive) != 0 || parse_u64(argv[3], &part) != 0 || part < 1 || part > 4) {
            term_push_line("usage: fdisk delete <drive> <part>");
            return;
        }
        ntux_mbr_part_req_t req;
        memset(&req, 0, sizeof(req));
        req.drive = (uint8_t)drive;
        req.part_index = (uint8_t)part;
        if (sys_block_set_mbr_partition(&req) != 0) term_push_line("fdisk: delete failed");
        else term_push_line("[ok] partition deleted");
        return;
    }
    if (argc >= 7 && strcmp(argv[1], "create") == 0) {
        uint64_t drive = 0, part = 0, start_mb = 0, size_mb = 0, type64 = 0;
        if (parse_u64(argv[2], &drive) != 0 || parse_u64(argv[3], &part) != 0 ||
            parse_u64(argv[4], &start_mb) != 0 || parse_u64(argv[5], &size_mb) != 0 ||
            parse_hex_u64(argv[6], &type64) != 0 || part < 1 || part > 4 || type64 > 0xFFu) {
            term_push_line("usage: fdisk create <drive> <part> <startMiB> <sizeMiB> <typeHex>");
            return;
        }
        ntux_mbr_part_req_t req;
        memset(&req, 0, sizeof(req));
        req.drive = (uint8_t)drive;
        req.part_index = (uint8_t)part;
        req.type = (uint8_t)type64;
        req.bootable = 0;
        req.lba_start = (uint32_t)(start_mb * 2048u);
        req.sectors = (uint32_t)(size_mb * 2048u);
        if (sys_block_set_mbr_partition(&req) != 0) term_push_line("fdisk: create failed");
        else term_push_line("[ok] partition created");
        return;
    }
    term_push_line("usage: fdisk -l");
    term_push_line("       fdisk create <drive> <part> <startMiB> <sizeMiB> <typeHex>");
    term_push_line("       fdisk delete <drive> <part>");
}

void term_cmd_mkfs(int ext4_mode, int argc, char* argv[]) {
    uint64_t drive = 0;
    uint64_t part = 0;
    ntux_partition_info_t pi;
    if (argc < 2) {
        term_push_line(ext4_mode ? "usage: mkfs.ext4 <drive>p<part>" : "usage: mkfs.ext2 <drive>p<part>");
        return;
    }
    if (parse_drive_part(argv[1], &drive, &part) != 0) {
        term_push_line("mkfs: invalid target (use <drive>p<part>)");
        return;
    }
    if (find_partition(drive, part, &pi) != 0) {
        term_push_line("mkfs: partition not found");
        return;
    }
    long rc = ext4_mode ? sys_mkfs_ext4(drive, pi.lba_start, pi.sectors) : sys_mkfs_ext2(drive, pi.lba_start, pi.sectors);
    if (rc != 0) {
        term_push_line("mkfs: failed");
        return;
    }
    term_push_line("[ok] filesystem written");
}

typedef struct {
    int is_block;
    uint64_t drive;
    uint64_t lba;
    char path[256];
} dd_endpoint_t;

static int dd_parse_endpoint(const char* cwd, const char* spec, dd_endpoint_t* out) {
    if (!cwd || !spec || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (strncmp(spec, "blk", 3) == 0) {
        const char* colon = 0;
        for (size_t i = 3; spec[i]; ++i) if (spec[i] == ':') { colon = &spec[i]; break; }
        if (!colon) return -1;
        char d[32];
        size_t dl = (size_t)(colon - (spec + 3));
        if (dl == 0 || dl >= sizeof(d)) return -1;
        memcpy(d, spec + 3, dl);
        d[dl] = '\0';
        if (parse_u64(d, &out->drive) != 0) return -1;
        if (parse_u64(colon + 1, &out->lba) != 0) return -1;
        out->is_block = 1;
        return 0;
    }
    if (normalize_path(cwd, spec, out->path, sizeof(out->path)) != 0) return -1;
    out->is_block = 0;
    return 0;
}

void term_cmd_dd(const char* cwd, int argc, char* argv[]) {
    const char* if_s = 0;
    const char* of_s = 0;
    uint64_t bs = 1;
    uint64_t count = 1;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "if=", 3) == 0) if_s = argv[i] + 3;
        else if (strncmp(argv[i], "of=", 3) == 0) of_s = argv[i] + 3;
        else if (strncmp(argv[i], "bs=", 3) == 0) { if (parse_u64(argv[i] + 3, &bs) != 0) bs = 0; }
        else if (strncmp(argv[i], "count=", 6) == 0) { if (parse_u64(argv[i] + 6, &count) != 0) count = 0; }
    }
    if (!if_s || !of_s || bs == 0 || count == 0) {
        term_push_line("usage: dd if=<blkN:LBA|/file> of=<blkN:LBA|/file> bs=<sectors> count=<blocks>");
        return;
    }

    uint64_t total_sectors = bs * count;
    uint64_t total_bytes = total_sectors * 512u;
    if (total_sectors == 0 || total_bytes == 0 || total_bytes > (2u * 1024u * 1024u)) {
        term_push_line("dd: transfer too large (max 2 MiB)");
        return;
    }

    dd_endpoint_t src, dst;
    if (dd_parse_endpoint(cwd, if_s, &src) != 0 || dd_parse_endpoint(cwd, of_s, &dst) != 0) {
        term_push_line("dd: invalid if/of endpoint");
        return;
    }

    uint8_t* buf = (uint8_t*)malloc((size_t)total_bytes);
    if (!buf) {
        term_push_line("dd: out of memory");
        return;
    }

    if (src.is_block) {
        if (sys_block_read(src.drive, src.lba, total_sectors, buf) != 0) {
            free(buf);
            term_push_line("dd: block read failed");
            return;
        }
    } else {
        uint64_t file_len = 0;
        if (sys_fs_read_file(src.path, 0, 0, &file_len) != 0 || file_len < total_bytes) {
            free(buf);
            term_push_line("dd: source file too small");
            return;
        }
        if (sys_fs_read_file(src.path, buf, total_bytes, &file_len) != 0) {
            free(buf);
            term_push_line("dd: file read failed");
            return;
        }
    }

    if (dst.is_block) {
        if (sys_block_write(dst.drive, dst.lba, total_sectors, buf) != 0) {
            free(buf);
            term_push_line("dd: block write failed");
            return;
        }
    } else {
        if (sys_fs_exists(dst.path) > 0) {
            if (sys_fs_write_file(dst.path, buf, total_bytes) != 0) {
                free(buf);
                term_push_line("dd: file write failed");
                return;
            }
        } else {
            char parent[256];
            char name[256];
            if (split_parent_name(dst.path, parent, name, sizeof(parent)) != 0 ||
                sys_fs_create_file(parent, name, buf, total_bytes) != 0) {
                free(buf);
                term_push_line("dd: file create failed");
                return;
            }
        }
    }
    free(buf);
    term_push_line("[ok] dd completed");
}

int term_write_args_file(const char* path, char* argv[], int start, int argc) {
    char buf[256];
    size_t p = 0;
    if (!path) return -1;
    buf[0] = '\0';
    for (int i = start; i < argc; ++i) {
        size_t l = strlen(argv[i]);
        if (p + l + 2 >= sizeof(buf)) break;
        memcpy(buf + p, argv[i], l);
        p += l;
        if (i + 1 < argc) buf[p++] = ' ';
    }
    buf[p] = '\0';
    if (sys_fs_write_file(path, buf, (uint64_t)p) != 0) {
        return (sys_fs_create_file("/tmp", path[0] == '/' ? path + 5 : "args", buf, (uint64_t)p) == 0) ? 0 : -1;
    }
    return 0;
}

void term_write_args_for_tid(int tid, const char* first, char* argv[], int start, int argc) {
    char path[64];
    char buf[384];
    size_t p = 0;
    if (tid <= 0) return;
    int n = snprintf(path, sizeof(path), "/tmp/args.%d", tid);
    if (n <= 0 || (size_t)n >= sizeof(path)) return;
    buf[0] = '\0';
    if (first && first[0]) {
        size_t l = strlen(first);
        if (p + l + 1 < sizeof(buf)) {
            memcpy(buf + p, first, l);
            p += l;
        }
    }
    for (int i = start; i < argc; ++i) {
        size_t l = strlen(argv[i]);
        if (p + l + 2 >= sizeof(buf)) break;
        if (p > 0) buf[p++] = ' ';
        memcpy(buf + p, argv[i], l);
        p += l;
    }
    buf[p] = '\0';
    if (sys_fs_write_file(path, buf, (uint64_t)p) != 0) {
        if (sys_fs_create_file("/tmp", path + 5, buf, (uint64_t)p) != 0) {
            (void)0;
        }
    }
}

void term_run_command_line(desk_window_t* tw, const char* line_in) {
    char line[256];
    char* argv[16];
    int argc;
    char path[256];
    desk_term_state_t* ts = term_state_for_window(tw);
    int term_idx = g_focus_index;
    if (term_idx < 0 || term_idx >= g_window_count || !g_windows[term_idx].terminal) {
        term_idx = -1;
    }

    if (!line_in || !ts) return;
    g_term_exec_state = ts;
    strncpy(line, line_in, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    term_push_line(line);

    argc = split_args(line, argv, 16);
    if (argc <= 0) return;

    if (strcmp(argv[0], "help") == 0) {
        term_push_line("help banner version clear exit");
        term_push_line("pwd cd ls ll cat mkdir touch rm mv");
    term_push_line("exists stat echo run lua konsole explorer browser clock taskmgr tetris deskdemo editor bench imgview objview paint calc snake flappy xeyes settings partutil test healthcheck whoami adduser task add task list");
        term_push_line("ticks sleep mouse netinfo setbg reboot shutdown poweroff");
        term_push_line("rescan blkrescan lsblk fdisk mkfs.ext2 mkfs.ext4 dd");
        term_push_line("dd if=<blkN:LBA|/file> of=<blkN:LBA|/file> bs=<sectors> count=<blocks>");
        return;
    }
    if (strcmp(argv[0], "banner") == 0) {
        term_print_banner();
        return;
    }
    if (strcmp(argv[0], "version") == 0) {
        term_push_line("NTux Shell Desktop Mode v0.6");
        return;
    }
    if (strcmp(argv[0], "clear") == 0) {
        ts->line_count = 0;
        term_print_banner();
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "pwd") == 0) {
        term_push_line(ts->cwd);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "cd") == 0) {
        term_cmd_cd(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "ls") == 0 || strcmp(argv[0], "ll") == 0) {
        term_cmd_ls(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "cat") == 0) {
        term_cmd_cat(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mkdir") == 0) {
        term_cmd_mkdir(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "touch") == 0) {
        term_cmd_touch(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "rm") == 0) {
        if (argc < 2) {
            term_push_line("usage: rm <path> [path...]");
        } else {
            for (int i = 1; i < argc; ++i) {
                term_cmd_rm(ts->cwd, argv[i]);
            }
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mv") == 0) {
        term_cmd_mv(ts->cwd, argc > 1 ? argv[1] : 0, argc > 2 ? argv[2] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "echo") == 0) {
        char out[DESK_TERM_COLS + 1];
        size_t p = 0;
        out[0] = '\0';
        for (int i = 1; i < argc; ++i) {
            size_t l = strlen(argv[i]);
            if (p + l + 2 >= sizeof(out)) break;
            memcpy(out + p, argv[i], l);
            p += l;
            if (i + 1 < argc) out[p++] = ' ';
        }
        out[p] = '\0';
        term_push_line(out);
        return;
    }
    if (strcmp(argv[0], "exists") == 0) {
        if (argc < 2) term_push_line("usage: exists <path>");
        else if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) term_push_line("exists: invalid path");
        else term_push_line(sys_fs_exists(path) > 0 ? "yes" : "no");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "stat") == 0) {
        uint64_t n = 0;
        if (argc < 2) {
            term_push_line("usage: stat <path>");
            return;
        }
        if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) {
            term_push_line("stat: invalid path");
            g_term_exec_state = 0;
            return;
        }
        char num[32];
        term_push_line(path);
        term_push_line(sys_fs_exists(path) > 0 ? "exists: yes" : "exists: no");
        (void)sys_fs_list_dir(path, 0, 0, &n);
        term_push_num_u64(n, num, sizeof(num));
        line[0] = '\0';
        (void)str_append(line, sizeof(line), "dir_entries: ");
        (void)str_append(line, sizeof(line), num);
        term_push_line(line);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "task") == 0 && argc >= 2 && strcmp(argv[1], "list") == 0) {
        term_task_list();
        return;
    }
    if (strcmp(argv[0], "task") == 0 && argc >= 3 && strcmp(argv[1], "add") == 0) {
        if (normalize_path(ts->cwd, argv[2], path, sizeof(path)) != 0) {
            term_push_line("[err] invalid path");
        } else {
            long tid = desktop_launch_target_tid(path);
            if (tid >= 0) {
                if (term_idx >= 0) term_route_register((int)tid, term_idx);
                term_push_line("[ok] task started");
            } else {
                term_push_line("[err] task start failed");
            }
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "lsblk") == 0) {
        term_cmd_lsblk();
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "fdisk") == 0) {
        term_cmd_fdisk(argc, argv);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mkfs.ext2") == 0) {
        term_cmd_mkfs(0, argc, argv);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mkfs.ext4") == 0) {
        term_cmd_mkfs(1, argc, argv);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "dd") == 0) {
        term_cmd_dd(ts->cwd, argc, argv);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "blkrescan") == 0) {
        term_cmd_blkrescan();
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "partutil") == 0) {
        term_push_line("[partutil] launching partition utility...");
        long tid = sys_task_add_module("partutil");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/partutil.elf");
        if (tid >= 0) {
            term_write_args_for_tid((int)tid, "partutil", argv, 1, argc);
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[partutil] partition utility started");
        } else {
            term_push_line("[partutil] failed to launch partition utility");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "test") == 0) {
        term_push_line("[test] launching desktop api test...");
        long tid = sys_task_add_module("test");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/test.elf");
        if (tid >= 0) {
            term_write_args_for_tid((int)tid, "test", argv, 1, argc);
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[test] test started");
        } else {
            term_push_line("[test] failed to launch test");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "setbg") == 0 && argc >= 2) {
        if (strcmp(argv[1], "off") == 0) {
            if (g_bg) {
                free(g_bg);
                g_bg = 0;
            }
            if (g_wallpaper_file) {
                free(g_wallpaper_file);
                g_wallpaper_file = 0;
                g_wallpaper_file_cap = 0;
            }
            bg_gradient();
            strncpy(g_wallpaper_pref, "gradient", sizeof(g_wallpaper_pref) - 1);
            g_wallpaper_pref[sizeof(g_wallpaper_pref) - 1] = '\0';
            g_wallpaper_custom = 0;
            if (g_wallpaper_builtin_enabled) {
                (void)set_bg_from_builtin();
            }
            (void)desktop_conf_save_layout();
            term_push_line("[ok] background reset");
            g_term_exec_state = 0;
            return;
        }
        if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) {
            term_push_line("[err] invalid path");
            g_term_exec_state = 0;
            return;
        }
        if (set_bg_from_image(path) != 0) {
            char errbuf[128];
            snprintf(errbuf, sizeof(errbuf), "[err] setbg failed: %s", image_failure_reason());
            term_push_line(errbuf);
        } else {
            char cfg[320];
            int n = snprintf(cfg, sizeof(cfg), "img:%s", path);
            if (n > 0 && (size_t)n < sizeof(cfg)) {
                strncpy(g_wallpaper_pref, cfg, sizeof(g_wallpaper_pref) - 1);
                g_wallpaper_pref[sizeof(g_wallpaper_pref) - 1] = '\0';
                g_wallpaper_custom = 1;
                (void)desktop_conf_save_layout();
            }
            term_push_line("[ok] background updated");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "setbg") == 0) {
        term_push_line("usage: setbg <path>|off");
        return;
    }
    if (strcmp(argv[0], "run") == 0 && argc >= 2) {
        if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) {
            term_push_line("[err] invalid path");
            g_term_exec_state = 0;
            return;
        }
        if (argc > 2) {
            (void)term_write_args_file("/tmp/run.args", argv, 2, argc);
            if (sys_fs_write_file("/tmp/run.path", path, (uint64_t)strlen(path)) != 0) {
                (void)sys_fs_create_file("/tmp", "run.path", path, (uint64_t)strlen(path));
            }
        }
        long tid = desktop_launch_target_tid(path);
        if (tid >= 0) {
            term_write_args_for_tid((int)tid, path, argv, 2, argc);
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] task started");
        } else {
            term_push_line("[err] task start failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "run") == 0) {
        term_push_line("usage: run <path> [args...]");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "lua") == 0 && argc >= 2) {
        if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) {
            term_push_line("[err] invalid lua path");
            g_term_exec_state = 0;
            return;
        }
        if (sys_fs_write_file("/tmp/lua.run", path, (uint64_t)strlen(path)) != 0) {
            (void)sys_fs_create_file("/tmp", "lua.run", path, (uint64_t)strlen(path));
        }
        if (argc > 2) {
            (void)term_write_args_file("/tmp/lua.args", argv, 2, argc);
        } else {
            (void)sys_fs_remove("/tmp/lua.args");
        }
        long tid = sys_task_add_module("klua");
        if (tid < 0) tid = sys_task_add_module("lua");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/lua.elf");
        if (tid >= 0) {
            term_write_args_for_tid((int)tid, "lua", argv, 1, argc);
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] lua started");
        } else {
            term_push_line("[err] lua start failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "lua") == 0) {
        term_push_line("usage: lua <file.lua> [args...]");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "konsole") == 0) {
        long tid = sys_task_add_module("deskconsole");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/deskconsole.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] konsole started");
        } else {
            open_console_window();
            term_push_line("[ok] konsole window opened");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "taskmgr") == 0) {
        long tid = sys_task_add_module("taskmgr");
        if (tid < 0) tid = desktop_launch_target_tid("taskmgr.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] taskmgr started");
        } else {
            term_push_line("[err] taskmgr failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "tetris") == 0) {
        long tid = sys_task_add_module("tetris");
        if (tid < 0) tid = desktop_launch_target_tid("tetris.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] tetris started");
        } else {
            term_push_line("[err] tetris failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "deskdemo") == 0) {
        long tid = sys_task_add_module("deskdemo");
        if (tid < 0) tid = desktop_launch_target_tid("deskdemo.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] deskdemo started");
        } else {
            term_push_line("[err] deskdemo failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "editor") == 0) {
        long tid = sys_task_add_module("editor");
        if (tid < 0) tid = desktop_launch_target_tid("editor.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] editor started");
        } else {
            term_push_line("[err] editor failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "bench") == 0) {
        long tid = sys_task_add_module("bench");
        if (tid < 0) tid = desktop_launch_target_tid("bench.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] bench started");
        } else {
            term_push_line("[err] bench failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "imgview") == 0) {
        long tid = sys_task_add_module("imgview");
        if (tid < 0) tid = desktop_launch_target_tid("imgview.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] imgview started");
        } else {
            term_push_line("[err] imgview failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "paint") == 0) {
        long tid = sys_task_add_module("paint");
        if (tid < 0) tid = desktop_launch_target_tid("paint.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] paint started");
        } else {
            term_push_line("[err] paint failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "calc") == 0) {
        long tid = sys_task_add_module("calc");
        if (tid < 0) tid = desktop_launch_target_tid("calc.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] calc started");
        } else {
            term_push_line("[err] calc failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "browser") == 0) {
        long tid = sys_task_add_module("browser");
        if (tid < 0) tid = desktop_launch_target_tid("browser.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] browser started");
        } else {
            term_push_line("[err] browser failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "snake") == 0) {
        long tid = sys_task_add_module("snake");
        if (tid < 0) tid = desktop_launch_target_tid("snake.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] snake started");
        } else {
            term_push_line("[err] snake failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "flappy") == 0) {
        long tid = sys_task_add_module("flappy");
        if (tid < 0) tid = desktop_launch_target_tid("flappy.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] flappy started");
        } else {
            term_push_line("[err] flappy failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "objview") == 0) {
        long tid = sys_task_add_module("objview");
        if (tid < 0) tid = desktop_launch_target_tid("objview.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] objview started");
        } else {
            term_push_line("[err] objview failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "healthcheck") == 0) {
        long tid = sys_task_add_module("healthcheck");
        if (tid < 0) tid = desktop_launch_target_tid("healthcheck.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] healthcheck started");
        } else {
            term_push_line("[err] healthcheck failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "xeyes") == 0) {
        long tid = sys_task_add_module("xeyes");
        if (tid < 0) tid = desktop_launch_target_tid("xeyes.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] xeyes started");
        } else {
            term_push_line("[err] xeyes failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "settings") == 0) {
        long tid = launch_settings_tid();
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] settings started");
        } else {
            term_push_line("[err] settings failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "explorer") == 0) {
        long tid = sys_task_add_module("explorer");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/explorer.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] explorer started");
        } else {
            open_explorer_window();
            term_push_line("[ok] explorer opened");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "clock") == 0) {
        open_clock_window();
        term_push_line("[ok] analog clock opened");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "ticks") == 0) {
        line[0] = '\0';
        (void)str_append(line, sizeof(line), "ticks=");
        (void)str_append_u64(line, sizeof(line), sys_get_ticks());
        term_push_line(line);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "whoami") == 0) {
        char who[96];
        who[0] = '\0';
        (void)str_append(who, sizeof(who), g_current_user);
        (void)str_append(who, sizeof(who), " uid=");
        (void)str_append_u64(who, sizeof(who), g_current_uid);
        term_push_line(who);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "adduser") == 0) {
        if (argc < 3) {
            term_push_line("usage: adduser <name> <password>");
        } else if (users_add_account(argv[1], argv[2]) != 0) {
            term_push_line("[err] adduser failed");
        } else {
            term_push_line("[ok] user created");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "sleep") == 0) {
        uint64_t t = 0;
        if (argc < 2 || parse_u64(argv[1], &t) != 0) term_push_line("usage: sleep <ticks>");
        else desktop_wait_ticks(t);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mouse") == 0) {
        ntux_mouse_state_t ms;
        if (sys_mouse_get_state(&ms) != 0) {
            term_push_line("mouse: failed");
            return;
        }
        line[0] = '\0';
        (void)str_append(line, sizeof(line), "x=");
        (void)str_append_i32(line, sizeof(line), ms.x);
        (void)str_append(line, sizeof(line), " y=");
        (void)str_append_i32(line, sizeof(line), ms.y);
        (void)str_append(line, sizeof(line), " scroll=");
        (void)str_append_i32(line, sizeof(line), ms.scroll);
        (void)str_append(line, sizeof(line), " LRM=");
        (void)str_append_u64(line, sizeof(line), (uint64_t)ms.left);
        (void)str_append_u64(line, sizeof(line), (uint64_t)ms.right);
        (void)str_append_u64(line, sizeof(line), (uint64_t)ms.middle);
        term_push_line(line);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "netinfo") == 0 || strcmp(argv[0], "netstat") == 0) {
        char out[1024];
        long rc = sys_net_debug(out, sizeof(out));
        if (rc == 0) {
            term_push_multiline(out);
        } else {
            line[0] = '\0';
            (void)str_append(line, sizeof(line), "netinfo: failed rc=");
            (void)str_append_u64(line, sizeof(line), (uint64_t)(rc < 0 ? -rc : rc));
            term_push_line(line);
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "reboot") == 0) {
        term_push_line("rebooting in 5s...");
        start_power_action(1);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "shutdown") == 0 || strcmp(argv[0], "poweroff") == 0) {
        term_push_line("shutting down in 5s...");
        start_power_action(2);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "rescan") == 0) {
        desktop_rescan_icons();
        term_push_line("[ok] icons rescanned");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "exit") == 0) {
        if (g_focus_index >= 0 && g_focus_index < g_window_count) {
            g_windows[g_focus_index].visible = 0;
            g_windows[g_focus_index].minimized = 0;
            update_focus_after_visibility_change();
        }
        g_term_exec_state = 0;
        return;
    }

    line[0] = '\0';
    (void)str_append(line, sizeof(line), "[err] unknown command: ");
    (void)str_append(line, sizeof(line), argv[0]);
    term_push_line(line);
    g_term_exec_state = 0;
}

void term_run_command(void) {
    desk_window_t* w;
    char line[256];
    if (g_focus_index < 0 || g_focus_index >= g_window_count) return;
    w = &g_windows[g_focus_index];
    desk_term_state_t* ts = term_state_for_window(w);
    if (!ts) return;
    memcpy(line, ts->input, (size_t)ts->input_len);
    line[ts->input_len] = '\0';
    term_run_command_line(w, line);
}

void term_route_register(int tid, int term_idx) {
    if (tid <= 0) return;
    if (term_idx < 0 || term_idx >= g_window_count) return;
    if (!g_windows[term_idx].terminal) return;
    for (int i = 0; i < DESK_TERM_ROUTE_MAX; ++i) {
        if (g_term_routes[i].tid == tid) {
            g_term_routes[i].term_idx = term_idx;
            return;
        }
    }
    for (int i = 0; i < DESK_TERM_ROUTE_MAX; ++i) {
        if (g_term_routes[i].tid == 0) {
            g_term_routes[i].tid = tid;
            g_term_routes[i].term_idx = term_idx;
            return;
        }
    }
    int slot = tid % DESK_TERM_ROUTE_MAX;
    if (slot < 0) slot = -slot;
    g_term_routes[slot].tid = tid;
    g_term_routes[slot].term_idx = term_idx;
}

int term_route_find(int tid) {
    if (tid <= 0) return -1;
    for (int i = 0; i < DESK_TERM_ROUTE_MAX; ++i) {
        if (g_term_routes[i].tid == tid) {
            int idx = g_term_routes[i].term_idx;
            if (idx >= 0 && idx < g_window_count && g_windows[idx].terminal) {
                return idx;
            }
            return -1;
        }
    }
    return -1;
}

char poll_char(void) {
    uint64_t now = sys_get_ticks();
    const uint64_t debounce = 12u;
    const uint64_t same_char_guard = 16u;
    if (!desktop_wants_console_input()) {
        return 0;
    }
    if (sys_console_claim() != 0) {
        return 0;
    }
    if (now - g_last_key_tick < debounce) return 0;

    if (poll_special_press(0x1C)) { g_last_key_tick = now; g_last_key_char = '\n'; desktop_mark_input(); return '\n'; }
    if (poll_special_press(0x0E)) { g_last_key_tick = now; g_last_key_char = '\b'; desktop_mark_input(); return '\b'; }
    if (poll_special_press(0x0F)) { g_last_key_tick = now; g_last_key_char = '\t'; desktop_mark_input(); return '\t'; }
    if (poll_special_press(0x39)) { g_last_key_tick = now; g_last_key_char = ' '; desktop_mark_input(); return ' '; }

    long v = sys_getchar();
    if (v >= 0 && v <= 255) {
        char c = (char)(uint8_t)v;
        if (c >= 32 && c < 127) {
            if (c == g_last_key_char && now - g_last_key_tick < same_char_guard) return 0;
            g_last_key_tick = now;
            g_last_key_char = c;
            desktop_mark_input();
            return c;
        }
    }
    return 0;
}

int poll_special_press(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    if (pressed) desktop_mark_input();
    return pressed;
}

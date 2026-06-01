#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>

#define SHELL_LINE_MAX 256
#define SHELL_ARGV_MAX 8
#define SHELL_PATH_MAX 256
#define SHELL_LS_MAX 64
#define SHELL_CAT_MAX 4096
#define SHELL_BG_MAX_FILE (48u * 1024u * 1024u)
#define LUA_RUN_PATH1 "/tmp/lua.run"
#define LUA_RUN_PATH2 "/boot/boot/modules/lua.run"
#define LUA_RUN_PATH3 "/boot/modules/lua.run"

enum {
    UI_COLOR_BG = 0xFF05070A,
    UI_COLOR_TEXT = 0xFFBFD0FF,
    UI_COLOR_DIM = 0xFF6E7B99,
    UI_COLOR_PANEL = 0xFF1FC2D8,
    UI_COLOR_ACCENT = 0xFF39FF88,
    UI_COLOR_WARN = 0xFFFF5C7A,
    UI_COLOR_INFO = 0xFFFFD166
};

static void print_u64(uint64_t v);
static int normalize_path(const char* cwd, const char* in, char out[SHELL_PATH_MAX]);
static void print_two_digits(uint32_t v);
static int cmd_setbg(const char* cwd, const char* arg);
static const char* path_basename_ptr(const char* path);

static uint32_t* g_bg_pixels = 0;
static uint32_t g_bg_width = 0;
static uint32_t g_bg_height = 0;
static uint32_t g_bg_pitch = 0;
static int g_bg_enabled = 0;
enum {
    INPUT_PASS_NONE = 0,
    INPUT_PASS_WAIT_CLAIM = 1,
    INPUT_PASS_WAIT_RELEASE = 2
};
static uint8_t g_input_passthrough = INPUT_PASS_NONE;

static uint16_t rd_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t rd_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t rd_i32_le(const uint8_t* p) {
    return (int32_t)rd_u32_le(p);
}

static int parse_ipv4(const char* s, uint32_t* out) {
    if (!s || !s[0] || !out) return -1;
    uint32_t parts[4] = {0};
    int pi = 0;
    for (int i = 0;; ++i) {
        char c = s[i];
        if (c == '.' || c == '\0') {
            if (pi > 3) return -1;
            if (parts[pi] > 255) return -1;
            if (c == '\0') break;
            pi++;
            continue;
        }
        if (c < '0' || c > '9') return -1;
        parts[pi] = parts[pi] * 10u + (uint32_t)(c - '0');
        if (parts[pi] > 255) return -1;
    }
    if (pi != 3) return -1;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

static const char* path_basename_ptr(const char* path) {
    const char* base = path;
    if (!path) return "";
    for (const char* p = path; *p; ++p) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

static void free_bg(void) {
    if (g_bg_pixels) {
        free(g_bg_pixels);
        g_bg_pixels = 0;
    }
    g_bg_width = 0;
    g_bg_height = 0;
    g_bg_pitch = 0;
    g_bg_enabled = 0;
}

static int load_bmp_for_background(const char* path) {
    uint64_t file_len = 0;
    uint8_t* file = 0;
    uint32_t* pixels = 0;
    int rc = -1;
    ntux_fb_info_t fb;

    if (sys_fs_read_file(path, 0, 0, &file_len) != 0 || file_len < 54u) {
        puts("setbg: failed to read file size");
        return -1;
    }
    if (file_len > SHELL_BG_MAX_FILE) {
        puts("setbg: file too large");
        return -1;
    }

    file = (uint8_t*)malloc((size_t)file_len);
    if (!file) {
        puts("setbg: out of memory");
        return -1;
    }
    if (sys_fs_read_file(path, file, file_len, &file_len) != 0 || file_len < 54u) {
        puts("setbg: failed to read file");
        goto done;
    }

    if (rd_u16_le(file) != 0x4D42u) {
        puts("setbg: not a BMP file");
        goto done;
    }

    uint32_t pixel_off = rd_u32_le(file + 10u);
    uint32_t dib_size = rd_u32_le(file + 14u);
    int32_t width_i = rd_i32_le(file + 18u);
    int32_t height_i = rd_i32_le(file + 22u);
    uint16_t planes = rd_u16_le(file + 26u);
    uint16_t bpp = rd_u16_le(file + 28u);
    uint32_t compression = rd_u32_le(file + 30u);

    if (dib_size < 40u || planes != 1u || (bpp != 24u && bpp != 32u) || compression != 0u) {
        puts("setbg: unsupported BMP format (need 24/32-bit uncompressed)");
        goto done;
    }
    if (width_i <= 0 || height_i == 0) {
        puts("setbg: invalid BMP dimensions");
        goto done;
    }

    uint32_t width = (uint32_t)width_i;
    uint32_t height = (height_i < 0) ? (uint32_t)(-height_i) : (uint32_t)height_i;
    if (sys_fb_get_info(&fb) != 0 || fb.width == 0 || fb.height == 0) {
        puts("setbg: framebuffer unavailable");
        goto done;
    }
    uint32_t out_w = (width > fb.width) ? fb.width : width;
    uint32_t out_h = (height > fb.height) ? fb.height : height;
    if (out_w == 0 || out_h == 0) {
        puts("setbg: invalid output size");
        goto done;
    }

    uint64_t row_bits = (uint64_t)width * (uint64_t)bpp;
    uint64_t row_stride = ((row_bits + 31u) / 32u) * 4u;
    uint64_t pixel_bytes = row_stride * (uint64_t)height;
    if ((uint64_t)pixel_off + pixel_bytes > file_len) {
        puts("setbg: corrupted BMP payload");
        goto done;
    }

    if ((uint64_t)out_w * (uint64_t)out_h > ((uint64_t)~0u / sizeof(uint32_t))) {
        puts("setbg: image dimensions overflow");
        goto done;
    }

    pixels = (uint32_t*)malloc((size_t)((uint64_t)out_w * (uint64_t)out_h * sizeof(uint32_t)));
    if (!pixels) {
        puts("setbg: out of memory");
        goto done;
    }

    const uint8_t* src_base = file + pixel_off;
    int is_bottom_up = (height_i > 0);
    for (uint32_t y = 0; y < out_h; ++y) {
        uint32_t src_y = is_bottom_up ? (height - 1u - y) : y;
        const uint8_t* row = src_base + ((uint64_t)src_y * row_stride);
        for (uint32_t x = 0; x < out_w; ++x) {
            const uint8_t* px = row + ((uint64_t)x * (uint64_t)(bpp / 8u));
            uint8_t b = px[0];
            uint8_t g = px[1];
            uint8_t r = px[2];
            pixels[(uint64_t)y * (uint64_t)out_w + (uint64_t)x] =
                ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    free_bg();
    g_bg_pixels = pixels;
    g_bg_width = out_w;
    g_bg_height = out_h;
    g_bg_pitch = out_w * 4u;
    g_bg_enabled = 1;
    pixels = 0;
    rc = 0;

done:
    if (pixels) free(pixels);
    if (file) free(file);
    return rc;
}

static void ui_color(uint32_t color) {
    (void)sys_set_text_color(color);
}

static void ui_reset(void) {
    ui_color(UI_COLOR_TEXT);
}

static void print_banner(void) {
    ui_color(UI_COLOR_PANEL);
    puts("+---------------------------- NTux Shell ----------------------------+");
    ui_color(UI_COLOR_DIM);
    puts("| profile: NTux-OS          renderer: framebuffer        status: live |");
    ui_color(UI_COLOR_PANEL);
    puts("+--------------------------------------------------------------------+");
    ui_reset();
    puts(" help  clear  ls  cd  run /boot/modules/doom.elf  taskmgr  tetris  deskdemo  shutdown");
    ui_color(UI_COLOR_PANEL);
    puts("+--------------------------------------------------------------------+");
    ui_reset();
}

static void print_prompt(const char* cwd) {
    ntux_time_t now;
    int has_time = (sys_get_time(&now) == 0);

    ui_color(UI_COLOR_PANEL);
    sys_write("+--", 3);
    ui_color(UI_COLOR_ACCENT);
    sys_write("ntux", 4);
    ui_color(UI_COLOR_DIM);
    sys_write(" @ ", 3);
    ui_color(UI_COLOR_TEXT);
    sys_write(cwd, strlen(cwd));
    ui_color(UI_COLOR_DIM);
    sys_write(" :: ", 4);
    ui_color(UI_COLOR_INFO);
    if (has_time) {
        print_two_digits(now.hour);
        sys_write(":", 1);
        print_two_digits(now.minute);
        sys_write(":", 1);
        print_two_digits(now.second);
    } else {
        sys_write("tick ", 5);
        print_u64(sys_get_ticks());
    }
    ui_color(UI_COLOR_PANEL);
    sys_write("\n+->", 4);
    ui_reset();
    sys_write(" ", 1);
}

static void print_help(void) {
    ui_color(UI_COLOR_PANEL);
    puts("System");
    ui_reset();
    puts("  help  banner  version  clear  exit");
    puts("  setbg <path.bmp>|off");
    puts("  ticks  sleep <ticks>  mouse  ping <host>  netinfo  netstat");
    puts("  dns <ip>  httpget <url>  time  uptime  meminfo  cpuinfo");
    puts("  diskstats  gpuinfo  modules");
    puts("  reboot  shutdown  poweroff");
    puts("");
    ui_color(UI_COLOR_PANEL);
    puts("Filesystem");
    ui_reset();
    puts("  pwd  cd <path>  ls [path]  ll [path]");
    puts("  cat <path>  mkdir <path>  touch <path>");
    puts("  rm <path>  mv <old> <new>");
    puts("  exists <path>  stat <path>  echo <text>");
    puts("");
    ui_color(UI_COLOR_PANEL);
    puts("Tasks");
    ui_reset();
    puts("  run <path>");
    puts("  tcc <flags> <files>");
    puts("  taskmgr  tetris  deskdemo  editor");
    puts("  task add <path>");
    puts("  task list");
    puts("");
    ui_color(UI_COLOR_PANEL);
    puts("Quick start");
    ui_reset();
    puts("  run /boot/modules/doom.elf");
    puts("  run /boot/modules/hello.elf");
    puts("  tcc /boot/modules/tcc_example.c -o /tmp/hello.elf");
    puts("  taskmgr");
    puts("  tetris");
    puts("  deskdemo");
}

static void print_task_result(const char* path, long rc) {
    if (rc >= 0) {
        ui_color(UI_COLOR_ACCENT);
        sys_write("[ok] ", 5);
        ui_reset();
        sys_write("task started: ", 14);
        puts(path);
        ui_color(UI_COLOR_DIM);
        puts("hint: use 'task list' to inspect scheduler state");
        ui_reset();
    } else {
        ui_color(UI_COLOR_WARN);
        sys_write("[err] ", 6);
        ui_reset();
        sys_write("task start failed: ", 19);
        puts(path);
    }
}

static int task_is_alive(int tid) {
    ntux_task_info_t tasks[64];
    uint64_t count = 0;
    if (sys_task_list(tasks, 64, &count) != 0) return 0;
    for (uint64_t i = 0; i < count && i < 64; ++i) {
        if (!tasks[i].active) continue;
        if ((int)tasks[i].id == tid) return 1;
    }
    return 0;
}

static void wait_task_exit(int tid) {
    if (tid < 0) return;
    while (task_is_alive(tid)) {
        sys_yield();
    }
}

static void shell_clear_and_banner(void) {
    sys_clear_screen(UI_COLOR_BG);
    if (g_bg_enabled && g_bg_pixels) {
        (void)sys_fb_blit32((const void*)g_bg_pixels, g_bg_width, g_bg_height, g_bg_pitch);
    }
    ui_reset();
    print_banner();
    putchar('\n');
}

static int write_lua_runfile(const char* cwd, const char* script_arg) {
    if (!cwd || !script_arg || !script_arg[0]) return -1;
    char script_path[SHELL_PATH_MAX];
    if (normalize_path(cwd, script_arg, script_path) != 0) return -1;
    const char* targets[] = { LUA_RUN_PATH1, LUA_RUN_PATH2, LUA_RUN_PATH3 };
    size_t len = strlen(script_path);
    for (size_t i = 0; i < (sizeof(targets) / sizeof(targets[0])); ++i) {
        const char* t = targets[i];
        if (sys_fs_write_file(t, script_path, len) == 0) return 0;
        const char* slash = t + strlen(t);
        while (slash > t && slash[-1] != '/') slash--;
        if (slash > t && slash[0]) {
            char parent[SHELL_PATH_MAX];
            size_t plen = (size_t)(slash - t - 1u);
            if (plen > 0 && plen < sizeof(parent)) {
                memcpy(parent, t, plen);
                parent[plen] = '\0';
                if (sys_fs_create_file(parent, slash, script_path, len) == 0) return 0;
            }
        }
    }
    return -1;
}


static void write_args_for_tid(int tid, const char* first, char* argv[], int start, int argc) {
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
        (void)sys_fs_create_file("/tmp", path + 5, buf, (uint64_t)p);
    }
}

static int shell_launch_task(const char* cwd, const char* arg, const char* cmd_name,
                             char* argv[], int arg_start, int argc) {
    char path[SHELL_PATH_MAX];
    if (!arg) {
        sys_write(cmd_name, strlen(cmd_name));
        puts(": missing path");
        return -1;
    }
    if (normalize_path(cwd, arg, path) != 0) {
        sys_write(cmd_name, strlen(cmd_name));
        puts(": invalid path");
        return -1;
    }
    if (sys_fs_exists(path) <= 0) {
        sys_write(cmd_name, strlen(cmd_name));
        puts(": file not found");
        return -1;
    }
    (void)sys_console_release();
    g_input_passthrough = INPUT_PASS_WAIT_CLAIM;
    long rc = -1;
    const char* base = path_basename_ptr(path);
    const char* extra = (arg_start < argc) ? argv[arg_start] : 0;
    if (strcmp(base, "lua.elf") == 0) {
        if (extra && extra[0]) {
            if (write_lua_runfile(cwd, extra) != 0) {
                puts("[warn] lua runfile: failed to pass script path");
            }
        }
        rc = sys_task_add_module("lua");
        if (rc != 0) rc = sys_task_add(path);
    } else {
        rc = sys_task_add(path);
    }
    print_task_result(path, rc);
    if (rc >= 0) {
        if (arg_start < argc) {
            write_args_for_tid((int)rc, path, argv, arg_start, argc);
        }
        (void)sys_yield();
        (void)sys_yield();
        wait_task_exit((int)rc);
    }
    return rc;
}

static void print_unknown(const char* cmd) {
    ui_color(UI_COLOR_WARN);
    sys_write("[err] ", 6);
    ui_reset();
    sys_write("unknown command: ", 17);
    puts(cmd ? cmd : "(null)");
    ui_color(UI_COLOR_DIM);
    puts("type 'help' for commands");
    ui_reset();
}

static void print_path_only(const char* cwd) {
    sys_write(cwd, strlen(cwd));
    putchar('\n');
}

static int parse_u64(const char* s, uint64_t* out) {
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

static void print_u64(uint64_t v) {
    char buf[32];
    int p = 0;
    if (v == 0) {
        putchar('0');
        return;
    }
    while (v > 0 && p < (int)sizeof(buf)) {
        buf[p++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (p > 0) putchar(buf[--p]);
}

static void print_two_digits(uint32_t v) {
    putchar((int)('0' + ((v / 10u) % 10u)));
    putchar((int)('0' + (v % 10u)));
}

static void print_i64(int64_t v) {
    if (v < 0) {
        putchar('-');
        print_u64((uint64_t)(-v));
        return;
    }
    print_u64((uint64_t)v);
}

static int split_args(char* line, char* argv[SHELL_ARGV_MAX]) {
    int argc = 0;
    char* p = line;
    while (*p && argc < SHELL_ARGV_MAX) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (!*p) break;
        *p++ = '\0';
    }
    return argc;
}

static int split_parent_name(const char* full, char* parent, char* name) {
    const char* slash;
    size_t plen;
    size_t nlen;
    if (!full || full[0] != '/') return -1;
    slash = 0;
    for (const char* p = full; *p; ++p) {
        if (*p == '/') slash = p;
    }
    if (!slash || !slash[1]) return -1;
    plen = (size_t)(slash - full);
    nlen = strlen(slash + 1);
    if (nlen == 0 || nlen >= SHELL_PATH_MAX) return -1;
    if (plen == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        if (plen >= SHELL_PATH_MAX) return -1;
        memcpy(parent, full, plen);
        parent[plen] = '\0';
    }
    memcpy(name, slash + 1, nlen + 1);
    return 0;
}

static int normalize_path(const char* cwd, const char* in, char out[SHELL_PATH_MAX]) {
    const char* src = in;
    char temp[SHELL_PATH_MAX];
    size_t tlen = 0;

    if (!in || !in[0]) return -1;
    if (in[0] == '/') {
        while (in[tlen] && tlen + 1 < sizeof(temp)) {
            temp[tlen] = in[tlen];
            tlen++;
        }
        temp[tlen] = '\0';
    } else if ((strcmp(cwd, "/") == 0 || strcmp(cwd, "/boot") == 0) &&
               strncmp(in, "boot/", 5) == 0) {
        temp[tlen++] = '/';
        for (size_t i = 0; in[i] && tlen + 1 < sizeof(temp); ++i) {
            temp[tlen++] = in[i];
        }
        temp[tlen] = '\0';
    } else {
        size_t clen = strlen(cwd);
        if (clen == 0 || cwd[0] != '/') return -1;
        if (clen >= sizeof(temp) - 1) return -1;
        memcpy(temp, cwd, clen);
        tlen = clen;
        if (tlen > 1 && temp[tlen - 1] == '/') tlen--;
        if (tlen + 1 >= sizeof(temp)) return -1;
        temp[tlen++] = '/';
        while (*in && tlen + 1 < sizeof(temp)) temp[tlen++] = *in++;
        temp[tlen] = '\0';
    }

    src = temp;
    out[0] = '/';
    out[1] = '\0';
    while (*src) {
        while (*src == '/') src++;
        if (!*src) break;

        char seg[SHELL_PATH_MAX];
        size_t sl = 0;
        while (src[sl] && src[sl] != '/' && sl + 1 < sizeof(seg)) {
            seg[sl] = src[sl];
            sl++;
        }
        seg[sl] = '\0';
        src += sl;

        if (strcmp(seg, ".") == 0) continue;
        if (strcmp(seg, "..") == 0) {
            size_t ol = strlen(out);
            if (ol > 1) {
                size_t j = ol - 1;
                while (j > 0 && out[j] != '/') j--;
                if (j == 0) {
                    out[1] = '\0';
                } else {
                    out[j] = '\0';
                }
            }
            continue;
        }

        size_t ol = strlen(out);
        if (ol > 1) {
            if (ol + 1 >= SHELL_PATH_MAX) return -1;
            out[ol++] = '/';
            out[ol] = '\0';
        }
        if (ol + sl >= SHELL_PATH_MAX) return -1;
        memcpy(out + ol, seg, sl + 1);
    }
    return 0;
}

static void cmd_ls(const char* cwd, const char* arg) {
    char path[SHELL_PATH_MAX];
    ntux_dirent_t ents[SHELL_LS_MAX];
    uint64_t count = 0;
    uint64_t show = 0;

    if (!arg) arg = ".";
    if (normalize_path(cwd, arg, path) != 0) {
        puts("ls: invalid path");
        return;
    }
    long rc = sys_fs_list_dir(path, ents, SHELL_LS_MAX, &count);
    if (rc != 0) {
        sys_write("ls: failed rc=", 13);
        if (rc < 0) {
            putchar('-');
            print_u64((uint64_t)(-rc));
        } else {
            print_u64((uint64_t)rc);
        }
        putchar('\n');
        return;
    }
    if (count > SHELL_LS_MAX) count = SHELL_LS_MAX;
    for (show = 0; show < count; ++show) {
        if (ents[show].is_dir) sys_write("[D] ", 4);
        else sys_write("[F] ", 4);
        sys_write(ents[show].name, strlen(ents[show].name));
        if (!ents[show].is_dir) {
            sys_write(" (", 2);
            print_u64(ents[show].size);
            sys_write(")", 1);
        }
        putchar('\n');
    }
}

static void cmd_cat(const char* cwd, const char* arg) {
    char path[SHELL_PATH_MAX];
    char buf[SHELL_CAT_MAX + 1];
    uint64_t out_len = 0;

    if (!arg) {
        puts("usage: cat <path>");
        return;
    }
    if (normalize_path(cwd, arg, path) != 0) {
        puts("cat: invalid path");
        return;
    }
    if (sys_fs_read_file(path, buf, SHELL_CAT_MAX, &out_len) != 0) {
        puts("cat: failed");
        return;
    }
    if (out_len > SHELL_CAT_MAX) out_len = SHELL_CAT_MAX;
    buf[out_len] = '\0';
    sys_write(buf, (size_t)out_len);
    if (out_len == 0 || buf[out_len - 1] != '\n') putchar('\n');
}

static void cmd_mkdir(const char* cwd, const char* arg) {
    char path[SHELL_PATH_MAX];
    char parent[SHELL_PATH_MAX];
    char name[SHELL_PATH_MAX];
    if (!arg) {
        puts("usage: mkdir <path>");
        return;
    }
    if (normalize_path(cwd, arg, path) != 0 || split_parent_name(path, parent, name) != 0) {
        puts("mkdir: invalid path");
        return;
    }
    long rc = sys_fs_mkdir(parent, name);
    if (rc != 0) {
        sys_write("mkdir: failed rc=", 17);
        if (rc < 0) {
            putchar('-');
            print_u64((uint64_t)(-rc));
        } else {
            print_u64((uint64_t)rc);
        }
        putchar('\n');
    }
}

static void cmd_touch(const char* cwd, const char* arg) {
    char path[SHELL_PATH_MAX];
    char parent[SHELL_PATH_MAX];
    char name[SHELL_PATH_MAX];
    if (!arg) {
        puts("usage: touch <path>");
        return;
    }
    if (normalize_path(cwd, arg, path) != 0 || split_parent_name(path, parent, name) != 0) {
        puts("touch: invalid path");
        return;
    }
    if (sys_fs_exists(path)) {
        if (sys_fs_write_file(path, "", 0) != 0) puts("touch: failed");
        return;
    }
    if (sys_fs_create_file(parent, name, "", 0) != 0) puts("touch: failed");
}

static void cmd_rm(const char* cwd, const char* arg) {
    char path[SHELL_PATH_MAX];
    if (!arg) {
        puts("usage: rm <path>");
        return;
    }
    if (normalize_path(cwd, arg, path) != 0) {
        puts("rm: invalid path");
        return;
    }
    if (sys_fs_remove(path) != 0) puts("rm: failed");
}

static void cmd_mv(const char* cwd, const char* old_arg, const char* new_arg) {
    char old_path[SHELL_PATH_MAX];
    char new_path[SHELL_PATH_MAX];
    if (!old_arg || !new_arg) {
        puts("usage: mv <old> <new>");
        return;
    }
    if (normalize_path(cwd, old_arg, old_path) != 0 || normalize_path(cwd, new_arg, new_path) != 0) {
        puts("mv: invalid path");
        return;
    }
    if (sys_fs_rename(old_path, new_path) != 0) puts("mv: failed");
}

static void cmd_cd(char* cwd, const char* arg) {
    char path[SHELL_PATH_MAX];
    ntux_dirent_t probe[1];
    uint64_t n = 0;
    if (!arg) arg = "/";
    if (normalize_path(cwd, arg, path) != 0) {
        puts("cd: invalid path");
        return;
    }
    long rc = sys_fs_list_dir(path, probe, 1, &n);
    if (rc != 0) {
        if (rc == -2) {
            puts("cd: not a directory");
            return;
        }
        sys_write("cd: failed rc=", 14);
        if (rc < 0) {
            putchar('-');
            print_u64((uint64_t)(-rc));
        } else {
            print_u64((uint64_t)rc);
        }
        putchar('\n');
        puts("cd: no such directory");
        return;
    }
    strcpy(cwd, path);
}

static void cmd_echo(int argc, char* argv[SHELL_ARGV_MAX]) {
    for (int i = 1; i < argc; ++i) {
        sys_write(argv[i], strlen(argv[i]));
        if (i + 1 < argc) putchar(' ');
    }
    putchar('\n');
}

static void cmd_exists(const char* cwd, const char* arg) {
    char path[SHELL_PATH_MAX];
    if (!arg) {
        puts("usage: exists <path>");
        return;
    }
    if (normalize_path(cwd, arg, path) != 0) {
        puts("exists: invalid path");
        return;
    }
    if (sys_fs_exists(path) > 0) puts("yes");
    else puts("no");
}

static void cmd_stat(const char* cwd, const char* arg) {
    char path[SHELL_PATH_MAX];
    uint64_t n = 0;
    if (!arg) {
        puts("usage: stat <path>");
        return;
    }
    if (normalize_path(cwd, arg, path) != 0) {
        puts("stat: invalid path");
        return;
    }

    long ex = sys_fs_exists(path);
    long lrc = sys_fs_list_dir(path, 0, 0, &n);

    sys_write("path: ", 6);
    puts(path);
    sys_write("exists: ", 8);
    puts(ex > 0 ? "yes" : "no");
    sys_write("list_rc: ", 9);
    if (lrc < 0) {
        putchar('-');
        print_u64((uint64_t)(-lrc));
    } else {
        print_u64((uint64_t)lrc);
    }
    putchar('\n');
    sys_write("dir_entries: ", 13);
    print_u64(n);
    putchar('\n');
}

static void cmd_version(void) {
    ui_color(UI_COLOR_PANEL);
    puts("NTux Shell v0.6 (NTux-OS style)");
    ui_reset();
    puts("NTux Kernel: int80 + VFS + PS/2 + tasking");
}

static void cmd_mouse(void) {
    ntux_mouse_state_t ms;
    if (sys_mouse_get_state(&ms) != 0) {
        puts("mouse: failed");
        return;
    }
    sys_write("x=", 2);
    print_i64(ms.x);
    sys_write(" y=", 3);
    print_i64(ms.y);
    sys_write(" scroll=", 8);
    print_i64(ms.scroll);
    sys_write(" buttons[LRM]=", 13);
    putchar(ms.left ? '1' : '0');
    putchar(ms.right ? '1' : '0');
    putchar(ms.middle ? '1' : '0');
    putchar('\n');
}

static void cmd_ping(const char* host) {
    if (!host || !host[0]) {
        puts("usage: ping <host>");
        return;
    }
    char out[512];
    long rc = sys_net_ping(host, out, sizeof(out));
    if (rc == 0) {
        puts(out);
        return;
    }
    sys_write("ping: failed rc=", 18);
    print_u64((uint64_t)(rc < 0 ? -rc : rc));
    putchar('\n');
}

static void cmd_netinfo(void) {
    char out[1024];
    long rc = sys_net_debug(out, sizeof(out));
    if (rc == 0) {
        puts(out);
        return;
    }
    sys_write("netinfo: failed rc=", 20);
    print_u64((uint64_t)(rc < 0 ? -rc : rc));
    putchar('\n');
}

static void cmd_dns(const char* ip_str) {
    if (!ip_str || !ip_str[0]) {
        puts("usage: dns <ip>");
        return;
    }
    uint32_t ip = 0;
    if (parse_ipv4(ip_str, &ip) != 0) {
        puts("dns: invalid ip");
        return;
    }
    long rc = sys_net_set_dns(ip);
    if (rc == 0) {
        puts("dns: ok");
        return;
    }
    sys_write("dns: failed rc=", 16);
    print_u64((uint64_t)(rc < 0 ? -rc : rc));
    putchar('\n');
}

static void cmd_http_get(const char* url) {
    if (!url || !url[0]) {
        puts("usage: httpget <url>");
        return;
    }
    char out[4096];
    long rc = sys_net_http_get(url, out, sizeof(out));
    if (rc == 0) {
        puts(out);
        return;
    }
    sys_write("httpget: failed rc=", 22);
    print_u64((uint64_t)(rc < 0 ? -rc : rc));
    putchar('\n');
}

static void cmd_time(void) {
    ntux_time_t now;
    if (sys_get_time(&now) != 0) {
        puts("time: failed");
        return;
    }
    print_two_digits(now.hour);
    putchar(':');
    print_two_digits(now.minute);
    putchar(':');
    print_two_digits(now.second);
    putchar('\n');
    print_two_digits(now.day);
    putchar('.');
    print_two_digits(now.month);
    putchar('.');
    print_u64(now.year);
    putchar('\n');
}

static void cmd_uptime(void) {
    uint64_t ticks = sys_get_ticks();
    uint64_t hz = sys_get_timer_hz();
    if (hz == 0) {
        puts("uptime: timer hz=0");
        return;
    }
    uint64_t secs = ticks / hz;
    sys_write("uptime: ", 9);
    print_u64(secs);
    sys_write("s (ticks=", 10);
    print_u64(ticks);
    sys_write(")\n", 2);
}

static void cmd_meminfo(void) {
    ntux_mem_info_t mi;
    if (sys_get_mem_info(&mi) != 0) {
        puts("meminfo: failed");
        return;
    }
    sys_write("mem total=", 10);
    print_u64(mi.total_bytes);
    sys_write(" bytes (", 8);
    print_u64(mi.total_bytes / (1024u * 1024u));
    sys_write(" MiB)\n", 7);
    sys_write("mem free=", 9);
    print_u64(mi.free_bytes);
    sys_write(" bytes (", 8);
    print_u64(mi.free_bytes / (1024u * 1024u));
    sys_write(" MiB)\n", 7);
}

static void cmd_cpuinfo(void) {
    ntux_cpu_info_t ci;
    char brand[64];
    if (sys_get_cpu_info(&ci) != 0) {
        puts("cpuinfo: failed");
        return;
    }
    brand[0] = '\0';
    (void)sys_get_cpu_brand(brand, sizeof(brand));
    sys_write("cpu hz=", 7);
    print_u64(ci.hz);
    sys_write(" ticks=", 7);
    print_u64(ci.ticks);
    sys_write(" idle=", 6);
    print_u64(ci.idle_ticks);
    putchar('\n');
    if (brand[0]) {
        sys_write("cpu brand=", 10);
        puts(brand);
    }
}

static void cmd_diskstats(void) {
    ntux_disk_stats_t ds;
    if (sys_get_disk_stats(&ds) != 0) {
        puts("diskstats: failed");
        return;
    }
    sys_write("disk read=", 10);
    print_u64(ds.read_bytes);
    sys_write(" bytes\n", 7);
    sys_write("disk write=", 11);
    print_u64(ds.write_bytes);
    sys_write(" bytes\n", 7);
}

static void cmd_gpuinfo(void) {
    ntux_gpu_info_t gi;
    ntux_gpu_stats_t gs;
    if (sys_gpu_get_info(&gi) != 0) {
        puts("gpuinfo: failed");
        return;
    }
    sys_write("gpu ", 4);
    puts(gi.name[0] ? gi.name : "?");
    sys_write("  res=", 6);
    print_u64(gi.width);
    putchar('x');
    print_u64(gi.height);
    sys_write(" bpp=", 5);
    print_u64(gi.bpp);
    sys_write(" fb=", 4);
    print_u64(gi.fb_size);
    sys_write(" bytes\n", 7);
    if (sys_gpu_get_stats(&gs) == 0) {
        sys_write("  blits=", 8);
        print_u64(gs.blit_count);
        sys_write(" errors=", 8);
        print_u64(gs.blit_errors);
        sys_write(" ticks=", 7);
        print_u64(gs.ticks);
        putchar('\n');
    }
}

static void cmd_modules(void) {
    ntux_module_info_t mods[32];
    uint64_t count = 0;
    if (sys_module_list(mods, 32, &count) != 0) {
        puts("modules: failed");
        return;
    }
    if (count > 32u) count = 32u;
    puts("modules:");
    for (uint64_t i = 0; i < count; ++i) {
        sys_write("  ", 2);
        sys_write(mods[i].token, strlen(mods[i].token));
        sys_write(" -> ", 4);
        puts(mods[i].path);
    }
}

static void cmd_task_list(void) {
    ntux_task_info_t tasks[32];
    uint64_t count = 0;
    if (sys_task_list(tasks, 32u, &count) != 0) {
        puts("task list: failed");
        return;
    }
    if (count > 32u) count = 32u;
    puts("id  name          state       core  affinity");
    for (uint64_t i = 0; i < count; ++i) {
        const char* st = "unknown";
        if (!tasks[i].active) continue;
        if (tasks[i].state == 0u) st = "running";
        else if (tasks[i].state == 1u) st = "ready";
        else if (tasks[i].state == 2u) st = "blocked";
        else if (tasks[i].state == 3u) st = "terminated";
        print_u64(tasks[i].id);
        sys_write("   ", 3);
        sys_write(tasks[i].name[0] ? tasks[i].name : "?", strlen(tasks[i].name[0] ? tasks[i].name : "?"));
        {
            size_t nlen = strlen(tasks[i].name[0] ? tasks[i].name : "?");
            if (nlen < 12u) {
                for (size_t p = nlen; p < 12u; ++p) putchar(' ');
            }
        }
        sys_write(st, strlen(st));
        if (strlen(st) < 10u) {
            for (size_t p = strlen(st); p < 10u; ++p) putchar(' ');
        }
        sys_write("  ", 2);
        print_i64((int64_t)tasks[i].running_core);
        sys_write("      ", 6);
        print_i64((int64_t)tasks[i].affinity_core);
        putchar('\n');
    }
}

static int cmd_setbg(const char* cwd, const char* arg) {
    char path[SHELL_PATH_MAX];
    if (!arg) {
        puts("usage: setbg <path.bmp> | setbg off");
        return -1;
    }
    if (strcmp(arg, "off") == 0) {
        free_bg();
        shell_clear_and_banner();
        puts("setbg: background disabled");
        return 0;
    }
    if (normalize_path(cwd, arg, path) != 0) {
        puts("setbg: invalid path");
        return -1;
    }
    if (load_bmp_for_background(path) != 0) {
        return -1;
    }
    shell_clear_and_banner();
    sys_write("setbg: loaded ", 14);
    puts(path);
    return 0;
}

void ntux_user_entry(void) {
    char line[SHELL_LINE_MAX];
    char* argv[SHELL_ARGV_MAX];
    int argc;
    char cwd[SHELL_PATH_MAX];

    strcpy(cwd, "/");
    shell_clear_and_banner();

    for (;;) {
        if (g_input_passthrough == INPUT_PASS_WAIT_CLAIM) {
            if (sys_console_is_free() != 0) {
                sys_yield();
                continue;
            }
            g_input_passthrough = INPUT_PASS_WAIT_RELEASE;
            continue;
        }
        if (g_input_passthrough == INPUT_PASS_WAIT_RELEASE) {
            if (sys_console_is_free() == 0) {
                sys_yield();
                continue;
            }
            g_input_passthrough = INPUT_PASS_NONE;
            continue;
        }
        if (sys_console_claim() != 0) {
            sys_yield();
            continue;
        }
        print_prompt(cwd);
        if (readline(line, sizeof(line)) == 0) continue;

        argc = split_args(line, argv);
        if (argc <= 0) continue;

        if (strcmp(argv[0], "help") == 0) {
            print_help();
            continue;
        }
        if (strcmp(argv[0], "banner") == 0) {
            print_banner();
            continue;
        }
        if (strcmp(argv[0], "version") == 0) {
            cmd_version();
            continue;
        }
        if (strcmp(argv[0], "pwd") == 0) {
            print_path_only(cwd);
            continue;
        }
        if (strcmp(argv[0], "cd") == 0) {
            cmd_cd(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "ls") == 0 || strcmp(argv[0], "ll") == 0) {
            cmd_ls(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "cat") == 0) {
            cmd_cat(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "mkdir") == 0) {
            cmd_mkdir(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "touch") == 0) {
            cmd_touch(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "rm") == 0) {
            cmd_rm(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "mv") == 0) {
            cmd_mv(cwd, argc > 1 ? argv[1] : 0, argc > 2 ? argv[2] : 0);
            continue;
        }
        if (strcmp(argv[0], "echo") == 0) {
            cmd_echo(argc, argv);
            continue;
        }
        if (strcmp(argv[0], "exists") == 0) {
            cmd_exists(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "stat") == 0) {
            cmd_stat(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "taskmgr") == 0) {
            long rc = sys_task_add_module("taskmgr");
            if (rc != 0) rc = sys_task_add("/boot/modules/taskmgr.elf");
            print_task_result("taskmgr", rc);
            if (rc >= 0) {
                (void)sys_yield();
                (void)sys_yield();
                wait_task_exit((int)rc);
            }
            continue;
        }
        if (strcmp(argv[0], "tetris") == 0) {
            long rc = sys_task_add_module("tetris");
            if (rc != 0) rc = sys_task_add("/boot/modules/tetris.elf");
            print_task_result("tetris", rc);
            if (rc >= 0) {
                (void)sys_yield();
                (void)sys_yield();
                wait_task_exit((int)rc);
            }
            continue;
        }
        if (strcmp(argv[0], "deskdemo") == 0) {
            long rc = sys_task_add_module("deskdemo");
            if (rc != 0) rc = sys_task_add("/boot/modules/deskdemo.elf");
            print_task_result("deskdemo", rc);
            if (rc >= 0) {
                (void)sys_yield();
                (void)sys_yield();
                wait_task_exit((int)rc);
            }
            continue;
        }
        if (strcmp(argv[0], "editor") == 0) {
            long rc = sys_task_add_module("editor");
            if (rc != 0) rc = sys_task_add("/boot/modules/editor.elf");
            print_task_result("editor", rc);
            if (rc >= 0) {
                (void)sys_yield();
                (void)sys_yield();
                wait_task_exit((int)rc);
            }
            continue;
        }
        if (strcmp(argv[0], "tcc") == 0) {
            const char *tcc_path = "/boot/modules/tcc.elf";
            if (sys_fs_exists(tcc_path) <= 0 && sys_fs_exists("/boot/boot/modules/tcc.elf") > 0) {
                tcc_path = "/boot/boot/modules/tcc.elf";
            }
            (void)shell_launch_task(cwd, tcc_path, "tcc", argv, 1, argc);
            continue;
        }
        if (strcmp(argv[0], "run") == 0 && argc >= 2) {
            (void)shell_launch_task(cwd, argv[1], "run", argv, 2, argc);
            continue;
        }
        if (strcmp(argv[0], "task") == 0 && argc >= 3 && strcmp(argv[1], "add") == 0) {
            (void)shell_launch_task(cwd, argv[2], "task add", argv, 3, argc);
            continue;
        }
        if (strcmp(argv[0], "task") == 0 && argc >= 2 && strcmp(argv[1], "list") == 0) {
            cmd_task_list();
            continue;
        }
        if (strcmp(argv[0], "clear") == 0) {
            shell_clear_and_banner();
            continue;
        }
        if (strcmp(argv[0], "ticks") == 0) {
            sys_write("ticks=", 6);
            print_u64(sys_get_ticks());
            putchar('\n');
            continue;
        }
        if (strcmp(argv[0], "sleep") == 0) {
            uint64_t t = 0;
            if (argc < 2 || parse_u64(argv[1], &t) != 0) {
                puts("usage: sleep <ticks>");
                continue;
            }
            sys_wait_ticks(t);
            continue;
        }
        if (strcmp(argv[0], "mouse") == 0) {
            cmd_mouse();
            continue;
        }
        if (strcmp(argv[0], "ping") == 0) {
            cmd_ping(argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "netinfo") == 0 || strcmp(argv[0], "netstat") == 0) {
            cmd_netinfo();
            continue;
        }
        if (strcmp(argv[0], "dns") == 0) {
            cmd_dns(argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "httpget") == 0) {
            cmd_http_get(argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "time") == 0) {
            cmd_time();
            continue;
        }
        if (strcmp(argv[0], "uptime") == 0) {
            cmd_uptime();
            continue;
        }
        if (strcmp(argv[0], "meminfo") == 0) {
            cmd_meminfo();
            continue;
        }
        if (strcmp(argv[0], "cpuinfo") == 0) {
            cmd_cpuinfo();
            continue;
        }
        if (strcmp(argv[0], "diskstats") == 0) {
            cmd_diskstats();
            continue;
        }
        if (strcmp(argv[0], "gpuinfo") == 0) {
            cmd_gpuinfo();
            continue;
        }
        if (strcmp(argv[0], "modules") == 0) {
            cmd_modules();
            continue;
        }
        if (strcmp(argv[0], "setbg") == 0) {
            (void)cmd_setbg(cwd, argc > 1 ? argv[1] : 0);
            continue;
        }
        if (strcmp(argv[0], "reboot") == 0) {
            puts("rebooting...");
            (void)sys_reboot();
            continue;
        }
        if (strcmp(argv[0], "shutdown") == 0 || strcmp(argv[0], "poweroff") == 0) {
            puts("shutting down...");
            (void)sys_shutdown();
            continue;
        }
        if (strcmp(argv[0], "exit") == 0) {
            puts("bye");
            sys_exit(0);
        }

        print_unknown(argv[0]);
    }
}








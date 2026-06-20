#include <syscall.h>
#include <window.h>
#include <stdio.h>
#include <stdarg.h>

/* ----- DEBUG helpers ----- */
static uint64_t g_dbgt0 = 0;
static void dbg_init(void) {
    g_dbgt0 = sys_get_ticks();
}
static uint64_t dbg_tick(void) {
    return sys_get_ticks() - g_dbgt0;
}
static void dbg(const char* fmt, ...) {
    uint64_t t = dbg_tick();
    printf("[TCC+%llu] ", (unsigned long long)t);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}
/* ------------------------ */

#ifndef ONE_SOURCE
# define ONE_SOURCE 1
#endif
#include "tcc.h"
#if ONE_SOURCE
# include "libtcc.c"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <args.h>
#include <stdarg.h>

char **environ = 0;

static char *strtok(char *s, const char *delim) {
    static char *last;
    if (s == NULL) s = last;
    if (s == NULL) return NULL;
    
    // Skip leading delimiters
    while (*s && strchr(delim, *s)) s++;
    if (*s == '\0') {
        last = NULL;
        return NULL;
    }
    
    char *tok = s;
    // Find end of token
    while (*s && !strchr(delim, *s)) s++;
    
    if (*s == '\0') {
        last = NULL;
    } else {
        *s = '\0';
        last = s + 1;
    }
    return tok;
}

#define CONSOLE_W 100
#define CONSOLE_H 30
#define CHAR_W 8
#define CHAR_H 16

static char g_console[CONSOLE_H][CONSOLE_W];
static uint32_t g_colors[CONSOLE_H][CONSOLE_W];
static int g_cur_x = 0;
static int g_cur_y = 0;
static window_t g_win_id = 0x5443434445534Bull; // "TCCDESK"
static char g_cwd[256];
static char g_input_buf[256];
static int g_input_len = 0;
static uint32_t g_current_color = 0xFFFFFF;

static void console_scroll() {
    for (int y = 0; y < CONSOLE_H - 1; ++y) {
        memcpy(g_console[y], g_console[y+1], CONSOLE_W);
        memcpy(g_colors[y], g_colors[y+1], CONSOLE_W * sizeof(uint32_t));
    }
    memset(g_console[CONSOLE_H - 1], ' ', CONSOLE_W);
    for (int x = 0; x < CONSOLE_W; ++x) g_colors[CONSOLE_H - 1][x] = 0xFFFFFF;
    g_cur_y = CONSOLE_H - 1;
}

static void console_putc(char c) {
    if (c == '\n') {
        g_cur_x = 0;
        g_cur_y++;
        if (g_cur_y >= CONSOLE_H) console_scroll();
    } else if (c == '\r') {
        g_cur_x = 0;
    } else if (c == '\b') {
        if (g_cur_x > 0) g_cur_x--;
    } else if (c == '\t') {
        g_cur_x = (g_cur_x + 8) & ~7;
        if (g_cur_x >= CONSOLE_W) console_putc('\n');
    } else {
        if (g_cur_x >= CONSOLE_W) console_putc('\n');
        g_console[g_cur_y][g_cur_x] = c;
        g_colors[g_cur_y][g_cur_x] = g_current_color;
        g_cur_x++;
    }
}

static void console_puts(const char* s) {
    while (*s) console_putc(*s++);
}

static void console_printf(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    console_puts(buf);
}

static void redraw() {
    window_clear(g_win_id, 0x1E1E1E);
    for (int y = 0; y < CONSOLE_H; ++y) {
        char line[CONSOLE_W + 1];
        int line_len = 0;
        uint32_t current_seg_color = 0;
        int seg_start_x = -1;
        
        for (int x = 0; x < CONSOLE_W; ++x) {
            char c = g_console[y][x];
            uint32_t color = g_colors[y][x];
            
            if (c == 0 || c == ' ') {
                if (seg_start_x != -1) {
                    line[line_len] = 0;
                    window_draw_text(g_win_id, seg_start_x * CHAR_W + 4, y * CHAR_H + 4, current_seg_color, line);
                    seg_start_x = -1;
                    line_len = 0;
                }
                continue;
            }
            
            if (seg_start_x != -1 && color != current_seg_color) {
                line[line_len] = 0;
                window_draw_text(g_win_id, seg_start_x * CHAR_W + 4, y * CHAR_H + 4, current_seg_color, line);
                seg_start_x = -1;
                line_len = 0;
            }
            
            if (seg_start_x == -1) {
                seg_start_x = x;
                current_seg_color = color;
            }
            line[line_len++] = c;
        }
        if (seg_start_x != -1) {
            line[line_len] = 0;
            window_draw_text(g_win_id, seg_start_x * CHAR_W + 4, y * CHAR_H + 4, current_seg_color, line);
        }
    }
    // Draw cursor
    window_draw_rect(g_win_id, g_cur_x * CHAR_W + 4, g_cur_y * CHAR_H + 4, 8, 2, 0x00FF00, 1);
    window_present(g_win_id);
}

static void tcc_error_func(void *opaque, const char *msg) {
    (void)opaque;
    g_current_color = 0xFF5555; // Red for errors
    console_printf("TCC: %s\n", msg);
    g_current_color = 0xFFFFFF;
    redraw();
}

static void print_progress(int percent, const char* stage) {
    char bar[22];
    int n = percent * 20 / 100;
    for (int i = 0; i < 20; i++)
        bar[i] = (i < n) ? '#' : (i == n && n < 20) ? '>' : ' ';
    bar[20] = 0;
    printf("[TCC] [%s] %d%% - %s\n", bar, percent, stage);
    fflush(stdout);
}

static void tcc_error_func_headless(void *opaque, const char *msg) {
    (void)opaque;
    printf("[TCC] Error: %s\n", msg);
}

static int resolve_path(const char* cwd, const char* in, char* out, size_t cap) {
    if (!cwd || !in || !out || cap < 2) return -1;
    char temp[256];
    if (in[0] == '/') {
        strncpy(temp, in, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = 0;
    } else {
        size_t clen = strlen(cwd);
        if (clen == 0 || cwd[0] != '/' || clen >= sizeof(temp) - 2) return -1;
        memcpy(temp, cwd, clen);
        size_t tlen = clen;
        if (tlen > 1 && temp[tlen - 1] == '/') tlen--;
        temp[tlen++] = '/';
        strncpy(temp + tlen, in, sizeof(temp) - tlen - 1);
        temp[sizeof(temp) - 1] = 0;
    }
    out[0] = '/';
    out[1] = 0;
    char* src = temp;
    while (*src) {
        while (*src == '/') src++;
        if (!*src) break;
        char seg[64];
        size_t sl = 0;
        while (src[sl] && src[sl] != '/' && sl + 1 < sizeof(seg)) {
            seg[sl] = src[sl];
            sl++;
        }
        seg[sl] = 0;
        src += sl;
        if (strcmp(seg, ".") == 0) continue;
        if (strcmp(seg, "..") == 0) {
            size_t ol = strlen(out);
            if (ol > 1) {
                size_t j = ol - 1;
                while (j > 0 && out[j] != '/') j--;
                out[j == 0 ? 1 : j] = 0;
            }
            continue;
        }
        size_t ol = strlen(out);
        if (ol > 1) {
            if (ol + 1 >= cap) return -1;
            out[ol++] = '/';
            out[ol] = 0;
        }
        if (ol + sl >= cap) return -1;
        memcpy(out + ol, seg, sl + 1);
    }
    return 0;
}

static void read_cwd(char* buf, size_t cap) {
    buf[0] = 0;
    uint64_t len = 0;
    sys_fs_read_file("/tmp/run.cwd", buf, cap, &len);
    if (len > 0 && len < cap && buf[len - 1] == '\n') buf[len - 1] = 0;
    if (buf[0] != '/') strcpy(buf, "/");
}

static int compile_headless(const char* src, const char* out_manual) {
    dbg_init();

    dbg("=== compile_headless START src=\"%s\" out_manual=\"%s\" ===", src, out_manual ? out_manual : "(null)");

    char cwd[256];
    read_cwd(cwd, sizeof(cwd));
    dbg("CWD read: \"%s\"", cwd);

    char src_path[256];
    if (resolve_path(cwd, src, src_path, sizeof(src_path)) != 0) {
        printf("[TCC] Invalid source path: %s\n", src);
        return -1;
    }
    dbg("src_path resolved: \"%s\"", src_path);

    char out_name[256];
    if (out_manual && out_manual[0]) {
        if (resolve_path(cwd, out_manual, out_name, sizeof(out_name)) != 0) {
            printf("[TCC] Invalid output path: %s\n", out_manual);
            return -1;
        }
        dbg("out_name (manual): \"%s\"", out_name);
    } else {
        strncpy(out_name, src_path, sizeof(out_name) - 5);
        char *dot = strrchr(out_name, '.');
        if (dot) *dot = 0;
        strcat(out_name, ".elf");
        dbg("out_name (auto): \"%s\"", out_name);
    }

    printf("[TCC] Compiling %s -> %s\n", src_path, out_name);

    print_progress(0, "Initializing");

    dbg("Calling tcc_new()...");
    TCCState *s = tcc_new();
    if (!s) {
        printf("[TCC] Failed to create state\n");
        return -1;
    }
    dbg("tcc_new() OK, state=%p", (void*)s);

    tcc_set_error_func(s, NULL, tcc_error_func_headless);
    dbg("error_func set");

    print_progress(10, "Setting include paths");
    dbg("tcc_add_include_path(/iso0/boot/tcc/include) ...");
    tcc_add_include_path(s, "/iso0/boot/tcc/include");
    dbg("  OK");
    dbg("tcc_add_include_path(/boot/tcc/include) ...");
    tcc_add_include_path(s, "/boot/tcc/include");
    dbg("  OK");
    dbg("tcc_add_include_path(/fat0/boot/tcc/include) ...");
    tcc_add_include_path(s, "/fat0/boot/tcc/include");
    dbg("  OK");

    print_progress(15, "Setting library paths");
    dbg("tcc_add_library_path(/iso0/boot/tcc/lib) ...");
    tcc_add_library_path(s, "/iso0/boot/tcc/lib");
    dbg("  OK");
    dbg("tcc_add_library_path(/boot/tcc/lib) ...");
    tcc_add_library_path(s, "/boot/tcc/lib");
    dbg("  OK");
    dbg("tcc_add_library_path(/fat0/boot/tcc/lib) ...");
    tcc_add_library_path(s, "/fat0/boot/tcc/lib");
    dbg("  OK");

    print_progress(20, "Configuring output type");
    dbg("Set s->nostdlib = 1, calling tcc_set_output_type(TCC_OUTPUT_EXE)...");
    sys_yield();
    s->nostdlib = 1;
    dbg("  calling tcc_set_output_type...");
    tcc_set_output_type(s, TCC_OUTPUT_EXE);
    dbg("  tcc_set_output_type OK");

    print_progress(25, "Loading source");
    dbg("=== tcc_add_file(src_path=\"%s\") CALL ===", src_path);
    sys_yield();
    int add_ret = tcc_add_file(s, src_path);
    dbg("=== tcc_add_file RETURNED ret=%d ===", add_ret);
    if (add_ret < 0) {
        printf("[TCC] Could not add file: %s\n", src_path);
        tcc_delete(s);
        return -1;
    }

    print_progress(40, "Configuring libraries");
    dbg("tcc_add_library(s, \"c\") ...");
    tcc_add_library(s, "c");
    dbg("  OK");

    print_progress(55, "Compiling");
    dbg("tcc_output_file(out_name=\"%s\") ...", out_name);
    int out_ret = tcc_output_file(s, out_name);
    dbg("tcc_output_file returned %d", out_ret);
    if (out_ret < 0) {
        printf("[TCC] Compilation FAILED\n");
        tcc_delete(s);
        return -1;
    }

    print_progress(100, "Done");
    dbg("tcc_delete() ...");
    tcc_delete(s);
    dbg("=== compile_headless DONE ===");
    return 0;
}

static void do_compile(const char* filename, const char* out_manual) {
    TCCState *s = tcc_new();
    if (!s) {
        console_puts("TCC: failed to create state\n");
        return;
    }
    tcc_set_error_func(s, NULL, tcc_error_func);
    
    // Add include/lib paths - iso first (where headers live), then boot, then fat
    tcc_add_include_path(s, "/iso0/boot/tcc/include");
    tcc_add_include_path(s, "/boot/tcc/include");
    tcc_add_include_path(s, "/fat0/boot/tcc/include");
    tcc_add_library_path(s, "/iso0/boot/tcc/lib");
    tcc_add_library_path(s, "/boot/tcc/lib");
    tcc_add_library_path(s, "/fat0/boot/tcc/lib");
    
    tcc_set_output_type(s, TCC_OUTPUT_EXE);
    
    if (tcc_add_file(s, filename) < 0) {
        console_printf("TCC: could not add file %s\n", filename);
        tcc_delete(s);
        return;
    }

    // Link with our libc to get Desktop API and other symbols
    tcc_add_library(s, "c");
    
    char out_name[256];
    if (out_manual && out_manual[0]) {
        strncpy(out_name, out_manual, sizeof(out_name));
    } else {
        strncpy(out_name, filename, sizeof(out_name)-5);
        char *dot = strrchr(out_name, '.');
        if (dot) *dot = 0;
        strcat(out_name, ".elf");
    }
    
    console_printf("TCC: compiling %s -> %s ...\n", filename, out_name);
    redraw();
    
    if (tcc_output_file(s, out_name) < 0) {
        console_puts("TCC: compilation failed\n");
    } else {
        console_puts("TCC: compilation successful\n");
    }
    
    tcc_delete(s);
    redraw();
}

static void do_ls() {
    ntux_dirent_t entries[128];
    uint64_t count = 0;
    if (sys_fs_list_dir(g_cwd, entries, 128, &count) != 0) {
        console_puts("ls: failed\n");
        return;
    }
    for (uint64_t i = 0; i < count; ++i) {
        if (entries[i].is_dir) g_current_color = 0x5555FF; // Blue for dirs
        else g_current_color = 0xFFFFFF;
        console_printf("%-16s  ", entries[i].name);
        if ((i + 1) % 4 == 0) console_putc('\n');
    }
    if (count % 4 != 0) console_putc('\n');
    g_current_color = 0xFFFFFF;
}

static void handle_command(char* cmd) {
    char* argv[16];
    int argc = 0;
    char* p = strtok(cmd, " ");
    while (p && argc < 16) {
        argv[argc++] = p;
        p = strtok(NULL, " ");
    }
    
    if (argc == 0) return;
    
    if (strcmp(argv[0], "ls") == 0) {
        do_ls();
    } else if (strcmp(argv[0], "cd") == 0) {
        if (argc < 2) {
            console_puts("usage: cd <dir>\n");
        } else {
            // Simple path handling
            if (argv[1][0] == '/') {
                strncpy(g_cwd, argv[1], sizeof(g_cwd));
            } else {
                if (g_cwd[strlen(g_cwd)-1] != '/') strcat(g_cwd, "/");
                strcat(g_cwd, argv[1]);
            }
        }
    } else if (strcmp(argv[0], "compile") == 0) {
        if (argc < 2) {
            console_puts("usage: compile <file.c> [outfile.elf]\n");
        } else {
            do_compile(argv[1], argc > 2 ? argv[2] : NULL);
        }
    } else if (strcmp(argv[0], "clear") == 0) {
        memset(g_console, ' ', sizeof(g_console));
        g_cur_x = g_cur_y = 0;
    } else if (strcmp(argv[0], "help") == 0) {
        console_puts("Commands: ls, cd, compile <src.c> [out.elf], clear, help\n");
    } else {
        console_printf("Unknown command: %s\n", argv[0]);
    }
}

void ntux_user_entry(void) {
    // Check for headless compilation mode: run tcc.elf <source.c> [output.elf]
    int ac = ntux_argc();
    if (ac >= 2) {
        int ret = compile_headless(ntux_arg(1), (ac >= 3) ? ntux_arg(2) : NULL);
        sys_exit(ret);
    }

    if (window_init() != 0) sys_exit(1);
    
    if (window_create(g_win_id, 100, 100, 800, 480, 0x1E1E1E, "TCC Desktop Console") != 0) {
        sys_exit(1);
    }
    
    strcpy(g_cwd, "/");
    memset(g_console, ' ', sizeof(g_console));
    for (int y = 0; y < CONSOLE_H; ++y) {
        for (int x = 0; x < CONSOLE_W; ++x) g_colors[y][x] = 0xFFFFFF;
    }
    
    console_puts("TCC Desktop Console v1.0\n");
    console_printf("%s> ", g_cwd);
    redraw();
    
    for (;;) {
        if (window_should_close(g_win_id)) break;

        long ch = sys_getchar();
        if (ch > 0) {
            if (ch == '\n' || ch == '\r') {
                console_putc('\n');
                g_input_buf[g_input_len] = 0;
                handle_command(g_input_buf);
                g_input_len = 0;
                console_printf("%s> ", g_cwd);
            } else if (ch == '\b' || ch == 127) {
                if (g_input_len > 0) {
                    g_input_len--;
                    console_putc('\b');
                    console_putc(' ');
                    console_putc('\b');
                }
            } else if (ch >= 32 && ch < 127) {
                if (g_input_len < (int)sizeof(g_input_buf) - 1) {
                    g_input_buf[g_input_len++] = (char)ch;
                    console_putc((char)ch);
                }
            }
            redraw();
        }
        
        window_input_state_t st;
        if (window_get_input_state(g_win_id, &st) == 0) {
            if (st.close_requested) break;
        }
        
        sys_wait_ticks(1);
    }
}

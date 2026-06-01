#include <syscall.h>
#include <window.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define CONSOLE_W 100
#define CONSOLE_H 30
#define CHAR_W 8
#define CHAR_H 16

static char g_console[CONSOLE_H][CONSOLE_W];
static uint32_t g_colors[CONSOLE_H][CONSOLE_W];
static int g_cur_x = 0;
static int g_cur_y = 0;
static window_t g_win_id = 0x4C55414445534Bull; // "LUADESK"
static char g_cwd[256];
static char g_input_buf[256];
static int g_input_len = 0;
static uint32_t g_current_color = 0xFFFFFF;
static int g_dialog_pending = 0;

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
    window_clear(g_win_id, 0x0C121B);
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
    window_draw_rect(g_win_id, g_cur_x * CHAR_W + 4, g_cur_y * CHAR_H + 4, 8, 2, 0x56E6AA, 1);
    window_present(g_win_id);
}

char **environ = 0;

static char *strtok(char *s, const char *delim) {
    static char *last;
    if (s == NULL) s = last;
    if (s == NULL) return NULL;
    while (*s && strchr(delim, *s)) s++;
    if (*s == '\0') {
        last = NULL;
        return NULL;
    }
    char *tok = s;
    while (*s && !strchr(delim, *s)) s++;
    if (*s == '\0') {
        last = NULL;
    } else {
        *s = '\0';
        last = s + 1;
    }
    return tok;
}

static int lua_print(lua_State *L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        const char *s = luaL_tolstring(L, i, NULL);
        if (i > 1) console_putc('\t');
        console_puts(s);
        lua_pop(L, 1);
    }
    console_putc('\n');
    redraw();
    return 0;
}

static void do_lua_run(const char* filename) {
    lua_State *L = luaL_newstate();
    if (!L) {
        console_puts("Lua: failed to create state\n");
        return;
    }
    luaL_openlibs(L);
    
    // Override print
    lua_pushcfunction(L, lua_print);
    lua_setglobal(L, "print");
    
    // Detect base path (some environments have an extra /boot level)
    const char* boot_base = "/boot";
    if (sys_fs_exists("/boot/boot/include") == 1) boot_base = "/boot/boot";

    console_printf("Lua: running %s ...\n", filename);
    redraw();
    
    if (luaL_dofile(L, filename) != LUA_OK) {
        g_current_color = 0xFF5555;
        console_printf("Lua Error: %s\n", lua_tostring(L, -1));
        g_current_color = 0xFFFFFF;
    } else {
        console_puts("Lua: execution finished\n");
    }
    
    lua_close(L);
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
        if (entries[i].is_dir) g_current_color = 0x5BC0FF;
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
            if (argv[1][0] == '/') {
                strncpy(g_cwd, argv[1], sizeof(g_cwd));
            } else {
                if (g_cwd[strlen(g_cwd)-1] != '/') strcat(g_cwd, "/");
                strcat(g_cwd, argv[1]);
            }
        }
    } else if (strcmp(argv[0], "run") == 0) {
        if (argc < 2) {
            console_puts("usage: run <file.lua>\n");
        } else {
            do_lua_run(argv[1]);
        }
    } else if (strcmp(argv[0], "pick") == 0) {
        g_dialog_pending = 1;
        window_open_file_picker("Select Lua Script", g_cwd, 0);
    } else if (strcmp(argv[0], "clear") == 0) {
        memset(g_console, ' ', sizeof(g_console));
        g_cur_x = g_cur_y = 0;
    } else if (strcmp(argv[0], "help") == 0) {
        console_puts("Commands: ls, cd, run, pick, clear, help\n");
    } else {
        console_printf("Unknown command: %s\n", argv[0]);
    }
}

void ntux_user_entry(void) {
    if (window_init() != 0) sys_exit(1);
    
    if (window_create(g_win_id, 150, 150, 800, 480, 0x0C121B, "Lua Desktop Console") != 0) {
        sys_exit(1);
    }
    
    strcpy(g_cwd, "/");
    memset(g_console, ' ', sizeof(g_console));
    for (int y = 0; y < CONSOLE_H; ++y) {
        for (int x = 0; x < CONSOLE_W; ++x) g_colors[y][x] = 0xFFFFFF;
    }
    
    console_puts("Lua Desktop Console v1.0\n");
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
                if (g_input_len < sizeof(g_input_buf) - 1) {
                    g_input_buf[g_input_len++] = (char)ch;
                    console_putc((char)ch);
                }
            }
            redraw();
        }
        
        if (g_dialog_pending) {
            char path[256];
            uint32_t code = 0;
            if (window_dialog_pop(path, sizeof(path), &code) == 0) {
                if (code == 1) {
                    do_lua_run(path);
                    console_printf("%s> ", g_cwd);
                }
                g_dialog_pending = 0;
                redraw();
            }
        }

        window_input_state_t st;
        if (window_get_input_state(g_win_id, &st) == 0) {
            if (st.close_requested) break;
        }
        
        sys_wait_ticks(1);
    }
    sys_exit(0);
}

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <window.h>
#include <window_protocol.h>
#include <args.h>

#define EDIT_MAX_LINES 256
#define EDIT_LINE_LEN 256

static char g_lines[EDIT_MAX_LINES][EDIT_LINE_LEN];
static int g_line_count = 1;
static int g_cur_line = 0;
static int g_cur_col = 0;
static int g_scroll = 0;
static char g_current_path[256] = "";
static int g_dialog_pending = 0; /* 1=open, 2=save */
static char g_status[64] = "";

static uint8_t g_key_last[128];
static uint64_t g_last_key_tick = 0;

static int key_edge(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static void ensure_line_bounds(void) {
    if (g_cur_line < 0) g_cur_line = 0;
    if (g_cur_line >= g_line_count) g_cur_line = g_line_count - 1;
    if (g_cur_col < 0) g_cur_col = 0;
    int len = (int)strlen(g_lines[g_cur_line]);
    if (g_cur_col > len) g_cur_col = len;
}

static void insert_char(char c) {
    if (g_cur_line < 0 || g_cur_line >= EDIT_MAX_LINES) return;
    char* line = g_lines[g_cur_line];
    int len = (int)strlen(line);
    if (len + 1 >= EDIT_LINE_LEN) return;
    if (g_cur_col < 0) g_cur_col = 0;
    if (g_cur_col > len) g_cur_col = len;
    for (int i = len; i >= g_cur_col; --i) {
        line[i + 1] = line[i];
    }
    line[g_cur_col] = c;
    g_cur_col++;
}

static void backspace_char(void) {
    if (g_cur_line < 0 || g_cur_line >= g_line_count) return;
    if (g_cur_col > 0) {
        char* line = g_lines[g_cur_line];
        int len = (int)strlen(line);
        for (int i = g_cur_col - 1; i < len; ++i) {
            line[i] = line[i + 1];
        }
        g_cur_col--;
        return;
    }
    if (g_cur_line > 0) {
        int prev = g_cur_line - 1;
        int prev_len = (int)strlen(g_lines[prev]);
        int cur_len = (int)strlen(g_lines[g_cur_line]);
        if (prev_len + cur_len < EDIT_LINE_LEN) {
            strncat(g_lines[prev], g_lines[g_cur_line], EDIT_LINE_LEN - prev_len - 1);
            for (int i = g_cur_line; i < g_line_count - 1; ++i) {
                memcpy(g_lines[i], g_lines[i + 1], EDIT_LINE_LEN);
            }
            g_line_count--;
            g_cur_line = prev;
            g_cur_col = prev_len;
        }
    }
}

static void newline_char(void) {
    if (g_line_count >= EDIT_MAX_LINES) return;
    char* line = g_lines[g_cur_line];
    int len = (int)strlen(line);
    if (g_cur_col > len) g_cur_col = len;

    for (int i = g_line_count; i > g_cur_line + 1; --i) {
        memcpy(g_lines[i], g_lines[i - 1], EDIT_LINE_LEN);
    }
    g_lines[g_cur_line + 1][0] = '\0';
    if (g_cur_col < len) {
        strncpy(g_lines[g_cur_line + 1], line + g_cur_col, EDIT_LINE_LEN - 1);
        g_lines[g_cur_line + 1][EDIT_LINE_LEN - 1] = '\0';
        line[g_cur_col] = '\0';
    }
    g_line_count++;
    g_cur_line++;
    g_cur_col = 0;
}

static void move_left(void) {
    if (g_cur_col > 0) g_cur_col--;
    else if (g_cur_line > 0) {
        g_cur_line--;
        g_cur_col = (int)strlen(g_lines[g_cur_line]);
    }
}

static void move_right(void) {
    int len = (int)strlen(g_lines[g_cur_line]);
    if (g_cur_col < len) g_cur_col++;
    else if (g_cur_line + 1 < g_line_count) {
        g_cur_line++;
        g_cur_col = 0;
    }
}

static void move_up(void) {
    if (g_cur_line > 0) g_cur_line--;
    ensure_line_bounds();
}

static void move_down(void) {
    if (g_cur_line + 1 < g_line_count) g_cur_line++;
    ensure_line_bounds();
}

static int split_parent_name(const char* full, char* parent, char* name, size_t cap) {
    if (!full || full[0] != '/' || !parent || !name || cap < 4) return -1;
    const char* slash = NULL;
    for (const char* p = full; *p; ++p) {
        if (*p == '/') slash = p;
    }
    if (!slash || !slash[1]) return -1;
    size_t plen = (size_t)(slash - full);
    size_t nlen = strlen(slash + 1);
    if (plen + 1 > cap || nlen + 1 > cap) return -1;
    if (plen == 0) {
        parent[0] = '/'; parent[1] = '\0';
    } else {
        memcpy(parent, full, plen);
        parent[plen] = '\0';
    }
    memcpy(name, slash + 1, nlen + 1);
    return 0;
}

static int load_file(const char* path) {
    if (!path || !path[0]) return -1;
    uint64_t len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0) return -1;
    if (len > 65535u) len = 65535u;
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) return -1;
    if (sys_fs_read_file(path, buf, len, &len) != 0) {
        free(buf);
        return -1;
    }
    buf[len] = '\0';

    for (int i = 0; i < EDIT_MAX_LINES; ++i) g_lines[i][0] = '\0';
    g_line_count = 1;
    const char* arg_path = ntux_arg(0);
    if (arg_path && arg_path[0]) {
        (void)load_file(arg_path);
    }
    g_cur_line = 0;
    g_cur_col = 0;

    int line = 0;
    size_t pos = 0;
    while (pos < len && line < EDIT_MAX_LINES) {
        size_t start = pos;
        while (pos < len && buf[pos] != '\n' && buf[pos] != '\r') pos++;
        size_t l = pos - start;
        if (l >= EDIT_LINE_LEN) l = EDIT_LINE_LEN - 1;
        memcpy(g_lines[line], buf + start, l);
        g_lines[line][l] = '\0';
        line++;
        if (pos < len && buf[pos] == '\r') pos++;
        if (pos < len && buf[pos] == '\n') pos++;
    }
    if (line == 0) line = 1;
    g_line_count = line;
    free(buf);
    strncpy(g_current_path, path, sizeof(g_current_path) - 1);
    g_current_path[sizeof(g_current_path) - 1] = '\0';
    strncpy(g_status, "Opened", sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
    return 0;
}

static int save_file(const char* path) {
    if (!path || !path[0]) return -1;
    size_t cap = 65535u;
    char* buf = (char*)malloc(cap);
    if (!buf) return -1;
    size_t used = 0;
    for (int i = 0; i < g_line_count; ++i) {
        size_t l = strlen(g_lines[i]);
        if (used + l + 1 >= cap) break;
        memcpy(buf + used, g_lines[i], l);
        used += l;
        if (i + 1 < g_line_count) buf[used++] = '\n';
    }
    if (used == 0 || buf[used - 1] != '\n') {
        if (used + 1 < cap) buf[used++] = '\n';
    }
    long rc = sys_fs_write_file(path, buf, (uint64_t)used);
    if (rc != 0) {
        char parent[256];
        char name[256];
        if (split_parent_name(path, parent, name, sizeof(parent)) == 0) {
            (void)sys_fs_create_file(parent, name, buf, (uint64_t)used);
        }
    }
    free(buf);
    strncpy(g_current_path, path, sizeof(g_current_path) - 1);
    g_current_path[sizeof(g_current_path) - 1] = '\0';
    strncpy(g_status, (rc == 0) ? "Saved" : "Save failed", sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
    return 0;
}

static void draw_editor(window_t id, int w, int h) {
    const uint32_t bg = 0xFF0C121Bu;
    const uint32_t text = 0xFFE6F0FFu;
    const uint32_t dim = 0xFF8EA6C8u;
    const uint32_t accent = 0xFF56E6AAu;
    int line_h = 10;
    int top = 8;
    int bottom = 26;
    int rows = (h - top - bottom) / line_h;
    if (rows < 1) rows = 1;

    window_clear(id, bg);

    char header[256];
    snprintf(header, sizeof(header), "NTux Editor  |  Ctrl+O Open  Ctrl+S Save  Esc Exit  |  %s",
             g_current_path[0] ? g_current_path : "(untitled)");
    if (g_status[0]) {
        size_t len = strlen(header);
        if (len + 3 < sizeof(header)) {
            strncat(header, " | ", sizeof(header) - len - 1);
            strncat(header, g_status, sizeof(header) - strlen(header) - 1);
        }
    }
    window_draw_text(id, 8, 6, dim, header);

    if (g_cur_line < g_scroll) g_scroll = g_cur_line;
    if (g_cur_line >= g_scroll + rows) g_scroll = g_cur_line - rows + 1;

    for (int i = 0; i < rows; ++i) {
        int idx = g_scroll + i;
        if (idx >= g_line_count) break;
        window_draw_text(id, 8, top + i * line_h, text, g_lines[idx]);
        if (idx == g_cur_line) {
            int cx = 8 + g_cur_col * 8;
            int cy = top + i * line_h + 9;
            window_draw_line(id, cx, cy, cx + 6, cy, accent);
        }
    }

    char status[128];
    snprintf(status, sizeof(status), "Ln %d, Col %d  |  Lines %d",
             g_cur_line + 1, g_cur_col + 1, g_line_count);
    window_draw_text(id, 8, h - 16, dim, status);

    window_present(id);
}

void ntux_user_entry(void) {
    window_t id = 0x454449544F5200ull; /* "EDITOR" */
    int w = 720, h = 440;
    if (window_init() != 0 || window_create(id, 120, 90, w, h, 0xFF0C121Bu, "Editor") != 0) {
        puts("[editor] window_create failed");
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/editor.bmp");

    for (int i = 0; i < EDIT_MAX_LINES; ++i) g_lines[i][0] = '\0';
    g_line_count = 1;

    for (;;) {
        if (window_should_close(id)) break;
        if (key_edge(0x01)) break; /* Esc */

        int ctrl = (sys_kbd_is_pressed(0x1D) > 0) ? 1 : 0;
        if (ctrl && key_edge(0x18)) { /* O */
            g_dialog_pending = 1;
            window_open_file_picker("Open File", "/", 0);
        } else if (ctrl && key_edge(0x1F)) { /* S */
            if (g_current_path[0]) {
                save_file(g_current_path);
            } else {
                strncpy(g_status, "No file opened", sizeof(g_status) - 1);
                g_status[sizeof(g_status) - 1] = '\0';
            }
        }

        if (key_edge(0x48)) move_up();
        if (key_edge(0x50)) move_down();
        if (key_edge(0x4B)) move_left();
        if (key_edge(0x4D)) move_right();

        uint64_t now = sys_get_ticks();
        long ch = sys_getchar();
        if (ch >= 32 && ch < 127) {
            if (now - g_last_key_tick >= 2u) {
                insert_char((char)ch);
                g_last_key_tick = now;
            }
        } else if (ch == '\n' || ch == '\r') {
            if (now - g_last_key_tick >= 2u) {
                newline_char();
                g_last_key_tick = now;
            }
        } else if (ch == '\b' || ch == 127) {
            if (now - g_last_key_tick >= 2u) {
                backspace_char();
                g_last_key_tick = now;
            }
        }

        if (g_dialog_pending) {
            char path[256];
            uint32_t code = 0;
            long rc = window_dialog_pop(path, sizeof(path), &code);
            if (rc == 0 && code == 1) {
                if (g_dialog_pending == 1) {
                    (void)load_file(path);
                } else if (g_dialog_pending == 2) {
                    (void)save_file(path);
                }
                g_dialog_pending = 0;
            } else if (rc == 0 && code == 0) {
                g_dialog_pending = 0;
            }
        }

        draw_editor(id, w, h);
        sys_wait_ticks(1);
    }

    sys_exit(0);
}

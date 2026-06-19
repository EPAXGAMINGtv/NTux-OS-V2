#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <window.h>
#include <args.h>

#define EDIT_W 820
#define EDIT_H 520
#define EDIT_MAX_LINES 512
#define EDIT_LINE_LEN 256

static char g_lines[EDIT_MAX_LINES][EDIT_LINE_LEN];
static int g_line_count = 1;
static int g_cur_line = 0;
static int g_cur_col = 0;
static int g_scroll = 0;
static char g_path[256] = "";
static char g_status[80] = "Ready";
static uint8_t g_key_last[128];
static uint64_t g_last_text_tick = 0;

static int key_edge(int focused, int sc) {
    if (sc < 0 || sc >= (int)sizeof(g_key_last)) return 0;
    if (!focused) {
        g_key_last[sc] = 0;
        return 0;
    }
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int edge = now && !g_key_last[sc];
    g_key_last[sc] = (uint8_t)now;
    return edge;
}

static void set_status(const char* s) {
    if (!s) return;
    strncpy(g_status, s, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
}

static void reset_doc(void) {
    for (int i = 0; i < EDIT_MAX_LINES; ++i) g_lines[i][0] = '\0';
    g_line_count = 1;
    g_cur_line = 0;
    g_cur_col = 0;
    g_scroll = 0;
}

static void clamp_cursor(void) {
    if (g_line_count < 1) g_line_count = 1;
    if (g_cur_line < 0) g_cur_line = 0;
    if (g_cur_line >= g_line_count) g_cur_line = g_line_count - 1;
    int len = (int)strlen(g_lines[g_cur_line]);
    if (g_cur_col < 0) g_cur_col = 0;
    if (g_cur_col > len) g_cur_col = len;
}

static int split_parent_name(const char* full, char* parent, char* name, size_t cap) {
    if (!full || full[0] != '/' || !parent || !name || cap < 4) return -1;
    const char* slash = 0;
    for (const char* p = full; *p; ++p) if (*p == '/') slash = p;
    if (!slash || !slash[1]) return -1;
    size_t plen = (size_t)(slash - full);
    if (plen == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        if (plen >= cap) return -1;
        memcpy(parent, full, plen);
        parent[plen] = '\0';
    }
    strncpy(name, slash + 1, cap - 1);
    name[cap - 1] = '\0';
    return 0;
}

static int load_file(const char* path) {
    if (!path || !path[0]) return -1;
    uint64_t len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0) {
        set_status("Open failed");
        return -1;
    }
    if (len > 128u * 1024u) len = 128u * 1024u;
    char* buf = (char*)malloc((size_t)len + 1u);
    if (!buf) {
        set_status("Out of memory");
        return -1;
    }
    if (sys_fs_read_file(path, buf, len, &len) != 0) {
        free(buf);
        set_status("Open failed");
        return -1;
    }
    buf[len] = '\0';
    reset_doc();
    int line = 0;
    size_t pos = 0;
    while (pos < len && line < EDIT_MAX_LINES) {
        size_t start = pos;
        while (pos < len && buf[pos] != '\n' && buf[pos] != '\r') pos++;
        size_t n = pos - start;
        if (n >= EDIT_LINE_LEN) n = EDIT_LINE_LEN - 1;
        memcpy(g_lines[line], buf + start, n);
        g_lines[line][n] = '\0';
        line++;
        if (pos < len && buf[pos] == '\r') pos++;
        if (pos < len && buf[pos] == '\n') pos++;
    }
    if (line < 1) line = 1;
    g_line_count = line;
    strncpy(g_path, path, sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = '\0';
    free(buf);
    set_status("Opened");
    clamp_cursor();
    return 0;
}

static int save_file(const char* path) {
    if (!path || !path[0]) {
        set_status("No path");
        return -1;
    }
    size_t cap = 160u * 1024u;
    char* buf = (char*)malloc(cap);
    if (!buf) {
        set_status("Out of memory");
        return -1;
    }
    size_t used = 0;
    for (int i = 0; i < g_line_count; ++i) {
        size_t n = strlen(g_lines[i]);
        if (used + n + 2 >= cap) break;
        memcpy(buf + used, g_lines[i], n);
        used += n;
        if (i + 1 < g_line_count) buf[used++] = '\n';
    }
    long rc = sys_fs_write_file(path, buf, (uint64_t)used);
    if (rc != 0) {
        char parent[256], name[256];
        if (split_parent_name(path, parent, name, sizeof(parent)) == 0) {
            rc = sys_fs_create_file(parent, name, buf, (uint64_t)used);
        }
    }
    free(buf);
    if (rc == 0) {
        strncpy(g_path, path, sizeof(g_path) - 1);
        g_path[sizeof(g_path) - 1] = '\0';
        set_status("Saved");
        return 0;
    }
    set_status("Save failed");
    return -1;
}

static void insert_char(char c) {
    char* line = g_lines[g_cur_line];
    int len = (int)strlen(line);
    if (len + 1 >= EDIT_LINE_LEN) return;
    if (g_cur_col > len) g_cur_col = len;
    memmove(line + g_cur_col + 1, line + g_cur_col, (size_t)(len - g_cur_col + 1));
    line[g_cur_col++] = c;
}

static void newline_char(void) {
    if (g_line_count >= EDIT_MAX_LINES) return;
    char* line = g_lines[g_cur_line];
    int len = (int)strlen(line);
    if (g_cur_col > len) g_cur_col = len;
    for (int i = g_line_count; i > g_cur_line + 1; --i) {
        memcpy(g_lines[i], g_lines[i - 1], EDIT_LINE_LEN);
    }
    strncpy(g_lines[g_cur_line + 1], line + g_cur_col, EDIT_LINE_LEN - 1);
    g_lines[g_cur_line + 1][EDIT_LINE_LEN - 1] = '\0';
    line[g_cur_col] = '\0';
    g_line_count++;
    g_cur_line++;
    g_cur_col = 0;
}

static void backspace_char(void) {
    if (g_cur_col > 0) {
        char* line = g_lines[g_cur_line];
        int len = (int)strlen(line);
        memmove(line + g_cur_col - 1, line + g_cur_col, (size_t)(len - g_cur_col + 1));
        g_cur_col--;
        return;
    }
    if (g_cur_line <= 0) return;
    int prev = g_cur_line - 1;
    int prev_len = (int)strlen(g_lines[prev]);
    int cur_len = (int)strlen(g_lines[g_cur_line]);
    if (prev_len + cur_len >= EDIT_LINE_LEN) return;
    strcat(g_lines[prev], g_lines[g_cur_line]);
    for (int i = g_cur_line; i + 1 < g_line_count; ++i) {
        memcpy(g_lines[i], g_lines[i + 1], EDIT_LINE_LEN);
    }
    g_line_count--;
    g_cur_line = prev;
    g_cur_col = prev_len;
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

static void draw_editor(window_t id) {
    int line_h = 12;
    int top = 48;
    int rows = (EDIT_H - top - 34) / line_h;
    if (rows < 1) rows = 1;
    if (g_cur_line < g_scroll) g_scroll = g_cur_line;
    if (g_cur_line >= g_scroll + rows) g_scroll = g_cur_line - rows + 1;
    if (g_scroll < 0) g_scroll = 0;

    window_clear(id, 0xFFF7FAFCu);
    window_draw_rect(id, 0, 0, EDIT_W, 38, 0xFFE7EEF5u, 1);
    window_draw_text(id, 14, 12, 0xFF152536u, "Editor");
    window_draw_button(id, 92, 8, 68, 24, "Open", WINDOW_BUTTON_SECONDARY);
    window_draw_button(id, 168, 8, 68, 24, "Save", WINDOW_BUTTON_PRIMARY);
    window_draw_text(id, 252, 14, 0xFF5B6C7Du, g_path[0] ? g_path : "(untitled)");

    for (int i = 0; i < rows; ++i) {
        int idx = g_scroll + i;
        if (idx >= g_line_count) break;
        int y = top + i * line_h;
        char ln[16];
        snprintf(ln, sizeof(ln), "%4d", idx + 1);
        window_draw_text(id, 10, y, 0xFF8A9BADu, ln);
        window_draw_text(id, 52, y, 0xFF17212Bu, g_lines[idx]);
        if (idx == g_cur_line) {
            int cx = 52 + g_cur_col * 8;
            window_draw_line(id, cx, y + 10, cx + 7, y + 10, 0xFF246BFEu);
        }
    }

    char footer[128];
    snprintf(footer, sizeof(footer), "Ln %d Col %d | %s", g_cur_line + 1, g_cur_col + 1, g_status);
    window_draw_rect(id, 0, EDIT_H - 26, EDIT_W, 26, 0xFFE7EEF5u, 1);
    window_draw_text(id, 14, EDIT_H - 18, 0xFF4D6174u, footer);
    window_present(id);
}

void ntux_user_entry(void) {
    window_t id = 0x454449544F5200ull;
    if (window_init() != 0 || window_create(id, 110, 80, EDIT_W, EDIT_H, 0xFFF7FAFCu, "Editor") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/editor.bmp");
    reset_doc();
    const char* arg = ntux_arg(0);
    if (arg && arg[0]) (void)load_file(arg);

    int last_left = 0;
    int dialog_open = 0;
    for (;;) {
        if (window_should_close(id)) break;
        window_input_state_t in;
        memset(&in, 0, sizeof(in));
        (void)window_get_input_state(id, &in);
        if (in.close_requested) break;

        int focused = in.focused ? 1 : 0;
        if (focused && in.mouse_left && !last_left) {
            if (in.mouse_y >= 8 && in.mouse_y < 32 && in.mouse_x >= 92 && in.mouse_x < 160) {
                dialog_open = 1;
                window_open_file_picker("Open File", "/", 0);
            } else if (in.mouse_y >= 8 && in.mouse_y < 32 && in.mouse_x >= 168 && in.mouse_x < 236) {
                if (g_path[0]) (void)save_file(g_path);
                else set_status("Open a file first");
            }
        }
        last_left = focused ? in.mouse_left : 0;

        if (key_edge(focused, 0x01)) break;
        int ctrl = focused && (sys_kbd_is_pressed(0x1D) > 0);
        if (ctrl && key_edge(focused, 0x18)) {
            dialog_open = 1;
            window_open_file_picker("Open File", "/", 0);
        }
        if (ctrl && key_edge(focused, 0x1F)) {
            if (g_path[0]) (void)save_file(g_path);
            else set_status("Open a file first");
        }
        if (key_edge(focused, 0x48) && g_cur_line > 0) g_cur_line--;
        if (key_edge(focused, 0x50) && g_cur_line + 1 < g_line_count) g_cur_line++;
        if (key_edge(focused, 0x4B)) move_left();
        if (key_edge(focused, 0x4D)) move_right();
        clamp_cursor();

        if (focused) {
            uint64_t now = sys_get_ticks();
            long ch = sys_getchar();
            if (now - g_last_text_tick >= 2u) {
                if (ch >= 32 && ch < 127 && !ctrl) {
                    insert_char((char)ch);
                    g_last_text_tick = now;
                } else if (ch == '\n' || ch == '\r') {
                    newline_char();
                    g_last_text_tick = now;
                } else if (ch == '\b' || ch == 127) {
                    backspace_char();
                    g_last_text_tick = now;
                }
            }
        }

        if (dialog_open) {
            char path[256];
            uint32_t code = 0;
            if (window_dialog_pop(path, sizeof(path), &code) == 0) {
                if (code == 1 && path[0]) (void)load_file(path);
                dialog_open = 0;
            }
        }

        draw_editor(id);
        sys_wait_ticks(1);
    }
    window_close(id);
    sys_exit(0);
}

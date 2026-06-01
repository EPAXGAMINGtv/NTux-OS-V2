#include <terminal_engine.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void term_init(term_engine_t* t, int rows, int cols) {
    memset(t, 0, sizeof(*t));
    if (rows < 10) rows = TERM_BUF_ROWS;
    if (cols < 10) cols = TERM_BUF_COLS;
    if (rows > TERM_BUF_ROWS) rows = TERM_BUF_ROWS;
    if (cols > TERM_BUF_COLS) cols = TERM_BUF_COLS;
    t->rows = rows;
    t->cols = cols;
    t->scroll_top = 0;
    t->scroll_bottom = rows - 1;
    t->fg = TERM_COLOR_DEFAULT_FG;
    t->bg = TERM_COLOR_DEFAULT_BG;
    t->cursor_visible = 1;
    for (int i = 0; i < cols; i++) {
        t->tab_stops[i] = (i % TERM_TAB_STOP == 0) ? 1 : 0;
    }
}

void term_resize(term_engine_t* t, int rows, int cols) {
    if (rows < 10) rows = 10;
    if (cols < 10) cols = 10;
    if (rows > TERM_BUF_ROWS) rows = TERM_BUF_ROWS;
    if (cols > TERM_BUF_COLS) cols = TERM_BUF_COLS;
    if (t->cur_x >= cols) t->cur_x = cols - 1;
    if (t->cur_y >= rows) t->cur_y = rows - 1;
    t->rows = rows;
    t->cols = cols;
    t->scroll_top = 0;
    t->scroll_bottom = rows - 1;
}

static void term_scroll_region_up(term_engine_t* t) {
    term_cell_t* top = t->screen[t->scroll_top];
    if (t->scrollback_count < TERM_SCROLLBACK) {
        int idx = (t->scrollback_head + t->scrollback_count) % TERM_SCROLLBACK;
        memcpy(t->scrollback[idx], top, t->cols * sizeof(term_cell_t));
        t->scrollback_count++;
    } else {
        int idx = t->scrollback_head;
        memcpy(t->scrollback[idx], top, t->cols * sizeof(term_cell_t));
        t->scrollback_head = (t->scrollback_head + 1) % TERM_SCROLLBACK;
    }
    for (int r = t->scroll_top; r < t->scroll_bottom; r++) {
        memcpy(t->screen[r], t->screen[r + 1], t->cols * sizeof(term_cell_t));
    }
    memset(t->screen[t->scroll_bottom], 0, t->cols * sizeof(term_cell_t));
    for (int c = 0; c < t->cols; c++) {
        t->screen[t->scroll_bottom][c].bg = t->bg;
    }
}

void term_scroll_up(term_engine_t* t, int n) {
    if (n <= 0) return;
    if (n > t->rows) n = t->rows;
    for (int i = 0; i < n; i++) {
        term_scroll_region_up(t);
    }
}

void term_scroll_down(term_engine_t* t, int n) {
    if (n <= 0) return;
    if (n > t->rows) n = t->rows;
    for (int i = 0; i < n; i++) {
        for (int r = t->scroll_bottom; r > t->scroll_top; r--) {
            memcpy(t->screen[r], t->screen[r - 1], t->cols * sizeof(term_cell_t));
        }
        memset(t->screen[t->scroll_top], 0, t->cols * sizeof(term_cell_t));
    }
}

void term_set_scroll_region(term_engine_t* t, int top, int bottom) {
    if (top < 0) top = 0;
    if (bottom >= t->rows) bottom = t->rows - 1;
    if (top < bottom) {
        t->scroll_top = top;
        t->scroll_bottom = bottom;
    }
}

static void term_write_cell(term_engine_t* t, uint8_t ch) {
    if (t->cur_x >= t->cols) {
        t->cur_x = 0;
        t->cur_y++;
        if (t->cur_y > t->scroll_bottom) {
            t->cur_y = t->scroll_bottom;
            term_scroll_region_up(t);
        }
    }
    term_cell_t* cell = &t->screen[t->cur_y][t->cur_x];
    cell->ch = ch;
    cell->fg = t->fg;
    cell->bg = t->bg;
    cell->attr = t->attr;
    t->cur_x++;
}

void term_putchar(term_engine_t* t, uint8_t ch) {
    if (ch == '\n') {
        term_newline(t);
    } else if (ch == '\r') {
        term_carriage_return(t);
    } else if (ch == '\t') {
        term_tab(t);
    } else if (ch == '\b') {
        term_backspace(t);
    } else if (ch == '\a') {
    } else if (ch >= 32) {
        term_write_cell(t, ch);
    }
}

void term_newline(term_engine_t* t) {
    t->cur_x = 0;
    t->cur_y++;
    if (t->cur_y > t->scroll_bottom) {
        t->cur_y = t->scroll_bottom;
        term_scroll_region_up(t);
    }
}

void term_carriage_return(term_engine_t* t) {
    t->cur_x = 0;
}

void term_tab(term_engine_t* t) {
    do {
        t->cur_x++;
    } while (t->cur_x < t->cols && !t->tab_stops[t->cur_x]);
    if (t->cur_x >= t->cols) {
        t->cur_x = t->cols - 1;
    }
}

void term_backspace(term_engine_t* t) {
    if (t->cur_x > 0) t->cur_x--;
}

void term_clear_screen(term_engine_t* t) {
    for (int r = 0; r < t->rows; r++) {
        memset(t->screen[r], 0, t->cols * sizeof(term_cell_t));
        for (int c = 0; c < t->cols; c++) {
            t->screen[r][c].bg = t->bg;
        }
    }
    t->cur_x = 0;
    t->cur_y = 0;
}

void term_clear_to_end(term_engine_t* t) {
    int start_x = t->cur_x;
    int start_y = t->cur_y;
    for (int c = start_x; c < t->cols; c++) {
        memset(&t->screen[start_y][c], 0, sizeof(term_cell_t));
        t->screen[start_y][c].bg = t->bg;
    }
    for (int r = start_y + 1; r < t->rows; r++) {
        memset(t->screen[r], 0, t->cols * sizeof(term_cell_t));
        for (int c = 0; c < t->cols; c++) {
            t->screen[r][c].bg = t->bg;
        }
    }
}

void term_clear_line(term_engine_t* t) {
    memset(t->screen[t->cur_y], 0, t->cols * sizeof(term_cell_t));
    for (int c = 0; c < t->cols; c++) {
        t->screen[t->cur_y][c].bg = t->bg;
    }
}

void term_cursor_up(term_engine_t* t, int n) {
    if (n <= 0) n = 1;
    t->cur_y -= n;
    if (t->cur_y < 0) t->cur_y = 0;
}

void term_cursor_down(term_engine_t* t, int n) {
    if (n <= 0) n = 1;
    t->cur_y += n;
    if (t->cur_y >= t->rows) t->cur_y = t->rows - 1;
}

void term_cursor_right(term_engine_t* t, int n) {
    if (n <= 0) n = 1;
    t->cur_x += n;
    if (t->cur_x >= t->cols) t->cur_x = t->cols - 1;
}

void term_cursor_left(term_engine_t* t, int n) {
    if (n <= 0) n = 1;
    t->cur_x -= n;
    if (t->cur_x < 0) t->cur_x = 0;
}

void term_cursor_set(term_engine_t* t, int x, int y) {
    if (x < 0) x = 0;
    if (x >= t->cols) x = t->cols - 1;
    if (y < 0) y = 0;
    if (y >= t->rows) y = t->rows - 1;
    t->cur_x = x;
    t->cur_y = y;
}

typedef struct {
    int saved_x;
    int saved_y;
} term_saved_cursor_t;

static term_saved_cursor_t g_saved_cursors[8];
static int g_saved_cursor_count = 0;

void term_cursor_save(term_engine_t* t) {
    (void)t;
    if (g_saved_cursor_count < 8) {
        g_saved_cursors[g_saved_cursor_count].saved_x = t->cur_x;
        g_saved_cursors[g_saved_cursor_count].saved_y = t->cur_y;
        g_saved_cursor_count++;
    }
}

void term_cursor_restore(term_engine_t* t) {
    if (g_saved_cursor_count > 0) {
        g_saved_cursor_count--;
        t->cur_x = g_saved_cursors[g_saved_cursor_count].saved_x;
        t->cur_y = g_saved_cursors[g_saved_cursor_count].saved_y;
    }
}

void term_set_fg(term_engine_t* t, int color) {
    if (color >= 0 && color < TERM_COLOR_COUNT) {
        t->fg = (uint8_t)color;
    }
}

void term_set_bg(term_engine_t* t, int color) {
    if (color >= 0 && color < TERM_COLOR_COUNT) {
        t->bg = (uint8_t)color;
    }
}

void term_set_attr(term_engine_t* t, int attr) {
    t->attr |= (uint8_t)attr;
}

void term_reset_attr(term_engine_t* t) {
    t->attr = TERM_ATTR_NONE;
}

void term_set_cursor_visible(term_engine_t* t, int visible) {
    t->cursor_visible = visible ? 1 : 0;
}

void term_scrollback_up(term_engine_t* t, int n) {
    if (n <= 0) n = 1;
    t->scroll_offset += n;
    int max_offset = t->scrollback_count;
    if (t->scroll_offset > max_offset) t->scroll_offset = max_offset;
}

void term_scrollback_down(term_engine_t* t, int n) {
    if (n <= 0) n = 1;
    t->scroll_offset -= n;
    if (t->scroll_offset < 0) t->scroll_offset = 0;
}

void term_scrollback_reset(term_engine_t* t) {
    t->scroll_offset = 0;
}

void term_set_tab(term_engine_t* t) {
    if (t->cur_x < t->cols) t->tab_stops[t->cur_x] = 1;
}

void term_clear_tab(term_engine_t* t) {
    if (t->cur_x < t->cols) t->tab_stops[t->cur_x] = 0;
}

void term_clear_all_tabs(term_engine_t* t) {
    for (int i = 0; i < t->cols; i++) {
        t->tab_stops[i] = 0;
    }
}

void term_erase_chars(term_engine_t* t, int n) {
    if (n <= 0) return;
    int end = t->cur_x + n;
    if (end > t->cols) end = t->cols;
    for (int c = t->cur_x; c < end; c++) {
        memset(&t->screen[t->cur_y][c], 0, sizeof(term_cell_t));
        t->screen[t->cur_y][c].bg = t->bg;
    }
}

void term_insert_lines(term_engine_t* t, int n) {
    if (n <= 0) return;
    int bot = t->scroll_bottom;
    for (int i = 0; i < n && t->cur_y + i <= bot; i++) {
        for (int r = bot; r > t->cur_y; r--) {
            memcpy(t->screen[r], t->screen[r - 1], t->cols * sizeof(term_cell_t));
        }
        memset(t->screen[t->cur_y], 0, t->cols * sizeof(term_cell_t));
        for (int c = 0; c < t->cols; c++) {
            t->screen[t->cur_y][c].bg = t->bg;
        }
    }
}

void term_delete_lines(term_engine_t* t, int n) {
    if (n <= 0) return;
    int bot = t->scroll_bottom;
    for (int i = 0; i < n && t->cur_y + i <= bot; i++) {
        for (int r = t->cur_y; r < bot; r++) {
            memcpy(t->screen[r], t->screen[r + 1], t->cols * sizeof(term_cell_t));
        }
        memset(t->screen[bot], 0, t->cols * sizeof(term_cell_t));
        for (int c = 0; c < t->cols; c++) {
            t->screen[bot][c].bg = t->bg;
        }
    }
}

void term_delete_chars(term_engine_t* t, int n) {
    if (n <= 0) return;
    int end = t->cur_x + n;
    if (end > t->cols) end = t->cols;
    int count = end - t->cur_x;
    for (int c = t->cur_x; c < t->cols - count; c++) {
        t->screen[t->cur_y][c] = t->screen[t->cur_y][c + count];
    }
    for (int c = t->cols - count; c < t->cols; c++) {
        memset(&t->screen[t->cur_y][c], 0, sizeof(term_cell_t));
        t->screen[t->cur_y][c].bg = t->bg;
    }
}

void term_insert_blanks(term_engine_t* t, int n) {
    if (n <= 0) return;
    if (t->cur_x + n > t->cols) n = t->cols - t->cur_x;
    for (int c = t->cols - 1; c >= t->cur_x + n; c--) {
        t->screen[t->cur_y][c] = t->screen[t->cur_y][c - n];
    }
    for (int c = t->cur_x; c < t->cur_x + n && c < t->cols; c++) {
        memset(&t->screen[t->cur_y][c], 0, sizeof(term_cell_t));
        t->screen[t->cur_y][c].bg = t->bg;
    }
}

uint32_t term_resolve_color(uint8_t idx) {
    if (idx < TERM_COLOR_COUNT) {
        return term_ansi_colors[idx];
    }
    return term_ansi_colors[TERM_COLOR_DEFAULT_FG];
}

static int term_parse_number(const char** p, int* val) {
    *val = 0;
    int count = 0;
    while (**p >= '0' && **p <= '9') {
        *val = *val * 10 + (**p - '0');
        (*p)++;
        count++;
    }
    return count;
}

static int term_parse_params(const char** p, int* params, int max_params) {
    int count = 0;
    while (count < max_params) {
        if (**p >= '0' && **p <= '9') {
            term_parse_number(p, &params[count]);
            count++;
        } else if (**p == ';' || **p == ':') {
            if (count == 0) {
                params[count++] = 0;
            }
            (*p)++;
        } else {
            break;
        }
    }
    if (count == 0) {
        params[count++] = 0;
    }
    return count;
}

static void term_apply_sgr(term_engine_t* t, int* params, int count) {
    for (int i = 0; i < count; i++) {
        int p = params[i];
        if (p == 0) {
            t->fg = TERM_COLOR_DEFAULT_FG;
            t->bg = TERM_COLOR_DEFAULT_BG;
            t->attr = TERM_ATTR_NONE;
        } else if (p == 1) {
            t->attr |= TERM_ATTR_BOLD;
        } else if (p == 2) {
            t->attr |= TERM_ATTR_DIM;
        } else if (p == 4) {
            t->attr |= TERM_ATTR_UNDERLINE;
        } else if (p == 5 || p == 6) {
            t->attr |= TERM_ATTR_BLINK;
        } else if (p == 7) {
            t->attr |= TERM_ATTR_REVERSE;
        } else if (p == 8) {
            t->attr |= TERM_ATTR_HIDDEN;
        } else if (p == 22) {
            t->attr &= ~(TERM_ATTR_BOLD | TERM_ATTR_DIM);
        } else if (p == 24) {
            t->attr &= ~TERM_ATTR_UNDERLINE;
        } else if (p == 25) {
            t->attr &= ~TERM_ATTR_BLINK;
        } else if (p == 27) {
            t->attr &= ~TERM_ATTR_REVERSE;
        } else if (p == 28) {
            t->attr &= ~TERM_ATTR_HIDDEN;
        } else if (p >= 30 && p <= 37) {
            t->fg = (uint8_t)(p - 30);
        } else if (p == 38) {
            if (i + 2 < count && params[i + 1] == 5) {
                int idx = params[i + 2];
                if (idx >= 0 && idx < 256) {
                    t->fg = (idx < 16) ? (uint8_t)idx : TERM_COLOR_DEFAULT_FG;
                }
                i += 2;
            }
        } else if (p == 39) {
            t->fg = TERM_COLOR_DEFAULT_FG;
        } else if (p >= 40 && p <= 47) {
            t->bg = (uint8_t)(p - 40);
        } else if (p == 48) {
            if (i + 2 < count && params[i + 1] == 5) {
                int idx = params[i + 2];
                if (idx >= 0 && idx < 256) {
                    t->bg = (idx < 16) ? (uint8_t)idx : TERM_COLOR_DEFAULT_BG;
                }
                i += 2;
            }
        } else if (p == 49) {
            t->bg = TERM_COLOR_DEFAULT_BG;
        } else if (p >= 90 && p <= 97) {
            t->fg = (uint8_t)(p - 90 + TERM_COLOR_BRIGHT_BLACK);
        } else if (p >= 100 && p <= 107) {
            t->bg = (uint8_t)(p - 100 + TERM_COLOR_BRIGHT_BLACK);
        }
    }
}

void term_write(term_engine_t* t, const char* data, int len) {
    if (!data || len <= 0) return;
    for (int i = 0; i < len; i++) {
        uint8_t ch = (uint8_t)data[i];
        if (ch == 0x1B) {
            if (i + 1 < len) {
                i++;
                ch = (uint8_t)data[i];
                if (ch == '[') {
                    i++;
                    if (i < len) {
                        const char* esc_start = data + i;
                        int esc_len = 0;
                        while (i + esc_len < len) {
                            char ec = data[i + esc_len];
                            esc_len++;
                            if ((ec >= 0x40 && ec <= 0x7E) && !(ec >= 0x60 && ec <= 0x7E)) {
                                break;
                            }
                        }
                        const char* p = esc_start;
                        int esc_remaining = esc_len;
                        if (esc_remaining > 0) {
                            char cmd = data[i + esc_len - 1];
                            int params[32];
                            int pcount = 0;
                            if (esc_remaining > 1) {
                                pcount = term_parse_params(&p, params, 32);
                            }
                            int remaining_after_params = esc_remaining - (int)(p - esc_start);
                            if (remaining_after_params > 0) {
                                cmd = data[i + esc_len - remaining_after_params];
                            }
                            switch (cmd) {
                                case 'A': term_cursor_up(t, pcount > 0 ? params[0] : 1); break;
                                case 'B': term_cursor_down(t, pcount > 0 ? params[0] : 1); break;
                                case 'C': term_cursor_right(t, pcount > 0 ? params[0] : 1); break;
                                case 'D': term_cursor_left(t, pcount > 0 ? params[0] : 1); break;
                                case 'E': t->cur_y += (pcount > 0 ? params[0] : 1); t->cur_x = 0; if (t->cur_y >= t->rows) t->cur_y = t->rows - 1; break;
                                case 'F': t->cur_y -= (pcount > 0 ? params[0] : 1); t->cur_x = 0; if (t->cur_y < 0) t->cur_y = 0; break;
                                case 'G': t->cur_x = pcount > 0 ? params[0] - 1 : 0; if (t->cur_x < 0) t->cur_x = 0; break;
                                case 'H': case 'f': {
                                    int x = pcount > 1 ? params[1] : 1;
                                    int y = pcount > 0 ? params[0] : 1;
                                    term_cursor_set(t, x - 1, y - 1);
                                    break;
                                }
                                case 'J': {
                                    if (pcount == 0 || params[0] == 0) term_clear_to_end(t);
                                    else if (params[0] == 1) { /* clear from start */ for (int r = 0; r < t->cur_y; r++) { memset(t->screen[r], 0, t->cols * sizeof(term_cell_t)); } memset(t->screen[t->cur_y], 0, (t->cur_x + 1) * sizeof(term_cell_t)); }
                                    else if (params[0] == 2 || params[0] == 3) term_clear_screen(t);
                                    break;
                                }
                                case 'K': {
                                    if (pcount == 0 || params[0] == 0) { for (int c = t->cur_x; c < t->cols; c++) { memset(&t->screen[t->cur_y][c], 0, sizeof(term_cell_t)); t->screen[t->cur_y][c].bg = t->bg; } }
                                    else if (params[0] == 1) { for (int c = 0; c <= t->cur_x; c++) { memset(&t->screen[t->cur_y][c], 0, sizeof(term_cell_t)); t->screen[t->cur_y][c].bg = t->bg; } }
                                    else if (params[0] == 2) term_clear_line(t);
                                    break;
                                }
                                case 'L': term_insert_lines(t, pcount > 0 ? params[0] : 1); break;
                                case 'M': term_delete_lines(t, pcount > 0 ? params[0] : 1); break;
                                case 'P': term_delete_chars(t, pcount > 0 ? params[0] : 1); break;
                                case '@': term_insert_blanks(t, pcount > 0 ? params[0] : 1); break;
                                case 'X': term_erase_chars(t, pcount > 0 ? params[0] : 1); break;
                                case 'S': term_scroll_up(t, pcount > 0 ? params[0] : 1); break;
                                case 'T': term_scroll_down(t, pcount > 0 ? params[0] : 1); break;
                                case 'd': { int y = pcount > 0 ? params[0] - 1 : 0; if (y < 0) y = 0; if (y >= t->rows) y = t->rows - 1; t->cur_y = y; break; }
                                case 'm': term_apply_sgr(t, params, pcount); break;
                                case 's': term_cursor_save(t); break;
                                case 'u': term_cursor_restore(t); break;
                                case 'r': {
                                    int top = pcount > 0 ? params[0] - 1 : 0;
                                    int bot = pcount > 1 ? params[1] - 1 : t->rows - 1;
                                    term_set_scroll_region(t, top, bot);
                                    break;
                                }
                                case 'h': {
                                    for (int j = 0; j < pcount; j++) {
                                        if (params[j] == 4 || params[j] == 20) { /* insert/scroll mode - ignore for now */ }
                                        if (params[j] == 25) t->cursor_visible = 1;
                                    }
                                    break;
                                }
                                case 'l': {
                                    for (int j = 0; j < pcount; j++) {
                                        if (params[j] == 25) t->cursor_visible = 0;
                                    }
                                    break;
                                }
                                case 'g': {
                                    if (pcount == 0 || params[0] == 0) term_clear_tab(t);
                                    else if (params[0] == 3) term_clear_all_tabs(t);
                                    break;
                                }
                            }
                            i += esc_len - 1;
                        }
                    }
                } else if (ch == ']') {
                    while (i < len && data[i] != '\a' && data[i] != 0x9C) i++;
                } else if (ch == 'D') {
                    term_scroll_up(t, 1);
                } else if (ch == 'M') {
                    term_scroll_down(t, 1);
                } else if (ch == 'c') {
                } else if (ch == '7') {
                    term_cursor_save(t);
                } else if (ch == '8') {
                    term_cursor_restore(t);
                }
            }
        } else if (ch == '\n') {
            term_newline(t);
        } else if (ch == '\r') {
            term_carriage_return(t);
        } else if (ch == '\t') {
            term_tab(t);
        } else if (ch == '\b') {
            term_backspace(t);
        } else if (ch == '\a') {
        } else if (ch == 14 || ch == 15) {
        } else if (ch >= 32) {
            term_write_cell(t, ch);
        }
    }
}

#ifndef NTUX_TERMINAL_ENGINE_H
#define NTUX_TERMINAL_ENGINE_H

#include <stdint.h>

#define TERM_BUF_ROWS 48
#define TERM_BUF_COLS 160
#define TERM_SCROLLBACK 256
#define TERM_TAB_STOP 8

typedef enum {
    TERM_ATTR_NONE      = 0,
    TERM_ATTR_BOLD      = 1,
    TERM_ATTR_DIM       = 2,
    TERM_ATTR_UNDERLINE = 4,
    TERM_ATTR_BLINK     = 8,
    TERM_ATTR_REVERSE   = 16,
    TERM_ATTR_HIDDEN    = 32
} term_attr_t;

typedef struct {
    uint8_t ch;
    uint8_t fg;
    uint8_t bg;
    uint8_t attr;
} term_cell_t;

typedef struct {
    uint8_t fg;
    uint8_t bg;
    uint8_t attr;
    uint8_t dirty;
} term_cursor_t;

typedef struct term_engine {
    term_cell_t screen[TERM_BUF_ROWS][TERM_BUF_COLS];
    int rows;
    int cols;
    int cur_x;
    int cur_y;
    int scroll_top;
    int scroll_bottom;
    uint8_t fg;
    uint8_t bg;
    uint8_t attr;
    uint8_t cursor_visible;
    term_cell_t scrollback[TERM_SCROLLBACK][TERM_BUF_COLS];
    int scrollback_head;
    int scrollback_count;
    int scroll_offset;
    int tab_stops[TERM_BUF_COLS];
    char input_buf[4096];
    int input_len;
    int input_pos;
} term_engine_t;

enum {
    TERM_COLOR_BLACK,
    TERM_COLOR_RED,
    TERM_COLOR_GREEN,
    TERM_COLOR_YELLOW,
    TERM_COLOR_BLUE,
    TERM_COLOR_MAGENTA,
    TERM_COLOR_CYAN,
    TERM_COLOR_WHITE,
    TERM_COLOR_BRIGHT_BLACK,
    TERM_COLOR_BRIGHT_RED,
    TERM_COLOR_BRIGHT_GREEN,
    TERM_COLOR_BRIGHT_YELLOW,
    TERM_COLOR_BRIGHT_BLUE,
    TERM_COLOR_BRIGHT_MAGENTA,
    TERM_COLOR_BRIGHT_CYAN,
    TERM_COLOR_BRIGHT_WHITE,
    TERM_COLOR_DEFAULT_FG = 16,
    TERM_COLOR_DEFAULT_BG = 17,
    TERM_COLOR_COUNT = 18
};

static const uint32_t term_ansi_colors[TERM_COLOR_COUNT] = {
    0xFF1A1A2E, 0xFFE74C3C, 0xFF2ECC71, 0xFFF1C40F,
    0xFF3498DB, 0xFF9B59B6, 0xFF1ABC9C, 0xFFDFE6E9,
    0xFF636E72, 0xFFE74C3C, 0xFF55EFC4, 0xFFFDCB6E,
    0xFF74B9FF, 0xFFA29BFE, 0xFF00CEC9, 0xFFFFEAA7,
    0xFFBFD0FF, 0xFF0C121B
};

void term_init(term_engine_t* t, int rows, int cols);
void term_resize(term_engine_t* t, int rows, int cols);
void term_write(term_engine_t* t, const char* data, int len);
void term_putchar(term_engine_t* t, uint8_t ch);
void term_newline(term_engine_t* t);
void term_carriage_return(term_engine_t* t);
void term_tab(term_engine_t* t);
void term_backspace(term_engine_t* t);
void term_clear_screen(term_engine_t* t);
void term_clear_to_end(term_engine_t* t);
void term_clear_line(term_engine_t* t);
void term_cursor_up(term_engine_t* t, int n);
void term_cursor_down(term_engine_t* t, int n);
void term_cursor_right(term_engine_t* t, int n);
void term_cursor_left(term_engine_t* t, int n);
void term_cursor_set(term_engine_t* t, int x, int y);
void term_cursor_save(term_engine_t* t);
void term_cursor_restore(term_engine_t* t);
void term_scroll_up(term_engine_t* t, int n);
void term_scroll_down(term_engine_t* t, int n);
void term_set_scroll_region(term_engine_t* t, int top, int bottom);
void term_set_fg(term_engine_t* t, int color);
void term_set_bg(term_engine_t* t, int color);
void term_set_attr(term_engine_t* t, int attr);
void term_reset_attr(term_engine_t* t);
void term_set_cursor_visible(term_engine_t* t, int visible);
void term_scrollback_up(term_engine_t* t, int n);
void term_scrollback_down(term_engine_t* t, int n);
void term_scrollback_reset(term_engine_t* t);
void term_set_tab(term_engine_t* t);
void term_clear_tab(term_engine_t* t);
void term_clear_all_tabs(term_engine_t* t);
void term_erase_chars(term_engine_t* t, int n);
void term_insert_lines(term_engine_t* t, int n);
void term_delete_lines(term_engine_t* t, int n);
void term_delete_chars(term_engine_t* t, int n);
void term_insert_blanks(term_engine_t* t, int n);
int term_process_escape(term_engine_t* t, const char** data, int* len);
uint32_t term_resolve_color(uint8_t idx);

#endif

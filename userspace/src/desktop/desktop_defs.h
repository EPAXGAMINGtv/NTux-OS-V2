#ifndef NTUX_DESKTOP_DEFS_H
#define NTUX_DESKTOP_DEFS_H

#include <stdint.h>
#include <terminal_engine.h>

#define DESK_MAX_WINDOWS 100
#define DESK_MAX_TITLE 48
#define DESK_TERM_LINES TERM_BUF_ROWS
#define DESK_TERM_COLS TERM_BUF_COLS
#define DESK_CMD_BUF 4096
#define DESK_BG_MAX_FILE (24u * 1024u * 1024u)
#define DESK_LS_MAX 256
#define DESK_CAT_MAX 4096
#define DESK_MAX_ICONS 1024
#define DESK_ICON_W 84
#define DESK_ICON_H 96
#define DESK_TASKBAR_H 26
#define DESK_TITLEBAR_H 22
#define DESK_MIN_W 220
#define DESK_MIN_H 140
#define DESK_START_W 420
#define DESK_START_MAX_ITEMS 256
#define DESK_SCAN_MAX_DEPTH 24
#define DESK_BROWSER_ROW_H 18
#define DESK_BROWSER_BTN_H 16
#define DESK_DBLCLICK_TICKS 40u
#define DESK_CANVAS_OPS 128
#define DESK_THEMES 1
#define DESK_MAX_USERS 16
#define DESK_CONF_MAX 8192
#define DESK_INST_PATH_MAX 256
#define DESK_INST_LS_MAX 128
#define DESK_INST_FILE_MAX (12u * 1024u * 1024u)
#define DESK_INST_LOG_LINES 14

typedef struct {
    uint8_t type;
    uint8_t filled;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    int16_t x2;
    int16_t y2;
    uint32_t color;
    char text[128];
} desk_canvas_op_t;

typedef struct {
    const char* name;
    uint32_t wall_a;
    uint32_t wall_b;
    uint32_t wall_c;
    uint32_t accent;
    uint32_t border_focus;
    uint32_t border_blur;
    uint32_t title_focus;
    uint32_t title_blur;
    uint32_t window_fill;
    uint32_t text_main;
    uint32_t text_dim;
    uint32_t taskbar_bg;
    uint32_t taskbar_border;
    uint32_t start_bg;
    uint32_t start_item;
} desk_theme_t;

typedef struct desk_browser_state desk_browser_state_t;

typedef struct {
    uint64_t id;
    int x;
    int y;
    int w;
    int h;
    int prev_x;
    int prev_y;
    int prev_w;
    int prev_h;
    uint32_t bg;
    uint8_t visible;
    uint8_t terminal;
    uint8_t term_slot;
    uint8_t file_browser;
    int browser_id;
    uint8_t analog_clock;
    uint8_t settings_app;
    uint8_t minimized;
    uint8_t maximized;
    uint8_t animating;
    uint8_t anim_type;
    uint8_t closing;
    uint16_t anim_dur;
    uint64_t anim_start;
    int anim_from_x;
    int anim_from_y;
    int anim_from_w;
    int anim_from_h;
    int anim_to_x;
    int anim_to_y;
    int anim_to_w;
    int anim_to_h;
    uint8_t canvas_enabled;
    uint8_t canvas_dirty;
    uint64_t close_tick;
    uint64_t birth_tick;
    int owner_tid;
    int draw_count;
    uint32_t canvas_clear;
    desk_canvas_op_t draw_ops[DESK_CANVAS_OPS];
    int present_count;
    uint32_t present_clear;
    desk_canvas_op_t present_ops[DESK_CANVAS_OPS];
    uint8_t present_valid;
    int canvas_base_w;
    int canvas_base_h;
    uint8_t image_enabled;
    uint8_t image_channels;
    uint16_t image_w;
    uint16_t image_h;
    uint8_t* image_data;
    uint8_t icon_ready;
    uint16_t icon_w;
    uint16_t icon_h;
    uint8_t* icon_data;
    char title[DESK_MAX_TITLE];
    char text[320];
} desk_window_t;

typedef struct {
    term_engine_t engine;
    char lines[DESK_TERM_LINES][DESK_TERM_COLS + 1];
    int line_count;
    char input[4096];
    int input_len;
    int input_pos;
    char cwd[256];
    int history_count;
    char history[256][256];
    int history_pos;
    int search_active;
    char search_buf[128];
    int search_pos;
} desk_term_state_t;

typedef struct {
    int scancode;
    char normal;
    char shifted;
} keymap_t;

typedef struct {
    uint64_t id;
    int x;
    int y;
    uint8_t visible;
    uint8_t is_dir;
    uint8_t is_image;
    uint8_t preview_ready;
    uint8_t preview_failed;
    uint8_t preview_loading;
    uint16_t preview_w;
    uint16_t preview_h;
    uint8_t* preview;
    uint8_t icon_ready;
    uint8_t icon_failed;
    uint16_t icon_w;
    uint16_t icon_h;
    uint8_t* icon_pixels;
    char icon_path[160];
    char label[24];
    char exec_path[160];
} desk_icon_t;

typedef struct {
    char name[64];
    uint8_t is_dir;
    uint64_t size;
} desk_browser_entry_t;

typedef struct {
    char name[32];
    char pass[32];
    uint32_t uid;
} desk_user_t;

enum {
    CANVAS_OP_RECT = 1,
    CANVAS_OP_LINE = 2,
    CANVAS_OP_TEXT = 3,
    CANVAS_OP_BUTTON = 4
};

#endif

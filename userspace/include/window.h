#ifndef NTUX_WINDOW_H
#define NTUX_WINDOW_H

#include <stdint.h>

typedef uint64_t window_t;

enum {
    WINDOW_BUTTON_SECONDARY = 0,
    WINDOW_BUTTON_PRIMARY = 1,
    WINDOW_BUTTON_DANGER = 2
};

/* Desktop window frame constants (must match desktop). */
#define WINDOW_TITLEBAR_H 22
#define WINDOW_CLIENT_OFF_X 2
#define WINDOW_CLIENT_OFF_Y (WINDOW_TITLEBAR_H + 1)

typedef struct {
    uint64_t window_id;
    int focused;
    int win_x;
    int win_y;
    int win_w;
    int win_h;
    int base_w;
    int base_h;
    int mouse_x;
    int mouse_y;
    int mouse_left;
    int mouse_right;
    int mouse_middle;
    int mouse_scroll;
    uint64_t close_window_id;
    uint64_t close_event_tick;
    int close_requested;
} window_input_state_t;

enum {
    WINDOW_PICKER_ALLOW_DIRS = 0x1u,
    WINDOW_PICKER_SAVE = 0x2u
};

enum {
    WINDOW_MSGBOX_OK = 0,
    WINDOW_MSGBOX_OK_CANCEL = 1,
    WINDOW_MSGBOX_YES_NO = 2
};

int window_init(void);
int window_create(window_t id, int x, int y, int w, int h, uint32_t color, const char* title);
int window_close(window_t id);
int window_show(window_t id, int visible);
int window_focus(window_t id);
int window_minimize(window_t id);
int window_toggle_maximize(window_t id);
int window_set_title(window_t id, const char* title);
int window_set_bg(window_t id, uint32_t color);
int window_move(window_t id, int x, int y);
int window_resize(window_t id, int w, int h);
int window_set_rect(window_t id, int x, int y, int w, int h);

int window_clear(window_t id, uint32_t color);
int window_draw_rect(window_t id, int x, int y, int w, int h, uint32_t color, int filled);
int window_draw_line(window_t id, int x0, int y0, int x1, int y1, uint32_t color);
int window_draw_text(window_t id, int x, int y, uint32_t color, const char* text);
int window_draw_button(window_t id, int x, int y, int w, int h, const char* text, int kind);
int window_draw_scrollbar(window_t id, int x, int y, int w, int h, int content, int view, int scroll, int horizontal);
int window_draw_dropdown(window_t id, int x, int y, int w, int h, const char* label, const char* value, int open, int focused);
int window_draw_dropdown_list(window_t id, int x, int y, int w, int item_h, int count, int sel, int scroll, int max_visible,
                              const char* (*item_at)(int));
int window_present(window_t id);

int window_set_image(window_t id, const char* path, int desired_channels);
int window_set_image_raw(window_t id, int w, int h, int channels, const void* data, uint32_t len);
int window_set_icon(window_t id, const char* path);
int window_get_input_state(window_t id, window_input_state_t* out);
int window_should_close(window_t id);

int window_open_console(void);
int window_open_explorer(void);
int window_open_settings(void);
int window_open_file_picker(const char* title, const char* start_dir, uint32_t flags);
int window_open_message_box(const char* title, const char* text, uint32_t flags);
int window_dialog_pop(char* out, uint32_t cap, uint32_t* out_code);
int window_notify(const char* title, const char* body);
int window_set_anim_level(int level);
int window_set_theme(const char* name);

#endif

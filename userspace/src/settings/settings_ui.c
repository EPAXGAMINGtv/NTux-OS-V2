#include "settings_app.h"
#include <string.h>
#include <syscall.h>

static int point_in(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

static void draw_header(window_t id) {
    (void)window_clear(id, 0xFF0B1118u);
    for (int i = 0; i < 52; ++i) {
        uint32_t c = 0xFF0B1118u + (uint32_t)(i * 2);
        (void)window_draw_rect(id, 0, i, SET_WIN_W, 1, c, 1);
    }
    (void)window_draw_text(id, 20, 18, 0xFFEAF4FFu, "NTux Settings");
    (void)window_draw_text(id, 20, 42, 0xFF9DC3E3u, "Time zone and keyboard layout");
}

static void draw_dropdown(window_t id, int x, int y, int w,
                          const char* label, const char* value, int open, int focused) {
    (void)window_draw_dropdown(id, x, y, w, 36, label, value, open, focused);
}

static void draw_status(window_t id, const settings_state_t* st) {
    if (!st || !st->status[0]) return;
    if (sys_get_ticks() > st->status_until) return;
    (void)window_draw_rect(id, 20, SET_WIN_H - 54, SET_WIN_W - 40, 30, 0xFF0E1D2Bu, 1);
    (void)window_draw_rect(id, 20, SET_WIN_H - 54, SET_WIN_W - 40, 30, 0xFF2B4C6Bu, 0);
    (void)window_draw_text(id, 32, SET_WIN_H - 46, 0xFFB7D5F3u, st->status);
}

void settings_draw(window_t id, const settings_state_t* st) {
    int drop_w = (SET_WIN_W - 72) / 2;
    int drop_y = 120;
    int left_x = 24;
    int right_x = left_x + drop_w + 24;
    int list_item_h = 24;
    int max_visible = 7;
    int list_y = drop_y + 36 + 6;

    draw_header(id);
    draw_dropdown(id, left_x, drop_y, drop_w, "Time zone",
                  settings_timezone_at(st ? st->tz_sel : 0),
                  st ? st->tz_open : 0, st ? (st->focus == 0) : 0);
    draw_dropdown(id, right_x, drop_y, drop_w, "Keyboard layout",
                  settings_kbd_at(st ? st->kbd_sel : 0),
                  st ? st->kbd_open : 0, st ? (st->focus == 1) : 0);

    if (st && st->tz_open) {
        (void)window_draw_dropdown_list(id, left_x, list_y, drop_w, list_item_h,
                                        settings_timezone_count(), st->tz_sel, st->tz_scroll, max_visible,
                                        settings_timezone_at);
    }
    if (st && st->kbd_open) {
        (void)window_draw_dropdown_list(id, right_x, list_y, drop_w, list_item_h,
                                        settings_kbd_count(), st->kbd_sel, st->kbd_scroll, max_visible,
                                        settings_kbd_at);
    }

    (void)window_draw_button(id, 24, 388, 220, 44, "Save", WINDOW_BUTTON_PRIMARY);
    (void)window_draw_button(id, SET_WIN_W - 244, 388, 220, 44, "Close", WINDOW_BUTTON_SECONDARY);

    if (st) {
        if (st->focus == 2) {
            (void)window_draw_rect(id, 24, 388, 220, 44, 0xFF48B0FFu, 0);
        } else if (st->focus == 3) {
            (void)window_draw_rect(id, SET_WIN_W - 244, 388, 220, 44, 0xFF48B0FFu, 0);
        }
    }
    draw_status(id, st);
}

settings_action_t settings_handle_click(settings_state_t* st, int mx, int my) {
    int drop_w = (SET_WIN_W - 72) / 2;
    int drop_y = 120;
    int left_x = 24;
    int right_x = left_x + drop_w + 24;
    int list_item_h = 24;
    int max_visible = 7;
    int list_y = drop_y + 36 + 6;

    int save_x = 24;
    int save_y = 388;
    int save_w = 220;
    int save_h = 44;
    int close_x = SET_WIN_W - 244;
    int close_y = 388;
    int close_w = 220;
    int close_h = 44;

    if (point_in(mx, my, left_x, drop_y, drop_w, 36)) {
        st->focus = 0;
        st->tz_open = !st->tz_open;
        st->kbd_open = 0;
        return SETTINGS_ACT_NONE;
    }
    if (point_in(mx, my, right_x, drop_y, drop_w, 36)) {
        st->focus = 1;
        st->kbd_open = !st->kbd_open;
        st->tz_open = 0;
        return SETTINGS_ACT_NONE;
    }

    if (st->tz_open) {
        int visible = settings_timezone_count() - st->tz_scroll;
        if (visible > max_visible) visible = max_visible;
        int list_h = visible * list_item_h + 8;
        if (point_in(mx, my, left_x, list_y, drop_w, list_h)) {
            int idx = (my - (list_y + 4)) / list_item_h;
            int pick = st->tz_scroll + idx;
            if (pick >= 0 && pick < settings_timezone_count()) {
                st->tz_sel = pick;
                st->tz_open = 0;
            }
            return SETTINGS_ACT_NONE;
        }
    }
    if (st->kbd_open) {
        int visible = settings_kbd_count() - st->kbd_scroll;
        if (visible > max_visible) visible = max_visible;
        int list_h = visible * list_item_h + 8;
        if (point_in(mx, my, right_x, list_y, drop_w, list_h)) {
            int idx = (my - (list_y + 4)) / list_item_h;
            int pick = st->kbd_scroll + idx;
            if (pick >= 0 && pick < settings_kbd_count()) {
                st->kbd_sel = pick;
                st->kbd_open = 0;
            }
            return SETTINGS_ACT_NONE;
        }
    }

    if (point_in(mx, my, save_x, save_y, save_w, save_h)) {
        return SETTINGS_ACT_SAVE;
    }
    if (point_in(mx, my, close_x, close_y, close_w, close_h)) {
        return SETTINGS_ACT_CLOSE;
    }
    st->tz_open = 0;
    st->kbd_open = 0;
    return SETTINGS_ACT_NONE;
}

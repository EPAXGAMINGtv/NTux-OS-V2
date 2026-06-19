#include "settings_app.h"
#include <string.h>
#include <stdio.h>
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
    (void)window_draw_text(id, 20, 42, 0xFF9DC3E3u, "Time zone, keyboard, font, and appearance");
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
    int font_x = 24;
    int font_y = drop_y + 36 + 6 + 7 * 24 + 16;
    int dark_y = font_y + 54;
    int list_item_h = 24;
    int max_visible = 7;
    int list_y = drop_y + 36 + 6;
    int font_list_y = font_y + 36 + 6;

    draw_header(id);
    draw_dropdown(id, left_x, drop_y, drop_w, "Time zone",
                  settings_timezone_at(st ? st->tz_sel : 0),
                  st ? st->tz_open : 0, st ? (st->focus == 0) : 0);
    draw_dropdown(id, right_x, drop_y, drop_w, "Keyboard layout",
                  settings_kbd_at(st ? st->kbd_sel : 0),
                  st ? st->kbd_open : 0, st ? (st->focus == 1) : 0);
    draw_dropdown(id, font_x, font_y, SET_WIN_W - 48, "Font",
                  st ? st->font_path : settings_font_at(0),
                  st ? st->font_open : 0, st ? (st->focus == 3) : 0);

    (void)window_draw_text(id, font_x, dark_y + 2, 0xFFB9CBE0u, "Dark mode");
    (void)window_draw_rect(id, font_x + 130, dark_y, 54, 24, st && st->dark_mode ? 0xFF2F80EDu : 0xFF273443u, 1);
    (void)window_draw_rect(id, font_x + 130, dark_y, 54, 24, 0xFF6E8296u, 0);
    (void)window_draw_rect(id, font_x + 134 + ((st && st->dark_mode) ? 28 : 0), dark_y + 4, 18, 16, 0xFFEAF4FFu, 1);

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
    if (st && st->font_open) {
        (void)window_draw_dropdown_list(id, font_x, font_list_y, SET_WIN_W - 48, list_item_h,
                                        settings_font_count(), st->font_sel, st->font_scroll, max_visible,
                                        settings_font_at);
    }

    int save_x = 24, save_y = SET_WIN_H - 100, save_w = 220, save_h = 44;
    int close_x = SET_WIN_W - 244, close_y = SET_WIN_H - 100, close_w = 220, close_h = 44;

    (void)window_draw_button(id, save_x, save_y, save_w, save_h, "Save", WINDOW_BUTTON_PRIMARY);
    (void)window_draw_button(id, close_x, close_y, close_w, close_h, "Close", WINDOW_BUTTON_SECONDARY);

    if (st) {
        if (st->focus == 2) (void)window_draw_rect(id, save_x, save_y, save_w, save_h, 0xFF48B0FFu, 0);
        else if (st->focus == 4) (void)window_draw_rect(id, close_x, close_y, close_w, close_h, 0xFF48B0FFu, 0);
    }
    draw_status(id, st);
}

settings_action_t settings_handle_click(settings_state_t* st, int mx, int my) {
    int drop_w = (SET_WIN_W - 72) / 2;
    int drop_y = 120;
    int left_x = 24;
    int right_x = left_x + drop_w + 24;
    int font_x = 24;
    int font_y = drop_y + 36 + 6 + 7 * 24 + 16;
    int dark_y = font_y + 54;
    int list_item_h = 24;
    int max_visible = 7;
    int list_y = drop_y + 36 + 6;
    int font_list_y = font_y + 36 + 6;

    int save_x = 24, save_y = SET_WIN_H - 100, save_w = 220, save_h = 44;
    int close_x = SET_WIN_W - 244, close_y = SET_WIN_H - 100, close_w = 220, close_h = 44;

    if (point_in(mx, my, left_x, drop_y, drop_w, 36)) {
        st->focus = 0; st->tz_open = !st->tz_open; st->kbd_open = 0; st->font_open = 0;
        return SETTINGS_ACT_NONE;
    }
    if (point_in(mx, my, right_x, drop_y, drop_w, 36)) {
        st->focus = 1; st->kbd_open = !st->kbd_open; st->tz_open = 0; st->font_open = 0;
        return SETTINGS_ACT_NONE;
    }
    if (point_in(mx, my, font_x, font_y, SET_WIN_W - 48, 36)) {
        st->focus = 3; st->font_open = !st->font_open; st->tz_open = 0; st->kbd_open = 0;
        return SETTINGS_ACT_NONE;
    }
    if (point_in(mx, my, font_x + 130, dark_y, 54, 24)) {
        st->dark_mode = st->dark_mode ? 0 : 1;
        st->focus = 3;
        return SETTINGS_ACT_APPLY_APPEARANCE;
    }

    if (st->tz_open) {
        int visible = settings_timezone_count() - st->tz_scroll;
        if (visible > max_visible) visible = max_visible;
        int list_h = visible * list_item_h + 8;
        if (point_in(mx, my, left_x, list_y, drop_w, list_h)) {
            int idx = (my - (list_y + 4)) / list_item_h;
            int pick = st->tz_scroll + idx;
            if (pick >= 0 && pick < settings_timezone_count()) { st->tz_sel = pick; st->tz_open = 0; }
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
            if (pick >= 0 && pick < settings_kbd_count()) { st->kbd_sel = pick; st->kbd_open = 0; }
            return SETTINGS_ACT_NONE;
        }
    }
    if (st->font_open) {
        int visible = settings_font_count() - st->font_scroll;
        if (visible > max_visible) visible = max_visible;
        int list_h = visible * list_item_h + 8;
        if (point_in(mx, my, font_x, font_list_y, SET_WIN_W - 48, list_h)) {
            int idx = (my - (font_list_y + 4)) / list_item_h;
            int pick = st->font_scroll + idx;
            if (pick >= 0 && pick < settings_font_count()) {
                st->font_sel = pick;
                snprintf(st->font_path, FONT_PATH_MAX, "%s", settings_font_at(pick));
                st->font_open = 0;
            }
            return SETTINGS_ACT_NONE;
        }
    }

    if (point_in(mx, my, save_x, save_y, save_w, save_h)) return SETTINGS_ACT_SAVE;
    if (point_in(mx, my, close_x, close_y, close_w, close_h)) return SETTINGS_ACT_CLOSE;
    st->tz_open = 0; st->kbd_open = 0; st->font_open = 0;
    return SETTINGS_ACT_NONE;
}

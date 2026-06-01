#include "settings_app.h"
#include <string.h>
#include <syscall.h>

static void clamp_scroll_to_sel(int* scroll, int count, int visible, int sel) {
    if (!scroll) return;
    if (count < 1) count = 1;
    if (visible < 1) visible = 1;
    if (*scroll < 0) *scroll = 0;
    int max_scroll = (count > visible) ? (count - visible) : 0;
    if (*scroll > max_scroll) *scroll = max_scroll;
    if (sel < *scroll) *scroll = sel;
    if (sel >= *scroll + visible) *scroll = sel - visible + 1;
    if (*scroll < 0) *scroll = 0;
    if (*scroll > max_scroll) *scroll = max_scroll;
}

void ntux_user_entry(void) {
    window_t id = 0x53455454u;
    settings_state_t st;
    if (window_init() != 0) {
        sys_exit(1);
    }
    if (window_create(id, 120, 90, SET_WIN_W, SET_WIN_H, 0xFF0B1118u, "Settings") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/settings.bmp");
    window_show(id, 1);
    window_focus(id);

    settings_load_state(&st);

    int last_left = 0;
    uint8_t key_last[128];
    memset(key_last, 0, sizeof(key_last));
    for (;;) {
        if (window_should_close(id)) break;

        window_input_state_t st_in;
        memset(&st_in, 0, sizeof(st_in));
        (void)window_get_input_state(id, &st_in);
        if (st_in.close_requested) break;

        int tab_edge = (sys_kbd_is_pressed(0x0F) > 0) && !key_last[0x0F];
        int enter_edge = (sys_kbd_is_pressed(0x1C) > 0) && !key_last[0x1C];
        int space_edge = (sys_kbd_is_pressed(0x39) > 0) && !key_last[0x39];
        int up_edge = (sys_kbd_is_pressed(0x48) > 0) && !key_last[0x48];
        int down_edge = (sys_kbd_is_pressed(0x50) > 0) && !key_last[0x50];
        int left_edge = (sys_kbd_is_pressed(0x4B) > 0) && !key_last[0x4B];
        int right_edge = (sys_kbd_is_pressed(0x4D) > 0) && !key_last[0x4D];

        key_last[0x0F] = (uint8_t)(sys_kbd_is_pressed(0x0F) > 0);
        key_last[0x1C] = (uint8_t)(sys_kbd_is_pressed(0x1C) > 0);
        key_last[0x39] = (uint8_t)(sys_kbd_is_pressed(0x39) > 0);
        key_last[0x48] = (uint8_t)(sys_kbd_is_pressed(0x48) > 0);
        key_last[0x50] = (uint8_t)(sys_kbd_is_pressed(0x50) > 0);
        key_last[0x4B] = (uint8_t)(sys_kbd_is_pressed(0x4B) > 0);
        key_last[0x4D] = (uint8_t)(sys_kbd_is_pressed(0x4D) > 0);

        if (tab_edge) {
            st.focus = (st.focus + 1) % 4;
            if (st.focus >= 2) {
                st.tz_open = 0;
                st.kbd_open = 0;
            }
        }
        if (left_edge) {
            if (st.focus == 1) st.focus = 0;
        }
        if (right_edge) {
            if (st.focus == 0) st.focus = 1;
        }
        if (up_edge) {
            if (st.focus == 0 && st.tz_sel > 0) st.tz_sel--;
            if (st.focus == 1 && st.kbd_sel > 0) st.kbd_sel--;
        }
        if (down_edge) {
            if (st.focus == 0 && st.tz_sel + 1 < settings_timezone_count()) st.tz_sel++;
            if (st.focus == 1 && st.kbd_sel + 1 < settings_kbd_count()) st.kbd_sel++;
        }

        if (enter_edge || space_edge) {
            if (st.focus == 0) {
                st.tz_open = !st.tz_open;
                st.kbd_open = 0;
            } else if (st.focus == 1) {
                st.kbd_open = !st.kbd_open;
                st.tz_open = 0;
            } else if (st.focus == 2) {
                if (settings_save_state(&st) == 0) {
                    settings_show_status(&st, "Saved /conf/time.conf and /conf/kbdlout.conf");
                } else {
                    settings_show_status(&st, "Save failed");
                }
            } else if (st.focus == 3) {
                break;
            }
        }

        if (st_in.mouse_scroll != 0) {
            if (st.tz_open) {
                st.tz_scroll -= st_in.mouse_scroll;
            } else if (st.kbd_open) {
                st.kbd_scroll -= st_in.mouse_scroll;
            }
        }
        clamp_scroll_to_sel(&st.tz_scroll, settings_timezone_count(), 7, st.tz_sel);
        clamp_scroll_to_sel(&st.kbd_scroll, settings_kbd_count(), 7, st.kbd_sel);

        if (st_in.mouse_left && !last_left) {
            settings_action_t act = settings_handle_click(&st, st_in.mouse_x, st_in.mouse_y);
            if (act == SETTINGS_ACT_SAVE) {
                if (settings_save_state(&st) == 0) {
                    settings_show_status(&st, "Saved /conf/time.conf and /conf/kbdlout.conf");
                } else {
                    settings_show_status(&st, "Save failed");
                }
            } else if (act == SETTINGS_ACT_CLOSE) {
                break;
            }
        }
        last_left = st_in.mouse_left;

        settings_draw(id, &st);
        (void)window_present(id);
        sys_wait_ticks(1);
    }

    window_close(id);
    sys_exit(0);
}

#include "partutil_app.h"
#include <string.h>
#include <syscall.h>

void ntux_user_entry(void) {
    window_t id = 0x50555554u;
    if (window_init() != 0) {
        sys_exit(1);
    }
    if (window_create(id, 80, 60, PARTUTIL_WIN_W, PARTUTIL_WIN_H, 0xFF0B1118u, "Partition Utility") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/partutil.bmp");
    window_show(id, 1);
    window_focus(id);

    partutil_state_t st;
    partutil_init(&st);

    int last_left = 0;
    uint8_t key_last[128];
    memset(key_last, 0, sizeof(key_last));

    for (;;) {
        if (window_should_close(id)) break;

        window_input_state_t in;
        memset(&in, 0, sizeof(in));
        (void)window_get_input_state(id, &in);
        if (in.close_requested) break;

        int tab_edge = (sys_kbd_is_pressed(0x0F) > 0) && !key_last[0x0F];
        int enter_edge = (sys_kbd_is_pressed(0x1C) > 0) && !key_last[0x1C];
        int esc_edge = (sys_kbd_is_pressed(0x01) > 0) && !key_last[0x01];
        int up_edge = (sys_kbd_is_pressed(0x48) > 0) && !key_last[0x48];
        int down_edge = (sys_kbd_is_pressed(0x50) > 0) && !key_last[0x50];
        int left_edge = (sys_kbd_is_pressed(0x4B) > 0) && !key_last[0x4B];
        int right_edge = (sys_kbd_is_pressed(0x4D) > 0) && !key_last[0x4D];

        key_last[0x0F] = (uint8_t)(sys_kbd_is_pressed(0x0F) > 0);
        key_last[0x1C] = (uint8_t)(sys_kbd_is_pressed(0x1C) > 0);
        key_last[0x01] = (uint8_t)(sys_kbd_is_pressed(0x01) > 0);
        key_last[0x48] = (uint8_t)(sys_kbd_is_pressed(0x48) > 0);
        key_last[0x50] = (uint8_t)(sys_kbd_is_pressed(0x50) > 0);
        key_last[0x4B] = (uint8_t)(sys_kbd_is_pressed(0x4B) > 0);
        key_last[0x4D] = (uint8_t)(sys_kbd_is_pressed(0x4D) > 0);

        if (st.confirm_active) {
            if (left_edge || right_edge || tab_edge) st.confirm_choice = !st.confirm_choice;
            if (enter_edge) {
                if (st.confirm_choice) {
                    partutil_do_action(&st, st.confirm_action);
                }
                st.confirm_active = 0;
            }
            if (esc_edge) st.confirm_active = 0;
        } else {
            if (tab_edge) {
                st.focus_group = (st.focus_group + 1) % 3;
            }
            if (st.focus_group == 0) {
                if (up_edge && st.sel_drive > 0) {
                    st.sel_drive--;
                    st.sel_part = 0;
                    partutil_rescan(&st);
                }
                if (down_edge && st.sel_drive + 1 < (int)st.drive_count) {
                    st.sel_drive++;
                    st.sel_part = 0;
                    partutil_rescan(&st);
                }
                if (enter_edge) {
                    st.focus_group = 1;
                }
            } else if (st.focus_group == 1) {
                if (up_edge && st.sel_part > 0) st.sel_part--;
                if (down_edge && st.sel_part + 1 < (int)st.part_count) st.sel_part++;
                if (enter_edge) {
                    st.focus_group = 2;
                }
            } else if (st.focus_group == 2) {
                int max_btn = 8;
                if (left_edge && st.action_focus > 0) st.action_focus--;
                if (right_edge && st.action_focus + 1 < max_btn) st.action_focus++;
                if (enter_edge) {
                    partutil_action_t act = (partutil_action_t)(PARTUTIL_ACT_RESCAN + st.action_focus);
                    if (act == PARTUTIL_ACT_RESCAN) {
                        partutil_do_action(&st, act);
                    } else {
                        st.confirm_active = 1;
                        st.confirm_action = act;
                        st.confirm_choice = 1;
                    }
                }
            }
        }

        if (in.mouse_left && !last_left) {
            partutil_action_t act = partutil_hit_test(&st, in.mouse_x, in.mouse_y);
            if (st.confirm_active && act == st.confirm_action) {
                partutil_do_action(&st, act);
                st.confirm_active = 0;
            } else if ((int)act >= 1000 && (int)act < 2000) {
                st.sel_drive = (int)act - 1000;
                st.sel_part = 0;
                st.focus_group = 0;
                partutil_rescan(&st);
            } else if ((int)act >= 2000 && (int)act < 3000) {
                st.sel_part = (int)act - 2000;
                st.focus_group = 1;
            } else if (act != PARTUTIL_ACT_NONE) {
                if (act == PARTUTIL_ACT_RESCAN) {
                    st.focus_group = 2;
                    st.action_focus = 0;
                    partutil_do_action(&st, act);
                } else {
                    st.focus_group = 2;
                    st.action_focus = (int)(act - PARTUTIL_ACT_RESCAN);
                    st.confirm_active = 1;
                    st.confirm_action = act;
                    st.confirm_choice = 1;
                }
            } else if (st.confirm_active) {
                st.confirm_active = 0;
            }
        }
        last_left = in.mouse_left;

        partutil_draw(id, &st);
        window_present(id);
        sys_wait_ticks(1);
    }

    window_close(id);
    sys_exit(0);
}

#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <window.h>
#include <stdio.h>

#define TEST_W 540
#define TEST_H 360

typedef struct {
    const char* label;
    int x;
    int y;
    int w;
    int h;
} test_btn_t;

static const test_btn_t g_btns[] = {
    {"Notify", 24, 56, 120, 28},
    {"MsgBox", 164, 56, 120, 28},
    {"Picker", 304, 56, 120, 28},
    {"Console", 24, 100, 120, 28},
    {"Explorer", 164, 100, 120, 28},
    {"Settings", 304, 100, 120, 28},
    {"Anim++", 24, 144, 120, 28},
    {"Theme", 164, 144, 120, 28},
    {"Set Image", 304, 144, 120, 28}
};

static int btn_hit(int mx, int my, const test_btn_t* b) {
    return (mx >= b->x && mx < b->x + b->w && my >= b->y && my < b->y + b->h);
}

static void draw_ui(window_t win, int hover_idx, const char* status) {
    window_clear(win, 0xFF0B121Bu);
    window_draw_text(win, 18, 16, 0xFFEAF4FFu, "Desktop API Test");
    window_draw_text(win, 18, 34, 0xFF9FC2DDu, "Click buttons to test desktop features.");

    for (int i = 0; i < (int)(sizeof(g_btns) / sizeof(g_btns[0])); ++i) {
        uint32_t bg = (i == hover_idx) ? 0xFF2BC6FFu : 0xFF1D78C9u;
        window_draw_rect(win, g_btns[i].x, g_btns[i].y, g_btns[i].w, g_btns[i].h, bg, 1);
        window_draw_rect(win, g_btns[i].x, g_btns[i].y, g_btns[i].w, g_btns[i].h, 0xFF78C9FFu, 0);
        window_draw_text(win, g_btns[i].x + 10, g_btns[i].y + 8, 0xFFF4FBFFu, g_btns[i].label);
    }

    if (status && status[0]) {
        window_draw_rect(win, 18, 200, TEST_W - 36, 120, 0xFF101926u, 1);
        window_draw_rect(win, 18, 200, TEST_W - 36, 120, 0xFF223246u, 0);
        window_draw_text(win, 28, 214, 0xFFEAF4FFu, "Last result:");
        window_draw_text(win, 28, 234, 0xFF9FC2DDu, status);
    }
    window_present(win);
}

void ntux_user_entry(void) {
    window_t win = 0x1A01u;
    if (window_init() != 0) {
        sys_exit(1);
    }
    if (window_create(win, 120, 90, TEST_W, TEST_H, 0xFF0B121Bu, "Desktop API Test") != 0) {
        sys_exit(1);
    }
    window_show(win, 1);
    window_focus(win);

    int last_left = 0;
    char status[160];
    status[0] = '\0';
    int anim_level = 2;
    int theme_flip = 0;
    uint64_t last_notify_tick = 0;
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;

    for (;;) {
        window_input_state_t st;
        memset(&st, 0, sizeof(st));
        (void)window_get_input_state(win, &st);
        if (window_should_close(win) || st.close_requested) break;

        int mx = st.mouse_x;
        int my = st.mouse_y;
        int hover = -1;
        for (int i = 0; i < (int)(sizeof(g_btns) / sizeof(g_btns[0])); ++i) {
            if (btn_hit(mx, my, &g_btns[i])) { hover = i; break; }
        }

        if (st.mouse_left && !last_left && hover >= 0) {
            const char* label = g_btns[hover].label;
            if (strcmp(label, "Notify") == 0) {
                window_notify("Desktop Test", "Notification from test.elf");
                strncpy(status, "Notification sent", sizeof(status) - 1);
            } else if (strcmp(label, "MsgBox") == 0) {
                window_open_message_box("Desktop Test", "Message box from test.elf", WINDOW_MSGBOX_OK_CANCEL);
                strncpy(status, "Opened message box", sizeof(status) - 1);
            } else if (strcmp(label, "Picker") == 0) {
                window_open_file_picker("Pick File", "/", WINDOW_PICKER_ALLOW_DIRS);
                strncpy(status, "Opened file picker", sizeof(status) - 1);
            } else if (strcmp(label, "Console") == 0) {
                window_open_console();
                strncpy(status, "Console requested", sizeof(status) - 1);
            } else if (strcmp(label, "Explorer") == 0) {
                window_open_explorer();
                strncpy(status, "Explorer requested", sizeof(status) - 1);
            } else if (strcmp(label, "Settings") == 0) {
                window_open_settings();
                strncpy(status, "Settings requested", sizeof(status) - 1);
            } else if (strcmp(label, "Anim++") == 0) {
                anim_level = (anim_level + 1) % 4;
                window_set_anim_level(anim_level);
                snprintf(status, sizeof(status), "Animation level = %d", anim_level);
            } else if (strcmp(label, "Theme") == 0) {
                theme_flip = !theme_flip;
                window_set_theme(theme_flip ? "Ocean" : "Mono");
                window_open_message_box("Theme", "Error", WINDOW_MSGBOX_OK_CANCEL);
                strncpy(status, "Theme switched", sizeof(status) - 1);
            } else if (strcmp(label, "Set Image") == 0) {
                if (window_set_image(win, "/wallpaper/wallpaper.jpg", 3) != 0) {
                    (void)window_set_image(win, "/boot/boot/res/background/4.bmp", 3);
                }
                strncpy(status, "Window image set", sizeof(status) - 1);
            }
            status[sizeof(status) - 1] = '\0';
        }

        uint32_t code = 0;
        char dialog[128];
        if (window_dialog_pop(dialog, sizeof(dialog), &code) == 0) {
            if (dialog[0]) {
                snprintf(status, sizeof(status), "Dialog: %s (code=%u)", dialog, code);
            } else {
                snprintf(status, sizeof(status), "Dialog code=%u", code);
            }
        }

        uint64_t now = sys_get_ticks();
        if (now - last_notify_tick >= hz) {
            window_notify("test", "test");
            last_notify_tick = now;
        }

        draw_ui(win, hover, status);
        last_left = st.mouse_left;
        sys_wait_ticks(1);
    }

    window_close(win);
    sys_exit(0);
}

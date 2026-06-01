#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <window.h>

#define XEYES_W 640
#define XEYES_H 360

static int isqrt_i32(int v) {
    if (v <= 0) return 0;
    int r = 0;
    int bit = 1 << 30;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

static void draw_px(window_t id, int x, int y, uint32_t c) {
    (void)window_draw_rect(id, x, y, 1, 1, c, 1);
}

static void draw_circle(window_t id, int cx, int cy, int r, uint32_t c, int filled) {
    if (r <= 0) return;
    int rr = r * r;
    for (int y = -r; y <= r; ++y) {
        int yy = y * y;
        int x = isqrt_i32(rr - yy);
        if (filled) {
            (void)window_draw_rect(id, cx - x, cy + y, x * 2 + 1, 1, c, 1);
        } else {
            draw_px(id, cx - x, cy + y, c);
            draw_px(id, cx + x, cy + y, c);
        }
    }
}

static void draw_eye(window_t id, int cx, int cy, int mx, int my) {
    const int eye_r = 64;
    const int pupil_r = 16;
    const int max_off = eye_r - pupil_r - 6;
    int dx = mx - cx;
    int dy = my - cy;
    int dist = isqrt_i32(dx * dx + dy * dy);
    int px = cx;
    int py = cy;
    if (dist > 0) {
        if (dist > max_off) {
            dx = (dx * max_off) / dist;
            dy = (dy * max_off) / dist;
        }
        px = cx + dx;
        py = cy + dy;
    }

    /* Sclera */
    draw_circle(id, cx, cy, eye_r, 0xFFF6FBFFu, 1);
    draw_circle(id, cx, cy, eye_r, 0xFF1B2A3Au, 0);

    /* Iris */
    draw_circle(id, px, py, 28, 0xFF86C6FFu, 1);
    draw_circle(id, px, py, 28, 0xFF2B4D6Bu, 0);

    /* Pupil */
    draw_circle(id, px, py, pupil_r, 0xFF0A1018u, 1);
    draw_circle(id, px, py, pupil_r, 0xFF3E5A7Au, 0);

    /* Highlight */
    draw_circle(id, px - 6, py - 6, 5, 0xFFFFFFFFu, 1);
}

void ntux_user_entry(void) {
    window_t id = 0x58594553u;
    if (window_init() != 0) {
        sys_exit(1);
    }
    if (window_create(id, 140, 110, XEYES_W, XEYES_H, 0xFF0B1118u, "xeyes") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/xeyes.bmp");
    window_show(id, 1);
    window_focus(id);

    for (;;) {
        if (window_should_close(id)) sys_exit(0);
        if (sys_kbd_is_pressed(0x01) > 0) sys_exit(0);

        window_input_state_t st;
        memset(&st, 0, sizeof(st));
        (void)window_get_input_state(id, &st);
        int mx = st.mouse_x;
        int my = st.mouse_y;

        (void)window_clear(id, 0xFF0B1118u);
        for (int i = 0; i < 36; ++i) {
            uint32_t c = 0xFF0B1118u + (uint32_t)(i * 2);
            (void)window_draw_rect(id, 0, i, XEYES_W, 1, c, 1);
        }

        int cx1 = XEYES_W / 3;
        int cx2 = XEYES_W * 2 / 3;
        int cy = XEYES_H / 2 + 10;
        draw_eye(id, cx1, cy, mx, my);
        draw_eye(id, cx2, cy, mx, my);

        (void)window_draw_text(id, 16, 18, 0xFF9EC5E5u, "xeyes - track the cursor");
        (void)window_present(id);
        sys_wait_ticks(1);
    }

    window_close(id);
    sys_exit(0);
}

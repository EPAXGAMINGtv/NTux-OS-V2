#include <window.h>
#include <ttf.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <stdlib.h>
#include <args.h>

#define FV_W 820
#define FV_H 620
#define CELL_SIZE 36
#define GLYPH_PX 16

static int write_font_conf(const char* path) {
    if (!path || !path[0]) return -1;
    if (sys_fs_exists("/conf") <= 0) (void)sys_fs_mkdir("/", "conf");
    uint64_t len = (uint64_t)strlen(path);
    if (sys_fs_write_file("/conf/font.conf", path, len) == 0) return 0;
    return (sys_fs_create_file("/conf", "font.conf", path, len) == 0) ? 0 : -1;
}

void ntux_user_entry(void) {
    char font_path[256] = "/boot/res/fonts/Hack-Regular.ttf";
    if (ntux_argc() > 1) {
        const char* arg = ntux_arg(1);
        if (arg) snprintf(font_path, sizeof(font_path), "%s", arg);
    } else {
        uint64_t flen = 0;
        if (sys_fs_read_file("/tmp/fontview_path", font_path, sizeof(font_path) - 1, &flen) == 0 && flen > 0) {
            font_path[flen] = '\0';
            size_t n = strlen(font_path);
            while (n > 0 && (font_path[n-1] == '\n' || font_path[n-1] == '\r')) font_path[--n] = '\0';
        }
    }

    if (window_init() != 0) sys_exit(1);
    window_t id = 0x464F4E54u;
    if (window_create(id, 60, 40, FV_W, FV_H, 0xFF0E1419u, "Font Viewer") != 0) {
        puts("[fontview] window_create failed");
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/fontview.bmp");

    uint64_t fsize = 4 * 1024 * 1024;
    char* fdata = (char*)malloc(fsize);
    if (!fdata) { puts("[fontview] malloc failed"); sys_exit(1); }
    uint64_t actual = 0;
    if (sys_fs_read_file(font_path, fdata, fsize, &actual) != 0 || actual == 0) {
        puts("[fontview] font not found");
        free(fdata);
        sys_exit(1);
    }

    ttf_font_t* font = ttf_load(fdata, (size_t)actual);
    if (!font) {
        puts("[fontview] ttf_load failed");
        free(fdata);
        sys_exit(1);
    }

    int fh = ttf_get_height(font, GLYPH_PX);
    int asc = ttf_get_ascender(font, GLYPH_PX);

    int grid_cols = (FV_W - 40) / CELL_SIZE;
    if (grid_cols < 1) grid_cols = 1;
    int grid_rows = (FV_H - 80) / CELL_SIZE;
    if (grid_rows < 1) grid_rows = 1;
    int total = grid_cols * grid_rows;

    int buf_w = FV_W, buf_h = FV_H;
    uint32_t* buf = (uint32_t*)calloc((size_t)buf_w * buf_h, sizeof(uint32_t));
    if (!buf) { puts("[fontview] buf alloc failed"); free(fdata); ttf_free(font); sys_exit(1); }

    int scroll = 0;
    int max_scroll = 127 - total;
    if (max_scroll < 0) max_scroll = 0;
    int last_scroll_val = 0;
    int last_left = 0;
    char status[64] = "";

    for (;;) {
        if (window_should_close(id)) break;
        window_input_state_t st;
        memset(&st, 0, sizeof(st));
        window_get_input_state(id, &st);
        if (st.close_requested) break;

        if (!st.focused) {
            last_left = 0;
        } else if (st.mouse_scroll != last_scroll_val) {
            scroll -= (st.mouse_scroll - last_scroll_val);
            if (scroll < 0) scroll = 0;
            if (scroll > max_scroll) scroll = max_scroll;
            last_scroll_val = st.mouse_scroll;
        }
        if (st.focused && st.mouse_left && !last_left &&
            st.mouse_x >= 20 && st.mouse_x < 210 && st.mouse_y >= 18 && st.mouse_y < 44) {
            if (write_font_conf(font_path) == 0) snprintf(status, sizeof(status), "Desktop font updated");
            else snprintf(status, sizeof(status), "Could not save font");
        }
        last_left = st.focused ? st.mouse_left : 0;

        for (int y = 0; y < buf_h; ++y)
            for (int x = 0; x < buf_w; ++x)
                buf[(size_t)y * buf_w + x] = 0xFF0E1419u;

        int ox = 20, oy = 60;
        for (int i = 0; i < total && 32 + scroll + i <= 127; ++i) {
            int gi = 32 + scroll + i;
            int col = i % grid_cols;
            int row = i / grid_cols;
            int cx = ox + col * CELL_SIZE;
            int cy = oy + row * CELL_SIZE;

            ttf_glyph_t* g = ttf_render_glyph(font, (uint32_t)gi, GLYPH_PX);
            if (g && g->bitmap) {
                int gx = cx + (CELL_SIZE - g->width) / 2;
                int gy = cy + (CELL_SIZE - g->height) / 2 + asc - g->bearing_y;
                for (int py = 0; py < g->height && gy + py < buf_h; ++py) {
                    for (int px = 0; px < g->width && gx + px < buf_w; ++px) {
                        if (g->bitmap[(size_t)py * g->width + px]) {
                            int bx = gx + px, by = gy + py;
                            if (bx >= 0 && by >= 0 && bx < buf_w && by < buf_h)
                                buf[(size_t)by * buf_w + bx] = 0xFFD4E6FFu;
                        }
                    }
                }
            }
            ttf_free_glyph(g);
        }

        char info[192];
        snprintf(info, sizeof(info), "Font: %s  Size: %dpx  H:%d  Scroll:%d",
                 font_path, GLYPH_PX, fh, scroll);
        (void)info;

        window_set_image_raw(id, buf_w, buf_h, 4, buf, (uint32_t)((size_t)buf_w * buf_h * 4));
        window_draw_button(id, 20, 18, 190, 26, "Set as desktop font", WINDOW_BUTTON_PRIMARY);
        window_draw_text(id, 224, 25, 0xFFD4E6FFu, font_path);
        if (status[0]) window_draw_text(id, 20, FV_H - 24, 0xFF9BE7B1u, status);
        window_present(id);
        sys_wait_ticks(1);
    }

    free(buf);
    free(fdata);
    ttf_free(font);
    window_close(id);
    sys_exit(0);
}

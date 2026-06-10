#include "window_internal.h"
#include "desktop_internal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <image.h>
#include <syscall.h>

extern desk_window_t g_windows[DESK_MAX_WINDOWS];
extern int g_window_count;
extern int g_focus_index;

static void window_canvas_init_base(desk_window_t* w) {
    if (!w) return;
    if (w->canvas_base_w > 0 && w->canvas_base_h > 0) return;
    int bw = w->w - 4;
    int bh = w->h - DESK_TITLEBAR_H - 3;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    w->canvas_base_w = bw;
    w->canvas_base_h = bh;
}

void window_clamp_rect(desk_window_t* w, int fb_w, int fb_h) {
    int max_w = fb_w;
    int max_h = fb_h - DESK_TASKBAR_H;
    if (!w) return;
    if (max_w < DESK_MIN_W) max_w = DESK_MIN_W;
    if (max_h < DESK_MIN_H) max_h = DESK_MIN_H;

    if (w->w < DESK_MIN_W) w->w = DESK_MIN_W;
    if (w->h < DESK_MIN_H) w->h = DESK_MIN_H;
    if (w->w > max_w) w->w = max_w;
    if (w->h > max_h) w->h = max_h;

    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;
    if (w->x + w->w > max_w) w->x = max_w - w->w;
    if (w->y + w->h > max_h) w->y = max_h - w->h;
    if (w->x < 0) w->x = 0;
    if (w->y < 0) w->y = 0;
}

void desk_window_toggle_maximize(desk_window_t* w, int fb_w, int fb_h) {
    if (!w) return;
    if (!w->maximized) {
        w->prev_x = w->x; w->prev_y = w->y; w->prev_w = w->w; w->prev_h = w->h;
        w->x = 0;
        w->y = 0;
        w->w = fb_w;
        w->h = fb_h - DESK_TASKBAR_H;
        w->maximized = 1;
    } else {
        w->x = w->prev_x; w->y = w->prev_y; w->w = w->prev_w; w->h = w->prev_h;
        w->maximized = 0;
        window_clamp_rect(w, fb_w, fb_h);
    }
}

desk_window_t* window_find_by_id(uint64_t id) {
    for (int i = 0; i < g_window_count; ++i) {
        if (g_windows[i].id == id) return &g_windows[i];
    }
    return 0;
}

int window_index_by_id(uint64_t id) {
    for (int i = 0; i < g_window_count; ++i) {
        if (g_windows[i].id == id) return i;
    }
    return -1;
}

void window_compact_invisible(void) {
    int write = 0;
    for (int read = 0; read < g_window_count; ++read) {
        desk_window_t* w = &g_windows[read];
        if (!w->visible && !w->minimized && !w->closing) continue;
        if (write != read) g_windows[write] = g_windows[read];
        write++;
    }
    g_window_count = write;
    if (g_focus_index >= g_window_count) g_focus_index = g_window_count - 1;
    for (int i = 0; i < g_window_count; ++i) {
        if (g_windows[i].terminal) g_windows[i].term_slot = (uint8_t)(i % DESK_MAX_WINDOWS);
    }
}

int window_find_recyclable_slot(void) {
    for (int i = 0; i < g_window_count; ++i) {
        if (!g_windows[i].visible && !g_windows[i].minimized && !g_windows[i].closing) return i;
    }
    return -1;
}

void window_bring_to_front(int idx) {
    if (idx < 0 || idx >= g_window_count) return;
    if (idx == g_window_count - 1) {
        g_focus_index = idx;
        return;
    }
    desk_window_t tmp = g_windows[idx];
    for (int i = idx; i + 1 < g_window_count; ++i) g_windows[i] = g_windows[i + 1];
    g_windows[g_window_count - 1] = tmp;
    g_focus_index = g_window_count - 1;
}

void window_canvas_clear(desk_window_t* w, uint32_t color) {
    if (!w) return;
    window_canvas_init_base(w);
    w->canvas_enabled = 1;
    w->canvas_clear = color;
    w->draw_count = 0;
    w->canvas_dirty = 1;
    desktop_mark_dirty();
}

void window_canvas_rect(desk_window_t* w, int x, int y, int rw, int rh, uint32_t color, int filled) {
    if (!w || w->draw_count >= DESK_CANVAS_OPS) return;
    window_canvas_init_base(w);
    w->canvas_enabled = 1;
    desk_canvas_op_t* op = &w->draw_ops[w->draw_count++];
    memset(op, 0, sizeof(*op));
    op->type = CANVAS_OP_RECT;
    op->filled = (uint8_t)(filled ? 1 : 0);
    op->x = (int16_t)x;
    op->y = (int16_t)y;
    op->w = (int16_t)rw;
    op->h = (int16_t)rh;
    op->color = color;
    w->canvas_dirty = 1;
    desktop_mark_dirty();
}

void window_canvas_line(desk_window_t* w, int x0, int y0, int x1, int y1, uint32_t color) {
    if (!w || w->draw_count >= DESK_CANVAS_OPS) return;
    window_canvas_init_base(w);
    w->canvas_enabled = 1;
    desk_canvas_op_t* op = &w->draw_ops[w->draw_count++];
    memset(op, 0, sizeof(*op));
    op->type = CANVAS_OP_LINE;
    op->x = (int16_t)x0;
    op->y = (int16_t)y0;
    op->x2 = (int16_t)x1;
    op->y2 = (int16_t)y1;
    op->color = color;
    w->canvas_dirty = 1;
    desktop_mark_dirty();
}

void window_canvas_text(desk_window_t* w, int x, int y, uint32_t color, const char* text) {
    if (!w || w->draw_count >= DESK_CANVAS_OPS) return;
    window_canvas_init_base(w);
    w->canvas_enabled = 1;
    desk_canvas_op_t* op = &w->draw_ops[w->draw_count++];
    memset(op, 0, sizeof(*op));
    op->type = CANVAS_OP_TEXT;
    op->x = (int16_t)x;
    op->y = (int16_t)y;
    op->color = color;
    if (text) {
        strncpy(op->text, text, sizeof(op->text) - 1);
        op->text[sizeof(op->text) - 1] = '\0';
    } else {
        op->text[0] = '\0';
    }
    w->canvas_dirty = 1;
    desktop_mark_dirty();
}

void window_canvas_button(desk_window_t* w, int x, int y, int rw, int rh, int kind, const char* text) {
    if (!w || w->draw_count >= DESK_CANVAS_OPS) return;
    window_canvas_init_base(w);
    w->canvas_enabled = 1;
    desk_canvas_op_t* op = &w->draw_ops[w->draw_count++];
    memset(op, 0, sizeof(*op));
    op->type = CANVAS_OP_BUTTON;
    op->filled = (uint8_t)kind;
    op->x = (int16_t)x;
    op->y = (int16_t)y;
    op->w = (int16_t)rw;
    op->h = (int16_t)rh;
    op->color = 0;
    if (text) {
        strncpy(op->text, text, sizeof(op->text) - 1);
        op->text[sizeof(op->text) - 1] = '\0';
    } else {
        op->text[0] = '\0';
    }
    w->canvas_dirty = 1;
    desktop_mark_dirty();
}

void window_canvas_present(desk_window_t* w) {
    if (!w) return;
    w->canvas_enabled = 1;
    if (w->draw_count == 0 && !w->canvas_dirty) {
        w->present_valid = 1;
        desktop_mark_dirty();
        return;
    }
    w->present_count = w->draw_count;
    w->present_clear = w->canvas_clear;
    if (w->present_count > 0) {
        memcpy(w->present_ops, w->draw_ops, sizeof(desk_canvas_op_t) * (size_t)w->present_count);
    }
    w->present_valid = 1;
    w->draw_count = 0;
    w->canvas_dirty = 0;
    desktop_mark_dirty();
}

static void img_debug_log(const char* path, int rc) {
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "imgset path=[%s] rc=%d reason=[%s]\n",
                     path ? path : "(null)", rc,
                     (rc != 0) ? (image_failure_reason() ? image_failure_reason() : "?") : "ok");
    if (n > 0 && (size_t)n < sizeof(buf)) {
        (void)sys_fs_write_file("/tmp/img_debug", buf, (uint64_t)n);
    }
}

void desk_window_set_image(desk_window_t* w, const char* path, int desired_channels) {
    if (!w) return;
    if (w->image_data) {
        free(w->image_data);
        w->image_data = 0;
    }
    w->image_enabled = 0;
    w->image_w = 0;
    w->image_h = 0;
    w->image_channels = 0;
    if (!path || !path[0]) { img_debug_log(path, -99); return; }
    int desired = (desired_channels == 3 || desired_channels == 4) ? desired_channels : 3;
    image_t img;
    int rc = image_decode_file(path, desired, &img);
    if (rc != 0) { img_debug_log(path, rc); return; }
    if (!img.data || img.width <= 0 || img.height <= 0) {
        img_debug_log(path, -999);
        image_free(&img);
        return;
    }
    img_debug_log(path, 0);
    w->image_data = img.data;
    w->image_w = (uint16_t)img.width;
    w->image_h = (uint16_t)img.height;
    w->image_channels = (uint8_t)img.channels;
    w->image_enabled = 1;
    desktop_mark_dirty();
}

void desk_window_set_icon(desk_window_t* w, const char* path) {
    if (!w) return;
    if (w->icon_data) {
        free(w->icon_data);
        w->icon_data = 0;
    }
    w->icon_ready = 0;
    w->icon_w = 0;
    w->icon_h = 0;
    if (!path || !path[0]) return;
    image_t img;
    if (image_decode_file_scaled(path, 3, 32, 32, &img) != 0) return;
    if (!img.data || img.width <= 0 || img.height <= 0) {
        image_free(&img);
        return;
    }
    w->icon_data = img.data;
    w->icon_w = (uint16_t)img.width;
    w->icon_h = (uint16_t)img.height;
    w->icon_ready = 1;
    desktop_mark_dirty();
}

#include "taskbar.h"

#include <string.h>
#include <stdio.h>

#include "desktop_internal.h"

static int in_rect_local(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static int taskbar_window_listed(const desk_window_t* w) {
    if (!w) return 0;
    if (w->closing) return 0;
    return (w->visible || w->minimized) ? 1 : 0;
}

static void taskbar_window_title(const desk_window_t* w, char* out, size_t cap) {
    if (!out || cap == 0) return;
    if (w && w->title[0]) {
        strncpy(out, w->title, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    if (w && w->owner_tid > 0) {
        snprintf(out, cap, "App %d", w->owner_tid);
        return;
    }
    strncpy(out, "Window", cap - 1);
    out[cap - 1] = '\0';
}

static int taskbar_window_icon_w(const desk_window_t* w) {
    if (!w || !w->icon_ready || !w->icon_data || w->icon_w == 0 || w->icon_h == 0) return 0;
    return 18;
}

static void taskbar_draw_window_icon(const desk_window_t* w, int x, int y) {
    if (!w || !w->icon_ready || !w->icon_data || w->icon_w == 0 || w->icon_h == 0) return;
    desktop_draw_icon_pixels(x, y, 12, 12, w->icon_data, (int)w->icon_w, (int)w->icon_h);
}

static int taskbar_visible_windows(void) {
    int count = 0;
    for (int i = 0; i < g_window_count; ++i) {
        if (!taskbar_window_listed(&g_windows[i])) continue;
        count++;
    }
    return count;
}

static int taskbar_title_max_chars(int cell_w) {
    int max = (cell_w - 6) / 8;
    if (max < 0) max = 0;
    return max;
}

static void taskbar_copy_title(char* out, size_t cap, const char* title, int max_chars) {
    if (!out || cap == 0) return;
    if (!title) title = "Window";
    if (max_chars <= 0) {
        out[0] = '\0';
        return;
    }
    size_t len = strlen(title);
    if ((int)len <= max_chars) {
        strncpy(out, title, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    int keep = max_chars;
    if (max_chars >= 2) keep = max_chars - 1;
    if (keep < 1) keep = 1;
    if ((size_t)keep >= cap) keep = (int)cap - 1;
    memcpy(out, title, (size_t)keep);
    out[keep] = '\0';
    if (max_chars >= 2 && (size_t)(keep + 1) < cap) {
        out[keep] = '.';
        out[keep + 1] = '\0';
    }
}

int taskbar_window_at(int x, int y) {
    int h = 28;
    int pad = 12;
    int bar_y = (int)g_fb.height - h - 8;
    int bar_w = (int)g_fb.width - pad * 2;
    int clock_w = 88;
    int clock_x = pad + bar_w - clock_w - 8;
    int gallery_w = 22;
    int gallery_x = clock_x - gallery_w - 8;
    int tx = pad + 116;
    if (y < bar_y || y > bar_y + h) return -1;
    int visible = taskbar_visible_windows();
    if (visible <= 0) return -1;
    int avail = gallery_x - tx - 6;
    if (avail <= 0) return -1;
    int total_w = 0;
    for (int i = 0; i < g_window_count; ++i) {
        if (!taskbar_window_listed(&g_windows[i])) continue;
        char title_buf[48];
        taskbar_window_title(&g_windows[i], title_buf, sizeof(title_buf));
        total_w += (int)strlen(title_buf) * 8 + 12 + taskbar_window_icon_w(&g_windows[i]);
    }
    if (total_w <= avail) {
        int cx = tx;
        for (int i = 0; i < g_window_count; ++i) {
            if (!taskbar_window_listed(&g_windows[i])) continue;
            char title_buf[48];
            taskbar_window_title(&g_windows[i], title_buf, sizeof(title_buf));
            int ww = (int)strlen(title_buf) * 8 + 12 + taskbar_window_icon_w(&g_windows[i]);
            if (in_rect_local(x, y, cx, bar_y + 8, ww, 12)) return i;
            cx += ww + 6;
        }
    } else {
        int cell_w = avail / visible;
        if (cell_w < 8) cell_w = 8;
        int seen = 0;
        for (int i = 0; i < g_window_count; ++i) {
            if (!taskbar_window_listed(&g_windows[i])) continue;
            int cx = tx + seen * cell_w;
            if (in_rect_local(x, y, cx, bar_y + 8, cell_w, 12)) return i;
            seen++;
        }
    }
    return -1;
}

int taskbar_window_rect(int idx, int* out_x, int* out_y, int* out_w, int* out_h) {
    int h = 28;
    int pad = 12;
    int bar_y = (int)g_fb.height - h - 8;
    int bar_w = (int)g_fb.width - pad * 2;
    int clock_w = 88;
    int clock_x = pad + bar_w - clock_w - 8;
    int gallery_w = 22;
    int gallery_x = clock_x - gallery_w - 8;
    int tx = pad + 116;
    if (idx < 0 || idx >= g_window_count) return -1;
    if (!taskbar_window_listed(&g_windows[idx])) return -1;
    int visible = taskbar_visible_windows();
    if (visible <= 0) return -1;
    int avail = gallery_x - tx - 6;
    if (avail <= 0) return -1;

    int total_w = 0;
    for (int i = 0; i < g_window_count; ++i) {
        if (!taskbar_window_listed(&g_windows[i])) continue;
        char title_buf[48];
        taskbar_window_title(&g_windows[i], title_buf, sizeof(title_buf));
        total_w += (int)strlen(title_buf) * 8 + 12 + taskbar_window_icon_w(&g_windows[i]);
    }

    if (total_w <= avail) {
        int cx = tx;
        for (int i = 0; i < g_window_count; ++i) {
            if (!taskbar_window_listed(&g_windows[i])) continue;
            char title_buf[48];
            taskbar_window_title(&g_windows[i], title_buf, sizeof(title_buf));
            int ww = (int)strlen(title_buf) * 8 + 12 + taskbar_window_icon_w(&g_windows[i]);
            if (i == idx) {
                if (out_x) *out_x = cx;
                if (out_y) *out_y = bar_y + 8;
                if (out_w) *out_w = ww;
                if (out_h) *out_h = 12;
                return 0;
            }
            cx += ww + 6;
        }
    } else {
        int cell_w = avail / visible;
        if (cell_w < 8) cell_w = 8;
        int seen = 0;
        for (int i = 0; i < g_window_count; ++i) {
            if (!g_windows[i].visible) continue;
            if (i == idx) {
                if (out_x) *out_x = tx + seen * cell_w;
                if (out_y) *out_y = bar_y + 8;
                if (out_w) *out_w = cell_w;
                if (out_h) *out_h = 12;
                return 0;
            }
            seen++;
        }
    }
    return -1;
}

int taskbar_start_hit(int x, int y) {
    int h = 28;
    int pad = 12;
    int bar_y = (int)g_fb.height - h - 8;
    return in_rect_local(x, y, pad + 8, bar_y + 6, 96, 16);
}

int taskbar_clock_hit(int x, int y) {
    int h = 28;
    int pad = 12;
    int bar_y = (int)g_fb.height - h - 8;
    int bar_w = (int)g_fb.width - pad * 2;
    int clock_w = 88;
    int clock_x = pad + bar_w - clock_w - 8;
    return in_rect_local(x, y, clock_x, bar_y + 6, clock_w, 16);
}

int taskbar_gallery_hit(int x, int y) {
    int h = 28;
    int pad = 12;
    int bar_y = (int)g_fb.height - h - 8;
    int bar_w = (int)g_fb.width - pad * 2;
    int clock_w = 88;
    int clock_x = pad + bar_w - clock_w - 8;
    int gallery_w = 22;
    int gallery_x = clock_x - gallery_w - 8;
    return in_rect_local(x, y, gallery_x, bar_y + 6, gallery_w, 16);
}

static void taskbar_draw_clock(int x, int y) {
    ntux_time_t t;
    char clock[9];
    clock[0] = '-'; clock[1] = '-'; clock[2] = ':'; clock[3] = '-';
    clock[4] = '-'; clock[5] = ':'; clock[6] = '-'; clock[7] = '-';
    clock[8] = '\0';
    if (sys_get_time(&t) == 0) {
        clock[0] = (char)('0' + ((t.hour / 10u) % 10u));
        clock[1] = (char)('0' + (t.hour % 10u));
        clock[3] = (char)('0' + ((t.minute / 10u) % 10u));
        clock[4] = (char)('0' + (t.minute % 10u));
        clock[6] = (char)('0' + ((t.second / 10u) % 10u));
        clock[7] = (char)('0' + (t.second % 10u));
    }
    draw_text(x, y, clock, desk_theme()->accent);
}

void taskbar_draw(void) {
    const desk_theme_t* th = desk_theme();
    int h = 28;
    int pad = 12;
    int bar_y = (int)g_fb.height - h - 8;
    int bar_w = (int)g_fb.width - pad * 2;
    int clock_w = 88;
    int clock_x = pad + bar_w - clock_w - 8;
    int gallery_w = 22;
    int gallery_x = clock_x - gallery_w - 8;

    fill_round_rect(pad, bar_y, bar_w, h, 10, th->taskbar_bg);
    draw_round_rect(pad, bar_y, bar_w, h, 10, th->taskbar_border);
    fill_round_rect(pad + 8, bar_y + 6, 96, 16, 6, g_start_open ? th->accent : th->title_blur);
    draw_round_rect(pad + 8, bar_y + 6, 96, 16, 6, th->taskbar_border);
    draw_text(pad + 20, bar_y + 10, "NTux-OS", th->text_main);

    int tx = pad + 116;
    int visible = taskbar_visible_windows();
    if (visible > 0) {
        int avail = gallery_x - tx - 6;
        if (avail > 0) {
            int total_w = 0;
            for (int i = 0; i < g_window_count; ++i) {
                if (!taskbar_window_listed(&g_windows[i])) continue;
                char title_buf[48];
                taskbar_window_title(&g_windows[i], title_buf, sizeof(title_buf));
                total_w += (int)strlen(title_buf) * 8 + 12;
            }
            if (total_w <= avail) {
                int cx = tx;
                for (int i = 0; i < g_window_count; ++i) {
                    if (!taskbar_window_listed(&g_windows[i])) continue;
                    char title_buf[48];
                    taskbar_window_title(&g_windows[i], title_buf, sizeof(title_buf));
                    uint32_t c = (i == g_focus_index) ? th->accent : (g_windows[i].minimized ? th->text_dim : th->text_main);
                    int ix = cx + 2;
                    int iy = bar_y + 8;
                    if (taskbar_window_icon_w(&g_windows[i]) > 0) {
                        taskbar_draw_window_icon(&g_windows[i], ix, iy);
                        ix += 14;
                    }
                    draw_text(ix, bar_y + 10, title_buf, c);
                    cx += (int)strlen(title_buf) * 8 + 18 + taskbar_window_icon_w(&g_windows[i]);
                }
            } else {
                int cell_w = avail / visible;
                if (cell_w < 8) cell_w = 8;
                int max_chars = taskbar_title_max_chars(cell_w);
                int seen = 0;
                for (int i = 0; i < g_window_count; ++i) {
                    if (!taskbar_window_listed(&g_windows[i])) continue;
                    char title_buf[48];
                    taskbar_window_title(&g_windows[i], title_buf, sizeof(title_buf));
                    uint32_t c = (i == g_focus_index) ? th->accent : (g_windows[i].minimized ? th->text_dim : th->text_main);
                    char label[32];
                    taskbar_copy_title(label, sizeof(label), title_buf, max_chars);
                    int x = tx + seen * cell_w + 2;
                    if (taskbar_window_icon_w(&g_windows[i]) > 0) {
                        taskbar_draw_window_icon(&g_windows[i], x, bar_y + 8);
                        x += 14;
                    }
                    draw_text(x, bar_y + 10, label, c);
                    seen++;
                }
            }
        }
    }

    fill_round_rect(gallery_x, bar_y + 6, gallery_w, 16, 6, th->title_blur);
    draw_round_rect(gallery_x, bar_y + 6, gallery_w, 16, 6, th->taskbar_border);
    /* simple image icon */
    fill_rect(gallery_x + 5, bar_y + 9, 12, 10, th->accent);
    draw_rect(gallery_x + 5, bar_y + 9, 12, 10, th->taskbar_border);
    fill_rect(gallery_x + 7, bar_y + 13, 4, 3, th->taskbar_bg);
    fill_rect(gallery_x + 12, bar_y + 12, 3, 4, th->taskbar_bg);

    fill_round_rect(clock_x, bar_y + 6, clock_w, 16, 6, th->title_blur);
    draw_round_rect(clock_x, bar_y + 6, clock_w, 16, 6, th->taskbar_border);
    taskbar_draw_clock(clock_x + 12, bar_y + 10);
}

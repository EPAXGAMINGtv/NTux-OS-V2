#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syscall.h>
#include <window.h>

#define PAINT_W 1024
#define PAINT_H 768
#define CANVAS_W 1024
#define CANVAS_H 768
#define CANVAS_SCALE 1
#define TOOLBAR_H 48

#define PANEL_W 190
#define PANEL_PAD 10
#define BTN_H 24

static uint32_t* g_canvas = NULL;
static uint32_t g_palette[] = {
    0xFF0B1119u, 0xFF203245u, 0xFF3E5A7Au, 0xFF7EA6C9u,
    0xFFFFFFFFu, 0xFFE8D7C2u, 0xFFBFA480u, 0xFF7D5A3Cu,
    0xFFFF6B6Bu, 0xFFFFC857u, 0xFF56E6AAu, 0xFF48B0FFu,
    0xFF9B5DE5u, 0xFFF15BB5u, 0xFFFFF08Au, 0xFFCED4DAu
};
static int g_color_idx = 0;
static int g_brush = 2;
static int g_brush_shape = 0; /* 0=round, 1=square, 2=diamond */
static int g_tool = 0; /* 0=brush, 1=eraser, 2=picker */
static char g_status[64] = "Ready";
static char g_current_path[256] = "";
static int g_dialog_pending = 0; /* 1=open, 2=save */

static uint8_t g_key_last[128];
static int g_last_left = 0;
static int g_last_right = 0;

static int key_edge(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static int split_parent_name(const char* full, char* parent, char* name, size_t cap) {
    if (!full || full[0] != '/' || !parent || !name || cap < 4) return -1;
    const char* slash = NULL;
    for (const char* p = full; *p; ++p) {
        if (*p == '/') slash = p;
    }
    if (!slash || !slash[1]) return -1;
    size_t plen = (size_t)(slash - full);
    size_t nlen = strlen(slash + 1);
    if (plen + 1 > cap || nlen + 1 > cap) return -1;
    if (plen == 0) {
        parent[0] = '/'; parent[1] = '\0';
    } else {
        memcpy(parent, full, plen);
        parent[plen] = '\0';
    }
    memcpy(name, slash + 1, nlen + 1);
    return 0;
}

static int str_ends_with_ci(const char* s, const char* suffix) {
    if (!s || !suffix) return 0;
    size_t sl = strlen(s);
    size_t tl = strlen(suffix);
    if (sl < tl) return 0;
    for (size_t i = 0; i < tl; ++i) {
        char a = s[sl - tl + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static int str_has_ext(const char* s) {
    if (!s) return 0;
    for (const char* p = s; *p; ++p) {
        if (*p == '.') return 1;
        if (*p == '/' || *p == '\\') return 0;
    }
    return 0;
}

static void layout_canvas(int* out_x, int* out_y, int* out_w, int* out_h, int* out_panel_x) {
    int canvas_w = CANVAS_W * CANVAS_SCALE;
    int canvas_h = CANVAS_H * CANVAS_SCALE;
    int cx = 0;
    int cy = 0;
    if (out_x) *out_x = cx;
    if (out_y) *out_y = cy;
    if (out_w) *out_w = canvas_w;
    if (out_h) *out_h = canvas_h;
    if (out_panel_x) *out_panel_x = PAINT_W - PANEL_W - 12;
}

static void canvas_clear(uint32_t color) {
    if (!g_canvas) return;
    for (int i = 0; i < CANVAS_W * CANVAS_H; ++i) {
        g_canvas[i] = color;
    }
}

static void set_status(const char* s) {
    if (!s) return;
    strncpy(g_status, s, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
}

static int brush_hit(int dx, int dy, int r) {
    if (g_brush_shape == 1) {
        int ax = dx < 0 ? -dx : dx;
        int ay = dy < 0 ? -dy : dy;
        return (ax <= r && ay <= r);
    }
    if (g_brush_shape == 2) {
        int ax = dx < 0 ? -dx : dx;
        int ay = dy < 0 ? -dy : dy;
        return (ax + ay <= r + r / 2);
    }
    return (dx * dx + dy * dy <= r * r);
}

static void paint_brush(int cx, int cy, int r, uint32_t color,
                        int* dx0, int* dy0, int* dx1, int* dy1) {
    if (!g_canvas) return;
    if (r < 1) r = 1;
    int x0 = cx - r;
    int x1 = cx + r;
    int y0 = cy - r;
    int y1 = cy + r;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= CANVAS_W) x1 = CANVAS_W - 1;
    if (y1 >= CANVAS_H) y1 = CANVAS_H - 1;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            if (brush_hit(dx, dy, r)) {
                g_canvas[y * CANVAS_W + x] = color;
            }
        }
    }
    if (dx0 && dy0 && dx1 && dy1) {
        if (x0 < *dx0) *dx0 = x0;
        if (y0 < *dy0) *dy0 = y0;
        if (x1 > *dx1) *dx1 = x1;
        if (y1 > *dy1) *dy1 = y1;
    }
}

static void draw_canvas_region(window_t id, int cx0, int cy0, int cx1, int cy1) {
    int canvas_x, canvas_y;
    layout_canvas(&canvas_x, &canvas_y, NULL, NULL, NULL);
    if (!g_canvas) return;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= CANVAS_W) cx1 = CANVAS_W - 1;
    if (cy1 >= CANVAS_H) cy1 = CANVAS_H - 1;
    for (int y = cy0; y <= cy1; ++y) {
        int py = canvas_y + y * CANVAS_SCALE;
        int run_x = cx0;
        uint32_t last = g_canvas[y * CANVAS_W + cx0];
        int run = 1;
        for (int x = cx0 + 1; x <= cx1 + 1; ++x) {
            uint32_t c = (x <= cx1) ? g_canvas[y * CANVAS_W + x] : 0xFFFFFFFFu;
            if (x <= cx1 && c == last) {
                run++;
            } else {
                int px = canvas_x + run_x * CANVAS_SCALE;
                window_draw_rect(id, px, py, run * CANVAS_SCALE, CANVAS_SCALE, last, 1);
                run_x = x;
                last = c;
                run = 1;
            }
        }
    }
}

static void draw_canvas_full(window_t id) {
    draw_canvas_region(id, 0, 0, CANVAS_W - 1, CANVAS_H - 1);
}

static int save_ppm(const char* path) {
    if (!path || !path[0] || !g_canvas) return -1;
    size_t data_len = (size_t)CANVAS_W * (size_t)CANVAS_H * 3u;
    char header[64];
    int n = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", CANVAS_W, CANVAS_H);
    if (n <= 0 || n >= (int)sizeof(header)) return -1;
    size_t total = (size_t)n + data_len;
    uint8_t* buf = (uint8_t*)malloc(total);
    if (!buf) return -1;
    memcpy(buf, header, (size_t)n);
    size_t p = (size_t)n;
    for (int y = 0; y < CANVAS_H; ++y) {
        for (int x = 0; x < CANVAS_W; ++x) {
            uint32_t c = g_canvas[y * CANVAS_W + x];
            buf[p++] = (uint8_t)((c >> 16) & 0xFFu);
            buf[p++] = (uint8_t)((c >> 8) & 0xFFu);
            buf[p++] = (uint8_t)(c & 0xFFu);
        }
    }
    long rc = sys_fs_write_file(path, buf, (uint64_t)p);
    if (rc != 0) {
        char parent[256];
        char name[256];
        if (split_parent_name(path, parent, name, sizeof(parent)) == 0) {
            (void)sys_fs_create_file(parent, name, buf, (uint64_t)p);
        }
    }
    free(buf);
    return (rc == 0) ? 0 : -1;
}

static int save_bmp(const char* path) {
    if (!path || !path[0] || !g_canvas) return -1;
    const int w = CANVAS_W;
    const int h = CANVAS_H;
    const uint32_t row_stride = (uint32_t)((w * 3 + 3) & ~3);
    const uint32_t data_size = row_stride * (uint32_t)h;
    const uint32_t file_size = 54u + data_size;

    uint8_t* buf = (uint8_t*)malloc(file_size);
    if (!buf) return -1;
    memset(buf, 0, file_size);
    buf[0] = 'B'; buf[1] = 'M';
    buf[2] = (uint8_t)(file_size & 0xFFu);
    buf[3] = (uint8_t)((file_size >> 8) & 0xFFu);
    buf[4] = (uint8_t)((file_size >> 16) & 0xFFu);
    buf[5] = (uint8_t)((file_size >> 24) & 0xFFu);
    buf[10] = 54;
    buf[14] = 40;
    buf[18] = (uint8_t)(w & 0xFF);
    buf[19] = (uint8_t)((w >> 8) & 0xFF);
    buf[20] = (uint8_t)((w >> 16) & 0xFF);
    buf[21] = (uint8_t)((w >> 24) & 0xFF);
    buf[22] = (uint8_t)(h & 0xFF);
    buf[23] = (uint8_t)((h >> 8) & 0xFF);
    buf[24] = (uint8_t)((h >> 16) & 0xFF);
    buf[25] = (uint8_t)((h >> 24) & 0xFF);
    buf[26] = 1;
    buf[28] = 24;
    buf[34] = (uint8_t)(data_size & 0xFFu);
    buf[35] = (uint8_t)((data_size >> 8) & 0xFFu);
    buf[36] = (uint8_t)((data_size >> 16) & 0xFFu);
    buf[37] = (uint8_t)((data_size >> 24) & 0xFFu);

    uint8_t* out = buf + 54;
    for (int y = h - 1; y >= 0; --y) {
        uint8_t* row = out + (uint32_t)(h - 1 - y) * row_stride;
        for (int x = 0; x < w; ++x) {
            uint32_t c = g_canvas[y * w + x];
            uint8_t r = (uint8_t)((c >> 16) & 0xFFu);
            uint8_t g = (uint8_t)((c >> 8) & 0xFFu);
            uint8_t b = (uint8_t)(c & 0xFFu);
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
    }

    long rc = sys_fs_write_file(path, buf, (uint64_t)file_size);
    if (rc != 0) {
        char parent[256];
        char name[256];
        if (split_parent_name(path, parent, name, sizeof(parent)) == 0) {
            (void)sys_fs_create_file(parent, name, buf, (uint64_t)file_size);
        }
    }
    free(buf);
    return (rc == 0) ? 0 : -1;
}

static int load_bmp(const char* path) {
    uint64_t len = 0;
    uint8_t* file = 0;
    if (!path || !g_canvas) return -1;
    if (sys_fs_read_file(path, 0, 0, &len) != 0 || len < 54 || len > (32u * 1024u * 1024u)) return -1;
    file = (uint8_t*)malloc((size_t)len);
    if (!file) return -1;
    if (sys_fs_read_file(path, file, len, &len) != 0 || len < 54) { free(file); return -1; }
    if (file[0] != 'B' || file[1] != 'M') { free(file); return -1; }
    uint32_t off = (uint32_t)(file[10] | (file[11] << 8) | (file[12] << 16) | (file[13] << 24));
    uint32_t dib = (uint32_t)(file[14] | (file[15] << 8) | (file[16] << 16) | (file[17] << 24));
    int32_t w = (int32_t)(file[18] | (file[19] << 8) | (file[20] << 16) | (file[21] << 24));
    int32_t h = (int32_t)(file[22] | (file[23] << 8) | (file[24] << 16) | (file[25] << 24));
    uint16_t bpp = (uint16_t)(file[28] | (file[29] << 8));
    uint32_t compression = (uint32_t)(file[30] | (file[31] << 8) | (file[32] << 16) | (file[33] << 24));
    if (dib < 40u || w <= 0 || h == 0 || (bpp != 24u && bpp != 32u) || compression != 0u) {
        free(file);
        return -1;
    }
    uint32_t src_w = (uint32_t)w;
    uint32_t src_h = (h < 0) ? (uint32_t)(-h) : (uint32_t)h;
    uint64_t row_stride = ((((uint64_t)src_w * (uint64_t)bpp) + 31u) / 32u) * 4u;
    if ((uint64_t)off + row_stride * (uint64_t)src_h > len) { free(file); return -1; }
    int bottom_up = (h > 0);
    int* xmap = (int*)malloc((size_t)CANVAS_W * sizeof(int));
    int* ymap = (int*)malloc((size_t)CANVAS_H * sizeof(int));
    if (!xmap || !ymap) {
        if (xmap) free(xmap);
        if (ymap) free(ymap);
        free(file);
        return -1;
    }
    for (int x = 0; x < CANVAS_W; ++x) {
        uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)src_w) / (uint64_t)CANVAS_W);
        xmap[x] = (int)(sx * (uint64_t)(bpp / 8u));
    }
    for (int y = 0; y < CANVAS_H; ++y) {
        uint32_t sy = (uint32_t)(((uint64_t)y * (uint64_t)src_h) / (uint64_t)CANVAS_H);
        if (bottom_up) sy = src_h - 1u - sy;
        ymap[y] = (int)sy;
    }
    for (int y = 0; y < CANVAS_H; ++y) {
        const uint8_t* row = file + off + (uint64_t)ymap[y] * row_stride;
        for (int x = 0; x < CANVAS_W; ++x) {
            const uint8_t* px = row + xmap[x];
            uint8_t b = px[0], g = px[1], r = px[2];
            g_canvas[y * CANVAS_W + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    free(xmap);
    free(ymap);
    free(file);
    return 0;
}

static int load_ppm(const char* path) {
    if (!path || !path[0] || !g_canvas) return -1;
    uint64_t len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0 || len < 16) return -1;
    if (len > (32u * 1024u * 1024u)) return -1;
    uint8_t* buf = (uint8_t*)malloc((size_t)len + 1u);
    if (!buf) return -1;
    if (sys_fs_read_file(path, buf, len, &len) != 0) { free(buf); return -1; }
    buf[len] = 0;
    if (buf[0] != 'P' || buf[1] != '6') { free(buf); return -1; }
    int w = 0, h = 0, maxv = 0;
    size_t pos = 2;
    int fields = 0;
    while (pos < len && fields < 3) {
        while (pos < len && (buf[pos] == ' ' || buf[pos] == '\n' || buf[pos] == '\r' || buf[pos] == '\t')) pos++;
        if (pos < len && buf[pos] == '#') {
            while (pos < len && buf[pos] != '\n') pos++;
            continue;
        }
        int val = 0;
        int got = 0;
        while (pos < len && buf[pos] >= '0' && buf[pos] <= '9') {
            val = val * 10 + (buf[pos] - '0');
            pos++;
            got = 1;
        }
        if (!got) break;
        if (fields == 0) w = val;
        else if (fields == 1) h = val;
        else maxv = val;
        fields++;
    }
    if (fields < 3 || w <= 0 || h <= 0 || maxv <= 0) { free(buf); return -1; }
    if (maxv > 255) { free(buf); return -1; }
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\n' || buf[pos] == '\r' || buf[pos] == '\t')) pos++;
    size_t need = (size_t)w * (size_t)h * 3u;
    if (pos + need > len) { free(buf); return -1; }

    for (int y = 0; y < CANVAS_H; ++y) {
        for (int x = 0; x < CANVAS_W; ++x) {
            int sx = (x * w) / CANVAS_W;
            int sy = (y * h) / CANVAS_H;
            size_t off = pos + ((size_t)sy * (size_t)w + (size_t)sx) * 3u;
            uint8_t r = buf[off + 0];
            uint8_t g = buf[off + 1];
            uint8_t b = buf[off + 2];
            g_canvas[y * CANVAS_W + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }

    free(buf);
    return 0;
}

static void draw_toolbar(window_t id) {
    window_draw_rect(id, 0, 0, PAINT_W, TOOLBAR_H, 0xFF0E1520u, 1);
    window_draw_rect(id, 0, TOOLBAR_H - 2, PAINT_W, 2, 0xFF2B4056u, 1);
    window_draw_text(id, 12, 16, 0xFFEAF4FFu, "NTux Paint");

    int btn_y = 12;
    int btn_x = 180;
    window_draw_rect(id, btn_x, btn_y, 70, BTN_H, 0xFF2B4C6Bu, 1);
    window_draw_text(id, btn_x + 14, btn_y + 6, 0xFFEAF4FFu, "New");
    btn_x += 80;
    window_draw_rect(id, btn_x, btn_y, 70, BTN_H, 0xFF2B4C6Bu, 1);
    window_draw_text(id, btn_x + 12, btn_y + 6, 0xFFEAF4FFu, "Open");
    btn_x += 80;
    window_draw_rect(id, btn_x, btn_y, 70, BTN_H, 0xFF2B4C6Bu, 1);
    window_draw_text(id, btn_x + 14, btn_y + 6, 0xFFEAF4FFu, "Save");

    char status[128];
    snprintf(status, sizeof(status), "%s%s%s", g_current_path[0] ? g_current_path : "(untitled)",
             g_status[0] ? " | " : "", g_status);
    window_draw_text(id, 12, TOOLBAR_H - 14, 0xFF9BB6D0u, status);
}

static void draw_panel(window_t id) {
    int canvas_x, canvas_y, canvas_w, canvas_h, panel_x;
    layout_canvas(&canvas_x, &canvas_y, &canvas_w, &canvas_h, &panel_x);
    int panel_y = TOOLBAR_H + 12;
    int panel_h = canvas_h - panel_y - 12;
    window_draw_rect(id, panel_x, panel_y, PANEL_W, panel_h, 0xFF0E141Cu, 1);
    window_draw_rect(id, panel_x, panel_y, PANEL_W, panel_h, 0xFF233044u, 0);
    window_draw_text(id, panel_x + PANEL_PAD, panel_y + 10, 0xFFB7D6F2u, "Palette");

    int pal_cols = 4;
    int size = 28;
    int gap = 6;
    int start_x = panel_x + PANEL_PAD;
    int start_y = panel_y + 28;
    for (int i = 0; i < 16; ++i) {
        int cx = i % pal_cols;
        int cy = i / pal_cols;
        int px = start_x + cx * (size + gap);
        int py = start_y + cy * (size + gap);
        window_draw_rect(id, px, py, size, size, g_palette[i], 1);
        uint32_t border = (i == g_color_idx) ? 0xFFFFFFFFu : 0xFF334255u;
        window_draw_rect(id, px - 1, py - 1, size + 2, size + 2, border, 0);
    }

    int by = start_y + 4 * (size + gap) + 18;
    window_draw_text(id, panel_x + PANEL_PAD, by, 0xFFB7D6F2u, "Brush");
    by += 16;
    window_draw_rect(id, panel_x + PANEL_PAD, by, 28, BTN_H, 0xFF2B4C6Bu, 1);
    window_draw_text(id, panel_x + PANEL_PAD + 10, by + 6, 0xFFEAF4FFu, "-");
    window_draw_rect(id, panel_x + PANEL_PAD + 36, by, 28, BTN_H, 0xFF2B4C6Bu, 1);
    window_draw_text(id, panel_x + PANEL_PAD + 46, by + 6, 0xFFEAF4FFu, "+");
    char btxt[32];
    snprintf(btxt, sizeof(btxt), "Size: %d", g_brush);
    window_draw_text(id, panel_x + PANEL_PAD + 70, by + 6, 0xFF9BB6D0u, btxt);

    by += 40;
    window_draw_text(id, panel_x + PANEL_PAD, by, 0xFFB7D6F2u, "Shape");
    by += 16;
    window_draw_rect(id, panel_x + PANEL_PAD, by, 28, BTN_H, 0xFF2B4C6Bu, 1);
    window_draw_text(id, panel_x + PANEL_PAD + 8, by + 6, 0xFFEAF4FFu, "<");
    window_draw_rect(id, panel_x + PANEL_PAD + 36, by, 28, BTN_H, 0xFF2B4C6Bu, 1);
    window_draw_text(id, panel_x + PANEL_PAD + 46, by + 6, 0xFFEAF4FFu, ">");
    const char* shape = (g_brush_shape == 1) ? "Square" : (g_brush_shape == 2) ? "Diamond" : "Round";
    window_draw_text(id, panel_x + PANEL_PAD + 70, by + 6, 0xFF9BB6D0u, shape);

    by += 40;
    window_draw_text(id, panel_x + PANEL_PAD, by, 0xFFB7D6F2u, "Tools");
    by += 16;
    window_draw_rect(id, panel_x + PANEL_PAD, by, 56, BTN_H, g_tool == 0 ? 0xFF3A6A9Eu : 0xFF2B4C6Bu, 1);
    window_draw_text(id, panel_x + PANEL_PAD + 6, by + 6, 0xFFEAF4FFu, "Brush");
    window_draw_rect(id, panel_x + PANEL_PAD + 62, by, 56, BTN_H, g_tool == 1 ? 0xFF3A6A9Eu : 0xFF2B4C6Bu, 1);
    window_draw_text(id, panel_x + PANEL_PAD + 70, by + 6, 0xFFEAF4FFu, "Erase");
    window_draw_rect(id, panel_x + PANEL_PAD + 124, by, 56, BTN_H, g_tool == 2 ? 0xFF3A6A9Eu : 0xFF2B4C6Bu, 1);
    window_draw_text(id, panel_x + PANEL_PAD + 132, by + 6, 0xFFEAF4FFu, "Pick");

    by += 40;
    window_draw_text(id, panel_x + PANEL_PAD, by, 0xFFB7D6F2u, "Shortcuts");
    by += 16;
    window_draw_text(id, panel_x + PANEL_PAD, by, 0xFF9BB6D0u, "Ctrl+S Save");
    by += 12;
    window_draw_text(id, panel_x + PANEL_PAD, by, 0xFF9BB6D0u, "Ctrl+O Open");
    by += 12;
    window_draw_text(id, panel_x + PANEL_PAD, by, 0xFF9BB6D0u, "C Clear  E Erase  P Pick");
    by += 12;
    window_draw_text(id, panel_x + PANEL_PAD, by, 0xFF9BB6D0u, "[ ] Size  B Shape");
}

static int canvas_hit(int mx, int my, int* out_cx, int* out_cy) {
    int canvas_x, canvas_y, canvas_w, canvas_h;
    layout_canvas(&canvas_x, &canvas_y, &canvas_w, &canvas_h, NULL);
    if (mx < canvas_x || my < canvas_y || mx >= canvas_x + canvas_w || my >= canvas_y + canvas_h) return 0;
    int cx = (mx - canvas_x) / CANVAS_SCALE;
    int cy = (my - canvas_y) / CANVAS_SCALE;
    if (cx < 0 || cy < 0 || cx >= CANVAS_W || cy >= CANVAS_H) return 0;
    if (out_cx) *out_cx = cx;
    if (out_cy) *out_cy = cy;
    return 1;
}

static int hit_button(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && my >= y && mx < x + w && my < y + h;
}

static void open_picker(int save) {
    g_dialog_pending = save ? 2 : 1;
    window_open_file_picker(save ? "Save Image" : "Open Image", "/", save ? WINDOW_PICKER_SAVE : 0);
}

void ntux_user_entry(void) {
    window_t id = 0x5041494E54554Dull; /* "PAINTUM" */
    if (window_init() != 0 || window_create(id, 80, 60, PAINT_W, PAINT_H, 0xFF0B1119u, "Paint") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/paint.bmp");
    (void)window_show(id, 1);
    window_focus(id);

    g_canvas = (uint32_t*)malloc((size_t)CANVAS_W * (size_t)CANVAS_H * sizeof(uint32_t));
    if (!g_canvas) sys_exit(1);
    canvas_clear(0xFFFFFFFFu);

    int dirty_all = 1;
    int dirty_any = 0;
    int dirty_x0 = CANVAS_W, dirty_y0 = CANVAS_H, dirty_x1 = 0, dirty_y1 = 0;
    int painting = 0;
    int last_cx = 0, last_cy = 0;
    int force_redraw = 8;
    int always_full_redraw = 1;

    for (;;) {
        if (window_should_close(id)) break;
        if (key_edge(0x01)) break; /* Esc */

        window_input_state_t st;
        memset(&st, 0, sizeof(st));
        (void)window_get_input_state(id, &st);

        int mx = st.mouse_x;
        int my = st.mouse_y;
        int ldown = st.mouse_left;
        int rdown = st.mouse_right;
        int lclick = ldown && !g_last_left;

        int ctrl = (sys_kbd_is_pressed(0x1D) > 0) ? 1 : 0;
        if (ctrl && key_edge(0x1F)) { /* Ctrl+S */
            if (g_current_path[0]) {
                if (str_ends_with_ci(g_current_path, ".bmp")) {
                    if (save_bmp(g_current_path) == 0) set_status("Saved");
                    else set_status("Save failed");
                } else if (save_ppm(g_current_path) == 0) {
                    set_status("Saved");
                } else {
                    set_status("Save failed");
                }
            } else {
                open_picker(1);
            }
            dirty_all = 1;
        }
        if (ctrl && key_edge(0x18)) { /* Ctrl+O */
            open_picker(0);
        }
        if (key_edge(0x2E)) { /* C */
            canvas_clear(0xFFFFFFFFu);
            set_status("Cleared");
            dirty_all = 1;
        }
        if (key_edge(0x12)) { /* E */
            g_tool = 1;
            dirty_all = 1;
        }
        if (key_edge(0x19)) { /* P */
            g_tool = 2;
            dirty_all = 1;
        }
        if (key_edge(0x30)) { /* B */
            g_brush_shape = (g_brush_shape + 1) % 3;
            dirty_all = 1;
        }
        if (key_edge(0x1A)) { /* [ */
            if (g_brush > 1) g_brush--;
            dirty_all = 1;
        }
        if (key_edge(0x1B)) { /* ] */
            if (g_brush < 24) g_brush++;
            dirty_all = 1;
        }

        int panel_x;
        layout_canvas(NULL, NULL, NULL, NULL, &panel_x);
        if (lclick) {
            int btn_y = 12;
            if (hit_button(mx, my, 180, btn_y, 70, BTN_H)) {
                canvas_clear(0xFFFFFFFFu);
                g_current_path[0] = '\0';
                set_status("New canvas");
                dirty_all = 1;
            } else if (hit_button(mx, my, 260, btn_y, 70, BTN_H)) {
                open_picker(0);
            } else if (hit_button(mx, my, 340, btn_y, 70, BTN_H)) {
                if (g_current_path[0]) {
                    if (str_ends_with_ci(g_current_path, ".bmp")) {
                        if (save_bmp(g_current_path) == 0) set_status("Saved");
                        else set_status("Save failed");
                    } else if (save_ppm(g_current_path) == 0) {
                        set_status("Saved");
                    } else {
                        set_status("Save failed");
                    }
                } else {
                    open_picker(1);
                }
                dirty_all = 1;
            }

            int pal_cols = 4;
            int size = 28;
            int gap = 6;
            int start_x = panel_x + PANEL_PAD;
            int start_y = TOOLBAR_H + 12 + 28;
            for (int i = 0; i < 16; ++i) {
                int cx = i % pal_cols;
                int cy = i / pal_cols;
                int px = start_x + cx * (size + gap);
                int py = start_y + cy * (size + gap);
                if (hit_button(mx, my, px, py, size, size)) {
                    g_color_idx = i;
                    dirty_all = 1;
                }
            }

            int by = start_y + 4 * (size + gap) + 18 + 16;
            if (hit_button(mx, my, panel_x + PANEL_PAD, by, 28, BTN_H)) {
                if (g_brush > 1) g_brush--;
                dirty_all = 1;
            } else if (hit_button(mx, my, panel_x + PANEL_PAD + 36, by, 28, BTN_H)) {
                if (g_brush < 24) g_brush++;
                dirty_all = 1;
            }

            by += 40;
            if (hit_button(mx, my, panel_x + PANEL_PAD, by, 28, BTN_H)) {
                g_brush_shape = (g_brush_shape + 2) % 3;
                dirty_all = 1;
            } else if (hit_button(mx, my, panel_x + PANEL_PAD + 36, by, 28, BTN_H)) {
                g_brush_shape = (g_brush_shape + 1) % 3;
                dirty_all = 1;
            }

            by += 40;
            if (hit_button(mx, my, panel_x + PANEL_PAD, by, 56, BTN_H)) {
                g_tool = 0;
                dirty_all = 1;
            } else if (hit_button(mx, my, panel_x + PANEL_PAD + 62, by, 56, BTN_H)) {
                g_tool = 1;
                dirty_all = 1;
            } else if (hit_button(mx, my, panel_x + PANEL_PAD + 124, by, 56, BTN_H)) {
                g_tool = 2;
                dirty_all = 1;
            }
        }

        int cx = 0, cy = 0;
        if ((ldown || rdown) && canvas_hit(mx, my, &cx, &cy)) {
            if (g_tool == 2 && ldown) {
                uint32_t picked = g_canvas[cy * CANVAS_W + cx];
                g_palette[15] = picked;
                g_color_idx = 15;
                set_status("Picked color");
                dirty_all = 1;
            } else {
                uint32_t color = (g_tool == 1 || rdown) ? 0xFFFFFFFFu : g_palette[g_color_idx];
                if (!painting) {
                    painting = 1;
                    last_cx = cx;
                    last_cy = cy;
                }
                int x0 = last_cx;
                int y0 = last_cy;
                int x1 = cx;
                int y1 = cy;
                int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
                int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
                int sx = (x0 < x1) ? 1 : -1;
                int sy = (y0 < y1) ? 1 : -1;
                int err = dx - dy;
                int local_x0 = CANVAS_W;
                int local_y0 = CANVAS_H;
                int local_x1 = 0;
                int local_y1 = 0;
                for (;;) {
                    paint_brush(x0, y0, g_brush, color, &local_x0, &local_y0, &local_x1, &local_y1);
                    if (x0 == x1 && y0 == y1) break;
                    int e2 = err * 2;
                    if (e2 > -dy) { err -= dy; x0 += sx; }
                    if (e2 < dx) { err += dx; y0 += sy; }
                }
                if (local_x0 <= local_x1 && local_y0 <= local_y1) {
                    if (!dirty_any) {
                        dirty_x0 = local_x0;
                        dirty_y0 = local_y0;
                        dirty_x1 = local_x1;
                        dirty_y1 = local_y1;
                    } else {
                        if (local_x0 < dirty_x0) dirty_x0 = local_x0;
                        if (local_y0 < dirty_y0) dirty_y0 = local_y0;
                        if (local_x1 > dirty_x1) dirty_x1 = local_x1;
                        if (local_y1 > dirty_y1) dirty_y1 = local_y1;
                    }
                    dirty_any = 1;
                }
                last_cx = cx;
                last_cy = cy;
            }
        } else {
            painting = 0;
        }

        if (st.mouse_scroll != 0) {
            if (st.mouse_scroll > 0 && g_brush < 24) g_brush++;
            if (st.mouse_scroll < 0 && g_brush > 1) g_brush--;
            dirty_all = 1;
        }

        char dialog_path[256];
        uint32_t dialog_code = 0;
        if (window_dialog_pop(dialog_path, sizeof(dialog_path), &dialog_code) == 0) {
            if (dialog_code == 1 && dialog_path[0]) {
                if (g_dialog_pending == 1) {
                    if (str_ends_with_ci(dialog_path, ".bmp")) {
                        if (load_bmp(dialog_path) == 0) {
                            strncpy(g_current_path, dialog_path, sizeof(g_current_path) - 1);
                            g_current_path[sizeof(g_current_path) - 1] = '\0';
                            set_status("Opened");
                            dirty_all = 1;
                        } else {
                            set_status("Open failed");
                            dirty_all = 1;
                        }
                    } else if (str_ends_with_ci(dialog_path, ".ppm")) {
                        if (load_ppm(dialog_path) == 0) {
                            strncpy(g_current_path, dialog_path, sizeof(g_current_path) - 1);
                            g_current_path[sizeof(g_current_path) - 1] = '\0';
                            set_status("Opened");
                            dirty_all = 1;
                        } else {
                            set_status("Open failed");
                            dirty_all = 1;
                        }
                    } else {
                        set_status("Unsupported format");
                        dirty_all = 1;
                    }
                } else if (g_dialog_pending == 2) {
                    char save_path[256];
                    strncpy(save_path, dialog_path, sizeof(save_path) - 1);
                    save_path[sizeof(save_path) - 1] = '\0';
                    if (!str_has_ext(save_path)) {
                        strncat(save_path, ".bmp", sizeof(save_path) - strlen(save_path) - 1);
                    }
                    if (str_ends_with_ci(save_path, ".bmp")) {
                        if (save_bmp(save_path) == 0) {
                            strncpy(g_current_path, save_path, sizeof(g_current_path) - 1);
                            g_current_path[sizeof(g_current_path) - 1] = '\0';
                            set_status("Saved");
                            dirty_all = 1;
                        } else {
                            set_status("Save failed");
                            dirty_all = 1;
                        }
                    } else if (str_ends_with_ci(save_path, ".ppm")) {
                        if (save_ppm(save_path) == 0) {
                            strncpy(g_current_path, save_path, sizeof(g_current_path) - 1);
                            g_current_path[sizeof(g_current_path) - 1] = '\0';
                            set_status("Saved");
                            dirty_all = 1;
                        } else {
                            set_status("Save failed");
                            dirty_all = 1;
                        }
                    } else {
                        set_status("Unsupported format");
                        dirty_all = 1;
                    }
                }
            }
            g_dialog_pending = 0;
        }

        if (always_full_redraw) {
            dirty_all = 1;
        } else if (force_redraw > 0) {
            dirty_all = 1;
            force_redraw--;
        }
        int need_present = 0;
        if (dirty_all) {
            window_clear(id, 0xFF0B1119u);
            draw_toolbar(id);
            draw_panel(id);
            draw_canvas_full(id);
            dirty_all = 0;
            dirty_any = 0;
            need_present = 1;
        } else if (dirty_any) {
            draw_canvas_region(id, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
            dirty_any = 0;
            need_present = 1;
        }
        if (need_present) window_present(id);

        g_last_left = ldown;
        g_last_right = rdown;
        sys_wait_ticks(1);
    }

    if (g_canvas) free(g_canvas);
    sys_exit(0);
}

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syscall.h>
#include <font8x8_basic.h>

typedef struct {
    int col;
    float y;
    float speed;
    int len;
    uint8_t brightness;
} matrix_col_t;

typedef struct {
    float x;
    float y;
    float dx;
    float dy;
    int life;
    int width;
} pipe_t;

static ntux_fb_info_t g_fb;
static uint32_t* g_frame = 0;
static uint32_t* g_bg = 0;
static uint32_t* g_dim = 0;
static size_t g_pixels = 0;
static uint8_t g_prev_keys[0x80];

static uint32_t rand_next(uint32_t* state) {
    *state = (*state * 1103515245u + 12345u);
    return *state;
}

static void put_px(int x, int y, uint32_t c) {
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;
    g_frame[(uint64_t)y * (uint64_t)g_fb.width + (uint64_t)x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy) {
        int py = y + yy;
        if (py < 0 || (uint32_t)py >= g_fb.height) continue;
        for (int xx = 0; xx < w; ++xx) {
            int px = x + xx;
            if (px < 0 || (uint32_t)px >= g_fb.width) continue;
            g_frame[(uint64_t)py * (uint64_t)g_fb.width + (uint64_t)px] = c;
        }
    }
}

static void draw_char(int x, int y, char ch, uint32_t color) {
    uint8_t idx = (uint8_t)ch;
    for (int row = 0; row < 8; ++row) {
        uint8_t bits = font8x8_basic[idx][row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1u << col)) put_px(x + col, y + row, color);
        }
    }
}

static void draw_text(int x, int y, const char* s, uint32_t c) {
    int cx = x;
    for (size_t i = 0; s && s[i]; ++i) {
        if (s[i] == '\n') {
            y += 10;
            cx = x;
            continue;
        }
        draw_char(cx, y, s[i], c);
        cx += 8;
    }
}

static void draw_text_scaled(int x, int y, const char* s, int scale, uint32_t c) {
    if (!s || scale <= 0) return;
    int cx = x;
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] == '\n') {
            y += 8 * scale + 2;
            cx = x;
            continue;
        }
        for (int row = 0; row < 8; ++row) {
            uint8_t bits = font8x8_basic[(uint8_t)s[i]][row];
            for (int col = 0; col < 8; ++col) {
                if (!(bits & (1u << col))) continue;
                int px = cx + col * scale;
                int py = y + row * scale;
                fill_rect(px, py, scale, scale, c);
            }
        }
        cx += 8 * scale;
    }
}

static void draw_text_shadow(int x, int y, const char* s, uint32_t fg, uint32_t shadow) {
    draw_text(x + 1, y + 1, s, shadow);
    draw_text(x, y, s, fg);
}

static void draw_text_scaled_shadow(int x, int y, const char* s, int scale, uint32_t fg, uint32_t shadow) {
    draw_text_scaled(x + scale, y + scale, s, scale, shadow);
    draw_text_scaled(x, y, s, scale, fg);
}

static void bg_load_from_config(void) {
    (void)g_bg;
}

static void dim_background(void) {
    uint32_t w = g_fb.width;
    uint32_t h = g_fb.height;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            (void)x;
            (void)y;
            g_dim[(uint64_t)y * (uint64_t)w + (uint64_t)x] = 0x00000000u;
        }
    }
}

static int any_new_key(void) {
    uint8_t now[0x80];
    if (sys_kbd_get_state(now, sizeof(now)) != 0) return 0;
    for (uint8_t sc = 1; sc < 0x80; ++sc) {
        uint8_t cur = now[sc] ? 1u : 0u;
        if (cur && !g_prev_keys[sc]) {
            memcpy(g_prev_keys, now, sizeof(g_prev_keys));
            return 1;
        }
    }
    memcpy(g_prev_keys, now, sizeof(g_prev_keys));
    return 0;
}

void ntux_user_entry(void) {
    if (sys_fb_get_info(&g_fb) != 0 || g_fb.width == 0 || g_fb.height == 0) sys_exit(1);
    g_pixels = (size_t)g_fb.width * (size_t)g_fb.height;
    g_frame = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
    g_bg = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
    g_dim = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
    if (!g_frame || !g_bg || !g_dim) sys_exit(1);

    bg_load_from_config();
    dim_background();
    (void)sys_fs_mkdir("/", "tmp");
    (void)sys_fs_write_file("/tmp/screensaver.active", "", 0);
    memset(g_prev_keys, 0, sizeof(g_prev_keys));
    (void)sys_kbd_get_state(g_prev_keys, sizeof(g_prev_keys));

    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;
    uint64_t last = sys_get_ticks();
    float t = 0.0f;
    uint32_t phase = 0;

    uint32_t seed = (uint32_t)sys_get_ticks() ^ 0xBADC0DEu;
    const int col_w = 12;
    int cols = (int)g_fb.width / col_w;
    if (cols < 1) cols = 1;
    if (cols > 256) cols = 256;
    matrix_col_t matrix[256];
    for (int i = 0; i < cols; ++i) {
        matrix[i].col = i;
        matrix[i].y = (float)(rand_next(&seed) % (g_fb.height + 200u)) - 200.0f;
        matrix[i].speed = 20.0f + (float)(rand_next(&seed) % 60u);
        matrix[i].len = 6 + (int)(rand_next(&seed) % 16u);
        matrix[i].brightness = (uint8_t)(120u + (rand_next(&seed) % 100u));
    }

    const int pipe_max = 10;
    pipe_t pipes[10];
    int pipe_count = 0;
    int pipe_spawn_timer = 0;
    static const int dir_dx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int dir_dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};

    for (;;) {
        if (any_new_key()) break;
        uint64_t now = sys_get_ticks();
        uint64_t dt_ticks = now - last;
        if (dt_ticks > hz / 2u) dt_ticks = hz / 2u;
        float dt = (float)dt_ticks / (float)hz;
        last = now;
        t += dt;
        phase = (phase + (uint32_t)(dt_ticks * 5u)) & 255u;

        memcpy(g_frame, g_dim, g_pixels * sizeof(uint32_t));

        // Matrix rain
        for (int i = 0; i < cols; ++i) {
            matrix_col_t* m = &matrix[i];
            m->y += m->speed * dt;
            if (m->y > (float)g_fb.height + 120.0f) {
                m->y = -((float)(rand_next(&seed) % 200u));
                m->speed = 24.0f + (float)(rand_next(&seed) % 70u);
                m->len = 6 + (int)(rand_next(&seed) % 16u);
                m->brightness = (uint8_t)(110u + (rand_next(&seed) % 110u));
            }
            int head_y = (int)m->y;
            for (int k = 0; k < m->len; ++k) {
                int y = head_y - k * 12;
                if (y < -16 || y >= (int)g_fb.height) continue;
                uint8_t fade = (uint8_t)((m->brightness * (m->len - k)) / (m->len + 2));
                uint32_t col = (k == 0) ? 0xFFE7FFF1u : (0xFF000000u | ((uint32_t)fade << 8));
                char ch = (char)('A' + (rand_next(&seed) % 26u));
                draw_char(m->col * col_w, y, ch, col);
            }
        }

        // Pipes / squiggles
        if (pipe_spawn_timer <= 0 && pipe_count < pipe_max) {
            pipe_t* p = &pipes[pipe_count++];
            p->x = (float)(rand_next(&seed) % g_fb.width);
            p->y = (float)(rand_next(&seed) % g_fb.height);
            int dir = (int)(rand_next(&seed) & 7u);
            float speed = 40.0f + (float)(rand_next(&seed) % 50u);
            p->dx = (float)dir_dx[dir] * speed;
            p->dy = (float)dir_dy[dir] * speed;
            p->life = 40 + (int)(rand_next(&seed) % 60u);
            p->width = 2 + (int)(rand_next(&seed) % 3u);
            pipe_spawn_timer = 10 + (int)(rand_next(&seed) % 20u);
        }
        if (pipe_spawn_timer > 0) pipe_spawn_timer--;
        for (int i = 0; i < pipe_count; ) {
            pipe_t* p = &pipes[i];
            int px = (int)p->x;
            int py = (int)p->y;
            fill_rect(px, py, p->width, p->width, 0xFF7D7D7Du);
            p->x += p->dx * dt * 0.4f;
            p->y += p->dy * dt * 0.4f;
            if (--p->life <= 0 || p->x < -20 || p->y < -20 || p->x > (float)g_fb.width + 20.0f || p->y > (float)g_fb.height + 20.0f) {
                pipes[i] = pipes[--pipe_count];
                continue;
            }
            i++;
        }

        const char* brand = "NTux-OS";
        int scale = (g_fb.width >= 1600u) ? 5 : 4;
        int bw = (int)strlen(brand) * 8 * scale;
        int bx = (int)g_fb.width / 2 - bw / 2;
        uint32_t tri = (phase < 128u) ? phase : (255u - phase);
        int by = (int)g_fb.height / 2 - 110;
        int glow_add = (int)(tri / 2u);
        uint32_t glow = (uint32_t)(0xFF66CCFFu + (uint32_t)(glow_add << 16));
        draw_text_scaled_shadow(bx, by, brand, scale, 0xFFF1F1F1u, glow);
        // Subtle scanline flicker like omarchy.
        int scan_y = (int)((phase * 3u) % (g_fb.height ? g_fb.height : 1u));
        if (scan_y >= 0 && scan_y < (int)g_fb.height) {
            for (int x = 0; x < (int)g_fb.width; x += 2) {
                put_px(x, scan_y, 0x33111111u);
            }
        }

        ntux_time_t now_time;
        if (sys_get_time(&now_time) == 0) {
            char time_buf[16];
            snprintf(time_buf, sizeof(time_buf), "%02u:%02u", now_time.hour, now_time.minute);
            int tscale = (g_fb.width >= 1600u) ? 4 : 3;
            int tw = (int)strlen(time_buf) * 8 * tscale;
            int tx = (int)g_fb.width / 2 - tw / 2;
            int ty = by + 70 - 3;
            uint32_t time_glow = (uint32_t)(0xFF0A0E14u + (uint32_t)(glow_add << 8));
            draw_text_scaled_shadow(tx, ty, time_buf, tscale, 0xFFEAF4FFu, time_glow);
        }

        {
            const char* hint = "Press any key to return";
            int hx = (int)g_fb.width / 2 - (int)strlen(hint) * 4;
            int hy = (int)g_fb.height - 46;
            draw_text_shadow(hx, hy, hint, 0xFFBFC6D4u, 0xFF0A0E14u);
        }

        (void)sys_fb_blit32(g_frame, g_fb.width, g_fb.height, g_fb.width * 4u);
        sys_wait_ticks(1);
    }

    (void)sys_fs_remove("/tmp/screensaver.active");
    sys_exit(0);
}

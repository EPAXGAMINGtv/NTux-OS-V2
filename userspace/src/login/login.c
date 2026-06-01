#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <font8x8_basic.h>
#include <image.h>

#define LOGIN_MAX_USERS 16
#define LOGIN_TASKBAR_H 28
#define LOGIN_TITLEBAR_H 22

typedef struct {
    char name[32];
    char hash_hex[17];
    uint32_t uid;
} login_user_t;

static ntux_fb_info_t g_fb;
static uint32_t* g_frame = 0;
static uint32_t* g_bg = 0;
static size_t g_pixels = 0;
static uint8_t g_key_last[128];
static login_user_t g_users[LOGIN_MAX_USERS];
static int g_user_count = 0;
static int g_user_sel = 0;
static int g_stage = 0; /* 0=user card, 1=password input */
static char g_pass[48] = "";
static char g_status[96] = "Press Enter";
static uint64_t g_status_tick = 0;
static int g_mouse_x = 32;
static int g_mouse_y = 32;
static uint8_t g_mouse_left_last = 0;

/* Desktop theme colors */
static const uint32_t TH_TASKBAR_BG = 0xEE141518u;
static const uint32_t TH_TASKBAR_BORDER = 0xFF2A2B2Fu;
static const uint32_t TH_ACCENT = 0xFF7D7D7Du;
static const uint32_t TH_WINDOW_FILL = 0xFF15161Au;
static const uint32_t TH_TEXT_MAIN = 0xFFDADADAu;
static const uint32_t TH_TEXT_DIM = 0xFF9A9A9Au;
static const uint32_t TH_TITLE_FOCUS = 0xFF2E3035u;
static const uint32_t TH_TITLE_BLUR = 0xFF1A1B1Eu;
static const uint32_t TH_BORDER_BLUR = 0xFF1E1F22u;
static const uint32_t TH_BORDER_FOCUS = 0xFF7D7D7Du;

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

static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c);
    fill_rect(x + w - 1, y, 1, h, c);
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


static uint32_t color_lerp(uint32_t a, uint32_t b, uint32_t t255) {
    uint32_t ar = (a >> 16) & 0xFFu, ag = (a >> 8) & 0xFFu, ab = a & 0xFFu;
    uint32_t br = (b >> 16) & 0xFFu, bg = (b >> 8) & 0xFFu, bb = b & 0xFFu;
    uint32_t r = (ar * (255u - t255) + br * t255) / 255u;
    uint32_t g = (ag * (255u - t255) + bg * t255) / 255u;
    uint32_t bl = (ab * (255u - t255) + bb * t255) / 255u;
    return (r << 16) | (g << 8) | bl;
}

static void fill_round_rect(int x, int y, int w, int h, int r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        fill_rect(x, y, w, h, c);
        return;
    }
    int maxr = (w < h ? w : h) / 2;
    if (r > maxr) r = maxr;
    fill_rect(x + r, y, w - 2 * r, h, c);
    fill_rect(x, y + r, r, h - 2 * r, c);
    fill_rect(x + w - r, y + r, r, h - 2 * r, c);
    int rr = r - 1;
    int rr2 = rr * rr;
    for (int dy = 0; dy < r; ++dy) {
        for (int dx = 0; dx < r; ++dx) {
            if (dx * dx + dy * dy <= rr2) {
                put_px(x + r - 1 - dx, y + r - 1 - dy, c);
                put_px(x + w - r + dx, y + r - 1 - dy, c);
                put_px(x + r - 1 - dx, y + h - r + dy, c);
                put_px(x + w - r + dx, y + h - r + dy, c);
            }
        }
    }
}

static void draw_round_rect(int x, int y, int w, int h, int r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        draw_rect(x, y, w, h, c);
        return;
    }
    int maxr = (w < h ? w : h) / 2;
    if (r > maxr) r = maxr;
    fill_rect(x + r, y, w - 2 * r, 1, c);
    fill_rect(x + r, y + h - 1, w - 2 * r, 1, c);
    fill_rect(x, y + r, 1, h - 2 * r, c);
    fill_rect(x + w - 1, y + r, 1, h - 2 * r, c);
    int rr = r - 1;
    int rr2 = rr * rr;
    int ir = rr - 1;
    int ir2 = ir > 0 ? ir * ir : -1;
    for (int dy = 0; dy < r; ++dy) {
        for (int dx = 0; dx < r; ++dx) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= rr2 && (ir2 < 0 || d2 > ir2)) {
                put_px(x + r - 1 - dx, y + r - 1 - dy, c);
                put_px(x + w - r + dx, y + r - 1 - dy, c);
                put_px(x + r - 1 - dx, y + h - r + dy, c);
                put_px(x + w - r + dx, y + h - r + dy, c);
            }
        }
    }
}


static int str_eq_ci(const char* a, const char* b) {
    if (!a || !b) return 0;
    for (;;) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        if (ca == '\0') return 1;
    }
}

static int str_ends_with_ci(const char* s, const char* suf) {
    if (!s || !suf) return 0;
    size_t sl = strlen(s);
    size_t fl = strlen(suf);
    if (fl > sl) return 0;
    return str_eq_ci(s + (sl - fl), suf);
}

static int parse_conf_bool_true(const char* v) {
    if (!v) return 0;
    if (str_eq_ci(v, "true") || str_eq_ci(v, "1") || str_eq_ci(v, "yes") || str_eq_ci(v, "on")) return 1;
    return 0;
}

static void bg_gradient_fill(uint32_t* out) {
    uint32_t w = g_fb.width, h = g_fb.height;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t dx = (x > w / 2u) ? (x - w / 2u) : (w / 2u - x);
            uint32_t dy = (y > h / 2u) ? (y - h / 2u) : (h / 2u - y);
            uint32_t dist = dx + dy;
            uint32_t vignette = (dist * 56u) / ((w + h) ? (w + h) : 1u);
            if (vignette > 56u) vignette = 56u;
            uint8_t r = (uint8_t)(12u + (x * 28u) / (w ? w : 1u) + (y * 10u) / (h ? h : 1u));
            uint8_t g = (uint8_t)(24u + (y * 92u) / (h ? h : 1u));
            uint8_t b = (uint8_t)(40u + ((x + y) * 120u) / ((w + h) ? (w + h) : 1u));
            r = (uint8_t)((r > vignette) ? (r - vignette) : 0u);
            g = (uint8_t)((g > vignette) ? (g - vignette) : 0u);
            b = (uint8_t)((b > vignette / 2u) ? (b - vignette / 2u) : 0u);
            out[(uint64_t)y * (uint64_t)w + (uint64_t)x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

static void bg_apply_vignette(uint32_t* out) {
    uint32_t w = g_fb.width, h = g_fb.height;
    uint32_t cx = w / 2u;
    uint32_t cy = h / 2u;
    uint32_t maxd = cx + cy;
    if (maxd == 0) return;
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t dy = (y > cy) ? (y - cy) : (cy - y);
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t dx = (x > cx) ? (x - cx) : (cx - x);
            uint32_t dist = dx + dy;
            uint32_t dark = (dist * 48u) / maxd;
            uint32_t c = out[(uint64_t)y * (uint64_t)w + (uint64_t)x];
            uint8_t r = (uint8_t)((c >> 16) & 0xFFu);
            uint8_t g = (uint8_t)((c >> 8) & 0xFFu);
            uint8_t b = (uint8_t)(c & 0xFFu);
            r = (uint8_t)((r > dark) ? (r - dark) : 0u);
            g = (uint8_t)((g > dark) ? (g - dark) : 0u);
            b = (uint8_t)((b > dark) ? (b - dark) : 0u);
            out[(uint64_t)y * (uint64_t)w + (uint64_t)x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

static void draw_top_bar(void) {
    int pad = 12;
    int bar_w = (int)g_fb.width - pad * 2;
    int clock_w = 88;
    int clock_x = pad + bar_w - clock_w - 8;
    fill_round_rect(pad, 8, bar_w, LOGIN_TASKBAR_H, 10, TH_TASKBAR_BG);
    draw_round_rect(pad, 8, bar_w, LOGIN_TASKBAR_H, 10, TH_TASKBAR_BORDER);
    fill_round_rect(pad + 8, 14, 96, 16, 6, TH_TITLE_BLUR);
    draw_round_rect(pad + 8, 14, 96, 16, 6, TH_TASKBAR_BORDER);
    draw_text(pad + 20, 18, "NTux-OS", TH_TEXT_MAIN);
    fill_round_rect(clock_x, 14, clock_w, 16, 6, TH_TITLE_BLUR);
    draw_round_rect(clock_x, 14, clock_w, 16, 6, TH_TASKBAR_BORDER);
    ntux_time_t now;
    if (sys_get_time(&now) == 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", now.hour, now.minute, now.second);
        draw_text(clock_x + 12, 18, buf, TH_ACCENT);
    }
}

static void draw_window_panel(int px, int py, int pw, int ph, const char* title) {
    int r = 6;
    uint32_t sh_hi = 68u;
    uint32_t sh_mid = 82u;
    uint32_t sh_base = 26u;
    fill_round_rect(px + 7, py + 9, pw, ph, r + 3, color_lerp(0xFF000000u, TH_TASKBAR_BG, sh_hi));
    fill_round_rect(px + 4, py + 5, pw, ph, r + 2, color_lerp(0xFF000000u, TH_TASKBAR_BG, sh_mid));
    fill_round_rect(px, py, pw, ph, r, color_lerp(TH_TASKBAR_BG, 0xFF000000u, sh_base));
    fill_round_rect(px + 1, py + 1, pw - 2, ph - 2, r - 1, TH_WINDOW_FILL);
    for (int yy = 0; yy < LOGIN_TITLEBAR_H - 1; ++yy) {
        uint32_t row = color_lerp(TH_TITLE_FOCUS, 0xFF000000u, (uint32_t)(yy * 110 / LOGIN_TITLEBAR_H));
        fill_rect(px + 1, py + 1 + yy, pw - 2, 1, row);
    }
    draw_round_rect(px, py, pw, ph, r, TH_BORDER_BLUR);
    draw_text(px + 8, py + 7, title, TH_TEXT_MAIN);
    fill_round_rect(px + pw - 18, py + 6, 12, 12, 6, 0xFFF95F62u);
    fill_round_rect(px + pw - 34, py + 6, 12, 12, 6, 0xFF4EC9FFu);
    fill_round_rect(px + pw - 50, py + 6, 12, 12, 6, TH_ACCENT);
}

static void draw_avatar(int cx, int cy, int r, uint32_t c) {
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            if (x * x + y * y <= r * r) put_px(cx + x, cy + y, c);
        }
    }
}

static int bg_load_image(const char* path, uint32_t* out) {
    image_t img;
    if (!path || !out) return -1;
    if (image_decode_file_scaled(path, 3, (int)g_fb.width, (int)g_fb.height, &img) != 0) return -1;
    if (img.width <= 0 || img.height <= 0 || !img.data) {
        image_free(&img);
        return -1;
    }
    int ch = (img.channels == 4) ? 4 : 3;
    uint32_t src_w = (uint32_t)img.width;
    uint32_t src_h = (uint32_t)img.height;
    int* xmap = (int*)malloc((size_t)g_fb.width * sizeof(int));
    if (!xmap) {
        image_free(&img);
        return -1;
    }
    for (uint32_t x = 0; x < g_fb.width; ++x) {
        uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)src_w) / (uint64_t)g_fb.width);
        xmap[x] = (int)(sx * (uint32_t)ch);
    }
    for (uint32_t y = 0; y < g_fb.height; ++y) {
        if ((y & 127u) == 0u) sys_yield();
        uint32_t sy = (uint32_t)(((uint64_t)y * (uint64_t)src_h) / (uint64_t)g_fb.height);
        const uint8_t* row = img.data + (uint64_t)sy * (uint64_t)src_w * (uint64_t)ch;
        uint32_t* dst = out + (uint64_t)y * (uint64_t)g_fb.width;
        for (uint32_t x = 0; x < g_fb.width; ++x) {
            const uint8_t* px = row + xmap[x];
            uint8_t r = px[0], g = px[1], b = px[2];
            dst[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    free(xmap);
    image_free(&img);
    return 0;
}


static int path_is_image_ext(const char* path) {
    return str_ends_with_ci(path, ".bmp") || str_ends_with_ci(path, ".png") ||
           str_ends_with_ci(path, ".jpg") || str_ends_with_ci(path, ".jpeg");
}

static void bg_load_from_config(void) {
    char cfg[512];
    uint64_t ln = 0;
    bg_gradient_fill(g_bg);
    if (sys_fs_read_file("/etc/desktop.conf", cfg, sizeof(cfg) - 1u, &ln) == 0) {
        if (ln >= sizeof(cfg)) ln = sizeof(cfg) - 1u;
        cfg[ln] = '\0';
        char wallpaper[256] = "";
        int builtin_bg = 1;
        char* cur = cfg;
        while (*cur) {
            char* line = cur;
            while (*cur && *cur != '\n' && *cur != '\r') cur++;
            if (*cur == '\r') *cur++ = '\0';
            if (*cur == '\n') *cur++ = '\0';
            if (!line[0]) continue;
            if (strncmp(line, "wallpaper=", 10) == 0) {
                strncpy(wallpaper, line + 10, sizeof(wallpaper) - 1);
                wallpaper[sizeof(wallpaper) - 1] = '\0';
                continue;
            }
            if (strncmp(line, "builtin_bg=", 11) == 0) {
                builtin_bg = parse_conf_bool_true(line + 11) ? 1 : 0;
                continue;
            }
        }
        if (wallpaper[0]) {
            if (strncmp(wallpaper, "bmp:", 4) == 0) {
                if (bg_load_image(wallpaper + 4, g_bg) == 0) { bg_apply_vignette(g_bg); return; }
                return;
            }
            if (strncmp(wallpaper, "img:", 4) == 0) {
                if (bg_load_image(wallpaper + 4, g_bg) == 0) { bg_apply_vignette(g_bg); return; }
                return;
            }
            if (strcmp(wallpaper, "gradient") == 0) { bg_apply_vignette(g_bg); return; }
            if (path_is_image_ext(wallpaper)) {
                if (bg_load_image(wallpaper, g_bg) == 0) { bg_apply_vignette(g_bg); return; }
                return;
            }
            if (!builtin_bg) return;
        }
    }
    if (sys_fs_read_file("/home/.ntux/wallpaper.cfg", cfg, sizeof(cfg) - 1u, &ln) != 0) return;
    if (ln >= sizeof(cfg)) ln = sizeof(cfg) - 1u;
    cfg[ln] = '\0';
    if (strncmp(cfg, "bmp:", 4) == 0) {
        if (bg_load_image(cfg + 4, g_bg) == 0) { bg_apply_vignette(g_bg); return; }
    } else if (strncmp(cfg, "img:", 4) == 0) {
        if (bg_load_image(cfg + 4, g_bg) == 0) { bg_apply_vignette(g_bg); return; }
    } else if (path_is_image_ext(cfg)) {
        if (bg_load_image(cfg, g_bg) == 0) { bg_apply_vignette(g_bg); return; }
    }
    bg_apply_vignette(g_bg);
}

static int poll_special_press(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static uint64_t fnv1a64_seed(const char* a, const char* b) {
    uint64_t h = 1469598103934665603ull;
    if (a) for (size_t i = 0; a[i]; ++i) { h ^= (uint8_t)a[i]; h *= 1099511628211ull; }
    h ^= (uint8_t)':'; h *= 1099511628211ull;
    if (b) for (size_t i = 0; b[i]; ++i) { h ^= (uint8_t)b[i]; h *= 1099511628211ull; }
    return h;
}

static void hash_to_hex(uint64_t h, char out[17]) {
    static const char* d = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        out[15 - i] = d[(int)(h & 0xFu)];
        h >>= 4u;
    }
    out[16] = '\0';
}

static int split_parent_name(const char* full, char* parent, char* name, size_t cap) {
    const char* slash = 0;
    if (!full || full[0] != '/' || !parent || !name || cap < 2) return -1;
    for (const char* p = full; *p; ++p) if (*p == '/') slash = p;
    if (!slash || !slash[1]) return -1;
    size_t plen = (size_t)(slash - full);
    size_t nlen = strlen(slash + 1);
    if (nlen == 0 || nlen >= cap) return -1;
    if (plen == 0) { parent[0] = '/'; parent[1] = '\0'; }
    else { if (plen >= cap) return -1; memcpy(parent, full, plen); parent[plen] = '\0'; }
    memcpy(name, slash + 1, nlen + 1u);
    return 0;
}

static int users_save_db(void) {
    char path[64], parent[64], name[64], buf[1024];
    size_t p = 0;
    if (sys_fs_exists("/home") <= 0) return -1;
    (void)sys_fs_mkdir("/home", ".ntux");
    for (int i = 0; i < g_user_count; ++i) {
        int n = snprintf(buf + p, sizeof(buf) - p, "%s:%s:%u\n", g_users[i].name, g_users[i].hash_hex, g_users[i].uid);
        if (n <= 0 || p + (size_t)n >= sizeof(buf)) break;
        p += (size_t)n;
    }
    snprintf(path, sizeof(path), "/home/.ntux/users.db");
    if (sys_fs_exists(path) > 0) return sys_fs_write_file(path, buf, p) == 0 ? 0 : -1;
    if (split_parent_name(path, parent, name, sizeof(parent)) != 0) return -1;
    return sys_fs_create_file(parent, name, buf, p) == 0 ? 0 : -1;
}

static void add_default_user(void) {
    char hex[17];
    g_user_count = 1;
    strncpy(g_users[0].name, "liveuser", sizeof(g_users[0].name) - 1);
    hash_to_hex(fnv1a64_seed("liveuser", "1234"), hex);
    strncpy(g_users[0].hash_hex, hex, sizeof(g_users[0].hash_hex) - 1);
    g_users[0].uid = 1000u;
}

static int users_load_db(void) {
    char buf[1024];
    uint64_t len = 0;
    g_user_count = 0;
    if (sys_fs_exists("/home") <= 0) {
        add_default_user();
        return -1;
    }
    (void)sys_fs_mkdir("/home", ".ntux");
    if (sys_fs_read_file("/home/.ntux/users.db", buf, sizeof(buf) - 1u, &len) != 0) {
        add_default_user();
        (void)users_save_db();
        return 0;
    }
    if (len >= sizeof(buf)) len = sizeof(buf) - 1u;
    buf[len] = '\0';
    char* cur = buf;
    int needs_migrate = 0;
    while (*cur && g_user_count < LOGIN_MAX_USERS) {
        char* line = cur;
        while (*cur && *cur != '\n') cur++;
        if (*cur == '\n') *cur++ = '\0';
        if (!line[0]) continue;
        char* c1 = line; while (*c1 && *c1 != ':') c1++;
        if (!*c1) continue;
        *c1++ = '\0';
        char* c2 = c1; while (*c2 && *c2 != ':') c2++;
        if (!*c2) continue;
        *c2++ = '\0';
        strncpy(g_users[g_user_count].name, line, sizeof(g_users[g_user_count].name) - 1);
        g_users[g_user_count].name[sizeof(g_users[g_user_count].name) - 1] = '\0';
        g_users[g_user_count].uid = (uint32_t)atoi(c2);
        if (g_users[g_user_count].uid == 0) g_users[g_user_count].uid = 1000u + (uint32_t)g_user_count;
        int hex_ok = ((int)strlen(c1) == 16);
        if (hex_ok) {
            for (int i = 0; i < 16; ++i) {
                char ch = c1[i];
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) { hex_ok = 0; break; }
            }
        }
        if (hex_ok) {
            strncpy(g_users[g_user_count].hash_hex, c1, sizeof(g_users[g_user_count].hash_hex) - 1);
        } else {
            char hex[17];
            hash_to_hex(fnv1a64_seed(g_users[g_user_count].name, c1), hex);
            strncpy(g_users[g_user_count].hash_hex, hex, sizeof(g_users[g_user_count].hash_hex) - 1);
            needs_migrate = 1;
        }
        g_users[g_user_count].hash_hex[16] = '\0';
        g_user_count++;
    }
    if (g_user_count == 0) add_default_user();
    if (needs_migrate) (void)users_save_db();
    return 0;
}

static int auth_current_user(void) {
    char hex[17];
    if (g_user_sel < 0 || g_user_sel >= g_user_count) return -1;
    hash_to_hex(fnv1a64_seed(g_users[g_user_sel].name, g_pass), hex);
    return (strcmp(hex, g_users[g_user_sel].hash_hex) == 0) ? 0 : -1;
}

static int consume_text_key(void) {
    long v = sys_getchar();
    if (v < 0 || v > 255) return -1;
    return (int)(unsigned char)v;
}

static void mouse_poll(int* clicked) {
    ntux_mouse_state_t ms;
    if (sys_mouse_get_state(&ms) != 0) {
        if (clicked) *clicked = 0;
        return;
    }
    g_mouse_x = ms.x;
    g_mouse_y = ms.y;
    if (g_mouse_x < 0) g_mouse_x = 0;
    if (g_mouse_y < 0) g_mouse_y = 0;
    if (g_mouse_x >= (int)g_fb.width) g_mouse_x = (int)g_fb.width - 1;
    if (g_mouse_y >= (int)g_fb.height) g_mouse_y = (int)g_fb.height - 1;
    if (clicked) *clicked = (ms.left && !g_mouse_left_last) ? 1 : 0;
    g_mouse_left_last = ms.left;
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void draw_cursor(void) {
    int x = g_mouse_x;
    int y = g_mouse_y;
    int glow = (int)(sys_get_ticks() % 2u);
    static const uint16_t arrow_mask[16] = {
        0x0001, 0x0003, 0x0007, 0x000F,
        0x001F, 0x003F, 0x007F, 0x00FF,
        0x01FF, 0x003F, 0x003F, 0x0070,
        0x0070, 0x00E0, 0x00E0, 0x0000
    };
    uint32_t c = glow ? 0xFFEAF4FFu : 0xFFF2F2F2u;
    uint32_t s = 0xFF121212u;
    for (int row = 0; row < 16; ++row) {
        uint16_t bits = arrow_mask[row];
        for (int col = 0; col < 16; ++col) {
            if (bits & (1u << col)) {
                put_px(x + col + 1, y + row + 1, s);
                put_px(x + col, y + row, c);
            }
        }
    }
}

static void draw_user_stage(void) {
    int pw = 520, ph = 300;
    int px = (int)g_fb.width / 2 - pw / 2;
    int py = (int)g_fb.height / 2 - ph / 2 + 20;
    draw_window_panel(px, py, pw, ph, "Sign in");
    draw_avatar(px + pw / 2, py + 80, 48, TH_ACCENT);
    int nx = px + pw / 2 - (int)strlen(g_users[g_user_sel].name) * 4;
    draw_text(nx, py + 136, g_users[g_user_sel].name, TH_TEXT_MAIN);
    if (g_user_count > 1) {
        draw_text(px + pw / 2 - 120, py + 162, "\x1B  Switch user  \x1A", TH_TEXT_DIM);
    }
    draw_text(px + pw / 2 - 60, py + 192, "Click or press Enter", TH_TEXT_DIM);
    fill_round_rect(px + pw / 2 - 52, py + 225, 104, 28, 6, TH_ACCENT);
    draw_round_rect(px + pw / 2 - 52, py + 225, 104, 28, 6, TH_BORDER_FOCUS);
    draw_text(px + pw / 2 - 28, py + 233, "Unlock", 0xFF091018u);
}

static void draw_pass_stage(void) {
    int pw = 520, ph = 340;
    int px = (int)g_fb.width / 2 - pw / 2;
    int py = (int)g_fb.height / 2 - ph / 2 + 20;
    char mask[64];
    size_t n = strlen(g_pass);
    if (n >= sizeof(mask)) n = sizeof(mask) - 1;
    for (size_t i = 0; i < n; ++i) mask[i] = '*';
    mask[n] = '\0';
    draw_window_panel(px, py, pw, ph, "Enter password");
    draw_avatar(px + pw / 2, py + 70, 40, TH_ACCENT);
    int nx = px + pw / 2 - (int)strlen(g_users[g_user_sel].name) * 4;
    draw_text(nx, py + 120, g_users[g_user_sel].name, TH_TEXT_MAIN);
    draw_text(px + 36, py + 156, "Password", TH_TEXT_DIM);
    fill_round_rect(px + 36, py + 170, pw - 72, 34, 6, 0xFF0B0C0Fu);
    draw_round_rect(px + 36, py + 170, pw - 72, 34, 6, TH_BORDER_BLUR);
    draw_text(px + 48, py + 182, mask[0] ? mask : "<type password>", TH_TEXT_MAIN);
    fill_round_rect(px + pw - 168, py + 222, 120, 28, 6, TH_ACCENT);
    draw_round_rect(px + pw - 168, py + 222, 120, 28, 6, TH_BORDER_FOCUS);
    draw_text(px + pw - 142, py + 230, "Unlock", 0xFF091018u);
    if (sys_get_ticks() - g_status_tick < 240u) {
        fill_round_rect(px + 32, py + 268, pw - 64, 22, 6, color_lerp(0xFF000000u, TH_TITLE_BLUR, 120u));
        draw_text(px + 40, py + 274, g_status, 0xFFFFB36Bu);
    }
    draw_text(px + 36, py + 306, "Esc to go back", TH_TEXT_DIM);
}

static long start_desktop_task(void) {
    long rc = sys_task_add_module("desktop");
    if (rc >= 0) return 0;
    rc = sys_task_add("/boot/modules/desktop.elf");
    if (rc >= 0) return 0;
    return -1;
}

void ntux_user_entry(void) {
    if (sys_fb_get_info(&g_fb) != 0 || g_fb.width == 0 || g_fb.height == 0) sys_exit(1);
    g_pixels = (size_t)g_fb.width * (size_t)g_fb.height;
    g_frame = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
    g_bg = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
    if (!g_frame || !g_bg) sys_exit(1);

    (void)users_load_db();
    g_user_sel = 0;
    bg_load_from_config();

    for (;;) {
        int clicked = 0;
        mouse_poll(&clicked);
        memcpy(g_frame, g_bg, g_pixels * sizeof(uint32_t));
        draw_top_bar();

        if (g_stage == 0) {
            int pw = 520, ph = 300;
            int px = (int)g_fb.width / 2 - pw / 2;
            int py = (int)g_fb.height / 2 - ph / 2 + 20;
            if (clicked && in_rect(g_mouse_x, g_mouse_y, px, py, pw, ph)) {
                g_stage = 1;
                g_pass[0] = '\0';
                strncpy(g_status, "Enter password", sizeof(g_status) - 1);
                g_status_tick = sys_get_ticks();
            }
            draw_user_stage();
        } else {
            int pw = 520, ph = 340;
            int px = (int)g_fb.width / 2 - pw / 2;
            int py = (int)g_fb.height / 2 - ph / 2 + 20;
            if (clicked && !in_rect(g_mouse_x, g_mouse_y, px, py, pw, ph)) {
                g_stage = 0;
                g_pass[0] = '\0';
                memset(g_key_last, 0, sizeof(g_key_last));
            }
            draw_pass_stage();
        }
        draw_cursor();
        (void)sys_fb_blit32(g_frame, g_fb.width, g_fb.height, g_fb.width * 4u);

        if (g_stage == 0) {
            int ch = consume_text_key();
            if (poll_special_press(0x4B) && g_user_count > 1) {
                g_user_sel = (g_user_sel - 1 + g_user_count) % g_user_count;
            }
            if (poll_special_press(0x4D) && g_user_count > 1) {
                g_user_sel = (g_user_sel + 1) % g_user_count;
            }
            if (poll_special_press(0x1C) || ch == '\n' || ch == '\r') {
                g_stage = 1;
                g_pass[0] = '\0';
                memset(g_key_last, 0, sizeof(g_key_last));
                strncpy(g_status, "Enter password", sizeof(g_status) - 1);
                g_status_tick = sys_get_ticks();
            }
        } else {
            int ch = consume_text_key();
            size_t len = strlen(g_pass);
            if ((ch >= 32 && ch < 127) && len + 1 < sizeof(g_pass)) {
                g_pass[len] = (char)ch;
                g_pass[len + 1] = '\0';
                len++;
            }
            if ((poll_special_press(0x0E) || ch == 8 || ch == 127) && len > 0) g_pass[len - 1] = '\0';
            if (poll_special_press(0x01) || ch == 27) {
                g_stage = 0;
                g_pass[0] = '\0';
                memset(g_key_last, 0, sizeof(g_key_last));
            }
            if (poll_special_press(0x1C) || ch == '\n' || ch == '\r') {
                strncpy(g_status, "Authenticating...", sizeof(g_status) - 1);
                g_status[sizeof(g_status) - 1] = '\0';
                g_status_tick = sys_get_ticks();
                if (auth_current_user() == 0) {
                    (void)sys_set_uid(g_users[g_user_sel].uid);
                    (void)sys_fs_mkdir("/home", ".ntux");
                    (void)sys_fs_write_file("/home/.ntux/session_user", g_users[g_user_sel].name,
                                            (uint64_t)strlen(g_users[g_user_sel].name));
                    long rc = start_desktop_task();
                    if (rc == 0) {
                        for (int i = 0; i < 8; ++i) (void)sys_yield();
                        sys_exit(0);
                    }
                    strncpy(g_status, "Desktop start failed", sizeof(g_status) - 1);
                    g_status[sizeof(g_status) - 1] = '\0';
                    g_status_tick = sys_get_ticks();
                } else {
                    strncpy(g_status, "Wrong password", sizeof(g_status) - 1);
                    g_status[sizeof(g_status) - 1] = '\0';
                    g_status_tick = sys_get_ticks();
                    g_pass[0] = '\0';
                }
            }
        }
        (void)sys_yield();
    }
}

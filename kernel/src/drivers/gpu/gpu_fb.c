#include "gpu_fb.h"
#include <data/fonts/font8x8/font8x8_basic.h>
#include "gpu.h"
#include <lib/string.h>
#include <stdint.h>

extern char font8x8_control[32][8];
extern char font8x8_ext_latin[96][8];

static volatile struct limine_framebuffer* fb_priv = NULL;

static const uint8_t* glyph_for_codepoint(uint32_t cp) {
    if (cp < 0x20u)
        return (const uint8_t*)font8x8_control[cp];
    if (cp < 0x80u)
        return (const uint8_t*)font8x8_basic[cp];
    if (cp >= 0xA0u && cp <= 0xFFu)
        return (const uint8_t*)font8x8_ext_latin[cp - 0xA0u];
    return (const uint8_t*)font8x8_basic[(int)'?'];
}

static void fb_put_pixel(int x, int y, uint32_t color) {
    if (!fb_priv) return;
    if (x < 0 || y < 0) return;
    if ((uint64_t)x >= fb_priv->width || (uint64_t)y >= fb_priv->height) return;
    uint8_t* pixel = (uint8_t*)fb_priv->address + y * fb_priv->pitch + x * (fb_priv->bpp / 8);
    *(uint32_t*)pixel = color;
}

static void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            fb_put_pixel(x + xx, y + yy, color);
}

static void fb_clear_screen(uint32_t color) {
    if (!fb_priv) return;
    for (uint64_t y = 0; y < fb_priv->height; y++)
        for (uint64_t x = 0; x < fb_priv->width; x++)
            fb_put_pixel((int)x, (int)y, color);
}

static void fb_draw_char(int x, int y, char c, uint32_t color) {
    const uint8_t* glyph = glyph_for_codepoint((uint8_t)c);
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col))
                fb_put_pixel(x + col, y + row, color);
        }
    }
}

static void fb_draw_text(int x, int y, const char* str, uint32_t color) {
    int cursor_x = x;
    while (*str) {
        if (*str == '\n') {
            cursor_x = x;
            y += 8;
            str++;
            continue;
        }
        uint32_t cp = 0;
        unsigned char c0 = (unsigned char)str[0];
        if (c0 < 0x80u) {
            cp = c0;
            str += 1;
        } else if ((c0 & 0xE0u) == 0xC0u && (str[1] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(str[1] & 0x3Fu);
            str += 2;
        } else if ((c0 & 0xF0u) == 0xE0u && (str[1] & 0xC0u) == 0x80u && (str[2] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x0Fu) << 12) | ((uint32_t)(str[1] & 0x3Fu) << 6) | (uint32_t)(str[2] & 0x3Fu);
            str += 3;
        } else {
            cp = (uint32_t)'?';
            str += 1;
        }
        const uint8_t* glyph = glyph_for_codepoint(cp);
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1 << col))
                    fb_put_pixel(cursor_x + col, y + row, color);
            }
        }
        cursor_x += 8;
    }
}

static void fb_scroll(int x, int y, int w, int h, int lines) {
    (void)w; (void)h;
    if (!fb_priv || lines <= 0) return;
    int row_pixels = lines * 8;
    int fb_w = (int)fb_priv->width;
    int fb_h = (int)fb_priv->height;
    for (int yy = y; yy < fb_h - row_pixels; yy++) {
        for (int xx = x; xx < fb_w; xx++) {
            uint32_t* dst = (uint32_t*)((uintptr_t)fb_priv->address + yy * fb_priv->pitch + xx * (fb_priv->bpp / 8));
            uint32_t* src = (uint32_t*)((uintptr_t)fb_priv->address + (yy + row_pixels) * fb_priv->pitch + xx * (fb_priv->bpp / 8));
            *dst = *src;
        }
    }
    for (int yy = fb_h - row_pixels; yy < fb_h; yy++)
        for (int xx = x; xx < fb_w; xx++)
            fb_put_pixel(xx, yy, COLOR_BLACK);
}

static void fb_blit(const void* src, int src_w, int src_h, int dst_x, int dst_y) {
    uint64_t* data = (uint64_t*)src;
    for (int y = 0; y < src_h; y++) {
        for (int x = 0; x < src_w; x++) {
            if (dst_x + x < 0 || dst_y + y < 0) continue;
            if (!fb_priv) continue;
            if ((uint64_t)(dst_x + x) >= fb_priv->width || (uint64_t)(dst_y + y) >= fb_priv->height) continue;
            uint32_t color = (uint32_t)(data[y * src_w + x] & 0xFFFFFFFF);
            fb_put_pixel(dst_x + x, dst_y + y, color);
        }
    }
}

static void* fb_get_address(void) {
    return fb_priv ? (void*)fb_priv->address : NULL;
}

static uint32_t fb_get_width(void) {
    return fb_priv ? (uint32_t)fb_priv->width : 0;
}

static uint32_t fb_get_height(void) {
    return fb_priv ? (uint32_t)fb_priv->height : 0;
}

static uint32_t fb_get_bpp(void) {
    return fb_priv ? (uint32_t)fb_priv->bpp : 0;
}

static uint32_t fb_get_pitch(void) {
    return fb_priv ? fb_priv->pitch : 0;
}

static int fb_init(void* context) {
    (void)context;
    return 0;
}

static void fb_deinit(void) {
}

static void fb_flush(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
}

gpu_driver_t gpu_fb_driver = {
    .name = "framebuffer",
    .is_accelerated = false,
    .init = fb_init,
    .deinit = fb_deinit,
    .put_pixel = fb_put_pixel,
    .fill_rect = fb_fill_rect,
    .clear_screen = fb_clear_screen,
    .draw_char = fb_draw_char,
    .draw_text = fb_draw_text,
    .scroll = fb_scroll,
    .blit = fb_blit,
    .flush = fb_flush,
    .get_address = fb_get_address,
    .get_width = fb_get_width,
    .get_height = fb_get_height,
    .get_bpp = fb_get_bpp,
    .get_pitch = fb_get_pitch,
};

int gpu_fb_init(volatile struct limine_framebuffer* fb) {
    if (!fb) return -1;
    fb_priv = fb;
    return 0;
}

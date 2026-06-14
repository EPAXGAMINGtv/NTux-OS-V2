#ifndef NTUX_TTF_H
#define NTUX_TTF_H

#include <stdint.h>
#include <stddef.h>

#define TTF_GLYPH_CACHE 256

typedef struct ttf_font ttf_font_t;

typedef struct {
    int width;
    int height;
    int advance_x;
    int lsb;
    int bearing_y;
    uint8_t* bitmap;
} ttf_glyph_t;

ttf_font_t* ttf_load(const void* data, size_t size);
void ttf_free(ttf_font_t* font);
int ttf_glyph_index(ttf_font_t* font, uint32_t codepoint);
ttf_glyph_t* ttf_render_glyph(ttf_font_t* font, uint32_t codepoint, int px_size);
void ttf_free_glyph(ttf_glyph_t* g);
int ttf_advance_x(ttf_font_t* font, uint32_t codepoint, int px_size);
int ttf_get_height(ttf_font_t* font, int px_size);
int ttf_get_ascender(ttf_font_t* font, int px_size);

#endif

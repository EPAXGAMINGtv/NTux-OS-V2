#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "gpu.h"

int graphics_init(void);
gpu_driver_t* graphics_get_driver(void);
bool graphics_has_acceleration(void);

void gpu_draw_scaled_char(int x, int y, char c, uint32_t color, int scale);
void gpu_draw_scaled_text(int x, int y, const char* str, uint32_t color, int scale);

static inline void gpu_put_pixel(int x, int y, uint32_t color) {
    gpu_driver_t* d = graphics_get_driver();
    if (d && d->put_pixel) d->put_pixel(x, y, color);
}

static inline void gpu_fill_rect(int x, int y, int w, int h, uint32_t color) {
    gpu_driver_t* d = graphics_get_driver();
    if (d && d->fill_rect) d->fill_rect(x, y, w, h, color);
}

static inline void gpu_clear_screen(uint32_t color) {
    gpu_driver_t* d = graphics_get_driver();
    if (d && d->clear_screen) d->clear_screen(color);
}

static inline void gpu_draw_char(int x, int y, char c, uint32_t color) {
    gpu_driver_t* d = graphics_get_driver();
    if (d && d->draw_char) d->draw_char(x, y, c, color);
}

static inline void gpu_draw_text(int x, int y, const char* str, uint32_t color) {
    gpu_driver_t* d = graphics_get_driver();
    if (d && d->draw_text) d->draw_text(x, y, str, color);
}

static inline void gpu_scroll(int x, int y, int w, int h, int lines) {
    gpu_driver_t* d = graphics_get_driver();
    if (d && d->scroll) d->scroll(x, y, w, h, lines);
}

static inline void gpu_blit(const void* src, int src_w, int src_h, int dst_x, int dst_y) {
    gpu_driver_t* d = graphics_get_driver();
    if (d && d->blit) d->blit(src, src_w, src_h, dst_x, dst_y);
}

static inline void gpu_flush(int x, int y, int w, int h) {
    gpu_driver_t* d = graphics_get_driver();
    if (d && d->flush) d->flush(x, y, w, h);
}

static inline void gpu_flush_all(void) {
    gpu_driver_t* d = graphics_get_driver();
    if (d && d->flush) d->flush(0, 0, (int)d->get_width(), (int)d->get_height());
}

static inline void* gpu_get_address(void) {
    gpu_driver_t* d = graphics_get_driver();
    return (d && d->get_address) ? d->get_address() : NULL;
}

static inline uint32_t gpu_get_width(void) {
    gpu_driver_t* d = graphics_get_driver();
    return (d && d->get_width) ? d->get_width() : 0;
}

static inline uint32_t gpu_get_height(void) {
    gpu_driver_t* d = graphics_get_driver();
    return (d && d->get_height) ? d->get_height() : 0;
}

static inline uint32_t gpu_get_bpp(void) {
    gpu_driver_t* d = graphics_get_driver();
    return (d && d->get_bpp) ? d->get_bpp() : 0;
}

static inline uint32_t gpu_get_pitch(void) {
    gpu_driver_t* d = graphics_get_driver();
    return (d && d->get_pitch) ? d->get_pitch() : 0;
}

#endif

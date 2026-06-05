#ifndef GPU_H
#define GPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <drivers/pci/pci.h>

#define COLOR_BLACK       0xFF000000
#define COLOR_WHITE       0xFFFFFFFF
#define COLOR_RED         0xFFFF0000
#define COLOR_GREEN       0xFF00FF00
#define COLOR_BLUE        0xFF0000FF
#define COLOR_YELLOW      0xFFFFFF00
#define COLOR_CYAN        0xFF00FFFF
#define COLOR_MAGENTA     0xFFFF00FF
#define COLOR_GRAY        0xFF808080
#define COLOR_LIGHT_GRAY  0xFFC0C0C0
#define COLOR_DARK_GRAY   0xFF404040
#define COLOR_ORANGE      0xFFFFA500
#define COLOR_PINK        0xFFFFC0CB
#define COLOR_PURPLE      0xFF800080
#define COLOR_BROWN       0xFFA52A2A
#define COLOR_LIGHT_BLUE  0xFFADD8E6
#define COLOR_LIGHT_GREEN 0xFF90EE90
#define COLOR_LIGHT_BLUE_SCREEN_BG 0xFF1E8CB0
#define COLOR_BLUE_SCREEN_BG       0xFF000080
#define COLOR_GOLD        0xFFFFD700
#define COLOR_SILVER      0xFFC0C0C0
#define COLOR_TEAL        0xFF008080
#define COLOR_NAVY        0xFF000080
#define COLOR_LIME        0xFF00FF00
#define COLOR_OLIVE       0xFF808000
#define COLOR_MAROON      0xFF800000
#define COLOR_DARK_RED        0xFF800000
#define COLOR_DARK_GREEN      0xFF006400
#define COLOR_DARK_BLUE       0xFF00008B
#define COLOR_DARK_YELLOW     0xFF9B870C
#define COLOR_DARK_CYAN       0xFF008B8B
#define COLOR_DARK_MAGENTA    0xFF8B008B
#define COLOR_DARK_ORANGE     0xFFFF8C00
#define COLOR_DARK_PINK       0xFFCC3366
#define COLOR_DARK_PURPLE     0xFF4B0082
#define COLOR_DARK_BROWN      0xFF5C4033
#define COLOR_LIGHT_RED       0xFFFF6666
#define COLOR_LIGHT_MAGENTA   0xFFFF99FF
#define COLOR_LIGHT_ORANGE    0xFFFFB266
#define COLOR_LIGHT_PINK      0xFFFFB6C1
#define COLOR_LIGHT_PURPLE    0xFFB19CD9
#define COLOR_LIGHT_YELLOW    0xFFFFFF99
#define COLOR_LIGHT_CYAN      0xFF99FFFF
#define COLOR_LIGHT_BROWN     0xFFCD853F

typedef struct gpu_driver {
    const char* name;
    bool is_accelerated;

    int (*init)(void* context);
    void (*deinit)(void);

    void (*put_pixel)(int x, int y, uint32_t color);
    void (*fill_rect)(int x, int y, int w, int h, uint32_t color);
    void (*clear_screen)(uint32_t color);
    void (*draw_char)(int x, int y, char c, uint32_t color);
    void (*draw_text)(int x, int y, const char* str, uint32_t color);
    void (*scroll)(int x, int y, int w, int h, int lines);
    void (*blit)(const void* src, int src_w, int src_h, int dst_x, int dst_y);
    void (*flush)(int x, int y, int w, int h);

    void* (*get_address)(void);
    uint32_t (*get_width)(void);
    uint32_t (*get_height)(void);
    uint32_t (*get_bpp)(void);
    uint32_t (*get_pitch)(void);
} gpu_driver_t;

#endif

#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>
#include <drivers/gpu/gpu.h>

typedef struct {
    int x, y;
    int char_width, char_height;
    int screen_width, screen_height;
} cursor_t;

void init_cursor(cursor_t* cursor, int screen_width, int screen_height);
void put_char_with_cursor(cursor_t* cursor, char c, uint32_t color);
void render_cursor(cursor_t* cursor, uint32_t color);
void clear_cursor(cursor_t* cursor);

#endif

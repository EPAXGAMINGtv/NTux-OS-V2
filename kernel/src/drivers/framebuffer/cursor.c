#include "cursor.h"
#include <drivers/gpu/graphics.h>
#include <stdint.h>
#include <stddef.h>

void init_cursor(cursor_t* cursor, int screen_width, int screen_height) {
    cursor->x = 0;
    cursor->y = 0;
    cursor->char_width = 8;
    cursor->char_height = 8;
    cursor->screen_width = screen_width;
    cursor->screen_height = screen_height;
}

void put_char_with_cursor(cursor_t* cursor, char c, uint32_t color) {
    gpu_draw_char(cursor->x, cursor->y, c, color);

    cursor->x += cursor->char_width;

    if (cursor->x + cursor->char_width > cursor->screen_width) {
        cursor->x = 0;
        cursor->y += cursor->char_height;
    }

    if (cursor->y + cursor->char_height > cursor->screen_height) {
        int row_pixels = cursor->char_height;
        gpu_scroll(0, 0, cursor->screen_width, cursor->screen_height, 1);
        cursor->y -= row_pixels;
    }
}

void render_cursor(cursor_t* cursor, uint32_t color) {
    gpu_fill_rect(cursor->x, cursor->y, cursor->char_width, cursor->char_height, color);
}

void clear_cursor(cursor_t* cursor) {
    gpu_fill_rect(cursor->x, cursor->y, cursor->char_width, cursor->char_height, COLOR_BLACK);
}

#ifndef NTUX_DESKTOP_CURSOR_H
#define NTUX_DESKTOP_CURSOR_H

typedef enum {
    CURSOR_ARROW = 0,
    CURSOR_HAND = 1,
    CURSOR_RESIZE_NWSE = 2,
    CURSOR_MOVE = 3
} cursor_kind_t;

void cursor_set_kind(cursor_kind_t kind);
cursor_kind_t cursor_get_kind(void);

#endif

#include "cursor.h"

static cursor_kind_t g_cursor_kind = CURSOR_ARROW;

void cursor_set_kind(cursor_kind_t kind) {
    g_cursor_kind = kind;
}

cursor_kind_t cursor_get_kind(void) {
    return g_cursor_kind;
}

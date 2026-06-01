#ifndef NTUX_DESKTOP_WINDOW_INTERNAL_H
#define NTUX_DESKTOP_WINDOW_INTERNAL_H

#include "desktop_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

desk_window_t* window_find_by_id(uint64_t id);
int window_index_by_id(uint64_t id);
void window_compact_invisible(void);
int window_find_recyclable_slot(void);
void window_bring_to_front(int idx);
void window_clamp_rect(desk_window_t* w, int fb_w, int fb_h);
void desk_window_toggle_maximize(desk_window_t* w, int fb_w, int fb_h);

void window_canvas_clear(desk_window_t* w, uint32_t color);
void window_canvas_rect(desk_window_t* w, int x, int y, int rw, int rh, uint32_t color, int filled);
void window_canvas_line(desk_window_t* w, int x0, int y0, int x1, int y1, uint32_t color);
void window_canvas_text(desk_window_t* w, int x, int y, uint32_t color, const char* text);
void window_canvas_button(desk_window_t* w, int x, int y, int rw, int rh, int kind, const char* text);
void window_canvas_present(desk_window_t* w);
void desk_window_set_image(desk_window_t* w, const char* path, int desired_channels);
void desk_window_set_icon(desk_window_t* w, const char* path);

#ifdef __cplusplus
}
#endif

#endif

#ifndef NTUX_DESKTOP_INTERNAL_H
#define NTUX_DESKTOP_INTERNAL_H

#include <syscall.h>

#include "desktop_defs.h"

extern ntux_fb_info_t g_fb;
extern desk_window_t g_windows[DESK_MAX_WINDOWS];
extern int g_window_count;
extern int g_focus_index;
extern desk_icon_t g_icons[DESK_MAX_ICONS];
extern int g_icon_count;
extern uint8_t g_start_open;
extern int g_anim_level;
extern int g_theme_index;
extern int g_ui_scale;
extern desk_term_state_t g_term_states[DESK_MAX_WINDOWS];
extern desk_term_state_t* g_term_exec_state;

const desk_theme_t* desk_theme(void);

void fill_rect(int x, int y, int w, int h, uint32_t c);
void draw_rect(int x, int y, int w, int h, uint32_t c);
void fill_round_rect(int x, int y, int w, int h, int r, uint32_t c);
void draw_round_rect(int x, int y, int w, int h, int r, uint32_t c);
void draw_text(int x, int y, const char* s, uint32_t c);
void desktop_draw_icon_pixels(int x, int y, int w, int h, const uint8_t* pixels, int pw, int ph);
void desktop_window_cleanup(int idx, int kill_owner);

void desk_term_write_for_tid(int tid, const char* s);
void desktop_mark_dirty(void);
void desktop_notify(const char* title, const char* body);

#endif

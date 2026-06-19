#ifndef NTUX_DESKTOP_INTERNAL_H
#define NTUX_DESKTOP_INTERNAL_H

#include <syscall.h>
#include <stdint.h>
#include <stddef.h>

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

extern int g_desktop_dirty;
extern uint64_t g_last_key_tick;
extern char g_last_key_char;
extern uint32_t* g_bg;
extern uint8_t* g_wallpaper_file;
extern size_t g_wallpaper_file_cap;
extern char g_wallpaper_pref[320];
extern int g_wallpaper_custom;
extern int g_wallpaper_builtin_enabled;
extern uint64_t g_current_uid;
extern char g_current_user[32];

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
void img_job_enqueue_window_image(uint64_t win_id, const char* path, int desired_channels);
void img_job_enqueue_window_icon(uint64_t win_id, const char* path);

int str_append(char* out, size_t cap, const char* s);
int str_append_char(char* out, size_t cap, char c);
int str_append_u64(char* out, size_t cap, uint64_t v);
int str_append_u32_2d(char* out, size_t cap, uint32_t v);
int str_append_i32(char* out, size_t cap, int32_t v);
int normalize_path(const char* cwd, const char* in, char* out, size_t cap);
int split_parent_name(const char* full, char* parent, char* name, size_t cap);
void desktop_mark_input(void);
int desktop_wants_console_input(void);
void open_console_window(void);
void open_explorer_window(void);
void open_clock_window(void);
long desktop_launch_target_tid(const char* path);
long launch_settings_tid(void);
void desktop_rescan_icons(void);
void desktop_wait_ticks(uint64_t ticks);
void update_focus_after_visibility_change(void);
int set_bg_from_image(const char* path);
int desktop_conf_save_layout(void);
const char* image_failure_reason(void);
void start_menu_clear_query(void);
void start_power_action(int action);
int users_add_account(const char* name, const char* pass);
void bg_gradient(void);
void icon_set_app_icon(desk_icon_t* icon, const char* path);
void icon_free_app_icon(desk_icon_t* icon);
int app_icon_path_for_exec(const char* exec_path, char* out, size_t cap);

#endif

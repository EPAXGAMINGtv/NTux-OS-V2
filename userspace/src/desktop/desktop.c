#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <syscall.h>
#include <image.h>
#include <window.h>
#include <font8x8_basic.h>

#include "desktop_defs.h"
#include "cursor.h"
#include "window_internal.h"
#include "api.h"
#include "taskbar.h"
#include "background.h"
#include <window_protocol.h>

#define DESK_ENABLE_INSTALLER 0

ntux_fb_info_t g_fb;
static uint32_t* g_frame = 0;
static uint32_t* g_bg = 0;
static uint32_t* g_bg_next = 0;
static float g_bg_blend = 0.0f;
static int g_bg_blending = 0;
static size_t g_pixels = 0;

desk_window_t g_windows[DESK_MAX_WINDOWS];
int g_window_count = 0;
int g_focus_index = -1;


static int g_dragging = 0;
static int g_drag_index = -1;
static int g_drag_off_x = 0;
static int g_drag_off_y = 0;
static uint8_t g_last_left = 0;
static int32_t g_last_mouse_x = 0;
static int32_t g_last_mouse_y = 0;
static int g_last_mouse_left = 0;
static int g_last_mouse_right = 0;
static int g_last_mouse_middle = 0;
static int g_last_mouse_scroll = 0;
static int g_icon_dragging = 0;
static int g_icon_drag_index = -1;
static int g_icon_drag_off_x = 0;
static int g_icon_drag_off_y = 0;
static uint8_t g_icon_custom_pos[DESK_MAX_ICONS];
static uint64_t g_last_icon_click_tick = 0;
static int g_last_icon_click_index = -1;
static int g_resizing = 0;
static int g_resize_index = -1;
static int g_resize_start_x = 0;
static int g_resize_start_y = 0;
static int g_resize_start_w = 0;
static int g_resize_start_h = 0;
desk_icon_t g_icons[DESK_MAX_ICONS];
int g_icon_count = 0;
uint8_t g_start_open = 0;
static uint8_t g_hotkey_t_last = 0;
static uint8_t g_hotkey_esc_last = 0;
static uint8_t g_hotkey_f4_last = 0;
static uint8_t g_hotkey_f11_last = 0;
static uint8_t g_hotkey_tab_last = 0;
static uint8_t g_hotkey_w_last = 0;
static uint64_t g_close_event_id = 0;
static uint64_t g_close_event_tick = 0;
static uint8_t g_alt_tab_active = 0;
static int g_alt_tab_choice = -1;
static uint64_t g_suppress_clock_until = 0;
static int g_start_sel = 0;
static float g_start_anim = 0.0f;
static char g_start_query[48] = "";
static int g_start_query_len = 0;
static int g_start_scroll = 0;
static uint64_t g_last_frame_ticks = 0;
static uint64_t g_last_redraw_tick = 0;
static uint64_t g_last_storage_rescan = 0;
static uint64_t g_storage_rescan_ticks = 0;
static int g_desktop_dirty = 1;
static uint64_t g_last_key_tick = 0;
static char g_last_key_char = 0;
/* Idle tracking uses CMOS time (seconds). */
static uint64_t g_last_input_tick = 0;
static uint64_t g_idle_timeout_ticks = 0;
static int g_screensaver_enabled = 0;
static uint8_t g_screensaver_active = 0;
static uint8_t g_ticks_advancing = 1;
static uint64_t g_idle_grace_until = 0;
static uint64_t g_hz_cached = 0;

#define NTUX_THREAD_TERMINATED 3u
static uint64_t g_boot_splash_until = 0;
static uint64_t g_boot_splash_start = 0;
static uint32_t g_boot_splash_frames = 0;
static int g_power_action = 0; /* 0 none, 1 reboot, 2 shutdown */
static uint64_t g_power_start = 0;
static uint64_t g_power_until = 0;

enum { DESK_NOTIFY_MAX = 6 };
typedef struct {
    int active;
    char title[48];
    char body[96];
    uint64_t born;
    float anim;
} desk_notify_t;
static desk_notify_t g_notifs[DESK_NOTIFY_MAX];

static int g_gallery_open = 0;
static float g_gallery_anim = 0.0f;
static int g_gallery_page = 0;
static int g_gallery_sel = 0;

static int g_ctx_open = 0;
static int g_ctx_x = 0;
static int g_ctx_y = 0;
static int g_ctx_hover = -1;
static int g_ctx_sub_open = 0;
static int g_ctx_sub_hover = -1;

static char g_wallpaper_paths[DESK_LS_MAX][256];
static int g_wallpaper_count = 0;

typedef struct {
    char path[256];
    uint8_t* pixels;
    int w;
    int h;
    uint8_t ready;
    uint8_t failed;
    uint8_t loading;
} desk_wall_thumb_t;
static desk_wall_thumb_t g_wallpaper_thumbs[DESK_LS_MAX];

enum {
    IMG_JOB_ICON = 1,
    IMG_JOB_BROWSER_THUMB = 2,
    IMG_JOB_WALL_THUMB = 3,
    IMG_JOB_BROWSER_PREVIEW = 4,
    IMG_JOB_WALLPAPER = 5
};

enum {
    NAMEBOX_NONE = 0,
    NAMEBOX_DESKTOP_NEW_FOLDER = 1,
    NAMEBOX_DESKTOP_NEW_FILE = 2,
    NAMEBOX_BROWSER_NEW_FOLDER = 3,
    NAMEBOX_BROWSER_NEW_FILE = 4,
    NAMEBOX_BROWSER_RENAME = 5,
    NAMEBOX_PICKER_SAVE = 6
};

typedef struct {
    uint8_t type;
    int idx;
    int max_w;
    int max_h;
    char path[256];
    int browser_id;
} img_job_t;

#define IMG_JOB_MAX 128
static img_job_t g_img_jobs[IMG_JOB_MAX];
static int g_img_job_head = 0;
static int g_img_job_tail = 0;
static int g_img_job_count = 0;

static int g_wallpaper_job_active = 0;
static image_t g_wallpaper_job_img;
static uint32_t g_wallpaper_job_row = 0;
static int* g_wallpaper_job_xmap = 0;
static char g_wallpaper_job_path[256] = "";
static int g_wallpaper_notify_pending = 0;

enum {
    DESK_ANIM_MINIMIZE = 1,
    DESK_ANIM_RESTORE = 2
};

typedef struct {
    int tid;
    int term_idx;
} desk_term_route_t;

#define DESK_TERM_ROUTE_MAX 64
static desk_term_route_t g_term_routes[DESK_TERM_ROUTE_MAX];

desk_term_state_t g_term_states[DESK_MAX_WINDOWS];
desk_term_state_t* g_term_exec_state = 0;
static uint8_t g_key_last[128];
static uint8_t g_term_passthrough = 0;

static void term_route_register(int tid, int term_idx);
static int term_route_find(int tid);
static long launch_settings_tid(void);
static long launch_browser_tid(void);
static void open_explorer_window_at(const char* path);
static void img_job_enqueue(uint8_t type, int idx, const char* path, int max_w, int max_h, int browser_id);
static void img_job_pump(void);
static void wallpaper_queue_render(const char* path);
static void wallpaper_job_step(uint32_t max_rows);
static int icon_request_preview(int idx);
static void draw_desktop_menu(void);
static int desktop_menu_item_at(int x, int y);
static int desktop_menu_sub_item_at(int x, int y);
static void desktop_menu_open_at(int x, int y);
static void desktop_menu_close(void);
static void draw_browser_menu(const desk_window_t* w, const desk_browser_state_t* st);
static int browser_menu_item_at(const desk_browser_state_t* st, int x, int y);
static int browser_menu_sub_item_at(const desk_browser_state_t* st, int x, int y);
static void browser_menu_open_at(desk_browser_state_t* st, int x, int y);
static void browser_menu_close(desk_browser_state_t* st);
static void browser_menu_close_all(void);
static desk_browser_state_t* browser_state_get(int id);
static int browser_state_register(desk_browser_state_t* st);
static void browser_state_unregister(int id);
static desk_browser_state_t* window_browser_state(const desk_window_t* w);
static void browser_state_destroy(desk_browser_state_t* st);
void desktop_mark_dirty(void);
static void browser_clipboard_set(desk_browser_state_t* st, int cut);
static int browser_clipboard_paste(desk_browser_state_t* st);
static void draw_namebox(void);
static int namebox_handle_mouse(int mx, int my, int left_edge);
static void namebox_open(int mode, const char* title, const char* initial, const char* base, const char* old_path, int browser_id);
static void namebox_close(void);
static void namebox_confirm(void);
static void desktop_snap_icon_to_grid(desk_icon_t* icon);
static void desktop_auto_arrange_icons(void);

typedef struct {
    char path[256];
    uint8_t* pixels;
    int w;
    int h;
    uint8_t ready;
    uint8_t failed;
    uint8_t loading;
} desk_thumb_t;
struct desk_browser_state {
    int id;
    desk_browser_entry_t ents[DESK_LS_MAX];
    int count;
    int scroll;
    int scroll_dragging;
    int scroll_drag_offset;
    int selected;
    char path[256];
    uint64_t last_click_tick;
    int last_click_index;
    uint8_t mark[DESK_LS_MAX];
    int mark_count;
    char status[96];
    uint8_t* preview;
    int preview_w;
    int preview_h;
    char preview_path[256];
    int preview_ready;
    int preview_loading;
    char text_preview[1024];
    int text_preview_len;
    int text_preview_ready;
    char text_preview_path[256];
    int ctx_open;
    int ctx_sub_open;
    int ctx_hover;
    int ctx_sub_hover;
    int ctx_x;
    int ctx_y;
    desk_thumb_t thumbs[DESK_LS_MAX];
};

static desk_browser_state_t* g_browser_states[DESK_MAX_WINDOWS];
static char g_browser_default_path[256] = "/";
static int g_browser_clip_count = 0;
static int g_browser_clip_cut = 0;
static char g_browser_clip_paths[DESK_LS_MAX][256];
static int g_picker_active = 0;
static uint32_t g_picker_flags = 0;
static int g_picker_owner_tid = -1;
static char g_picker_title[64] = "Open File";
static char g_picker_path[256] = "/";
static char g_picker_status[96] = "Ready";
static desk_browser_entry_t g_picker_ents[DESK_LS_MAX];
static int g_picker_count = 0;
static int g_picker_scroll = 0;
static int g_picker_selected = -1;
static uint64_t g_picker_last_click_tick = 0;
static int g_picker_last_click_index = -1;

static int inside_window(const desk_window_t* w, int x, int y);
static int g_msgbox_active = 0;
static uint32_t g_msgbox_flags = 0;
static int g_msgbox_owner_tid = -1;
static char g_msgbox_title[64] = "Message";
static char g_msgbox_text[WINDOW_MAX_TEXT] = "";

static int g_namebox_active = 0;
static int g_namebox_mode = 0;
static char g_namebox_title[64] = "";
static char g_namebox_value[64] = "";
static char g_namebox_base[256] = "";
static char g_namebox_old[256] = "";
static int g_namebox_browser_id = 0;
int g_anim_level = 0;
int g_theme_index = 0;
int g_ui_scale = 1;
static uint64_t g_current_uid = 0;
static char g_current_user[32] = "kernel";
static char g_user_store_root[32] = "/home";
static char g_desktop_store_root[32] = "/";
static char g_desktop_dir[64] = "/desktop";
static char g_desktop_conf_dir[64] = "/etc";
static char g_desktop_conf_path[96] = "/etc/desktop.conf";
static int g_desktop_conf_ready = 0;
static char g_wallpaper_pref[320] = "gradient";
static int g_wallpaper_custom = 0;
static int g_wallpaper_builtin_enabled = 0;
#if DESK_ENABLE_INSTALLER
static ntux_dirent_t g_inst_dirents[DESK_INST_LS_MAX];
static uint64_t g_inst_drive_ids[8];
static ntux_block_device_info_t g_inst_devs[8];
static int g_inst_dev_count = 0;
static int g_inst_selected = 0;
static int g_inst_focus_btn = 1;
static char g_inst_source[DESK_INST_PATH_MAX];
static char g_inst_logs[DESK_INST_LOG_LINES][96];
static int g_inst_log_count = 0;
static uint64_t g_inst_copy_total = 0;
static uint64_t g_inst_copy_done = 0;
static uint64_t g_inst_copy_total_bytes = 0;
static uint64_t g_inst_copy_done_bytes = 0;
static int g_inst_copy_last_percent = -1;
static int g_inst_phase = 0;
static int g_inst_adv_enabled = 0;
static int g_inst_part_scheme = 0; /* 0=MBR, 1=GPT */
static uint8_t g_inst_fs_choice = 4; /* default ext4 */
static int g_inst_request_busy = 0;
#endif
static uint8_t* g_wallpaper_file = 0;
static size_t g_wallpaper_file_cap = 0;

static desk_user_t g_users[DESK_MAX_USERS];
static int g_user_count = 0;

static const desk_theme_t g_themes[DESK_THEMES] = {
    {
        "Mono",
        0xFF101012u, 0xFF16171Au, 0xFF0B0C0Fu, 0xFF7D7D7Du,
        0xFF7D7D7Du, 0xFF1E1F22u, 0xFF2E3035u, 0xFF1A1B1Eu,
        0xFF15161Au, 0xFFDADADAu, 0xFF9A9A9Au,
        0xEE141518u, 0xFF2A2B2Fu, 0xEE15161Au, 0x55252528u
    }
};

static const keymap_t g_keys[] = {
    {0x02, '1', '!'}, {0x03, '2', '"'}, {0x04, '3', '#'}, {0x05, '4', '$'},
    {0x06, '5', '%'}, {0x07, '6', '&'}, {0x08, '7', '/'}, {0x09, '8', '('},
    {0x0A, '9', ')'}, {0x0B, '0', '='}, {0x0C, '-', '_'}, {0x0D, '+', '*'},
    {0x10, 'q', 'Q'}, {0x11, 'w', 'W'}, {0x12, 'e', 'E'}, {0x13, 'r', 'R'},
    {0x14, 't', 'T'}, {0x15, 'y', 'Y'}, {0x16, 'u', 'U'}, {0x17, 'i', 'I'},
    {0x18, 'o', 'O'}, {0x19, 'p', 'P'}, {0x1A, '[', '{'}, {0x1B, ']', '}'},
    {0x1E, 'a', 'A'}, {0x1F, 's', 'S'}, {0x20, 'd', 'D'}, {0x21, 'f', 'F'},
    {0x22, 'g', 'G'}, {0x23, 'h', 'H'}, {0x24, 'j', 'J'}, {0x25, 'k', 'K'},
    {0x26, 'l', 'L'}, {0x27, ';', ':'}, {0x28, '\'', '"'}, {0x29, '`', '~'},
    {0x2B, '\\', '|'}, {0x2C, 'z', 'Z'}, {0x2D, 'x', 'X'}, {0x2E, 'c', 'C'},
    {0x2F, 'v', 'V'}, {0x30, 'b', 'B'}, {0x31, 'n', 'N'}, {0x32, 'm', 'M'},
    {0x33, ',', '<'}, {0x34, '.', '>'}, {0x35, '/', '?'}, {0x39, ' ', ' '}
};

static uint16_t rd_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t rd_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd_i32_le(const uint8_t* p) {
    return (int32_t)rd_u32_le(p);
}

static int normalize_path(const char* cwd, const char* in, char* out, size_t cap);
static int split_parent_name(const char* full, char* parent, char* name, size_t cap);
static int find_icon_index_by_id(uint64_t id);
static int find_icon_hit(int x, int y);
static void clamp_icon_position(desk_icon_t* icon);
static void seed_filesystem_elf_icons(void);
void desktop_rescan_icons(void);
void open_explorer_window(void);
void open_console_window(void);
void open_clock_window(void);
void open_settings_window(void);
static int browser_refresh(desk_browser_state_t* st);
static int browser_handle_click(const desk_window_t* w, desk_browser_state_t* st, int x, int y);
static void draw_file_browser(const desk_window_t* w, desk_browser_state_t* st);
static void browser_thumbs_clear(desk_browser_state_t* st);
static void browser_thumb_try_load(desk_browser_state_t* st, int idx, int max_w, int max_h);
static int settings_handle_click(const desk_window_t* w, int x, int y);
static void draw_settings_window(const desk_window_t* w);
static desk_term_state_t* term_state_for_window(const desk_window_t* w);
static desk_term_state_t* term_state_active(void);
static void term_push_line(const char* s);
static void term_push_line_state(desk_term_state_t* ts, const char* s);
static void notify_push(const char* title, const char* body);
static void notify_update(float dt);
static void draw_notifications(void);
static int notify_hit(int mx, int my);
static void draw_gallery_panel(void);
static int gallery_handle_click(int mx, int my);
static int gallery_hit(int mx, int my);
static void gallery_update(float dt);
static void wallpaper_scan_all(void);
static int gallery_image_count(void);
static void wallpaper_set_and_save(const char* path);
static void wallpaper_thumbs_clear(void);
static void wallpaper_thumb_try_load(int idx, int max_w, int max_h);
static void draw_analog_clock_window(const desk_window_t* w);
void draw_text(int x, int y, const char* s, uint32_t c);
static int users_load_db(void);
static int users_save_db(void);
static int users_authenticate(const char* name, const char* pass, uint32_t* out_uid);
static int users_add_account(const char* name, const char* pass);
static char poll_char(void);
static int poll_special_press(int sc);
static void desktop_mark_input(void);
static int screensaver_is_running(void);
static void screensaver_try_start(uint64_t now);
static void desktop_logout(void);
static int desktop_ticks_are_advancing(void);
static void desktop_wait_ticks(uint64_t ticks);
static uint64_t desktop_get_hz(void);
static uint64_t desktop_now_seconds(void);
static void draw_alt_tab_overlay(void);
static long desktop_launch_target(const char* path);
static void setup_desktop_storage(void);
static int desktop_conf_load_layout(void);
static int desktop_conf_save_layout(void);
static int desktop_conf_is_installed(void);
#if DESK_ENABLE_INSTALLER
static int desktop_run_installer_ui(int force);
static void desk_inst_draw_ui(const uint64_t drive_ids[8], const ntux_block_device_info_t devs[8], int dev_count,
                              int selected, int focus_btn, const char* source,
                              const char logs[DESK_INST_LOG_LINES][96], int log_count);
static int desk_inst_copy_tree(const char* src, const char* dst);
static void desktop_check_installer_request(void);
static int desk_inst_is_target_name(const char* name);
#endif
static int in_rect(int x, int y, int rx, int ry, int rw, int rh);
static void draw_cursor(void);
static void draw_boot_splash(void);
static void start_power_action(int action);
static void bg_gradient(void);
static int set_bg_from_image(const char* path);
static void desktop_apply_wallpaper_pref(void);
static char lower_char(char c);
static int str_ends_with_ci(const char* s, const char* ext);
static int path_is_image_ext(const char* path);
static int path_is_text_ext(const char* path);
static int path_is_obj_ext(const char* path);
static int text_contains_ci(const char* hay, const char* needle);
static int start_menu_match_icon(int idx);
static void start_menu_clear_query(void);
static int start_menu_category_id(const desk_icon_t* icon);
static const char* start_menu_category_name(int id);
static int start_menu_category_match_icon(const desk_icon_t* icon, int cat);
static int start_menu_content_height(void);
static int start_menu_item_y_by_visible(int sel);

static int desktop_wants_console_input(void) {
    if (g_namebox_active) return 1;
    if (g_start_open) return 1;
    if (g_focus_index < 0 || g_focus_index >= g_window_count) return 0;
    if (!g_windows[g_focus_index].visible || g_windows[g_focus_index].minimized) return 0;
    return g_windows[g_focus_index].terminal ? 1 : 0;
}

static int str_ends_with(const char* s, const char* suffix) {
    size_t sl, tl;
    if (!s || !suffix) return 0;
    sl = strlen(s);
    tl = strlen(suffix);
    if (sl < tl) return 0;
    return (memcmp(s + sl - tl, suffix, tl) == 0) ? 1 : 0;
}

static int name_is_elf(const char* name) {
    if (!name) return 0;
    if (str_ends_with(name, ".elf")) return 1;
    if (str_ends_with(name, ".ELF")) return 1;
    return 0;
}

static int path_is_elf_exec(const char* path, const char* name) {
    if (!path || !path[0]) return 0;
    return name_is_elf(name);
}

static int path_is_terminal_elf(const char* path) {
    if (!path || !path[0]) return 0;
    if (strcmp(path, "terminal.elf") == 0) return 1;
    if (str_ends_with(path, "/terminal.elf")) return 1;
    if (str_ends_with(path, "/TERMINAL.ELF")) return 1;
    return 0;
}

static int module_token_wants_console(const char* token) {
    if (!token || !token[0]) return 0;
    if (text_contains_ci(token, "console")) return 1;
    if (text_contains_ci(token, "konsole")) return 1;
    if (text_contains_ci(token, "terminal")) return 1;
    if (text_contains_ci(token, "shell")) return 1;
    if (text_contains_ci(token, "cli")) return 1;
    if (text_contains_ci(token, "healthcheck")) return 1;
    return 0;
}

static int str_append(char* out, size_t cap, const char* s) {
    size_t i;
    size_t j = 0;
    if (!out || cap == 0 || !s) return -1;
    i = strlen(out);
    if (i >= cap) return -1;
    while (s[j] && i + 1 < cap) out[i++] = s[j++];
    out[i] = '\0';
    return s[j] ? -1 : 0;
}

static int parse_i32_safe(const char* s, int* out) {
    int sign = 1;
    int v = 0;
    size_t i = 0;
    if (!s || !s[0] || !out) return -1;
    if (s[0] == '-') {
        sign = -1;
        i = 1;
    }
    if (!s[i]) return -1;
    for (; s[i]; ++i) {
        if (s[i] < '0' || s[i] > '9') return -1;
        if (v > 214748364) return -1;
        v = v * 10 + (int)(s[i] - '0');
    }
    *out = sign * v;
    return 0;
}

static int str_append_char(char* out, size_t cap, char c) {
    size_t i;
    if (!out || cap < 2) return -1;
    i = strlen(out);
    if (i + 1 >= cap) return -1;
    out[i++] = c;
    out[i] = '\0';
    return 0;
}

static int str_append_u64(char* out, size_t cap, uint64_t v) {
    char tmp[32];
    int p = 0;
    if (!out || cap == 0) return -1;
    if (v == 0) return str_append_char(out, cap, '0');
    while (v > 0 && p < (int)sizeof(tmp)) {
        tmp[p++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (p > 0) {
        if (str_append_char(out, cap, tmp[--p]) != 0) return -1;
    }
    return 0;
}

static int str_append_u32_2d(char* out, size_t cap, uint32_t v) {
    char d0 = (char)('0' + ((v / 10u) % 10u));
    char d1 = (char)('0' + (v % 10u));
    if (str_append_char(out, cap, d0) != 0) return -1;
    if (str_append_char(out, cap, d1) != 0) return -1;
    return 0;
}

static int str_append_i32(char* out, size_t cap, int32_t v) {
    uint64_t uv;
    if (v < 0) {
        if (str_append_char(out, cap, '-') != 0) return -1;
        uv = (uint64_t)(-(int64_t)v);
    } else {
        uv = (uint64_t)v;
    }
    return str_append_u64(out, cap, uv);
}

static int icon_exists_path(const char* exec_path) {
    if (!exec_path || !exec_path[0]) return 0;
    for (int i = 0; i < g_icon_count; ++i) {
        if (!g_icons[i].visible) continue;
        if (strcmp(g_icons[i].exec_path, exec_path) == 0) return 1;
    }
    return 0;
}

static void label_from_filename(const char* filename, char* out, size_t out_cap) {
    size_t i = 0;
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!filename || !filename[0]) return;
    if (strcmp(filename, "calc") == 0 || strcmp(filename, "calc.elf") == 0) {
        strncpy(out, "Calculator", out_cap - 1);
        out[out_cap - 1] = '\0';
        return;
    }
    while (filename[i] && filename[i] != '.' && i + 1 < out_cap) {
        char c = filename[i];
        if (i == 0 && c >= 'a' && c <= 'z') c = (char)('A' + (c - 'a'));
        out[i] = c;
        ++i;
    }
    out[i] = '\0';
}

static void label_from_name(const char* name, char* out, size_t out_cap) {
    size_t i = 0;
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!name || !name[0]) return;
    if (strcmp(name, "calc") == 0) {
        strncpy(out, "Calculator", out_cap - 1);
        out[out_cap - 1] = '\0';
        return;
    }
    while (name[i] && i + 1 < out_cap) {
        char c = name[i];
        if (i == 0 && c >= 'a' && c <= 'z') c = (char)('A' + (c - 'a'));
        out[i] = c;
        ++i;
    }
    out[i] = '\0';
}

static int join_path(const char* base, const char* name, char* out, size_t out_cap) {
    if (!base || !name || !out || out_cap < 2) return -1;
    out[0] = '\0';
    if (strcmp(base, "/") == 0) {
        if (str_append_char(out, out_cap, '/') != 0) return -1;
        return str_append(out, out_cap, name);
    }
    if (str_append(out, out_cap, base) != 0) return -1;
    if (str_append_char(out, out_cap, '/') != 0) return -1;
    return str_append(out, out_cap, name);
}

static void put_px(int x, int y, uint32_t c) {
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;
    g_frame[(uint64_t)y * (uint64_t)g_fb.width + (uint64_t)x] = c;
}

void fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy) {
        int py = y + yy;
        if (py < 0 || (uint32_t)py >= g_fb.height) continue;
        for (int xx = 0; xx < w; ++xx) {
            int px = x + xx;
            if (px < 0 || (uint32_t)px >= g_fb.width) continue;
            g_frame[(uint64_t)py * (uint64_t)g_fb.width + (uint64_t)px] = c;
        }
    }
}

void draw_rect(int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c);
    fill_rect(x + w - 1, y, 1, h, c);
}

void desktop_window_cleanup(int idx, int kill_owner) {
    if (idx < 0 || idx >= g_window_count) return;
    desk_window_t* w = &g_windows[idx];
    uint64_t wid = w->id;
    if (kill_owner && w->owner_tid >= 0) {
        (void)sys_task_kill(w->owner_tid);
    }
    if (w->image_data) {
        free(w->image_data);
        w->image_data = 0;
        w->image_enabled = 0;
    }
    if (w->icon_data) {
        free(w->icon_data);
        w->icon_data = 0;
        w->icon_ready = 0;
    }
    if (w->file_browser && w->browser_id > 0) {
        if (g_namebox_active && g_namebox_browser_id == w->browser_id) {
            namebox_close();
        }
        desk_browser_state_t* st = browser_state_get(w->browser_id);
        browser_state_unregister(w->browser_id);
        browser_state_destroy(st);
        w->browser_id = 0;
    }
    if (w->terminal) {
        int slot = (int)w->term_slot;
        if (slot >= 0 && slot < DESK_MAX_WINDOWS) {
            memset(&g_term_states[slot], 0, sizeof(g_term_states[slot]));
            strncpy(g_term_states[slot].cwd, "/", sizeof(g_term_states[slot].cwd) - 1);
            g_term_states[slot].cwd[sizeof(g_term_states[slot].cwd) - 1] = '\0';
            if (g_term_exec_state == &g_term_states[slot]) g_term_exec_state = 0;
        }
    }
    if (w->terminal) {
        for (int i = 0; i < DESK_TERM_ROUTE_MAX; ++i) {
            if (g_term_routes[i].term_idx == idx) {
                g_term_routes[i].tid = 0;
                g_term_routes[i].term_idx = -1;
            }
        }
    }
    if (g_close_event_id != 0 && g_close_event_id == wid) {
        g_close_event_id = 0;
        g_close_event_tick = 0;
    }
    memset(w, 0, sizeof(*w));
    w->visible = 0;
    w->minimized = 0;
    w->owner_tid = -1;
    desktop_mark_dirty();
}

void fill_round_rect(int x, int y, int w, int h, int r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        fill_rect(x, y, w, h, c);
        return;
    }
    int maxr = (w < h ? w : h) / 2;
    if (r > maxr) r = maxr;
    fill_rect(x + r, y, w - 2 * r, h, c);
    fill_rect(x, y + r, r, h - 2 * r, c);
    fill_rect(x + w - r, y + r, r, h - 2 * r, c);
    int rr = r - 1;
    int rr2 = rr * rr;
    for (int dy = 0; dy < r; ++dy) {
        for (int dx = 0; dx < r; ++dx) {
            if (dx * dx + dy * dy <= rr2) {
                put_px(x + r - 1 - dx, y + r - 1 - dy, c);
                put_px(x + w - r + dx, y + r - 1 - dy, c);
                put_px(x + r - 1 - dx, y + h - r + dy, c);
                put_px(x + w - r + dx, y + h - r + dy, c);
            }
        }
    }
}

void draw_round_rect(int x, int y, int w, int h, int r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        draw_rect(x, y, w, h, c);
        return;
    }
    int maxr = (w < h ? w : h) / 2;
    if (r > maxr) r = maxr;
    fill_rect(x + r, y, w - 2 * r, 1, c);
    fill_rect(x + r, y + h - 1, w - 2 * r, 1, c);
    fill_rect(x, y + r, 1, h - 2 * r, c);
    fill_rect(x + w - 1, y + r, 1, h - 2 * r, c);
    int rr = r - 1;
    int rr2 = rr * rr;
    int ir = rr - 1;
    int ir2 = ir > 0 ? ir * ir : -1;
    for (int dy = 0; dy < r; ++dy) {
        for (int dx = 0; dx < r; ++dx) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= rr2 && (ir2 < 0 || d2 > ir2)) {
                put_px(x + r - 1 - dx, y + r - 1 - dy, c);
                put_px(x + w - r + dx, y + r - 1 - dy, c);
                put_px(x + r - 1 - dx, y + h - r + dy, c);
                put_px(x + w - r + dx, y + h - r + dy, c);
            }
        }
    }
}

static void draw_line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? -(y1 - y0) : -(y0 - y1);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static char spinner_frame(void) {
    uint64_t t = sys_get_ticks();
    switch ((int)((t / 6u) % 4u)) {
        case 0: return '|';
        case 1: return '/';
        case 2: return '-';
        default: return '\\';
    }
}

static void draw_spinner_at(int x, int y, uint32_t color) {
    char s[2];
    s[0] = spinner_frame();
    s[1] = '\0';
    draw_text(x, y, s, color);
}

static uint32_t color_lerp(uint32_t a, uint32_t b, uint32_t t255);

static void draw_spinner_ring(int cx, int cy, int r, uint32_t color, uint32_t dim_color) {
    static const int8_t ring[12][2] = {
        { 0, -10}, { 5, -9}, { 9, -5}, {10,  0},
        { 9,  5}, { 5,  9}, { 0, 10}, {-5,  9},
        {-9,  5}, {-10, 0}, {-9, -5}, {-5, -9}
    };
    if (r < 4) r = 4;
    uint64_t t = sys_get_ticks();
    int head = (int)((t / 3u) % 12u);
    for (int i = 0; i < 12; ++i) {
        int idx = (head - i);
        if (idx < 0) idx += 12;
        uint32_t c = (i == 0) ? color : color_lerp(dim_color, color, (uint32_t)(60 - i * 4));
        int px = cx + (ring[idx][0] * r) / 10;
        int py = cy + (ring[idx][1] * r) / 10;
        put_px(px, py, c);
        put_px(px + 1, py, c);
        put_px(px, py + 1, c);
    }
}

static void draw_boot_progress(int x, int y, int w, int h, uint32_t base, uint32_t glow) {
    uint64_t t = sys_get_ticks();
    fill_rect(x, y, w, h, base);
    draw_rect(x, y, w, h, glow);
    int band = w / 4;
    if (band < 18) band = 18;
    int pos = (int)((t / 2u) % (uint64_t)(w + band)) - band;
    if (pos < 0) pos = 0;
    if (pos > w) pos = w;
    int len = band;
    if (pos + len > w) len = w - pos;
    if (len > 0) {
        fill_rect(x + pos, y + 2, len, h - 4, glow);
    }
}

const desk_theme_t* desk_theme(void) {
    if (g_theme_index < 0 || g_theme_index >= DESK_THEMES) return &g_themes[0];
    return &g_themes[g_theme_index];
}

static uint32_t color_lerp(uint32_t a, uint32_t b, uint32_t t255) {
    uint32_t ar = (a >> 16) & 0xFFu, ag = (a >> 8) & 0xFFu, ab = a & 0xFFu;
    uint32_t br = (b >> 16) & 0xFFu, bg = (b >> 8) & 0xFFu, bb = b & 0xFFu;
    uint32_t r = (ar * (255u - t255) + br * t255) / 255u;
    uint32_t g = (ag * (255u - t255) + bg * t255) / 255u;
    uint32_t bl = (ab * (255u - t255) + bb * t255) / 255u;
    return (r << 16) | (g << 8) | bl;
}

__attribute__((unused)) static int theme_index_from_name(const char* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < DESK_THEMES; ++i) {
        if (strcmp(name, g_themes[i].name) == 0) return i;
    }
    return -1;
}

static void draw_window_canvas(desk_window_t* w, int ox, int oy, int body_y) {
    if (!w || !w->canvas_enabled) return;
    const desk_canvas_op_t* ops = w->draw_ops;
    int op_count = w->draw_count;
    uint32_t clear = w->canvas_clear;
    int base_w = w->canvas_base_w;
    int base_h = w->canvas_base_h;
    int cur_w = w->w - 4;
    int cur_h = w->h - DESK_TITLEBAR_H - 3;
    float sx = 1.0f;
    float sy = 1.0f;

    if (w->canvas_dirty) {
        w->present_count = w->draw_count;
        w->present_clear = w->canvas_clear;
        if (w->present_count > 0) {
            memcpy(w->present_ops, w->draw_ops, sizeof(desk_canvas_op_t) * (size_t)w->present_count);
        }
        w->present_valid = 1;
        w->draw_count = 0;
        w->canvas_dirty = 0;
    } else if (w->present_valid) {
        ops = w->present_ops;
        op_count = w->present_count;
        clear = w->present_clear;
    }

    if (base_w <= 0) base_w = 1;
    if (base_h <= 0) base_h = 1;
    if (cur_w < 1) cur_w = 1;
    if (cur_h < 1) cur_h = 1;
    sx = (float)cur_w / (float)base_w;
    sy = (float)cur_h / (float)base_h;

    fill_rect(ox + 2, body_y, w->w - 4, w->h - DESK_TITLEBAR_H - 3, clear);
    for (int i = 0; i < op_count; ++i) {
        const desk_canvas_op_t* op = &ops[i];
        int px = ox + (int)((float)op->x * sx + 0.5f);
        int py = oy + DESK_TITLEBAR_H + (int)((float)op->y * sy + 0.5f);
        int pw = (int)((float)op->w * sx + 0.5f);
        int ph = (int)((float)op->h * sy + 0.5f);
        if (op->type == CANVAS_OP_RECT) {
            if (op->filled) fill_rect(px, py, pw, ph, op->color);
            else draw_rect(px, py, pw, ph, op->color);
        } else if (op->type == CANVAS_OP_LINE) {
            int x2 = ox + (int)((float)op->x2 * sx + 0.5f);
            int y2 = oy + DESK_TITLEBAR_H + (int)((float)op->y2 * sy + 0.5f);
            draw_line(px, py, x2, y2, op->color);
        } else if (op->type == CANVAS_OP_TEXT) {
            draw_text(px, py, op->text, op->color);
        } else if (op->type == CANVAS_OP_BUTTON) {
            const desk_theme_t* th = desk_theme();
            uint32_t bg = th->title_blur;
            uint32_t border = th->taskbar_border;
            uint32_t text = th->text_main;
            if (op->filled == 1) {
                bg = th->accent;
                border = th->taskbar_border;
                text = 0xFF091018u;
            } else if (op->filled == 2) {
                bg = 0xFF6A3A48u;
                border = 0xFFB65A74u;
                text = 0xFFFFDFE8u;
            }
            int r = g_ui_scale > 1 ? 8 : 6;
            if (pw < 2) pw = 2;
            if (ph < 2) ph = 2;
            fill_round_rect(px, py, pw, ph, r, bg);
            draw_round_rect(px, py, pw, ph, r, border);
            int tw = (int)strlen(op->text) * 8;
            int tx = px + (pw - tw) / 2;
            int ty = py + (ph - 8) / 2;
            draw_text(tx, ty, op->text, text);
        }
    }
}

static uint32_t blend_rgba(uint32_t bg, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint32_t br = (bg >> 16) & 0xFFu;
    uint32_t bgc = (bg >> 8) & 0xFFu;
    uint32_t bb = bg & 0xFFu;
    uint32_t ia = 255u - (uint32_t)a;
    uint32_t rr = ((uint32_t)r * a + br * ia) / 255u;
    uint32_t rg = ((uint32_t)g * a + bgc * ia) / 255u;
    uint32_t rb = ((uint32_t)b * a + bb * ia) / 255u;
    return (rr << 16) | (rg << 8) | rb;
}

static void draw_window_image(const desk_window_t* w, int ox, int oy) {
    if (!w || !w->image_enabled || !w->image_data) return;
    int body_x = ox + 2;
    int body_y = oy + DESK_TITLEBAR_H + 1;
    int body_w = w->w - 4;
    int body_h = w->h - DESK_TITLEBAR_H - 3;
    if (body_w <= 0 || body_h <= 0) return;
    uint32_t bg = w->bg ? w->bg : desk_theme()->window_fill;
    fill_rect(body_x, body_y, body_w, body_h, bg);

    int src_w = (int)w->image_w;
    int src_h = (int)w->image_h;
    if (src_w <= 0 || src_h <= 0) return;

    float sx = (float)body_w / (float)src_w;
    float sy = (float)body_h / (float)src_h;
    float scale = (sx < sy) ? sx : sy;
    int dst_w = (int)((float)src_w * scale);
    int dst_h = (int)((float)src_h * scale);
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;
    int dx = body_x + (body_w - dst_w) / 2;
    int dy = body_y + (body_h - dst_h) / 2;

    const uint8_t* src = w->image_data;
    int ch = w->image_channels == 4 ? 4 : 3;
    for (int y = 0; y < dst_h; ++y) {
        int syi = y * src_h / dst_h;
        const uint8_t* row = src + (size_t)syi * (size_t)src_w * (size_t)ch;
        for (int x = 0; x < dst_w; ++x) {
            int sxi = x * src_w / dst_w;
            const uint8_t* px = row + (size_t)sxi * (size_t)ch;
            uint8_t r = px[0];
            uint8_t g = px[1];
            uint8_t b = px[2];
            uint32_t c = (ch == 4) ? blend_rgba(bg, r, g, b, px[3]) : ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            put_px(dx + x, dy + y, c);
        }
    }
}

static void desktop_publish_input_state(void) {
    window_input_state_t st;
    memset(&st, 0, sizeof(st));
    int hover_index = -1;
    for (int i = g_window_count - 1; i >= 0; --i) {
        const desk_window_t* w = &g_windows[i];
        if (!w->visible || w->minimized) continue;
        if (inside_window(w, g_last_mouse_x, g_last_mouse_y)) {
            hover_index = i;
            break;
        }
    }
    if (hover_index >= 0) {
        const desk_window_t* w = &g_windows[hover_index];
        int base_w = w->canvas_base_w;
        int base_h = w->canvas_base_h;
        int client_w = w->w - 4;
        int client_h = w->h - DESK_TITLEBAR_H - 3;
        if (client_w < 1) client_w = 1;
        if (client_h < 1) client_h = 1;
        if (base_w <= 0) base_w = client_w;
        if (base_h <= 0) base_h = client_h;
        st.window_id = w->id;
        st.win_x = w->x;
        st.win_y = w->y;
        st.win_w = w->w;
        st.win_h = w->h;
        st.base_w = base_w;
        st.base_h = base_h;
        if (hover_index == g_focus_index) st.focused = 1;
    } else if (g_focus_index >= 0 && g_focus_index < g_window_count) {
        const desk_window_t* w = &g_windows[g_focus_index];
        if (w->visible && !w->minimized) {
            int base_w = w->canvas_base_w;
            int base_h = w->canvas_base_h;
            int client_w = w->w - 4;
            int client_h = w->h - DESK_TITLEBAR_H - 3;
            if (client_w < 1) client_w = 1;
            if (client_h < 1) client_h = 1;
            if (base_w <= 0) base_w = client_w;
            if (base_h <= 0) base_h = client_h;
            st.focused = 1;
            st.window_id = w->id;
            st.win_x = w->x;
            st.win_y = w->y;
            st.win_w = w->w;
            st.win_h = w->h;
            st.base_w = base_w;
            st.base_h = base_h;
        }
    }
    st.mouse_x = g_last_mouse_x;
    st.mouse_y = g_last_mouse_y;
    st.mouse_left = g_last_mouse_left;
    st.mouse_right = g_last_mouse_right;
    st.mouse_middle = g_last_mouse_middle;
    st.mouse_scroll = g_last_mouse_scroll;

    if (g_close_event_id != 0) {
        uint64_t hz = (uint64_t)sys_get_timer_hz();
        if (hz == 0) hz = 200u;
        uint64_t now = sys_get_ticks();
        if (now - g_close_event_tick > hz * 2u) {
            g_close_event_id = 0;
            g_close_event_tick = 0;
        }
    }
    if (g_close_event_id != 0) {
        st.close_requested = 1;
        st.close_window_id = g_close_event_id;
        st.close_event_tick = g_close_event_tick;
    }

    if (sys_fs_write_file("/tmp/desk_input", &st, (uint64_t)sizeof(st)) != 0) {
        (void)sys_fs_create_file("/tmp", "desk_input", &st, (uint64_t)sizeof(st));
    }
}

static void draw_char(int x, int y, char ch, uint32_t color) {
    uint8_t idx = (uint8_t)ch;
    for (int row = 0; row < 8; ++row) {
        uint8_t bits = font8x8_basic[idx][row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1u << col)) put_px(x + col, y + row, color);
        }
    }
}

static void draw_char_scaled(int x, int y, char ch, int scale, uint32_t color) {
    if (scale <= 1) {
        draw_char(x, y, ch, color);
        return;
    }
    uint8_t idx = (uint8_t)ch;
    for (int row = 0; row < 8; ++row) {
        uint8_t bits = font8x8_basic[idx][row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1u << col)) {
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        put_px(x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

static void draw_text_scaled(int x, int y, const char* s, int scale, uint32_t c) {
    int cx = x;
    if (scale < 1) scale = 1;
    for (size_t i = 0; s && s[i]; ++i) {
        if (s[i] == '\n') {
            y += 10 * scale;
            cx = x;
            continue;
        }
        draw_char_scaled(cx, y, s[i], scale, c);
        cx += 8 * scale;
    }
}

void draw_text(int x, int y, const char* s, uint32_t c) {
    int cx = x;
    for (size_t i = 0; s && s[i]; ++i) {
        if (s[i] == '\n') {
            y += 10;
            cx = x;
            continue;
        }
        draw_char(cx, y, s[i], c);
        cx += 8;
    }
}

static desk_term_state_t* term_state_for_window(const desk_window_t* w) {
    if (!w || !w->terminal) return 0;
    if (w->term_slot >= DESK_MAX_WINDOWS) return 0;
    return &g_term_states[w->term_slot];
}

static desk_term_state_t* term_state_active(void) {
    if (g_focus_index < 0 || g_focus_index >= g_window_count) return 0;
    return term_state_for_window(&g_windows[g_focus_index]);
}

void term_print_banner(void) {
    term_push_line("+---------------------------- NTux Shell ----------------------------+");
    term_push_line("| profile: NTux-OS          renderer: desktop window     status: live|");
    term_push_line("+--------------------------------------------------------------------+");
    term_push_line(" help clear ls cd run adduser whoami mkfs.ext4 lsblk fdisk dd shutdown");
    term_push_line("+--------------------------------------------------------------------+");
}

static void term_make_prompt(const desk_term_state_t* ts, char* out, size_t cap) {
    ntux_time_t t;
    if (!out || cap < 4) return;
    out[0] = '\0';
    if (!ts) {
        (void)str_append(out, cap, "+--ntux @ / :: --:--:--");
        return;
    }
    (void)str_append(out, cap, "+--ntux @ ");
    (void)str_append(out, cap, ts->cwd);
    (void)str_append(out, cap, " :: ");
    if (sys_get_time(&t) == 0) {
        (void)str_append_u32_2d(out, cap, (uint32_t)t.hour);
        (void)str_append_char(out, cap, ':');
        (void)str_append_u32_2d(out, cap, (uint32_t)t.minute);
        (void)str_append_char(out, cap, ':');
        (void)str_append_u32_2d(out, cap, (uint32_t)t.second);
    } else {
        (void)str_append(out, cap, "tick ");
        (void)str_append_u64(out, cap, sys_get_ticks());
    }
}

void update_focus_after_visibility_change(void) {
    if (g_focus_index >= 0 && g_focus_index < g_window_count) {
        desk_window_t* w = &g_windows[g_focus_index];
        if (w->visible && !w->minimized) return;
    }
    g_focus_index = -1;
    for (int i = g_window_count - 1; i >= 0; --i) {
        if (g_windows[i].visible && !g_windows[i].minimized) {
            g_focus_index = i;
            return;
        }
    }
}

static void bg_gradient_to(uint32_t* dst) {
    const desk_theme_t* th = desk_theme();
    uint32_t w = g_fb.width;
    uint32_t h = g_fb.height;
    if (!dst) return;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t t = (x * 255u) / (w ? w : 1u);
            uint32_t a = color_lerp(th->wall_a, th->wall_b, t);
            uint32_t b = color_lerp(th->wall_b, th->wall_c, (y * 255u) / (h ? h : 1u));
            uint32_t c = color_lerp(a, b, 130u);
            uint32_t dx = (x > w / 2u) ? (x - w / 2u) : (w / 2u - x);
            uint32_t dy = (y > h / 2u) ? (y - h / 2u) : (h / 2u - y);
            uint32_t v = ((dx + dy) * 42u) / ((w + h) ? (w + h) : 1u);
            uint32_t r = ((c >> 16) & 0xFFu), g = ((c >> 8) & 0xFFu), bl = (c & 0xFFu);
            if (v > 42u) v = 42u;
            r = (r > v) ? (r - v) : 0u;
            g = (g > v) ? (g - v) : 0u;
            bl = (bl > (v / 2u)) ? (bl - (v / 2u)) : 0u;
            dst[(uint64_t)y * (uint64_t)w + (uint64_t)x] = (r << 16) | (g << 8) | bl;
        }
    }
}

static void bg_gradient(void) {
    if (!g_bg) return;
    bg_gradient_to(g_bg);
}

static void draw_wallpaper_fx(void) {
    if (g_anim_level <= 0) return;
    const desk_theme_t* th = desk_theme();
    uint64_t tk = sys_get_ticks();
    int stride = g_ui_scale > 1 ? 20 : 14;
    int amp = g_ui_scale > 1 ? 36 : 26;
    for (int y = 0; y < (int)g_fb.height - DESK_TASKBAR_H; y += stride) {
        int phase = (int)((tk * (uint64_t)(g_anim_level + 1) + (uint64_t)y * 7u) % 80u) - 40;
        int x1 = phase + ((y / stride) % 2 == 0 ? 0 : 120);
        int x2 = x1 + amp + ((y / stride) % 3) * 12;
        uint32_t c = color_lerp(th->accent, th->wall_b, (uint32_t)((y * 170) / (g_fb.height ? g_fb.height : 1u)));
        draw_line(x1, y, x2, y + stride / 2, c);
    }
}

static void wallpaper_update(float dt) {
    if (!g_bg_blending) return;
    g_bg_blend += dt * 0.04f;
    if (g_bg_blend >= 1.0f) {
        g_bg_blend = 1.0f;
        g_bg_blending = 0;
        if (g_bg && g_bg_next) {
            uint32_t* tmp = g_bg;
            g_bg = g_bg_next;
            g_bg_next = tmp;
        }
    }
    g_desktop_dirty = 1;
}

static void draw_wallpaper_base(uint32_t* dst) {
    if (!dst) return;
    if (!g_bg) {
        if (g_bg_next) {
            memcpy(dst, g_bg_next, g_pixels * sizeof(uint32_t));
        } else {
            bg_gradient_to(dst);
        }
        return;
    }
    if (!g_bg_blending || !g_bg_next) {
        memcpy(dst, g_bg, g_pixels * sizeof(uint32_t));
        return;
    }
    uint32_t t = (uint32_t)(g_bg_blend * 255.0f);
    for (size_t i = 0; i < g_pixels; ++i) {
        dst[i] = color_lerp(g_bg[i], g_bg_next[i], t);
    }
}

static int path_from_root(char* out, size_t out_cap, const char* root, const char* suffix) {
    if (!out || out_cap < 2 || !root || !root[0] || !suffix || !suffix[0]) return -1;
    out[0] = '\0';
    if (str_append(out, out_cap, root) != 0) return -1;
    return str_append(out, out_cap, suffix);
}

static int root_available(const char* root) {
    ntux_dirent_t probe[1];
    uint64_t n = 0;
    if (!root || !root[0]) return 0;
    return (sys_fs_list_dir(root, probe, 1, &n) == 0) ? 1 : 0;
}

static int str_starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s != *prefix) return 0;
        ++s;
        ++prefix;
    }
    return 1;
}

static int desktop_conf_path_for_root(const char* root, char* out, size_t out_cap) {
    return path_from_root(out, out_cap, root, "/conf/desktop.conf");
}

static int parse_conf_bool_true(const char* v) {
    if (!v || !v[0]) return 0;
    if (strcmp(v, "1") == 0) return 1;
    if (strcmp(v, "true") == 0) return 1;
    if (strcmp(v, "yes") == 0) return 1;
    if (strcmp(v, "on") == 0) return 1;
    return 0;
}

static int desktop_conf_installed_from_path(const char* conf_path, int* out_installed) {
    char buf[DESK_CONF_MAX];
    uint64_t len = 0;
    char* cur;
    if (out_installed) *out_installed = 0;
    if (!conf_path || !conf_path[0]) return -1;
    if (sys_fs_exists(conf_path) <= 0) return -1;
    if (sys_fs_read_file(conf_path, buf, sizeof(buf) - 1u, &len) != 0) return -1;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1u;
    buf[len] = '\0';
    cur = buf;
    while (*cur) {
        char* line = cur;
        while (*cur && *cur != '\n' && *cur != '\r') cur++;
        if (*cur == '\r') *cur++ = '\0';
        if (*cur == '\n') *cur++ = '\0';
        if (strncmp(line, "installed=", 10) == 0) {
            int is_true = parse_conf_bool_true(line + 10);
            if (out_installed) *out_installed = is_true;
            return 0;
        }
    }
    return -1;
}

static int desktop_conf_write_for_root(const char* root, int installed) {
    char conf_path[DESK_INST_PATH_MAX];
    char conf_dir[DESK_INST_PATH_MAX];
    char desktop_dir[DESK_INST_PATH_MAX];
    char parent[DESK_INST_PATH_MAX];
    char name[DESK_INST_PATH_MAX];
    char out[DESK_CONF_MAX];
    int len;
    if (!root || !root[0]) return -1;
    if (path_from_root(conf_dir, sizeof(conf_dir), root, "/conf") != 0) return -1;
    if (path_from_root(desktop_dir, sizeof(desktop_dir), root, "/desktop") != 0) return -1;
    if (desktop_conf_path_for_root(root, conf_path, sizeof(conf_path)) != 0) return -1;
    (void)sys_fs_mkdir(root, "conf");
    (void)sys_fs_mkdir(root, "desktop");
    len = snprintf(out, sizeof(out),
                   "version=2\ninstalled=%s\ndesktop_dir=%s\nwallpaper=gradient\nbuiltin_bg=true\n",
                   installed ? "true" : "false", desktop_dir);
    if (len <= 0 || (size_t)len >= sizeof(out)) return -1;
    if (sys_fs_exists(conf_path) > 0) {
        return (sys_fs_write_file(conf_path, out, (uint64_t)len) == 0) ? 0 : -1;
    }
    if (split_parent_name(conf_path, parent, name, sizeof(parent)) != 0) return -1;
    return (sys_fs_create_file(parent, name, out, (uint64_t)len) == 0) ? 0 : -1;
}

static int root_has_desktop_conf(const char* root) {
    char conf_path[DESK_INST_PATH_MAX];
    if (desktop_conf_path_for_root(root, conf_path, sizeof(conf_path)) != 0) return 0;
    return sys_fs_exists(conf_path) > 0 ? 1 : 0;
}

static void setup_desktop_storage(void) {
    g_desktop_conf_ready = 0;
    strncpy(g_user_store_root, "/home", sizeof(g_user_store_root) - 1);
    g_user_store_root[sizeof(g_user_store_root) - 1] = '\0';
    strncpy(g_desktop_store_root, "/", sizeof(g_desktop_store_root) - 1);
    g_desktop_store_root[sizeof(g_desktop_store_root) - 1] = '\0';
    strncpy(g_desktop_dir, "/desktop", sizeof(g_desktop_dir) - 1);
    g_desktop_dir[sizeof(g_desktop_dir) - 1] = '\0';
    strncpy(g_desktop_conf_dir, "/etc", sizeof(g_desktop_conf_dir) - 1);
    g_desktop_conf_dir[sizeof(g_desktop_conf_dir) - 1] = '\0';
    strncpy(g_desktop_conf_path, "/etc/desktop.conf", sizeof(g_desktop_conf_path) - 1);
    g_desktop_conf_path[sizeof(g_desktop_conf_path) - 1] = '\0';
    (void)sys_fs_mkdir("/", "desktop");
    (void)sys_fs_mkdir("/", "tmp");
    (void)sys_fs_mkdir("/home", ".ntux");
    g_desktop_conf_ready = 1;
}

static void users_set_default_local(void) {
    g_user_count = 1;
    strncpy(g_users[0].name, "liveuser", sizeof(g_users[0].name) - 1);
    g_users[0].name[sizeof(g_users[0].name) - 1] = '\0';
    strncpy(g_users[0].pass, "1234", sizeof(g_users[0].pass) - 1);
    g_users[0].pass[sizeof(g_users[0].pass) - 1] = '\0';
    g_users[0].uid = 1000u;
}

static int users_save_db(void) {
    char buf[1024];
    size_t p = 0;
    char path[128];
    char parent[128];
    char name[64];
    if (sys_fs_exists(g_user_store_root) <= 0) return -1;
    (void)sys_fs_mkdir(g_user_store_root, ".ntux");
    buf[0] = '\0';
    for (int i = 0; i < g_user_count; ++i) {
        int n = snprintf(buf + p, sizeof(buf) - p, "%s:%s:%u\n",
                         g_users[i].name, g_users[i].pass, g_users[i].uid);
        if (n <= 0 || p + (size_t)n >= sizeof(buf)) break;
        p += (size_t)n;
    }
    path[0] = '\0';
    (void)str_append(path, sizeof(path), g_user_store_root);
    (void)str_append(path, sizeof(path), "/.ntux/users.db");
    if (sys_fs_exists(path) > 0) return (sys_fs_write_file(path, buf, p) == 0) ? 0 : -1;
    if (split_parent_name(path, parent, name, sizeof(parent)) != 0) return -1;
    return (sys_fs_create_file(parent, name, buf, p) == 0) ? 0 : -1;
}

static int users_load_db(void) {
    char path[128];
    char buf[1024];
    uint64_t len = 0;
    g_user_count = 0;
    users_set_default_local();
    if (sys_fs_exists(g_user_store_root) <= 0) {
        return -1;
    }
    (void)sys_fs_mkdir(g_user_store_root, ".ntux");
    path[0] = '\0';
    (void)str_append(path, sizeof(path), g_user_store_root);
    (void)str_append(path, sizeof(path), "/.ntux/users.db");
    if (sys_fs_exists(path) <= 0) {
        (void)users_save_db();
        return -1;
    }
    if (sys_fs_read_file(path, buf, sizeof(buf) - 1u, &len) != 0) return -1;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1u;
    buf[len] = '\0';
    g_user_count = 0;
    char* cur = buf;
    while (*cur && g_user_count < DESK_MAX_USERS) {
        char* line = cur;
        while (*cur && *cur != '\n') cur++;
        if (*cur == '\n') *cur++ = '\0';
        if (!line[0]) continue;
        char* c1 = line;
        while (*c1 && *c1 != ':') c1++;
        if (!*c1) continue;
        *c1++ = '\0';
        char* c2 = c1;
        while (*c2 && *c2 != ':') c2++;
        if (!*c2) continue;
        *c2++ = '\0';
        strncpy(g_users[g_user_count].name, line, sizeof(g_users[g_user_count].name) - 1);
        g_users[g_user_count].name[sizeof(g_users[g_user_count].name) - 1] = '\0';
        strncpy(g_users[g_user_count].pass, c1, sizeof(g_users[g_user_count].pass) - 1);
        g_users[g_user_count].pass[sizeof(g_users[g_user_count].pass) - 1] = '\0';
        g_users[g_user_count].uid = (uint32_t)atoi(c2);
        if (!g_users[g_user_count].uid) g_users[g_user_count].uid = 1000u + (uint32_t)g_user_count;
        g_user_count++;
    }
    if (g_user_count == 0) {
        users_set_default_local();
        (void)users_save_db();
    }
    return 0;
}

static int users_authenticate(const char* name, const char* pass, uint32_t* out_uid) {
    if (!name || !pass) return -1;
    for (int i = 0; i < g_user_count; ++i) {
        if (strcmp(name, g_users[i].name) == 0 && strcmp(pass, g_users[i].pass) == 0) {
            if (out_uid) *out_uid = g_users[i].uid;
            return 0;
        }
    }
    return -1;
}

static int users_add_account(const char* name, const char* pass) {
    uint32_t max_uid = 1000u;
    if (!name || !pass || !name[0] || !pass[0]) return -1;
    if (g_user_count >= DESK_MAX_USERS) return -1;
    for (int i = 0; i < g_user_count; ++i) {
        if (strcmp(g_users[i].name, name) == 0) return -1;
        if (g_users[i].uid > max_uid) max_uid = g_users[i].uid;
    }
    strncpy(g_users[g_user_count].name, name, sizeof(g_users[g_user_count].name) - 1);
    g_users[g_user_count].name[sizeof(g_users[g_user_count].name) - 1] = '\0';
    strncpy(g_users[g_user_count].pass, pass, sizeof(g_users[g_user_count].pass) - 1);
    g_users[g_user_count].pass[sizeof(g_users[g_user_count].pass) - 1] = '\0';
    g_users[g_user_count].uid = max_uid + 1u;
    g_user_count++;
    return users_save_db();
}

static int desktop_conf_is_installed(void) {
    int installed = 0;
    if (desktop_conf_installed_from_path(g_desktop_conf_path, &installed) != 0) return 0;
    return installed ? 1 : 0;
}

#if DESK_ENABLE_INSTALLER
static int desk_inst_is_source_name(const char* name) {
    if (!name || !name[0]) return 0;
    return str_starts_with(name, "iso") || str_starts_with(name, "cd");
}

static int desk_inst_is_target_name(const char* name) {
    if (!name || !name[0]) return 0;
    if (str_starts_with(name, "fat")) return 1;
    if (str_starts_with(name, "disk")) return 1;
    if (str_starts_with(name, "ext")) return 1;
    if (str_starts_with(name, "drive")) return 1;
    return 0;
}

static void desk_inst_log(char logs[DESK_INST_LOG_LINES][96], int* log_count, const char* line) {
    if (!logs || !log_count || !line) return;
    if (*log_count < DESK_INST_LOG_LINES) {
        strncpy(logs[*log_count], line, 95);
        logs[*log_count][95] = '\0';
        (*log_count)++;
        return;
    }
    for (int i = 1; i < DESK_INST_LOG_LINES; ++i) {
        memcpy(logs[i - 1], logs[i], 96);
    }
    strncpy(logs[DESK_INST_LOG_LINES - 1], line, 95);
    logs[DESK_INST_LOG_LINES - 1][95] = '\0';
}

static int desk_inst_find_root_for_drive_part(uint64_t drive, uint64_t part, char out[DESK_INST_PATH_MAX]) {
    char probe[DESK_INST_PATH_MAX];
    ntux_dirent_t one[1];
    uint64_t n = 0;
    char letter = (char)('a' + (drive % 26u));
    int rc = snprintf(probe, sizeof(probe), "/mnt/sd%c%llu", letter, (unsigned long long)part);
    if (rc > 0 && (size_t)rc < sizeof(probe) && sys_fs_list_dir(probe, one, 1, &n) == 0) {
        strncpy(out, probe, DESK_INST_PATH_MAX - 1);
        out[DESK_INST_PATH_MAX - 1] = '\0';
        return 0;
    }
    return -1;
}

static int desk_inst_find_source_mount(char out[DESK_INST_PATH_MAX]) {
    uint64_t count = 0;
    out[0] = '\0';
    if (sys_fs_list_dir("/", g_inst_dirents, DESK_INST_LS_MAX, &count) != 0) return -1;
    if (count > DESK_INST_LS_MAX) count = DESK_INST_LS_MAX;
    for (uint64_t i = 0; i < count; ++i) {
        if (!g_inst_dirents[i].is_dir) continue;
        if (!desk_inst_is_source_name(g_inst_dirents[i].name)) continue;
        if (join_path("/", g_inst_dirents[i].name, out, DESK_INST_PATH_MAX) == 0) return 0;
    }
    return -1;
}

static int desk_inst_collect_devices(uint64_t drive_ids[8], ntux_block_device_info_t devs[8], int* out_count) {
    ntux_block_device_info_t all[8];
    uint64_t count = 0;
    int k = 0;
    if (out_count) *out_count = 0;
    if (sys_block_list(all, 8u, &count) != 0) return -1;
    if (count > 8u) count = 8u;
    for (uint64_t i = 0; i < count; ++i) {
        if (!all[i].present || all[i].is_atapi) continue;
        if (k >= 8) break;
        drive_ids[k] = i;
        memcpy(&devs[k], &all[i], sizeof(devs[k]));
        k++;
    }
    if (out_count) *out_count = k;
    return 0;
}

static int desk_inst_wipe_and_partition(uint64_t drive, uint64_t sectors,
                                        uint64_t* out_lba_start, uint64_t* out_part_sectors) {
    ntux_mbr_part_req_t req;
    uint8_t* zeros = 0;
    uint64_t wipe_secs = 2048u;
    if (sectors <= 4096u) return -1;
    if (wipe_secs > sectors) wipe_secs = sectors;
    memset(&req, 0, sizeof(req));
    req.drive = (uint8_t)drive;
    for (uint8_t p = 1; p <= 4; ++p) {
        req.part_index = p;
        if (sys_block_set_mbr_partition(&req) != 0) return -1;
    }
    zeros = (uint8_t*)malloc(512u * 128u);
    if (!zeros) return -1;
    memset(zeros, 0, 512u * 128u);
    for (uint64_t at = 0; at < wipe_secs; at += 128u) {
        uint64_t step = (wipe_secs - at > 128u) ? 128u : (wipe_secs - at);
        if (sys_block_write(drive, at, step, zeros) != 0) {
            free(zeros);
            return -1;
        }
    }
    free(zeros);
    memset(&req, 0, sizeof(req));
    req.drive = (uint8_t)drive;
    req.part_index = 1;
    req.type = 0x83u;
    req.bootable = 0;
    req.lba_start = 2048u;
    req.sectors = (uint32_t)(sectors > (uint64_t)0xFFFFFFFFu + 2048u ? 0xFFFFFFFFu : (uint32_t)(sectors - 2048u));
    if (req.sectors < 8192u) return -1;
    if (sys_block_set_mbr_partition(&req) != 0) return -1;
    if (sys_mkfs_ext4(drive, req.lba_start, req.sectors) != 0) return -1;
    if (sys_fs_rescan() != 0) return -1;
    if (out_lba_start) *out_lba_start = req.lba_start;
    if (out_part_sectors) *out_part_sectors = req.sectors;
    return 0;
}

static int desk_inst_copy_file(const char* src, const char* dst) {
    uint64_t len = 0;
    char parent[DESK_INST_PATH_MAX];
    char name[DESK_INST_PATH_MAX];
    void* buf = 0;
    if (!src || !src[0] || !dst || !dst[0]) return -1;
    if (sys_fs_read_file(src, 0, 0, &len) != 0) return -1;
    if (len > DESK_INST_FILE_MAX) {
        return -1;
    }
    if (split_parent_name(dst, parent, name, sizeof(parent)) != 0) return -1;
    if (len > 0) {
        buf = malloc((size_t)len);
        if (!buf) return -1;
        if (sys_fs_read_file(src, buf, len, &len) != 0) {
            free(buf);
            return -1;
        }
    }
    if (sys_fs_exists(dst) > 0) {
        long rc = sys_fs_write_file(dst, buf, len);
        if (buf) free(buf);
        return (rc == 0) ? 0 : -1;
    }
    long rc = sys_fs_create_file(parent, name, buf, len);
    if (buf) free(buf);
    return (rc == 0) ? 0 : -1;
}

static int desk_inst_should_skip_on_install(const char* src_path) {
    if (!src_path) return 0;
    if (str_ends_with(src_path, "/boot/modules/installer.elf")) return 1;
    if (str_ends_with(src_path, "/boot/modules/INSTALLER.ELF")) return 1;
    return 0;
}

static int desk_inst_dir_exists(const char* path) {
    ntux_dirent_t probe[1];
    uint64_t n = 0;
    if (!path || !path[0]) return 0;
    return (sys_fs_list_dir(path, probe, 1, &n) == 0) ? 1 : 0;
}

static int desk_inst_mkdir_full(const char* path) {
    char parent[DESK_INST_PATH_MAX];
    char name[DESK_INST_PATH_MAX];
    if (sys_fs_exists(path) > 0) return 0;
    if (split_parent_name(path, parent, name, sizeof(parent)) != 0) return -1;
    return (sys_fs_mkdir(parent, name) == 0) ? 0 : -1;
}

static int desk_inst_copy_tree_if_exists(const char* src, const char* dst) {
    if (!desk_inst_dir_exists(src)) return 0;
    if (sys_fs_exists(dst) <= 0) {
        if (desk_inst_mkdir_full(dst) != 0) return -1;
    }
    return desk_inst_copy_tree(src, dst);
}

static void desk_inst_progress_update(int force) {
    uint64_t total = g_inst_copy_total_bytes ? g_inst_copy_total_bytes : g_inst_copy_total;
    uint64_t done = g_inst_copy_total_bytes ? g_inst_copy_done_bytes : g_inst_copy_done;
    if (total == 0) return;
    int percent = (int)((done * 100u) / total);
    if (!force && percent == g_inst_copy_last_percent) return;
    g_inst_copy_last_percent = percent;
    g_inst_phase = 2;
    if (g_bg) {
        memcpy(g_frame, g_bg, g_pixels * sizeof(uint32_t));
    } else {
        bg_gradient_to(g_frame);
    }
    desk_inst_draw_ui(g_inst_drive_ids, g_inst_devs, g_inst_dev_count,
                      g_inst_selected, g_inst_focus_btn,
                      g_inst_source, g_inst_logs, g_inst_log_count);
    draw_cursor();
    (void)sys_fb_blit32(g_frame, g_fb.width, g_fb.height, g_fb.width * 4u);
}

static int desk_inst_scan_tree(const char* src, uint64_t* out_files, uint64_t* out_bytes) {
    uint64_t count = 0;
    int rc = -1;
    char names[DESK_INST_LS_MAX][64];
    uint8_t kinds[DESK_INST_LS_MAX];
    uint64_t sizes[DESK_INST_LS_MAX];
    if (sys_fs_list_dir(src, g_inst_dirents, DESK_INST_LS_MAX, &count) != 0) return -1;
    if (count > DESK_INST_LS_MAX) count = DESK_INST_LS_MAX;
    for (uint64_t i = 0; i < count; ++i) {
        if ((i & 0x3Fu) == 0u) {
            sys_yield();
        }
        strncpy(names[i], g_inst_dirents[i].name, sizeof(names[i]) - 1);
        names[i][sizeof(names[i]) - 1] = '\0';
        kinds[i] = g_inst_dirents[i].is_dir ? 1u : 0u;
        sizes[i] = g_inst_dirents[i].size;
    }
    for (uint64_t i = 0; i < count; ++i) {
        char src_child[DESK_INST_PATH_MAX];
        if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0) continue;
        if (join_path(src, names[i], src_child, sizeof(src_child)) != 0) goto cleanup;
        if (kinds[i]) {
            if (desk_inst_scan_tree(src_child, out_files, out_bytes) != 0) goto cleanup;
        } else {
            if (desk_inst_should_skip_on_install(src_child)) continue;
            if (out_files) (*out_files)++;
            if (out_bytes) (*out_bytes) += sizes[i];
        }
    }
    rc = 0;

cleanup:
    return rc;
}

static int desk_inst_remove_tree(const char* path) {
    uint64_t count = 0;
    char names[DESK_INST_LS_MAX][64];
    uint8_t kinds[DESK_INST_LS_MAX];
    if (!path || !path[0]) return -1;
    if (sys_fs_list_dir(path, g_inst_dirents, DESK_INST_LS_MAX, &count) != 0) return -1;
    if (count > DESK_INST_LS_MAX) count = DESK_INST_LS_MAX;
    for (uint64_t i = 0; i < count; ++i) {
        strncpy(names[i], g_inst_dirents[i].name, sizeof(names[i]) - 1);
        names[i][sizeof(names[i]) - 1] = '\0';
        kinds[i] = g_inst_dirents[i].is_dir ? 1u : 0u;
    }
    for (uint64_t i = 0; i < count; ++i) {
        if ((i & 0x3Fu) == 0u) {
            sys_yield();
        }
        char child[DESK_INST_PATH_MAX];
        if (join_path(path, names[i], child, sizeof(child)) != 0) return -1;
        if (kinds[i]) {
            if (desk_inst_remove_tree(child) != 0) return -1;
        }
        if (sys_fs_remove(child) != 0) return -1;
    }
    return 0;
}

static int desk_inst_copy_tree(const char* src, const char* dst) {
    uint64_t count = 0;
    char names[DESK_INST_LS_MAX][64];
    uint8_t kinds[DESK_INST_LS_MAX];
    uint64_t sizes[DESK_INST_LS_MAX];
    if (!src || !src[0] || !dst || !dst[0]) return -1;
    if (sys_fs_list_dir(src, g_inst_dirents, DESK_INST_LS_MAX, &count) != 0) return -1;
    if (count > DESK_INST_LS_MAX) count = DESK_INST_LS_MAX;
    for (uint64_t i = 0; i < count; ++i) {
        if ((i & 0x3Fu) == 0u) {
            sys_yield();
        }
        strncpy(names[i], g_inst_dirents[i].name, sizeof(names[i]) - 1);
        names[i][sizeof(names[i]) - 1] = '\0';
        kinds[i] = g_inst_dirents[i].is_dir ? 1u : 0u;
        sizes[i] = g_inst_dirents[i].size;
    }
    for (uint64_t i = 0; i < count; ++i) {
        if ((i & 0x3Fu) == 0u) {
            sys_yield();
        }
        char src_child[DESK_INST_PATH_MAX];
        char dst_child[DESK_INST_PATH_MAX];
        if (join_path(src, names[i], src_child, sizeof(src_child)) != 0) return -1;
        if (join_path(dst, names[i], dst_child, sizeof(dst_child)) != 0) return -1;
        if (kinds[i]) {
            char parent[DESK_INST_PATH_MAX];
            char name[DESK_INST_PATH_MAX];
            if (split_parent_name(dst_child, parent, name, sizeof(parent)) != 0) return -1;
            if (sys_fs_exists(dst_child) <= 0 && sys_fs_mkdir(parent, name) != 0) return -1;
            if (desk_inst_copy_tree(src_child, dst_child) != 0) return -1;
            continue;
        }
        if (desk_inst_should_skip_on_install(src_child)) {
            continue;
        }
        if (desk_inst_copy_file(src_child, dst_child) != 0) return -1;
        g_inst_copy_done++;
        g_inst_copy_done_bytes += sizes[i];
        desk_inst_progress_update(0);
    }
    return 0;
}

static int desk_inst_line_contains(const char* line, size_t line_len, const char* needle) {
    size_t nl = strlen(needle);
    if (!line || !needle || nl == 0 || line_len < nl) return 0;
    for (size_t i = 0; i + nl <= line_len; ++i) {
        if (memcmp(line + i, needle, nl) == 0) return 1;
    }
    return 0;
}

static int desk_inst_patch_limine_conf(const char* target_root,
                                       char logs[DESK_INST_LOG_LINES][96], int* log_count) {
    char path[DESK_INST_PATH_MAX];
    if (join_path(target_root, "boot/limine/limine.conf", path, sizeof(path)) != 0) return -1;

    uint64_t len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0) {
        desk_inst_log(logs, log_count, "[warn] limine.conf missing");
        return -1;
    }
    if (len == 0 || len > DESK_INST_FILE_MAX) return -1;

    char* buf = (char*)malloc((size_t)len + 1u);
    if (!buf) return -1;
    if (sys_fs_read_file(path, buf, len, &len) != 0) {
        free(buf);
        return -1;
    }
    buf[len] = '\0';

    char* out = (char*)malloc((size_t)len + 1u);
    if (!out) {
        free(buf);
        return -1;
    }

    size_t op = 0;
    size_t line_start = 0;
    int removed = 0;
    for (size_t i = 0; i <= len; ++i) {
        char c = buf[i];
        if (c == '\n' || c == '\0') {
            size_t line_len = i - line_start;
            const char* line = buf + line_start;
            int skip = 0;
            if (line_len > 0) {
                if (desk_inst_line_contains(line, line_len, "module_path:") &&
                    desk_inst_line_contains(line, line_len, "installer.elf")) {
                    skip = 1;
                }
                if (desk_inst_line_contains(line, line_len, "module_string:") &&
                    desk_inst_line_contains(line, line_len, "installer")) {
                    skip = 1;
                }
            }
            if (!skip) {
                if (op + line_len + 1u < DESK_INST_FILE_MAX) {
                    memcpy(out + op, line, line_len);
                    op += line_len;
                    if (c == '\n') out[op++] = '\n';
                }
            } else {
                removed = 1;
            }
            line_start = i + 1;
        }
    }
    out[op] = '\0';

    if (sys_fs_write_file(path, out, op) == 0) {
        if (removed) desk_inst_log(logs, log_count, "[ok] limine.conf: installer removed");
        else desk_inst_log(logs, log_count, "[warn] limine.conf: installer not found");
    } else {
        desk_inst_log(logs, log_count, "[warn] limine.conf patch failed");
    }

    free(out);
    free(buf);
    return 0;
}

static void desk_inst_remove_installer_on_target(const char* target_root,
                                                 char logs[DESK_INST_LOG_LINES][96], int* log_count) {
    char path[DESK_INST_PATH_MAX];
    if (join_path(target_root, "boot/modules/installer.elf", path, sizeof(path)) == 0) {
        (void)sys_fs_remove(path);
    }
    if (join_path(target_root, "boot/modules/INSTALLER.ELF", path, sizeof(path)) == 0) {
        (void)sys_fs_remove(path);
    }
    desk_inst_log(logs, log_count, "[dbg] removed installer module");
}

static int desk_inst_wipe_range(uint64_t drive, uint64_t start, uint64_t sectors) {
    uint8_t buf[512 * 8];
    memset(buf, 0, sizeof(buf));
    uint64_t left = sectors;
    uint64_t at = start;
    while (left > 0) {
        uint64_t step = (left > 8u) ? 8u : left;
        if (sys_block_write(drive, at, step, buf) != 0) {
            for (uint64_t s = 0; s < step; ++s) {
                if (sys_block_write(drive, at + s, 1, buf) != 0) return -1;
            }
        }
        at += step;
        left -= step;
    }
    return 0;
}

static uint32_t desk_inst_crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    uint32_t c = ~crc;
    for (size_t i = 0; i < len; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k) {
            uint32_t mask = (uint32_t)-(int)(c & 1u);
            c = (c >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~c;
}

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sectors;
} desk_inst_mbr_part_t;

typedef struct __attribute__((packed)) {
    uint8_t boot[446];
    desk_inst_mbr_part_t part[4];
    uint16_t sig;
} desk_inst_mbr_sector_t;

typedef struct __attribute__((packed)) {
    uint8_t signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t number_of_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_entry_array_crc32;
} desk_inst_gpt_header_t;

static void desk_inst_gpt_write_le_guid(uint8_t out[16], const uint8_t in[16]) {
    out[0] = in[3];
    out[1] = in[2];
    out[2] = in[1];
    out[3] = in[0];
    out[4] = in[5];
    out[5] = in[4];
    out[6] = in[7];
    out[7] = in[6];
    for (int i = 8; i < 16; ++i) out[i] = in[i];
}

static int desk_inst_write_gpt_single(uint64_t drive, uint64_t total,
                                      uint64_t* out_lba, uint64_t* out_sectors) {
    if (total < 2048u + 34u) return -1;
    uint64_t first_usable = 34u;
    uint64_t last_usable = total - 34u;
    uint64_t start = 2048u;
    if (start < first_usable) start = first_usable;
    if (start > last_usable) start = first_usable;
    uint64_t end = last_usable;
    if (end <= start) return -1;

    desk_inst_mbr_sector_t pmbr;
    memset(&pmbr, 0, sizeof(pmbr));
    pmbr.part[0].type = 0xEE;
    pmbr.part[0].lba_first = 1u;
    pmbr.part[0].sectors = (total - 1u > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)(total - 1u);
    pmbr.sig = 0xAA55u;
    if (sys_block_write(drive, 0, 1, &pmbr) != 0) return -1;

    uint8_t part_array[512 * 32];
    memset(part_array, 0, sizeof(part_array));

    const uint8_t basic_data_guid[16] = {0xEB,0xD0,0xA0,0xA2,0xB9,0xE5,0x44,0x33,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7};
    uint8_t* ent = part_array;
    desk_inst_gpt_write_le_guid(ent, basic_data_guid);
    uint8_t uniq_guid[16] = {0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0,0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0};
    desk_inst_gpt_write_le_guid(ent + 16, uniq_guid);
    memcpy(ent + 32, &start, sizeof(start));
    memcpy(ent + 40, &end, sizeof(end));
    const char* name = "NTux System";
    for (int i = 0; name[i] && i < 36; ++i) {
        ent[56 + i * 2] = (uint8_t)name[i];
        ent[56 + i * 2 + 1] = 0;
    }

    uint32_t part_crc = desk_inst_crc32_update(0, part_array, sizeof(part_array));

    desk_inst_gpt_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.signature, "EFI PART", 8);
    hdr.revision = 0x00010000u;
    hdr.header_size = 92u;
    hdr.current_lba = 1u;
    hdr.backup_lba = total - 1u;
    hdr.first_usable_lba = first_usable;
    hdr.last_usable_lba = last_usable;
    const uint8_t disk_guid[16] = {0xA1,0xB2,0xC3,0xD4,0xE5,0xF6,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0xA0};
    desk_inst_gpt_write_le_guid(hdr.disk_guid, disk_guid);
    hdr.partition_entry_lba = 2u;
    hdr.number_of_partition_entries = 128u;
    hdr.size_of_partition_entry = 128u;
    hdr.partition_entry_array_crc32 = part_crc;
    hdr.header_crc32 = 0;
    hdr.header_crc32 = desk_inst_crc32_update(0, (const uint8_t*)&hdr, hdr.header_size);

    uint8_t hdr_sector[512];
    memset(hdr_sector, 0, sizeof(hdr_sector));
    memcpy(hdr_sector, &hdr, sizeof(hdr));
    if (sys_block_write(drive, 1, 1, hdr_sector) != 0) return -1;
    if (sys_block_write(drive, 2, 32, part_array) != 0) return -1;

    uint64_t backup_part_lba = total - 33u;
    if (sys_block_write(drive, backup_part_lba, 32, part_array) != 0) return -1;

    desk_inst_gpt_header_t bhdr = hdr;
    bhdr.current_lba = total - 1u;
    bhdr.backup_lba = 1u;
    bhdr.partition_entry_lba = backup_part_lba;
    bhdr.header_crc32 = 0;
    bhdr.header_crc32 = desk_inst_crc32_update(0, (const uint8_t*)&bhdr, bhdr.header_size);
    memset(hdr_sector, 0, sizeof(hdr_sector));
    memcpy(hdr_sector, &bhdr, sizeof(bhdr));
    if (sys_block_write(drive, total - 1u, 1, hdr_sector) != 0) return -1;

    if (out_lba) *out_lba = start;
    if (out_sectors) *out_sectors = end - start + 1u;
    return 0;
}

static int desk_inst_write_gpt_dual(uint64_t drive, uint64_t total,
                                    uint64_t* out_esp_lba, uint64_t* out_esp_secs,
                                    uint64_t* out_sys_lba, uint64_t* out_sys_secs) {
    if (total < 2048u + 34u + 2048u) return -1;
    uint64_t first_usable = 34u;
    uint64_t last_usable = total - 34u;
    uint64_t start = 2048u;
    if (start < first_usable) start = first_usable;
    if (start > last_usable) start = first_usable;
    if (last_usable <= start + 2048u) return -1;

    uint64_t esp_secs = 65536u;
    if (esp_secs + start > last_usable) {
        esp_secs = (last_usable - start) / 4u;
    }
    if (esp_secs < 8192u) return -1;
    uint64_t esp_start = start;
    uint64_t esp_end = esp_start + esp_secs - 1u;

    uint64_t sys_start = esp_end + 1u;
    if (sys_start < start) sys_start = start;
    if (sys_start > last_usable) return -1;
    uint64_t sys_end = last_usable;

    desk_inst_mbr_sector_t pmbr;
    memset(&pmbr, 0, sizeof(pmbr));
    pmbr.part[0].type = 0xEE;
    pmbr.part[0].lba_first = 1u;
    pmbr.part[0].sectors = (total - 1u > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)(total - 1u);
    pmbr.sig = 0xAA55u;
    if (sys_block_write(drive, 0, 1, &pmbr) != 0) return -1;

    uint8_t part_array[512 * 32];
    memset(part_array, 0, sizeof(part_array));

    const uint8_t guid_esp[16] = {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
    const uint8_t guid_linux[16] = {0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4};

    uint8_t* ent0 = part_array;
    desk_inst_gpt_write_le_guid(ent0, guid_esp);
    uint8_t uniq0[16] = {0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE};
    desk_inst_gpt_write_le_guid(ent0 + 16, uniq0);
    memcpy(ent0 + 32, &esp_start, sizeof(esp_start));
    memcpy(ent0 + 40, &esp_end, sizeof(esp_end));
    const char* name0 = "NTux ESP";
    for (int i = 0; name0[i] && i < 36; ++i) {
        ent0[56 + i * 2] = (uint8_t)name0[i];
        ent0[56 + i * 2 + 1] = 0;
    }

    uint8_t* ent1 = part_array + 128;
    desk_inst_gpt_write_le_guid(ent1, guid_linux);
    uint8_t uniq1[16] = {0x21,0x43,0x65,0x87,0xA9,0xCB,0xED,0x0F,0x21,0x43,0x65,0x87,0xA9,0xCB,0xED,0x0F};
    desk_inst_gpt_write_le_guid(ent1 + 16, uniq1);
    memcpy(ent1 + 32, &sys_start, sizeof(sys_start));
    memcpy(ent1 + 40, &sys_end, sizeof(sys_end));
    const char* name1 = "NTux System";
    for (int i = 0; name1[i] && i < 36; ++i) {
        ent1[56 + i * 2] = (uint8_t)name1[i];
        ent1[56 + i * 2 + 1] = 0;
    }

    uint32_t part_crc = desk_inst_crc32_update(0, part_array, sizeof(part_array));

    desk_inst_gpt_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.signature, "EFI PART", 8);
    hdr.revision = 0x00010000u;
    hdr.header_size = 92u;
    hdr.current_lba = 1u;
    hdr.backup_lba = total - 1u;
    hdr.first_usable_lba = first_usable;
    hdr.last_usable_lba = last_usable;
    const uint8_t disk_guid[16] = {0xB1,0xC2,0xD3,0xE4,0xF5,0x06,0x17,0x28,0x39,0x4A,0x5B,0x6C,0x7D,0x8E,0x9F,0xA0};
    desk_inst_gpt_write_le_guid(hdr.disk_guid, disk_guid);
    hdr.partition_entry_lba = 2u;
    hdr.number_of_partition_entries = 128u;
    hdr.size_of_partition_entry = 128u;
    hdr.partition_entry_array_crc32 = part_crc;
    hdr.header_crc32 = 0;
    hdr.header_crc32 = desk_inst_crc32_update(0, (const uint8_t*)&hdr, hdr.header_size);

    uint8_t hdr_sector[512];
    memset(hdr_sector, 0, sizeof(hdr_sector));
    memcpy(hdr_sector, &hdr, sizeof(hdr));
    if (sys_block_write(drive, 1, 1, hdr_sector) != 0) return -1;
    if (sys_block_write(drive, 2, 32, part_array) != 0) return -1;

    uint64_t backup_part_lba = total - 33u;
    if (sys_block_write(drive, backup_part_lba, 32, part_array) != 0) return -1;

    desk_inst_gpt_header_t bhdr = hdr;
    bhdr.current_lba = total - 1u;
    bhdr.backup_lba = 1u;
    bhdr.partition_entry_lba = backup_part_lba;
    bhdr.header_crc32 = 0;
    bhdr.header_crc32 = desk_inst_crc32_update(0, (const uint8_t*)&bhdr, bhdr.header_size);
    memset(hdr_sector, 0, sizeof(hdr_sector));
    memcpy(hdr_sector, &bhdr, sizeof(bhdr));
    if (sys_block_write(drive, total - 1u, 1, hdr_sector) != 0) return -1;

    if (out_esp_lba) *out_esp_lba = esp_start;
    if (out_esp_secs) *out_esp_secs = esp_end - esp_start + 1u;
    if (out_sys_lba) *out_sys_lba = sys_start;
    if (out_sys_secs) *out_sys_secs = sys_end - sys_start + 1u;
    return 0;
}

static int desk_inst_perform_install(uint64_t drive, uint64_t drive_sectors, int force_repartition,
                                     char logs[DESK_INST_LOG_LINES][96], int* log_count) {
    char source[DESK_INST_PATH_MAX];
    char target[DESK_INST_PATH_MAX];
    char esp_target[DESK_INST_PATH_MAX];
    ntux_partition_info_t parts[8];
    uint64_t part_count = 0;
    uint64_t target_part = 0;
    uint64_t esp_part = 0;
    int have_part = 0;
    uint64_t part_lba = 0;
    uint64_t part_secs = 0;
    uint64_t esp_lba = 0;
    uint64_t esp_secs = 0;
    uint8_t fs_type = g_inst_fs_choice;
    int gpt_mode = g_inst_part_scheme ? 1 : 0;
    int use_esp = 0;
    target[0] = '\0';
    desk_inst_log(logs, log_count, "[dbg] scanning source mount...");
    if (desk_inst_find_source_mount(source) != 0) {
        desk_inst_log(logs, log_count, "[err] source mount /iso* not found");
        return -1;
    }
    desk_inst_log(logs, log_count, source);
    if (sys_block_partitions(drive, parts, 8u, &part_count) == 0) {
        if (part_count > 8u) part_count = 8u;
        for (uint64_t i = 0; i < part_count; ++i) {
            if (parts[i].index >= 1 && parts[i].sectors > 0) {
                have_part = 1;
                target_part = parts[i].index;
                break;
            }
        }
    }
    if (fs_type == 2 || fs_type == 4) {
        use_esp = 1;
        gpt_mode = 1;
    }
    if (force_repartition || !have_part) {
        desk_inst_log(logs, log_count, "[dbg] repartition+mkfs starting...");
        if (desk_inst_wipe_range(drive, 0, (drive_sectors > 2048u) ? 2048u : drive_sectors) != 0) {
            desk_inst_log(logs, log_count, "[err] wipe failed");
            return -1;
        }
        if (gpt_mode) {
            if (use_esp) {
                if (desk_inst_write_gpt_dual(drive, drive_sectors, &esp_lba, &esp_secs, &part_lba, &part_secs) != 0) {
                    desk_inst_log(logs, log_count, "[err] GPT dual create failed");
                    return -1;
                }
                esp_part = 1;
                target_part = 2;
            } else {
                if (desk_inst_write_gpt_single(drive, drive_sectors, &part_lba, &part_secs) != 0) {
                    desk_inst_log(logs, log_count, "[err] GPT create failed");
                    return -1;
                }
                target_part = 1;
            }
        } else {
            ntux_mbr_part_req_t req;
            memset(&req, 0, sizeof(req));
            req.drive = (uint8_t)drive;
            for (uint8_t p = 1; p <= 4; ++p) {
                req.part_index = p;
                if (sys_block_set_mbr_partition(&req) != 0) return -1;
            }
            uint32_t start = (drive_sectors > 4096u) ? 2048u : 1u;
            if (drive_sectors <= start + 8u) {
                desk_inst_log(logs, log_count, "[err] drive too small");
                return -1;
            }
            uint32_t usable = (drive_sectors > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)drive_sectors;
            uint32_t secs = (usable > start) ? (usable - start) : 0u;
            uint8_t type = 0x0Cu;
            if (fs_type == 12) type = 0x01u;
            else if (fs_type == 16) type = 0x06u;
            else if (fs_type == 32 || fs_type == 0) type = 0x0Cu;
            else if (fs_type == 2 || fs_type == 4) type = 0x83u;
            req.part_index = 1;
            req.type = type;
            req.bootable = 0;
            req.lba_start = start;
            req.sectors = secs;
            if (req.sectors < 2048u || sys_block_set_mbr_partition(&req) != 0) {
                desk_inst_log(logs, log_count, "[err] MBR partition create failed");
                return -1;
            }
            part_lba = req.lba_start;
            part_secs = req.sectors;
            target_part = 1;
        }

        if (use_esp) {
            if (esp_secs > 0xFFFFFFFFu || sys_mkfs_fat(drive, esp_lba, esp_secs, 0) != 0) {
                desk_inst_log(logs, log_count, "[err] mkfs.fat (ESP) failed");
                return -1;
            }
        }
        if (fs_type == 2) {
            if (sys_mkfs_ext2(drive, part_lba, part_secs) != 0) {
                desk_inst_log(logs, log_count, "[err] mkfs.ext2 failed");
                return -1;
            }
        } else if (fs_type == 4) {
            if (sys_mkfs_ext4(drive, part_lba, part_secs) != 0) {
                desk_inst_log(logs, log_count, "[err] mkfs.ext4 failed");
                return -1;
            }
        } else {
            if (part_secs > 0xFFFFFFFFu) {
                desk_inst_log(logs, log_count, "[err] FAT partition too large");
                return -1;
            }
            if (sys_mkfs_fat(drive, part_lba, part_secs, fs_type) != 0) {
                if (sys_mkfs_fat(drive, part_lba, part_secs, 0) != 0) {
                    desk_inst_log(logs, log_count, "[err] mkfs.fat failed");
                    return -1;
                }
            }
        }
        if (sys_fs_rescan() != 0) {
            desk_inst_log(logs, log_count, "[err] FS rescan failed");
            return -1;
        }
    }
    if (use_esp) {
        if (desk_inst_find_root_for_drive_part(drive, esp_part ? esp_part : 1, esp_target) != 0) {
            desk_inst_log(logs, log_count, "[err] ESP mount not found");
            return -1;
        }
    }
    if (desk_inst_find_root_for_drive_part(drive, target_part ? target_part : 1, target) != 0) {
        desk_inst_log(logs, log_count, "[dbg] trying fallback remount scan...");
        (void)sys_fs_rescan();
        if (desk_inst_find_root_for_drive_part(drive, target_part ? target_part : 1, target) != 0) {
            desk_inst_log(logs, log_count, "[err] target mount not found");
            return -1;
        }
    }
    if (strcmp(source, target) == 0 || (use_esp && strcmp(source, esp_target) == 0)) {
        desk_inst_log(logs, log_count, "[err] source equals target");
        return -1;
    }
    desk_inst_log(logs, log_count, "[dbg] wiping target tree...");
    if (desk_inst_remove_tree(target) != 0) {
        desk_inst_log(logs, log_count, "[err] failed to clear target tree");
        return -1;
    }
    if (use_esp) {
        if (desk_inst_remove_tree(esp_target) != 0) {
            desk_inst_log(logs, log_count, "[err] failed to clear ESP tree");
            return -1;
        }
    }
    desk_inst_log(logs, log_count, "[dbg] copying files...");
    g_inst_copy_total = 0;
    g_inst_copy_done = 0;
    g_inst_copy_total_bytes = 0;
    g_inst_copy_done_bytes = 0;
    g_inst_copy_last_percent = -1;
    g_inst_phase = 1;
    if (desk_inst_scan_tree(source, &g_inst_copy_total, &g_inst_copy_total_bytes) != 0) {
        desk_inst_log(logs, log_count, "[err] scan failed");
        return -1;
    }
    if (g_inst_copy_total == 0) g_inst_copy_total = 1;
    if (g_inst_copy_total_bytes == 0) g_inst_copy_total_bytes = 1;
    desk_inst_progress_update(1);
    if (desk_inst_copy_tree(source, target) != 0) {
        desk_inst_log(logs, log_count, "[err] copy failed");
        return -1;
    }
    if (use_esp) {
        char src_efi[DESK_INST_PATH_MAX];
        char src_lim[DESK_INST_PATH_MAX];
        char src_kernel[DESK_INST_PATH_MAX];
        char src_modules[DESK_INST_PATH_MAX];
        char src_recovery[DESK_INST_PATH_MAX];
        char dst_efi[DESK_INST_PATH_MAX];
        char dst_lim[DESK_INST_PATH_MAX];
        char dst_kernel[DESK_INST_PATH_MAX];
        char dst_modules[DESK_INST_PATH_MAX];
        char dst_recovery[DESK_INST_PATH_MAX];
        if (join_path(source, "EFI", src_efi, sizeof(src_efi)) == 0 &&
            join_path(esp_target, "EFI", dst_efi, sizeof(dst_efi)) == 0) {
            (void)desk_inst_copy_tree_if_exists(src_efi, dst_efi);
        }
        if (join_path(source, "boot/limine", src_lim, sizeof(src_lim)) == 0 &&
            join_path(esp_target, "boot/limine", dst_lim, sizeof(dst_lim)) == 0) {
            (void)desk_inst_copy_tree_if_exists(src_lim, dst_lim);
        }
        if (join_path(source, "kernel", src_kernel, sizeof(src_kernel)) == 0 &&
            join_path(esp_target, "boot/kernel", dst_kernel, sizeof(dst_kernel)) == 0) {
            (void)desk_inst_copy_file(src_kernel, dst_kernel);
        }
        if (join_path(source, "modules", src_modules, sizeof(src_modules)) == 0 &&
            join_path(esp_target, "boot/modules", dst_modules, sizeof(dst_modules)) == 0) {
            (void)desk_inst_copy_tree_if_exists(src_modules, dst_modules);
        }
        if (join_path(source, "recovery", src_recovery, sizeof(src_recovery)) == 0 &&
            join_path(esp_target, "boot/recovery", dst_recovery, sizeof(dst_recovery)) == 0) {
            (void)desk_inst_copy_tree_if_exists(src_recovery, dst_recovery);
        }
        (void)desk_inst_patch_limine_conf(esp_target, logs, log_count);
        desk_inst_remove_installer_on_target(esp_target, logs, log_count);
    }
    desk_inst_log(logs, log_count, "[dbg] writing desktop.conf installed=true");
    if (desktop_conf_write_for_root(target, 1) != 0) {
        desk_inst_log(logs, log_count, "[err] desktop.conf write failed");
        return -1;
    }
    (void)desk_inst_patch_limine_conf(target, logs, log_count);
    desk_inst_remove_installer_on_target(target, logs, log_count);
    desk_inst_log(logs, log_count, "[ok] installation complete");
    g_inst_phase = 0;
    return 0;
}

static void desk_inst_draw_ui(const uint64_t drive_ids[8], const ntux_block_device_info_t devs[8], int dev_count,
                              int selected, int focus_btn, const char* source,
                              const char logs[DESK_INST_LOG_LINES][96], int log_count) {
    const desk_theme_t* th = desk_theme();
    int px = (int)g_fb.width / 2 - 480;
    int py = (int)g_fb.height / 2 - 300;
    int pw = 960;
    int ph = 600;
    fill_rect(px - 18, py - 18, pw + 36, ph + 36, color_lerp(0xFF000000u, th->taskbar_bg, 40u));
    fill_rect(px - 8, py - 8, pw + 16, ph + 16, color_lerp(th->taskbar_bg, 0xFF000000u, 55u));
    fill_rect(px, py, pw, ph, color_lerp(th->window_fill, 0xFF000000u, 22u));
    draw_rect(px, py, pw, ph, th->border_focus);
    for (int i = 0; i < 30; ++i) {
        uint32_t c = color_lerp(0xFF0B1E2Eu, 0xFF1F4E74u, (uint32_t)(i * 100 / 29));
        fill_rect(px + 2, py + 2 + i, pw - 4, 1, c);
    }
    fill_rect(px + 2, py + 32, pw - 4, 2, color_lerp(0xFF2EA6FFu, 0xFF0B1E2Eu, 70u));
    draw_text(px + 16, py + 11, "NTux Setup Wizard", 0xFFF2F7FFu);
    draw_text(px + 300, py + 11, "Detect  •  Select  •  Install", 0xFFB8D3EFu);
    draw_text(px + 620, py + 12, source[0] ? source : "<no source>", th->text_dim);
    draw_text(px + 16, py + 46, "Disks", th->text_main);
    draw_text(px + 420, py + 46, "Debug Log", th->text_main);
    fill_rect(px + 14, py + 64, 380, 456, 0xFF0E1B2Au);
    draw_rect(px + 14, py + 64, 380, 456, th->border_blur);
    fill_rect(px + 410, py + 64, 536, 456, 0xFF101926u);
    draw_rect(px + 410, py + 64, 536, 456, th->border_blur);
    for (int i = 0; i < dev_count && i < 12; ++i) {
        char line[96];
        int ry = py + 74 + i * 34;
        uint32_t row = (i == selected) ? th->accent : color_lerp(th->title_blur, th->window_fill, 80u);
        fill_rect(px + 20, ry, 368, 30, row);
        draw_rect(px + 20, ry, 368, 30, th->taskbar_border);
        line[0] = '\0';
        (void)str_append(line, sizeof(line), "blk");
        (void)str_append_u64(line, sizeof(line), drive_ids[i]);
        (void)str_append(line, sizeof(line), "  ");
        (void)str_append_u64(line, sizeof(line), devs[i].sectors / 2048u);
        (void)str_append(line, sizeof(line), " MiB");
        draw_text(px + 28, ry + 10, line, (i == selected) ? 0xFF061018u : th->text_main);
    }
    for (int i = 0; i < log_count && i < DESK_INST_LOG_LINES; ++i) {
        draw_text(px + 420, py + 74 + i * 30, logs[i], th->text_main);
    }
    {
        uint64_t total = g_inst_copy_total_bytes ? g_inst_copy_total_bytes : g_inst_copy_total;
        uint64_t done = g_inst_copy_total_bytes ? g_inst_copy_done_bytes : g_inst_copy_done;
        int percent = (total == 0) ? 0 : (int)((done * 100u) / total);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        int bar_x = px + 410;
        int bar_y = py + 520;
        int bar_w = 536;
        int bar_h = 6;
        fill_rect(bar_x, bar_y, bar_w, bar_h, 0xFF0B141Fu);
        fill_rect(bar_x, bar_y, (bar_w * percent) / 100, bar_h, 0xFF2EB7FFu);
        draw_rect(bar_x, bar_y, bar_w, bar_h, 0xFF3A4B60u);
        {
            char pct[24];
            pct[0] = '\0';
            (void)str_append(pct, sizeof(pct), "Copy ");
            (void)str_append_i32(pct, sizeof(pct), percent);
            (void)str_append(pct, sizeof(pct), "%");
            draw_text(bar_x + 4, bar_y - 14, pct, 0xFF9FC2DDu);
        }
    }

    {
        int ax = px + 20;
        int ay = py + 520;
        int aw = 368;
        int ah = 6;
        fill_rect(ax, ay, aw, ah, 0xFF0B141Fu);
        draw_rect(ax, ay, aw, ah, 0xFF243041u);
    }

    {
        int bx = px + 20;
        int by = py + 476;
        int bw = 368;
        int bh = 48;
        uint32_t adv_bg = g_inst_adv_enabled ? 0xFF243B2Eu : 0xFF1B1F26u;
        fill_rect(bx, by, bw, bh, adv_bg);
        draw_rect(bx, by, bw, bh, g_inst_adv_enabled ? 0xFF58E59Du : th->border_blur);
        draw_text(bx + 12, by + 16, g_inst_adv_enabled ? "Advanced: ON" : "Advanced: OFF", 0xFFEAF4FFu);
        if (g_inst_adv_enabled) {
            const char* part = g_inst_part_scheme ? "GPT" : "MBR";
            const char* fs = (g_inst_fs_choice == 2) ? "EXT2" :
                             (g_inst_fs_choice == 4) ? "EXT4" :
                             (g_inst_fs_choice == 12) ? "FAT12" :
                             (g_inst_fs_choice == 16) ? "FAT16" :
                             (g_inst_fs_choice == 32) ? "FAT32" : "FAT";
            draw_text(bx + 180, by + 6, "Partition:", 0xFFB8D3EFu);
            draw_text(bx + 270, by + 6, part, 0xFF9FE0FFu);
            draw_text(bx + 180, by + 26, "Filesystem:", 0xFFB8D3EFu);
            draw_text(bx + 270, by + 26, fs, 0xFF9FE0FFu);
        }
    }
    fill_rect(px + 20, py + 534, 120, 34, 0xFF335A80u);
    draw_rect(px + 20, py + 534, 120, 34, 0xFF66A6E8u);
    if (focus_btn == 0) draw_rect(px + 18, py + 532, 124, 38, 0xFFFFFFFFu);
    draw_text(px + 52, py + 546, "Refresh", 0xFFEAF4FFu);
    fill_rect(px + 154, py + 534, 240, 34, 0xFF2E7A56u);
    draw_rect(px + 154, py + 534, 240, 34, 0xFF6EE6A8u);
    if (focus_btn == 1) draw_rect(px + 152, py + 532, 244, 38, 0xFFFFFFFFu);
    draw_text(px + 170, py + 546, "Install (Keep Partition)", 0xFFEAF4FFu);
    fill_rect(px + 410, py + 534, 300, 34, 0xFF7A3B2Eu);
    draw_rect(px + 410, py + 534, 300, 34, 0xFFE38A78u);
    if (focus_btn == 2) draw_rect(px + 408, py + 532, 304, 38, 0xFFFFFFFFu);
    draw_text(px + 424, py + 546, "Erase + Repartition + Install", 0xFFFFF0EBu);
    fill_rect(px + 724, py + 534, 220, 34, 0xFF4C2F65u);
    draw_rect(px + 724, py + 534, 220, 34, 0xFFC490FFu);
    if (focus_btn == 3) draw_rect(px + 722, py + 532, 224, 38, 0xFFFFFFFFu);
    draw_text(px + 782, py + 546, "Abort", 0xFFF3E9FFu);
}

static int desktop_run_installer_ui(int force) {
    uint8_t last_left = 0;
    int px, py;
    if (!force && desktop_conf_is_installed()) return 0;
    memset(g_inst_logs, 0, sizeof(g_inst_logs));
    g_inst_log_count = 0;
    bg_gradient();
    if (desk_inst_find_source_mount(g_inst_source) != 0) g_inst_source[0] = '\0';
    if (desk_inst_collect_devices(g_inst_drive_ids, g_inst_devs, &g_inst_dev_count) != 0) g_inst_dev_count = 0;
    g_inst_selected = 0;
    g_inst_focus_btn = 1;
    desk_inst_log(g_inst_logs, &g_inst_log_count, "[dbg] desktop.conf installed=false");
    desk_inst_log(g_inst_logs, &g_inst_log_count, "[dbg] installer UI loaded");
    if (!g_inst_source[0]) desk_inst_log(g_inst_logs, &g_inst_log_count, "[warn] source /iso* not found yet");
    if (g_inst_dev_count <= 0) desk_inst_log(g_inst_logs, &g_inst_log_count, "[warn] no installable disks found");

    for (;;) {
        ntux_mouse_state_t ms;
        int mx, my;
        if (sys_mouse_get_state(&ms) != 0) memset(&ms, 0, sizeof(ms));
        mx = ms.x;
        my = ms.y;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx >= (int)g_fb.width) mx = (int)g_fb.width - 1;
        if (my >= (int)g_fb.height) my = (int)g_fb.height - 1;
        g_last_mouse_x = mx;
        g_last_mouse_y = my;
        memcpy(g_frame, g_bg, g_pixels * sizeof(uint32_t));
        desk_inst_draw_ui(g_inst_drive_ids, g_inst_devs, g_inst_dev_count, g_inst_selected, g_inst_focus_btn,
                          g_inst_source, g_inst_logs, g_inst_log_count);
        draw_cursor();
        (void)sys_fb_blit32(g_frame, g_fb.width, g_fb.height, g_fb.width * 4u);
        desktop_wait_ticks(1);

        px = (int)g_fb.width / 2 - 480;
        py = (int)g_fb.height / 2 - 300;
        if (ms.left && !last_left) {
            if (in_rect(mx, my, px + 20, py + 534, 120, 34)) {
                g_inst_focus_btn = 0;
                if (desk_inst_find_source_mount(g_inst_source) != 0) g_inst_source[0] = '\0';
                if (desk_inst_collect_devices(g_inst_drive_ids, g_inst_devs, &g_inst_dev_count) != 0) g_inst_dev_count = 0;
                if (g_inst_selected >= g_inst_dev_count) g_inst_selected = g_inst_dev_count > 0 ? g_inst_dev_count - 1 : 0;
                desk_inst_log(g_inst_logs, &g_inst_log_count, "[dbg] refreshed devices and source");
            } else if (in_rect(mx, my, px + 154, py + 534, 240, 34)) {
                g_inst_focus_btn = 1;
                if (g_inst_dev_count <= 0) desk_inst_log(g_inst_logs, &g_inst_log_count, "[err] no disk selected");
                else {
                    char msg[96];
                    msg[0] = '\0';
                    (void)str_append(msg, sizeof(msg), "[dbg] install on blk");
                    (void)str_append_u64(msg, sizeof(msg), g_inst_drive_ids[g_inst_selected]);
                    desk_inst_log(g_inst_logs, &g_inst_log_count, msg);
                    if (desk_inst_perform_install(g_inst_drive_ids[g_inst_selected],
                                                  g_inst_devs[g_inst_selected].sectors,
                                                  0, g_inst_logs, &g_inst_log_count) == 0) {
                        return 0;
                    }
                }
            } else if (in_rect(mx, my, px + 410, py + 534, 300, 34)) {
                g_inst_focus_btn = 2;
                if (g_inst_dev_count <= 0) desk_inst_log(g_inst_logs, &g_inst_log_count, "[err] no disk selected");
                else {
                    char msg[96];
                    msg[0] = '\0';
                    (void)str_append(msg, sizeof(msg), "[dbg] force repartition on blk");
                    (void)str_append_u64(msg, sizeof(msg), g_inst_drive_ids[g_inst_selected]);
                    desk_inst_log(g_inst_logs, &g_inst_log_count, msg);
                    if (desk_inst_perform_install(g_inst_drive_ids[g_inst_selected],
                                                  g_inst_devs[g_inst_selected].sectors,
                                                  1, g_inst_logs, &g_inst_log_count) == 0) {
                        return 0;
                    }
                }
            } else if (in_rect(mx, my, px + 724, py + 534, 220, 34)) {
                g_inst_focus_btn = 3;
                desk_inst_log(g_inst_logs, &g_inst_log_count, "[err] aborted by user");
                return -1;
            } else if (in_rect(mx, my, px + 20, py + 74, 368, 408)) {
                int idx = (my - (py + 74)) / 34;
                if (idx >= 0 && idx < g_inst_dev_count) g_inst_selected = idx;
            } else if (in_rect(mx, my, px + 20, py + 476, 368, 48)) {
                if (!g_inst_adv_enabled) {
                    g_inst_adv_enabled = 1;
                    desk_inst_log(g_inst_logs, &g_inst_log_count, "[dbg] advanced mode enabled");
                } else {
                    if (mx < px + 180) {
                        g_inst_adv_enabled = 0;
                        desk_inst_log(g_inst_logs, &g_inst_log_count, "[dbg] advanced mode disabled");
                    } else if (my < py + 500) {
                        g_inst_part_scheme = g_inst_part_scheme ? 0 : 1;
                        desk_inst_log(g_inst_logs, &g_inst_log_count, "[dbg] partition scheme toggled");
                    } else {
                        if (g_inst_fs_choice == 4) g_inst_fs_choice = 0;
                        else if (g_inst_fs_choice == 0) g_inst_fs_choice = 2;
                        else if (g_inst_fs_choice == 2) g_inst_fs_choice = 4;
                        else g_inst_fs_choice = 4;
                        desk_inst_log(g_inst_logs, &g_inst_log_count, "[dbg] filesystem toggled");
                    }
                }
            }
        }
        last_left = ms.left;
        if (poll_special_press(0x0F)) {
            int shift = (sys_kbd_is_pressed(0x2A) > 0 || sys_kbd_is_pressed(0x36) > 0) ? 1 : 0;
            if (shift) g_inst_focus_btn = (g_inst_focus_btn + 3) % 4;
            else g_inst_focus_btn = (g_inst_focus_btn + 1) % 4;
        }
        if (poll_special_press(0x1C)) {
            if (g_inst_focus_btn == 0) {
                if (desk_inst_find_source_mount(g_inst_source) != 0) g_inst_source[0] = '\0';
                if (desk_inst_collect_devices(g_inst_drive_ids, g_inst_devs, &g_inst_dev_count) != 0) g_inst_dev_count = 0;
                if (g_inst_selected >= g_inst_dev_count) g_inst_selected = g_inst_dev_count > 0 ? g_inst_dev_count - 1 : 0;
                desk_inst_log(g_inst_logs, &g_inst_log_count, "[dbg] refreshed devices and source");
            } else if (g_inst_focus_btn == 1) {
                if (g_inst_dev_count <= 0) desk_inst_log(g_inst_logs, &g_inst_log_count, "[err] no disk selected");
                else {
                    char msg[96];
                    msg[0] = '\0';
                    (void)str_append(msg, sizeof(msg), "[dbg] install on blk");
                    (void)str_append_u64(msg, sizeof(msg), g_inst_drive_ids[g_inst_selected]);
                    desk_inst_log(g_inst_logs, &g_inst_log_count, msg);
                    if (desk_inst_perform_install(g_inst_drive_ids[g_inst_selected],
                                                  g_inst_devs[g_inst_selected].sectors,
                                                  0, g_inst_logs, &g_inst_log_count) == 0) {
                        return 0;
                    }
                }
            } else if (g_inst_focus_btn == 2) {
                if (g_inst_dev_count <= 0) desk_inst_log(g_inst_logs, &g_inst_log_count, "[err] no disk selected");
                else {
                    char msg[96];
                    msg[0] = '\0';
                    (void)str_append(msg, sizeof(msg), "[dbg] force repartition on blk");
                    (void)str_append_u64(msg, sizeof(msg), g_inst_drive_ids[g_inst_selected]);
                    desk_inst_log(g_inst_logs, &g_inst_log_count, msg);
                    if (desk_inst_perform_install(g_inst_drive_ids[g_inst_selected],
                                                  g_inst_devs[g_inst_selected].sectors,
                                                  1, g_inst_logs, &g_inst_log_count) == 0) {
                        return 0;
                    }
                }
            } else if (g_inst_focus_btn == 3) {
                desk_inst_log(g_inst_logs, &g_inst_log_count, "[err] aborted by user");
                return -1;
            }
        }
        if (poll_special_press(0x01)) return -1;
        (void)sys_yield();
    }
}

static void desktop_check_installer_request(void) {
    if (g_inst_request_busy) return;
    if (sys_fs_exists("/tmp/installer.request") <= 0) return;
    (void)sys_fs_remove("/tmp/installer.request");
    g_inst_request_busy = 1;
    (void)desktop_run_installer_ui(1);
    g_inst_request_busy = 0;
}
#endif

static void term_push_line_state(desk_term_state_t* ts, const char* s) {
    if (!ts || !s) return;
    if (ts->line_count < DESK_TERM_LINES) {
        strncpy(ts->lines[ts->line_count], s, DESK_TERM_COLS);
        ts->lines[ts->line_count][DESK_TERM_COLS] = '\0';
        ts->line_count++;
        return;
    }
    for (int i = 1; i < DESK_TERM_LINES; ++i) {
        memcpy(ts->lines[i - 1], ts->lines[i], DESK_TERM_COLS + 1);
    }
    strncpy(ts->lines[DESK_TERM_LINES - 1], s, DESK_TERM_COLS);
    ts->lines[DESK_TERM_LINES - 1][DESK_TERM_COLS] = '\0';
}

static void term_push_line(const char* s) {
    desk_term_state_t* ts = g_term_exec_state ? g_term_exec_state : term_state_active();
    term_push_line_state(ts, s);
}

static void term_push_multiline(const char* s) {
    char line[DESK_TERM_COLS + 1];
    size_t li = 0;
    if (!s) return;
    for (size_t i = 0;; ++i) {
        char c = s[i];
        if (c == '\n' || c == '\0') {
            line[li] = '\0';
            term_push_line(line);
            li = 0;
            if (c == '\0') break;
            continue;
        }
        if (li + 1 >= (size_t)sizeof(line)) {
            line[li] = '\0';
            term_push_line(line);
            li = 0;
        }
        if (c == '\r') continue;
        line[li++] = c;
    }
}

static int split_args(char* line, char* argv[], int maxc) {
    int argc = 0;
    char* p = line;
    while (*p && argc < maxc) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (!*p) break;
        *p++ = '\0';
    }
    return argc;
}

static void term_push_num_u64(uint64_t v, char* out, size_t cap) {
    char tmp[32];
    int p = 0;
    size_t i = 0;
    if (!out || cap < 2) return;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0 && p < (int)sizeof(tmp)) {
        tmp[p++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (p > 0 && i + 1 < cap) out[i++] = tmp[--p];
    out[i] = '\0';
}

static int path_is_directory(const char* path) {
    ntux_dirent_t probe[1];
    uint64_t n = 0;
    if (!path || !path[0]) return 0;
    return (sys_fs_list_dir(path, probe, 1, &n) == 0) ? 1 : 0;
}

static const char* path_basename_ptr(const char* path) {
    const char* base = path;
    if (!path) return "";
    for (const char* p = path; *p; ++p) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

static long desktop_launch_by_basename_tid(const char* base) {
    char candidate[256];
    char app_dir[256];
    static const char* fixed_roots[] = {
        "/boot/modules",
        "/bin",
        "/apps",
        "/usr/bin",
        "/usr/local/bin",
        "/opt/bin"
    };
    if (!base || !base[0] || !name_is_elf(base)) return -1;
    for (size_t i = 0; i < sizeof(fixed_roots) / sizeof(fixed_roots[0]); ++i) {
        if (join_path(fixed_roots[i], base, candidate, sizeof(candidate)) != 0) continue;
        if (sys_fs_exists(candidate) <= 0) continue;
        long tid = sys_task_add(candidate);
        if (tid >= 0) return tid;
    }
    for (int d = 0; d < 8; ++d) {
        char media_root[32];
        media_root[0] = '\0';
        if (str_append(media_root, sizeof(media_root), "/mnt/sd") != 0) continue;
        if (str_append_char(media_root, sizeof(media_root), (char)('a' + d)) != 0) continue;
        for (int p = 1; p <= 8; ++p) {
            char part_root[40];
            part_root[0] = '\0';
            if (str_append(part_root, sizeof(part_root), media_root) != 0) continue;
            if (str_append_i32(part_root, sizeof(part_root), p) != 0) continue;
            if (join_path(part_root, "apps", app_dir, sizeof(app_dir)) != 0) continue;
            if (join_path(app_dir, base, candidate, sizeof(candidate)) != 0) continue;
            if (sys_fs_exists(candidate) <= 0) continue;
            long tid = sys_task_add(candidate);
            if (tid >= 0) return tid;
        }
    }
    return -1;
}

static long desktop_launch_target_tid(const char* path) {
    if (!path || !path[0]) return -1;
    if (strcmp(path, "::console") == 0 || path_is_terminal_elf(path)) {
        open_console_window();
        return 0;
    }
    if (strcmp(path, "::settings") == 0) {
        long tid = launch_settings_tid();
        return (tid >= 0) ? tid : -1;
    }
    if (strcmp(path, "::browser") == 0) {
        long tid = launch_browser_tid();
        return (tid >= 0) ? tid : -1;
    }
    if (strcmp(path, "::explorer") == 0) {
        open_explorer_window();
        return 0;
    }
    if (str_starts_with(path, "::module:")) {
        const char* token = path + 9;
        if (!token[0]) return -1;
        int term_idx = -1;
        if (module_token_wants_console(token)) {
            open_console_window();
            term_idx = g_focus_index;
        }
        long tid = sys_task_add_module(token);
        if (tid >= 0 && term_idx >= 0 && term_idx < g_window_count && g_windows[term_idx].terminal) {
            term_route_register((int)tid, term_idx);
        }
        return (tid >= 0) ? tid : -1;
    }
    if (path_is_directory(path)) {
        open_explorer_window_at(path);
        return 0;
    }
    if (path[0] == '/') {
        const char* base = path_basename_ptr(path);
        if (!name_is_elf(base)) return -1;
        if (sys_fs_exists(path) <= 0) return desktop_launch_by_basename_tid(base);
    }
    {
        long tid = sys_task_add(path);
        if (tid >= 0) return tid;
    }
    return desktop_launch_by_basename_tid(path_basename_ptr(path));
}

static long launch_settings_tid(void) {
    long tid = sys_task_add_module("settings");
    if (tid < 0) tid = desktop_launch_by_basename_tid("settings.elf");
    return tid;
}

static long launch_browser_tid(void) {
    long tid = sys_task_add_module("browser");
    if (tid < 0) tid = desktop_launch_by_basename_tid("browser.elf");
    return tid;
}

static long desktop_launch_by_basename(const char* base) {
    return (desktop_launch_by_basename_tid(base) >= 0) ? 0 : -1;
}

static long desktop_launch_target(const char* path) {
    return (desktop_launch_target_tid(path) >= 0) ? 0 : -1;
}

static int write_imgview_path(const char* path) {
    if (!path) return -1;
    size_t len = strlen(path);
    if (sys_fs_write_file("/tmp/imgview_path", path, (uint64_t)len) == 0) return 0;
    return (sys_fs_create_file("/tmp", "imgview_path", path, (uint64_t)len) == 0) ? 0 : -1;
}

static int write_objview_path(const char* path) {
    if (!path) return -1;
    size_t len = strlen(path);
    if (sys_fs_write_file("/tmp/objview_path", path, (uint64_t)len) == 0) return 0;
    return (sys_fs_create_file("/tmp", "objview_path", path, (uint64_t)len) == 0) ? 0 : -1;
}

static long desktop_launch_image_viewer(const char* path) {
    if (!path || !path[0]) return -1;
    (void)write_imgview_path(path);
    long rc = sys_task_add_module("imgview");
    if (rc != 0) rc = (desktop_launch_target("imgview.elf") == 0) ? 0 : -1;
    return rc;
}

static long desktop_launch_objview_with_path(const char* path) {
    if (!path || !path[0]) return -1;
    (void)write_objview_path(path);
    long rc = sys_task_add_module("objview");
    if (rc != 0) rc = (desktop_launch_target("objview.elf") == 0) ? 0 : -1;
    return rc;
}

static int desktop_write_args_for_tid(int tid, const char* arg) {
    if (tid <= 0 || !arg || !arg[0]) return -1;
    char path[64];
    int n = snprintf(path, sizeof(path), "/tmp/args.%d", tid);
    if (n <= 0 || (size_t)n >= sizeof(path)) return -1;
    size_t len = strlen(arg);
    if (sys_fs_write_file(path, arg, (uint64_t)len) == 0) return 0;
    char name[32];
    n = snprintf(name, sizeof(name), "args.%d", tid);
    if (n <= 0 || (size_t)n >= sizeof(name)) return -1;
    return (sys_fs_create_file("/tmp", name, arg, (uint64_t)len) == 0) ? 0 : -1;
}

static long desktop_launch_editor_with_path(const char* path) {
    long tid = sys_task_add_module("editor");
    if (tid <= 0) tid = desktop_launch_by_basename_tid("editor.elf");
    if (tid <= 0) return -1;
    if (path && path[0]) (void)desktop_write_args_for_tid((int)tid, path);
    return tid;
}

static int parse_u64(const char* s, uint64_t* out) {
    uint64_t v = 0;
    if (!s || !s[0] || !out) return -1;
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10u + (uint64_t)(s[i] - '0');
    }
    *out = v;
    return 0;
}

static int split_parent_name(const char* full, char* parent, char* name, size_t cap) {
    const char* slash = 0;
    if (!full || full[0] != '/' || !parent || !name || cap < 2) return -1;
    for (const char* p = full; *p; ++p) {
        if (*p == '/') slash = p;
    }
    if (!slash || !slash[1]) return -1;

    size_t plen = (size_t)(slash - full);
    size_t nlen = strlen(slash + 1);
    if (nlen == 0 || nlen >= cap) return -1;

    if (plen == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        if (plen >= cap) return -1;
        memcpy(parent, full, plen);
        parent[plen] = '\0';
    }
    memcpy(name, slash + 1, nlen + 1u);
    return 0;
}

static void term_cmd_ls(const char* cwd, const char* arg) {
    char path[256];
    ntux_dirent_t ents[DESK_LS_MAX];
    uint64_t count = 0;
    if (!arg) arg = ".";
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0) {
        term_push_line("ls: invalid path");
        return;
    }
    if (sys_fs_list_dir(path, ents, DESK_LS_MAX, &count) != 0) {
        term_push_line("ls: failed");
        return;
    }
    if (count > DESK_LS_MAX) count = DESK_LS_MAX;
    for (uint64_t i = 0; i < count; ++i) {
        char line[128];
        line[0] = '\0';
        if (ents[i].is_dir) {
            (void)str_append(line, sizeof(line), "[D] ");
            (void)str_append(line, sizeof(line), ents[i].name);
        } else {
            (void)str_append(line, sizeof(line), "[F] ");
            (void)str_append(line, sizeof(line), ents[i].name);
            (void)str_append(line, sizeof(line), " (");
            (void)str_append_u64(line, sizeof(line), ents[i].size);
            (void)str_append_char(line, sizeof(line), ')');
        }
        term_push_line(line);
    }
}

static void term_push_multiline_state(desk_term_state_t* ts, const char* s) {
    char line[DESK_TERM_COLS + 1];
    size_t li = 0;
    if (!s || !ts) return;
    for (size_t i = 0;; ++i) {
        char c = s[i];
        if (c == '\n' || c == '\0') {
            line[li] = '\0';
            term_push_line_state(ts, line);
            li = 0;
            if (c == '\0') break;
            continue;
        }
        if (li + 1 >= sizeof(line)) {
            line[li] = '\0';
            term_push_line_state(ts, line);
            li = 0;
        }
        if (c == '\r') continue;
        line[li++] = c;
    }
}

void desk_term_write_for_tid(int tid, const char* s) {
    if (!s || tid <= 0) return;
    int term_idx = term_route_find(tid);
    if (term_idx < 0) {
        if (g_focus_index >= 0 && g_focus_index < g_window_count && g_windows[g_focus_index].terminal) {
            term_idx = g_focus_index;
        } else {
            open_console_window();
            term_idx = g_focus_index;
        }
        if (term_idx >= 0 && term_idx < g_window_count && g_windows[term_idx].terminal) {
            term_route_register(tid, term_idx);
        }
    }
    if (term_idx < 0 || term_idx >= g_window_count) return;
    desk_window_t* w = &g_windows[term_idx];
    desk_term_state_t* ts = term_state_for_window(w);
    if (!ts) return;
    term_push_multiline_state(ts, s);
    g_desktop_dirty = 1;
}

void desktop_mark_dirty(void) {
    g_desktop_dirty = 1;
}

static void term_cmd_cat(const char* cwd, const char* arg) {
    char path[256];
    char buf[DESK_CAT_MAX + 1];
    uint64_t out_len = 0;
    if (!arg) {
        term_push_line("usage: cat <path>");
        return;
    }
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0) {
        term_push_line("cat: invalid path");
        return;
    }
    if (sys_fs_read_file(path, buf, DESK_CAT_MAX, &out_len) != 0) {
        term_push_line("cat: failed");
        return;
    }
    if (out_len > DESK_CAT_MAX) out_len = DESK_CAT_MAX;
    buf[out_len] = '\0';
    term_push_multiline(buf);
}

static void term_cmd_cd(char* cwd, const char* arg) {
    char path[256];
    ntux_dirent_t probe[1];
    uint64_t n = 0;
    if (!arg) arg = "/";
    if (normalize_path(cwd, arg, path, 256) != 0) {
        term_push_line("cd: invalid path");
        return;
    }
    if (sys_fs_list_dir(path, probe, 1, &n) != 0) {
        term_push_line("cd: no such directory");
        return;
    }
    strncpy(cwd, path, 127);
    cwd[127] = '\0';
}

static void term_cmd_mkdir(const char* cwd, const char* arg) {
    char path[256], parent[256], name[256];
    if (!arg) {
        term_push_line("usage: mkdir <path>");
        return;
    }
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0 || split_parent_name(path, parent, name, sizeof(parent)) != 0) {
        term_push_line("mkdir: invalid path");
        return;
    }
    if (sys_fs_mkdir(parent, name) != 0) term_push_line("mkdir: failed");
}

static void term_cmd_touch(const char* cwd, const char* arg) {
    char path[256], parent[256], name[256];
    if (!arg) {
        term_push_line("usage: touch <path>");
        return;
    }
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0 || split_parent_name(path, parent, name, sizeof(parent)) != 0) {
        term_push_line("touch: invalid path");
        return;
    }
    if (sys_fs_exists(path) > 0) {
        if (sys_fs_write_file(path, "", 0) != 0) term_push_line("touch: failed");
        return;
    }
    if (sys_fs_create_file(parent, name, "", 0) != 0) term_push_line("touch: failed");
}

static void term_cmd_rm(const char* cwd, const char* arg) {
    char path[256];
    if (!arg) {
        term_push_line("usage: rm <path>");
        return;
    }
    if (normalize_path(cwd, arg, path, sizeof(path)) != 0) {
        term_push_line("rm: invalid path");
        return;
    }
    if (sys_fs_remove(path) != 0) term_push_line("rm: failed");
}

static void term_cmd_mv(const char* cwd, const char* old_arg, const char* new_arg) {
    char old_path[256], new_path[256];
    if (!old_arg || !new_arg) {
        term_push_line("usage: mv <old> <new>");
        return;
    }
    if (normalize_path(cwd, old_arg, old_path, sizeof(old_path)) != 0 ||
        normalize_path(cwd, new_arg, new_path, sizeof(new_path)) != 0) {
        term_push_line("mv: invalid path");
        return;
    }
    if (sys_fs_rename(old_path, new_path) != 0) term_push_line("mv: failed");
}

static int normalize_path(const char* cwd, const char* in, char* out, size_t cap) {
    char temp[256];
    size_t tlen = 0;
    const char* src;
    if (!cwd || !in || !out || cap < 2) return -1;
    if (in[0] == '/') {
        strncpy(temp, in, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
    } else {
        size_t clen = strlen(cwd);
        if (clen == 0 || cwd[0] != '/' || clen >= sizeof(temp) - 2) return -1;
        memcpy(temp, cwd, clen);
        tlen = clen;
        if (tlen > 1 && temp[tlen - 1] == '/') tlen--;
        temp[tlen++] = '/';
        strncpy(temp + tlen, in, sizeof(temp) - tlen - 1);
        temp[sizeof(temp) - 1] = '\0';
    }

    out[0] = '/';
    out[1] = '\0';
    src = temp;
    while (*src) {
        while (*src == '/') src++;
        if (!*src) break;
        char seg[64];
        size_t sl = 0;
        while (src[sl] && src[sl] != '/' && sl + 1 < sizeof(seg)) {
            seg[sl] = src[sl];
            sl++;
        }
        seg[sl] = '\0';
        src += sl;
        if (strcmp(seg, ".") == 0) continue;
        if (strcmp(seg, "..") == 0) {
            size_t ol = strlen(out);
            if (ol > 1) {
                size_t j = ol - 1;
                while (j > 0 && out[j] != '/') j--;
                out[j == 0 ? 1 : j] = '\0';
            }
            continue;
        }
        size_t ol = strlen(out);
        if (ol > 1) {
            if (ol + 1 >= cap) return -1;
            out[ol++] = '/';
            out[ol] = '\0';
        }
        if (ol + sl >= cap) return -1;
        memcpy(out + ol, seg, sl + 1);
    }
    return 0;
}

static void term_task_list(void) {
    ntux_task_info_t tasks[32];
    uint64_t count = 0;
    if (sys_task_list(tasks, 32u, &count) != 0) {
        term_push_line("[err] task list failed");
        return;
    }
    if (count > 32u) count = 32u;
    term_push_line("id uid state core aff");
    for (uint64_t i = 0; i < count; ++i) {
        if (!tasks[i].active) continue;
        char line[96];
        line[0] = '\0';
        (void)str_append_u64(line, sizeof(line), tasks[i].id);
        (void)str_append_char(line, sizeof(line), ' ');
        (void)str_append_u64(line, sizeof(line), (uint64_t)tasks[i].uid);
        (void)str_append_char(line, sizeof(line), ' ');
        (void)str_append_u64(line, sizeof(line), (uint64_t)tasks[i].state);
        (void)str_append_char(line, sizeof(line), ' ');
        (void)str_append_i32(line, sizeof(line), (int32_t)tasks[i].running_core);
        (void)str_append_char(line, sizeof(line), ' ');
        (void)str_append_i32(line, sizeof(line), (int32_t)tasks[i].affinity_core);
        term_push_line(line);
    }
}

static int parse_hex_u64(const char* s, uint64_t* out) {
    uint64_t v = 0;
    size_t i = 0;
    if (!s || !s[0] || !out) return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    if (!s[i]) return -1;
    for (; s[i]; ++i) {
        char c = s[i];
        uint64_t d = 0;
        if (c >= '0' && c <= '9') d = (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (uint64_t)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (uint64_t)(c - 'A');
        else return -1;
        v = (v << 4u) | d;
    }
    *out = v;
    return 0;
}

static int parse_drive_part(const char* s, uint64_t* out_drive, uint64_t* out_part) {
    const char* p = 0;
    const char* base = s;
    if (!s || !out_drive || !out_part) return -1;
    if (strncmp(base, "blk", 3) == 0) base += 3;
    for (size_t i = 0; base[i]; ++i) {
        if (base[i] == 'p') {
            p = &base[i];
            break;
        }
    }
    if (!p) return -1;
    char d[32];
    size_t dl = (size_t)(p - base);
    if (dl == 0 || dl >= sizeof(d)) return -1;
    memcpy(d, base, dl);
    d[dl] = '\0';
    if (parse_u64(d, out_drive) != 0) return -1;
    if (parse_u64(p + 1, out_part) != 0) return -1;
    if (*out_part < 1 || *out_part > 4) return -1;
    return 0;
}

static int find_partition(uint64_t drive, uint64_t part, ntux_partition_info_t* out_part) {
    ntux_partition_info_t parts[8];
    uint64_t count = 0;
    if (!out_part) return -1;
    if (sys_block_partitions(drive, parts, 8u, &count) != 0) return -1;
    if (count > 8u) count = 8u;
    for (uint64_t i = 0; i < count; ++i) {
        if (parts[i].index == (uint8_t)part) {
            *out_part = parts[i];
            return 0;
        }
    }
    return -1;
}

static void term_cmd_lsblk(void) {
    ntux_block_device_info_t devs[8];
    uint64_t dev_count = 0;
    if (sys_block_list(devs, 8u, &dev_count) != 0) {
        term_push_line("lsblk: failed");
        return;
    }
    if (dev_count > 8u) dev_count = 8u;
    term_push_line("NAME      TYPE   SIZE(MiB)   INFO");
    for (uint64_t i = 0; i < dev_count; ++i) {
        if (!devs[i].present) continue;
        char line[160];
        line[0] = '\0';
        (void)str_append(line, sizeof(line), "blk");
        (void)str_append_u64(line, sizeof(line), i);
        (void)str_append(line, sizeof(line), "      ");
        (void)str_append(line, sizeof(line), devs[i].is_atapi ? "rom   " : "disk  ");
        (void)str_append_u64(line, sizeof(line), devs[i].sectors / 2048u);
        (void)str_append(line, sizeof(line), "        ");
        (void)str_append(line, sizeof(line), devs[i].model);
        term_push_line(line);

        ntux_partition_info_t parts[8];
        uint64_t part_count = 0;
        if (sys_block_partitions(i, parts, 8u, &part_count) != 0) continue;
        if (part_count > 8u) part_count = 8u;
        for (uint64_t j = 0; j < part_count; ++j) {
            char pline[160];
            pline[0] = '\0';
            (void)str_append(pline, sizeof(pline), "blk");
            (void)str_append_u64(pline, sizeof(pline), i);
            (void)str_append(pline, sizeof(pline), "p");
            (void)str_append_u64(pline, sizeof(pline), (uint64_t)parts[j].index);
            (void)str_append(pline, sizeof(pline), "    part  ");
            (void)str_append_u64(pline, sizeof(pline), (uint64_t)parts[j].sectors / 2048u);
            (void)str_append(pline, sizeof(pline), "        type=0x");
            char hx[8];
            const char* dig = "0123456789ABCDEF";
            hx[0] = dig[(parts[j].type >> 4) & 0xFu];
            hx[1] = dig[parts[j].type & 0xFu];
            hx[2] = '\0';
            (void)str_append(pline, sizeof(pline), hx);
            (void)str_append(pline, sizeof(pline), " lba=");
            (void)str_append_u64(pline, sizeof(pline), parts[j].lba_start);
            term_push_line(pline);
        }
    }
}

static void term_cmd_blkrescan(void) {
    if (sys_fs_rescan() != 0) term_push_line("blkrescan: failed");
    else term_push_line("[ok] storage rescanned");
}

static void term_cmd_fdisk(int argc, char* argv[]) {
    if (argc == 2 && strcmp(argv[1], "-l") == 0) {
        term_cmd_lsblk();
        return;
    }
    if (argc >= 4 && strcmp(argv[1], "delete") == 0) {
        uint64_t drive = 0;
        uint64_t part = 0;
        if (parse_u64(argv[2], &drive) != 0 || parse_u64(argv[3], &part) != 0 || part < 1 || part > 4) {
            term_push_line("usage: fdisk delete <drive> <part>");
            return;
        }
        ntux_mbr_part_req_t req;
        memset(&req, 0, sizeof(req));
        req.drive = (uint8_t)drive;
        req.part_index = (uint8_t)part;
        if (sys_block_set_mbr_partition(&req) != 0) term_push_line("fdisk: delete failed");
        else term_push_line("[ok] partition deleted");
        return;
    }
    if (argc >= 7 && strcmp(argv[1], "create") == 0) {
        uint64_t drive = 0, part = 0, start_mb = 0, size_mb = 0, type64 = 0;
        if (parse_u64(argv[2], &drive) != 0 || parse_u64(argv[3], &part) != 0 ||
            parse_u64(argv[4], &start_mb) != 0 || parse_u64(argv[5], &size_mb) != 0 ||
            parse_hex_u64(argv[6], &type64) != 0 || part < 1 || part > 4 || type64 > 0xFFu) {
            term_push_line("usage: fdisk create <drive> <part> <startMiB> <sizeMiB> <typeHex>");
            return;
        }
        ntux_mbr_part_req_t req;
        memset(&req, 0, sizeof(req));
        req.drive = (uint8_t)drive;
        req.part_index = (uint8_t)part;
        req.type = (uint8_t)type64;
        req.bootable = 0;
        req.lba_start = (uint32_t)(start_mb * 2048u);
        req.sectors = (uint32_t)(size_mb * 2048u);
        if (sys_block_set_mbr_partition(&req) != 0) term_push_line("fdisk: create failed");
        else term_push_line("[ok] partition created");
        return;
    }
    term_push_line("usage: fdisk -l");
    term_push_line("       fdisk create <drive> <part> <startMiB> <sizeMiB> <typeHex>");
    term_push_line("       fdisk delete <drive> <part>");
}

static void term_cmd_mkfs(int ext4_mode, int argc, char* argv[]) {
    uint64_t drive = 0;
    uint64_t part = 0;
    ntux_partition_info_t pi;
    if (argc < 2) {
        term_push_line(ext4_mode ? "usage: mkfs.ext4 <drive>p<part>" : "usage: mkfs.ext2 <drive>p<part>");
        return;
    }
    if (parse_drive_part(argv[1], &drive, &part) != 0) {
        term_push_line("mkfs: invalid target (use <drive>p<part>)");
        return;
    }
    if (find_partition(drive, part, &pi) != 0) {
        term_push_line("mkfs: partition not found");
        return;
    }
    long rc = ext4_mode ? sys_mkfs_ext4(drive, pi.lba_start, pi.sectors) : sys_mkfs_ext2(drive, pi.lba_start, pi.sectors);
    if (rc != 0) {
        term_push_line("mkfs: failed");
        return;
    }
    term_push_line("[ok] filesystem written");
}

typedef struct {
    int is_block;
    uint64_t drive;
    uint64_t lba;
    char path[256];
} dd_endpoint_t;

static int dd_parse_endpoint(const char* cwd, const char* spec, dd_endpoint_t* out) {
    if (!cwd || !spec || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (strncmp(spec, "blk", 3) == 0) {
        const char* colon = 0;
        for (size_t i = 3; spec[i]; ++i) if (spec[i] == ':') { colon = &spec[i]; break; }
        if (!colon) return -1;
        char d[32];
        size_t dl = (size_t)(colon - (spec + 3));
        if (dl == 0 || dl >= sizeof(d)) return -1;
        memcpy(d, spec + 3, dl);
        d[dl] = '\0';
        if (parse_u64(d, &out->drive) != 0) return -1;
        if (parse_u64(colon + 1, &out->lba) != 0) return -1;
        out->is_block = 1;
        return 0;
    }
    if (normalize_path(cwd, spec, out->path, sizeof(out->path)) != 0) return -1;
    out->is_block = 0;
    return 0;
}

static void term_cmd_dd(const char* cwd, int argc, char* argv[]) {
    const char* if_s = 0;
    const char* of_s = 0;
    uint64_t bs = 1;
    uint64_t count = 1;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "if=", 3) == 0) if_s = argv[i] + 3;
        else if (strncmp(argv[i], "of=", 3) == 0) of_s = argv[i] + 3;
        else if (strncmp(argv[i], "bs=", 3) == 0) { if (parse_u64(argv[i] + 3, &bs) != 0) bs = 0; }
        else if (strncmp(argv[i], "count=", 6) == 0) { if (parse_u64(argv[i] + 6, &count) != 0) count = 0; }
    }
    if (!if_s || !of_s || bs == 0 || count == 0) {
        term_push_line("usage: dd if=<blkN:LBA|/file> of=<blkN:LBA|/file> bs=<sectors> count=<blocks>");
        return;
    }

    uint64_t total_sectors = bs * count;
    uint64_t total_bytes = total_sectors * 512u;
    if (total_sectors == 0 || total_bytes == 0 || total_bytes > (2u * 1024u * 1024u)) {
        term_push_line("dd: transfer too large (max 2 MiB)");
        return;
    }

    dd_endpoint_t src, dst;
    if (dd_parse_endpoint(cwd, if_s, &src) != 0 || dd_parse_endpoint(cwd, of_s, &dst) != 0) {
        term_push_line("dd: invalid if/of endpoint");
        return;
    }

    uint8_t* buf = (uint8_t*)malloc((size_t)total_bytes);
    if (!buf) {
        term_push_line("dd: out of memory");
        return;
    }

    if (src.is_block) {
        if (sys_block_read(src.drive, src.lba, total_sectors, buf) != 0) {
            free(buf);
            term_push_line("dd: block read failed");
            return;
        }
    } else {
        uint64_t file_len = 0;
        if (sys_fs_read_file(src.path, 0, 0, &file_len) != 0 || file_len < total_bytes) {
            free(buf);
            term_push_line("dd: source file too small");
            return;
        }
        if (sys_fs_read_file(src.path, buf, total_bytes, &file_len) != 0) {
            free(buf);
            term_push_line("dd: file read failed");
            return;
        }
    }

    if (dst.is_block) {
        if (sys_block_write(dst.drive, dst.lba, total_sectors, buf) != 0) {
            free(buf);
            term_push_line("dd: block write failed");
            return;
        }
    } else {
        if (sys_fs_exists(dst.path) > 0) {
            if (sys_fs_write_file(dst.path, buf, total_bytes) != 0) {
                free(buf);
                term_push_line("dd: file write failed");
                return;
            }
        } else {
            char parent[256];
            char name[256];
            if (split_parent_name(dst.path, parent, name, sizeof(parent)) != 0 ||
                sys_fs_create_file(parent, name, buf, total_bytes) != 0) {
                free(buf);
                term_push_line("dd: file create failed");
                return;
            }
        }
    }
    free(buf);
    term_push_line("[ok] dd completed");
}

static int render_bg_from_image(const char* path, uint32_t* dst) {
    image_t img;
    if (!path || !path[0] || !dst) return -1;
    if (image_decode_file_scaled(path, 3, (int)g_fb.width, (int)g_fb.height, &img) != 0) return -1;
    if (img.width <= 0 || img.height <= 0 || !img.data) {
        image_free(&img);
        return -1;
    }
    int ch = (img.channels == 4) ? 4 : 3;
    uint32_t src_w = (uint32_t)img.width;
    uint32_t src_h = (uint32_t)img.height;
    int* xmap = (int*)malloc((size_t)g_fb.width * sizeof(int));
    if (!xmap) {
        image_free(&img);
        return -1;
    }
    for (uint32_t x = 0; x < g_fb.width; ++x) {
        uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)src_w) / (uint64_t)g_fb.width);
        xmap[x] = (int)(sx * (uint32_t)ch);
    }
    for (uint32_t y = 0; y < g_fb.height; ++y) {
        if ((y & 127u) == 0u) {
            sys_yield();
        }
        uint32_t sy = (uint32_t)(((uint64_t)y * (uint64_t)src_h) / (uint64_t)g_fb.height);
        const uint8_t* row = img.data + (uint64_t)sy * (uint64_t)src_w * (uint64_t)ch;
        uint32_t* out = dst + (uint64_t)y * (uint64_t)g_fb.width;
        for (uint32_t x = 0; x < g_fb.width; ++x) {
            const uint8_t* px = row + xmap[x];
            uint8_t r = px[0], g = px[1], b = px[2];
            out[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    free(xmap);
    image_free(&img);
    return 0;
}

static int set_bg_from_image(const char* path) {
    if (!path || !path[0]) return -1;
    if (!g_bg) {
        g_bg = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
        if (!g_bg) return -1;
    }
    return render_bg_from_image(path, g_bg);
}

static int set_bg_from_builtin(void) {
    if (!g_bg) {
        g_bg = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
        if (!g_bg) return -1;
    }
    if (g_bg_next) {
        free(g_bg_next);
        g_bg_next = 0;
    }
    g_bg_blending = 0;
    if (BACKGROUND_WIDTH <= 0 || BACKGROUND_HEIGHT <= 0) return -1;
    if ((uint32_t)BACKGROUND_WIDTH == g_fb.width && (uint32_t)BACKGROUND_HEIGHT == g_fb.height) {
        memcpy(g_bg, background, g_pixels * sizeof(uint32_t));
        return 0;
    }
    int* xmap = (int*)malloc((size_t)g_fb.width * sizeof(int));
    if (!xmap) return -1;
    for (uint32_t x = 0; x < g_fb.width; ++x) {
        uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)BACKGROUND_WIDTH) / (uint64_t)g_fb.width);
        if (sx >= (uint32_t)BACKGROUND_WIDTH) sx = (uint32_t)BACKGROUND_WIDTH - 1u;
        xmap[x] = (int)sx;
    }
    for (uint32_t y = 0; y < g_fb.height; ++y) {
        if ((y & 127u) == 0u) {
            sys_yield();
        }
        uint32_t sy = (uint32_t)(((uint64_t)y * (uint64_t)BACKGROUND_HEIGHT) / (uint64_t)g_fb.height);
        if (sy >= (uint32_t)BACKGROUND_HEIGHT) sy = (uint32_t)BACKGROUND_HEIGHT - 1u;
        const uint32_t* row = background + (uint64_t)sy * (uint64_t)BACKGROUND_WIDTH;
        uint32_t* out = g_bg + (uint64_t)y * (uint64_t)g_fb.width;
        for (uint32_t x = 0; x < g_fb.width; ++x) {
            uint32_t sx = (uint32_t)xmap[x];
            out[x] = row[sx];
        }
    }
    free(xmap);
    return 0;
}

static int set_bg_transition(const char* path) {
    if (!path || !path[0]) return -1;
    if (!g_bg) {
        g_bg = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
        if (!g_bg) return -1;
        bg_gradient_to(g_bg);
    }
    if (!g_bg_next) {
        g_bg_next = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
        if (!g_bg_next) return -1;
    }
    if (render_bg_from_image(path, g_bg_next) != 0) return -1;
    g_bg_blend = 0.0f;
    g_bg_blending = 1;
    return 0;
}

static void desktop_apply_wallpaper_pref(void) {
    if (!g_wallpaper_custom && g_wallpaper_builtin_enabled) {
        if (set_bg_from_builtin() == 0) return;
    }
    if (strncmp(g_wallpaper_pref, "img:", 4) == 0) {
        if (set_bg_from_image(g_wallpaper_pref + 4) == 0) return;
    }
    if (strncmp(g_wallpaper_pref, "bmp:", 4) == 0) {
        if (set_bg_from_image(g_wallpaper_pref + 4) == 0) return;
    }
    if (g_bg) {
        free(g_bg);
        g_bg = 0;
    }
    if (g_wallpaper_file) {
        free(g_wallpaper_file);
        g_wallpaper_file = 0;
        g_wallpaper_file_cap = 0;
    }
    bg_gradient();
    strncpy(g_wallpaper_pref, "gradient", sizeof(g_wallpaper_pref) - 1);
    g_wallpaper_pref[sizeof(g_wallpaper_pref) - 1] = '\0';
    g_wallpaper_custom = 0;
}

static int term_write_args_file(const char* path, char* argv[], int start, int argc) {
    char buf[256];
    size_t p = 0;
    if (!path) return -1;
    buf[0] = '\0';
    for (int i = start; i < argc; ++i) {
        size_t l = strlen(argv[i]);
        if (p + l + 2 >= sizeof(buf)) break;
        memcpy(buf + p, argv[i], l);
        p += l;
        if (i + 1 < argc) buf[p++] = ' ';
    }
    buf[p] = '\0';
    if (sys_fs_write_file(path, buf, (uint64_t)p) != 0) {
        return (sys_fs_create_file("/tmp", path[0] == '/' ? path + 5 : "args", buf, (uint64_t)p) == 0) ? 0 : -1;
    }
    return 0;
}

static void term_write_args_for_tid(int tid, const char* first, char* argv[], int start, int argc) {
    char path[64];
    char buf[384];
    size_t p = 0;
    if (tid <= 0) return;
    int n = snprintf(path, sizeof(path), "/tmp/args.%d", tid);
    if (n <= 0 || (size_t)n >= sizeof(path)) return;
    buf[0] = '\0';
    if (first && first[0]) {
        size_t l = strlen(first);
        if (p + l + 1 < sizeof(buf)) {
            memcpy(buf + p, first, l);
            p += l;
        }
    }
    for (int i = start; i < argc; ++i) {
        size_t l = strlen(argv[i]);
        if (p + l + 2 >= sizeof(buf)) break;
        if (p > 0) buf[p++] = ' ';
        memcpy(buf + p, argv[i], l);
        p += l;
    }
    buf[p] = '\0';
    if (sys_fs_write_file(path, buf, (uint64_t)p) != 0) {
        if (sys_fs_create_file("/tmp", path + 5, buf, (uint64_t)p) != 0) {
            (void)0;
        }
    }
}

static void start_power_action(int action) {
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;
    g_power_action = action;
    g_power_start = sys_get_ticks();
    g_power_until = g_power_start + hz * 5u;
    g_desktop_dirty = 1;
}

void term_run_command_line(desk_window_t* tw, const char* line_in) {
    char line[256];
    char* argv[16];
    int argc;
    char path[256];
    desk_term_state_t* ts = term_state_for_window(tw);
    int term_idx = g_focus_index;
    if (term_idx < 0 || term_idx >= g_window_count || !g_windows[term_idx].terminal) {
        term_idx = -1;
    }

    if (!line_in || !ts) return;
    g_term_exec_state = ts;
    strncpy(line, line_in, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    term_push_line(line);

    argc = split_args(line, argv, 16);
    if (argc <= 0) return;

    if (strcmp(argv[0], "help") == 0) {
        term_push_line("help banner version clear exit");
        term_push_line("pwd cd ls ll cat mkdir touch rm mv");
    term_push_line("exists stat echo run lua konsole explorer browser clock taskmgr tetris deskdemo editor bench imgview objview paint calc snake flappy xeyes settings partutil test healthcheck whoami adduser task add task list");
        term_push_line("ticks sleep mouse netinfo setbg reboot shutdown poweroff");
        term_push_line("rescan blkrescan lsblk fdisk mkfs.ext2 mkfs.ext4 dd");
        term_push_line("dd if=<blkN:LBA|/file> of=<blkN:LBA|/file> bs=<sectors> count=<blocks>");
        return;
    }
    if (strcmp(argv[0], "banner") == 0) {
        term_print_banner();
        return;
    }
    if (strcmp(argv[0], "version") == 0) {
        term_push_line("NTux Shell Desktop Mode v0.6");
        return;
    }
    if (strcmp(argv[0], "clear") == 0) {
        ts->line_count = 0;
        term_print_banner();
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "pwd") == 0) {
        term_push_line(ts->cwd);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "cd") == 0) {
        term_cmd_cd(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "ls") == 0 || strcmp(argv[0], "ll") == 0) {
        term_cmd_ls(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "cat") == 0) {
        term_cmd_cat(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mkdir") == 0) {
        term_cmd_mkdir(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "touch") == 0) {
        term_cmd_touch(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "rm") == 0) {
        term_cmd_rm(ts->cwd, argc > 1 ? argv[1] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mv") == 0) {
        term_cmd_mv(ts->cwd, argc > 1 ? argv[1] : 0, argc > 2 ? argv[2] : 0);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "echo") == 0) {
        char out[DESK_TERM_COLS + 1];
        size_t p = 0;
        out[0] = '\0';
        for (int i = 1; i < argc; ++i) {
            size_t l = strlen(argv[i]);
            if (p + l + 2 >= sizeof(out)) break;
            memcpy(out + p, argv[i], l);
            p += l;
            if (i + 1 < argc) out[p++] = ' ';
        }
        out[p] = '\0';
        term_push_line(out);
        return;
    }
    if (strcmp(argv[0], "exists") == 0) {
        if (argc < 2) term_push_line("usage: exists <path>");
        else if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) term_push_line("exists: invalid path");
        else term_push_line(sys_fs_exists(path) > 0 ? "yes" : "no");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "stat") == 0) {
        uint64_t n = 0;
        if (argc < 2) {
            term_push_line("usage: stat <path>");
            return;
        }
        if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) {
            term_push_line("stat: invalid path");
            g_term_exec_state = 0;
            return;
        }
        char num[32];
        term_push_line(path);
        term_push_line(sys_fs_exists(path) > 0 ? "exists: yes" : "exists: no");
        (void)sys_fs_list_dir(path, 0, 0, &n);
        term_push_num_u64(n, num, sizeof(num));
        line[0] = '\0';
        (void)str_append(line, sizeof(line), "dir_entries: ");
        (void)str_append(line, sizeof(line), num);
        term_push_line(line);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "task") == 0 && argc >= 2 && strcmp(argv[1], "list") == 0) {
        term_task_list();
        return;
    }
    if (strcmp(argv[0], "task") == 0 && argc >= 3 && strcmp(argv[1], "add") == 0) {
        if (normalize_path(ts->cwd, argv[2], path, sizeof(path)) != 0) {
            term_push_line("[err] invalid path");
        } else {
            long tid = desktop_launch_target_tid(path);
            if (tid >= 0) {
                if (term_idx >= 0) term_route_register((int)tid, term_idx);
                term_push_line("[ok] task started");
            } else {
                term_push_line("[err] task start failed");
            }
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "lsblk") == 0) {
        term_cmd_lsblk();
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "fdisk") == 0) {
        term_cmd_fdisk(argc, argv);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mkfs.ext2") == 0) {
        term_cmd_mkfs(0, argc, argv);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mkfs.ext4") == 0) {
        term_cmd_mkfs(1, argc, argv);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "dd") == 0) {
        term_cmd_dd(ts->cwd, argc, argv);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "blkrescan") == 0) {
        term_cmd_blkrescan();
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "partutil") == 0) {
        term_push_line("[partutil] launching partition utility...");
        long tid = sys_task_add_module("partutil");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/partutil.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[partutil] partition utility started");
        } else {
            term_push_line("[partutil] failed to launch partition utility");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "test") == 0) {
        term_push_line("[test] launching desktop api test...");
        long tid = sys_task_add_module("test");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/test.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[test] test started");
        } else {
            term_push_line("[test] failed to launch test");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "setbg") == 0 && argc >= 2) {
        if (strcmp(argv[1], "off") == 0) {
            if (g_bg) {
                free(g_bg);
                g_bg = 0;
            }
            if (g_wallpaper_file) {
                free(g_wallpaper_file);
                g_wallpaper_file = 0;
                g_wallpaper_file_cap = 0;
            }
            bg_gradient();
            strncpy(g_wallpaper_pref, "gradient", sizeof(g_wallpaper_pref) - 1);
            g_wallpaper_pref[sizeof(g_wallpaper_pref) - 1] = '\0';
            g_wallpaper_custom = 0;
            if (g_wallpaper_builtin_enabled) {
                (void)set_bg_from_builtin();
            }
            (void)desktop_conf_save_layout();
            term_push_line("[ok] background reset");
            g_term_exec_state = 0;
            return;
        }
        if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) {
            term_push_line("[err] invalid path");
            g_term_exec_state = 0;
            return;
        }
        if (set_bg_from_image(path) != 0) {
            term_push_line("[err] setbg failed");
        } else {
            char cfg[320];
            int n = snprintf(cfg, sizeof(cfg), "img:%s", path);
            if (n > 0 && (size_t)n < sizeof(cfg)) {
                strncpy(g_wallpaper_pref, cfg, sizeof(g_wallpaper_pref) - 1);
                g_wallpaper_pref[sizeof(g_wallpaper_pref) - 1] = '\0';
                g_wallpaper_custom = 1;
                (void)desktop_conf_save_layout();
            }
            term_push_line("[ok] background updated");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "setbg") == 0) {
        term_push_line("usage: setbg <path>|off");
        return;
    }
    if (strcmp(argv[0], "run") == 0 && argc >= 2) {
        if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) {
            term_push_line("[err] invalid path");
            g_term_exec_state = 0;
            return;
        }
        if (argc > 2) {
            (void)term_write_args_file("/tmp/run.args", argv, 2, argc);
            if (sys_fs_write_file("/tmp/run.path", path, (uint64_t)strlen(path)) != 0) {
                (void)sys_fs_create_file("/tmp", "run.path", path, (uint64_t)strlen(path));
            }
        }
        long tid = desktop_launch_target_tid(path);
        if (tid >= 0) {
            if (argc > 2) term_write_args_for_tid((int)tid, path, argv, 2, argc);
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] task started");
        } else {
            term_push_line("[err] task start failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "run") == 0) {
        term_push_line("usage: run <path> [args...]");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "lua") == 0 && argc >= 2) {
        if (normalize_path(ts->cwd, argv[1], path, sizeof(path)) != 0) {
            term_push_line("[err] invalid lua path");
            g_term_exec_state = 0;
            return;
        }
        if (sys_fs_write_file("/tmp/lua.run", path, (uint64_t)strlen(path)) != 0) {
            (void)sys_fs_create_file("/tmp", "lua.run", path, (uint64_t)strlen(path));
        }
        if (argc > 2) {
            (void)term_write_args_file("/tmp/lua.args", argv, 2, argc);
        } else {
            (void)sys_fs_remove("/tmp/lua.args");
        }
        long tid = sys_task_add_module("klua");
        if (tid < 0) tid = sys_task_add_module("lua");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/lua.elf");
        if (tid >= 0) {
            term_write_args_for_tid((int)tid, "lua", argv, 1, argc);
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] lua started");
        } else {
            term_push_line("[err] lua start failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "lua") == 0) {
        term_push_line("usage: lua <file.lua> [args...]");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "konsole") == 0) {
        long tid = sys_task_add_module("deskconsole");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/deskconsole.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] konsole started");
        } else {
            open_console_window();
            term_push_line("[ok] konsole window opened");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "taskmgr") == 0) {
        long tid = sys_task_add_module("taskmgr");
        if (tid < 0) tid = desktop_launch_target_tid("taskmgr.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] taskmgr started");
        } else {
            term_push_line("[err] taskmgr failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "tetris") == 0) {
        long tid = sys_task_add_module("tetris");
        if (tid < 0) tid = desktop_launch_target_tid("tetris.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] tetris started");
        } else {
            term_push_line("[err] tetris failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "deskdemo") == 0) {
        long tid = sys_task_add_module("deskdemo");
        if (tid < 0) tid = desktop_launch_target_tid("deskdemo.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] deskdemo started");
        } else {
            term_push_line("[err] deskdemo failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "editor") == 0) {
        long tid = sys_task_add_module("editor");
        if (tid < 0) tid = desktop_launch_target_tid("editor.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] editor started");
        } else {
            term_push_line("[err] editor failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "bench") == 0) {
        long tid = sys_task_add_module("bench");
        if (tid < 0) tid = desktop_launch_target_tid("bench.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] bench started");
        } else {
            term_push_line("[err] bench failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "imgview") == 0) {
        long tid = sys_task_add_module("imgview");
        if (tid < 0) tid = desktop_launch_target_tid("imgview.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] imgview started");
        } else {
            term_push_line("[err] imgview failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "paint") == 0) {
        long tid = sys_task_add_module("paint");
        if (tid < 0) tid = desktop_launch_target_tid("paint.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] paint started");
        } else {
            term_push_line("[err] paint failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "calc") == 0) {
        long tid = sys_task_add_module("calc");
        if (tid < 0) tid = desktop_launch_target_tid("calc.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] calc started");
        } else {
            term_push_line("[err] calc failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "browser") == 0) {
        long tid = sys_task_add_module("browser");
        if (tid < 0) tid = desktop_launch_target_tid("browser.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] browser started");
        } else {
            term_push_line("[err] browser failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "snake") == 0) {
        long tid = sys_task_add_module("snake");
        if (tid < 0) tid = desktop_launch_target_tid("snake.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] snake started");
        } else {
            term_push_line("[err] snake failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "flappy") == 0) {
        long tid = sys_task_add_module("flappy");
        if (tid < 0) tid = desktop_launch_target_tid("flappy.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] flappy started");
        } else {
            term_push_line("[err] flappy failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "objview") == 0) {
        long tid = sys_task_add_module("objview");
        if (tid < 0) tid = desktop_launch_target_tid("objview.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] objview started");
        } else {
            term_push_line("[err] objview failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "healthcheck") == 0) {
        long tid = sys_task_add_module("healthcheck");
        if (tid < 0) tid = desktop_launch_target_tid("healthcheck.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] healthcheck started");
        } else {
            term_push_line("[err] healthcheck failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "xeyes") == 0) {
        long tid = sys_task_add_module("xeyes");
        if (tid < 0) tid = desktop_launch_target_tid("xeyes.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] xeyes started");
        } else {
            term_push_line("[err] xeyes failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "settings") == 0) {
        long tid = launch_settings_tid();
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] settings started");
        } else {
            term_push_line("[err] settings failed");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "explorer") == 0) {
        long tid = sys_task_add_module("explorer");
        if (tid < 0) tid = desktop_launch_target_tid("/boot/modules/explorer.elf");
        if (tid >= 0) {
            if (term_idx >= 0) term_route_register((int)tid, term_idx);
            term_push_line("[ok] explorer started");
        } else {
            open_explorer_window();
            term_push_line("[ok] explorer opened");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "clock") == 0) {
        open_clock_window();
        term_push_line("[ok] analog clock opened");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "ticks") == 0) {
        line[0] = '\0';
        (void)str_append(line, sizeof(line), "ticks=");
        (void)str_append_u64(line, sizeof(line), sys_get_ticks());
        term_push_line(line);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "whoami") == 0) {
        char who[96];
        who[0] = '\0';
        (void)str_append(who, sizeof(who), g_current_user);
        (void)str_append(who, sizeof(who), " uid=");
        (void)str_append_u64(who, sizeof(who), g_current_uid);
        term_push_line(who);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "adduser") == 0) {
        if (argc < 3) {
            term_push_line("usage: adduser <name> <password>");
        } else if (users_add_account(argv[1], argv[2]) != 0) {
            term_push_line("[err] adduser failed");
        } else {
            term_push_line("[ok] user created");
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "sleep") == 0) {
        uint64_t t = 0;
        if (argc < 2 || parse_u64(argv[1], &t) != 0) term_push_line("usage: sleep <ticks>");
        else desktop_wait_ticks(t);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "mouse") == 0) {
        ntux_mouse_state_t ms;
        if (sys_mouse_get_state(&ms) != 0) {
            term_push_line("mouse: failed");
            return;
        }
        line[0] = '\0';
        (void)str_append(line, sizeof(line), "x=");
        (void)str_append_i32(line, sizeof(line), ms.x);
        (void)str_append(line, sizeof(line), " y=");
        (void)str_append_i32(line, sizeof(line), ms.y);
        (void)str_append(line, sizeof(line), " scroll=");
        (void)str_append_i32(line, sizeof(line), ms.scroll);
        (void)str_append(line, sizeof(line), " LRM=");
        (void)str_append_u64(line, sizeof(line), (uint64_t)ms.left);
        (void)str_append_u64(line, sizeof(line), (uint64_t)ms.right);
        (void)str_append_u64(line, sizeof(line), (uint64_t)ms.middle);
        term_push_line(line);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "netinfo") == 0 || strcmp(argv[0], "netstat") == 0) {
        char out[1024];
        long rc = sys_net_debug(out, sizeof(out));
        if (rc == 0) {
            term_push_multiline(out);
        } else {
            line[0] = '\0';
            (void)str_append(line, sizeof(line), "netinfo: failed rc=");
            (void)str_append_u64(line, sizeof(line), (uint64_t)(rc < 0 ? -rc : rc));
            term_push_line(line);
        }
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "reboot") == 0) {
        term_push_line("rebooting in 5s...");
        start_power_action(1);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "shutdown") == 0 || strcmp(argv[0], "poweroff") == 0) {
        term_push_line("shutting down in 5s...");
        start_power_action(2);
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "rescan") == 0) {
        desktop_rescan_icons();
        term_push_line("[ok] icons rescanned");
        g_term_exec_state = 0;
        return;
    }
    if (strcmp(argv[0], "exit") == 0) {
        if (g_focus_index >= 0 && g_focus_index < g_window_count) {
            g_windows[g_focus_index].visible = 0;
            g_windows[g_focus_index].minimized = 0;
            update_focus_after_visibility_change();
        }
        g_term_exec_state = 0;
        return;
    }

    line[0] = '\0';
    (void)str_append(line, sizeof(line), "[err] unknown command: ");
    (void)str_append(line, sizeof(line), argv[0]);
    term_push_line(line);
    g_term_exec_state = 0;
}

static void term_run_command(void) {
    desk_window_t* w;
    char line[256];
    if (g_focus_index < 0 || g_focus_index >= g_window_count) return;
    w = &g_windows[g_focus_index];
    desk_term_state_t* ts = term_state_for_window(w);
    if (!ts) return;
    memcpy(line, ts->input, (size_t)ts->input_len);
    line[ts->input_len] = '\0';
    term_run_command_line(w, line);
}

static int in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static int window_btn_close_hit(const desk_window_t* w, int x, int y) {
    return in_rect(x, y, w->x + w->w - 18, w->y + 6, 12, 12);
}

static int window_btn_max_hit(const desk_window_t* w, int x, int y) {
    return in_rect(x, y, w->x + w->w - 34, w->y + 6, 12, 12);
}

static int window_btn_min_hit(const desk_window_t* w, int x, int y) {
    return in_rect(x, y, w->x + w->w - 50, w->y + 6, 12, 12);
}

static int window_resize_handle_hit(const desk_window_t* w, int x, int y) {
    return in_rect(x, y, w->x + w->w - 14, w->y + w->h - 14, 14, 14);
}

static int clamp_i32(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void term_route_register(int tid, int term_idx) {
    if (tid <= 0) return;
    if (term_idx < 0 || term_idx >= g_window_count) return;
    if (!g_windows[term_idx].terminal) return;
    for (int i = 0; i < DESK_TERM_ROUTE_MAX; ++i) {
        if (g_term_routes[i].tid == tid) {
            g_term_routes[i].term_idx = term_idx;
            return;
        }
    }
    for (int i = 0; i < DESK_TERM_ROUTE_MAX; ++i) {
        if (g_term_routes[i].tid == 0) {
            g_term_routes[i].tid = tid;
            g_term_routes[i].term_idx = term_idx;
            return;
        }
    }
    int slot = tid % DESK_TERM_ROUTE_MAX;
    if (slot < 0) slot = -slot;
    g_term_routes[slot].tid = tid;
    g_term_routes[slot].term_idx = term_idx;
}

static int term_route_find(int tid) {
    if (tid <= 0) return -1;
    for (int i = 0; i < DESK_TERM_ROUTE_MAX; ++i) {
        if (g_term_routes[i].tid == tid) {
            int idx = g_term_routes[i].term_idx;
            if (idx >= 0 && idx < g_window_count && g_windows[idx].terminal) {
                return idx;
            }
            return -1;
        }
    }
    return -1;
}

static void taskbar_target_rect(int idx, int* out_x, int* out_y, int* out_w, int* out_h) {
    int tx = 0;
    int ty = 0;
    int tw = 80;
    int th = 12;
    if (taskbar_window_rect(idx, &tx, &ty, &tw, &th) != 0) {
        int h = 28;
        int pad = 12;
        int bar_y = (int)g_fb.height - h - 8;
        tx = pad + 120;
        ty = bar_y + 8;
    }
    if (tw < 8) tw = 8;
    if (th < 8) th = 8;
    if (out_x) *out_x = tx;
    if (out_y) *out_y = ty;
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th;
}

static void window_start_minimize(int idx) {
    if (idx < 0 || idx >= g_window_count) return;
    desk_window_t* w = &g_windows[idx];
    if (!w->visible || w->minimized) return;
    int tx, ty, tw, th;
    taskbar_target_rect(idx, &tx, &ty, &tw, &th);
    w->animating = 1;
    w->anim_type = DESK_ANIM_MINIMIZE;
    w->anim_start = sys_get_ticks();
    w->anim_dur = (uint16_t)(g_anim_level >= 2 ? 12u : 8u);
    w->anim_from_x = w->x;
    w->anim_from_y = w->y;
    w->anim_from_w = w->w;
    w->anim_from_h = w->h;
    w->anim_to_x = tx;
    w->anim_to_y = ty;
    w->anim_to_w = tw;
    w->anim_to_h = th;
    w->minimized = 1;
}

static void window_start_restore(int idx) {
    if (idx < 0 || idx >= g_window_count) return;
    desk_window_t* w = &g_windows[idx];
    if (!w->visible) w->visible = 1;
    if (!w->minimized) return;
    int tx, ty, tw, th;
    taskbar_target_rect(idx, &tx, &ty, &tw, &th);
    w->animating = 1;
    w->anim_type = DESK_ANIM_RESTORE;
    w->anim_start = sys_get_ticks();
    w->anim_dur = (uint16_t)(g_anim_level >= 2 ? 12u : 8u);
    w->anim_from_x = tx;
    w->anim_from_y = ty;
    w->anim_from_w = tw;
    w->anim_from_h = th;
    w->anim_to_x = w->x;
    w->anim_to_y = w->y;
    w->anim_to_w = w->w;
    w->anim_to_h = w->h;
    w->minimized = 0;
}

static void desk_signal_close_request(uint64_t id) {
    if (id == 0) return;
    g_close_event_id = id;
    g_close_event_tick = sys_get_ticks();
}

static void desktop_request_close(int idx) {
    if (idx < 0 || idx >= g_window_count) return;
    desk_window_t* w = &g_windows[idx];
    if (w->closing) return;
    desk_signal_close_request(w->id);
    w->closing = 1;
    w->close_tick = sys_get_ticks();
    w->visible = 0;
    w->minimized = 0;
    w->animating = 0;
    desktop_mark_dirty();
    update_focus_after_visibility_change();
}

static void desktop_pump_close_requests(void) {
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;
    uint64_t now = sys_get_ticks();
    uint64_t timeout = hz * 2u;
    int closed_any = 0;
    for (int i = 0; i < g_window_count; ++i) {
        desk_window_t* w = &g_windows[i];
        if (!w->closing) continue;
        if (now - w->close_tick < timeout) continue;
        desktop_window_cleanup(i, 1);
        closed_any = 1;
    }
    if (closed_any) update_focus_after_visibility_change();
}

/* Desktop API is handled in api.c via sys_deskapi_* messages. */

static void draw_window(desk_window_t* w, int focused) {
    if (!w || !w->visible) return;
    const desk_theme_t* th = desk_theme();
    uint32_t border = focused ? th->border_focus : th->border_blur;
    uint32_t title_bg = focused ? th->title_focus : th->title_blur;
    int ox = w->x, oy = w->y, ow = w->w, oh = w->h;
    if (ow <= 0 || oh <= 0) return;

    if (w->animating && w->anim_dur > 0) {
        uint64_t now = sys_get_ticks();
        uint64_t age = now - w->anim_start;
        if (age >= w->anim_dur) {
            if (w->anim_type == DESK_ANIM_MINIMIZE) {
                w->animating = 0;
                w->anim_type = 0;
                return;
            }
            w->animating = 0;
            w->anim_type = 0;
        } else {
            float t = (float)age / (float)w->anim_dur;
            float ease = t * t * (3.0f - 2.0f * t);
            ox = (int)((float)w->anim_from_x + (float)(w->anim_to_x - w->anim_from_x) * ease);
            oy = (int)((float)w->anim_from_y + (float)(w->anim_to_y - w->anim_from_y) * ease);
            ow = (int)((float)w->anim_from_w + (float)(w->anim_to_w - w->anim_from_w) * ease);
            oh = (int)((float)w->anim_from_h + (float)(w->anim_to_h - w->anim_from_h) * ease);
            ow = clamp_i32(ow, 12, w->w);
            oh = clamp_i32(oh, 12, w->h);
        }
    }

    if (w->minimized && !w->animating) return;
    uint64_t age = sys_get_ticks() - w->birth_tick;
    if (!w->animating && g_anim_level > 0) {
        uint64_t open_dur = (g_anim_level >= 2) ? 18u : 12u;
        if (age < open_dur) {
            float t = (float)age / (float)open_dur;
            float ease = t * t * (3.0f - 2.0f * t);
            float base = 0.82f + 0.18f * ease;
            if (t > 0.85f) {
                float pop = (g_anim_level >= 2) ? 1.06f : 1.02f;
                float k = (t - 0.85f) / 0.15f;
                base *= (1.0f + (pop - 1.0f) * k);
            }
            int sw = (int)((float)w->w * base);
            int sh = (int)((float)w->h * base);
        if (sw < DESK_MIN_W) sw = DESK_MIN_W;
        if (sh < DESK_MIN_H) sh = DESK_MIN_H;
        ox = w->x + (w->w - sw) / 2;
        oy = w->y + (w->h - sh) / 2;
        ow = sw;
        oh = sh;
        }
    }
    int r = g_ui_scale > 1 ? 8 : 6;
    uint32_t sh_hi = focused ? 68u : 50u;
    uint32_t sh_mid = focused ? 82u : 68u;
    uint32_t sh_base = focused ? 26u : 18u;
    fill_round_rect(ox + 7, oy + 9, ow, oh, r + 3, color_lerp(0xFF000000u, th->taskbar_bg, sh_hi));
    fill_round_rect(ox + 4, oy + 5, ow, oh, r + 2, color_lerp(0xFF000000u, th->taskbar_bg, sh_mid));
    fill_round_rect(ox, oy, ow, oh, r, color_lerp(th->taskbar_bg, 0xFF000000u, sh_base));
    fill_round_rect(ox + 1, oy + 1, ow - 2, oh - 2, r - 1, w->bg ? w->bg : th->window_fill);
    for (int yy = 0; yy < DESK_TITLEBAR_H - 1; ++yy) {
        uint32_t row = color_lerp(title_bg, 0xFF000000u, (uint32_t)(yy * 110 / (DESK_TITLEBAR_H ? DESK_TITLEBAR_H : 1)));
        fill_rect(ox + 1, oy + 1 + yy, ow - 2, 1, row);
    }
    draw_round_rect(ox, oy, ow, oh, r, border);
    draw_text(ox + 8, oy + 7, w->title, th->text_main);
    fill_round_rect(ox + ow - 18, oy + 6, 12, 12, 6, 0xFFF95F62u);
    fill_round_rect(ox + ow - 34, oy + 6, 12, 12, 6, 0xFF4EC9FFu);
    fill_round_rect(ox + ow - 50, oy + 6, 12, 12, 6, th->accent);
    fill_rect(ox + ow - 12, oy + oh - 4, 8, 2, th->text_dim);
    fill_rect(ox + ow - 8, oy + oh - 8, 4, 2, th->text_dim);

    if (w->terminal) {
        desk_term_state_t* ts = term_state_for_window(w);
        int tx = ox + 6;
        int ty = oy + DESK_TITLEBAR_H + 4;
        fill_rect(ox + 2, oy + DESK_TITLEBAR_H + 1, ow - 4, oh - DESK_TITLEBAR_H - 3, 0xFF070B10u);
        fill_rect(ox + 2, oy + DESK_TITLEBAR_H + 1, ow - 4, 16, th->title_focus);
        draw_text(ox + 8, oy + DESK_TITLEBAR_H + 5, "NTux Shell Window", th->text_main);
        if (ts) {
            for (int i = 0; i < ts->line_count; ++i) {
                draw_text(tx, ty + 18 + i * 10, ts->lines[i], 0xFFBFD0FFu);
            }
        }
        char prompt_top[DESK_TERM_COLS + 64];
        char prompt_line[DESK_TERM_COLS + 64];
        term_make_prompt(ts, prompt_top, sizeof(prompt_top));
        prompt_line[0] = '\0';
        (void)str_append(prompt_line, sizeof(prompt_line), "+-> ");
        (void)str_append(prompt_line, sizeof(prompt_line), ts ? ts->input : "");
        draw_text(tx, oy + oh - 24, prompt_top, th->accent);
        draw_text(tx, oy + oh - 14, prompt_line, 0xFF39FF88u);
    } else if (w->file_browser) {
        draw_file_browser(w, window_browser_state(w));
    } else if (w->analog_clock) {
        draw_analog_clock_window(w);
    } else if (w->settings_app) {
        draw_settings_window(w);
    } else if (w->image_enabled) {
        draw_window_image(w, ox, oy);
    } else if (w->canvas_enabled) {
        draw_window_canvas(w, ox, oy, oy + DESK_TITLEBAR_H + 1);
    } else if (w->text[0]) {
        draw_text(ox + 6, oy + 24, w->text, 0xFFE3F0FFu);
    }
}

static int inside_window(const desk_window_t* w, int x, int y) {
    return w && w->visible && !w->minimized && x >= w->x && y >= w->y && x < w->x + w->w && y < w->y + w->h;
}

static int desktop_is_background_hit(int x, int y) {
    if (y >= (int)g_fb.height - DESK_TASKBAR_H) return 0;
    for (int i = g_window_count - 1; i >= 0; --i) {
        if (inside_window(&g_windows[i], x, y)) return 0;
    }
    return 1;
}

static int find_icon_index_by_id(uint64_t id) {
    for (int i = 0; i < g_icon_count; ++i) {
        if (g_icons[i].visible && g_icons[i].id == id) return i;
    }
    return -1;
}

static int find_icon_hit(int x, int y) {
    for (int i = g_icon_count - 1; i >= 0; --i) {
        if (!g_icons[i].visible) continue;
        if (in_rect(x, y, g_icons[i].x, g_icons[i].y, DESK_ICON_W, DESK_ICON_H)) return i;
    }
    return -1;
}

static void icon_free_preview(desk_icon_t* icon) {
    if (!icon || !icon->preview) return;
    free(icon->preview);
    icon->preview = 0;
    icon->preview_w = 0;
    icon->preview_h = 0;
    icon->preview_ready = 0;
    icon->preview_loading = 0;
}

static void icon_free_app_icon(desk_icon_t* icon) {
    if (!icon || !icon->icon_pixels) return;
    free(icon->icon_pixels);
    icon->icon_pixels = 0;
    icon->icon_w = 0;
    icon->icon_h = 0;
    icon->icon_ready = 0;
    icon->icon_failed = 0;
}

static int app_icon_path_for_exec(const char* exec_path, char* out, size_t cap) {
    if (!out || cap == 0) return -1;
    strncpy(out, "/boot/res/icons/app.bmp", cap - 1);
    out[cap - 1] = '\0';
    if (!exec_path || !exec_path[0]) return 0;
    if (strcmp(exec_path, "::explorer") == 0) return (int)(strncpy(out, "/boot/res/icons/explorer.bmp", cap - 1), out[cap - 1] = '\0', 0);
    if (strcmp(exec_path, "::console") == 0) return (int)(strncpy(out, "/boot/res/icons/terminal.bmp", cap - 1), out[cap - 1] = '\0', 0);
    if (strcmp(exec_path, "::settings") == 0) return (int)(strncpy(out, "/boot/res/icons/settings.bmp", cap - 1), out[cap - 1] = '\0', 0);
    if (strcmp(exec_path, "::browser") == 0) return (int)(strncpy(out, "/boot/res/icons/browser.bmp", cap - 1), out[cap - 1] = '\0', 0);
    if (str_starts_with(exec_path, "::module:")) {
        const char* token = exec_path + 9;
        if (!token[0]) return 0;
        if (strcmp(token, "klua") == 0 || strcmp(token, "lua") == 0) return (int)(strncpy(out, "/boot/res/icons/lua.bmp", cap - 1), out[cap - 1] = '\0', 0);
        if (strcmp(token, "konsole") == 0 || strcmp(token, "deskconsole") == 0) return (int)(strncpy(out, "/boot/res/icons/terminal.bmp", cap - 1), out[cap - 1] = '\0', 0);
        if (strcmp(token, "healthcheck") == 0) return (int)(strncpy(out, "/boot/res/icons/healthcheck.bmp", cap - 1), out[cap - 1] = '\0', 0);
        if (strcmp(token, "desktop") == 0) return (int)(strncpy(out, "/boot/res/icons/desktop.bmp", cap - 1), out[cap - 1] = '\0', 0);
        char buf[96];
        buf[0] = '\0';
        (void)str_append(buf, sizeof(buf), "/boot/res/icons/");
        (void)str_append(buf, sizeof(buf), token);
        (void)str_append(buf, sizeof(buf), ".bmp");
        strncpy(out, buf, cap - 1);
        out[cap - 1] = '\0';
        return 0;
    }
    const char* base = path_basename_ptr(exec_path);
    if (!base) return 0;
    if (str_ends_with_ci(base, ".elf")) {
        char name[64];
        size_t bl = strlen(base);
        if (bl >= sizeof(name)) return 0;
        memcpy(name, base, bl);
        name[bl] = '\0';
        if (bl > 4) name[bl - 4] = '\0';
        if (strcmp(name, "klua") == 0 || strcmp(name, "lua") == 0) {
            strncpy(out, "/boot/res/icons/lua.bmp", cap - 1);
            out[cap - 1] = '\0';
            return 0;
        }
        if (strcmp(name, "konsole") == 0 || strcmp(name, "deskconsole") == 0) {
            strncpy(out, "/boot/res/icons/terminal.bmp", cap - 1);
            out[cap - 1] = '\0';
            return 0;
        }
        if (strcmp(name, "healthcheck") == 0) {
            strncpy(out, "/boot/res/icons/healthcheck.bmp", cap - 1);
            out[cap - 1] = '\0';
            return 0;
        }
        char buf[96];
        buf[0] = '\0';
        (void)str_append(buf, sizeof(buf), "/boot/res/icons/");
        (void)str_append(buf, sizeof(buf), name);
        (void)str_append(buf, sizeof(buf), ".bmp");
        strncpy(out, buf, cap - 1);
        out[cap - 1] = '\0';
    }
    return 0;
}

static void icon_set_app_icon(desk_icon_t* icon, const char* path) {
    if (!icon) return;
    icon_free_app_icon(icon);
    icon->icon_failed = 0;
    if (!path || !path[0]) return;
    image_t img;
    if (image_decode_file_scaled(path, 3, 32, 32, &img) != 0 || !img.data) {
        if (strcmp(path, "/boot/res/icons/app.bmp") != 0) {
            if (image_decode_file_scaled("/boot/res/icons/app.bmp", 3, 32, 32, &img) != 0 || !img.data) {
                icon->icon_failed = 1;
                return;
            }
        } else {
            icon->icon_failed = 1;
            return;
        }
    }
    icon->icon_pixels = img.data;
    icon->icon_w = (uint16_t)img.width;
    icon->icon_h = (uint16_t)img.height;
    icon->icon_ready = 1;
    strncpy(icon->icon_path, path, sizeof(icon->icon_path) - 1);
    icon->icon_path[sizeof(icon->icon_path) - 1] = '\0';
}

static int icon_request_preview(int idx) {
    if (idx < 0 || idx >= g_icon_count) return -1;
    desk_icon_t* icon = &g_icons[idx];
    if (!icon || !icon->exec_path[0] || !icon->is_image) return -1;
    if (icon->preview_ready || icon->preview_failed || icon->preview_loading) return 0;
    icon->preview_loading = 1;
    img_job_enqueue(IMG_JOB_ICON, idx, icon->exec_path, 56, 58, 0);
    return 0;
}

static void icon_draw_preview(const desk_icon_t* icon, int x, int y, int ox, int oy, int w, int h) {
    if (!icon || !icon->preview || icon->preview_w == 0 || icon->preview_h == 0) return;
    int pw = (int)icon->preview_w;
    int ph = (int)icon->preview_h;
    int dx = x + ox;
    int dy = y + oy;
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy) {
        int sy = yy * ph / h;
        const uint8_t* row = icon->preview + (size_t)sy * (size_t)pw * 3u;
        for (int xx = 0; xx < w; ++xx) {
            int sx = xx * pw / w;
            const uint8_t* px = row + (size_t)sx * 3u;
            uint32_t c = ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[2];
            put_px(dx + xx, dy + yy, c);
        }
    }
}

static void draw_icon_pixels(const uint8_t* pixels, int pw, int ph, int x, int y, int w, int h) {
    if (!pixels || pw <= 0 || ph <= 0 || w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy) {
        int sy = yy * ph / h;
        const uint8_t* row = pixels + (size_t)sy * (size_t)pw * 3u;
        for (int xx = 0; xx < w; ++xx) {
            int sx = xx * pw / w;
            const uint8_t* px = row + (size_t)sx * 3u;
            uint32_t c = ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[2];
            put_px(x + xx, y + yy, c);
        }
    }
}

void desktop_draw_icon_pixels(int x, int y, int w, int h, const uint8_t* pixels, int pw, int ph) {
    draw_icon_pixels(pixels, pw, ph, x, y, w, h);
}

static void clamp_icon_position(desk_icon_t* icon) {
    int max_x;
    int max_y;
    if (!icon) return;
    max_x = (int)g_fb.width - DESK_ICON_W - 2;
    max_y = (int)g_fb.height - DESK_TASKBAR_H - DESK_ICON_H - 2;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (icon->x < 0) icon->x = 0;
    if (icon->y < 0) icon->y = 0;
    if (icon->x > max_x) icon->x = max_x;
    if (icon->y > max_y) icon->y = max_y;
}

static void desktop_snap_icon_to_grid(desk_icon_t* icon) {
    if (!icon) return;
    int cell_w = DESK_ICON_W + 14;
    int cell_h = DESK_ICON_H + 8;
    int start_x = 24;
    int start_y = 24;
    int max_x = (int)g_fb.width - DESK_ICON_W - 2;
    int max_y = (int)g_fb.height - DESK_TASKBAR_H - DESK_ICON_H - 2;
    if (max_x < start_x) max_x = start_x;
    if (max_y < start_y) max_y = start_y;
    int gx = (icon->x - start_x + cell_w / 2) / cell_w;
    int gy = (icon->y - start_y + cell_h / 2) / cell_h;
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;
    int nx = start_x + gx * cell_w;
    int ny = start_y + gy * cell_h;
    if (nx > max_x) nx = max_x;
    if (ny > max_y) ny = max_y;
    icon->x = nx;
    icon->y = ny;
    clamp_icon_position(icon);
}

static int icon_label_cmp(const desk_icon_t* a, const desk_icon_t* b) {
    if (!a || !b) return 0;
    return strcmp(a->label, b->label);
}

static void desktop_auto_arrange_icons(void) {
    int cell_w = DESK_ICON_W + 14;
    int cell_h = DESK_ICON_H + 8;
    int start_x = 24;
    int start_y = 24;
    int max_x = (int)g_fb.width - DESK_ICON_W - 2;
    int max_y = (int)g_fb.height - DESK_TASKBAR_H - DESK_ICON_H - 2;
    if (max_x < start_x) max_x = start_x;
    if (max_y < start_y) max_y = start_y;
    int cols = (max_x - start_x) / cell_w + 1;
    int rows = (max_y - start_y) / cell_h + 1;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    int total_cells = cols * rows;
    if (total_cells <= 0) return;
    uint8_t* used = (uint8_t*)malloc((size_t)total_cells);
    if (!used) return;
    memset(used, 0, (size_t)total_cells);

    for (int i = 0; i < g_icon_count; ++i) {
        if (!g_icons[i].visible || !g_icon_custom_pos[i]) continue;
        desktop_snap_icon_to_grid(&g_icons[i]);
        int gx = (g_icons[i].x - start_x) / cell_w;
        int gy = (g_icons[i].y - start_y) / cell_h;
        if (gx < 0) gx = 0;
        if (gy < 0) gy = 0;
        if (gx >= cols) gx = cols - 1;
        if (gy >= rows) gy = rows - 1;
        int idx = gy * cols + gx;
        if (idx >= 0 && idx < total_cells) used[idx] = 1;
    }

    int* order = (int*)malloc((size_t)g_icon_count * sizeof(int));
    if (!order) {
        free(used);
        return;
    }
    int count = 0;
    for (int i = 0; i < g_icon_count; ++i) {
        if (!g_icons[i].visible) continue;
        if (g_icon_custom_pos[i]) continue;
        order[count++] = i;
    }
    for (int i = 1; i < count; ++i) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && icon_label_cmp(&g_icons[order[j]], &g_icons[key]) > 0) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    int next_cell_found = 0;
    int next_gx = 0;
    int next_gy = 0;
    for (int i = 0; i < count; ++i) {
        next_cell_found = 0;
        for (int cx = 0; cx < cols && !next_cell_found; ++cx) {
            for (int cy = 0; cy < rows; ++cy) {
                int idx = cy * cols + cx;
                if (!used[idx]) {
                    next_gx = cx;
                    next_gy = cy;
                    used[idx] = 1;
                    next_cell_found = 1;
                    break;
                }
            }
        }
        if (!next_cell_found) break;
        int gx = next_gx;
        int gy = next_gy;
        g_icons[order[i]].x = start_x + gx * cell_w;
        g_icons[order[i]].y = start_y + gy * cell_h;
        clamp_icon_position(&g_icons[order[i]]);
    }

    free(order);
    free(used);
}

static void add_exec_icon(const char* path, const char* filename) {
    char label[24];
    int idx;
    int per_row;
    if (!path || !path[0]) return;
    if (path_is_terminal_elf(path)) return;
    if (g_icon_count >= DESK_MAX_ICONS) return;
    if (icon_exists_path(path)) return;

    idx = g_icon_count++;
    memset(&g_icons[idx], 0, sizeof(g_icons[idx]));
    g_icons[idx].id = 10000u + (uint64_t)idx;
    per_row = ((int)g_fb.width - 24) / (DESK_ICON_W + 14);
    if (per_row < 1) per_row = 1;
    g_icons[idx].x = 24 + (idx % per_row) * (DESK_ICON_W + 14);
    g_icons[idx].y = 24 + (idx / per_row) * (DESK_ICON_H + 8);
    g_icons[idx].visible = 1;
    g_icons[idx].is_dir = 0;
    g_icons[idx].is_image = 0;
    label_from_filename(filename, label, sizeof(label));
    strncpy(g_icons[idx].label, label[0] ? label : "App", sizeof(g_icons[idx].label) - 1);
    g_icons[idx].label[sizeof(g_icons[idx].label) - 1] = '\0';
    strncpy(g_icons[idx].exec_path, path, sizeof(g_icons[idx].exec_path) - 1);
    g_icons[idx].exec_path[sizeof(g_icons[idx].exec_path) - 1] = '\0';
    {
        char icon_path[160];
        app_icon_path_for_exec(g_icons[idx].exec_path, icon_path, sizeof(icon_path));
        icon_set_app_icon(&g_icons[idx], icon_path);
    }
}

static void add_module_request_icon(const char* token) {
    char label[24];
    char exec_path[96];
    int idx;
    int per_row;
    if (!token || !token[0]) return;
    exec_path[0] = '\0';
    if (str_append(exec_path, sizeof(exec_path), "::module:") != 0) return;
    if (str_append(exec_path, sizeof(exec_path), token) != 0) return;
    if (icon_exists_path(exec_path)) return;
    if (g_icon_count >= DESK_MAX_ICONS) return;

    idx = g_icon_count++;
    memset(&g_icons[idx], 0, sizeof(g_icons[idx]));
    g_icons[idx].id = 12000u + (uint64_t)idx;
    per_row = ((int)g_fb.width - 24) / (DESK_ICON_W + 14);
    if (per_row < 1) per_row = 1;
    g_icons[idx].x = 24 + (idx % per_row) * (DESK_ICON_W + 14);
    g_icons[idx].y = 24 + (idx / per_row) * (DESK_ICON_H + 8);
    g_icons[idx].visible = 1;
    g_icons[idx].is_dir = 0;
    g_icons[idx].is_image = 0;
    label_from_name(token, label, sizeof(label));
    strncpy(g_icons[idx].label, label[0] ? label : "Modul", sizeof(g_icons[idx].label) - 1);
    g_icons[idx].label[sizeof(g_icons[idx].label) - 1] = '\0';
    strncpy(g_icons[idx].exec_path, exec_path, sizeof(g_icons[idx].exec_path) - 1);
    g_icons[idx].exec_path[sizeof(g_icons[idx].exec_path) - 1] = '\0';
    {
        char icon_path[160];
        app_icon_path_for_exec(g_icons[idx].exec_path, icon_path, sizeof(icon_path));
        icon_set_app_icon(&g_icons[idx], icon_path);
    }
}

static void add_builtin_explorer_icon(void) {
    int idx;
    int per_row;
    const char* path = "::explorer";
    if (icon_exists_path(path)) return;
    if (g_icon_count >= DESK_MAX_ICONS) return;
    idx = g_icon_count++;
    memset(&g_icons[idx], 0, sizeof(g_icons[idx]));
    g_icons[idx].id = 5000u;
    per_row = ((int)g_fb.width - 24) / (DESK_ICON_W + 14);
    if (per_row < 1) per_row = 1;
    g_icons[idx].x = 24 + (idx % per_row) * (DESK_ICON_W + 14);
    g_icons[idx].y = 24 + (idx / per_row) * (DESK_ICON_H + 8);
    g_icons[idx].visible = 1;
    g_icons[idx].is_dir = 0;
    g_icons[idx].is_image = 0;
    strncpy(g_icons[idx].label, "Explorer", sizeof(g_icons[idx].label) - 1);
    g_icons[idx].label[sizeof(g_icons[idx].label) - 1] = '\0';
    strncpy(g_icons[idx].exec_path, path, sizeof(g_icons[idx].exec_path) - 1);
    g_icons[idx].exec_path[sizeof(g_icons[idx].exec_path) - 1] = '\0';
    {
        char icon_path[160];
        app_icon_path_for_exec(g_icons[idx].exec_path, icon_path, sizeof(icon_path));
        icon_set_app_icon(&g_icons[idx], icon_path);
    }
}

static void add_builtin_console_icon(void) {
    int idx;
    int per_row;
    const char* path = "::console";
    if (icon_exists_path(path)) return;
    if (g_icon_count >= DESK_MAX_ICONS) return;
    idx = g_icon_count++;
    memset(&g_icons[idx], 0, sizeof(g_icons[idx]));
    g_icons[idx].id = 5001u;
    per_row = ((int)g_fb.width - 24) / (DESK_ICON_W + 14);
    if (per_row < 1) per_row = 1;
    g_icons[idx].x = 24 + (idx % per_row) * (DESK_ICON_W + 14);
    g_icons[idx].y = 24 + (idx / per_row) * (DESK_ICON_H + 8);
    g_icons[idx].visible = 1;
    g_icons[idx].is_dir = 0;
    g_icons[idx].is_image = 0;
    strncpy(g_icons[idx].label, "Konsole", sizeof(g_icons[idx].label) - 1);
    g_icons[idx].label[sizeof(g_icons[idx].label) - 1] = '\0';
    strncpy(g_icons[idx].exec_path, path, sizeof(g_icons[idx].exec_path) - 1);
    g_icons[idx].exec_path[sizeof(g_icons[idx].exec_path) - 1] = '\0';
    {
        char icon_path[160];
        app_icon_path_for_exec(g_icons[idx].exec_path, icon_path, sizeof(icon_path));
        icon_set_app_icon(&g_icons[idx], icon_path);
    }
}

static void add_builtin_settings_icon(void) {
    int idx;
    int per_row;
    const char* path = "::settings";
    if (icon_exists_path(path)) return;
    if (g_icon_count >= DESK_MAX_ICONS) return;
    idx = g_icon_count++;
    memset(&g_icons[idx], 0, sizeof(g_icons[idx]));
    g_icons[idx].id = 5002u;
    per_row = ((int)g_fb.width - 24) / (DESK_ICON_W + 14);
    if (per_row < 1) per_row = 1;
    g_icons[idx].x = 24 + (idx % per_row) * (DESK_ICON_W + 14);
    g_icons[idx].y = 24 + (idx / per_row) * (DESK_ICON_H + 8);
    g_icons[idx].visible = 1;
    g_icons[idx].is_dir = 0;
    g_icons[idx].is_image = 0;
    strncpy(g_icons[idx].label, "Settings", sizeof(g_icons[idx].label) - 1);
    g_icons[idx].label[sizeof(g_icons[idx].label) - 1] = '\0';
    strncpy(g_icons[idx].exec_path, path, sizeof(g_icons[idx].exec_path) - 1);
    g_icons[idx].exec_path[sizeof(g_icons[idx].exec_path) - 1] = '\0';
    {
        char icon_path[160];
        app_icon_path_for_exec(g_icons[idx].exec_path, icon_path, sizeof(icon_path));
        icon_set_app_icon(&g_icons[idx], icon_path);
    }
}

static void add_builtin_browser_icon(void) {
    int idx;
    int per_row;
    const char* path = "::browser";
    if (icon_exists_path(path)) return;
    if (g_icon_count >= DESK_MAX_ICONS) return;
    idx = g_icon_count++;
    memset(&g_icons[idx], 0, sizeof(g_icons[idx]));
    g_icons[idx].id = 5003u;
    per_row = ((int)g_fb.width - 24) / (DESK_ICON_W + 14);
    if (per_row < 1) per_row = 1;
    g_icons[idx].x = 24 + (idx % per_row) * (DESK_ICON_W + 14);
    g_icons[idx].y = 24 + (idx / per_row) * (DESK_ICON_H + 8);
    g_icons[idx].visible = 1;
    g_icons[idx].is_dir = 0;
    g_icons[idx].is_image = 0;
    strncpy(g_icons[idx].label, "Browser", sizeof(g_icons[idx].label) - 1);
    g_icons[idx].label[sizeof(g_icons[idx].label) - 1] = '\0';
    strncpy(g_icons[idx].exec_path, path, sizeof(g_icons[idx].exec_path) - 1);
    g_icons[idx].exec_path[sizeof(g_icons[idx].exec_path) - 1] = '\0';
    {
        char icon_path[160];
        app_icon_path_for_exec(g_icons[idx].exec_path, icon_path, sizeof(icon_path));
        icon_set_app_icon(&g_icons[idx], icon_path);
    }
}
static void scan_elfs_dir_flat(const char* dir) {
    ntux_dirent_t ents[DESK_LS_MAX];
    uint64_t count = 0;
    if (!dir || !dir[0] || g_icon_count >= DESK_MAX_ICONS) return;
    if (sys_fs_list_dir(dir, ents, DESK_LS_MAX, &count) != 0) return;
    if (count > DESK_LS_MAX) count = DESK_LS_MAX;

    for (uint64_t i = 0; i < count && g_icon_count < DESK_MAX_ICONS; ++i) {
        char path[256];
        if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
        if (join_path(dir, ents[i].name, path, sizeof(path)) != 0) continue;
        if (ents[i].is_dir) continue;
        if (!path_is_elf_exec(path, ents[i].name)) continue;
        add_exec_icon(path, ents[i].name);
    }
}

static void scan_desktop_dir_icons(void) {
    ntux_dirent_t ents[DESK_LS_MAX];
    uint64_t count = 0;
    if (!g_desktop_dir[0]) return;
    if (sys_fs_list_dir(g_desktop_dir, ents, DESK_LS_MAX, &count) != 0) return;
    if (count > DESK_LS_MAX) count = DESK_LS_MAX;
    for (uint64_t i = 0; i < count && g_icon_count < DESK_MAX_ICONS; ++i) {
        char full[256];
        char label[24];
        int idx;
        int per_row;
        if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
        if (join_path(g_desktop_dir, ents[i].name, full, sizeof(full)) != 0) continue;
        if (icon_exists_path(full)) continue;
        idx = g_icon_count++;
        memset(&g_icons[idx], 0, sizeof(g_icons[idx]));
        g_icons[idx].id = 20000u + (uint64_t)idx;
        per_row = ((int)g_fb.width - 24) / (DESK_ICON_W + 14);
        if (per_row < 1) per_row = 1;
        g_icons[idx].x = 24 + (idx % per_row) * (DESK_ICON_W + 14);
        g_icons[idx].y = 24 + (idx / per_row) * (DESK_ICON_H + 8);
        g_icons[idx].visible = 1;
        g_icons[idx].is_dir = ents[i].is_dir ? 1u : 0u;
        g_icons[idx].is_image = (!ents[i].is_dir && path_is_image_ext(full)) ? 1u : 0u;
        label_from_name(ents[i].name, label, sizeof(label));
        strncpy(g_icons[idx].label, label[0] ? label : "Datei", sizeof(g_icons[idx].label) - 1);
        g_icons[idx].label[sizeof(g_icons[idx].label) - 1] = '\0';
        strncpy(g_icons[idx].exec_path, full, sizeof(g_icons[idx].exec_path) - 1);
        g_icons[idx].exec_path[sizeof(g_icons[idx].exec_path) - 1] = '\0';
        if (!ents[i].is_dir && name_is_elf(ents[i].name)) {
            char icon_path[160];
            app_icon_path_for_exec(g_icons[idx].exec_path, icon_path, sizeof(icon_path));
            icon_set_app_icon(&g_icons[idx], icon_path);
        }
        clamp_icon_position(&g_icons[idx]);
    }
}

static void scan_elfs_recursive(const char* dir, int depth) {
    char p1[256];
    char p2[256];
    (void)depth;
    if (!dir || !dir[0]) return;

    /* Keep scanning stable: flat on root dir + common app subdirs. */
    scan_elfs_dir_flat(dir);

    if (join_path(dir, "apps", p1, sizeof(p1)) == 0) {
        scan_elfs_dir_flat(p1);
    }
    if (join_path(dir, "bin", p1, sizeof(p1)) == 0) {
        scan_elfs_dir_flat(p1);
    }
    if (join_path(dir, "boot", p1, sizeof(p1)) == 0) {
        scan_elfs_dir_flat(p1);
        if (join_path(p1, "modules", p2, sizeof(p2)) == 0) {
            scan_elfs_dir_flat(p2);
        }
    }
}

static void scan_mount_family(const char* prefix, int max_index) {
    char root[32];
    ntux_dirent_t probe[1];
    uint64_t out_n = 0;
    if (!prefix || !prefix[0] || max_index <= 0) return;
    for (int i = 0; i < max_index && g_icon_count < DESK_MAX_ICONS; ++i) {
        root[0] = '\0';
        if (str_append_char(root, sizeof(root), '/') != 0) continue;
        if (str_append(root, sizeof(root), prefix) != 0) continue;
        if (str_append_i32(root, sizeof(root), i) != 0) continue;
        if (sys_fs_list_dir(root, probe, 1, &out_n) != 0) continue;
        scan_elfs_recursive(root, 0);
    }
}

static void scan_drive_partition_family(const char* prefix, int max_drive, int max_part) {
    char root[32];
    ntux_dirent_t probe[1];
    uint64_t n = 0;
    if (!prefix || !prefix[0] || max_drive <= 0 || max_part <= 0) return;
    for (int d = 0; d < max_drive && g_icon_count < DESK_MAX_ICONS; ++d) {
        for (int p = 1; p <= max_part && g_icon_count < DESK_MAX_ICONS; ++p) {
            root[0] = '\0';
            if (str_append_char(root, sizeof(root), '/') != 0) continue;
            if (str_append(root, sizeof(root), prefix) != 0) continue;
            if (str_append_i32(root, sizeof(root), d) != 0) continue;
            if (str_append_char(root, sizeof(root), 'p') != 0) continue;
            if (str_append_i32(root, sizeof(root), p) != 0) continue;
            if (sys_fs_list_dir(root, probe, 1, &n) != 0) continue;
            scan_elfs_recursive(root, 0);
        }
    }
}

static void seed_module_request_icons(void) {
    ntux_module_info_t mods[128];
    uint64_t count = 0;
    if (sys_module_list(mods, 128, &count) != 0) return;
    if (count > 128) count = 128;
    for (uint64_t i = 0; i < count && g_icon_count < DESK_MAX_ICONS; ++i) {
        if (!mods[i].token[0]) continue;
        add_module_request_icon(mods[i].token);
    }
}

static void seed_filesystem_elf_icons(void) {
    /* Always provide core desktop apps first. */
    add_builtin_console_icon();
    add_builtin_explorer_icon();
    add_builtin_settings_icon();
    add_builtin_browser_icon();
    seed_module_request_icons();
    /* Discover common ELF app locations to enable multi-app desktop usage. */
    scan_elfs_dir_flat("/boot/modules");
    scan_elfs_dir_flat("/boot/boot/modules");
    scan_elfs_dir_flat("/bin");
    scan_elfs_dir_flat("/apps");
    scan_elfs_dir_flat("/usr/bin");
    scan_elfs_dir_flat("/boot/res/modules");
    scan_elfs_recursive("/mnt", 0);
    scan_elfs_recursive("/", 0);
    /* Desktop behaves like a real desktop folder. */
    scan_desktop_dir_icons();
}

void desktop_rescan_icons(void) {
    for (int i = 0; i < g_icon_count; ++i) {
        icon_free_preview(&g_icons[i]);
        icon_free_app_icon(&g_icons[i]);
        g_icons[i].preview_failed = 0;
    }
    g_icon_count = 0;
    memset(g_icon_custom_pos, 0, sizeof(g_icon_custom_pos));
    seed_filesystem_elf_icons();
    (void)desktop_conf_load_layout();
    desktop_auto_arrange_icons();
    wallpaper_scan_all();
}

static int desktop_conf_write_buffer(const char* buf, size_t len) {
    char parent[128];
    char name[64];
    if (!g_desktop_conf_ready || !buf) return -1;
    if (sys_fs_exists(g_desktop_conf_path) > 0) {
        return (sys_fs_write_file(g_desktop_conf_path, buf, (uint64_t)len) == 0) ? 0 : -1;
    }
    if (split_parent_name(g_desktop_conf_path, parent, name, sizeof(parent)) != 0) return -1;
    return (sys_fs_create_file(parent, name, buf, (uint64_t)len) == 0) ? 0 : -1;
}

static int desktop_conf_save_layout(void) {
    char out[DESK_CONF_MAX];
    size_t p = 0;
    if (!g_desktop_conf_ready) return -1;
    out[0] = '\0';
    p = (size_t)snprintf(out, sizeof(out),
                         "version=2\ninstalled=true\ndesktop_dir=%s\nwallpaper=%s\nbuiltin_bg=%s\n",
                         g_desktop_dir, g_wallpaper_pref, g_wallpaper_builtin_enabled ? "true" : "false");
    if (p >= sizeof(out)) return -1;
    for (int i = 0; i < g_icon_count; ++i) {
        int n;
        if (!g_icons[i].visible || !g_icons[i].exec_path[0]) continue;
        n = snprintf(out + p, sizeof(out) - p, "icon=%s|%d|%d\n",
                     g_icons[i].exec_path, g_icons[i].x, g_icons[i].y);
        if (n <= 0 || p + (size_t)n >= sizeof(out)) break;
        p += (size_t)n;
    }
    return desktop_conf_write_buffer(out, p);
}

static int desktop_conf_apply_icon_line(const char* line) {
    const char* data;
    const char* sep1;
    const char* sep2;
    char path[160];
    char sx[24];
    char sy[24];
    int x = 0;
    int y = 0;
    size_t plen;
    size_t sx_len;
    size_t sy_len;

    if (!line || strncmp(line, "icon=", 5) != 0) return -1;
    data = line + 5;
    sep1 = strchr(data, '|');
    if (!sep1) return -1;
    sep2 = strchr(sep1 + 1, '|');
    if (!sep2) return -1;

    plen = (size_t)(sep1 - data);
    sx_len = (size_t)(sep2 - (sep1 + 1));
    sy_len = strlen(sep2 + 1);
    if (plen == 0 || plen >= sizeof(path)) return -1;
    if (sx_len == 0 || sx_len >= sizeof(sx) || sy_len == 0 || sy_len >= sizeof(sy)) return -1;

    memcpy(path, data, plen);
    path[plen] = '\0';
    memcpy(sx, sep1 + 1, sx_len);
    sx[sx_len] = '\0';
    memcpy(sy, sep2 + 1, sy_len + 1u);
    if (parse_i32_safe(sx, &x) != 0 || parse_i32_safe(sy, &y) != 0) return -1;

    for (int i = 0; i < g_icon_count; ++i) {
        if (!g_icons[i].visible) continue;
        if (strcmp(g_icons[i].exec_path, path) != 0) continue;
        g_icons[i].x = x;
        g_icons[i].y = y;
        g_icon_custom_pos[i] = 1;
        clamp_icon_position(&g_icons[i]);
        return 0;
    }
    return -1;
}

static int desktop_conf_load_layout(void) {
    char buf[DESK_CONF_MAX];
    uint64_t len = 0;
    char* cur;
    if (!g_desktop_conf_ready) return -1;
    if (sys_fs_exists(g_desktop_conf_path) <= 0) return -1;
    if (sys_fs_read_file(g_desktop_conf_path, buf, sizeof(buf) - 1u, &len) != 0) return -1;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1u;
    buf[len] = '\0';
    cur = buf;
    g_wallpaper_custom = 0;
    g_wallpaper_builtin_enabled = 1;
    while (*cur) {
        char* line = cur;
        while (*cur && *cur != '\n' && *cur != '\r') cur++;
        if (*cur == '\r') *cur++ = '\0';
        if (*cur == '\n') *cur++ = '\0';
        if (!line[0]) continue;
        if (strncmp(line, "wallpaper=", 10) == 0) {
            strncpy(g_wallpaper_pref, line + 10, sizeof(g_wallpaper_pref) - 1);
            g_wallpaper_pref[sizeof(g_wallpaper_pref) - 1] = '\0';
            if (strncmp(g_wallpaper_pref, "img:", 4) == 0 || strncmp(g_wallpaper_pref, "bmp:", 4) == 0) {
                g_wallpaper_custom = 1;
            } else if (strcmp(g_wallpaper_pref, "gradient") != 0) {
                g_wallpaper_custom = 1;
            }
            continue;
        }
        if (strncmp(line, "builtin_bg=", 11) == 0) {
            g_wallpaper_builtin_enabled = parse_conf_bool_true(line + 11) ? 1 : 0;
            continue;
        }
        (void)desktop_conf_apply_icon_line(line);
    }
    desktop_apply_wallpaper_pref();
    return 0;
}

enum {
    BROWSER_BTN_NONE = 0,
    BROWSER_BTN_UP = 1,
    BROWSER_BTN_REFRESH = 2,
    BROWSER_BTN_NEW_DIR = 3,
    BROWSER_BTN_NEW_FILE = 4,
    BROWSER_BTN_DELETE = 5,
    BROWSER_BTN_RENAME = 6,
    BROWSER_BTN_COPY = 7,
    BROWSER_BTN_PASTE = 8
};

static void browser_path_join(const char* base, const char* name, char* out, size_t cap) {
    if (!out || cap < 2) return;
    out[0] = '\0';
    if (!base || !name || !name[0]) return;
    (void)join_path(base, name, out, cap);
}

static desk_browser_state_t* browser_state_get(int id) {
    if (id <= 0 || id > DESK_MAX_WINDOWS) return 0;
    return g_browser_states[id - 1];
}

static int browser_state_register(desk_browser_state_t* st) {
    if (!st) return 0;
    for (int i = 0; i < DESK_MAX_WINDOWS; ++i) {
        if (!g_browser_states[i]) {
            g_browser_states[i] = st;
            st->id = i + 1;
            return st->id;
        }
    }
    return 0;
}

static void browser_state_unregister(int id) {
    if (id <= 0 || id > DESK_MAX_WINDOWS) return;
    if (g_browser_states[id - 1]) g_browser_states[id - 1] = 0;
}

static desk_browser_state_t* window_browser_state(const desk_window_t* w) {
    if (!w || !w->file_browser) return 0;
    return browser_state_get(w->browser_id);
}

static void picker_path_join(const char* base, const char* name, char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!base || !base[0]) {
        strncpy(out, "/", cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    size_t bl = strlen(base);
    if (bl + 2 >= cap) return;
    strncpy(out, base, cap - 1);
    out[cap - 1] = '\0';
    if (out[bl - 1] != '/') {
        out[bl++] = '/';
        out[bl] = '\0';
    }
    strncat(out, name, cap - bl - 1);
}

static void browser_preview_clear(desk_browser_state_t* st) {
    if (!st) return;
    if (st->preview) {
        free(st->preview);
        st->preview = 0;
    }
    st->preview_w = 0;
    st->preview_h = 0;
    st->preview_ready = 0;
    st->preview_loading = 0;
    st->preview_path[0] = '\0';
}

static void browser_text_preview_clear(desk_browser_state_t* st) {
    if (!st) return;
    st->text_preview[0] = '\0';
    st->text_preview_len = 0;
    st->text_preview_ready = 0;
    st->text_preview_path[0] = '\0';
}

static int browser_text_preview_load(desk_browser_state_t* st, const char* path) {
    if (!st || !path || !path[0]) return -1;
    if (st->text_preview_ready && strcmp(st->text_preview_path, path) == 0) return 0;
    uint64_t len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0) return -1;
    if (len == 0) {
        browser_text_preview_clear(st);
        st->text_preview_ready = 1;
        strncpy(st->text_preview_path, path, sizeof(st->text_preview_path) - 1);
        st->text_preview_path[sizeof(st->text_preview_path) - 1] = '\0';
        return 0;
    }
    if (len > 4096u) len = 4096u;
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) return -1;
    if (sys_fs_read_file(path, buf, len, &len) != 0) {
        free(buf);
        return -1;
    }
    buf[len] = '\0';
    int out_len = 0;
    int max_out = (int)sizeof(st->text_preview) - 1;
    for (uint64_t i = 0; i < len && out_len < max_out; ++i) {
        char c = buf[i];
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        if (c < 32 && c != '\n') c = '.';
        st->text_preview[out_len++] = c;
    }
    st->text_preview[out_len] = '\0';
    st->text_preview_len = out_len;
    st->text_preview_ready = 1;
    strncpy(st->text_preview_path, path, sizeof(st->text_preview_path) - 1);
    st->text_preview_path[sizeof(st->text_preview_path) - 1] = '\0';
    free(buf);
    return 0;
}

static void img_job_enqueue(uint8_t type, int idx, const char* path, int max_w, int max_h, int browser_id) {
    if (!path || !path[0]) return;
    if (g_img_job_count >= IMG_JOB_MAX) return;
    img_job_t* job = &g_img_jobs[g_img_job_tail];
    job->type = type;
    job->idx = idx;
    job->max_w = max_w;
    job->max_h = max_h;
    strncpy(job->path, path, sizeof(job->path) - 1);
    job->path[sizeof(job->path) - 1] = '\0';
    job->browser_id = browser_id;
    g_img_job_tail = (g_img_job_tail + 1) % IMG_JOB_MAX;
    g_img_job_count++;
}

static int img_job_pop(img_job_t* out) {
    if (g_img_job_count <= 0) return 0;
    *out = g_img_jobs[g_img_job_head];
    g_img_job_head = (g_img_job_head + 1) % IMG_JOB_MAX;
    g_img_job_count--;
    return 1;
}

static void wallpaper_job_cancel(void) {
    if (g_wallpaper_job_active) {
        image_free(&g_wallpaper_job_img);
        g_wallpaper_job_active = 0;
    }
    if (g_wallpaper_job_xmap) {
        free(g_wallpaper_job_xmap);
        g_wallpaper_job_xmap = 0;
    }
    g_wallpaper_job_row = 0;
    g_wallpaper_job_path[0] = '\0';
}

static void wallpaper_queue_render(const char* path) {
    if (!path || !path[0]) return;
    if (strcmp(g_wallpaper_job_path, path) == 0 && g_wallpaper_job_active) return;
    wallpaper_job_cancel();
    strncpy(g_wallpaper_job_path, path, sizeof(g_wallpaper_job_path) - 1);
    g_wallpaper_job_path[sizeof(g_wallpaper_job_path) - 1] = '\0';
    g_wallpaper_notify_pending = 1;
    img_job_enqueue(IMG_JOB_WALLPAPER, -1, g_wallpaper_job_path, (int)g_fb.width, (int)g_fb.height, 0);
}

static void wallpaper_job_step(uint32_t max_rows) {
    if (!g_wallpaper_job_active || !g_bg_next) return;
    if (g_wallpaper_job_row >= g_fb.height) {
        if (!g_bg) {
            g_bg = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
            if (g_bg) {
                memcpy(g_bg, g_bg_next, g_pixels * sizeof(uint32_t));
                g_bg_blending = 0;
            }
        }
        image_free(&g_wallpaper_job_img);
        g_wallpaper_job_active = 0;
        if (g_wallpaper_job_xmap) {
            free(g_wallpaper_job_xmap);
            g_wallpaper_job_xmap = 0;
        }
        g_wallpaper_job_path[0] = '\0';
        if (g_bg) {
            g_bg_blend = 0.0f;
            g_bg_blending = 1;
        }
        if (g_wallpaper_notify_pending) {
            notify_push("Wallpaper", "Updated successfully");
            g_wallpaper_notify_pending = 0;
        }
        g_desktop_dirty = 1;
        return;
    }
    uint32_t rows = max_rows;
    if (rows < 1) rows = 1;
    uint32_t end = g_wallpaper_job_row + rows;
    if (end > g_fb.height) end = g_fb.height;
    uint32_t src_w = (uint32_t)g_wallpaper_job_img.width;
    uint32_t src_h = (uint32_t)g_wallpaper_job_img.height;
    int ch = (g_wallpaper_job_img.channels == 4) ? 4 : 3;
    for (uint32_t y = g_wallpaper_job_row; y < end; ++y) {
        uint32_t sy = (uint32_t)(((uint64_t)y * (uint64_t)src_h) / (uint64_t)g_fb.height);
        const uint8_t* row = g_wallpaper_job_img.data + (uint64_t)sy * (uint64_t)src_w * (uint64_t)ch;
        uint32_t* out = g_bg_next + (uint64_t)y * (uint64_t)g_fb.width;
        for (uint32_t x = 0; x < g_fb.width; ++x) {
            const uint8_t* px = row + g_wallpaper_job_xmap[x];
            out[x] = ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[2];
        }
    }
    g_wallpaper_job_row = end;
    g_desktop_dirty = 1;
}

static void img_job_pump(void) {
    if (g_img_job_count <= 0) return;
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;
    uint64_t budget = hz / 100u;
    if (budget < 1u) budget = 1u;
    uint64_t start = sys_get_ticks();
    while (g_img_job_count > 0) {
        img_job_t job;
        if (!img_job_pop(&job)) break;
        if (job.type == IMG_JOB_ICON) {
            if (job.idx >= 0 && job.idx < g_icon_count) {
                desk_icon_t* icon = &g_icons[job.idx];
                if (!icon->is_image || icon->preview_ready || icon->preview_failed == 1) {
                    icon->preview_loading = 0;
                } else {
                    image_t img;
                    if (image_decode_file_scaled(job.path, 3, job.max_w, job.max_h, &img) == 0 &&
                        img.data && img.width > 0 && img.height > 0) {
                        icon_free_preview(icon);
                        icon->preview = img.data;
                        icon->preview_w = (uint16_t)img.width;
                        icon->preview_h = (uint16_t)img.height;
                        icon->preview_ready = 1;
                        icon->preview_loading = 0;
                        img.data = 0;
                        g_desktop_dirty = 1;
                    } else {
                        icon->preview_failed = 1;
                        icon->preview_loading = 0;
                    }
                }
            }
        } else if (job.type == IMG_JOB_BROWSER_THUMB) {
            desk_browser_state_t* st = browser_state_get(job.browser_id);
            if (!st) continue;
            if (job.idx >= 0 && job.idx < st->count) {
                desk_thumb_t* t = &st->thumbs[job.idx];
                char full[256];
                browser_path_join(st->path, st->ents[job.idx].name, full, sizeof(full));
                if (strcmp(full, job.path) != 0) {
                    t->loading = 0;
                } else {
                    image_t img;
                    if (image_decode_file_scaled(job.path, 3, job.max_w, job.max_h, &img) == 0 &&
                        img.data && img.width > 0 && img.height > 0) {
                        if (t->pixels) free(t->pixels);
                        strncpy(t->path, job.path, sizeof(t->path) - 1);
                        t->path[sizeof(t->path) - 1] = '\0';
                        t->pixels = img.data;
                        t->w = img.width;
                        t->h = img.height;
                        t->ready = 1;
                        t->failed = 0;
                        t->loading = 0;
                        img.data = 0;
                        g_desktop_dirty = 1;
                    } else {
                        t->failed = 1;
                        t->loading = 0;
                    }
                }
            }
        } else if (job.type == IMG_JOB_WALL_THUMB) {
            if (job.idx >= 0 && job.idx < g_wallpaper_count) {
                desk_wall_thumb_t* t = &g_wallpaper_thumbs[job.idx];
                if (strcmp(g_wallpaper_paths[job.idx], job.path) != 0) {
                    t->loading = 0;
                } else {
                    image_t img;
                    if (image_decode_file_scaled(job.path, 3, job.max_w, job.max_h, &img) == 0 &&
                        img.data && img.width > 0 && img.height > 0) {
                        if (t->pixels) free(t->pixels);
                        strncpy(t->path, job.path, sizeof(t->path) - 1);
                        t->path[sizeof(t->path) - 1] = '\0';
                        t->pixels = img.data;
                        t->w = img.width;
                        t->h = img.height;
                        t->ready = 1;
                        t->failed = 0;
                        t->loading = 0;
                        img.data = 0;
                        g_desktop_dirty = 1;
                    } else {
                        t->failed = 1;
                        t->loading = 0;
                    }
                }
            }
        } else if (job.type == IMG_JOB_BROWSER_PREVIEW) {
            desk_browser_state_t* st = browser_state_get(job.browser_id);
            if (!st) continue;
            if (strcmp(st->preview_path, job.path) != 0 && st->preview_ready) {
                st->preview_loading = 0;
            } else {
                image_t img;
                if (image_decode_file_scaled(job.path, 3, job.max_w, job.max_h, &img) == 0 &&
                    img.data && img.width > 0 && img.height > 0) {
                    browser_preview_clear(st);
                    st->preview = img.data;
                    st->preview_w = img.width;
                    st->preview_h = img.height;
                    st->preview_ready = 1;
                    st->preview_loading = 0;
                    strncpy(st->preview_path, job.path, sizeof(st->preview_path) - 1);
                    st->preview_path[sizeof(st->preview_path) - 1] = '\0';
                    img.data = 0;
                    g_desktop_dirty = 1;
                } else {
                    st->preview_loading = 0;
                }
            }
        } else if (job.type == IMG_JOB_WALLPAPER) {
            if (!g_bg_next) {
                g_bg_next = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
                if (!g_bg_next) {
                    if (g_wallpaper_notify_pending) {
                        notify_push("Wallpaper", "Failed to load image");
                        g_wallpaper_notify_pending = 0;
                    }
                    continue;
                }
            }
            image_t img;
            if (image_decode_file_scaled(job.path, 3, job.max_w, job.max_h, &img) == 0 &&
                img.data && img.width > 0 && img.height > 0) {
                wallpaper_job_cancel();
                g_wallpaper_job_img = img;
                g_wallpaper_job_row = 0;
                g_wallpaper_job_active = 1;
                if (g_wallpaper_job_xmap) free(g_wallpaper_job_xmap);
                g_wallpaper_job_xmap = (int*)malloc((size_t)g_fb.width * sizeof(int));
                if (!g_wallpaper_job_xmap) {
                    wallpaper_job_cancel();
                    if (g_wallpaper_notify_pending) {
                        notify_push("Wallpaper", "Failed to load image");
                        g_wallpaper_notify_pending = 0;
                    }
                    continue;
                }
                int ch = (img.channels == 4) ? 4 : 3;
                for (uint32_t x = 0; x < g_fb.width; ++x) {
                    uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)img.width) / (uint64_t)g_fb.width);
                    g_wallpaper_job_xmap[x] = (int)(sx * (uint32_t)ch);
                }
            } else {
                if (g_wallpaper_notify_pending) {
                    notify_push("Wallpaper", "Failed to load image");
                    g_wallpaper_notify_pending = 0;
                }
            }
        }

        if ((sys_get_ticks() - start) >= budget) break;
    }
}

static void browser_thumbs_clear(desk_browser_state_t* st) {
    if (!st) return;
    for (int i = 0; i < DESK_LS_MAX; ++i) {
        if (st->thumbs[i].pixels) {
            free(st->thumbs[i].pixels);
            st->thumbs[i].pixels = 0;
        }
        st->thumbs[i].w = 0;
        st->thumbs[i].h = 0;
        st->thumbs[i].ready = 0;
        st->thumbs[i].failed = 0;
        st->thumbs[i].loading = 0;
        st->thumbs[i].path[0] = '\0';
    }
}

static void browser_thumb_try_load(desk_browser_state_t* st, int idx, int max_w, int max_h) {
    if (!st) return;
    if (idx < 0 || idx >= st->count) return;
    if (!path_is_image_ext(st->ents[idx].name)) return;
    desk_thumb_t* t = &st->thumbs[idx];
    if (t->ready || t->failed || t->loading) return;

    char full[256];
    browser_path_join(st->path, st->ents[idx].name, full, sizeof(full));
    if (!full[0]) { t->failed = 1; return; }
    t->loading = 1;
    img_job_enqueue(IMG_JOB_BROWSER_THUMB, idx, full, max_w, max_h, st->id);
}

static void browser_clear_marks(desk_browser_state_t* st) {
    if (!st) return;
    memset(st->mark, 0, sizeof(st->mark));
    st->mark_count = 0;
}

static void browser_mark_only(desk_browser_state_t* st, int idx) {
    if (!st) return;
    browser_clear_marks(st);
    if (idx >= 0 && idx < st->count) {
        st->mark[idx] = 1u;
        st->mark_count = 1;
        st->selected = idx;
    }
}

static void browser_mark_all(desk_browser_state_t* st) {
    if (!st) return;
    browser_clear_marks(st);
    for (int i = 0; i < st->count && i < DESK_LS_MAX; ++i) {
        st->mark[i] = 1u;
        st->mark_count++;
    }
    if (st->count > 0) st->selected = 0;
}

static int browser_preview_load(desk_browser_state_t* st, const char* path) {
    if (!st || !path || !path[0]) return -1;
    if (strcmp(st->preview_path, path) == 0 && st->preview_ready) return 0;
    if (st->preview_loading && strcmp(st->preview_path, path) == 0) return 0;
    browser_preview_clear(st);
    st->preview_loading = 1;
    strncpy(st->preview_path, path, sizeof(st->preview_path) - 1);
    st->preview_path[sizeof(st->preview_path) - 1] = '\0';
    img_job_enqueue(IMG_JOB_BROWSER_PREVIEW, -1, path, 160, 120, st->id);
    return 0;
}

static void browser_state_destroy(desk_browser_state_t* st) {
    if (!st) return;
    if (st->ctx_open) browser_menu_close(st);
    browser_preview_clear(st);
    browser_thumbs_clear(st);
    free(st);
}

static int picker_refresh(void) {
    ntux_dirent_t ents[DESK_LS_MAX];
    uint64_t count = 0;
    g_picker_count = 0;
    g_picker_selected = -1;
    g_picker_scroll = 0;
    if (sys_fs_list_dir(g_picker_path, ents, DESK_LS_MAX, &count) != 0) {
        strncpy(g_picker_status, "Directory read failed", sizeof(g_picker_status) - 1);
        g_picker_status[sizeof(g_picker_status) - 1] = '\0';
        return -1;
    }
    for (uint64_t i = 0; i < count && g_picker_count < DESK_LS_MAX; ++i) {
        strncpy(g_picker_ents[g_picker_count].name, ents[i].name, sizeof(g_picker_ents[g_picker_count].name) - 1);
        g_picker_ents[g_picker_count].name[sizeof(g_picker_ents[g_picker_count].name) - 1] = '\0';
        g_picker_ents[g_picker_count].is_dir = ents[i].is_dir ? 1u : 0u;
        g_picker_ents[g_picker_count].size = ents[i].size;
        g_picker_count++;
    }
    g_picker_status[0] = '\0';
    (void)str_append_u64(g_picker_status, sizeof(g_picker_status), (uint64_t)g_picker_count);
    (void)str_append(g_picker_status, sizeof(g_picker_status), " entries");
    return 0;
}

static void picker_go_up(void) {
    size_t len = strlen(g_picker_path);
    while (len > 1 && g_picker_path[len - 1] == '/') len--;
    while (len > 1 && g_picker_path[len - 1] != '/') len--;
    g_picker_path[len] = '\0';
    picker_refresh();
}

static void picker_enter_entry(int idx) {
    char full[256];
    if (idx < 0 || idx >= g_picker_count) return;
    picker_path_join(g_picker_path, g_picker_ents[idx].name, full, sizeof(full));
    if (g_picker_ents[idx].is_dir) {
        strncpy(g_picker_path, full, sizeof(g_picker_path) - 1);
        g_picker_path[sizeof(g_picker_path) - 1] = '\0';
        picker_refresh();
    }
}

static int picker_entry_at(int x, int y, int list_x, int list_y, int list_w, int list_h) {
    if (!in_rect(x, y, list_x, list_y, list_w, list_h)) return -1;
    int row = (y - list_y) / 18;
    int idx = g_picker_scroll + row;
    if (idx < 0 || idx >= g_picker_count) return -1;
    return idx;
}

static void picker_finish(uint32_t code, const char* path) {
    if (g_picker_owner_tid >= 0) {
        (void)sys_dialog_push(g_picker_owner_tid, code, path ? path : "");
    }
    g_picker_active = 0;
    g_picker_owner_tid = -1;
}

void desktop_open_file_picker(const window_msg_t* msg) {
    if (!msg) return;
    g_picker_active = 1;
    g_picker_flags = msg->flags;
    g_picker_owner_tid = (int)msg->owner_tid;
    strncpy(g_picker_title, msg->text[0] ? msg->text : "Open File", sizeof(g_picker_title) - 1);
    g_picker_title[sizeof(g_picker_title) - 1] = '\0';
    strncpy(g_picker_path, msg->text2[0] ? msg->text2 : "/", sizeof(g_picker_path) - 1);
    g_picker_path[sizeof(g_picker_path) - 1] = '\0';
    (void)picker_refresh();
    if (g_picker_count > 0) g_picker_selected = 0;
}

static void draw_file_picker(void) {
    if (!g_picker_active) return;
    const desk_theme_t* th = desk_theme();
    int pw = 560;
    int ph = 340;
    int px = (int)g_fb.width / 2 - pw / 2;
    int py = (int)g_fb.height / 2 - ph / 2;
    int list_x = px + 16;
    int list_y = py + 64;
    int list_w = pw - 32;
    int list_h = ph - 120;

    fill_round_rect(px, py, pw, ph, 8, color_lerp(th->taskbar_bg, 0xFF000000u, 35u));
    draw_round_rect(px, py, pw, ph, 8, th->taskbar_border);
    fill_rect(px + 1, py + 1, pw - 2, 26, th->title_focus);
    draw_text(px + 10, py + 8, g_picker_title, th->text_main);

    fill_round_rect(px + 10, py + 32, 50, 18, 6, th->title_blur);
    draw_round_rect(px + 10, py + 32, 50, 18, 6, th->taskbar_border);
    draw_text(px + 18, py + 36, "Up", th->text_main);

    draw_text(px + 70, py + 36, g_picker_path, th->text_dim);

    fill_round_rect(list_x, list_y, list_w, list_h, 6, 0xFF0B1119u);
    draw_round_rect(list_x, list_y, list_w, list_h, 6, th->taskbar_border);

    int visible_rows = list_h / 18;
    for (int i = 0; i < visible_rows; ++i) {
        int idx = g_picker_scroll + i;
        if (idx >= g_picker_count) break;
        int ry = list_y + i * 18;
        if (idx == g_picker_selected) {
            fill_rect(list_x + 1, ry + 1, list_w - 2, 16, color_lerp(th->accent, 0xFF000000u, 30u));
        }
        draw_text(list_x + 6, ry + 4, g_picker_ents[idx].is_dir ? "[D]" : "[F]", g_picker_ents[idx].is_dir ? 0xFFFFD166u : 0xFFD7E8FFu);
        draw_text(list_x + 34, ry + 4, g_picker_ents[idx].name, 0xFFEAF4FFu);
    }

    draw_text(px + 16, py + ph - 32, g_picker_status, th->text_dim);

    int btn_w = 90;
    int btn_h = 20;
    int ok_x = px + pw - 2 * btn_w - 24;
    int cancel_x = px + pw - btn_w - 12;
    int btn_y = py + ph - 40;
    fill_round_rect(ok_x, btn_y, btn_w, btn_h, 6, th->accent);
    draw_round_rect(ok_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
    draw_text(ok_x + 24, btn_y + 6, (g_picker_flags & WINDOW_PICKER_SAVE) ? "Save" : "Open", 0xFF091018u);
    fill_round_rect(cancel_x, btn_y, btn_w, btn_h, 6, th->title_blur);
    draw_round_rect(cancel_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
    draw_text(cancel_x + 18, btn_y + 6, "Cancel", th->text_main);
}

static void draw_message_box(void) {
    if (!g_msgbox_active) return;
    const desk_theme_t* th = desk_theme();
    int pw = 420;
    int ph = 180;
    int px = (int)g_fb.width / 2 - pw / 2;
    int py = (int)g_fb.height / 2 - ph / 2;
    fill_round_rect(px, py, pw, ph, 8, color_lerp(th->taskbar_bg, 0xFF000000u, 35u));
    draw_round_rect(px, py, pw, ph, 8, th->taskbar_border);
    fill_rect(px + 1, py + 1, pw - 2, 26, th->title_focus);
    draw_text(px + 10, py + 8, g_msgbox_title, th->text_main);

    draw_text(px + 16, py + 44, g_msgbox_text, th->text_main);

    int btn_w = 90;
    int btn_h = 20;
    int btn_y = py + ph - 36;
    if (g_msgbox_flags == WINDOW_MSGBOX_YES_NO) {
        int yes_x = px + pw - 2 * btn_w - 24;
        int no_x = px + pw - btn_w - 12;
        fill_round_rect(yes_x, btn_y, btn_w, btn_h, 6, th->accent);
        draw_round_rect(yes_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
        draw_text(yes_x + 28, btn_y + 6, "Yes", 0xFF091018u);
        fill_round_rect(no_x, btn_y, btn_w, btn_h, 6, th->title_blur);
        draw_round_rect(no_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
        draw_text(no_x + 30, btn_y + 6, "No", th->text_main);
    } else if (g_msgbox_flags == WINDOW_MSGBOX_OK_CANCEL) {
        int ok_x = px + pw - 2 * btn_w - 24;
        int cancel_x = px + pw - btn_w - 12;
        fill_round_rect(ok_x, btn_y, btn_w, btn_h, 6, th->accent);
        draw_round_rect(ok_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
        draw_text(ok_x + 28, btn_y + 6, "OK", 0xFF091018u);
        fill_round_rect(cancel_x, btn_y, btn_w, btn_h, 6, th->title_blur);
        draw_round_rect(cancel_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
        draw_text(cancel_x + 18, btn_y + 6, "Cancel", th->text_main);
    } else {
        int ok_x = px + pw - btn_w - 12;
        fill_round_rect(ok_x, btn_y, btn_w, btn_h, 6, th->accent);
        draw_round_rect(ok_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
        draw_text(ok_x + 28, btn_y + 6, "OK", 0xFF091018u);
    }
}

void desktop_open_message_box(const window_msg_t* msg) {
    if (!msg) return;
    g_msgbox_active = 1;
    g_msgbox_flags = msg->flags;
    g_msgbox_owner_tid = (int)msg->owner_tid;
    strncpy(g_msgbox_title, msg->text[0] ? msg->text : "Message", sizeof(g_msgbox_title) - 1);
    g_msgbox_title[sizeof(g_msgbox_title) - 1] = '\0';
    strncpy(g_msgbox_text, msg->text2[0] ? msg->text2 : "", sizeof(g_msgbox_text) - 1);
    g_msgbox_text[sizeof(g_msgbox_text) - 1] = '\0';
}

static void namebox_open(int mode, const char* title, const char* initial, const char* base, const char* old_path, int browser_id) {
    g_namebox_active = 1;
    g_namebox_mode = mode;
    g_namebox_browser_id = browser_id;
    strncpy(g_namebox_title, title ? title : "Name", sizeof(g_namebox_title) - 1);
    g_namebox_title[sizeof(g_namebox_title) - 1] = '\0';
    g_namebox_value[0] = '\0';
    if (initial && initial[0]) {
        strncpy(g_namebox_value, initial, sizeof(g_namebox_value) - 1);
        g_namebox_value[sizeof(g_namebox_value) - 1] = '\0';
    }
    g_namebox_base[0] = '\0';
    if (base && base[0]) {
        strncpy(g_namebox_base, base, sizeof(g_namebox_base) - 1);
        g_namebox_base[sizeof(g_namebox_base) - 1] = '\0';
    }
    g_namebox_old[0] = '\0';
    if (old_path && old_path[0]) {
        strncpy(g_namebox_old, old_path, sizeof(g_namebox_old) - 1);
        g_namebox_old[sizeof(g_namebox_old) - 1] = '\0';
    }
}

static void namebox_close(void) {
    g_namebox_active = 0;
    g_namebox_mode = NAMEBOX_NONE;
    g_namebox_browser_id = 0;
}

static int namebox_valid_name(const char* name) {
    if (!name || !name[0]) return 0;
    for (const char* p = name; *p; ++p) {
        if (*p == '/' || *p == '\\') return 0;
    }
    return 1;
}

static void namebox_confirm(void) {
    if (!namebox_valid_name(g_namebox_value)) {
        namebox_close();
        return;
    }
    int rc = -1;
    if (g_namebox_mode == NAMEBOX_DESKTOP_NEW_FOLDER) {
        rc = sys_fs_mkdir(g_desktop_dir, g_namebox_value);
        if (rc == 0) desktop_rescan_icons();
    } else if (g_namebox_mode == NAMEBOX_DESKTOP_NEW_FILE) {
        rc = sys_fs_create_file(g_desktop_dir, g_namebox_value, "", 0);
        if (rc == 0) desktop_rescan_icons();
    } else if (g_namebox_mode == NAMEBOX_BROWSER_NEW_FOLDER) {
        desk_browser_state_t* st = browser_state_get(g_namebox_browser_id);
        if (st) {
            rc = sys_fs_mkdir(st->path, g_namebox_value);
            if (rc == 0) browser_refresh(st);
        }
    } else if (g_namebox_mode == NAMEBOX_BROWSER_NEW_FILE) {
        desk_browser_state_t* st = browser_state_get(g_namebox_browser_id);
        if (st) {
            rc = sys_fs_create_file(st->path, g_namebox_value, "", 0);
            if (rc == 0) browser_refresh(st);
        }
    } else if (g_namebox_mode == NAMEBOX_BROWSER_RENAME) {
        desk_browser_state_t* st = browser_state_get(g_namebox_browser_id);
        if (st && g_namebox_old[0]) {
            rc = sys_fs_rename(g_namebox_old, g_namebox_value);
            if (rc == 0) browser_refresh(st);
        }
    } else if (g_namebox_mode == NAMEBOX_PICKER_SAVE) {
        char full[256];
        full[0] = '\0';
        if (g_namebox_base[0]) {
            picker_path_join(g_namebox_base, g_namebox_value, full, sizeof(full));
            if (full[0]) {
                picker_finish(1u, full);
                rc = 0;
            }
        }
    }
    if (rc != 0) {
        desk_browser_state_t* st = browser_state_get(g_namebox_browser_id);
        if (st) {
            strncpy(st->status, "Action failed", sizeof(st->status) - 1);
            st->status[sizeof(st->status) - 1] = '\0';
        }
    }
    namebox_close();
    g_desktop_dirty = 1;
}

static int namebox_handle_mouse(int mx, int my, int left_edge) {
    if (!g_namebox_active) return 0;
    if (!left_edge) return 1;
    int pw = 420;
    int ph = 170;
    int px = (int)g_fb.width / 2 - pw / 2;
    int py = (int)g_fb.height / 2 - ph / 2;
    int btn_w = 90;
    int btn_h = 22;
    int ok_x = px + pw - 2 * btn_w - 24;
    int cancel_x = px + pw - btn_w - 12;
    int btn_y = py + ph - 36;
    if (in_rect(mx, my, ok_x, btn_y, btn_w, btn_h)) {
        namebox_confirm();
        return 1;
    }
    if (in_rect(mx, my, cancel_x, btn_y, btn_w, btn_h)) {
        namebox_close();
        return 1;
    }
    return 1;
}

static void draw_namebox(void) {
    if (!g_namebox_active) return;
    const desk_theme_t* th = desk_theme();
    int pw = 420;
    int ph = 170;
    int px = (int)g_fb.width / 2 - pw / 2;
    int py = (int)g_fb.height / 2 - ph / 2;
    fill_round_rect(px, py, pw, ph, 8, color_lerp(th->taskbar_bg, 0xFF000000u, 35u));
    draw_round_rect(px, py, pw, ph, 8, th->taskbar_border);
    fill_rect(px + 1, py + 1, pw - 2, 26, th->title_focus);
    draw_text(px + 10, py + 8, g_namebox_title, th->text_main);

    fill_round_rect(px + 16, py + 48, pw - 32, 26, 6, th->title_blur);
    draw_round_rect(px + 16, py + 48, pw - 32, 26, 6, th->taskbar_border);
    char line[80];
    strncpy(line, g_namebox_value, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    draw_text(px + 22, py + 56, line[0] ? line : "Enter name...", line[0] ? th->text_main : th->text_dim);

    int btn_w = 90;
    int btn_h = 22;
    int ok_x = px + pw - 2 * btn_w - 24;
    int cancel_x = px + pw - btn_w - 12;
    int btn_y = py + ph - 36;
    fill_round_rect(ok_x, btn_y, btn_w, btn_h, 6, th->accent);
    draw_round_rect(ok_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
    draw_text(ok_x + 30, btn_y + 7, "OK", 0xFF091018u);
    fill_round_rect(cancel_x, btn_y, btn_w, btn_h, 6, th->title_blur);
    draw_round_rect(cancel_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
    draw_text(cancel_x + 18, btn_y + 7, "Cancel", th->text_main);
}

static int picker_handle_mouse(int mx, int my, int left_edge, int scroll) {
    if (!g_picker_active) return 0;
    int pw = 560;
    int ph = 340;
    int px = (int)g_fb.width / 2 - pw / 2;
    int py = (int)g_fb.height / 2 - ph / 2;
    int list_x = px + 16;
    int list_y = py + 64;
    int list_w = pw - 32;
    int list_h = ph - 120;
    int btn_w = 90;
    int btn_h = 20;
    int ok_x = px + pw - 2 * btn_w - 24;
    int cancel_x = px + pw - btn_w - 12;
    int btn_y = py + ph - 40;

    if (scroll != 0) {
        int delta = scroll > 0 ? -1 : 1;
        g_picker_scroll += delta;
        if (g_picker_scroll < 0) g_picker_scroll = 0;
        if (g_picker_scroll > g_picker_count - 1) g_picker_scroll = g_picker_count > 0 ? g_picker_count - 1 : 0;
    }

    if (!left_edge) return 1;

    if (in_rect(mx, my, px + 10, py + 32, 50, 18)) {
        picker_go_up();
        return 1;
    }
    if (in_rect(mx, my, ok_x, btn_y, btn_w, btn_h)) {
        if (g_picker_flags & WINDOW_PICKER_SAVE) {
            if (g_picker_selected >= 0 && g_picker_selected < g_picker_count) {
                char full[256];
                picker_path_join(g_picker_path, g_picker_ents[g_picker_selected].name, full, sizeof(full));
                if (g_picker_ents[g_picker_selected].is_dir) {
                    namebox_open(NAMEBOX_PICKER_SAVE, "Save File", "New File.txt", full, 0, 0);
                } else {
                    picker_finish(1u, full);
                }
            } else {
                namebox_open(NAMEBOX_PICKER_SAVE, "Save File", "New File.txt", g_picker_path, 0, 0);
            }
            return 1;
        }

        if (g_picker_selected >= 0 && g_picker_selected < g_picker_count) {
            char full[256];
            picker_path_join(g_picker_path, g_picker_ents[g_picker_selected].name, full, sizeof(full));
            if (g_picker_ents[g_picker_selected].is_dir && !(g_picker_flags & WINDOW_PICKER_ALLOW_DIRS)) {
                picker_enter_entry(g_picker_selected);
                return 1;
            }
            picker_finish(1u, full);
        } else {
            picker_finish(0u, "");
        }
        return 1;
    }
    if (in_rect(mx, my, cancel_x, btn_y, btn_w, btn_h)) {
        picker_finish(0u, "");
        return 1;
    }
    if (in_rect(mx, my, list_x, list_y, list_w, list_h)) {
        int idx = picker_entry_at(mx, my, list_x, list_y, list_w, list_h);
        if (idx >= 0) {
            uint64_t now = sys_get_ticks();
            if (g_picker_last_click_index == idx && now - g_picker_last_click_tick <= DESK_DBLCLICK_TICKS) {
                if (g_picker_ents[idx].is_dir) {
                    picker_enter_entry(idx);
                } else {
                    char full[256];
                    picker_path_join(g_picker_path, g_picker_ents[idx].name, full, sizeof(full));
                    picker_finish(1u, full);
                }
                g_picker_last_click_index = -1;
                g_picker_last_click_tick = 0;
            } else {
                g_picker_selected = idx;
                g_picker_last_click_index = idx;
                g_picker_last_click_tick = now;
            }
        }
        return 1;
    }
    return 1;
}

static int msgbox_handle_mouse(int mx, int my, int left_edge) {
    if (!g_msgbox_active) return 0;
    int pw = 420;
    int ph = 180;
    int px = (int)g_fb.width / 2 - pw / 2;
    int py = (int)g_fb.height / 2 - ph / 2;
    int btn_w = 90;
    int btn_h = 20;
    int btn_y = py + ph - 36;
    if (!left_edge) return 1;

    if (g_msgbox_flags == WINDOW_MSGBOX_YES_NO) {
        int yes_x = px + pw - 2 * btn_w - 24;
        int no_x = px + pw - btn_w - 12;
        if (in_rect(mx, my, yes_x, btn_y, btn_w, btn_h)) {
            if (g_msgbox_owner_tid >= 0) (void)sys_dialog_push(g_msgbox_owner_tid, 3u, "");
            g_msgbox_active = 0;
            return 1;
        }
        if (in_rect(mx, my, no_x, btn_y, btn_w, btn_h)) {
            if (g_msgbox_owner_tid >= 0) (void)sys_dialog_push(g_msgbox_owner_tid, 4u, "");
            g_msgbox_active = 0;
            return 1;
        }
    } else if (g_msgbox_flags == WINDOW_MSGBOX_OK_CANCEL) {
        int ok_x = px + pw - 2 * btn_w - 24;
        int cancel_x = px + pw - btn_w - 12;
        if (in_rect(mx, my, ok_x, btn_y, btn_w, btn_h)) {
            if (g_msgbox_owner_tid >= 0) (void)sys_dialog_push(g_msgbox_owner_tid, 1u, "");
            g_msgbox_active = 0;
            return 1;
        }
        if (in_rect(mx, my, cancel_x, btn_y, btn_w, btn_h)) {
            if (g_msgbox_owner_tid >= 0) (void)sys_dialog_push(g_msgbox_owner_tid, 2u, "");
            g_msgbox_active = 0;
            return 1;
        }
    } else {
        int ok_x = px + pw - btn_w - 12;
        if (in_rect(mx, my, ok_x, btn_y, btn_w, btn_h)) {
            if (g_msgbox_owner_tid >= 0) (void)sys_dialog_push(g_msgbox_owner_tid, 1u, "");
            g_msgbox_active = 0;
            return 1;
        }
    }
    return 1;
}

static int browser_refresh(desk_browser_state_t* st) {
    if (!st) return -1;
    ntux_dirent_t ents[DESK_LS_MAX];
    uint64_t count = 0;
    st->count = 0;
    st->selected = -1;
    st->scroll = 0;
    st->scroll_dragging = 0;
    browser_clear_marks(st);
    browser_preview_clear(st);
    browser_text_preview_clear(st);
    browser_thumbs_clear(st);
    if (sys_fs_list_dir(st->path, ents, DESK_LS_MAX, &count) != 0) {
        strncpy(st->status, "Directory read failed", sizeof(st->status) - 1);
        st->status[sizeof(st->status) - 1] = '\0';
        return -1;
    }
    if (count > DESK_LS_MAX) count = DESK_LS_MAX;
    for (uint64_t i = 0; i < count && st->count < DESK_LS_MAX; ++i) {
        if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
        strncpy(st->ents[st->count].name, ents[i].name, sizeof(st->ents[st->count].name) - 1);
        st->ents[st->count].name[sizeof(st->ents[st->count].name) - 1] = '\0';
        st->ents[st->count].is_dir = ents[i].is_dir ? 1u : 0u;
        st->ents[st->count].size = ents[i].size;
        st->count++;
    }
    st->status[0] = '\0';
    (void)str_append_u64(st->status, sizeof(st->status), (uint64_t)st->count);
    (void)str_append(st->status, sizeof(st->status), " entries");
    return 0;
}

static void browser_go_up(desk_browser_state_t* st) {
    if (!st) return;
    size_t len = strlen(st->path);
    if (len <= 1) return;
    while (len > 1 && st->path[len - 1] == '/') len--;
    while (len > 1 && st->path[len - 1] != '/') len--;
    if (len == 0) len = 1;
    st->path[len] = '\0';
    strncpy(g_browser_default_path, st->path, sizeof(g_browser_default_path) - 1);
    g_browser_default_path[sizeof(g_browser_default_path) - 1] = '\0';
    browser_refresh(st);
}

static void browser_enter_entry(desk_browser_state_t* st, int idx) {
    if (!st) return;
    char full[256];
    if (idx < 0 || idx >= st->count) return;
    browser_path_join(st->path, st->ents[idx].name, full, sizeof(full));
    if (!full[0]) return;
    browser_preview_clear(st);
    browser_text_preview_clear(st);
    if (st->ents[idx].is_dir) {
        strncpy(st->path, full, sizeof(st->path) - 1);
        st->path[sizeof(st->path) - 1] = '\0';
        strncpy(g_browser_default_path, st->path, sizeof(g_browser_default_path) - 1);
        g_browser_default_path[sizeof(g_browser_default_path) - 1] = '\0';
        browser_refresh(st);
        return;
    }
    if (path_is_image_ext(st->ents[idx].name)) {
        (void)desktop_launch_image_viewer(full);
        return;
    }
    if (path_is_text_ext(st->ents[idx].name)) {
        (void)desktop_launch_editor_with_path(full);
        return;
    }
    if (path_is_obj_ext(st->ents[idx].name)) {
        (void)desktop_launch_objview_with_path(full);
        return;
    }
    if (name_is_elf(st->ents[idx].name)) {
        int wants_console = text_contains_ci(st->ents[idx].name, "console") ||
            text_contains_ci(st->ents[idx].name, "konsole") ||
            text_contains_ci(st->ents[idx].name, "terminal") ||
            text_contains_ci(st->ents[idx].name, "shell") ||
            text_contains_ci(st->ents[idx].name, "cli") ||
            text_contains_ci(st->ents[idx].name, "healthcheck");
        int term_idx = -1;
        if (wants_console) {
            open_console_window();
            term_idx = g_focus_index;
        }
        long tid = desktop_launch_target_tid(full);
        if (tid > 0 && term_idx >= 0 && term_idx < g_window_count && g_windows[term_idx].terminal) {
            term_route_register((int)tid, term_idx);
        }
    }
}

static int browser_button_at(const desk_window_t* w, int x, int y) {
    int bx = w->x + 8;
    int by = w->y + DESK_TITLEBAR_H + 4;
    if (!in_rect(x, y, w->x + 6, by - 1, w->w - 12, DESK_BROWSER_BTN_H + 2)) return BROWSER_BTN_NONE;
    if (in_rect(x, y, bx, by, 36, DESK_BROWSER_BTN_H)) return BROWSER_BTN_UP;
    bx += 40;
    if (in_rect(x, y, bx, by, 64, DESK_BROWSER_BTN_H)) return BROWSER_BTN_REFRESH;
    bx += 68;
    if (in_rect(x, y, bx, by, 64, DESK_BROWSER_BTN_H)) return BROWSER_BTN_NEW_DIR;
    bx += 68;
    if (in_rect(x, y, bx, by, 72, DESK_BROWSER_BTN_H)) return BROWSER_BTN_NEW_FILE;
    bx += 76;
    if (in_rect(x, y, bx, by, 56, DESK_BROWSER_BTN_H)) return BROWSER_BTN_DELETE;
    bx += 60;
    if (in_rect(x, y, bx, by, 68, DESK_BROWSER_BTN_H)) return BROWSER_BTN_RENAME;
    bx += 72;
    if (in_rect(x, y, bx, by, 54, DESK_BROWSER_BTN_H)) return BROWSER_BTN_COPY;
    bx += 58;
    if (in_rect(x, y, bx, by, 60, DESK_BROWSER_BTN_H)) return BROWSER_BTN_PASTE;
    return BROWSER_BTN_NONE;
}

static int browser_menu_item_at(const desk_browser_state_t* st, int x, int y) {
    if (!st || !st->ctx_open) return -1;
    const int w = 240;
    const int item_h = 26;
    const int pad = 8;
    const int count = 7;
    if (!in_rect(x, y, st->ctx_x, st->ctx_y, w, pad * 2 + item_h * count)) return -1;
    int rel = y - (st->ctx_y + pad);
    if (rel < 0) return -1;
    int idx = rel / item_h;
    if (idx < 0 || idx >= count) return -1;
    return idx;
}

static int browser_menu_sub_item_at(const desk_browser_state_t* st, int x, int y) {
    if (!st || !st->ctx_sub_open) return -1;
    const int w = 190;
    const int item_h = 24;
    const int pad = 6;
    const int count = 2;
    int sx = st->ctx_x + 240 - 2;
    int sy = st->ctx_y + pad;
    if (!in_rect(x, y, sx, sy, w, pad * 2 + item_h * count)) return -1;
    int rel = y - (sy + pad);
    if (rel < 0) return -1;
    int idx = rel / item_h;
    if (idx < 0 || idx >= count) return -1;
    return idx;
}

static void browser_menu_open_at(desk_browser_state_t* st, int x, int y) {
    if (!st) return;
    const int w = 240;
    const int item_h = 26;
    const int pad = 8;
    const int count = 7;
    int h = pad * 2 + item_h * count;
    st->ctx_x = x;
    st->ctx_y = y;
    if (st->ctx_x + w > (int)g_fb.width) st->ctx_x = (int)g_fb.width - w - 4;
    if (st->ctx_y + h > (int)g_fb.height - DESK_TASKBAR_H) st->ctx_y = (int)g_fb.height - DESK_TASKBAR_H - h - 4;
    if (st->ctx_x < 4) st->ctx_x = 4;
    if (st->ctx_y < 4) st->ctx_y = 4;
    desktop_menu_close();
    browser_menu_close_all();
    st->ctx_open = 1;
    st->ctx_hover = -1;
    st->ctx_sub_open = 0;
    st->ctx_sub_hover = -1;
    g_desktop_dirty = 1;
}

static void browser_menu_close(desk_browser_state_t* st) {
    if (!st || !st->ctx_open) return;
    st->ctx_open = 0;
    st->ctx_hover = -1;
    st->ctx_sub_open = 0;
    st->ctx_sub_hover = -1;
    g_desktop_dirty = 1;
}

static void browser_menu_close_all(void) {
    for (int i = 0; i < DESK_MAX_WINDOWS; ++i) {
        desk_browser_state_t* st = g_browser_states[i];
        if (st && st->ctx_open) browser_menu_close(st);
    }
}

static void draw_browser_menu(const desk_window_t* w, const desk_browser_state_t* st) {
    (void)w;
    if (!st || !st->ctx_open) return;
    const desk_theme_t* th = desk_theme();
    const int menu_w = 240;
    const int item_h = 26;
    const int pad = 8;
    const char* items[] = {"New", "Open", "Rename", "Copy", "Paste", "Refresh", "Delete"};
    int count = (int)(sizeof(items) / sizeof(items[0]));
    int h = pad * 2 + item_h * count;
    uint32_t bg = color_lerp(th->taskbar_bg, th->wall_b, 40u);
    fill_round_rect(st->ctx_x, st->ctx_y, menu_w, h, 8, bg);
    draw_round_rect(st->ctx_x, st->ctx_y, menu_w, h, 8, th->taskbar_border);
    for (int i = 0; i < count; ++i) {
        int y = st->ctx_y + pad + i * item_h;
        int ix = st->ctx_x + 12;
        int iy = y + 6;
        uint32_t ic = color_lerp(th->accent, th->wall_c, 80u);
        if (i == st->ctx_hover) {
            fill_round_rect(st->ctx_x + 6, y, menu_w - 12, item_h - 2, 6, th->accent);
            draw_text(st->ctx_x + 16, y + 8, items[i], 0xFF091018u);
            ic = 0xFF091018u;
            if (i == 0) draw_text(st->ctx_x + menu_w - 18, y + 8, ">", 0xFF091018u);
        } else {
            draw_text(st->ctx_x + 16, y + 8, items[i], th->text_main);
            if (i == 0) draw_text(st->ctx_x + menu_w - 18, y + 8, ">", th->text_dim);
        }
        if (i == 0) {
            fill_round_rect(ix, iy, 10, 10, 2, ic);
        } else if (i == 1) {
            draw_rect(ix, iy, 10, 10, ic);
            draw_line(ix + 2, iy + 4, ix + 8, iy + 4, ic);
            draw_line(ix + 2, iy + 7, ix + 8, iy + 7, ic);
        } else if (i == 2) {
            draw_line(ix + 2, iy + 2, ix + 8, iy + 2, ic);
            draw_line(ix + 2, iy + 2, ix + 2, iy + 8, ic);
            draw_line(ix + 2, iy + 8, ix + 8, iy + 8, ic);
            draw_line(ix + 8, iy + 2, ix + 8, iy + 8, ic);
        } else if (i == 3) {
            draw_rect(ix + 1, iy + 2, 8, 6, ic);
            draw_rect(ix + 3, iy + 4, 8, 6, ic);
        } else if (i == 4) {
            draw_rect(ix + 2, iy + 2, 8, 8, ic);
            draw_line(ix + 4, iy + 4, ix + 8, iy + 8, ic);
        } else if (i == 5) {
            draw_line(ix, iy + 5, ix + 10, iy + 5, ic);
            draw_line(ix + 8, iy + 3, ix + 10, iy + 5, ic);
            draw_line(ix + 8, iy + 7, ix + 10, iy + 5, ic);
        } else if (i == 6) {
            draw_line(ix + 2, iy + 2, ix + 8, iy + 8, ic);
            draw_line(ix + 2, iy + 8, ix + 8, iy + 2, ic);
        }
    }

    if (st->ctx_sub_open) {
        const int sw = 190;
        const int sh = pad * 2 + 24 * 2;
        int sx = st->ctx_x + menu_w - 2;
        int sy = st->ctx_y + pad;
        uint32_t sbg = color_lerp(th->taskbar_bg, th->wall_b, 55u);
        fill_round_rect(sx, sy, sw, sh, 8, sbg);
        draw_round_rect(sx, sy, sw, sh, 8, th->taskbar_border);
        for (int i = 0; i < 2; ++i) {
            int y = sy + pad + i * 24;
            const char* label = (i == 0) ? "Folder" : "Empty File";
            if (i == st->ctx_sub_hover) {
                fill_round_rect(sx + 6, y, sw - 12, 22, 6, th->accent);
                draw_text(sx + 16, y + 6, label, 0xFF091018u);
            } else {
                draw_text(sx + 16, y + 6, label, th->text_main);
            }
        }
    }
}

static void browser_list_rect(const desk_window_t* w, const desk_browser_state_t* st, int* out_x, int* out_y, int* out_w, int* out_h) {
    int sidebar_w = 140;
    int list_x = w->x + 8 + sidebar_w + 8;
    int list_y = w->y + DESK_TITLEBAR_H + 56;
    int list_w = w->w - 16 - sidebar_w - 8;
    int list_h = w->h - DESK_TITLEBAR_H - 64;
    if (st && (st->preview_ready || st->text_preview_ready)) {
        int pw = st->preview_ready ? (st->preview_w + 16) : 240;
        if (list_w - pw > 120) {
            list_w -= pw;
        }
    }
    if (list_w < 60) list_w = 60;
    if (list_h < 40) list_h = 40;
    if (out_x) *out_x = list_x;
    if (out_y) *out_y = list_y;
    if (out_w) *out_w = list_w;
    if (out_h) *out_h = list_h;
}

static void browser_calc_grid(const desk_window_t* w, const desk_browser_state_t* st, int* out_cols, int* out_rows, int* out_total_rows, int* out_content_w) {
    int list_x, list_y, list_w, list_h;
    int cell_w = 116;
    int cell_h = 102;
    browser_list_rect(w, st, &list_x, &list_y, &list_w, &list_h);
    (void)list_x;
    (void)list_y;
    int cols = list_w / cell_w;
    if (cols < 1) cols = 1;
    int rows = list_h / cell_h;
    if (rows < 1) rows = 1;
    int total_rows = 0;
    if (st && st->count > 0) {
        total_rows = (st->count + cols - 1) / cols;
    }
    int content_w = list_w;
    if (total_rows > rows && list_w > 24) {
        content_w = list_w - 12;
        cols = content_w / cell_w;
        if (cols < 1) cols = 1;
        total_rows = (st && st->count > 0) ? (st->count + cols - 1) / cols : 0;
    }
    if (out_cols) *out_cols = cols;
    if (out_rows) *out_rows = rows;
    if (out_total_rows) *out_total_rows = total_rows;
    if (out_content_w) *out_content_w = content_w;
}

static int browser_scrollbar_geom(const desk_window_t* w, const desk_browser_state_t* st,
                                  int* out_track_x, int* out_track_y, int* out_track_w, int* out_track_h,
                                  int* out_thumb_y, int* out_thumb_h, int* out_cols, int* out_rows, int* out_total_rows,
                                  int* out_content_w) {
    int list_x, list_y, list_w, list_h;
    int cols = 1;
    int rows = 1;
    int total_rows = 0;
    int content_w = 0;
    if (!w || !st) return 0;
    browser_list_rect(w, st, &list_x, &list_y, &list_w, &list_h);
    browser_calc_grid(w, st, &cols, &rows, &total_rows, &content_w);
    if (total_rows <= rows) return 0;

    int scroll_gap = list_w - content_w;
    int track_w = 6;
    int track_x = list_x + content_w + (scroll_gap - track_w) / 2;
    int track_y = list_y + 6;
    int track_h = list_h - 12;
    if (track_h < 16) track_h = 16;
    int max_top = total_rows - rows;
    if (max_top < 1) max_top = 1;
    int thumb_h = (track_h * rows) / total_rows;
    if (thumb_h < 18) thumb_h = 18;
    if (thumb_h > track_h) thumb_h = track_h;
    int top_row = (cols > 0) ? (st->scroll / cols) : 0;
    int thumb_y = track_y + ((track_h - thumb_h) * top_row) / max_top;

    if (out_track_x) *out_track_x = track_x;
    if (out_track_y) *out_track_y = track_y;
    if (out_track_w) *out_track_w = track_w;
    if (out_track_h) *out_track_h = track_h;
    if (out_thumb_y) *out_thumb_y = thumb_y;
    if (out_thumb_h) *out_thumb_h = thumb_h;
    if (out_cols) *out_cols = cols;
    if (out_rows) *out_rows = rows;
    if (out_total_rows) *out_total_rows = total_rows;
    if (out_content_w) *out_content_w = content_w;
    return 1;
}

static int browser_scroll_max(const desk_window_t* w, const desk_browser_state_t* st) {
    int cols = 1;
    int rows = 1;
    int total_rows = 0;
    browser_calc_grid(w, st, &cols, &rows, &total_rows, 0);
    if (total_rows <= rows) return 0;
    return (total_rows - rows) * cols;
}

static int browser_entry_at(const desk_window_t* w, desk_browser_state_t* st, int x, int y) {
    int list_x, list_y, list_w, list_h;
    browser_list_rect(w, st, &list_x, &list_y, &list_w, &list_h);
    if (!in_rect(x, y, list_x, list_y, list_w, list_h)) return -1;
    int cell_w = 116;
    int cell_h = 102;
    int cols = 1;
    int content_w = list_w;
    browser_calc_grid(w, st, &cols, 0, 0, &content_w);
    if (x >= list_x + content_w) return -1;
    int col = (x - list_x) / cell_w;
    int row = (y - list_y) / cell_h;
    int max_start = browser_scroll_max(w, st);
    if (st && st->scroll > max_start) st->scroll = max_start;
    int idx = (st ? st->scroll : 0) + row * cols + col;
    if (!st || idx < 0 || idx >= st->count) return -1;
    return idx;
}

static void browser_make_name(desk_browser_state_t* st, char* out, size_t cap, const char* base, const char* ext) {
    int n = 0;
    if (!out || cap < 4) return;
    for (;;) {
        int used = 0;
        out[0] = '\0';
        (void)str_append(out, cap, base ? base : "");
        if (n != 0) {
            (void)str_append_char(out, cap, ' ');
            (void)str_append_i32(out, cap, n);
        }
        (void)str_append(out, cap, ext ? ext : "");
        for (int i = 0; st && i < st->count; ++i) {
            if (strcmp(st->ents[i].name, out) == 0) {
                used = 1;
                break;
            }
        }
        if (!used) return;
        n++;
        if (n > 9999) {
            out[0] = '\0';
            (void)str_append(out, cap, base ? base : "");
            (void)str_append_char(out, cap, '_');
            (void)str_append_u64(out, cap, sys_get_ticks());
            return;
        }
    }
}

static int browser_handle_click(const desk_window_t* w, desk_browser_state_t* st, int x, int y) {
    if (!st) return 0;
    st->scroll_dragging = 0;
    if (st->ctx_open) {
        int idx = browser_menu_item_at(st, x, y);
        int sidx = browser_menu_sub_item_at(st, x, y);
        if (idx == 0) {
            if (sidx >= 0) {
                if (sidx == 0) {
                    namebox_open(NAMEBOX_BROWSER_NEW_FOLDER, "New Folder", "New Folder", st->path, 0, st->id);
                } else if (sidx == 1) {
                    namebox_open(NAMEBOX_BROWSER_NEW_FILE, "New File", "New File.txt", st->path, 0, st->id);
                }
                browser_menu_close(st);
                return 1;
            }
            st->ctx_sub_open = 1;
            return 1;
        }
        if (idx == 1) {
            if (st->selected >= 0 && st->selected < st->count) {
                browser_enter_entry(st, st->selected);
            }
            browser_menu_close(st);
            return 1;
        }
        if (idx == 2) {
            if (st->selected >= 0 && st->selected < st->count) {
                char full[256];
                browser_path_join(st->path, st->ents[st->selected].name, full, sizeof(full));
                namebox_open(NAMEBOX_BROWSER_RENAME, "Rename", st->ents[st->selected].name, st->path, full, st->id);
            }
            browser_menu_close(st);
            return 1;
        }
        if (idx == 3) {
            browser_clipboard_set(st, 0);
            browser_menu_close(st);
            return 1;
        }
        if (idx == 4) {
            if (browser_clipboard_paste(st) == 0) browser_refresh(st);
            else {
                strncpy(st->status, "Paste failed", sizeof(st->status) - 1);
                st->status[sizeof(st->status) - 1] = '\0';
            }
            browser_menu_close(st);
            return 1;
        }
        if (idx == 5) {
            browser_refresh(st);
            browser_menu_close(st);
            return 1;
        }
        if (idx == 6) {
            char full[256];
            if (st->selected >= 0 && st->selected < st->count) {
                browser_path_join(st->path, st->ents[st->selected].name, full, sizeof(full));
                if (full[0] && sys_fs_remove(full) == 0) browser_refresh(st);
                else {
                    strncpy(st->status, "Delete failed (dir not empty?)", sizeof(st->status) - 1);
                    st->status[sizeof(st->status) - 1] = '\0';
                }
            }
            browser_menu_close(st);
            return 1;
        }
        if (idx >= 0) {
            browser_menu_close(st);
            return 1;
        }
        browser_menu_close(st);
    }

    int btn = browser_button_at(w, x, y);
    if (btn == BROWSER_BTN_UP) {
        browser_go_up(st);
        return 1;
    }
    if (btn == BROWSER_BTN_REFRESH) {
        browser_refresh(st);
        return 1;
    }
    if (btn == BROWSER_BTN_NEW_DIR) {
        namebox_open(NAMEBOX_BROWSER_NEW_FOLDER, "New Folder", "New Folder", st->path, 0, st->id);
        return 1;
    }
    if (btn == BROWSER_BTN_NEW_FILE) {
        namebox_open(NAMEBOX_BROWSER_NEW_FILE, "New File", "New File.txt", st->path, 0, st->id);
        return 1;
    }
    if (btn == BROWSER_BTN_DELETE) {
        char full[256];
        if (st->selected >= 0 && st->selected < st->count) {
            browser_path_join(st->path, st->ents[st->selected].name, full, sizeof(full));
            if (full[0] && sys_fs_remove(full) == 0) browser_refresh(st);
            else {
                strncpy(st->status, "Delete failed (dir not empty?)", sizeof(st->status) - 1);
                st->status[sizeof(st->status) - 1] = '\0';
            }
        }
        return 1;
    }
    if (btn == BROWSER_BTN_RENAME) {
        if (st->selected >= 0 && st->selected < st->count) {
            char full[256];
            browser_path_join(st->path, st->ents[st->selected].name, full, sizeof(full));
            namebox_open(NAMEBOX_BROWSER_RENAME, "Rename", st->ents[st->selected].name, st->path, full, st->id);
        }
        return 1;
    }
    if (btn == BROWSER_BTN_COPY) {
        browser_clipboard_set(st, 0);
        return 1;
    }
    if (btn == BROWSER_BTN_PASTE) {
        if (browser_clipboard_paste(st) == 0) browser_refresh(st);
        else {
            strncpy(st->status, "Paste failed", sizeof(st->status) - 1);
            st->status[sizeof(st->status) - 1] = '\0';
        }
        return 1;
    }

    /* Sidebar quick links */
    {
        int sidebar_x = w->x + 8;
        int sidebar_y = w->y + DESK_TITLEBAR_H + 56;
        int sidebar_w = 140;
        int item_h = 20;
        if (in_rect(x, y, sidebar_x, sidebar_y, sidebar_w, 7 * item_h + 8)) {
            int idx = (y - sidebar_y - 6) / item_h;
            char path[256];
            path[0] = '\0';
            if (idx == 0) strncpy(path, "/", sizeof(path) - 1);
            else if (idx == 1) snprintf(path, sizeof(path), "%s/%s/desktop", g_user_store_root, g_current_user);
            else if (idx == 2) snprintf(path, sizeof(path), "%s/%s/Downloads", g_user_store_root, g_current_user);
            else if (idx == 3) snprintf(path, sizeof(path), "%s/%s/Documents", g_user_store_root, g_current_user);
            else if (idx == 4) snprintf(path, sizeof(path), "%s/%s/Music", g_user_store_root, g_current_user);
            else if (idx == 5) snprintf(path, sizeof(path), "%s/%s/Pictures", g_user_store_root, g_current_user);
            else if (idx == 6) snprintf(path, sizeof(path), "%s/%s/Videos", g_user_store_root, g_current_user);
            path[sizeof(path) - 1] = '\0';
            if (path[0] && sys_fs_list_dir(path, 0, 0, 0) == 0) {
                strncpy(st->path, path, sizeof(st->path) - 1);
                st->path[sizeof(st->path) - 1] = '\0';
                strncpy(g_browser_default_path, st->path, sizeof(g_browser_default_path) - 1);
                g_browser_default_path[sizeof(g_browser_default_path) - 1] = '\0';
                browser_refresh(st);
                return 1;
            }
        }
    }

    {
        int track_x, track_y, track_w, track_h, thumb_y, thumb_h;
        int cols = 1;
        int rows = 1;
        int total_rows = 0;
        if (browser_scrollbar_geom(w, st, &track_x, &track_y, &track_w, &track_h, &thumb_y, &thumb_h,
                                   &cols, &rows, &total_rows, 0)) {
            if (in_rect(x, y, track_x, track_y, track_w, track_h)) {
                int max_top = total_rows - rows;
                if (max_top < 1) max_top = 1;
                if (y >= thumb_y && y <= thumb_y + thumb_h) {
                    st->scroll_dragging = 1;
                    st->scroll_drag_offset = y - thumb_y;
                } else {
                    st->scroll_dragging = 1;
                    st->scroll_drag_offset = thumb_h / 2;
                    int rel = y - track_y - st->scroll_drag_offset;
                    if (rel < 0) rel = 0;
                    if (rel > track_h - thumb_h) rel = track_h - thumb_h;
                    int top_row = (track_h - thumb_h) > 0 ? (rel * max_top) / (track_h - thumb_h) : 0;
                    st->scroll = top_row * cols;
                }
                return 1;
            }
        }
    }

    int idx = browser_entry_at(w, st, x, y);
    if (idx >= 0) {
        uint64_t now = sys_get_ticks();
        if (st->last_click_index == idx && now - st->last_click_tick <= DESK_DBLCLICK_TICKS) {
            browser_enter_entry(st, idx);
            st->last_click_index = -1;
            st->last_click_tick = 0;
        } else {
            int ctrl = (sys_kbd_is_pressed(0x1D) > 0) ? 1 : 0;
            if (ctrl) {
                st->mark[idx] = st->mark[idx] ? 0u : 1u;
                st->mark_count = 0;
                for (int i = 0; i < st->count && i < DESK_LS_MAX; ++i) {
                    if (st->mark[i]) st->mark_count++;
                }
            } else {
                browser_mark_only(st, idx);
            }
            st->selected = idx;
            st->last_click_index = idx;
            st->last_click_tick = now;
            if (!st->ents[idx].is_dir && path_is_image_ext(st->ents[idx].name)) {
                char full[256];
                browser_path_join(st->path, st->ents[idx].name, full, sizeof(full));
                if (full[0]) (void)browser_preview_load(st, full);
                browser_text_preview_clear(st);
            } else if (!st->ents[idx].is_dir && path_is_text_ext(st->ents[idx].name)) {
                char full[256];
                browser_path_join(st->path, st->ents[idx].name, full, sizeof(full));
                browser_preview_clear(st);
                if (full[0]) (void)browser_text_preview_load(st, full);
            } else {
                browser_preview_clear(st);
                browser_text_preview_clear(st);
            }
        }
        return 1;
    }

    {
        int list_x, list_y, list_w, list_h;
        browser_list_rect(w, st, &list_x, &list_y, &list_w, &list_h);
        if (in_rect(x, y, list_x, list_y, list_w, list_h)) {
            return 1;
        }
    }
    return 0;
}

static void browser_draw_folder_icon(int x, int y, int w, int h, uint32_t c, uint32_t border) {
    fill_round_rect(x, y + h / 4, w, h - h / 4, 6, c);
    draw_round_rect(x, y + h / 4, w, h - h / 4, 6, border);
    fill_round_rect(x + 4, y, w / 2, h / 3, 4, color_lerp(c, 0xFFFFFFFFu, 35u));
}

static void browser_draw_file_icon(int x, int y, int w, int h, uint32_t c, uint32_t border) {
    fill_round_rect(x, y, w, h, 6, c);
    draw_round_rect(x, y, w, h, 6, border);
    uint32_t top = color_lerp(c, 0xFFFFFFFFu, 22u);
    fill_round_rect(x + 3, y + 3, w - 6, 12, 4, top);
    fill_rect(x + 6, y + 20, w - 12, 2, border);
    fill_rect(x + 6, y + 26, w - 18, 2, border);
    fill_rect(x + 6, y + 32, w - 22, 2, border);
    if (w > 16 && h > 16) {
        int fx = x + w - 14;
        int fy = y + 2;
        fill_rect(fx, fy, 10, 10, top);
        draw_line(fx, fy, fx + 9, fy + 9, border);
        draw_line(fx + 9, fy, fx + 9, fy + 9, border);
        draw_line(fx, fy + 9, fx + 9, fy + 9, border);
    }
}

static void browser_draw_music_icon(int x, int y, int w, int h, uint32_t c, uint32_t border) {
    fill_round_rect(x, y, w, h, 6, c);
    draw_round_rect(x, y, w, h, 6, border);
    fill_rect(x + 10, y + 8, 3, h - 14, border);
    fill_rect(x + 12, y + 8, 12, 3, border);
    fill_rect(x + 22, y + 8, 3, 14, border);
    fill_rect(x + 12, y + 22, 12, 3, border);
    fill_rect(x + 8, y + h - 12, 10, 6, border);
}

static void browser_draw_video_icon(int x, int y, int w, int h, uint32_t c, uint32_t border) {
    fill_round_rect(x, y, w, h, 6, c);
    draw_round_rect(x, y, w, h, 6, border);
    fill_rect(x + 8, y + 8, w - 16, h - 16, border);
    fill_rect(x + 14, y + 12, 6, 6, c);
    fill_rect(x + 24, y + 12, 6, 6, c);
}

static void browser_draw_download_icon(int x, int y, int w, int h, uint32_t c, uint32_t border) {
    fill_round_rect(x, y, w, h, 6, c);
    draw_round_rect(x, y, w, h, 6, border);
    fill_rect(x + w / 2 - 2, y + 8, 4, h - 20, border);
    fill_rect(x + w / 2 - 8, y + h - 18, 16, 4, border);
    fill_rect(x + w / 2 - 8, y + h - 12, 16, 4, border);
}

static void browser_draw_image_icon(int x, int y, int w, int h, uint32_t c, uint32_t border) {
    fill_round_rect(x, y, w, h, 6, c);
    draw_round_rect(x, y, w, h, 6, border);
    int ix = x + 4;
    int iy = y + 4;
    int iw = w - 8;
    int ih = h - 8;
    if (iw < 6 || ih < 6) return;
    uint32_t sky = color_lerp(c, 0xFFFFFFFFu, 40u);
    fill_round_rect(ix, iy, iw, ih, 4, sky);
    int sun = iw / 5;
    if (sun < 4) sun = 4;
    fill_rect(ix + iw - sun - 3, iy + 3, sun, sun, 0xFFFFE8A3u);
    int base_y = iy + ih - 5;
    draw_line(ix + 3, base_y, ix + iw / 2, iy + ih / 2, border);
    draw_line(ix + iw / 2, iy + ih / 2, ix + iw - 4, base_y, border);
    draw_line(ix + iw / 3, base_y, ix + iw * 2 / 3, base_y - ih / 4, border);
}

static void draw_file_browser(const desk_window_t* w, desk_browser_state_t* st) {
    if (!st) return;
    const desk_theme_t* th = desk_theme();
    int bx = w->x + 8;
    int by = w->y + DESK_TITLEBAR_H + 4;
    int list_x, list_y, list_w, list_h;
    browser_list_rect(w, st, &list_x, &list_y, &list_w, &list_h);
    int cell_w = 116;
    int cell_h = 102;
    int cols = 1;
    int rows = 1;
    int total_rows = 0;
    int content_w = list_w;
    browser_calc_grid(w, st, &cols, &rows, &total_rows, &content_w);
    int max_start = browser_scroll_max(w, st);
    if (st->scroll < 0) st->scroll = 0;
    if (st->scroll > max_start) st->scroll = max_start;

    uint32_t bar_bg = color_lerp(th->window_fill, 0xFF000000u, 28u);
    uint32_t btn_bg = color_lerp(th->window_fill, 0xFF243447u, 40u);
    fill_round_rect(w->x + 6, by - 1, w->w - 12, DESK_BROWSER_BTN_H + 4, 6, bar_bg);
    fill_round_rect(bx, by, 36, DESK_BROWSER_BTN_H, 6, btn_bg);
    draw_text(bx + 8, by + 4, "Up", th->text_main);
    bx += 40;
    fill_round_rect(bx, by, 64, DESK_BROWSER_BTN_H, 6, btn_bg);
    draw_text(bx + 4, by + 4, "Refresh", th->text_main);
    bx += 68;
    fill_round_rect(bx, by, 64, DESK_BROWSER_BTN_H, 6, btn_bg);
    draw_text(bx + 6, by + 4, "NewDir", th->text_main);
    bx += 68;
    fill_round_rect(bx, by, 72, DESK_BROWSER_BTN_H, 6, btn_bg);
    draw_text(bx + 6, by + 4, "NewFile", th->text_main);
    bx += 76;
    fill_round_rect(bx, by, 56, DESK_BROWSER_BTN_H, 6, 0xFF6A3A48u);
    draw_text(bx + 8, by + 4, "Delete", 0xFFFFDFE8u);
    bx += 60;
    fill_round_rect(bx, by, 68, DESK_BROWSER_BTN_H, 6, btn_bg);
    draw_text(bx + 6, by + 4, "Rename", th->text_main);
    bx += 72;
    fill_round_rect(bx, by, 54, DESK_BROWSER_BTN_H, 6, btn_bg);
    draw_text(bx + 10, by + 4, "Copy", th->text_main);
    bx += 58;
    fill_round_rect(bx, by, 60, DESK_BROWSER_BTN_H, 6, btn_bg);
    draw_text(bx + 8, by + 4, "Paste", th->text_main);

    fill_round_rect(w->x + 6, w->y + DESK_TITLEBAR_H + 24, w->w - 12, 18, 6, color_lerp(th->title_blur, 0xFF000000u, 40u));
    char path_line[DESK_TERM_COLS + 1];
    path_line[0] = '\0';
    (void)str_append(path_line, sizeof(path_line), "Path: ");
    (void)str_append(path_line, sizeof(path_line), st->path);
    draw_text(w->x + 12, w->y + DESK_TITLEBAR_H + 28, path_line, 0xFFB7D6F2u);
    draw_text(w->x + w->w - 8 - (int)strlen(st->status) * 8, w->y + DESK_TITLEBAR_H + 26, st->status, 0xFFBFD0FFu);

    /* Sidebar */
    int sidebar_x = w->x + 8;
    int sidebar_y = w->y + DESK_TITLEBAR_H + 56;
    int sidebar_w = 140;
    int sidebar_h = list_h;
    fill_round_rect(sidebar_x, sidebar_y, sidebar_w, sidebar_h, 8, color_lerp(th->window_fill, 0xFF000000u, 22u));
    draw_round_rect(sidebar_x, sidebar_y, sidebar_w, sidebar_h, 8, th->taskbar_border);
    const char* items[7] = {"Root", "Desktop", "Downloads", "Documents", "Music", "Pictures", "Videos"};
    for (int i = 0; i < 7; ++i) {
        int iy = sidebar_y + 6 + i * 20;
        int active = (i == 0 && strcmp(st->path, "/") == 0);
        if (active) {
            fill_round_rect(sidebar_x + 6, iy - 2, sidebar_w - 12, 18, 6, th->accent);
        }
        uint32_t c = active ? 0xFF091018u : th->text_main;
        draw_text(sidebar_x + 20, iy, items[i], c);
        uint32_t ic = active ? 0xFF091018u : th->text_dim;
        fill_round_rect(sidebar_x + 8, iy + 2, 8, 8, 2, ic);
    }

    fill_round_rect(list_x, list_y, list_w, list_h, 10, color_lerp(th->window_fill, 0xFF000000u, 35u));
    draw_round_rect(list_x, list_y, list_w, list_h, 10, th->taskbar_border);

    int visible_start = st->scroll;
    if (visible_start < 0) visible_start = 0;
    if (visible_start > st->count) visible_start = st->count;

    int loaded_this_frame = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = visible_start + r * cols + c;
            if (idx >= st->count) break;
            int cx = list_x + c * cell_w + 8;
            int cy = list_y + r * cell_h + 8;
            int box_w = cell_w - 16;
            int box_h = cell_h - 16;
            if (st->mark[idx] || idx == st->selected) {
                fill_round_rect(cx - 4, cy - 4, box_w + 8, box_h + 8, 8, color_lerp(th->accent, 0xFF000000u, 35u));
            }

            uint32_t card = color_lerp(th->title_blur, 0xFF000000u, 45u);
            fill_round_rect(cx, cy, box_w, box_h, 8, card);
            draw_round_rect(cx, cy, box_w, box_h, 8, th->taskbar_border);

            int icon_x = cx + 10;
            int icon_y = cy + 10;
            int icon_w = box_w - 20;
            int icon_h = box_h - 36;
            if (st->ents[idx].is_dir) {
                uint32_t fc = 0xFF2E8FD3u;
                const char* nm = st->ents[idx].name;
                if (text_contains_ci(nm, "music")) fc = 0xFF2DB36Fu;
                else if (text_contains_ci(nm, "video")) fc = 0xFF2D7AB3u;
                else if (text_contains_ci(nm, "download")) fc = 0xFF8A6B2Du;
                else if (text_contains_ci(nm, "picture") || text_contains_ci(nm, "image")) fc = 0xFF8A2DB3u;
                browser_draw_folder_icon(icon_x, icon_y, icon_w, icon_h, fc, th->taskbar_border);
            } else if (path_is_image_ext(st->ents[idx].name)) {
                if (!st->thumbs[idx].ready && !st->thumbs[idx].failed && loaded_this_frame < 2) {
                    browser_thumb_try_load(st, idx, icon_w, icon_h);
                    loaded_this_frame++;
                }
                if (st->thumbs[idx].ready && st->thumbs[idx].pixels) {
                    int tw = st->thumbs[idx].w;
                    int thh = st->thumbs[idx].h;
                    int px = icon_x + (icon_w - tw) / 2;
                    int py = icon_y + (icon_h - thh) / 2;
                    for (int yy = 0; yy < thh; ++yy) {
                        const uint8_t* row = st->thumbs[idx].pixels + (size_t)yy * (size_t)tw * 3u;
                        for (int xx = 0; xx < tw; ++xx) {
                            const uint8_t* pxl = row + (size_t)xx * 3u;
                            uint32_t cc = ((uint32_t)pxl[0] << 16) | ((uint32_t)pxl[1] << 8) | (uint32_t)pxl[2];
                            put_px(px + xx, py + yy, cc);
                        }
                    }
                    draw_round_rect(px - 2, py - 2, tw + 4, thh + 4, 6, th->taskbar_border);
                } else {
                    browser_draw_image_icon(icon_x, icon_y, icon_w, icon_h, 0xFF2D7AB3u, th->taskbar_border);
                }
            } else if (text_contains_ci(st->ents[idx].name, ".elf")) {
                browser_draw_file_icon(icon_x, icon_y, icon_w, icon_h, 0xFF3A6A4Du, th->taskbar_border);
            } else {
                browser_draw_file_icon(icon_x, icon_y, icon_w, icon_h, 0xFF394861u, th->taskbar_border);
            }

            char label[32];
            strncpy(label, st->ents[idx].name, sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
            draw_text(cx + 6, cy + box_h - 16, label, th->text_main);
        }
    }

    {
        int track_x, track_y, track_w, track_h, thumb_y, thumb_h;
        if (browser_scrollbar_geom(w, st, &track_x, &track_y, &track_w, &track_h, &thumb_y, &thumb_h, 0, 0, 0, 0)) {
            uint32_t track_c = color_lerp(th->window_fill, 0xFF000000u, 50u);
            fill_round_rect(track_x, track_y, track_w, track_h, 3, track_c);
            fill_round_rect(track_x, thumb_y, track_w, thumb_h, 3, th->accent);
        }
    }

    if (st->preview_ready && st->preview_w > 0 && st->preview_h > 0) {
        int px = list_x + list_w + 8;
        int py = list_y;
        int pw = st->preview_w;
        int ph = st->preview_h;
        if (px + pw + 8 < w->x + w->w) {
            fill_round_rect(px - 2, py - 2, pw + 4, ph + 4, 8, color_lerp(th->window_fill, 0xFF000000u, 40u));
            draw_round_rect(px - 2, py - 2, pw + 4, ph + 4, 8, th->taskbar_border);
            for (int yy = 0; yy < ph; ++yy) {
                const uint8_t* row = st->preview + (size_t)yy * (size_t)pw * 3u;
                for (int xx = 0; xx < pw; ++xx) {
                    const uint8_t* pxl = row + (size_t)xx * 3u;
                    uint32_t c = ((uint32_t)pxl[0] << 16) | ((uint32_t)pxl[1] << 8) | (uint32_t)pxl[2];
                    put_px(px + xx, py + yy, c);
                }
            }
            draw_text(px, py + ph + 6, "Preview", 0xFFB7D6F2u);
        }
    } else if (st->text_preview_ready && st->text_preview_len > 0) {
        int px = list_x + list_w + 8;
        int py = list_y;
        int pw = 232;
        int ph = list_h;
        if (px + pw + 8 < w->x + w->w) {
            fill_round_rect(px - 2, py - 2, pw + 4, ph + 4, 8, color_lerp(th->window_fill, 0xFF000000u, 40u));
            draw_round_rect(px - 2, py - 2, pw + 4, ph + 4, 8, th->taskbar_border);
            draw_text(px + 6, py + 6, "Text Preview", 0xFFB7D6F2u);
            int max_cols = (pw - 12) / 8;
            if (max_cols < 8) max_cols = 8;
            int line_h = 10;
            int max_lines = (ph - 20) / line_h;
            if (max_lines < 1) max_lines = 1;
            int line = 0;
            int col = 0;
            char linebuf[64];
            int lb = 0;
            for (int i = 0; i < st->text_preview_len && line < max_lines; ++i) {
                char c = st->text_preview[i];
                if (c == '\n' || col >= max_cols) {
                    linebuf[lb] = '\0';
                    draw_text(px + 6, py + 18 + line * line_h, linebuf, th->text_main);
                    line++;
                    col = 0;
                    lb = 0;
                    if (c == '\n') continue;
                }
                if (line >= max_lines) break;
                if (lb + 1 < (int)sizeof(linebuf)) {
                    linebuf[lb++] = c;
                    col++;
                }
            }
            if (lb > 0 && line < max_lines) {
                linebuf[lb] = '\0';
                draw_text(px + 6, py + 18 + line * line_h, linebuf, th->text_main);
            }
        }
    }

    draw_browser_menu(w, st);
}

static int settings_handle_click(const desk_window_t* w, int x, int y) {
    (void)w;
    (void)x;
    (void)y;
    return 0;
}

static void draw_settings_window(const desk_window_t* w) {
    (void)w;
}

static void clock_dir60(int idx, int* dx, int* dy) {
    static const int d12x[12] = {0, 128, 222, 256, 222, 128, 0, -128, -222, -256, -222, -128};
    static const int d12y[12] = {-256, -222, -128, 0, 128, 222, 256, 222, 128, 0, -128, -222};
    int base = (idx / 5) % 12;
    int next = (base + 1) % 12;
    int t = idx % 5;
    if (!dx || !dy) return;
    *dx = (d12x[base] * (5 - t) + d12x[next] * t) / 5;
    *dy = (d12y[base] * (5 - t) + d12y[next] * t) / 5;
}

static void draw_analog_clock_window(const desk_window_t* w) {
    ntux_time_t t;
    int cx = w->x + w->w / 2;
    int cy = w->y + DESK_TITLEBAR_H + (w->h - DESK_TITLEBAR_H) / 2 + 6;
    int r = (w->w < (w->h - DESK_TITLEBAR_H) ? w->w : (w->h - DESK_TITLEBAR_H)) / 2 - 18;
    if (r < 30) r = 30;
    fill_rect(w->x + 4, w->y + DESK_TITLEBAR_H + 2, w->w - 8, w->h - DESK_TITLEBAR_H - 6, 0xFF0D1826u);
    for (int i = 0; i < 12; ++i) {
        int dx, dy;
        clock_dir60(i * 5, &dx, &dy);
        int ox = cx + (dx * r) / 256;
        int oy = cy + (dy * r) / 256;
        int ix = cx + (dx * (r - 10)) / 256;
        int iy = cy + (dy * (r - 10)) / 256;
        draw_line(ix, iy, ox, oy, 0xFF8CB9E6u);
    }
    draw_rect(cx - r - 1, cy - r - 1, 2 * r + 2, 2 * r + 2, 0xFF3E6D9Fu);
    draw_rect(cx - r, cy - r, 2 * r, 2 * r, 0xFF1F3E5Du);

    if (sys_get_time(&t) == 0) {
        int h_idx = ((int)(t.hour % 12u) * 5) + ((int)t.minute / 12);
        int m_idx = (int)t.minute % 60;
        int s_idx = (int)t.second % 60;
        int hdx, hdy, mdx, mdy, sdx, sdy;
        clock_dir60(h_idx, &hdx, &hdy);
        clock_dir60(m_idx, &mdx, &mdy);
        clock_dir60(s_idx, &sdx, &sdy);
        draw_line(cx, cy, cx + (hdx * (r - 24)) / 256, cy + (hdy * (r - 24)) / 256, 0xFFFFD166u);
        draw_line(cx, cy, cx + (mdx * (r - 14)) / 256, cy + (mdy * (r - 14)) / 256, 0xFF39FF88u);
        draw_line(cx, cy, cx + (sdx * (r - 8)) / 256, cy + (sdy * (r - 8)) / 256, 0xFFFF5C7Au);
    }
    fill_rect(cx - 2, cy - 2, 5, 5, 0xFFEAF4FFu);
}

void open_console_window(void) {
    int slot = 0;
    if (g_window_count >= DESK_MAX_WINDOWS) return;
    slot = g_window_count;
    desk_window_t* w = &g_windows[g_window_count++];
    const desk_theme_t* th = desk_theme();
    memset(w, 0, sizeof(*w));
    w->id = 0xC000u + (uint64_t)g_window_count + (sys_get_ticks() & 0x0FFFu);
    w->x = 36 + g_ui_scale * 10;
    w->y = 34 + g_ui_scale * 8;
    w->w = (int)g_fb.width > 1100 ? (g_ui_scale > 1 ? 1140 : 980) : (int)g_fb.width - 72;
    w->h = (int)g_fb.height > 760 ? (g_ui_scale > 1 ? 700 : 620) : (int)g_fb.height - 90;
    w->bg = color_lerp(th->window_fill, 0xFF000000u, 80u);
    w->visible = 1;
    w->terminal = 1;
    w->owner_tid = -1;
    w->birth_tick = sys_get_ticks();
    w->term_slot = (uint8_t)slot;
    strncpy(w->title, "Konsole", sizeof(w->title) - 1);
    desk_window_set_icon(w, "/boot/res/icons/terminal.bmp");
    window_clamp_rect(w, (int)g_fb.width, (int)g_fb.height);
    g_focus_index = g_window_count - 1;
    if (slot < 0 || slot >= DESK_MAX_WINDOWS) return;
    desk_term_state_t* ts = &g_term_states[slot];
    memset(ts, 0, sizeof(*ts));
    strncpy(ts->cwd, "/", sizeof(ts->cwd) - 1);
    ts->cwd[sizeof(ts->cwd) - 1] = '\0';
    g_term_exec_state = ts;
    term_print_banner();
    g_term_exec_state = 0;
}

void open_clock_window(void) {
    for (int i = 0; i < g_window_count; ++i) {
        if (g_windows[i].visible && g_windows[i].analog_clock) {
            window_bring_to_front(i);
            return;
        }
    }
    if (g_window_count >= DESK_MAX_WINDOWS) return;
    desk_window_t* w = &g_windows[g_window_count++];
    memset(w, 0, sizeof(*w));
    w->id = 0xA11u;
    w->x = (int)g_fb.width - 340;
    if (w->x < 24) w->x = 24;
    w->y = 48;
    w->w = 300;
    w->h = 300;
    w->bg = 0xFF0D1826u;
    w->visible = 1;
    w->analog_clock = 1;
    w->owner_tid = -1;
    w->birth_tick = sys_get_ticks();
    strncpy(w->title, "Analog Clock", sizeof(w->title) - 1);
    desk_window_set_icon(w, "/boot/res/icons/clock.bmp");
    window_clamp_rect(w, (int)g_fb.width, (int)g_fb.height);
    g_focus_index = g_window_count - 1;
}

static void open_explorer_window_at(const char* path) {
    if (g_window_count >= DESK_MAX_WINDOWS) return;
    desk_browser_state_t* st = (desk_browser_state_t*)malloc(sizeof(*st));
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->selected = -1;
    st->last_click_index = -1;
    strncpy(st->status, "Ready", sizeof(st->status) - 1);
    st->status[sizeof(st->status) - 1] = '\0';
    if (path && path[0]) {
        strncpy(st->path, path, sizeof(st->path) - 1);
        st->path[sizeof(st->path) - 1] = '\0';
        strncpy(g_browser_default_path, st->path, sizeof(g_browser_default_path) - 1);
        g_browser_default_path[sizeof(g_browser_default_path) - 1] = '\0';
    } else if (g_browser_default_path[0]) {
        strncpy(st->path, g_browser_default_path, sizeof(st->path) - 1);
        st->path[sizeof(st->path) - 1] = '\0';
    } else {
        strncpy(st->path, "/", sizeof(st->path) - 1);
        st->path[sizeof(st->path) - 1] = '\0';
    }
    if (!st->path[0]) {
        strncpy(st->path, "/", sizeof(st->path) - 1);
        st->path[sizeof(st->path) - 1] = '\0';
    }
    if (!browser_state_register(st)) {
        free(st);
        return;
    }

    if (g_window_count >= DESK_MAX_WINDOWS) {
        browser_state_unregister(st->id);
        free(st);
        return;
    }
    desk_window_t* w = &g_windows[g_window_count++];
    const desk_theme_t* th = desk_theme();
    memset(w, 0, sizeof(*w));
    w->id = 0xE100u + (uint64_t)g_window_count + (sys_get_ticks() & 0x0FFFu);
    w->x = 70 + g_ui_scale * 8;
    w->y = 44 + g_ui_scale * 8;
    w->w = (int)g_fb.width > 900 ? (g_ui_scale > 1 ? 1100 : 900) : (int)g_fb.width - 40;
    w->h = (int)g_fb.height > 640 ? (g_ui_scale > 1 ? 680 : 560) : (int)g_fb.height - 60;
    w->bg = th->window_fill;
    w->visible = 1;
    w->file_browser = 1;
    w->browser_id = st->id;
    w->owner_tid = -1;
    w->birth_tick = sys_get_ticks();
    strncpy(w->title, "Explorer", sizeof(w->title) - 1);
    desk_window_set_icon(w, "/boot/res/icons/explorer.bmp");
    window_clamp_rect(w, (int)g_fb.width, (int)g_fb.height);
    g_focus_index = g_window_count - 1;
    (void)browser_refresh(st);
}

void open_explorer_window(void) {
    open_explorer_window_at(0);
}

void open_settings_window(void) {
    (void)0;
}

static int start_menu_item_at(int x, int y) {
    int pad = 12;
    int bar_h = 28;
    int bar_y = (int)g_fb.height - bar_h - 8;
    int sh = bar_y - 10;
    int sx = pad;
    int sy = bar_y - sh;
    int header_h = 54;
    int quick_w = 90;
    int quick_h = 20;
    int quick_y = sy + 30;
    int quick_x2 = sx + DESK_START_W - 12 - quick_w;
    int quick_x1 = quick_x2 - 8 - quick_w;
    int search_h = 28;
    int row_h = 28;
    int iy = sy + header_h + search_h + 18;
    int list_x = sx + 12;
    int list_w = DESK_START_W - 24;
    int shown = 0;
    if (!g_start_open) return -1;
    if (!in_rect(x, y, sx, sy, DESK_START_W, sh)) return -1;
    if (in_rect(x, y, quick_x1, quick_y, quick_w, quick_h)) return -20;
    if (in_rect(x, y, quick_x2, quick_y, quick_w, quick_h)) return -21;
    {
        int btn_w = (DESK_START_W - 40) / 3;
        int gap = 6;
        int by = sy + sh - 46;
        int bx = sx + 12;
        if (in_rect(x, y, bx, by, btn_w, 24)) return -10;
        if (in_rect(x, y, bx + btn_w + gap, by, btn_w, 24)) return -12;
        if (in_rect(x, y, bx + 2 * (btn_w + gap), by, btn_w, 24)) return -11;
    }
    int list_h = (sy + sh - 54) - iy;
    if (list_h < 40) list_h = 40;
    int yy = iy - g_start_scroll;
    const int cat_header_h = 18;
    for (int cat = 0; cat < 5; ++cat) {
        int count = 0;
        for (int i = 0; i < g_icon_count; ++i) {
            if (!start_menu_match_icon(i)) continue;
            if (start_menu_category_id(&g_icons[i]) != cat) continue;
            count++;
        }
        if (count == 0) continue;
        yy += cat_header_h;
        for (int i = 0; i < g_icon_count; ++i) {
            if (!start_menu_match_icon(i)) continue;
            if (start_menu_category_id(&g_icons[i]) != cat) continue;
            if (shown >= DESK_START_MAX_ITEMS) break;
            if (yy + row_h > iy && yy < iy + list_h) {
                if (in_rect(x, y, list_x, yy, list_w, row_h - 4)) return i;
            }
            shown++;
            yy += row_h;
        }
        if (shown >= DESK_START_MAX_ITEMS) break;
    }
    return -2;
}

static char lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static float ease_smooth(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static int str_ends_with_ci(const char* s, const char* ext) {
    if (!s || !ext) return 0;
    size_t sl = strlen(s);
    size_t el = strlen(ext);
    if (el == 0 || sl < el) return 0;
    const char* p = s + sl - el;
    for (size_t i = 0; i < el; ++i) {
        if (lower_char(p[i]) != lower_char(ext[i])) return 0;
    }
    return 1;
}

static int path_is_image_ext(const char* path) {
    if (!path || !path[0]) return 0;
    return str_ends_with_ci(path, ".png") ||
           str_ends_with_ci(path, ".jpg") ||
           str_ends_with_ci(path, ".jpeg") ||
           str_ends_with_ci(path, ".bmp");
}

static int browser_is_dir_path(const char* path) {
    uint64_t n = 0;
    return sys_fs_list_dir(path, 0, 0, &n) == 0;
}

static int browser_copy_file(const char* src, const char* dst) {
    if (!src || !dst) return -1;
    if (sys_fs_copy_fast(src, dst) == 0) return 0;
    uint64_t len = 0;
    if (sys_fs_read_file(src, 0, 0, &len) != 0) return -1;
    if (len == 0) {
        char parent[256];
        char name[96];
        if (split_parent_name(dst, parent, name, sizeof(parent)) == 0) {
            return sys_fs_create_file(parent, name, "", 0) == 0 ? 0 : -1;
        }
        return -1;
    }
    if (len > (64u * 1024u * 1024u)) return -1;
    void* buf = malloc((size_t)len);
    if (!buf) return -1;
    if (sys_fs_read_file(src, buf, len, &len) != 0) {
        free(buf);
        return -1;
    }
    long rc = sys_fs_write_file(dst, buf, len);
    if (rc != 0) {
        char parent[256];
        char name[96];
        if (split_parent_name(dst, parent, name, sizeof(parent)) == 0) {
            rc = sys_fs_create_file(parent, name, buf, len);
        }
    }
    free(buf);
    return rc == 0 ? 0 : -1;
}

static int browser_copy_tree(const char* src, const char* dst) {
    ntux_dirent_t ents[DESK_LS_MAX];
    uint64_t count = 0;
    if (sys_fs_list_dir(src, ents, DESK_LS_MAX, &count) != 0) return -1;
    if (sys_fs_exists(dst) <= 0) {
        char parent[256];
        char name[96];
        if (split_parent_name(dst, parent, name, sizeof(parent)) != 0) return -1;
        if (sys_fs_mkdir(parent, name) != 0) return -1;
    }
    if (count > DESK_LS_MAX) count = DESK_LS_MAX;
    for (uint64_t i = 0; i < count; ++i) {
        if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
        char s[256];
        char d[256];
        if (join_path(src, ents[i].name, s, sizeof(s)) != 0) return -1;
        if (join_path(dst, ents[i].name, d, sizeof(d)) != 0) return -1;
        if (ents[i].is_dir) {
            if (browser_copy_tree(s, d) != 0) return -1;
        } else {
            if (browser_copy_file(s, d) != 0) return -1;
        }
    }
    return 0;
}

static void browser_clipboard_set(desk_browser_state_t* st, int cut) {
    g_browser_clip_count = 0;
    g_browser_clip_cut = cut ? 1 : 0;
    if (!st) return;
    for (int i = 0; i < st->count && i < DESK_LS_MAX; ++i) {
        if (!st->mark[i]) continue;
        char full[256];
        browser_path_join(st->path, st->ents[i].name, full, sizeof(full));
        if (!full[0]) continue;
        strncpy(g_browser_clip_paths[g_browser_clip_count], full, sizeof(g_browser_clip_paths[g_browser_clip_count]) - 1);
        g_browser_clip_paths[g_browser_clip_count][sizeof(g_browser_clip_paths[g_browser_clip_count]) - 1] = '\0';
        g_browser_clip_count++;
        if (g_browser_clip_count >= DESK_LS_MAX) break;
    }
    if (g_browser_clip_count == 0 && st->selected >= 0 && st->selected < st->count) {
        char full[256];
        browser_path_join(st->path, st->ents[st->selected].name, full, sizeof(full));
        if (full[0]) {
            strncpy(g_browser_clip_paths[0], full, sizeof(g_browser_clip_paths[0]) - 1);
            g_browser_clip_paths[0][sizeof(g_browser_clip_paths[0]) - 1] = '\0';
            g_browser_clip_count = 1;
        }
    }
}

static int browser_clipboard_paste(desk_browser_state_t* st) {
    if (g_browser_clip_count <= 0) return -1;
    if (!st) return -1;
    for (int i = 0; i < g_browser_clip_count; ++i) {
        const char* src = g_browser_clip_paths[i];
        const char* base = path_basename_ptr(src);
        if (!src || !src[0] || !base || !base[0]) continue;
        char dst[256];
        if (join_path(st->path, base, dst, sizeof(dst)) != 0) continue;
        if (sys_fs_exists(dst) > 0) {
            char name[96];
            char base_name[96];
            char ext[32];
            char tmp[256];
            const char* dot = strrchr(base, '.');
            if (dot && dot != base) {
                size_t bl = (size_t)(dot - base);
                if (bl >= sizeof(base_name)) bl = sizeof(base_name) - 1;
                memcpy(base_name, base, bl);
                base_name[bl] = '\0';
                strncpy(ext, dot, sizeof(ext) - 1);
                ext[sizeof(ext) - 1] = '\0';
            } else {
                strncpy(base_name, base, sizeof(base_name) - 1);
                base_name[sizeof(base_name) - 1] = '\0';
                ext[0] = '\0';
            }
            browser_make_name(st, name, sizeof(name), base_name, ext);
            if (join_path(st->path, name, tmp, sizeof(tmp)) == 0 && sys_fs_exists(tmp) <= 0) {
                strncpy(dst, tmp, sizeof(dst) - 1);
                dst[sizeof(dst) - 1] = '\0';
            }
        }
        if (browser_is_dir_path(src)) {
            if (browser_copy_tree(src, dst) != 0) return -1;
        } else {
            if (browser_copy_file(src, dst) != 0) return -1;
        }
        if (g_browser_clip_cut) {
            (void)sys_fs_remove(src);
        }
    }
    return 0;
}

static int path_is_audio_ext(const char* path) {
    if (!path || !path[0]) return 0;
    return str_ends_with_ci(path, ".mp3") ||
           str_ends_with_ci(path, ".wav") ||
           str_ends_with_ci(path, ".flac") ||
           str_ends_with_ci(path, ".ogg") ||
           str_ends_with_ci(path, ".m4a") ||
           str_ends_with_ci(path, ".aac") ||
           str_ends_with_ci(path, ".wma") ||
           str_ends_with_ci(path, ".aiff") ||
           str_ends_with_ci(path, ".alac") ||
           str_ends_with_ci(path, ".m3u");
}

static int path_is_video_ext(const char* path) {
    if (!path || !path[0]) return 0;
    return str_ends_with_ci(path, ".mp4") ||
           str_ends_with_ci(path, ".mkv") ||
           str_ends_with_ci(path, ".avi") ||
           str_ends_with_ci(path, ".webm") ||
           str_ends_with_ci(path, ".mov") ||
           str_ends_with_ci(path, ".wmv") ||
           str_ends_with_ci(path, ".m4v") ||
           str_ends_with_ci(path, ".mpg") ||
           str_ends_with_ci(path, ".mpeg") ||
           str_ends_with_ci(path, ".3gp");
}

static int path_is_archive_ext(const char* path) {
    if (!path || !path[0]) return 0;
    return str_ends_with_ci(path, ".zip") ||
           str_ends_with_ci(path, ".tar") ||
           str_ends_with_ci(path, ".gz") ||
           str_ends_with_ci(path, ".7z") ||
           str_ends_with_ci(path, ".rar") ||
           str_ends_with_ci(path, ".bz2") ||
           str_ends_with_ci(path, ".xz") ||
           str_ends_with_ci(path, ".iso") ||
           str_ends_with_ci(path, ".tgz") ||
           str_ends_with_ci(path, ".tbz") ||
           str_ends_with_ci(path, ".txz");
}

static int path_is_code_ext(const char* path) {
    if (!path || !path[0]) return 0;
    return str_ends_with_ci(path, ".c") ||
           str_ends_with_ci(path, ".h") ||
           str_ends_with_ci(path, ".cpp") ||
           str_ends_with_ci(path, ".hpp") ||
           str_ends_with_ci(path, ".cc") ||
           str_ends_with_ci(path, ".cxx") ||
           str_ends_with_ci(path, ".hh") ||
           str_ends_with_ci(path, ".rs") ||
           str_ends_with_ci(path, ".go") ||
           str_ends_with_ci(path, ".java") ||
           str_ends_with_ci(path, ".cs") ||
           str_ends_with_ci(path, ".kt") ||
           str_ends_with_ci(path, ".py") ||
           str_ends_with_ci(path, ".js") ||
           str_ends_with_ci(path, ".ts") ||
           str_ends_with_ci(path, ".tsx") ||
           str_ends_with_ci(path, ".jsx") ||
           str_ends_with_ci(path, ".lua") ||
           str_ends_with_ci(path, ".php") ||
           str_ends_with_ci(path, ".rb") ||
           str_ends_with_ci(path, ".swift") ||
           str_ends_with_ci(path, ".html") ||
           str_ends_with_ci(path, ".css") ||
           str_ends_with_ci(path, ".scss") ||
           str_ends_with_ci(path, ".xml") ||
           str_ends_with_ci(path, ".yml") ||
           str_ends_with_ci(path, ".yaml") ||
           str_ends_with_ci(path, ".asm") ||
           str_ends_with_ci(path, ".s") ||
           str_ends_with_ci(path, ".S");
}

static int path_is_text_ext(const char* path) {
    if (!path || !path[0]) return 0;
    return str_ends_with_ci(path, ".txt") ||
           str_ends_with_ci(path, ".md") ||
           str_ends_with_ci(path, ".markdown") ||
           str_ends_with_ci(path, ".rst") ||
           str_ends_with_ci(path, ".cfg") ||
           str_ends_with_ci(path, ".ini") ||
           str_ends_with_ci(path, ".conf") ||
           str_ends_with_ci(path, ".json") ||
           str_ends_with_ci(path, ".toml") ||
           str_ends_with_ci(path, ".log") ||
           str_ends_with_ci(path, ".nfo") ||
           str_ends_with_ci(path, ".rtf") ||
           str_ends_with_ci(path, ".xml") ||
           str_ends_with_ci(path, ".csv");
}

static int path_is_obj_ext(const char* path) {
    if (!path || !path[0]) return 0;
    return str_ends_with_ci(path, ".obj");
}

static int text_contains_ci(const char* hay, const char* needle) {
    if (!needle || !needle[0]) return 1;
    if (!hay) return 0;
    for (size_t i = 0; hay[i]; ++i) {
        size_t j = 0;
        while (needle[j] && hay[i + j] &&
               lower_char(hay[i + j]) == lower_char(needle[j])) {
            ++j;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static int start_menu_is_app(const desk_icon_t* icon) {
    const char* p;
    if (!icon || !icon->exec_path[0]) return 0;
    if (icon->is_dir) return 0;
    p = icon->exec_path;
    if (strncmp(p, "::", 2) == 0) return 1;
    if (str_ends_with_ci(p, ".elf")) return 1;
    return 0;
}

static int start_menu_match_icon(int idx) {
    if (idx < 0 || idx >= g_icon_count) return 0;
    if (!g_icons[idx].visible || !g_icons[idx].exec_path[0]) return 0;
    if (!start_menu_is_app(&g_icons[idx])) return 0;
    if (g_start_query_len == 0) return 1;
    if (text_contains_ci(g_icons[idx].label, g_start_query)) return 1;
    if (text_contains_ci(g_icons[idx].exec_path, g_start_query)) return 1;
    return 0;
}

static void start_menu_clear_query(void) {
    g_start_query_len = 0;
    g_start_query[0] = '\0';
    g_start_scroll = 0;
}

static int start_menu_category_id(const desk_icon_t* icon) {
    if (!icon) return 4;
    const char* p = icon->exec_path;
    const char* l = icon->label;
    if (text_contains_ci(l, "tetris") || text_contains_ci(l, "doom") ||
        text_contains_ci(l, "snake") || text_contains_ci(l, "flappy") ||
        text_contains_ci(l, "game")) return 3;
    if (text_contains_ci(l, "img") || text_contains_ci(l, "view") ||
        text_contains_ci(l, "music") || text_contains_ci(l, "video")) return 2;
    if (text_contains_ci(l, "settings") || text_contains_ci(l, "task") ||
        text_contains_ci(l, "part") ||
        text_contains_ci(l, "terminal") || text_contains_ci(l, "console") ||
        text_contains_ci(l, "explorer")) return 0;
    if (p && (text_contains_ci(p, "/bin") || text_contains_ci(p, "/apps"))) return 1;
    return 4;
}

static const char* start_menu_category_name(int id) {
    switch (id) {
        case 0: return "System";
        case 1: return "Tools";
        case 2: return "Media";
        case 3: return "Games";
        default: return "Other";
    }
}

__attribute__((unused)) static int start_menu_category_match_icon(const desk_icon_t* icon, int cat) {
    if (!icon) return 0;
    if (!start_menu_match_icon((int)(icon - g_icons))) return 0;
    return start_menu_category_id(icon) == cat;
}

static int start_menu_content_height(void) {
    int h = 0;
    const int cat_header_h = 18;
    const int row_h = 28;
    for (int cat = 0; cat < 5; ++cat) {
        int count = 0;
        for (int i = 0; i < g_icon_count; ++i) {
            if (!start_menu_match_icon(i)) continue;
            if (start_menu_category_id(&g_icons[i]) != cat) continue;
            count++;
        }
        if (count == 0) continue;
        h += cat_header_h;
        h += count * row_h;
    }
    return h;
}

static int start_menu_item_y_by_visible(int sel) {
    const int cat_header_h = 18;
    const int row_h = 28;
    int shown = 0;
    int y = 0;
    for (int cat = 0; cat < 5; ++cat) {
        int count = 0;
        for (int i = 0; i < g_icon_count; ++i) {
            if (!start_menu_match_icon(i)) continue;
            if (start_menu_category_id(&g_icons[i]) != cat) continue;
            count++;
        }
        if (count == 0) continue;
        y += cat_header_h;
        for (int i = 0; i < g_icon_count; ++i) {
            if (!start_menu_match_icon(i)) continue;
            if (start_menu_category_id(&g_icons[i]) != cat) continue;
            if (shown == sel) return y;
            shown++;
            y += row_h;
        }
    }
    return 0;
}

static int start_menu_visible_count(void) {
    int shown = 0;
    for (int i = 0; i < g_icon_count; ++i) {
        if (!start_menu_match_icon(i)) continue;
        if (shown >= DESK_START_MAX_ITEMS) break;
        shown++;
    }
    return shown;
}

static int start_menu_index_by_visible(int sel) {
    int shown = 0;
    if (sel < 0) return -1;
    for (int i = 0; i < g_icon_count; ++i) {
        if (!start_menu_match_icon(i)) continue;
        if (shown == sel) return i;
        shown++;
        if (shown >= DESK_START_MAX_ITEMS) break;
    }
    return -1;
}

static void update_cursor_kind(int mx, int my) {
    cursor_set_kind(CURSOR_ARROW);
    if (taskbar_start_hit(mx, my) || taskbar_clock_hit(mx, my) || taskbar_gallery_hit(mx, my) ||
        start_menu_item_at(mx, my) >= 0) {
        cursor_set_kind(CURSOR_HAND);
        return;
    }
    if (notify_hit(mx, my) >= 0) {
        cursor_set_kind(CURSOR_HAND);
        return;
    }
    if (gallery_hit(mx, my)) {
        cursor_set_kind(CURSOR_HAND);
        return;
    }
    if (g_ctx_open && desktop_menu_item_at(mx, my) >= 0) {
        cursor_set_kind(CURSOR_HAND);
        return;
    }
    if (find_icon_hit(mx, my) >= 0) {
        cursor_set_kind(CURSOR_HAND);
        return;
    }
    for (int i = g_window_count - 1; i >= 0; --i) {
        desk_window_t* w = &g_windows[i];
        if (!inside_window(w, mx, my)) continue;
        if (window_resize_handle_hit(w, mx, my)) {
            cursor_set_kind(CURSOR_RESIZE_NWSE);
        } else if (my < w->y + DESK_TITLEBAR_H) {
            cursor_set_kind(CURSOR_MOVE);
        } else {
            cursor_set_kind(CURSOR_ARROW);
        }
        return;
    }
}

static void handle_mouse(void) {
    ntux_mouse_state_t ms;
    if (sys_mouse_get_state(&ms) != 0) {
        ms.x = g_last_mouse_x;
        ms.y = g_last_mouse_y;
        ms.left = 0;
        ms.right = 0;
        ms.middle = 0;
        ms.scroll = 0;
    }
    int mx = ms.x;
    int my = ms.y;
    int kleft = (sys_kbd_is_pressed(0x4B) > 0) ? 1 : 0;
    int kright = (sys_kbd_is_pressed(0x4D) > 0) ? 1 : 0;
    int kup = (sys_kbd_is_pressed(0x48) > 0) ? 1 : 0;
    int kdown = (sys_kbd_is_pressed(0x50) > 0) ? 1 : 0;
    if (kleft || kright || kup || kdown) {
        int speed = ((sys_kbd_is_pressed(0x2A) > 0) || (sys_kbd_is_pressed(0x36) > 0)) ? 10 : 4;
        mx = g_last_mouse_x + (kright - kleft) * speed;
        my = g_last_mouse_y + (kdown - kup) * speed;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx >= (int)g_fb.width) mx = (int)g_fb.width - 1;
        if (my >= (int)g_fb.height) my = (int)g_fb.height - 1;
    }

    if (mx != g_last_mouse_x || my != g_last_mouse_y ||
        ms.left != g_last_mouse_left || ms.right != g_last_mouse_right ||
        ms.middle != g_last_mouse_middle || ms.scroll != 0) {
        desktop_mark_input();
        g_desktop_dirty = 1;
    }

    if (g_msgbox_active) {
        (void)msgbox_handle_mouse(mx, my, (ms.left && !g_last_left));
        g_last_left = ms.left;
        g_last_mouse_x = mx;
        g_last_mouse_y = my;
        update_cursor_kind(mx, my);
        return;
    }
    if (g_namebox_active) {
        (void)namebox_handle_mouse(mx, my, (ms.left && !g_last_left));
        g_last_left = ms.left;
        g_last_mouse_x = mx;
        g_last_mouse_y = my;
        update_cursor_kind(mx, my);
        return;
    }
    if (g_picker_active) {
        (void)picker_handle_mouse(mx, my, (ms.left && !g_last_left), ms.scroll);
        g_last_left = ms.left;
        g_last_mouse_x = mx;
        g_last_mouse_y = my;
        update_cursor_kind(mx, my);
        return;
    }
    if (g_gallery_open) {
        int total = gallery_image_count();
        int cols = 4;
        int page_size = cols * 2;
        if (poll_special_press(0x0F)) { /* Tab */
            if (total > 0) g_gallery_sel = (g_gallery_sel + 1) % total;
        } else if (poll_special_press(0x48)) { /* Up */
            if (total > 0) {
                g_gallery_sel -= cols;
                if (g_gallery_sel < 0) g_gallery_sel = 0;
            }
        } else if (poll_special_press(0x50)) { /* Down */
            if (total > 0) {
                g_gallery_sel += cols;
                if (g_gallery_sel >= total) g_gallery_sel = total - 1;
            }
        } else if (poll_special_press(0x4B)) { /* Left */
            if (total > 0 && g_gallery_sel > 0) g_gallery_sel--;
        } else if (poll_special_press(0x4D)) { /* Right */
            if (total > 0 && g_gallery_sel + 1 < total) g_gallery_sel++;
        } else if (poll_special_press(0x1C)) { /* Enter */
            if (g_gallery_sel >= 0 && g_gallery_sel < total) {
                wallpaper_set_and_save(g_wallpaper_paths[g_gallery_sel]);
            }
        } else if (poll_special_press(0x01)) { /* Esc */
            g_gallery_open = 0;
        }
        if (total > 0) {
            int new_page = g_gallery_sel / page_size;
            if (new_page != g_gallery_page) g_gallery_page = new_page;
        }
        return;
    }

    if (g_start_open && ms.scroll != 0) {
        int pad = 12;
        int bar_h = 28;
        int bar_y = (int)g_fb.height - bar_h - 8;
        int sh = bar_y - 10;
        int sx = pad;
        int sy = bar_y - sh;
        if (in_rect(mx, my, sx, sy, DESK_START_W, sh)) {
            int delta = (ms.scroll > 0) ? -28 : 28;
            g_start_scroll += delta;
            if (g_start_scroll < 0) g_start_scroll = 0;
            g_desktop_dirty = 1;
        }
    }

    if (ms.right && !g_last_mouse_right) {
        int consumed = 0;
        if (g_focus_index >= 0 && g_focus_index < g_window_count) {
            desk_window_t* w = &g_windows[g_focus_index];
            if (w->visible && w->file_browser) {
                desk_browser_state_t* st = window_browser_state(w);
                int list_x, list_y, list_w, list_h;
                browser_list_rect(w, st, &list_x, &list_y, &list_w, &list_h);
                if (in_rect(mx, my, list_x, list_y, list_w, list_h)) {
                    int idx = browser_entry_at(w, st, mx, my);
                    if (idx >= 0) {
                        browser_mark_only(st, idx);
                    } else {
                        browser_clear_marks(st);
                    }
                    browser_menu_open_at(st, mx, my);
                    consumed = 1;
                }
            }
        }
        if (!consumed && desktop_is_background_hit(mx, my)) {
            desktop_menu_open_at(mx, my);
            consumed = 1;
        }
        if (consumed) {
            g_last_left = ms.left;
            g_last_mouse_x = mx;
            g_last_mouse_y = my;
            g_last_mouse_left = ms.left;
            g_last_mouse_right = ms.right;
            g_last_mouse_middle = ms.middle;
            g_last_mouse_scroll = ms.scroll;
            update_cursor_kind(mx, my);
            desktop_publish_input_state();
            return;
        }
    }

    if (ms.left && !g_last_left) {
        g_icon_dragging = 0;
        g_icon_drag_index = -1;
        int clicked = 0;
    if (g_ctx_open) {
        int idx = desktop_menu_item_at(mx, my);
        int sidx = desktop_menu_sub_item_at(mx, my);
        if (idx >= 0) {
            if (idx == 0) {
                g_ctx_sub_open = 1;
                g_ctx_sub_hover = sidx;
                clicked = 1;
            } else if (idx == 1) {
                open_console_window();
            } else if (idx == 2) {
                open_explorer_window();
            } else if (idx == 3) {
                g_gallery_open = 1;
                wallpaper_scan_all();
                g_gallery_sel = 0;
            } else if (idx == 4) {
                open_settings_window();
            } else if (idx == 5) {
                desktop_rescan_icons();
            } else if (idx == 6) {
                start_power_action(1);
            } else if (idx == 7) {
                start_power_action(2);
            }
            if (idx != 0) {
                desktop_menu_close();
            }
            clicked = 1;
        } else if (sidx >= 0) {
            if (sidx == 0) {
                namebox_open(NAMEBOX_DESKTOP_NEW_FOLDER, "New Folder", "New Folder", g_desktop_dir, 0, 0);
            } else if (sidx == 1) {
                namebox_open(NAMEBOX_DESKTOP_NEW_FILE, "New File", "New File.txt", g_desktop_dir, 0, 0);
            }
            desktop_menu_close();
            clicked = 1;
        } else {
            desktop_menu_close();
        }
        }
        int nidx = notify_hit(mx, my);
        if (nidx >= 0) {
            g_notifs[nidx].active = 0;
            g_notifs[nidx].anim = 0.0f;
            clicked = 1;
        } else if (taskbar_gallery_hit(mx, my)) {
            g_gallery_open = g_gallery_open ? 0 : 1;
            if (g_gallery_open) {
                wallpaper_scan_all();
                g_gallery_sel = 0;
            }
            clicked = 1;
        } else if (g_gallery_open && gallery_handle_click(mx, my)) {
            clicked = 1;
        } else if (g_gallery_open) {
            g_gallery_open = 0;
        }
        if (!clicked && taskbar_start_hit(mx, my)) {
            g_start_open = g_start_open ? 0u : 1u;
            if (g_start_open) {
                g_start_sel = 0;
                start_menu_clear_query();
            }
            clicked = 1;
        } else if (!clicked && taskbar_clock_hit(mx, my)) {
            open_clock_window();
            clicked = 1;
        } else if (!clicked && g_start_open) {
            int sm = start_menu_item_at(mx, my);
            if (sm >= 0) {
                if (strcmp(g_icons[sm].exec_path, "::explorer") == 0) open_explorer_window();
                else if (strcmp(g_icons[sm].exec_path, "::console") == 0) open_console_window();
                else if (g_icons[sm].exec_path[0]) (void)desktop_launch_target(g_icons[sm].exec_path);
                g_start_open = 0;
                clicked = 1;
            } else if (sm == -20) {
                open_explorer_window();
                g_start_open = 0;
                clicked = 1;
            } else if (sm == -21) {
                open_console_window();
                g_start_open = 0;
                clicked = 1;
            } else if (sm == -10) {
                start_power_action(1);
                clicked = 1;
            } else if (sm == -12) {
                desktop_logout();
                clicked = 1;
            } else if (sm == -11) {
                start_power_action(2);
                clicked = 1;
            } else if (sm == -1) {
                g_start_open = 0;
            } else {
                clicked = 1;
            }
        }
        int tb_idx = taskbar_window_at(mx, my);
        if (!clicked && tb_idx >= 0 && tb_idx < g_window_count) {
            desk_window_t* tw = &g_windows[tb_idx];
            if (tw->minimized) {
                window_start_restore(tb_idx);
            } else {
                tw->visible = 1;
            }
            window_bring_to_front(tb_idx);
            clicked = 1;
        }
        for (int i = g_window_count - 1; !clicked && i >= 0; --i) {
            desk_window_t* w = &g_windows[i];
            if (!w->visible) continue;
            if (inside_window(w, mx, my)) {
                clicked = 1;
                g_start_open = 0;
                desktop_menu_close();
                window_bring_to_front(i);
                int top = g_focus_index;
                if (top >= 0) {
                    desk_window_t* fw = &g_windows[top];
                    if (window_btn_close_hit(fw, mx, my)) {
                        if (fw->owner_tid >= 0) {
                            desktop_request_close(top);
                        } else {
                            desktop_window_cleanup(top, 0);
                            update_focus_after_visibility_change();
                        }
                    } else if (window_btn_max_hit(fw, mx, my)) {
                        desk_window_toggle_maximize(fw, (int)g_fb.width, (int)g_fb.height);
                    } else if (window_btn_min_hit(fw, mx, my)) {
                        window_start_minimize(top);
                        update_focus_after_visibility_change();
                    } else if (window_resize_handle_hit(fw, mx, my)) {
                        g_resizing = 1;
                        g_resize_index = top;
                        g_resize_start_x = mx;
                        g_resize_start_y = my;
                        g_resize_start_w = fw->w;
                        g_resize_start_h = fw->h;
                    } else if (my < fw->y + DESK_TITLEBAR_H && !fw->maximized) {
                        g_dragging = 1;
                        g_drag_index = top;
                        g_drag_off_x = mx - fw->x;
                        g_drag_off_y = my - fw->y;
                    } else if (fw->file_browser) {
                        (void)browser_handle_click(fw, window_browser_state(fw), mx, my);
                    }
                }
                break;
            }
        }
        if (!clicked) {
            int icon_idx = find_icon_hit(mx, my);
            if (icon_idx >= 0) {
                desktop_menu_close();
                g_icon_dragging = 1;
                g_icon_drag_index = icon_idx;
                g_icon_drag_off_x = mx - g_icons[icon_idx].x;
                g_icon_drag_off_y = my - g_icons[icon_idx].y;
                uint64_t now = sys_get_ticks();
                if (g_last_icon_click_index == icon_idx && now - g_last_icon_click_tick <= DESK_DBLCLICK_TICKS) {
                    g_icon_dragging = 0;
                    g_icon_drag_index = -1;
                    if (strcmp(g_icons[icon_idx].exec_path, "::explorer") == 0) {
                        open_explorer_window();
                    } else if (strcmp(g_icons[icon_idx].exec_path, "::console") == 0) {
                        open_console_window();
                    } else if (g_icons[icon_idx].is_image) {
                        (void)desktop_launch_image_viewer(g_icons[icon_idx].exec_path);
                    } else if (path_is_obj_ext(g_icons[icon_idx].exec_path)) {
                        (void)desktop_launch_objview_with_path(g_icons[icon_idx].exec_path);
                    } else if (g_icons[icon_idx].exec_path[0]) {
                        (void)desktop_launch_target(g_icons[icon_idx].exec_path);
                    }
                    g_last_icon_click_tick = 0;
                    g_last_icon_click_index = -1;
                } else {
                    g_last_icon_click_tick = now;
                    g_last_icon_click_index = icon_idx;
                }
            }
        }
        if (!clicked && desktop_is_background_hit(mx, my)) {
            desktop_menu_close();
        }
    }
    if (!ms.left && g_last_left) {
        if (g_icon_dragging) {
            if (g_icon_drag_index >= 0 && g_icon_drag_index < g_icon_count) {
                desktop_snap_icon_to_grid(&g_icons[g_icon_drag_index]);
                g_icon_custom_pos[g_icon_drag_index] = 1;
            }
            (void)desktop_conf_save_layout();
        }
        if (g_focus_index >= 0 && g_focus_index < g_window_count) {
            desk_window_t* w = &g_windows[g_focus_index];
            if (w->visible && w->file_browser) {
                desk_browser_state_t* st = window_browser_state(w);
                if (st) st->scroll_dragging = 0;
            }
        }
        g_icon_dragging = 0;
        g_icon_drag_index = -1;
        g_dragging = 0;
        g_drag_index = -1;
        g_resizing = 0;
        g_resize_index = -1;
    }

    if (g_dragging && g_drag_index >= 0 && g_drag_index < g_window_count) {
        desk_window_t* w = &g_windows[g_drag_index];
        if (w->maximized) {
            g_dragging = 0;
        } else {
            w->x = mx - g_drag_off_x;
            w->y = my - g_drag_off_y;
            window_clamp_rect(w, (int)g_fb.width, (int)g_fb.height);
        }
    }
    if (g_resizing && g_resize_index >= 0 && g_resize_index < g_window_count) {
        desk_window_t* w = &g_windows[g_resize_index];
        if (!w->maximized) {
            w->w = g_resize_start_w + (mx - g_resize_start_x);
            w->h = g_resize_start_h + (my - g_resize_start_y);
            window_clamp_rect(w, (int)g_fb.width, (int)g_fb.height);
        }
    }
    if (g_icon_dragging && g_icon_drag_index >= 0 && g_icon_drag_index < g_icon_count) {
        desk_icon_t* icon = &g_icons[g_icon_drag_index];
        icon->x = mx - g_icon_drag_off_x;
        icon->y = my - g_icon_drag_off_y;
        clamp_icon_position(icon);
    }

    if (g_focus_index >= 0 && g_focus_index < g_window_count) {
        desk_window_t* w = &g_windows[g_focus_index];
        if (w->visible && w->file_browser) {
            desk_browser_state_t* st = window_browser_state(w);
            if (st && st->scroll_dragging) {
                if (ms.left) {
                    int track_x, track_y, track_w, track_h, thumb_y, thumb_h;
                    int cols = 1;
                    int rows = 1;
                    int total_rows = 0;
                    if (browser_scrollbar_geom(w, st, &track_x, &track_y, &track_w, &track_h, &thumb_y, &thumb_h,
                                               &cols, &rows, &total_rows, 0)) {
                        int max_top = total_rows - rows;
                        if (max_top < 1) max_top = 1;
                        int rel = my - track_y - st->scroll_drag_offset;
                        if (rel < 0) rel = 0;
                        if (rel > track_h - thumb_h) rel = track_h - thumb_h;
                        int top_row = (track_h - thumb_h) > 0 ? (rel * max_top) / (track_h - thumb_h) : 0;
                        st->scroll = top_row * cols;
                    }
                } else {
                    st->scroll_dragging = 0;
                }
            }
        }
    }

    if (g_ctx_open) {
        g_ctx_hover = desktop_menu_item_at(mx, my);
        g_ctx_sub_hover = desktop_menu_sub_item_at(mx, my);
        if (g_ctx_hover > 0) g_ctx_sub_open = 0;
    }
    if (g_focus_index >= 0 && g_focus_index < g_window_count) {
        desk_window_t* w = &g_windows[g_focus_index];
        if (w->visible && w->file_browser) {
            desk_browser_state_t* st = window_browser_state(w);
            if (st && st->ctx_open) {
                st->ctx_hover = browser_menu_item_at(st, mx, my);
                st->ctx_sub_hover = browser_menu_sub_item_at(st, mx, my);
                if (st->ctx_hover > 0) st->ctx_sub_open = 0;
            }
        }
    }

    if (ms.scroll != 0 && g_focus_index >= 0 && g_focus_index < g_window_count) {
        desk_window_t* w = &g_windows[g_focus_index];
        if (w->visible && w->file_browser) {
            desk_browser_state_t* st = window_browser_state(w);
            int cols = 1;
            browser_calc_grid(w, st, &cols, 0, 0, 0);
            int delta = ms.scroll > 0 ? -cols : cols;
            if (st) {
                st->scroll += delta;
                if (st->scroll < 0) st->scroll = 0;
                int max_start = browser_scroll_max(w, st);
                if (st->scroll > max_start) st->scroll = max_start;
            }
        }
    }

    g_last_left = ms.left;
    g_last_mouse_x = mx;
    g_last_mouse_y = my;
    g_last_mouse_left = ms.left;
    g_last_mouse_right = ms.right;
    g_last_mouse_middle = ms.middle;
    g_last_mouse_scroll = ms.scroll;
    update_cursor_kind(mx, my);
    desktop_publish_input_state();
}


static char poll_char(void) {
    uint64_t now = sys_get_ticks();
    const uint64_t debounce = 12u;
    const uint64_t same_char_guard = 16u;
    if (!desktop_wants_console_input()) {
        return 0;
    }
    if (sys_console_claim() != 0) {
        return 0;
    }
    if (now - g_last_key_tick < debounce) return 0;
    long v = sys_getchar();
    if (v >= 0 && v <= 255) {
        char c = (char)(uint8_t)v;
        if (c == '\r') c = '\n';
        if (c == '\n' || c == '\t' || c == '\b' || c == 127) {
            g_last_key_tick = now;
            g_last_key_char = c;
            desktop_mark_input();
            return c;
        }
        if (c >= 32 && c < 127) {
            if (c == g_last_key_char && now - g_last_key_tick < same_char_guard) return 0;
            g_last_key_tick = now;
            g_last_key_char = c;
            desktop_mark_input();
            return c;
        }
    }

    if (poll_special_press(0x1C)) { g_last_key_tick = now; g_last_key_char = '\n'; return '\n'; }
    if (poll_special_press(0x0E)) { g_last_key_tick = now; g_last_key_char = '\b'; return '\b'; }
    if (poll_special_press(0x0F)) { g_last_key_tick = now; g_last_key_char = '\t'; return '\t'; }
    if (poll_special_press(0x39)) { g_last_key_tick = now; g_last_key_char = ' '; return ' '; }

    int shift = (sys_kbd_is_pressed(0x2A) > 0) || (sys_kbd_is_pressed(0x36) > 0);
    for (size_t i = 0; i < sizeof(g_keys) / sizeof(g_keys[0]); ++i) {
        if (poll_special_press(g_keys[i].scancode)) {
            g_last_key_tick = now;
            g_last_key_char = shift ? g_keys[i].shifted : g_keys[i].normal;
            return g_last_key_char;
        }
    }
    return 0;
}

static void desktop_mark_input(void) {
    uint64_t now = desktop_now_seconds();
    if (now != 0) g_last_input_tick = now;
}

static int desktop_ticks_are_advancing(void) {
    uint64_t t0 = sys_get_ticks();
    for (int i = 0; i < 4096; ++i) {
        sys_yield();
        if (sys_get_ticks() != t0) return 1;
    }
    return 0;
}

static void desktop_wait_ticks(uint64_t ticks) {
    if (ticks == 0) return;
    if (g_ticks_advancing) {
        sys_wait_ticks(ticks);
        return;
    }
    for (uint64_t i = 0; i < ticks; ++i) {
        sys_yield();
    }
}

static uint64_t desktop_get_hz(void) {
    if (g_hz_cached) return g_hz_cached;
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz < 20u || hz > 10000u) hz = 200u;
    ntux_time_t t0;
    if (sys_get_time(&t0) == 0) {
        uint8_t sec = t0.second;
        uint64_t start = sys_get_ticks();
        uint64_t deadline = start + hz * 2u;
        for (;;) {
            ntux_time_t t1;
            if (sys_get_time(&t1) == 0 && t1.second != sec) {
                uint64_t delta = sys_get_ticks() - start;
                if (delta >= 20u && delta <= 4000u) {
                    hz = delta;
                }
                break;
            }
            if (sys_get_ticks() > deadline) break;
            sys_yield();
        }
    }
    g_hz_cached = hz;
    return hz;
}

static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ?  -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static uint64_t desktop_now_seconds(void) {
    ntux_time_t t;
    if (sys_get_time(&t) != 0) return 0;
    if (t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31) return 0;
    int64_t days = days_from_civil((int)t.year, (unsigned)t.month, (unsigned)t.day);
    if (days < 0) return 0;
    return (uint64_t)days * 86400u + (uint64_t)t.hour * 3600u + (uint64_t)t.minute * 60u + (uint64_t)t.second;
}

static int screensaver_is_running(void) {
    int file_present = (sys_fs_exists("/tmp/screensaver.active") > 0);
    ntux_task_info_t tasks[64];
    uint64_t count = 0;
    if (sys_task_list(tasks, 64u, &count) != 0) return 0;
    uint64_t limit = count < 64u ? count : 64u;
    int found = 0;
    for (uint64_t i = 0; i < limit; ++i) {
        if (tasks[i].state == NTUX_THREAD_TERMINATED) continue;
        if (strcmp(tasks[i].name, "screensaver") == 0) { found = 1; break; }
        if (strcmp(tasks[i].name, "screensaver.elf") == 0) { found = 1; break; }
    }
    if (!found && file_present) {
        (void)sys_fs_remove("/tmp/screensaver.active");
    }
    return (found || file_present) ? 1 : 0;
}

static void screensaver_try_start(uint64_t now) {
    if (!g_screensaver_enabled) return;
    if (g_screensaver_active) return;
    if (g_power_action != 0) return;
    if (g_boot_splash_until != 0 && sys_get_ticks() < g_boot_splash_until) return;
    if (g_idle_grace_until != 0 && now < g_idle_grace_until) return;
    if (g_idle_timeout_ticks == 0) g_idle_timeout_ticks = 300u;
    if (g_last_input_tick == 0) g_last_input_tick = now;
    if (now - g_last_input_tick < g_idle_timeout_ticks) return;
    long tid = sys_task_add_module("screensaver");
    if (tid < 0) tid = desktop_launch_by_basename_tid("screensaver.elf");
    if (tid >= 0) {
        g_screensaver_active = 1;
        g_last_input_tick = now;
        g_desktop_dirty = 1;
    }
}

static void desktop_logout(void) {
    long tid = sys_task_add_module("login");
    if (tid < 0) tid = desktop_launch_by_basename_tid("login.elf");
    if (tid < 0) {
        notify_push("Logout", "Failed to start login");
        return;
    }
    sys_exit(0);
}

static int poll_special_press(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    if (pressed) desktop_mark_input();
    return pressed;
}

static void handle_super_key(void) {
    if (g_namebox_active) return;
    int super_evt = (sys_kbd_consume_super_press() > 0) ? 1 : 0;
    int left_super = poll_special_press(0x5B);
    int right_super = poll_special_press(0x5C);
    int menu_key = poll_special_press(0x5D);
    int ctrl = (sys_kbd_is_pressed(0x1D) > 0) ? 1 : 0;
    int esc_now = (sys_kbd_is_pressed(0x01) > 0) ? 1 : 0;
    int ctrl_esc = (ctrl && esc_now && !g_hotkey_esc_last) ? 1 : 0;
    g_hotkey_esc_last = (uint8_t)esc_now;
    if (super_evt || left_super || right_super || menu_key || ctrl_esc) {
        g_start_open = g_start_open ? 0u : 1u;
        if (g_start_open) {
            g_start_sel = 0;
            start_menu_clear_query();
        }
    }
}

static void handle_terminal_input(void) {
    desk_term_state_t* ts;
    if (g_namebox_active) return;
    if (g_term_passthrough) return;
    if (g_start_open) return;
    if (g_focus_index < 0 || g_focus_index >= g_window_count) return;
    if (!g_windows[g_focus_index].terminal) return;
    ts = term_state_for_window(&g_windows[g_focus_index]);
    if (!ts) return;

    char c = poll_char();
    if (c >= 32 && c < 127 && ts->input_len + 1 < (int)sizeof(ts->input)) {
        ts->input[ts->input_len++] = c;
        ts->input[ts->input_len] = '\0';
        g_desktop_dirty = 1;
    }
    if (poll_special_press(0x0E) || c == '\b' || c == 127) {
        if (ts->input_len > 0) ts->input[--ts->input_len] = '\0';
        g_desktop_dirty = 1;
    }
    if (poll_special_press(0x1C) || c == '\n' || c == '\r') {
        term_run_command();
        ts->input_len = 0;
        ts->input[0] = '\0';
        g_desktop_dirty = 1;
    }
}

static void handle_global_hotkeys(void) {
    if (g_namebox_active) {
        g_hotkey_f4_last = (uint8_t)(sys_kbd_is_pressed(0x3E) > 0);
        g_hotkey_f11_last = (uint8_t)(sys_kbd_is_pressed(0x57) > 0);
        g_hotkey_t_last = (uint8_t)(sys_kbd_is_pressed(0x14) > 0);
        g_hotkey_tab_last = (uint8_t)(sys_kbd_is_pressed(0x0F) > 0);
        return;
    }
    int ctrl = (sys_kbd_is_pressed(0x1D) > 0) ? 1 : 0;
    int alt = (sys_kbd_is_pressed(0x38) > 0) ? 1 : 0;
    int tab_now = (sys_kbd_is_pressed(0x0F) > 0) ? 1 : 0;
    int tnow = (sys_kbd_is_pressed(0x14) > 0) ? 1 : 0;
    int wnow = (sys_kbd_is_pressed(0x11) > 0) ? 1 : 0;
    int f4_now = (sys_kbd_is_pressed(0x3E) > 0) ? 1 : 0;
    int f11_now = (sys_kbd_is_pressed(0x57) > 0) ? 1 : 0;
    if (ctrl && alt && wnow && !g_hotkey_w_last) {
        g_gallery_open = g_gallery_open ? 0 : 1;
        if (g_gallery_open) {
            wallpaper_scan_all();
            g_gallery_sel = 0;
        }
        g_desktop_dirty = 1;
    }
    if (alt && tab_now && !g_hotkey_tab_last) {
        int start = (g_alt_tab_active && g_alt_tab_choice >= 0) ? g_alt_tab_choice : g_focus_index;
        int found = -1;
        for (int step = 1; step <= g_window_count; ++step) {
            int idx = start + step;
            if (idx >= g_window_count) idx %= g_window_count;
            if (idx < 0 || idx >= g_window_count) continue;
            if (!g_windows[idx].visible || g_windows[idx].minimized) continue;
            found = idx;
            break;
        }
        if (found >= 0) {
            g_alt_tab_active = 1;
            g_alt_tab_choice = found;
        }
    }
    if (!alt && g_alt_tab_active) {
        if (g_alt_tab_choice >= 0 && g_alt_tab_choice < g_window_count) {
            window_bring_to_front(g_alt_tab_choice);
        }
        g_alt_tab_active = 0;
        g_alt_tab_choice = -1;
    }
    if (ctrl && alt && tnow && !g_hotkey_t_last) {
        g_term_passthrough = 0;
        uint64_t hz = (uint64_t)sys_get_timer_hz();
        if (hz == 0) hz = 200u;
        g_suppress_clock_until = sys_get_ticks() + (hz / 2u);
        open_console_window();
    }
    if (ctrl && g_focus_index >= 0 && g_focus_index < g_window_count && g_windows[g_focus_index].file_browser) {
        desk_browser_state_t* st = window_browser_state(&g_windows[g_focus_index]);
        if (poll_special_press(0x1E)) { /* A */
            browser_mark_all(st);
            g_desktop_dirty = 1;
        } else if (poll_special_press(0x2E)) { /* C */
            browser_clipboard_set(st, 0);
        } else if (poll_special_press(0x2F)) { /* V */
            if (browser_clipboard_paste(st) == 0) browser_refresh(st);
        }
    }
    if (g_msgbox_active) {
        if (poll_special_press(0x1C)) {
            if (g_msgbox_owner_tid >= 0) (void)sys_dialog_push(g_msgbox_owner_tid, 1u, "");
            g_msgbox_active = 0;
        } else if (poll_special_press(0x01)) {
            if (g_msgbox_owner_tid >= 0) (void)sys_dialog_push(g_msgbox_owner_tid, 2u, "");
            g_msgbox_active = 0;
        }
        g_hotkey_f4_last = (uint8_t)f4_now;
        g_hotkey_f11_last = (uint8_t)f11_now;
        g_hotkey_t_last = (uint8_t)tnow;
        g_hotkey_tab_last = (uint8_t)tab_now;
        return;
    }
    if (g_picker_active) {
        if (poll_special_press(0x48)) {
            if (g_picker_selected > 0) g_picker_selected--;
            if (g_picker_selected < g_picker_scroll) g_picker_scroll = g_picker_selected;
        } else if (poll_special_press(0x50)) {
            if (g_picker_selected < g_picker_count - 1) g_picker_selected++;
            int visible = (340 - 120) / 18;
            if (g_picker_selected >= g_picker_scroll + visible) g_picker_scroll = g_picker_selected - visible + 1;
        } else if (poll_special_press(0x0E)) {
            picker_go_up();
        } else if (poll_special_press(0x1C)) {
            if (g_picker_selected >= 0 && g_picker_selected < g_picker_count) {
                if (g_picker_ents[g_picker_selected].is_dir) {
                    picker_enter_entry(g_picker_selected);
                } else {
                    char full[256];
                    picker_path_join(g_picker_path, g_picker_ents[g_picker_selected].name, full, sizeof(full));
                    picker_finish(1u, full);
                }
            }
        } else if (poll_special_press(0x01)) {
            picker_finish(0u, "");
        }
        g_hotkey_f4_last = (uint8_t)f4_now;
        g_hotkey_f11_last = (uint8_t)f11_now;
        g_hotkey_t_last = (uint8_t)tnow;
        g_hotkey_tab_last = (uint8_t)tab_now;
        return;
    }
    if (alt && f4_now && !g_hotkey_f4_last) {
        if (g_focus_index >= 0 && g_focus_index < g_window_count) {
            desk_window_t* w = &g_windows[g_focus_index];
            if (w->owner_tid >= 0) {
                desktop_request_close(g_focus_index);
            } else {
                desktop_window_cleanup(g_focus_index, 0);
                update_focus_after_visibility_change();
            }
        }
    }
    if (f11_now && !g_hotkey_f11_last) {
        if (g_focus_index >= 0 && g_focus_index < g_window_count) {
            desk_window_t* w = &g_windows[g_focus_index];
            desk_window_toggle_maximize(w, (int)g_fb.width, (int)g_fb.height);
        }
    }
    if (g_start_open) {
        char c = poll_char();
        if (c >= 32 && c < 127) {
            if (g_start_query_len + 1 < (int)sizeof(g_start_query)) {
                g_start_query[g_start_query_len++] = c;
                g_start_query[g_start_query_len] = '\0';
                g_start_sel = 0;
                g_start_scroll = 0;
            }
        } else if (c == '\b' || c == 127) {
            if (g_start_query_len > 0) {
                g_start_query[--g_start_query_len] = '\0';
                g_start_sel = 0;
                g_start_scroll = 0;
            }
        }
        int count = start_menu_visible_count();
        if (poll_special_press(0x48)) {
            if (count > 0) g_start_sel = (g_start_sel - 1 + count) % count;
        } else if (poll_special_press(0x50)) {
            if (count > 0) g_start_sel = (g_start_sel + 1) % count;
        } else if (poll_special_press(0x1C) || c == '\n') {
            int idx = start_menu_index_by_visible(g_start_sel);
            if (idx >= 0) {
                if (strcmp(g_icons[idx].exec_path, "::explorer") == 0) open_explorer_window();
                else if (strcmp(g_icons[idx].exec_path, "::console") == 0) open_console_window();
                else if (g_icons[idx].exec_path[0]) (void)desktop_launch_target(g_icons[idx].exec_path);
            }
            g_start_open = 0;
        } else if (poll_special_press(0x01)) {
            g_start_open = 0;
        }
    } else if (!alt && tab_now && !g_hotkey_tab_last) {
        if (g_window_count > 0) {
            int next = g_focus_index + 1;
            if (next >= g_window_count) next = 0;
            window_bring_to_front(next);
        } else {
            g_start_open = 1;
            g_start_sel = 0;
            start_menu_clear_query();
        }
    }
    g_hotkey_f4_last = (uint8_t)f4_now;
    g_hotkey_f11_last = (uint8_t)f11_now;
    g_hotkey_t_last = (uint8_t)tnow;
    g_hotkey_tab_last = (uint8_t)tab_now;
    g_hotkey_w_last = (uint8_t)wnow;
}

static void draw_alt_tab_overlay(void) {
    const desk_theme_t* th = desk_theme();
    if (!g_alt_tab_active || g_window_count <= 0) return;
    int pw = 420;
    int ph = 170;
    int px = (int)g_fb.width / 2 - pw / 2;
    int py = (int)g_fb.height / 2 - ph / 2;
    fill_round_rect(px, py, pw, ph, 8, color_lerp(th->taskbar_bg, 0xFF000000u, 25u));
    draw_round_rect(px, py, pw, ph, 8, th->accent);
    draw_text(px + 14, py + 12, "Alt+Tab - Program Switcher", th->text_main);
    int y = py + 40;
    int shown = 0;
    for (int i = 0; i < g_window_count && shown < 6; ++i) {
        int idx = g_window_count - 1 - i;
        if (!g_windows[idx].visible || g_windows[idx].minimized) continue;
        uint32_t rowc = (idx == g_alt_tab_choice) ? th->accent : th->title_blur;
        uint32_t textc = (idx == g_alt_tab_choice) ? 0xFF091018u : th->text_main;
        fill_rect(px + 14, y, pw - 28, 18, rowc);
        draw_round_rect(px + 14, y, pw - 28, 18, 6, th->taskbar_border);
        draw_text(px + 22, y + 5, g_windows[idx].title[0] ? g_windows[idx].title : "Window", textc);
        y += 22;
        shown++;
    }
}

static void draw_start_menu(void) {
    const desk_theme_t* th = desk_theme();
    if (g_start_anim <= 0.01f) return;
    int pad = 12;
    int bar_h = 28;
    int bar_y = (int)g_fb.height - bar_h - 8;
    int sh = bar_y - 10;
    int sx = pad;
    int sy = bar_y - sh;
    int shown = 0;
    float anim = ease_smooth(g_start_anim);
    int slide = (int)((1.0f - anim) * 32.0f);
    sy += slide;
    int header_h = 54;
    int search_h = 28;
    int row_h = 28;
    int quick_w = 90;
    int quick_h = 20;
    int quick_y = sy + 30;
    int quick_x2 = sx + DESK_START_W - 12 - quick_w;
    int quick_x1 = quick_x2 - 8 - quick_w;
    int list_y = sy + header_h + search_h + 18;
    int list_x = sx + 12;
    int list_w = DESK_START_W - 24;
    int list_h = (sy + sh - 54) - list_y;
    if (list_h < 60) list_h = 60;

    uint32_t base = color_lerp(th->start_bg, th->wall_b, 40u);
    fill_round_rect(sx, sy, DESK_START_W, sh, 12, base);
    draw_round_rect(sx, sy, DESK_START_W, sh, 12, th->taskbar_border);
    fill_rect(sx + 1, sy + 1, DESK_START_W - 2, header_h, th->title_focus);
    draw_text(sx + 18, sy + 14, "Start", th->text_main);
    draw_text(sx + DESK_START_W - 110, sy + 14, g_current_user, th->text_dim);
    fill_round_rect(quick_x1, quick_y, quick_w, quick_h, 6, th->title_blur);
    draw_round_rect(quick_x1, quick_y, quick_w, quick_h, 6, th->taskbar_border);
    draw_text(quick_x1 + 12, quick_y + 6, "Explorer", th->text_main);
    fill_round_rect(quick_x2, quick_y, quick_w, quick_h, 6, th->title_blur);
    draw_round_rect(quick_x2, quick_y, quick_w, quick_h, 6, th->taskbar_border);
    draw_text(quick_x2 + 12, quick_y + 6, "Konsole", th->text_main);

    uint64_t tk = sys_get_ticks();
    uint32_t pulse = (uint32_t)((tk * 3u) % 120u);
    uint32_t glow = (pulse < 60u) ? (pulse * 3u) : ((120u - pulse) * 3u);
    uint32_t glowc = color_lerp(th->accent, th->title_blur, glow);
    fill_round_rect(sx + 12, sy + header_h + 8, DESK_START_W - 24, search_h, 8, th->title_blur);
    draw_round_rect(sx + 12, sy + header_h + 8, DESK_START_W - 24, search_h, 8, th->taskbar_border);
    draw_round_rect(sx + 10, sy + header_h + 6, DESK_START_W - 20, search_h + 4, 9, glowc);
    draw_text(sx + 20, sy + header_h + 16, g_start_query_len ? g_start_query : "Search apps...",
              g_start_query_len ? th->text_main : th->text_dim);

    int content_h = start_menu_content_height();
    int max_scroll = content_h > list_h ? (content_h - list_h) : 0;
    if (g_start_scroll < 0) g_start_scroll = 0;
    if (g_start_scroll > max_scroll) g_start_scroll = max_scroll;
    int sel_y = start_menu_item_y_by_visible(g_start_sel);
    if (sel_y < g_start_scroll) g_start_scroll = sel_y;
    if (sel_y + row_h > g_start_scroll + list_h) g_start_scroll = sel_y + row_h - list_h;

    int yy = list_y - g_start_scroll;
    const int cat_header_h = 18;
    for (int cat = 0; cat < 5; ++cat) {
        int count = 0;
        for (int i = 0; i < g_icon_count; ++i) {
            if (!start_menu_match_icon(i)) continue;
            if (start_menu_category_id(&g_icons[i]) != cat) continue;
            count++;
        }
        if (count == 0) continue;
        if (yy + cat_header_h > list_y && yy < list_y + list_h) {
            draw_text(list_x + 6, yy + 12, start_menu_category_name(cat), th->text_dim);
        }
        yy += cat_header_h;
        for (int i = 0; i < g_icon_count; ++i) {
            int y;
            if (!start_menu_match_icon(i)) continue;
            if (start_menu_category_id(&g_icons[i]) != cat) continue;
            if (shown >= DESK_START_MAX_ITEMS) break;
            y = yy;
            if (y + row_h > list_y && y < list_y + list_h) {
                if (shown == g_start_sel) {
                    fill_round_rect(list_x, y, list_w, row_h - 4, 8, th->accent);
                } else {
                    fill_round_rect(list_x, y, list_w, row_h - 4, 8, th->start_item);
                }
                draw_round_rect(list_x, y, list_w, row_h - 4, 8, th->taskbar_border);
                if (g_icons[i].icon_ready && g_icons[i].icon_pixels && g_icons[i].icon_w && g_icons[i].icon_h) {
                    int ix = list_x + 8;
                    int iy = y + 4;
                    int iw = 20;
                    int ih = 20;
                    draw_icon_pixels(g_icons[i].icon_pixels, (int)g_icons[i].icon_w, (int)g_icons[i].icon_h, ix, iy, iw, ih);
                } else if (g_icons[i].preview_ready && g_icons[i].preview && g_icons[i].preview_w && g_icons[i].preview_h) {
                    int ix = list_x + 8;
                    int iy = y + 4;
                    int iw = 20;
                    int ih = 20;
                    icon_draw_preview(&g_icons[i], ix - 14, iy - 12, 0, 0, iw, ih);
                } else {
                    int ix = list_x + 8;
                    int iy = y + 6;
                    fill_round_rect(ix, iy, 16, 16, 4, color_lerp(th->accent, th->wall_c, 80u));
                    draw_rect(ix + 4, iy + 4, 8, 8, th->title_blur);
                }
                draw_text(list_x + 30, y + 8, g_icons[i].label[0] ? g_icons[i].label : g_icons[i].exec_path,
                          (shown == g_start_sel) ? 0xFF091018u : th->text_main);
            }
            shown++;
            yy += row_h;
        }
        if (shown >= DESK_START_MAX_ITEMS) break;
    }

    int btn_w = (DESK_START_W - 40) / 3;
    int gap = 6;
    int by = sy + sh - 46;
    int bx = sx + 12;
    fill_round_rect(bx, by, btn_w, 24, 8, 0xFF2E6A8Fu);
    draw_round_rect(bx, by, btn_w, 24, 8, 0xFF7BC3FFu);
    draw_text(bx + 14, by + 8, "Reboot", 0xFFEAF4FFu);
    fill_round_rect(bx + btn_w + gap, by, btn_w, 24, 8, 0xFF2A2B2Fu);
    draw_round_rect(bx + btn_w + gap, by, btn_w, 24, 8, 0xFF7D7D7Du);
    draw_text(bx + btn_w + gap + 12, by + 8, "Logout", 0xFFEAF4FFu);
    fill_round_rect(bx + 2 * (btn_w + gap), by, btn_w, 24, 8, 0xFF7A2A3Au);
    draw_round_rect(bx + 2 * (btn_w + gap), by, btn_w, 24, 8, 0xFFDB6B82u);
    draw_text(bx + 2 * (btn_w + gap) + 6, by + 8, "Shutdown", 0xFFFFEAF0u);
}

static void wallpaper_scan_dir(const char* root, int depth) {
    if (!root || !root[0] || depth > 4 || g_wallpaper_count >= DESK_LS_MAX) return;
    ntux_dirent_t ents[DESK_LS_MAX];
    uint64_t count = 0;
    if (sys_fs_list_dir(root, ents, DESK_LS_MAX, &count) != 0) return;
    if (count > DESK_LS_MAX) count = DESK_LS_MAX;
    for (uint64_t i = 0; i < count && g_wallpaper_count < DESK_LS_MAX; ++i) {
        if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
        char full[256];
        if (join_path(root, ents[i].name, full, sizeof(full)) != 0) continue;
        if (ents[i].is_dir) {
            wallpaper_scan_dir(full, depth + 1);
        } else if (path_is_image_ext(ents[i].name)) {
            strncpy(g_wallpaper_paths[g_wallpaper_count], full, sizeof(g_wallpaper_paths[g_wallpaper_count]) - 1);
            g_wallpaper_paths[g_wallpaper_count][sizeof(g_wallpaper_paths[g_wallpaper_count]) - 1] = '\0';
            g_wallpaper_count++;
        }
        if ((i & 7u) == 0) sys_yield();
    }
}

static void wallpaper_scan_all(void) {
    g_wallpaper_count = 0;
    for (int i = 0; i < DESK_LS_MAX; ++i) g_wallpaper_paths[i][0] = '\0';
    wallpaper_thumbs_clear();
    wallpaper_scan_dir("/boot", 0);
    wallpaper_scan_dir("/mnt", 0);
    wallpaper_scan_dir("/cd", 0);
    wallpaper_scan_dir("/cdrom", 0);
    wallpaper_scan_dir("/iso", 0);
    wallpaper_scan_dir("/iso0", 0);
    if (g_gallery_sel >= g_wallpaper_count) g_gallery_sel = g_wallpaper_count > 0 ? 0 : -1;
}

static void wallpaper_thumbs_clear(void) {
    for (int i = 0; i < DESK_LS_MAX; ++i) {
        if (g_wallpaper_thumbs[i].pixels) {
            free(g_wallpaper_thumbs[i].pixels);
            g_wallpaper_thumbs[i].pixels = 0;
        }
        g_wallpaper_thumbs[i].w = 0;
        g_wallpaper_thumbs[i].h = 0;
        g_wallpaper_thumbs[i].ready = 0;
        g_wallpaper_thumbs[i].failed = 0;
        g_wallpaper_thumbs[i].loading = 0;
        g_wallpaper_thumbs[i].path[0] = '\0';
    }
}

static void wallpaper_thumb_try_load(int idx, int max_w, int max_h) {
    if (idx < 0 || idx >= g_wallpaper_count) return;
    desk_wall_thumb_t* t = &g_wallpaper_thumbs[idx];
    if (t->ready || t->failed || t->loading) return;
    const char* path = g_wallpaper_paths[idx];
    if (!path || !path[0]) { t->failed = 1; return; }
    t->loading = 1;
    img_job_enqueue(IMG_JOB_WALL_THUMB, idx, path, max_w, max_h, 0);
}

static int gallery_image_count(void) {
    return g_wallpaper_count;
}

static int gallery_index_to_icon(int idx) {
    if (idx < 0 || idx >= g_wallpaper_count) return -1;
    return idx;
}

static int desktop_menu_item_at(int x, int y) {
    if (!g_ctx_open) return -1;
    const int w = 240;
    const int item_h = 26;
    const int pad = 8;
    const int count = 8;
    if (!in_rect(x, y, g_ctx_x, g_ctx_y, w, pad * 2 + item_h * count)) return -1;
    int rel = y - (g_ctx_y + pad);
    if (rel < 0) return -1;
    int idx = rel / item_h;
    if (idx < 0 || idx >= count) return -1;
    return idx;
}

static int desktop_menu_sub_item_at(int x, int y) {
    if (!g_ctx_sub_open) return -1;
    const int w = 190;
    const int item_h = 24;
    const int pad = 6;
    const int count = 2;
    int sx = g_ctx_x + 240 - 2;
    int sy = g_ctx_y + pad;
    if (!in_rect(x, y, sx, sy, w, pad * 2 + item_h * count)) return -1;
    int rel = y - (sy + pad);
    if (rel < 0) return -1;
    int idx = rel / item_h;
    if (idx < 0 || idx >= count) return -1;
    return idx;
}

static void desktop_menu_open_at(int x, int y) {
    const int w = 240;
    const int item_h = 26;
    const int pad = 8;
    const int count = 8;
    int h = pad * 2 + item_h * count;
    g_ctx_x = x;
    g_ctx_y = y;
    if (g_ctx_x + w > (int)g_fb.width) g_ctx_x = (int)g_fb.width - w - 4;
    if (g_ctx_y + h > (int)g_fb.height - DESK_TASKBAR_H) g_ctx_y = (int)g_fb.height - DESK_TASKBAR_H - h - 4;
    if (g_ctx_x < 4) g_ctx_x = 4;
    if (g_ctx_y < 4) g_ctx_y = 4;
    g_ctx_open = 1;
    g_ctx_hover = -1;
    g_ctx_sub_open = 0;
    g_ctx_sub_hover = -1;
    g_desktop_dirty = 1;
}

static void desktop_menu_close(void) {
    if (!g_ctx_open) return;
    g_ctx_open = 0;
    g_ctx_hover = -1;
    g_ctx_sub_open = 0;
    g_ctx_sub_hover = -1;
    g_desktop_dirty = 1;
}

static void draw_desktop_menu(void) {
    if (!g_ctx_open) return;
    const desk_theme_t* th = desk_theme();
    const int w = 240;
    const int item_h = 26;
    const int pad = 8;
    const char* items[] = {
        "New",
        "Open Terminal",
        "Open Explorer",
        "Wallpaper Gallery",
        "Settings",
        "Refresh Desktop",
        "Reboot",
        "Shutdown"
    };
    int count = (int)(sizeof(items) / sizeof(items[0]));
    int h = pad * 2 + item_h * count;
    uint32_t bg = color_lerp(th->taskbar_bg, th->wall_b, 40u);
    fill_round_rect(g_ctx_x, g_ctx_y, w, h, 8, bg);
    draw_round_rect(g_ctx_x, g_ctx_y, w, h, 8, th->taskbar_border);
    for (int i = 0; i < count; ++i) {
        int y = g_ctx_y + pad + i * item_h;
        int ix = g_ctx_x + 12;
        int iy = y + 6;
        uint32_t ic = color_lerp(th->accent, th->wall_c, 80u);
        if (i == g_ctx_hover) {
            fill_round_rect(g_ctx_x + 6, y, w - 12, item_h - 2, 6, th->accent);
            draw_text(g_ctx_x + 16, y + 8, items[i], 0xFF091018u);
            ic = 0xFF091018u;
            if (i == 0) {
                draw_text(g_ctx_x + w - 18, y + 8, ">", 0xFF091018u);
            }
        } else {
            draw_text(g_ctx_x + 16, y + 8, items[i], th->text_main);
            if (i == 0) {
                draw_text(g_ctx_x + w - 18, y + 8, ">", th->text_dim);
            }
        }
        if (i == 0) {
            fill_round_rect(ix, iy, 10, 10, 2, ic);
        } else if (i == 1) {
            fill_rect(ix, iy + 2, 12, 8, ic);
            draw_rect(ix + 2, iy + 4, 8, 4, th->title_blur);
        } else if (i == 2) {
            fill_rect(ix, iy + 4, 12, 6, ic);
            fill_rect(ix + 2, iy + 2, 6, 4, ic);
        } else if (i == 3) {
            fill_round_rect(ix, iy, 12, 10, 3, ic);
            draw_line(ix + 2, iy + 6, ix + 10, iy + 6, th->title_blur);
        } else if (i == 4) {
            draw_rect(ix, iy, 12, 12, ic);
            draw_rect(ix + 3, iy + 3, 6, 6, th->title_blur);
        } else if (i == 5) {
            draw_line(ix, iy + 8, ix + 10, iy + 2, ic);
            draw_line(ix + 10, iy + 2, ix + 8, iy + 2, ic);
        } else if (i == 6) {
            draw_line(ix + 6, iy, ix + 6, iy + 10, ic);
            draw_line(ix + 3, iy + 2, ix + 9, iy + 2, ic);
        } else if (i == 7) {
            draw_line(ix + 2, iy + 2, ix + 10, iy + 10, ic);
            draw_line(ix + 2, iy + 10, ix + 10, iy + 2, ic);
        }
    }

    if (g_ctx_sub_open) {
        const int sw = 190;
        const int sh = pad * 2 + 24 * 2;
        int sx = g_ctx_x + w - 2;
        int sy = g_ctx_y + pad;
        uint32_t sbg = color_lerp(th->taskbar_bg, th->wall_b, 55u);
        fill_round_rect(sx, sy, sw, sh, 8, sbg);
        draw_round_rect(sx, sy, sw, sh, 8, th->taskbar_border);
        for (int i = 0; i < 2; ++i) {
            int y = sy + pad + i * 24;
            const char* label = (i == 0) ? "Folder" : "Empty File";
            if (i == g_ctx_sub_hover) {
                fill_round_rect(sx + 6, y, sw - 12, 22, 6, th->accent);
                draw_text(sx + 16, y + 6, label, 0xFF091018u);
            } else {
                draw_text(sx + 16, y + 6, label, th->text_main);
            }
        }
    }
}

static void gallery_calc_layout(int* out_px, int* out_py, int* out_pw, int* out_ph) {
    int pw = 560;
    int ph = 300;
    int px = (int)g_fb.width / 2 - pw / 2;
    int base_y = (int)g_fb.height - DESK_TASKBAR_H - 16 - ph;
    int slide = (int)((1.0f - g_gallery_anim) * 40.0f);
    int py = base_y + slide;
    if (out_px) *out_px = px;
    if (out_py) *out_py = py;
    if (out_pw) *out_pw = pw;
    if (out_ph) *out_ph = ph;
}

static void wallpaper_set_and_save(const char* path) {
    if (!path || !path[0]) return;
    wallpaper_queue_render(path);
    snprintf(g_wallpaper_pref, sizeof(g_wallpaper_pref), "img:%s", path);
    g_wallpaper_pref[sizeof(g_wallpaper_pref) - 1] = '\0';
    g_wallpaper_custom = 1;
    (void)desktop_conf_save_layout();
}

static void draw_gallery_panel(void) {
    if (g_gallery_anim <= 0.01f) return;
    const desk_theme_t* th = desk_theme();
    int px, py, pw, ph;
    gallery_calc_layout(&px, &py, &pw, &ph);
    int header_h = 38;
    int grid_x = px + 22;
    int grid_y = py + header_h + 12;
    int cell_w = 128;
    int cell_h = 104;
    int cols = 4;
    int rows = 2;
    int page_size = cols * rows;

    fill_round_rect(px, py, pw, ph, 10, color_lerp(th->taskbar_bg, 0xFF000000u, 30u));
    draw_round_rect(px, py, pw, ph, 10, th->taskbar_border);
    fill_rect(px + 1, py + 1, pw - 2, header_h, th->title_focus);
    draw_text(px + 16, py + 12, "Wallpaper Gallery", th->text_main);

    int total = gallery_image_count();
    int pages = (total + page_size - 1) / page_size;
    if (g_gallery_page < 0) g_gallery_page = 0;
    if (g_gallery_page >= pages && pages > 0) g_gallery_page = pages - 1;

    int start = g_gallery_page * page_size;
    int drawn = 0;
    int loaded_this_frame = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = start + drawn;
            int icon_idx = gallery_index_to_icon(idx);
            int cx = grid_x + c * cell_w;
            int cy = grid_y + r * cell_h;
            uint32_t card = (idx == g_gallery_sel) ? color_lerp(th->accent, th->taskbar_bg, 40u) : th->title_blur;
            fill_round_rect(cx, cy, cell_w - 12, cell_h - 8, 8, card);
            draw_round_rect(cx, cy, cell_w - 12, cell_h - 8, 8, th->taskbar_border);
            if (icon_idx >= 0) {
                const char* path = g_wallpaper_paths[icon_idx];
                const char* name = path;
                for (const char* p = path; *p; ++p) {
                    if (*p == '/') name = p + 1;
                }
                int thumb_w = cell_w - 24;
                int thumb_h = 56;
                int tx = cx + 6;
                int ty = cy + 8;
                if (!g_wallpaper_thumbs[icon_idx].ready && !g_wallpaper_thumbs[icon_idx].failed && loaded_this_frame < 2) {
                    wallpaper_thumb_try_load(icon_idx, thumb_w, thumb_h);
                    loaded_this_frame++;
                }
                if (g_wallpaper_thumbs[icon_idx].ready && g_wallpaper_thumbs[icon_idx].pixels) {
                    int tw = g_wallpaper_thumbs[icon_idx].w;
                    int thh = g_wallpaper_thumbs[icon_idx].h;
                    int px = tx + (thumb_w - tw) / 2;
                    int py = ty + (thumb_h - thh) / 2;
                    for (int yy = 0; yy < thh; ++yy) {
                        const uint8_t* row = g_wallpaper_thumbs[icon_idx].pixels + (size_t)yy * (size_t)tw * 3u;
                        for (int xx = 0; xx < tw; ++xx) {
                            const uint8_t* pxl = row + (size_t)xx * 3u;
                            uint32_t cc = ((uint32_t)pxl[0] << 16) | ((uint32_t)pxl[1] << 8) | (uint32_t)pxl[2];
                            put_px(px + xx, py + yy, cc);
                        }
                    }
                    draw_round_rect(tx, ty, thumb_w, thumb_h, 6, th->taskbar_border);
                } else {
                    fill_round_rect(tx, ty, thumb_w, thumb_h, 6, th->title_blur);
                    draw_round_rect(tx, ty, thumb_w, thumb_h, 6, th->taskbar_border);
                    fill_rect(tx + 8, ty + 10, thumb_w - 16, 6, th->accent);
                    fill_rect(tx + 8, ty + 24, thumb_w - 22, 2, th->taskbar_border);
                }
                draw_text(cx + 8, cy + cell_h - 24, name, th->text_main);
            } else {
                draw_text(cx + 12, cy + 14, "Empty", th->text_dim);
            }
            drawn++;
        }
    }

    int btn_y = py + ph - 36;
    int btn_w = 28;
    int btn_h = 20;
    int prev_x = px + 20;
    int next_x = px + pw - 20 - btn_w;
    fill_round_rect(prev_x, btn_y, btn_w, btn_h, 6, th->title_blur);
    draw_round_rect(prev_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
    draw_text(prev_x + 10, btn_y + 6, "<", th->text_main);
    fill_round_rect(next_x, btn_y, btn_w, btn_h, 6, th->title_blur);
    draw_round_rect(next_x, btn_y, btn_w, btn_h, 6, th->taskbar_border);
    draw_text(next_x + 10, btn_y + 6, ">", th->text_main);
    char page_txt[32];
    snprintf(page_txt, sizeof(page_txt), "%d / %d", pages ? (g_gallery_page + 1) : 0, pages);
    draw_text(px + pw / 2 - 20, btn_y + 6, page_txt, th->text_dim);
}

static int gallery_handle_click(int mx, int my) {
    if (g_gallery_anim <= 0.01f) return 0;
    int px, py, pw, ph;
    gallery_calc_layout(&px, &py, &pw, &ph);
    if (mx < px || mx >= px + pw || my < py || my >= py + ph) return 0;

    int header_h = 38;
    int grid_x = px + 22;
    int grid_y = py + header_h + 12;
    int cell_w = 128;
    int cell_h = 104;
    int cols = 4;
    int rows = 2;
    int page_size = cols * rows;
    int total = gallery_image_count();
    int pages = (total + page_size - 1) / page_size;

    int btn_y = py + ph - 36;
    int btn_w = 28;
    int btn_h = 20;
    int prev_x = px + 20;
    int next_x = px + pw - 20 - btn_w;
    if (mx >= prev_x && mx < prev_x + btn_w && my >= btn_y && my < btn_y + btn_h) {
        if (g_gallery_page > 0) g_gallery_page--;
        return 1;
    }
    if (mx >= next_x && mx < next_x + btn_w && my >= btn_y && my < btn_y + btn_h) {
        if (g_gallery_page + 1 < pages) g_gallery_page++;
        return 1;
    }

    int start = g_gallery_page * page_size;
    int idx = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int cx = grid_x + c * cell_w;
            int cy = grid_y + r * cell_h;
            int rx = cx;
            int ry = cy;
            int rw = cell_w - 12;
            int rh = cell_h - 8;
            if (mx >= rx && mx < rx + rw && my >= ry && my < ry + rh) {
                int icon_idx = gallery_index_to_icon(start + idx);
                if (icon_idx >= 0) {
                    wallpaper_set_and_save(g_wallpaper_paths[icon_idx]);
                    g_gallery_sel = icon_idx;
                    return 1;
                }
            }
            idx++;
        }
    }
    return 1;
}

static int gallery_hit(int mx, int my) {
    if (g_gallery_anim <= 0.01f) return 0;
    int px, py, pw, ph;
    gallery_calc_layout(&px, &py, &pw, &ph);
    if (mx < px || mx >= px + pw || my < py || my >= py + ph) return 0;
    return 1;
}

static void gallery_update(float dt) {
    float target = g_gallery_open ? 1.0f : 0.0f;
    float step = 0.16f * dt;
    if (g_gallery_anim < target) {
        g_gallery_anim += step;
        if (g_gallery_anim > target) g_gallery_anim = target;
        g_desktop_dirty = 1;
    } else if (g_gallery_anim > target) {
        g_gallery_anim -= step;
        if (g_gallery_anim < target) g_gallery_anim = target;
        g_desktop_dirty = 1;
    }
}

static void notify_push(const char* title, const char* body) {
    int slot = -1;
    uint64_t oldest = (uint64_t)-1;
    for (int i = 0; i < DESK_NOTIFY_MAX; ++i) {
        if (!g_notifs[i].active) { slot = i; break; }
        if (g_notifs[i].born < oldest) { oldest = g_notifs[i].born; slot = i; }
    }
    if (slot < 0) return;
    desk_notify_t* n = &g_notifs[slot];
    n->active = 1;
    n->born = sys_get_ticks();
    n->anim = 0.0f;
    strncpy(n->title, title ? title : "Notice", sizeof(n->title) - 1);
    n->title[sizeof(n->title) - 1] = '\0';
    strncpy(n->body, body ? body : "", sizeof(n->body) - 1);
    n->body[sizeof(n->body) - 1] = '\0';
    g_desktop_dirty = 1;
}

void desktop_notify(const char* title, const char* body) {
    notify_push(title, body);
}

static void notify_update(float dt) {
    uint64_t now = sys_get_ticks();
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;
    uint64_t ttl = hz * 6u;
    for (int i = 0; i < DESK_NOTIFY_MAX; ++i) {
        if (!g_notifs[i].active) continue;
        float target = (now - g_notifs[i].born < ttl) ? 1.0f : 0.0f;
        float step = 0.20f * dt;
        float prev = g_notifs[i].anim;
        if (g_notifs[i].anim < target) {
            g_notifs[i].anim += step;
            if (g_notifs[i].anim > target) g_notifs[i].anim = target;
        } else if (g_notifs[i].anim > target) {
            g_notifs[i].anim -= step;
            if (g_notifs[i].anim < target) g_notifs[i].anim = target;
        }
        if (g_notifs[i].anim <= 0.01f && target == 0.0f) {
            g_notifs[i].active = 0;
            g_notifs[i].anim = 0.0f;
        }
        if (g_notifs[i].anim != prev) g_desktop_dirty = 1;
    }
}

static int notify_hit(int mx, int my) {
    const int w = 280;
    const int h = 56;
    int base_x = (int)g_fb.width - w - 24;
    int base_y = (int)g_fb.height - DESK_TASKBAR_H - 18 - h;
    int slot = 0;
    for (int i = 0; i < DESK_NOTIFY_MAX; ++i) {
        if (!g_notifs[i].active || g_notifs[i].anim <= 0.01f) continue;
        int oy = (int)((1.0f - g_notifs[i].anim) * 14.0f);
        int y = base_y - slot * (h + 10) - oy;
        if (mx >= base_x && mx < base_x + w && my >= y && my < y + h) {
            return i;
        }
        slot++;
    }
    return -1;
}

static void draw_notifications(void) {
    const desk_theme_t* th = desk_theme();
    const int w = 280;
    const int h = 56;
    int base_x = (int)g_fb.width - w - 24;
    int base_y = (int)g_fb.height - DESK_TASKBAR_H - 18 - h;
    int slot = 0;
    for (int i = 0; i < DESK_NOTIFY_MAX; ++i) {
        if (!g_notifs[i].active || g_notifs[i].anim <= 0.01f) continue;
        int oy = (int)((1.0f - g_notifs[i].anim) * 14.0f);
        int y = base_y - slot * (h + 10) - oy;
        uint32_t bg = color_lerp(th->taskbar_bg, 0xFF000000u, 35u);
        fill_round_rect(base_x, y, w, h, 8, bg);
        draw_round_rect(base_x, y, w, h, 8, th->taskbar_border);
        fill_rect(base_x + 8, y + 8, 4, h - 16, th->accent);
        draw_text(base_x + 18, y + 10, g_notifs[i].title, th->text_main);
        draw_text(base_x + 18, y + 28, g_notifs[i].body, th->text_dim);
        slot++;
    }
}

static void draw_icons(void) {
    const desk_theme_t* th = desk_theme();
    uint64_t tk = sys_get_ticks();
    int loaded_this_frame = 0;
    for (int i = 0; i < g_icon_count; ++i) {
        if (!g_icons[i].visible) continue;
        if (g_desktop_dir[0]) {
            size_t dl = strlen(g_desktop_dir);
            if (dl > 0) {
                const char* p = g_icons[i].exec_path;
                if (!p[0] || strncmp(p, g_desktop_dir, dl) != 0 || (p[dl] != '/' && p[dl] != '\0')) {
                    continue;
                }
            }
        }
        int x = g_icons[i].x;
        int y = g_icons[i].y;
        int pulse = (int)((tk + (uint64_t)i * 13u) % 30u);
        int oy = (g_anim_level >= 2 && pulse < 6) ? (3 - pulse / 2) : 0;

        if (g_icons[i].is_image && !g_icons[i].preview_ready && !g_icons[i].preview_failed &&
            !g_icons[i].preview_loading && loaded_this_frame < 6) {
            (void)icon_request_preview(i);
            loaded_this_frame++;
        }

        fill_round_rect(x, y - oy, DESK_ICON_W, DESK_ICON_H, 6, color_lerp(th->title_blur, 0xFF000000u, 60u));
        draw_round_rect(x, y - oy, DESK_ICON_W, DESK_ICON_H, 6, th->taskbar_border);
        fill_round_rect(x + 14, y + 12 - oy, 56, 58, 7, th->title_blur);
        draw_round_rect(x + 14, y + 12 - oy, 56, 58, 7, th->accent);
        if (g_icons[i].is_image && g_icons[i].preview_ready) {
            icon_draw_preview(&g_icons[i], x, y - oy, 14, 12, 56, 58);
        } else {
            const char* p = g_icons[i].exec_path;
            const char* base = path_basename_ptr(p);
            if (g_icons[i].is_dir) {
                uint32_t fc = 0xFF2E8FD3u;
                if (text_contains_ci(base, "music")) fc = 0xFF2DB36Fu;
                else if (text_contains_ci(base, "video")) fc = 0xFF2D7AB3u;
                else if (text_contains_ci(base, "download")) fc = 0xFF8A6B2Du;
                else if (text_contains_ci(base, "picture") || text_contains_ci(base, "image")) fc = 0xFF8A2DB3u;
                browser_draw_folder_icon(x + 20, y + 18 - oy, 44, 40, fc, th->taskbar_border);
            } else if (path_is_audio_ext(p)) {
                browser_draw_music_icon(x + 20, y + 18 - oy, 44, 40, 0xFF2DB36Fu, th->taskbar_border);
            } else if (path_is_video_ext(p)) {
                browser_draw_video_icon(x + 20, y + 18 - oy, 44, 40, 0xFF2D7AB3u, th->taskbar_border);
            } else if (path_is_archive_ext(p)) {
                browser_draw_file_icon(x + 20, y + 18 - oy, 44, 40, 0xFF8A6B2Du, th->taskbar_border);
            } else if (path_is_code_ext(p)) {
                browser_draw_file_icon(x + 20, y + 18 - oy, 44, 40, 0xFF3A6A4Du, th->taskbar_border);
            } else if (path_is_image_ext(p)) {
                browser_draw_image_icon(x + 20, y + 18 - oy, 44, 40, 0xFF2D7AB3u, th->taskbar_border);
            } else if (name_is_elf(base)) {
                browser_draw_file_icon(x + 20, y + 18 - oy, 44, 40, 0xFF3A6A7Eu, th->taskbar_border);
            } else if (path_is_text_ext(p)) {
                browser_draw_file_icon(x + 20, y + 18 - oy, 44, 40, 0xFF394861u, th->taskbar_border);
            } else {
                browser_draw_file_icon(x + 20, y + 18 - oy, 44, 40, 0xFF405078u, th->taskbar_border);
            }
        }
        draw_text(x + 8, y + 74 - oy, g_icons[i].label[0] ? g_icons[i].label : "App", th->text_main);
    }
}

static void draw_cursor(void) {
    int x = g_last_mouse_x;
    int y = g_last_mouse_y;
    int glow = g_anim_level > 0 ? (int)(sys_get_ticks() % 2u) : 0;
    cursor_kind_t kind = cursor_get_kind();
    if (kind == CURSOR_RESIZE_NWSE) {
        for (int i = 0; i < 12; ++i) {
            put_px(x + i, y + i, glow ? 0xFFEAF4FFu : 0xFFFFFFFFu);
            if (i > 1 && i < 10) {
                put_px(x + i - 1, y + i, glow ? 0xFFEAF4FFu : 0xFFFFFFFFu);
            }
        }
        put_px(x, y + 2, 0xFFFFFFFFu);
        put_px(x + 2, y, 0xFFFFFFFFu);
        put_px(x + 11, y + 9, 0xFFFFFFFFu);
        put_px(x + 9, y + 11, 0xFFFFFFFFu);
        return;
    }
    if (kind == CURSOR_HAND) {
        fill_rect(x + 3, y + 1, 2, 10, 0xFFFFFFFFu);
        fill_rect(x + 5, y + 3, 2, 8, 0xFFFFFFFFu);
        fill_rect(x + 7, y + 5, 2, 6, 0xFFFFFFFFu);
        fill_rect(x + 1, y + 7, 10, 3, glow ? 0xFFEAF4FFu : 0xFFFFFFFFu);
        draw_rect(x, y, 12, 12, 0xFF000000u);
        return;
    }
    if (kind == CURSOR_MOVE) {
        fill_rect(x + 1, y + 5, 10, 2, 0xFFFFFFFFu);
        fill_rect(x + 5, y + 1, 2, 10, 0xFFFFFFFFu);
        put_px(x + 1, y + 4, 0xFFFFFFFFu);
        put_px(x + 10, y + 4, 0xFFFFFFFFu);
        put_px(x + 1, y + 7, 0xFFFFFFFFu);
        put_px(x + 10, y + 7, 0xFFFFFFFFu);
        put_px(x + 4, y + 1, 0xFFFFFFFFu);
        put_px(x + 7, y + 1, 0xFFFFFFFFu);
        put_px(x + 4, y + 10, glow ? 0xFFEAF4FFu : 0xFFFFFFFFu);
        put_px(x + 7, y + 10, glow ? 0xFFEAF4FFu : 0xFFFFFFFFu);
        return;
    }
    // Windows-like arrow cursor with subtle shadow.
    static const uint16_t arrow_mask[16] = {
        0x0001, 0x0003, 0x0007, 0x000F,
        0x001F, 0x003F, 0x007F, 0x00FF,
        0x01FF, 0x003F, 0x003F, 0x0070,
        0x0070, 0x00E0, 0x00E0, 0x0000
    };
    uint32_t c = 0xFFF2F2F2u;
    uint32_t s = 0xFF121212u;
    for (int row = 0; row < 16; ++row) {
        uint16_t bits = arrow_mask[row];
        for (int col = 0; col < 16; ++col) {
            if (bits & (1u << col)) {
                put_px(x + col + 1, y + row + 1, s);
                put_px(x + col, y + row, c);
            }
        }
    }

    if (g_picker_active || g_msgbox_active || g_start_open) {
        draw_spinner_at(x + 10, y + 10, glow ? 0xFF2F7FD6u : 0xFF1C4D99u);
    }
}

static void draw_text_scaled_palette(int x, int y, const char* text, int scale, uint32_t c0, uint32_t c1) {
    if (!text) return;
    int len = (int)strlen(text);
    if (len <= 0) return;
    int cx = x;
    for (int i = 0; i < len; ++i) {
        char ch[2] = { text[i], '\0' };
        uint32_t t = (uint32_t)((i * 255u) / (uint32_t)(len - 1 > 0 ? (len - 1) : 1));
        uint32_t c = color_lerp(c0, c1, t);
        draw_text_scaled(cx, y, ch, scale, c);
        cx += 8 * scale;
    }
}

static void draw_boot_splash(void) {
    const desk_theme_t* th = desk_theme();
    int w = (int)g_fb.width;
    int h = (int)g_fb.height;
    uint32_t top = color_lerp(th->accent, 0xFF0A1020u, 90u);
    uint32_t bot = color_lerp(th->taskbar_bg, 0xFF000000u, 210u);
    for (int y = 0; y < h; y += 4) {
        uint32_t c = color_lerp(top, bot, (uint32_t)((y * 255u) / (uint32_t)h));
        fill_rect(0, y, w, 4, c);
    }

    const char* title = "NTux-OS";
    int title_scale = 4;
    uint64_t t = sys_get_ticks();
    float pulse = 1.0f + 0.06f * (float)sin((double)t * 0.015);
    int anim_scale = (int)((float)title_scale * pulse);
    if (anim_scale < 2) anim_scale = 2;
    int title_w = (int)strlen(title) * 8 * anim_scale;
    if (title_w > w - 40) {
        anim_scale = 3;
        title_w = (int)strlen(title) * 8 * anim_scale;
    }
    int tx = (w / 2) - title_w / 2;
    int ty = (h / 2) - 78;
    draw_text_scaled_palette(tx, ty, title, anim_scale,
                             color_lerp(th->accent, 0xFFFFFFFFu, 110u),
                             color_lerp(th->accent, 0xFF66CCFFu, 80u));
    const char* phase_txt = "Booting services";
    uint64_t now = sys_get_ticks();
    uint64_t span = (g_boot_splash_until > g_boot_splash_start) ? (g_boot_splash_until - g_boot_splash_start) : 1u;
    uint64_t age = (now > g_boot_splash_start) ? (now - g_boot_splash_start) : 0u;
    uint64_t p = (age * 100u) / span;
    if (p < 34u) phase_txt = "Booting services";
    else if (p < 67u) phase_txt = "Starting desktop shell";
    else phase_txt = "Finalizing session";
    draw_text_scaled(tx + 6, ty + 36, phase_txt, 1, th->text_dim);

    int cx = w / 2;
    int cy = ty + 86;
    draw_boot_progress(cx - 120, cy + 34, 240, 10, 0xFF122030u, th->accent);
}

static void draw_power_splash(void) {
    const desk_theme_t* th = desk_theme();
    int w = (int)g_fb.width;
    int h = (int)g_fb.height;
    uint32_t top = color_lerp(th->accent, 0xFF0A1020u, 90u);
    uint32_t bot = color_lerp(th->taskbar_bg, 0xFF000000u, 210u);
    for (int y = 0; y < h; y += 4) {
        uint32_t c = color_lerp(top, bot, (uint32_t)((y * 255u) / (uint32_t)h));
        fill_rect(0, y, w, 4, c);
    }

    const char* brand = "NTux-OS";
    int brand_scale = 5;
    int brand_w = (int)strlen(brand) * 8 * brand_scale;
    if (brand_w > w - 40) {
        brand_scale = 4;
        brand_w = (int)strlen(brand) * 8 * brand_scale;
    }
    int bx = (w / 2) - brand_w / 2;
    int by = (h / 2) - 108;
    draw_text_scaled_palette(bx, by, brand, brand_scale,
                             color_lerp(th->accent, 0xFFFFFFFFu, 90u),
                             color_lerp(th->accent, 0xFF66CCFFu, 80u));

    const char* title = (g_power_action == 1) ? "Rebooting" : "Shutting down";
    int title_scale = 3;
    int title_w = (int)strlen(title) * 8 * title_scale;
    if (title_w > w - 40) {
        title_scale = 2;
        title_w = (int)strlen(title) * 8 * title_scale;
    }
    int tx = (w / 2) - title_w / 2;
    int ty = by + (brand_scale * 8) + 14;
    draw_text_scaled_palette(tx, ty, title, title_scale,
                             color_lerp(th->accent, 0xFFFFFFFFu, 90u),
                             color_lerp(th->accent, 0xFF66CCFFu, 80u));

    uint64_t now = sys_get_ticks();
    uint64_t left = (g_power_until > now) ? (g_power_until - now) : 0u;
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;
    uint64_t sec = left / hz;
    char line[64];
    snprintf(line, sizeof(line), "Powering off in %llu s", (unsigned long long)sec);
    draw_text_scaled((w / 2) - (int)strlen(line) * 4, ty + 40, line, 1, th->text_dim);

    int cx = w / 2;
    int cy = ty + 86;
    draw_boot_progress(cx - 120, cy + 34, 240, 10, 0xFF122030u, th->accent);
}

static void init_windows(void) {
    g_window_count = 0;
    g_focus_index = -1;
    memset(g_term_routes, 0, sizeof(g_term_routes));
    memset(g_term_states, 0, sizeof(g_term_states));
    for (int i = 0; i < DESK_MAX_WINDOWS; ++i) {
        strncpy(g_term_states[i].cwd, "/", sizeof(g_term_states[i].cwd) - 1);
        g_term_states[i].cwd[sizeof(g_term_states[i].cwd) - 1] = '\0';
    }
    g_term_exec_state = 0;
    memset(g_browser_states, 0, sizeof(g_browser_states));
    g_browser_clip_count = 0;
    g_browser_clip_cut = 0;
    memset(g_browser_clip_paths, 0, sizeof(g_browser_clip_paths));
    strncpy(g_browser_default_path, "/", sizeof(g_browser_default_path) - 1);
    g_browser_default_path[sizeof(g_browser_default_path) - 1] = '\0';
    g_dragging = 0;
    g_drag_index = -1;
    g_icon_dragging = 0;
    g_icon_drag_index = -1;
    g_resizing = 0;
    g_resize_index = -1;
    g_last_icon_click_tick = 0;
    g_last_icon_click_index = -1;
    g_icon_count = 0;
    g_start_open = 0;
    g_hotkey_t_last = 0;
    g_hotkey_f4_last = 0;
    g_hotkey_tab_last = 0;
    g_alt_tab_active = 0;
    g_alt_tab_choice = -1;
    g_start_anim = 0.0f;
    start_menu_clear_query();
    for (int i = 0; i < DESK_NOTIFY_MAX; ++i) {
        g_notifs[i].active = 0;
        g_notifs[i].anim = 0.0f;
        g_notifs[i].title[0] = '\0';
        g_notifs[i].body[0] = '\0';
        g_notifs[i].born = 0;
    }
    g_gallery_open = 0;
    g_gallery_anim = 0.0f;
    g_gallery_page = 0;
    g_gallery_sel = 0;
    wallpaper_thumbs_clear();
}

int desktop_init(void) {
    if (sys_fb_get_info(&g_fb) != 0 || g_fb.width == 0 || g_fb.height == 0) {
        return -1;
    }
    g_pixels = (size_t)g_fb.width * (size_t)g_fb.height;
    g_frame = (uint32_t*)malloc(g_pixels * sizeof(uint32_t));
    if (!g_frame) {
        return -1;
    }
    if (g_fb.width >= 1800u || g_fb.height >= 1200u) g_ui_scale = 2;
    else g_ui_scale = 1;
    setup_desktop_storage();
    bg_gradient();
    {
        uint64_t hz = desktop_get_hz();
        g_boot_splash_start = sys_get_ticks();
        g_boot_splash_until = g_boot_splash_start + hz * 30u;
        g_boot_splash_frames = 0;
        g_storage_rescan_ticks = 0;
        g_last_storage_rescan = sys_get_ticks();
        g_idle_timeout_ticks = 300u;
        g_last_input_tick = desktop_now_seconds();
        if (g_last_input_tick != 0) g_idle_grace_until = g_last_input_tick + 30u;
        g_ticks_advancing = desktop_ticks_are_advancing() ? 1u : 0u;
    }

    (void)users_load_db();
    g_current_uid = sys_get_uid();
    strncpy(g_current_user, "user", sizeof(g_current_user) - 1);
    g_current_user[sizeof(g_current_user) - 1] = '\0';
    if (g_user_count > 0) {
        g_current_uid = g_users[0].uid;
        strncpy(g_current_user, g_users[0].name, sizeof(g_current_user) - 1);
        g_current_user[sizeof(g_current_user) - 1] = '\0';
    }
    char session_user_path[96];
    session_user_path[0] = '\0';
    (void)str_append(session_user_path, sizeof(session_user_path), g_user_store_root);
    (void)str_append(session_user_path, sizeof(session_user_path), "/.ntux/session_user");
    if (sys_fs_exists(session_user_path) > 0) {
        uint64_t ln = 0;
        if (sys_fs_read_file(session_user_path, g_current_user, sizeof(g_current_user) - 1u, &ln) == 0) {
            if (ln >= sizeof(g_current_user)) ln = sizeof(g_current_user) - 1u;
            g_current_user[ln] = '\0';
        }
    }
    init_windows();
    memset(g_icon_custom_pos, 0, sizeof(g_icon_custom_pos));
    seed_filesystem_elf_icons();
    wallpaper_scan_all();
    if (desktop_conf_load_layout() != 0) {
        g_wallpaper_custom = 0;
        desktop_apply_wallpaper_pref();
    }
    desktop_auto_arrange_icons();
    (void)desktop_conf_save_layout();
    return 0;
}

static void handle_namebox_input(void) {
    if (!g_namebox_active) return;
    char c = poll_char();
    if (c >= 32 && c < 127) {
        size_t len = strlen(g_namebox_value);
        if (len + 1 < sizeof(g_namebox_value)) {
            g_namebox_value[len] = c;
            g_namebox_value[len + 1] = '\0';
            g_desktop_dirty = 1;
        }
    }
    if (poll_special_press(0x0E) || c == '\b' || c == 127) {
        size_t len = strlen(g_namebox_value);
        if (len > 0) g_namebox_value[len - 1] = '\0';
        g_desktop_dirty = 1;
    }
    if (poll_special_press(0x1C) || c == '\n' || c == '\r') {
        namebox_confirm();
    } else if (poll_special_press(0x01)) {
        namebox_close();
    }
}

void desktop_run(void) {
    for (;;) {
        uint64_t now = sys_get_ticks();
        uint64_t now_sec = desktop_now_seconds();
        if (now_sec != 0 && (g_last_input_tick == 0 || now_sec < g_last_input_tick)) {
            g_last_input_tick = now_sec;
        }
        if (g_last_frame_ticks == 0) g_last_frame_ticks = now;
        uint64_t dt = now - g_last_frame_ticks;
        if (dt > 12u) dt = 12u;
        g_last_frame_ticks = now;
        if (g_storage_rescan_ticks > 0 && now - g_last_storage_rescan >= g_storage_rescan_ticks) {
            if (sys_fs_rescan() == 0) {
                g_desktop_dirty = 1;
            }
            g_last_storage_rescan = now;
        }
        if (g_screensaver_active) {
            if (!screensaver_is_running()) {
                g_screensaver_active = 0;
                desktop_mark_input();
                {
                    uint64_t now_idle = desktop_now_seconds();
                    if (now_idle != 0) g_idle_grace_until = now_idle + 10u;
                }
                g_desktop_dirty = 1;
            }
            desktop_wait_ticks(2);
            continue;
        }
        float target = g_start_open ? 1.0f : 0.0f;
        float prev_start = g_start_anim;
        float speed = 0.18f * (float)dt;
        g_start_anim += (target - g_start_anim) * speed;
        if (g_start_anim < 0.001f) g_start_anim = 0.0f;
        if (g_start_anim > 0.999f) g_start_anim = 1.0f;
        if (g_start_anim != prev_start) {
            g_desktop_dirty = 1;
        }
        gallery_update((float)dt);
        notify_update((float)dt);
        wallpaper_update((float)dt);
        img_job_pump();
        wallpaper_job_step(24u + (uint32_t)dt * 4u);

        handle_mouse();
        int msg_count = window_ipc_process();
        if (msg_count > 0) g_desktop_dirty = 1;
        desktop_pump_close_requests();
        if (!g_desktop_dirty) {
            for (int i = 0; i < g_window_count; ++i) {
                if (g_windows[i].canvas_dirty || g_windows[i].draw_count > 0) {
                    g_desktop_dirty = 1;
                    break;
                }
            }
        }
        update_focus_after_visibility_change();
        if (!desktop_wants_console_input()) {
            (void)sys_console_release();
        }
        handle_super_key();
        handle_global_hotkeys();
        /* installer handled externally */
        desktop_publish_input_state();
        if (sys_get_ticks() < g_boot_splash_until) {
            draw_wallpaper_base(g_frame);
            draw_boot_splash();
            (void)sys_fb_blit32(g_frame, g_fb.width, g_fb.height, g_fb.width * 4u);
            desktop_wait_ticks(1);
            g_last_redraw_tick = now;
            if (g_boot_splash_frames < 1000000u) g_boot_splash_frames++;
            if (g_boot_splash_frames > 30000u) {
                g_boot_splash_until = 0;
            }
            continue;
        }
        if (g_power_action != 0 && sys_get_ticks() < g_power_until) {
            draw_wallpaper_base(g_frame);
            draw_power_splash();
            (void)sys_fb_blit32(g_frame, g_fb.width, g_fb.height, g_fb.width * 4u);
            desktop_wait_ticks(1);
            g_last_redraw_tick = now;
            continue;
        }
        if (g_power_action != 0 && sys_get_ticks() >= g_power_until) {
            if (g_power_action == 1) (void)sys_reboot();
            else (void)sys_shutdown();
            g_power_action = 0;
        }
        handle_namebox_input();
        handle_terminal_input();
        if (now_sec != 0) {
            screensaver_try_start(now_sec);
        }
        if (g_screensaver_active) {
            desktop_wait_ticks(2);
            continue;
        }
        int animating = 0;
        if (g_start_anim > 0.01f && g_start_anim < 0.99f) animating = 1;
        if (g_anim_level >= 2) animating = 1;
        if (!animating) {
            for (int i = 0; i < g_window_count; ++i) {
                if (g_windows[i].animating) {
                    animating = 1;
                    break;
                }
            }
        }

        if (!g_desktop_dirty && !animating && (now - g_last_redraw_tick) < 3u) {
            desktop_wait_ticks(2);
            continue;
        }

        draw_wallpaper_base(g_frame);
        if (g_anim_level > 0) {
            if (g_anim_level >= 2 || (now & 1u) == 0u) {
                draw_wallpaper_fx();
            }
        }
        draw_icons();
        for (int i = 0; i < g_window_count; ++i) {
            draw_window(&g_windows[i], i == g_focus_index);
        }
        draw_start_menu();
        draw_gallery_panel();
        draw_notifications();
        taskbar_draw();
        draw_desktop_menu();
        draw_alt_tab_overlay();
        draw_message_box();
        draw_namebox();
        draw_file_picker();
        draw_cursor();

        (void)sys_fb_blit32(g_frame, g_fb.width, g_fb.height, g_fb.width * 4u);
        g_last_redraw_tick = now;
        g_desktop_dirty = 0;
        desktop_wait_ticks(1);
    }
}

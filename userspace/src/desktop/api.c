#include "api.h"

#include <string.h>
#include <stdlib.h>
#include <syscall.h>

#include <window_protocol.h>
#include <image.h>

#include "desktop_internal.h"
#include "window_internal.h"


void update_focus_after_visibility_change(void);
void open_explorer_window(void);
void open_console_window(void);
void open_clock_window(void);
void open_settings_window(void);
void term_print_banner(void);
void term_run_command_line(desk_window_t* w, const char* cmdline);
void desktop_rescan_icons(void);
void desktop_open_file_picker(const window_msg_t* msg);
void desktop_open_message_box(const window_msg_t* msg);

static void deskapi_copy_text(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static char deskapi_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static int deskapi_text_contains_ci(const char* hay, const char* needle) {
    if (!needle || !needle[0]) return 1;
    if (!hay) return 0;
    for (size_t i = 0; hay[i]; ++i) {
        size_t j = 0;
        while (needle[j] && hay[i + j] &&
               deskapi_lower(hay[i + j]) == deskapi_lower(needle[j])) {
            ++j;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static const char* deskapi_icon_for_title(const char* title) {
    if (!title || !title[0]) return "/boot/res/icons/app.bmp";
    if (deskapi_text_contains_ci(title, "terminal") || deskapi_text_contains_ci(title, "konsole") ||
        deskapi_text_contains_ci(title, "console")) return "/boot/res/icons/terminal.bmp";
    if (deskapi_text_contains_ci(title, "explorer")) return "/boot/res/icons/explorer.bmp";
    if (deskapi_text_contains_ci(title, "editor")) return "/boot/res/icons/editor.bmp";
    if (deskapi_text_contains_ci(title, "browser")) return "/boot/res/icons/browser.bmp";
    if (deskapi_text_contains_ci(title, "task")) return "/boot/res/icons/taskmanager.bmp";
    if (deskapi_text_contains_ci(title, "bench")) return "/boot/res/icons/bench.bmp";
    if (deskapi_text_contains_ci(title, "benchmark")) return "/boot/res/icons/bench.bmp";
    if (deskapi_text_contains_ci(title, "image")) return "/boot/res/icons/imgview.bmp";
    if (deskapi_text_contains_ci(title, "obj")) return "/boot/res/icons/objview.bmp";
    if (deskapi_text_contains_ci(title, "paint")) return "/boot/res/icons/paint.bmp";
    if (deskapi_text_contains_ci(title, "settings")) return "/boot/res/icons/settings.bmp";
    if (deskapi_text_contains_ci(title, "partition")) return "/boot/res/icons/partutil.bmp";
    if (deskapi_text_contains_ci(title, "doom")) return "/boot/res/icons/doom.bmp";
    if (deskapi_text_contains_ci(title, "tetris")) return "/boot/res/icons/tetris.bmp";
    if (deskapi_text_contains_ci(title, "flappy")) return "/boot/res/icons/flappy.bmp";
    if (deskapi_text_contains_ci(title, "snake")) return "/boot/res/icons/snake.bmp";
    if (deskapi_text_contains_ci(title, "xeyes")) return "/boot/res/icons/xeyes.bmp";
    if (deskapi_text_contains_ci(title, "deskdemo") || deskapi_text_contains_ci(title, "screensaver")) return "/boot/res/icons/deskdemo.bmp";
    if (deskapi_text_contains_ci(title, "login")) return "/boot/res/icons/login.bmp";
    if (deskapi_text_contains_ci(title, "health")) return "/boot/res/icons/healthcheck.bmp";
    if (deskapi_text_contains_ci(title, "clock")) return "/boot/res/icons/clock.bmp";
    if (deskapi_text_contains_ci(title, "installer")) return "/boot/res/icons/installer.bmp";
    if (deskapi_text_contains_ci(title, "deskconsole")) return "/boot/res/icons/deskconsole.bmp";
    if (deskapi_text_contains_ci(title, "epaxfetch")) return "/boot/res/icons/epaxfetch.bmp";
    if (deskapi_text_contains_ci(title, "cpphello")) return "/boot/res/icons/cpphello.bmp";
    if (deskapi_text_contains_ci(title, "hello")) return "/boot/res/icons/hello.bmp";

    if (deskapi_text_contains_ci(title, "vim")) return "/boot/res/icons/vim.bmp";
    if (deskapi_text_contains_ci(title, "lua")) return "/boot/res/icons/lua.bmp";
    if (deskapi_text_contains_ci(title, "tcc") || deskapi_text_contains_ci(title, "tinycc")) return "/boot/res/icons/tcc.bmp";
    return "/boot/res/icons/app.bmp";
}

static void deskapi_clamp_icon(desk_icon_t* icon) {
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

static int deskapi_alloc_window_slot(void) {
    int slot = window_find_recyclable_slot();
    if (slot >= 0) return slot;
    if (g_window_count >= DESK_MAX_WINDOWS) return -1;
    return g_window_count++;
}

static uint64_t g_deskapi_id_nonce = 0x9E3779B97F4A7C15ull;

static uint64_t deskapi_rand_u64(uint64_t salt) {
    uint64_t x = (uint64_t)sys_get_ticks();
    x ^= (uint64_t)(uintptr_t)&x;
    x ^= (uint64_t)g_window_count << 32;
    x ^= salt;
    g_deskapi_id_nonce ^= g_deskapi_id_nonce << 7;
    g_deskapi_id_nonce ^= g_deskapi_id_nonce >> 9;
    g_deskapi_id_nonce ^= g_deskapi_id_nonce << 8;
    x ^= g_deskapi_id_nonce;
    if (x == 0) x = 1;
    return x;
}

static uint64_t deskapi_fallback_id(uint64_t base) {
    return base + (uint64_t)(g_window_count + 1) + (sys_get_ticks() & 0x0FFFu);
}

static uint64_t deskapi_alloc_window_id(uint64_t base, uint64_t preferred) {
    if (preferred != 0 && window_find_by_id(preferred) == 0) return preferred;
    uint64_t prefix = (base & 0xFFFFu) << 48;
    for (int attempt = 0; attempt < 16; ++attempt) {
        uint64_t id = (deskapi_rand_u64(preferred + base + (uint64_t)attempt) & 0x0000FFFFFFFFFFFFull) | prefix;
        if (id == 0) continue;
        if (window_find_by_id(id) == 0) return id;
    }
    uint64_t id = deskapi_fallback_id(base);
    while (id != 0 && window_find_by_id(id) != 0) {
        id += 1u;
    }
    return id ? id : 1u;
}

static int deskapi_create_basic_window(const window_msg_t* msg) {
    int slot = deskapi_alloc_window_slot();
    if (slot < 0) return -1;
    desk_window_t* w = &g_windows[slot];
    memset(w, 0, sizeof(*w));
    w->id = deskapi_alloc_window_id(0xD500u, msg->id);
    w->x = msg->x;
    w->y = msg->y;
    w->canvas_base_w = msg->w > 0 ? msg->w : 360;
    w->canvas_base_h = msg->h > 0 ? msg->h : 240;
    w->w = w->canvas_base_w;
    w->h = w->canvas_base_h;
    if (g_ui_scale > 1) {
        w->w *= g_ui_scale;
        w->h *= g_ui_scale;
    }
    w->bg = msg->color ? msg->color : 0xFF0A121Cu;
    w->canvas_clear = w->bg;
    w->canvas_enabled = 1;
    w->canvas_dirty = 1;
    w->visible = 1;
    w->owner_tid = (msg->owner_tid > 0) ? (int)msg->owner_tid : -1;
    w->birth_tick = sys_get_ticks();
    deskapi_copy_text(w->title, sizeof(w->title), msg->text[0] ? msg->text : "Window");
    desk_window_set_icon(w, deskapi_icon_for_title(w->title));
    window_clamp_rect(w, (int)g_fb.width, (int)g_fb.height);
    g_focus_index = slot;
    window_bring_to_front(slot);
    return 0;
}

static int deskapi_open_terminal(const window_msg_t* msg) {
    int slot = deskapi_alloc_window_slot();
    if (slot < 0) return -1;
    desk_window_t* w = &g_windows[slot];
    memset(w, 0, sizeof(*w));
    w->id = deskapi_alloc_window_id(0xC000u, msg->id);
    w->x = msg->x;
    w->y = msg->y;
    w->w = msg->w > 0 ? msg->w : ((int)g_fb.width > 1100 ? (g_ui_scale > 1 ? 1140 : 980) : (int)g_fb.width - 72);
    w->h = msg->h > 0 ? msg->h : ((int)g_fb.height > 760 ? (g_ui_scale > 1 ? 700 : 620) : (int)g_fb.height - 90);
    w->bg = msg->color ? msg->color : 0xFF0F1824u;
    w->visible = 1;
    w->terminal = 1;
    w->owner_tid = (msg->owner_tid > 0) ? (int)msg->owner_tid : -1;
    w->birth_tick = sys_get_ticks();
    w->term_slot = (uint8_t)slot;
    deskapi_copy_text(w->title, sizeof(w->title), msg->text[0] ? msg->text : "Konsole");
    desk_window_set_icon(w, deskapi_icon_for_title(w->title));
    window_clamp_rect(w, (int)g_fb.width, (int)g_fb.height);
    g_focus_index = slot;
    window_bring_to_front(slot);

    if (slot < 0 || slot >= DESK_MAX_WINDOWS) return -1;
    desk_term_state_t* ts = &g_term_states[slot];
    memset(ts, 0, sizeof(*ts));
    strncpy(ts->cwd, "/", sizeof(ts->cwd) - 1);
    ts->cwd[sizeof(ts->cwd) - 1] = '\0';
    g_term_exec_state = ts;
    term_print_banner();
    g_term_exec_state = 0;
    return 0;
}

static int deskapi_add_icon(const window_msg_t* msg) {
    if (g_icon_count >= DESK_MAX_ICONS) return -1;
    int idx = g_icon_count++;
    desk_icon_t* icon = &g_icons[idx];
    memset(icon, 0, sizeof(*icon));
    icon->id = msg->id ? msg->id : (10000u + (uint64_t)idx);
    icon->x = msg->x;
    icon->y = msg->y;
    icon->visible = 1;
    deskapi_copy_text(icon->label, sizeof(icon->label), msg->text[0] ? msg->text : "App");
    deskapi_copy_text(icon->exec_path, sizeof(icon->exec_path), msg->text2);
    deskapi_clamp_icon(icon);
    return 0;
}

static int deskapi_remove_icon(uint64_t id) {
    for (int i = 0; i < g_icon_count; ++i) {
        if (!g_icons[i].visible) continue;
        if (g_icons[i].id != id) continue;
        g_icons[i].visible = 0;
        g_icons[i].exec_path[0] = '\0';
        return 0;
    }
    return -1;
}

static void deskapi_apply_theme(const char* name) {
    if (!name || !name[0]) return;
    if (strcmp(name, "Ocean") == 0) {
        g_theme_index = 0;
        return;
    }
    g_theme_index = 0;
}

static void deskapi_handle_message(const window_msg_t* msg) {
    if (!msg) return;
    switch ((window_cmd_t)msg->cmd) {
        case WINDOW_CMD_INIT:
            break;
        case WINDOW_CMD_CREATE:
            (void)deskapi_create_basic_window(msg);
            break;
        case WINDOW_CMD_SET_TEXT: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            deskapi_copy_text(w->text, sizeof(w->text), msg->text);
            break;
        }
        case WINDOW_CMD_SET_TITLE: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            deskapi_copy_text(w->title, sizeof(w->title), msg->text);
            break;
        }
        case WINDOW_CMD_SET_RECT: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            uint32_t flags = msg->flags;
            if (flags == 0) flags = WINDOW_RECT_HAS_X | WINDOW_RECT_HAS_Y | WINDOW_RECT_HAS_W | WINDOW_RECT_HAS_H;
            if (flags & WINDOW_RECT_HAS_X) w->x = msg->x;
            if (flags & WINDOW_RECT_HAS_Y) w->y = msg->y;
            if (flags & WINDOW_RECT_HAS_W) {
                w->canvas_base_w = msg->w;
                w->w = (g_ui_scale > 1) ? (msg->w * g_ui_scale) : msg->w;
            }
            if (flags & WINDOW_RECT_HAS_H) {
                w->canvas_base_h = msg->h;
                w->h = (g_ui_scale > 1) ? (msg->h * g_ui_scale) : msg->h;
            }
            window_clamp_rect(w, (int)g_fb.width, (int)g_fb.height);
            break;
        }
        case WINDOW_CMD_SET_BG: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            w->bg = msg->color;
            break;
        }
        case WINDOW_CMD_SET_IMAGE: {
            img_job_enqueue_window_image(msg->id, msg->text, (int)msg->flags);
            break;
        }
        case WINDOW_CMD_SET_ICON: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            desk_window_set_icon(w, msg->text);
            break;
        }
        case WINDOW_CMD_SHOW: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            w->visible = msg->flags ? 1u : 0u;
            if (w->visible) w->minimized = 0;
            update_focus_after_visibility_change();
            break;
        }
        case WINDOW_CMD_FOCUS: {
            int idx = window_index_by_id(msg->id);
            if (idx < 0) break;
            g_windows[idx].visible = 1;
            g_windows[idx].minimized = 0;
            window_bring_to_front(idx);
            break;
        }
        case WINDOW_CMD_MINIMIZE: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            w->minimized = 1;
            update_focus_after_visibility_change();
            break;
        }
        case WINDOW_CMD_TOGGLE_MAXIMIZE: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            desk_window_toggle_maximize(w, (int)g_fb.width, (int)g_fb.height);
            break;
        }
        case WINDOW_CMD_CLOSE: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            int idx = window_index_by_id(msg->id);
            if (idx >= 0) {
                desktop_window_cleanup(idx, 1);
            } else {
                w->visible = 0;
                w->minimized = 0;
            }
            update_focus_after_visibility_change();
            break;
        }
        case WINDOW_CMD_OPEN_TERMINAL:
            (void)deskapi_open_terminal(msg);
            break;
        case WINDOW_CMD_TERMINAL_EXEC: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w || !w->terminal) break;
            term_run_command_line(w, msg->text);
            break;
        }
        case WINDOW_CMD_OPEN_EXPLORER:
            break;
        case WINDOW_CMD_OPEN_CONSOLE:
            open_console_window();
            break;
        case WINDOW_CMD_OPEN_CLOCK:
            open_clock_window();
            break;
        case WINDOW_CMD_OPEN_SETTINGS:
            open_settings_window();
            break;
        case WINDOW_CMD_OPEN_FILE_PICKER:
            desktop_open_file_picker(msg);
            break;
        case WINDOW_CMD_OPEN_MESSAGE_BOX:
            desktop_open_message_box(msg);
            break;
        case WINDOW_CMD_SET_THEME:
            deskapi_apply_theme(msg->text);
            break;
        case WINDOW_CMD_SET_ANIM_LEVEL:
            g_anim_level = (int)msg->x;
            if (g_anim_level < 0) g_anim_level = 0;
            if (g_anim_level > 3) g_anim_level = 3;
            break;
        case WINDOW_CMD_ADD_ICON:
            (void)deskapi_add_icon(msg);
            break;
        case WINDOW_CMD_REMOVE_ICON:
            (void)deskapi_remove_icon(msg->id);
            break;
        case WINDOW_CMD_RESCAN_ICONS:
            desktop_rescan_icons();
            break;
        case WINDOW_CMD_DRAW_CLEAR: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            window_canvas_clear(w, msg->color);
            break;
        }
        case WINDOW_CMD_DRAW_RECT: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            window_canvas_rect(w, msg->x, msg->y, msg->w, msg->h, msg->color, msg->flags ? 1 : 0);
            break;
        }
        case WINDOW_CMD_DRAW_LINE: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            window_canvas_line(w, msg->x, msg->y, msg->w, msg->h, msg->color);
            break;
        }
        case WINDOW_CMD_DRAW_TEXT: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            window_canvas_text(w, msg->x, msg->y, msg->color, msg->text);
            break;
        }
        case WINDOW_CMD_DRAW_PRESENT: {
            desk_window_t* w = window_find_by_id(msg->id);
            if (!w) break;
            window_canvas_present(w);
            break;
        }
        case WINDOW_CMD_TERMINAL_WRITE:
            desk_term_write_for_tid((int)msg->owner_tid, msg->text);
            break;
        case WINDOW_CMD_NOTIFY:
            desktop_notify(msg->text, msg->text2);
            break;
        default:
            break;
    }
}

static void deskapi_handle_batch(const uint8_t* raw, uint64_t len) {
    if (!raw || len < sizeof(window_batch_msg_t)) return;
    window_batch_msg_t header;
    memcpy(&header, raw, sizeof(header));
    if (header.size > len || header.size < sizeof(window_batch_msg_t)) return;
    uint32_t max_ops = (WINDOW_MAX_MSG > sizeof(window_batch_msg_t)) ?
        (uint32_t)((WINDOW_MAX_MSG - sizeof(window_batch_msg_t)) / sizeof(window_draw_op_t)) : 0;
    if (header.count > max_ops) header.count = max_ops;

    size_t ops_bytes = (size_t)header.count * sizeof(window_draw_op_t);
    if (sizeof(window_batch_msg_t) + ops_bytes > header.size) {
        size_t max_ops_in_msg = (header.size - sizeof(window_batch_msg_t)) / sizeof(window_draw_op_t);
        if (max_ops_in_msg > (size_t)max_ops) max_ops_in_msg = (size_t)max_ops;
        header.count = (uint32_t)max_ops_in_msg;
    }

    desk_window_t* w = window_find_by_id(header.id);
    if (!w) return;

    if (header.clear_valid) {
        window_canvas_clear(w, header.clear_color);
    }

    const window_draw_op_t* ops = (const window_draw_op_t*)(raw + sizeof(window_batch_msg_t));
    for (uint32_t i = 0; i < header.count; ++i) {
        const window_draw_op_t* op = &ops[i];
        if (op->type == WINDOW_DRAW_OP_RECT) {
            window_canvas_rect(w, op->x, op->y, op->w, op->h, op->color, op->filled ? 1 : 0);
        } else if (op->type == WINDOW_DRAW_OP_LINE) {
            window_canvas_line(w, op->x, op->y, op->x2, op->y2, op->color);
        } else if (op->type == WINDOW_DRAW_OP_TEXT) {
            window_canvas_text(w, op->x, op->y, op->color, op->text);
        } else if (op->type == WINDOW_DRAW_OP_BUTTON) {
            window_canvas_button(w, op->x, op->y, op->w, op->h, (int)op->filled, op->text);
        }
    }
}

static void deskapi_handle_image_raw(const uint8_t* raw, uint64_t len) {
    if (!raw || len < sizeof(window_image_msg_t)) return;
    window_image_msg_t header;
    memcpy(&header, raw, sizeof(header));
    if (header.size > len || header.size < sizeof(window_image_msg_t)) return;
    if (header.data_len + sizeof(window_image_msg_t) > header.size) return;
    if (header.w == 0 || header.h == 0) return;
    if (header.channels != 3 && header.channels != 4) return;
    uint64_t total = (uint64_t)header.w * (uint64_t)header.h * (uint64_t)header.channels;
    if (total == 0 || total > (128u * 1024u * 1024u)) return;
    if ((uint64_t)header.offset + (uint64_t)header.data_len > total) return;

    desk_window_t* w = window_find_by_id(header.id);
    if (!w) return;
    if ((header.flags & WINDOW_IMAGE_FLAG_ALLOC) || !w->image_data ||
        w->image_w != header.w || w->image_h != header.h || w->image_channels != header.channels) {
        if (w->image_data) free(w->image_data);
        w->image_data = (uint8_t*)malloc((size_t)total);
        if (!w->image_data) {
            w->image_enabled = 0;
            return;
        }
        w->image_w = (uint16_t)header.w;
        w->image_h = (uint16_t)header.h;
        w->image_channels = (uint8_t)header.channels;
        w->image_enabled = 1;
    }

    const uint8_t* src = raw + sizeof(window_image_msg_t);
    memcpy(w->image_data + header.offset, src, header.data_len);
    desktop_mark_dirty();
}

int window_ipc_process(void) {
    uint8_t raw[WINDOW_MAX_MSG + 1];
    uint64_t len = 0;
    int processed = 0;
    while (sys_deskapi_pop((char*)raw, sizeof(raw), &len) == 0) {
        processed++;
        if (len < sizeof(uint32_t) * 2) continue;
        uint32_t cmd = 0;
        uint32_t size = 0;
        memcpy(&cmd, raw, sizeof(cmd));
        memcpy(&size, raw + sizeof(uint32_t), sizeof(size));
        if (cmd == WINDOW_CMD_DRAW_BATCH) {
            deskapi_handle_batch(raw, len);
            continue;
        }
        if (cmd == WINDOW_CMD_SET_IMAGE_RAW) {
            deskapi_handle_image_raw(raw, len);
            continue;
        }
        if (len < sizeof(window_msg_t)) continue;
        window_msg_t msg;
        memcpy(&msg, raw, sizeof(msg));
        if (msg.size != sizeof(msg)) {
            msg.size = sizeof(msg);
        }
        deskapi_handle_message(&msg);
    }
    return processed;
}

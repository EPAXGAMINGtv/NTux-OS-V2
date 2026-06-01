#include <window.h>
#include <window_protocol.h>
#include <syscall.h>

#include <string.h>
#include <stddef.h>

static int window_send_raw(const void* buf, size_t len) {
    if (!buf || len == 0) return -1;
    for (int attempt = 0; attempt < 8; ++attempt) {
        long rc = sys_deskapi_push((const char*)buf, (uint64_t)len);
        if (rc == 0) return 0;
        if (rc == -2) {
            (void)sys_yield();
            continue;
        }
        return -1;
    }
    return -1;
}

static int window_send(const window_msg_t* msg) {
    if (!msg) return -1;
    return window_send_raw(msg, sizeof(*msg));
}

typedef struct {
    window_t id;
    uint32_t clear_color;
    uint32_t clear_valid;
    uint32_t count;
    window_draw_op_t ops[WINDOW_MAX_OPS];
} window_batch_state_t;

static window_batch_state_t g_batch = {0};
static uint8_t g_batch_buf[WINDOW_MAX_MSG];

static uint32_t window_batch_max_ops(void) {
    size_t header = sizeof(window_batch_msg_t);
    size_t op = sizeof(window_draw_op_t);
    if (WINDOW_MAX_MSG <= header) return 0;
    return (uint32_t)((WINDOW_MAX_MSG - header) / op);
}

static void window_batch_reset(void) {
    g_batch.count = 0;
    g_batch.clear_color = 0;
}

static int window_batch_flush(void) {
    if (g_batch.count == 0 && g_batch.clear_valid == 0) return 0;
    size_t header_size = sizeof(window_batch_msg_t);
    uint32_t max_ops = window_batch_max_ops();
    if (g_batch.count > max_ops) g_batch.count = max_ops;
    size_t ops_size = sizeof(window_draw_op_t) * (size_t)g_batch.count;
    size_t total = header_size + ops_size;
    if (total > sizeof(g_batch_buf)) return -1;

    window_batch_msg_t* msg = (window_batch_msg_t*)g_batch_buf;
    msg->cmd = WINDOW_CMD_DRAW_BATCH;
    msg->size = (uint32_t)total;
    msg->id = g_batch.id;
    msg->count = g_batch.count;
    msg->clear_valid = g_batch.clear_valid;
    msg->clear_color = g_batch.clear_color;
    msg->reserved = 0;
    if (g_batch.count > 0) {
        memcpy(msg->ops, g_batch.ops, ops_size);
    }

    window_batch_reset();
    g_batch.clear_valid = 0;
    return window_send_raw(msg, total);
}

static void window_batch_ensure_id(window_t id) {
    if (g_batch.id == 0 || g_batch.id == id) {
        g_batch.id = id;
        return;
    }
    (void)window_batch_flush();
    g_batch.id = id;
}

static void window_batch_push_op(window_t id, const window_draw_op_t* op) {
    if (!op) return;
    window_batch_ensure_id(id);
    uint32_t max_ops = window_batch_max_ops();
    if (max_ops == 0) return;
    if (g_batch.count >= max_ops) {
        (void)window_batch_flush();
        g_batch.id = id;
    }
    if (g_batch.count < max_ops) {
        g_batch.ops[g_batch.count++] = *op;
    }
}

static void window_init_msg(window_msg_t* msg, window_cmd_t cmd) {
    memset(msg, 0, sizeof(*msg));
    msg->cmd = (uint32_t)cmd;
    msg->size = (uint32_t)sizeof(*msg);
    {
        int tid = (int)sys_get_tid();
        if (tid >= 0) msg->owner_tid = (uint32_t)tid;
    }
}

static void window_copy_text(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

int window_init(void) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_INIT);
    return window_send(&msg);
}

int window_create(window_t id, int x, int y, int w, int h, uint32_t color, const char* title) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_CREATE);
    msg.id = id;
    msg.x = x; msg.y = y; msg.w = w; msg.h = h;
    msg.color = color;
    window_copy_text(msg.text, sizeof(msg.text), title);
    return window_send(&msg);
}

int window_close(window_t id) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_CLOSE);
    msg.id = id;
    return window_send(&msg);
}

int window_show(window_t id, int visible) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SHOW);
    msg.id = id;
    msg.flags = visible ? 1u : 0u;
    return window_send(&msg);
}

int window_focus(window_t id) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_FOCUS);
    msg.id = id;
    return window_send(&msg);
}

int window_minimize(window_t id) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_MINIMIZE);
    msg.id = id;
    return window_send(&msg);
}

int window_toggle_maximize(window_t id) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_TOGGLE_MAXIMIZE);
    msg.id = id;
    return window_send(&msg);
}

int window_set_title(window_t id, const char* title) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SET_TITLE);
    msg.id = id;
    window_copy_text(msg.text, sizeof(msg.text), title);
    return window_send(&msg);
}

int window_set_bg(window_t id, uint32_t color) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SET_BG);
    msg.id = id;
    msg.color = color;
    return window_send(&msg);
}

int window_move(window_t id, int x, int y) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SET_RECT);
    msg.id = id;
    msg.x = x; msg.y = y;
    msg.flags = WINDOW_RECT_HAS_X | WINDOW_RECT_HAS_Y;
    return window_send(&msg);
}

int window_resize(window_t id, int w, int h) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SET_RECT);
    msg.id = id;
    msg.w = w; msg.h = h;
    msg.flags = WINDOW_RECT_HAS_W | WINDOW_RECT_HAS_H;
    return window_send(&msg);
}

int window_set_rect(window_t id, int x, int y, int w, int h) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SET_RECT);
    msg.id = id;
    msg.x = x; msg.y = y; msg.w = w; msg.h = h;
    msg.flags = WINDOW_RECT_HAS_X | WINDOW_RECT_HAS_Y | WINDOW_RECT_HAS_W | WINDOW_RECT_HAS_H;
    return window_send(&msg);
}

int window_clear(window_t id, uint32_t color) {
    window_batch_ensure_id(id);
    g_batch.clear_valid = 1u;
    g_batch.clear_color = color;
    g_batch.count = 0;
    return 0;
}

int window_draw_rect(window_t id, int x, int y, int w, int h, uint32_t color, int filled) {
    window_draw_op_t op;
    memset(&op, 0, sizeof(op));
    op.type = WINDOW_DRAW_OP_RECT;
    op.filled = (uint8_t)(filled ? 1 : 0);
    op.x = (int16_t)x;
    op.y = (int16_t)y;
    op.w = (int16_t)w;
    op.h = (int16_t)h;
    op.color = color;
    window_batch_push_op(id, &op);
    return 0;
}

int window_draw_line(window_t id, int x0, int y0, int x1, int y1, uint32_t color) {
    window_draw_op_t op;
    memset(&op, 0, sizeof(op));
    op.type = WINDOW_DRAW_OP_LINE;
    op.x = (int16_t)x0;
    op.y = (int16_t)y0;
    op.x2 = (int16_t)x1;
    op.y2 = (int16_t)y1;
    op.color = color;
    window_batch_push_op(id, &op);
    return 0;
}

int window_draw_text(window_t id, int x, int y, uint32_t color, const char* text) {
    window_draw_op_t op;
    memset(&op, 0, sizeof(op));
    op.type = WINDOW_DRAW_OP_TEXT;
    op.x = (int16_t)x;
    op.y = (int16_t)y;
    op.color = color;
    window_copy_text(op.text, sizeof(op.text), text);
    window_batch_push_op(id, &op);
    return 0;
}

int window_draw_button(window_t id, int x, int y, int w, int h, const char* text, int kind) {
    window_draw_op_t op;
    memset(&op, 0, sizeof(op));
    op.type = WINDOW_DRAW_OP_BUTTON;
    op.filled = (uint8_t)kind;
    op.x = (int16_t)x;
    op.y = (int16_t)y;
    op.w = (int16_t)w;
    op.h = (int16_t)h;
    op.color = 0;
    window_copy_text(op.text, sizeof(op.text), text);
    window_batch_push_op(id, &op);
    return 0;
}

int window_draw_scrollbar(window_t id, int x, int y, int w, int h, int content, int view, int scroll, int horizontal) {
    if (w <= 0 || h <= 0) return -1;
    if (content <= 0) content = 1;
    if (view <= 0) view = 1;
    if (scroll < 0) scroll = 0;
    if (scroll > content - view) scroll = (content > view) ? (content - view) : 0;

    uint32_t track = 0xFF0F1720u;
    uint32_t border = 0xFF2A3B52u;
    uint32_t thumb = 0xFF3C556Fu;

    window_draw_rect(id, x, y, w, h, track, 1);
    window_draw_rect(id, x, y, w, h, border, 0);

    if (content <= view) {
        return 0;
    }

    int track_len = horizontal ? w : h;
    int thumb_len = (view * track_len) / content;
    if (thumb_len < 16) thumb_len = 16;
    if (thumb_len > track_len) thumb_len = track_len;

    int max_scroll = content - view;
    int max_pos = track_len - thumb_len;
    int pos = (max_scroll > 0) ? (scroll * max_pos) / max_scroll : 0;

    if (horizontal) {
        window_draw_rect(id, x + pos, y + 2, thumb_len, h - 4, thumb, 1);
    } else {
        window_draw_rect(id, x + 2, y + pos, w - 4, thumb_len, thumb, 1);
    }
    return 0;
}

int window_draw_dropdown(window_t id, int x, int y, int w, int h, const char* label, const char* value, int open, int focused) {
    uint32_t bg = open ? 0xFF182230u : 0xFF121A26u;
    uint32_t border = focused ? 0xFF4BB2FFu : 0xFF2A3B52u;
    uint32_t text = 0xFFE8F2FFu;
    if (w <= 0 || h <= 0) return -1;

    window_draw_rect(id, x, y, w, h, bg, 1);
    window_draw_rect(id, x, y, w, h, border, 0);
    if (label && label[0]) {
        window_draw_text(id, x + 10, y - 16, 0xFF9BB6D0u, label);
    }
    if (value && value[0]) {
        window_draw_text(id, x + 10, y + (h / 2) - 6, text, value);
    }
    int cx = x + w - 18;
    int cy = y + (h / 2) - 2;
    uint32_t caret = 0xFF9BB6D0u;
    window_draw_line(id, cx - 5, cy, cx, cy + 5, caret);
    window_draw_line(id, cx, cy + 5, cx + 5, cy, caret);
    return 0;
}

int window_draw_dropdown_list(window_t id, int x, int y, int w, int item_h, int count, int sel, int scroll, int max_visible,
                              const char* (*item_at)(int)) {
    if (w <= 0 || item_h <= 0 || count <= 0 || !item_at) return -1;
    if (scroll < 0) scroll = 0;
    if (max_visible < 1) max_visible = 1;
    int visible = count - scroll;
    if (visible > max_visible) visible = max_visible;
    if (visible < 1) visible = 1;
    int h = visible * item_h + 8;
    uint32_t bg = 0xFF0F1622u;
    uint32_t border = 0xFF2A3B52u;
    uint32_t sel_bg = 0xFF234663u;
    uint32_t text = 0xFFB7CBE0u;
    uint32_t text_sel = 0xFFF0FAFFu;

    window_draw_rect(id, x, y, w, h, bg, 1);
    window_draw_rect(id, x, y, w, h, border, 0);
    for (int i = 0; i < visible; ++i) {
        int idx = scroll + i;
        int ry = y + 4 + i * item_h;
        uint32_t row_bg = (idx == sel) ? sel_bg : bg;
        uint32_t row_fg = (idx == sel) ? text_sel : text;
        window_draw_rect(id, x + 4, ry, w - 8, item_h - 2, row_bg, 1);
        window_draw_text(id, x + 12, ry + (item_h / 2) - 6, row_fg, item_at(idx));
    }
    if (count > visible) {
        window_draw_scrollbar(id, x + w - 10, y + 4, 6, h - 8, count, visible, scroll, 0);
    }
    return 0;
}

static uint64_t g_present_last_yield = 0;

int window_present(window_t id) {
    window_batch_ensure_id(id);
    int rc = window_batch_flush();
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_DRAW_PRESENT);
    msg.id = id;
    if (rc != 0) return rc;
    rc = window_send(&msg);
    /* Yield at most once per tick to avoid over-scheduling overhead. */
    uint64_t now = sys_get_ticks();
    if (now != g_present_last_yield) {
        g_present_last_yield = now;
        (void)sys_yield();
    }
    return rc;
}

int window_set_image(window_t id, const char* path, int desired_channels) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SET_IMAGE);
    msg.id = id;
    msg.flags = (desired_channels == 3 || desired_channels == 4) ? (uint32_t)desired_channels : 0u;
    window_copy_text(msg.text, sizeof(msg.text), path ? path : "");
    return window_send(&msg);
}

int window_set_image_raw(window_t id, int w, int h, int channels, const void* data, uint32_t len) {
    if (!data || len == 0 || w <= 0 || h <= 0) return -1;
    size_t header = sizeof(window_image_msg_t);
    if (header >= WINDOW_MAX_MSG) return -1;
    size_t max_payload = WINDOW_MAX_MSG - header;
    const uint8_t* src = (const uint8_t*)data;
    uint32_t offset = 0;
    uint8_t buf[WINDOW_MAX_MSG];

    while (offset < len) {
        uint32_t chunk = (uint32_t)((len - offset) > max_payload ? max_payload : (len - offset));
        window_image_msg_t* msg = (window_image_msg_t*)buf;
        msg->cmd = WINDOW_CMD_SET_IMAGE_RAW;
        msg->id = id;
        msg->w = (uint32_t)w;
        msg->h = (uint32_t)h;
        msg->channels = (uint32_t)channels;
        msg->offset = offset;
        msg->data_len = chunk;
        msg->flags = (offset == 0) ? WINDOW_IMAGE_FLAG_ALLOC : 0u;
        msg->reserved = 0;
        msg->size = (uint32_t)(header + chunk);
        memcpy(msg->data, src + offset, chunk);
        if (window_send_raw(msg, msg->size) != 0) return -1;
        offset += chunk;
    }
    return 0;
}

int window_set_icon(window_t id, const char* path) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SET_ICON);
    msg.id = id;
    window_copy_text(msg.text, sizeof(msg.text), path ? path : "");
    return window_send(&msg);
}

int window_get_input_state(window_t id, window_input_state_t* out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    uint64_t len = 0;
    if (sys_fs_read_file("/tmp/desk_input", 0, 0, &len) != 0) return -1;
    if (len < sizeof(*out)) return -1;
    if (sys_fs_read_file("/tmp/desk_input", out, sizeof(*out), &len) != 0) return -1;
    if (id != 0 && out->window_id != id) {
        out->focused = 0;
    }
    if (id != 0 && out->window_id == id) {
        out->mouse_x -= (out->win_x + WINDOW_CLIENT_OFF_X);
        out->mouse_y -= (out->win_y + WINDOW_CLIENT_OFF_Y);
        out->win_w -= (WINDOW_CLIENT_OFF_X * 2);
        out->win_h -= (WINDOW_TITLEBAR_H + 3);
        if (out->base_w > 0 && out->base_h > 0 && out->win_w > 0 && out->win_h > 0) {
            out->mouse_x = (out->mouse_x * out->base_w) / out->win_w;
            out->mouse_y = (out->mouse_y * out->base_h) / out->win_h;
            out->win_w = out->base_w;
            out->win_h = out->base_h;
        }
    }
    return 0;
}

int window_should_close(window_t id) {
    static uint64_t last_id = 0;
    static uint64_t last_tick = 0;
    window_input_state_t st;
    if (window_get_input_state(0, &st) != 0) return 0;
    if (!st.close_requested) return 0;
    if (st.close_window_id != id) return 0;
    if (st.close_event_tick == last_tick && last_id == id) return 0;
    last_id = id;
    last_tick = st.close_event_tick;
    return 1;
}

int window_open_console(void) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_OPEN_CONSOLE);
    return window_send(&msg);
}

int window_open_explorer(void) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_OPEN_EXPLORER);
    return window_send(&msg);
}

int window_open_settings(void) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_OPEN_SETTINGS);
    return window_send(&msg);
}

int window_open_file_picker(const char* title, const char* start_dir, uint32_t flags) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_OPEN_FILE_PICKER);
    msg.flags = flags;
    window_copy_text(msg.text, sizeof(msg.text), title ? title : "Open File");
    window_copy_text(msg.text2, sizeof(msg.text2), start_dir ? start_dir : "/");
    return window_send(&msg);
}

int window_open_message_box(const char* title, const char* text, uint32_t flags) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_OPEN_MESSAGE_BOX);
    msg.flags = flags;
    window_copy_text(msg.text, sizeof(msg.text), title ? title : "Message");
    window_copy_text(msg.text2, sizeof(msg.text2), text ? text : "");
    return window_send(&msg);
}

int window_dialog_pop(char* out, uint32_t cap, uint32_t* out_code) {
    if (!out || cap == 0) return -1;
    return (int)sys_dialog_pop(out, cap, out_code);
}

int window_notify(const char* title, const char* body) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_NOTIFY);
    window_copy_text(msg.text, sizeof(msg.text), title ? title : "Notice");
    window_copy_text(msg.text2, sizeof(msg.text2), body ? body : "");
    return window_send(&msg);
}

int window_set_anim_level(int level) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SET_ANIM_LEVEL);
    msg.x = level;
    return window_send(&msg);
}

int window_set_theme(const char* name) {
    window_msg_t msg;
    window_init_msg(&msg, WINDOW_CMD_SET_THEME);
    window_copy_text(msg.text, sizeof(msg.text), name ? name : "");
    return window_send(&msg);
}

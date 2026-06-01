#ifndef NTUX_WINDOW_PROTOCOL_H
#define NTUX_WINDOW_PROTOCOL_H

#include <stdint.h>

#define WINDOW_MAX_TEXT 192
#define WINDOW_MAX_OPS 512
#define WINDOW_MAX_MSG 60000

typedef enum {
    WINDOW_CMD_NOP = 0,
    WINDOW_CMD_INIT = 1,
    WINDOW_CMD_CREATE = 2,
    WINDOW_CMD_SET_TEXT = 3,
    WINDOW_CMD_SET_TITLE = 4,
    WINDOW_CMD_SET_RECT = 5,
    WINDOW_CMD_SET_BG = 6,
    WINDOW_CMD_SHOW = 7,
    WINDOW_CMD_FOCUS = 8,
    WINDOW_CMD_MINIMIZE = 9,
    WINDOW_CMD_TOGGLE_MAXIMIZE = 10,
    WINDOW_CMD_CLOSE = 11,
    WINDOW_CMD_OPEN_TERMINAL = 12,
    WINDOW_CMD_TERMINAL_EXEC = 13,
    WINDOW_CMD_OPEN_EXPLORER = 14,
    WINDOW_CMD_OPEN_CONSOLE = 15,
    WINDOW_CMD_OPEN_CLOCK = 16,
    WINDOW_CMD_OPEN_SETTINGS = 17,
    WINDOW_CMD_SET_THEME = 18,
    WINDOW_CMD_SET_ANIM_LEVEL = 19,
    WINDOW_CMD_ADD_ICON = 20,
    WINDOW_CMD_REMOVE_ICON = 21,
    WINDOW_CMD_RESCAN_ICONS = 22,
    WINDOW_CMD_DRAW_CLEAR = 23,
    WINDOW_CMD_DRAW_RECT = 24,
    WINDOW_CMD_DRAW_LINE = 25,
    WINDOW_CMD_DRAW_TEXT = 26,
    WINDOW_CMD_DRAW_PRESENT = 27,
    WINDOW_CMD_DRAW_BATCH = 28,
    WINDOW_CMD_TERMINAL_WRITE = 29,
    WINDOW_CMD_OPEN_FILE_PICKER = 30,
    WINDOW_CMD_OPEN_MESSAGE_BOX = 31,
    WINDOW_CMD_SET_IMAGE = 32,
    WINDOW_CMD_NOTIFY = 33,
    WINDOW_CMD_SET_ICON = 34,
    WINDOW_CMD_SET_IMAGE_RAW = 35
} window_cmd_t;

enum {
    WINDOW_DRAW_OP_RECT = 1,
    WINDOW_DRAW_OP_LINE = 2,
    WINDOW_DRAW_OP_TEXT = 3,
    WINDOW_DRAW_OP_BUTTON = 4
};

enum {
    WINDOW_RECT_HAS_X = 0x1u,
    WINDOW_RECT_HAS_Y = 0x2u,
    WINDOW_RECT_HAS_W = 0x4u,
    WINDOW_RECT_HAS_H = 0x8u
};

/* Picker/MsgBox flags live in window.h for app use. */

typedef struct {
    uint8_t type;
    uint8_t filled;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    int16_t x2;
    int16_t y2;
    uint32_t color;
    char text[128];
} window_draw_op_t;

typedef struct {
    uint32_t cmd;
    uint32_t size;
    uint64_t id;
    uint32_t count;
    uint32_t clear_valid;
    uint32_t clear_color;
    uint32_t reserved;
    window_draw_op_t ops[];
} window_batch_msg_t;

typedef struct {
    uint32_t cmd;
    uint32_t size;
    uint64_t id;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    uint32_t color;
    uint32_t flags;
    uint32_t owner_tid;
    uint32_t opts;
    char text[WINDOW_MAX_TEXT];
    char text2[WINDOW_MAX_TEXT];
} window_msg_t;

enum {
    WINDOW_IMAGE_FLAG_ALLOC = 0x1u
};

typedef struct {
    uint32_t cmd;
    uint32_t size;
    uint64_t id;
    uint32_t w;
    uint32_t h;
    uint32_t channels;
    uint32_t offset;
    uint32_t data_len;
    uint32_t flags;
    uint32_t reserved;
    uint8_t data[];
} window_image_msg_t;

#endif

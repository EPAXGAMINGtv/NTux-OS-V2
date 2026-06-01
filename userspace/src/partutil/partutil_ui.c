#include "partutil_app.h"
#include <string.h>
#include <stdio.h>
#include <syscall.h>

#define UI_BG 0xFF0B1118u
#define UI_PANEL 0xFF111A24u
#define UI_PANEL_HI 0xFF162232u
#define UI_TEXT 0xFFE9F2FFu
#define UI_SUB 0xFF95B5D5u
#define UI_ACCENT 0xFF2EB7FFu
#define UI_DANGER 0xFFB83B4Bu

typedef struct {
    int x, y, w, h;
    const char* label;
    partutil_action_t act;
    int danger;
} ui_btn_t;

static const ui_btn_t g_buttons[] = {
    { 20, 440, 100, 48, "Rescan", PARTUTIL_ACT_RESCAN, 0 },
    { 130, 440, 100, 48, "Wipe", PARTUTIL_ACT_WIPE, 1 },
    { 240, 440, 110, 48, "GPT", PARTUTIL_ACT_GPT_SINGLE, 0 },
    { 360, 440, 120, 48, "GPT+ESP", PARTUTIL_ACT_GPT_DUAL, 0 },
    { 490, 440, 110, 48, "MBR", PARTUTIL_ACT_MBR_SINGLE, 0 },
    { 610, 440, 80, 48, "FAT", PARTUTIL_ACT_MKFS_FAT, 0 },
    { 700, 440, 90, 48, "EXT2", PARTUTIL_ACT_MKFS_EXT2, 0 },
    { 800, 440, 90, 48, "EXT4", PARTUTIL_ACT_MKFS_EXT4, 0 }
};

static void draw_panel(window_t id, int x, int y, int w, int h) {
    window_draw_rect(id, x, y, w, h, UI_PANEL, 1);
    window_draw_rect(id, x, y, w, h, UI_PANEL_HI, 0);
}

static void draw_text(window_t id, int x, int y, uint32_t color, const char* text) {
    window_draw_text(id, x, y, color, text);
}

static void u64_to_str(uint64_t v, char* out, size_t cap) {
    char buf[32];
    int p = 0;
    if (v == 0) {
        if (cap > 1) {
            out[0] = '0';
            out[1] = '\0';
        }
        return;
    }
    while (v > 0 && p < (int)sizeof(buf)) {
        buf[p++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    int o = 0;
    while (p > 0 && o + 1 < (int)cap) {
        out[o++] = buf[--p];
    }
    out[o] = '\0';
}

static void draw_drives(window_t id, const partutil_state_t* st) {
    draw_panel(id, 20, 60, 260, 360);
    draw_text(id, 30, 70, UI_TEXT, "Drives");

    for (uint64_t i = 0; i < st->drive_count && i < PARTUTIL_MAX_DRIVES; ++i) {
        const ntux_block_device_info_t* d = &st->drives[i];
        int row_y = 98 + (int)i * 26;
        if ((int)i == st->sel_drive) {
            window_draw_rect(id, 26, row_y - 4, 248, 22, UI_PANEL_HI, 1);
        }
        char num[16];
        u64_to_str(st->drive_ids[i], num, sizeof(num));
        char size[32];
        uint64_t mib = (d->sectors * 512u) / (1024u * 1024u);
        u64_to_str(mib, size, sizeof(size));
        char line[96];
        snprintf(line, sizeof(line), "sd%c  %s MiB", (char)('a' + (int)st->drive_ids[i]), size);
        draw_text(id, 34, row_y, UI_TEXT, line);
        draw_text(id, 160, row_y, UI_SUB, d->model);
    }
}

static void draw_parts(window_t id, const partutil_state_t* st) {
    draw_panel(id, 300, 60, 580, 360);
    draw_text(id, 310, 70, UI_TEXT, "Partitions");

    for (uint64_t i = 0; i < st->part_count && i < PARTUTIL_MAX_PARTS; ++i) {
        const ntux_partition_info_t* p = &st->parts[i];
        int row_y = 98 + (int)i * 26;
        if ((int)i == st->sel_part) {
            window_draw_rect(id, 306, row_y - 4, 568, 22, UI_PANEL_HI, 1);
        }
        char idx[16];
        u64_to_str(p->index, idx, sizeof(idx));
        char lba[32];
        u64_to_str(p->lba_start, lba, sizeof(lba));
        char sz[32];
        u64_to_str(p->sectors, sz, sizeof(sz));
        char line[160];
        snprintf(line, sizeof(line), "p%s  type 0x%02x  start %s  sectors %s", idx, p->type, lba, sz);
        draw_text(id, 314, row_y, UI_TEXT, line);
    }
}

static void draw_buttons(window_t id, const partutil_state_t* st) {
    for (uint64_t i = 0; i < sizeof(g_buttons) / sizeof(g_buttons[0]); ++i) {
        const ui_btn_t* b = &g_buttons[i];
        uint32_t col = b->danger ? UI_DANGER : UI_ACCENT;
        if (st && st->focus_group == 2 && (int)i == st->action_focus) {
            col = 0xFF43C1FFu;
        }
        window_draw_rect(id, b->x, b->y, b->w, b->h, col, 1);
        window_draw_rect(id, b->x, b->y, b->w, b->h, 0xFFFFFFFFu, 0);
        window_draw_text(id, b->x + 10, b->y + 16, 0xFFF4FBFFu, b->label);
    }
}

static void draw_status(window_t id, const partutil_state_t* st) {
    if (!st->status[0]) return;
    window_draw_rect(id, 20, 410, 860, 22, 0xFF0E1621u, 1);
    window_draw_text(id, 28, 414, UI_SUB, st->status);
}

static void draw_confirm(window_t id, const partutil_state_t* st) {
    if (!st->confirm_active) return;
    window_draw_rect(id, 240, 170, 420, 140, 0xFF0B141Fu, 1);
    window_draw_rect(id, 240, 170, 420, 140, UI_ACCENT, 0);
    draw_text(id, 260, 190, UI_TEXT, "Confirm action?");
    draw_text(id, 260, 214, UI_SUB, "This will modify the selected drive.");

    uint32_t yes_col = st->confirm_choice ? 0xFF43C1FFu : UI_ACCENT;
    uint32_t no_col = (!st->confirm_choice) ? 0xFF43C1FFu : 0xFF2C3A4Au;
    window_draw_rect(id, 260, 250, 140, 40, yes_col, 1);
    window_draw_text(id, 300, 262, 0xFFF4FBFFu, "Yes");

    window_draw_rect(id, 420, 250, 140, 40, no_col, 1);
    window_draw_text(id, 460, 262, 0xFFF4FBFFu, "No");
}

partutil_action_t partutil_hit_test(const partutil_state_t* st, int mx, int my) {
    (void)st;
    if (mx < 0 || my < 0) return PARTUTIL_ACT_NONE;

    if (st->confirm_active) {
        if (mx >= 260 && mx < 400 && my >= 250 && my < 290) return st->confirm_action;
        if (mx >= 420 && mx < 560 && my >= 250 && my < 290) return PARTUTIL_ACT_NONE;
        return PARTUTIL_ACT_NONE;
    }

    for (uint64_t i = 0; i < sizeof(g_buttons) / sizeof(g_buttons[0]); ++i) {
        const ui_btn_t* b = &g_buttons[i];
        if (mx >= b->x && mx < b->x + b->w && my >= b->y && my < b->y + b->h) {
            return b->act;
        }
    }

    if (mx >= 20 && mx < 280 && my >= 90 && my < 420) {
        int idx = (my - 98) / 26;
        if (idx >= 0 && idx < (int)st->drive_count) return (partutil_action_t)(1000 + idx);
    }
    if (mx >= 300 && mx < 880 && my >= 90 && my < 420) {
        int idx = (my - 98) / 26;
        if (idx >= 0 && idx < (int)st->part_count) return (partutil_action_t)(2000 + idx);
    }

    return PARTUTIL_ACT_NONE;
}

void partutil_draw(window_t id, const partutil_state_t* st) {
    window_clear(id, UI_BG);

    window_draw_text(id, 20, 20, UI_TEXT, "NTux Partition Utility");
    window_draw_text(id, 20, 40, UI_SUB, "Select a drive, then pick an action.");

    draw_drives(id, st);
    draw_parts(id, st);
    draw_buttons(id, st);
    draw_status(id, st);
    draw_confirm(id, st);
}

void partutil_init(partutil_state_t* st) {
    memset(st, 0, sizeof(*st));
    st->focus_group = 0;
    st->action_focus = 0;
    st->confirm_choice = 1;
    partutil_rescan(st);
    partutil_set_status(st, "Ready.");
}

void partutil_set_status(partutil_state_t* st, const char* msg) {
    if (!st) return;
    st->status[0] = '\0';
    if (msg) {
        strncpy(st->status, msg, sizeof(st->status) - 1);
        st->status[sizeof(st->status) - 1] = '\0';
        st->status_tick = sys_get_ticks();
    }
}

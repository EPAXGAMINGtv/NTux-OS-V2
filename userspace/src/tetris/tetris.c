#include <window.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>

#define BOARD_W 10
#define BOARD_H 20

static void sleep_ms(uint64_t ms) {
    if (ms == 0) return;
    usleep((unsigned int)(ms * 1000u));
}

static uint8_t g_board[BOARD_H][BOARD_W];

static const uint8_t g_shapes[7][4][4][4] = {
    // I
    {
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
        {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}
    },
    // O
    {
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
    },
    // T
    {
        {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    // S
    {
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}}
    },
    // Z
    {
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}}
    },
    // J
    {
        {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}
    },
    // L
    {
        {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}
    }
};

static uint8_t g_piece = 0;
static uint8_t g_rot = 0;
static int g_px = 3;
static int g_py = 0;
static uint32_t g_score = 0;
static uint32_t g_lines = 0;

static uint32_t tetris_level(void) {
    return g_lines / 10u;
}

static uint64_t drop_interval_ticks(void) {
    uint64_t base = 600u;
    uint64_t step = 40u;
    uint64_t min = 120u;
    uint64_t level = (uint64_t)tetris_level();
    uint64_t dec = level * step;
    if (dec >= base - min) return min;
    return base - dec;
}

static int piece_cell(int p, int r, int x, int y) {
    return g_shapes[p][r][y][x] != 0;
}

static int collides(int px, int py, int p, int r) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (!piece_cell(p, r, x, y)) continue;
            int bx = px + x;
            int by = py + y;
            if (bx < 0 || bx >= BOARD_W || by < 0 || by >= BOARD_H) return 1;
            if (g_board[by][bx]) return 1;
        }
    }
    return 0;
}

static void place_piece(void) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (!piece_cell(g_piece, g_rot, x, y)) continue;
            int bx = g_px + x;
            int by = g_py + y;
            if (bx < 0 || bx >= BOARD_W || by < 0 || by >= BOARD_H) continue;
            g_board[by][bx] = (uint8_t)(g_piece + 1);
        }
    }
}

static void clear_lines(void) {
    for (int y = BOARD_H - 1; y >= 0; --y) {
        int full = 1;
        for (int x = 0; x < BOARD_W; ++x) {
            if (!g_board[y][x]) { full = 0; break; }
        }
        if (!full) continue;
        for (int yy = y; yy > 0; --yy) {
            memcpy(g_board[yy], g_board[yy - 1], BOARD_W);
        }
        memset(g_board[0], 0, BOARD_W);
        g_lines++;
        g_score += 100;
        y++;
    }
}

static void spawn_piece(uint8_t p) {
    g_piece = p;
    g_rot = 0;
    g_px = 3;
    g_py = 0;
}

static uint8_t pseudo_rand(uint32_t* state) {
    *state = (*state * 1103515245u + 12345u);
    return (uint8_t)((*state >> 16) & 0x7Fu);
}

static void render(uint64_t win) {
    window_clear(win, 0xFF0B0F14u);

    window_draw_text(win, 16, 12, 0xFFBFD7FFu, "NTux Tetris");

    char line[BOARD_W + 1];
    for (int y = 0; y < BOARD_H; ++y) {
        for (int x = 0; x < BOARD_W; ++x) {
            uint8_t cell = g_board[y][x];
            line[x] = cell ? '#' : '.';
        }
        line[BOARD_W] = '\0';
        window_draw_text(win, 16, 32 + y * 12, 0xFF9CE0FFu, line);
    }

    // Overlay current piece as text
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (!piece_cell(g_piece, g_rot, x, y)) continue;
            int bx = g_px + x;
            int by = g_py + y;
            if (bx < 0 || bx >= BOARD_W || by < 0 || by >= BOARD_H) continue;
            char ch[2] = {'@', '\0'};
            window_draw_text(win, 16 + bx * 8, 32 + by * 12, 0xFFFFF08Au, ch);
        }
    }

    char info[64];
    snprintf(info, sizeof(info), "Score: %u", (unsigned)g_score);
    window_draw_text(win, 130, 32, 0xFFB7FF9Bu, info);
    snprintf(info, sizeof(info), "Lines: %u", (unsigned)g_lines);
    window_draw_text(win, 130, 44, 0xFFB7FF9Bu, info);
    snprintf(info, sizeof(info), "Level: %u", (unsigned)tetris_level());
    window_draw_text(win, 130, 56, 0xFFB7FF9Bu, info);
    window_draw_text(win, 130, 76, 0xFF8AD6FFu, "Arrows: move");
    window_draw_text(win, 130, 88, 0xFF8AD6FFu, "Up: rotate");
    window_draw_text(win, 130, 100, 0xFF8AD6FFu, "Space: drop");

    window_present(win);
}

static int key_edge(uint8_t scancode, int* prev) {
    int down = sys_kbd_is_pressed(scancode) > 0 ? 1 : 0;
    int fired = (down && !*prev);
    *prev = down;
    return fired;
}

void ntux_user_entry(void) {
    uint64_t win = 0x7454524953ull;
    window_init();
    window_create(win, 120, 80, 360, 320, 0xFF0B0F14u, "Tetris");
    (void)window_set_icon(win, "/boot/res/icons/tetris.bmp");

    memset(g_board, 0, sizeof(g_board));
    uint32_t rng = (uint32_t)sys_get_ticks() ^ 0xA5A5u;
    spawn_piece((uint8_t)(pseudo_rand(&rng) % 7));

    uint64_t last_drop = sys_get_ticks();

    int prev_left = 0, prev_right = 0, prev_down = 0, prev_up = 0, prev_space = 0;

    for (;;) {
        if (window_should_close(win)) break;
        if (key_edge(0x4B, &prev_left)) {
            if (!collides(g_px - 1, g_py, g_piece, g_rot)) g_px--;
        }
        if (key_edge(0x4D, &prev_right)) {
            if (!collides(g_px + 1, g_py, g_piece, g_rot)) g_px++;
        }
        if (key_edge(0x48, &prev_up)) {
            uint8_t nr = (uint8_t)((g_rot + 1) & 3);
            if (!collides(g_px, g_py, g_piece, nr)) g_rot = nr;
        }
        if (key_edge(0x50, &prev_down)) {
            if (!collides(g_px, g_py + 1, g_piece, g_rot)) g_py++;
        }
        if (key_edge(0x39, &prev_space)) {
            while (!collides(g_px, g_py + 1, g_piece, g_rot)) g_py++;
            place_piece();
            clear_lines();
            spawn_piece((uint8_t)(pseudo_rand(&rng) % 7));
            if (collides(g_px, g_py, g_piece, g_rot)) {
                memset(g_board, 0, sizeof(g_board));
                g_score = 0;
                g_lines = 0;
            }
        }

        uint64_t now = sys_get_ticks();
        if (now - last_drop >= drop_interval_ticks()) {
            last_drop = now;
            if (!collides(g_px, g_py + 1, g_piece, g_rot)) {
                g_py++;
            } else {
                place_piece();
                clear_lines();
                spawn_piece((uint8_t)(pseudo_rand(&rng) % 7));
                if (collides(g_px, g_py, g_piece, g_rot)) {
                    memset(g_board, 0, sizeof(g_board));
                    g_score = 0;
                    g_lines = 0;
                }
            }
        }

        render(win);
        sleep_ms(20);
    }
}

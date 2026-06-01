#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <window.h>

#define SNAKE2_W 720
#define SNAKE2_H 480
#define CELL 20
#define GRID_W (SNAKE2_W / CELL)
#define GRID_H (SNAKE2_H / CELL)
#define MAX_SNAKE (GRID_W * GRID_H)

typedef struct {
    int x;
    int y;
} seg_t;

static seg_t g_snake[MAX_SNAKE];
static int g_len = 0;
static int g_dir_x = 1;
static int g_dir_y = 0;
static int g_next_x = 1;
static int g_next_y = 0;
static int g_food_x = 5;
static int g_food_y = 5;
static int g_game_over = 0;
static uint32_t g_rng = 0xA1234B5Cu;
static uint8_t g_key_last[128];

static int key_edge(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static uint32_t rng_next(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

static int cell_free(int x, int y) {
    for (int i = 0; i < g_len; ++i) {
        if (g_snake[i].x == x && g_snake[i].y == y) return 0;
    }
    return 1;
}

static void spawn_food(void) {
    for (int i = 0; i < 200; ++i) {
        int x = (int)(rng_next() % GRID_W);
        int y = (int)(rng_next() % GRID_H);
        if (cell_free(x, y)) {
            g_food_x = x;
            g_food_y = y;
            return;
        }
    }
    g_food_x = GRID_W / 2;
    g_food_y = GRID_H / 2;
}

static void reset_game(void) {
    g_len = 4;
    g_snake[0].x = GRID_W / 2;
    g_snake[0].y = GRID_H / 2;
    for (int i = 1; i < g_len; ++i) {
        g_snake[i].x = g_snake[0].x - i;
        g_snake[i].y = g_snake[0].y;
    }
    g_dir_x = 1;
    g_dir_y = 0;
    g_next_x = 1;
    g_next_y = 0;
    g_game_over = 0;
    spawn_food();
}

static void step_game(void) {
    if (g_game_over) return;
    g_dir_x = g_next_x;
    g_dir_y = g_next_y;

    int nx = g_snake[0].x + g_dir_x;
    int ny = g_snake[0].y + g_dir_y;
    if (nx < 0 || ny < 0 || nx >= GRID_W || ny >= GRID_H) {
        g_game_over = 1;
        return;
    }
    for (int i = 0; i < g_len; ++i) {
        if (g_snake[i].x == nx && g_snake[i].y == ny) {
            g_game_over = 1;
            return;
        }
    }

    for (int i = g_len; i > 0; --i) {
        g_snake[i] = g_snake[i - 1];
    }
    g_snake[0].x = nx;
    g_snake[0].y = ny;

    if (nx == g_food_x && ny == g_food_y) {
        if (g_len < MAX_SNAKE - 1) g_len++;
        spawn_food();
    }
}

static void draw_grid(window_t id) {
    for (int y = 0; y < GRID_H; ++y) {
        for (int x = 0; x < GRID_W; ++x) {
            uint32_t c = 0xFF0E151Eu;
            (void)window_draw_rect(id, x * CELL, y * CELL, CELL, CELL, c, 1);
        }
    }
    for (int y = 0; y <= GRID_H; ++y) {
        (void)window_draw_line(id, 0, y * CELL, GRID_W * CELL, y * CELL, 0xFF1D2A3Au);
    }
    for (int x = 0; x <= GRID_W; ++x) {
        (void)window_draw_line(id, x * CELL, 0, x * CELL, GRID_H * CELL, 0xFF1D2A3Au);
    }
}

static void draw_snake(window_t id) {
    for (int i = g_len - 1; i >= 0; --i) {
        int x = g_snake[i].x * CELL;
        int y = g_snake[i].y * CELL;
        uint32_t body = (i == 0) ? 0xFF8DE0B0u : 0xFF4FBF86u;
        (void)window_draw_rect(id, x + 2, y + 2, CELL - 4, CELL - 4, body, 1);
        (void)window_draw_rect(id, x + 2, y + 2, CELL - 4, CELL - 4, 0xFF1C2A22u, 0);
    }
    int fx = g_food_x * CELL;
    int fy = g_food_y * CELL;
    (void)window_draw_rect(id, fx + 3, fy + 3, CELL - 6, CELL - 6, 0xFFFF6B6Bu, 1);
    (void)window_draw_rect(id, fx + 3, fy + 3, CELL - 6, CELL - 6, 0xFF5A1F1Fu, 0);
}

void ntux_user_entry(void) {
    window_t id = 0x534E4B32554C4Cull; /* "SNK2UL" */
    if (window_init() != 0 || window_create(id, 120, 90, SNAKE2_W, SNAKE2_H, 0xFF0B1119u, "Snake 2") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/snake.bmp");
    (void)window_show(id, 1);
    window_focus(id);

    reset_game();
    uint64_t last = sys_get_ticks();
    uint64_t acc = 0;

    for (;;) {
        if (window_should_close(id)) break;
        if (key_edge(0x01)) break; /* Esc */

        int up = key_edge(0x48);
        int down = key_edge(0x50);
        int left = key_edge(0x4B);
        int right = key_edge(0x4D);
        if (up && g_dir_y == 0) { g_next_x = 0; g_next_y = -1; }
        else if (down && g_dir_y == 0) { g_next_x = 0; g_next_y = 1; }
        else if (left && g_dir_x == 0) { g_next_x = -1; g_next_y = 0; }
        else if (right && g_dir_x == 0) { g_next_x = 1; g_next_y = 0; }
        if (key_edge(0x13)) { /* R */
            reset_game();
        }

        uint64_t now = sys_get_ticks();
        uint64_t dt = now - last;
        last = now;
        acc += dt;
        while (acc >= 6u) { /* tick speed */
            step_game();
            acc -= 6u;
        }

        (void)window_clear(id, 0xFF0B1119u);
        draw_grid(id);
        draw_snake(id);
        if (g_game_over) {
            (void)window_draw_rect(id, 120, 170, 480, 120, 0xCC111821u, 1);
            (void)window_draw_rect(id, 120, 170, 480, 120, 0xFF2C3E52u, 0);
            (void)window_draw_text(id, 180, 200, 0xFFEAF4FFu, "Game Over");
            (void)window_draw_text(id, 180, 224, 0xFF9EC5E5u, "Press R to restart or Esc to quit");
        }
        (void)window_present(id);
        sys_wait_ticks(1);
    }

    window_close(id);
    sys_exit(0);
}

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <window.h>

typedef struct {
    float x;
    int gap_y;
    int scored;
} pipe_t;

static uint8_t g_key_last[128];

static int key_edge(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static uint32_t rng_next(uint32_t* s) {
    *s = (*s * 1664525u) + 1013904223u;
    return *s;
}

static int rand_range(uint32_t* s, int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(rng_next(s) % (uint32_t)(hi - lo + 1));
}

static void reset_pipes(pipe_t* pipes, int count, int w, int gap_h, int spacing, uint32_t* rng) {
    for (int i = 0; i < count; ++i) {
        pipes[i].x = (float)(w + i * spacing);
        pipes[i].gap_y = rand_range(rng, 120, 360);
        pipes[i].scored = 0;
    }
}

void ntux_user_entry(void) {
    window_t id = 0x464C41505059ull; /* "FLAPPY" */
    const int w = 420;
    const int h = 540;
    if (window_init() != 0 || window_create(id, 140, 100, w, h, 0xFF0B1119u, "Flappy Bird") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/flappy.bmp");

    const int ground_h = 80;
    const int pipe_w = 54;
    const int gap_h = 150;
    const int pipe_count = 4;
    const int pipe_spacing = 170;
    const float pipe_speed = 2.2f;
    const float gravity = 0.35f;
    const float flap_v = -6.2f;
    const int bird_x = 120;
    const int bird_r = 10;

    pipe_t pipes[pipe_count];
    uint32_t rng = (uint32_t)sys_get_ticks();
    reset_pipes(pipes, pipe_count, w, gap_h, pipe_spacing, &rng);

    float bird_y = 240.0f;
    float bird_v = 0.0f;
    int score = 0;
    int best = 0;
    int over = 0;

    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;
    uint64_t last_tick = sys_get_ticks();

    for (;;) {
        if (window_should_close(id)) break;
        if (key_edge(0x01)) break; /* Esc */

        if (key_edge(0x39) || key_edge(0x11)) { /* Space or W */
            if (over) {
                over = 0;
                bird_y = 240.0f;
                bird_v = 0.0f;
                score = 0;
                reset_pipes(pipes, pipe_count, w, gap_h, pipe_spacing, &rng);
            } else {
                bird_v = flap_v;
            }
        }

        if (key_edge(0x13)) { /* R */
            over = 0;
            bird_y = 240.0f;
            bird_v = 0.0f;
            score = 0;
            reset_pipes(pipes, pipe_count, w, gap_h, pipe_spacing, &rng);
        }

        uint64_t now = sys_get_ticks();
        if (now - last_tick >= 1) {
            last_tick = now;
            if (!over) {
                bird_v += gravity;
                bird_y += bird_v;

                if (bird_y - bird_r < 0) {
                    bird_y = (float)bird_r;
                    bird_v = 0.0f;
                }
                if (bird_y + bird_r > (float)(h - ground_h)) {
                    over = 1;
                }

                for (int i = 0; i < pipe_count; ++i) {
                    pipes[i].x -= pipe_speed;
                    if (pipes[i].x + pipe_w < 0) {
                        pipes[i].x = (float)(w + pipe_spacing);
                        pipes[i].gap_y = rand_range(&rng, 120, 360);
                        pipes[i].scored = 0;
                    }

                    int px = (int)pipes[i].x;
                    if (!pipes[i].scored && px + pipe_w < bird_x) {
                        pipes[i].scored = 1;
                        score += 1;
                        if (score > best) best = score;
                    }

                    int in_x = (bird_x + bird_r > px) && (bird_x - bird_r < px + pipe_w);
                    int gap_top = pipes[i].gap_y - gap_h / 2;
                    int gap_bot = pipes[i].gap_y + gap_h / 2;
                    if (in_x) {
                        if (bird_y - bird_r < (float)gap_top || bird_y + bird_r > (float)gap_bot) {
                            over = 1;
                        }
                    }
                }
            }
        }

        window_clear(id, 0xFF0D1B2A);
        window_draw_rect(id, 0, h - ground_h, w, ground_h, 0xFF3D2B1Fu, 1);
        window_draw_rect(id, 0, h - ground_h, w, 6, 0xFF5E3A26u, 1);

        for (int i = 0; i < pipe_count; ++i) {
            int px = (int)pipes[i].x;
            int gap_top = pipes[i].gap_y - gap_h / 2;
            int gap_bot = pipes[i].gap_y + gap_h / 2;
            int top_h = gap_top;
            int bot_y = gap_bot;
            int bot_h = (h - ground_h) - gap_bot;
            if (top_h > 0) window_draw_rect(id, px, 0, pipe_w, top_h, 0xFF3EDC7Du, 1);
            if (bot_h > 0) window_draw_rect(id, px, bot_y, pipe_w, bot_h, 0xFF3EDC7Du, 1);
            window_draw_rect(id, px - 2, gap_top - 6, pipe_w + 4, 6, 0xFF2EB969u, 1);
            window_draw_rect(id, px - 2, gap_bot, pipe_w + 4, 6, 0xFF2EB969u, 1);
        }

        window_draw_rect(id, bird_x - bird_r, (int)bird_y - bird_r, bird_r * 2, bird_r * 2, 0xFFFFD166u, 1);
        window_draw_rect(id, bird_x + 6, (int)bird_y - 6, 4, 4, 0xFF1B263Bu, 1);

        char line[64];
        snprintf(line, sizeof(line), "Score: %d  Best: %d", score, best);
        window_draw_text(id, 10, 10, 0xFFEAF4FFu, line);
        window_draw_text(id, 10, 26, 0xFF9ED1FFu, "Space/W flap  R restart  Esc exit");
        if (over) {
            window_draw_text(id, 118, 250, 0xFFFF5C7Au, "GAME OVER");
            window_draw_text(id, 94, 268, 0xFFEAF4FFu, "Press Space or R");
        }

        window_present(id);
        sys_wait_ticks(1);
    }

    sys_exit(0);
}

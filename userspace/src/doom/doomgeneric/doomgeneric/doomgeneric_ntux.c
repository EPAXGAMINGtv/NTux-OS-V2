#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <window.h>

#include "doomtype.h"
#include "doomgeneric.h"

#define KEY_QUEUE_SIZE 64

typedef struct {
    uint8_t scancode;
    uint8_t pressed;
} key_evt_t;

static key_evt_t g_keyq[KEY_QUEUE_SIZE];
static uint8_t g_keyq_w = 0;
static uint8_t g_keyq_r = 0;
static uint8_t g_prev_state[0x80];
static uint8_t g_state[0x80];

static uint64_t doom_cached_hz = 0;
static int g_use_window = 0;
static window_t g_win_id = 0x444F4F4D554C4Cull; /* "DOOMUL" */
static uint8_t* g_rgb_buf = 0;

static int doom_ticks_are_advancing(void) {
    uint64_t t0 = sys_get_ticks();
    for (int i = 0; i < 4096; ++i) {
        sys_yield();
        if (sys_get_ticks() != t0) return 1;
    }
    return 0;
}

static uint64_t doom_get_hz(void) {
    if (doom_cached_hz) return doom_cached_hz;
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;

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

    doom_cached_hz = hz;
    return hz;
}

static uint8_t doom_is_leap(uint16_t y) {
    return ((y % 4u) == 0u && (y % 100u) != 0u) || ((y % 400u) == 0u);
}

static uint32_t doom_days_before_month(uint16_t year, uint8_t month) {
    static const uint8_t days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t days = 0;
    for (uint8_t m = 1; m < month; ++m) {
        days += days_in_month[m - 1];
        if (m == 2 && doom_is_leap(year)) days += 1;
    }
    return days;
}

static uint64_t doom_unix_seconds(const ntux_time_t* t) {
    if (!t) return 0;
    uint32_t days = 0;
    for (uint16_t y = 1970; y < t->year; ++y) {
        days += doom_is_leap(y) ? 366u : 365u;
    }
    days += doom_days_before_month(t->year, t->month);
    days += (uint32_t)(t->day - 1u);
    return (uint64_t)days * 86400ull +
           (uint64_t)t->hour * 3600ull +
           (uint64_t)t->minute * 60ull +
           (uint64_t)t->second;
}
static void keyq_push(uint8_t scancode, uint8_t pressed) {
    uint8_t next = (uint8_t)((g_keyq_w + 1u) & (KEY_QUEUE_SIZE - 1u));
    if (next == g_keyq_r) return;
    g_keyq[g_keyq_w].scancode = scancode;
    g_keyq[g_keyq_w].pressed = pressed;
    g_keyq_w = next;
}

void DG_Init() {
    memset(g_prev_state, 0, sizeof(g_prev_state));
    g_keyq_w = 0;
    g_keyq_r = 0;
}

void DG_DrawFrame() {
    static int reported = 0;
    if (g_use_window) {
        if (window_should_close(g_win_id)) {
            sys_exit(0);
        }
        uint32_t count = (uint32_t)(DOOMGENERIC_RESX * DOOMGENERIC_RESY);
        if (!g_rgb_buf) {
            g_rgb_buf = (uint8_t*)malloc((size_t)count * 3u);
        }
        if (!g_rgb_buf) return;
        const uint32_t* src = (const uint32_t*)DG_ScreenBuffer;
        uint8_t* dst = g_rgb_buf;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t c = src[i];
            *dst++ = (uint8_t)((c >> 16) & 0xFFu); // R
            *dst++ = (uint8_t)((c >> 8) & 0xFFu);  // G
            *dst++ = (uint8_t)(c & 0xFFu);         // B
        }
        (void)window_set_image_raw(g_win_id, DOOMGENERIC_RESX, DOOMGENERIC_RESY, 3, g_rgb_buf, count * 3u);
        return;
    }
    long rc = sys_fb_blit32((const void*)DG_ScreenBuffer,
                            DOOMGENERIC_RESX,
                            DOOMGENERIC_RESY,
                            DOOMGENERIC_RESX * sizeof(uint32_t));
    if (rc != 0 && !reported) {
        reported = 1;
        printf("[doom] fb_blit32 failed rc=%ld\n", rc);
    }
}

void DG_SleepMs(uint32_t ms) {
    // Avoid blocking on kernel timer stalls.
    uint64_t start = DG_GetTicksMs();
    uint64_t end = start + ms;
    uint64_t spins = 0;
    while (DG_GetTicksMs() < end && spins < 200000u) {
        sys_yield();
        spins++;
    }
}

uint32_t DG_GetTicksMs() {
    ntux_time_t now;
    if (sys_get_time(&now) != 0) {
        return 0;
    }

    uint64_t hz = doom_get_hz();
    static uint8_t last_sec = 0xFFu;
    static uint64_t sec_base_ticks = 0;

    if (last_sec == 0xFFu || now.second != last_sec) {
        last_sec = now.second;
        sec_base_ticks = sys_get_ticks();
    }

    uint64_t unix_s = doom_unix_seconds(&now);
    uint64_t sub = 0;
    uint64_t ticks = sys_get_ticks();
    if (ticks >= sec_base_ticks) {
        sub = ((ticks - sec_base_ticks) * 1000ull) / (hz ? hz : 1u);
        if (sub > 999ull) sub = 999ull;
    }
    return (uint32_t)(unix_s * 1000ull + sub);
}

int DG_GetKey(int* pressed, unsigned char* key) {
    if (sys_kbd_get_state(g_state, sizeof(g_state)) != 0) {
        return 0;
    }
    for (uint8_t sc = 1; sc < 0x80; ++sc) {
        uint8_t cur = g_state[sc] ? 1u : 0u;
        if (cur != g_prev_state[sc]) {
            g_prev_state[sc] = cur;
            keyq_push(sc, cur);
        }
    }

    if (g_keyq_r == g_keyq_w) return 0;

    key_evt_t evt = g_keyq[g_keyq_r];
    g_keyq_r = (uint8_t)((g_keyq_r + 1u) & (KEY_QUEUE_SIZE - 1u));

    if (pressed) *pressed = evt.pressed ? 1 : 0;
    if (key) *key = evt.scancode;
    return 1;
}

void DG_SetWindowTitle(const char* title) {
    (void)title;
}

static int doom_main(int argc, char **argv) {
    // Clear screen so the status line is at the top before Doom starts.
    sys_clear_screen(0x00000000);
    sys_set_text_color(0xFFFFFFFF);
    printf("DOOM starting...\n");

    // NTux: fall back to singletics only if the timer is not advancing.
    extern boolean singletics;
    singletics = doom_ticks_are_advancing() ? false : true;

    if (argc <= 1 || argv == 0) {
        const char* iwad = "/boot/DOOM1.WAD";
        if (sys_fs_exists("/boot/DOOM1.WAD") > 0) {
            iwad = "/boot/DOOM1.WAD";
        } else if (sys_fs_exists("/boot/doom1.wad") > 0) {
            iwad = "/boot/doom1.wad";
        } else if (sys_fs_exists("/boot/DOOM.WAD") > 0) {
            iwad = "/boot/DOOM.WAD";
        } else if (sys_fs_exists("/boot/doom.wad") > 0) {
            iwad = "/boot/doom.wad";
        } else if (sys_fs_exists("/DOOM1.WAD") > 0) {
            iwad = "/DOOM1.WAD";
        } else if (sys_fs_exists("/doom1.wad") > 0) {
            iwad = "/doom1.wad";
        } else if (sys_fs_exists("/DOOM.WAD") > 0) {
            iwad = "/DOOM.WAD";
        } else if (sys_fs_exists("/doom.wad") > 0) {
            iwad = "/doom.wad";
        }
        char *default_argv[] = {
            "doom",
            "-iwad",
            (char*)iwad,
            "-nosound",
            "-nosfx",
            "-nomusic",
        };
        doomgeneric_Create(6, default_argv);
    } else {
        doomgeneric_Create(argc, argv);
    }

    uint64_t last_ms = DG_GetTicksMs();
    uint64_t frames = 0;
    for (;;) {
        doomgeneric_Tick();
        frames++;
        uint64_t now_ms = DG_GetTicksMs();
        if ((now_ms - last_ms) >= 1000u) {
            last_ms = now_ms;
        } else if ((frames % 300u) == 0u) {
            // Fallback heartbeat if timer stalls.
            printf("[doom] loop frame\n");
        }
        sys_yield();
    }
}

void ntux_user_entry(void) {
    if (window_init() == 0) {
        int w = DOOMGENERIC_RESX * 2;
        int h = DOOMGENERIC_RESY * 2;
        if (window_create(g_win_id, 90, 70, w, h, 0xFF0B1119u, "DOOM") == 0) {
            (void)window_set_icon(g_win_id, "/boot/res/icons/doom.bmp");
            (void)window_show(g_win_id, 1);
            window_focus(g_win_id);
            g_use_window = 1;
        }
    }
    (void)doom_main(0, 0);
    sys_exit(0);
}

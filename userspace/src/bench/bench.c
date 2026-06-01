
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <window.h>

static uint8_t g_key_last[128];

static int key_edge(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static uint64_t get_hz(void) {
    uint64_t hz = (uint64_t)sys_get_timer_hz();
    if (hz == 0) hz = 200u;
    return hz;
}

static void format_rate(char* out, size_t cap, uint64_t value, const char* unit) {
    if (!out || cap == 0) return;
    uint64_t whole = value / 1000000u;
    uint64_t frac = (value % 1000000u) / 1000u;
    snprintf(out, cap, "%llu.%03llu %s", (unsigned long long)whole,
             (unsigned long long)frac, unit ? unit : "");
}

static void draw_ui(window_t id, int w, int h, const char* status,
                    const char* cpu, const char* mem, const char* gfx) {
    const uint32_t bg = 0xFF0B1119u;
    const uint32_t text = 0xFFEAF4FFu;
    const uint32_t dim = 0xFF8EA6C8u;
    const uint32_t accent = 0xFF56E6AAu;
    const uint32_t head = 0xFFBFD7FFu;

    window_clear(id, bg);
    window_draw_text(id, 12, 8, head, "NTux Benchmark Suite");
    window_draw_text(id, 12, 22, dim, "Press 1/2/3/4/5 to run a benchmark. Esc to exit.");

    window_draw_text(id, 12, 54, accent, "1) CPU (integer mix)");
    window_draw_text(id, 12, 70, text, cpu && cpu[0] ? cpu : "last: -");

    window_draw_text(id, 12, 100, accent, "2) Memory (read+write)");
    window_draw_text(id, 12, 116, text, mem && mem[0] ? mem : "last: -");

    window_draw_text(id, 12, 146, accent, "3) GFX (particles+rects)");
    window_draw_text(id, 12, 162, text, gfx && gfx[0] ? gfx : "last: -");

    window_draw_text(id, 12, 192, accent, "4) Sort (1024 elems)");
    window_draw_text(id, 12, 208, text, "last: see status");

    window_draw_text(id, 12, 238, accent, "5) 3D (multi-obj scene)");
    window_draw_text(id, 12, 254, text, "last: see status");

    if (status && status[0]) {
        window_draw_text(id, 12, h - 20, dim, status);
    }
    window_present(id);
}

static void bench_cpu(char* out, size_t cap) {
    uint64_t hz = get_hz();
    uint64_t start = sys_get_ticks();
    uint64_t end = start + hz * 10u;
    uint64_t a = 0x123456789ABCDEF0ull, b = 0xFEDCBA9876543210ull, c = 1;
    uint64_t fib0 = 0, fib1 = 1, fib2;
    uint64_t ops = 0;
    while (sys_get_ticks() < end) {
        for (int i = 0; i < 10000; ++i) {
            // Complex mix: mul/add/shift/rotate/bitrev/fib
            a = (a * 6364136223846793005ull + 1442695040888963407ull) ^ (a >> 33);
            b = b + a * 1103515245ull + 12345ull;
            b = (b << 13) ^ (b >> 19);
            c = __builtin_popcountll(a ^ b) * c;
            fib2 = fib0 + fib1; fib0 = fib1; fib1 = fib2;
            ops += 5;
        }
    }
    uint64_t elapsed = sys_get_ticks() - start;
    if (elapsed == 0) elapsed = 1;
    uint64_t gops = ops * hz / (elapsed * 1000000000ull);
    char rate[64];
    format_rate(rate, sizeof(rate), gops * 1000ull, "Gops/s");
    snprintf(out, cap, "last: %s", rate);
    (void)a; (void)b; (void)c;
}

static void bench_mem(char* out, size_t cap) {
    const uint32_t size = 32u * 1024u * 1024u;
    uint8_t* buf = (uint8_t*)malloc(size);
    uint8_t* tmp = (uint8_t*)malloc(size);
    if (!buf || !tmp) {
        snprintf(out, cap, "last: alloc failed");
        free(buf); free(tmp);
        return;
    }
    uint64_t hz = get_hz();
    volatile uint8_t* v = (volatile uint8_t*)buf;
    for (uint32_t i = 0; i < size; ++i) buf[i] = (uint8_t)(i & 0xFFu);
    const uint32_t passes = 64;
    uint64_t start = sys_get_ticks();
    uint64_t sum = 0;
    for (uint32_t p = 0; p < passes; ++p) {
        // Seq write/read
        for (uint32_t i = 0; i < size; ++i) v[i] = (uint8_t)((i + p * 257) & 0xFFu);
        for (uint32_t i = 0; i < size; ++i) sum += v[i];
        // Random access sim (stride 1024)
        for (uint32_t i = 0; i < size; i += 1024) v[i] ^= 0xAAu;
        // Copy + reverse
        memcpy(tmp, buf, size);
        for (uint32_t i = 0; i < size / 2; ++i) {
            uint8_t t = tmp[i]; tmp[i] = tmp[size - 1 - i]; tmp[size - 1 - i] = t;
        }
        memcpy(buf, tmp, size);
    }
    uint64_t elapsed = sys_get_ticks() - start;
    if (elapsed == 0) elapsed = 1;
    uint64_t bytes = (uint64_t)size * passes * 6ull; // w/r/rand/copy/rev/copy
    uint64_t bytes_per_sec = bytes * hz / elapsed;
    uint64_t gib_per_sec = bytes_per_sec / (1024ull * 1024ull * 1024ull);
    snprintf(out, cap, "last: %.1f GiB/s", (double)gib_per_sec);
    free(buf); free(tmp);
    (void)sum;
}

static void bench_gfx(window_t id, int w, int h, char* out, size_t cap) {
    uint64_t hz = get_hz();
    uint32_t seed = 0xC001D00Du;
    uint64_t start = sys_get_ticks();
    uint64_t end = start + hz * 20u;
    uint64_t frames = 0;
    while (sys_get_ticks() < end) {
        if (window_should_close(id)) {
            snprintf(out, cap, "aborted");
            return;
        }
        window_clear(id, 0xFF0B1119u);
        for (int i = 0; i < 220; ++i) {
            seed = seed * 1664525u + 1013904223u;
            int x = (int)(seed & 0x3FFu) % (w - 20);
            seed = seed * 1664525u + 1013904223u;
            int y = (int)(seed & 0x3FFu) % (h - 60);
            seed = seed * 1664525u + 1013904223u;
            int rw = 6 + (int)(seed & 0x1Fu);
            seed = seed * 1664525u + 1013904223u;
            int rh = 6 + (int)(seed & 0x1Fu);
            uint32_t c = 0xFF000000u | (seed & 0x00FFFFFFu);
            window_draw_rect(id, x + 8, y + 32, rw, rh, c, 1);
        }
        window_draw_text(id, 12, 8, 0xFFBFD7FFu, "GFX benchmark running...");
        window_present(id);
        frames++;
    }
    uint64_t elapsed = sys_get_ticks() - start;
    if (elapsed == 0) elapsed = 1;
    uint64_t fps = frames * hz / elapsed;
    snprintf(out, cap, "last: %llu FPS", (unsigned long long)fps);
}

static void bench_sort(window_t id, int w, int h, char* out, size_t cap) {
    const int count = 8192;
    int vals[count];
    uint32_t seed = 0xA3C59AC3u;
    int chart_x = 16;
    int chart_y = 40;
    int chart_w = w - 32;
    int chart_h = h - 80;
    if (chart_h < 120) chart_h = 120;

    uint64_t hz = get_hz();
    uint64_t start = sys_get_ticks();
    uint64_t end = start + hz * 20u;
    int passes = 0;
    while (sys_get_ticks() < end) {
        if (window_should_close(id)) {
            snprintf(out, cap, "aborted");
            return;
        }
        // Ultra worst-case: reverse sorted multiple times for O(n^2) torture
        for (int i = 0; i < count; ++i) vals[i] = count - 1 - i;
        // Rotate/shuffle to stay bad
        int rot = (seed & 1023);
        for (int i = 0; i < rot; ++i) {
            int tmp = vals[count - 1];
            for (int j = count - 1; j > 0; --j) vals[j] = vals[j - 1];
            vals[0] = tmp;
        }
        // Pure bubble sort hell: full N^2 passes
        for (int outer = 0; outer < count / 4; ++outer) { // Multiple full bubbles
            int swapped;
            do {
                swapped = 0;
                for (int j = 1; j < count - outer; ++j) {
                    if (vals[j - 1] > vals[j]) {
                        int tmp = vals[j - 1];
                        vals[j - 1] = vals[j];
                        vals[j] = tmp;
                        swapped = 1;
                    }
                }
            } while (swapped);
        }
        // Viz: super-grouped for large N
        if (passes % 5 == 0) {
            window_clear(id, 0xFF0B1119u);
            window_draw_text(id, 12, 8, 0xFFBFD7FFu, "Sort benchmark running...");
            int groups = 128;
            int group_w = chart_w / groups;
            if (group_w < 2) group_w = 2;
            for (int g = 0; g < groups; ++g) {
                int gstart = g * (count / groups);
                int gend = (g + 1) * (count / groups);
                int avg = 0, cnt = 0;
                for (int k = gstart; k < gend; ++k) {
                    avg += vals[k];
                    cnt++;
                }
                if (cnt) avg /= cnt;
                int bh = (avg * chart_h) / 255;
                int x = chart_x + g * group_w;
                int y = chart_y + (chart_h - bh);
                uint32_t col = 0xFF000000u | ((uint32_t)avg * 0x101 | ((uint32_t)avg * 0x2020 >> 8) | ((uint32_t)avg * 0xAA << 16));
                window_draw_rect(id, x, y, group_w - 1, bh, col, 1);
            }
            window_draw_text(id, chart_x, chart_y - 12, 0xFF56E6AAu, "Ultra-hard sort!");
            window_present(id);
        }
        passes++;
        seed = seed * 1664525u + 1013904223u;
    }

    uint64_t elapsed = sys_get_ticks() - start;
    if (elapsed == 0) elapsed = 1;
    uint64_t ms = (elapsed * 1000u) / hz;
    snprintf(out, cap, "sort: %d elems x %d passes (ultra-hard) %llu ms", count, passes, (unsigned long long)ms);
}

static float fabsf_local(float v) {
    return v < 0.0f ? -v : v;
}

static float fast_sin(float x) {
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318531f;
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    float y = (4.0f / PI) * x + (-4.0f / (PI * PI)) * x * fabsf_local(x);
    return 0.225f * (y * fabsf_local(y) - y) + y;
}

static float fast_cos(float x) {
    const float PI_HALF = 1.57079632f;
    return fast_sin(x + PI_HALF);
}

static void bench_3d(window_t id, int w, int h, char* out, size_t cap) {
    const float cube[8][3] = {
        {-1.f, -1.f, -1.f},
        { 1.f, -1.f, -1.f},
        { 1.f,  1.f, -1.f},
        {-1.f,  1.f, -1.f},
        {-1.f, -1.f,  1.f},
        { 1.f, -1.f,  1.f},
        { 1.f,  1.f,  1.f},
        {-1.f,  1.f,  1.f}
    };
    const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    uint64_t hz = get_hz();
    uint64_t start = sys_get_ticks();
    uint64_t end = start + hz * 12u;
    uint64_t frames = 0;
    float ang = 0.0f;

    while (sys_get_ticks() < end) {
        if (window_should_close(id)) {
            snprintf(out, cap, "aborted");
            return;
        }
        window_clear(id, 0xFF0B1119u);
        float ax = ang * 0.8f;
        float ay = ang * 1.1f;
        float sx = fast_sin(ax);
        float cx = fast_cos(ax);
        float sy = fast_sin(ay);
        float cy = fast_cos(ay);
        int px[8];
        int py[8];

        for (int i = 0; i < 8; ++i) {
            float x = cube[i][0];
            float y = cube[i][1];
            float z = cube[i][2];
            float y1 = y * cx - z * sx;
            float z1 = y * sx + z * cx;
            float x2 = x * cy + z1 * sy;
            float z2 = -x * sy + z1 * cy;
            float dist = 3.2f;
            float inv = 1.0f / (z2 + dist);
            float sxp = x2 * inv;
            float syp = y1 * inv;
            px[i] = (int)(w * 0.5f + sxp * (float)w * 0.35f);
            py[i] = (int)(h * 0.5f + syp * (float)w * 0.35f);
        }

        for (int e = 0; e < 12; ++e) {
            int a = edges[e][0];
            int b = edges[e][1];
            window_draw_line(id, px[a], py[a], px[b], py[b], 0xFF56E6AAu);
        }
        /* Extra load: draw 2 more scaled cubes + radial spokes. */
        for (int s = 0; s < 2; ++s) {
            float scale = (s == 0) ? 0.65f : 0.4f;
            int tx = (s == 0) ? -w / 6 : w / 6;
            int ty = (s == 0) ? h / 10 : -h / 12;
            int spx[8];
            int spy[8];
            for (int i = 0; i < 8; ++i) {
                spx[i] = (int)((float)px[i] * scale) + tx;
                spy[i] = (int)((float)py[i] * scale) + ty;
            }
            for (int e = 0; e < 12; ++e) {
                int a = edges[e][0];
                int b = edges[e][1];
                window_draw_line(id, spx[a], spy[a], spx[b], spy[b], 0xFF2CC8FFu);
            }
        }
        int cxp = w / 2;
        int cyp = h / 2;
        for (int i = 0; i < 18; ++i) {
            float a = ang + (float)i * 0.349f;
            int rx = (int)((float)w * 0.32f * fast_cos(a));
            int ry = (int)((float)w * 0.20f * fast_sin(a));
            window_draw_line(id, cxp, cyp, cxp + rx, cyp + ry, 0xFF9FD7FFu);
        }
        window_draw_text(id, 12, 8, 0xFFBFD7FFu, "3D benchmark running...");
        window_present(id);
        ang += 0.06f;
        frames++;
    }

    uint64_t elapsed = sys_get_ticks() - start;
    if (elapsed == 0) elapsed = 1;
    uint64_t fps = frames * hz / elapsed;
    snprintf(out, cap, "3D: %llu FPS", (unsigned long long)fps);
}

void ntux_user_entry(void) {
    window_t id = 0x42454E434800ull; /* "BENCH" */
    int w = 720, h = 440;
    if (window_init() != 0 || window_create(id, 120, 90, w, h, 0xFF0B1119u, "Benchmark") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/bench.bmp");

    char status[96] = "";
    char last_cpu[96] = "";
    char last_mem[96] = "";
    char last_gfx[96] = "";
    char last_sort[96] = "";
    char last_3d[96] = "";

    for (;;) {
        if (window_should_close(id)) break;
        if (key_edge(0x01)) break; /* Esc */
        long ch = sys_getchar();
        if (ch == '1') {
            strncpy(status, "Running CPU benchmark...", sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0';
            draw_ui(id, w, h, status, last_cpu, last_mem, last_gfx);
            bench_cpu(last_cpu, sizeof(last_cpu));
            strncpy(status, "CPU benchmark done.", sizeof(status) - 1);
        } else if (ch == '2') {
            strncpy(status, "Running memory benchmark...", sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0';
            draw_ui(id, w, h, status, last_cpu, last_mem, last_gfx);
            bench_mem(last_mem, sizeof(last_mem));
            strncpy(status, "Memory benchmark done.", sizeof(status) - 1);
        } else if (ch == '3') {
            strncpy(status, "Running GFX benchmark...", sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0';
            draw_ui(id, w, h, status, last_cpu, last_mem, last_gfx);
            bench_gfx(id, w, h, last_gfx, sizeof(last_gfx));
            strncpy(status, "GFX benchmark done.", sizeof(status) - 1);
        } else if (ch == '4') {
            strncpy(status, "Running sort benchmark...", sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0';
            draw_ui(id, w, h, status, last_cpu, last_mem, last_gfx);
            bench_sort(id, w, h, last_sort, sizeof(last_sort));
            strncpy(status, last_sort, sizeof(status) - 1);
        } else if (ch == '5') {
            strncpy(status, "Running 3D benchmark...", sizeof(status) - 1);
            status[sizeof(status) - 1] = '\0';
            draw_ui(id, w, h, status, last_cpu, last_mem, last_gfx);
            bench_3d(id, w, h, last_3d, sizeof(last_3d));
            strncpy(status, last_3d, sizeof(status) - 1);
        }

        status[sizeof(status) - 1] = '\0';
        draw_ui(id, w, h, status, last_cpu, last_mem, last_gfx);
        sys_wait_ticks(1);
    }

    sys_exit(0);
}

#include <window.h>

#include <stdint.h>
#include <stdio.h>
#include <syscall.h>
#include <unistd.h>

typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    uint32_t color;
} star_t;

static uint32_t rand_next(uint32_t* state) {
    *state = (*state * 1103515245u + 12345u);
    return *state;
}

void ntux_user_entry(void) {
    uint64_t id = 0x4445534B44454D4Full;
    if (window_init() != 0 || window_create(id, 120, 90, 520, 300, 0xFF0A121Cu, "DeskDemo Screensaver") != 0) {
        puts("[deskdemo] window_create failed");
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/deskdemo.bmp");

    const int area_x = 16;
    const int area_y = 20;
    const int area_w = 488;
    const int area_h = 240;
    const int star_count = 32;
    star_t stars[32];
    uint32_t seed = (uint32_t)sys_get_ticks() ^ 0xC0FFEEu;

    for (int i = 0; i < star_count; ++i) {
        stars[i].x = area_x + (int)(rand_next(&seed) % (uint32_t)(area_w - 8));
        stars[i].y = area_y + (int)(rand_next(&seed) % (uint32_t)(area_h - 8));
        stars[i].dx = ((int)(rand_next(&seed) % 3) + 1) * ((rand_next(&seed) & 1u) ? 1 : -1);
        stars[i].dy = ((int)(rand_next(&seed) % 3) + 1) * ((rand_next(&seed) & 1u) ? 1 : -1);
        stars[i].color = 0xFF5BC0FFu + (rand_next(&seed) & 0x1Fu);
    }

    int logo_x = area_x + 80;
    int logo_y = area_y + 60;
    int logo_dx = 2;
    int logo_dy = 2;
    uint64_t frame = 0;

    for (;;) {
        if (window_should_close(id)) break;
        if (sys_kbd_is_pressed(0x01) > 0) break;

        (void)window_clear(id, 0xFF0A121Cu);
        (void)window_draw_rect(id, area_x - 2, area_y - 2, area_w + 4, area_h + 4, 0xFF1D3249u, 0);

        for (int i = 0; i < star_count; ++i) {
            stars[i].x += stars[i].dx;
            stars[i].y += stars[i].dy;
            if (stars[i].x < area_x || stars[i].x > area_x + area_w - 4) stars[i].dx = -stars[i].dx;
            if (stars[i].y < area_y || stars[i].y > area_y + area_h - 4) stars[i].dy = -stars[i].dy;
            (void)window_draw_rect(id, stars[i].x, stars[i].y, 3, 3, stars[i].color, 1);
        }

        logo_x += logo_dx;
        logo_y += logo_dy;
        if (logo_x < area_x + 6 || logo_x > area_x + area_w - 80) logo_dx = -logo_dx;
        if (logo_y < area_y + 6 || logo_y > area_y + area_h - 20) logo_dy = -logo_dy;

        (void)window_draw_text(id, logo_x, logo_y, 0xFFEAF4FFu, "NTux");
        (void)window_draw_text(id, logo_x, logo_y + 12, 0xFF9ED1FFu, "Screensaver");

        char info[96];
        snprintf(info, sizeof(info), "frame=%llu tick=%llu  ESC to exit",
            (unsigned long long)frame, (unsigned long long)sys_get_ticks());
        (void)window_draw_text(id, area_x + 6, area_y + area_h + 6, 0xFFBFD7FFu, info);
    
        (void)window_present(id);
        frame++;
        usleep(33 * 1000);
    }

    sys_exit(0);
}

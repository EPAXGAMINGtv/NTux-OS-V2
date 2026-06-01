#include <drivers/input/input.h>

#include <drivers/ps2/keyboard.h>
#include <drivers/ps2/mouse.h>
#include <drivers/ps2/ringbuffer.h>
#include <lib/string.h>

static int g_input_init_done;
static ringbuffer_t g_input_chars;
static uint8_t g_ps2_key_down[128];
static input_event_t g_evbuf[128];
static volatile uint32_t g_ev_head;
static volatile uint32_t g_ev_tail;


static void input_init_once(void) {
    if (g_input_init_done) return;
    rb_init(&g_input_chars);
    for (int i = 0; i < 128; ++i) {
        g_ps2_key_down[i] = 0;
    }
    g_ev_head = 0;
    g_ev_tail = 0;
    g_input_init_done = 1;
}

void input_poll(void) {
    input_init_once();
    keyboard_poll();

    char c = 0;
    while (keyboard_getchar(&c) != 0) {
        rb_put(&g_input_chars, c);
    }

    for (uint8_t k = 0; k < 128; ++k) {
        g_ps2_key_down[k] = keyboard_is_key_pressed(k) ? 1u : 0u;
    }

    int budget = 256;
    while (budget-- > 0 && mouse_data()) {
        mouse_poll();
    }
}

int input_try_getchar(char* out) {
    if (!out) return 0;
    input_poll();
    return rb_get(&g_input_chars, out);
}

int input_key_pressed(uint8_t key) {
    input_init_once();
    if (key >= 128) return 0;
    input_poll();
    return g_ps2_key_down[key] != 0u;
}

void input_copy_key_state(uint8_t* out, size_t len) {
    input_init_once();
    if (!out || len == 0) return;
    if (len > 128u) len = 128u;
    input_poll();
    for (size_t i = 0; i < len; ++i) {
        out[i] = g_ps2_key_down[i] != 0u ? 1u : 0u;
    }
}

int input_consume_super_press(void) {
    input_init_once();
    input_poll();
    return keyboard_consume_super_press();
}

void input_mouse_get_state(input_mouse_state_t* out, int bound_w, int bound_h) {
    if (!out) return;
    if (bound_w > 0 && bound_h > 0) {
        mouse_set_bounds(bound_w, bound_h);
    }
    input_poll();

    out->x = mouse_get_x();
    out->y = mouse_get_y();
    out->scroll = mouse_get_scroll();
    out->left = mouse_left_pressed() ? 1u : 0u;
    out->right = mouse_right_pressed() ? 1u : 0u;
    out->middle = mouse_middle_pressed() ? 1u : 0u;
}

void input_inject_scancode_set1(uint8_t scancode, bool pressed) {
    input_init_once();
    uint8_t key = (uint8_t)(scancode & 0x7Fu);
    if (key >= 128) return;
    g_ps2_key_down[key] = pressed ? 1u : 0u;
}

void input_inject_char(char c) {
    input_init_once();
    if (!c) return;
    rb_put(&g_input_chars, c);
}

void input_inject_mouse_report(int dx, int dy, int wheel, bool left, bool right, bool middle) {
    mouse_inject_report(dx, dy, wheel, left, right, middle);
}

static void evbuf_push(const input_event_t* ev) {
    uint32_t next = (g_ev_head + 1u) % (uint32_t)(sizeof(g_evbuf) / sizeof(g_evbuf[0]));
    if (next == g_ev_tail) {
        g_ev_tail = (g_ev_tail + 1u) % (uint32_t)(sizeof(g_evbuf) / sizeof(g_evbuf[0]));
    }
    g_evbuf[g_ev_head] = *ev;
    g_ev_head = next;
}

void input_evdev_push_key(uint8_t scancode, int pressed) {
    input_init_once();
    input_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = (uint16_t)scancode;
    ev.value = pressed ? 1 : 0;
    evbuf_push(&ev);
}

void input_evdev_push_mouse(int dx, int dy, int wheel, bool left, bool right, bool middle) {
    input_init_once();
    if (dx != 0) {
        input_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_REL;
        ev.code = REL_X;
        ev.value = dx;
        evbuf_push(&ev);
    }
    if (dy != 0) {
        input_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_REL;
        ev.code = REL_Y;
        ev.value = dy;
        evbuf_push(&ev);
    }
    if (wheel != 0) {
        input_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_REL;
        ev.code = REL_WHEEL;
        ev.value = wheel;
        evbuf_push(&ev);
    }
    {
        input_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_KEY;
        ev.code = BTN_LEFT;
        ev.value = left ? 1 : 0;
        evbuf_push(&ev);
    }
    {
        input_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_KEY;
        ev.code = BTN_RIGHT;
        ev.value = right ? 1 : 0;
        evbuf_push(&ev);
    }
    {
        input_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_KEY;
        ev.code = BTN_MIDDLE;
        ev.value = middle ? 1 : 0;
        evbuf_push(&ev);
    }
}

int input_evdev_pop(input_event_t* out) {
    input_init_once();
    if (!out) return -1;
    if (g_ev_tail == g_ev_head) return 0;
    *out = g_evbuf[g_ev_tail];
    g_ev_tail = (g_ev_tail + 1u) % (uint32_t)(sizeof(g_evbuf) / sizeof(g_evbuf[0]));
    return 1;
}

#include "keyboard.h"
#include <drivers/input/input.h>
#include <arch/x86_64/io.h>
#include <interrupt/irq.h>
#include <interrupt/pic.h>
#include <drivers/framebuffer/kprint.h>
#include "ps2.h"
#include "ringbuffer.h"
#include "scancode_tables.h"

static const char* g_scancode_ascii = scancode_ascii_de;
static const char* g_scancode_ascii_shift = scancode_ascii_shift_de;
static int g_layout_idx = 1;


ringbuffer_t kb_buffer;
uint8_t shift_pressed = 0;
uint8_t ctrl_pressed  = 0;
uint8_t alt_pressed   = 0;
uint8_t super_pressed = 0;
static uint8_t super_press_event = 0;
static uint8_t extended_scancode = 0;
static uint8_t key_down[128];
static uint8_t key_press_event[128];
static volatile uint64_t g_keyboard_irq_hits = 0;

static void ps2_wait_input_clear(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((inb(0x64) & 0x02) == 0) return;
        __asm__ volatile("pause");
    }
}

__attribute__((unused)) static void ps2_flush_output(void) {
    for (int i = 0; i < 256; ++i) {
        if ((inb(0x64) & 0x01) == 0) return;
        (void)inb(0x60);
    }
}

__attribute__((unused)) static void ps2_send_kbd_cmd(uint8_t cmd) {
    ps2_wait_input_clear();
    outb(0x60, cmd);
}

__attribute__((unused)) static uint8_t kbd_ps2_read_config(void) {
    ps2_wait_input_clear();
    outb(0x64, 0x20);
    for (int i = 0; i < 100000; ++i) {
        if (inb(0x64) & 0x01) {
            return inb(0x60);
        }
        __asm__ volatile("pause");
    }
    return 0;
}

__attribute__((unused)) static void kbd_ps2_write_config(uint8_t cfg) {
    ps2_wait_input_clear();
    outb(0x64, 0x60);
    ps2_wait_input_clear();
    outb(0x60, cfg);
}

static void keyboard_apply_scancode(uint8_t key, bool released, bool extended) {
    uint8_t was_down = (key < 128) ? key_down[key] : 0;
    if (key < 128) {
    key_down[key] = released ? 0 : 1;
    input_evdev_push_key(key, released ? 0 : 1);
        if (!released) key_press_event[key] = 1;
    }

    if (key == 0x5B || key == 0x5C) {
        super_pressed = !released;
        if (!released) super_press_event = 1;
    }

    if (extended) {
        if (key == 0x1D) ctrl_pressed = !released;
        if (key == 0x38) alt_pressed = !released;
        return;
    }

    if (key == 0x2A || key == 0x36) shift_pressed = !released;
    if (key == 0x1D) ctrl_pressed = !released;
    if (key == 0x38) alt_pressed = !released;
    if (!released && !was_down) {
        char c = shift_pressed ? g_scancode_ascii_shift[key] : g_scancode_ascii[key];
        if (ctrl_pressed) {
            if (c >= 'a' && c <= 'z') {
                c = (char)(c & 0x1F);
            } else if (c >= 'A' && c <= 'Z') {
                c = (char)(c & 0x1F);
            }
        }
        if (c) rb_put(&kb_buffer, c);
    }
}

void keyboard_poll() {
    
    int poll_budget = 256;
    while (poll_budget-- > 0) {
        uint8_t status = inb(0x64);
        if ((status & 0x01) == 0) {
            return;
        }
        if (status & 0x20) {
            (void)inb(0x60);
            continue;
        }
        uint8_t scancode = inb(0x60);
        if (scancode == 0xE0) {
            extended_scancode = 1;
            continue;
        }

        uint8_t released = scancode & 0x80;
        uint8_t key = scancode & 0x7F;
        if (extended_scancode) {
            keyboard_apply_scancode(key, released != 0, true);
            extended_scancode = 0;
            continue;
        }
        keyboard_apply_scancode(key, released != 0, false);
    }
}

int keyboard_getchar(char* c) {
    return rb_get(&kb_buffer, c);
}

int keyboard_is_key_pressed(uint8_t key) {
    if (key == SCANCODE_LEFT_SHIFT || key == SCANCODE_RIGHT_SHIFT) {
        return shift_pressed;
    }
    if (key == SCANCODE_CTRL) {
        return ctrl_pressed;
    }
    if (key == SCANCODE_ALT) {
        return alt_pressed;
    }
    if (key < 128) return key_down[key] != 0;
    return 0;
}

void keyboard_irq_handler(void) {
    g_keyboard_irq_hits++;
    keyboard_poll();       
}

uint64_t keyboard_get_irq_hits(void) {
    return g_keyboard_irq_hits;
}

int keyboard_set_layout(const char* name) {
    int idx = scancode_layout_index(name);
    if (idx < 0) return -1;
    g_layout_idx = idx;
    g_scancode_ascii = g_scancode_layouts[idx].table;
    g_scancode_ascii_shift = g_scancode_layouts[idx].table_shift;
    kprint_serial_only("[KBD] layout switched\n");
    return 0;
}

const char* keyboard_get_layout_name(void) {
    if (g_layout_idx < 0 || g_layout_idx >= g_scancode_layout_count) return "de";
    return g_scancode_layouts[g_layout_idx].name;
}

void keyboard_init() {
    rb_init(&kb_buffer);
    for (int i = 0; i < 128; ++i) {
        key_down[i] = 0;
        key_press_event[i] = 0;
    }

    if (ps2_write_device(1, 0xF4)) {
        kprint_ok("[KBD] PS/2 keyboard scanning enabled\n");
    } else {
        kprint_error("[KBD] PS/2 keyboard scanning enable failed\n");
    }

    /* Set typematic rate: 1000ms delay, ~6.15 chars/sec (max delay) */
    ps2_write_device(1, 0xF3);
    ps2_write_device(1, 0x3F);

    irq_register_handler(1, keyboard_irq_handler);
    pic_clear_mask(1);
}

int keyboard_consume_super_press(void) {
    if (!super_press_event) return 0;
    super_press_event = 0;
    return 1;
}

int keyboard_consume_key_press(uint8_t key) {
    if (key >= 128) return 0;
    if (!key_press_event[key]) return 0;
    key_press_event[key] = 0;
    return 1;
}

void keyboard_inject_scancode_set1(uint8_t scancode, bool pressed) {
    uint8_t key = (uint8_t)(scancode & 0x7Fu);
    if (key >= 128) return;
    keyboard_apply_scancode(key, !pressed, false);
}

void keyboard_inject_char(char c) {
    if (c) rb_put(&kb_buffer, c);
}

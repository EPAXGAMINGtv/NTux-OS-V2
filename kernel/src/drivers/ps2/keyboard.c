#include "keyboard.h"
#include <drivers/input/input.h>
#include <arch/x86_64/io.h>
#include <interrupt/irq.h>
#include <interrupt/pic.h>
#include <drivers/framebuffer/kprint.h>
#include "ps2.h"
#include "ringbuffer.h"
#include "scancode_table_de.h"


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

static void kb_put_seq(const char* s) {
    if (!s) return;
    while (*s) {
        rb_put(&kb_buffer, *s++);
    }
}

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
        if (!released) {
            switch (key) {
                case 0x48: kb_put_seq("\x1b[A"); return; /* Up */
                case 0x50: kb_put_seq("\x1b[B"); return; /* Down */
                case 0x4D: kb_put_seq("\x1b[C"); return; /* Right */
                case 0x4B: kb_put_seq("\x1b[D"); return; /* Left */
                case 0x47: kb_put_seq("\x1b[H"); return; /* Home */
                case 0x4F: kb_put_seq("\x1b[F"); return; /* End */
                case 0x49: kb_put_seq("\x1b[5~"); return; /* Page Up */
                case 0x51: kb_put_seq("\x1b[6~"); return; /* Page Down */
                case 0x52: kb_put_seq("\x1b[2~"); return; /* Insert */
                case 0x53: kb_put_seq("\x1b[3~"); return; /* Delete */
                default: break;
            }
        }
        return;
    }

    if (key == 0x2A || key == 0x36) shift_pressed = !released;
    if (key == 0x1D) ctrl_pressed = !released;
    if (key == 0x38) alt_pressed = !released;
    if (!released) {
        char c = shift_pressed ? scancode_ascii_shift[key] : scancode_ascii[key];
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

#include <drivers/ps2/mouse.h>
#include <drivers/ps2/ps2.h>
#include <arch/x86_64/io.h>
#include <interrupt/irq.h>
#include <interrupt/pic.h>
#include <drivers/gpu/graphics.h>
#include <drivers/framebuffer/kprint.h>
#include <drivers/ps2/ringbuffer.h>
#include <drivers/input/input.h>

#define PS2_STATUS 0x64
#define PS2_DATA 0x60


static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[4];
static uint8_t mouse_packet_size = 3;
static bool mouse_has_wheel = false;

int mouse_X = 100;
int mouse_y = 100;
static int mouse_scroll = 0;
int mouse_left = 0;
int mouse_right = 0;
int mouse_middle = 0;

static int mouse_bound_w = 640;
static int mouse_bound_h = 480;

static inline bool mouse_wait_read(void) {
    for (int i = 0; i < 100000; ++i) {
        if (inb(PS2_STATUS) & 1) {
            return true;
        }
    }
    return false;
}

static inline bool mouse_wait_write(void) {
    for (int i = 0; i < 100000; ++i) {
        if ((inb(PS2_STATUS) & 2) == 0) {
            return true;
        }
    }
    return false;
}

static inline void ps2_flush_output(void) {
    for (int i = 0; i < 256; ++i) {
        if ((inb(PS2_STATUS) & 0x01) == 0) return;
        (void)inb(PS2_DATA);
    }
}

static inline uint8_t mouse_read() {
    return inb(PS2_DATA);
}

static inline bool mouse_has_data() {
    uint8_t status = inb(PS2_STATUS);
    return ((status & 1) != 0) && ((status & 0x20) != 0);
}

static inline bool mouse_has_data_any() {
    return (inb(PS2_STATUS) & 1) != 0;
}

static bool mouse_write_device(uint8_t value) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!mouse_wait_write()) return false;
        outb(PS2_STATUS, 0xD4);
        if (!mouse_wait_write()) return false;
        outb(PS2_DATA, value);
        if (!mouse_wait_read()) return false;
        uint8_t ack = inb(PS2_DATA);
        if (ack == 0xFA) return true; /* ACK */
        if (ack != 0xFE) return false; /* not RESEND */
    }
    return false;
}

static uint8_t mouse_read_data_byte(void) {
    if (!mouse_wait_read()) return 0;
    return inb(PS2_DATA);
}

static bool mouse_get_device_id(uint8_t* out_id) {
    if (!out_id) return false;
    if (!mouse_write_device(0xF2)) return false;
    *out_id = mouse_read_data_byte();
    return true;
}

static void mouse_try_enable_wheel(void) {
    mouse_has_wheel = false;
    mouse_packet_size = 3;

    
    if (!mouse_write_device(0xF3)) return;
    if (!mouse_write_device(200)) return;
    if (!mouse_write_device(0xF3)) return;
    if (!mouse_write_device(100)) return;
    if (!mouse_write_device(0xF3)) return;
    if (!mouse_write_device(80)) return;

    uint8_t id = 0;
    if (!mouse_get_device_id(&id)) return;
    if (id == 3 || id == 4) {
        mouse_has_wheel = true;
        mouse_packet_size = 4;
        kprint("PS/2 mouse wheel enabled\n");
    }
}

void mouse_set_bounds(int width, int height) {
    if (width > 0) mouse_bound_w = width;
    if (height > 0) mouse_bound_h = height;

    if (mouse_X < 0) mouse_X = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_X >= mouse_bound_w) mouse_X = mouse_bound_w - 1;
    if (mouse_y >= mouse_bound_h) mouse_y = mouse_bound_h - 1;
}

bool mouse_init(void) {
    if (!ps2_is_dual_channel()) {
        /* Some laptops expose the touchpad via the aux device even if the controller
           reports only a single channel. Try anyway and enable aux IRQ if it responds. */
        kprint("[MOUSE] No second PS/2 port reported, probing aux device...\n");
        ps2_write_command(PS2_CMD_ENABLE_PORT2);
        uint8_t cfg = ps2_read_config();
        cfg |= PS2_CFG_PORT2_INT;
        cfg &= ~PS2_CFG_PORT2_DISABLED;
        ps2_write_config(cfg);
    }

    if (!ps2_write_device(2, 0xF6)) {
        kprint_error("[MOUSE] PS/2 mouse reset defaults failed\n");
        return false;
    }
    if (!ps2_write_device(2, 0xF4)) {
        kprint_error("[MOUSE] PS/2 mouse enable stream failed\n");
        return false;
    }

    mouse_cycle = 0;
    mouse_scroll = 0;
    mouse_set_bounds((int)gpu_get_width(), (int)gpu_get_height());
    mouse_try_enable_wheel();
    irq_register_handler(12, mouse_irq_handler);
    pic_clear_mask(2);
    pic_clear_mask(12);
    return true;
}

static void mouse_process_byte(uint8_t data) {
    switch (mouse_cycle) {
        case 0:
            if ((data & 0x08) == 0) {
                return;
            }
            mouse_bytes[0] = data;
            mouse_cycle = 1;
            break;
        case 1:
            mouse_bytes[1] = data;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_bytes[2] = data;
            if (mouse_packet_size == 4) {
                mouse_cycle = 3;
                break;
            }
            mouse_cycle = 0;

            {
                int dx = (int8_t)mouse_bytes[1];
                int dy = (int8_t)mouse_bytes[2];
                /* If overflow is flagged, clamp to a sane value instead of dropping. */
                if (mouse_bytes[0] & 0x40) dx = (dx < 0) ? -127 : 127;
                if (mouse_bytes[0] & 0x80) dy = (dy < 0) ? -127 : 127;

                mouse_X += dx;
                mouse_y -= dy;
            }

            if (mouse_X < 0) mouse_X = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_X >= mouse_bound_w) mouse_X = mouse_bound_w - 1;
            if (mouse_y >= mouse_bound_h) mouse_y = mouse_bound_h - 1;

            mouse_left = (mouse_bytes[0] & 0x01) != 0;
            mouse_right = (mouse_bytes[0] & 0x02) != 0;
            mouse_middle = (mouse_bytes[0] & 0x04) != 0;
            input_evdev_push_mouse((int8_t)mouse_bytes[1], (int8_t)mouse_bytes[2], 0,
                                   mouse_left, mouse_right, mouse_middle);
            break;
        default:
            mouse_bytes[3] = data;
            mouse_cycle = 0;

            {
                int dx = (int8_t)mouse_bytes[1];
                int dy = (int8_t)mouse_bytes[2];
                if (mouse_bytes[0] & 0x40) dx = (dx < 0) ? -127 : 127;
                if (mouse_bytes[0] & 0x80) dy = (dy < 0) ? -127 : 127;

                mouse_X += dx;
                mouse_y -= dy;
            }

            int wheel = 0;
            if (mouse_has_wheel) {
                int8_t z = (int8_t)(mouse_bytes[3] << 4);
                z >>= 4;
                mouse_scroll += (int)z;
                wheel = (int)z;
            }

            if (mouse_X < 0) mouse_X = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_X >= mouse_bound_w) mouse_X = mouse_bound_w - 1;
            if (mouse_y >= mouse_bound_h) mouse_y = mouse_bound_h - 1;

            mouse_left = (mouse_bytes[0] & 0x01) != 0;
            mouse_right = (mouse_bytes[0] & 0x02) != 0;
            mouse_middle = (mouse_bytes[0] & 0x04) != 0;
            input_evdev_push_mouse((int8_t)mouse_bytes[1], (int8_t)mouse_bytes[2], wheel,
                                   mouse_left, mouse_right, mouse_middle);
            break;
    }
}

void mouse_poll() {
    if (!mouse_has_data()) return;
    mouse_process_byte(mouse_read());
}

void mouse_irq_handler(void) {
    int budget = 64;
    while (budget-- > 0 && mouse_has_data_any()) {
        mouse_process_byte(mouse_read());
    }
}


void draw_mouse_cursor(void) {
    mouse_set_bounds((int)gpu_get_width(), (int)gpu_get_height());
    gpu_put_pixel(mouse_X, mouse_y, 0xFFFFFFFF);
    gpu_put_pixel(mouse_X + 1, mouse_y, 0xFFFFFFFF);
    gpu_put_pixel(mouse_X, mouse_y + 1, 0xFFFFFFFF);
}

int mouse_get_x(void) { return mouse_X; }
int mouse_get_y(void) { return mouse_y; }
bool mouse_left_pressed(void) { return mouse_left != 0; }
bool mouse_right_pressed(void) { return mouse_right != 0; }
bool mouse_middle_pressed(void) { return mouse_middle != 0; }
int mouse_get_scroll(void) {
    int value = mouse_scroll;
    mouse_scroll = 0;
    return value;
}
bool mouse_data() {
    return mouse_has_data();
}

void mouse_inject_report(int dx, int dy, int wheel, bool left, bool right, bool middle) {
    mouse_X += dx;
    mouse_y -= dy;
    if (mouse_X < 0) mouse_X = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_X >= mouse_bound_w) mouse_X = mouse_bound_w - 1;
    if (mouse_y >= mouse_bound_h) mouse_y = mouse_bound_h - 1;

    mouse_scroll += wheel;
    mouse_left = left ? 1 : 0;
    mouse_right = right ? 1 : 0;
    mouse_middle = middle ? 1 : 0;
}

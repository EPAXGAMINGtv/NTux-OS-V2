#include "kprint.h"
#include <stdarg.h>
#include <drivers/gpu/gpu.h>
#include <interrupt/timer.h>
#include <arch/x86_64/io.h>
#include <drivers/gpu/graphics.h>

#define COM1_PORT 0x3F8

static uint8_t g_serial_init_done = 0;
static uint8_t g_serial_enabled = 1;
static uint8_t g_screen_enabled = 1;
static uint8_t g_user_stdout_serial_only = 0;

static void serial_init_once(void) {
    if (g_serial_init_done) return;
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x01);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
    g_serial_init_done = 1;
}

static void serial_write_char(char c) {
    if (!g_serial_enabled) return;
    serial_init_once();
    for (int i = 0; i < 1024; ++i) {
        if (inb(COM1_PORT + 5) & 0x20) {
            outb(COM1_PORT + 0, (uint8_t)c);
            return;
        }
    }
}

void kprint_serial_char(char c) {
    serial_write_char(c);
}

void kprint_serial_only(const char* text) {
    if (!text) return;
    const char* ptr = text;
    while (*ptr) {
        if (*ptr == '\n') {
            serial_write_char('\n');
            serial_write_char('\r');
        } else {
            serial_write_char(*ptr);
        }
        ptr++;
    }
}

kprint_t g_printer;
static uint32_t g_default_color = COLOR_WHITE;
static int g_ansi_state = 0;
static int g_ansi_params[8];
static int g_ansi_param_count = 0;
static int g_ansi_param_value = 0;
static int g_ansi_param_has_value = 0;
static int g_ansi_private = 0;
static int g_saved_cursor_x = 0;
static int g_saved_cursor_y = 0;

static void reverse(char* str, int len) {
    int i = 0, j = len - 1;
    while (i < j) {
        char t = str[i]; str[i++] = str[j]; str[j--] = t;
    }
}

static int itoa_unsigned(uint64_t num, char* str, int base) {
    int i = 0;
    if (num == 0) { str[i++] = '0'; str[i] = '\0'; return i; }
    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num /= base;
    }
    str[i] = '\0';
    reverse(str, i);
    return i;
}

static int itoa_signed(int64_t num, char* str) {
    int i = 0;
    if (num == 0) { str[i++] = '0'; str[i] = '\0'; return i; }
    if (num < 0) { str[i++] = '-'; num = -num; }
    i += itoa_unsigned((uint64_t)num, str + i, 10);
    str[i] = '\0';
    return i;
}

void itoa(int num, char* str, int base) {
    int i = 0;
    int isNegative = 0;
    if (num == 0) { str[i++] = '0'; str[i] = '\0'; return; }
    if (num < 0 && base == 10) { isNegative = 1; num = -num; }
    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }
    if (isNegative) str[i++] = '-';
    str[i] = '\0';
    int start = 0, end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++; end--;
    }
}

void init_kprint_global(cursor_t* cursor, uint32_t color) {
    g_printer.cursor = cursor;
    g_printer.color = color;
    g_default_color = color ? color : COLOR_WHITE;
}

static void kprint_erase_one(void) {
    if (!g_printer.cursor) return;

    if (g_printer.cursor->x > 0) {
        g_printer.cursor->x -= g_printer.cursor->char_width;
    } else if (g_printer.cursor->y > 0) {
        g_printer.cursor->y -= g_printer.cursor->char_height;
        g_printer.cursor->x = g_printer.cursor->screen_width - g_printer.cursor->char_width;
        if (g_printer.cursor->x < 0) g_printer.cursor->x = 0;
    } else {
        return;
    }

    gpu_fill_rect(g_printer.cursor->x, g_printer.cursor->y,
                  g_printer.cursor->char_width, g_printer.cursor->char_height,
                  COLOR_BLACK);
}

static void kprint_clamp_cursor(void) {
    if (!g_printer.cursor) return;
    if (g_printer.cursor->x < 0) g_printer.cursor->x = 0;
    if (g_printer.cursor->y < 0) g_printer.cursor->y = 0;
    if (g_printer.cursor->x >= g_printer.cursor->screen_width) {
        g_printer.cursor->x = g_printer.cursor->screen_width - g_printer.cursor->char_width;
        if (g_printer.cursor->x < 0) g_printer.cursor->x = 0;
    }
    if (g_printer.cursor->y >= g_printer.cursor->screen_height) {
        g_printer.cursor->y = g_printer.cursor->screen_height - g_printer.cursor->char_height;
        if (g_printer.cursor->y < 0) g_printer.cursor->y = 0;
    }
}

static void ansi_push_param(void) {
    if (g_ansi_param_count >= 8) {
        g_ansi_param_value = 0;
        g_ansi_param_has_value = 0;
        return;
    }
    g_ansi_params[g_ansi_param_count++] = g_ansi_param_has_value ? g_ansi_param_value : -1;
    g_ansi_param_value = 0;
    g_ansi_param_has_value = 0;
}

static int ansi_param_or(int idx, int def) {
    if (idx < 0 || idx >= g_ansi_param_count) return def;
    return (g_ansi_params[idx] < 0) ? def : g_ansi_params[idx];
}

static uint32_t ansi_color_basic(int code) {
    switch (code) {
        case 0: return COLOR_BLACK;
        case 1: return COLOR_RED;
        case 2: return COLOR_GREEN;
        case 3: return COLOR_YELLOW;
        case 4: return COLOR_BLUE;
        case 5: return COLOR_MAGENTA;
        case 6: return COLOR_CYAN;
        case 7: return COLOR_WHITE;
        default: return g_default_color;
    }
}

static uint32_t ansi_color_bright(int code) {
    switch (code) {
        case 0: return COLOR_DARK_GRAY;
        case 1: return COLOR_LIGHT_RED;
        case 2: return COLOR_LIGHT_GREEN;
        case 3: return COLOR_LIGHT_YELLOW;
        case 4: return COLOR_LIGHT_BLUE;
        case 5: return COLOR_LIGHT_MAGENTA;
        case 6: return COLOR_LIGHT_CYAN;
        case 7: return COLOR_LIGHT_GRAY;
        default: return g_default_color;
    }
}

static void ansi_apply_sgr(void) {
    if (g_ansi_param_count == 0) {
        g_printer.color = g_default_color;
        return;
    }
    for (int i = 0; i < g_ansi_param_count; ++i) {
        int p = g_ansi_params[i];
        if (p < 0) continue;
        if (p == 0) {
            g_printer.color = g_default_color;
        } else if (p >= 30 && p <= 37) {
            g_printer.color = ansi_color_basic(p - 30);
        } else if (p >= 90 && p <= 97) {
            g_printer.color = ansi_color_bright(p - 90);
        }
    }
}

static void ansi_handle_csi_final(char final) {
    if (!g_printer.cursor) return;
    int n = ansi_param_or(0, 1);
    switch (final) {
        case 'A':
            g_printer.cursor->y -= n * g_printer.cursor->char_height;
            break;
        case 'B':
            g_printer.cursor->y += n * g_printer.cursor->char_height;
            break;
        case 'C':
            g_printer.cursor->x += n * g_printer.cursor->char_width;
            break;
        case 'D':
            g_printer.cursor->x -= n * g_printer.cursor->char_width;
            break;
        case 'H':
        case 'f': {
            int row = ansi_param_or(0, 1);
            int col = ansi_param_or(1, 1);
            g_printer.cursor->y = (row > 0 ? row - 1 : 0) * g_printer.cursor->char_height;
            g_printer.cursor->x = (col > 0 ? col - 1 : 0) * g_printer.cursor->char_width;
            break;
        }
        case 'J': {
            int mode = ansi_param_or(0, 0);
            if (mode == 2 || mode == 0) {
                gpu_clear_screen(COLOR_BLACK);
                g_printer.cursor->x = 0;
                g_printer.cursor->y = 0;
            }
            break;
        }
        case 'K': {
            int mode = ansi_param_or(0, 0);
            if (mode == 0) {
                int x = g_printer.cursor->x;
                int y = g_printer.cursor->y;
                int w = g_printer.cursor->screen_width - x;
                int h = g_printer.cursor->char_height;
                if (w > 0 && h > 0) {
                    gpu_fill_rect(x, y, w, h, COLOR_BLACK);
                }
            }
            break;
        }
        case 'm':
            ansi_apply_sgr();
            break;
        case 's':
            g_saved_cursor_x = g_printer.cursor->x;
            g_saved_cursor_y = g_printer.cursor->y;
            break;
        case 'u':
            g_printer.cursor->x = g_saved_cursor_x;
            g_printer.cursor->y = g_saved_cursor_y;
            break;
        default:
            break;
    }
    kprint_clamp_cursor();
}

static void kprint_handle_screen_char(char ch) {
    if (!g_printer.cursor) return;
    if (g_ansi_state == 0) {
        if (ch == 0x1B) {
            g_ansi_state = 1;
            return;
        }
        if (ch == '\n') {
            g_printer.cursor->x = 0;
            g_printer.cursor->y += g_printer.cursor->char_height;
            return;
        }
        if (ch == '\r') {
            g_printer.cursor->x = 0;
            return;
        }
        if (ch == '\b' || ch == 127) {
            kprint_erase_one();
            return;
        }
        put_char_with_cursor(g_printer.cursor, ch, g_printer.color);
        return;
    }
    if (g_ansi_state == 1) {
        if (ch == '[') {
            g_ansi_state = 2;
            g_ansi_param_count = 0;
            g_ansi_param_value = 0;
            g_ansi_param_has_value = 0;
            g_ansi_private = 0;
            return;
        }
        g_ansi_state = 0;
        return;
    }
    if (g_ansi_state == 2) {
        if (ch >= '0' && ch <= '9') {
            g_ansi_param_value = g_ansi_param_value * 10 + (ch - '0');
            g_ansi_param_has_value = 1;
            return;
        }
        if (ch == ';') {
            ansi_push_param();
            return;
        }
        if (ch == '?') {
            g_ansi_private = 1;
            return;
        }
        ansi_push_param();
        (void)g_ansi_private;
        ansi_handle_csi_final(ch);
        g_ansi_state = 0;
        return;
    }
}

void kprint(const char* text) {
    const char* ptr = text;
    while (*ptr) {
        if (*ptr == '\n') {
            serial_write_char('\n');
            serial_write_char('\r');
        } else {
            serial_write_char(*ptr);
        }
        if (g_screen_enabled) {
            kprint_handle_screen_char(*ptr);
        }
        ptr++;
    }
    if (g_screen_enabled) {
        gpu_flush_all();
    }
}

void kprint_set_serial_enabled(int enabled) {
    g_serial_enabled = enabled ? 1u : 0u;
}

int kprint_get_serial_enabled(void) {
    return g_serial_enabled ? 1 : 0;
}

void kprint_set_screen_enabled(int enabled) {
    g_screen_enabled = enabled ? 1u : 0u;
}

int kprint_get_screen_enabled(void) {
    return g_screen_enabled ? 1 : 0;
}

void kprint_set_user_stdout_serial_only(int enabled) {
    g_user_stdout_serial_only = enabled ? 1u : 0u;
}

int kprint_get_user_stdout_serial_only(void) {
    return g_user_stdout_serial_only ? 1 : 0;
}

void kprintcolor(const char* text, uint32_t color) {
    const char* ptr = text;
    while (*ptr) {
        if (g_screen_enabled) {
            if (*ptr == '\n') {
                g_printer.cursor->x = 0;
                g_printer.cursor->y += g_printer.cursor->char_height;
            } else if (*ptr == '\r') {
                g_printer.cursor->x = 0;
            } else if (*ptr == '\b' || *ptr == 127) {
                kprint_erase_one();
            } else {
                put_char_with_cursor(g_printer.cursor, *ptr, color);
            }
        }
        ptr++;
    }
    if (g_screen_enabled) {
        gpu_flush_all();
    }
}

void kprint_int(int num) {
    char buffer[20];
    itoa(num, buffer, 10);
    kprint(buffer);
}

void kprint_uint(uint32_t num) {
    char buffer[20];
    itoa((int)num, buffer, 10);
    kprint(buffer);
}

void kprinthex(uint32_t num) {
    char buffer[20];
    itoa((int)num, buffer, 16);
    kprint("0x");
    kprint(buffer);
}

void kprint_hex64(uint64_t num) {
    char buffer[32];
    static const char* hex = "0123456789abcdef";
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        buffer[2 + i] = hex[(num >> (60 - (i * 4))) & 0xFu];
    }
    buffer[18] = '\0';
    kprint(buffer);
}

void kprintcolor_int32(int num, uint32_t color) {
    char buffer[20];
    itoa(num, buffer, 10);
    kprintcolor(buffer, color);
}

void kprintcolor_uint32(uint32_t num, uint32_t color) {
    char buffer[20];
    itoa((int)num, buffer, 10);
    kprintcolor(buffer, color);
}

void kprintcolorhex(uint32_t num, uint32_t color) {
    char buffer[20];
    itoa((int)num, buffer, 16);
    kprintcolor("0x", color);
    kprintcolor(buffer, color);
}

void kprint_ok(const char* text) {
    kprint(text);
    kprint(" ");
    kprint("[");
    kprintcolor("  OK  ", COLOR_GREEN);
    kprint("]\n");
}

void kprint_error(const char* text) {
    kprint(text);
    kprint(" ");
    kprint("[");
    kprintcolor(" ERROR ", COLOR_RED);
    kprint("]\n");
}

static const char* exception_names[] = {
    "Divide Error", "Debug Exception", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "BOUND Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check",
    "SIMD Floating-Point Exception", "Virtualization Exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Security Exception", "Reserved"
};

void trigger_blue_screen(uint64_t interrupt_number, uint64_t error_code) {
    gpu_clear_screen(COLOR_LIGHT_BLUE_SCREEN_BG);
    gpu_flush_all();
    g_printer.cursor->x = 3;
    g_printer.cursor->y = 3;
    kprint("=====================================\n");
    kprint("       KERNEL PANIC - NTux-OS       \n");
    kprint("=====================================\n\n");
    kprint("Exception: ");
    if (interrupt_number < 32) {
        kprint(exception_names[interrupt_number]);
    } else {
        kprint("Unknown");
    }
    kprint(" (");
    kprint_uint(interrupt_number);
    kprint(")\n\n");
    kprint("Error Code: ");
    kprint_hex64(error_code);
    kprint("\n\n");
    if (interrupt_number == 14) {
        uint64_t cr2 = 0;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kprint("CR2 (fault address): ");
        kprint_hex64(cr2);
        kprint("\n\n");
    }
    kprint(":( - Ein kritischer Fehler ist aufgetreten.\n");
    kprint("Das System wurde angehalten.\n\n");
    kprint("Bitte notieren Sie die Exception-Nummer\n");
    kprint("und starten Sie das System neu.\n\n");
    kprintcolor(" System wird in 10 Sekunden neustarten... ", COLOR_RED);
    kprint("\n");
    sleep_s(10);
}

void kprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[64];
    while (*format) {
        if (*format != '%') {
            char tmp[2] = {*format++, '\0'};
            kprint(tmp);
            continue;
        }
        format++;
        switch (*format) {
            case 'd':
            case 'i': {
                int val = va_arg(args, int);
                itoa_signed(val, buffer);
                kprint(buffer);
                break;
            }
            case 'u': {
                unsigned int val = va_arg(args, unsigned int);
                itoa_unsigned(val, buffer, 10);
                kprint(buffer);
                break;
            }
            case 'x':
            case 'X': {
                unsigned int val = va_arg(args, unsigned int);
                kprint("0x");
                itoa_unsigned(val, buffer, 16);
                kprint(buffer);
                break;
            }
            case 'p': {
                void* ptr = va_arg(args, void*);
                kprint("0x");
                itoa_unsigned((uint64_t)(uintptr_t)ptr, buffer, 16);
                kprint(buffer);
                break;
            }
            case 's': {
                char* str = va_arg(args, char*);
                kprint(str ? str : "(null)");
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                char tmp[2] = {c, '\0'};
                kprint(tmp);
                break;
            }
            case '%':
                kprint("%");
                break;
            case '\0':
                goto end;
            default: {
                kprint("%");
                char tmp[2] = {*format, '\0'};
                kprint(tmp);
                break;
            }
        }
        format++;
    }
end:
    va_end(args);
}

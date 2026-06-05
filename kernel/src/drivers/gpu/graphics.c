#include "graphics.h"
#include "gpu_fb.h"
#include "virtio_gpu.h"
#include <data/fonts/font8x8/font8x8_basic.h>
#include <drivers/pci/pci.h>
#include <limine.h>

extern volatile struct limine_framebuffer_request framebuffer_request;
extern char font8x8_control[32][8];
extern char font8x8_ext_latin[96][8];

static gpu_driver_t* active_gpu = NULL;
static int graphics_initialized = 0;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    gpu_driver_t* driver;
    int (*probe_init)(pci_device_t* dev);
} gpu_pci_entry_t;

static int probe_virtio_gpu(pci_device_t* dev) {
    if (virtio_gpu_init(dev) == 0) {
        active_gpu = &virtio_gpu_driver;
        return 0;
    }
    return -1;
}

static gpu_pci_entry_t gpu_pci_table[] = {
    { 0x1AF4, 0x1010, &virtio_gpu_driver, probe_virtio_gpu },
    { 0x1AF4, 0x1050, &virtio_gpu_driver, probe_virtio_gpu },
};

static int gpu_probe_pci(void) {
    pci_device_t pci_dev;
    for (size_t i = 0; i < sizeof(gpu_pci_table) / sizeof(gpu_pci_table[0]); i++) {
        if (pci_find_device(gpu_pci_table[i].vendor_id, gpu_pci_table[i].device_id, &pci_dev)) {
            if (gpu_pci_table[i].probe_init && gpu_pci_table[i].probe_init(&pci_dev) == 0) {
                return 0;
            }
        }
    }
    return -1;
}

static const uint8_t* glyph_for_codepoint(uint32_t cp) {
    if (cp < 0x20u)
        return (const uint8_t*)font8x8_control[cp];
    if (cp < 0x80u)
        return (const uint8_t*)font8x8_basic[cp];
    if (cp >= 0xA0u && cp <= 0xFFu)
        return (const uint8_t*)font8x8_ext_latin[cp - 0xA0u];
    return (const uint8_t*)font8x8_basic[(int)'?'];
}

void gpu_draw_scaled_char(int x, int y, char c, uint32_t color, int scale) {
    const uint8_t* glyph = glyph_for_codepoint((uint8_t)c);
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                gpu_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void gpu_draw_scaled_text(int x, int y, const char* str, uint32_t color, int scale) {
    if (!str || scale <= 0) return;
    int cursor_x = x;
    while (*str) {
        if (*str == '\n') {
            cursor_x = x;
            y += 8 * scale;
            str++;
            continue;
        }
        uint32_t cp = 0;
        unsigned char c0 = (unsigned char)str[0];
        if (c0 < 0x80u) {
            cp = c0;
            str += 1;
        } else if ((c0 & 0xE0u) == 0xC0u && (str[1] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(str[1] & 0x3Fu);
            str += 2;
        } else if ((c0 & 0xF0u) == 0xE0u &&
                   (str[1] & 0xC0u) == 0x80u &&
                   (str[2] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x0Fu) << 12) |
                 ((uint32_t)(str[1] & 0x3Fu) << 6) |
                 (uint32_t)(str[2] & 0x3Fu);
            str += 3;
        } else {
            cp = (uint32_t)'?';
            str += 1;
        }
        gpu_draw_scaled_char(cursor_x, y, (char)cp, color, scale);
        cursor_x += 8 * scale;
    }
}

int graphics_init(void) {
    if (graphics_initialized) return 0;

    if (gpu_probe_pci() == 0) {
        graphics_initialized = 1;
        return 0;
    }

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        return -1;
    }

    volatile struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
    if (gpu_fb_init(fb) != 0) {
        return -1;
    }

    active_gpu = &gpu_fb_driver;
    graphics_initialized = 1;

    return 0;
}

gpu_driver_t* graphics_get_driver(void) {
    return active_gpu;
}

bool graphics_has_acceleration(void) {
    return active_gpu ? active_gpu->is_accelerated : false;
}

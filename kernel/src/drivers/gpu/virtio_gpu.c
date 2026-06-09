#include "virtio_gpu.h"
#include "graphics.h"
#include <arch/x86_64/platform.h>
#include <lib/string.h>
#include <data/fonts/font8x8/font8x8_basic.h>

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_GPU_F_VIRGL      0
#define VIRTIO_GPU_F_EDID       1
#define VIRTIO_GPU_F_VIRGL_BIT  (1u << 28)

#define VIRTIO_GPU_CMD_CTX_CREATE       0x0201
#define VIRTIO_GPU_CMD_CTX_DESTROY      0x0202
#define VIRTIO_GPU_CMD_CTX_ATTACH_RES   0x0203
#define VIRTIO_GPU_CMD_CTX_DETACH_RES   0x0204
#define VIRTIO_GPU_CMD_SUBMIT_3D        0x0205
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D 0x0208
#define VIRTIO_GPU_CMD_RESOURCE_UNREF   0x0102

#define VIRTIO_GPU_RESOURCE_TYPE_PLANAR 0
#define VIRTIO_GPU_RESOURCE_TYPE_Y_UV_420 1
#define VIRTIO_GPU_RESOURCE_TYPE_Y_U_V_420 2
#define VIRTIO_GPU_RESOURCE_TYPE_Y_U_V_422 3
#define VIRTIO_GPU_RESOURCE_TYPE_Y_U_V_444 4

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO      0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D    0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT           0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH        0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D   0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VIRTIO_GPU_RESP_OK_NODATA            0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO      0x1101

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM     1

#define MAX_SCANOUTS 1
#define VIRTIO_GPU_MAX_W 1920
#define VIRTIO_GPU_MAX_H 1200
#define VIRTIO_FB_SIZE (VIRTIO_GPU_MAX_W * VIRTIO_GPU_MAX_H * 4)

/* ---- Modern VirtIO PCI constants ---- */
#define VIRTIO_PCI_CAP_ID          0x09
#define VIRTIO_PCI_CAP_COMMON_CFG  1
#define VIRTIO_PCI_CAP_NOTIFY_CFG  2
#define VIRTIO_PCI_CAP_ISR_CFG     3
#define VIRTIO_PCI_CAP_DEVICE_CFG  4

/* Common config register offsets (VirtIO 1.2 spec §4.1.4.3) */
#define VIRTIO_COMMON_DF_SEL       0x00
#define VIRTIO_COMMON_DF           0x04
#define VIRTIO_COMMON_DRF_SEL      0x08
#define VIRTIO_COMMON_DRF          0x0C
#define VIRTIO_COMMON_NUM_Q        0x12
#define VIRTIO_COMMON_STATUS       0x14
#define VIRTIO_COMMON_Q_SEL        0x16
#define VIRTIO_COMMON_Q_SIZE       0x18
#define VIRTIO_COMMON_Q_ENABLE     0x1C
#define VIRTIO_COMMON_Q_NOTIFY_OFF 0x1E
#define VIRTIO_COMMON_Q_DESC_LO    0x20
#define VIRTIO_COMMON_Q_DESC_HI    0x24
#define VIRTIO_COMMON_Q_DRIVER_LO  0x28
#define VIRTIO_COMMON_Q_DRIVER_HI  0x2C
#define VIRTIO_COMMON_Q_DEVICE_LO  0x30
#define VIRTIO_COMMON_Q_DEVICE_HI  0x34

/* Device status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE  1
#define VIRTIO_STATUS_DRIVER       2
#define VIRTIO_STATUS_DRIVER_OK    4
#define VIRTIO_STATUS_FEATURES_OK  8
#define VIRTIO_STATUS_FAILED       128

struct vq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
} __attribute__((packed));

struct vq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vq_used {
    uint16_t flags;
    uint16_t idx;
    struct vq_used_elem ring[256];
} __attribute__((packed));

struct virtqueue {
    struct vq_desc* desc;
    struct vq_avail* avail;
    struct vq_used* used;
    uint16_t q_size;
    uint16_t last_used_idx;
    uint16_t last_avail_idx;
    uint16_t notify_off;
};

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t num_ext;
};

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one {
        uint32_t r;
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
        uint32_t enabled;
        uint32_t flags;
    } modes[16];
};

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};

struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
};

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t r;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
};

struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t r;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t resource_id;
    uint32_t padding;
};

struct virtio_gpu_ctx_create {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t nlen;
    uint32_t padding;
    char name[64];
};

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

/* ---- Modern transport state ---- */
static volatile void* bar_virt[6];
static uint8_t common_bar;
static uint32_t common_offset;
static uint8_t notify_bar;
static uint32_t notify_offset;
static uint32_t notify_off_multiplier;
static uint8_t isr_bar;
static uint32_t isr_offset;

/* Page table pages for mapping 64-bit MMIO BAR into HHDM */
static uint64_t bar_pdpt[512] __attribute__((aligned(4096)));
static uint64_t bar_pd[512] __attribute__((aligned(4096)));
static uint64_t bar_pt[512] __attribute__((aligned(4096)));

extern void kprint(const char* text);
extern void kprint_hex64(uint64_t num);

#define MMIO_PTE_FLAGS (3 | 0x08 | 0x10) /* P | RW | PCD | PWT = UC- */

static void hhdm_map_range(uint64_t phys_base, uint64_t size) {
    uint64_t hhdm = hhdm_offset_get();
    uint64_t virt = hhdm + phys_base;

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t* pml4 = (uint64_t*)(hhdm + (cr3 & ~0xFFFULL));

    unsigned pml4_idx = (virt >> 39) & 0x1FF;

    if (!(pml4[pml4_idx] & 1)) {
        uint64_t pdpt_phys_paddr = v2p((uint64_t)(uintptr_t)bar_pdpt);
        pml4[pml4_idx] = pdpt_phys_paddr | 3;
    }

    uint64_t* pdpt = (uint64_t*)(hhdm + (pml4[pml4_idx] & ~0xFFFULL));

    unsigned pdpt_idx = (virt >> 30) & 0x1FF;
    if (!(pdpt[pdpt_idx] & 1)) {
        uint64_t pd_phys_paddr = v2p((uint64_t)(uintptr_t)bar_pd);
        pdpt[pdpt_idx] = pd_phys_paddr | 3;
    }

    uint64_t* pd = (uint64_t*)(hhdm + (pdpt[pdpt_idx] & ~0xFFFULL));

    unsigned pd_idx = (virt >> 21) & 0x1FF;
    if (!(pd[pd_idx] & 1)) {
        uint64_t pt_phys_paddr = v2p((uint64_t)(uintptr_t)bar_pt);
        pd[pd_idx] = pt_phys_paddr | 3;
    }

    uint64_t* pt = (uint64_t*)(hhdm + (pd[pd_idx] & ~0xFFFULL));

    unsigned pt_start = (virt >> 12) & 0x1FF;
    unsigned total_pages = (size + 0xFFF) >> 12;
    unsigned pt_end = pt_start + total_pages;
    if (pt_end > 512) pt_end = 512;

    for (unsigned i = pt_start; i < pt_end && i < 512; i++) {
        uint64_t page_phys = phys_base + ((uint64_t)(i - pt_start) << 12);
        pt[i] = page_phys | MMIO_PTE_FLAGS;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

static int gpu_initialized = 0;
static int gpu_resource_id = 1;
static uint32_t gpu_width = 0;
static uint32_t gpu_height = 0;
static int display_enabled = 0;
static int gpu_virgl_enabled = 0;
static uint32_t gpu_ctx_id = 1;

static uint8_t virtio_fb[VIRTIO_FB_SIZE] __attribute__((aligned(4096)));

static struct virtqueue ctrl_vq;
static uint8_t ctrl_ring_mem[16384] __attribute__((aligned(4096)));
static uint8_t cmd_buffer[4096] __attribute__((aligned(16)));
static uint8_t resp_buffer[4096] __attribute__((aligned(16)));

extern char font8x8_control[32][8];
extern char font8x8_ext_latin[96][8];

/* ---- MMIO helpers ---- */
static inline volatile void* virtio_addr(uint8_t bar, uint32_t offset) {
    return (volatile void*)((uintptr_t)bar_virt[bar] + offset);
}

static inline void mmio_write8(uint8_t bar, uint32_t offset, uint8_t val) {
    *(volatile uint8_t*)virtio_addr(bar, offset) = val;
}
static inline uint8_t mmio_read8(uint8_t bar, uint32_t offset) {
    return *(volatile uint8_t*)virtio_addr(bar, offset);
}
static inline void mmio_write16(uint8_t bar, uint32_t offset, uint16_t val) {
    *(volatile uint16_t*)virtio_addr(bar, offset) = val;
}
static inline uint16_t mmio_read16(uint8_t bar, uint32_t offset) {
    return *(volatile uint16_t*)virtio_addr(bar, offset);
}
static inline void mmio_write32(uint8_t bar, uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)virtio_addr(bar, offset) = val;
}
static inline uint32_t mmio_read32(uint8_t bar, uint32_t offset) {
    return *(volatile uint32_t*)virtio_addr(bar, offset);
}

static void virtqueue_init(struct virtqueue* vq, uint8_t* mem, uint16_t qsize) {
    vq->q_size = qsize;
    vq->last_used_idx = 0;
    vq->last_avail_idx = 0;
    vq->notify_off = 0;
    vq->desc = (struct vq_desc*)mem;
    vq->avail = (struct vq_avail*)(mem + qsize * sizeof(struct vq_desc));
    uintptr_t avail_end = (uintptr_t)vq->avail + sizeof(struct vq_avail) + sizeof(uint16_t);
    uintptr_t used_start = (avail_end + 4095) & ~4095;
    vq->used = (struct vq_used*)used_start;
    memset(mem, 0, 16384);
}

static int virtio_gpu_send_command(const void* cmd, uint32_t cmd_len, void* resp, uint32_t resp_len) {
    volatile struct vq_used* used = ctrl_vq.used;
    uint32_t timeout = 10000000;
    while ((uint16_t)(ctrl_vq.last_avail_idx - used->idx) >= (ctrl_vq.q_size - 2)) {
        if (--timeout == 0) return -1;
        __asm__ __volatile__("pause");
    }

    uint16_t head = ctrl_vq.last_avail_idx % ctrl_vq.q_size;
    uint16_t next = (head + 1) % ctrl_vq.q_size;

    memcpy(cmd_buffer, cmd, cmd_len);
    ctrl_vq.desc[head].addr = v2p((uint64_t)(uintptr_t)cmd_buffer);
    ctrl_vq.desc[head].len = cmd_len;
    ctrl_vq.desc[head].flags = VRING_DESC_F_NEXT;
    ctrl_vq.desc[head].next = next;

    memset(resp_buffer, 0, resp_len);
    ctrl_vq.desc[next].addr = v2p((uint64_t)(uintptr_t)resp_buffer);
    ctrl_vq.desc[next].len = resp_len;
    ctrl_vq.desc[next].flags = VRING_DESC_F_WRITE;

    ctrl_vq.avail->ring[ctrl_vq.avail->idx % ctrl_vq.q_size] = head;
    __asm__ __volatile__("mfence");
    ctrl_vq.avail->idx++;
    __asm__ __volatile__("mfence");
    ctrl_vq.last_avail_idx++;

    {
        uint32_t na = notify_offset + ctrl_vq.notify_off * notify_off_multiplier;
        mmio_write16(notify_bar, na, 0);
    }

    timeout = 100000000;
    while (ctrl_vq.last_used_idx == used->idx) {
        if (--timeout == 0) return -1;
        __asm__ __volatile__("pause");
    }

    ctrl_vq.last_used_idx = used->idx;
    memcpy(resp, resp_buffer, resp_len);
    return 0;
}

static int virtio_get_display_info(void) {
    struct virtio_gpu_ctrl_hdr cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    struct virtio_gpu_resp_display_info resp;
    memset(&resp, 0, sizeof(resp));

    if (virtio_gpu_send_command(&cmd, sizeof(cmd), &resp, sizeof(resp)) != 0)
        return -1;

    if (resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
        return -1;

    for (int i = 0; i < 16; i++) {
        if (resp.modes[i].enabled) {
            gpu_width = resp.modes[i].width;
            gpu_height = resp.modes[i].height;
            if (gpu_width > VIRTIO_GPU_MAX_W) gpu_width = VIRTIO_GPU_MAX_W;
            if (gpu_height > VIRTIO_GPU_MAX_H) gpu_height = VIRTIO_GPU_MAX_H;
            display_enabled = 1;
            return 0;
        }
    }

    gpu_width = 1024;
    gpu_height = 768;
    display_enabled = 1;
    return 0;
}

static int virtio_attach_backing(uint32_t rid, uint64_t addr, uint64_t size) {
    struct {
        struct virtio_gpu_ctrl_hdr hdr;
        uint32_t resource_id;
        uint32_t nr_entries;
        struct virtio_gpu_mem_entry entry;
    } cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd.resource_id = rid;
    cmd.nr_entries = 1;
    cmd.entry.addr = addr;
    cmd.entry.length = size;

    struct virtio_gpu_ctrl_hdr resp;
    memset(&resp, 0, sizeof(resp));

    int sc_ret = virtio_gpu_send_command(&cmd, sizeof(cmd), &resp, sizeof(resp));
    kprint("[GPU-DBG] send_cmd_ret=");
    kprint_hex64(sc_ret);
    kprint(" resp.type=0x");
    kprint_hex64(resp.type);
    kprint("\n");
    if (sc_ret != 0) return -1;

    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int virtio_ctx_create(uint32_t ctx_id) {
    struct virtio_gpu_ctx_create cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_CTX_CREATE;
    cmd.hdr.ctx_id = ctx_id;
    cmd.nlen = 0;
    cmd.padding = 0;

    struct virtio_gpu_ctrl_hdr resp;
    memset(&resp, 0, sizeof(resp));

    if (virtio_gpu_send_command(&cmd, sizeof(cmd), &resp, sizeof(resp)) != 0)
        return -1;

    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int virtio_create_resource(uint32_t rid, uint32_t w, uint32_t h) {
    struct virtio_gpu_resource_create_2d cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd.resource_id = rid;
    cmd.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    cmd.width = w;
    cmd.height = h;

    struct virtio_gpu_ctrl_hdr resp;
    memset(&resp, 0, sizeof(resp));

    if (virtio_gpu_send_command(&cmd, sizeof(cmd), &resp, sizeof(resp)) != 0)
        return -1;

    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int virtio_set_scanout(uint32_t rid, uint32_t w, uint32_t h) {
    struct virtio_gpu_set_scanout cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd.scanout_id = 0;
    cmd.resource_id = rid;
    cmd.r.width = w;
    cmd.r.height = h;

    struct virtio_gpu_ctrl_hdr resp;
    memset(&resp, 0, sizeof(resp));

    if (virtio_gpu_send_command(&cmd, sizeof(cmd), &resp, sizeof(resp)) != 0)
        return -1;

    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

void virtio_gpu_flush(int x, int y, int w, int h) {
    if (!gpu_initialized || !display_enabled) return;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint32_t)(x + w) > gpu_width) w = (int)gpu_width - x;
    if ((uint32_t)(y + h) > gpu_height) h = (int)gpu_height - y;
    if (w <= 0 || h <= 0) return;

    uint64_t offset = (uint64_t)(y * (int)gpu_width + x) * 4;

    struct virtio_gpu_transfer_to_host_2d cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd.resource_id = gpu_resource_id;
    cmd.x = (uint32_t)x;
    cmd.y = (uint32_t)y;
    cmd.width = (uint32_t)w;
    cmd.height = (uint32_t)h;
    cmd.offset = offset;

    struct virtio_gpu_ctrl_hdr resp;
    memset(&resp, 0, sizeof(resp));

    virtio_gpu_send_command(&cmd, sizeof(cmd), &resp, sizeof(resp));

    struct virtio_gpu_resource_flush fcmd;
    memset(&fcmd, 0, sizeof(fcmd));
    fcmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    fcmd.resource_id = gpu_resource_id;
    fcmd.x = (uint32_t)x;
    fcmd.y = (uint32_t)y;
    fcmd.width = (uint32_t)w;
    fcmd.height = (uint32_t)h;

    memset(&resp, 0, sizeof(resp));
    virtio_gpu_send_command(&fcmd, sizeof(fcmd), &resp, sizeof(resp));
}

static void vgpu_put_pixel(int x, int y, uint32_t color) {
    if (!gpu_initialized || !display_enabled) return;
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= gpu_width || (uint32_t)y >= gpu_height) return;
    uint32_t* pixel = (uint32_t*)&virtio_fb[(uint64_t)y * gpu_width * 4 + (uint64_t)x * 4];
    *pixel = color;
}

static void vgpu_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            vgpu_put_pixel(x + xx, y + yy, color);
}

static void vgpu_clear_screen(uint32_t color) {
    for (uint64_t y = 0; y < gpu_height; y++)
        for (uint64_t x = 0; x < gpu_width; x++)
            vgpu_put_pixel((int)x, (int)y, color);
}

static const uint8_t* vgpu_glyph_for_codepoint(uint32_t cp);

static void vgpu_draw_char(int x, int y, char c, uint32_t color) {
    const uint8_t* glyph = vgpu_glyph_for_codepoint((uint8_t)c);
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col))
                vgpu_put_pixel(x + col, y + row, color);
        }
    }
}

static void vgpu_draw_text(int x, int y, const char* str, uint32_t color) {
    int cx = x;
    while (*str) {
        if (*str == '\n') { cx = x; y += 8; str++; continue; }
        uint32_t cp = 0;
        unsigned char c0 = (unsigned char)str[0];
        if (c0 < 0x80u) { cp = c0; str += 1; }
        else if ((c0 & 0xE0u) == 0xC0u && (str[1] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(str[1] & 0x3Fu); str += 2;
        } else if ((c0 & 0xF0u) == 0xE0u && (str[1] & 0xC0u) == 0x80u && (str[2] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x0Fu) << 12) | ((uint32_t)(str[1] & 0x3Fu) << 6) | (uint32_t)(str[2] & 0x3Fu);
            str += 3;
        } else { cp = (uint32_t)'?'; str += 1; }
        const uint8_t* glyph = vgpu_glyph_for_codepoint(cp);
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++)
                if (bits & (1 << col)) vgpu_put_pixel(cx + col, y + row, color);
        }
        cx += 8;
    }
}

static void vgpu_scroll(int x, int y, int w, int h, int lines) {
    (void)w; (void)h;
    if (!gpu_initialized || lines <= 0) return;
    int row_px = lines * 8;
    for (int yy = y; yy < (int)gpu_height - row_px; yy++)
        for (int xx = x; xx < (int)gpu_width; xx++) {
            uint32_t* dst = (uint32_t*)&virtio_fb[(uint64_t)yy * gpu_width * 4 + (uint64_t)xx * 4];
            uint32_t* src = (uint32_t*)&virtio_fb[(uint64_t)(yy + row_px) * gpu_width * 4 + (uint64_t)xx * 4];
            *dst = *src;
        }
    for (int yy = (int)gpu_height - row_px; yy < (int)gpu_height; yy++)
        for (int xx = x; xx < (int)gpu_width; xx++)
            vgpu_put_pixel(xx, yy, COLOR_BLACK);
}

static void vgpu_blit(const void* src, int src_w, int src_h, int dst_x, int dst_y) {
    uint64_t* data = (uint64_t*)src;
    for (int y = 0; y < src_h; y++)
        for (int x = 0; x < src_w; x++)
            vgpu_put_pixel(dst_x + x, dst_y + y, (uint32_t)(data[y * src_w + x] & 0xFFFFFFFF));
}

static void vgpu_flush(int x, int y, int w, int h) {
    virtio_gpu_flush(x, y, w, h);
}

static void* vgpu_get_address(void) {
    return gpu_initialized ? (void*)virtio_fb : NULL;
}

static uint32_t vgpu_get_width(void) { return gpu_width; }
static uint32_t vgpu_get_height(void) { return gpu_height; }
static uint32_t vgpu_get_bpp(void) { return 32; }
static uint32_t vgpu_get_pitch(void) { return gpu_width * 4; }

static void vgpu_deinit(void) {
    gpu_initialized = 0;
    display_enabled = 0;
}

static const uint8_t* vgpu_glyph_for_codepoint(uint32_t cp) {
    if (cp < 0x20u) return (const uint8_t*)font8x8_control[cp];
    if (cp < 0x80u) return (const uint8_t*)font8x8_basic[cp];
    if (cp >= 0xA0u && cp <= 0xFFu) return (const uint8_t*)font8x8_ext_latin[cp - 0xA0u];
    return (const uint8_t*)font8x8_basic[(int)'?'];
}

static int vgpu_init(void* context) {
    pci_device_t* dev = (pci_device_t*)context;
    if (!dev) { kprint("[GPU-DBG] vgpu_init: null dev\n"); return -1; }

    kprint("[GPU-DBG] Dumping all BARs:\n");
    for (int i = 0; i < 6; i++) {
        uint32_t val = pci_read_config(dev->bus, dev->device, dev->function, 0x10 + i * 4);
        kprint("[GPU-DBG]  BAR");
        kprint_hex64(i);
        kprint("=0x");
        kprint_hex64(val);
        kprint("\n");
    }

    for (int i = 0; i < 6; i++) {
        bar_virt[i] = NULL;
    }

    kprint("[GPU-DBG] BAR mapping: hhdm_offset=");
    kprint_hex64(hhdm_offset_get());
    kprint("\n");

    for (int i = 0; i < 6; i++) {
        uint32_t val = pci_read_config(dev->bus, dev->device, dev->function, 0x10 + i * 4);
        uint64_t paddr;
        if (val & 1) continue;
        uint8_t type = (val >> 1) & 3;
        if (type == 0) {
            paddr = val & ~0xFULL;
            if (paddr)
                bar_virt[i] = (volatile void*)(hhdm_offset_get() + paddr);
        } else if (type == 2 && i < 5) {
            uint32_t hi = pci_read_config(dev->bus, dev->device, dev->function, 0x10 + (i + 1) * 4);
            paddr = ((uint64_t)hi << 32) | (val & ~0xFULL);
            if (paddr) {
                bar_virt[i] = (volatile void*)(hhdm_offset_get() + paddr);
                bar_virt[i + 1] = NULL;
            }
            i++;
        }
    }

    kprint("[GPU-DBG] After BAR mapping\n");

    uint32_t command = pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, command | (1 << 1) | (1 << 2));
    kprint("[GPU-DBG] PCI command set\n");

    common_bar = 0xFF; notify_bar = 0xFF; isr_bar = 0xFF;
    common_offset = 0; notify_offset = 0; isr_offset = 0;
    notify_off_multiplier = 0;

    kprint("[GPU-DBG] Calling pci_find_capability...\n");
    uint8_t cap_ptr = pci_find_capability(dev->bus, dev->device, dev->function, VIRTIO_PCI_CAP_ID);
    kprint("[GPU-DBG] pci_find_capability returned: ");
    kprint_hex64(cap_ptr);
    kprint("\n");
    while (cap_ptr) {
        kprint("[GPU-DBG] cap_ptr=0x");
        kprint_hex64(cap_ptr);
        kprint(" id=");
        uint8_t id = pci_read_field(dev->bus, dev->device, dev->function, cap_ptr, 1);
        kprint_hex64(id);
        kprint("\n");
        if (id != VIRTIO_PCI_CAP_ID) break;

        uint8_t  cfg_type = pci_read_field(dev->bus, dev->device, dev->function, cap_ptr + 3, 1);
        uint8_t  bar      = pci_read_field(dev->bus, dev->device, dev->function, cap_ptr + 4, 1);
        kprint("[GPU-DBG]  cfg_type=");
        kprint_hex64(cfg_type);
        kprint(" bar=");
        kprint_hex64(bar);
        uint32_t off      = pci_read_field(dev->bus, dev->device, dev->function, cap_ptr + 8, 4);
        uint32_t len      = pci_read_field(dev->bus, dev->device, dev->function, cap_ptr + 12, 4);
        kprint(" off=0x");
        kprint_hex64(off);
        kprint("\n");

        switch (cfg_type) {
        case VIRTIO_PCI_CAP_COMMON_CFG:
            common_bar    = bar;
            common_offset = off;
            break;
        case VIRTIO_PCI_CAP_NOTIFY_CFG:
            notify_bar    = bar;
            notify_offset = off;
            notify_off_multiplier = pci_read_field(dev->bus, dev->device, dev->function, cap_ptr + 16, 4);
            break;
        case VIRTIO_PCI_CAP_ISR_CFG:
            isr_bar    = bar;
            isr_offset = off;
            break;
        case 4:
            break;
        }

        cap_ptr = pci_read_field(dev->bus, dev->device, dev->function, cap_ptr + 1, 1);
        cap_ptr &= 0xFC;
    }

    kprint("[GPU-DBG] After cap loop. common_bar=");
    kprint_hex64(common_bar);
    kprint(" notify_bar=");
    kprint_hex64(notify_bar);
    kprint(" isr_bar=");
    kprint_hex64(isr_bar);
    kprint("\n[GPU-DBG] bar_virt[4]=");
    kprint_hex64((uint64_t)(uintptr_t)bar_virt[4]);
    kprint("\n[GPU-DBG] About to check cap validity...\n");

    if (common_bar == 0xFF || notify_bar == 0xFF || isr_bar == 0xFF) {
        kprint("[GPU-DBG] Missing virtio PCI capabilities\n");
        return -1;
    }

    if (!bar_virt[common_bar] || !bar_virt[notify_bar]) {
        kprint("[GPU-DBG] Required BAR not mapped\n");
        return -1;
    }

    for (int i = 0; i < 6; i++) {
        if (!bar_virt[i]) continue;
        uint64_t va = (uint64_t)(uintptr_t)bar_virt[i];
        uint64_t pa = va - hhdm_offset_get();
        uint64_t mapped_pa = v2p(va);
        if (mapped_pa != pa) {
            kprint("[GPU-DBG] Mapping BAR");
            kprint_hex64(i);
            kprint(" phys=0x");
            kprint_hex64(pa);
            kprint("\n");
            hhdm_map_range(pa, 0x100000);
        }
    }

    kprint("[GPU-DBG] First MMIO access...\n");
    mmio_write8(common_bar, common_offset + VIRTIO_COMMON_STATUS, 0);
    kprint("[GPU-DBG] ...device reset done\n");

    mmio_write8(common_bar, common_offset + VIRTIO_COMMON_STATUS,
                VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    mmio_write32(common_bar, common_offset + VIRTIO_COMMON_DF_SEL, 0);
    uint32_t features = mmio_read32(common_bar, common_offset + VIRTIO_COMMON_DF);

    kprint("[GPU-DBG] features=0x");
    kprint_hex64(features);
    kprint("\n");

    uint32_t drf = 0;
    if (features & VIRTIO_GPU_F_VIRGL_BIT) {
        kprint("[GPU-DBG] VIRGL supported but skipped (no Mesa userspace yet)\n");
    }
    mmio_write32(common_bar, common_offset + VIRTIO_COMMON_DRF_SEL, 0);
    mmio_write32(common_bar, common_offset + VIRTIO_COMMON_DRF, drf);

    mmio_write8(common_bar, common_offset + VIRTIO_COMMON_STATUS,
                VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    uint8_t status = mmio_read8(common_bar, common_offset + VIRTIO_COMMON_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        kprint("[GPU-DBG] Feature negotiation failed\n");
        mmio_write8(common_bar, common_offset + VIRTIO_COMMON_STATUS,
                    VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_FAILED);
        return -1;
    }

    gpu_virgl_enabled = (drf & VIRTIO_GPU_F_VIRGL_BIT) ? 1 : 0;
    kprint("[GPU-DBG] VIRGL enabled: ");
    kprint_hex64(gpu_virgl_enabled);
    kprint("\n");

    mmio_write16(common_bar, common_offset + VIRTIO_COMMON_Q_SEL, 0);
    uint16_t qsize = mmio_read16(common_bar, common_offset + VIRTIO_COMMON_Q_SIZE);
    uint16_t notify_off = mmio_read16(common_bar, common_offset + VIRTIO_COMMON_Q_NOTIFY_OFF);

    kprint("[GPU-DBG] qsize=0x");
    kprint_hex64(qsize);
    kprint(" notify_off=0x");
    kprint_hex64(notify_off);
    kprint("\n");

    if (qsize == 0) return -1;
    if (qsize > 256) qsize = 256;

    virtqueue_init(&ctrl_vq, ctrl_ring_mem, qsize);
    ctrl_vq.notify_off = notify_off;

    uint64_t desc_paddr   = v2p((uint64_t)(uintptr_t)ctrl_vq.desc);
    uint64_t avail_paddr  = v2p((uint64_t)(uintptr_t)ctrl_vq.avail);
    uint64_t used_paddr   = v2p((uint64_t)(uintptr_t)ctrl_vq.used);

    kprint("[GPU-DBG] desc_paddr=0x");
    kprint_hex64(desc_paddr);
    kprint("\n");

    mmio_write32(common_bar, common_offset + VIRTIO_COMMON_Q_DESC_LO,  (uint32_t)(desc_paddr));
    mmio_write32(common_bar, common_offset + VIRTIO_COMMON_Q_DESC_HI,  (uint32_t)(desc_paddr >> 32));
    mmio_write32(common_bar, common_offset + VIRTIO_COMMON_Q_DRIVER_LO,(uint32_t)(avail_paddr));
    mmio_write32(common_bar, common_offset + VIRTIO_COMMON_Q_DRIVER_HI,(uint32_t)(avail_paddr >> 32));
    mmio_write32(common_bar, common_offset + VIRTIO_COMMON_Q_DEVICE_LO,(uint32_t)(used_paddr));
    mmio_write32(common_bar, common_offset + VIRTIO_COMMON_Q_DEVICE_HI,(uint32_t)(used_paddr >> 32));

    mmio_write16(common_bar, common_offset + VIRTIO_COMMON_Q_ENABLE, 1);

    mmio_write8(common_bar, common_offset + VIRTIO_COMMON_STATUS,
                VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    kprint("[GPU-DBG] Getting display info...\n");
    if (virtio_get_display_info() != 0) {
        kprint("[GPU-DBG] GET_DISPLAY_INFO failed\n");
        return -1;
    }
    kprint("[GPU-DBG] display: ");
    kprint_hex64(gpu_width);
    kprint("x");
    kprint_hex64(gpu_height);
    kprint("\n");

    if (virtio_create_resource(gpu_resource_id, gpu_width, gpu_height) != 0) {
        kprint("[GPU-DBG] CREATE_RESOURCE failed\n");
        return -1;
    }

    uint64_t fb_paddr = v2p((uint64_t)(uintptr_t)virtio_fb);
    uint64_t fb_size = (uint64_t)gpu_width * gpu_height * 4;
    kprint("[GPU-DBG] fb_paddr=0x");
    kprint_hex64(fb_paddr);
    kprint(" size=0x");
    kprint_hex64(fb_size);
    kprint("\n");
    int ab_ret = virtio_attach_backing(gpu_resource_id, fb_paddr, fb_size);
    kprint("[GPU-DBG] attach_backing ret=");
    kprint_hex64(ab_ret);
    kprint("\n");
    if (ab_ret != 0) {
        kprint("[GPU-DBG] ATTACH_BACKING failed\n");
        return -1;
    }

    if (virtio_set_scanout(gpu_resource_id, gpu_width, gpu_height) != 0) {
        kprint("[GPU-DBG] SET_SCANOUT failed\n");
        return -1;
    }

    memset(virtio_fb, 0, VIRTIO_FB_SIZE);
    virtio_gpu_flush(0, 0, (int)gpu_width, (int)gpu_height);

    gpu_initialized = 1;
    kprint("[GPU-DBG] VirtIO GPU init OK\n");
    return 0;
}

gpu_driver_t virtio_gpu_driver = {
    .name = "virtio-gpu",
    .is_accelerated = true,
    .init = vgpu_init,
    .deinit = vgpu_deinit,
    .put_pixel = vgpu_put_pixel,
    .fill_rect = vgpu_fill_rect,
    .clear_screen = vgpu_clear_screen,
    .draw_char = vgpu_draw_char,
    .draw_text = vgpu_draw_text,
    .scroll = vgpu_scroll,
    .blit = vgpu_blit,
    .flush = vgpu_flush,
    .get_address = vgpu_get_address,
    .get_width = vgpu_get_width,
    .get_height = vgpu_get_height,
    .get_bpp = vgpu_get_bpp,
    .get_pitch = vgpu_get_pitch,
};

int virtio_gpu_init(pci_device_t* dev) {
    if (vgpu_init(dev) != 0) return -1;
    return 0;
}

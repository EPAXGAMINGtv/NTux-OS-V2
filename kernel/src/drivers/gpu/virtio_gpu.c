#include <drivers/gpu/virtio_gpu.h>
#include <drivers/gpu/gpu.h>
#include <drivers/gpu/gpu_ioctl.h>

#include <drivers/pci/pci.h>
#include <drivers/framebuffer/kprint.h>
#include <interrupt/timer.h>
#include <arch/x86_64/io.h>
#include <mm/kmalloc.h>
#include <mm/hhdm.h>
#include <limine.h>
#include <lib/string.h>

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_GPU_DEV_ID_LEGACY 0x1050
#define VIRTIO_GPU_DEV_ID_MODERN 0x1040

#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_NUM 0x0C
#define VIRTIO_PCI_QUEUE_SEL 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_STATUS 0x12

#define VIRTIO_STATUS_ACK 0x01
#define VIRTIO_STATUS_DRIVER 0x02
#define VIRTIO_STATUS_DRIVER_OK 0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08
#define VIRTIO_STATUS_FAILED 0x80

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO     0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D   0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF       0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT          0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH       0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D  0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107

#define VIRTIO_GPU_RESP_OK_NODATA        0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO  0x1101

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1

#define PCI_STATUS 0x06
#define PCI_CAP_PTR 0x34
#define PCI_CAP_ID_VNDR 0x09

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG 3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

extern volatile struct limine_framebuffer_request framebuffer_request;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} virtq_used_t;

typedef struct {
    uint16_t qsize;
    uint16_t last_used_idx;
    uint8_t* mem_raw;
    uint8_t* mem;
    uint32_t mem_size;
    virtq_desc_t* desc;
    virtq_avail_t* avail;
    virtq_used_t* used;
} virtq_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} virtio_gpu_ctrl_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} virtio_gpu_rect_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t scanout_id;
    uint32_t resource_id;
} virtio_gpu_set_scanout_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} virtio_gpu_resource_create_2d_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} virtio_gpu_resource_attach_backing_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} virtio_gpu_mem_entry_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} virtio_gpu_transfer_to_host_2d_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t resource_id;
    uint32_t padding;
} virtio_gpu_resource_flush_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    struct {
        virtio_gpu_rect_t r;
        uint32_t enabled;
        uint32_t flags;
    } pmodes[16];
} virtio_gpu_resp_display_info_t;

typedef struct __attribute__((packed)) {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} virtio_pci_common_cfg_t;

typedef struct {
    bool present;
    bool ready;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar0;
    uint8_t irq_line;
    bool modern;
} virtio_gpu_pci_t;

typedef struct {
    virtio_pci_common_cfg_t* common;
    volatile uint8_t* notify_base;
    uint32_t notify_off_multiplier;
    volatile uint8_t* isr;
    volatile uint8_t* device;
} virtio_pci_caps_t;

static virtio_gpu_pci_t g_pci;
static virtio_pci_caps_t g_caps;
static uint16_t g_io_base;
static virtq_t g_ctrlq;
static gpu_device_t g_virtio_gpu;
static uint32_t g_resource_id = 1;
static uint8_t* g_backing = NULL;
static uint32_t g_backing_size = 0;
static uint32_t g_width = 0;
static uint32_t g_height = 0;

static uint32_t align_up_u32(uint32_t v, uint32_t align) {
    return (v + align - 1u) & ~(align - 1u);
}

static uint64_t pci_read_bar_base(uint32_t bus, uint32_t dev, uint32_t fn, uint8_t bar_index) {
    uint32_t off = 0x10u + (uint32_t)bar_index * 4u;
    uint32_t low = (uint32_t)pci_read_field(bus, dev, fn, off, 4);
    if (low & 0x1u) {
        return (uint64_t)(low & ~0x3u);
    }
    uint32_t type = low & 0x6u;
    if (type == 0x4u) {
        uint32_t high = (uint32_t)pci_read_field(bus, dev, fn, off + 4u, 4);
        return ((uint64_t)high << 32) | (uint64_t)(low & ~0xFu);
    }
    return (uint64_t)(low & ~0xFu);
}

static int virtio_pci_find_caps(uint32_t bus, uint32_t dev, uint32_t fn, virtio_pci_caps_t* out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    uint16_t status = (uint16_t)pci_read_field(bus, dev, fn, PCI_STATUS, 2);
    if ((status & 0x10u) == 0) return -1;
    uint8_t cap = (uint8_t)pci_read_field(bus, dev, fn, PCI_CAP_PTR, 1);
    int guard = 0;
    while (cap && guard++ < 64) {
        uint8_t cap_id = (uint8_t)pci_read_field(bus, dev, fn, cap + 0, 1);
        uint8_t cap_next = (uint8_t)pci_read_field(bus, dev, fn, cap + 1, 1);
        uint8_t cap_len = (uint8_t)pci_read_field(bus, dev, fn, cap + 2, 1);
        uint8_t cfg_type = (uint8_t)pci_read_field(bus, dev, fn, cap + 3, 1);
        if (cap_id == PCI_CAP_ID_VNDR && cap_len >= 16) {
            uint8_t bar = (uint8_t)pci_read_field(bus, dev, fn, cap + 4, 1);
            uint32_t offset = (uint32_t)pci_read_field(bus, dev, fn, cap + 8, 4);
            uint32_t length = (uint32_t)pci_read_field(bus, dev, fn, cap + 12, 4);
            uint64_t base = pci_read_bar_base(bus, dev, fn, bar);
            volatile uint8_t* mmio = (volatile uint8_t*)(uintptr_t)(base + offset + hhdm_offset_get());
            if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                out->common = (virtio_pci_common_cfg_t*)(uintptr_t)mmio;
            } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                out->notify_base = mmio;
                if (cap_len >= 20) {
                    out->notify_off_multiplier = (uint32_t)pci_read_field(bus, dev, fn, cap + 16, 4);
                } else {
                    out->notify_off_multiplier = 0;
                }
                (void)length;
            } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
                out->isr = mmio;
            } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                out->device = mmio;
            }
        }
        cap = cap_next;
    }
    if (!out->common || !out->notify_base) return -1;
    return 0;
}

static bool virtq_setup_legacy(uint16_t qindex, virtq_t* q) {
    memset(q, 0, sizeof(*q));

    outw(g_io_base + VIRTIO_PCI_QUEUE_SEL, qindex);
    uint16_t qsize = inw(g_io_base + VIRTIO_PCI_QUEUE_NUM);
    if (qsize == 0) return false;

    uint32_t sz_desc = (uint32_t)sizeof(virtq_desc_t) * qsize;
    uint32_t sz_avail = 6u + 2u * qsize;
    uint32_t used_off = align_up_u32(sz_desc + sz_avail, 4096u);
    uint32_t sz_used = 6u + 8u * qsize;
    uint32_t total = used_off + sz_used;
    uint32_t total_alloc = total + 4096u;

    uint8_t* raw = (uint8_t*)kmalloc(total_alloc);
    if (!raw) return false;
    memset(raw, 0, total_alloc);

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + 4095u) & ~(uintptr_t)4095u;
    uint8_t* mem = (uint8_t*)aligned;
    memset(mem, 0, total);

    q->qsize = qsize;
    q->mem_raw = raw;
    q->mem = mem;
    q->mem_size = total;
    q->desc = (virtq_desc_t*)mem;
    q->avail = (virtq_avail_t*)(mem + sz_desc);
    q->used = (virtq_used_t*)(mem + used_off);
    q->last_used_idx = 0;

    outl(g_io_base + VIRTIO_PCI_QUEUE_PFN, (uint32_t)(aligned >> 12));
    return true;
}

static bool virtq_setup_modern(uint16_t qindex, virtq_t* q) {
    if (!g_caps.common) return false;
    memset(q, 0, sizeof(*q));

    g_caps.common->queue_select = qindex;
    uint16_t qsize = g_caps.common->queue_size;
    if (qsize == 0) return false;

    uint32_t sz_desc = (uint32_t)sizeof(virtq_desc_t) * qsize;
    uint32_t sz_avail = 6u + 2u * qsize;
    uint32_t used_off = align_up_u32(sz_desc + sz_avail, 4096u);
    uint32_t sz_used = 6u + 8u * qsize;
    uint32_t total = used_off + sz_used;
    uint32_t total_alloc = total + 4096u;

    uint8_t* raw = (uint8_t*)kmalloc(total_alloc);
    if (!raw) return false;
    memset(raw, 0, total_alloc);

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + 4095u) & ~(uintptr_t)4095u;
    uint8_t* mem = (uint8_t*)aligned;
    memset(mem, 0, total);

    q->qsize = qsize;
    q->mem_raw = raw;
    q->mem = mem;
    q->mem_size = total;
    q->desc = (virtq_desc_t*)mem;
    q->avail = (virtq_avail_t*)(mem + sz_desc);
    q->used = (virtq_used_t*)(mem + used_off);
    q->last_used_idx = 0;

    g_caps.common->queue_desc = (uint64_t)(uintptr_t)aligned;
    g_caps.common->queue_avail = (uint64_t)(uintptr_t)(aligned + sz_desc);
    g_caps.common->queue_used = (uint64_t)(uintptr_t)(aligned + used_off);
    g_caps.common->queue_msix_vector = 0xFFFF;
    g_caps.common->queue_enable = 1;
    return true;
}

static void virtq_kick_legacy(uint16_t qindex) {
    outw(g_io_base + VIRTIO_PCI_QUEUE_NOTIFY, qindex);
}

static void virtq_kick_modern(uint16_t qindex) {
    if (!g_caps.common || !g_caps.notify_base) return;
    g_caps.common->queue_select = qindex;
    uint16_t notify_off = g_caps.common->queue_notify_off;
    uint32_t mult = g_caps.notify_off_multiplier ? g_caps.notify_off_multiplier : 1u;
    volatile uint32_t* notify = (volatile uint32_t*)(uintptr_t)(g_caps.notify_base + (uint64_t)notify_off * mult);
    *notify = qindex;
}

static int virtio_gpu_send(void* req, uint32_t req_len, void* resp, uint32_t resp_len) {
    if (!req || req_len == 0) return -1;
    uint16_t id0 = 0;
    uint16_t id1 = 1;

    g_ctrlq.desc[id0].addr = (uint64_t)(uintptr_t)req;
    g_ctrlq.desc[id0].len = req_len;
    g_ctrlq.desc[id0].flags = VIRTQ_DESC_F_NEXT;
    g_ctrlq.desc[id0].next = id1;

    g_ctrlq.desc[id1].addr = (uint64_t)(uintptr_t)resp;
    g_ctrlq.desc[id1].len = resp_len;
    g_ctrlq.desc[id1].flags = VIRTQ_DESC_F_WRITE;
    g_ctrlq.desc[id1].next = 0;

    uint16_t aidx = g_ctrlq.avail->idx;
    g_ctrlq.avail->ring[aidx % g_ctrlq.qsize] = id0;
    __sync_synchronize();
    g_ctrlq.avail->idx = (uint16_t)(aidx + 1);
    if (g_pci.modern) virtq_kick_modern(0); else virtq_kick_legacy(0);

    uint64_t start = get_tick_count();
    while ((uint16_t)(g_ctrlq.used->idx - g_ctrlq.last_used_idx) == 0) {
        if ((int64_t)(get_tick_count() - start) > (int64_t)(timer_get_hz() ? timer_get_hz() : 200u)) return -1;
    }
    g_ctrlq.last_used_idx++;
    return 0;
}

static int virtio_gpu_get_display_info(void) {
    virtio_gpu_ctrl_hdr_t req;
    virtio_gpu_resp_display_info_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    if (virtio_gpu_send(&req, sizeof(req), &resp, sizeof(resp)) != 0) return -1;
    if (resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) return -1;
    if (!resp.pmodes[0].enabled) return -1;

    g_width = resp.pmodes[0].r.width;
    g_height = resp.pmodes[0].r.height;
    return 0;
}

static int virtio_gpu_create_resource(void) {
    virtio_gpu_resource_create_2d_t req;
    virtio_gpu_ctrl_hdr_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    req.resource_id = g_resource_id;
    req.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    req.width = g_width;
    req.height = g_height;

    if (virtio_gpu_send(&req, sizeof(req), &resp, sizeof(resp)) != 0) return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int virtio_gpu_attach_backing(void) {
    struct {
        virtio_gpu_resource_attach_backing_t req;
        virtio_gpu_mem_entry_t entry;
    } __attribute__((packed)) msg;
    virtio_gpu_ctrl_hdr_t resp;

    memset(&msg, 0, sizeof(msg));
    memset(&resp, 0, sizeof(resp));
    msg.req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    msg.req.resource_id = g_resource_id;
    msg.req.nr_entries = 1;
    msg.entry.addr = (uint64_t)(uintptr_t)g_backing;
    msg.entry.length = g_backing_size;

    if (virtio_gpu_send(&msg, sizeof(msg), &resp, sizeof(resp)) != 0) return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int virtio_gpu_set_scanout(void) {
    virtio_gpu_set_scanout_t req;
    virtio_gpu_ctrl_hdr_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    req.r.x = 0;
    req.r.y = 0;
    req.r.width = g_width;
    req.r.height = g_height;
    req.scanout_id = 0;
    req.resource_id = g_resource_id;

    if (virtio_gpu_send(&req, sizeof(req), &resp, sizeof(resp)) != 0) return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int virtio_gpu_transfer_to_host(uint32_t w, uint32_t h) {
    virtio_gpu_transfer_to_host_2d_t req;
    virtio_gpu_ctrl_hdr_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    req.r.x = 0;
    req.r.y = 0;
    req.r.width = w;
    req.r.height = h;
    req.offset = 0;
    req.resource_id = g_resource_id;

    if (virtio_gpu_send(&req, sizeof(req), &resp, sizeof(resp)) != 0) return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int virtio_gpu_resource_flush(uint32_t w, uint32_t h) {
    virtio_gpu_resource_flush_t req;
    virtio_gpu_ctrl_hdr_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    req.r.x = 0;
    req.r.y = 0;
    req.r.width = w;
    req.r.height = h;
    req.resource_id = g_resource_id;

    if (virtio_gpu_send(&req, sizeof(req), &resp, sizeof(resp)) != 0) return -1;
    return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}

static int virtio_gpu_blit32(const ntux_gpu_blit32_t* req) {
    if (!g_backing || !req || !req->src) return -1;
    uint32_t copy_w = req->width;
    uint32_t copy_h = req->height;
    if (copy_w > g_width) copy_w = g_width;
    if (copy_h > g_height) copy_h = g_height;
    if (req->pitch < copy_w * sizeof(uint32_t)) return -1;

    uint32_t* dst = (uint32_t*)g_backing;
    for (uint32_t y = 0; y < copy_h; ++y) {
        const uint8_t* src_row = (const uint8_t*)req->src + (size_t)y * (size_t)req->pitch;
        const uint32_t* s = (const uint32_t*)(const void*)src_row;
        uint32_t* d = dst + (size_t)y * g_width;
        for (uint32_t x = 0; x < copy_w; ++x) {
            uint32_t p = s[x];
            uint8_t b = (uint8_t)(p & 0xFFu);
            uint8_t g = (uint8_t)((p >> 8) & 0xFFu);
            uint8_t r = (uint8_t)((p >> 16) & 0xFFu);
            d[x] = (uint32_t)(0xFFu << 24) | (uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)b;
        }
    }

    if (virtio_gpu_transfer_to_host(copy_w, copy_h) != 0) return -1;
    if (virtio_gpu_resource_flush(copy_w, copy_h) != 0) return -1;
    return 0;
}

static int virtio_gpu_ioctl(struct gpu_device* dev, uint64_t req, void* arg) {
    if (!dev) return -1;
    if (req == NTUX_GPU_IOCTL_GET_INFO) {
        if (!arg) return -1;
        ntux_gpu_info_t* out = (ntux_gpu_info_t*)arg;
        out->width = dev->info.width;
        out->height = dev->info.height;
        out->pitch = dev->info.pitch;
        out->bpp = dev->info.bpp;
        out->fb_size = dev->info.fb_size;
        return 0;
    }
    if (req == NTUX_GPU_IOCTL_BLIT32) {
        if (!g_pci.ready) return -1;
        return virtio_gpu_blit32((const ntux_gpu_blit32_t*)arg);
    }
    return -1;
}

static const gpu_ops_t g_virtio_gpu_ops = {
    .ioctl = virtio_gpu_ioctl
};

static bool virtio_gpu_match(uint32_t bus, uint32_t device, uint32_t function,
                             uint16_t vendor, uint16_t device_id) {
    (void)bus;
    (void)device;
    (void)function;
    if (vendor != VIRTIO_VENDOR_ID) return false;
    return (device_id == VIRTIO_GPU_DEV_ID_LEGACY || device_id == VIRTIO_GPU_DEV_ID_MODERN);
}

static gpu_device_t* virtio_gpu_probe(uint32_t bus, uint32_t device, uint32_t function,
                                      uint16_t vendor, uint16_t device_id) {
    (void)vendor;
    (void)device_id;

    memset(&g_pci, 0, sizeof(g_pci));
    memset(&g_caps, 0, sizeof(g_caps));
    g_pci.present = true;
    g_pci.bus = (uint8_t)bus;
    g_pci.device = (uint8_t)device;
    g_pci.function = (uint8_t)function;
    g_pci.bar0 = pci_read_field(bus, device, function, 0x10, 4);
    g_pci.irq_line = (uint8_t)pci_read_field(bus, device, function, 0x3C, 1);

    if ((g_pci.bar0 & 0x1u) == 0) {
        if (virtio_pci_find_caps(bus, device, function, &g_caps) != 0) {
            kprint("[GPU] VirtIO-GPU modern caps missing\n");
            return NULL;
        }
        g_pci.modern = true;
    } else {
        g_pci.modern = false;
        g_io_base = (uint16_t)(g_pci.bar0 & ~0x3u);
    }

    uint16_t cmd = (uint16_t)pci_read_field(bus, device, function, 0x04, 2);
    cmd |= 0x0005;
    pci_write_field(bus, device, function, 0x04, 2, cmd);

    if (g_pci.modern) {
        g_caps.common->msix_config = 0xFFFF;
        g_caps.common->device_status = 0;
        g_caps.common->device_status = VIRTIO_STATUS_ACK;
        g_caps.common->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER;
        g_caps.common->driver_feature_select = 0;
        g_caps.common->driver_feature = 0;
        g_caps.common->driver_feature_select = 1;
        g_caps.common->driver_feature = 0;
        g_caps.common->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
        if ((g_caps.common->device_status & VIRTIO_STATUS_FEATURES_OK) == 0) {
            g_caps.common->device_status = VIRTIO_STATUS_FAILED;
            kprint("[GPU] VirtIO-GPU feature negotiation failed\n");
            return NULL;
        }
        if (!virtq_setup_modern(0, &g_ctrlq)) {
            g_caps.common->device_status = VIRTIO_STATUS_FAILED;
            kprint("[GPU] VirtIO-GPU queue setup failed\n");
            return NULL;
        }
    } else {
        outb(g_io_base + VIRTIO_PCI_STATUS, 0);
        outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
        outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
        outl(g_io_base + VIRTIO_PCI_GUEST_FEATURES, 0);
        if (!virtq_setup_legacy(0, &g_ctrlq)) {
            outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
            kprint("[GPU] VirtIO-GPU queue setup failed\n");
            return NULL;
        }
    }

    if (virtio_gpu_get_display_info() != 0) {
        if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0) {
            volatile struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
            g_width = (uint32_t)fb->width;
            g_height = (uint32_t)fb->height;
        } else {
            g_width = 640;
            g_height = 480;
        }
    }

    g_backing_size = g_width * g_height * 4u;
    g_backing = (uint8_t*)kmalloc(g_backing_size);
    if (!g_backing) {
        if (g_pci.modern) g_caps.common->device_status = VIRTIO_STATUS_FAILED;
        else outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
        kprint("[GPU] VirtIO-GPU backing alloc failed\n");
        return NULL;
    }
    memset(g_backing, 0, g_backing_size);

    if (virtio_gpu_create_resource() != 0 ||
        virtio_gpu_attach_backing() != 0 ||
        virtio_gpu_set_scanout() != 0) {
        if (g_pci.modern) g_caps.common->device_status = VIRTIO_STATUS_FAILED;
        else outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
        kprint("[GPU] VirtIO-GPU init failed\n");
        return NULL;
    }

    if (g_pci.modern) {
        g_caps.common->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK;
    } else {
        outb(g_io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    }
    g_pci.ready = true;

    memset(&g_virtio_gpu, 0, sizeof(g_virtio_gpu));
    g_virtio_gpu.name = "virtio-gpu";
    g_virtio_gpu.ops = &g_virtio_gpu_ops;
    g_virtio_gpu.info.width = g_width;
    g_virtio_gpu.info.height = g_height;
    g_virtio_gpu.info.pitch = g_width * 4u;
    g_virtio_gpu.info.bpp = 32;
    g_virtio_gpu.info.fb_virt = g_backing;
    g_virtio_gpu.info.fb_phys = (uint64_t)(uintptr_t)g_backing;
    g_virtio_gpu.info.fb_size = g_backing_size;

    kprint("[GPU] VirtIO-GPU ready at PCI ");
    kprint_hex64((uint64_t)bus);
    kprint(":");
    kprint_hex64((uint64_t)device);
    kprint(".");
    kprint_hex64((uint64_t)function);
    kprint(g_pci.modern ? " (modern)" : " (legacy)");
    kprint(" res=");
    kprint_int((int)g_width);
    kprint("x");
    kprint_int((int)g_height);
    kprint("\n");

    return &g_virtio_gpu;
}

static const gpu_driver_t g_virtio_gpu_driver = {
    .name = "virtio-gpu",
    .priority = 100,
    .match_pci = virtio_gpu_match,
    .probe_pci = virtio_gpu_probe
};

void virtio_gpu_register(void) {
    gpu_register_driver(&g_virtio_gpu_driver);
}

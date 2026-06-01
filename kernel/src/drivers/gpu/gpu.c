#include <drivers/gpu/gpu.h>

#include <drivers/pci/pci.h>
#include <drivers/framebuffer/kprint.h>
#include <fs/devfs.h>
#include <limine.h>
#include <lib/string.h>

#include <drivers/gpu/virtio_gpu.h>
#include <drivers/gpu/gpu_ioctl.h>
/* Intel driver deferred: #include <drivers/gpu/intel_braswell.h> */

#define GPU_MAX_DRIVERS 8

extern volatile struct limine_framebuffer_request framebuffer_request;

/* Cycle counter for performance measurement */
static inline uint64_t __rdtsc(void) {
    uint32_t hi, lo;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static const gpu_driver_t* g_drivers[GPU_MAX_DRIVERS];
static size_t g_driver_count = 0;
static gpu_device_t* g_primary_gpu = NULL;
static int g_primary_priority = -1;

static int gpu_devfs_ioctl(void* ctx, uint64_t req, void* arg) {
    gpu_device_t* dev = (gpu_device_t*)ctx;
    if (!dev || !dev->ops || !dev->ops->ioctl) return -1;
    return dev->ops->ioctl(dev, req, arg);
}

static const devfs_ops_t g_gpu_devfs_ops = {
    .read = NULL,
    .write = NULL,
    .ioctl = gpu_devfs_ioctl
};

void gpu_register_driver(const gpu_driver_t* driver) {
    if (!driver || g_driver_count >= GPU_MAX_DRIVERS) return;
    g_drivers[g_driver_count++] = driver;
}

void gpu_register_device(gpu_device_t* dev, int priority) {
    if (!dev) return;
    if (!g_primary_gpu || priority > g_primary_priority) {
        g_primary_gpu = dev;
        g_primary_priority = priority;
        devfs_register("dri/card0", &g_gpu_devfs_ops, dev);
    }
}

const gpu_device_t* gpu_get_primary(void) {
    return g_primary_gpu;
}

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b, volatile struct limine_framebuffer* fb) {
    uint32_t out = 0;
    if (fb->red_mask_size) {
        uint32_t rmax = (1u << fb->red_mask_size) - 1u;
        uint32_t rv = (uint32_t)((r * rmax) / 255u);
        out |= (rv & rmax) << fb->red_mask_shift;
    }
    if (fb->green_mask_size) {
        uint32_t gmax = (1u << fb->green_mask_size) - 1u;
        uint32_t gv = (uint32_t)((g * gmax) / 255u);
        out |= (gv & gmax) << fb->green_mask_shift;
    }
    if (fb->blue_mask_size) {
        uint32_t bmax = (1u << fb->blue_mask_size) - 1u;
        uint32_t bv = (uint32_t)((b * bmax) / 255u);
        out |= (bv & bmax) << fb->blue_mask_shift;
    }
    return out;
}

static int gpu_blit32_to_fb(const ntux_gpu_blit32_t* req, volatile struct limine_framebuffer* fb) {
    if (!req || !req->src || !fb) return -1;
    if (req->width == 0 || req->height == 0 || req->pitch == 0) return -1;
    if (req->pitch < req->width * sizeof(uint32_t)) return -1;
    if (fb->bpp != 32 && fb->bpp != 24) return -1;
    if (fb->memory_model != LIMINE_FRAMEBUFFER_RGB) return -1;

    uint32_t copy_w = req->width;
    uint32_t copy_h = req->height;
    if (copy_w > (uint32_t)fb->width) copy_w = (uint32_t)fb->width;
    if (copy_h > (uint32_t)fb->height) copy_h = (uint32_t)fb->height;

    uint64_t start_cycles = __rdtsc();

    uint8_t* dst = (uint8_t*)(uintptr_t)fb->address;
    for (uint32_t y = 0; y < copy_h; ++y) {
        const uint8_t* src_row = (const uint8_t*)req->src + (size_t)y * (size_t)req->pitch;
        uint8_t* dst_row = dst + (size_t)y * (size_t)fb->pitch;
        const uint32_t* s = (const uint32_t*)(const void*)src_row;
        for (uint32_t x = 0; x < copy_w; ++x) {
            uint32_t p = s[x];
            uint8_t b = (uint8_t)(p & 0xFFu);
            uint8_t g = (uint8_t)((p >> 8) & 0xFFu);
            uint8_t r = (uint8_t)((p >> 16) & 0xFFu);
            uint32_t out = pack_rgb(r, g, b, fb);
            if (fb->bpp == 32) {
                dst_row[x * 4u + 0u] = (uint8_t)(out & 0xFFu);
                dst_row[x * 4u + 1u] = (uint8_t)((out >> 8) & 0xFFu);
                dst_row[x * 4u + 2u] = (uint8_t)((out >> 16) & 0xFFu);
                dst_row[x * 4u + 3u] = (uint8_t)((out >> 24) & 0xFFu);
            } else {
                dst_row[x * 3u + 0u] = (uint8_t)(out & 0xFFu);
                dst_row[x * 3u + 1u] = (uint8_t)((out >> 8) & 0xFFu);
                dst_row[x * 3u + 2u] = (uint8_t)((out >> 16) & 0xFFu);
            }
        }
    }

    uint64_t end_cycles = __rdtsc();
    return (int)(end_cycles - start_cycles);
}

static int gpu_ioctl_impl(struct gpu_device* dev, uint64_t req, void* arg) {
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
        if (!arg) return -1;
        if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1) {
            dev->stats.blit_errors++;
            return -1;
        }
        volatile struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
        int result = gpu_blit32_to_fb((const ntux_gpu_blit32_t*)arg, fb);
        if (result < 0) {
            dev->stats.blit_errors++;
            return -1;
        }
        dev->stats.blit_count++;
        dev->stats.blit_cycles += (uint64_t)result;
        return 0;
    }
    if (req == NTUX_GPU_IOCTL_GET_STATS) {
        if (!arg) return -1;
        ntux_gpu_stats_t* out = (ntux_gpu_stats_t*)arg;
        out->blit_count = dev->stats.blit_count;
        out->blit_errors = dev->stats.blit_errors;
        out->blit_cycles = dev->stats.blit_cycles;
        out->current_memory_usage = dev->stats.current_memory_usage;
        out->max_memory_usage = dev->stats.max_memory_usage;
        out->memory_allocations = dev->stats.memory_allocations;
        return 0;
    }
    return -1;
}

static const gpu_ops_t g_default_gpu_ops = {
    .ioctl = gpu_ioctl_impl
};

const gpu_ops_t* gpu_default_ops(void) {
    return &g_default_gpu_ops;
}

static void gpu_pci_probe(uint32_t bus, uint32_t device, uint32_t function,
                          uint16_t vendor, uint16_t device_id, void* extra) {
    (void)extra;
    for (size_t i = 0; i < g_driver_count; ++i) {
        const gpu_driver_t* drv = g_drivers[i];
        if (!drv || !drv->match_pci) continue;
        if (!drv->match_pci(bus, device, function, vendor, device_id)) continue;
        if (!drv->probe_pci) continue;
        gpu_device_t* dev = drv->probe_pci(bus, device, function, vendor, device_id);
        if (dev) {
            gpu_register_device(dev, drv->priority);
        }
    }
}

static gpu_device_t* gpu_bootfb_create(void) {
    if (!framebuffer_request.response || framebuffer_request.response->framebuffer_count < 1) {
        return NULL;
    }

    static gpu_device_t bootfb;
    volatile struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
    memset(&bootfb, 0, sizeof(bootfb));
    bootfb.name = "bootfb";
    bootfb.ops = &g_default_gpu_ops;
    bootfb.info.width = (uint32_t)fb->width;
    bootfb.info.height = (uint32_t)fb->height;
    bootfb.info.pitch = (uint32_t)fb->pitch;
    bootfb.info.bpp = (uint32_t)fb->bpp;
    bootfb.info.fb_virt = (void*)(uintptr_t)fb->address;
    bootfb.info.fb_phys = (uint64_t)(uintptr_t)fb->address;
    bootfb.info.fb_size = (uint32_t)(fb->pitch * fb->height);
    return &bootfb;
}

void gpu_init(void) {
    kprint("[GPU] Initializing GPU subsystem...\n");
    
    /* Register available GPU drivers */
    virtio_gpu_register();
    /* intel_gpu_register();  Intel driver deferred for future integration */
    
    /* Scan PCI for GPUs */
    pci_scan_ex(gpu_pci_probe, NULL);

    if (!g_primary_gpu) {
        gpu_device_t* bootfb = gpu_bootfb_create();
        if (bootfb) {
            gpu_register_device(bootfb, 1);
            kprint_ok("GPU: boot framebuffer registered");
        } else {
            kprint_error("GPU: no framebuffer available");
        }
    }
}

gpu_stats_t gpu_get_stats(const gpu_device_t* dev) {
    gpu_stats_t empty = {0};
    if (!dev) return empty;
    return dev->stats;
}

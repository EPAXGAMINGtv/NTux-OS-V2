#ifndef GPU_H
#define GPU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct gpu_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t fb_phys;
    void* fb_virt;
    uint32_t fb_size;
} gpu_info_t;

typedef struct gpu_stats {
    uint64_t blit_count;
    uint64_t blit_errors;
    uint64_t blit_cycles;
    uint32_t current_memory_usage;
    uint32_t max_memory_usage;
    uint32_t memory_allocations;
} gpu_stats_t;

struct gpu_device;

typedef struct gpu_ops {
    int (*ioctl)(struct gpu_device* dev, uint64_t req, void* arg);
} gpu_ops_t;

typedef struct gpu_device {
    const char* name;
    gpu_info_t info;
    gpu_stats_t stats;
    const gpu_ops_t* ops;
} gpu_device_t;

typedef struct gpu_driver {
    const char* name;
    int priority;
    bool (*match_pci)(uint32_t bus, uint32_t device, uint32_t function,
                      uint16_t vendor, uint16_t device_id);
    gpu_device_t* (*probe_pci)(uint32_t bus, uint32_t device, uint32_t function,
                               uint16_t vendor, uint16_t device_id);
} gpu_driver_t;

void gpu_register_driver(const gpu_driver_t* driver);
void gpu_register_device(gpu_device_t* dev, int priority);
const gpu_device_t* gpu_get_primary(void);
void gpu_init(void);
const gpu_ops_t* gpu_default_ops(void);
gpu_stats_t gpu_get_stats(const gpu_device_t* dev);

#endif

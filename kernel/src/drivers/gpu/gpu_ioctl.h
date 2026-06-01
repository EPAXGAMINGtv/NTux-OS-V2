#ifndef GPU_IOCTL_H
#define GPU_IOCTL_H

#include <stdint.h>

#define NTUX_GPU_IOCTL_BASE 0x47505500u
#define NTUX_GPU_IOCTL_GET_INFO (NTUX_GPU_IOCTL_BASE + 1u)
#define NTUX_GPU_IOCTL_BLIT32   (NTUX_GPU_IOCTL_BASE + 2u)
#define NTUX_GPU_IOCTL_GET_STATS (NTUX_GPU_IOCTL_BASE + 3u)

typedef struct ntux_gpu_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t fb_size;
} ntux_gpu_info_t;

typedef struct ntux_gpu_blit32 {
    const void* src;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} ntux_gpu_blit32_t;

typedef struct ntux_gpu_stats {
    uint64_t blit_count;
    uint64_t blit_errors;
    uint64_t blit_cycles;
    uint32_t current_memory_usage;
    uint32_t max_memory_usage;
    uint32_t memory_allocations;
} ntux_gpu_stats_t;

#endif

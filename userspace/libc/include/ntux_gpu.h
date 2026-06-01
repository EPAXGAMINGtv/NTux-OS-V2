#ifndef NTUX_GPU_H
#define NTUX_GPU_H

#include <stdint.h>
#include <syscall.h>  /* Use ntux_gpu_info_t from syscall.h */

#define NTUX_GPU_IOCTL_BASE 0x47505500u
#define NTUX_GPU_IOCTL_GET_INFO (NTUX_GPU_IOCTL_BASE + 1u)
#define NTUX_GPU_IOCTL_BLIT32   (NTUX_GPU_IOCTL_BASE + 2u)

typedef struct ntux_gpu_blit32 {
    const void* src;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} ntux_gpu_blit32_t;

int ntux_gpu_get_info(ntux_gpu_info_t* out);
int ntux_gpu_blit32(const void* src, uint32_t width, uint32_t height, uint32_t pitch);

#endif

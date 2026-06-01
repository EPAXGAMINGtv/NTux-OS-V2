#include <ntux_gpu.h>
#include <syscall.h>
#include <unistd.h>

int ntux_gpu_get_info(ntux_gpu_info_t* out) {
    if (!out) return -1;
    int fd = (int)sys_open("/dev/dri/card0", 0);
    if (fd < 0) return -1;
    int rc = (int)sys_ioctl(fd, NTUX_GPU_IOCTL_GET_INFO, out);
    (void)sys_close(fd);
    return rc;
}

int ntux_gpu_blit32(const void* src, uint32_t width, uint32_t height, uint32_t pitch) {
    if (!src) return -1;
    ntux_gpu_blit32_t req = {
        .src = src,
        .width = width,
        .height = height,
        .pitch = pitch
    };
    int fd = (int)sys_open("/dev/dri/card0", 0);
    if (fd < 0) return -1;
    int rc = (int)sys_ioctl(fd, NTUX_GPU_IOCTL_BLIT32, &req);
    (void)sys_close(fd);
    return rc;
}

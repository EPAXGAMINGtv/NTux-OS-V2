#ifndef GPU_FB_H
#define GPU_FB_H

#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include "gpu.h"

extern gpu_driver_t gpu_fb_driver;

int gpu_fb_init(volatile struct limine_framebuffer* fb);

#endif

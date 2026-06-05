#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "gpu.h"
#include <drivers/pci/pci.h>

extern gpu_driver_t virtio_gpu_driver;

int virtio_gpu_init(pci_device_t* dev);
void virtio_gpu_flush(int x, int y, int w, int h);

#endif

#ifndef NTUX_IMAGE_H
#define NTUX_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int width;
    int height;
    int channels;
    uint8_t* data;
} image_t;

/* desired_channels: 0 = keep source, 3 = RGB, 4 = RGBA */
int image_decode_file(const char* path, int desired_channels, image_t* out);
int image_decode_memory(const void* data, size_t len, int desired_channels, image_t* out);
int image_decode_file_scaled(const char* path, int desired_channels, int max_w, int max_h, image_t* out);
int image_decode_memory_scaled(const void* data, size_t len, int desired_channels, int max_w, int max_h, image_t* out);
void image_free(image_t* img);
const char* image_failure_reason(void);

#endif

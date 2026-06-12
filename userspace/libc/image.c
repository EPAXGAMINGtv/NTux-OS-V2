#include <image.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define STBI_NO_SIMD
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define IMAGE_MAX_FILE_BYTES (128ull * 1024ull * 1024ull)

int image_decode_memory(const void* data, size_t len, int desired_channels, image_t* out) {
    if (!out) return -1;
    out->width = 0; out->height = 0; out->channels = 0; out->data = 0;
    if (!data || len == 0) return -1;
    if (desired_channels != 0 && desired_channels != 3 && desired_channels != 4) return -2;

    int w, h, comp;
    int req = desired_channels;
    stbi_uc* pixels = stbi_load_from_memory((const stbi_uc*)data, (int)len, &w, &h, &comp, req);
    if (!pixels) return -1;

    out->data = pixels;
    out->width = w;
    out->height = h;
    out->channels = req ? req : comp;
    return 0;
}

static int read_file_to_buf(const char* path, uint8_t** out_buf, size_t* out_len) {
    uint64_t len = 0;
    if (sys_fs_read_file(path, 0, 0, &len) != 0 || len == 0) return -1;
    if (len > IMAGE_MAX_FILE_BYTES) return -5;
    uint8_t* buf = (uint8_t*)malloc((size_t)len);
    if (!buf) return -4;
    if (sys_fs_read_file(path, buf, len, &len) != 0) {
        free(buf);
        return -1;
    }
    *out_buf = buf;
    *out_len = (size_t)len;
    return 0;
}

int image_decode_file(const char* path, int desired_channels, image_t* out) {
    if (!out) return -1;
    out->width = 0; out->height = 0; out->channels = 0; out->data = 0;
    if (!path || !path[0]) return -1;
    if (desired_channels != 0 && desired_channels != 3 && desired_channels != 4) return -2;

    uint8_t* buf = NULL;
    size_t len = 0;
    int rc = read_file_to_buf(path, &buf, &len);
    if (rc != 0) return rc;
    rc = image_decode_memory(buf, len, desired_channels, out);
    free(buf);
    return rc;
}

const char* image_failure_reason(void) {
    return stbi_failure_reason();
}

void image_free(image_t* img) {
    if (!img || !img->data) return;
    stbi_image_free(img->data);
    img->data = 0;
    img->width = 0;
    img->height = 0;
    img->channels = 0;
}

static int safe_mul(size_t a, size_t b, size_t* out) {
    if (a == 0 || b == 0) { *out = 0; return 0; }
    if (a > ((size_t)-1) / b) return -1;
    *out = a * b;
    return 0;
}

static int image_downscale_nearest(image_t* img, int max_w, int max_h) {
    if (!img || !img->data || img->width <= 0 || img->height <= 0) return -1;
    if (max_w <= 0 && max_h <= 0) return 0;
    int tw = img->width;
    int th = img->height;
    if (max_w > 0 && tw > max_w) {
        th = (int)((int64_t)th * max_w / (tw ? tw : 1));
        tw = max_w;
    }
    if (max_h > 0 && th > max_h) {
        tw = (int)((int64_t)tw * max_h / (th ? th : 1));
        th = max_h;
    }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    if (tw == img->width && th == img->height) return 0;

    int ch = img->channels;
    size_t out_sz = 0;
    if (safe_mul((size_t)tw, (size_t)th, &out_sz) != 0) return -1;
    if (safe_mul(out_sz, (size_t)ch, &out_sz) != 0) return -1;
    uint8_t* out = (uint8_t*)malloc(out_sz);
    if (!out) return -1;

    for (int y = 0; y < th; ++y) {
        int sy = (int)((int64_t)y * img->height / (th ? th : 1));
        const uint8_t* row = img->data + (size_t)sy * (size_t)img->width * (size_t)ch;
        uint8_t* dst = out + (size_t)y * (size_t)tw * (size_t)ch;
        for (int x = 0; x < tw; ++x) {
            int sx = (int)((int64_t)x * img->width / (tw ? tw : 1));
            const uint8_t* px = row + (size_t)sx * (size_t)ch;
            memcpy(&dst[x * ch], px, (size_t)ch);
        }
    }
    free(img->data);
    img->data = out;
    img->width = tw;
    img->height = th;
    return 0;
}

int image_decode_memory_scaled(const void* data, size_t len, int desired_channels, int max_w, int max_h, image_t* out) {
    int rc = image_decode_memory(data, len, desired_channels, out);
    if (rc != 0) return rc;
    (void)image_downscale_nearest(out, max_w, max_h);
    return 0;
}

int image_decode_file_scaled(const char* path, int desired_channels, int max_w, int max_h, image_t* out) {
    if (!out) return -1;
    out->width = 0; out->height = 0; out->channels = 0; out->data = 0;
    if (!path || !path[0]) return -1;
    if (desired_channels != 0 && desired_channels != 3 && desired_channels != 4) return -2;

    uint8_t* buf = NULL;
    size_t len = 0;
    int rc = read_file_to_buf(path, &buf, &len);
    if (rc != 0) return rc;
    rc = image_decode_memory_scaled(buf, len, desired_channels, max_w, max_h, out);
    free(buf);
    return rc;
}

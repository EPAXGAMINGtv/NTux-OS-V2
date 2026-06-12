#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <window.h>
#include <image.h>

void ntux_user_entry(void) {
    char req_path[64];
    snprintf(req_path, sizeof(req_path), "/tmp/imgdecode_req.%ld", sys_get_tid());

    char buf[512];
    uint64_t len = 0;
    if (sys_fs_read_file(req_path, 0, 0, &len) != 0 || len == 0 || len >= sizeof(buf))
        sys_exit(1);
    if (sys_fs_read_file(req_path, buf, len, &len) != 0) sys_exit(1);
    buf[len] = '\0';
    (void)sys_fs_remove(req_path);

    char* line = buf;
    char* next = strchr(line, '\n');
    if (!next) sys_exit(1);
    *next = '\0';
    uint64_t win_id = strtoull(line, 0, 16);
    line = next + 1;

    next = strchr(line, '\n');
    if (!next) sys_exit(1);
    *next = '\0';
    char path[256];
    strncpy(path, line, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    line = next + 1;

    next = strchr(line, '\n');
    if (!next) sys_exit(1);
    *next = '\0';
    int desired = atoi(line);
    if (desired != 3 && desired != 4) desired = 3;
    line = next + 1;

    next = strchr(line, '\n');
    if (!next) sys_exit(1);
    *next = '\0';
    int max_w = atoi(line);
    line = next + 1;

    next = strchr(line, '\n');
    if (!next) sys_exit(1);
    *next = '\0';
    int max_h = atoi(line);
    line = next + 1;

    char return_file[128] = "";
    if (line && line[0]) {
        next = strchr(line, '\n');
        if (next) *next = '\0';
        strncpy(return_file, line, sizeof(return_file) - 1);
        return_file[sizeof(return_file) - 1] = '\0';
    }

    image_t img;
    int r;
    if (max_w > 0 && max_h > 0)
        r = image_decode_file_scaled(path, desired, max_w, max_h, &img);
    else
        r = image_decode_file(path, desired, &img);
    if (r != 0) sys_exit(1);
    if (!img.data || img.width <= 0 || img.height <= 0) {
        image_free(&img);
        sys_exit(1);
    }

    size_t pixel_bytes = (size_t)img.width * (size_t)img.height * (size_t)img.channels;

    if (return_file[0]) {
        char hdr[64];
        int hdr_len = snprintf(hdr, sizeof(hdr), "%d\n%d\n%d\n", img.width, img.height, img.channels);
        size_t total = (size_t)hdr_len + pixel_bytes;
        uint8_t* out = (uint8_t*)malloc(total);
        if (out) {
            memcpy(out, hdr, (size_t)hdr_len);
            memcpy(out + (size_t)hdr_len, img.data, pixel_bytes);
            const char* name = strrchr(return_file, '/');
            if (name) name++; else name = return_file;
            char parent[64];
            size_t plen = (size_t)(name - return_file);
            if (plen > sizeof(parent) - 1) plen = sizeof(parent) - 1;
            memcpy(parent, return_file, plen);
            parent[plen] = '\0';
            if (plen > 1 && parent[plen - 1] == '/') parent[plen - 1] = '\0';
            (void)sys_fs_create_file(parent[0] ? parent : "/tmp", name, (const char*)out, (uint64_t)total);
            free(out);
        }
    } else {
        if (pixel_bytes > 0) {
            (void)window_set_image_raw(win_id, img.width, img.height, img.channels, img.data, (uint32_t)pixel_bytes);
        }
    }

    image_free(&img);
    sys_exit(0);
}

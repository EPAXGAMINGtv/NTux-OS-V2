#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <window.h>

static uint8_t g_key_last[128];

static int key_edge(int sc) {
    int now = (sys_kbd_is_pressed((uint8_t)sc) > 0) ? 1 : 0;
    int pressed = (now && !g_key_last[sc]) ? 1 : 0;
    g_key_last[sc] = (uint8_t)now;
    return pressed;
}

static int read_start_path(char* out, size_t cap) {
    uint64_t len = 0;
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    if (sys_fs_read_file("/tmp/imgview_path", 0, 0, &len) != 0 || len == 0 || len >= cap) return -1;
    if (sys_fs_read_file("/tmp/imgview_path", out, len, &len) != 0) return -1;
    if (len >= cap) len = cap - 1;
    out[len] = '\0';
    return 0;
}

static const char* path_basename_ptr(const char* path) {
    const char* last = path;
    if (!path) return "";
    for (const char* p = path; *p; ++p) {
        if (*p == '/') last = p + 1;
    }
    return last;
}

void ntux_user_entry(void) {
    window_t id = 0x494D475645576Dull; /* "IMGVIEW" */
    int w = 900, h = 600;
    if (window_init() != 0 || window_create(id, 80, 60, w, h, 0xFF0B1119u, "Image Viewer") != 0) {
        sys_exit(1);
    }
    (void)window_set_icon(id, "/boot/res/icons/imgview.bmp");

    char current[256] = "";
    window_set_title(id, "Image Viewer");
    if (read_start_path(current, sizeof(current)) == 0) {
        (void)window_set_image(id, current, 4);
        window_set_title(id, path_basename_ptr(current));
    } else {
        window_open_file_picker("Open Image", "/", 0);
    }

    for (;;) {
        if (window_should_close(id)) break;
        if (key_edge(0x01)) break; /* Esc */
        long ch = sys_getchar();
        if (ch == 'o' || ch == 'O') {
            window_open_file_picker("Open Image", "/", 0);
        }

        char path[256];
        uint32_t code = 0;
        if (window_dialog_pop(path, sizeof(path), &code) == 0) {
            if (code == 1 && path[0]) {
                strncpy(current, path, sizeof(current) - 1);
                current[sizeof(current) - 1] = '\0';
                (void)window_set_image(id, current, 4);
                window_set_title(id, path_basename_ptr(current));
            }
        }
        sys_wait_ticks(1);
    }
    sys_exit(0);
}

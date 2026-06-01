#include "settings_app.h"
#include <string.h>
#include <syscall.h>

static void trim_line(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[n - 1] = '\0';
        --n;
    }
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) memmove(s, s + i, strlen(s + i) + 1u);
}

static void ensure_conf_dir(void) {
    if (sys_fs_exists("/conf") <= 0) {
        (void)sys_fs_mkdir("/", "conf");
    }
}

static int read_conf(const char* path, char* out, uint64_t cap) {
    uint64_t len = 0;
    if (!out || cap == 0) return -1;
    if (sys_fs_read_file(path, out, cap - 1u, &len) != 0) return -1;
    if (len >= cap) len = cap - 1u;
    out[len] = '\0';
    trim_line(out);
    return 0;
}

static int write_conf(const char* path, const char* value) {
    if (!path || !value) return -1;
    ensure_conf_dir();
    uint64_t len = (uint64_t)strlen(value);
    if (sys_fs_write_file(path, value, len) == 0) return 0;
    if (strcmp(path, TIME_CONF_PATH) == 0) {
        return (sys_fs_create_file("/conf", "time.conf", value, len) == 0) ? 0 : -1;
    }
    if (strcmp(path, KBD_CONF_PATH) == 0) {
        return (sys_fs_create_file("/conf", "kbdlout.conf", value, len) == 0) ? 0 : -1;
    }
    return -1;
}

void settings_load_state(settings_state_t* st) {
    char tmp[128];
    if (!st) return;
    st->tz_sel = 0;
    st->kbd_sel = 0;
    st->focus = 0;
    st->tz_open = 0;
    st->kbd_open = 0;
    st->tz_scroll = 0;
    st->kbd_scroll = 0;
    st->status[0] = '\0';
    st->status_until = 0;

    if (read_conf(TIME_CONF_PATH, tmp, sizeof(tmp)) == 0) {
        for (int i = 0; i < settings_timezone_count(); ++i) {
            if (strcmp(tmp, settings_timezone_at(i)) == 0) {
                st->tz_sel = i;
                break;
            }
        }
    }
    if (read_conf(KBD_CONF_PATH, tmp, sizeof(tmp)) == 0) {
        for (int i = 0; i < settings_kbd_count(); ++i) {
            if (strcmp(tmp, settings_kbd_at(i)) == 0) {
                st->kbd_sel = i;
                break;
            }
        }
    }
}

int settings_save_state(const settings_state_t* st) {
    if (!st) return -1;
    int ok1 = write_conf(TIME_CONF_PATH, settings_timezone_at(st->tz_sel)) == 0;
    int ok2 = write_conf(KBD_CONF_PATH, settings_kbd_at(st->kbd_sel)) == 0;
    return (ok1 && ok2) ? 0 : -1;
}

void settings_show_status(settings_state_t* st, const char* msg) {
    if (!st || !msg) return;
    strncpy(st->status, msg, sizeof(st->status) - 1);
    st->status[sizeof(st->status) - 1] = '\0';
    st->status_until = sys_get_ticks() + 180;
}

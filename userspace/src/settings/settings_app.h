#ifndef NTUX_SETTINGS_APP_H
#define NTUX_SETTINGS_APP_H

#include <stdint.h>
#include <window.h>

#define SET_WIN_W 760
#define SET_WIN_H 460

#define TIME_CONF_PATH "/conf/time.conf"
#define KBD_CONF_PATH  "/conf/kbdlout.conf"

typedef struct {
    int tz_sel;
    int kbd_sel;
    int focus;
    int tz_open;
    int kbd_open;
    int tz_scroll;
    int kbd_scroll;
    char status[64];
    uint64_t status_until;
} settings_state_t;

typedef enum {
    SETTINGS_ACT_NONE = 0,
    SETTINGS_ACT_SAVE = 1,
    SETTINGS_ACT_CLOSE = 2
} settings_action_t;

int settings_timezone_count(void);
const char* settings_timezone_at(int idx);
int settings_kbd_count(void);
const char* settings_kbd_at(int idx);

void settings_load_state(settings_state_t* st);
int settings_save_state(const settings_state_t* st);
void settings_show_status(settings_state_t* st, const char* msg);

void settings_draw(window_t id, const settings_state_t* st);
settings_action_t settings_handle_click(settings_state_t* st, int mx, int my);

#endif

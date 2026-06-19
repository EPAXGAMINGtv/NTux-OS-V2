#include "settings_app.h"
#include <string.h>

static const char* g_timezones[] = {
    "UTC", "Europe/Berlin", "Europe/London", "Europe/Paris",
    "America/New_York", "America/Los_Angeles", "America/Sao_Paulo",
    "Asia/Tokyo", "Asia/Shanghai"
};

static const char* g_kbd_layouts[] = {
    "us", "de", "fr", "es", "it", "pt", "pl", "ru", "tr"
};

static const char* g_fonts[] = {
    "/boot/res/fonts/Hack-Regular.ttf",
    "/boot/res/fonts/JetBrainsMono.ttf",
    "/boot/res/fonts/FiraCode.ttf",
    "/boot/res/fonts/DejaVuSans.ttf",
    "/boot/res/fonts/DejaVuSansMono.ttf",
    "/boot/res/fonts/LiberationMono.ttf"
};

int settings_timezone_count(void) {
    return (int)(sizeof(g_timezones) / sizeof(g_timezones[0]));
}
const char* settings_timezone_at(int idx) {
    if (idx < 0 || idx >= settings_timezone_count()) return g_timezones[0];
    return g_timezones[idx];
}
int settings_kbd_count(void) {
    return (int)(sizeof(g_kbd_layouts) / sizeof(g_kbd_layouts[0]));
}
const char* settings_kbd_at(int idx) {
    if (idx < 0 || idx >= settings_kbd_count()) return g_kbd_layouts[0];
    return g_kbd_layouts[idx];
}
int settings_font_count(void) {
    return (int)(sizeof(g_fonts) / sizeof(g_fonts[0]));
}
const char* settings_font_at(int idx) {
    if (idx < 0 || idx >= settings_font_count()) return g_fonts[0];
    return g_fonts[idx];
}

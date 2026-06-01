#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
    int32_t scroll;
    uint8_t left;
    uint8_t right;
    uint8_t middle;
} input_mouse_state_t;

typedef struct {
    uint64_t sec;
    uint64_t usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
} input_event_t;

// Minimal evdev constants used by /dev/input/event* ioctls
#define EV_KEY 0x01
#define EV_REL 0x02
#define REL_X 0x00
#define REL_Y 0x01
#define REL_WHEEL 0x08
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

void input_poll(void);
int input_try_getchar(char* out);
int input_key_pressed(uint8_t key);
void input_copy_key_state(uint8_t* out, size_t len);
int input_consume_super_press(void);
void input_mouse_get_state(input_mouse_state_t* out, int bound_w, int bound_h);
void input_inject_scancode_set1(uint8_t scancode, bool pressed);
void input_inject_char(char c);
void input_inject_mouse_report(int dx, int dy, int wheel, bool left, bool right, bool middle);
void input_evdev_push_key(uint8_t scancode, int pressed);
void input_evdev_push_mouse(int dx, int dy, int wheel, bool left, bool right, bool middle);
int input_evdev_pop(input_event_t* out);

#endif

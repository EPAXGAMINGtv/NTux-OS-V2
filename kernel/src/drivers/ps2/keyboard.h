#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define SCANCODE_T  0x14
#define SCANCODE_F4 0x3E
#define SCANCODE_ARROW_UP 0x48
#define SCANCODE_ARROW_LEFT 0x4B
#define SCANCODE_ARROW_RIGHT 0x4D
#define SCANCODE_ARROW_DOWN 0x50

void keyboard_init();
int keyboard_getchar(char* c);
void keyboard_poll();
uint64_t keyboard_get_irq_hits(void);
int keyboard_is_key_pressed(uint8_t key) ;
void keyboard_irq_handler(void);
int keyboard_consume_super_press(void);
int keyboard_consume_key_press(uint8_t key);
void keyboard_inject_scancode_set1(uint8_t scancode, bool pressed);
void keyboard_inject_char(char c);

extern uint8_t shift_pressed;
extern uint8_t ctrl_pressed;
extern uint8_t alt_pressed;
extern uint8_t super_pressed;

#endif

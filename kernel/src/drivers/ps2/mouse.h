#ifndef MOUSE_H
#define MOUSE_H

#include <stdbool.h>
#include <limine.h>
#include <stdint.h>


extern int mouse_X;
extern int mouse_y;
extern int mouse_left;
extern int mouse_right;
extern int mouse_middle;


bool mouse_init(void);
void mouse_irq_handler(void);


void mouse_poll(void);


void mouse_set_bounds(int width, int height);


int mouse_get_x(void);
int mouse_get_y(void);
bool mouse_left_pressed(void);
bool mouse_right_pressed(void);
bool mouse_middle_pressed(void);
int mouse_get_scroll(void);


void draw_mouse_cursor(volatile struct limine_framebuffer* fb);


bool mouse_data();
void mouse_inject_report(int dx, int dy, int wheel, bool left, bool right, bool middle);

#endif

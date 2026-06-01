#ifndef NTUX_DESKTOP_TASKBAR_H
#define NTUX_DESKTOP_TASKBAR_H

int taskbar_window_at(int x, int y);
int taskbar_window_rect(int idx, int* out_x, int* out_y, int* out_w, int* out_h);
int taskbar_start_hit(int x, int y);
int taskbar_clock_hit(int x, int y);
int taskbar_gallery_hit(int x, int y);
void taskbar_draw(void);

#endif

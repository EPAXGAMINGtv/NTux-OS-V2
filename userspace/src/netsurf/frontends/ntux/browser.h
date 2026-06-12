#ifndef NTUX_BROWSER_H
#define NTUX_BROWSER_H

#include <stdint.h>
#include "netsurf/window.h"

struct gui_window {
	struct gui_window *r_next;
	struct gui_window *r_prev;
	uint32_t win_num;
	struct browser_window *bw;
	uint64_t ntux_win;

	int width;
	int height;
	int scrollx;
	int scrolly;
};

extern struct gui_window_table *ntux_window_table;

struct gui_window *ntux_find_window_by_num(uint32_t win_num);
void ntux_kill_browser_windows(void);
void ntux_redraw_window(struct gui_window *gw);

#endif

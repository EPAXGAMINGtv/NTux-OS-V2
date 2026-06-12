#ifndef NTUX_BITMAP_H
#define NTUX_BITMAP_H

#include <stdbool.h>
#include <stddef.h>

struct bitmap {
	void *ptr;
	size_t rowstride;
	int width;
	int height;
	bool opaque;
};

extern struct gui_bitmap_table *ntux_bitmap_table;

#endif

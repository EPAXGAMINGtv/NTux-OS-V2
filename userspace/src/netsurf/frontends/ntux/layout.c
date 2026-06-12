#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "netsurf/layout.h"
#include "netsurf/plot_style.h"

#include "ntux/layout.h"

static nserror ntux_layout_width(const plot_font_style_t *fstyle,
				 const char *string, size_t length,
				 int *width)
{
	(void)fstyle;
	*width = length * 8;
	return NSERROR_OK;
}

static nserror ntux_layout_position(const plot_font_style_t *fstyle,
				    const char *string, size_t length,
				    int x, size_t *char_offset, int *actual_x)
{
	(void)fstyle;
	(void)string;
	size_t pos = (x + 4) / 8;
	if (pos > length) pos = length;
	*char_offset = pos;
	*actual_x = pos * 8;
	return NSERROR_OK;
}

static nserror ntux_layout_split(const plot_font_style_t *fstyle,
				 const char *string, size_t length,
				 int x, size_t *char_offset, int *actual_x)
{
	(void)fstyle;
	(void)string;
	size_t pos = x / 8;
	if (pos > length) pos = length;
	*char_offset = pos;
	*actual_x = pos * 8;
	return NSERROR_OK;
}

static struct gui_layout_table layout_table = {
	.width = ntux_layout_width,
	.position = ntux_layout_position,
	.split = ntux_layout_split,
};

struct gui_layout_table *ntux_layout_table = &layout_table;

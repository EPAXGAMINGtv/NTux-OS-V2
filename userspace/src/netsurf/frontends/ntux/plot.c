#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "utils/utils.h"
#include "utils/errors.h"
#include "netsurf/plotters.h"
#include "netsurf/bitmap.h"

#include "ntux/plot.h"
#include "ntux/bitmap.h"

#include <font8x8_basic.h>

static uint32_t *render_buf = NULL;
static int render_w = 0;
static int render_h = 0;
static int clip_x0, clip_y0, clip_x1, clip_y1;

void ntux_plot_set_buffer(uint32_t *buf, int w, int h)
{
	render_buf = buf;
	render_w = w;
	render_h = h;
	clip_x0 = 0; clip_y0 = 0;
	clip_x1 = w; clip_y1 = h;
}

static inline void put_pixel(int x, int y, uint32_t colour)
{
	if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1)
		return;
	if (x < 0 || x >= render_w || y < 0 || y >= render_h)
		return;
	render_buf[y * render_w + x] = colour;
}

static nserror ntux_plot_clip(const struct redraw_context *ctx, const struct rect *clip)
{
	(void)ctx;
	clip_x0 = clip->x0;
	clip_y0 = clip->y0;
	clip_x1 = clip->x1;
	clip_y1 = clip->y1;
	return NSERROR_OK;
}

static nserror ntux_plot_arc(const struct redraw_context *ctx, const plot_style_t *pstyle,
			     int x, int y, int radius, int angle1, int angle2)
{
	(void)ctx;
	(void)pstyle;
	(void)x;
	(void)y;
	(void)radius;
	(void)angle1;
	(void)angle2;
	return NSERROR_OK;
}

static nserror ntux_plot_disc(const struct redraw_context *ctx, const plot_style_t *pstyle,
			      int x, int y, int radius)
{
	(void)ctx;
	(void)pstyle;
	(void)x;
	(void)y;
	(void)radius;
	return NSERROR_OK;
}

static nserror ntux_plot_line(const struct redraw_context *ctx, const plot_style_t *pstyle,
			      const struct rect *line)
{
	(void)ctx;
	(void)pstyle;
	(void)line;
	return NSERROR_OK;
}

static nserror ntux_plot_rectangle(const struct redraw_context *ctx, const plot_style_t *pstyle,
				   const struct rect *rect)
{
	uint32_t colour = pstyle->fill_colour;
	int x0 = rect->x0, y0 = rect->y0, x1 = rect->x1, y1 = rect->y1;

	if (x0 < clip_x0) x0 = clip_x0;
	if (y0 < clip_y0) y0 = clip_y0;
	if (x1 > clip_x1) x1 = clip_x1;
	if (y1 > clip_y1) y1 = clip_y1;

	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			put_pixel(x, y, colour);
		}
	}
	return NSERROR_OK;
}

static nserror ntux_plot_polygon(const struct redraw_context *ctx, const plot_style_t *pstyle,
				 const int *p, unsigned int n)
{
	(void)ctx;
	(void)pstyle;
	(void)p;
	(void)n;
	return NSERROR_OK;
}

static nserror ntux_plot_path(const struct redraw_context *ctx, const plot_style_t *pstyle,
			      const float *p, unsigned int n, const float transform[6])
{
	(void)ctx;
	(void)pstyle;
	(void)p;
	(void)n;
	(void)transform;
	return NSERROR_OK;
}

static nserror ntux_plot_bitmap(const struct redraw_context *ctx, struct bitmap *bitmap,
				int x, int y, int width, int height,
				colour bg, bitmap_flags_t flags)
{
	unsigned char *pixels;
	int bmp_w = ((struct bitmap *)bitmap)->width;
	int bmp_h = ((struct bitmap *)bitmap)->height;
	int src_x, src_y;

	pixels = ((struct bitmap *)bitmap)->ptr;

	for (int dy = 0; dy < height; dy++) {
		for (int dx = 0; dx < width; dx++) {
			src_x = dx * bmp_w / width;
			src_y = dy * bmp_h / height;
			unsigned int *src = (unsigned int *)pixels;
			uint32_t col = src[src_y * bmp_w + src_x];
			uint8_t a = (col >> 24) & 0xFF;
			if (a == 0xFF) {
				put_pixel(x + dx, y + dy, col);
			} else if (a > 0) {
				uint32_t bg_col = bg;
				uint32_t r = ((col >> 16) & 0xFF) * a / 255 + ((bg_col >> 16) & 0xFF) * (255 - a) / 255;
				uint32_t g = ((col >> 8) & 0xFF) * a / 255 + ((bg_col >> 8) & 0xFF) * (255 - a) / 255;
				uint32_t b = (col & 0xFF) * a / 255 + (bg_col & 0xFF) * (255 - a) / 255;
				put_pixel(x + dx, y + dy, 0xFF000000 | (r << 16) | (g << 8) | b);
			}
		}
	}
	return NSERROR_OK;
}

static nserror ntux_plot_text(const struct redraw_context *ctx, const plot_font_style_t *fstyle,
			      int x, int y, const char *text, size_t length)
{
	uint32_t colour = fstyle->foreground;
	(void)ctx;
	(void)fstyle;
	for (size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)text[i];
		if (c < 32 || c > 126)
			c = (unsigned char)'?';
		int top = y - 7;
		for (int row = 0; row < 8; row++) {
			uint8_t bits = font8x8_basic[c][row];
			if (bits == 0)
				continue;
			for (int col = 0; col < 8; col++) {
				if (bits & (1 << (7 - col)))
					put_pixel(x + col, top + row, colour);
			}
		}
		x += 8;
	}
	return NSERROR_OK;
}

static nserror ntux_plot_group_start(const struct redraw_context *ctx, const char *name)
{
	(void)ctx;
	(void)name;
	return NSERROR_OK;
}

static nserror ntux_plot_group_end(const struct redraw_context *ctx)
{
	(void)ctx;
	return NSERROR_OK;
}

static nserror ntux_plot_flush(const struct redraw_context *ctx)
{
	(void)ctx;
	return NSERROR_OK;
}

static struct plotter_table ntux_plotters = {
	.clip = ntux_plot_clip,
	.arc = ntux_plot_arc,
	.disc = ntux_plot_disc,
	.line = ntux_plot_line,
	.rectangle = ntux_plot_rectangle,
	.polygon = ntux_plot_polygon,
	.path = ntux_plot_path,
	.bitmap = ntux_plot_bitmap,
	.text = ntux_plot_text,
	.group_start = ntux_plot_group_start,
	.group_end = ntux_plot_group_end,
	.flush = ntux_plot_flush,
	.option_knockout = false,
};

struct plotter_table *ntux_plotters_table = &ntux_plotters;

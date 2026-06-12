#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/ring.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "netsurf/mouse.h"
#include "netsurf/window.h"
#include "netsurf/browser_window.h"
#include "netsurf/plotters.h"
#include "netsurf/bitmap.h"

#include <window.h>

#include "ntux/browser.h"
#include "ntux/plot.h"

#define NTUX_WIN_BASE 0x4E455453555246ull

static uint32_t win_ctr = 0;
static struct gui_window *gw_ring = NULL;

struct gui_window *ntux_find_window_by_num(uint32_t win_num)
{
	struct gui_window *ret = NULL;
	RING_ITERATE_START(struct gui_window, gw_ring, c_ring) {
		if (c_ring->win_num == win_num) {
			ret = c_ring;
			RING_ITERATE_STOP(gw_ring, c_ring);
		}
	} RING_ITERATE_END(gw_ring, c_ring);
	return ret;
}

void ntux_kill_browser_windows(void)
{
	while (gw_ring != NULL) {
		browser_window_destroy(gw_ring->bw);
	}
}

void ntux_redraw_window(struct gui_window *gw)
{
	if (gw == NULL || gw->bw == NULL) return;

	if (!browser_window_redraw_ready(gw->bw)) return;

	static uint32_t *buf = NULL;
	static int buf_size = 0;
	size_t bufsize = gw->width * gw->height * 4;

	if (buf == NULL || buf_size < (int)bufsize) {
		free(buf);
		buf = malloc(bufsize);
		if (buf == NULL) return;
		buf_size = bufsize;
	}
	memset(buf, 0xFF, bufsize);

	ntux_plot_set_buffer(buf, gw->width, gw->height);

	struct redraw_context ctx = {
		.interactive = false,
		.background_images = true,
		.plot = ntux_plotters_table,
		.priv = NULL,
	};

	struct rect clip = {
		.x0 = 0,
		.y0 = 0,
		.x1 = gw->width,
		.y1 = gw->height,
	};

	browser_window_redraw(gw->bw, 0, 0, &clip, &ctx);

	window_set_image_raw(gw->ntux_win, gw->width, gw->height, 4,
			     (const char *)buf, bufsize);
}

static struct gui_window *
gui_window_create(struct browser_window *bw, struct gui_window *existing,
		  gui_window_create_flags flags)
{
	struct gui_window *ret = calloc(1, sizeof(*ret));
	if (ret == NULL) return NULL;

	ret->win_num = win_ctr++;
	ret->bw = bw;
	ret->width = 1024;
	ret->height = 768;

	ret->ntux_win = NTUX_WIN_BASE | (uint64_t)ret->win_num;
	window_init();
	window_create(ret->ntux_win, 50, 50, ret->width, ret->height,
		      0xFFF0F0F0u, "NetSurf");
	window_set_icon(ret->ntux_win, "/boot/res/icons/browser.bmp");
	window_show(ret->ntux_win, 1);

	RING_INSERT(gw_ring, ret);
	
	return ret;
}

static void gui_window_destroy(struct gui_window *g)
{
	window_close(g->ntux_win);
	RING_REMOVE(gw_ring, g);
	free(g);
}

static void gui_window_set_title(struct gui_window *g, const char *title)
{
	window_set_title(g->ntux_win, title);
}

static nserror gui_window_get_dimensions(struct gui_window *g, int *width, int *height)
{
	*width = g->width;
	*height = g->height;
	return NSERROR_OK;
}

static void gui_window_new_content(struct gui_window *g)
{
	(void)g;
}

static void gui_window_set_icon(struct gui_window *g, struct hlcache_handle *icon)
{
	(void)g;
	(void)icon;
}

static void gui_window_start_throbber(struct gui_window *g)
{
	(void)g;
}

static void gui_window_stop_throbber(struct gui_window *g)
{
	(void)g;
}

static nserror gui_window_set_scroll(struct gui_window *gw, const struct rect *rect)
{
	gw->scrollx = rect->x0;
	gw->scrolly = rect->y0;
	return NSERROR_OK;
}

static nserror ntux_window_invalidate_area(struct gui_window *gw, const struct rect *rect)
{
	(void)rect;
	if (gw != NULL) {
		ntux_redraw_window(gw);
	}
	return NSERROR_OK;
}

static void gui_window_update_extent(struct gui_window *g)
{
	int width, height;
	if (browser_window_get_extents(g->bw, false, &width, &height) != NSERROR_OK)
		return;
	(void)width;
	(void)height;
}

static void gui_window_set_status(struct gui_window *g, const char *text)
{
	(void)g;
	(void)text;
}

static void gui_window_set_pointer(struct gui_window *g, gui_pointer_shape shape)
{
	(void)g;
	(void)shape;
}

static nserror gui_window_set_url(struct gui_window *g, nsurl *url)
{
	(void)g;
	(void)url;
	return NSERROR_OK;
}

static bool gui_window_get_scroll(struct gui_window *g, int *sx, int *sy)
{
	*sx = g->scrollx;
	*sy = g->scrolly;
	return true;
}

static bool gui_window_scroll_start(struct gui_window *g)
{
	g->scrollx = g->scrolly = 0;
	return true;
}

static void gui_window_place_caret(struct gui_window *g, int x, int y, int height,
				   const struct rect *clip)
{
	(void)g;
	(void)x;
	(void)y;
	(void)height;
	(void)clip;
}

static void gui_window_remove_caret(struct gui_window *g)
{
	(void)g;
}

static bool gui_window_drag_start(struct gui_window *g, gui_drag_type type,
				  const struct rect *rect)
{
	(void)g;
	(void)type;
	(void)rect;
	return false;
}

static nserror gui_window_save_link(struct gui_window *g, nsurl *url, const char *title)
{
	(void)g;
	(void)url;
	(void)title;
	return NSERROR_OK;
}

static void gui_window_console_log(struct gui_window *g,
				   browser_window_console_source src,
				   const char *msg, size_t msglen,
				   browser_window_console_flags flags)
{
	(void)g;
	(void)src;
	(void)msg;
	(void)msglen;
	(void)flags;
}

static void gui_window_report_page_info(struct gui_window *g)
{
	(void)g;
}

static nserror gui_window_event(struct gui_window *gw, enum gui_window_event event)
{
	switch (event) {
	case GW_EVENT_UPDATE_EXTENT:
		gui_window_update_extent(gw);
		break;
	case GW_EVENT_REMOVE_CARET:
		gui_window_remove_caret(gw);
		break;
	case GW_EVENT_SCROLL_START:
		gui_window_scroll_start(gw);
		break;
	case GW_EVENT_NEW_CONTENT:
		gui_window_new_content(gw);
		break;
	case GW_EVENT_START_THROBBER:
		gui_window_start_throbber(gw);
		break;
	case GW_EVENT_STOP_THROBBER:
		gui_window_stop_throbber(gw);
		break;
	case GW_EVENT_PAGE_INFO_CHANGE:
		gui_window_report_page_info(gw);
		break;
	default:
		break;
	}
	return NSERROR_OK;
}

static struct gui_window_table window_table = {
	.create = gui_window_create,
	.destroy = gui_window_destroy,
	.invalidate = ntux_window_invalidate_area,
	.get_scroll = gui_window_get_scroll,
	.set_scroll = gui_window_set_scroll,
	.get_dimensions = gui_window_get_dimensions,
	.event = gui_window_event,
	.set_title = gui_window_set_title,
	.set_url = gui_window_set_url,
	.set_icon = gui_window_set_icon,
	.set_status = gui_window_set_status,
	.set_pointer = gui_window_set_pointer,
	.place_caret = gui_window_place_caret,
	.drag_start = gui_window_drag_start,
	.save_link = gui_window_save_link,
	.console_log = gui_window_console_log,
};

struct gui_window_table *ntux_window_table = &window_table;

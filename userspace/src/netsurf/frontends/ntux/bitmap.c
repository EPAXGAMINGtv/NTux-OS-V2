#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "netsurf/bitmap.h"

#include "ntux/bitmap.h"

static void *bitmap_create(int width, int height, enum gui_bitmap_flags flags)
{
	struct bitmap *ret = calloc(1, sizeof(*ret));
	if (ret == NULL)
		return NULL;

	ret->width = width;
	ret->height = height;
	ret->opaque = (flags & BITMAP_OPAQUE) == BITMAP_OPAQUE;
	ret->ptr = calloc(width, height * 4);
	if (ret->ptr == NULL) {
		free(ret);
		return NULL;
	}
	return ret;
}

static void bitmap_destroy(void *bitmap)
{
	struct bitmap *bmap = bitmap;
	free(bmap->ptr);
	free(bmap);
}

static void bitmap_set_opaque(void *bitmap, bool opaque)
{
	struct bitmap *bmap = bitmap;
	bmap->opaque = opaque;
}

static bool bitmap_get_opaque(void *bitmap)
{
	struct bitmap *bmap = bitmap;
	return bmap->opaque;
}

static unsigned char *bitmap_get_buffer(void *bitmap)
{
	struct bitmap *bmap = bitmap;
	return (unsigned char *)(bmap->ptr);
}

static size_t bitmap_get_rowstride(void *bitmap)
{
	struct bitmap *bmap = bitmap;
	return bmap->width * 4;
}

static void bitmap_modified(void *bitmap)
{
	(void)bitmap;
}

static int bitmap_get_width(void *bitmap)
{
	struct bitmap *bmap = bitmap;
	return bmap->width;
}

static int bitmap_get_height(void *bitmap)
{
	struct bitmap *bmap = bitmap;
	return bmap->height;
}

static nserror bitmap_render(struct bitmap *bitmap, struct hlcache_handle *content)
{
	(void)bitmap;
	(void)content;
	return NSERROR_OK;
}

static struct gui_bitmap_table bitmap_table = {
	.create = bitmap_create,
	.destroy = bitmap_destroy,
	.set_opaque = bitmap_set_opaque,
	.get_opaque = bitmap_get_opaque,
	.get_buffer = bitmap_get_buffer,
	.get_rowstride = bitmap_get_rowstride,
	.get_width = bitmap_get_width,
	.get_height = bitmap_get_height,
	.modified = bitmap_modified,
	.render = bitmap_render,
};

struct gui_bitmap_table *ntux_bitmap_table = &bitmap_table;

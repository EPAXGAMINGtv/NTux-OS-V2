#ifndef LIBRSVG_RSVG_H
#define LIBRSVG_RSVG_H

#include <stddef.h>
#include <stdint.h>

typedef struct { void *private; } RsvgHandle;
typedef struct { int width; int height; double em; double ex; } RsvgDimensionData;
typedef struct { void *private; } cairo_t;
typedef struct { void *private; } cairo_surface_t;

RsvgHandle *rsvg_handle_new_from_data(const uint8_t *data, size_t len);
void rsvg_handle_get_dimensions(RsvgHandle *handle, RsvgDimensionData *dim);
gboolean rsvg_handle_render_cairo(RsvgHandle *handle, cairo_t *cr);
void rsvg_handle_free(RsvgHandle *handle);

#endif

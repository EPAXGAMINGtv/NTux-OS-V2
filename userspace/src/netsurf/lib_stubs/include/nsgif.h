#ifndef _NSGIF_H
#define _NSGIF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct { void *private; } nsgif_t;
typedef struct { int width; int height; int frame_count; } nsgif_info_t;
typedef enum {
	NSGIF_OK = 0,
	NSGIF_NOMEM = 1,
	NSGIF_ERR_OOM = 1,
	NSGIF_BAD_DATA = 2,
} nsgif_error;

typedef uint32_t nsgif_bitmap_fmt_t;

#define NSGIF_INFINITE -1
#define NSGIF_BITMAP_FMT_RGBA 0
#define NSGIF_BITMAP_FMT_BGRA 1
#define NSGIF_BITMAP_FMT_ARGB 2
#define NSGIF_BITMAP_FMT_ABGR 3
#define NSGIF_BITMAP_FMT_R8G8B8A8 0
#define NSGIF_BITMAP_FMT_A 4
#define NSGIF_BITMAP_FMT_R 5
#define NSGIF_BITMAP_FMT_B 6

nsgif_error nsgif_create(nsgif_t **gif);
nsgif_error nsgif_analyse(nsgif_t *gif, size_t size, const uint8_t *data);
void nsgif_destroy(nsgif_t *gif);
nsgif_error nsgif_get_info(const nsgif_t *gif, nsgif_info_t *info);
nsgif_error nsgif_frame_prepare(nsgif_t *gif, int *area, unsigned int *delay, unsigned int *frame);

#endif

#ifndef _LIBNSBMP_H
#define _LIBNSBMP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct { void *private; } nsbmp_t;
typedef nsbmp_t bmp_image;
typedef struct { int width; int height; } nsbmp_colourmap_t;
typedef enum { NSBMP_OK = 0, NSBMP_NOMEM = 1, NSBMP_BAD_DATA = 2 } nsbmp_error;

#define BMP_OPAQUE (1 << 0)
#define BMP_CLEAR_MEMORY (1 << 1)

typedef struct {
	void *(*bitmap_create)(int width, int height, unsigned int state);
	void (*bitmap_destroy)(void *bitmap);
	unsigned char *(*bitmap_get_buffer)(void *bitmap);
} bmp_bitmap_callback_vt;

nsbmp_error nsbmp_create(nsbmp_t **bmp);
nsbmp_error nsbmp_analyse(nsbmp_t *bmp, size_t size, const uint8_t *data);
void nsbmp_destroy(nsbmp_t *bmp);
uint16_t nsbmp_get_width(nsbmp_t *bmp);
uint16_t nsbmp_get_height(nsbmp_t *bmp);
#endif

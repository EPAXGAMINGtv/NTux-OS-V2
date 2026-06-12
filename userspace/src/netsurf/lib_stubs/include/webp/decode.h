#ifndef _WEBP_DECODE_H
#define _WEBP_DECODE_H

#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t *rgba; int w; int h; } WebPRGBA;

typedef enum {
	VP8_STATUS_OK = 0,
	VP8_STATUS_OUT_OF_MEMORY = 1,
	VP8_STATUS_INVALID_PARAM = 2,
	VP8_STATUS_BITSTREAM_ERROR = 3,
	VP8_STATUS_UNSUPPORTED_FEATURE = 4,
	VP8_STATUS_SUSPENDED = 5,
	VP8_STATUS_USER_ABORT = 6,
	VP8_STATUS_NOT_ENOUGH_DATA = 7,
} VP8StatusCode;

typedef struct {
	int width;
	int height;
	int has_alpha;
	int has_animation;
	int format;
	VP8StatusCode status;
} WebPBitstreamFeatures;

VP8StatusCode WebPGetFeatures(const uint8_t *data, size_t size, WebPBitstreamFeatures *features);
int WebPGetInfo(const uint8_t *data, size_t size, int *w, int *h);
uint8_t *WebPDecodeRGBA(const uint8_t *data, size_t size, int *w, int *h);
void WebPFree(void *ptr);

#endif

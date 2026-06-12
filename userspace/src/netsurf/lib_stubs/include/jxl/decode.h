#ifndef JXL_DECODE_H
#define JXL_DECODE_H

#include <stddef.h>
#include <stdint.h>

typedef struct { void *private; } JxlDecoder;
typedef struct {
	uint32_t num_channels;
} JxlPixelFormat;

JxlDecoder *JxlDecoderCreate(void);
void JxlDecoderDestroy(JxlDecoder *dec);
void JxlDecoderSubscribeEvents(JxlDecoder *dec, int events);
void JxlDecoderSetInput(JxlDecoder *dec, const uint8_t *data, size_t size);
int JxlDecoderProcessInput(JxlDecoder *dec);
void JxlDecoderReleaseInput(JxlDecoder *dec);
int JxlDecoderGetColorAsEncodedProfile(JxlDecoder *dec, const JxlPixelFormat *fmt, int *out);
void JxlDecoderSetImageOutCallback(JxlDecoder *dec, const JxlPixelFormat *fmt, void *cb, void *user);
void JxlDecoderCloseInput(JxlDecoder *dec);

#define JXL_DEC_SUCCESS 0
#define JXL_DEC_NEED_MORE_INPUT 1
#define JXL_DEC_NEED_IMAGE_OUT_BUFFER 2
#define JXL_DEC_FULL_IMAGE 3
#define JXL_DEC_BASIC_INFO 4
#define JXL_DEC_COLOR_ENCODING 5

#endif

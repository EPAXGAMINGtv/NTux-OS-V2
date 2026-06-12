#ifndef _PNG_H
#define _PNG_H

#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>

typedef struct { void *private; } png_struct;
typedef struct { void *private; } png_info;
typedef png_struct *png_structp;
typedef png_info *png_infop;
typedef void (*png_error_ptr)(png_structp, const char*);
typedef uint8_t png_byte;
typedef png_byte *png_bytep;
typedef png_bytep *png_bytepp;
typedef const char *png_const_charp;

#define PNG_TRANSFORM_IDENTITY 0
#define PNG_COLOR_TYPE_RGBA 6
#define PNG_INTERLACE_NONE 0
#define PNG_COMPRESSION_TYPE_BASE 0
#define PNG_FILTER_TYPE_BASE 0

png_structp png_create_read_struct(int version, void *err, png_error_ptr err_fn, png_error_ptr warn_fn);
png_infop png_create_info_struct(png_structp png);
void png_init_io(png_structp png, void *fp);
void png_set_sig_bytes(png_structp png, int n);
void png_read_png(png_structp png, png_infop info, int transforms, void *params);
void png_get_IHDR(png_structp png, png_infop info, uint32_t *w, uint32_t *h, int *b, int *ct, int *it, int *c, int *f);
void png_destroy_read_struct(png_structp *png, png_infop *info, void *end);
jmp_buf *png_set_longjmp_fn(png_structp png, jmp_buf *jmp, size_t sz);
void png_set_expand(png_structp png);
void png_set_strip_16(png_structp png);
void png_set_gray_to_rgb(png_structp png);
void png_set_palette_to_rgb(png_structp png);
void png_set_tRNS_to_alpha(png_structp png);
void png_set_bgr(png_structp png);
void png_set_filler(png_structp png, uint32_t filler, int flags);
void png_set_gamma(png_structp png, double screen, double file);
void png_set_interlace_handling(png_structp png);
void png_set_quantize(png_structp png, void *palette, int p, int m, int d, int f);
void png_set_strip_alpha(png_structp png);
void png_set_swap_alpha(png_structp png);
void png_set_invert_alpha(png_structp png);
void png_set_user_limits(png_structp png, uint32_t w, uint32_t h);
png_bytepp png_get_rows(png_structp png, png_infop info);
#define PNG_LIBPNG_VER_STRING "1.6.34"

#define png_jmpbuf(png_ptr) (*png_set_longjmp_fn((png_ptr), (void*)0, sizeof(jmp_buf)))

#endif

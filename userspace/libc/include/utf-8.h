#ifndef UTF8_H
#define UTF8_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t text_decode_utf8(const char *s, int *advance);
int text_encode_utf8(uint32_t cp, char *out);
const char* text_next_utf8(const char *s);
const char* text_prev_utf8(const char *start, const char *s);

#ifdef __cplusplus
}
#endif

#endif
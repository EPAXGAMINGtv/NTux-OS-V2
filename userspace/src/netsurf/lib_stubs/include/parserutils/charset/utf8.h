#ifndef parserutils_charset_utf8_h
#define parserutils_charset_utf8_h

#include <stddef.h>
#include <stdint.h>

typedef enum {
	PARSERUTILS_OK = 0,
	PARSERUTILS_BADENCODING = 1,
	PARSERUTILS_NOMEM = 2,
	PARSERUTILS_NEEDDATA = 3,
	PARSERUTILS_EOF = 4,
} parserutils_error;

parserutils_error parserutils_charset_utf8_to_ucs4(const uint8_t *data, size_t len, uint32_t *ucs4, size_t *clen);
parserutils_error parserutils_charset_utf8_from_ucs4(uint32_t ucs4, uint8_t **in, size_t *len);
parserutils_error parserutils_charset_utf8_length(const uint8_t *data, size_t max, size_t *len);
parserutils_error parserutils_charset_utf8_char_byte_length(const uint8_t *data, size_t *len);
parserutils_error parserutils_charset_utf8_prev(const uint8_t *data, size_t off, uint32_t *prev);
parserutils_error parserutils_charset_utf8_next(const uint8_t *data, size_t len, size_t off, uint32_t *next);

#endif

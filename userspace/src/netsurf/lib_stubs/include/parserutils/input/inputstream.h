#ifndef parserutils_input_inputstream_h
#define parserutils_input_inputstream_h

#include <stddef.h>
#include <stdint.h>
#include "parserutils/charset/utf8.h"

typedef struct parserutils_inputstream parserutils_inputstream;
typedef parserutils_error (*parserutils_inputstream_encoding_hack)(const uint8_t *data, size_t len, uint16_t *mibenum, uint32_t *source);

parserutils_error parserutils_inputstream_create(const uint8_t *data, size_t len, parserutils_inputstream_encoding_hack hack, parserutils_inputstream **stream);
void parserutils_inputstream_destroy(parserutils_inputstream *stream);
parserutils_error parserutils_inputstream_advance(parserutils_inputstream *stream, size_t count);
parserutils_error parserutils_inputstream_append(parserutils_inputstream *stream, const uint8_t *data, size_t len);
parserutils_error parserutils_inputstream_peek(parserutils_inputstream *stream, size_t count, const uint8_t **data, size_t *datalen);

#endif

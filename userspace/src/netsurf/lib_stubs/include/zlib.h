#ifndef _ZLIB_H
#define _ZLIB_H

#include <stddef.h>
#include <stdint.h>

typedef struct z_stream_s {
    const uint8_t *next_in;
    unsigned int avail_in;
    unsigned long total_in;
    uint8_t *next_out;
    unsigned int avail_out;
    unsigned long total_out;
    const char *msg;
    void *state;
    void *zalloc;
    void *zfree;
    void *opaque;
    int data_type;
    unsigned long adler;
    unsigned long reserved;
} z_stream;

typedef z_stream *z_streamp;
typedef struct gzFile_s { int fd; } *gzFile;

#define MAX_WBITS 15

#define Z_OK 0
#define Z_STREAM_END 1
#define Z_NEED_DICT 2
#define Z_ERRNO (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR (-3)
#define Z_MEM_ERROR (-4)
#define Z_BUF_ERROR (-5)
#define Z_VERSION_ERROR (-6)

#define Z_NO_FLUSH 0
#define Z_SYNC_FLUSH 2
#define Z_FULL_FLUSH 3
#define Z_FINISH 4

#define Z_DEFLATED 8
#define Z_DEFAULT_COMPRESSION (-1)

#define Z_NULL 0

const char *zlibVersion(void);
int deflateInit(z_streamp strm, int level);
int deflate(z_streamp strm, int flush);
int deflateEnd(z_streamp strm);
int inflateInit(z_streamp strm);
int inflate(z_streamp strm, int flush);
int inflateEnd(z_streamp strm);
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);
int inflateInit2(z_streamp strm, int windowBits);

gzFile gzopen(const char *path, const char *mode);
const char *gzgets(gzFile file, char *buf, int len);
int gzclose(gzFile file);

#endif

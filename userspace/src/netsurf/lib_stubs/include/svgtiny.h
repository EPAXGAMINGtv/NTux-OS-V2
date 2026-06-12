#ifndef _SVGTINY_H
#define _SVGTINY_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t svgtiny_colour;

typedef struct svgtiny_shape {
	float *path;
	int path_length;
	svgtiny_colour stroke;
	svgtiny_colour fill;
	int stroke_width;
	char *text;
	float text_x;
	float text_y;
} svgtiny_shape;

typedef struct svgtiny_diagram {
	int width;
	int height;
	int shape_count;
	svgtiny_shape *shape;
} svgtiny_diagram;

typedef enum { svgtiny_OK = 0, svgtiny_OUT_OF_MEM = 1, svgtiny_LIBXML_ERROR = 2 } svgtiny_code;

#define svgtiny_TRANSPARENT 0xFFFFFFFF

#define svgtiny_RED(c) ((c) & 0xFF)
#define svgtiny_GREEN(c) (((c) >> 8) & 0xFF)
#define svgtiny_BLUE(c) (((c) >> 16) & 0xFF)

svgtiny_diagram *svgtiny_create(void);
svgtiny_code svgtiny_parse(svgtiny_diagram *diagram, const char *data, size_t size, const char *url, int width, int height);
void svgtiny_destroy(svgtiny_diagram *diagram);

#endif

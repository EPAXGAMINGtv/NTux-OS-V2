#include <string.h>

#include "utils/errors.h"
#include "netsurf/fetch.h"
#include "utils/nsurl.h"

static const char *ntux_fetch_filetype(const char *unix_path)
{
	const char *dot = strrchr(unix_path, '.');
	if (dot == NULL)
		return "application/octet-stream";
	if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0)
		return "text/html";
	if (strcasecmp(dot, ".css") == 0)
		return "text/css";
	if (strcasecmp(dot, ".js") == 0)
		return "text/javascript";
	if (strcasecmp(dot, ".png") == 0)
		return "image/png";
	if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcasecmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcasecmp(dot, ".bmp") == 0)
		return "image/bmp";
	if (strcasecmp(dot, ".svg") == 0)
		return "image/svg+xml";
	if (strcasecmp(dot, ".ico") == 0)
		return "image/x-icon";
	if (strcasecmp(dot, ".webp") == 0)
		return "image/webp";
	if (strcasecmp(dot, ".json") == 0)
		return "application/json";
	if (strcasecmp(dot, ".pdf") == 0)
		return "application/pdf";
	return "application/octet-stream";
}

static struct gui_fetch_table fetch_table = {
	.filetype = ntux_fetch_filetype,
};

struct gui_fetch_table *ntux_fetch_table = &fetch_table;

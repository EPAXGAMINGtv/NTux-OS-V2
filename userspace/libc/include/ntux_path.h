#ifndef NTUX_PATH_H
#define NTUX_PATH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ntux_resolve_path(const char *in, char *out, size_t cap);
const char *ntux_get_cwd(void);

#ifdef __cplusplus
}
#endif

#endif

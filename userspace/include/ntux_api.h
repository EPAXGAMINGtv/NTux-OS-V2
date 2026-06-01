#ifndef NTUX_API_H
#define NTUX_API_H

typedef struct {
    void (*print)(const char *text);
} ntux_host_api_t;

#endif

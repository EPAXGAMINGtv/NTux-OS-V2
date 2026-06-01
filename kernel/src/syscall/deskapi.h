#ifndef NTUX_DESKAPI_H
#define NTUX_DESKAPI_H

#include <stdint.h>

#define DESKAPI_RING_SIZE 128
#define DESKAPI_MAX_MSG 60000
#define DIALOG_MAX_TEXT 256

int deskapi_push(const char* buf, uint64_t len);
int deskapi_pop(char* out, uint64_t cap, uint64_t* out_len);
int deskapi_dialog_push(int tid, uint32_t code, const char* text);
int deskapi_dialog_pop(int tid, char* out, uint64_t cap, uint32_t* out_code);

#endif

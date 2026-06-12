#ifndef LIBROSPRITE_H
#define LIBROSPRITE_H

#include <stddef.h>
#include <stdint.h>

typedef struct { void *private; } rosprite_t;
typedef int rosprite_error;

#define ROSPRITE_OK 0
#define ROSPRITE_NOMEM 1
#define ROSPRITE_BAD_DATA 2

#endif

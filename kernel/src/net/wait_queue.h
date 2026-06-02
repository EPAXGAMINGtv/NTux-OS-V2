#ifndef WAIT_QUEUE_H
#define WAIT_QUEUE_H

#include <stdint.h>

typedef volatile int wait_queue_head_t;

static inline void wait_queue_init(wait_queue_head_t *wq) {
    *wq = 0;
}

static inline void wait_queue_wake_all(wait_queue_head_t *wq) {
    *wq = 1;
}

#endif

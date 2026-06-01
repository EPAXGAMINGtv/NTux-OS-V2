#include "ringbuffer.h"

static void rb_lock(ringbuffer_t* rb) {
    while (__atomic_test_and_set(&rb->lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
}

static void rb_unlock(ringbuffer_t* rb) {
    __atomic_clear(&rb->lock, __ATOMIC_RELEASE);
}

void rb_init(ringbuffer_t* rb) {
    rb->head = 0;
    rb->tail = 0;
    rb->lock = 0;
}

int rb_empty(ringbuffer_t* rb) {
    int empty;
    rb_lock(rb);
    empty = (rb->head == rb->tail);
    rb_unlock(rb);
    return empty;
}

int rb_put(ringbuffer_t* rb, char c) {
    rb_lock(rb);
    uint32_t next = (rb->head + 1) % RINGBUFFER_SIZE;
    if(next == rb->tail) {
        rb_unlock(rb);
        return 0;
    }
    rb->buffer[rb->head] = c;
    rb->head = next;
    rb_unlock(rb);
    return 1;
}

int rb_get(ringbuffer_t* rb, char* c) {
    rb_lock(rb);
    if(rb->head == rb->tail) {
        rb_unlock(rb);
        return 0;
    }
    *c = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % RINGBUFFER_SIZE;
    rb_unlock(rb);
    return 1;
}

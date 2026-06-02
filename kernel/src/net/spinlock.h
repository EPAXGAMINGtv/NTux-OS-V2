#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef volatile uint64_t spinlock_t;

#define SPINLOCK_INIT 0

static inline uint64_t spinlock_acquire_irqsave(spinlock_t *lock) {
    uint64_t flags;
    __asm__ volatile(
        "pushfq\n\t"
        "pop %0\n\t"
        "cli\n\t"
        : "=r"(flags)
        :
        : "memory"
    );
    while (__sync_lock_test_and_set(lock, 1)) {
        __asm__ volatile("pause");
    }
    return flags;
}

static inline void spinlock_release_irqrestore(spinlock_t *lock, uint64_t flags) {
    __sync_lock_release(lock);
    if (flags & 0x200) {
        __asm__ volatile("sti");
    }
}

#endif

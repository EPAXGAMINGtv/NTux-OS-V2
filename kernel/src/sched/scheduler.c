#include <stdint.h>
#include <stddef.h>

#include <sched/thread.h>
#include <arch/x86_64/gdt.h>
#include <drivers/framebuffer/kprint.h>

static uint64_t g_kernel_cr3 = 0;

/* ------------------ O(1) Ready Queue ------------------ */
static thread_t* rq_head = NULL;
static thread_t* rq_tail = NULL;
static int rq_count = 0;

void rq_enqueue(thread_t* t) {
    if (!t || t->rq_queued) return;
    t->rq_next = NULL;
    t->rq_prev = rq_tail;
    if (rq_tail)
        rq_tail->rq_next = t;
    else
        rq_head = t;
    rq_tail = t;
    t->rq_queued = 1;
    rq_count++;
}

thread_t* rq_dequeue(void) {
    thread_t* t = rq_head;
    if (!t) return NULL;
    rq_head = t->rq_next;
    if (rq_head)
        rq_head->rq_prev = NULL;
    else
        rq_tail = NULL;
    t->rq_next = NULL;
    t->rq_prev = NULL;
    t->rq_queued = 0;
    rq_count--;
    return t;
}

void rq_remove(thread_t* t) {
    if (!t || !t->rq_queued) return;
    if (t->rq_prev)
        t->rq_prev->rq_next = t->rq_next;
    else
        rq_head = t->rq_next;
    if (t->rq_next)
        t->rq_next->rq_prev = t->rq_prev;
    else
        rq_tail = t->rq_prev;
    t->rq_next = NULL;
    t->rq_prev = NULL;
    t->rq_queued = 0;
    rq_count--;
}

int rq_get_count(void) {
    return rq_count;
}

static inline uint64_t read_cr3(void) {
    uint64_t v = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(uint64_t v) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

/* ------------------ Core Scheduler ------------------
 * Enqueues current RUNNING thread, dequeues next READY thread,
 * prepares the new thread.
 * Called with thread_lock_global held.
 * After return, caller MUST release the lock.
 * If `out_switch_to` is set, caller must perform the context switch
 * (lock released first).
 */
static thread_t* schedule_core(thread_t** out_switch_to) {
    if (out_switch_to) *out_switch_to = NULL;

    thread_reap_terminated();

    int old_id = current_thread_id;
    thread_t* old = (old_id >= 0 && old_id < MAX_THREADS) ? thread_list[old_id] : NULL;

    if (old && old->state == THREAD_RUNNING) {
        old->state = THREAD_READY;
        rq_enqueue(old);
    }

    thread_t* next = rq_dequeue();
    if (!next) {
        /* old was either NULL, BLOCKED, or TERMINATED — nothing left to run */
        current_thread_id = -1;
        return NULL;
    }

    int next_id = (int)next->id;
    if (old_id == next_id) {
        next->state = THREAD_RUNNING;
        next->quantum = TIME_SLICE_DEFAULT;
        return NULL;
    }

    next->state = THREAD_RUNNING;
    if (next->quantum <= 0) next->quantum = TIME_SLICE_DEFAULT;
    current_thread_id = next_id;

    if (!g_kernel_cr3) g_kernel_cr3 = read_cr3();
    {
        uint64_t target_cr3 = next->cr3 ? next->cr3 : g_kernel_cr3;
        if (target_cr3) write_cr3(target_cr3);
    }

    if (next->stack) {
        uintptr_t rsp0 = (uintptr_t)next->stack + (uintptr_t)8192;
        rsp0 &= ~0xFull;
        gdt_set_kernel_stack(rsp0);
    }

    if (out_switch_to) *out_switch_to = next;
    return old;
}

/* Public API – acquires lock, schedules, releases lock */
void scheduler(void) {
    thread_lock_global();
    thread_t* old = NULL;
    thread_t* next = NULL;
    old = schedule_core(&next);
    if (next) {
        thread_unlock_global();
        if (old) {
            switch_context(&old->rsp, next->rsp);
        } else {
            uint64_t* dummy = NULL;
            switch_context(&dummy, next->rsp);
        }
    } else {
        thread_unlock_global();
    }
}

/* Called from timer IRQ – lock must be held by caller */
void scheduler_from_irq(void) {
    thread_t* old = NULL;
    thread_t* next = NULL;
    old = schedule_core(&next);
    if (next) {
        thread_unlock_global();
        if (old) {
            switch_context(&old->rsp, next->rsp);
        } else {
            uint64_t* dummy = NULL;
            switch_context(&dummy, next->rsp);
        }
    }
    /* If no switch: lock remains held, caller (timer_handler) will release */
}

void thread_yield(void) {
    for (;;) {
        scheduler();
        if (current_thread_id >= 0) break;
        __asm__ volatile("hlt");
    }
}

void thread_exit_current(void) {
    int tid = current_thread_id;
    thread_lock_global();
    if (tid >= 0 && tid < MAX_THREADS && thread_list[tid]) {
        rq_remove(thread_list[tid]);
        thread_list[tid]->state = THREAD_TERMINATED;
    }
    thread_unlock_global();
    for (;;) {
        scheduler();
        if (current_thread_id >= 0) break;
        __asm__ volatile("hlt");
    }
    for (;;) {
        __asm__ volatile("hlt");
    }
}

#include <stdint.h>
#include <stddef.h>
#include <lib/string.h>

#include <sched/thread.h>
#include <drivers/framebuffer/kprint.h>
#include <elf/module_loader.h>
#include <mm/kmalloc.h>

thread_t* thread_list[MAX_THREADS] = {0};
int current_thread_id = -1;
int next_thread_id = 0;
volatile uint32_t g_thread_blocked_count = 0;

static volatile int g_thread_lock = 0;

static inline void thread_lock_spin(void) {
    while (__atomic_test_and_set(&g_thread_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
}

static inline void thread_unlock_spin(void) {
    __atomic_clear(&g_thread_lock, __ATOMIC_RELEASE);
}

void thread_lock_global(void) {
    thread_lock_spin();
}

void thread_unlock_global(void) {
    thread_unlock_spin();
}

bool thread_try_lock_global(void) {
    return __atomic_test_and_set(&g_thread_lock, __ATOMIC_ACQUIRE) == 0;
}

enum { THREAD_STACK_SIZE = 8192 };

static void thread_init_common(thread_t* t, void (*entry)(void)) {
    memset(t, 0, sizeof(*t));
    t->id = (uint64_t)next_thread_id++;
    t->state = THREAD_READY;
    t->entry_point = entry;
    memset(t->name, 0, sizeof(t->name));
    strncpy(t->name, "kernel", sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';
    t->uid = 0;
    t->user_vstart = 0;
    t->user_vend = 0;
    t->cr3 = 0;
    t->wake_tick = 0;
    t->kill_pending = 0;
    t->cpu_ticks = 0;
    t->quantum = TIME_SLICE_DEFAULT;
    t->priority = 0;
    t->rq_next = NULL;
    t->rq_prev = NULL;
    t->rq_queued = 0;
    memset(t->fds, 0, sizeof(t->fds));

    t->stack = kmalloc(THREAD_STACK_SIZE);
    if (!t->stack) return;

    uint64_t *sp = (uint64_t*)((uint8_t*)t->stack + THREAD_STACK_SIZE);

    *(--sp) = (uint64_t)thread_exit_current;
    *(--sp) = (uint64_t)entry;
    *(--sp) = 0x202;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;

    t->rsp = sp;
}

void thread_init(void) {
    for (int i = 0; i < MAX_THREADS; i++)
        thread_list[i] = NULL;

    current_thread_id = -1;
    next_thread_id = 0;
    g_thread_blocked_count = 0;

    kprint_ok("Threading initialized.");
}

int thread_create(void (*entry)(void)) {
    thread_lock_spin();
    if (next_thread_id >= MAX_THREADS) {
        thread_unlock_spin();
        kprint_error("Max threads reached.");
        return -1;
    }

    thread_t *t = kmalloc(sizeof(thread_t));
    if (!t) {
        thread_unlock_spin();
        return -1;
    }

    thread_init_common(t, entry);
    if (!t->stack) {
        kfree(t);
        thread_unlock_spin();
        return -1;
    }

    int tid = (int)t->id;
    thread_list[tid] = t;
    rq_enqueue(t);
    thread_unlock_spin();
    kprintf("Created thread %d\n", tid);
    return tid;
}

int thread_set_name(int tid, const char* name) {
    if (tid < 0 || tid >= MAX_THREADS) return -1;
    thread_lock_global();
    thread_t* t = thread_list[tid];
    if (!t) {
        thread_unlock_global();
        return -1;
    }
    memset(t->name, 0, sizeof(t->name));
    if (!name) name = "";
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';
    thread_unlock_global();
    return 0;
}

void thread_reap_terminated(void) {
    for (int i = 0; i < MAX_THREADS; ++i) {
        thread_t* t = thread_list[i];
        if (!t) continue;
        if (t->state != THREAD_TERMINATED) continue;
        if (current_thread_id == i) continue;

        rq_remove(t);
        fd_close_all(t);
        if (t->cr3 && t->user_vend > t->user_vstart) {
            module_loader_free_user_space(t->cr3, t->user_vstart, t->user_vend);
        }
        if (t->stack) {
            kfree(t->stack);
            t->stack = NULL;
        }
        kfree(t);
        thread_list[i] = NULL;
    }
}

int thread_kill(int tid) {
    if (tid < 0 || tid >= MAX_THREADS || !thread_list[tid]) return -1;
    thread_lock_spin();
    if (thread_list[tid]->state == THREAD_BLOCKED && g_thread_blocked_count > 0) {
        g_thread_blocked_count--;
    }
    rq_remove(thread_list[tid]);
    thread_list[tid]->state = THREAD_TERMINATED;
    thread_unlock_spin();
    return 0;
}

uint32_t thread_get_current_uid(void) {
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS || !thread_list[tid]) return 0;
    return thread_list[tid]->uid;
}

int thread_set_current_uid(uint32_t uid) {
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS || !thread_list[tid]) return -1;
    thread_list[tid]->uid = uid;
    return 0;
}

int thread_get_current_user_range(uint64_t* out_start, uint64_t* out_end) {
    if (!out_start || !out_end) return -1;
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS || !thread_list[tid]) return -1;
    *out_start = thread_list[tid]->user_vstart;
    *out_end = thread_list[tid]->user_vend;
    return 0;
}

uint64_t thread_get_current_cr3(void) {
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS || !thread_list[tid]) return 0;
    return thread_list[tid]->cr3;
}

void thread_set_current_cr3(uint64_t cr3) {
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS || !thread_list[tid]) return;
    thread_list[tid]->cr3 = cr3;
}

int thread_get_current_tid(void) {
    int tid = current_thread_id;
    if (tid < 0 || tid >= MAX_THREADS || !thread_list[tid]) return -1;
    return tid;
}

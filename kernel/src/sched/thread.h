#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include <stdbool.h>

#include <fs/fd.h>

typedef enum {
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_BLOCKED,
    THREAD_TERMINATED
} thread_state_t;

#define TIME_SLICE_DEFAULT 8

typedef struct thread {
    uint64_t *stack;
    uint64_t *rsp;
    thread_state_t state;
    uint64_t id;
    char name[32];
    uint32_t uid;
    uint64_t user_vstart;
    uint64_t user_vend;
    uint64_t cr3;
    void (*entry_point)(void);
    uint64_t wake_tick;
    uint8_t kill_pending;
    uint64_t cpu_ticks;
    uint64_t user_mem_bytes;
    fd_entry_t fds[FD_MAX];

    /* Ready queue links */
    struct thread* rq_next;
    struct thread* rq_prev;
    /* Scheduling */
    int64_t quantum;
    int64_t priority;
    uint8_t rq_queued;
} thread_t;

#define MAX_THREADS 64
extern thread_t* thread_list[MAX_THREADS];
extern int current_thread_id;
extern int next_thread_id;
extern volatile uint32_t g_thread_blocked_count;

/* Ready queue operations */
void rq_enqueue(thread_t* t);
thread_t* rq_dequeue(void);
void rq_remove(thread_t* t);
int rq_get_count(void);

/* Thread lifecycle */
void thread_init(void);
int thread_create(void (*entry_point)(void));
void thread_exit_current(void);
int thread_kill(int tid);
int thread_set_name(int tid, const char* name);

/* Internal: reap terminated threads (called by scheduler) */
void thread_reap_terminated(void);

/* Scheduler */
void scheduler(void);
void scheduler_from_irq(void);
void thread_yield(void);

/* Current thread accessors */
uint32_t thread_get_current_uid(void);
int thread_set_current_uid(uint32_t uid);
int thread_get_current_user_range(uint64_t* out_start, uint64_t* out_end);
uint64_t thread_get_current_cr3(void);
void thread_set_current_cr3(uint64_t cr3);
int thread_get_current_tid(void);

/* Global thread lock */
void thread_lock_global(void);
void thread_unlock_global(void);
bool thread_try_lock_global(void);

/* Context switch (implemented in assembly) */
void switch_context(uint64_t **old_rsp, uint64_t *new_rsp);

#endif

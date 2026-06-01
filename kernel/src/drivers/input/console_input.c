#include <drivers/input/console_input.h>

#include <sched/thread.h>

static volatile int32_t g_console_input_owner_tid = -1;

static int owner_is_alive(int tid) {
    if (tid < 0 || tid >= MAX_THREADS) return 0;
    int alive = 0;
    thread_lock_global();
    thread_t* t = thread_list[tid];
    if (t && t->state != THREAD_TERMINATED) alive = 1;
    thread_unlock_global();
    return alive;
}

int console_input_owner_is_tid(int tid) {
    if (tid < 0) return 0;
    int32_t owner = __atomic_load_n(&g_console_input_owner_tid, __ATOMIC_ACQUIRE);
    return owner == (int32_t)tid;
}

int console_input_claim_or_is_current(int tid) {
    if (tid < 0) return 0;

    int32_t owner = __atomic_load_n(&g_console_input_owner_tid, __ATOMIC_ACQUIRE);
    if (owner == (int32_t)tid) return 1;

    if (owner >= 0 && !owner_is_alive(owner)) {
        int32_t expected = owner;
        (void)__atomic_compare_exchange_n(
            &g_console_input_owner_tid, &expected, -1,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE
        );
        owner = __atomic_load_n(&g_console_input_owner_tid, __ATOMIC_ACQUIRE);
    }

    int32_t expected = -1;
    if (__atomic_compare_exchange_n(
            &g_console_input_owner_tid, &expected, (int32_t)tid,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return 1;
    }
    return 0;
}

void console_input_release_if_tid(int tid) {
    if (tid < 0) return;
    int32_t expected = (int32_t)tid;
    (void)__atomic_compare_exchange_n(
        &g_console_input_owner_tid, &expected, -1,
        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE
    );
}

int console_input_is_free(void) {
    int32_t owner = __atomic_load_n(&g_console_input_owner_tid, __ATOMIC_ACQUIRE);
    if (owner < 0) return 1;
    if (!owner_is_alive(owner)) {
        int32_t expected = owner;
        (void)__atomic_compare_exchange_n(
            &g_console_input_owner_tid, &expected, -1,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE
        );
        owner = __atomic_load_n(&g_console_input_owner_tid, __ATOMIC_ACQUIRE);
        return owner < 0;
    }
    return 0;
}

void console_input_force_owner(int tid) {
    if (tid < 0) return;
    __atomic_store_n(&g_console_input_owner_tid, (int32_t)tid, __ATOMIC_RELEASE);
}

/*
 * timer.c — PIT-based system timer and tick management.
 *
 * The PIT (Intel 8253/8254) is a legacy timer chip on x86.
 * Channel 0 generates periodic IRQ 0 at the configured frequency.
 *
 * We also implement "PIT stretching" — reading back the current
 * counter value to get sub-tick precision for get_tick_count().
 * This prevents drift when the IRQ handler is delayed.
 */

#include <interrupt/irq.h>
#include <drivers/framebuffer/kprint.h>
#include <interrupt/pic.h>
#include <arch/x86_64/io.h>
#include <sched/thread.h>
#include "timer.h"

/* PIT base frequency: 1.193182 MHz */
#define PIT_BASE_HZ 1193182u

/* Global tick count — incremented by the timer IRQ handler */
static volatile uint64_t tick_count = 0;

static const uint32_t g_timer_hz = TIMER_HZ;
static uint16_t g_pit_divisor = 0;

/*
 * PIT "stretching" state: we read the current counter on each
 * tick() call and accumulate cycles to derive a precise tick count.
 */
static uint16_t pit_last = 0;
static uint64_t pit_cycles = 0;
static uint64_t pit_ticks = 0;
static uint8_t pit_inited = 0;

/* Count of timer IRQs where the current thread was idle */
static volatile uint64_t idle_tick_count = 0;

/*
 * Read the PIT channel 0 current count.
 * We latch the counter by writing 0x00 to the command register (0x43),
 * then read low byte + high byte from port 0x40.
 */
static uint16_t pit_read_count(void) {
    outb(0x43, 0x00);
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40);
    return (uint16_t)((uint16_t)hi << 8) | (uint16_t)lo;
}

/*
 * Update the stretched tick count based on the PIT current counter.
 * This gives us sub-tick precision and prevents drift.
 */
static void pit_update(void) {
    if (g_pit_divisor == 0) return;
    if (!pit_inited) {
        pit_last = pit_read_count();
        pit_inited = 1;
        return;
    }

    uint16_t cur = pit_read_count();
    uint16_t dec;
    if (pit_last >= cur) {
        dec = (uint16_t)(pit_last - cur);
    } else {
        /* Counter wrapped around */
        dec = (uint16_t)(pit_last + (uint16_t)(g_pit_divisor - cur));
    }
    pit_last = cur;

    pit_cycles += dec;
    uint64_t new_ticks = pit_cycles / (uint64_t)g_pit_divisor;
    if (new_ticks > pit_ticks) {
        pit_ticks = new_ticks;
        tick_count = pit_ticks;
    }
}

/*
 * Configure PIT channel 0 in mode 2 (rate generator).
 * The divisor is computed from the desired HZ value.
 */
void timer_pit_config_c(void) {
    uint32_t hz = g_timer_hz ? g_timer_hz : 1000u;
    uint32_t divisor = (PIT_BASE_HZ + (hz / 2u)) / hz;
    if (divisor < 1u) divisor = 1u;
    if (divisor > 65535u) divisor = 65535u;
    g_pit_divisor = (uint16_t)divisor;

    /* PIT command: channel 0, lobyte/hibyte, mode 2, binary */
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)(g_pit_divisor & 0xFFu));
    outb(0x40, (uint8_t)((g_pit_divisor >> 8) & 0xFFu));
}

/*
 * Timer IRQ handler (called on each PIT tick).
 *
 * Responsibilities:
 *   1. Update the stretched tick count.
 *   2. Wake any threads whose wake_tick has expired.
 *   3. Decrement the current thread's quantum; if zero, reschedule.
 *   4. If no thread is running, count idle ticks.
 */
void timer_handler(void) {
    pit_update();
    uint64_t now = pit_ticks;

    /* Wake sleeping threads whose wake_tick has passed */
    if (wake_list_head != NULL && thread_try_lock_global()) {
        thread_t* t = wake_list_head;
        while (t) {
            thread_t* next = t->wl_next;
            if (t->state == THREAD_BLOCKED && t->wake_tick > 0 && t->wake_tick <= now) {
                t->state = THREAD_READY;
                t->wake_tick = 0;
                rq_enqueue(t);
                wake_list_remove(t);
                if (g_thread_blocked_count > 0) g_thread_blocked_count--;
            }
            t = next;
        }
        thread_unlock_global();
    }

    /* Preemptive multitasking: decrement quantum */
    int tid = current_thread_id;
    if (tid >= 0 && tid < MAX_THREADS) {
        thread_t* t = thread_list[tid];
        if (t && t->state == THREAD_RUNNING) {
            t->cpu_ticks++;
            t->quantum--;
            if (t->quantum <= 0) {
                if (thread_try_lock_global()) {
                    if (current_thread_id == tid && thread_list[tid] == t) {
                        scheduler_from_irq();
                    }
                    thread_unlock_global();
                }
            }
            return;
        }
    }
    idle_tick_count++;
}

void init_timer() {
    timer_pit_config();
    irq_register_handler(0, timer_handler);
    pic_clear_mask(0);
    kprint_ok("Timer initialized");
}

/*
 * Busy-wait loops (inefficient, only for early boot or short delays).
 * These spin on the pause instruction rather than HLT because they
 * are sometimes used with interrupts disabled.
 */
void sleep(uint32_t ticks) {
    uint64_t start_tick = get_tick_count();
    while (get_tick_count() - start_tick < ticks) {
        __asm__ volatile("pause");
    }
}

void sleep_s(uint32_t seconds) {
    sleep(seconds * g_timer_hz);
}

void sleep_m(uint32_t minutes) {
    sleep(minutes * 60 * g_timer_hz);
}

uint64_t get_tick_count() {
    pit_update();
    return pit_ticks;
}

uint64_t get_idle_tick_count(void) {
    return idle_tick_count;
}

void timer_add_idle_ticks(uint64_t ticks) {
    idle_tick_count += ticks;
}

uint32_t timer_get_hz(void) {
    return g_timer_hz;
}

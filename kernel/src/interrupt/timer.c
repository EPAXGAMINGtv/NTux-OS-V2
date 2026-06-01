#include <interrupt/irq.h>
#include <drivers/framebuffer/kprint.h>
#include <interrupt/pic.h>
#include <arch/x86_64/io.h>
#include <sched/thread.h>
#include "timer.h"

#define PIT_BASE_HZ 1193182u

static volatile uint64_t tick_count = 0;
static const uint32_t g_timer_hz = TIMER_HZ;
static uint16_t g_pit_divisor = 0;

static uint16_t pit_last = 0;
static uint64_t pit_cycles = 0;
static uint64_t pit_ticks = 0;
static uint8_t pit_inited = 0;
static volatile uint64_t idle_tick_count = 0;

static uint16_t pit_read_count(void) {
    outb(0x43, 0x00);
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40);
    return (uint16_t)((uint16_t)hi << 8) | (uint16_t)lo;
}

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

void timer_pit_config_c(void) {
    uint32_t hz = g_timer_hz ? g_timer_hz : 1000u;
    uint32_t divisor = (PIT_BASE_HZ + (hz / 2u)) / hz;
    if (divisor < 1u) divisor = 1u;
    if (divisor > 65535u) divisor = 65535u;
    g_pit_divisor = (uint16_t)divisor;

    outb(0x43, 0x34);
    outb(0x40, (uint8_t)(g_pit_divisor & 0xFFu));
    outb(0x40, (uint8_t)((g_pit_divisor >> 8) & 0xFFu));
}

void timer_handler(void) {
    pit_update();
    uint64_t now = pit_ticks;
    if (g_thread_blocked_count > 0 && thread_try_lock_global()) {
        for (int i = 0; i < MAX_THREADS; ++i) {
            thread_t* t = thread_list[i];
            if (!t) continue;
            if (t->state == THREAD_BLOCKED && t->wake_tick > 0 && t->wake_tick <= now) {
                t->state = THREAD_READY;
                t->wake_tick = 0;
                rq_enqueue(t);
                if (g_thread_blocked_count > 0) g_thread_blocked_count--;
            }
        }
        thread_unlock_global();
    }
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
        }
    }
}
void init_timer() {
    timer_pit_config();
    irq_register_handler(0, timer_handler);
    pic_clear_mask(0);
    kprint_ok("Timer initialized");
}

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

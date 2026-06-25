#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/*
 * Timer system: PIT-based tick generation.
 *
 * The PIT (Programmable Interval Timer) channels 0 fires at TIMER_HZ
 * interrupts per second. Each interrupt increments a global tick_count.
 *
 * The timer serves two purposes:
 *   1. Preemptive multitasking (quantum expiry → scheduler)
 *   2. Sleep/wake timing (WAIT_TICKS syscall)
 */

/* Desired timer frequency in Hz. 250 Hz = 4ms per tick. */
#define TIMER_HZ 250u

/* PIT configuration (called once during boot) */
void timer_pit_config(void);

/* Initialize the timer system: configure PIT + register IRQ handler */
void init_timer(void);

/* Busy-wait for a given number of ticks (not efficient, used for early boot) */
void sleep(uint32_t ticks);
void sleep_s(uint32_t seconds);
void sleep_m(uint32_t minutes);

/* Get current global tick count since boot */
uint64_t get_tick_count(void);
uint64_t get_idle_tick_count(void);
void timer_add_idle_ticks(uint64_t ticks);
uint32_t timer_get_hz(void);

#endif

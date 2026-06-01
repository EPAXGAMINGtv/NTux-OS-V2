#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define TIMER_HZ 250u

void timer_pit_config(void);
void init_timer(void);
void sleep(uint32_t ticks);
void sleep_s(uint32_t seconds);
void sleep_m(uint32_t minutes);
uint64_t get_tick_count();
uint64_t get_idle_tick_count(void);
void timer_add_idle_ticks(uint64_t ticks);
uint32_t timer_get_hz(void);


#endif 

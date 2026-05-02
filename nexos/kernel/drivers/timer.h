/* NexOS — kernel/drivers/timer.h | PIT timer driver | MIT License */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void     timer_init(uint32_t freq_hz);
void     timer_sleep_ms(uint32_t ms);
uint64_t timer_get_ticks(void);
uint64_t timer_get_uptime_seconds(void);

#endif

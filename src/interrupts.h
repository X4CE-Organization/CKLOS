#ifndef CKLOS_INTERRUPTS_H
#define CKLOS_INTERRUPTS_H

#include "types.h"

void interrupts_init(void);
void timer_tick(void);
uint32_t timer_ticks(void);
uint32_t timer_uptime_ms(void);
void timer_sleep_ms(uint32_t milliseconds);

#endif

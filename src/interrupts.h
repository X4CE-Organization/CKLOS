#ifndef CKLOS_INTERRUPTS_H
#define CKLOS_INTERRUPTS_H

#include "types.h"

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame;

void interrupts_init(void);
void timer_tick(void);
uint32_t timer_ticks(void);
uint32_t timer_uptime_ms(void);
void timer_sleep_ms(uint32_t milliseconds);

#endif

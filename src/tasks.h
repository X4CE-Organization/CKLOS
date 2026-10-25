#ifndef CKLOS_TASKS_H
#define CKLOS_TASKS_H

#include "types.h"

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_STOPPED
} task_state;

typedef struct {
    char name[16];
    task_state state;
    uint32_t saved_esp;
    void *stack;
    uint32_t stack_size;
    uint32_t yields;
    volatile uint32_t work;
    void (*entry)(void);
} kernel_task;

void tasks_init(void);
int tasks_spawn(const char *kind);
int tasks_stop(uint32_t id);
void task_yield(void);
void tasks_preempt(void);
uint32_t task_count(void);
uint32_t task_switches(void);
const kernel_task *task_at(uint32_t index);
const char *task_state_name(task_state state);

#endif

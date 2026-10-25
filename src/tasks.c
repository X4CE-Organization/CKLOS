#include "io.h"
#include "memory.h"
#include "string.h"
#include "tasks.h"

#define TASK_LIMIT 8
#define TASK_STACK_SIZE 16384

extern void task_switch(uint32_t *old_esp, uint32_t new_esp);

static kernel_task tasks[TASK_LIMIT];
static uint32_t total_tasks;
static uint32_t current_task;
static uint32_t switch_count;

static void task_bootstrap(void);

static void idle_entry(void) {
    while (1) {
        __asm__ volatile("sti; hlt");
        task_yield();
    }
}

static void worker_entry(void) {
    while (1) {
        tasks[current_task].work++;
        for (uint32_t spin = 0; spin < 20000; spin++) {
            cpu_pause();
        }
        task_yield();
    }
}

static bool add_task(const char *name, void (*entry)(void)) {
    uint32_t slot = TASK_LIMIT;
    for (uint32_t index = 1; index < total_tasks; index++) {
        if (tasks[index].state == TASK_STOPPED && !tasks[index].stack) {
            slot = index;
            break;
        }
    }
    if (slot == TASK_LIMIT) {
        if (total_tasks >= TASK_LIMIT) {
            return false;
        }
        slot = total_tasks;
    }
    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!stack) {
        return false;
    }
    uint32_t top = ((uint32_t)stack + TASK_STACK_SIZE) & ~0x0FU;
    uint32_t *esp = (uint32_t *)(top - 20);
    esp[0] = 0;
    esp[1] = 0;
    esp[2] = 0;
    esp[3] = 0;
    esp[4] = (uint32_t)task_bootstrap;

    memset(&tasks[slot], 0, sizeof(tasks[slot]));
    strcpy(tasks[slot].name, name);
    tasks[slot].state = TASK_READY;
    tasks[slot].saved_esp = (uint32_t)esp;
    tasks[slot].stack = stack;
    tasks[slot].stack_size = TASK_STACK_SIZE;
    tasks[slot].entry = entry;
    if (slot == total_tasks) {
        total_tasks++;
    }
    return true;
}

void tasks_init(void) {
    memset(tasks, 0, sizeof(tasks));
    total_tasks = 0;
    current_task = 0;
    switch_count = 0;

    strcpy(tasks[0].name, "shell");
    tasks[0].state = TASK_RUNNING;
    tasks[0].stack_size = 0x90000;
    total_tasks = 1;
    add_task("system-idle", idle_entry);
}

void task_yield(void) {
    if (total_tasks < 2) {
        return;
    }
    kernel_task *old_task = &tasks[current_task];
    uint32_t start = current_task;
    uint32_t next = current_task;
    for (uint32_t offset = 1; offset <= total_tasks; offset++) {
        uint32_t candidate = (start + offset) % total_tasks;
        if (tasks[candidate].state == TASK_READY) {
            next = candidate;
            break;
        }
    }
    if (next == current_task) {
        if (old_task->state == TASK_RUNNING) {
            return;
        }
    }

    if (old_task->state == TASK_RUNNING) {
        old_task->state = TASK_READY;
    }
    old_task->yields++;
    tasks[next].state = TASK_RUNNING;
    current_task = next;
    switch_count++;
    task_switch(&old_task->saved_esp, tasks[next].saved_esp);
    __asm__ volatile("sti");
}

void tasks_preempt(void) {
    if (total_tasks < 2) {
        return;
    }
    kernel_task *old_task = &tasks[current_task];
    uint32_t next = current_task;
    for (uint32_t offset = 1; offset < total_tasks; offset++) {
        uint32_t candidate = (current_task + offset) % total_tasks;
        if (tasks[candidate].state == TASK_READY) {
            next = candidate;
            break;
        }
    }
    if (next == current_task) {
        return;
    }

    if (old_task->state == TASK_RUNNING) {
        old_task->state = TASK_READY;
    }
    tasks[next].state = TASK_RUNNING;
    current_task = next;
    switch_count++;
    task_switch(&old_task->saved_esp, tasks[next].saved_esp);
}

int tasks_spawn(const char *kind) {
    if (strcmp(kind, "idle") == 0) {
        char name[16];
        strcpy(name, "idle-");
        uint32_t count = total_tasks;
        name[5] = (char)('0' + count % 10);
        name[6] = '\0';
        return add_task(name, idle_entry) ? 0 : -1;
    }
    if (strcmp(kind, "worker") == 0) {
        char name[16];
        strcpy(name, "worker-");
        uint32_t count = total_tasks;
        name[7] = (char)('0' + count % 10);
        name[8] = '\0';
        return add_task(name, worker_entry) ? 0 : -1;
    }
    return -2;
}

int tasks_stop(uint32_t id) {
    if (id == 0 || id >= total_tasks) {
        return -1;
    }
    kernel_task *task = &tasks[id];
    if (task->state == TASK_STOPPED && !task->stack) {
        return -2;
    }
    if (id == current_task) {
        return -3;
    }
    task->state = TASK_STOPPED;
    if (task->stack) {
        kfree(task->stack);
        task->stack = 0;
        task->stack_size = 0;
        task->saved_esp = 0;
        task->entry = 0;
    }
    return 0;
}

static void task_bootstrap(void) {
    __asm__ volatile("sti");
    kernel_task *task = &tasks[current_task];
    task->entry();
    task->state = TASK_STOPPED;
    while (1) {
        task_yield();
    }
}

uint32_t task_count(void) {
    return total_tasks;
}

uint32_t task_switches(void) {
    return switch_count;
}

const kernel_task *task_at(uint32_t index) {
    return index < total_tasks ? &tasks[index] : 0;
}

const char *task_state_name(task_state state) {
    if (state == TASK_RUNNING) {
        return "running";
    }
    if (state == TASK_READY) {
        return "ready";
    }
    return "stopped";
}

#ifndef CKLOS_PROCESS_H
#define CKLOS_PROCESS_H

#include "interrupts.h"
#include "types.h"

void process_init(void);
int process_run_elf(const char *path);
int process_run_elf_args(const char *path, int argc, const char *const *argv);
interrupt_frame *process_syscall(interrupt_frame *frame);
interrupt_frame *process_user_exception(interrupt_frame *frame);
interrupt_frame *process_timer(interrupt_frame *frame);
uint32_t process_count(void);
uint32_t process_current_pid(void);
bool process_user_copy(void *destination, const void *user_pointer, uint32_t size);
bool process_user_read_string(char *destination, const char *user_pointer,
                              uint32_t capacity);

#endif

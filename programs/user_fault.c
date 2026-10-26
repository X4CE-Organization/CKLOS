#include "user_syscall.h"

__attribute__((section(".text.start")))
void _start(void) {
    user_write(1, "fault test: touching supervisor memory\n", 39);
    volatile unsigned long *kernel_pointer =
        (volatile unsigned long *)0x1000;
    *kernel_pointer = 0xDEADBEEF;
    user_write(1, "fault test: isolation failed\n", 29);
    user_exit(2);
}

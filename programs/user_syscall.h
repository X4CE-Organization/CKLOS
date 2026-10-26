#ifndef CKLOS_USER_SYSCALL_H
#define CKLOS_USER_SYSCALL_H

static inline long user_syscall1(long number, long first) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(first)
                     : "rcx", "r11", "memory");
    return result;
}

static inline long user_syscall3(long number, long first, long second, long third) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(first), "S"(second), "d"(third)
                     : "rcx", "r11", "memory");
    return result;
}

static inline long user_write(int fd, const void *buffer, unsigned long length) {
    return user_syscall3(1, fd, (long)buffer, (long)length);
}

static inline long user_getpid(void) {
    return user_syscall1(39, 0);
}

static inline long user_yield(void) {
    return user_syscall1(24, 0);
}

static inline long user_fork(void) {
    return user_syscall1(57, 0);
}

static inline long user_wait4(long pid, int *status) {
    return user_syscall3(61, pid, (long)status, 0);
}

static __attribute__((noreturn)) void user_exit(int status) {
    user_syscall1(60, status);
    for (;;) {
        __asm__ volatile("pause");
    }
}

#endif

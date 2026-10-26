#include "user_syscall.h"

static long user_execve(const char *path) {
    return user_syscall3(59, (long)path, 0, 0);
}

__attribute__((section(".text.start")))
void _start(void) {
    long child = user_fork();
    if (child == 0) {
        user_write(1, "exec child replacing image\n", 27);
        long result = user_execve("/bin/USERHELLO.ELF");
        user_write(1, "execve failed\n", 14);
        user_exit((int)(-result));
    }
    int status = -1;
    long waited = user_wait4(child, &status);
    if (waited == child && status == 0) {
        user_write(1, "execve and wait completed\n", 26);
        user_exit(0);
    }
    user_write(1, "execve test failed\n", 19);
    user_exit(1);
}

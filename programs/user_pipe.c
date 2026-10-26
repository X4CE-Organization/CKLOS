#include "user_syscall.h"

static long user_pipe(int fds[2]) {
    return user_syscall1(22, (long)fds);
}

__attribute__((section(".text.start")))
void _start(void) {
    int fds[2] = { -1, -1 };
    if (user_pipe(fds) != 0) {
        user_write(1, "pipe failed\n", 12);
        user_exit(1);
    }
    long child = user_fork();
    if (child == 0) {
        user_write((unsigned long)fds[1], "pipe across fork\n", 17);
        user_exit(0);
    }
    int status = -1;
    user_wait4(child, &status);
    char buffer[32];
    long count = user_syscall3(0, fds[0], (long)buffer, sizeof(buffer));
    if (count > 0) {
        user_write(1, "parent read: ", 13);
        user_write(1, buffer, (unsigned long)count);
        user_exit(0);
    }
    user_write(1, "pipe read failed\n", 17);
    user_exit(2);
}

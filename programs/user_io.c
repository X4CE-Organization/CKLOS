#include "user_syscall.h"

static long user_open(const char *path, int flags) {
    return user_syscall3(2, (long)path, flags, 0);
}

static long user_close(int fd) {
    return user_syscall1(3, fd);
}

static long user_read(int fd, void *buffer, unsigned long length) {
    return user_syscall3(0, fd, (long)buffer, (long)length);
}

__attribute__((section(".text.start")))
void _start(void) {
    const char path[] = "/home/cklos/user-io.txt";
    const char message[] = "CKLOS user file I/O\n";
    long fd = user_open(path, 0x241); /* O_WRONLY | O_CREAT | O_TRUNC */
    if (fd < 3) {
        user_write(1, "open for write failed\n", 22);
        user_exit(1);
    }
    user_write(fd, message, sizeof(message) - 1);
    user_close((int)fd);

    fd = user_open(path, 0);
    if (fd < 3) {
        user_write(1, "open for read failed\n", 21);
        user_exit(2);
    }
    char buffer[64];
    long count = user_read(fd, buffer, sizeof(buffer));
    user_close((int)fd);
    if (count <= 0) {
        user_write(1, "read failed\n", 12);
        user_exit(3);
    }
    user_write(1, "read back: ", 11);
    user_write(1, buffer, (unsigned long)count);
    user_exit(0);
}

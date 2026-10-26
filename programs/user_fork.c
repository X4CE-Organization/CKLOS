#include "user_syscall.h"

static void print_number(long value) {
    char digits[24];
    int count = 0;
    if (value < 0) {
        user_write(1, "-", 1);
        value = -value;
    }
    if (!value) digits[count++] = '0';
    while (value > 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    char reversed[24];
    for (int index = 0; index < count; index++) {
        reversed[index] = digits[count - index - 1];
    }
    user_write(1, reversed, (unsigned long)count);
}

__attribute__((section(".text.start")))
void _start(void) {
    long child = user_fork();
    if (child == 0) {
        user_write(1, "child: fork returned 0\n", 23);
        user_exit(7);
    }
    if (child < 0) {
        user_write(1, "fork failed\n", 12);
        user_exit(1);
    }
    int status = -1;
    long waited = user_wait4(child, &status);
    user_write(1, "parent: waited pid=", 18);
    print_number(waited);
    user_write(1, " status=", 8);
    print_number(status);
    user_write(1, "\n", 1);
    user_exit(0);
}

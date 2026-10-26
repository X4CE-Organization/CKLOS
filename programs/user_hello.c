#include "user_syscall.h"

static unsigned long text_length(const char *text) {
    unsigned long length = 0;
    while (text[length]) length++;
    return length;
}

static void print(const char *text) {
    user_write(1, text, text_length(text));
}

__attribute__((section(".text.start")))
void _start(void) {
    print("CKLOS ring-3 ELF64 user program is running.\n");
    print("PID: ");
    char digits[16];
    long pid = user_getpid();
    int count = 0;
    if (!pid) {
        digits[count++] = '0';
    }
    while (pid > 0) {
        digits[count++] = (char)('0' + pid % 10);
        pid /= 10;
    }
    char output[17];
    for (int index = 0; index < count; index++) {
        output[index] = digits[count - index - 1];
    }
    output[count++] = '\n';
    user_write(1, output, (unsigned long)count);
    user_yield();
    print("Cooperative user-mode system call round trip complete.\n");
    user_exit(0);
}

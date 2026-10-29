#include "user_syscall.h"

static unsigned long text_length(const char *text) {
    unsigned long length = 0;
    while (text[length]) length++;
    return length;
}

static void print(const char *text) {
    user_write(1, text, text_length(text));
}

static void print_count(const char *prefix, unsigned long value) {
    char buffer[48];
    unsigned long length = text_length(prefix);
    for (unsigned long index = 0; index < length; index++) {
        buffer[index] = prefix[index];
    }
    char digits[24];
    unsigned long count = 0;
    if (!value) digits[count++] = '0';
    while (value) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count) buffer[length++] = digits[--count];
    buffer[length++] = '\n';
    user_write(1, buffer, length);
}

static long read_exact(long fd, void *buffer, unsigned long length) {
    unsigned char *bytes = (unsigned char *)buffer;
    unsigned long total = 0;
    for (unsigned long attempts = 0; attempts < 100000 && total < length; attempts++) {
        long result = user_read_fd(fd, bytes + total, length - total);
        if (result > 0) {
            total += (unsigned long)result;
        } else if (result == 0) {
            return 0;
        } else {
            user_yield();
        }
    }
    return total == length ? (long)total : -1;
}

static int starts_with(const char *text, const char *prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

__attribute__((used)) static void agent_main(unsigned long *initial_stack) {
    unsigned long argc = *initial_stack++;
    char **environment = (char **)(initial_stack + argc + 1);
    char *socket_path = 0;
    while (*environment) {
        if (starts_with(*environment, "SSH_AUTH_SOCK=")) {
            socket_path = *environment + 14;
            print(*environment);
            print("\n");
            break;
        }
        environment++;
    }
    if (!socket_path) {
        print("AGENT_USER: SSH_AUTH_SOCK is missing.\n");
        user_exit(1);
    }

    unsigned char address[110];
    for (unsigned long index = 0; index < sizeof(address); index++) {
        address[index] = 0;
    }
    address[0] = 1;
    unsigned long path_length = text_length(socket_path);
    for (unsigned long index = 0; index <= path_length; index++) {
        address[index + 2] = (unsigned char)socket_path[index];
    }
    long fd = user_socket(1, 1, 0);
    print("AGENT_USER: socket created.\n");
    if (fd < 0 || user_connect(fd, address, path_length + 3) != 0) {
        print("AGENT_USER: connect failed.\n");
        user_exit(2);
    }
    print("AGENT_USER: connected.\n");
    for (unsigned long attempts = 0; attempts < 100000; attempts++) {
        if (user_poll_fd(fd) > 0) break;
        user_yield();
    }

    const unsigned char request[5] = {0, 0, 0, 1, 11};
    print("AGENT_USER: sending request.\n");
    long written = -1;
    for (unsigned long attempts = 0; attempts < 100000; attempts++) {
        written = user_write(fd, request, sizeof(request));
        if (written == (long)sizeof(request)) break;
        user_yield();
    }
    if (written != (long)sizeof(request)) {
        print("AGENT_USER: request failed.\n");
        user_exit(3);
    }
    unsigned char length_bytes[4];
    if (read_exact(fd, length_bytes, sizeof(length_bytes)) != 4) {
        print("AGENT_USER: response length failed.\n");
        user_exit(4);
    }
    unsigned long response_length = (unsigned long)length_bytes[0] << 24
                                  | (unsigned long)length_bytes[1] << 16
                                  | (unsigned long)length_bytes[2] << 8
                                  | length_bytes[3];
    unsigned char response[5];
    unsigned long wanted = response_length < sizeof(response)
                         ? response_length : sizeof(response);
    if (wanted < 5 || read_exact(fd, response, wanted) != (long)wanted
        || response[0] != 12) {
        print("AGENT_USER: identity response failed.\n");
        user_exit(5);
    }
    unsigned long identities = (unsigned long)response[1] << 24
                             | (unsigned long)response[2] << 16
                             | (unsigned long)response[3] << 8
                             | response[4];
    print_count("AGENT_USER: identities=", identities);
    user_exit(0);
}

__attribute__((naked, section(".text.start")))
void _start(void) {
    __asm__ volatile(
        "movq %rsp, %rdi\n"
        "andq $-16, %rsp\n"
        "call agent_main\n"
        "movl $127, %edi\n"
        "int $0x80\n"
        "1: pause\n"
        "jmp 1b\n"
    );
}

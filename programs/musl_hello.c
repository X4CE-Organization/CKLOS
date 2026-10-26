#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    write(1, "musl: entered main\n", 19);
    char *buffer = malloc(64);
    if (!buffer) {
        printf("musl: malloc failed errno=%d\n", errno);
        return 1;
    }
    strcpy(buffer, "CKLOS musl static libc is running.\n");
    fputs(buffer, stdout);
    free(buffer);
    printf("pid=%d\n", (int)getpid());
    return 0;
}

#include "io.h"
#include "serial.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_write_char(char character) {
    if (character == '\n') {
        serial_write_char('\r');
    }
    while (!(inb(COM1 + 5) & 0x20)) {
        cpu_pause();
    }
    outb(COM1, (uint8_t)character);
}

void serial_write(const char *text) {
    while (*text) {
        serial_write_char(*text++);
    }
}

bool serial_can_read(void) {
    return (inb(COM1 + 5) & 0x01) != 0;
}

char serial_read_char(void) {
    return (char)inb(COM1);
}

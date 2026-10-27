#include "console.h"
#include "gui.h"
#include "serial.h"
#include "ssh.h"
#include "string.h"

void console_init(void) {
    serial_init();
    gui_init();
    gui_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
}

void console_clear(void) {
    gui_clear();
}

void console_set_color(uint8_t foreground, uint8_t background) {
    gui_set_color(foreground, background);
}

uint8_t console_color(void) {
    return gui_color();
}

void console_putc(char character) {
    if (character == '\b') {
        serial_write("\b \b");
    } else {
        serial_write_char(character);
    }

    gui_putc(character);
    ssh_console_putc(character);
}

void console_write(const char *text) {
    while (*text) {
        console_putc(*text++);
    }
}

void console_print_uint(uint32_t value) {
    char buffer[33];
    uint_to_string(value, buffer, 10);
    console_write(buffer);
}

void console_print_int(int32_t value) {
    if (value < 0) {
        console_putc('-');
        console_print_uint((uint32_t)(-(value + 1)) + 1);
    } else {
        console_print_uint((uint32_t)value);
    }
}

void console_print_hex(uint32_t value) {
    char buffer[33];
    console_write("0x");
    uint_to_string(value, buffer, 16);
    console_write(buffer);
}

static void print_padded_hex(uint32_t value, uint32_t width) {
    char buffer[33];
    uint_to_string(value, buffer, 16);
    uint32_t length = (uint32_t)strlen(buffer);
    while (length < width) {
        console_putc('0');
        length++;
    }
    console_write(buffer);
}

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

void console_printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    while (*format) {
        if (*format != '%') {
            console_putc(*format++);
            continue;
        }
        format++;
        if (format[0] == '0' && format[1] == '8' && format[2] == 'x') {
            print_padded_hex(va_arg(arguments, uint32_t), 8);
            format += 3;
            continue;
        }
        switch (*format++) {
            case 's': console_write(va_arg(arguments, const char *)); break;
            case 'c': console_putc((char)va_arg(arguments, int)); break;
            case 'd': console_print_int(va_arg(arguments, int32_t)); break;
            case 'u': console_print_uint(va_arg(arguments, uint32_t)); break;
            case 'x': console_print_hex(va_arg(arguments, uint32_t)); break;
            case '%': console_putc('%'); break;
            case '\0': format--; break;
            default: console_putc('?'); break;
        }
    }
    va_end(arguments);
}

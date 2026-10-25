#ifndef CKLOS_CONSOLE_H
#define CKLOS_CONSOLE_H

#include "types.h"

enum {
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GRAY = 7,
    COLOR_DARK_GRAY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_YELLOW = 14,
    COLOR_WHITE = 15
};

void console_init(void);
void console_clear(void);
void console_set_color(uint8_t foreground, uint8_t background);
uint8_t console_color(void);
void console_putc(char character);
void console_write(const char *text);
void console_print_int(int32_t value);
void console_print_uint(uint32_t value);
void console_print_hex(uint32_t value);
void console_printf(const char *format, ...);

#endif

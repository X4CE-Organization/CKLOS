#ifndef CKLOS_KEYBOARD_H
#define CKLOS_KEYBOARD_H

#include "types.h"

enum {
    KEY_UP = 0x100,
    KEY_DOWN = 0x101,
    KEY_LEFT = 0x102,
    KEY_RIGHT = 0x103,
    KEY_HOME = 0x104,
    KEY_END = 0x105,
    KEY_PAGE_UP = 0x106,
    KEY_PAGE_DOWN = 0x107,
    KEY_ESCAPE = 0x108,
    KEY_MOUSE_CLICK = 0x10A
};

uint16_t input_getchar(void);
void input_set_device_polling(bool enabled);
void input_enable_interrupts(void);
void keyboard_irq_handler(void);

#endif

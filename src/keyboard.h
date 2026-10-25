#ifndef CKLOS_KEYBOARD_H
#define CKLOS_KEYBOARD_H

#include "types.h"

enum {
    KEY_UP = 0x100,
    KEY_DOWN = 0x101,
    KEY_LEFT = 0x102,
    KEY_RIGHT = 0x103,
    KEY_ESCAPE = 0x104
};

uint16_t input_getchar(void);
void keyboard_irq_handler(void);

#endif

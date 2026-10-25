#ifndef CKLOS_GUI_H
#define CKLOS_GUI_H

#include "types.h"

void gui_init(void);
void gui_clear(void);
void gui_set_color(uint8_t foreground, uint8_t background);
uint8_t gui_color(void);
void gui_putc(char character);

#endif

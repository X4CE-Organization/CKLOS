#ifndef CKLOS_MOUSE_H
#define CKLOS_MOUSE_H

#include "types.h"

void mouse_init(void);
void mouse_irq_handler(void);
int32_t mouse_x(void);
int32_t mouse_y(void);
bool mouse_left_down(void);
bool mouse_take_click(void);
bool mouse_available(void);

#endif

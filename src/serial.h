#ifndef CKLOS_SERIAL_H
#define CKLOS_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_write_char(char character);
void serial_write(const char *text);
bool serial_can_read(void);
char serial_read_char(void);

#endif

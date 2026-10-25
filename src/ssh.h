#ifndef CKLOS_SSH_H
#define CKLOS_SSH_H

#include "types.h"

bool ssh_init(void);
bool ssh_console_active(void);
void ssh_console_putc(char character);
bool ssh_console_read(char *character);
const char *ssh_status(void);

#endif

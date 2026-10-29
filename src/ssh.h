#ifndef CKLOS_SSH_H
#define CKLOS_SSH_H

#include "types.h"

bool ssh_init(void);
void ssh_poll(void);
bool ssh_console_active(void);
void ssh_console_putc(char character);
bool ssh_console_read(char *character);
const char *ssh_status(void);
bool ssh_agent_available(void);
const char *ssh_agent_socket_path(void);
bool ssh_agent_connect(void);
bool ssh_agent_connect_async(void);
bool ssh_agent_connected(void);
bool ssh_agent_readable(void);
int32_t ssh_agent_read_nonblock(uint8_t *data, uint32_t capacity);
int32_t ssh_agent_read(uint8_t *data, uint32_t capacity, uint32_t timeout_ms);
bool ssh_agent_write(const uint8_t *data, uint32_t length);
void ssh_agent_close(void);
bool ssh_agent_request_identities(uint32_t *count);

#endif

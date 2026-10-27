#ifndef CKLOS_RESOLVER_H
#define CKLOS_RESOLVER_H

#include "types.h"

bool resolver_parse_ipv4(const char *text, uint32_t *address);
bool resolver_lookup_ipv4(const char *host, uint32_t *address, uint32_t timeout_ms);
bool resolver_lookup_ipv6(const char *host, uint8_t address[16], uint32_t timeout_ms);

#endif

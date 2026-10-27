#ifndef CKLOS_TLS_H
#define CKLOS_TLS_H

#include "types.h"

bool tls_https_request(const char *host, uint32_t address, uint16_t port,
                       const uint8_t *request, uint32_t request_size,
                       uint8_t *response, uint32_t capacity, uint32_t *size,
                       bool verbose);

#endif

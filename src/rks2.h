#ifndef CKLOS_RKS2_H
#define CKLOS_RKS2_H

#include "types.h"

#define RKS2_HEADER_SIZE 64U
#define RKS2_MAX_PAYLOAD (8U * 1024U * 1024U)

bool rks2_wrap_file(const char *path);
bool rks2_extract(const char *path, uint8_t **payload, uint32_t *size);
bool rks2_is_container(const uint8_t *data, uint32_t size);

#endif

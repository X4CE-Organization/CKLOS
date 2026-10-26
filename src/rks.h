#ifndef CKLOS_RKS_H
#define CKLOS_RKS_H

#include "types.h"

void rks_init(void);
int rks_run(const char *name);
uint32_t rks_installed_count(void);

#endif

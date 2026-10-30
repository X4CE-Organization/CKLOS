#ifndef CKLOS_EDITOR_H
#define CKLOS_EDITOR_H

#include "types.h"

bool editor_open(const char *path, bool create);
bool editor_last_saved(char *path, uint32_t capacity, uint32_t *size);

#endif

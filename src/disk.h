#ifndef CKLOS_DISK_H
#define CKLOS_DISK_H

#include "types.h"

bool disk_init(void);
bool disk_available(void);
bool disk_save_filesystem(void);
bool disk_load_filesystem(void);
bool disk_format(void);
uint32_t disk_saved_file_count(void);
uint32_t disk_capacity_bytes(void);

#endif

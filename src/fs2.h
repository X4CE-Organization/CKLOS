#ifndef CKLOS_FS2_H
#define CKLOS_FS2_H

#include "types.h"

typedef void (*fs2_list_callback)(const char *name, uint32_t size, uint8_t type);

#define FS2_TYPE_FILE 1
#define FS2_TYPE_DIRECTORY 2

bool fs2_init(void);
void fs2_sync(void);
bool fs2_available(void);
int fs2_set_cwd(const char *path);
const char *fs2_get_cwd(void);
int fs2_list(const char *path, fs2_list_callback callback);
int fs2_read(const char *path, void *buffer, uint32_t capacity, uint32_t *size);
int fs2_read_at(const char *path, uint32_t offset, void *buffer,
                uint32_t capacity, uint32_t *size);
int fs2_write(const char *path, const void *data, uint32_t size, bool append);
int fs2_write_at(const char *path, uint32_t offset, const void *data,
                 uint32_t size);
int fs2_truncate(const char *path, uint32_t size);
int fs2_mkdir(const char *path);
int fs2_remove(const char *path);
int fs2_rename(const char *old_path, const char *new_path);
int fs2_stat(const char *path, uint32_t *size, uint8_t *type);
int fs2_migrate_legacy(uint32_t *imported, uint32_t *skipped);
int fs2_fsck(uint32_t *used_blocks, uint32_t *total_blocks);
int fs2_copy_to_vfs(const char *path, const char *name, bool read_only);
uint32_t fs2_capacity_bytes(void);
uint32_t fs2_free_bytes(void);

#endif

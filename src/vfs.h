#ifndef CKLOS_VFS_H
#define CKLOS_VFS_H

#include "types.h"

#define VFS_MAX_FILES 24
#define VFS_NAME_CAPACITY 13
#define VFS_CONTENT_CAPACITY 512

typedef struct {
    char name[VFS_NAME_CAPACITY];
    char content[VFS_CONTENT_CAPACITY + 1];
    uint32_t size;
    bool read_only;
    bool used;
} virtual_file;

enum {
    VFS_OK = 0,
    VFS_NOT_FOUND = -1,
    VFS_INVALID_NAME = -2,
    VFS_NO_SPACE = -3,
    VFS_READ_ONLY = -4,
    VFS_NAME_EXISTS = -5,
    VFS_CONTENT_TOO_LARGE = -6
};

void vfs_init(void);
uint32_t vfs_count(void);
const virtual_file *vfs_at(uint32_t index);
const virtual_file *vfs_find(const char *name);
int vfs_write(const char *name, const char *text, bool append);
int vfs_add(const char *name, const void *content, uint32_t size, bool read_only);
int vfs_touch(const char *name);
int vfs_remove(const char *name);
int vfs_rename(const char *old_name, const char *new_name);
uint32_t vfs_used_bytes(void);
uint32_t vfs_capacity_bytes(void);

#endif

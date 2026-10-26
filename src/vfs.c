#include "string.h"
#include "vfs.h"

static virtual_file files[VFS_MAX_FILES];

static bool normalize_name(const char *input, char *output) {
    uint32_t length = 0;
    while (*input) {
        char character = *input++;
        if (character >= 'a' && character <= 'z') {
            character = (char)(character - 'a' + 'A');
        }
        bool valid = (character >= 'A' && character <= 'Z')
                  || (character >= '0' && character <= '9')
                  || character == '.' || character == '_' || character == '-';
        if (!valid || length + 1 >= VFS_NAME_CAPACITY) {
            return false;
        }
        output[length++] = character;
    }
    output[length] = '\0';
    return length > 0;
}

static virtual_file *find_mutable(const char *name) {
    char normalized[VFS_NAME_CAPACITY];
    if (!normalize_name(name, normalized)) {
        return 0;
    }
    for (uint32_t index = 0; index < VFS_MAX_FILES; index++) {
        if (files[index].used && strcmp(files[index].name, normalized) == 0) {
            return &files[index];
        }
    }
    return 0;
}

static int add_file(const char *name, const char *content, bool read_only) {
    char normalized[VFS_NAME_CAPACITY];
    if (!normalize_name(name, normalized)) {
        return VFS_INVALID_NAME;
    }
    if (find_mutable(normalized)) {
        return VFS_NAME_EXISTS;
    }
    for (uint32_t index = 0; index < VFS_MAX_FILES; index++) {
        if (!files[index].used) {
            uint32_t length = (uint32_t)strlen(content);
            if (length > VFS_CONTENT_CAPACITY) {
                return VFS_CONTENT_TOO_LARGE;
            }
            files[index].used = true;
            files[index].read_only = read_only;
            files[index].size = length;
            strcpy(files[index].name, normalized);
            strcpy(files[index].content, content);
            return VFS_OK;
        }
    }
    return VFS_NO_SPACE;
}

int vfs_add(const char *name, const void *content, uint32_t size, bool read_only) {
    char normalized[VFS_NAME_CAPACITY];
    if (!normalize_name(name, normalized)) {
        return VFS_INVALID_NAME;
    }
    if (find_mutable(normalized)) {
        return VFS_NAME_EXISTS;
    }
    if (size > VFS_CONTENT_CAPACITY) {
        return VFS_CONTENT_TOO_LARGE;
    }
    for (uint32_t index = 0; index < VFS_MAX_FILES; index++) {
        if (!files[index].used) {
            files[index].used = true;
            files[index].read_only = read_only;
            files[index].size = size;
            strcpy(files[index].name, normalized);
            memcpy(files[index].content, content, size);
            files[index].content[size] = '\0';
            return VFS_OK;
        }
    }
    return VFS_NO_SPACE;
}

void vfs_init(void) {
    memset(files, 0, sizeof(files));
    add_file(
        "MOTD.TXT",
        "Welcome to CKLOS.\n"
        "A compact command-line operating system built from scratch.\n"
        "Type HELP to see every available command.\n",
        true
    );
    add_file(
        "ABOUT.TXT",
        "CKLOS is a freestanding 64-bit x86 operating system.\n"
        "It uses its own bootloader, IDT, timer, terminal driver and shell.\n"
        "There is no Linux kernel or userspace underneath.\n",
        true
    );
    add_file(
        "LICENSE.TXT",
        "CKLOS local source distribution.\n"
        "Released under the MIT License. See LICENSE in the source tree.\n",
        true
    );
    add_file(
        "DEMO.CKL",
        "# CKLOS script engine demonstration\n"
        "echo CKLOS script engine is active.\n"
        "uptime\n"
        "df\n"
        "tasks\n",
        true
    );
}

uint32_t vfs_count(void) {
    uint32_t count = 0;
    for (uint32_t index = 0; index < VFS_MAX_FILES; index++) {
        if (files[index].used) {
            count++;
        }
    }
    return count;
}

const virtual_file *vfs_at(uint32_t index) {
    uint32_t current = 0;
    for (uint32_t slot = 0; slot < VFS_MAX_FILES; slot++) {
        if (!files[slot].used) {
            continue;
        }
        if (current++ == index) {
            return &files[slot];
        }
    }
    return 0;
}

const virtual_file *vfs_find(const char *name) {
    return find_mutable(name);
}

int vfs_write(const char *name, const char *text, bool append) {
    char normalized[VFS_NAME_CAPACITY];
    if (!normalize_name(name, normalized)) {
        return VFS_INVALID_NAME;
    }

    bool created = false;
    virtual_file *file = find_mutable(normalized);
    if (file && file->read_only) {
        return VFS_READ_ONLY;
    }
    if (!file) {
        for (uint32_t index = 0; index < VFS_MAX_FILES; index++) {
            if (!files[index].used) {
                files[index].used = true;
                files[index].read_only = false;
                files[index].size = 0;
                files[index].content[0] = '\0';
                strcpy(files[index].name, normalized);
                file = &files[index];
                created = true;
                break;
            }
        }
        if (!file) {
            return VFS_NO_SPACE;
        }
    }

    uint32_t old_size = append ? file->size : 0;
    uint32_t text_size = (uint32_t)strlen(text);
    if (old_size + text_size > VFS_CONTENT_CAPACITY) {
        if (created) {
            memset(file, 0, sizeof(*file));
        }
        return VFS_CONTENT_TOO_LARGE;
    }
    memcpy(file->content + old_size, text, text_size);
    file->size = old_size + text_size;
    file->content[file->size] = '\0';
    return VFS_OK;
}

int vfs_touch(const char *name) {
    char normalized[VFS_NAME_CAPACITY];
    if (!normalize_name(name, normalized)) {
        return VFS_INVALID_NAME;
    }
    if (find_mutable(normalized)) {
        return VFS_NAME_EXISTS;
    }
    return vfs_write(normalized, "", false);
}

int vfs_remove(const char *name) {
    virtual_file *file = find_mutable(name);
    if (!file) {
        return VFS_NOT_FOUND;
    }
    if (file->read_only) {
        return VFS_READ_ONLY;
    }
    memset(file, 0, sizeof(*file));
    return VFS_OK;
}

int vfs_rename(const char *old_name, const char *new_name) {
    virtual_file *file = find_mutable(old_name);
    if (!file) {
        return VFS_NOT_FOUND;
    }
    if (file->read_only) {
        return VFS_READ_ONLY;
    }
    char normalized[VFS_NAME_CAPACITY];
    if (!normalize_name(new_name, normalized)) {
        return VFS_INVALID_NAME;
    }
    virtual_file *existing = find_mutable(normalized);
    if (existing && existing != file) {
        return VFS_NAME_EXISTS;
    }
    strcpy(file->name, normalized);
    return VFS_OK;
}

uint32_t vfs_used_bytes(void) {
    uint32_t total = 0;
    for (uint32_t index = 0; index < VFS_MAX_FILES; index++) {
        if (files[index].used) {
            total += files[index].size;
        }
    }
    return total;
}

uint32_t vfs_capacity_bytes(void) {
    return VFS_MAX_FILES * VFS_CONTENT_CAPACITY;
}

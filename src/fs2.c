#include "fs2.h"
#include "io.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"

#include "lfs.h"

#define ATA_DATA 0x1F0
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_STATUS 0x1F7
#define FS2_BLOCK_SIZE 4096U
#define FS2_RESERVED_BLOCK 1U
#define FS2_TOTAL_BLOCKS 32768U
#define FS2_LFS_BLOCKS (FS2_TOTAL_BLOCKS - FS2_RESERVED_BLOCK)
#define FS2_NAME_MAX 255U
#define FS2_PATH_MAX 4096U
#define FS2_SUPER_CHECKSUM_OFFSET 24U

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t lfs_offset_blocks;
    uint32_t checksum;
} __attribute__((packed)) cklosfs2_super;

static lfs_t filesystem;
static bool mounted;
static char current_directory[FS2_PATH_MAX] = "/";
static uint8_t read_cache[FS2_BLOCK_SIZE];
static uint8_t program_cache[FS2_BLOCK_SIZE];
static uint8_t lookahead_cache[64];
static uint8_t file_cache[FS2_BLOCK_SIZE];
static uint8_t fsck_bitmap[FS2_LFS_BLOCKS / 8];

static bool wait_not_busy(void);
static bool wait_drq(void);

static bool disk_read_sector(uint32_t lba, void *buffer) {
    if (!wait_not_busy()) return false;
    outb(ATA_DRIVE, 0xF0 | (uint8_t)(lba >> 24));
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_STATUS, 0x20);
    if (!wait_drq()) return false;
    insw(ATA_DATA, buffer, 256);
    return true;
}

static bool wait_not_busy(void) {
    for (uint32_t timeout = 0; timeout < 1000000; timeout++) {
        uint8_t status = inb(ATA_STATUS);
        if (status == 0xFF) {
            return false;
        }
        if (!(status & 0x80)) {
            return true;
        }
    }
    return false;
}

static bool wait_drq(void) {
    for (uint32_t timeout = 0; timeout < 1000000; timeout++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & 0x21) {
            return false;
        }
        if (!(status & 0x80) && (status & 0x08)) {
            return true;
        }
    }
    return false;
}

static uint32_t disk_lba(lfs_block_t block) {
    return ((uint32_t)block + FS2_RESERVED_BLOCK) * 8U;
}

static uint32_t super_checksum(const uint8_t *bytes) {
    uint32_t checksum = 0;
    for (uint32_t index = 0; index < FS2_SUPER_CHECKSUM_OFFSET; index++) {
        checksum = checksum * 33U + bytes[index];
    }
    return checksum;
}

static int disk_read(const struct lfs_config *config, lfs_block_t block,
                     lfs_off_t offset, void *buffer, lfs_size_t size) {
    (void)config;
    uint8_t *output = (uint8_t *)buffer;
    while (size) {
        uint32_t lba = disk_lba(block) + (uint32_t)(offset / 512U);
        uint32_t in_sector = (uint32_t)offset % 512U;
        uint32_t available = 512U - in_sector;
        uint32_t count = size < available ? size : available;
        if (in_sector == 0 && count == 512U) {
            if (!wait_not_busy()) return LFS_ERR_IO;
            outb(ATA_DRIVE, 0xF0 | (uint8_t)(lba >> 24));
            outb(ATA_SECTOR_COUNT, 1);
            outb(ATA_LBA_LOW, (uint8_t)lba);
            outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
            outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
            outb(ATA_STATUS, 0x20);
            if (!wait_drq()) return LFS_ERR_IO;
            insw(ATA_DATA, output, 256);
        } else {
            uint8_t sector[512];
            if (!wait_not_busy()) return LFS_ERR_IO;
            outb(ATA_DRIVE, 0xF0 | (uint8_t)(lba >> 24));
            outb(ATA_SECTOR_COUNT, 1);
            outb(ATA_LBA_LOW, (uint8_t)lba);
            outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
            outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
            outb(ATA_STATUS, 0x20);
            if (!wait_drq()) return LFS_ERR_IO;
            insw(ATA_DATA, sector, 256);
            memcpy(output, sector + in_sector, count);
        }
        output += count;
        offset += count;
        size -= count;
    }
    return LFS_ERR_OK;
}

static int disk_program(const struct lfs_config *config, lfs_block_t block,
                        lfs_off_t offset, const void *buffer, lfs_size_t size) {
    (void)config;
    const uint8_t *input = (const uint8_t *)buffer;
    while (size) {
        uint32_t lba = disk_lba(block) + (uint32_t)(offset / 512U);
        uint32_t in_sector = (uint32_t)offset % 512U;
        uint32_t available = 512U - in_sector;
        uint32_t count = size < available ? size : available;
        uint8_t sector[512];
        if (!wait_not_busy()) return LFS_ERR_IO;
        outb(ATA_DRIVE, 0xF0 | (uint8_t)(lba >> 24));
        outb(ATA_SECTOR_COUNT, 1);
        outb(ATA_LBA_LOW, (uint8_t)lba);
        outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
        outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
        if (count == 512U) {
            outb(ATA_STATUS, 0x30);
            if (!wait_drq()) return LFS_ERR_IO;
            outsw(ATA_DATA, input, 256);
        } else {
            outb(ATA_STATUS, 0x20);
            if (!wait_drq()) return LFS_ERR_IO;
            insw(ATA_DATA, sector, 256);
            memcpy(sector + in_sector, input, count);
            if (!wait_not_busy()) return LFS_ERR_IO;
            outb(ATA_DRIVE, 0xF0 | (uint8_t)(lba >> 24));
            outb(ATA_SECTOR_COUNT, 1);
            outb(ATA_LBA_LOW, (uint8_t)lba);
            outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
            outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
            outb(ATA_STATUS, 0x30);
            if (!wait_drq()) return LFS_ERR_IO;
            outsw(ATA_DATA, sector, 256);
        }
        input += count;
        offset += count;
        size -= count;
    }
    return LFS_ERR_OK;
}

static int disk_erase(const struct lfs_config *config, lfs_block_t block) {
    (void)config;
    uint8_t zeros[512] = {0};
    for (uint32_t sector = 0; sector < 8; sector++) {
        if (disk_program(config, block, sector * 512U, zeros, 512U) != LFS_ERR_OK) {
            return LFS_ERR_IO;
        }
    }
    return LFS_ERR_OK;
}

static int disk_sync(const struct lfs_config *config) {
    (void)config;
    outb(ATA_STATUS, 0xE7);
    return wait_not_busy() ? LFS_ERR_OK : LFS_ERR_IO;
}

static const struct lfs_config configuration = {
    .read = disk_read,
    .prog = disk_program,
    .erase = disk_erase,
    .sync = disk_sync,
    .read_size = 16,
    .prog_size = 16,
    .block_size = FS2_BLOCK_SIZE,
    .block_count = FS2_LFS_BLOCKS,
    .cache_size = FS2_BLOCK_SIZE,
    .lookahead_size = sizeof(lookahead_cache),
    .read_buffer = read_cache,
    .prog_buffer = program_cache,
    .lookahead_buffer = lookahead_cache,
    .block_cycles = 500,
};

static void path_normalize(const char *input, char *output) {
    char combined[FS2_PATH_MAX * 2];
    if (input[0] == '/') {
        strcpy(combined, input);
    } else if (strcmp(current_directory, "/") == 0) {
        combined[0] = '/';
        strcpy(combined + 1, input);
    } else {
        strcpy(combined, current_directory);
        size_t length = strlen(combined);
        combined[length++] = '/';
        strcpy(combined + length, input);
    }

    char *parts[128];
    uint32_t count = 0;
    char *cursor = combined;
    while (*cursor) {
        while (*cursor == '/') cursor++;
        if (!*cursor) break;
        char *start = cursor;
        while (*cursor && *cursor != '/') cursor++;
        if (*cursor) *cursor++ = '\0';
        if (strcmp(start, ".") == 0) continue;
        if (strcmp(start, "..") == 0) {
            if (count) count--;
            continue;
        }
        if (count < 128) parts[count++] = start;
    }
    size_t length = 0;
    output[length++] = '/';
    for (uint32_t index = 0; index < count; index++) {
        size_t part_length = strlen(parts[index]);
        if (length + part_length + 2 >= FS2_PATH_MAX) break;
        memcpy(output + length, parts[index], part_length);
        length += part_length;
        if (index + 1 < count) output[length++] = '/';
    }
    output[length] = '\0';
}

bool fs2_init(void) {
    uint8_t sector[512];
    memset(sector, 0, sizeof(sector));
    cklosfs2_super *super = (cklosfs2_super *)sector;
    if (!disk_read_sector(0, sector)
        || strncmp(super->magic, "CKLOSFS2", 8) != 0
        || super->version != 2 || super->block_size != FS2_BLOCK_SIZE
        || super->total_blocks != FS2_TOTAL_BLOCKS
        || super->lfs_offset_blocks != FS2_RESERVED_BLOCK
        || super->checksum != super_checksum(sector)) {
        mounted = false;
        return false;
    }
    mounted = lfs_mount(&filesystem, &configuration) == LFS_ERR_OK;
    if (mounted) {
        struct lfs_info home;
        if (lfs_stat(&filesystem, "/home/cklos", &home) == LFS_ERR_OK
            && home.type == LFS_TYPE_DIR) {
            strcpy(current_directory, "/home/cklos");
        } else {
            strcpy(current_directory, "/");
        }
    }
    return mounted;
}

void fs2_sync(void) {
    if (mounted) lfs_unmount(&filesystem);
    if (mounted) lfs_mount(&filesystem, &configuration);
}

bool fs2_available(void) {
    return mounted;
}

int fs2_set_cwd(const char *path) {
    struct lfs_info info;
    char resolved[FS2_PATH_MAX];
    path_normalize(path, resolved);
    if (lfs_stat(&filesystem, resolved, &info) != LFS_ERR_OK || info.type != LFS_TYPE_DIR) {
        return -1;
    }
    strcpy(current_directory, resolved);
    return 0;
}

const char *fs2_get_cwd(void) {
    return current_directory;
}

int fs2_list(const char *path, fs2_list_callback callback) {
    char resolved[FS2_PATH_MAX];
    path_normalize(path && *path ? path : ".", resolved);
    lfs_dir_t directory;
    if (lfs_dir_open(&filesystem, &directory, resolved) != LFS_ERR_OK) return -1;
    struct lfs_info info;
    int result = 0;
    while (lfs_dir_read(&filesystem, &directory, &info) > 0) {
        if (strcmp(info.name, ".") && strcmp(info.name, "..")) {
            callback(info.name, info.size, info.type);
        }
    }
    lfs_dir_close(&filesystem, &directory);
    return result;
}

int fs2_read(const char *path, void *buffer, uint32_t capacity, uint32_t *size) {
    char resolved[FS2_PATH_MAX];
    path_normalize(path, resolved);
    struct lfs_file_config file_config = {.buffer = file_cache};
    struct lfs_file file;
    if (lfs_file_opencfg(&filesystem, &file, resolved, LFS_O_RDONLY, &file_config) != LFS_ERR_OK) {
        return -1;
    }
    lfs_ssize_t count = lfs_file_read(&filesystem, &file, buffer, capacity);
    lfs_file_close(&filesystem, &file);
    if (count < 0) return -2;
    if (size) *size = (uint32_t)count;
    return 0;
}

int fs2_read_at(const char *path, uint32_t offset, void *buffer,
                uint32_t capacity, uint32_t *size) {
    char resolved[FS2_PATH_MAX];
    path_normalize(path, resolved);
    struct lfs_file_config file_config = {.buffer = file_cache};
    struct lfs_file file;
    if (lfs_file_opencfg(&filesystem, &file, resolved, LFS_O_RDONLY,
                        &file_config) != LFS_ERR_OK) {
        return -1;
    }
    if (lfs_file_seek(&filesystem, &file, offset, LFS_SEEK_SET) < 0) {
        lfs_file_close(&filesystem, &file);
        return -1;
    }
    lfs_ssize_t count = lfs_file_read(&filesystem, &file, buffer, capacity);
    lfs_file_close(&filesystem, &file);
    if (count < 0) return -2;
    if (size) *size = (uint32_t)count;
    return 0;
}

int fs2_write(const char *path, const void *data, uint32_t size, bool append) {
    char resolved[FS2_PATH_MAX];
    path_normalize(path, resolved);
    struct lfs_file_config file_config = {.buffer = file_cache};
    struct lfs_file file;
    int flags = LFS_O_WRONLY | LFS_O_CREAT | (append ? LFS_O_APPEND : LFS_O_TRUNC);
    if (lfs_file_opencfg(&filesystem, &file, resolved, flags, &file_config) != LFS_ERR_OK) {
        return -1;
    }
    lfs_ssize_t count = lfs_file_write(&filesystem, &file, data, size);
    int result = lfs_file_close(&filesystem, &file);
    return count == (lfs_ssize_t)size && result == LFS_ERR_OK ? 0 : -2;
}

int fs2_write_at(const char *path, uint32_t offset, const void *data,
                 uint32_t size) {
    char resolved[FS2_PATH_MAX];
    path_normalize(path, resolved);
    struct lfs_file_config file_config = {.buffer = file_cache};
    struct lfs_file file;
    if (lfs_file_opencfg(&filesystem, &file, resolved,
                        LFS_O_WRONLY | LFS_O_CREAT, &file_config) != LFS_ERR_OK) {
        return -1;
    }
    if (lfs_file_seek(&filesystem, &file, offset, LFS_SEEK_SET) < 0) {
        lfs_file_close(&filesystem, &file);
        return -1;
    }
    lfs_ssize_t count = lfs_file_write(&filesystem, &file, data, size);
    int result = lfs_file_close(&filesystem, &file);
    return count == (lfs_ssize_t)size && result == LFS_ERR_OK ? 0 : -2;
}

int fs2_truncate(const char *path, uint32_t size) {
    char resolved[FS2_PATH_MAX];
    path_normalize(path, resolved);
    struct lfs_file_config file_config = {.buffer = file_cache};
    struct lfs_file file;
    if (lfs_file_opencfg(&filesystem, &file, resolved,
                        LFS_O_WRONLY | LFS_O_CREAT, &file_config) != LFS_ERR_OK) {
        return -1;
    }
    int result = lfs_file_truncate(&filesystem, &file, size);
    int close_result = lfs_file_close(&filesystem, &file);
    return result == LFS_ERR_OK && close_result == LFS_ERR_OK ? 0 : -2;
}

int fs2_mkdir(const char *path) {
    char resolved[FS2_PATH_MAX];
    path_normalize(path, resolved);
    return lfs_mkdir(&filesystem, resolved) == LFS_ERR_OK ? 0 : -1;
}

int fs2_remove(const char *path) {
    char resolved[FS2_PATH_MAX];
    path_normalize(path, resolved);
    return lfs_remove(&filesystem, resolved) == LFS_ERR_OK ? 0 : -1;
}

int fs2_rename(const char *old_path, const char *new_path) {
    char old_resolved[FS2_PATH_MAX];
    char new_resolved[FS2_PATH_MAX];
    path_normalize(old_path, old_resolved);
    path_normalize(new_path, new_resolved);
    return lfs_rename(&filesystem, old_resolved, new_resolved) == LFS_ERR_OK ? 0 : -1;
}

int fs2_stat(const char *path, uint32_t *size, uint8_t *type) {
    char resolved[FS2_PATH_MAX];
    path_normalize(path, resolved);
    struct lfs_info info;
    if (lfs_stat(&filesystem, resolved, &info) != LFS_ERR_OK) return -1;
    if (size) *size = info.size;
    if (type) *type = info.type;
    return 0;
}

int fs2_migrate_legacy(uint32_t *imported, uint32_t *skipped) {
    uint32_t copied = 0;
    uint32_t ignored = 0;
    if (!mounted) return -1;
    for (uint32_t index = 0; index < vfs_count(); index++) {
        const virtual_file *file = vfs_at(index);
        if (!file || file->read_only) continue;
        char destination[FS2_PATH_MAX] = "/import/";
        uint32_t base_length = (uint32_t)strlen(destination);
        uint32_t name_length = (uint32_t)strlen(file->name);
        if (base_length + name_length + 1 > sizeof(destination)) {
            ignored++;
            continue;
        }
        memcpy(destination + base_length, file->name, name_length + 1);
        if (fs2_stat(destination, 0, 0) == 0) {
            ignored++;
            continue;
        }
        if (fs2_write(destination, file->content, file->size, false) != 0) {
            return -2;
        }
        copied++;
    }
    if (imported) *imported = copied;
    if (skipped) *skipped = ignored;
    return 0;
}

static int fsck_visit(void *data, lfs_block_t block) {
    uint32_t *used = (uint32_t *)data;
    if (block >= FS2_LFS_BLOCKS) return -101;
    uint32_t byte = (uint32_t)block / 8U;
    uint8_t mask = (uint8_t)(1U << ((uint32_t)block % 8U));
    if (fsck_bitmap[byte] & mask) return 0;
    fsck_bitmap[byte] |= mask;
    (*used)++;
    return 0;
}

int fs2_fsck(uint32_t *used_blocks, uint32_t *total_blocks) {
    if (!mounted) return -1;
    memset(fsck_bitmap, 0, sizeof(fsck_bitmap));
    uint32_t used = 0;
    int result = lfs_fs_traverse(&filesystem, fsck_visit, &used);
    if (used_blocks) *used_blocks = used;
    if (total_blocks) *total_blocks = FS2_LFS_BLOCKS;
    return result == LFS_ERR_OK ? 0 : result;
}

int fs2_copy_to_vfs(const char *path, const char *name, bool read_only) {
    uint8_t buffer[VFS_CONTENT_CAPACITY + 1];
    uint32_t size;
    if (fs2_read(path, buffer, VFS_CONTENT_CAPACITY, &size) != 0) return -1;
    return vfs_add(name, buffer, size, read_only);
}

uint32_t fs2_capacity_bytes(void) {
    return FS2_LFS_BLOCKS * FS2_BLOCK_SIZE;
}

uint32_t fs2_free_bytes(void) {
    lfs_ssize_t used = lfs_fs_size(&filesystem);
    lfs_size_t total = FS2_LFS_BLOCKS;
    if (used < 0 || (lfs_size_t)used > total) return 0;
    return (uint32_t)(total - (lfs_size_t)used) * FS2_BLOCK_SIZE;
}

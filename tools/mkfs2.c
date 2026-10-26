#define _DARWIN_C_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lfs.h"

#define BLOCK_SIZE 4096U
#define RESERVED_BLOCK 1U
#define TOTAL_BLOCKS 32768U
#define LFS_BLOCKS (TOTAL_BLOCKS - RESERVED_BLOCK)

static int disk_fd;
static lfs_t fs;
static uint8_t read_buffer[BLOCK_SIZE];
static uint8_t program_buffer[BLOCK_SIZE];
static uint8_t lookahead[64];

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t lfs_offset_blocks;
    uint32_t checksum;
} __attribute__((packed)) cklosfs2_super;

static int disk_read(const struct lfs_config *config, lfs_block_t block,
                     lfs_off_t offset, void *buffer, lfs_size_t size) {
    (void)config;
    off_t at = ((off_t)block + RESERVED_BLOCK) * BLOCK_SIZE + offset;
    ssize_t read_count = pread(disk_fd, buffer, size, at);
    return read_count == (ssize_t)size ? 0 : LFS_ERR_IO;
}

static int disk_program(const struct lfs_config *config, lfs_block_t block,
                        lfs_off_t offset, const void *buffer, lfs_size_t size) {
    (void)config;
    off_t at = ((off_t)block + RESERVED_BLOCK) * BLOCK_SIZE + offset;
    ssize_t written = pwrite(disk_fd, buffer, size, at);
    return written == (ssize_t)size ? 0 : LFS_ERR_IO;
}

static int disk_erase(const struct lfs_config *config, lfs_block_t block) {
    (void)config;
    off_t at = ((off_t)block + RESERVED_BLOCK) * BLOCK_SIZE;
    uint8_t zeros[BLOCK_SIZE] = {0};
    return pwrite(disk_fd, zeros, sizeof(zeros), at) == (ssize_t)sizeof(zeros)
         ? 0 : LFS_ERR_IO;
}

static int disk_sync(const struct lfs_config *config) {
    (void)config;
    return fsync(disk_fd) == 0 ? 0 : LFS_ERR_IO;
}

static const struct lfs_config config = {
    .read = disk_read,
    .prog = disk_program,
    .erase = disk_erase,
    .sync = disk_sync,
    .read_size = 16,
    .prog_size = 16,
    .block_size = BLOCK_SIZE,
    .block_count = LFS_BLOCKS,
    .cache_size = BLOCK_SIZE,
    .lookahead_size = sizeof(lookahead),
    .read_buffer = read_buffer,
    .prog_buffer = program_buffer,
    .lookahead_buffer = lookahead,
    .block_cycles = 500,
};

static int copy_file(const char *host_path, const char *guest_path) {
    FILE *source = fopen(host_path, "rb");
    if (!source) {
        perror(host_path);
        return -1;
    }
    struct lfs_file file;
    int error = lfs_file_open(&fs, &file, guest_path,
                              LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (error) {
        fclose(source);
        fprintf(stderr, "lfs open %s: %d\n", guest_path, error);
        return -1;
    }
    uint8_t buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0) {
        if (lfs_file_write(&fs, &file, buffer, count) != (lfs_ssize_t)count) {
            fprintf(stderr, "write failed: %s\n", guest_path);
            lfs_file_close(&fs, &file);
            fclose(source);
            return -1;
        }
    }
    int close_error = lfs_file_close(&fs, &file);
    fclose(source);
    return error ? -1 : close_error;
}

static int copy_tree(const char *host_path, const char *guest_path) {
    struct stat info;
    if (stat(host_path, &info) != 0) {
        perror(host_path);
        return -1;
    }
    if (S_ISREG(info.st_mode)) {
        return copy_file(host_path, guest_path);
    }
    if (!S_ISDIR(info.st_mode)) {
        return 0;
    }

    int error = lfs_mkdir(&fs, guest_path);
    if (error && error != LFS_ERR_EXIST) {
        fprintf(stderr, "mkdir %s: %d\n", guest_path, error);
        return -1;
    }
    DIR *directory = opendir(host_path);
    if (!directory) {
        perror(host_path);
        return -1;
    }
    struct dirent *entry;
    int result = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char host_child[4096];
        char guest_child[4096];
        snprintf(host_child, sizeof(host_child), "%s/%s", host_path, entry->d_name);
        if (strcmp(guest_path, "/") == 0) {
            snprintf(guest_child, sizeof(guest_child), "/%s", entry->d_name);
        } else {
            snprintf(guest_child, sizeof(guest_child), "%s/%s", guest_path, entry->d_name);
        }
        if (copy_tree(host_child, guest_child) != 0) {
            result = -1;
            break;
        }
    }
    closedir(directory);
    return result;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: mkfs2 DISK.img ROOT_DIRECTORY\n");
        return 2;
    }
    disk_fd = open(argv[1], O_RDWR | O_CREAT, 0644);
    if (disk_fd < 0) {
        perror(argv[1]);
        return 1;
    }
    if (ftruncate(disk_fd, (off_t)TOTAL_BLOCKS * BLOCK_SIZE) != 0) {
        perror("ftruncate");
        return 1;
    }
    if (lfs_format(&fs, &config) != 0 || lfs_mount(&fs, &config) != 0) {
        fprintf(stderr, "LittleFS format failed\n");
        return 1;
    }
    if (copy_tree(argv[2], "/") != 0) {
        return 1;
    }
    lfs_unmount(&fs);

    cklosfs2_super super = {0};
    memcpy(super.magic, "CKLOSFS2", 8);
    super.version = 2;
    super.block_size = BLOCK_SIZE;
    super.total_blocks = TOTAL_BLOCKS;
    super.lfs_offset_blocks = RESERVED_BLOCK;
    const uint8_t *bytes = (const uint8_t *)&super;
    for (uint32_t index = 0; index < offsetof(cklosfs2_super, checksum); index++) {
        super.checksum = super.checksum * 33U + bytes[index];
    }
    if (pwrite(disk_fd, &super, sizeof(super), 0) != (ssize_t)sizeof(super)) {
        perror("superblock");
        return 1;
    }
    fsync(disk_fd);
    close(disk_fd);
    printf("Built CKLOSFS2 image: %s (%u MiB)\n", argv[1],
           TOTAL_BLOCKS * BLOCK_SIZE / (1024U * 1024U));
    return 0;
}

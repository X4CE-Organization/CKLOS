#include "disk.h"
#include "io.h"
#include "string.h"
#include "vfs.h"

#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7

#define ATA_STATUS_ERROR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_BSY 0x80

#define DISK_MAGIC "CKLOSFS1"
#define DISK_VERSION 1
#define SUPER_SECTOR 0
#define METADATA_START 1
#define CONTENT_START (METADATA_START + VFS_MAX_FILES)
#define DISK_TOTAL_SECTORS 4096

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t file_count;
    uint32_t checksum;
    uint8_t reserved[492];
} __attribute__((packed)) disk_superblock;

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t flags;
    uint32_t content_sector;
    char name[VFS_NAME_CAPACITY];
    uint8_t reserved[7];
} __attribute__((packed)) disk_record;

static bool present;
static uint32_t saved_count;
static uint8_t sector_buffer[513];

static uint32_t disk_checksum(const uint8_t *data, uint32_t size) {
    uint32_t checksum = 2166136261U;
    for (uint32_t index = 0; index < size; index++) {
        checksum ^= data[index];
        checksum *= 16777619U;
    }
    return checksum;
}

static bool wait_not_busy(void) {
    for (uint32_t timeout = 0; timeout < 1000000; timeout++) {
        uint8_t status = inb(ATA_STATUS);
        if (status == 0xFF) {
            return false;
        }
        if (!(status & ATA_STATUS_BSY)) {
            return true;
        }
    }
    return false;
}

static bool wait_drq(void) {
    for (uint32_t timeout = 0; timeout < 1000000; timeout++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & ATA_STATUS_ERROR || status & ATA_STATUS_DF) {
            return false;
        }
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) {
            return true;
        }
    }
    return false;
}

static bool ata_read(uint32_t lba, void *buffer) {
    if (!wait_not_busy()) {
        return false;
    }
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, 0x20);
    if (!wait_drq()) {
        return false;
    }
    insw(ATA_DATA, buffer, 256);
    return true;
}

static bool ata_write(uint32_t lba, const void *buffer) {
    if (!wait_not_busy()) {
        return false;
    }
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, 0x30);
    if (!wait_drq()) {
        return false;
    }
    outsw(ATA_DATA, buffer, 256);
    outb(ATA_COMMAND, 0xE7);
    return wait_not_busy();
}

bool disk_init(void) {
    present = false;
    saved_count = 0;
    if (inb(ATA_STATUS) == 0xFF) {
        return false;
    }
    if (!ata_read(SUPER_SECTOR, sector_buffer)) {
        return false;
    }
    present = true;
    disk_superblock *super = (disk_superblock *)sector_buffer;
    if (strncmp(super->magic, DISK_MAGIC, 8) == 0 && super->version == DISK_VERSION) {
        saved_count = super->file_count <= VFS_MAX_FILES
                    ? super->file_count : VFS_MAX_FILES;
    }
    return true;
}

bool disk_available(void) {
    return present;
}

bool disk_save_filesystem(void) {
    if (!present) {
        return false;
    }

    memset(sector_buffer, 0, sizeof(sector_buffer));
    for (uint32_t index = 0; index < VFS_MAX_FILES; index++) {
        memset(sector_buffer, 0, sizeof(sector_buffer));
        if (!ata_write(METADATA_START + index, sector_buffer)) {
            return false;
        }
    }

    uint32_t count = 0;
    for (uint32_t index = 0; index < vfs_count(); index++) {
        const virtual_file *file = vfs_at(index);
        if (file->read_only) {
            continue;
        }
        if (count >= VFS_MAX_FILES) {
            break;
        }
        memset(sector_buffer, 0, sizeof(sector_buffer));
        disk_record *record = (disk_record *)sector_buffer;
        record->magic = 0x454C4946U;
        record->size = file->size;
        record->flags = 1;
        record->content_sector = CONTENT_START + count;
        strcpy(record->name, file->name);
        if (!ata_write(METADATA_START + count, sector_buffer)) {
            return false;
        }

        memset(sector_buffer, 0, sizeof(sector_buffer));
        memcpy(sector_buffer, file->content, file->size);
        if (!ata_write(CONTENT_START + count, sector_buffer)) {
            return false;
        }
        count++;
    }

    memset(sector_buffer, 0, sizeof(sector_buffer));
    disk_superblock *super = (disk_superblock *)sector_buffer;
    memcpy(super->magic, DISK_MAGIC, 8);
    super->version = DISK_VERSION;
    super->file_count = count;
    super->checksum = disk_checksum(sector_buffer, 16);
    if (!ata_write(SUPER_SECTOR, sector_buffer)) {
        return false;
    }
    saved_count = count;
    return true;
}

bool disk_load_filesystem(void) {
    if (!present) {
        return false;
    }
    if (!ata_read(SUPER_SECTOR, sector_buffer)) {
        return false;
    }
    disk_superblock *super = (disk_superblock *)sector_buffer;
    if (strncmp(super->magic, DISK_MAGIC, 8) != 0 || super->version != DISK_VERSION) {
        return false;
    }
    uint32_t count = super->file_count <= VFS_MAX_FILES ? super->file_count : VFS_MAX_FILES;
    for (uint32_t index = 0; index < count; index++) {
        if (!ata_read(METADATA_START + index, sector_buffer)) {
            return false;
        }
        disk_record *record = (disk_record *)sector_buffer;
        if (record->magic != 0x454C4946U || !record->flags
            || record->size > VFS_CONTENT_CAPACITY) {
            continue;
        }
        uint32_t content_sector = record->content_sector;
        char name[VFS_NAME_CAPACITY];
        strcpy(name, record->name);
        uint32_t size = record->size;
        if (!ata_read(content_sector, sector_buffer)) {
            return false;
        }
        sector_buffer[size] = '\0';
        if (vfs_write(name, (const char *)sector_buffer, false) != VFS_OK) {
            return false;
        }
    }
    saved_count = count;
    return true;
}

bool disk_format(void) {
    if (!present) {
        return false;
    }
    memset(sector_buffer, 0, sizeof(sector_buffer));
    if (!ata_write(SUPER_SECTOR, sector_buffer)) {
        return false;
    }
    for (uint32_t index = 0; index < VFS_MAX_FILES; index++) {
        memset(sector_buffer, 0, sizeof(sector_buffer));
        if (!ata_write(METADATA_START + index, sector_buffer)) {
            return false;
        }
    }
    saved_count = 0;
    return true;
}

uint32_t disk_saved_file_count(void) {
    return saved_count;
}

uint32_t disk_capacity_bytes(void) {
    return DISK_TOTAL_SECTORS * 512U;
}

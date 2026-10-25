#ifndef CKLOS_MEMORY_H
#define CKLOS_MEMORY_H

#include "types.h"

#define BOOT_INFO_MAGIC 0x534C4B43

typedef struct {
    uint32_t magic;
    uint32_t entry_count;
    uint32_t map_address;
    uint32_t boot_drive;
} boot_info;

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
} __attribute__((packed)) e820_entry;

void memory_init(const boot_info *info);
uint32_t physical_memory_bytes(void);
uint32_t page_total(void);
uint32_t page_used(void);
uint32_t page_free(void);
void *page_alloc(void);
bool page_free_address(void *address);

void *kmalloc(size_t size);
void kfree(void *address);
uint32_t heap_total(void);
uint32_t heap_used(void);
uint32_t heap_free(void);
uint32_t heap_largest_free(void);
bool heap_self_test(void);

#endif

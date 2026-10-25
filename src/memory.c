#include "memory.h"
#include "string.h"

#define PAGE_SIZE 4096U
#define IDENTITY_MAP_BYTES (64U * 1024U * 1024U)
#define PAGE_BITMAP_LIMIT (256U * 1024U * 1024U)
#define PAGE_ALLOC_START (4U * 1024U * 1024U)
#define HEAP_START (4U * 1024U * 1024U)
#define HEAP_SIZE (1024U * 1024U)
#define HEAP_MAGIC 0x48454150U

#define PAGE_PRESENT 0x001U
#define PAGE_WRITABLE 0x002U
#define PAGE_4MB 0x080U

typedef struct heap_block {
    uint32_t magic;
    uint32_t size;
    bool free;
    struct heap_block *previous;
    struct heap_block *next;
} heap_block;

static uint32_t memory_size;
static uint32_t usable_top;
static uint32_t page_bitmap[PAGE_BITMAP_LIMIT / PAGE_SIZE / 32];
static heap_block *heap_first;

static uint32_t boot_entry_base(const e820_entry *entry) {
    return (uint32_t)entry->base;
}

static uint32_t boot_entry_end(const e820_entry *entry) {
    uint64_t end = entry->base + entry->length;
    if (end > 0xFFFFFFFFULL || entry->base > 0xFFFFFFFFULL) {
        return 0xFFFFFFFFU;
    }
    return (uint32_t)end;
}

static bool bitmap_page_used(uint32_t page) {
    return (page_bitmap[page / 32] & (1U << (page % 32))) != 0;
}

static void bitmap_mark_used(uint32_t page) {
    page_bitmap[page / 32] |= 1U << (page % 32);
}

static void bitmap_mark_free(uint32_t page) {
    page_bitmap[page / 32] &= ~(1U << (page % 32));
}

static void paging_init(void) {
    /* Long mode page tables are installed by the bootloader. */
}

void memory_init(const boot_info *info) {
    uint32_t detected_top = 0;
    uint32_t entry_count = 0;
    const e820_entry *entries = 0;

    memset(page_bitmap, 0xFF, sizeof(page_bitmap));
    if (info && info->magic == BOOT_INFO_MAGIC && info->entry_count) {
        entry_count = info->entry_count > 64 ? 64 : info->entry_count;
        entries = (const e820_entry *)(uintptr_t)info->map_address;
        for (uint32_t index = 0; index < entry_count; index++) {
            if (entries[index].type != 1) {
                continue;
            }
            uint32_t end = boot_entry_end(&entries[index]);
            if (end > detected_top) {
                detected_top = end;
            }
        }
    }

    if (detected_top < PAGE_ALLOC_START + HEAP_SIZE + PAGE_SIZE) {
        detected_top = 64U * 1024U * 1024U;
    }
    usable_top = detected_top > IDENTITY_MAP_BYTES ? IDENTITY_MAP_BYTES : detected_top;
    usable_top &= ~(PAGE_SIZE - 1);
    memory_size = usable_top;

    if (entries) {
        for (uint32_t index = 0; index < entry_count; index++) {
            if (entries[index].type != 1) {
                continue;
            }
            uint32_t start = boot_entry_base(&entries[index]);
            uint32_t end = boot_entry_end(&entries[index]);
            start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            end &= ~(PAGE_SIZE - 1);
            if (start < PAGE_ALLOC_START) {
                start = PAGE_ALLOC_START;
            }
            if (end > usable_top) {
                end = usable_top;
            }
            for (uint32_t address = start; address < end; address += PAGE_SIZE) {
                bitmap_mark_free(address / PAGE_SIZE);
            }
        }
    } else {
        for (uint32_t address = PAGE_ALLOC_START; address < usable_top; address += PAGE_SIZE) {
            bitmap_mark_free(address / PAGE_SIZE);
        }
    }

    /* The first MiB after PAGE_ALLOC_START belongs to the kernel heap. */
    for (uint32_t address = HEAP_START; address < HEAP_START + HEAP_SIZE; address += PAGE_SIZE) {
        bitmap_mark_used(address / PAGE_SIZE);
    }

    paging_init();

    heap_first = (heap_block *)HEAP_START;
    heap_first->magic = HEAP_MAGIC;
    heap_first->size = HEAP_SIZE - sizeof(heap_block);
    heap_first->free = true;
    heap_first->previous = 0;
    heap_first->next = 0;
}

uint32_t physical_memory_bytes(void) {
    return memory_size;
}

uint32_t page_total(void) {
    uint32_t total = 0;
    for (uint32_t page = PAGE_ALLOC_START / PAGE_SIZE;
         page < usable_top / PAGE_SIZE; page++) {
        total++;
    }
    return total;
}

uint32_t page_used(void) {
    uint32_t used = 0;
    for (uint32_t page = PAGE_ALLOC_START / PAGE_SIZE;
         page < usable_top / PAGE_SIZE; page++) {
        if (bitmap_page_used(page)) {
            used++;
        }
    }
    return used;
}

uint32_t page_free(void) {
    return page_total() - page_used();
}

void *page_alloc(void) {
    for (uint32_t page = PAGE_ALLOC_START / PAGE_SIZE;
         page < usable_top / PAGE_SIZE; page++) {
        if (!bitmap_page_used(page)) {
            bitmap_mark_used(page);
            void *address = (void *)(uintptr_t)(page * PAGE_SIZE);
            memset(address, 0, PAGE_SIZE);
            return address;
        }
    }
    return 0;
}

bool page_free_address(void *address) {
    uintptr_t value = (uintptr_t)address;
    if (!value || value < PAGE_ALLOC_START || value >= usable_top || value & (PAGE_SIZE - 1)) {
        return false;
    }
    uint32_t page = value / PAGE_SIZE;
    if (!bitmap_page_used(page)) {
        return false;
    }
    bitmap_mark_free(page);
    return true;
}

void *kmalloc(size_t requested_size) {
    if (!requested_size) {
        return 0;
    }
    uint32_t size = ((uint32_t)requested_size + 7U) & ~7U;
    for (heap_block *block = heap_first; block; block = block->next) {
        if (block->magic != HEAP_MAGIC || !block->free || block->size < size) {
            continue;
        }
        if (block->size >= size + sizeof(heap_block) + 16) {
            heap_block *split = (heap_block *)((uint8_t *)block + sizeof(heap_block) + size);
            split->magic = HEAP_MAGIC;
            split->size = block->size - size - sizeof(heap_block);
            split->free = true;
            split->previous = block;
            split->next = block->next;
            if (split->next) {
                split->next->previous = split;
            }
            block->next = split;
            block->size = size;
        }
        block->free = false;
        return (uint8_t *)block + sizeof(heap_block);
    }
    return 0;
}

void kfree(void *address) {
    if (!address) {
        return;
    }
    heap_block *block = (heap_block *)((uint8_t *)address - sizeof(heap_block));
    if (block->magic != HEAP_MAGIC || block->free) {
        return;
    }
    block->free = true;

    if (block->next && block->next->magic == HEAP_MAGIC && block->next->free) {
        heap_block *next = block->next;
        block->size += sizeof(heap_block) + next->size;
        block->next = next->next;
        if (block->next) {
            block->next->previous = block;
        }
        next->magic = 0;
    }
    if (block->previous && block->previous->magic == HEAP_MAGIC && block->previous->free) {
        heap_block *previous = block->previous;
        previous->size += sizeof(heap_block) + block->size;
        previous->next = block->next;
        if (previous->next) {
            previous->next->previous = previous;
        }
        block->magic = 0;
    }
}

uint32_t heap_total(void) {
    return HEAP_SIZE;
}

uint32_t heap_used(void) {
    uint32_t used = 0;
    for (heap_block *block = heap_first; block; block = block->next) {
        if (!block->free) {
            used += block->size + sizeof(heap_block);
        }
    }
    return used;
}

uint32_t heap_free(void) {
    uint32_t available = 0;
    for (heap_block *block = heap_first; block; block = block->next) {
        if (block->free) {
            available += block->size;
        }
    }
    return available;
}

uint32_t heap_largest_free(void) {
    uint32_t largest = 0;
    for (heap_block *block = heap_first; block; block = block->next) {
        if (block->free && block->size > largest) {
            largest = block->size;
        }
    }
    return largest;
}

bool heap_self_test(void) {
    uint8_t *first = (uint8_t *)kmalloc(128);
    uint8_t *second = (uint8_t *)kmalloc(4096);
    if (!first || !second) {
        kfree(first);
        kfree(second);
        return false;
    }
    memset(first, 0xA5, 128);
    memset(second, 0x5A, 4096);
    bool ok = first[127] == 0xA5 && second[4095] == 0x5A;
    kfree(first);
    kfree(second);
    return ok;
}

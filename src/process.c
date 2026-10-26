#include "console.h"
#include "fs2.h"
#include "memory.h"
#include "process.h"
#include "rks2.h"
#include "string.h"
#include "syscall.h"

extern void process_load_gdt(void *descriptor);
extern void process_enter_user(uint64_t entry, uint64_t stack);
extern void process_user_return(void);

#define PROCESS_LIMIT 8
#define PROCESS_PAGES 512
#define USER_BASE 0x0000004000000000ULL
#define USER_REGION_LIMIT 0x0000007800000000ULL
#define MMAP_BASE 0x0000005000000000ULL
#define STACK_TOP 0x0000007FFFFFF000ULL
#define USER_STACK_SIZE 0x0000000000040000ULL
#define PAGE_SIZE 4096ULL
#define ELF_MAX_SIZE (512U * 1024U)
#define PAGE_USER 0x007ULL
#define PAGE_PRESENT 0x001ULL

typedef struct {
    uint8_t identity[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) elf64_header;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed)) elf64_program_header;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) process_tss;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_pointer;

typedef struct {
    uint64_t virtual_address;
    uint64_t flags;
} user_page;

typedef struct {
    bool used;
    bool writable;
    bool dirty;
    uint8_t kind;
    uint8_t pipe_index;
    char path[256];
    uint8_t *data;
    uint32_t size;
    uint32_t offset;
} process_file;

#define FILE_KIND_FS 1
#define FILE_KIND_PIPE_READ 2
#define FILE_KIND_PIPE_WRITE 3
#define PIPE_LIMIT 4
#define PIPE_CAPACITY 4096

typedef struct {
    bool used;
    uint32_t head;
    uint32_t size;
    uint32_t readers;
    uint32_t writers;
    uint8_t data[PIPE_CAPACITY];
} pipe_buffer;

typedef enum {
    PROCESS_UNUSED,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_ZOMBIE,
    PROCESS_STOPPED
} process_state;

typedef struct {
    process_state state;
    int32_t pid;
    int32_t parent_pid;
    int32_t exit_code;
    char name[40];
    uint64_t cr3;
    uint64_t entry;
    uint64_t stack_pointer;
    uint64_t brk_start;
    uint64_t brk_current;
    uint64_t brk_limit;
    uint64_t mmap_next;
    uint64_t fs_base;
    uint64_t wait_status_pointer;
    uint64_t allocations[PROCESS_PAGES];
    uint32_t allocation_count;
    user_page pages[PROCESS_PAGES];
    uint32_t page_count;
    process_file files[8];
    interrupt_frame frame;
    uint8_t fx_state[512] __attribute__((aligned(16)));
} process;

static uint64_t gdt[7];
static process_tss tss;
static uint8_t exception_stack[32768] __attribute__((aligned(16)));
static process processes[PROCESS_LIMIT];
static int32_t current_process = -1;
static int32_t next_pid = 1;
static int32_t last_exit = -1;
static uint64_t kernel_cr3;
static uint32_t preempt_ticks;
static uint8_t default_fx_state[512] __attribute__((aligned(16)));
static pipe_buffer pipes[PIPE_LIMIT];

static void set_fs_base(uint64_t base) {
    uint32_t low = (uint32_t)base;
    uint32_t high = (uint32_t)(base >> 32);
    __asm__ volatile("wrmsr" : : "c"(0xC0000100U), "a"(low), "d"(high));
}

static uint64_t get_fs_base(void) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000100U));
    return ((uint64_t)high << 32) | low;
}

static void save_fx(process *slot) {
    __asm__ volatile("fxsave (%0)" : : "r"(slot->fx_state) : "memory");
}

static void restore_fx(process *slot) {
    __asm__ volatile("fxrstor (%0)" : : "r"(slot->fx_state) : "memory");
}

static bool user_write_cr3(uint64_t cr3, void *user_pointer,
                           const void *source, uint32_t size);

static void release_file_reference(process_file *file) {
    if (file->kind == FILE_KIND_FS && file->data) {
        kfree(file->data);
    } else if (file->kind == FILE_KIND_PIPE_READ) {
        pipe_buffer *pipe = &pipes[file->pipe_index];
        if (pipe->readers) pipe->readers--;
        if (!pipe->readers && !pipe->writers) pipe->used = false;
    } else if (file->kind == FILE_KIND_PIPE_WRITE) {
        pipe_buffer *pipe = &pipes[file->pipe_index];
        if (pipe->writers) pipe->writers--;
        if (!pipe->readers && !pipe->writers) pipe->used = false;
    }
    memset(file, 0, sizeof(*file));
}

static void clear_files(process *slot) {
    for (uint32_t index = 0; index < 8; index++) {
        if (slot->files[index].used) {
            release_file_reference(&slot->files[index]);
        }
    }
    memset(slot->files, 0, sizeof(slot->files));
}

static uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void write_cr3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

static uint64_t *entry_address(uint64_t entry) {
    return (uint64_t *)(uintptr_t)(entry & 0x000FFFFFFFFFF000ULL);
}

static bool track_allocation(process *slot, uint64_t page) {
    if (!page || slot->allocation_count >= PROCESS_PAGES) return false;
    slot->allocations[slot->allocation_count++] = page;
    return true;
}

static uint64_t allocate_table(process *slot) {
    uint64_t page = (uint64_t)(uintptr_t)page_alloc();
    return track_allocation(slot, page) ? page : 0;
}

static uint64_t allocate_user_page(process *slot, uint64_t address, uint64_t flags) {
    if (slot->page_count >= PROCESS_PAGES) return 0;
    uint64_t page = (uint64_t)(uintptr_t)page_alloc();
    if (!track_allocation(slot, page)) return 0;
    slot->pages[slot->page_count].virtual_address = address & ~(PAGE_SIZE - 1);
    slot->pages[slot->page_count].flags = flags;
    slot->page_count++;
    memset((void *)(uintptr_t)page, 0, PAGE_SIZE);
    return page;
}

static uint64_t *ensure_child_table(process *slot, uint64_t *parent) {
    if (*parent & PAGE_PRESENT) return entry_address(*parent);
    uint64_t page = allocate_table(slot);
    if (!page) return 0;
    *parent = page | PAGE_USER;
    return (uint64_t *)(uintptr_t)page;
}

static uint64_t *page_entry(process *slot, uint64_t address, bool create) {
    uint64_t *pml4 = entry_address(slot->cr3);
    uint64_t pml4_index = (address >> 39) & 0x1FF;
    uint64_t *pdpt;
    if (pml4[pml4_index] & PAGE_PRESENT) {
        pdpt = entry_address(pml4[pml4_index]);
    } else if (create) {
        pdpt = ensure_child_table(slot, &pml4[pml4_index]);
        if (!pdpt) return 0;
    } else {
        return 0;
    }

    uint64_t pdpt_index = (address >> 30) & 0x1FF;
    uint64_t *pd;
    if (pdpt[pdpt_index] & PAGE_PRESENT) {
        pd = entry_address(pdpt[pdpt_index]);
    } else if (create) {
        pd = ensure_child_table(slot, &pdpt[pdpt_index]);
        if (!pd) return 0;
    } else {
        return 0;
    }

    uint64_t pd_index = (address >> 21) & 0x1FF;
    uint64_t *pt;
    if (pd[pd_index] & PAGE_PRESENT) {
        if (pd[pd_index] & 0x80) return 0;
        pt = entry_address(pd[pd_index]);
    } else if (create) {
        pt = ensure_child_table(slot, &pd[pd_index]);
        if (!pt) return 0;
    } else {
        return 0;
    }
    return &pt[(address >> 12) & 0x1FF];
}

static uint64_t map_page(process *slot, uint64_t address, uint64_t flags) {
    address &= ~(PAGE_SIZE - 1);
    uint64_t *entry = page_entry(slot, address, true);
    if (!entry) return 0;
    if (*entry & PAGE_PRESENT) {
        return *entry & 0x000FFFFFFFFFF000ULL;
    }
    uint64_t page = allocate_user_page(slot, address, flags);
    if (!page) return 0;
    *entry = page | flags | PAGE_PRESENT | 0x04;
    return page;
}

static void clear_address_space(process *slot) {
    clear_files(slot);
    for (uint32_t index = 0; index < slot->allocation_count; index++) {
        if (slot->allocations[index]) {
            page_free_address((void *)(uintptr_t)slot->allocations[index]);
        }
    }
    slot->allocation_count = 0;
    slot->page_count = 0;
    slot->cr3 = 0;
}

static bool create_address_space(process *slot) {
    uint64_t page = allocate_table(slot);
    if (!page) return false;
    slot->cr3 = page;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)page;
    /*
     * USER_BASE (256 GiB) shares PML4 index 0 with the low kernel identity
     * map.  Give the process a private PDPT so the kernel's supervisor
     * entries and the user's U/S entries can coexist safely.
     */
    uint64_t pdpt_page = allocate_table(slot);
    if (!pdpt_page) return false;
    pml4[0] = pdpt_page | PAGE_USER;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)pdpt_page;
    pdpt[0] = 0x3003;
    uint64_t *kernel_pdpt = (uint64_t *)(uintptr_t)0x2000;
    for (uint32_t index = 1; index < 512; index++) {
        if (kernel_pdpt[index] & PAGE_PRESENT) {
            pdpt[index] = kernel_pdpt[index];
        }
    }
    slot->brk_start = USER_BASE;
    slot->brk_current = USER_BASE;
    slot->brk_limit = USER_BASE;
    slot->mmap_next = MMAP_BASE;
    slot->fs_base = 0;
    return true;
}

static process *find_pid(int32_t pid) {
    for (uint32_t index = 0; index < PROCESS_LIMIT; index++) {
        if (processes[index].state != PROCESS_UNUSED && processes[index].pid == pid) {
            return &processes[index];
        }
    }
    return 0;
}

static process *free_slot(void) {
    for (uint32_t index = 0; index < PROCESS_LIMIT; index++) {
        if (processes[index].state == PROCESS_UNUSED) return &processes[index];
    }
    return 0;
}

static int32_t slot_index(process *slot) {
    return (int32_t)(slot - processes);
}

static bool install_arguments(process *slot, int argc, const char *const *argv) {
    if (argc < 0 || argc > 32) return false;
    uint64_t top = STACK_TOP;
    uint64_t pointers[32];
    for (int index = argc - 1; index >= 0; index--) {
        uint32_t length = (uint32_t)strlen(argv[index]);
        top -= length + 1;
        if (top < STACK_TOP - USER_STACK_SIZE) return false;
        if (!user_write_cr3(slot->cr3, (void *)(uintptr_t)top,
                            argv[index], length + 1)) {
            return false;
        }
        pointers[index] = top;
    }
    uint64_t frame_size = (uint64_t)(argc + 4) * sizeof(uint64_t);
    uint64_t stack = (top - frame_size) & ~0x0FULL;
    if (stack < STACK_TOP - USER_STACK_SIZE) return false;
    uint64_t zero = 0;
    uint64_t count = (uint64_t)(uint32_t)argc;
    if (!user_write_cr3(slot->cr3, (void *)(uintptr_t)stack,
                        &count, sizeof(count))) {
        return false;
    }
    for (int index = 0; index < argc; index++) {
        if (!user_write_cr3(slot->cr3,
                            (void *)(uintptr_t)(stack + 8 * (index + 1)),
                            &pointers[index], sizeof(pointers[index]))) {
            return false;
        }
    }
    if (!user_write_cr3(slot->cr3,
                        (void *)(uintptr_t)(stack + 8 * (argc + 1)),
                        &zero, sizeof(zero))
        || !user_write_cr3(slot->cr3,
                           (void *)(uintptr_t)(stack + 8 * (argc + 2)),
                           &zero, sizeof(zero))
        || !user_write_cr3(slot->cr3,
                           (void *)(uintptr_t)(stack + 8 * (argc + 3)),
                           &zero, sizeof(zero))) {
        return false;
    }
    slot->frame.rsp = stack;
    slot->stack_pointer = stack;
    return true;
}

static bool load_elf(process *slot, const uint8_t *data, uint32_t size) {
    if (size < sizeof(elf64_header)) return false;
    const elf64_header *header = (const elf64_header *)data;
    if (header->identity[0] != 0x7F || header->identity[1] != 'E'
        || header->identity[2] != 'L' || header->identity[3] != 'F'
        || header->identity[4] != 2 || header->type != 2
        || header->machine != 62 || header->version != 1
        || header->entry < USER_BASE || header->entry >= USER_REGION_LIMIT
        || header->phentsize != sizeof(elf64_program_header)
        || header->phnum == 0 || header->phnum > 16
        || header->phoff + (uint64_t)header->phnum * sizeof(elf64_program_header) > size) {
        return false;
    }

    uint64_t highest = USER_BASE;
    for (uint16_t index = 0; index < header->phnum; index++) {
        const elf64_program_header *segment =
            (const elf64_program_header *)(data + header->phoff
                                           + index * sizeof(*segment));
        if (segment->type != 1) continue;
        if (segment->memsz < segment->filesz
            || segment->vaddr < USER_BASE
            || segment->vaddr + segment->memsz > USER_REGION_LIMIT
            || segment->vaddr + segment->memsz < segment->vaddr
            || segment->offset + segment->filesz > size
            || segment->offset + segment->filesz < segment->offset) {
            return false;
        }
        uint64_t first = segment->vaddr & ~(PAGE_SIZE - 1);
        uint64_t last = (segment->vaddr + segment->memsz + PAGE_SIZE - 1)
                      & ~(PAGE_SIZE - 1);
        for (uint64_t address = first; address < last; address += PAGE_SIZE) {
            uint64_t physical = map_page(slot, address, PAGE_USER);
            if (!physical) return false;
            uint8_t *destination = (uint8_t *)(uintptr_t)physical;
            uint64_t page_start = address;
            uint64_t copy_start = page_start > segment->vaddr
                                ? page_start : segment->vaddr;
            uint64_t copy_end = page_start + PAGE_SIZE;
            uint64_t segment_file_end = segment->vaddr + segment->filesz;
            if (copy_end > segment_file_end) copy_end = segment_file_end;
            if (copy_start < copy_end) {
                uint64_t file_offset = segment->offset
                                     + (copy_start - segment->vaddr);
                memcpy(destination + (copy_start - page_start),
                       data + file_offset, copy_end - copy_start);
            }
        }
        if (last > highest) highest = last;
    }

    uint64_t stack_page = STACK_TOP - USER_STACK_SIZE;
    uint64_t stack_physical = 0;
    for (uint64_t address = stack_page; address < STACK_TOP;
         address += PAGE_SIZE) {
        stack_physical = map_page(slot, address, PAGE_USER);
        if (!stack_physical) return false;
    }
    stack_physical = map_page(slot, STACK_TOP - PAGE_SIZE, PAGE_USER);
    if (!stack_physical) return false;
    uint64_t *initial_stack =
        (uint64_t *)(uintptr_t)(stack_physical + PAGE_SIZE - 32);
    initial_stack[0] = 0;
    initial_stack[1] = 0;
    initial_stack[2] = 0;
    initial_stack[3] = 0;
    slot->entry = header->entry;
    slot->stack_pointer = STACK_TOP - 32;
    slot->brk_start = (highest + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    slot->brk_current = slot->brk_start;
    slot->brk_limit = slot->brk_start;
    slot->fs_base = 0;
    return true;
}

static process *create_from_elf(const char *path, int32_t parent_pid) {
    uint32_t file_size;
    if (fs2_stat(path, &file_size, 0) != 0 || !file_size
        || file_size > RKS2_HEADER_SIZE + RKS2_MAX_PAYLOAD) return 0;
    uint8_t *image = (uint8_t *)kmalloc(file_size);
    if (!image) return 0;
    uint32_t actual;
    if (fs2_read(path, image, file_size, &actual) != 0 || actual != file_size) {
        kfree(image);
        return 0;
    }
    uint32_t size = file_size;
    if (rks2_is_container(image, file_size)) {
        uint8_t *payload;
        kfree(image);
        if (!rks2_extract(path, &payload, &size)) return 0;
        image = payload;
    }
    if (!size || size > ELF_MAX_SIZE) {
        kfree(image);
        return 0;
    }
    process *slot = free_slot();
    if (!slot || !create_address_space(slot)) {
        kfree(image);
        if (slot) clear_address_space(slot);
        return 0;
    }
    bool valid = load_elf(slot, image, size);
    kfree(image);
    if (!valid) {
        clear_address_space(slot);
        return 0;
    }
    slot->state = PROCESS_READY;
    slot->pid = next_pid++;
    slot->parent_pid = parent_pid;
    slot->exit_code = 0;
    strcpy(slot->name, path);
    memset(&slot->frame, 0, sizeof(slot->frame));
    memcpy(slot->fx_state, default_fx_state, sizeof(default_fx_state));
    slot->frame.rip = slot->entry;
    slot->frame.rsp = slot->stack_pointer;
    slot->frame.cs = 0x1B;
    slot->frame.ss = 0x23;
    slot->frame.rflags = 0x202;
    return slot;
}

static uint64_t user_page_physical(uint64_t cr3, uint64_t address, bool write) {
    if (address < USER_BASE) return 0;
    uint64_t *pml4 = entry_address(cr3);
    uint64_t entry = pml4[(address >> 39) & 0x1FF];
    if (!(entry & PAGE_PRESENT) || !(entry & 0x04)) return 0;
    uint64_t *pdpt = entry_address(entry);
    entry = pdpt[(address >> 30) & 0x1FF];
    if (!(entry & PAGE_PRESENT) || !(entry & 0x04)) return 0;
    uint64_t *pd = entry_address(entry);
    entry = pd[(address >> 21) & 0x1FF];
    if (!(entry & PAGE_PRESENT) || !(entry & 0x04)) return 0;
    if (entry & 0x80) {
        if (write && !(entry & 0x02)) return 0;
        return (entry & 0x000FFFFFFFE00000ULL)
             | (address & 0x1FFFFFULL);
    }
    uint64_t *pt = entry_address(entry);
    entry = pt[(address >> 12) & 0x1FF];
    if (!(entry & PAGE_PRESENT) || !(entry & 0x04)
        || (write && !(entry & 0x02))) {
        return 0;
    }
    return (entry & 0x000FFFFFFFFFF000ULL) | (address & 0xFFF);
}

static bool user_page_mapped(uint64_t cr3, uint64_t address, bool write) {
    return user_page_physical(cr3, address, write) != 0;
}

bool process_user_copy(void *destination, const void *user_pointer, uint32_t size) {
    if (current_process < 0) return false;
    process *slot = &processes[current_process];
    uint64_t address = (uint64_t)(uintptr_t)user_pointer;
    uint32_t copied = 0;
    while (copied < size) {
        uint64_t page = (address + copied) & ~(PAGE_SIZE - 1);
        if (!user_page_mapped(slot->cr3, page, false)) return false;
        uint32_t in_page = (uint32_t)((address + copied) & (PAGE_SIZE - 1));
        uint32_t count = (uint32_t)PAGE_SIZE - in_page;
        if (count > size - copied) count = size - copied;
        memcpy((uint8_t *)destination + copied,
               (const void *)(uintptr_t)(address + copied), count);
        copied += count;
    }
    return true;
}

static bool user_write_cr3(uint64_t cr3, void *user_pointer,
                           const void *source, uint32_t size) {
    uint64_t address = (uint64_t)(uintptr_t)user_pointer;
    uint32_t copied = 0;
    while (copied < size) {
        uint64_t physical =
            user_page_physical(cr3, address + copied, true);
        if (!physical) return false;
        uint32_t in_page = (uint32_t)((address + copied) & (PAGE_SIZE - 1));
        uint32_t count = (uint32_t)PAGE_SIZE - in_page;
        if (count > size - copied) count = size - copied;
        memcpy((void *)(uintptr_t)physical,
               (const uint8_t *)source + copied, count);
        copied += count;
    }
    return true;
}

bool process_user_read_string(char *destination, const char *user_pointer,
                              uint32_t capacity) {
    if (!capacity) return false;
    for (uint32_t index = 0; index + 1 < capacity; index++) {
        char character;
        if (!process_user_copy(&character,
                               (const char *)user_pointer + index, 1)) {
            return false;
        }
        destination[index] = character;
        if (!character) return index > 0;
    }
    destination[capacity - 1] = '\0';
    return false;
}

static process *choose_next(void) {
    int32_t start = current_process >= 0 ? current_process : -1;
    for (uint32_t offset = 1; offset <= PROCESS_LIMIT; offset++) {
        int32_t index = (start + (int32_t)offset + PROCESS_LIMIT) % PROCESS_LIMIT;
        if (processes[index].state == PROCESS_READY) return &processes[index];
    }
    return 0;
}

static interrupt_frame *return_to_kernel(interrupt_frame *frame, int32_t code) {
    write_cr3(kernel_cr3);
    current_process = -1;
    last_exit = code;
    frame->rip = (uint64_t)(uintptr_t)process_user_return;
    frame->cs = 0x08;
    frame->ss = 0x10;
    frame->rsp = 0x90000;
    frame->rflags = 0x002;
    frame->rax = code < 0 ? (uint64_t)(int64_t)code : (uint64_t)code;
    return frame;
}

static interrupt_frame *schedule(interrupt_frame *frame) {
    if (current_process < 0) return frame;
    process *old = &processes[current_process];
    memcpy(&old->frame, frame, sizeof(*frame));
    save_fx(old);
    if (old->state == PROCESS_RUNNING) old->state = PROCESS_READY;

    process *next = choose_next();
    if (!next) {
        int32_t code = old->state == PROCESS_ZOMBIE ? old->exit_code : 0;
        return return_to_kernel(frame, code);
    }
    next->state = PROCESS_RUNNING;
    current_process = slot_index(next);
    memcpy(frame, &next->frame, sizeof(*frame));
    restore_fx(next);
    set_fs_base(next->fs_base);
    write_cr3(next->cr3);
    return frame;
}

static int32_t clone_address_space(process *parent, process *child) {
    if (!create_address_space(child)) return -CKLOS_ENOMEM;
    for (uint32_t index = 0; index < parent->page_count; index++) {
        uint64_t address = parent->pages[index].virtual_address;
        uint64_t parent_entry = 0;
        uint64_t *entry = page_entry(parent, address, false);
        if (entry) parent_entry = *entry;
        uint64_t source = parent_entry & 0x000FFFFFFFFFF000ULL;
        uint64_t destination = map_page(child, address, parent->pages[index].flags);
        if (!source || !destination) return -CKLOS_ENOMEM;
        memcpy((void *)(uintptr_t)destination, (const void *)(uintptr_t)source,
               PAGE_SIZE);
    }
    child->entry = parent->entry;
    child->stack_pointer = parent->stack_pointer;
    child->brk_start = parent->brk_start;
    child->brk_current = parent->brk_current;
    child->brk_limit = parent->brk_limit;
    child->mmap_next = parent->mmap_next;
    return 0;
}

static process_file *file_for(process *slot, uint64_t fd) {
    if (fd < 3 || fd >= 11) return 0;
    process_file *file = &slot->files[fd - 3];
    return file->used ? file : 0;
}

static int32_t open_file(process *slot, const char *path, uint32_t flags) {
    if (!*path) return -CKLOS_EINVAL;
    bool writable = (flags & 3) != 0;
    bool create = (flags & 0x40) != 0;
    bool truncate = (flags & 0x200) != 0;
    uint32_t size = 0;
    uint8_t type = 0;
    int status = fs2_stat(path, &size, &type);
    if (status != 0) {
        if (!create) return -CKLOS_ENOENT;
        size = 0;
    } else if (type != FS2_TYPE_FILE) {
        return -CKLOS_EINVAL;
    }
    if (truncate) size = 0;
    if (writable && size > 512U * 1024U) return -CKLOS_ENOMEM;
    if (!writable && size > 8U * 1024U * 1024U) return -CKLOS_ENOMEM;
    uint32_t slot_index_file = 8;
    for (uint32_t index = 0; index < 8; index++) {
        if (!slot->files[index].used) {
            slot_index_file = index;
            break;
        }
    }
    if (slot_index_file == 8) return -CKLOS_EMFILE;
    process_file *file = &slot->files[slot_index_file];
    memset(file, 0, sizeof(*file));
    if (writable) {
        file->data = (uint8_t *)kmalloc(size ? size : 1);
        if (!file->data) return -CKLOS_ENOMEM;
        uint32_t actual = 0;
        if (status == 0 && size
            && fs2_read(path, file->data, size, &actual) != 0) {
            kfree(file->data);
            memset(file, 0, sizeof(*file));
            return -CKLOS_EINVAL;
        }
        file->size = actual;
    } else {
        file->size = size;
    }
    file->writable = writable;
    file->used = true;
    file->kind = FILE_KIND_FS;
    strcpy(file->path, path);
    return (int32_t)slot_index_file + 3;
}

static int32_t flush_file(process_file *file) {
    if (!file->writable || !file->dirty) return 0;
    if (fs2_write(file->path, file->data, file->size, false) != 0) {
        return -CKLOS_EINVAL;
    }
    file->dirty = false;
    return 0;
}

static int32_t close_file(process_file *file) {
    int32_t result = flush_file(file);
    release_file_reference(file);
    return result;
}

static int32_t clone_files(process *parent, process *child) {
    for (uint32_t index = 0; index < 8; index++) {
        process_file *source = &parent->files[index];
        if (!source->used) continue;
        process_file *destination = &child->files[index];
        if (source->data) {
            destination->data =
                (uint8_t *)kmalloc(source->size ? source->size : 1);
            if (!destination->data) return -CKLOS_ENOMEM;
            memcpy(destination->data, source->data, source->size);
        }
        destination->used = true;
        destination->writable = source->writable;
        destination->dirty = source->dirty;
        destination->kind = source->kind;
        destination->pipe_index = source->pipe_index;
        destination->size = source->size;
        destination->offset = source->offset;
        strcpy(destination->path, source->path);
        if (source->kind == FILE_KIND_PIPE_READ) {
            pipes[source->pipe_index].readers++;
        } else if (source->kind == FILE_KIND_PIPE_WRITE) {
            pipes[source->pipe_index].writers++;
        }
    }
    return 0;
}

static int32_t duplicate_file(process *slot, uint64_t old_fd, uint64_t new_fd) {
    process_file *source = file_for(slot, old_fd);
    if (!source || new_fd >= 11) return -CKLOS_EBADF;
    process_file *destination = &slot->files[new_fd - 3];
    if (source == destination) return (int32_t)new_fd;
    if (destination->used) release_file_reference(destination);
    *destination = *source;
    if (source->kind == FILE_KIND_FS) {
        if (source->data) {
            destination->data =
                (uint8_t *)kmalloc(source->size ? source->size : 1);
            if (!destination->data) {
                memset(destination, 0, sizeof(*destination));
                return -CKLOS_ENOMEM;
            }
            if (source->size) {
                memcpy(destination->data, source->data, source->size);
            }
        }
    } else if (source->kind == FILE_KIND_PIPE_READ) {
        pipes[source->pipe_index].readers++;
    } else if (source->kind == FILE_KIND_PIPE_WRITE) {
        pipes[source->pipe_index].writers++;
    }
    return (int32_t)new_fd;
}

static int32_t create_pipe(process *slot, int32_t *user_fds) {
    uint8_t pipe_index = PIPE_LIMIT;
    for (uint8_t index = 0; index < PIPE_LIMIT; index++) {
        if (!pipes[index].used) {
            pipe_index = index;
            break;
        }
    }
    if (pipe_index == PIPE_LIMIT) return -CKLOS_EMFILE;
    int32_t read_slot = -1;
    int32_t write_slot = -1;
    for (uint8_t index = 0; index < 8; index++) {
        if (!slot->files[index].used) {
            if (read_slot < 0) read_slot = index;
            else {
                write_slot = index;
                break;
            }
        }
    }
    if (write_slot < 0) return -CKLOS_EMFILE;
    memset(&pipes[pipe_index], 0, sizeof(pipes[pipe_index]));
    pipes[pipe_index].used = true;
    pipes[pipe_index].readers = 1;
    pipes[pipe_index].writers = 1;
    memset(&slot->files[read_slot], 0, sizeof(process_file));
    memset(&slot->files[write_slot], 0, sizeof(process_file));
    slot->files[read_slot].used = true;
    slot->files[read_slot].kind = FILE_KIND_PIPE_READ;
    slot->files[read_slot].pipe_index = pipe_index;
    slot->files[write_slot].used = true;
    slot->files[write_slot].kind = FILE_KIND_PIPE_WRITE;
    slot->files[write_slot].pipe_index = pipe_index;
    int32_t fds[2] = { read_slot + 3, write_slot + 3 };
    if (!user_write_cr3(slot->cr3, user_fds, fds, sizeof(fds))) {
        release_file_reference(&slot->files[read_slot]);
        release_file_reference(&slot->files[write_slot]);
        return -CKLOS_EFAULT;
    }
    return 0;
}

static int64_t sys_write(process *slot, uint64_t fd, const char *buffer, uint64_t count) {
    if (count > 65536) count = 65536;
    if (fd >= 3) {
        process_file *file = file_for(slot, fd);
        if (!file) return -CKLOS_EBADF;
        if (file->kind == FILE_KIND_PIPE_WRITE) {
            pipe_buffer *pipe = &pipes[file->pipe_index];
            if (!pipe->readers) return -CKLOS_EBADF;
            if (count > PIPE_CAPACITY - pipe->size) {
                return pipe->size ? 0 : -CKLOS_EAGAIN;
            }
            char chunk[256];
            uint64_t written = 0;
            while (written < count) {
                uint32_t part = count - written > sizeof(chunk)
                              ? sizeof(chunk) : (uint32_t)(count - written);
                if (!process_user_copy(chunk, buffer + written, part)) {
                    return written ? (int64_t)written : -CKLOS_EFAULT;
                }
                for (uint32_t index = 0; index < part; index++) {
                    uint32_t tail = (pipe->head + pipe->size) % PIPE_CAPACITY;
                    pipe->data[tail] = (uint8_t)chunk[index];
                    pipe->size++;
                }
                written += part;
            }
            return (int64_t)written;
        }
        if (!file->writable) return -CKLOS_EBADF;
        if (file->offset + count > 512U * 1024U) return -CKLOS_ENOMEM;
        uint32_t needed = (uint32_t)(file->offset + count);
        if (needed > file->size) {
            uint8_t *grown = (uint8_t *)kmalloc(needed ? needed : 1);
            if (!grown) return -CKLOS_ENOMEM;
            if (file->size) memcpy(grown, file->data, file->size);
            kfree(file->data);
            file->data = grown;
            file->size = needed;
        }
        if (!process_user_copy(file->data + file->offset, buffer,
                               (uint32_t)count)) {
            return -CKLOS_EFAULT;
        }
        file->offset = needed;
        file->dirty = true;
        return (int64_t)count;
    }
    if (fd != 1 && fd != 2) return -CKLOS_EBADF;
    char chunk[256];
    uint64_t written = 0;
    while (written < count) {
        uint32_t part = count - written > sizeof(chunk)
                      ? sizeof(chunk) : (uint32_t)(count - written);
        if (!process_user_copy(chunk, buffer + written, part)) {
            return -CKLOS_EFAULT;
        }
        for (uint32_t index = 0; index < part; index++) {
            console_putc(chunk[index]);
        }
        written += part;
    }
    return (int64_t)written;
}

static int64_t sys_read(process *slot, uint64_t fd, char *buffer, uint64_t count) {
    process_file *file = file_for(slot, fd);
    if (!file) return -CKLOS_EBADF;
    if (file->kind == FILE_KIND_PIPE_READ) {
        pipe_buffer *pipe = &pipes[file->pipe_index];
        if (!pipe->size) return pipe->writers ? -CKLOS_EAGAIN : 0;
        if (count > pipe->size) count = pipe->size;
        if (count > 65536) count = 65536;
        uint8_t chunk[256];
        uint64_t copied = 0;
        while (copied < count) {
            uint32_t part = count - copied > sizeof(chunk)
                          ? sizeof(chunk) : (uint32_t)(count - copied);
            for (uint32_t index = 0; index < part; index++) {
                chunk[index] = pipe->data[pipe->head];
                pipe->head = (pipe->head + 1) % PIPE_CAPACITY;
                pipe->size--;
            }
            if (!user_write_cr3(slot->cr3, buffer + copied, chunk, part)) {
                return copied ? (int64_t)copied : -CKLOS_EFAULT;
            }
            copied += part;
        }
        return (int64_t)copied;
    }
    if (file->offset >= file->size || !count) return 0;
    uint64_t available = file->size - file->offset;
    if (count > available) count = available;
    if (count > 65536) count = 65536;
    if (file->data) {
        if (!user_write_cr3(slot->cr3, buffer, file->data + file->offset,
                            (uint32_t)count)) {
            return -CKLOS_EFAULT;
        }
    } else {
        uint8_t chunk[512];
        uint32_t copied = 0;
        while (copied < count) {
            uint32_t part = count - copied > sizeof(chunk)
                          ? sizeof(chunk) : (uint32_t)(count - copied);
            uint32_t actual = 0;
            if (fs2_read_at(file->path, file->offset + copied, chunk, part,
                            &actual) != 0 || actual != part) {
                return copied ? (int64_t)copied : -CKLOS_EINVAL;
            }
            if (!user_write_cr3(slot->cr3, buffer + copied, chunk, part)) {
                return copied ? (int64_t)copied : -CKLOS_EFAULT;
            }
            copied += part;
        }
    }
    file->offset += (uint32_t)count;
    return (int64_t)count;
}

static int64_t sys_lseek(process *slot, uint64_t fd, int64_t offset, uint64_t whence) {
    process_file *file = file_for(slot, fd);
    if (!file || file->kind != FILE_KIND_FS) return -CKLOS_EBADF;
    int64_t position;
    if (whence == 0) position = offset;
    else if (whence == 1) position = (int64_t)file->offset + offset;
    else if (whence == 2) position = (int64_t)file->size + offset;
    else return -CKLOS_EINVAL;
    int64_t limit = file->data ? 512 * 1024 : 8 * 1024 * 1024;
    if (position < 0 || position > limit) return -CKLOS_EINVAL;
    file->offset = (uint32_t)position;
    return position;
}

typedef struct {
    uint64_t base;
    uint64_t length;
} user_iovec;

static int64_t sys_writev(process *slot, uint64_t fd, uint64_t vector,
                          uint64_t count) {
    if (count > 16) return -CKLOS_EINVAL;
    int64_t total = 0;
    for (uint64_t index = 0; index < count; index++) {
        user_iovec item;
        if (!process_user_copy(&item,
                               (const void *)(uintptr_t)(vector + index * sizeof(item)),
                               sizeof(item))) {
            return total ? total : -CKLOS_EFAULT;
        }
        int64_t result = sys_write(
            slot, fd, (const char *)(uintptr_t)item.base, item.length);
        if (result < 0) return total ? total : result;
        total += result;
    }
    return total;
}

static int64_t replace_with_elf(process *slot, interrupt_frame *frame,
                                const char *path) {
    uint32_t file_size;
    if (fs2_stat(path, &file_size, 0) != 0 || !file_size
        || file_size > RKS2_HEADER_SIZE + RKS2_MAX_PAYLOAD) {
        return -CKLOS_EINVAL;
    }
    uint8_t *image = (uint8_t *)kmalloc(file_size);
    if (!image) return -CKLOS_ENOMEM;
    uint32_t actual;
    if (fs2_read(path, image, file_size, &actual) != 0 || actual != file_size) {
        kfree(image);
        return -CKLOS_EINVAL;
    }
    uint32_t size = file_size;
    if (rks2_is_container(image, file_size)) {
        uint8_t *payload;
        kfree(image);
        if (!rks2_extract(path, &payload, &size)) return -CKLOS_EINVAL;
        image = payload;
    }
    if (!size || size > ELF_MAX_SIZE) {
        kfree(image);
        return -CKLOS_EINVAL;
    }
    process replacement;
    memset(&replacement, 0, sizeof(replacement));
    bool valid = create_address_space(&replacement)
              && load_elf(&replacement, image, size);
    kfree(image);
    if (!valid) {
        clear_address_space(&replacement);
        return -CKLOS_EINVAL;
    }

    int32_t pid = slot->pid;
    int32_t parent_pid = slot->parent_pid;
    process_file retained_files[8];
    memcpy(retained_files, slot->files, sizeof(retained_files));
    memset(slot->files, 0, sizeof(slot->files));
    clear_address_space(slot);
    *slot = replacement;
    memcpy(slot->files, retained_files, sizeof(retained_files));
    memcpy(slot->fx_state, default_fx_state, sizeof(default_fx_state));
    slot->pid = pid;
    slot->parent_pid = parent_pid;
    slot->state = PROCESS_RUNNING;
    slot->exit_code = 0;
    strcpy(slot->name, path);
    memset(&slot->frame, 0, sizeof(slot->frame));
    slot->frame.rip = slot->entry;
    slot->frame.rsp = slot->stack_pointer;
    slot->frame.cs = 0x1B;
    slot->frame.ss = 0x23;
    slot->frame.rflags = 0x202;
    memcpy(frame, &slot->frame, sizeof(*frame));
    write_cr3(slot->cr3);
    return 0;
}

static interrupt_frame *sys_exit(interrupt_frame *frame, int32_t code) {
    process *slot = &processes[current_process];
    slot->state = PROCESS_ZOMBIE;
    slot->exit_code = code & 0xFF;
    for (uint32_t index = 0; index < PROCESS_LIMIT; index++) {
        if (processes[index].state == PROCESS_WAITING
            && processes[index].pid == slot->parent_pid) {
            processes[index].state = PROCESS_READY;
            processes[index].frame.rax = (uint64_t)slot->pid;
            if (processes[index].wait_status_pointer) {
                int32_t status = slot->exit_code;
                user_write_cr3(processes[index].cr3,
                               (void *)(uintptr_t)processes[index].wait_status_pointer,
                               &status, sizeof(status));
            }
        }
    }
    return schedule(frame);
}

interrupt_frame *process_syscall(interrupt_frame *frame) {
    if (current_process < 0 || (frame->cs & 3) != 3) {
        frame->rax = (uint64_t)-CKLOS_EINVAL;
        return frame;
    }
    process *slot = &processes[current_process];
    switch (frame->rax) {
        case SYS_WRITE:
            frame->rax = (uint64_t)sys_write(
                slot, frame->rdi, (const char *)(uintptr_t)frame->rsi, frame->rdx);
            break;
        case SYS_WRITEV:
            frame->rax = (uint64_t)sys_writev(
                slot, frame->rdi, frame->rsi, frame->rdx);
            break;
        case SYS_GETPID:
            frame->rax = (uint64_t)(uint32_t)slot->pid;
            break;
        case SYS_YIELD:
            frame->rax = 0;
            return schedule(frame);
        case SYS_EXIT:
        case SYS_EXIT_GROUP:
            return sys_exit(frame, (int32_t)frame->rdi);
        case SYS_BRK: {
            uint64_t requested = frame->rdi;
            if (!requested) {
                frame->rax = slot->brk_current;
                break;
            }
            if (requested < slot->brk_start || requested >= USER_REGION_LIMIT) {
                frame->rax = slot->brk_current;
                break;
            }
            uint64_t needed = (requested + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            for (uint64_t address = slot->brk_limit; address < needed; address += PAGE_SIZE) {
                if (!map_page(slot, address, PAGE_USER)) {
                    frame->rax = slot->brk_current;
                    return frame;
                }
            }
            if (needed > slot->brk_limit) slot->brk_limit = needed;
            slot->brk_current = requested;
            frame->rax = requested;
            break;
        }
        case SYS_EXECVE: {
            char path[256];
            if (!process_user_read_string(path,
                                           (const char *)(uintptr_t)frame->rdi,
                                           sizeof(path))) {
                frame->rax = (uint64_t)-CKLOS_EFAULT;
                break;
            }
            frame->rax = (uint64_t)replace_with_elf(slot, frame, path);
            break;
        }
        case SYS_MMAP: {
            uint64_t length = (frame->rsi + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            bool fixed = (frame->r10 & 0x10) != 0;
            uint64_t suggested = frame->rdi
                          ? frame->rdi & ~(PAGE_SIZE - 1)
                          : slot->mmap_next;
            uint64_t base = fixed ? suggested : slot->mmap_next;
            if (!length || base + length >= USER_REGION_LIMIT
                || base + length < base) {
                frame->rax = (uint64_t)-CKLOS_ENOMEM;
                break;
            }
            for (uint64_t address = base; address < base + length; address += PAGE_SIZE) {
                if (!map_page(slot, address, PAGE_USER)) {
                    frame->rax = (uint64_t)-CKLOS_ENOMEM;
                    goto mmap_done;
                }
            }
            if (!fixed) slot->mmap_next = base + length;
            frame->rax = base;
mmap_done:
            break;
        }
        case SYS_MUNMAP:
            frame->rax = 0;
            break;
        case SYS_MPROTECT:
        case SYS_MADVISE:
            frame->rax = 0;
            break;
        case SYS_MREMAP:
            frame->rax = (uint64_t)-CKLOS_ENOSYS;
            break;
        case SYS_ARCH_PRCTL:
            if (frame->rdi == 0x1002) {
                slot->fs_base = frame->rsi;
                set_fs_base(slot->fs_base);
                frame->rax = 0;
            } else if (frame->rdi == 0x1003) {
                uint64_t base = get_fs_base();
                frame->rax = user_write_cr3(slot->cr3,
                                           (void *)(uintptr_t)frame->rsi,
                                           &base, sizeof(base))
                          ? 0 : (uint64_t)-CKLOS_EFAULT;
            } else {
                frame->rax = (uint64_t)-CKLOS_EINVAL;
            }
            break;
        case SYS_SET_TID_ADDRESS:
            frame->rax = (uint64_t)(uint32_t)slot->pid;
            break;
        case SYS_SET_ROBUST_LIST:
        case SYS_RSEQ:
            frame->rax = 0;
            break;
        case SYS_FORK: {
            process *child = free_slot();
            if (!child) {
                frame->rax = (uint64_t)-CKLOS_ENOMEM;
                break;
            }
            memset(child, 0, sizeof(*child));
            int32_t result = clone_address_space(slot, child);
            if (!result) {
                result = clone_files(slot, child);
            }
            if (result) {
                clear_address_space(child);
                memset(child, 0, sizeof(*child));
                frame->rax = (uint64_t)result;
                break;
            }
            child->state = PROCESS_READY;
            child->pid = next_pid++;
            child->parent_pid = slot->pid;
            child->exit_code = 0;
            child->fs_base = slot->fs_base;
            strcpy(child->name, slot->name);
            memcpy(&child->frame, frame, sizeof(*frame));
            save_fx(slot);
            memcpy(child->fx_state, slot->fx_state, sizeof(child->fx_state));
            child->frame.rax = 0;
            frame->rax = (uint64_t)(uint32_t)child->pid;
            break;
        }
        case SYS_READ:
            frame->rax = (uint64_t)sys_read(
                slot, frame->rdi, (char *)(uintptr_t)frame->rsi, frame->rdx);
            break;
        case SYS_LSEEK:
            frame->rax = (uint64_t)sys_lseek(
                slot, frame->rdi, (int64_t)frame->rsi, frame->rdx);
            break;
        case SYS_OPEN: {
            char path[256];
            if (!process_user_read_string(path,
                                           (const char *)(uintptr_t)frame->rdi,
                                           sizeof(path))) {
                frame->rax = (uint64_t)-CKLOS_EFAULT;
                break;
            }
            frame->rax = (uint64_t)open_file(slot, path, (uint32_t)frame->rsi);
            break;
        }
        case SYS_PIPE:
            frame->rax = (uint64_t)create_pipe(
                slot, (int32_t *)(uintptr_t)frame->rdi);
            break;
        case SYS_DUP: {
            uint64_t target = 3;
            while (target < 11 && slot->files[target - 3].used) target++;
            frame->rax = target < 11
                       ? (uint64_t)duplicate_file(slot, frame->rdi, target)
                       : (uint64_t)-CKLOS_EMFILE;
            break;
        }
        case SYS_DUP2:
            frame->rax = (uint64_t)duplicate_file(slot, frame->rdi, frame->rsi);
            break;
        case SYS_CLOSE: {
            process_file *file = file_for(slot, frame->rdi);
            frame->rax = file ? (uint64_t)close_file(file)
                              : (uint64_t)-CKLOS_EBADF;
            break;
        }
        case SYS_POLL: {
            process_file *file = file_for(slot, frame->rdi);
            if (frame->rdi <= 2) frame->rax = 1;
            else if (!file) frame->rax = 0;
            else if (file->kind == FILE_KIND_PIPE_READ) {
                pipe_buffer *pipe = &pipes[file->pipe_index];
                frame->rax = pipe->size || !pipe->writers;
            } else if (file->kind == FILE_KIND_PIPE_WRITE) {
                pipe_buffer *pipe = &pipes[file->pipe_index];
                frame->rax = pipe->readers && pipe->size < PIPE_CAPACITY;
            } else {
                frame->rax = 1;
            }
            break;
        }
        case SYS_IOCTL:
            frame->rax = (uint64_t)-CKLOS_ENOTTY;
            break;
        case SYS_WAIT4: {
            bool reaped = false;
            for (uint32_t index = 0; index < PROCESS_LIMIT; index++) {
                process *child = &processes[index];
                if (child->state == PROCESS_ZOMBIE
                    && child->parent_pid == slot->pid) {
                    int32_t status = child->exit_code;
                    frame->rax = (uint64_t)child->pid;
                    if (frame->rsi) {
                        user_write_cr3(slot->cr3,
                                      (void *)(uintptr_t)frame->rsi,
                                      &status, sizeof(status));
                    }
                    int32_t pid = child->pid;
                    clear_address_space(child);
                    memset(child, 0, sizeof(*child));
                    last_exit = pid;
                    reaped = true;
                    break;
                }
            }
            if (reaped) break;
            bool has_child = false;
            for (uint32_t index = 0; index < PROCESS_LIMIT; index++) {
                if (processes[index].state != PROCESS_UNUSED
                    && processes[index].parent_pid == slot->pid) {
                    has_child = true;
                }
            }
            if (!has_child) frame->rax = (uint64_t)-CKLOS_ECHILD;
            else {
                slot->wait_status_pointer = frame->rsi;
                slot->state = PROCESS_WAITING;
                return schedule(frame);
            }
            break;
        }
        case SYS_KILL: {
            process *target = find_pid((int32_t)frame->rdi);
            if (!target || target == slot) frame->rax = (uint64_t)-CKLOS_EINVAL;
            else {
                target->state = PROCESS_STOPPED;
                target->exit_code = 128 + (int32_t)frame->rsi;
                frame->rax = 0;
            }
            break;
        }
        default:
            frame->rax = (uint64_t)-CKLOS_ENOSYS;
            break;
    }
    return frame;
}

interrupt_frame *process_user_exception(interrupt_frame *frame) {
    if (current_process < 0) return frame;
    process *slot = &processes[current_process];
    uint64_t fault_address = 0;
    if (frame->vector == 14) {
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_address));
    }
    console_printf("\nProcess %d terminated by exception %u at 0x%08x:%08x",
                   slot->pid, (uint32_t)frame->vector,
                   (uint32_t)(frame->rip >> 32), (uint32_t)frame->rip);
    if (fault_address) {
        console_printf(" fault 0x%08x:%08x",
                       (uint32_t)(fault_address >> 32),
                       (uint32_t)fault_address);
    }
    console_putc('\n');
    slot->state = PROCESS_ZOMBIE;
    slot->exit_code = 128 + (int32_t)frame->vector;
    return schedule(frame);
}

interrupt_frame *process_timer(interrupt_frame *frame) {
    if (current_process < 0 || (frame->cs & 3) != 3) return frame;
    if (++preempt_ticks < 10) return frame;
    preempt_ticks = 0;
    return schedule(frame);
}

int process_run_elf_args(const char *path, int argc, const char *const *argv) {
    if (current_process >= 0) return -1;
    process *slot = create_from_elf(path, 0);
    if (!slot) {
        console_printf("PROCESS: cannot load ELF64 file %s.\n", path);
        return -2;
    }
    if (!install_arguments(slot, argc, argv)) {
        clear_address_space(slot);
        memset(slot, 0, sizeof(*slot));
        console_write("PROCESS: cannot construct user argument stack.\n");
        return -3;
    }
    current_process = slot_index(slot);
    slot->state = PROCESS_RUNNING;
    last_exit = -1;
    restore_fx(slot);
    set_fs_base(slot->fs_base);
    write_cr3(slot->cr3);
    console_printf("PROCESS: pid %d started %s in ring 3.\n", slot->pid, path);
    process_enter_user(slot->entry, slot->stack_pointer);
    if (current_process >= 0) write_cr3(kernel_cr3);
    current_process = -1;
    console_printf("PROCESS: returned to kernel (exit %d).\n", last_exit);
    for (uint32_t index = 0; index < PROCESS_LIMIT; index++) {
        if (processes[index].state == PROCESS_ZOMBIE
            || processes[index].state == PROCESS_READY
            || processes[index].state == PROCESS_STOPPED) {
            clear_address_space(&processes[index]);
            memset(&processes[index], 0, sizeof(processes[index]));
        }
    }
    return last_exit;
}

int process_run_elf(const char *path) {
    const char *argv[1] = { path };
    return process_run_elf_args(path, 1, argv);
}

uint32_t process_count(void) {
    uint32_t count = 0;
    for (uint32_t index = 0; index < PROCESS_LIMIT; index++) {
        if (processes[index].state != PROCESS_UNUSED) count++;
    }
    return count;
}

uint32_t process_current_pid(void) {
    return current_process >= 0 ? (uint32_t)processes[current_process].pid : 0;
}

static void set_code_descriptor(uint32_t index, uint64_t descriptor) {
    gdt[index] = descriptor;
}

void process_init(void) {
    kernel_cr3 = read_cr3();
    memset(processes, 0, sizeof(processes));
    current_process = -1;
    next_pid = 1;
    preempt_ticks = 0;

    uint64_t cr0;
    uint64_t cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~0x0CULL;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x600ULL;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    __asm__ volatile("fninit");
    uint32_t mxcsr = 0x1F80;
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
    __asm__ volatile("fxsave (%0)" : : "r"(default_fx_state) : "memory");

    gdt[0] = 0;
    set_code_descriptor(1, 0x00AF9A000000FFFFULL);
    set_code_descriptor(2, 0x00AF92000000FFFFULL);
    set_code_descriptor(3, 0x00AFFA000000FFFFULL);
    set_code_descriptor(4, 0x00CFF2000000FFFFULL);

    memset(&tss, 0, sizeof(tss));
    tss.rsp0 = (uint64_t)(uintptr_t)(exception_stack + sizeof(exception_stack));
    tss.iomap_base = sizeof(tss);
    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint32_t limit = sizeof(tss) - 1;
    gdt[5] = (uint64_t)(limit & 0xFFFF)
           | ((base & 0xFFFFFFULL) << 16)
           | (0x89ULL << 40)
           | ((uint64_t)(limit >> 16) << 48)
           | (((base >> 24) & 0xFFULL) << 56);
    gdt[6] = base >> 32;

    gdt_pointer pointer = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)(uintptr_t)gdt
    };
    process_load_gdt(&pointer);
}

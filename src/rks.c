#include "console.h"
#include "interrupts.h"
#include "mouse.h"
#include "rks.h"
#include "string.h"
#include "vfs.h"

#define RKS_LOAD_BASE 0x00300000U
#define RKS_HEADER_SIZE 32U
#define RKS_MAX_SIZE 0x00100000U
#define RKS_MAGIC 0x31534B52U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t entry;
    uint32_t code_size;
    uint32_t data_size;
    uint32_t checksum;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed)) rks_header;

typedef struct {
    void (*print)(const char *text);
    void (*put_char)(char character);
    void (*clear)(void);
    uint32_t (*ticks)(void);
    void (*sleep_ms)(uint32_t milliseconds);
    int32_t (*mouse_x)(void);
    int32_t (*mouse_y)(void);
    uint32_t (*mouse_buttons)(void);
} rks_api;

uint32_t rks_installed_count(void);

static void api_print(const char *text) {
    console_write(text);
}

static void api_put_char(char character) {
    console_putc(character);
}

static void api_clear(void) {
    console_clear();
}

static uint32_t api_ticks(void) {
    return timer_ticks();
}

static uint32_t api_mouse_buttons(void) {
    return mouse_left_down() ? 1U : 0U;
}

static const rks_api api = {
    api_print,
    api_put_char,
    api_clear,
    api_ticks,
    timer_sleep_ms,
    mouse_x,
    mouse_y,
    api_mouse_buttons
};

void rks_init(void) {
}

uint32_t rks_installed_count(void) {
    return vfs_find("HELLO.RKS") ? 1U : 0U;
}

int rks_run(const char *name) {
    const virtual_file *file = vfs_find(name);
    if (!file) {
        console_printf("RKS: %s: file not found.\n", name);
        return -1;
    }
    if (file->size < RKS_HEADER_SIZE || file->size > RKS_MAX_SIZE) {
        console_printf("RKS: %s: invalid file size.\n", file->name);
        return -2;
    }
    const rks_header *header = (const rks_header *)file->content;
    if (header->magic != RKS_MAGIC || header->version != 1
        || header->header_size != RKS_HEADER_SIZE || !(header->flags & 1)) {
        console_printf("RKS: %s: invalid RKS1 header.\n", file->name);
        return -3;
    }
    if (header->entry >= header->code_size
        || RKS_HEADER_SIZE + header->code_size > file->size
        || RKS_LOAD_BASE + header->code_size > 0x00400000U) {
        console_printf("RKS: %s: executable image is invalid.\n", file->name);
        return -4;
    }
    const uint8_t *payload = (const uint8_t *)file->content + RKS_HEADER_SIZE;
    uint32_t checksum = 0;
    for (uint32_t index = 0; index < header->code_size; index++) {
        checksum += payload[index];
    }
    if (checksum != header->checksum) {
        console_printf("RKS: %s: checksum mismatch.\n", file->name);
        return -5;
    }

    uint8_t *image = (uint8_t *)(uintptr_t)RKS_LOAD_BASE;
    memcpy(image, payload, header->code_size);
    int (*entry)(const rks_api *) =
        (int (*)(const rks_api *))(uintptr_t)(RKS_LOAD_BASE + header->entry);
    int result = entry(&api);
    return result;
}

#include "fs2.h"
#include "memory.h"
#include "string.h"
#include "unicode.h"

typedef struct {
    uint8_t magic[4];
    uint32_t count;
} __attribute__((packed)) unicode_header;

typedef struct {
    uint32_t codepoint;
    uint8_t bitmap[32];
} __attribute__((packed)) unicode_entry;

static uint8_t *font_data;
static uint32_t font_count;

bool unicode_init(const char *font_path) {
    uint32_t size;
    if (fs2_stat(font_path, &size, 0) != 0
        || size < sizeof(unicode_header)) {
        return false;
    }
    uint8_t *data = (uint8_t *)kmalloc(size);
    if (!data) return false;
    uint32_t actual;
    if (fs2_read(font_path, data, size, &actual) != 0 || actual != size) {
        kfree(data);
        return false;
    }
    unicode_header *header = (unicode_header *)data;
    if (memcmp(header->magic, "UF16", 4) != 0
        || header->count > (size - sizeof(*header)) / sizeof(unicode_entry)) {
        kfree(data);
        return false;
    }
    font_data = data;
    font_count = header->count;
    return true;
}

const uint8_t *unicode_glyph(uint32_t codepoint) {
    if (!font_data || codepoint < 0x80) return 0;
    unicode_entry *entries =
        (unicode_entry *)(font_data + sizeof(unicode_header));
    uint32_t low = 0;
    uint32_t high = font_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        if (entries[middle].codepoint < codepoint) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < font_count && entries[low].codepoint == codepoint) {
        return entries[low].bitmap;
    }
    return 0;
}

uint32_t unicode_width(uint32_t codepoint) {
    if (codepoint < 0x80) return 1;
    return unicode_glyph(codepoint) ? 2 : 1;
}

uint32_t utf8_decode(const char *text, uint32_t *consumed) {
    const uint8_t *bytes = (const uint8_t *)text;
    if (bytes[0] < 0x80) {
        *consumed = 1;
        return bytes[0];
    }
    if ((bytes[0] & 0xE0) == 0xC0 && (bytes[1] & 0xC0) == 0x80) {
        uint32_t value = ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
        if (value >= 0x80) {
            *consumed = 2;
            return value;
        }
    } else if ((bytes[0] & 0xF0) == 0xE0
               && (bytes[1] & 0xC0) == 0x80
               && (bytes[2] & 0xC0) == 0x80) {
        uint32_t value = ((bytes[0] & 0x0F) << 12)
                       | ((bytes[1] & 0x3F) << 6)
                       | (bytes[2] & 0x3F);
        if (value >= 0x800 && !(value >= 0xD800 && value <= 0xDFFF)) {
            *consumed = 3;
            return value;
        }
    } else if ((bytes[0] & 0xF8) == 0xF0
               && (bytes[1] & 0xC0) == 0x80
               && (bytes[2] & 0xC0) == 0x80
               && (bytes[3] & 0xC0) == 0x80) {
        uint32_t value = ((bytes[0] & 0x07) << 18)
                       | ((bytes[1] & 0x3F) << 12)
                       | ((bytes[2] & 0x3F) << 6)
                       | (bytes[3] & 0x3F);
        if (value >= 0x10000 && value <= 0x10FFFF) {
            *consumed = 4;
            return value;
        }
    }
    *consumed = 1;
    return 0xFFFD;
}

uint32_t utf8_encode(uint32_t codepoint, uint8_t output[4]) {
    if (codepoint < 0x80) {
        output[0] = (uint8_t)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        output[0] = (uint8_t)(0xC0 | (codepoint >> 6));
        output[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        output[0] = (uint8_t)(0xE0 | (codepoint >> 12));
        output[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        output[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    output[0] = (uint8_t)(0xF0 | (codepoint >> 18));
    output[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
    output[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
    output[3] = (uint8_t)(0x80 | (codepoint & 0x3F));
    return 4;
}

uint32_t utf8_previous(const char *text, uint32_t offset) {
    if (!offset) return 0;
    offset--;
    while (offset && (((uint8_t)text[offset] & 0xC0) == 0x80)) offset--;
    return offset;
}

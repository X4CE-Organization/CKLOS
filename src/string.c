#include "string.h"

uint64_t __umoddi3(uint64_t dividend, uint64_t divisor) {
    if (!divisor) {
        return 0;
    }
    uint64_t remainder = 0;
    for (int bit = 63; bit >= 0; bit--) {
        bool overflow = (remainder & (1ULL << 63)) != 0;
        remainder = (remainder << 1) | ((dividend >> bit) & 1ULL);
        if (overflow || remainder >= divisor) {
            remainder -= divisor;
        }
    }
    return remainder;
}

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length]) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return (uint8_t)*left - (uint8_t)*right;
}

int strncmp(const char *left, const char *right, size_t count) {
    while (count && *left && *left == *right) {
        left++;
        right++;
        count--;
    }
    if (!count) {
        return 0;
    }
    return (uint8_t)*left - (uint8_t)*right;
}

int memcmp(const void *left, const void *right, size_t count) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    while (count--) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        a++;
        b++;
    }
    return 0;
}

char *strcpy(char *destination, const char *source) {
    char *result = destination;
    while ((*destination++ = *source++)) {
    }
    return result;
}

char *strchr(const char *text, int character) {
    while (*text) {
        if ((uint8_t)*text == (uint8_t)character) {
            return (char *)text;
        }
        text++;
    }
    return character == 0 ? (char *)text : 0;
}

size_t strspn(const char *text, const char *accepted) {
    size_t count = 0;
    while (text[count]) {
        bool found = false;
        for (const char *cursor = accepted; *cursor; cursor++) {
            if (text[count] == *cursor) {
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }
        count++;
    }
    return count;
}

size_t strcspn(const char *text, const char *rejected) {
    size_t count = 0;
    while (text[count]) {
        for (const char *cursor = rejected; *cursor; cursor++) {
            if (text[count] == *cursor) {
                return count;
            }
        }
        count++;
    }
    return count;
}

void *memset(void *destination, int value, size_t count) {
    uint8_t *bytes = (uint8_t *)destination;
    while (count--) {
        *bytes++ = (uint8_t)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *to = (uint8_t *)destination;
    const uint8_t *from = (const uint8_t *)source;
    while (count--) {
        *to++ = *from++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    uint8_t *to = (uint8_t *)destination;
    const uint8_t *from = (const uint8_t *)source;
    if (to < from) {
        while (count--) {
            *to++ = *from++;
        }
    } else {
        while (count--) {
            to[count] = from[count];
        }
    }
    return destination;
}

static int char_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

int32_t parse_int(const char *text, bool *ok) {
    uint32_t value = 0;
    bool negative = false;
    uint32_t base = 10;

    *ok = false;
    while (*text == ' ') {
        text++;
    }
    if (*text == '+' || *text == '-') {
        negative = (*text == '-');
        text++;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    if (!*text) {
        return 0;
    }
    while (*text) {
        int digit = char_value(*text++);
        if (digit < 0 || (uint32_t)digit >= base) {
            return 0;
        }
        value = value * base + (uint32_t)digit;
    }
    *ok = true;
    return negative ? -(int32_t)value : (int32_t)value;
}

void uint_to_string(uint32_t value, char *buffer, uint32_t base) {
    const char *digits = "0123456789ABCDEF";
    char temporary[33];
    uint32_t count = 0;

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    while (value) {
        temporary[count++] = digits[value % base];
        value /= base;
    }
    for (uint32_t index = 0; index < count; index++) {
        buffer[index] = temporary[count - index - 1];
    }
    buffer[count] = '\0';
}

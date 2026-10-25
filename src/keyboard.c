#include "io.h"
#include "keyboard.h"
#include "network.h"
#include "serial.h"
#include "tasks.h"

static bool shift_down;
static bool caps_lock;
static bool extended_scancode;
static volatile uint16_t key_queue[64];
static volatile uint32_t key_queue_head;
static volatile uint32_t key_queue_tail;

static const char normal_map[59] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static const char shifted_map[59] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

static void queue_key(uint16_t key) {
    uint32_t next = (key_queue_head + 1) % 64;
    if (!key || next == key_queue_tail) {
        return;
    }
    key_queue[key_queue_head] = key;
    key_queue_head = next;
}

static void handle_scancode(uint8_t scancode) {
    if (scancode == 0xE0 || scancode == 0xE1) {
        extended_scancode = true;
        return;
    }
    if (extended_scancode) {
        extended_scancode = false;
        if (scancode == 0x48) {
            queue_key(KEY_UP);
        } else if (scancode == 0x50) {
            queue_key(KEY_DOWN);
        } else if (scancode == 0x4B) {
            queue_key(KEY_LEFT);
        } else if (scancode == 0x4D) {
            queue_key(KEY_RIGHT);
        }
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_down = true;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_down = false;
        return;
    }
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }
    if (scancode & 0x80 || scancode >= 59) {
        return;
    }

    char character = shift_down ? shifted_map[scancode] : normal_map[scancode];
    if (caps_lock && character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    } else if (caps_lock && shift_down && character >= 'A' && character <= 'Z') {
        character = (char)(character - 'A' + 'a');
    }
    queue_key((uint8_t)character);
}

void keyboard_irq_handler(void) {
    while (inb(0x64) & 0x01) {
        handle_scancode(inb(0x60));
    }
}

static uint16_t keyboard_queue_pop(void) {
    if (key_queue_tail == key_queue_head) {
        return 0;
    }
    uint16_t key = key_queue[key_queue_tail];
    key_queue_tail = (key_queue_tail + 1) % 64;
    return key;
}

static uint16_t serial_read_escape(void) {
    uint32_t wait = 200000;
    while (!serial_can_read() && wait--) {
        cpu_pause();
    }
    if (!serial_can_read()) {
        return KEY_ESCAPE;
    }
    char bracket = serial_read_char();
    if (bracket != '[') {
        return KEY_ESCAPE;
    }

    wait = 200000;
    while (!serial_can_read() && wait--) {
        cpu_pause();
    }
    if (!serial_can_read()) {
        return KEY_ESCAPE;
    }
    char key = serial_read_char();
    if (key == 'A') {
        return KEY_UP;
    }
    if (key == 'B') {
        return KEY_DOWN;
    }
    if (key == 'C') {
        return KEY_RIGHT;
    }
    if (key == 'D') {
        return KEY_LEFT;
    }
    return KEY_ESCAPE;
}

uint16_t input_getchar(void) {
    while (1) {
        uint16_t character = keyboard_queue_pop();
        if (character) {
            return character;
        }
        if (serial_can_read()) {
            char serial_character = serial_read_char();
            if (serial_character == '\x1B') {
                return serial_read_escape();
            }
            character = (uint8_t)serial_character;
            if (character == '\r') {
                return '\n';
            }
            if (character == 0x7F) {
                return '\b';
            }
            return character;
        }
        network_poll();
        task_yield();
        cpu_pause();
    }
}

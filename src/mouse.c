#include "io.h"
#include "mouse.h"

static int32_t cursor_x = 160;
static int32_t cursor_y = 100;
static bool buttons_down;
static bool click_pending;
static bool initialized;
static uint8_t packet[3];
static uint32_t packet_index;

static bool wait_input_clear(void) {
    for (uint32_t timeout = 0; timeout < 100000; timeout++) {
        if (!(inb(0x64) & 2)) {
            return true;
        }
        cpu_pause();
    }
    return false;
}

static bool wait_output_full(void) {
    for (uint32_t timeout = 0; timeout < 100000; timeout++) {
        if (inb(0x64) & 1) {
            return true;
        }
        cpu_pause();
    }
    return false;
}

static bool send_controller(uint8_t command) {
    if (!wait_input_clear()) {
        return false;
    }
    outb(0x64, command);
    return true;
}

static bool send_mouse(uint8_t command) {
    if (!send_controller(0xD4) || !wait_input_clear()) {
        return false;
    }
    outb(0x60, command);
    return wait_output_full() && inb(0x60) == 0xFA;
}

void mouse_init(void) {
    initialized = false;
    packet_index = 0;
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    if (!send_controller(0xA8)) {
        return;
    }
    if (!send_controller(0x20) || !wait_output_full()) {
        return;
    }
    uint8_t config = inb(0x60);
    config |= 0x02;
    config &= (uint8_t)~0x20;
    if (!send_controller(0x60) || !wait_input_clear()) {
        return;
    }
    outb(0x60, config);
    initialized = send_mouse(0xF6) && send_mouse(0xF4);
}

static int8_t movement(uint8_t low, uint8_t high_bit) {
    int16_t value = low;
    if (high_bit) {
        value -= 256;
    }
    return (int8_t)value;
}

void mouse_irq_handler(void) {
    while ((inb(0x64) & 0x21) == 0x21) {
        uint8_t value = inb(0x60);
        packet[packet_index++] = value;
        if (packet_index != 3) {
            continue;
        }
        packet_index = 0;
        if (!(packet[0] & 0x08)) {
            continue;
        }
        bool left = packet[0] & 1;
        if (left && !buttons_down) {
            click_pending = true;
        }
        buttons_down = left;
        if (!(packet[0] & 0xC0)) {
            cursor_x += movement(packet[1], packet[0] & 0x10);
            cursor_y -= movement(packet[2], packet[0] & 0x20);
            if (cursor_x < 0) {
                cursor_x = 0;
            }
            if (cursor_x > 319) {
                cursor_x = 319;
            }
            if (cursor_y < 0) {
                cursor_y = 0;
            }
            if (cursor_y > 199) {
                cursor_y = 199;
            }
        }
    }
}

int32_t mouse_x(void) { return cursor_x; }
int32_t mouse_y(void) { return cursor_y; }
bool mouse_left_down(void) { return buttons_down; }

bool mouse_take_click(void) {
    bool result = click_pending;
    click_pending = false;
    return result;
}

bool mouse_available(void) {
    return initialized;
}

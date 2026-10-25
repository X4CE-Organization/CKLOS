#ifndef CKLOS_IO_H
#define CKLOS_IO_H

#include "types.h"

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline void insw(uint16_t port, void *buffer, uint32_t count) {
    __asm__ volatile(
        "cld; rep insw"
        : "+D"(buffer), "+c"(count)
        : "d"(port)
        : "memory"
    );
}

static inline void outsw(uint16_t port, const void *buffer, uint32_t count) {
    __asm__ volatile(
        "cld; rep outsw"
        : "+S"(buffer), "+c"(count)
        : "d"(port)
        : "memory"
    );
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}

static inline void cpu_halt(void) {
    __asm__ volatile("cli; hlt");
}

static inline void cpu_reboot(void) {
    outb(0x64, 0xFE);
    while (1) {
        cpu_halt();
    }
}

#endif

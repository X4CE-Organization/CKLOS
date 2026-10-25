#include "console.h"
#include "interrupts.h"
#include "io.h"
#include "keyboard.h"
#include "network.h"
#include "string.h"
#include "tasks.h"

#define IDT_ENTRIES 256

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t reserved;
    uint8_t flags;
    uint16_t offset_high;
} __attribute__((packed)) idt_gate;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_pointer;

typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t vector;
    uint32_t error;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} interrupt_frame;

extern const uint32_t interrupt_stubs[48];

static idt_gate idt[IDT_ENTRIES];
static idt_pointer idt_descriptor;
static volatile uint32_t ticks;

static const char *exception_names[32] = {
    "Divide-by-zero", "Debug", "Non-maskable interrupt", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode", "Device unavailable",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS",
    "Segment not present", "Stack-segment fault", "General protection fault",
    "Page fault", "Reserved exception", "x87 floating-point", "Alignment check",
    "Machine check", "SIMD floating-point", "Virtualization", "Control protection",
    "Reserved exception", "Reserved exception", "Reserved exception",
    "Reserved exception", "Reserved exception", "Reserved exception",
    "Hypervisor injection", "VMM communication", "Security", "Reserved exception"
};

static void idt_set_gate(uint32_t vector, uint32_t handler) {
    idt[vector].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector = 0x08;
    idt[vector].reserved = 0;
    idt[vector].flags = 0x8E;
    idt[vector].offset_high = (uint16_t)(handler >> 16);
}

static void pic_remap(void) {
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    /* Enable the PIT and PS/2 keyboard IRQs. */
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

static void pit_init(void) {
    uint32_t divisor = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
}

void interrupt_dispatch(interrupt_frame *frame) {
    if (frame->vector >= 32 && frame->vector < 48) {
        if (frame->vector == 32) {
            timer_tick();
        } else if (frame->vector == 33) {
            keyboard_irq_handler();
        }
        if (frame->vector >= 40) {
            outb(0xA0, 0x20);
        }
        outb(0x20, 0x20);
        if (frame->vector == 32) {
            tasks_preempt();
        }
        return;
    }

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    console_write("\nCKLOS CPU exception: ");
    if (frame->vector < 32) {
        console_write(exception_names[frame->vector]);
    } else {
        console_write("unknown interrupt");
    }
    console_printf("\n  Vector: %u  Error: 0x%08x  EIP: 0x%08x\n",
                   frame->vector, frame->error, frame->eip);
    console_write("  System halted to protect kernel state.\n");
    cpu_halt();
}

void timer_tick(void) {
    ticks++;
}

uint32_t timer_ticks(void) {
    return ticks;
}

uint32_t timer_uptime_ms(void) {
    return ticks * 10;
}

void timer_sleep_ms(uint32_t milliseconds) {
    uint32_t target = ticks + milliseconds / 10;
    while ((int32_t)(ticks - target) < 0) {
        __asm__ volatile("sti; hlt");
        network_poll();
        task_yield();
    }
}

void interrupts_init(void) {
    memset(idt, 0, sizeof(idt));
    for (uint32_t vector = 0; vector < 48; vector++) {
        idt_set_gate(vector, interrupt_stubs[vector]);
    }
    idt_descriptor.limit = sizeof(idt) - 1;
    idt_descriptor.base = (uint32_t)idt;

    __asm__ volatile("lidt %0" : : "m"(idt_descriptor));
    pic_remap();
    pit_init();
    ticks = 0;
    __asm__ volatile("sti");
}

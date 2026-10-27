#include "console.h"
#include "interrupts.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "network.h"
#include "process.h"
#include "string.h"
#include "tasks.h"

#define IDT_ENTRIES 256

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_high;
    uint32_t offset_top;
    uint32_t reserved;
} __attribute__((packed)) idt_gate;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_pointer;

extern const uint64_t interrupt_stubs[49];
extern void isr128(void);

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

static void idt_set_gate(uint32_t vector, uint64_t handler, uint8_t flags) {
    idt[vector].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].flags = flags;
    idt[vector].offset_high = (uint16_t)(handler >> 16);
    idt[vector].offset_top = (uint32_t)(handler >> 32);
    idt[vector].reserved = 0;
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
    outb(0x21, 0xF8);
    outb(0xA1, 0xEF);
}

static void pit_init(void) {
    uint32_t divisor = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
}

interrupt_frame *interrupt_dispatch(interrupt_frame *frame) {
    if (frame->vector == 128) {
        return process_syscall(frame);
    }
    if (frame->vector >= 32 && frame->vector < 48) {
        if (frame->vector == 32) {
            timer_tick();
        } else if (frame->vector == 33) {
            keyboard_irq_handler();
        } else if (frame->vector == 44) {
            mouse_irq_handler();
        }
        if (frame->vector >= 40) {
            outb(0xA0, 0x20);
        }
        outb(0x20, 0x20);
        if (frame->vector == 32 && (frame->cs & 3) == 3) {
            return process_timer(frame);
        }
        return frame;
    }

    if ((frame->cs & 3) == 3 && frame->vector < 32) {
        return process_user_exception(frame);
    }

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    console_write("\nCKLOS CPU exception: ");
    if (frame->vector < 32) {
        console_write(exception_names[frame->vector]);
    } else {
        console_write("unknown interrupt");
    }
    console_printf("\n  Vector: %u  Error: 0x%08x  RIP: 0x%08x\n",
                   (uint32_t)frame->vector, (uint32_t)frame->error,
                   (uint32_t)frame->rip);
    console_write("  System halted to protect kernel state.\n");
    cpu_halt();
    return frame;
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
    __asm__ volatile("cli");
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    memset(idt, 0, sizeof(idt));
    for (uint32_t vector = 0; vector < 48; vector++) {
        idt_set_gate(vector, interrupt_stubs[vector], 0x8E);
    }
    idt_set_gate(128, (uint64_t)(uintptr_t)isr128, 0xEE);
    idt_descriptor.limit = sizeof(idt) - 1;
    idt_descriptor.base = (uint64_t)idt;

    __asm__ volatile("lidt %0" : : "m"(idt_descriptor));
    pic_remap();
    pit_init();
    ticks = 0;
}

void interrupts_enable(void) {
    __asm__ volatile("sti");
}

void interrupts_set_device_polling(bool polling) {
    if (polling) {
        outb(0x21, 0xFF);
        outb(0xA1, 0xFF);
    } else {
        outb(0x21, 0xF8);
        outb(0xA1, 0xEF);
    }
}

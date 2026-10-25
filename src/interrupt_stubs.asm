; Low-level interrupt entry points for CKLOS.

bits 32
section .text

extern interrupt_dispatch

global interrupt_stubs

%macro ISR_NO_ERROR 1
isr%1:
    cli
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERROR_CODE 1
isr%1:
    cli
    push dword %1
    jmp isr_common
%endmacro

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR_CODE 8
ISR_NO_ERROR 9
ISR_ERROR_CODE 10
ISR_ERROR_CODE 11
ISR_ERROR_CODE 12
ISR_ERROR_CODE 13
ISR_ERROR_CODE 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR_CODE 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR_CODE 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR_CODE 29
ISR_ERROR_CODE 30
ISR_NO_ERROR 31

%assign vector 32
%rep 16
ISR_NO_ERROR vector
%assign vector vector + 1
%endrep

isr_common:
    pushad
    push esp
    call interrupt_dispatch
    add esp, 4
    popad
    add esp, 8
    iretd

section .rodata
align 4
interrupt_stubs:
    dd isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    dd isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    dd isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dd isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    dd isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39
    dd isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47

section .note.GNU-stack noalloc noexec nowrite progbits

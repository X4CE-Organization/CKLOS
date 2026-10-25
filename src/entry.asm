; 32-bit CKLOS entry point. The bootloader jumps here with flat segments.

bits 32
section .text.entry
global kernel_entry
extern kernel_main
extern __bss_start
extern __bss_end

kernel_entry:
    cli
    mov esp, 0x90000
    xor ebp, ebp

    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    push dword 0
    call kernel_main
    add esp, 4

.halt:
    cli
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits

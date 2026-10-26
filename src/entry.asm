bits 64
section .text.entry
global kernel_entry
extern kernel_main
extern __bss_start
extern __bss_end

kernel_entry:
    cli
    mov rsp, 0x90000
    xor rbp, rbp
    cld
    mov rbx, rdi

    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor eax, eax
    rep stosb

    mov rdi, rbx
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits

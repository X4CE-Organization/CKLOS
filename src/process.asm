bits 64
section .text

global process_load_gdt
global process_enter_user
global process_user_return

process_load_gdt:
    cli
    lgdt [rdi]
    mov ax, 0x28
    ltr ax
    ret

process_enter_user:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rel kernel_resume_rsp], rsp

    mov r8, rdi
    mov r9, rsi
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword 0x23
    push r9
    push qword 0x202
    push qword 0x1B
    push r8
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    iretq

process_user_return:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, [rel kernel_resume_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

section .bss
align 8
kernel_resume_rsp:
    resq 1

section .note.GNU-stack noalloc noexec nowrite progbits

; Cooperative x86 task context switch.
; void task_switch(uint32_t *old_esp, uint32_t new_esp);

bits 32
section .text
global task_switch

task_switch:
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp + 20]
    mov edx, [esp + 24]
    mov [eax], esp
    mov esp, edx

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits

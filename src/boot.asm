bits 16
org 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov word [current_lba], 1
    mov word [sectors_left], 100

.load:
    mov ax, [current_lba]
    xor dx, dx
    mov cx, 18
    div cx
    inc dx
    mov [sector], dl
    xor dx, dx
    mov cx, 2
    div cx
    mov [head], dl
    mov ch, al
    mov cl, [sector]
    mov dh, [head]
    mov dl, [boot_drive]
    mov [saved_es], es
    mov [saved_bx], bx
    mov ax, 0x0201
    int 0x13
    xor dx, dx
    mov ds, dx
    mov ax, [saved_es]
    mov es, ax
    mov bx, [saved_bx]
    jc disk_error
    add bx, 512
    jnc .next
    mov ax, es
    add ax, 0x20
    mov es, ax
.next:
    inc word [current_lba]
    dec word [sectors_left]
    jnz .load

    mov ax, 0x0013
    int 0x10

    xor ax, ax
    mov es, ax
    mov di, 0x1000
    xor eax, eax
    mov cx, 0x0C00
    cld
    rep stosd
    mov dword [0x1000], 0x2003
    mov dword [0x2000], 0x3003
    mov edi, 0x3000
    mov eax, 0x83
    xor ecx, ecx
.page_table:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    inc ecx
    cmp ecx, 512
    jne .page_table

    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov eax, 0x1000
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    jmp dword 0x08:long_mode

disk_error:
    mov si, msg_error
    call bios_print
    cli
    hlt
    jmp $

bios_print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp bios_print
.done:
    ret

msg_error db "CKLOS I/O error", 13, 10, 0
boot_drive db 0
current_lba dw 1
sectors_left dw 100
sector db 0
head db 0
saved_es dw 0
saved_bx dw 0

align 8
gdt_start:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

bits 64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000
    xor rbp, rbp
    mov rax, 0x10000
    jmp rax

times 510 - ($ - $$) db 0
dw 0xAA55

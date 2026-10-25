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
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

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
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

bits 32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    xor ebp, ebp
    jmp 0x10000

times 510 - ($ - $$) db 0
dw 0xAA55

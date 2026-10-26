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
    mov word [sectors_left], 32
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
    add ax, 0x1000
    mov es, ax
.next:
    inc word [current_lba]
    dec word [sectors_left]
    jnz .load
    mov dl, [boot_drive]
    jmp 0x1000:0x0000

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

msg_error db "CKLOS stage1 error", 13, 10, 0
boot_drive db 0
current_lba dw 1
sectors_left dw 32
sector db 0
head db 0
saved_es dw 0
saved_bx dw 0

times 510 - ($ - $$) db 0
dw 0xAA55

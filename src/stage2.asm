bits 16
org 0x10000

KERNEL_SECTORS equ 256
KERNEL_SEGMENT equ 0x2000

stage2_start:
    cli
    mov ax, cs
    mov ds, ax
    xor ax, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl
    mov al, '2'
    call serial_char
    mov ax, KERNEL_SEGMENT
    mov es, ax
    xor bx, bx
    mov word [current_lba], 33
    mov word [sectors_left], KERNEL_SECTORS

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
    push ds
    push es
    push bx
    mov ax, 0x0201
    int 0x13
    pop bx
    pop es
    pop ds
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

    mov al, 'K'
    call serial_char

    ; Collect the firmware memory map before leaving real mode.  BIOS writes
    ; the entries through ES:DI, while DS remains the stage-2 data segment.
    xor ax, ax
    mov es, ax
    mov di, 0x0600
    xor ebx, ebx
    xor bp, bp
.e820:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc bp
    cmp bp, 64
    jae .e820_done
    add di, 24
    test ebx, ebx
    jnz .e820
.e820_done:
    mov [e820_count], bp
    mov dword [es:0x0500], 0x534C4B43
    mov ax, bp
    mov [es:0x0504], ax
    mov word [es:0x0506], 0
    mov dword [es:0x0508], 0x0600
    mov al, [boot_drive]
    mov [es:0x050C], al

    mov ax, 0x0013
    int 0x10
    xor ax, ax
    mov es, ax
    mov di, 0x1000
    xor eax, eax
    mov cx, 0x0C00
    cld
    rep stosd
    mov dword [es:0x1000], 0x2003
    mov dword [es:0x2000], 0x3003
    mov edi, 0x3000
    mov eax, 0x83
    xor ecx, ecx
.page_table:
    mov [es:edi], eax
    mov dword [es:edi + 4], 0
    add eax, 0x200000
    add edi, 8
    inc ecx
    cmp ecx, 512
    jne .page_table

    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al

    ; Enter flat protected mode before paging.  A 16-bit compatibility-mode
    ; code segment cannot fetch the instruction after CR0.PG is enabled.
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x18:protected_mode

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

serial_char:
    mov ah, 0x01
    xor dx, dx
    int 0x14
    ret

msg_error db "CKLOS stage2 error", 13, 10, 0
boot_drive db 0
current_lba dw 33
sectors_left dw KERNEL_SECTORS
sector db 0
head db 0
e820_count dw 0

align 8
gdt_start:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
    dq 0x00CF9A000000FFFF
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

    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov eax, 0x1000
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    jmp 0x08:long_mode

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
    mov edi, 0x0500
    mov rax, 0x20000
    jmp rax

times (32 * 512) - ($ - $$) db 0

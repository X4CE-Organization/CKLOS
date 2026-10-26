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
    mov ax, ds
    mov es, ax
    mov di, 0x7000
    mov dword [es:di], 0x32454256
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .vga_fallback

    mov word [requested_width], 1024
    mov word [requested_height], 768
    call vbe_find_mode
    test ax, ax
    jnz .vbe_set_mode
    mov word [requested_width], 800
    mov word [requested_height], 600
    call vbe_find_mode
    test ax, ax
    jnz .vbe_set_mode
    jmp .vga_fallback

.vbe_set_mode:
    mov bx, [selected_mode]
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .vga_fallback

    xor ax, ax
    mov es, ax
    mov dword [es:0x0500], 0x534C4B43
    mov ax, [e820_count]
    mov [es:0x0504], ax
    mov word [es:0x0506], 0
    mov dword [es:0x0508], 0x0600
    mov al, [boot_drive]
    mov [es:0x050C], al
    mov eax, [mode_framebuffer]
    mov [es:0x0510], eax
    movzx eax, word [mode_width]
    mov [es:0x0514], eax
    movzx eax, word [mode_height]
    mov [es:0x0518], eax
    movzx eax, word [mode_pitch]
    mov [es:0x051C], eax
    movzx eax, byte [mode_bpp]
    mov [es:0x0520], eax
    mov dword [es:0x0524], 1
    jmp .mode_ready

.vga_fallback:
    mov ax, 0x0013
    int 0x10
    xor ax, ax
    mov es, ax
    mov dword [es:0x0500], 0x534C4B43
    mov ax, [e820_count]
    mov [es:0x0504], ax
    mov word [es:0x0506], 0
    mov dword [es:0x0508], 0x0600
    mov al, [boot_drive]
    mov [es:0x050C], al
    mov dword [es:0x0510], 0x000A0000
    mov dword [es:0x0514], 320
    mov dword [es:0x0518], 200
    mov dword [es:0x051C], 320
    mov dword [es:0x0520], 8
    mov dword [es:0x0524], 0

.mode_ready:
    xor ax, ax
    mov es, ax
    mov di, 0x1000
    xor eax, eax
    mov cx, 0x1000
    cld
    rep stosd
    mov dword [es:0x1000], 0x2003
    mov dword [es:0x2000], 0x3003
    mov dword [es:0x2018], 0x4003
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

    mov edi, 0x4000
    mov eax, 0xC0000083
    xor ecx, ecx
.page_table_high:
    mov [es:edi], eax
    mov dword [es:edi + 4], 0
    add eax, 0x200000
    add edi, 8
    inc ecx
    cmp ecx, 512
    jne .page_table_high

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

vbe_find_mode:
    mov si, [es:0x700E]
    mov ax, [es:0x7010]
    mov fs, ax
.vbe_mode_loop:
    mov cx, [fs:si]
    add si, 2
    cmp cx, 0xFFFF
    je .vbe_not_found
    push si
    mov ax, ds
    mov es, ax
    mov di, 0x7200
    mov ax, 0x4F01
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .vbe_mode_loop
    mov ax, ds
    mov es, ax
    mov ax, [es:0x7200]
    and ax, 0x0081
    cmp ax, 0x0081
    jne .vbe_mode_loop
    mov ax, [es:0x7212]
    cmp ax, [requested_width]
    jne .vbe_mode_loop
    mov ax, [es:0x7214]
    cmp ax, [requested_height]
    jne .vbe_mode_loop
    cmp byte [es:0x7219], 32
    jne .vbe_mode_loop
    mov [selected_mode], cx
    mov ax, [es:0x7212]
    mov [mode_width], ax
    mov ax, [es:0x7214]
    mov [mode_height], ax
    mov ax, [es:0x7210]
    mov [mode_pitch], ax
    mov al, [es:0x7219]
    mov [mode_bpp], al
    mov eax, [es:0x7228]
    mov [mode_framebuffer], eax
    mov ax, 1
    ret
.vbe_not_found:
    xor ax, ax
    ret

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
requested_width dw 1024
requested_height dw 768
selected_mode dw 0
mode_width dw 0
mode_height dw 0
mode_pitch dw 0
mode_bpp db 0
align 4
mode_framebuffer dd 0

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

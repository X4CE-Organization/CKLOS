OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_entry)

SECTIONS
{
    . = 0x00020000;

    .text : ALIGN(16)
    {
        *(.text.entry)
        *(.text*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata*)
        *(.lrodata*)
    }

    .data : ALIGN(16)
    {
        *(.data*)
        *(.ldata*)
    }

    .bss : ALIGN(16)
    {
        __bss_start = .;
        *(.bss*)
        *(.lbss*)
        *(COMMON)
        __bss_end = .;
    }
}

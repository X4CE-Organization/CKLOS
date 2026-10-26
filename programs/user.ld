OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x0000004000000000;

    .text : ALIGN(4096)
    {
        *(.text.start)
        *(.text*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata*)
        *(.lrodata*)
    }

    .data : ALIGN(4096)
    {
        *(.data*)
        *(.ldata*)
    }

    .bss : ALIGN(4096)
    {
        *(.bss*)
        *(.lbss*)
        *(COMMON)
    }
}

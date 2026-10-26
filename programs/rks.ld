OUTPUT_FORMAT(elf64-x86-64)
ENTRY(rks_main)

SECTIONS
{
    . = 0x00300000;
    .text : ALIGN(16)
    {
        *(.text.rks_entry)
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
}

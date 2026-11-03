ENTRY(_start)

MEMORY
{
    BFLASH (rx) : ORIGIN = 0x1FFF0000, LENGTH = 3328
    RAM    (rw) : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS
{
    .text :
    {
        KEEP(*(.init))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(4);
        _etext = .;
    } >BFLASH

    /* WCH docs: data segments may not live in the BOOT area. Keep it empty. */
    .data :
    {
        *(.data .data.* .sdata .sdata.*)
    } >RAM AT>BFLASH

    .bss (NOLOAD) :
    {
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
    } >RAM

    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) }

    ASSERT(SIZEOF(.data) == 0, "error: initialized data (.data) is not allowed in the BOOT area")
    ASSERT(_etext <= 0x1FFF0000 + 3328, "error: bootloader exceeds the 3328-byte BOOT area")
}

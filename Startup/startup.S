/* CH32X033/CH32X035 DFU bootloader entry point.
 * Runs from the 3328-byte BOOT area (0x1FFF0000). The image is linked at
 * 0x1FFF0000 and the chip may start executing it either at 0x1FFF0000 or at
 * the alias 0x00000000, so this startup only uses PC-relative calls.
 */
    .section .init, "ax", @progbits
    .align 2
    .global _start
_start:
    lui     sp, 0x20005             /* sp = 0x20005000 (top of 20K SRAM)  */
    call    main
1:  j       1b
    .size   _start, . - _start

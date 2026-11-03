/*
 * CH32X033/CH32X035 DFU bootloader.
 *
 * Power-on: the chip starts in the BOOT area (0x1FFF0000).
 *   - FLASH_STATR.BOOT_MODE latched by the application -> clear, stay in DFU
 *   - user key (PC3) held high                        -> stay in DFU
 *   - application area blank (0xFF/0x00)              -> stay in DFU
 *   - otherwise -> system reset, which boots user flash at 0x0
 */
#include "dfu_bl.h"

#define KEY_SETTLE_LOOPS  20000u

void system_reset_to_user(void)
{
    PFIC_SCTLR = 0x80000000u;
    for (;;)
        ;
}

static void settle_delay(void)
{
    for (volatile uint32_t i = 0; i < KEY_SETTLE_LOOPS; i++)
        ;
}

/* SDK compatibility: the application can request the bootloader with
 *
 *     SystemReset_StartMode(Start_Mode_BOOT);   // FLASH_STATR.BOOT_MODE = 1
 *     NVIC_SystemReset();
 *
 * which makes the next system reset start in the BOOT area. Take that latch,
 * clear it (so the reset after flashing goes back to the user section) and
 * stay in DFU. */
static uint8_t bootloader_requested(void)
{
    if (!(FLASH_STATR & FLASH_STATR_BOOT_MODE))
        return 0;

    FLASH_KEYR = 0x45670123u;
    FLASH_KEYR = 0xCDEF89ABu;
    FLASH_BOOT_MODEKEYR = 0x45670123u;
    FLASH_BOOT_MODEKEYR = 0xCDEF89ABu;
    FLASH_STATR &= ~FLASH_STATR_BOOT_MODE;
    FLASH_CTLR |= (1u << 15) | (1u << 7);   /* FLOCK | LOCK */

    return 1;
}

int main(void)
{
    uint32_t app_word;

    if (bootloader_requested())
        goto dfu_mode;

    settle_delay();

    /* PC3 is a plain GPIO input after reset */
    RCC_APB2PCENR |= (1u << 4);

    if (GPIOC_INDR & KEY_PIN_MASK)
        goto dfu_mode;

    /* Only blank flash counts as "no application": a valid image may start with
     * any instruction (the WCH startup's "j handle_reset" is often assembled
     * into a 2-byte c.j, and custom startups may begin with lui/csrw/...). */
    app_word = *(volatile uint32_t *)APP_BASE;
    if (app_word == 0xFFFFFFFFu || app_word == 0x00000000u)
        goto dfu_mode;

    system_reset_to_user();

dfu_mode:
    usb_init();
    for (;;)
        usb_poll();
}

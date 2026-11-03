/*
 * CH32X033 / CH32X035 DFU 1.1 bootloader
 *
 * Target: the 3328-byte BOOT area at 0x1FFF0000.
 *   power-on  -> BOOT area runs (this firmware decides: DFU or application)
 *   app start -> system reset (PFIC->SCTLR bit31) boots user flash at 0x00000000
 *
 * USB: USBFS device controller at 0x40023400, EP0-only DFU class.
 * The USBCLK is derived from HSI 48 MHz, independently of HCLK, so HCLK is
 * left at its 8 MHz reset default (see CH32X035 DS 1.4.17 / RM fig. 3-2).
 */
#ifndef DFU_BL_H
#define DFU_BL_H

#include <stdint.h>

#define APP_BASE        0x08000000u
#define APP_END         0x0800F800u

/* 96-bit chip unique ID (3 words), used as the USB serial number. */
#define UID_BASE        0x1FFFF7E8u

/* Option-byte area: 16 bytes = RDPR/~RDPR/USER/~USER, Data0/Data1,
 * WRP0..WRP3 (each with its complement). DFU alternate setting 1. */
#define OB_BASE         0x1FFFF800u
#define OB_SIZE         16u

/* Independent watchdog. USER option bit0 = 0 selects hardware mode, i.e. the
 * IWDG runs from reset, so the bootloader must keep reloading it. */
#define IWDG_CTLR       (*(volatile uint32_t *)0x40003000u)
#define IWDG_RELOAD     0xAAAAu

static inline void wdg_feed(void)
{
    IWDG_CTLR = IWDG_RELOAD;
}

/* ---------------- USBFS registers (0x40023400) ---------------- */
#define USBFS_BASE  0x40023400u

typedef struct {
    volatile uint8_t  BASE_CTRL;
    volatile uint8_t  UDEV_CTRL;
    volatile uint8_t  INT_EN;
    volatile uint8_t  DEV_ADDR;
    volatile uint8_t  RES0;
    volatile uint8_t  MIS_ST;
    volatile uint8_t  INT_FG;
    volatile uint8_t  INT_ST;
    volatile uint16_t RX_LEN;
    volatile uint16_t RES1;
    volatile uint8_t  UEP4_1_MOD;
    volatile uint8_t  UEP2_3_MOD;
    volatile uint8_t  UEP567_MOD;
    volatile uint8_t  RES2;
    volatile uint32_t UEP0_DMA;
    volatile uint32_t UEP1_DMA;
    volatile uint32_t UEP2_DMA;
    volatile uint32_t UEP3_DMA;
    volatile uint16_t UEP0_TX_LEN;
    volatile uint16_t UEP0_CTRL_H;
    volatile uint16_t UEP1_TX_LEN;
    volatile uint16_t UEP1_CTRL_H;
    volatile uint16_t UEP2_TX_LEN;
    volatile uint16_t UEP2_CTRL_H;
    volatile uint16_t UEP3_TX_LEN;
    volatile uint16_t UEP3_CTRL_H;
} usbfs_t;

#define USBFS ((usbfs_t *)USBFS_BASE)

/* BASE_CTRL */
#define UC_DEV_PU_EN    0x20
#define UC_INT_BUSY     0x08
#define UC_DMA_EN       0x01
/* UDEV_CTRL */
#define UD_PD_DIS       0x80
#define UD_PORT_EN      0x01
/* INT_FG */
#define UIF_BUS_RST     0x01
#define UIF_TRANSFER    0x02
#define UIF_SUSPEND     0x04
/* INT_ST */
#define UIS_TOG_OK      0x40
#define UIS_TOKEN_MASK  0x30
#define UIS_TOKEN_OUT   0x00
#define UIS_TOKEN_IN    0x20
#define UIS_TOKEN_SETUP 0x30
#define UIS_ENDP_MASK   0x0F
/* UEPn_CTRL_H (low byte: TX side, bits 2..3/7: RX side) */
#define UEP_T_TOG       0x40
#define UEP_T_RES_ACK   0x00
#define UEP_T_RES_NAK   0x02
#define UEP_T_RES_STALL 0x03
#define UEP_R_TOG       0x80
#define UEP_R_RES_ACK   0x00
#define UEP_R_RES_STALL 0x0C

/* ---------------- RCC / GPIO / AFIO / FLASH ---------------- */
#define RCC_BASE      0x40021000u
#define GPIOC_BASE    0x40011000u
#define AFIO_BASE     0x40010000u
#define FLASH_BASE    0x40022000u

#define RCC_AHBPCENR  (*(volatile uint32_t *)(RCC_BASE + 0x14))
#define RCC_APB2PCENR (*(volatile uint32_t *)(RCC_BASE + 0x18))

#define GPIOC_INDR    (*(volatile uint32_t *)(GPIOC_BASE + 0x08))
#define GPIOC_CFGXR   (*(volatile uint32_t *)(GPIOC_BASE + 0x1C))
#define GPIOC_BSXR    (*(volatile uint32_t *)(GPIOC_BASE + 0x20))

#define AFIO_CTLR     (*(volatile uint32_t *)(AFIO_BASE + 0x18))

#define FLASH_KEYR     (*(volatile uint32_t *)(FLASH_BASE + 0x04))
#define FLASH_STATR    (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_CTLR     (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_ADDR     (*(volatile uint32_t *)(FLASH_BASE + 0x14))
#define FLASH_MODEKEYR (*(volatile uint32_t *)(FLASH_BASE + 0x24))
#define FLASH_BOOT_MODEKEYR (*(volatile uint32_t *)(FLASH_BASE + 0x28))
#define FLASH_OBR      (*(volatile uint32_t *)(FLASH_BASE + 0x1C))
#define FLASH_OBKEYR   (*(volatile uint32_t *)(FLASH_BASE + 0x08))

/* FLASH_STATR bits (SDK: FLASH_STATR_BOOT_MODE / SystemReset_StartMode) */
#define FLASH_STATR_BOOT_MODE (1u << 14)

/* FLASH_OBR bits (decoded option bytes, SDK: FLASH_OBR_RDPRT) */
#define FLASH_OBR_RDPRT (1u << 1)   /* read protection active */
#define FLASH_STATR_WRPRTERR (1u << 4)

#define PFIC_SCTLR     (*(volatile uint32_t *)0xE000ED10u)

/* DFU 1.1 states */
#define DFU_IDLE                2
#define DFU_DNLOAD_SYNC         3
#define DFU_DNLOAD_IDLE         5
#define DFU_MANIFEST_SYNC       6
#define DFU_MANIFEST_WAIT_RESET 8

/* Enter-DFU pin: PC3, active high (matches the application's user key). */
#define KEY_PIN_MASK  (1u << 3)

/* DFU transfer size: one control transfer carries WTRANSFER bytes, i.e.
 * WTRANSFER/64 EP0 packets. Must be a multiple of 64 and of the 256-byte
 * flash page (2048 = 32 packets = 8 pages). The block size is also learned
 * from the first block, so the host may use a smaller one (dfu-util -t). */
#define WTRANSFER    2048u

extern uint8_t ep0_buf[64];
extern uint8_t page_buf[256];

/* rest of an in-progress DFU_UPLOAD data stage (set by dfu_setup, used by
 * the EP0 IN handler in usb.c) */
extern const uint8_t *dfu_in_ptr;
extern uint16_t dfu_in_left;

void usb_init(void);
void usb_poll(void);

void dfu_init(void);
void dfu_reset(void);
uint8_t dfu_setup(uint16_t code, uint16_t value, uint16_t len,
                  uint16_t *txlen, uint16_t *out_len);
uint8_t dfu_rdp(void);
void dfu_set_alt(uint16_t alt);
void dfu_out_data(const uint8_t *data, uint16_t len);
void dfu_out_done(void);
void dfu_poll(void);

void system_reset_to_user(void);

#endif /* DFU_BL_H */

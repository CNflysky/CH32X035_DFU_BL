/* USBFS device layer: EP0-only DFU 1.1 device. */
#include "dfu_bl.h"

uint8_t ep0_buf[64] __attribute__((aligned(4)));
uint8_t page_buf[256] __attribute__((aligned(4)));

static const uint8_t dev_desc[18] = {
    18, 1,                  /* bLength, bDescriptorType */
    0x10, 0x01,             /* bcdUSB 1.10              */
    0, 0, 0,                /* class/subclass/protocol (per-interface) */
    64,                     /* bMaxPacketSize0          */
    0x86, 0x1A,             /* idVendor  = 0x1A86 (WCH) */
    0x11, 0xDF,             /* idProduct = 0xDF11       */
    0x00, 0x01,             /* bcdDevice 1.00           */
    1, 2, 3, 1              /* iMan, iProduct, iSerial, bNumConfigurations */
};

static const uint8_t cfg_desc[45] = {
    9, 2, 45, 0, 1, 1, 0, 0x80, 50,                  /* configuration        */
    9, 4, 0, 0, 0, 0xFE, 0x01, 0x02, 4,              /* alt 0: user flash    */
    9, 0x21, 0x07, 0xFF, 0x00, 0x00, 0x08, 0x10, 0x01,/* DFU functional      */
    9, 4, 0, 1, 0, 0xFE, 0x01, 0x02, 5,              /* alt 1: option bytes  */
    9, 0x21, 0x07, 0xFF, 0x00, 0x00, 0x08, 0x10, 0x01 /* DFU functional       */
};

/* String descriptors: 0 = language, 1 = manufacturer, 2 = product,
 * 3 = serial number (built from the chip UID at run time),
 * 4 / 5 = alternate setting names (shown as "name=" by dfu-util -l). */
static const uint8_t str_lang[4] = {4, 3, 0x09, 0x04};

static const uint8_t str_man[14] = {
    14, 3, 'w', 0, 'c', 0, 'h', 0, '.', 0, 'c', 0, 'n', 0
};

static const uint8_t str_prod[30] = {
    30, 3, 'd', 0, 'f', 0, 'u', 0, ' ', 0, 'b', 0, 'o', 0, 'o', 0, 't', 0,
        'l', 0, 'o', 0, 'a', 0, 'd', 0, 'e', 0, 'r', 0
};

static const uint8_t str_flash[14] = {
    14, 3, '@', 0, 'F', 0, 'l', 0, 'a', 0, 's', 0, 'h', 0
};

static const uint8_t str_opt[16] = {
    16, 3, '@', 0, 'O', 0, 'p', 0, 't', 0, 'i', 0, 'o', 0, 'n', 0
};

static uint8_t  setup_type;
static uint8_t  setup_code;
static uint16_t setup_value;
static uint16_t in_left;
static const uint8_t *in_ptr;
static uint8_t  dev_addr;
static uint8_t  configured;
static uint16_t out_left;
static uint8_t  out_active;

static void ep0_load(const uint8_t *src, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++)
        ep0_buf[i] = src[i];
}

/* String descriptor 3: the 96-bit chip UID at 0x1FFFF7E8 rendered as 24 hex
 * characters, written straight into ep0_buf. Returns the descriptor length. */
static uint8_t serial_desc(void)
{
    uint16_t *out = (uint16_t *)(ep0_buf + 2);

    ep0_buf[0] = 34;                        /* 2 + 16 * 2 (UTF-16LE) */
    ep0_buf[1] = 3;
    for (uint8_t i = 0; i < 8; i++) {       /* first 64 bits of the UID */
        uint32_t v = *(volatile const uint32_t *)(UID_BASE + ((i >> 2) << 2));
        uint8_t b = (uint8_t)(v >> (24 - ((i & 3) << 3)));
        uint8_t hi = b >> 4, lo = b & 15;
        out[2 * i]     = (uint16_t)(hi < 10 ? '0' + hi : 'a' + hi - 10);
        out[2 * i + 1] = (uint16_t)(lo < 10 ? '0' + lo : 'a' + lo - 10);
    }
    return 34;
}

static void handle_setup(void)
{
    const uint8_t *b = ep0_buf;
    uint16_t setup_len;
    uint16_t txlen = 0;
    uint16_t out_len = 0;
    uint8_t  err = 0;

    setup_type  = b[0];
    setup_code  = b[1];
    setup_value = (uint16_t)(b[2] | (b[3] << 8));
    setup_len   = (uint16_t)(b[6] | (b[7] << 8));

    in_left = 0;
    out_active = 0;
    out_left = 0;

    /* EP0: NAK IN, ACK OUT, both toggles to DATA1 */
    USBFS->UEP0_CTRL_H = UEP_T_TOG | UEP_T_RES_NAK | UEP_R_TOG | UEP_R_RES_ACK;

    if ((setup_type & 0x60) == 0x00) {
        switch (setup_code) {
        case 0x06: {                        /* GET_DESCRIPTOR */
            uint8_t dt  = (uint8_t)(setup_value >> 8);
            uint8_t idx = (uint8_t)setup_value;
            uint8_t dyn = 0;                /* already in ep0_buf */
            const uint8_t *d = 0;
            uint8_t dl = 0;
            if (dt == 1) {
                d = dev_desc; dl = sizeof(dev_desc);
            } else if (dt == 2) {
                d = cfg_desc; dl = sizeof(cfg_desc);
            } else if (dt == 3) {
                if (idx == 0)      { d = str_lang;  dl = sizeof(str_lang); }
                else if (idx == 1) { d = str_man;   dl = sizeof(str_man); }
                else if (idx == 2) { d = str_prod;  dl = sizeof(str_prod); }
                else if (idx == 4) { d = str_flash; dl = sizeof(str_flash); }
                else if (idx == 5) { d = str_opt;   dl = sizeof(str_opt); }
                else if (idx == 3) { d = ep0_buf; dl = serial_desc(); dyn = 1; }
                else err = 1;
            } else {
                err = 1;
            }
            if (!err) {
                if (setup_len > dl) setup_len = dl;
                txlen = setup_len;
                if (txlen > 64) txlen = 64;
                in_left = (uint16_t)(setup_len - txlen);
                in_ptr = d + txlen;
                if (!dyn)
                    ep0_load(d, txlen);
                /* Read-protected part: drop canUpload (byte 20 of the
                 * config descriptor) so the host refuses to read out. */
                if (dt == 2 && txlen > 20 && dfu_rdp())
                    ep0_buf[20] &= (uint8_t)~0x02u;
            }
            break;
        }
        case 0x05:                          /* SET_ADDRESS */
            dev_addr = (uint8_t)(setup_value & 0x7F);
            break;
        case 0x09:                          /* SET_CONFIGURATION */
            configured = setup_value ? 1 : 0;
            dfu_set_alt(0);
            break;
        case 0x08:                          /* GET_CONFIGURATION */
            ep0_buf[0] = configured;
            if (setup_len > 1) setup_len = 1;
            txlen = setup_len;
            break;
        case 0x00:                          /* GET_STATUS */
            ep0_buf[0] = 0;
            ep0_buf[1] = 0;
            if (setup_len > 2) setup_len = 2;
            txlen = setup_len;
            break;
        case 0x0A:                          /* GET_INTERFACE */
            ep0_buf[0] = 0;
            if (setup_len > 1) setup_len = 1;
            txlen = setup_len;
            break;
        case 0x0B:                          /* SET_INTERFACE */
            dfu_set_alt(setup_value);       /* 0 = flash, 1 = OB */
            break;
        case 0x01:                          /* CLEAR_FEATURE */
        case 0x03:                          /* SET_FEATURE   */
            break;
        default:
            err = 1;
        }
    } else if ((setup_type & 0x60) == 0x20) {   /* class request */
        err = dfu_setup(setup_code, setup_value, setup_len, &txlen, &out_len);
        if (!err && out_len) {
            out_left = out_len;
            out_active = 1;
        } else if (!err) {
            /* DFU_UPLOAD may answer with several packets: the first one is
             * already in ep0_buf, the rest is streamed from here. */
            in_ptr = dfu_in_ptr;
            in_left = dfu_in_left;
            dfu_in_left = 0;
        }
    } else {
        err = 1;
    }

    if (err) {
        USBFS->UEP0_CTRL_H = UEP_T_TOG | UEP_T_RES_STALL |
                             UEP_R_TOG | UEP_R_RES_STALL;
    } else {
        USBFS->UEP0_TX_LEN = txlen;
        USBFS->UEP0_CTRL_H = UEP_T_TOG | UEP_T_RES_ACK |
                             UEP_R_TOG | UEP_R_RES_ACK;
    }
}

static void handle_in(void)
{
    if (in_left) {
        uint16_t n = (in_left > 64) ? 64 : in_left;
        in_left = (uint16_t)(in_left - n);
        ep0_load(in_ptr, n);
        in_ptr += n;
        USBFS->UEP0_TX_LEN = n;
        USBFS->UEP0_CTRL_H ^= UEP_T_TOG;
    } else if (setup_code == 0x05 && (setup_type & 0x60) == 0 &&
               !(setup_type & 0x80)) {
        /* SET_ADDRESS takes effect after the status stage */
        USBFS->DEV_ADDR = (uint8_t)((USBFS->DEV_ADDR & 0x80) | dev_addr);
        USBFS->UEP0_CTRL_H = UEP_T_RES_NAK | UEP_R_RES_ACK;
    } else {
        /* accept the OUT status stage */
        USBFS->UEP0_CTRL_H = UEP_R_TOG | UEP_R_RES_ACK;
    }
}

static void handle_out(void)
{
    uint16_t n;

    if (!(USBFS->INT_ST & UIS_TOG_OK))
        return;
    if (!out_active)
        return;

    n = USBFS->RX_LEN;
    if (n > out_left) n = out_left;
    dfu_out_data(ep0_buf, n);
    out_left = (uint16_t)(out_left - n);

    if (out_left == 0) {
        out_active = 0;
        dfu_out_done();
        USBFS->UEP0_TX_LEN = 0;
        USBFS->UEP0_CTRL_H = UEP_T_TOG | UEP_T_RES_ACK;
    } else {
        /* Multi-packet data stage: EP0 toggles are manual, so the next
         * packet of this transfer carries the opposite toggle. */
        USBFS->UEP0_CTRL_H ^= UEP_R_TOG;
    }
}

void usb_init(void)
{
    /* AFIO + GPIOC clocks, USBFS clock */
    RCC_APB2PCENR |= (1u << 0) | (1u << 4);
    RCC_AHBPCENR  |= (1u << 12);

    /* PC16 = USB D-, floating; PC17 = USB D+, input pull-up */
    GPIOC_CFGXR = (uint32_t)((GPIOC_CFGXR & ~0xFFu) | 0x84u);
    GPIOC_BSXR  = (1u << 1);

    /* 3.3 V board: internal 1.5k D+ pull-up, V33 PHY, USB pin remap */
    AFIO_CTLR = (AFIO_CTLR & ~(0x0Cu | 0x30u | 0x40u)) | 0x0Cu | 0x40u | 0x80u;

    USBFS->BASE_CTRL = 0;
    USBFS->UEP4_1_MOD = 0;
    USBFS->UEP2_3_MOD = 0;
    USBFS->UEP0_DMA = (uint32_t)ep0_buf;
    USBFS->UEP0_TX_LEN = 0;
    USBFS->UEP0_CTRL_H = UEP_T_RES_NAK | UEP_R_RES_ACK;
    USBFS->DEV_ADDR = 0;
    USBFS->BASE_CTRL = UC_DEV_PU_EN | UC_INT_BUSY | UC_DMA_EN;
    USBFS->INT_FG = 0xFF;
    USBFS->UDEV_CTRL = UD_PD_DIS | UD_PORT_EN;
    USBFS->INT_EN = 0;              /* polled only, no vector table */

    dfu_init();
}

void usb_poll(void)
{
    uint8_t f = USBFS->INT_FG;

    wdg_feed();                         /* hardware IWDG may be running */

    if (f & UIF_TRANSFER) {
        uint8_t st = USBFS->INT_ST;
        uint8_t token = (uint8_t)(st & UIS_TOKEN_MASK);

        if (token == UIS_TOKEN_SETUP) {
            handle_setup();
        } else if ((st & UIS_ENDP_MASK) == 0) {
            if (token == UIS_TOKEN_IN)
                handle_in();
            else if (token == UIS_TOKEN_OUT)
                handle_out();
        }
        USBFS->INT_FG = UIF_TRANSFER;
    }

    if (f & UIF_BUS_RST) {
        USBFS->DEV_ADDR = 0;
        USBFS->UEP0_TX_LEN = 0;
        USBFS->UEP0_CTRL_H = UEP_T_RES_NAK | UEP_R_RES_ACK;
        in_left = 0;
        out_active = 0;
        out_left = 0;
        configured = 0;
        dfu_set_alt(0);
        USBFS->INT_FG = UIF_BUS_RST;
    }

    if (f & UIF_SUSPEND)
        USBFS->INT_FG = UIF_SUSPEND;

    dfu_poll();
}

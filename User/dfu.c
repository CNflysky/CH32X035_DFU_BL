/* DFU 1.1 class requests + streamed flash programming (256-byte pages). */
#include "dfu_bl.h"

static uint8_t dfu_state;
uint8_t dfu_alt;                            /* 0 = flash, 1 = option bytes */

const uint8_t *dfu_in_ptr;                  /* in-progress upload stage */
uint16_t dfu_in_left;

static uint8_t  ob_buf[OB_SIZE] __attribute__((aligned(4)));
static uint16_t ob_len;
static uint8_t  dfu_err;                    /* DFU bStatus to report, 0 = OK */

static uint32_t wr_addr;                    /* download write pointer */
static uint32_t rd_addr;                    /* upload read pointer    */
static uint32_t xfer_size;                  /* bytes per DFU transfer */
static uint32_t cur_page;
static uint16_t page_fill;
static uint8_t  reboot_pending;
static uint32_t reboot_count;
static uint8_t  upload_eof;

static void flash_unlock(void)
{
    FLASH_KEYR = 0x45670123u;
    FLASH_KEYR = 0xCDEF89ABu;
    FLASH_MODEKEYR = 0x45670123u;
    FLASH_MODEKEYR = 0xCDEF89ABu;
}

static void flash_wait(void)
{
    while (FLASH_STATR & 1u)
        wdg_feed();
}

/* WRPRTERR is latched by the hardware (write 1 to clear). A stale one - from an
 * earlier blocked page - would make every later write report errWRITE, so drop
 * it before touching the flash and only check what this operation raised. */
static void flash_clear_wrp(void)
{
    FLASH_STATR = FLASH_STATR_WRPRTERR;
}

static void flash_check_wrp(void)
{
    if (FLASH_STATR & FLASH_STATR_WRPRTERR)
        dfu_err = 3;                        /* bStatus = errWRITE */
}

/* 256-byte fast page erase */
static void flash_erase_page(uint32_t addr)
{
    FLASH_CTLR = (1u << 17);                /* FTER (also clears OPTER/PER) */
    FLASH_ADDR = addr;
    FLASH_CTLR = (1u << 17) | (1u << 6);    /* FTER | STRT */
    flash_wait();
    FLASH_CTLR = 0;
}

/* Fast program of n words (64 for a 256-byte page, 4 for the 16-byte option
 * page). Follows WCH's FLASH_ROM_WRITE line by line: PAGE_PG and BUF_RST are
 * separate read-modify-writes of CTLR (a single combined store does not give
 * the buffer reset time to settle and silently drops the first word of every
 * 256-byte page), and CTLR is never overwritten with a literal, so the OBWRE
 * bit set by the option-byte unlock survives into the programming step. */
static void flash_program_words(uint32_t addr, const uint32_t *w, uint32_t n)
{
    flash_clear_wrp();
    FLASH_CTLR |= (1u << 16);               /* PAGE_PG */
    FLASH_CTLR |= (1u << 19);               /* BUF_RST */
    flash_wait();
    for (volatile uint32_t d = 0; d < 16; d++)
        ;

    for (uint32_t i = 0; i < n; i++) {
        *(volatile uint32_t *)(addr + (i << 2)) = w[i];
        FLASH_CTLR |= (1u << 18);           /* BUF_LOAD */
        flash_wait();
    }

    FLASH_ADDR  = addr;
    FLASH_CTLR |= (1u << 6);                /* STRT */
    flash_wait();
    FLASH_CTLR &= ~(1u << 16);              /* PAGE_PG off */
    flash_check_wrp();
}

static void flash_program_page(uint32_t addr, const uint8_t *buf)
{
    flash_program_words(addr, (const uint32_t *)buf, 64);
}

static void flush_page(void)
{
    if (page_fill == 0)
        return;
    while (page_fill < 256)
        page_buf[page_fill++] = 0xFF;
    flash_program_page(cur_page, page_buf);
    page_fill = 0;
}

/* Option-byte page erase (OPTER). OBWRE (bit9) is set by the OBKEYR unlock and
 * is required for the erase/program to be accepted, so CTLR is only OR-ed and
 * AND-ed here - never assigned a literal, which would clear OBWRE again. */
static void flash_erase_ob(void)
{
    flash_unlock();
    FLASH_OBKEYR = 0x45670123u;
    FLASH_OBKEYR = 0xCDEF89ABu;
    FLASH_CTLR |= (1u << 5);                /* OPTER  (OBWRE preserved) */
    FLASH_CTLR |= (1u << 6);                /* STRT */
    flash_wait();
    FLASH_CTLR &= ~((1u << 5) | (1u << 6));
}

/* Option-byte page write.
 *
 * While read-protected the hardware refuses option-byte programming, but the
 * OPTER page erase still works. The stock bootloader relies on this: it writes
 * RDPR/nRDPR = 0x5AA5 with OPTPG, which only becomes possible on an erased
 * (0xFF) page - programming cannot turn 0x00 into 0x5A. So the order matters:
 *
 *   1. erase the option-byte page (allowed while protected),
 *   2. program the RDPR halfword 0x5AA5 (OBWRE|OPTPG) -> protection lifted,
 *   3. erase + fast-page program the requested 16 bytes (now unprotected).
 *
 * Only the option-byte page is touched; user flash and MER are never involved. */
static uint8_t flash_write_ob(const uint8_t *buf)
{
    if (FLASH_OBR & FLASH_OBR_RDPRT) {
        /* Read-protected: erase the page (OPTER works), then program the
         * RDPR halfword 0x5AA5 with OPTPG. From the erased 0xFF page both
         * bytes can be programmed. The hardware only latches the new
         * protection state on reset, so report back "unlocked, reboot needed"
         * and let the host send the real option bytes again afterwards. */
        flash_erase_ob();
        flash_unlock();
        FLASH_OBKEYR = 0x45670123u;
        FLASH_OBKEYR = 0xCDEF89ABu;
        FLASH_CTLR |= (1u << 9);                /* OBWRE (keep it set) */
        FLASH_CTLR |= (1u << 4);                /* OPTPG */
        *(volatile uint16_t *)OB_BASE = 0x5AA5u;
        flash_wait();
        FLASH_CTLR = 0;
        return 1;
    }
    flash_erase_ob();
    flash_program_words(OB_BASE, (const uint32_t *)buf, 4);
    return 0;
}

/* Read protection (option byte) state, from the decoded FLASH_OBR bit. */
uint8_t dfu_rdp(void)
{
    return (FLASH_OBR & FLASH_OBR_RDPRT) ? 1 : 0;
}

void dfu_init(void)
{
    dfu_state = DFU_IDLE;
    dfu_alt = 0;
    ob_len = 0;
    dfu_err = 0;
    wr_addr = APP_BASE;
    rd_addr = APP_BASE;
    xfer_size = WTRANSFER;
    cur_page = 0;
    page_fill = 0;
    reboot_pending = 0;
    reboot_count = 0;
    flash_unlock();
}

void dfu_reset(void)
{
    dfu_state = DFU_IDLE;
    page_fill = 0;
    cur_page = 0;
    upload_eof = 0;
    ob_len = 0;
    dfu_err = 0;
    /* NOTE: do not cancel a pending reboot here. dfu-util issues a USB bus
     * reset right after dfuMANIFEST_WAIT_RESET ("Resetting USB to switch
     * back to runtime mode"), which lands in this function; clearing the
     * flag there makes the device stay in DFU forever. */
}

/* Alternate setting 0 = user flash, 1 = option bytes. */
void dfu_set_alt(uint16_t alt)
{
    dfu_alt = (uint8_t)(alt & 1u);
    dfu_reset();
}

/* Zero-length DNLOAD on alternate setting 1: program the buffered 16 bytes in
 * one burst (the erased/partially written window stays a few ms), then let the
 * normal manifest path reset the chip so the new option bytes take effect.
 * No read-protection pre-check here: the option-byte area stays writable from
 * DFU in every state, and whether the hardware accepts the erase/program is
 * left to the flash controller (reported back as errWRITE). */
static uint8_t ob_write(void)
{
    if (ob_len != OB_SIZE) {
        dfu_err = 2;                        /* bStatus = errFILE */
        return 0;
    }

    /* keep the complemented bytes consistent, whatever the host sent.
     * Layout: RDPR, USER, DATA0, DATA1, WRP0..3 - every byte is followed by
     * its complement (DATA0/1 are 8-bit, not 16-bit halves). */
    for (uint32_t i = 0; i < OB_SIZE; i += 2)
        ob_buf[i + 1] = (uint8_t)~ob_buf[i];

    if (flash_write_ob(ob_buf)) {           /* unlocked: reboot, host re-sends */
        dfu_state = DFU_MANIFEST_SYNC;
        reboot_pending = 1;
        reboot_count = 200000;
        return 0;
    }

    if (dfu_err)
        return 0;
    dfu_state = DFU_MANIFEST_SYNC;
    reboot_pending = 1;
    reboot_count = 200000;          /* fallback if the host never polls status */
    return 0;
}

uint8_t dfu_setup(uint16_t code, uint16_t value, uint16_t len,
                  uint16_t *txlen, uint16_t *out_len)
{
    *txlen = 0;
    *out_len = 0;
    dfu_in_left = 0;                        /* cancel a stale upload tail */

    switch (code) {
    case 0:                                 /* DFU_DETACH */
        return 0;

    case 1:                                 /* DFU_DNLOAD */
        if (dfu_alt) {                      /* option-byte area */
            if (len == 0)
                return ob_write();
            if (value == 0)
                ob_len = 0;
            if ((uint32_t)ob_len + len > OB_SIZE)
                return 1;                   /* bigger than the OB page */
            *out_len = len;
            return 0;
        }
        if (len == 0) {                     /* end of download: manifest */
            flush_page();
            dfu_state = DFU_MANIFEST_SYNC;
            reboot_pending = 1;
            reboot_count = 200000;   /* fallback if the host never polls status */
            return 0;
        }
        if (value == 0) {                   /* first block of a download */
            wr_addr   = APP_BASE;
            xfer_size = len ? len : WTRANSFER;
            cur_page  = 0;                  /* drop any stale page      */
            page_fill = 0;
        } else {
            wr_addr = APP_BASE + (uint32_t)value * xfer_size;
        }
        *out_len = len;
        return 0;

    case 2: {                               /* DFU_UPLOAD: readback */
        uint16_t total;

        if (dfu_alt) {                      /* option-byte area */
            if (value) {
                if (upload_eof)
                    return 1;
                upload_eof = 1;
                return 0;                   /* zero-length reply ends it */
            }
            upload_eof = 0;
            for (uint16_t i = 0; i < 4; i++)
                ((uint32_t *)ep0_buf)[i] =
                    *(volatile const uint32_t *)(OB_BASE + (i << 2));
            *txlen = (len < OB_SIZE) ? len : (uint16_t)OB_SIZE;
            return 0;
        }

        if (dfu_rdp())                      /* read-protected: refuse */
            return 1;

        if (value == 0) {
            rd_addr   = APP_BASE;
            xfer_size = len ? len : WTRANSFER;
        } else {
            rd_addr = APP_BASE + (uint32_t)value * xfer_size;
        }

        if (rd_addr + 64u > APP_END) {      /* end of the mapped area */
            if (upload_eof)
                return 1;                   /* host ignored the EOF */
            upload_eof = 1;
            return 0;                       /* zero-length reply ends it */
        }
        upload_eof = 0;

        total = (len && len < WTRANSFER) ? len : (uint16_t)WTRANSFER;
        if ((uint32_t)total > APP_END - rd_addr)
            total = (uint16_t)(APP_END - rd_addr);

        for (uint16_t i = 0; i < 16; i++)   /* first packet, word reads */
            ((uint32_t *)ep0_buf)[i] =
                *(volatile const uint32_t *)(rd_addr + (i << 2));

        *txlen      = (total < 64u) ? total : 64u;
        dfu_in_ptr  = (const uint8_t *)(rd_addr + *txlen);
        dfu_in_left = (total > *txlen) ? (uint16_t)(total - *txlen) : 0;
        rd_addr += total;
        return 0;
    }

    case 3:                                 /* DFU_GETSTATUS */
        ep0_buf[0] = dfu_err;               /* bStatus (0 = OK)  */
        ep0_buf[1] = 0;                     /* bwPollTimeout     */
        ep0_buf[2] = 0;
        ep0_buf[3] = 0;
        ep0_buf[5] = 0;                     /* iString           */
        if (dfu_err) {
            dfu_state = 10;                 /* dfuERROR          */
        } else if (dfu_state == DFU_DNLOAD_SYNC) {
            dfu_state = DFU_DNLOAD_IDLE;
        } else if (dfu_state == DFU_MANIFEST_SYNC) {
            dfu_state = DFU_MANIFEST_WAIT_RESET;
            reboot_count = 20000;    /* host has state 8 now: reset shortly */
        }
        ep0_buf[4] = dfu_state;
        *txlen = 6;
        return 0;

    case 4:                                 /* DFU_CLRSTATUS */
        dfu_state = DFU_IDLE;
        return 0;

    case 5:                                 /* DFU_GETSTATE */
        ep0_buf[0] = dfu_state;
        *txlen = 1;
        return 0;

    case 6:                                 /* DFU_ABORT */
        dfu_state = DFU_IDLE;
        return 0;

    default:
        return 1;
    }
}

/* Consume one EP0 packet of a download data stage; a whole transfer may span
 * several 256-byte pages, which are erased/programmed as they fill up. */
void dfu_out_data(const uint8_t *data, uint16_t len)
{
    if (dfu_alt) {                          /* option bytes: stage 16 bytes */
        for (uint16_t i = 0; i < len && ob_len < OB_SIZE; i++)
            ob_buf[ob_len++] = data[i];
        return;
    }

    while (len) {
        uint32_t page = wr_addr & ~0xFFu;
        uint16_t off  = (uint16_t)(wr_addr & 0xFFu);
        uint16_t n;

        if (wr_addr < APP_BASE || wr_addr >= APP_END)
            return;                         /* outside the application area */

        if (page != cur_page) {
            flush_page();
            cur_page = page;
            flash_erase_page(page);
        }

        n = (uint16_t)(256u - off);
        if (n > len)
            n = len;

        for (uint16_t i = 0; i < n; i++)
            page_buf[off + i] = data[i];

        wr_addr += n;
        data    += n;
        len      = (uint16_t)(len - n);
        page_fill = (uint16_t)(off + n);

        if (page_fill >= 256) {
            flash_program_page(cur_page, page_buf);
            page_fill = 0;
        }
    }
}

void dfu_out_done(void)
{
    dfu_state = DFU_DNLOAD_SYNC;
}

void dfu_poll(void)
{
    if (!reboot_pending)
        return;
    if (reboot_count) {
        reboot_count--;
    } else {
        system_reset_to_user();
    }
}

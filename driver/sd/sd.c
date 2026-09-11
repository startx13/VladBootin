/*
 * Copyright (C) 2018 bzt (bztsrc@github.com)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "../../driver/uart/uart.h"
#include "../../driver/sd/sd.h"
#include "../../driver/gpio/gpio.h"
#include "../../driver/delays/delays.h"
#include "../../lib/printf.h"

#define EMMC_ARG2            ((volatile unsigned int*)(MMIO_BASE+0x00300000))
#define EMMC_BLKSIZECNT      ((volatile unsigned int*)(MMIO_BASE+0x00300004))
#define EMMC_ARG1            ((volatile unsigned int*)(MMIO_BASE+0x00300008))
#define EMMC_CMDTM           ((volatile unsigned int*)(MMIO_BASE+0x0030000C))
#define EMMC_RESP0           ((volatile unsigned int*)(MMIO_BASE+0x00300010))
#define EMMC_RESP1           ((volatile unsigned int*)(MMIO_BASE+0x00300014))
#define EMMC_RESP2           ((volatile unsigned int*)(MMIO_BASE+0x00300018))
#define EMMC_RESP3           ((volatile unsigned int*)(MMIO_BASE+0x0030001C))
#define EMMC_DATA            ((volatile unsigned int*)(MMIO_BASE+0x00300020))
#define EMMC_STATUS          ((volatile unsigned int*)(MMIO_BASE+0x00300024))
#define EMMC_CONTROL0        ((volatile unsigned int*)(MMIO_BASE+0x00300028))
#define EMMC_CONTROL1        ((volatile unsigned int*)(MMIO_BASE+0x0030002C))
#define EMMC_INTERRUPT       ((volatile unsigned int*)(MMIO_BASE+0x00300030))
#define EMMC_INT_MASK        ((volatile unsigned int*)(MMIO_BASE+0x00300034))
#define EMMC_INT_EN          ((volatile unsigned int*)(MMIO_BASE+0x00300038))
#define EMMC_CONTROL2        ((volatile unsigned int*)(MMIO_BASE+0x0030003C))
#define EMMC_SLOTISR_VER     ((volatile unsigned int*)(MMIO_BASE+0x003000FC))

/* command flags */
#define CMD_NEED_APP         0x80000000
#define CMD_RSPNS_48        0x00020000
#define CMD_ERRORS_MASK      0xfff9c004
#define CMD_RCA_MASK         0xffff0000

/* COMMANDs */
#define CMD_GO_IDLE          0x00000000
#define CMD_ALL_SEND_CID     0x02010000
#define CMD_SEND_REL_ADDR    0x03020000
#define CMD_CARD_SELECT      0x07030000
#define CMD_SEND_IF_COND     0x08020000
#define CMD_STOP_TRANS       0x0C030000
#define CMD_READ_SINGLE      0x11220010
#define CMD_READ_MULTI       0x12220032
#define CMD_SET_BLOCKCNT     0x17020000

/*
 * CMD55 / APP_CMD
 *
 * CMD55 has an R1 response.
 */
#define CMD_APP_CMD          0x37020000

#define CMD_SET_BUS_WIDTH    (0x06020000 | CMD_NEED_APP)
#define CMD_SEND_OP_COND     (0x29020000 | CMD_NEED_APP)
#define CMD_SEND_SCR         (0x33220010 | CMD_NEED_APP)

/* STATUS register settings */
#define SR_READ_AVAILABLE    0x00000800
#define SR_DAT_INHIBIT       0x00000002
#define SR_CMD_INHIBIT       0x00000001
#define SR_APP_CMD           0x00000020

/* INTERRUPT register settings */
#define INT_DATA_TIMEOUT     0x00100000
#define INT_CMD_TIMEOUT      0x00010000
#define INT_READ_RDY         0x00000020
#define INT_CMD_DONE         0x00000001

#define INT_ERROR_MASK       0x017E8000

/* CONTROL register settings */
#define C0_SPI_MODE_EN       0x00100000
#define C0_HCTL_HS_EN        0x00000004
#define C0_HCTL_DWITDH       0x00000002

#define C1_SRST_DATA         0x04000000
#define C1_SRST_CMD          0x02000000
#define C1_SRST_HC           0x01000000
#define C1_TOUNIT_DIS        0x000f0000
#define C1_TOUNIT_MAX        0x000e0000
#define C1_CLK_GENSEL        0x00000020
#define C1_CLK_EN            0x00000004
#define C1_CLK_STABLE        0x00000002
#define C1_CLK_INTLEN        0x00000001

/* SLOTISR_VER values */
#define HOST_SPEC_NUM        0x00ff0000
#define HOST_SPEC_NUM_SHIFT  16
#define HOST_SPEC_V3         2
#define HOST_SPEC_V2         1
#define HOST_SPEC_V1         0

/* SCR flags */
#define SCR_SD_BUS_WIDTH_4   0x00000400
#define SCR_SUPP_SET_BLKCNT  0x02000000
#define SCR_SUPP_CCS         0x00000001

#define ACMD41_VOLTAGE       0x00ff8000
#define ACMD41_CMD_COMPLETE  0x80000000
#define ACMD41_CMD_CCS       0x40000000
#define ACMD41_ARG_HC        0x51ff8000

unsigned long sd_scr[2], sd_ocr, sd_rca, sd_err, sd_hv;

/**
 * Wait for data or command ready
 */
int sd_status(unsigned int mask)
{
    unsigned int cnt = 10000;

    while((*EMMC_STATUS & mask) &&
          !(*EMMC_INTERRUPT & INT_ERROR_MASK) &&
          cnt--)
    {
        wait_cycles(10);
    }

    if(*EMMC_INTERRUPT & INT_ERROR_MASK)
        return SD_ERROR;

    if(cnt == 0)
        return SD_TIMEOUT;

    return SD_OK;
}

/**
 * Wait for interrupt
 */
int sd_int(unsigned int mask)
{
    unsigned int r;
    unsigned int m = mask | INT_ERROR_MASK;
    unsigned int cnt = 10000;

    while(!(*EMMC_INTERRUPT & m) && cnt--)
        wait_cycles(10);

    r = *EMMC_INTERRUPT;

    if(cnt == 0)
    {
        *EMMC_INTERRUPT = r;
        return SD_TIMEOUT;
    }

    if(r & (INT_CMD_TIMEOUT | INT_DATA_TIMEOUT))
    {
        *EMMC_INTERRUPT = r;
        return SD_TIMEOUT;
    }

    if(r & INT_ERROR_MASK)
    {
        *EMMC_INTERRUPT = r;
        return SD_ERROR;
    }

    *EMMC_INTERRUPT = mask;

    return SD_OK;
}

/**
 * Send a command
 */
int sd_cmd(unsigned int code, unsigned int arg)
{
    unsigned int r;

    sd_err = SD_OK;

    /*
     * Application-specific command.
     */
    if(code & CMD_NEED_APP)
    {
        r = sd_cmd(CMD_APP_CMD, sd_rca);

        if(sd_err)
            return 0;

        /*
         * CMD55 must return R1 with APP_CMD set
         * when a valid RCA exists.
         */
        if(sd_rca && !(r & SR_APP_CMD))
        {
            uart_puts("\r\n[SD] ERROR: failed to send SD APP command");
            sd_err = SD_ERROR;
            return 0;
        }

        code &= ~CMD_NEED_APP;
    }

    /*
     * Wait until command path is free.
     */
    if(sd_status(SR_CMD_INHIBIT))
    {
        uart_puts("\r\n[SD] ERROR: EMMC command inhibit timeout");
        uart_puts("\r\n[SD] STATUS: ");
        uart_hex(*EMMC_STATUS);
        uart_puts("\r\n[SD] INTERRUPT: ");
        uart_hex(*EMMC_INTERRUPT);

        sd_err = SD_TIMEOUT;
        return 0;
    }

    //uart_puts("\r\n[SD] Sending command ");
    //uart_hex(code);
    //uart_puts(" arg ");
    //uart_hex(arg);

    /*
     * Clear pending interrupts.
     *
     * INTERRUPT bits are cleared by writing 1.
     */
    *EMMC_INTERRUPT = *EMMC_INTERRUPT;

    *EMMC_ARG1 = arg;
    *EMMC_CMDTM = code;

    /*
     * CMD0 has no response.
     */
    if(code == CMD_GO_IDLE)
        return 0;

    /*
     * Wait for command completion.
     */
    if(sd_int(INT_CMD_DONE))
    {
        uart_puts("\r\n[SD] ERROR: failed to send EMMC command");
        uart_puts("\r\n[SD] STATUS: ");
        uart_hex(*EMMC_STATUS);
        uart_puts("\r\n[SD] INTERRUPT: ");
        uart_hex(*EMMC_INTERRUPT);

        sd_err = SD_ERROR;
        return 0;
    }

    r = *EMMC_RESP0;

    /*
     * CMD55 returns R1.
     *
     * Return the complete response so the caller can test
     * the APP_CMD bit.
     */
    if(code == CMD_APP_CMD)
        return r;

    if(code == CMD_SEND_OP_COND)
        return r;

    if(code == CMD_SEND_IF_COND)
    {
        if(r != arg)
        {
            sd_err = SD_ERROR;
            return 0;
        }

        return SD_OK;
    }

    if(code == CMD_ALL_SEND_CID)
    {
        return r;
    }

    if(code == CMD_SEND_REL_ADDR)
    {
        sd_err =
            (((r & 0x1fff)) |
             ((r & 0x2000) << 6) |
             ((r & 0x4000) << 8) |
             ((r & 0x8000) << 8)) & CMD_ERRORS_MASK;

        return r & CMD_RCA_MASK;
    }

    /*
     * For normal R1 commands return the error bits.
     */
    return r & CMD_ERRORS_MASK;
}

/**
 * Read a block from SD card and return the number of bytes read.
 * Returns 0 on error.
 */
int sd_readblock(unsigned int lba, unsigned int *buffer, unsigned int num)
{
    int r;
    unsigned int c = 0;
    unsigned int d;

    if(num < 1)
        num = 1;

    //uart_puts("\r\n[SD] sd_readblock lba ");
    //uart_hex(lba);
    //uart_puts(" num ");
    //uart_hex(num);

    /*
     * Data path must be idle.
     */
    if(sd_status(SR_DAT_INHIBIT))
    {
        uart_puts("\r\n[SD] ERROR: EMMC data inhibit timeout");
        uart_puts("\r\n[SD] STATUS: ");
        uart_hex(*EMMC_STATUS);
        uart_puts("\r\n[SD] INTERRUPT: ");
        uart_hex(*EMMC_INTERRUPT);

        sd_err = SD_TIMEOUT;
        return 0;
    }

    /*
     * SDHC/SDXC: block addressing.
     */
    if(sd_scr[0] & SCR_SUPP_CCS)
    {
        if(num > 1 && (sd_scr[0] & SCR_SUPP_SET_BLKCNT))
        {
            sd_cmd(CMD_SET_BLOCKCNT, num);

            if(sd_err)
                return 0;
        }

        *EMMC_BLKSIZECNT = (num << 16) | 512;

        sd_cmd(num == 1 ? CMD_READ_SINGLE : CMD_READ_MULTI, lba);

        if(sd_err)
            return 0;
    }
    else
    {
        /*
         * SDSC: byte addressing.
         */
        *EMMC_BLKSIZECNT = (1 << 16) | 512;
    }

    while(c < num)
    {
        /*
         * SDSC requires one command per block.
         */
        if(!(sd_scr[0] & SCR_SUPP_CCS))
        {
            sd_cmd(CMD_READ_SINGLE, (lba + c) * 512);

            if(sd_err)
                return 0;
        }

        /*
         * Wait until data is available.
         */
        r = sd_int(INT_READ_RDY);

        if(r)
        {
            uart_puts("\r\n[SD] ERROR: data read timeout");
            uart_puts("\r\n[SD] STATUS: ");
            uart_hex(*EMMC_STATUS);
            uart_puts("\r\n[SD] INTERRUPT: ");
            uart_hex(*EMMC_INTERRUPT);

            sd_err = r;
            return 0;
        }

        /*
         * Read one 512-byte block.
         */
        for(d = 0; d < 128; d++)
            buffer[d] = *EMMC_DATA;

        buffer += 128;
        c++;
    }

    /*
     * Wait until the data transaction is completely finished.
     */
    if(sd_status(SR_DAT_INHIBIT))
    {
        uart_puts("\r\n[SD] ERROR: data transfer did not finish");
        uart_puts("\r\n[SD] STATUS: ");
        uart_hex(*EMMC_STATUS);
        uart_puts("\r\n[SD] INTERRUPT: ");
        uart_hex(*EMMC_INTERRUPT);

        sd_err = SD_TIMEOUT;
        return 0;
    }

    /*
     * Stop a multiple-block transfer when required.
     */
    if(num > 1 &&
       !(sd_scr[0] & SCR_SUPP_SET_BLKCNT) &&
       (sd_scr[0] & SCR_SUPP_CCS))
    {
        sd_cmd(CMD_STOP_TRANS, 0);

        if(sd_err)
            return 0;
    }

    return (sd_err != SD_OK || c != num) ? 0 : num * 512;
}

/**
 * Set SD clock to frequency in Hz.
 */
int sd_clk(unsigned int f)
{
    unsigned int d, c = 41666666 / f, x, s = 32, h = 0;
    int cnt = 100000;

    while((*EMMC_STATUS & (SR_CMD_INHIBIT | SR_DAT_INHIBIT)) && cnt--)
        wait_cycles(10);

    if(cnt <= 0)
    {
        uart_puts("\r\n[SD] ERROR: timeout waiting for inhibit flag");
        uart_puts("\r\n[SD] STATUS: ");
        uart_hex(*EMMC_STATUS);

        return SD_ERROR;
    }

    *EMMC_CONTROL1 &= ~C1_CLK_EN;
    wait_msec(10);

    x = c - 1;

    if(!x)
    {
        s = 0;
    }
    else
    {
        if(!(x & 0xffff0000u))
        {
            x <<= 16;
            s -= 16;
        }

        if(!(x & 0xff000000u))
        {
            x <<= 8;
            s -= 8;
        }

        if(!(x & 0xf0000000u))
        {
            x <<= 4;
            s -= 4;
        }

        if(!(x & 0xc0000000u))
        {
            x <<= 2;
            s -= 2;
        }

        if(!(x & 0x80000000u))
        {
            x <<= 1;
            s -= 1;
        }

        if(s > 0)
            s--;

        if(s > 7)
            s = 7;
    }

    if(sd_hv > HOST_SPEC_V2)
        d = c;
    else
        d = (1 << s);

    if(d <= 2)
    {
        d = 2;
        s = 0;
    }

    uart_puts("\r\n[SD] sd_clk divisor ");
    uart_hex(d);
    uart_puts(", shift ");
    uart_hex(s);

    if(sd_hv > HOST_SPEC_V2)
        h = (d & 0x300) >> 2;

    d = (((d & 0x0ff) << 8) | h);

    *EMMC_CONTROL1 = (*EMMC_CONTROL1 & 0xffff003f) | d;

    wait_msec(10);

    *EMMC_CONTROL1 |= C1_CLK_EN;

    wait_msec(10);

    cnt = 10000;

    while(!(*EMMC_CONTROL1 & C1_CLK_STABLE) && cnt--)
        wait_msec(10);

    if(cnt <= 0)
    {
        uart_puts("\r\n[SD] ERROR: failed to get stable clock");
        return SD_ERROR;
    }

    return SD_OK;
}

/**
 * Initialize EMMC to read SDHC card.
 */
unsigned short int init = 0;

int sd_init()
{
    if(!init)
    {
        long r, cnt, ccs = 0;

        /* GPIO_CD */
        r = *GPFSEL4;
        r &= ~(7 << (7 * 3));
        *GPFSEL4 = r;

        *GPPUD = 2;
        wait_cycles(150);
        *GPPUDCLK1 = (1 << 15);
        wait_cycles(150);
        *GPPUD = 0;
        *GPPUDCLK1 = 0;

        r = *GPHEN1;
        r |= 1 << 15;
        *GPHEN1 = r;

        /* GPIO_CLK, GPIO_CMD - MODIFICATO PER RPI 2 REALE */
        r = *GPFSEL4;
        r |= (7 << (8 * 3)) | (7 << (9 * 3));
        *GPFSEL4 = r;

        // 1. Applica il Pull-Up (GPPUD = 2) SOLO al comando (Pin 49 -> bit 17)
        *GPPUD = 2; 
        wait_cycles(150);
        *GPPUDCLK1 = (1 << 17); 
        wait_cycles(150);
        *GPPUD = 0;
        *GPPUDCLK1 = 0;

        // 2. Disabilita il Pull (GPPUD = 0) SOLO sul clock (Pin 48 -> bit 16)
        *GPPUD = 0; 
        wait_cycles(150);
        *GPPUDCLK1 = (1 << 16);
        wait_cycles(150);
        *GPPUD = 0;
        *GPPUDCLK1 = 0;


        /* GPIO_DAT0, GPIO_DAT1, GPIO_DAT2, GPIO_DAT3 */
        r = *GPFSEL5;
        r |= (7 << (0 * 3)) |
             (7 << (1 * 3)) |
             (7 << (2 * 3)) |
             (7 << (3 * 3));
        *GPFSEL5 = r;

        *GPPUD = 2;
        wait_cycles(150);

        *GPPUDCLK1 =
            (1 << 18) |
            (1 << 19) |
            (1 << 20) |
            (1 << 21);

        wait_cycles(150);

        *GPPUD = 0;
        *GPPUDCLK1 = 0;

        sd_hv =
            (*EMMC_SLOTISR_VER & HOST_SPEC_NUM) >>
            HOST_SPEC_NUM_SHIFT;

        uart_puts("\r\n[SD] EMMC: GPIO set up");

        /*
         * Reset EMMC host controller.
         */
        *EMMC_CONTROL0 = 0;
        *EMMC_CONTROL1 |= C1_SRST_HC;

        cnt = 10000;

        do
        {
            wait_msec(10);
        }
        while((*EMMC_CONTROL1 & C1_SRST_HC) && cnt--);

        if(cnt <= 0)
        {
            uart_puts("\r\n[SD] ERROR: failed to reset EMMC");
            return SD_ERROR;
        }

        uart_puts("\r\n[SD] EMMC: reset OK");

        *EMMC_CONTROL1 |= C1_CLK_INTLEN | C1_TOUNIT_MAX;

        wait_msec(10);

        /*
         * Initialization clock.
         */
        if((r = sd_clk(400000)))
            return r;

        *EMMC_INT_EN   = 0xffffffff;
        *EMMC_INT_MASK = 0xffffffff;

        sd_scr[0] = 0;
        sd_scr[1] = 0;
        sd_rca = 0;
        sd_err = SD_OK;

        /*
         * CMD0.
         */
        sd_cmd(CMD_GO_IDLE, 0);

        if(sd_err)
            return sd_err;

        /*
         * CMD8.
         */
        sd_cmd(CMD_SEND_IF_COND, 0x000001AA);

        if(sd_err)
            return sd_err;

        /*
         * ACMD41.
         */
        cnt = 1000;
        r = 0;

        while(!(r & ACMD41_CMD_COMPLETE) && cnt--)
        {
            wait_msec(10);

            r = sd_cmd(CMD_SEND_OP_COND, ACMD41_ARG_HC);

            uart_puts("\r\n[SD] EMMC: CMD_SEND_OP_COND returned ");

            if(r & ACMD41_CMD_COMPLETE)
                uart_puts("\r\n[SD] COMPLETE ");

            if(r & ACMD41_VOLTAGE)
                uart_puts("\r\n[SD] VOLTAGE ");

            if(r & ACMD41_CMD_CCS)
            {
                uart_puts("\r\n[SD] CCS ");
                ccs = SCR_SUPP_CCS;
            }

            //uart_hex(r >> 32); 64-bit
            uart_hex(r);

            if(sd_err != SD_TIMEOUT && sd_err != SD_OK)
            {
                uart_puts("\r\n[SD] ERROR: EMMC ACMD41 returned error");
                return sd_err;
            }
        }

        if(!(r & ACMD41_CMD_COMPLETE) || !cnt)
            return SD_TIMEOUT;

        if(!(r & ACMD41_VOLTAGE))
            return SD_ERROR;

        /*
         * CMD2.
         */
        sd_cmd(CMD_ALL_SEND_CID, 0);

        if(sd_err)
            return sd_err;

        /*
         * CMD3.
         */
        sd_rca = sd_cmd(CMD_SEND_REL_ADDR, 0);

        uart_puts("\r\n[SD] EMMC: CMD_SEND_REL_ADDR returned ");
        //uart_hex(sd_rca >> 32); 64-bit
        uart_hex(sd_rca);

        if(sd_err)
            return sd_err;

         /*
         * 4 MHz (abbassato per stabilità su hardware reale).
         */
        if((r = sd_clk(4000000)))
            return r;

        /*
         * Select card.
         */
        sd_cmd(CMD_CARD_SELECT, sd_rca);

        if(sd_err)
            return sd_err;

        /*
         * Make sure data path is idle before SCR.
         */
        if(sd_status(SR_DAT_INHIBIT))
        {
            uart_puts("\r\n[SD] ERROR: DAT_INHIBIT before SCR");
            uart_puts("\r\n[SD] STATUS: ");
            uart_hex(*EMMC_STATUS);
            uart_puts("\r\n[SD] INTERRUPT: ");
            uart_hex(*EMMC_INTERRUPT);

            return SD_TIMEOUT;
        }

        /*
         * SCR = 8 bytes.
         */
        *EMMC_BLKSIZECNT = (1 << 16) | 8;

        /*
         * ACMD51 / SEND_SCR.
         */
        sd_cmd(CMD_SEND_SCR, 0);

        if(sd_err)
            return sd_err;

        if(sd_int(INT_READ_RDY))
        {
            uart_puts("\r\n[SD] ERROR: SCR read ready timeout");
            uart_puts("\r\n[SD] STATUS: ");
            uart_hex(*EMMC_STATUS);
            uart_puts("\r\n[SD] INTERRUPT: ");
            uart_hex(*EMMC_INTERRUPT);

            return SD_TIMEOUT;
        }

        /*
         * Read the two 32-bit words of the SCR.
         */
        r = 0;
        cnt = 10000;

        while(r < 2 && cnt--)
        {
            if(*EMMC_STATUS & SR_READ_AVAILABLE)
            {
                sd_scr[r++] = *EMMC_DATA;
            }
            else
            {
                wait_msec(1);
            }
        }

        if(r != 2)
        {
            uart_puts("\r\n[SD] ERROR: SCR read timeout");
            uart_puts("\r\n[SD] STATUS: ");
            uart_hex(*EMMC_STATUS);
            uart_puts("\r\n[SD] INTERRUPT: ");
            uart_hex(*EMMC_INTERRUPT);

            return SD_TIMEOUT;
        }

        /*
         * Wait until SCR transfer is completely finished.
         */
        if(sd_status(SR_DAT_INHIBIT))
        {
            uart_puts("\r\n[SD] ERROR: SCR transfer did not finish");
            uart_puts("\r\n[SD] STATUS: ");
            uart_hex(*EMMC_STATUS);
            uart_puts("\r\n[SD] INTERRUPT: ");
            uart_hex(*EMMC_INTERRUPT);

            return SD_TIMEOUT;
        }

        /*
         * Enable 4-bit bus.
         */
        if(sd_scr[0] & SCR_SD_BUS_WIDTH_4)
        {
            sd_cmd(CMD_SET_BUS_WIDTH, sd_rca | 2);

            if(sd_err)
                return sd_err;

            *EMMC_CONTROL0 |= C0_HCTL_DWITDH;

            if(sd_status(SR_DAT_INHIBIT))
            {
                uart_puts("\r\n[SD] ERROR: DAT_INHIBIT after ACMD6");
                uart_puts("\r\n[SD] STATUS: ");
                uart_hex(*EMMC_STATUS);
                uart_puts("\r\n[SD] INTERRUPT: ");
                uart_hex(*EMMC_INTERRUPT);

                return SD_TIMEOUT;
            }
        }

        /*
         * Card capabilities.
         */
        uart_puts("\r\n[SD] EMMC: supports ");

        if(sd_scr[0] & SCR_SUPP_SET_BLKCNT)
            uart_puts("\r\n[SD] SET_BLKCNT ");

        if(ccs)
            uart_puts("\r\n[SD] CCS ");

        /*
         * Software CCS flag.
         */
        sd_scr[0] &= ~SCR_SUPP_CCS;
        sd_scr[0] |= ccs;

        init = 1;
    }

    return SD_OK;
}
#include <stddef.h>
#include <stdint.h>
#include "uart.h"
#include "../../lib/printf.h"

#ifndef GPFSEL1
#define GPFSEL1 0x3F200004 // GPIO Function Select 1 - RPi 2 / BCM2836
#endif

// Memory-Mapped I/O output
static inline void mmio_write(uint32_t reg, uint32_t data)
{
    *(volatile uint32_t*)reg = data;
}

// Memory-Mapped I/O input
static inline uint32_t mmio_read(uint32_t reg)
{
    return *(volatile uint32_t*)reg;
}

// Delay loop
static inline void delay(int32_t count)
{
    asm volatile(
        "__delay_%=: subs %[count], %[count], #1; "
        "bne __delay_%=\n"
        : "=r"(count)
        : [count]"0"(count)
        : "cc"
    );
}

/*
 * Mailbox message:
 *
 * Set PL011 UART clock to 48 MHz.
 *
 * Mailbox property tag:
 *   0x38002 = Set Clock Rate
 *   clock ID 2 = UART / PL011
 *
 * With UARTCLK = 48 MHz and:
 *
 *   IBRD = 1
 *   FBRD = 0
 *
 * the resulting baudrate is:
 *
 *   48,000,000 / (16 * 1) = 3,000,000 baud
 */
volatile unsigned int __attribute__((aligned(16))) uart_mbox[9] = {
    9 * 4,       // Buffer size
    0,            // Request
    0x38002,      // Set Clock Rate
    12,           // Value buffer size
    8,            // Request/response size
    2,            // Clock ID: PL011 UART
    48000000,     // Clock rate: 48 MHz
    0,            // End tag
    0
};

void uart_init()
{
    /*
     * 1. Set PL011 UART clock to 48 MHz through mailbox.
     */

    // Wait until mailbox can accept a write
    while (mmio_read(MBOX_STATUS) & 0x80000000) {
    }

    // Send buffer address on property channel 8
    mmio_write(
        MBOX_WRITE,
        ((uint32_t)((void*)&uart_mbox) & ~0xF) | 8
    );

    // Wait for mailbox response
    while (1) {
        while (mmio_read(MBOX_STATUS) & 0x40000000) {
        }

        uint32_t response = mmio_read(MBOX_READ);

        if ((response & 0xF) == 8)
            break;
    }

    /*
     * 2. Disable UART while configuring it.
     */
    mmio_write(UART0_CR, 0x00000000);

    /*
     * 3. Configure GPIO 14/15 for ALT0 = PL011 UART.
     */

    uint32_t selector = mmio_read(GPFSEL1);

    // Clear GPIO14 and GPIO15 function bits
    selector &= ~((7 << 12) | (7 << 15));

    // ALT0 = 100b
    selector |= (4 << 12) | (4 << 15);

    mmio_write(GPFSEL1, selector);

    /*
     * 4. Disable GPIO pull-up/down.
     */

    mmio_write(GPPUD, 0x00000000);

    delay(150);

    mmio_write(
        GPPUDCLK0,
        (1 << 14) | (1 << 15)
    );

    delay(150);

    mmio_write(GPPUDCLK0, 0x00000000);

    /*
     * 5. Clear pending interrupts and UART errors.
     */

    mmio_write(UART0_ICR, 0x7FF);
    mmio_write(UART0_RSRECR, 0x0);

    /*
     * 6. Configure baudrate.
     *
     * UARTCLK = 48 MHz
     *
     * Baud = UARTCLK / (16 * divisor)
     *
     * divisor = IBRD + FBRD / 64
     *
     * IBRD = 1
     * FBRD = 0
     *
     * Baud = 48,000,000 / 16
     *      = 3,000,000 baud
     */

    mmio_write(UART0_IBRD, 2);
    mmio_write(UART0_FBRD, 0);

    /*
     * 7. Configure:
     *
     *   8 data bits
     *   No parity
     *   1 stop bit
     *   FIFO enabled
     */

    mmio_write(
        UART0_LCRH,
        (1 << 4) |  // FEN
        (1 << 5) |  // WLEN bit 0
        (1 << 6)    // WLEN bit 1
    );

    /*
     * 8. Mask interrupts.
     */

    mmio_write(
        UART0_IMSC,
        (1 << 1)  |  // CTSMIM
        (1 << 4)  |  // RXIM
        (1 << 5)  |  // TXIM
        (1 << 6)  |  // RTIM
        (1 << 7)  |  // FEIM
        (1 << 8)  |  // PEIM
        (1 << 9)  |  // BEIM
        (1 << 10)    // OEIM
    );

    /*
     * 9. Enable UART, TX and RX.
     */

    mmio_write(
        UART0_CR,
        (1 << 0) |   // UARTEN
        (1 << 8) |   // TXE
        (1 << 9)     // RXE
    );
}

void uart_putc(unsigned char c)
{
    // Wait until TX FIFO is not full
    while (mmio_read(UART0_FR) & (1 << 5)) {
    }

    mmio_write(UART0_DR, c);
}

unsigned char uart_getc()
{
    // Wait until RX FIFO contains data
    while (mmio_read(UART0_FR) & (1 << 4)) {
    }

    // Clear accumulated receive errors
    mmio_write(UART0_RSRECR, 0);

    return (unsigned char)(mmio_read(UART0_DR) & 0xFF);
}

void uart_puts(const char* str)
{
    for (size_t i = 0; str[i] != '\0'; i++) {
        uart_putc((unsigned char)str[i]);
    }
}

void uart_hex(unsigned int d)
{
    unsigned int n;
    int c;

    for (c = 28; c >= 0; c -= 4) {
        n = (d >> c) & 0xF;

        n += n > 9 ? 0x37 : 0x30;

        uart_putc(n);
    }
}

void uart_dump(unsigned int ptr, unsigned int size)
{
    printf(
        "\r\nDumping memory at 0x%x size 0x%x\r\n",
        ptr,
        size - ptr
    );

    unsigned long a, b, d;
    unsigned char c;

    for (a = ptr; a < size; a += 16) {

        uart_hex(a);
        uart_puts(": ");

        /*
         * Hexadecimal dump
         */
        for (b = 0; b < 16; b++) {

            c = *((unsigned char*)(a + b));

            d = (unsigned int)c;
            d >>= 4;
            d &= 0xF;
            d += d > 9 ? 0x37 : 0x30;
            uart_putc(d);

            d = (unsigned int)c;
            d &= 0xF;
            d += d > 9 ? 0x37 : 0x30;
            uart_putc(d);

            uart_putc(' ');

            if (b % 4 == 3)
                uart_putc(' ');
        }

        /*
         * ASCII dump
         */
        for (b = 0; b < 16; b++) {

            c = *((unsigned char*)(a + b));

            uart_putc(
                c < 32 || c >= 127 ? '.' : c
            );
        }

        uart_putc('\r');
        uart_putc('\n');
    }
}
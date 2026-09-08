#include <stddef.h>
#include <stdint.h>

static inline void mmio_write(uint32_t reg, uint32_t data);
static inline uint32_t mmio_read(uint32_t reg);
static inline void delay(int32_t count);
void uart_putc(unsigned char c);
unsigned char uart_getc();
void uart_puts(const char* str);
void uart_hex(unsigned int d);

enum
{
    MMIO_BASE = 0x3F000000,
    // The offsets for reach register.
    GPIO_BASE = (MMIO_BASE + 0x200000),
 
    // Controls actuation of pull up/down to ALL GPIO pins.
    GPPUD = (GPIO_BASE + 0x94),
 
    // Controls actuation of pull up/down for specific GPIO pin.
    GPPUDCLK0 = (GPIO_BASE + 0x98),
 
    // The base address for UART.
    UART0_BASE = 0x3F201000,//(GPIO_BASE + 0x1000), // for raspi4 0xFE201000, raspi2 & 3 0x3F201000, and 0x20201000 for raspi1
    //UART0_BASE = 0x3f215040,
    // The offsets for reach register for the UART.
    UART0_DR     = (UART0_BASE + 0x00),
    UART0_RSRECR = (UART0_BASE + 0x04),
    UART0_FR     = (UART0_BASE + 0x18),
    UART0_ILPR   = (UART0_BASE + 0x20),
    UART0_IBRD   = (UART0_BASE + 0x24),
    UART0_FBRD   = (UART0_BASE + 0x28),
    UART0_LCRH   = (UART0_BASE + 0x2C),
    UART0_CR     = (UART0_BASE + 0x30),
    UART0_IFLS   = (UART0_BASE + 0x34),
    UART0_IMSC   = (UART0_BASE + 0x38),
    UART0_RIS    = (UART0_BASE + 0x3C),
    UART0_MIS    = (UART0_BASE + 0x40),
    UART0_ICR    = (UART0_BASE + 0x44),
    UART0_DMACR  = (UART0_BASE + 0x48),
    UART0_ITCR   = (UART0_BASE + 0x80),
    UART0_ITIP   = (UART0_BASE + 0x84),
    UART0_ITOP   = (UART0_BASE + 0x88),
    UART0_TDR    = (UART0_BASE + 0x8C),
 
    // The offsets for Mailbox registers
    MBOX_BASE    = (MMIO_BASE + 0xB880),
    MBOX_READ    = (MBOX_BASE + 0x00),
    MBOX_STATUS  = (MBOX_BASE + 0x18),
    MBOX_WRITE   = (MBOX_BASE + 0x20)
};

void uart_init(void);
void uart_dump(unsigned int ptr, unsigned int size);

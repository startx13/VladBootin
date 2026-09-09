#include <stddef.h>
#include <stdint.h>
#include "uart.h"
#include "printf.h"

#ifndef GPFSEL1
#define GPFSEL1 0x3F200004 // Indirizzo fisico GPIO Function Select 1 su RPi 2 (BCM2836)
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
 
// Loop <delay> times in a way that the compiler won't optimize away
static inline void delay(int32_t count)
{
    asm volatile("__delay_%=: subs %[count], %[count], #1; bne __delay_%=\n"
         : "=r"(count): [count]"0"(count) : "cc");
}
 
 
// A Mailbox message with set clock rate of PL011 to 3MHz tag
volatile unsigned int  __attribute__((aligned(16))) uart_mbox[9] = {
    9*4, 0, 0x38002, 12, 8, 2, 3000000, 0 ,0
};
 
void uart_init()
{
    mmio_write(UART0_CR, 0x00000000);

    // Legge il registro GPFSEL1
    uint32_t selector = mmio_read(GPFSEL1);
    selector &= ~((7 << 12) | (7 << 15)); // Reset bit per GPIO 14 e 15
    selector |= (4 << 12) | (4 << 15);   // Imposta ALT0 (100b) per UART0
    mmio_write(GPFSEL1, selector);

    // Gestione Pull-up/down
    mmio_write(GPPUD, 0x00000000);
    delay(150);
    mmio_write(GPPUDCLK0, (1 << 14) | (1 << 15));
    delay(150);
    mmio_write(GPPUDCLK0, 0x00000000);

    // Pulisci interrupt pendenti ed errori
    mmio_write(UART0_ICR, 0x7FF);
    mmio_write(UART0_RSRECR, 0x0);

    // Configurazione Baud Rate per Clock UART a 3MHz (RPi 2 Default)
    mmio_write(UART0_IBRD, 1);
    mmio_write(UART0_FBRD, 40);

    // 8N1 e abilita FIFO
    mmio_write(UART0_LCRH, (1 << 4) | (1 << 5) | (1 << 6));

    // Maschera interrupt
    mmio_write(UART0_IMSC, (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6) |
                           (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10));

    // Abilita UART, TX e RX
    mmio_write(UART0_CR, (1 << 0) | (1 << 8) | (1 << 9));
}
 
void uart_putc(unsigned char c)
{
    // Wait for UART to become ready to transmit.
    while ( mmio_read(UART0_FR) & (1 << 5) ) { }
    mmio_write(UART0_DR, c);
}
 
unsigned char uart_getc()
{
    // Attendi che la RX FIFO contenga dati
    while ( mmio_read(UART0_FR) & (1 << 4) ) { }
    
    // Pulisci errori di linea accumulati
    mmio_write(UART0_RSRECR, 0);
    
    return (unsigned char)(mmio_read(UART0_DR) & 0xFF);
}
 
void uart_puts(const char* str)
{
    for (size_t i = 0; str[i] != '\0'; i ++)
        uart_putc((unsigned char)str[i]);
}
 
void uart_hex(unsigned int d) {
    unsigned int n;
    int c;
    for(c=28;c>=0;c-=4) {
        // get highest tetrad
        n=(d>>c)&0xF;
        // 0-9 => '0'-'9', 10-15 => 'A'-'F'
        n+=n>9?0x37:0x30;
        uart_putc(n);
    }
}

void uart_dump(unsigned int ptr,unsigned int size)
{
    printf("\r\nDumping memory at 0x%x size 0x%x\r\n",ptr,size-ptr);
    unsigned long a,b,d;
    unsigned char c;
    for(a=ptr;a<size;a+=16)
    {
        uart_hex(a); uart_puts(": ");
        for(b=0;b<16;b++) {
            c=*((unsigned char*)(a+b));
            d=(unsigned int)c;d>>=4;d&=0xF;d+=d>9?0x37:0x30;uart_putc(d);
            d=(unsigned int)c;d&=0xF;d+=d>9?0x37:0x30;uart_putc(d);
            uart_putc(' ');
            if(b%4==3)
                uart_putc(' ');
        }
        for(b=0;b<16;b++) {
            c=*((unsigned char*)(a+b));
            uart_putc(c<32||c>=127?'.':c);
        }
        uart_putc('\r');
        uart_putc('\n');
    }
}

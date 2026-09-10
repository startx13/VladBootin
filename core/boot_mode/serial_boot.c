#include "serial_boot.h"
#include "../../defs.h"
#include "../../driver/uart/uart.h"
#include "../../lib/printf.h"

extern void halt(void);
extern void prepare_boot(void);
extern void clean_dcache_range(unsigned int start, unsigned int end);
extern void linux_boot(uint32_t kernel_entry,uint32_t machine_type,uint32_t dtb);

void bootFromSerial(char *args, unsigned int args_len)
{
    #define ACK              0x06
    #define NAK              0x15
    #define SYN              0x16
    #define READY            0x11
    #define CHUNK_SIZE       256

    printf("\r\nBooting from serial.....");
    printf("\r\nWaiting for console to attach......");

    uart_putc(READY);

    unsigned char c;

    do
    {
        c = uart_getc();

        if(c == 0x03 || c == 0x1B)   // Ctrl+C / ESC
            return;
    }
    while(c != SYN);

    uart_putc(ACK);

    /* =========================
     * KERNEL SIZE
     * ========================= */

    unsigned int kernel_size = 0;

    kernel_size |= ((unsigned int)uart_getc());
    kernel_size |= ((unsigned int)uart_getc()) << 8;
    kernel_size |= ((unsigned int)uart_getc()) << 16;
    kernel_size |= ((unsigned int)uart_getc()) << 24;

    /*if(kernel_size == 0x52455751 && DEBUG)
    {
        uart_putc(ACK);
        return;
    }*/

    if(kernel_size == 0)
    {
        uart_putc(NAK);
        return;
    }

    unsigned char *kernel_start =
        (unsigned char *)KERNEL_LOAD_ADDR;

    unsigned char *kernel_end =
        kernel_start + kernel_size;

    /*
     * Il kernel deve rimanere nella RAM.
     *
     * Non controlliamo DTB qui perché il DTB
     * può tranquillamente stare PRIMA del kernel.
     */
    if(kernel_end < kernel_start ||
       kernel_end > (unsigned char *)MEMORY_END)
    {
        uart_putc(NAK);
        return;
    }

    uart_putc(ACK);

    /* =========================
     * RECEIVE KERNEL
     * ========================= */

    unsigned char *kernel = kernel_start;
    unsigned int remaining = kernel_size;

    while(remaining > 0)
    {
        unsigned int chunk = remaining;

        if(chunk > CHUNK_SIZE)
            chunk = CHUNK_SIZE;

        for(unsigned int i = 0; i < chunk; i++)
        {
            *kernel++ = uart_getc();
        }

        remaining -= chunk;

        uart_putc(ACK);
    }

    /* =========================
     * DTB SIZE
     * ========================= */

    unsigned int dtb_size = 0;

    dtb_size |= ((unsigned int)uart_getc());
    dtb_size |= ((unsigned int)uart_getc()) << 8;
    dtb_size |= ((unsigned int)uart_getc()) << 16;
    dtb_size |= ((unsigned int)uart_getc()) << 24;

    if(dtb_size == 0 ||
       dtb_size > MAX_DTB_SIZE)
    {
        uart_putc(NAK);
        return;
    }

    unsigned char *dtb_start =
        (unsigned char *)DTB_LOAD_ADDR;

    unsigned char *dtb_end =
        dtb_start + dtb_size;

    /*
     * Controllo overflow + limite RAM.
     */
    if(dtb_end < dtb_start ||
       dtb_end > (unsigned char *)MEMORY_END)
    {
        uart_putc(NAK);
        return;
    }

    /*
     * Controllo generico di overlap.
     *
     * Gli intervalli sono:
     *
     *   [DTB_START,    DTB_END)
     *   [KERNEL_START, KERNEL_END)
     *
     * Possono essere in qualsiasi ordine.
     */
    if((dtb_start < kernel_end) &&
       (kernel_start < dtb_end))
    {
        uart_putc(NAK);
        return;
    }

    uart_putc(ACK);

    /* =========================
     * RECEIVE DTB
     * ========================= */

    unsigned char *dtb = dtb_start;
    remaining = dtb_size;

    while(remaining > 0)
    {
        unsigned int chunk = remaining;

        if(chunk > CHUNK_SIZE)
            chunk = CHUNK_SIZE;

        for(unsigned int i = 0; i < chunk; i++)
        {
            *dtb++ = uart_getc();
        }

        remaining -= chunk;

        uart_putc(ACK);
    }

    /* =========================
     * VALIDATE DTB MAGIC
     * ========================= */

    if(dtb_start[0] != 0xd0 ||
       dtb_start[1] != 0x0d ||
       dtb_start[2] != 0xfe ||
       dtb_start[3] != 0xed)
    {
        uart_putc(NAK);
        return;
    }

    printf("\r\nKernel received: %u bytes.", kernel_size);
    printf("\r\nDTB received: %u bytes.", dtb_size);
    printf("\r\nPreparing CPU for Linux...");

    prepare_boot();

    linux_boot(
        (uint32_t)kernel_start,
        0xFFFFFFFF,
        DTB_LOAD_ADDR
    );

    while(1){}
}

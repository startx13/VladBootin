#include "serial_boot.h"
#include "../../defs.h"
#include "../../driver/uart/uart.h"
#include "../../lib/printf.h"
#include "../../driver/delays/delays.h"
#include "../../lib/crypto/sha256.h"
#include "../../lib/fdt.h"

extern void halt(void);
extern void prepare_boot(void);
extern void clean_dcache_range(unsigned int start, unsigned int end);
extern void linux_boot(uint32_t kernel_entry,uint32_t machine_type,uint32_t dtb);

int bootFromSerial(char *args, unsigned int args_len)
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

    unsigned int t = 0;

    // Gira per un massimo di 5 secondi (5000 millisecondi)
    while (t < 10000) 
    {
        // Controlla se la seriale ha ricevuto un carattere (NON è bloccante)
        if (uart_is_readable()) 
        {
            c = uart_getc(); // Ora è sicuro leggerlo perché sappiamo che c'è
            
            if (c == 0x03 || c == 0x1B)   // Se premi Ctrl+C o ESC esce subito
                return RET_EXIT;
            
            if (c == SYN)                 // Se il loader invia il SYN interrompe il timer
                break;
        }

        wait_msec(1); // Aspetta 1 millisecondo reale
        t++;          // Incrementa il contatore del tempo passato
    }

    // Se sono passati 5 secondi e NON è arrivato il carattere SYN dal loader
    if (c != SYN) 
    {
        printf("\r\n[SERIAL] No Loader attached within 5s.");
        return RET_TIMEOUT; // Esce e dice al main di andare al boot da file
    }

    uart_putc(ACK);

    /* =========================
     * KERNEL SIZE
     * ========================= */

    unsigned int kernel_size = 0;

    kernel_size |= ((unsigned int)uart_getc());
    kernel_size |= ((unsigned int)uart_getc()) << 8;
    kernel_size |= ((unsigned int)uart_getc()) << 16;
    kernel_size |= ((unsigned int)uart_getc()) << 24;

    if(kernel_size == 0)
    {
        uart_putc(NAK);
        return RET_ERR;
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
        return RET_ERR;
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
        return RET_ERR;
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
        return RET_ERR;
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
        return RET_ERR;
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
        return RET_ERR;
    }

    printf("\r\nKernel received: %u bytes.", kernel_size);
    printf("\r\nDTB received: %u bytes.", dtb_size);
    //Kernel SHA256
    uint8_t kernel_digest[32];
    sha256((void *)KERNEL_LOAD_ADDR, (size_t)kernel_size, kernel_digest);
    print_sha256(kernel_digest);

    //DTB SHA256
    uint8_t dtb_digest[32];
    sha256((void *)DTB_LOAD_ADDR, (size_t)dtb_size, dtb_digest);
    print_sha256(dtb_digest);

    fdt_update_bootargs((void *)DTB_LOAD_ADDR, "root=/dev/mmcblk0p2 rw rootwait console=ttyS1,115200");
    fdt_update_memory((void *)DTB_LOAD_ADDR, 0x3c000000);

    printf("\r\nPreparing CPU for Linux...");

    prepare_boot();

    linux_boot(
        (uint32_t)kernel_start,
        0xFFFFFFFF,
        DTB_LOAD_ADDR
    );

    while(1){}
}

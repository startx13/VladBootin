#include "../lib/printf.h"

extern void enable_irq();
extern void enable_fiq();
extern void disable_irq();
extern void disable_fiq();
extern void enable_mmu();
extern void loop();

#define MMUTABLEBASE 0x00004000
#define MMIO_BASE 0x3F000000

struct block
{
    unsigned int size;
    unsigned int ptr;
    unsigned int next;
};

extern unsigned char __data;
extern unsigned char __end;

unsigned int nextptr = (unsigned int)&__end;
unsigned short int first_clean = 0;

//Clear memory before use
void mem_clear(void *ptr, unsigned int size)
{
    unsigned char *p = (unsigned char *)ptr;
    for(unsigned int i = 0; i < size; i++) {
        p[i] = 0;
    }
}

void reset_mem()
{
    mem_clear((void *)&__end, nextptr - (unsigned int)&__end);
    nextptr = (unsigned int)&__end;
}

unsigned int last_block()
{
    return nextptr;
}

unsigned int alloc(unsigned int size)
{
    // Align size to 16 bytes
    size = (size + 15) & ~15;

    // Ensure base pointer is aligned to 16 bytes
    unsigned int ptr = (nextptr + 15) & ~15;

    if((ptr + size) > MMIO_BASE)
    {
        printf("\r\n[MM] Not enough space after __end");
        return 0;
    }
    nextptr = ptr + size;
    mem_clear((void *)ptr, size);
    return ptr;
}

static unsigned short int mmu_started = 0;

void interrupt_init();

void fiq_interrupt_handler(void)
{
    printf("\r\n[MM] FIQ Interrupt");
}

void undefined_instruction_interrupt_handler(void)
{
    printf("\r\n[MM] Undefined Instruction Interrupt");
}

void bad_interrupt_handler(void)
{
    printf("\r\n[MM] Bad Interrupt");
}

void data_abort_interrupt_handler(void)
{
    unsigned int dfsr, dfar, fault_pc;
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm__ volatile("sub %0, lr, #8" : "=r"(fault_pc));
    printf("\r\n[MM] Data abort interrupt at PC: 0x%x, DFAR: 0x%x, DFSR: 0x%x\r\n", fault_pc, dfar, dfsr);
    while(1) {
        __asm__ volatile("wfe");
    }
}

void irq_interrupt_handler_c(void)
{
    printf("\r\n[MM] IRQ Interrupt");
}

void interrupt_init() {
  // enabling IRQ interrupts
  //enable_irq();
  //enable_fiq();
}

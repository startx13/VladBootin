#include "printf.h"

extern void enable_irq();
extern void enable_fiq();
extern void disable_irq();
extern void disable_fiq();
extern void enable_mmu();
extern void loop();

#define MMUTABLEBASE 0x00004000

struct block
{
    unsigned int size;
    unsigned int ptr;
    unsigned int next;
};

extern unsigned char __data;
extern unsigned char __end;

unsigned int *nextptr = &__end;
unsigned int MMIO_BASE = 0x3F000000;

unsigned int *alloc(unsigned int size)
{
    unsigned int *ptr = nextptr;
    if(ptr + size > MMIO_BASE)
    {
        printf("\r\nNot enough space after __end");
        return NULL;
    }
    nextptr = ptr+size;
    return ptr;
}

static unsigned short int mmu_started = 0;

void init_mmu()
{
    if(!mmu_started)
    {
        interrupt_init();
        enable_mmu(MMUTABLEBASE, ~0);
        mmu_started = 1;
    }
}

void fiq_interrupt_handler(void)
{
  uart_puts("FIQ Interrupt !\r\n");
}

void undefined_instruction_interrupt_handler(void)
{
  uart_puts("Undefined Instruction Interrupt !\r\n");
}

void bad_interrupt_handler(void)
{
  uart_puts("Bad Interrupt !\r\n");
}

void data_abort_interrupt_handler(void)
{
  uart_puts("Data abort interrupt !\r\n");
}

void irq_interrupt_handler_c(void)
{
  uart_puts("IRQ Interrupt !\r\n");
}

void interrupt_init() {
  // enabling IRQ interrupts
  enable_irq();
  enable_fiq();
}

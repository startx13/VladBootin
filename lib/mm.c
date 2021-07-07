#include "printf.h"

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

unsigned int nextptr = &__end;
unsigned short int first_clean = 0;

//Clear memory before use
void mem_clear(unsigned int *ptr, unsigned int size)
{
    for(unsigned int i = ptr; i<= ptr+size;i++)
    {
        *ptr = 0;
    }
}

void reset_mem()
{
    //printf("\r\n[MM] Reseting memory");
    mem_clear(&__end,nextptr);
    nextptr = &__end;
}

unsigned int last_block()
{
    return nextptr;
}

unsigned int alloc(unsigned int size)
{
    
    unsigned int ptr = nextptr;
    //printf("\r\n[MM] Allocating 0x%x bytes at 0x%x MMIO_BASE AT 0x%x",size,ptr,MMIO_BASE);
    if((ptr + size) > MMIO_BASE)
    {
        printf("\r\n[MM] Not enough space after __end");
        return NULL;
    }
    nextptr = ptr+size;
    //printf("\r\n[MM] Cleaning block");
    mem_clear(ptr,size);
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
  printf("\r\n[MM] Data abort interrupt");
}

void irq_interrupt_handler_c(void)
{
    printf("\r\n[MM] IRQ Interrupt");
}

void interrupt_init() {
  // enabling IRQ interrupts
  enable_irq();
  enable_fiq();
}

ENTRY(_start)

SECTIONS
{
    /* Starts at LOADER_ADDR */
    . = 0x00008000;
    __start = .;
    __text_start = .;
    .text :
    {
        KEEP(*(.text.boot))
        *(.text)
    }
    . = ALIGN(4096); /* align to page size */
    __text_end = .;
 
    __rodata_start = .;
    .rodata :
    {
        *(.rodata)
    }
    . = ALIGN(4096); /* align to page size */
    __rodata_end = .;
 
    . = ALIGN(4);
    __sig_start = .;
    .sig :
    {
        KEEP(*(.sig))
    }
    __sig_end = .;

    __data_start = .;
    .data :
    {
        *(.data)
    }
    . = ALIGN(4096); /* align to page size */
    __data_end = .;
 
    __bss_start = .;
    .bss :
    {
        bss = .;
        *(.bss)
    }
    . = ALIGN(4096); /* align to page size */

    __bss_end = .;
    __bss_size = __bss_end - __bss_start;

     . = ALIGN(4096);

    /* Stacks (growing downwards) */
    . = . + 0x40000;  /* 256 KB SVC stack */
    __svc_stack = .;

    . = . + 0x10000;  /* 64 KB IRQ stack */
    __irq_stack = .;

    . = . + 0x10000;  /* 64 KB FIQ stack */
    __fiq_stack = .;

    . = . + 0x10000;  /* 64 KB ABT stack */
    __abt_stack = .;

    . = . + 0x10000;  /* 64 KB UND stack */
    __und_stack = .;

    . = ALIGN(4096);
    __end = .;
}

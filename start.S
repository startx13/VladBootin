.section ".text.boot"

.globl _start
.globl enable_irq
.globl enable_fiq
.globl disable_irq
.globl disable_fiq
.globl halt
.globl set_reg
.globl enable_mmu
.globl read_cp15
.globl reset_cp15
.globl loop
.globl prepare_boot
.globl linux_boot
.globl clean_dcache_range

/*
 * =========================================================================
 * ARM Linux ARM32 boot requirements & Magic Header per QEMU
 * =========================================================================
 */

/* CPU modes */
.equ Usr32md, 0x10
.equ FIQ32md, 0x11
.equ IRQ32md, 0x12
.equ SVC32md, 0x13
.equ Abt32md, 0x17
.equ Und32md, 0x1b
.equ Sys32md, 0x1f

.equ SMPBOOT_ADDR,  0x300
.equ MBOX3_BASE,    0x400000CC

/* ------------------------------------------------------------------------- */
/* ARM exception vector table & zImage Magic Table                           */
/* ------------------------------------------------------------------------- */
/* _start si trova esattamente a 0x8000 all'inizio del file .img             */

.align 2

_start:
    /* 
     * Il primo vettore (Reset) deve essere un salto istantaneo a _reset.
     * Questo garantisce che sia la Raspberry Pi reale che QEMU eseguano subito il codice.
     */
    b       _reset
    ldr     pc, _undefined_instruction_handler_h
    ldr     pc, _software_interrupt_handler_h
    ldr     pc, _prefetch_abort_handler_h
    ldr     pc, _data_abort_handler_h
    ldr     pc, _unused_handler_h
    ldr     pc, _interrupt_handler_h
    ldr     pc, _fast_interrupt_handler_h

/* 
 * TRUCCO HARDWARE: Tabella magica Linux zImage inserita subito dopo i vettori.
 * Occupa esattamente lo spazio dei vettori letterali successivi per ingannare QEMU
 * senza spostare gli offset delle funzioni o rompere il boot su scheda reale.
 */
_zimage_magic:
    .word   0                            /* Riservato */
    .word   0                            /* Indirizzo iniziale del modulo */
    .word   __end - _start               /* Dimensione totale del binario terminato */
    .word   0x016f2818                   /* MAGIC NUMBER LINUX ARM32 ZIMAGE (Richiesto da QEMU) */
    .word   0                            /* Flag addizionali */
    .word   0                            /* Riservato */
    .word   0                            /* Riservato */

/* Vector literal table */
_reset_handler_h:
    .word   _reset
_undefined_instruction_handler_h:
    .word   undefined_instruction_interrupt_handler
_software_interrupt_handler_h:
    .word   bad_interrupt_handler
_prefetch_abort_handler_h:
    .word   bad_interrupt_handler
_data_abort_handler_h:
    .word   data_abort_interrupt_handler
_unused_handler_h:
    .word   _reset
_interrupt_handler_h:
    .word   irq_interrupt_handler
_fast_interrupt_handler_h:
    .word   fiq_interrupt_handler

/* ------------------------------------------------------------------------- */
/* BCM2836 SMP wakeup                                                        */
/* ------------------------------------------------------------------------- */
smp_wakeup_stub:
    /* Read MPIDR */
    mrc     p15, 0, r0, c0, c0, 5
    and     r0, r0, #3

    /* r5 = mailbox base */
    ldr     r5, =MBOX3_BASE

1:
    /* Wait for Linux to write the secondary entry address */
    wfe
    ldr     r3, [r5, r0, lsl #4]
    cmp     r3, #0
    beq     1b

    /* Clear mailbox */
    str     r3, [r5, r0, lsl #4]
    dsb     sy
    isb

    /* Jump directly to Linux's secondary entrypoint */
    bx      r3
smp_wakeup_stub_end:

/* ------------------------------------------------------------------------- */
/* IRQ handler                                                               */
/* ------------------------------------------------------------------------- */
irq_interrupt_handler:
    sub     lr, lr, #4
    stmfd   sp!, {r0-r3, r12, lr}
    bl      disable_irq
    bl      irq_interrupt_handler_c
    bl      enable_irq
    ldmfd   sp!, {r0-r3, r12, pc}

/* ------------------------------------------------------------------------- */
/* Boot initialization / reset entry                                         */
/* ------------------------------------------------------------------------- */
_reset:
    /* Preserva immediatamente gli argomenti di boot passati da QEMU */
    mov     r10, r0
    mov     r11, r1
    mov     r12, r2

    /* Determine CPU ID */
    mrc     p15, 0, r5, c0, c0, 5
    and     r5, r5, #3

    cmp     r5, #0
    bne     smp_wakeup_stub

    /* Copy exception vectors to physical address 0 */
    /* SICURO: non usiamo r2 nella copia per non distruggere i parametri di boot */
    ldr     r0, =_start
    mov     r1, #0x00000000

    /* Blocco 1 (Istruzioni dei vettori con salto zImage incorporato - 32 byte) */
    ldmia   r0!, {r3-r9}
    stmia   r1!, {r3-r9}
    ldr     r3, [r0], #4
    str     r3, [r1], #4

    /* Blocco 2 (Literal table delle etichette + Header Magic - 32 byte) */
    ldmia   r0!, {r3-r9}
    stmia   r1!, {r3-r9}
    ldr     r3, [r0], #4
    str     r3, [r1], #4

    dsb     sy
    isb

    /* UND stack */
    ldr     r0, =Und32md
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    isb
    ldr     sp, =__und_stack

    /* ABT stack */
    ldr     r0, =Abt32md
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    isb
    ldr     sp, =__abt_stack

    /* FIQ stack */
    ldr     r0, =FIQ32md
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    isb
    ldr     sp, =__fiq_stack

    /* IRQ stack */
    ldr     r0, =IRQ32md
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    isb
    ldr     sp, =__irq_stack

    /* Return to SVC32 with SVC stack */
    ldr     r0, =SVC32md
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    isb
    ldr     sp, =__svc_stack

    /* Clear BSS */
    ldr     r4, =__bss_start
    ldr     r9, =__bss_end
    mov     r5, #0
    mov     r6, #0
    mov     r7, #0
    mov     r8, #0
    b       2f
1:
    stmia   r4!, {r5-r8}
2:
    cmp     r4, r9
    blo     1b

    /* Restore original boot arguments */
    mov     r0, r10
    mov     r1, r11
    mov     r2, r12

    /* Enter VladBootin C */
    ldr     r3, =vladBootin_main
    blx     r3

    b       loop

/* ------------------------------------------------------------------------- */
/* Linux boot preparation                                                    */
/* ------------------------------------------------------------------------- */
prepare_boot:
    mrs     r3, cpsr
    bic     r3, r3, #0x1f
    orr     r3, r3, #0x13
    orr     r3, r3, #0xc0
    msr     cpsr_c, r3
    isb

    mrc     p15, 0, r3, c1, c0, 0
    bic     r3, r3, #(1 << 0)
    bic     r3, r3, #(1 << 2)
    bic     r3, r3, #(1 << 11)
    bic     r3, r3, #(1 << 12)
    mcr     p15, 0, r3, c1, c0, 0
    dsb     sy
    isb

    mov     r3, #0
    mcr     p15, 0, r3, c8, c7, 0
    dsb     sy
    isb

    mcr     p15, 0, r3, c7, c5, 0
    mcr     p15, 0, r3, c7, c5, 6
    dsb     sy
    isb

    bx      lr

/* ------------------------------------------------------------------------- */
/* D-cache clean range                                                       */
/* ------------------------------------------------------------------------- */
clean_dcache_range:
    mrc     p15, 0, r2, c0, c0, 1
    ubfx    r2, r2, #16, #4
    mov     r3, #4
    lsl     r3, r3, r2
    sub     r2, r3, #1
    bic     r0, r0, r2
1:
    mcr     p15, 0, r0, c7, c10, 1
    add     r0, r0, r3
    cmp     r0, r1
    blo     1b
    dsb     sy
    bx      lr

/* ------------------------------------------------------------------------- */
/* Direct Linux handoff                                                      */
/* ------------------------------------------------------------------------- */
linux_boot:
    mov     r3, r0
    mov     r0, #0
    mvn     r1, #0

    mrs     r12, cpsr
    bic     r12, r12, #0x1f
    orr     r12, r12, #0x13
    orr     r12, r12, #0xc0
    msr     cpsr_c, r12
    isb

    bx      r3
1:
    b       1b

/* ------------------------------------------------------------------------- */
/* Halt                                                                      */
/* ------------------------------------------------------------------------- */
halt:
1:
    wfe
    b       1b

/* ------------------------------------------------------------------------- */
/* IRQ control                                                               */
/* ------------------------------------------------------------------------- */
enable_irq:
    mrs     r1, cpsr
    bic     r1, r1, #0x80
    msr     cpsr_c, r1
    isb
    bx      lr

disable_irq:
    mrs     r1, cpsr
    orr     r1, r1, #0x80
    msr     cpsr_c, r1
    isb
    bx      lr

/* ------------------------------------------------------------------------- */
/* FIQ control                                                               */
/* ------------------------------------------------------------------------- */
enable_fiq:
    mrs     r1, cpsr
    bic     r1, r1, #0x40
    msr     cpsr_c, r1
    isb
    bx      lr

disable_fiq:
    mrs     r1, cpsr
    orr     r1, r1, #0x40
    msr     cpsr_c, r1
    isb
    bx      lr

/* ------------------------------------------------------------------------- */
/* MMU                                                                       */
/* ------------------------------------------------------------------------- */
enable_mmu:
    mcr     p15, 0, r2, c8, c7, 0
    dsb     sy
    isb

    mov     r2, #0x8000
    mcr     p15, 0, r2, c2, c0, 2
    mcr     p15, 0, r0, c2, c0, 0
    mcr     p15, 0, r1, c3, c0, 0

    mrc     p15, 0, r2, c1, c0, 0
    orr     r2, r2, #0x1
    mcr     p15, 0, r2, c1, c0, 0
    dsb     sy
    isb
    bx      lr

/* ------------------------------------------------------------------------- */
/* CP15                                                                      */
/* ------------------------------------------------------------------------- */
read_cp15:
    mrc     p15, 0, r2, c1, c0, 0
    bx      lr
reset_cp15:
    mvn     r0, #0x0
    mcr     p15, 0, r0, c1, c0, 0
    dsb     sy
    isb
    bx      lr
/* Infinite loop */                                                            
loop:
    b       loop
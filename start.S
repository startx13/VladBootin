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


/*
 * ARM Linux boot entry requirements:
 *
 *   r0 = 0
 *   r1 = machine type / ~0 for DT-only platforms
 *   r2 = physical address of ATAGS or DTB
 *
 * Linux expects:
 *
 *   - SVC32 mode
 *   - IRQ disabled
 *   - FIQ disabled
 *   - MMU disabled
 *   - D-cache disabled
 *   - I-cache may be enabled or disabled
 *
 * _start is also the exception vector table.
 */


/* ------------------------------------------------------------------------- */
/* CPU modes                                                                 */
/* ------------------------------------------------------------------------- */

.equ Usr32md, 0x10
.equ FIQ32md, 0x11
.equ IRQ32md, 0x12
.equ SVC32md, 0x13
.equ Abt32md, 0x17
.equ Und32md, 0x1b
.equ Sys32md, 0x1f


/* ------------------------------------------------------------------------- */
/* Stacks                                                                    */
/* ------------------------------------------------------------------------- */

.equ USR_Stack, 0x03020000
.equ IRQ_Stack, 0x03008000
.equ SVC_Stack, IRQ_Stack-128


/* ------------------------------------------------------------------------- */
/* BCM2836 SMP wakeup                                                        */
/* ------------------------------------------------------------------------- */

/*
 * Secondary cores (1-3) spin here waiting to be released by Linux.
 *
 * Linux's "brcm,bcm2836-smp" enable-method wakes a secondary core
 * by writing its entry point address into that core's mailbox 3
 * (LOCAL_MAILBOX3_SET0, one register per core, stride 0x10),
 * at the BCM2836 local interrupt controller base 0x40000000.
 *
 * Without this stub running on the secondary cores, Linux's
 * smp_init() waits forever for cores that are simply parked in
 * halt/wfe and never polling their mailbox - this is what was
 * hanging the boot.
 *
 * This mirrors what QEMU's write_smpboot() / the real RPi2
 * firmware does before handing control to Linux.
 */

.equ SMPBOOT_ADDR,  0x300
.equ MBOX3_BASE,    0x400000CC   @ LOCAL_MAILBOX3_CLR0/SET0, core 0

smp_wakeup_stub:

    mrc p15, 0, r0, c0, c0, 5      @ MPIDR
    and r0, r0, #3                 @ core id (0-3)

    ldr r5, =MBOX3_BASE

1:
    wfe
    ldr r3, [r5, r0, lsl #4]       @ read this core's mailbox3
    cmp r3, #0
    beq 1b                         @ spin while zero

    str r3, [r5, r0, lsl #4]       @ clear mailbox3
    dsb sy
    bx r3                          @ jump to the address Linux gave us

smp_wakeup_stub_end:


/* ------------------------------------------------------------------------- */
/* Exception vector table                                                    */
/* ------------------------------------------------------------------------- */

_start:

    /*
     * Check CPU ID.
     *
     * CPU0 runs the bootloader.
     * CPU1-3 park in the SMP wakeup stub, waiting for Linux.
     */
    mrc p15, 0, r5, c0, c0, 5
    and r5, r5, #3
    cmp r5, #0
    bne smp_wakeup_stub


    /*
     * ARM exception vectors.
     *
     * The vector table is copied to address 0 below.
     */

    ldr pc, _reset_handler_h
    ldr pc, _undefined_instruction_handler_h
    ldr pc, _software_interrupt_handler_h
    ldr pc, _prefetch_abort_handler_h
    ldr pc, _data_abort_handler_h
    ldr pc, _unused_handler_h
    ldr pc, _interrupt_handler_h
    ldr pc, _fast_interrupt_handler_h


_reset_handler_h:
    .word _reset

_undefined_instruction_handler_h:
    .word undefined_instruction_interrupt_handler

_software_interrupt_handler_h:
    .word bad_interrupt_handler

_prefetch_abort_handler_h:
    .word bad_interrupt_handler

_data_abort_handler_h:
    .word data_abort_interrupt_handler

_unused_handler_h:
    .word _reset

_interrupt_handler_h:
    .word irq_interrupt_handler

_fast_interrupt_handler_h:
    .word fiq_interrupt_handler


/* ------------------------------------------------------------------------- */
/* IRQ handler                                                               */
/* ------------------------------------------------------------------------- */

irq_interrupt_handler:

    /*
     * IRQ exception LR points two instructions past the interrupted
     * instruction. Rewind it before returning.
     */
    sub lr, #4

    /*
     * Save interrupted context.
     */
    stmfd sp!, {r0-r3, r12, lr}

    /*
     * Disable IRQ while handling the interrupt.
     */
    bl disable_irq

    /*
     * C interrupt handler.
     */
    bl irq_interrupt_handler_c

    /*
     * Re-enable IRQ.
     */
    bl enable_irq

    /*
     * Restore interrupted context and return.
     */
    ldmfd sp!, {r0-r3, r12, pc}


/* ------------------------------------------------------------------------- */
/* Boot initialization                                                       */
/* ------------------------------------------------------------------------- */

_reset:

    /*
     * Preserve incoming boot arguments.
     *
     * These are restored immediately before entering C.
     */
    mov r10, r0
    mov r11, r1
    mov r12, r2


    /*
     * ---------------------------------------------------------------------
     * Copy exception vectors to physical address 0.
     * ---------------------------------------------------------------------
     */

    ldr r0, =_start
    mov r1, #0x00

    ldmia r0!, {r2, r3, r4, r5, r6, r7, r8, r9}
    stmia r1!, {r2, r3, r4, r5, r6, r7, r8, r9}

    ldmia r0!, {r2, r3, r4, r5, r6, r7, r8, r9}
    stmia r1!, {r2, r3, r4, r5, r6, r7, r8, r9}


    /*
     * ---------------------------------------------------------------------
     * CPU mode / stack initialization
     * ---------------------------------------------------------------------
     *
     * VladBootin remains in privileged SVC32 mode.
     *
     * IRQ and FIQ are disabled during boot.
     */


    /*
     * SVC stack.
     */
    ldr r0, =SVC32md
    orr r0, r0, #0xC0
    msr cpsr_c, r0
    ldr sp, =SVC_Stack


    /*
     * IRQ stack.
     */
    ldr r0, =IRQ32md
    orr r0, r0, #0xC0
    msr cpsr_c, r0
    ldr sp, =IRQ_Stack


    /*
     * FIQ stack.
     *
     * No dedicated FIQ stack existed in the original bootloader,
     * therefore use the existing IRQ stack for now.
     */
    ldr r0, =FIQ32md
    orr r0, r0, #0xC0
    msr cpsr_c, r0
    ldr sp, =IRQ_Stack


    /*
     * Return to SVC32.
     */
    ldr r0, =SVC32md
    orr r0, r0, #0xC0
    msr cpsr_c, r0
    ldr sp, =SVC_Stack


    /*
     * ---------------------------------------------------------------------
     * Clear BSS
     * ---------------------------------------------------------------------
     */

    ldr r4, =__bss_start
    ldr r9, =__bss_end

    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov r8, #0

    b 2f

1:
    stmia r4!, {r5-r8}

2:
    cmp r4, r9
    blo 1b


    /*
     * ---------------------------------------------------------------------
     * Restore original boot arguments.
     * ---------------------------------------------------------------------
     */

    mov r0, r10
    mov r1, r11
    mov r2, r12


    /*
     * ---------------------------------------------------------------------
     * Enter the C side of VladBootin.
     * ---------------------------------------------------------------------
     *
     * CPU is now:
     *
     *   SVC32
     *   IRQ disabled
     *   FIQ disabled
     */

    ldr r3, =vladBootin_main
    blx r3


/* ------------------------------------------------------------------------- */
/* Linux boot preparation                                                    */
/* ------------------------------------------------------------------------- */

/*
 * prepare_boot()
 *
 * Prepare the CPU for Linux ARM32 kernel handoff.
 *
 * On return:
 *
 *   SVC32
 *   IRQ disabled
 *   FIQ disabled
 *   MMU disabled
 *   D-cache disabled
 *
 * r0, r1 and r2 are preserved.
 *
 * The caller is responsible for setting:
 *
 *   r0 = 0
 *   r1 = ~0
 *   r2 = physical address of DTB
 */

.globl clean_dcache_range

clean_dcache_range:
    mrc p15, 0, r2, c0, c0, 1
    ubfx r2, r2, #16, #4

    mov r3, #4
    lsl r3, r3, r2

    sub r2, r3, #1
    bic r0, r0, r2

1:
    mcr p15, 0, r0, c7, c10, 1
    add r0, r0, r3
    cmp r0, r1
    blo 1b

    dsb sy
    bx lr

prepare_boot:

    /*
     * Preserve r0/r1/r2 (Linux boot args) across this whole function -
     * we use r0-r3 below for the SMP stub copy.
     */
    push {r0, r1, r2, lr}


    /*
     * Enter SVC32 and disable IRQ/FIQ.
     */
    mrs r3, cpsr

    bic r3, r3, #0x1f
    orr r3, r3, #SVC32md
    orr r3, r3, #0xc0

    msr cpsr_c, r3


    /*
     * Read SCTLR.
     */
    mrc p15, 0, r3, c1, c0, 0


    /*
     * Disable MMU.
     */
    bic r3, r3, #0x01

    /*
     * Disable D-cache.
     */
    bic r3, r3, #0x04

    /*
     * Disable I-cache (bit 12).
     */
    bic r3, r3, #0x1000

    /*
     * Disable branch predictor (bit 11).
     */
    bic r3, r3, #0x800


    /*
     * Write SCTLR.
     */
    mcr p15, 0, r3, c1, c0, 0


    /*
     * Synchronize control register change.
     */
    isb
    dsb sy
    isb


    /*
     * Invalidate entire I-cache.
     */
    mov r3, #0
    mcr p15, 0, r3, c7, c5, 0      @ ICIALLU - invalidate I-cache

    /*
     * Invalidate branch predictor array.
     */
    mcr p15, 0, r3, c7, c5, 6      @ BPIALL

    dsb sy
    isb


    /*
     * ---------------------------------------------------------------------
     * Copy the SMP wakeup stub to SMPBOOT_ADDR (0x300).
     * ---------------------------------------------------------------------
     *
     * Secondary cores are already spinning on smp_wakeup_stub
     * (linked at 0x03000000+something), waiting on their mailbox3.
     * We copy that same stub down to the fixed low address 0x300,
     * matching what Linux's "brcm,bcm2836-smp" enable-method and
     * cpu-release mechanism expect, and re-point the cores there
     * via an event (they're in wfe).
     *
     * Simpler alternative actually used here: since our secondary
     * cores are ALREADY running smp_wakeup_stub (linked wherever
     * the linker put it, above 0x03000000), and that code only
     * touches the mailbox registers (position-independent, no
     * absolute self-references), we don't strictly need to
     * relocate it to 0x300 - the cores are already parked in the
     * right polling loop. We only need to make sure they're
     * actually spinning on 'wfe' and not stuck in the old halt.
     *
     * This copy is kept for compatibility with any code path that
     * still expects the stub at the fixed SMPBOOT_ADDR.
     */

    ldr r0, =smp_wakeup_stub
    ldr r1, =SMPBOOT_ADDR
    ldr r2, =smp_wakeup_stub_end
    sub r2, r2, r0                  @ size in bytes

2:
    cmp r2, #0
    beq 3f
    ldr r3, [r0], #4
    str r3, [r1], #4
    sub r2, r2, #4
    b 2b

3:
    dsb sy
    isb


    /*
     * Wake up secondary cores from their wfe, in case they need
     * to notice anything changed (mailbox stub copy above).
     */
    dsb sy
    sev


    pop {r0, r1, r2, lr}
    bx lr

/* ------------------------------------------------------------------------- */
/* Halt                                                                      */
/* ------------------------------------------------------------------------- */

halt:

    wfe
    bne halt


/* ------------------------------------------------------------------------- */
/* IRQ control                                                               */
/* ------------------------------------------------------------------------- */

enable_irq:

    mrs r1, cpsr
    bic r1, r1, #0x80
    msr cpsr_c, r1
    bx lr


disable_irq:

    mrs r1, cpsr
    orr r1, r1, #0x80
    msr cpsr_c, r1
    bx lr


/* ------------------------------------------------------------------------- */
/* FIQ control                                                               */
/* ------------------------------------------------------------------------- */

enable_fiq:

    mrs r1, cpsr
    bic r1, r1, #0x40
    msr cpsr_c, r1
    bx lr


disable_fiq:

    mrs r1, cpsr
    orr r1, r1, #0x40
    msr cpsr_c, r1
    bx lr


/* ------------------------------------------------------------------------- */
/* MMU                                                                       */
/* ------------------------------------------------------------------------- */

enable_mmu:

    /*
     * Invalidate TLB.
     */
    mcr p15, 0, r2, c8, c7, 0


    /*
     * Set Translation Table Base Control Register.
     */
    mov r2, #0x8000
    mcr p15, 0, r2, c2, c0, 2


    /*
     * Set Translation Table Base Register 0.
     */
    mcr p15, 0, r0, c2, c0, 0


    /*
     * Set Domain Access Control Register.
     */
    mcr p15, 0, r1, c3, c0, 0


    /*
     * Enable MMU.
     */
    mrc p15, 0, r2, c1, c0, 0
    orr r2, r2, #0x1
    mcr p15, 0, r2, c1, c0, 0

    bx lr


/* ------------------------------------------------------------------------- */
/* CP15                                                                      */
/* ------------------------------------------------------------------------- */

read_cp15:

    mrc p15, 0, r2, c1, c0, 0
    bx lr


/*
 * WARNING:
 *
 * This function writes 0xffffffff to SCTLR.
 * It is NOT suitable for preparing a Linux kernel handoff.
 *
 * Kept here for compatibility with the original bootloader.
 */

reset_cp15:

    mvn r0, #0x0
    mcr p15, 0, r0, c1, c0, 0
    bx lr


/* ------------------------------------------------------------------------- */
/* Infinite loop                                                             */
/* ------------------------------------------------------------------------- */

loop:

    b loop
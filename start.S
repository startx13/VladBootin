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
 * ARM Linux ARM32 boot requirements
 * =========================================================================
 *
 *   r0 = 0
 *   r1 = machine type / ~0 for DT-only platforms
 *   r2 = physical address of ATAGS or DTB
 *
 * CPU state at kernel entry:
 *
 *   SVC32
 *   IRQ disabled
 *   FIQ disabled
 *   MMU disabled
 *   D-cache disabled
 *   I-cache may be enabled or disabled
 *
 * The kernel must be entered directly in ARM state.
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
 * Secondary cores (1-3) wait here for Linux to provide their entrypoint.
 *
 * Linux's BCM2836 SMP enable method uses mailbox 3.
 */

.equ SMPBOOT_ADDR,  0x300
.equ MBOX3_BASE,    0x400000CC


smp_wakeup_stub:

    /*
     * Read MPIDR.
     */
    mrc     p15, 0, r0, c0, c0, 5
    and     r0, r0, #3

    /*
     * r5 = mailbox base.
     */
    ldr     r5, =MBOX3_BASE


1:
    /*
     * Wait for Linux to write the secondary entry address.
     */
    wfe

    ldr     r3, [r5, r0, lsl #4]

    cmp     r3, #0
    beq     1b

    /*
     * Clear mailbox.
     */
    str     r3, [r5, r0, lsl #4]

    dsb     sy
    isb

    /*
     * Jump directly to Linux's secondary entrypoint.
     */
    bx      r3


smp_wakeup_stub_end:


/* ------------------------------------------------------------------------- */
/* ARM exception vector table                                                */
/* ------------------------------------------------------------------------- */

/*
 * IMPORTANT:
 *
 * _start MUST begin with the ARM exception vectors.
 *
 * Offsets:
 *
 *   0x00 Reset
 *   0x04 Undefined Instruction
 *   0x08 SVC
 *   0x0C Prefetch Abort
 *   0x10 Data Abort
 *   0x14 Reserved
 *   0x18 IRQ
 *   0x1C FIQ
 *
 * The 32-byte vector area together with the following 32-byte literal
 * table is copied to physical address 0x00000000 by _reset.
 *
 * We use "ldr pc, label" rather than "b label", because the vector table
 * is relocated from 0x03000000 to 0x00000000. The literal contains the
 * absolute target address and therefore remains valid after relocation.
 */

.align 2

_start:

    ldr     pc, _reset_handler_h
    ldr     pc, _undefined_instruction_handler_h
    ldr     pc, _software_interrupt_handler_h
    ldr     pc, _prefetch_abort_handler_h
    ldr     pc, _data_abort_handler_h
    ldr     pc, _unused_handler_h
    ldr     pc, _interrupt_handler_h
    ldr     pc, _fast_interrupt_handler_h


/* ------------------------------------------------------------------------- */
/* Vector literal table                                                      */
/* ------------------------------------------------------------------------- */

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
/* IRQ handler                                                               */
/* ------------------------------------------------------------------------- */

irq_interrupt_handler:

    /*
     * IRQ exception LR points beyond the interrupted instruction.
     */
    sub     lr, lr, #4

    /*
     * Save interrupted context.
     */
    stmfd   sp!, {r0-r3, r12, lr}

    /*
     * Disable IRQ while handling it.
     */
    bl      disable_irq

    /*
     * C interrupt handler.
     */
    bl      irq_interrupt_handler_c

    /*
     * Re-enable IRQ.
     */
    bl      enable_irq

    /*
     * Restore interrupted context and return.
     */
    ldmfd   sp!, {r0-r3, r12, pc}


/* ------------------------------------------------------------------------- */
/* Boot initialization / reset entry                                         */
/* ------------------------------------------------------------------------- */

_reset:

    /*
     * ---------------------------------------------------------------------
     * Determine CPU ID.
     * ---------------------------------------------------------------------
     *
     * CPU0:
     *     starts VladBootin normally.
     *
     * CPU1-3:
     *     enter the Linux SMP wakeup stub.
     */

    mrc     p15, 0, r5, c0, c0, 5
    and     r5, r5, #3

    cmp     r5, #0
    bne     smp_wakeup_stub


    /*
     * ---------------------------------------------------------------------
     * Preserve incoming boot arguments.
     * ---------------------------------------------------------------------
     */

    mov     r10, r0
    mov     r11, r1
    mov     r12, r2


    /*
     * ---------------------------------------------------------------------
     * Copy exception vectors to physical address 0.
     * ---------------------------------------------------------------------
     *
     * _start.._start+0x3f =
     *
     *   0x00..0x1f   vector instructions
     *   0x20..0x3f   literal targets
     *
     * Total: 64 bytes.
     */

    ldr     r0, =_start
    mov     r1, #0x00000000

    ldmia   r0!, {r2-r9}
    stmia   r1!, {r2-r9}

    ldmia   r0!, {r2-r9}
    stmia   r1!, {r2-r9}

    ldmia   r0!, {r2-r9}
    stmia   r1!, {r2-r9}

    ldmia   r0!, {r2-r9}
    stmia   r1!, {r2-r9}

    dsb     sy
    isb


    /*
     * ---------------------------------------------------------------------
     * SVC32 stack
     * ---------------------------------------------------------------------
     */

    ldr     r0, =SVC32md
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    isb

    ldr     sp, =SVC_Stack


    /*
     * ---------------------------------------------------------------------
     * IRQ stack
     * ---------------------------------------------------------------------
     */

    ldr     r0, =IRQ32md
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    isb

    ldr     sp, =IRQ_Stack


    /*
     * ---------------------------------------------------------------------
     * FIQ stack
     * ---------------------------------------------------------------------
     *
     * Reuse IRQ stack, as in the original implementation.
     */

    ldr     r0, =FIQ32md
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    isb

    ldr     sp, =IRQ_Stack


    /*
     * ---------------------------------------------------------------------
     * Return to SVC32.
     * ---------------------------------------------------------------------
     */

    ldr     r0, =SVC32md
    orr     r0, r0, #0xC0
    msr     cpsr_c, r0
    isb

    ldr     sp, =SVC_Stack


    /*
     * ---------------------------------------------------------------------
     * Clear BSS.
     * ---------------------------------------------------------------------
     */

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


    /*
     * ---------------------------------------------------------------------
     * Restore original boot arguments.
     * ---------------------------------------------------------------------
     */

    mov     r0, r10
    mov     r1, r11
    mov     r2, r12


    /*
     * ---------------------------------------------------------------------
     * Enter VladBootin C.
     * ---------------------------------------------------------------------
     */

    ldr     r3, =vladBootin_main
    blx     r3


    /*
     * vladBootin_main() should never return.
     */
    b       loop


/* ------------------------------------------------------------------------- */
/* Linux boot preparation                                                    */
/* ------------------------------------------------------------------------- */

/*
 * prepare_boot()
 *
 * Leaves CPU in the state required by Linux ARM32 boot protocol:
 *
 *   SVC32
 *   IRQ disabled
 *   FIQ disabled
 *   MMU disabled
 *   D-cache disabled
 *
 * I-cache is also disabled here.
 * Branch prediction is also disabled.
 *
 * r0/r1/r2 are preserved.
 *
 * IMPORTANT:
 *     VBAR is NOT changed here.
 *     Physical address 0 is NOT modified here.
 */

prepare_boot:

    /*
     * ---------------------------------------------------------------------
     * Enter SVC32 and disable IRQ/FIQ.
     * ---------------------------------------------------------------------
     */

    mrs     r3, cpsr

    bic     r3, r3, #0x1f
    orr     r3, r3, #0x13
    orr     r3, r3, #0xc0

    msr     cpsr_c, r3
    isb


    /*
     * ---------------------------------------------------------------------
     * Read SCTLR.
     * ---------------------------------------------------------------------
     */

    mrc     p15, 0, r3, c1, c0, 0


    /*
     * Disable:
     *
     * M = MMU
     * C = D-cache
     * Z = branch prediction
     * I = I-cache
     */

    bic     r3, r3, #(1 << 0)
    bic     r3, r3, #(1 << 2)
    bic     r3, r3, #(1 << 11)
    bic     r3, r3, #(1 << 12)

    mcr     p15, 0, r3, c1, c0, 0

    dsb     sy
    isb


    /*
     * ---------------------------------------------------------------------
     * Invalidate unified TLB.
     * ---------------------------------------------------------------------
     */

    mov     r3, #0

    mcr     p15, 0, r3, c8, c7, 0

    dsb     sy
    isb


    /*
     * ---------------------------------------------------------------------
     * Invalidate instruction cache.
     * ---------------------------------------------------------------------
     */

    mcr     p15, 0, r3, c7, c5, 0


    /*
     * ---------------------------------------------------------------------
     * Invalidate branch predictor.
     * ---------------------------------------------------------------------
     */

    mcr     p15, 0, r3, c7, c5, 6

    dsb     sy
    isb


    /*
     * Return to bootFromSerial().
     */
    bx      lr


/* ------------------------------------------------------------------------- */
/* D-cache clean range                                                       */
/* ------------------------------------------------------------------------- */

/*
 * clean_dcache_range()
 *
 *   r0 = start address
 *   r1 = end address (exclusive)
 *
 * Cleans all D-cache lines covering [r0, r1).
 *
 * This is kept as a general-purpose helper.
 */

clean_dcache_range:

    /*
     * Read Cache Type Register.
     */
    mrc     p15, 0, r2, c0, c0, 1

    /*
     * CTR.DminLine = bits [19:16].
     */
    ubfx    r2, r2, #16, #4

    /*
     * Cache line size = 4 << DminLine.
     */
    mov     r3, #4
    lsl     r3, r3, r2

    /*
     * Align start address down.
     */
    sub     r2, r3, #1
    bic     r0, r0, r2


1:
    /*
     * Clean data cache line by MVA to PoC.
     */
    mcr     p15, 0, r0, c7, c10, 1

    add     r0, r0, r3

    cmp     r0, r1
    blo     1b

    dsb     sy

    bx      lr


/* ------------------------------------------------------------------------- */
/* Direct Linux handoff                                                      */
/* ------------------------------------------------------------------------- */

/*
 * linux_boot()
 *
 *   r0 = kernel entry address
 *   r1 = unused
 *   r2 = physical DTB address
 *
 * The supplied r1 argument is ignored because this loader is using a
 * Device Tree boot.
 *
 * Linux requires:
 *
 *   r0 = 0
 *   r1 = ~0
 *   r2 = physical DTB address
 *
 * This function NEVER returns.
 */

linux_boot:

    /*
     * Preserve kernel entry address.
     */
    mov     r3, r0


    /*
     * Linux ARM boot arguments.
     */
    mov     r0, #0
    mvn     r1, #0


    /*
     * r2 already contains the physical DTB address.
     */


    /*
     * ---------------------------------------------------------------------
     * Force SVC32 and disable IRQ/FIQ again immediately before entry.
     * ---------------------------------------------------------------------
     */

    mrs     r12, cpsr

    bic     r12, r12, #0x1f
    orr     r12, r12, #0x13
    orr     r12, r12, #0xc0

    msr     cpsr_c, r12
    isb


    /*
     * ---------------------------------------------------------------------
     * DIRECT JUMP TO KERNEL
     * ---------------------------------------------------------------------
     *
     * No BL.
     * No BLX.
     * No return address.
     *
     * Kernel is entered in ARM state.
     */

    bx      r3


    /*
     * Should never be reached.
     */
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

    /*
     * Invalidate TLB.
     */
    mcr     p15, 0, r2, c8, c7, 0

    dsb     sy
    isb


    /*
     * Set Translation Table Base Control Register.
     */
    mov     r2, #0x8000
    mcr     p15, 0, r2, c2, c0, 2


    /*
     * Set Translation Table Base Register 0.
     */
    mcr     p15, 0, r0, c2, c0, 0


    /*
     * Set Domain Access Control Register.
     */
    mcr     p15, 0, r1, c3, c0, 0


    /*
     * Enable MMU.
     */
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


/*
 * WARNING:
 *
 * This function writes 0xffffffff to SCTLR.
 * It is NOT suitable for Linux handoff.
 *
 * Kept for compatibility with the original bootloader.
 */

reset_cp15:

    mvn     r0, #0x0
    mcr     p15, 0, r0, c1, c0, 0

    dsb     sy
    isb

    bx      lr


/* ------------------------------------------------------------------------- */
/* Infinite loop                                                             */
/* ------------------------------------------------------------------------- */

loop:

    b       loop
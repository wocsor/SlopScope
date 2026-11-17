.syntax unified
.cpu arm926ej-s
.arm
.section .text.entry,"ax",%progbits
.global _start
.type _start,%function
_start:
#ifdef SCOPE_COLD_BOOT
    mov r0, #0xd3
    msr cpsr_c, r0
    ldr sp, =0x80070000
    /* Stock BOOT1 has identity mappings */
1:  mrc p15, 0, apsr_nzcv, c7, c14, 3
    bne 1b
    mov r0, #0
    mcr p15, 0, r0, c7, c10, 4
    mrc p15, 0, r1, c1, c0, 0
    bic r1, r1, #0x1000
    bic r1, r1, #5
    mcr p15, 0, r1, c1, c0, 0
    mcr p15, 0, r0, c7, c7, 0
    mcr p15, 0, r0, c8, c7, 0
    ldr r1, =__bss_start
    ldr r2, =__bss_end
2:  cmp r1, r2
    strlo r0, [r1], #4
    blo 2b
    ldr r1, =0x800a0100
    str r0, [r1]
    str r0, [r1, #4]
    str r0, [r1, #8]
    ldr r2, =0x4155544f
    str r2, [r1, #12]
    mov r2, #1
    str r2, [r1, #16]
    bl usb_main
3:  b 3b
#else
    stmdb sp!, {r0-r12, lr}
    mov r4, sp
    mrs r5, cpsr
    orr r0, r5, #0xc0
    msr cpsr_c, r0
    ldr sp, =0x5800
    bl usb_main
    mov sp, r4
    msr cpsr_c, r5
    ldmia sp!, {r0-r12, pc}
#endif
.size _start, .-_start
 .global enter_brom_fel
.type enter_brom_fel,%function
enter_brom_fel:
    /* BOOT0 clears V; ROM FEL enables IRQs using vectors at ffff0000. */
    mrc p15, 0, r1, c1, c0, 0
    orr r1, r1, #0x2000
    mcr p15, 0, r1, c1, c0, 0
    mov r0, #0xd3
    msr cpsr_c, r0
    ldr r0, =0xffff0020
    bx r0
.size enter_brom_fel, .-enter_brom_fel
.section .note.GNU-stack,"",%progbits

.syntax unified
.cpu arm926ej-s
.arm
.section .text.entry,"ax",%progbits
.global _start
.type _start,%function
_start:
    stmdb sp!, {r0-r12, lr}
    mov r4, sp
    mrs r5, cpsr
    orr r0, r5, #0xc0
    msr cpsr_c, r0
    ldr sp, =0x5800
    bl hello_main
    mov sp, r4
    msr cpsr_c, r5
    ldmia sp!, {r0-r12, pc}
.size _start, .-_start
.section .note.GNU-stack,"",%progbits

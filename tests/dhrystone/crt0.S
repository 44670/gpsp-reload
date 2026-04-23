    .syntax unified
    .arm

    .section .text.start, "ax", %progbits
    .global _start
    .type _start, %function

_start:
    b startup
    .space 0xA8
    .ascii "DHRY"
    .byte 0x00, 0x00
    .align 2

startup:
    ldr r0, =__data_load_start
    ldr r1, =__data_start__
    ldr r2, =__data_end__
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b

2:
    ldr r1, =__bss_start__
    ldr r2, =__bss_end__
    mov r3, #0
3:
    cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    add r1, r1, #4
    b 3b

4:
    bl c_start
5:
    b 5b

    .size _start, . - _start

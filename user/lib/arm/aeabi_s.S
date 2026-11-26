/* __aeabi_uldivmod y __aeabi_ldivmod devuelven cociente en r0:r1 y resto en r2:r3 (no se puede en C) */
    .syntax unified
    .arm
    .text
    .globl __aeabi_uldivmod
__aeabi_uldivmod:
    push    {r11, lr}
    sub     sp, sp, #16
    add     r12, sp, #8
    str     r12, [sp]
    bl      zz_udivmod64
    ldr     r2, [sp, #8]
    ldr     r3, [sp, #12]
    add     sp, sp, #16
    pop     {r11, pc}
    .globl __aeabi_ldivmod
__aeabi_ldivmod:
    push    {r11, lr}
    sub     sp, sp, #16
    add     r12, sp, #8
    str     r12, [sp]
    bl      zz_sdivmod64
    ldr     r2, [sp, #8]
    ldr     r3, [sp, #12]
    add     sp, sp, #16
    pop     {r11, pc}

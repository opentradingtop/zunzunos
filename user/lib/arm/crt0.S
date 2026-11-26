/* arranque de los programas en ARM: cabecera ZUNAPP01 (entrada y tamano en memoria), pila alineada a 8 */
    .syntax unified
    .arm
    .section .head, "ax"
    .ascii "ZUNAPP01"
    .word _start, 0
    .word __appsize, 0
    .text
    .globl _start
_start:
    bic     sp, sp, #7
    bl      z_start
1:  mov     r0, #0
    mov     r7, #0
    svc     #0
    b       1b
/* hilos: r0 apunta a {funcion (u64), argumento (u64)} */
    .globl z_thread_entry
z_thread_entry:
    bic     sp, sp, #7
    ldr     r1, [r0]
    ldr     r0, [r0, #8]
    blx     r1
2:  mov     r0, #0
    mov     r7, #1
    svc     #0
    b       2b

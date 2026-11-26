/* ZunzunOS - arranque ARMv7 (Raspberry Pi 2). El firmware carga kernel7.img en 0x8000 y salta aqui,
   normalmente en modo HYP; QEMU (-M raspi2b -bios kernel7.img) hace lo mismo. El kernel esta enlazado en
   0x80008000: hasta encender la MMU todo el codigo debe funcionar en la direccion fisica. */
    .syntax unified
    .arm
    .fpu neon-vfpv4
    .arch_extension virt
    .arch_extension sec

#define VOFF 0x80000000
#define MODE_SVC 0x13
#define MODE_HYP 0x1A
/* descriptores de seccion (1 MB): memoria normal con cache WB-WA compartible, y dispositivo sin ejecucion */
#define SEC_NORMAL 0x1140E
#define SEC_DEVICE 0x00416

    .section .head, "ax"
    .globl _start
_start:
    b       arranque
    .ascii  "ZUNKERN1"            /* firma: el kernel ARM y su disco en memoria (initrd) van en kernel7.img */
    .word   __kend - VOFF         /* donde empieza el initrd (fisica), justo detras del kernel */

arranque:
    /* solo el nucleo 0: los demas esperan para siempre */
    mrc     p15, 0, r0, c0, c0, 5
    ands    r0, r0, #3
    bne     aparcar
    /* HYP -> SVC: el temporizador y la FPU quedan accesibles desde el modo SVC */
    mrs     r0, cpsr
    and     r1, r0, #0x1F
    cmp     r1, #MODE_HYP
    bne     1f
    mrc     p15, 4, r1, c14, c1, 0      /* CNTHCTL: PL1 lee el contador y usa el temporizador fisico */
    orr     r1, r1, #3
    mcr     p15, 4, r1, c14, c1, 0
    mov     r1, #0
    mcrr    p15, 4, r1, r1, c14         /* CNTVOFF = 0 */
    mrc     p15, 4, r1, c1, c1, 2       /* HCPTR: sin trampas para la FPU */
    bic     r1, r1, #0xC00
    bic     r1, r1, #0x8000
    mcr     p15, 4, r1, c1, c1, 2
    mov     r1, #0
    mcr     p15, 4, r1, c1, c1, 3       /* HSTR = 0 */
    mcr     p15, 4, r1, c1, c1, 0       /* HCR = 0 */
    adr     r1, 1f
    msr     elr_hyp, r1
    mov     r1, #(MODE_SVC | 0x1C0)
    msr     spsr_hyp, r1
    eret
1:  cpsid   aif, #MODE_SVC
    /* pila temporal (fisica) */
    ldr     sp, =pila_arranque_tope
    sub     sp, sp, #VOFF
    /* borrar el BSS */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    sub     r0, r0, #VOFF
    sub     r1, r1, #VOFF
    mov     r2, #0
2:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     2b
    /* tabla de primer nivel del arranque (tambien sera la del kernel, TTBR1) */
    ldr     r4, =tabla_kernel
    sub     r4, r4, #VOFF
    /* identidad 0-1008 MB (para pasar a la direccion alta) y la misma RAM en 0x80000000 */
    ldr     r5, =SEC_NORMAL
    mov     r0, #0
3:  orr     r1, r5, r0, lsl #20
    str     r1, [r4, r0, lsl #2]
    add     r2, r0, #0x800
    str     r1, [r4, r2, lsl #2]
    add     r0, r0, #1
    cmp     r0, #0x3F0
    blo     3b
    /* perifericos: 0x3F000000 -> 0xF2000000 (16 MB) y 0x40000000 -> 0xF3000000 (1 MB) */
    ldr     r5, =SEC_DEVICE
    mov     r0, #0
4:  add     r1, r0, #0x3F0
    orr     r1, r5, r1, lsl #20
    add     r2, r0, #0xF20
    str     r1, [r4, r2, lsl #2]
    add     r0, r0, #1
    cmp     r0, #16
    blo     4b
    ldr     r1, =(0x40000000 | SEC_DEVICE)
    ldr     r2, =(0xF30 * 4)
    str     r1, [r4, r2]
    /* SMP en ACTLR (lo pide el Cortex-A7 antes de usar las caches) */
    mrc     p15, 0, r0, c1, c0, 1
    orr     r0, r0, #0x40
    mcr     p15, 0, r0, c1, c0, 1
    isb
    bl      invalidar_l1
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0       /* TLBIALL */
    mcr     p15, 0, r0, c7, c5, 0       /* ICIALLU */
    mcr     p15, 0, r0, c7, c5, 6       /* BPIALL */
    dsb
    isb
    ldr     r0, =0x55555555             /* dominios: cliente (se respetan los permisos) */
    mcr     p15, 0, r0, c3, c0, 0
    mov     r0, #1                      /* TTBCR.N = 1: TTBR0 cubre 0-2 GB, TTBR1 el resto */
    mcr     p15, 0, r0, c2, c0, 2
    orr     r0, r4, #0x4A               /* recorrido de tablas con cache WB-WA, compartible */
    mcr     p15, 0, r0, c2, c0, 0       /* TTBR0 (identidad, solo para el salto) */
    mcr     p15, 0, r0, c2, c0, 1       /* TTBR1 */
    isb
    mrc     p15, 0, r0, c1, c0, 0       /* SCTLR */
    bic     r0, r0, #0x2                /* sin comprobar alineacion */
    bic     r0, r0, #0x2000             /* vectores en VBAR */
    bic     r0, r0, #0x70000000         /* TRE, AFE, TE = 0 */
    orr     r0, r0, #0x5                /* MMU y cache de datos */
    orr     r0, r0, #0x1800             /* prediccion de saltos y cache de instrucciones */
    dsb
    mcr     p15, 0, r0, c1, c0, 0
    isb
    ldr     pc, =alto
alto:
    ldr     sp, =pila_arranque_tope
    ldr     r0, =vectores
    mcr     p15, 0, r0, c12, c0, 0      /* VBAR */
    /* FPU/NEON: acceso a cp10 y cp11, y FPEXC.EN */
    mrc     p15, 0, r0, c1, c0, 2
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
    isb
    mov     r0, #0x40000000
    vmsr    fpexc, r0
    mov     r0, #0
    bl      pi_main
5:  wfi
    b       5b

aparcar:
    wfe
    b       aparcar

/* invalida la cache de datos de nivel 1 por conjunto/via (el firmware puede dejar lineas viejas) */
invalidar_l1:
    mov     r0, #0
    mcr     p15, 2, r0, c0, c0, 0       /* CSSELR: L1 datos */
    isb
    mrc     p15, 1, r1, c0, c0, 0       /* CCSIDR */
    and     r2, r1, #7
    add     r2, r2, #4                  /* log2 del tamano de linea */
    ldr     r3, =0x3FF
    and     r3, r3, r1, lsr #3          /* vias - 1 */
    clz     r5, r3
    ldr     r7, =0x7FFF
    and     r7, r7, r1, lsr #13         /* conjuntos - 1 */
6:  mov     r9, r3
7:  lsl     r11, r9, r5
    orr     r11, r11, r7, lsl r2
    mcr     p15, 0, r11, c7, c6, 2      /* DCISW */
    subs    r9, r9, #1
    bge     7b
    subs    r7, r7, #1
    bge     6b
    dsb
    isb
    bx      lr

/* --- excepciones ---
   Todas acaban en modo SVC con este marco en la pila del kernel de la tarea (regs_t):
   r0-r12, sp y lr del usuario, lr del SVC, pc de vuelta y cpsr. srsdb guarda los dos ultimos. */
    .text
    .p2align 5
    .globl vectores
vectores:
    b       .
    b       ent_indef
    b       ent_svc
    b       ent_pabt
    b       ent_dabt
    b       .
    b       ent_irq
    b       .

.macro GUARDAR
    sub     sp, sp, #12
    stmia   sp, {sp, lr}^
    str     lr, [sp, #8]
    push    {r0-r12}
    mov     r4, sp
    bic     sp, sp, #7
    mov     r0, r4
.endm

ent_irq:
    sub     lr, lr, #4
    srsdb   sp!, #MODE_SVC
    cps     #MODE_SVC
    GUARDAR
    bl      irq_entrada
    b       salir_exc
ent_svc:
    srsdb   sp!, #MODE_SVC
    GUARDAR
    bl      svc_entrada
    b       salir_exc
ent_dabt:
    sub     lr, lr, #8
    srsdb   sp!, #MODE_SVC
    cps     #MODE_SVC
    GUARDAR
    mov     r1, #1
    bl      abort_entrada
    b       salir_exc
ent_pabt:
    sub     lr, lr, #4
    srsdb   sp!, #MODE_SVC
    cps     #MODE_SVC
    GUARDAR
    mov     r1, #2
    bl      abort_entrada
    b       salir_exc
ent_indef:
    sub     lr, lr, #4
    srsdb   sp!, #MODE_SVC
    cps     #MODE_SVC
    GUARDAR
    mov     r1, #3
    bl      abort_entrada
    b       salir_exc

salir_exc:
    mov     sp, r4
    .globl isr_return
isr_return:
    pop     {r0-r12}
    ldmia   sp, {sp, lr}^
    nop
    ldr     lr, [sp, #8]
    add     sp, sp, #12
    rfeia   sp!

/* void switch_context(u64 *old_sp, u64 new_sp): r0 = &old, r2:r3 = nueva sp */
    .globl switch_context
switch_context:
    push    {r4-r11, lr}
    str     sp, [r0]
    mov     r1, #0
    str     r1, [r0, #4]
    mov     sp, r2
    pop     {r4-r11, lr}
    bx      lr

/* primera vez que corre un hilo del kernel: r4 = funcion, r5 = argumento */
    .globl kthread_tramp
kthread_tramp:
    cpsie   i
    mov     r0, r5
    blx     r4
    bl      task_exit

/* FPU: d0-d31 y FPSCR (264 bytes) */
    .globl fpu_save
fpu_save:
    vstmia  r0!, {d0-d15}
    vstmia  r0!, {d16-d31}
    vmrs    r1, fpscr
    str     r1, [r0]
    bx      lr
    .globl fpu_load
fpu_load:
    vldmia  r0!, {d0-d15}
    vldmia  r0!, {d16-d31}
    ldr     r1, [r0]
    vmsr    fpscr, r1
    bx      lr

    .bss
    .p2align 14
    .globl tabla_kernel
tabla_kernel:
    .skip   16384
    .p2align 3
    .skip   65536
pila_arranque_tope:

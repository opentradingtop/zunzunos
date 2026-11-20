.section .head, "ax"
    .ascii "ZUNKERN1"
    .quad _start
    .quad __ksize

.text
.globl _start
_start:
    cli
    mov $boot_stack_top, %rsp
    mov %rdi, %r12
    mov $__bss_start, %rdi
    mov $__kend, %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb
    mov %r12, %rdi
    xor %ebp, %ebp
    call kmain
1:  hlt
    jmp 1b

.globl isr_table
.p2align 4
isr_table:
.set i, 0
.rept 256
    .p2align 4
    .if (i == 8) || (i == 10) || (i == 11) || (i == 12) || (i == 13) || (i == 14) || (i == 17) || (i == 21)
    .else
    pushq $0
    .endif
    pushq $i
    jmp isr_common
    .set i, i + 1
.endr

isr_common:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %rdi
    cld
    xor %ebp, %ebp
    call isr_dispatch
.globl isr_return
isr_return:
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    add $16, %rsp
    iretq

# void switch_context(u64 *old_rsp, u64 new_rsp)
.globl switch_context
switch_context:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rdi)
    mov %rsi, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret

.globl kthread_tramp
kthread_tramp:
    sti
    mov %r13, %rdi
    call *%r12
    call task_exit

.globl load_gdt
load_gdt:
    lgdt (%rdi)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %eax, %eax
    mov %ax, %fs
    mov %ax, %gs
    pushq $0x08
    lea 1f(%rip), %rax
    push %rax
    lretq
1:  mov $0x30, %ax
    ltr %ax
    ret

.bss
.p2align 12
    .skip 65536
boot_stack_top:

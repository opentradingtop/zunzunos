.section .head, "ax"
    .ascii "ZUNAPP01"
    .quad _start
    .quad __appsize
.text
.globl _start
_start:
    and $-16, %rsp
    call z_start
1:  xor %eax, %eax
    xor %ebx, %ebx
    int $0x40
    jmp 1b
.globl z_thread_entry
z_thread_entry:
    and $-16, %rsp
    mov %rdi, %rax
    mov 8(%rax), %rdi
    call *(%rax)
2:  mov $1, %eax
    int $0x40
    jmp 2b

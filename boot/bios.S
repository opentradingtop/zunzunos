# ZunzunOS - cargador BIOS: MBR (sector 0) + segunda etapa (sectores 1..)
# Memoria baja: 0x4000 VBE, 0x5000 bootinfo, 0x6000 mapa E820,
# 0x7C00 este codigo, 0x10000 bufer de disco, 0x70000 tablas de paginas.
.set STAGE_SECTORS, 24
.set BI, 0x5000
.set MMAP, 0x6000
.set VBEI, 0x4000
.set VBEM, 0x4200
.set EDID, 0x4400
.set BUF_SEG, 0x1000
.set KERNEL_PHYS, 0x200000
.set INITRD_PHYS, 0x1000000
.set PT, 0x70000

.code16
.text
.globl _start
_start:
    jmp entry
    nop
    .org 0x5A                       # hueco para un BPB que algunas BIOS tocan
entry:
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7C00, %sp
    ljmp $0, $1f
1:  sti
    cld
    mov %dl, boot_drive
    mov $0x41, %ah
    mov $0x55AA, %bx
    int $0x13
    jc  mbr_err
    cmp $0xAA55, %bx
    jne mbr_err
    mov $dap0, %si
    mov $0x42, %ah
    mov boot_drive, %dl
    int $0x13
    jc  mbr_err
    jmp stage2
mbr_err:
    mov $msg_disk, %si
    jmp fatal
fatal:
    lodsb
    test %al, %al
    jz  2f
    mov $0x0E, %ah
    mov $7, %bx
    int $0x10
    jmp fatal
2:  hlt
    jmp 2b

boot_drive: .byte 0
.balign 4
dap0: .byte 16, 0
      .word STAGE_SECTORS
      .word 0x7E00, 0
      .quad 1
msg_disk: .asciz "ZunzunOS: error de disco"
    .org 440
    .org 510
    .word 0xAA55

# ---- sector 1: campos que rellena mkimage.py ----
    .ascii "ZZBOOT01"
kernel_lba:     .long 0
kernel_sectors: .long 0
initrd_lba:     .long 0
initrd_sectors: .long 0

msg_a20:  .asciz "ZunzunOS: sin A20 / no A20"
msg_read: .asciz "ZunzunOS: error leyendo el sistema / error reading the system"
msg_lm:   .asciz "ZunzunOS: este PC no es de 64 bits / this PC is not 64-bit"

.balign 4
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF        # 0x08 codigo 32
    .quad 0x00CF92000000FFFF        # 0x10 datos
    .quad 0x00AF9A000000FFFF        # 0x18 codigo 64
gdt_end:
gdtr: .word gdt_end - gdt - 1
      .long gdt

dap: .byte 16, 0
dap_n:   .word 0
dap_off: .word 0
dap_seg: .word BUF_SEG
dap_lba: .quad 0

best_mode:  .word 0
best_score: .long 0
native_w:   .word 0
native_h:   .word 0
load_dest:  .long 0
load_left:  .long 0

stage2:
    # CPU de 64 bits?
    mov $0x80000001, %eax
    cpuid
    bt  $29, %edx
    jc  1f
    mov $msg_lm, %si
    jmp fatal
1:
    # A20
    mov $0x2401, %ax
    int $0x15
    in  $0x92, %al
    or  $2, %al
    and $0xFE, %al
    out %al, $0x92

    # bootinfo a cero
    mov $BI, %di
    mov $64, %cx
    xor %ax, %ax
    rep stosw

    call read_e820
    call setup_video
    call enter_unreal

    movl kernel_lba, %eax
    movl kernel_sectors, %ecx
    movl $KERNEL_PHYS, %edi
    call load_high
    movl initrd_lba, %eax
    movl initrd_sectors, %ecx
    movl $INITRD_PHYS, %edi
    call load_high

    # numero de serie del volumen de arranque
    movl 0x7C00+446+8, %eax
    movl %eax, dap_lba
    movl $0, dap_lba+4
    movw $1, dap_n
    movw $0, dap_off
    movw $BUF_SEG, dap_seg
    mov $dap, %si
    mov $0x42, %ah
    mov boot_drive, %dl
    int $0x13
    jc 1f
    mov $BUF_SEG, %ax
    mov %ax, %fs
    movl %fs:67, %eax
    movl %eax, BI+108
1:
    # rellenar bootinfo
    movl $0x4F4E555A, BI            # "ZUNO"
    movl $0x31544F42, BI+4          # "BOT1"
    movl $MMAP, BI+32
    movl $1, BI+44                  # modo BIOS
    movl $KERNEL_PHYS, BI+48
    movl $INITRD_PHYS, BI+64
    movl initrd_sectors, %eax
    shl  $9, %eax
    movl %eax, BI+72

    cli
    lgdt gdtr
    mov %cr0, %eax
    or  $1, %eax
    mov %eax, %cr0
    ljmp $0x08, $pm32

# --- mapa de memoria ---
read_e820:
    xor %ebx, %ebx
    mov $MMAP, %di
    xor %bp, %bp
1:  mov $0xE820, %eax
    mov $0x534D4150, %edx
    mov $24, %ecx
    movl $1, %es:20(%di)
    int $0x15
    jc  2f
    cmp $0x534D4150, %eax
    jne 2f
    add $24, %di
    inc %bp
    cmp $200, %bp
    jae 2f
    test %ebx, %ebx
    jnz 1b
2:  movzx %bp, %eax
    movl %eax, BI+40
    ret

# --- VBE: el mejor modo lineal, preferimos la resolucion nativa (EDID) ---
setup_video:
    mov $0x4F15, %ax
    mov $1, %bx
    xor %cx, %cx
    xor %dx, %dx
    mov $EDID, %di
    int $0x10
    cmp $0x004F, %ax
    jne 1f
    movzbw EDID+58, %ax
    and $0xF0, %ax
    shl $4, %ax
    movzbw EDID+56, %bx
    or  %bx, %ax
    mov %ax, native_w
    movzbw EDID+61, %ax
    and $0xF0, %ax
    shl $4, %ax
    movzbw EDID+59, %bx
    or  %bx, %ax
    mov %ax, native_h
1:  movl $0x32454256, VBEI          # "VBE2"
    mov $0x4F00, %ax
    mov $VBEI, %di
    int $0x10
    cmp $0x004F, %ax
    jne 9f
    mov VBEI+14, %si
    mov VBEI+16, %ax
    mov %ax, %fs
2:  mov %fs:(%si), %cx
    add $2, %si
    cmp $0xFFFF, %cx
    je  5f
    push %cx
    push %si
    push %fs
    mov $0x4F01, %ax
    mov $VBEM, %di
    int $0x10
    pop %fs
    pop %si
    pop %cx
    cmp $0x004F, %ax
    jne 2b
    mov VBEM, %ax
    and $0x91, %ax
    cmp $0x91, %ax
    jne 2b
    cmpb $6, VBEM+0x1B
    jne 2b
    mov VBEM+0x19, %bl
    cmp $32, %bl
    je  3f
    cmp $24, %bl
    jne 2b
3:  movzwl VBEM+0x12, %eax
    movzwl VBEM+0x14, %edx
    mov VBEM+0x12, %di
    cmp native_w, %di
    jne 31f
    mov VBEM+0x14, %di
    cmp native_h, %di
    jne 31f
    cmp $1920, %eax
    ja  31f
    imul %edx, %eax
    add $0x10000000, %eax           # la nativa siempre gana
    jmp 32f
31: cmp $1280, %eax
    ja  2b
    cmp $1024, %edx
    ja  2b
    imul %edx, %eax
32: shl $1, %eax
    cmp $32, %bl
    jne 4f
    inc %eax
4:  cmp best_score, %eax
    jbe 2b
    mov %eax, best_score
    mov %cx, best_mode
    jmp 2b
5:  mov best_mode, %cx
    test %cx, %cx
    jz  9f
    mov $0x4F01, %ax
    mov $VBEM, %di
    int $0x10
    mov best_mode, %bx
    or  $0x4000, %bx
    mov $0x4F02, %ax
    int $0x10
    cmp $0x004F, %ax
    jne 9f
    movl VBEM+0x28, %eax
    movl %eax, BI+8
    movzwl VBEM+0x12, %eax
    movl %eax, BI+16
    movzwl VBEM+0x14, %eax
    movl %eax, BI+20
    movzwl VBEM+0x10, %eax
    cmpb $3, VBEI+5
    jb  6f
    movzwl VBEM+0x32, %edx
    test %edx, %edx
    jz  6f
    mov %edx, %eax
6:  movl %eax, BI+24
    movzbl VBEM+0x19, %eax
    movl %eax, BI+28
    xor %eax, %eax
    cmpb $0, VBEM+0x20              # rojo en el bit 0 => RGB
    jne 7f
    inc %eax
7:  movl %eax, BI+104
9:  ret

# --- modo irreal: DS/ES con limite de 4 GB ---
enter_unreal:
    cli
    lgdt gdtr
    mov %cr0, %eax
    or  $1, %eax
    mov %eax, %cr0
    jmp 1f
1:  mov $0x10, %bx
    mov %bx, %ds
    mov %bx, %es
    and $0xFE, %al
    mov %eax, %cr0
    jmp 2f
2:  xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    sti
    ret

# eax=LBA ecx=sectores edi=destino fisico
load_high:
    movl %edi, load_dest
    movl %ecx, load_left
    movl %eax, dap_lba
1:  movl load_left, %ecx
    test %ecx, %ecx
    jz  9f
    cmp $64, %ecx
    jbe 2f
    mov $64, %ecx
2:  mov %cx, dap_n
    movw $0, dap_off
    movw $BUF_SEG, dap_seg
    push %ecx
    mov $dap, %si
    mov $0x42, %ah
    mov boot_drive, %dl
    int $0x13
    pop %ecx
    jnc 3f
    mov $msg_read, %si
    jmp fatal
3:  addl %ecx, dap_lba
    subl %ecx, load_left
    push %ecx
    call enter_unreal
    pop %ecx
    shl $9, %ecx
    mov $0x10000, %esi
    movl load_dest, %edi
    addl %ecx, load_dest
    cld
    .byte 0x67
    rep movsb
    jmp 1b
9:  ret

.code32
pm32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov $0x7C00, %esp
    # tablas: PML4, PDPT baja, 4 PD (identidad 4 GB), PDPT alta, PD kernel
    mov $PT, %edi
    mov $(0x8000/4), %ecx
    xor %eax, %eax
    rep stosl
    movl $(PT+0x1000+3), PT
    movl $(PT+0x6000+3), PT+0xFF8
    mov $PT+0x1000, %edi
    mov $PT+0x2000+3, %eax
    mov $4, %ecx
1:  mov %eax, (%edi)
    add $8, %edi
    add $0x1000, %eax
    loop 1b
    mov $PT+0x2000, %edi
    mov $0x83, %eax
    xor %edx, %edx
    mov $2048, %ecx
2:  mov %eax, (%edi)
    mov %edx, 4(%edi)
    add $8, %edi
    add $0x200000, %eax
    loop 2b
    movl $(PT+0x7000+3), PT+0x6000+510*8
    mov $PT+0x7000, %edi
    mov $KERNEL_PHYS+0x83, %eax
    mov $16, %ecx
3:  mov %eax, (%edi)
    add $8, %edi
    add $0x200000, %eax
    loop 3b

    mov %cr4, %eax
    or  $0x20, %eax
    mov %eax, %cr4
    mov $PT, %eax
    mov %eax, %cr3
    mov $0xC0000080, %ecx
    rdmsr
    or  $0x100, %eax
    wrmsr
    mov %cr0, %eax
    or  $0x80000000, %eax
    mov %eax, %cr0
    ljmp $0x18, $lm64

.code64
lm64:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7C00, %esp
    mov $BI, %edi
    mov $KERNEL_PHYS+8, %eax
    mov (%rax), %rax
    jmp *%rax

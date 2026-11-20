ENTRY(_start)
EXTERN(z_start)
SECTIONS {
    . = 0x400000;
    __appstart = .;
    .head : { KEEP(*(.head)) }
    .text : { *(.text .text.*) }
    .rodata : { __ro_start = .; *(.rodata .rodata.*) __ro_end = .; }
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    . = ALIGN(4096);
    __append = .;
    __appsize = __append - __appstart;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

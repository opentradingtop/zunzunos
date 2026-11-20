ENTRY(_start)
SECTIONS {
    . = 0xFFFFFFFF80000000;
    __kstart = .;
    .head : { KEEP(*(.head)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    . = ALIGN(4096);
    __kend = .;
    __ksize = __kend - __kstart;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

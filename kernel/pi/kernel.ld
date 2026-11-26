/* kernel ARM: enlazado en 0x80008000, cargado por el firmware en la fisica 0x8000 */
ENTRY(_start)
SECTIONS {
    . = 0x80008000;
    __kstart = .;
    .head : { KEEP(*(.head)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    . = ALIGN(16);
    __bss_end = .;
    . = ALIGN(4096);
    __kend = .;
    __ksize = __kend - __kstart;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*) *(.ARM.extab*) *(.ARM.attributes) }
}

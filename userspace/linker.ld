ENTRY(_start)

USER_BASE = DEFINED(USER_BASE) ? USER_BASE : 0x00160000;

SECTIONS
{
    . = USER_BASE;

    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .init_array : {
        __init_array_start = .;
        KEEP (*(.init_array .init_array.*))
        __init_array_end = .;
    }
    .fini_array : {
        __fini_array_start = .;
        KEEP (*(.fini_array .fini_array.*))
        __fini_array_end = .;
    }
    .bss : { *(.bss .bss.* COMMON) }
}

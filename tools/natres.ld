/* GC Station v49 : programme resident du lanceur natif, lie a une adresse fixe (natres.h) */
ENTRY(natres_hdr)
SECTIONS
{
    . = 0x817EE000;                 /* (= NATRES_AT, natres.h) */
    .head   : { KEEP(*(.res_head)) }
    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.* .sdata2 .sdata2.*) }
    .data   : { *(.data .data.* .sdata .sdata.* .got .got2) }
    .bss    : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    . = ALIGN(32);
    __natres_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu.attributes) }
}
ASSERT(__natres_end <= 0x817FFF00, "natres : programme resident trop gros")

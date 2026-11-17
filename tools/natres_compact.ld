/* GC Station v49 : programme resident du lanceur natif, lie a une adresse fixe (natres.h) */
ENTRY(natres_hdr)
SECTIONS
{
    . = 0x817FA800;                 /* (= NATRES_AT_COMPACT, natres.h : v474, Pulse compact) */
    .head   : { KEEP(*(.res_head)) }
    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.* .sdata2 .sdata2.*) }
    .data   : { *(.data .data.* .sdata .sdata.* .got .got2) }
    .bss    : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    . = ALIGN(32);
    __natres_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu.attributes) }
}
ASSERT(__natres_end <= 0x817FFF00, "natres : Pulse compact trop gros (descendre NATRES_AT_COMPACT, natres.h et ici)")

/* GC Station v187 : retour a GC Station en quittant une appli (source/natres/natret.c), en 0x80001800 */
ENTRY(nrt_hdr)
SECTIONS
{
    . = 0x80001800;
    .head   : { KEEP(*(.res_head)) }
    .start  : { KEEP(*(.text.nrt_start)) }
    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.* .sdata2 .sdata2.*) }
    .data   : { *(.data .data.* .sdata .sdata.* .got .got2) }
    .bss    : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    . = ALIGN(32);
    __natret_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu.attributes) }
}

ASSERT(__natret_end <= 0x80002C00, "natret : trop gros (0x80001800 - 0x80002C00 ; ensuite : programme de copie de GC Station)")
ASSERT(nrt_start == ADDR(.head) + SIZEOF(.head), "natret : nrt_start doit suivre l'en-tete")

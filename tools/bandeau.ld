/* v397 : bandeau des succes (source/natres/bandeau.c) : du code seul, l'entree (ban_run) en premier. Lie a 0 : il ne
   contient que des branchements relatifs (verifie par la compilation : aucune donnee, aucune adresse fixe). */
ENTRY(ban_run)
SECTIONS
{
    . = 0;
    .text : { KEEP(*(.text.ban_entree)) *(.text .text.*) }
    __ban_fin = .;
    .interdit : { *(.rodata .rodata.* .data .data.* .sdata .sdata.* .sdata2 .sdata2.* .bss .bss.* .sbss .sbss.* COMMON .got .got2) }
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu.attributes) }
}
ASSERT(SIZEOF(.interdit) == 0, "bandeau : le code ne doit avoir aucune donnee (il est pose a n'importe quelle adresse)")
ASSERT(__ban_fin <= 0xC00, "bandeau : plus de 3 Ko de code")   /* (v398 : 3 Ko : coins arrondis, glissement) */

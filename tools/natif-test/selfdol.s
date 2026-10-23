# faux "GC Station" (v64) : Resident doit le recharger depuis la carte et y sauter
    .text
    .globl _start
_start:
    lis 3,0x8000
    lis 4,0x4242
    ori 4,4,0x4242
    stw 4,0x1F50(3)
    lis 12,0x8000
    ori 12,12,0x1000
    mtctr 12
    bctr

# v95 : faux "programme suivant" (charge par le code de redemarrage en 0x80400000) : repere, puis il lit
# le disque avec ses propres acces au lecteur (que le resident doit avoir branches a la lecture)
    .text
    .globl _start
_start:
    lis 20,0x8000
    lis 0,0x7777
    ori 0,0,0x7777
    stw 0,0x1F60(20)
    lis 9,0xCC00
    lis 0,0xA800
    stw 0,0x6008(9)
    li 0,0x1004/4
    stw 0,0x600C(9)
    li 0,0x20
    stw 0,0x6010(9)
    lis 0,0x0042
    stw 0,0x6014(9)
    li 0,0x20
    stw 0,0x6018(9)
    li 0,3
    stw 0,0x601C(9)
1:  lwz 0,0x601C(9)
    andi. 0,0,1
    bne 1b
    lis 12,0x8000
    ori 12,12,0x1000
    mtctr 12
    bctr

# v95 : faux code de redemarrage de l'apploader (OSExec, ancien apploader), lu par le jeu en 0x81300000 :
# il lit lui-meme le programme suivant (0x60 octets a l'offset 0x5000 de l'ISO -> 0x80400000) avec les
# registres du lecteur, comme le vrai, puis y saute.
    .text
    .globl _start
_start:
    lis 9,0xCC00
    lis 0,0xA800
    stw 0,0x6008(9)
    li 0,0x5000/4
    stw 0,0x600C(9)
    li 0,0x60
    stw 0,0x6010(9)
    lis 0,0x0040
    stw 0,0x6014(9)
    li 0,0x60
    stw 0,0x6018(9)
    li 0,3
    stw 0,0x601C(9)
1:  lwz 0,0x601C(9)
    andi. 0,0,1
    bne 1b
    lis 12,0x8040
    mtctr 12
    bctr

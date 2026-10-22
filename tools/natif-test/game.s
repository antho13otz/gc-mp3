# faux jeu : lit le disque par les registres du lecteur, comme une bibliotheque DVD
    .text
    .globl _start
_start:
    stwu 1,-32(1)
    mflr 0
    stw 0,36(1)
    # active les interruptions de fin (masque TCINT) et acquitte
    lis 9,0xCC00
    li 0,0x3A
    stw 0,0x6000(9)
    # 1) ID du disque : A8000040 -> 0x80100000
    lis 3,0x8010
    li 4,0x20
    li 5,0
    lis 6,0xA800
    ori 6,6,0x40
    bl dvd_cmd
    # 2) lecture : 0x2000 octets a l'offset 0x1004 -> 0x80101000 (non aligne)
    lis 3,0x8010
    ori 3,3,0x1000
    li 4,0x2000
    li 5,0x1004
    lis 6,0xA800
    bl dvd_cmd
    # 3) grosse lecture : 0x40000 octets a l'offset 0x20000 -> 0x80200000
    lis 3,0x8020
    lis 4,0x0004
    lis 5,0x0002
    lis 6,0xA800
    bl dvd_cmd
    # 4) erreur du lecteur : E0000000 -> DIIMMBUF dans r31
    li 3,0
    li 4,0
    li 5,0
    lis 6,0xE000
    bl dvd_cmd
    lis 9,0xCC00
    lwz 31,0x6020(9)
    # 5) couvercle
    lwz 30,0x6004(9)
    # 6) v52 : demi-registre (lhz/sth) et forme "u" (lwzu)
    lhz 29,0x6008(9)
    lis 10,0xCC00
    lwzu 28,0x6008(10)
    li 0,0x1234
    sth 0,0x600E(9)
    lwz 27,0x600C(9)
    lwz 0,36(1)
    mtlr 0
    addi 1,1,32
    blr

# dvd_cmd(r3=memoire, r4=taille, r5=position, r6=commande) : fonction feuille (sans pile)
dvd_cmd:
    lis 9,0xCC00
    stw 6,0x6008(9)
    srwi 5,5,2
    stw 5,0x600C(9)
    stw 4,0x6010(9)
    stw 3,0x6014(9)
    stw 4,0x6018(9)
    li 0,3
    stw 0,0x601C(9)
1:  lwz 0,0x601C(9)
    andi. 0,0,1
    bne 1b
    lwz 7,0x6000(9)
    andi. 0,7,0x10
    beq 2f
    ori 7,7,0x10
    stw 7,0x6000(9)
    li 3,0
    blr
2:  li 3,-1
    blr

# faux jeu : lit le disque par les registres du lecteur, comme une bibliotheque DVD
    .text
    .globl _start
_start:
    stwu 1,-32(1)
    mflr 0
    stw 0,36(1)
    # v57 : table des interruptions du SDK (0x80003040) : SI 20, DI 21, VI 24
    lis 11,0x8000
    lis 12,dummy_si@ha
    addi 12,12,dummy_si@l
    stw 12,0x3090(11)
    lis 12,dummy_di@ha
    addi 12,12,dummy_di@l
    stw 12,0x3094(11)
    lis 12,dummy_vi@ha
    addi 12,12,dummy_vi@l
    stw 12,0x30A0(11)
    # v60 : table des exceptions du SDK (0x80003000) : 2 DSI, 3 ISI, 5 alignement, 6 programme
    lis 12,dummy_di@ha
    addi 12,12,dummy_di@l
    stw 12,0x3008(11)
    stw 12,0x300C(11)
    stw 12,0x3014(11)
    stw 12,0x3018(11)
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
    # (resultats des etapes 5-6 ranges en memoire : l'etape 7 finit dans un redemarrage)
    lis 20,0x8000
    stw 27,0x1F10(20)
    stw 28,0x1F14(20)
    stw 29,0x1F18(20)
    stw 10,0x1F1C(20)
    stw 30,0x1F20(20)
    stw 31,0x1F24(20)
    # 7) v57 : 3 images (VI) par la table, puis A+Z+START tenus (SI) : retour a GC Station
    lis 20,0x8000
    li 21,3
3:  lwz 12,0x30A0(20)
    mtctr 12
    li 3,24
    li 4,0
    bctrl
    addic. 21,21,-1
    bne 3b
    # 8) v60 : si 0x80001F40 = 1, faux plantage (DSI) au lieu de A+Z+START
    lwz 12,0x1F40(20)
    cmpwi 12,1
    bne 9f
    lis 4,0x8000
    ori 4,4,0x1C00
    lis 12,0x8000
    ori 12,12,0x1234
    stw 12,0x198(4)
    lis 12,0x8000
    ori 12,12,0x5678
    stw 12,0x84(4)
    lwz 12,0x3008(20)
    mtctr 12
    li 3,2
    bctr
9:  li 21,90
4:  lwz 12,0x3090(20)
    mtctr 12
    li 3,20
    li 4,0
    bctrl
    addic. 21,21,-1
    bne 4b
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

dummy_di:
    blr
dummy_vi:
    lis 9,0x8000
    lwz 10,0x1F00(9)
    addi 10,10,1
    stw 10,0x1F00(9)
    blr
dummy_si:
    lis 9,0x8000
    lwz 10,0x1F04(9)
    addi 10,10,1
    stw 10,0x1F04(9)
    blr

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
    # v65 : DSP_AI (5) : interruption DMA du son
    lis 12,dummy_ai@ha
    addi 12,12,dummy_ai@l
    stw 12,0x3054(11)
    # v99 : DSP (7) : certains jeux (Cocoto) y programment le tampon du son suivant
    lis 12,dummy_dsp@ha
    addi 12,12,dummy_dsp@l
    stw 12,0x305C(11)
    # v60 : table des exceptions du SDK (0x80003000) : 2 DSI, 3 ISI, 5 alignement, 6 programme
    lis 12,dummy_di@ha
    addi 12,12,dummy_di@l
    stw 12,0x3008(11)
    stw 12,0x300C(11)
    stw 12,0x3014(11)
    stw 12,0x3018(11)
    # v67 : faux OSContext (0x80001C00) passe aux fonctions d'interruption : srr0 = dvd_cmd, lr = _start
    lis 12,dvd_cmd@ha
    addi 12,12,dvd_cmd@l
    stw 12,0x1D98(11)
    lis 12,_start@ha
    addi 12,12,_start@l
    stw 12,0x1C84(11)
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
    # 9) v65 : musique du disque : E1 (0x10000, 0x400 octets d'ADPCM), etat E2, puis une
    #    interruption DMA du son : Resident doit ajouter la musique au tampon du jeu
    li 3,0
    li 4,0x400
    lis 5,1
    lis 6,0xE100
    bl dvd_cmd
    li 3,0
    li 4,0
    li 5,0
    lis 6,0xE200
    bl dvd_cmd
    lis 9,0xCC00
    lwz 26,0x6020(9)
    stw 26,0x1F28(20)
    li 0,3                  # AICR : PSTAT + 48 kHz (disque), DSP 48 kHz
    stw 0,0x6C00(9)
    li 0,0
    ori 0,0,0xFFFF          # AIVR : volume max
    stw 0,0x6C04(9)
    li 0,0x30               # DMA du son : 0x00300000, 0x280 octets (160 echantillons stereo)
    sth 0,0x5030(9)
    li 0,0
    sth 0,0x5032(9)
    ori 0,0,0x8014
    sth 0,0x5036(9)
    lwz 12,0x3054(20)
    mtctr 12
    li 3,5
    li 4,0
    bctrl
    # 9b) v99 : le meme tampon rejoue (le DSP du jeu en retard) : pas de 2e couche de musique
    lwz 12,0x3054(20)
    mtctr 12
    li 3,5
    li 4,0
    bctrl
    # 9c) v99 : le jeu programme le tampon suivant (0x00301000) dans son interruption DSP :
    #     Resident doit y ajouter la suite de la musique, puis rien de plus au son suivant
    lwz 12,0x305C(20)
    mtctr 12
    li 3,7
    li 4,0
    bctrl
    lwz 12,0x3054(20)
    mtctr 12
    li 3,5
    li 4,0
    bctrl
    # 9d) v106 : le DSP recalcule ce meme tampon (Cocoto Kart Racer) : Resident doit y remettre les
    #     MEMES echantillons (pas les suivants : la musique avancerait deux fois trop vite)
    lis 9,0x8030
    ori 9,9,0x1000
    li 0,0
    li 10,0xA0
    mtctr 10
1:  stw 0,0(9)
    addi 9,9,4
    bdnz 1b
    lwz 12,0x305C(20)
    mtctr 12
    li 3,7
    li 4,0
    bctrl
    # 10) v65 : carte memoire du slot A par les registres EXI, ecrits comme le SDK
    #     (__EXIRegs[canal * 5 + n] : adresse calculee) ; resultats en 0x80001F2C...
    lis 9,0xCC00
    li 0,0
    stw 0,0x6800(9)         # (comme EXIInit / OSInterrupt : adresse fixe du canal 0)
    li 3,0
    bl exi_probe
    stw 3,0x1F30(20)
    li 3,0                  # ID : 00 00 puis 4 octets
    li 4,0xC0
    bl exi_sel
    li 3,0
    li 4,0
    li 5,1
    li 6,1
    bl exi_imm
    li 3,0
    li 4,0
    li 5,3
    li 6,0
    bl exi_imm
    stw 3,0x1F2C(20)
    li 3,0
    bl exi_desel
    lis 10,0x8040           # motif de 128 octets en 0x80400000
    li 11,0
    li 12,32
    mtctr 12
5:  lis 12,0xA500
    or 12,12,11
    stwx 12,10,11
    addi 11,11,4
    bdnz 5b
    lis 3,0xF200            # ecriture : F2 00 10 00 | 00 + DMA de 128 octets -> carte 0x2000
    ori 3,3,0x1000
    lis 4,0x0040
    bl card_prog
    lis 3,0xF208            # F2 08 80 00 | 00 -> carte 0x110000 (2e morceau du fichier)
    ori 3,3,0x8000
    lis 4,0x0040
    bl card_prog
    lis 3,0x5200            # lecture : 52 00 10 00 | 00 | 4 octets d'attente + DMA de 512 octets
    ori 3,3,0x1000
    lis 4,0x0041
    bl card_read
    lis 3,0x5208
    ori 3,3,0x8000
    lis 4,0x0041
    ori 4,4,0x0200
    bl card_read
    li 3,1                  # canal 1 (pas la carte) : doit aller au vrai materiel
    li 4,0
    bl exi_sel
    li 3,1
    lis 4,0x5A5A
    ori 4,4,0x5A5A
    li 5,3
    li 6,1
    bl exi_imm
    li 3,1
    bl exi_desel
    lis 20,0x8000
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
    # v79 : comme un jeu qui s'arrete expres : message en r3, adresse bidon en r4,
    # et une pile avec 2 appels (cadres 80001A00 -> 80001A40 -> 80001A80 -> fin)
    lis 5,0x8000
    ori 5,5,0x1A00
    stw 5,4(4)
    addi 12,5,0x40
    stw 12,0(5)
    addi 11,5,0x80
    stw 11,0(12)
    li 0,0
    stw 0,0(11)
    lis 0,0x8000
    ori 0,0,0x7777
    stw 0,4(12)
    lis 0,0x8000
    ori 0,0,0x8888
    stw 0,4(11)
    lis 0,0x6D5F
    ori 0,0,0x446F
    stw 0,0x100(5)
    lis 0,0x5F6D
    ori 0,0,0x6163
    stw 0,0x104(5)
    lis 0,0x6869
    ori 0,0,0x6E65
    stw 0,0x108(5)
    lis 0,0x2E63
    ori 0,0,0x7070
    stw 0,0x10C(5)
    li 0,0
    stw 0,0x110(5)
    addi 12,5,0x100
    stw 12,0x0C(4)
    stw 12,8(5)
    # (v82 : le message est aussi sur la pile, comme dans la fonction d'erreur d'un jeu)
    lis 0,0x0123
    ori 0,0,0x4567
    stw 0,0x10(4)
    lwz 12,0x3008(20)
    mtctr 12
    li 3,2
    # (v83 : vrais registres au plantage : r1 = pile, r25 = repere ; le contexte garde les anciens)
    mr 1,5
    lis 13,0x8001
    lis 25,0x5151
    ori 25,25,0x5151
    bctr
9:  lwz 12,0x1F44(20)       # v95 : si 0x80001F44 = 1 : OSExec (ancien apploader) au lieu de A+Z+START
    cmpwi 12,2
    beq 7f
    cmpwi 12,1
    bne 8f
    lis 3,0x8130            # le code de redemarrage (0x60 octets en 0x3460 de l'ISO) -> 0x81300000
    li 4,0x60
    li 5,0x3460
    lis 6,0xA800
    bl dvd_cmd
    lis 12,0x8130
    mtctr 12
    bctr
    # v95 : 0x80001F44 = 2 : OSExec, apploader recent : le jeu relit l'apploader, lit l'en-tete du .dol
    # suivant et sa section dans un tampon, la recopie a sa place (0x80400000), puis y saute
7:  lis 3,0x8050
    li 4,0x20
    li 5,0x2440
    lis 6,0xA800
    bl dvd_cmd
    lis 3,0x8050
    ori 3,3,0x0100
    li 4,0x100
    li 5,0x5800
    lis 6,0xA800
    bl dvd_cmd
    lis 3,0x8051
    li 4,0x60
    li 5,0x5900
    lis 6,0xA800
    bl dvd_cmd
    lis 10,0x8051
    lis 11,0x8040
    li 12,0x60/4
    mtctr 12
6:  lwz 0,0(10)
    stw 0,0(11)
    addi 10,10,4
    addi 11,11,4
    bdnz 6b
    lis 12,0x8040
    mtctr 12
    bctr
8:  li 21,150
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
    bne 6f                  # (v61 : lecteur occupe -> "interruption", plus bas)
    lwz 7,0x6000(9)
    andi. 0,7,0x10
    beq 2f
    ori 7,7,0x10
    stw 7,0x6000(9)
    li 3,0
    blr
2:  li 3,-1
    blr
    # (v61 : comme une interruption DI : on appelle la fonction de la table, puis on revient attendre)
6:  stwu 1,-32(1)
    mflr 0
    stw 0,36(1)
    lis 12,0x8000
    lwz 12,0x3094(12)
    mtctr 12
    li 3,21
    lis 4,0x8000
    ori 4,4,0x1C00
    bctrl
    lwz 0,36(1)
    mtlr 0
    addi 1,1,32
    lis 9,0xCC00
    b 1b

# --- v65 : fonctions EXI, comme celles du SDK (adresse = 0xCC006800 + canal * 0x14) ---
# exi_sel(r3=canal, r4=CS et horloge)
exi_sel:
    mulli 0,3,0x14
    lis 5,0xCC00
    addi 5,5,0x6800
    add 5,5,0
    lwz 6,0(5)
    andi. 6,6,0x405
    or 6,6,4
    stw 6,0(5)
    blr
# exi_desel(r3=canal)
exi_desel:
    lis 5,0xCC00
    mulli 0,3,0x14
    ori 5,5,0x6800
    add 5,0,5
    lwz 6,0(5)
    andi. 6,6,0x405
    stw 6,0(5)
    blr
# exi_probe(r3=canal) -> r3 = CSR (lwzx)
exi_probe:
    mulli 0,3,0x14
    lis 7,0xCC00
    addi 7,7,0x6800
    lwzx 3,7,0
    blr
# exi_imm(r3=canal, r4=donnees, r5=octets-1, r6=0 lire / 1 ecrire) -> r3 = DATA
exi_imm:
    lis 7,0xCC00
    mulli 0,3,0x14
    addi 7,7,0x6800
    add 7,7,0
    stw 4,0x10(7)
    slwi 5,5,4
    slwi 6,6,2
    or 5,5,6
    ori 5,5,1
    stw 5,0x0C(7)
1:  lwz 0,0x0C(7)
    andi. 0,0,1
    bne 1b
    lwz 3,0x10(7)
    blr
# exi_dma(r3=canal, r4=adresse physique, r5=taille, r6=0 lire / 1 ecrire)
exi_dma:
    lis 7,0xCC00
    addi 7,7,0x6800
    mulli 0,3,0x14
    add 8,7,0
    stw 4,4(8)
    stw 5,8(8)
    slwi 6,6,2
    ori 6,6,3
    stw 6,0x0C(8)
1:  lwz 0,0x0C(8)
    andi. 0,0,1
    bne 1b
    blr
# card_prog(r3 = F2 a1 a2 a3, r4 = adresse physique des 128 octets) : canal 0
card_prog:
    stwu 1,-32(1)
    mflr 0
    stw 0,36(1)
    stw 31,28(1)
    stw 30,24(1)
    mr 31,3
    mr 30,4
    li 3,0
    li 4,0xC0
    bl exi_sel
    li 3,0
    mr 4,31
    li 5,3
    li 6,1
    bl exi_imm
    li 3,0
    li 4,0
    li 5,0
    li 6,1
    bl exi_imm
    li 3,0
    mr 4,30
    li 5,0x80
    li 6,1
    bl exi_dma
    li 3,0
    bl exi_desel
    lwz 30,24(1)
    lwz 31,28(1)
    lwz 0,36(1)
    mtlr 0
    addi 1,1,32
    blr
# card_read(r3 = 52 a1 a2 a3, r4 = adresse physique des 512 octets) : canal 0
card_read:
    stwu 1,-32(1)
    mflr 0
    stw 0,36(1)
    stw 31,28(1)
    stw 30,24(1)
    mr 31,3
    mr 30,4
    li 3,0
    li 4,0xC0
    bl exi_sel
    li 3,0
    mr 4,31
    li 5,3
    li 6,1
    bl exi_imm
    li 3,0
    li 4,0
    li 5,0
    li 6,1
    bl exi_imm
    li 3,0
    li 4,0
    li 5,3
    li 6,1
    bl exi_imm
    li 3,0
    mr 4,30
    li 5,0x200
    li 6,0
    bl exi_dma
    li 3,0
    bl exi_desel
    lwz 30,24(1)
    lwz 31,28(1)
    lwz 0,36(1)
    mtlr 0
    addi 1,1,32
    blr

dummy_dsp:                  # (v99 : programme le tampon 0x00301000 la 1re fois)
    lis 9,0x8000
    lwz 10,0x1F0C(9)
    addi 10,10,1
    stw 10,0x1F0C(9)
    cmpwi 10,1
    bnelr
    lis 9,0xCC00
    li 0,0x30
    sth 0,0x5030(9)
    li 0,0x1000
    sth 0,0x5032(9)
    li 0,0
    ori 0,0,0x8014
    sth 0,0x5036(9)
    blr
dummy_ai:
    lis 9,0x8000
    lwz 10,0x1F08(9)
    addi 10,10,1
    stw 10,0x1F08(9)
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

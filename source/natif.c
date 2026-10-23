// GC Station v52 : lanceur natif (inclus dans main.c apres gclib.c)
// Code original de GC Station (aucun code de Swiss). Base : la documentation publique
// du materiel GameCube (YAGCD, Dolphin) et la specification SD (mode SPI).
//
// Ce que fait un lanceur de jeux :
//  1. lire l'en-tete du disque (boot.bin 0x000, bi2.bin 0x440, apploader 0x2440) ;
//  2. charger main.dol et la table des fichiers (FST) en memoire, comme l'apploader,
//     et remplir la memoire basse (0x80000000...) comme le BIOS ;
//  3. rediriger les lectures du lecteur DVD (registres 0xCC006000) vers la carte SD :
//     a) position de l'ISO sur la carte (secteurs) et programme resident (source/natres) ;
//     b) faux lecteur : les acces du jeu au lecteur sont branches sur le resident.
// Fiche d'un jeu (onglet JEUX) : R = analyse + tests ; A puis X = lanceur maison.
// Banc d'essai sur PC : tools/natif-test/run.sh

#include "natres/natres.h"                  /* programme resident (source/natres/natres.c) */
#define NA_DIR   GCD "/natif"
#define NA_DI    0xCC006000u                 /* registres du lecteur DVD (DI) */
#define NA_EXI0  0xCC006800u                 /* v65 : registres EXI du canal 0 (carte memoire slot A, horloge, SRAM) */
#define NA_XREG  0x1000u                     /* (hit_reg : marque d'un registre EXI) */
#define NA_XIDX  0x80u                       /* (hit_reg EXI : lwzx/stwx, registre connu pendant le jeu seulement) */
#define NA_MAXHIT 400                        /* (1080 Avalanche : 83 acces au lecteur ; v65 : + EXI) */
static void nat_qr_open(const char *id);      /* v51 : QR code vers le rapport (plus bas) */
static const char *na_tramp_why(u32 ins);     /* v52 (plus bas) */
static const char *na_xtramp_why(u32 ins);    /* v65 (plus bas) */

typedef struct {
    char id[7], title[72];
    u8 disc, ver, region, stream;
    u32 dol_off, fst_off, fst_sz, fst_max;
    char al_date[17];
    u32 al_entry, al_size, al_trailer;
    /* main.dol */
    u32 dol_len, entry, lo, hi, bss, bsz;
    int nsec;
    u32 sec_addr[18], sec_sz[18], sec_off[18];
    /* FST */
    u32 nfiles, ndirs, fst_dest;
    /* acces aux registres du lecteur */
    int nhit, ntotal, nexi;                   /* ntotal : tous ceux trouves, meme au-dela de la limite ; nexi : dont EXI */
    int di_lost, x_lost;                      /* v65 : au-dela de la limite (lecteur : jeu pas lance ; EXI : pas de carte virtuelle) */
    u32 hit_addr[NA_MAXHIT];
    u16 hit_reg[NA_MAXHIT];
    u8 hit_store[NA_MAXHIT];
    u32 hit_ins[NA_MAXHIT];                   /* l'instruction elle-meme (pour la remplacer) */
    int nfunc;
    u32 func_addr[NA_MAXHIT];
    int problems;
    char err[160];
} nat_info_t;

static const char *na_adapter(void);
static const char *na_reg_name(u32 r) {
    if (r & NA_XREG) {
        static const char *const xn[5] = { "EXI CSR (etat, choix)", "EXI MAR (adresse DMA)", "EXI LENGTH (taille DMA)", "EXI CR (demarrage)", "EXI DATA (donnees)" };
        if (r & NA_XIDX) return "EXI (registre calcule pendant le jeu)";
        u32 x = (r & 0x7F) >> 2;
        return x < 5 ? xn[x] : "EXI ?";
    }
    switch (r) {
    case 0x00: return "DISR (etat)";
    case 0x04: return "DICVR (couvercle)";
    case 0x08: return "DICMDBUF0 (commande)";
    case 0x0C: return "DICMDBUF1 (position)";
    case 0x10: return "DICMDBUF2 (longueur)";
    case 0x14: return "DIMAR (adresse memoire)";
    case 0x18: return "DILENGTH (taille DMA)";
    case 0x1C: return "DICR (demarrage)";
    case 0x20: return "DIIMMBUF (reponse)";
    case 0x24: return "DICFG (config)";
    }
    return "?";
}
static const char *na_region(u8 r) {
    return r == 0 ? "Japon (NTSC-J)" : r == 1 ? "Amerique (NTSC-U)" : r == 2 ? "Europe (PAL)" : r == 4 ? "Coree (NTSC-K)" : "inconnue";
}

/* debut de la fonction qui contient addr : on remonte jusqu'a "blr" ou "mflr r0" / "stwu r1" */
static u32 na_func_start(const u8 *txt, u32 base, u32 addr) {
    u32 a = addr;
    for (int n = 0; n < 2048; n++) {
        u32 cur = be32(txt + (a - base));
        if ((cur & 0xFFFF0000u) == 0x94210000u && (cur & 0x8000u)) return a;   /* stwu r1,-N(r1) : prologue */
        if (a == base) return a;                              /* debut de la section */
        if (be32(txt + (a - 4 - base)) == 0x4E800020u) return a;   /* blr : la fonction d'avant finit la */
        a -= 4;
    }
    return addr;
}

static void na_hit(nat_info_t *in, const u8 *txt, u32 base, u32 at, u32 reg, int store, u32 ins) {
    in->ntotal++;
    if (reg & NA_XREG) in->nexi++;
    if (in->nhit >= NA_MAXHIT) { if (reg & NA_XREG) in->x_lost++; else in->di_lost++; return; }
    in->hit_addr[in->nhit] = at;
    in->hit_reg[in->nhit] = (u16)reg;
    in->hit_store[in->nhit] = (u8)store;
    in->hit_ins[in->nhit] = ins;
    in->nhit++;
    u32 f = na_func_start(txt, base, at);
    for (int i = 0; i < in->nfunc; i++) if (in->func_addr[i] == f) return;
    if (in->nfunc < NA_MAXHIT) in->func_addr[in->nfunc++] = f;
}

/* v65 : acces EXI (carte memoire, horloge). Le SDK ecrit __EXIRegs[canal * 5 + n] : l'adresse
   vaut 0xCC006800 + un decalage connu seulement pendant le jeu (le canal). On suit donc les
   registres "base connue + inconnu" (lis/addi/ori, add, mr), et on ne les oublie qu'au debut
   ou a la fin d'une fonction (blr, bctr, prologue). L'aiguillage recalcule l'adresse pendant
   le jeu : seul le canal 0 va a la carte virtuelle, le reste au vrai materiel. Un faux positif
   ne change donc rien (meme acces, fait par le resident). */
static void na_xscan(nat_info_t *in, const u8 *txt, u32 base, u32 o, u32 *xv, u32 *xm) {
    u32 ins = be32(txt + o), op = ins >> 26, rd = (ins >> 21) & 31, ra = (ins >> 16) & 31, rb = (ins >> 11) & 31, xo = (ins >> 1) & 0x3FF;
    s32 simm = (s16)(ins & 0xFFFF);
    u32 uimm = ins & 0xFFFF, m = *xm, ea = 0;
    int acc = 0, store = 0, idx = 0;
#define XK(r) ((m >> (r)) & 1u)
#define XS(r, v) do { xv[r] = (v); m |= 1u << (r); } while (0)
#define XC(r) (m &= ~(1u << (r)))
    if (op == 37 && rd == 1 && ra == 1 && simm < 0) m = 0;            /* stwu r1,-N(r1) : debut de fonction */
    if (op == 15) { if (!ra) XS(rd, uimm << 16); else if (XK(ra)) XS(rd, xv[ra] + (uimm << 16)); else XC(rd); }
    else if (op == 14) { if (!ra) XS(rd, (u32)simm); else if (XK(ra)) XS(rd, xv[ra] + (u32)simm); else XC(rd); }
    else if (op == 24) { if (XK(rd)) XS(ra, xv[rd] | uimm); else XC(ra); }   /* ori rA,rS,imm */
    else if (op >= 32 && op <= 45) {                                   /* lwz lwzu lbz lbzu stw stwu stb stbu lhz lhzu lha lhau sth sthu */
        store = op >= 36 && op <= 39 ? 1 : op >= 44;
        if (ra && XK(ra)) { ea = xv[ra] + (u32)simm; acc = op == 32 || op == 33 || op == 36 || op == 37 || op == 40 || op == 44 ? 1 : 2; }
        if ((op & 1) && ra && XK(ra)) xv[ra] += (u32)simm;              /* (forme "u" : rA avance, toujours "base + inconnu") */
        else if (op & 1) XC(ra);
        if (!store) XC(rd);
    }
    else if (op == 46) m &= (1u << rd) - 1u;                           /* lmw : rD..r31 */
    else if (op == 31) {
        if (xo == 266) {                                               /* add rD,rA,rB */
            if (XK(ra) && XK(rb)) XS(rd, xv[ra] + xv[rb]); else if (XK(ra)) XS(rd, xv[ra]); else if (XK(rb)) XS(rd, xv[rb]); else XC(rd);
        } else if (xo == 444 && rd == rb) { if (XK(rd)) XS(ra, xv[rd]); else XC(ra); }   /* mr rA,rS */
        else if (xo == 23 || xo == 151 || xo == 279 || xo == 407 || xo == 87 || xo == 215 || xo == 55 || xo == 183) {   /* lwzx stwx lhzx sthx lbzx stbx lwzux stwux */
            store = xo == 151 || xo == 407 || xo == 215 || xo == 183;
            if ((ra && XK(ra)) || XK(rb)) { ea = (ra && XK(ra) ? xv[ra] : 0) + (XK(rb) ? xv[rb] : 0); acc = xo == 23 || xo == 151 ? 1 : 2; idx = 1; }
            if (xo == 55 || xo == 183) XC(ra);
            if (!store) XC(rd);
        } else m &= ~((1u << rd) | (1u << ra));
    }
    else if (op == 16 || op == 18 || op == 19) {
        u32 lk = ins & 1, bo = (ins >> 21) & 31;
        int uncond = op == 18 || ((bo & 0x14) == 0x14);
        if (op == 19 && xo != 16 && xo != 528) { /* (crand, isync...) */ }
        else if (lk) m &= ~0x00001FF9u;                                /* appel : r0, r3-r12 */
        else if (op == 19 && uncond) m = 0;                            /* blr, bctr : fin de fonction */
    }
    else if (op == 7 || op == 8 || op == 12 || op == 13) XC(rd);
    else if (op >= 20 && op <= 29) XC(ra);                             /* (rotations, logiques : destination rA) */
#undef XK
#undef XS
#undef XC
    *xm = m;
    if (acc && ea >= NA_EXI0 && ea < NA_EXI0 + 0x40) {
        na_hit(in, txt, base, base + o, NA_XREG | (idx ? NA_XIDX : (ea - NA_EXI0) % 0x14), store, ins);
    }
}

/* Cherche les acces aux registres du lecteur. On suit, instruction par instruction,
   les registres qui contiennent une adresse connue (lis rX,0xCC00 puis addi / ori),
   et on note les lectures / ecritures dont l'adresse tombe dans 0xCC006000..0xCC00603F.
   L'etat est oublie a chaque branchement (simple, mais suffisant pour un rapport). */
static void na_scan_text(nat_info_t *in, const u8 *txt, u32 base, u32 len) {
    u32 val[32]; u32 known = 0;
    u32 xv[32], xm = 0;                                       /* v65 : suivi EXI (voir na_xscan) */
    for (u32 o = 0; o + 4 <= len; o += 4) {
        if ((o & 0x3FFFF) == 0) service_audio();                  /* (v50 : la musique continue pendant l'analyse) */
        na_xscan(in, txt, base, o, xv, &xm);
        u32 ins = be32(txt + o);
        u32 op = ins >> 26, rd = (ins >> 21) & 31, ra = (ins >> 16) & 31;
        s32 simm = (s16)(ins & 0xFFFF);
        u32 uimm = ins & 0xFFFF;
        if (op == 15) {                                       /* addis / lis */
            if (ra == 0) { val[rd] = uimm << 16; known |= 1u << rd; }
            else if (known & (1u << ra)) { val[rd] = val[ra] + (uimm << 16); known |= 1u << rd; }
            else known &= ~(1u << rd);
            continue;
        }
        if (op == 14) {                                       /* addi / li */
            if (ra == 0) { val[rd] = (u32)simm; known |= 1u << rd; }
            else if (known & (1u << ra)) { val[rd] = val[ra] + (u32)simm; known |= 1u << rd; }
            else known &= ~(1u << rd);
            continue;
        }
        if (op == 24) {                                       /* ori rA,rS,imm */
            u32 rs = rd, rdst = ra;
            if (known & (1u << rs)) { val[rdst] = val[rs] | uimm; known |= 1u << rdst; }
            else known &= ~(1u << rdst);
            continue;
        }
        /* lwz 32, lwzu 33, stw 36, stwu 37, lhz 40, sth 44 */
        if (op == 32 || op == 33 || op == 36 || op == 37 || op == 40 || op == 44) {
            int store = op >= 36 && op != 40;
            if (ra && (known & (1u << ra))) {
                u32 ea = val[ra] + (u32)simm;
                if (ea >= NA_DI && ea < NA_DI + 0x40) na_hit(in, txt, base, base + o, ea - NA_DI, store, ins);   /* (EXI : na_xscan) */
                if (op == 33 || op == 37) val[ra] = ea;       /* (forme "u" : rA avance) */
            }
            if (!store) known &= ~(1u << rd);
            continue;
        }
        /* branchements : un saut sans condition finit le bloc (on oublie tout) ; un appel
           (bit LK) peut changer r0 et r3-r12 ; un saut conditionnel garde l'etat */
        if (op == 16 || op == 18 || op == 19) {
            u32 lk = ins & 1, bo = (ins >> 21) & 31, xo = (ins >> 1) & 0x3FF;
            int uncond = op == 18 || ((op == 16 || op == 19) && (bo & 0x14) == 0x14);
            if (op == 19 && xo != 16 && xo != 528) continue;   /* (crand, isync... : pas un saut) */
            if (lk) known &= ~0x00001FF9u;                  /* r0, r3-r12 */
            else if (uncond) known = 0;
            continue;
        }
        /* toute autre instruction qui ecrit un registre general : on l'oublie (approximation) */
        if (op == 31) known &= ~((1u << rd) | (1u << ra));                                     /* (or, and, mr : destination = rA ; add, lwzx : rD) */
        else if (op == 7 || op == 8 || op == 12 || op == 13) known &= ~(1u << rd);              /* destination = rD */
        else if (op == 46) known = 0;                                                          /* lmw : plusieurs registres */
        else if ((op >= 20 && op <= 29) || op == 34 || op == 35 || op == 41 || op == 42 || op == 43)  /* rotations, logiques ; lbz, lha */
            known &= ~((1u << ra) | (1u << rd));
    }
}

/* analyse complete ; retourne 0 si le disque est illisible */
static int nat_analyse(const char *path, nat_info_t *in) {
    memset(in, 0, sizeof(*in));
    gdisc_t d;
    if (!gd_open(&d, path)) { snprintf(in->err, sizeof(in->err), "Fichier illisible"); return 0; }
    u8 h[0x440], b2[0x40], al[0x20];
    if (!gd_read(&d, 0, h, sizeof(h)) || be32(h + 0x1C) != 0xC2339F3Du) { gd_close(&d); snprintf(in->err, sizeof(in->err), "Pas un disque GameCube"); return 0; }
    memcpy(in->id, h, 6);
    in->disc = h[6]; in->ver = h[7]; in->stream = h[8];
    gl_latin(h + 0x20, 0x40, in->title, sizeof(in->title));
    in->dol_off = be32(h + 0x420);
    in->fst_off = be32(h + 0x424);
    in->fst_sz  = be32(h + 0x428);
    in->fst_max = be32(h + 0x42C);
    if (gd_read(&d, 0x440, b2, sizeof(b2))) in->region = (u8)be32(b2 + 0x18);
    if (gd_read(&d, 0x2440, al, sizeof(al))) {
        memcpy(in->al_date, al, 16); in->al_date[16] = 0;
        for (int i = 0; i < 16; i++) if ((u8)in->al_date[i] < 0x20 || (u8)in->al_date[i] > 0x7E) in->al_date[i] = 0;
        in->al_entry = be32(al + 0x10); in->al_size = be32(al + 0x14); in->al_trailer = be32(al + 0x18);
        if (in->al_entry < 0x81200000u || in->al_entry >= 0x81300000u || in->al_size == 0 || in->al_size > 0x100000) {
            in->problems++; snprintf(in->err, sizeof(in->err), "Apploader inattendu");
        }
    }
    /* main.dol : en-tete puis sections */
    u8 dh[0x100];
    if (!in->dol_off || !gd_read(&d, in->dol_off, dh, sizeof(dh))) { gd_close(&d); snprintf(in->err, sizeof(in->err), "main.dol introuvable"); return 0; }
    in->entry = be32(dh + 0xE0); in->bss = be32(dh + 0xD8); in->bsz = be32(dh + 0xDC);
    in->lo = 0xFFFFFFFFu; in->hi = 0;
    u32 dol_end = 0x100;
    for (int i = 0; i < 18; i++) {
        u32 off = be32(dh + i * 4), addr = be32(dh + 0x48 + i * 4), sz = be32(dh + 0x90 + i * 4);
        if (!sz) continue;
        if (addr < 0x80003000u || addr + (u64)sz > 0x81800000u) { in->problems++; snprintf(in->err, sizeof(in->err), "Section %d hors memoire (%08X)", i, (unsigned)addr); continue; }
        in->sec_addr[in->nsec] = addr; in->sec_sz[in->nsec] = sz; in->sec_off[in->nsec] = off; in->nsec++;
        if (addr < in->lo) in->lo = addr;
        if (addr + sz > in->hi) in->hi = addr + sz;
        if (off + sz > dol_end) dol_end = off + sz;
        if (i < 7) {                                         /* code : on le parcourt */
            u8 *t = (u8 *)malloc(sz);
            int okr = t != NULL;
            for (u32 o = 0; okr && o < sz; o += 0x20000) {      /* (v50 : lu par morceaux de 128 Ko, sans couper la musique) */
                u32 k = sz - o < 0x20000 ? sz - o : 0x20000;
                okr = gd_read(&d, in->dol_off + off + o, t + o, k);
                service_audio();
            }
            if (okr) na_scan_text(in, t, addr, sz);
            else { in->problems++; snprintf(in->err, sizeof(in->err), "Section %d illisible (m\xC3\xA9moire ?)", i); }
            free(t);
            service_audio();
        }
    }
    in->dol_len = dol_end;
    if (in->bss + (u64)in->bsz > in->hi) in->hi = in->bss + in->bsz;
    if (!in->nsec || in->entry < in->lo || in->entry >= in->hi) { in->problems++; snprintf(in->err, sizeof(in->err), "Point d'entree invalide"); }
    /* memoire haute : [ jeu ... | FST | bi2.bin | programme resident ] 0x81800000 */
    in->fst_dest = (NATRES_AT - 0x2000u - in->fst_max) & ~31u;
    if (in->fst_sz && in->fst_sz <= 4u << 20) {
        u8 *f = (u8 *)malloc(in->fst_sz);
        if (f && gd_read(&d, in->fst_off, f, in->fst_sz)) {
            u32 n = be32(f + 8);
            if (n && n * 12 <= in->fst_sz)
                for (u32 i = 1; i < n; i++) { if (f[i * 12]) in->ndirs++; else in->nfiles++; }
        }
        free(f);
    } else { in->problems++; snprintf(in->err, sizeof(in->err), "FST invalide"); }
    if (in->hi > in->fst_dest) { in->problems++; snprintf(in->err, sizeof(in->err), "main.dol chevauche la FST"); }
    gd_close(&d);
    return 1;
}

static void nat_write_report(const nat_info_t *in, const char *path, char *out, int cap) {
    mkdir(NA_DIR, 0777);
    snprintf(out, cap, "%s/%s.txt", NA_DIR, in->id[0] ? in->id : "inconnu");
    FILE *f = fopen(out, "w");
    if (!f) { out[0] = 0; return; }
    fprintf(f, "GC Station - analyse native (pour Resident)\n");
    fprintf(f, "Fichier : %s\nAdaptateur SD : %s\n\n", path, na_adapter());
    fprintf(f, "[Disque]\nID : %s   disque %u   version %u\nNom : %s\nRegion : %s\nAudio en streaming : %s\n\n",
            in->id, in->disc + 1, in->ver, in->title, na_region(in->region), in->stream ? "oui" : "non");
    fprintf(f, "[Apploader] 0x2440\nDate : %s\nPoint d'entree : %08X   taille : %u   suite : %u\n\n",
            in->al_date, (unsigned)in->al_entry, (unsigned)in->al_size, (unsigned)in->al_trailer);
    fprintf(f, "[main.dol] position sur le disque : %08X   taille : %u octets\n", (unsigned)in->dol_off, (unsigned)in->dol_len);
    fprintf(f, "Point d'entree : %08X\nMemoire utilisee : %08X - %08X (BSS %08X, %u octets)\n",
            (unsigned)in->entry, (unsigned)in->lo, (unsigned)in->hi, (unsigned)in->bss, (unsigned)in->bsz);
    for (int i = 0; i < in->nsec; i++) fprintf(f, "  section %2d : %08X  %7u octets\n", i, (unsigned)in->sec_addr[i], (unsigned)in->sec_sz[i]);
    fprintf(f, "\n[FST] position %08X  taille %u  maximum %u\n", (unsigned)in->fst_off, (unsigned)in->fst_sz, (unsigned)in->fst_max);
    fprintf(f, "Fichiers : %u   dossiers : %u\nEmplacement prevu en memoire : %08X\n\n", (unsigned)in->nfiles, (unsigned)in->ndirs, (unsigned)in->fst_dest);
    fprintf(f, "[Memoire basse a preparer]\n");
    fprintf(f, "80000000 : 32 premiers octets du disque (ID...)\n");
    fprintf(f, "80000020 : 0D15EA5E (demarrage normal)   80000028 : 01800000 (24 Mo)\n");
    fprintf(f, "80000034 : %08X (haut de l'arene)   80000038 : %08X (FST)   8000003C : %08X\n",
            (unsigned)in->fst_dest, (unsigned)in->fst_dest, (unsigned)in->fst_max);
    fprintf(f, "bi2.bin : %08X   programme resident : %08X - 81800000\n", (unsigned)(NATRES_AT - 0x2000u), (unsigned)NATRES_AT);
    fprintf(f, "800000F4 : copie de bi2.bin   800000F8 : 09A7EC80 (bus)   800000FC : 1CF7C580 (processeur)\n\n");
    fprintf(f, "[Lecteur DVD et carte memoire] acces aux registres CC006000 (lecteur) et CC006800 (EXI, carte memoire) trouves dans main.dol : %d (dont %d EXI)\n", in->ntotal, in->nexi);
    if (in->ntotal > in->nhit) fprintf(f, "  (limite atteinte : seuls les %d premiers sont notes)\n", in->nhit);
    for (int i = 0; i < in->nhit; i++)
    {
        static const char *const opn[] = { "lwz ", "lwzu", "lbz ", "lbzu", "stw ", "stwu", "stb ", "stbu", "lhz ", "lhzu", "lha ", "lhau", "sth ", "sthu" };
        u32 op = in->hit_ins[i] >> 26, xo = (in->hit_ins[i] >> 1) & 0x3FF;
        int isx = (in->hit_reg[i] & NA_XREG) != 0;
        const char *why = isx ? na_xtramp_why(in->hit_ins[i]) : na_tramp_why(in->hit_ins[i]);
        const char *nm = op >= 32 && op <= 45 ? opn[op - 32] : op == 31 && xo == 23 ? "lwzx" : op == 31 && xo == 151 ? "stwx" : "?";
        fprintf(f, "  %08X  %s %s %s%s%s\n", (unsigned)in->hit_addr[i], nm, in->hit_store[i] ? "ecrit " : "lit   ",
                na_reg_name(in->hit_reg[i]), why ? "   <- NON BRANCHABLE : " : "", why ? why : "");
    }
    fprintf(f, "Fonctions concernees : %d\n", in->nfunc);
    for (int i = 0; i < in->nfunc; i++) fprintf(f, "  %08X\n", (unsigned)in->func_addr[i]);
    fprintf(f, "\n(Ce sont elles que Resident branche sur son faux lecteur ; les acces EXI, sur la carte memoire virtuelle.)\n");
    if (in->stream) fprintf(f, "Ce jeu lit sa musique en streaming sur le disque : Resident la decode et l'ajoute au son (v65).\n");
    fprintf(f, "\n");
    fprintf(f, "Bilan : %s%s\n", in->problems ? "probleme : " : "OK, Resident peut essayer ce jeu", in->problems ? in->err : "");
    fclose(f);
}

/* ======================= v49 : etape 3a = l'ISO sur la carte SD =======================
   Pendant le jeu, plus de systeme de fichiers : le programme resident lit des secteurs.
   On calcule donc avant le lancement ou se trouve chaque morceau de l'ISO sur la carte :
   libfat donne le premier cluster du fichier (st_ino), on suit la chaine de la FAT. */
#include "natres_bin.h"                       /* programme resident compile (build/natres_bin.h) */

static natres_ext_t na_ext[NATRES_MAXEXT];
static u32 na_next = 0;

#ifdef GEKKO
/* (libogc de devkitPro : readSectors(secteur, nombre, tampon)) */
static const DISC_INTERFACE *na_iface(void) {
    return sd_slot == 0 ? &__io_gcsd2 : sd_slot == 1 ? &__io_gcsda : sd_slot == 2 ? &__io_gcsdb : NULL;
}
static int na_rawsec(u32 sec, void *buf) {
    const DISC_INTERFACE *d = na_iface();
    return d && d->readSectors(sec, 1, buf) ? 0 : -1;
}
#elif defined(NATIF_BENCH)
static int na_rawsec(u32 sec, void *buf);     /* (banc d'essai : fourni par tools/natif-test/h2.c) */
#else
static int na_rawsec(u32 sec, void *buf) { (void)sec; (void)buf; return -1; }   /* (simulateur : pas de vraie carte) */
#endif

static u32 na_le16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 na_le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static int na_is_fat(const u8 *b) {
    return b[510] == 0x55 && b[511] == 0xAA && na_le16(b + 0x0B) == 512 && b[0x0D] && (!memcmp(b + 0x36, "FAT", 3) || !memcmp(b + 0x52, "FAT", 3));
}

/* morceaux de l'ISO sur la carte ; 0 = OK */
static int nat_map(const char *path, char *err, int cap) {
    static u8 b[512] ATTRIBUTE_ALIGN(32), fb[512] ATTRIBUTE_ALIGN(32);
    struct stat st;
    na_next = 0;
    if (sd_virtual) { snprintf(err, cap, "Pas de vraie carte SD"); return -1; }
    if (stat(path, &st) != 0) { snprintf(err, cap, "Jeu introuvable"); return -1; }
    u32 clus = (u32)st.st_ino;
    u32 need = (u32)(((u64)st.st_size + 511) >> 9);
    /* partition FAT : secteur 0, sinon la 1re partition de la table (MBR) qui est en FAT */
    u32 part = 0;
    if (na_rawsec(0, b)) { snprintf(err, cap, "Lecture brute de la carte impossible"); return -1; }
    if (!na_is_fat(b)) {
        int found = 0;
        u8 mbr[64]; memcpy(mbr, b + 0x1BE, 64);
        for (int i = 0; i < 4 && !found; i++) {
            u32 lba = na_le32(mbr + i * 16 + 8);
            if (lba && !na_rawsec(lba, b) && na_is_fat(b)) { part = lba; found = 1; }
        }
        if (!found) { snprintf(err, cap, "Carte SD : partition FAT introuvable (exFAT ?)"); return -1; }
    }
    u32 spc = b[0x0D], rsvd = na_le16(b + 0x0E), nfats = b[0x10], rootent = na_le16(b + 0x11);
    u32 tot = na_le16(b + 0x13) ? na_le16(b + 0x13) : na_le32(b + 0x20);
    u32 fatsz = na_le16(b + 0x16) ? na_le16(b + 0x16) : na_le32(b + 0x24);
    u32 rootsec = (rootent * 32 + 511) / 512;
    u32 data = rsvd + nfats * fatsz + rootsec;
    if (!spc || !fatsz || tot <= data) { snprintf(err, cap, "Carte SD : FAT illisible"); return -1; }
    u32 ncl = (tot - data) / spc;
    int fat32 = ncl >= 65525;
    if (ncl < 4085) { snprintf(err, cap, "Carte SD en FAT12 : non prise en charge"); return -1; }
    u32 fat_lba = part + rsvd, data_lba = part + data, cached = 0xFFFFFFFFu;
    u32 isec = 0, guard = 0;
    while (isec < need) {
        if (clus < 2 || clus >= ncl + 2 || ++guard > ncl) { snprintf(err, cap, "Chaine FAT invalide (cluster %u)", (unsigned)clus); return -1; }
        u32 sd = data_lba + (clus - 2) * spc, cnt = spc;
        if (isec + cnt > need) cnt = need - isec;
        if (na_next && na_ext[na_next - 1].sd + na_ext[na_next - 1].cnt == sd) na_ext[na_next - 1].cnt += cnt;   /* (a la suite : meme morceau) */
        else {
            if (na_next >= NATRES_MAXEXT) { snprintf(err, cap, "Jeu trop fragment\xC3\xA9 sur la carte (d\xC3\xA9" "fragmente-la)"); return -1; }
            na_ext[na_next].iso = isec; na_ext[na_next].sd = sd; na_ext[na_next].cnt = cnt; na_next++;
        }
        isec += cnt;
        if (isec >= need) break;
        u32 off = fat32 ? clus * 4 : clus * 2, fs = fat_lba + off / 512;
        if (fs != cached) { if (na_rawsec(fs, fb)) { snprintf(err, cap, "Lecture de la FAT impossible"); return -1; } cached = fs; }
        clus = fat32 ? na_le32(fb + off % 512) & 0x0FFFFFFFu : na_le16(fb + off % 512);
        if ((fat32 && clus >= 0x0FFFFFF8u) || (!fat32 && clus >= 0xFFF8u)) break;
        if ((guard & 255) == 0) service_audio();
    }
    if (isec < need) { snprintf(err, cap, "Fichier plus court que pr\xC3\xA9vu sur la carte"); return -1; }
    return 0;
}

/* verification : 3 endroits de l'ISO, lus par la carte des secteurs et par libfat */
static int nat_map_check(const char *path, long size, char *err, int cap) {
    static u8 a[512] ATTRIBUTE_ALIGN(32), r[512] ATTRIBUTE_ALIGN(32);
    u32 last = (u32)(((u64)size - 1) >> 9);
    u32 at[3] = { 0, last / 2, last };
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, cap, "Jeu introuvable"); return -1; }
    for (int i = 0; i < 3; i++) {
        u32 isec = at[i], sd = 0, ok = 0;
        for (u32 k = 0; k < na_next; k++) if (isec >= na_ext[k].iso && isec - na_ext[k].iso < na_ext[k].cnt) { sd = na_ext[k].sd + (isec - na_ext[k].iso); ok = 1; break; }
        memset(a, 0, 512);
        size_t want = (u64)isec * 512 + 512 <= (u64)size ? 512 : (size_t)((u64)size - (u64)isec * 512);
        if (!ok || fseek(f, (long)isec * 512, SEEK_SET) || fread(a, 1, want, f) != want || na_rawsec(sd, r) || memcmp(a, r, want)) {
            fclose(f); snprintf(err, cap, "Carte des secteurs fausse (secteur %u)", (unsigned)isec); return -1;
        }
    }
    fclose(f);
    return 0;
}

/* ---------- v54 : journal pendant le jeu ----------
   GC Station prepare un fichier d'un secteur ; le programme resident y ecrit son etat
   (compteurs + les 24 dernieres commandes du jeu) pendant le jeu. Au demarrage suivant,
   GC Station le traduit en texte : SD:/gc-mp3/natif/journal-jeu.txt (aussi sur le telephone). */
#define NA_JBIN NA_DIR "/journal-jeu.bin"
#define NA_LOG   NA_DIR "/dernier-lancement.txt"
#define NA_JTXT NA_DIR "/journal-jeu.txt"
static u32 nat_journal_prep(void) {
    if (sd_virtual) return 0;
    mkdir(NA_DIR, 0777);
    static u8 z[512];
    memset(z, 0, sizeof(z));
    FILE *f = fopen(NA_JBIN, "wb");
    if (!f) return 0;
    int ok = fwrite(z, 1, 512, f) == 512;
    fclose(f);
    char e[80];
    if (!ok || nat_map(NA_JBIN, e, sizeof(e)) || na_next != 1) return 0;
    return na_ext[0].sd;
}
static const char *na_cmd_name(u32 c) {
    switch (c >> 24) {
    case 0xA8: return (c & 0xC0) == 0x40 ? "lire l'ID du disque" : "lire";
    case 0xAB: return "chercher"; case 0xE0: return "demander l'erreur"; case 0x12: return "infos lecteur";
    case 0xAC: return "jeu : ecrit DISR";
    case 0xE1: return "audio : jouer"; case 0xE2: return "audio : etat"; case 0xE3: return "arreter le moteur"; case 0xE4: return "audio : tampon";
    default: return "?";
    }
}
/* v67 : code du jeu autour d'une adresse (lu dans main.dol de l'ISO du dernier lancement), pour comprendre ou il bloque */
static void na_code_dump(FILE *f, u32 addr, const char *label) {
    if (addr < 0x80003000u || addr >= 0x81800000u) return;
    static char iso[NAME_LEN];
    iso[0] = 0;
    FILE *lf = fopen(NA_LOG, "r");
    if (lf) { char l[NAME_LEN + 16]; while (fgets(l, sizeof(l), lf)) if (!strncmp(l, "Fichier : ", 10)) { snprintf(iso, sizeof(iso), "%s", l + 10); iso[strcspn(iso, "\r\n")] = 0; break; } fclose(lf); }
    if (!iso[0]) return;
    gdisc_t g;
    if (!gd_open(&g, iso)) return;
    u8 h[0x100], w[96];
    u32 dol = 0;
    int ok = gd_read(&g, 0x420, w, 4);
    if (ok) { dol = be32(w); ok = gd_read(&g, dol, h, sizeof(h)); }
    u32 a0 = (addr - 48) & ~3u, fo = 0;
    for (int i = 0; ok && i < 18 && !fo; i++) {
        u32 off = be32(h + i * 4), at = be32(h + 0x48 + i * 4), sz = be32(h + 0x90 + i * 4);
        if (sz && a0 >= at && a0 + sizeof(w) <= at + sz) fo = dol + off + (a0 - at);
    }
    if (fo && gd_read(&g, fo, w, sizeof(w))) {
        fprintf(f, "  code (%s) :", label);
        for (int i = 0; i < 24; i++) fprintf(f, "%s%s%08X", i % 8 ? " " : "\n    ", a0 + i * 4 == (addr & ~3u) ? ">" : "", (unsigned)be32(w + i * 4));
        fprintf(f, "\n    (debut %08X)\n", (unsigned)a0);
    }
    gd_close(&g);
}
static void nat_journal_convert(void) {
    long len = 0;
    u8 *d = (u8 *)read_file(NA_JBIN, 4096, &len);
    if (!d) return;
    if (len < 512 || be32(d) != NATRES_MAGIC) { free(d); return; }
    FILE *f = fopen(NA_JTXT, "w");
    if (f) {
        u32 H[84];
        for (int i = 0; i < 84; i++) H[i] = be32(d + i * 4);
        int v3 = H[1] >= 3, v4 = H[1] >= 4, v5 = H[1] >= 5, v6 = H[1] >= 6, v7 = H[1] >= 7, v8 = H[1] >= 8, v9 = H[1] >= 9, v10 = H[1] >= 10;   /* (en-tete : 0x80 ... 0x150 (v67) octets) */
        if (!v3) for (int i = 32; i < 84; i++) H[i] = 0;
        if (!v4) for (int i = 40; i < 84; i++) H[i] = 0;
        if (!v5) for (int i = 44; i < 84; i++) H[i] = 0;
        if (!v6) for (int i = 52; i < 84; i++) H[i] = 0;
        if (!v7) for (int i = 56; i < 84; i++) H[i] = 0;
        if (!v8) for (int i = 60; i < 84; i++) H[i] = 0;
        if (!v9) for (int i = 72; i < 84; i++) H[i] = 0;
        if (!v10) for (int i = 76; i < 84; i++) H[i] = 0;
        static const char *const en[] = { "OK", "pas de carte", "la carte ne demarre pas", "commande refusee", "pas de donnees", "hors de l'ISO", "delai depasse" };
        fprintf(f, "GC Station - journal du dernier jeu lance avec Resident\n\n");
        if (v5 && H[44]) {
            static const char *const xn[16] = { "?", "machine check (acces materiel ou memoire invalide)", "acces memoire interdit (DSI)", "code introuvable (ISI)", "?", "alignement", "instruction invalide", "?", "?", "?", "?", "?", "?", "?", "?", "?" };
            fprintf(f, "*** PLANTAGE DU JEU : %s ***\n    a l'adresse %08X (LR %08X), adresse visee %08X (DSISR %08X)\n    -> la console a redemarre toute seule\n\n",
                    xn[H[44] & 15], (unsigned)H[45], (unsigned)H[46], (unsigned)H[47], (unsigned)H[48]);
        }
        fprintf(f, "Carte SD : %s, etape %u, erreur : %s (reponse %02X)\n", H[10] ? "SDHC" : "SD", (unsigned)H[13], H[11] < 7 ? en[H[11]] : "?", (unsigned)H[12]);
        fprintf(f, "Commandes du jeu au faux lecteur : %u   secteurs lus : %u\n", (unsigned)H[19], (unsigned)H[14]);
        fprintf(f, "Derniere commande : %08X (%s)  position %08X  longueur %u\n", (unsigned)H[20], na_cmd_name(H[20]), (unsigned)H[21], (unsigned)H[22]);
        fprintf(f, "Erreurs de lecture SD : %u   reprises reussies : %u\n", (unsigned)H[27], (unsigned)H[28]);
        fprintf(f, "Vrai lecteur (reveil du jeu) : %u commandes lancees, %u fois occupe\n", (unsigned)H[25], (unsigned)H[26]);
        fprintf(f, "Le jeu a ecrit DISR (acquittements) : %u fois, dont %u avec une vraie fin de commande   dernier etat reel DISR : %02X\n\n",
                (unsigned)H[23], (unsigned)H[29], (unsigned)(H[30] & 0xFF));
        if (v3) {
            fprintf(f, "Commandes speciales : audio jouer %u, audio etat %u, chercher %u, autres %u\n", (unsigned)H[32], (unsigned)H[33], (unsigned)H[34], (unsigned)H[35]);
            fprintf(f, "Le jeu tournait encore : %u images (VI), %u lectures des manettes (SI)%s\n", (unsigned)H[36], (unsigned)H[37],
                    H[36] ? "" : "  (crochets pas branches)");
            if (v5) fprintf(f, "Signes de vie : son %u, DSP %u, dessin %u (le journal est reecrit toutes les 3 s tant que le jeu vit)\n", (unsigned)H[49], (unsigned)H[50], (unsigned)H[51]);
            if (v6) fprintf(f, "Lectures en fond (le son continue) : %u, en %u morceaux de 8 Ko ; reveil du vrai lecteur : %u us en moyenne, %u us au pire\n",
                            (unsigned)H[52], (unsigned)H[53], H[53] ? (unsigned)(H[54] / H[53]) : 0u, (unsigned)H[55]);
            if (v8) {
                fprintf(f, "Musique du disque : %u blocs decodes, ajoutee dans %u tampons du son\n", (unsigned)H[66], (unsigned)H[67]);
                fprintf(f, "Carte memoire virtuelle : %s ; %u commandes (derniere %02X), %u secteurs ecrits, %u interruptions\n",
                        H[18] & NRF_CARD ? "active" : "coupee", (unsigned)H[68], (unsigned)H[70], (unsigned)H[69], (unsigned)H[71]);
            }
            if (v9) {
                fprintf(f, "Fonctions du jeu branchees : %s%s%s%s%s%s%s(masque %08X) ; vraies inquiry : %u\n",
                        H[73] & (1u << 21) ? "lecteur " : "", H[73] & (1u << 24) ? "image " : "", H[73] & (1u << 20) ? "manettes " : "",
                        H[73] & (1u << 5) ? "son " : "", H[73] & (1u << 7) ? "DSP " : "", H[73] & (3u << 18) ? "dessin " : "", H[73] ? "" : "aucune ",
                        (unsigned)H[73], (unsigned)H[72]);
                fprintf(f, "Entrees : image (VI) %u, sorties %u ; manettes (SI) %u, sorties %u%s\n", (unsigned)H[74], (unsigned)H[36], (unsigned)H[75], (unsigned)H[37],
                        H[74] > H[36] ? "  <- le jeu est reste bloque DANS sa fonction d'image" : H[73] & (1u << 24) && !H[74] ? "  <- plus aucune image : interruptions coupees par le jeu ?" : "");
            }
            if (v10 && (H[76] || !H[36])) {
                fprintf(f, "Ou en etait le jeu avant sa 1re image : %u echantillons ; dernier code %08X (LR %08X), avant : %08X %08X %08X\n",
                        (unsigned)H[76], (unsigned)H[78], (unsigned)H[77], (unsigned)H[79], (unsigned)H[80], (unsigned)H[81]);
                fprintf(f, "DSP : controle %04X, boites aux lettres %08X%s\n", (unsigned)H[82], (unsigned)H[83],
                        !H[76] ? "\n  (aucun echantillon : le jeu a coupe ses interruptions et tourne en boucle)" : "");
                na_code_dump(f, H[78], "dernier code"); na_code_dump(f, H[77], "LR");
            }
            if (H[38]) fprintf(f, "Retour a GC Station demande (A+Z+START)%s\n", v7 ? (H[56] ? " : .dol de GC Station recharge" : " : redemarrage de la console") : "");
            if (v4) fprintf(f, "Manettes (A+Z+START %s) : %u lues, %u laissees au jeu, touches manette 1 : %04X (%u changements)\n",
                            H[18] & NRF_IGR ? "actif" : "coupe", (unsigned)H[40], (unsigned)H[41], (unsigned)H[42], (unsigned)H[43]);
            fprintf(f, "\n");
        }
        fprintf(f, "Dernieres commandes (la plus ancienne en premier) :\n");
        int rbase = v10 ? 0x150 : v9 ? 0x130 : v8 ? 0x120 : v7 ? 0xF0 : v6 ? 0xE0 : v5 ? 0xD0 : v4 ? 0xB0 : v3 ? 0xA0 : 0x80, nring = (512 - rbase) / 16;
        for (int k = 0; k < nring; k++) {
            const u8 *e = d + rbase + k * 16;
            u32 c = be32(e), o = be32(e + 4), l = be32(e + 8), in = be32(e + 12);
            if (!c && !o && !l && !in) continue;
            if ((c >> 24) == 0xAC) {                      /* (le jeu acquitte : il a ete reveille) */
                fprintf(f, "  %08X %-20s valeur %02X  vrai DISR avant %02X  faux DISR apres %02X\n", (unsigned)c, na_cmd_name(c), (unsigned)(c & 0xFF), (unsigned)(o & 0xFF), (unsigned)(l & 0xFF));
                continue;
            }
            fprintf(f, "  %08X %-20s pos %08X  long %6u  %s%s%s%sDISR %02X  reel %02X\n", (unsigned)c, na_cmd_name(c), (unsigned)o, (unsigned)l,
                    in & 1 ? "ERREUR LECTURE  " : "", in & 8 ? "CARTE PAS PRETE  " : "", in & 2 ? "LECTEUR OCCUPE  " : "", in & 4 ? "" : "PAS DE REVEIL  ",
                    (unsigned)((in >> 8) & 0xFF), (unsigned)((in >> 16) & 0xFF));
        }
        long ll = 0;                                      /* (et le journal du lancement, ecrit avant le saut) */
        char *lt = (char *)read_file(NA_LOG, 16384, &ll);
        if (lt) { fprintf(f, "\n---------- dernier-lancement.txt ----------\n"); fwrite(lt, 1, ll, f); free(lt); }
        fclose(f);
    }
    memset(d, 0, 512);                                    /* (traduit : on ne le refait pas au prochain demarrage) */
    FILE *b = fopen(NA_JBIN, "r+b");
    if (b) { fwrite(d, 1, 512, b); fclose(b); }
    free(d);
}

/* test du programme resident sur la console, sans lancer de jeu : il demarre la carte SD
   tout seul et relit 3 endroits de l'ISO. La memoire qu'il occupe est sauvee puis remise. */
static const char *na_res_names[] = { "OK", "pas de carte", "la carte ne d\xC3\xA9marre pas", "commande refus\xC3\xA9" "e", "pas de donn\xC3\xA9" "es", "hors de l'ISO", "d\xC3\xA9lai d\xC3\xA9pass\xC3\xA9" };
static int nat_res_test(const char *path, long size, char *msg, int cap) {
#ifdef GEKKO
    static u8 want[3][512] ATTRIBUTE_ALIGN(32), got[512] ATTRIBUTE_ALIGN(32);
    u32 offs[3] = { 0, (u32)(size / 2) & ~511u, (u32)(size - 512) & ~511u };
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(msg, cap, "Jeu introuvable"); return -1; }
    for (int i = 0; i < 3; i++) if (fseek(f, (long)offs[i], SEEK_SET) || fread(want[i], 1, 512, f) != 512) { fclose(f); snprintf(msg, cap, "Lecture du jeu impossible"); return -1; }
    fclose(f);
    if (sizeof(natres_bin) > NATRES_SIZE) { snprintf(msg, cap, "Programme r\xC3\xA9sident trop gros"); return -1; }
    u8 *save = (u8 *)memalign(32, NATRES_SIZE);
    if (!save) { snprintf(msg, cap, "Pas assez de m\xC3\xA9moire"); return -1; }
    if ((u32)save < NATRES_AT + NATRES_SIZE && (u32)save + NATRES_SIZE > NATRES_AT) { free(save); snprintf(msg, cap, "M\xC3\xA9moire du r\xC3\xA9sident occup\xC3\xA9" "e"); return -1; }   /* (v52) */
    stop_track();
    fatUnmount("sd");                                     /* (v52 : la FAT est ecrite sur la carte AVANT qu'on la prenne) */
    u32 level = IRQ_Disable();                            /* (personne d'autre sur le bus EXI pendant le test) */
    memcpy(save, (void *)NATRES_AT, NATRES_SIZE);
    memset((void *)NATRES_AT, 0, NATRES_SIZE);
    memcpy((void *)NATRES_AT, natres_bin, sizeof(natres_bin));
    natres_hdr_t *h = (natres_hdr_t *)NATRES_AT;
    int ok = h->magic == NATRES_MAGIC && h->end <= NATRES_AT + NATRES_SIZE;
    int r = -1, bad = -1;
    if (ok) {
        h->chan = sd_slot == 0 ? 2 : sd_slot == 1 ? 0 : 1;
        h->dev = 0; h->freq = 4;
        h->n_ext = na_next;
        memcpy((void *)h->ext, na_ext, na_next * sizeof(natres_ext_t));
        DCFlushRange((void *)NATRES_AT, NATRES_SIZE);
        ICInvalidateRange((void *)NATRES_AT, NATRES_SIZE);
        r = ((natres_init_fn)h->init)();
        for (int i = 0; i < 3 && r == 0; i++) {
            memset(got, 0, 512);
            if (((natres_read_fn)h->read)(got, offs[i], 512)) r = -1;
            else if (memcmp(got, want[i], 512)) { bad = i; r = -2; }
        }
    }
    u32 err = ok ? h->err : 99, stage = ok ? h->stage : 0, sdhc = ok ? h->sdhc : 0, r1 = ok ? h->r1 : 0, dev = ok ? h->dev : 0;
    memcpy((void *)NATRES_AT, save, NATRES_SIZE);
    DCFlushRange((void *)NATRES_AT, NATRES_SIZE);
    ICInvalidateRange((void *)NATRES_AT, NATRES_SIZE);
    IRQ_Restore(level);
    free(save);
    /* la carte a ete redemarree : on remonte la FAT pour que GC Station continue */
    if (!mount_sd()) { sd_ok = 0; }
    if (!ok) snprintf(msg, cap, "programme r\xC3\xA9sident abim\xC3\xA9");
    else if (r == 0) snprintf(msg, cap, "OK (carte %s, p\xC3\xA9riph\xC3\xA9rique %u)", sdhc ? "SDHC" : "SD", (unsigned)dev);
    else if (r == -2) snprintf(msg, cap, "donn\xC3\xA9" "es diff\xC3\xA9rentes (endroit %d)", bad + 1);
    else snprintf(msg, cap, "%s (\xC3\xA9tape %u, r\xC3\xA9ponse %02X)", err < 7 ? na_res_names[err] : "?", (unsigned)stage, (unsigned)r1);
    return r == 0 ? 0 : -1;
#else
    snprintf(msg, cap, "(simulateur : pas de test du r\xC3\xA9sident)");
    return 0;
#endif
}

/* ======================= v49 : etape 3b = brancher le faux lecteur =======================
   Chaque "lwz rD,reg(rA)" / "stw rS,reg(rA)" du jeu qui vise un registre du lecteur est
   remplace par un saut vers un aiguillage, ecrit ici en code machine PowerPC :
     - garde tous les registres du jeu sur sa pile (r0, r2-r31, LR, CR, CTR, XER) ;
     - appelle di_load(reg) ou di_store(reg, valeur) dans le programme resident ;
     - (lecture) range le resultat a la place de rD ; remet tout ; revient juste apres.
   Le saut "b" ne touche pas a LR : ca marche aussi dans les fonctions sans pile. */
#define PPC_D(op, d, a, imm)  ((u32)(op) << 26 | (u32)(d) << 21 | (u32)(a) << 16 | ((u32)(imm) & 0xFFFFu))
#define PPC_STW(s, a, d)      PPC_D(36, s, a, d)
#define PPC_LWZ(d, a, o)      PPC_D(32, d, a, o)
#define PPC_ADDI(d, a, i)     PPC_D(14, d, a, i)
#define NA_FRAME 0x100

static void na_wbe32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 na_slot(u32 r) { return r == 0 ? 0x08 : 0x0C + (r - 2) * 4; }   /* place du registre r sur la pile */

/* ecrit l'aiguillage (mots big-endian) en w ; rend le nombre de mots, 0 = instruction non geree */
/* v52 : aussi lwzu/stwu (rA avance) et lhz/sth (demi-registre : lecture, ou lecture-modification-ecriture) */
#define PPC_RLW(op, s, a, sh, mb, me) ((u32)(op) << 26 | (u32)(s) << 21 | (u32)(a) << 16 | (u32)(sh) << 11 | (u32)(mb) << 6 | (u32)(me) << 1)
static const char *na_tramp_why(u32 ins) {             /* NULL = on sait brancher cette instruction */
    u32 op = ins >> 26, rt = (ins >> 21) & 31, ra = (ins >> 16) & 31;
    if (op != 32 && op != 33 && op != 36 && op != 37 && op != 40 && op != 44) return "instruction inconnue";
    if ((op == 32 || op == 33 || op == 40) && rt == 1) return "lecture dans r1 (pile)";
    if ((op == 33 || op == 37) && (ra == 0 || ra == 1 || (op == 33 && ra == rt))) return "forme u invalide";
    return NULL;
}
/* v65 : acces EXI : mots seulement (lwz lwzu stw stwu lwzx stwx), jamais la pile */
static const char *na_xtramp_why(u32 ins) {
    u32 op = ins >> 26, rt = (ins >> 21) & 31, ra = (ins >> 16) & 31, rb = (ins >> 11) & 31, xo = (ins >> 1) & 0x3FF;
    int x = op == 31 && (xo == 23 || xo == 151);
    if (!x && op != 32 && op != 33 && op != 36 && op != 37) return op == 40 || op == 44 || (op == 31 && (xo == 279 || xo == 407)) ? "demi-mot EXI" : "instruction inconnue";
    if (ra == 1 || (x && rb == 1)) return "adresse sur la pile";
    if (!x && ra == 0) return "adresse fixe";
    if ((op == 32 || op == 33 || (x && xo == 23)) && rt == 1) return "lecture dans r1 (pile)";
    if ((op == 33 && ra == rt) || (op == 37 && ra == 0)) return "forme u invalide";
    return NULL;
}
static int na_tramp(u8 *w, u32 at, u32 site, u32 ins, u32 reg, u32 fn_load, u32 fn_store) {
    u32 op = ins >> 26, rt = (ins >> 21) & 31, ra = (ins >> 16) & 31, rb = (ins >> 11) & 31;
    s32 simm = (s16)(ins & 0xFFFF);
    int dyn = (reg & NA_XREG) != 0;                       /* v65 : EXI : r3 = adresse, calculee pendant le jeu */
    if (dyn ? na_xtramp_why(ins) != NULL : na_tramp_why(ins) != NULL) return 0;
    int xf = op == 31;
    int load = op == 32 || op == 33 || op == 40 || (xf && ((ins >> 1) & 0x3FF) == 23), half = op == 40 || op == 44;
    u32 ral = reg & ~3u, k = reg & 3u;
    if (half && (k & 1)) return 0;
    u32 c[64]; int n = 0;
#define NA_CALL(fn) do { c[n++] = PPC_D(15, 12, 0, (fn) >> 16); c[n++] = PPC_D(24, 12, 12, (fn) & 0xFFFF); \
                         c[n++] = 0x7D8903A6u; c[n++] = 0x4E800421u; } while (0)   /* lis/ori r12 ; mtctr ; bctrl */
    c[n++] = PPC_D(37, 1, 1, -NA_FRAME);                 /* stwu r1,-0x100(r1) */
    c[n++] = PPC_STW(0, 1, 0x08);
    c[n++] = PPC_D(47, 2, 1, 0x0C);                      /* stmw r2,0x0C(r1) */
    c[n++] = 0x7C0802A6u; c[n++] = PPC_STW(0, 1, 0x88);  /* mflr r0 */
    c[n++] = 0x7C000026u; c[n++] = PPC_STW(0, 1, 0x8C);  /* mfcr r0 */
    c[n++] = 0x7C0902A6u; c[n++] = PPC_STW(0, 1, 0x90);  /* mfctr r0 */
    c[n++] = 0x7C0102A6u; c[n++] = PPC_STW(0, 1, 0x94);  /* mfxer r0 */
#define NA_EA() do { if (xf) { c[n++] = PPC_LWZ(3, 1, na_slot(rb)); if (ra) { c[n++] = PPC_LWZ(0, 1, na_slot(ra)); c[n++] = 0x7C630214u; } } \
                     else { c[n++] = PPC_LWZ(3, 1, na_slot(ra)); c[n++] = PPC_ADDI(3, 3, simm); } } while (0)   /* (add r3,r3,r0) */
    if (load) {
        if (dyn) NA_EA(); else c[n++] = PPC_ADDI(3, 0, ral);   /* li r3,reg */
        NA_CALL(fn_load);
        if (half) c[n++] = PPC_RLW(21, 3, 3, k ? 0 : 16, 16, 31);   /* rlwinm : le demi-registre voulu */
        c[n++] = PPC_STW(3, 1, na_slot(rt));             /* resultat -> rD (sur la pile) */
    } else {
        if (half) {                                      /* sth : lire, remplacer la moitie, ecrire */
            c[n++] = PPC_ADDI(3, 0, ral);
            NA_CALL(fn_load);
            c[n++] = rt == 1 ? PPC_ADDI(4, 1, NA_FRAME) : PPC_LWZ(4, 1, na_slot(rt));
            c[n++] = PPC_RLW(20, 4, 3, k ? 0 : 16, k ? 16 : 0, k ? 31 : 15);   /* rlwimi r3,r4 */
            c[n++] = 0x7C641B78u;                        /* mr r4,r3 */
        } else c[n++] = rt == 1 ? PPC_ADDI(4, 1, NA_FRAME) : PPC_LWZ(4, 1, na_slot(rt));   /* valeur -> r4 */
        if (dyn) NA_EA(); else c[n++] = PPC_ADDI(3, 0, ral);
        NA_CALL(fn_store);
    }
#undef NA_EA
#undef NA_CALL
    c[n++] = PPC_LWZ(0, 1, 0x94); c[n++] = 0x7C0103A6u;  /* mtxer */
    c[n++] = PPC_LWZ(0, 1, 0x90); c[n++] = 0x7C0903A6u;  /* mtctr */
    c[n++] = PPC_LWZ(0, 1, 0x8C); c[n++] = 0x7C0FF120u;  /* mtcrf 0xFF */
    c[n++] = PPC_LWZ(0, 1, 0x88); c[n++] = 0x7C0803A6u;  /* mtlr */
    c[n++] = PPC_LWZ(0, 1, 0x08);
    c[n++] = PPC_D(46, 2, 1, 0x0C);                      /* lmw r2,0x0C(r1) */
    c[n++] = PPC_ADDI(1, 1, NA_FRAME);
    if (op == 33 || op == 37) c[n++] = PPC_ADDI(ra, ra, simm);   /* forme "u" : rA = adresse visee */
    u32 back = site + 4, here = at + (u32)n * 4;
    c[n++] = 0x48000000u | ((back - here) & 0x03FFFFFCu);   /* b site+4 */
    for (int i = 0; i < n; i++) { w[i * 4] = (u8)(c[i] >> 24); w[i * 4 + 1] = (u8)(c[i] >> 16); w[i * 4 + 2] = (u8)(c[i] >> 8); w[i * 4 + 3] = (u8)c[i]; }
    return n;
}

/* pose les aiguillages : dol = main.dol (copie a modifier), res = image du resident (NATRES_AT) ;
   rend le nombre d'acces branches, et dans *skipped ceux qu'on n'a pas su brancher */
#define NA_MAXSKIP 16
static u32 na_skip_at[NA_MAXSKIP], na_skip_ins[NA_MAXSKIP];
static const char *na_skip_why[NA_MAXSKIP];
static int na_nskip = 0;
static u32 na_xfo[NA_MAXHIT], na_xold[NA_MAXHIT];
static char na_xwhy[120] = "";
static int na_xdone = 0;
static int na_patch(const nat_info_t *in, u8 *dol, u8 *res, int *skipped, int *card_on) {
    u32 at = (be32(res + 0x3C) + 31) & ~31u;             /* (h->end, lu en big-endian) */
    u32 dl = be32(res + 0x40), ds = be32(res + 0x44);   /* (h->di_load, h->di_store) */
    u32 xl = be32(res + 0xF0), xs = be32(res + 0xF4);   /* v65 : (h->exi_load, h->exi_store) */
    int done = 0;
    *skipped = 0; na_nskip = 0; na_xdone = 0; na_xwhy[0] = 0;
    /* passe 0 : le lecteur (tous, sinon le jeu n'est pas lance) ; passe 1 : la carte (tout ou rien) */
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1 && !*card_on) break;
        u32 at0 = at, cov = 0;
        int nx = 0;
        const char *xfail = NULL;
        u32 xfail_at = 0;
        for (int i = 0; i < in->nhit; i++) {
            int isx = (in->hit_reg[i] & NA_XREG) != 0;
            if (isx != pass) continue;
            u32 a = in->hit_addr[i], fo = 0;
            int sec = -1;
            for (int k = 0; k < in->nsec; k++) if (a >= in->sec_addr[k] && a - in->sec_addr[k] < in->sec_sz[k]) { sec = k; fo = in->sec_off[k] + (a - in->sec_addr[k]); }
            const char *why = sec < 0 ? "hors des sections" : be32(dol + fo) != in->hit_ins[i] ? "instruction differente"
                            : at + 64 * 4 > NATRES_AT + NATRES_SIZE ? "plus de place"
                            : isx ? na_xtramp_why(in->hit_ins[i]) : na_tramp_why(in->hit_ins[i]);
            s32 d = (s32)(at - a);
            if (!why && (d >= 0x02000000 || d < -0x02000000)) why = "trop loin";
            int n = why ? 0 : na_tramp(res + (at - NATRES_AT), at, a, in->hit_ins[i], in->hit_reg[i], isx ? xl : dl, isx ? xs : ds);
            if (!why && !n) why = "demi-registre impair";
            if (why && isx) { xfail = why; xfail_at = a; break; }
            if (why) {
                if (na_nskip < NA_MAXSKIP) { na_skip_at[na_nskip] = a; na_skip_ins[na_nskip] = in->hit_ins[i]; na_skip_why[na_nskip] = why; na_nskip++; }
                (*skipped)++; continue;
            }
            if (isx) {
                na_xfo[nx] = fo; na_xold[nx] = be32(dol + fo); nx++;
                if (!(in->hit_reg[i] & NA_XIDX)) cov |= 1u << (((in->hit_reg[i] & 0x7F) >> 2) + (in->hit_store[i] ? 8 : 0));
            }
            u32 b = 0x48000000u | ((u32)d & 0x03FFFFFCu);    /* b aiguillage */
            na_wbe32(dol + fo, b);
            at += ((u32)n * 4 + 31) & ~31u;
            done++;
        }
        if (pass == 1) {
            /* (au minimum : CSR lu et ecrit, CR ecrit, DATA lu : sinon une partie irait a la vraie carte) */
            const u32 need = (1u << 0) | (1u << 8) | (1u << 11) | (1u << 4);
            if (!xfail && in->x_lost) xfail = "trop d'acces EXI";
            if (!xfail && (cov & need) != need) xfail = "acces EXI incomplets";
            if (xfail) {
                for (int j = 0; j < nx; j++) na_wbe32(dol + na_xfo[j], na_xold[j]);   /* on rend au jeu ses acces : la vraie carte, comme avant */
                at = at0; done -= nx; *card_on = 0;
                snprintf(na_xwhy, sizeof(na_xwhy), "coupee pour ce jeu : %s (%08X, trouves %03X)", xfail, (unsigned)xfail_at, (unsigned)cov);
            } else na_xdone = nx;
        }
    }
    return done;
}

/* fiche d'un jeu : R */
static void nat_run(int k) {
    if (k < 0 || k >= gl_n) return;
    gl_status("Analyse native du jeu...");
    static nat_info_t in;
    char rep[NAME_LEN];
    int ok = nat_analyse(gl[k].path, &in);
    gl_busy[0] = 0;
    if (!ok) { gl_note(in.err); crumb("natif : %s", in.err); return; }
    nat_write_report(&in, gl[k].path, rep, sizeof(rep));
    /* etape 3a : l'ISO sur la carte, et le programme resident qui la lit */
    char me[120] = "", rt[120] = "";
    int mok = -1, rok = -1;
    if (gl[k].ciso) snprintf(me, sizeof(me), "fichier .ciso : pas encore pris en charge par Resident");
    else {
        gl_status("Analyse native : position du jeu sur la carte...");
        mok = nat_map(gl[k].path, me, sizeof(me));
        if (!mok) mok = nat_map_check(gl[k].path, gl[k].size, me, sizeof(me));
        if (!mok) {
            snprintf(me, sizeof(me), "OK, %u morceau%s", (unsigned)na_next, na_next > 1 ? "x" : "");
            gl_status("Analyse native : test du lecteur SD maison...");
            rok = nat_res_test(gl[k].path, gl[k].size, rt, sizeof(rt));
        }
        gl_busy[0] = 0;
    }
    FILE *f = rep[0] ? fopen(rep, "a") : NULL;
    if (f) {
        fprintf(f, "\n[Etape 3 : lecture sur la carte SD]\nPosition du jeu sur la carte : %s\n", me);
        for (u32 i = 0; !mok && i < na_next && i < 12; i++)
            fprintf(f, "  ISO %08X -> carte %08X  (%u secteurs)\n", (unsigned)na_ext[i].iso, (unsigned)na_ext[i].sd, (unsigned)na_ext[i].cnt);
        if (!mok && na_next > 12) fprintf(f, "  (... %u morceaux en tout)\n", (unsigned)na_next);
        if (rt[0]) fprintf(f, "Lecteur SD maison (programme resident, %u octets) : %s\n", (unsigned)sizeof(natres_bin), rt);
        fclose(f);
    }
    char m[200];
    if (in.problems) snprintf(m, sizeof(m), "Analyse : %s", in.err);
    else if (mok) snprintf(m, sizeof(m), "Analyse : %s", me);
    else if (rok) snprintf(m, sizeof(m), "Lecteur SD maison : %s", rt);
    else snprintf(m, sizeof(m), "Analyse OK, lecteur SD maison OK (rapport : gc-mp3/natif/%s.txt)", in.id);
    gl_note(m);
    if (rep[0]) nat_qr_open(in.id);                          /* v51 : QR code vers le rapport */
    crumb("natif %s : %d acces DI, %d fonctions, %d problemes, carte %d, resident %d", in.id, in.nhit, in.nfunc, in.problems, mok, rok);
}

/* ======================= v51 : etape 2 = demarrage natif =======================
   GC Station fait lui-meme le travail de l'apploader :
   - lit main.dol, bi2.bin et la FST dans l'ISO (ou le .ciso) ;
   - les range tout en haut de la memoire, puis coupe tout (son, video, interruptions) ;
   - remplit la memoire basse avec les infos du disque ;
   - le petit programme gl_stub (gclib.c) recopie les sections a leur place et saute
     au point d'entree du jeu.
   Etape 3 : le programme resident est installe tout en haut, et les acces du jeu au
   lecteur DVD sont branches sur lui (faux lecteur) : le jeu lit l'ISO sur la carte SD.
   Journal (ecrit avant le saut) : SD:/gc-mp3/natif/dernier-lancement.txt */
#define NA_BI2   0x2000u                      /* taille de bi2.bin */

static const char *na_adapter(void) {
    return sd_virtual ? "aucun (SD virtuelle)" : sd_slot == 0 ? "SD2SP2 (port serie 2)" : sd_slot == 1 ? "slot A (SD Gecko ?)" : sd_slot == 2 ? "slot B (SD Gecko ?)" : "inconnu";
}

/* v55 : le vrai lecteur DVD sert de "reveil" (sa fin de commande = interruption pour le jeu).
   Swiss peut le laisser en reset (bit 2 de PI 0xCC003024 a 0) : il ne repond alors a rien,
   le jeu attend 10 s puis affiche "Une erreur est survenue". On le libere et on verifie
   qu'il repond a une commande "inquiry" (interruptions coupees, sans libogc). */
static char na_drv[220] = "";
#ifdef GEKKO
static void nat_ld_tick(void);                          /* (ecran de chargement, plus bas) */
static int na_inq(volatile u32 *di, u8 *buf, u32 ms) {  /* 0x10 fin, 0x04 erreur (repond quand meme), -1 rien */
    di[0] = 0x54u;                                        /* (v64 : masques coupes : pas d'interruption pour libogc) */
    DCInvalidateRange(buf, 32);
    di[2] = 0x12000000u; di[3] = 0; di[4] = 32;
    di[5] = (u32)MEM_VIRTUAL_TO_PHYSICAL(buf); di[6] = 32;
    di[7] = 3;
    u64 t0 = now_ms();
    while (di[7] & 1) {
        nat_ld_tick();
        if (now_ms() - t0 > ms) {                         /* (rien : on interrompt la commande) */
            di[0] = 1u;
            u64 t1 = now_ms();
            while ((di[7] & 1) && now_ms() - t1 < 200) ;
            di[0] = 0x54u;
            return -1;
        }
    }
    u32 sr = di[0];
    di[0] = 0x54u;
    DCInvalidateRange(buf, 32);
    return (int)(sr & 0x54u);
}
static void na_drive_wake(void) {
    volatile u32 *di = (volatile u32 *)0xCC006000u, *pi = (volatile u32 *)0xCC003000u;
    static u8 inq[32] ATTRIBUTE_ALIGN(32);
    u32 p9 = pi[9];
    int held = !(p9 & 4u), r1 = -2, r2 = -2;
    u32 wait = 0;
    if (!held) r1 = na_inq(di, inq, 800);
    if (held || r1 < 0) {                                 /* reset du lecteur, puis on attend qu'il reponde */
        u32 level = IRQ_Disable();
        pi[9] = (pi[9] & ~4u) | 1u;
        u64 t = now_ms(); while (now_ms() - t < 2) ;
        pi[9] = pi[9] | 5u;
        IRQ_Restore(level);
        u64 t0 = now_ms();
        do r2 = na_inq(di, inq, 1500); while (r2 < 0 && now_ms() - t0 < 7000);
        wait = (u32)(now_ms() - t0);
    }
    di[1] = 4u;                                           /* (couvercle : acquitte, sans interruption) */
    int ok = r1 >= 0 || r2 >= 0, mot = -2;
    if (ok) {                                             /* v64 : moteur arrete (E3) : le lecteur reste silencieux */
        di[0] = 0x54u; di[2] = 0xE3000000u; di[3] = 0; di[4] = 0; di[7] = 1;
        u64 t0 = now_ms();
        while ((di[7] & 1) && now_ms() - t0 < 3000) nat_ld_tick();
        mot = di[7] & 1 ? -1 : (int)(di[0] & 0x54u);
        di[0] = 0x54u;
    }
    snprintf(na_drv, sizeof(na_drv), "%s (PI 3024 = %08X%s ; inquiry : %s ; apres reset : %s, %u ms ; date %02X%02X%02X%02X ; moteur arrete : %s)",
             ok ? "REPOND" : "NE REPOND PAS", (unsigned)p9, held ? ", lecteur tenu en reset" : "",
             r1 == -2 ? "pas essaye" : r1 < 0 ? "rien" : r1 & 0x10 ? "fin OK" : "erreur",
             r2 == -2 ? "pas besoin" : r2 < 0 ? "rien" : r2 & 0x10 ? "fin OK" : "erreur", (unsigned)wait,
             inq[4], inq[5], inq[6], inq[7], mot == -2 ? "non" : mot < 0 ? "pas de reponse" : mot & 0x10 ? "oui" : "erreur");
}
#else
static void na_drive_wake(void) { snprintf(na_drv, sizeof(na_drv), "(simulateur)"); }
#endif

/* ---------- v65 : ecran de chargement de Resident, dans le style de GC Station (degrade,
   jaquette du jeu avec halo, panneau arrondi, barre lumineuse, etapes cochees) ---------- */
static char nat_ld_title[96], nat_ld_step[96];
static int nat_ld_pct = 0, nat_ld_from = 0, nat_ld_to = 0, nat_ld_k = -1;
static u64 nat_ld_t0 = 0, nat_ld_last = 0;
#ifdef NATIF_BENCH
static void nat_draw_loading(void) {}
#else
static void nat_draw_loading(void) {
    char b[120], fv[120];
    f32 t = (f32)(now_ms() % 100000u) / 1000.0f;
    gx_vgrad(0, 0, 640, 480, RGBA(20, 10, 44, 255), RGBA(4, 2, 12, 255));
    gx_circle(168, 232, 230, RGBA(120, 60, 255, 60), RGBA(120, 60, 255, 0));   /* (lueur derriere la jaquette) */
    /* jaquette */
    const f32 cx = 72, cy = 104, cw = 192, chh = 256;
    gx_glow(cx, cy, cw, chh, 10, RGBA(160, 100, 255, 170), 6);
    if (nat_ld_k >= 0 && nat_ld_k < gl_n) gl_draw_cover(nat_ld_k, cx, cy, cw, chh, 1);
    else {
        gx_rrect_fill(cx, cy, cw, chh, 10, RGBA(44, 26, 88, 255), RGBA(22, 12, 46, 255));
        gx_circle(cx + cw / 2, cy + chh / 2, 62, RGBA(200, 190, 230, 255), RGBA(120, 110, 160, 255));   /* (un disque) */
        gx_circle(cx + cw / 2, cy + chh / 2, 14, RGBA(22, 12, 46, 255), RGBA(22, 12, 46, 255));
    }
    gx_rrect_stroke(cx - 1, cy - 1, cw + 2, chh + 2, 10, 1.5f, RGBA(220, 190, 255, 200));
    /* panneau */
    const f32 px = 288, py = 104, pw = 304, ph = 256;
    gx_rrect_fill(px, py, pw, ph, 12, RGBA(38, 22, 76, 235), RGBA(18, 10, 38, 235));
    gx_rrect_stroke(px, py, pw, ph, 12, 1.0f, RGBA(130, 90, 230, 140));
    gx_rrect_fill(px + 18, py + 16, 92, 20, 10, RGBA(170, 100, 255, 255), RGBA(120, 60, 235, 255));   /* badge */
    gx_text_c(F_SMALL, px + 18 + 46, py + 31, "RESIDENT", RGBA(255, 255, 255, 255));
    gx_text_r(F_SMALL, px + pw - 18, py + 31, TR("Lancement sans Swiss"), RGBA(150, 136, 196, 255));
    text_display(nat_ld_title, b, sizeof(b));
    fit_text(F_MED, b, (int)pw - 36, fv, sizeof(fv));
    gx_text1(F_MED, px + 18, py + 70, fv, RGBA(255, 255, 255, 255));
    /* barre */
    const f32 bx = px + 18, by = py + 90, bw = pw - 36, bh = 8;
    f32 fw = bw * (f32)nat_ld_pct / 100.0f;
    gx_rrect_fill(bx, by, bw, bh, 4, RGBA(16, 8, 34, 255), RGBA(30, 18, 60, 255));
    if (fw >= 8) {
        gx_glow(bx, by, fw, bh, 4, RGBA(180, 110, 255, 150), 4);
        gx_rrect_fill(bx, by, fw, bh, 4, RGBA(210, 150, 255, 255), RGBA(130, 70, 245, 255));
        f32 sh = fmodf(t * 160.0f, fw + 60.0f) - 30.0f;           /* (reflet qui court) */
        if (sh > 2 && sh < fw - 10) gx_hgrad(bx + sh, by + 1, 8, bh - 2, RGBA(255, 255, 255, 0), RGBA(255, 255, 255, 120));
        gx_circle_glow(bx + fw - 4, by + bh / 2, 5, RGBA(230, 200, 255, 200), 5);
    }
    snprintf(b, sizeof(b), "%d %%", nat_ld_pct);
    gx_text_r(F_SMALL, bx + bw, by + 26, b, RGBA(220, 205, 255, 255));
    fit_text(F_SMALL, nat_ld_step, (int)bw - 50, fv, sizeof(fv));
    gx_text1(F_SMALL, bx, by + 26, fv, RGBA(180, 166, 226, 255));
    /* etapes */
    static const char *const st[5] = { "Analyse du jeu", "Carte SD : jeu et sauvegardes", "Lecture du jeu", "Pr\xC3\xA9paration du lecteur", "D\xC3\xA9marrage" };
    static const int at[6] = { 0, 10, 22, 40, 100, 101 };
    for (int i = 0; i < 5; i++) {
        f32 ry = by + 52 + i * 21;
        int done = nat_ld_pct >= at[i + 1], cur = !done && nat_ld_pct >= at[i];
        if (done) {
            gx_circle(bx + 7, ry - 4, 7, RGBA(190, 130, 255, 255), RGBA(140, 80, 245, 255));
            gx_tri(bx + 3, ry - 4, bx + 6, ry - 1, bx + 6, ry + 1, RGBA(255, 255, 255, 255));   /* (coche) */
            gx_poly4(bx + 5, ry - 1, bx + 10, ry - 8, bx + 12, ry - 7, bx + 6, ry + 1,
                     RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
        } else if (cur) {
            f32 p = 0.5f + 0.5f * sinf(t * 6.0f);
            gx_circle_glow(bx + 7, ry - 4, 6, RGBA(200, 150, 255, (u8)(120 + 120 * p)), 4);
            gx_ring(bx + 7, ry - 4, 5, 7, RGBA(210, 170, 255, 255));
        } else gx_ring(bx + 7, ry - 4, 5.5f, 7, RGBA(90, 74, 140, 255));
        gx_text1(F_SMALL, bx + 22, ry + 1, TR(st[i]), done ? RGBA(200, 190, 235, 255) : cur ? RGBA(255, 255, 255, 255) : RGBA(110, 98, 150, 255));
    }
    /* bas */
    if (nat_igr) gx_text_c(F_SMALL, 320, 400, TR("Pendant le jeu : A + Z + START = retour \xC3\xA0 GC Station"), RGBA(150, 136, 196, 255));
    if (nat_card) gx_text_c(F_SMALL, 320, 420, TR("Sauvegardes : carte m\xC3\xA9moire virtuelle (slot A, sur la carte SD)"), RGBA(120, 108, 166, 255));
    gx_text_r(F_SMALL, 616, 460, "GC STATION  \xC2\xB7  RESIDENT", RGBA(142, 124, 196, 255));
}
#endif
static void nat_ld(const char *step, int pct) {         /* une etape (et dessine tout de suite) */
    snprintf(nat_ld_step, sizeof(nat_ld_step), "%s", TR(step));
    nat_ld_pct = nat_ld_from = nat_ld_to = pct;
    nat_ld_on = 1; nat_ld_last = now_ms();
    crumb("resident : %s", step);
    draw();
}
static void nat_ld_span(const char *step, int from, int to) {   /* etape longue : avance avec le temps (7 s) */
    nat_ld(step, from);
    nat_ld_from = from; nat_ld_to = to; nat_ld_t0 = now_ms();
}
static void nat_ld_tick(void) {
    if (!nat_ld_on) return;
    u64 n = now_ms();
    if (n - nat_ld_last < 40) return;
    nat_ld_last = n;
    if (nat_ld_to > nat_ld_from) { u64 e = n - nat_ld_t0; if (e > 7000) e = 7000; nat_ld_pct = nat_ld_from + (int)((nat_ld_to - nat_ld_from) * e / 7000); }
    draw();
}

/* .dol de GC Station sur la carte (pour y revenir depuis le jeu : A+Z+START, plantage) */
#ifdef NATIF_BENCH
static const char *nat_self_path(void);               /* (banc d'essai) */
#else
static const char *nat_self_path(void) { up_pick_target(); return up_target; }
#endif
static natres_ext_t na_ret[NATRES_RETMAX];
static u32 na_ret_n = 0, na_ret_size = 0;
static void nat_ret_prep(void) {
    na_ret_n = 0; na_ret_size = 0;
    const char *p = nat_self_path();
    struct stat st;
    if (!p || !p[0] || sd_virtual || stat(p, &st) != 0 || st.st_size < 0x100) return;
    u8 h[0x100];
    FILE *f = fopen(p, "rb");
    if (!f) return;
    int ok = fread(h, 1, sizeof(h), f) == sizeof(h);
    fclose(f);
    if (!ok) return;
    for (int i = 0; i < 18; i++) {                        /* (le .dol ne doit pas toucher au resident en se chargeant) */
        u32 off = be32(h + i * 4), addr = be32(h + 0x48 + i * 4), sz = be32(h + 0x90 + i * 4);
        if (sz && (addr < 0x80003000u || addr + (u64)sz > NATRES_AT || off + (u64)sz > (u64)st.st_size)) return;
    }
    char e[80];
    if (nat_map(p, e, sizeof(e)) || na_next > NATRES_RETMAX) return;
    memcpy(na_ret, na_ext, na_next * sizeof(natres_ext_t));
    na_ret_n = na_next; na_ret_size = (u32)st.st_size;
}

/* v65 : carte memoire virtuelle (slot A) : un fichier sur la carte SD. Absent : il est cree
   (2 Mo, 251 blocs, vide : le jeu propose alors de formater la carte, comme une carte neuve).
   Une image de carte deja faite (Dolphin, GCMM : 59 a 2043 blocs) peut aussi etre posee la. */
#define NA_CARD_DIR  GCD "/resident"
#define NA_CARD      NA_CARD_DIR "/carte-A.raw"
#define NA_CARD_NEW  (2u << 20)
static natres_ext_t na_card[NATRES_CARDMAX];
static u32 na_card_n = 0, na_card_size = 0;
static char na_card_msg[128] = "";
static int nat_card_prep(void) {
    na_card_n = 0; na_card_size = 0;
    if (!nat_card) { snprintf(na_card_msg, sizeof(na_card_msg), "coupee (reglage)"); return 0; }
    if (sd_virtual) { snprintf(na_card_msg, sizeof(na_card_msg), "pas de vraie carte SD"); return 0; }
    struct stat st;
    mkdir(NA_CARD_DIR, 0777);
    if (stat(NA_CARD, &st) != 0) {
        nat_ld("Pr\xC3\xA9paration de la carte m\xC3\xA9moire virtuelle", 12);
        static u8 ff[32768];
        memset(ff, 0xFF, sizeof(ff));
        FILE *f = fopen(NA_CARD ".part", "wb");
        int ok = f != NULL;
        for (u32 o = 0; ok && o < NA_CARD_NEW; o += sizeof(ff)) { if (fwrite(ff, 1, sizeof(ff), f) != sizeof(ff)) ok = 0; nat_ld_tick(); service_audio(); }
        if (f && fclose(f) != 0) ok = 0;
        if (!ok || rename(NA_CARD ".part", NA_CARD) != 0 || stat(NA_CARD, &st) != 0) {
            remove(NA_CARD ".part"); snprintf(na_card_msg, sizeof(na_card_msg), "creation du fichier impossible"); return 0;
        }
    }
    u32 sz = (u32)st.st_size;
    if (sz < 0x80000u || sz > 0x1000000u || (sz & (sz - 1))) {      /* (on ne touche pas a un fichier inconnu) */
        snprintf(na_card_msg, sizeof(na_card_msg), "carte-A.raw : taille %u octets, pas une image de carte (512 Ko a 16 Mo)", (unsigned)sz); return 0;
    }
    char e[80];
    if (nat_map(NA_CARD, e, sizeof(e))) { snprintf(na_card_msg, sizeof(na_card_msg), "%s", e); return 0; }
    if (na_next > NATRES_CARDMAX) { snprintf(na_card_msg, sizeof(na_card_msg), "carte-A.raw trop morcele sur la carte SD (%u morceaux)", (unsigned)na_next); return 0; }
    memcpy(na_card, na_ext, na_next * sizeof(natres_ext_t));
    na_card_n = na_next; na_card_size = sz;
    snprintf(na_card_msg, sizeof(na_card_msg), "active (gc-mp3/resident/carte-A.raw, %u blocs)", (unsigned)(sz / 8192u - 5u));
    return 1;
}

static const char *nat_boot(int k) {
    if (k < 0 || k >= gl_n) return "Jeu introuvable";
    static nat_info_t in;
    snprintf(nat_ld_title, sizeof(nat_ld_title), "%s", gl[k].title);
    nat_ld_k = k;
    nat_ld("Analyse du jeu", 4);
    if (!nat_analyse(gl[k].path, &in)) return in.err;
    if (in.problems) return in.err;
    if (in.fst_max < in.fst_sz || in.fst_max > (4u << 20)) return "FST : taille maximale invalide";
    /* memoire du haut, une fois le jeu lance : [ jeu ... | FST | bi2 | resident ] 0x81800000 */
    u32 fst_dest = in.fst_dest;
    u32 bi2_dest = NATRES_AT - NA_BI2;
    u32 dol_len = in.dol_len;
    /* programme resident (pas encore appele par le jeu : il sera branche a l'etape 3b) */
    char merr[120] = "";
    nat_ld("Position du jeu sur la carte SD", 10);
    nat_ret_prep();                                        /* v64 : retour a GC Station (avant la carte de l'ISO : meme tableau) */
    int card_on = nat_card_prep();                         /* v65 : carte memoire virtuelle */
    u32 log_sec = nat_journal_prep();                      /* v54 : journal du jeu (un secteur de la carte) */
    int res = !gl[k].ciso && !nat_map(gl[k].path, merr, sizeof(merr)) && sizeof(natres_bin) <= NATRES_SIZE;
    u32 total = ((dol_len + 31) & ~31u) + NA_BI2 + ((in.fst_sz + 31) & ~31u) + (res ? NATRES_SIZE : 0);
    /* zone d'attente : sous 0x81700000 (au-dessus vit la pile de GC Station jusqu'au saut),
       au-dessus du jeu, et sous ce que le jeu recevra en haut (FST, bi2, resident) */
    u32 top = fst_dest < 0x81700000u - 0x400u ? fst_dest : 0x81700000u - 0x400u;   /* (v52 : jamais par-dessus la FST) */
    if (total + in.hi + 32 > top) return "Pas assez de m\xC3\xA9moire pour ce jeu (main.dol trop gros)";
    u32 stage = (top - total) & ~31u;
    if (stage < in.hi + 32 || stage + total > fst_dest) return "Pas assez de m\xC3\xA9moire pour ce jeu (main.dol trop gros)";
    u8 *buf = (u8 *)memalign(32, total);
    if (!buf) return "Pas assez de m\xC3\xA9moire libre dans GC Station";
    memset(buf, 0, total);
    u32 o_dol = 0, o_bi2 = (dol_len + 31) & ~31u, o_fst = o_bi2 + NA_BI2, o_res = o_fst + ((in.fst_sz + 31) & ~31u);
    if (res) {                                            /* image du resident, deja reglee (en-tete : natres.h) */
        u8 *r = buf + o_res;
        memcpy(r, natres_bin, sizeof(natres_bin));
        u32 eo = be32(r + 0x24) - NATRES_AT;
        if (be32(r) != NATRES_MAGIC || be32(r + 0x3C) > NATRES_AT + NATRES_SIZE || eo + na_next * 12 > NATRES_SIZE) res = 0;
        else {
            na_wbe32(r + 0x14, sd_slot == 0 ? 2 : sd_slot == 1 ? 0 : 1);   /* canal EXI */
            na_wbe32(r + 0x18, 0); na_wbe32(r + 0x1C, 4);                   /* peripherique, 16 MHz */
            na_wbe32(r + 0x20, na_next);
            u32 co = be32(r + 0xFC) - NATRES_AT;                            /* (natres_hdr_t.card_tab) */
            if (card_on && co + na_card_n * 12 > NATRES_SIZE) card_on = 0;
            na_wbe32(r + 0x48, NRF_REAL_IRQ | (nat_igr ? NRF_IGR : 0) | (card_on ? NRF_CARD : 0));
            if (card_on) {                                                  /* v65 : carte memoire virtuelle */
                na_wbe32(r + 0xF8, na_card_n); na_wbe32(r + 0x100, na_card_size); na_wbe32(r + 0x104, na_card_size >> 17);   /* (ID = taille en Mbits) */
                for (u32 i = 0; i < na_card_n; i++) {
                    na_wbe32(r + co + i * 12, na_card[i].iso); na_wbe32(r + co + i * 12 + 4, na_card[i].sd); na_wbe32(r + co + i * 12 + 8, na_card[i].cnt);
                }
            }
            na_wbe32(r + 0x60, log_sec);                                    /* (natres_hdr_t.log_sec) */
            u32 ro = be32(r + 0xE4) - NATRES_AT;                            /* (natres_hdr_t.ret_tab) */
            if (na_ret_n && ro + na_ret_n * 12 <= NATRES_SIZE) {             /* v64 : .dol de GC Station */
                na_wbe32(r + 0xE0, na_ret_n); na_wbe32(r + 0xE8, na_ret_size);
                for (u32 i = 0; i < na_ret_n; i++) {
                    na_wbe32(r + ro + i * 12, na_ret[i].iso); na_wbe32(r + ro + i * 12 + 4, na_ret[i].sd); na_wbe32(r + ro + i * 12 + 8, na_ret[i].cnt);
                }
            } else na_ret_n = 0;
            for (u32 i = 0; i < na_next; i++) {
                na_wbe32(r + eo + i * 12, na_ext[i].iso); na_wbe32(r + eo + i * 12 + 4, na_ext[i].sd); na_wbe32(r + eo + i * 12 + 8, na_ext[i].cnt);
            }
        }
    }
    gdisc_t d;
    nat_ld("Lecture du jeu", 22);
    int ok = gd_open(&d, gl[k].path);
    if (ok) ok = gd_read(&d, in.dol_off, buf + o_dol, dol_len);
    if (ok) ok = gd_read(&d, 0x440, buf + o_bi2, NA_BI2);
    if (ok) { nat_ld("Lecture du jeu", 34); ok = gd_read(&d, in.fst_off, buf + o_fst, in.fst_sz); }
    u8 hdr[0x20];
    if (ok) ok = gd_read(&d, 0, hdr, sizeof(hdr));
    gd_close(&d);
    if (!ok) { free(buf); return "Lecture du jeu impossible (carte SD)"; }
    /* etape 3b : le jeu parle au faux lecteur (programme resident) au lieu du vrai */
    int patched = 0, skipped = 0;
    if (res) {
        int was = card_on;
        patched = na_patch(&in, buf + o_dol, buf + o_res, &skipped, &card_on);
        if (was && !card_on) {                              /* (carte refusee pour ce jeu : flag et table retires) */
            u8 *r = buf + o_res;
            na_wbe32(r + 0x48, be32(r + 0x48) & ~NRF_CARD);
            na_wbe32(r + 0xF8, 0);
            snprintf(na_card_msg, sizeof(na_card_msg), "%s", na_xwhy);
        }
    }
    if (res && (skipped || in.di_lost)) {                /* un acces oublie = vrai lecteur interroge : on ne lance pas */
        free(buf);
        mkdir(NA_DIR, 0777);
        FILE *lf = fopen(NA_LOG, "w");                    /* (v52 : le journal dit lesquels et pourquoi) */
        if (lf) {
            fprintf(lf, "GC Station - Resident : jeu PAS lance\nJeu : %s (%s)\n", in.title, in.id);
            fprintf(lf, "Acces au lecteur : %d trouves, %d branches, %d non branches\n", in.ntotal - in.nexi, patched - na_xdone, skipped + in.di_lost);
            if (in.di_lost) fprintf(lf, "  %d au-dela de la limite de %d\n", in.di_lost, NA_MAXHIT);
            for (int i = 0; i < na_nskip; i++) fprintf(lf, "  %08X  instruction %08X : %s\n", (unsigned)na_skip_at[i], (unsigned)na_skip_ins[i], na_skip_why[i]);
            fclose(lf);
        }
        static char e[120];
        snprintf(e, sizeof(e), "Resident : %d acc\xC3\xA8s au lecteur non branch\xC3\xA9s, jeu pas lanc\xC3\xA9", skipped + in.di_lost);
        return e;
    }
    /* table de copie pour gl_stub : {destination, source, taille} */
    u32 tab[18 * 3 + 12], n = 0;
    for (int i = 0; i < 18; i++) {
        u32 off = be32(buf + i * 4), addr = be32(buf + 0x48 + i * 4), sz = be32(buf + 0x90 + i * 4);
        if (!sz) continue;
        tab[n * 3] = addr; tab[n * 3 + 1] = stage + o_dol + off; tab[n * 3 + 2] = sz; n++;
    }
    tab[n * 3] = bi2_dest; tab[n * 3 + 1] = stage + o_bi2; tab[n * 3 + 2] = NA_BI2; n++;
    if (in.fst_sz) { tab[n * 3] = fst_dest; tab[n * 3 + 1] = stage + o_fst; tab[n * 3 + 2] = in.fst_sz; n++; }
    if (res) { tab[n * 3] = NATRES_AT; tab[n * 3 + 1] = stage + o_res; tab[n * 3 + 2] = NATRES_SIZE; n++; }
    /* v55 : le vrai lecteur doit repondre (sa fin de commande reveille le jeu) */
    if (res) { stop_track(); nat_ld_span("Pr\xC3\xA9paration du lecteur", 40, 92); na_drive_wake(); }
    nat_ld("D\xC3\xA9marrage", 100);
    /* journal, ecrit AVANT le saut (apres, plus rien ne peut ecrire sur la carte) */
    mkdir(NA_DIR, 0777);
    FILE *f = fopen(NA_LOG, "w");
    if (f) {
        fprintf(f, "GC Station - Resident (lancement sans Swiss)\n");
        fprintf(f, "Jeu : %s  %s\nFichier : %s\nAdaptateur SD : %s\n\n", in.id, in.title, gl[k].path, na_adapter());
        fprintf(f, "Donnees rangees en %08X (%u octets)\nbi2.bin -> %08X   FST -> %08X (%u / %u octets)\n",
                (unsigned)stage, (unsigned)total, (unsigned)bi2_dest, (unsigned)fst_dest, (unsigned)in.fst_sz, (unsigned)in.fst_max);
        for (u32 i = 0; i < n; i++) fprintf(f, "  copie %08X <- %08X  %u octets\n", (unsigned)tab[i * 3], (unsigned)tab[i * 3 + 1], (unsigned)tab[i * 3 + 2]);
        fprintf(f, "Point d'entree : %08X\n", (unsigned)in.entry);
        fprintf(f, "Programme resident : %s\n", res ? "installe" : merr[0] ? merr : "non installe");
        fprintf(f, "Journal du jeu : %s\n", log_sec ? "oui (gc-mp3/natif/journal-jeu.txt au prochain demarrage de GC Station)" : "non");
        fprintf(f, "Faux lecteur DVD : %d acces branches, %d non branches (sur %d trouves)\n", patched - na_xdone, skipped, in.ntotal - in.nexi);
        fprintf(f, "Vrai lecteur : %s\n", na_drv[0] ? na_drv : "pas teste");
        fprintf(f, "Retour a GC Station (A+Z+START, plantage) : %s\n", na_ret_n ? "pret (le .dol de GC Station est relu sur la carte)" : "redemarrage de la console");
        fprintf(f, "Carte memoire virtuelle (slot A) : %s ; %d acces EXI trouves, %d branches\n", na_card_msg[0] ? na_card_msg : "non", in.nexi, na_xdone);
        fprintf(f, "Memoire basse avant : 28=%08X 2C=%08X CC=%08X F8=%08X FC=%08X\n",
#ifdef GEKKO
                (unsigned)*(volatile u32 *)0x80000028, (unsigned)*(volatile u32 *)0x8000002C, (unsigned)*(volatile u32 *)0x800000CC,
                (unsigned)*(volatile u32 *)0x800000F8, (unsigned)*(volatile u32 *)0x800000FC
#else
                0u, 0u, 0u, 0u, 0u
#endif
                );
        fprintf(f, "\nSaut vers le jeu... Ce que tu vois a l'ecran ensuite, c'est le resultat :\n"
                   "- le jeu demarre et joue = Resident OK !\n"
                   "- message d'erreur de disque du jeu = le faux lecteur n'a pas suffi ;\n"
                   "- ecran noir fige / retour au menu = a me signaler.\n");
        fclose(f);
    }
    crumb("natif : saut vers %s (entree %08X)", in.id, (unsigned)in.entry);
#ifdef GEKKO
    if (settings_dirty) save_settings();
    stop_track(); stop_station();
    AUDIO_StopDMA();
    fatUnmount("sd");
    VIDEO_SetBlack(TRUE); VIDEO_Flush(); VIDEO_WaitVSync(); VIDEO_WaitVSync();
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    u32 level = IRQ_Disable();                            /* plus d'interruptions : on ecrase GC Station */
    (void)level;
    memmove((void *)stage, buf, total);
    DCFlushRange((void *)stage, total);
    /* memoire basse (on garde ce que le BIOS a deja mis : taille memoire, type de console,
       vitesses, mode video) */
    volatile u32 *lm = (volatile u32 *)0x80000000;
    memcpy((void *)0x80000000, hdr, 0x20);                /* ID du jeu, disque, version... */
    lm[0x20 / 4] = 0x0D15EA5Eu;                           /* demarrage normal */
    lm[0x24 / 4] = 1;
    if (!lm[0x28 / 4]) lm[0x28 / 4] = 0x01800000u;        /* 24 Mo */
    lm[0x30 / 4] = 0;                                     /* bas de l'arene : calcule par le jeu */
    lm[0x34 / 4] = fst_dest;                              /* haut de l'arene (le jeu ne va pas au-dessus) */
    lm[0x38 / 4] = fst_dest;                              /* FST */
    lm[0x3C / 4] = in.fst_max;
    lm[0xF0 / 4] = 0x01800000u;                           /* memoire simulee */
    lm[0xF4 / 4] = bi2_dest;                              /* bi2.bin */
    if (!lm[0xF8 / 4]) lm[0xF8 / 4] = 0x09A7EC80u;        /* vitesse du bus */
    if (!lm[0xFC / 4]) lm[0xFC / 4] = 0x1CF7C580u;        /* vitesse du processeur */
    DCFlushRange((void *)0x80000000, 0x100);
    u32 *t = (u32 *)GL_TAB_AT;
    for (u32 i = 0; i < n * 3; i++) t[i] = tab[i];
    memcpy((void *)GL_STUB_AT, gl_stub, sizeof(gl_stub));
    DCFlushRange((void *)0x80001800, 0x1800);
    ICInvalidateRange((void *)GL_STUB_AT, sizeof(gl_stub));
    ((void (*)(u32 *, u32, u32))GL_STUB_AT)(t, n, in.entry);
    for (;;) ;
#else
    if (getenv("NATIF_DUMP")) {                          /* (banc d'essai : image de la memoire a lancer) */
        FILE *df = fopen(getenv("NATIF_DUMP"), "wb");
        if (df) {
            u8 w[4];
            na_wbe32(w, in.entry); fwrite(w, 1, 4, df);
            na_wbe32(w, n); fwrite(w, 1, 4, df);
            for (u32 i = 0; i < n; i++) {
                na_wbe32(w, tab[i * 3]); fwrite(w, 1, 4, df); na_wbe32(w, tab[i * 3 + 2]); fwrite(w, 1, 4, df);
                fwrite(buf + (tab[i * 3 + 1] - stage), 1, tab[i * 3 + 2], df);
            }
            fwrite(hdr, 1, 0x20, df);
            fclose(df);
        }
    }
    printf("NATIF %s stage=%08x total=%u bi2=%08x fst=%08x entree=%08x copies=%u\n",
           in.id, (unsigned)stage, (unsigned)total, (unsigned)bi2_dest, (unsigned)fst_dest, (unsigned)in.entry, (unsigned)n);
    free(buf);
    return "(simulateur : lancement natif simul\xC3\xA9)";
#endif
}

/* ---------- v51 : le rapport sur le telephone (QR code) ----------
   Le serveur de la telecommande (onglet TELEPHONE) donne /natif (liste) et /natif/<ID>.txt.
   Apres une analyse, la fiche montre un QR code vers le rapport. */
static int na_qr_on = 0, na_qr_ok = 0;
static char na_qr_id[8] = "", na_qr_url[96] = "";
static u8 na_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
static void nat_qr_open(const char *id) {
    snprintf(na_qr_id, sizeof(na_qr_id), "%s", id);
    na_qr_on = 1; na_qr_ok = 0; na_qr_url[0] = 0;
    if (tp_state != PS_ON) { tp_wanted = 1; net_start_async(); }      /* (le serveur demarre tout seul) */
}
static int nat_qr_input(u32 down) {                     /* 1 = touche prise par la fenetre */
    if (!na_qr_on) return 0;
    if (down & (PAD_BUTTON_B | PAD_BUTTON_A | PAD_TRIGGER_R)) na_qr_on = 0;
    return 1;
}
static void nat_draw_qr(void) {
    if (!na_qr_on) return;
    if (!na_qr_ok && tp_state == PS_ON && ip_str[0]) {
        if (PHONE_PORT == 80) snprintf(na_qr_url, sizeof(na_qr_url), "http://%s/natif/%s.txt", ip_str, na_qr_id);
        else snprintf(na_qr_url, sizeof(na_qr_url), "http://%s:%d/natif/%s.txt", ip_str, PHONE_PORT, na_qr_id);
        static u8 tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
        na_qr_ok = qrcodegen_encodeText(na_qr_url, tmp, na_qr, qrcodegen_Ecc_MEDIUM, 1, 8, qrcodegen_Mask_AUTO, true) ? 1 : -1;
    }
    const f32 w = 440, h = 300, x = 100, y = 96;
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
    gx_rrect_fill(x, y, w, h, 12, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
    draw_panel(x, y, w, h);
    gx_text_c(F_MED, 320, y + 30, "Rapport d'analyse sur ton t\xC3\xA9l\xC3\xA9phone", RGBA(255, 255, 255, 255));
    if (na_qr_ok > 0) {
        draw_qr_buf(na_qr, 320 - 90, y + 44, 180);
        gx_text_c(F_SMALL, 320, y + 246, na_qr_url, RGBA(255, 210, 120, 255));
        gx_text_c(F_SMALL, 320, y + 266, "Scanne le QR code (m\xC3\xAAme Wi-Fi que la GameCube)", RGBA(190, 180, 235, 255));
    } else if (tp_state == PS_ERR || (net_phase == NET_DONE && !net_ok)) {
        gx_text_c(F_SMALL, 320, y + 140, "Pas de r\xC3\xA9seau : le rapport est dans SD:/gc-mp3/natif", RGBA(255, 160, 120, 255));
    } else {
        gx_text_c(F_SMALL, 320, y + 140, "D\xC3\xA9marrage du serveur...", RGBA(190, 180, 235, 255));
    }
    gx_text_c(F_SMALL, 320, y + h - 10, "B : fermer", RGBA(255, 210, 120, 255));
}

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
#define NA_SREG  0x4000u                     /* v122 : (hit_reg) registre des manettes (SI, 0xCC006400) ; avec NA_XREG */
#define NA_SIO   0xCC006400u
#define NA_DDYN  0x2000u                     /* v133 : (hit_reg) acces au lecteur trouve seulement par le suivi large (adresse recalculee pendant le jeu) */
#define NA_MAXHIT 400                        /* (1080 Avalanche : 83 acces au lecteur ; v65 : + EXI) */
static void nat_qr_open(const char *id);      /* v51 : QR code vers le rapport (plus bas) */
static const char *na_tramp_why(u32 ins);     /* v52 (plus bas) */
static const char *na_xtramp_why(u32 ins);    /* v65 (plus bas) */
static void nat_ld_tick(void);                /* v72 : ecran de chargement (plus bas) */

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
    int nsi, si_lost;                         /* v122 : acces aux manettes (SI) trouves, au-dela de la limite */
    u32 hit_addr[NA_MAXHIT];
    u16 hit_reg[NA_MAXHIT];
    u8 hit_store[NA_MAXHIT];
    u32 hit_ins[NA_MAXHIT];                   /* l'instruction elle-meme (pour la remplacer) */
    int nfunc;
    u32 func_addr[NA_MAXHIT];
    int problems;
    char err[160];
    u32 ws_fn, ws_ins;                        /* v121 : GXSetProjection (16:9 force) : adresse, 1re instruction */
    int ws_n;                                 /* (nombre de fonctions qui lui ressemblent : 1 = sur) */
} nat_info_t;

static const char *na_adapter(void);
static const char *na_reg_name(u32 r) {
    if (r & NA_SREG) return "SI (manettes)";
    if (r & NA_DDYN) return "lecteur (adresse calculee pendant le jeu)";
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
    if (reg & NA_SREG) {                                  /* v122 : manettes : a part (pas comptees avec le lecteur) */
        in->nsi++;
        if (in->nhit >= NA_MAXHIT - 40) { in->si_lost++; return; }   /* (place gardee pour le lecteur) */
    } else {
        in->ntotal++;
        if (reg & NA_XREG) in->nexi++;
    }
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

/* v133 : acces au lecteur vus par le suivi large (na_xscan : il ne s'arrete pas aux sauts, suit add / mr et les
   formes indexees) ; ceux que la recherche simple n'a pas trouves sont branches "en secours" : l'aiguillage
   recalcule l'adresse pendant le jeu (un faux positif ne change rien : meme acces, fait par le resident) */
#define NA_DCMAX 64
static u32 na_dc_at[NA_DCMAX], na_dc_ins[NA_DCMAX];
static u8 na_dc_st[NA_DCMAX];
static int na_dc_n = 0;
static int na_dd_found = 0, na_dd_done = 0, na_dd_skip = 0;   /* (journal) */

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
    {   /* v133 : lecteur, mots seulement (lwz lwzu stw stwu lwzx stwx) */
        int wop = op == 32 || op == 33 || op == 36 || op == 37 || (op == 31 && (xo == 23 || xo == 151));
        if (acc && wop && ea >= NA_DI && ea < NA_DI + 0x40 && na_dc_n < NA_DCMAX) {
            na_dc_at[na_dc_n] = base + o; na_dc_ins[na_dc_n] = ins; na_dc_st[na_dc_n] = (u8)store; na_dc_n++;
        }
    }
    if (acc && ea >= NA_EXI0 && ea < NA_EXI0 + 0x40) {
        na_hit(in, txt, base, base + o, NA_XREG | (idx ? NA_XIDX : (ea - NA_EXI0) % 0x14), store, ins);
    } else if (acc == 1 && ea >= NA_SIO && ea < NA_SIO + 0x40) {   /* v122 : manettes (mots seulement) */
        na_hit(in, txt, base, base + o, NA_XREG | NA_SREG | (idx ? NA_XIDX : (ea - NA_SIO)), store, ins);
    }
}

/* Cherche les acces aux registres du lecteur. On suit, instruction par instruction,
   les registres qui contiennent une adresse connue (lis rX,0xCC00 puis addi / ori),
   et on note les lectures / ecritures dont l'adresse tombe dans 0xCC006000..0xCC00603F.
   L'etat est oublie a chaque branchement (simple, mais suffisant pour un rapport). */
static void na_scan_text2(nat_info_t *in, const u8 *txt, u32 base, u32 len);
static void na_scan_text(nat_info_t *in, const u8 *txt, u32 base, u32 len) {
    int h0 = in->nhit;
    na_dc_n = 0;
    na_scan_text2(in, txt, base, len);
    for (int c = 0; c < na_dc_n; c++) {                      /* v133 : acces au lecteur oublies par la recherche simple */
        int seen = 0;
        for (int i = h0; i < in->nhit && !seen; i++) if (in->hit_addr[i] == na_dc_at[c]) seen = 1;
        if (seen) continue;
        na_dd_found++;
        na_hit(in, txt, base, na_dc_at[c], NA_DDYN | NA_XIDX, na_dc_st[c], na_dc_ins[c]);
    }
}
static void na_scan_text2(nat_info_t *in, const u8 *txt, u32 base, u32 len) {
    u32 val[32]; u32 known = 0;
    u32 xv[32], xm = 0;                                       /* v65 : suivi EXI (voir na_xscan) */
    for (u32 o = 0; o + 4 <= len; o += 4) {
        if ((o & 0x3FFFF) == 0) service_audio();                  /* (v50 : la musique continue pendant l'analyse) */
        if ((o & 0x3FFF) == 0) nat_ld_tick();                       /* (v72 : l'ecran de chargement aussi) */
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

/* v121 : 16:9 force. Toutes les images 3D passent par GXSetProjection(matrice, type) de la bibliotheque
   graphique du jeu : elle lit dans la matrice (r3) les cases 0x00 0x14 0x28 0x2C, puis selon le type (r4,
   compare a 1 = orthographique) 0x0C 0x1C ou 0x08 0x18. On la reconnait a ces lectures, sans rien savoir
   d'autre du jeu. Pulse y pose un aiguillage : pour une perspective, la 1re ligne de la matrice (largeur)
   est multipliee par 3/4 sur une copie : l'image 4:3 etiree par une tele 16:9 retrouve ses proportions, et
   on voit plus large. Les menus en 2D (orthographiques) restent etires, comme avec le reglage de Swiss. */
static void na_ws_scan(nat_info_t *in, const u8 *txt, u32 base, u32 len) {
    for (u32 o = 0; o + 4 <= len; o += 4) {
        u32 ins = be32(txt + o);
        if (ins != 0x2C040001u && ins != 0x28040001u) continue;      /* cmpwi / cmplwi r4,1 */
        u32 f = na_func_start(txt, base, base + o);
        if (base + o - f > 0x40) continue;
        u32 seen = 0;
        for (u32 a = f; a < f + 0x80 && a + 4 <= base + len; a += 4) {
            u32 w = be32(txt + (a - base)), op = w >> 26, ra = (w >> 16) & 31, d = w & 0xFFFF;
            if ((op == 48 || op == 32) && ra == 3 && d < 0x40 && !(d & 3)) seen |= 1u << (d >> 2);
            if (w == 0x4E800020u) break;                                  /* blr */
        }
        const u32 need = 1u << 0 | 1u << 5 | 1u << 10 | 1u << 11 | 1u << 2 | 1u << 6 | 1u << 3 | 1u << 7;
        if ((seen & need) != need) continue;
        if (in->ws_n && in->ws_fn == f) continue;
        in->ws_n++;
        in->ws_fn = f; in->ws_ins = be32(txt + (f - base));
    }
}

/* analyse complete ; retourne 0 si le disque est illisible */
static int nat_analyse(const char *path, nat_info_t *in) {
    memset(in, 0, sizeof(*in));
    na_dd_found = 0;                                      /* v133 */
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
                service_audio(); nat_ld_tick();
            }
            if (okr) { na_scan_text(in, t, addr, sz); na_ws_scan(in, t, addr, sz); }
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
    fprintf(f, "GC Station - analyse native (pour Pulse)\n");
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
    fprintf(f, "\n(Ce sont elles que Pulse branche sur son faux lecteur ; les acces EXI, sur la carte memoire virtuelle.)\n");
    if (in->stream) fprintf(f, "Ce jeu lit sa musique en streaming sur le disque : Pulse la decode et l'ajoute au son (v65).\n");
    fprintf(f, "\n");
    fprintf(f, "Bilan : %s%s\n", in->problems ? "probleme : " : "OK, Pulse peut essayer ce jeu", in->problems ? in->err : "");
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

/* v97 : place libre et taille de la carte, lues dans le secteur FSInfo de la FAT32 (2 secteurs, instantane).
   Avant : statvfs(), qui fait relire TOUTE la FAT par libfat : sur une carte de 1 To (30 millions de
   clusters), des dizaines de secondes a chaque ouverture d'un dossier de l'onglet FICHIERS.
   La geometrie est gardee ; le compteur de place libre est relu a chaque appel (libfat le tient a jour). */
#ifndef NATIF_BENCH
static int sd_space_fast(unsigned long long *tot, unsigned long long *fre) {
    static int geo = 0;                                  /* 0 = pas encore lu, 1 = ok, -1 = pas une FAT32 lisible */
    static u32 g_part, g_fsi, g_ncl, g_spc;
    static u8 b[512] ATTRIBUTE_ALIGN(32);
    if (geo < 0 || sd_virtual || !sd_ok) return 0;
    if (!geo) {
        geo = -1;
        if (na_rawsec(0, b)) return 0;
        u32 part = 0;
        if (!na_is_fat(b)) {
            u8 mbr[64]; memcpy(mbr, b + 0x1BE, 64);
            int found = 0;
            for (int i = 0; i < 4 && !found; i++) { u32 lba = na_le32(mbr + i * 16 + 8); if (lba && !na_rawsec(lba, b) && na_is_fat(b)) { part = lba; found = 1; } }
            if (!found) return 0;
        }
        u32 spc = b[0x0D], rsvd = na_le16(b + 0x0E), nfats = b[0x10];
        u32 totsec = na_le16(b + 0x13) ? na_le16(b + 0x13) : na_le32(b + 0x20);
        u32 fatsz = na_le16(b + 0x16) ? na_le16(b + 0x16) : na_le32(b + 0x24);
        u32 fsi = na_le16(b + 0x30);
        if (!spc || na_le16(b + 0x16) || !fsi || fsi == 0xFFFF) return 0;      /* (FAT32 seulement) */
        u32 data = rsvd + nfats * fatsz;
        if (totsec <= data) return 0;
        g_part = part; g_fsi = fsi; g_spc = spc; g_ncl = (totsec - data) / spc;
        geo = 1;
    }
    if (na_rawsec(g_part + g_fsi, b)) return 0;
    u32 freecl = na_le32(b + 0x1E8);
    if (na_le32(b) != 0x41615252u || na_le32(b + 0x1E4) != 0x61417272u || freecl > g_ncl) return 0;   /* (0xFFFFFFFF = inconnu) */
    *tot = (unsigned long long)g_ncl * g_spc * 512; *fre = (unsigned long long)freecl * g_spc * 512;
    return 1;
}

/* place libre / taille : FSInfo si possible, sinon statvfs une seule fois par session (lent) */
static int sd_space(unsigned long long *tot, unsigned long long *fre) {
    static int slow_known = 0;
    static unsigned long long s_tot, s_fre;
    if (sd_space_fast(tot, fre)) return 1;
    if (!sd_ok) return 0;
    if (!slow_known) {
        struct statvfs sv;
        if (statvfs(SD_ROOT "/", &sv) != 0) return 0;
        s_tot = (unsigned long long)sv.f_blocks * sv.f_frsize; s_fre = (unsigned long long)sv.f_bavail * sv.f_frsize;
        slow_known = 1;
    }
    *tot = s_tot; *fre = s_fre;
    return 1;
}
#endif

/* v121 : lecture brute d'une autre carte que celle de GC Station (carte SD du GC Loader) */
static int (*na_dev_rd)(u32 sec, void *buf) = NULL;
static int na_rs(u32 sec, void *buf) { return na_dev_rd ? na_dev_rd(sec, buf) : na_rawsec(sec, buf); }
/* morceaux de l'ISO sur la carte ; 0 = OK */
static int nat_map(const char *path, char *err, int cap) {
    static u8 b[512] ATTRIBUTE_ALIGN(32), fb[512] ATTRIBUTE_ALIGN(32);
    struct stat st;
    na_next = 0;
    if (sd_virtual && !na_dev_rd) { snprintf(err, cap, "Pas de vraie carte SD"); return -1; }
    if (stat(path, &st) != 0) { snprintf(err, cap, "Jeu introuvable"); return -1; }
    u32 clus = (u32)st.st_ino;
    u32 need = (u32)(((u64)st.st_size + 511) >> 9);
    /* partition FAT : secteur 0, sinon la 1re partition de la table (MBR) qui est en FAT */
    u32 part = 0;
    if (na_rs(0, b)) { snprintf(err, cap, "Lecture brute de la carte impossible"); return -1; }
    if (!na_is_fat(b)) {
        int found = 0;
        u8 mbr[64]; memcpy(mbr, b + 0x1BE, 64);
        for (int i = 0; i < 4 && !found; i++) {
            u32 lba = na_le32(mbr + i * 16 + 8);
            if (lba && !na_rs(lba, b) && na_is_fat(b)) { part = lba; found = 1; }
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
        if (fs != cached) { if (na_rs(fs, fb)) { snprintf(err, cap, "Lecture de la FAT impossible"); return -1; } cached = fs; }
        clus = fat32 ? na_le32(fb + off % 512) & 0x0FFFFFFFu : na_le16(fb + off % 512);
        if ((fat32 && clus >= 0x0FFFFFF8u) || (!fat32 && clus >= 0xFFF8u)) break;
        if ((guard & 255) == 0) { service_audio(); nat_ld_tick(); }
    }
    if (isec < need) { snprintf(err, cap, "Fichier plus court que pr\xC3\xA9vu sur la carte"); return -1; }
    return 0;
}

/* v121 : .ciso (en-tete "CISO", taille de bloc, carte des blocs presents ; les blocs presents sont a la
   suite dans le fichier, a partir de 0x8000). Les morceaux du FICHIER sur la carte (nat_map) deviennent des
   morceaux de l'ISO : un bloc present = ses secteurs dans le fichier ; un bloc absent = des zeros
   (NATRES_ZERO), jusqu'a la taille d'un vrai disque. */
#define NA_DISC_SECS 2851520u                 /* (1 459 978 240 octets : un disque GameCube) */
static natres_ext_t na_fext[NATRES_MAXEXT];
static u32 na_iso_secs = 0;                   /* taille de l'ISO vue par le jeu (secteurs) */
static int na_ext_add(u32 iso, u32 sd, u32 cnt) {
    if (!cnt) return 0;
    natres_ext_t *l = na_next ? &na_ext[na_next - 1] : NULL;
    if (l && l->iso + l->cnt == iso && ((sd == NATRES_ZERO && l->sd == NATRES_ZERO) || (sd != NATRES_ZERO && l->sd != NATRES_ZERO && l->sd + l->cnt == sd))) { l->cnt += cnt; return 0; }
    if (na_next >= NATRES_MAXEXT) return -1;
    na_ext[na_next].iso = iso; na_ext[na_next].sd = sd; na_ext[na_next].cnt = cnt; na_next++;
    return 0;
}
static int nat_map_game(const char *path, int ciso, char *err, int cap) {
    if (nat_map(path, err, cap)) return -1;
    if (!ciso) {                                          /* v122 : apres la fin du fichier (.nkit.iso, plus court) : des zeros */
        na_iso_secs = na_next ? na_ext[na_next - 1].iso + na_ext[na_next - 1].cnt : 0;
        if (na_iso_secs && na_iso_secs < NA_DISC_SECS && na_next < NATRES_MAXEXT) {
            u32 e0 = na_iso_secs;
            if (na_ext_add(e0, NATRES_ZERO, NA_DISC_SECS - e0)) { snprintf(err, cap, "Jeu trop fragment\xC3\xA9"); return -1; }
        }
        return 0;
    }
    u32 nf = na_next;
    memcpy(na_fext, na_ext, nf * sizeof(natres_ext_t));
    FILE *f = fopen(path, "rb");
    static u8 hm[0x8000];
    if (!f || fread(hm, 1, sizeof(hm), f) != sizeof(hm) || memcmp(hm, "CISO", 4)) { if (f) fclose(f); snprintf(err, cap, ".ciso illisible"); return -1; }
    fclose(f);
    u32 bs = na_le32(hm + 4), bsec = bs >> 9;
    if (!bs || (bs & 511) || bs > (64u << 20)) { snprintf(err, cap, ".ciso : taille de bloc %u non prise en charge", (unsigned)bs); return -1; }
    u32 nb = 0x8000 - 8, last = 0, pos = 0;
    for (u32 b = 0; b < nb; b++) if (hm[8 + b]) last = b + 1;
    na_next = 0;
    for (u32 b = 0; b < last; b++) {
        u32 iso = b * bsec;
        if (!hm[8 + b]) { if (na_ext_add(iso, NATRES_ZERO, bsec)) goto frag; continue; }
        u32 fs = 0x8000u / 512 + pos * bsec, left = bsec;            /* (secteurs du bloc dans le fichier) */
        pos++;
        while (left) {
            u32 k = 0;
            while (k < nf && !(fs >= na_fext[k].iso && fs - na_fext[k].iso < na_fext[k].cnt)) k++;
            if (k >= nf) { snprintf(err, cap, ".ciso plus court que pr\xC3\xA9vu"); return -1; }
            u32 n = na_fext[k].cnt - (fs - na_fext[k].iso);
            if (n > left) n = left;
            if (na_ext_add(iso, na_fext[k].sd + (fs - na_fext[k].iso), n)) goto frag;
            iso += n; fs += n; left -= n;
        }
    }
    if (last * bsec < NA_DISC_SECS && na_ext_add(last * bsec, NATRES_ZERO, NA_DISC_SECS - last * bsec)) goto frag;
    na_iso_secs = last * bsec > NA_DISC_SECS ? last * bsec : NA_DISC_SECS;
    return 0;
frag:
    snprintf(err, cap, "Jeu trop fragment\xC3\xA9 sur la carte (d\xC3\xA9" "fragmente-la)");
    return -1;
}

/* verification : 3 endroits de l'ISO, lus par la carte des secteurs et par libfat (v121 : par gd_read : .ciso aussi) */
static int nat_map_check(const char *path, long size, char *err, int cap) {
    static u8 a[512] ATTRIBUTE_ALIGN(32), r[512] ATTRIBUTE_ALIGN(32);
    (void)size;
    if (!na_iso_secs) { snprintf(err, cap, "Jeu vide"); return -1; }
    u32 last = na_iso_secs - 1;
    u32 at[3] = { 0, last / 2, last };
    gdisc_t d;
    if (!gd_open(&d, path)) { snprintf(err, cap, "Jeu introuvable"); return -1; }
    for (int i = 0; i < 3; i++) {
        u32 isec = at[i], sd = 0, ok = 0;
        for (u32 k = 0; k < na_next; k++) if (isec >= na_ext[k].iso && isec - na_ext[k].iso < na_ext[k].cnt) { sd = na_ext[k].sd == NATRES_ZERO ? NATRES_ZERO : na_ext[k].sd + (isec - na_ext[k].iso); ok = 1; break; }
        memset(a, 0, 512);
        if (!d.ciso) {                                   /* (la fin d'un .iso peut ne pas faire un secteur entier) */
            long sz = 0; fseek(d.fp, 0, SEEK_END); sz = ftell(d.fp);
            u32 want = (u64)isec * 512 + 512 <= (u64)sz ? 512 : (u32)((u64)sz - (u64)isec * 512);
            if (!ok || !gd_read(&d, isec * 512u, a, want)) { gd_close(&d); snprintf(err, cap, "Carte des secteurs fausse (secteur %u)", (unsigned)isec); return -1; }
            if (na_rawsec(sd, r) || memcmp(a, r, want)) { gd_close(&d); snprintf(err, cap, "Carte des secteurs fausse (secteur %u)", (unsigned)isec); return -1; }
            continue;
        }
        if (!ok || !gd_read(&d, isec * 512u, a, 512)) { gd_close(&d); snprintf(err, cap, "Carte des secteurs fausse (secteur %u)", (unsigned)isec); return -1; }
        if (sd == NATRES_ZERO) memset(r, 0, 512);
        else if (na_rawsec(sd, r)) { gd_close(&d); snprintf(err, cap, "Lecture brute de la carte impossible"); return -1; }
        if (memcmp(a, r, 512)) { gd_close(&d); snprintf(err, cap, "Carte des secteurs fausse (secteur %u)", (unsigned)isec); return -1; }
    }
    gd_close(&d);
    return 0;
}

/* ---------- v54 : journal pendant le jeu ----------
   GC Station prepare un fichier d'un secteur ; le programme resident y ecrit son etat
   (compteurs + les 24 dernieres commandes du jeu) pendant le jeu. Au demarrage suivant,
   GC Station le traduit en texte : SD:/gc-mp3/natif/journal-jeu.txt (aussi sur le telephone). */
#define NA_JBIN NA_DIR "/journal-jeu.bin"
#define NA_LOG   NA_DIR "/dernier-lancement.txt"
#define NA_JTXT NA_DIR "/journal-jeu.txt"
#define NA_SNAP NA_DIR "/memoire-jeu.bin"             /* v83 : photo de la memoire du jeu au plantage */
static int na_jcrash = 0;                              /* v79 : 2e secteur du journal (details d'un plantage) disponible */
static int na_jstack = 0;                              /* v82 : 3e secteur (pile du jeu au plantage) disponible */
static u32 nat_journal_prep(void) {
    na_jcrash = na_jstack = 0;
    if (sd_virtual) return 0;
    mkdir(NA_DIR, 0777);
    static u8 z[1536];
    memset(z, 0, sizeof(z));
    FILE *f = fopen(NA_JBIN, "wb");
    if (!f) return 0;
    int ok = fwrite(z, 1, 1536, f) == 1536;                /* v79 : 2 secteurs (le 2e : details d'un plantage) ; v82 : 3 (le 3e : pile du jeu) */
    fclose(f);
    char e[80];
    if (!ok || nat_map(NA_JBIN, e, sizeof(e)) || !na_next || na_next > 2) return 0;
    na_jcrash = na_next == 1 && na_ext[0].cnt >= 2;       /* (les 2 secteurs a la suite sur la carte) */
    na_jstack = na_next == 1 && na_ext[0].cnt >= 3;
    return na_ext[0].sd;
}
/* v83 : fichier de la photo de la memoire (cree une fois, 352 Ko ; 1er secteur remis a zero a
   chaque lancement) ; rend son 1er secteur sur la carte (0 = pas d'un seul morceau : pas de photo) */
static u32 nat_snap_prep(void) {
    if (sd_virtual) return 0;
    const long want = (long)NATRES_SNAP_SECS * 512;
    struct stat st;
    FILE *f = NULL;
    if (stat(NA_SNAP, &st) == 0 && st.st_size == want) f = fopen(NA_SNAP, "r+b");
    static u8 z[4096];
    memset(z, 0, sizeof(z));
    int ok;
    if (f) ok = fwrite(z, 1, 512, f) == 512;
    else {
        f = fopen(NA_SNAP, "wb");
        if (!f) return 0;
        ok = 1;
        for (long o = 0; ok && o < want; o += sizeof(z)) ok = fwrite(z, 1, sizeof(z), f) == sizeof(z);
    }
    fclose(f);
    char e[80];
    if (!ok || nat_map(NA_SNAP, e, sizeof(e)) || na_next != 1 || na_ext[0].cnt < NATRES_SNAP_SECS) return 0;
    return na_ext[0].sd;
}
/* v84 : mode video de la console (0x800000CC), normalement mis par le menu de la console (BIOS).
   GC Station demarre sans lui : la valeur restait 0 (NTSC) et les jeux PAL s'arretaient expres
   au demarrage (VIConfigure : "Tried to change mode ... which is forbidden" ; Wind Waker PAL).
   Jeu PAL -> PAL (EURGB60 si GC Station est deja en PAL 60 Hz), jeu NTSC -> NTSC. */
static int na_game_pal(const char *id) {
    switch (id[3]) {
    case 'P': case 'D': case 'F': case 'S': case 'I': case 'X': case 'Y': case 'Z': case 'U': case 'H': case 'R': case 'W': case 'V': return 1;
    default: return 0;
    }
}
static u32 na_vi_bootrom(const char *id) {
    if (!na_game_pal(id)) return 0;                       /* VI_NTSC */
#ifdef GEKKO
    if (VIDEO_GetCurrentTvMode() == VI_EURGB60) return 5;
#endif
    return 1;                                             /* VI_PAL */
}
static const char *na_cmd_name(u32 c) {
    if ((c & 0xFFFFFF00u) == 0xE3D15C00u) return "CHANGEMENT DE DISQUE";   /* v121 */
    switch (c >> 24) {
    case 0xA8: return (c & 0xC0) == 0x40 ? "lire l'ID du disque" : "lire";
    case 0xAB: return "chercher"; case 0xE0: return "demander l'erreur"; case 0x12: return "infos lecteur";
    case 0xAC: return "jeu : ecrit DISR";
    case 0xDE: return (c & 0xFF) == 1 ? "LECTEUR BLOQUE : sans lui" : "PASSE SANS LE LECTEUR";   /* v133 */
    case 0xDD: return (c & 0xFF) == 3 ? "SECOURS : fin donnee" : (c & 0xFF) == 1 ? "SECOURS : lecture finie" : (c & 0xFF) == 2 ? "SECOURS : erreur" : "SECOURS : lecture relancee";   /* v133 */
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
/* v135 : la fonction ENTIERE qui contient addr (du prologue "stwu r1,-n(r1)" jusqu'au "blr", 200 mots au plus),
   lue dans le main.dol de l'ISO : pour la desassembler sans avoir le jeu */
static void na_func_dump(FILE *f, u32 addr, const char *label) {
    if (addr < 0x80003000u || addr >= 0x81800000u) return;
    static char iso[NAME_LEN];
    iso[0] = 0;
    FILE *lf = fopen(NA_LOG, "r");
    if (lf) { char l[NAME_LEN + 16]; while (fgets(l, sizeof(l), lf)) if (!strncmp(l, "Fichier : ", 10)) { snprintf(iso, sizeof(iso), "%s", l + 10); iso[strcspn(iso, "\r\n")] = 0; break; } fclose(lf); }
    if (!iso[0]) return;
    gdisc_t g;
    if (!gd_open(&g, iso)) return;
    enum { NW = 400 };                                   /* (fenetre : 200 mots avant, 200 apres) */
    static u8 w[NW * 4];
    u8 h[0x100];
    u32 dol = 0, a0 = ((addr & ~3u) - 200 * 4), fo = 0, nw = NW;
    int ok = gd_read(&g, 0x420, h, 4);
    if (ok) { dol = be32(h); ok = gd_read(&g, dol, h, sizeof(h)); }
    for (int i = 0; ok && i < 18 && !fo; i++) {           /* (la fenetre est rognee aux bords de la section) */
        u32 off = be32(h + i * 4), at = be32(h + 0x48 + i * 4), sz = be32(h + 0x90 + i * 4);
        if (!sz || addr < at || addr >= at + sz) continue;
        if (a0 < at) a0 = at;
        if (a0 + nw * 4 > at + sz) nw = (at + sz - a0) / 4;
        fo = dol + off + (a0 - at);
    }
    if (!fo || !nw || !gd_read(&g, fo, w, nw * 4)) { gd_close(&g); return; }
    gd_close(&g);
    int at = (int)((addr & ~3u) - a0) / 4, b = at, e = at;
    while (b > 0 && at - b < 199 && (be32(w + b * 4) & 0xFFFF0000u) != 0x94210000u) b--;   /* stwu r1,-n(r1) */
    while (e < (int)nw - 1 && e - b < 199 && be32(w + e * 4) != 0x4E800020u) e++;        /* blr */
    fprintf(f, "  fonction (%s) : %08X - %08X%s :", label, (unsigned)(a0 + b * 4), (unsigned)(a0 + e * 4),
            (be32(w + b * 4) & 0xFFFF0000u) == 0x94210000u ? "" : " (debut pas trouve)");
    for (int i = b; i <= e; i++) fprintf(f, "%s%s%08X", (i - b) % 8 ? " " : "\n    ", i == at ? ">" : "", (unsigned)be32(w + i * 4));
    fprintf(f, "\n");
}
/* v79 : details d'un plantage (2e secteur du journal, format : natres.h) */
static void na_crash_print(FILE *f, const u8 *c) {
    u32 w[0x110 / 4];
    for (int i = 0; i < 0x110 / 4; i++) w[i] = be32(c + i * 4);
    const u32 *g = w + 0x30 / 4, *lo = w + 0xB0 / 4, *bt = w + 0xD0 / 4;
    if (w[2] == 0xFF) fprintf(f, "\n========== Ou en etait le jeu au retour (A+Z+START) ==========\n"
                                 "(le jeu ne plantait pas : c'est l'endroit ou il tournait, utile pour un ecran noir ou un jeu fige)\n");   /* v133 */
    else fprintf(f, "\n========== Details du plantage ==========\n");
    fprintf(f, "PC %08X   LR %08X   CTR %08X   CR %08X   SRR1 %08X\n", (unsigned)w[3], (unsigned)w[5], (unsigned)w[7], (unsigned)w[6], (unsigned)w[4]);
    fprintf(f, "Adresse visee %08X (DSISR %08X)%s\n", (unsigned)w[8], (unsigned)w[9],
            w[8] == 0x01234567u ? "  <- adresse bidon : le jeu s'est tres probablement arrete EXPRES (erreur detectee au demarrage)" : "");
    fprintf(f, "Registres :\n");
    for (int r = 0; r < 32; r++) {
        fprintf(f, "%s r%-2d %08X", r % 4 ? "" : "  ", r, (unsigned)g[r]);
        if (r % 4 == 3) fputc('\n', f);
    }
    /* textes : ce que pointaient r3 ... r10 (un message d'erreur, un nom de fichier du jeu...) */
    int nt = 0;
    for (int r = 3; r <= 10; r++) {
        const u8 *t = c + 0x110 + (r - 3) * NATRES_CRASH_STR;
        int n = 0;
        while (n < NATRES_CRASH_STR && t[n] >= 0x20 && t[n] < 0x7F) n++;
        if (n < 4 || (n < NATRES_CRASH_STR && t[n] != 0 && t[n] != '\n')) continue;
        if (!nt++) fprintf(f, "Textes que le jeu avait en main (souvent le message de son erreur) :\n");
        fprintf(f, "  r%-2d -> \"%.*s\"%s\n", r, n, (const char *)t, n == NATRES_CRASH_STR ? "..." : "");
    }
    if (!nt) fprintf(f, "Textes : aucun texte lisible dans r3 ... r10\n");
    fprintf(f, "Chaine des appels (le plus recent d'abord) : %u\n   ", (unsigned)w[11]);
    for (u32 i = 0; i < w[11] && i < NATRES_CRASH_NBT; i++) fprintf(f, " %08X", (unsigned)bt[i]);
    fprintf(f, "\n");
    /* memoire : ce que le jeu a recu, et ce qu'il aurait eu sur une vraie console */
    u32 hi = lo[3], fstmax = lo[5], real = (0x81800000u - fstmax) & ~31u;
    fprintf(f, "Memoire basse : taille %08X, console %08X, arene %08X - %08X, FST %08X (max %u), simulee %08X, bi2 %08X\n",
            (unsigned)lo[0], (unsigned)lo[1], (unsigned)lo[2], (unsigned)lo[3], (unsigned)lo[4], (unsigned)lo[5], (unsigned)lo[6], (unsigned)lo[7]);
    if (hi && fstmax < 0x400000u && real > hi)
        fprintf(f, "Memoire laissee au jeu : haut de l'arene %08X ; sur une vraie console : environ %08X (%u Ko de moins avec Pulse)\n",
                (unsigned)hi, (unsigned)real, (unsigned)((real - hi) >> 10));
    na_func_dump(f, w[3], "PC");                          /* v135 : fonctions entieres (PC, LR, 2 premiers appels) */
    na_func_dump(f, w[5], "LR");
    for (u32 i = 0; i < w[11] && i < 2; i++) { char lb[24]; snprintf(lb, sizeof(lb), "appel %u", (unsigned)i + 1); na_func_dump(f, bt[i], lb); }
    if (w[11] > 2) { char lb[24]; snprintf(lb, sizeof(lb), "appel 3"); na_code_dump(f, bt[2], lb); }
}
/* v82 : pile du jeu au plantage (3e secteur) : textes (message, fichier) et adresses de code */
static void na_stack_print(FILE *f, const u8 *b) {
    u32 sp = be32(b + 4), nw = be32(b + 8), ns = be32(b + 12);
    if (nw > NATRES_STACK_WORDS) nw = NATRES_STACK_WORDS;
    if (ns > NATRES_STACK_NSTR) ns = NATRES_STACK_NSTR;
    fprintf(f, "\n========== Pile du jeu au plantage (r1 = %08X) ==========\n", (unsigned)sp);
    if (ns) {
        fprintf(f, "Textes trouves sur la pile (souvent : fichier du jeu, ligne, message de l'erreur) :\n");
        for (u32 k = 0; k < ns; k++) {
            const u8 *e = b + 0x150 + k * 32;
            char t[NATRES_STACK_SLEN + 1]; int n = 0;
            while (n < NATRES_STACK_SLEN && e[4 + n] >= 0x20 && e[4 + n] < 0x7F) { t[n] = (char)e[4 + n]; n++; }
            t[n] = 0;
            fprintf(f, "  %08X -> \"%s\"%s\n", (unsigned)be32(e), t, n == NATRES_STACK_SLEN ? "..." : "");
        }
    } else fprintf(f, "Textes : aucun texte trouve sur la pile\n");
    u32 code[8]; int nc = 0;
    for (u32 i = 0; i < nw; i++) {
        u32 v = be32(b + 0x10 + i * 4);
        if (i % 4 == 0) fprintf(f, "  %08X :", (unsigned)(sp + i * 4));
        int isc = v >= 0x80003100u && v < 0x80600000u && !(v & 3);
        fprintf(f, " %08X%c", (unsigned)v, isc ? '*' : ' ');
        if (i % 4 == 3) fputc('\n', f);
        if (isc && nc < 8) { int dup = 0; for (int k = 0; k < nc; k++) if (code[k] == v) dup = 1; if (!dup) code[nc++] = v; }
    }
    fprintf(f, "(* = adresse dans le code du jeu : probablement un appel en cours)\n");
    for (int k = 0; k < nc && k < 3; k++) { char lb[32]; snprintf(lb, sizeof(lb), "pile %08X", (unsigned)code[k]); na_code_dump(f, code[k], lb); }
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
        fprintf(f, "GC Station - journal du dernier jeu lance avec Pulse\n\n");
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
            if (v4) fprintf(f, "Manettes (A+Z+START %s) : %u lues, %u laissees au jeu, touches manette 1 : %04X (%u changements ; touches vues : %04X%s)\n",
                            H[18] & NRF_IGR ? "actif" : "coupe", (unsigned)H[40], (unsigned)H[41], (unsigned)H[42], (unsigned)(H[43] & 0xFFFF), (unsigned)(H[43] >> 16),
                            (H[43] >> 16 & 0x1110) == 0x1110 ? " : A, Z et START vus" : "");
            fprintf(f, "\n");
        }
        if (H[1] >= 15) {                                     /* v121 : 2e disque, cheats (natres_hdr_t.disc, 0x198) */
            u32 dz = be32(d + 0x198);
            if (be32(d + 0x194) || (dz >> 8 & 0xFF)) fprintf(f, "Changements de disque : %u (disque en cours : %u)\n", (unsigned)(dz >> 8 & 0xFF), (unsigned)(dz & 1) + 1);
            if (be32(d + 0x19C)) fprintf(f, "Cheats appliques par Pulse : %u images\n", (unsigned)(dz >> 16));
            if (H[1] >= 16 && be32(d + 0x1B8)) fprintf(f, "Manettes / lecteur (options %08X) : %u lectures des manettes modifiees\n", (unsigned)be32(d + 0x1B8), (unsigned)(be32(d + 0x1BC) & 0xFFFFu));
            if (H[1] >= 16 && (be32(d + 0x1BC) >> 16)) fprintf(f, "Corrections du jeu dans les fichiers lus : %u octets changes au passage\n", (unsigned)(be32(d + 0x1BC) >> 16));   /* v136 */
            if (H[1] >= 17 && (be32(d + 0x1B8) & NRO_TRAP)) {     /* v139 : Pulse universel */
                static const char *const why[] = { "pas encore active (le jeu n'a pas installe ses fonctions)", "actif", "refuse : BAT de la zone 0xC0000000 inattendu",
                    "refuse : 2e BAT deja utilise par le jeu", "refuse : le jeu a sa propre table des pages", "refuse : vecteur 0x300 inattendu",
                    "refuse : table des pages", "perdu (le programme suivant a remis ses BAT)" };
                u32 ts = be32(d + 0x1C0);
                fprintf(f, "Acces au lecteur par exception (experimental) : %s ; %u acces pris (dernier en %08X)", ts < 8 ? why[ts] : "?", (unsigned)be32(d + 0x1C4), (unsigned)be32(d + 0x1C8));
                if (be32(d + 0x1CC)) fprintf(f, " ; instruction pas prise en charge (sautee) : %08X", (unsigned)be32(d + 0x1CC));
                fprintf(f, "\n");
            }
            fprintf(f, "\n");
        }
        fprintf(f, "Dernieres commandes (la plus ancienne en premier) :\n");
        int rbase = H[1] >= 17 ? 0x1D0 : H[1] >= 16 ? 0x1C0 : H[1] >= 15 ? 0x1A0 : H[1] >= 14 ? 0x190 : H[1] >= 13 ? 0x180 : H[1] >= 12 ? 0x170 : H[1] >= 11 ? 0x160 : v10 ? 0x150 : v9 ? 0x130 : v8 ? 0x120 : v7 ? 0xF0 : v6 ? 0xE0 : v5 ? 0xD0 : v4 ? 0xB0 : v3 ? 0xA0 : 0x80, nring = (512 - rbase) / 16;
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
        if (len >= 1024 && be32(d + 512) == NATRES_CRASH_MAGIC) na_crash_print(f, d + 512);   /* v79 */
        if (len >= 1536 && be32(d + 1024) == NATRES_STACK_MAGIC) na_stack_print(f, d + 1024);  /* v82 */
        if (H[1] >= 12) {                                                     /* v94 */
            u32 xf = be32(d + 0x160);
            fprintf(f, "Lecture de la carte : %s, %s%s ; blocs au CRC faux : %u\n", xf & 1 ? "DMA" : "sans DMA", xf & 2 ? "32 MHz" : "16 MHz",
                    xf & 4 ? " (32 MHz abandonne : CRC faux)" : "", (unsigned)(be32(d + 0x164) & 0xFFFFu));
            if (H[67]) fprintf(f, "Musique du disque : %u tampons recalcules par le DSP (musique remise), %u tampons rejoues laisses tels quels\n",
                               (unsigned)(xf >> 16), (unsigned)(be32(d + 0x164) >> 16));
            if (be32(d + 0x168) || be32(d + 0x16C)) fprintf(f, "Programme suivant du disque (OSExec) : %u demande(s), dernier %08X%s ; acces au lecteur branches pendant le jeu : %u\n",
                    (unsigned)be32(d + 0x168), (unsigned)be32(d + 0x16C), H[1] >= 13 && !be32(d + 0x174) ? " (apploader recent : pas encore pris en charge)" : "", H[1] >= 13 ? (unsigned)be32(d + 0x17C) : 0);
        }
        if (H[1] >= 14) {                                                     /* v99 : l'ecran */
            u32 vn = be32(d + 0x180), vd = be32(d + 0x184), dcr = vn >> 16;
            fprintf(f, "Ecran : %s, %s, %s%s ; cable %s ; changements de mode : %u (dernier a l'image %u) ; interruption de 1re ligne (DI1) : %s\n",
                    dcr & 1 ? "actif" : "COUPE", (dcr >> 8 & 3) == 1 ? "PAL" : (dcr >> 8 & 3) == 2 ? "MPAL" : "NTSC",
                    dcr & 4 ? (vn & 1 ? "480p (progressif, 54 MHz)" : "non entrelace 27 MHz") : (vn & 1 ? "entrelace mais horloge 54 MHz (incoherent)" : "entrelace (480i/576i)"),
                    "", vd >> 16 & 1 ? "numerique / composante (480p possible)" : "analogique",
                    (unsigned)be32(d + 0x188), (unsigned)be32(d + 0x18C), vd & 0x1000 ? "active" : "COUPEE (480p impossible)");
        }
        if (H[1] >= 11 && be32(d + 0x15C)) {                                  /* v83 : photo de la memoire */
            fprintf(f, "\n========== Photo de la memoire du jeu au plantage ==========\n");
            long sl = 0;
            u8 *sd = (u8 *)read_file(NA_SNAP, 512, &sl);
            if (sd && sl == 512 && be32(sd) == NATRES_SNAP_MAGIC) {
                u32 nz = be32(sd + 4);
                static const char *const zn[7] = { "memoire basse", "variables du jeu (r13)", "pile (r1)", "autour de r3", "autour de r4", "autour de r30",
                                                   "debut de l'arene (tas du jeu)" };   /* v135 : + r3, r4, r30 */
                for (u32 z = 0; z < nz && z < 7; z++) {
                    u32 k = be32(sd + 20 + z * 16); if (k > 6) k = 6;
                    fprintf(f, "  %-30s %08X  %6u octets\n", zn[k], (unsigned)be32(sd + 8 + z * 16), (unsigned)be32(sd + 12 + z * 16));
                }
                FILE *sf = fopen(NA_SNAP, "rb");                  /* v135 : les zones r3 / r4 / r30 en clair dans le journal */
                for (u32 z = 0; sf && z < nz && z < 7; z++) {
                    u32 k = be32(sd + 20 + z * 16);
                    if (k < 3 || k > 5) continue;
                    u32 zb = be32(sd + 8 + z * 16), zl = be32(sd + 12 + z * 16), zs = be32(sd + 16 + z * 16);
                    static u8 zd[0x800];
                    if (zl > sizeof(zd)) zl = sizeof(zd);
                    if (fseek(sf, (long)zs * 512, SEEK_SET) != 0 || fread(zd, 1, zl, sf) != zl) continue;
                    fprintf(f, "\n  %s (%08X) :\n", zn[k], (unsigned)zb);
                    for (u32 o = 0; o < zl; o += 32) {
                        fprintf(f, "    %08X :", (unsigned)(zb + o));
                        for (u32 i = 0; i < 32; i += 4) fprintf(f, " %08X", (unsigned)be32(zd + o + i));
                        fputc('\n', f);
                    }
                }
                if (sf) fclose(sf);
                fprintf(f, "%u secteurs ecrits : envoie aussi le fichier gc-mp3/natif/memoire-jeu.bin\n(il est remplace au prochain plantage)\n", (unsigned)be32(d + 0x15C));
            } else fprintf(f, "  (photo incomplete : %u secteurs ecrits)\n", (unsigned)be32(d + 0x15C));
            free(sd);
        }
        long ll = 0;                                      /* (et le journal du lancement, ecrit avant le saut) */
        char *lt = (char *)read_file(NA_LOG, 16384, &ll);
        if (lt) { fprintf(f, "\n---------- dernier-lancement.txt ----------\n"); fwrite(lt, 1, ll, f); free(lt); }
        fclose(f);
    }
#ifndef NATIF_BENCH
    if (be32(d + 4) >= 3 && be32(d + 0x90)) {             /* v73 : temps de jeu (images comptees par Resident) */
        long ll = 0;
        char *lt = (char *)read_file(NA_LOG, 16384, &ll), id[8] = "";
        if (lt) { char *p = strstr(lt, "Jeu : "); if (p && ll > (p - lt) + 12) { memcpy(id, p + 6, 6); id[6] = 0; } free(lt); }
        if (id[0] && id[5]) gl_stats_add_play(id, be32(d + 0x90) / (id[3] == 'P' ? 50u : 60u));
    }
    {   /* v137 : base de compatibilite : plantage note tout seul, sinon question au retour */
        long ll = 0;
        char *lt = (char *)read_file(NA_LOG, 16384, &ll), id[8] = "", title[72] = "";
        int slow = 0, vm = 0;
        if (lt) {
            char *p = strstr(lt, "Jeu : ");
            if (p && ll > (p - lt) + 12) { memcpy(id, p + 6, 6); id[6] = 0; snprintf(title, sizeof(title), "%s", p + 14); title[strcspn(title, "\r\n")] = 0; }
            slow = strstr(lt, "Vitesse du lecteur : comme un vrai lecteur") != NULL;
            p = strstr(lt, "\nVideo : ");
            if (p) vm = !strncmp(p + 9, "480p", 4) ? 1 : !strncmp(p + 9, "PAL 60", 6) ? 2 : !strncmp(p + 9, "240p", 4) ? 3 : 0;
            if (!strstr(lt, "Saut vers le jeu")) id[0] = 0;   /* (jeu pas lance : rien a demander) */
            if (id[0]) {                                  /* v140 : diagnostic pour la reparation automatique */
                int br = 0, sk = 0;
                char *q = strstr(lt, "Faux lecteur DVD : ");
                if (q) sscanf(q, "Faux lecteur DVD : %d acces branches, %d non branches", &br, &sk);
                q = strstr(lt, "\nCheats : ");
                int cheats = q && strncmp(q + 10, "aucun", 5) && strncmp(q + 10, "coup", 4);
                q = strstr(lt, "\n16:9 force : ");
                int ws = q && strncmp(q + 15, "non", 3);
                q = strstr(lt, "Carte memoire virtuelle (slot A) : ");
                int card = q && strncmp(q + 35, "non", 3) && strncmp(q + 35, "coupee", 6);
                int trap = strstr(lt, "Acces au lecteur par exception") != NULL;
                u32 vi = be32(d + 4) >= 3 ? be32(d + 36 * 4) : 1, vin = be32(d + 4) >= 9 ? be32(d + 74 * 4) : 0;
                rep_diag_set(id, be32(d + 4) >= 5 && be32(d + 44 * 4) != 0, vi == 0, vin > vi, sk, cheats, ws, card, vm, slow, trap);
                sv_after_game(id, lt);                    /* v141 : copie des sauvegardes du jeu (carte virtuelle) */
            }
            free(lt);
        }
        if (id[0] && id[5]) cp_after_game(id, title, (be32(d + 4) >= 5 && be32(d + 44 * 4) != 0), slow, vm);
    }
#endif
    memset(d, 0, len);                                    /* (traduit : on ne le refait pas au prochain demarrage) */
    FILE *b = fopen(NA_JBIN, "r+b");
    if (b) { fwrite(d, 1, len, b); fclose(b); }
    free(d);
}

/* test du programme resident sur la console, sans lancer de jeu : il demarre la carte SD
   tout seul et relit 3 endroits de l'ISO. La memoire qu'il occupe est sauvee puis remise. */
static const char *na_res_names[] = { "OK", "pas de carte", "la carte ne d\xC3\xA9marre pas", "commande refus\xC3\xA9" "e", "pas de donn\xC3\xA9" "es", "hors de l'ISO", "d\xC3\xA9lai d\xC3\xA9pass\xC3\xA9" };
static int nat_res_test(const char *path, long size, char *msg, int cap) {
#ifdef GEKKO
    static u8 want[3][512] ATTRIBUTE_ALIGN(32), got[512] ATTRIBUTE_ALIGN(32);
    gdisc_t gd;
    if (!gd_open(&gd, path)) { snprintf(msg, cap, "Jeu introuvable"); return -1; }
    u32 vsz = gd.ciso ? na_iso_secs * 512u : (u32)size;   /* (v121 : taille vue par le jeu, .ciso compris) */
    u32 offs[3] = { 0, (vsz / 2) & ~511u, (vsz - 512) & ~511u };
    for (int i = 0; i < 3; i++) { memset(want[i], 0, 512); if (!gd_read(&gd, offs[i], want[i], 512) && !gd.ciso) { gd_close(&gd); snprintf(msg, cap, "Lecture du jeu impossible"); return -1; } }
    gd_close(&gd);
    if (sizeof(natres_bin) > NATRES_SIZE) { snprintf(msg, cap, "Programme r\xC3\xA9sident trop gros"); return -1; }
    u8 *save = (u8 *)memalign(32, NATRES_SIZE);
    if (!save) { snprintf(msg, cap, "Pas assez de m\xC3\xA9moire"); return -1; }
    if ((u32)save < NATRES_AT + NATRES_SIZE && (u32)save + NATRES_SIZE > NATRES_AT) { free(save); snprintf(msg, cap, "M\xC3\xA9moire du r\xC3\xA9sident occup\xC3\xA9" "e"); return -1; }   /* (v52) */
    stop_track();
    bg_quiesce();                                         /* v96 : la lecture en fond (onglet JEUX) finit d'abord */
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
        h->ext = (NATRES_AT + NATRES_SIZE - (na_next > NATRES_RETMAX ? na_next : NATRES_RETMAX) * 12) & ~31u;   /* (v97) */
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
    gj_hold = 0;                                          /* v96 : lectures en fond de nouveau permises */
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
/* v126 : sauvegarde / restauration des registres communes a tous les aiguillages (avant : recopiees dans
   chacun, ~13 instructions de plus par aiguillage) ; posees une fois au debut de la place libre. */
static u32 na_ssave = 0, na_srest = 0;
static u32 na_stubs(u8 *w, u32 at) {                     /* rend la place prise (octets) */
    u32 c[16]; int n = 0;
    na_ssave = at;
    c[n++] = PPC_D(47, 2, 1, 0x0C);                      /* stmw r2,0x0C(r1) */
    c[n++] = 0x7C000026u; c[n++] = PPC_STW(0, 1, 0x8C);  /* mfcr r0 */
    c[n++] = 0x7C0902A6u; c[n++] = PPC_STW(0, 1, 0x90);  /* mfctr r0 */
    c[n++] = 0x7C0102A6u; c[n++] = PPC_STW(0, 1, 0x94);  /* mfxer r0 */
    c[n++] = 0x4E800020u;                                /* blr */
    na_srest = at + (u32)n * 4;
    c[n++] = PPC_LWZ(0, 1, 0x94); c[n++] = 0x7C0103A6u;  /* mtxer */
    c[n++] = PPC_LWZ(0, 1, 0x90); c[n++] = 0x7C0903A6u;  /* mtctr */
    c[n++] = PPC_LWZ(0, 1, 0x8C); c[n++] = 0x7C0FF120u;  /* mtcrf 0xFF */
    c[n++] = PPC_D(46, 2, 1, 0x0C);                      /* lmw r2,0x0C(r1) */
    c[n++] = 0x4E800020u;                                /* blr */
    for (int i = 0; i < n; i++) { w[i * 4] = (u8)(c[i] >> 24); w[i * 4 + 1] = (u8)(c[i] >> 16); w[i * 4 + 2] = (u8)(c[i] >> 8); w[i * 4 + 3] = (u8)c[i]; }
    return ((u32)n * 4 + 31) & ~31u;
}
static int na_tramp(u8 *w, u32 at, u32 site, u32 ins, u32 reg, u32 fn_load, u32 fn_store) {
    u32 op = ins >> 26, rt = (ins >> 21) & 31, ra = (ins >> 16) & 31, rb = (ins >> 11) & 31;
    s32 simm = (s16)(ins & 0xFFFF);
    int dyn = (reg & (NA_XREG | NA_DDYN)) != 0;           /* v65 : EXI : r3 = adresse, calculee pendant le jeu ; v133 : lecteur en secours */
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
    c[n++] = 0x7C0802A6u; c[n++] = PPC_STW(0, 1, 0x88);  /* mflr r0 */
    if (na_ssave) { c[n] = 0x48000001u | ((na_ssave - (at + (u32)n * 4)) & 0x03FFFFFCu); n++; }   /* v126 : bl sauvegarde commune */
    else {
        c[n++] = PPC_D(47, 2, 1, 0x0C);                  /* stmw r2,0x0C(r1) */
        c[n++] = 0x7C000026u; c[n++] = PPC_STW(0, 1, 0x8C);  /* mfcr r0 */
        c[n++] = 0x7C0902A6u; c[n++] = PPC_STW(0, 1, 0x90);  /* mfctr r0 */
        c[n++] = 0x7C0102A6u; c[n++] = PPC_STW(0, 1, 0x94);  /* mfxer r0 */
    }
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
    if (na_srest) { c[n] = 0x48000001u | ((na_srest - (at + (u32)n * 4)) & 0x03FFFFFCu); n++; }   /* v126 : bl restauration commune (xer ctr cr r2-r31) */
    else {
        c[n++] = PPC_LWZ(0, 1, 0x94); c[n++] = 0x7C0103A6u;  /* mtxer */
        c[n++] = PPC_LWZ(0, 1, 0x90); c[n++] = 0x7C0903A6u;  /* mtctr */
        c[n++] = PPC_LWZ(0, 1, 0x8C); c[n++] = 0x7C0FF120u;  /* mtcrf 0xFF */
        c[n++] = PPC_D(46, 2, 1, 0x0C);                  /* lmw r2,0x0C(r1) */
    }
    c[n++] = PPC_LWZ(0, 1, 0x88); c[n++] = 0x7C0803A6u;  /* mtlr */
    c[n++] = PPC_LWZ(0, 1, 0x08);
    c[n++] = PPC_ADDI(1, 1, NA_FRAME);
    if (op == 33 || op == 37) c[n++] = PPC_ADDI(ra, ra, simm);   /* forme "u" : rA = adresse visee */
    u32 back = site + 4, here = at + (u32)n * 4;
    c[n++] = 0x48000000u | ((back - here) & 0x03FFFFFCu);   /* b site+4 */
    for (int i = 0; i < n; i++) { w[i * 4] = (u8)(c[i] >> 24); w[i * 4 + 1] = (u8)(c[i] >> 16); w[i * 4 + 2] = (u8)(c[i] >> 8); w[i * 4 + 3] = (u8)c[i]; }
    return n;
}

/* pose les aiguillages : dol = main.dol (copie a modifier), res = image du resident (NATRES_AT) ;
   rend le nombre d'acces branches, et dans *skipped ceux qu'on n'a pas su brancher */
/* v121 : haut de la place libre du resident : la table des morceaux (ou celle de l'autre disque, en dessous) */
static u32 na_ceil(const u8 *res) {
    u32 c = be32(res + 0x24), e2 = be32(res + offsetof(natres_hdr_t, ext2));
    return be32(res + offsetof(natres_hdr_t, n_ext2)) && e2 && e2 < c ? e2 : c;
}
#define NA_MAXSKIP 16
static u32 na_skip_at[NA_MAXSKIP], na_skip_ins[NA_MAXSKIP];
static const char *na_skip_why[NA_MAXSKIP];
static int na_nskip = 0;
static u32 na_xfo[NA_MAXHIT], na_xold[NA_MAXHIT];
static char na_xwhy[120] = "";
static int na_xdone = 0;
static int na_si_done = 0, na_si_skip = 0;
static int na_patch(const nat_info_t *in, u8 *dol, u8 *res, int *skipped, int *card_on, int *si_on) {
    u32 at = (be32(res + 0x3C) + 31) & ~31u;             /* (h->end, lu en big-endian) */
    u32 dl = be32(res + 0x40), ds = be32(res + 0x44);   /* (h->di_load, h->di_store) */
    u32 xl = be32(res + 0xF0), xs = be32(res + 0xF4);   /* v65 : (h->exi_load, h->exi_store) */
    int done = 0;
    *skipped = 0; na_nskip = 0; na_xdone = 0; na_xwhy[0] = 0; na_dd_done = 0; na_dd_skip = 0;
    na_ssave = na_srest = 0;
    if (at + 0x80 <= na_ceil(res)) at += na_stubs(res + (at - NATRES_AT), at);   /* v126 */
    /* passe 0 : le lecteur (tous, sinon le jeu n'est pas lance) ; passe 1 : la carte (tout ou rien) ;
       v122 : passe 2 : les manettes (ceux qu'on sait brancher ; les autres restent au vrai materiel) */
    u32 sl = be32(res + offsetof(natres_hdr_t, si_load)), ss = be32(res + offsetof(natres_hdr_t, si_store));
    na_si_done = na_si_skip = 0;
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 1 && !*card_on) continue;
        if (pass == 2 && !*si_on) break;
        u32 at0 = at, cov = 0;
        int nx = 0;
        const char *xfail = NULL;
        u32 xfail_at = 0;
        for (int i = 0; i < in->nhit; i++) {
            int kind = in->hit_reg[i] & NA_SREG ? 2 : in->hit_reg[i] & NA_XREG ? 1 : 0, isx = kind != 0;
            int dd = (in->hit_reg[i] & NA_DDYN) != 0;     /* v133 : lecteur, en secours (adresse recalculee : fonctions EXI du resident) */
            if (kind != pass) continue;
            u32 a = in->hit_addr[i], fo = 0;
            int sec = -1;
            for (int k = 0; k < in->nsec; k++) if (a >= in->sec_addr[k] && a - in->sec_addr[k] < in->sec_sz[k]) { sec = k; fo = in->sec_off[k] + (a - in->sec_addr[k]); }
            const char *why = sec < 0 ? "hors des sections" : be32(dol + fo) != in->hit_ins[i] ? "instruction differente"
                            : at + 64 * 4 > na_ceil(res) ? "plus de place"   /* (v97 : sous la table des morceaux ; v121 : des 2 disques) */
                            : isx || dd ? na_xtramp_why(in->hit_ins[i]) : na_tramp_why(in->hit_ins[i]);
            s32 d = (s32)(at - a);
            if (!why && (d >= 0x02000000 || d < -0x02000000)) why = "trop loin";
            int n = why ? 0 : na_tramp(res + (at - NATRES_AT), at, a, in->hit_ins[i], in->hit_reg[i], kind == 2 ? sl : isx || dd ? xl : dl, kind == 2 ? ss : isx || dd ? xs : ds);
            if (!why && !n) why = "demi-registre impair";
            if (kind == 2) {                                  /* (manettes : un acces pas branche reste au vrai materiel) */
                if (why) { na_si_skip++; continue; }
                na_wbe32(dol + fo, 0x48000000u | ((u32)d & 0x03FFFFFCu));
                at += (u32)n * 4;                         /* v126 : serres (avant : arrondis a 32 octets) */
                na_si_done++;
                continue;
            }
            if (dd) {                                     /* (secours : un acces pas branche ne bloque pas le lancement) */
                if (why) { na_dd_skip++; continue; }
                na_wbe32(dol + fo, 0x48000000u | ((u32)d & 0x03FFFFFCu));
                at += (u32)n * 4;
                na_dd_done++; done++;
                continue;
            }
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
            at += (u32)n * 4;                         /* v126 : serres (avant : arrondis a 32 octets) */
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
    at = (at + 31) & ~31u;                                /* v126 : (la suite reste alignee) */
    na_wbe32(res + offsetof(natres_hdr_t, tramp_at), at);   /* v95 : la suite de la place libre, pour les aiguillages poses pendant le jeu (OSExec) */
    return done;
}

/* ---------- v121 : 16:9 force (voir na_ws_scan) ----------
   aiguillage, a l'entree de GXSetProjection : si type = perspective (r4 = 0), les 12 premiers mots de la
   matrice sont recopies dans un tampon du resident, la 1re ligne (0x00 et 0x08) multipliee par 3/4, et r3
   pointe sur la copie (la matrice du jeu n'est jamais changee) ; puis la 1re instruction de la fonction et
   retour juste apres. Registres touches : r0, r12, f0, f1, cr0 (libres a l'entree d'une fonction). */
static int nat_ws = 0;                                     /* (reglage du jeu, pose par jeux_cfg_apply) */
static char na_ws_msg[120] = "";
static u32 na_ws_patch(const nat_info_t *in, u8 *dol, u8 *res, u32 at, u32 ceil) {
    if (in->ws_n != 1) { snprintf(na_ws_msg, sizeof(na_ws_msg), "non : %s", in->ws_n ? "plusieurs fonctions possibles" : "fonction GXSetProjection introuvable"); return at; }
    u32 f = in->ws_fn, fo = 0, ins = in->ws_ins, op = ins >> 26;
    int sec = -1;
    for (int k = 0; k < in->nsec; k++) if (f >= in->sec_addr[k] && f - in->sec_addr[k] < in->sec_sz[k]) { sec = k; fo = in->sec_off[k] + (f - in->sec_addr[k]); }
    if (sec < 0 || be32(dol + fo) != ins) { snprintf(na_ws_msg, sizeof(na_ws_msg), "non : fonction %08X introuvable dans main.dol", (unsigned)f); return at; }
    if (op == 16 || op == 18 || (op == 19 && (((ins >> 1) & 0x3FF) == 16 || ((ins >> 1) & 0x3FF) == 528))) { snprintf(na_ws_msg, sizeof(na_ws_msg), "non : la fonction commence par un saut"); return at; }
    u32 c[48], n = 0, buf = (at + 38 * 4 + 31) & ~31u;
    if (buf + 0x40 + 64 * 4 > ceil) { snprintf(na_ws_msg, sizeof(na_ws_msg), "non : plus de place dans le resident"); return at; }
    s32 dj = (s32)(at - f);
    if (dj >= 0x02000000 || dj < -0x02000000) { snprintf(na_ws_msg, sizeof(na_ws_msg), "non : trop loin"); return at; }
    c[n++] = 0x2C040000u;                                 /* cmpwi r4,0 (perspective ?) */
    c[n++] = 0x40820000u | (35u * 4);                     /* bne -> instruction d'origine */
    c[n++] = 0x3D800000u | (buf >> 16);                   /* lis r12,tampon */
    c[n++] = 0x618C0000u | (buf & 0xFFFF);                /* ori r12,r12,tampon */
    for (u32 o = 0; o < 0x30; o += 4) { c[n++] = 0x80030000u | o; c[n++] = 0x900C0000u | o; }   /* lwz r0,o(r3) ; stw r0,o(r12) */
    c[n++] = 0xC00C0030u;                                 /* lfs f0,0x30(r12) : 0,75 */
    c[n++] = 0xC02C0000u; c[n++] = 0xEC210032u; c[n++] = 0xD02C0000u;   /* m[0][0] *= 0,75 */
    c[n++] = 0xC02C0008u; c[n++] = 0xEC210032u; c[n++] = 0xD02C0008u;   /* m[0][2] *= 0,75 */
    c[n++] = 0x7D836378u;                                 /* mr r3,r12 */
    c[n++] = ins;                                         /* (instruction d'origine) */
    u32 here = at + n * 4;
    c[n++] = 0x48000000u | ((f + 4 - here) & 0x03FFFFFCu);   /* b fonction + 4 */
    for (u32 i = 0; i < n; i++) na_wbe32(res + (at - NATRES_AT) + i * 4, c[i]);
    for (u32 i = 0; i < 16; i++) na_wbe32(res + (buf - NATRES_AT) + i * 4, i == 12 ? 0x3F400000u : 0);   /* (0,75 en 0x30) */
    na_wbe32(dol + fo, 0x48000000u | ((u32)dj & 0x03FFFFFCu));          /* b aiguillage */
    snprintf(na_ws_msg, sizeof(na_ws_msg), "oui (GXSetProjection %08X)", (unsigned)f);
    return (buf + 0x40 + 31) & ~31u;
}

/* ---------- v122 : reglages du jeu pour Pulse (poses par jeux_cfg_apply) ---------- */
static int nat_vmode = 0;                                  /* 0 = comme le jeu, 1 = 480p, 2 = PAL 60 Hz, 3 = 240p */
static int nat_deflick = 0, nat_wide = 0;                  /* sans flou ; image plus large (704 points) */
static int nat_cstick = 0, nat_trig = 0, nat_poll = 0, nat_speed = 0, nat_trap = 0, nat_nocard = 0, nat_nocheat = 0;   /* (v140 : carte virtuelle / cheats coupes pour ce jeu) */   /* stick C, seuil des gachettes, frequence, vitesse du lecteur ; v139 : acces au lecteur par exception */
static u32 na_opt(void) {
    static const u8 thr[4] = { 0, 77, 128, 192 };
    u32 o = 0;
    if (nat_cstick == 1) o |= 1; else if (nat_cstick == 2) o |= 3; else if (nat_cstick == 3) o |= 4;
    o |= (u32)thr[nat_trig & 3] << 8;
    if (nat_poll == 1) o |= 2u << 16; else if (nat_poll == 2) o |= 4u << 16;
    if (nat_speed) o |= NRO_SPEED;
    if (nat_trap) o |= NRO_TRAP;
    return o;
}
/* v122 : modes video du jeu. Les jeux decrivent l'image dans des "GXRenderModeObj" (0x3C octets : mode de la
   tele, largeur, hauteurs, position, mode de la memoire d'image, filtres...) ranges dans main.dol. On les
   reconnait a leur contenu (valeurs possibles, filtre vertical dont la somme fait 64), et on les modifie
   avant le lancement : 480p (progressif), PAL en 60 Hz (480 lignes), 240p, sans flou (filtre vertical neutre),
   image plus large (704 points sur 720), le tout sans connaitre le code du jeu. */
static char na_rm_msg[160] = "";
static u32 na_be16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static void na_wbe16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static int na_rm_ok(const u8 *p) {
    u32 tv = be32(p), fmt = tv >> 2, md = tv & 3;
    if (tv > 23 || (fmt != 0 && fmt != 1 && fmt != 2 && fmt != 5) || md > 2) return 0;
    u32 fbw = na_be16(p + 4), efb = na_be16(p + 6), xfb = na_be16(p + 8), vxo = na_be16(p + 10), vyo = na_be16(p + 12), vw = na_be16(p + 14), vh = na_be16(p + 16);
    if (fbw < 512 || fbw > 720 || efb < 200 || efb > 600 || xfb < 200 || xfb > 600 || vw < 512 || vw > 720 || vh < 200 || vh > 600 || vxo > 120 || vyo > 120) return 0;
    if (na_be16(p + 18) || be32(p + 20) > 1 || p[24] > 1 || p[25] > 1) return 0;
    for (int i = 0; i < 24; i++) if (p[26 + i] > 12) return 0;
    u32 sum = 0;
    for (int i = 0; i < 7; i++) { if (p[50 + i] > 63) return 0; sum += p[50 + i]; }
    return sum == 64 && p[57] == 0;
}
/* ======================= v135 : corrections par jeu =======================
   Quelques jeux ont besoin qu'on change une instruction pour tourner sans le vrai lecteur, ou avec une autre
   carte memoire. Les endroits viennent de l'etude de Swiss (swiss-gc, GPL 2, cube/swiss/source/patcher.c,
   Patch_GameSpecific : docs/ETUDE-SWISS.md) ; ici c'est une table de donnees propre a Pulse : le jeu est
   reconnu a son code (6 lettres, ou 4) ET a la taille exacte de son main.dol, l'adresse est convertie en
   position dans le fichier par les sections du .dol (une adresse hors des sections est ignoree). */
enum { NF_WORD, NF_LANG, NF_CARD };                       /* mot ; "li r3, langue" ; taille de carte acceptee (demi-mot) */
typedef struct { const char *ids; u32 len, addr, val; u8 kind; const char *why; u32 orig; } na_fix_t;   /* orig : instruction attendue (0 = pas verifiee) */
static const na_fix_t na_fixes[] = {
    /* course entre une copie vers l'ARAM et sa fin (lecture instantanee) : une instruction retiree */
    { "GDREAF", 1881216, 0x8000AF34, 0x60000000, NF_WORD, "transfert ARAM" },
    { "GDRP69", 1950240, 0x8000B7EC, 0x60000000, NF_WORD, "transfert ARAM" },
    { "GHAE08",  800672, 0x800339E4, 0x60000000, NF_WORD, "transfert ARAM" },
    { "GHAJ08", 1072288, 0x80065FFC, 0x60000000, NF_WORD, "transfert ARAM" },
    { "GHAP08",  805984, 0x80033D60, 0x60000000, NF_WORD, "transfert ARAM" },
    { "GLEE08", 2303296, 0x80150E94, 0x60000000, NF_WORD, "transfert ARAM" },
    { "GLEJ08", 2298208, 0x8015110C, 0x60000000, NF_WORD, "transfert ARAM" },
    /* cartes memoire 1019 / 2043 blocs acceptees (le jeu n'accepte que 507 au plus) */
    { "GDKEA4", 3009280, 0x801F1FF2, 0, NF_CARD, "carte 1019 / 2043" },
    { "GDKJA4", 3075488, 0x801FF53E, 0, NF_CARD, "carte 1019 / 2043" },
    { "GDXEA4", 2039904, 0x800EFF3A, 0, NF_CARD, "carte 1019 / 2043" },
    { "GDXEA4", 2039904, 0x800F0082, 0, NF_CARD, "carte 1019 / 2043" },
    { "GDXEA4", 2039904, 0x800F0502, 0, NF_CARD, "carte 1019 / 2043" },
    { "GDXJA4", 1853024, 0x800E65EE, 0, NF_CARD, "carte 1019 / 2043" },
    { "GDXJA4", 1853024, 0x800E6736, 0, NF_CARD, "carte 1019 / 2043" },
    { "GDXJA4", 1853024, 0x800E6BB6, 0, NF_CARD, "carte 1019 / 2043" },
    { "GWTEA4",  839584, 0x8003758E, 0, NF_CARD, "carte 1019 / 2043" },
    { "GWTJA4",  804544, 0x8003740A, 0, NF_CARD, "carte 1019 / 2043" },
    { "GWTPA4",  834912, 0x800374D6, 0, NF_CARD, "carte 1019 / 2043" },
    /* initialisation de l'image (ecran noir au demarrage) */
    { "GGIJ13",  205696, 0x8001501C, 0x807E0000, NF_WORD, "image" },
    { "GGIJ13",  266336, 0x807023F0, 0x807E0000, NF_WORD, "image" },
    { "GOND69 GONE69 GONF69 GONP69", 277472, 0x80014C54, 0x807E0000, NF_WORD, "image" },
    { "GOND69 GONE69 GONF69 GONP69", 339520, 0x80701C38, 0x807E0000, NF_WORD, "image" },
    { "GONJ13",  276416, 0x80014C54, 0x807E0000, NF_WORD, "image" },
    { "GONJ13",  338400, 0x80701C38, 0x807E0000, NF_WORD, "image" },
    { "GOYD69 GOYE69 GOYF69 GOYP69 GOYS69", 207232, 0x8001501C, 0x807E0000, NF_WORD, "image" },
    { "GOYD69 GOYE69 GOYF69 GOYP69 GOYS69", 268384, 0x807023F0, 0x807E0000, NF_WORD, "image" },
    { "GR8D69 GR8E69 GR8F69 GR8P69", 206016, 0x80500EB4, 0x387D2720, NF_WORD, "image" },
    { "GR8D69 GR8E69 GR8F69 GR8P69", 260192, 0x80500EB4, 0x387D2720, NF_WORD, "image" },
    { "GRZJ13",  206240, 0x80500EB4, 0x387D2800, NF_WORD, "image" },
    { "GRZJ13",  260864, 0x80500EB4, 0x387D2800, NF_WORD, "image" },
    /* v136 : Mario Superstar Baseball (USA) : la video d'intro (HVQM4) attend de la place dans son tampon avec une
       boucle qui ne relit jamais les pointeurs (lus avant la boucle) : si le tampon est plein en arrivant, elle ne
       finit jamais (journaux des builds 487 et 489 : PC 800AF558-800AF570). Le saut de fin de boucle repart
       0x30 octets plus haut (800AF540) : les pointeurs sont relus a chaque tour, c'est une vraie attente.
       Taille du main.dol : 0 = toute taille, mais seulement si l'instruction d'origine est la bonne. */
    { "GYQE01", 0, 0x800AF570, 0x4080FFD0, NF_WORD, "attente de la video d'intro", 0x4080FFE8 },
    /* langue forcee (celle de la console, ou celle du reglage du jeu) */
    { "GMSP01", 4083072, 0x80289B4C, 0, NF_LANG, "langue" },
    { "GMSP01", 4083072, 0x80289B9C, 0, NF_LANG, "langue" },
    /* code anti-debogage retire (disques de demo, Pokemon Colosseum) */
#define NF_AD(ids, len, a) { ids, len, a, 0x60000000, NF_WORD, "anti-debogage" }
    NF_AD("D58E01 D59E01 D62E01", 4336128, 0x8000F614), NF_AD("D58E01 D59E01 D62E01", 4336128, 0x8005CC4C),
    NF_AD("D58E01 D59E01 D62E01", 4336128, 0x8005CD00), NF_AD("D58E01 D59E01 D62E01", 4336128, 0x8005CD24),
    NF_AD("D58E01 D59E01 D62E01", 4336128, 0x80086CCC), NF_AD("D58E01 D59E01 D62E01", 4336128, 0x80086DD0),
    NF_AD("D58E01 D59E01 D62E01", 4336128, 0x8008789C),
    NF_AD("D78P01 D86U01", 4548544, 0x800089D8), NF_AD("D78P01 D86U01", 4548544, 0x800569C8), NF_AD("D78P01 D86U01", 4548544, 0x80056A7C),
    NF_AD("D78P01 D86U01", 4548544, 0x80056AA0), NF_AD("D78P01 D86U01", 4548544, 0x800815F0), NF_AD("D78P01 D86U01", 4548544, 0x800816F4),
    NF_AD("D78P01 D86U01", 4548544, 0x80082090),
    NF_AD("D86P01 D89U01", 4101504, 0x80005614), NF_AD("D86P01 D89U01", 4101504, 0x80005E08), NF_AD("D86P01 D89U01", 4101504, 0x80005F10),
    NF_AD("D86P01 D89U01", 4101504, 0x80005F34), NF_AD("D86P01 D89U01", 4101504, 0x80038D24), NF_AD("D86P01 D89U01", 4101504, 0x80038E14),
    NF_AD("D86P01 D89U01", 4101504, 0x80038ECC),
    NF_AD("E22J01 E23J01", 4205664, 0x8000F510), NF_AD("E22J01 E23J01", 4205664, 0x8005C9A4), NF_AD("E22J01 E23J01", 4205664, 0x8005CA0C),
    NF_AD("E22J01 E23J01", 4205664, 0x8005CA30), NF_AD("E22J01 E23J01", 4205664, 0x80086448), NF_AD("E22J01 E23J01", 4205664, 0x8008654C),
    NF_AD("E22J01 E23J01", 4205664, 0x80086FDC),
    NF_AD("GC6E01", 3779808, 0x80005614), NF_AD("GC6E01", 3779808, 0x80005C24), NF_AD("GC6E01", 3779808, 0x80005D2C),
    NF_AD("GC6E01", 3779808, 0x80005D50), NF_AD("GC6E01", 3779808, 0x80036598), NF_AD("GC6E01", 3779808, 0x80036688),
    NF_AD("GC6E01", 3779808, 0x80036740),
    NF_AD("GC6J01", 3699680, 0x80005CA4),
    NF_AD("GC6P01", 4096576, 0x80005614), NF_AD("GC6P01", 4096576, 0x80005D1C), NF_AD("GC6P01", 4096576, 0x80005E24),
    NF_AD("GC6P01", 4096576, 0x80005E48), NF_AD("GC6P01", 4096576, 0x800387E4), NF_AD("GC6P01", 4096576, 0x800388D4),
    NF_AD("GC6P01", 4096576, 0x8003898C),
#undef NF_AD
};
static char na_fix_msg[120] = "";
static int na_id_in(const char *ids, const char *id) {    /* "GOND69 GONE69" contient-il ce code ? */
    for (const char *p = ids; *p; ) {
        int n = (int)strcspn(p, " ");
        if (n && !strncmp(p, id, n)) return 1;
        p += n; while (*p == ' ') p++;
    }
    return 0;
}
static int na_lang_now(void);
static int na_card_size_now(int virt);
static int na_lang_now(void) {                            /* langue du reglage du jeu, sinon celle de la console */
#if defined(NATIF_BENCH)
    return 0;
#elif defined(GEKKO)
    return jc_lang >= 0 ? jc_lang : SYS_GetLanguage();
#else
    return jc_lang >= 0 ? jc_lang : 0;
#endif
}
static int na_card_size_now(int virt) {                   /* taille (Mbit) de la vraie carte du slot A ; 0 = rien a changer */
    if (virt) return 0;                                   /* (carte virtuelle de Pulse : 16 Mbit, acceptee telle quelle) */
#ifdef GEKKO
    s32 mb = 0, sec = 0;
    u64 t0 = gettime();
    s32 r;
    while ((r = CARD_ProbeEx(CARD_SLOTA, &mb, &sec)) == CARD_ERROR_BUSY && ticks_to_millisecs(gettime() - t0) < 500) ;
    return r == CARD_ERROR_READY && mb > 32 ? (int)mb : 0;   /* (507 blocs et moins : deja acceptee) */
#else
    return 0;
#endif
}
/* applique les corrections du jeu au main.dol (avant le lancement) ; rend le nombre d'endroits changes */
static int na_game_fix(const nat_info_t *in, u8 *dol, int card_virtual) {
    int done = 0;
    const char *why = NULL;
    na_fix_msg[0] = 0;
    for (unsigned i = 0; i < sizeof(na_fixes) / sizeof(na_fixes[0]); i++) {
        const na_fix_t *f = &na_fixes[i];
        if ((f->len ? f->len != in->dol_len : !f->orig) || !na_id_in(f->ids, in->id)) continue;   /* (v136 : taille 0 = toute taille, instruction verifiee) */
        u32 fo = 0; int sec = -1;
        for (int k = 0; k < in->nsec; k++) if (f->addr >= in->sec_addr[k] && f->addr - in->sec_addr[k] < in->sec_sz[k]) { sec = k; fo = in->sec_off[k] + (f->addr - in->sec_addr[k]); }
        if (sec < 0 || fo + 4 > in->dol_len) continue;
        if (f->orig && be32(dol + fo) != f->orig) continue;   /* (v136 : pas l'instruction attendue : on ne touche a rien) */
        if (f->kind == NF_CARD) {                         /* (carte virtuelle de Pulse : 251 blocs, deja acceptee) */
            int sz = na_card_size_now(card_virtual);
            if (sz <= 0) continue;
            u32 v = (u32)(sz < 32 ? 64 : sz);
            dol[fo] = (u8)(v >> 8); dol[fo + 1] = (u8)v;
        } else if (f->kind == NF_LANG) na_wbe32(dol + fo, 0x38600000u | ((u32)na_lang_now() & 0xFFFF));
        else na_wbe32(dol + fo, f->val);
        done++; why = f->why;
    }
    if (done) snprintf(na_fix_msg, sizeof(na_fix_msg), "%s (%d endroit%s)", why, done, done > 1 ? "s" : "");
    return done;
}


/* ---------- v136 : les autres executables du disque (.dol, .elf, et ceux des .tgc) ----------
   Pulse ne reecrit pas l'ISO : GC Station cherche ces fichiers dans la table des fichiers du disque, et pour
   chaque correction dont la taille est celle d'un de ces fichiers, calcule la position dans l'ISO de l'endroit
   a changer (sections du .dol, segments du .elf) ; le resident le change au moment ou le jeu lit ce morceau
   (fx_apply). switcher.prs est compresse : pas possible ainsi (aucune correction de la table ne le vise). */
#define NA_FXMAX 48
static u32 na_fx[NA_FXMAX][3];                            /* {position dans l'ISO, valeur, taille} */
static int na_fx_n = 0;
static char na_fx_msg[160] = "";
static int na_map_dol(const u8 *h, u32 addr, u32 *fo) {  /* adresse -> position dans le fichier .dol (en-tete de 0x100) */
    for (int i = 0; i < 18; i++) {
        u32 off = be32(h + i * 4), at = be32(h + 0x48 + i * 4), sz = be32(h + 0x90 + i * 4);
        if (sz && addr >= at && addr - at < sz) { *fo = off + (addr - at); return 1; }
    }
    return 0;
}
static int na_map_elf(gdisc_t *g, u32 base, u32 addr, u32 *fo) {   /* ... dans un .elf (segments PT_LOAD) */
    u8 e[0x34], ph[32];
    if (!gd_read(g, base, e, sizeof(e)) || memcmp(e, "\x7F" "ELF", 4) || e[5] != 2) return 0;   /* (gros-boutiste) */
    u32 phoff = be32(e + 0x1C), phn = (u32)(e[0x2C] << 8 | e[0x2D]), phs = (u32)(e[0x2A] << 8 | e[0x2B]);
    if (phs < 32 || phn > 32) return 0;
    for (u32 i = 0; i < phn; i++) {
        if (!gd_read(g, base + phoff + i * phs, ph, sizeof(ph))) return 0;
        u32 type = be32(ph), off = be32(ph + 4), va = be32(ph + 8), fsz = be32(ph + 16);
        if (type == 1 && fsz && addr >= va && addr - va < fsz) { *fo = off + (addr - va); return 1; }
    }
    return 0;
}
/* un fichier candidat : base = position dans l'ISO, len = sa taille, elf = 1 si .elf */
static void na_fx_file(gdisc_t *g, const nat_info_t *in, u32 base, u32 len, int elf, int card_virtual, const char *name) {
    if (base == in->dol_off) return;                      /* (le main.dol : deja corrige avant le lancement) */
    u8 h[0x100];
    int hok = !elf && gd_read(g, base, h, sizeof(h));
    for (unsigned i = 0; i < sizeof(na_fixes) / sizeof(na_fixes[0]); i++) {
        const na_fix_t *f = &na_fixes[i];
        if (!f->len || f->len != len || !na_id_in(f->ids, in->id) || na_fx_n >= NA_FXMAX) continue;
        u32 fo = 0;
        if (!(elf ? na_map_elf(g, base, f->addr, &fo) : hok && na_map_dol(h, f->addr, &fo)) || fo + 4 > len) continue;
        u8 cur[4];
        if (f->orig && (!gd_read(g, base + fo, cur, 4) || be32(cur) != f->orig)) continue;
        u32 v = f->val, sz = 4;
        if (f->kind == NF_CARD) { int mb = na_card_size_now(card_virtual); if (mb <= 0) continue; v = (u32)(mb < 32 ? 64 : mb); sz = 2; }
        else if (f->kind == NF_LANG) v = 0x38600000u | ((u32)na_lang_now() & 0xFFFF);
        na_fx[na_fx_n][0] = base + fo; na_fx[na_fx_n][1] = v; na_fx[na_fx_n][2] = sz; na_fx_n++;
        snprintf(na_fx_msg, sizeof(na_fx_msg), "%s : %s", name, f->why);
    }
}
static int na_name_end(const char *n, const char *ext) { size_t a = strlen(n), b = strlen(ext); return a > b && !strcasecmp(n + a - b, ext); }
/* parcourt une table des fichiers (FST) ; delta : decalage des positions (fichiers d'un .tgc), depth : .tgc dans un .tgc */
static void na_fx_fst(gdisc_t *g, const nat_info_t *in, const u8 *fst, u32 fsz, s32 delta, int card_virtual, int depth) {
    if (fsz < 12) return;
    u32 n = be32(fst + 8);
    if (n < 1 || n * 12 > fsz) return;
    const char *names = (const char *)fst + n * 12;
    for (u32 i = 1; i < n; i++) {
        const u8 *e = fst + i * 12;
        if (e[0]) continue;                               /* (dossier) */
        u32 no = be32(e) & 0xFFFFFFu, off = be32(e + 4) + (u32)delta, len = be32(e + 8);
        if ((const u8 *)names + no >= fst + fsz) continue;
        const char *nm = names + no;
        if (na_name_end(nm, ".dol")) na_fx_file(g, in, off, len, 0, card_virtual, nm);
        else if (na_name_end(nm, ".elf")) na_fx_file(g, in, off, len, 1, card_virtual, nm);
        else if (na_name_end(nm, ".tgc") && depth == 0) {   /* .tgc : son default.dol et ses propres fichiers */
            u8 t[0x40];
            if (!gd_read(g, off, t, sizeof(t)) || be32(t) != 0xAE0F38A2u) continue;
            u32 dol_s = be32(t + 0x1C), dol_l = be32(t + 0x20), fst_s = be32(t + 0x10), fst_l = be32(t + 0x14), user = be32(t + 0x24), guser = be32(t + 0x34);
            if (dol_l) na_fx_file(g, in, off + dol_s, dol_l, 0, card_virtual, nm);
            if (fst_l && fst_l < 0x40000) {
                u8 *tf = (u8 *)malloc(fst_l);
                if (tf && gd_read(g, off + fst_s, tf, fst_l)) na_fx_fst(g, in, tf, fst_l, (s32)(off + user) - (s32)guser, card_virtual, 1);
                free(tf);
            }
        }
    }
}
static int na_fx_scan(const char *path, const nat_info_t *in, const u8 *fst, int card_virtual) {
    na_fx_n = 0; na_fx_msg[0] = 0;
    int any = 0;                                          /* (rien a chercher si aucune correction ne vise ce jeu) */
    for (unsigned i = 0; i < sizeof(na_fixes) / sizeof(na_fixes[0]) && !any; i++) any = na_id_in(na_fixes[i].ids, in->id) && na_fixes[i].len && na_fixes[i].len != in->dol_len;
    if (!any) return 0;
    gdisc_t g;
    if (!gd_open(&g, path)) return 0;
    na_fx_fst(&g, in, fst, in->fst_sz, 0, card_virtual, 0);
    gd_close(&g);
    return na_fx_n;
}
static int na_rm_patch(const nat_info_t *in, u8 *dol) {
    (void)in;
    static const u8 flat[7] = { 0, 0, 21, 22, 21, 0, 0 };
    int found = 0, changed = 0, prog = 0, p60 = 0, ds = 0;
    na_rm_msg[0] = 0;
    if (!nat_vmode && !nat_deflick && !nat_wide) return 0;
    for (int sct = 0; sct < 18; sct++) {
        u32 off = be32(dol + sct * 4), sz = be32(dol + 0x90 + sct * 4);
        if (!sz || off < 0x100) continue;
        for (u32 o = 0; o + 0x3C <= sz; o += 4) {
            u8 *p = dol + off + o;
            if (!na_rm_ok(p)) continue;
            found++;
            u32 tv = be32(p), fmt = tv >> 2, md = tv & 3, ch = 0;
            if ((nat_vmode == 1 || nat_vmode == 2 || nat_vmode == 3) && fmt == 1 && md != 1) {   /* PAL 50 Hz -> 480 lignes, 60 Hz */
                if (na_be16(p + 6) > 480) na_wbe16(p + 6, 480);
                if (na_be16(p + 8) > 480) na_wbe16(p + 8, 480);
                if (na_be16(p + 16) > 480) na_wbe16(p + 16, 480);
                na_wbe16(p + 12, (480 - na_be16(p + 16)) / 2);
                fmt = 5; tv = fmt << 2 | md; ch = 1; p60++;
            }
            if (nat_vmode == 1 && md == 0) {                     /* entrelace -> progressif (480p) */
                tv = 0u << 2 | 2;                                /* (NTSC progressif : memes timings que EURGB60) */
                na_wbe32(p + 20, 0); p[24] = 0;                  /* (une seule trame, pas de rendu par trame) */
                memcpy(p + 50, flat, 7);
                ch = 1; prog++;
            } else if (nat_vmode == 3 && md == 0 && na_be16(p + 16) >= 448) {   /* entrelace 480 lignes -> 240p */
                tv = fmt << 2 | 1;
                na_wbe16(p + 8, 240); na_wbe16(p + 16, 240); na_wbe16(p + 12, na_be16(p + 12) / 2);
                na_wbe32(p + 20, 0); p[24] = 0;
                memcpy(p + 50, flat, 7);
                ch = 1; ds++;
            }
            na_wbe32(p, tv);
            if (nat_deflick && memcmp(p + 50, flat, 7)) { memcpy(p + 50, flat, 7); ch = 1; }
            if (nat_wide && na_be16(p + 14) == 640) { na_wbe16(p + 14, 704); na_wbe16(p + 10, 8); ch = 1; }
            if (ch) changed++;
            o += 0x38;                                       /* (le suivant ne peut pas chevaucher) */
        }
    }
    snprintf(na_rm_msg, sizeof(na_rm_msg), "%d mode%s video trouve%s, %d modifie%s (480p %d, 60 Hz %d, 240p %d)%s%s",
             found, found > 1 ? "s" : "", found > 1 ? "s" : "", changed, changed > 1 ? "s" : "", prog, p60, ds,
             nat_deflick ? ", sans flou" : "", nat_wide ? ", image large" : "");
    return changed;
}

/* ---------- v121 : cheats dans Pulse (codes Gecko : voir gk_run dans natres.c) ---------- */
#define NA_GKMAX 4096
static u32 na_gk[NA_GKMAX];
static int na_gk_n = 0;
static char na_gk_msg[120] = "";
#ifndef NATIF_BENCH
static int ch_collect(const char *id, u32 *out, int cap);  /* (cheats.c) */
#endif
/* les codes sont-ils tous compris par Pulse ? (sinon : lanceur de secours) */
static int na_gk_ok(const u32 *w, int n, char *why, int cap) {
    for (int i = 0; i + 1 < n; i += 2) {
        u32 a = w[i], b = w[i + 1], t = a >> 24, ct = t & 0xEE;
        if (t == 0xF0) return 1;
        if (t == 0xE0 || t == 0xDE || t == 0xCE) continue;
        if (t == 0xE2) { if (((a >> 20) & 0xF) > 1) goto no; continue; }
        if (t < 0x40 && (ct == 0x00 || ct == 0x02 || ct == 0x04 || (ct >= 0x20 && ct <= 0x2E))) continue;
        if (t < 0x40 && ct == 0x06) { i += (int)(((b + 7) >> 3) * 2); continue; }
        if (t < 0x40 && ct == 0x08) { if (i + 3 >= n || (w[i + 2] >> 28) > 2) goto no; i += 2; continue; }
        if (t == 0x40 || t == 0x42 || t == 0x48 || t == 0x4A) { if (((a >> 20) & 0xF) > 1 || ((a >> 16) & 0xF) > 1 || ((a >> 12) & 0xF)) goto no; continue; }
        if (t == 0xC2 || t == 0xC3 || t == 0xC0) { if (!b || i + 2 + (int)b * 2 > n) goto no; i += (int)b * 2; continue; }
    no:
        snprintf(why, cap, "code %08X %08X pas compris par Pulse", (unsigned)a, (unsigned)b);
        return 0;
    }
    return 1;
}
/* rend 1 = codes prets pour Pulse, 0 = aucun cheat active, -1 = cheats a confier au lanceur de secours */
static int nat_cheats_prep(const char *id) {
    na_gk_n = 0; na_gk_msg[0] = 0;
    if (nat_nocheat) { snprintf(na_gk_msg, sizeof(na_gk_msg), "coup\xC3\xA9s (r\xC3\xA9glage du jeu)"); return 0; }   /* v140 */
    int n = ch_collect(id, na_gk, NA_GKMAX);
    if (n == 0) return 0;
    if (n < 0) { snprintf(na_gk_msg, sizeof(na_gk_msg), "trop de codes"); return -1; }
    if (!na_gk_ok(na_gk, n, na_gk_msg, sizeof(na_gk_msg))) return -1;
    na_gk_n = n;
    snprintf(na_gk_msg, sizeof(na_gk_msg), "%d ligne%s de code appliqu\xC3\xA9" "e%s par Pulse", n / 2, n > 2 ? "s" : "", n > 2 ? "s" : "");
    return 1;
}

/* ---------- v121 : jeux a 2 disques : l'autre disque (meme ID, autre numero) ---------- */
static natres_ext_t na_ext2[NATRES_MAXEXT];
static u32 na_n2 = 0;
static char na_disc2_msg[140] = "";
static int na_disc2(int k) {
    for (int j = 0; j < gl_n; j++)
        if (j != k && !gl[j].dvd && !strncmp(gl[j].id, gl[k].id, 6) && gl[j].disc != gl[k].disc) return j;
    return -1;
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
    {
        gl_status("Analyse native : position du jeu sur la carte...");
        mok = nat_map_game(gl[k].path, gl[k].ciso, me, sizeof(me));   /* (v121 : .ciso aussi) */
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
static int na_drv_ok = 1;                               /* v133 : le vrai lecteur repond (sinon : Pulse reveille le jeu sans lui) */
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
    na_drv_ok = ok;
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
static void na_drive_wake(void) {
    snprintf(na_drv, sizeof(na_drv), "(simulateur)");
#ifdef NATIF_BENCH
    na_drv_ok = !getenv("NATIF_NODRIVE");                /* (banc d'essai : console sans lecteur) */
#endif
}
#endif

/* ---------- v65 : ecran de chargement de Resident, dans le style de GC Station (degrade,
   jaquette du jeu avec halo, panneau arrondi, barre lumineuse, etapes cochees) ---------- */
static char nat_ld_title[96], nat_ld_step[96];
static int nat_ld_pct = 0, nat_ld_from = 0, nat_ld_to = 0, nat_ld_k = -1;
static u64 nat_ld_t0 = 0, nat_ld_last = 0;
#ifdef NATIF_BENCH
static void nat_draw_loading(void) {}
#else
/* v111 : fond commun : noir et aurores (force 0..1) */
static void nl_bg(f32 t, f32 k) {
    gx_vgrad(0, 0, 640, 480, RGBA(8, 6, 16, 255), RGBA(3, 3, 7, 255));
    for (int band = 0; band < 3; band++) {
        static const f32 base[3] = { 70, 418, 446 }, amp[3] = { 14, 20, 11 }, th[3] = { 60, 90, 50 }, sp[3] = { 0.30f, 0.22f, 0.40f };
        static const u8 al[3] = { 34, 40, 24 };
        for (int i = 0; i < 24; i++) {
            f32 x0 = -10 + i * 660.0f / 24, x1 = -10 + (i + 1) * 660.0f / 24;
            f32 y0 = base[band] + amp[band] * sinf(x0 * 0.012f + t * sp[band] + band * 2.1f);
            f32 y1 = base[band] + amp[band] * sinf(x1 * 0.012f + t * sp[band] + band * 2.1f);
            u8 a = (u8)(al[band] * k);
            col_t c0 = pl_grad((f32)i / 24, a), c1 = pl_grad((f32)(i + 1) / 24, a), z0 = c0, z1 = c1;
            z0.a = 0; z1.a = 0;
            gx_poly4(x0, y0 - th[band], x1, y1 - th[band], x1, y1, x0, y0, z0, z1, c1, c0);
            gx_poly4(x0, y0, x1, y1, x1, y1 + th[band] * 0.35f, x0, y0 + th[band] * 0.35f, c0, c1, z1, z0);
        }
    }
}
/* barre en degrade (avancement en douceur) ; rend la valeur affichee */
static f32 nl_shown = -1;
static f32 nl_progress(void) {
    if (nl_shown < 0 || nl_shown > nat_ld_pct + 0.5f || nat_ld_pct < 5) nl_shown = (f32)nat_ld_pct;
    else nl_shown += ((f32)nat_ld_pct - nl_shown) * (1.0f - expf(-ui_dt * 10.0f));
    return nl_shown;
}
static void nl_bar(f32 bx, f32 by, f32 bw, f32 bh, f32 shown, f32 t) {
    f32 fw = bw * shown / 100.0f;
    gx_rrect_fill(bx, by, bw, bh, bh / 2, RGBA(255, 255, 255, 22), RGBA(255, 255, 255, 22));
    if (fw >= bh) {
        gx_glow(bx, by, fw, bh, bh / 2, pl_grad(0.3f, 120), 4);
        gx_rrect_fill(bx, by, bh, bh, bh / 2, pl_grad(0, 255), pl_grad(0, 255));
        gx_rrect_fill(bx + fw - bh, by, bh, bh, bh / 2, pl_grad(fw / bw * 0.8f, 255), pl_grad(fw / bw * 0.8f, 255));
        gx_hgrad(bx + bh / 2, by, fw - bh, bh, pl_grad(0, 255), pl_grad(fw / bw * 0.8f, 255));
        f32 sh = fmodf(t * 160.0f, fw + 60.0f) - 30.0f;
        if (sh > 2 && sh < fw - 10) gx_hgrad(bx + sh, by + 1, 8, bh - 2, RGBA(255, 255, 255, 0), RGBA(255, 255, 255, 140));
        gx_circle_glow(bx + fw - bh / 2, by + bh / 2, bh * 0.6f, RGBA(255, 255, 255, 200), 5);
    }
}
static void nl_title(char *out, int cap) {
    char b[120];
    text_display(nat_ld_title, b, sizeof(b));
    snprintf(out, cap, "%s", b);
}
/* 1 : Coeur : le logo en grand au centre, qui bat ; le nom du jeu et la barre dessous */
static void nl_coeur(f32 t) {
    char b[120], fv[120];
    nl_bg(t, 0.8f);
    f32 ph = fmodf(t, 1.1f) / 1.1f, beat = expf(-ph * 9.0f);
    gx_circle(320, 168, 190 + 30 * beat, pl_grad(0.3f, (u8)(40 + 40 * beat)), pl_grad(0.3f, 0));
    pulse_logo(320, 168, 72, t);
    nl_title(b, sizeof(b));
    fit_text(F_LARGE, b, 520, fv, sizeof(fv));
    gx_text_c(F_LARGE, 320, 318, fv, RGBA(255, 255, 255, 255));
    f32 sh = nl_progress();
    nl_bar(170, 336, 300, 6, sh, t);
    snprintf(b, sizeof(b), "%s  \xC2\xB7  %d %%", nat_ld_step, nat_ld_pct);
    fit_text(F_SMALL, b, 500, fv, sizeof(fv));
    gx_text_c(F_SMALL, 320, 364, fv, RGBA(175, 175, 200, 255));
    if (nat_igr) gx_text_c(F_SMALL, 320, 430, TR("Pendant le jeu : A + Z + START = retour \xC3\xA0 GC Station"), RGBA(130, 130, 160, 255));
    pulse_word(F_MED, 296, 456, 1.0f);
}
/* 2 : Cinema : grande jaquette, nom du jeu en grand, une ligne de battement qui se dessine (avancement) */
static void nl_cinema(f32 t) {
    char b[120], fv[120];
    nl_bg(t, 0.6f);
    const f32 cx = 40, cy = 64, cw = 222, chh = 296;
    gx_circle(cx + cw / 2, cy + chh / 2, 240, pl_grad(0.35f, 70), pl_grad(0.35f, 0));
    gx_glow(cx, cy, cw, chh, 12, pl_grad(0.3f + 0.2f * sinf(t * 2), 190), 7);
    nv_raw = 0;
    if (nat_ld_k >= 0 && nat_ld_k < gl_n) gl_draw_cover(nat_ld_k, cx, cy, cw, chh, 1);
    else gx_rrect_fill(cx, cy, cw, chh, 12, RGBA(30, 24, 60, 255), RGBA(12, 10, 26, 255));
    nv_raw = 1;
    pulse_logo(300, 92, 14, t);
    pulse_word(F_MED, 322, 100, 1.0f);
    nl_title(b, sizeof(b));
    char l1[120], l2[120];
    wrap2(F_LARGE, b, 310, l1, l2, sizeof(l1));
    txt_scale = 1.15f;
    gx_text1(F_LARGE, 290, 160, l1, RGBA(255, 255, 255, 255));
    if (l2[0]) gx_text1(F_LARGE, 290, 194, l2, RGBA(255, 255, 255, 255));
    txt_scale = 1.0f;
    fit_text(F_SMALL, nat_ld_step, 300, fv, sizeof(fv));
    gx_text1(F_SMALL, 292, 236, fv, RGBA(175, 175, 200, 255));
    /* ligne de battement qui avance avec le chargement */
    f32 sh = nl_progress();
    const f32 lx0 = 290, lx1 = 610, ly = 300;
    f32 xe = lx0 + (lx1 - lx0) * sh / 100.0f;
    gx_rect(lx0, ly, lx1 - lx0, 1, RGBA(255, 255, 255, 30));
    f32 px = lx0, py = ly;
    for (f32 x = lx0 + 3; x <= xe; x += 3) {
        f32 u = fmodf((x - lx0) / 64.0f, 1.0f), y = ly;
        if (u > 0.40f && u < 0.47f) y = ly - 30 * (u - 0.40f) / 0.07f;
        else if (u >= 0.47f && u < 0.55f) y = ly - 30 + 52 * (u - 0.47f) / 0.08f;
        else if (u >= 0.55f && u < 0.61f) y = ly + 22 - 22 * (u - 0.55f) / 0.06f;
        col_t c = pl_grad((x - lx0) / (lx1 - lx0), 255);
        pl_seg(px, py, x, y, 6, RGBA(c.r, c.g, c.b, 60), RGBA(c.r, c.g, c.b, 60));
        pl_seg(px, py, x, y, 2.4f, c, c);
        px = x; py = y;
    }
    gx_circle_glow(px, py, 4, RGBA(255, 255, 255, 220), 5);
    gx_circle(px, py, 3, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
    snprintf(b, sizeof(b), "%d %%", nat_ld_pct);
    gx_text_r(F_MED, 610, 346, b, RGBA(235, 235, 250, 255));
    if (nat_igr) gx_text1(F_SMALL, 290, 400, TR("Pendant le jeu : A + Z + START = retour \xC3\xA0 GC Station"), RGBA(130, 130, 160, 255));
}
/* 3 : Minimal : noir, le logo de Pulse qui bat (v113 : identique au logo), le nom du jeu, une fine barre */
static void nl_minimal(f32 t) {
    char b[120], fv[120];
    gx_vgrad(0, 0, 640, 480, RGBA(4, 4, 8, 255), RGBA(0, 0, 0, 255));
    const f32 cx = 320, cy = 190;
    pulse_logo(cx, cy, 88, t);
    f32 sh = nl_progress();
    const f32 bx = 220, bw = 200, by = 370;                      /* (avancement : fine ligne en degrade) */
    gx_rrect_fill(bx, by, bw, 2, 1, RGBA(255, 255, 255, 26), RGBA(255, 255, 255, 26));
    if (sh > 0.5f) gx_hgrad(bx, by, bw * sh / 100.0f, 2, pl_grad(0, 255), pl_grad(sh / 100.0f, 255));
    nl_title(b, sizeof(b));
    fit_text(F_MED, b, 520, fv, sizeof(fv));
    gx_text_c(F_MED, cx, 350, fv, RGBA(235, 235, 250, 255));
    snprintf(b, sizeof(b), "%d %%", nat_ld_pct);
    gx_text_c(F_SMALL, cx, 392, b, RGBA(130, 130, 160, 255));
    gx_text_c(F_SMALL, cx, 456, "PULSE", RGBA(90, 90, 120, 255));
}
static int nl_style = -1;                                       /* (choisi au debut de chaque lancement) */
static void nl_aurore(f32 t);
static void nat_draw_loading(void) {
    f32 t = (f32)(now_ms() % 100000u) / 1000.0f;
    if (nl_style < 0) nl_style = pulse_ld >= 0 && pulse_ld < 4 ? pulse_ld : (int)(now_ms() % 4);
    nv_raw = 1;
    switch (nl_style) {
    case 1: nl_coeur(t); break;
    case 2: nl_cinema(t); break;
    case 3: nl_minimal(t); break;
    default: nl_aurore(t); break;
    }
    nv_raw = 0;
}
/* 0 : Aurore (v109) : noir et aurores, logo qui bat, jaquette, etapes */
static void nl_aurore(f32 t) {
    char b[120], fv[120];
    nv_raw = 1;
    nl_bg(t, 1.0f);
    /* marque : logo qui bat + Pulse */
    pulse_logo(62, 54, 27, t);
    int ww = pulse_word(F_LARGE, 106, 66, 1.4f);
    gx_text1(F_SMALL, 112 + ww, 66, TR("lanceur GC Station"), RGBA(150, 150, 180, 255));
    /* jaquette */
    const f32 cx = 72, cy = 104, cw = 192, chh = 256;
    gx_circle(cx + cw / 2, cy + chh / 2, 200, pl_grad(0.35f, 60), pl_grad(0.35f, 0));
    gx_glow(cx, cy, cw, chh, 10, pl_grad(0.3f + 0.2f * sinf(t * 2), 170), 6);
    nv_raw = 0;
    if (nat_ld_k >= 0 && nat_ld_k < gl_n) gl_draw_cover(nat_ld_k, cx, cy, cw, chh, 1);
    else {
        gx_rrect_fill(cx, cy, cw, chh, 10, RGBA(44, 26, 88, 255), RGBA(22, 12, 46, 255));
        gx_circle(cx + cw / 2, cy + chh / 2, 62, RGBA(200, 190, 230, 255), RGBA(120, 110, 160, 255));   /* (un disque) */
        gx_circle(cx + cw / 2, cy + chh / 2, 14, RGBA(22, 12, 46, 255), RGBA(22, 12, 46, 255));
    }
    nv_raw = 1;
    gx_rrect_stroke(cx - 1, cy - 1, cw + 2, chh + 2, 10, 1.5f, RGBA(255, 255, 255, 60));
    /* panneau en verre */
    const f32 px = 288, py = 104, pw = 304, ph = 256;
    gx_rrect_fill(px, py, pw, ph, 18, RGBA(255, 255, 255, 12), RGBA(255, 255, 255, 6));
    gx_rrect_stroke(px, py, pw, ph, 18, 1.0f, RGBA(255, 255, 255, 26));
    gx_hgrad(px + pw / 2 - 110, py, 110, 1.2f, pl_grad(0, 0), pl_grad(0, 160));
    gx_hgrad(px + pw / 2, py, 110, 1.2f, pl_grad(0.7f, 160), pl_grad(0.7f, 0));
    pulse_logo(px + 30, py + 26, 9, 0);
    gx_text1(F_SMALL, px + 46, py + 31, "PULSE", RGBA(255, 255, 255, 255));
    gx_text_r(F_SMALL, px + pw - 18, py + 31, TR("Lancement du jeu"), RGBA(150, 150, 180, 255));
    text_display(nat_ld_title, b, sizeof(b));
    fit_text(F_MED, b, (int)pw - 36, fv, sizeof(fv));
    gx_text1(F_MED, px + 18, py + 70, fv, RGBA(255, 255, 255, 255));
    /* barre en degrade */
    const f32 bx = px + 18, by = py + 90, bw = pw - 36, bh = 8;
    static f32 shown = -1;                                       /* v72 : la barre glisse vers sa valeur */
    if (shown < 0 || shown > nat_ld_pct + 0.5f || nat_ld_pct < 5) shown = (f32)nat_ld_pct;
    else shown += ((f32)nat_ld_pct - shown) * (1.0f - expf(-ui_dt * 10.0f));
    f32 fw = bw * shown / 100.0f;
    gx_rrect_fill(bx, by, bw, bh, 4, RGBA(255, 255, 255, 22), RGBA(255, 255, 255, 22));
    if (fw >= 8) {
        gx_glow(bx, by, fw, bh, 4, pl_grad(0.3f, 120), 4);
        gx_rrect_fill(bx, by, 8, bh, 4, pl_grad(0, 255), pl_grad(0, 255));
        gx_rrect_fill(bx + fw - 8, by, 8, bh, 4, pl_grad(fw / bw * 0.8f, 255), pl_grad(fw / bw * 0.8f, 255));
        gx_hgrad(bx + 4, by, fw - 8, bh, pl_grad(0, 255), pl_grad(fw / bw * 0.8f, 255));
        f32 sh = fmodf(t * 160.0f, fw + 60.0f) - 30.0f;           /* (reflet qui court) */
        if (sh > 2 && sh < fw - 10) gx_hgrad(bx + sh, by + 1, 8, bh - 2, RGBA(255, 255, 255, 0), RGBA(255, 255, 255, 140));
        gx_circle_glow(bx + fw - 4, by + bh / 2, 5, RGBA(255, 255, 255, 200), 5);
    }
    snprintf(b, sizeof(b), "%d %%", nat_ld_pct);
    gx_text_r(F_SMALL, bx + bw, by + 26, b, RGBA(235, 235, 250, 255));
    fit_text(F_SMALL, nat_ld_step, (int)bw - 50, fv, sizeof(fv));
    gx_text1(F_SMALL, bx, by + 26, fv, RGBA(175, 175, 200, 255));
    /* etapes */
    static const char *const st[5] = { "Analyse du jeu", "Carte SD : jeu et sauvegardes", "Lecture du jeu", "Pr\xC3\xA9paration du lecteur", "D\xC3\xA9marrage" };
    static const int at[6] = { 0, 10, 22, 40, 100, 101 };
    for (int i = 0; i < 5; i++) {
        f32 ry = by + 52 + i * 21;
        int done = nat_ld_pct >= at[i + 1], cur = !done && nat_ld_pct >= at[i];
        if (done) {
            gx_circle(bx + 7, ry - 4, 7, pl_grad(i / 5.0f, 255), pl_grad(i / 5.0f + 0.15f, 255));
            gx_tri(bx + 3, ry - 4, bx + 6, ry - 1, bx + 6, ry + 1, RGBA(255, 255, 255, 255));   /* (coche) */
            gx_poly4(bx + 5, ry - 1, bx + 10, ry - 8, bx + 12, ry - 7, bx + 6, ry + 1,
                     RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
        } else if (cur) {
            f32 p = 0.5f + 0.5f * sinf(t * 6.0f);
            gx_circle_glow(bx + 7, ry - 4, 6, pl_grad(0.2f, (u8)(120 + 120 * p)), 4);
            gx_ring(bx + 7, ry - 4, 5, 7, pl_grad(0.3f, 255));
        } else gx_ring(bx + 7, ry - 4, 5.5f, 7, RGBA(90, 90, 120, 255));
        gx_text1(F_SMALL, bx + 22, ry + 1, TR(st[i]), done ? RGBA(200, 200, 225, 255) : cur ? RGBA(255, 255, 255, 255) : RGBA(110, 110, 140, 255));
    }
    /* bas */
    if (nat_igr) gx_text_c(F_SMALL, 320, 400, TR("Pendant le jeu : A + Z + START = retour \xC3\xA0 GC Station"), RGBA(170, 170, 200, 255));
    if (nat_card) gx_text_c(F_SMALL, 320, 420, nat_card >= 2 ? TR("Sauvegardes : cartes m\xC3\xA9moire virtuelles (slots A et B, sur la carte SD)") : TR("Sauvegardes : carte m\xC3\xA9moire virtuelle (slot A, sur la carte SD)"), RGBA(130, 130, 160, 255));
    gx_text_r(F_SMALL, 616, 460, "GC STATION  \xC2\xB7  PULSE", RGBA(150, 150, 180, 255));
    nv_raw = 0;
}
#endif
#if !defined(GEKKO) && !defined(NATIF_BENCH)
static void nat_sim_loading(void) {
    static int once = 0;
    if (once || !getenv("PCSIM_CHARGEMENT")) return;
    once = 1;
    nl_style = atoi(getenv("PCSIM_CHARGEMENT")) - 1;            /* (1 = Aurore, 2 = Coeur, 3 = Cinema, 4 = Minimal) */
    if (nl_style < 0 || nl_style > 3) nl_style = 0;
    snprintf(nat_ld_title, sizeof(nat_ld_title), "Mario Superstar Baseball");
    snprintf(nat_ld_step, sizeof(nat_ld_step), "%s", TR("Lecture du jeu"));
    nat_ld_k = gl_n > 1 ? 1 : -1;
    nat_ld_pct = 32;
    nat_ld_on = 1;
}
#endif
static u32 nat_ld_rc = 0;
static void nat_ld_draw(void) {                          /* v72 : une image, sans attendre l'ecran (le chargement continue) */
    present_nowait = retrace_count != nat_ld_rc;         /* (l'image d'avant est deja a l'ecran : pas d'attente ; sinon on attend, sans dechirure) */
    draw(); present_nowait = 0;
    nat_ld_rc = retrace_count; nat_ld_last = now_ms();
}
static void nat_ld(const char *step, int pct) {         /* une etape (et dessine tout de suite) */
#ifndef NATIF_BENCH
    if (!nat_ld_on) { nl_style = -1; nl_shown = -1; }      /* v111 : nouveau lancement : style (au hasard) choisi de nouveau */
#endif
    snprintf(nat_ld_step, sizeof(nat_ld_step), "%s", TR(step));
    nat_ld_pct = nat_ld_from = nat_ld_to = pct;
    nat_ld_on = 1;
    crumb("resident : %s", step);
    nat_ld_draw();
}
static void nat_ld_span(const char *step, int from, int to) {   /* etape longue : avance avec le temps (7 s) */
    nat_ld(step, from);
    nat_ld_from = from; nat_ld_to = to; nat_ld_t0 = now_ms();
}
static void nat_ld_tick(void) {                          /* (appelee souvent pendant le chargement : une image par balayage de l'ecran) */
    if (!nat_ld_on) return;
    u64 n = now_ms();
    if (retrace_count == nat_ld_rc && n - nat_ld_last < 33) return;
    if (n - nat_ld_last < 12) return;
    if (nat_ld_to > nat_ld_from) { u64 e = n - nat_ld_t0; if (e > 7000) e = 7000; nat_ld_pct = nat_ld_from + (int)((nat_ld_to - nat_ld_from) * e / 7000); }
    nat_ld_draw();
}
/* v72 : lecture du jeu par morceaux de 128 Ko, l'ecran de chargement continue de bouger entre deux */
static int na_read_ticked(gdisc_t *d, u32 off, u8 *out, u32 len, int pct_from, int pct_to) {
    u32 done = 0;
    while (done < len) {
        u32 n = len - done > 131072u ? 131072u : len - done;
        if (!gd_read(d, off + done, out + done, n)) return 0;
        done += n;
        if (pct_to > pct_from) nat_ld_pct = pct_from + (int)((u64)(pct_to - pct_from) * done / len);
        nat_ld_tick(); service_audio();
    }
    return 1;
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
static char vc_active[64] = "carte-A.raw";                /* v73 : carte choisie (Outils > Cartes virtuelles) */
static const char *na_card_file(int part) {
    static char p[2][160];
    snprintf(p[part], sizeof(p[0]), NA_CARD_DIR "/%s%s", vc_active, part ? ".part" : "");
    return p[part];
}
#define NA_CARD      na_card_file(0)
#define NA_CARD_NEW  (2u << 20)
static natres_ext_t na_card[NATRES_CARDMAX], na_card2[NATRES_CARDMAX];
static u32 na_card_n = 0, na_card_size = 0, na_card2_n = 0, na_card2_size = 0;
static char na_card_msg[128] = "", na_card2_msg[128] = "";
static int nat_card_reg = 0;                              /* v122 : une carte par region du jeu (reglage) */
/* v122 : nom du fichier de la carte (slot 0 = A, 1 = B), avec la region du jeu si le reglage est mis :
   carte-A.raw -> carte-A-EUR.raw (sauf carte choisie pour ce jeu : prise telle quelle) */
static const char *na_card_name(int slot, const char *id) {
    static char nm[2][96];
    const char *base = slot ? "carte-B.raw" : vc_active;
    if (!nat_card_reg || (!slot && vc_keep[0]) || !id) { snprintf(nm[slot], sizeof(nm[0]), "%s", base); return nm[slot]; }
    const char *reg = id[3] == 'J' ? "JAP" : na_game_pal(id) ? "EUR" : "USA";
    int bl = (int)strlen(base);
    if (bl > 4 && !strcasecmp(base + bl - 4, ".raw")) bl -= 4;
    snprintf(nm[slot], sizeof(nm[0]), "%.*s-%s.raw", bl, base, reg);
    return nm[slot];
}
/* prepare une carte (fichier cree s'il manque) ; rend 1 si prete (morceaux dans out) */
static int na_card_map(const char *name, natres_ext_t *out, u32 *n, u32 *size, char *msg, int cap) {
    *n = 0; *size = 0;
    if (sd_virtual) { snprintf(msg, cap, "pas de vraie carte SD"); return 0; }
    char path[192], part[200];
    snprintf(path, sizeof(path), NA_CARD_DIR "/%s", name);
    snprintf(part, sizeof(part), "%s.part", path);
    struct stat st;
    mkdir(NA_CARD_DIR, 0777);
    if (stat(path, &st) != 0) {
        nat_ld("Pr\xC3\xA9paration de la carte m\xC3\xA9moire virtuelle", 12);
        static u8 ff[32768];
        memset(ff, 0xFF, sizeof(ff));
        FILE *f = fopen(part, "wb");
        int ok = f != NULL;
        for (u32 o = 0; ok && o < NA_CARD_NEW; o += sizeof(ff)) { if (fwrite(ff, 1, sizeof(ff), f) != sizeof(ff)) ok = 0; nat_ld_tick(); service_audio(); }
        if (f && fclose(f) != 0) ok = 0;
        if (!ok || rename(part, path) != 0 || stat(path, &st) != 0) { remove(part); snprintf(msg, cap, "creation du fichier impossible"); return 0; }
    }
    u32 sz = (u32)st.st_size;
    if (sz < 0x80000u || sz > 0x1000000u || (sz & (sz - 1))) {      /* (on ne touche pas a un fichier inconnu) */
        snprintf(msg, cap, "%s : taille %u octets, pas une image de carte (512 Ko a 16 Mo)", name, (unsigned)sz); return 0;
    }
    char e[80];
    if (nat_map(path, e, sizeof(e))) { snprintf(msg, cap, "%s", e); return 0; }
    if (na_next > NATRES_CARDMAX) { snprintf(msg, cap, "%s trop morcele sur la carte SD (%u morceaux)", name, (unsigned)na_next); return 0; }
    memcpy(out, na_ext, na_next * sizeof(natres_ext_t));
    *n = na_next; *size = sz;
    snprintf(msg, cap, "active (gc-mp3/resident/%s, %u blocs)", name, (unsigned)(sz / 8192u - 5u));
    return 1;
}
/* rend bit 0 = slot A pret, bit 1 = slot B pret */
static int nat_card_prep(const char *id) {
    int r = 0;
    na_card_n = na_card2_n = 0; na_card2_msg[0] = 0;
    if (!nat_card || nat_nocard) { snprintf(na_card_msg, sizeof(na_card_msg), nat_nocard ? "coupee (reglage du jeu)" : "coupee (reglage)"); return 0; }
    if (na_card_map(na_card_name(0, id), na_card, &na_card_n, &na_card_size, na_card_msg, sizeof(na_card_msg))) r |= 1;
    if (nat_card >= 2 && na_card_map(na_card_name(1, id), na_card2, &na_card2_n, &na_card2_size, na_card2_msg, sizeof(na_card2_msg))) r |= 2;
    return r;
}

#ifdef GEKKO
/* v111 : interruptions d'ecran comme les met le BIOS (__VIInit du SDK) : DI0 au milieu de l'image ET DI1 a la
   1re ligne. libogc (GC Station) laisse DI1 coupee ; le jeu n'avait alors plus que DI0, toujours dans la 2e
   trame, et son passage en 480p (applique seulement sur la 1re trame) attendait pour toujours, ecran
   deja au noir (Mario Superstar Baseball). vb : 1 = PAL (625 lignes), sinon 525 lignes. */
static void na_vi_irqs(u32 vb) {
    volatile unsigned short *vi = (volatile unsigned short *)0xCC002000u;
    u32 nh = vb == 1 ? 625 : 525, hlw = vb == 1 ? 432 : 429;
    vi[24] = (unsigned short)((nh / 2 + 1) | 0x1000); vi[25] = (unsigned short)(hlw + 1);   /* DI0 */
    vi[26] = 0x1001; vi[27] = 1;                                                       /* DI1 */
}
#endif
static const char *nat_boot(int k) {
    if (k < 0 || k >= gl_n) return "Jeu introuvable";
    static nat_info_t in;
    snprintf(nat_ld_title, sizeof(nat_ld_title), "%s", gl[k].title);
    nat_ld_k = k;
    nat_ld("Analyse du jeu", 4);
#ifndef NATIF_BENCH
    gl_stats_bump(k);                                      /* v73 : les lancements par Resident comptent aussi */
    tl_recent_add(gl[k].path);
#endif
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
    int cards = nat_card_prep(in.id);                      /* v65 : carte memoire virtuelle ; v122 : bit 1 = slot B aussi */
    int card_on = cards != 0;
    u32 snap_sec = nat_snap_prep();                        /* v83 : photo de la memoire au plantage */
    u32 log_sec = nat_journal_prep();                      /* v54 : journal du jeu (un secteur de la carte) */
    na_n2 = 0; na_disc2_msg[0] = 0;                        /* v121 : l'autre disque (jeux a 2 disques) */
    int k2 = na_disc2(k);
    if (k2 >= 0) {
        char e2[120];
        nat_ld("Position de l'autre disque sur la carte SD", 14);
        if (nat_map_game(gl[k2].path, gl[k2].ciso, e2, sizeof(e2))) snprintf(na_disc2_msg, sizeof(na_disc2_msg), "disque %d : %s", gl[k2].disc + 1, e2);
        else { memcpy(na_ext2, na_ext, na_next * sizeof(natres_ext_t)); na_n2 = na_next; snprintf(na_disc2_msg, sizeof(na_disc2_msg), "disque %d pr\xC3\xAAt (%u morceaux)", gl[k2].disc + 1, (unsigned)na_n2); }
    }
    int gk = nat_cheats_prep(in.id);                       /* v121 : cheats appliques par Pulse */
    int res = !nat_map_game(gl[k].path, gl[k].ciso, merr, sizeof(merr)) && sizeof(natres_bin) <= NATRES_SIZE;   /* (v121 : .ciso aussi) */
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
        /* v97 : table des morceaux tout en haut, a la taille utile (au moins la place du .dol de GC Station,
           recopie la au retour A+Z+START) : le reste sert aux aiguillages */
        u32 ext_n = na_next > NATRES_RETMAX ? na_next : NATRES_RETMAX;
        u32 ext_at = ((nat_trap ? NATRES_AT + NATRES_PTEG : NATRES_AT + NATRES_SIZE) - ext_n * 12) & ~31u;   /* (v139 : place de la table des pages) */
        na_wbe32(r + 0x24, ext_at);
        u32 eo = ext_at - NATRES_AT, low = ext_at;
        if (na_n2) {                                                        /* v121 : table de l'autre disque, juste en dessous */
            u32 e2n = na_n2 > NATRES_RETMAX ? na_n2 : NATRES_RETMAX;
            u32 e2 = (ext_at - e2n * 12) & ~31u;
            if (e2 > be32(r + 0x3C) + 0x3000u) {                         /* (v139 : 12 Ko gardes pour les aiguillages, 16 avant) */
                low = e2;
                na_wbe32(r + offsetof(natres_hdr_t, ext2), e2); na_wbe32(r + offsetof(natres_hdr_t, n_ext2), na_n2);
                for (u32 i = 0; i < na_n2; i++) {
                    na_wbe32(r + (e2 - NATRES_AT) + i * 12, na_ext2[i].iso); na_wbe32(r + (e2 - NATRES_AT) + i * 12 + 4, na_ext2[i].sd); na_wbe32(r + (e2 - NATRES_AT) + i * 12 + 8, na_ext2[i].cnt);
                }
            } else { na_n2 = 0; snprintf(na_disc2_msg, sizeof(na_disc2_msg), "pas assez de place dans le r\xC3\xA9sident (jeu trop fragment\xC3\xA9)"); }
        }
        if (be32(r) != NATRES_MAGIC || be32(r + 0x3C) > low) res = 0;
        else {
            na_wbe32(r + 0x14, sd_slot == 0 ? 2 : sd_slot == 1 ? 0 : 1);   /* canal EXI */
            na_wbe32(r + 0x18, 0); na_wbe32(r + 0x1C, 4);                   /* peripherique, 16 MHz */
            na_wbe32(r + 0x20, na_next);
            u32 co = be32(r + 0xFC) - NATRES_AT;                            /* (natres_hdr_t.card_tab) */
            if ((cards & 1) && co + na_card_n * 12 > NATRES_SIZE) cards &= ~1;
            u32 co2 = be32(r + offsetof(natres_hdr_t, card2_tab)) - NATRES_AT;   /* v122 : slot B */
            if ((cards & 2) && co2 + na_card2_n * 12 > NATRES_SIZE) cards &= ~2;
            card_on = cards != 0;
            na_wbe32(r + 0x48, NRF_REAL_IRQ | (nat_igr ? NRF_IGR : 0) | (cards & 1 ? NRF_CARD : 0) | (cards & 2 ? NRF_CARDB : 0) | (na_jcrash ? NRF_CRASH : 0) | (na_jstack ? NRF_STACK : 0));
            if (cards & 2) {
                na_wbe32(r + offsetof(natres_hdr_t, card2_n), na_card2_n); na_wbe32(r + offsetof(natres_hdr_t, card2_size), na_card2_size);
                na_wbe32(r + offsetof(natres_hdr_t, card2_id), na_card2_size >> 17);
                for (u32 i = 0; i < na_card2_n; i++) {
                    na_wbe32(r + co2 + i * 12, na_card2[i].iso); na_wbe32(r + co2 + i * 12 + 4, na_card2[i].sd); na_wbe32(r + co2 + i * 12 + 8, na_card2[i].cnt);
                }
            }
            na_wbe32(r + offsetof(natres_hdr_t, opt), na_opt());          /* v122 : manettes, vitesse du lecteur */
            if (cards & 1) {                                                  /* v65 : carte memoire virtuelle */
                na_wbe32(r + 0xF8, na_card_n); na_wbe32(r + 0x100, na_card_size); na_wbe32(r + 0x104, na_card_size >> 17);   /* (ID = taille en Mbits) */
                for (u32 i = 0; i < na_card_n; i++) {
                    na_wbe32(r + co + i * 12, na_card[i].iso); na_wbe32(r + co + i * 12 + 4, na_card[i].sd); na_wbe32(r + co + i * 12 + 8, na_card[i].cnt);
                }
            }
            na_wbe32(r + 0x60, log_sec);                                    /* (natres_hdr_t.log_sec) */
            if (snap_sec && log_sec && na_jcrash) {                         /* v83 : (natres_hdr_t.snap_sec, snap_cnt, snap_hi) */
                na_wbe32(r + 0x150, snap_sec); na_wbe32(r + 0x154, NATRES_SNAP_SECS); na_wbe32(r + 0x158, in.hi);
                na_wbe32(r + 0x48, be32(r + 0x48) | NRF_SNAP);
            }
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
    if (ok) ok = na_read_ticked(&d, in.dol_off, buf + o_dol, dol_len, 22, 33);
    if (ok) ok = gd_read(&d, 0x440, buf + o_bi2, NA_BI2);
    if (ok) ok = na_read_ticked(&d, in.fst_off, buf + o_fst, in.fst_sz, 33, 38);
    u8 hdr[0x20];
    if (ok) ok = gd_read(&d, 0, hdr, sizeof(hdr));
    gd_close(&d);
    if (!ok) { free(buf); return "Lecture du jeu impossible (carte SD)"; }
    /* etape 3b : le jeu parle au faux lecteur (programme resident) au lieu du vrai */
    int patched = 0, skipped = 0;
    if (res) {
        int was = card_on, si_on = (na_opt() & 0x000FFF07u) != 0;
        patched = na_patch(&in, buf + o_dol, buf + o_res, &skipped, &card_on, &si_on);
        {   /* v95 : code de redemarrage de l'apploader (OSExec, ancien apploader : <= 2004/02/01) */
            u8 *r = buf + o_res;
            int old = in.al_trailer && strncmp(in.al_date, "2004/02/01", 10) <= 0;
            na_wbe32(r + offsetof(natres_hdr_t, reboot_off), 0x2440 + 0x20 + in.al_size);
            na_wbe32(r + offsetof(natres_hdr_t, reboot_len), old ? (in.al_trailer + 31) & ~31u : 0);
        }
        {   /* v121 : 16:9 force et cheats, a la suite des aiguillages (la suite reste libre pour OSExec) */
            u8 *r = buf + o_res;
            u32 at = be32(r + offsetof(natres_hdr_t, tramp_at)), ceil = na_ceil(r);
            na_ws_msg[0] = 0;
            if (nat_ws) at = na_ws_patch(&in, buf + o_dol, r, at, ceil);
            int ngk = gk > 0 ? na_gk_n : 0;
            if (ngk > 0 || na_fx_n > 0) {                 /* v136 : {cheats} puis {"FIXT", n, corrections dans les fichiers lus} */
                u32 need = 4 + (u32)ngk * 4 + (na_fx_n ? 8 + (u32)na_fx_n * 12 : 0);
                if (at + need + 64 * 4 <= ceil) {
                    u8 *q = r + (at - NATRES_AT);
                    na_wbe32(q, (u32)ngk);
                    for (int i = 0; i < ngk; i++) na_wbe32(q + 4 + i * 4, na_gk[i]);
                    q += 4 + ngk * 4;
                    if (na_fx_n) {
                        na_wbe32(q, 0x46495854u); na_wbe32(q + 4, (u32)na_fx_n);
                        for (int i = 0; i < na_fx_n; i++) for (int j = 0; j < 3; j++) na_wbe32(q + 8 + i * 12 + j * 4, na_fx[i][j]);
                    }
                    na_wbe32(r + offsetof(natres_hdr_t, chts), at);
                    at = (at + need + 31) & ~31u;
                } else {
                    if (ngk) snprintf(na_gk_msg, sizeof(na_gk_msg), "pas assez de place dans le r\xC3\xA9sident");
                    if (na_fx_n) { snprintf(na_fx_msg, sizeof(na_fx_msg), "pas assez de place dans le r\xC3\xA9sident"); na_fx_n = 0; }
                }
            }
            na_wbe32(r + offsetof(natres_hdr_t, tramp_at), at);
        }
        if (was && !card_on) {                              /* (carte refusee pour ce jeu : flag et table retires) */
            u8 *r = buf + o_res;
            na_wbe32(r + 0x48, be32(r + 0x48) & ~(NRF_CARD | NRF_CARDB));
            na_wbe32(r + 0xF8, 0); na_wbe32(r + offsetof(natres_hdr_t, card2_n), 0);
            snprintf(na_card_msg, sizeof(na_card_msg), "%s", na_xwhy);
            if (na_card2_n) snprintf(na_card2_msg, sizeof(na_card2_msg), "%s", na_xwhy);
        }
#ifdef GEKKO
        if (nat_vmode == 1 && !VIDEO_HaveComponentCable()) { nat_vmode = 0; snprintf(na_rm_msg, sizeof(na_rm_msg), "480p : pas de c\xC3\xA2" "ble composante / num\xC3\xA9rique"); }
#endif
        na_rm_patch(&in, buf + o_dol);                      /* v122 : mode video, flou, largeur, position de l'image */
        na_game_fix(&in, buf + o_dol, card_on);             /* v135 : corrections par jeu (table) */
        na_fx_scan(gl[k].path, &in, buf + o_fst, card_on);   /* v136 : ... et dans les autres executables du disque */
    }
    if (res && (skipped || in.di_lost)) {                /* un acces oublie = vrai lecteur interroge : on ne lance pas */
        free(buf);
        mkdir(NA_DIR, 0777);
        FILE *lf = fopen(NA_LOG, "w");                    /* (v52 : le journal dit lesquels et pourquoi) */
        if (lf) {
            fprintf(lf, "GC Station - Pulse : jeu PAS lance\nJeu : %s (%s)\n", in.title, in.id);
            fprintf(lf, "Acces au lecteur : %d trouves, %d branches, %d non branches\n", in.ntotal - in.nexi, patched - na_xdone, skipped + in.di_lost);
            if (in.di_lost) fprintf(lf, "  %d au-dela de la limite de %d\n", in.di_lost, NA_MAXHIT);
            for (int i = 0; i < na_nskip; i++) fprintf(lf, "  %08X  instruction %08X : %s\n", (unsigned)na_skip_at[i], (unsigned)na_skip_ins[i], na_skip_why[i]);
            fclose(lf);
        }
        static char e[120];
        snprintf(e, sizeof(e), "Pulse : %d acc\xC3\xA8s au lecteur non branch\xC3\xA9s, jeu pas lanc\xC3\xA9", skipped + in.di_lost);
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
    if (res && !na_drv_ok) { u8 *r = buf + o_res; na_wbe32(r + 0x48, be32(r + 0x48) & ~NRF_REAL_IRQ); }   /* v133 : pas de lecteur : Pulse reveille le jeu sans lui */
    nat_ld("D\xC3\xA9marrage", 100);
    /* journal, ecrit AVANT le saut (apres, plus rien ne peut ecrire sur la carte) */
    mkdir(NA_DIR, 0777);
    FILE *f = fopen(NA_LOG, "w");
    if (f) {
        fprintf(f, "GC Station - Pulse\n");
        fprintf(f, "Jeu : %s  %s\nFichier : %s\nAdaptateur SD : %s\n\n", in.id, in.title, gl[k].path, na_adapter());
        fprintf(f, "Donnees rangees en %08X (%u octets)\nbi2.bin -> %08X   FST -> %08X (%u / %u octets)\n",
                (unsigned)stage, (unsigned)total, (unsigned)bi2_dest, (unsigned)fst_dest, (unsigned)in.fst_sz, (unsigned)in.fst_max);
        for (u32 i = 0; i < n; i++) fprintf(f, "  copie %08X <- %08X  %u octets\n", (unsigned)tab[i * 3], (unsigned)tab[i * 3 + 1], (unsigned)tab[i * 3 + 2]);
        fprintf(f, "Point d'entree : %08X\n", (unsigned)in.entry);
        fprintf(f, "Programme resident : %s\n", res ? "installe" : merr[0] ? merr : "non installe");
        fprintf(f, "Journal du jeu : %s\n", log_sec ? "oui (gc-mp3/natif/journal-jeu.txt au prochain demarrage de GC Station)" : "non");
        fprintf(f, "Faux lecteur DVD : %d acces branches, %d non branches (sur %d trouves)\n", patched - na_xdone, skipped, in.ntotal - in.nexi);
        if (na_dd_found) fprintf(f, "  dont %d trouves seulement par le suivi large (branches en secours : %d, pas branches : %d)\n", na_dd_found, na_dd_done, na_dd_skip);   /* v133 */
        fprintf(f, "Vrai lecteur : %s\n", na_drv[0] ? na_drv : "pas teste");
        fprintf(f, "Reveil du jeu (fins de commande) : %s\n", na_drv_ok ? "par le vrai lecteur (secours sans lui si un reveil se perd)" : "sans le lecteur (depuis les interruptions du jeu)");   /* v133 */
        fprintf(f, "Retour a GC Station (A+Z+START, plantage) : %s\n", na_ret_n ? "pret (le .dol de GC Station est relu sur la carte)" : "redemarrage de la console");
        fprintf(f, "Carte memoire virtuelle (slot A) : %s ; %d acces EXI trouves, %d branches\n", na_card_msg[0] ? na_card_msg : "non", in.nexi, na_xdone);
        fprintf(f, "Format : %s\n", gl[k].ciso ? ".ciso (blocs absents lus comme des zeros)" : ".iso / .gcm");
        fprintf(f, "Autre disque : %s\n", na_disc2_msg[0] ? na_disc2_msg : "aucun");
        fprintf(f, "16:9 force : %s\n", !nat_ws ? "non (reglage du jeu)" : na_ws_msg[0] ? na_ws_msg : "non");
        fprintf(f, "Corrections du jeu : %s\n", na_fix_msg[0] ? na_fix_msg : "aucune pour ce jeu");
        if (na_fx_msg[0]) fprintf(f, "Corrections dans les autres fichiers du disque : %d endroit(s) ; %s\n", na_fx_n, na_fx_msg);   /* v136 */
        fprintf(f, "Cheats : %s\n", na_gk_msg[0] ? na_gk_msg : "aucun");
        static const char *const vmn[4] = { "comme le jeu", "480p force", "PAL 60 Hz", "240p" };
        fprintf(f, "Video : %s%s%s ; %s\n", vmn[nat_vmode & 3], nat_deflick ? ", sans flou" : "", nat_wide ? ", image large" : "", na_rm_msg[0] ? na_rm_msg : "rien a changer");
        fprintf(f, "Manettes : options %08X ; %d acces branches, %d laisses au vrai materiel (sur %d trouves%s)\n", (unsigned)(na_opt() & 0x00FFFFFFu), na_si_done, na_si_skip, in.nsi, in.si_lost ? ", limite atteinte" : "");
        fprintf(f, "Vitesse du lecteur : %s%s\n", nat_speed ? "comme un vrai lecteur" : "rapide (carte SD)", nat_speed_auto ? " (automatique : jeu connu pour bloquer si le disque repond trop vite)" : "");
        if (nat_trap) fprintf(f, "Acces au lecteur par exception (Pulse universel, experimental) : demande\n");
        if (rep_msg[0]) fprintf(f, "Reparation automatique : %s\n", rep_msg);   /* v140 */
        fprintf(f, "Carte memoire virtuelle (slot B) : %s\n", nat_card >= 2 ? (na_card2_msg[0] ? na_card2_msg : "non") : "non (reglage)");
        fprintf(f, "Mode video donne au jeu (800000CC) : %s (jeu %s)\n", na_vi_bootrom(in.id) == 1 ? "PAL" : na_vi_bootrom(in.id) == 5 ? "EURGB60 (PAL 60 Hz)" : "NTSC",
                na_game_pal(in.id) ? "PAL" : "NTSC");
        fprintf(f, "Memoire basse avant : 28=%08X 2C=%08X CC=%08X F8=%08X FC=%08X\n",
#ifdef GEKKO
                (unsigned)*(volatile u32 *)0x80000028, (unsigned)*(volatile u32 *)0x8000002C, (unsigned)*(volatile u32 *)0x800000CC,
                (unsigned)*(volatile u32 *)0x800000F8, (unsigned)*(volatile u32 *)0x800000FC
#else
                0u, 0u, 0u, 0u, 0u
#endif
                );
        fprintf(f, "\nSaut vers le jeu... Ce que tu vois a l'ecran ensuite, c'est le resultat :\n"
                   "- le jeu demarre et joue = Pulse OK !\n"
                   "- message d'erreur de disque du jeu = le faux lecteur n'a pas suffi ;\n"
                   "- ecran noir fige / retour au menu = a me signaler.\n");
        fclose(f);
    }
    crumb("natif : saut vers %s (entree %08X)", in.id, (unsigned)in.entry);
#ifdef GEKKO
    if (settings_dirty) save_settings();
    stop_track(); stop_station();
    AUDIO_StopDMA();
    bg_quiesce();                                         /* v96 : la lecture en fond finit avant le demontage */
    fatUnmount("sd");
    /* v89 : l'ecran dans le mode du jeu avant le saut (comme le fait Swiss) : PAL 576i, PAL 60 (EURGB60) ou
       NTSC 480i. Avant : le mode de GC Station (souvent 480p) restait, et certains jeux le reprenaient tel quel
       (Paper Mario : rouge et bleu inverses sur la prise numerique / adaptateur HDMI) */
    u32 na_vb = na_vi_bootrom(in.id);
    if (nat_vmode && na_game_pal(in.id)) na_vb = 5;          /* v122 : jeu PAL mis en 60 Hz (480 lignes) : EURGB60 */
    GXRModeObj *na_gm = na_vb == 1 ? &TVPal576IntDfScale : na_vb == 5 ? &TVEurgb60Hz480IntDf : &TVNtsc480IntDf;
    VIDEO_SetBlack(TRUE); VIDEO_Flush(); VIDEO_WaitVSync();
    VIDEO_Configure(na_gm); VIDEO_SetBlack(TRUE); VIDEO_Flush(); VIDEO_WaitVSync(); VIDEO_WaitVSync();
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    u32 level = IRQ_Disable();                            /* plus d'interruptions : on ecrase GC Station */
    na_vi_irqs(na_vb);                                    /* v111 : 480p (interruptions d'ecran comme le BIOS) */
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
    lm[0xCC / 4] = na_vb;                                 /* v84 : mode video "du BIOS" (sinon les jeux PAL s'arretent : VIConfigure) */
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

/* ======================= v97 : lancer le disque du vrai lecteur de la console =======================
   Case "Disque de la console" de l'onglet Jeux. Comme le BIOS : reset du lecteur, lecture de l'ID du
   disque (le lecteur se deverrouille, la puce s'il y en a une fait son travail), reglage de la musique
   en streaming, puis main.dol, bi2.bin et la FST sont lus sur le disque et le jeu demarre directement
   (sans Resident : c'est le vrai lecteur qui sert le jeu ; pas de A+Z+START). */
#ifdef GEKKO
/* une commande au lecteur (masques coupes : pas d'interruption) ; 0 = fin OK, 1 = erreur, -1 = rien */
static int na_di(volatile u32 *di, u32 c0, u32 c1, u32 c2, void *dst, u32 len, u32 ms) {
    di[0] = 0x54u;
    if (dst) DCInvalidateRange(dst, len);
    di[2] = c0; di[3] = c1; di[4] = c2;
    if (dst) { di[5] = (u32)MEM_VIRTUAL_TO_PHYSICAL(dst); di[6] = len; di[7] = 3; }
    else di[7] = 1;
    u64 t0 = now_ms();
    while (di[7] & 1) {
        nat_ld_tick();
        if (now_ms() - t0 > ms) { di[0] = 1u; u64 t1 = now_ms(); while ((di[7] & 1) && now_ms() - t1 < 200) { } di[0] = 0x54u; return -1; }
    }
    u32 sr = di[0];
    di[0] = 0x54u;
    if (dst) DCInvalidateRange(dst, len);
    return sr & 0x04u ? 1 : 0;
}
static int na_dvd_read(volatile u32 *di, u32 off, u8 *dst, u32 len, int pf, int pt) {   /* (len : multiple de 32) */
    for (u32 o = 0; o < len; ) {
        u32 n = len - o > 0x40000u ? 0x40000u : len - o;
        if (na_di(di, 0xA8000000u, (off + o) >> 2, n, dst + o, n, 10000)) return 0;
        o += n;
        nat_ld_pct = nat_ld_from = nat_ld_to = pf + (int)((u64)(pt - pf) * o / len);
        nat_ld_tick();
    }
    return 1;
}
#endif
static const char *nat_boot_drive(int k, int reset);
static const char *nat_boot_disc(int k) { return nat_boot_drive(k, 1); }
/* v121 : reset = 0 : le lecteur repond deja (GC Loader : le jeu vient d'y etre choisi, un reset l'oublierait) */
static const char *nat_boot_drive(int k, int reset) {
    if (reset) snprintf(nat_ld_title, sizeof(nat_ld_title), "%s", TR("Disque de la console"));
    nat_ld_k = k;
    nat_ld("Pr\xC3\xA9paration du lecteur", 4);
#ifdef GEKKO
    volatile u32 *di = (volatile u32 *)0xCC006000u, *pi = (volatile u32 *)0xCC003000u;
    static u8 hdr[0x440] ATTRIBUTE_ALIGN(32), dh[0x100] ATTRIBUTE_ALIGN(32);
    stop_track(); stop_station();
    if (reset) {   /* reset du lecteur (GC Station le tient en reset), puis on attend qu'il reponde */
        u32 level = IRQ_Disable();
        pi[9] = (pi[9] & ~4u) | 1u;
        u64 t = now_ms(); while (now_ms() - t < 2) ;
        pi[9] = pi[9] | 5u;
        IRQ_Restore(level);
        static u8 inq[32] ATTRIBUTE_ALIGN(32);
        u64 t0 = now_ms(); int r;
        nat_ld_span("Pr\xC3\xA9paration du lecteur", 4, 30);
        do r = na_di(di, 0x12000000u, 0, 32, inq, 32, 1500); while (r < 0 && now_ms() - t0 < 8000);
        if (r < 0) return "Le lecteur de la console ne r\xC3\xA9pond pas";
    }
    di[1] = 4u;                                            /* (couvercle : acquitte) */
    nat_ld_span("Lecture du disque", 30, 50);
    int r = -1;
    u64 t0 = now_ms();
    do r = na_di(di, 0xA8000040u, 0, 0x20, hdr, 0x20, 15000); while (r > 0 && now_ms() - t0 < 4000 && (nat_ld_tick(), 1));
    if (r) {
        u32 err = 0;
        if (!na_di(di, 0xE0000000u, 0, 0, NULL, 0, 2000)) err = di[8];
        if ((err & 0x00FFFFFFu) == 0x023A00u) return "Pas de disque dans le lecteur";
        if ((err >> 24) == 1) return "Le couvercle du lecteur est ouvert";
        static char e[96];
        snprintf(e, sizeof(e), "Disque illisible (erreur du lecteur %08X)", (unsigned)err);
        return e;
    }
    if (!na_dvd_read(di, 0, hdr, sizeof(hdr), 50, 52) || be32(hdr + 0x1C) != 0xC2339F3Du) return "Ce n'est pas un disque GameCube";
    char id[7]; memcpy(id, hdr, 6); id[6] = 0;
    char title[72]; gl_latin(hdr + 0x20, 0x40, title, sizeof(title));
    snprintf(nat_ld_title, sizeof(nat_ld_title), "%s", title[0] ? title : id);
    u32 dol_off = be32(hdr + 0x420), fst_off = be32(hdr + 0x424), fst_sz = be32(hdr + 0x428), fst_max = be32(hdr + 0x42C);
    if (!dol_off || (dol_off & 3) || (fst_off & 3) || !fst_sz || fst_max < fst_sz || fst_max > (4u << 20)) return "Disque : en-t\xC3\xAAte inattendu";
    if (!na_dvd_read(di, dol_off, dh, sizeof(dh), 52, 54)) return "Disque : main.dol illisible";
    u32 entry = be32(dh + 0xE0), lo = 0xFFFFFFFFu, hi = 0, dol_len = 0x100;
    for (int i = 0; i < 18; i++) {
        u32 off = be32(dh + i * 4), addr = be32(dh + 0x48 + i * 4), sz = be32(dh + 0x90 + i * 4);
        if (!sz) continue;
        if (addr < 0x80003000u || addr + (u64)sz > 0x81800000u) return "Disque : main.dol inattendu";
        if (addr < lo) lo = addr;
        if (addr + sz > hi) hi = addr + sz;
        if (off + sz > dol_len) dol_len = off + sz;
    }
    if (be32(dh + 0xD8) + be32(dh + 0xDC) > hi) hi = be32(dh + 0xD8) + be32(dh + 0xDC);
    if (entry < lo || entry >= hi) return "Disque : point d'entr\xC3\xA9" "e invalide";
    /* memoire du haut : [ jeu ... | FST | bi2.bin ] 0x81800000 */
    u32 bi2_dest = 0x81800000u - NA_BI2;
    u32 fst_dest = (bi2_dest - fst_max) & ~31u;
    if (hi > fst_dest) return "Pas assez de m\xC3\xA9moire pour ce jeu";
    u32 o_bi2 = (dol_len + 31) & ~31u, o_fst = o_bi2 + NA_BI2, total = o_fst + ((fst_sz + 31) & ~31u);
    u32 top = fst_dest < 0x81700000u - 0x400u ? fst_dest : 0x81700000u - 0x400u;
    if (total + hi + 32 > top) return "Pas assez de m\xC3\xA9moire pour ce jeu (main.dol trop gros)";
    u32 stage = (top - total) & ~31u;
    u8 *buf = (u8 *)memalign(32, total);
    if (!buf) return "Pas assez de m\xC3\xA9moire libre dans GC Station";
    memset(buf, 0, total);
    int ok = na_dvd_read(di, dol_off, buf, o_bi2, 54, 88)
          && na_dvd_read(di, 0x440, buf + o_bi2, NA_BI2, 88, 90)
          && na_dvd_read(di, fst_off, buf + o_fst, (fst_sz + 31) & ~31u, 90, 96);
    if (!ok) { free(buf); return "Lecture du disque impossible"; }
    /* musique en streaming (comme le BIOS) : octet 8 = oui / non, octet 9 = taille du tampon */
    na_di(di, 0xE4000000u | (hdr[8] ? 0x00010000u | hdr[9] : 0), 0, 0, NULL, 0, 2000);
    u32 tab[18 * 3 + 6], n = 0;
    for (int i = 0; i < 18; i++) {
        u32 off = be32(buf + i * 4), addr = be32(buf + 0x48 + i * 4), sz = be32(buf + 0x90 + i * 4);
        if (!sz) continue;
        tab[n * 3] = addr; tab[n * 3 + 1] = stage + off; tab[n * 3 + 2] = sz; n++;
    }
    tab[n * 3] = bi2_dest; tab[n * 3 + 1] = stage + o_bi2; tab[n * 3 + 2] = NA_BI2; n++;
    tab[n * 3] = fst_dest; tab[n * 3 + 1] = stage + o_fst; tab[n * 3 + 2] = fst_sz; n++;
    nat_ld("D\xC3\xA9marrage", 100);
    mkdir(NA_DIR, 0777);
    FILE *f = fopen(NA_DIR "/disque-console.txt", "w");
    if (f) {
        fprintf(f, "GC Station - disque du vrai lecteur\nJeu : %s  %s\nmain.dol %08X (%u octets), entree %08X\nFST %08X -> %08X (%u / %u)\nStreaming : %s (tampon %u)\n",
                id, title, (unsigned)dol_off, (unsigned)dol_len, (unsigned)entry, (unsigned)fst_off, (unsigned)fst_dest, (unsigned)fst_sz, (unsigned)fst_max,
                hdr[8] ? "oui" : "non", (unsigned)hdr[9]);
        fclose(f);
    }
    crumb("disque : saut vers %s (entree %08X)", id, (unsigned)entry);
    if (settings_dirty) save_settings();
    AUDIO_StopDMA();
    bg_quiesce();
    fatUnmount("sd");
    u32 vb = na_vi_bootrom(id);
    GXRModeObj *gm = vb == 1 ? &TVPal576IntDfScale : vb == 5 ? &TVEurgb60Hz480IntDf : &TVNtsc480IntDf;
    VIDEO_SetBlack(TRUE); VIDEO_Flush(); VIDEO_WaitVSync();
    VIDEO_Configure(gm); VIDEO_SetBlack(TRUE); VIDEO_Flush(); VIDEO_WaitVSync(); VIDEO_WaitVSync();
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    u32 level = IRQ_Disable();
    (void)level;
    na_vi_irqs(vb);                                       /* v111 */
    memmove((void *)stage, buf, total);
    DCFlushRange((void *)stage, total);
    volatile u32 *lm = (volatile u32 *)0x80000000;
    memcpy((void *)0x80000000, hdr, 0x20);
    lm[0x20 / 4] = 0x0D15EA5Eu;
    lm[0x24 / 4] = 1;
    if (!lm[0x28 / 4]) lm[0x28 / 4] = 0x01800000u;
    lm[0x30 / 4] = 0;
    lm[0x34 / 4] = fst_dest;
    lm[0x38 / 4] = fst_dest;
    lm[0x3C / 4] = fst_max;
    lm[0xF0 / 4] = 0x01800000u;
    lm[0xCC / 4] = vb;
    lm[0xF4 / 4] = bi2_dest;
    if (!lm[0xF8 / 4]) lm[0xF8 / 4] = 0x09A7EC80u;
    if (!lm[0xFC / 4]) lm[0xFC / 4] = 0x1CF7C580u;
    DCFlushRange((void *)0x80000000, 0x100);
    u32 *t = (u32 *)GL_TAB_AT;
    for (u32 i = 0; i < n * 3; i++) t[i] = tab[i];
    memcpy((void *)GL_STUB_AT, gl_stub, sizeof(gl_stub));
    DCFlushRange((void *)0x80001800, 0x1800);
    ICInvalidateRange((void *)GL_STUB_AT, sizeof(gl_stub));
    ((void (*)(u32 *, u32, u32))GL_STUB_AT)(t, n, entry);
    for (;;) ;
#else
    return "(simulateur : pas de vrai lecteur)";
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

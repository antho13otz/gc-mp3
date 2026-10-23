// GC Station v49 : programme resident du lanceur natif (code original, sans Swiss)
// Compile a part (Makefile : build/natres.bin), lie a NATRES_AT (tools/natres.ld),
// sans bibliotheque : il doit marcher pendant le jeu, sans GC Station autour.
//
// Lecture de la carte SD en mode SPI par le bus EXI (registres 0xCC006800) :
//  - CSR (+0x00) : bit 7 + n = peripherique n selectionne, bits 4-6 = vitesse ;
//  - CR  (+0x0C) : bit 0 = demarrer, bits 2-3 = sens (0 lire, 1 ecrire), bits 4-5 = octets - 1 ;
//  - DATA(+0x10) : jusqu'a 4 octets, le premier dans l'octet de poids fort.
// Protocole SD (specification publique "SD Physical Layer, mode SPI") : CMD0, CMD8,
// ACMD41, CMD58, CMD16, puis CMD17 (lire un secteur, jeton 0xFE, 512 octets, CRC).
#include "natres.h"

typedef unsigned int u32;
typedef unsigned char u8;

static int nr_init(void);
static int nr_read(void *dst, u32 off, u32 len);
static int nr_read_sd(u32 sector, void *dst);
static u32 di_load(u32 reg);
static void di_store(u32 reg, u32 val);

static natres_ext_t ext_tab[NATRES_MAXEXT];
static natres_ext_t ret_tab[NATRES_RETMAX];              /* v64 : le .dol de GC Station sur la carte */
static u8 secbuf[512] __attribute__((aligned(32)));
static u32 sec_cached = 0xFFFFFFFFu;
static u32 ext_last = 0;
extern u8 __natres_end[];

__attribute__((section(".res_head"), used))
natres_hdr_t natres_hdr = {
    NATRES_MAGIC, NATRES_VER, (u32)nr_init, (u32)nr_read, (u32)nr_read_sd,
    2, 0, 4,                      /* SD2SP2, peripherique 0, 16 MHz */
    0, (u32)ext_tab,
    0, 0, 0, 0, 0,
    (u32)__natres_end,
    (u32)di_load, (u32)di_store, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0,
    0, (u32)ret_tab, 0, 0,
};
#define H natres_hdr

/* (le compilateur peut appeler ces fonctions de lui-meme) */
void *memset(void *d, int c, unsigned long n) { u8 *p = (u8 *)d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { u8 *p = (u8 *)d; const u8 *q = (const u8 *)s; while (n--) *p++ = *q++; return d; }

/* ---------- bus EXI ---------- */
static volatile u32 *exi;
static u32 freq;

static int ex_wait(void) {
    for (u32 i = 0; i < 1000000u; i++) if (!(exi[3] & 1)) return 0;
    H.err = NR_ETIMEOUT;
    return -1;
}
static void ex_sel(int cs) {
    u32 v = (exi[0] & 0x405) | (freq << 4);
    if (cs) v |= 0x80u << H.dev;
    exi[0] = v;
}
static void ex_desel(void) { exi[0] = exi[0] & 0x405; }
static void ex_put(u32 v, int n) { exi[4] = v; exi[3] = ((u32)(n - 1) << 4) | (1u << 2) | 1u; ex_wait(); }
static u32 ex_get(int n) { exi[4] = 0xFFFFFFFFu; exi[3] = ((u32)(n - 1) << 4) | 1u; ex_wait(); return exi[4]; }
static u32 ex_get8(void) { return ex_get(1) >> 24; }

/* ---------- carte SD (SPI) ---------- */
static void sd_end(void) {                     /* fin de commande : CS relache + 8 coups d'horloge */
    ex_desel();
    ex_sel(0); ex_put(0xFF000000u, 1); ex_desel();
}
static u32 sd_cmd(u32 cmd, u32 arg, u32 crc) {  /* envoie la commande, rend la reponse R1 (CS reste actif) */
    ex_sel(1);
    ex_put(0xFF000000u, 1);
    ex_put(((0x40u | cmd) << 24) | (arg >> 8), 4);
    ex_put(((arg & 0xFFu) << 24) | (crc << 16), 2);
    u32 r = 0xFF;
    for (int i = 0; i < 16; i++) { r = ex_get8(); if (!(r & 0x80)) break; }
    H.r1 = r;
    return r;
}

static int nr_init(void) {
    exi = (volatile u32 *)(0xCC006800u + H.chan * 0x14u);
    H.err = NR_OK; H.sdhc = 0; H.stage = 1;
    sec_cached = 0xFFFFFFFFu;
    u32 want = H.freq ? H.freq : 4;
    freq = 0;                                   /* 1 MHz pour demarrer la carte */
    /* peripherique : celui indique, puis l'autre position du port (0 / 2) sur le canal 0 */
    int found = 0;
    for (int t = 0; t < 2 && !found; t++) {
        if (t == 1) { if (H.chan != 0) break; H.dev = H.dev == 0 ? 2 : 0; }
        ex_sel(0);
        for (int i = 0; i < 10; i++) ex_put(0xFF000000u, 1);   /* 80 coups d'horloge, CS relache */
        ex_desel();
        for (int k = 0; k < 8 && !found; k++) {
            u32 r = sd_cmd(0, 0, 0x95);          /* CMD0 : retour a l'etat de depart, en SPI */
            sd_end();
            if (r == 0x01) found = 1;
        }
    }
    if (!found) { H.err = NR_ENOCARD; return -1; }
    H.stage = 2;
    int v2 = 0;
    u32 r = sd_cmd(8, 0x1AA, 0x87);              /* CMD8 : carte 2.0 ? */
    if (r == 0x01) { u32 r7 = ex_get(4); v2 = (r7 & 0xFFF) == 0x1AA; }
    sd_end();
    H.stage = 3;
    int ok = 0;
    for (u32 i = 0; i < 20000u && !ok; i++) {    /* ACMD41 : jusqu'a ce que la carte soit prete */
        sd_cmd(55, 0, 0x01); sd_end();
        r = sd_cmd(41, v2 ? 0x40000000u : 0, 0x01); sd_end();
        if (r == 0x00) ok = 1;
        else if (r & 0x04) {                      /* (vieille carte MMC : CMD1) */
            r = sd_cmd(1, 0, 0x01); sd_end();
            if (r == 0x00) ok = 1;
        }
    }
    if (!ok) { H.err = NR_EINIT; return -1; }
    H.stage = 4;
    if (v2) {                                      /* CMD58 : SDHC / SDXC (adresse en secteurs) ? */
        r = sd_cmd(58, 0, 0x01);
        u32 ocr = ex_get(4);
        sd_end();
        if (r == 0x00 && (ocr & 0x40000000u)) H.sdhc = 1;
    }
    if (!H.sdhc) { sd_cmd(16, 512, 0x01); sd_end(); }   /* CMD16 : secteurs de 512 octets */
    freq = want;
    H.stage = 5;
    return 0;
}

static int nr_read_sd(u32 sector, void *dst) {
    u8 *o = (u8 *)dst;
    u32 r = sd_cmd(17, H.sdhc ? sector : sector << 9, 0x01);   /* CMD17 : lire un secteur */
    if (r != 0) { sd_end(); H.err = NR_ECMD; return -1; }
    u32 t = 0xFF;
    for (u32 i = 0; i < 400000u; i++) { t = ex_get8(); if (t != 0xFF) break; }
    if (t != 0xFE) { sd_end(); H.err = NR_ETOKEN; H.r1 = t; return -1; }
    for (int i = 0; i < 128; i++) {
        u32 w = ex_get(4);
        o[0] = (u8)(w >> 24); o[1] = (u8)(w >> 16); o[2] = (u8)(w >> 8); o[3] = (u8)w;
        o += 4;
    }
    ex_get(2);                                    /* CRC (pas verifie) */
    sd_end();
    H.nread++;
    return 0;
}

/* v54 : ecrire un secteur (CMD24) : seulement le journal, dans un fichier prepare par GC Station */
static int nr_write_sd(u32 sector, const u8 *s) {
    u32 r = sd_cmd(24, H.sdhc ? sector : sector << 9, 0x01);
    if (r != 0) { sd_end(); return -1; }
    ex_put(0xFF000000u, 1);
    ex_put(0xFE000000u, 1);                       /* jeton de debut */
    for (int i = 0; i < 128; i++, s += 4) ex_put((u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3], 4);
    ex_put(0xFFFF0000u, 2);                       /* CRC (ignore en SPI) */
    u32 t = 0xFF;
    for (int i = 0; i < 16; i++) { t = ex_get8(); if (t != 0xFF) break; }
    for (u32 i = 0; i < 2000000u; i++) if (ex_get8() == 0xFF) break;   /* la carte ecrit (occupee = 00) */
    sd_end();
    return (t & 0x1F) == 0x05 ? 0 : -1;
}

/* secteur de l'ISO -> secteur de la carte */
static int iso_to_sd(u32 isec, u32 *sd) {
    u32 n = H.n_ext;
    if (ext_last < n) {
        const natres_ext_t *e = &ext_tab[ext_last];
        if (isec >= e->iso && isec - e->iso < e->cnt) { *sd = e->sd + (isec - e->iso); return 0; }
    }
    u32 lo = 0, hi = n;                           /* (morceaux ranges dans l'ordre de l'ISO) */
    while (lo < hi) {
        u32 mid = (lo + hi) >> 1;
        const natres_ext_t *e = &ext_tab[mid];
        if (isec < e->iso) hi = mid;
        else if (isec - e->iso >= e->cnt) lo = mid + 1;
        else { ext_last = mid; *sd = e->sd + (isec - e->iso); return 0; }
    }
    H.err = NR_ERANGE;
    return -1;
}

static int nr_read1(void *dst, u32 off, u32 len);
static int di_ready = 0;
static int nr_read(void *dst, u32 off, u32 len) {    /* v54 : une erreur de la carte -> on la redemarre et on reessaie */
    for (int t = 0; t < 3; t++) {
        if (nr_read1(dst, off, len) == 0) { if (t) H.nretry++; return 0; }
        if (H.err == NR_ERANGE) break;
        H.nrderr++;
        sec_cached = 0xFFFFFFFFu;
        di_ready = nr_init() == 0;
    }
    return -1;
}
static int nr_read1(void *dst, u32 off, u32 len) {
    u8 *o = (u8 *)dst;
    while (len) {
        u32 isec = off >> 9, in = off & 511, n = 512 - in;
        if (n > len) n = len;
        u32 sd;
        if (iso_to_sd(isec, &sd)) return -1;
        if (in == 0 && n == 512) {                 /* secteur entier : directement a destination */
            if (nr_read_sd(sd, o)) return -1;
        } else {
            if (sec_cached != sd) {
                sec_cached = 0xFFFFFFFFu;
                if (nr_read_sd(sd, secbuf)) return -1;
                sec_cached = sd;
            }
            for (u32 i = 0; i < n; i++) o[i] = secbuf[in + i];
        }
        o += n; off += n; len -= n;
    }
    return 0;
}

/* ======================= faux lecteur DVD (etape 3b) =======================
   Les instructions du jeu qui lisaient ou ecrivaient les registres du lecteur
   (0xCC006000 + reg) appellent maintenant di_load / di_store (aiguillages poses par
   GC Station). Registres (doc publique du lecteur, "DI") :
   00 DISR  : bits 6 BRKINT, 5 masque, 4 TCINT (fin), 3 masque, 2 DEINT (erreur), 1 masque
   04 DICVR : bit 2 CVRINT, 1 masque, 0 couvercle ouvert
   08/0C/10 DICMDBUF0-2 : commande ; 14 DIMAR : adresse memoire ; 18 DILENGTH : taille
   1C DICR  : bit 0 demarrer, 1 DMA, 2 ecriture ; 20 DIIMMBUF : reponse ; 24 DICFG
   Commandes : A8 lire (A8000040 = ID du disque), AB chercher, E0 erreur, 12 infos,
   E1-E4 audio en streaming. */
#define DI_REAL ((volatile u32 *)0xCC006000u)
static u32 di[10];                                      /* faux registres */
static u8 di_scratch[64] __attribute__((aligned(32)));  /* (cible de la vraie commande "inquiry") */

static inline u32 irq_off(void) {
    u32 m;
    __asm__ volatile("mfmsr %0" : "=r"(m));
    __asm__ volatile("mtmsr %0" :: "r"(m & ~0x8000u) : "memory");
    return m;
}
static inline void irq_restore(u32 m) { __asm__ volatile("mtmsr %0" :: "r"(m) : "memory"); }
static void cache_flush(u32 a, u32 len) {                /* donnees ecrites par le processeur -> memoire */
    u32 e = a + len;
    for (a &= ~31u; a < e; a += 32) __asm__ volatile("dcbf 0,%0" :: "r"(a) : "memory");
    __asm__ volatile("sync" ::: "memory");
}

static u32 kick_tb = 0;
static inline u32 tbl(void);
static u32 di_real_kick(void) {                          /* vraie commande "inquiry" : sa fin reveille le jeu */
    volatile u32 *r = DI_REAL;
    for (u32 i = 0; i < 3000000u && (r[7] & 1); i++) ;  /* v54 : le vrai lecteur est occupe : on attend un peu */
    if (r[7] & 1) { H.nskip++; return 2; }
    H.nkick++;
    kick_tb = tbl();
    r[2] = 0x12000000u; r[3] = 0; r[4] = 0x20;
    r[5] = (u32)di_scratch & 0x01FFFFFFu; r[6] = 0x20;
    r[7] = 3;                                           /* DMA + demarrer */
    return 4 | (r[0] & 0xFF) << 16;
}

static u32 log_ring[NATRES_LOGN][4];
static u8 logbuf[512] __attribute__((aligned(32)));
static inline u32 tbl(void) { u32 t; __asm__ volatile("mftb %0" : "=r"(t)); return t; }   /* (declaree plus haut) */
#define TB_SEC 40500000u                                  /* (horloge : 162 MHz / 4) */
static void nr_log_flush(void) {
    H.flush_tb = tbl();
    if (!H.log_sec || !di_ready) return;
    const u8 *p = (const u8 *)&H;
    for (u32 i = 0; i < sizeof(natres_hdr_t); i++) logbuf[i] = p[i];
    for (u32 k = 0; k < NATRES_LOGN; k++) {                     /* le plus ancien d'abord */
        u32 j = (H.nlog + k) % NATRES_LOGN, *d = (u32 *)(logbuf + sizeof(natres_hdr_t) + k * 16);
        for (int w = 0; w < 4; w++) d[w] = log_ring[j][w];
    }
    nr_write_sd(H.log_sec, logbuf);
}
static void nr_log_add(u32 a, u32 b, u32 c, u32 d) {
    u32 *e = log_ring[H.nlog % NATRES_LOGN];
    e[0] = a; e[1] = b; e[2] = c; e[3] = d;
    H.nlog++;
}
static void nr_log(u32 off, u32 len, u32 info) {
    nr_log_add(di[2], off, len, info | (H.err & 0xFF) << 24);
    if (info & 1 || H.ncmd <= 40 || tbl() - H.flush_tb > TB_SEC * 2) nr_log_flush();   /* (debut du jeu, erreurs, puis toutes les 2 s) */
}
/* ---------- v57 : crochets dans la table des interruptions du jeu ----------
   Le systeme des jeux (SDK Nintendo) range ses fonctions d'interruption dans une table a
   0x80003040 (une par source : 20 = manettes SI, 21 = lecteur DI, 24 = image VI).
   - VI (60 fois par seconde) : le journal est ecrit toutes les 3 s, meme si le jeu est bloque ;
   - SI : APRES la fonction du jeu (elle a deja lu les manettes), A+Z+START tenus 1 s
     = retour a GC Station : redemarrage de la console (PicoLoader -> ipl.dol). */
#define OS_INT_TABLE ((volatile u32 *)0x80003040u)
#define RD_CHUNK 8192u                                    /* v61 : morceau d'une lecture en fond */
static u32 si_orig = 0, igr_hold = 0;
static void hot_reset(void);
/* ---------- v64 : retour a GC Station ----------
   Le jeu est abandonne : on arrete ce qui tourne (interruptions, son, DSP, dessin), puis le
   .dol de GC Station est relu sur la carte (meme lecteur SD que pour l'ISO) et lance.
   GC Station (libogc) remet tout le reste en route. Sans .dol prepare : redemarrage. */
static void ret_to_gcstation(void) {
    irq_off();
    if (!H.ret_n || H.ret_n > NATRES_RETMAX || H.ret_state) hot_reset();
    H.ret_state = 1;
    volatile u32 *pi = (volatile u32 *)0xCC003000u;
    volatile unsigned short *dsp = (volatile unsigned short *)0xCC005000u;
    pi[1] = 0;                                           /* (plus aucune interruption) */
    dsp[0x36 / 2] = 0;                                   /* DMA du son arrete */
    dsp[0x0A / 2] = (unsigned short)((dsp[0x0A / 2] & ~0x0150u) | 0x00ACu);   /* DSP : arret, masques coupes, acquitte */
    *(volatile u32 *)0xCC006C00u &= ~1u;                 /* son du disque (streaming) arrete */
    *(volatile unsigned short *)0xCC000002u = 0;         /* processeur graphique : plus de lecture de la file */
    if (!di_ready) di_ready = nr_init() == 0;
    for (u32 i = 0; i < H.ret_n; i++) ext_tab[i] = ret_tab[i];
    H.n_ext = H.ret_n; ext_last = 0; sec_cached = 0xFFFFFFFFu;
    static u8 dh[256] __attribute__((aligned(32)));
    if (!di_ready || nr_read(dh, 0, 256)) hot_reset();
    const u32 *w = (const u32 *)dh;                      /* (en-tete du .dol, big-endian comme la console) */
    u32 entry = w[0xE0 / 4];
    if (entry < 0x80003000u || entry >= 0x817F0000u) hot_reset();
    for (int i = 0; i < 18; i++) {
        u32 off = w[i], addr = w[0x48 / 4 + i], sz = w[0x90 / 4 + i];
        if (!sz) continue;
        if (addr < 0x80003000u || addr + sz > 0x817F0000u || off + sz > H.ret_size) hot_reset();
        if (nr_read((void *)addr, off, sz)) hot_reset();
        cache_flush(addr, sz);
        for (u32 a = addr & ~31u; a < addr + sz; a += 32) __asm__ volatile("icbi 0,%0" :: "r"(a));
    }
    __asm__ volatile("sync; isync" ::: "memory");
    ((void (*)(void))entry)();                           /* (GC Station : ne revient pas) */
    hot_reset();
}
/* (sur une pile a lui : celle du jeu peut etre ecrasee par le .dol qu'on recharge) */
void ret_go(void) __attribute__((used));
void ret_go(void) { ret_to_gcstation(); }
extern void ret_entry(void);
__asm__(".pushsection .text\n.globl ret_entry\nret_entry:\n"
        "  lis 1,exc_stack@ha\n  addi 1,1,exc_stack@l\n  addi 1,1,2032\n  b ret_go\n.popsection\n");
static void hot_reset(void) {
    volatile u32 *pi = (volatile u32 *)0xCC003000u;
    irq_off();
    pi[9] = 3;
    pi[9] = 0;                                           /* (redemarrage : comme OSResetSystem) */
    for (;;) ;
}
/* v60 : signes de vie. Chaque crochet compte, appelle la fonction du jeu, et ecrit le journal
   toutes les 3 s : on voit si le jeu tourne encore (image, son, DSP, dessin) meme sans lecteur. */
static void hooks_check(void);
static u32 alive_n = 0;
static void alive_tick(void) {
    if ((++alive_n & 63) == 0) hooks_check();            /* (le jeu a pu remettre sa fonction : on se rebranche) */
    /* (une ecriture sur la carte coupe le son quelques ms : toutes les 3 s la 1re minute, puis toutes les 20 s) */
    if (tbl() - H.flush_tb > (H.vi_count < 3000u ? TB_SEC * 3 : TB_SEC * 20)) nr_log_flush();
}
#define NR_HOOK(nm, counter) \
    static u32 nm##_orig = 0; \
    static void nm##_hook(u32 irq, u32 ctx) { \
        if (nm##_orig) ((void (*)(u32, u32))nm##_orig)(irq, ctx); \
        counter++; alive_tick(); \
    }
NR_HOOK(vi, H.vi_count)
NR_HOOK(ai, H.cnt_ai)
NR_HOOK(dsp, H.cnt_dsp)
NR_HOOK(pet, H.cnt_pe)
NR_HOOK(pef, H.cnt_pe)

/* v60 : plantage du jeu (acces memoire interdit, code introuvable, alignement, instruction
   invalide) : on note ou, on ecrit le journal, et la console redemarre (retour a GC Station
   si c'est ipl.dol). Table des exceptions du SDK : 0x80003000 (2 DSI, 3 ISI, 5, 6).
   Le gestionnaire tourne sur sa propre pile (celle du jeu est peut-etre la cause). */
#define OS_EXC_TABLE ((volatile u32 *)0x80003000u)
u8 exc_stack[2048] __attribute__((aligned(16), used));
void exc_c(u32 ex, u32 ctx) __attribute__((used));
extern void exc_entry(void);
__asm__(".pushsection .text\n.globl exc_entry\nexc_entry:\n"
        "  lis 1,exc_stack@ha\n  addi 1,1,exc_stack@l\n  addi 1,1,2032\n  b exc_c\n.popsection\n");
void exc_c(u32 ex, u32 ctx) {
    u32 dar, dsisr;
    __asm__ volatile("mfdar %0" : "=r"(dar));
    __asm__ volatile("mfdsisr %0" : "=r"(dsisr));
    if (ctx < 0x01800000u) ctx |= 0x80000000u;           /* (adresse physique -> virtuelle) */
    H.exc_num = 0x100u | (ex & 0xFF);
    H.exc_dar = dar; H.exc_dsisr = dsisr;
    if (ctx >= 0x80000000u && ctx < 0x817F0000u) { H.exc_srr0 = *(volatile u32 *)(ctx + 0x198); H.exc_lr = *(volatile u32 *)(ctx + 0x84); }
    nr_log_flush();
    ret_to_gcstation();                                  /* (ou redemarrage si impossible) */
}
static void si_hook(u32 irq, u32 ctx) {
    if (si_orig) ((void (*)(u32, u32))si_orig)(irq, ctx);
    H.si_count++;
    alive_tick();
    volatile u32 *si = (volatile u32 *)0xCC006400u;
    int on = 0;
    u32 sr = si[14];                                     /* SISR (lecture sans effet) */
    for (int ch = 0; ch < 4; ch++) {
        /* v58 : lire SICnINBUFH efface le drapeau "donnees neuves" (RDST) : si le jeu ne les a
           pas encore prises, il croirait la manette debranchee (1080 : fige sur la question 60 Hz).
           On ne lit donc que si le jeu les a deja prises (RDST = 0). */
        if (sr & (0x20000000u >> (ch * 8))) { H.si_skip++; continue; }
        u32 b = si[ch * 3 + 1];                          /* SICnINBUFH : boutons (deja lus par le jeu) */
        (void)si[ch * 3 + 2];                            /* v59 : puis INBUFL, comme le jeu (H puis L : rien de bloque) */
        H.si_reads++;
        if (ch == 0 && (b >> 16) != H.si_last) { H.si_last = b >> 16; H.si_changes++; }
        if (b & 0x80000000u) continue;                   /* (pas de manette) */
        if ((b & 0x11000000u) == 0x11000000u && (b & 0x00100000u)) on = 1;   /* A + START, Z */
    }
    igr_hold = on ? igr_hold + 1 : 0;
    if (igr_hold > 40) {                                 /* (environ 1 s) */
        H.igr = 1;
        nr_log_flush();
        ret_entry();
    }
}
/* ---------- v61 : lectures en fond ----------
   Avant, une lecture de 512 Ko se faisait d'un coup, interruptions coupees (~0,5 s) : le son
   du jeu (DMA audio, DSP) n'etait plus servi et craquait. Maintenant une grosse lecture se fait
   par morceaux de 8 Ko, un a chaque fin de commande du vrai lecteur (son interruption DI) :
   entre deux morceaux, la console sert les autres interruptions (son, image), comme avec un
   vrai lecteur. A la fin : TCINT dans le faux DISR, et la fonction du jeu est appelee. */
static u32 di_orig = 0, rd_dst = 0, rd_off = 0, rd_left = 0;
static int rd_on = 0;
static void di_hook(u32 irq, u32 ctx) {
    if (rd_on) {
        volatile u32 *r = DI_REAL;
        u32 dt = (tbl() - kick_tb) / 41u;                /* (us depuis le reveil) */
        H.kick_us += dt; if (dt > H.kick_max_us) H.kick_max_us = dt;
        r[0] = (r[0] & 0x2Au) | 0x54u;                   /* (vraie fin : acquittee ici, le jeu n'en sait rien) */
        u32 n = rd_left > RD_CHUNK ? RD_CHUNK : rd_left;
        H.n_chunks++;
        if (nr_read((void *)rd_dst, rd_off, n)) {
            rd_on = 0; di[0] |= 0x04; di[7] &= ~1u;      /* DEINT : erreur, le jeu redemande */
            nr_log_add(di[2], rd_off, n, 1 | (H.err & 0xFF) << 24);
        } else {
            cache_flush(rd_dst, n);
            rd_dst += n; rd_off += n; rd_left -= n;
            di[5] += n; di[6] -= n;
            if (rd_left) { di_real_kick(); return; }     /* (morceau suivant a la prochaine fin) */
            rd_on = 0; di[0] |= 0x10; di[7] &= ~1u;      /* TCINT : lecture terminee */
        }
    }
    if (di_orig) ((void (*)(u32, u32))di_orig)(irq, ctx);
    alive_tick();
}
static int nr_game_fn(u32 a) { return a >= 0x80003100u && a < 0x817F0000u; }
static void hook1(int idx, u32 *orig, u32 fn) {
    volatile u32 *t = OS_INT_TABLE;
    u32 v = t[idx];
    if (v && v != fn && nr_game_fn(v)) { *orig = v; t[idx] = fn; }
}
static void hooks_check(void) {
    volatile u32 *t = OS_INT_TABLE;
    if (!nr_game_fn(t[21]) && t[21] != (u32)di_hook) return;   /* (pas la table du SDK : on ne touche a rien) */
    if (!rd_on) hook1(21, &di_orig, (u32)di_hook);       /* v61 : DI (lectures en fond) */
    hook1(24, &vi_orig, (u32)vi_hook);
    hook1(5, &ai_orig, (u32)ai_hook);                    /* (DSP_AI : DMA du son) */
    hook1(7, &dsp_orig, (u32)dsp_hook);
    hook1(18, &pet_orig, (u32)pet_hook);
    hook1(19, &pef_orig, (u32)pef_hook);
    if (H.flags & NRF_IGR) hook1(20, &si_orig, (u32)si_hook);
    volatile u32 *e = OS_EXC_TABLE;
    if (nr_game_fn(e[2])) {                              /* (exceptions : remplacees, pas chainees) */
        static const int ex[4] = { 2, 3, 5, 6 };
        for (int i = 0; i < 4; i++) if (e[ex[i]] != (u32)exc_entry && nr_game_fn(e[ex[i]])) e[ex[i]] = (u32)exc_entry;
    }
}

static void di_exec(void) {
    u32 info = 0;
    rd_on = 0;                                           /* (nouvelle commande : une lecture en fond oubliee s'arrete) */
    if (!di_ready) { di_ready = nr_init() == 0; if (!di_ready) info |= 8; }   /* 1er acces du jeu : on demarre la carte SD */
    hooks_check();
    u32 cmd = di[2] >> 24, len = di[6], dst = di[5] | 0x80000000u;
    H.ncmd++; H.last_cmd = di[2]; H.last_off = di[3] << 2; H.last_len = len;
    di[8] = 0;
    if (cmd == 0xA8) {
        u32 off = (di[2] & 0xC0) == 0x40 ? 0 : di[3] << 2;   /* A8000040 : ID du disque (debut de l'ISO) */
        if ((di[2] & 0xC0) == 0x40 && len > 0x20) len = 0x20;
        H.last_off = off; H.last_len = len;
        if (len > RD_CHUNK && di_orig && (H.flags & NRF_REAL_IRQ)) {   /* v61 : grosse lecture : en fond */
            rd_dst = dst; rd_off = off; rd_left = len; rd_on = 1;
            H.n_async++;
            info |= di_real_kick() | 0x10;                 /* (le lecteur reste "occupe" : DICR bit 0) */
            nr_log(off, len, info | (di[0] & 0xFF) << 8);
            return;
        }
        if (len && nr_read((void *)dst, off, len)) { di[0] |= 0x04; info |= 1; goto done; }   /* DEINT : erreur */
        cache_flush(dst, len);
        di[5] += len; di[6] = 0;
    } else if (cmd == 0x12) {                            /* infos du lecteur */
        u8 *o = (u8 *)dst;
        for (u32 i = 0; i < len && i < 0x20; i++) o[i] = 0;
        if (len >= 8) { o[4] = 0x20; o[5] = 0x02; o[6] = 0x04; o[7] = 0x02; }   /* (une revision connue) */
        cache_flush(dst, len);
        di[5] += len; di[6] = 0;
    }
    else if (cmd == 0xE1) H.n_e1++;
    else if (cmd == 0xE2) H.n_e2++;
    else if (cmd == 0xAB) H.n_ab++;
    else if (cmd != 0xE0) H.n_misc++;
    /* AB (chercher), E0 (erreur : 0), E1-E4 (audio), autres : rien a faire */
    di[0] |= 0x10;                                       /* TCINT : termine */
done:
    di[7] &= ~1u;
    if (H.flags & NRF_REAL_IRQ) info |= di_real_kick();
    nr_log(H.last_off, H.last_len, info | (di[0] & 0xFF) << 8);
}

static u32 hk_n = 0;
static void hooks_lazy(void) { if ((++hk_n & 255) == 1) hooks_check(); }   /* v62 : le jeu installe ses fonctions apres sa 1re commande */
static u32 di_load(u32 reg) {
    u32 i = (reg & 0x3F) >> 2;
    hooks_lazy();
    if (i == 1) return di[1] & 0x02;                      /* couvercle ferme, pas d'interruption */
    if (i == 9) return DI_REAL[9];                        /* DICFG : celui du vrai lecteur */
    return i < 10 ? di[i] : 0;
}

static void di_store(u32 reg, u32 val) {
    u32 i = (reg & 0x3F) >> 2;
    hooks_lazy();
    if (i == 0) {                                        /* DISR : 1 = effacer l'interruption ; masques */
        di[0] = (di[0] & ~(val & 0x54u) & 0x54u) | (val & 0x2Au);
        u32 rs = DI_REAL[0];
        H.last_real_disr = rs;
        if (rs & 0x10) H.nack_tc++;
        DI_REAL[0] = (val & 0x2Au) | 0x54u;              /* (vrai lecteur : memes masques, tout acquitte) */
        H.pad++;                                         /* v55 : nombre d'ecritures de DISR par le jeu */
        if (H.pad <= 16) {                               /* (les premieres : dans le journal, on voit si le jeu se reveille) */
            nr_log_add(0xAC000000u | (val & 0xFF), rs, di[0], 0);
            nr_log_flush();
        }
        return;
    }
    if (i == 1) { di[1] = val & 0x02; DI_REAL[1] = 4u; return; }   /* (vrai couvercle : acquitte, sans interruption) */
    if (i == 7) {
        di[7] = val;
        if (val & 1) { u32 m = irq_off(); di_exec(); irq_restore(m); }
        return;
    }
    if (i < 10) di[i] = val;
}

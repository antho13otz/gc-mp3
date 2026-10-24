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
static u32 exi_load(u32 a);
static void exi_store(u32 a, u32 val);

static natres_ext_t ext_tab[NATRES_MAXEXT];
static natres_ext_t ret_tab[NATRES_RETMAX];              /* v64 : le .dol de GC Station sur la carte */
static natres_ext_t card_tab[NATRES_CARDMAX];            /* v65 : le fichier de la carte memoire virtuelle */
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
    (u32)exi_load, (u32)exi_store, 0, (u32)card_tab, 0, 0x10,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, { 0, 0, 0, 0 }, 0, 0,
    0, 0, 0, 0,
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
/* v94 : CRC16 des blocs de donnees (CCITT, comme la carte) : sert a verifier le 32 MHz */
static unsigned short crc_tab[256];
static void crc_init(void) {
    for (u32 i = 0; i < 256; i++) { u32 c = i << 8; for (int k = 0; k < 8; k++) c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1; crc_tab[i] = (unsigned short)c; }
}
static u32 crc16(const u8 *p, u32 n) { u32 c = 0; while (n--) c = ((c << 8) ^ crc_tab[((c >> 8) ^ *p++) & 0xFF]) & 0xFFFF; return c; }
static int x_dma = 0, x_fast = 0, x_fast_off = 0, x_crcbad = 0;   /* (H.xfer) */
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

/* v94 : DMA et 32 MHz, essayes une fois (le meme secteur lu en mode simple, en DMA, puis a 32 MHz,
   4 fois, CRC verifie) : gardes seulement si tout est identique */
static int nr_read_sd(u32 sector, void *dst);
static u8 t_ref[512] __attribute__((aligned(32))), t_buf[512] __attribute__((aligned(32)));
static int t_same(void) { for (int i = 0; i < 512; i++) if (t_ref[i] != t_buf[i]) return 0; return 1; }
static void nr_speed(void) {
    static int tried = 0;
    u32 sec = H.n_ext ? ext_tab[0].sd : 0;
    if (!crc_tab[1]) crc_init();
    if (tried) { x_fast = x_fast && !x_fast_off; if (x_fast) freq = 5; goto out; }
    tried = 1;
    x_dma = 0; x_fast = 1;                                  /* (CRC verifie pendant l'essai) */
    u32 slow = freq > 4 ? 4 : freq;
    freq = slow;
    if (nr_read_sd(sec, t_ref)) { x_fast = 0; goto done; }
    x_dma = 1;
    if (nr_read_sd(sec, t_buf) || !t_same()) x_dma = 0;
    freq = 5;
    for (int k = 0; k < 4 && x_fast; k++) if (nr_read_sd(sec, t_buf) || !t_same()) x_fast = 0;
    if (!x_fast) freq = slow;
done:
    H.err = NR_OK; H.crc_err = 0; x_crcbad = 0;
out:
    H.xfer = (u32)x_dma | (u32)x_fast << 1 | (u32)x_fast_off << 2;
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
    nr_speed();
    return 0;
}

/* v94 : les 512 octets d'un bloc (apres le jeton FE) puis son CRC. En DMA, le bus EXI copie le bloc
   tout seul en memoire (adresse alignee sur 32) : bien moins de travail pour le processeur. A 32 MHz,
   le CRC est verifie (un bloc faux = erreur : relu, et on repasse a 16 MHz). Rend 0 si bon. */
static u8 dmabuf[512] __attribute__((aligned(32)));
static int nr_block(u8 *o) {
    u8 *d = o;
    if (x_dma) {
        if ((u32)o & 31) d = dmabuf;
        for (u32 a = (u32)d; a < (u32)d + 512; a += 32) __asm__ volatile("dcbi 0,%0" :: "r"(a) : "memory");
        exi[1] = (u32)d & 0x1FFFFFE0u;
        exi[2] = 512;
        exi[3] = 3;                               /* DMA, lecture, depart */
        if (ex_wait()) return -1;
        if (d != o) for (int i = 0; i < 512; i++) o[i] = d[i];
    } else {
        for (int i = 0; i < 128; i++) {
            u32 w = ex_get(4);
            o[i * 4] = (u8)(w >> 24); o[i * 4 + 1] = (u8)(w >> 16); o[i * 4 + 2] = (u8)(w >> 8); o[i * 4 + 3] = (u8)w;
        }
    }
    u32 crc = ex_get(2) >> 16;
    if (x_fast && crc16(o, 512) != crc) { H.crc_err++; x_crcbad = 1; H.err = NR_ETOKEN; return -1; }
    return 0;
}
static int nr_read_sd(u32 sector, void *dst) {
    u8 *o = (u8 *)dst;
    u32 r = sd_cmd(17, H.sdhc ? sector : sector << 9, 0x01);   /* CMD17 : lire un secteur */
    if (r != 0) { sd_end(); H.err = NR_ECMD; return -1; }
    u32 t = 0xFF;
    for (u32 i = 0; i < 400000u; i++) { t = ex_get8(); if (t != 0xFF) break; }
    if (t != 0xFE) { sd_end(); H.err = NR_ETOKEN; H.r1 = t; return -1; }
    int e = nr_block(o);
    sd_end();
    if (e) return -1;
    H.nread++;
    return 0;
}

/* v88 : plusieurs secteurs a la suite (CMD18 : une commande, puis les blocs ; CMD12 : arret).
   Avant : une commande par secteur (CMD17) : la carte cherche a chaque fois (lecture lente,
   le jeu attendait, le son du jeu craquait / coupait). */
static int nr_read_multi(u32 sector, u32 count, u8 *o) {
    u32 r = sd_cmd(18, H.sdhc ? sector : sector << 9, 0x01);
    if (r != 0) { sd_end(); H.err = NR_ECMD; return -1; }
    int ok = 1;
    for (u32 s = 0; s < count && ok; s++) {
        u32 t = 0xFF;
        for (u32 i = 0; i < 400000u; i++) { t = ex_get8(); if (t != 0xFF) break; }
        if (t != 0xFE) { H.err = NR_ETOKEN; H.r1 = t; ok = 0; break; }
        if (nr_block(o)) { ok = 0; break; }
        o += 512;
        H.nread++;
    }
    ex_put(0x4C000000u, 4);                           /* CMD12 : arret de la lecture */
    ex_put(0x00010000u, 2);
    ex_get8();                                        /* (octet de bourrage) */
    for (int i = 0; i < 16; i++) { r = ex_get8(); if (!(r & 0x80)) break; }
    for (u32 i = 0; i < 400000u; i++) if (ex_get8() == 0xFF) break;   /* (occupee : 00) */
    sd_end();
    return ok ? 0 : -1;
}

/* v54 : ecrire un secteur (CMD24) : seulement le journal, dans un fichier prepare par GC Station */
static int nr_write_sd1(u32 sector, const u8 *s);
static int nr_write_sd(u32 sector, const u8 *s) {       /* v94 : ecriture toujours a 16 MHz au plus (pas de CRC verifie) */
    u32 f = freq;
    if (freq > 4) freq = 4;
    int r = nr_write_sd1(sector, s);
    freq = f;
    return r;
}
static int nr_write_sd1(u32 sector, const u8 *s) {
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

/* secteur de l'ISO -> secteur de la carte (v88 : *run = secteurs a la suite sur la carte) */
static u32 iso_run = 1;
static int iso_to_sd(u32 isec, u32 *sd) {
    u32 n = H.n_ext;
    if (ext_last < n) {
        const natres_ext_t *e = &ext_tab[ext_last];
        if (isec >= e->iso && isec - e->iso < e->cnt) { *sd = e->sd + (isec - e->iso); iso_run = e->cnt - (isec - e->iso); return 0; }
    }
    u32 lo = 0, hi = n;                           /* (morceaux ranges dans l'ordre de l'ISO) */
    while (lo < hi) {
        u32 mid = (lo + hi) >> 1;
        const natres_ext_t *e = &ext_tab[mid];
        if (isec < e->iso) hi = mid;
        else if (isec - e->iso >= e->cnt) lo = mid + 1;
        else { ext_last = mid; *sd = e->sd + (isec - e->iso); iso_run = e->cnt - (isec - e->iso); return 0; }
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
        if (x_fast && x_crcbad) x_fast_off = 1;           /* v94 : CRC faux a 32 MHz : retour a 16 MHz */
        x_crcbad = 0;
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
        u32 run = len >> 9;
        if (in == 0 && run >= 2 && iso_run >= 2) {        /* v88 : plusieurs secteurs entiers a la suite : une commande */
            if (run > iso_run) run = iso_run;
            if (run > 64) run = 64;
            if (nr_read_multi(sd, run, o)) return -1;
            n = run << 9;
        } else if (in == 0 && n == 512) {          /* secteur entier : directement a destination */
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

/* v66 : "inquiry" du jeu : on la pose au vrai lecteur et on attend sa reponse (interruptions
   coupees, < 50 ms) ; sa fin est acquittee ici (le reveil du jeu vient ensuite, comme avant) */
static int di_real_inq(void) {
    volatile u32 *r = DI_REAL;
    if (!(H.flags & NRF_REAL_IRQ)) return 0;
    for (u32 i = 0; i < 3000000u && (r[7] & 1); i++) ;
    if (r[7] & 1) return 0;
    r[2] = 0x12000000u; r[3] = 0; r[4] = 0x20;
    r[5] = (u32)di_scratch & 0x01FFFFFFu; r[6] = 0x20;
    r[7] = 3;
    u32 t0 = tbl();
    while ((r[7] & 1) && tbl() - t0 < 40500000u / 20) ;   /* (50 ms) */
    if (r[7] & 1) return 0;
    u32 sr = r[0];
    r[0] = (sr & 0x2Au) | 0x54u;
    __asm__ volatile("dcbi 0,%0" :: "r"((u32)di_scratch) : "memory");
    H.n_inq++;
    return (sr & 0x10) != 0;
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
#define RD_CHUNK 2048u                                    /* v61 : morceau d'une lecture en fond (v88 : 8 Ko -> 2 Ko) */
#define RD_BUDGET (TB_SEC / 1000u * 3u / 2u)              /* v88 : au plus ~1,5 ms de lecture par interruption (le son du jeu
                                                             attend : une sortie audio toutes les ~5 ms) */
static u32 si_orig, igr_hold = 0;
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
static void x_kick(void);
static u32 x_int = 0, x_csr = 0;
static void alive_tick(void) {
    if ((++alive_n & 63) == 0) hooks_check();            /* (le jeu a pu remettre sa fonction : on se rebranche) */
    if (x_int & ((x_csr & 1 ? 2u : 0) | (x_csr & 4 ? 8u : 0))) x_kick();   /* (carte : interruption pas encore donnee) */
    /* (une ecriture sur la carte coupe le son quelques ms : toutes les 3 s la 1re minute, puis toutes les 20 s) */
    if (tbl() - H.flush_tb > (H.vi_count < 3000u ? TB_SEC * 3 : TB_SEC * 20)) nr_log_flush();
}
#define NR_HOOK(nm, counter) \
    static u32 nm##_orig = 0; \
    static void nm##_hook(u32 irq, u32 ctx) { \
        if (nm##_orig) ((void (*)(u32, u32))nm##_orig)(irq, ctx); \
        counter++; alive_tick(); \
    }
static u32 vi_orig;
static void nr_log_flush(void);
static void igr_poll(void);
static void igr_poll2(int count);
static void vi_hook(u32 irq, u32 ctx) {                  /* v66 : entree comptee avant la fonction du jeu */
    if (++H.vi_in == 1) nr_log_flush();                  /* (1re image : on sait si le jeu y arrive) */
    if (vi_orig) ((void (*)(u32, u32))vi_orig)(irq, ctx);
    H.vi_count++; alive_tick();
    igr_poll();                                          /* v86 : (apres le jeu : il a lu les manettes) */
}
static u32 ai_orig;
static void st_mix(void);
static void ai_hook(u32 irq, u32 ctx) {                  /* v65 : apres le jeu, la musique du disque */
    if (ai_orig) ((void (*)(u32, u32))ai_orig)(irq, ctx);
    H.cnt_ai++;
    st_mix();
    alive_tick();
    igr_poll2(0);                                        /* v89 : aussi ici (~ toutes les 5 ms) : a l'image (VI), le jeu n'a
                                                            souvent pas encore pris les manettes (Wind Waker) : lecture interdite */
}
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
/* v83 : le vecteur du SDK ne sauve que r3-r5, CR, LR, CTR, XER, SRR0/1 : r0-r2 et r6-r31 sont
   sauves par le gestionnaire (comme OSDefaultExceptionHandler), sinon le contexte garde les
   valeurs d'un ancien changement de tache (journal faux : r1, pile, appels). (CTR : deja sauve) */
__asm__(".pushsection .text\n.globl exc_entry\nexc_entry:\n"
        "  mtctr 0\n  oris 5,4,0x8000\n  rlwinm 0,5,7,25,31\n  cmplwi 0,0x40\n  mfctr 0\n  bne 1f\n"
        "  stw 0,0(5)\n  stw 1,4(5)\n  stw 2,8(5)\n  stmw 6,0x18(5)\n"
        "1:\n  lis 1,exc_stack@ha\n  addi 1,1,exc_stack@l\n  addi 1,1,2032\n  b exc_c\n.popsection\n");
/* v79 : details d'un plantage (format : natres.h, NATRES_CRASH_*), pour savoir POURQUOI le
   jeu s'est arrete : registres, chaine des appels (pile du jeu), textes passes a sa fonction
   d'erreur (un jeu qui s'arrete expres ecrit souvent a une adresse bidon comme 01234567,
   apres avoir prepare un message), et la memoire que le jeu a recue. */
static u8 crashbuf[512] __attribute__((aligned(32)));
static int ram_ok(u32 a, u32 n) { return a >= 0x80000000u && a < 0x81800000u && a + n <= 0x81800000u; }
static void cw(u32 o, u32 v) { crashbuf[o] = (u8)(v >> 24); crashbuf[o + 1] = (u8)(v >> 16); crashbuf[o + 2] = (u8)(v >> 8); crashbuf[o + 3] = (u8)v; }
/* v82 : la pile du jeu au moment du plantage (3e secteur du journal) : un jeu qui s'arrete
   expres (OSPanic) y a range le nom du fichier, la ligne et le message de son erreur */
static int is_txt(u32 a) {
    if (!ram_ok(a, 8)) return 0;
    int n = 0;
    for (u32 i = 0; i < 24; i++) { u8 ch = *(volatile u8 *)(a + i); if (!ch) break; if (ch < 0x20 || ch > 0x7E) return 0; n++; }
    return n >= 4;
}
static void stack_dump(u32 sp) {
    for (u32 i = 0; i < 512; i++) crashbuf[i] = 0;
    cw(0x00, NATRES_STACK_MAGIC); cw(0x04, sp);
    if (!ram_ok(sp & ~3u, NATRES_STACK_WORDS * 4)) { nr_write_sd(H.log_sec + 2, crashbuf); return; }
    sp &= ~3u;
    cw(0x08, NATRES_STACK_WORDS);
    u32 ns = 0;
    for (u32 i = 0; i < NATRES_STACK_WORDS; i++) {
        u32 v = *(volatile u32 *)(sp + i * 4);
        cw(0x10 + i * 4, v);
        if (ns < NATRES_STACK_NSTR && is_txt(v)) {
            int dup = 0;
            for (u32 k = 0; k < ns; k++) { u32 o = 0x150 + k * 32; if (((u32)crashbuf[o] << 24 | crashbuf[o + 1] << 16 | crashbuf[o + 2] << 8 | crashbuf[o + 3]) == v) dup = 1; }
            if (dup) continue;
            u32 o = 0x150 + ns * 32;
            cw(o, v);
            for (u32 j = 0; j < NATRES_STACK_SLEN; j++) crashbuf[o + 4 + j] = *(volatile u8 *)(v + j);
            ns++;
        }
    }
    cw(0x0C, ns);
    nr_write_sd(H.log_sec + 2, crashbuf);
}
/* v83 : photo de la memoire du jeu (format : natres.h, NATRES_SNAP_*) */
static void mem_snap(volatile u32 *c) {
    if (!(H.flags & NRF_SNAP) || !H.snap_sec || H.snap_cnt < 2) return;
    u32 zb[4], zl[4];
    zb[0] = 0x80000000u; zl[0] = 0x3200;
    zb[1] = (c[13] & ~31u) - 0x8000u; zl[1] = 0x10000;
    zb[2] = (c[1] & ~31u) - 0x200u; zl[2] = 0x4000;
    zb[3] = (H.snap_hi + 31) & ~31u; zl[3] = 0x40000;
    for (u32 i = 0; i < 512; i++) crashbuf[i] = 0;
    cw(0x00, NATRES_SNAP_MAGIC);
    u32 sec = 1, nz = 0;
    for (int z = 0; z < 4; z++) {
        u32 b = zb[z], l = (zl[z] + 511) & ~511u;
        if (b < 0x80000000u) b = 0x80000000u;
        if (b >= 0x81800000u) continue;
        if (b + l > 0x81800000u) l = (0x81800000u - b) & ~511u;
        if (sec + (l >> 9) > H.snap_cnt) l = (H.snap_cnt - sec) << 9;
        if (!l) continue;
        cw(0x08 + nz * 16, b); cw(0x0C + nz * 16, l); cw(0x10 + nz * 16, sec);
        for (u32 o = 0; o < l; o += 512, sec++) if (nr_write_sd(H.snap_sec + sec, (const u8 *)(b + o)) == 0) H.snap_done++;
        nz++;
    }
    cw(0x04, nz);
    nr_write_sd(H.snap_sec, crashbuf);
}
static void crash_dump(u32 ex, u32 ctx, u32 dar, u32 dsisr) {
    if (!(H.flags & NRF_CRASH) || !H.log_sec || !di_ready) return;
    for (u32 i = 0; i < 512; i++) crashbuf[i] = 0;
    cw(0x00, NATRES_CRASH_MAGIC); cw(0x04, NATRES_CRASH_VER); cw(0x08, ex);
    cw(0x20, dar); cw(0x24, dsisr); cw(0x28, ctx);
    static const u32 low[8] = { 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0xF0, 0xF4 };
    for (int i = 0; i < 8; i++) cw(0xB0 + i * 4, *(volatile u32 *)(0x80000000u + low[i]));
    if (ctx < 0x80000000u || ctx >= 0x817F0000u || (ctx & 3)) { nr_write_sd(H.log_sec + 1, crashbuf); return; }
    volatile u32 *c = (volatile u32 *)ctx;               /* OSContext : r0-r31 0x00, CR 0x80, LR 0x84, CTR 0x88, srr0 0x198, srr1 0x19C */
    cw(0x0C, c[0x198 / 4]); cw(0x10, c[0x19C / 4]); cw(0x14, c[0x84 / 4]); cw(0x18, c[0x80 / 4]); cw(0x1C, c[0x88 / 4]);
    for (int r = 0; r < 32; r++) cw(0x30 + r * 4, c[r]);
    u32 sp = c[1], n = 0;                                /* pile : [sp] = cadre de l'appelant, [cadre + 4] = adresse de retour */
    while (n < NATRES_CRASH_NBT && ram_ok(sp, 8) && !(sp & 3)) {
        u32 up = *(volatile u32 *)sp;
        if (up <= sp || !ram_ok(up, 8) || (up & 3)) break;
        cw(0xD0 + n * 4, *(volatile u32 *)(up + 4));
        n++; sp = up;
    }
    cw(0x2C, n);
    for (int r = 3; r <= 10; r++) {                      /* textes : ce que pointent r3 ... r10 (si c'est de la memoire) */
        u32 a = c[r];
        if (!ram_ok(a, NATRES_CRASH_STR)) continue;
        for (u32 i = 0; i < NATRES_CRASH_STR; i++) crashbuf[0x110 + (r - 3) * NATRES_CRASH_STR + i] = *(volatile u8 *)(a + i);
    }
    nr_write_sd(H.log_sec + 1, crashbuf);
    if (H.flags & NRF_STACK) stack_dump(c[1]);
    mem_snap(c);
}
void exc_c(u32 ex, u32 ctx) {
    u32 dar, dsisr;
    __asm__ volatile("mfdar %0" : "=r"(dar));
    __asm__ volatile("mfdsisr %0" : "=r"(dsisr));
    if (ctx < 0x01800000u) ctx |= 0x80000000u;           /* (adresse physique -> virtuelle) */
    H.exc_num = 0x100u | (ex & 0xFF);
    H.exc_dar = dar; H.exc_dsisr = dsisr;
    if (ctx >= 0x80000000u && ctx < 0x817F0000u) { H.exc_srr0 = *(volatile u32 *)(ctx + 0x198); H.exc_lr = *(volatile u32 *)(ctx + 0x84); }
    if (!di_ready) di_ready = nr_init() == 0;             /* v79 : (plantage avant la 1re commande au lecteur : carte pas encore demarree) */
    crash_dump(ex, ctx, dar, dsisr);                      /* v79 : details (registres, appels, textes) dans le 2e secteur */
    nr_log_flush();
    ret_to_gcstation();                                  /* (ou redemarrage si impossible) */
}
static void nr_sample(u32 ctx);
/* v86 : A+Z+START vu aussi a chaque image (VI) : les jeux recents lisent les manettes pendant
   l'interruption de l'ecran et ne declenchent presque plus d'interruption SI (Wind Waker) */
static u32 igr_t0 = 0;
static void igr_poll(void);
static void si_hook(u32 irq, u32 ctx) {
    H.si_in++;
    if (!H.vi_count) nr_sample(ctx);
    if (si_orig) ((void (*)(u32, u32))si_orig)(irq, ctx);
    H.si_count++;
    alive_tick();
    igr_poll();
}
static void igr_poll(void) { igr_poll2(1); }
/* count = 0 : ne compte que le temps (interruption du son : trop frequente pour compter les lectures) */
static void igr_poll2(int count) {
    if (!(H.flags & NRF_IGR)) return;
    volatile u32 *si = (volatile u32 *)0xCC006400u;
    int on = 0, seen = 0;
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
        seen = 1;
        if ((b & 0x11000000u) == 0x11000000u && (b & 0x00100000u)) on = 1;   /* A + START, Z */
    }
    u32 now = tbl();
    /* v93 : aucune manette lue cette fois (donnees pas encore prises par le jeu) : on ne sait rien, on garde
       l'etat. Avant, cela remettait le compte a zero : tres frequent depuis l'interruption du son (v89),
       A+Z+START n'etait jamais tenu 1 s "sans interruption" et ne marchait dans aucun jeu. */
    if (!seen) return;
    if (!on) { igr_hold = 0; return; }
    if (!igr_hold) { igr_t0 = now; igr_hold = 1; }
    if (count) igr_hold++;
    if (now - igr_t0 > TB_SEC || igr_hold > 60) {        /* (tenus 1 s, ou 60 lectures des manettes de suite) */
        H.igr = 1;
        nr_log_flush();
        ret_entry();
    }
}
/* ======================= v65 : musique du disque (audio en streaming, "DTK") =======================
   Certains jeux (1080, Zelda, F-Zero...) jouent leur musique directement depuis le disque :
   le lecteur envoie des echantillons ADPCM au processeur du son (AI). Ici, pas de vrai disque :
   Resident lit ces blocs ADPCM dans l'ISO, les decode (meme formule que la console / Dolphin)
   et les ajoute au son que le jeu envoie a la sortie audio (tampon DMA), au rythme de ses
   interruptions DMA. Commandes : E1 (jouer / arreter), E2 (etat, position). */
static u32 st_on = 0, st_stop_end = 0, st_pos = 0, st_cur_start = 0, st_cur_len = 0, st_next_start = 0, st_next_len = 0;
static int st_hl1 = 0, st_hl2 = 0, st_hr1 = 0, st_hr2 = 0;
static short st_pcm[56];
static u32 st_pcm_i = 28, st_phase = 0;
static int st_cl = 0, st_cr = 0;
static u8 st_sec[512] __attribute__((aligned(32)));
static u32 st_sec_off = 0xFFFFFFFFu;
static int st_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int st_dec(int bits, int q, int *h1, int *h2) {
    int hist = 0;
    switch (q >> 4) {
    case 1: hist = *h1 * 0x3c; break;
    case 2: hist = *h1 * 0x73 - *h2 * 0x34; break;
    case 3: hist = *h1 * 0x62 - *h2 * 0x37; break;
    default: break;
    }
    hist = st_clamp((hist + 0x20) >> 6, -0x200000, 0x1fffff);
    int cur = (((int)(short)(bits << 12) >> (q & 0xf)) << 6) + hist;
    *h2 = *h1; *h1 = cur;
    return st_clamp(cur >> 6, -0x8000, 0x7fff);
}
static int st_block(void) {                              /* bloc suivant (32 octets -> 28 echantillons stereo) */
    if (st_pos >= st_cur_start + st_cur_len) {
        st_pos = st_next_start; st_cur_start = st_next_start; st_cur_len = st_next_len;
        if (st_stop_end) { st_stop_end = 0; st_on = 0; return -1; }
        st_hl1 = st_hl2 = st_hr1 = st_hr2 = 0;
    }
    u32 so = st_pos & ~511u, sd;
    if (so != st_sec_off) {
        if (iso_to_sd(so >> 9, &sd) || nr_read_sd(sd, st_sec)) { st_on = 0; return -1; }
        st_sec_off = so;
    }
    const u8 *a = st_sec + (st_pos & 511);
    for (int i = 0; i < 28; i++) {
        st_pcm[i * 2] = (short)st_dec(a[i + 4] & 0xF, a[0], &st_hl1, &st_hl2);
        st_pcm[i * 2 + 1] = (short)st_dec(a[i + 4] >> 4, a[1], &st_hr1, &st_hr2);
    }
    st_pos += 32; st_pcm_i = 0;
    H.st_blocks++;
    return 0;
}
static void st_adv(void) {                               /* echantillon suivant du disque (48 kHz) */
    if (st_pcm_i >= 28 && (!st_on || st_block())) { st_cl = st_cr = 0; return; }
    st_cl = st_pcm[st_pcm_i * 2]; st_cr = st_pcm[st_pcm_i * 2 + 1]; st_pcm_i++;
}
static void st_cmd(u32 c0, u32 off, u32 len) {           /* E1 */
    u32 sub = (c0 >> 16) & 0xFF;
    if (sub == 0) {
        if (off == 0 && len == 0) st_stop_end = 1;
        else if (!st_stop_end) {
            st_next_start = off; st_next_len = len;
            if (!st_on) {
                st_cur_start = st_next_start; st_cur_len = st_next_len; st_pos = st_cur_start;
                st_hl1 = st_hl2 = st_hr1 = st_hr2 = 0; st_pcm_i = 28; st_phase = 0;
                st_on = 1;
            }
        }
    } else if (sub == 1) { st_stop_end = 0; st_on = 0; }
}
static u32 st_status(u32 c0) {                           /* E2 */
    switch ((c0 >> 16) & 0xFF) {
    case 0: return st_on ? 1 : 0;
    case 1: return (st_pos & ~0x7FFFu) >> 2;
    case 2: return st_cur_start >> 2;
    case 3: return st_cur_len;
    }
    return 0;
}
/* apres la fonction du jeu (interruption DMA du son) : le tampon qu'il vient de programmer
   va etre joue ; on y ajoute la musique du disque, au bon volume et a la bonne frequence */
static void st_mix(void) {
    volatile u32 *ai = (volatile u32 *)0xCC006C00u;
    u32 cr = ai[0];
    if (!st_on || !(cr & 1)) return;                     /* (PSTAT : le jeu a lance la lecture du disque) */
    volatile unsigned short *dsp = (volatile unsigned short *)0xCC005000u;
    u32 addr = ((((u32)dsp[0x30 / 2] & 0x3FFu) << 16) | dsp[0x32 / 2]) & ~31u;
    u32 len = ((u32)dsp[0x36 / 2] & 0x7FFFu) << 5;
    if (!len || len > 0x8000u || addr + len > 0x01800000u) return;
    u32 vol = ai[1];
    int vl = (int)(vol & 0xFF), vr = (int)((vol >> 8) & 0xFF);
    u32 drate = (cr & 0x40) ? 32000u : 48000u, srate = (cr & 2) ? 48000u : 32000u;
    u32 step = (srate << 16) / drate;
    u8 *p = (u8 *)(addr | 0x80000000u);
    for (u32 a = (u32)p; a < (u32)p + len; a += 32) __asm__ volatile("dcbi 0,%0" :: "r"(a) : "memory");   /* (ecrit par le DSP) */
    short *s = (short *)p;
    u32 n = len / 4;
    for (u32 i = 0; i < n; i++) {
        st_phase += step;
        while (st_phase >= 0x10000u) { st_phase -= 0x10000u; st_adv(); }
        s[i * 2] = (short)st_clamp(s[i * 2] + ((st_cl * vl) >> 8), -0x8000, 0x7fff);
        s[i * 2 + 1] = (short)st_clamp(s[i * 2 + 1] + ((st_cr * vr) >> 8), -0x8000, 0x7fff);
    }
    cache_flush((u32)p, len);
    H.st_mix++;
}

/* ======================= v65 : carte memoire virtuelle (slot A) =======================
   Les acces du jeu aux registres EXI du canal 0 (0xCC006800-13) sont branches ici (comme ceux
   du lecteur). Peripherique 0 (la carte du slot A) : emule, le contenu est un fichier de la
   carte SD (morceaux prepares par GC Station). Peripheriques 1 et 2 (horloge, SRAM, police
   de la console, port serie 1) : transmis tels quels au vrai materiel. Protocole de la carte :
   comme Dolphin (EXI_DeviceMemoryCard). Les "interruptions" de la carte (fin de transfert,
   fin d'ecriture) sont donnees au jeu en appelant ses fonctions EXI, au rythme d'une vraie
   fin de commande du lecteur (meme mecanisme que pour le faux lecteur). */
#define EXI0_REAL ((volatile u32 *)0xCC006800u)
static u32 x_mar = 0, x_len = 0, x_cr = 0, x_data = 0, x_disp = 0;
static u32 c_pos = 0, c_cmd = 0, c_addr = 0, c_status = 0xC1u, c_irq_sw = 0;   /* (occupe | debloquee | prete) */
static u8 c_prog[128];
static u8 c_sec[512] __attribute__((aligned(32)));
static u32 c_sec_no = 0xFFFFFFFFu;
static int card_sd(u32 csec, u32 *sd) {
    for (u32 k = 0; k < H.card_n && k < NATRES_CARDMAX; k++) {
        const natres_ext_t *e = &card_tab[k];
        if (csec >= e->iso && csec - e->iso < e->cnt) { *sd = e->sd + (csec - e->iso); return 0; }
    }
    return -1;
}
static int card_load(u32 csec) {                         /* secteur de la carte dans c_sec */
    if (csec == c_sec_no) return 0;
    u32 sd;
    c_sec_no = 0xFFFFFFFFu;
    if (card_sd(csec, &sd) || nr_read_sd(sd, c_sec)) return -1;
    c_sec_no = csec;
    return 0;
}
static u8 card_rd1(u32 a) { return card_load(a >> 9) ? 0xFF : c_sec[a & 511]; }
static void card_rd(u8 *dst, u32 a, u32 len) {
    while (len) {
        u32 n = 512 - (a & 511);
        if (n > len) n = len;
        if (card_load(a >> 9)) { for (u32 i = 0; i < n; i++) dst[i] = 0xFF; }
        else for (u32 i = 0; i < n; i++) dst[i] = c_sec[(a & 511) + i];
        dst += n; a += n; len -= n;
    }
}
static void card_wr(u32 a, const u8 *src, int fill, u32 len) {   /* src = NULL : remplir avec fill */
    while (len) {
        u32 n = 512 - (a & 511), sd;
        if (n > len) n = len;
        if (n == 512) c_sec_no = a >> 9;                  /* (secteur entier : pas besoin de le relire) */
        if (!card_load(a >> 9)) {
            for (u32 i = 0; i < n; i++) c_sec[(a & 511) + i] = src ? src[i] : (u8)fill;
            if (!card_sd(a >> 9, &sd) && !nr_write_sd(sd, c_sec)) H.card_writes++;
            else c_sec_no = 0xFFFFFFFFu;
        }
        if (src) src += n;
        a += n; len -= n;
    }
}
static void x_kick(void);
static void card_done(void) {                            /* fin d'effacement / d'ecriture */
    c_status |= 0x01; c_status &= ~0x80u;
    if (c_irq_sw) { x_int |= 0x2; H.card_irqs++; x_kick(); }   /* EXIINT */
}
static void card_cs(int on) {
    if (on) { c_pos = 0; return; }
    u32 mask = H.card_size - 1;
    if (c_cmd == 0xF1 && c_pos > 2) { card_wr(c_addr & mask & ~0x1FFFu, 0, 0xFF, 0x2000); card_done(); }
    else if (c_cmd == 0xF4 && c_pos > 2) { card_wr(0, 0, 0xFF, H.card_size); c_status &= ~0x80u; }
    else if (c_cmd == 0xF2 && c_pos >= 5) {
        u32 cnt = c_pos - 5, a = c_addr;
        u8 one;
        for (u32 i = 0; i < cnt; i++) {                  /* (octets envoyes en direct ; le DMA, lui, ecrit tout de suite) */
            one = c_prog[i & 127];
            card_wr(a & mask, &one, 0, 1);
            a = (a & ~0x1FFu) | ((a + 1) & 0x1FFu);
        }
        card_done();
    }
}
static u8 card_byte(u8 b) {
    if (c_pos == 0) {
        c_cmd = b; b = 0xFF;
        H.card_cmds++; H.card_last = c_cmd;
        if (c_cmd == 0x89) { c_status &= ~0x18u; c_status |= 0x01; x_int &= ~0x2u; c_pos = 0; }
    } else {
        switch (c_cmd) {
        case 0x00: b = c_pos == 1 ? 0x80 : (u8)(H.card_id >> (24 - (((c_pos - 2) & 3) * 8))); break;
        case 0x52:
        case 0xF2:
            if (c_pos == 1) c_addr = (u32)b << 17;
            else if (c_pos == 2) c_addr |= (u32)b << 9;
            else if (c_pos == 3) c_addr |= ((u32)b & 3) << 7;
            else if (c_pos == 4) c_addr |= (u32)b & 0x7F;
            if (c_cmd == 0xF2) { if (c_pos >= 5) c_prog[(c_pos - 5) & 0x7F] = b; b = 0xFF; break; }
            if (c_pos == 1) b = 0xFF;
            else {
                b = card_rd1(c_addr & (H.card_size - 1));
                if (c_pos >= 9) c_addr = (c_addr & ~0x1FFu) | ((c_addr + 1) & 0x1FFu);
            }
            break;
        case 0x83: b = (u8)c_status; break;
        case 0x85: b = c_pos == 1 ? 0xC2 : ((c_pos & 1) ? 0x21 : 0xC2); break;   /* (ID 0xC221 : carte Nintendo) */
        case 0xF1: if (c_pos == 1) c_addr = (u32)b << 17; else if (c_pos == 2) c_addr |= (u32)b << 9; b = 0xFF; break;
        case 0x81: if (c_pos == 1) c_irq_sw = b; b = 0xFF; break;
        default: b = 0xFF; break;
        }
    }
    c_pos++;
    return b;
}
static void x_xfer(void) {                               /* transfert vers la carte virtuelle */
    u32 n = ((x_cr >> 4) & 3) + 1, rw = (x_cr >> 2) & 3;
    if (!(x_cr & 2)) {                                   /* immediat (1 a 4 octets dans DATA) */
        if (rw == 1) { for (u32 i = 0; i < n; i++) card_byte((u8)(x_data >> (24 - i * 8))); }
        else if (rw == 0) { u32 v = 0; for (u32 i = 0; i < n; i++) v |= (u32)card_byte(0) << (24 - i * 8); x_data = v; }
    } else {                                             /* DMA */
        u32 dst = (x_mar & 0x01FFFFE0u) | 0x80000000u, len = x_len & 0x03FFFFE0u;
        if (len && dst + len <= 0x81800000u) {
            if (rw == 0) { card_rd((u8 *)dst, c_addr & (H.card_size - 1), len); cache_flush(dst, len); }
            else if (rw == 1) card_wr(c_addr & (H.card_size - 1), (const u8 *)dst, 0, len);
        }
    }
    x_cr &= ~1u;
    x_int |= 0x8;                                        /* TCINT */
    if (x_csr & 0x4) x_kick();                            /* (le jeu attend une interruption) */
}
/* v65 : a = adresse visee par le jeu (calculee pendant le jeu) : seul le canal 0 est virtuel */
static u32 exi_load(u32 a) {
    if (a - 0xCC006800u >= 0x14u || !(H.flags & NRF_CARD)) return *(volatile u32 *)a;   /* (canaux 1 et 2 : vrai materiel) */
    u32 m = irq_off(), v;
    switch ((a - 0xCC006800u) >> 2) {
    case 0: {
        u32 r = EXI0_REAL[0];
        v = (r & ~0x1F8Fu) | (x_csr & 0x0785u) | x_int | (r & 0x2u) | 0x1000u;   /* (masques et CS du jeu, carte presente) */
        if (!(x_csr & 0x80)) v |= r & 0x8u;              /* (fin d'un vrai transfert : horloge, SRAM...) */
        break; }
    case 1: v = (x_csr & 0x80) ? x_mar : EXI0_REAL[1]; break;
    case 2: v = (x_csr & 0x80) ? x_len : EXI0_REAL[2]; break;
    case 3: v = (x_csr & 0x80) ? x_cr : EXI0_REAL[3]; break;
    case 4: v = (x_csr & 0x80) ? x_data : EXI0_REAL[4]; break;
    default: v = 0; break;
    }
    irq_restore(m);
    return v;
}
static void exi_store(u32 a, u32 val) {
    if (a - 0xCC006800u >= 0x14u || !(H.flags & NRF_CARD)) { *(volatile u32 *)a = val; return; }
    u32 m = irq_off();
    switch ((a - 0xCC006800u) >> 2) {
    case 0: {
        u32 old = x_csr;
        if (val & 0x2) x_int &= ~0x2u;
        if (val & 0x8) x_int &= ~0x8u;
        x_csr = val & 0x27F5u;                           /* (masques, CLK, CS, EXTINTMASK, ROMDIS) */
        if ((old & 0x80) && !(val & 0x80)) card_cs(0);
        if (!(old & 0x80) && (val & 0x80)) card_cs(1);
        EXI0_REAL[0] = val & ~0x0C80u;                    /* (vrai materiel : jamais la carte du slot A, pas d'insertion) */
        break; }
    case 1: x_mar = val; EXI0_REAL[1] = val; break;
    case 2: x_len = val; EXI0_REAL[2] = val; break;
    case 3: if (x_csr & 0x80) { x_cr = val; if (val & 1) x_xfer(); } else EXI0_REAL[3] = val; break;
    case 4: x_data = val; EXI0_REAL[4] = val; break;
    }
    irq_restore(m);
}
/* les "interruptions" de la carte : fonctions EXI du jeu appelees depuis une interruption */
static void x_dispatch(u32 ctx) {
    if (!(H.flags & NRF_CARD)) return;
    volatile u32 *t = OS_INT_TABLE;
    for (int g = 0; g < 3 && (x_int & ((x_csr & 1 ? 2u : 0) | (x_csr & 4 ? 8u : 0))); g++) {
        if ((x_int & 0x8) && (x_csr & 0x4) && t[10] >= 0x80003100u && t[10] < 0x817F0000u) ((void (*)(u32, u32))t[10])(10, ctx);   /* EXI_0_TC */
        if ((x_int & 0x2) && (x_csr & 0x1) && t[9] >= 0x80003100u && t[9] < 0x817F0000u) ((void (*)(u32, u32))t[9])(9, ctx);       /* EXI_0_EXI */
        x_disp++;
    }
}

/* ---------- v61 : lectures en fond ----------
   Avant, une lecture de 512 Ko se faisait d'un coup, interruptions coupees (~0,5 s) : le son
   du jeu (DMA audio, DSP) n'etait plus servi et craquait. Maintenant une grosse lecture se fait
   par morceaux de 8 Ko, un a chaque fin de commande du vrai lecteur (son interruption DI) :
   entre deux morceaux, la console sert les autres interruptions (son, image), comme avec un
   vrai lecteur. A la fin : TCINT dans le faux DISR, et la fonction du jeu est appelee. */
static u32 di_orig = 0, rd_dst = 0, rd_off = 0, rd_left = 0;
static u32 rd_dst0 = 0, rd_off0 = 0, rd_len0 = 0;          /* v95 : la lecture en fond entiere (OSExec) */
static int exec_on = 0;                                    /* v95 : OSExec en cours (voir nx_read_done) */
static int exec_a = 0;                                     /* v95 : OSExec, apploader recent : le jeu lit encore tout lui-meme */
static void nx_read_done(u32 dst, u32 off, u32 len);
static int rd_on = 0, di_game_pending = 0, x_kicked = 0;
static void x_kick(void) {                               /* v65 : vraie fin de commande du lecteur -> fonctions EXI du jeu */
    if (x_kicked || rd_on || !(H.flags & NRF_REAL_IRQ)) return;   /* (une lecture en fond reveille deja souvent) */
    if (DI_REAL[7] & 1) return;
    x_kicked = 1;
    di_real_kick();
}
/* v67 : echantillon : ou le jeu a ete interrompu (OSContext : srr0 en 0x198, lr en 0x84) */
static void nr_sample(u32 ctx) {
    if (ctx < 0x80000000u || ctx >= 0x817F0000u || (ctx & 3)) return;
    u32 pc = *(volatile u32 *)(ctx + 0x198);
    H.n_samp++; H.samp_lr = *(volatile u32 *)(ctx + 0x84);
    if (pc != H.samp_pc[0]) { H.samp_pc[3] = H.samp_pc[2]; H.samp_pc[2] = H.samp_pc[1]; H.samp_pc[1] = H.samp_pc[0]; H.samp_pc[0] = pc; }
    volatile unsigned short *dsp = (volatile unsigned short *)0xCC005000u;
    H.dsp_csr = dsp[5]; H.dsp_mbox = (u32)dsp[0] << 16 | dsp[2];     /* (boites : vers le DSP haut, depuis le DSP haut ; jamais 5006 : sa lecture vide la boite) */
}
static void di_hook(u32 irq, u32 ctx) {
    int chain = di_game_pending;
    if (rd_on) {
        volatile u32 *r = DI_REAL;
        u32 dt = (tbl() - kick_tb) / 41u;                /* (us depuis le reveil) */
        H.kick_us += dt; if (dt > H.kick_max_us) H.kick_max_us = dt;
        r[0] = (r[0] & 0x2Au) | 0x54u;                   /* (vraie fin : acquittee ici, le jeu n'en sait rien) */
        u32 t0 = tbl();
        int err = 0, k = 0;
        do {                                             /* v88 : des morceaux de 2 Ko tant qu'on reste sous ~1,5 ms */
            u32 n = rd_left > RD_CHUNK ? RD_CHUNK : rd_left;
            H.n_chunks++;
            if (nr_read((void *)rd_dst, rd_off, n)) { err = 1; nr_log_add(di[2], rd_off, n, 1 | (H.err & 0xFF) << 24); break; }
            cache_flush(rd_dst, n);
            rd_dst += n; rd_off += n; rd_left -= n;
            di[5] += n; di[6] -= n;
        } while (rd_left && ++k < 8 && tbl() - t0 < RD_BUDGET);   /* (v94 : 16 Ko au plus : la carte est plus rapide en DMA / 32 MHz) */
        if (err) { rd_on = 0; di[0] |= 0x04; di[7] &= ~1u; }   /* DEINT : erreur, le jeu redemande */
        else {
            if (rd_left) { di_real_kick(); x_dispatch(ctx); return; }   /* (morceau suivant a la prochaine fin) */
            rd_on = 0; di[0] |= 0x10; di[7] &= ~1u;      /* TCINT : lecture terminee */
            nx_read_done(rd_dst0, rd_off0, rd_len0);
        }
        chain = 1;
    }
    if (!chain) DI_REAL[0] = (DI_REAL[0] & 0x2Au) | 0x54u;   /* (reveil pour la carte : le jeu n'attend rien du lecteur) */
    di_game_pending = 0; x_kicked = 0;
    x_dispatch(ctx);
    if (chain && di_orig) ((void (*)(u32, u32))di_orig)(irq, ctx);
    alive_tick();
    if (!H.vi_count && !rd_on && !di_game_pending && (H.flags & NRF_REAL_IRQ) && H.n_samp < 2000000u) {   /* v67 : pas encore d'image : on regarde ou en est le jeu */
        nr_sample(ctx);
        if (!(DI_REAL[7] & 1)) { x_kicked = 1; di_real_kick(); }
    }
}
static int nr_game_fn(u32 a) { return a >= 0x80003100u && a < 0x817F0000u; }
static void hook1(int idx, u32 *orig, u32 fn) {
    volatile u32 *t = OS_INT_TABLE;
    u32 v = t[idx];
    if (v && v != fn && nr_game_fn(v)) {
        *orig = v; t[idx] = fn; H.hook_mask |= 1u << idx;
        if (idx == 21 && (exec_on || exec_a)) { exec_on = exec_a = 0; nr_log_add(0xEF000000u, v, H.tramp_n, 0); nr_log_flush(); }   /* v95 : le nouveau programme a pris la main */
    }
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
        static const int ex[5] = { 2, 3, 5, 6, 1 };      /* v68 : + 1 = machine check (acces materiel invalide) */
        for (int i = 0; i < 5; i++) if (e[ex[i]] != (u32)exc_entry && nr_game_fn(e[ex[i]])) e[ex[i]] = (u32)exc_entry;
    }
}

/* ======================= v95 : OSExec (programme suivant du disque) =======================
   Les disques a plusieurs programmes (Zelda Collector's Edition : jeux, film) relancent la console
   sur un autre programme du disque : le jeu lit le code de redemarrage de l'apploader en 0x81300000
   et y saute ; ce code lit lui-meme le disque (registres du lecteur, sans la bibliotheque du jeu),
   puis le nouveau programme. Ici : des que ce code est lu, ses acces au lecteur sont branches au faux
   lecteur (memes aiguillages que GC Station, ecrits ici), et tout ce qui est lu ensuite aussi
   (le nouveau programme arrive directement a sa place), jusqu'a ce que le nouveau programme ait
   installe sa propre fonction d'interruption du lecteur. Lectures sans attente pendant ce temps. */
static u32 nx_w(u32 *w, int n, u32 v) { w[n] = v; return n + 1; }
#define PD(op, d, a, imm) ((u32)(op) << 26 | (u32)(d) << 21 | (u32)(a) << 16 | ((u32)(imm) & 0xFFFFu))
#define PRLW(op, s_, a, sh, mb, me) ((u32)(op) << 26 | (u32)(s_) << 21 | (u32)(a) << 16 | (u32)(sh) << 11 | (u32)(mb) << 6 | (u32)(me) << 1)
static u32 nx_slot(u32 r) { return r == 0 ? 0x08 : 0x0C + (r - 2) * 4; }
/* meme aiguillage que GC Station (natif.c, na_tramp), pour un acces au lecteur (registre reg) */
static int nx_tramp(u32 *c, u32 at, u32 site, u32 ins, u32 reg) {
    u32 op = ins >> 26, rt = (ins >> 21) & 31, ra = (ins >> 16) & 31;
    int simm = (short)(ins & 0xFFFF);
    if (op != 32 && op != 33 && op != 36 && op != 37 && op != 40 && op != 44) return 0;
    if ((op == 32 || op == 33 || op == 40) && rt == 1) return 0;
    if ((op == 33 || op == 37) && (ra == 0 || ra == 1 || (op == 33 && ra == rt))) return 0;
    int load = op == 32 || op == 33 || op == 40, half = op == 40 || op == 44;
    u32 ral = reg & ~3u, k = reg & 3u;
    if (half && (k & 1)) return 0;
    u32 fl = (u32)di_load, fs = (u32)di_store;
    int n = 0;
#define NX_CALL(fn) do { n = nx_w(c, n, PD(15, 12, 0, (fn) >> 16)); n = nx_w(c, n, PD(24, 12, 12, (fn) & 0xFFFF)); \
                         n = nx_w(c, n, 0x7D8903A6u); n = nx_w(c, n, 0x4E800421u); } while (0)
    n = nx_w(c, n, PD(37, 1, 1, -0x100));
    n = nx_w(c, n, PD(36, 0, 1, 0x08));
    n = nx_w(c, n, PD(47, 2, 1, 0x0C));
    n = nx_w(c, n, 0x7C0802A6u); n = nx_w(c, n, PD(36, 0, 1, 0x88));
    n = nx_w(c, n, 0x7C000026u); n = nx_w(c, n, PD(36, 0, 1, 0x8C));
    n = nx_w(c, n, 0x7C0902A6u); n = nx_w(c, n, PD(36, 0, 1, 0x90));
    n = nx_w(c, n, 0x7C0102A6u); n = nx_w(c, n, PD(36, 0, 1, 0x94));
    if (load) {
        n = nx_w(c, n, PD(14, 3, 0, ral));
        NX_CALL(fl);
        if (half) n = nx_w(c, n, PRLW(21, 3, 3, k ? 0 : 16, 16, 31));
        n = nx_w(c, n, PD(36, 3, 1, nx_slot(rt)));
    } else {
        if (half) {
            n = nx_w(c, n, PD(14, 3, 0, ral));
            NX_CALL(fl);
            n = nx_w(c, n, rt == 1 ? PD(14, 4, 1, 0x100) : PD(32, 4, 1, nx_slot(rt)));
            n = nx_w(c, n, PRLW(20, 4, 3, k ? 0 : 16, k ? 16 : 0, k ? 31 : 15));
            n = nx_w(c, n, 0x7C641B78u);
        } else n = nx_w(c, n, rt == 1 ? PD(14, 4, 1, 0x100) : PD(32, 4, 1, nx_slot(rt)));
        n = nx_w(c, n, PD(14, 3, 0, ral));
        NX_CALL(fs);
    }
#undef NX_CALL
    n = nx_w(c, n, PD(32, 0, 1, 0x94)); n = nx_w(c, n, 0x7C0103A6u);
    n = nx_w(c, n, PD(32, 0, 1, 0x90)); n = nx_w(c, n, 0x7C0903A6u);
    n = nx_w(c, n, PD(32, 0, 1, 0x8C)); n = nx_w(c, n, 0x7C0FF120u);
    n = nx_w(c, n, PD(32, 0, 1, 0x88)); n = nx_w(c, n, 0x7C0803A6u);
    n = nx_w(c, n, PD(32, 0, 1, 0x08));
    n = nx_w(c, n, PD(46, 2, 1, 0x0C));
    n = nx_w(c, n, PD(14, 1, 1, 0x100));
    if (op == 33 || op == 37) n = nx_w(c, n, PD(14, ra, ra, simm));
    u32 back = site + 4, here = at + (u32)n * 4;
    n = nx_w(c, n, 0x48000000u | ((back - here) & 0x03FFFFFCu));
    return n;
}
static void nx_sync(u32 a, u32 len) {                     /* code ecrit : cache de donnees -> memoire, puis cache d'instructions */
    for (u32 x = a & ~31u; x < a + len; x += 32) __asm__ volatile("dcbst 0,%0; sync; icbi 0,%0" :: "r"(x) : "memory");
    __asm__ volatile("sync; isync" ::: "memory");
}
/* cherche les acces au lecteur dans [a, a + len) (code deja a sa place) et les branche ; meme suivi
   des registres que GC Station (lis 0xCC00, addi, ori ; oublie aux sauts) */
static void nx_patch2(u32 a, u32 fin, u32 len) {        /* a = ou est le code maintenant, fin = ou il tournera */
    if (a < 0x80003000u || a >= 0x817F0000u || (a & 3) || (fin & 3) || len < 8) return;
    if (fin < 0x80003000u || fin >= 0x817F0000u) return;
    if (a + len > 0x817F0000u) len = 0x817F0000u - a;
    volatile u32 *t = (volatile u32 *)a;
    u32 val[32], known = 0, end = (H.tramp_at ? 0x817FFF00u : 0);
    for (u32 o = 0; o + 4 <= len; o += 4) {
        u32 ins = t[o >> 2], op = ins >> 26, rd = (ins >> 21) & 31, ra = (ins >> 16) & 31;
        int simm = (short)(ins & 0xFFFF);
        u32 uimm = ins & 0xFFFF;
        if (op == 15) { if (!ra) { val[rd] = uimm << 16; known |= 1u << rd; } else if (known & (1u << ra)) { val[rd] = val[ra] + (uimm << 16); known |= 1u << rd; } else known &= ~(1u << rd); continue; }
        if (op == 14) { if (!ra) { val[rd] = (u32)simm; known |= 1u << rd; } else if (known & (1u << ra)) { val[rd] = val[ra] + (u32)simm; known |= 1u << rd; } else known &= ~(1u << rd); continue; }
        if (op == 24) { if (known & (1u << rd)) { val[ra] = val[rd] | uimm; known |= 1u << ra; } else known &= ~(1u << ra); continue; }
        if (op == 32 || op == 33 || op == 36 || op == 37 || op == 40 || op == 44) {
            int store = op >= 36 && op != 40;
            if (ra && (known & (1u << ra))) {
                u32 ea = val[ra] + (u32)simm;
                if (ea >= 0xCC006000u && ea < 0xCC006040u && H.tramp_at && H.tramp_at + 64 * 4 <= end) {
                    u32 site = fin + o, at = H.tramp_at;
                    int d = (int)(at - site);
                    int n = d < 0x02000000 && d >= -0x02000000 ? nx_tramp((u32 *)at, at, site, ins, ea - 0xCC006000u) : 0;
                    if (n) {
                        nx_sync(at, (u32)n * 4);
                        t[o >> 2] = 0x48000000u | ((u32)d & 0x03FFFFFCu);
                        nx_sync(a + o, 4);
                        H.tramp_at = (at + (u32)n * 4 + 31) & ~31u;
                        H.tramp_n++;
                    }
                }
                if (op == 33 || op == 37) val[ra] = ea;
            }
            if (!store) known &= ~(1u << rd);
            continue;
        }
        if (op == 16 || op == 18 || op == 19) {
            u32 lk = ins & 1, bo = (ins >> 21) & 31, xo = (ins >> 1) & 0x3FF;
            int uncond = op == 18 || ((op == 16 || op == 19) && (bo & 0x14) == 0x14);
            if (op == 19 && xo != 16 && xo != 528) continue;
            if (lk) known &= ~0x00001FF9u; else if (uncond) known = 0;
            continue;
        }
        if (op == 31) known &= ~((1u << rd) | (1u << ra));
        else if (op == 7 || op == 8 || op == 12 || op == 13) known &= ~(1u << rd);
        else if (op == 46) known = 0;
        else if ((op >= 20 && op <= 29) || op == 34 || op == 35 || op == 41 || op == 42 || op == 43) known &= ~((1u << ra) | (1u << rd));
    }
}
static void nx_patch(u32 a, u32 len) { nx_patch2(a, a, len); }
static u32 vi_orig, si_orig, ai_orig, dsp_orig, pet_orig, pef_orig;
/* le programme suivant : son en-tete .dol (sections : position dans l'ISO, adresse finale, taille). Ses
   sections peuvent etre lues ailleurs puis recopiees (apploader recent) : on branche avec l'adresse finale. */
static struct { u32 off, so[18], sa[18], ss[18]; int ok; } xdol;
static u32 rbe(u32 a) { return *(volatile u32 *)a; }
static int nx_dol(u32 dst, u32 off, u32 len) {
    if (len < 0x100 || (dst & 3)) return 0;
    u32 entry = rbe(dst + 0xE0);
    int text = 0, n = 0;
    for (int i = 0; i < 18; i++) {
        u32 so = rbe(dst + i * 4), sa = rbe(dst + 0x48 + i * 4), ss = rbe(dst + 0x90 + i * 4);
        if (!ss) continue;
        if (so < 0x100 || ss > 0x01800000u || sa < 0x80000000u || sa + ss > 0x81800000u || (sa & 3)) return 0;
        if (i < 7 && entry >= sa && entry < sa + ss) text = 1;
        n++;
    }
    if (!text || !n) return 0;
    xdol.off = off;
    for (int i = 0; i < 18; i++) { xdol.so[i] = rbe(dst + i * 4); xdol.sa[i] = rbe(dst + 0x48 + i * 4); xdol.ss[i] = i < 7 ? rbe(dst + 0x90 + i * 4) : 0; }
    xdol.ok = 1;
    H.exec_last = entry;
    return 1;
}
/* une lecture terminee (dst = memoire, off / len = ISO) : le code de redemarrage, puis tout ce qu'il charge */
static void nx_read_done(u32 dst, u32 off, u32 len) {
    if (!exec_on && !exec_a) {
        if (off == 0x2440 && len == 0x20 && H.vi_count) {   /* OSExec commence : le jeu relit l'en-tete de l'apploader */
            H.exec_n++; H.exec_last = off; xdol.ok = 0;
            if (!H.reboot_len) { exec_a = 1; nr_log_add(0xED000000u, off, len, H.tramp_n); nr_log_flush(); }
            return;
        }
        if (!H.reboot_len || off != H.reboot_off || dst != 0x81300000u || len < H.reboot_len) return;
        exec_on = 1; H.exec_last = 0x81300000u; xdol.ok = 0;   /* ancien apploader : code de redemarrage en 0x81300000 */
        di_orig = vi_orig = si_orig = ai_orig = dsp_orig = pet_orig = pef_orig = 0;   /* (le jeu va disparaitre : plus de chainage) */
        nr_log_add(0xEE000000u, off, len, H.tramp_n);
        nx_patch(dst, len);
        nr_log_flush();
        return;
    }
    if (nx_dol(dst, off, len)) return;                    /* en-tete du programme suivant */
    if (xdol.ok) {                                        /* une section (ou un morceau) : branchee a son adresse finale */
        for (int i = 0; i < 7; i++) {
            u32 so = xdol.off + xdol.so[i], ss = xdol.ss[i];   /* (position dans l'ISO = debut du .dol + position dans le .dol) */
            if (!ss || off >= so + ss || off + len <= so) continue;
            u32 a0 = off > so ? off : so, a1 = off + len < so + ss ? off + len : so + ss;
            nx_patch2(dst + (a0 - off), xdol.sa[i] + (a0 - so), a1 - a0);
        }
        return;
    }
    if (exec_on) nx_patch(dst & ~3u, len);                /* (ancien apploader : ce qu'il charge arrive a sa place) */
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
        if (len > RD_CHUNK && di_orig && (H.flags & NRF_REAL_IRQ) && !exec_on) {   /* v61 : grosse lecture : en fond */
            rd_dst = dst; rd_off = off; rd_left = len; rd_on = 1;
            rd_dst0 = dst; rd_off0 = off; rd_len0 = len;
            H.n_async++;
            info |= di_real_kick() | 0x10;                 /* (le lecteur reste "occupe" : DICR bit 0) */
            nr_log(off, len, info | (di[0] & 0xFF) << 8);
            return;
        }
        if (len && nr_read((void *)dst, off, len)) { di[0] |= 0x04; info |= 1; goto done; }   /* DEINT : erreur */
        cache_flush(dst, len);
        di[5] += len; di[6] = 0;
        nx_read_done(dst, off, len);                     /* v95 : OSExec */
    } else if (cmd == 0x12) {                            /* infos du lecteur */
        u8 *o = (u8 *)dst;
        int real = di_real_inq();                        /* v66 : la vraie reponse du lecteur (date, revision) */
        for (u32 i = 0; i < len && i < 0x20; i++) o[i] = real ? ((volatile u8 *)di_scratch)[i] : 0;
        if (!real && len >= 8) { o[4] = 0x20; o[5] = 0x02; o[6] = 0x08; o[7] = 0x23; }   /* (une revision connue) */
        cache_flush(dst, len);
        di[5] += len; di[6] = 0;
    }
    else if (cmd == 0xE1) { H.n_e1++; st_cmd(di[2], di[3] << 2, di[4]); }          /* v65 : musique du disque */
    else if (cmd == 0xE2) { H.n_e2++; di[8] = st_status(di[2]); }
    else if (cmd == 0xAB) H.n_ab++;
    else if (cmd != 0xE0) H.n_misc++;
    /* AB (chercher), E0 (erreur : 0), E1-E4 (audio), autres : rien a faire */
    di[0] |= 0x10;                                       /* TCINT : termine */
done:
    di[7] &= ~1u;
    if ((H.flags & NRF_REAL_IRQ) && !exec_on) { di_game_pending = 1; info |= di_real_kick(); }   /* (v95 : pendant OSExec, personne n'attend d'interruption) */
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

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

static u32 di_real_kick(void) {                          /* vraie commande "inquiry" : sa fin reveille le jeu */
    volatile u32 *r = DI_REAL;
    for (u32 i = 0; i < 3000000u && (r[7] & 1); i++) ;  /* v54 : le vrai lecteur est occupe : on attend un peu */
    if (r[7] & 1) { H.nskip++; return 2; }
    H.nkick++;
    r[2] = 0x12000000u; r[3] = 0; r[4] = 0x20;
    r[5] = (u32)di_scratch & 0x01FFFFFFu; r[6] = 0x20;
    r[7] = 3;                                           /* DMA + demarrer */
    return 4 | (r[0] & 0xFF) << 16;
}

static u32 log_ring[NATRES_LOGN][4];
static u8 logbuf[512] __attribute__((aligned(32)));
static void nr_log(u32 off, u32 len, u32 info) {
    u32 *e = log_ring[H.nlog % NATRES_LOGN];
    e[0] = di[2]; e[1] = off; e[2] = len; e[3] = info | (H.err & 0xFF) << 24;
    H.nlog++;
    int err = info & 1;
    if (!H.log_sec || !di_ready) return;
    if (!(err || H.ncmd <= 40 || (H.ncmd & 63) == 0)) return;   /* (debut du jeu, erreurs, puis de temps en temps) */
    const u8 *p = (const u8 *)&H;
    for (u32 i = 0; i < sizeof(natres_hdr_t); i++) logbuf[i] = p[i];
    for (u32 k = 0; k < NATRES_LOGN; k++) {                     /* le plus ancien d'abord */
        u32 j = (H.nlog + k) % NATRES_LOGN, *d = (u32 *)(logbuf + sizeof(natres_hdr_t) + k * 16);
        for (int w = 0; w < 4; w++) d[w] = H.nlog >= NATRES_LOGN || j < H.nlog ? log_ring[j][w] : 0;
    }
    nr_write_sd(H.log_sec, logbuf);
}
static void di_exec(void) {
    u32 info = 0;
    if (!di_ready) { di_ready = nr_init() == 0; if (!di_ready) info |= 8; }   /* 1er acces du jeu : on demarre la carte SD */
    u32 cmd = di[2] >> 24, len = di[6], dst = di[5] | 0x80000000u;
    H.ncmd++; H.last_cmd = di[2]; H.last_off = di[3] << 2; H.last_len = len;
    di[8] = 0;
    if (cmd == 0xA8) {
        u32 off = (di[2] & 0xC0) == 0x40 ? 0 : di[3] << 2;   /* A8000040 : ID du disque (debut de l'ISO) */
        if ((di[2] & 0xC0) == 0x40 && len > 0x20) len = 0x20;
        H.last_off = off; H.last_len = len;
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
    /* AB (chercher), E0 (erreur : 0), E1-E4 (audio), autres : rien a faire */
    di[0] |= 0x10;                                       /* TCINT : termine */
done:
    di[7] &= ~1u;
    if (H.flags & NRF_REAL_IRQ) info |= di_real_kick();
    nr_log(H.last_off, H.last_len, info | (di[0] & 0xFF) << 8);
}

static u32 di_load(u32 reg) {
    u32 i = (reg & 0x3F) >> 2;
    if (i == 1) return di[1] & 0x02;                      /* couvercle ferme, pas d'interruption */
    if (i == 9) return DI_REAL[9];                        /* DICFG : celui du vrai lecteur */
    return i < 10 ? di[i] : 0;
}

static void di_store(u32 reg, u32 val) {
    u32 i = (reg & 0x3F) >> 2;
    if (i == 0) {                                        /* DISR : 1 = effacer l'interruption ; masques */
        di[0] = (di[0] & ~(val & 0x54u) & 0x54u) | (val & 0x2Au);
        u32 rs = DI_REAL[0];
        H.last_real_disr = rs;
        if (rs & 0x10) H.nack_tc++;
        DI_REAL[0] = (val & 0x2Au) | 0x54u;              /* (vrai lecteur : memes masques, tout acquitte) */
        return;
    }
    if (i == 1) { di[1] = val & 0x02; return; }
    if (i == 7) {
        di[7] = val;
        if (val & 1) { u32 m = irq_off(); di_exec(); irq_restore(m); }
        return;
    }
    if (i < 10) di[i] = val;
}

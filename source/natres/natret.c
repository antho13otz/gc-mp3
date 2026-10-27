/* GC Station v187 : retour a GC Station en quittant une appli (.dol homebrew).
   Petit programme range en 0x80001800 avant de lancer une appli, avec la signature "STUBHAXX" en 0x80001804 :
   c'est la convention des applis faites avec libogc (exit(), fin de main, bouton Reset dans beaucoup d'applis) :
   elles sautent en 0x80001800 si elles la trouvent. Ici : tout est arrete, la carte SD est redemarree (mode SPI,
   comme Pulse), puis le .dol de GC Station est relu d'apres la liste de ses morceaux sur la carte (preparee par
   GC Station au lancement de l'appli) et lance. Sans liste ou en cas d'erreur : redemarrage de la console.
   Tout doit tenir entre 0x80001800 et 0x80002C00 (tools/natret.ld) ; pile sous 0x80002FF0. */
typedef unsigned int u32;
typedef unsigned char u8;

typedef struct { u32 iso, sd, cnt; } nrt_ext_t;            /* morceau : secteurs du fichier -> secteurs de la carte */
#define NRT_EXTMAX 48
typedef struct {
    u32 jump;                                              /* b nrt_start */
    u32 stub, haxx;                                        /* "STUB" "HAXX" : signature lue par libogc */
    u32 magic;                                             /* "GCRT" */
    u32 chan, dev, freq;                                   /* (GC Station) bus EXI de la carte SD, vitesse (4 = 16 MHz) */
    u32 n, size;                                           /* (GC Station) morceaux du .dol de GC Station, taille */
    u32 err, stage, sdhc;                                  /* (diagnostic) */
    nrt_ext_t ext[NRT_EXTMAX];                             /* (GC Station) */
} nrt_hdr_t;
#define NRT_MAGIC 0x47435254u

void nrt_start(void);
__attribute__((section(".res_head"), used)) nrt_hdr_t nrt_hdr = {
    0x48000000u | (sizeof(nrt_hdr_t) & 0x03FFFFFCu),       /* b vers la suite de l'en-tete (nrt_start est juste apres) */
    0x53545542u, 0x48415858u, NRT_MAGIC, 0, 0, 4, 0, 0, 0, 0, 0, { { 0, 0, 0 } }
};
#define H nrt_hdr

static void hot_reset(void) {
    volatile u32 *pi = (volatile u32 *)0xCC003000u;
    pi[9] = 3; pi[9] = 0;
    for (;;) ;
}

/* ---------- bus EXI et carte SD (SPI) ---------- */
static volatile u32 *exi;
static u32 freq;
static int ex_wait(void) { for (u32 i = 0; i < 1000000u; i++) if (!(exi[3] & 1)) return 0; return -1; }
static void ex_sel(int cs) { u32 v = (exi[0] & 0x405) | (freq << 4); if (cs) v |= 0x80u << H.dev; exi[0] = v; }
static void ex_desel(void) { exi[0] = exi[0] & 0x405; }
static void ex_put(u32 v, int n) { exi[4] = v; exi[3] = ((u32)(n - 1) << 4) | (1u << 2) | 1u; ex_wait(); }
static u32 ex_get(int n) { exi[4] = 0xFFFFFFFFu; exi[3] = ((u32)(n - 1) << 4) | 1u; ex_wait(); return exi[4]; }
static u32 ex_get8(void) { return ex_get(1) >> 24; }
static void sd_end(void) { ex_desel(); ex_sel(0); ex_put(0xFF000000u, 1); ex_desel(); }
static u32 sd_cmd(u32 cmd, u32 arg, u32 crc) {
    ex_sel(1);
    ex_put(0xFF000000u, 1);
    ex_put(((0x40u | cmd) << 24) | (arg >> 8), 4);
    ex_put(((arg & 0xFFu) << 24) | (crc << 16), 2);
    u32 r = 0xFF;
    for (int i = 0; i < 16; i++) { r = ex_get8(); if (!(r & 0x80)) break; }
    return r;
}
static int sd_init(void) {
    exi = (volatile u32 *)(0xCC006800u + H.chan * 0x14u);
    H.sdhc = 0; H.stage = 1;
    freq = 0;                                              /* 1 MHz pour demarrer la carte */
    int found = 0;
    for (int t = 0; t < 2 && !found; t++) {
        if (t == 1) { if (H.chan != 0) break; H.dev = H.dev == 0 ? 2 : 0; }
        ex_sel(0);
        for (int i = 0; i < 10; i++) ex_put(0xFF000000u, 1);
        ex_desel();
        for (int k = 0; k < 8 && !found; k++) { u32 r = sd_cmd(0, 0, 0x95); sd_end(); if (r == 0x01) found = 1; }
    }
    if (!found) return -1;
    H.stage = 2;
    int v2 = 0;
    u32 r = sd_cmd(8, 0x1AA, 0x87);
    if (r == 0x01) v2 = (ex_get(4) & 0xFFF) == 0x1AA;
    sd_end();
    H.stage = 3;
    int ok = 0;
    for (u32 i = 0; i < 20000u && !ok; i++) {
        sd_cmd(55, 0, 0x01); sd_end();
        r = sd_cmd(41, v2 ? 0x40000000u : 0, 0x01); sd_end();
        if (r == 0x00) ok = 1;
        else if (r & 0x04) { r = sd_cmd(1, 0, 0x01); sd_end(); if (r == 0x00) ok = 1; }
    }
    if (!ok) return -1;
    H.stage = 4;
    if (v2) { r = sd_cmd(58, 0, 0x01); u32 ocr = ex_get(4); sd_end(); if (r == 0x00 && (ocr & 0x40000000u)) H.sdhc = 1; }
    if (!H.sdhc) { sd_cmd(16, 512, 0x01); sd_end(); }
    freq = H.freq > 4 ? 4 : H.freq;                       /* (16 MHz au plus : pas de verification du CRC ici) */
    H.stage = 5;
    return 0;
}
static int sd_block(u8 *o) {                               /* jeton FE, 512 octets, CRC */
    u32 t = 0xFF;
    for (u32 i = 0; i < 400000u; i++) { t = ex_get8(); if (t != 0xFF) break; }
    if (t != 0xFE) return -1;
    for (int i = 0; i < 128; i++) {
        u32 w = ex_get(4);
        o[i * 4] = (u8)(w >> 24); o[i * 4 + 1] = (u8)(w >> 16); o[i * 4 + 2] = (u8)(w >> 8); o[i * 4 + 3] = (u8)w;
    }
    ex_get(2);
    return 0;
}
static int sd_read(u32 sector, u32 count, u8 *o) {        /* CMD18 : plusieurs secteurs a la suite, puis CMD12 */
    u32 r = sd_cmd(18, H.sdhc ? sector : sector << 9, 0x01);
    if (r != 0) { sd_end(); return -1; }
    int ok = 1;
    for (u32 s = 0; s < count && ok; s++, o += 512) if (sd_block(o)) ok = 0;
    ex_put(0x4C000000u, 4); ex_put(0x00010000u, 2);
    ex_get8();
    for (int i = 0; i < 16; i++) { r = ex_get8(); if (!(r & 0x80)) break; }
    for (u32 i = 0; i < 400000u; i++) if (ex_get8() == 0xFF) break;
    sd_end();
    return ok ? 0 : -1;
}

/* ---------- le .dol de GC Station : lecture d'apres ses morceaux ---------- */
static u8 secbuf[512] __attribute__((aligned(32)));
static int map(u32 fsec, u32 *sd, u32 *run) {
    for (u32 i = 0; i < H.n && i < NRT_EXTMAX; i++) {
        const nrt_ext_t *e = &H.ext[i];
        if (fsec >= e->iso && fsec - e->iso < e->cnt) { *sd = e->sd + (fsec - e->iso); *run = e->cnt - (fsec - e->iso); return 0; }
    }
    return -1;
}
static int file_read(u8 *dst, u32 off, u32 len) {
    while (len) {
        u32 fsec = off >> 9, in = off & 511, sd, run;
        if (map(fsec, &sd, &run)) return -1;
        if (in == 0 && len >= 512) {                       /* secteurs entiers : directement a leur place */
            u32 n = len >> 9;
            if (n > run) n = run;
            if (n > 64) n = 64;
            int e = sd_read(sd, n, dst);
            if (e) { if (sd_init() || sd_read(sd, n, dst)) return -1; }   /* (une erreur : carte redemarree, un 2e essai) */
            dst += n << 9; off += n << 9; len -= n << 9;
        } else {
            if (sd_read(sd, 1, secbuf) && (sd_init() || sd_read(sd, 1, secbuf))) return -1;
            u32 n = 512 - in;
            if (n > len) n = len;
            for (u32 i = 0; i < n; i++) dst[i] = secbuf[in + i];
            dst += n; off += n; len -= n;
        }
    }
    return 0;
}
static void cache_sync(u32 a, u32 n) {
    for (u32 p = a & ~31u; p < a + n; p += 32) __asm__ volatile("dcbf 0,%0; icbi 0,%0" :: "r"(p) : "memory");
}

void nrt_main(void) __attribute__((used));
void nrt_main(void) {
    volatile u32 *pi = (volatile u32 *)0xCC003000u;
    volatile unsigned short *dsp = (volatile unsigned short *)0xCC005000u;
    pi[1] = 0;                                             /* (plus aucune interruption) */
    dsp[0x36 / 2] = 0;                                     /* DMA du son arrete */
    dsp[0x0A / 2] = (unsigned short)((dsp[0x0A / 2] & ~0x0150u) | 0x00ACu);   /* DSP : arret, masques coupes, acquitte */
    *(volatile u32 *)0xCC006C00u &= ~1u;                   /* son du disque arrete */
    *(volatile unsigned short *)0xCC000002u = 0;           /* processeur graphique : plus de lecture de la file */
    pi[6] = 1;
    for (volatile u32 i = 0; i < 4000u; i++) { }
    pi[6] = 0;
    for (volatile u32 i = 0; i < 1000u; i++) { }
    if (H.magic != NRT_MAGIC || !H.n || H.n > NRT_EXTMAX || H.size < 0x100) hot_reset();
    if (sd_init()) { H.err = 1; hot_reset(); }
    static u8 dh[256] __attribute__((aligned(32)));
    if (file_read(dh, 0, 256)) { H.err = 2; hot_reset(); }
    const u32 *w = (const u32 *)dh;
    u32 entry = w[0xE0 / 4];
    if (entry < 0x80003000u || entry >= 0x81800000u) { H.err = 3; hot_reset(); }
    for (int i = 0; i < 18; i++) {
        u32 off = w[i], addr = w[0x48 / 4 + i], sz = w[0x90 / 4 + i];
        if (!sz) continue;
        if (addr < 0x80003000u || addr + sz > 0x81800000u || off + sz > H.size) { H.err = 4; hot_reset(); }
        if (file_read((u8 *)addr, off, sz)) { H.err = 5; hot_reset(); }
        cache_sync(addr, sz);
    }
    u32 bss = w[0xD8 / 4], bsz = w[0xDC / 4];              /* (BSS : remise a zero par le programme lui-meme ; rien ici) */
    (void)bss; (void)bsz;
    __asm__ volatile("sync; isync" ::: "memory");
    ((void (*)(void))entry)();
    hot_reset();
}
/* entree : interruptions coupees, une pile a nous (sous 0x80003000 : libre, l'appli est abandonnee) */
__asm__(".pushsection .text.nrt_start,\"ax\"\n.globl nrt_start\nnrt_start:\n"
        "  mfmsr 3\n  rlwinm 3,3,0,17,15\n  mtmsr 3\n  isync\n"
        "  lis 1,0x8000\n  ori 1,1,0x2FF0\n  li 0,0\n  stw 0,0(1)\n  b nrt_main\n.popsection\n");

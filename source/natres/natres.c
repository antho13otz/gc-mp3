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
static void early_note(u32 a, u32 v, u32 st);           /* v166 */

/* v97 : la table des morceaux de l'ISO n'est plus dans le programme (18 Ko reserves pour 1500 morceaux) :
   GC Station la range tout en haut de la place du resident, a la taille utile ; il reste ainsi bien plus
   de place pour les aiguillages (Cocoto Kart Racer : 3 acces non branches faute de place en v95). */
#define ext_tab ((natres_ext_t *)natres_hdr.ext)
static natres_ext_t ret_tab[NATRES_RETMAX];              /* v64 : le .dol de GC Station sur la carte */
static natres_ext_t card_tab[NATRES_CARDMAX];            /* v65 : le fichier de la carte memoire virtuelle */
static natres_ext_t card2_tab[NATRES_CARDMAX];           /* v122 : celui du slot B */
static u32 si_load(u32 a);
/* v139 : acces du jeu a la page des registres DI / SI / EXI / AI (0xCC006000) pris par exception (option) : le
   resident passe alors par une 2e adresse de la meme page (0xCC406000), qui ne declenche rien */
static u32 hw_d = 0;
#define HW6(a) ((volatile u32 *)((u32)(a) + hw_d))
#define MFSPR(n) ({ u32 _v; __asm__ volatile("mfspr %0," #n : "=r"(_v)); _v; })
#define MTSPR(n, v) __asm__ volatile("mtspr " #n ",%0" :: "r"(v) : "memory")
extern u32 trap_vec[];
#define TRAP_BR (0x48000002u | ((u32)trap_vec & 0x03FFFFFCu))   /* (ba trap_vec, en adresse physique) */
static u32 trap_br = 0, trap_mmu = 0;
static void trap_fix(void);
/* a chaque entree dans le resident : un autre programme du disque a pu remettre ses BAT ou ses vecteurs */
static void __attribute__((noinline)) trap_guard(void) { if (hw_d && ((trap_mmu && MFSPR(538) != trap_br) || *(volatile u32 *)0x80000300u != TRAP_BR)) trap_fix(); }
static void si_store(u32 a, u32 val);
static u8 secbuf[512] __attribute__((aligned(32)));
static u32 sec_cached = 0xFFFFFFFFu;
static u32 ext_last = 0;
extern u8 __natres_end[];

__attribute__((section(".res_head"), used))
natres_hdr_t natres_hdr = {
    NATRES_MAGIC, NATRES_VER, (u32)nr_init, (u32)nr_read, (u32)nr_read_sd,
    2, 0, 4,                      /* SD2SP2, peripherique 0, 16 MHz */
    0, NATRES_AT + NATRES_SIZE - 256 * 12u,      /* (v139 : GC Station la place lui-meme, a la taille utile) */
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
    .card2_tab = (u32)card2_tab, .si_load = (u32)si_load, .si_store = (u32)si_store,   /* (v122) */
};
#define H natres_hdr

/* (le compilateur peut appeler ces fonctions de lui-meme) */
void *memset(void *d, int c, unsigned long n) { u8 *p = (u8 *)d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { u8 *p = (u8 *)d; const u8 *q = (const u8 *)s; while (n--) *p++ = *q++; return d; }

/* ---------- bus EXI ---------- */
static volatile u32 *exi;
static u32 freq;
/* v145 : bus EXI partage. Avec un adaptateur SD dans un port de carte (SD Gecko : canal 0 ou 1), le jeu se sert
   du meme canal pour d'autres peripheriques : horloge / SRAM (canal 0, peripherique 1), Broadband Adapter
   (canal 0, peripherique 2 : jeux en reseau local), vraie carte memoire. S'il a choisi un autre peripherique
   (CS) ou si un transfert est en cours, parler a la carte SD couperait sa commande au milieu : on attend.
   (Swiss passe par EXILock du jeu ; ici on regarde l'etat du canal lui-meme.) */
static int exi_busy(void) {
    if (!exi) return 0;
    return ((exi[0] & 0x380u) & ~(0x80u << H.dev)) || (exi[3] & 1u);
}
static u32 eb_tb = 0, eb_n = 0;
static int exi_defer(void) {                             /* 1 = attendre (au plus 1 s de suite : un bus jamais rendu -> comme avant) */
    if (H.net || !exi_busy()) { eb_n = 0; return 0; }
    u32 t; __asm__ volatile("mftb %0" : "=r"(t));
    if (!eb_n++) eb_tb = t;
    return t - eb_tb < 40500000u && eb_n < 100000u;      /* (et un nombre de fois : horloge arretee dans le banc d'essai) */
}

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
    exi = (volatile u32 *)(0xCC006800u + hw_d + H.chan * 0x14u);
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
        if (isec >= e->iso && isec - e->iso < e->cnt) { *sd = e->sd == NATRES_ZERO ? NATRES_ZERO : e->sd + (isec - e->iso); iso_run = e->cnt - (isec - e->iso); return 0; }
    }
    u32 lo = 0, hi = n;                           /* (morceaux ranges dans l'ordre de l'ISO) */
    while (lo < hi) {
        u32 mid = (lo + hi) >> 1;
        const natres_ext_t *e = &ext_tab[mid];
        if (isec < e->iso) hi = mid;
        else if (isec - e->iso >= e->cnt) lo = mid + 1;
        else { ext_last = mid; *sd = e->sd == NATRES_ZERO ? NATRES_ZERO : e->sd + (isec - e->iso); iso_run = e->cnt - (isec - e->iso); return 0; }
    }
    H.err = NR_ERANGE;
    return -1;
}

static int nr_read1(void *dst, u32 off, u32 len);
static int di_ready = 0;
static int pn_read(void *dst, u32 off, u32 len);   /* v143 : (ISO sur le reseau) */
static int pn_ready = 0;
/* v136 : corrections par jeu dans les autres executables du disque (.dol, .elf, .tgc) : GC Station a calcule
   la position dans l'ISO de chaque instruction a changer ; elle est changee quand le jeu lit cet endroit.
   Table a la suite des cheats : {mots de cheats, mots..., "FIXT", n, n x {position, valeur, taille (1-4)}} */
#define NATRES_FIXT 0x46495854u
static void fx_apply(u8 *dst, u32 off, u32 len) {
    if (!H.chts) return;
    const u32 *w = (const u32 *)H.chts;
    const u32 *f = w + 1 + w[0];
    if (f[0] != NATRES_FIXT) return;
    u32 n = f[1];
    f += 2;
    for (u32 i = 0; i < n; i++, f += 3) {
        u32 iso = f[0], val = f[1], sz = f[2] & 7;
        if (iso + sz <= off || iso >= off + len) continue;
        for (u32 k = 0; k < sz; k++) {
            u32 o = iso + k;
            if (o >= off && o < off + len) { dst[o - off] = (u8)(val >> (8 * (sz - 1 - k))); H.si_fixes += 0x10000u; }
        }
    }
}
static int nr_read(void *dst, u32 off, u32 len) {    /* v54 : une erreur de la carte -> on la redemarre et on reessaie */
    for (int t = 0; t < 3; t++) {
        if (nr_read1(dst, off, len) == 0) { if (t) H.nretry++; fx_apply((u8 *)dst, off, len); return 0; }
        if (H.err == NR_ERANGE) break;
        H.nrderr++;
        if (x_fast && x_crcbad) x_fast_off = 1;           /* v94 : CRC faux a 32 MHz : retour a 16 MHz */
        x_crcbad = 0;
        sec_cached = 0xFFFFFFFFu;
        if (H.net) { pn_ready = 0; continue; }          /* v143 : (reseau : on redemarre le BBA) */
        di_ready = nr_init() == 0;
    }
    return -1;
}
static int nr_read1(void *dst, u32 off, u32 len) {
    if (H.net) return pn_read(dst, off, len);           /* v143 : ISO sur le reseau */
    u8 *o = (u8 *)dst;
    while (len) {
        u32 isec = off >> 9, in = off & 511, n = 512 - in;
        if (n > len) n = len;
        u32 sd;
        if (iso_to_sd(isec, &sd)) return -1;
        u32 run = len >> 9;
        if (sd == NATRES_ZERO) {                          /* v121 : bloc absent d'un .ciso : des zeros */
            u32 z = iso_run * 512u - in;
            if (z < n) z = n;
            if (z > len) z = len;
            for (u32 i = 0; i < z; i++) o[i] = 0;
            n = z;
        } else if (in == 0 && run >= 2 && iso_run >= 2) {        /* v88 : plusieurs secteurs entiers a la suite : une commande */
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
#define DI_REAL ((volatile u32 *)(0xCC006000u + hw_d))
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
/* v133 : comment le jeu est reveille a la fin d'une commande. 0 = par une vraie commande du lecteur (rapide :
   ~0,1 ms) ; 1 = sans le lecteur (pas de lecteur, ou il ne repond plus) : la fin est donnee au jeu depuis ses
   propres interruptions (image, son, dessin, manettes : quelques ms) */
static int wk_tick = -1;
static int wake_tick(void) { if (wk_tick < 0) wk_tick = !(H.flags & NRF_REAL_IRQ); return wk_tick; }
static u32 di_real_kick(void) {                          /* vraie commande "inquiry" : sa fin reveille le jeu */
    volatile u32 *r = DI_REAL;
    if (wake_tick()) return 4;                           /* (v133 : sans le lecteur : la fin viendra a la prochaine interruption du jeu) */
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
    if (!(H.flags & NRF_REAL_IRQ) || wake_tick()) return 0;
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
static void vi_snap(void) {                              /* v99 : etat de l'ecran (journal) */
    volatile unsigned short *v = (volatile unsigned short *)0xCC002000u;
    u32 now = (u32)v[1] << 16 | (v[0x6C / 2] & 1u);
    if (H.vi_now && ((now ^ H.vi_now) & ~0x00010000u)) { H.vi_chg++; H.vi_chg_at = H.vi_count; }
    H.vi_now = now;
    H.vi_dtv = (u32)v[0x6E / 2] << 16 | v[26];           /* (v111 : bas = DI1, interruption de la 1re ligne) */
}
static void nr_log_flush(void) {
    H.flush_tb = tbl();
    vi_snap();
    if (!H.log_sec) return;
    if (exi_defer()) return;                             /* v145 : (le jeu se sert du bus : ecrit la prochaine fois) */
    if (!di_ready) {                                     /* v161 : jeu bloque avant sa 1re lecture du disque : on demarre la carte */
        if (H.net) return;                               /* (pour ecrire quand meme le journal ; avant : rien, et l'ancien restait) */
        di_ready = nr_init() == 0;
        if (!di_ready) return;
    }
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
/* ======================= v143 : Pulse par le reseau (option, experimental) =======================
   L'ISO n'est pas sur la carte SD : elle est lue sur une autre GC Station (ou un PC, tools/pulse-serveur.py)
   par le Broadband Adapter (EXI canal 0, peripherique 2). Pilote minimal ecrit ici : registres du
   controleur reseau (commandes EXI de 2 octets pour ses registres a lui, de 4 octets pour ceux du
   controleur Ethernet), anneau de reception dans sa memoire (pages de 256 octets, descripteur de 4 octets),
   envoi par sa file. Par-dessus : ARP (repondre, trouver le serveur), IPv4, UDP. Protocole a nous :
   demande "PNR1" {numero, fichier, position, longueur <= 2048} ; reponses "PNA1" {numero, position,
   longueur <= 1024, donnees}. Une demande perdue est refaite apres 25 ms. */
#define PN_NET ((const natres_net_t *)H.net)
#define BB_NCRA 0x00
#define BB_NCRB 0x01
#define BB_IMR  0x08
#define BB_IR   0x09
#define BB_BP   0x0A
#define BB_TLBP 0x0C
#define BB_RWP  0x16
#define BB_RRP  0x18
#define BB_RHBP 0x1A
#define BB_PAR  0x20
#define BB_NWAYS 0x31
#define BB_GCA  0x32
#define BB_TXFIFOCNT 0x3E
#define BB_WRTXFIFOD 0x48
#define BB_MISC2 0x50
#define BB_SI_ACTRL2 0x60
#define BB_RXINTT 0x14
static volatile u32 *bx;                                   /* registres EXI du canal 0 */
extern u8 exc_stack[2048];
#define pn_fr exc_stack                                   /* (trame : la pile du retour a GC Station ne sert pas en meme temps) */
static u8 pn_mac[6], pn_smac[6];
static u32 pn_seq = 0x5000;
static void bb_sel(void) { bx[0] = (bx[0] & 0x405) | (5u << 4) | (0x80u << 2); }   /* 32 MHz, peripherique 2 */
static void bb_desel(void) { bx[0] = bx[0] & 0x405; }
static void bb_w(u32 v, int n) { bx[4] = v; bx[3] = ((u32)(n - 1) << 4) | 4u | 1u; for (u32 i = 0; i < 100000u && (bx[3] & 1); i++) { } }
static u32 bb_r(int n) { bx[4] = 0; bx[3] = ((u32)(n - 1) << 4) | 1u; for (u32 i = 0; i < 100000u && (bx[3] & 1); i++) { } return bx[4]; }
static void bb_cout(u32 r, u32 v) { bb_sel(); bb_w(((r << 8) | 0x4000u) << 16, 2); bb_w(v << 24, 1); bb_desel(); }
static void bb_mx(u32 a, int wr) { bb_w(0x80000000u | (wr ? 0x40000000u : 0) | (a << 8), 4); }
static u32 bb_in8(u32 a) { bb_sel(); bb_mx(a, 0); u32 v = bb_r(1) >> 24; bb_desel(); return v; }
static void bb_out8(u32 a, u32 v) { bb_sel(); bb_mx(a, 1); bb_w(v << 24, 1); bb_desel(); }
static u32 bb_in16(u32 a) { return bb_in8(a) | bb_in8(a + 1) << 8; }       /* (registres de 16 bits : petit-boutiste) */
static void bb_out16(u32 a, u32 v) { bb_out8(a, v & 0xFF); bb_out8(a + 1, (v >> 8) & 0xFF); }
static void bb_ins(u32 a, u8 *d, u32 n) {
    bb_sel(); bb_mx(a, 0);
    for (; n >= 4; n -= 4, d += 4) { u32 w = bb_r(4); d[0] = (u8)(w >> 24); d[1] = (u8)(w >> 16); d[2] = (u8)(w >> 8); d[3] = (u8)w; }
    for (; n; n--) *d++ = (u8)(bb_r(1) >> 24);
    bb_desel();
}
static void bb_outs(u32 a, const u8 *s, u32 n) {
    bb_sel(); bb_mx(a, 1);
    for (; n >= 4; n -= 4, s += 4) bb_w((u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3], 4);
    for (; n; n--) bb_w((u32)*s++ << 24, 1);
    bb_desel();
}
static u32 pn_be16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static u32 pn_be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void pn_p16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void pn_p32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void pn_send(u8 *f, u32 n) {                       /* trame Ethernet complete (adresses comprises) */
    while (n < 60) f[n++] = 0;
    u32 m = irq_off();
    bb_out16(BB_TXFIFOCNT, n);
    bb_outs(BB_WRTXFIFOD, f, n);
    bb_out8(BB_NCRA, (bb_in8(BB_NCRA) & ~2u) | 4u);       /* ST1 : envoyer */
    for (u32 i = 0; i < 20000u && (bb_in8(BB_NCRA) & 6u); i++) { }
    irq_restore(m);
}
/* une trame recue (0 = aucune) : copiee dans pn_fr */
static u32 pn_recv(void) {
    u32 m = irq_off(), n = 0;
    u32 rrp = bb_in16(BB_RRP) & 0xFFF, rwp = bb_in16(BB_RWP) & 0xFFF;
    if (rrp != rwp) {
        u8 d[4];
        bb_ins(rrp << 8, d, 4);
        u32 w = (u32)d[0] | (u32)d[1] << 8 | (u32)d[2] << 16 | (u32)d[3] << 24;
        u32 next = w & 0xFFF, len = (w >> 12) & 0xFFF;
        if (len > 4 && len - 4 <= 1536u) {
            n = len - 4;
            u32 a = (rrp << 8) + 4, end = 0x10u << 8, first = end - a < n ? end - a : n;   /* (l'anneau finit a la page 0x10) */
            bb_ins(a, pn_fr, first);
            if (first < n) bb_ins(0x01u << 8, pn_fr + first, n - first);
        }
        bb_out16(BB_RRP, next ? next : 1);
    }
    irq_restore(m);
    return n;
}
static u32 pn_ipsum(const u8 *h) { u32 s = 0; for (int i = 0; i < 20; i += 2) s += pn_be16(h + i); while (s >> 16) s = (s & 0xFFFF) + (s >> 16); return ~s & 0xFFFF; }
static void pn_eth(u8 *f, const u8 *dst, u32 type) { for (int i = 0; i < 6; i++) { f[i] = dst[i]; f[6 + i] = pn_mac[i]; } pn_p16(f + 12, type); }
static void pn_arp(int op, const u8 *tmac, u32 tip) {      /* 1 = demande (a tous), 2 = reponse */
    static const u8 all[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    u8 *f = pn_fr;
    pn_eth(f, op == 1 ? all : tmac, 0x0806);
    pn_p16(f + 14, 1); pn_p16(f + 16, 0x0800); f[18] = 6; f[19] = 4; pn_p16(f + 20, (u32)op);
    for (int i = 0; i < 6; i++) { f[22 + i] = pn_mac[i]; f[32 + i] = op == 1 ? 0 : tmac[i]; }
    pn_p32(f + 28, PN_NET->ip); pn_p32(f + 38, tip);
    pn_send(f, 42);
}
/* trame recue : ARP (on repond pour notre adresse, on note celle du serveur) ; rend 1 si c'est une reponse du serveur */
static int pn_rx(u32 n, u32 seq) {
    const u8 *f = pn_fr;
    if (n < 42) return 0;
    u32 type = pn_be16(f + 12);
    if (type == 0x0806) {
        u32 op = pn_be16(f + 20), sip = pn_be32(f + 28), tip = pn_be32(f + 38);
        u32 hop = PN_NET->gw && ((PN_NET->srv ^ PN_NET->ip) & PN_NET->mask) ? PN_NET->gw : PN_NET->srv;
        if (sip == hop) for (int i = 0; i < 6; i++) pn_smac[i] = f[22 + i];
        if (op == 1 && tip == PN_NET->ip) { u8 m[6]; for (int i = 0; i < 6; i++) m[i] = f[22 + i]; pn_arp(2, m, sip); }
        return 0;
    }
    if (type != 0x0800 || f[23] != 17 || pn_be32(f + 26) != PN_NET->srv || n < 42 + 16) return 0;
    const u8 *u = f + 14 + (f[14] & 15) * 4;
    if (pn_be16(u) != PN_NET->port || pn_be32(u + 8) != 0x504E4131u || pn_be32(u + 12) != seq) return 0;   /* "PNA1" */
    return 1;
}
static int __attribute__((noinline)) pn_init(void) {
    if (!H.net || PN_NET->magic != NATRES_NET_MAGIC) return -1;
    bx = HW6(0xCC006800u);
    u32 m = irq_off();
    bb_sel(); bb_w(0, 2); u32 id = bb_r(4); bb_desel();      /* (identifiant du peripherique EXI) */
    if (id != 0x04020200u) { irq_restore(m); H.net_rt = (H.net_rt & 0xFFFFFFu) | (u32)NRN_NOBBA << 24; return -1; }
    bb_cout(0x02, 0x00);                                  /* (interruptions du BBA : aucune) */
    bb_out8(BB_NCRA, 1);                                  /* remise a zero */
    for (volatile u32 i = 0; i < 20000u; i++) { }
    bb_out8(BB_NCRA, 0);
    bb_out8(0x5B, bb_in8(0x5B) & ~0x80u);
    bb_out8(0x5E, 0x01);
    bb_out8(0x5C, bb_in8(0x5C) | 0x04u);
    bb_out8(BB_NCRB, 0x00);
    bb_out8(BB_SI_ACTRL2, 0x74);
    bb_out8(BB_RXINTT, 0x00); bb_out8(BB_RXINTT + 1, 0x06);
    bb_out8(BB_MISC2, 0x80);                              /* (reprise automatique apres une erreur de reception) */
    bb_out16(BB_TLBP, 0x00); bb_out16(BB_BP, 0x01); bb_out16(BB_RHBP, 0x0F); bb_out16(BB_RWP, 0x01); bb_out16(BB_RRP, 0x01);
    bb_out8(BB_GCA, 0x08);
    bb_out8(BB_IMR, 0x00); bb_out8(BB_IR, 0xFF);
    bb_out8(BB_NCRB, 0x04);                               /* (diffusion acceptee : ARP) */
    bb_out8(BB_NCRA, 0x08);                               /* SR : reception */
    bb_ins(BB_PAR, pn_mac, 6);
    irq_restore(m);
    u32 hop = PN_NET->gw && ((PN_NET->srv ^ PN_NET->ip) & PN_NET->mask) ? PN_NET->gw : PN_NET->srv;
    for (int i = 0; i < 6; i++) pn_smac[i] = 0;
    for (int t = 0; t < 40; t++) {                        /* adresse Ethernet du serveur (ou de la passerelle) : ARP */
        pn_arp(1, pn_smac, hop);
        u32 t0 = tbl();
        for (u32 it = 0; it < 400000u && tbl() - t0 < TB_SEC / 20u; it++) {   /* (et un nombre de tours : horloge arretee dans le banc d'essai) */
            u32 n = pn_recv();
            if (n) pn_rx(n, 0);
            if (pn_smac[0] | pn_smac[1] | pn_smac[2] | pn_smac[3] | pn_smac[4] | pn_smac[5]) { H.net_rt = (H.net_rt & 0xFFFFFFu) | (u32)NRN_ON << 24; return 0; }
        }
    }
    H.net_rt = (H.net_rt & 0xFFFFFFu) | (u32)NRN_NOARP << 24;
    return -1;
}
/* une demande (<= 2048 octets) et ses reponses */
static int __attribute__((noinline)) pn_req(u8 *dst, u32 off, u32 len) {
    u32 tok = PN_NET->tok[H.disc & 1u];
    for (int t = 0; t < 120; t++) {
        u32 seq = ++pn_seq, got = 0;
        u8 *f = pn_fr;
        pn_eth(f, pn_smac, 0x0800);
        u8 *ip = f + 14, *u = ip + 20;
        ip[0] = 0x45; ip[1] = 0; pn_p16(ip + 2, 20 + 8 + 18); pn_p16(ip + 4, seq & 0xFFFF); pn_p16(ip + 6, 0x4000); ip[8] = 64; ip[9] = 17;
        pn_p16(ip + 10, 0); pn_p32(ip + 12, PN_NET->ip); pn_p32(ip + 16, PN_NET->srv); pn_p16(ip + 10, pn_ipsum(ip));
        pn_p16(u, 51236); pn_p16(u + 2, PN_NET->port); pn_p16(u + 4, 8 + 18); pn_p16(u + 6, 0);
        pn_p32(u + 8, 0x504E5231u); pn_p32(u + 12, seq); pn_p32(u + 16, tok); pn_p32(u + 20, off); pn_p16(u + 24, len);   /* "PNR1" */
        pn_send(f, 14 + 20 + 8 + 18);
        u32 t0 = tbl(), it = 0;
        while (got < len && tbl() - t0 < TB_SEC / 40u && ++it < 200000u) {
            u32 n = pn_recv();
            if (!n || !pn_rx(n, seq)) continue;
            const u8 *uh = pn_fr + 14 + (pn_fr[14] & 15) * 4, *d = uh + 8;
            u32 po = pn_be32(d + 8), pl = pn_be16(d + 12);
            if (po < off || po + pl > off + len || pl > 1024 || 14 + 20 + 8 + 14 + pl > n + 4) continue;
            for (u32 i = 0; i < pl; i++) dst[po - off + i] = d[14 + i];
            got += pl; H.net_pk++;
            t0 = tbl(); it = 0;
        }
        if (got >= len) return 0;
        H.net_rt++;
    }
    H.net_rt = (H.net_rt & 0xFFFFFFu) | (u32)NRN_TIMEOUT << 24;
    H.err = NR_ETIMEOUT;
    return -1;
}
static int __attribute__((noinline)) pn_read(void *dst, u32 off, u32 len) {
    if (!pn_ready) { pn_ready = pn_init() == 0; if (!pn_ready) { H.err = NR_ENOCARD; return -1; } }
    u8 *o = (u8 *)dst;
    u32 sz = PN_NET->size[H.disc & 1u];
    while (len) {
        if (sz && off >= sz) { for (u32 i = 0; i < len; i++) o[i] = 0; return 0; }   /* (apres la fin : des zeros, comme .nkit) */
        u32 n = len > 2048 ? 2048 : len;
        if (sz && off + n > sz) n = sz - off;
        if (pn_req(o, off, n)) return -1;
        o += n; off += n; len -= n;
    }
    return 0;
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
static void trap_disarm(void);
static void ret_to_gcstation(void) {
    irq_off();
    trap_disarm();                                       /* v139 : (BAT, table des pages, vecteur du jeu remis) */
    if (!H.ret_n || H.ret_n > NATRES_RETMAX || H.ret_state) hot_reset();
    H.ret_state = 1;
    volatile u32 *pi = (volatile u32 *)0xCC003000u;
    volatile unsigned short *dsp = (volatile unsigned short *)0xCC005000u;
    pi[1] = 0;                                           /* (plus aucune interruption) */
    dsp[0x36 / 2] = 0;                                   /* DMA du son arrete */
    dsp[0x0A / 2] = (unsigned short)((dsp[0x0A / 2] & ~0x0150u) | 0x00ACu);   /* DSP : arret, masques coupes, acquitte */
    *HW6(0xCC006C00u) &= ~1u;                 /* son du disque (streaming) arrete */
    *(volatile unsigned short *)0xCC000002u = 0;         /* processeur graphique : plus de lecture de la file */
    {   /* v108 : remise a zero du processeur graphique (comme GX_AbortFrame) : sinon, arrete au milieu d'une
           commande du jeu, il prenait les premieres commandes de GC Station pour la suite de celle-ci et se
           bloquait (« GX ne repond plus » au demarrage, apres A+Z+START dans Cocoto) */
        pi[6] = 1;
        for (volatile u32 i = 0; i < 4000u; i++) ;           /* (~20 us ; pas l'horloge : arretee dans le banc d'essai) */
        pi[6] = 0;
        for (volatile u32 i = 0; i < 1000u; i++) ;
    }
    H.net = 0;                                           /* v143 : (le .dol de GC Station est sur la carte SD) */
    if (!di_ready) di_ready = nr_init() == 0;
    natres_hdr.ext = (u32)ret_tab;                       /* v159 : la table du .dol de GC Station, en place (plus de recopie) */
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
static int vc_pending(void);
static void sw_tick(void);
static void alive_tick(void) {
    if ((++alive_n & 63) == 0) hooks_check();            /* (le jeu a pu remettre sa fonction : on se rebranche) */
    sw_tick();                                           /* v121 : changement de disque en attente ? */
    if (vc_pending()) x_kick();                          /* (carte : interruption pas encore donnee) */
    /* (une ecriture sur la carte coupe le son quelques ms : toutes les 3 s la 1re minute, puis toutes les 20 s) */
    if (tbl() - H.flush_tb > (H.vi_count < 3000u ? TB_SEC * 3 : TB_SEC * 20)) nr_log_flush();
}
#define NR_HOOK(nm, counter) \
    static u32 nm##_orig = 0; \
    static void nm##_hook(u32 irq, u32 ctx) { \
        if (nm##_orig) ((void (*)(u32, u32))nm##_orig)(irq, ctx); \
        counter++; alive_tick(); di_tick(ctx, 0); \
    }
static u32 vi_orig;
static void nr_log_flush(void);
static void igr_poll(void);
static void igr_poll2(int count);
static void gk_run(void);
void ach_run(void);
static u32 igr_ctx = 0;                                  /* v133 : le jeu interrompu (pour le journal au retour A+Z+START) */
static void di_tick(u32 ctx, int vi);
/* v169 : jeu fige alors que l'image continue (Crash Bandicoot GCBP7D : bloque toujours apres 569 commandes) : plus de
   lecture des manettes (interruptions SI) ni de commande au lecteur depuis 6 s, alors qu'il y en avait. On note, une
   seule fois, ou en est le fil principal du jeu : la tache en attente la plus prioritaire (file des taches actives du
   SDK : tete en 0x800000DC ; OSThread : contexte en 0, etat (u16) 0x2C8, priorite 0x2D0, suivante 0x2FC), sinon le
   code interrompu par l'image. Comme A+Z+START : registres, appels, pile, photo de la memoire (exception FE). */
static int ram_ok(u32 a, u32 n);
static void crash_dump(u32 ex, u32 ctx, u32 dar, u32 dsisr);
static u32 frz_si = 0, frz_cmd = 0, frz_tb = 0, frz_done = 0;
static void freeze_check(u32 ctx) {
    if (frz_done || H.vi_count < 300) return;
    u32 now = tbl();
    if (H.si_count != frz_si || H.ncmd != frz_cmd || !frz_tb) { frz_si = H.si_count; frz_cmd = H.ncmd; frz_tb = now; return; }
    if (frz_si < 600 || now - frz_tb < 6u * TB_SEC) return;  /* (manettes jamais lues par SI : rien a dire) */
    frz_done = 1;
    u32 best = 0, bprio = 0xFFFFFFFFu, nth = 0, t = *(volatile u32 *)0x800000DCu;
    for (int g = 0; g < 24 && ram_ok(t, 0x304) && !(t & 3); g++) {
        u32 st = *(volatile unsigned short *)(t + 0x2C8), pr = *(volatile u32 *)(t + 0x2D0);
        nth++;
        if (st == 4 && pr < bprio && pr < 31) { best = t; bprio = pr; }   /* (4 = en attente ; 31 = tache d'attente du SDK) */
        t = *(volatile u32 *)(t + 0x2FC);
    }
    u32 c = best ? best : ctx;
    if (!ram_ok(c, 0x2C8) || (c & 3)) return;
    H.exc_srr0 = *(volatile u32 *)(c + 0x198); H.exc_lr = *(volatile u32 *)(c + 0x84);
    crash_dump(0xFE, c, best, (best ? (*(volatile unsigned short *)(best + 0x2C8)) << 16 | (bprio & 0xFFFF) : 0) | nth << 24);
    nr_log_flush();
}
static void vi_hook(u32 irq, u32 ctx) {                  /* v66 : entree comptee avant la fonction du jeu */
    trap_guard();
    if (++H.vi_in == 1) nr_log_flush();                  /* (1re image : on sait si le jeu y arrive) */
    if (vi_orig) ((void (*)(u32, u32))vi_orig)(irq, ctx);
    H.vi_count++; vi_snap(); alive_tick();
    freeze_check(ctx);                                   /* v169 */
    igr_ctx = ctx;
    di_tick(ctx, 1);                                     /* v133 : fins de commande (sans le lecteur, ou secours) */
    if (H.chts) gk_run();                                /* v121 : cheats, une fois par image (apres le jeu) */
    if (H.flags & NRF_ACH) ach_run();                    /* v268 : succes */
    igr_poll();                                          /* v86 : (apres le jeu : il a lu les manettes) */
}
static u32 ai_orig;
static void st_mix(void);
static void ai_hook(u32 irq, u32 ctx) {                  /* v65 : apres le jeu, la musique du disque */
    trap_guard();
    if (ai_orig) ((void (*)(u32, u32))ai_orig)(irq, ctx);
    H.cnt_ai++;
    st_mix();
    alive_tick();
    di_tick(ctx, 0);
    igr_ctx = ctx;
    igr_poll2(0);                                        /* v89 : aussi ici (~ toutes les 5 ms) : a l'image (VI), le jeu n'a
                                                            souvent pas encore pris les manettes (Wind Waker) : lecture interdite */
}
static u32 dsp_orig = 0;
static void st_mix_from(int from_dsp);
static void dsp_hook(u32 irq, u32 ctx) {                 /* v98 : le jeu y programme parfois le tampon du son suivant */
    trap_guard();
    if (dsp_orig) ((void (*)(u32, u32))dsp_orig)(irq, ctx);
    H.cnt_dsp++;
    st_mix_from(1);
    alive_tick();
    di_tick(ctx, 0);
}
NR_HOOK(pet, H.cnt_pe)
/* (v244 : NR_HOOK(pef) retire : voir hooks_check) */

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
    u32 zb[7], zl[7];                                    /* v135 : + 0x800 octets autour de r3, r4 et r30 (structures du jeu) */
    zb[0] = 0x80000000u; zl[0] = 0x3200;
    zb[1] = (c[13] & ~31u) - 0x8000u; zl[1] = 0x10000;
    zb[2] = (c[1] & ~31u) - 0x200u; zl[2] = 0x4000;
    zb[3] = (c[3] & ~511u) - 0x200u; zl[3] = 0x800;
    zb[4] = (c[4] & ~511u) - 0x200u; zl[4] = 0x800;
    zb[5] = (c[30] & ~511u) - 0x200u; zl[5] = 0x800;
    zb[6] = (H.snap_hi + 31) & ~31u; zl[6] = 0x3E800;      /* (arene : 0x40000 avant, pour garder 704 secteurs) */
    for (u32 i = 0; i < 512; i++) crashbuf[i] = 0;
    cw(0x00, NATRES_SNAP_MAGIC);
    u32 sec = 1, nz = 0;
    for (int z = 0; z < 7; z++) {
        if (z >= 3 && z <= 5 && (c[z == 3 ? 3 : z == 4 ? 4 : 30] < 0x80000000u || c[z == 3 ? 3 : z == 4 ? 4 : 30] >= 0x81800000u)) continue;   /* (pas une adresse) */
        u32 b = zb[z], l = (zl[z] + 511) & ~511u;
        if (b < 0x80000000u) b = 0x80000000u;
        if (b >= 0x81800000u) continue;
        if (b + l > 0x81800000u) l = (0x81800000u - b) & ~511u;
        if (sec + (l >> 9) > H.snap_cnt) l = (H.snap_cnt - sec) << 9;
        if (!l) continue;
        cw(0x08 + nz * 16, b); cw(0x0C + nz * 16, l); cw(0x10 + nz * 16, sec); cw(0x14 + nz * 16, (u32)z);   /* v135 : + le genre de zone */
        for (u32 o = 0; o < l; o += 512, sec++) if (nr_write_sd(H.snap_sec + sec, (const u8 *)(b + o)) == 0) H.snap_done++;
        nz++;
    }
    cw(0x04, nz);
    nr_write_sd(H.snap_sec, crashbuf);
}
/* v243 : les fils du jeu (file des fils actifs du SDK : tete en 0x800000DC, suivant en 0x2FC ; OSThread : contexte en 0,
   etat (u16) 0x2C8, priorite 0x2D0) : ou chacun attend (4e secteur du journal) */
static void thr_dump(void) {
    for (u32 i = 0; i < 512; i++) crashbuf[i] = 0;
    cw(0x00, NATRES_THR_MAGIC); cw(0x08, *(volatile u32 *)0x800000E4u);
    u32 t = *(volatile u32 *)0x800000DCu, n = 0;
    while (n < NATRES_THR_MAX && ram_ok(t, 0x300) && !(t & 3)) {
        volatile u32 *c = (volatile u32 *)t;
        u32 o = 0x10 + n * 32, sp = c[1];
        cw(o, t); cw(o + 4, (u32)*(volatile unsigned short *)(t + 0x2C8) << 16 | (c[0x2D0 / 4] & 0xFFFF));
        cw(o + 8, c[0x198 / 4]); cw(o + 12, c[0x84 / 4]); cw(o + 16, sp);
        for (u32 k = 0; k < 3 && ram_ok(sp, 8) && !(sp & 3); k++) {
            u32 up = *(volatile u32 *)sp;
            if (up <= sp || !ram_ok(up, 8) || (up & 3)) break;
            cw(o + 20 + k * 4, *(volatile u32 *)(up + 4));
            sp = up;
        }
        n++;
        u32 nx = c[0x2FC / 4];
        if (nx == t) break;
        t = nx;
    }
    cw(0x04, n);
    for (u32 i = 0; i < 24; i++) cw(0x190 + i * 4, *(volatile u32 *)(0xCC002000u + i * 4));   /* v257 : registres de l'image */
    /* v245 : interruptions et processeur graphique (un fil qui attend la fin de dessin pour toujours : interruption
       arrivee mais masquee, ou jamais levee ?) : cause et masque du PI, controle PE << 16 | etat CP, masques du SDK */
    cw(0x1F0, *(volatile u32 *)0xCC003000u); cw(0x1F4, *(volatile u32 *)0xCC003004u);
    cw(0x1F8, (u32)*(volatile unsigned short *)0xCC00100Au << 16 | *(volatile unsigned short *)0xCC000000u);
    cw(0x1FC, *(volatile u32 *)0x800000C4u | *(volatile u32 *)0x800000C8u);
    nr_write_sd(H.log_sec + 3, crashbuf);
}
static void vc_diag(void);
static u32 xl_n;
static void crash_dump(u32 ex, u32 ctx, u32 dar, u32 dsisr) {
    if (!(H.flags & NRF_CRASH) || !H.log_sec || !di_ready) return;
    if ((H.flags & NRF_EXILOG) && xl_n) nr_write_sd(H.log_sec + 4, crashbuf);   /* v260 : (avant tout le reste : crashbuf) */
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
    if (ex == 0xFE) vc_diag();                           /* v171 : jeu fige : etat de la carte virtuelle a la place des textes */
    nr_write_sd(H.log_sec + 1, crashbuf);
    if (H.flags & NRF_STACK) stack_dump(c[1]);
    if (H.flags & NRF_THREADS) thr_dump();
    mem_snap(c);
}
void exc_c(u32 ex, u32 ctx) {
    trap_guard();
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
    trap_guard();
    H.si_in++;
    if (!H.vi_count) nr_sample(ctx);
    if (si_orig) ((void (*)(u32, u32))si_orig)(irq, ctx);
    H.si_count++;
    alive_tick();
    di_tick(ctx, 0);
    igr_ctx = ctx;
    igr_poll();
}
static void igr_poll(void) { igr_poll2(1); }
/* count = 0 : ne compte que le temps (interruption du son : trop frequente pour compter les lectures) */
static void igr_poll2(int count) {
    if (!(H.flags & NRF_IGR)) return;
    volatile u32 *si = HW6(0xCC006400u);
    static u8 igr_ch[4];                                 /* v99 : derniere chose vue sur chaque port (1 = A+Z+START tenus) */
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
        if (ch == 0 && (b >> 16) != H.si_last) { H.si_last = b >> 16; H.si_changes = ((H.si_changes & 0xFFFFu) + 1) | (H.si_changes & 0xFFFF0000u) | ((b >> 16) & 0x1F7Fu) << 16; }   /* v96 : bits hauts = toutes les touches vues (diagnostic A+Z+START) */
        /* v99 : une vraie manette met toujours le bit 0x0080 de ses touches. Un port que le jeu n'interroge
           pas (rien de branche) garde 00000000, sans le bit d'erreur : avant, il comptait comme "manette sans
           A+Z+START" chaque fois que la manette 1 etait sautee (donnees neuves) et remettait le compte a zero,
           plusieurs fois par seconde : A+Z+START ne pouvait jamais etre tenu 1 s. */
        if ((b & 0x80000000u) || !(b & 0x00800000u)) { igr_ch[ch] = 0; continue; }   /* (pas de manette) */
        seen = 1;
        igr_ch[ch] = (b & 0x11000000u) == 0x11000000u && (b & 0x00100000u);   /* A + START, Z */
    }
    for (int ch = 0; ch < 4; ch++) if (igr_ch[ch]) on = 1;   /* (port saute cette fois : son dernier etat) */
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
        /* v133 : ou en etait le jeu (comme un plantage : code, appels, pile, memoire) : un jeu fige
           (ecran noir, il tourne encore) montre ainsi ce qu'il attend */
        if (igr_ctx >= 0x80000000u && igr_ctx < 0x817F0000u && !(igr_ctx & 3)) {
            H.exc_srr0 = *(volatile u32 *)(igr_ctx + 0x198); H.exc_lr = *(volatile u32 *)(igr_ctx + 0x84);
            crash_dump(0xFF, igr_ctx, 0, 0);
        }
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
static u32 st_snap_addr = 0xFFFFFFFFu;                /* v106 : tampon de la derniere sauvegarde du decodeur */
static short st_pcm[56];
static u32 st_pcm_i = 28, st_phase = 0;
static int st_cl = 0, st_cr = 0;
static int st_pl = 0, st_pr = 0;                          /* v107 : dernier echantillon de l'image precedente (filtre) */
/* v98 : lu par 4 secteurs a la suite (2 Ko = 37 ms de musique) : 4 fois moins de recherches de la carte
   pendant l'interruption du son (avant : un secteur toutes les 9 ms) */
#define ST_SECS 4u
static u8 st_sec[512 * ST_SECS] __attribute__((aligned(32)));
static u32 st_sec_off = 0xFFFFFFFFu, st_sec_n = 0;
static int nr_read_multi(u32 sector, u32 count, u8 *o);
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
    if (st_sec_off == 0xFFFFFFFFu || so < st_sec_off || so >= st_sec_off + st_sec_n) {
        if (H.net) {                                      /* v143 : (ISO sur le reseau) */
            if (pn_read(st_sec, so, ST_SECS * 512u)) { st_sec_off = 0xFFFFFFFFu; st_on = 0; return -1; }
            st_sec_off = so; st_sec_n = ST_SECS * 512u;
            goto have;
        }
        if (iso_to_sd(so >> 9, &sd)) { st_on = 0; return -1; }
        u32 n = iso_run < ST_SECS ? iso_run : ST_SECS;
        if (!n) n = 1;
        if (sd == NATRES_ZERO) { for (u32 i = 0; i < n * 512; i++) st_sec[i] = 0; }   /* (v121 : .ciso, bloc absent) */
        else if (n == 1 ? nr_read_sd(sd, st_sec) : nr_read_multi(sd, n, st_sec)) { st_sec_off = 0xFFFFFFFFu; st_on = 0; return -1; }
        st_sec_off = so; st_sec_n = n * 512;
    }
have:
    { }
    const u8 *a = st_sec + (st_pos - st_sec_off);
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
    st_snap_addr = 0xFFFFFFFFu;                          /* (v106 : nouvelle commande : pas de retour en arriere) */
    u32 sub = (c0 >> 16) & 0xFF;
    if (sub == 0) {
        if (off == 0 && len == 0) st_stop_end = 1;
        else if (!st_stop_end) {
            st_next_start = off; st_next_len = len;
            if (!st_on) {
                st_cur_start = st_next_start; st_cur_len = st_next_len; st_pos = st_cur_start;
                st_hl1 = st_hl2 = st_hr1 = st_hr2 = 0; st_pcm_i = 28; st_phase = 0; st_pl = st_pr = 0;
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
/* v98 : le tampon est complete des que le jeu le programme, depuis l'interruption du son OU celle du DSP
   (certains jeux, Cocoto, programment le tampon suivant a la fin du calcul du DSP : avant, la musique
   etait ajoutee au tampon deja en train d'etre joue, donc en partie seulement : saccades), et jamais deux
   fois (un tampon rejoue tel quel quand le DSP est en retard gardait sa musique, et en recevait une 2e). */
static u32 st_last_addr = 0xFFFFFFFFu, st_last_sum = 0, st_dsp_n = 0;
/* v106 : certains jeux (Cocoto Kart Racer) programment le tampon suivant a l'interruption du son, PUIS
   le DSP le calcule : la musique ajoutee a l'interruption du son etait effacee, puis ajoutee une 2e fois a
   la fin du DSP avec les echantillons suivants : la musique avancait deux fois trop vite et sautait un
   morceau sur deux (saccades). Maintenant : l'etat du decodeur est garde avant chaque ajout ; si le DSP a
   reecrit ce meme tampon, on revient a cet etat et on ajoute les memes echantillons. */
typedef struct { u32 on, stop_end, pos, cs, clen, ns, nl, pcm_i, phase; int hl1, hl2, hr1, hr2, cl, cr, pl, pr; short pcm[56]; } stsnap_t;
static stsnap_t st_snap;
static void st_save(u32 addr) {
    stsnap_t *s = &st_snap;
    s->on = st_on; s->stop_end = st_stop_end; s->pos = st_pos; s->cs = st_cur_start; s->clen = st_cur_len; s->ns = st_next_start; s->nl = st_next_len;
    s->pcm_i = st_pcm_i; s->phase = st_phase; s->hl1 = st_hl1; s->hl2 = st_hl2; s->hr1 = st_hr1; s->hr2 = st_hr2; s->cl = st_cl; s->cr = st_cr; s->pl = st_pl; s->pr = st_pr;
    for (int i = 0; i < 56; i++) s->pcm[i] = st_pcm[i];
    st_snap_addr = addr;
}
static void st_load(void) {
    const stsnap_t *s = &st_snap;
    st_on = s->on; st_stop_end = s->stop_end; st_pos = s->pos; st_cur_start = s->cs; st_cur_len = s->clen; st_next_start = s->ns; st_next_len = s->nl;
    st_pcm_i = s->pcm_i; st_phase = s->phase; st_hl1 = s->hl1; st_hl2 = s->hl2; st_hr1 = s->hr1; st_hr2 = s->hr2; st_cl = s->cl; st_cr = s->cr; st_pl = s->pl; st_pr = s->pr;
    for (int i = 0; i < 56; i++) st_pcm[i] = s->pcm[i];
}
static u32 st_sum(const u32 *w, u32 n) { u32 s = 0x9E3779B9u; for (u32 i = 0; i < n; i++) s = (s ^ w[i]) * 0x01000193u; return s; }
static void st_mix_from(int from_dsp) {
    volatile u32 *ai = HW6(0xCC006C00u);
    u32 cr = ai[0];
    if (!st_on || !(cr & 1)) return;                     /* (PSTAT : le jeu a lance la lecture du disque) */
    volatile unsigned short *dsp = (volatile unsigned short *)0xCC005000u;
    u32 addr = ((((u32)dsp[0x30 / 2] & 0x3FFu) << 16) | dsp[0x32 / 2]) & ~31u;
    u32 len = ((u32)dsp[0x36 / 2] & 0x7FFFu) << 5;
    if (!len || len > 0x8000u || addr + len > 0x01800000u) return;
    {
        u8 *q = (u8 *)(addr | 0x80000000u);
        for (u32 a = (u32)q; a < (u32)q + len; a += 32) __asm__ volatile("dcbi 0,%0" :: "r"(a) : "memory");
        if (addr == st_last_addr && st_sum((const u32 *)q, len / 4) == st_last_sum) {   /* (deja complete) */
            if (!from_dsp && H.crc_err < 0xFFFF0000u) H.crc_err += 0x10000u;            /* (journal : tampons rejoues) */
            return;
        }
    }
    if (from_dsp && addr == st_last_addr && st_snap_addr == addr) {   /* v106 : le DSP a reecrit le tampon : memes echantillons */
        st_load();
        if (st_dsp_n < 0xFFFFu) { st_dsp_n++; H.xfer = (H.xfer & 0xFFFFu) | st_dsp_n << 16; }
    }
    st_save(addr);
    u32 vol = ai[1];
    int vl = (int)(vol & 0xFF), vr = (int)((vol >> 8) & 0xFF);
    u32 drate = (cr & 0x40) ? 32000u : 48000u, srate = (cr & 2) ? 48000u : 32000u;
    u32 step = (srate << 16) / drate;
    u8 *p = (u8 *)(addr | 0x80000000u);
    for (u32 a = (u32)p; a < (u32)p + len; a += 32) __asm__ volatile("dcbi 0,%0" :: "r"(a) : "memory");   /* (ecrit par le DSP) */
    short *s = (short *)p;
    u32 n = len / 4;
    /* v107 : musique a 48 kHz, son du jeu a 32 kHz : chaque echantillon de sortie est la moyenne des
       echantillons du disque qu'il remplace (et du dernier d'avant). Avant : un sur 1,5 simplement garde :
       les aigus se repliaient en grezillement. (Meme frequence : echantillons tels quels.) */
    for (u32 i = 0; i < n; i++) {
        int ol, orr;
        st_phase += step;
        if (step == 0x10000u) {
            while (st_phase >= 0x10000u) { st_phase -= 0x10000u; st_adv(); }
            ol = st_cl; orr = st_cr;
        } else {
            int al = st_pl, ar = st_pr, k = 1;
            while (st_phase >= 0x10000u) { st_phase -= 0x10000u; st_adv(); al += st_cl; ar += st_cr; k++; }
            if (k == 1) { al += st_cl; ar += st_cr; k = 2; }
            ol = al / k; orr = ar / k;
            st_pl = st_cl; st_pr = st_cr;
        }
        s[i * 2] = (short)st_clamp(s[i * 2] + ((ol * vl) >> 8), -0x8000, 0x7fff);
        s[i * 2 + 1] = (short)st_clamp(s[i * 2 + 1] + ((orr * vr) >> 8), -0x8000, 0x7fff);
    }
    st_last_addr = addr; st_last_sum = st_sum((const u32 *)p, len / 4);
    cache_flush((u32)p, len);
    H.st_mix++;
}
static void st_mix(void) { st_mix_from(0); }

/* ======================= v65 : cartes memoire virtuelles (v122 : slots A et B) =======================
   Les acces du jeu aux registres EXI des canaux 0 et 1 (0xCC006800-27) sont branches ici (comme ceux du
   lecteur). Peripherique 0 du canal 0 = carte du slot A, du canal 1 = carte du slot B : emulees, le contenu
   est un fichier de la carte SD (morceaux prepares par GC Station). Les autres peripheriques (horloge, SRAM,
   police de la console, port serie 1) : transmis tels quels au vrai materiel. Protocole de la carte : comme
   Dolphin (EXI_DeviceMemoryCard). Les "interruptions" de la carte (fin de transfert, fin d'ecriture) sont
   donnees au jeu en appelant ses fonctions EXI, au rythme d'une vraie fin de commande du lecteur (meme
   mecanisme que pour le faux lecteur). */
typedef struct {
    u32 mar, len, cr, data, irq, csr;                    /* registres vus par le jeu (irq : EXIINT 2, TCINT 8) */
    u32 pos, cmd, addr, status, irq_sw, sec_no;
    u8 prog[128];
    const natres_ext_t *tab;
    u32 n, size, id, on;
} vcard_t;
static vcard_t vc[2] = { { .status = 0xC1u, .sec_no = 0xFFFFFFFFu }, { .status = 0xC1u, .sec_no = 0xFFFFFFFFu } };   /* (occupe | debloquee | prete) */
static u8 c_sec[2][512] __attribute__((aligned(32)));
static u32 x_disp = 0;
static void vc_setup(void) {                             /* (au 1er acces : tables donnees par GC Station) */
    vc[0].tab = card_tab; vc[0].n = H.card_n; vc[0].size = H.card_size; vc[0].id = H.card_id; vc[0].on = (H.flags & NRF_CARD) && H.card_n;
    vc[1].tab = card2_tab; vc[1].n = H.card2_n; vc[1].size = H.card2_size; vc[1].id = H.card2_id; vc[1].on = (H.flags & NRF_CARDB) && H.card2_n;
}
static int card_sd(vcard_t *v, u32 csec, u32 *sd) {
    for (u32 k = 0; k < v->n && k < NATRES_CARDMAX; k++) {
        const natres_ext_t *e = &v->tab[k];
        if (csec >= e->iso && csec - e->iso < e->cnt) { *sd = e->sd + (csec - e->iso); return 0; }
    }
    return -1;
}
static u8 *card_buf(vcard_t *v) { return c_sec[v == &vc[1]]; }
static int card_load(vcard_t *v, u32 csec) {             /* secteur de la carte dans c_sec */
    if (csec == v->sec_no) return 0;
    u32 sd;
    v->sec_no = 0xFFFFFFFFu;
    if (card_sd(v, csec, &sd) || nr_read_sd(sd, card_buf(v))) return -1;
    v->sec_no = csec;
    return 0;
}
static u8 card_rd1(vcard_t *v, u32 a) { return card_load(v, a >> 9) ? 0xFF : card_buf(v)[a & 511]; }
static void card_rd(vcard_t *v, u8 *dst, u32 a, u32 len) {
    while (len) {
        u32 n = 512 - (a & 511);
        if (n > len) n = len;
        if (card_load(v, a >> 9)) { for (u32 i = 0; i < n; i++) dst[i] = 0xFF; }
        else for (u32 i = 0; i < n; i++) dst[i] = card_buf(v)[(a & 511) + i];
        dst += n; a += n; len -= n;
    }
}
static void card_wr(vcard_t *v, u32 a, const u8 *src, int fill, u32 len) {   /* src = NULL : remplir avec fill */
    while (len) {
        u32 n = 512 - (a & 511), sd;
        if (n > len) n = len;
        if (n == 512) v->sec_no = a >> 9;                 /* (secteur entier : pas besoin de le relire) */
        if (!card_load(v, a >> 9)) {
            u8 *b = card_buf(v);
            for (u32 i = 0; i < n; i++) b[(a & 511) + i] = src ? src[i] : (u8)fill;
            if (!card_sd(v, a >> 9, &sd) && !nr_write_sd(sd, b)) H.card_writes++;
            else v->sec_no = 0xFFFFFFFFu;
        }
        if (src) src += n;
        a += n; len -= n;
    }
}
static void x_kick(void);
static void card_done(vcard_t *v) {                      /* fin d'effacement / d'ecriture */
    v->status |= 0x01; v->status &= ~0x80u;
    if (v->irq_sw) { v->irq |= 0x2; H.card_irqs++; x_kick(); }   /* EXIINT */
}
static void card_cs(vcard_t *v, int on) {
    if (on) { v->pos = 0; return; }
    u32 mask = v->size - 1;
    if (v->cmd == 0xF1 && v->pos > 2) { card_wr(v, v->addr & mask & ~0x1FFFu, 0, 0xFF, 0x2000); card_done(v); }
    else if (v->cmd == 0xF4 && v->pos > 2) { card_wr(v, 0, 0, 0xFF, v->size); v->status &= ~0x80u; }
    else if (v->cmd == 0xF2 && v->pos >= 5) {
        u32 cnt = v->pos - 5, a = v->addr;
        u8 one;
        for (u32 i = 0; i < cnt; i++) {                  /* (octets envoyes en direct ; le DMA, lui, ecrit tout de suite) */
            one = v->prog[i & 127];
            card_wr(v, a & mask, &one, 0, 1);
            a = (a & ~0x1FFu) | ((a + 1) & 0x1FFu);
        }
        card_done(v);
    }
}
static u8 card_byte(vcard_t *v, u8 b) {
    if (v->pos == 0) {
        v->cmd = b; b = 0xFF;
        H.card_cmds++; H.card_last = v->cmd;
        if (v->cmd == 0x89) { v->status &= ~0x18u; v->status |= 0x01; v->irq &= ~0x2u; v->pos = 0; }
    } else {
        switch (v->cmd) {
        case 0x00: b = v->pos == 1 ? 0x80 : (u8)(v->id >> (24 - (((v->pos - 2) & 3) * 8))); break;
        case 0x52:
        case 0xF2:
            if (v->pos == 1) v->addr = (u32)b << 17;
            else if (v->pos == 2) v->addr |= (u32)b << 9;
            else if (v->pos == 3) v->addr |= ((u32)b & 3) << 7;
            else if (v->pos == 4) v->addr |= (u32)b & 0x7F;
            if (v->cmd == 0xF2) { if (v->pos >= 5) v->prog[(v->pos - 5) & 0x7F] = b; b = 0xFF; break; }
            if (v->pos == 1) b = 0xFF;
            else {
                b = card_rd1(v, v->addr & (v->size - 1));
                if (v->pos >= 9) v->addr = (v->addr & ~0x1FFu) | ((v->addr + 1) & 0x1FFu);
            }
            break;
        case 0x83: b = (u8)v->status; break;
        case 0x85: b = v->pos == 1 ? 0xC2 : ((v->pos & 1) ? 0x21 : 0xC2); break;   /* (ID 0xC221 : carte Nintendo) */
        case 0xF1: if (v->pos == 1) v->addr = (u32)b << 17; else if (v->pos == 2) v->addr |= (u32)b << 9; b = 0xFF; break;
        case 0x81: if (v->pos == 1) v->irq_sw = b; b = 0xFF; break;
        default: b = 0xFF; break;
        }
    }
    v->pos++;
    return b;
}
static void x_xfer(vcard_t *v) {                         /* transfert vers la carte virtuelle */
    u32 n = ((v->cr >> 4) & 3) + 1, rw = (v->cr >> 2) & 3;
    if (!(v->cr & 2)) {                                  /* immediat (1 a 4 octets dans DATA) */
        if (rw == 1) { for (u32 i = 0; i < n; i++) card_byte(v, (u8)(v->data >> (24 - i * 8))); }
        else if (rw == 0) { u32 x = 0; for (u32 i = 0; i < n; i++) x |= (u32)card_byte(v, 0) << (24 - i * 8); v->data = x; }
    } else {                                             /* DMA */
        u32 dst = (v->mar & 0x01FFFFE0u) | 0x80000000u, len = v->len & 0x03FFFFE0u;
        if (len && dst + len <= 0x81800000u) {
            if (rw == 0) { card_rd(v, (u8 *)dst, v->addr & (v->size - 1), len); cache_flush(dst, len); }
            else if (rw == 1) card_wr(v, v->addr & (v->size - 1), (const u8 *)dst, 0, len);
        }
    }
    v->cr &= ~1u;
    v->irq |= 0x8;                                       /* TCINT */
    if (v->csr & 0x4) x_kick();                          /* (le jeu attend une interruption) */
}
/* v258 : comme une vraie console, la fin d'un transfert de la carte n'est donnee au jeu que s'il ne l'a pas masquee
   dans son systeme (masques du SDK : 0x800000C4 global, 0x800000C8 local ; EXI0 : 9 et 10, EXI1 : 12 et 13). Sinon
   elle attend qu'il la demasque (Soulcalibur II GRSPAF : fige en relisant sa sauvegarde, la fin arrivait masquee). */
#define VC_OSMASK (*(volatile u32 *)0x800000C4u | *(volatile u32 *)0x800000C8u)
static u32 vc_want(int c) {                              /* interruptions de la carte c que le jeu accepte maintenant */
    vcard_t *v = &vc[c];
    u32 w = (v->csr & 1 ? 2u : 0) | (v->csr & 4 ? 8u : 0), m = VC_OSMASK;
    if (m & (0x80000000u >> (c ? 12 : 9))) w &= ~2u;
    if (m & (0x80000000u >> (c ? 13 : 10))) w &= ~8u;
    return w;
}
static int vc_pending(void) {                            /* une interruption de carte attend d'etre donnee au jeu */
    int p = 0;
    for (int c = 0; c < 2; c++) if (vc[c].on && (vc[c].irq & vc_want(c))) p = 1;
    return p;
}
/* a = adresse visee par le jeu (calculee pendant le jeu) : canal 0 (slot A), canal 1 (slot B) si emules */
static vcard_t *vc_of(u32 a) {
    u32 o = a - 0xCC006800u;
    if (o >= 0x28u) return 0;
    if (!vc[0].tab) vc_setup();
    vcard_t *v = &vc[o / 0x14u];
    return v->on ? v : 0;
}
/* v260 : ce qui passe vers les autres peripheriques du canal 0 (police, horloge / reglages de la console), carte du
   slot A emulee : Soulcalibur II sans textes ni question 50/60 Hz avec la carte virtuelle, normal sans elle */
/* (les echanges sont notes directement dans crashbuf, libre pendant le jeu : il ne sert qu'aux comptes rendus de la
   fin, et le 5e secteur est ecrit en premier) */
static u32 xl_n = 0, xl_cur = 0xFFFFFFFFu;
static u32 xl_get(u32 o) { return (u32)crashbuf[o] << 24 | (u32)crashbuf[o + 1] << 16 | (u32)crashbuf[o + 2] << 8 | crashbuf[o + 3]; }
static u32 xl_rd(u32 a) { return *(volatile u32 *)((a & 0x01FFFFFCu) | 0xC0000000u); }
static void xl_start(vcard_t *v, u32 cr) {
    if (v != &vc[0]) return;
    u32 o;
    cw(0x00, NATRES_EXI_MAGIC); cw(0x04, ++xl_n);
    xl_cur = 0xFFFFFFFFu;
    if (xl_n > NATRES_EXI_MAX) return;
    o = 0x20 + (xl_n - 1) * 32;
    cw(o, (v->csr & 0xFFFFu) << 16 | (cr & 0xFFFFu)); cw(o + 4, v->data); cw(o + 8, v->mar); cw(o + 12, v->len);
    xl_cur = o;
}
static void xl_end(void) {                               /* (le jeu deselectionne : les donnees du DMA sont la) */
    u32 o = xl_cur, c;
    if (o >= 0x200u) return;
    xl_cur = 0xFFFFFFFFu;
    c = xl_get(o);
    if (!(c & 2) || ((c >> 2) & 3)) return;              /* (seulement un DMA en lecture) */
    u32 m = xl_get(o + 8);
    for (u32 k = 0; k < 4; k++) cw(o + 16 + k * 4, xl_rd(m + (k & 1) * 4 + (k & 2) * 8));
}
static u32 exi_load(u32 a) {
    trap_guard();
    if (a - 0xCC006000u < 0x40u) return di_load(a);      /* v133 : acces au lecteur branches en secours (adresse calculee) */
    early_note(a, 0, 0);
    vcard_t *v = vc_of(a);
    if (!v) return *HW6(a);                              /* (autres canaux, cartes reelles : vrai materiel) */
    volatile u32 *real = HW6(a & ~3u) - (((a & ~3u) - 0xCC006800u) % 0x14u) / 4u;   /* (registres du canal) */
    u32 m = irq_off(), x;
    switch ((((a - 0xCC006800u) % 0x14u)) >> 2) {
    case 0: {
        u32 r = real[0];
        x = (r & ~0x1F8Fu) | (v->csr & 0x0785u) | v->irq | (r & 0x2u) | 0x1000u;   /* (masques et CS du jeu, carte presente) */
        if (!(v->csr & 0x80)) x |= r & 0x8u;             /* (fin d'un vrai transfert : horloge, SRAM...) */
        break; }
    case 1: x = (v->csr & 0x80) ? v->mar : real[1]; break;
    case 2: x = (v->csr & 0x80) ? v->len : real[2]; break;
    case 3: x = (v->csr & 0x80) ? v->cr : real[3]; break;
    case 4: x = (v->csr & 0x80) ? v->data : real[4]; if (!(v->csr & 0x80) && xl_cur < 0x200u && !(xl_get(xl_cur) & 2)) cw(xl_cur + 16, x); break;
    default: x = 0; break;
    }
    irq_restore(m);
    return x;
}
static void exi_store(u32 a, u32 val) {
    trap_guard();
    if (a - 0xCC006000u < 0x40u) { di_store(a, val); return; }   /* v133 */
    early_note(a, val, 1);
    if (a == 0xCC006800u && (val & 0x200u)) H.bba_n++;   /* v145 : le jeu parle au Broadband Adapter (jeu en reseau) */
    vcard_t *v = vc_of(a);
    if (!v) { *HW6(a) = val; return; }
    volatile u32 *real = HW6(a & ~3u) - (((a & ~3u) - 0xCC006800u) % 0x14u) / 4u;
    u32 m = irq_off();
    switch ((((a - 0xCC006800u) % 0x14u)) >> 2) {
    case 0: {
        u32 old = v->csr;
        if (val & 0x2) v->irq &= ~0x2u;
        if (val & 0x8) v->irq &= ~0x8u;
        v->csr = val & 0x27F5u;                          /* (masques, CLK, CS, EXTINTMASK, ROMDIS) */
        if ((old & 0x80) && !(val & 0x80)) card_cs(v, 0);
        if (!(old & 0x80) && (val & 0x80)) card_cs(v, 1);
        if ((old & 0x300u) && !(val & 0x300u)) xl_end();
        real[0] = val & ~0x0C80u;                        /* (vrai materiel : jamais la vraie carte du slot, pas d'insertion) */
        break; }
    case 1: v->mar = val; real[1] = val; break;
    case 2: v->len = val; real[2] = val; break;
    case 3: if (v->csr & 0x80) { v->cr = val; if (val & 1) x_xfer(v); } else { if ((val & 1) && (v->csr & 0x300u)) xl_start(v, val); real[3] = val; } break;
    case 4: v->data = val; real[4] = val; break;
    }
    irq_restore(m);
}
/* les "interruptions" des cartes : fonctions EXI du jeu appelees depuis une interruption
   (table du SDK : canal 0 = 9 EXI, 10 TC ; canal 1 = 12 EXI, 13 TC) */
static void x_dispatch(u32 ctx) {
    volatile u32 *t = OS_INT_TABLE;
    for (int c = 0; c < 2; c++) {
        vcard_t *v = &vc[c];
        if (!v->on) continue;
        u32 itc = c ? 13 : 10, iex = c ? 12 : 9;
        for (int g = 0; g < 3 && (v->irq & vc_want(c)); g++) {
            u32 w = vc_want(c);
            if ((v->irq & w & 0x8) && t[itc] >= 0x80003100u && t[itc] < 0x817F0000u) ((void (*)(u32, u32))t[itc])(itc, ctx);
            if ((v->irq & vc_want(c) & 0x2) && t[iex] >= 0x80003100u && t[iex] < 0x817F0000u) ((void (*)(u32, u32))t[iex])(iex, ctx);
            x_disp++;
        }
    }
}

/* ======================= v122 : manettes (registres SI) =======================
   Si un reglage de manette est choisi pour ce jeu, les acces du jeu aux registres des manettes
   (0xCC006400-3F) passent par ici : stick C inverse / echange avec le stick, L et R "enfonces" des que la
   gachette analogique depasse un seuil, manettes lues plus souvent (SIPOLL). Mode analogique 3 seulement
   (celui des jeux) : pour les autres, rien n'est change. */
static u32 si_h[4], si_l[4];                             /* dernieres valeurs brutes lues (manette, stick C / gachettes) */
static u32 si_mode(int ch) { return (*HW6(0xCC006400u + ch * 12) >> 8) & 7; }
static u32 si_inv(u32 v) { return v ? 256u - v > 255u ? 255u : 256u - v : 255u; }
static u32 si_fix(int ch, int lo) {
    u32 o = H.opt, h = si_h[ch], l = si_l[ch];
    u32 sx = (h >> 8) & 0xFF, sy = h & 0xFF, cx = l >> 24, cy = (l >> 16) & 0xFF, tl = (l >> 8) & 0xFF, tr = l & 0xFF, thr = (o >> 8) & 0xFF;
    if (o & 4u) { u32 t = sx; sx = cx; cx = t; t = sy; sy = cy; cy = t; }   /* (sticks echanges) */
    if (o & 1u) cy = si_inv(cy);                         /* (stick C : haut / bas inverses) */
    if (o & 2u) cx = si_inv(cx);                         /* (stick C : gauche / droite inverses) */
    H.si_fixes++;
    if (lo) return cx << 24 | cy << 16 | tl << 8 | tr;
    u32 btn = h & 0xFFFF0000u;
    if (thr && !(h & 0x80000000u)) { if (tl >= thr) btn |= 0x00400000u; if (tr >= thr) btn |= 0x00200000u; }   /* (L, R numeriques) */
    return btn | sx << 8 | sy;
}
static u32 si_load(u32 a) {
    trap_guard();
    u32 o = a - 0xCC006400u, v = *HW6(a);
    if (o < 0x30u && (H.opt & 0xFF07u)) {
        int ch = (int)(o / 12u), r = (int)(o % 12u);
        if ((r == 4 || r == 8) && si_mode(ch) == 3) {
            if (r == 4) { si_h[ch] = v; si_l[ch] = *HW6(a + 4); return si_fix(ch, 0); }   /* (H : on lit aussi L) */
            si_l[ch] = v; return si_fix(ch, 1);
        }
    }
    return v;
}
static void si_store(u32 a, u32 val) {
    trap_guard();
    u32 k = (H.opt >> 16) & 0xF;
    if (a == 0xCC006430u && k > 1) {                     /* SIPOLL : Y lectures par image, X lignes entre deux */
        u32 x = (val >> 16) & 0x3FF, y = (val >> 8) & 0xFF;
        y *= k; if (y > 255) y = 255;
        x /= k; if (x < 1) x = 1;
        val = (val & 0xFC0000FFu) | x << 16 | y << 8;
    }
    *HW6(a) = val;
}

/* ======================= v121 : cheats (codes Gecko) =======================
   GC Station range ici les codes actives du jeu : {nombre de mots, puis les codes (2 mots chacun)}.
   A chaque image, ils sont appliques comme le fait un gestionnaire de codes Gecko (format public des codes
   "WiiRD / Gecko"). Types pris en charge (GC Station verifie avant le lancement, sinon : lanceur de secours) :
   00-07 ecritures (8, 16, 32 bits, avec repetition ; texte), 08 ecriture en serie, 20-2F conditions (32 bits,
   16 bits masque), 40 42 48 4A adresse de base / pointeur, C2 code insere, E0 E2 fins de conditions, F0 fin.
   Bit 0x10 du 1er octet : adresse relative au pointeur (po) au lieu de l'adresse de base (ba). */
static int gk_ram(u32 a, u32 n) { return a >= 0x80000000u && a + n <= NATRES_AT && a + n > a; }
static void gk_sync(u32 a, u32 len) {
    for (u32 x = a & ~31u; x < a + len; x += 32) __asm__ volatile("dcbst 0,%0; sync; icbi 0,%0" :: "r"(x) : "memory");
    __asm__ volatile("sync; isync" ::: "memory");
}
static void gk_w8(u32 a, u32 v) { if (gk_ram(a, 1)) { *(volatile u8 *)a = (u8)v; gk_sync(a, 1); } }
static void gk_w16(u32 a, u32 v) { a &= ~1u; if (gk_ram(a, 2)) { *(volatile unsigned short *)a = (unsigned short)v; gk_sync(a, 2); } }
static void gk_w32(u32 a, u32 v) { a &= ~3u; if (gk_ram(a, 4)) { *(volatile u32 *)a = v; gk_sync(a, 4); } }
static u32 gk_r32(u32 a) { a &= ~3u; return gk_ram(a, 4) ? *(volatile u32 *)a : 0; }
static u32 gk_r16(u32 a) { a &= ~1u; return gk_ram(a, 2) ? *(volatile unsigned short *)a : 0; }
/* niveaux de conditions : depth = ouverts, skip = 1er niveau faux (0 = tout est vrai, on applique) */
static void gk_endif(u32 *depth, u32 *skip, u32 n) {
    *depth = n > *depth ? 0 : *depth - n;
    if (*skip > *depth) *skip = 0;
}
static void gk_run(void) {
    const u32 *w = (const u32 *)H.chts;
    u32 nw = w[0], ba = 0x80000000u, po = 0x80000000u, depth = 0, skip = 0;
    w++;
    for (u32 i = 0; i + 1 < nw; i += 2) {
        u32 a = w[i], b = w[i + 1], t = a >> 24;
        if (t == 0xF0) break;                            /* fin des codes */
        if (t == 0xE0) {                                 /* fin de toutes les conditions ; ba / po (16 bits hauts) */
            depth = skip = 0;
            if (b >> 16) ba = b & 0xFFFF0000u;
            if (b & 0xFFFFu) po = b << 16;
            continue;
        }
        if (t == 0xE2) {                                 /* E2T000VV : VV fins de condition ; T = 1 : puis "sinon" */
            gk_endif(&depth, &skip, a & 0xFFu);
            if ((a >> 20) & 1) { if (depth && skip == depth) skip = 0; else if (depth && !skip) skip = depth; }
            if (!skip) { if (b >> 16) ba = b & 0xFFFF0000u; if (b & 0xFFFFu) po = b << 16; }
            continue;
        }
        if (t == 0xDE || t == 0xCE) {                    /* DE : si 0xXXXX0000 <= po < 0xYYYY0000 (CE : ba) */
            depth++;
            u32 v = t == 0xDE ? po : ba;
            if (!skip && !(v >= (b & 0xFFFF0000u) && v < (b << 16))) skip = depth;
            continue;
        }
        u32 ct = t & 0xEE, ad = ((t & 0x10) ? po : ba) + (a & 0x01FFFFFFu);
        if (ct == 0x40 || ct == 0x42 || ct == 0x48 || ct == 0x4A) {   /* adresse de base / pointeur */
            if (skip) continue;
            int isp = ct >= 0x48, set = (a >> 20) & 0xF ? 0 : 1, y = (a >> 16) & 0xF;
            u32 x = b + (y == 1 ? (isp ? po : ba) : 0);
            u32 v = (ct == 0x40 || ct == 0x48) ? gk_r32(x) : x;
            if (isp) po = set ? v : po + v; else ba = set ? v : ba + v;
            continue;
        }
        if (ct >= 0x20 && ct <= 0x2E) {                  /* conditions (bit 0 de l'adresse : une fin de condition d'abord) */
            if (a & 1) { gk_endif(&depth, &skip, 1); ad &= ~1u; }
            depth++;
            if (skip) continue;
            int ok;
            if (ct < 0x28) {
                u32 v = gk_r32(ad);
                ok = ct == 0x20 ? v == b : ct == 0x22 ? v != b : ct == 0x24 ? v > b : v < b;
            } else {
                u32 v = gk_r16(ad) & ~(b >> 16) & 0xFFFFu, z = b & 0xFFFFu;
                ok = ct == 0x28 ? v == z : ct == 0x2A ? v != z : ct == 0x2C ? v > z : v < z;
            }
            if (!ok) skip = depth;
            continue;
        }
        if (ct == 0x06) {                                /* texte : b octets, sur les lignes suivantes */
            u32 nl = (b + 7) >> 3;
            if (!skip && i + 2 + nl * 2 <= nw) { const u8 *src = (const u8 *)&w[i + 2]; for (u32 k = 0; k < b; k++) gk_w8(ad + k, src[k]); }
            i += nl * 2;
            continue;
        }
        if (ct == 0x08) {                                /* ecriture en serie : 2 lignes */
            if (i + 3 >= nw) break;
            u32 c = w[i + 2], d = w[i + 3], sz = c >> 28, cnt = ((c >> 16) & 0xFFFu) + 1, step = c & 0xFFFFu, v = b;
            if (!skip) for (u32 k = 0; k < cnt; k++, ad += step, v += d) { if (sz == 0) gk_w8(ad, v); else if (sz == 1) gk_w16(ad, v); else gk_w32(ad, v); }
            i += 2;
            continue;
        }
        if (t == 0xC0) {                                 /* code execute a chaque image : b lignes, finit par blr */
            static u32 c0_synced = 0;
            u32 nl = b, code = (u32)&w[i + 2];
            if (!skip && nl && i + 2 + nl * 2 <= nw) {
                if (c0_synced != code) { gk_sync(code, nl * 8); c0_synced = code; }
                ((void (*)(void))code)();
            }
            i += nl * 2;
            continue;
        }
        if (t == 0xC2 || t == 0xC3) {                    /* code insere : b lignes d'instructions, la derniere finit par 00000000 */
            u32 nl = b, code = (u32)&w[i + 2];
            if (!skip && nl && i + 2 + nl * 2 <= nw && gk_ram(ad, 4) && !(ad & 3)) {
                u32 *last = (u32 *)&w[i + 1 + nl * 2];
                u32 bb = 0x48000000u | ((ad + 4 - (u32)last) & 0x03FFFFFCu), jj = 0x48000000u | ((code - ad) & 0x03FFFFFCu);
                if (*last == 0) { *last = bb; gk_sync(code, nl * 8); }
                if (*last == bb && *(volatile u32 *)ad != jj) gk_w32(ad, jj);
            }
            i += nl * 2;
            continue;
        }
        if (skip) continue;
        if (ct == 0x00) { u32 n = (b >> 16) + 1; for (u32 k = 0; k < n; k++) gk_w8(ad + k, b); }
        else if (ct == 0x02) { u32 n = (b >> 16) + 1; for (u32 k = 0; k < n; k++) gk_w16(ad + k * 2, b); }
        else if (ct == 0x04) gk_w32(ad, b);
    }
    if ((H.disc >> 16) < 0xFFFFu) H.disc += 0x10000u;   /* (journal : images avec cheats appliques, 16 bits hauts) */
}

/* ---------- v268 : succes (conditions sur la memoire du jeu, verifiees une fois par image) ----------
   Section juste apres les cheats (GC Station ne la met pas s'il y a des corrections "FIXT") : {"ACH1", n,
   n x {etat, image}, n x {termes, images, compte, termes x {type, adresse, decalage, valeur}}}. type = pointeur << 24 |
   taille (1, 2, 4) << 16 | masque << 8 : le masque dit quels resultats rendent le terme vrai (bit 0 : lu < valeur,
   bit 1 : egal, bit 2 : plus grand ; ex. >= = 6) ; pointeur : la valeur 32 bits lue a l'adresse, + decalage. Un succes
   est d'abord "arme" (sa condition a ete fausse au moins une fois : pas d'un coup au demarrage), puis obtenu quand
   elle reste vraie "images" fois de suite. Etat : 1 arme, 2 obtenu (image dans "image"). A chaque succes obtenu, les
   512 premiers octets de la section (en-tete et etats : 63 succes au plus) sont ecrits dans le 6e secteur du journal.
   (Le plus petit possible : le resident n'avait plus que 576 octets de libres.) */
#define NATRES_ACH 0x41434831u
static u32 *ach_p = 0, ach_dirty = 0;                     /* (ach_p : 0 = pas encore cherche, 1 = pas de succes) */
/* u32 ach_eval(u32 *section, u32 image) : verifie tous les succes ; rend 1 si l'un vient d'etre obtenu. En assembleur :
   meme taille avec le compilateur de la console et celui du banc d'essai (seulement les registres volatils, pas d'appel).
   r5 succes restants, r6 etat, r7 definition, r9 terme, r10 resultat, r11 type, r12 adresse, r0 valeur, r8 limite. */
u32 ach_eval(u32 *a, u32 vi);
__asm__(".pushsection .text\n.globl ach_eval\nach_eval:\n"
        "  lwz 5,4(3)\n  addi 6,3,8\n  slwi 7,5,3\n  add 7,6,7\n  li 3,0\n"   /* (GC Station : au moins 1 succes, 1 terme chacun) */
        ".Lach_a:\n"                                      /* un succes */
        "  lwz 8,0(7)\n  addi 9,7,12\n  li 10,1\n  mtctr 8\n"
        ".Lach_b:\n"                                      /* un terme */
        "  lis 8,0x0180\n  addi 8,8,-4\n  lwz 11,0(9)\n  lwz 12,4(9)\n  srwi. 0,11,24\n  beq .Lach_c\n"
        "  lwz 0,0(12)\n  lwz 12,8(9)\n  add 12,0,12\n"   /* pointeur (adresse verifiee par GC Station) : valeur lue + decalage */
        ".Lach_c:\n"
        "  addis 0,12,-0x8000\n  cmplw 0,8\n  li 0,0\n  bgt .Lach_v\n"   /* (hors de la memoire du jeu : 0) */
        "  rlwinm 8,11,16,29,31\n  cmpwi 8,2\n  lhz 0,0(12)\n  beq .Lach_v\n  lbz 0,0(12)\n  blt .Lach_v\n  lwz 0,0(12)\n"   /* taille 2, 1, 4 */
        ".Lach_v:\n"                                      /* bit 8 + (v >= c) + (v > c) du type */
        "  lwz 12,12(9)\n  li 8,8\n  cmplw 0,12\n  blt .Lach_m\n  li 8,9\n  beq .Lach_m\n  li 8,10\n"
        ".Lach_m:\n  srw 11,11,8\n  and 10,10,11\n  addi 9,9,16\n  bdnz .Lach_b\n"
        ".Lach_d:\n"                                      /* r10 & 1 : condition vraie */
        "  lwz 11,0(6)\n  andi. 0,11,2\n  bne .Lach_e\n"  /* (deja obtenu) */
        "  andi. 0,10,1\n  bne .Lach_t\n"
        "  li 11,1\n  li 0,0\n  stw 0,8(7)\n  b .Lach_s\n"   /* fausse : armee, compte a 0 */
        ".Lach_t:\n  cmpwi 11,0\n  beq .Lach_e\n"         /* (vraie mais pas encore armee) */
        "  lwz 12,8(7)\n  addi 12,12,1\n  stw 12,8(7)\n  lwz 0,4(7)\n  cmplw 12,0\n  blt .Lach_e\n"
        "  li 11,3\n  stw 4,4(6)\n  li 3,1\n"            /* obtenu : image notee */
        ".Lach_s:\n  stw 11,0(6)\n"
        ".Lach_e:\n  addi 6,6,8\n  mr 7,9\n  addic. 5,5,-1\n  bne .Lach_a\n  blr\n.popsection\n");
#if defined(__GNUC__) && !defined(__clang__)
#define ACH_OS __attribute__((optimize("Os")))                /* (resident compile en -O2 : ici, le plus petit) */
#else
#define ACH_OS
#endif
void ach_run(void) __attribute__((used)) ACH_OS;
void ach_run(void) {
    u32 *a = ach_p;
    if (!a) { a = (u32 *)H.chts; a += 1 + a[0]; ach_p = a = a[0] == NATRES_ACH ? a : (u32 *)1; }
    if ((u32)a == 1) return;
    ach_dirty |= ach_eval(a, H.vi_count);
    /* (bus pris par le jeu, carte pas encore demarree : a l'image suivante) */
    if (ach_dirty && di_ready && !exi_defer() && !nr_write_sd(H.log_sec + 5, (const u8 *)a)) ach_dirty = 0;
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
static u32 sp_until = 0, sp_next = 0, sp_kicks = 0;       /* v122 : vitesse d'un vrai lecteur (fin pas avant sp_until) */
static void x_kick(void) {                               /* v65 : vraie fin de commande du lecteur -> fonctions EXI du jeu */
    if (x_kicked || rd_on) return;                       /* (une lecture en fond reveille deja souvent) */
    if (wake_tick()) { x_kicked = 1; return; }           /* (v133 : sans le lecteur : a la prochaine interruption du jeu) */
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
/* v133 : image (VI) du dernier progres d'une lecture en fond, et de la derniere fin a donner au jeu */
static u32 rd_vi = 0, dp_vi = 0;
/* v171 : jeu fige (Crash Bandicoot GCBP7D, carte virtuelle) : tout ce qui decide si la fin d'un transfert de la
   carte arrive au jeu, ecrit a la place des textes dans le 2e secteur du journal (0x110, magique "VCRD") */
static void di_hook(u32 irq, u32 ctx);
static void vc_diag(void) {
    for (u32 i = 0x110; i < 0x200; i++) crashbuf[i] = 0;
    vcard_t *v = &vc[0];
    volatile u32 *t = OS_INT_TABLE, *e0 = HW6(0xCC006800u), *pi = (volatile u32 *)0xCC003000u;
    const u32 w[] = { 0x56435244u, v->on, v->csr, v->irq, v->cr, v->mar, v->len, v->pos, v->cmd, v->addr, v->status, v->irq_sw,
                      (u32)x_kicked | (u32)rd_on << 8 | (u32)di_game_pending << 16 | (u32)(wk_tick & 0xFF) << 24,
                      DI_REAL[0], DI_REAL[7], x_disp, *(volatile u32 *)0x800000C4u, *(volatile u32 *)0x800000C8u,
                      pi[0], pi[1], e0[0], e0[3], e0[5], e0[10], H.chan, t[9], t[10], t[21], di_orig, (u32)di_hook,
                      vc[1].on ? vc[1].irq | vc[1].csr << 16 : 0xFFFFFFFFu, rd_left, H.vi_count - rd_vi, H.vi_count - dp_vi };
    for (u32 i = 0; i < sizeof(w) / 4; i++) cw(0x110 + i * 4, w[i]);
}
/* lecture en fond : les morceaux suivants. 0 = pas finie, 1 = finie (TCINT), -1 = erreur (DEINT) */
static int rd_pump(void) {
    u32 t0 = tbl(), bud = wake_tick() ? RD_BUDGET * 2u : RD_BUDGET;   /* (v133 : sans le lecteur, moins souvent : plus par fois) */
    int err = 0, k = 0;
    if (rd_left && exi_defer()) { H.exi_wait++; rd_vi = H.vi_count; return 0; }   /* v145 : bus EXI pris par le jeu : plus tard */
    if (rd_left) do {                                    /* v88 : des morceaux de 2 Ko tant qu'on reste sous ~1,5 ms (v122 : rien si deja tout lu) */
        u32 n = rd_left > RD_CHUNK ? RD_CHUNK : rd_left;
        H.n_chunks++;
        if (nr_read((void *)rd_dst, rd_off, n)) { err = 1; nr_log_add(di[2], rd_off, n, 1 | (H.err & 0xFF) << 24); break; }
        cache_flush(rd_dst, n);
        rd_dst += n; rd_off += n; rd_left -= n;
        di[5] += n; di[6] -= n;
    } while (rd_left && ++k < (wk_tick == 1 ? 32 : 8) && tbl() - t0 < bud);   /* (v94 : 16 Ko au plus : la carte est plus rapide en DMA / 32 MHz) */
    rd_vi = H.vi_count;
    if (err) { rd_on = 0; di[0] |= 0x04; di[7] &= ~1u; return -1; }   /* DEINT : erreur, le jeu redemande */
    if (rd_left) return 0;                               /* (morceau suivant a la prochaine fin) */
    if ((H.opt & NRO_SPEED) && (int)(sp_until - tbl()) > 0 && ++sp_kicks < 20000u) return 0;   /* v122 : un vrai lecteur n'aurait pas fini */
    rd_on = 0; di[0] |= 0x10; di[7] &= ~1u;              /* TCINT : lecture terminee */
    nx_read_done(rd_dst0, rd_off0, rd_len0);
    return 1;
}
static void di_hook(u32 irq, u32 ctx) {
    trap_guard();
    int chain = di_game_pending;
    if (rd_on) {
        volatile u32 *r = DI_REAL;
        u32 dt = (tbl() - kick_tb) / 41u;                /* (us depuis le reveil) */
        H.kick_us += dt; if (dt > H.kick_max_us) H.kick_max_us = dt;
        r[0] = (r[0] & 0x2Au) | 0x54u;                   /* (vraie fin : acquittee ici, le jeu n'en sait rien) */
        if (rd_pump() == 0) { di_real_kick(); x_dispatch(ctx); return; }
        chain = 1;
    }
    if (!chain) DI_REAL[0] = (DI_REAL[0] & 0x2Au) | 0x54u;   /* (reveil pour la carte : le jeu n'attend rien du lecteur) */
    di_game_pending = 0; x_kicked = 0;
    x_dispatch(ctx);
    if (chain && di_orig) ((void (*)(u32, u32))di_orig)(irq, ctx);
    alive_tick();
    if (!H.vi_count && !rd_on && !di_game_pending && !wake_tick() && H.n_samp < 2000000u) {   /* v67 : pas encore d'image : on regarde ou en est le jeu */
        nr_sample(ctx);
        if (!(DI_REAL[7] & 1)) { x_kicked = 1; di_real_kick(); }
    }
}
static int nr_game_fn(u32 a) { return a >= 0x80003100u && a < 0x817F0000u; }
/* v133 : fin de commande jamais arrivee au jeu. Avec le vrai lecteur, le reveil passe par une vraie commande :
   si son interruption se perd (fonction du jeu remise, lecteur qui ne repond pas), une lecture en fond
   s'arretait au milieu, ou la fin d'une commande n'etait jamais donnee : le jeu attendait pour toujours
   (ecran noir, le jeu tourne encore : Mario Superstar Baseball). di_tick est appele depuis les interruptions du
   jeu (image, son, DSP, dessin, manettes), apres sa fonction :
    - sans le lecteur (wake_tick) : la lecture en fond avance ici, et chaque fin est donnee aussitot ;
    - avec : secours, a chaque image : rien depuis 3 images, lecteur au repos -> la lecture continue d'ici et la
      fin est donnee au jeu ; apres 3 secours (ou un lecteur bloque 2 s), on passe pour de bon "sans le lecteur".
   Jamais si le jeu a masque l'interruption du lecteur (masques du SDK : 0x800000C4 global, 0x800000C8 local). */
#define OS_INT_MASKS (*(volatile u32 *)0x800000C4u | *(volatile u32 *)0x800000C8u)
static u32 wd_n = 0, busy_vi = 0;
static void di_tick(u32 ctx, int vi) {
    if (exec_on) return;
    int rescue = 0;
    if (!wake_tick()) {
        if (!vi) return;
        if (DI_REAL[7] & 1) {                            /* (vrai lecteur occupe : son reveil arrive... sauf s'il est bloque) */
            if (!busy_vi) busy_vi = H.vi_count | 1u;
            else if (H.vi_count - busy_vi > 120u) { wk_tick = 1; nr_log_add(0xDE000001u, DI_REAL[2], DI_REAL[0], H.vi_count); nr_log_flush(); }
            return;
        }
        busy_vi = 0;
        if (rd_on ? H.vi_count - rd_vi < 3u : (!di_game_pending || H.vi_count - dp_vi < 3u)) return;
        rescue = 1;
    }
    if (OS_INT_MASKS & (0x80000000u >> 21)) return;      /* (le jeu ne veut pas de l'interruption du lecteur pour l'instant) */
    if (rd_on) {
        int r = rd_pump();
        if (rescue) { nr_log_add(0xDD000000u | (r < 0 ? 2u : (u32)r), rd_off, rd_left, H.vi_count); H.n_rescue++; }
        if (r == 0) { if (!wake_tick()) di_real_kick(); return; }
    } else if (!di_game_pending) { if (vc_pending()) x_dispatch(ctx); return; }
    else if (rescue) { nr_log_add(0xDD000003u, H.last_off, H.last_len, H.vi_count); H.n_rescue++; }
    if (rescue && ++wd_n >= 3 && !wk_tick) { wk_tick = 1; nr_log_add(0xDE000000u, wd_n, 0, H.vi_count); }
    if (rescue) nr_log_flush();
    di_game_pending = 0; x_kicked = 0;
    x_dispatch(ctx);
    volatile u32 *t = OS_INT_TABLE;
    u32 f = t[21] == (u32)di_hook ? di_orig : t[21];
    if (nr_game_fn(f)) ((void (*)(u32, u32))f)(21, ctx);
}
/* ======================= v139 : Pulse universel (option par jeu, experimental) =======================
   D'habitude, GC Station trouve les acces du jeu aux registres du lecteur dans son code et les remplace par
   des aiguillages (natif.c ; le resident en ajoute pendant le jeu). Ce qui echappe a cette recherche (adresse
   lue en memoire, code charge ou decompresse plus tard) touchait le vrai lecteur. Avec cette option, c'est le
   processeur qui les attrape : la page des registres DI / SI / EXI / AI (0xCC006000) n'est plus traduite,
   donc chaque acces du jeu a cette page provoque une exception DSI, que le resident traite comme un aiguillage
   (meme faux lecteur, memes cartes virtuelles, memes manettes), puis le jeu continue a l'instruction suivante.
   - La zone 0xC0000000 (memoire sans cache) etait couverte par un seul BAT de 256 Mo : il est reduit a 32 Mo
     (la memoire), un 2e BAT couvre l'image (EFB, 0xC8000000), et les pages des registres sont decrites une par
     une dans une table des pages (64 Ko, HTABORG = la place du resident ; seuls 16 groupes nous servent, dans
     0x817FFC00, laisse libre par GC Station : le segment 0xC a un VSID choisi pour que les pages tombent la).
   - Le resident lui-meme passe par une 2e adresse de cette page (0xCC406000, hw_d), traduite normalement.
   - Vecteur 0x300 : la 1re instruction du SDK est remplacee par un saut vers trap_vec (en adresse physique :
     les exceptions demarrent sans traduction). Ce qui n'est pas pour nous repart dans le vecteur du SDK.
   Refuse (trap_state) si le jeu a deja sa propre table des pages ou un autre BAT dans cette zone. */
static u32 trap_regs[40] __attribute__((aligned(32), used));   /* r0-r31, CR, LR, CTR, XER, SRR0, SRR1, DAR, DSISR */
#define trap_pteg ((u32 *)(NATRES_AT + NATRES_PTEG))              /* 12 groupes de 8 entrees de la table des pages (place laissee par GC Station) */
#define TRAP_PTEG_LEN 0x300u
static u32 trap_vsid = 0;
extern u32 trap_orig[];
void trap_c(void) __attribute__((used));
/* vecteur (sans traduction : adresses physiques) */
__asm__(".pushsection .text\n.globl trap_vec\n.globl trap_orig\n.globl trap_entry\n.p2align 5\ntrap_vec:\n"
        "  mtspr 274,3\n  mtspr 275,4\n  mfcr 3\n"
        "  lis 4,trap_regs@ha\n  addi 4,4,trap_regs@l\n  clrlwi 4,4,2\n  stw 3,128(4)\n"
        "  mfspr 3,18\n  andis. 3,3,0x4000\n  beq 9f\n"                      /* DSISR : pas de traduction trouvee */
        "  mfspr 3,19\n  rlwinm 3,3,0,0,19\n  addis 3,3,0x3400\n  cmplwi 3,0x6000\n  bne 9f\n"   /* DAR dans 0xCC006000 */
        "  mfspr 3,26\n  srwi 3,3,25\n  cmplwi 3,0x40\n  bne 9f\n"           /* code du jeu en 0x80000000-0x81FFFFFF */
        "  stw 0,0(4)\n  stw 1,4(4)\n  stw 2,8(4)\n  stmw 5,20(4)\n"
        "  mfspr 3,274\n  stw 3,12(4)\n  mfspr 3,275\n  stw 3,16(4)\n"
        "  mflr 3\n  stw 3,132(4)\n  mfctr 3\n  stw 3,136(4)\n  mfxer 3\n  stw 3,140(4)\n"
        "  mfspr 3,26\n  stw 3,144(4)\n  mfspr 3,27\n  stw 3,148(4)\n  mfspr 3,19\n  stw 3,152(4)\n  mfspr 3,18\n  stw 3,156(4)\n"
        "  addi 1,1,-512\n  clrrwi 1,1,4\n"                                    /* (sur la pile du jeu, plus bas) */
        "  lis 3,trap_entry@ha\n  addi 3,3,trap_entry@l\n  mtspr 26,3\n"
        "  mfspr 3,27\n  rlwinm 3,3,0,17,15\n  mtspr 27,3\n  rfi\n"            /* (traduction comme le jeu, sans EE) */
        "9:\n  lwz 3,128(4)\n  mtcrf 0xFF,3\n  mfspr 3,274\n  mfspr 4,275\n"
        "trap_orig:\n  nop\n  ba 0x304\n"
        /* (avec traduction) : traitement, puis retour a l'instruction suivante du jeu */
        "trap_entry:\n  bl trap_c\n"
        "  lis 31,trap_regs@ha\n  addi 31,31,trap_regs@l\n"
        "  lwz 0,144(31)\n  mtspr 26,0\n  lwz 0,148(31)\n  mtspr 27,0\n"
        "  lwz 0,128(31)\n  mtcrf 0xFF,0\n  lwz 0,132(31)\n  mtlr 0\n  lwz 0,136(31)\n  mtctr 0\n  lwz 0,140(31)\n  mtxer 0\n"
        "  lwz 0,0(31)\n  lwz 1,4(31)\n  lwz 2,8(31)\n"
        "  .irp r,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n  lwz \\r,(\\r*4)(31)\n  .endr\n"
        "  lwz 31,124(31)\n  rfi\n.popsection\n");
static void trap_sync(u32 a) { __asm__ volatile("dcbst 0,%0; sync; icbi 0,%0; sync; isync" :: "r"(a) : "memory"); }
/* registres de la page, en 32 bits : les memes fonctions que les aiguillages */
static u32 __attribute__((noinline)) trap_rd(u32 a) {
    u32 o = a & 0xFFFu;
    if (o < 0x40u) return di_load(a);
    if (o >= 0x400u && o < 0x440u) return si_load(a);
    if (o >= 0x800u && o < 0x828u) return exi_load(a);
    return *HW6(a);
}
static void __attribute__((noinline)) trap_wr(u32 a, u32 v) {
    u32 o = a & 0xFFFu;
    if (o < 0x40u) { di_store(a, v); return; }
    if (o >= 0x400u && o < 0x440u) { si_store(a, v); return; }
    if (o >= 0x800u && o < 0x828u) { exi_store(a, v); return; }
    *HW6(a) = v;
}
static int trap_emul(u32 a) { u32 o = a & 0xFFFu; return o < 0x40u || (o >= 0x400u && o < 0x440u) || (o >= 0x800u && o < 0x828u); }
static u32 bswap(u32 v, int n) { return n == 4 ? v >> 24 | (v >> 8 & 0xFF00u) | (v << 8 & 0xFF0000u) | v << 24 : (v >> 8 & 0xFFu) | (v << 8 & 0xFF00u); }
/* formes prises en charge : taille (1, 2, 4) | 8 lecture | 16 mise a jour de rA | 32 signe | 64 octets inverses */
static const unsigned short trap_xo[18] = { 23, 55, 87, 119, 151, 183, 215, 247, 279, 311, 343, 375, 407, 439, 534, 662, 790, 918 };
static const u8 trap_xf[18] = { 4|8, 4|8|16, 1|8, 1|8|16, 4, 4|16, 1, 1|16, 2|8, 2|8|16, 2|8|32, 2|8|32|16, 2, 2|16, 4|8|64, 4|64, 2|8|64, 2|64 };
static const u8 trap_df[14] = { 4|8, 4|8|16, 1|8, 1|8|16, 4, 4|16, 1, 1|16, 2|8, 2|8|16, 2|8|32, 2|8|32|16, 2, 2|16 };   /* (lwz ... sthu) */
void trap_c(void) {
    u32 *r = trap_regs, pc = r[36], ins = *(volatile u32 *)pc, ea = r[38];
    u32 op = ins >> 26, rd = (ins >> 21) & 31, ra = (ins >> 16) & 31, f = 0;
    if (op == 31) { u32 xo = (ins >> 1) & 0x3FF; for (int i = 0; i < 18; i++) if (trap_xo[i] == xo) f = trap_xf[i]; }
    else if (op >= 32 && op <= 45) f = trap_df[op - 32];
    int n = (int)(f & 7), ld = f & 8, up = f & 16, sg = f & 32, rv = f & 64;
    H.trap_n++; H.trap_pc = pc;
    r[36] = pc + 4;
    if (!n) { H.trap_bad = ins; return; }                 /* (lmw, dcb*... : saute, note dans le journal) */
    u32 a4 = ea & ~3u, sh = (4u - (ea & 3u) - (u32)n) * 8u, m = n == 4 ? 0xFFFFFFFFu : (1u << (n * 8)) - 1u;
    if (ld) {
        u32 v;
        if (n == 4 || trap_emul(ea)) v = (trap_rd(a4) >> sh) & m;
        else v = n == 2 ? *(volatile unsigned short *)(ea + hw_d) : *(volatile u8 *)(ea + hw_d);
        if (rv) v = bswap(v, n);
        if (sg && (v & 0x8000u)) v |= 0xFFFF0000u;
        r[rd] = v;
    } else {
        u32 v = r[rd] & m;
        if (rv) v = bswap(v, n);
        if (n == 4) trap_wr(a4, v);
        else if (trap_emul(ea)) trap_wr(a4, (trap_rd(a4) & ~(m << sh)) | v << sh);   /* (comme les aiguillages) */
        else if (n == 2) *(volatile unsigned short *)(ea + hw_d) = (unsigned short)v;
        else *(volatile u8 *)(ea + hw_d) = (u8)v;
    }
    if (up && ra) r[ra] = ea;
}
static void trap_pte(u32 page, u32 phys) {               /* page = adresse (0xCC0xx000) -> phys, sans cache, garde */
    u32 pi = (page >> 12) & 0xFFFFu, hash = (trap_vsid ^ pi) & 0x3FFu;
    u32 *g = (u32 *)(NATRES_AT + hash * 64u);
    if ((u32)g < (u32)trap_pteg || (u32)g >= (u32)trap_pteg + TRAP_PTEG_LEN) return;
    for (int i = 0; i < 8; i++) if (!(g[i * 2] & 0x80000000u)) {
        g[i * 2 + 1] = (phys & 0xFFFFF000u) | 0x180u | 0x28u | 2u;   /* R, C, WIMG = I + G, lecture / ecriture */
        g[i * 2] = 0x80000000u | trap_vsid << 7 | (pi >> 10);
        return;
    }
}
static int trap_pteg_clear(u32 hash) {                   /* groupe secondaire (hors de trap_pteg) : rien qui nous ressemble */
    const u32 *g = (const u32 *)(NATRES_AT + (hash & 0x3FFu) * 64u);
    for (int i = 0; i < 8; i++) if ((g[i * 2] & 0x80000040u) == 0x80000040u && ((g[i * 2] >> 7) & 0xFFFFFFu) == trap_vsid) return 0;   /* (V, H = 1, notre VSID) */
    return 1;
}
static void __attribute__((noinline)) trap_arm(void) {
    u32 msr, m = irq_off();
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    volatile u32 *vec = (volatile u32 *)0x80000300u;
    u32 w = vec[0], o = w >> 26;
    if (o == 16 || o == 18 || (o == 19 && (((w >> 1) & 0x3FF) == 16 || ((w >> 1) & 0x3FF) == 528))) { H.trap_state = NRT_VEC; irq_restore(m); return; }
    trap_mmu = (msr & 0x10u) != 0;                       /* (banc d'essai : sans traduction, pas de BAT a changer) */
    if (trap_mmu) {
        u32 b1u = MFSPR(538), b2u = MFSPR(540), sdr = MFSPR(25);
        if ((b1u & 0xFFFE1FFFu) != 0xC0001FFFu) { H.trap_state = NRT_BAT; irq_restore(m); return; }   /* (256 Mo en 0xC0000000) */
        if (b2u & 3u) { H.trap_state = NRT_BAT2; irq_restore(m); return; }
        if (sdr) { H.trap_state = NRT_MMU; irq_restore(m); return; }   /* (le jeu a sa table des pages : Rogue Squadron...) */
        trap_vsid = 0x00A50000u | (NATRES_PTEG / 64u);
        for (u32 i = 0; i < TRAP_PTEG_LEN / 4; i++) trap_pteg[i] = 0;
        for (u32 k = 0; k < 9; k++) if (k != 6 && k != 7) trap_pte(0xCC000000u + k * 0x1000u, 0x0C000000u + k * 0x1000u);
        trap_pte(0xCC406000u, 0x0C006000u);              /* (la 2e adresse, pour le resident) */
        if (!trap_pteg_clear(~(trap_vsid ^ 0xC006u)) || !trap_pteg_clear(~(trap_vsid ^ 0xC007u))) { H.trap_state = NRT_PTE; irq_restore(m); return; }
        for (u32 a = (u32)trap_pteg; a < (u32)trap_pteg + TRAP_PTEG_LEN; a += 32) __asm__ volatile("dcbf 0,%0" :: "r"(a) : "memory");
        __asm__ volatile("sync; isync" ::: "memory");
        __asm__ volatile("mtsr 12,%0" :: "r"(trap_vsid) : "memory");
        MTSPR(25, 0x017F0000u);                          /* SDR1 : table de 64 Ko a la place du resident */
        MTSPR(541, 0x0800002Au); MTSPR(540, 0xC80001FFu); /* DBAT2 : image (EFB), 16 Mo, sans cache */
        __asm__ volatile("isync" ::: "memory");
        trap_br = 0xC00003FFu;
        MTSPR(538, trap_br);                             /* DBAT1 : 32 Mo (la memoire) : 0xCC000000 passe par la table */
        __asm__ volatile("isync" ::: "memory");
        for (u32 a = 0xCC000000u; a < 0xCC009000u; a += 0x1000u) __asm__ volatile("tlbie %0" :: "r"(a) : "memory");
        __asm__ volatile("tlbie %0; sync; isync" :: "r"(0xCC406000u) : "memory");
    }
    hw_d = 0x00400000u;
    exi = (volatile u32 *)((u32)exi + hw_d);
    trap_orig[0] = w; trap_sync((u32)trap_orig);
    vec[0] = TRAP_BR;
    trap_sync(0x80000300u);
    H.trap_state = NRT_ON;
    irq_restore(m);
}
static void __attribute__((noinline)) trap_disarm(void) {                          /* (retour a GC Station ; trap_state reste : journal) */
    if (!hw_d) return;
    u32 m = irq_off();
    volatile u32 *vec = (volatile u32 *)0x80000300u;
    if (vec[0] == TRAP_BR) { vec[0] = trap_orig[0]; trap_sync(0x80000300u); }
    if (trap_mmu) {
        MTSPR(538, 0xC0001FFFu);
        __asm__ volatile("isync" ::: "memory");
        MTSPR(540, 0); MTSPR(541, 0); MTSPR(25, 0);
        __asm__ volatile("mtsr 12,%0; isync" :: "r"(0x80000000u) : "memory");
    }
    exi = (volatile u32 *)((u32)exi - hw_d);
    hw_d = 0;
    irq_restore(m);
}
static void trap_fix(void) {
    volatile u32 *vec = (volatile u32 *)0x80000300u;
    if (trap_mmu && MFSPR(538) != trap_br) {             /* (un autre programme du disque a remis ses BAT : on suit) */
        u32 m = irq_off();
        exi = (volatile u32 *)((u32)exi - hw_d); hw_d = 0;
        H.trap_state = NRT_OFF;
        irq_restore(m);
        if (nr_game_fn(OS_INT_TABLE[21])) trap_arm();
        return;
    }
    if (vec[0] != TRAP_BR) { trap_orig[0] = vec[0]; trap_sync((u32)trap_orig); vec[0] = TRAP_BR; trap_sync(0x80000300u); }   /* (vecteurs remis par le jeu) */
}
static void trap_check(void) {
    if (!(H.opt & NRO_TRAP)) return;
    if (H.trap_state == NRT_OFF) { if (nr_game_fn(OS_INT_TABLE[21])) trap_arm(); return; }
    trap_guard();
}
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
    trap_check();                                        /* v139 */
    hook1(21, &di_orig, (u32)di_hook);                   /* v61 : DI (lectures en fond) ; v133 : aussi pendant une lecture en fond (avant :
                                                            si le jeu remettait sa fonction pendant ce temps, la lecture ne finissait jamais) */
    hook1(24, &vi_orig, (u32)vi_hook);
    hook1(5, &ai_orig, (u32)ai_hook);                    /* (DSP_AI : DMA du son) */
    hook1(7, &dsp_orig, (u32)dsp_hook);
    hook1(18, &pet_orig, (u32)pet_hook);
    /* v244 : plus de crochet sur la fin de dessin (19) : LEGO Star Wars II restait sur un ecran noir, son fil principal
       attendant pour toujours cette interruption (GXWaitDrawDone). Le jeton (18) suffit pour les signes de vie. */
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
    u32 val[32], known = 0, end = (H.tramp_at ? (H.tramp_end ? H.tramp_end : H.ext2 && H.ext2 < H.ext ? H.ext2 : H.ext) : 0);   /* (v159 : table hors du resident) */   /* (v97 : la table des morceaux est au-dessus ; v121 : les deux disques) */
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
                if (ea >= 0xCC006000u && ea < 0xCC006040u && !(H.tramp_at && H.tramp_at + 64 * 4 <= end) && H.exec_n < 0xFFFF0000u) H.exec_n += 0x10000u;   /* v253 : pas de place (16 bits hauts) */
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
/* une lecture du .dol suivi (xdol) : les morceaux de ses sections de code branches a leur adresse finale */
static void nx_dol_sections(u32 dst, u32 off, u32 len) {
    for (int i = 0; i < 7; i++) {
        u32 so = xdol.off + xdol.so[i], ss = xdol.ss[i];   /* (position dans l'ISO = debut du .dol + position dans le .dol) */
        if (!ss || off >= so + ss || off + len <= so) continue;
        u32 a0 = off > so ? off : so, a1 = off + len < so + ss ? off + len : so + ss;
        nx_patch2(dst + (a0 - off), xdol.sa[i] + (a0 - so), a1 - a0);
    }
}
/* v251 : un .dol entier lu d'un coup par le jeu (chargeur maison : Phantasy Star Online lit son vrai programme puis
   le recopie lui-meme a sa place) : ses sections de code sont branchees dans le tampon, avec leur adresse finale. */
static int nx_whole_dol(u32 dst, u32 off, u32 len) {
    if (len < 0x200 || (dst & 3) || dst < 0x80003000u || dst + len > 0x817F0000u) return 0;
    u32 entry = rbe(dst + 0xE0), nt = 0;
    int text = 0;
    for (int i = 0; i < 18; i++) {
        u32 so = rbe(dst + i * 4), sa = rbe(dst + 0x48 + i * 4), ss = rbe(dst + 0x90 + i * 4);
        if (!ss) continue;
        if (so < 0x100 || so + ss > len || ss > 0x01800000u || sa < 0x80003000u || sa + ss > 0x81800000u || (sa & 3) || (so & 3)) return 0;
        if (i < 7 && entry >= sa && entry < sa + ss) text = 1;
    }
    if (!text) return 0;
    u32 n0 = H.tramp_n;
    for (int i = 0; i < 7; i++) {
        u32 so = rbe(dst + i * 4), sa = rbe(dst + 0x48 + i * 4), ss = rbe(dst + 0x90 + i * 4);
        if (ss) { nx_patch2(dst + so, sa, ss); nt++; }
    }
    H.exec_n++; H.exec_last = entry;
    nr_log_add(0xEC000000u | (H.tramp_n - n0), off, len, entry);
    nr_log_flush();
    return (int)nt;
}
/* une lecture terminee (dst = memoire, off / len = ISO) : le code de redemarrage, puis tout ce qu'il charge */
static void nx_read_done(u32 dst, u32 off, u32 len) {
    if (!exec_on && !exec_a && nx_whole_dol(dst, off, len)) return;   /* v251 */
    if (!exec_on && !exec_a && nx_dol(dst, off, len)) {   /* v252 : un .dol relu alors qu'il tourne deja (son propre main.dol) : rien a brancher */
        u32 so0 = xdol.so[0], sa0 = xdol.sa[0];
        int same = xdol.ss[0] >= 64 && so0 + 64 <= len;
        for (u32 k = 0; same && k < 16; k++) if (rbe(dst + so0 + k * 4) != rbe(sa0 + k * 4)) same = 0;
        if (same) { xdol.ok = 0; return; }
    }
    if (!exec_on && !exec_a && xdol.ok) {                 /* v252 : un .dol lu par morceaux par le jeu (PsoV3.dol de PSO) */
        if (H.exec_last != xdol.off) { H.exec_n++; nr_log_add(0xEB000000u | (H.tramp_n & 0xFFFFFFu), off, len, xdol.off); nr_log_flush(); }
        H.exec_last = xdol.off;
        nx_dol_sections(dst, off, len);
        return;
    }
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
    nx_dol(dst, off, len);                                /* en-tete du programme suivant (v252 : et la suite de cette lecture) */
    if (xdol.ok) { nx_dol_sections(dst, off, len); return; }
    if (exec_on) nx_patch(dst & ~3u, len);                /* (ancien apploader : ce qu'il charge arrive a sa place) */
}
/* ======================= v121 : jeux a 2 disques =======================
   Pour changer de disque, la bibliotheque du jeu arrete le moteur (commande E3), active l'interruption du
   couvercle (DICVR, masque) et attend qu'on ouvre puis referme le couvercle. Ici : une fois le masque pose
   (et au moins une seconde apres E3), la table des morceaux de l'autre disque prend la place de celle-ci, et
   le jeu recoit l'interruption "couvercle referme" (CVRINT) : il relit l'ID du disque et trouve l'autre. */
static u32 sw_pending = 0, sw_vi = 0;
static void disc_swap(void) {
    u32 e = H.ext, n = H.n_ext;
    H.ext = H.ext2; H.n_ext = H.n_ext2; H.ext2 = e; H.n_ext2 = n;
    ext_last = 0; sec_cached = 0xFFFFFFFFu;
    st_on = 0; st_stop_end = 0; st_sec_off = 0xFFFFFFFFu;  /* (musique du disque : arretee, comme un vrai changement) */
    H.disc = ((H.disc & 1u) ^ 1u) | ((H.disc & 0xFFFFFF00u) + 0x100u);
    di[1] = (di[1] & ~1u) | 4u;                           /* couvercle ferme + CVRINT */
    nr_log_add(0xE3D15C00u | (H.disc & 1u), H.n_ext, H.disc, 0);
    nr_log_flush();
    di_game_pending = 1; dp_vi = H.vi_count; di_real_kick();   /* (reveil : la fonction DI du jeu voit le couvercle) */
}
static void sw_tick(void) {
    if (!sw_pending || !(di[1] & 2u) || rd_on || H.vi_count - sw_vi < 60u) return;
    if (DI_REAL[7] & 1) return;                           /* (le vrai lecteur doit pouvoir reveiller le jeu) */
    sw_pending = 0;
    disc_swap();
}
static void di_exec(void) {
    u32 info = 0;
    rd_on = 0;                                           /* (nouvelle commande : une lecture en fond oubliee s'arrete) */
    if (!di_ready) { di_ready = nr_init() == 0; if (!di_ready) info |= 8; }   /* 1er acces du jeu : on demarre la carte SD */
    hooks_check();
    u32 cmd = di[2] >> 24, len = di[6], dst = di[5] | 0x80000000u;
    H.ncmd++; H.last_cmd = di[2]; H.last_off = di[3] << 2; H.last_len = len;
    di[8] = 0;
    if (cmd == 0xA8) sw_pending = 0;                     /* (v121 : le jeu relit : ce n'etait pas un changement de disque) */
    if (cmd == 0xE3 && H.n_ext2) { sw_pending = 1; sw_vi = H.vi_count; }   /* v121 : moteur arrete : le jeu va demander l'autre disque */
    if (cmd == 0xA8) {
        u32 off = (di[2] & 0xC0) == 0x40 ? 0 : di[3] << 2;   /* A8000040 : ID du disque (debut de l'ISO) */
        if ((di[2] & 0xC0) == 0x40 && len > 0x20) len = 0x20;
        H.last_off = off; H.last_len = len;
        int slow = (H.opt & NRO_SPEED) != 0;             /* v122 : vitesse d'un vrai lecteur : toutes les lectures en fond */
        int busy = len && exi_defer();         /* v145 : bus EXI pris par le jeu (SD Gecko + BBA...) : en fond, attendre */
        if (busy) H.exi_sync++;
        if ((len > RD_CHUNK || (slow && len) || busy) && di_orig && !exec_on) {   /* v61 : grosse lecture : en fond */
            if (slow) {                                  /* (deplacement de la tete si loin, puis ~3 Mo/s) */
                u32 d = off > sp_next ? off - sp_next : sp_next - off;
                sp_until = tbl() + (d > 0x100000u ? TB_SEC / 1000u * 60u : TB_SEC / 1000u * 1u) + (len / 3000u) * (TB_SEC / 1000u);
                sp_kicks = 0; sp_next = off + len;
            }
            rd_dst = dst; rd_off = off; rd_left = len; rd_on = 1; rd_vi = H.vi_count;
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
    if (!exec_on) { di_game_pending = 1; dp_vi = H.vi_count; info |= di_real_kick(); }   /* (v95 : pendant OSExec, personne n'attend d'interruption) */
    nr_log(H.last_off, H.last_len, info | (di[0] & 0xFF) << 8);
}

static u32 hk_n = 0;
/* v166 : jeu fige avant sa 1re interruption (Crash Bandicoot : aucune trace) : tant qu'aucune fonction d'image ou de
   manettes n'a tourne, chaque acces aiguille est compte et, 3 s apres le 1er, le journal est ecrit (puis toutes les
   3 s) avec le dernier registre touche. Rien ne change pour un jeu qui a deja ses interruptions (tous les autres). */
static u32 en_n = 0, en_t0 = 0;
static void early_note(u32 a, u32 v, u32 st) {
    if (H.vi_in || H.si_in || !H.log_sec) return;
    en_n++;
    u32 t = tbl();
    if (!en_t0) { en_t0 = t; return; }
    if (t - en_t0 < TB_SEC * 3) return;
    en_t0 = t;
    nr_log_add(0xEE000000u | (st << 23) | (en_n & 0x7FFFFFu), a, v, H.ncmd);
    nr_log_flush();
}
static void hooks_lazy(void) { trap_guard(); if (H.trap_state == NRT_OFF && (H.opt & NRO_TRAP)) trap_check(); if (++hk_n <= 64 || (hk_n & 255) == 1) hooks_check(); }   /* v164 : (les 64 premiers acces : le jeu peut se figer tot) */   /* v62 : le jeu installe ses fonctions apres sa 1re commande */
static u32 di_load(u32 reg) {
    u32 i = (reg & 0x3F) >> 2;
    hooks_lazy();
    early_note(reg, 0, 0);
    if (i == 1) return di[1] & 0x07;                      /* couvercle ferme ; v121 : CVRINT apres un changement de disque */
    if (i == 9) return DI_REAL[9];                        /* DICFG : celui du vrai lecteur */
    return i < 10 ? di[i] : 0;
}

static void di_store(u32 reg, u32 val) {
    u32 i = (reg & 0x3F) >> 2;
    hooks_lazy();
    early_note(reg, val, 1);
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
    if (i == 1) { di[1] = (di[1] & 1u) | (val & 2u) | (di[1] & 4u & ~val); DI_REAL[1] = 4u; return; }   /* (vrai couvercle : acquitte, sans interruption ; v121 : CVRINT efface par 1) */
    if (i == 7) {
        di[7] = val;
        if (val & 1) { u32 m = irq_off(); di_exec(); irq_restore(m); }
        return;
    }
    if (i < 10) di[i] = val;
}

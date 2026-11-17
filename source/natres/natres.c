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
/* v327 : MEM_OS (le plus petit, -Os) aussi pour ce qui ne sert qu'au demarrage, au reseau ou au plantage : le resident
   doit tenir sous 0x817FFF00 (le pilote W5500 a ajoute ~1,5 Ko). v332 : aussi la carte virtuelle (lente de toute facon), le
   journal, le retour A+Z+START, la verification des crochets et les pages de NRO_TRAP (place du micro du telephone). v337 :
   aussi les aiguillages EXI / SI, les crochets image / manettes, l'envoi reseau : avec NRO_TRAP, le resident doit finir sous
   NATRES_HTAB + NATRES_PTEG (0x817FFC00 : la table des pages au-dessus), sinon GC Station lance le jeu sans Pulse */
#if defined(__GNUC__) && !defined(__clang__)
#define MEM_OS __attribute__((optimize("Os")))                /* (resident compile en -O2 : ici, le plus petit) */
#elif defined(__clang__)
#define MEM_OS __attribute__((minsize))                       /* (v409 : clang aussi : simulateur PC, banc d'essai) */
#else
#define MEM_OS
#endif

typedef unsigned int u32;
static u32 igr_tel = 0;                                   /* v418 : retour demande par le telephone (fait a la prochaine lecture) */
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
#if defined(NATRES_COMPACT)
static natres_ext_t card_tab[1], card2_tab[1];                  /* v474 : (compact : jamais de carte memoire virtuelle) */
#else
static natres_ext_t card_tab[NATRES_CARDMAX];            /* v65 : le fichier de la carte memoire virtuelle */
static natres_ext_t card2_tab[NATRES_CARD2MAX];           /* v122 : celui du slot B */
#endif
static u32 si_load(u32 a);
static u32 igr_gh[4], igr_gt = 0x80000000u;                            /* v457 : dernieres touches lues par le jeu (INBUFH), et quand */
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
static MEM_OS void __attribute__((noinline)) trap_guard(void) { if (hw_d && ((trap_mmu && MFSPR(538) != trap_br) || *(volatile u32 *)0x80000300u != TRAP_BR)) trap_fix(); }
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
void * MEM_OS memset(void *d, int c, unsigned long n) { u8 *p = (u8 *)d; while (n--) *p++ = (u8)c; return d; }
void * MEM_OS memcpy(void *d, const void *s, unsigned long n) { u8 *p = (u8 *)d; const u8 *q = (const u8 *)s; while (n--) *p++ = *q++; return d; }

/* ---------- bus EXI ---------- */
static volatile u32 *exi;
static u32 freq;
/* v145 : bus EXI partage. Avec un adaptateur SD dans un port de carte (SD Gecko : canal 0 ou 1), le jeu se sert
   du meme canal pour d'autres peripheriques : horloge / SRAM (canal 0, peripherique 1), Broadband Adapter
   (canal 0, peripherique 2 : jeux en reseau local), vraie carte memoire. S'il a choisi un autre peripherique
   (CS) ou si un transfert est en cours, parler a la carte SD couperait sa commande au milieu : on attend.
   (Swiss passe par EXILock du jeu ; ici on regarde l'etat du canal lui-meme.) */
static volatile u32 xv_st;                               /* (v444 : declaree ici pour exi_busy) */
static int exi_busy(void) {
    if (!exi || xv_st) return 0;                         /* (v444 : notre propre suite par interruptions n'est pas le jeu) */
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
/* build 1059 : carte d'un slot (GC2SD), comme le pilote de GC Station (build 1051) : la carte n'est choisie (CS) que pour
   envoyer ; tout ce qui se lit (reponse, jeton, blocs, CRC, attente) se lit horloge seule, carte non choisie. Le 32 MHz et le DMA
   rataient sur la carte de Platter quand elle restait choisie pendant les lectures. x_cs : bit CS de la carte choisie pour la commande en cours, 0 sinon */
static u32 x_nocs = 0, x_cs = 0;
static void ex_sel(int cs) {
    x_cs = cs ? 0x80u << H.dev : 0u;
    u32 v = (exi[0] & 0x405) | (freq << 4);
    if (cs) v |= 0x80u << H.dev;
    exi[0] = v;
}
static void ex_desel(void) { x_cs = 0; exi[0] = exi[0] & 0x405; }
/* v94 : CRC16 des blocs de donnees (CCITT, comme la carte) : sert a verifier le 32 MHz */
/* (build 1063 : table de 16 valeurs, par demi-octet, au lieu de 256 calculees au demarrage : 480 octets de place ; meme resultat) */
static const unsigned short crc_tab[16] = { 0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
                                            0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF };
static u32 crc16(const u8 *p, u32 n) {
    u32 c = 0;
    while (n--) { u32 b = *p++; c = ((c << 4) ^ crc_tab[((c >> 12) ^ (b >> 4)) & 15]) & 0xFFFF; c = ((c << 4) ^ crc_tab[((c >> 12) ^ b) & 15]) & 0xFFFF; }
    return c;
}
static int x_dma = 0, x_fast = 0, x_fast_off = 0, x_crcbad = 0;   /* (H.xfer) */
static u32 x_why = 0;                                    /* v432 : (H.xfer, bits 3-15) raison du DMA refuse */
static void ex_put(u32 v, int n) { if (x_nocs) exi[0] = (exi[0] & 0x475u) | x_cs; exi[4] = v; exi[3] = ((u32)(n - 1) << 4) | (1u << 2) | 1u; ex_wait(); }
static u32 ex_get(int n) { if (x_nocs) exi[0] &= 0x475u; exi[4] = 0xFFFFFFFFu; exi[3] = ((u32)(n - 1) << 4) | 1u; ex_wait(); return exi[4]; }
static u32 ex_get8(void) { return ex_get(1) >> 24; }

/* ---------- carte SD (SPI) ---------- */
static MEM_OS void sd_end(void) {                     /* fin de commande : CS relache + 8 coups d'horloge */
    ex_desel();
    ex_sel(0); ex_put(0xFF000000u, 1); ex_desel();
}
/* v488 : canal de la carte SD rien qu'a Pulse : port serie 2, ou slot B quand sa carte memoire virtuelle est imposee (le jeu ne
   touche jamais a ce canal). v489 : slot A aussi (son canal 0 sert aussi a l'horloge et aux reglages de la console : voir sd_yield). */
#define SD_OWN_CH (H.chan == 2 || (H.chan == 1 && (H.flags & NRF_CARDB)) || (H.chan == 0 && (H.flags & NRF_CARD)))   /* (v489 : slot A aussi :
                                                           le jeu qui touche au canal fait d'abord finir Pulse, sd_yield) */
static u32 sd_wbusy = 0;                           /* (build 1062 : ecriture envoyee, carte peut-etre encore occupee) */
static u32 st_open = 0, st_next = 0;               /* v434 : lecture en continu ouverte, prochain secteur */
static void st_close(void) __attribute__((noinline));
static volatile u32 xv_st;
static void xv_drain(void);
static MEM_OS u32 sd_cmd(u32 cmd, u32 arg, u32 crc) {  /* (v439 : -Os, place) */ /* envoie la commande, rend la reponse R1 (CS reste actif) */
    if (xv_st) xv_drain();                      /* v437 : (lecture par interruptions en cours : finie d'abord) */
    st_close();                                  /* v434 : (lecture en continu ouverte : arretee d'abord) */
    ex_sel(1);
    /* build 1062 : la carte finit d'enregistrer la derniere ecriture (occupee = 00) avant la commande suivante, pas a la fin de
       l'ecriture : le jeu repart pendant que la carte enregistre (Kirby Air Ride : ses ecritures de la carte memoire, 1 a 10 ms) */
    if (sd_wbusy) { for (u32 i = 0; i < 2000000u; i++) if (ex_get8() == 0xFF) break; sd_wbusy = 0; }
    ex_put(0xFF000000u, 1);
    ex_put(((0x40u | cmd) << 24) | (arg >> 8), 4);
    ex_put(((arg & 0xFFu) << 24) | (crc << 16), 2);
    u32 r = 0xFF;
    for (int i = 0; i < 16; i++) { r = ex_get8(); if (!(r & 0x80)) break; }
    H.r1 = r;
    return r;
}
#if !defined(NATRES_COMPACT)
/* build 1063 : la carte SD a-t-elle fini d'enregistrer la derniere ecriture ? (un octet lu, sans attendre) */
static MEM_OS __attribute__((noinline)) int sd_free(void) {
    if (!sd_wbusy) return 1;
    st_close(); ex_sel(1);
    if (ex_get8() == 0xFF) sd_wbusy = 0;
    sd_end();
    return !sd_wbusy;
}
#endif

/* v94 : DMA et 32 MHz, essayes une fois (le meme secteur lu en mode simple, en DMA, puis a 32 MHz,
   4 fois, CRC verifie) : gardes seulement si tout est identique */
static int nr_read_sd(u32 sector, void *dst);
static u8 dmabuf[512] __attribute__((aligned(32)));
static u32 x_dnocs = 0;                                  /* v494 : DMA sans la carte choisie pendant le bloc (2e facon) */
#define t_ref secbuf                                     /* v485 : le test emprunte le cache du secteur (vide par nr_init juste avant)
                                                           et le tampon du DMA : 1 Ko de moins (place du reveil provoque) */
#define t_buf dmabuf
static int t_same(void) { for (int i = 0; i < 512; i++) if (t_ref[i] != t_buf[i]) return 0; return 1; }
#if !defined(NATRES_COMPACT)
static MEM_OS void nr_speed(void) {
    static int tried = 0;
    u32 sec = H.n_ext ? ext_tab[0].sd : 0;
    if (tried) { x_fast = x_fast && !x_fast_off; if (x_fast) freq = 5; goto out; }
    tried = 1;
    x_dma = 0; x_fast = 1;                                  /* (CRC verifie pendant l'essai) */
    u32 slow = freq > 4 ? 4 : freq;
    freq = slow;
    if (nr_read_sd(sec, t_ref)) { x_fast = 0; goto done; }
    x_dma = 1;
    /* v432 : pourquoi le DMA est refuse (journal, champ xfer) : 08 lecture en erreur (code en bits 8-15), 10 donnees
       differentes (1re difference / 2 en bits 8-15), + 20 tout a 00, + 40 tout a FF (le DMA n'a rien recu) */
    if (nr_read_sd(sec, t_buf)) { x_dma = 0; x_why = 0x08u | (H.err & 0xFFu) << 8; }
    else if (!t_same()) {
        u32 i = 0, z = 1, ff = 1;
        while (t_ref[i] == t_buf[i]) i++;
        for (u32 k = 0; k < 512; k++) { if (t_buf[k]) z = 0; if (t_buf[k] != 0xFF) ff = 0; }
        x_dma = 0; x_why = 0x10u | (i >> 1) << 8 | z << 5 | ff << 6;
    }
    if (!x_dma) {                                        /* v494 : 2e essai : DMA sans la carte choisie pendant le bloc (comme Swiss) */
        x_dma = 1; x_dnocs = 1;
        if (nr_read_sd(sec, t_buf) || !t_same()) { x_dma = 0; x_dnocs = 0; }
        else x_why = 0x20u;                              /* (journal : avec DMA (bit 0) + 20 : la 2e facon) */
    }
    if (slow < 4 || (H.freq & 0x10u)) { x_fast = 0; goto done; }   /* v471 : carte d'un slot (comme Swiss) : la vitesse de GC Station, jamais 32 MHz */
    freq = 5;
    for (int a = 0, ok = 0; a < 3 && !ok; a++) {         /* v451 : 3 essais (avant : 1 ; Wind Waker, Paper Mario restaient a 16 MHz) ;
                                                            sans risque : chaque bloc lu a 32 MHz est verifie (CRC), aussi par interruptions */
        ok = 1; x_fast = 1; x_crcbad = 0;
        for (int k = 0; k < 4 && ok; k++) if (nr_read_sd(sec, t_buf) || !t_same()) ok = 0;
        x_fast = ok;
    }
    if (!x_fast) freq = slow;
done:
    H.err = NR_OK; H.crc_err = 0; x_crcbad = 0;
out:
    H.xfer = (u32)x_dma | (u32)x_fast << 1 | (u32)x_fast_off << 2 | x_why;
}
#else
static void nr_speed(void) { x_dma = 0; x_fast = 0; H.err = NR_OK; H.xfer = 0; }   /* (compact : ni DMA ni 32 MHz : pas d'essai) */   /* v474 : Pulse compact */
#endif
static int MEM_OS nr_init(void) {
    exi = (volatile u32 *)(0xCC006800u + hw_d + H.chan * 0x14u);
    H.err = NR_OK; H.sdhc = 0; H.stage = 1; x_nocs = 0;   /* (demarrage de la carte : a l'ancienne) */
    sec_cached = 0xFFFFFFFFu;
    st_open = 0;                                /* v434 : (carte redemarree : plus de lecture en continu) */
    u32 want = (H.freq & 7u) ? (H.freq & 7u) : 4;         /* (v471 : bit 0x10 = jamais 32 MHz, voir nr_speed) */
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
    x_nocs = (H.freq >> 5) & 1u;                   /* build 1059 : bit 0x20 = carte de slot lue carte non choisie (GC Station l'a verifie) */
    nr_speed();
    return 0;
}

/* v94 : les 512 octets d'un bloc (apres le jeton FE) puis son CRC. En DMA, le bus EXI copie le bloc
   tout seul en memoire (adresse alignee sur 32) : bien moins de travail pour le processeur. A 32 MHz,
   le CRC est verifie (un bloc faux = erreur : relu, et on repasse a 16 MHz). Rend 0 si bon. */
static MEM_OS void __attribute__((noinline)) dc_inv512(u8 *d) {   /* (v431 : 512 octets retires du cache, avant et apres le DMA) */
    __asm__ volatile("sync" ::: "memory");
    for (u32 a = (u32)d; a < (u32)d + 512; a += 32) __asm__ volatile("dcbi 0,%0" :: "r"(a) : "memory");
    __asm__ volatile("sync\n isync" ::: "memory");
}
static int __attribute__((noinline)) nr_block(u8 *o) {   /* (v494 : une seule copie : place) */
    u8 *d = o;
    if (x_dma) {
        if ((u32)o & 31) d = dmabuf;
        dc_inv512(d);
        exi[4] = 0xFFFFFFFFu;                     /* v493 : la carte doit recevoir des 1 (0xFF) pendant qu'elle envoie le bloc ;
                                                     avant, le registre gardait le dernier octet recu (le jeton FE) : bloc faux des la
                                                     position 8, DMA refuse au test (port serie 2 et slot B). Swiss le met aussi a FF */
        u32 cs = exi[0];
        if (x_dnocs) exi[0] = (cs & 0x405u) | (cs & 0x070u);   /* v494 : 2e facon (si la 1re rate au test) : pendant le DMA du bloc, la carte
                                                             n'est plus choisie (CS relache, horloge gardee), comme Swiss dans sa version DMA ;
                                                             1021 : donnees encore fausses des la position 18 avec la carte choisie */
        exi[1] = (u32)d & 0x1FFFFFE0u;
        exi[2] = 512;
        exi[3] = 3;                               /* DMA, lecture, depart */
        int w = ex_wait();
        if (x_dnocs) exi[0] = (cs & 0x405u) | (cs & 0x3F0u);   /* (la carte de nouveau choisie : CRC, suite) */
        if (w) return -1;
        /* v431 : le cache est vide une 2e fois APRES le transfert : pendant l'attente, le processeur peut deja lire ce tampon
           par anticipation (la suite du code : CRC, comparaison) et garder dans son cache l'ancien contenu de la memoire. Le
           test du DMA (meme secteur lu sans et avec DMA) voyait alors une difference et le DMA restait coupe (lecture lente :
           videos des jeux saccadees, journal : "sans DMA") */
        dc_inv512(d);
        if (d != o) for (int i = 0; i < 512; i++) o[i] = d[i];
    } else if (!((u32)o & 3)) {
        /* v435 : sans DMA (port serie 2), la boucle la plus courte possible : registres du canal en direct (pas d'appel par
           mot), un mot ecrit d'un coup, et une petite attente avant de regarder si le transfert est fini (le transfert de
           4 octets dure ~1 us a 32 MHz : chaque lecture d'un registre du bus coute cher, on n'en fait plus 3 ou 4 pour rien).
           Avant : ~3 us par mot, la video de Mario Kart (1,6 Mo/s) n'etait servie qu'a ~1,3 Mo/s */
        volatile u32 *e = exi;
        u32 *w = (u32 *)o, pause = freq >= 5 ? 30u : freq == 4 ? 70u : 0u;   /* (tics de 1/40,5 us : un peu moins que le transfert) */
        for (int i = 0; i < 128; i++) {
            e[4] = 0xFFFFFFFFu;
            e[3] = 0x31u;                                 /* 4 octets, lecture, depart */
            u32 t, t0, k = 0; __asm__ volatile("mftb %0" : "=r"(t0));   /* (et un nombre de tours : horloge arretee dans le banc d'essai) */
            do __asm__ volatile("mftb %0" : "=r"(t)); while (t - t0 < pause && ++k < 64u);
            u32 n = 0;
            while (e[3] & 1) if (++n > 1000000u) { H.err = NR_ETIMEOUT; return -1; }
            w[i] = e[4];
        }
    } else {
        for (int i = 0; i < 128; i++) {
            u32 w = ex_get(4);
            o[i * 4] = (u8)(w >> 24); o[i * 4 + 1] = (u8)(w >> 16); o[i * 4 + 2] = (u8)(w >> 8); o[i * 4 + 3] = (u8)w;
        }
    }
    u32 crc = ex_get(2) >> 16;
    if ((x_fast | x_dma) && crc16(o, 512) != crc)   /* v484 : en DMA aussi (carte d'un slot) */
        { H.crc_err++; x_crcbad = 1; H.err = NR_ETOKEN; return -1; }
    return 0;
}
static MEM_OS int nr_read_sd(u32 sector, void *dst) {   /* (v464 : -Os : attend surtout la carte) */
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
/* v434 : lecture en continu laissee ouverte entre deux lectures (st_open) : si la suivante commence au secteur d'apres
   (video du jeu, gros fichier lu par morceaux de 2 Ko), la carte continue d'envoyer ses blocs sans nouvelle commande ni
   arret. Avant, chaque morceau de 2 Ko payait une commande, l'attente du 1er bloc et l'arret (CMD12) : sans DMA (port
   serie 2), presque autant que la lecture elle-meme, et les videos saccadaient. Toute autre commande a la carte (sd_cmd)
   ferme d'abord la lecture en cours. */
static MEM_OS void st_close(void) {
    if (!st_open) return;
    st_open = 0;
    ex_put(0x4C000000u, 4);                           /* CMD12 : arret de la lecture */
    ex_put(0x00010000u, 2);
    ex_get8();                                        /* (octet de bourrage) */
    for (int i = 0; i < 16; i++) { if (!(ex_get8() & 0x80)) break; }
    for (u32 i = 0; i < 400000u; i++) if (ex_get8() == 0xFF) break;   /* (occupee : 00) */
    sd_end();
}
static MEM_OS int nr_read_multi(u32 sector, u32 count, u8 *o) {   /* (v434 : -Os : place) */
    if (xv_st) xv_drain();                            /* v437 */
    if (!(st_open && sector == st_next)) {
        u32 r = sd_cmd(18, H.sdhc ? sector : sector << 9, 0x01);   /* (ferme la lecture d'avant) */
        if (r != 0) { sd_end(); H.err = NR_ECMD; return -1; }
        st_open = 1;
    }
    for (u32 s = 0; s < count; s++) {
        u32 t = 0xFF;
        for (u32 i = 0; i < 400000u; i++) { t = ex_get8(); if (t != 0xFF) break; }
        if (t != 0xFE) { H.err = NR_ETOKEN; H.r1 = t; st_close(); return -1; }
        if (nr_block(o)) { st_close(); return -1; }
        o += 512;
        H.nread++;
    }
    st_next = sector + count;                         /* (la carte prepare deja le bloc suivant : on le prendra peut-etre) */
    if (!SD_OWN_CH) st_close();                       /* (SD Gecko dans un port de carte : le jeu partage ce canal, on le rend ; v488 : pas
                                                         en slot B avec la carte virtuelle imposee : le jeu n'y touche jamais) */
    return 0;
}

/* v54 : ecrire un secteur (CMD24) : seulement le journal, dans un fichier prepare par GC Station */
static int nr_write_sd1(u32 sector, const u8 *s);
static MEM_OS __attribute__((noinline)) int nr_write_sd(u32 sector, const u8 *s) {       /* v94 : ecriture toujours a 16 MHz au plus (pas de CRC verifie) */
    u32 f = freq;
    if (freq > 4) freq = 4;
    int r = nr_write_sd1(sector, s);
    freq = f;
    return r;
}
static MEM_OS int nr_write_sd1(u32 sector, const u8 *s) {
    u32 r = sd_cmd(24, H.sdhc ? sector : sector << 9, 0x01);
    if (r != 0) { sd_end(); return -1; }
    ex_put(0xFF000000u, 1);
    ex_put(0xFE000000u, 1);                       /* jeton de debut */
    for (int i = 0; i < 128; i++, s += 4) ex_put((u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3], 4);
    ex_put(0xFFFF0000u, 2);                       /* CRC (ignore en SPI) */
    u32 t = 0xFF;
    for (int i = 0; i < 16; i++) { t = ex_get8(); if (t != 0xFF) break; }
    sd_wbusy = 1;                                 /* (build 1062 : la carte ecrit ; attendu avant la commande suivante, sd_cmd) */
    sd_end();
    return (t & 0x1F) == 0x05 ? 0 : -1;
}

/* secteur de l'ISO -> secteur de la carte (v88 : *run = secteurs a la suite sur la carte) */
static u32 iso_run = 1;
static __attribute__((noinline)) int iso_to_sd(u32 isec, u32 *sd) {
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
/* ======================= v467 : journal complet (Pulse leger seulement, diagnostic) =======================
   Chaque evenement (interruptions, commandes et registres du lecteur, lectures de la carte, reveils, carte memoire) :
   16 octets {type << 24 | temps (mftb / 256, 24 bits), numero, a, b}, 32 par secteur, dans un fichier de la carte
   prepare par GC Station (gc-mp3/natif/journal-complet.bin : snap_sec, snap_cnt secteurs, en boucle ; snap_done =
   secteurs pleins, snap_hi = numero du dernier evenement). Le secteur en cours est reecrit a chaque image. */
#if defined(NATRES_LITE) && !defined(NATRES_COMPACT)
#define EVON ((H.opt & NRO_LOGALL) && H.snap_cnt > 1u && !(H.flags & NRF_SNAP))
static u32 ev_buf[128] __attribute__((aligned(32)));
static u32 ev_n = 0;
static MEM_OS __attribute__((noinline)) void ev_flush(void) {
    if (!EVON || !di_ready || exi_defer()) return;
    nr_write_sd(H.snap_sec + H.snap_done % H.snap_cnt, (const u8 *)ev_buf);
}
static MEM_OS __attribute__((noinline)) void ev(u32 t, u32 a, u32 b) {
    if (!EVON) return;
    u32 m, tb;
    __asm__ volatile("mfmsr %0" : "=r"(m));
    __asm__ volatile("mtmsr %0" :: "r"(m & ~0x8000u) : "memory");
    __asm__ volatile("mftb %0" : "=r"(tb));
    u32 *e = ev_buf + ev_n;
    e[0] = t << 24 | ((tb >> 8) & 0xFFFFFFu); e[1] = ++H.snap_hi; e[2] = a; e[3] = b;
    if ((ev_n += 4) >= 128u) { ev_flush(); ev_n = 0; H.snap_done++; for (u32 i = 0; i < 128u; i++) ev_buf[i] = 0; }
    __asm__ volatile("mtmsr %0" :: "r"(m) : "memory");
}
static MEM_OS __attribute__((noinline)) void evc(u32 t, u32 ctx) {   /* (interruption : ou etait le jeu) */
    int ok = ctx >= 0x80000000u && ctx < NATRES_AT && !(ctx & 3);
    ev(t, ok ? *(volatile u32 *)(ctx + 0x198) : 0, ok ? *(volatile u32 *)(ctx + 0x84) : ctx);
}
static MEM_OS __attribute__((noinline)) void ev_sum(u32 off, u32 len, u32 dst) {   /* v469 : empreinte des octets donnes au jeu */
    if (!EVON) return;
    u32 s = 0; const u8 *q = (const u8 *)dst;
    for (u32 i = 0; i < len; i++) s = (s << 5 | s >> 27) ^ q[i];   /* (meme calcul dans GC Station, natif.c) */
    ev(0x2C, off, len); ev(0x2D, s, dst);
}
#define EV(t, a, b) ev(t, a, b)
#define EVC(t, ctx) evc(t, ctx)
#define EVF() ev_flush()
#define EVSUM(off, len, dst) ev_sum(off, len, dst)
#else
#define EV(t, a, b) ((void)0)
#define EVC(t, ctx) ((void)0)
#define EVF() ((void)0)
#define EVSUM(off, len, dst) ((void)0)
#endif
static int pn_read(void *dst, u32 off, u32 len);   /* v143 : (ISO sur le reseau) */
static int pn_ready = 0;
/* v136 : corrections par jeu dans les autres executables du disque (.dol, .elf, .tgc) : GC Station a calcule
   la position dans l'ISO de chaque instruction a changer ; elle est changee quand le jeu lit cet endroit.
   Table a la suite des cheats : {mots de cheats, mots..., "FIXT", n, n x {position, valeur, taille (1-4)}} */
#define NATRES_FIXT 0x46495854u
static MEM_OS void fx_apply(u8 *dst, u32 off, u32 len) {   /* (v462 : -Os) */
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
static MEM_OS int nr_read(void *dst, u32 off, u32 len) {    /* v54 : une erreur de la carte -> on la redemarre et on reessaie */
    EV(0x30, off, len);
    for (int t = 0; t < 3; t++) {
        if (nr_read1(dst, off, len) == 0) { if (t) H.nretry++; fx_apply((u8 *)dst, off, len); return 0; }
        if (H.err == NR_ERANGE) break;
        H.nrderr++;
        if (x_fast && x_crcbad) x_fast_off = 1;           /* v94 : CRC faux a 32 MHz : retour a 16 MHz */
        if (x_dma) { x_dma = 0; x_why = 0x18u | (H.err & 0xFFu) << 8; }   /* v484 : lecture ratee en DMA : sans DMA jusqu'a la fin (journal : 08+10) */
        x_crcbad = 0;
        sec_cached = 0xFFFFFFFFu;
        if (H.net) { pn_ready = 0; continue; }          /* v143 : (reseau : on redemarre le BBA) */
        di_ready = nr_init() == 0;
    }
    return -1;
}
static MEM_OS int nr_read1(void *dst, u32 off, u32 len) {
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
static MEM_OS void cache_flush(u32 a, u32 len) {                /* donnees ecrites par le processeur -> memoire */
    u32 e = a + len;
    for (a &= ~31u; a < e; a += 32) __asm__ volatile("dcbf 0,%0" :: "r"(a) : "memory");
    __asm__ volatile("sync" ::: "memory");
}

static u32 kick_tb = 0;
static inline u32 tbl(void);
/* v513 : mesure du temps passe dans Pulse pendant les interruptions (Kirby Air Ride : son qui gresille pendant les videos,
   meme sans telephone ni vrai lecteur). Par type : nombre de passages, temps total, pire passage (us), le temps du jeu
   (ses propres fonctions d'interruption) en dehors ; et les ecarts entre deux interruptions du DSP (le son du jeu).
   Ecrit dans le 8e secteur du journal (GCSI), avec l'en-tete. Seulement dans le Pulse leger (place : le Pulse normal est plein). */
#if defined(NATRES_LITE) && !defined(NATRES_COMPACT) && !defined(NATRES_LAN)
#define PF_ON 1
#define PFN 9                                            /* 0 image, 1 son (AI), 2 DSP, 3 lecteur, 4 carte SD (suites), 5 dessin ;
                                                            v526 : morceaux du temps du lecteur (Kirby : 19 ms au pire, menus) : 6 lecture en fond (rd_pump,
                                                            partout), 7 aiguillage EXI (carte virtuelle) dans le crochet du lecteur, 8 relance du vrai lecteur */
static u32 pf_n[PFN], pf_us[PFN], pf_max[PFN], pf_dsp_tb, pf_gap[3];   /* (ecart max, ecarts > 6 ms, > 12 ms) */
static MEM_OS __attribute__((noinline)) void pf_add(u32 k, u32 t0) {
    u32 d = (tbl() - t0) / 41u;
    pf_n[k]++; pf_us[k] += d; if (d > pf_max[k]) pf_max[k] = d;
}
#define PF_T0(v) u32 v = tbl()
#define PF_ADD(k, v) pf_add(k, v)
#define PF_XD(c) do { u32 t_ = tbl(); x_dispatch(c); pf_add(7, t_); } while (0)   /* v526 */
static u32 ld_tb, ld_n, ld_us, ld_kb, ld_max, ld_mlen;   /* v527 : chargements (lectures en fond) : nombre, temps (us), Ko, la plus longue */
#define PF_LD0() (ld_tb = tbl())
#define PF_LD1() do { u32 d_ = (tbl() - ld_tb) / 41u; ld_n++; ld_us += d_; ld_kb += rd_len0 >> 10; if (d_ > ld_max) { ld_max = d_; ld_mlen = rd_len0; } } while (0)
#else
#define PF_XD(c) x_dispatch(c)
#define PF_LD0() ((void)0)
#define PF_LD1() ((void)0)
#define PF_ON 0
#define PF_T0(v) do { } while (0)
#define PF_ADD(k, v) do { } while (0)
#endif
/* v133 : comment le jeu est reveille a la fin d'une commande. 0 = par une vraie commande du lecteur (rapide :
   ~0,1 ms) ; 1 = sans le lecteur (pas de lecteur, ou il ne repond plus) : la fin est donnee au jeu depuis ses
   propres interruptions (image, son, dessin, manettes : quelques ms) */
static int wk_tick = -1;
#if 1
#define WT_SELF (H.opt & NRO_SELFIRQ)                    /* v480 : sans lecteur, le reveil provoque (v485 : Pulse normal aussi) plutot que les interruptions du jeu */
#else
#define WT_SELF 0u
#endif
static int wake_tick(void) { if (wk_tick < 0) wk_tick = !(H.flags & NRF_REAL_IRQ) && !WT_SELF; return wk_tick; }
/* v476 : reveil sans le vrai lecteur, par une vraie interruption (idee de Swiss, codee ici) : une lecture a une adresse ou il
   n'y a rien (0xC4000000 : memoire sans cache, 64 Mo, au-dela des 24 Mo) fait lever au processeur d'E/S (PI) son interruption
   "erreur". Le systeme du jeu l'envoie a err_hook (table du SDK, numero 23, demasquee ici), qui l'efface et fait comme une fin
   du lecteur (di_hook). Interruptions coupees (fonctions d'interruption, souvent les fonctions du lecteur du jeu) : elle arrive
   juste apres, comme celle d'un lecteur tres rapide. */
#if 1                             /* (v476-v477 : Pulse leger seulement ; v485 : Pulse normal aussi) */
#define SELFIRQ_ON (H.opt & NRO_SELFIRQ)
static void di_hook(u32 irq, u32 ctx);
static u32 err_orig = 0;
/* v477 : avec le reveil provoque, la fin de commande n'est donnee au jeu qu'avec son interruption, comme un vrai lecteur (et
   comme Dolphin et Swiss) : jusque-la le lecteur reste "occupe" (DICR bit 0) et ni TCINT ni DEINT. Avant : fini des l'envoi,
   interruption apres. sf_bits : la fin en attente (0x10 TCINT, 0x04 DEINT). Si le jeu attend en relisant les registres sans
   interruptions, la fin lui est donnee au bout de ~300 us (di_load), comme un lecteur qui finit tout seul. */
static u32 sf_bits = 0;
static void sf_apply(void) { if (sf_bits) { di[0] |= sf_bits; di[7] &= ~1u; sf_bits = 0; } }
static void err_hook(u32 irq, u32 ctx) {
    (void)irq;
    *(volatile u32 *)0xCC003000u = 1u;                   /* (efface l'erreur voulue) */
    sf_apply();                                          /* v477 */
    di_hook(21, ctx);
}
static MEM_OS __attribute__((noinline)) void self_irq(void) {
    volatile u32 *t = (volatile u32 *)0x80003040u;       /* (OS_INT_TABLE, plus bas) */
    if (t[23] != (u32)err_hook) { err_orig = t[23]; t[23] = (u32)err_hook; }
    *(volatile u32 *)0x800000C4u &= ~(0x80000000u >> 23);   /* (masques du SDK : 1 = coupee) */
    *(volatile u32 *)0x800000C8u &= ~(0x80000000u >> 23);
    *(volatile u32 *)0xCC003004u |= 1u;                   /* (masque materiel du PI : erreur permise) */
    (void)*(volatile u32 *)0xC4000000u;
}
#else
#define SELFIRQ_ON 0
#define sf_bits 0u
static void sf_apply(void) { }
static void self_irq(void) { }
#endif
#if PF_ON
static u32 di_real_kick_i(void);
static MEM_OS __attribute__((noinline)) u32 di_real_kick(void) { PF_T0(t); u32 r = di_real_kick_i(); PF_ADD(8, t); return r; }   /* v526 : mesure */
#else
#define di_real_kick_i di_real_kick
#endif
static MEM_OS u32 di_real_kick_i(void) {                   /* (v501 : -Os) vraie commande "inquiry" : sa fin reveille le jeu */
    EV(0x26, DI_REAL[0], DI_REAL[7]);
    volatile u32 *r = DI_REAL;
    if (wake_tick()) return 4;                           /* (v133 : sans le lecteur : la fin viendra a la prochaine interruption du jeu) */
    if (SELFIRQ_ON) { H.nkick++; kick_tb = tbl(); self_irq(); return 4; }   /* v476 */
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
static MEM_OS int di_real_inq(void) {
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

#if !defined(NATRES_NOLOG)
static u32 log_ring[NATRES_LOGN][4];
static u8 logbuf[512] __attribute__((aligned(32)));
#endif
static inline u32 tbl(void) { u32 t; __asm__ volatile("mftb %0" : "=r"(t)); return t; }   /* (declaree plus haut) */
#define TB_SEC 40500000u                                  /* (horloge : 162 MHz / 4) */
static MEM_OS void vi_snap(void) {                              /* v99 : etat de l'ecran (journal) */
    volatile unsigned short *v = (volatile unsigned short *)0xCC002000u;
    u32 now = (u32)v[1] << 16 | (v[0x6C / 2] & 1u);
    if (H.vi_now && ((now ^ H.vi_now) & ~0x00010000u)) { H.vi_chg++; H.vi_chg_at = H.vi_count; }
    H.vi_now = now;
    H.vi_dtv = (u32)v[0x6E / 2] << 16 | v[26];           /* (v111 : bas = DI1, interruption de la 1re ligne) */
}
static u32 xv_fall, xv_lost, ch2_w;                      /* (v517 : declarees plus bas ; ecrites dans le secteur de mesure) */
static MEM_OS void nr_log_flush(void) {
    H.flush_tb = tbl();
    vi_snap();
    if (!H.log_sec) return;
    if (exi_defer()) return;                             /* v145 : (le jeu se sert du bus : ecrit la prochaine fois) */
    if (!di_ready) {                                     /* v161 : jeu bloque avant sa 1re lecture du disque : on demarre la carte */
        if (H.net) return;                               /* (pour ecrire quand meme le journal ; avant : rien, et l'ancien restait) */
        di_ready = nr_init() == 0;
        if (!di_ready) return;
    }
#if defined(NATRES_NOLOG)
    nr_write_sd(H.log_sec, (const u8 *)&H);              /* v509 : Pulse sans journaux : l'en-tete seul (temps de jeu, resume), sans tampon */
#else
    const u8 *p = (const u8 *)&H;
    for (u32 i = 0; i < sizeof(natres_hdr_t); i++) logbuf[i] = p[i];
    for (u32 k = 0; k < NATRES_LOGN; k++) {                     /* le plus ancien d'abord */
        u32 j = (H.nlog + k) % NATRES_LOGN, *d = (u32 *)(logbuf + sizeof(natres_hdr_t) + k * 16);
        for (int w = 0; w < 4; w++) d[w] = log_ring[j][w];
    }
    nr_write_sd(H.log_sec, logbuf);
#if PF_ON
    if ((H.flags & NRF_PERF) && H.vi_count) {            /* v513 : 8e secteur : temps passe dans Pulse */
        u32 *w = (u32 *)logbuf;
        for (u32 i = 0; i < 128; i++) w[i] = 0;
        w[0] = NATRES_PERF_MAGIC; w[1] = 5;              /* (v517 : 2 = avec les lectures de la carte debloquees ; v526 : 4 = morceaux du lecteur en 26..34 ; v527 : 5 = chargements en 35..40) */
        for (u32 k = 0; k < PFN; k++) { u32 b = k < 6u ? 2 + k * 3 : 8 + k * 3; w[b] = pf_n[k]; w[b + 1] = pf_us[k]; w[b + 2] = pf_max[k]; }
        w[20] = pf_gap[0]; w[21] = pf_gap[1]; w[22] = pf_gap[2]; w[23] = xv_fall; w[24] = xv_lost; w[25] = ch2_w;
        w[35] = ld_n; w[36] = ld_us; w[37] = ld_kb; w[38] = ld_max; w[39] = ld_mlen;
        nr_write_sd(H.log_sec + 7, logbuf);
    }
#endif
#endif
}
static void nr_log_add(u32 a, u32 b, u32 c, u32 d) {
#if defined(NATRES_NOLOG)
    (void)a; (void)b; (void)c; (void)d; H.nlog++;
#else
    u32 *e = log_ring[H.nlog % NATRES_LOGN];
    e[0] = a; e[1] = b; e[2] = c; e[3] = d;
    H.nlog++;
#endif
}
static int exec_on;                                      /* (v463 : declaree plus bas, OSExec en cours) */
#if defined(NATRES_LITE) && !defined(NATRES_COMPACT)
/* v470 : fin d'une interruption (son, DSP, image GX, manettes, lecteur) : le secteur en cours du journal complet est aussi ecrit
   ici, au plus toutes les 4 ms (avant : seulement a chaque image ; un figeage entre deux images perdait la fin du journal :
   Simpsons US). Pendant OSExec (pas d'image : Shrek Extra Large), a chaque fois. Jamais au milieu d'un transfert sur le bus. */
static u32 ev_tb = 0;
static MEM_OS __attribute__((noinline)) void ev_end(u32 t, u32 a, u32 b) {
    ev(t, a, b);
    u32 tb; __asm__ volatile("mftb %0" : "=r"(tb));
    if (!exi || (exi[0] & 0x380u) || (exi[3] & 1u) || (!exec_on && tb - ev_tb < 162000u)) return;
    ev_tb = tb; ev_flush();
}
#define EVE(t, a, b) ev_end(t, a, b)
#else
#define EVE(t, a, b) ((void)0)
#endif
static void nr_log(u32 off, u32 len, u32 info) {
    nr_log_add(di[2], off, len, info | (H.err & 0xFF) << 24);
    if (info & 1 || H.ncmd <= 40 || exec_on || (!H.vi_count && tbl() - H.flush_tb > TB_SEC * 2)) nr_log_flush();   /* (debut du jeu, erreurs ;
                                                            v516 : toutes les 2 s seulement avant la 1re image, comme alive_tick en v515 :
                                                            Kirby Air Ride lit sans arret, le journal etait encore reecrit toutes les 2 s) */
}
/* ======================= v143 : Pulse par le reseau (option, experimental) =======================
   L'ISO n'est pas sur la carte SD : elle est lue sur une autre GC Station (ou un PC, tools/pulse-serveur.py)
   par le Broadband Adapter (EXI canal 0, peripherique 2). Pilote minimal ecrit ici : registres du
   controleur reseau (commandes EXI de 2 octets pour ses registres a lui, de 4 octets pour ceux du
   controleur Ethernet), anneau de reception dans sa memoire (pages de 256 octets, descripteur de 4 octets),
   envoi par sa file. Par-dessus : ARP (repondre, trouver le serveur), IPv4, UDP. Protocole a nous :
   demande "PNR1" {numero, fichier, position, longueur <= 2048} ; reponses "PNA1" {numero, position,
   longueur <= 1024, donnees}. Une demande perdue est refaite apres 25 ms. */
static const natres_net_t *pn_cfg = 0;                   /* (H.net, ou les reglages des manettes du telephone) */
#define PN_NET pn_cfg
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
extern u8 exc_stack[1600];
#define pn_fr exc_stack                                   /* (trame : la pile du retour a GC Station ne sert pas en meme temps) */
static u8 pn_mac[6], pn_smac[6];
static u32 pn_seq = 0x5000;
/* v327 : l'adaptateur : 1 = Broadband Adapter (canal 0, peripherique 2), 2 = W5500 (ETH2SP1 : canal 0, peripherique 2 ;
   ETH2GC : port de carte B ou A, peripherique 0) ; pn_cs / pn_frq : son peripherique et sa vitesse */
static int pn_hw = 0;
/* build 908 : deux Pulse compiles a partir de ce fichier (place pour le direct) : PULSE_NET=1 Broadband Adapter seulement
   (natres_bin), PULSE_NET=2 W5500 seulement (natres_wz_bin) ; GC Station met le bon selon l'adaptateur branche (natif.c,
   na_rb). Sans PULSE_NET (Pulse LAN, bancs d'essai) : les deux pilotes, comme avant. Le pilote absent n'est pas compile. */
/* v455 : PULSE_NET=3 W6100 / W6300 seulement (natres_w6_bin), PULSE_NET=4 ENC28J60 seulement (natres_enc_bin) */
#ifdef NATRES_LITE                                        /* v452 : Pulse leger : aucun adaptateur (ni telephone, ni jeu par le reseau) */
#undef PULSE_NET
#define PULSE_NET 9
#endif
#ifndef PULSE_NET                                         /* (build 909 : sans precision, le Pulse du BBA ; Pulse LAN : les deux pilotes, comme avant) */
#ifdef NATRES_LAN
#define PULSE_NET 0
#else
#define PULSE_NET 1
#endif
#endif
#define PN_BBA (PULSE_NET == 0 || PULSE_NET == 1)
#define PN_WZ (PULSE_NET == 0 || PULSE_NET == 2 || PULSE_NET == 3)
#define PN_WZ6 (PULSE_NET == 3)                           /* (v455 : meme code que le W5500, autres registres) */
#define PN_ENC (PULSE_NET == 4)
static u32 pn_cs = 0x80u << 2, pn_frq = 5, pn_ch = 0;
static void bb_sel(void) { bx[0] = (bx[0] & 0x405) | (pn_frq << 4) | pn_cs; }   /* (BBA : 32 MHz, peripherique 2) */
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
#if PN_WZ
/* ---- v327 : W5500 (WIZnet, ETH2SP1 / ETH2GC), socket 0 en MACRAW (trames Ethernet brutes), sans interruption.
   Trame SPI : adresse (16 bits), controle (bloc << 3 | ecriture << 2 | 0 = longueur libre), donnees. Blocs : 0 commun,
   1 registres du socket 0, 2 son tampon d'envoi, 3 son tampon de reception (les pointeurs font le tour seuls). */
/* v455 : W6100 / W6300 (ETH2SP1 recents) : memes trames que le W5500 pour le W6100 ; le W6300 met d'abord un octet
   d'instruction (ecriture << 5 | bloc), puis l'adresse et un octet vide. Registres du socket et communs ailleurs. */
#if PN_WZ6
#define WZ_SCR 0x10
#define WZ_SSR 0x30
#define WZ_TXFSR 0x204
#define WZ_TXWR 0x20C
#define WZ_RXRSR 0x224
#define WZ_RXRD 0x228
#define WZ_TXBSR 0x200
#define WZ_RXBSR 0x220
#define WZ_SIMR0 0x24
#define WZ_SHAR 0x4120
static u32 wz_63 = 0;                                     /* (1 : W6300) */
#else
#define WZ_SCR 0x01
#define WZ_SSR 0x03
#define WZ_TXFSR 0x20
#define WZ_TXWR 0x24
#define WZ_RXRSR 0x26
#define WZ_RXRD 0x28
#define WZ_TXBSR 0x1F
#define WZ_RXBSR 0x1E
#define WZ_SIMR0 0x2C
#define WZ_SHAR 0x09
#endif
static __attribute__((noinline, optimize("Os"))) void wz_hdr(u32 bsb, u32 a, int wr) {
#if PN_WZ6
    if (wz_63) { bb_w(((bsb | (wr ? 0x20u : 0u)) << 24) | (a << 8), 4); return; }
#endif
    bb_w((a << 16) | ((bsb << 3) | (wr ? 4u : 0u)) << 8, 3);
}
static __attribute__((noinline, optimize("Os"))) void wz_wr(u32 bsb, u32 a, const u8 *s, u32 n) {
    bb_sel(); wz_hdr(bsb, a, 1);
    for (; n >= 4; n -= 4, s += 4) bb_w((u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3], 4);
    for (; n; n--) bb_w((u32)*s++ << 24, 1);
    bb_desel();
}
static __attribute__((noinline, optimize("Os"))) void wz_rd(u32 bsb, u32 a, u8 *d, u32 n) {
    bb_sel(); wz_hdr(bsb, a, 0);
    for (; n >= 4; n -= 4, d += 4) { u32 w = bb_r(4); d[0] = (u8)(w >> 24); d[1] = (u8)(w >> 16); d[2] = (u8)(w >> 8); d[3] = (u8)w; }
    for (; n; n--) *d++ = (u8)(bb_r(1) >> 24);
    bb_desel();
}
static __attribute__((noinline, optimize("Os"))) u32 wz_r8(u32 bsb, u32 a) { u8 v; wz_rd(bsb, a, &v, 1); return v; }
static __attribute__((noinline, optimize("Os"))) void wz_w8(u32 bsb, u32 a, u32 v) { u8 b = (u8)v; wz_wr(bsb, a, &b, 1); }
static __attribute__((noinline, optimize("Os"))) u32 wz_r16(u32 bsb, u32 a) {                       /* (lu deux fois de suite pareil : la puce le change en meme temps) */
    u32 v = 0;
    for (int t = 0; t < 8; t++) { u8 b[2], c[2]; wz_rd(bsb, a, b, 2); wz_rd(bsb, a, c, 2); v = (u32)b[0] << 8 | b[1]; if (b[0] == c[0] && b[1] == c[1]) break; }
    return v;
}
static __attribute__((noinline, optimize("Os"))) void wz_w16(u32 bsb, u32 a, u32 v) { u8 b[2] = { (u8)(v >> 8), (u8)v }; wz_wr(bsb, a, b, 2); }
static __attribute__((noinline, optimize("Os"))) void wz_cmd(u32 c) { wz_w8(1, WZ_SCR, c); for (u32 i = 0; i < 20000u && wz_r8(1, WZ_SCR); i++) { } }   /* Sn_CR */
static __attribute__((noinline, optimize("Os"))) void wz_send(const u8 *f, u32 n) {
    for (u32 i = 0; i < 20000u && wz_r16(1, WZ_TXFSR) < n; i++) { }    /* Sn_TX_FSR : place dans le tampon d'envoi */
    u32 wr = wz_r16(1, WZ_TXWR);                                    /* Sn_TX_WR */
    wz_wr(2, wr, f, n);
    wz_w16(1, WZ_TXWR, (wr + n) & 0xFFFFu);
    wz_cmd(0x20);                                                   /* SEND */
}
static __attribute__((noinline, optimize("Os"))) u32 wz_recv(void) {
    u32 rsr = wz_r16(1, WZ_RXRSR);                                  /* Sn_RX_RSR : octets recus */
    if (rsr < 2) return 0;
    u32 rd = wz_r16(1, WZ_RXRD), n = 0;                             /* Sn_RX_RD */
    u8 h[2]; wz_rd(3, rd, h, 2);
    u32 len = (u32)h[0] << 8 | h[1];                                /* (longueur, ces 2 octets compris) */
    if (len < 2 || len > rsr || len - 2 > 1536u) len = rsr;         /* (incoherent : tout est jete) */
    else { n = len - 2; wz_rd(3, (rd + 2) & 0xFFFFu, pn_fr, n); }
    wz_w16(1, WZ_RXRD, (rd + len) & 0xFFFFu);
    wz_cmd(0x40);                                                   /* RECV */
    return n;
}
#endif
#if PN_ENC
/* ---- v455 : ENC28J60 (Microchip, ETH2GC Lite...), sans interruption. Commandes SPI : 3 bits d'operation + 5 bits
   d'adresse. Registres par banque (ECON1.BSEL) ; ceux du MAC et du MII rendent d'abord un octet vide. Ici, un registre
   = banque << 5 | adresse, + 0x80 pour MAC / MII. Tampon : reception 0x0000-0x0FFF (anneau), envoi a 0x1000. */
#define EN_ECON1 0x1F
#define EN_ECON2 0x1E
#define EN_ESTAT 0x1D
#define EN_EIR 0x1C
#define EN_RXEND 0x0FFFu
#define EN_TXST 0x1000u
static u32 en_bank = 0, en_next = 0;
static __attribute__((noinline, optimize("Os"))) void en_op(u32 op, u32 r, u32 v) {           /* WCR (0x40), BFS (0x80), BFC (0xA0) */
    bb_sel(); bb_w(((op | (r & 0x1Fu)) << 24) | (v << 16), 2); bb_desel();
}
static __attribute__((noinline, optimize("Os"))) void en_bk(u32 r) {
    u32 b = (r >> 5) & 3u;
    if ((r & 0x1Fu) < 0x1Bu && b != en_bank) { en_op(0xA0, EN_ECON1, 3); if (b) en_op(0x80, EN_ECON1, b); en_bank = b; }
}
static __attribute__((noinline, optimize("Os"))) void en_w(u32 r, u32 v) { en_bk(r); en_op(0x40, r, v & 0xFFu); }
static __attribute__((noinline, optimize("Os"))) void en_w16(u32 r, u32 v) { en_w(r, v); en_w(r + 1, v >> 8); }
static __attribute__((noinline, optimize("Os"))) u32 en_r(u32 r) {
    en_bk(r);
    bb_sel(); bb_w((r & 0x1Fu) << 24, 1); if (r & 0x80u) bb_r(1); u32 v = bb_r(1) >> 24; bb_desel();
    return v;
}
static __attribute__((noinline, optimize("Os"))) void en_buf(int wr, u8 *d, u32 n) {         /* RBM (0x3A) / WBM (0x7A) */
    bb_sel(); bb_w((wr ? 0x7Au : 0x3Au) << 24, 1);
    if (wr) for (; n; n--) bb_w((u32)*d++ << 24, 1);
    else for (; n; n--) *d++ = (u8)(bb_r(1) >> 24);
    bb_desel();
}
static __attribute__((noinline, optimize("Os"))) void en_phy(u32 a, u32 v) {                 /* registre du PHY */
    en_w(0x80 | 2 << 5 | 0x14, a); en_w(0x80 | 2 << 5 | 0x16, v); en_w(0x80 | 2 << 5 | 0x17, v >> 8);   /* MIREGADR, MIWRL, MIWRH */
    for (u32 i = 0; i < 20000u && (en_r(0x80 | 3 << 5 | 0x0A) & 1u); i++) { }   /* MISTAT.BUSY */
}
static __attribute__((noinline, optimize("Os"))) void en_send(const u8 *f, u32 n) {
    for (u32 i = 0; i < 20000u && (en_r(EN_ECON1) & 0x08u); i++) { }   /* (envoi precedent fini : TXRTS) */
    if (en_r(EN_EIR) & 0x02u) { en_op(0x80, EN_ECON1, 0x80); en_op(0xA0, EN_ECON1, 0x80); en_op(0xA0, EN_EIR, 0x02); }   /* (erreur d'envoi : remise a zero de l'envoi) */
    en_w16(0x02, EN_TXST);                                /* EWRPT */
    u8 c = 0; en_buf(1, &c, 1);                           /* (octet de controle : reglages par defaut) */
    en_buf(1, (u8 *)f, n);
    en_w16(0x04, EN_TXST); en_w16(0x06, EN_TXST + n);     /* ETXST, ETXND */
    en_op(0x80, EN_ECON1, 0x08);                          /* TXRTS : envoyer */
}
static __attribute__((noinline, optimize("Os"))) u32 en_recv(void) {
    if (!en_r(1 << 5 | 0x19)) return 0;                   /* EPKTCNT */
    en_w16(0x00, en_next);                                /* ERDPT */
    u8 h[6]; en_buf(0, h, 6);                             /* suivante (2), longueur (2), etat (2) */
    u32 nx = h[0] | (u32)h[1] << 8, len = h[2] | (u32)h[3] << 8, n = 0;
    if (len >= 4 + 14 && len - 4 <= 1536u && nx <= EN_RXEND) { n = len - 4; en_buf(0, pn_fr, n); }   /* (sans le CRC) */
    if (nx > EN_RXEND) nx = 0;                            /* (incoherent : on repart du debut) */
    en_next = nx;
    en_w16(0x0C, nx ? nx - 1 : EN_RXEND);                 /* ERXRDPT (impair : erratum) */
    en_op(0x80, EN_ECON2, 0x40);                          /* PKTDEC */
    return n;
}
#endif
static MEM_OS void pn_send(u8 *f, u32 n) {                       /* trame Ethernet complete (adresses comprises) */
    while (n < 60) f[n++] = 0;
#if PN_WZ
    if (!PN_BBA || pn_hw == 2) { u32 m = irq_off(); wz_send(f, n); irq_restore(m); return; }
#endif
#if PN_ENC
    { u32 m = irq_off(); en_send(f, n); irq_restore(m); return; }
#endif
#if PN_BBA
    u32 m = irq_off();
    bb_out16(BB_TXFIFOCNT, n);
    bb_outs(BB_WRTXFIFOD, f, n);
    bb_out8(BB_NCRA, (bb_in8(BB_NCRA) & ~2u) | 4u);       /* ST1 : envoyer */
    for (u32 i = 0; i < 20000u && (bb_in8(BB_NCRA) & 6u); i++) { }
    irq_restore(m);
#endif
}
/* une trame recue (0 = aucune) : copiee dans pn_fr */
static u32 pn_recv(void) {
#if PN_WZ
    if (!PN_BBA || pn_hw == 2) { u32 m = irq_off(); u32 k = wz_recv(); irq_restore(m); return k; }
#endif
#if PN_ENC
    { u32 m = irq_off(); u32 k = en_recv(); irq_restore(m); return k; }
#endif
#if PN_BBA
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
#endif
}
static u32 pn_ipsum(const u8 *h) { u32 s = 0; for (int i = 0; i < 20; i += 2) s += pn_be16(h + i); while (s >> 16) s = (s & 0xFFFF) + (s >> 16); return ~s & 0xFFFF; }
static void pn_eth(u8 *f, const u8 *dst, u32 type) { for (int i = 0; i < 6; i++) { f[i] = dst[i]; f[6 + i] = pn_mac[i]; } pn_p16(f + 12, type); }
static void MEM_OS pn_arp(int op, const u8 *tmac, u32 tip) {      /* 1 = demande (a tous), 2 = reponse */
    static const u8 all[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    u8 *f = pn_fr;
    pn_eth(f, op == 1 ? all : tmac, 0x0806);
    pn_p16(f + 14, 1); pn_p16(f + 16, 0x0800); f[18] = 6; f[19] = 4; pn_p16(f + 20, (u32)op);
    for (int i = 0; i < 6; i++) { f[22 + i] = pn_mac[i]; f[32 + i] = op == 1 ? 0 : tmac[i]; }
    pn_p32(f + 28, PN_NET->ip); pn_p32(f + 38, tip);
    pn_send(f, 42);
}
/* trame recue : ARP (on repond pour notre adresse, on note celle du serveur) ; rend 1 si c'est une reponse du serveur */
static int MEM_OS pn_rx(u32 n, u32 seq) {
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
#if PN_WZ
/* v327 : un W5500 en (canal, peripherique) pret a recevoir : 0, ou -1. Son adresse Ethernet est gardee (GC Station
   l'a reglee, le routeur la connait deja) ; sinon une adresse tiree du numero de la console (registres ECID). */
static int __attribute__((noinline, optimize("Os"))) pn_wz_init(u32 ch, u32 dev) {
    if (exi && exi == HW6(0xCC006800u + ch * 0x14u) && dev == H.dev) return -1;   /* (la carte SD de Pulse : on n'y touche pas) */
    bx = HW6(0xCC006800u + ch * 0x14u);
    if (((bx[0] & 0x380u) & ~(0x80u << dev)) || (bx[3] & 1u)) return -2;          /* (le jeu se sert de ce canal : plus tard) */
    pn_cs = 0x80u << dev; pn_frq = 3;
    u32 m = irq_off();
    bb_sel(); bb_w(0, 2); u32 id = bb_r(4); bb_desel();
#if PN_WZ6
    if (id != 0x03000000u && id != 0xFF800000u) { irq_restore(m); return -1; }   /* (W6100 : comme le W5500 ; W6300 : le sien) */
    wz_63 = id == 0xFF800000u;
#else
    if (id != 0x03000000u) { irq_restore(m); return -1; }
#endif
    static const u32 fq[3] = { 5, 4, 3 };                 /* (32, 16, 8 MHz : la premiere qui repond juste) */
    int ok = 0;
#if PN_WZ6
    for (int i = 0; i < 3 && !ok; i++) { pn_frq = fq[i]; ok = wz_r8(0, 0x0000) == 0x61 && wz_r8(0, 0x0002) == 0x46; }   /* CIDR, VER */
#else
    for (int i = 0; i < 3 && !ok; i++) { pn_frq = fq[i]; ok = wz_r8(0, 0x39) == 0x04; }   /* VERSIONR */
#endif
    if (!ok) { irq_restore(m); return -1; }
    u8 mac[6]; wz_rd(0, WZ_SHAR, mac, 6);                 /* SHAR : l'adresse que GC Station a mise */
    u32 z = mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5], f = mac[0] & mac[1] & mac[2] & mac[3] & mac[4] & mac[5];
    if (!z || f == 0xFF) {
        u32 e0, e1, e2, e3;
        __asm__ volatile("mfspr %0,924\n\tmfspr %1,925\n\tmfspr %2,926\n\tmfspr %3,927" : "=r"(e0), "=r"(e1), "=r"(e2), "=r"(e3));
        u32 x = e0 ^ (e1 * 31u) ^ (e2 * 961u) ^ (e3 * 29791u) ^ ch;
        mac[0] = 0x00; mac[1] = 0x09; mac[2] = 0xBF; mac[3] = (u8)(x >> 16); mac[4] = (u8)(x >> 8); mac[5] = (u8)x;
    }
#if PN_WZ6
    wz_w8(0, 0x41F4, 0xCE); wz_w8(0, 0x2004, 0); wz_w8(0, 0x41F4, 0x31);   /* CHPLCKR ouvert, SYCR0 : remise a zero, ferme */
    for (u32 i = 0; i < 20000u && wz_r8(0, 0x0000) != 0x61; i++) { }       /* (la puce repond de nouveau) */
    wz_w8(0, 0x41F5, 0x3A); wz_wr(0, WZ_SHAR, mac, 6); wz_w8(0, 0x41F5, 0xC5);   /* (NETLCKR ouvert le temps d'ecrire SHAR) */
    wz_w8(0, 0x2104, 0); wz_w8(0, 0x2114, 0);             /* (aucune interruption : IMR, SIMR) */
#else
    wz_w8(0, 0x00, 0x80);                                 /* MR : remise a zero */
    for (u32 i = 0; i < 20000u && (wz_r8(0, 0x00) & 0x80u); i++) { }
    wz_wr(0, WZ_SHAR, mac, 6);
    wz_w8(0, 0x16, 0); wz_w8(0, 0x18, 0);                 /* (aucune interruption : IMR, SIMR) */
#endif
    for (u32 k = 1; k < 8; k++) { wz_w8(k * 4 + 1, WZ_RXBSR, 0); wz_w8(k * 4 + 1, WZ_TXBSR, 0); }   /* (autres sockets : sans tampon) */
    wz_w8(1, WZ_RXBSR, 16); wz_w8(1, WZ_TXBSR, 16);       /* socket 0 : 16 Ko + 16 Ko */
    wz_w8(1, WZ_SIMR0, 0);                                /* (S0_IMR) */
    wz_w8(1, 0x00, PN_WZ6 ? 0x87 : 0x84);                 /* S0_MR : MACRAW, filtre (notre adresse + diffusion) */
    wz_cmd(0x01);                                         /* OPEN */
    ok = wz_r8(1, WZ_SSR) == 0x42;                        /* S0_SR : MACRAW */
    for (int i = 0; i < 6; i++) pn_mac[i] = mac[i];
    irq_restore(m);
    if (!ok) return -1;
    pn_hw = 2; pn_ch = ch;
    return 0;
}
#endif
#if PN_ENC
/* v455 : un ENC28J60 en (canal, peripherique) pret a recevoir : 0, -1, ou -2 (canal pris par le jeu). La puce n'a pas
   d'adresse Ethernet : celle que la pile reseau de GC Station lui donne (tiree du numero de la console, registres ECID,
   comme le fait le pilote ENC28J60 de libogc2, licence zlib), pour que le routeur la reconnaisse. */
static int __attribute__((noinline, optimize("Os"))) pn_en_init(u32 ch, u32 dev) {
    if (exi && exi == HW6(0xCC006800u + ch * 0x14u) && dev == H.dev) return -1;   /* (la carte SD de Pulse) */
    bx = HW6(0xCC006800u + ch * 0x14u);
    if (((bx[0] & 0x380u) & ~(0x80u << dev)) || (bx[3] & 1u)) return -2;
    pn_cs = 0x80u << dev; pn_frq = 0;                     /* (1 MHz : remise a zero et identifiant) */
    u32 m = irq_off();
    bb_sel(); bb_w(0xFFu << 24, 1); bb_desel();           /* SRC : remise a zero */
    for (u32 t0 = tbl(), i = 0; i < 200000u && tbl() - t0 < TB_SEC / 500u; i++) { }   /* (2 ms) */
    bb_sel(); bb_w(0, 2); u32 id = bb_r(4); bb_desel();   /* (ERDPT apres remise a zero : 0x05FA) */
    if (id != 0xFA050000u) { irq_restore(m); return -1; }
    pn_frq = 4; en_bank = 0;                              /* (16 MHz) */
    for (u32 i = 0; i < 20000u && !(en_r(EN_ESTAT) & 1u); i++) { }   /* CLKRDY */
    en_w16(0x08, 0); en_w16(0x0A, EN_RXEND); en_w16(0x0C, EN_RXEND); en_w16(0x00, 0); en_next = 0;   /* ERXST, ERXND, ERXRDPT, ERDPT */
    en_w(1 << 5 | 0x18, 0xA1);                            /* ERXFCON : notre adresse, CRC juste, diffusion */
    en_w(0x80 | 2 << 5 | 0x00, 0x01);                     /* MACON1 : MARXEN */
    en_w(0x80 | 2 << 5 | 0x02, 0x32);                     /* MACON3 : bourrage a 60 octets + CRC, longueur verifiee */
    en_w(0x80 | 2 << 5 | 0x03, 0x40);                     /* MACON4 : DEFER */
    en_w16(0x80 | 2 << 5 | 0x0A, 1536);                   /* MAMXFL */
    en_w(0x80 | 2 << 5 | 0x04, 0x12); en_w16(0x80 | 2 << 5 | 0x06, 0x0C12);   /* MABBIPG, MAIPG */
    u32 e[4];
    __asm__ volatile("mfspr %0,924\n\tmfspr %1,925\n\tmfspr %2,926\n\tmfspr %3,927" : "=r"(e[0]), "=r"(e[1]), "=r"(e[2]), "=r"(e[3]));
    u8 d[19]; for (int i = 0; i < 16; i++) d[i] = (u8)(e[i >> 2] >> (24 - 8 * (i & 3)));
    d[16] = 0x04; d[17] = 0xA3; d[18] = 0; d[15] ^= 0x00;
    u32 sum = ch;
    for (int i = 0; i < 18; i += 3) { sum += (u32)d[i] << 16 | (u32)d[i + 1] << 8 | d[i + 2]; sum = (sum & 0xFFFFFFu) + (sum >> 24); }
    u8 mac[6] = { 0x00, 0x09, 0xBF, (u8)(sum >> 16), (u8)(sum >> 8), (u8)sum };
    static const u8 ma[6] = { 0x04, 0x05, 0x02, 0x03, 0x00, 0x01 };   /* MAADR1..6 (banque 3) */
    for (int i = 0; i < 6; i++) en_w(0x80 | 3 << 5 | ma[i], mac[i]);
    en_phy(0x10, 0x0100);                                 /* PHCON2 : HDLDIS */
    en_w(0x1B, 0);                                        /* EIE : aucune interruption */
    en_op(0x80, EN_ECON1, 0x04);                          /* RXEN : reception */
    for (int i = 0; i < 6; i++) pn_mac[i] = mac[i];
    irq_restore(m);
    pn_hw = 3; pn_ch = ch;
    return 0;
}
#endif
/* l'adaptateur pret a recevoir (sans chercher de serveur) : 0, -1 = aucun, -2 = un canal pris par le jeu (plus tard).
   Broadband Adapter d'abord, puis W5500 : port serie 1 (ETH2SP1), puis ports de carte B et A (ETH2GC) */
/* v372 : vitesse du cable : avant, 10 Mbit/s seulement (0x01) ; maintenant toutes, jusqu'a 100 Mbit/s (0x1F, comme libogc) ;
   5 s apres, si le lien n'est pas venu (box ou switch qui negocie mal), retour aux 10 Mbit/s d'avant, pour de bon */
static u32 pn_nw = 0x1F, pn_nw_tb = 0;
static int __attribute__((noinline)) MEM_OS pn_bba_init(void) {
    bx = HW6(0xCC006800u); pn_cs = 0x80u << 2; pn_frq = 5; pn_hw = 1; pn_ch = 0;
#if PN_BBA
    u32 m = irq_off();
    bb_sel(); bb_w(0, 2); u32 id = bb_r(4); bb_desel();      /* (identifiant du peripherique EXI) */
    if (id != 0x04020200u) {
        irq_restore(m);
#endif
#if PN_WZ || PN_ENC
        /* (v455 : port serie 1, port serie 2 (sauf si la carte SD y est), ports de carte B et A) */
        static const u8 wp[4][2] = { { 0, 2 }, { 2, 0 }, { 1, 0 }, { 0, 0 } };
        int pris = 0;
#if PN_ENC
        for (int i = 0; i < 4; i++) { int r = pn_en_init(wp[i][0], wp[i][1]); if (!r) return 0; if (r == -2) pris = 1; }
#else
        for (int i = 0; i < 4; i++) { int r = pn_wz_init(wp[i][0], wp[i][1]); if (!r) return 0; if (r == -2) pris = 1; }
#endif
        bx = HW6(0xCC006800u); pn_cs = 0x80u << 2; pn_frq = 5; pn_hw = 1; pn_ch = 0;
        return pris ? -2 : -1;
#else
        return -1;
#endif
#if PN_BBA
    }
    bb_cout(0x02, 0x00);                                  /* (interruptions du BBA : aucune) */
    bb_out8(BB_NCRA, 1);                                  /* remise a zero */
    for (volatile u32 i = 0; i < 20000u; i++) { }
    bb_out8(BB_NCRA, 0);
    bb_out8(0x5B, bb_in8(0x5B) & ~0x80u);
    bb_out8(0x5E, pn_nw); pn_nw_tb = tbl() | 1;
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
    return 0;
#endif
}
#if !defined(NATRES_COMPACT)
static int __attribute__((noinline)) MEM_OS pn_init(void) {
    if (!H.net || ((const natres_net_t *)H.net)->magic != NATRES_NET_MAGIC) return -1;
    pn_cfg = (const natres_net_t *)H.net;
    if (pn_bba_init()) { H.net_rt = (H.net_rt & 0xFFFFFFu) | (u32)NRN_NOBBA << 24; return -1; }
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
#else
static int __attribute__((noinline)) pn_init(void) { return -1; }   /* (v474 : Pulse compact) */
#endif
/* une demande (<= 2048 octets) et ses reponses */
static int __attribute__((noinline)) MEM_OS pn_req(u8 *dst, u32 off, u32 len) {
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
#if !defined(NATRES_COMPACT)
static int __attribute__((noinline)) MEM_OS pn_read(void *dst, u32 off, u32 len) {
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
#else
static int __attribute__((noinline)) pn_read(void *dst, u32 off, u32 len) { (void)dst; (void)off; (void)len; return -1; }   /* (v474 : Pulse compact) */
#endif

#if !defined(NATRES_LAN) && !defined(NATRES_LITE)      /* (le Pulse normal ; Pulse LAN : voir plus bas, le pont ; v452 : Pulse leger : rien) */
/* ======================= manettes du telephone =======================
   L'appli du telephone envoie ses touches en UDP (port 51237) : {"GCPD", version, port 0-3, 0, 0, numero, INBUFH,
   INBUFL}. A chaque image, Pulse lit le Broadband Adapter (si le jeu ne se sert pas du canal EXI 0), repond aux
   demandes ARP pour l'adresse de la console, et garde les dernieres touches de chaque port. Le jeu les voit comme une
   manette branchee (si_load) : INBUF, SISR (donnees neuves, pas d'erreur), et les reponses des commandes "type"
   (00 / FF : manette standard 0900) et "origine" (41 / 42 : sticks au centre) envoyees par la bibliotheque des
   manettes du jeu sur ce port. Pas de touches depuis 1 s : manette au repos. */
static natres_pad_t *pd = 0;
static u32 pd_seen = 0;
static u32 pd_cm0;                                       /* v457 : ports du telephone voulus (actifs au 1er paquet) */
static MEM_OS natres_pad_t *pd_find(void) {   /* (v494 : -Os, place) */
    if (pd_seen) return pd;
    pd_seen = 1;
    if (!(H.opt & (NRO_PHONE | NRO_MEM)) || !H.chts || H.net) return 0;
    u32 *q = (u32 *)H.chts;
    q += 1 + q[0];
    if (q[0] == 0x46495854u) q += 2 + q[1] * 3;          /* ("FIXT") */
    if (q[0] == 0x41434831u) { u32 n = q[1], *d = q + 2 + 2 * n; while (n--) d += 3 + 4 * d[0]; q = d; }   /* ("ACH1") */
    if (q[0] == NATRES_PAD_MAGIC) { pd = (natres_pad_t *)q; pd_cm0 = pd->chmask; pd->chmask = 0; }
    /* v457 : un port du telephone n'existe pour le jeu qu'a partir du 1er paquet de touches recu. Avant, sans telephone,
       le port restait "branche" : le jeu lui posait sa question "type" ~14 fois par seconde, et ce transfert partait vers
       la vraie manette du port 1 (lue en ce moment) : des lectures de la manette 1 perdues (touches et stick qui sautent,
       Tony Hawk's Pro Skater 4) */
    return pd;
}
/* trame UDP vers (mac, ip, port) : donnees deja en pn_fr + 42 */
static MEM_OS __attribute__((noinline)) void pn_udp_hdr(natres_pad_t *p, const u8 *mac, u32 ip, u32 port, u32 dlen) {   /* (v371 : l'en-tete seul) */
    u8 *fr = pn_fr, *iph = fr + 14, *uu = iph + 20;
    static u32 ipid = 0;
    pn_eth(fr, mac, 0x0800);
    iph[0] = 0x45; iph[1] = 0; pn_p16(iph + 2, 28 + dlen); pn_p16(iph + 4, ++ipid & 0xFFFF); pn_p16(iph + 6, 0x4000); iph[8] = 64; iph[9] = 17;
    pn_p16(iph + 10, 0); pn_p32(iph + 12, p->net.ip); pn_p32(iph + 16, ip); pn_p16(iph + 10, pn_ipsum(iph));
    pn_p16(uu, NATRES_PAD_PORT); pn_p16(uu + 2, port); pn_p16(uu + 4, 8 + dlen); pn_p16(uu + 6, 0);
}
static MEM_OS void pn_udp(natres_pad_t *p, const u8 *mac, u32 ip, u32 port, u32 dlen) { pn_udp_hdr(p, mac, ip, port, dlen); pn_send(pn_fr, 42 + dlen); }
/* ======================= v306 : chercheur de codes de l'appli =======================
   Le telephone lit la memoire du jeu par blocs de 1 Ko (les mots nuls ne passent pas : une carte de 256 bits dit
   lesquels sont la), y ecrit une valeur, et met le jeu en pause le temps de tout lire (sinon 2 blocs par image). */
static u32 ach_bits(u32 *b);
/* v307 : valeurs bloquees en direct par le telephone ("GCMF" : a = adresse, b = taille 1/2/4, 0 = liberer, c = valeur ;
   a = 0, b = 0 : tout liberer) : reecrites a chaque image, apres le jeu (comme les cheats) */
#define MFZ_MAX 8
static u32 mfz[MFZ_MAX * 3], mfz_n = 0;
static MEM_OS __attribute__((noinline, used)) void mfz_run(void) {
    for (u32 i = 0; i < mfz_n; i++) {
        u32 a = mfz[i * 3], t = mfz[i * 3 + 1], v = mfz[i * 3 + 2];
        if (t == 1) *(volatile u8 *)a = (u8)v; else if (t == 2) *(volatile unsigned short *)a = (unsigned short)v; else *(volatile u32 *)a = v;
    }
}
static u32 mem_ok(u32 a, u32 n) { return a >= 0x80000000u && a + n <= 0x81800000u && a + n > a; }
static MEM_OS void mem_ack(natres_pad_t *p, u32 seq) {
    u8 *d = pn_fr + 42;
    u32 b[2];
    pn_p32(d, 0x47434D41u); pn_p32(d + 4, seq); pn_p32(d + 8, p->mpause);   /* "GCMA" */
    for (int i = 0; i < 8; i++) d[12 + i] = ((volatile u8 *)0x80000000u)[i];   /* (jeu : ID, editeur, disque, version) */
    pn_p32(d + 20, H.vi_count); pn_p32(d + 24, ach_bits(b)); pn_p32(d + 28, b[0]); pn_p32(d + 32, b[1]);
    /* v307 : l'image affichee (capture du telephone) : registres de la video (VTR << 16 | DCR, TFBL, BFBL, PICCONF),
       puis les valeurs bloquees */
    volatile unsigned short *vi = (volatile unsigned short *)0xCC002000u;
    pn_p32(d + 36, (u32)vi[0] << 16 | vi[1]); pn_p32(d + 40, *(volatile u32 *)0xCC00201Cu); pn_p32(d + 44, *(volatile u32 *)0xCC002024u);
    pn_p32(d + 48, vi[0x24]); pn_p32(d + 52, mfz_n);
    pn_p32(d + 56, p->msr ? 256u : 96u);                 /* v419 : morceaux du direct acceptes (MS_MAX sans la reserve) */
    pn_udp(p, p->mmac, p->mip, p->mport, 60);
}
static MEM_OS void mem_ach(natres_pad_t *p) {
    u8 *d = pn_fr + 42;
    u32 b[2];
    pn_p32(d, 0x47434143u); pn_p32(d + 4, ach_bits(b)); pn_p32(d + 8, b[0]); pn_p32(d + 12, b[1]);   /* "GCAC" */
    pn_udp(p, p->mmac, p->mip, p->mport, 16);
}
static MEM_OS void mem_blocks(natres_pad_t *p, u32 seq, u32 a, u32 n) {
    a &= ~3u;
    if (n > (p->mpause ? 64u : 2u)) n = p->mpause ? 64u : 2u;
    for (; n && mem_ok(a, NATRES_MEM_BLK); n--, a += NATRES_MEM_BLK) {
        u8 *d = pn_fr + 42, *map = d + 16, *o = d + 48;
        const u32 *m = (const u32 *)a;
        pn_p32(d, 0x47434D44u); pn_p32(d + 4, seq); pn_p32(d + 8, a); pn_p32(d + 12, 0);   /* "GCMD" */
        for (u32 i = 0; i < 32; i++) map[i] = 0;
        for (u32 i = 0; i < NATRES_MEM_BLK / 4; i++) {
            u32 w = m[i];
            if (!w) continue;
            map[i >> 3] |= (u8)(0x80u >> (i & 7));
            pn_p32(o, w); o += 4;
        }
        pn_udp(p, p->mmac, p->mip, p->mport, (u32)(o - d));
        p->mblk++;
    }
}
/* ======================= appli : le direct ("GCMS") =======================
   L'appli s'abonne a des morceaux de la memoire du jeu (karts, personnages...) : {"GCMS", numero, n, periode, 0, puis n x
   {adresse, longueur}} (48 morceaux, 4,5 Ko en tout au plus ; a renouveler : sans nouvelles depuis 3 s, l'envoi s'arrete).
   Toutes les "periode" images, Pulse envoie ces morceaux a la suite, en paquets {"GCMS", image, morceau << 16 | morceaux,
   taille totale, donnees (1400 octets au plus)} : l'appli recoit tout ce qui bouge a chaque image, sans le demander.
   v328 : 64 morceaux, 10 Ko envoyes par image au plus ; options dans le haut de la longueur : bit 31 « compact » (des
   matrices 3 x 4 : la 1re telle quelle, les suivantes en 9 nombres de 16 bits, voir ms_mat), bit 30 seulement aux images
   paires, bit 29 seulement aux impaires. (Une Pulse plus ancienne s'arrete au 1er morceau compact : l'appli les met a la fin.) */
#define MS_MAX 96                         /* (build 911 : 64 avant ; mode Detaille de l'appli) */
#define MS_TOT 16384u                     /* (build 911 : 10 Ko avant) */
#define MS_PAQ 1400u
/* v414 (diagnostic, dans "GCMT") : images gardees sans etre envoyees (bus pris), images en attente renvoyees, images sautees sans
   etre gardees, attentes de la file d'envoi trop longues (le paquet suivant ecrit quand meme) */
static u32 ms_cb = 0, ms_cr = 0, ms_cs = 0, ms_cw = 0;
static u32 ms_ab = 0, ms_cz;
                                     /* (v413 : numero de l'abonnement dans les paquets, + 0x100) */
static u32 ms_a[MS_MAX], ms_n = 0, ms_per = 1, ms_tb = 0, ms_nc = 0, ms_dup = 0, ms_prev = 0, ms_ann = 0;   /* (v408 : ms_dup = exemplaires en plus, ms_prev : derniere image envoyee ; v409 : ms_ann) */
static unsigned short ms_l[MS_MAX];                       /* (longueur, et les options en bits 13 a 15) */
/* v419 : avec la reserve du direct (chercheur de codes de l'appli), la liste vit dans la structure du telephone, apres la
   reserve : 256 morceaux (NATRES_MSL_BYTES) au lieu de 96, sans prendre de place dans Pulse (Super Mario Sunshine : des
   centaines de petites zones) ; sinon, la liste de Pulse (96) */
static u32 *ms_ap = ms_a, ms_max = MS_MAX;
static unsigned short *ms_lp = ms_l;
static MEM_OS u32 ms_out(u32 l) { return l >> 15 ? 48u + ((l & 0x1FFFu) / 48u - 1u) * 18u : l & 0x1FFFu; }
static MEM_OS void ms_req(natres_pad_t *p, const u8 *d, u32 dispo) {
    u32 n = pn_be32(d + 8), per = pn_be32(d + 12), tot = 0, o = dispo >= 20 ? pn_be32(d + 16) : 0;
    if (p->msr) { ms_ap = (u32 *)((u8 *)p + p->msr - NATRES_MSL_BYTES); ms_lp = (unsigned short *)(ms_ap + 256); ms_max = 256; }
    else { ms_ap = ms_a; ms_lp = ms_l; ms_max = MS_MAX; }
    /* v419 : option bit 6 : suite de la liste (un paquet ne tient que ~180 morceaux) : ajoutes a la fin */
    if (o >> 6 & 1u) for (u32 i = 0; i < ms_n; i++) tot += ms_out(ms_lp[i]);
    else ms_n = 0;
    for (u32 i = 0; i < n && ms_n < ms_max && 20 + 8 * i + 8 <= dispo; i++) {
        u32 a = pn_be32(d + 20 + 8 * i), l = pn_be32(d + 24 + 8 * i), f = l >> 29;
        l &= 0xFFFFu;
        if (f & 4 && (l % 48u || a & 3)) f &= 3;           /* (compact : des matrices entieres, alignees) */
        if (!l || l > 4608u || !mem_ok(a, l)) break;          /* (build 909 : (f & 3) == 3 : un envoi sur deux, voir ms_saute) */
        l |= f << 13;
        if (tot + ms_out(l) > MS_TOT) break;
        ms_ap[ms_n] = a; ms_lp[ms_n] = (unsigned short)l; ms_n++; tot += ms_out(l);
    }
    ms_per = per >= 1 && per <= 8 ? per : 1;
    ms_nc = PN_BBA && dispo >= 20 ? pn_be32(d + 16) & 1u : 0;
    ms_dup = dispo >= 20 ? pn_be32(d + 16) >> 1 & 3u : 0;
    ms_ann = dispo >= 20 ? pn_be32(d + 16) >> 3 & 1u : 0;
    /* v413 : bit 4 : chaque paquet porte le numero de cet abonnement (bits 8-15 du mot morceau / morceaux) : l'appli lit
       chaque image avec la liste qui l'a demandee, meme juste apres un changement de liste */
    ms_ab = dispo >= 20 && (pn_be32(d + 16) >> 4 & 1u) ? 0x100u | (pn_be32(d + 4) & 0xFFu) : 0;
    ms_cz = dispo >= 20 ? pn_be32(d + 16) >> 5 & 1u : 0;   /* v416 : bit 5 : compression (images de reference et changements) */   /* v409 : bit 3 : anneau (copies 2 et 5 images plus tard) */   /* v406 / v408 : options bits 1-2 (appli, RetroAchievements) : 0 a 3 envois de plus de toute l'image */   /* build 909 (Pulse « 3 », essai) : envoi sans copie, demande par l'appli */
    ms_tb = tbl();
}
/* un flottant (bits IEEE) x 2^sh en entier, sans calcul flottant (Pulse est en -msoft-float) ; trop grand : +-2^30 */
static MEM_OS int ms_fx(u32 b, int sh) {
    u32 x = b >> 23 & 0xFFu, m = (b & 0x7FFFFFu) | 0x800000u;
    int e = (int)x - 150 + sh, v;
    if (!x) return 0;
    v = e >= 7 ? 0x40000000 : e <= -24 ? 0 : e >= 0 ? (int)(m << e) : (int)(m >> -e);
    return b >> 31 ? -v : v;
}
static u32 ms_o, ms_part, ms_parts, ms_tot;
/* build 909 (Pulse « 2 ») : un morceau saute a cet envoi ? options : 2 images paires seulement, 1 impaires seulement,
   3 un envoi sur deux (les objets du direct de Wind Waker : moitie moins de donnees ; l'appli prolonge leur mouvement) */
static MEM_OS int ms_saute(u32 f, u32 imp, u32 alt) { f &= 3; return (f == 2 && imp) || (f == 1 && !imp) || (f == 3 && alt); }
/* v370 : mesure du temps pris au jeu : le direct (ms_send), tout ce que Pulse fait pour le telephone a chaque image
   (pd_poll), et les octets envoyes ; une fois par seconde, un paquet "GCMT" a l'appli abonnee au direct :
   {"GCMT", image, moyenne du direct (us), pire du direct (us), pire de tout (us), octets du direct par image} */
static u32 mt_sum = 0, mt_n = 0, mt_max = 0, mt_oct = 0;   /* (v372 : plus l'attente du cable ni l'ecriture : la place de Pulse) */
/* v371 : envoi du direct seulement (la GBA et le reste gardent pn_send) : la trame va dans la file d'envoi de l'adaptateur
   par DMA (avant : 4 octets a la fois, chacun attendu), et l'envoi n'est plus attendu tout de suite : on prepare le
   paquet suivant pendant qu'il part ; on attend seulement avant d'ecrire le suivant, et a la fin du direct (les autres
   envois de Pulse comptent sur une file libre) */
static MEM_OS void pn_link_check(void) {                /* v372 : (voir pn_nw) une seule fois, 5 s apres le demarrage de l'adaptateur */
    if (!PN_BBA || !pn_nw_tb || tbl() - pn_nw_tb < 5u * TB_SEC || (*HW6(0xCC006800u) & 0x380u)) return;   /* (pn_nw_tb : Broadband Adapter seulement) */
    bx = HW6(0xCC006800u);
    u32 m = irq_off();
    if (!(bb_in8(0x31) & 3u)) { pn_nw = 0x01; bb_out8(0x5E, 0x01); bb_out8(0x5C, bb_in8(0x5C) | 0x04u); }   /* (NWAYS : pas de lien) */
    pn_nw_tb = 0;
    irq_restore(m);
}
static MEM_OS void ms_txwait(void) {
    u32 i = 0;
    for (; i < 20000u && (bb_in8(BB_NCRA) & 6u); i++) { }
    if (i == 20000u) ms_cw++;                            /* (v414 : diagnostic) */
}
static MEM_OS __attribute__((noinline)) void ms_tx(u8 *f, u32 n) {
    if (!PN_BBA || pn_hw != 1) { pn_send(f, n); return; }   /* (W5500 : comme avant) */
    while (n < 60) f[n++] = 0;
    u32 m = irq_off();
    ms_txwait();
    u32 k = n & ~31u;
    bb_out16(BB_TXFIFOCNT, n);
    {                                                     /* (f = pn_fr : aligne sur 32 ; n >= 60 : k >= 32) */
        cache_flush((u32)f, k);
        bb_sel(); bb_mx(BB_WRTXFIFOD, 1);
        bx[1] = (u32)f & 0x1FFFFFE0u; bx[2] = k; bx[3] = 7;   /* (DMA, ecriture, depart) */
        for (u32 i = 0; i < 200000u && (bx[3] & 1); i++) { }
        bb_desel();
        if (n > k) bb_outs(BB_WRTXFIFOD, f + k, n - k);   /* (la fin : a la suite dans la file) */
    }
    bb_out8(BB_NCRA, (bb_in8(BB_NCRA) & ~2u) | 4u);       /* ST1 : envoyer (sans attendre) */
    irq_restore(m);
}
/* build 909 (Pulse « 3 », 1re etape, essai : seulement si l'appli le demande ; Broadband Adapter seulement) : le direct sans
   recopie. Les octets bruts des morceaux ne sont plus copies un par un dans la trame : la trame est une liste de segments
   (l'en-tete et les octets calcules des morceaux compacts dans pn_fr, les autres directement dans la memoire du jeu) ;
   chacun va dans la file d'envoi de l'adaptateur : debut et fin non alignes en ecriture directe, le milieu (blocs de 32
   octets alignes) par DMA, apres avoir vide le cache du processeur sur ces octets. */
#define MS_SEGS 72
static const u8 *ms_sa[MS_SEGS];
static unsigned short ms_sn[MS_SEGS];
static u32 ms_ns = 0, ms_st = 0;                          /* (segments de la trame ; octets calcules deja ranges dans pn_fr) */
static MEM_OS void ms_fifo(const u8 *s, u32 n) {
    u32 h = (32u - ((u32)s & 31u)) & 31u, k;
    if (h > n) h = n;
    if (h) bb_outs(BB_WRTXFIFOD, s, h);
    s += h; n -= h; k = n & ~31u;
    if (k) {
        cache_flush((u32)s, k);
        bb_sel(); bb_mx(BB_WRTXFIFOD, 1);
        bx[1] = (u32)s & 0x1FFFFFE0u; bx[2] = k; bx[3] = 7;   /* (DMA, ecriture, depart) */
        for (u32 i = 0; i < 200000u && (bx[3] & 1); i++) { }
        bb_desel();
    }
    if (n > k) bb_outs(BB_WRTXFIFOD, s + k, n - k);
}
static MEM_OS void ms_seg(const u8 *s, u32 n) {            /* un segment de plus (colle au precedent s'il le prolonge) */
    if (ms_ns && ms_sa[ms_ns - 1] + ms_sn[ms_ns - 1] == s) { ms_sn[ms_ns - 1] = (unsigned short)(ms_sn[ms_ns - 1] + n); return; }
    ms_sa[ms_ns] = s; ms_sn[ms_ns] = (unsigned short)n; ms_ns++;
}
static MEM_OS __attribute__((noinline)) void ms_tx_segs(u32 n) {   /* n : trame entiere (en-tete compris) */
    static const u8 zero[20] = { 0 };
    u32 m = irq_off();
    ms_txwait();
    bb_out16(BB_TXFIFOCNT, n < 60 ? 60 : n);
    ms_fifo(pn_fr, 42 + 16);
    for (u32 i = 0; i < ms_ns; i++) ms_fifo(ms_sa[i], ms_sn[i]);
    if (n < 60) ms_fifo(zero, 60 - n);
    bb_out8(BB_NCRA, (bb_in8(BB_NCRA) & ~2u) | 4u);       /* ST1 : envoyer (sans attendre) */
    irq_restore(m);
    ms_ns = 0; ms_st = 0;
}
static u32 ms_muet = 0;
                                   /* v412 : image preparee pour l'anneau seulement (bus pris par le jeu) */
static MEM_OS void ms_udp(natres_pad_t *p, u32 dlen) { if (ms_muet) return; pn_udp_hdr(p, p->mmac, p->mip, p->mport, dlen); ms_tx(pn_fr, 42 + dlen); }   /* (comme pn_udp) */
static MEM_OS void ms_put(natres_pad_t *p, u32 v) {     /* un octet de plus ; paquet plein (ou v > 255 : la fin) : on l'envoie */
    u8 *d = pn_fr + 42;
    if (v < 256u) {
        if (ms_nc && pn_hw == 1) { d[16 + ms_st] = (u8)v; ms_seg(d + 16 + ms_st, 1); ms_st++; ms_o++; }   /* (build 909 : octet calcule) */
        else d[16 + ms_o++] = (u8)v;
    }
    if (ms_o == MS_PAQ || (v > 255u && ms_o)) {
        { u32 dv = H.vi_count - ms_prev; if (dv > 255u) dv = 255u;   /* v408 : avec exemplaires en plus : images depuis le dernier envoi (bits 24-31) */
          pn_p32(d, 0x47434D53u); pn_p32(d + 4, H.vi_count); pn_p32(d + 8, ms_part << 16 | (ms_ab & 0xFFu) << 8 | ms_parts); pn_p32(d + 12, ms_tot | (ms_dup || ms_ann ? dv << 24 : 0)); }   /* "GCMS" */
        if (ms_nc && pn_hw == 1) { pn_udp_hdr(p, p->mmac, p->mip, p->mport, 16 + ms_o); ms_tx_segs(42 + 16 + ms_o); }
        else ms_udp(p, 16 + ms_o);   /* (v371 : DMA, sans attendre l'envoi) */
        ms_part++; ms_o = 0;
    }
}
/* build 910 : plus vite : les octets vont directement dans la trame tant qu'elle a de la place (avant : ms_put pour chacun,
   avec ses tests) ; la trame pleine ou l'envoi sans recopie passent par ms_put, comme avant. Memes octets envoyes. */
static MEM_OS void ms_p16(natres_pad_t *p, int v) {
    v = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    if (!ms_nc && ms_o + 2u < MS_PAQ) { u8 *d = pn_fr + 42 + 16 + ms_o; d[0] = (u8)((u32)v >> 8); d[1] = (u8)v; ms_o += 2; return; }
    ms_put(p, (u32)v >> 8 & 0xFFu); ms_put(p, (u32)v & 0xFFu);
}
static MEM_OS void ms_copie(natres_pad_t *p, const u8 *src, u32 n) {   /* n octets bruts (sans l'envoi sans recopie) */
    while (n) {
        u32 t = MS_PAQ - ms_o;
        if (t > n) t = n;
        u8 *d = pn_fr + 42 + 16 + ms_o;
        for (u32 i = 0; i < t; i++) d[i] = src[i];
        ms_o += t; src += t; n -= t;
        if (ms_o == MS_PAQ) ms_put(p, 256u);              /* (trame pleine : envoyee) */
    }
}
/* compact : matrice suivante = les 2 premieres lignes de la rotation x 16384 (la 3e se refait : produit vectoriel), puis la
   position x 4 moins celle de la 1re matrice du morceau ; 18 octets au lieu de 48 */
static MEM_OS void ms_mat(natres_pad_t *p, const u32 *w, const int *t0) {
    for (int q = 0; q < 8; q++) if ((q & 3) != 3) ms_p16(p, ms_fx(w[q], 14));
    for (int r = 0; r < 3; r++) ms_p16(p, ms_fx(w[4 * r + 3], 2) - t0[r]);
}
/* ======================= v416 : reserve du direct (journal d'images) =======================
   La reserve (p->msr, NATRES_MSR_BYTES) : une table de 32 images (image, position, longueur, drapeaux), l'image de
   reference (compression seulement) puis le journal : les images a la suite, comme envoyees (debut du journal quand la fin
   est atteinte ; une image ecrasee sort de la table). Chaque image y est gardee ; elle repart si elle n'a pas pu partir
   (bus pris par le jeu : des le prochain envoi), et en copie 2, 7, 15 et 31 images plus tard si elle y est encore.
   Compression (option bit 5, appli 545) : toutes les 30 images, une image de reference complete ; entre deux, seulement les
   octets changes depuis elle (sauter, nombre, octets), avec une somme des octets de l'image entiere : l'appli refait l'image
   et la refuse si la somme ne va pas (jamais une fausse valeur). En-tete de l'image codee (16 octets) : image de reference
   (elle-meme : image complete), octets de l'image entiere, sommes s1 (des octets) et s2 (des s1). Paquets : bit 7 du nombre
   de morceaux (image codee). Sans compression : l'image telle quelle, comme avant. */
#define MSJ_N 32u
#define MSJ_TAB (MSJ_N * 16u)
static u32 ms_jw = 0, ms_jtot = 0, ms_jcz = 0, ms_kvi = 0xFFFFFFFFu;
static u8 ms_kh[16];                                       /* (v417 : en-tete de l'image de reference, gardee dans ref) */
/* une image (gardee) en paquets ; h : en-tete de 16 octets avant e (image de reference : ses octets sont dans ref) */
static MEM_OS void ms_emit(natres_pad_t *p, const u8 *h, const u8 *e, u32 len, u32 vi, u32 fl) {
    u8 *d = pn_fr + 42;
    u32 parts = (len + MS_PAQ - 1) / MS_PAQ, o = 0;
    for (u32 q = 0; q < parts; q++, o += MS_PAQ) {
        u32 k = len - o < MS_PAQ ? len - o : MS_PAQ;
        pn_p32(d, 0x47434D53u); pn_p32(d + 4, vi); pn_p32(d + 8, q << 16 | (ms_ab & 0xFFu) << 8 | (fl & 2u ? 0x80u : 0u) | parts);
        pn_p32(d + 12, len | (fl >> 8 & 0xFFu) << 24);
        for (u32 i = 0; i < k; i++) { u32 j = o + i; d[16 + i] = h ? (j < 16u ? h[j] : e[j - 16u]) : e[j]; }
        ms_udp(p, 16 + k);
    }
}
/* code l'image (cle : complete) dans o ; rend la longueur, 0 si plus longue que lim (alors : image complete) */
static MEM_OS u32 ms_code(u8 *o, u32 lim, u8 *ref, u32 cle) {
    u32 s1 = 0, s2 = 0, n = 0, pos = 0, saut = 0, run = 0, h = ms_cz ? 16u : 0u;
    u8 *rl = 0;
    n = h;
    for (u32 i = 0; i < ms_n; i++) {
        const u8 *src = (const u8 *)ms_ap[i];
        u32 m = ms_lp[i] & 0x1FFFu;
        for (u32 k = 0; k < m; k++, pos++) {
            u32 b = src[k];
            s1 += b; s2 += s1;
            if (!ms_cz) { o[n++] = (u8)b; continue; }
            if (cle) { ref[pos] = (u8)b; continue; }       /* (v417 : image de reference : seulement dans ref, pas dans le journal) */
            if (b == ref[pos]) { if (run) { rl[0] = (u8)(run >> 8); rl[1] = (u8)run; run = 0; } saut++; continue; }
            if (!run) {
                if (n + 4u > lim) return 0;
                o[n] = (u8)(saut >> 8); o[n + 1] = (u8)saut; rl = o + n + 2; n += 4; saut = 0;
            }
            if (n + 1u > lim) return 0;
            o[n++] = (u8)b; run++;
        }
    }
    if (run) { rl[0] = (u8)(run >> 8); rl[1] = (u8)run; }
    if (h) { u8 *t = cle ? ms_kh : o; pn_p32(t, cle ? H.vi_count : ms_kvi); pn_p32(t + 4, pos); pn_p32(t + 8, s1); pn_p32(t + 12, s2); }
    return cle && h ? 16u + pos : n;
}
/* renvoie les images gardees : pas encore parties (toujours), et 2, 7, 15, 31 images avant (si copies) */
static MEM_OS void ms_rejoue(natres_pad_t *p, u8 *tab, u8 *ref, u8 *jr, u32 copies) {
    for (u32 sl = 0; sl < MSJ_N; sl++) {
        u32 *e = (u32 *)(tab + sl * 16u), age = H.vi_count - e[0];
        if (!e[3] || !age || age > 255u) continue;
        if (!(e[3] & 1u) && (!copies || (age != 2u && age != 7u && age != 15u && age != 31u))) continue;
        if (e[1] == 0xFFFFFFFFu && e[0] != ms_kvi) { e[3] = 0; continue; }   /* (ancienne image de reference : plus dans ref) */
        if (e[3] & 1u) { ms_cr++; e[3] &= ~1u; }          /* (v414 : diagnostic) */
        if (e[1] == 0xFFFFFFFFu) ms_emit(p, ms_kh, ref, e[2], e[0], e[3]);
        else ms_emit(p, 0, jr + e[1], e[2], e[0], e[3]);
    }
}
static MEM_OS void ms_send(natres_pad_t *p) {
    if (!ms_n || !p->mip) return;
    if (tbl() - ms_tb > 10u * TB_SEC) { ms_n = 0; return; }   /* (l'appli ne renouvelle plus : fini ; v417 : 10 s au lieu de 3 : telephone gele un moment, les paquets l'attendent) */
    if (H.vi_count % ms_per) return;
    u32 imp = H.vi_count & 1, alt = (H.vi_count / ms_per) & 1, i, brut = 1;
    ms_tot = 0;
    for (i = 0; i < ms_n; i++) if (!ms_saute(ms_lp[i] >> 13, imp, alt)) { ms_tot += ms_out(ms_lp[i]); if (ms_lp[i] >> 13) brut = 0; }
    if (!ms_tot) return;
    u32 dv = H.vi_count - ms_prev; if (dv > 255u) dv = 255u;
    /* v416 : reserve (voir plus haut) : les images brutes seulement (RetroAchievements), sans l'envoi sans recopie */
    if (ms_ann && p->msr && !ms_nc && brut) {
        u8 *tab = (u8 *)p + p->msr;
        /* v417 : compression : l'image de reference n'est que dans ref (le journal ne garde que les changements, au plus un
           quart du journal chacun : sinon image de reference) : listes jusqu'a ~12 Ko dans 16 Ko (avant : ~3,9 Ko) */
        u32 cz = ms_cz, refsz = cz ? (ms_tot + 3u) & ~3u : 0, rs = p->len * 4u - p->msr, js = rs > MSJ_TAB + refsz ? rs - MSJ_TAB - refsz : 0,   /* (v508 : taille de la reserve = la fin de la structure : 16 Ko, ou moins pour un jeu qui manque de place) */
            need = (16u + ms_tot + 3u) & ~3u;
        u8 *ref = tab + MSJ_TAB, *jr = ref + refsz;
        if (cz && need > js / 4u) need = (js / 4u) & ~3u;
        if (cz ? js >= 1024u : js >= 3u * need) {
            if (ms_tot != ms_jtot || cz != ms_jcz) {     /* (autre taille : tout recommence) */
                for (i = 0; i < MSJ_N * 4u; i++) ((u32 *)tab)[i] = 0;
                ms_jtot = ms_tot; ms_jcz = cz; ms_jw = 0; ms_kvi = 0xFFFFFFFFu;
            }
            if (!ms_muet) ms_rejoue(p, tab, ref, jr, 0); /* (d'abord celles pas parties : leur place peut etre prise) */
            if (ms_jw + need > js) ms_jw = 0;
            u32 off = ms_jw, fin;
            u32 *ek = (u32 *)(tab + (ms_kvi & (MSJ_N - 1u)) * 16u);
            u32 cle = !cz || ms_kvi == 0xFFFFFFFFu || H.vi_count - ms_kvi >= 30u || !ek[3] || ek[0] != ms_kvi;
            u32 len = 0;
            fin = off;
            if (!cle) { len = ms_code(jr + off, need, ref, 0); fin = off + (len ? len : need); if (!len) cle = 1; }   /* (essai rate : ecrit jusqu'a need) */
            if (cle) { len = ms_code(jr + off, need, cz ? ref : 0, 1); if (!cz) fin = off + len; }   /* (reference : seulement dans ref) */
            for (i = 0; i < MSJ_N; i++) {                /* (images ecrasees : hors de la table) */
                u32 *e = (u32 *)(tab + i * 16u);
                if (e[3] && e[1] != 0xFFFFFFFFu && e[1] < fin && off < e[1] + e[2]) e[3] = 0;
            }
            if (cle && cz) ms_kvi = H.vi_count;
            u32 *e = (u32 *)(tab + (H.vi_count & (MSJ_N - 1u)) * 16u);
            e[0] = H.vi_count; e[1] = cle && cz ? 0xFFFFFFFFu : off; e[2] = len; e[3] = 4u | (cz ? 2u : 0u) | (ms_muet ? 1u : 0u) | dv << 8;
            if (!(cle && cz)) ms_jw = (off + len + 3u) & ~3u;
            if (ms_muet) return;
            for (u32 pass = 0; pass <= ms_dup; pass++) {
                if (cle && cz) ms_emit(p, ms_kh, ref, len, H.vi_count, e[3]);
                else ms_emit(p, 0, jr + off, len, H.vi_count, e[3]);
            }
            ms_prev = H.vi_count;
            ms_rejoue(p, tab, ref, jr, 1);
            if (PN_BBA && pn_hw == 1) { u32 m = irq_off(); ms_txwait(); irq_restore(m); }
            mt_oct += len;
            return;
        }
    }
    if (ms_muet) return;                                 /* (pas de reserve : rien a garder) */
    /* v408 : avec ms_dup, toute l'image est renvoyee 1 a 3 fois de suite (pas paquet par paquet) : une coupure breve du Wi-Fi
       n'emporte plus les deux exemplaires d'un meme paquet. Le jeu est arrete pendant l'interruption : memes octets. */
    for (u32 pass = 0; pass <= ms_dup; pass++) {
    ms_parts = (ms_tot + MS_PAQ - 1) / MS_PAQ; ms_part = 0; ms_o = 0;
    for (i = 0; i < ms_n; i++) {
        u32 l = ms_lp[i], f = l >> 13, n = l & 0x1FFFu, k = 0;
        if (ms_saute(f, imp, alt)) continue;
        const u8 *src = (const u8 *)ms_ap[i];
        if (f & 4) {
            const u32 *w = (const u32 *)src;
            int t0[3] = { ms_fx(w[3], 2), ms_fx(w[7], 2), ms_fx(w[11], 2) };
            if (!ms_nc) { ms_copie(p, src, 48); k = 48; } else for (; k < 48; k++) ms_put(p, src[k]);
            for (; k < n; k += 48) ms_mat(p, w + k / 4, t0);
        } else if (ms_nc && pn_hw == 1) {                 /* build 909 : octets bruts : directement depuis la memoire du jeu */
            while (k < n) {
                u32 t = n - k, r = MS_PAQ - ms_o;
                if (t > r) t = r;
                if (ms_ns >= MS_SEGS - 1) { ms_put(p, src[k]); k++; continue; }   /* (liste pleine : recopie, comme avant) */
                ms_seg(src + k, t); k += t; ms_o += t;
                if (ms_o == MS_PAQ) ms_put(p, 256u);
            }
        } else ms_copie(p, src + k, n - k);               /* (build 910 : par blocs) */
    }
    ms_put(p, 256u);
    }
    ms_prev = H.vi_count;
    if (PN_BBA && pn_hw == 1) { u32 m = irq_off(); ms_txwait(); irq_restore(m); }   /* (v371 : file libre pour les autres envois) */
    mt_oct += ms_tot;
}
static MEM_OS void mem_req(natres_pad_t *p, const u8 *f, const u8 *u, const u8 *d, u32 dispo) {
    if (!(H.opt & NRO_MEM)) return;
    u32 t = d[3], seq = pn_be32(d + 4), a = pn_be32(d + 8), b = pn_be32(d + 12), c = pn_be32(d + 16);
    p->mrx++;
    p->mip = pn_be32(f + 26); p->mport = pn_be16(u);
    for (int i = 0; i < 6; i++) p->mmac[i] = f[6 + i];
    if (t == 'R') { mem_blocks(p, seq, a, b); return; }
    if (t == 'S') { ms_req(p, d, dispo); return; }
    if (t == 'Q') { if (a == 0x51554954u) { mem_ack(p, seq); igr_tel = 1; } return; }   /* v418 : "QUIT" : retour au menu (comme A+Z+START) */
    if (t == 'P') p->mpause = a & 1;
    else if (t == 'W') {                                  /* (hors de la memoire du jeu, ou dans Pulse : rien, mais une reponse) */
        if ((b == 1 || b == 2 || b == 4) && mem_ok(a, b) && !(a & (b - 1)) && (a < NATRES_AT || a >= NATRES_AT + NATRES_SIZE)) {
            if (b == 1) *(volatile u8 *)a = (u8)c; else if (b == 2) *(volatile unsigned short *)a = (unsigned short)c; else *(volatile u32 *)a = c;
            __asm__ volatile("dcbf 0,%0\n sync" :: "r"(a) : "memory");
        }
    }
    else if (t == 'F') {                                  /* v307 : bloquer / liberer */
        u32 k = 0;
        while (k < mfz_n && mfz[k * 3] != a) k++;
        if (!a && !b) mfz_n = 0;
        else if (!b) { if (k < mfz_n) { mfz_n--; for (u32 j = k * 3; j < mfz_n * 3; j++) mfz[j] = mfz[j + 3]; } }
        else if ((b == 1 || b == 2 || b == 4) && mem_ok(a, b) && !(a & (b - 1)) && (a < NATRES_AT || a >= NATRES_AT + NATRES_SIZE) && (k < mfz_n || mfz_n < MFZ_MAX)) {
            mfz[k * 3] = a; mfz[k * 3 + 1] = b; mfz[k * 3 + 2] = c;
            if (k == mfz_n) mfz_n++;
        }
    }
    else if (t != 'I') return;
    mem_ack(p, seq);
}
static void pd_frame(natres_pad_t *p, u32 n, u32 now);
/* pause : on reste dans l'interruption de l'image (le jeu attend), son coupe (DMA audio du DSP), jusqu'a la reprise ou
   20 s sans paquet ; puis les gardiens du jeu fige repartent de maintenant */
static u32 mem_resumed = 0;
static MEM_OS void mem_pause(natres_pad_t *p) {
    volatile unsigned short *adma = (volatile unsigned short *)0xCC005036u;
    unsigned short a0 = *adma;
    *adma = a0 & 0x7FFFu;
    u32 last = tbl();
    while (p->mpause) {
        u32 n = pn_recv(), now = tbl();
        if (n) { last = now; pd_frame(p, n, now); }
        else if (now - last > 20u * TB_SEC) p->mpause = 0;
    }
    *adma = a0;
    mem_resumed = 1;
}
/* ======================= v331 : GBA du telephone (bus des manettes) =======================
   Le jeu parle a une GBA par le bus des manettes (commandes du bus JOY, format public (GBATEK) : 00 / FF type et etat,
   14 lire JOY_TRANS, 15 ecrire JOY_RECV, octets de poids faible d'abord ; reponse : type 0004, JOYSTAT). Le telephone est
   trop loin pour repondre a temps : Pulse repond tout de suite a sa place et lui fait suivre les mots du jeu.
   Demarrage de la GBA (le jeu lui envoie son programme) : Pulse joue le role de la console portable qui attend un
   programme : apres une remise a zero (FF), cle de session (JOY_TRANS, JOYSTAT 0x10) ; 1er mot ecrit : la taille
   (JOYSTAT 0x20) ; ensuite chaque mot fait basculer le bit 0x10 (le jeu attend cette bascule pour le mot suivant ; si le
   tampon des mots du telephone est plein, la bascule attend : le jeu ralentit). Tous les mots vont au telephone (il
   dechiffre le programme avec la cle de session). Programme recu : JOYSTAT 0x08 (un mot a lire), puis la 1re lecture
   (14) = programme lance : relais (JOY_TRANS et les drapeaux 0x30 viennent du telephone, ses lectures effacent 0x02). */
/* v344 : mots du jeu pas encore accuses par le telephone : 4096 (16 Ko, avant 256 : le demarrage du SDK abandonnait quand le
   tampon plein le faisait attendre), hors du resident, apres la section des manettes ; l'anneau du micro le suit */
#define GB_RING NATRES_GBA_RING
#define gb_ring ((u32 *)((u8 *)p + sizeof(natres_pad_t)))
#define MIC_RING(p) ((u8 *)(p) + sizeof(natres_pad_t) + ((p)->gbmask ? NATRES_GBA_RING * 4u : 0u))
static u32 gb_o0 = 0, gb_o1 = 0, gb_r0 = 0, gb_r1 = 0;    /* (tampon de sortie ecrit par le jeu ; reponse a lui rendre) */
static int gb_port(natres_pad_t *p, u32 ch) { return ch < 4 && (p->gbmask >> ch & 1); }
static MEM_OS void gb_reset(natres_pad_t *p) {           /* FF : cle de session (suite du generateur, longueur au hasard) */
    u32 k = tbl() | 1u, n = 5u + (k & 0x3FFu);
    while (n--) k = k * 0x6177614Bu + 1u;
    p->gkey = k; p->gtrans = k ^ 0x6F646573u; p->gjoy = 0x18u;   /* (0x10 : pret ; 0x08 : un mot a lire) */
    p->gst = 1; p->glen = p->gpos = 0; p->gw = p->ga = 0; p->gnew = 0; p->gtid = p->gtidlu = 0;
    p->gcode = p->gx = p->gfin = 0; p->grst = p->gid = 0; p->grw = p->gpred = 0; p->gfr = p->gfn = 0;
    p->gqr = p->gqw = p->gqlast = 0;
}
static MEM_OS void gb_cmd(natres_pad_t *p) {             /* debut d'un transfert vers le port GBA : la reponse, tout de suite */
    u32 c = gb_o0 >> 24;
    gb_r0 = gb_r1 = 0;
    if (c == 0x00u || c == 0xFFu) {
        p->gn00++;
        gb_r0 = 0x00040000u | (p->gjoy & 0xFFu) << 8;
        /* v340 : reponse a la remise a zero : JOYSTAT 0x08 seulement (cle de session dans JOY_TRANS), puis 0x18 aux
           demandes d'etat : ce que verifie le demarrage du SDK (table du demarrage de Rayman 3 : octets 37 puis 0) */
        /* v345 : programme lance : une remise a zero ne relance pas le demarrage (une vraie GBA ne le peut plus non plus) */
        if (c == 0xFFu && p->gst != 4) { gb_reset(p); gb_r0 = 0x00040800u; }
        if (c == 0xFFu && p->gst == 4) {                 /* v347 : la GBA emulee l'apprendra ; si on connait sa reponse : tout de suite */
            p->grst++; p->grw = p->gw; p->gpred = 0; p->gfr = 0; p->gqr = p->gqw;   /* (v365 : file de la GBA videe) */
            if (p->gid) { p->gtrans = p->gid; p->gjoy = 0x28u | (p->gjoy & 0x02u); }
        }
        return;
    }
    if (c == 0x14u) {                                    /* le jeu lit JOY_TRANS (octet de poids faible d'abord) */
        u32 t = p->gtrans;
        p->gn14++;
        gb_r0 = (t & 0xFFu) << 24 | (t >> 8 & 0xFFu) << 16 | (t >> 16 & 0xFFu) << 8 | t >> 24;
        gb_r1 = (p->gjoy & 0xFFu) << 24;
        /* v346 : programme recu : 1re lecture = la preuve (gx), puis JOYSTAT reste 0x08 : le code du jeu suit (2e lecture) */
        if (p->gst == 3 && !p->gfin) { p->gfin = 1; p->gtrans = p->gcode; return; }
        u32 avait8 = p->gjoy & 0x08u;
        if (p->gjoy & 0x08u) p->gtidlu = p->gtid;          /* (v333 : ce mot du telephone est lu : on le lui dira) */
        p->gjoy &= ~0x08u;
        /* v365 : mot de la file lu : le suivant tout de suite. v366 : seulement s'il etait annonce (0x08) et que c'est bien
           lui (le jeu lit aussi sans 0x08 : le mot etait perdu) ; tant qu'il en reste, 0x08 (avant : efface pour de bon par
           une lecture sans mot, la file restait bloquee et le jeu recommencait la connexion) */
        if (p->gqw != p->gqr) {
            if (avait8 && t == p->gq[p->gqr % 32u]) { p->gtidlu = p->gqid[p->gqr % 32u]; p->gqr++; }
            if (p->gqw != p->gqr) { p->gtrans = p->gq[p->gqr % 32u]; p->gjoy |= 0x08u; }
        }
        if (p->gst == 3) {                               /* (1re lecture apres le programme : il tourne) */
            p->gst = 4;
            /* v360 : l'identifiant que la GBA donnera a chaque remise a zero, connu d'avance : son code de jeu (mot 0xAC de
               l'en-tete : Tingle Tuner de Wind Waker, Rayman 3). Le jeu peut ne remettre a zero que deux fois (Wind Waker) : la
               1re reponse doit deja etre 0x28 + l'identifiant, comme une vraie GBA, sans attendre le telephone */
            if (!p->gid && p->gcode) p->gid = p->gcode;
        }
        return;
    }
    if (c == 0x15u) {                                    /* le jeu ecrit JOY_RECV */
        u32 j0 = p->gjoy;                                /* (v343 : la reponse donne JOYSTAT d'avant le mot, + 0x02 "recu") */
        int ech = 0;                                     /* (v363 : echo predit : la reponse reste JOYSTAT d'avant le mot) */
        u32 w = (gb_o0 >> 16 & 0xFFu) | (gb_o0 >> 8 & 0xFFu) << 8 | (gb_o0 & 0xFFu) << 16 | (gb_o1 >> 24) << 24;
        p->gn15++;
        /* v364 : Tingle Tuner connecte : entre deux messages, le jeu ecrit FFFFFFFF en continu (lecture / ecriture a chaque
           tour) ; la GBA les ignore, ils ne servent qu'a la faire repondre. Tous envoyes au telephone, ils s'empilaient
           (32 000 en retard) et ses vrais messages arrivaient trop tard (« Unable to link » apres 2 s). Un seul en route a
           la fois ; les messages du jeu (FEFEFEFE, numero, etat, longueur, donnees, somme) passent tous */
        int saute = 0;
        if (p->gst == 4 && (p->gcode & 0xFFFFFFu) == 0x4C5A47u) {
            if (p->gfr == 0) {
                if (w == 0xFEFEFEFEu) p->gfr = 1;
                else if (w == 0xFFFFFFFFu && p->gw != p->ga && gb_ring[(p->gw - 1u) % GB_RING] == 0xFFFFFFFFu) saute = 1;
            } else if (p->gfr == 1) p->gfr = 2;
            else if (p->gfr == 2) p->gfr = (w == 3u) ? 3u : 0u;
            else if (p->gfr == 3) { p->gfn = w > 4u ? (w + 3u) / 4u : 1u; p->gfr = 5; }
            else if (p->gfr == 5) { if (--p->gfn == 0) p->gfr = 4; }
            else p->gfr = 0;
        }
        if (saute) p->gnskip++;
        else if (p->gw - p->ga < GB_RING) { gb_ring[p->gw % GB_RING] = w; p->gw++; }
        if (p->gst == 1) {                               /* 1er mot : la taille du programme (format public de la cle) */
            u32 s = w ^ ((w & 0x200u) ? 0x6F646573u : 0x6177614Bu), n = (s >> 8) & 0x7Fu;
            if (s & 0x10000u) n += 0x80u;
            n = ((n << 7 | (s & 0x7Fu)) + 0x3Fu) << 3;
            p->glen = (n & 0x3FFF8u) + 0xCu; p->gpos = 0; p->gst = 2; p->gjoy = (p->gjoy & 0x0Au) | 0x20u;
        } else if (p->gst == 2) {
            if (p->gpos == 0xACu) p->gcode = w;              /* (v345 : code du jeu, en clair dans l'en-tete) */
            if (p->gpos == 0x1F8u) p->gx = w;                /* (v346 : preuve : produit des mots chiffres 0x1F8 et 0x1FC) */
            if (p->gpos == 0x1FCu) p->gx *= w;
            p->gpos += 4;
            if (p->gpos >= p->glen) { p->gst = 3; p->gjoy = 0x08u; p->gtrans = p->gx; }   /* (v346 : la preuve, puis le code du jeu) */
            else if (p->gw - p->ga < GB_RING - 1u) p->gjoy ^= 0x10u;
            else p->gnew = 1;                            /* (tampon plein : la bascule attend le telephone) */
        } else if (p->gid && w == p->gid && (p->gjoy & 0x38u) == 0x20u) {
            p->gjoy = (p->gjoy & ~0x32u) | 0x30u;          /* (v347 : le jeu renvoie l'identifiant : la GBA repond 0x30 et lit le mot) */
            p->gpred = 1;
        } else if (p->gpred == 1 && (p->gcode & 0xFFFFFFu) == 0x4C5A47u && (p->gjoy & 0x38u) == 0x30u) {
            /* v363 : Tingle Tuner (code GZL.) : le mot suivant (cle de la partie) est renvoye tel quel par la GBA, des sa
               reception (JOY_TRANS, 0x08) : donne d'avance au jeu, qui le relit tout de suite ; par le telephone, il arrivait
               trop tard (le jeu abandonnait, la GBA affichait « Unable to link » 2 s plus tard). L'appli ne le redonne pas */
            p->gtrans = w; p->gjoy |= 0x08u; p->gpred = 2; ech = 1;
        } else if (p->gst != 4) p->gjoy |= 0x02u;
        /* v349 : programme lance : comme une vraie GBA (son gestionnaire du cable lit JOY_RECV des qu'il arrive), le mot est
           dit lu tout de suite (le telephone le recoit dans l'ordre) : Rayman 3 attend « lu » apres chaque mot, et un aller-
           retour par le Wi-Fi par mot le deconnectait (« La connexion a echoue » en pleine partie) */
        /* v343 : comme une vraie GBA : la reponse a 15 est JOYSTAT a la fin du transfert, avant que le BIOS n'ait traite le
           mot (0x12 pour la cle B, et non 0x22) : le demarrage du SDK le verifie (Rayman 3 recommencait apres le 1er mot) */
        gb_r0 = (((p->gst == 4 && !ech ? p->gjoy : j0) | 0x02u) & 0xFFu) << 24;
        return;
    }
}
static MEM_OS void gb_rx(natres_pad_t *p, const u8 *f, const u8 *u, const u8 *d, u32 dispo) {   /* paquet "GCGB" du telephone */
    if (dispo < 24 || !p->gbmask) { p->bad++; return; }   /* (v334 : une seule GBA, sur le port choisi : le port du paquet n'est plus lu) */
    p->gnrx++;
    p->gip = pn_be32(f + 26); p->gport = pn_be16(u);
    p->gmac0 = pn_be32(f + 6); p->gmac1 = pn_be16(f + 10) << 16;
    u32 ack = pn_be32(d + 12), t = pn_be32(d + 16), o = pn_be32(d + 20), sq = pn_be32(d + 8);
    /* v350 : un paquet plus ancien que le dernier pris (arrive en retard, apres un plus recent) est ignore : il redonnait au
       jeu un ancien JOY_TRANS (« La connexion a echoue ») ; numero <= 16 : l'appli a recommence */
    if (p->gseq && sq > 16u && (int)(sq - p->gseq) <= 0 && p->gseq - sq < 1000u) return;
    p->gseq = sq;
    if (ack - p->ga <= p->gw - p->ga) p->ga = ack;       /* (accuse dans [ga, gw]) */
    if (o & 4u) { p->gst = 0; p->gjoy = 0; p->gw = p->ga = 0; p->gnew = 0; p->gtid = p->gtidlu = 0; p->grst = p->gid = 0; p->grw = p->gpred = 0; p->gqr = p->gqw = p->gqlast = 0; return; }
    /* v427 : GBA du telephone allumee sur sa cartouche (Pokemon Colosseum, XD) : aucun programme a recevoir ; le jeu de la
       cartouche ecoute lui-meme le cable : relais des maintenant (compteurs a 0 : le telephone repart de 0 aussi) */
    if ((o & 0x40u) && p->gst != 4) {
        p->gst = 4; p->gjoy = 0; p->gw = p->ga = 0; p->gnew = 0; p->gtid = p->gtidlu = 0; p->grst = p->gid = 0; p->grw = p->gpred = 0;
        p->gqr = p->gqw = p->gqlast = 0; p->gcode = p->gx = p->gfin = 0; p->glen = p->gpos = 0; p->gfr = p->gfn = 0;
    }
    u32 id = (o >> 16) & 0xFFu;                          /* (v333 : un JOY_TRANS deja donne n'est pas redonne) */
    /* v347 : appli qui suit les remises a zero (bit 4) : JOY_TRANS et drapeaux d'avant la derniere remise a zero ignores */
    int frais = !(o & 0x10u) || (o >> 24) == (p->grst & 0xFFu);
    /* (v348 : l'identifiant (JOY_TRANS avec 0x20, reponse a une remise a zero) est appris meme en retard : le jeu remet a
       zero a chaque image, le paquet arrive toujours apres la suivante ; il ne change pas) */
    if ((o & 1u) && (o & 0x10u) && p->grst && ((o >> 8) & 0x30u) == 0x20u && t) p->gid = t;
    if ((o & 1u) && frais && (!id || id != p->gtid)) { p->gtrans = t; p->gjoy |= 0x08u; p->gtid = id; }
    if (o & 2u) p->gjoy &= ~0x02u;
    /* v351 : 0x30 predit : les drapeaux d'avant (0x20 : la GBA n'a pas encore recu l'identifiant renvoye) ne le defont pas ;
       le jeu, connecte, s'arretait en les voyant */
    if (p->gpred && (o & 8u) && ((o >> 8) & 0x30u) != 0x30u) frais = 0;
    if ((o & 8u) && ((o >> 8) & 0x30u) == 0x30u) p->gpred = 0;
    if ((o & 8u) && frais) p->gjoy = (p->gjoy & ~0x30u) | ((o >> 8) & 0x30u);
    /* v365 : file des mots de la GBA (Tingle Tuner connecte) : chaque mot une fois, dans l'ordre (numeros 1-255 ; un plus
       recent que le dernier pris, dans la moitie qui suit, est pris : apres une remise a zero l'appli a saute ceux d'avant) */
    if ((o & 0x20u) && frais && dispo >= 32u && p->gst == 4) {
        u32 k = pn_be32(d + 24), i0 = pn_be32(d + 28) & 0xFFu;
        if (k > 32u) k = 32u;
        if (dispo < 32u + 4u * k) k = (dispo - 32u) / 4u;
        for (u32 i = 0; i < k; i++) {
            u32 id = (i0 - 1u + i) % 255u + 1u, df = (id + 255u - (p->gqlast % 255u)) % 255u;
            if (p->gqlast && (df == 0 || df > 128u)) continue;   /* (deja pris) */
            if (p->gqw - p->gqr >= 32u) break;
            u32 w = pn_be32(d + 32 + 4 * i);
            p->gq[p->gqw % 32u] = w; p->gqid[p->gqw % 32u] = (u8)id; p->gqw++; p->gqlast = id; p->gqn++;
        }
    }
    if (p->gqw != p->gqr && p->gst == 4) { p->gtrans = p->gq[p->gqr % 32u]; p->gjoy |= 0x08u; }   /* (v366 : la tete de la file, annoncee) */
}
/* v332 : micro du telephone : les octets du paquet "GCVO" (echantillons 16 bits, deja a la frequence du jeu) dans l'anneau
   (les plus vieux sautes s'il deborde) */
static MEM_OS void mic_rx(natres_pad_t *p, const u8 *f, const u8 *u, const u8 *d, u32 dispo) {
    u32 n = pn_be32(d + 8);
    p->vip = pn_be32(f + 26); p->vport = pn_be16(u); p->vmac0 = pn_be32(f + 6); p->vmac1 = pn_be16(f + 10) << 16;   /* (v352 : meme micro coupe : on lui repond) */
    if (!p->micon || n > 1472u || n + 12u > dispo) { p->bad++; return; }
    p->mnrx++;
    for (u32 i = 0; i < n; i++) MIC_RING(p)[p->mw++ & (NATRES_MIC_RING - 1u)] = d[12 + i];
    if (p->mw - p->mr > NATRES_MIC_RING) p->mr = p->mw - NATRES_MIC_RING;
}
static void mic_ready(natres_pad_t *p);
static u32 mic_flags(natres_pad_t *p);
static MEM_OS __attribute__((noinline)) void mic_tx(natres_pad_t *p) {             /* (toutes les 30 images) etat du micro emule, vers le telephone */
    static u32 tick = 0;
    if (!p->vip || ++tick % 10u) return;
    u8 *uu = pn_fr + 42, mac[6];
    const u32 *w = &p->mstat;                            /* (mstat ... mlow : 8 mots, dans l'ordre de natres.h) */
    pn_p32(uu, NATRES_MIC_MAGIC); pn_p32(uu + 4, 0x02000000u);
    for (u32 i = 0; i < 8; i++) pn_p32(uu + 8 + i * 4, w[i]);
    /* v352 (diagnostic) : selections du slot B par le jeu, lectures de l'identifiant, drapeaux : bit 0 micro active dans GC
       Station, bit 1 slot B = micro emule, bit 2 slot B emule (Pulse y repond) */
    pn_p32(uu + 40, p->mnsel); pn_p32(uu + 44, p->mnid);
    pn_p32(uu + 48, mic_flags(p)); pn_p32(uu + 52, p->mnrefill);   /* (v354 : + coupures du son, reserve refaite) */
    pn_p32(mac, p->vmac0); pn_p16(mac + 4, p->vmac1 >> 16);
    pn_udp(p, mac, p->vip, p->vport, 56);
}
static MEM_OS void gb_tx(natres_pad_t *p) {              /* (chaque image) : mots du jeu au telephone, etat */
    static u32 tick = 0, lu = 0;
    if (p->gnew && p->gw - p->ga < GB_RING - 1u) { p->gnew = 0; p->gjoy ^= 0x10u; }
    if (!p->gip) return;
    u32 n = p->gw - p->ga;
    if (!n && p->gtidlu == lu && (++tick % 15u)) return;   /* (v333 : un mot du telephone lu par le jeu : dit tout de suite) */
    lu = p->gtidlu;
    /* v344 : jusqu'a 4 paquets de 300 mots par image (avant 1) : le telephone suit le debit du demarrage */
    u32 ch = p->gbmask & 1 ? 0 : p->gbmask & 2 ? 1 : p->gbmask & 4 ? 2 : 3;
    const u32 dg[13] = { p->poll, p->redir, p->tr[(p->ntr - 1) & 7], p->gdirq, p->gdsr0, p->gdsr1, p->gdcom, p->gdtc, p->gdbuf, p->gdrep,
                         p->gbmask | (p->gdnirq > 0xFFFFFFu ? 0xFFFFFFu : p->gdnirq) << 8, H.trap_state << 24 | (H.trap_n & 0xFFFFFFu), p->grst };   /* (v347 : + remises a zero) */
    (void)ch;
    for (u32 k = 0, off = 0; k < 4u && (k == 0 || off < n); k++, off += 300u) {
        u32 m = n - off > 300u ? 300u : n - off, a0 = p->ga + off;
        u8 *uu = pn_fr + 42, mac[6];
        pn_p32(uu, NATRES_GBA_MAGIC); uu[4] = 2; uu[5] = (u8)p->gtidlu; uu[6] = (u8)p->gst; uu[7] = (u8)p->gjoy;
        pn_p32(uu + 8, p->gkey); pn_p32(uu + 12, p->glen); pn_p32(uu + 16, p->gpos); pn_p32(uu + 20, a0); pn_p32(uu + 24, m);
        pn_p32(uu + 28, p->gn00); pn_p32(uu + 32, p->gn14); pn_p32(uu + 36, p->gn15);
        for (u32 i = 0; i < m; i++) pn_p32(uu + 40 + i * 4, gb_ring[(a0 + i) % GB_RING]);
        for (u32 i = 0; i < 13; i++) pn_p32(uu + 40 + m * 4 + i * 4, dg[i]);
        pn_p32(uu + 40 + m * 4 + 52, ++p->gntx2);         /* (v350 : numero du paquet, pour l'ordre cote telephone) */
        pn_p32(uu + 40 + m * 4 + 56, p->grw);             /* (v351 : mots ecrits avant la derniere remise a zero) */   /* (v335 : diagnostic du port de la GBA, apres les mots) */
        pn_p32(mac, p->gmac0); pn_p16(mac + 4, p->gmac1 >> 16);
        pn_udp(p, mac, p->gip, p->gport, 40 + m * 4 + 60);
        p->gntx++;
    }
}

static u32 pd_bba_n = 0, pd_bba_tb = 0;                  /* (derniers acces du jeu au BBA) */
static int pd_busy(void) { return ((bx[0] & 0x380u) & ~pn_cs) || (bx[3] & 1u); }   /* (le jeu se sert du canal de l'adaptateur) */
static void MEM_OS pd_frame(natres_pad_t *p, u32 n, u32 now) {
    const u8 *f = pn_fr;
    if (n < 42) return;
    u32 type = pn_be16(f + 12);
    if (type == 0x0806) {
        if (pn_be16(f + 20) == 1 && pn_be32(f + 38) == p->net.ip) { u8 m[6]; for (int i = 0; i < 6; i++) m[i] = f[22 + i]; pn_arp(2, m, pn_be32(f + 28)); p->arp++; }
        return;
    }
    if (type != 0x0800 || f[23] != 17 || pn_be32(f + 30) != p->net.ip) return;
    const u8 *u = f + 14 + (f[14] & 15) * 4, *d = u + 8;
    if (pn_be16(u + 2) != NATRES_PAD_PORT) return;
    if (d + 20 > f + n) { p->bad++; return; }
    if ((pn_be32(d) & 0xFFFFFF00u) == 0x47434D00u) { mem_req(p, f, u, d, (u32)(f + n - d)); return; }   /* v306 : "GCM?" */
    if (pn_be32(d) == NATRES_GBA_MAGIC) { gb_rx(p, f, u, d, (u32)(f + n - d)); return; }   /* v331 : GBA du telephone */
    if (pn_be32(d) == NATRES_MIC_MAGIC) { mic_rx(p, f, u, d, (u32)(f + n - d)); return; }   /* v332 : micro du telephone */
    if (pn_be32(d) != 0x47435044u) { p->bad++; return; }   /* "GCPD" */
    u32 ch = d[5] & 3;
    if (!(pd_cm0 >> ch & 1)) { p->bad++; return; }
    u32 seq = pn_be32(d + 8);
    if (seq == p->seq[ch]) return;                        /* (deja vu) */
    /* v383 : d[6] bit 0 = bongos (DK Bongos : Donkey Konga, Jungle Beat). Les vrais bongos se disent manette standard (0x0900)
       mais n'ont pas le bit 0x0080 des touches : c'est ce que le jeu regarde pour les reconnaitre (lu dans Donkey Konga 2,
       adresses seulement : 0x800B0B5C) ; il prend alors la gachette R pour le niveau du micro. Ce bit n'est donc plus
       ajoute pour un port bongos */
    p->seq[ch] = seq; p->h[ch] = pn_be32(d + 12) | ((d[6] & 3) ? 0u : 0x00800000u); p->l[ch] = pn_be32(d + 16); p->tb[ch] = now; p->rx++; p->chmask = pd_cm0;
    /* v384 : d[6] bit 1 = clavier : INBUFH porte alors les 3 touches enfoncees (codes du clavier, octets 1 a 3) ; la
       reponse au jeu (8 octets : compteur, 0, 0, 0, touches, controle) est faite a sa lecture (kb_h, kb_l) */
    if (d[6] & 2) { p->kbmask |= 1u << ch; p->h[ch] &= 0x00FFFFFFu; p->l[ch] = 0; } else p->kbmask &= ~(1u << ch);
    p->pip[ch] = pn_be32(f + 26); p->pport[ch] = pn_be16(u); for (int i = 0; i < 6; i++) p->pmac[ch][i] = f[6 + i];   /* (v275 : pour la vibration) */
}
void pd_poll(void) __attribute__((used));
void MEM_OS pd_poll(void) {
    natres_pad_t *p = pd_find();
    if (!p || p->state == 2) return;
    u32 now = tbl();
    /* le jeu se sert du BBA (Mario Kart Double Dash le cherche au demarrage, pour le jeu en reseau local) : on le laisse,
       puis, 2 s apres son dernier acces, on le reprend (remis a zero et regle de nouveau) */
    if (H.bba_n != pd_bba_n) { pd_bba_n = H.bba_n; pd_bba_tb = now; p->state = 3; }
    if (p->state == 3) { if (now - pd_bba_tb < 2u * TB_SEC) { if (ms_n) ms_cs++; return; } p->state = 0; }
    bx = HW6(0xCC006800u + pn_ch * 0x14u);                /* (v327 : canal de l'adaptateur : 0, ou 1 pour un ETH2GC en B) */
    if (pd_busy()) {
        /* v412 : le jeu se sert du bus : l'image n'est pas envoyee, mais elle est preparee et gardee dans l'anneau (rien
           n'est envoye : seulement la memoire) : elle repart avec les copies 2, 7, 15 et 31 images plus tard. Avant, elle
           etait perdue pour de bon (RetroAchievements : "pas envoyees par la console" ; hardcore : mode normal). */
        if (p->state == 1 && ms_ann && p->msr && !ms_nc) { ms_muet = 1; ms_send(p); ms_muet = 0; ms_cb++; }
        else if (ms_n) ms_cs++;
        return;
    }
    if (!p->state) {
        pn_cfg = &p->net;
        int r = pn_bba_init();
        if (r == -2) return;                              /* (v327 : un canal pris par le jeu : on reessaiera) */
        if (r) { p->state = 2; return; }
        p->state = 1;
    }
    for (int k = 0; k < 8; k++) {
        u32 n = pn_recv();
        if (!n) break;
        pd_frame(p, n, now);
    }
    if (p->mpause) mem_pause(p);                          /* v306 : jeu en pause pour le chercheur de codes */
    {   /* v370 : le direct, mesure ; une fois par seconde : la mesure a l'appli */
        u32 t0 = tbl();
        ms_send(p);                                      /* (appli : le direct) */
        u32 us = tbl() - t0;                             /* (v372 : en tops d'horloge, 40,5 par us : l'appli divise) */
        if (ms_n) { mt_sum += us; mt_n++; if (us > mt_max) mt_max = us; }
        if (ms_n && p->mip && mt_n >= 60) {               /* v372 : "GCMT" brut : sommes, pires, nombre d'images (28 octets) */
            u8 *b = pn_fr + 42;
            pn_p32(b, 0x47434D54u); pn_p32(b + 4, H.vi_count); pn_p32(b + 8, mt_sum); pn_p32(b + 12, mt_max); pn_p32(b + 16, H.cnt_pe);   /* v495 : images dessinees par le jeu (jetons du GP) : images par seconde dans l'appli */
            pn_p32(b + 20, mt_oct); pn_p32(b + 24, mt_n);
            pn_p32(b + 28, ms_cb << 16 | (ms_cr & 0xFFFFu)); pn_p32(b + 32, ms_cs << 16 | (ms_cw & 0xFFFFu));   /* v414 : diagnostic (36 octets) */
            pn_udp(p, p->mmac, p->mip, p->mport, 36);
            mt_sum = mt_n = mt_max = mt_oct = 0;
        }
    }
    if (p->gbmask) gb_tx(p);                             /* v331 : GBA du telephone */
    if (p->micon) mic_ready(p);                           /* v332 : micro du telephone : un paquet pret ? */
    mic_tx(p);                                           /* (v352 : l'etat, meme micro coupe dans GC Station : le telephone le dit) */
    if (p->mach && p->mip) { p->mach--; mem_ach(p); }     /* v306 : succes obtenu : annonce au telephone */
    {   /* v275 : vibration demandee par le jeu : envoyee au telephone quand elle change, puis toutes les 8 images tant
           qu'elle dure (l'appli s'arrete seule sans nouvelles : un paquet perdu ne la laisse pas vibrer) */
        static u32 vi_n = 0;
        vi_n++;
        for (u32 ch = 0; ch < 4; ch++) {
            if (!(p->chmask >> ch & 1) || !p->pip[ch]) continue;
            u32 on = p->rmb[ch];
            if (on == p->rsent[ch] && !(on && (vi_n & 7) == 0)) continue;
            if (on && !p->rsent[ch]) p->nrmb++;
            p->rsent[ch] = on;
            u8 *uu = pn_fr + 42;
            pn_p32(uu, 0x47435242u); uu[4] = (u8)ch; uu[5] = (u8)on; uu[6] = uu[7] = 0;   /* "GCRB" */
            pn_udp(p, p->pmac[ch], p->pip[ch], p->pport[ch], 8);
        }
    }
    H.net_pk = p->rx; H.net_rt = p->state << 24 | (p->arp & 0xFFF) << 12 | (p->bad & 0xFFF);   /* (journal : pas de H.net ici) */
    for (u32 ch = 0; ch < 4; ch++)
        if ((p->chmask >> ch & 1) && now - p->tb[ch] > TB_SEC) {   /* (au repos ; v384 : un clavier reste un clavier, sans touche) */
            if (p->kbmask >> ch & 1) { p->h[ch] = 0; p->l[ch] = 0; } else { p->h[ch] = 0x00808080u; p->l[ch] = 0x80800000u; } }
}
static void pd_ev(natres_pad_t *p, u32 t, u32 d) {      /* (journal : 20 premiers, puis les 20 derniers) */
    u32 i = p->nev < 20 ? p->nev : 20 + (p->nev - 20) % 20;
    p->ev[i] = t << 24 | (d & 0xFFFFFFu); p->nev++;
}
/* v384 : reponse d'un clavier (commande 0x54, 8 octets) : octet 0 = compteur (4 bits), octets 4 a 6 = touches enfoncees,
   octet 7 = controle (touches et compteur melanges par ou exclusif) : ce que lit un vrai clavier (voir source/gckb.c) */
static u32 kb_h(natres_pad_t *p) { p->kbcnt = (p->kbcnt + 1) & 15u; return p->kbcnt << 24; }
static u32 kb_l(natres_pad_t *p, u32 ch) {
    u32 k = p->h[ch] & 0x00FFFFFFu, c = (k >> 16 ^ k >> 8 ^ k ^ p->kbcnt) & 0xFFu;
    return k << 8 | c;
}
static MEM_OS u32 pd_si_load(natres_pad_t *p, u32 o, u32 v, int *done) {
    *done = 1;
    if (o < 0x30u) {
        u32 ch = o / 12u, r = o % 12u;
        if (r == 4) p->ninb[ch]++;                        /* (journal : quels ports le jeu lit) */
        if ((p->chmask & p->kbmask) >> ch & 1) { if (r == 4) { H.si_fixes++; return kb_h(p); } if (r == 8) return kb_l(p, ch); }   /* (v384 : clavier) */
        if (p->chmask >> ch & 1) { if (r == 4) { H.si_fixes++; return p->h[ch]; } if (r == 8) return p->l[ch]; }   /* (journal : lectures donnees) */
        *done = 0; return v;
    }
    if (o == 0x38u) {                                     /* SISR : donnees neuves, aucune erreur sur les ports du telephone */
        p->nsr++;
        for (u32 ch = 0; ch < 4; ch++) if (p->chmask >> ch & 1) { u32 s = (3 - ch) * 8; v = (v & ~(0x3Fu << s)) | (0x20u << s); }
        if (p->gbmask) p->gdsr0 = v;                     /* (v335 : diagnostic) */
        for (u32 ch = 0; ch < 4; ch++) if (p->gbmask >> ch & 1) v &= ~(0x3Fu << (3 - ch) * 8);   /* (v331 : GBA : sans erreur) */
        if (p->gbmask) p->gdsr1 = v;
        return v;
    }
    int mine = p->xch < 4 && (p->chmask >> p->xch & 1), gm = gb_port(p, p->xch);
    if (o >= 0x80u && o < 0x100u && gm) { u32 w = (o - 0x80u) / 4u, r = w == 0 ? gb_r0 : w == 1 ? gb_r1 : 0; p->nbuf++; p->gdbuf++; p->gdrep = r; return r; }   /* v331 : reponse de la GBA */
    if (o == 0x34u && (mine || gm)) {                     /* COMCSR : pas d'erreur de transfert (et le port demande par le jeu) */
        p->ncom++; if ((v & 0x80000000u) && p->ncom < 4000u) pd_ev(p, 5, v >> 8);
        v = (v & ~0x20000006u) | p->xch << 1;
        if (gm) { p->gdcom = v; if (v & 0x80000000u) p->gdtc++; }   /* (v335 : diagnostic) */
        return v;
    }
    if (o >= 0x80u && o < 0x100u && mine) {               /* reponse d'une commande envoyee au port du telephone */
        u32 w = (o - 0x80u) / 4u;
        p->nbuf++;
        if (p->xcmd != 0x00 || p->nbuf < 4) pd_ev(p, 4, (p->xcmd & 0xFF) << 8 | w);
        int kb = p->kbmask >> p->xch & 1;                 /* (v384 : ce port est un clavier) */
        if (p->xcmd == 0x00 || p->xcmd == 0xFF) { if (!w) p->types++; return w ? 0 : kb ? 0x08200000u : 0x09000000u; }   /* manette standard (v384 : ou clavier) */
        if (p->xcmd == 0x54 && kb) return w == 0 ? kb_h(p) : w == 1 ? kb_l(p, p->xch) : 0;   /* (v384 : touches du clavier) */
        if (p->xcmd == 0x41 || p->xcmd == 0x42) { if (!w) p->origins++; return w == 0 ? 0x00808080u : w == 1 ? 0x80801F1Fu : 0; }   /* origine */
    }
    *done = 0;
    return v;
}

#else                                                     /* (Pulse LAN : pas de telephone ni de chercheur de codes : place pour le pont ; v452 : Pulse leger aussi) */
static natres_pad_t *const pd = 0;
static natres_pad_t *pd_find(void) { return 0; }
static int mem_resumed = 0, mfz_n = 0;
static u32 gb_r0, gb_r1, gb_o0, gb_o1;
static void mfz_run(void) { }
static int gb_port(natres_pad_t *p, u32 c) { (void)p; (void)c; return 0; }
static void gb_cmd(natres_pad_t *p) { (void)p; }
static void pd_ev(natres_pad_t *p, u32 k, u32 v) { (void)p; (void)k; (void)v; }
static void mic_ready(natres_pad_t *p) { (void)p; }
void pd_poll(void) __attribute__((used));
static u32 pd_si_load(natres_pad_t *p, u32 o, u32 v, int *done) { (void)p; (void)o; *done = 0; return v; }
#endif

/* ---------- v57 : crochets dans la table des interruptions du jeu ----------
   Le systeme des jeux (SDK Nintendo) range ses fonctions d'interruption dans une table a
   0x80003040 (une par source : 20 = manettes SI, 21 = lecteur DI, 24 = image VI).
   - VI (60 fois par seconde) : le journal est ecrit toutes les 3 s, meme si le jeu est bloque ;
   - SI : APRES la fonction du jeu (elle a deja lu les manettes), A+Z+START tenus 1 s
     = retour a GC Station : redemarrage de la console (PicoLoader -> ipl.dol). */
#define OS_INT_TABLE ((volatile u32 *)0x80003040u)
#define RD_CHUNK 2048u                                    /* v61 : morceau d'une lecture en fond (v88 : 8 Ko -> 2 Ko) */
#define RD_BUDGET (TB_SEC / 1000u * 3u)                  /* v436 : 3 ms (avant 1,5 ms : videos des jeux servies trop lentement sans DMA) ; v88 : au plus ~1,5 ms de lecture par interruption (le son du jeu
                                                             attend : une sortie audio toutes les ~5 ms) */
static u32 si_orig, igr_hold = 0;
static void hot_reset(void);
/* ---------- v64 : retour a GC Station ----------
   Le jeu est abandonne : on arrete ce qui tourne (interruptions, son, DSP, dessin), puis le
   .dol de GC Station est relu sur la carte (meme lecteur SD que pour l'ISO) et lance.
   GC Station (libogc) remet tout le reste en route. Sans .dol prepare : redemarrage. */
static void trap_disarm(void);
static void vc_flush_all(void);
static MEM_OS void ret_to_gcstation(void) {
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
           bloquait (Â« GX ne repond plus Â» au demarrage, apres A+Z+START dans Cocoto) */
        pi[6] = 1;
        for (volatile u32 i = 0; i < 4000u; i++) ;           /* (~20 us ; pas l'horloge : arretee dans le banc d'essai) */
        pi[6] = 0;
        for (volatile u32 i = 0; i < 1000u; i++) ;
    }
    {   /* v456 : video remise a zero et eteinte : VIDEO_Init de GC Station refait alors l'initialisation complete,
           au lieu de garder le 480p du jeu (ecran brouille ou fige apres A+Z+START, Tony Hawk's Pro Skater 4) */
        volatile unsigned short *vi = (volatile unsigned short *)0xCC002000u;
        vi[1] = 2;
        for (volatile u32 i = 0; i < 2000u; i++) ;
        vi[1] = 0;
    }
    H.net = 0;                                           /* v143 : (le .dol de GC Station est sur la carte SD) */
    if (!di_ready) di_ready = nr_init() == 0;
    vc_flush_all();                                      /* v512 : ecritures de la carte virtuelle pas encore faites */
    natres_hdr.ext = (u32)ret_tab;                       /* v159 : la table du .dol de GC Station, en place (plus de recopie) */
    H.n_ext = H.ret_n; ext_last = 0; sec_cached = 0xFFFFFFFFu;
    static u8 dh[256] __attribute__((aligned(32)));
    if (!di_ready || nr_read(dh, 0, 256)) hot_reset();
    const u32 *w = (const u32 *)dh;                      /* (en-tete du .dol, big-endian comme la console) */
    u32 entry = w[0xE0 / 4];
    if (entry < 0x80003000u || entry >= NATRES_AT) hot_reset();
    for (int i = 0; i < 18; i++) {
        u32 off = w[i], addr = w[0x48 / 4 + i], sz = w[0x90 / 4 + i];
        if (!sz) continue;
        if (addr < 0x80003000u || addr + sz > NATRES_AT || off + sz > H.ret_size) hot_reset();
        if (nr_read((void *)addr, off, sz)) hot_reset();
        cache_flush(addr, sz);
        for (u32 a = addr & ~31u; a < addr + sz; a += 32) __asm__ volatile("icbi 0,%0" :: "r"(a));
    }
    if (xv_st) xv_drain();                               /* v446 : la carte rendue au repos avant GC Station : lecture en continu */
    st_close(); sd_end();                                /* (v434) arretee (CMD12), CS relache ; sinon la carte envoyait encore ses
                                                            secteurs, GC Station ne la montait pas et gardait ses reglages par defaut */
    __asm__ volatile("sync; isync" ::: "memory");
    ((void (*)(void))entry)();                           /* (GC Station : ne revient pas) */
    hot_reset();
}
/* (sur une pile a lui : celle du jeu peut etre ecrasee par le .dol qu'on recharge) */
void ret_go(void) __attribute__((used));
void ret_go(void) { ret_to_gcstation(); }
extern void ret_entry(void);
__asm__(".pushsection .text\n.globl ret_entry\nret_entry:\n"
        "  lis 1,exc_stack@ha\n  addi 1,1,exc_stack@l\n  addi 1,1,1584\n  b ret_go\n.popsection\n");
static MEM_OS void hot_reset(void) {
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
static void crash_dump(u32 ex, u32 ctx, u32 dar, u32 dsisr);
static u32 fd_ctx = 0;                                   /* v402 : le jeu interrompu par la derniere interruption vue (journal avant la 1re image) */
static MEM_OS void alive_tick(void) {
    ++alive_n;                                           /* (le jeu a pu remettre sa fonction : on se rebranche) */
    if (!H.vi_count || (alive_n & 63) == 0) hooks_check();   /* v402 : avant la 1re image, a chaque fois (Rayman 3 : image jamais branchee) */
    sw_tick();                                           /* v121 : changement de disque en attente ? */
    if (vc_pending()) x_kick();                          /* (carte : interruption pas encore donnee) */
    /* (une ecriture sur la carte coupe le son quelques ms : toutes les 3 s la 1re minute, puis toutes les 20 s)
       v515 : plus du tout une fois que le jeu affiche des images. Mesure (Kirby Air Ride, build 1042) : jusqu'a 18-20 ms
       d'interruptions coupees a chaque reecriture, le son du jeu (toutes les 5 ms) gresillait. Le journal est ecrit a la fin
       (A+Z+START, plantage, redemarrage demande par le jeu) ; avant la 1re image (jeux qui se figent au demarrage), comme avant. */
    if (!H.vi_count && tbl() - H.flush_tb > TB_SEC * 3) {
        nr_log_flush();
        if (!H.vi_count && fd_ctx) crash_dump(0xFD, fd_ctx, 0, 0);   /* v402 : pas encore d'image : registres, appels, fils du jeu, interruptions */
    }
}
#define NR_HOOK(nm, counter) \
    static u32 nm##_orig = 0; \
    static void nm##_hook(u32 irq, u32 ctx) { \
        EVC(0x42, ctx); \
        if (nm##_orig) ((void (*)(u32, u32))nm##_orig)(irq, ctx); \
        PF_T0(pt_); \
        EVE(0x4A, 0, 0); \
        counter++; fd_ctx = ctx; alive_tick(); di_tick(ctx, 0); \
        PF_ADD(5, pt_); \
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
static MEM_OS void freeze_check(u32 ctx) {
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
static void nr_sample(u32 ctx);
static MEM_OS void vi_hook(u32 irq, u32 ctx) {                  /* v66 : entree comptee avant la fonction du jeu */
    PF_T0(pt_);
    trap_guard();
#if defined(NATRES_LAN)
    pd_poll();                                            /* (Pulse LAN : signe de vie du pont) */
#elif !defined(NATRES_LITE)                              /* (v452 : Pulse leger : ni telephone ni chercheur de codes) */
    if (H.opt & NRO_PHONE) {                             /* manettes du telephone : avant que le jeu lise les manettes (v487 : le chercheur
                                                            de codes seul : apres la fonction d'image du jeu, plus bas) */
        pd_poll();
        pn_link_check();                                 /* v372 */
    }
#endif        /* manettes du telephone, chercheur de codes : avant que le jeu lise les manettes */
    if (mem_resumed) { mem_resumed = 0; frz_tb = tbl(); }   /* (v306 : apres une pause : pas un jeu fige) */
#if defined(NATRES_LITE) && !defined(NATRES_COMPACT)
#define LOGALL_ON (H.opt & NRO_LOGALL)                   /* (v467 : journal detaille et complet : Pulse leger seulement) */
#else
#define LOGALL_ON 0
#endif
    if (++H.vi_in == 1 || LOGALL_ON) {
        nr_sample(ctx);
        if (LOGALL_ON) { nr_log_add(0xE4000002u, H.vi_in, 0, 0); EVC(0x01, ctx); EVF(); }   /* (v467 : une ecriture du journal) */
        nr_log_flush();                                  /* v464 : repere dans l'anneau (diagnostic) : bloque avec 1 = dans
                                                            l'ecriture de la carte (Pulse) ; avec 2 = dans la fonction d'image du jeu */
    }   /* (1re image : on sait si le jeu y arrive ; v462 : diagnostic,
                                                            a chaque image : un jeu bloque dans sa fonction d'image laisse vi_in = vi_count + 1) */
    PF_ADD(0, pt_);                                      /* (v513 : avant la fonction d'image du jeu) */
    if (vi_orig) {
        ((void (*)(u32, u32))vi_orig)(irq, ctx);          /* (v468 : plus d'interruptions ouvertes pendant celle-ci : la 1re interruption
                                                            arrivee avant que le jeu ait garde son etat l'ecrasait, Simpsons US fige des la 1re image) */
        EV(0x02, H.vi_in, 0);
    }
#if PF_ON
    pt_ = tbl();                                         /* (v513 : apres la fonction d'image du jeu) */
#endif
#if !defined(NATRES_LITE) && !defined(NATRES_LAN)
    /* v487 : chercheur de codes / succes / compagnon sans manettes du telephone : la liaison apres la fonction d'image du jeu.
       Avant elle, l'envoi des morceaux de memoire retardait le changement d'image du jeu : image dechiree (deux images
       decalees un instant), et grésillements (Simpsons, Sonic Adventure DX, build 1013) */
    if ((H.opt & (NRO_PHONE | NRO_MEM)) == NRO_MEM) { pd_poll(); pn_link_check(); }
#endif
    H.vi_count++; vi_snap(); alive_tick();
    igr_ctx = ctx;
    di_tick(ctx, 1);                                     /* v133 : fins de commande (sans le lecteur, ou secours) */
    freeze_check(ctx);                                   /* v169 */
    if ((H.vi_count & 31u) == 0) nr_sample(ctx);         /* v355 : apres la 1re image aussi (toutes les 32 images) : ou tourne
                                                            le jeu quand il reste bloque sans figer l'image (Mario Party 7) */
    if (H.chts) gk_run();                                /* v121 : cheats, une fois par image (apres le jeu) */
    if (H.flags & NRF_ACH) ach_run();                    /* v268 : succes */
    if (mfz_n) mfz_run();                                /* v307 : valeurs bloquees par le telephone */
    igr_poll();                                          /* v86 : (apres le jeu : il a lu les manettes) */
    PF_ADD(0, pt_);
}
static u32 ai_orig;
static void st_mix(void);
static void ai_hook(u32 irq, u32 ctx) {                  /* v65 : apres le jeu, la musique du disque */
    trap_guard();
    EVC(0x40, ctx);
    if (ai_orig) ((void (*)(u32, u32))ai_orig)(irq, ctx);
    PF_T0(pt_);
    EVE(0x48, 0, 0);
    H.cnt_ai++;
    st_mix();
    alive_tick();
    di_tick(ctx, 0);
    igr_ctx = ctx;
    igr_poll2(0);                                        /* v89 : aussi ici (~ toutes les 5 ms) : a l'image (VI), le jeu n'a
                                                            souvent pas encore pris les manettes (Wind Waker) : lecture interdite */
    PF_ADD(1, pt_);
}
static u32 dsp_orig = 0;
static void st_mix_from(int from_dsp);
static void dsp_hook(u32 irq, u32 ctx) {                 /* v98 : le jeu y programme parfois le tampon du son suivant */
    trap_guard();
#if PF_ON
    {   u32 t = tbl();                                   /* v513 : ecart avec l'interruption du DSP d'avant */
        if (pf_dsp_tb) { u32 g = (t - pf_dsp_tb) / 41u; if (g > pf_gap[0]) pf_gap[0] = g; if (g > 6000u) pf_gap[1]++; if (g > 12000u) pf_gap[2]++; }
        pf_dsp_tb = t; }
#endif
    EVC(0x41, ctx);
    if (dsp_orig) ((void (*)(u32, u32))dsp_orig)(irq, ctx);
    PF_T0(pt_);
    EVE(0x49, 0, 0);
    H.cnt_dsp++;
    st_mix_from(1);
    alive_tick();
    di_tick(ctx, 0);
    PF_ADD(2, pt_);
}
NR_HOOK(pet, H.cnt_pe)
/* (v244 : NR_HOOK(pef) retire : voir hooks_check) */

/* v60 : plantage du jeu (acces memoire interdit, code introuvable, alignement, instruction
   invalide) : on note ou, on ecrit le journal, et la console redemarre (retour a GC Station
   si c'est ipl.dol). Table des exceptions du SDK : 0x80003000 (2 DSI, 3 ISI, 5, 6).
   Le gestionnaire tourne sur sa propre pile (celle du jeu est peut-etre la cause). */
#define OS_EXC_TABLE ((volatile u32 *)0x80003000u)
u8 exc_stack[1600] __attribute__((aligned(32), used));   /* (builds 1061-1063 : 1600 au lieu de 2048, place ; une trame reseau tient en 1536 ;
                                                            pile du plantage : moins de 600 octets au plus, mesure -fstack-usage) */   /* (v371 : 32 : le direct l'envoie par DMA) */
void exc_c(u32 ex, u32 ctx) __attribute__((used));
extern void exc_entry(void);
/* v83 : le vecteur du SDK ne sauve que r3-r5, CR, LR, CTR, XER, SRR0/1 : r0-r2 et r6-r31 sont
   sauves par le gestionnaire (comme OSDefaultExceptionHandler), sinon le contexte garde les
   valeurs d'un ancien changement de tache (journal faux : r1, pile, appels). (CTR : deja sauve) */
__asm__(".pushsection .text\n.globl exc_entry\nexc_entry:\n"
        "  mtctr 0\n  oris 5,4,0x8000\n  rlwinm 0,5,7,25,31\n  cmplwi 0,0x40\n  mfctr 0\n  bne 1f\n"
        "  stw 0,0(5)\n  stw 1,4(5)\n  stw 2,8(5)\n  stmw 6,0x18(5)\n"
        "1:\n  lis 1,exc_stack@ha\n  addi 1,1,exc_stack@l\n  addi 1,1,1584\n  b exc_c\n.popsection\n");
/* v79 : details d'un plantage (format : natres.h, NATRES_CRASH_*), pour savoir POURQUOI le
   jeu s'est arrete : registres, chaine des appels (pile du jeu), textes passes a sa fonction
   d'erreur (un jeu qui s'arrete expres ecrit souvent a une adresse bidon comme 01234567,
   apres avoir prepare un message), et la memoire que le jeu a recue. */
static u8 crashbuf[512] __attribute__((aligned(32)));
static int ram_ok(u32 a, u32 n) { return a >= 0x80000000u && a < 0x81800000u && a + n <= 0x81800000u; }
static void cw(u32 o, u32 v) { crashbuf[o] = (u8)(v >> 24); crashbuf[o + 1] = (u8)(v >> 16); crashbuf[o + 2] = (u8)(v >> 8); crashbuf[o + 3] = (u8)v; }
#if !defined(NATRES_NOLOG)                               /* v509 : (Pulse sans journaux : aucun compte rendu) */
/* v82 : la pile du jeu au moment du plantage (3e secteur du journal) : un jeu qui s'arrete
   expres (OSPanic) y a range le nom du fichier, la ligne et le message de son erreur */
static int is_txt(u32 a) {
    if (!ram_ok(a, 8)) return 0;
    int n = 0;
    for (u32 i = 0; i < 24; i++) { u8 ch = *(volatile u8 *)(a + i); if (!ch) break; if (ch < 0x20 || ch > 0x7E) return 0; n++; }
    return n >= 4;
}
static MEM_OS void stack_dump(u32 sp) {
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
static MEM_OS void mem_snap(volatile u32 *c) {
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
static MEM_OS void thr_dump(void) {
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
#endif
static u32 xl_n;
#if !defined(NATRES_COMPACT) && !defined(NATRES_NOLOG)
static void MEM_OS crash_dump(u32 ex, u32 ctx, u32 dar, u32 dsisr) {
    if (!(H.flags & NRF_CRASH) || !H.log_sec || !di_ready) return;
    if ((H.flags & NRF_EXILOG) && xl_n) nr_write_sd(H.log_sec + 4, crashbuf);   /* v260 : (avant tout le reste : crashbuf) */
    for (u32 i = 0; i < 512; i++) crashbuf[i] = 0;
    cw(0x00, NATRES_CRASH_MAGIC); cw(0x04, NATRES_CRASH_VER); cw(0x08, ex);
    cw(0x20, dar); cw(0x24, dsisr); cw(0x28, ctx);
    static const u32 low[8] = { 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0xF0, 0xF4 };
    for (int i = 0; i < 8; i++) cw(0xB0 + i * 4, *(volatile u32 *)(0x80000000u + low[i]));
    if (ctx < 0x80000000u || ctx >= NATRES_AT || (ctx & 3)) { nr_write_sd(H.log_sec + 1, crashbuf); return; }
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
    if (ex == 0xFE || ex == 0xFD) vc_diag();          /* (v402 : FD = avant la 1re image) */                           /* v171 : jeu fige : etat de la carte virtuelle a la place des textes */
    nr_write_sd(H.log_sec + 1, crashbuf);
    if (H.flags & NRF_STACK) stack_dump(c[1]);
    if (H.flags & NRF_THREADS) thr_dump();
    if ((H.flags & NRF_PADLOG) && pd_find()) {           /* manettes du telephone : la section au retour */
        for (u32 i = 0; i < 512; i++) crashbuf[i] = 0;
        const u32 *q = (const u32 *)pd;
        cw(0x00, NATRES_PADLOG_MAGIC);
        for (u32 i = 0; i < sizeof(natres_pad_t) / 4 && 4 + i * 4 < 512; i++) cw(4 + i * 4, q[i]);
        nr_write_sd(H.log_sec + 6, crashbuf);
    }
    if (ex != 0xFD) mem_snap(c);
}
#else
static void crash_dump(u32 ex, u32 ctx, u32 dar, u32 dsisr) { (void)ex; (void)ctx; (void)dar; (void)dsisr; }   /* (v474 : Pulse compact ; v509 : sans journaux) */
#endif
#if defined(NATRES_LITE) && !defined(NATRES_COMPACT)
/* v480 : GC Station branche l'entree de OSPanic (arret d'urgence du SDK) et de OSReport (messages du jeu pour ses
   developpeurs) sur un aiguillage qui appelle di_store(0x7F00 + n, pile de l'aiguillage). Messages : les 8 derniers,
   ecrits dans le 5e secteur ("GCSR") avant la 1re image (64 fois au plus) ; arret d'urgence : en plus, comme un
   plantage (registres, appels, fils du jeu, photo de la memoire), exception FC. Pile de l'aiguillage : r0 en 0x08,
   r2-r31 en 0x0C, LR (retour vers l'appelant) en 0x88 ; pile du jeu = pile + 0x100. */
static u32 rp_fmt[8], rp_lr[8], rp_n = 0;
static int rd_on;                                        /* (plus bas) */
static MEM_OS void rp_write(u32 why) {
    for (u32 i = 0; i < 512; i++) crashbuf[i] = 0;
    cw(0x00, NATRES_REP_MAGIC); cw(0x04, rp_n); cw(0x08, why); cw(0x0C, H.vi_count);
    for (u32 k = 0; k < 8; k++) {
        if (rp_n < 8 - k) continue;
        u32 j = (rp_n - 8 + k) & 7, a = rp_fmt[j], o = 0x10 + k * 0x3C;
        cw(o, rp_lr[j]); cw(o + 4, a);
        if (ram_ok(a, NATRES_REP_STR)) for (u32 i = 0; i < NATRES_REP_STR; i++) crashbuf[o + 8 + i] = *(volatile u8 *)(a + i);
    }
    nr_write_sd(H.log_sec + 4, crashbuf);
}
static MEM_OS void os_hook(u32 n, u32 fr) {
    volatile u32 *f = (volatile u32 *)fr;
    u32 lr = f[0x88 / 4];
    if (n == 0) {                                        /* OSReport(fmt, ...) */
        rp_fmt[rp_n & 7] = f[0x10 / 4]; rp_lr[rp_n & 7] = lr; rp_n++;
        if (!H.vi_count && rp_n <= 64 && di_ready && !rd_on && !xv_st && !exi_busy()) { u32 m = irq_off(); rp_write(0); irq_restore(m); }   /* (pas une lecture du jeu au milieu) */
        return;
    }
    static u8 rst_done;                                  /* v491 : n >= 2 : le jeu ecrit dans le registre de redemarrage (0xCC003024) */
    if (n >= 2) { if (rst_done) return; rst_done = 1; }  /* (une fois : la boucle de redemarrage y repasse sans fin) */
    u32 ctx[0x1A0 / 4];                                  /* OSPanic(fichier, ligne, fmt, ...) : un OSContext pour crash_dump */
    for (u32 i = 0; i < 0x1A0 / 4; i++) ctx[i] = 0;
    ctx[0] = f[0x08 / 4]; ctx[1] = fr + 0x100u;
    for (u32 i = 2; i < 32; i++) ctx[i] = f[(0x0C + (i - 2) * 4) / 4];
    ctx[0x84 / 4] = lr; ctx[0x198 / 4] = lr;
    u32 ex = n >= 2 ? 0xFBu : 0xFCu;
    if (n >= 2) ctx[0x19C / 4] = n - 2;                  /* (v491 : quel endroit du jeu, dans SRR1) */
    H.exc_num = ex; H.exc_srr0 = lr; H.exc_lr = lr;
    if (!di_ready) di_ready = nr_init() == 0;
    if (!di_ready) return;
    u32 m = irq_off();
    if (n == 1) { rp_fmt[rp_n & 7] = ctx[5]; rp_lr[rp_n & 7] = lr; rp_n++; }   /* (le message de l'arret, en dernier) */
    rp_write(1);
    crash_dump(ex, (u32)ctx, n >= 2 ? di[5] : 0, n >= 2 ? di[6] : 0);   /* (v491 : derniere lecture : DIMAR, DILENGTH) */
    nr_log_flush();
    irq_restore(m);
}
#endif
MEM_OS void exc_c(u32 ex, u32 ctx) {   /* (v462 : -Os) */
    trap_guard();
    u32 dar, dsisr;
    __asm__ volatile("mfdar %0" : "=r"(dar));
    __asm__ volatile("mfdsisr %0" : "=r"(dsisr));
    if (ctx < 0x01800000u) ctx |= 0x80000000u;           /* (adresse physique -> virtuelle) */
    H.exc_num = 0x100u | (ex & 0xFF);
    H.exc_dar = dar; H.exc_dsisr = dsisr;
    if (ctx >= 0x80000000u && ctx < NATRES_AT) { H.exc_srr0 = *(volatile u32 *)(ctx + 0x198); H.exc_lr = *(volatile u32 *)(ctx + 0x84); }
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
static MEM_OS void si_hook(u32 irq, u32 ctx) {
    trap_guard();
    H.si_in++; fd_ctx = ctx;
    {   /* (v338 : diagnostic de la GBA du telephone : l'interruption arrive-t-elle, et le vrai COMCSR a ce moment) */
        natres_pad_t *p = (H.opt & NRO_PHONE) ? pd_find() : 0;
        if (p && p->gbmask && gb_port(p, p->xch)) {
            p->gdirq = *HW6(0xCC006434u); p->gdnirq++;
            /* v341 : fin du transfert vers la GBA (aiguille vers une vraie manette) : sa reponse ecrite dans le vrai tampon,
               avant le jeu. La bibliotheque SI lit les octets en plus d'un mot entier a une adresse calculee, sans
               aiguillage (Rayman 3 : l'etat apres la cle de session, 5e octet de la reponse a 14, venait de la manette) */
            if (p->gdirq & 0x80000000u) { *HW6(0xCC006480u) = gb_r0; *HW6(0xCC006484u) = gb_r1; }
        }
    }
    if (!H.vi_count) nr_sample(ctx);
    EVC(0x43, ctx);
    if (si_orig) ((void (*)(u32, u32))si_orig)(irq, ctx);
    EVE(0x4B, 0, 0);
    H.si_count++;
    alive_tick();
    di_tick(ctx, 0);
    igr_ctx = ctx;
    igr_poll();
}
static void igr_poll(void) { igr_poll2(1); }
/* count = 0 : ne compte que le temps (interruption du son : trop frequente pour compter les lectures) */
static MEM_OS void igr_go(void);
static MEM_OS void igr_poll2(int count) {
    if (igr_tel) igr_go();
    if (!(H.flags & NRF_IGR)) return;
    volatile u32 *si = HW6(0xCC006400u);
    static u8 igr_ch[4];                                 /* v99 : derniere chose vue sur chaque port (1 = A+Z+START tenus) */
    int on = 0, seen = 0;
    u32 sr = si[14];                                     /* SISR (lecture sans effet) */
    /* v457 : le jeu lit ses manettes par nos aiguillages (si_load) : on regarde ce qu'il a lu. Lire nous-memes INBUF
       effacait parfois des donnees neuves arrivees entre SISR et INBUF : le jeu recevait une manette vide pour cette
       image (touches perdues, stick qui saute). Sans lecture du jeu depuis 12 images (jeu fige) : comme avant. */
    int game = H.vi_count - igr_gt < 12u;          /* (igr_gt part loin : rien lu par le jeu = comme avant) */
    for (int ch = 0; ch < 4; ch++) {
        /* v58 : lire SICnINBUFH efface le drapeau "donnees neuves" (RDST) : si le jeu ne les a
           pas encore prises, il croirait la manette debranchee (1080 : fige sur la question 60 Hz).
           On ne lit donc que si le jeu les a deja prises (RDST = 0). */
        u32 b;
        if (game) b = igr_gh[ch];                        /* v457 : ce que le jeu a lu lui-meme : plus aucune lecture du materiel */
        else {
            if (sr & (0x20000000u >> (ch * 8))) { H.si_skip++; continue; }
            b = si[ch * 3 + 1];                          /* SICnINBUFH : boutons (deja lus par le jeu) */
            (void)si[ch * 3 + 2];                        /* v59 : puis INBUFL, comme le jeu (H puis L : rien de bloque) */
        }
        H.si_reads++;
        if (ch == 0 && (b >> 16) != H.si_last) { H.si_last = b >> 16; H.si_changes = (H.si_changes + 1) | ((b >> 16) & 0x1F7Fu) << 16; }   /* v96 : bits hauts = toutes les touches vues (diagnostic A+Z+START) */
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
    if (now - igr_t0 > TB_SEC || igr_hold > 60) igr_go();   /* (tenus 1 s, ou 60 lectures des manettes de suite) */
}
/* retour au menu de GC Station (A+Z+START, ou v418 : bouton de l'appli privee) */
static MEM_OS void igr_go(void) {
    H.igr = 1;
    /* v133 : ou en etait le jeu (comme un plantage : code, appels, pile, memoire) : un jeu fige
       (ecran noir, il tourne encore) montre ainsi ce qu'il attend */
    if (igr_ctx >= 0x80000000u && igr_ctx < NATRES_AT && !(igr_ctx & 3)) {
        H.exc_srr0 = *(volatile u32 *)(igr_ctx + 0x198); H.exc_lr = *(volatile u32 *)(igr_ctx + 0x84);
        crash_dump(0xFF, igr_ctx, 0, 0);
    }
    nr_log_flush();
    ret_entry();
}
/* ======================= v65 : musique du disque (audio en streaming, "DTK") =======================
   Certains jeux (1080, Zelda, F-Zero...) jouent leur musique directement depuis le disque :
   le lecteur envoie des echantillons ADPCM au processeur du son (AI). Ici, pas de vrai disque :
   Resident lit ces blocs ADPCM dans l'ISO, les decode (meme formule que la console / Dolphin)
   et les ajoute au son que le jeu envoie a la sortie audio (tampon DMA), au rythme de ses
   interruptions DMA. Commandes : E1 (jouer / arreter), E2 (etat, position). */
static u32 st_on = 0, st_stop_end = 0, st_pos = 0, st_cur_start = 0, st_cur_len = 0, st_next_start = 0, st_next_len = 0;
/* build 1060 : morceaux de 4 Ko quand la musique du disque joue (64 secteurs sinon). La musique lit la carte dans l'interruption
   du son : elle attendait la fin du morceau en cours, jusqu'a 32 Ko (16 ms, interruptions coupees) : petits trous dans le son
   (journal de Super Monkey Ball : 16,6 ms au pire dans l'interruption du son). Les morceaux s'enchainent sans commande (v527) */
/* build 1061 : pareil pendant les acces du jeu a la carte memoire virtuelle (et 5 s apres) : elle lit et ecrit la carte SD dans le
   crochet du lecteur et attendait aussi la fin du morceau en cours (Kirby Air Ride : 17 ms au pire, grezillements des menus) */
static u32 vc_vi = 0x80000000u;
static __attribute__((noinline)) u32 xv_max(void) { return st_on || (u32)(H.vi_count - vc_vi) < 300u ? 8u : 64u; }
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
#if !defined(NATRES_COMPACT)
#define WQ_N ST_SECS                                      /* (build 1063 : ecritures de la carte memoire en attente, dans st_sec) */
static u32 wq_sd[WQ_N], wq_n = 0;
static void wq_drain(void);
#endif
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
#if !defined(NATRES_COMPACT)
        if (wq_n) wq_drain();                             /* (build 1063 : le tampon servait aux ecritures en attente) */
#endif
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
#if !defined(NATRES_COMPACT)
static void st_adv(void) {                               /* echantillon suivant du disque (48 kHz) */
    if (st_pcm_i >= 28 && (!st_on || st_block())) { st_cl = st_cr = 0; return; }
    st_cl = st_pcm[st_pcm_i * 2]; st_cr = st_pcm[st_pcm_i * 2 + 1]; st_pcm_i++;
}
#else
static void st_adv(void) { }   /* (v474 : Pulse compact) */
#endif
#if !defined(NATRES_COMPACT)
static MEM_OS void st_cmd(u32 c0, u32 off, u32 len) {           /* E1 */
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
#else
static void st_cmd(u32 c0, u32 off, u32 len) { (void)c0; (void)off; (void)len; }   /* (v474 : Pulse compact) */
#endif
#if !defined(NATRES_COMPACT)
static MEM_OS u32 st_status(u32 c0) {                           /* E2 */
    switch ((c0 >> 16) & 0xFF) {
    case 0: return st_on ? 1 : 0;
    case 1: return (st_pos & ~0x7FFFu) >> 2;
    case 2: return st_cur_start >> 2;
    case 3: return st_cur_len;
    }
    return 0;
}
#else
static u32 st_status(u32 c0) { (void)c0; return 0; }   /* (v474 : Pulse compact) */
#endif
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
static MEM_OS void st_save(u32 addr) {
    stsnap_t *s = &st_snap;
    s->on = st_on; s->stop_end = st_stop_end; s->pos = st_pos; s->cs = st_cur_start; s->clen = st_cur_len; s->ns = st_next_start; s->nl = st_next_len;
    s->pcm_i = st_pcm_i; s->phase = st_phase; s->hl1 = st_hl1; s->hl2 = st_hl2; s->hr1 = st_hr1; s->hr2 = st_hr2; s->cl = st_cl; s->cr = st_cr; s->pl = st_pl; s->pr = st_pr;
    for (int i = 0; i < 56; i++) s->pcm[i] = st_pcm[i];
    st_snap_addr = addr;
}
static MEM_OS void st_load(void) {
    const stsnap_t *s = &st_snap;
    st_on = s->on; st_stop_end = s->stop_end; st_pos = s->pos; st_cur_start = s->cs; st_cur_len = s->clen; st_next_start = s->ns; st_next_len = s->nl;
    st_pcm_i = s->pcm_i; st_phase = s->phase; st_hl1 = s->hl1; st_hl2 = s->hl2; st_hr1 = s->hr1; st_hr2 = s->hr2; st_cl = s->cl; st_cr = s->cr; st_pl = s->pl; st_pr = s->pr;
    for (int i = 0; i < 56; i++) st_pcm[i] = s->pcm[i];
}
static u32 st_sum(const u32 *w, u32 n) { u32 s = 0x9E3779B9u; for (u32 i = 0; i < n; i++) s = (s ^ w[i]) * 0x01000193u; return s; }
#if !defined(NATRES_COMPACT)
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
#else
static void st_mix_from(int from_dsp) { (void)from_dsp; }   /* (v474 : Pulse compact) */
#endif
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
    u32 dirty, lvi, eblk, emask;                         /* v512 : secteur modifie pas encore ecrit ; image de la derniere ecriture ;
                                                            bloc efface (1er secteur) et ses secteurs pas encore ecrits (FF) */
    u8 prog[128];
    const natres_ext_t *tab;
    u32 n, size, id, on;
} vcard_t;
#define MIC_ID 0x0A000000u                               /* v332 : identifiant EXI du micro GameCube */
static natres_pad_t *mic_p(void);
static vcard_t vc[2] = { { .status = 0xC1u, .sec_no = 0xFFFFFFFFu }, { .status = 0xC1u, .sec_no = 0xFFFFFFFFu } };   /* (occupe | debloquee | prete) */
static u8 c_sec[2][512] __attribute__((aligned(32)));
static u32 x_disp = 0;
static MEM_OS void vc_setup(void) {                             /* (au 1er acces : tables donnees par GC Station) */
    vc[0].tab = card_tab; vc[0].n = H.card_n; vc[0].size = H.card_size; vc[0].id = H.card_id; vc[0].on = (H.flags & NRF_CARD) && H.card_n;
    vc[1].tab = card2_tab; vc[1].n = H.card2_n; vc[1].size = H.card2_size; vc[1].id = H.card2_id; vc[1].on = (H.flags & NRF_CARDB) && H.card2_n;
    if (mic_p()) { vc[1].on = 1; vc[1].n = 0; vc[1].id = MIC_ID; }   /* v332 : micro du telephone (a la place de la carte B) */
}
static MEM_OS int card_sd(vcard_t *v, u32 csec, u32 *sd) {
    for (u32 k = 0; k < v->n && k < NATRES_CARDMAX; k++) {
        const natres_ext_t *e = &v->tab[k];
        if (csec >= e->iso && csec - e->iso < e->cnt) { *sd = e->sd + (csec - e->iso); return 0; }
    }
    return -1;
}
static u8 *card_buf(vcard_t *v) { return c_sec[v == &vc[1]]; }
/* v512 : ecritures differees. Avant : chaque ecriture du jeu (page de 128 octets, effacement de 8 Ko) reecrivait tout de suite
   des secteurs entiers de la carte SD, interruptions coupees : 4 ecritures par secteur pour les pages, 16 par effacement, puis
   16 de plus pour les donnees ; pendant ce temps, plus de lecture du jeu (Kirby Air Ride : 1840 secteurs ecrits en 44 s
   pendant l'intro, son hache dans la video et les menus, build 1039 ; propre avec une vraie carte). Maintenant : le secteur
   modifie reste en memoire et n'est ecrit que quand le jeu passe a un autre secteur, ou a une image sans lecture en cours ;
   un bloc efface n'est pas ecrit en FF tout de suite (ses secteurs le sont s'il ne les reecrit pas). Tout est ecrit avant
   le retour a GC Station. */
/* build 1063 : jusqu'a 4 secteurs a ecrire mis en attente (dans le tampon de la musique du disque, st_sec, libre quand elle ne
   joue pas) et ecrits a une image ou la carte SD est libre (vc_idle) : plus d'ecriture ni d'attente de la carte SD quand le jeu passe
   d'un secteur de sa carte memoire a l'autre (Kirby Air Ride : 9 ms au pire, grezillements des menus). Un secteur en attente relu
   par le jeu vient d'ici. Tout est ecrit avant le retour a GC Station (vc_flush_all) et avant que la musique reprenne le tampon. */
#if !defined(NATRES_COMPACT)
static MEM_OS __attribute__((noinline)) void cp512(u8 *d, const u8 *s) { u32 *a = (u32 *)d; const u32 *b = (const u32 *)s; for (u32 i = 0; i < 128; i++) a[i] = b[i]; }
static MEM_OS __attribute__((noinline)) void wq_write1(void) {   /* le plus ancien secteur en attente, ecrit (sans attendre la fin) */
    if (!nr_write_sd(wq_sd[0], st_sec)) H.card_writes++;
    wq_n--;
    for (u32 i = 0; i < wq_n; i++) { wq_sd[i] = wq_sd[i + 1]; cp512(st_sec + i * 512u, st_sec + i * 512u + 512u); }
}
static MEM_OS void wq_drain(void) { while (wq_n) wq_write1(); }
static MEM_OS __attribute__((noinline)) u8 *wq_find(u32 sd) { for (u32 i = 0; i < wq_n; i++) if (wq_sd[i] == sd) return st_sec + i * 512u; return 0; }
#endif
static MEM_OS __attribute__((noinline)) int card_flush(vcard_t *v) {
    u32 sd;
    if (!v->dirty) return 0;
    v->dirty = 0;
    if (card_sd(v, v->sec_no, &sd)) { v->sec_no = 0xFFFFFFFFu; return -1; }
#if !defined(NATRES_COMPACT)
    if (!st_on) {                                        /* (build 1063 : en attente, ecrit plus tard) */
        u8 *q = wq_find(sd);
        if (!q) { if (wq_n >= WQ_N) wq_write1(); q = st_sec + wq_n * 512u; wq_sd[wq_n++] = sd; st_sec_off = 0xFFFFFFFFu; }
        cp512(q, card_buf(v));
        return 0;
    }
#endif
#if !defined(NATRES_COMPACT)
    if (wq_n) wq_drain();                                /* (musique du disque en cours : les plus anciennes d'abord, jamais par-dessus) */
#endif
    if (nr_write_sd(sd, card_buf(v))) { v->sec_no = 0xFFFFFFFFu; return -1; }
    H.card_writes++;
    return 0;
}
static MEM_OS int card_load(vcard_t *v, u32 csec) {   /* (v464 : -Os) */             /* secteur de la carte dans c_sec */
    if (csec == v->sec_no) return 0;
    card_flush(v);
    u32 sd, e = csec - v->eblk;
    v->sec_no = 0xFFFFFFFFu;
    if (e < 16u && (v->emask >> e & 1u)) {               /* (efface, pas encore ecrit : FF, a ecrire) */
        u8 *b = card_buf(v);
        for (u32 i = 0; i < 512; i++) b[i] = 0xFF;
        v->emask &= ~(1u << e); v->dirty = 1;
    } else {
        if (card_sd(v, csec, &sd)) return -1;
#if !defined(NATRES_COMPACT)
        const u8 *q = wq_find(sd);                       /* (build 1063 : encore en attente d'ecriture : la copie d'ici) */
        if (q) cp512(card_buf(v), q); else
#endif
        if (nr_read_sd(sd, card_buf(v))) return -1;
    }
    v->sec_no = csec;
    return 0;
}
static MEM_OS __attribute__((noinline)) void card_e1(vcard_t *v) {   /* un secteur efface pas encore ecrit : en FF sur la carte SD */
    if (card_load(v, v->eblk + 31u - (u32)__builtin_clz(v->emask))) v->emask = 0;
    card_flush(v);
}
static MEM_OS __attribute__((noinline)) void card_erase(vcard_t *v, u32 s) {   /* effacement du bloc de 8 Ko qui commence au secteur s */
    while (v->emask) card_e1(v);
    v->eblk = s; v->emask = 0xFFFFu; v->lvi = H.vi_count;
    if (v->sec_no - s < 16u) { v->sec_no = 0xFFFFFFFFu; v->dirty = 0; }   /* (le secteur en memoire est efface : relu en FF) */
}
#if !defined(NATRES_COMPACT)
static MEM_OS void vc_flush_all(void) { for (int c = 0; c < 2; c++) { while (vc[c].emask) card_e1(&vc[c]); card_flush(&vc[c]); } wq_drain(); }
#else
static void vc_flush_all(void) { }
#endif
static u8 card_rd1(vcard_t *v, u32 a) { return card_load(v, a >> 9) ? 0xFF : card_buf(v)[a & 511]; }
static MEM_OS void card_rd(vcard_t *v, u8 *dst, u32 a, u32 len) {   /* (v410 : -Os) */
    while (len) {
        u32 n = 512 - (a & 511);
        if (n > len) n = len;
        if (card_load(v, a >> 9)) { for (u32 i = 0; i < n; i++) dst[i] = 0xFF; }
        else for (u32 i = 0; i < n; i++) dst[i] = card_buf(v)[(a & 511) + i];
        dst += n; a += n; len -= n;
    }
}
static MEM_OS void card_wr(vcard_t *v, u32 a, const u8 *src, int fill, u32 len) {   /* src = NULL : remplir avec fill */
    while (len) {
        u32 n = 512 - (a & 511), s = a >> 9, e = s - v->eblk;
        if (n > len) n = len;
        if (n == 512 && s != v->sec_no) {                /* (secteur entier : pas besoin de le relire) */
            card_flush(v); v->sec_no = s;
            if (e < 16u) v->emask &= ~(1u << e);
        }
        if (!card_load(v, s)) {
            u8 *b = card_buf(v);
            for (u32 i = 0; i < n; i++) b[(a & 511) + i] = src ? src[i] : (u8)fill;
            v->dirty = 1; v->lvi = H.vi_count;            /* v512 : ecrit plus tard */
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
#if !defined(NATRES_LAN) && !defined(NATRES_LITE)
/* ======================= v332 : micro du telephone (slot B) =======================
   Le micro GameCube (DOL-022 : Mario Party 6 et 7, Karaoke Revolution...) se branche dans le slot B (canal EXI 1,
   peripherique 0). Son fonctionnement a ete lu dans le pilote de Mario Party 7 (commandes et bits seulement, aucun code
   repris) : identifiant 0x0A000000 ; FF remise a zero ; 40 : lire l'etat (2 octets) ; 80 : ecrire l'etat (2 octets ; bit 15
   en marche, bits 13-14 paquets de 32 / 64 / 128 octets, bits 11-12 11025 / 22050 / 44100 Hz, bit 10 gain) ; quand un
   paquet est pret, interruption EXI : le jeu lit l'etat, puis 20 et le paquet par DMA (echantillons 16 bits gros-boutistes).
   Ici, le slot B emule (comme la carte virtuelle) joue ce micro avec le son du telephone : l'appli lit l'etat (renvoye toutes
   les 10 images) et envoie des echantillons 16 bits deja a la bonne frequence ; interruption des qu'un paquet entier est la. */
static MEM_OS u32 mic_flags(natres_pad_t *p) { return (p->micon ? 1u : 0u) | (vc[1].id == MIC_ID ? 2u : 0u) | (vc[1].on ? 4u : 0u); }
static MEM_OS __attribute__((noinline)) natres_pad_t *mic_p(void) { natres_pad_t *p = pd_find(); return p && p->micon ? p : 0; }
/* v353 : comme un vrai micro, un paquet a la cadence de la frequence choisie (32 octets a 11025 Hz : toutes les 1,45 ms),
   des que le jeu l'allume : le son du telephone s'il est la, sinon du silence. Avant : seulement quand le son du telephone
   etait arrive (~200 ms apres la mise en marche) ; Mario Party 7 eteignait le micro avant (« no Mic »). */
static u32 mic_tnext = 0;
static u32 mic_rng = 0x2545F491u;                       /* v387 : souffle du silence (suite pseudo-aleatoire) */
static u8 mic_lo = 0, mic_got = 0;                      /* (mic_got : le son du telephone est deja arrive depuis la mise en marche) */                                /* (horloge : quand le prochain paquet est du) */
static MEM_OS void mic_ready(natres_pad_t *p) {          /* un paquet est du : interruption EXI du slot B */
    vcard_t *v = &vc[1];
    u32 k = (p->mstat >> 13) & 3u, r = (p->mstat >> 11) & 3u;   /* (paquets de 32, 64, 128 octets ; 11025, 22050, 44100 Hz) */
    if (v->id != MIC_ID || !(p->mstat & 0x8000u) || (v->irq & 2u)) return;
    u32 pk = 32u << (k > 2u ? 2u : k), tpb = 40500000u / (22050u << (r > 2u ? 2u : r));   /* (tops d'horloge par octet) */
    u32 now = tbl();
    if ((int)(now - mic_tnext) < 0) return;              /* (pas encore) */
    if ((int)(now - mic_tnext) > 4050000) mic_tnext = now;   /* (plus de 100 ms de retard : on repart d'ici, sans rafale) */
    mic_tnext += pk * tpb;
    v->irq |= 2u; p->mnirq++; x_kick();
}
static MEM_OS u8 mic_byte(vcard_t *v, u8 b) {
    natres_pad_t *p = mic_p();
    u8 r = 0xFF;
    if (!p) return r;
    if (v->pos == 0) {
        v->cmd = b; p->mncmd++;
        if (b == 0x00u) p->mnid++;                       /* (v352 : diagnostic) */
        if (b == 0xFF) { p->mstat = 0; v->irq &= ~2u; }
    } else if (v->cmd == 0x40) r = (u8)(v->pos == 1 ? p->mstat >> 8 : p->mstat);
    else if (v->cmd == 0x80) {
        if (v->pos == 1) v->addr = (u32)b << 8;
        else if (v->pos == 2) {
            u32 st = (v->addr | b) & 0xFDFFu;            /* (bit 9 : debordement, jamais ici) */
            if (st != p->mstat) {                       /* (v387b : a la mise en marche, les ~46 ms les plus recentes du telephone, qui envoie
                                                    son son micro eteint (appli 477) ; une autre frequence en marche : pas de vieux son) */
                if ((st & 0x8000u) && !(p->mstat & 0x8000u)) { if (p->mw - p->mr > 1024u) p->mr = p->mw - 1024u; }
                else p->mr = p->mw;
                mic_tnext = tbl(); p->mfill = 0; mic_got = 0;
            }   /* (mise en marche, autre frequence : pas de vieux son ; v353 : 1er paquet tout de suite ;
                                                     v387 : le son du telephone des qu'il arrive, sans attendre la reserve : Mario Party 7
                                                     teste le micro ~0,35 s apres l'avoir allume et ne recevait que du silence) */
            if (!(st & 0x8000u)) v->irq &= ~2u;
            p->mstat = st;
        }
    } else if (v->cmd == 0x20) {                         /* (echantillons ; rien du telephone : silence) */
        /* v354 : reserve de ~90 ms (selon la frequence) avant de donner le son, et de nouveau apres un trou : la voix arrive
           entiere (la reconnaissance des mots de Mario Party 7 ratait sur un son hache par le Wi-Fi) ; plus de 370 ms
           d'avance : on rattrape (latence bornee) */
        u32 pre = 2048u << (((p->mstat >> 11) & 3u) > 2u ? 2u : ((p->mstat >> 11) & 3u));
        if (pre > NATRES_MIC_RING / 2u) pre = NATRES_MIC_RING / 2u;
        u32 dispo = p->mw - p->mr;
        if (p->mfill && dispo >= pre) p->mfill = 0;
        if (!p->mfill && dispo > pre * 4u && pre * 4u < NATRES_MIC_RING) { p->mr = (p->mw - pre * 2u) & ~1u; dispo = p->mw - p->mr; }
        if (!p->mfill && dispo) { r = MIC_RING(p)[p->mr++ & (NATRES_MIC_RING - 1u)]; mic_got = 1; }
        else {                                           /* v387 : un souffle faible (+-255 sur 32767) plutot que des zeros parfaits :  */
            if (!(p->mnrd & 1u)) {                       /* un vrai micro n'en donne jamais, et le test du micro de Mario Party 7 le */
                mic_rng = mic_rng * 1664525u + 1013904223u;   /* jugeait mort (micro « absent ») */
                int n = (int)((mic_rng >> 24) + ((mic_rng >> 16) & 0xFFu)) - 255;   /* (v387b : +-255, en triangle : le moteur de voix
                                                                                       de Mario Party 7 a aussi « TOO QUIET », « NO SIGNAL ») */
                r = (u8)(n < 0 ? 0xFF : 0x00); mic_lo = (u8)n;
            } else r = mic_lo;
            p->mlow++; if (mic_got && !p->mfill && !(p->mnrd & 1u)) { p->mfill = 1; p->mnrefill++; }
        }
        p->mnrd++;
    }
    else if (v->cmd == 0x00 && v->pos >= 2) r = (u8)(MIC_ID >> (24 - ((v->pos - 2) & 3) * 8));
    v->pos++;
    return r;
}
#else
static natres_pad_t *mic_p(void) { return 0; }
static u8 mic_byte(vcard_t *v, u8 b) { (void)v; (void)b; return 0xFF; }
#endif
static MEM_OS void card_cs(vcard_t *v, int on) {
    if (on) { v->pos = 0; if (v->id == MIC_ID) { natres_pad_t *p = pd_find(); if (p) p->mnsel++; } return; }   /* (v352 : diagnostic) */
    if (v->id == MIC_ID) { natres_pad_t *p = mic_p(); if (p && v->cmd == 0x20) mic_ready(p); return; }   /* v332 : paquet lu : le suivant ? */
    u32 mask = v->size - 1;
    if (v->cmd == 0xF1 && v->pos > 2) { card_erase(v, (v->addr & mask & ~0x1FFFu) >> 9); card_done(v); }   /* v512 */
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
static MEM_OS u8 card_byte(vcard_t *v, u8 b) {               /* (v410 : -Os) */
    if (v->id == MIC_ID) return mic_byte(v, b);          /* v332 */
    if (v->pos == 0) {
        v->cmd = b; b = 0xFF;
        H.card_cmds++; H.card_last = v->cmd; vc_vi = H.vi_count;
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
static MEM_OS void x_xfer(vcard_t *v) {                         /* transfert vers la carte virtuelle */
    u32 n = ((v->cr >> 4) & 3) + 1, rw = (v->cr >> 2) & 3;
    if (!(v->cr & 2)) {                                  /* immediat (1 a 4 octets dans DATA) */
        if (rw == 1) { for (u32 i = 0; i < n; i++) card_byte(v, (u8)(v->data >> (24 - i * 8))); }
        else if (rw == 0) { u32 x = 0; for (u32 i = 0; i < n; i++) x |= (u32)card_byte(v, 0) << (24 - i * 8); v->data = x; }
    } else {                                             /* DMA */
        u32 dst = (v->mar & 0x01FFFFE0u) | 0x80000000u, len = v->len & 0x03FFFFE0u;
        if (len && dst + len <= 0x81800000u) {
            if (v->id == MIC_ID) {                       /* v332 : micro : le paquet d'echantillons */
                if (rw == 0) { for (u32 i = 0; i < len; i++) ((u8 *)dst)[i] = mic_byte(v, 0); cache_flush(dst, len); }
            }
            else if (rw == 0) { card_rd(v, (u8 *)dst, v->addr & (v->size - 1), len); cache_flush(dst, len); }
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
static MEM_OS u32 vc_want(int c) {                              /* interruptions de la carte c que le jeu accepte maintenant */
    vcard_t *v = &vc[c];
    u32 w = (v->csr & 1 ? 2u : 0) | (v->csr & 4 ? 8u : 0), m = VC_OSMASK;
    if (m & (0x80000000u >> (c ? 12 : 9))) w &= ~2u;
    if (m & (0x80000000u >> (c ? 13 : 10))) w &= ~8u;
    return w;
}
static MEM_OS int vc_pending(void) {                            /* une interruption de carte attend d'etre donnee au jeu */
    int p = 0;
    for (int c = 0; c < 2; c++) if (vc[c].on && (vc[c].irq & vc_want(c))) p = 1;
    return p;
}
/* a = adresse visee par le jeu (calculee pendant le jeu) : canal 0 (slot A), canal 1 (slot B) si emules */
static MEM_OS vcard_t *vc_of(u32 a) {
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
#if defined(NATRES_NOLOG)
static inline void xl_start(vcard_t *v, u32 cr) { (void)v; (void)cr; }
static inline void xl_end(void) {}
#else
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
static MEM_OS void xl_end(void) {                               /* (le jeu deselectionne : les donnees du DMA sont la) */
    u32 o = xl_cur, c;
    if (o >= 0x200u) return;
    xl_cur = 0xFFFFFFFFu;
    c = xl_get(o);
    if (!(c & 2) || ((c >> 2) & 3)) return;              /* (seulement un DMA en lecture) */
    u32 m = xl_get(o + 8);
    for (u32 k = 0; k < 4; k++) cw(o + 16 + k * 4, xl_rd(m + (k & 1) * 4 + (k & 2) * 8));
}
#endif
#ifdef NATRES_LAN
/* ======================= Pulse LAN : le pont (jeu en reseau local par Internet) =======================
   Le jeu garde le vrai Broadband Adapter (canal 0, peripherique 2) ; Pulse se glisse entre les deux :
   - envoi : ce que le jeu ecrit dans la file d'envoi (WRTXFIFOD) ne va pas a l'adaptateur (la commande est changee en
     une lecture sans effet : le transfert reste vrai, sa fin aussi) mais dans ln_tx ; quand le jeu demande l'envoi
     (NCRA, ST1), Pulse l'emballe (Ethernet + IPv4 + UDP + "GCLR1" + partie, comme GC Station PC et tools/lan-relais.py)
     et l'envoie lui-meme vers l'ami, a la fin de la transaction du jeu ; la vraie fin d'envoi previent le jeu ;
   - reception : avant chaque transaction du jeu avec l'adaptateur, les trames arrivees (anneau de reception) qui
     viennent de l'ami sont deballees sur place (la trame du jeu remise au debut, longueur corrigee) : le jeu ne voit
     que les trames de l'autre console. Pulse repond aussi aux demandes ARP pour l'adresse de la console, et trouve
     l'adresse Ethernet de la box (passerelle). Signe de vie toutes les 5 s sans envoi (la box garde le passage ouvert).
   Les registres de l'adaptateur (anneau, file, interruptions) restent les vrais : leurs interruptions aussi. */
typedef struct {
    u32 magic;                                            /* "PLAN" : GC Station remplit (natif.c) */
    u32 ip, mask, gw;                                     /* la console sur son reseau */
    u32 peer, port;                                       /* l'ami (IPv4 publique) et son port UDP (51240) */
    u32 room;                                             /* partie : 4 premiers octets du SHA-256 du code (en majuscules) */
    u32 tx, rx, drop, arp, ka;                            /* (comptes, pour le journal) */
} natres_lan_t;
natres_lan_t lan_cfg __attribute__((used)) = { 0x504C414Eu };
#define LN_PORT 51240u
static u8 ln_tx[1600] __attribute__((aligned(32)));       /* trame du jeu en attente (apres les 51 octets d'en-tete) */
static u32 ln_txn = 0, ln_send = 0, ln_ncra = 0;
static u32 ln_sel = 0, ln_ph = 0, ln_cmd = 0, ln_data = 0, ln_mxa = 0, ln_fifo = 0, ln_fake = 0;
static u8 ln_gw[6]; static u32 ln_gwok = 0, ln_arp_tb = 0, ln_last_tb = 0, ln_rp = 0, ln_init = 0;
static int ln_on(void) { return lan_cfg.magic == 0x504C414Eu && lan_cfg.peer && lan_cfg.ip; }
/* l'en-tete (51 octets) devant n octets de trame, dans f */
static MEM_OS void ln_hdr(u8 *f, u32 n) {
    static u32 ipid = 0;
    pn_eth(f, ln_gw, 0x0800);
    u8 *ip = f + 14, *u = ip + 20;
    ip[0] = 0x45; ip[1] = 0; pn_p16(ip + 2, 20 + 8 + 9 + n); pn_p16(ip + 4, ++ipid & 0xFFFF); pn_p16(ip + 6, 0x4000); ip[8] = 64; ip[9] = 17;
    pn_p16(ip + 10, 0); pn_p32(ip + 12, lan_cfg.ip); pn_p32(ip + 16, lan_cfg.peer); pn_p16(ip + 10, pn_ipsum(ip));
    pn_p16(u, LN_PORT); pn_p16(u + 2, lan_cfg.port); pn_p16(u + 4, 8 + 9 + n); pn_p16(u + 6, 0);
    u[8] = 'G'; u[9] = 'C'; u[10] = 'L'; u[11] = 'R'; u[12] = '1'; pn_p32(u + 13, lan_cfg.room);
}
/* un envoi a nous (signe de vie, ARP) : seulement hors d'une trame du jeu ; sa fin ne doit pas tromper le jeu */
static MEM_OS void ln_own(u8 *f, u32 n) {
    if (ln_txn || ln_send || (bb_in8(BB_NCRA) & 6u)) return;
    u32 imr = bb_in8(BB_IMR);
    bb_out8(BB_IMR, imr & ~4u);                           /* (pas d'interruption "envoye" pour le jeu) */
    pn_send(f, n);
    bb_out8(BB_IR, 4u);
    bb_out8(BB_IMR, imr);
}
static MEM_OS void ln_keepalive(void) { u8 *f = pn_fr; ln_hdr(f, 0); ln_own(f, 51); lan_cfg.ka++; }
static MEM_OS void ln_arpreq(void) {
    u32 hop = (lan_cfg.peer ^ lan_cfg.ip) & lan_cfg.mask ? lan_cfg.gw : lan_cfg.peer;
    static const u8 all[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    u8 *f = pn_fr;
    pn_eth(f, all, 0x0806);
    pn_p16(f + 14, 1); pn_p16(f + 16, 0x0800); f[18] = 6; f[19] = 4; pn_p16(f + 20, 1);
    for (int i = 0; i < 6; i++) { f[22 + i] = pn_mac[i]; f[32 + i] = 0; }
    pn_p32(f + 28, lan_cfg.ip); pn_p32(f + 38, hop);
    ln_own(f, 42);
}
/* anneau de reception : adresse dans la memoire de l'adaptateur de l'octet o du paquet de la page p (apres son descripteur) */
static u32 ln_bp, ln_he;
static u32 ln_at(u32 p, u32 o) { u32 a = (p << 8) + 4 + o; if (a >= ln_he) a -= ln_he - (ln_bp << 8); return a; }
static MEM_OS void ln_rd(u32 p, u32 o, u8 *d, u32 n) {
    u32 a = ln_at(p, o), k = ln_he - a < n ? ln_he - a : n;
    bb_ins(a, d, k);
    if (k < n) bb_ins(ln_bp << 8, d + k, n - k);
}
static MEM_OS void ln_wr(u32 p, u32 o, const u8 *s, u32 n) {
    u32 a = ln_at(p, o), k = ln_he - a < n ? ln_he - a : n;
    bb_outs(a, s, k);
    if (k < n) bb_outs(ln_bp << 8, s + k, n - k);
}
/* les trames arrivees depuis la derniere fois : deballees sur place (adaptateur libre : avant une transaction du jeu) */
static MEM_OS __attribute__((noinline)) void ln_rx(void) {
    u32 rwp = bb_in16(BB_RWP) & 0xFFF;
    if (rwp == ln_rp) return;
    u32 rrp = bb_in16(BB_RRP) & 0xFFF;
    ln_bp = bb_in16(BB_BP) & 0xFFF; ln_he = ((bb_in16(BB_RHBP) & 0xFFF) + 1) << 8;
    if (ln_bp < 1 || (ln_he >> 8) <= ln_bp || ln_he > 0x1000u) { ln_rp = rwp; return; }
    /* (notre curseur hors de [rrp, rwp] : l'adaptateur a ete remis a zero, ou le jeu a tout lu : on repart de rrp) */
    int in = rrp <= rwp ? (ln_rp >= rrp && ln_rp <= rwp) : (ln_rp >= rrp || ln_rp <= rwp);
    if (!in) ln_rp = rrp;
    for (int k = 0; k < 16 && ln_rp != rwp; k++) {
        u8 d[4], h[51];
        bb_ins(ln_rp << 8, d, 4);
        u32 w = (u32)d[0] | (u32)d[1] << 8 | (u32)d[2] << 16 | (u32)d[3] << 24;
        u32 next = w & 0xFFF, len = (w >> 12) & 0xFFF;
        if (len <= 4 || len > 1600u || next < ln_bp || next >= (ln_he >> 8)) { ln_rp = rwp; break; }
        u32 n = len - 4;
        ln_rd(ln_rp, 0, h, n < 51 ? n : 51);
        u32 type = pn_be16(h + 12);
        if (n >= 51 && type == 0x0800 && h[23] == 17 && pn_be32(h + 30) == lan_cfg.ip && pn_be16(h + 36) == LN_PORT &&
            h[42] == 'G' && h[43] == 'C' && h[44] == 'L' && h[45] == 'R' && h[46] == '1' && pn_be32(h + 47) == lan_cfg.room) {
            if (!ln_gwok) { for (int i = 0; i < 6; i++) ln_gw[i] = h[6 + i]; ln_gwok = 1; }
            u32 m = n - 51;
            if (m >= 14 && m <= 1514u) {                  /* la trame de l'ami, au debut du paquet */
                ln_rd(ln_rp, 51, pn_fr, m);
                ln_wr(ln_rp, 0, pn_fr, m);
                w = (w & 0xFF000FFFu) | (m + 4) << 12;
                d[0] = (u8)w; d[1] = (u8)(w >> 8); d[2] = (u8)(w >> 16); d[3] = (u8)(w >> 24);
                bb_outs(ln_rp << 8, d, 4);
                lan_cfg.rx++;
            } else {                                      /* (signe de vie : rien pour le jeu) */
                static const u8 z[2] = { 0x88, 0xB5 };      /* (type inconnu : le jeu l'ignore) */
                ln_wr(ln_rp, 12, z, 2);
            }
        } else if (n >= 42 && type == 0x0806) {           /* ARP */
            u8 a[28];
            ln_rd(ln_rp, 14, a, 28);
            u32 op = pn_be16(a + 6), sip = pn_be32(a + 14), tip = pn_be32(a + 24);
            u32 hop = (lan_cfg.peer ^ lan_cfg.ip) & lan_cfg.mask ? lan_cfg.gw : lan_cfg.peer;
            if (op == 2 && sip == hop && tip == lan_cfg.ip) { for (int i = 0; i < 6; i++) ln_gw[i] = a[8 + i]; ln_gwok = 1; }
            if (op == 1 && tip == lan_cfg.ip) {           /* (la box demande ou est la console : on repond) */
                u8 *f = pn_fr;
                pn_eth(f, a + 8, 0x0806);
                pn_p16(f + 14, 1); pn_p16(f + 16, 0x0800); f[18] = 6; f[19] = 4; pn_p16(f + 20, 2);
                for (int i = 0; i < 6; i++) { f[22 + i] = pn_mac[i]; f[32 + i] = a[8 + i]; }
                pn_p32(f + 28, lan_cfg.ip); pn_p32(f + 38, sip);
                ln_own(f, 42); lan_cfg.arp++;
            }
        }
        ln_rp = next;
    }
}
/* l'envoi demande par le jeu (a la fin de sa transaction) */
static MEM_OS void ln_flush(void) {
    u32 n = ln_txn;
    ln_send = 0; ln_txn = 0;
    if (!ln_gwok || n < 14 || n > 1514u - 51u) { lan_cfg.drop++; return; }
    u8 *f = ln_tx - 51 + 64;                              /* (ln_tx : en-tete dans les 64 premiers octets) */
    ln_hdr(f, n);
    u32 c = 51 + n;
    while (c < 60) f[c++] = 0;
    bb_out16(BB_TXFIFOCNT, c);
    bb_outs(BB_WRTXFIFOD, f, c);
    bb_out8(BB_NCRA, ln_ncra);                            /* (ST1, comme le jeu l'avait demande) */
    lan_cfg.tx++; ln_last_tb = tbl();
}
/* chaque ecriture du jeu dans les registres du canal 0 (avant le vrai materiel) : 1 = faite ici */
static MEM_OS __attribute__((noinline)) int lan_store(u32 a, u32 val) {
    if (!ln_on()) return 0;
    u32 o = a - 0xCC006800u;
    volatile u32 *r = HW6(0xCC006800u);
    if (o == 0) {
        u32 sel = (val & 0x380u) == 0x200u;
        if (sel && !ln_sel) {                             /* le jeu commence une transaction : l'adaptateur est libre */
            bx = r; pn_cs = 0x80u << 2; pn_frq = 5; pn_hw = 1;
            u32 m = irq_off();
            if (!ln_init) { ln_init = 1; bb_ins(BB_PAR, pn_mac, 6); }
            ln_rx();
            if (!ln_gwok && (!ln_arp_tb || tbl() - ln_arp_tb > TB_SEC / 2u)) { ln_arp_tb = tbl() | 1; ln_arpreq(); }
            irq_restore(m);
            ln_sel = 1; ln_ph = 0;
        } else if (!sel && ln_sel) {                      /* fin de la transaction du jeu */
            ln_sel = 0;
            if (ln_send) {
                r[0] = val;
                bx = r; u32 m = irq_off(); ln_flush(); irq_restore(m);
                return 1;
            }
        }
        return 0;
    }
    if (!ln_sel) return 0;
    if (o == 0x10) { ln_data = val; return 0; }
    if (o != 0x0C || !(val & 1)) return 0;
    u32 dma = val & 2, rw = (val >> 2) & 3, n = dma ? r[2] : ((val >> 4) & 3) + 1;
    if (ln_ph++ == 0) {                                   /* la commande */
        ln_cmd = dma ? 0 : ln_data; ln_fake = 0;
        ln_mxa = (ln_cmd >> 8) & 0xFFFFu;
        if ((ln_cmd & 0xC0000000u) == 0xC0000000u && ln_mxa == BB_WRTXFIFOD) {   /* file d'envoi : vers ln_tx */
            r[4] = 0x80000000u | (0x100u << 8);           /* (une lecture de la memoire de reception : sans effet) */
            ln_fifo = 1;
        } else if ((ln_cmd & 0xC0000000u) == 0xC0000000u && (ln_mxa == BB_TXFIFOCNT || ln_mxa == BB_TXFIFOCNT + 1)) {
            r[4] = 0x80000000u | (0x100u << 8);           /* (le compte, c'est Pulse qui l'ecrit a l'envoi) */
            ln_fifo = 0;
        } else ln_fifo = 0;
        if ((ln_cmd & 0xC0000000u) == 0x80000000u && (ln_mxa == BB_TXFIFOCNT || ln_mxa == BB_TXFIFOCNT + 1)) ln_fake = 1;
        return 0;
    }
    if (rw == 1 && ln_fifo) {                             /* donnees de la trame */
        if (ln_txn + n > sizeof(ln_tx) - 64) { ln_txn = sizeof(ln_tx); return 0; }
        u8 *d = ln_tx + 64 + ln_txn;
        if (dma) { const u8 *s = (const u8 *)((r[1] & 0x01FFFFFFu) | 0xC0000000u); for (u32 i = 0; i < n; i++) d[i] = s[i]; }
        else for (u32 i = 0; i < n; i++) d[i] = (u8)(ln_data >> (24 - 8 * i));
        ln_txn += n;
        return 0;
    }
    if (rw == 1 && !dma && (ln_cmd & 0xC0000000u) == 0xC0000000u && ln_mxa == BB_NCRA && (ln_data >> 24) & 4u && ln_txn) {
        ln_ncra = ln_data >> 24;                          /* envoi demande (ST1) : a la fin de la transaction */
        r[4] = (ln_data & ~0x04000000u);
        ln_send = 1;
    }
    return 0;
}
/* lecture du jeu : le nombre d'octets dans la file d'envoi (TXFIFOCNT) est celui de ln_tx */
static MEM_OS int lan_load(u32 a, u32 *v) {
    if (!ln_on() || !ln_sel || !ln_fake || a != 0xCC006810u) return 0;
    u32 c = ln_txn, b0 = ln_mxa == BB_TXFIFOCNT ? c & 0xFF : (c >> 8) & 0xFF;
    *v = b0 << 24 | ((c >> 8) & 0xFF) << 16;
    return 1;
}
/* a chaque image : signe de vie (box et GC Station PC : la partie reste ouverte) */
void pd_poll(void) __attribute__((used));
void MEM_OS pd_poll(void) {
    if (!ln_on() || ln_sel || (*HW6(0xCC006800u) & 0x380u)) return;   /* (le bus est pris) */
    if (!ln_init) return;                                 /* (le jeu ne s'est pas encore servi de l'adaptateur) */
    if (tbl() - ln_last_tb < 5u * TB_SEC) return;
    bx = HW6(0xCC006800u); pn_cs = 0x80u << 2; pn_frq = 5; pn_hw = 1;
    u32 m = irq_off();
    ln_last_tb = tbl();
    if (!ln_gwok) ln_arpreq(); else ln_keepalive();
    irq_restore(m);
}
#endif

#if !defined(NATRES_COMPACT)
/* v489 : le jeu touche au canal de la carte SD (carte virtuelle du slot, horloge, reglages) : Pulse finit d'abord sa lecture par
   interruptions et ferme la lecture en continu (la carte SD n'est plus choisie), puis le jeu passe (comme le verrou du canal
   que prend Swiss). Avant, une ecriture du jeu dans ce canal pendant une lecture de Pulse pouvait desactiver la carte SD. */
/* v519 : carte SD dans le port serie 2 (canal 2, rien qu'a Pulse) : le jeu y ecrit quand meme. Kirby Air Ride (journal complet,
   build 1046) : CSR du canal 2 = 0 puis 1 environ toutes les secondes (son pilote EXI qui guette un peripherique) ; pendant une
   lecture par interruptions de Pulse, ca effacait le masque de fin de transfert et la selection de la carte : fin jamais signalee
   (83 lectures bloquees en 3 min 30), et le secours de 10 ms figeait tout le temps de finir. Maintenant, les acces du jeu a ce
   canal ne vont plus au vrai materiel (lectures : laissees, sans effet). */
#if PF_ON
#define CH2_CNT() (ch2_w++)
#else
#define CH2_CNT() ((void)0)
#endif
static void sd_yield(u32 a) {
    if ((a - 0xCC006800u) / 0x14u != H.chan || hw_d || !(xv_st || st_open)) return;
    u32 m = irq_off();
    if (xv_st) xv_drain();
    st_close();
    irq_restore(m);
}
static MEM_OS u32 exi_load(u32 a) {
    trap_guard();
    EV(0x51, a, 0);
#ifdef NATRES_LAN
    { u32 lv; if (lan_load(a, &lv)) { (void)*HW6(a); return lv; } }
#endif
    if (a - 0xCC006000u < 0x40u) return di_load(a);      /* v133 : acces au lecteur branches en secours (adresse calculee) */
    early_note(a, 0, 0);
    /* manettes du telephone, chercheur de codes de l'appli (v312) : le Broadband Adapter est pour Pulse. Le jeu qui le
       cherche (Mario Kart Double Dash, jeu en reseau local) lit "aucun peripherique" : sinon il s'en servait sans arret et
       Pulse ne pouvait jamais le reprendre (avant v312, avec le seul chercheur de codes : appli sans reponse dans ce jeu) */
    if ((H.opt & (NRO_PHONE | NRO_MEM)) && a == 0xCC006810u && (*HW6(0xCC006800u) & 0x380u) == 0x200u) { H.bba_n++; return 0; }
    vcard_t *v = vc_of(a);
    if (!v) return *HW6(a);                              /* (autres canaux, cartes reelles : vrai materiel) */
    sd_yield(a);                                         /* v489 */
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
    case 4: x = (v->csr & 0x80) ? v->data : real[4];
#if !defined(NATRES_NOLOG)
        if (!(v->csr & 0x80) && xl_cur < 0x200u && !(xl_get(xl_cur) & 2)) cw(xl_cur + 16, x);
#endif
        break;
    default: x = 0; break;
    }
    irq_restore(m);
    return x;
}
#else
static u32 exi_load(u32 a) { (void)a; return 0; }   /* (v474 : Pulse compact) */
#endif
#if !defined(NATRES_COMPACT)
static MEM_OS void exi_store(u32 a, u32 val) {
    trap_guard();
    EV(0x50, a, val);
#ifdef NATRES_LAN
    if (a - 0xCC006800u < 0x14u && lan_store(a, val)) return;
#endif
    if (a - 0xCC006000u < 0x40u) { di_store(a, val); return; }   /* v133 */
    early_note(a, val, 1);
    if (a == 0xCC006800u && (val & 0x200u)) H.bba_n++;   /* v145 : le jeu parle au Broadband Adapter (jeu en reseau) */
    vcard_t *v = vc_of(a);
    if (!v) { if (H.chan == 2u && a - 0xCC006828u < 0x14u) { CH2_CNT(); return; } *HW6(a) = val; return; }   /* (v519 : canal 2 : rien) */
    sd_yield(a);                                         /* v489 */
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
#else
static void exi_store(u32 a, u32 val) { (void)a; (void)val; }   /* (v474 : Pulse compact) */
#endif
/* les "interruptions" des cartes : fonctions EXI du jeu appelees depuis une interruption
   (table du SDK : canal 0 = 9 EXI, 10 TC ; canal 1 = 12 EXI, 13 TC) */
static MEM_OS void x_dispatch(u32 ctx) {
    volatile u32 *t = OS_INT_TABLE;
    for (int c = 0; c < 2; c++) {
        vcard_t *v = &vc[c];
        if (!v->on) continue;
        u32 itc = c ? 13 : 10, iex = c ? 12 : 9;
        for (int g = 0; g < 3 && (v->irq & vc_want(c)); g++) {
            u32 w = vc_want(c);
            if ((v->irq & w & 0x8) && t[itc] >= 0x80003100u && t[itc] < NATRES_AT) ((void (*)(u32, u32))t[itc])(itc, ctx);
            if ((v->irq & vc_want(c) & 0x2) && t[iex] >= 0x80003100u && t[iex] < NATRES_AT) ((void (*)(u32, u32))t[iex])(iex, ctx);
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
static MEM_OS u32 si_fix(int ch, int lo) {               /* (v410 : -Os) */
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
#if !defined(NATRES_COMPACT)
static MEM_OS u32 si_load(u32 a) {
    trap_guard();
    u32 o = a - 0xCC006400u, v = *HW6(a);
    int ch = (int)(o / 12u), r = (int)(o - (u32)ch * 12u);
    if (o < 0x30u && r == 4) { igr_gh[ch] = v; igr_gt = H.vi_count; }   /* v457 : touches lues par le jeu (A+Z+START) */
    if (H.opt & NRO_PHONE) { natres_pad_t *p = pd_find(); int done; if (p) { v = pd_si_load(p, o, v, &done); if (done) return v; } }   /* */
    if (o < 0x30u && (H.opt & 0xFF07u)) {
        if ((r == 4 || r == 8) && si_mode(ch) == 3) {
            if (r == 4) { si_h[ch] = v; si_l[ch] = *HW6(a + 4); return si_fix(ch, 0); }   /* (H : on lit aussi L) */
            si_l[ch] = v; return si_fix(ch, 1);
        }
    }
    return v;
}
#else
static u32 si_load(u32 a) { return *(volatile u32 *)a; }   /* v474 : Pulse compact */
#endif
#if !defined(NATRES_COMPACT)
static MEM_OS void si_store(u32 a, u32 val) {
    trap_guard();
    if (H.opt & NRO_PHONE) {                              /* transfert vers un port : on retient le port et la commande */
        natres_pad_t *p = pd_find();
        if (p && a == 0xCC006480u) { p->xcmd = val >> 24; gb_o0 = val; }
        if (p && a == 0xCC006484u) gb_o1 = val;            /* (v331 : 2e mot : 4e octet ecrit a la GBA) */
        if (p && a == 0xCC006434u && (val & 1)) {
            /* v339 : la commande, lue dans le vrai tampon d'envoi : le jeu l'y ecrit parfois sans aiguillage (boucle a
               adresse calculee de la bibliotheque SI : Rayman 3) ; avant, Pulse croyait chaque transfert "00" (la remise a
               zero FF de la GBA repondue comme un "00" : le jeu recommencait sans fin) */
            gb_o0 = *HW6(0xCC006480u); gb_o1 = *HW6(0xCC006484u); p->xcmd = gb_o0 >> 24;
            p->xch = (val >> 1) & 3; p->tr[p->ntr++ & 7] = p->xch << 24 | (p->xcmd & 0xFF) << 16 | ((val >> 16) & 0x7F) << 8 | ((val >> 8) & 0x7F);
            if (gb_port(p, p->xch)) {                     /* v331 : GBA du telephone : la reponse tout de suite */
                gb_cmd(p);
                /* v342 : le vrai transfert (aiguille vers une vraie manette) devient un "00" (1 octet envoye, 3 attendus) :
                   une manette ne repond pas a 14 / 15, le vrai COMCSR finissait en ERREUR et le jeu, qui le lit parfois sans
                   aiguillage, abandonnait (Rayman 3 : la lecture de la cle). La reponse de la GBA est ecrite dans le vrai
                   tampon a la fin (si_hook, v341) */
                *HW6(0xCC006480u) = 0;
                val = (val & ~0x007F7F00u) | 0x00010300u;
            }
            if ((p->chmask | p->gbmask) >> p->xch & 1) {
                pd_ev(p, 1, p->xch << 8 | (p->xcmd & 0xFF));
                /* b9000015 : rien n'est branche sur ce port : le vrai transfert finissait en erreur (pas de reponse), et une
                   partie des lectures du jeu (hors aiguillages) le voyait encore : le jeu redemandait le type sans fin. Le
                   transfert part donc vers une vraie manette lue en ce moment (SIPOLL) : il finit sans erreur, et le jeu lit
                   nos reponses (tampon, COMCSR) comme venant du port du telephone. */
                for (u32 c = 0; c < 4; c++)
                    if (!((p->chmask | p->gbmask) >> c & 1) && (p->poll >> (7 - c) & 1)) { val = (val & ~6u) | c << 1; p->redir++; break; }
                /* v336 : erreur restee dans le vrai SISR pour ce port (un 1er transfert vers le port vide, avant l'aiguillage) :
                   effacee. Le jeu lit parfois SISR sans aiguillage (fin de transfert de la bibliotheque SI) : avec l'erreur, sa
                   detection du type rendait "pas de reponse" (Rayman 3, Four Swords : GBA jamais reconnue) */
                *HW6(0xCC006438u) = 0x0Fu << ((3u - p->xch) * 8u);
            }
        }
        if (p && a == 0xCC006430u) { if (p->poll != val) pd_ev(p, 2, val); p->poll = val; }
        if (p && a < 0xCC006430u && (a - 0xCC006400u) % 12u == 0 && (p->chmask >> ((a - 0xCC006400u) / 12u) & 1)) {
            pd_ev(p, 3, val);
            if ((val >> 16) == 0x40u) p->rmb[(a - 0xCC006400u) / 12u] = (val & 3u) == 1u;   /* v275 : moteur (0 arret, 1 marche, 2 arret net) */
        }
    }
    u32 k = (H.opt >> 16) & 0xF;
    if (a == 0xCC006430u && k > 1) {                     /* SIPOLL : Y lectures par image, X lignes entre deux */
        u32 x = (val >> 16) & 0x3FF, y = (val >> 8) & 0xFF;
        y *= k; if (y > 255) y = 255;
        x /= k; if (x < 1) x = 1;
        val = (val & 0xFC0000FFu) | x << 16 | y << 8;
    }
    *HW6(a) = val;
}
#else
static void si_store(u32 a, u32 val) { *(volatile u32 *)a = val; }   /* v474 : Pulse compact */
#endif

/* ======================= v121 : cheats (codes Gecko) =======================
   GC Station range ici les codes actives du jeu : {nombre de mots, puis les codes (2 mots chacun)}.
   A chaque image, ils sont appliques comme le fait un gestionnaire de codes Gecko (format public des codes
   "WiiRD / Gecko"). Types pris en charge (GC Station verifie avant le lancement, sinon : lanceur de secours) :
   00-07 ecritures (8, 16, 32 bits, avec repetition ; texte), 08 ecriture en serie, 20-2F conditions (32 bits,
   16 bits masque), 40 42 48 4A adresse de base / pointeur, C2 code insere, E0 E2 fins de conditions, F0 fin.
   Bit 0x10 du 1er octet : adresse relative au pointeur (po) au lieu de l'adresse de base (ba). */
static int gk_ram(u32 a, u32 n) { return a >= 0x80000000u && a + n <= NATRES_AT && a + n > a; }
static MEM_OS void gk_sync(u32 a, u32 len) {
    for (u32 x = a & ~31u; x < a + len; x += 32) __asm__ volatile("dcbst 0,%0; sync; icbi 0,%0" :: "r"(x) : "memory");
    __asm__ volatile("sync; isync" ::: "memory");
}
static MEM_OS void gk_w8(u32 a, u32 v) { if (gk_ram(a, 1)) { *(volatile u8 *)a = (u8)v; gk_sync(a, 1); } }
static MEM_OS void gk_w16(u32 a, u32 v) { a &= ~1u; if (gk_ram(a, 2)) { *(volatile unsigned short *)a = (unsigned short)v; gk_sync(a, 2); } }
static MEM_OS void gk_w32(u32 a, u32 v) { a &= ~3u; if (gk_ram(a, 4)) { *(volatile u32 *)a = v; gk_sync(a, 4); } }
static u32 gk_r32(u32 a) { a &= ~3u; return gk_ram(a, 4) ? *(volatile u32 *)a : 0; }
static u32 gk_r16(u32 a) { a &= ~1u; return gk_ram(a, 2) ? *(volatile unsigned short *)a : 0; }
/* niveaux de conditions : depth = ouverts, skip = 1er niveau faux (0 = tout est vrai, on applique) */
static void gk_endif(u32 *depth, u32 *skip, u32 n) {
    *depth = n > *depth ? 0 : *depth - n;
    if (*skip > *depth) *skip = 0;
}
#if !defined(NATRES_COMPACT)
static MEM_OS void gk_run(void) {                            /* (v407 : -Os, place pour la suite) */
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
#else
static void gk_run(void) { }   /* (v474 : Pulse compact) */
#endif

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
        "  lis 8,0x0180\n  addi 8,8,-4\n  lwz 11,0(9)\n  lwz 12,4(9)\n  andis. 0,11,0x0100\n  beq .Lach_c\n"   /* (v389 : bit 24 seul : pointeur) */
        "  lwz 0,0(12)\n  lwz 12,8(9)\n  add 12,0,12\n"   /* pointeur (adresse verifiee par GC Station) : valeur lue + decalage */
        ".Lach_c:\n"
        "  addis 0,12,-0x8000\n  cmplw 0,8\n  li 0,0\n  bgt .Lach_v\n"   /* (hors de la memoire du jeu : 0) */
        "  rlwinm 8,11,16,29,31\n  cmpwi 8,2\n  lhz 0,0(12)\n  beq .Lach_v\n  lbz 0,0(12)\n  blt .Lach_v\n  lwz 0,0(12)\n"   /* taille 2, 1, 4 */
        ".Lach_v:\n"                                      /* bit 8 + (v >= c) + (v > c) du type */
        "  lwz 12,12(9)\n  andis. 8,11,0x0200\n  beq .Lach_w\n  and 0,0,12\n"   /* (v389 : bit 25 : bits a 1 : v & c, compare a c) */
        ".Lach_w:\n  li 8,8\n  cmplw 0,12\n  blt .Lach_m\n  li 8,9\n  beq .Lach_m\n  li 8,10\n"
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
    u32 got = ach_eval(a, H.vi_count);
    ach_dirty |= got;
    if (got && pd) pd->mach = 3;                          /* v306 : annonce au telephone (pd_poll) */
    /* (bus pris par le jeu, carte pas encore demarree : a l'image suivante) */
    if (ach_dirty && di_ready && !exi_defer() && !nr_write_sd(H.log_sec + 5, (const u8 *)a)) ach_dirty = 0;
}

/* v306 : succes obtenus (bits 0-63) pour le telephone ; rend leur nombre (0 : pas de succes) */
static u32 ach_bits(u32 *b) {
    u32 *a = ach_p;
    b[0] = b[1] = 0;
    if (!a || (u32)a == 1) return 0;
    u32 n = a[1];
    for (u32 i = 0; i < n && i < 64; i++) if (a[2 + 2 * i] & 2) b[i >> 5] |= 1u << (i & 31);
    return n;
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
    if (!WT_SELF && (DI_REAL[7] & 1)) return;           /* v516 : pas avec le reveil provoque (il ne passe pas par le vrai lecteur).
                                                            Console sans lecteur (Platter, build 1042-1043) : DICR toujours occupe,
                                                            la fin d'une lecture de la carte virtuelle n'arrivait jamais : jeu fige */
    x_kicked = 1;
    di_real_kick();
}
/* v67 : echantillon : ou le jeu a ete interrompu (OSContext : srr0 en 0x198, lr en 0x84) */
static MEM_OS void nr_sample(u32 ctx) {
    if (ctx < 0x80000000u || ctx >= NATRES_AT || (ctx & 3)) return;
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
#if !defined(NATRES_NOLOG)
static MEM_OS void vc_diag(void) {
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
#endif
#if !defined(NATRES_COMPACT)
static MEM_OS __attribute__((noinline)) void rd_adv(u32 n) {   /* (n octets de la lecture en fond arrives) */
    cache_flush(rd_dst, n);
    rd_dst += n; rd_off += n; rd_left -= n;
    di[5] += n; di[6] -= n;
}
#endif
#if !defined(NATRES_NO_XV) && !defined(NATRES_COMPACT)                                     /* (v437 : pas dans les Pulse du mode universel : place) */
/* ======================= v437 : lecture de la carte par interruptions (option NRO_XV, essai) =======================
   Sans DMA (port serie 2), chaque mot de 4 octets de la carte est un transfert du bus (~1 us a 32 MHz). Avant, Pulse
   attendait la fin de chacun, interruptions du jeu coupees, par paquets de 1,5 a 3 ms : le jeu (video, son) ne tournait
   pas pendant ce temps. Ici, Pulse lance le transfert et rend la main ; la fin du transfert (interruption "TC" du canal)
   arrive par le vecteur des interruptions externes (0x500), avant le systeme du jeu : xv_vec garde les registres, active
   la traduction des donnees (registres du bus sans cache), appelle xv_c sur une petite pile a part, puis revient au jeu
   (rfi). Ce qui n'est pas pour nous repart dans le vecteur du SDK (son 1re instruction, puis 0x504). xv_c fait avancer
   la lecture : attente du jeton du bloc (FE), 128 mots (directement a destination), 2 octets de CRC (pas verifie ici),
   bloc suivant. rd_pump (au reveil du lecteur) lance une suite de secteurs et regarde ou elle en est. Si les
   interruptions n'arrivent pas (masquees par le jeu...), la suite est finie a l'ancienne (xv_drain), sans rien perdre.
   Pas avec Pulse universel (registres a une 2e adresse), ni avec une carte dans un port de carte memoire.
   Code d'execution sans traduction des instructions (adresses physiques) : pas de saut par table dans xv_c. */
/* (place : les registres sont gardes dans trap_regs (Pulse universel, jamais en meme temps) et la pile est le haut de
   exc_stack (pile du journal de plantage)) */
static volatile u32 xv_st = 0;                          /* 0 rien, 1 attente du jeton, 2 mots du bloc, 3 CRC */
static volatile u32 xv_row = 0;                         /* (v444 : reprises de suite sans vraie interruption) */
static u32 xv_tok, xv_n, xv_nsec, xv_cnt, xv_err, xv_sd, xv_words, xv_last, xv_off;
static u32 *xv_buf;
static u32 xv_dm = 0, xv_cs = 0;                       /* v501 : suite en DMA (un bloc de 512 octets par transfert, comme Swiss) ;
                                                            CS de la carte pendant le bloc (2e facon) */
static u32 xv_fall = 0, xv_runs = 0;                     /* (journal) suites finies a l'ancienne, suites lancees */
static u32 xv_ptb = 0;                                   /* v517 : heure du dernier vrai progres de la suite (xv_adv) */
extern u32 xv_vec[], xv_orig[];
static void trap_sync(u32 a);
int xv_c(void) __attribute__((used));
__asm__(".pushsection .text\n.globl xv_vec\n.globl xv_orig\n.p2align 5\nxv_vec:\n"
        "  mtspr 274,3\n  mtspr 275,4\n"
        "  lis 4,trap_regs@ha\n  addi 4,4,trap_regs@l\n  clrlwi 4,4,2\n"           /* (sans traduction : adresse physique) */
        "  mfcr 3\n  stw 3,0(4)\n  stw 0,4(4)\n  stw 1,8(4)\n  mfspr 3,274\n  stw 3,12(4)\n  mfspr 3,275\n  stw 3,16(4)\n"
        "  stw 5,20(4)\n  stw 6,24(4)\n  stw 7,28(4)\n  stw 8,32(4)\n  stw 9,36(4)\n  stw 10,40(4)\n  stw 11,44(4)\n  stw 12,48(4)\n"
        "  mflr 3\n  stw 3,52(4)\n  mfctr 3\n  stw 3,56(4)\n  mfxer 3\n  stw 3,60(4)\n"
        "  mfmsr 3\n  stw 3,64(4)\n  ori 3,3,0x10\n  mtmsr 3\n  isync\n"            /* (traduction des donnees : registres sans cache) */
        "  lis 1,exc_stack@ha\n  addi 1,1,exc_stack@l\n  addi 1,1,1584\n"
        "  bl xv_c\n"
        "  lis 4,trap_regs@ha\n  addi 4,4,trap_regs@l\n"                          /* (v440 : adresse normale : la traduction est encore active) */
        "  lwz 5,64(4)\n  mtmsr 5\n  isync\n  clrlwi 4,4,2\n"                    /* (de nouveau sans traduction, puis adresse physique) */
        "  lwz 5,52(4)\n  mtlr 5\n  lwz 5,56(4)\n  mtctr 5\n  lwz 5,60(4)\n  mtxer 5\n"
        "  lwz 0,4(4)\n  lwz 1,8(4)\n  lwz 6,24(4)\n  lwz 7,28(4)\n  lwz 8,32(4)\n  lwz 9,36(4)\n  lwz 10,40(4)\n  lwz 11,44(4)\n  lwz 12,48(4)\n"
        "  cmpwi 3,0\n  beq 8f\n"                                                 /* (le choix est fait avant de remettre CR) */
        "  lwz 5,0(4)\n  mtcrf 0xFF,5\n  lwz 5,20(4)\n  lwz 3,12(4)\n  lwz 4,16(4)\n  rfi\n"
        "8:\n  lwz 5,0(4)\n  mtcrf 0xFF,5\n  lwz 5,20(4)\n  lwz 3,12(4)\n  lwz 4,16(4)\n"
        "xv_orig:\n  nop\n  ba 0x504\n.popsection\n");
#define XV_BR (0x48000002u | ((u32)xv_vec & 0x03FFFFFCu))    /* (ba xv_vec, en adresse physique) */
static void xv_go(u32 n) { exi[4] = 0xFFFFFFFFu; exi[3] = ((n - 1u) << 4) | 1u; }   /* (n octets, lecture, depart) */
static void xv_stop(void) { exi[0] = exi[0] & ~0x80Eu; xv_st = 0; }   /* (fin : plus d'interruption de fin de transfert) */
/* v501 : bloc de 512 octets en DMA dans la suite (comme Swiss) : fin = 0 : depart ; fin = 1 : le bloc est arrive */
static MEM_OS __attribute__((noinline)) void xv_dmab(int fin) {
    u32 cs = fin ? xv_cs : exi[0];
    if (fin) { if (x_dnocs) exi[0] = (cs & 0x405u) | (cs & 0x3F0u); dc_inv512((u8 *)xv_buf); xv_buf += 128; return; }
    dc_inv512((u8 *)xv_buf);
    exi[4] = 0xFFFFFFFFu;                                /* (v493 : 0xFF envoye a la carte pendant le bloc) */
    xv_cs = cs;
    if (x_dnocs) exi[0] = (cs & 0x405u) | (cs & 0x070u);   /* (v494 : carte pas choisie pendant le bloc) */
    exi[1] = (u32)xv_buf & 0x1FFFFFE0u; exi[2] = 512; exi[3] = 3;
}
static int xv_start(u32 sd, u32 nsec, u32 *dst);
/* v527 : une suite finie (dans l'interruption) : comptee ici, et la suivante part tout de suite si la lecture continue au
   secteur suivant de la carte (lecture de la carte encore ouverte : pas de commande). Avant, chaque suite de 32 Ko au plus
   attendait un aller-retour (reveil du lecteur, crochet de Pulse, rd_pump) ; Swiss enchaine jusqu'a la fin de la lecture.
   Sinon (fin, autre morceau du disque, reste de moins d'un secteur) : rd_pump comme avant. */
static u32 xv_rr = 0;                                    /* (secteurs du meme morceau du disque apres la suite en cours) */
static u32 xv_nod = 0;                                   /* (xv_drain en cours : pas de suite suivante, elle serait lue d'un coup) */
static MEM_OS __attribute__((noinline)) int xv_more(void) {
    if (!rd_on || xv_err || !xv_nsec || xv_nod) return 0;
    rd_adv(xv_cnt << 9);
    xv_nsec = 0;                                         /* (deja comptee : rd_pump ne la recompte pas) */
    u32 ns = rd_left >> 9;
    if (ns > xv_rr) ns = xv_rr;
    { u32 mx = xv_max(); if (ns > mx) ns = mx; }
    if (!ns || xv_off) return 0;
    xv_rr -= ns;
    return xv_start(st_next, ns, (u32 *)rd_dst) == 0;   /* (meme morceau, a la suite : la carte lit deja la) */
}
/* un transfert fini : la suite (appele par l'interruption, ou par xv_drain) */
/* v443 : un transfert fini : acquitte, interruption de fin reposee (le systeme du jeu reecrit les masques des canaux EXI
   quand il ouvre ou ferme ses interruptions : la notre disparaissait, la suite attendait pour toujours), mot suivant */
static MEM_OS void xv_adv(u32 c) {                     /* (v501 : -Os, place) */
    xv_ptb = tbl();
    exi[0] = (c & ~0x802u) | 0xCu;
    u32 d = exi[4];
    if (xv_st == 1) {
        if ((d >> 24) == 0xFEu && xv_dm) { xv_st = 2; xv_dmab(0); }   /* v501 : le bloc entier en DMA : une seule interruption a sa fin */
        else if ((d >> 24) == 0xFEu) { xv_st = 2; xv_n = 128; xv_go(4); }
        else if (--xv_tok) xv_go(1);
        else { H.trap_state = 1u << 28 | 1u << 24 | (d >> 24) << 16 | (xv_cnt & 0xFFu) << 8 | (xv_nsec & 0xFFu); xv_err = 1; xv_stop(); }
                                                        /* (st_next reste invalide : la lecture sera arretee ; v442 : detail, journal) */
    } else if (xv_st == 2) {
        if (xv_dm) { xv_dmab(1); xv_st = 3; xv_go(2); return; }   /* v501 : bloc en DMA fini */
        *xv_buf++ = d;
        if (--xv_n) xv_go(4); else { xv_st = 3; xv_go(2); }
    } else {
        if ((x_fast | xv_dm) && crc16((const u8 *)(xv_buf - 128), 512) != (d >> 16)) {   /* v451 : a 32 MHz, bloc verifie (CRC) comme a l'ancienne (v501 : en DMA aussi) */
            H.crc_err++; x_crcbad = 1; xv_err = 1; xv_stop(); return;
        }
        H.nread++; xv_cnt++;
        if (xv_cnt < xv_nsec) { xv_st = 1; xv_tok = 400000u; xv_go(1); }
        else { st_next = xv_sd + xv_cnt; if (xv_more()) return; xv_stop(); if (rd_on && (SELFIRQ_ON || !(DI_REAL[7] & 1))) di_real_kick(); }   /* (v523 : avec le vrai lecteur
                                                            aussi : la fin de la suite relance la lecture en fond tout de suite) */   /* (la carte continue : la suite pourra reprendre la ;
                                                            v483 : reveil provoque : la lecture en fond avance a la fin de la suite) */
    }
}
int xv_c(void) {
    if (!xv_st) return 0;
    xv_words++;
    u32 c = exi[0];
    if ((c & 0xCu) != 0xCu) return 0;                   /* (fin de transfert de ce canal, que l'on a demandee ?) */
    if (exi[3] & 1u) { exi[0] = (c & ~0x802u) | 8u; return 1; }   /* v441 : fin d'un transfert d'avant : le notre n'est pas fini */
    xv_row = 0;                                          /* (v444 : une vraie interruption : les interruptions arrivent) */
    PF_T0(pt_);
    xv_adv(c);
    PF_ADD(4, pt_);
    return 1;
}
/* finir la suite sans interruptions (jeu qui les masque, ou autre commande a la carte) */
static MEM_OS void xv_drain(void) {
    u32 m = irq_off();                                   /* (sinon l'interruption et cette boucle se disputeraient le meme mot) */
    xv_nod = 1;
    for (u32 i = 0; xv_st && i < 50000000u; i++) if (!(exi[3] & 1u)) { xv_words++; xv_adv(exi[0]); }   /* (v443 : transfert arrete = fini) */
    xv_nod = 0;
    if (xv_st) { H.trap_state = 2u << 28 | xv_st << 24 | (exi[3] & 0xFFu) << 16 | (xv_cnt & 0xFFu) << 8 | (xv_nsec & 0xFFu); xv_err = 1; xv_stop(); }   /* (v442 : detail) */
    irq_restore(m);
}
/* v439 : journal des etapes de la lecture par interruptions (cases du mode universel, jamais utilise avec : trap_n, trap_pc,
   trap_bad) : trap_bad = les 8 dernieres etapes (4 bits chacune, la plus recente en bas), trap_pc = etape << 24 | image,
   trap_n = interruptions vues pendant une suite. Les 24 premieres etapes importantes ecrivent le journal sur la carte
   aussitot (pas pendant une suite) : apres un blocage, le journal dit jusqu'ou Pulse est alle. */
static u32 xv_nerr = 0, xv_lost = 0;
/* v527 : etapes plus notees (place pour l'enchainement des suites dans l'interruption ; la mesure des chargements dit
   mieux ou passe le temps) : avant, H.trap_pc / trap_bad / trap_n (8 dernieres etapes, interruptions vues) */
#define xv_mark(step, fl) ((void)0)
static MEM_OS __attribute__((noinline)) int xv_ok(void) {
    return !(xv_off || !(H.opt & NRO_XV) || !SD_OWN_CH || hw_d || H.net);   /* (v501 : avec le DMA aussi : blocs en DMA enchaines) */   /* (v488 : slot B aussi, carte virtuelle imposee) */
}
/* lancer la lecture de nsec secteurs (carte : sd) vers dst (aligne sur 4) */
static MEM_OS __attribute__((noinline)) int xv_start(u32 sd, u32 nsec, u32 *dst) {
    if (!(st_open && sd == st_next)) {
        u32 r = sd_cmd(18, H.sdhc ? sd : sd << 9, 0x01);
        if (r != 0) { sd_end(); H.err = NR_ECMD; return -1; }
        st_open = 1;
    }
    volatile u32 *v = (volatile u32 *)0x80000500u;      /* (v438 : vecteur pose seulement au 1er transfert, pas avant) */
    if (v[0] != XV_BR) { xv_orig[0] = v[0]; trap_sync((u32)xv_orig); v[0] = XV_BR; trap_sync(0x80000500u); }
    xv_buf = dst; xv_nsec = nsec; xv_cnt = 0; xv_err = 0; xv_sd = sd; xv_tok = 400000u; xv_last = xv_words - 1u; xv_ptb = tbl();
    xv_dm = x_dma && !((u32)dst & 31u);                  /* v501 : DMA si le DMA est valide et la destination alignee */
    st_next = 0xFFFFFFFFu;                               /* (pendant la suite : personne ne continue la lecture de la carte) */
    u32 m = irq_off();
    /* (v438 : le masque des interruptions du processeur (PI) n'est plus touche : en ouvrant celles du bus EXI, le jeu recevait
       aussi celles des autres peripheriques qu'il avait fermees et ne s'en sortait plus (ecran noir des la 1re lecture, Mario
       Kart). S'il les a fermees, les notres n'arrivent pas : la suite est finie a l'ancienne (rd_pump, xv_drain)) */
    exi[0] = (exi[0] & ~0x802u) | 0xCu;                  /* (fin de transfert de ce canal : interruption ; v441 : l'ancienne fin, laissee
                                                            par les lectures normales, effacee (1 = efface) : sinon interruption tout de
                                                            suite, mot pas encore arrive, transfert relance par-dessus : suite decalee) */
    xv_st = 1; xv_go(1);
    irq_restore(m);
    if (xv_runs < 0x7FFFu) xv_runs++;
    H.xfer = (H.xfer & 0xFFFFu) | 0x80u | (xv_runs | (xv_off ? 0x8000u : 0u)) << 16;   /* (v438 : journal : suites lancees, mode coupe) */
    return 0;
}
#else
static volatile u32 xv_st = 0;
static u32 xv_nsec = 0, xv_cnt, xv_err, xv_words, xv_last, xv_off, xv_fall, xv_runs, xv_nerr, xv_lost, xv_row, xv_ptb;
static void xv_adv(u32 c) { (void)c; }
static void xv_drain(void) { }
static void xv_mark(u32 step, int fl) { (void)step; (void)fl; }
static int xv_ok(void) { return 0; }
static int xv_start(u32 sd, u32 nsec, u32 *dst) { (void)sd; (void)nsec; (void)dst; return -1; }
static u32 xv_rr = 0;
#endif
/* lecture en fond : les morceaux suivants. 0 = pas finie, 1 = finie (TCINT), -1 = erreur (DEINT) */
#if !defined(NATRES_COMPACT)
#if PF_ON
static int rd_pump_i(void);
static MEM_OS __attribute__((noinline)) int rd_pump(void) { PF_T0(t); int r = rd_pump_i(); PF_ADD(6, t); return r; }   /* v526 : mesure */
#else
#define rd_pump_i rd_pump
#endif
static MEM_OS int rd_pump_i(void) {                   /* (v437 : -Os, place) */
    EV(0x25, rd_off, rd_left);
    u32 t0 = tbl(), bud = wake_tick() ? RD_BUDGET * 2u : SELFIRQ_ON ? RD_BUDGET / 3u : RD_BUDGET;   /* (v133 : sans le lecteur, moins souvent : plus par fois ;
                                                            v524 : reveil provoque : 1 ms au plus par interruption du jeu (avant 3 ms : la lecture
                                                            se fait dans les interruptions du son et de l'image, le jeu saccadait a chaque chargement,
                                                            Platter et Kirby Air Ride sans lecteur, build 1052 : 48 attentes du son de plus de 12 ms
                                                            par minute) ; elle y revient plus souvent */
    int err = 0, k = 0;
    if (rd_left && exi_defer()) { H.exi_wait++; rd_vi = H.vi_count; return 0; }   /* v145 : bus EXI pris par le jeu : plus tard */
    if (rd_left && !xv_st && !sd_free()) { rd_vi = H.vi_count; return 0; }   /* build 1063 : carte SD encore occupee a enregistrer : plus tard */
    if (xv_ok() || xv_st || xv_nsec) {                   /* v437 : lecture par interruptions */
        xv_mark(1, 0);                                   /* (v439 : 1 = lecture en fond appelee) */
        if (xv_st) {
            u32 m = irq_off(), lost = 0;
            if (xv_words == xv_last && xv_st && !(exi[3] & 1u)) { xv_words++; xv_adv(exi[0]); lost = 1; }   /* v443 : fin perdue (masque
                                                            efface par le systeme du jeu) : reprise ici, et l'interruption reposee */
            irq_restore(m);
            if (lost && ++xv_row >= 16u) lost = 0;      /* (v444 : 16 reprises de suite sans vraie interruption : ce jeu ne les
                                                            laisse pas passer : finie a l'ancienne, et plus de suites pour ce jeu) */
            if (lost) { if (xv_lost < 0xFFFu) xv_lost++; xv_mark(9, 0); xv_last = xv_words; rd_vi = H.vi_count; return 0; }   /* (9) */
            if (xv_row >= 16u) xv_last = xv_words;
            int stuck = tbl() - xv_ptb > TB_SEC / 100u;  /* (v518 : 10 ms) */   /* v517 : xv_words compte toutes les interruptions externes (image, son,
                                                            adaptateur reseau...), pas seulement les fins de la carte : une suite dont la
                                                            fin se perdait n'etait jamais vue bloquee (Kirby Air Ride, build 1043-1044 :
                                                            erreur du jeu au chargement des sons). Avant, la reecriture du journal toutes
                                                            les 2-3 s la finissait au passage (sd_cmd -> xv_drain). Maintenant : 20 ms sans
                                                            progres = finie ici ; les suites restent permises (16 fois au plus) */
            if (xv_words == xv_last || stuck) { xv_drain(); xv_fall++; if (!stuck || ++xv_row >= 16u) xv_off = 1; xv_mark(5, 1); }   /* (5 = finie a l'ancienne ;
                                                            v518 : coupee seulement apres 16 suites bloquees sans une seule vraie interruption entre
                                                            deux (xv_row, remis a 0 par xv_c) : Kirby en perd une de temps en temps, 16 en 49 s, et repassait aux
                                                            lectures d'un coup, 18 % du temps dans Pulse, son coupe jusqu'a 0,57 s) */   /* (rien depuis le dernier reveil : les interruptions
                                                            n'arrivent pas, ce jeu les masque : finie a l'ancienne, et plus pour ce jeu) */
            else { xv_last = xv_words; rd_vi = H.vi_count; return 0; }
        }
        int redo = 0;
        if (xv_nsec) {                                   /* (suite finie : la lecture en fond avance) */
            u32 n = xv_cnt << 9;
            xv_nsec = 0;
            H.xfer = (H.xfer & 0xFFFFu) | (xv_runs | (xv_off ? 0x8000u : 0u)) << 16;
            if (xv_err) {                                /* (6 = erreur ; v442 : plus d'erreur donnee au jeu : ce morceau est relu a
                                                            l'ancienne, juste en dessous ; 3 suites ratees : plus de suites pour ce jeu) */
                xv_mark(6, 1); redo = 1;
                if (++xv_nerr >= 3u) xv_off = 1;
                if (x_fast && x_crcbad) { x_fast_off = 1; x_crcbad = 0; di_ready = nr_init() == 0; }   /* v451 : CRC faux a 32 MHz : 16 MHz */
            } else {
                xv_mark(4, 1);                           /* (4 = une suite finie) */
                rd_adv(n);
            }
        }
        u32 sd;
        if (!redo && !xv_off && rd_left >= 512u && !(rd_off & 511u) && !(rd_dst & 3u) && !iso_to_sd(rd_off >> 9, &sd) && sd != NATRES_ZERO) {
            u32 ns = rd_left >> 9;
            if (ns > iso_run) ns = iso_run;
            { u32 mx = xv_max(); if (ns > mx) ns = mx; }
            xv_rr = iso_run - ns;                        /* v527 */
            xv_mark(2, 1);                               /* (2 = une suite va partir) */
            if (!xv_start(sd, ns, (u32 *)rd_dst)) { xv_mark(3, 0); rd_vi = H.vi_count; return 0; }   /* (3 = partie) */
            xv_mark(7, 1);                               /* (7 = depart refuse par la carte) */
        }
    }
    if (rd_left) do {                                    /* v88 : des morceaux de 2 Ko tant qu'on reste sous ~1,5 ms (v122 : rien si deja tout lu) */
        /* (v430 : morceau taille au temps mesure par secteur : retire en v433, Mario Kart se figeait pendant un chargement :
           une 1re mesure trop courte (secteur deja en cache) faisait lire jusqu'a 32 Ko d'un coup, interruptions coupees) */
        u32 n = RD_CHUNK - (rd_off & 511u);             /* v434 : morceaux alignes sur les secteurs (lecture en continu) */
        if (n > rd_left) n = rd_left;
        H.n_chunks++;
        if (nr_read((void *)rd_dst, rd_off, n)) { err = 1; nr_log_add(di[2], rd_off, n, 1 | (H.err & 0xFF) << 24); break; }
        rd_adv(n);
    } while (rd_left && ++k < (wk_tick == 1 ? 32 : 8) && tbl() - t0 < bud
             && (rd_off & 511u || !xv_ok()));   /* (v94 : 16 Ko au plus : la carte est plus rapide en DMA / 32 MHz ;
                                                            v524 : des que la lecture est alignee sur un secteur, la suite par interruptions
                                                            prend le relais (a la prochaine interruption) : avant, une lecture du jeu pas alignee (Kirby : 6 788 morceaux
                                                            de 2 Ko contre 717 suites en 1 min) etait lue entiere par morceaux, tout bloque) */
    rd_vi = H.vi_count;
    if (err) { rd_on = 0; di[0] |= 0x04; di[7] &= ~1u; return -1; }   /* DEINT : erreur, le jeu redemande */
    if (rd_left) return 0;                               /* (morceau suivant a la prochaine fin) */
    if ((H.opt & NRO_SPEED) && (int)(sp_until - tbl()) > 0 && ++sp_kicks < 20000u) return 0;   /* v122 : un vrai lecteur n'aurait pas fini */
    rd_on = 0; di[0] |= 0x10; di[7] &= ~1u;              /* TCINT : lecture terminee */
    PF_LD1();
    EV(0x24, rd_off0, rd_len0);
    EVSUM(rd_off0, rd_len0, rd_dst0);
    nx_read_done(rd_dst0, rd_off0, rd_len0);
    return 1;
}
#else
static int rd_pump(void) { rd_on = 0; return 1; }   /* (compact : jamais de lecture en fond) */   /* v474 : Pulse compact */
#endif
static MEM_OS void di_hook(u32 irq, u32 ctx) {           /* (v501 : -Os, place) */
    PF_T0(pt_);
    trap_guard();
    sf_apply();                                          /* v477 : une interruption du lecteur = la fin en attente */
    fd_ctx = ctx;
    int chain = di_game_pending;
    EVC(0x10, ctx); EV(0x11, DI_REAL[0], (u32)rd_on | (u32)chain << 8 | rd_left << 16);
    if (rd_on) {
        volatile u32 *r = DI_REAL;
        if (H.opt & NRO_XV) xv_mark(8, 0);               /* (v439 : 8 = reveil du vrai lecteur) */
        u32 dt = (tbl() - kick_tb) / 41u;                /* (us depuis le reveil) */
        H.kick_us += dt; if (dt > H.kick_max_us) H.kick_max_us = dt;
        r[0] = (r[0] & 0x2Au) | 0x54u;                   /* (vraie fin : acquittee ici, le jeu n'en sait rien) */
        if (rd_pump() == 0) { if (!SELFIRQ_ON && !xv_st) di_real_kick(); PF_XD(ctx); PF_ADD(3, pt_); return; }   /* (v523 : suite de la carte
                                                            par interruptions en cours : pas de relance a chaque fin ; sa fin relance le lecteur
                                                            (xv_adv), et di_tick relance au plus toutes les 5 ms pour la surveiller. Avant :
                                                            ~3 500 relances par seconde pendant les lectures (Kirby Air Ride, build 1042-1046)) */   /* (v483 : reveil provoque : la suite avance
                                                            par les interruptions du jeu (di_tick), plus en boucle : 74 000 par seconde, saccades) */
        chain = 1;
    }
    if (!chain) DI_REAL[0] = (DI_REAL[0] & 0x2Au) | 0x54u;   /* (reveil pour la carte : le jeu n'attend rien du lecteur) */
    di_game_pending = 0; x_kicked = 0;
    PF_XD(ctx);
    EV(0x12, di_orig, (u32)chain);
    PF_ADD(3, pt_);
    if (chain && di_orig) ((void (*)(u32, u32))di_orig)(irq, ctx);
    EVE(0x13, di[0], di[7]);
    alive_tick();
    if (!H.vi_count && !rd_on && !di_game_pending && !wake_tick() && H.n_samp < 2000000u) {   /* v67 : pas encore d'image : on regarde ou en est le jeu */
        nr_sample(ctx);
        if (!(DI_REAL[7] & 1) && !SELFIRQ_ON) { x_kicked = 1; di_real_kick(); }   /* (v476 : pas avec le reveil provoque : il se relancerait sans fin) */
    }
}
static int nr_game_fn(u32 a) { return a >= 0x80003100u && a < NATRES_AT; }
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
#if !defined(NATRES_COMPACT)
static MEM_OS __attribute__((noinline)) void vc_idle(void) {   /* v512 : a une image sans lecture en cours : une ecriture de la carte au plus */
    if (xv_st) return;
    if (wq_n) { if (sd_free()) wq_write1(); return; }    /* (build 1063 : un secteur en attente, si la carte SD est libre ; aussi
                                                            pendant un chargement, entre deux suites de lectures) */
    if (rd_on) return;
    for (vcard_t *v = vc; v < vc + 2; v++) {
        u32 d = H.vi_count - v->lvi;
        if (v->dirty && d >= 2u) { card_flush(v); return; }
        if (v->emask && d >= 30u) { card_e1(v); return; }   /* (bloc efface pas reecrit depuis 0,5 s : un secteur en FF) */
    }
}
#else
#define vc_idle() ((void)0)
#endif
static MEM_OS void di_tick(u32 ctx, int vi) {            /* (v410 : -Os) */
    if (vi) vc_idle();
    if (exec_on) return;
    if (!SELFIRQ_ON && rd_on && xv_st && !wake_tick() && tbl() - kick_tb > TB_SEC / 200u && !(DI_REAL[7] & 1)) di_real_kick();   /* v523 : suite de la
                                                            carte en cours : un reveil toutes les 5 ms au plus (surveillance : fin perdue, bloquee) */
    if (SELFIRQ_ON && rd_on) {                           /* v483 : reveil provoque : la lecture en fond avance ici, au rythme des
                                                            interruptions du jeu ; sa fin seulement est annoncee par une interruption
                                                            provoquee (la fonction du jeu tourne alors dans sa vraie interruption) */
        if (OS_INT_MASKS & (0x80000000u >> 21)) return;
        if (xv_st && !vi) return;                        /* (suite par interruptions en cours : vue a chaque image, pour la reprise) */
        if (rd_pump() != 0) { di_game_pending = 1; dp_vi = H.vi_count; di_real_kick(); }
        return;
    }
    int rescue = 0;
    if (!wake_tick()) {
        if (!vi) return;
        if (!WT_SELF && (DI_REAL[7] & 1)) {              /* (vrai lecteur occupe : son reveil arrive... sauf s'il est bloque) ; v514 : pas avec le
                                                            reveil provoque, qui ne touche pas au vrai lecteur. Console sans lecteur (Platter) :
                                                            DICR reste occupe (inquiry de GC Station jamais finie), et 2 s apres, Pulse passait aux
                                                            fins donnees aux interruptions du jeu ; au redemarrage du SDK (Shrek, tout coupe sauf
                                                            le lecteur) la fin de la lecture de 0x2440 n'arrivait jamais : redemarrage complet */
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
    if (rescue && ++wd_n >= 3 && !wk_tick && !WT_SELF) { wk_tick = 1; nr_log_add(0xDE000000u, wd_n, 0, H.vi_count); }   /* (v514 : idem) */
    if (rescue) nr_log_flush();
    di_game_pending = 0; x_kicked = 0;
    sf_apply();                                          /* v477 : (secours : la fin en attente aussi) */
    x_dispatch(ctx);
    volatile u32 *t = OS_INT_TABLE;
    u32 f = t[21] == (u32)di_hook ? di_orig : t[21];
    EV(0x18, f, (u32)rescue | (u32)vi << 8);
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
static u32 pef_orig __attribute__((section(".data")));   /* (v529 : dans .data, avant la fin : 32 octets de place au bout de Pulse) */
static u32 trap_regs[40] __attribute__((aligned(32), used));   /* r0-r31, CR, LR, CTR, XER, SRR0, SRR1, DAR, DSISR */
#define trap_pteg ((u32 *)(NATRES_HTAB + NATRES_PTEG))              /* 12 groupes de 8 entrees de la table des pages (place laissee par GC Station) */
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
#if !defined(NATRES_COMPACT)
void MEM_OS trap_c(void) {                                /* (v407 : -Os : mode trap seulement) */
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
#else
void trap_c(void) { }   /* (v474 : Pulse compact) */
#endif
static MEM_OS void trap_pte(u32 page, u32 phys) {               /* page = adresse (0xCC0xx000) -> phys, sans cache, garde */
    u32 pi = (page >> 12) & 0xFFFFu, hash = (trap_vsid ^ pi) & 0x3FFu;
    u32 *g = (u32 *)(NATRES_HTAB + hash * 64u);
    if ((u32)g < (u32)trap_pteg || (u32)g >= (u32)trap_pteg + TRAP_PTEG_LEN) return;
    for (int i = 0; i < 8; i++) if (!(g[i * 2] & 0x80000000u)) {
        g[i * 2 + 1] = (phys & 0xFFFFF000u) | 0x180u | 0x28u | 2u;   /* R, C, WIMG = I + G, lecture / ecriture */
        g[i * 2] = 0x80000000u | trap_vsid << 7 | (pi >> 10);
        return;
    }
}
static MEM_OS int trap_pteg_clear(u32 hash) {                   /* groupe secondaire (hors de trap_pteg) : rien qui nous ressemble */
    const u32 *g = (const u32 *)(NATRES_HTAB + (hash & 0x3FFu) * 64u);
    for (int i = 0; i < 8; i++) if ((g[i * 2] & 0x80000040u) == 0x80000040u && ((g[i * 2] >> 7) & 0xFFFFFFu) == trap_vsid) return 0;   /* (V, H = 1, notre VSID) */
    return 1;
}
#if !defined(NATRES_COMPACT)
static MEM_OS void __attribute__((noinline)) trap_arm(void) {
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
#else
static void __attribute__((noinline)) trap_arm(void) { }   /* (v474 : Pulse compact) */
#endif
static MEM_OS void __attribute__((noinline)) trap_disarm(void) {                          /* (retour a GC Station ; trap_state reste : journal) */
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
static MEM_OS void hook1(int idx, u32 *orig, u32 fn) {   /* (v462 : -Os, place) */
    volatile u32 *t = OS_INT_TABLE;
    u32 v = t[idx];
    if (v && v != fn && nr_game_fn(v)) {
        *orig = v; t[idx] = fn; H.hook_mask |= 1u << idx;
        if (idx == 21 && (exec_on || exec_a) && v - 0x81300000u >= H.reboot_len) { exec_on = exec_a = 0; nr_log_add(0xEF000000u, v, H.tramp_n, 0); nr_log_flush(); }   /* v95 : le nouveau programme a pris la main */
    }
}
static MEM_OS void hooks_check(void) {
    volatile u32 *t = OS_INT_TABLE;
    if (!nr_game_fn(t[21]) && t[21] != (u32)di_hook) return;   /* (pas la table du SDK : on ne touche a rien) */
    trap_check();                                        /* v139 */
    hook1(21, &di_orig, (u32)di_hook);                   /* v61 : DI (lectures en fond) ; v133 : aussi pendant une lecture en fond (avant :
                                                            si le jeu remettait sa fonction pendant ce temps, la lecture ne finissait jamais) */
    hook1(24, &vi_orig, (u32)vi_hook);
    if (!(H.opt & NRO_MIN)) {                            /* v492 : Pulse minimum : seulement image, manettes, lecteur (et la carte) */
        hook1(5, &ai_orig, (u32)ai_hook);                /* (DSP_AI : DMA du son) */
        hook1(7, &dsp_orig, (u32)dsp_hook);
        hook1(18, &pet_orig, (u32)pet_hook);
    }
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
static MEM_OS int nx_tramp(u32 *c, u32 at, u32 site, u32 ins, u32 reg) {
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
static MEM_OS void nx_sync(u32 a, u32 len) {                     /* code ecrit : cache de donnees -> memoire, puis cache d'instructions */
    for (u32 x = a & ~31u; x < a + len; x += 32) __asm__ volatile("dcbst 0,%0; sync; icbi 0,%0" :: "r"(x) : "memory");
    __asm__ volatile("sync; isync" ::: "memory");
}
/* cherche les acces au lecteur dans [a, a + len) (code deja a sa place) et les branche ; meme suivi
   des registres que GC Station (lis 0xCC00, addi, ori ; oublie aux sauts) */
static MEM_OS void nx_patch2(u32 a, u32 fin, u32 len) {        /* a = ou est le code maintenant, fin = ou il tournera */
    if (a < 0x80003000u || a >= NATRES_AT || (a & 3) || (fin & 3) || len < 8) return;
    if (fin < 0x80003000u || fin >= NATRES_AT) return;
    if (a + len > NATRES_AT) len = NATRES_AT - a;
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
                int dia = ea - 0xCC006000u < 0x40u && (store || ea != 0xCC006024u);   /* v497 : DICFG lu directement (pas de detour : EXISync le relit a chaque transfert) */
                if (dia && !(H.tramp_at && H.tramp_at + 64 * 4 <= end) && H.exec_n < 0xFFFF0000u) H.exec_n += 0x10000u;   /* v253 : pas de place (16 bits hauts) */
                if (dia && H.tramp_at && H.tramp_at + 64 * 4 <= end) {
                    u32 site = fin + o, at = H.tramp_at;
                    int d = (int)(at - site);
                    int n = d < 0x02000000 && d >= -0x02000000 ? nx_tramp((u32 *)at, at, site, ins, ea - 0xCC006000u) : 0;
                    if (n) {
                        nx_sync(at, (u32)n * 4);
                        t[o >> 2] = 0x48000000u | ((u32)d & 0x03FFFFFCu);
                        nx_sync(a + o, 4);
                        H.tramp_at = at + (u32)n * 4;                        /* v456 : serres (avant : arrondis a 32 octets) */
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
static u32 vi_orig, si_orig, ai_orig, dsp_orig, pet_orig;   /* (v529 : pef_orig plus haut, avant trap_regs : 32 octets de place) */
/* v459 : 1re commande du code de redemarrage (OSExec) : l'ancien jeu va disparaitre, plus de chainage vers ses fonctions */
/* v460 : seulement les fonctions de l'ancien jeu (sous 0x81300000) : celles que le code de redemarrage installe lui-meme
   (son propre pilote du lecteur, Shrek Extra Large : 0x81308984) restent chainees, il en a besoin */
static u32 *const nx_origs[] = { &di_orig, &vi_orig, &si_orig, &ai_orig, &dsp_orig, &pet_orig, &pef_orig };
#if !defined(NATRES_COMPACT)
static MEM_OS __attribute__((noinline)) void nx_unchain(void) {
    if (!exec_on) return;
    for (u32 i = 0; i < 7; i++) if (*nx_origs[i] < 0x81300000u) *nx_origs[i] = 0;
}
#else
static void __attribute__((noinline)) nx_unchain(void) { }   /* (v474 : Pulse compact) */
#endif
/* le programme suivant : son en-tete .dol (sections : position dans l'ISO, adresse finale, taille). Ses
   sections peuvent etre lues ailleurs puis recopiees (apploader recent) : on branche avec l'adresse finale. */
static struct { u32 off, so[18], sa[18], ss[18]; int ok; } xdol;
static u32 rbe(u32 a) { return *(volatile u32 *)a; }
static MEM_OS int nx_dol(u32 dst, u32 off, u32 len) {   /* (v407 : -Os) */
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
static MEM_OS void nx_dol_sections(u32 dst, u32 off, u32 len) {   /* (v462 : -Os) */
    for (int i = 0; i < 7; i++) {
        u32 so = xdol.off + xdol.so[i], ss = xdol.ss[i];   /* (position dans l'ISO = debut du .dol + position dans le .dol) */
        if (!ss || off >= so + ss || off + len <= so) continue;
        u32 a0 = off > so ? off : so, a1 = off + len < so + ss ? off + len : so + ss;
        nx_patch2(dst + (a0 - off), xdol.sa[i] + (a0 - so), a1 - a0);
    }
}
/* v251 : un .dol entier lu d'un coup par le jeu (chargeur maison : Phantasy Star Online lit son vrai programme puis
   le recopie lui-meme a sa place) : ses sections de code sont branchees dans le tampon, avec leur adresse finale. */
static MEM_OS int nx_whole_dol(u32 dst, u32 off, u32 len) {
    if (len < 0x200 || (dst & 3) || dst < 0x80003000u || dst + len > NATRES_AT) return 0;
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
/* v463 : le code de redemarrage (ancien apploader, Shrek Extra Large) relache le reset du vrai lecteur juste avant de lancer
   le nouveau programme (lis rA,0xCC00 ; addi rA,rA,0x3000 ; li rB,7 ; stw rB,0x24(rA) : PI 0xCC003024 = 7). Pulse le garde
   en reset pendant tout le jeu (c'est le faux lecteur qui repond) : relache, il redemarrait et se melait des commandes du
   jeu recharge. 7 devient 3 (bit 2, reset du lecteur, laisse a 0). Journal : exec_n bits 8-15 = ecritures changees. */
static MEM_OS __attribute__((noinline)) void nx_pireset(u32 a, u32 len) {
    for (u32 p = a + 4; p + 8 <= a + len; p += 4) {
        u32 i0 = rbe(p - 4), i1 = rbe(p), i2 = rbe(p + 4), ra = (i0 >> 16) & 31, rb = (i1 >> 21) & 31;
        if ((i0 & 0xFC00FFFFu) == 0x38003000u && ((i0 >> 21) & 31) == ra && (i1 & 0xFC1FFFFFu) == 0x38000007u
            && i2 == (0x90000024u | rb << 21 | ra << 16)) { *(volatile u32 *)p = i1 - 4u; nx_sync(p, 4); H.exec_n += 0x100u; }   /* (journal : exec_n, bits 8-15) */
    }
}
#if !defined(NATRES_COMPACT)
/* v482 : OSExec : l'apploader du disque range bi2 et la FST tout en haut de la memoire, d'apres la taille de la memoire
   (0x80000028 : FST = taille - taille de la FST, bi2 juste dessous). Avec 24 Mo, c'etait dans Pulse : Pulse refusait la
   lecture (Shrek Extra Large : erreur, retour au menu de la console apres l'introduction). La memoire s'arrete donc pour
   lui sous ce que GC Station a reserve (0x800000EC, pose au lancement), comme pour le premier programme. */
static MEM_OS void nx_memtop(void) {
    u32 fl = *(volatile u32 *)0x800000ECu;
    if (fl < 0x81000000u || fl > NATRES_AT) fl = NATRES_AT - 0x20000u;
    *(volatile u32 *)0x80000028u = (fl & ~0x1Fu) - 0x80000000u;
    nr_log_add(0xEC000000u, fl, 0, 0);
}
static MEM_OS void nx_read_done(u32 dst, u32 off, u32 len) {   /* (v407 : -Os : OSExec, rare) */
    EV(0x60, off, len | (u32)exec_on << 31);
    if ((exec_on | exec_a) && off <= 0x444u && off + len >= 0x448u) {   /* v498 : bi2 du disque (0x440) relu par l'apploader pendant le
                                                            redemarrage : sa memoire simulee (0x01800000) depassait la memoire laissee au jeu
                                                            (nx_memtop, sous Pulse) : l'apploader s'arretait sur une erreur (Shrek Extra Large) */
        u32 *sm = (u32 *)(dst + 0x444u - off), m = *(volatile u32 *)0x80000028u;
        if (*sm > m) { *sm = m; cache_flush((u32)sm, 4); }
    }
    if (!exec_on && !exec_a && H.exec_last == 0x81300001u) { exec_on = 1; H.exec_last = 0x81300000u; return; }   /* v529 : (la lecture du menu apres le lanceur .tgc) */
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
            if (!H.reboot_len) { exec_a = 1; nx_memtop(); nr_log_add(0xED000000u, off, len, H.tramp_n); nr_log_flush(); }
            return;
        }
        if (!H.reboot_len || off != (H.reboot_off & ~3u) || dst != 0x81300000u || len < H.reboot_len) return;
        exec_on = !(H.reboot_off & 1u);                  /* v529 : lanceur .tgc (bit 0) : le menu fait encore une lecture (la FST du jeu choisi)
                                                            et attend sa fin : OSExec commence a la fin de celle-ci (plus bas) */ H.exec_last = 0x81300000u | (H.reboot_off & 1u); xdol.ok = 0;   /* ancien apploader : code de redemarrage en 0x81300000 */
        nx_memtop();                                      /* v482 */
        /* v459 : le chainage vers le jeu reste en place ici : l'ancien jeu attend encore l'interruption de fin de CETTE lecture
           (Shrek Extra Large : boucle sur DVDGetCommandBlockStatus, fige). Coupe a la 1re commande du code de redemarrage. */
        nr_log_add(0xEE000000u, off, len, H.tramp_n);
        nx_pireset(dst, len);                             /* v463 */
        nx_patch(dst, len);
        nr_log_flush();
        return;
    }
    nx_dol(dst, off, len);                                /* en-tete du programme suivant (v252 : et la suite de cette lecture) */
    if (xdol.ok) { nx_dol_sections(dst, off, len); return; }
    if (exec_on) nx_patch(dst & ~3u, len);                /* (ancien apploader : ce qu'il charge arrive a sa place) */
}
#else
static void nx_read_done(u32 dst, u32 off, u32 len) { (void)dst; (void)off; (void)len; }   /* (v474 : Pulse compact) */
#endif
/* ======================= v121 : jeux a 2 disques =======================
   Pour changer de disque, la bibliotheque du jeu arrete le moteur (commande E3), active l'interruption du
   couvercle (DICVR, masque) et attend qu'on ouvre puis referme le couvercle. Ici : une fois le masque pose
   (et au moins une seconde apres E3), la table des morceaux de l'autre disque prend la place de celle-ci, et
   le jeu recoit l'interruption "couvercle referme" (CVRINT) : il relit l'ID du disque et trouve l'autre. */
static u32 sw_pending = 0, sw_vi = 0;
static MEM_OS void disc_swap(void) {
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
static MEM_OS void sw_tick(void) {   /* (v495 : -Os, place) */
    if (!sw_pending || !(di[1] & 2u) || rd_on || H.vi_count - sw_vi < 60u) return;
    if (DI_REAL[7] & 1) return;                           /* (le vrai lecteur doit pouvoir reveiller le jeu) */
    sw_pending = 0;
    disc_swap();
}
static MEM_OS void di_exec(void) {                     /* (v437 : -Os, place ; une fois par commande du jeu) */
    u32 info = 0;
    int ex0 = exec_on;                                   /* v497 : redemarrage pas encore commence avant cette commande */
    rd_on = 0;                                           /* (nouvelle commande : une lecture en fond oubliee s'arrete) */
    if (xv_st) xv_drain();                               /* v441 : (sa suite par interruptions aussi : finie, puis pas comptee */
    xv_nsec = 0;                                         /*  dans la lecture suivante) */
    if (!di_ready) { di_ready = nr_init() == 0; if (!di_ready) info |= 8; }   /* 1er acces du jeu : on demarre la carte SD */
    hooks_check();
    u32 cmd = di[2] >> 24, len = di[6], dst = di[5] | 0x80000000u;
    H.ncmd++; H.last_cmd = di[2]; H.last_off = di[3] << 2; H.last_len = len;
    EV(0x20, di[2], di[3] << 2); EV(0x21, len, dst);
    nx_unchain();                                        /* v459 */
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
        #if defined(NATRES_COMPACT)
        if (busy) { for (u32 w = 0; w < 40500000u && exi_defer(); w++) ; }   /* v474 : compact : bus pris par le jeu : attendre (au plus ~1 s) puis lire */
        if (0) {
#else
        /* v495 : reveil provoque et le jeu a tout coupe sauf le lecteur (redemarrage du SDK, Shrek Extra Large) : les
           interruptions qui font avancer la lecture en fond (image, son, manettes) n'arrivent plus : lire d'un coup */
        int lone = SELFIRQ_ON && (OS_INT_MASKS | 0x51Fu) == 0xFFFFFFFFu;
        if ((len > RD_CHUNK || (slow && len) || busy) && di_orig && !exec_on && !lone && (!(H.opt & NRO_SYNC) || busy)) {
#endif   /* (v473 : NRO_SYNC) */   /* v61 : grosse lecture : en fond */
            if (slow) {                                  /* (deplacement de la tete si loin, puis ~3 Mo/s) */
                u32 d = off > sp_next ? off - sp_next : sp_next - off;
                sp_until = tbl() + (d > 0x100000u ? TB_SEC / 1000u * 60u : TB_SEC / 1000u * 1u) + (len / 3000u) * (TB_SEC / 1000u);
                sp_kicks = 0; sp_next = off + len;
            }
            rd_dst = dst; rd_off = off; rd_left = len; rd_on = 1; rd_vi = H.vi_count;
            rd_dst0 = dst; rd_off0 = off; rd_len0 = len; PF_LD0();
            H.n_async++;
            info |= di_real_kick() | 0x10;                 /* (le lecteur reste "occupe" : DICR bit 0) */
            nr_log(off, len, info | (di[0] & 0xFF) << 8);
            EV(0x23, info, len);
            return;
        }
        if (len && nr_read((void *)dst, off, len)) {
#if defined(NATRES_LITE)
            if (SELFIRQ_ON) sf_bits = 0x04; else
#endif
            di[0] |= 0x04;
            info |= 1; goto done; }   /* DEINT : erreur (v477 : avec l'interruption) */
        cache_flush(dst, len);
        di[5] += len; di[6] = 0;
        EVSUM(off, len, dst);
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
#if defined(NATRES_LITE)
    if (SELFIRQ_ON) sf_bits = 0x10; else                 /* (v477 : avec l'interruption provoquee) */
#endif
    di[0] |= 0x10;                                       /* TCINT : termine */
done:
    if (!sf_bits) di[7] &= ~1u;                          /* (v477 : sinon, encore occupe jusqu'a l'interruption) */
    if (!exec_on || !ex0 || di_orig - 0x81300000u < H.reboot_len) {   /* (v497 : !ex0 : la lecture du code de redemarrage elle-meme,
                                                            faite d'un coup (v495) : le jeu attend encore sa fin, Shrek) */ di_game_pending = 1; dp_vi = H.vi_count; info |= di_real_kick(); }   /* (v95 : pendant OSExec,
                                                            personne n'attend d'interruption ; v465 : sauf le code de redemarrage qui a installe sa propre
                                                            fonction (Shrek Extra Large, 0x81308984) : il attend la fin de chaque commande) */
    nr_log(H.last_off, H.last_len, info | (di[0] & 0xFF) << 8);
    EV(0x22, di[0], info);
    if (exec_on) EVF();
}

static u32 hk_n = 0;
/* v166 : jeu fige avant sa 1re interruption (Crash Bandicoot : aucune trace) : tant qu'aucune fonction d'image ou de
   manettes n'a tourne, chaque acces aiguille est compte et, 3 s apres le 1er, le journal est ecrit (puis toutes les
   3 s) avec le dernier registre touche. Rien ne change pour un jeu qui a deja ses interruptions (tous les autres). */
static u32 en_n = 0, en_t0 = 0;
static void early_note(u32 a, u32 v, u32 st) {
    if (H.vi_in || !H.log_sec) return;                   /* v403 : aussi apres des manettes (SI) : Rayman 3 en lit 3 puis se fige */
    en_n++;
    u32 t = tbl();
    if (!en_t0) { en_t0 = t; return; }
    if (t - en_t0 < TB_SEC * 3) return;
    en_t0 = t;
    nr_log_add(0xEE000000u | (st << 23) | (en_n & 0x7FFFFFu), a, v, H.ncmd);
    nr_log_flush();
}
static void hooks_lazy(void) { trap_guard(); if (H.trap_state == NRT_OFF && (H.opt & NRO_TRAP)) trap_check(); if (++hk_n <= 64 || (hk_n & 255) == 1) hooks_check(); }   /* v164 : (les 64 premiers acces : le jeu peut se figer tot) */   /* v62 : le jeu installe ses fonctions apres sa 1re commande */
static MEM_OS u32 di_load(u32 reg) {
    u32 i = (reg & 0x3F) >> 2;
    hooks_lazy();
    early_note(reg, 0, 0);
#if defined(NATRES_LITE)
    static u32 sf_n = 0;
    if (sf_bits && (i == 0 || i == 7) && (tbl() - kick_tb > 12150u || ++sf_n > 400u)) { sf_n = 0; sf_apply(); }   /* v477 : ~300 us apres (ou 400 relectures : horloge arretee du banc) : fini meme sans interruption */
#endif
    u32 v = i == 1 ? di[1] & 0x07 : i == 9 ? DI_REAL[9] : i < 10 ? di[i] : 0;   /* couvercle ferme (v121 : CVRINT apres un changement
                                                            de disque) ; DICFG : celui du vrai lecteur */
    EV(0x29, reg, v);
    return v;
}

static MEM_OS void di_store(u32 reg, u32 val) {           /* (v407 : -Os) */
#if defined(NATRES_LITE) && !defined(NATRES_COMPACT)
    if ((reg & 0xFF00u) == 0x7F00u) { os_hook(reg & 0xFFu, val); return; }   /* v480 : aiguillage de OSReport / OSPanic */
#endif
    u32 i = (reg & 0x3F) >> 2;
    hooks_lazy();
    EV(0x28, reg, val);
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

/* v397 : bandeau des succes sur la tele (voir bandeau.h). Ecrit pour GC Station.
   v398 : coins arrondis, texte plus grand (12 x 24), arrivee et depart en glissant.
   Compile a part, sans bibliotheque, sans donnees (ni variables globales, ni tables constantes, ni sauts par table) :
   seulement des branchements relatifs, pour pouvoir etre pose a n'importe quelle adresse. Appele dans l'interruption de
   l'image (par Pulse, apres le jeu) : rien en flottant, peu de pile.

   Ou dessiner : l'image affichee en ce moment est lue dans les registres de l'image (VI) :
     0x00 VTR : lignes actives par trame (bits 4-13)        0x1C TFBL / 0x24 BFBL : debut des trames haute et basse
     0x48 : largeur d'une ligne (bits 8-15, par 16 pixels) et pas entre deux lignes d'une trame (bits 0-7, par 32 octets)
   L'image est en YUYV (2 pixels par mot : Y0 Cb Y1 Cr). Elle est ecrite sans passer par le cache (0xC0000000 + adresse).
   Image entrelacee rangee en entier (trame basse = une ligne plus loin) : 2 x lignes par trame, pas d'une ligne entiere ;
   sinon une seule trame (480p) ; trame seule de 240 lignes : une ligne du bandeau sur deux. */
#include "bandeau.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef BAN_PTR                                           /* (essai sur PC : bandeau_essai.c les remplace) */
#define BAN_PTR(a) ((u8 *)(a))
#define BAN_IMG(a) ((volatile u32 *)(0xC0000000u | (a)))
#endif
#define M(d, o) (*(volatile u32 *)((d) + (o)))

void ban_run(u8 *d) __attribute__((section(".text.ban_entree")));

static u32 ban_adr(u32 v) {                               /* registre TFBL / BFBL -> adresse physique */
    u32 a = v & 0x00FFFFFFu;
    return (v & 0x10000000u) ? a << 5 : a;
}

static u32 ban_long(const u8 *t, u32 max) {
    u32 n = 0;
    while (n < max && t[n]) n++;
    return n;
}

static void ban_copie(u8 *o, const u8 *s, u32 n) {
    for (u32 i = 0; i < n; i++) o[i] = s[i];
}

/* une ligne (gy, 0 a BAN_LH - 1) d'un texte dans le rang, a partir de x, couleur c (ne depasse pas fin) */
static void ban_texte(u8 *rang, u32 x, u32 fin, const u8 *t, u32 n, const u16 *pol, u32 gy, u8 c) {
    for (u32 k = 0; k < n; k++, x += BAN_LW) {
        u32 g = t[k];
        if (g >= 157) g = '?';
        u32 b = pol[g * BAN_LH + gy];
        if (!b) continue;
        for (u32 i = 0; i < BAN_LW; i++) if ((b & (0x8000u >> i)) && x + i < fin) rang[x + i] = c;
    }
}

/* v398 : coin arrondi. dx, dy : distance au bord (0 = pixel du bord), en pixels. Rend BAN_VIDE (le jeu reste visible),
   1 (bord rose) ou 0 (fond). Calcul au centre du pixel, en demi-pixels : (2R - 2dx - 1)^2 + (2R - 2dy - 1)^2. */
static u32 ban_coin(u32 dx, u32 dy) {
    int a = 2 * BAN_RAYON - 2 * (int)dx - 1, b = 2 * BAN_RAYON - 2 * (int)dy - 1;
    u32 q = (u32)(a * a + b * b);
    if (q > 4u * BAN_RAYON * BAN_RAYON) return BAN_VIDE;
    if (q > 4u * (BAN_RAYON - BAN_BORD) * (BAN_RAYON - BAN_BORD)) return 1;
    return 0;
}

static void ban_dessin(u8 *d) {
    volatile u16 *v16 = (volatile u16 *)BAN_PTR(M(d, BAN_O_VI));
    volatile u32 *v32 = (volatile u32 *)v16;
    u32 acv = (v16[0] >> 4) & 0x3FFu;
    u32 ta = ban_adr(v32[0x1C / 4]), ba = ban_adr(v32[0x24 / 4]);
    u32 pc = v16[0x48 / 2], wpl = (pc >> 8) & 0xFFu, std = pc & 0xFFu;
    if (!acv || !wpl || !std || !ta || ta >= 0x01800000u) return;
    u32 large = wpl * 16;
    if (large < 320 || large > 720) return;
    int entrel = ba == ta + wpl * 32 && std == 2 * wpl;
    u32 lignes = entrel ? acv * 2 : acv, pas = entrel ? wpl * 32 : std * 32;
    int demi = !entrel && acv <= 300;
    u32 type = M(d, BAN_O_TYPE);
    if (type < 1 || type > 4) type = 1;
    const u8 *l1 = type == 4 ? d + BAN_O_ETIQ4 : d + BAN_O_ETIQ + (type - 1) * 32, *l2 = d + BAN_O_LIGNE;
    u8 tro = type == 4 ? (u8)M(d, BAN_O_ICONE) : 1;            /* (v399 : l'icone : trophee, ou celle de la notification) */
    const u8 *img = tro >= 16 ? d + BAN_O_IMG + (tro - 16) * BAN_IMG_T : 0;   /* (v401 : l'image de l'application) */
    u32 n1 = ban_long(l1, 31), n2 = ban_long(l2, BAN_TXT - 1);
    u32 nt = n1 > n2 ? n1 : n2;
    u32 bw = BAN_TX + nt * BAN_LW + 14;                   /* (trophee, marge, texte, marge) */
    if (bw > large - 24) bw = large - 24;
    bw &= ~1u;
    u32 bh = BAN_HAUT;
    u32 hh = demi ? bh / 2 : bh;
    if (lignes < hh + 40) return;
    u32 marge = lignes / 16;
    u32 x0 = ((large - bw) / 2) & ~1u;
    /* v400 : en haut, au milieu ; arrivee (descend du haut, freine a la fin) et depart (remonte, de plus en plus vite).
       v398-v399 : en bas. haut : premiere ligne de l'image ou il est dessine (negatif : encore au-dessus de l'image). */
    u32 vu = BAN_DUREE - 1 - M(d, BAN_O_RESTE), reste = M(d, BAN_O_RESTE), course = hh + marge, k = 0, kn = 1;
    if (vu < BAN_ENTREE) { k = BAN_ENTREE - vu; kn = BAN_ENTREE; }
    else if (reste < BAN_SORTIE) { k = BAN_SORTIE - reste; kn = BAN_SORTIE; }
    int glisse = (int)(course * k * k / (kn * kn));
    int haut = M(d, BAN_O_BAS) ? (int)(lignes - hh - marge) + glisse     /* (v400 : reglage « en bas » : comme la v398) */
                               : (int)marge - glisse;
    const u16 *pol = (const u16 *)BAN_PTR(M(d, BAN_O_POLICE));
    const u8 *cl = d + BAN_O_COUL;
    u8 *rang = d + BAN_O_RANG;
    for (u32 oy = 0; oy < hh; oy++) {
        if (haut + (int)oy < 0) continue;                    /* (encore au-dessus de l'image) */
        u32 y = (u32)(haut + (int)oy);
        if (y >= lignes) break;
        u32 gy = demi ? oy * 2 : oy;
        for (u32 x = 0; x < bw; x++) rang[x] = 0;
        if (gy < BAN_BORD) { for (u32 x = 0; x < bw; x++) rang[x] = 1; }                /* (bord rose en haut et en bas) */
        else if (gy >= bh - BAN_BORD) { for (u32 x = 0; x < bw; x++) rang[x] = 1; }
        else {
            if (gy >= BAN_L1 && gy < BAN_L1 + BAN_LH) {
                if (!img) ban_texte(rang, 14, bw, &tro, 1, pol, gy - BAN_L1, 4);   /* (icone : code 1 a 6 de la police) */
                ban_texte(rang, BAN_TX, bw - 10, l1, n1, pol, gy - BAN_L1, 2);
            }
            if (gy >= BAN_L2 && gy < BAN_L2 + BAN_LH) ban_texte(rang, BAN_TX, bw - 10, l2, n2, pol, gy - BAN_L2, 3);
            for (u32 x = 0; x < BAN_BORD; x++) rang[x] = rang[bw - 1 - x] = 1;            /* (bords sur les cotes) */
        }
        if (gy < BAN_RAYON || gy >= bh - BAN_RAYON) {                                    /* (coins arrondis) */
            u32 dy = gy < BAN_RAYON ? gy : bh - 1 - gy;
            for (u32 x = 0; x < BAN_RAYON; x++) rang[x] = rang[bw - 1 - x] = (u8)ban_coin(x, dy);
        }
        volatile u32 *p = BAN_IMG(ta + y * pas + x0 * 2);
        int iy = (int)gy - (BAN_L1 + (BAN_LH - BAN_IMG_W) / 2);       /* (v401 : ligne de l'image de l'application) */
        const u8 *il = img && iy >= 0 && iy < BAN_IMG_W ? img + iy * BAN_IMG_W * 2 : 0;
        for (u32 x = 0; x < bw; x += 2) {
            u32 a = rang[x], b = rang[x + 1];
            if (a == BAN_VIDE && b == BAN_VIDE) continue;     /* (dehors du coin : l'image du jeu reste) */
            if (il && x >= 12 && x < 12 + BAN_IMG_W) {         /* (v401 : l'image de l'application, telle quelle) */
                const u8 *q = il + (x - 12) * 2;
                p[x / 2] = (u32)q[0] << 24 | (u32)q[1] << 16 | (u32)q[2] << 8 | q[3];
                continue;
            }
            u32 w = p[x / 2];
            u32 ya = a == BAN_VIDE ? w >> 24 : cl[a * 4], yb = b == BAN_VIDE ? (w >> 8) & 0xFFu : cl[b * 4];
            u32 c = a == BAN_VIDE ? b : b == BAN_VIDE ? a : a > b ? a : b;   /* (couleur de la paire : la plus importante) */
            p[x / 2] = ya << 24 | (u32)cl[c * 4 + 1] << 16 | yb << 8 | cl[c * 4 + 2];
        }
    }
}

void ban_run(u8 *d) {
    if (M(d, BAN_O_MAGIC) != BAN_MAGIC) return;
    volatile u32 *ach = M(d, BAN_O_ACH) ? (volatile u32 *)BAN_PTR(M(d, BAN_O_ACH)) : 0;
    u32 n = M(d, BAN_O_NTIT);
    if (ach && (ach[0] != 0x41434831u || ach[1] < n)) ach = 0;   /* ("ACH1", n etats) */
    if (n > 64) n = 64;
    if (!M(d, BAN_O_INIT)) {                              /* (1re image : deja obtenus = deja vus) */
        if (ach) for (u32 i = 0; i < n; i++) if (ach[2 + 2 * i] == 2) M(d, i < 32 ? BAN_O_VUS0 : BAN_O_VUS1) |= 1u << (i & 31);
        M(d, BAN_O_RAVU) = M(d, BAN_O_RASEQ);
        M(d, BAN_O_INIT) = 1;
        return;
    }
    u32 tour = ++M(d, BAN_O_TOUR);
    if (!M(d, BAN_O_RESTE)) {
        if (M(d, BAN_O_RASEQ) != M(d, BAN_O_RAVU)) {      /* texte du telephone */
            M(d, BAN_O_RAVU) = M(d, BAN_O_RASEQ);
            ban_copie(d + BAN_O_LIGNE, d + BAN_O_RATXT, BAN_TXT);
            d[BAN_O_LIGNE + BAN_TXT - 1] = 0;
            u32 rt = M(d, BAN_O_RATYPE);
            M(d, BAN_O_TYPE) = rt == 3 ? 3 : rt == 4 ? 4 : 2;
            if (rt == 4) {                                   /* v399 : notification : etiquette et icone du telephone */
                ban_copie(d + BAN_O_ETIQ4, d + BAN_O_RAETIQ, 32);
                d[BAN_O_ETIQ4 + 31] = 0;
                u32 ic = M(d, BAN_O_RAICONE);
                M(d, BAN_O_ICONE) = (ic >= 1 && ic <= 6) || (ic >= 16 && ic < 16 + BAN_IMG_N) ? ic : 6;
            }
            M(d, BAN_O_RESTE) = BAN_DUREE;
        } else if (ach && (tour & 7) == 0) {              /* succes de GC Station (toutes les 8 images) */
            const u8 *tit = BAN_PTR(M(d, BAN_O_TITRES));
            for (u32 i = 0; i < n; i++) {
                u32 o = i < 32 ? BAN_O_VUS0 : BAN_O_VUS1, b = 1u << (i & 31);
                if (ach[2 + 2 * i] != 2 || (M(d, o) & b)) continue;
                M(d, o) |= b;
                ban_copie(d + BAN_O_LIGNE, tit + i * BAN_TITRE, BAN_TITRE);
                d[BAN_O_LIGNE + BAN_TITRE - 1] = 0;
                M(d, BAN_O_TYPE) = 1;
                M(d, BAN_O_RESTE) = BAN_DUREE;
                break;
            }
        }
        if (!M(d, BAN_O_RESTE)) return;
    }
    M(d, BAN_O_RESTE)--;
    ban_dessin(d);
}

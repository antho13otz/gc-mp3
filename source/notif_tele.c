/* v399 : notifications du telephone dans le menu de GC Station (inclus dans main.c, apres ui_extra.c).
   L'appli envoie /api/notif?e=<etiquette>&m=<texte>&i=<icone> (tab_phone.c) quand aucun jeu ne tourne ; pendant un jeu,
   elle ecrit dans le bandeau de Pulse (natres/bandeau.c, type 4). Une carte en verre descend du haut de l'ecran avec un
   ressort, reste ~5 s, puis remonte ; les suivantes attendent leur tour (4 au plus). Icones : celles de la police du
   bandeau (1 trophee, 2 message, 3 appel, 4 alarme, 5 ami, 6 info), dessinees point par point. */
#include "natres/bandeau_police.h"

static void ntf_son_jouer(void);
#define NTF_MAX 4
#define NTF_DUREE 5200u
#define NTF_PX 24                                         /* v401 : icone de l'application : 24 x 24, RGBA 4444 */
typedef struct { char e[48], m[160]; int ic, img; unsigned short px[NTF_PX * NTF_PX]; } ntf_t;
static ntf_t ntf_file[NTF_MAX];
static int ntf_n = 0;
static u64 ntf_t0 = 0;                                     /* (debut de la carte affichee ; 0 : aucune) */

/* une notification de plus (fil principal) */
static void ntf_ajouter(const char *e, const char *m, int ic, const unsigned short *px) {
    if (!m || !m[0]) return;
    if (ntf_n == NTF_MAX) { memmove(ntf_file + 1, ntf_file + 2, sizeof(ntf_t) * (NTF_MAX - 2)); ntf_n--; }   /* (pleine : la plus ancienne qui attend saute) */
    ntf_t *n = &ntf_file[ntf_n++];
    snprintf(n->e, sizeof(n->e), "%s", e && e[0] ? e : "Notification");
    snprintf(n->m, sizeof(n->m), "%s", m);
    n->ic = ic >= 1 && ic <= 6 ? ic : 6;
    n->img = px != NULL;                                  /* (v401 : la vraie icone de l'application, envoyee par l'appli) */
    if (px) memcpy(n->px, px, sizeof(n->px));
    if (ntf_n == 1) { ntf_t0 = now_ms(); if (ntf_son) ntf_son_jouer(); }   /* (v400 : le carillon quand elle descend) */
    last_input_ms = now_ms();                             /* (reveille l'ecran de veille) */
}

static f32 ntf_ressort(f32 x) {                            /* (arrivee : depasse un peu puis se pose) */
    if (x >= 1) return 1;
    f32 c1 = 1.70158f, c3 = c1 + 1;
    f32 y = x - 1;
    return 1 + c3 * y * y * y + c1 * y * y;
}

/* l'icone : un rond de couleur et le dessin de la police du bandeau (12 x 24), en blanc */
static void ntf_icone(f32 cx, f32 cy, int ic, u8 a) {
    static const u8 coul[7][3] = { { 0, 0, 0 }, { 230, 170, 40 }, { 70, 150, 255 }, { 60, 200, 110 }, { 255, 120, 70 }, { 200, 90, 255 }, { 120, 120, 150 } };
    gx_circle(cx, cy, 17, RGBA(coul[ic][0], coul[ic][1], coul[ic][2], a), RGBA(coul[ic][0] * 3 / 4, coul[ic][1] * 3 / 4, coul[ic][2] * 3 / 4, a));
    const unsigned short *g = ban_police + ic * BAN_LH;
    f32 x0 = cx - 6, y0 = cy - 13;
    for (int y = 0; y < BAN_LH; y++) {
        unsigned short b = g[y];
        if (!b) continue;
        for (int x = 0; x < BAN_LW; x++) {
            if (!(b & (0x8000u >> x))) continue;
            int x1 = x;
            while (x + 1 < BAN_LW && (b & (0x8000u >> (x + 1)))) x++;   /* (une barre par suite de points) */
            gx_rect(x0 + x1, y0 + y, (f32)(x - x1 + 1), 1, RGBA(255, 255, 255, a));
        }
    }
}

/* v401 : l'icone de l'application (24 x 24, RGBA 4444), agrandie a 32 x 32 ; les suites de points de meme couleur d'une
   ligne sont dessinees d'un seul rectangle */
static void ntf_image(f32 cx, f32 cy, const unsigned short *px, u8 a) {
    const f32 k = 32.0f / NTF_PX;
    f32 x0 = cx - 16, y0 = cy - 16;
    for (int y = 0; y < NTF_PX; y++) {
        const unsigned short *l = px + y * NTF_PX;
        for (int x = 0; x < NTF_PX; ) {
            unsigned short c = l[x];
            int x1 = x;
            while (x < NTF_PX && l[x] == c) x++;
            u32 al = (c & 15u) * 17u * a / 255u;
            if (al) gx_rect(x0 + x1 * k, y0 + y * k, (x - x1) * k, k, RGBA((c >> 12) * 17, (c >> 8 & 15) * 17, (c >> 4 & 15) * 17, al));
        }
    }
}

/* coupe le texte a la largeur donnee (avec "...") */
static void ntf_couper(int f, const char *s, int max, char *o, int cap) {
    snprintf(o, cap, "%s", s);
    if (text_width(f, o, 0) <= max) return;
    int n = (int)strlen(o);
    while (n > 0) {
        n--;
        while (n > 0 && (o[n] & 0xC0) == 0x80) n--;      /* (pas au milieu d'une lettre UTF-8) */
        o[n] = 0;
        char t[200];
        snprintf(t, sizeof(t), "%s\xE2\x80\xA6", o);
        if (text_width(f, t, 0) <= max) { snprintf(o, cap, "%s", t); return; }
    }
}

static void ntf_draw(void) {
    if (!ntf_n) return;
    u64 now = now_ms();
    if (!ntf_t0) ntf_t0 = now;
    u32 t = (u32)(now - ntf_t0);
    if (t >= NTF_DUREE) {                                   /* (la suivante) */
        memmove(ntf_file, ntf_file + 1, sizeof(ntf_t) * (NTF_MAX - 1));
        ntf_n--;
        ntf_t0 = ntf_n ? now : 0;
        if (ntf_n && ntf_son) ntf_son_jouer();
        return;
    }
    ntf_t *n = &ntf_file[0];
    f32 k = t < 480 ? ntf_ressort(t / 480.0f) : t > NTF_DUREE - 380 ? 1 - (f32)(t - (NTF_DUREE - 380)) / 380.0f : 1;
    if (t > NTF_DUREE - 380) k = k * k;                      /* (remonte de plus en plus vite) */
    char e[64], m[200];
    ntf_couper(F_SMALL, n->e, 430, e, sizeof(e));
    ntf_couper(F_MED, n->m, 430, m, sizeof(m));
    int tw = text_width(F_MED, m, 0), ew = text_width(F_SMALL, e, 0);
    f32 w = (f32)((tw > ew ? tw : ew) + 92);
    if (w < 260) w = 260;
    if (w > 560) w = 560;
    f32 h = 58, x = 320 - w / 2, y = -72 + 92 * k;
    u8 a = (u8)(k < 0 ? 0 : k > 1 ? 255 : 255 * k);
    gx_rrect_fill(x + 2, y + 4, w, h, 22, RGBA(0, 0, 0, a / 3), RGBA(0, 0, 0, a / 3));                  /* (ombre) */
    gx_rrect_fill(x, y, w, h, 22, RGBA(38, 30, 62, a * 235 / 255), RGBA(16, 12, 30, a * 235 / 255));   /* (verre sombre) */
    gx_rrect_fill(x + 1, y + 1, w - 2, h / 2, 21, RGBA(255, 255, 255, a / 12), RGBA(255, 255, 255, 0)); /* (reflet du haut) */
    gx_rrect_stroke(x, y, w, h, 22, 1.0f, RGBA(255, 255, 255, a / 4));
    if (n->img) ntf_image(x + 34, y + h / 2, n->px, a);
    else ntf_icone(x + 34, y + h / 2, n->ic, a);
    gx_text1(F_SMALL, x + 62, y + 24, e, RGBA(140, 210, 255, a));
    gx_text1(F_MED, x + 62, y + 46, m, RGBA(255, 255, 255, a));
    if (ntf_n > 1) {                                       /* (d'autres attendent : petit chiffre) */
        char b[8];
        snprintf(b, sizeof(b), "+%d", ntf_n - 1);
        gx_text_r(F_SMALL, x + w - 14, y + 24, b, RGBA(200, 190, 230, a));
    }
}

/* v400 : petit carillon quand une carte descend (reglage « Son des notifications du telephone ») : deux notes de cloche
   (mi puis la, avec un peu d'harmonique), calculees une fois dans le fil principal (0,45 s a 48 kHz), puis ajoutees par
   dma_cb aux blocs du son qui partent, par-dessus la musique (entiers seulement dans l'interruption). */
#define NTF_SON_N 21600
static s16 *ntf_son_tab = NULL;
static volatile int ntf_son_pos = -1;                     /* (prochain echantillon a jouer ; -1 : rien) */
static void ntf_son_jouer(void) {
    if (!ntf_son_tab) {
        s16 *t = (s16 *)malloc(NTF_SON_N * sizeof(s16));
        if (!t) return;
        for (int i = 0; i < NTF_SON_N; i++) {
            f32 s = i / 48000.0f, v = 0;
            static const f32 f[2] = { 1318.5f, 1760.0f }, d0[2] = { 0.0f, 0.13f };
            for (int n = 0; n < 2; n++) {
                f32 u = s - d0[n];
                if (u < 0) continue;
                f32 env = (u < 0.004f ? u / 0.004f : 1.0f) * expf(-u / 0.11f);
                v += env * (sinf(2 * (f32)M_PI * f[n] * u) + 0.25f * sinf(4 * (f32)M_PI * f[n] * u));
            }
            if (i > NTF_SON_N - 480) v *= (NTF_SON_N - i) / 480.0f;   /* (fin sans clic) */
            t[i] = (s16)(v * 6500.0f);
        }
        ntf_son_tab = t;
    }
    ntf_son_pos = 0;
}
static void ntf_son_mix(s16 *buf) {
    int p = ntf_son_pos;
    if (p < 0 || !ntf_son_tab) return;
    for (int i = 0; i < DMA_SAMPLES && p < NTF_SON_N; i++, p++) {
        int c = ntf_son_tab[p];
        int l = buf[2 * i] + c, r = buf[2 * i + 1] + c;
        buf[2 * i] = (s16)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        buf[2 * i + 1] = (s16)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
    DCFlushRange(buf, DMA_BYTES);
    ntf_son_pos = p >= NTF_SON_N ? -1 : p;
}

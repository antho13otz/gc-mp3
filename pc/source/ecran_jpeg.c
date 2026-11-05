/* ---------- v285 : second ecran : ce qu'affiche la tele, en JPEG, pour l'appli du telephone ----------
   GET /api/ecran?w=320|640&q=10..95 : une image JPEG (baseline, couleurs 4:2:0) faite directement depuis le tampon
   video (YUYV) : pas de conversion en RGB, pas de grosse reservation (on lit le tampon bloc par bloc).
   En-tetes : X-Ecran: <largeur>x<hauteur>, X-Ms: <temps d'encodage en ms>, X-Web: 1 si l'onglet WEB est affiche
   (le doigt sur l'image devient alors le pointeur de Cube Browser).
   Encodeur ecrit pour GC Station : tables de la norme JPEG (ITU T.81, annexe K), DCT a 8 points par la factorisation
   d'Arai, Agui et Nakajima (en flottants : le Gekko a une unite de calcul flottant rapide). */

static const u8 ej_zz[64] = {                     /* ordre en zigzag -> position naturelle */
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
static const u8 ej_qlum[64] = {                   /* (annexe K.1, ordre naturel) */
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
static const u8 ej_qchr[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 };
/* tables de Huffman habituelles (annexe K.3) : nombre de codes par longueur (1 a 16), puis les valeurs */
static const u8 ej_dcl_n[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const u8 ej_dcl_v[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const u8 ej_dcc_n[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const u8 ej_dcc_v[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const u8 ej_acl_n[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const u8 ej_acl_v[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };
static const u8 ej_acc_n[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const u8 ej_acc_v[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };

typedef struct { u16 code[256]; u8 len[256]; } ej_huff;
static ej_huff ej_hdl, ej_hdc, ej_hal, ej_hac;
static int ej_ready = 0;
static u8 ej_ylut[256], ej_clut[256];             /* niveaux video (16-235 / 16-240) -> pleine echelle du JPEG */

static void ej_build(ej_huff *h, const u8 *n, const u8 *v) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < n[l - 1]; i++, k++) { h->code[v[k]] = (u16)code; h->len[v[k]] = (u8)l; code++; }
        code <<= 1;
    }
}
static void ej_init(void) {
    if (ej_ready) return;
    ej_build(&ej_hdl, ej_dcl_n, ej_dcl_v); ej_build(&ej_hdc, ej_dcc_n, ej_dcc_v);
    ej_build(&ej_hal, ej_acl_n, ej_acl_v); ej_build(&ej_hac, ej_acc_n, ej_acc_v);
    for (int i = 0; i < 256; i++) {
        int y = (i - 16) * 255 / 219, c = (i - 128) * 255 / 224 + 128;
        ej_ylut[i] = (u8)(y < 0 ? 0 : y > 255 ? 255 : y);
        ej_clut[i] = (u8)(c < 0 ? 0 : c > 255 ? 255 : c);
    }
    ej_ready = 1;
}

typedef struct { u8 *o; int n, cap, ovf; u32 acc; int nb; } ej_out;
static void ej_byte(ej_out *w, int b) { if (w->n < w->cap) w->o[w->n++] = (u8)b; else w->ovf = 1; }
static void ej_bits(ej_out *w, u32 v, int n) {          /* n bits de v, le plus fort d'abord ; 0xFF suivi de 0x00 */
    w->acc = (w->acc << n) | (v & ((1u << n) - 1));
    w->nb += n;
    while (w->nb >= 8) {
        int b = (int)(w->acc >> (w->nb - 8)) & 255;
        ej_byte(w, b);
        if (b == 255) ej_byte(w, 0);
        w->nb -= 8;
    }
}
static void ej_flush(ej_out *w) { if (w->nb > 0) ej_bits(w, 0x7F, 8 - w->nb); }   /* (complete avec des 1) */

/* DCT 2D sur 8x8 (en place) : lignes puis colonnes ; le facteur d'echelle est dans la table de quantification */
static void ej_dct8(float *d, int st) {
    float t0 = d[0] + d[7 * st], t7 = d[0] - d[7 * st], t1 = d[st] + d[6 * st], t6 = d[st] - d[6 * st];
    float t2 = d[2 * st] + d[5 * st], t5 = d[2 * st] - d[5 * st], t3 = d[3 * st] + d[4 * st], t4 = d[3 * st] - d[4 * st];
    float e0 = t0 + t3, e3 = t0 - t3, e1 = t1 + t2, e2 = t1 - t2;
    d[0] = e0 + e1; d[4 * st] = e0 - e1;
    float m = (e2 + e3) * 0.707106781f;
    d[2 * st] = e3 + m; d[6 * st] = e3 - m;
    float o0 = t4 + t5, o1 = t5 + t6, o2 = t6 + t7;
    float r = (o0 - o2) * 0.382683433f;
    float a = 0.541196100f * o0 + r, b = 1.306562965f * o2 + r, c = o1 * 0.707106781f;
    float p = t7 + c, q = t7 - c;
    d[5 * st] = q + a; d[3 * st] = q - a; d[st] = p + b; d[7 * st] = p - b;
}

static float ej_fl[64], ej_fc[64];                /* 1 / (quantification x echelle de la DCT), ordre naturel */
static u8 ej_ql[64], ej_qc[64];                   /* quantification de l'en-tete (ordre en zigzag) */
static int ej_q_cur = -1;
static void ej_quality(int q) {
    if (q == ej_q_cur) return;
    ej_q_cur = q;
    int sc = q < 50 ? 5000 / q : 200 - 2 * q;
    static const float as[8] = { 1.0f, 1.387039845f, 1.306562965f, 1.175875602f, 1.0f, 0.785694958f, 0.541196100f, 0.275899379f };
    for (int i = 0; i < 64; i++) {
        int l = (ej_qlum[i] * sc + 50) / 100, c = (ej_qchr[i] * sc + 50) / 100;
        l = l < 1 ? 1 : l > 255 ? 255 : l; c = c < 1 ? 1 : c > 255 ? 255 : c;
        float s = as[i >> 3] * as[i & 7] * 8.0f;
        ej_fl[i] = 1.0f / (l * s); ej_fc[i] = 1.0f / (c * s);
    }
    for (int k = 0; k < 64; k++) {
        int l = (ej_qlum[ej_zz[k]] * sc + 50) / 100, c = (ej_qchr[ej_zz[k]] * sc + 50) / 100;
        ej_ql[k] = (u8)(l < 1 ? 1 : l > 255 ? 255 : l); ej_qc[k] = (u8)(c < 1 ? 1 : c > 255 ? 255 : c);
    }
}

static int ej_block(ej_out *w, float *b, const float *fq, int dc, const ej_huff *hd, const ej_huff *ha) {
    for (int i = 0; i < 8; i++) ej_dct8(b + i * 8, 1);
    for (int i = 0; i < 8; i++) ej_dct8(b + i, 8);
    int z[64];
    for (int k = 0; k < 64; k++) { float v = b[ej_zz[k]] * fq[ej_zz[k]]; z[k] = (int)(v < 0 ? v - 0.5f : v + 0.5f); }
    int diff = z[0] - dc, a = diff < 0 ? -diff : diff, nb = 0;
    while (a) { nb++; a >>= 1; }
    ej_bits(w, hd->code[nb], hd->len[nb]);
    if (nb) ej_bits(w, (u32)(diff < 0 ? diff - 1 : diff), nb);
    int last = 63;
    while (last > 0 && !z[last]) last--;
    for (int k = 1, run = 0; k <= last; k++) {
        if (!z[k]) { run++; continue; }
        while (run >= 16) { ej_bits(w, ha->code[0xF0], ha->len[0xF0]); run -= 16; }
        int v = z[k]; a = v < 0 ? -v : v; nb = 0;
        while (a) { nb++; a >>= 1; }
        int s = (run << 4) | nb;
        ej_bits(w, ha->code[s], ha->len[s]);
        ej_bits(w, (u32)(v < 0 ? v - 1 : v), nb);
        run = 0;
    }
    if (last < 63) ej_bits(w, ha->code[0], ha->len[0]);
    return z[0];
}

static void ej_marker(ej_out *w, int m, int len) { ej_byte(w, 0xFF); ej_byte(w, m); ej_byte(w, len >> 8); ej_byte(w, len & 255); }
static void ej_dht(ej_out *w, int cls_id, const u8 *n, const u8 *v, int nv) {
    ej_byte(w, cls_id);
    for (int i = 0; i < 16; i++) ej_byte(w, n[i]);
    for (int i = 0; i < nv; i++) ej_byte(w, v[i]);
}

/* fb : tampon YUYV (sw x sh), lu tous les s pixels (s = 1 ou 2) -> JPEG dans out (cap octets) ; rend la taille, 0 si trop petit */
static int ej_encode(const u8 *fb, int sw, int sh, int s, int quality, u8 *out, int cap) {
    ej_init();
    ej_quality(quality < 10 ? 10 : quality > 95 ? 95 : quality);
    int W = sw / s, H = sh / s;
    ej_out w = { out, 0, cap, 0, 0, 0 };
    ej_byte(&w, 0xFF); ej_byte(&w, 0xD8);
    ej_marker(&w, 0xE0, 16);
    { static const u8 jf[] = { 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0 }; for (unsigned i = 0; i < sizeof(jf); i++) ej_byte(&w, jf[i]); }
    ej_marker(&w, 0xDB, 132);
    ej_byte(&w, 0); for (int k = 0; k < 64; k++) ej_byte(&w, ej_ql[k]);
    ej_byte(&w, 1); for (int k = 0; k < 64; k++) ej_byte(&w, ej_qc[k]);
    ej_marker(&w, 0xC0, 17);
    ej_byte(&w, 8); ej_byte(&w, H >> 8); ej_byte(&w, H & 255); ej_byte(&w, W >> 8); ej_byte(&w, W & 255); ej_byte(&w, 3);
    ej_byte(&w, 1); ej_byte(&w, 0x22); ej_byte(&w, 0);
    ej_byte(&w, 2); ej_byte(&w, 0x11); ej_byte(&w, 1);
    ej_byte(&w, 3); ej_byte(&w, 0x11); ej_byte(&w, 1);
    ej_marker(&w, 0xC4, 2 + 4 * 17 + 12 + 12 + 162 + 162);
    ej_dht(&w, 0x00, ej_dcl_n, ej_dcl_v, 12); ej_dht(&w, 0x10, ej_acl_n, ej_acl_v, 162);
    ej_dht(&w, 0x01, ej_dcc_n, ej_dcc_v, 12); ej_dht(&w, 0x11, ej_acc_n, ej_acc_v, 162);
    ej_marker(&w, 0xDA, 12);
    ej_byte(&w, 3); ej_byte(&w, 1); ej_byte(&w, 0x00); ej_byte(&w, 2); ej_byte(&w, 0x11); ej_byte(&w, 3); ej_byte(&w, 0x11);
    ej_byte(&w, 0); ej_byte(&w, 63); ej_byte(&w, 0);
    int dy = 0, du = 0, dv = 0;
    float yb[4][64], ub[64], vb[64];
    for (int my = 0; my < H; my += 16) {
        for (int mx = 0; mx < W; mx += 16) {
            /* un bloc de 16x16 pixels : 4 blocs de luminance, 1 de chaque couleur (moyenne de 2x2 pixels) */
            for (int y = 0; y < 16; y++) {
                int oy = my + y; if (oy >= H) oy = H - 1;
                for (int x = 0; x < 16; x += 2) {
                    int ox = mx + x; if (ox >= W) ox = (W - 2) & ~1;
                    int ya, yb2, u, v;
                    if (s == 1) {
                        const u8 *p = fb + ((size_t)oy * sw + ox) * 2;
                        ya = ej_ylut[p[0]]; yb2 = ej_ylut[p[2]]; u = p[1]; v = p[3];
                    } else {                           /* (moitie : moyenne de 2x2 pixels) */
                        const u8 *p = fb + ((size_t)(oy * 2) * sw + ox * 2) * 2, *q = p + (size_t)sw * 2;
                        ya = ej_ylut[(p[0] + p[2] + q[0] + q[2] + 2) >> 2]; yb2 = ej_ylut[(p[4] + p[6] + q[4] + q[6] + 2) >> 2];
                        u = (p[1] + p[5] + q[1] + q[5] + 2) >> 2; v = (p[3] + p[7] + q[3] + q[7] + 2) >> 2;
                    }
                    float *bl = yb[(y >> 3) * 2 + (x >> 3)];
                    int bi = (y & 7) * 8 + (x & 7);
                    bl[bi] = ya - 128.0f; bl[bi + 1] = yb2 - 128.0f;
                    int ci = (y >> 1) * 8 + (x >> 1);
                    if (!(y & 1)) { ub[ci] = (float)u; vb[ci] = (float)v; }
                    else { ub[ci] = ej_clut[(int)(ub[ci] + u + 1) >> 1] - 128.0f; vb[ci] = ej_clut[(int)(vb[ci] + v + 1) >> 1] - 128.0f; }
                }
            }
            for (int k = 0; k < 4; k++) dy = ej_block(&w, yb[k], ej_fl, dy, &ej_hdl, &ej_hal);
            du = ej_block(&w, ub, ej_fc, du, &ej_hdc, &ej_hac);
            dv = ej_block(&w, vb, ej_fc, dv, &ej_hdc, &ej_hac);
            if (w.ovf) return 0;
        }
    }
    ej_flush(&w);
    ej_byte(&w, 0xFF); ej_byte(&w, 0xD9);
    return w.ovf ? 0 : w.n;
}

/* ---------- v294 : images partielles (GET /api/ecranz) : seuls les blocs de 16x16 qui ont change ----------
   La console garde ce que le telephone a deja (la reference, bloc par bloc, apres le passage en couleurs JPEG).
   Le telephone donne le numero de la derniere image recue (s=) : si c'est la bonne, la console compare chaque bloc
   et n'envoie que ceux qui ont change, ranges a la suite dans une bande JPEG (40 blocs par ligne) precedee de leur
   place ; sinon (ou si plus de 60 % ont change) l'image entiere. Rien de change : reponse vide (204).
   Un bloc = 384 octets : 256 de luminance (16x16), 64 de bleu, 64 de rouge (8x8). */
#define EJ_MCU 384
static u8 *ej_ref = NULL, *ej_cur = NULL;
static int ej_rw = 0, ej_rh = 0, ej_rok = 0;
static u32 ej_seq = 1;

/* un bloc de 16x16 de l'image (W x H, lue tous les s pixels dans le tampon YUYV sw x sh) */
static void ej_mcu_sample(const u8 *fb, int sw, int s, int W, int H, int mx, int my, u8 *o) {
    int cu[64], cv[64];
    for (int y = 0; y < 16; y++) {
        int oy = my + y; if (oy >= H) oy = H - 1;
        for (int x = 0; x < 16; x += 2) {
            int ox = mx + x; if (ox >= W) ox = (W - 2) & ~1;
            int ya, yb2, u, v;
            if (s == 1) {
                const u8 *p = fb + ((size_t)oy * sw + ox) * 2;
                ya = ej_ylut[p[0]]; yb2 = ej_ylut[p[2]]; u = p[1]; v = p[3];
            } else {
                const u8 *p = fb + ((size_t)(oy * 2) * sw + ox * 2) * 2, *q = p + (size_t)sw * 2;
                ya = ej_ylut[(p[0] + p[2] + q[0] + q[2] + 2) >> 2]; yb2 = ej_ylut[(p[4] + p[6] + q[4] + q[6] + 2) >> 2];
                u = (p[1] + p[5] + q[1] + q[5] + 2) >> 2; v = (p[3] + p[7] + q[3] + q[7] + 2) >> 2;
            }
            o[y * 16 + x] = (u8)ya; o[y * 16 + x + 1] = (u8)yb2;
            int ci = (y >> 1) * 8 + (x >> 1);
            if (!(y & 1)) { cu[ci] = u; cv[ci] = v; }
            else { o[256 + ci] = ej_clut[(cu[ci] + u + 1) >> 1]; o[320 + ci] = ej_clut[(cv[ci] + v + 1) >> 1]; }
        }
    }
}

/* JPEG de n blocs (idx : leur numero dans base, NULL : dans l'ordre), cols blocs par ligne, image W x H */
static int ej_encode_mcus(const u8 *base, const u16 *idx, int n, int cols, int W, int H, int quality, u8 *out, int cap) {
    ej_init();
    ej_quality(quality < 10 ? 10 : quality > 95 ? 95 : quality);
    ej_out w = { out, 0, cap, 0, 0, 0 };
    ej_byte(&w, 0xFF); ej_byte(&w, 0xD8);
    ej_marker(&w, 0xE0, 16);
    { static const u8 jf[] = { 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0 }; for (unsigned i = 0; i < sizeof(jf); i++) ej_byte(&w, jf[i]); }
    ej_marker(&w, 0xDB, 132);
    ej_byte(&w, 0); for (int k = 0; k < 64; k++) ej_byte(&w, ej_ql[k]);
    ej_byte(&w, 1); for (int k = 0; k < 64; k++) ej_byte(&w, ej_qc[k]);
    ej_marker(&w, 0xC0, 17);
    ej_byte(&w, 8); ej_byte(&w, H >> 8); ej_byte(&w, H & 255); ej_byte(&w, W >> 8); ej_byte(&w, W & 255); ej_byte(&w, 3);
    ej_byte(&w, 1); ej_byte(&w, 0x22); ej_byte(&w, 0);
    ej_byte(&w, 2); ej_byte(&w, 0x11); ej_byte(&w, 1);
    ej_byte(&w, 3); ej_byte(&w, 0x11); ej_byte(&w, 1);
    ej_marker(&w, 0xC4, 2 + 4 * 17 + 12 + 12 + 162 + 162);
    ej_dht(&w, 0x00, ej_dcl_n, ej_dcl_v, 12); ej_dht(&w, 0x10, ej_acl_n, ej_acl_v, 162);
    ej_dht(&w, 0x01, ej_dcc_n, ej_dcc_v, 12); ej_dht(&w, 0x11, ej_acc_n, ej_acc_v, 162);
    ej_marker(&w, 0xDA, 12);
    ej_byte(&w, 3); ej_byte(&w, 1); ej_byte(&w, 0x00); ej_byte(&w, 2); ej_byte(&w, 0x11); ej_byte(&w, 3); ej_byte(&w, 0x11);
    ej_byte(&w, 0); ej_byte(&w, 63); ej_byte(&w, 0);
    int dy = 0, du = 0, dv = 0;
    float yb[4][64], ub[64], vb[64];
    int total = cols * ((H + 15) / 16);
    for (int k = 0; k < total; k++) {
        int kk = k < n ? k : n - 1;                              /* (fin de la derniere ligne : le dernier bloc, repete) */
        const u8 *m = base + (size_t)(idx ? idx[kk] : kk) * EJ_MCU;
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) yb[(y >> 3) * 2 + (x >> 3)][(y & 7) * 8 + (x & 7)] = m[y * 16 + x] - 128.0f;
        for (int i = 0; i < 64; i++) { ub[i] = m[256 + i] - 128.0f; vb[i] = m[320 + i] - 128.0f; }
        for (int b = 0; b < 4; b++) dy = ej_block(&w, yb[b], ej_fl, dy, &ej_hdl, &ej_hal);
        du = ej_block(&w, ub, ej_fc, du, &ej_hdc, &ej_hac);
        dv = ej_block(&w, vb, ej_fc, dv, &ej_hdc, &ej_hac);
        if (w.ovf) return 0;
    }
    ej_flush(&w);
    ej_byte(&w, 0xFF); ej_byte(&w, 0xD9);
    return w.ovf ? 0 : w.n;
}

/* l'image (ou ses blocs changes) dans out : rend la taille, *plein (1 : image entiere), *nb (blocs), 0 : rien de change ;
   -1 : impossible (memoire, trop gros). Les places des blocs (2 octets : colonne, ligne) sont au debut de out. */
static int ej_partiel(const u8 *fb, int sw, int sh, int s, int quality, u32 seq_client, u8 *out, int cap, int *plein, int *nb) {
    ej_init();
    int W = sw / s, H = sh / s, cols = (W + 15) / 16, rows = (H + 15) / 16, total = cols * rows;
    if (!ej_ref || ej_rw != W || ej_rh != H) {
        free(ej_ref); free(ej_cur);
        ej_ref = (u8 *)malloc((size_t)total * EJ_MCU); ej_cur = (u8 *)malloc((size_t)total * EJ_MCU);
        if (!ej_ref || !ej_cur) { free(ej_ref); free(ej_cur); ej_ref = ej_cur = NULL; ej_rw = ej_rh = 0; return -1; }
        ej_rw = W; ej_rh = H; ej_rok = 0;
    }
    static u16 idx[40 * 36];
    int n = 0;
    for (int by = 0; by < rows; by++) for (int bx = 0; bx < cols; bx++) {
        int k = by * cols + bx;
        u8 *c = ej_cur + (size_t)k * EJ_MCU, *r = ej_ref + (size_t)k * EJ_MCU;
        ej_mcu_sample(fb, sw, s, W, H, bx * 16, by * 16, c);
        if (ej_rok && seq_client == ej_seq) {
            int d = 0;
            for (int i = 0; i < EJ_MCU; i++) { int e = c[i] - r[i]; d += e < 0 ? -e : e; }
            if (d > 300 && n < (int)(sizeof(idx) / sizeof(idx[0]))) idx[n++] = (u16)k;
        }
    }
    int full = !ej_rok || seq_client != ej_seq || n * 10 > total * 6;
    if (!full && n == 0) { *plein = 0; *nb = 0; return 0; }
    int len;
    if (full) {
        len = ej_encode_mcus(ej_cur, NULL, total, cols, W, H, quality, out, cap);
        if (!len) len = ej_encode_mcus(ej_cur, NULL, total, cols, W, H, quality / 2, out, cap);
        if (!len) return -1;
        memcpy(ej_ref, ej_cur, (size_t)total * EJ_MCU);
        n = total;
    } else {
        int sc = n < 40 ? n : 40, sr = (n + sc - 1) / sc, hd = n * 2;
        for (int i = 0; i < n; i++) { out[i * 2] = (u8)(idx[i] % cols); out[i * 2 + 1] = (u8)(idx[i] / cols); }
        len = ej_encode_mcus(ej_cur, idx, n, sc, sc * 16, sr * 16, quality, out + hd, cap - hd);
        if (!len) return -1;
        len += hd;
        for (int i = 0; i < n; i++) memcpy(ej_ref + (size_t)idx[i] * EJ_MCU, ej_cur + (size_t)idx[i] * EJ_MCU, EJ_MCU);   /* (le telephone a ces blocs) */
    }
    ej_rok = 1;
    ej_seq++;
    *plein = full; *nb = n;
    return len;
}

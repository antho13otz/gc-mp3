// GC Station v152 : compresseur deflate / zlib (ecrit pour GC Station, format public RFC 1950 / 1951).
// Sert a recompresser le programme de Star Wars Rogue Squadron III (natif.c) apres l'avoir corrige :
// il doit tenir dans la place de l'original. Recherche des repetitions par chaines de hachage (longues),
// choix differe d'une position (« lazy »), blocs avec codes de Huffman calcules pour chacun (longueurs
// limitees a 15 bits), puis l'en-tete zlib et la somme Adler-32.
// df_zlib(src, n, dst, cap) -> taille ecrite, 0 si ca ne tient pas dans cap.

typedef struct { u8 *o; u32 cap, n; u32 bits; int nb; int over; } df_out;
static void df_put(df_out *w, u32 v, int n) {           /* n bits, poids faibles d'abord */
    w->bits |= v << w->nb; w->nb += n;
    while (w->nb >= 8) { if (w->n < w->cap) w->o[w->n++] = (u8)w->bits; else w->over = 1; w->bits >>= 8; w->nb -= 8; }
}
static void df_flush(df_out *w) { if (w->nb) df_put(w, 0, 8 - w->nb); }
static u32 df_rev(u32 c, int n) { u32 r = 0; while (n--) { r = (r << 1) | (c & 1); c >>= 1; } return r; }
/* longueurs de codes (au plus maxb bits) a partir des frequences : Huffman (paquets successifs) */
static void df_lens(const u32 *freq, int n, int maxb, u8 *len) {
    static u32 w[320]; static int idx[320], par[640];
    int m = 0;
    for (int i = 0; i < n; i++) { len[i] = 0; if (freq[i]) { idx[m] = i; w[m] = freq[i]; m++; } }
    if (m == 0) return;
    if (m == 1) { len[idx[0]] = 1; return; }
    for (int scale = 0; ; scale++) {                     /* (trop long : frequences aplaties, et on recommence) */
        /* noeuds : feuilles 0..m-1, internes m..2m-2 ; deux plus petits a chaque fois (simple : n petit) */
        static u32 nw[640]; static u8 used[640];
        int nn = m;
        for (int i = 0; i < m; i++) { u32 f = w[i] >> scale; nw[i] = f ? f : 1; used[i] = 0; par[i] = -1; }
        for (int k = 0; k < m - 1; k++) {
            int a = -1, b = -1;
            for (int i = 0; i < nn; i++) if (!used[i]) { if (a < 0 || nw[i] < nw[a]) { b = a; a = i; } else if (b < 0 || nw[i] < nw[b]) b = i; }
            used[a] = used[b] = 1; nw[nn] = nw[a] + nw[b]; used[nn] = 0; par[nn] = -1; par[a] = par[b] = nn; nn++;
        }
        int ok = 1;
        for (int i = 0; i < m; i++) { int d = 0; for (int p = i; par[p] >= 0; p = par[p]) d++; if (d > maxb) ok = 0; len[idx[i]] = (u8)d; }
        if (ok) return;
    }
}
static void df_codes(const u8 *len, int n, u16 *code) {
    u16 cnt[16] = { 0 }, next[16];
    for (int i = 0; i < n; i++) cnt[len[i]]++;
    cnt[0] = 0;
    u16 c = 0;
    for (int b = 1; b < 16; b++) { c = (u16)((c + cnt[b - 1]) << 1); next[b] = c; }
    for (int i = 0; i < n; i++) code[i] = len[i] ? (u16)df_rev(next[len[i]]++, len[i]) : 0;
}
static const u16 df_lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const u8  df_lext[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const u16 df_dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const u8  df_dext[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static int df_lsym(int l) { int i = 28; while (df_lbase[i] > l) i--; return i; }
static int df_dsym(int d) { int i = 29; while (df_dbase[i] > d) i--; return i; }
/* symboles d'un bloc : litteral (< 256), ou longueur << 16 | distance (longueur >= 3) */
#define DF_BLK 32768
static u32 df_sym[DF_BLK + 8];
static void df_block(df_out *w, const u32 *sym, int ns, int last) {
    static u32 lf[288], dfq[30], cf[19];
    static u8 ll[288], dl[30], cl[19];
    static u16 lc[288], dc[30], cc[19];
    memset(lf, 0, sizeof(lf)); memset(dfq, 0, sizeof(dfq)); memset(cf, 0, sizeof(cf));
    for (int i = 0; i < ns; i++) {
        u32 s = sym[i];
        if (s < 256) lf[s]++;
        else { lf[257 + df_lsym(s >> 16)]++; dfq[df_dsym(s & 0xFFFF)]++; }
    }
    lf[256] = 1;
    df_lens(lf, 286, 15, ll); df_lens(dfq, 30, 15, dl);
    int nl = 286, nd = 30;
    while (nl > 257 && !ll[nl - 1]) nl--;
    while (nd > 1 && !dl[nd - 1]) nd--;
    if (!dl[0] && nd == 1) dl[0] = 1;                   /* (au moins un code de distance) */
    /* longueurs des deux tables, compressees (16, 17, 18 : repetitions) */
    static u8 all[320]; static u16 rl[320];
    int na = 0, nr = 0;
    for (int i = 0; i < nl; i++) all[na++] = ll[i];
    for (int i = 0; i < nd; i++) all[na++] = dl[i];
    for (int i = 0; i < na; ) {
        int v = all[i], r = 1;
        while (i + r < na && all[i + r] == v) r++;
        if (v == 0 && r >= 3) { int k = r > 138 ? 138 : r; rl[nr++] = (u16)(k <= 10 ? 17 | (k - 3) << 8 : 18 | (k - 11) << 8); cf[k <= 10 ? 17 : 18]++; i += k; continue; }
        if (v && r >= 4) { rl[nr++] = (u16)v; cf[v]++; int k = r - 1 > 6 ? 6 : r - 1; rl[nr++] = (u16)(16 | (k - 3) << 8); cf[16]++; i += 1 + k; continue; }
        rl[nr++] = (u16)v; cf[v]++; i++;
    }
    df_lens(cf, 19, 7, cl);
    static const u8 ord[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    int nc = 19;
    while (nc > 4 && !cl[ord[nc - 1]]) nc--;
    df_codes(ll, 288, lc); df_codes(dl, 30, dc); df_codes(cl, 19, cc);
    df_put(w, last ? 1 : 0, 1); df_put(w, 2, 2);         /* bloc a codes calcules */
    df_put(w, (u32)(nl - 257), 5); df_put(w, (u32)(nd - 1), 5); df_put(w, (u32)(nc - 4), 4);
    for (int i = 0; i < nc; i++) df_put(w, cl[ord[i]], 3);
    for (int i = 0; i < nr; i++) {
        int s = rl[i] & 0xFF, e = rl[i] >> 8;
        df_put(w, cc[s], cl[s]);
        if (s == 16) df_put(w, (u32)e, 2); else if (s == 17) df_put(w, (u32)e, 3); else if (s == 18) df_put(w, (u32)e, 7);
    }
    for (int i = 0; i < ns; i++) {
        u32 s = sym[i];
        if (s < 256) { df_put(w, lc[s], ll[s]); continue; }
        int l = (int)(s >> 16), d = (int)(s & 0xFFFF), ls = df_lsym(l), ds = df_dsym(d);
        df_put(w, lc[257 + ls], ll[257 + ls]); if (df_lext[ls]) df_put(w, (u32)(l - df_lbase[ls]), df_lext[ls]);
        df_put(w, dc[ds], dl[ds]); if (df_dext[ds]) df_put(w, (u32)(d - df_dbase[ds]), df_dext[ds]);
    }
    df_put(w, lc[256], ll[256]);
}
#define DF_HB 15
#define DF_WIN 32768
static u32 df_zlib(const u8 *src, u32 n, u8 *dst, u32 cap) {
    df_out w = { dst, cap, 0, 0, 0, 0 };
    if (cap < 8) return 0;
    df_put(&w, 0x78, 8); df_put(&w, 0xDA, 8);            /* (zlib, fenetre 32 Ko, niveau max) */
    int *head = (int *)malloc(sizeof(int) << DF_HB), *prev = (int *)malloc(sizeof(int) * DF_WIN);
    if (!head || !prev) { free(head); free(prev); return 0; }
    for (int i = 0; i < (1 << DF_HB); i++) head[i] = -1;
    int ns = 0;
#define DF_H(p) ((((u32)src[p] << 10) ^ ((u32)src[(p) + 1] << 5) ^ src[(p) + 2]) & ((1u << DF_HB) - 1))
#define DF_INS(p) do { if ((p) + 2 < (int)n) { u32 hh = DF_H(p); prev[(p) & (DF_WIN - 1)] = head[hh]; head[hh] = (p); } } while (0)
    int i = 0;
    while (i < (int)n) {
        int bl = 0, bd = 0;
        if (i + 2 < (int)n) {                               /* meilleure repetition a i */
            int lim = n - i > 258 ? 258 : (int)(n - i), chain = 4096;
            for (int c = head[DF_H(i)]; c >= 0 && i - c <= DF_WIN && chain--; c = prev[c & (DF_WIN - 1)]) {
                if (src[c + bl] != src[i + bl]) continue;
                int l = 0;
                while (l < lim && src[c + l] == src[i + l]) l++;
                if (l > bl) { bl = l; bd = i - c; if (l == lim) break; }
            }
        }
        if (bl >= 3 && i + 1 < (int)n && bl < 258) {        /* lazy : un peu plus loin, mieux ? */
            DF_INS(i);
            int l2 = 0, lim = n - i - 1 > 258 ? 258 : (int)(n - i - 1), chain = 1024;
            if (i + 3 < (int)n)
                for (int c = head[DF_H(i + 1)]; c >= 0 && i + 1 - c <= DF_WIN && chain--; c = prev[c & (DF_WIN - 1)]) {
                    if (c >= i + 1) continue;
                    int l = 0;
                    while (l < lim && src[c + l] == src[i + 1 + l]) l++;
                    if (l > l2) l2 = l;
                }
            if (l2 > bl) { df_sym[ns++] = src[i]; i++; goto flush; }
            for (int k = 1; k < bl; k++) DF_INS(i + k);
            df_sym[ns++] = (u32)bl << 16 | (u32)bd; i += bl;
        } else if (bl >= 3) {
            for (int k = 0; k < bl; k++) DF_INS(i + k);
            df_sym[ns++] = (u32)bl << 16 | (u32)bd; i += bl;
        } else { DF_INS(i); df_sym[ns++] = src[i]; i++; }
    flush:
        if (ns >= DF_BLK) { df_block(&w, df_sym, ns, 0); ns = 0; if (w.over) break; }
    }
    if (!w.over) df_block(&w, df_sym, ns, 1);
    df_flush(&w);
    free(head); free(prev);
    u32 a = 1, b = 0;                                      /* Adler-32 */
    for (u32 k = 0; k < n; k++) { a = (a + src[k]) % 65521u; b = (b + a) % 65521u; }
    u32 ad = b << 16 | a;
    for (int k = 3; k >= 0; k--) df_put(&w, (ad >> (8 * k)) & 0xFF, 8);
    return w.over ? 0 : w.n;
}

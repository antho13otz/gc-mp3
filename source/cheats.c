// GC-MP3 v25 : editeur de cheats (inclus dans main.c, apres tabs_extra.c)
// v90 : les cheats sont dans SD:/gc-mp3/cheats/<ID du jeu>.txt (avant SD:/swiss/cheats), ex. GALE01.txt ;
// le lanceur de secours les lit la (format des cheats de swiss-gc) :
//     Nom du cheat
//     XXXXXXXX YYYYYYYY        (une ou plusieurs lignes de code Action Replay / Gecko)
//     (ligne vide entre deux cheats)
// Ouverture : onglet FICHIERS > X > Cheats, START > Cheats, ou un .txt
// de SD:/gc-mp3/cheats ouvert dans l'onglet FICHIERS.
// Le fichier est garde ligne par ligne : ce que l'editeur ne comprend pas reste intact.

#define CH_DIR      GCD "/cheats"
#define CH_MAXL     3000                    /* lignes par fichier */
#define CH_LW       128                     /* longueur d'une ligne */
#define CH_MAXF     300                     /* fichiers dans le dossier */
#define CH_MAXC     400                     /* cheats par fichier */

enum { CHV_LIST, CHV_FILE, CHV_CODES };
static int ch_view = CHV_LIST;
static char (*ch_fn)[64] = NULL;           /* fichiers .txt du dossier */
static long *ch_fsz = NULL;
static int ch_nf = 0, ch_fsel = 0, ch_fconfirm = 0;
static char (*ch_l)[CH_LW] = NULL;         /* lignes du fichier ouvert */
static int ch_nl = 0, ch_crlf = 0, ch_dirty = 0, ch_quit_warn = 0;
static char ch_path[NAME_LEN], ch_name[64];
static int ch_cs[CH_MAXC], ch_ce[CH_MAXC], ch_nc = 0;    /* cheat = lignes [cs, ce[ */
static int ch_csel = 0, ch_lsel = 0;
static int ch_edit = 0, ch_dig = 0;        /* edition chiffre par chiffre */
static int ch_pop = 0, ch_psel = 0, ch_pconfirm = 0;
static char ch_msg[160];
static u64 ch_msg_t = 0;

/* v31 : cheats actives / desactives, comme dans Swiss (fichier <ID>.chtsel) */
static unsigned char ch_en[CH_MAXC];        /* bloc active */
static short ch_swn[CH_MAXC], ch_swbad[CH_MAXC];   /* cheats Swiss utilisables / refuses (code avec XX) dans le bloc */
static short *ch_fen = NULL;                /* liste : nombre de cheats actives par fichier */
static void ch_index(void);
static void ch_sel_save(void);
static int ch_save(void);
static int ch_ncodes(int c, int *bad);
static void ch_sw_map(void);

static void ch_note(const char *m) { snprintf(ch_msg, sizeof(ch_msg), "%s", TR(m)); ch_msg_t = now_ms(); }

static int ch_hex(int c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
/* ligne de code : 8 chiffres hexa, espace, 8 chiffres hexa (espaces autour acceptes) */
static int ch_is_code(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    for (int i = 0; i < 8; i++) if (!ch_hex((u8)s[i])) return 0;
    if (s[8] != ' ' && s[8] != '\t') return 0;
    const char *v = s + 9;
    while (*v == ' ' || *v == '\t') v++;
    for (int i = 0; i < 8; i++) if (!ch_hex((u8)v[i])) return 0;
    v += 8;
    while (*v == ' ' || *v == '\t' || *v == '\r') v++;
    return *v == 0;
}
static int ch_blank(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\r') s++; return *s == 0; }
/* ressemble a une ligne de code (commence par un chiffre hexa et ne fait presque que de l'hexa) */
static int ch_codeish(const char *s) {
    int hx = 0, other = 0;
    for (; *s; s++) { if (ch_hex((u8)*s)) hx++; else if (*s != ' ' && *s != '\t' && *s != '\r') other++; }
    return hx >= 8 && other == 0;
}

/* ---- lecture des cheats exactement comme Swiss (cheats.c de Swiss : parseCheats) ----
   Swiss saute les lignes vides ; un cheat commence a une ligne de code et continue tant
   que les lignes sont du code ; un cheat avec une ligne "XX" est ignore (et ne compte pas). */
/* v37 : sans sscanf (plus rapide, et rien de complique sur la console) ; meme resultat que
   sscanf(l, "%*2[hex]%*6[alnum] %*8[alnum]%n") et sscanf(l, "%*8[hex] %*8[hex]%n") dans Swiss */
static int ch_ishex(u8 c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static int ch_isaln(u8 c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int ch_isws(u8 c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
static int ch_run(const char *l, int i, int max, int (*ok)(u8)) { int n = 0; while (n < max && l[i + n] && ok((u8)l[i + n])) n++; return n; }
static int ch_sw_iscode_raw(const char *l) {
    int i = 0, n;
    if (!(n = ch_run(l, i, 2, ch_ishex))) return 0;
    i += n;
    if (!(n = ch_run(l, i, 6, ch_isaln))) return 0;
    i += n;
    while (l[i] && ch_isws((u8)l[i])) i++;
    if (!(n = ch_run(l, i, 8, ch_isaln))) return 0;
    return i + n > 16;
}
static int ch_sw_valid_raw(const char *l) {
    int i = 0, n;
    if (!(n = ch_run(l, i, 8, ch_ishex))) return 0;
    i += n;
    while (l[i] && ch_isws((u8)l[i])) i++;
    if (!(n = ch_run(l, i, 8, ch_ishex))) return 0;
    return i + n > 16;
}
/* drapeaux par ligne, calcules une fois dans ch_index : 1 = ligne de code pour Swiss, 2 = code valide */
static u8 *ch_lf = NULL;
static int ch_lf_n = 0;
static int ch_sw_iscode(const char *l) {
    if (ch_lf && l >= ch_l[0] && l < ch_l[0] + (size_t)ch_lf_n * CH_LW) return ch_lf[(l - ch_l[0]) / CH_LW] & 1;
    return ch_sw_iscode_raw(l);
}
static int ch_sw_valid(const char *l) {
    if (ch_lf && l >= ch_l[0] && l < ch_l[0] + (size_t)ch_lf_n * CH_LW) return (ch_lf[(l - ch_l[0]) / CH_LW] & 2) != 0;
    return ch_sw_valid_raw(l);
}
static void ch_flags(void) {
    if (!ch_lf) ch_lf = (u8 *)malloc(CH_MAXL);
    ch_lf_n = 0;
    if (!ch_lf || !ch_l) return;
    for (int i = 0; i < ch_nl; i++) ch_lf[i] = (u8)((ch_sw_iscode_raw(ch_l[i]) ? 1 : 0) | (ch_sw_valid_raw(ch_l[i]) ? 2 : 0));
    ch_lf_n = ch_nl;
}
/* "%x" : chiffres hexa, sature a 0xFFFFFFFF comme strtoul */
static u32 ch_hexval(const char **pp) {
    const char *p = *pp;
    while (*p && ch_isws((u8)*p)) p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && ch_ishex((u8)p[2])) p += 2;
    unsigned long long v = 0;
    while (ch_ishex((u8)*p)) {
        int d = *p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10;
        v = v * 16 + d;
        if (v > 0xFFFFFFFFull) v = 0x100000000ull;
        p++;
    }
    *pp = p;
    return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)v;
}
/* parcourt les cheats Swiss : fn(no, premiere ligne, fin) pour chaque cheat utilisable */
static int ch_sw_walk(void (*fn)(int, int, int, void *), void *ctx) {
    int n = 0, i = 0;
    while (i < ch_nl) {
        if (!ch_l[i][0] || !ch_sw_iscode(ch_l[i])) { i++; continue; }
        int s = i, bad = !ch_sw_valid(ch_l[i]);
        i++;
        while (i < ch_nl) {
            if (!ch_l[i][0]) { i++; continue; }
            if (!ch_sw_iscode(ch_l[i])) break;
            if (!ch_sw_valid(ch_l[i])) bad = 1;
            i++;
        }
        if (fn) fn(bad ? -1 : n, s, i, ctx);
        if (!bad) n++;
    }
    return n;
}
static int ch_blk_of(int line) {
    for (int c = 0; c < ch_nc; c++) if (line >= ch_cs[c] && line < ch_ce[c]) return c;
    return -1;
}
static void ch_sw_map_cb(int no, int s, int e, void *ctx) {
    (void)e; (void)ctx;
    int b = ch_blk_of(s);
    if (b < 0) return;
    if (no < 0) ch_swbad[b]++; else ch_swn[b]++;
}
static void ch_sw_map(void) {
    memset(ch_swn, 0, sizeof(ch_swn)); memset(ch_swbad, 0, sizeof(ch_swbad));
    if (ch_l) ch_sw_walk(ch_sw_map_cb, NULL);
}

/* XXH64 (graine 0), comme la bibliotheque xxhash utilisee par Swiss */
#define XP1 11400714785074694791ULL
#define XP2 14029467366897019727ULL
#define XP3 1609587929392839161ULL
#define XP4 9650029242287828579ULL
#define XP5 2870177450012600261ULL
static u64 xh_rotl(u64 x, int r) { return (x << r) | (x >> (64 - r)); }
static u64 xh_rd64(const u8 *p) { u64 v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }
static u32 xh_rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u64 xh_round(u64 acc, u64 in) { acc += in * XP2; acc = xh_rotl(acc, 31); return acc * XP1; }
static u64 xh_merge(u64 acc, u64 v) { acc ^= xh_round(0, v); return acc * XP1 + XP4; }
static u64 xxh64(const u8 *p, size_t len) {
    const u8 *end = p + len;
    u64 h;
    if (len >= 32) {
        u64 v1 = XP1 + XP2, v2 = XP2, v3 = 0, v4 = 0 - XP1;
        while (end - p >= 32) {
            v1 = xh_round(v1, xh_rd64(p)); v2 = xh_round(v2, xh_rd64(p + 8));
            v3 = xh_round(v3, xh_rd64(p + 16)); v4 = xh_round(v4, xh_rd64(p + 24));
            p += 32;
        }
        h = xh_rotl(v1, 1) + xh_rotl(v2, 7) + xh_rotl(v3, 12) + xh_rotl(v4, 18);
        h = xh_merge(h, v1); h = xh_merge(h, v2); h = xh_merge(h, v3); h = xh_merge(h, v4);
    } else h = XP5;
    h += (u64)len;
    while (end - p >= 8) { h ^= xh_round(0, xh_rd64(p)); h = xh_rotl(h, 27) * XP1 + XP4; p += 8; }
    if (end - p >= 4) { h ^= (u64)xh_rd32(p) * XP1; h = xh_rotl(h, 23) * XP2 + XP3; p += 4; }
    while (p < end) { h ^= (u64)(*p++) * XP5; h = xh_rotl(h, 11) * XP1; }
    h ^= h >> 33; h *= XP2; h ^= h >> 29; h *= XP3; h ^= h >> 32;
    return h;
}
/* empreinte des codes (Swiss : calcCheatsHash, codes en memoire GameCube = gros-boutiste) */
typedef struct { u8 *b; int n, cap; } ch_hbuf;
static void ch_hash_cb(int no, int s, int e, void *ctx) {
    ch_hbuf *hb = (ch_hbuf *)ctx;
    if (no < 0 || hb->cap < 0) return;
    for (int i = s; i < e; i++) {
        if (!ch_l[i][0] || !ch_sw_iscode(ch_l[i])) continue;
        const char *pp = ch_l[i];
        u32 a = ch_hexval(&pp), b = ch_hexval(&pp);
        if (hb->n + 8 > hb->cap) {
            int nc = hb->cap ? hb->cap * 2 : 4096;
            u8 *nb = realloc(hb->b, nc);
            if (!nb) { hb->cap = -1; return; }
            hb->b = nb; hb->cap = nc;
        }
        u8 *q = hb->b + hb->n;
        q[0] = a >> 24; q[1] = a >> 16; q[2] = a >> 8; q[3] = a; q[4] = b >> 24; q[5] = b >> 16; q[6] = b >> 8; q[7] = b;
        hb->n += 8;
    }
}
static int ch_codes_hash(u64 *out) {
    ch_hbuf hb = { NULL, 0, 0 };
    ch_sw_walk(ch_hash_cb, &hb);
    if (hb.cap < 0) { free(hb.b); return 0; }
    *out = xxh64(hb.b ? hb.b : (const u8 *)"", (size_t)hb.n);
    free(hb.b);
    return 1;
}
/* SD:/gc-mp3/cheats/<ID>.chtsel (le nom du .txt doit etre l'ID du jeu, 6 caracteres) */
static int ch_sel_path(const char *txtname, char *out, int cap) {
    const char *dot = strrchr(txtname, '.');
    if (!dot || dot - txtname != 6) return 0;
    snprintf(out, cap, "%s/%.6s.chtsel", CH_DIR, txtname);
    return 1;
}
typedef struct { const u8 *sel; long n; } ch_lctx;
static void ch_sel_load_cb(int no, int s, int e, void *ctx) {
    (void)e;
    ch_lctx *lc = (ch_lctx *)ctx;
    if (no < 0 || no >= lc->n) return;
    int b = ch_blk_of(s);
    if (b >= 0 && lc->sel[no]) ch_en[b] = 1;
}
static void ch_sel_load(void) {
    memset(ch_en, 0, sizeof(ch_en));
    char p[NAME_LEN];
    if (!ch_sel_path(ch_name, p, sizeof(p))) return;
    long len = 0;
    unsigned char *d = read_file(p, 65536, &len);
    if (!d) return;
    u64 h = 0;
    if (len >= 8 && ch_codes_hash(&h)) {
        u64 old = 0;
        for (int i = 0; i < 8; i++) old = (old << 8) | d[len - 8 + i];
        if (old == h) { ch_lctx lc = { d, len - 8 }; ch_sw_walk(ch_sel_load_cb, &lc); }
    }
    free(d);
}
/* v121 : les codes actives d'un jeu (pour Pulse, sans le lanceur de secours) : memes regles que Swiss (un
   cheat = des lignes de code a la suite ; choix dans <ID>.chtsel, valable si l'empreinte des codes est la
   meme). Ne touche pas a l'editeur. Rend le nombre de mots ecrits dans out (2 par ligne), 0 = rien
   d'active, -1 = trop de codes ou memoire insuffisante. */
static int ch_collect(const char *id, u32 *out, int cap) {
    char p[NAME_LEN];
    snprintf(p, sizeof(p), "%s/%.6s.txt", CH_DIR, id);
    long len = 0;
    unsigned char *d = read_file(p, 1024 * 1024, &len);
    if (!d) return 0;
    char (*l)[CH_LW] = malloc(sizeof(*l) * CH_MAXL);
    if (!l) { free(d); return -1; }
    int nl = 0;
    long i = len >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF ? 3 : 0;
    while (i < len && nl < CH_MAXL) {
        long st = i;
        while (i < len && d[i] != '\n') i++;
        long n = i - st;
        if (n > 0 && d[st + n - 1] == '\r') n--;
        if (n > CH_LW - 1) n = CH_LW - 1;
        memcpy(l[nl], d + st, n); l[nl][n] = 0;
        nl++; i++;
    }
    free(d);
    /* cheats valides (comme ch_sw_walk) : debut, fin ; empreinte de tous leurs codes */
    int ns = 0, cs[CH_MAXC], ce[CH_MAXC];
    ch_hbuf hb = { NULL, 0, 0 };
    for (int k = 0; k < nl;) {
        if (!l[k][0] || !ch_sw_iscode_raw(l[k])) { k++; continue; }
        int s0 = k, bad = !ch_sw_valid_raw(l[k]);
        k++;
        while (k < nl) { if (!l[k][0]) { k++; continue; } if (!ch_sw_iscode_raw(l[k])) break; if (!ch_sw_valid_raw(l[k])) bad = 1; k++; }
        if (bad) continue;
        if (ns < CH_MAXC) { cs[ns] = s0; ce[ns] = k; }
        ns++;
        for (int j = s0; j < k; j++) {
            if (!l[j][0] || !ch_sw_iscode_raw(l[j])) continue;
            const char *pp = l[j];
            u32 a = ch_hexval(&pp), b = ch_hexval(&pp);
            if (hb.cap >= 0 && hb.n + 8 > hb.cap) { int nc = hb.cap ? hb.cap * 2 : 4096; u8 *nb = realloc(hb.b, nc); if (!nb) hb.cap = -1; else { hb.b = nb; hb.cap = nc; } }
            if (hb.cap < 0) continue;
            u8 *q = hb.b + hb.n;
            q[0] = a >> 24; q[1] = a >> 16; q[2] = a >> 8; q[3] = a; q[4] = b >> 24; q[5] = b >> 16; q[6] = b >> 8; q[7] = b;
            hb.n += 8;
        }
    }
    int r = 0;
    snprintf(p, sizeof(p), "%s/%.6s.chtsel", CH_DIR, id);
    long sl = 0;
    unsigned char *sel = hb.cap >= 0 ? read_file(p, 65536, &sl) : NULL;
    if (hb.cap < 0) r = -1;
    else if (sel && sl >= 8) {
        u64 h = xxh64(hb.b ? hb.b : (const u8 *)"", (size_t)hb.n), old = 0;
        for (int k = 0; k < 8; k++) old = (old << 8) | sel[sl - 8 + k];
        if (old == h) {
            for (int c = 0; c < ns && c < CH_MAXC && c < sl - 8 && r >= 0; c++) {
                if (!sel[c]) continue;
                for (int j = cs[c]; j < ce[c]; j++) {
                    if (!l[j][0] || !ch_sw_iscode_raw(l[j])) continue;
                    if (r + 2 > cap) { r = -1; break; }
                    const char *pp = l[j];
                    out[r] = ch_hexval(&pp); out[r + 1] = ch_hexval(&pp); r += 2;
                }
            }
        }
    }
    free(sel); free(hb.b); free(l);
    return r;
}
typedef struct { u8 *sel; int n, any; } ch_sctx;
static void ch_sel_save_cb(int no, int s, int e, void *ctx) {
    (void)e;
    ch_sctx *sc = (ch_sctx *)ctx;
    if (no < 0 || no >= sc->n) return;
    int b = ch_blk_of(s);
    sc->sel[no] = b >= 0 && ch_en[b];
    if (sc->sel[no]) sc->any = 1;
}
static int ch_sel_gen = 0;                      /* v46 : change a chaque enregistrement (fiche du jeu a jour) */
static void ch_sel_save(void) {
    char p[NAME_LEN];
    if (!ch_l || !ch_sel_path(ch_name, p, sizeof(p))) return;
    ch_index();                                               /* (lignes peut-etre modifiees depuis) */
    int n = ch_sw_walk(NULL, NULL);
    u8 *sel = calloc(1, (size_t)n + 8);
    u64 h = 0;
    if (!sel || !ch_codes_hash(&h)) { free(sel); ch_note("M\xC3\xA9moire insuffisante"); return; }
    ch_sctx sc = { sel, n, 0 };
    ch_sw_walk(ch_sel_save_cb, &sc);
    if (!sc.any) { remove(p); free(sel); ch_sel_gen++; return; }       /* (Swiss efface aussi le fichier quand rien n'est active) */
    for (int i = 0; i < 8; i++) sel[n + i] = (u8)(h >> (56 - 8 * i));
    char tmp[NAME_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(sel, 1, (size_t)n + 8, f) == (size_t)n + 8;
    if (f && fclose(f) != 0) ok = 0;
    free(sel);
    if (!ok) { remove(tmp); ch_note("Impossible d'\xC3\xA9" "crire sur la carte SD"); return; }
    remove(p);
    if (rename(tmp, p) != 0) ch_note("Impossible d'\xC3\xA9" "crire sur la carte SD");
    ch_sel_gen++;
}
static int ch_sel_dirty = 0;                    /* v37 : choix pas encore ecrits (ecrits 1 s apres, ou en quittant) */
static u64 ch_sel_t = 0;
static void ch_sel_flush(void) {
    if (!ch_sel_dirty || ch_dirty) return;          /* (texte modifie : ecrit avec lui, par Y) */
    ch_sel_dirty = 0;
    crumb("cheats : choix enregistres");
    ch_sel_save();
}
static void cheats_tick(void) { if (ch_sel_dirty && now_ms() - ch_sel_t > 1000) ch_sel_flush(); }   /* (boucle principale) */
static int ch_count_on(void) { int k = 0; for (int c = 0; c < ch_nc; c++) if (ch_en[c] && ch_swn[c]) k++; return k; }
/* active / desactive le cheat c */
static void ch_toggle(int c) {
    if (c < 0 || c >= ch_nc) return;
    char p[NAME_LEN];
    if (!ch_sel_path(ch_name, p, sizeof(p))) { ch_note("Le fichier doit s'appeler comme l'ID du jeu (ex. GALE01.txt)"); return; }
    if (!ch_swn[c]) {
        if (ch_swbad[c]) ch_note("Code non pris en charge (lettres XX)");
        else if (!ch_ncodes(c, NULL)) ch_note("Ce n'est pas un cheat (pas de ligne de code)");
        else ch_note("Code pas au bon format (XXXXXXXX YYYYYYYY)");
        return;
    }
    ch_en[c] = !ch_en[c];
    ch_sel_dirty = 1; ch_sel_t = now_ms();            /* (ecrit un peu plus tard : on peut en cocher plusieurs vite) */
    crumb("cheats : %d -> %d", c, ch_en[c]);
    int k = ch_count_on();
    if (k == 1) ch_note(ch_en[c] ? "Cheat activ\xC3\xA9 (1 actif)" : "Cheat d\xC3\xA9sactiv\xC3\xA9 (1 actif)");
    else {
        if (ch_en[c]) snprintf(ch_msg, sizeof(ch_msg), "Cheat activ\xC3\xA9 (%d actifs)", k);
        else snprintf(ch_msg, sizeof(ch_msg), "Cheat d\xC3\xA9sactiv\xC3\xA9 (%d actifs)", k);
        ch_msg_t = now_ms();
    }
}

/* decoupe le fichier en cheats : un bloc = lignes non vides entre deux lignes vides */
static void ch_index(void) {
    ch_flags();
    ch_nc = 0;
    int i = 0;
    while (i < ch_nl && ch_nc < CH_MAXC) {
        while (i < ch_nl && ch_blank(ch_l[i])) i++;
        if (i >= ch_nl) break;
        int s = i;
        while (i < ch_nl && !ch_blank(ch_l[i])) i++;
        ch_cs[ch_nc] = s; ch_ce[ch_nc] = i; ch_nc++;
    }
    if (ch_csel > ch_nc) ch_csel = ch_nc;
    if (ch_csel < 0) ch_csel = 0;
    ch_sw_map();
}
static int ch_ncodes(int c, int *bad) {
    int n = 0; if (bad) *bad = 0;
    int odd = 0, odd_hex = 0;
    for (int i = ch_cs[c]; i < ch_ce[c]; i++) {
        if (ch_is_code(ch_l[i])) n++;
        else if (i > ch_cs[c]) { odd++; if (ch_codeish(ch_l[i])) odd_hex++; }
    }
    /* ligne fausse : pas du code, dans un cheat qui a du code (l'en-tete ID + nom du jeu n'en a pas) */
    if (bad) *bad = n ? odd : odd_hex;
    return n;
}
/* nom d'un cheat : sa premiere ligne si ce n'est pas du code */
static const char *ch_cname(int c) {
    if (ch_is_code(ch_l[ch_cs[c]])) return "(sans nom)";
    return ch_l[ch_cs[c]];
}
/* premiere ligne de code (ou de contenu apres le nom) d'un cheat */
static int ch_first_line(int c) { return ch_is_code(ch_l[ch_cs[c]]) ? ch_cs[c] : ch_cs[c] + 1; }

static int ch_fcmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static void ch_scan(void) {
    if (!ch_fn) { ch_fn = malloc(sizeof(*ch_fn) * CH_MAXF); ch_fsz = malloc(sizeof(long) * CH_MAXF); }
    ch_nf = 0;
    if (!ch_fn || !ch_fsz || !sd_ok) return;
    mkdir(GCD, 0777);
    mkdir(CH_DIR, 0777);
    DIR *d = opendir(CH_DIR);
    if (!d) return;
    struct dirent *e;
    static char sel[64][8];                                 /* v37 : les .chtsel vus dans le dossier (pas de stat par fichier) */
    int nsel = 0;
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot) continue;
        if (!strcasecmp(dot, ".chtsel")) { if (nsel < 64 && dot - e->d_name == 6) { memcpy(sel[nsel], e->d_name, 6); sel[nsel][6] = 0; nsel++; } continue; }
        if (ch_nf >= CH_MAXF || strcasecmp(dot, ".txt") || strlen(e->d_name) >= 64) continue;
        snprintf(ch_fn[ch_nf], 64, "%s", e->d_name);
        ch_nf++;
        if ((ch_nf & 31) == 0) service_audio();
    }
    closedir(d);
    qsort(ch_fn, ch_nf, 64, ch_fcmp);
    if (!ch_fen) ch_fen = malloc(sizeof(short) * CH_MAXF);
    for (int i = 0; i < ch_nf; i++) {
        ch_fsz[i] = -1;                                     /* (taille lue plus tard, seulement pour les lignes affichees) */
        if (!ch_fen) continue;
        ch_fen[i] = 0;                                      /* cheats actives (sans verifier l'empreinte) */
        for (int k = 0; k < nsel; k++) {
            if (strncasecmp(ch_fn[i], sel[k], 6) || ch_fn[i][6] != '.') continue;
            char p[NAME_LEN];
            snprintf(p, sizeof(p), "%s/%s.chtsel", CH_DIR, sel[k]);
            long len = 0; unsigned char *dd = read_file(p, 65536, &len);
            if (dd) { for (long j = 0; j + 8 < len; j++) if (dd[j]) ch_fen[i]++; free(dd); }
            break;
        }
    }
    if (ch_fsel > ch_nf) ch_fsel = ch_nf;
}

static int ch_load(const char *path) {
    if (!ch_l) ch_l = malloc(sizeof(*ch_l) * CH_MAXL);
    if (!ch_l) { ch_note("M\xC3\xA9moire insuffisante"); return 0; }
    long len = 0;
    unsigned char *d = read_file(path, 1024 * 1024, &len);
    ch_nl = 0; ch_crlf = 0;
    if (d) {
        long i = 0;
        if (len >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) i = 3;     /* (BOM UTF-8) */
        while (i < len && ch_nl < CH_MAXL) {
            long s = i;
            while (i < len && d[i] != '\n') i++;
            long n = i - s;
            if (n > 0 && d[s + n - 1] == '\r') { n--; ch_crlf = 1; }
            if (n > CH_LW - 1) n = CH_LW - 1;
            memcpy(ch_l[ch_nl], d + s, n); ch_l[ch_nl][n] = 0;
            ch_nl++;
            i++;
        }
        if (len > 0 && d[len - 1] == '\n' && ch_nl > 0 && !ch_l[ch_nl - 1][0]) { /* (derniere ligne vide = fin du fichier) */ }
        free(d);
        if (ch_nl >= CH_MAXL) ch_note("Fichier trop long : seul le d\xC3\xA9" "but est modifiable");
    }
    snprintf(ch_path, sizeof(ch_path), "%s", path);
    snprintf(ch_name, sizeof(ch_name), "%s", base_of(path));
    ch_dirty = 0; ch_quit_warn = 0; ch_csel = 0; ch_lsel = 0; ch_edit = 0; ch_pop = 0;
    ch_index();
    ch_sel_load();
    return 1;
}

static int ch_save(void) {
    if (!ch_l || !ch_path[0]) return 0;
    char tmp[NAME_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", ch_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { ch_note("Impossible d'\xC3\xA9" "crire sur la carte SD"); return 0; }
    int n = ch_nl;
    while (n > 0 && ch_blank(ch_l[n - 1])) n--;               /* pas de lignes vides a la fin */
    int ok = 1;
    for (int i = 0; i < n && ok; i++) {
        if (fputs(ch_l[i], f) < 0 || fputs(ch_crlf ? "\r\n" : "\n", f) < 0) ok = 0;
    }
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); ch_note("\xC3\x89" "criture incompl\xC3\xA8te (carte pleine ?)"); return 0; }
    remove(ch_path);
    if (rename(tmp, ch_path) != 0) { ch_note("Impossible de remplacer le fichier"); return 0; }
    ch_dirty = 0; ch_quit_warn = 0;
    ch_sel_dirty = 0;
    ch_sel_save();                                            /* (garde les cheats actives : l'empreinte change) */
    ch_note("Enregistr\xC3\xA9 : appliqu\xC3\xA9 au prochain lancement du jeu (lanceur de secours)");
    return 1;
}

/* insere une ligne avant la position at */
static int ch_ins(int at, const char *s) {
    if (ch_nl >= CH_MAXL) { ch_note("Fichier plein (3000 lignes)"); return 0; }
    if (at < 0) at = 0;
    if (at > ch_nl) at = ch_nl;
    memmove(ch_l[at + 1], ch_l[at], (size_t)(ch_nl - at) * CH_LW);
    snprintf(ch_l[at], CH_LW, "%s", s);
    ch_nl++; ch_dirty = 1;
    return 1;
}
static void ch_del(int at, int n) {
    if (at < 0 || n <= 0 || at >= ch_nl) return;
    if (at + n > ch_nl) n = ch_nl - at;
    memmove(ch_l[at], ch_l[at + n], (size_t)(ch_nl - at - n) * CH_LW);
    ch_nl -= n; ch_dirty = 1;
}

static void cheats_open(void) {                 /* liste des fichiers */
    ch_on = 1; ch_view = CHV_LIST; ch_fconfirm = 0; ch_pop = 0;
    tab = TAB_FILES;
    ch_scan();
    crumb("cheats");
}
static void cheats_open_file(const char *path) {
    ch_on = 1;
    tab = TAB_FILES;
    if (ch_load(path)) ch_view = CHV_FILE;
    else { ch_view = CHV_LIST; ch_scan(); }
}

/* -------- clavier -------- */
static int ch_kb_target = -1;                  /* ligne a remplacer (-1 = nouvelle) */
static void ch_norm_code(const char *t, char *out, int cap) {
    char hx[17]; int n = 0;
    for (const char *p = t; *p && n < 16; p++) if (ch_hex((u8)*p)) hx[n++] = (char)toupper((u8)*p);
    hx[n] = 0;
    int other = 0;
    for (const char *p = t; *p; p++) if (!ch_hex((u8)*p) && *p != ' ' && *p != '\t') other++;
    if (n == 16 && !other) snprintf(out, cap, "%.8s %.8s", hx, hx + 8);
    else snprintf(out, cap, "%s", t);                         /* garde tel quel (en rouge si faux) */
}
static void cheats_kb_done(int mode, const char *t) {
    if (mode == KB_CHEATID) {
        char id[64]; int n = 0;
        for (const char *p = t; *p && n < 40; p++) {
            char c = *p;
            if (isalnum((u8)c) || c == '_' || c == '-') id[n++] = (char)toupper((u8)c);
        }
        id[n] = 0;
        if (!n) { ch_note("Tape l'ID du jeu (6 caract\xC3\xA8res, ex. GALE01)"); return; }
        char path[NAME_LEN]; struct stat st;
        snprintf(path, sizeof(path), "%s/%s.txt", CH_DIR, id);
        if (stat(path, &st) != 0) {
            mkdir(GCD, 0777); mkdir(CH_DIR, 0777);
            FILE *f = fopen(path, "wb");
            if (!f) { ch_note("Impossible d'\xC3\xA9" "crire sur la carte SD"); return; }
            fclose(f);
        }
        cheats_open_file(path);
        if (n != 6) ch_note("Attention : un ID de jeu GameCube fait 6 caract\xC3\xA8res (ex. GALE01)");
        return;
    }
    if (!ch_l) return;
    if (mode == KB_CHEATNAME) {
        char nm[CH_LW];
        snprintf(nm, sizeof(nm), "%s", t);
        if (ch_kb_target < 0) {                                /* nouveau cheat a la fin */
            int at = ch_nl;
            while (at > 0 && ch_blank(ch_l[at - 1])) at--;
            ch_nl = at;
            if (at > 0) ch_ins(ch_nl, "");
            ch_ins(ch_nl, nm);
            ch_ins(ch_nl, "00000000 00000000");
            ch_index();
            ch_csel = ch_nc - 1;
            if (ch_csel >= 0) ch_en[ch_csel] = 0;
            ch_view = CHV_CODES; ch_lsel = 0; ch_edit = 1; ch_dig = 0;
            ch_note("Nouveau cheat : modifie la ligne avec la croix, A pour finir");
        } else {
            if (ch_is_code(ch_l[ch_kb_target])) ch_ins(ch_kb_target, nm);   /* (pas encore de nom) */
            else { snprintf(ch_l[ch_kb_target], CH_LW, "%s", nm); ch_dirty = 1; }
            ch_index();
        }
        return;
    }
    if (mode == KB_CHEATCODE) {
        char ln[CH_LW];
        ch_norm_code(t, ln, sizeof(ln));
        if (ch_kb_target >= 0 && ch_kb_target < ch_nl) { snprintf(ch_l[ch_kb_target], CH_LW, "%s", ln); ch_dirty = 1; }
        else if (ch_csel < ch_nc) { ch_ins(ch_ce[ch_csel], ln); }
        ch_index();
        if (!ch_is_code(ln)) ch_note("Ligne pas au format XXXXXXXX YYYYYYYY (en rouge)");
    }
}

/* -------- actions -------- */
enum { CHP_CODES, CHP_RENAME, CHP_DUP, CHP_DEL, CHP_UP, CHP_DOWN, CHP_COUNT };
static const char *const chp_names[CHP_COUNT] = { "Voir / modifier les codes", "Renommer", "Dupliquer", "Supprimer", "Monter", "Descendre" };
enum { CHL_ADD, CHL_TYPE, CHL_DEL, CHL_DUP, CHL_COUNT };
static const char *const chl_names[CHL_COUNT] = { "Ajouter une ligne", "Taper la ligne au clavier", "Supprimer la ligne", "Dupliquer la ligne" };

/* deplace le cheat c d'un cran (d = -1 / +1) */
static void ch_move(int c, int d) {
    int o = c + d;
    if (o < 0 || o >= ch_nc) return;
    int a = d < 0 ? o : c, b = d < 0 ? c : o;           /* a avant b */
    int as = ch_cs[a], ae = ch_ce[a], bs = ch_cs[b], be = ch_ce[b];
    int na = ae - as, nb = be - bs, gap = bs - ae;
    int tot = be - as;
    char (*tmp)[CH_LW] = malloc((size_t)tot * CH_LW);
    if (!tmp) return;
    memcpy(tmp, ch_l[bs], (size_t)nb * CH_LW);
    memcpy(tmp + nb, ch_l[ae], (size_t)gap * CH_LW);
    memcpy(tmp + nb + gap, ch_l[as], (size_t)na * CH_LW);
    memcpy(ch_l[as], tmp, (size_t)tot * CH_LW);
    free(tmp);
    ch_dirty = 1;
    ch_index();
    { unsigned char t = ch_en[a]; ch_en[a] = ch_en[b]; ch_en[b] = t; }
    ch_csel = o;
}

static void ch_file_action(int a) {
    int c = ch_csel;
    if (c >= ch_nc) { ch_pop = 0; return; }
    switch (a) {
    case CHP_CODES:
        ch_view = CHV_CODES; ch_lsel = 0; ch_edit = 0;
        break;
    case CHP_RENAME:
        ch_kb_target = ch_cs[c];
        kb_start(KB_CHEATNAME, ch_is_code(ch_l[ch_cs[c]]) ? "" : ch_l[ch_cs[c]], -1, -1);
        break;
    case CHP_DUP: {
        int n = ch_ce[c] - ch_cs[c];
        if (ch_nl + n + 1 >= CH_MAXL) { ch_note("Fichier plein (3000 lignes)"); break; }
        int at = ch_ce[c];
        ch_ins(at, "");
        for (int i = 0; i < n; i++) ch_ins(at + 1 + i, ch_l[ch_cs[c] + i]);
        if (!ch_is_code(ch_l[at + 1])) { char nm[CH_LW]; snprintf(nm, sizeof(nm), "%.110s (copie)", ch_l[at + 1]); snprintf(ch_l[at + 1], CH_LW, "%s", nm); }
        ch_index(); ch_csel = c + 1;
        memmove(ch_en + c + 2, ch_en + c + 1, (size_t)(CH_MAXC - c - 2)); ch_en[c + 1] = 0;
        break; }
    case CHP_DEL:
        if (!ch_pconfirm) { ch_pconfirm = 1; return; }
        ch_pconfirm = 0;
        {   int s = ch_cs[c], e = ch_ce[c];
            while (e < ch_nl && ch_blank(ch_l[e])) e++;           /* avec les lignes vides qui suivent */
            ch_del(s, e - s);
            ch_index();
            memmove(ch_en + c, ch_en + c + 1, (size_t)(CH_MAXC - c - 1)); ch_en[CH_MAXC - 1] = 0;
            ch_note("Cheat supprim\xC3\xA9 (Y pour enregistrer)"); }
        break;
    case CHP_UP: ch_move(c, -1); break;
    case CHP_DOWN: ch_move(c, 1); break;
    }
    ch_pop = 0;
}

static void ch_line_action(int a) {
    int c = ch_csel;
    if (c >= ch_nc) { ch_pop = 0; return; }
    int f = ch_first_line(c), n = ch_ce[c] - f, li = f + ch_lsel;
    switch (a) {
    case CHL_ADD:
        ch_ins(ch_lsel < n ? li + 1 : ch_ce[c], "00000000 00000000");
        ch_index();
        if (ch_lsel < n) ch_lsel++;
        ch_edit = 1; ch_dig = 0;
        break;
    case CHL_TYPE:
        ch_kb_target = ch_lsel < n ? li : -1;
        kb_start(KB_CHEATCODE, ch_lsel < n ? ch_l[li] : "", -1, -1);
        break;
    case CHL_DEL:
        if (ch_lsel >= n) break;
        if (ch_ce[c] - ch_cs[c] <= 1) { ch_note("Derni\xC3\xA8re ligne : supprime plut\xC3\xB4t le cheat"); break; }
        ch_del(li, 1);
        ch_index();
        if (ch_lsel > 0 && ch_lsel >= ch_ce[c] - ch_first_line(c)) ch_lsel--;
        break;
    case CHL_DUP:
        if (ch_lsel >= n) break;
        ch_ins(li + 1, ch_l[li]); ch_index(); ch_lsel++;
        break;
    }
    ch_pop = 0;
}

/* change le chiffre hexa sous le curseur (d = +1 / -1) */
static void ch_digit_step(char *ln, int dig, int d) {
    char nrm[CH_LW];
    if (!ch_is_code(ln)) { ch_norm_code(ln, nrm, sizeof(nrm)); if (!ch_is_code(nrm)) snprintf(nrm, sizeof(nrm), "00000000 00000000"); snprintf(ln, CH_LW, "%s", nrm); }
    else { ch_norm_code(ln, nrm, sizeof(nrm)); snprintf(ln, CH_LW, "%s", nrm); }
    int pos = dig < 8 ? dig : dig + 1;
    char c = ln[pos];
    int v = c >= 'A' ? c - 'A' + 10 : c >= 'a' ? c - 'a' + 10 : c - '0';
    v = (v + d + 16) & 15;
    ln[pos] = "0123456789ABCDEF"[v];
    ch_dirty = 1;
}

static void ch_leave_file(void) {
    if (ch_dirty && !ch_quit_warn) { ch_quit_warn = 1; ch_note("Pas enregistr\xC3\xA9 : Y pour enregistrer, B encore pour quitter sans enregistrer"); return; }
    if (ch_dirty && ch_sel_dirty) ch_note("Cheats coch\xC3\xA9s pas enregistr\xC3\xA9s (fichier pas enregistr\xC3\xA9)");
    ch_dirty = 0;
    if (!ch_quit_warn) ch_sel_flush();
    ch_sel_dirty = 0;
    ch_view = CHV_LIST; ch_quit_warn = 0;
    ch_scan();
}

static void cheats_input(u32 down, u32 nav, int fire) {
    if (ch_pop) {                                         /* petite fenetre d'actions */
        int cnt = ch_view == CHV_FILE ? CHP_COUNT : CHL_COUNT;
        if (fire && (nav & PAD_BUTTON_DOWN)) { ch_psel = (ch_psel + 1) % cnt; ch_pconfirm = 0; }
        if (fire && (nav & PAD_BUTTON_UP)) { ch_psel = (ch_psel + cnt - 1) % cnt; ch_pconfirm = 0; }
        if (down & PAD_BUTTON_A) { if (ch_view == CHV_FILE) ch_file_action(ch_psel); else ch_line_action(ch_psel); }
        if (down & (PAD_BUTTON_B | PAD_BUTTON_X)) { ch_pop = 0; ch_pconfirm = 0; }
        return;
    }
    if (ch_view == CHV_LIST) {
        int rows = ch_nf + 1;
        if (fire && (nav & PAD_BUTTON_DOWN)) { ch_fsel++; ch_fconfirm = 0; }
        if (fire && (nav & PAD_BUTTON_UP)) { ch_fsel--; ch_fconfirm = 0; }
        if (fire && (nav & PAD_BUTTON_RIGHT)) ch_fsel += 12;
        if (fire && (nav & PAD_BUTTON_LEFT)) ch_fsel -= 12;
        if (ch_fsel > rows - 1) ch_fsel = rows - 1;
        if (ch_fsel < 0) ch_fsel = 0;
        if (down & PAD_BUTTON_A) {
            ch_fconfirm = 0;
            if (ch_fsel == 0) kb_start(KB_CHEATID, "", -1, -1);
            else { char p[NAME_LEN]; snprintf(p, sizeof(p), "%s/%s", CH_DIR, ch_fn[ch_fsel - 1]); cheats_open_file(p); }
        }
        if ((down & PAD_BUTTON_X) && ch_fsel > 0) {
            if (!ch_fconfirm) { ch_fconfirm = 1; ch_note("X encore une fois pour supprimer ce fichier"); }
            else {
                char p[NAME_LEN]; snprintf(p, sizeof(p), "%s/%s", CH_DIR, ch_fn[ch_fsel - 1]);
                ch_fconfirm = 0;
                if (remove(p) == 0) ch_note("Fichier effac\xC3\xA9"); else ch_note("Impossible d'effacer ce fichier");
                ch_scan();
            }
        }
        if (down & PAD_BUTTON_B) { ch_on = 0; fe_load(); }
        return;
    }
    if (ch_view == CHV_FILE) {
        int rows = ch_nc + 1;
        if (fire && (nav & PAD_BUTTON_DOWN)) ch_csel++;
        if (fire && (nav & PAD_BUTTON_UP)) ch_csel--;
        if (fire && (nav & PAD_BUTTON_RIGHT)) ch_csel += 11;
        if (fire && (nav & PAD_BUTTON_LEFT)) ch_csel -= 11;
        if (ch_csel > rows - 1) ch_csel = rows - 1;
        if (ch_csel < 0) ch_csel = 0;
        if (fire && (nav & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT))) ch_quit_warn = 0;
        if (down & PAD_BUTTON_A) {
            if (ch_csel >= ch_nc) { ch_kb_target = -1; kb_start(KB_CHEATNAME, "", -1, -1); }
            else if (ch_swn[ch_csel] || ch_swbad[ch_csel]) ch_toggle(ch_csel);    /* v31 : A = activer / desactiver */
            else { ch_view = CHV_CODES; ch_lsel = 0; ch_edit = 0; }
        }
        if ((down & PAD_BUTTON_X) && ch_csel < ch_nc) { ch_pop = 1; ch_psel = 0; ch_pconfirm = 0; }
        if (down & PAD_BUTTON_Y) ch_save();
        if (down & PAD_BUTTON_B) ch_leave_file();
        return;
    }
    /* CHV_CODES : les lignes d'un cheat */
    if (ch_csel >= ch_nc) { ch_view = CHV_FILE; return; }
    int f = ch_first_line(ch_csel), n = ch_ce[ch_csel] - f;
    if (ch_edit) {                                        /* croix : chiffre par chiffre */
        if (ch_lsel >= n) { ch_edit = 0; return; }
        char *ln = ch_l[f + ch_lsel];
        if (fire && (nav & PAD_BUTTON_LEFT)) ch_dig = (ch_dig + 15) % 16;
        if (fire && (nav & PAD_BUTTON_RIGHT)) ch_dig = (ch_dig + 1) % 16;
        if (fire && (nav & PAD_BUTTON_UP)) ch_digit_step(ln, ch_dig, 1);
        if (fire && (nav & PAD_BUTTON_DOWN)) ch_digit_step(ln, ch_dig, -1);
        if (down & (PAD_BUTTON_A | PAD_BUTTON_B)) ch_edit = 0;
        if (down & PAD_BUTTON_Y) { ch_edit = 0; ch_save(); }
        return;
    }
    int rows = n + 1;
    if (fire && (nav & PAD_BUTTON_DOWN)) ch_lsel++;
    if (fire && (nav & PAD_BUTTON_UP)) ch_lsel--;
    if (ch_lsel > rows - 1) ch_lsel = rows - 1;
    if (ch_lsel < 0) ch_lsel = 0;
    if (down & PAD_BUTTON_A) {
        if (ch_lsel >= n) ch_line_action(CHL_ADD);
        else if (ch_is_code(ch_l[f + ch_lsel]) || ch_codeish(ch_l[f + ch_lsel])) { ch_edit = 1; ch_dig = 0; }
        else { ch_kb_target = f + ch_lsel; kb_start(KB_CHEATCODE, ch_l[f + ch_lsel], -1, -1); }
    }
    if (down & PAD_BUTTON_X) { ch_pop = 1; ch_psel = 0; ch_pconfirm = 0; }
    if (down & PAD_BUTTON_Y) ch_save();
    if (down & PAD_BUTTON_B) { ch_view = CHV_FILE; ch_index(); }
}

/* -------- affichage -------- */
static void ch_draw_code(f32 x, f32 base, const char *ln, int cursor, col_t c, col_t cc) {
    /* chiffres a pas fixe, pour que le curseur tombe pile */
    const f32 step = 14;
    char one[2] = { 0, 0 };
    int k = 0;
    for (int i = 0; ln[i] && i < 17; i++) {
        if (ln[i] == ' ') { x += step; continue; }
        one[0] = ln[i];
        if (k == cursor) {
            gx_rrect_fill(x - 2, base - 17, step, 22, 4, RGBA(255, 210, 120, 255), RGBA(230, 160, 60, 255));
            gx_text1(F_MED, x, base, one, RGBA(30, 16, 60, 255));
        } else gx_text1(F_MED, x, base, one, c);
        x += step; k++;
    }
    (void)cc;
}

static void draw_cheats(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    draw_panel(11, 73, 618, 338);
    int tx = draw_tabs(NULL);
    const char *where = ch_view == CHV_LIST ? "Cheats : SD:/gc-mp3/cheats" : ch_name;
    if (ch_view == CHV_CODES && ch_csel < ch_nc) {
        text_display(ch_cname(ch_csel), d, sizeof(d));
        snprintf(buf, sizeof(buf), "%s  >  %s", ch_name, d);
        fit_text(F_SMALL, buf, 600 - tx, d, sizeof(d));
        where = d;
    }
    gx_text1(F_SMALL, tx + 6, 99, where, RGBA(200, 190, 240, 255));
    if (ch_view != CHV_LIST && ch_dirty) gx_text_r(F_SMALL, 614, 99, "modifi\xC3\xA9", RGBA(255, 210, 120, 255));
    const col_t sel1 = RGBA(116, 46, 236, 255), sel2 = RGBA(74, 22, 172, 255);
    const int vis = 11;
    if (ch_view == CHV_LIST) {
        int rows = ch_nf + 1, start = ch_fsel - vis / 2;
        if (start > rows - vis) start = rows - vis;
        if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < rows; i++) {
            int r = start + i;
            f32 ry = 114.0f + i * 24.0f, cy = ry + 12.0f;
            int on = r == ch_fsel;
            gp_zone(18, ry + 1, 604, 22, &ch_fsel, r, GPZ_SEL);
            if (on) gx_rrect_fill(20, ry + 1, 600, 22, 5, sel1, sel2);
            col_t txt = on ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
            if (r == 0) { gx_text1(F_MED, 34, cy + 5, "+ Nouveau fichier (ID du jeu, ex. GALE01)", on ? txt : RGBA(255, 210, 120, 255)); continue; }
            draw_note(36, cy, on ? txt : RGBA(120, 100, 190, 255));
            text_display(ch_fn[r - 1], d, sizeof(d));
            fit_text_sel(on, F_MED, d, 470, buf, sizeof(buf));
            gx_text1(F_MED, 52, cy + 5, buf, txt);
            if (ch_fsz[r - 1] < 0) {                       /* (taille : une seule fois, a l'affichage) */
                char p[NAME_LEN]; struct stat st;
                snprintf(p, sizeof(p), "%s/%s", CH_DIR, ch_fn[r - 1]);
                ch_fsz[r - 1] = stat(p, &st) == 0 ? (long)st.st_size : 0;
            }
            fmt_size(buf, sizeof(buf), ch_fsz[r - 1]);
            gx_text_r(F_SMALL, 612, cy + 4, buf, on ? RGBA(235, 225, 255, 255) : RGBA(140, 125, 200, 255));
            if (ch_fen && ch_fen[r - 1] > 0) {
                snprintf(d, sizeof(d), ch_fen[r - 1] == 1 ? "1 actif" : "%d actifs", ch_fen[r - 1]);
                gx_text_r(F_SMALL, 540, cy + 4, d, on ? RGBA(200, 255, 200, 255) : RGBA(120, 220, 140, 255));
            }
        }
        if (!ch_nf) {
            gx_text1(F_SMALL, 34, 172, "Aucun fichier de cheats pour l'instant.", RGBA(170, 155, 225, 255));
            gx_text1(F_SMALL, 34, 190, "Un fichier par jeu, nomm\xC3\xA9 avec l'ID du jeu (affich\xC3\xA9 dans la fiche du jeu), ex. GALE01.txt", RGBA(170, 155, 225, 255));
        }
    } else if (ch_view == CHV_FILE) {
        int rows = ch_nc + 1, start = ch_csel - vis / 2;
        if (start > rows - vis) start = rows - vis;
        if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < rows; i++) {
            int r = start + i;
            f32 ry = 114.0f + i * 24.0f, cy = ry + 12.0f;
            int on = r == ch_csel;
            gp_zone(18, ry + 1, 604, 22, &ch_csel, r, GPZ_SEL);
            if (on) gx_rrect_fill(20, ry + 1, 600, 22, 5, sel1, sel2);
            col_t txt = on ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
            if (r == ch_nc) { gx_text1(F_MED, 34, cy + 5, "+ Ajouter un cheat", on ? txt : RGBA(255, 210, 120, 255)); continue; }
            int bad = 0, nc = ch_ncodes(r, &bad);
            f32 nx = 34;
            if (ch_swn[r] || ch_swbad[r]) {                   /* case a cocher (v31) */
                int en = ch_en[r] && ch_swn[r];
                gx_rrect_fill(30, cy - 8, 16, 16, 4, RGBA(200, 190, 240, 255), RGBA(160, 150, 210, 255));
                gx_rrect_fill(32, cy - 6, 12, 12, 3, en ? RGBA(80, 220, 120, 255) : RGBA(24, 14, 60, 255), en ? RGBA(40, 170, 80, 255) : RGBA(16, 10, 44, 255));
                if (en) gx_rrect_fill(35, cy - 3, 6, 6, 2, RGBA(240, 255, 240, 255), RGBA(220, 255, 220, 255));
                if (ch_swbad[r] && !ch_swn[r]) gx_rrect_fill(34, cy - 1, 8, 2, 1, RGBA(255, 140, 140, 255), RGBA(255, 140, 140, 255));
                nx = 54;
            }
            text_display(ch_cname(r), d, sizeof(d));
            fit_text_sel(on, F_MED, d, 440 - (nx - 34), buf, sizeof(buf));
            gx_text1(F_MED, nx, cy + 5, buf, txt);
            if (!nc && r == 0 && !bad) snprintf(d, sizeof(d), "en-t\xC3\xAAte (ID et nom du jeu)");
            else if (bad) snprintf(d, sizeof(d), "%d ligne(s) fausse(s)", bad);
            else if (ch_swbad[r] && !ch_swn[r]) snprintf(d, sizeof(d), "%s", TR("non pris en charge"));
            else if (ch_en[r] && ch_swn[r]) snprintf(d, sizeof(d), nc == 1 ? "activ\xC3\xA9 - 1 ligne" : "activ\xC3\xA9 - %d lignes", nc);
            else snprintf(d, sizeof(d), nc == 1 ? "1 ligne" : "%d lignes", nc);
            gx_text_r(F_SMALL, 612, cy + 4, d, bad ? RGBA(255, 140, 140, 255) : on ? RGBA(235, 225, 255, 255) : RGBA(140, 125, 200, 255));
        }
        if (!ch_nc) gx_text1(F_SMALL, 34, 172, "Fichier vide : A sur \xC2\xAB + Ajouter un cheat \xC2\xBB", RGBA(170, 155, 225, 255));
    } else if (ch_csel < ch_nc) {
        int f = ch_first_line(ch_csel), n = ch_ce[ch_csel] - f;
        int rows = n + 1, start = ch_lsel - vis / 2;
        if (start > rows - vis) start = rows - vis;
        if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < rows; i++) {
            int r = start + i;
            f32 ry = 114.0f + i * 24.0f, cy = ry + 12.0f;
            int on = r == ch_lsel;
            gp_zone(18, ry + 1, 604, 22, &ch_lsel, r, GPZ_SEL);
            if (on) gx_rrect_fill(20, ry + 1, 600, 22, 5, ch_edit ? RGBA(70, 40, 140, 255) : sel1, ch_edit ? RGBA(50, 26, 110, 255) : sel2);
            col_t txt = on ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
            if (r == n) { gx_text1(F_MED, 34, cy + 5, "+ Ajouter une ligne", on ? txt : RGBA(255, 210, 120, 255)); continue; }
            const char *ln = ch_l[f + r];
            snprintf(buf, sizeof(buf), "%d", r + 1);
            gx_text_r(F_SMALL, 52, cy + 4, buf, RGBA(140, 125, 200, 255));
            if (ch_is_code(ln) || (on && ch_edit)) {
                char nrm[CH_LW];
                ch_norm_code(ln, nrm, sizeof(nrm));
                ch_draw_code(66, cy + 6, nrm, on && ch_edit ? ch_dig : -1, txt, txt);
            } else {
                text_display(ln, d, sizeof(d));
                fit_text_sel(on, F_MED, d, 530, buf, sizeof(buf));
                gx_text1(F_MED, 66, cy + 5, buf, (ch_codeish(ln) || ch_ncodes(ch_csel, NULL)) ? RGBA(255, 140, 140, 255) : RGBA(190, 220, 255, 255));
            }
        }
        if (ch_edit) gx_text1(F_SMALL, 330, 404 - 18, "Gauche / Droite : chiffre   Haut / Bas : valeur", RGBA(190, 180, 235, 255));
    }
    if (now_ms() - ch_msg_t < 4000) gx_text1(F_SMALL, 30, 404, ch_msg, RGBA(255, 210, 120, 255));
    if (ch_pop) {
        int cnt = ch_view == CHV_FILE ? CHP_COUNT : CHL_COUNT;
        const char *const *nm = ch_view == CHV_FILE ? chp_names : chl_names;
        const f32 w = 290, h = 46 + cnt * 24, x = 330, y = 130;
        gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
        draw_panel(x, y, w, h);
        for (int a = 0; a < cnt; a++) {
            f32 ry = y + 14 + a * 24;
            gp_zone(x + 10, ry, w - 20, 22, &ch_psel, a, GPZ_SEL);
            if (a == ch_psel) gx_rrect_fill(x + 10, ry, w - 20, 22, 5, sel1, sel2);
            int conf = ch_view == CHV_FILE && a == CHP_DEL && ch_pconfirm && a == ch_psel;
            gx_text1(F_MED, x + 22, ry + 17, conf ? "Confirmer : A pour supprimer" : nm[a], conf ? RGBA(255, 160, 160, 255) : RGBA(240, 232, 255, 255));
        }
        gx_text_c(F_SMALL, x + w / 2, y + h - 8, "A : valider   B : fermer", RGBA(255, 210, 120, 255));
    }
    if (ch_view == CHV_LIST) {
        static const char *const k[] = { "A", "X", "B" };
        static const char *const l[] = { "OUVRIR", "SUPPRIMER", "RETOUR" };
        draw_legend_keys(k, l, 3);
    } else if (ch_view == CHV_CODES && ch_edit) {
        static const char *const k[] = { "A", "Y" };
        static const char *const l[] = { "FINI", "ENREGISTRER" };
        draw_legend_keys(k, l, 2);
    } else if (ch_view == CHV_FILE) {
        static const char *const k[] = { "A", "X", "Y", "B" };
        static const char *const l[] = { "ACTIVER", "ACTIONS", "ENREGISTRER", "RETOUR" };
        draw_legend_keys(k, l, 4);
    } else {
        static const char *const k[] = { "A", "X", "Y", "B" };
        static const char *const l[] = { "MODIFIER", "ACTIONS", "ENREGISTRER", "RETOUR" };
        draw_legend_keys(k, l, 4);
    }
}

/* ---------- v240 : cheats d'un jeu depuis le telephone (extras73.c, demandes de /api/cheats) ----------
   Meme lecture que ch_collect : les cheats utilisables de SD:/gc-mp3/cheats/<ID>.txt (numerotes comme Swiss),
   leur nom (la ligne juste avant les codes) et le choix de <ID>.chtsel (valable si l'empreinte est la meme). */
typedef struct { char (*l)[CH_LW]; int nl, ns, cs[CH_MAXC], ce[CH_MAXC]; u64 h; } ch_raw_t;
static ch_raw_t *ch_raw_load(const char *id) {
    char p[NAME_LEN];
    snprintf(p, sizeof(p), "%s/%.6s.txt", CH_DIR, id);
    long len = 0;
    unsigned char *d = read_file(p, 1024 * 1024, &len);
    if (!d) return NULL;
    ch_raw_t *r = calloc(1, sizeof(*r));
    if (r) r->l = malloc(sizeof(*r->l) * CH_MAXL);
    if (!r || !r->l) { if (r) free(r); free(d); return NULL; }
    long i = len >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF ? 3 : 0;
    while (i < len && r->nl < CH_MAXL) {
        long st = i;
        while (i < len && d[i] != '\n') i++;
        long n = i - st;
        if (n > 0 && d[st + n - 1] == '\r') n--;
        if (n > CH_LW - 1) n = CH_LW - 1;
        memcpy(r->l[r->nl], d + st, n); r->l[r->nl][n] = 0;
        r->nl++; i++;
    }
    free(d);
    ch_hbuf hb = { NULL, 0, 0 };
    char (*l)[CH_LW] = r->l;
    for (int k = 0; k < r->nl;) {
        if (!l[k][0] || !ch_sw_iscode_raw(l[k])) { k++; continue; }
        int s0 = k, bad = !ch_sw_valid_raw(l[k]);
        k++;
        while (k < r->nl) { if (!l[k][0]) { k++; continue; } if (!ch_sw_iscode_raw(l[k])) break; if (!ch_sw_valid_raw(l[k])) bad = 1; k++; }
        if (bad) continue;
        if (r->ns < CH_MAXC) { r->cs[r->ns] = s0; r->ce[r->ns] = k; }
        r->ns++;
        for (int j = s0; j < k; j++) {
            if (!l[j][0] || !ch_sw_iscode_raw(l[j])) continue;
            const char *pp = l[j];
            u32 a = ch_hexval(&pp), b = ch_hexval(&pp);
            if (hb.cap >= 0 && hb.n + 8 > hb.cap) { int nc = hb.cap ? hb.cap * 2 : 4096; u8 *nb = realloc(hb.b, nc); if (!nb) hb.cap = -1; else { hb.b = nb; hb.cap = nc; } }
            if (hb.cap < 0) continue;
            u8 *q = hb.b + hb.n;
            q[0] = a >> 24; q[1] = a >> 16; q[2] = a >> 8; q[3] = a; q[4] = b >> 24; q[5] = b >> 16; q[6] = b >> 8; q[7] = b;
            hb.n += 8;
        }
    }
    if (hb.cap < 0) { free(hb.b); free(r->l); free(r); return NULL; }
    r->h = xxh64(hb.b ? hb.b : (const u8 *)"", (size_t)hb.n);
    free(hb.b);
    return r;
}
static void ch_raw_free(ch_raw_t *r) { if (r) { free(r->l); free(r); } }
/* {"x":1 si le fichier existe,"c":[["nom",0|1,nombre de lignes de code],...]} */
static char *ch_phone_json(const char *id) {
    sbuf_t s = { NULL, 0, 0 };
    ch_raw_t *r = ch_raw_load(id);
    sb_add(&s, r ? "{\"x\":1,\"c\":[" : "{\"x\":0,\"c\":[");
    if (r) {
        char p[NAME_LEN], m[48];
        snprintf(p, sizeof(p), "%s/%.6s.chtsel", CH_DIR, id);
        long sl = 0;
        unsigned char *sel = read_file(p, 65536, &sl);
        int good = 0;
        if (sel && sl >= 8) { u64 old = 0; for (int k = 0; k < 8; k++) old = (old << 8) | sel[sl - 8 + k]; good = old == r->h; }
        int n = r->ns < CH_MAXC ? r->ns : CH_MAXC;
        for (int c = 0; c < n; c++) {
            int s0 = r->cs[c], nc = 0;
            for (int j = s0; j < r->ce[c]; j++) if (r->l[j][0] && ch_sw_iscode_raw(r->l[j])) nc++;
            const char *nm = s0 > 0 && !ch_blank(r->l[s0 - 1]) && !ch_sw_iscode_raw(r->l[s0 - 1]) ? r->l[s0 - 1] : NULL;
            char def[24];
            if (!nm) { snprintf(def, sizeof(def), "Cheat %d", c + 1); nm = def; }
            if (c) sb_add(&s, ",");
            sb_add(&s, "[");
            x73_json_raw(&s, nm);
            snprintf(m, sizeof(m), ",%d,%d]", good && c < sl - 8 && sel[c] ? 1 : 0, nc);
            sb_add(&s, m);
        }
        free(sel);
        ch_raw_free(r);
    }
    sb_add(&s, "]}");
    return s.b;
}
/* ecrit <ID>.chtsel : un octet par cheat utilisable puis l'empreinte (comme Swiss) ; rien d'active = efface */
static void ch_phone_set(const char *id, const char *bits) {
    ch_raw_t *r = ch_raw_load(id);
    if (!r) return;
    char p[NAME_LEN], tmp[NAME_LEN + 8];
    snprintf(p, sizeof(p), "%s/%.6s.chtsel", CH_DIR, id);
    int n = r->ns, any = 0;
    u8 *sel = calloc(1, (size_t)n + 8);
    if (sel) {
        for (int c = 0; c < n && bits[c]; c++) if (bits[c] == '1') { sel[c] = 1; any = 1; }
        if (!any) remove(p);
        else {
            for (int i = 0; i < 8; i++) sel[n + i] = (u8)(r->h >> (56 - 8 * i));
            snprintf(tmp, sizeof(tmp), "%s.tmp", p);
            FILE *f = fopen(tmp, "wb");
            int ok = f && fwrite(sel, 1, (size_t)n + 8, f) == (size_t)n + 8;
            if (f && fclose(f) != 0) ok = 0;
            if (ok) { remove(p); ok = rename(tmp, p) == 0; }
            if (!ok) remove(tmp);
        }
        free(sel);
        crumb("cheats (telephone) : %.6s", id);
    }
    ch_raw_free(r);
    ch_sel_gen++;                                             /* (Mes jeux : nombre de cheats actifs a jour) */
    char nm[16];
    snprintf(nm, sizeof(nm), "%.6s.txt", id);
    if (ch_l && !strcasecmp(ch_name, nm) && !ch_dirty) { ch_sel_dirty = 0; ch_sel_load(); }   /* (editeur ouvert sur ce jeu) */
}

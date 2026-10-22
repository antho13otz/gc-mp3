// GC-MP3 v25 : « Mes jeux GameCube » (onglet JEUX) — inclus dans main.c apres cheats.c
// Liste les jeux de la carte SD (.iso, .gcm, .ciso, .nkit.iso) avec :
//  - l'ID et le nom lus dans le jeu, la banniere du disque (opening.bnr) ;
//  - les jaquettes du pack Swiss Indigo : SD:/swiss/ui/posters.pak (192x256, CMPR),
//    les captures SD:/swiss/ui/stills.pak (320x240) et SD:/swiss/ui/descriptions.txt.
// Format des packs : SWPK v1 (docs/PACKS.md d'Indigo), nombres en big-endian.
// Les jeux se lancent depuis Swiss ; ici on les voit, et X ouvre leurs cheats.

#define GL_MAX      400
#define GL_UI       SD_ROOT "/swiss/ui"
#define GL_CACHE    GCD "/cache/jeux"
#define GL_SLOTS    20                       /* jaquettes gardees en memoire (128x128) */
#define GL_BSLOTS   40                       /* bannieres gardees en memoire */

typedef struct {
    char path[NAME_LEN];
    char id[8];
    char title[72];
    long size;
    u8 disc, ciso;
    s16 post, still;                         /* position dans les packs, -1 = aucune, -2 = pas encore cherche */
    s8 bslot;                                /* banniere en memoire (-1 = pas chargee) */
    u8 bstate;                               /* 0 = a lire, 1 = lue, 2 = pas de banniere */
    char *desc;                              /* description (descriptions.txt ou banniere) */
    long key;                                /* v45 : cle de tri */
    u16 runs;                                /* v45 : nombre de lancements depuis GC-MP3 */
    u32 last;                                /* v45 : dernier lancement (secondes) */
} gcg_t;
/* v28 : 6 facons d'afficher les jeux (R / L pour changer) */
enum { GLV_GRID, GLV_CAROUSEL, GLV_LIST, GLV_WALL, GLV_SHOW, GLV_SHELF, GLV_COUNT };
static void tl_recent_add(const char *path);            /* v35 : derniers jeux lances (tools.c) */
static void indigo_game_open(const char *id, const char *title);   /* v35 : reglages du jeu (indigo_cfg.c) */
static int gl_view = GLV_GRID;
static const char *const gl_view_names[GLV_COUNT] = { "Grille", "Carrousel", "Liste", "Mur de jaquettes", "Vitrine", "\xC3\x89tag\xC3\xA8re" };
static int gl_view_cols(void) { return gl_view == GLV_WALL ? 9 : gl_view == GLV_GRID || gl_view == GLV_SHELF ? 6 : 1; }
static void gl_load_big(int k);
static void gl_stats_load(void);
static void gl_apply_sort(void);
static int ld_ready(void);
static gcg_t *gl = NULL;
static int gl_n = 0, gl_scanned = 0, gl_on = 0, gl_sel = 0, gl_detail = 0;
static char gl_msg[160];
static u64 gl_msg_t = 0;
static int gl_launch_sel;
static int gl_list_dirty = 0;
static char gl_busy[96] = "";                            /* v27 : message "en cours" affiche par-dessus */
static void gl_status(const char *m) { snprintf(gl_busy, sizeof(gl_busy), "%s", TR(m)); ui_status(m); }                            /* v27 : noms / descriptions lus dans les bannieres */
static const char *gl_launch(int k);

/* ---------- packs SWPK ---------- */
typedef struct {
    FILE *fp; int n; u8 *idx;                 /* index : n x 32 octets */
    u32 rsize; int cw, ch, iw, ih, levels;
} swpk_t;
static swpk_t gl_post, gl_still;


static void swpk_close(swpk_t *k) { if (k->fp) fclose(k->fp); free(k->idx); memset(k, 0, sizeof(*k)); }
static int swpk_open(swpk_t *k, const char *path, int cw, int ch) {
    swpk_close(k);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    u8 h[64];
    if (fread(h, 1, 64, f) != 64 || memcmp(h, "SWPK", 4) || be32(h + 4) != 1 || h[0x2D] != 14) { fclose(f); return 0; }
    int n = (int)be32(h + 0x0C);
    u32 io = be32(h + 0x10), il = be32(h + 0x14), rs = be32(h + 0x20);
    if (n < 1 || n > 4096 || io != 64 || il != (u32)n * 32 || be16(h + 0x24) != cw || be16(h + 0x26) != ch) { fclose(f); return 0; }
    u8 *idx = (u8 *)malloc(il);
    if (!idx || fread(idx, 1, il, f) != il) { free(idx); fclose(f); return 0; }
    k->fp = f; k->n = n; k->idx = idx; k->rsize = rs;
    k->cw = cw; k->ch = ch; k->iw = be16(h + 0x28); k->ih = be16(h + 0x2A); k->levels = h[0x2C];
    if (k->iw <= 0 || k->iw > cw) k->iw = cw;
    if (k->ih <= 0 || k->ih > ch) k->ih = ch;
    return 1;
}
/* l'ID complet, sinon un enregistrement "universel" des 4 premiers caracteres */
static int swpk_find(const swpk_t *k, const char *id) {
    if (!k->idx) return -1;
    int lo = 0, hi = k->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2, c = memcmp(k->idx + m * 32, id, 6);
        if (!c) return m;
        if (c < 0) lo = m + 1; else hi = m - 1;
    }
    for (int i = 0; i < k->n; i++) if (!memcmp(k->idx + i * 32, id, 4) && (k->idx[i * 32 + 6] & 1)) return i;
    return -1;
}
/* lit le niveau "level" (0 = taille reelle) d'une texture CMPR */
static u8 *swpk_read(const swpk_t *k, int r, int level, int *w, int *h) {
    if (!k->fp || r < 0 || r >= k->n || level >= (k->levels ? k->levels : 1)) return NULL;
    u32 off = be32(k->idx + r * 32 + 8);
    int lw = k->cw, lh = k->ch;
    for (int l = 0; l < level; l++) { off += (u32)lw * lh / 2; lw /= 2; lh /= 2; }
    u32 len = (u32)lw * lh / 2;
    u8 *b = (u8 *)malloc(len);
    if (!b) return NULL;
    if (fseek(k->fp, (long)off, SEEK_SET) != 0 || fread(b, 1, len, k->fp) != len) { free(b); return NULL; }
    *w = lw; *h = lh;
    return b;
}

/* CMPR (blocs DXT1 ranges par tuiles de 8x8) -> RGB5A3 en tuiles 4x4 */
static void gl_put5a3(u16 *tex, int tw, int x, int y, u16 v) {
    tex[((y >> 2) * (tw >> 2) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)] = v;
}
static u16 *gl_cmpr_to_5a3(const u8 *src, int w, int h) {
    u16 *tex = (u16 *)memalign(32, (size_t)w * h * 2);
    if (!tex) return NULL;
    const u8 *p = src;
    for (int ty = 0; ty < h; ty += 8)
        for (int tx = 0; tx < w; tx += 8)
            for (int sb = 0; sb < 4; sb++, p += 8) {
                int bx = tx + (sb & 1) * 4, by = ty + (sb >> 1) * 4;
                u16 c0 = be16(p), c1 = be16(p + 2);
                int r[4], g[4], b[4], a[4] = { 255, 255, 255, 255 };
                r[0] = (c0 >> 11) * 255 / 31; g[0] = ((c0 >> 5) & 63) * 255 / 63; b[0] = (c0 & 31) * 255 / 31;
                r[1] = (c1 >> 11) * 255 / 31; g[1] = ((c1 >> 5) & 63) * 255 / 63; b[1] = (c1 & 31) * 255 / 31;
                if (c0 > c1) {
                    r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
                    r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
                } else {
                    r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; b[2] = (b[0] + b[1]) / 2;
                    r[3] = g[3] = b[3] = 0; a[3] = 0;
                }
                for (int yy = 0; yy < 4; yy++) {
                    u8 bits = p[4 + yy];
                    for (int xx = 0; xx < 4; xx++) {
                        int i = (bits >> (6 - xx * 2)) & 3;
                        if (bx + xx < w && by + yy < h) gl_put5a3(tex, w, bx + xx, by + yy, rgb5a3(r[i], g[i], b[i], a[i]));
                    }
                }
            }
    DCFlushRange(tex, (u32)w * h * 2);
    return tex;
}

/* ---------- lecture des disques (.iso / .gcm / .ciso) ---------- */
typedef struct { FILE *fp; int ciso; u32 bs; u8 *map; u32 *pos; int nb; } gdisc_t;
static void gd_close(gdisc_t *d) { if (d->fp) fclose(d->fp); free(d->map); free(d->pos); memset(d, 0, sizeof(*d)); }
static int gd_open(gdisc_t *d, const char *path) {
    memset(d, 0, sizeof(*d));
    d->fp = fopen(path, "rb");
    if (!d->fp) return 0;
    u8 h[8];
    if (fread(h, 1, 8, d->fp) == 8 && !memcmp(h, "CISO", 4)) {
        d->ciso = 1;
        d->bs = (u32)h[4] | (u32)h[5] << 8 | (u32)h[6] << 16 | (u32)h[7] << 24;
        d->nb = 0x8000 - 8;
        d->map = (u8 *)malloc(d->nb);
        d->pos = (u32 *)malloc(sizeof(u32) * d->nb);
        if (!d->bs || d->bs > (64u << 20) || !d->map || !d->pos || fread(d->map, 1, d->nb, d->fp) != (size_t)d->nb) { gd_close(d); return 0; }
        u32 c = 0;
        for (int i = 0; i < d->nb; i++) { d->pos[i] = d->map[i] ? c++ : 0xFFFFFFFFu; }
    }
    return 1;
}
static int gd_read(gdisc_t *d, u32 off, void *out, u32 len) {
    u8 *o = (u8 *)out;
    if (!d->ciso) return fseek(d->fp, (long)off, SEEK_SET) == 0 && fread(o, 1, len, d->fp) == len;
    while (len > 0) {
        u32 b = off / d->bs, in = off % d->bs, n = d->bs - in;
        if (n > len) n = len;
        if ((int)b >= d->nb) return 0;
        if (d->pos[b] == 0xFFFFFFFFu) memset(o, 0, n);
        else if (fseek(d->fp, (long)(0x8000 + (u64)d->pos[b] * d->bs + in), SEEK_SET) != 0 || fread(o, 1, n, d->fp) != n) return 0;
        o += n; off += n; len -= n;
    }
    return 1;
}

/* Windows-1252 -> UTF-8 (textes des bannieres et des descriptions) */
static void gl_latin(const u8 *s, int n, char *out, int cap) {
    static const u16 w1252[32] = { 0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
                                   0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178 };
    int o = 0;
    for (int i = 0; i < n && s[i] && o < cap - 4; i++) {
        u32 c = s[i];
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c >= 0x80 && c < 0xA0) c = w1252[c - 0x80] ? w1252[c - 0x80] : '?';
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | c >> 6); out[o++] = (char)(0x80 | (c & 63)); }
        else { out[o++] = (char)(0xE0 | c >> 12); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
    }
    out[o] = 0;
    while (o > 0 && out[o - 1] == ' ') out[--o] = 0;
}
static int gl_id_ok(const char *id) {
    for (int i = 0; i < 6; i++) if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))) return 0;
    return 1;
}

/* ---------- liste des jeux ---------- */
static int gl_cmp(const void *a, const void *b) {
    const gcg_t *x = (const gcg_t *)a, *y = (const gcg_t *)b;
    int c = strcasecmp(x->title, y->title);
    return c ? c : x->disc - y->disc;
}
static void gl_add(const char *path, long size) {
    if (gl_n >= GL_MAX) return;
    gdisc_t d;
    if (!gd_open(&d, path)) return;
    u8 h[0x60];
    int ok = gd_read(&d, 0, h, sizeof(h));
    int ciso = d.ciso;
    gd_close(&d);
    if (!ok || be32(h + 0x1C) != 0xC2339F3Du) return;     /* (mot magique des disques GameCube) */
    gcg_t *g = &gl[gl_n];
    memset(g, 0, sizeof(*g));
    snprintf(g->path, sizeof(g->path), "%s", path);
    memcpy(g->id, h, 6); g->id[6] = 0;
    if (!gl_id_ok(g->id)) return;
    g->disc = h[6];
    g->ciso = (u8)ciso;
    g->size = size;
    if (g->id[3] == 'J') snprintf(g->title, sizeof(g->title), "%s", g->id);   /* (japonais : Shift-JIS) */
    else gl_latin(h + 0x20, 0x40, g->title, sizeof(g->title));
    if (!g->title[0]) snprintf(g->title, sizeof(g->title), "%s", g->id);
    g->post = g->still = -2; g->bslot = -1;
    gl_n++;
}
static int gl_skip_dir(const char *name) {
    static const char *const sk[] = { "gc-mp3", "mp3", "downloads", "photos", "videos", "ecran", "retro", "radio", "podcasts", "swiss", "System Volume Information", NULL };
    if (name[0] == '.') return 1;
    for (int i = 0; sk[i]; i++) if (!strcasecmp(name, sk[i])) return 1;
    return 0;
}
static void gl_walk(const char *dir, int depth, int *budget) {
    DIR *dp = opendir(dir);
    if (!dp) return;
    struct dirent *e;
    char p[NAME_LEN];
    while ((e = readdir(dp)) != NULL && gl_n < GL_MAX && (*budget)-- > 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (snprintf(p, sizeof(p), "%s/%s", dir, e->d_name) >= (int)sizeof(p)) continue;
        if (is_dir(p, e)) { if (depth < 3 && !gl_skip_dir(e->d_name)) gl_walk(p, depth + 1, budget); continue; }
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || !(!strcasecmp(dot, ".iso") || !strcasecmp(dot, ".gcm") || !strcasecmp(dot, ".ciso"))) continue;
        struct stat st;
        if (stat(p, &st) != 0 || st.st_size < 0x440) continue;
        gl_add(p, (long)st.st_size);
        service_audio();
    }
    closedir(dp);
}
/* descriptions.txt : "ID description" par ligne (premiere ligne gagnante) */
static void gl_load_desc(void) {
    long len = 0;
    unsigned char *d = read_file(GL_UI "/descriptions.txt", 1024 * 1024, &len);
    if (!d) return;
    char tmp[600];
    for (int pass = 0; pass < 2; pass++) {                 /* 1 : ID exact ; 2 : meme jeu, autre disque / editeur */
        long i = 0;
        while (i < len) {
            long s = i;
            while (i < len && d[i] != '\n') i++;
            long n = i - s; i++;
            if (n < 8 || d[s] == '#' || (d[s + 6] != ' ' && d[s + 6] != '\t')) continue;
            char id[7]; memcpy(id, d + s, 6); id[6] = 0;
            if (!gl_id_ok(id)) continue;
            for (int k = 0; k < gl_n; k++) {
                gcg_t *g = &gl[k];
                if (g->desc || (pass == 0 ? memcmp(g->id, id, 6) : memcmp(g->id, id, 4))) continue;
                long m = n - 7; if (m > 590) m = 590;
                gl_latin(d + s + 7, (int)m, tmp, sizeof(tmp));
                g->desc = strdup(tmp);
            }
        }
    }
    free(d);
}
static void gl_free_list(void);
/* v27 : liste gardee dans SD:/gc-mp3/cache/jeux.txt (ouverture immediate ;
   Y = chercher a nouveau sur toute la carte). Une ligne par jeu :
   chemin <TAB> taille <TAB> ID <TAB> disque <TAB> ciso <TAB> nom <TAB> description */
#define GL_LIST GL_CACHE "/liste.txt"
static void gl_list_save(void) {
    mkdir(GCD, 0777); mkdir(GCD "/cache", 0777); mkdir(GL_CACHE, 0777);
    FILE *f = fopen(GL_LIST, "w");
    if (!f) return;
    fprintf(f, "GLC1\n");
    for (int k = 0; k < gl_n; k++)
        fprintf(f, "%s\t%ld\t%s\t%u\t%u\t%s\t%s\n", gl[k].path, gl[k].size, gl[k].id, gl[k].disc, gl[k].ciso, gl[k].title, gl[k].desc ? gl[k].desc : "");
    fclose(f);
}
static int gl_list_load(void) {
    long len = 0;
    unsigned char *d = read_file(GL_LIST, 2 * 1024 * 1024, &len);
    if (!d) return 0;
    if (len < 5 || memcmp(d, "GLC1\n", 5)) { free(d); return 0; }
    char *p = (char *)d + 5, *end = (char *)d + len;
    while (p < end && gl_n < GL_MAX) {
        char *nl = memchr(p, '\n', end - p);
        if (!nl) nl = end;
        *nl = 0;
        char *f[7]; int n = 0;
        f[n++] = p;
        for (char *c = p; *c && n < 7; c++) if (*c == '\t') { *c = 0; f[n++] = c + 1; }
        if (n >= 6 && strlen(f[2]) == 6 && gl_id_ok(f[2])) {
            gcg_t *g = &gl[gl_n];
            memset(g, 0, sizeof(*g));
            snprintf(g->path, sizeof(g->path), "%s", f[0]);
            g->size = atol(f[1]);
            snprintf(g->id, sizeof(g->id), "%s", f[2]);
            g->disc = (u8)atoi(f[3]); g->ciso = (u8)atoi(f[4]);
            snprintf(g->title, sizeof(g->title), "%s", f[5]);
            if (n >= 7 && f[6][0]) g->desc = strdup(f[6]);
            g->post = g->still = -2; g->bslot = -1;
            gl_n++;
        }
        p = nl + 1;
    }
    free(d);
    return 1;
}
static void gl_scan_full(int force) {
    if (!gl) gl = (gcg_t *)calloc(GL_MAX, sizeof(gcg_t));
    gl_free_list();
    if (!gl || !sd_ok) { gl_scanned = 1; return; }
    int cached = !force && gl_list_load();
    if (!cached) {
        gl_status("Recherche des jeux GameCube sur la carte SD...");
        int budget = 6000;
        gl_walk(SD_ROOT, 0, &budget);
        qsort(gl, gl_n, sizeof(gcg_t), gl_cmp);
        gl_load_desc();
    }
    swpk_open(&gl_post, GL_UI "/posters.pak", 256, 256);
    swpk_open(&gl_still, GL_UI "/stills.pak", 320, 240);
    for (int k = 0; k < gl_n; k++) { gl[k].post = (s16)swpk_find(&gl_post, gl[k].id); gl[k].still = (s16)swpk_find(&gl_still, gl[k].id); }
    if (!cached) gl_list_save();
    gl_stats_load();                                     /* v45 : lancements, puis tri choisi */
    gl_apply_sort();
    gl_busy[0] = 0;
    gl_scanned = 1;
    if (gl_sel >= gl_n) gl_sel = gl_n ? gl_n - 1 : 0;
}
static void gl_scan(void) { gl_scan_full(1); }

static void gl_slots_reset(void);
/* ---------- v45 : lancements (SD:/gc-mp3/jeux_stats.txt : ID|disque|nombre|date) et tri ---------- */
#define GL_STATS GCD "/jeux_stats.txt"
static void gl_stats_load(void) {
    for (int k = 0; k < gl_n; k++) { gl[k].runs = 0; gl[k].last = 0; }
    long n = 0;
    char *d = (char *)read_file(GL_STATS, 64 * 1024, &n);
    if (!d) return;
    char *t = (char *)realloc(d, n + 1);
    if (!t) { free(d); return; }
    t[n] = 0;
    for (char *l = t; l && *l; ) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char id[8] = ""; int disc = 0; unsigned runs = 0, last = 0;
        if (sscanf(l, "%6[^|]|%d|%u|%u", id, &disc, &runs, &last) == 4)
            for (int k = 0; k < gl_n; k++)
                if (!strcmp(gl[k].id, id) && gl[k].disc == disc) { gl[k].runs = (u16)(runs > 65535 ? 65535 : runs); gl[k].last = last; }
        l = e ? e + 1 : NULL;
    }
    free(t);
}
static void gl_stats_bump(int k) {
    if (k < 0 || k >= gl_n || !sd_ok) return;
    gl[k].runs = gl[k].runs < 65535 ? gl[k].runs + 1 : 65535;
    gl[k].last = (u32)time(NULL);
    long n = 0;
    char *d = (char *)read_file(GL_STATS, 64 * 1024, &n);
    mkdir(GCD, 0777);
    FILE *f = fopen(GL_STATS ".tmp", "wb");
    if (!f) { free(d); return; }
    fprintf(f, "%s|%d|%u|%u\n", gl[k].id, gl[k].disc, (unsigned)gl[k].runs, (unsigned)gl[k].last);
    char me[16];
    snprintf(me, sizeof(me), "%s|%d|", gl[k].id, gl[k].disc);
    for (long i = 0; d && i < n; ) {                     /* les autres jeux, tels quels */
        long s = i;
        while (i < n && d[i] != '\n') i++;
        if (i - s > 0 && (i - s < (long)strlen(me) || memcmp(d + s, me, strlen(me)))) { fwrite(d + s, 1, i - s, f); fputc('\n', f); }
        i++;
    }
    free(d);
    if (fclose(f) == 0) { remove(GL_STATS); rename(GL_STATS ".tmp", GL_STATS); }
}
static int gl_cmp_key(const void *a, const void *b) {
    const gcg_t *x = (const gcg_t *)a, *y = (const gcg_t *)b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return gl_cmp(a, b);
}
/* trie la liste (le jeu choisi reste choisi) ; les images en memoire sont relues */
static void gl_apply_sort(void) {
    if (gl_n <= 0) return;
    char keep[NAME_LEN];
    snprintf(keep, sizeof(keep), "%s", gl[gl_sel >= 0 && gl_sel < gl_n ? gl_sel : 0].path);
    for (int k = 0; k < gl_n; k++) {
        gcg_t *g = &gl[k];
        char r = g->id[3];
        switch (gl_sort) {
        case 1: g->key = -(long)g->last; break;
        case 2: g->key = -(g->size >> 10); break;
        case 3: g->key = r == 'P' ? 0 : r == 'F' || r == 'D' || r == 'S' || r == 'I' ? 1 : r == 'E' ? 2 : r == 'J' ? 3 : 4; break;
        default: g->key = 0; break;
        }
    }
    qsort(gl, gl_n, sizeof(gcg_t), gl_cmp_key);
    gl_slots_reset();
    for (int k = 0; k < gl_n; k++) { gl[k].bslot = -1; if (!strcmp(gl[k].path, keep)) gl_sel = k; }
}

/* ---------- jaquettes et bannieres en memoire ---------- */
typedef struct { int game; u16 *tex; GXTexObj obj; u64 used; } glslot_t;
static glslot_t gl_cov[GL_SLOTS], gl_ban[GL_BSLOTS];
static struct { int game; u16 *tex; GXTexObj obj; int kind; } gl_big = { -1, NULL, {{0}}, 0 }, gl_bigst = { -1, NULL, {{0}}, 0 };
static void gl_slots_reset(void) {
    for (int i = 0; i < GL_SLOTS; i++) { free(gl_cov[i].tex); gl_cov[i].tex = NULL; gl_cov[i].game = -1; }
    for (int i = 0; i < GL_BSLOTS; i++) { free(gl_ban[i].tex); gl_ban[i].tex = NULL; gl_ban[i].game = -1; }
    free(gl_big.tex); gl_big.tex = NULL; gl_big.game = -1;
    free(gl_bigst.tex); gl_bigst.tex = NULL; gl_bigst.game = -1;
}
static void gl_free_list(void) {
    for (int k = 0; k < gl_n; k++) { free(gl[k].desc); gl[k].desc = NULL; }
    gl_n = 0;
    gl_slots_reset();
}
static glslot_t *gl_find_slot(glslot_t *s, int n, int game) {
    for (int i = 0; i < n; i++) if (s[i].tex && s[i].game == game) { s[i].used = now_ms(); return &s[i]; }
    return NULL;
}
static glslot_t *gl_new_slot(glslot_t *s, int n) {
    int best = 0;
    for (int i = 0; i < n; i++) { if (!s[i].tex) { best = i; break; } if (s[i].used < s[best].used) best = i; }
    free(s[best].tex); s[best].tex = NULL; s[best].game = -1;
    return &s[best];
}
static GXTexObj *gl_cover(int k, int load) {                 /* jaquette 128x256 (niveau 1 du pack) */
    glslot_t *s = gl_find_slot(gl_cov, GL_SLOTS, k);
    if (s) return &s->obj;
    if (!load || gl[k].post < 0) return NULL;
    int w, h;
    u8 *c = swpk_read(&gl_post, gl[k].post, 1, &w, &h);
    if (!c) { gl[k].post = -1; return NULL; }
    u16 *t = gl_cmpr_to_5a3(c, w, h);
    free(c);
    if (!t) return NULL;
    s = gl_new_slot(gl_cov, GL_SLOTS);
    s->tex = t; s->game = k; s->used = now_ms();
    GX_InitTexObj(&s->obj, t, w, h, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&s->obj, GX_LINEAR, GX_LINEAR);
    return &s->obj;
}
/* banniere du disque : cache SD:/gc-mp3/cache/jeux/<ID><disque>.bnr, sinon lue dans le jeu */
static void gl_banner_load(int k) {
    gcg_t *g = &gl[k];
    char cp[NAME_LEN];
    snprintf(cp, sizeof(cp), GL_CACHE "/%s%u.bnr", g->id, g->disc);
    u8 *b = NULL;
    long len = 0;
    unsigned char *c = read_file(cp, 0x2000, &len);
    if (c && len >= 0x1820 + 0x140 && !memcmp(c, "BNR", 3)) b = c;
    else {
        free(c);
        gdisc_t d;
        if (gd_open(&d, g->path)) {
            u8 h[0x440];
            if (gd_read(&d, 0, h, sizeof(h))) {
                u32 fo = be32(h + 0x424), fs = be32(h + 0x428);
                u8 *fst = (fs >= 12 && fs < (4u << 20)) ? (u8 *)malloc(fs) : NULL;
                if (fst && gd_read(&d, fo, fst, fs)) {
                    u32 ne = be32(fst + 8);
                    if (ne > 0 && (u64)ne * 12 <= fs) {
                        const char *names = (const char *)fst + ne * 12;
                        u32 nl = fs - ne * 12;
                        for (u32 i = 1; i < ne; i++) {
                            const u8 *e = fst + i * 12;
                            u32 no = be32(e) & 0xFFFFFF;
                            if (e[0] != 0 || no + 12 > nl) continue;
                            if (!strncasecmp(names + no, "opening.bnr", 12)) {
                                u32 bo = be32(e + 4), bl = be32(e + 8);
                                if (bl >= 0x1960) {
                                    b = (u8 *)malloc(0x1960);
                                    if (b && !gd_read(&d, bo, b, 0x1960)) { free(b); b = NULL; }
                                    if (b && memcmp(b, "BNR", 3)) { free(b); b = NULL; }
                                }
                                break;
                            }
                        }
                    }
                }
                free(fst);
            }
            gd_close(&d);
        }
        if (b) {                                             /* garde-la pour la prochaine fois */
            mkdir(GCD, 0777); mkdir(GCD "/cache", 0777); mkdir(GL_CACHE, 0777);
            FILE *f = fopen(cp, "wb");
            if (f) { fwrite(b, 1, 0x1960, f); fclose(f); }
        }
    }
    if (!b) { g->bstate = 2; return; }
    g->bstate = 1;
    if (!g->desc && g->id[3] != 'J') {                     /* description de la banniere */
        char t[400];
        gl_latin(b + 0x1820 + 0xC0, 0x80, t, sizeof(t));
        if (t[0]) { g->desc = strdup(t); gl_list_dirty = 1; }
        char full[96];
        gl_latin(b + 0x1820 + 0x40, 0x40, full, sizeof(full));
        if (full[0] && strcmp(full, g->title)) { snprintf(g->title, sizeof(g->title), "%s", full); gl_list_dirty = 1; }
    }
    u16 *t = (u16 *)memalign(32, 96 * 32 * 2);
    if (t) {
        for (int i = 0; i < 96 * 32; i++) t[i] = be16(b + 0x20 + i * 2);
        DCFlushRange(t, 96 * 32 * 2);
        glslot_t *s = gl_new_slot(gl_ban, GL_BSLOTS);
        s->tex = t; s->game = k; s->used = now_ms();
        GX_InitTexObj(&s->obj, t, 96, 32, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&s->obj, GX_LINEAR, GX_LINEAR);
        g->bslot = (s8)(s - gl_ban);
    }
    free(b);
}
static GXTexObj *gl_banner(int k) {
    glslot_t *s = gl_find_slot(gl_ban, GL_BSLOTS, k);
    return s ? &s->obj : NULL;
}

/* v42 : deplacement dans une grille, en boucle (deroulement infini) : dx = +-1, dy = +-1 ligne */
static int nav_wrap(int sel, int n, int cols, int dx, int dy) {
    if (n <= 0) return 0;
    if (cols < 1) cols = 1;
    if (dx) sel = ((sel + dx) % n + n) % n;
    if (dy > 0) { sel += cols; if (sel >= n) sel = (sel - cols) % cols; }       /* (en bas : retour en haut, meme colonne) */
    if (dy < 0) {
        sel -= cols;
        if (sel < 0) { int c = sel + cols, last = ((n - 1) / cols) * cols + c; if (last >= n) last -= cols; sel = last < 0 ? 0 : last; }
    }
    if (sel >= n) sel = n - 1;
    return sel;
}
/* v42 : defilement doux (en lignes) */
static f32 ease_rows(f32 *v, f32 target) {
    if (!gl_anim) { *v = target; return *v; }               /* v45 : animations coupees */
    if (*v < -1000.0f || fabsf(target - *v) > 3.5f) *v = target;
    else { *v += (target - *v) * (1.0f - expf(-ui_dt * 16.0f)); if (fabsf(target - *v) < 0.01f) *v = target; }
    return *v;
}
static void gl_note(const char *m) { snprintf(gl_msg, sizeof(gl_msg), "%s", TR(m)); gl_msg_t = now_ms(); }

/* ---------- entree ---------- */
static void gclib_open(void) {
    gl_on = 1; gl_detail = 0;
    if (!gl_scanned) gl_scan_full(0);                      /* (liste gardee : tout de suite) */
    crumb("mes jeux");
}
static void gl_open_cheats(int k) {
    char p[NAME_LEN];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%s.txt", CH_DIR, gl[k].id);
    if (stat(p, &st) != 0) {
        mkdir(SD_ROOT "/swiss", 0777); mkdir(CH_DIR, 0777);
        FILE *f = fopen(p, "wb");
        if (!f) { gl_note("Impossible d'\xC3\xA9" "crire sur la carte SD"); return; }
        fprintf(f, "%s\n%s\n", gl[k].id, gl[k].title);          /* en-tete : ID et nom du jeu */
        fclose(f);
    }
    cheats_open_file(p);
}
#define GL_COLS 6
static int gl_opt = -1;                                    /* v45 : fenetre Options (Y), ligne choisie */
static f32 gl_slide = 0;                                   /* v45 : fiche : glissement vers le jeu suivant */
static void gclib_input(u32 down, u32 nav, int fire) {
    if (gl_opt >= 0) {                                     /* v45 : Options : tri, jeu au hasard, animations, actualiser */
        if (fire && (nav & PAD_BUTTON_DOWN)) gl_opt = (gl_opt + 1) % 4;
        if (fire && (nav & PAD_BUTTON_UP)) gl_opt = (gl_opt + 3) % 4;
        int ch = (fire && (nav & PAD_BUTTON_RIGHT)) ? 1 : (fire && (nav & PAD_BUTTON_LEFT)) ? -1 : (down & PAD_BUTTON_A) ? 1 : 0;
        if (gl_opt == 0 && ch) { gl_sort = (gl_sort + ch + 4) % 4; gl_apply_sort(); settings_dirty = 1; }
        else if (gl_opt == 1 && (down & PAD_BUTTON_A) && gl_n) {
            gl_sel = (int)(lib_rand() % (u32)gl_n); gl_opt = -1; gl_detail = 1; gl_slide = gl_anim ? 1.0f : 0.0f;
            return;
        }
        else if (gl_opt == 2 && ch) { gl_anim = !gl_anim; settings_dirty = 1; }
        else if (gl_opt == 3 && (down & PAD_BUTTON_A)) {
            gl_opt = -1; gl_scanned = 0; gl_scan();
            gl_note(gl_n ? "Liste actualis\xC3\xA9" "e" : "Aucun jeu trouv\xC3\xA9");
            return;
        }
        if (down & (PAD_BUTTON_B | PAD_BUTTON_Y)) gl_opt = -1;
        return;
    }
    if (gl_launch_sel >= 0) {                              /* "Lancer ce jeu ?" */
        if (down & PAD_BUTTON_A) {
            int k = gl_launch_sel;
            gl_launch_sel = -1;
            gl_status(ld_ready() ? "Lancement du jeu..." : "Lancement de Swiss...");
            const char *e = gl_launch(k);                   /* (ne revient pas si ca marche) */
            gl_busy[0] = 0;
            if (e) gl_note(e);
        }
        if (down & PAD_BUTTON_B) gl_launch_sel = -1;
        return;
    }
    if (gl_detail) {
        if (fire && (nav & PAD_BUTTON_LEFT) && gl_n) { gl_sel = nav_wrap(gl_sel, gl_n, 1, -1, 0); if (gl_anim) gl_slide = -1.0f; }
        if (fire && (nav & PAD_BUTTON_RIGHT) && gl_n) { gl_sel = nav_wrap(gl_sel, gl_n, 1, 1, 0); if (gl_anim) gl_slide = 1.0f; }
        if (down & PAD_BUTTON_X && gl_n) gl_open_cheats(gl_sel);
        if ((down & PAD_BUTTON_Y) && gl_n) indigo_game_open(gl[gl_sel].id, gl[gl_sel].title);   /* v35 : reglages du jeu */
        if ((down & PAD_BUTTON_A) && gl_n) gl_launch_sel = gl_sel;
        if (down & PAD_BUTTON_B) gl_detail = 0;
        return;
    }
    if (fire && gl_n) {
        int cols = gl_view_cols();
        /* v42 : en boucle (du dernier au premier et inversement) */
        if (gl_view == GLV_LIST) {
            if (nav & PAD_BUTTON_DOWN) gl_sel = nav_wrap(gl_sel, gl_n, 1, 1, 0);
            if (nav & PAD_BUTTON_UP) gl_sel = nav_wrap(gl_sel, gl_n, 1, -1, 0);
            if (nav & PAD_BUTTON_RIGHT) gl_sel = gl_sel + 9 < gl_n ? gl_sel + 9 : gl_sel == gl_n - 1 ? 0 : gl_n - 1;
            if (nav & PAD_BUTTON_LEFT) gl_sel = gl_sel - 9 >= 0 ? gl_sel - 9 : gl_sel == 0 ? gl_n - 1 : 0;
        } else {
            int c = cols > 1 ? cols : 5;
            if (nav & PAD_BUTTON_RIGHT) gl_sel = nav_wrap(gl_sel, gl_n, c, 1, 0);
            if (nav & PAD_BUTTON_LEFT) gl_sel = nav_wrap(gl_sel, gl_n, c, -1, 0);
            if (nav & PAD_BUTTON_DOWN) gl_sel = cols > 1 ? nav_wrap(gl_sel, gl_n, c, 0, 1) : (gl_sel + 5 < gl_n ? gl_sel + 5 : gl_sel == gl_n - 1 ? 0 : gl_n - 1);
            if (nav & PAD_BUTTON_UP) gl_sel = cols > 1 ? nav_wrap(gl_sel, gl_n, c, 0, -1) : (gl_sel - 5 >= 0 ? gl_sel - 5 : gl_sel == 0 ? gl_n - 1 : 0);
        }
        if (gl_sel >= gl_n) gl_sel = gl_n - 1;
        if (gl_sel < 0) gl_sel = 0;
    }
    if (down & (PAD_TRIGGER_R | PAD_TRIGGER_L)) {          /* v28 : changer d'affichage */
        gl_view = (gl_view + ((down & PAD_TRIGGER_R) ? 1 : GLV_COUNT - 1)) % GLV_COUNT;
        settings_dirty = 1;
        gl_note(gl_view_names[gl_view]);
    }
    if ((down & PAD_BUTTON_A) && gl_n) gl_detail = 1;
    if ((down & PAD_BUTTON_X) && gl_n) gl_open_cheats(gl_sel);
    if (down & PAD_BUTTON_Y) gl_opt = 0;                      /* v45 : options (tri, hasard, animations, actualiser) */
    if (down & PAD_BUTTON_B) { gl_on = 0; if (gl_list_dirty) { gl_list_dirty = 0; gl_list_save(); } }
}

/* ---------- affichage ---------- */
static void gl_size_txt(long v, char *b, int cap) {
    if (v >= 1024L * 1024 * 1024) snprintf(b, cap, "%ld,%ld Go", v >> 30, ((v >> 20) & 1023) * 10 / 1024);
    else snprintf(b, cap, "%ld Mo", v >> 20);
}
/* texte sur plusieurs lignes (coupe aux espaces) ; retourne le nombre de lignes */
static int gl_wrap(int f, const char *s, f32 x, f32 y, int maxw, f32 lh, int maxl, col_t c) {
    char line[300];
    int nl = 0;
    s = TR(s);
    while (*s && nl < maxl) {
        while (*s == ' ') s++;
        int best = 0, i = 0;
        while (s[i]) {
            int j = i;
            while (s[j] && s[j] != ' ') j++;
            if (j >= (int)sizeof(line) - 4) break;
            memcpy(line, s, j); line[j] = 0;
            if (text_width(f, line, 0) > maxw && best) break;
            best = j;
            i = j;
            if (s[i] == ' ') i++;
            if (!s[j]) break;
        }
        if (!best) { best = (int)strlen(s); if (best > (int)sizeof(line) - 4) best = sizeof(line) - 4; }
        memcpy(line, s, best); line[best] = 0;
        if (nl == maxl - 1 && s[best]) {                       /* derniere ligne : "..." */
            char fit[320];
            snprintf(fit, sizeof(fit), "%s...", line);
            fit_text(f, fit, maxw, line, sizeof(line));
        }
        gx_text1(f, x, y + nl * lh, line, c);
        nl++;
        s += best;
    }
    return nl;
}
static void gl_card_banner(int k, f32 x, f32 y, f32 w, f32 h, int on) {
    /* pas de jaquette : banniere du disque + ID */
    gx_rrect_fill(x, y, w, h, 6, on ? RGBA(70, 40, 150, 255) : RGBA(34, 20, 84, 240), on ? RGBA(44, 22, 110, 255) : RGBA(20, 12, 52, 240));
    GXTexObj *b = gl_banner(k);
    if (b) draw_tex_fit(b, 96, 32, 96, 32, x + 4, y + h / 2 - 28, w - 8, 30);
    else { gx_circle(x + w / 2, y + h / 2 - 14, 18, RGBA(200, 200, 215, 255), RGBA(120, 120, 140, 255)); gx_circle(x + w / 2, y + h / 2 - 14, 4, RGBA(30, 20, 60, 255), RGBA(30, 20, 60, 255)); }
    gx_text_c(F_SMALL, x + w / 2, y + h / 2 + 18, gl[k].id, RGBA(255, 210, 120, 255));
    if (h > 160) {                                             /* (grande fiche : le nom aussi) */
        char buf[96];
        fit_text(F_SMALL, gl[k].title, (int)w - 6, buf, sizeof(buf));
        gx_text_c(F_SMALL, x + w / 2, y + h / 2 + 38, buf, RGBA(210, 200, 240, 255));
    }
}
/* ---------- v28 : 6 facons d'afficher les jeux (R pour changer) ---------- */
static int gl_loaded_frame = 0;                       /* jaquettes lues pendant cette image (3 au plus) */
/* une jaquette (ou la banniere) dans le cadre x, y, w, h */
static void gl_draw_cover(int k, f32 x, f32 y, f32 w, f32 h, int on) {
    int had = gl[k].post >= 0 && gl_find_slot(gl_cov, GL_SLOTS, k) != NULL;
    GXTexObj *cov = gl[k].post >= 0 ? gl_cover(k, gl_loaded_frame < 3) : NULL;
    if (cov && !had) gl_loaded_frame++;
    if (cov) draw_tex_fit(cov, 128, 128, 96, 128, x, y, w, h);
    else gl_card_banner(k, x, y, w, h, on);
}
static void gl_title_of(int k, char *out, int cap) {
    if (gl[k].disc) snprintf(out, cap, "%s (%d)", gl[k].title, gl[k].disc + 1); else snprintf(out, cap, "%s", gl[k].title);
}
/* v45 : suit une valeur en douceur (k = vitesse) ; sans animations : tout de suite */
static f32 gl_ease(f32 *v, f32 target, f32 k) {
    if (!gl_anim || *v < -9000.0f) *v = target;
    else { *v += (target - *v) * (1.0f - expf(-ui_dt * k)); if (fabsf(target - *v) < 0.05f) *v = target; }
    return *v;
}
/* v45 : la selection "grossit" un peu quand elle arrive sur un jeu */
static f32 gl_pop(int sel) {
    static int last = -1;
    static f32 p = 1.0f;
    if (sel != last) { last = sel; p = gl_anim ? 0.0f : 1.0f; }
    p += (1.0f - p) * (1.0f - expf(-ui_dt * 12.0f));
    return p;
}
static void gl_draw_grid(int cols, int rows, f32 cw, f32 ch, f32 sw, f32 rh, int titles) {
    char buf[NAME_LEN], d[NAME_LEN];
    f32 x0 = 320 - (cols * sw - (sw - cw)) / 2, y0 = 112;
    int row = gl_sel / cols, top = row - rows / 2;
    int nrow = (gl_n + cols - 1) / cols;
    if (top > nrow - rows) top = nrow - rows;
    if (top < 0) top = 0;
    static f32 topf = -2000.0f;                                   /* v42 : defilement doux */
    static int last_cols = 0;
    if (last_cols != cols) { topf = -2000.0f; last_cols = cols; }
    f32 tf = ease_rows(&topf, (f32)top);
    int r0 = (int)floorf(tf);
    /* v45 : le cadre de selection glisse d'un jeu a l'autre */
    static f32 hx = -10000.0f, hy = -10000.0f;
    static int hcols = 0;
    f32 tx = x0 + (gl_sel % cols) * sw, ty = y0 + (gl_sel / cols - tf) * rh;
    if (hcols != cols || fabsf(tx - hx) > sw * 3.5f || fabsf(ty - hy) > rh * 1.6f) { hx = tx; hy = ty; hcols = cols; }
    gl_ease(&hx, tx, 20.0f); gl_ease(&hy, ty, 20.0f);
    f32 pop = gl_pop(gl_sel), gr = 1.0f + 0.07f * pop, ex = cw * (gr - 1) / 2, ey = ch * (gr - 1) / 2;
    scr_scissor(0, y0 - 8, 640, rows * rh + 6);
    gx_glow(hx - ex, hy - ey, cw + 2 * ex, ch + 2 * ey, 8, RGBA(150, 80, 255, 150), 3);
    for (int r = r0; r <= r0 + rows; r++)
        for (int c = 0; c < cols; c++) {
            int k = r * cols + c;
            if (r < 0 || k >= gl_n) break;
            f32 x = x0 + c * sw, y = y0 + (r - tf) * rh;
            int on = k == gl_sel;
            gp_zone(x, y, cw, ch + (titles ? 16 : 0), &gl_sel, k, GPZ_SEL);
            if (on) gl_draw_cover(k, x - ex, y - ey, cw + 2 * ex, ch + 2 * ey, on);
            else gl_draw_cover(k, x, y, cw, ch, on);
            if (titles) {
                gl_title_of(k, d, sizeof(d));
                fit_text(F_SMALL, d, (int)cw + 6, buf, sizeof(buf));
                gx_text_c(F_SMALL, x + cw / 2, y + ch + 14 + (on ? ey : 0), buf, on ? RGBA(255, 255, 255, 255) : RGBA(180, 168, 228, 255));
            }
        }
    gx_rrect_stroke(hx - ex - 2, hy - ey - 2, cw + 2 * ex + 4, ch + 2 * ey + 4, 6, 2.0f, RGBA(255, 210, 120, 255));
    scr_viewport();
    if (nrow > rows) {                                            /* barre de defilement */
        f32 th = 270.0f * rows / nrow, ty = 116 + (270.0f - th) * tf / (f32)(nrow - rows);
        gx_rrect_fill(621, ty, 3, th, 1.5f, RGBA(150, 96, 255, 255), RGBA(110, 60, 230, 255));
    }
}
/* v45 : carrousel fluide : les jaquettes glissent (position en virgule) au lieu de sauter */
static f32 gl_tab_at(const f32 *t, f32 d) {                  /* valeur du tableau t[0..4] a la distance d */
    if (d >= 4.0f) return t[4];
    int i = (int)d;
    return t[i] + (t[i + 1] - t[i]) * (d - i);
}
static void gl_draw_carousel(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    static f32 pos = -10000.0f;
    int loop = gl_n >= 7;
    if (pos < -9000.0f || !gl_anim) pos = (f32)gl_sel;
    else {
        f32 diff = (f32)gl_sel - pos;
        if (loop) { while (diff > gl_n / 2.0f) diff -= gl_n; while (diff < -gl_n / 2.0f) diff += gl_n; }
        if (fabsf(diff) > 6.0f) diff = diff > 0 ? 6.0f : -6.0f, pos = (f32)gl_sel - diff;   /* (grand saut : on rattrape vite) */
        pos += diff * (1.0f - expf(-ui_dt * 13.0f));
        if (fabsf((f32)gl_sel - pos) < 0.003f) pos = (f32)gl_sel;
        if (loop) { while (pos < 0) pos += gl_n; while (pos >= gl_n) pos -= gl_n; }
    }
    static const f32 scl[5] = { 1.0f, 0.72f, 0.54f, 0.40f, 0.30f };
    static const f32 off[5] = { 0, 136, 220, 268, 300 };
    int base = (int)floorf(pos + 0.5f);
    /* de loin vers le centre : les plus proches par-dessus */
    int order[9], no = 0;
    for (int i = -4; i <= 4; i++) order[no++] = i;
    for (int a = 0; a < no; a++)
        for (int b2 = a + 1; b2 < no; b2++) {
            f32 da = fabsf(base + order[a] - pos), db = fabsf(base + order[b2] - pos);
            if (db > da) { int t = order[a]; order[a] = order[b2]; order[b2] = t; }
        }
    for (int oi = 0; oi < no; oi++) {
        int i = order[oi];
        f32 p = base + i - pos, ad = fabsf(p);
        if (ad > 3.6f) continue;
        int k = base + i;
        if (loop) k = (k % gl_n + gl_n) % gl_n;
        else if (k < 0 || k >= gl_n) continue;
        f32 sc = gl_tab_at(scl, ad), w = 156 * sc, h = 208 * sc;
        f32 cx = 320 + (p < 0 ? -1 : 1) * gl_tab_at(off, ad);
        f32 y = 126 + (208 - h) / 2;
        int on = ad < 0.5f;
        if (on) gx_glow(cx - w / 2, y, w, h, 10, RGBA(150, 80, 255, (u8)(160 * (1.0f - ad * 2))), 4);
        gl_draw_cover(k, cx - w / 2, y, w, h, on);
        f32 dm = ad < 1 ? ad * 100.0f : 100.0f + (ad - 1) * 40.0f;
        u8 dim = (u8)(dm > 200 ? 200 : dm);
        if (dim) gx_rrect_fill(cx - w / 2, y, w, h, 4, RGBA(10, 6, 30, dim), RGBA(10, 6, 30, dim));
        if (on) gx_rrect_stroke(cx - w / 2 - 2, y - 2, w + 4, h + 4, 6, 2.0f, RGBA(255, 210, 120, (u8)(255 * (1.0f - ad * 2))));
        gp_zone(cx - w / 2, y, w, h, &gl_sel, k, GPZ_SEL);
    }
    gl_title_of(gl_sel, d, sizeof(d));
    fit_text(F_LARGE, d, 560, buf, sizeof(buf));
    gx_text_c(F_LARGE, 320, 362, buf, RGBA(255, 255, 255, 255));
    for (int i = 0; i < gl_n && gl_n <= 40; i++)                 /* points de position */
        gx_circle(320 + (i - (gl_n - 1) / 2.0f) * 9, 382, 2.4f, i == gl_sel ? RGBA(255, 210, 120, 255) : RGBA(110, 90, 170, 255), i == gl_sel ? RGBA(255, 210, 120, 255) : RGBA(110, 90, 170, 255));
}
static void gl_draw_list(void) {
    char buf[NAME_LEN], d[NAME_LEN], sz[32];
    const int vis = 9;
    int start = gl_sel - vis / 2;
    if (start > gl_n - vis) start = gl_n - vis;
    if (start < 0) start = 0;
    static f32 sf = -2000.0f;                                     /* v42 : defilement doux */
    f32 st = ease_rows(&sf, (f32)start);
    int s0 = (int)floorf(st);
    scr_scissor(0, 112, 640, vis * 31 + 2);
    for (int i = 0; i <= vis && s0 + i < gl_n; i++) {
        int k = s0 + i;
        if (k < 0) continue;
        f32 y = 114 + (k - st) * 31;
        int on = k == gl_sel;
        gp_zone(20, y, 600, 29, &gl_sel, k, GPZ_SEL);
        if (on) gx_rrect_fill(20, y, 600, 29, 6, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        GXTexObj *cov = NULL;
        if (gl[k].post >= 0) {
            int had = gl_find_slot(gl_cov, GL_SLOTS, k) != NULL;
            cov = gl_cover(k, gl_loaded_frame < 3);
            if (cov && !had) gl_loaded_frame++;
        }
        if (cov) draw_tex_fit(cov, 128, 128, 96, 128, 26, y + 2, 19, 25);
        else { GXTexObj *b = gl_banner(k); if (b) draw_tex_fit(b, 96, 32, 96, 32, 24, y + 8, 26, 9); }
        gl_title_of(k, d, sizeof(d));
        fit_text(F_MED, d, 380, buf, sizeof(buf));
        gx_text1(F_MED, 56, y + 21, buf, on ? RGBA(255, 255, 255, 255) : RGBA(215, 205, 245, 255));
        gl_size_txt(gl[k].size, sz, sizeof(sz));
        snprintf(d, sizeof(d), "%s   %s", gl[k].id, sz);
        gx_text_r(F_SMALL, 610, y + 20, d, on ? RGBA(255, 230, 170, 255) : RGBA(170, 155, 225, 255));
    }
    scr_viewport();
}
static void gl_draw_showcase(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    gcg_t *g = &gl[gl_sel];
    if (now_ms() - last_input_ms > 150) gl_load_big(gl_sel);     /* (pas pendant le defilement) */
    /* grande image : la capture, sinon la jaquette */
    const f32 px = 24, py = 114, pw = 280, ph = 198;
    if (g->still >= 0 && gl_bigst.game == gl_sel && gl_bigst.tex) draw_tex_fit(&gl_bigst.obj, 320, 240, 320, 240, px, py, pw, ph);
    else if (g->post >= 0 && gl_big.game == gl_sel && gl_big.tex) draw_tex_fit(&gl_big.obj, 256, 256, 192, 256, px, py, pw, ph);
    else gl_card_banner(gl_sel, px + pw / 2 - 75, py, 150, ph, 1);
    gx_rrect_stroke(px - 1, py - 1, pw + 2, ph + 2, 4, 1.2f, RGBA(120, 90, 220, 255));
    gl_title_of(gl_sel, d, sizeof(d));
    int nl = gl_wrap(F_LARGE, d, 318, 136, 296, 24, 2, RGBA(255, 255, 255, 255));
    char sz[32]; gl_size_txt(g->size, sz, sizeof(sz));
    snprintf(buf, sizeof(buf), "%s   %s", g->id, sz);
    gx_text1(F_SMALL, 318, 136 + nl * 24, buf, RGBA(255, 210, 120, 255));
    if (g->desc) gl_wrap(F_SMALL, g->desc, 318, 158 + nl * 24, 296, 16, nl > 1 ? 7 : 8, RGBA(215, 205, 245, 255));
    /* bande des jaquettes en bas */
    const int n = 9; const f32 cw = 50, ch = 66, sp = 64;
    int start = gl_sel - n / 2;
    if (start > gl_n - n) start = gl_n - n;
    if (start < 0) start = 0;
    static f32 sfv = -10000.0f;                                    /* v45 : bande qui glisse */
    if (fabsf(sfv - start) > 12) sfv = (f32)start;
    f32 sf = gl_ease(&sfv, (f32)start, 14.0f);
    int s0 = (int)floorf(sf);
    f32 x0 = 320 - (n * sp - (sp - cw)) / 2;
    scr_scissor(16, 300, 608, 100);
    for (int i = -1; i <= n && s0 + i < gl_n; i++) {
        int k = s0 + i, on = k == gl_sel;
        if (k < 0) continue;
        f32 x = x0 + (k - sf) * sp, y = on ? 320 : 326;
        if (on) gx_glow(x, y, cw, ch, 6, RGBA(150, 80, 255, 150), 3);
        gl_draw_cover(k, x, y, cw, ch, on);
        if (on) gx_rrect_stroke(x - 2, y - 2, cw + 4, ch + 4, 5, 2.0f, RGBA(255, 210, 120, 255));
        gp_zone(x, y, cw, ch, &gl_sel, k, GPZ_SEL);
    }
    scr_viewport();
}
static void gl_draw_shelf(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    const int cols = 6, rows = 2; const f32 cw = 72, ch = 96, sw = 96;
    int row = gl_sel / cols, top = row > 0 ? row - 1 : 0;
    int nrow = (gl_n + cols - 1) / cols;
    if (top > nrow - rows) top = nrow - rows;
    if (top < 0) top = 0;
    f32 x0 = 320 - (cols * sw - (sw - cw)) / 2;
    static f32 tfv = -10000.0f;                                    /* v45 : les etageres defilent en douceur */
    if (fabsf(tfv - top) > 3) tfv = (f32)top;
    f32 tf = gl_ease(&tfv, (f32)top, 14.0f);
    int r0 = (int)floorf(tf);
    scr_scissor(16, 104, 608, 300);
    for (int rr = r0; rr <= r0 + rows; rr++) {
        int r = rr - top;
        f32 base = 222 + (rr - tf) * 134;                          /* dessus de la planche */
        if (rr < 0 || rr >= nrow) continue;
        gx_quad4(30, base, 610, base, RGBA(150, 100, 60, 255), RGBA(150, 100, 60, 255), RGBA(96, 60, 34, 255), RGBA(96, 60, 34, 255));
        gx_rect(30, base, 580, 2, RGBA(200, 150, 100, 255));
        gx_quad4(30, base + 8, 610, base + 8, RGBA(70, 44, 26, 255), RGBA(70, 44, 26, 255), RGBA(40, 24, 14, 255), RGBA(40, 24, 14, 255));
        gx_rect(30, base + 8, 580, 6, RGBA(70, 44, 26, 255));
        gx_quad4(30, base + 14, 610, base + 14, RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0));
        gx_rect(30, base + 14, 580, 10, RGBA(0, 0, 0, 50));
        (void)r;
        for (int c = 0; c < cols; c++) {
            int k = rr * cols + c;
            if (k >= gl_n) break;
            int on = k == gl_sel;
            f32 x = x0 + c * sw, y = base - ch - (on ? 10 : 0);
            gx_rect(x + 4, base - 3, cw - 4, 3, RGBA(0, 0, 0, 80));        /* ombre sur la planche */
            if (on) gx_glow(x, y, cw, ch, 8, RGBA(150, 80, 255, 160), 3);
            gl_draw_cover(k, x, y, cw, ch, on);
            if (on) gx_rrect_stroke(x - 2, y - 2, cw + 4, ch + 4, 5, 2.0f, RGBA(255, 210, 120, 255));
            gp_zone(x, y, cw, ch, &gl_sel, k, GPZ_SEL);
        }
    }
    scr_viewport();
    (void)buf; (void)d;
}

/* grande jaquette (niveau 0 : 256x256, dont 192x256 d'image) et capture du jeu k */
static void gl_load_big(int k) {
    gcg_t *g = &gl[k];
    if (g->post >= 0 && gl_big.game != k) {
        free(gl_big.tex); gl_big.tex = NULL; gl_big.game = k;
        int w, h; u8 *c = swpk_read(&gl_post, g->post, 0, &w, &h);
        if (c) { gl_big.tex = gl_cmpr_to_5a3(c, w, h); free(c); }
        if (gl_big.tex) { GX_InitTexObj(&gl_big.obj, gl_big.tex, 256, 256, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE); GX_InitTexObjFilterMode(&gl_big.obj, GX_LINEAR, GX_LINEAR); }
    }
    if (g->still >= 0 && gl_bigst.game != k) {
        free(gl_bigst.tex); gl_bigst.tex = NULL; gl_bigst.game = k;
        int w, h; u8 *c = swpk_read(&gl_still, g->still, 0, &w, &h);
        if (c) { gl_bigst.tex = gl_cmpr_to_5a3(c, w, h); free(c); }
        if (gl_bigst.tex) { GX_InitTexObj(&gl_bigst.obj, gl_bigst.tex, 320, 240, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE); GX_InitTexObjFilterMode(&gl_bigst.obj, GX_LINEAR, GX_LINEAR); }
    }
}
static void draw_gclib_main(void);
static void draw_gclib(void) {
    draw_gclib_main();
    if (gl_busy[0]) {                                        /* recherche / lancement en cours */
        const f32 w = 440, h = 64, x = 100, y = 200;
        gx_rrect_fill(x, y, w, h, 12, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
        draw_panel(x, y, w, h);
        gx_text_c(F_MED, x + w / 2, y + 40, gl_busy, RGBA(255, 255, 255, 255));
    }
}
static void draw_gclib_main(void) {
    gl_loaded_frame = 0;
    char buf[NAME_LEN], d[NAME_LEN];
    draw_panel(11, 73, 618, 338);
    int tx = draw_tabs(NULL);
    snprintf(buf, sizeof(buf), gl_detail ? "Mes jeux GameCube (%d)  >  fiche" : "Mes jeux GameCube (%d)", gl_n);
    gx_text1(F_SMALL, tx + 6, 99, buf, RGBA(200, 190, 240, 255));
    if (!gl_post.fp) gx_text_r(F_SMALL, 590, 99, "(pas de jaquettes : swiss/ui/posters.pak)", RGBA(150, 135, 210, 255));
    /* une banniere a lire par image, quand on ne bouge pas */
    if (gl_n && now_ms() - last_input_ms > 120) {
        int first = gl_detail ? gl_sel : (gl_sel / GL_COLS - (gl_sel / GL_COLS > 0)) * GL_COLS;
        if (first < 0) first = 0;
        for (int k = first; k < gl_n && k < first + GL_COLS * 3; k++)
            if (gl[k].bstate == 0 || (gl[k].bstate == 1 && !gl_banner(k) && (gl[k].post < 0 || gl_detail))) { gl_banner_load(k); break; }
    }
    if (!gl_n) {
        gx_text1(F_MED, 34, 150, "Aucun jeu GameCube trouv\xC3\xA9 sur la carte SD", RGBA(255, 255, 255, 255));
        gx_text1(F_SMALL, 34, 176, "Jeux reconnus : .iso, .gcm, .ciso (et .nkit.iso), dans n'importe quel dossier (3 niveaux).", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 34, 196, "Jaquettes : le pack Swiss Indigo, SD:/swiss/ui/posters.pak (+ stills.pak, descriptions.txt).", RGBA(190, 180, 235, 255));
        static const char *const k[] = { "Y", "B" };
        static const char *const l[] = { "ACTUALISER", "RETOUR" };
        draw_legend_keys(k, l, 2);
        return;
    }
    if (gl_sel >= gl_n) gl_sel = gl_n - 1;
    gcg_t *g = &gl[gl_sel];
    if (gl_detail) {
        gl_load_big(gl_sel);
        /* v45 : en changeant de jeu, la fiche glisse (gauche / droite) */
        gl_slide *= gl_anim ? expf(-ui_dt * 11.0f) : 0.0f;
        if (fabsf(gl_slide) < 0.004f) gl_slide = 0;
        const f32 ox = gl_slide * 120.0f;
        scr_scissor(16, 104, 608, 300);
        const f32 cx = 28 + ox, cy = 112, cw = 192 * 0.98f, chh = 256 * 0.98f;
        if (g->post >= 0 && gl_big.game == gl_sel && gl_big.tex) {
            gx_glow(cx, cy, cw, chh, 8, RGBA(150, 80, 255, 110), 3);
            draw_tex_fit(&gl_big.obj, 256, 256, 192, 256, cx, cy, cw, chh);
        } else gl_card_banner(gl_sel, cx, cy, cw, chh, 1);
        const f32 rx = 236 + ox * 1.4f; const int rw = 380;
        int nl = gl_wrap(F_LARGE, g->title, rx, 134, rw, 26, 2, RGBA(255, 255, 255, 255));
        f32 y = 134 + nl * 26 - 4;
        char sz[32]; gl_size_txt(g->size, sz, sizeof(sz));
        const char *reg = g->id[3] == 'E' ? "USA" : g->id[3] == 'P' ? "Europe" : g->id[3] == 'J' ? "Japon" : g->id[3] == 'F' ? "France" : g->id[3] == 'D' ? "Allemagne" : "";
        snprintf(buf, sizeof(buf), "%s   %s%s%s%s", g->id, TR(reg), reg[0] ? "   " : "", sz, g->ciso ? "   CISO" : "");
        gx_text1(F_SMALL, rx, y + 4, buf, RGBA(255, 210, 120, 255));
        if (g->disc) { snprintf(buf, sizeof(buf), "Disque %d", g->disc + 1); gx_text_r(F_SMALL, 614, y + 4, buf, RGBA(255, 210, 120, 255)); }
        text_display(g->path, d, sizeof(d));
        fit_text(F_SMALL, d, rw, buf, sizeof(buf));
        gx_text1(F_SMALL, rx, y + 22, buf, RGBA(150, 135, 210, 255));
        int has_st = g->still >= 0 && gl_bigst.game == gl_sel && gl_bigst.tex;
        f32 dy = y + 46;
        int maxl = has_st ? 3 : 11;
        if (g->desc) gl_wrap(F_SMALL, g->desc, rx, dy, rw, 17, maxl, RGBA(215, 205, 245, 255));
        else gx_text1(F_SMALL, rx, dy, g->bstate == 0 ? "Lecture de la banni\xC3\xA8re..." : "Pas de description pour ce jeu.", RGBA(170, 155, 225, 255));
        if (has_st) { f32 sw = 176, sh = 132; draw_tex_fit(&gl_bigst.obj, 320, 240, 320, 240, rx, 396 - sh - 6, sw, sh); }
        if (g->runs) {                                         /* v45 : lancements depuis GC-MP3 */
            time_t lt = (time_t)g->last;
            struct tm *tm = g->last ? localtime(&lt) : NULL;
            if (tm) snprintf(buf, sizeof(buf), "%s %u   %s %02d/%02d/%04d", TR("Lanc\xC3\xA9 :"), (unsigned)g->runs, TR("dernier :"), tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
            else snprintf(buf, sizeof(buf), "%s %u", TR("Lanc\xC3\xA9 :"), (unsigned)g->runs);
            gx_text1(F_SMALL, has_st ? rx + 186 : rx, 380, buf, RGBA(170, 155, 225, 255));
        }
        scr_viewport();
        if (now_ms() - gl_msg_t < 6000) { char m[200]; fit_text(F_SMALL, gl_msg, 586, m, sizeof(m)); gx_text1(F_SMALL, 30, 404, m, RGBA(255, 210, 120, 255)); }
        static const char *const k[] = { "A", "X", "Y", "B" };
        static const char *const l[] = { "LANCER", "CHEATS", "R\xC3\x89GLAGES", "RETOUR" };
        draw_legend_keys(k, l, 4);
        if (gl_launch_sel >= 0) {                              /* confirmation */
            const f32 w = 420, h = 112, x = 110, y = 170;
            gx_rrect_fill(x, y, w, h, 12, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
            draw_panel(x, y, w, h);
            gx_text_c(F_MED, x + w / 2, y + 32, ld_ready() ? "Lancer ce jeu ?" : "Lancer ce jeu avec Swiss ?", RGBA(255, 255, 255, 255));
            fit_text(F_SMALL, g->title, (int)w - 30, buf, sizeof(buf));
            gx_text_c(F_SMALL, x + w / 2, y + 56, buf, RGBA(255, 210, 120, 255));
            gx_text_c(F_SMALL, x + w / 2, y + 76, ld_ready() ? "Lanceur GC-MP3 (B maintenu : \xC3\xA9" "cran du jeu de Swiss)" : "GC-MP3 se ferme et Swiss ouvre directement le jeu.", RGBA(190, 180, 235, 255));
            gx_text_c(F_SMALL, x + w / 2, y + 98, "A : lancer     B : annuler", RGBA(255, 210, 120, 255));
        }
        snprintf(buf, sizeof(buf), "%d / %d   (gauche / droite)", gl_sel + 1, gl_n);
        gx_text_r(F_SMALL, 614, 396, buf, RGBA(150, 135, 210, 255));
        return;
    }
    switch (gl_view) {
    case GLV_CAROUSEL: gl_draw_carousel(); break;
    case GLV_LIST:     gl_draw_list(); break;
    case GLV_WALL:     gl_draw_grid(9, 3, 58, 78, 66, 89, 0); break;
    case GLV_SHOW:     gl_draw_showcase(); break;
    case GLV_SHELF:    gl_draw_shelf(); break;
    default:           gl_draw_grid(6, 2, 86, 115, 98, 136, 1); break;
    }
    /* le jeu choisi */
    char sz[32]; gl_size_txt(g->size, sz, sizeof(sz));
    if (g->disc) snprintf(d, sizeof(d), "%s   disque %d   %s", g->id, g->disc + 1, sz);
    else snprintf(d, sizeof(d), "%s   %s", g->id, sz);
    fit_text(F_MED, g->title, 420, buf, sizeof(buf));
    if (now_ms() - gl_msg_t < 3500) gx_text1(F_SMALL, 30, 400, gl_msg, RGBA(255, 210, 120, 255));
    else if (gl_view != GLV_CAROUSEL && gl_view != GLV_SHOW) { gx_text1(F_MED, 30, 400, buf, RGBA(255, 255, 255, 255)); gx_text_r(F_SMALL, 614, 400, d, RGBA(255, 210, 120, 255)); }
    snprintf(d, sizeof(d), "R : %s", TR(gl_view_names[gl_view]));
    gx_text_r(F_SMALL, 614, 99, d, RGBA(170, 160, 220, 255));
    static const char *const k[] = { "A", "X", "Y", "R", "B" };
    static const char *const l[] = { "FICHE", "CHEATS", "OPTIONS", "AFFICHAGE", "RETOUR" };
    draw_legend_keys(k, l, 5);
    if (gl_opt >= 0) {                                         /* v45 : fenetre Options */
        static const char *const nm[4] = { "Trier par", "Jeu au hasard", "Animations", "Actualiser la liste" };
        const f32 w = 400, rh = 30, h = 60 + 4 * rh, x = 120, y = 240 - h / 2;
        gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 130), RGBA(0, 0, 0, 130), RGBA(0, 0, 0, 130), RGBA(0, 0, 0, 130));
        gx_rrect_fill(x, y, w, h, 12, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
        draw_panel(x, y, w, h);
        gx_text1(F_LARGE, x + 20, y + 32, "Options", RGBA(255, 255, 255, 255));
        for (int i = 0; i < 4; i++) {
            f32 ry = y + 44 + i * rh;
            int on = i == gl_opt;
            gp_zone(x + 12, ry, w - 24, rh - 3, &gl_opt, i, GPZ_SEL);
            if (on) gx_rrect_fill(x + 12, ry, w - 24, rh - 3, 6, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
            gx_text1(F_MED, x + 24, ry + 19, nm[i], RGBA(240, 232, 255, 255));
            const char *v = i == 0 ? gl_sort_names[gl_sort & 3] : i == 2 ? (gl_anim ? "Oui" : "Non") : "(A)";
            gx_text_r(F_MED, x + w - 24, ry + 19, v, on ? RGBA(255, 255, 255, 255) : RGBA(190, 160, 255, 255));
        }
        gx_text_c(F_SMALL, 320, y + h - 8, "Gauche / Droite ou A : changer     B : fermer", RGBA(255, 210, 120, 255));
    }
}

/* ======================= v26 : lancer un jeu avec Swiss =======================
   GC-MP3 charge le .dol de Swiss en memoire et le demarre avec l'argument
   "Autoload=sdc:/chemin/du/jeu.iso" : Swiss (et Indigo) lisent leurs arguments
   comme des lignes de reglages, ouvrent le jeu tout de suite, sans rien garder. */
static int gl_launch_sel = -1;                      /* fenetre "Lancer ?" ouverte (-1 = non) */
static char gl_swiss[NAME_LEN];

static int gl_mem_has(const u8 *d, long n, const char *pat) {    /* (sans tenir compte des majuscules) */
    int m = (int)strlen(pat);
    for (long i = 0; i + m <= n; i++) {
        int k = 0;
        while (k < m && tolower(d[i + k]) == pat[k]) k++;
        if (k == m) return 1;
    }
    return 0;
}
/* un .dol de Swiss ? (on lit les 2 premiers Mo : les textes "swiss" y sont) */
static int gl_is_swiss(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static u8 b[1 << 16];
    int found = 0, mine = 0, indigo = 0;
    long tot = 0;
    size_t n;
    u8 tail[32] = { 0 };
    while (tot < (2 << 20) && (n = fread(b + 32, 1, sizeof(b) - 32, f)) > 0) {
        memcpy(b, tail, 32);
        if (gl_mem_has(b, (long)n + 32, "swiss")) found = 1;
        if (gl_mem_has(b, (long)n + 32, "indigo audio player")) mine = 1;
        if (gl_mem_has(b, (long)n + 32, "posters.pak")) indigo = 1;      /* (Swiss Indigo : jaquettes) */
        memcpy(tail, b + n, 32);
        tot += (long)n;
        service_audio();
    }
    fclose(f);
    return found && !mine ? 1 + indigo : 0;          /* 2 = Swiss Indigo, 1 = autre Swiss */
}
#define GL_SWISS_CACHE GL_CACHE "/swiss.txt"            /* v27 : Swiss deja trouve (chemin | taille) */
static int gl_find_swiss(void) {
    if (swiss_pref[0]) { snprintf(gl_swiss, sizeof(gl_swiss), "%s", swiss_pref); return access(gl_swiss, R_OK) == 0; }
    {
        long len = 0;
        char *c = (char *)read_file(GL_SWISS_CACHE, 4096, &len);
        if (c) {
            char *bar = strchr(c, '|');
            struct stat st;
            if (bar) {
                *bar = 0;
                if (stat(c, &st) == 0 && (long)st.st_size == atol(bar + 1)) { snprintf(gl_swiss, sizeof(gl_swiss), "%s", c); free(c); return 1; }
            }
            free(c);
        }
    }
    gl_status("Recherche de Swiss sur la carte SD...");
    char cand[24][NAME_LEN];
    int nc = 0;
    const char *dirs[] = { SD_ROOT, SD_ROOT "/swiss", SD_ROOT "/apps/swiss", SD_ROOT "/apps/indigo", SD_ROOT "/apps/Swiss", NULL };
    for (int pass = 0; pass < 2; pass++)                      /* 1 : nom avec "swiss" / "indigo" ; 2 : ipl.dol, boot.dol... */
        for (int di = 0; dirs[di] && nc < 24; di++) {
            DIR *d = opendir(dirs[di]);
            if (!d) continue;
            struct dirent *e;
            while ((e = readdir(d)) != NULL && nc < 24) {
                const char *dot = strrchr(e->d_name, '.');
                if (!dot || strcasecmp(dot, ".dol")) continue;
                char low[128]; int i = 0;
                for (; e->d_name[i] && i < 127; i++) low[i] = (char)tolower((u8)e->d_name[i]);
                low[i] = 0;
                if (strstr(low, "gc-mp3") || strstr(low, "gcmp3")) continue;
                int named = strstr(low, "swiss") || strstr(low, "indigo");
                int generic = !strcmp(low, "ipl.dol") || !strcmp(low, "boot.dol") || !strcmp(low, "autoexec.dol") || !strcmp(low, "igr.dol");
                if (pass == 0 ? !named : !generic) continue;
                snprintf(cand[nc++], NAME_LEN, "%s/%s", dirs[di], e->d_name);
            }
            closedir(d);
        }
    int best = 0;                                             /* Swiss Indigo d'abord (celui des jaquettes) */
    for (int i = 0; i < nc && best < 2; i++) {
        int sc = gl_is_swiss(cand[i]);
        if (sc > best) { best = sc; snprintf(gl_swiss, sizeof(gl_swiss), "%s", cand[i]); }
    }
    if (best > 0) {                                         /* garde-le pour la prochaine fois */
        struct stat st;
        FILE *f = stat(gl_swiss, &st) == 0 ? fopen(GL_SWISS_CACHE, "w") : NULL;
        if (f) { fprintf(f, "%s|%ld\n", gl_swiss, (long)st.st_size); fclose(f); }
    }
    return best > 0;
}

/* petit programme copie a 0x80001800 : recopie les sections du .dol a leur
   place (par-dessus GC-MP3), vide les caches, puis saute au debut de Swiss.
   r3 = table {dest, source, taille}, r4 = nombre de lignes, r5 = point d'entree */
static const u32 gl_stub[] = {
    0x7cc000a6, 0x54c6045e, 0x7cc00124, 0x4c00012c, 0x2c040000, 0x41820060, 0x80e30000, 0x81030004,
    0x81230008, 0x3863000c, 0x2c090000, 0x41820040, 0x7cea3b78, 0x7d674a14, 0x7d2903a6, 0x38e7ffff,
    0x3908ffff, 0x8d880001, 0x9d870001, 0x4200fff8, 0x554a0034, 0x7c00506c, 0x7c0004ac, 0x7c0057ac,
    0x394a0020, 0x7c0a5840, 0x4180ffec, 0x3484ffff, 0x4082ffa8, 0x7c0004ac, 0x4c00012c, 0x7ca903a6,
    0x4e800420,
};
#define GL_STUB_AT  0x80001800u
#define GL_TAB_AT   0x80002000u
#define GL_ARGV_AT  0x80002900u
#define GL_CMD_AT   (0x81700000u - 0x400u)        /* ligne de commande : en haut de la memoire, comme le fait Swiss */
#define LD_DOL  GCD "/lanceur.dol"                 /* v44 : lanceur de jeux GC-MP3 (lanceur.c) */
#define LD_VER  GCD "/lanceur.txt"
static void ld_fetch(int loud);
static void as_self(char *out, int cap);
static const char *gl_arg3 = NULL;                 /* v44 : 3e argument (GCMP3=... : retour sur GC-MP3) */

/* demarre Swiss avec "Autoload=<jeu>" (path = chemin sd:/...), ou "Autoload=" (vide :
   le menu de Swiss / Indigo, meme si GC-MP3 y est regle pour demarrer tout seul) */
static const char *gl_boot_swiss(const char *path);
static int gl_boot_cheats = 0;                         /* v31 : ajoute "AutoCheats=Yes" (cheats actives pour ce jeu) */
static const char *gl_launch(int k) {                  /* retourne un message d'erreur, ou ne revient pas */
    if (k < 0 || k >= gl_n) return "Jeu introuvable";
    if (access(gl[k].path, R_OK) != 0) return "Jeu introuvable : Y pour actualiser la liste";
    char p[NAME_LEN]; struct stat st;
    snprintf(p, sizeof(p), "%s/%.6s.chtsel", CH_DIR, gl[k].id);
    gl_boot_cheats = stat(p, &st) == 0 && st.st_size > 8;
    tl_recent_add(gl[k].path);
    gl_stats_bump(k);                                  /* v45 : compteur de lancements */
    const char *e = gl_boot_swiss(gl[k].path);
    gl_boot_cheats = 0;
    return e;
}
/* v35 : lance n'importe quel .dol (Swiss, ou une appli), avec jusqu'a 2 arguments */
static const char *gl_boot_dol(const char *dolpath, const char *a1, const char *a2, const char *what);
static int ld_cache;
static const char *gl_boot_swiss(const char *path) {
    static const char *const dev[3] = { "sdc:", "sda:", "sdb:" };
    char a1[NAME_LEN + 32];
    if (path) snprintf(a1, sizeof(a1), "Autoload=%s%s", dev[sd_slot >= 0 && sd_slot < 3 ? sd_slot : 0], path + strlen(SD_ROOT));
    else snprintf(a1, sizeof(a1), "Autoload=");
    if (path && ld_ready()) {                      /* v44 : lanceur GC-MP3 (pas de menu Swiss) */
        static char a3[NAME_LEN + 16];
        char me[NAME_LEN + 8];
        as_self(me, sizeof(me));
        snprintf(a3, sizeof(a3), "GCMP3=%s", me);
        gl_arg3 = a3;
        const char *e = gl_boot_dol(LD_DOL, a1, gl_boot_cheats ? "AutoCheats=Yes" : NULL, "jeu");
        gl_arg3 = NULL;
        crumb("lanceur GC-MP3 : %s", e ? e : "?");
        if (e && strncmp(e, "(simulateur", 11)) { remove(LD_DOL); ld_cache = -1; }   /* (lanceur abime : Swiss, puis nouveau telechargement) */
        else return e;
    } else if (path) ld_fetch(0);                 /* (pas encore la : pret pour la prochaine fois) */
    if (!gl_find_swiss()) return "Swiss introuvable : mets son .dol a la racine (ex. swiss.dol) ou indique-le dans gc-mp3.txt (swiss = ...)";
    const char *e = gl_boot_dol(gl_swiss, a1, path && gl_boot_cheats ? "AutoCheats=Yes" : NULL, "Swiss");
    if (e && !strcmp(e, "-noarg")) return "Ce Swiss ne prend pas d'arguments : mets-le a jour";
    return e;
}
static const char *gl_boot_dol(const char *dolpath, const char *a1, const char *a2, const char *what) {
    char stmsg[96];
    snprintf(stmsg, sizeof(stmsg), "Lancement de %s...", what);
    /* lecture du .dol par morceaux, avec l'avancement a l'ecran */
    long len = 0;
    unsigned char *dol = NULL;
    {
        FILE *f = fopen(dolpath, "rb");
        struct stat st;
        if (f && stat(dolpath, &st) == 0 && st.st_size > 0 && st.st_size <= 12 * 1024 * 1024) {
            len = (long)st.st_size;
            dol = (unsigned char *)malloc(len);
            long got = 0;
            char m[96];
            while (dol && got < len) {
                long n = len - got < 262144 ? len - got : 262144;
                if ((long)fread(dol + got, 1, n, f) != n) break;
                got += n;
                snprintf(m, sizeof(m), "%s %ld %%", TR(stmsg), got * 100 / len);
                gl_status(m);
            }
            if (dol && got < len) { free(dol); dol = NULL; }
        }
        if (f) fclose(f);
    }
    if (!dol) return "Lecture du .dol impossible (m\xC3\xA9moire ou carte SD)";
    if (len < 0x100) { free(dol); return "Fichier .dol invalide"; }
    /* sections du .dol */
    u32 tab[18 * 3 + 3], n = 0, lo = 0xFFFFFFFFu, hi = 0;
    for (int i = 0; i < 18; i++) {
        u32 off = be32(dol + i * 4), addr = be32(dol + 0x48 + i * 4), sz = be32(dol + 0x90 + i * 4);
        if (!sz) continue;
        if (off + (u64)sz > (u64)len || addr < 0x80003000u || addr + (u64)sz > 0x81700000u) { free(dol); return "Fichier .dol invalide"; }
        tab[n * 3] = addr; tab[n * 3 + 1] = off; tab[n * 3 + 2] = sz; n++;
        if (addr < lo) lo = addr;
        if (addr + sz > hi) hi = addr + sz;
    }
    u32 bss = be32(dol + 0xD8), bsz = be32(dol + 0xDC), entry = be32(dol + 0xE0);
    if (bsz && bss >= 0x80003000u && bss + (u64)bsz < 0x81700000u && bss + bsz > hi) hi = bss + bsz;
    if (!n || entry < lo || entry >= hi) { free(dol); return "Fichier .dol invalide"; }
    u32 dst = (GL_CMD_AT - (u32)len) & ~31u;           /* le .dol est d'abord range tout en haut de la memoire */
    if (dst < hi + 32) { free(dol); return "Ce .dol est trop gros pour etre lance d'ici"; }
    /* arguments : chemin du .dol, puis a1 / a2 (ex. Autoload=sdX:/chemin pour Swiss) */
    char cmd[NAME_LEN * 3 + 96];
    int cl = snprintf(cmd, sizeof(cmd), "%s", dolpath);
    if (a1) cl += 1 + snprintf(cmd + cl + 1, sizeof(cmd) - cl - 1, "%s", a1);
    if (a2) cl += 1 + snprintf(cmd + cl + 1, sizeof(cmd) - cl - 1, "%s", a2);   /* (ex. AutoCheats=Yes : Swiss applique les cheats coches) */
    if (gl_arg3) cl += 1 + snprintf(cmd + cl + 1, sizeof(cmd) - cl - 1, "%s", gl_arg3);
    cl += 1;
    /* le .dol accepte-t-il des arguments ? (texte "_arg" juste apres le point d'entree) */
    int has_arg = 0;
    for (u32 i = 0; i < n; i++)
        if (entry + 8 >= tab[i * 3] && entry + 8 < tab[i * 3] + tab[i * 3 + 2])
            has_arg = !memcmp(dol + tab[i * 3 + 1] + (entry + 4 - tab[i * 3]), "_arg", 4);
    if (!has_arg && a1) { free(dol); return "-noarg"; }
    crumb("lancer : %s %s", dolpath, a1 ? a1 : "");
#ifdef GEKKO
    if (settings_dirty) save_settings();
    stop_track(); stop_station();
    AUDIO_StopDMA();
    fatUnmount("sd");
    VIDEO_SetBlack(TRUE); VIDEO_Flush(); VIDEO_WaitVSync(); VIDEO_WaitVSync();
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    u32 level = IRQ_Disable();                           /* plus d'interruptions : on ecrase GC-MP3 */
    (void)level;
    memmove((void *)dst, dol, (size_t)len);
    u32 *t = (u32 *)GL_TAB_AT;
    for (u32 i = 0; i < n; i++) { t[i * 3] = tab[i * 3]; t[i * 3 + 1] = dst + tab[i * 3 + 1]; t[i * 3 + 2] = tab[i * 3 + 2]; }
    memcpy((void *)GL_CMD_AT, cmd, cl);
    DCFlushRange((void *)GL_CMD_AT, 0x400);
    u32 *a = (u32 *)GL_ARGV_AT;                          /* struct __argv */
    a[0] = 0x5f617267; a[1] = GL_CMD_AT; a[2] = (u32)cl; a[3] = 0; a[4] = 0; a[5] = 0;
    if (has_arg) { t[n * 3] = entry + 8; t[n * 3 + 1] = GL_ARGV_AT; t[n * 3 + 2] = 24; n++; }   /* (copie apres le code du .dol) */
    memcpy((void *)GL_STUB_AT, gl_stub, sizeof(gl_stub));
    DCFlushRange((void *)0x80001800, 0x1800);
    ICInvalidateRange((void *)GL_STUB_AT, sizeof(gl_stub));
    DCFlushRange((void *)dst, (u32)len);
    ((void (*)(u32 *, u32, u32))GL_STUB_AT)(t, n, entry);
    for (;;) ;
#else
    printf("LANCER dol=%s dst=%08x hi=%08x entree=%08x sections=%u args=%d arg=[%s]", dolpath, dst, hi, entry, n, has_arg, cmd + strlen(cmd) + 1);
    { const char *a2 = cmd + strlen(cmd) + 1; a2 += strlen(a2) + 1; if (a2 < cmd + cl) printf(" arg2=[%s]", a2);
      if (a2 < cmd + cl) { a2 += strlen(a2) + 1; if (a2 < cmd + cl) printf(" arg3=[%s]", a2); } }
    printf("\n");
    free(dol);
    return "(simulateur : lancement simule)";
#endif
}

/* ======================= v30 : demarrer GC-MP3 avec Indigo / Swiss =======================
   Indigo (et Swiss) ouvrent tout seuls le fichier ecrit dans la ligne "Autoload="
   de SD:/swiss/settings/global.ini. GC-MP3 y ecrit son propre .dol (ou l'enleve).
   Pour revenir au menu d'Indigo : START > Quitter vers Swiss (Autoload vide). */
#define GL_SWISS_INI SD_ROOT "/swiss/settings/global.ini"
static int as_cache = -1;
static char *as_read(long *len) {                            /* global.ini, termine par un zero */
    unsigned char *r = read_file(GL_SWISS_INI, 256 * 1024, len);
    if (!r) return NULL;
    char *d = (char *)malloc(*len + 1);
    if (d) { memcpy(d, r, *len); d[*len] = 0; }
    free(r);
    return d;
}
static void as_self(char *out, int cap) {                    /* "sdc:/chemin/gc-mp3player.dol" */
    static const char *const dev[3] = { "sdc:", "sda:", "sdb:" };
    up_pick_target();
    const char *rel = up_target;
    if (!strncasecmp(rel, SD_ROOT, strlen(SD_ROOT))) rel += strlen(SD_ROOT);
    else { const char *c = strchr(rel, ':'); if (c && c - rel <= 5) rel = c + 1; }   /* ("sdc:/...", "carda:/...") */
    snprintf(out, cap, "%s%s%s", dev[sd_slot >= 0 && sd_slot < 3 ? sd_slot : 0], rel[0] == '/' ? "" : "/", rel);
}
static int autostart_get(void) {
    if (as_cache >= 0) return as_cache;
    as_cache = 0;
    if (!sd_ok) return 0;
    long len = 0;
    char *d = as_read(&len);
    if (!d) return 0;
    char me[NAME_LEN + 8];
    as_self(me, sizeof(me));
    for (char *l = d; l && *l; ) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        int n = (int)strlen(l);
        if (n && l[n - 1] == '\r') l[n - 1] = 0;
        if (!strncmp(l, "Autoload=", 9) && !strcasecmp(l + 9, me)) as_cache = 1;
        l = e ? e + 1 : NULL;
    }
    free(d);
    return as_cache;
}
static const char *autostart_set(int on) {
    if (!sd_ok || sd_virtual) return "Pas de carte SD";
    long len = 0;
    char *d = as_read(&len);
    if (!d && !on) { as_cache = 0; return NULL; }
    char me[NAME_LEN + 8];
    as_self(me, sizeof(me));
    mkdir(SD_ROOT "/swiss", 0777); mkdir(SD_ROOT "/swiss/settings", 0777);
    char tmp[NAME_LEN];
    snprintf(tmp, sizeof(tmp), "%s.tmp", GL_SWISS_INI);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(d); return "Impossible d'\xC3\xA9" "crire sur la carte SD"; }
    int crlf = !d || strstr(d, "\r\n") != NULL, put = 0;
    for (char *l = d; l && *l; ) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        int n = (int)strlen(l);
        if (n && l[n - 1] == '\r') l[--n] = 0;
        if (!strncmp(l, "Autoload=", 9)) {                   /* (une seule ligne Autoload, la notre) */
            if (on && !put) { fprintf(f, "Autoload=%s%s", me, crlf ? "\r\n" : "\n"); put = 1; }
            else if (!on) fprintf(f, "Autoload=%s", crlf ? "\r\n" : "\n");
        } else fprintf(f, "%s%s", l, crlf ? "\r\n" : "\n");
        l = e ? e + 1 : NULL;
    }
    if (on && !put) fprintf(f, "Autoload=%s%s", me, crlf ? "\r\n" : "\n");
    free(d);
    if (fclose(f) != 0) { remove(tmp); return "Impossible d'\xC3\xA9" "crire sur la carte SD"; }
    remove(GL_SWISS_INI);
    if (rename(tmp, GL_SWISS_INI) != 0) return "Impossible d'\xC3\xA9" "crire sur la carte SD";
    as_cache = on;
    lib_note(on ? "GC-MP3 s'ouvrira au d\xC3\xA9marrage d'Indigo / Swiss" : "Indigo / Swiss s'ouvrira normalement");
    return NULL;
}

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
} gcg_t;
static gcg_t *gl = NULL;
static int gl_n = 0, gl_scanned = 0, gl_on = 0, gl_sel = 0, gl_detail = 0;
static char gl_msg[160];
static u64 gl_msg_t = 0;

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
static void gl_scan(void) {
    if (!gl) gl = (gcg_t *)calloc(GL_MAX, sizeof(gcg_t));
    gl_free_list();
    if (!gl || !sd_ok) { gl_scanned = 1; return; }
    int budget = 6000;
    gl_walk(SD_ROOT, 0, &budget);
    qsort(gl, gl_n, sizeof(gcg_t), gl_cmp);
    swpk_open(&gl_post, GL_UI "/posters.pak", 256, 256);
    swpk_open(&gl_still, GL_UI "/stills.pak", 320, 240);
    gl_load_desc();
    for (int k = 0; k < gl_n; k++) { gl[k].post = (s16)swpk_find(&gl_post, gl[k].id); gl[k].still = (s16)swpk_find(&gl_still, gl[k].id); }
    gl_scanned = 1;
    if (gl_sel >= gl_n) gl_sel = gl_n ? gl_n - 1 : 0;
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
        if (t[0]) g->desc = strdup(t);
        char full[96];
        gl_latin(b + 0x1820 + 0x40, 0x40, full, sizeof(full));
        if (full[0]) snprintf(g->title, sizeof(g->title), "%s", full);
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

static void gl_note(const char *m) { snprintf(gl_msg, sizeof(gl_msg), "%s", TR(m)); gl_msg_t = now_ms(); }

/* ---------- entree ---------- */
static void gclib_open(void) {
    gl_on = 1; gl_detail = 0;
    if (!gl_scanned) gl_scan();
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
static void gclib_input(u32 down, u32 nav, int fire) {
    if (gl_detail) {
        if (fire && (nav & PAD_BUTTON_LEFT) && gl_sel > 0) gl_sel--;
        if (fire && (nav & PAD_BUTTON_RIGHT) && gl_sel < gl_n - 1) gl_sel++;
        if (down & PAD_BUTTON_X && gl_n) gl_open_cheats(gl_sel);
        if (down & (PAD_BUTTON_B | PAD_BUTTON_A)) gl_detail = 0;
        return;
    }
    if (fire && gl_n) {
        if (nav & PAD_BUTTON_RIGHT) gl_sel++;
        if (nav & PAD_BUTTON_LEFT) gl_sel--;
        if (nav & PAD_BUTTON_DOWN) gl_sel += GL_COLS;
        if (nav & PAD_BUTTON_UP) gl_sel -= GL_COLS;
        if (gl_sel >= gl_n) gl_sel = gl_n - 1;
        if (gl_sel < 0) gl_sel = 0;
    }
    if ((down & PAD_BUTTON_A) && gl_n) gl_detail = 1;
    if ((down & PAD_BUTTON_X) && gl_n) gl_open_cheats(gl_sel);
    if (down & PAD_BUTTON_Y) { gl_scanned = 0; gl_scan(); gl_note(gl_n ? "Liste actualis\xC3\xA9" "e" : "Aucun jeu trouv\xC3\xA9"); }
    if (down & PAD_BUTTON_B) gl_on = 0;
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
static void draw_gclib(void) {
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
        /* grande jaquette (niveau 0 : 256x256, dont 192x256 d'image) */
        if (g->post >= 0 && gl_big.game != gl_sel) {
            free(gl_big.tex); gl_big.tex = NULL; gl_big.game = gl_sel;
            int w, h; u8 *c = swpk_read(&gl_post, g->post, 0, &w, &h);
            if (c) { gl_big.tex = gl_cmpr_to_5a3(c, w, h); free(c); }
            if (gl_big.tex) { GX_InitTexObj(&gl_big.obj, gl_big.tex, 256, 256, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE); GX_InitTexObjFilterMode(&gl_big.obj, GX_LINEAR, GX_LINEAR); }
        }
        if (g->still >= 0 && gl_bigst.game != gl_sel) {
            free(gl_bigst.tex); gl_bigst.tex = NULL; gl_bigst.game = gl_sel;
            int w, h; u8 *c = swpk_read(&gl_still, g->still, 0, &w, &h);
            if (c) { gl_bigst.tex = gl_cmpr_to_5a3(c, w, h); free(c); }
            if (gl_bigst.tex) { GX_InitTexObj(&gl_bigst.obj, gl_bigst.tex, 320, 240, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE); GX_InitTexObjFilterMode(&gl_bigst.obj, GX_LINEAR, GX_LINEAR); }
        }
        const f32 cx = 28, cy = 112, cw = 192 * 0.98f, chh = 256 * 0.98f;
        if (g->post >= 0 && gl_big.game == gl_sel && gl_big.tex) {
            gx_glow(cx, cy, cw, chh, 8, RGBA(150, 80, 255, 110), 3);
            draw_tex_fit(&gl_big.obj, 256, 256, 192, 256, cx, cy, cw, chh);
        } else gl_card_banner(gl_sel, cx, cy, cw, chh, 1);
        const f32 rx = 236; const int rw = 380;
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
        if (now_ms() - gl_msg_t < 3500) gx_text1(F_SMALL, 30, 404, gl_msg, RGBA(255, 210, 120, 255));
        static const char *const k[] = { "X", "B" };
        static const char *const l[] = { "CHEATS", "RETOUR" };
        draw_legend_keys(k, l, 2);
        snprintf(buf, sizeof(buf), "%d / %d   (gauche / droite)", gl_sel + 1, gl_n);
        gx_text_r(F_SMALL, 614, 396, buf, RGBA(150, 135, 210, 255));
        return;
    }
    /* grille : 6 colonnes x 2 rangees */
    const f32 x0 = 26, y0 = 112, sw = 98, cwid = 86, chei = 115, rh = 136;
    int row = gl_sel / GL_COLS, top = row > 0 ? row - 1 : 0;
    int rows = (gl_n + GL_COLS - 1) / GL_COLS;
    if (top > rows - 2) top = rows - 2;
    if (top < 0) top = 0;
    int loaded = 0;
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < GL_COLS; c++) {
            int k = (top + r) * GL_COLS + c;
            if (k >= gl_n) break;
            f32 x = x0 + c * sw, y = y0 + r * rh;
            int on = k == gl_sel;
            gp_zone(x, y, cwid, chei + 16, &gl_sel, k, GPZ_SEL);
            if (on) gx_glow(x, y, cwid, chei, 8, RGBA(150, 80, 255, 150), 3);
            int had = gl[k].post >= 0 && gl_find_slot(gl_cov, GL_SLOTS, k) != NULL;
            GXTexObj *cov = gl[k].post >= 0 ? gl_cover(k, loaded < 3) : NULL;   /* (3 jaquettes lues par image au plus) */
            if (cov && !had) loaded++;
            if (cov) draw_tex_fit(cov, 128, 128, 96, 128, x, y, cwid, chei);
            else gl_card_banner(k, x, y, cwid, chei, on);
            if (on) gx_rrect_stroke(x - 2, y - 2, cwid + 4, chei + 4, 6, 2.0f, RGBA(255, 210, 120, 255));
            if (gl[k].disc) { snprintf(d, sizeof(d), "%s (%d)", gl[k].title, gl[k].disc + 1); fit_text(F_SMALL, d, (int)cwid + 6, buf, sizeof(buf)); }
            else fit_text(F_SMALL, gl[k].title, (int)cwid + 6, buf, sizeof(buf));
            gx_text_c(F_SMALL, x + cwid / 2, y + chei + 14, buf, on ? RGBA(255, 255, 255, 255) : RGBA(180, 168, 228, 255));
        }
    /* le jeu choisi */
    char sz[32]; gl_size_txt(g->size, sz, sizeof(sz));
    if (g->disc) snprintf(d, sizeof(d), "%s   disque %d   %s", g->id, g->disc + 1, sz);
    else snprintf(d, sizeof(d), "%s   %s", g->id, sz);
    fit_text(F_MED, g->title, 420, buf, sizeof(buf));
    if (now_ms() - gl_msg_t < 3500) gx_text1(F_SMALL, 30, 400, gl_msg, RGBA(255, 210, 120, 255));
    else { gx_text1(F_MED, 30, 400, buf, RGBA(255, 255, 255, 255)); gx_text_r(F_SMALL, 614, 400, d, RGBA(255, 210, 120, 255)); }
    static const char *const k[] = { "A", "X", "Y", "B" };
    static const char *const l[] = { "FICHE", "CHEATS", "ACTUALISER", "RETOUR" };
    draw_legend_keys(k, l, 4);
}

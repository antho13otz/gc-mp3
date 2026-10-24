// GC-MP3 v25 : « Mes jeux GameCube » (onglet JEUX) — inclus dans main.c apres cheats.c
// Liste les jeux de la carte SD (.iso, .gcm, .ciso, .nkit.iso) avec :
//  - l'ID et le nom lus dans le jeu, la banniere du disque (opening.bnr) ;
//  - les jaquettes : SD:/gc-mp3/jaquettes/posters.pak (192x256, CMPR), les captures
//    stills.pak (320x240) et descriptions.txt (v90 : avant dans SD:/swiss/ui, deplaces une fois).
// Format des packs : SWPK v1, nombres en big-endian.
// v90 : les jeux se lancent avec Resident (natif.c), ou le lanceur de secours (lanceur.c).

#define GL_MAX      400
#define GL_UI       GCD "/jaquettes"
#define GL_CACHE    GCD "/cache/jeux"
#define GL_SLOTS    96                       /* jaquettes gardees en memoire (128x128 CMPR : 8 Ko chacune) ; v92 : 96 */
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
    u32 play;                                /* v73 : temps de jeu mesure par Resident (secondes) */
    u8 fav;                                  /* v76 : jeu favori (remonte en haut de la liste) */
    u8 dvd;                                  /* v97 : case "Disque de la console" (vrai lecteur), toujours la 1re */
} gcg_t;
/* v28 : 6 facons d'afficher les jeux (R / L pour changer) */
enum { GLV_GRID, GLV_CAROUSEL, GLV_LIST, GLV_WALL, GLV_SHOW, GLV_SHELF, GLV_COUNT };
static void tl_recent_add(const char *path);            /* v35 : derniers jeux lances (tools.c) */
static void indigo_game_open(const char *id, const char *title);   /* v35 : reglages du jeu (indigo_cfg.c) */
static int gl_view = GLV_GRID;
static const char *const gl_view_names[GLV_COUNT] = { "Grille", "Carrousel", "Liste", "Mur de jaquettes", "Vitrine", "\xC3\x89tag\xC3\xA8re" };
/* v103 : styles Moderne, Nouvelle et Aurore : la vue Grille (et Carrousel) devient une rangee de grandes
   jaquettes qui defile, dessinee dans le style choisi (Classique : inchange) */
static int gl_styled(void) { return ui_skin >= 1 && gl_view != GLV_LIST; }   /* (R : jaquettes <-> liste) */
static const char *gl_view_name(void) { return gl_styled() ? "Jaquettes" : gl_view_names[gl_view]; }
static int gl_view_cols(void) { return gl_styled() ? 1 : gl_view == GLV_WALL ? 9 : gl_view == GLV_GRID || gl_view == GLV_SHELF ? 6 : 1; }
static void gl_load_big(int k);
/* v46 : cheats du jeu k : -1 = pas de fichier de cheats, sinon nombre de cheats actives */
static int gl_cheats_on(int k);
static void gl_stats_load(void);
static void gl_fav_load(void);
static void gl_fav_save(void);
static void gl_apply_sort(void);
static int ld_ready(void);
static int ld_build(void);                                 /* (lanceur.c) */
static gcg_t *gl = NULL;
static int gl_n = 0, gl_scanned = 0, gl_on = 0, gl_sel = 0, gl_detail = 0;
static char gl_msg[160];
static u64 gl_msg_t = 0;
static int gl_launch_sel;
static int gl_list_dirty = 0;
static char gl_busy[96] = "";                            /* v27 : message "en cours" affiche par-dessus */
static void gl_status(const char *m) { snprintf(gl_busy, sizeof(gl_busy), "%s", TR(m)); ui_status(m); }                            /* v27 : noms / descriptions lus dans les bannieres */
static const char *gl_launch(int k);
static const char *game_boot(int k, int resident);         /* v75 : lancement avec les reglages du jeu (jeux_cfg.c) */
static const char *game_boot_auto(int k);                  /* v90 : Resident, ou le lanceur de secours si des cheats sont actives */
static void nat_run(int k);                                /* v50 : lanceur natif, analyse (natif.c) */
static const char *nat_boot(int k);                        /* v51 : lanceur maison, demarrage natif (natif.c), en test */
static const char *nat_boot_disc(int k);                   /* v97 : disque du vrai lecteur (natif.c) */
static int gl_has_dvd(void);
static int nat_qr_input(u32 down);                         /* v51 : QR code du rapport */
static void nat_draw_qr(void);

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
/* lit le niveau "level" (0 = taille reelle) d'une texture CMPR ; v92 : alignee, prete pour GX (GX_TF_CMPR) */
static u8 *swpk_read(const swpk_t *k, int r, int level, int *w, int *h) {
    if (!k->fp || r < 0 || r >= k->n || level >= (k->levels ? k->levels : 1)) return NULL;
    u32 off = be32(k->idx + r * 32 + 8);
    int lw = k->cw, lh = k->ch;
    for (int l = 0; l < level; l++) { off += (u32)lw * lh / 2; lw /= 2; lh /= 2; }
    u32 len = (u32)lw * lh / 2;
    u8 *b = (u8 *)memalign(32, len);
    if (!b) return NULL;
    if (fseek(k->fp, (long)off, SEEK_SET) != 0 || fread(b, 1, len, k->fp) != len) { free(b); return NULL; }
    DCFlushRange(b, len);
    *w = lw; *h = lh;
    return b;
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
    for (int k = 0; k < gl_n; k++) if (!gl[k].dvd)
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
/* v97 : la case "Disque de la console" : lance le jeu qui est dans le vrai lecteur */
static int gl_has_dvd(void) { return gl_n > 0 && gl[0].dvd; }
static void gl_dvd_add(void) {
    if (!gl || gl_n >= GL_MAX) return;
    for (int k = 0; k < gl_n; k++) if (gl[k].dvd) return;
    memmove(&gl[1], &gl[0], sizeof(gcg_t) * gl_n);
    gcg_t *g = &gl[0];
    memset(g, 0, sizeof(*g));
    snprintf(g->path, sizeof(g->path), "dvd:");
    snprintf(g->title, sizeof(g->title), "Disque de la console");
    g->dvd = 1; g->post = g->still = -1; g->bslot = -1; g->bstate = 2;
    gl_n++;
}
static void gl_scan_full(int force) {
    if (!gl) gl = (gcg_t *)calloc(GL_MAX, sizeof(gcg_t));
    gl_free_list();
    if (!gl || !sd_ok) { gl_dvd_add(); gl_scanned = 1; return; }
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
    gl_dvd_add();                                          /* v97 : case du vrai lecteur */
    gl_stats_load();                                     /* v45 : lancements, puis tri choisi */
    gl_fav_load();                                        /* v76 : favoris */
    gl_apply_sort();
    gl_busy[0] = 0;
    gl_scanned = 1;
    if (gl_sel >= gl_n) gl_sel = gl_n ? gl_n - 1 : 0;
    if (gl_sel == 0 && gl_has_dvd() && gl_n > 1) gl_sel = 1;   /* v97 : le 1er jeu choisi (la case du lecteur juste avant) */
}
static void gl_scan(void) { gl_scan_full(1); }

static void gl_slots_reset(void);
static int gl_cheats_on(int k) {
    static int ck = -1, cgen = -1, cval = -1;
    static char cid[8] = "";
    if (k < 0 || k >= gl_n) return -1;
    if (k == ck && cgen == ch_sel_gen && !strcmp(cid, gl[k].id)) return cval;
    ck = k; cgen = ch_sel_gen; snprintf(cid, sizeof(cid), "%s", gl[k].id);
    char p[NAME_LEN];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%.6s.txt", CH_DIR, gl[k].id);
    if (stat(p, &st) != 0 || st.st_size < 8) { cval = -1; return cval; }
    cval = 0;
    snprintf(p, sizeof(p), "%s/%.6s.chtsel", CH_DIR, gl[k].id);
    long n = 0;
    u8 *d = read_file(p, 65536, &n);
    if (d) { for (long i = 0; i + 8 < n; i++) if (d[i]) cval++; free(d); }
    return cval;
}
/* ---------- v45 : lancements (SD:/gc-mp3/jeux_stats.txt : ID|disque|nombre|date) et tri ---------- */
#define GL_STATS GCD "/jeux_stats.txt"
#define GL_FAV   GCD "/jeux-favoris.txt"
/* v76 : jeux favoris (une ligne "id|disc" par jeu) */
static void gl_fav_load(void) {
    for (int k = 0; k < gl_n; k++) gl[k].fav = 0;
    long n = 0;
    char *d = (char *)read_file(GL_FAV, 16 * 1024, &n);
    if (!d) return;
    char *t = (char *)realloc(d, n + 1);
    if (!t) { free(d); return; }
    t[n] = 0;
    for (char *l = t; l && *l; ) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char id[8] = ""; int disc = 0;
        if (sscanf(l, "%6[^|]|%d", id, &disc) >= 1)
            for (int k = 0; k < gl_n; k++) if (!strcmp(gl[k].id, id) && gl[k].disc == disc) gl[k].fav = 1;
        l = e ? e + 1 : NULL;
    }
    free(t);
}
static void gl_fav_save(void) {
    if (!sd_ok) return;
    mkdir(GCD, 0777);
    FILE *f = fopen(GL_FAV, "wb");
    if (!f) return;
    for (int k = 0; k < gl_n; k++) if (gl[k].fav) fprintf(f, "%s|%d\n", gl[k].id, gl[k].disc);
    fclose(f);
}
static void gl_stats_load(void) {
    for (int k = 0; k < gl_n; k++) { gl[k].runs = 0; gl[k].last = 0; gl[k].play = 0; }
    long n = 0;
    char *d = (char *)read_file(GL_STATS, 64 * 1024, &n);
    if (!d) return;
    char *t = (char *)realloc(d, n + 1);
    if (!t) { free(d); return; }
    t[n] = 0;
    for (char *l = t; l && *l; ) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char id[8] = ""; int disc = 0; unsigned runs = 0, last = 0, play = 0;
        if (sscanf(l, "%6[^|]|%d|%u|%u|%u", id, &disc, &runs, &last, &play) >= 4)
            for (int k = 0; k < gl_n; k++)
                if (!strcmp(gl[k].id, id) && gl[k].disc == disc) { gl[k].runs = (u16)(runs > 65535 ? 65535 : runs); gl[k].last = last; gl[k].play = play; }
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
    fprintf(f, "%s|%d|%u|%u|%u\n", gl[k].id, gl[k].disc, (unsigned)gl[k].runs, (unsigned)gl[k].last, (unsigned)gl[k].play);
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
/* v73 : temps de jeu (Resident : images comptees pendant le jeu), ajoute par l'identifiant du jeu,
   meme si la liste des jeux n'est pas encore lue (au demarrage) */
static void gl_stats_add_play(const char *id6, u32 sec) {
    if (!sd_ok || !id6[0] || !sec) return;
    long n = 0;
    char *d = (char *)read_file(GL_STATS, 64 * 1024, &n);
    mkdir(GCD, 0777);
    FILE *f = fopen(GL_STATS ".tmp", "wb");
    if (!f) { free(d); return; }
    int found = 0;
    for (long i = 0; d && i < n; ) {
        long s = i;
        while (i < n && d[i] != '\n') i++;
        char l[96]; long ln = i - s < 95 ? i - s : 95;
        memcpy(l, d + s, ln); l[ln] = 0;
        char id[8] = ""; int disc = 0; unsigned runs = 0, last = 0, play = 0;
        if (!found && sscanf(l, "%6[^|]|%d|%u|%u|%u", id, &disc, &runs, &last, &play) >= 4 && !strcmp(id, id6)) {
            fprintf(f, "%s|%d|%u|%u|%u\n", id, disc, runs, last, play + sec); found = 1;
        } else if (ln > 0) { fwrite(d + s, 1, i - s, f); fputc('\n', f); }
        i++;
    }
    if (!found) fprintf(f, "%s|0|1|%u|%u\n", id6, (unsigned)time(NULL), (unsigned)sec);
    free(d);
    if (fclose(f) == 0) { remove(GL_STATS); rename(GL_STATS ".tmp", GL_STATS); }
    for (int k = 0; k < gl_n; k++) if (!strcmp(gl[k].id, id6)) { gl[k].play += sec; break; }
}
static int gl_cmp_key(const void *a, const void *b) {
    const gcg_t *x = (const gcg_t *)a, *y = (const gcg_t *)b;
    if (x->dvd != y->dvd) return y->dvd - x->dvd;                   /* v97 : le disque de la console d'abord */
    if (x->fav != y->fav) return y->fav - x->fav;                   /* v76 : favoris en haut */
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
    u8 *c = swpk_read(&gl_post, gl[k].post, 1, &w, &h);    /* v92 : texture CMPR telle quelle (plus de conversion) */
    if (!c) { gl[k].post = -1; return NULL; }
    s = gl_new_slot(gl_cov, GL_SLOTS);
    s->tex = (u16 *)c; s->game = k; s->used = now_ms();
    GX_InitTexObj(&s->obj, c, w, h, GX_TF_CMPR, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&s->obj, GX_LINEAR, GX_LINEAR);
    return &s->obj;
}
/* v92 : lectures dans la table des fichiers du disque par fenetres de 4 Ko (deux fenetres : entrees, noms) */
typedef struct { u32 base; int ok; u8 b[4096]; } gl_win_t;
static int gl_win_read(gdisc_t *d, gl_win_t *w, u32 off, void *out, u32 len, u32 lim) {
    if (len > 64) return gd_read(d, off, out, len);
    if (!w->ok || off < w->base || off + len > w->base + sizeof(w->b)) {
        u32 b0 = off & ~2047u, n = sizeof(w->b);
        if (lim > b0 && lim - b0 < n) n = lim - b0;
        if (n < (off - b0) + len || !gd_read(d, b0, w->b, n)) { w->ok = 0; return gd_read(d, off, out, len); }
        w->base = b0; w->ok = 1;
        usleep(300);                                         /* (thread de fond : l'ecran passe devant) */
    }
    memcpy(out, w->b + (off - w->base), len);
    return 1;
}
/* banniere du disque : cache SD:/gc-mp3/cache/jeux/<ID><disque>.bnr, sinon lue dans le jeu */
/* v92 : lecture de la banniere (cache, sinon dans le jeu, puis mise en cache) : faite par le thread de
   fond (gl_job_run), sans rien toucher de la liste des jeux */
static u8 *gl_banner_fetch(const char *path, const char *cp) {
    u8 *b = NULL;
    long len = 0;
    unsigned char *c = read_file(cp, 0x2000, &len);
    if (c && len >= 0x1820 + 0x140 && !memcmp(c, "BNR", 3)) b = c;
    else {
        free(c);
        gdisc_t d;
        if (gd_open(&d, path)) {
            u8 h[0x440];
            if (gd_read(&d, 0, h, sizeof(h))) {
                /* v92 : seulement les fichiers a la racine du disque (on saute le contenu des dossiers),
                   au lieu de lire toute la table des fichiers (jusqu'a plusieurs centaines de Ko) */
                u32 fo = be32(h + 0x424), fs = be32(h + 0x428);
                u8 e[12], root[12];
                static gl_win_t we, wn;
                we.ok = wn.ok = 0;
                if (fs >= 12 && fs < (4u << 20) && gd_read(&d, fo, root, 12)) {
                    u32 ne = be32(root + 8), st = fo + ne * 12;
                    int guard = 0;
                    for (u32 i = 1; ne <= fs / 12 && i < ne && guard < 2048; guard++) {
                        if (!gl_win_read(&d, &we, fo + i * 12, e, 12, fo + fs)) break;
                        if (e[0]) { u32 nx = be32(e + 8); if (nx <= i) break; i = nx; continue; }   /* (dossier : on le saute) */
                        char nm[16] = { 0 };
                        u32 no = be32(e) & 0xFFFFFF;
                        if (st + no + 12 <= fo + fs && gl_win_read(&d, &wn, st + no, nm, 12, fo + fs) && !strncasecmp(nm, "opening.bnr", 12)) {
                            u32 bo = be32(e + 4), bl = be32(e + 8);
                            if (bl >= 0x1960) {
                                b = (u8 *)malloc(0x1960);
                                if (b && !gd_read(&d, bo, b, 0x1960)) { free(b); b = NULL; }
                                if (b && memcmp(b, "BNR", 3)) { free(b); b = NULL; }
                            }
                            break;
                        }
                        i++;
                    }
                }
            }
            gd_close(&d);
        }
        if (b) {                                             /* garde-la pour la prochaine fois */
            mkdir(GCD, 0777); mkdir(GCD "/cache", 0777); mkdir(GL_CACHE, 0777);
            FILE *f = fopen(cp, "wb");
            if (f) { fwrite(b, 1, 0x1960, f); fclose(f); }
        }
    }
    return b;
}
static void gl_banner_apply(int k, u8 *b) {
    gcg_t *g = &gl[k];
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
/* ---------- v92 : lectures sur la carte SD dans le thread de fond (dec_thread_fn, main.c) ----------
   Bannieres des jeux et grandes images (jaquette 256x256, capture 320x240) : l'ecran ne s'arrete plus
   pendant ces lectures. Un travail a la fois ; le resultat est verifie (chemin du jeu) avant usage. */
enum { GJ_BANNER = 1, GJ_BIG, GJ_STILL };
static struct { int kind, k; char path[NAME_LEN], cp[NAME_LEN], pak[NAME_LEN]; u32 off, len; u8 *res; } gj;
static void gl_job_run(void) {                               /* (thread de fond) */
    gj.res = NULL;
    if (gj.kind == GJ_BANNER) { gj.res = gl_banner_fetch(gj.path, gj.cp); return; }
    FILE *f = fopen(gj.pak, "rb");
    if (!f) return;
    u8 *b = (u8 *)memalign(32, gj.len);
    int ok = b && fseek(f, (long)gj.off, SEEK_SET) == 0;
    for (u32 o = 0; ok && o < gj.len; o += 4096) {            /* (par 4 Ko : l'ecran garde la main entre deux) */
        u32 n = gj.len - o < 4096 ? gj.len - o : 4096;
        ok = fread(b + o, 1, n, f) == n;
        usleep(500);
    }
    if (ok) { DCFlushRange(b, gj.len); gj.res = b; }
    else free(b);
    fclose(f);
}
static int gl_job_post(int kind, int k) {
    if (gj_state != 0 || gj_hold || k < 0 || k >= gl_n || !dec_ensure()) return 0;
    gcg_t *g = &gl[k];
    gj.kind = kind; gj.k = k; gj.res = NULL;
    snprintf(gj.path, sizeof(gj.path), "%s", g->path);
    if (kind == GJ_BANNER) snprintf(gj.cp, sizeof(gj.cp), GL_CACHE "/%s%u.bnr", g->id, g->disc);
    else {
        const swpk_t *pk = kind == GJ_BIG ? &gl_post : &gl_still;
        int r = kind == GJ_BIG ? g->post : g->still;
        if (!pk->idx || r < 0 || r >= pk->n) return 0;
        snprintf(gj.pak, sizeof(gj.pak), "%s", kind == GJ_BIG ? GL_UI "/posters.pak" : GL_UI "/stills.pak");
        gj.off = be32(pk->idx + r * 32 + 8); gj.len = (u32)pk->cw * pk->ch / 2;
    }
    gj_state = 1;
    return 1;
}
static void gl_job_done(void) {
    if (gj_state != 2) return;
    int k = gj.k;
    u8 *b = gj.res;
    gj.res = NULL;
    gj_state = 0;
    if (k < 0 || k >= gl_n || strcmp(gl[k].path, gj.path)) { free(b); return; }   /* (liste changee entre-temps) */
    if (gj.kind == GJ_BANNER) { gl_banner_apply(k, b); return; }
    if (gj.kind == GJ_BIG) {
        free(gl_big.tex); gl_big.tex = (u16 *)b; gl_big.game = k;
        if (b) { GX_InitTexObj(&gl_big.obj, b, 256, 256, GX_TF_CMPR, GX_CLAMP, GX_CLAMP, GX_FALSE); GX_InitTexObjFilterMode(&gl_big.obj, GX_LINEAR, GX_LINEAR); }
    } else {
        free(gl_bigst.tex); gl_bigst.tex = (u16 *)b; gl_bigst.game = k;
        if (b) { GX_InitTexObj(&gl_bigst.obj, b, 320, 240, GX_TF_CMPR, GX_CLAMP, GX_CLAMP, GX_FALSE); GX_InitTexObjFilterMode(&gl_bigst.obj, GX_LINEAR, GX_LINEAR); }
    }
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
        mkdir(CH_DIR, 0777);
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
        if (down & PAD_BUTTON_A) {                         /* v90 : Resident (le lanceur de secours si des cheats sont actives) */
            int k = gl_launch_sel;
            gl_launch_sel = -1;
            const char *e = game_boot_auto(k);              /* (ne revient pas si ca marche ; avec les reglages du jeu) */
            gl_busy[0] = 0;
            if (e) gl_note(e);
        }
        if (down & PAD_BUTTON_X) {                         /* v90 : lanceur de secours */
            int k = gl_launch_sel;
            gl_launch_sel = -1;
            gl_status("Lancement du jeu...");
            const char *e = game_boot(k, 0);                /* (ne revient pas si ca marche) */
            gl_busy[0] = 0;
            if (e) gl_note(e);
        }
        if (down & PAD_BUTTON_B) gl_launch_sel = -1;
        return;
    }
    if (gl_detail) {
        if (nat_qr_input(down)) return;
        if (fire && (nav & PAD_BUTTON_LEFT) && gl_n) { gl_sel = nav_wrap(gl_sel, gl_n, 1, -1, 0); if (gl_anim) gl_slide = -1.0f; }
        if (fire && (nav & PAD_BUTTON_RIGHT) && gl_n) { gl_sel = nav_wrap(gl_sel, gl_n, 1, 1, 0); if (gl_anim) gl_slide = 1.0f; }
        if ((down & PAD_BUTTON_A) && gl_n) gl_launch_sel = gl_sel;
        if (gl_n && gl[gl_sel].dvd) { if (down & PAD_BUTTON_B) gl_detail = 0; return; }   /* v97 : (case du vrai lecteur) */
        if (down & PAD_BUTTON_X && gl_n) gl_open_cheats(gl_sel);
        if ((down & PAD_BUTTON_Y) && gl_n) indigo_game_open(gl[gl_sel].id, gl[gl_sel].title);   /* v35 : reglages du jeu */
        if ((down & PAD_TRIGGER_R) && gl_n) nat_run(gl_sel);          /* v50 : analyse native (natif.c) */
        if ((down & PAD_TRIGGER_L) && gl_n) {                         /* v76 : favori */
            gl[gl_sel].fav = !gl[gl_sel].fav;
            gl_note(gl[gl_sel].fav ? "Ajout\xC3\xA9 aux favoris" : "Retir\xC3\xA9 des favoris");
            gl_fav_save();
            gl_apply_sort();
        }
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
        if (ui_skin >= 1) gl_view = gl_view == GLV_LIST ? GLV_GRID : GLV_LIST;   /* v103 : styles : jaquettes ou liste */
        else gl_view = (gl_view + ((down & PAD_TRIGGER_R) ? 1 : GLV_COUNT - 1)) % GLV_COUNT;
        settings_dirty = 1;
        gl_note(gl_view_name());
    }
    if ((down & PAD_BUTTON_A) && gl_n) { gl_detail = 1; if (gl[gl_sel].dvd) gl_launch_sel = gl_sel; }   /* v97 : disque : tout de suite "Lancer ?" */
    if ((down & PAD_BUTTON_X) && gl_n && !gl[gl_sel].dvd) gl_open_cheats(gl_sel);
    if (down & PAD_BUTTON_Y) gl_opt = 0;                      /* v45 : options (tri, hasard, animations, actualiser) */
    if (gm_hub_input()) return;                               /* v84 : stick C : applis / cartes memoire */
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
static void gl_card_dvd(f32 x, f32 y, f32 w, f32 h, int on) {   /* v97 : case du vrai lecteur */
    gx_rrect_fill(x, y, w, h, 6, on ? RGBA(40, 120, 170, 255) : RGBA(18, 54, 92, 240), on ? RGBA(20, 70, 120, 255) : RGBA(10, 28, 56, 240));
    f32 r = (w < h ? w : h) * 0.30f, cx = x + w / 2, cy = y + h / 2 - (h > 60 ? h * 0.10f : 0);
    gx_circle(cx, cy, r, RGBA(225, 230, 245, 255), RGBA(150, 160, 190, 255));
    gx_circle(cx, cy, r * 0.62f, RGBA(190, 200, 225, 255), RGBA(120, 130, 165, 255));
    gx_circle(cx, cy, r * 0.20f, on ? RGBA(20, 70, 120, 255) : RGBA(10, 28, 56, 255), on ? RGBA(20, 70, 120, 255) : RGBA(10, 28, 56, 255));
    if (h > 60) {
        char b[64];
        fit_text(F_SMALL, TR("Lecteur de la console"), (int)w - 6, b, sizeof(b));
        gx_text_c(F_SMALL, cx, y + h - 10, b, RGBA(220, 235, 255, 255));
    }
}
static void gl_card_banner(int k, f32 x, f32 y, f32 w, f32 h, int on) {
    if (gl[k].dvd) { gl_card_dvd(x, y, w, h, on); return; }
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
static int gl_loaded_frame = 0;                       /* jaquettes lues pendant cette image */
/* v68 : une seule lecture par image (avant 3 : l'image sautait en defilant) ; pendant qu'on
   navigue, seulement la jaquette choisie ; les voisines quand on s'arrete */
static int gl_may_load(int k) { return gl_loaded_frame < 1 && (k == gl_sel || now_ms() - last_input_ms > 150); }
/* une jaquette (ou la banniere) dans le cadre x, y, w, h */
static void gl_draw_cover(int k, f32 x, f32 y, f32 w, f32 h, int on) {
    int had = gl[k].post >= 0 && gl_find_slot(gl_cov, GL_SLOTS, k) != NULL;
    GXTexObj *cov = gl[k].post >= 0 ? gl_cover(k, gl_may_load(k)) : NULL;
    if (cov && !had) gl_loaded_frame++;
    if (cov) draw_tex_fit(cov, 128, 128, 96, 128, x, y, w, h);
    else gl_card_banner(k, x, y, w, h, on);
}
static void gl_title_of(int k, char *out, int cap) {
    if (gl[k].dvd) { snprintf(out, cap, "%s", TR(gl[k].title)); return; }
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
/* ---------- v103 : rangee de jaquettes dans le style choisi (Moderne, Nouvelle, Aurore) ---------- */
/* reflet : le bas de l'image, retourne, qui s'efface (alpha a en haut, 0 en bas) */
static void gl_tex_refl(GXTexObj *o, int tw, int th, int iw, int ih, f32 x, f32 y, f32 w, f32 h, f32 rh, u8 a) {
    f32 cxd = x + w / 2;
    w *= ux_k;
    uxsave_t uz = ux_native(cxd);
    x = cxd - w / 2;
    f32 s = w / iw;
    if (ih * s > h) s = h / ih;
    f32 dw = iw * s, dh = ih * s, x0 = x + (w - dw) / 2, y0 = y + (h - dh) / 2 + dh + 3;
    if (rh > dh) rh = dh;
    f32 u1 = (f32)iw / tw, v1 = (f32)ih / th, v0 = v1 * (1 - rh / dh);
    gx_set_image(o);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(UX(x0), y0);           GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(0.0f, v1);
    GX_Position2f32(UX(x0 + dw), y0);      GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(u1, v1);
    GX_Position2f32(UX(x0 + dw), y0 + rh); GX_Color4u8(255, 255, 255, 0); GX_TexCoord2f32(u1, v0);
    GX_Position2f32(UX(x0), y0 + rh);      GX_Color4u8(255, 255, 255, 0); GX_TexCoord2f32(0.0f, v0);
    GX_End();
    ux_restore(uz);
}
static void gl_draw_styled(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    static f32 pos = -10000.0f;
    int loop = gl_n >= 7, st = ui_skin;
    if (pos < -9000.0f || !gl_anim) pos = (f32)gl_sel;
    else {
        f32 diff = (f32)gl_sel - pos;
        if (loop) { while (diff > gl_n / 2.0f) diff -= gl_n; while (diff < -gl_n / 2.0f) diff += gl_n; }
        if (fabsf(diff) > 6.0f) diff = diff > 0 ? 6.0f : -6.0f, pos = (f32)gl_sel - diff;
        pos += diff * (1.0f - expf(-ui_dt * 12.0f));
        if (fabsf((f32)gl_sel - pos) < 0.003f) pos = (f32)gl_sel;
        if (loop) { while (pos < 0) pos += gl_n; while (pos >= gl_n) pos -= gl_n; }
    }
    if (now_ms() - last_input_ms > 300 && gl_n) gl_load_big(gl_sel);   /* (grande jaquette du jeu choisi, en fond) */
    /* tailles et ecarts selon le style */
    static const f32 scl3[5] = { 1.0f, 0.70f, 0.52f, 0.40f, 0.31f }, off3[5] = { 0, 150, 246, 308, 350 };
    static const f32 scl2[5] = { 1.0f, 0.78f, 0.62f, 0.50f, 0.42f }, off2[5] = { 0, 140, 238, 314, 372 };
    static const f32 scl1[5] = { 1.0f, 0.80f, 0.66f, 0.56f, 0.48f }, off1[5] = { 0, 136, 236, 318, 388 };
    const f32 *scl = st == 3 ? scl3 : st == 2 ? scl2 : scl1, *off = st == 3 ? off3 : st == 2 ? off2 : off1;
    const f32 CW = st == 3 ? 160 : 140, CH = CW * 4 / 3, top = st == 3 ? 114 : 122;
    int base = (int)floorf(pos + 0.5f);
    int order[11], no = 0;
    for (int i = -5; i <= 5; i++) order[no++] = i;
    for (int a = 0; a < no; a++)
        for (int b2 = a + 1; b2 < no; b2++) {
            f32 da = fabsf(base + order[a] - pos), db = fabsf(base + order[b2] - pos);
            if (db > da) { int t = order[a]; order[a] = order[b2]; order[b2] = t; }
        }
    scr_scissor(14, 104, 612, 304);
    if (st == 3) {                                         /* Aurore : lueur sous la jaquette du centre */
        nv_raw = 1;
        gx_ellipse(320, top + CH + 6, 150, 26, au_mix(0.4f, 70), au_mix(0.4f, 0));
        nv_raw = 0;
    }
    for (int oi = 0; oi < no; oi++) {
        int i = order[oi];
        f32 p = base + i - pos, ad = fabsf(p);
        if (ad > 4.4f) continue;
        int k = base + i;
        if (loop) k = (k % gl_n + gl_n) % gl_n;
        else if (k < 0 || k >= gl_n) continue;
        f32 sc = gl_tab_at(scl, ad), w = CW * sc, h = CH * sc;
        f32 cx = 320 + (p < 0 ? -1 : 1) * gl_tab_at(off, ad);
        f32 y = st == 3 ? top + (CH - h) : top + (CH - h) / 2;     /* (Aurore : tout pose sur la meme ligne) */
        f32 near = ad < 1 ? 1 - ad : 0;
        int on = ad < 0.5f;
        nv_raw = 1;
        if (st == 2) {                                     /* Nouvelle : carte en verre autour */
            gx_rrect_fill(cx - w / 2 - 7 + 2, y - 7 + 6, w + 14, h + 14, 14, RGBA(0, 0, 0, 60), RGBA(0, 0, 0, 80));
            gx_rrect_fill(cx - w / 2 - 7, y - 7, w + 14, h + 14, 14, RGBA(40, 44, 72, 230), RGBA(26, 29, 50, 236));
            gx_rrect_stroke(cx - w / 2 - 7, y - 7, w + 14, h + 14, 14, 1.0f, RGBA(255, 255, 255, 26));
            if (near > 0.05f) gx_glow(cx - w / 2 - 7, y - 7, w + 14, h + 14, 14, nv_acc((u8)(120 * near)), 4);
        } else if (st == 1) {                              /* Moderne : ombre portee */
            gx_rrect_fill(cx - w / 2 + 3, y + 5, w, h, 6, RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 90));
        } else if (near > 0.05f) {                         /* Aurore : halo en degrade */
            gx_glow(cx - w / 2, y, w, h, 8, au_mix(0.5f, (u8)(150 * near)), 5);
        }
        nv_raw = 0;
        int big = on && gl[k].post >= 0 && gl_big.game == k && gl_big.tex;
        if (big) draw_tex_fit(&gl_big.obj, 256, 256, 192, 256, cx - w / 2, y, w, h);
        else gl_draw_cover(k, cx - w / 2, y, w, h, on);
        if (st == 3) {                                     /* reflet sur le sol */
            GXTexObj *ro = big ? &gl_big.obj : gl[k].post >= 0 ? gl_cover(k, 0) : NULL;
            if (ro) gl_tex_refl(ro, big ? 256 : 128, big ? 256 : 128, big ? 192 : 96, big ? 256 : 128, cx - w / 2, y, w, h, h * 0.30f, (u8)(70 + 40 * near));
        }
        nv_raw = 1;
        f32 dm = ad < 1 ? ad * (st == 3 ? 130.0f : 90.0f) : (st == 3 ? 130.0f : 90.0f) + (ad - 1) * 40.0f;
        u8 dim = (u8)(dm > 210 ? 210 : dm);
        col_t bg = st == 3 ? RGBA(4, 4, 8, dim) : st == 2 ? RGBA(16, 18, 34, dim) : RGBA(40, 40, 44, dim);
        if (dim) gx_rect(cx - w / 2, y, w, h, bg);
        if (st == 1 && near > 0.05f) gx_rrect_fill(cx - 22, y + h + 8, 44, 3, 1.5f, RGBA(mod_acc[0], mod_acc[1], mod_acc[2], (u8)(255 * near)), RGBA(mod_acc[0], mod_acc[1], mod_acc[2], (u8)(255 * near)));
        nv_raw = 0;
        gp_zone(cx - w / 2, y, w, h, &gl_sel, k, GPZ_SEL);
    }
    scr_viewport();
    /* le jeu choisi : nom, puis ID, region, taille ; position */
    gcg_t *g = &gl[gl_sel];
    gl_title_of(gl_sel, d, sizeof(d));
    fit_text(F_LARGE, d, 560, buf, sizeof(buf));
    f32 ty = st == 3 ? 380 : 372;
    nv_raw = 1;
    gx_text_c(F_LARGE, 320, ty, buf, RGBA(255, 255, 255, 255));
    if (g->dvd) snprintf(d, sizeof(d), "%s", TR("Lecteur de la console"));
    else {
        char sz[32]; gl_size_txt(g->size, sz, sizeof(sz));
        const char *reg = g->id[3] == 'E' ? "USA" : g->id[3] == 'P' ? "Europe" : g->id[3] == 'J' ? "Japon" : g->id[3] == 'F' ? "France" : g->id[3] == 'D' ? "Allemagne" : "";
        snprintf(d, sizeof(d), "%s  \xC2\xB7  %s%s%s", g->id, TR(reg), reg[0] ? "  \xC2\xB7  " : "", sz);
    }
    col_t sub = st == 3 ? RGBA(170, 170, 195, 255) : st == 2 ? RGBA(160, 168, 200, 255) : RGBA(170, 170, 178, 255);
    gx_text_c(F_SMALL, 320, ty + 19, d, sub);
    snprintf(d, sizeof(d), "%d / %d", gl_sel + 1, gl_n);
    gx_text_r(F_SMALL, 614, 404, d, sub);
    if (g->fav) { draw_star1(30, 400, 6.0f, RGBA(255, 210, 90, 255)); gx_text1(F_SMALL, 40, 404, TR("Favori"), RGBA(255, 210, 90, 255)); }
    nv_raw = 0;
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
            cov = gl_cover(k, gl_may_load(k));
            if (cov && !had) gl_loaded_frame++;
        }
        if (cov) draw_tex_fit(cov, 128, 128, 96, 128, 26, y + 2, 19, 25);
        else { GXTexObj *b = gl_banner(k); if (b) draw_tex_fit(b, 96, 32, 96, 32, 24, y + 8, 26, 9); }
        gl_title_of(k, d, sizeof(d));
        fit_text(F_MED, d, 380, buf, sizeof(buf));
        gx_text1(F_MED, 56, y + 21, buf, on ? RGBA(255, 255, 255, 255) : RGBA(215, 205, 245, 255));
        gl_size_txt(gl[k].size, sz, sizeof(sz));
        if (gl[k].dvd) snprintf(d, sizeof(d), "%s", TR("vrai lecteur"));
        else snprintf(d, sizeof(d), "%s   %s", gl[k].id, sz);
        gx_text_r(F_SMALL, 610, y + 20, d, on ? RGBA(255, 230, 170, 255) : RGBA(170, 155, 225, 255));
    }
    scr_viewport();
}
static void gl_draw_showcase(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    gcg_t *g = &gl[gl_sel];
    if (now_ms() - last_input_ms > 150) gl_load_big(gl_sel);     /* (pas pendant le defilement ; v92 : en fond) */
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

/* grande jaquette (niveau 0 : 256x256, dont 192x256 d'image) et capture du jeu k : v92 : demandees au
   thread de fond (gl_job_post), affichees des qu'elles sont lues */
static void gl_load_big(int k) {
    gcg_t *g = &gl[k];
    if (g->post >= 0 && gl_big.game != k) { gl_job_post(GJ_BIG, k); return; }
    if (g->still >= 0 && gl_bigst.game != k) gl_job_post(GJ_STILL, k);
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
    f32 hub_end = 0;
    if (!gl_detail && tab == TAB_GAMES) hub_end = gm_hub_bar(tx + 6, 85);   /* v84 : jeux / applis / cartes (stick C) */
    else {
        snprintf(buf, sizeof(buf), gl_detail ? "Mes jeux GameCube (%d)  >  fiche" : "Mes jeux GameCube (%d)", gl_n - gl_has_dvd());
        gx_text1(F_SMALL, tx + 6, 99, buf, RGBA(200, 190, 240, 255));
    }
    if (!gl_post.fp && hub_end + 12 < 590 - text_width(F_SMALL, TR("(pas de jaquettes)"), 1))
        gx_text_r(F_SMALL, 590, 99, TR("(pas de jaquettes)"), RGBA(150, 135, 210, 255));
    /* une banniere a lire par image, quand on ne bouge pas */
    gl_job_done();                                           /* v92 : resultat du thread de fond */
    if (gl_n && now_ms() - last_input_ms > 150 && gj_state == 0) {   /* (v92 : la banniere est lue en fond) */
        int first = gl_detail ? gl_sel : (gl_sel / GL_COLS - (gl_sel / GL_COLS > 0)) * GL_COLS;
        if (first < 0) first = 0;
        for (int k = first; k < gl_n && k < first + GL_COLS * 3; k++)
            if (gl[k].bstate == 0 || (gl[k].bstate == 1 && !gl_banner(k) && (gl[k].post < 0 || gl_detail))) { gl_job_post(GJ_BANNER, k); break; }
    }
    if (!gl_n) {
        gx_text1(F_MED, 34, 150, "Aucun jeu GameCube trouv\xC3\xA9 sur la carte SD", RGBA(255, 255, 255, 255));
        gx_text1(F_SMALL, 34, 176, "Jeux reconnus : .iso, .gcm, .ciso (et .nkit.iso), dans n'importe quel dossier (3 niveaux).", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 34, 196, "Jaquettes : SD:/gc-mp3/jaquettes/posters.pak (+ stills.pak, descriptions.txt).", RGBA(190, 180, 235, 255));
        static const char *const k[] = { "Y", "B" };
        static const char *const l[] = { "ACTUALISER", "RETOUR" };
        draw_legend_keys(k, l, 2);
        return;
    }
    if (gl_sel >= gl_n) gl_sel = gl_n - 1;
    gcg_t *g = &gl[gl_sel];
    if (gl_detail && g->dvd) {                                 /* v97 : fiche du vrai lecteur */
        gl_card_dvd(28, 112, 188, 250, 1);
        gx_text1(F_LARGE, 236, 140, TR("Disque de la console"), RGBA(255, 255, 255, 255));
        gl_wrap(F_SMALL, "Lance le jeu qui est dans le lecteur de la GameCube, comme le BIOS de la console : "
                         "le vrai lecteur lit le jeu (pas la carte SD). Mets le disque et ferme le couvercle avant.", 236, 172, 380, 18, 5, RGBA(215, 205, 245, 255));
        gl_wrap(F_SMALL, "Pas de A + Z + START dans ce mode : pour revenir, red\xC3\xA9marre la console.", 236, 272, 380, 18, 2, RGBA(170, 155, 225, 255));
        if (now_ms() - gl_msg_t < 6000) { char m[200]; fit_text(F_SMALL, gl_msg, 586, m, sizeof(m)); gx_text1(F_SMALL, 30, 404, m, RGBA(255, 210, 120, 255)); }
        static const char *const k[] = { "A", "B" };
        static const char *const l[] = { "LANCER", "RETOUR" };
        draw_legend_keys(k, l, 2);
        if (gl_launch_sel >= 0) {
            const f32 w = 420, h = 100, x = 110, y = 170;
            gx_rrect_fill(x, y, w, h, 12, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
            draw_panel(x, y, w, h);
            gx_text_c(F_MED, x + w / 2, y + 34, "Lancer le disque de la console ?", RGBA(255, 255, 255, 255));
            gx_text_c(F_SMALL, x + w / 2, y + 58, TR("Le jeu est lu par le vrai lecteur"), RGBA(190, 180, 235, 255));
            gx_text_c(F_SMALL, x + w / 2, y + 84, "A : lancer     B : annuler", RGBA(255, 210, 120, 255));
        }
        return;
    }
    if (gl_detail) {
        if (now_ms() - last_input_ms > 100) gl_load_big(gl_sel);   /* v92 : en fond, pas pendant qu'on fait defiler les fiches */
        /* v45 : en changeant de jeu, la fiche glisse (gauche / droite) */
        gl_slide *= gl_anim ? expf(-ui_dt * 11.0f) : 0.0f;
        if (fabsf(gl_slide) < 0.004f) gl_slide = 0;
        const f32 ox = gl_slide * 120.0f;
        scr_scissor(16, 104, 608, 300);
        const f32 cx = 28 + ox, cy = 112, cw = 192 * 0.98f, chh = 256 * 0.98f;
        if (g->post >= 0 && gl_big.game == gl_sel && gl_big.tex) {
            gx_glow(cx, cy, cw, chh, 8, RGBA(150, 80, 255, 110), 3);
            draw_tex_fit(&gl_big.obj, 256, 256, 192, 256, cx, cy, cw, chh);
        } else {
            GXTexObj *sc = g->post >= 0 ? gl_cover(gl_sel, 1) : NULL;   /* v92 : la petite jaquette en attendant la grande */
            if (sc) { gx_glow(cx, cy, cw, chh, 8, RGBA(150, 80, 255, 110), 3); draw_tex_fit(sc, 128, 128, 96, 128, cx, cy, cw, chh); }
            else gl_card_banner(gl_sel, cx, cy, cw, chh, 1);
        }
        const f32 rx = 236 + ox * 1.4f; const int rw = 380;
        int nl = gl_wrap(F_LARGE, g->title, rx, 134, rw, 26, 2, RGBA(255, 255, 255, 255));
        f32 y = 134 + nl * 26 - 4;
        char sz[32]; gl_size_txt(g->size, sz, sizeof(sz));
        const char *reg = g->id[3] == 'E' ? "USA" : g->id[3] == 'P' ? "Europe" : g->id[3] == 'J' ? "Japon" : g->id[3] == 'F' ? "France" : g->id[3] == 'D' ? "Allemagne" : "";
        snprintf(buf, sizeof(buf), "%s   %s%s%s%s", g->id, TR(reg), reg[0] ? "   " : "", sz, g->ciso ? "   CISO" : "");
        gx_text1(F_SMALL, rx, y + 4, buf, RGBA(255, 210, 120, 255));
        if (g->disc) { snprintf(buf, sizeof(buf), "Disque %d", g->disc + 1); gx_text_r(F_SMALL, 614, y + 4, buf, RGBA(255, 210, 120, 255)); }
        if (g->fav) { draw_star1(rx + 7, 120, 7.0f, RGBA(255, 210, 90, 255)); gx_text1(F_SMALL, rx + 18, 124, TR("Favori  (L)"), RGBA(255, 210, 90, 255)); }   /* v76 */
        else gx_text1(F_SMALL, rx, 124, TR("L : ajouter aux favoris"), RGBA(130, 118, 180, 255));
        text_display(g->path, d, sizeof(d));
        fit_text(F_SMALL, d, rw, buf, sizeof(buf));
        gx_text1(F_SMALL, rx, y + 22, buf, RGBA(150, 135, 210, 255));
        int has_st = g->still >= 0 && gl_bigst.game == gl_sel && gl_bigst.tex;
        f32 dy = y + 46;
        int maxl = has_st ? 3 : 11;
        if (g->desc) gl_wrap(F_SMALL, g->desc, rx, dy, rw, 17, maxl, RGBA(215, 205, 245, 255));
        else gx_text1(F_SMALL, rx, dy, g->bstate == 0 ? "Lecture de la banni\xC3\xA8re..." : "Pas de description pour ce jeu.", RGBA(170, 155, 225, 255));
        if (has_st) { f32 sw = 176, sh = 132; draw_tex_fit(&gl_bigst.obj, 320, 240, 320, 240, rx, 396 - sh - 6, sw, sh); }
        {                                                      /* v46 : cheats du jeu, actives d'ici (X) */
            int cn = now_ms() - last_input_ms > 150 || !gl_anim ? gl_cheats_on(gl_sel) : -2;
            if (cn >= 0 || cn == -1) {
                if (cn > 0) snprintf(buf, sizeof(buf), cn == 1 ? TR("Cheats : 1 activ\xC3\xA9  (X pour changer)") : TR("Cheats : %d activ\xC3\xA9s  (X pour changer)"), cn);
                else if (cn == 0) snprintf(buf, sizeof(buf), "%s", TR("Cheats : aucun activ\xC3\xA9  (X pour en choisir)"));
                else snprintf(buf, sizeof(buf), "%s", TR("Pas de fichier de cheats  (X pour en ajouter)"));
                gx_text1(F_SMALL, has_st ? rx + 186 : rx, g->runs ? 362 : 380, buf, cn > 0 ? RGBA(130, 230, 150, 255) : RGBA(170, 155, 225, 255));
            }
        }
        if (g->runs) {                                         /* v45 : lancements depuis GC-MP3 */
            time_t lt = (time_t)g->last;
            struct tm *tm = g->last ? localtime(&lt) : NULL;
            if (tm) snprintf(buf, sizeof(buf), "%s %u   %s %02d/%02d/%04d", TR("Lanc\xC3\xA9 :"), (unsigned)g->runs, TR("dernier :"), tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
            else snprintf(buf, sizeof(buf), "%s %u", TR("Lanc\xC3\xA9 :"), (unsigned)g->runs);
            if (g->play >= 60) {                               /* v73 : temps de jeu (Resident) */
                size_t bl = strlen(buf);
                if (g->play >= 3600) snprintf(buf + bl, sizeof(buf) - bl, "   %s %u h %02u", TR("jou\xC3\xA9 :"), (unsigned)(g->play / 3600), (unsigned)(g->play / 60 % 60));
                else snprintf(buf + bl, sizeof(buf) - bl, "   %s %u min", TR("jou\xC3\xA9 :"), (unsigned)(g->play / 60));
            }
            gx_text1(F_SMALL, has_st ? rx + 186 : rx, 380, buf, RGBA(170, 155, 225, 255));
        }
        scr_viewport();
        if (now_ms() - gl_msg_t < 6000) { char m[200]; fit_text(F_SMALL, gl_msg, 586, m, sizeof(m)); gx_text1(F_SMALL, 30, 404, m, RGBA(255, 210, 120, 255)); }
        static const char *const k[] = { "A", "X", "Y", "R", "B" };
        static const char *const l[] = { "LANCER", "CHEATS", "R\xC3\x89GLAGES", "ANALYSE", "RETOUR" };
        draw_legend_keys(k, l, 5);
        if (gl_launch_sel >= 0) {                              /* confirmation */
            const f32 w = 420, h = 112, x = 110, y = 170;
            gx_rrect_fill(x, y, w, h, 12, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
            draw_panel(x, y, w, h);
            gx_text_c(F_MED, x + w / 2, y + 32, "Lancer ce jeu ?", RGBA(255, 255, 255, 255));
            fit_text(F_SMALL, g->title, (int)w - 30, buf, sizeof(buf));
            gx_text_c(F_SMALL, x + w / 2, y + 56, buf, RGBA(255, 210, 120, 255));
            int cn = gl_cheats_on(gl_sel);
            if (cn > 0) {
                snprintf(buf, sizeof(buf), cn == 1 ? TR("Avec 1 cheat activ\xC3\xA9 (lanceur de secours)") : TR("Avec %d cheats activ\xC3\xA9s (lanceur de secours)"), cn);
                gx_text_c(F_SMALL, x + w / 2, y + 76, buf, RGBA(130, 230, 150, 255));
            } else
            gx_text_c(F_SMALL, x + w / 2, y + 76, TR("Resident : A + Z + START tenus 1 s = retour \xC3\xA0 GC Station"), RGBA(190, 180, 235, 255));
            gx_text_c(F_SMALL, x + w / 2, y + 98, "A : lancer     X : lanceur de secours     B : annuler", RGBA(255, 210, 120, 255));
        }
        snprintf(buf, sizeof(buf), "%d / %d   (gauche / droite)", gl_sel + 1, gl_n);
        gx_text_r(F_SMALL, 614, 396, buf, RGBA(150, 135, 210, 255));
        nat_draw_qr();                                         /* v51 : rapport d'analyse sur le telephone */
        return;
    }
    if (gl_styled()) gl_draw_styled(); else
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
    if (g->dvd) snprintf(d, sizeof(d), "%s", TR("vrai lecteur"));
    else if (g->disc) snprintf(d, sizeof(d), "%s   disque %d   %s", g->id, g->disc + 1, sz);
    else snprintf(d, sizeof(d), "%s   %s", g->id, sz);
    fit_text(F_MED, g->dvd ? TR(g->title) : g->title, 420, buf, sizeof(buf));
    if (now_ms() - gl_msg_t < 3500) gx_text1(F_SMALL, 30, 400, gl_msg, RGBA(255, 210, 120, 255));
    else if (gl_view != GLV_CAROUSEL && gl_view != GLV_SHOW && !gl_styled()) { gx_text1(F_MED, 30, 400, buf, RGBA(255, 255, 255, 255)); gx_text_r(F_SMALL, 614, 400, d, RGBA(255, 210, 120, 255)); }
    snprintf(d, sizeof(d), "R : %s", TR(gl_view_name()));
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

/* ======================= lancer un jeu avec le lanceur de secours =======================
   GC Station charge SD:/gc-mp3/lanceur.dol en memoire et le demarre avec l'argument
   "Autoload=sdc:/chemin/du/jeu.iso" (v90 : plus de Swiss / Indigo). */
static int gl_launch_sel = -1;                      /* fenetre "Lancer ?" ouverte (-1 = non) */
/* petit programme copie a 0x80001800 : recopie les sections du .dol a leur
   place (par-dessus GC-MP3), vide les caches, puis saute au debut du .dol.
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
#define GL_CMD_AT   (0x81700000u - 0x400u)        /* ligne de commande : en haut de la memoire */
#define LD_DOL  GCD "/lanceur.dol"                 /* v44 : lanceur de jeux GC-MP3 (lanceur.c) */
#define LD_VER  GCD "/lanceur.txt"
static void ld_fetch(int loud);
static void as_self(char *out, int cap);
static const char *gl_arg3 = NULL;                 /* v44 : 3e argument (GCMP3=... : retour sur GC-MP3) */
static const char *gl_arg4 = NULL;                 /* v46 : 4e argument (GCMP3TITLE=... : nom du jeu, ecran de chargement) */
static const char *gl_boot_title = NULL;           /* v46 : nom du jeu lance */

/* lanceur de secours avec "Autoload=<jeu>" (path = chemin sd:/...) */
static const char *gl_boot_loader(const char *path);
static int gl_boot_cheats = 0;                         /* v31 : ajoute "AutoCheats=Yes" (cheats actives pour ce jeu) */
static const char *gl_launch(int k) {                  /* retourne un message d'erreur, ou ne revient pas */
    if (k < 0 || k >= gl_n) return "Jeu introuvable";
    if (access(gl[k].path, R_OK) != 0) return "Jeu introuvable : Y pour actualiser la liste";
    char p[NAME_LEN]; struct stat st;
    snprintf(p, sizeof(p), "%s/%.6s.chtsel", CH_DIR, gl[k].id);
    gl_boot_cheats = stat(p, &st) == 0 && st.st_size > 8;
    tl_recent_add(gl[k].path);
    gl_stats_bump(k);                                  /* v45 : compteur de lancements */
    gl_boot_title = gl[k].title;
    const char *e = gl_boot_loader(gl[k].path);
    gl_boot_title = NULL;
    gl_boot_cheats = 0;
    return e;
}
/* v35 : lance n'importe quel .dol (lanceur, ou une appli), avec jusqu'a 2 arguments */
static const char *gl_boot_dol(const char *dolpath, const char *a1, const char *a2, const char *what);
static int ld_cache;
static const char *gl_boot_loader(const char *path) {
    static const char *const dev[3] = { "sdc:", "sda:", "sdb:" };
    char a1[NAME_LEN + 32];
    if (!path) return "Jeu introuvable";
    snprintf(a1, sizeof(a1), "Autoload=%s%s", dev[sd_slot >= 0 && sd_slot < 3 ? sd_slot : 0], path + strlen(SD_ROOT));
    if (ld_ready()) {
        static char a3[NAME_LEN + 16];
        char me[NAME_LEN + 8];
        as_self(me, sizeof(me));
        snprintf(a3, sizeof(a3), "GCMP3=%s", me);
        gl_arg3 = a3;
        static char a4[112];                       /* v46 : nom du jeu (Latin-1 : police de la console) */
        a4[0] = 0;
        if (gl_boot_title) {
            int o = snprintf(a4, sizeof(a4), "GCMP3TITLE=");
            for (const u8 *q = (const u8 *)gl_boot_title; *q && o < (int)sizeof(a4) - 1; q++) {
                if (*q < 0x80) a4[o++] = (char)*q;
                else if ((*q == 0xC2 || *q == 0xC3) && (q[1] & 0xC0) == 0x80) { a4[o++] = (char)(((*q & 3) << 6) | (q[1] & 0x3F)); q++; }
                else if (*q >= 0xC0) { a4[o++] = '?'; while ((q[1] & 0xC0) == 0x80) q++; }
            }
            a4[o] = 0;
            gl_arg4 = a4;
        }
        const char *e = gl_boot_dol(LD_DOL, a1, gl_boot_cheats ? "AutoCheats=Yes" : NULL, "jeu");
        gl_arg3 = NULL; gl_arg4 = NULL;
        crumb("lanceur GC Station : %s", e ? e : "?");
        if (e && strncmp(e, "(simulateur", 11)) { remove(LD_DOL); ld_cache = -1; ld_fetch(0); return "Lanceur de secours ab\xC3\xAEm\xC3\xA9 : nouveau t\xC3\xA9l\xC3\xA9" "chargement (ou A : Resident)"; }
        return e;
    }
    ld_fetch(1);                                   /* (pas encore la : telecharge pour la prochaine fois) */
    return "Lanceur de secours pas encore t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9 (R\xC3\xA9glages > Jeux) : A lance avec Resident";
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
    /* arguments : chemin du .dol, puis a1 / a2 (ex. Autoload=sdX:/chemin pour le lanceur) */
    char cmd[NAME_LEN * 3 + 96];
    int cl = snprintf(cmd, sizeof(cmd), "%s", dolpath);
    if (a1) cl += 1 + snprintf(cmd + cl + 1, sizeof(cmd) - cl - 1, "%s", a1);
    if (a2) cl += 1 + snprintf(cmd + cl + 1, sizeof(cmd) - cl - 1, "%s", a2);   /* (ex. AutoCheats=Yes : le lanceur applique les cheats coches) */
    if (gl_arg3) cl += 1 + snprintf(cmd + cl + 1, sizeof(cmd) - cl - 1, "%s", gl_arg3);
    if (gl_arg4 && cl + 2 < (int)sizeof(cmd)) cl += 1 + snprintf(cmd + cl + 1, sizeof(cmd) - cl - 1, "%s", gl_arg4);
    if (cl >= (int)sizeof(cmd)) cl = (int)sizeof(cmd) - 1;
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
    bg_quiesce();                                        /* v96 : la lecture en fond finit avant le demontage */
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
      if (a2 < cmd + cl) { a2 += strlen(a2) + 1; if (a2 < cmd + cl) printf(" arg3=[%s]", a2); }
      if (a2 < cmd + cl) { a2 += strlen(a2) + 1; if (a2 < cmd + cl) printf(" arg4=[%s]", a2); } }
    printf("\n");
    free(dol);
    return "(simulateur : lancement simule)";
#endif
}

/* reglages du lanceur de secours (v90 : dans SD:/gc-mp3/lanceur, plus dans SD:/swiss) */
#define LD_DIR      GCD "/lanceur"
#define LD_INI      LD_DIR "/settings/global.ini"
static void as_self(char *out, int cap) {                    /* "sdc:/chemin/gc-mp3player.dol" */
    static const char *const dev[3] = { "sdc:", "sda:", "sdb:" };
    up_pick_target();
    const char *rel = up_target;
    if (!strncasecmp(rel, SD_ROOT, strlen(SD_ROOT))) rel += strlen(SD_ROOT);
    else { const char *c = strchr(rel, ':'); if (c && c - rel <= 5) rel = c + 1; }   /* ("sdc:/...", "carda:/...") */
    snprintf(out, cap, "%s%s%s", dev[sd_slot >= 0 && sd_slot < 3 ? sd_slot : 0], rel[0] == '/' ? "" : "/", rel);
}
/* v57 : demarrer la console sur GC Station. PicoLoader (comme iplboot) lance SD:/ipl.dol :
   GC Station s'y copie, l'ancien ipl.dol est garde sous ipl-avant-gcstation.dol (v90 : avant
   ipl-swiss.dol, renomme une fois). Le bouton Reset et le retour en jeu ramenent alors sur
   GC Station. La mise a jour remplace aussi ipl.dol (update.c). */
#define IPL_DOL SD_ROOT "/ipl.dol"
#define IPL_OLD SD_ROOT "/ipl-avant-gcstation.dol"
#define IPL_OLD_V89 SD_ROOT "/ipl-swiss.dol"
static int ipl_cache = -1;
static int ipl_get(void) {
    if (ipl_cache < 0) {
        struct stat st;
        ipl_cache = sd_ok && !sd_virtual && stat(IPL_DOL, &st) == 0 && up_is_me(IPL_DOL);   /* (v92 : resultat garde, update.c) */
    }
    return ipl_cache;
}
static int ipl_copy(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    char part[NAME_LEN + 8];
    snprintf(part, sizeof(part), "%s.part", dst);
    FILE *out = fopen(part, "wb");
    if (!out) { fclose(in); return 0; }
    static u8 b[65536];
    size_t n;
    int ok = 1;
    while ((n = fread(b, 1, sizeof(b), in)) > 0) { if (fwrite(b, 1, n, out) != n) { ok = 0; break; } service_audio(); }
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    struct stat st;
    if (!ok || stat(part, &st) != 0 || st.st_size < 0x100) { remove(part); return 0; }
    remove(dst);
    if (rename(part, dst) != 0) { remove(part); return 0; }
    return 1;
}
static const char *ipl_set(int on) {
    if (!sd_ok || sd_virtual) return "Pas de carte SD";
    struct stat st;
    up_found[0] = 0;
    if (on) {
        if (ipl_get()) return NULL;
        up_pick_target();
        char src[NAME_LEN];
        snprintf(src, sizeof(src), "%s", up_target);
        if (!strcasecmp(src, IPL_DOL) || stat(src, &st) != 0) return "GC Station introuvable sur la carte SD";
        ui_status("Copie de GC Station dans ipl.dol...");
        int had = stat(IPL_DOL, &st) == 0;
        if (had) {
            if (stat(IPL_OLD, &st) == 0) { remove(SD_ROOT "/ipl-ancien.dol"); if (rename(IPL_DOL, SD_ROOT "/ipl-ancien.dol") != 0) return "Impossible de garder l'ancien ipl.dol"; }
            else if (rename(IPL_DOL, IPL_OLD) != 0) return "Impossible de garder l'ancien ipl.dol";
        }
        if (!ipl_copy(src, IPL_DOL)) {
            if (had && stat(IPL_DOL, &st) != 0) rename(stat(SD_ROOT "/ipl-ancien.dol", &st) == 0 ? SD_ROOT "/ipl-ancien.dol" : IPL_OLD, IPL_DOL);
            ipl_cache = -1;
            return "Copie impossible (carte pleine ?)";
        }
        ipl_cache = 1;
        crumb("ipl.dol = GC Station (ancien : %s)", had ? IPL_OLD : "aucun");
        return had ? "La console d\xC3\xA9marre sur GC Station (ancien ipl.dol gard\xC3\xA9 : ipl-avant-gcstation.dol)"
                   : "La console d\xC3\xA9marre sur GC Station";
    }
    if (!ipl_get()) return NULL;
    if (stat(IPL_OLD, &st) != 0) return "Pas d'ancien ipl.dol \xC3\xA0 remettre (ipl-avant-gcstation.dol)";
    /* GC Station doit rester quelque part sur la carte */
    char other[NAME_LEN];
    if (!up_find_dol(SD_ROOT, 2, other, sizeof(other)) && !ipl_copy(IPL_DOL, SD_ROOT "/gc-mp3player.dol")) return "Copie de GC Station impossible";
    remove(IPL_DOL);
    if (rename(IPL_OLD, IPL_DOL) != 0) { ipl_cache = -1; return "Impossible de remettre l'ancien ipl.dol"; }
    ipl_cache = 0;
    crumb("ipl.dol remis (ancien)");
    return "La console d\xC3\xA9marre de nouveau sur l'ancien ipl.dol";
}

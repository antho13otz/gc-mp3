// GC Station v207 : jukebox des musiques des jeux GameCube (inclus dans main.c, apres gclib.c).
// Fiche d'un jeu (onglet JEUX) > bas : les musiques rangees DANS l'image du jeu (.iso / .gcm / .ciso) sont
// listees dans l'onglet RETRO et se jouent directement depuis l'image, sans rien copier :
//   - AST (Mario Kart Double Dash, Zelda Twilight Princess, Pikmin 2...)
//   - HPS (Smash Bros. Melee, Kirby Air Ride...)
//   - DSP (Metroid Prime, beaucoup d'autres ; paires _L / _R)
//   - ADP (flux du lecteur : Zelda Wind Waker, Super Mario Sunshine...)
// Les jeux dont la musique est jouee note par note (sequences) n'ont pas de fichier a lire : message.
// Un morceau s'appelle "chemin/du/jeu.iso#/dossier/musique.ast" (lu par gcf_* de tab_retro.c).

typedef struct { char *p; u32 off, size; } jbt_t;   /* musique trouvee : chemin dans le jeu, position, taille */
static char jb_iso[NAME_LEN];                       /* jeu dont la table est en memoire */
static char jb_title[96];
static jbt_t *jb_t = NULL;
static int jb_nt = 0;
typedef struct { gdisc_t d; u32 base; } jbh_t;

static const char *jb_game_name(void) { return jb_title; }
static void jb_free_table(void) {
    for (int i = 0; i < jb_nt; i++) free(jb_t[i].p);
    free(jb_t);
    jb_t = NULL;
    jb_nt = 0;
    jb_iso[0] = 0;
}
static int jb_audio_ext(const char *n, u32 size) {
    const char *dot = strrchr(n, '.');
    if (!dot) return 0;
    if (!strcasecmp(dot, ".ast") || !strcasecmp(dot, ".hps")) return size >= 16 * 1024;
    if (!strcasecmp(dot, ".dsp") || !strcasecmp(dot, ".adp")) return size >= 192 * 1024;   /* (plus petit : des bruitages) */
    return 0;
}
/* lit la table des fichiers (FST) d'une image et garde les musiques ; -1 = image illisible */
static int jb_load(const char *iso) {
    if (!strcmp(iso, jb_iso)) return jb_nt;
    jb_free_table();
    gdisc_t d;
    if (!gd_open(&d, iso)) return -1;
    u8 h[0x440];
    if (!gd_read(&d, 0, h, sizeof(h)) || be32(h + 0x1C) != 0xC2339F3Du) { gd_close(&d); return -1; }
    u32 fo = be32(h + 0x424), fs = be32(h + 0x428);
    if (fs < 12 || fs > (8u << 20)) { gd_close(&d); return -1; }
    u8 *f = (u8 *)malloc(fs + 1);
    if (!f || !gd_read(&d, fo, f, fs)) { free(f); gd_close(&d); return -1; }
    gd_close(&d);
    f[fs] = 0;
    u32 n = be32(f + 8);
    if (n < 1 || n > fs / 12) { free(f); return -1; }
    const char *names = (const char *)f + n * 12;
    u32 nlen = fs - n * 12;
    int cap = 0;
    struct { u32 end; int plen; } st[16];
    int sp = 0;
    char path[NAME_LEN];
    path[0] = 0;
    int plen = 0;
    for (u32 i = 1; i < n; i++) {
        while (sp > 0 && i >= st[sp - 1].end) { sp--; plen = st[sp].plen; path[plen] = 0; }
        const u8 *e = f + i * 12;
        u32 no = be32(e) & 0xFFFFFF;
        if (no >= nlen) continue;
        const char *nm = names + no;
        if (e[0]) {                                                    /* dossier */
            u32 end = be32(e + 8);
            if (sp < 16 && end > i && end <= n) {
                st[sp].end = end; st[sp].plen = plen; sp++;
                plen += snprintf(path + plen, sizeof(path) - plen, "/%s", nm);
                if (plen >= (int)sizeof(path)) plen = (int)sizeof(path) - 1;
            } else i = end > i && end <= n ? end - 1 : i;             /* (trop profond : on saute) */
            continue;
        }
        u32 off = be32(e + 4), size = be32(e + 8);
        if (!jb_audio_ext(nm, size)) continue;
        if (jb_nt == cap) {
            cap = cap ? cap * 2 : 64;
            jbt_t *nt = (jbt_t *)realloc(jb_t, sizeof(jbt_t) * cap);
            if (!nt) break;
            jb_t = nt;
        }
        char full[NAME_LEN];
        snprintf(full, sizeof(full), "%s/%s", path, nm);
        jb_t[jb_nt].p = strdup(full);
        jb_t[jb_nt].off = off;
        jb_t[jb_nt].size = size;
        if (jb_t[jb_nt].p) jb_nt++;
    }
    free(f);
    snprintf(jb_iso, sizeof(jb_iso), "%s", iso);
    return jb_nt;
}
static int jb_split(const char *path, char *iso, int cap, const char **inner) {
    const char *hsh = strstr(path, "#/");
    if (!hsh || hsh - path >= cap) return 0;
    memcpy(iso, path, hsh - path);
    iso[hsh - path] = 0;
    *inner = hsh + 1;
    return 1;
}
static const jbt_t *jb_find(const char *path) {
    char iso[NAME_LEN];
    const char *in;
    if (!jb_split(path, iso, sizeof(iso), &in) || jb_load(iso) < 0) return NULL;
    for (int i = 0; i < jb_nt; i++) if (!strcasecmp(jb_t[i].p, in)) return &jb_t[i];
    return NULL;
}
/* (pour gcf_* de tab_retro.c) */
static void *jb_iso_open(const char *path, u32 *size) {
    const jbt_t *t = jb_find(path);
    if (!t) return NULL;
    jbh_t *h = (jbh_t *)calloc(1, sizeof(jbh_t));
    if (!h) return NULL;
    if (!gd_open(&h->d, jb_iso)) { free(h); return NULL; }
    h->base = t->off;
    *size = t->size;
    return h;
}
static int jb_iso_read(void *hv, u32 off, void *out, u32 n) { jbh_t *h = (jbh_t *)hv; return gd_read(&h->d, h->base + off, out, n); }
static void jb_iso_close(void *hv) { jbh_t *h = (jbh_t *)hv; if (!h) return; gd_close(&h->d); free(h); }
static void jb_tags(const char *path, tags_t *tg) {
    if (!jb_iso_path(path)) return;
    char iso[NAME_LEN];
    const char *in;
    if (!jb_split(path, iso, sizeof(iso), &in)) return;
    if (!strcmp(iso, jb_iso) && jb_title[0]) snprintf(tg->album, sizeof(tg->album), "%s", jb_title);
    else snprintf(tg->album, sizeof(tg->album), "%s", display_name(base_of(iso)));
    char *dot = strrchr(tg->title, '.');                /* "boss_L.dsp" -> "boss L" */
    if (dot) *dot = 0;
    for (char *c = tg->title; *c; c++) if (*c == '_') *c = ' ';
}

/* fiche d'un jeu > bas : ses musiques dans l'onglet RETRO */
static void jb_open_game(int g) {
    if (g < 0 || g >= gl_n) return;
    if (gl[g].dvd) { gl_note("Musiques : seulement pour les jeux de la carte SD (pas le disque)"); return; }
    int n = jb_load(gl[g].path);
    if (n < 0) { gl_note("Image du jeu illisible"); return; }
    if (n == 0) { gl_note("Pas de musique lisible dans ce jeu (sa musique est jou\xC3\xA9" "e note par note)"); return; }
    snprintf(jb_title, sizeof(jb_title), "%s", gl[g].title);
    if (!sd_ok) { gl_note("Pas de carte SD"); return; }
    int first = -1, added = 0;
    LSC_LOCK();
    for (int i = 0; i < jb_nt && nfiles < MAX_FILES; i++) {
        char p[NAME_LEN];
        if (snprintf(p, sizeof(p), "%s#%s", gl[g].path, jb_t[i].p) >= (int)sizeof(p)) continue;
        int k = bib_find(p);
        if (k < 0) {
            k = nfiles;
            snprintf(names[k], NAME_LEN, "%s", p);
            dur_cache[k] = -2;
            is_retro[k] = retro_ext_fmt(p) >= 0;
            tg_done[k] = 0; tg_artist[k][0] = tg_album[k][0] = tg_title[k][0] = 0;
            play_cnt[k] = 0; play_last[k] = 0;
            nfiles++;
            added++;
        }
        if (first < 0) first = k;
    }
    LSC_UNLOCK();
    if (added) lib_rebuild();
    snprintf(jb_filter, sizeof(jb_filter), "%s", gl[g].path);
    retro_sel = 0;
    retro_rescan();
    if (!tab_shown(TAB_RETRO)) { tab_hidden &= ~(1u << TAB_RETRO); settings_dirty = 1; }
    tab = TAB_RETRO;
    tab_enter(tab);
    if (retro_n > 0) { int k = retro_list[0]; queue_build(k); start_track(k); }
    char m[160];
    snprintf(m, sizeof(m), "%s : %d musique%s", jb_title, retro_n, retro_n > 1 ? "s" : "");
    lib_note(m);
}

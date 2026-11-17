// GC Station v207 : jukebox des musiques des jeux GameCube (inclus dans main.c, apres gclib.c).
// Fiche d'un jeu (onglet JEUX) > bas : les musiques rangees DANS l'image du jeu (.iso / .gcm / .ciso) sont
// listees dans l'onglet RETRO et se jouent directement depuis l'image, sans rien copier :
//   - AST (Mario Kart Double Dash, Zelda Twilight Princess, Pikmin 2...)
//   - HPS (Smash Bros. Melee, Kirby Air Ride...)
//   - DSP (Metroid Prime, beaucoup d'autres ; paires _L / _R)
//   - ADP (flux du lecteur : Zelda Wind Waker, Super Mario Sunshine...)
//   - v422 : ADX (CRI : F-Zero GX, Super Monkey Ball, Sonic...), seuls ou dans des archives AFS ; et les musiques
//     rangees sous un autre nom (.str, .bgm, .sng...) : reconnues a leur contenu (en-tete AST, HPS, ADX ou DSP)
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
    if (!strcasecmp(dot, ".adx")) return size >= 64 * 1024;   /* (v422) */
    return 0;
}
/* v422 : fichiers qu'on ne regarde pas de plus pres (programme, images, modeles, video, archives Nintendo) */
static int jb_skip_ext(const char *n) {
    static const char *const x[] = { ".dol", ".rel", ".map", ".tpl", ".bti", ".bmd", ".bdl", ".bck", ".btk", ".brk", ".bmg", ".bnr",
        ".thp", ".h4m", ".szs", ".rarc", ".arc", ".bin_", ".txt", ".csv", ".ini", ".fnt", ".gpl", ".tex", ".bmp", ".png", ".jpg", NULL };
    const char *dot = strrchr(n, '.');
    if (!dot) return 0;
    for (int i = 0; x[i]; i++) if (!strcasecmp(dot, x[i])) return 1;
    return 0;
}
static u32 jb_le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
/* v422 : le format d'une musique d'apres son debut (0x100 octets) : ".ast", ".hps", ".adx", ".dsp" ou NULL */
static const char *jb_sniff(const u8 *h, u32 size) {
    if (!memcmp(h, "STRM", 4) && be32(h + 0x10) >= 4000 && be32(h + 0x10) <= 96000) return ".ast";
    if (!memcmp(h, " HALPST", 7)) return ".hps";
    if (h[0] == 0x80 && h[1] == 0) {
        u32 co = ((u32)h[2] << 8) | h[3];
        if (co >= 0x10 && co + 4 <= 0x100 && co >= 2 && !memcmp(h + co - 2, "(c)CRI", 6) && h[4] == 3 && h[5] == 18 && h[6] == 4) return ".adx";
    }
    u32 ns = be32(h), nn = be32(h + 4), sr = be32(h + 8);     /* DSP standard : en-tete coherent et premier octet de donnees */
    if (ns >= 14 * 1000 && sr >= 8000 && sr <= 48000 && h[0x0E] == 0 && h[0x0F] == 0 && nn >= ns && nn <= ns / 14 * 16 + 32 &&
        nn / 2 + 0x60 <= size + 16 && h[0x3E] == 0 && (h[0x3F] == h[0x60] || h[0x3F] == 0) && (h[0x60] & 0x80) == 0) return ".dsp";
    return NULL;
}
/* v422 : une musique de plus dans la table */
static int jb_add(jbt_t **pt, int *pn, int *cap, const char *full, u32 off, u32 size) {
    if (*pn == *cap) {
        int nc = *cap ? *cap * 2 : 64;
        jbt_t *nt = (jbt_t *)realloc(*pt, sizeof(jbt_t) * nc);
        if (!nt) return 0;
        *pt = nt; *cap = nc;
    }
    (*pt)[*pn].p = strdup(full);
    (*pt)[*pn].off = off;
    (*pt)[*pn].size = size;
    if ((*pt)[*pn].p) (*pn)++;
    return 1;
}
static void jb_free_tab(jbt_t *t, int n) { if (!t) return; for (int i = 0; i < n; i++) free(t[i].p); free(t); }
/* v456 : la table des musiques d'un jeu est gardee sur la carte (GCD/jeux/musiques-ID.txt) : la 2e fois, plus
   rien a chercher dans l'image. 1re ligne : "GCJB1 <taille de l'image> <chemin>", puis "<position> <taille> <nom>". */
static void jb_cache_path(const char *id6, char *out, int cap) { snprintf(out, cap, GCD "/jeux/musiques-%.6s.txt", id6); }
static long long jb_iso_size(const char *iso) { struct stat st; return stat(iso, &st) ? -1 : (long long)st.st_size; }
static int jb_cache_read(const char *iso, const char *id6, jbt_t **pt, int *pn) {
    char p[NAME_LEN];
    jb_cache_path(id6, p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    static char line[NAME_LEN + 64];
    long long sz = -2;
    int ok = 0, cap = 0;
    if (fgets(line, sizeof(line), f) && sscanf(line, "GCJB1 %lld", &sz) == 1 && sz == jb_iso_size(iso)) {
        char *sp = strchr(line + 6, ' ');
        if (sp) { sp++; sp[strcspn(sp, "\r\n")] = 0; ok = !strcmp(sp, iso); }
    }
    while (ok && fgets(line, sizeof(line), f)) {
        unsigned o, z; int k = 0;
        if (sscanf(line, "%u %u %n", &o, &z, &k) < 2 || !k) continue;
        line[strcspn(line, "\r\n")] = 0;
        if (!jb_add(pt, pn, &cap, line + k, o, z)) break;
    }
    fclose(f);
    if (!ok) { jb_free_tab(*pt, *pn); *pt = NULL; *pn = 0; }
    return ok;
}
static void jb_cache_write(const char *iso, const char *id6, const jbt_t *t, int n) {
    char p[NAME_LEN];
    jb_cache_path(id6, p, sizeof(p));
    mkdir(GCD "/jeux", 0777);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "GCJB1 %lld %s\n", jb_iso_size(iso), iso);
    for (int i = 0; i < n; i++) fprintf(f, "%u %u %s\n", (unsigned)t[i].off, (unsigned)t[i].size, t[i].p);
    fclose(f);
}
/* lit la table des fichiers (FST) d'une image et garde les musiques ; -1 = image illisible */
/* v456 : sans les globales (le fil de fond s'en sert) ; prog : fichiers regardes ; stop : arreter */
static int jb_scan(const char *iso, jbt_t **pt, int *pn, volatile int *prog, volatile int *stop) {
    *pt = NULL; *pn = 0;
    gdisc_t d;
    if (!gd_open(&d, iso)) return -1;
    u8 h[0x440];
    if (!gd_read(&d, 0, h, sizeof(h)) || be32(h + 0x1C) != 0xC2339F3Du) { gd_close(&d); return -1; }
    u32 fo = be32(h + 0x424), fs = be32(h + 0x428);
    char id6[7]; memcpy(id6, h, 6); id6[6] = 0;
    for (int i = 0; i < 6; i++) if (!isalnum((unsigned char)id6[i])) id6[i] = '_';
    if (jb_cache_read(iso, id6, pt, pn)) { gd_close(&d); return *pn; }
    if (fs < 12 || fs > (8u << 20)) { gd_close(&d); return -1; }
    u8 *f = (u8 *)malloc(fs + 1);
    if (!f || !gd_read(&d, fo, f, fs)) { free(f); gd_close(&d); return -1; }
    f[fs] = 0;                                                     /* (v422 : l'image reste ouverte : debuts de fichiers lus) */
    u32 n = be32(f + 8);
    if (n < 1 || n > fs / 12) { free(f); gd_close(&d); return -1; }
    int nsniff = 0;
    const char *names = (const char *)f + n * 12;
    u32 nlen = fs - n * 12;
    int cap = 0;
    struct { u32 end; int plen; } st[16];
    int sp = 0;
    char path[NAME_LEN];
    path[0] = 0;
    int plen = 0;
    for (u32 i = 1; i < n; i++) {
        if (stop && *stop) break;
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
        char full[NAME_LEN];
        if (jb_audio_ext(nm, size)) {
            snprintf(full, sizeof(full), "%s/%s", path, nm);
            if (!jb_add(pt, pn, &cap, full, off, size)) break;
            continue;
        }
        /* v422 : regarder le debut des gros fichiers au nom inconnu (au plus 700 par jeu), et les archives AFS */
        if (size < 64 * 1024 || jb_skip_ext(nm) || nsniff >= 700) continue;
        u8 h[0x100];
        nsniff++;
        if (prog) *prog = nsniff;
        if (!gd_read(&d, off, h, sizeof(h))) continue;
        if (!memcmp(h, "AFS", 4)) {                               /* archive AFS (petit-boutiste) : table de (position, taille) */
            u32 na = jb_le32(h + 4);
            if (na == 0 || na > 4000) continue;
            u8 *tb = (u8 *)malloc(na * 8);
            if (!tb || !gd_read(&d, off + 8, tb, na * 8)) { free(tb); continue; }
            for (u32 a = 0; a < na && nsniff < 2500 && !(stop && *stop); a++) {
                u32 ao = jb_le32(tb + a * 8), as = jb_le32(tb + a * 8 + 4);
                if (as < 64 * 1024 || ao >= size || as > size - ao) continue;
                u8 ah[0x100];
                nsniff++;
                if (prog) *prog = nsniff;
                if (!gd_read(&d, off + ao, ah, sizeof(ah))) continue;
                const char *k = jb_sniff(ah, as);
                if (!k) continue;
                snprintf(full, sizeof(full), "%s/%s/%04u%s", path, nm, (unsigned)a, k);
                if (!jb_add(pt, pn, &cap, full, off + ao, as)) break;
            }
            free(tb);
            continue;
        }
        const char *k = jb_sniff(h, size);
        if (!k) continue;
        snprintf(full, sizeof(full), "%s/%s%s", path, nm, k);     /* (nom d'origine + le format trouve : "bgm01.str.adx") */
        if (!jb_add(pt, pn, &cap, full, off, size)) break;
    }
    gd_close(&d);
    free(f);
    if (stop && *stop) { jb_free_tab(*pt, *pn); *pt = NULL; *pn = 0; return -1; }
    jb_cache_write(iso, id6, *pt, *pn);
    return *pn;
}
static int jb_load(const char *iso) {
    if (!strcmp(iso, jb_iso)) return jb_nt;
    jbt_t *t; int n;
    if (jb_scan(iso, &t, &n, NULL, NULL) < 0) return -1;
    jb_free_table();
    jb_t = t; jb_nt = n;
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
/* v456 : la recherche dans l'image (jusqu'a 2500 lectures sur la carte : 25 s d'ecran fige sur certains jeux)
   se fait dans un fil a part ; la fiche reste fluide et affiche la progression. */
static lwp_t jb_th = LWP_THREAD_NULL;
static u8 jb_stack[32 * 1024] __attribute__((aligned(32)));
static volatile int jb_busy = 0, jb_prog = 0, jb_stop = 0, jb_res = 0;
static int jb_pend_g = -1;
static char jb_pend_iso[NAME_LEN];
static jbt_t *jb_new_t = NULL;
static int jb_new_n = 0;
static u64 jb_note_t = 0;
static void *jb_thread(void *arg) {
    (void)arg;
    jb_res = jb_scan(jb_pend_iso, &jb_new_t, &jb_new_n, &jb_prog, &jb_stop);
    jb_busy = 0;
    return NULL;
}
static void jb_join(void) { if (jb_th != LWP_THREAD_NULL) { LWP_JoinThread(jb_th, NULL); jb_th = LWP_THREAD_NULL; } }
/* (avant un lancement, un demontage de la carte : la recherche s'arrete) */
static void jb_halt(void) {
    if (jb_busy) jb_stop = 1;
    jb_join();
    if (jb_new_t) { jb_free_tab(jb_new_t, jb_new_n); jb_new_t = NULL; jb_new_n = 0; }
    jb_pend_g = -1; jb_stop = 0;
}
static void jb_open_finish(int g);
/* (a chaque image) */
static void jb_poll(void) {
    if (jb_pend_g < 0) return;
    if (jb_busy) {
        u64 t = now_ms();
        if (t - jb_note_t >= 400) {
            jb_note_t = t;
            char m[120];
            snprintf(m, sizeof(m), "Recherche des musiques dans le jeu... (%d fichiers regard\xC3\xA9s)", jb_prog);
            gl_note(m);
        }
        return;
    }
    jb_join();
    int g = jb_pend_g;
    jb_pend_g = -1;
    if (jb_res < 0 || g >= gl_n || strcmp(gl[g].path, jb_pend_iso)) {
        jb_free_tab(jb_new_t, jb_new_n); jb_new_t = NULL; jb_new_n = 0;
        if (jb_res < 0) gl_note("Image du jeu illisible");
        return;
    }
    jb_free_table();
    jb_t = jb_new_t; jb_nt = jb_new_n; jb_new_t = NULL; jb_new_n = 0;
    snprintf(jb_iso, sizeof(jb_iso), "%s", jb_pend_iso);
    jb_open_finish(g);
}
static void jb_open_game(int g) {
    if (g < 0 || g >= gl_n) return;
    if (gl[g].dvd) { gl_note("Musiques : seulement pour les jeux de la carte SD (pas le disque)"); return; }
    if (jb_pend_g >= 0) return;                               /* (deja en recherche) */
    if (strcmp(gl[g].path, jb_iso)) {
        jb_join();
        snprintf(jb_pend_iso, sizeof(jb_pend_iso), "%s", gl[g].path);
        jb_pend_g = g; jb_prog = 0; jb_stop = 0; jb_busy = 1; jb_note_t = 0;
        if (LWP_CreateThread(&jb_th, jb_thread, NULL, jb_stack, sizeof(jb_stack), 30) < 0) { jb_th = LWP_THREAD_NULL; jb_thread(NULL); }
        jb_poll();
        return;
    }
    jb_open_finish(g);
}
static void jb_open_finish(int g) {
    int n = jb_nt;
    if (n == 0) { gl_note("Pas de musique lisible dans ce jeu : sa musique est jou\xC3\xA9" "e note par note, ou dans un format pas encore lu (lus : AST, HPS, DSP, ADP, ADX)"); return; }
    snprintf(jb_title, sizeof(jb_title), "%s", gl[g].title);
    if (!sd_ok) { gl_note("Pas de carte SD"); return; }
    int first = -1, added = 0;
    LSC_LOCK();
    for (int i = 0; i < jb_nt && nfiles < MAX_FILES; i++) {
        char p[NAME_LEN];
        if (snprintf(p, sizeof(p), "%s#%s", gl[g].path, jb_t[i].p) >= (int)sizeof(p)) continue;
        int k = bib_find(p);
        if (k < 0) {
            char *np = nm_store(p);                       /* v236 : reserve des noms */
            if (!np) continue;
            k = nfiles;
            names[k] = np;
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

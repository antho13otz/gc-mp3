// GC-MP3 : onglet FICHIERS (inclus dans main.c)
// Parcourt la carte SD : ouvrir, renommer, supprimer, copier / couper / coller,
// creer un dossier, voir la place libre, lire un fichier texte.
#define FE_MAX 1000

typedef struct { char name[128]; long size; u8 dir, mark; } fent_t;
static fent_t *fe = NULL;
static int nfe = 0, fe_sel = 0;
static char fe_cwd[NAME_LEN] = "";
static char fe_msg[160];
static u64 fe_msg_t = 0;
static long fe_free = -1;
static int fe_show_hidden = 1, fe_hide_unknown = 0;   /* v34 : reglages d'Indigo (fichiers caches, types inconnus) */
/* presse-papiers (v72 : plusieurs fichiers / dossiers) */
#define CLIP_MAX 1000
static char clip_path[NAME_LEN] = "";                 /* (le premier : affichage) */
static char (*clip_list)[NAME_LEN] = NULL;
static int clip_n = 0, clip_cut = 0;
/* copie en cours (par morceaux, sans bloquer ; v72 : une file de fichiers, dossiers compris) */
static FILE *fcp_in = NULL, *fcp_out = NULL;
static long fcp_done = 0, fcp_total = 0;
static char fcp_dst[NAME_LEN];
static char (*cpq_src)[NAME_LEN] = NULL, (*cpq_dst)[NAME_LEN] = NULL;
static int cpq_n = 0, cpq_i = 0, cpq_fail = 0;
static long cpq_bytes = 0, cpq_total = 0;
/* menu d'actions et confirmation */
static int fa_open = 0, fa_sel = 0, fa_confirm = 0;
/* lecture d'un fichier texte */
static char *tv_text = NULL;
static int tv_lines = 0, tv_top = 0;
static char tv_name[128];

/* v25 : editeur de cheats (cheats.c) */
static int ch_on = 0;
static void cheats_input(u32 down, u32 nav, int fire);
static void draw_cheats(void);
static void cheats_open(void);
static void cheats_open_file(const char *path);

/* v55 : partage par QR code (tab_phone.c) */
static void share_open(const char *path, int dir);
static int share_input(u32 down);
static void share_draw(void);

static void fe_note(const char *m) { snprintf(fe_msg, sizeof(fe_msg), "%s", m); fe_msg_t = now_ms(); }

static int fe_cmp(const void *a, const void *b) {
    const fent_t *x = (const fent_t *)a, *y = (const fent_t *)b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}

/* v34 : types que GC-MP3 ou Indigo savent ouvrir (reglage "Cacher les fichiers inconnus") */
static int fe_known(const char *n) {
    static const char *const ext[] = { ".avi", ".txt", ".lrc", ".m3u", ".pls", ".md", ".ini", ".cfg", ".dol", ".elf", ".iso", ".gcm",
                                       ".ciso", ".tgc", ".gci", ".gcs", ".sav", ".zip", ".fzn", NULL };
    if (audio_ext_len(n) > 0 || img_ext(n)) return 1;
    const char *dot = strrchr(n, '.');
    if (!dot) return 0;
    for (int i = 0; ext[i]; i++) if (!strcasecmp(dot, ext[i])) return 1;
    return 0;
}
/* v86 : au-dessus de la carte SD : les appareils (carte SD, serveurs SMB / FTP / FSP regles) */
static int fe_devs = 0;                                /* 1 = liste des appareils */
static char fe_dev[1 + NFS_N + SALON_MAX][8];
static int fe_peer[1 + NFS_N + SALON_MAX];
static int fe_gen = -1;             /* v138 : console du Salon (-1 = non) */
static int fe_net(void) { return nfs_is_net(fe_cwd); }
static int fe_at_root(void) { return !strchr(fe_cwd, '/'); }   /* "sd:", "ftp:"... */
static void fe_load_devs(void) {
    nfe = 0;
    snprintf(fe[nfe].name, sizeof(fe[nfe].name), "Carte SD");
    snprintf(fe_dev[nfe], sizeof(fe_dev[0]), "%s", SD_ROOT);
    fe[nfe].dir = 1; fe[nfe].mark = 0; fe[nfe].size = 0; fe_peer[nfe] = -1; nfe++;
    for (int p = 0; p < NFS_GCS; p++) {
        if (!nfs_cfg_ok(p)) continue;
        fe_peer[nfe] = -1;
        if (p == NFS_SMB) snprintf(fe[nfe].name, sizeof(fe[nfe].name), "%s  \\\\%s\\%s", nfs_label[p], nfs_cfg[p].host, nfs_cfg[p].share);
        else snprintf(fe[nfe].name, sizeof(fe[nfe].name), "%s  %s", nfs_label[p], nfs_cfg[p].host);
        snprintf(fe_dev[nfe], sizeof(fe_dev[0]), "%s:", nfs_scheme[p]);
        fe[nfe].dir = 1; fe[nfe].mark = 0; fe[nfe].size = 0; nfe++;
    }
    for (int k = 0; k < salon_n && nfe < 1 + NFS_N + SALON_MAX; k++) {   /* v138 : les autres GC Station (Salon) */
        snprintf(fe[nfe].name, sizeof(fe[nfe].name), "GC Station : %s  %s%s%s", salon_peers[k].name, salon_peers[k].ip,
                 salon_peers[k].now[0] ? "  \xE2\x99\xAA " : "", salon_peers[k].now);
        snprintf(fe_dev[nfe], sizeof(fe_dev[0]), "gcs:");
        fe_peer[nfe] = k;
        fe[nfe].dir = 1; fe[nfe].mark = 0; fe[nfe].size = 0; nfe++;
    }
    if (fe_sel > nfe) fe_sel = nfe;
    fe_gen = salon_gen;
}
static void fe_devs_refresh(void) {                   /* v138 : une console du Salon arrive ou part : la liste suit */
    if (!fe_devs || fe_gen == salon_gen || !fe) return;
    char dev[8] = "";                                     /* (carte SD ou serveur choisi : on reste dessus) */
    if (fe_sel > 0 && fe_sel - 1 < nfe && fe_peer[fe_sel - 1] < 0) snprintf(dev, sizeof(dev), "%s", fe_dev[fe_sel - 1]);
    fe_load_devs();
    if (dev[0]) for (int i = 0; i < nfe; i++) if (!strcmp(fe_dev[i], dev) && fe_peer[i] < 0) fe_sel = i + 1;
}
static void fe_load(void) {
    if (!fe) fe = (fent_t *)malloc(sizeof(fent_t) * FE_MAX);
    nfe = 0;
    if (!fe) return;
    fe_free = -1;                                          /* (place libre : carte SD seulement) */
    if (fe_devs) { fe_load_devs(); return; }
    if (!fe_cwd[0]) snprintf(fe_cwd, sizeof(fe_cwd), "%s", SD_ROOT);
    int net = fe_net();
    if (!net && !sd_ok) return;
    if (net && net_phase != NET_DONE) {                   /* (reseau pas encore pret : il demarre) */
        net_start_async();
        fe_note("R\xC3\xA9seau en cours de connexion... (B puis A pour r\xC3\xA9" "essayer)");
        return;
    }
    if (net) nfs_err[0] = 0;
    DIR *d = opendir(fe_cwd);
    if (!d) { if (net) fe_note(nfs_err[0] ? nfs_err : "Serveur injoignable"); return; }
    struct dirent *e;
    char path[NAME_LEN];
    while ((e = readdir(d)) != NULL && nfe < FE_MAX) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!fe_show_hidden && e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", fe_cwd, e->d_name);
        if (fe_hide_unknown && !is_dir(path, e) && !fe_known(e->d_name)) continue;
        snprintf(fe[nfe].name, sizeof(fe[nfe].name), "%s", e->d_name);
        fe[nfe].dir = (u8)is_dir(path, e);
        fe[nfe].mark = 0;
        fe[nfe].size = 0;
        if (!fe[nfe].dir) { struct stat st; if (stat(path, &st) == 0) fe[nfe].size = (long)st.st_size; }   /* (reseau : taille deja connue, cache de netfs) */
        nfe++;
        if ((nfe & 31) == 0) service_audio();
    }
    closedir(d);
    qsort(fe, nfe, sizeof(fent_t), fe_cmp);
    if (fe_sel >= nfe + 1) fe_sel = nfe;
    unsigned long long st_, sf_;                       /* v97 : FSInfo (statvfs relisait toute la FAT : carte 1 To) */
    if (!net && sd_space(&st_, &sf_)) fe_free = (long)(sf_ >> 20);
}

static void files_enter(void) { if (!fe || !nfe) fe_load(); }

static void fe_path(int i, char *out, int cap) { snprintf(out, cap, "%s/%s", fe_cwd, fe[i].name); }

/* apres une modification : les listes de musique / photos suivent */
static void fe_changed(void) {
    fe_load();
    if (strstr(fe_cwd, "/mp3") || strstr(fe_cwd, "/downloads") || strstr(fe_cwd, "/retro") || !strcmp(fe_cwd, SD_ROOT)) { music_rescan(); retro_rescan(); }
    if (strstr(fe_cwd, "/photos") || strstr(fe_cwd, "/videos") || strstr(fe_cwd, "/ecran")) npf = -1;
}

static void tv_open(const char *path, const char *name) {
    long len = 0;
    unsigned char *d = read_file(path, 512 * 1024, &len);
    if (!d) { fe_note("Fichier illisible (512 Ko maximum)"); return; }
    free(tv_text);
    tv_text = (char *)malloc(len + 1);
    if (!tv_text) { free(d); return; }
    memcpy(tv_text, d, len);
    tv_text[len] = 0;
    free(d);
    tv_lines = 1;
    for (char *p = tv_text; *p; p++) if (*p == '\n') tv_lines++;
    tv_top = 0;
    snprintf(tv_name, sizeof(tv_name), "%s", name);
}

/* v86 : un fichier du reseau : musique (tout le dossier dans la file), photo, video */
static int fe_track_of(const char *path) { for (int k = 0; k < nfiles; k++) if (!strcmp(names[k], path)) return k; return -1; }
static int fe_net_music(int i) {
    char p[NAME_LEN];
    for (int k = 0; k < nfe; k++) {
        if (fe[k].dir || !audio_ext_len(fe[k].name)) continue;
        fe_path(k, p, sizeof(p));
        if (fe_track_of(p) < 0) music_add_file(p);
    }
    fe_path(i, p, sizeof(p));
    int t = fe_track_of(p);
    if (t < 0) { fe_note("Biblioth\xC3\xA8que pleine"); return 0; }
    queue_build(t);
    start_track(t);
    fe_note(track_err ? track_err : "Lecture lanc\xC3\xA9" "e");
    return 1;
}
static void fe_net_photo(int i) {
    if (npf < 0) photo_rescan();
    if (!pf) return;
    char p[NAME_LEN];
    int want = -1;
    fe_path(i, p, sizeof(p));
    for (int k = 0; k < nfe; k++) {                           /* (les photos du dossier, pour passer de l'une a l'autre) */
        const char *dot = strrchr(fe[k].name, '.');
        int vid = dot && !strcasecmp(dot, ".avi");
        if (fe[k].dir || (!img_ext(fe[k].name) && !vid)) continue;
        char q[NAME_LEN];
        fe_path(k, q, sizeof(q));
        int f = -1;
        for (int j = 0; j < npf; j++) if (!strcmp(pf[j].path, q)) { f = j; break; }
        if (f < 0 && npf < PH_MAXF) { f = npf++; snprintf(pf[f].path, NAME_LEN, "%s", q); pf[f].video = (u8)vid; pf[f].bad = 0; pf[f].date = 0; }
        if (!strcmp(q, p)) want = f;
    }
    if (want < 0) { fe_note("Trop de photos"); return; }
    tab = TAB_PHOTO;
    pv_open(want);
}
/* v121 : jeu GameCube sur un serveur du reseau (SMB, FTP, FSP) : copie sur la carte SD (SD:/jeux-reseau),
   puis lancement (Pulse lit la carte SD pendant le jeu). Deja copie (meme taille) : lance tout de suite. */
static const char *gl_play_path(const char *path);           /* (gclib.c) */
static char fe_play_after[NAME_LEN] = "";
static void cpq_add(const char *src, const char *dst);
static int fe_is_game(const char *n) {
    const char *dot = strrchr(n, '.');
    return dot && (!strcasecmp(dot, ".iso") || !strcasecmp(dot, ".gcm") || !strcasecmp(dot, ".ciso"));
}
static void fe_net_game(int i, const char *path) {
    char dst[NAME_LEN];
    if (!sd_ok || sd_virtual) { fe_note("Il faut une carte SD dans GC Station pour jouer \xC3\xA0 un jeu du r\xC3\xA9seau"); return; }
    if (fcp_in || cpq_i < cpq_n) { fe_note("Une copie est d\xC3\xA9j\xC3\xA0 en cours"); return; }
    mkdir(SD_ROOT "/jeux-reseau", 0777);
    snprintf(dst, sizeof(dst), SD_ROOT "/jeux-reseau/%s", fe[i].name);
    struct stat st;
    if (stat(dst, &st) == 0 && (long)st.st_size == fe[i].size) {   /* (deja sur la carte) */
        const char *e = gl_play_path(dst);
        if (e) fe_note(e);
        return;
    }
    unsigned long long tot = 0, fre = 0;
    if (sd_space(&tot, &fre) && fre < (unsigned long long)fe[i].size + (16ull << 20)) { fe_note("Pas assez de place sur la carte SD pour ce jeu"); return; }
    remove(dst);
    cpq_n = cpq_i = cpq_fail = 0; cpq_bytes = cpq_total = 0;
    cpq_add(path, dst);
    if (!cpq_n) { fe_note("Copie impossible"); return; }
    snprintf(fe_play_after, sizeof(fe_play_after), "%s", dst);
    fe_note("Copie du jeu sur la carte SD, puis lancement...");
}
static void fe_open(int i) {
    char path[NAME_LEN];
    if (fe_devs) {
        if (fe_peer[i] >= 0 && fe_peer[i] < salon_n) { snprintf(nfs_cfg[NFS_GCS].host, sizeof(nfs_cfg[NFS_GCS].host), "%s", salon_peers[fe_peer[i]].ip); nfs_cfg[NFS_GCS].port = 80; }   /* v138 : (port du serveur du telephone) */
        snprintf(fe_cwd, sizeof(fe_cwd), "%s", fe_dev[i]); fe_devs = 0; fe_sel = 0; fe_load(); return;
    }
    fe_path(i, path, sizeof(path));
    if (fe[i].dir) { snprintf(fe_cwd, sizeof(fe_cwd), "%s", path); fe_sel = 0; fe_load(); return; }
    if (fe_net()) {
        const char *dot = strrchr(fe[i].name, '.');
        if (audio_ext_len(fe[i].name) > 0) { fe_net_music(i); return; }
        if (img_ext(fe[i].name) || (dot && !strcasecmp(dot, ".avi"))) { fe_net_photo(i); return; }
        if (fe_is_game(fe[i].name)) { fe_net_game(i, path); return; }   /* v121 */
    }
    if (fe_is_game(fe[i].name)) { const char *e = gl_play_path(path); if (e) fe_note(e); return; }   /* v121 : un jeu de la carte : lance */
    for (int k = 0; k < nfiles; k++)                                 /* un son de la bibliotheque */
        if (!strcmp(names[k], path)) { queue_build(k); start_track(k); fe_note("Lecture lanc\xC3\xA9" "e"); return; }
    const char *dot = strrchr(fe[i].name, '.');
    if (dot && (img_ext(fe[i].name) || !strcasecmp(dot, ".avi"))) {
        photo_rescan();
        for (int f = 0; f < npf; f++) if (!strcmp(pf[f].path, path)) { tab = TAB_PHOTO; pv_open(f); return; }
        fe_note("Range la photo dans SD:/photos pour l'ouvrir");
        return;
    }
    if (dot && !strcasecmp(dot, ".txt")) {                          /* v25 : cheats -> editeur */
        char low[NAME_LEN];
        int n = 0;
        for (; path[n] && n < NAME_LEN - 1; n++) low[n] = (char)tolower((unsigned char)path[n]);
        low[n] = 0;
        if (strstr(low, "/gc-mp3/cheats/") || strstr(low, "/swiss/cheats/")) { cheats_open_file(path); return; }
    }
    if (dot && (!strcasecmp(dot, ".txt") || !strcasecmp(dot, ".lrc") || !strcasecmp(dot, ".m3u") || !strcasecmp(dot, ".pls") ||
                !strcasecmp(dot, ".md") || !strcasecmp(dot, ".ini") || !strcasecmp(dot, ".cfg"))) { tv_open(path, fe[i].name); return; }
    fe_note("Ce type de fichier ne s'ouvre pas ici");
}

static int fe_nmark(void) { int n = 0; for (int i = 0; i < nfe; i++) n += fe[i].mark; return n; }
/* nom libre dans le dossier : "nom (2).ext" si "nom.ext" existe deja */
static void fe_free_name(const char *dir, const char *name, char *out, int cap) {
    struct stat st;
    snprintf(out, cap, "%s/%s", dir, name);
    const char *e = strrchr(name, '.');
    int bl = e && e != name ? (int)(e - name) : (int)strlen(name);
    for (int k = 2; k < 1000 && stat(out, &st) == 0; k++) snprintf(out, cap, "%s/%.*s (%d)%s", dir, bl, name, k, e && e != name ? e : "");
}
static void cpq_add(const char *src, const char *dst) {
    if (!cpq_src) { cpq_src = malloc(sizeof(*cpq_src) * CLIP_MAX); cpq_dst = malloc(sizeof(*cpq_dst) * CLIP_MAX); }
    if (!cpq_src || !cpq_dst || cpq_n >= CLIP_MAX) { cpq_fail++; return; }
    struct stat st;
    if (stat(src, &st) == 0) cpq_total += (long)st.st_size;
    snprintf(cpq_src[cpq_n], NAME_LEN, "%s", src);
    snprintf(cpq_dst[cpq_n], NAME_LEN, "%s", dst);
    cpq_n++;
}
static void cpq_add_tree(const char *src, const char *dst, int depth) {   /* un dossier : recree, et ses fichiers dans la file */
    if (depth > 8 || mkdir(dst, 0777) != 0) { cpq_fail++; return; }
    DIR *d = opendir(src);
    if (!d) { cpq_fail++; return; }
    struct dirent *e;
    char s2[NAME_LEN], d2[NAME_LEN];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(s2, sizeof(s2), "%s/%s", src, e->d_name);
        snprintf(d2, sizeof(d2), "%s/%s", dst, e->d_name);
        if (is_dir(s2, e)) cpq_add_tree(s2, d2, depth + 1); else cpq_add(s2, d2);
    }
    closedir(d);
}
static void fe_paste(void) {
    if (!clip_n) { fe_note("Rien \xC3\xA0 coller (copie ou coupe d'abord)"); return; }
    if (fcp_in || cpq_i < cpq_n) { fe_note("Une copie est d\xC3\xA9j\xC3\xA0 en cours"); return; }
    int ok = 0, fail = 0;
    char dst[NAME_LEN];
    if (!clip_cut) { cpq_n = cpq_i = cpq_fail = 0; cpq_bytes = cpq_total = 0; }
    for (int c = 0; c < clip_n; c++) {
        const char *src = clip_list[c];
        size_t sl = strlen(src);
        if (!strncmp(fe_cwd, src, sl) && (fe_cwd[sl] == '/' || fe_cwd[sl] == 0)) { fail++; continue; }   /* (un dossier dans lui-meme) */
        fe_free_name(fe_cwd, base_of(src), dst, sizeof(dst));
        struct stat st;
        if (stat(src, &st) != 0) { fail++; continue; }
        if (clip_cut) { if (rename(src, dst) == 0) ok++; else fail++; }
        else if (S_ISDIR(st.st_mode)) cpq_add_tree(src, dst, 0);
        else cpq_add(src, dst);
    }
    char m[120];
    if (clip_cut) {
        snprintf(m, sizeof(m), fail ? "D\xC3\xA9plac\xC3\xA9s : %d, impossibles : %d" : "D\xC3\xA9plac\xC3\xA9s : %d", ok, fail);
        fe_note(m);
        clip_n = 0; clip_path[0] = 0;
        fe_changed();
        return;
    }
    cpq_fail += fail;
    if (!cpq_n) { fe_note(cpq_fail ? "Copie impossible" : "Rien \xC3\xA0 copier"); fe_changed(); return; }
    snprintf(m, sizeof(m), "Copie de %d fichier%s...", cpq_n, cpq_n > 1 ? "s" : "");
    fe_note(m);
}

static void files_pump(void) {
    if (!fcp_in) {
        if (cpq_i >= cpq_n) return;
        while (cpq_i < cpq_n && !fcp_in) {                       /* fichier suivant de la file */
            fcp_in = fopen(cpq_src[cpq_i], "rb");
            fcp_out = fcp_in ? fopen(cpq_dst[cpq_i], "wb") : NULL;
            if (!fcp_in || !fcp_out) { if (fcp_in) fclose(fcp_in); fcp_in = NULL; cpq_fail++; cpq_i++; continue; }
            struct stat st;
            fcp_total = stat(cpq_src[cpq_i], &st) == 0 ? (long)st.st_size : 0;
            fcp_done = 0;
            snprintf(fcp_dst, sizeof(fcp_dst), "%s", cpq_dst[cpq_i]);
        }
        if (!fcp_in) goto fini;
    }
    {
        /* v121 : morceaux de 16 Ko tant qu'on reste sous ~10 ms par image (avant : 64 Ko d'un coup : depuis le
           reseau, chaque image attendait ~100 ms pendant toute la copie d'un jeu) */
        static unsigned char b[16384];
        u64 t0 = now_ms();
        int werr = 0, eof = 0;
        for (;;) {
            size_t n = fread(b, 1, sizeof(b), fcp_in);
            if (n == 0) { eof = 1; break; }
            if (fwrite(b, 1, n, fcp_out) != n) { werr = 1; break; }
            fcp_done += (long)n; cpq_bytes += (long)n;
            if (fcp_done >= fcp_total) { eof = 1; break; }
            if (now_ms() - t0 >= 10) break;
        }
        if (werr) {
            fclose(fcp_in); fclose(fcp_out); fcp_in = fcp_out = NULL; remove(fcp_dst); cpq_i = cpq_n; fe_note("Copie interrompue : carte pleine ?"); fe_changed(); fe_play_after[0] = 0; return;
        }
        if (!eof) return;
        if (fcp_done < fcp_total) cpq_fail++;                 /* (fichier plus court que prevu : reseau coupe) */
        fclose(fcp_in); fclose(fcp_out); fcp_in = fcp_out = NULL;
        cpq_i++;
        if (cpq_i < cpq_n) return;
    }
fini:
    {
        char m[120];
        if (cpq_fail) snprintf(m, sizeof(m), "Copie termin\xC3\xA9" "e : %d fichier%s, %d impossible%s", cpq_n - cpq_fail, cpq_n - cpq_fail > 1 ? "s" : "", cpq_fail, cpq_fail > 1 ? "s" : "");
        else snprintf(m, sizeof(m), "Copie termin\xC3\xA9" "e : %d fichier%s", cpq_n, cpq_n > 1 ? "s" : "");
        fe_note(m);
        cpq_n = cpq_i = 0;
        fe_changed();
        if (fe_play_after[0]) {                               /* v121 : jeu du reseau copie : on le lance */
            char p[NAME_LEN];
            snprintf(p, sizeof(p), "%s", fe_play_after);
            fe_play_after[0] = 0;
            if (!cpq_fail) { const char *e = gl_play_path(p); if (e) fe_note(e); }
        }
    }
}

enum { FA_SHARE, FA_RENAME, FA_DELETE, FA_COPY, FA_CUT, FA_PASTE, FA_MKDIR, FA_ALL, FA_CHEATS, FA_COUNT };
static const char *const fa_names[FA_COUNT] = { "Partager (QR code)", "Renommer", "Supprimer", "Copier", "Couper", "Coller ici", "Nouveau dossier", "Tout s\xC3\xA9lectionner / rien", "Cheats (\xC3\xA9" "diteur)" };

static void fe_action(int a) {
    char path[NAME_LEN];
    int i = fe_sel - 1;                                    /* (ligne 0 : dossier parent) */
    int has = i >= 0 && i < nfe;
    if (has) fe_path(i, path, sizeof(path));
    switch (a) {
    case FA_SHARE:                                       /* v55 : fichier, dossier, ou (ligne du haut) le dossier ouvert */
        if (has) share_open(path, fe[i].dir); else share_open(fe_cwd, 1);
        break;
    case FA_RENAME:
        if (fe_nmark() > 1) { kb_start(KB_RENAME, "", -1, -1); break; }        /* v72 : plusieurs : "Nom 01", "Nom 02"... */
        if (has) kb_start(KB_RENAME, fe[i].name, -1, -1);
        break;
    case FA_DELETE: {
        int nm = fe_nmark();
        if (!has && !nm) break;
        if (!fa_confirm) { fa_confirm = 1; return; }
        fa_confirm = 0;
        if (nm) {                                            /* v72 : la selection */
            int ok = 0, fail = 0;
            char p[NAME_LEN], m[100];
            for (int k = 0; k < nfe; k++) if (fe[k].mark) { fe_path(k, p, sizeof(p)); if (fe[k].dir ? rmdir(p) == 0 : remove(p) == 0) ok++; else fail++; }
            snprintf(m, sizeof(m), fail ? "Supprim\xC3\xA9s : %d (%d impossibles : dossiers non vides ?)" : "Supprim\xC3\xA9s : %d", ok, fail);
            fe_note(m); fe_changed();
            break;
        }
        if (fe[i].dir ? rmdir(path) == 0 : remove(path) == 0) { fe_note("Supprim\xC3\xA9"); fe_changed(); }
        else fe_note(fe[i].dir ? "Le dossier doit \xC3\xAAtre vide" : "Suppression impossible");
        break; }
    case FA_COPY: case FA_CUT: {
        if (!clip_list) clip_list = malloc(sizeof(*clip_list) * CLIP_MAX);
        if (!clip_list) break;
        clip_n = 0;
        if (fe_nmark()) { for (int k = 0; k < nfe && clip_n < CLIP_MAX; k++) if (fe[k].mark) fe_path(k, clip_list[clip_n++], NAME_LEN); }
        else if (has) snprintf(clip_list[clip_n++], NAME_LEN, "%s", path);
        if (!clip_n) break;
        snprintf(clip_path, sizeof(clip_path), "%s", clip_list[0]);
        clip_cut = (a == FA_CUT);
        char m[140];
        snprintf(m, sizeof(m), "%s %d \xC3\xA9l\xC3\xA9ment%s : va dans un dossier puis X > Coller ici", a == FA_CUT ? "Coup\xC3\xA9" : "Copi\xC3\xA9", clip_n, clip_n > 1 ? "s" : "");
        fe_note(m);
        for (int k = 0; k < nfe; k++) fe[k].mark = 0;
        break; }
    case FA_ALL: { int all = fe_nmark() < nfe; for (int k = 0; k < nfe; k++) fe[k].mark = (u8)all; break; }
    case FA_PASTE: fe_paste(); break;
    case FA_MKDIR: kb_start(KB_MKDIR, "", -1, -1); break;
    case FA_CHEATS: fa_open = 0; cheats_open(); return;
    }
    fa_open = 0;
}

/* nom tape au clavier (renommer / nouveau dossier) */
static void files_kb_done(int mode, const char *t) {
    char clean[128], path[NAME_LEN], dst[NAME_LEN];
    snprintf(clean, sizeof(clean), "%s", t);
    sanitize_name(clean);
    if (!clean[0]) return;
    snprintf(dst, sizeof(dst), "%s/%s", fe_cwd, clean);
    if (mode == KB_MKDIR) { if (mkdir(dst, 0777) == 0) fe_note("Dossier cr\xC3\xA9\xC3\xA9"); else fe_note("Cr\xC3\xA9" "ation impossible"); fe_changed(); return; }
    if (fe_nmark() > 1) {                                    /* v72 : renommage de la selection : "Nom 01.ext", "Nom 02.ext"... */
        int ok = 0, fail = 0, no = 0, tot = fe_nmark();
        char m[100];
        for (int k = 0; k < nfe; k++) {
            if (!fe[k].mark) continue;
            no++;
            const char *ext = fe[k].dir ? NULL : strrchr(fe[k].name, '.');
            char nn[200];
            snprintf(nn, sizeof(nn), tot >= 100 ? "%s %03d%s" : "%s %02d%s", clean, no, ext ? ext : "");
            fe_path(k, path, sizeof(path));
            fe_free_name(fe_cwd, nn, dst, sizeof(dst));
            if (rename(path, dst) == 0) ok++; else fail++;
        }
        snprintf(m, sizeof(m), fail ? "Renomm\xC3\xA9s : %d, impossibles : %d" : "Renomm\xC3\xA9s : %d", ok, fail);
        fe_note(m); fe_changed();
        return;
    }
    int i = fe_sel - 1;
    if (i < 0 || i >= nfe) return;
    fe_path(i, path, sizeof(path));
    struct stat st;
    if (stat(dst, &st) == 0) { fe_note("Ce nom existe d\xC3\xA9j\xC3\xA0"); return; }
    if (rename(path, dst) == 0) { fe_note("Renomm\xC3\xA9"); fe_changed(); } else fe_note("Impossible de renommer");
}

static void fe_up(void) {                                /* v86 : dossier parent ; au-dessus de la racine : les appareils */
    if (fe_devs) return;
    char *sl = strrchr(fe_cwd, '/');
    fe_sel = 0;
    if (sl) { *sl = 0; fe_load(); return; }
    fe_devs = 1;
    fe_load();
    for (int i = 0; i < nfe; i++) if (!strcmp(fe_dev[i], fe_cwd)) fe_sel = i + 1;   /* (sur l'appareil qu'on quitte) */
}
static void files_input(u32 down, u32 nav, int fire) {
    if (ch_on) { cheats_input(down, nav, fire); return; }
    if (share_input(down)) return;                           /* v55 : fenetre du QR code */
    if (!fe) fe_load();
    if (tv_text) {                                           /* lecture d'un texte */
        if (fire && (nav & PAD_BUTTON_DOWN)) tv_top++;
        if (fire && (nav & PAD_BUTTON_UP)) tv_top--;
        if (fire && (nav & PAD_BUTTON_RIGHT)) tv_top += 14;
        if (fire && (nav & PAD_BUTTON_LEFT)) tv_top -= 14;
        if (tv_top > tv_lines - 14) tv_top = tv_lines - 14;
        if (tv_top < 0) tv_top = 0;
        if (down & (PAD_BUTTON_B | PAD_BUTTON_A)) { free(tv_text); tv_text = NULL; }
        return;
    }
    if (fa_open) {
        if (fire && (nav & PAD_BUTTON_DOWN)) { fa_sel = (fa_sel + 1) % FA_COUNT; fa_confirm = 0; }
        if (fire && (nav & PAD_BUTTON_UP)) { fa_sel = (fa_sel + FA_COUNT - 1) % FA_COUNT; fa_confirm = 0; }
        if (down & PAD_BUTTON_A) fe_action(fa_sel);
        if (down & (PAD_BUTTON_B | PAD_BUTTON_X)) { fa_open = 0; fa_confirm = 0; }
        return;
    }
    fe_devs_refresh();
    int rows = nfe + 1;
    if (fire) {
        if (nav & PAD_BUTTON_DOWN) fe_sel++;
        if (nav & PAD_BUTTON_UP) fe_sel--;
        if (nav & PAD_BUTTON_RIGHT) fe_sel += 13;
        if (nav & PAD_BUTTON_LEFT) fe_sel -= 13;
        if (fe_sel < 0) fe_sel = 0;
        if (fe_sel > rows - 1) fe_sel = rows - 1;
    }
    if (down & PAD_BUTTON_A) {
        if (fe_sel == 0) fe_up();
        else fe_open(fe_sel - 1);
    }
    if (down & PAD_BUTTON_B) fe_up();
    if ((down & PAD_BUTTON_X) && fe_devs) down &= ~PAD_BUTTON_X;   /* (pas d'actions sur la liste des appareils) */
    if (down & PAD_BUTTON_X) { fa_open = 1; fa_sel = fe_sel == 0 ? FA_PASTE : 0; fa_confirm = 0; }
    if ((down & PAD_BUTTON_Y) && fe_devs && fe_sel > 0 && fe_sel - 1 < nfe && fe_peer[fe_sel - 1] >= 0) {   /* v138 : Y = ecouter avec cette console */
        const char *e = salon_listen(fe_peer[fe_sel - 1]);
        fe_note(e ? e : "\xC3\x89" "coute avec l'autre console");
        down &= ~PAD_BUTTON_Y;
    }
    if ((down & PAD_BUTTON_Y) && fe_sel > 0 && fe_sel - 1 < nfe) {   /* v72 : Y = cocher (plusieurs fichiers d'un coup) */
        fe[fe_sel - 1].mark = !fe[fe_sel - 1].mark;
        if (fe_sel < rows - 1) fe_sel++;
    }
}

static void draw_tab_files(void) {
    if (ch_on) { draw_cheats(); return; }
    char buf[NAME_LEN], d[NAME_LEN];
    draw_panel(11, 73, 618, 338);
    int tx = draw_tabs(NULL);
    if (fe_devs) snprintf(d, sizeof(d), "%s", "Appareils");
    else if (fe_net() && fe_at_root()) { int p = -1; nfs_rel(fe_cwd, &p); snprintf(d, sizeof(d), "%s  (%s)", p >= 0 ? nfs_label[p] : fe_cwd, p >= 0 ? nfs_cfg[p].host : ""); }
    else text_display(fe_cwd, d, sizeof(d));
    fit_text(F_SMALL, d, 600 - tx - (fe_free >= 0 ? 110 : 0), buf, sizeof(buf));
    gx_text1(F_SMALL, tx + 6, 99, buf, RGBA(200, 190, 240, 255));
    if (fe_free >= 0) { snprintf(buf, sizeof(buf), "Libre : %ld Mo", fe_free); gx_text_r(F_SMALL, 614, 99, buf, RGBA(150, 135, 210, 255)); }
    if (tv_text) {                                           /* texte */
        gx_text1(F_MED, 30, 132, tv_name, RGBA(255, 255, 255, 255));
        const char *p = tv_text;
        for (int l = 0; l < tv_top && *p; l++) { p = strchr(p, '\n'); if (!p) break; p++; }
        for (int l = 0; l < 14 && p && *p; l++) {
            const char *e = strchr(p, '\n');
            int n = e ? (int)(e - p) : (int)strlen(p);
            if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
            memcpy(buf, p, n); buf[n] = 0;
            if (n && buf[n - 1] == '\r') buf[n - 1] = 0;
            text_display(buf, d, sizeof(d));
            fit_text(F_SMALL, d, 580, buf, sizeof(buf));
            gx_text1(F_SMALL, 30, 156 + l * 17, buf, RGBA(215, 205, 245, 255));
            p = e ? e + 1 : NULL;
        }
        static const char *const k[] = { "B" };
        static const char *const l[] = { "FERMER" };
        draw_legend_keys(k, l, 1);
        return;
    }
    fe_devs_refresh();
    int rows = nfe + 1, vis = 12;
    int start = fe_sel - vis / 2;
    if (start > rows - vis) start = rows - vis;
    if (start < 0) start = 0;
    for (int i = 0; i < vis; i++) {
        int r = start + i;
        if (r >= rows) break;
        f32 ry = 114.0f + i * 24.0f, cy = ry + 12.0f;
        int on = (r == fe_sel);
        gp_zone(18, ry + 1, 604, 22, &fe_sel, r, GPZ_SEL);
        if (on) gx_rrect_fill(20, ry + 1, 600, 22, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        col_t txt = on ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
        col_t dim = on ? RGBA(235, 225, 255, 255) : RGBA(140, 125, 200, 255);
        if (r == 0) { gx_text1(F_MED, 34, cy + 5, fe_devs ? "Carte SD et serveurs du r\xC3\xA9seau" : !fe_at_root() ? ".. (dossier parent)" : ".. (appareils : carte SD, SMB, FTP, FSP)", dim); continue; }
        fent_t *e = &fe[r - 1];
        if (e->mark) {                                           /* v72 : coche */
            gx_rrect_fill(28, cy - 8, 16, 16, 4, RGBA(255, 210, 110, 255), RGBA(230, 160, 60, 255));
            col_t ck = RGBA(40, 20, 80, 255);
            gx_poly4(31, cy, 33, cy - 2, 37, cy + 2, 35, cy + 4, ck, ck, ck, ck);           /* (coche) */
            gx_poly4(35, cy + 4, 33, cy + 2, 40, cy - 5, 42, cy - 3, ck, ck, ck, ck);
        }
        else if (e->dir) draw_folder_icon(36, cy, on ? txt : RGBA(150, 120, 235, 255));
        else draw_note(36, cy, on ? txt : RGBA(120, 100, 190, 255));
        text_display(e->name, d, sizeof(d));
        fit_text_sel(on, F_MED, d, 470, buf, sizeof(buf));
        gx_text1(F_MED, 52, cy + 5, buf, e->mark ? RGBA(255, 225, 150, 255) : txt);
        if (!e->dir) { fmt_size(buf, sizeof(buf), e->size); gx_text_r(F_SMALL, 612, cy + 4, buf, dim); }
        if (clip_n == 1 && clip_path[0] && !strcmp(base_of(clip_path), e->name)) gx_text_r(F_SMALL, 540, cy + 4, clip_cut ? "coup\xC3\xA9" : "copi\xC3\xA9", RGBA(255, 210, 120, 255));
    }
    if (nfe == 0) gx_text1(F_SMALL, 34, 166, fe_net() && nfs_err[0] ? nfs_err : "Dossier vide", RGBA(170, 155, 225, 255));
    if (fe_devs && nfe == 1) gx_text1(F_SMALL, 34, 190, "Serveurs SMB, FTP, FSP : START > R\xC3\xA9glages > R\xC3\xA9seau", RGBA(170, 155, 225, 255));
    if ((fcp_in || cpq_i < cpq_n) && cpq_total > 0) {
        snprintf(buf, sizeof(buf), "Copie : fichier %d / %d  -  %ld %%", cpq_i + 1, cpq_n, (long)((long long)cpq_bytes * 100 / cpq_total));
        gx_text1(F_SMALL, 30, 404, buf, RGBA(255, 210, 120, 255));
    } else if (now_ms() - fe_msg_t < 3500) gx_text1(F_SMALL, 30, 404, fe_msg, RGBA(255, 210, 120, 255));
    else if (fe_nmark()) { snprintf(buf, sizeof(buf), "%d s\xC3\xA9lectionn\xC3\xA9%s  -  X : copier, couper, renommer, supprimer", fe_nmark(), fe_nmark() > 1 ? "s" : ""); gx_text1(F_SMALL, 30, 404, buf, RGBA(255, 210, 120, 255)); }
    if (fa_open) {                                           /* menu des actions */
        const f32 w = 260, h = 46 + FA_COUNT * 24, x = 360, y = 130;
        gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
        draw_panel(x, y, w, h);
        for (int a = 0; a < FA_COUNT; a++) {
            f32 ry = y + 14 + a * 24;
            if (a == fa_sel) gx_rrect_fill(x + 10, ry, w - 20, 22, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
            const char *lab = (a == FA_DELETE && fa_confirm && a == fa_sel) ? "Confirmer : A pour supprimer" : fa_names[a];
            gx_text1(F_MED, x + 22, ry + 17, lab, (a == FA_DELETE && fa_confirm) ? RGBA(255, 160, 160, 255) : RGBA(240, 232, 255, 255));
        }
        gx_text_c(F_SMALL, x + w / 2, y + h - 8, "A : valider   B : fermer", RGBA(255, 210, 120, 255));
    }
    static const char *const k[] = { "A", "B", "X", "Y" };
    static const char *const l[] = { "OUVRIR", "RETOUR", "ACTIONS", "COCHER" };
    draw_legend_keys(k, l, 4);
    share_draw();                                            /* v55 */
}

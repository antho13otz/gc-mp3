// GC-MP3 : onglet FICHIERS (inclus dans main.c)
// Parcourt la carte SD : ouvrir, renommer, supprimer, copier / couper / coller,
// creer un dossier, voir la place libre, lire un fichier texte.
#define FE_MAX 1000

typedef struct { char name[128]; long size; u8 dir; } fent_t;
static fent_t *fe = NULL;
static int nfe = 0, fe_sel = 0;
static char fe_cwd[NAME_LEN] = "";
static char fe_msg[160];
static u64 fe_msg_t = 0;
static long fe_free = -1;
static int fe_show_hidden = 1, fe_hide_unknown = 0;   /* v34 : reglages d'Indigo (fichiers caches, types inconnus) */
/* presse-papiers */
static char clip_path[NAME_LEN] = "";
static int clip_cut = 0;
/* copie en cours (par morceaux, sans bloquer) */
static FILE *fcp_in = NULL, *fcp_out = NULL;
static long fcp_done = 0, fcp_total = 0;
static char fcp_dst[NAME_LEN];
/* menu d'actions et confirmation */
static int fa_open = 0, fa_sel = 0, fa_confirm = 0;
/* lecture d'un fichier texte */
static char *tv_text = NULL;
static int tv_lines = 0, tv_top = 0;
static char tv_name[128];

/* v25 : editeur de cheats Swiss (cheats.c) */
static int ch_on = 0;
static void cheats_input(u32 down, u32 nav, int fire);
static void draw_cheats(void);
static void cheats_open(void);
static void cheats_open_file(const char *path);

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
static void fe_load(void) {
    if (!fe) fe = (fent_t *)malloc(sizeof(fent_t) * FE_MAX);
    nfe = 0;
    if (!fe || !sd_ok) return;
    if (!fe_cwd[0]) snprintf(fe_cwd, sizeof(fe_cwd), "%s", SD_ROOT);
    DIR *d = opendir(fe_cwd);
    if (!d) return;
    struct dirent *e;
    char path[NAME_LEN];
    while ((e = readdir(d)) != NULL && nfe < FE_MAX) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!fe_show_hidden && e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", fe_cwd, e->d_name);
        if (fe_hide_unknown && !is_dir(path, e) && !fe_known(e->d_name)) continue;
        snprintf(fe[nfe].name, sizeof(fe[nfe].name), "%s", e->d_name);
        fe[nfe].dir = (u8)is_dir(path, e);
        fe[nfe].size = 0;
        if (!fe[nfe].dir) { struct stat st; if (stat(path, &st) == 0) fe[nfe].size = (long)st.st_size; }
        nfe++;
        if ((nfe & 31) == 0) service_audio();
    }
    closedir(d);
    qsort(fe, nfe, sizeof(fent_t), fe_cmp);
    if (fe_sel >= nfe + 1) fe_sel = nfe;
    struct statvfs sv;
    if (statvfs(SD_ROOT "/", &sv) == 0) fe_free = (long)((unsigned long long)sv.f_bavail * sv.f_frsize / (1024 * 1024));
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

static void fe_open(int i) {
    char path[NAME_LEN];
    fe_path(i, path, sizeof(path));
    if (fe[i].dir) { snprintf(fe_cwd, sizeof(fe_cwd), "%s", path); fe_sel = 0; fe_load(); return; }
    for (int k = 0; k < nfiles; k++)                                 /* un son de la bibliotheque */
        if (!strcmp(names[k], path)) { queue_build(k); start_track(k); fe_note("Lecture lanc\xC3\xA9" "e"); return; }
    const char *dot = strrchr(fe[i].name, '.');
    if (dot && (img_ext(fe[i].name) || !strcasecmp(dot, ".avi"))) {
        photo_rescan();
        for (int f = 0; f < npf; f++) if (!strcmp(pf[f].path, path)) { tab = TAB_PHOTO; pv_open(f); return; }
        fe_note("Range la photo dans SD:/photos pour l'ouvrir");
        return;
    }
    if (dot && !strcasecmp(dot, ".txt")) {                          /* v25 : cheats Swiss -> editeur */
        char low[NAME_LEN];
        int n = 0;
        for (; path[n] && n < NAME_LEN - 1; n++) low[n] = (char)tolower((unsigned char)path[n]);
        low[n] = 0;
        if (strstr(low, "/swiss/cheats/")) { cheats_open_file(path); return; }
    }
    if (dot && (!strcasecmp(dot, ".txt") || !strcasecmp(dot, ".lrc") || !strcasecmp(dot, ".m3u") || !strcasecmp(dot, ".pls") ||
                !strcasecmp(dot, ".md") || !strcasecmp(dot, ".ini") || !strcasecmp(dot, ".cfg"))) { tv_open(path, fe[i].name); return; }
    fe_note("Ce type de fichier ne s'ouvre pas ici");
}

static void fe_paste(void) {
    if (!clip_path[0]) { fe_note("Rien \xC3\xA0 coller (copie ou coupe d'abord)"); return; }
    char dst[NAME_LEN];
    snprintf(dst, sizeof(dst), "%s/%s", fe_cwd, base_of(clip_path));
    struct stat st;
    if (stat(dst, &st) == 0) { fe_note("Un fichier porte d\xC3\xA9j\xC3\xA0 ce nom ici"); return; }
    if (clip_cut) {
        if (rename(clip_path, dst) == 0) { fe_note("D\xC3\xA9plac\xC3\xA9"); clip_path[0] = 0; fe_changed(); }
        else fe_note("D\xC3\xA9placement impossible");
        return;
    }
    if (stat(clip_path, &st) != 0 || S_ISDIR(st.st_mode)) { fe_note("Seuls les fichiers se copient (pas les dossiers)"); return; }
    if (fcp_in) { fe_note("Une copie est d\xC3\xA9j\xC3\xA0 en cours"); return; }
    fcp_in = fopen(clip_path, "rb");
    fcp_out = fcp_in ? fopen(dst, "wb") : NULL;
    if (!fcp_in || !fcp_out) { if (fcp_in) fclose(fcp_in); fcp_in = NULL; fe_note("Copie impossible"); return; }
    fcp_total = (long)st.st_size;
    fcp_done = 0;
    snprintf(fcp_dst, sizeof(fcp_dst), "%s", dst);
}

static void files_pump(void) {
    if (!fcp_in) return;
    static unsigned char b[65536];
    size_t n = fread(b, 1, sizeof(b), fcp_in);
    if (n > 0 && fwrite(b, 1, n, fcp_out) != n) {
        fclose(fcp_in); fclose(fcp_out); fcp_in = fcp_out = NULL; remove(fcp_dst); fe_note("Copie interrompue : carte pleine ?"); return;
    }
    fcp_done += (long)n;
    if (n == 0 || fcp_done >= fcp_total) {
        fclose(fcp_in); fclose(fcp_out); fcp_in = fcp_out = NULL;
        fe_note("Copie termin\xC3\xA9" "e");
        fe_changed();
    }
}

enum { FA_RENAME, FA_DELETE, FA_COPY, FA_CUT, FA_PASTE, FA_MKDIR, FA_CHEATS, FA_COUNT };
static const char *const fa_names[FA_COUNT] = { "Renommer", "Supprimer", "Copier", "Couper", "Coller ici", "Nouveau dossier", "Cheats Swiss (\xC3\xA9" "diteur)" };

static void fe_action(int a) {
    char path[NAME_LEN];
    int i = fe_sel - 1;                                    /* (ligne 0 : dossier parent) */
    int has = i >= 0 && i < nfe;
    if (has) fe_path(i, path, sizeof(path));
    switch (a) {
    case FA_RENAME: if (has) kb_start(KB_RENAME, fe[i].name, -1, -1); break;
    case FA_DELETE:
        if (!has) break;
        if (!fa_confirm) { fa_confirm = 1; return; }
        fa_confirm = 0;
        if (fe[i].dir ? rmdir(path) == 0 : remove(path) == 0) { fe_note("Supprim\xC3\xA9"); fe_changed(); }
        else fe_note(fe[i].dir ? "Le dossier doit \xC3\xAAtre vide" : "Suppression impossible");
        break;
    case FA_COPY: case FA_CUT:
        if (!has) break;
        snprintf(clip_path, sizeof(clip_path), "%s", path);
        clip_cut = (a == FA_CUT);
        fe_note(a == FA_CUT ? "Coup\xC3\xA9 : va dans un dossier puis X > Coller ici" : "Copi\xC3\xA9 : va dans un dossier puis X > Coller ici");
        break;
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
    int i = fe_sel - 1;
    if (i < 0 || i >= nfe) return;
    fe_path(i, path, sizeof(path));
    struct stat st;
    if (stat(dst, &st) == 0) { fe_note("Ce nom existe d\xC3\xA9j\xC3\xA0"); return; }
    if (rename(path, dst) == 0) { fe_note("Renomm\xC3\xA9"); fe_changed(); } else fe_note("Impossible de renommer");
}

static void files_input(u32 down, u32 nav, int fire) {
    if (ch_on) { cheats_input(down, nav, fire); return; }
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
        if (fe_sel == 0) { char *sl = strrchr(fe_cwd, '/'); if (sl && strcmp(fe_cwd, SD_ROOT)) { *sl = 0; fe_sel = 0; fe_load(); } }
        else fe_open(fe_sel - 1);
    }
    if (down & PAD_BUTTON_B) { char *sl = strrchr(fe_cwd, '/'); if (sl && strcmp(fe_cwd, SD_ROOT)) { *sl = 0; fe_sel = 0; fe_load(); } }
    if (down & PAD_BUTTON_X) { fa_open = 1; fa_sel = fe_sel == 0 ? FA_PASTE : 0; fa_confirm = 0; }
    if ((down & PAD_BUTTON_Y) && src == SRC_FILE && playing) paused = !paused;
}

static void draw_tab_files(void) {
    if (ch_on) { draw_cheats(); return; }
    char buf[NAME_LEN], d[NAME_LEN];
    draw_panel(11, 73, 618, 338);
    int tx = draw_tabs(NULL);
    text_display(fe_cwd, d, sizeof(d));
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
        if (r == 0) { gx_text1(F_MED, 34, cy + 5, strcmp(fe_cwd, SD_ROOT) ? ".. (dossier parent)" : "(racine de la carte SD)", dim); continue; }
        fent_t *e = &fe[r - 1];
        if (e->dir) draw_folder_icon(36, cy, on ? txt : RGBA(150, 120, 235, 255));
        else draw_note(36, cy, on ? txt : RGBA(120, 100, 190, 255));
        text_display(e->name, d, sizeof(d));
        fit_text(F_MED, d, 470, buf, sizeof(buf));
        gx_text1(F_MED, 52, cy + 5, buf, txt);
        if (!e->dir) { fmt_size(buf, sizeof(buf), e->size); gx_text_r(F_SMALL, 612, cy + 4, buf, dim); }
        if (clip_path[0] && !strcmp(base_of(clip_path), e->name)) gx_text_r(F_SMALL, 540, cy + 4, clip_cut ? "coup\xC3\xA9" : "copi\xC3\xA9", RGBA(255, 210, 120, 255));
    }
    if (nfe == 0) gx_text1(F_SMALL, 34, 166, "Dossier vide", RGBA(170, 155, 225, 255));
    if (fcp_in && fcp_total > 0) {
        snprintf(buf, sizeof(buf), "Copie : %ld %%", fcp_done * 100 / fcp_total);
        gx_text1(F_SMALL, 30, 404, buf, RGBA(255, 210, 120, 255));
    } else if (now_ms() - fe_msg_t < 3500) gx_text1(F_SMALL, 30, 404, fe_msg, RGBA(255, 210, 120, 255));
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
    static const char *const k[] = { "A", "B", "X" };
    static const char *const l[] = { "OUVRIR", "RETOUR", "ACTIONS" };
    draw_legend_keys(k, l, 3);
}

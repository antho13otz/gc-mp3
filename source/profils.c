/* GC Station v191 : profils de joueurs et limite de temps de jeu (inclus dans main.c, apres tools.c).
   - Un profil = un dossier gc-mp3/profils/<nom> (jeux lances, temps de jeu, favoris, succes, records des mini-jeux :
     gp_path) + profil.txt (carte memoire virtuelle, limite par jour). Le profil principal garde gc-mp3/mes-parties,
     la carte et la limite de gc-mp3.txt.
   - Limite de temps de jeu par jour (par profil) : le temps mesure par Pulse est compte par jour (jour.txt) ; au-dela,
     les jeux ne se lancent plus jusqu'au lendemain. Le temps restant est affiche avant de lancer un jeu. */
#define PJ_MAX 12
static char pj_list[PJ_MAX][32];
static int pj_n = 0, pj_sel = 0, pj_ask = 1, pj_del = 0;
/* pj_main_card (declare dans extras73.c) : carte du profil principal, gardee dans gc-mp3.txt */
static int pj_limit = 0, pj_main_limit = 0;             /* minutes par jour (0 = pas de limite) */
static const int pj_limits[] = { 0, 30, 60, 90, 120, 180 };

static int pj_name_ok(const char *n) {
    if (!n[0] || strlen(n) > 20 || n[0] == '.' || n[0] == ' ') return 0;
    for (const unsigned char *p = (const unsigned char *)n; *p; p++) if (*p < 32 || strchr("/\\:*?\"<>|", *p)) return 0;
    return 1;
}
static void pj_scan(void) {
    pj_n = 0;
    DIR *d = sd_ok ? opendir(GCD_PROF) : NULL;
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL && pj_n < PJ_MAX) {
        if (e->d_name[0] == '.' || !pj_name_ok(e->d_name)) continue;
        char p[NAME_LEN];
        snprintf(p, sizeof(p), GCD_PROF "/%s", e->d_name);
        if (!is_dir(p, e)) continue;
        snprintf(pj_list[pj_n++], sizeof(pj_list[0]), "%s", e->d_name);
    }
    if (d) closedir(d);
    qsort(pj_list, pj_n, sizeof(pj_list[0]), (int (*)(const void *, const void *))strcasecmp);
}
/* profil.txt : "carte = <fichier>" et "limite = <minutes>" */
static void pj_load_file(const char *name, char *card, int cap, int *limit) {
    char p[NAME_LEN];
    snprintf(card, cap, "carte-%s.raw", name);
    for (char *c = card; *c; c++) if (*c == ' ') *c = '_';
    *limit = 0;
    snprintf(p, sizeof(p), GCD_PROF "/%s/profil.txt", name);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (!strcasecmp(k, "carte") && v[0] && strlen(v) < (size_t)cap && !strchr(v, '/') && !strstr(v, "..")) snprintf(card, cap, "%s", v);
        else if (!strcasecmp(k, "limite")) { int m = atoi(v); *limit = m > 0 && m <= 24 * 60 ? m : 0; }
    }
    fclose(f);
}
static void pj_save_file(void) {
    if (!gp_name[0] || !sd_ok) return;
    static char last[NAME_LEN + 160];                       /* v194 : reecrit seulement s'il change */
    char t[sizeof(last)];
    snprintf(t, sizeof(t), "# GC Station : profil %s\ncarte = %s\nlimite = %d\n", gp_name, vc_keep[0] ? vc_keep : vc_active, pj_limit);
    if (!strcmp(t, last)) return;
    char p[NAME_LEN];
    mkdir(GCD_PROF, 0777);
    snprintf(p, sizeof(p), GCD_PROF "/%s", gp_name); mkdir(p, 0777);
    snprintf(p, sizeof(p), GCD_PROF "/%s/profil.txt", gp_name);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fputs(t, f);
    if (fclose(f) == 0) snprintf(last, sizeof(last), "%s", t);
}
/* reglages (gc-mp3.txt) : appele par load_settings pour les cles inconnues */
static int pj_settings_key(const char *k, const char *v) {
    if (!strcasecmp(k, "profil")) { snprintf(gp_name, sizeof(gp_name), "%s", pj_name_ok(v) ? v : ""); return 1; }
    if (!strcasecmp(k, "profil_demander")) { pj_ask = !has_word(v, "non"); return 1; }
    if (!strcasecmp(k, "limite_jeu")) { int m = atoi(v); pj_main_limit = m > 0 && m <= 24 * 60 ? m : 0; return 1; }
    return 0;
}
static void pj_settings_save(FILE *f) {
    fprintf(f, "# Profil de joueur actuel (vide : principal), demande au demarrage, limite de jeu par jour du principal (minutes, 0 = non)\n"
               "profil = %s\nprofil_demander = %s\nlimite_jeu = %d\n", gp_name, pj_ask ? "oui" : "non", gp_name[0] ? pj_main_limit : pj_limit);
}
/* apres la lecture des reglages : la carte et la limite du profil */
static void pj_apply(void) {
    snprintf(pj_main_card, sizeof(pj_main_card), "%s", vc_active);
    pj_limit = pj_main_limit;
    if (!gp_name[0]) return;
    char p[NAME_LEN]; struct stat st;
    snprintf(p, sizeof(p), GCD_PROF "/%s", gp_name);
    if (stat(p, &st) != 0) { gp_name[0] = 0; return; }   /* (profil efface : retour au principal) */
    pj_load_file(gp_name, vc_active, sizeof(vc_active), &pj_limit);
}
static void sc_reload(void);
static void gm_recent_load(void);
static void pj_switch(const char *name) {               /* "" = principal */
    if (gp_name[0]) pj_save_file(); else { snprintf(pj_main_card, sizeof(pj_main_card), "%s", vc_active); pj_main_limit = pj_limit; }
    snprintf(gp_name, sizeof(gp_name), "%s", name);
    if (gp_name[0]) {
        char p[NAME_LEN];
        mkdir(GCD_PROF, 0777);
        snprintf(p, sizeof(p), GCD_PROF "/%s", gp_name); mkdir(p, 0777);
        pj_load_file(gp_name, vc_active, sizeof(vc_active), &pj_limit);
        pj_save_file();
    } else { snprintf(vc_active, sizeof(vc_active), "%s", pj_main_card[0] ? pj_main_card : "carte-A.raw"); pj_limit = pj_main_limit; }
    if (gl && gl_scanned) { for (int k = 0; k < gl_n; k++) { gl[k].runs = 0; gl[k].last = 0; gl[k].play = 0; gl[k].fav = 0; } gl_stats_load(); gl_fav_load(); gl_apply_sort(); }
    sc_reload();
    gm_recent_load();
    save_settings();
    crumb("profil : %s", gp_name[0] ? gp_name : "principal");
}

/* ---------- temps de jeu du jour ---------- */
static u32 pj_today(void) { time_t t = time(NULL); struct tm *tm = localtime(&t); return tm ? (u32)((tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday) : 0; }
static u32 pj_day_sec(void) {                           /* secondes jouees aujourd'hui (profil actuel) */
    long n = 0;
    char *d = (char *)read_file(gp_path("jour.txt"), 256, &n);
    unsigned day = 0, sec = 0;
    if (d) { char b[64]; snprintf(b, sizeof(b), "%.*s", (int)(n < 63 ? n : 63), d); free(d); if (sscanf(b, "%u|%u", &day, &sec) != 2) sec = 0; }
    return day == pj_today() ? sec : 0;
}
static void pj_day_add(u32 sec) {
    if (!sd_ok || !sec) return;
    u32 total = pj_day_sec() + sec;
    if (gp_name[0]) { char p[NAME_LEN]; mkdir(GCD_PROF, 0777); snprintf(p, sizeof(p), GCD_PROF "/%s", gp_name); mkdir(p, 0777); }
    else mkdir(GCD_PARTIES, 0777);
    FILE *f = fopen(gp_path("jour.txt"), "w");
    if (f) { fprintf(f, "%u|%u\n", (unsigned)pj_today(), (unsigned)total); fclose(f); }
}
static int pj_left_min(void) {                          /* minutes restantes aujourd'hui (-1 : pas de limite) */
    if (!pj_limit) return -1;
    int left = pj_limit - (int)(pj_day_sec() / 60);
    return left < 0 ? 0 : left;
}
static void pj_hm(int m, char *o, int cap) { if (m >= 60) snprintf(o, cap, m % 60 ? "%d h %02d" : "%d h", m / 60, m % 60); else snprintf(o, cap, "%d min", m); }
static const char *pj_launch_check(void) {              /* NULL = le jeu peut se lancer */
    int left = pj_left_min();
    if (left != 0) return NULL;
    static char m[120]; char hm[24];
    pj_hm(pj_limit, hm, sizeof(hm));
    snprintf(m, sizeof(m), "%s (%s) : %s", TR("Temps de jeu du jour atteint"), hm, TR("\xC3\xA0 demain !"));
    return m;
}
static int pj_left_line(char *o, int cap) {             /* ligne pour "Lancer ce jeu ?" (0 = rien) */
    int left = pj_left_min();
    if (left < 0) return 0;
    char hm[24]; pj_hm(left, hm, sizeof(hm));
    snprintf(o, cap, "%s%s%s : %s", TR("Temps de jeu restant aujourd'hui"), gp_name[0] ? " - " : "", gp_name, hm);
    return 1;
}

/* ---------- ecran des profils (START > Outils > Profils, et au demarrage : "Qui joue ?") ---------- */
static int pj_boot = 0;                                  /* (ouvert au demarrage) */
static void pj_open(int boot) {
    pj_scan();
    pj_boot = boot; pj_del = 0;
    pj_sel = 0;
    for (int i = 0; i < pj_n; i++) if (!strcasecmp(pj_list[i], gp_name)) pj_sel = i + 1;
    tl_screen = TL_PROFILS;
}
static void pj_kb_done(const char *t) {
    char n[32];
    snprintf(n, sizeof(n), "%s", t);
    char *s = trim(n);
    if (!pj_name_ok(s)) { tl_note("Nom de profil invalide (20 lettres au plus, sans / : * ?)"); return; }
    pj_switch(s);
    pj_open(pj_boot);
    char m[96]; snprintf(m, sizeof(m), "%s %s", TR("Profil :"), s); snprintf(tl_msg, sizeof(tl_msg), "%s", m); tl_msg_t = now_ms();
}
static int pj_rmtree(const char *dir) {                 /* (fichiers du profil : pas de sous-dossiers) */
    DIR *d = opendir(dir);
    struct dirent *e;
    char p[NAME_LEN];
    while (d && (e = readdir(d)) != NULL) { if (e->d_name[0] == '.' && (!e->d_name[1] || e->d_name[1] == '.')) continue; snprintf(p, sizeof(p), "%s/%s", dir, e->d_name); remove(p); }
    if (d) closedir(d);
    return rmdir(dir) == 0;
}
static void pj_input(u32 down, u32 nav, int fire) {
    int rows = pj_n + 2;                                  /* principal, profils, nouveau */
    int was = pj_sel;
    if (fire && (nav & PAD_BUTTON_DOWN)) pj_sel = (pj_sel + 1) % rows;
    if (fire && (nav & PAD_BUTTON_UP)) pj_sel = (pj_sel + rows - 1) % rows;
    if (pj_sel != was) pj_del = 0;
    if (down & PAD_BUTTON_A) {
        if (pj_sel == rows - 1) { kb_start(KB_PROFILE, "", -1, -1); return; }
        pj_switch(pj_sel == 0 ? "" : pj_list[pj_sel - 1]);
        tl_screen = TL_NONE;
        char m[96]; snprintf(m, sizeof(m), "%s %s", TR("Profil :"), gp_name[0] ? gp_name : TR("Principal"));
        lib_note(m);
        return;
    }
    if ((down & PAD_BUTTON_X) && pj_sel >= 1 && pj_sel <= pj_n) {   /* supprimer (X deux fois) : pas le profil actuel */
        if (!strcasecmp(pj_list[pj_sel - 1], gp_name)) { tl_note("C'est le profil actuel : choisis-en un autre d'abord"); return; }
        if (!pj_del) { pj_del = 1; tl_note("X encore une fois : supprimer ce profil (sa carte m\xC3\xA9moire reste)"); return; }
        char p[NAME_LEN]; snprintf(p, sizeof(p), GCD_PROF "/%s", pj_list[pj_sel - 1]);
        tl_note(pj_rmtree(p) ? "Profil supprim\xC3\xA9" : "Suppression impossible");
        pj_del = 0; pj_scan(); if (pj_sel > pj_n) pj_sel = pj_n;
        return;
    }
    if (down & PAD_BUTTON_Y) { pj_ask = !pj_ask; settings_dirty = 1; tl_note(pj_ask ? "Demander au d\xC3\xA9marrage : oui" : "Demander au d\xC3\xA9marrage : non"); }
    if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) { tl_screen = TL_NONE; if (settings_dirty) save_settings(); }
}
static void pj_draw_profils(void) {
    char fv[160], b[96];
    tl_frame(TR(pj_boot ? "Qui joue ?" : "Profils"), NULL);
    int rows = pj_n + 2;
    f32 rh = rows > 9 ? 26 : 30;
    for (int r = 0; r < rows && r < 12; r++) {
        f32 y = 92 + r * rh;
        int on = r == pj_sel;
        gp_zone(52, y, 536, rh - 3, &pj_sel, r, GPZ_SEL);
        if (on) gx_rrect_fill(52, y, 536, rh - 3, 7, TL_SEL1, TL_SEL2);
        const char *nm = r == 0 ? TR("Principal") : r == rows - 1 ? TR("+ Nouveau profil") : pj_list[r - 1];
        int cur = (r == 0 && !gp_name[0]) || (r > 0 && r < rows - 1 && !strcasecmp(pj_list[r - 1], gp_name));
        fit_text(F_MED, nm, 360, fv, sizeof(fv));
        gx_text1(F_MED, 66, y + rh / 2 + 6, fv, r == rows - 1 && !on ? RGBA(190, 160, 255, 255) : RGBA(255, 255, 255, 255));
        if (cur) gx_text_r(F_SMALL, 574, y + rh / 2 + 5, TR("actuel"), RGBA(130, 230, 160, 255));
        else if (r < rows - 1) {
            char card[64]; int lim = 0;
            if (r == 0) { snprintf(card, sizeof(card), "%s", pj_main_card[0] ? pj_main_card : vc_active); lim = gp_name[0] ? pj_main_limit : pj_limit; }
            else pj_load_file(pj_list[r - 1], card, sizeof(card), &lim);
            if (lim) { char hm[24]; pj_hm(lim, hm, sizeof(hm)); snprintf(b, sizeof(b), "%s   %s/%s", card, hm, TR("jour")); } else snprintf(b, sizeof(b), "%s", card);
            gx_text_r(F_SMALL, 574, y + rh / 2 + 5, b, RGBA(190, 160, 255, 220));
        }
    }
    gx_text_c(F_SMALL, 320, 384, TR("Chaque profil : sa carte m\xC3\xA9moire, ses favoris, ses succ\xC3\xA8s, son temps de jeu"), RGBA(200, 190, 245, 230));
    snprintf(b, sizeof(b), "%s : %s", TR("Demander au d\xC3\xA9marrage"), pj_ask ? TR("Oui") : TR("Non"));
    gx_text_c(F_SMALL, 320, 404, b, RGBA(170, 155, 225, 230));
    tl_msgbox();
    static const char *const k[] = { "A", "X", "Y", "B" }, *const l[] = { "CHOISIR", "SUPPRIMER", "DEMANDER", "FERMER" };
    tl_legend(k, l, 4);
}
/* au demarrage : "Qui joue ?" s'il y a d'autres profils (reglage) */
static void pj_boot_ask(void) { pj_scan(); if (pj_ask && pj_n > 0) pj_open(1); }
/* Reglages > Jeux > Limite de jeu par jour (profil actuel) */
static void pj_limit_text(char *o, int cap) {
    if (!pj_limit) { snprintf(o, cap, "%s", "Non"); return; }
    char hm[24]; pj_hm(pj_limit, hm, sizeof(hm));
    if (gp_name[0]) snprintf(o, cap, "%s (%s)", hm, gp_name); else snprintf(o, cap, "%s", hm);
}
static void pj_limit_next(int d) {
    int n = (int)(sizeof(pj_limits) / sizeof(pj_limits[0])), cur = 0;
    for (int i = 0; i < n; i++) if (pj_limits[i] == pj_limit) cur = i;
    pj_limit = pj_limits[(cur + d + n) % n];
    if (!gp_name[0]) pj_main_limit = pj_limit;
    settings_dirty = 1;
}

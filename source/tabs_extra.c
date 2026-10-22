// GC-MP3 v23 : onglet JEUX, onglet TELECHARGEMENTS, et curseur au stick dans
// tous les onglets (inclus dans main.c, apres fun_extra.c).

/* ======================= Curseur au stick partout =======================
   Le stick principal deplace une fleche (comme dans l'onglet WEB). Les
   dessins declarent les zones cliquables de l'image ; A sur une zone la
   choisit (ligne d'une liste, bouton, onglet, ligne du menu...). */
static int gp_hit(void) {
    for (int i = gp_nzl - 1; i >= 0; i--)                   /* (les fenetres du dessus d'abord) */
        if (gp_x >= gp_zl[i].x && gp_x <= gp_zl[i].x + gp_zl[i].w && gp_y >= gp_zl[i].y && gp_y <= gp_zl[i].y + gp_zl[i].h) return i;
    return -1;
}
static int gp_usable(void) { return tab != TAB_WEB && !fun_mode && !party_on && !saver_on && !night_on && !kb_open; }
/* deplace la fleche ; A sur une zone : la choisit. Renvoie les boutons a traiter. */
static u32 gp_input(u32 down) {
    if (!gp_usable()) { gp_on = 0; return down; }
    int mx = PAD_StickX(0), my = PAD_StickY(0), ax = abs(mx), ay = abs(my);
    if (ax > 18 || ay > 18) {
        gp_on = 1;
        gp_t = now_ms();
        f32 sx = ax > 18 ? (f32)(mx > 0 ? mx - 18 : mx + 18) : 0, sy = ay > 18 ? (f32)(my > 0 ? my - 18 : my + 18) : 0;
        f32 k = 0.085f + (ax + ay > 120 ? 0.07f : 0);
        gp_x += sx * k * 1.4f / ux_k;
        gp_y -= sy * k * 1.4f;
        if (gp_x < 2) gp_x = 2;
        if (gp_x > 636) gp_x = 636;
        if (gp_y < 2) gp_y = 2;
        if (gp_y > 476) gp_y = 476;
    } else if (gp_on && now_ms() - gp_t > 6000) gp_on = 0;
    if (!gp_on || !(down & PAD_BUTTON_A)) return down;
    int i = gp_hit();
    if (i < 0) return down;
    gpzone_t *z = &gp_zl[i];
    gp_t = now_ms();
    switch (z->code) {
    case GPZ_SEL:  if (z->sel) *z->sel = z->val; return down;                         /* la ligne, puis A comme d'habitude */
    case GPZ_KEY:  return (down & ~PAD_BUTTON_A) | (u32)z->val;                        /* comme un autre bouton */
    case GPZ_TAB:  if (z->val >= 0 && z->val < TAB_COUNT && tab_shown(z->val)) { tab = z->val; tab_enter(tab); } return down & ~PAD_BUTTON_A;
    case GPZ_STEP: transport_step(z->val); return down & ~PAD_BUTTON_A;
    }
    return down;
}
static void gp_draw(void) {
    if (!gp_on || !gp_usable()) return;
    int i = gp_hit();
    if (i >= 0) {                                           /* la zone survolee */
        gpzone_t *z = &gp_zl[i];
        gx_rrect_stroke(z->x - 1, z->y - 1, z->w + 2, z->h + 2, 5, 1.5f, RGBA(255, 230, 140, 220));
    }
    f32 x = gp_x, y = gp_y;
    uxsave_t uz = ux_native(x);
    gx_tri(x + 1, y + 1, x + 1, y + 19, x + 14, y + 14, RGBA(0, 0, 0, 150));
    gx_tri(x, y, x, y + 18, x + 13, y + 13, RGBA(255, 255, 255, 255));
    gx_tri(x + 2, y + 4, x + 2, y + 14, x + 9, y + 11, i >= 0 ? RGBA(255, 200, 80, 255) : RGBA(90, 80, 130, 255));
    ux_restore(uz);
}
/* zones communes : boutons du lecteur, bandeau des onglets */
static void gp_player_zones(void) {
    if (!EL(EL_BUTTONS)) return;
    gp_zone(326, 343, 42, 42, NULL, -1, GPZ_STEP);
    gp_zone(394, 335, 58, 58, NULL, src != SRC_NONE ? PAD_BUTTON_Y : PAD_BUTTON_A, GPZ_KEY);
    gp_zone(478, 343, 42, 42, NULL, 1, GPZ_STEP);
    gp_zone(546, 343, 42, 42, NULL, src == SRC_RADIO ? PAD_BUTTON_Y : PAD_BUTTON_B, GPZ_KEY);
}

/* ======================= Onglet JEUX ======================= */
enum { GM_KARAOKE, GM_RHYTHM, GM_QUIZ, GM_SNAKE, GM_SNAKE2, GM_BRICKS, GM_PONG, GM_FLAPPY, GM_SIMON, GM_COUNT };
static const char *const gm_names[GM_COUNT] = { "Karaok\xC3\xA9", "Jeu de rythme", "Quiz musical", "Snake", "Snake sans murs",
                                                "Casse-briques", "Pong", "Note volante", "Simon" };
static const char *const gm_desc[GM_COUNT] = { "Les paroles en grand", "Solo ou duel", "1 \xC3\xA0 4 joueurs", "Mange les notes",
                                               "On traverse les bords", "Briques en rythme", "1 ou 2 joueurs", "Vole entre les murs", "Retiens la suite" };
static const char *const gm_rec[GM_COUNT] = { NULL, NULL, NULL, "jeu:snake", "jeu:snake-sans-murs", "jeu:casse-briques", NULL, "jeu:note-volante", "jeu:simon" };
static int gm_sel = 0;
static int gm_record[GM_COUNT];
static u64 gm_rec_t = 0;
#define GM_COLS 3
static void games_tab_launch(int g) {
    int k = (tab == TAB_GAMES) ? -1 : fun_selected_track();
    switch (g) {
    case GM_KARAOKE: karaoke_start(k); break;
    case GM_RHYTHM:  rhythm_start(k); break;
    case GM_QUIZ:    if (fun_tracks() < 4) lib_note("Il faut au moins 4 morceaux"); else quiz_start(); break;
    case GM_SNAKE:   sn_wrap = 0; snake_start(); break;
    case GM_SNAKE2:  sn_wrap = 1; snake_start(); break;
    case GM_BRICKS:  bricks_start(); break;
    case GM_PONG:    pong_start(); break;
    case GM_FLAPPY:  flappy_start(); break;
    case GM_SIMON:   simon_start(); break;
    }
    gm_rec_t = 0;
}
static int gl_on;                                   /* v25 : Mes jeux GameCube (gclib.c) */
static void gclib_open(void);
static void ap_open(void);                         /* v41 : applis (tools.c) */
static void mc_open(void);                         /* v43 : cartes memoire (tools.c) */
static void gclib_input(u32 down, u32 nav, int fire);
static void draw_gclib(void);
static void games_tab_input(u32 down, u32 nav, int fire) {
    if (gl_on) { gclib_input(down, nav, fire); return; }
    if (gm_sel >= GM_COUNT) {                              /* barres jeux GameCube / applis / cartes memoire (v41, v43) */
        int k = gm_sel - GM_COUNT;
        if (fire && (nav & PAD_BUTTON_DOWN)) gm_sel = k < GM_COUNT ? k : 0;
        if (fire && (nav & PAD_BUTTON_UP)) gm_sel = (GM_COUNT - 1) / GM_COLS * GM_COLS + k < GM_COUNT ? (GM_COUNT - 1) / GM_COLS * GM_COLS + k : GM_COUNT - 1;
        if (fire && (nav & PAD_BUTTON_RIGHT)) gm_sel = GM_COUNT + (k + 1) % 3;
        if (fire && (nav & PAD_BUTTON_LEFT)) gm_sel = GM_COUNT + (k + 2) % 3;
        if (down & PAD_BUTTON_A) { if (k == 0) gclib_open(); else if (k == 1) ap_open(); else mc_open(); }
    } else {
        if (fire && (nav & PAD_BUTTON_RIGHT)) gm_sel = (gm_sel + 1) % GM_COUNT;
        if (fire && (nav & PAD_BUTTON_LEFT)) gm_sel = (gm_sel + GM_COUNT - 1) % GM_COUNT;
        if (fire && (nav & PAD_BUTTON_DOWN)) gm_sel = gm_sel + GM_COLS >= GM_COUNT ? GM_COUNT + gm_sel % GM_COLS : gm_sel + GM_COLS;
        if (fire && (nav & PAD_BUTTON_UP)) gm_sel = gm_sel < GM_COLS ? GM_COUNT + gm_sel : gm_sel - GM_COLS;
        if (down & PAD_BUTTON_A) games_tab_launch(gm_sel);
    }
    if ((down & PAD_BUTTON_Y) && src == SRC_FILE && playing) paused = !paused;
    if (down & PAD_BUTTON_B) { stop_track(); stop_station(); }
}
static void gm_icon(int g, f32 cx, f32 cy, f32 t) {
    col_t w = RGBA(255, 255, 255, 235);
    switch (g) {
    case GM_KARAOKE:                                               /* micro */
        gx_ellipse(cx, cy - 8, 9, 11, RGBA(230, 230, 250, 255), RGBA(150, 150, 190, 255));
        gx_rect(cx - 2, cy + 3, 4, 16, w); gx_rect(cx - 8, cy + 18, 16, 3, w); break;
    case GM_RHYTHM:
        for (int i = 0; i < 4; i++) gx_circle(cx - 24 + i * 16, cy + 6 - 10 * sinf(t * 3 + i), 6, lane_top[i], lane_bot[i]);
        break;
    case GM_QUIZ: fun_shadow_text(F_LARGE, cx, cy + 14, "?", 2.0f, RGBA(255, 220, 90, 255)); break;
    case GM_SNAKE: case GM_SNAKE2:
        for (int i = 0; i < 6; i++) gx_circle(cx - 25 + i * 9, cy + 6 * sinf(t * 4 + i * 0.8f), 5, hsv(120 + i * 12, 0.6f, 1, 255), hsv(140 + i * 12, 0.9f, 0.6f, 255));
        if (g == GM_SNAKE2) gx_rrect_stroke(cx - 34, cy - 20, 68, 40, 6, 1.2f, RGBA(255, 255, 255, 90));
        break;
    case GM_BRICKS:
        for (int r = 0; r < 2; r++) for (int c = 0; c < 4; c++) gx_rrect_fill(cx - 32 + c * 16, cy - 18 + r * 9, 14, 7, 2, hsv(r * 60 + c * 20, 0.6f, 1, 255), hsv(r * 60 + c * 20, 0.9f, 0.6f, 255));
        gx_rrect_fill(cx - 14 + 10 * sinf(t * 2), cy + 14, 28, 5, 2, w, w);
        gx_circle(cx + 6 * sinf(t * 3), cy + 4, 3.5f, w, w); break;
    case GM_PONG:
        gx_rect(cx - 30, cy - 12 + 6 * sinf(t * 2), 4, 22, RGBA(140, 200, 255, 255));
        gx_rect(cx + 26, cy - 12 - 6 * sinf(t * 2), 4, 22, RGBA(255, 150, 170, 255));
        gx_circle(cx + 20 * sinf(t * 2.5f), cy, 4, w, w); break;
    case GM_FLAPPY:
        gx_ellipse(cx - 8, cy + 4 * sinf(t * 5), 10, 8, RGBA(255, 245, 170, 255), RGBA(255, 190, 60, 255));
        gx_rect(cx + 1, cy - 20 + 4 * sinf(t * 5), 3, 22, RGBA(255, 220, 120, 255));
        gx_rect(cx + 20, cy - 26, 10, 16, RGBA(120, 200, 140, 255)); gx_rect(cx + 20, cy + 12, 10, 14, RGBA(120, 200, 140, 255)); break;
    default:
        for (int i = 0; i < 4; i++) { int on = ((int)(t * 2)) % 4 == i; gx_circle(cx + (i % 2 ? 11 : -11), cy + (i < 2 ? 9 : -9), 9, on ? lane_top[i] : lane_bot[i], lane_bot[i]); }
        break;
    }
}
static void draw_tab_games(void) {
    if (gl_on) { draw_gclib(); return; }
    char buf[96];
    draw_panel(11, 73, 618, 338);
    snprintf(buf, sizeof(buf), "%d", GM_COUNT);
    draw_tabs(buf);
    if (!gm_rec_t) {                                            /* records : relus en ouvrant l'onglet et apres une partie (v45 : plus toutes les 5 s) */
        gm_rec_t = now_ms();
        for (int g = 0; g < GM_COUNT; g++) gm_record[g] = gm_rec[g] && sd_ok ? rg_record_get(gm_rec[g]) : 0;
    }
    f32 t = (f32)(now_ms() % 100000) / 1000.0f;
    for (int k = 0; k < 3; k++) {   /* v25 : jeux GameCube ; v41 : + applis ; v43 : + cartes memoire */
        static const char *const bl[3] = { "Jeux GameCube", "Mes applis", "Cartes m\xC3\xA9moire" };
        int on = gm_sel == GM_COUNT + k;
        const f32 bw = 196, bx = 22 + k * (bw + 4), by = 110, bh = 30;
        if (on) gx_glow(bx, by, bw, bh, 8, RGBA(150, 80, 255, 140), 3);
        gx_rrect_fill(bx, by, bw, bh, 8, on ? RGBA(116, 46, 236, 255) : RGBA(34, 20, 84, 230), on ? RGBA(74, 22, 172, 255) : RGBA(20, 12, 52, 230));
        gx_rrect_stroke(bx, by, bw, bh, 8, 1.2f, on ? RGBA(200, 150, 255, 255) : RGBA(90, 64, 170, 255));
        if (k == 0) {                                          /* disque */
            gx_circle(bx + 22, by + bh / 2, 10, RGBA(200, 200, 215, 255), RGBA(120, 120, 140, 255));
            gx_circle(bx + 22, by + bh / 2, 3, RGBA(30, 20, 60, 255), RGBA(30, 20, 60, 255));
        } else if (k == 1) {                                   /* fenetre d'appli */
            gx_rrect_fill(bx + 12, by + 8, 20, 14, 3, RGBA(200, 200, 215, 255), RGBA(120, 120, 140, 255));
            gx_rect(bx + 15, by + 11, 14, 5, RGBA(30, 20, 60, 255));
        } else {                                               /* carte memoire */
            gx_rrect_fill(bx + 15, by + 6, 14, 18, 2, RGBA(200, 200, 215, 255), RGBA(120, 120, 140, 255));
            gx_rect(bx + 18, by + 9, 8, 4, RGBA(30, 20, 60, 255));
        }
        char lb[64];
        fit_text(F_MED, TR(bl[k]), (int)bw - 72, lb, sizeof(lb));
        gx_text1(F_MED, bx + 40, by + 21, lb, RGBA(255, 255, 255, 255));
        gx_text_r(F_SMALL, bx + bw - 10, by + 20, "(A)", on ? RGBA(235, 225, 255, 255) : RGBA(170, 155, 225, 255));
        gp_zone(bx, by, bw, bh, &gm_sel, GM_COUNT + k, GPZ_SEL);
    }
    const f32 cw = 192, ch = 72, x0 = 22, y0 = 148;
    for (int g = 0; g < GM_COUNT; g++) {
        f32 x = x0 + (g % GM_COLS) * (cw + 8), y = y0 + (g / GM_COLS) * (ch + 5);
        int on = g == gm_sel;
        if (on) gx_glow(x, y, cw, ch, 10, RGBA(150, 80, 255, 140), 3);
        gx_rrect_fill(x, y, cw, ch, 10, on ? RGBA(116, 46, 236, 255) : RGBA(34, 20, 84, 230), on ? RGBA(74, 22, 172, 255) : RGBA(20, 12, 52, 230));
        gx_rrect_stroke(x, y, cw, ch, 10, 1.2f, on ? RGBA(200, 150, 255, 255) : RGBA(90, 64, 170, 255));
        gm_icon(g, x + 42, y + ch / 2, t);
        fit_text(F_MED, gm_names[g], (int)cw - 88, buf, sizeof(buf));
        gx_text1(F_MED, x + 82, y + 26, buf, RGBA(255, 255, 255, 255));
        fit_text(F_SMALL, gm_desc[g], (int)cw - 88, buf, sizeof(buf));
        gx_text1(F_SMALL, x + 82, y + 44, buf, on ? RGBA(235, 225, 255, 255) : RGBA(170, 155, 225, 255));
        if (gm_record[g] > 0) { snprintf(buf, sizeof(buf), "Record %d", gm_record[g]); gx_text1(F_SMALL, x + 82, y + 62, buf, RGBA(255, 210, 120, 255)); }
        gp_zone(x, y, cw, ch, &gm_sel, g, GPZ_SEL);
    }
    gx_text_c(F_SMALL, 320, 402, "Croix ou curseur : choisir     A : jouer     Y : pause de la musique     B : arr\xC3\xAAter la musique", RGBA(200, 190, 245, 230));
}

/* ======================= Onglet TELECHARGEMENTS ======================= */
/* Ce qui arrive sur la carte SD : telechargements de l'onglet WEB, episodes de
   podcasts, fichiers envoyes depuis le telephone. Historique dans
   sd:/gc-mp3/telechargements.txt (date | origine | chemin | taille). */
#define DLH_FILE GCD "/telechargements.txt"
#define DLH_MAX 60
typedef struct { char path[NAME_LEN]; char when[20]; char from[12]; long size; } dlh_t;
static dlh_t *dlh = NULL;
static int dlh_n = 0, dlh_loaded = 0, dl_sel = 0, dl_confirm = -1;
static int tp_done_r = 0;

static void dlh_load(void) {
    dlh_loaded = 1;
    dlh_n = 0;
    if (!dlh) dlh = (dlh_t *)malloc(sizeof(dlh_t) * DLH_MAX);
    if (!dlh || !sd_ok) return;
    FILE *f = fopen(DLH_FILE, "r");
    if (!f) return;
    char line[NAME_LEN + 80];
    while (fgets(line, sizeof(line), f)) {
        char *p[4]; int n = 0;
        p[n++] = line;
        for (char *c = line; *c && n < 4; c++) if (*c == '|') { *c = 0; p[n++] = c + 1; }
        if (n < 4) continue;
        if (dlh_n == DLH_MAX) { memmove(&dlh[0], &dlh[1], sizeof(dlh_t) * (DLH_MAX - 1)); dlh_n--; }
        dlh_t *h = &dlh[dlh_n++];
        snprintf(h->when, sizeof(h->when), "%s", trim(p[0]));
        snprintf(h->from, sizeof(h->from), "%s", trim(p[1]));
        snprintf(h->path, sizeof(h->path), "%s", trim(p[2]));
        h->size = atol(p[3]);
    }
    fclose(f);
}
/* un fichier vient d'arriver (appele par le telechargement ou le telephone) */
static void dlh_add(const char *from, const char *path) {
    if (!sd_ok || !path || !path[0]) return;
    struct stat st;
    long sz = stat(path, &st) == 0 ? (long)st.st_size : 0;
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    char when[20] = "-";
    if (tm && tm->tm_year >= 100) snprintf(when, sizeof(when), "%02d/%02d %02d:%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_hour, tm->tm_min);
    mkdir(GCD, 0777);
    FILE *f = fopen(DLH_FILE, "a");
    if (f) { fprintf(f, "%s | %s | %s | %ld\n", when, from, path, sz); fclose(f); }
    dlh_loaded = 0;                                          /* (relu a l'affichage) */
}
static void dltab_pump(void) {
    while (tp_done_r != tp_done_w) {                         /* fichiers recus du telephone */
        dlh_add("t\xC3\xA9l\xC3\xA9phone", tp_done_path[tp_done_r % 4]);
        tp_done_r++;
    }
}
static int dl_rows(void) { return dlh_n; }
static void dl_open(int i) {                                 /* A : ecouter / voir le fichier */
    if (i < 0 || i >= dlh_n) return;
    int r = dlh_n - 1 - i;                                    /* (les plus recents en haut) */
    const char *p = dlh[r].path;
    struct stat st;
    if (stat(p, &st) != 0) { lib_note("Fichier introuvable (d\xC3\xA9plac\xC3\xA9 ou effac\xC3\xA9 ?)"); return; }
    for (int k = 0; k < nfiles; k++) if (!strcmp(names[k], p)) {
        if (is_retro[k]) { tab = TAB_RETRO; tab_enter(tab); lib_note("Morceau r\xC3\xA9tro : choisis-le dans l'onglet R\xC3\xA9TRO"); return; }
        queue_build(k); start_track(k); lib_note("Lecture"); return;
    }
    if (img_ext(p)) {                                         /* photo : plein ecran dans PHOTOS */
        tab = TAB_PHOTO; tab_enter(tab); photo_rescan();
        for (int f = 0; f < npf; f++) if (!strcmp(pf[f].path, p)) { pv_open(f); break; }
        return;
    }
    if (audio_ext_len(p)) { music_rescan(); for (int k = 0; k < nfiles; k++) if (!strcmp(names[k], p)) { queue_build(k); start_track(k); return; } }
    lib_note("Ouvre ce fichier dans l'onglet FICHIERS");
}
static void dltab_input(u32 down, u32 nav, int fire) {
    if (!dlh_loaded) dlh_load();
    int rows = dl_rows();
    if (fire && rows > 0) {
        if (nav & PAD_BUTTON_DOWN) dl_sel++;
        if (nav & PAD_BUTTON_UP) dl_sel--;
        if (nav & PAD_BUTTON_RIGHT) dl_sel += 8;
        if (nav & PAD_BUTTON_LEFT) dl_sel -= 8;
        if (dl_sel < 0) dl_sel = 0;
        if (dl_sel > rows - 1) dl_sel = rows - 1;
        dl_confirm = -1;
    }
    if (down & PAD_BUTTON_A) dl_open(dl_sel);
    if ((down & PAD_BUTTON_B) && dl_active) { dl_stop("T\xC3\xA9l\xC3\xA9" "chargement annul\xC3\xA9", 1); lib_note("T\xC3\xA9l\xC3\xA9" "chargement annul\xC3\xA9"); }
    if ((down & PAD_BUTTON_X) && rows > 0) {                 /* X deux fois : effacer le fichier */
        if (dl_confirm != dl_sel) { dl_confirm = dl_sel; lib_note("X encore une fois pour effacer ce fichier"); }
        else {
            int r = dlh_n - 1 - dl_sel;
            if (remove(dlh[r].path) == 0) { lib_note("Fichier effac\xC3\xA9"); music_rescan(); } else lib_note("Impossible d'effacer ce fichier");
            dl_confirm = -1;
        }
    }
    if (down & PAD_BUTTON_Y) { tab = TAB_WEB; tab_enter(tab); }
}
static void dl_fmt_size(long b, char *o, int cap) {
    if (b >= 1024L * 1024) snprintf(o, cap, "%ld,%ld Mo", b / (1024L * 1024), (b % (1024L * 1024)) * 10 / (1024L * 1024));
    else snprintf(o, cap, "%ld Ko", (b + 1023) / 1024);
}
static void draw_tab_dl(void) {
    char buf[NAME_LEN + 64], d[NAME_LEN], t[40];
    if (!dlh_loaded) dlh_load();
    draw_panel(11, 73, 618, 338);
    snprintf(buf, sizeof(buf), "%d", dlh_n);
    draw_tabs(buf);
    f32 y = 120;
    /* en cours */
    if (dl_active) {
        text_display(dl_name, d, sizeof(d));
        fit_text(F_MED, d, 360, buf, sizeof(buf));
        gx_text1(F_MED, 30, y + 14, buf, RGBA(255, 255, 255, 255));
        f32 frac = dl_total > 0 ? (f32)dl_got / (f32)dl_total : 0;
        if (frac > 1) frac = 1;
        gx_rrect_fill(30, y + 22, 420, 6, 3, RGBA(70, 50, 140, 230), RGBA(70, 50, 140, 230));
        if (dl_total > 0) gx_rrect_fill(30, y + 22, 420 * frac, 6, 3, RGBA(150, 90, 255, 255), RGBA(110, 60, 230, 255));
        u64 el = now_ms() - dl_t0;
        long kbs = el > 500 ? (long)((long long)dl_got * 1000 / 1024 / (long long)el) : 0;
        char a[24], b[24];
        dl_fmt_size(dl_got, a, sizeof(a)); dl_fmt_size(dl_total, b, sizeof(b));
        if (dl_total > 0) snprintf(t, sizeof(t), "%s / %s   %ld Ko/s", a, b, kbs); else snprintf(t, sizeof(t), "%s   %ld Ko/s", a, kbs);
        gx_text_r(F_SMALL, 610, y + 14, "WEB / PODCAST", RGBA(255, 210, 120, 255));
        gx_text1(F_SMALL, 460, y + 29, t, RGBA(200, 190, 245, 255));
        y += 40;
    }
    if (tp_up_active && tp_up_total > 0) {
        text_display(tp_up_name, d, sizeof(d));
        fit_text(F_MED, d, 360, buf, sizeof(buf));
        gx_text1(F_MED, 30, y + 14, buf, RGBA(255, 255, 255, 255));
        f32 frac = (f32)tp_up_done / (f32)tp_up_total;
        gx_rrect_fill(30, y + 22, 420, 6, 3, RGBA(70, 50, 140, 230), RGBA(70, 50, 140, 230));
        gx_rrect_fill(30, y + 22, 420 * (frac > 1 ? 1 : frac), 6, 3, RGBA(90, 220, 140, 255), RGBA(40, 160, 90, 255));
        gx_text_r(F_SMALL, 610, y + 14, "T\xC3\x89L\xC3\x89PHONE", RGBA(140, 255, 170, 255));
        y += 40;
    }
    for (int i = 0; i < pa_nq && y < 220; i++) {             /* episodes de podcasts en attente */
        text_display(pa_q_title[i], d, sizeof(d));
        snprintf(buf, sizeof(buf), "En attente : %s", d);
        fit_text(F_SMALL, buf, 560, t, sizeof(t));
        fit_text(F_SMALL, buf, 560, d, sizeof(d));
        gx_text1(F_SMALL, 30, y + 12, d, RGBA(200, 190, 245, 230));
        y += 18;
    }
    if (y > 120) { gx_rect(25, y + 4, 590, 1, RGBA(76, 52, 150, 200)); y += 10; }
    /* historique */
    if (dlh_n == 0) {
        gx_text1(F_MED, 30, y + 24, "Aucun fichier re\xC3\xA7u pour l'instant", RGBA(255, 205, 110, 255));
        gx_text1(F_SMALL, 30, y + 48, "T\xC3\xA9l\xC3\xA9" "charge depuis l'onglet WEB, les PODCASTS (X), ou envoie des fichiers", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 30, y + 66, "depuis ton t\xC3\xA9l\xC3\xA9phone (onglet T\xC3\x89L\xC3\x89PHONE, Envoyer) : ils appara\xC3\xAEtront ici.", RGBA(190, 180, 235, 255));
    } else {
        int vis = (int)((392 - y) / 22);
        if (vis < 3) vis = 3;
        if (dl_sel > dlh_n - 1) dl_sel = dlh_n - 1;
        int start = dl_sel - vis / 2;
        if (start > dlh_n - vis) start = dlh_n - vis;
        if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < dlh_n; i++) {
            int row = start + i, r = dlh_n - 1 - row;
            f32 ry = y + i * 22;
            int on = row == dl_sel;
            if (on) gx_rrect_fill(20, ry + 1, 600, 20, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
            struct stat st;
            int gone = dl_confirm == row ? 0 : 0;
            (void)gone;
            text_display(base_of(dlh[r].path), d, sizeof(d));
            fit_text(F_MED, d, 330, buf, sizeof(buf));
            gx_text1(F_MED, 30, ry + 16, buf, on ? RGBA(255, 255, 255, 255) : RGBA(215, 205, 245, 255));
            gx_text1(F_SMALL, 372, ry + 15, dlh[r].from, on ? RGBA(255, 230, 160, 255) : RGBA(170, 155, 225, 255));
            dl_fmt_size(dlh[r].size, t, sizeof(t));
            gx_text_r(F_SMALL, 530, ry + 15, t, on ? RGBA(255, 255, 255, 255) : RGBA(170, 155, 225, 255));
            gx_text_r(F_SMALL, 612, ry + 15, dlh[r].when, on ? RGBA(255, 255, 255, 255) : RGBA(150, 135, 210, 255));
            (void)st;
            gp_zone(20, ry + 1, 600, 20, &dl_sel, row, GPZ_SEL);
        }
    }
    gx_text_c(F_SMALL, 320, 402, dl_active ? "A : ouvrir     X X : effacer     B : annuler le t\xC3\xA9l\xC3\xA9" "chargement     Y : onglet WEB"
                                           : "A : \xC3\xA9" "couter / voir     X X : effacer le fichier     Y : aller dans l'onglet WEB", RGBA(200, 190, 245, 230));
}

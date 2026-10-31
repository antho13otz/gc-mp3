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
static int gb_screen_on(void);                     /* v210 : globe.c */
static int gp_usable(void) { return !gb_screen_on() && tab != TAB_WEB && !fun_mode && !party_on && !saver_on && !night_on && !kb_open; }
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
    /* v104 : la croix reprend la main : la fleche disparait (le stick la fait revenir) */
    else if (gp_on && (down & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT))) gp_on = 0;
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
enum { GM_KARAOKE, GM_RHYTHM, GM_QUIZ, GM_SNAKE, GM_SNAKE2, GM_BRICKS, GM_PONG, GM_FLAPPY, GM_SIMON, GM_DUEL, GM_RACE, GM_BUBBLES, GM_TRON, GM_HOCKEY, GM_P4, GM_SUMO, GM_COUNT };
static const char *const gm_names[GM_COUNT] = { "Karaok\xC3\xA9", "Jeu de rythme", "Quiz musical", "Snake", "Snake sans murs",
                                                "Casse-briques", "Pong", "Note volante", "Simon", "D\xC3\xA9gaine !", "Course folle", "Chasse aux bulles", "Motos lumineuses", "Air hockey", "Puissance 4", "Sumo" };
static const char *const gm_desc[GM_COUNT] = { "Les paroles en grand", "Solo ou duel", "1 \xC3\xA0 4 joueurs", "Mange les notes",
                                               "On traverse les bords", "Briques en rythme", "1 ou 2 joueurs", "Vole entre les murs", "Retiens la suite",
                                               "R\xC3\xA9" "flexes, 1 \xC3\xA0 4", "Mart\xC3\xA8le A, 1 \xC3\xA0 4", "Au stick, 1 \xC3\xA0 4", "Duel, 2 \xC3\xA0 4", "Duel au stick", "Duel, 2 joueurs", "Pousse, 2 \xC3\xA0 4" };
static const char *const gm_rec[GM_COUNT] = { NULL, NULL, NULL, "jeu:snake", "jeu:snake-sans-murs", "jeu:casse-briques", NULL, "jeu:note-volante", "jeu:simon", "jeu:degaine", "jeu:course", "jeu:bulles", NULL, NULL, NULL, NULL };
static int gm_sel = 0;
static int gm_record[GM_COUNT];
static u64 gm_rec_t = 0;
#define GM_COLS 4                                     /* v279 : 16 jeux : 4 colonnes */
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
    case GM_DUEL:    duel_start(); break;                 /* v235 : fun_party.c */
    case GM_RACE:    race_start(); break;
    case GM_BUBBLES: bubbles_start(); break;
    case GM_TRON:    tron_start(); break;      /* v279 : fun_duel2.c */
    case GM_HOCKEY:  hockey_start(); break;
    case GM_P4:      p4_start(); break;
    case GM_SUMO:    sumo_start(); break;
    }
    gm_rec_t = 0;
}
/* v279 : mini-jeux pour le telephone : la liste (nom, description, joueurs a la manette) et le lancement sur la tele */
static int gm_phone_list(char *o, int cap) {
    int n = snprintf(o, cap, "{\"pads\":%u,\"cur\":%d,\"j\":[", (unsigned)fun_pads, fun_mode != FUN_OFF ? 1 : 0);
    for (int g = 0; g < GM_COUNT && n < cap - 200; g++) {
        n += snprintf(o + n, cap - n, "%s{\"n\":\"%s\",\"d\":\"%s\"}", g ? "," : "", gm_names[g], gm_desc[g]);
    }
    n += snprintf(o + n, cap - n, "]}");
    return n < cap ? n : cap - 1;
}
static void gm_phone_launch(int g) {
    if (g < 0 || g >= GM_COUNT) return;
    if (fun_mode != FUN_OFF) fun_leave();
    saver_on = 0; menu_open = 0; tl_screen = 0; kb_open = 0;
    tab = TAB_GAMES; tab_enter(tab);
    gm_sel = g;
    games_tab_launch(g);
}
static int gl_on;                                   /* v25 : Mes jeux GameCube (gclib.c) */
static void gclib_open(void);
static void ap_open(void);                         /* v41 : applis (tools.c) */
static void mc_open(void);                         /* v43 : cartes memoire (tools.c) */
static void gclib_input(u32 down, u32 nav, int fire);
static void gclib_open_recent(const char *path);       /* v176 (gclib.c) */
static void sc_check(void);                            /* v177 : succes (tools.c) */
/* v176 : accueil de JEUX / APPS : dernier jeu lance (jeux_recents.txt) et temps de jeu (jeux_stats.txt, mesure par Pulse) */
static char gm_last_path[NAME_LEN], gm_last_name[64];
static u32 gm_play_total = 0, gm_play_last = 0;
static void gm_recent_load(void) {
    gm_last_path[0] = gm_last_name[0] = 0; gm_play_total = gm_play_last = 0;
    if (!sd_ok) return;
    long n = 0;
    char *d = (char *)read_file(gp_path("jeux_recents.txt"), 32768, &n);
    if (d) {
        long i = 0;
        while (i < n && d[i] != '\n' && d[i] != '\r') i++;
        if (i > 0 && i < NAME_LEN) { memcpy(gm_last_path, d, i); gm_last_path[i] = 0; }
        free(d);
    }
    if (gm_last_path[0]) {
        const char *b = strrchr(gm_last_path, '/'); b = b ? b + 1 : gm_last_path;
        snprintf(gm_last_name, sizeof(gm_last_name), "%s", b);
        char *e = strrchr(gm_last_name, '.'); if (e) *e = 0;
    }
    d = (char *)read_file(gp_path("jeux_stats.txt"), 64 * 1024, &n);
    if (!d) return;
    u32 best = 0;
    for (long i = 0; i < n; ) {
        long st = i;
        while (i < n && d[i] != '\n') i++;
        char l[96]; long ln = i - st < 95 ? i - st : 95; memcpy(l, d + st, ln); l[ln] = 0; i++;
        char id[8]; int disc; unsigned runs, last, play = 0;
        if (sscanf(l, "%6[^|]|%d|%u|%u|%u", id, &disc, &runs, &last, &play) >= 4) {
            gm_play_total += play;
            if (last >= best) { best = last; gm_play_last = play; }
        }
    }
    free(d);
}
static void gm_hms(char *o, size_t cap, u32 s) {
    if (s >= 3600) snprintf(o, cap, "%u h %02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
    else snprintf(o, cap, "%u min", (unsigned)(s / 60));
}
static int gm_hub_input(void);                         /* v84 : stick C : jeux GameCube / applis / cartes memoire (tools.c) */
static f32 gm_hub_bar(f32 x, f32 y);
static void draw_gclib(void);
static void games_tab_input(u32 down, u32 nav, int fire) {
    if (gl_on) { gclib_input(down, nav, fire); return; }
    if (gm_sel >= GM_COUNT) {                              /* barres jeux GameCube / applis / cartes memoire (v41, v43) */
        int k = gm_sel - GM_COUNT;
        /* v196 : deux barres (jeux GameCube, cartes memoire) : les applis sont dans l'espace jeux */
        int col = k == 0 ? 0 : GM_COLS - 1;
        if (fire && (nav & PAD_BUTTON_DOWN)) gm_sel = col < GM_COUNT ? col : 0;
        if (fire && (nav & PAD_BUTTON_UP)) gm_sel = (GM_COUNT - 1) / GM_COLS * GM_COLS + col < GM_COUNT ? (GM_COUNT - 1) / GM_COLS * GM_COLS + col : GM_COUNT - 1;
        if (fire && (nav & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT))) gm_sel = GM_COUNT + (k ? 0 : 1);
        if (down & PAD_BUTTON_A) { if (k == 0) gclib_open(); else mc_open(); }
    } else {
        if (fire && (nav & PAD_BUTTON_RIGHT)) gm_sel = (gm_sel + 1) % GM_COUNT;
        if (fire && (nav & PAD_BUTTON_LEFT)) gm_sel = (gm_sel + GM_COUNT - 1) % GM_COUNT;
        if (fire && (nav & PAD_BUTTON_DOWN)) gm_sel = gm_sel + GM_COLS >= GM_COUNT ? GM_COUNT + gm_sel % GM_COLS : gm_sel + GM_COLS;
        if (fire && (nav & PAD_BUTTON_UP)) gm_sel = gm_sel < GM_COLS ? GM_COUNT + (gm_sel % GM_COLS == GM_COLS - 1 ? 1 : 0) : gm_sel - GM_COLS;   /* v196 : (deux barres) */
        if (down & PAD_BUTTON_A) games_tab_launch(gm_sel);
    }
    if ((down & PAD_BUTTON_X) && gm_last_path[0]) gclib_open_recent(gm_last_path);   /* v176 : relancer le dernier jeu */
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
    case GM_DUEL: {                                                /* v235 : signal rouge / vert */
        int go = ((int)(t * 1.2f)) % 3 == 2;
        gx_circle(cx, cy, 17, go ? RGBA(120, 255, 160, 255) : RGBA(220, 60, 80, 255), go ? RGBA(30, 170, 80, 255) : RGBA(90, 15, 30, 255));
        if (go) fun_shadow_text(F_SMALL, cx, cy + 5, "!", 1.4f, RGBA(255, 255, 255, 255));
        break; }
    case GM_RACE:
        for (int i = 0; i < 3; i++) { f32 p = fmodf(t * (0.5f + i * 0.13f), 1.0f); gx_circle(cx - 28 + p * 56, cy - 12 + i * 12, 5, qz_pcol[i], qz_pcol[i]); }
        for (int k = 0; k < 4; k++) gx_rect(cx + 30 + (k & 1) * 3, cy - 18 + k * 9, 3, 9, w);
        break;
    case GM_BUBBLES:
        for (int i = 0; i < 4; i++) { f32 y = cy + 14 - fmodf(t * 18 + i * 11, 36); gx_circle(cx - 20 + i * 13, y, 5 + (i & 1) * 2, RGBA(150, 220, 255, 90), RGBA(150, 220, 255, 220)); }
        gx_rrect_stroke(cx + 6, cy - 8, 16, 16, 8, 1.5f, RGBA(255, 215, 90, 255));
        break;
    case GM_TRON:                                                  /* traces de motos */
        gx_rect(cx - 24, cy + 6, 26, 4, qz_pcol[0]); gx_rect(cx, cy - 12, 4, 22, qz_pcol[0]);
        gx_rect(cx + 2, cy - 14, 22, 4, qz_pcol[1]); gx_circle(cx + 26, cy - 12, 4, w, w);
        break;
    case GM_HOCKEY:
        gx_circle(cx - 14, cy, 10, qz_pcol[0], RGBA(60, 90, 130, 255)); gx_circle(cx + 14, cy, 10, qz_pcol[1], RGBA(130, 60, 70, 255));
        gx_circle(cx + sinf(t * 3) * 6, cy + 10, 5, RGBA(40, 40, 50, 255), RGBA(10, 10, 16, 255));
        break;
    case GM_P4:
        gx_rrect_fill(cx - 22, cy - 16, 44, 32, 6, RGBA(40, 70, 210, 255), RGBA(22, 40, 140, 255));
        for (int i = 0; i < 6; i++) gx_circle(cx - 13 + (i % 3) * 13, cy - 7 + (i / 3) * 14, 5, i % 2 ? qz_pcol[3] : qz_pcol[1], RGBA(20, 20, 40, 255));
        break;
    case GM_SUMO:
        gx_circle(cx, cy, 20, RGBA(232, 206, 156, 255), RGBA(190, 150, 100, 255));
        gx_circle(cx - 7, cy, 7, qz_pcol[0], qz_pcol[0]); gx_circle(cx + 7, cy, 7, qz_pcol[1], qz_pcol[1]);
        break;
    default:
        for (int i = 0; i < 4; i++) { int on = ((int)(t * 2)) % 4 == i; gx_circle(cx + (i % 2 ? 11 : -11), cy + (i < 2 ? 9 : -9), 9, on ? lane_top[i] : lane_bot[i], lane_bot[i]); }
        break;
    }
}
/* v110 : Aurore : carte en verre ; la choisie : la selection en degrade (qui glisse) */
static void gm_au_card(f32 x, f32 y, f32 w, f32 h, f32 r, int on) {
    nv_raw = 1;
    gx_rrect_fill(x, y, w, h, r, RGBA(255, 255, 255, 12), RGBA(255, 255, 255, 5));
    gx_rrect_stroke(x, y, w, h, r, 1.0f, RGBA(255, 255, 255, 24));
    nv_raw = 0;
    if (on) gx_rrect_fill(x, y, w, h, r, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
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
        gm_recent_load();                                       /* v176 */
        sc_check();                                             /* v177 : nouveaux succes */
    }
    f32 t = (f32)(now_ms() % 100000) / 1000.0f;
    for (int k = 0; k < 3; k += 2) {   /* v25 : jeux GameCube ; v43 : + cartes memoire ; v196 : les applis sont dans l'espace jeux */
        static const char *const bl[3] = { "Jeux GameCube", "Mes applis", "Cartes m\xC3\xA9moire" };
        int on = gm_sel == GM_COUNT + (k ? 1 : 0);
        const f32 bw = 298, bx = 22 + (k ? 1 : 0) * (bw + 4), by = 110, bh = 30;
        if (ui_skin >= 3) gm_au_card(bx, by, bw, bh, 15, on);          /* v110 : Aurore : verre */
        else {
        if (on) gx_glow(bx, by, bw, bh, 8, RGBA(150, 80, 255, 140), 3);
        gx_rrect_fill(bx, by, bw, bh, 8, on ? RGBA(116, 46, 236, 255) : RGBA(34, 20, 84, 230), on ? RGBA(74, 22, 172, 255) : RGBA(20, 12, 52, 230));
        gx_rrect_stroke(bx, by, bw, bh, 8, 1.2f, on ? RGBA(200, 150, 255, 255) : RGBA(90, 64, 170, 255));
        }
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
        fit_text_sel(on, F_MED, TR(bl[k]), (int)bw - 72, lb, sizeof(lb));
        gx_text1(F_MED, bx + 40, by + 21, lb, RGBA(255, 255, 255, 255));
        gx_text_r(F_SMALL, bx + bw - 10, by + 20, "(A)", on ? RGBA(235, 225, 255, 255) : RGBA(170, 155, 225, 255));
        gp_zone(bx, by, bw, bh, &gm_sel, GM_COUNT + (k ? 1 : 0), GPZ_SEL);
    }
    const int rows = (GM_COUNT + GM_COLS - 1) / GM_COLS;
    const f32 cw = (596.0f - (GM_COLS - 1) * 8) / GM_COLS, ch = rows > 3 ? 54 : 72, x0 = 22, y0 = 148;      /* (v235 : 4 rangees : cartes plus basses) */
    for (int g = 0; g < GM_COUNT; g++) {
        f32 x = x0 + (g % GM_COLS) * (cw + 8), y = y0 + (g / GM_COLS) * (ch + 5);
        int on = g == gm_sel;
        if (ui_skin >= 3) gm_au_card(x, y, cw, ch, 16, on);           /* v110 : Aurore : verre */
        else {
        if (on) gx_glow(x, y, cw, ch, 10, RGBA(150, 80, 255, 140), 3);
        gx_rrect_fill(x, y, cw, ch, 10, on ? RGBA(116, 46, 236, 255) : RGBA(34, 20, 84, 230), on ? RGBA(74, 22, 172, 255) : RGBA(20, 12, 52, 230));
        gx_rrect_stroke(x, y, cw, ch, 10, 1.2f, on ? RGBA(200, 150, 255, 255) : RGBA(90, 64, 170, 255));
        }
        int cmp = ch < 60;
        const f32 ix = GM_COLS > 3 ? 30 : 42, tx = GM_COLS > 3 ? 62 : 82;   /* (v279 : 4 colonnes : icone et texte plus serres) */
        gm_icon(g, x + ix, y + ch / 2, t);
        fit_text_sel(on, F_MED, gm_names[g], (int)(cw - tx - 6), buf, sizeof(buf));
        gx_text1(F_MED, x + tx, y + (cmp ? 21 : 26), buf, RGBA(255, 255, 255, 255));
        char rec[32] = "";
        if (gm_record[g] > 0) {
            if (g == GM_DUEL) snprintf(rec, sizeof(rec), "%d ms", 2000 - gm_record[g]);         /* (v235 : meilleur reflexe) */
            else if (g == GM_RACE) { int ms = 60000 - gm_record[g]; snprintf(rec, sizeof(rec), "%d,%d s", ms / 1000, ms % 1000 / 100); }
            else snprintf(rec, sizeof(rec), "Record %d", gm_record[g]);
        }
        if (cmp && rec[0]) {                                    /* (compact : description puis record, sur une ligne) */
            snprintf(buf, sizeof(buf), "%s", rec);
            gx_text1(F_SMALL, x + tx, y + 40, buf, RGBA(255, 210, 120, 255));
        } else {
            fit_text_sel(on, F_SMALL, gm_desc[g], (int)(cw - tx - 6), buf, sizeof(buf));
            gx_text1(F_SMALL, x + tx, y + (cmp ? 40 : 44), buf, on ? RGBA(235, 225, 255, 255) : RGBA(170, 155, 225, 255));
            if (rec[0]) gx_text1(F_SMALL, x + tx, y + 62, rec, RGBA(255, 210, 120, 255));
        }
        gp_zone(x, y, cw, ch, &gm_sel, g, GPZ_SEL);
    }
    if (gm_last_path[0]) {                                      /* v176 : dernier jeu, temps de jeu */
        char a[24], b[24], l2[160], f2[160];
        gm_hms(a, sizeof(a), gm_play_last); gm_hms(b, sizeof(b), gm_play_total);
        snprintf(l2, sizeof(l2), "Dernier jeu : %s (%s)     Temps de jeu total : %s     X : le relancer", gm_last_name, a, b);
        fit_text(F_SMALL, l2, 590, f2, sizeof(f2));
        gx_text_c(F_SMALL, 320, 393, f2, RGBA(255, 210, 120, 255));
    }
    gx_text_c(F_SMALL, 320, gm_last_path[0] ? 407 : 402, "Croix ou curseur : choisir     A : jouer     Y : pause de la musique     B : arr\xC3\xAAter la musique", RGBA(200, 190, 245, 230));
}

/* ======================= Onglet TELECHARGEMENTS ======================= */
/* Ce qui arrive sur la carte SD : telechargements de l'onglet WEB, episodes de
   podcasts, fichiers envoyes depuis le telephone. Historique dans
   sd:/gc-mp3/bibliotheque/telechargements.txt (date | origine | chemin | taille). */
#define DLH_FILE GCD_BIB "/telechargements.txt"
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
static char nt_id[8];                                      /* (notice.c) */
/* v297 : l'archive .zip contient-elle des fichiers sous ce dossier (ex. "notices/") ? */
static int zip_has_dir(const char *zpath, const char *pre, char ids[][8], int *nid) {   /* (v299 : ids : les <ID> de notices/<ID>/) */
    FILE *f = fopen(zpath, "rb");
    if (!f) return 0;
    long fsz = 0;
    if (fseek(f, 0, SEEK_END) == 0) fsz = ftell(f);
    long tail = fsz < 65557 ? fsz : 65557;
    u8 *t = tail > 22 ? (u8 *)malloc(tail) : NULL;
    int r = 0;
    if (t && fseek(f, fsz - tail, SEEK_SET) == 0 && fread(t, 1, tail, f) == (size_t)tail) {
        long e = -1;
        for (long i = tail - 22; i >= 0; i--) if (rz_le32(t + i) == 0x06054b50u) { e = i; break; }
        if (e >= 0) {
            u32 cnt = rz_le16(t + e + 10), cds = rz_le32(t + e + 12), cdo = rz_le32(t + e + 16);
            u8 *cd = cds && cds <= (1u << 20) && (u64)cdo + cds <= (u64)fsz ? (u8 *)malloc(cds) : NULL;
            if (cd && fseek(f, cdo, SEEK_SET) == 0 && fread(cd, 1, cds, f) == cds) {
                size_t pl = strlen(pre);
                for (u32 n = 0, q = 0; n < cnt && q + 46 <= cds && (!r || ids); n++) {
                    if (rz_le32(cd + q) != 0x02014b50u) break;
                    u16 nl = rz_le16(cd + q + 28), el = rz_le16(cd + q + 30), cl = rz_le16(cd + q + 32);
                    const char *nm = (const char *)cd + q + 46;
                    if (q + 46 + nl <= cds && nl > pl && !strncasecmp(nm, pre, pl)) {
                        r = 1;
                        if (ids && nid && nl >= pl + 7 && nm[pl + 6] == '/' && *nid < 8) {
                            int dej = 0;
                            for (int k = 0; k < *nid; k++) if (!strncmp(ids[k], nm + pl, 6)) dej = 1;
                            if (!dej) { memcpy(ids[*nid], nm + pl, 6); ids[*nid][6] = 0; (*nid)++; }
                        }
                    }
                    q += 46 + nl + el + cl;
                }
            }
            free(cd);
        }
    }
    free(t);
    fclose(f);
    return r;
}
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
    const char *dot = strrchr(p, '.');
    char nids[8][8]; int nn = 0;
    if (dot && !strcasecmp(dot, ".zip") && zip_has_dir(p, "notices/", nids, &nn)) {   /* v297 : notices de jeux : dans SD:/gc-mp3/notices/ */
        lib_note("D\xC3\xA9" "compression de la notice...");
        for (int i = 0; i < nn; i++) for (int k = 1; k <= 99; k++) {   /* v299 : l'ancienne notice de ce jeu est remplacee (pas de pages en trop) */
            char op[NAME_LEN]; snprintf(op, sizeof(op), GCD "/notices/%s/%02d.jpg", nids[i], k); remove(op);
        }
        int sk = 0, n = zip_extract(p, GCD, 0, &sk);
        char m[200];
        if (n < 0) snprintf(m, sizeof(m), "Archive illisible (pas un .zip complet ?)");
        else snprintf(m, sizeof(m), "Notice install\xC3\xA9" "e : %d page(s) dans SD:/gc-mp3/notices/", n);
        lib_note(m);
        nt_id[0] = 0;                                          /* (la fiche ouverte : recompter les pages) */
        return;
    }
    if (dot && !strcasecmp(dot, ".zip")) {                    /* v201 : archive : decompressee a cote, dans un dossier */
        char dir[NAME_LEN];
        int zl = (int)(dot - p);
        if (zl <= 0 || zl >= (int)sizeof(dir) - 8) return;
        memcpy(dir, p, zl); dir[zl] = 0;
        for (int k = 2; k < 50 && stat(dir, &st) == 0; k++) snprintf(dir + zl, sizeof(dir) - zl, " (%d)", k);
        lib_note("D\xC3\xA9" "compression...");
        int sk = 0, n = zip_extract(p, dir, 0, &sk);
        char m[300];
        if (n < 0) snprintf(m, sizeof(m), "Archive illisible (pas un .zip complet ?)");
        else if (sk) snprintf(m, sizeof(m), "D\xC3\xA9" "compress\xC3\xA9 : %d fichier(s) dans %s (%d trop gros ou illisibles)", n, base_of(dir), sk);
        else snprintf(m, sizeof(m), "D\xC3\xA9" "compress\xC3\xA9 : %d fichier(s) dans SD:/downloads/%s", n, base_of(dir));
        lib_note(m);
        if (n > 0) music_rescan();
        return;
    }
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
            fit_text_sel(on, F_MED, d, 330, buf, sizeof(buf));
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
                                           : "A : \xC3\xA9" "couter / voir / d\xC3\xA9" "compresser (.zip)     X X : effacer     Y : onglet WEB", RGBA(200, 190, 245, 230));
}

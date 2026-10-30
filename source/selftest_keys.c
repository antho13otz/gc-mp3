// GC Station v278 : TEST DES TOUCHES (inclus dans main.c, après selftest.c).
// START > Réglages > Outils > Test de la console > Z (ou depuis l'appli du téléphone) : GC Station se manipule tout
// seul pendant 3 heures au plus. Chaque écran (chaque onglet, le menu START et chacune de ses sections, chaque outil,
// la table de cartes, les 6 affichages des jeux, chaque filtre de la vitrine, la fiche d'un jeu, ses cheats, ses
// réglages, ses musiques, les photos, la vidéo, le clavier, chaque mini-jeu, le mode soirée, l'économiseur) reçoit
// chaque touche : A B X Y Z L R START, la croix, le stick C dans les 4 sens, A et B maintenus. Quand une touche ouvre
// autre chose, chaque touche y est essayée aussi (2 niveaux). Le temps qui reste : touches au hasard (« singe »).
// Pendant le test, rien de ce qui abîme ou quitte ne part (sx_safe, main.c) : écrire / effacer / renommer un fichier
// (sauf les journaux), écrire sur une carte mémoire, lancer un jeu ou une appli, analyser un jeu, mettre à jour,
// redémarrer ; ces tentatives sont comptées. Les réglages d'avant sont remis à la fin.
// Chaque action : notée dans le journal de plantage avant d'être faite (si la console plante ou se fige, le journal
// dit sur quoi) ; sa pire image ; ce qui n'a pas pu être refermé par B. Rapport : test-console.txt (et le téléphone).

static u32 kt_limit = 3u * 3600u * 1000u;           /* (3 h au plus) */
#define KT_LIMIT_MS kt_limit
#define KT_EXH_MS (kt_limit - kt_limit / 12u)        /* (les 15 dernieres minutes : plus de 2e niveau) */
#define KT_MAXSC 160
#define KT_NOTES 40
enum { KS_TAB, KS_MENU, KS_TOOL, KS_CARTES, KS_GLVIEW, KS_GSF, KS_FICHE, KS_CHEATS, KS_GCFG, KS_JUKE, KS_PHOTO, KS_VIDEO, KS_KB,
       KS_GAME, KS_PARTY, KS_SAVER };
typedef struct { char nm[60]; u8 kind; s16 a; u32 acts, slow, worst_us, forced, blocked; s32 dmem; } kt_sc_t;
static const struct { const char *nm; u32 btn; s8 cx, cy; u16 hold; } kt_keys[] = {
    { "A", PAD_BUTTON_A, 0, 0, 0 }, { "B", PAD_BUTTON_B, 0, 0, 0 }, { "X", PAD_BUTTON_X, 0, 0, 0 }, { "Y", PAD_BUTTON_Y, 0, 0, 0 },
    { "Z", PAD_TRIGGER_Z, 0, 0, 0 }, { "L", PAD_TRIGGER_L, 0, 0, 0 }, { "R", PAD_TRIGGER_R, 0, 0, 0 }, { "START", PAD_BUTTON_START, 0, 0, 0 },
    { "haut", PAD_BUTTON_UP, 0, 0, 0 }, { "bas", PAD_BUTTON_DOWN, 0, 0, 0 }, { "gauche", PAD_BUTTON_LEFT, 0, 0, 0 }, { "droite", PAD_BUTTON_RIGHT, 0, 0, 0 },
    { "stick C gauche", 0, -100, 0, 250 }, { "stick C droite", 0, 100, 0, 250 }, { "stick C haut", 0, 0, 100, 250 }, { "stick C bas", 0, 0, -100, 250 },
    { "A maintenu", PAD_BUTTON_A, 0, 0, 1300 }, { "B maintenu", PAD_BUTTON_B, 0, 0, 1300 } };
#define KT_NK ((int)(sizeof(kt_keys) / sizeof(kt_keys[0])))
#define KT_NK2 12                                    /* (2e niveau : les touches et la croix) */

static int kt_on = 0, kt_done = 0, kt_aborted = 0;
static kt_sc_t *kt_sc = NULL;
static int kt_nsc = 0, kt_si = 0, kt_k1 = 0, kt_k2 = -1, kt_ph = 0, kt_rb = 0, kt_l2n = 0, kt_l2 = 0;
static u64 kt_t = 0, kt_t0 = 0, kt_hold_end = 0;
static u32 kt_sig0 = 0, kt_sig1 = 0, kt_acts = 0, kt_forced = 0, kt_blk0 = 0, kt_aworst = 0, kt_monkey = 0, kt_mem0 = 0, kt_mem1 = 0;
static u32 kt_slow_tot = 0, kt_frames = 0;
typedef struct { char t[120]; u32 ms; } kt_note_t;
static kt_note_t kt_slow[KT_NOTES], kt_stuck[KT_NOTES], kt_blk[KT_NOTES];
static int kt_nslow = 0, kt_nstuck = 0, kt_nblk = 0;
static char kt_cur[120] = "";

static void kt_note(kt_note_t *a, int *n, u32 ms, const char *t) {
    int i = *n < KT_NOTES ? (*n)++ : -1;
    if (i < 0) {                                     /* (plein : on garde les plus longues) */
        int m = 0;
        for (int k = 1; k < KT_NOTES; k++) if (a[k].ms < a[m].ms) m = k;
        if (a[m].ms >= ms) return;
        i = m;
    }
    snprintf(a[i].t, sizeof(a[i].t), "%s", t); a[i].ms = ms;
}
static void kt_add(int kind, int a, const char *fmt, ...) {
    if (!kt_sc || kt_nsc >= KT_MAXSC) return;
    kt_sc_t *s = &kt_sc[kt_nsc++];
    memset(s, 0, sizeof(*s));
    va_list ap; va_start(ap, fmt); vsnprintf(s->nm, sizeof(s->nm), fmt, ap); va_end(ap);
    s->kind = (u8)kind; s->a = (s16)a;
}
static void kt_build(void) {
    free(kt_sc); kt_sc = (kt_sc_t *)calloc(KT_MAXSC, sizeof(kt_sc_t)); kt_nsc = 0;
    FOR_TABS(t) if (tab_shown(t)) kt_add(KS_TAB, t, "Onglet %s", tab_names[t]);
    kt_add(KS_MENU, -1, "Menu START");
    for (int s = 0; s < SEC_TOOLS + 1; s++) if (sec_n(s) > 0) kt_add(KS_MENU, s, "Menu START : section %d", s + 1);
    for (int i = 0; i < SX_NTOOLS; i++) kt_add(KS_TOOL, i, "Outil : %s", sx_tools[i].nm);
    kt_add(KS_CARTES, 0, "Jeu de cartes (télé)");
    for (int v = 0; v < GLV_COUNT; v++) kt_add(KS_GLVIEW, v, "Jeux : affichage %s", gl_view_names[v]);
    for (int f = 0; f < GSF_N; f++) kt_add(KS_GSF, f, "Vitrine : filtre %s", gs_filter_names[f]);
    kt_add(KS_FICHE, 0, "Fiche d'un jeu");
    kt_add(KS_CHEATS, 0, "Cheats d'un jeu");
    kt_add(KS_GCFG, 0, "Réglages d'un jeu");
    kt_add(KS_JUKE, 0, "Musiques d'un jeu");
    kt_add(KS_PHOTO, 0, "Photo en grand");
    kt_add(KS_VIDEO, 0, "Vidéo");
    kt_add(KS_KB, 0, "Clavier à l'écran");
    for (int g = 0; g < GM_COUNT; g++) kt_add(KS_GAME, g, "Mini-jeu : %s", gm_names[g]);
    kt_add(KS_PARTY, 0, "Mode soirée");
    kt_add(KS_SAVER, 0, "Économiseur d'écran");
}

/* ce qui est à l'écran (pour savoir si une touche a ouvert quelque chose, et si B l'a refermé) */
static u32 kt_sig(void) {
    u32 h = 2166136261u;
#define KH(v) (h = (h ^ (u32)(v)) * 16777619u)
    KH(tab); KH(tl_screen); KH(menu_open); KH(menu_open ? menu_sec : 0); KH(kb_open); KH(ic_on); KH(sh_on); KH(am_open); KH(fun_mode);
    KH(night_on); KH(party_on); KH(saver_on); KH(gl_on); KH(gl_detail); KH(gl_launch_sel >= 0); KH(gs_app_confirm >= 0); KH(pv_mode); KH(ck != 0);
#undef KH
    return h;
}
/* tout refermer (après les B : ce qui est resté ouvert est fermé de force) */
static void kt_force_home(void) {
    if (kb_open) kb_open = 0;
    if (fun_mode) fun_leave();
    if (pv_mode == 2) video_stop();
    pv_mode = 0; pv_slide = 0;
    ic_on = 0; sh_on = 0; am_open = 0; night_on = 0;
    gl_detail = 0; gl_launch_sel = -1; gs_app_confirm = -1;
    sx_reset_ui();
    sx_fsx = sx_fsy = sx_fmx = sx_fmy = 0; sx_hold = 0;
}
static int kt_game(void) {                           /* (un jeu de la liste pour la fiche, ses cheats...) */
    if (!gl_scanned) gl_scan_full(0);
    for (int k = 0; k < gl_n; k++) if (!gl[k].dvd) return k;
    return -1;
}
/* ouvre l'écran ; 0 = pas disponible ici */
static int kt_enter(const kt_sc_t *s) {
    kt_force_home();
    switch (s->kind) {
    case KS_TAB: tab = s->a; tab_enter(tab); return 1;
    case KS_MENU: menu_open = 1; if (s->a >= 0) { menu_sec = s->a; menu_sel = sec_items[s->a][0]; } return 1;
    case KS_TOOL: sx_tools[s->a].fn(); return tl_screen != TL_NONE;
    case KS_CARTES: tl_screen = TL_CARTES; return 1;
    case KS_GLVIEW: tab = TAB_GAMES; tab_enter(tab); gl_view = s->a; gclib_open(); gs_dirty = 1; return gl_on;
    case KS_GSF: tab = TAB_GAMES; tab_enter(tab); gl_view = GLV_GRID; gs_filter = s->a; gclib_open(); gs_dirty = 1; return gl_on && gs_view();
    case KS_FICHE: case KS_CHEATS: case KS_GCFG: case KS_JUKE: {
        int k = kt_game();
        if (k < 0) return 0;
        tab = TAB_GAMES; tab_enter(tab); gclib_open(); gl_sel = k;
        if (s->kind == KS_FICHE) gl_detail = 1;
        else if (s->kind == KS_CHEATS) gl_open_cheats(k);
        else if (s->kind == KS_GCFG) indigo_game_open(gl[k].id, gl[k].title);
        else jb_open_game(k);
        return 1;
    }
    case KS_PHOTO: case KS_VIDEO: {
        if (!tab_shown(TAB_PHOTO)) return 0;
        tab = TAB_PHOTO; tab_enter(tab);
        if (npf < 0) photo_rescan();
        int i = sx_photo_idx(0, s->kind == KS_VIDEO);
        if (i < 0) return 0;
        pf_sel = i; pv_open(i);
        return pv_mode != 0;
    }
    case KS_KB: kb_start(KB_LIBFIND, "", -1, -1); return kb_open;
    case KS_GAME:
        tab = TAB_GAMES; tab_enter(tab);
        if (s->a == GM_QUIZ && fun_tracks() < 4) return 0;
        games_tab_launch(s->a);
        return fun_mode != FUN_OFF;
    case KS_PARTY: party_start(); return 1;
    case KS_SAVER: saver_on = 1; return 1;
    }
    return 0;
}
static void kt_press(int k) {
    if (kt_keys[k].cx || kt_keys[k].cy) { sx_fsx = kt_keys[k].cx; sx_fsy = kt_keys[k].cy; }
    if (kt_keys[k].btn) { tp_keys |= kt_keys[k].btn; if (kt_keys[k].hold) sx_hold = kt_keys[k].btn; }
    kt_hold_end = now_ms() + (kt_keys[k].hold ? kt_keys[k].hold : 60);
}
static void kt_release(void) { sx_fsx = sx_fsy = 0; sx_hold = 0; }
static void kt_desc(char *o, int cap) {
    const kt_sc_t *s = &kt_sc[kt_si];
    if (kt_monkey) snprintf(o, cap, "Au hasard : %s", s->nm);
    else if (kt_k2 < 0) snprintf(o, cap, "%s : %s", s->nm, kt_keys[kt_k1].nm);
    else snprintf(o, cap, "%s : %s puis %s", s->nm, kt_keys[kt_k1].nm, kt_keys[kt_k2].nm);
}

static void kt_finish(void);
static void kt_begin(void) {
    kt_build();
    kt_limit = 3u * 3600u * 1000u;
#ifndef GEKKO
    if (getenv("PCSIM_KT_MIN")) kt_limit = (u32)atoi(getenv("PCSIM_KT_MIN")) * 60000u;   /* (simulateur : test plus court) */
#endif
    free(sx_cfg0); sx_cfg0 = NULL; sx_cfg0_n = 0; sx_cfg_nd = 0;
    if (!cfgw_build(&sx_cfg0, &sx_cfg0_n)) { sx_cfg0 = NULL; sx_cfg0_n = 0; }   /* (reglages d'avant : remis a la fin) */
    sx_quiet = 1; sx_safe = 1; sx_blocked = 0; sx_blk_last[0] = 0;
    sx_tab0 = tab;
    kt_on = 1; kt_done = 0; kt_aborted = 0;
    kt_si = 0; kt_k1 = 0; kt_k2 = -1; kt_ph = 0; kt_l2n = 0; kt_l2 = 0; kt_monkey = 0;
    kt_acts = kt_forced = 0; kt_nslow = kt_nstuck = kt_nblk = 0; kt_slow_tot = kt_frames = 0;
    kt_t0 = kt_t = now_ms();
    kt_mem0 = sx_free_now();
    perf_on = 1; sx_perf_reset();
    crumb("test des touches : debut (%d ecrans)", kt_nsc);
}
/* (perf_frame -> sx_frame_hook -> ici) chaque image pendant le test */
static void kt_frame(u32 tot) {
    if (!kt_on) return;
    kt_frames++;
    if (tot > VID_SLOW_US) kt_slow_tot++;
    if (tot > kt_aworst) kt_aworst = tot;
    if (kt_sc && kt_si < kt_nsc) { kt_sc_t *s = &kt_sc[kt_si]; if (tot > VID_SLOW_US) s->slow++; if (tot > s->worst_us) s->worst_us = tot; }
}
/* fin d'une action : pire image, blocages, fermeture */
static void kt_action_done(int forced) {
    kt_sc_t *s = &kt_sc[kt_si];
    s->acts++; kt_acts++;
    if (kt_aworst >= 150000u) { char b[150]; snprintf(b, sizeof(b), "%s : %u ms", kt_cur, (unsigned)(kt_aworst / 1000u)); kt_note(kt_slow, &kt_nslow, kt_aworst / 1000u, b); }
    if (forced) { s->forced++; kt_forced++; kt_note(kt_stuck, &kt_nstuck, 1, kt_cur); }
    if (sx_blocked != kt_blk0) { char b[150]; s->blocked += sx_blocked - kt_blk0; snprintf(b, sizeof(b), "%s : %s", kt_cur, sx_blk_last); kt_note(kt_blk, &kt_nblk, 1, b); }
}
/* l'action suivante (rend 0 si tout est fait) */
static int kt_next(void) {
    u64 el = now_ms() - kt_t0;
    if (el >= KT_LIMIT_MS) return 0;                 /* (temps ecoule : fin, meme si tout n'a pas ete vu) */
    if (kt_monkey) return 1;
    if (kt_l2) {                                      /* 2e niveau en cours apres kt_k1 */
        if (++kt_k2 < KT_NK2) return 1;
        kt_l2 = 0; kt_k2 = -1;
    }
    if (++kt_k1 < KT_NK) return 1;
    /* ecran fini */
    kt_sc[kt_si].dmem = (s32)kt_mem1 - (s32)sx_free_now();
    kt_k1 = 0; kt_k2 = -1; kt_l2n = 0;
    if (++kt_si < kt_nsc) return 1;
    if (el + 60000u < KT_LIMIT_MS) { kt_monkey = 1; kt_si = 0; return 1; }   /* (le temps qui reste : au hasard) */
    return 0;
}
static void kt_tick(void) {
    if (!kt_on) return;
    u64 n = now_ms();
    last_input_ms = n;                                /* (pas d'economiseur pendant le test, sauf celui qu'on essaie) */
    if (PAD_ButtonsDown(0) & PAD_BUTTON_B) {         /* (vraie manette : arreter) */
        if (!(tp_keys & PAD_BUTTON_B) && !(sx_hold & PAD_BUTTON_B)) { kt_aborted = 1; kt_finish(); return; }
    }
    if (n - kt_t0 >= KT_LIMIT_MS + 120000u) { kt_finish(); return; }
    if (kt_hold_end && n >= kt_hold_end) { kt_release(); kt_hold_end = 0; }
    if (n < kt_t) return;
    kt_sc_t *s = &kt_sc[kt_si];
    switch (kt_ph) {
    case 0:                                           /* ouvrir l'ecran */
        if (kt_k1 == 0 && kt_k2 < 0 && !kt_monkey) kt_mem1 = sx_free_now();
        if (!kt_enter(s)) {                           /* (pas disponible : ecran saute) */
            if (!kt_monkey) { snprintf(s->nm + strlen(s->nm), sizeof(s->nm) - strlen(s->nm), " (absent)"); kt_k1 = KT_NK - 1; kt_l2 = 0; }
            kt_force_home();
            if (!kt_next()) { kt_finish(); return; }
            if (kt_monkey) kt_si = (int)(c8_rand() % (u32)kt_nsc);
            kt_t = n + 50; return;
        }
        kt_sig0 = kt_sig(); kt_aworst = 0; kt_blk0 = sx_blocked;
        kt_ph = 1; kt_t = n + 400; return;
    case 1:                                           /* 1re touche (ou suite au hasard) */
        if (kt_monkey) {
            snprintf(kt_cur, sizeof(kt_cur), "Au hasard : %s", s->nm);
            crumb("test des touches : %s", kt_cur);
            kt_rb = 0; kt_ph = 5; kt_t = n; return;
        }
        kt_desc(kt_cur, sizeof(kt_cur));
        crumb("test des touches : %s", kt_cur);        /* (si la console plante ou se fige : on sait sur quoi) */
        kt_press(kt_k1);
        kt_ph = kt_k2 >= 0 ? 2 : 3; kt_t = n + (kt_keys[kt_k1].hold ? kt_keys[kt_k1].hold + 300 : 380); return;
    case 2:                                           /* 2e touche */
        kt_release();
        kt_press(kt_k2);
        kt_ph = 3; kt_t = n + (kt_keys[kt_k2].hold ? kt_keys[kt_k2].hold + 300 : 380); return;
    case 3:                                           /* resultat : quelque chose s'est ouvert ? puis B pour refermer */
        kt_release();
        kt_sig1 = kt_sig();
        if (kt_k2 < 0 && kt_sig1 != kt_sig0 && kt_k1 < KT_NK2 && kt_l2n < 6 && n - kt_t0 < (u64)KT_EXH_MS * (u64)(kt_si + 1) / (u64)kt_nsc) kt_l2 = 2;   /* (au rythme : chaque ecran a sa part du temps) */   /* (a explorer ensuite) */
        kt_rb = 0; kt_ph = 4; kt_t = n; return;
    case 4: {                                         /* refermer : B jusqu'a 3 fois, sinon de force */
        u32 sg = kt_sig();
        if (sg != kt_sig0 && kt_rb < 3 && !(s->kind == KS_SAVER || s->kind == KS_PARTY)) { tp_keys |= PAD_BUTTON_B; kt_rb++; kt_t = n + 260; return; }
        int forced = sg != kt_sig0 && !(s->kind == KS_SAVER || s->kind == KS_PARTY || s->kind == KS_GAME);
        kt_action_done(forced);
        kt_force_home();
        if (kt_l2 == 2) { kt_l2 = 1; kt_l2n++; kt_k2 = -1; }
        if (!kt_next()) { kt_finish(); return; }
        if (kt_monkey) kt_si = (int)(c8_rand() % (u32)kt_nsc);
        kt_ph = 0; kt_t = n + 150; return;
    }
    case 5:                                           /* au hasard : 25 touches, une toutes les 0,12 a 0,4 s */
        kt_release();
        if (kt_rb++ < 25) {
            int k = (int)(c8_rand() % (u32)KT_NK);
            if (kt_keys[k].hold) k = 0;
            kt_press(k);
            kt_t = n + 120 + c8_rand() % 280; return;
        }
        kt_rb = 0; kt_ph = 4; kt_t = n + 200; return;
    }
}

static void kt_write_report(FILE *f) {
    u32 el = (u32)((now_ms() - kt_t0) / 1000u);
    fprintf(f, "GC Station %s - test des touches%s\n", GCMP3_VERSION, kt_aborted ? " (arrêté avec B)" : "");
    fprintf(f, "Durée : %u h %02u min ; %u actions ; %d écrans ; %u images (%u ratées)\n", el / 3600u, el / 60u % 60u, (unsigned)kt_acts, kt_nsc, (unsigned)kt_frames, (unsigned)kt_slow_tot);
    fprintf(f, "Mémoire libre : %d Ko de moins à la fin qu'au début\n", (int)(((s32)kt_mem0 - (s32)sx_free_now()) / 1024));
    fprintf(f, "Fermés de force (B ne suffisait pas) : %u ; actions dangereuses bloquées : %u\n", (unsigned)kt_forced, (unsigned)sx_blocked);
    if (sx_cfg_nd) { fprintf(f, "Réglages pas remis comme avant :\n"); for (int i = 0; i < sx_cfg_nd; i++) fprintf(f, "  %s\n", sx_cfg_diff[i]); }
    fprintf(f, "\n== Écrans (actions, images ratées, pire image, mémoire gardée, fermés de force, bloqués) ==\n");
    for (int i = 0; i < kt_nsc; i++) {
        const kt_sc_t *s = &kt_sc[i];
        if (!s->acts) { fprintf(f, "%-44s pas atteint (temps écoulé)\n", s->nm); continue; }
        fprintf(f, "%-44s %4u actions  %4u ratées  pire %5u ms  mémoire %+5d Ko  forcés %u  bloqués %u\n", s->nm, (unsigned)s->acts, (unsigned)s->slow,
                (unsigned)(s->worst_us / 1000u), (int)(s->dmem / 1024), (unsigned)s->forced, (unsigned)s->blocked);
    }
    fprintf(f, "\n== Les actions les plus lentes (une image de 150 ms et plus) ==\n");
    for (int i = 0; i < kt_nslow; i++) fprintf(f, "  %s\n", kt_slow[i].t);
    if (!kt_nslow) fprintf(f, "  aucune\n");
    fprintf(f, "\n== Ce que B ne refermait pas (fermé de force) ==\n");
    for (int i = 0; i < kt_nstuck; i++) fprintf(f, "  %s\n", kt_stuck[i].t);
    if (!kt_nstuck) fprintf(f, "  rien\n");
    fprintf(f, "\n== Actions dangereuses bloquées (rien n'a été fait) ==\n");
    for (int i = 0; i < kt_nblk; i++) fprintf(f, "  %s\n", kt_blk[i].t);
    if (!kt_nblk) fprintf(f, "  aucune\n");
}
static void kt_finish(void) {
    if (!kt_on) return;
    kt_release();
    kt_force_home();
    kt_on = 0; kt_done = 1;
    perf_on = 0;
    sx_safe = 0;                                      /* (avant d'ecrire le rapport et de remettre les reglages) */
    sx_cfg_restore(1);
    sx_quiet = 0;
    tab = sx_tab0; tab_enter(tab);
    crumb("test des touches : fin (%u actions)", (unsigned)kt_acts);
    if (sd_ok && !sd_virtual) {
        remove(SX_OLD); rename(SX_FILE, SX_OLD);
        FILE *f = fopen(SX_FILE, "w");
        if (f) { kt_write_report(f); fclose(f); }
    }
    tl_screen = TL_SELFTEST; menu_open = 0;
}
static void kt_overlay(void) {
    if (!kt_on) return;
    char b[200];
    u32 el = (u32)((now_ms() - kt_t0) / 60000u);
    if (kt_monkey) snprintf(b, sizeof(b), "Test des touches : au hasard (%u min / 180)  -  B : arrêter", (unsigned)el);
    else snprintf(b, sizeof(b), "Test des touches : %.70s (%d / %d, %u min)  -  B : arrêter", kt_cur, kt_si + 1, kt_nsc, (unsigned)el);
    f32 w = (f32)text_width(F_SMALL, b, 0) + 24;
    if (w > 630) w = 630;
    gx_rrect_fill(320 - w / 2, 440, w, 24, 8, RGBA(60, 10, 40, 235), RGBA(60, 10, 40, 235));
    gx_text_c(F_SMALL, 320, 457, b, RGBA(255, 230, 150, 255));
}
/* résumé à l'écran (Test de la console, après le test des touches) */
static void kt_draw_result(void) {
    char b[200];
    u32 el = (u32)((now_ms() - kt_t0) / 1000u);
    tl_frame("Test des touches", kt_aborted ? "arrêté" : "fini");
    snprintf(b, sizeof(b), "%u actions sur %d écrans, %u h %02u min", (unsigned)kt_acts, kt_nsc, el / 3600u, el / 60u % 60u);
    gx_text1(F_MED, 64, 110, b, RGBA(255, 255, 255, 255));
    snprintf(b, sizeof(b), "Images ratées : %u sur %u", (unsigned)kt_slow_tot, (unsigned)kt_frames);
    gx_text1(F_MED, 64, 140, b, SX_TXT);
    snprintf(b, sizeof(b), "Fermés de force (B ne suffisait pas) : %u", (unsigned)kt_forced);
    gx_text1(F_MED, 64, 166, b, kt_forced ? RGBA(255, 200, 120, 255) : SX_OK);
    snprintf(b, sizeof(b), "Actions dangereuses bloquées : %u (rien n'a été fait)", (unsigned)sx_blocked);
    gx_text1(F_MED, 64, 192, b, SX_TXT);
    gx_text1(F_MED, 64, 226, "Les plus lentes :", RGBA(200, 190, 240, 255));
    int best = -1;
    for (int i = 0; i < kt_nslow; i++) if (best < 0 || kt_slow[i].ms > kt_slow[best].ms) best = i;
    if (best >= 0) { char fv[200]; fit_text(F_SMALL, kt_slow[best].t, 520, fv, sizeof(fv)); gx_text1(F_SMALL, 74, 250, fv, RGBA(255, 210, 120, 255)); }
    if (kt_nstuck) {
        gx_text1(F_MED, 64, 286, "Pas refermé par B (exemple) :", RGBA(200, 190, 240, 255));
        char fv[200]; fit_text(F_SMALL, kt_stuck[0].t, 520, fv, sizeof(fv)); gx_text1(F_SMALL, 74, 310, fv, RGBA(255, 210, 120, 255));
    }
    if (!kt_nslow) gx_text1(F_SMALL, 74, 250, "aucune image de 150 ms et plus", SX_OK);
    gx_text1(F_SMALL, 64, 380, "Détail : SD:/gc-mp3/journaux/test-console.txt  -  téléphone : Outils > Test", RGBA(170, 155, 225, 255));
    static const char *const k[] = { "A", "B" }, *const lb[] = { "AUTRE TEST", "RETOUR" };
    tl_legend(k, lb, 2);
}

/* ======================= v278 : lancer un test depuis le téléphone ======================= */
/* (boucle) m : 0 rapide, 1 complet, 2 total, 3 touches */
static void sx_phone_start(int m) {
    if (kt_on || (sx_state >= SX_BENCH && sx_state < SX_DONE)) return;   /* (un test tourne deja) */
    saver_on = 0; kb_open = 0;
    sx_open();
    kt_done = 0;
    if (m == 3) kt_begin(); else sx_begin(m < 0 ? 0 : m > 2 ? 2 : m);
}
/* (fil du téléphone) où en est le test : JSON */
static int sx_phone_state(char *o, int cap) {
    char d[200] = "";
    int run = kt_on || (sx_state >= SX_BENCH && sx_state < SX_DONE);
    if (kt_on) {
        u32 el = (u32)((now_ms() - kt_t0) / 60000u);
        if (kt_monkey) snprintf(d, sizeof(d), "Test des touches : au hasard (%u min)", (unsigned)el);
        else snprintf(d, sizeof(d), "Test des touches : %s (%d / %d écrans, %u min)", kt_cur, kt_si + 1, kt_nsc, (unsigned)el);
    } else if (run) {
        static const char *const nm[] = { "", "", "mesures du matériel", "réseau", "formats audio", "radio", "réseau en vrai", "parcours des onglets", "test total : chaque élément", "stress" };
        snprintf(d, sizeof(d), "Test de la console : %s", sx_state < (int)(sizeof(nm) / sizeof(nm[0])) ? nm[sx_state] : "");
    } else if (kt_done) snprintf(d, sizeof(d), "Test des touches fini : %u actions, %u fermés de force, %u bloqués", (unsigned)kt_acts, (unsigned)kt_forced, (unsigned)sx_blocked);
    else if (sx_state == SX_DONE) snprintf(d, sizeof(d), "Test de la console fini (note %d / 100)", sx_score);
    int n = snprintf(o, cap, "{\"run\":%d,\"txt\":\"", run);
    for (const char *p = d; *p && n < cap - 4; p++) if (*p != '"' && *p != '\\') o[n++] = *p;
    n += snprintf(o + n, cap - n, "\"}");
    return n < cap ? n : cap - 1;
}

// GC-MP3 : onglets, liste a cocher, dispositions du lecteur.
// Fichier inclus dans main.c (meme unite de compilation).

/* ---------- Onglets ---------- */
static u64 tab_strip_until = 0;              /* bandeau des onglets affiche jusqu'a ... */


static void tab_enter(int t);
static void tab_next(int dir) {
    int k = 0, t = tab;
    for (int i = 0; i < TAB_COUNT; i++) if (tab_order[i] == tab) k = i;
    for (int i = 0; i < TAB_COUNT; i++) {                    /* (v43 : dans l'ordre choisi) */
        k = (k + dir + TAB_COUNT) % TAB_COUNT;
        t = tab_order[k];
        if (tab_shown(t)) break;
    }
    tab = t;
    tab_strip_until = now_ms() + 1800;
    tab_enter(t);
}

/* bandeau de tous les onglets, en haut de l'ecran, apres un appui sur Z */
/* v104 : le curseur en haut de l'ecran : tous les onglets s'affichent (icone + nom), dans le style choisi ;
   un clic (A) ouvre l'onglet. Le bandeau reste tant que le curseur est dessus, puis remonte. */
static u64 tm_seen = 0, tm_open_t = 0;
static int web_switch = -1;
static int tm_vis = 0;
static int tab_menu_draw(void) {
    int hot = gp_on && gp_y < (tm_vis ? 70 : 40) && !menu_open && !tl_screen && !ic_on;
    /* v218 : onglet Cube Browser : c'est le pointeur du navigateur qui fait descendre la barre */
    f32 wpx = -1, wpy = -1;
    const int web = tab == TAB_WEB && cb_host_input_enabled() && (cb_view() == CBV_HOME || cb_view() == CBV_PAGE);
    if (web) { float px, py; cb_pointer(&px, &py); wpx = px; wpy = py; hot = wpy < (tm_vis ? 70 : 40); }
    if (hot) { if (!tm_vis) tm_open_t = now_ms(); tm_seen = now_ms(); tm_vis = 1; }
    else if (tm_vis && now_ms() - tm_seen > 350) tm_vis = 0;
    if (!tm_vis) return 0;
    f32 p = gl_anim ? (now_ms() - tm_open_t) / 160.0f : 1.0f;
    if (p > 1) p = 1;
    f32 dy = -66 * (1 - p) * (1 - p);
    int n = 0;
    FOR_TABS(t) if (tab_shown(t)) n++;
    f32 iw = n ? 612.0f / n : 60, x = 14;
    int sk = ui_skin >= 3 ? 3 : ui_skin;
    nv_raw = 1;
    col_t bg1 = sk == 3 ? RGBA(10, 10, 18, 245) : sk == 2 ? RGBA(26, 30, 52, 245) : sk == 1 ? RGBA(46, 46, 50, 245) : RGBA(16, 10, 44, 245);
    col_t bg2 = sk == 3 ? RGBA(4, 4, 8, 248) : sk == 2 ? RGBA(16, 18, 34, 248) : sk == 1 ? RGBA(36, 36, 40, 248) : RGBA(8, 5, 26, 248);
    gx_rrect_fill(6, 4 + dy, 628, 62, 16, bg1, bg2);
    gx_rrect_stroke(6, 4 + dy, 628, 62, 16, 1.0f, sk == 0 ? RGBA(120, 80, 230, 255) : RGBA(255, 255, 255, 30));
    nv_raw = 0;
    FOR_TABS(t) {
        if (!tab_shown(t)) continue;
        int on = t == tab;
        if (on) gx_rrect_fill(x + 2, 8 + dy, iw - 4, 54, 10, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));   /* (selection du style) */
        nv_raw = 1;
        col_t c = on ? RGBA(255, 255, 255, 255) : sk == 0 ? RGBA(180, 165, 235, 255) : RGBA(175, 180, 205, 255);
        nv_icon(t, x + iw / 2, 25 + dy, c);
        char nm[48];
        txt_scale = 0.82f;
        fit_text(F_SMALL, TR(nv_tab_names[t]), (int)(iw - 4), nm, sizeof(nm));
        f32 tw = (f32)text_width(F_SMALL, nm, 0);
        gx_text1(F_SMALL, x + iw / 2 - tw / 2, 53 + dy, nm, c);
        txt_scale = 1.0f;
        nv_raw = 0;
        gp_zone(x, 0, iw, 66 + dy, NULL, t, GPZ_TAB);          /* (jusqu'au bord : la fleche s'arrete en haut) */
        if (web && wpx >= x && wpx < x + iw && wpy < 66) {     /* (v218 : pointeur du navigateur : cadre, et A ouvre l'onglet) */
            gx_rrect_stroke(x + 2, 8 + dy, iw - 4, 54, 10, 1.5f, RGBA(255, 255, 255, 200));
            if ((PAD_ButtonsDown(0) & PAD_BUTTON_A) && t != tab) { web_switch = t; }
        }
        x += iw;
    }
    if (web) cb_overlay_cursor();                              /* (le pointeur reste visible sur la barre) */
    if (web_switch >= 0) { int t = web_switch; web_switch = -1; tm_vis = 0; tab = t; tab_strip_until = now_ms() + 1800; tab_enter(t); }
    return 1;
}
static void draw_tab_strip(void) {
    if (tab_menu_draw()) return;
    if (now_ms() >= tab_strip_until || ui_skin >= 2) return;   /* (v101 : Nouvelle : le dock est toujours la) */
    int n = 0, total = 0;
    FOR_TABS(t) if (tab_shown(t)) { total += text_width(F_SMALL, tab_names[t], 1) + 16; n++; }
    total += (n - 1) * 4;
    f32 sc = total > 600 ? 600.0f / total : 1.0f;          /* (v23 : 11 onglets, le bandeau se resserre) */
    if (sc < 1) {
        txt_scale = sc;
        total = 0;
        FOR_TABS(t) if (tab_shown(t)) total += text_width(F_SMALL, tab_names[t], 1) + 10;
        total += (n - 1) * 3;
    }
    f32 x = 320 - total / 2.0f;
    gx_rrect_fill(x - 10, 4, total + 20, 26, 10, RGBA(10, 6, 30, 235), RGBA(10, 6, 30, 235));
    gx_rrect_stroke(x - 10, 4, total + 20, 26, 10, 1.2f, RGBA(120, 80, 230, 255));
    FOR_TABS(t) {
        if (!tab_shown(t)) continue;
        int w = text_width(F_SMALL, tab_names[t], 1) + (sc < 1 ? 10 : 16);
        if (t == tab) gx_rrect_fill(x, 8, (f32)w, 18, 9, RGBA(124, 54, 240, 255), RGBA(80, 26, 180, 255));
        gx_text(F_SMALL, x + (sc < 1 ? 5 : 8), 21, tab_names[t], t == tab ? RGBA(255, 255, 255, 255) : RGBA(150, 135, 210, 255),
                t == tab ? RGBA(255, 255, 255, 255) : RGBA(150, 135, 210, 255), 1);
        gp_zone(x, 6, (f32)w, 22, NULL, t, GPZ_TAB);
        x += w + (sc < 1 ? 3 : 4);
    }
    txt_scale = 1.0f;
    if (gp_on) tab_strip_until = now_ms() + 1500;          /* (le curseur garde le bandeau visible) */
}

/* ---------- Liste a cocher (onglets affiches, elements du lecteur) ---------- */
typedef struct {
    const char *title;
    int n;
    const char *const *labels;
    int (*get)(int i);
    void (*toggle)(int i);
    int locked;                                  /* premiere ligne non modifiable (-1 = aucune) */
    void (*move)(int i, int dir);                /* v43 : X / Y = deplacer la ligne (NULL = non) */
    int (*lockf)(void);                          /* v43 : ligne non modifiable, calculee (NULL = locked) */
} checklist_t;
static const checklist_t *ck = NULL;
static int ck_sel = 0;

static void ck_tab_labels(void);
static void checklist_open(const checklist_t *c) { ck = c; ck_sel = 0; ck_tab_labels(); }

static void checklist_input(u32 down, u32 nav, int fire) {
    if (!ck) return;
    if (fire && (nav & PAD_BUTTON_DOWN)) ck_sel = (ck_sel + 1) % ck->n;
    if (fire && (nav & PAD_BUTTON_UP)) ck_sel = (ck_sel + ck->n - 1) % ck->n;
    int lk = ck->lockf ? ck->lockf() : ck->locked;
    if ((down & PAD_BUTTON_A) && ck_sel != lk) { ck->toggle(ck_sel); settings_dirty = 1; }
    if (ck->move && (down & (PAD_BUTTON_X | PAD_BUTTON_Y))) {
        int d = (down & PAD_BUTTON_X) ? 1 : -1, to = ck_sel + d;
        if (to >= 0 && to < ck->n) { ck->move(ck_sel, d); ck_sel = to; settings_dirty = 1; }
    }
    if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) { ck = NULL; if (settings_dirty) save_settings(); }
}

static void draw_checklist(void) {
    if (!ck) return;
    const f32 rh = 25, w = ck->move ? 440 : 360, h = 72 + ck->n * rh, x = 320 - w / 2, y = 240 - h / 2;
    int lk = ck->lockf ? ck->lockf() : ck->locked;
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
    gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 255), RGBA(8, 5, 26, 255));
    draw_panel(x, y, w, h);
    gx_text1(F_LARGE, x + 20, y + 34, ck->title, RGBA(255, 255, 255, 255));
    for (int i = 0; i < ck->n; i++) {
        f32 ry = y + 48 + i * rh;
        int on = (i == ck_sel);
        gp_zone(x + 14, ry + 1, w - 28, rh - 3, &ck_sel, i, GPZ_SEL);
        if (on) gx_rrect_fill(x + 14, ry + 1, w - 28, rh - 3, 6, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        int v = ck->get(i);
        /* v122 : coche bien visible (case pleine + coche ; avant : un point de 7 pixels, a peine visible a la tele) ;
           decoche : case vide et nom grise */
        if (v) {
            gx_rrect_fill(x + 25, ry + 5, 15, 15, 3, RGBA(120, 230, 160, 255), RGBA(60, 190, 120, 255));
            col_t ckc = RGBA(20, 40, 30, 255);
            f32 cx = x + 25, cy = ry + 5;
            gx_poly4(cx + 3, cy + 7.5f, cx + 5, cy + 5.5f, cx + 7.5f, cy + 8, cx + 6, cy + 10.5f, ckc, ckc, ckc, ckc);
            gx_poly4(cx + 6, cy + 10.5f, cx + 5.5f, cy + 8.5f, cx + 11, cy + 3, cx + 12.5f, cy + 4.5f, ckc, ckc, ckc, ckc);
        } else gx_rrect_stroke(x + 25, ry + 5, 15, 15, 3, 1.5f, RGBA(200, 190, 235, 200));
        gx_text1(F_MED, x + 50, ry + 18, ck->labels[i], i == lk ? RGBA(150, 135, 210, 255) : v || on ? RGBA(240, 232, 255, 255) : RGBA(150, 140, 185, 255));
        if (ck->move && on) {                                 /* fleches : X descend, Y monte */
            if (i > 0) gx_tri(x + w - 40, ry + 15, x + w - 34, ry + 6, x + w - 28, ry + 15, RGBA(255, 255, 255, 220));
            if (i + 1 < ck->n) gx_tri(x + w - 58, ry + 8, x + w - 46, ry + 8, x + w - 52, ry + 17, RGBA(255, 255, 255, 220));
        }
    }
    if (ck->move) gx_text_c(F_SMALL, 320, y + h - 8, "A : cocher     X / Y : descendre / monter     B : fermer", RGBA(255, 210, 120, 255));
    else gx_text_c(F_SMALL, 320, y + h - 8, "A : cocher / d\xC3\xA9" "cocher     B : fermer", RGBA(255, 210, 120, 255));
}

static int ck_tab_get(int i) { return tab_shown(i); }
static void tab_disable(int t);
static void ck_tab_toggle(int i) { if (i != TAB_MUSIC) tab_hidden ^= (1u << i); if (!tab_shown(i)) tab_disable(i); if (!tab_shown(tab)) tab = TAB_MUSIC; }
/* v43 : la liste suit l'ordre des onglets ; X / Y les deplacent */
static const char *ck_tab_lab[TAB_COUNT];
static void ck_tab_labels(void) { for (int i = 0; i < TAB_COUNT; i++) ck_tab_lab[i] = tab_names[tab_order[i]]; }
static int ck_tabo_get(int i) { return tab_shown(tab_order[i]); }
static void ck_tabo_toggle(int i) { ck_tab_toggle(tab_order[i]); }
static int ck_tabo_lock(void) { for (int i = 0; i < TAB_COUNT; i++) if (tab_order[i] == TAB_MUSIC) return i; return 0; }
static void ck_tabo_move(int i, int d) {
    int j = i + d;
    if (j < 0 || j >= TAB_COUNT) return;
    u8 t = tab_order[i]; tab_order[i] = tab_order[j]; tab_order[j] = t;
    ck_tab_labels();
}
static const checklist_t ck_tabs = { "Onglets affich\xC3\xA9s", TAB_COUNT, ck_tab_lab, ck_tabo_get, ck_tabo_toggle, -1, ck_tabo_move, ck_tabo_lock };

static const char *const el_names[EL_COUNT] = { "Pochette", "Paroles / spectre", "Boutons", "Album et ann\xC3\xA9" "e", "Horloge (en haut)" };
static int ck_el_get(int i) { return (el_mask >> i) & 1; }
static void ck_el_toggle(int i) { el_mask ^= (1u << i); }
static const checklist_t ck_elems = { "\xC3\x89l\xC3\xA9ments du lecteur", EL_COUNT, el_names, ck_el_get, ck_el_toggle, -1 };

/* ---------- Ce qui joue (pour les dispositions) ---------- */
typedef struct { char title[200], sub[200], info[200]; int elapsed, dur, radio, active; } np_t;

static void np_get(np_t *n) {
    memset(n, 0, sizeof(*n));
    char raw[200];
    if (src == SRC_RADIO && cur_station >= 0 && state != ST_IDLE) {
        n->radio = 1; n->active = 1;
        text_display(stations[cur_station].name, n->title, sizeof(n->title));
        if (state == ST_ERROR) snprintf(n->sub, sizeof(n->sub), "%s", err_msg);
        else if (now_title[0]) text_display(now_title, n->sub, sizeof(n->sub));
        n->elapsed = pod_pos_sec();
        n->dur = pod_on ? pod_dur : -1;
        return;
    }
    if (src == SRC_FILE && playing && cur_track >= 0) {
        n->active = 1;
        track_title(cur_track, raw, sizeof(raw));
        text_display(raw, n->title, sizeof(n->title));
        if (track_err) snprintf(n->sub, sizeof(n->sub), "%s", track_err);
        else if (artist[0]) text_display(artist, n->sub, sizeof(n->sub));
        cover_info(n->info, sizeof(n->info));
        n->elapsed = (int)(consumed / 48000u);
        n->dur = track_duration(cur_track);
        return;
    }
    snprintf(n->title, sizeof(n->title), "Rien en cours");
    snprintf(n->sub, sizeof(n->sub), "Choisis un morceau et appuie sur A");
}

/* pochette a n'importe quelle taille (sinon le cube du logo) */
static void draw_cover_at(f32 x, f32 y, f32 sz) {
    int have = (src == SRC_FILE && playing && cover_ok) || (src == SRC_RADIO && cover_ok && cur_station >= 0 && state != ST_IDLE);
    if (have) {
        uxsave_t uz = ux_native(x + sz / 2);             /* (16:9 : pochette carree) */
        gx_set_cover();
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(UX(x), y);           GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(UX(x + sz), y);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(UX(x + sz), y + sz); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(UX(x), y + sz);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f);
        GX_End();
        gx_rrect_stroke(x, y, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 235));
        ux_restore(uz);
    } else {
        gx_vgrad(x, y, sz, sz, RGBA(20, 12, 56, 255), RGBA(70, 30, 150, 255));
        draw_logo_cube(x + sz / 2, y + sz / 2, sz * 0.28f);
        gx_rrect_stroke(x, y, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 235));
    }
}

static void draw_progress_at(f32 x, f32 y, f32 w, const np_t *n) {
    char t1[16], t2[16], buf[40];
    fmt_listen(t1, n->active ? n->elapsed : 0);
    fmt_time(t2, n->dur);
    if (n->radio && n->dur <= 0) snprintf(buf, sizeof(buf), "%s", t1);
    else snprintf(buf, sizeof(buf), "%s / %s", t1, t2);
    gx_text1(F_MED, x, y, buf, RGBA(180, 160, 240, 255));
    f32 frac = (n->active && n->dur > 0) ? (f32)n->elapsed / (f32)n->dur : 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    gx_rrect_fill(x, y + 10, w, 3, 1.5f, RGBA(70, 50, 140, 230), RGBA(70, 50, 140, 230));
    if (frac > 0) gx_hgrad(x, y + 10, w * frac, 3, RGBA(120, 64, 255, 255), RGBA(180, 100, 255, 255));
    if (n->active && n->dur > 0) gx_circle(x + w * frac, y + 11.5f, 5, RGBA(240, 232, 255, 255), RGBA(240, 232, 255, 255));
}

/* paroles (3 lignes) si le morceau en a, sinon le spectre */
static void draw_viz_at(f32 x, f32 y, f32 w, f32 h, int big) {
    if (src == SRC_FILE && playing && lrc_n > 0 && lrc_track == cur_track && lyr_show) {
        char a[300], b[300], d1[300];
        if (lyr_show == 2) {                                  /* v43 : paroles + spectre (attenue dessous) */
            draw_viz(x, y + h * 0.82f, w, h * 0.6f);
            gx_rect(x - 4, y, w + 8, h, RGBA(8, 5, 26, 150));
        }
        int li = lyrics_current();
        f32 cx = x + w / 2, cy = y + h / 2;
        if (li > 0) {
            text_display(lyrics_line(li - 1), d1, sizeof(d1));
            fit_text(F_SMALL, d1, (int)w, a, sizeof(a));
            gx_text_c(F_SMALL, cx, cy - (big ? 34 : 26), a, RGBA(120, 104, 180, 255));
        }
        text_display(li >= 0 ? lyrics_line(li) : "...", d1, sizeof(d1));
        if (big) txt_scale = 1.2f;
        wrap2(big ? F_LARGE : F_MED, d1, (int)w, a, b, sizeof(a));
        f32 lh = big ? 32 : 19;
        gx_text_c(big ? F_LARGE : F_MED, cx, b[0] ? cy - 2 : cy + 6, a, RGBA(255, 255, 255, 255));
        if (b[0]) gx_text_c(big ? F_LARGE : F_MED, cx, cy - 2 + lh, b, RGBA(255, 255, 255, 255));
        txt_scale = 1.0f;
        if (li + 1 < lrc_n) {
            text_display(lyrics_line(li + 1), d1, sizeof(d1));
            fit_text(F_SMALL, d1, (int)w, a, sizeof(a));
            gx_text_c(F_SMALL, cx, cy + (big ? 64 : 40), a, RGBA(120, 104, 180, 255));
        }
        return;
    }
    draw_viz(x, y + h * 0.82f, w, h * 0.7f);              /* v43 : le style choisi (avant : toujours des barres) */
}

static void draw_buttons_at(f32 cx, f32 cy) {
    col_t icon = RGBA(230, 220, 255, 255);
    draw_round_button(cx - 76, cy, 21);
    draw_round_button(cx + 76, cy, 21);
    gx_tri(cx - 86, cy, cx - 77, cy - 7, cx - 77, cy + 7, icon);
    gx_tri(cx - 77, cy, cx - 68, cy - 7, cx - 68, cy + 7, icon);
    gx_tri(cx + 86, cy, cx + 77, cy - 7, cx + 77, cy + 7, icon);
    gx_tri(cx + 77, cy, cx + 68, cy - 7, cx + 68, cy + 7, icon);
    gx_circle_glow(cx, cy, 29, RGBA(170, 90, 255, 150), 5);
    gx_circle(cx, cy, 29, RGBA(150, 78, 255, 255), RGBA(92, 36, 206, 255));
    int on = (src == SRC_FILE && playing && !paused) || (src == SRC_RADIO && state == ST_PLAY);
    if (on) {
        gx_rrect_fill(cx - 9, cy - 10, 6, 20, 2, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
        gx_rrect_fill(cx + 3, cy - 10, 6, 20, 2, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
    } else gx_tri(cx - 6, cy - 11, cx - 6, cy + 11, cx + 11, cy, RGBA(255, 255, 255, 255));
}

/* morceaux a suivre (file de lecture) */
static void draw_upnext_at(f32 x, f32 y, f32 w, int rows) {
    if (src != SRC_FILE || !playing || qn <= 0 || qpos < 0) return;
    gx_text(F_SMALL, x, y, "\xC3\x80 SUIVRE", RGBA(140, 122, 210, 255), RGBA(140, 122, 210, 255), 3);
    for (int i = 1; i <= rows; i++) {
        int p = qpos + i;
        if (p >= qn) { if (repeat_mode == REP_ALL) p %= qn; else break; }
        char raw[200], d[200], f[200];
        track_title(queue[p], raw, sizeof(raw));
        text_display(raw, d, sizeof(d));
        fit_text(F_MED, d, (int)w, f, sizeof(f));
        gx_text1(F_MED, x, y + 4 + i * 21, f, RGBA(205, 195, 245, 255));
    }
}

static void draw_list_wide(void);
/* ---------- Dispositions du lecteur ---------- */
static u64 layout_toast_until = 0;

/* Pochette geante : grande pochette, infos, paroles et file d'attente */
static void draw_layout_cover(void) {
    np_t n;
    np_get(&n);
    char buf[300];
    draw_panel(11, 73, 618, 338);
    f32 csz = 250, cx0 = 28, cy0 = 92;
    if (EL(EL_COVER)) draw_cover_at(cx0, cy0, csz);
    else { csz = 0; cx0 = 10; }
    f32 tx = cx0 + csz + 22, tw = 616 - tx - 10;
    txt_scale = 1.2f;
    fit_text(F_LARGE, n.title, (int)tw, buf, sizeof(buf));
    gx_text1(F_LARGE, tx, 126, buf, RGBA(255, 255, 255, 255));
    txt_scale = 1.0f;
    fit_text(F_MED, n.sub, (int)tw, buf, sizeof(buf));
    gx_text1(F_MED, tx, 152, buf, RGBA(156, 116, 255, 255));
    if (EL(EL_INFO) && n.info[0]) { fit_text(F_SMALL, n.info, (int)tw, buf, sizeof(buf)); gx_text1(F_SMALL, tx, 172, buf, RGBA(140, 122, 200, 255)); }
    draw_progress_at(tx, 196, tw - 10, &n);
    if (EL(EL_VIZ)) draw_viz_at(tx, 222, tw, 104, 0);
    else draw_upnext_at(tx, 236, tw, 4);
    if (EL(EL_BUTTONS)) draw_buttons_at(tx + tw / 2, 372);
    else if (EL(EL_VIZ)) draw_upnext_at(tx, 344, tw, 2);
}

/* Liste etendue : la liste sur toute la largeur + un petit lecteur en bas */
static void draw_layout_list(void) {
    np_t n;
    np_get(&n);
    char buf[300];
    draw_list_wide();
    legend_box(289, 341);
    f32 x = 296;
    if (EL(EL_COVER)) { draw_cover_at(x, 423, 31); x += 38; }
    fit_text(F_MED, n.title, (int)(622 - x - 70), buf, sizeof(buf));
    gx_text1(F_MED, x, 437, buf, RGBA(255, 255, 255, 255));
    fit_text(F_SMALL, n.sub, (int)(622 - x - 70), buf, sizeof(buf));
    gx_text1(F_SMALL, x, 452, buf, RGBA(156, 116, 255, 255));
    char t1[16], t2[16];
    fmt_listen(t1, n.active ? n.elapsed : 0);
    fmt_time(t2, n.dur);
    snprintf(buf, sizeof(buf), "%s", t1);
    gx_text_r(F_SMALL, 622, 437, buf, RGBA(180, 160, 240, 255));
    if (n.dur > 0) gx_text_r(F_SMALL, 622, 452, t2, RGBA(130, 115, 200, 255));
}

/* Minimaliste : sans cadre, pochette au centre, titre et paroles */
static void draw_layout_minimal(void) {
    np_t n;
    np_get(&n);
    char buf[300];
    f32 y = 86;
    if (EL(EL_COVER)) { draw_cover_at(320 - 85, y, 170); y += 194; }
    else y += 40;
    txt_scale = 1.25f;
    fit_text(F_LARGE, n.title, 560, buf, sizeof(buf));
    gx_text_c(F_LARGE, 320, y, buf, RGBA(255, 255, 255, 255));
    txt_scale = 1.0f;
    fit_text(F_MED, n.sub, 560, buf, sizeof(buf));
    gx_text_c(F_MED, 320, y + 24, buf, RGBA(170, 140, 255, 255));
    if (EL(EL_INFO) && n.info[0]) { fit_text(F_SMALL, n.info, 560, buf, sizeof(buf)); gx_text_c(F_SMALL, 320, y + 42, buf, RGBA(140, 122, 200, 255)); }
    draw_progress_at(170, y + 62, 300, &n);
    if (EL(EL_VIZ)) draw_viz_at(80, y + 80, 480, 470 - (y + 80), 0);
}

/* v43 : Spectre geant : le spectre (style choisi) sur tout l'ecran, titre et paroles par-dessus */
static void draw_layout_bigviz(void) {
    np_t n;
    np_get(&n);
    char buf[300];
    draw_panel(11, 73, 618, 338);
    f32 x = 30;
    if (EL(EL_COVER)) { draw_cover_at(x, 88, 64); x += 76; }
    txt_scale = 1.15f;
    fit_text(F_LARGE, n.title, (int)(610 - x), buf, sizeof(buf));
    gx_text1(F_LARGE, x, 114, buf, RGBA(255, 255, 255, 255));
    txt_scale = 1.0f;
    fit_text(F_MED, n.sub, (int)(610 - x), buf, sizeof(buf));
    gx_text1(F_MED, x, 138, buf, RGBA(156, 116, 255, 255));
    draw_progress_at(x, 162, 600 - x, &n);
    if (!EL(EL_VIZ)) return;
    int lyr = src == SRC_FILE && playing && lrc_n > 0 && lrc_track == cur_track && lyr_show;
    if (!lyr || lyr_show == 2) draw_viz(30, 392, 580, lyr ? 180 : 214);
    if (lyr) {
        int keep = lyr_show; lyr_show = 1;
        if (keep == 2) { gx_rect(20, 296, 600, 100, RGBA(8, 5, 26, 140)); draw_viz_at(30, 290, 580, 110, 1); }
        else draw_viz_at(30, 196, 580, 170, 1);
        lyr_show = keep;
    }
}

static void draw_layout_toast(void) {
    if (now_ms() >= layout_toast_until) return;
    char buf[96];
    snprintf(buf, sizeof(buf), "Disposition : %s   (stick C vers le bas)", layout_names[player_layout]);
    int w = text_width(F_MED, buf, 0) + 30;
    gx_rrect_fill(320 - w / 2.0f, 440, (f32)w, 28, 10, RGBA(10, 6, 30, 235), RGBA(10, 6, 30, 235));
    gx_text_c(F_MED, 320, 459, buf, RGBA(255, 255, 255, 255));
}

static void layout_next(void) {
    player_layout = (player_layout + 1) % LAYOUT_COUNT;
    if (player_layout == L_LIST && tab != TAB_MUSIC) player_layout = (player_layout + 1) % LAYOUT_COUNT;   /* (liste etendue : MUSIQUE) */
    layout_toast_until = now_ms() + 2000;
    settings_dirty = 1;
}

/* horloge en haut de l'ecran */
static void draw_header_clock(void) {
    if (ui_skin) return;                                         /* (Moderne : dans l'en-tete) */
    if (!EL(EL_CLOCK)) return;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm) return;
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d", tm->tm_hour, tm->tm_min);
    gx_text_r(F_LARGE, 386, 42, b, RGBA(220, 205, 255, 255));
}

/* Liste MUSIQUE sur toute la largeur : titre, artiste, album, duree */
static void draw_list_wide(void) {
    const int rows = 12;
    int sel = sel_row;
    int start = sel - rows / 2;
    if (start > nents - rows) start = nents - rows;
    if (start < 0) start = 0;
    char buf[NAME_LEN], t[24], d[NAME_LEN];
    draw_panel(11, 73, 618, 338);
    snprintf(buf, sizeof(buf), "%d", nents);
    draw_tabs(buf);
    gx_text(F_SMALL, 76, 125, "TITRE", RGBA(140, 122, 210, 255), RGBA(140, 122, 210, 255), 2);
    gx_text(F_SMALL, 330, 125, "ARTISTE", RGBA(140, 122, 210, 255), RGBA(140, 122, 210, 255), 2);
    gx_text(F_SMALL, 460, 125, "ALBUM", RGBA(140, 122, 210, 255), RGBA(140, 122, 210, 255), 2);
    for (int i = 0; i < rows; i++) {
        int r = start + i;
        if (r >= nents) break;
        lent_t e = ents[r];
        f32 ry = 130.0f + i * 23.0f, cy = ry + 11.5f;
        int is_sel = (r == sel), is_cur = (e.type == E_TRACK && playing && e.idx == cur_track);
        gp_zone(18, ry + 1, 604, 21, &sel_row, r, GPZ_SEL);
        if (is_sel) gx_rrect_fill(20, ry + 1, 600, 21, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        col_t txt = is_sel ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
        col_t dim = is_sel ? RGBA(235, 225, 255, 255) : RGBA(140, 125, 200, 255);
        if (e.type != E_TRACK) {
            fit_text_sel(is_sel, F_MED, group_label(e.idx), 520, d, sizeof(d));
            gx_text1(F_MED, 50, cy + 5, e.type == E_UP ? ".." : d, txt);
            if (e.type == E_UP) { fit_text_sel(is_sel, F_MED, group_label(e.idx), 480, d, sizeof(d)); gx_text1(F_MED, 76, cy + 5, d, txt); }
            continue;
        }
        int k = e.idx;
        if (!tg_done[k]) tag_scan_one(k);                    /* etiquettes lues a l'affichage */
        if (is_cur) gx_tri(31, cy - 6, 31, cy + 6, 41, cy, RGBA(255, 255, 255, 255));
        else draw_note(36, cy, dim);
        char raw[NAME_LEN];
        track_label(k, raw, sizeof(raw), !(cur_group >= 0 && (view_mode == VIEW_ARTISTS || view_mode == VIEW_ALBUMS)));
        text_display(raw, d, sizeof(d));
        fit_text_sel(is_sel, F_MED, d, 270, buf, sizeof(buf));
        gx_text1(F_MED, 52, cy + 5, buf, txt);
        text_display(tg_artist[k], d, sizeof(d));
        fit_text_sel(is_sel, F_SMALL, d, 122, buf, sizeof(buf));
        gx_text1(F_SMALL, 330, cy + 4, buf, dim);
        text_display(tg_album[k], d, sizeof(d));
        fit_text_sel(is_sel, F_SMALL, d, 96, buf, sizeof(buf));
        gx_text1(F_SMALL, 460, cy + 4, buf, dim);
        if (rating[k] > 0) {                                 /* v76 : note (une etoile + chiffre) */
            draw_star1(566, cy, 4.2f, is_sel ? RGBA(255, 225, 140, 255) : RGBA(255, 205, 80, 255));
            char rn[4]; snprintf(rn, sizeof(rn), "%d", rating[k]);
            gx_text1(F_SMALL, 572, cy + 4, rn, is_sel ? RGBA(255, 235, 190, 255) : RGBA(220, 185, 120, 255));
        }
        fmt_time(t, track_duration(k));
        gx_text_r(F_SMALL, 612, cy + 4, t, dim);
    }
    if (nents > rows) {
        f32 th = 270.0f * rows / nents, ty = 132.0f + (270.0f - th) * (f32)start / (f32)(nents - rows);
        gx_rrect_fill(623, ty, 3, th, 1.5f, RGBA(150, 96, 255, 255), RGBA(110, 60, 230, 255));
    }
    legend_box(11, 271);
    snprintf(buf, sizeof(buf), "< %s >   Liste \xC3\xA9tendue", TR(view_names[view_mode]));
    gx_text1(F_SMALL, 24, 443, buf, RGBA(170, 155, 225, 255));
}

static void draw_layout_hint(void) {
    legend_box(11, 271);
    gx_text1(F_SMALL, 24, 436, "A : pause    L / R : morceau", RGBA(208, 198, 240, 255));
    gx_text1(F_SMALL, 24, 452, "Stick C bas : disposition", RGBA(150, 135, 210, 255));
}

/* legende des onglets plein ecran : touches propres + Z ONGLET + ST MENU */
static void draw_mini_badge(f32 cx, f32 cy, const char *k);
static void draw_legend_keys(const char *const *keys, const char *const *labels, int n) {
    const char *K[8], *L[8];
    int cnt = 0;
    for (int i = 0; i < n && cnt < 6; i++) { K[cnt] = keys[i]; L[cnt] = labels[i]; cnt++; }
    K[cnt] = "Z"; L[cnt] = "ONGLET"; cnt++;
    K[cnt] = "ST"; L[cnt] = "MENU"; cnt++;
    const f32 x0 = 11, w = 618, cy = 438.5f;
    legend_box(x0, w);
    f32 total = 0;
    for (int i = 0; i < cnt; i++) total += 20 + 5 + text_width(F_SMALL, L[i], 1);
    f32 space = (w - 40 - total) / (cnt - 1);
    f32 x = x0 + 20;
    for (int i = 0; i < cnt; i++) {
        draw_mini_badge(x + 10, cy, K[i]);
        x += 25;
        x += gx_text(F_SMALL, x, cy + 4.5f, L[i], RGBA(208, 198, 240, 255), RGBA(208, 198, 240, 255), 1) + space;
    }
}

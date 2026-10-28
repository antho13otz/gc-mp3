// GC Station v196 : l'espace « Jeux GameCube » (inclus dans main.c, apres gclib.c)
// A sur « Jeux GameCube » : on entre dans un espace a part, dans les couleurs d'Aurore :
//  - en haut, la meme barre que les onglets (nom au centre, voisins de chaque cote, points en degrade),
//    mais avec les filtres des jeux : Recents, Tout, Favoris, Plus joues, A a Z (Z : filtre suivant) ;
//  - au centre, la vitrine 3D : les jaquettes en perspective, reflet au sol, lueur rose -> bleu ;
//  - le nom du jeu, ses infos, et une barre d'aide en verre. B : retour a l'accueil de JEUX.
// Les styles a jaquettes (ui_skin >= 1) l'utilisent ; R : liste (comme avant), R encore : la vitrine.
// La fiche (Y), "Lancer ce jeu ?" (A), les cheats (X) et les favoris (L) restent ceux de gclib.c.

enum { GSF_RECENT, GSF_ALL, GSF_FAV, GSF_PLAYED, GSF_AZ, GSF_APPS, GSF_N };
static const char *const gs_filter_names[GSF_N] = { "R\xC3\xA9" "cents", "Tout", "Favoris", "Plus jou\xC3\xA9s", "A \xC3\xA0 Z", "Applis" };
static const char *const gs_filter_keys[GSF_N] = { "recents", "tout", "favoris", "plus_joues", "az", "applis" };
static int gs_app_confirm = -1;                 /* "Lancer cette appli ?" (rang dans ap[]) */
static int gs_filter = GSF_ALL;                 /* (reglage jeux_filtre) */
static int gs_apps(void) { return gs_filter == GSF_APPS; }
static s16 gs_idx[GL_MAX];                      /* jeux du filtre (rangs dans gl[]) */
static int gs_n = 0, gs_pos = 0, gs_dirty = 1, gs_built_n = -1;
static u64 gs_built_t = 0;
static int gs_built_f = -1;                    /* v198 : filtre de la derniere liste */
static f32 gs_fp = -1;                          /* filtre affiche (en douceur, comme les onglets d'Aurore) */
static f32 gs_cpos = -10000.0f;                 /* position de la vitrine (en douceur) */

static int gs_view(void) { return ui_skin >= 1 && gl_view != GLV_LIST; }
/* l'espace est a l'ecran (dessin et touche Z) */
static int gs_active(void) { return tab == TAB_GAMES && gl_on && gs_view() && !menu_open && !tl_screen && !ic_on && !kb_is_open(); }   /* (v197 : la fiche aussi) */

static int gs_cmp_title(const void *a, const void *b) {
    const gcg_t *x = &gl[*(const s16 *)a], *y = &gl[*(const s16 *)b];
    int c = strcasecmp(x->title, y->title);
    return c ? c : x->disc - y->disc;
}
static int gs_cmp_recent(const void *a, const void *b) {
    const gcg_t *x = &gl[*(const s16 *)a], *y = &gl[*(const s16 *)b];
    if (x->last != y->last) return x->last > y->last ? -1 : 1;
    return gs_cmp_title(a, b);
}
static int gs_cmp_played(const void *a, const void *b) {
    const gcg_t *x = &gl[*(const s16 *)a], *y = &gl[*(const s16 *)b];
    if (x->play != y->play) return x->play > y->play ? -1 : 1;
    if (x->runs != y->runs) return x->runs > y->runs ? -1 : 1;
    return gs_cmp_title(a, b);
}
/* liste du filtre ; le jeu choisi reste choisi s'il y est */
static void gs_build(void) {
    if (gs_apps()) {                            /* v196 : les applis homebrew (avant : Mes applis, accueil de JEUX) */
        if (!ap_scanned) ap_rescan();
        gs_n = ap ? (ap_n < GL_MAX ? ap_n : GL_MAX) : 0;
        for (int i = 0; i < gs_n; i++) gs_idx[i] = (s16)i;
        gs_pos = ap_sel >= 0 && ap_sel < gs_n ? ap_sel : 0;
        gs_dirty = 0; gs_built_n = gl_n; gs_built_t = now_ms(); gs_built_f = gs_filter;
        return;
    }
    int keep = gl_sel >= 0 && gl_sel < gl_n ? gl_sel : -1;
    gs_n = 0;
    for (int k = 0; k < gl_n && gs_n < GL_MAX; k++) {
        const gcg_t *g = &gl[k];
        int in = gs_filter == GSF_ALL ? 1
               : gs_filter == GSF_RECENT ? !g->dvd && g->last
               : gs_filter == GSF_FAV ? !g->dvd && g->fav
               : gs_filter == GSF_PLAYED ? !g->dvd && (g->play || g->runs)
               : !g->dvd;
        if (in) gs_idx[gs_n++] = (s16)k;
    }
    if (gs_filter == GSF_RECENT) qsort(gs_idx, gs_n, sizeof(s16), gs_cmp_recent);
    else if (gs_filter == GSF_PLAYED) qsort(gs_idx, gs_n, sizeof(s16), gs_cmp_played);
    else if (gs_filter == GSF_AZ) qsort(gs_idx, gs_n, sizeof(s16), gs_cmp_title);
    int old = gs_built_f == gs_filter ? gs_pos : 0;   /* v198 : jeu sorti du filtre (retire des favoris) : son voisin */
    gs_pos = old < gs_n ? old : (gs_n ? gs_n - 1 : 0);
    for (int i = 0; i < gs_n; i++) if (gs_idx[i] == keep) { gs_pos = i; break; }
    if (gs_n) gl_sel = gs_idx[gs_pos];
    gs_dirty = 0; gs_built_n = gl_n; gs_built_t = now_ms(); gs_built_f = gs_filter;
}
static void gs_check(void) {                    /* (la liste des jeux a pu changer : tri, favori, actualiser) */
    if (gs_dirty || gs_built_n != gl_n || now_ms() - gs_built_t > 3000) gs_build();
    if (gs_apps()) { if (gs_pos >= gs_n) gs_pos = gs_n ? gs_n - 1 : 0; ap_sel = gs_pos; return; }
    if (gs_n && (gs_pos >= gs_n || gs_idx[gs_pos] != gl_sel)) {   /* (choisi ailleurs : fiche, jeu au hasard...) */
        int f = -1;
        for (int i = 0; i < gs_n; i++) if (gs_idx[i] == gl_sel) { f = i; break; }
        if (f >= 0) gs_pos = f; else { if (gs_pos >= gs_n) gs_pos = gs_n - 1; gl_sel = gs_idx[gs_pos]; }
    }
}
static void gs_filter_load(const char *v) { for (int i = 0; i < GSF_N; i++) if (!strcasecmp(v, gs_filter_keys[i])) gs_filter = i; }
static const char *gs_filter_key(void) { return gs_filter_keys[gs_filter >= 0 && gs_filter < GSF_N ? gs_filter : GSF_ALL]; }
static void gs_filter_next(int d) {             /* (touche Z) */
    gs_filter = (gs_filter + d + GSF_N) % GSF_N;
    gs_dirty = 1;
    settings_dirty = 1;
    gs_check();
    gs_cpos = -10000.0f;                        /* (la vitrine se replace tout de suite sur le jeu choisi) */
}

/* ---------- touches dans l'espace ---------- */
static void gs_input(u32 down, u32 nav, int fire) {
    gs_check();
    if (gs_app_confirm >= 0) {                  /* "Lancer cette appli ?" */
        if ((down & PAD_BUTTON_A) && gs_app_confirm < ap_n) {
            int k = gs_app_confirm;
            gs_app_confirm = -1;
            if (settings_dirty) save_settings();
            gl_ret_app = 1;                     /* (retour a GC Station quand l'appli quitte) */
            const char *e = gl_boot_dol(ap[k].path, NULL, NULL, ap[k].name);   /* (ne revient pas si ca marche) */
            gl_ret_app = 0;
            gl_busy[0] = 0;
            if (e) gl_note(e);
        }
        if (down & PAD_BUTTON_B) gs_app_confirm = -1;
        return;
    }
    if (fire && gs_n) {
        int p = gs_pos;
        if (nav & PAD_BUTTON_RIGHT) p = (p + 1) % gs_n;
        if (nav & PAD_BUTTON_LEFT) p = (p + gs_n - 1) % gs_n;
        if (nav & PAD_BUTTON_DOWN) p = p + 5 < gs_n ? p + 5 : (p == gs_n - 1 ? 0 : gs_n - 1);
        if (nav & PAD_BUTTON_UP) p = p - 5 >= 0 ? p - 5 : (p == 0 ? gs_n - 1 : 0);
        gs_pos = p;
        if (gs_apps()) ap_sel = p; else gl_sel = gs_idx[p];
    }
    if (gs_apps()) {                            /* applis : A lancer, Y relire la carte, B retour */
        if ((down & PAD_BUTTON_A) && gs_n) gs_app_confirm = gs_idx[gs_pos];
        if (down & PAD_BUTTON_Y) { ap_rescan(); gs_dirty = 1; gl_note("Liste actualis\xC3\xA9" "e"); }
        if (down & PAD_BUTTON_B) { gl_on = 0; if (gl_list_dirty) { gl_list_dirty = 0; gl_list_save(); } }
        return;
    }
    if ((down & PAD_BUTTON_A) && gs_n) gl_launch_sel = gl_sel;                       /* Jouer : "Lancer ce jeu ?" */
    if ((down & PAD_BUTTON_Y) && gs_n) gl_detail = 1;                                 /* fiche du jeu */
    if ((down & PAD_BUTTON_X) && gs_n && !gl[gl_sel].dvd) gl_open_cheats(gl_sel);
    if ((down & PAD_TRIGGER_L) && gs_n && !gl[gl_sel].dvd) {                          /* favori */
        gl[gl_sel].fav = !gl[gl_sel].fav;
        gl_note(gl[gl_sel].fav ? "Ajout\xC3\xA9 aux favoris" : "Retir\xC3\xA9 des favoris");
        gl_fav_save();
        gl_apply_sort();
        gs_dirty = 1;
    }
    if (down & PAD_TRIGGER_R) { gl_view = GLV_LIST; settings_dirty = 1; gl_note(gl_view_name()); }   /* liste */
    if (down & PAD_BUTTON_B) { gl_on = 0; if (gl_list_dirty) { gl_list_dirty = 0; gl_list_save(); } }
}

/* ---------- dessin ---------- */
/* jaquette en perspective (bandes verticales : pas de pli au milieu) : bord gauche (xl, haut, bas), bord droit */
static void gs_tex_quad(GXTexObj *o, f32 u1, f32 v0, f32 v1, f32 xl, f32 tl, f32 bl, f32 xr, f32 tr, f32 br, u8 at, u8 ab) {
    enum { S = 6 };
    gx_set_image(o);
    GX_Begin(GX_QUADS, GX_VTXFMT0, S * 4);
    for (int i = 0; i < S; i++) {
        f32 a = (f32)i / S, b = (f32)(i + 1) / S;
        f32 xa = xl + (xr - xl) * a, xb = xl + (xr - xl) * b;
        f32 ta = tl + (tr - tl) * a, tb = tl + (tr - tl) * b, ba = bl + (br - bl) * a, bb = bl + (br - bl) * b;
        GX_Position2f32(UX(xa), ta); GX_Color4u8(255, 255, 255, at); GX_TexCoord2f32(u1 * a, v0);
        GX_Position2f32(UX(xb), tb); GX_Color4u8(255, 255, 255, at); GX_TexCoord2f32(u1 * b, v0);
        GX_Position2f32(UX(xb), bb); GX_Color4u8(255, 255, 255, ab); GX_TexCoord2f32(u1 * b, v1);
        GX_Position2f32(UX(xa), ba); GX_Color4u8(255, 255, 255, ab); GX_TexCoord2f32(u1 * a, v1);
    }
    GX_End();
}
/* une barre d'aide : rond de couleur + lettre, puis le mot */
static f32 gs_key(f32 x, f32 y, const char *k, const char *label, col_t c) {
    gx_circle(x + 8, y, 8, c, c);
    gx_text_c(F_SMALL, x + 8, y + 4, k, RGBA(24, 20, 40, 255));
    gx_text1(F_SMALL, x + 20, y + 4, TR(label), RGBA(235, 235, 245, 255));
    return x + 20 + text_width(F_SMALL, TR(label), 0) + 16;
}
/* barre du haut : petit logo, filtres comme les onglets d'Aurore, Z, heure */
static void gs_header(void) {
    nv_raw = 1;
    draw_logo_cube(26, 34, 13);                                  /* petit logo : le cube et le nom */
    txt_scale = 0.42f;
    gx_text(F_LOGO, 45, 41, "GC STATION", RGBA(240, 232, 255, 255), RGBA(150, 84, 255, 255), 0);
    txt_scale = 1.0f;
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    char b[40];
    if (tm && EL(EL_CLOCK)) { snprintf(b, sizeof(b), "%02d:%02d", tm->tm_hour, tm->tm_min); gx_text_r(F_MED, 616, 34, b, RGBA(240, 240, 250, 255)); }
    int cur = gs_filter;
    if (gs_fp < -0.5f || !gl_anim) gs_fp = (f32)cur;
    else { f32 k = 1.0f - expf(-ui_dt * 10.0f); gs_fp += (cur - gs_fp) * k; if (fabsf(cur - gs_fp) < 0.002f) gs_fp = (f32)cur; }
    f32 wd[GSF_N], cx[GSF_N], acc = 0;
    for (int i = 0; i < GSF_N; i++) { wd[i] = (f32)text_width(F_MED, TR(gs_filter_names[i]), 0); cx[i] = acc + wd[i] / 2; acc += wd[i] + 46; }
    int i0 = (int)floorf(gs_fp); if (i0 < 0) i0 = 0; if (i0 > GSF_N - 1) i0 = GSF_N - 1;
    int i1 = i0 + 1 < GSF_N ? i0 + 1 : i0;
    f32 fr = gs_fp - i0, center = cx[i0] + (cx[i1] - cx[i0]) * fr;
    scr_scissor(168, 0, 304, 72);
    for (int i = 0; i < GSF_N; i++) {
        f32 x = 320 + (cx[i] - center);
        f32 d = fabsf(x - 320) / 170.0f;
        if (d > 1.2f) continue;
        f32 near = 1 - fabsf(i - gs_fp); if (near < 0) near = 0;
        txt_scale = 1.0f + 0.32f * near;
        f32 tw = wd[i] * txt_scale;
        u8 al = (u8)(255 * (d >= 1 ? 0 : 1 - d * d) * (0.45f + 0.55f * near));
        gx_text1(F_MED, x - tw / 2, 44, TR(gs_filter_names[i]), lerp_c(RGBA(150, 150, 175, al), RGBA(255, 255, 255, al), near));
        txt_scale = 1.0f;
    }
    scr_viewport();
    f32 dw = 5, dg = 7, sel_w = 22, tot = (GSF_N - 1) * (dw + dg) + sel_w, x = 320 - tot / 2;
    for (int i = 0; i < GSF_N; i++) {
        f32 near = 1 - fabsf(i - gs_fp); if (near < 0) near = 0;
        f32 w = dw + (sel_w - dw) * near;
        if (near > 0.05f) au_pill_h(x, 58, w, 4, 2, au_a((u8)(120 + 135 * near)), au_b((u8)(120 + 135 * near)));
        else gx_rrect_fill(x, 58, w, 4, 2, RGBA(255, 255, 255, 46), RGBA(255, 255, 255, 46));
        x += w + dg;
    }
    gx_rrect_fill(512, 30, 22, 18, 5, au_a(235), au_b(235));          /* Z : filtre suivant */
    gx_text_c(F_SMALL, 523, 43, "Z", RGBA(255, 255, 255, 255));
    gx_hgrad(20, 72, 300, 1, RGBA(255, 255, 255, 0), RGBA(255, 255, 255, 34));   /* filet sous la barre */
    gx_hgrad(320, 72, 300, 1, RGBA(255, 255, 255, 34), RGBA(255, 255, 255, 0));
    nv_raw = 0;
}

static void gs_draw_vitrine(void) {
    static const f32 scl[5] = { 1.0f, 0.74f, 0.60f, 0.50f, 0.42f }, off[5] = { 0, 152, 242, 302, 348 };
    static const f32 sq[5] = { 1.0f, 0.82f, 0.78f, 0.76f, 0.74f }, kk[5] = { 1.0f, 0.86f, 0.84f, 0.82f, 0.82f };
    const f32 CW = 150, CH = 200, FLOOR = 302;
    int loop = gs_n >= 7;
    if (gs_cpos < -9000.0f || !gl_anim) gs_cpos = (f32)gs_pos;
    else {
        f32 diff = (f32)gs_pos - gs_cpos;
        if (loop) { while (diff > gs_n / 2.0f) diff -= gs_n; while (diff < -gs_n / 2.0f) diff += gs_n; }
        if (fabsf(diff) > 6.0f) diff = diff > 0 ? 6.0f : -6.0f, gs_cpos = (f32)gs_pos - diff;
        gs_cpos += diff * (1.0f - expf(-ui_dt * 11.0f));
        if (fabsf((f32)gs_pos - gs_cpos) < 0.003f) gs_cpos = (f32)gs_pos;
        if (loop) { while (gs_cpos < 0) gs_cpos += gs_n; while (gs_cpos >= gs_n) gs_cpos -= gs_n; }
    }
    if (now_ms() - last_input_ms > 300) gl_load_big(gl_sel);    /* (grande jaquette du jeu choisi, en fond) */
    uxsave_t uz = ux_native(320);                               /* (16:9 : la vitrine garde ses proportions) */
    nv_raw = 1;
    gx_ellipse(320, FLOOR - 92, 250, 120, au_mix(0.45f, 60), au_mix(0.45f, 0));   /* lueur derriere la vitrine */
    gx_ellipse(320, FLOOR + 6, 210, 22, au_mix(0.5f, 70), au_mix(0.5f, 0));       /* et sur le sol */
    int base = (int)floorf(gs_cpos + 0.5f);
    int order[11], no = 0;
    for (int i = -5; i <= 5; i++) order[no++] = i;
    for (int a = 0; a < no; a++)
        for (int b2 = a + 1; b2 < no; b2++) {
            f32 da = fabsf(base + order[a] - gs_cpos), db = fabsf(base + order[b2] - gs_cpos);
            if (db > da) { int t = order[a]; order[a] = order[b2]; order[b2] = t; }
        }
    for (int oi = 0; oi < no; oi++) {
        int i = order[oi];
        f32 p = base + i - gs_cpos, ad = fabsf(p);
        if (ad > 4.4f) continue;
        int q = base + i;
        if (loop) q = (q % gs_n + gs_n) % gs_n;
        else if (q < 0 || q >= gs_n) continue;
        int k = gs_idx[q];
        int app = gs_apps();
        f32 sc = gl_tab_at(scl, ad), w = CW * sc * gl_tab_at(sq, ad), h = CH * sc, e = gl_tab_at(kk, ad);
        f32 cx = 320 + (p < 0 ? -1 : 1) * gl_tab_at(off, ad);
        f32 near = ad < 1 ? 1 - ad : 0;
        int on = ad < 0.5f;
        /* bords : le plus proche du centre est le plus loin (plus court) */
        f32 xl = cx - w / 2, xr = cx + w / 2;
        f32 hl = p < 0 ? h : h * e, hr = p < 0 ? h * e : h;
        if (ad < 0.001f) hl = hr = h;
        f32 bl = FLOOR - (h - hl) / 2, br = FLOOR - (h - hr) / 2, tl = bl - hl, tr = br - hr;
        GXTexObj *o = NULL;
        int big = 0;
        if (app) {                                               /* appli : son image (lue en fond, une a la fois) */
            if (ap[k].istate == 0 && gj_state == 0 && now_ms() - last_input_ms > 120) ap_post(k);
            if (ap[k].istate == 1 && ap[k].tex) o = &ap[k].obj;
        } else {
            int had = gl[k].post >= 0 && gl_find_slot(gl_cov, GL_SLOTS, k) != NULL;
            big = on && gl[k].post >= 0 && gl_big.game == k && gl_big.tex;
            o = big ? &gl_big.obj : gl[k].post >= 0 ? gl_cover(k, gl_may_load(k)) : NULL;
            if (o && !had && !big) gl_loaded_frame++;
        }
        f32 u1 = app ? 1.0f : big ? 192.0f / 256.0f : 96.0f / 128.0f;
        if (near > 0.05f) gx_glow(xl, tl, w, h, 8, au_mix(0.5f, (u8)(170 * near)), 3);   /* halo rose -> bleu */
        if (o) {
            gs_tex_quad(o, u1, 0.0f, 1.0f, xl, tl, bl, xr, tr, br, 255, 255);
            f32 rf = 0.30f;                                      /* reflet : le bas de l'image, retourne, qui s'efface */
            u8 ra = (u8)(60 + 45 * near);
            gs_tex_quad(o, u1, 1.0f, 1.0f - rf, xl, bl + 3, bl + 3 + hl * rf, xr, br + 3, br + 3 + hr * rf, ra, 0);
        } else if (app) {                                        /* (image pas encore lue) : carte en verre avec le nom */
            gx_rrect_fill(xl, tl, w, h, 10, RGBA(255, 255, 255, 22), RGBA(255, 255, 255, 10));
            gx_rrect_stroke(xl, tl, w, h, 10, 1.0f, RGBA(255, 255, 255, 40));
            char nb[64]; fit_text(F_SMALL, ap[k].name, (int)w - 12, nb, sizeof(nb));
            gx_text_c(F_SMALL, (xl + xr) / 2, (tl + bl) / 2 + 4, nb, RGBA(235, 235, 245, 255));
        } else gl_card_banner(k, xl, tl, w, h, on);
        f32 dm = ad < 1 ? ad * 120.0f : 120.0f + (ad - 1) * 36.0f;   /* les jaquettes de cote s'assombrissent */
        u8 dim = (u8)(dm > 200 ? 200 : dm);
        if (dim) gx_poly4(xl, tl, xr, tr, xr, br, xl, bl, RGBA(6, 6, 14, dim), RGBA(6, 6, 14, dim), RGBA(6, 6, 14, dim), RGBA(6, 6, 14, dim));
        if (near > 0.5f) {                                       /* contour en degrade rose -> bleu */
            gx_rrect_stroke(xl - 3, tl - 3, w + 6, h + 6, 6, 2.0f, au_a((u8)(255 * near)));
            gx_rrect_stroke(xl - 1.5f, tl - 1.5f, w + 3, h + 3, 5, 1.0f, au_b((u8)(200 * near)));
            if (!app && !gl[k].dvd) cp_badge(gl[k].id, xr - 8, tl + 8, 5.0f);
        }
        if (!app) gp_zone(xl, tl, w, h, &gl_sel, k, GPZ_SEL);
    }
    nv_raw = 0;
    ux_restore(uz);
}

static void gs_draw(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    gs_check();
    /* une banniere a lire par image, quand on ne bouge pas (jeux autour du choisi) */
    gl_job_done();
    if (gs_n && now_ms() - last_input_ms > 150 && gj_state == 0)
        for (int j = 0; j <= 8; j++) {
            int q = (gs_pos + (j & 1 ? (j + 1) / 2 : -(j / 2)) + gs_n * 4) % gs_n, k = gs_idx[q];
            if (gl[k].bstate == 0 || (gl[k].bstate == 1 && !gl_banner(k) && gl[k].post < 0)) { gl_job_post(GJ_BANNER, k); break; }
        }
    gs_header();
    nv_raw = 1;
    if (!gs_n) {                                                 /* filtre vide */
        static const char *const e[GSF_N] = {
            "Aucun jeu lanc\xC3\xA9 pour l'instant", "Aucun jeu GameCube sur la carte SD",
            "Aucun favori : L sur un jeu pour l'ajouter", "Aucun jeu jou\xC3\xA9 pour l'instant", "Aucun jeu GameCube sur la carte SD",
            "Aucune appli : SD:/apps/<nom>/boot.dol (avec icon.png)" };
        gx_text_c(F_LARGE, 320, 230, TR(e[gs_filter]), RGBA(255, 255, 255, 255));
        gx_text_c(F_SMALL, 320, 258, TR("Z : autre filtre"), RGBA(170, 170, 195, 255));
    } else if (gs_apps()) {
        gs_draw_vitrine();
        nv_raw = 1;
        ap_ent *a = &ap[gs_idx[gs_pos]];
        fit_text(F_LARGE, a->name, 560, buf, sizeof(buf));
        gx_text_c(F_LARGE, 320, 392, buf, RGBA(255, 255, 255, 255));
        if (a->desc[0]) fit_text(F_SMALL, a->desc, 520, d, sizeof(d));
        else { gl_size_txt(a->size, buf, sizeof(buf)); snprintf(d, sizeof(d), "%s  \xC2\xB7  %s", TR("Appli homebrew"), buf); }
        gx_text_c(F_SMALL, 320, 412, d, RGBA(185, 185, 210, 255));
        snprintf(d, sizeof(d), "%d / %d", gs_pos + 1, gs_n);
        gx_text_r(F_SMALL, 616, 412, d, RGBA(130, 130, 155, 255));
    } else {
        gs_draw_vitrine();
        nv_raw = 1;
        gcg_t *g = &gl[gl_sel];
        gl_title_of(gl_sel, d, sizeof(d));
        fit_text(F_LARGE, d, 560, buf, sizeof(buf));
        gx_text_c(F_LARGE, 320, 392, buf, RGBA(255, 255, 255, 255));
        if (g->dvd) snprintf(d, sizeof(d), "%s", TR("Lecteur de la console"));
        else {
            char sz[32]; gl_size_txt(g->size, sz, sizeof(sz));
            const char *reg = g->id[3] == 'E' ? "USA" : g->id[3] == 'P' ? "Europe" : g->id[3] == 'J' ? "Japon" : g->id[3] == 'F' ? "France" : g->id[3] == 'D' ? "Allemagne" : "";
            int o = snprintf(d, sizeof(d), "%s  \xC2\xB7  %s%s%s", g->id, TR(reg), reg[0] ? "  \xC2\xB7  " : "", sz);
            if (g->play >= 3600 && o > 0) snprintf(d + o, sizeof(d) - o, "  \xC2\xB7  %u h %s", (unsigned)(g->play / 3600), TR("jou\xC3\xA9" "es"));
            else if (g->play >= 60 && o > 0) snprintf(d + o, sizeof(d) - o, "  \xC2\xB7  %u min %s", (unsigned)(g->play / 60), TR("jou\xC3\xA9" "es"));
        }
        f32 iw = (f32)text_width(F_SMALL, d, 0);
        gx_text_c(F_SMALL, 320 + (g->fav ? 9 : 0), 412, d, RGBA(185, 185, 210, 255));
        if (g->fav) draw_star1(320 - iw / 2 - 4, 408, 5.5f, RGBA(255, 210, 90, 255));
        snprintf(d, sizeof(d), "%d / %d", gs_pos + 1, gs_n);
        gx_text_r(F_SMALL, 616, 412, d, RGBA(130, 130, 155, 255));
    }
    if (now_ms() - gl_msg_t < 3500) gx_text_c(F_SMALL, 320, 428, gl_msg, RGBA(255, 210, 120, 255));
    /* barre d'aide en verre */
    gx_rrect_fill(20, 438, 600, 30, 15, RGBA(255, 255, 255, 16), RGBA(255, 255, 255, 8));
    gx_rrect_stroke(20, 438, 600, 30, 15, 1.0f, RGBA(255, 255, 255, 34));
    gx_hgrad(170, 438, 150, 1.2f, au_a(0), au_a(140));
    gx_hgrad(320, 438, 150, 1.2f, au_b(140), au_b(0));
    f32 x = 34;
    if (gs_apps()) {
        x = gs_key(x, 453, "A", "Lancer", RGBA(110, 220, 140, 255));
        x = gs_key(x, 453, "Y", "Actualiser", RGBA(235, 235, 245, 255));
        x = gs_key(x, 453, "Z", "Filtre", au_a(255));
        gs_key(x, 453, "B", "Retour", RGBA(240, 110, 110, 255));
        nv_raw = 0;
        if (gs_app_confirm >= 0 && gs_app_confirm < ap_n) {     /* "Lancer cette appli ?" */
            const f32 w = 420, h = 110, bx = 110, by = 170;
            gx_rrect_fill(bx, by, w, h, 12, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
            draw_panel(bx, by, w, h);
            gx_text_c(F_MED, bx + w / 2, by + 34, TR("Lancer cette appli ?"), RGBA(255, 255, 255, 255));
            fit_text(F_SMALL, ap[gs_app_confirm].name, (int)w - 30, buf, sizeof(buf));
            gx_text_c(F_SMALL, bx + w / 2, by + 58, buf, RGBA(255, 210, 120, 255));
            gx_text_c(F_SMALL, bx + w / 2, by + 92, TR("A : lancer     B : annuler"), RGBA(255, 210, 120, 255));
        }
        return;
    }
    if (gs_n) {                                                                /* v198 : (filtre vide : seulement Z, R, B) */
        x = gs_key(x, 453, "A", "Jouer", RGBA(110, 220, 140, 255));
        x = gs_key(x, 453, "Y", "Fiche", RGBA(235, 235, 245, 255));
    }
    if (gs_n && !(gl_sel >= 0 && gl_sel < gl_n && gl[gl_sel].dvd)) {           /* v198 : (pas de cheats ni de favori pour le lecteur) */
        x = gs_key(x, 453, "X", "Cheats", RGBA(235, 235, 245, 255));
        x = gs_key(x, 453, "L", "Favori", RGBA(255, 210, 90, 255));
    }
    x = gs_key(x, 453, "Z", "Filtre", au_a(255));
    x = gs_key(x, 453, "R", "Liste", RGBA(235, 235, 245, 255));
    gs_key(x, 453, "B", "Retour", RGBA(240, 110, 110, 255));
    nv_raw = 0;
    if (gl_launch_sel >= 0) gl_draw_launch_box();                /* "Lancer ce jeu ?" (gclib.c) */
}

/* ---------- v197 : la fiche d'un jeu (Y), dans l'espace ----------
   A gauche la grande jaquette (lueur d'Aurore, reflet) ; a droite le nom, des etiquettes (ID, region, taille,
   disque, favori), la description, la capture du jeu et ce qu'on sait de lui (lancements, temps de jeu, cheats).
   Gauche / droite : jeu suivant du filtre, la fiche glisse. Les touches restent celles de gclib.c. */
static int gs_step(int d) {                     /* (fiche) jeu suivant / precedent du filtre ; rend gl_sel */
    gs_check();
    if (gs_apps() || gs_n <= 0) { gl_sel = nav_wrap(gl_sel, gl_n, 1, d, 0); return gl_sel; }
    gs_pos = (gs_pos + d + gs_n) % gs_n;
    gl_sel = gs_idx[gs_pos];
    return gl_sel;
}
static f32 gs_chip(f32 x, f32 y, const char *t, col_t c) {   /* etiquette arrondie ; rend la fin */
    f32 w = (f32)text_width(F_SMALL, t, 0) + 18;
    gx_rrect_fill(x, y, w, 20, 10, RGBA(255, 255, 255, 22), RGBA(255, 255, 255, 12));
    gx_rrect_stroke(x, y, w, 20, 10, 1.0f, RGBA(255, 255, 255, 40));
    gx_text1(F_SMALL, x + 9, y + 15, t, c);
    return x + w + 6;
}
static void gs_draw_fiche(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    if (gl_sel < 0 || gl_sel >= gl_n) return;
    gcg_t *g = &gl[gl_sel];
    gl_job_done();
    if (now_ms() - last_input_ms > 100) gl_load_big(gl_sel);     /* (grande jaquette et capture, en fond) */
    if (g->bstate == 0 && gj_state == 0 && now_ms() - last_input_ms > 150) gl_job_post(GJ_BANNER, gl_sel);   /* (description) */
    gs_header();
    gl_slide *= gl_anim ? expf(-ui_dt * 11.0f) : 0.0f;
    if (fabsf(gl_slide) < 0.004f) gl_slide = 0;
    const f32 ox = gl_slide * 120.0f;
    nv_raw = 1;
    /* jaquette */
    const f32 cw = 180, chh = 240, cx = 40 + ox * 0.6f, cy = 96;
    gx_ellipse(cx + cw / 2, cy + chh / 2, 170, 150, au_mix(0.45f, 55), au_mix(0.45f, 0));
    gx_glow(cx, cy, cw, chh, 10, au_mix(0.5f, 170), 3);
    GXTexObj *o = NULL; int big = 0;
    if (g->dvd) gl_card_dvd(cx, cy, cw, chh, 1);
    else if (g->post >= 0 && gl_big.game == gl_sel && gl_big.tex) { o = &gl_big.obj; big = 1; }
    else if (g->post >= 0) o = gl_cover(gl_sel, 1);              /* (la petite en attendant la grande) */
    if (o) {
        f32 u1 = big ? 192.0f / 256.0f : 96.0f / 128.0f;
        gs_tex_quad(o, u1, 0.0f, 1.0f, cx, cy, cy + chh, cx + cw, cy, cy + chh, 255, 255);
        gs_tex_quad(o, u1, 1.0f, 0.72f, cx, cy + chh + 3, cy + chh + 3 + chh * 0.28f, cx + cw, cy + chh + 3, cy + chh + 3 + chh * 0.28f, 80, 0);
    } else if (!g->dvd) gl_card_banner(gl_sel, cx, cy, cw, chh, 1);
    gx_rrect_stroke(cx - 3, cy - 3, cw + 6, chh + 6, 6, 2.0f, au_a(255));
    gx_rrect_stroke(cx - 1.5f, cy - 1.5f, cw + 3, chh + 3, 5, 1.0f, au_b(200));
    /* a droite */
    const f32 rx = 250 + ox; const int rw = 366;
    if (g->dvd) {
        gx_text1(F_LARGE, rx, 122, TR("Disque de la console"), RGBA(255, 255, 255, 255));
        gl_wrap(F_SMALL, "Lance le jeu qui est dans le lecteur de la GameCube, comme le BIOS de la console : "
                         "le vrai lecteur lit le jeu (pas la carte SD). Mets le disque et ferme le couvercle avant.", rx, 154, rw, 18, 5, RGBA(215, 215, 235, 255));
        gl_wrap(F_SMALL, "Pas de A + Z + START dans ce mode : pour revenir, red\xC3\xA9marre la console.", rx, 254, rw, 18, 2, RGBA(170, 170, 195, 255));
    } else {
        int nl = gl_wrap(F_LARGE, g->title, rx, 118, rw, 26, 2, RGBA(255, 255, 255, 255));
        f32 y = 118 + nl * 26 - 12;
        const char *reg = g->id[3] == 'E' ? "USA" : g->id[3] == 'P' ? "Europe" : g->id[3] == 'J' ? "Japon" : g->id[3] == 'F' ? "France" : g->id[3] == 'D' ? "Allemagne" : "";
        f32 x = rx;
        x = gs_chip(x, y, g->id, RGBA(255, 210, 120, 255));
        if (reg[0]) x = gs_chip(x, y, TR(reg), RGBA(235, 235, 245, 255));
        char sz[32]; gl_size_txt(g->size, sz, sizeof(sz));
        x = gs_chip(x, y, sz, RGBA(235, 235, 245, 255));
        if (g->disc) { snprintf(buf, sizeof(buf), "%s %d", TR("Disque"), g->disc + 1); x = gs_chip(x, y, buf, RGBA(235, 235, 245, 255)); }
        if (g->ciso) x = gs_chip(x, y, "CISO", RGBA(235, 235, 245, 255));
        if (g->fav) { f32 x0 = x; x = gs_chip(x, y, "    Favori", RGBA(255, 210, 90, 255)); draw_star1(x0 + 13, y + 10, 5.0f, RGBA(255, 210, 90, 255)); }
        y += 40;
        int has_st = g->still >= 0 && gl_bigst.game == gl_sel && gl_bigst.tex;
        if (g->desc) gl_wrap(F_SMALL, g->desc, rx, y, rw, 17, has_st ? 4 : 9, RGBA(210, 210, 230, 255));
        else gx_text1(F_SMALL, rx, y, g->bstate == 0 ? "Lecture de la banni\xC3\xA8re..." : "Pas de description pour ce jeu.", RGBA(160, 160, 185, 255));
        /* capture du jeu, et a cote : lancements, temps de jeu, cheats */
        f32 sy = 268, ix = rx;
        if (has_st) {
            const f32 sw = 160, sh = 120;
            gx_rrect_fill(rx - 2, sy - 2, sw + 4, sh + 4, 8, RGBA(255, 255, 255, 30), RGBA(255, 255, 255, 18));
            draw_tex_fit(&gl_bigst.obj, 320, 240, 320, 240, rx, sy, sw, sh);
            ix = rx + sw + 16;
        }
        f32 ly = sy + 14;
        if (g->runs) {
            snprintf(buf, sizeof(buf), g->runs == 1 ? TR("Lanc\xC3\xA9 1 fois") : TR("Lanc\xC3\xA9 %u fois"), (unsigned)g->runs);
            gx_text1(F_SMALL, ix, ly, buf, RGBA(235, 235, 245, 255)); ly += 20;
            time_t lt = (time_t)g->last;
            struct tm *tm = g->last ? localtime(&lt) : NULL;
            if (tm) { snprintf(buf, sizeof(buf), "%s %02d/%02d/%04d", TR("Derni\xC3\xA8re partie :"), tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900); gx_text1(F_SMALL, ix, ly, buf, RGBA(185, 185, 210, 255)); ly += 20; }
        } else { gx_text1(F_SMALL, ix, ly, TR("Jamais lanc\xC3\xA9 d'ici"), RGBA(185, 185, 210, 255)); ly += 20; }
        if (g->play >= 60) {
            if (g->play >= 3600) snprintf(buf, sizeof(buf), "%s %u h %02u", TR("Temps de jeu :"), (unsigned)(g->play / 3600), (unsigned)(g->play / 60 % 60));
            else snprintf(buf, sizeof(buf), "%s %u min", TR("Temps de jeu :"), (unsigned)(g->play / 60));
            gx_text1(F_SMALL, ix, ly, buf, RGBA(185, 185, 210, 255)); ly += 20;
        }
        int cn = now_ms() - last_input_ms > 150 || !gl_anim ? gl_cheats_on(gl_sel) : -2;
        if (cn >= -1) {
            if (cn > 0) snprintf(buf, sizeof(buf), cn == 1 ? TR("Cheats : 1 activ\xC3\xA9") : TR("Cheats : %d activ\xC3\xA9s"), cn);
            else snprintf(buf, sizeof(buf), "%s", cn == 0 ? TR("Cheats : aucun activ\xC3\xA9") : TR("Pas de fichier de cheats"));
            gx_text1(F_SMALL, ix, ly, buf, cn > 0 ? RGBA(130, 230, 150, 255) : RGBA(185, 185, 210, 255)); ly += 20;
        }
        cp_badge(g->id, cx + cw - 9, cy + 9, 6.0f);
        text_display(g->path, d, sizeof(d));
        fit_text(F_SMALL, d, rw, buf, sizeof(buf));
        gx_text1(F_SMALL, rx, 404, buf, RGBA(120, 120, 145, 255));
    }
    if (gs_n && !gs_apps()) { snprintf(d, sizeof(d), "%d / %d", gs_pos + 1, gs_n); gx_text_r(F_SMALL, 616, 404, d, RGBA(130, 130, 155, 255)); }
    if (now_ms() - gl_msg_t < 3500) gx_text_c(F_SMALL, 320, 428, gl_msg, RGBA(255, 210, 120, 255));
    gx_rrect_fill(20, 438, 600, 30, 15, RGBA(255, 255, 255, 16), RGBA(255, 255, 255, 8));
    gx_rrect_stroke(20, 438, 600, 30, 15, 1.0f, RGBA(255, 255, 255, 34));
    gx_hgrad(170, 438, 150, 1.2f, au_a(0), au_a(140));
    gx_hgrad(320, 438, 150, 1.2f, au_b(140), au_b(0));
    f32 x = 34;
    x = gs_key(x, 453, "A", "Jouer", RGBA(110, 220, 140, 255));
    if (!g->dvd) {
        x = gs_key(x, 453, "X", "Cheats", RGBA(235, 235, 245, 255));
        x = gs_key(x, 453, "Y", "R\xC3\xA9glages", RGBA(235, 235, 245, 255));
        x = gs_key(x, 453, "L", "Favori", RGBA(255, 210, 90, 255));
        x = gs_key(x, 453, "R", "Analyse", RGBA(235, 235, 245, 255));
        x = gs_key(x, 453, "\xE2\x86\x93", "Musiques", RGBA(150, 200, 255, 255));   /* v207 : jukebox.c */
    }
    gs_key(x, 453, "B", "Retour", RGBA(240, 110, 110, 255));
    nv_raw = 0;
    if (gl_launch_sel >= 0) gl_draw_launch_box();
    nat_draw_qr();                                           /* (rapport d'analyse sur le telephone) */
}

// GC Station v102 : style « Aurore » (START > Reglages > Affichage > Style : Aurore). Inclus dans main.c
// apres ui_nova.c (il en reprend la selection qui glisse et la transition). Seul le visuel change.
//  - noir profond, avec des aurores (rubans de lumiere rose, violet, bleu) qui ondulent lentement ;
//  - en-tete : le nom de l'onglet en grand au centre, les onglets voisins a cote qui s'effacent vers
//    les bords ; changer d'onglet fait defiler les noms (facon Apple TV) ; points d'avancement dessous ;
//  - cadres presque invisibles (verre tres fin, filet lumineux en haut), pas de cadre pour la legende ;
//  - selection en degrade rose -> bleu qui glisse, page qui apparait en zoom doux.

static col_t au_a(u8 a) {                                /* rose (Indigo) ; sinon l'accent du theme */
    if (ui_skin == 5) return halo_acc(a);                /* v129 : Halo : la couleur de l'onglet */
    if (ui_skin == 4 && theme_id == THEME_INDIGO) return RGBA(255, 79, 176, a);   /* v129 : Nebula : rose */
    if (theme_id == THEME_INDIGO) return RGBA(255, 70, 170, a);
    return RGBA(mod_acc[0], mod_acc[1], mod_acc[2], a);
}
static col_t au_b(u8 a) {                                /* bleu (Indigo) ; sinon l'accent plus sombre et plus bleu */
    if (ui_skin == 5) { col_t c = halo_acc(a); c.r = (u8)(c.r * 3 / 5); c.g = (u8)(c.g * 3 / 5); c.b = (u8)(c.b * 3 / 5); return c; }
    if (ui_skin == 4 && theme_id == THEME_INDIGO) return RGBA(47, 211, 255, a);   /* v129 : Nebula : cyan */
    if (theme_id == THEME_INDIGO) return RGBA(70, 110, 255, a);
    return RGBA(mod_acc[0] / 2, mod_acc[1] * 2 / 3, mod_acc[2] / 2 + 110, a);
}
static col_t au_mix(f32 t, u8 a) {                       /* rose -> violet -> bleu -> cyan */
    static const u8 p[4][3] = { { 255, 70, 170 }, { 160, 80, 255 }, { 70, 110, 255 }, { 40, 210, 255 } };
    if (theme_id != THEME_INDIGO) { col_t x = au_a(a), y = au_b(a); return lerp_c(x, y, t); }
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    f32 s = t * 3; int i = (int)s; if (i > 2) i = 2; f32 f = s - i;
    return RGBA(p[i][0] + (p[i + 1][0] - p[i][0]) * f, p[i][1] + (p[i + 1][1] - p[i][1]) * f, p[i][2] + (p[i + 1][2] - p[i][2]) * f, a);
}
/* pastille avec un degrade horizontal (deux bouts ronds + le milieu) */
static void au_pill_h(f32 x, f32 y, f32 w, f32 h, f32 r, col_t l, col_t rr) {
    if (r > h / 2) r = h / 2;
    if (w < 2 * r + 1) { gx_rrect_fill(x, y, w, h, r, l, rr); return; }
    gx_rrect_fill(x, y, 2 * r, h, r, l, l);
    gx_rrect_fill(x + w - 2 * r, y, 2 * r, h, r, rr, rr);
    gx_hgrad(x + r, y, w - 2 * r, h, l, rr);
}

/* ---------- fond : noir et aurores ---------- */
/* v120 : points et couleurs calcules une fois chacun (avant : deux fois), un seul envoi au GX par ruban
   (avant : 48 petits envois) */
static void au_ribbon(f32 base, f32 amp, f32 th, f32 t, f32 sp, f32 ph, u8 alpha) {
    enum { N = 24 };
    f32 xs[N + 1], ys[N + 1];
    col_t cs[N + 1], zs[N + 1];
    for (int i = 0; i <= N; i++) {
        f32 x = -10 + i * 660.0f / N;
        xs[i] = x;
        ys[i] = base + amp * sinf(x * 0.012f + t * sp + ph) + 0.4f * amp * sinf(x * 0.031f - t * sp * 1.7f);
        cs[i] = au_mix((f32)i / N, alpha);
        zs[i] = cs[i]; zs[i].a = 0;
    }
    gx_set_flat();
    GX_Begin(GX_QUADS, GX_VTXFMT0, N * 8);
    for (int i = 0; i < N; i++) {
        f32 x0 = xs[i], x1 = xs[i + 1], y0 = ys[i], y1 = ys[i + 1];
        vtx(x0, y0 - th, zs[i]); vtx(x1, y1 - th, zs[i + 1]); vtx(x1, y1, cs[i + 1]); vtx(x0, y0, cs[i]);   /* (fondu vers le haut) */
        vtx(x0, y0, cs[i]); vtx(x1, y1, cs[i + 1]); vtx(x1, y1 + th * 0.35f, zs[i + 1]); vtx(x0, y0 + th * 0.35f, zs[i]);
    }
    GX_End();
}
/* v129 : Nebula : trois grandes lueurs floues (rose, cyan, violet) qui derivent lentement */
static void nb_blob(f32 x, f32 y, f32 r, col_t c) { col_t z = c; z.a = 0; gx_circle(x, y, r, c, z); }
static void nb_background(void) {
    nv_raw = 1;
    gx_quad4(0, 0, 640, 480, RGBA(7, 5, 16, 255), RGBA(6, 4, 15, 255), RGBA(5, 3, 12, 255), RGBA(6, 4, 14, 255));
    f32 t = (f32)(now_ms() % 3600000) / 1000.0f;
    nb_blob(70 + 30 * sinf(t * 0.11f), 40 + 20 * cosf(t * 0.13f), 260, au_a(120));
    nb_blob(560 + 25 * cosf(t * 0.09f), 60 + 25 * sinf(t * 0.12f), 240, au_b(95));
    nb_blob(330 + 40 * sinf(t * 0.07f), 470 + 15 * cosf(t * 0.1f), 280, RGBA(122, 60, 255, 110));
    nv_raw = 0;
}
/* v129 : Halo : noir profond, deux halos de la couleur de l'onglet */
static void hl_background(void) {
    nv_raw = 1;
    gx_rect(0, 0, 640, 480, RGBA(3, 3, 6, 255));
    f32 t = (f32)(now_ms() % 3600000) / 1000.0f;
    nb_blob(520 + 20 * sinf(t * 0.1f), -30, 300, halo_acc(70));
    nb_blob(60, 520 + 15 * cosf(t * 0.12f), 260, halo_acc(40));
    nv_raw = 0;
}
static void au_background(void) {
    if (ui_skin == 4) { nb_background(); return; }
    if (ui_skin == 5) { hl_background(); return; }
    nv_raw = 1;
    gx_quad4(0, 0, 640, 480, RGBA(6, 6, 12, 255), RGBA(7, 6, 14, 255), RGBA(3, 3, 6, 255), RGBA(4, 3, 8, 255));
    f32 t = (f32)(now_ms() % 3600000) / 1000.0f;
    au_ribbon(118, 16, 70, t, 0.35f, 0.0f, 40);
    au_ribbon(430, 22, 110, t, 0.25f, 2.1f, 34);
    au_ribbon(460, 12, 60, t, 0.42f, 4.0f, 22);
    nv_raw = 0;
}

/* ---------- cadres ---------- */
static void au_panel(f32 x, f32 y, f32 w, f32 h) {
    if (ui_skin == 4) {                                    /* v129 : Nebula : verre plus present, bord et reflet */
        nv_raw = 1;
        int wa = wall_active();
        gx_rrect_fill(x, y, w, h, 18, RGBA(255, 255, 255, wa ? 34 : 18), RGBA(255, 255, 255, wa ? 28 : 11));
        gx_rrect_stroke(x, y, w, h, 18, 1.0f, RGBA(255, 255, 255, 34));
        gx_hgrad(x + 18, y + 1, w - 36, 1.0f, RGBA(255, 255, 255, 70), RGBA(255, 255, 255, 10));
        nv_raw = 0;
        return;
    }
    if (ui_skin == 5) {                                    /* v129 : Halo : carte noire ; la grande carte (droite) a le contour lumineux */
        nv_raw = 1;
        int lit = x > 200 || w > 400;
        if (lit) gx_glow(x, y, w, h, 16, halo_acc(40), 3);
        gx_rrect_fill(x, y, w, h, 16, RGBA(11, 10, 20, 240), RGBA(11, 10, 20, 240));
        gx_rrect_stroke(x, y, w, h, 16, lit ? 1.5f : 1.0f, lit ? halo_acc(230) : RGBA(255, 255, 255, 22));
        nv_raw = 0;
        return;
    }
    nv_raw = 1;
    int wa = wall_active();
    gx_rrect_fill(x, y, w, h, 22, RGBA(255, 255, 255, wa ? 30 : 9), RGBA(255, 255, 255, wa ? 34 : 5));
    gx_rrect_stroke(x, y, w, h, 22, 1.0f, RGBA(255, 255, 255, 18));
    f32 lw = w * 0.45f;                                    /* filet lumineux en haut */
    gx_hgrad(x + w / 2 - lw, y, lw, 1.2f, au_a(0), au_a(150));
    gx_hgrad(x + w / 2, y, lw, 1.2f, au_b(150), au_b(0));
    nv_raw = 0;
}
static void au_legend_box(f32 x, f32 w) {
    if (ui_skin == 4) {                                    /* v129 : Nebula : legende dans une pastille de verre */
        nv_raw = 1;
        gx_rrect_fill(x + 6, 421, w - 12, 34, 17, RGBA(10, 6, 24, 150), RGBA(10, 6, 24, 150));
        gx_rrect_stroke(x + 6, 421, w - 12, 34, 17, 1.0f, RGBA(255, 255, 255, 34));
        nv_raw = 0;
        return;
    }
    nv_raw = 1;
    gx_hgrad(x + 20, 419, w / 2 - 20, 1, RGBA(255, 255, 255, 0), RGBA(255, 255, 255, 30));
    gx_hgrad(x + w / 2, 419, w / 2 - 20, 1, RGBA(255, 255, 255, 30), RGBA(255, 255, 255, 0));
    nv_raw = 0;
}
static int au_draw_tabs(const char *count) {             /* (le nom de l'onglet est deja en grand en haut) */
    nv_raw = 1;
    if (count) gx_text_r(F_SMALL, IS_PLAYER_TAB(tab) ? 266 : 614, 101, count, RGBA(140, 140, 160, 255));
    nv_raw = 0;
    return 26;
}

/* ---------- en-tete : noms des onglets qui defilent ---------- */
static f32 au_p = -1;                                    /* position (rang de l'onglet, en douceur) */
static void hl_header(void);
static void au_header(void) {
    if (ui_skin == 5) { hl_header(); return; }
    nv_raw = 1;
    draw_logo_cube(22, 22, 9);
    gx_text1(F_SMALL, 36, 26, "GC Station", RGBA(150, 150, 170, 255));
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    char b[40];
    if (tm && EL(EL_CLOCK)) { snprintf(b, sizeof(b), "%02d:%02d", tm->tm_hour, tm->tm_min); gx_text_r(F_MED, 616, 30, b, RGBA(240, 240, 250, 255)); }
    if (net_phase == NET_DONE) {
        col_t nc = net_ok ? RGBA(70, 230, 160, 255) : RGBA(255, 140, 90, 255);
        gx_circle(612, 44, 2.6f, nc, nc);
        gx_text_r(F_SMALL, 604, 48, net_ok ? TR("En ligne") : TR("Hors ligne"), RGBA(120, 120, 140, 255));
    }
    if (rec_fp) { gx_circle(22, 52, 4, RGBA(255, 70, 70, 255), RGBA(255, 70, 70, 255)); gx_text1(F_SMALL, 30, 56, "REC", RGBA(255, 120, 120, 255)); }
    /* rangs des onglets affiches */
    int ids[TAB_COUNT], n = 0, cur = 0;
    FOR_TABS(t) if (tab_shown(t)) { if (t == tab) cur = n; ids[n++] = t; }
    if (au_p < -0.5f || !gl_anim) au_p = (f32)cur;
    else { f32 k = 1.0f - expf(-ui_dt * 10.0f); au_p += (cur - au_p) * k; if (fabsf(cur - au_p) < 0.002f) au_p = (f32)cur; }
    /* noms : chacun a sa place sur une rangee (largeurs reelles), la rangee glisse pour mettre
       l'onglet choisi au centre ; plus gros et plus clair au centre */
    f32 wd[TAB_COUNT], cx[TAB_COUNT], acc = 0;
    static f32 wc[TAB_COUNT]; static const char *wk[TAB_COUNT]; static f32 wk_ux;   /* (v120 : largeurs gardees) */
    if (wk_ux != ux_k) { memset(wk, 0, sizeof(wk)); wk_ux = ux_k; }
    for (int i = 0; i < n; i++) {
        const char *nm = TR(nv_tab_names[ids[i]]);
        if (wk[ids[i]] != nm) { wk[ids[i]] = nm; wc[ids[i]] = (f32)text_width(F_MED, nm, 0); }
        wd[i] = wc[ids[i]]; cx[i] = acc + wd[i] / 2; acc += wd[i] + 46;
    }
    int i0 = (int)floorf(au_p); if (i0 < 0) i0 = 0; if (i0 > n - 1) i0 = n - 1;
    int i1 = i0 + 1 < n ? i0 + 1 : i0;
    f32 fr = au_p - i0, center = cx[i0] + (cx[i1] - cx[i0]) * fr;
    scr_scissor(130, 0, 380, 72);
    for (int i = 0; i < n; i++) {
        f32 x = 320 + (cx[i] - center);
        f32 d = fabsf(x - 320) / 190.0f;                   /* (0 au centre, 1 au bord) */
        if (d > 1.2f) continue;
        f32 near = 1 - fabsf(i - au_p); if (near < 0) near = 0;
        txt_scale = 1.0f + 0.32f * near;
        f32 tw = wd[i] * txt_scale;
        u8 al = (u8)(255 * (d >= 1 ? 0 : 1 - d * d) * (0.45f + 0.55f * near));
        col_t c = lerp_c(RGBA(150, 150, 175, al), RGBA(255, 255, 255, al), near);
        gx_text1(F_MED, x - tw / 2, 44, TR(nv_tab_names[ids[i]]), c);
        txt_scale = 1.0f;
        gp_zone(x - tw / 2 - 6, 18, tw + 12, 32, NULL, ids[i], GPZ_TAB);
    }
    scr_viewport();
    /* points d'avancement : le choisi s'allonge en degrade */
    f32 dw = 5, dg = 7, sel_w = 22, tot = (n - 1) * (dw + dg) + sel_w, x = 320 - tot / 2;
    for (int i = 0; i < n; i++) {
        f32 near = 1 - fabsf(i - au_p); if (near < 0) near = 0;
        f32 w = dw + (sel_w - dw) * near;
        if (near > 0.05f) au_pill_h(x, 58, w, 4, 2, au_a((u8)(120 + 135 * near)), au_b((u8)(120 + 135 * near)));
        else gx_rrect_fill(x, 58, w, 4, 2, RGBA(255, 255, 255, 46), RGBA(255, 255, 255, 46));
        x += w + dg;
    }
    nv_raw = 0;
}

/* ---------- selection (degrade rose -> bleu, lueur) ---------- */
static void au_sel_paint(f32 x, f32 y, f32 w, f32 h, f32 r) {
    if (ui_skin == 5) {                                    /* v129 : Halo : contour de la couleur de l'onglet, fond teinte, lueur */
        gx_glow(x, y, w, h, r, halo_acc(50), 3);
        gx_rrect_fill(x, y, w, h, r, halo_acc(44), halo_acc(30));
        gx_rrect_stroke(x, y, w, h, r, 1.5f, halo_acc(255));
        return;
    }
    gx_glow(x, y, w, h, r, au_a(60), 3);
    au_pill_h(x, y, w, h, r, au_a(235), au_b(235));
    if (w > 30) gx_hgrad(x + r, y + 1, w - 2 * r, 1, RGBA(255, 255, 255, 80), RGBA(255, 255, 255, 10));
}

/* ---------- v103 : menu START : il monte et grandit en s'ouvrant (260 ms), le fond s'assombrit ---------- */
static int au_menu_was = 0;
static u64 au_menu_t0 = 0;
static int au_menu_mtx = 0;
static void au_menu_begin(void) {
    if (!au_menu_was) au_menu_t0 = now_ms();
    au_menu_was = 1;
    if (ui_skin < 3) return;
    f32 p = gl_anim ? (now_ms() - au_menu_t0) / 260.0f : 1.0f;
    if (p > 1) p = 1;
    f32 e = 1 - (1 - p) * (1 - p) * (1 - p);
    nv_raw = 1;
    gx_rect(0, 0, 640, 480, RGBA(2, 2, 6, (u8)(120 * e)));
    nv_raw = 0;
    if (e < 1) {
        f32 s = 0.94f + 0.06f * e, cx = UX(320), cy = 232, dy = (1 - e) * 28;
        Mtx m;
        guMtxIdentity(m);
        m[0][0] = s; m[1][1] = s; m[0][3] = cx * (1 - s); m[1][3] = cy * (1 - s) + dy;
        GX_LoadPosMtxImm(m, GX_PNMTX0);
        au_menu_mtx = 1;
    }
}
static void au_menu_end(void) {
    if (!au_menu_mtx) return;
    Mtx m;
    guMtxIdentity(m);
    GX_LoadPosMtxImm(m, GX_PNMTX0);
    au_menu_mtx = 0;
}
static void au_menu_closed(void) { au_menu_was = 0; }

/* ---------- v129 : Halo : nom de l'onglet en grand a gauche (point de sa couleur), icones des onglets ---------- */
static void hl_header(void) {
    nv_raw = 1;
    const char *nm = TR(nv_tab_names[tab >= 0 && tab < TAB_COUNT ? tab : 0]);
    gx_text1(F_LARGE, 22, 48, nm, RGBA(255, 255, 255, 255));
    f32 tw = (f32)text_width(F_LARGE, nm, 0);
    gx_circle(22 + tw + 7, 45, 3.4f, halo_acc(255), halo_acc(255));
    gx_circle_glow(22 + tw + 7, 45, 3.4f, halo_acc(90), 2);
    /* icones des onglets affiches : l'onglet choisi dans une pastille de sa couleur */
    int ids[TAB_COUNT], n = 0;
    FOR_TABS(t) if (tab_shown(t)) ids[n++] = t;
    f32 step = 26, x0 = 600 - n * step + step / 2;
    if (x0 < 22 + tw + 40) { step = (600 - (22 + tw + 40)) / (f32)(n ? n : 1); x0 = 22 + tw + 40 + step / 2; }
    for (int i = 0; i < n; i++) {
        f32 cx = x0 + i * step, cy = 54;
        if (ids[i] == tab) {
            gx_glow(cx - 11, cy - 11, 22, 22, 7, halo_acc(70), 2);
            gx_rrect_fill(cx - 11, cy - 11, 22, 22, 7, halo_acc(255), halo_acc(255));
            nv_icon(ids[i], cx, cy, RGBA(5, 4, 10, 255));
        } else nv_icon(ids[i], cx, cy, RGBA(95, 91, 130, 255));
        gp_zone(cx - 12, cy - 12, 24, 24, NULL, ids[i], GPZ_TAB);
    }
    /* heure, reseau */
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    char b[64];
    if (tm && EL(EL_CLOCK)) { snprintf(b, sizeof(b), "%02d:%02d", tm->tm_hour, tm->tm_min); gx_text_r(F_SMALL, 616, 24, b, RGBA(220, 218, 240, 255)); }
    if (net_phase == NET_DONE) {
        col_t nc = net_ok ? halo_acc(255) : RGBA(255, 140, 90, 255);
        f32 w = (f32)text_width(F_SMALL, net_ok ? TR("En ligne") : TR("Hors ligne"), 0);
        gx_circle(576 - w - 8, 20, 2.6f, nc, nc);
        gx_text_r(F_SMALL, 576, 24, net_ok ? TR("En ligne") : TR("Hors ligne"), RGBA(124, 120, 158, 255));
    }
    if (rec_fp) { gx_circle(22, 64, 4, RGBA(255, 70, 70, 255), RGBA(255, 70, 70, 255)); gx_text1(F_SMALL, 30, 68, "REC", RGBA(255, 120, 120, 255)); }
    nv_raw = 0;
}

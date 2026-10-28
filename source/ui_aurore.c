// GC Station v102 : style « Aurore » (START > Reglages > Affichage > Style : Aurore). Inclus dans main.c
// apres ui_nova.c (il en reprend la selection qui glisse et la transition). Seul le visuel change.
//  - noir profond, avec des aurores (rubans de lumiere rose, violet, bleu) qui ondulent lentement ;
//  - en-tete : le nom de l'onglet en grand au centre, les onglets voisins a cote qui s'effacent vers
//    les bords ; changer d'onglet fait defiler les noms (facon Apple TV) ; points d'avancement dessous ;
//  - cadres presque invisibles (verre tres fin, filet lumineux en haut), pas de cadre pour la legende ;
//  - selection en degrade rose -> bleu qui glisse, page qui apparait en zoom doux.

/* v131 : Vitrine et Ilots (ui_styles.c, inclus juste apres) */
static void vi_background(void);
static void il_background(void);
static void vi_panel(f32 x, f32 y, f32 w, f32 h);
static void il_tile(f32 x, f32 y, f32 w, f32 h);
static void vi_header(void);
static void vi_page_label(const char *left, const char *count);
static const char *st_tab_name(int t);
static void vi_sel_paint(f32 x, f32 y, f32 w, f32 h, f32 r);
static void il_sel_paint(f32 x, f32 y, f32 w, f32 h, f32 r);

static col_t au_a(u8 a) {                                /* rose (Indigo) ; sinon l'accent du theme */
    if (ui_skin >= 4 && theme_id == THEME_INDIGO) return RGBA(255, 79, 176, a);   /* v131 : Vitrine, Ilots : rose */
    if (theme_id == THEME_INDIGO) return RGBA(255, 70, 170, a);
    return RGBA(mod_acc[0], mod_acc[1], mod_acc[2], a);
}
static col_t au_b(u8 a) {                                /* bleu (Indigo) ; sinon l'accent plus sombre et plus bleu */
    if (ui_skin >= 4 && theme_id == THEME_INDIGO) return RGBA(47, 211, 255, a);   /* v131 : Vitrine, Ilots : cyan */
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
/* v131 : une seule forme (avant : deux bouts + le milieu, qui se chevauchaient : une bande plus foncee
   aux bouts quand la couleur est transparente) */
static void au_pill_h(f32 x, f32 y, f32 w, f32 h, f32 r, col_t l, col_t rr) {
    if (r > h / 2) r = h / 2;
    if (w < 1) return;
    f32 px[RR_N], py[RR_N];
    rr_points(x, y, w, h, r, px, py);
    gx_set_flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, RR_N + 2);
    vtx(x + w / 2, y + h / 2, lerp_c(l, rr, 0.5f));
    for (int i = 0; i <= RR_N; i++) {
        int k = i % RR_N;
        vtx(px[k], py[k], lerp_c(l, rr, (px[k] - x) / w));
    }
    GX_End();
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
static void au_background(void) {
    if (ui_skin == 4) { vi_background(); return; }       /* v131 : Vitrine, Ilots (ui_styles.c) */
    if (ui_skin == 5) { il_background(); return; }
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
    if (ui_skin == 4) { vi_panel(x, y, w, h); return; }
    if (ui_skin == 5) { il_tile(x, y, w, h); return; }
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
    if (ui_skin >= 4) return;                             /* (v131 : Vitrine, Ilots : pas de cadre) */
    nv_raw = 1;
    gx_hgrad(x + 20, 419, w / 2 - 20, 1, RGBA(255, 255, 255, 0), RGBA(255, 255, 255, 30));
    gx_hgrad(x + w / 2, 419, w / 2 - 20, 1, RGBA(255, 255, 255, 30), RGBA(255, 255, 255, 0));
    nv_raw = 0;
}
static int au_draw_tabs(const char *count) {             /* (le nom de l'onglet est deja en grand en haut) */
    if (ui_skin == 4) { vi_page_label(st_tab_name(tab), count); return 26; }   /* v131 : Vitrine : en haut de la page */
    nv_raw = 1;
    if (count) gx_text_r(F_SMALL, IS_PLAYER_TAB(tab) ? 266 : 614, 101, count, RGBA(140, 140, 160, 255));
    nv_raw = 0;
    return 26;
}

/* ---------- en-tete : noms des onglets qui defilent ---------- */
static f32 au_p = -1;                                    /* position (rang de l'onglet, en douceur) */
static void au_header(void) {
    if (ui_skin == 4) { vi_header(); return; }           /* v131 : Vitrine : le rail ; Ilots : le dock (apres la page) */
    if (ui_skin == 5) return;
    nv_raw = 1;
    draw_brand(12, 5, 0.9f);                              /* v159 : la marque du style Classique (grand logo, nom, sous-titre) */
    nv_raw = 1;
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    char b[40];
    if (tm && EL(EL_CLOCK)) { snprintf(b, sizeof(b), "%02d:%02d", tm->tm_hour, tm->tm_min); gx_text_r(F_MED, 616, 30, b, RGBA(240, 240, 250, 255)); }
    if (net_phase == NET_DONE) {
        col_t nc = net_ok ? RGBA(70, 230, 160, 255) : RGBA(255, 140, 90, 255);
        gx_circle(612, 44, 2.6f, nc, nc);
        gx_text_r(F_SMALL, 604, 48, net_ok ? TR("En ligne") : TR("Hors ligne"), RGBA(120, 120, 140, 255));
    }
    if (rec_fp) { gx_circle(58, 52, 4, RGBA(255, 70, 70, 255), RGBA(255, 70, 70, 255)); gx_text1(F_SMALL, 66, 56, "REC", RGBA(255, 120, 120, 255)); }
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
    /* v200 : la rangee entre le logo et l'heure (avant : centree sur l'ecran, l'onglet de gauche passait sous le logo) */
    const f32 HC = 397, HW = 150;
    scr_scissor(HC - HW - 2, 0, 2 * HW + 4, 72);
    for (int i = 0; i < n; i++) {
        f32 x = HC + (cx[i] - center);
        f32 d = fabsf(x - HC) / HW;                        /* (0 au centre, 1 au bord) */
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
    f32 dw = 5, dg = 7, sel_w = 22, tot = (n - 1) * (dw + dg) + sel_w, x = HC - tot / 2;
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
    if (ui_skin == 4) { vi_sel_paint(x, y, w, h, r); return; }
    if (ui_skin == 5) { il_sel_paint(x, y, w, h, r); return; }
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

// GC Station v102 : style « Aurore » (START > Reglages > Affichage > Style : Aurore). Inclus dans main.c
// apres ui_nova.c (il en reprend la selection qui glisse et la transition). Seul le visuel change.
//  - noir profond, avec des aurores (rubans de lumiere rose, violet, bleu) qui ondulent lentement ;
//  - en-tete : le nom de l'onglet en grand au centre, les onglets voisins a cote qui s'effacent vers
//    les bords ; changer d'onglet fait defiler les noms (facon Apple TV) ; points d'avancement dessous ;
//  - cadres presque invisibles (verre tres fin, filet lumineux en haut), pas de cadre pour la legende ;
//  - selection en degrade rose -> bleu qui glisse, page qui apparait en zoom doux.

static col_t au_a(u8 a) {                                /* rose (Indigo) ; sinon l'accent du theme */
    if (theme_id == THEME_INDIGO) return RGBA(255, 70, 170, a);
    return RGBA(mod_acc[0], mod_acc[1], mod_acc[2], a);
}
static col_t au_b(u8 a) {                                /* bleu (Indigo) ; sinon l'accent plus sombre et plus bleu */
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
static void au_ribbon(f32 base, f32 amp, f32 th, f32 t, f32 sp, f32 ph, u8 alpha) {
    const int N = 24;
    for (int i = 0; i < N; i++) {
        f32 x0 = -10 + i * 660.0f / N, x1 = -10 + (i + 1) * 660.0f / N;
        f32 y0 = base + amp * sinf(x0 * 0.012f + t * sp + ph) + 0.4f * amp * sinf(x0 * 0.031f - t * sp * 1.7f);
        f32 y1 = base + amp * sinf(x1 * 0.012f + t * sp + ph) + 0.4f * amp * sinf(x1 * 0.031f - t * sp * 1.7f);
        col_t c0 = au_mix((f32)i / N, alpha), c1 = au_mix((f32)(i + 1) / N, alpha);
        col_t z0 = c0, z1 = c1; z0.a = 0; z1.a = 0;
        gx_poly4(x0, y0 - th, x1, y1 - th, x1, y1, x0, y0, z0, z1, c1, c0);           /* (fondu vers le haut) */
        gx_poly4(x0, y0, x1, y1, x1, y1 + th * 0.35f, x0, y0 + th * 0.35f, c0, c1, z1, z0);
    }
}
static void au_background(void) {
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
static void au_header(void) {
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
    for (int i = 0; i < n; i++) { wd[i] = (f32)text_width(F_MED, TR(nv_tab_names[ids[i]]), 0); cx[i] = acc + wd[i] / 2; acc += wd[i] + 46; }
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
        f32 tw = text_width(F_MED, TR(nv_tab_names[ids[i]]), 0);
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
    gx_glow(x, y, w, h, r, au_a(60), 3);
    au_pill_h(x, y, w, h, r, au_a(235), au_b(235));
    if (w > 30) gx_hgrad(x + r, y + 1, w - 2 * r, 1, RGBA(255, 255, 255, 80), RGBA(255, 255, 255, 10));
}

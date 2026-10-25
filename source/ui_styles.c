// GC Station v131 : styles « Vitrine » (ui_skin 4) et « Ilots » (ui_skin 5), a la place de Nebula et Halo.
// Inclus dans main.c apres ui_aurore.c. Ils gardent le moteur d'Aurore (selection qui glisse, page qui
// arrive en zoom doux, menu START qui s'ouvre en douceur, couleurs graphite) ; le reste change :
//  - Vitrine : un rail de verre a gauche (logo, noms des onglets, l'heure), la page a cote ; noir avec deux
//    grandes lueurs floues (rose en haut, bleu en bas) ; cadres presque invisibles ; la selection est une
//    barre en degrade a gauche sur un fond doux ; MUSIQUE : une rangee de pochettes (ui_styles2.c) ;
//  - Ilots : des tuiles de verre (widgets) ; un dock d'icones en bas (l'onglet choisi grandit et montre
//    son nom) ; la selection est un degrade rose ; MUSIQUE, INFOS et les lecteurs en tuiles (ui_styles2.c).
// La page de chaque onglet est dessinee dans sa zone : Vitrine la resserre a droite du rail (les positions
// et les largeurs, pas le texte : le meme mecanisme que le 16:9) ; Ilots la remonte au-dessus du dock.

#define ST_VI (ui_skin == 4)
#define ST_IL (ui_skin == 5)
#define VI_RAIL 112                                       /* (largeur du rail, bord compris) */

/* ---------- zone de la page ---------- */
/* (st_on, st_dy, st_bk, st_bo : en haut de main.c) */
static int st_mapped_layout(void) {                       /* (dispositions plein ecran du lecteur : pas remontees) */
    return !(IS_PLAYER_TAB(tab) && (player_layout == L_MINIMAL || player_layout == L_BIGVIZ));
}
static void st_page_map(void) {
    st_on = 0; st_dy = 0;
    if (!ST_VI && !ST_IL) return;
    if (st_skip) return;                                  /* (fenetre plein ecran : rien derriere) */
    if (tab == TAB_WEB && web_full) return;
    st_bk = ux_k; st_bo = ux_o;
    st_on = 1;
    if (ST_VI) {
        f32 x0 = UX(VI_RAIL + 8), x1 = UX(636);
        ux_o = x0; ux_k = (x1 - x0) / 640.0f;
        st_dy = st_mapped_layout() ? -22 : 0;
    } else st_dy = st_mapped_layout() ? -58 : -30;
}
static void st_page_unmap(void) {
    if (!st_on) return;
    ux_k = st_bk; ux_o = st_bo;
    st_on = 0; st_dy = 0;
}
/* repere "vrai" dans la page : x = 0 au bord gauche de la zone, 1 unite = 1 point (pochettes carrees,
   mises en page faites pour la largeur reelle) ; st_w = largeur de la zone */
static f32 st_w = 640;
static uxsave_t st_real(void) {
    uxsave_t s = { ux_k, ux_o };
    st_w = 640 * ux_k;
    ux_k = 1;
    return s;
}

/* "Retro" -> "RETRO" (avec les accents latins) */
static void st_upper(const char *s, char *o, int cap) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < cap - 1) {
        unsigned char c = *p++;
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c == 0xC3 && *p >= 0xA0 && *p <= 0xBE && *p != 0xB7 && n < cap - 2) { o[n++] = (char)c; c = *p++ - 0x20; }
        o[n++] = (char)c;
    }
    o[n] = 0;
}
static void st_clock(char *b, int cap) {
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    if (tm) snprintf(b, cap, "%02d:%02d", tm->tm_hour, tm->tm_min); else b[0] = 0;
}
static const char *st_tab_name(int t) { return TR(nv_tab_names[t >= 0 && t < TAB_COUNT ? t : 0]); }

/* ---------- fonds ---------- */
static void st_blob(f32 x, f32 y, f32 r, col_t c) { col_t z = c; z.a = 0; gx_circle(x, y, r, c, z); }
static void vi_background(void) {                          /* noir, rose en haut a droite, bleu en bas */
    nv_raw = 1;
    gx_rect(0, 0, 640, 480, RGBA(5, 5, 9, 255));
    f32 t = (f32)(now_ms() % 3600000) / 1000.0f;
    st_blob(560 + 24 * sinf(t * 0.09f), 10 + 18 * cosf(t * 0.11f), 330, au_a(84));
    st_blob(330 + 36 * sinf(t * 0.07f), 540 + 14 * cosf(t * 0.1f), 320, au_b(96));
    nv_raw = 0;
}
static void il_background(void) {                          /* noir, bleu en haut a droite, rose en bas a gauche */
    nv_raw = 1;
    gx_rect(0, 0, 640, 480, RGBA(6, 6, 10, 255));
    f32 t = (f32)(now_ms() % 3600000) / 1000.0f;
    st_blob(600 + 20 * cosf(t * 0.08f), 20 + 20 * sinf(t * 0.1f), 300, au_b(100));
    st_blob(30 + 24 * sinf(t * 0.09f), 470 + 12 * cosf(t * 0.12f), 300, au_a(80));
    nv_raw = 0;
}

/* ---------- cadres ---------- */
static void vi_panel(f32 x, f32 y, f32 w, f32 h) {         /* vitrine : presque rien, la page flotte */
    nv_raw = 1;
    int wa = wall_active();
    gx_rrect_fill(x, y, w, h, 18, RGBA(255, 255, 255, wa ? 26 : 8), RGBA(255, 255, 255, wa ? 30 : 3));
    nv_raw = 0;
}
static void il_tile(f32 x, f32 y, f32 w, f32 h) {          /* ilot : tuile de verre, bord fin, reflet en haut */
    nv_raw = 1;
    int wa = wall_active();
    gx_rrect_fill(x, y, w, h, 18, RGBA(255, 255, 255, wa ? 36 : 17), RGBA(255, 255, 255, wa ? 30 : 10));
    gx_rrect_stroke(x, y, w, h, 18, 1.0f, RGBA(255, 255, 255, 30));
    if (w > 60) gx_hgrad(x + 18, y + 1, w - 36, 1.0f, RGBA(255, 255, 255, 46), RGBA(255, 255, 255, 6));
    nv_raw = 0;
}
/* petit titre de tuile ("A SUIVRE", "SPECTRE"...) */
static void il_label(f32 x, f32 y, const char *s) {
    char u[64];
    st_upper(TR(s), u, sizeof(u));
    nv_raw = 1;
    gx_text(F_SMALL, x, y, u, RGBA(150, 150, 172, 255), RGBA(150, 150, 172, 255), 2);
    nv_raw = 0;
}

/* ---------- selections ---------- */
static void vi_sel_paint(f32 x, f32 y, f32 w, f32 h, f32 r) {
    au_pill_h(x, y, w, h, r, au_a(70), au_b(6));
    gx_rrect_fill(x + 1, y + 3, 3, h - 6, 1.5f, au_a(255), au_b(255));
}
static void il_sel_paint(f32 x, f32 y, f32 w, f32 h, f32 r) {
    au_pill_h(x, y, w, h, r, au_a(150), au_a(34));
    if (w > 30) gx_hgrad(x + r, y + 1, w - 2 * r, 1, RGBA(255, 255, 255, 60), RGBA(255, 255, 255, 0));
}

/* ---------- Vitrine : rail des onglets ---------- */
static f32 vi_p = -1;                                      /* rang de l'onglet choisi (en douceur) */
static void vi_header(void) {
    nv_raw = 1;
    const f32 rx = 8, ry = 8, rw = VI_RAIL - 10, rh = 464;
    gx_rrect_fill(rx, ry, rw, rh, 20, RGBA(255, 255, 255, 14), RGBA(255, 255, 255, 7));
    gx_rrect_stroke(rx, ry, rw, rh, 20, 1.0f, RGBA(255, 255, 255, 24));
    draw_logo_cube(rx + rw / 2, ry + 30, 21);             /* v132 : le logo en grand, le nom dessous */
    {
        const char *nm = "GC STATION";
        f32 w = (f32)text_width(F_SMALL, nm, 1);
        gx_text(F_SMALL, rx + rw / 2 - w / 2, ry + 68, nm, RGBA(235, 235, 245, 255), RGBA(235, 235, 245, 255), 1);
    }
    /* onglets */
    int ids[TAB_COUNT], n = 0, cur = 0;
    FOR_TABS(t) if (tab_shown(t)) { if (t == tab) cur = n; ids[n++] = t; }
    if (vi_p < -0.5f || !gl_anim) vi_p = (f32)cur;
    else { f32 k = 1.0f - expf(-ui_dt * 14.0f); vi_p += (cur - vi_p) * k; if (fabsf(cur - vi_p) < 0.003f) vi_p = (f32)cur; }
    f32 y0 = 90, step = n ? (396 - y0) / n : 30;
    if (step > 30) step = 30;
    f32 rh2 = step - 4 < 24 ? step - 4 : 24;
    f32 sy = y0 + vi_p * step;                              /* (fond et barre de la selection, qui glissent) */
    au_pill_h(rx + 4, sy, rw - 8, rh2, 8, au_a(64), au_b(4));
    gx_rrect_fill(rx + 1, sy + 3, 3, rh2 - 6, 1.5f, au_a(255), au_b(255));
    for (int i = 0; i < n; i++) {
        f32 y = y0 + i * step;
        f32 near = 1 - fabsf(i - vi_p); if (near < 0) near = 0;
        col_t c = lerp_c(RGBA(140, 140, 160, 255), RGBA(255, 255, 255, 255), near);
        const char *nm = st_tab_name(ids[i]);
        int f = text_width(F_MED, nm, 0) <= rw - 26 ? F_MED : F_SMALL;
        char b[48];
        fit_text(f, nm, (int)(rw - 24), b, sizeof(b));
        gx_text1(f, rx + 14, y + rh2 / 2 + (f == F_MED ? 5.5f : 4.5f), b, c);
        gp_zone(rx, y, rw, rh2, NULL, ids[i], GPZ_TAB);
    }
    /* bas du rail : heure, reseau, enregistrement */
    char b[40];
    if (EL(EL_CLOCK)) { st_clock(b, sizeof(b)); txt_scale = 1.15f; gx_text1(F_LARGE, rx + 14, 438, b, RGBA(245, 245, 252, 255)); txt_scale = 1.0f; }
    if (net_phase == NET_DONE) {
        col_t nc = net_ok ? RGBA(70, 230, 160, 255) : RGBA(255, 140, 90, 255);
        gx_circle(rx + 17, 453, 2.6f, nc, nc);
        gx_text1(F_SMALL, rx + 24, 457, net_ok ? TR("En ligne") : TR("Hors ligne"), RGBA(130, 130, 150, 255));
    }
    if (rec_fp) { gx_circle(rx + 17, 410, 4, RGBA(255, 70, 70, 255), RGBA(255, 70, 70, 255)); gx_text1(F_SMALL, rx + 26, 414, "REC", RGBA(255, 120, 120, 255)); }
    nv_raw = 0;
}
/* titre en petites capitales en haut de la page (dans la page) : "MUSIQUE · 128" */
static void vi_page_label(const char *left, const char *count) {
    char u[96], b[140];
    st_upper(left, u, sizeof(u));
    if (count && count[0]) snprintf(b, sizeof(b), "%s  \xC2\xB7  %s", u, count); else snprintf(b, sizeof(b), "%s", u);
    nv_raw = 1;
    gx_text(F_SMALL, 16, 54, b, RGBA(150, 150, 170, 255), RGBA(150, 150, 170, 255), 3);
    nv_raw = 0;
}

/* ---------- Ilots : dock en bas ---------- */
static f32 il_grow[TAB_COUNT];                             /* 0 = icone, 1 = pastille avec le nom (en douceur) */
static void il_dock(void) {
    int ids[TAB_COUNT], n = 0;
    FOR_TABS(t) if (tab_shown(t)) ids[n++] = t;
    if (!n) return;
    f32 k = gl_anim ? 1.0f - expf(-ui_dt * 14.0f) : 1.0f;
    f32 slot = 38, extra[TAB_COUNT], tot = 12;
    for (int i = 0; i < n; i++) {
        int t = ids[i];
        il_grow[t] += ((t == tab ? 1.0f : 0.0f) - il_grow[t]) * k;
        if (fabsf(il_grow[t] - (t == tab)) < 0.003f) il_grow[t] = (f32)(t == tab);
        extra[i] = (f32)text_width(F_SMALL, st_tab_name(t), 0) + 16;
        tot += slot + extra[i] * il_grow[t];
    }
    tot += 4;
    if (tot > 548) { slot = 34; tot = 16; for (int i = 0; i < n; i++) tot += slot + extra[i] * il_grow[ids[i]]; }
    const f32 H = 44, y = 428;
    f32 x = 320 - tot / 2;
    nv_raw = 1;
    gx_rrect_fill(x, y, tot, H, 22, RGBA(16, 16, 24, 210), RGBA(10, 10, 16, 220));
    gx_rrect_stroke(x, y, tot, H, 22, 1.0f, RGBA(255, 255, 255, 30));
    x += 8;
    for (int i = 0; i < n; i++) {
        int t = ids[i];
        f32 g = il_grow[t], w = slot + extra[i] * g;
        f32 ih = 32 + 10 * g, iy = y + (H - ih) / 2 - 7 * g;      /* (la choisie grandit et monte) */
        if (g > 0.02f) {
            gx_glow(x + 1, iy, w - 2, ih, 12, au_a((u8)(70 * g)), 3);
            au_pill_h(x + 1, iy, w - 2, ih, 12, au_a((u8)(255 * g)), au_b((u8)(255 * g)));
        }
        if (g < 0.98f) gx_rrect_fill(x + 3, y + 6, slot - 6, 32, 10, RGBA(255, 255, 255, (u8)(18 * (1 - g))), RGBA(255, 255, 255, (u8)(12 * (1 - g))));
        f32 icx = x + slot / 2, icy = iy + ih / 2;
        nv_icon(t, icx, icy, lerp_c(RGBA(175, 175, 195, 255), RGBA(255, 255, 255, 255), g));
        if (g > 0.3f) {
            scr_scissor(x, iy, w - 4, ih);
            gx_text1(F_SMALL, x + slot - 2, icy + 4.5f, st_tab_name(t), RGBA(255, 255, 255, (u8)(255 * (g - 0.3f) / 0.7f)));
            scr_viewport();
        }
        gp_zone(x, y - 8, w, H + 8, NULL, t, GPZ_TAB);
        x += w;
    }
    /* heure et reseau a cote du dock */
    char b[40];
    f32 dl = 320 - tot / 2, dr = 320 + tot / 2;
    if (EL(EL_CLOCK) && dr < 600) { st_clock(b, sizeof(b)); gx_text_r(F_MED, 630, 452, b, RGBA(240, 240, 250, 255)); }
    if (net_phase == NET_DONE && dr < 590) {
        col_t nc = net_ok ? RGBA(70, 230, 160, 255) : RGBA(255, 140, 90, 255);
        gx_circle(626, 462, 2.6f, nc, nc);
    }
    if (rec_fp && dl > 50) { gx_circle(16, 450, 4, RGBA(255, 70, 70, 255), RGBA(255, 70, 70, 255)); gx_text1(F_SMALL, 24, 454, "REC", RGBA(255, 120, 120, 255)); }
    nv_raw = 0;
}
/* apres la page (et avant les fenetres) : le dock des Ilots */
static void st_after_page(void) {
    if (ST_IL && !(tab == TAB_WEB && web_full)) il_dock();
}
/* zone cliquable dessinee dans la page : ramenee au repere de l'ecran (la fleche y vit) */
static void st_zone_fix(f32 *x, f32 *y, f32 *w) {
    f32 a = UX(*x), b = UX(*x + *w);
    *x = (a - st_bo) / st_bk;
    *w = (b - a) / st_bk;
    *y += st_dy;
}

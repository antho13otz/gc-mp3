// GC Station v103 : style Aurore, lecteur de l'onglet MUSIQUE (inclus dans main.c apres ui_extra.c).
// La liste des morceaux reste a gauche (memes touches) ; a droite, le lecteur : grande pochette avec son
// reflet, une lumiere aux couleurs de la pochette derriere (elle respire avec la musique), le titre en
// grand, la barre d'avancement en degrade et trois boutons.

static f32 au_cr = 120, au_cg = 60, au_cb = 220;          /* couleur de la pochette (suivie en douceur) */
static void au_cover_color(int have) {
    f32 tr = 150, tg = 70, tb = 230;                      /* (pas de pochette : violet) */
    if (have) {
        u32 r = 0, g = 0, b = 0, n = 0;
        for (u32 i = 0; i < COVER_SZ * COVER_SZ; i += (COVER_SZ * COVER_SZ) / 256) {
            u16 p = cover_tex[i];
            u32 pr = (p >> 11) & 31, pg = (p >> 5) & 63, pb = p & 31;
            u32 mx = pr * 2 > pg ? (pr * 2 > pb * 2 ? pr * 2 : pb * 2) : (pg > pb * 2 ? pg : pb * 2);
            if (mx < 10) continue;                            /* (les noirs ne colorent rien) */
            r += pr * 255 / 31; g += pg * 255 / 63; b += pb * 255 / 31; n++;
        }
        if (n) {
            tr = (f32)r / n; tg = (f32)g / n; tb = (f32)b / n;
            f32 l = (tr + tg + tb) / 3;                      /* plus vive et assez claire */
            tr = l + (tr - l) * 1.6f; tg = l + (tg - l) * 1.6f; tb = l + (tb - l) * 1.6f;
            f32 m = tr > tg ? (tr > tb ? tr : tb) : (tg > tb ? tg : tb);
            if (m < 170 && m > 1) { tr *= 170 / m; tg *= 170 / m; tb *= 170 / m; }
            if (tr < 0) tr = 0; if (tg < 0) tg = 0; if (tb < 0) tb = 0;
            if (tr > 255) tr = 255; if (tg > 255) tg = 255; if (tb > 255) tb = 255;
        }
    }
    f32 k = gl_anim ? 1.0f - expf(-ui_dt * 3.0f) : 1.0f;
    au_cr += (tr - au_cr) * k; au_cg += (tg - au_cg) * k; au_cb += (tb - au_cb) * k;
}
static col_t au_cc(u8 a) { return RGBA((u8)au_cr, (u8)au_cg, (u8)au_cb, a); }

/* v159 : comme le style Classique : paroles (.lrc) dans le cadre de la pochette (Reglages > Affichage > Paroles dans
   le lecteur : a la place / par-dessus le spectre), spectre au style choisi, elements du lecteur (pochette, spectre,
   boutons, infos) */
static int au_lyr_on(void) { return src == SRC_FILE && playing && lrc_n > 0 && lrc_track == cur_track && lyr_show && EL(EL_VIZ); }
static void au_lyrics(f32 cx, f32 y, f32 w, f32 h) {
    char a[300], b[300], d1[300];
    int li = lyrics_current();
    f32 my = y + h / 2;
    if (li > 0) { text_display(lyrics_line(li - 1), d1, sizeof(d1)); fit_text(F_SMALL, d1, (int)w, a, sizeof(a)); gx_text_c(F_SMALL, cx, my - 34, a, RGBA(200, 190, 230, 200)); }
    text_display(li >= 0 ? lyrics_line(li) : "...", d1, sizeof(d1));
    wrap2(F_MED, d1, (int)w, a, b, sizeof(a));
    gx_text_c(F_MED, cx, b[0] ? my - 2 : my + 6, a, RGBA(255, 255, 255, 255));
    if (b[0]) gx_text_c(F_MED, cx, my + 17, b, RGBA(255, 255, 255, 255));
    if (li + 1 < lrc_n) { text_display(lyrics_line(li + 1), d1, sizeof(d1)); fit_text(F_SMALL, d1, (int)w, a, sizeof(a)); gx_text_c(F_SMALL, cx, my + 44, a, RGBA(200, 190, 230, 200)); }
}
static void au_player(void) {
    np_t n;
    np_get(&n);
    int have = (src == SRC_FILE && playing && cover_ok) || (src == SRC_RADIO && cover_ok && cur_station >= 0 && state != ST_IDLE);
    int lyr = au_lyr_on(), showc = EL(EL_COVER), viz = EL(EL_VIZ) && !lyr;   /* v159 */
    if (!showc) have = 0;
    au_cover_color(have);
    const f32 cx = 459, sz = 180, x = cx - sz / 2, y = 84;
    f32 e = 0;
    for (int i = 0; i < 12; i++) e += bar_val[i];
    e /= 12; if (e > 1) e = 1;
    static f32 es = 0;
    es += (e - es) * (1.0f - expf(-ui_dt * 8.0f));
    nv_raw = 1;
    gx_circle(cx, y + sz / 2, 250, au_cc(60), au_cc(0));                               /* lumiere de la pochette */
    gx_circle(cx, y + sz / 2, sz * 0.62f + 26 * es, au_cc((u8)(70 + 90 * es)), au_cc(0));   /* (respire avec la musique) */
    if (showc || lyr) gx_glow(x, y + 5, sz, sz, have ? 4 : 14, RGBA(0, 0, 0, 150), 10);   /* ombre douce, centree (v200 : avant, un bloc noir decale a droite) */
    nv_raw = 0;
    if (!showc && !lyr) {                                    /* v159 : sans pochette : le spectre en grand a sa place */
        if (EL(EL_VIZ)) draw_viz(cx - 150, y + sz, 300, sz - 20);
    } else if (have) {
        uxsave_t uz = ux_native(cx);
        gx_set_cover();
        GX_Begin(GX_QUADS, GX_VTXFMT0, viz ? 4 : 8);       /* (v105 : 8 sommets : pochette + reflet ; avant 4 annonces = GX bloque ; v159 : 4 si le spectre remplace le reflet) */
        GX_Position2f32(UX(x), y);           GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(UX(x + sz), y);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(UX(x + sz), y + sz); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(UX(x), y + sz);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f);
        if (!viz) {
        const f32 rh = 26, ry = y + sz + 3;                                                  /* reflet */
        GX_Position2f32(UX(x), ry);          GX_Color4u8(255, 255, 255, 80); GX_TexCoord2f32(0.0f, 1.0f);
        GX_Position2f32(UX(x + sz), ry);     GX_Color4u8(255, 255, 255, 80); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(UX(x + sz), ry + rh); GX_Color4u8(255, 255, 255, 0); GX_TexCoord2f32(1.0f, 1.0f - rh / sz);
        GX_Position2f32(UX(x), ry + rh);     GX_Color4u8(255, 255, 255, 0);  GX_TexCoord2f32(0.0f, 1.0f - rh / sz);
        }
        GX_End();
        ux_restore(uz);
        nv_raw = 1;
        gx_rrect_stroke(x, y, sz, sz, 2, 1.0f, RGBA(255, 255, 255, 40));
        nv_raw = 0;
    } else {
        nv_raw = 1;
        gx_rrect_fill(x, y, sz, sz, 14, au_mix(0.15f, 255), au_mix(0.75f, 255));
        nv_raw = 0;
        if (!lyr) draw_logo_cube(cx, y + sz / 2, sz * 0.24f);
    }
    if (lyr) {                                               /* v159 : paroles par-dessus la pochette (assombrie) */
        nv_raw = 1;
        gx_rrect_fill(x - 40, y, sz + 80, sz, 14, RGBA(10, 6, 30, 175), RGBA(10, 6, 30, 200));
        nv_raw = 0;
        if (lyr_show == 2) draw_viz(x - 30, y + sz - 6, sz + 60, 60);   /* (paroles + spectre) */
        nv_raw = 1;
        au_lyrics(cx, y + 6, sz + 60, sz - 40);
        nv_raw = 0;
    }
    if (viz && showc) draw_viz(x, y + sz + 22, sz, 18);             /* v159 : le spectre au style choisi, a la place du reflet (v200 : largeur de la pochette) */
    char buf[220];
    nv_raw = 1;
    fit_text(F_LARGE, n.title, 320, buf, sizeof(buf));
    gx_text_c(F_LARGE, cx, 316, buf, RGBA(255, 255, 255, 255));
    if (n.sub[0]) { fit_text(F_MED, n.sub, 320, buf, sizeof(buf)); gx_text_c(F_MED, cx, 337, buf, au_cc(255)); }
    /* avancement */
    const f32 bx = 299, bw = 320, by = 349;
    f32 frac = (n.active && n.dur > 0) ? (f32)n.elapsed / (f32)n.dur : 0.0f;
    if (frac > 1) frac = 1;
    gx_rrect_fill(bx, by, bw, 4, 2, RGBA(255, 255, 255, 34), RGBA(255, 255, 255, 34));
    if (frac > 0.01f) au_pill_h(bx, by, bw * frac, 4, 2, au_a(255), au_b(255));
    if (n.active && n.dur > 0) { gx_circle(bx + bw * frac, by + 2, 6, au_cc(120), au_cc(0)); gx_circle(bx + bw * frac, by + 2, 4, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255)); }
    char t1[16], t2[16];
    fmt_listen(t1, n.active ? n.elapsed : 0);
    fmt_time(t2, n.dur);
    gx_text1(F_SMALL, bx, by + 18, t1, RGBA(170, 170, 195, 255));
    if (!(n.radio && n.dur <= 0)) gx_text_r(F_SMALL, bx + bw, by + 18, t2, RGBA(170, 170, 195, 255));
    if (n.info[0] && EL(EL_INFO)) { fit_text(F_SMALL, n.info, 200, buf, sizeof(buf)); gx_text_c(F_SMALL, cx, by + 18, buf, RGBA(130, 130, 155, 255)); }
    if (!EL(EL_BUTTONS)) { nv_raw = 0; return; }             /* v159 */
    /* boutons : precedent, lecture / pause, suivant */
    const f32 bcy = 393;
    col_t w = RGBA(235, 235, 245, 255);
    gx_circle(cx - 56, bcy, 15, RGBA(255, 255, 255, 18), RGBA(255, 255, 255, 10));
    gx_circle(cx + 56, bcy, 15, RGBA(255, 255, 255, 18), RGBA(255, 255, 255, 10));
    gx_tri(cx - 64, bcy, cx - 56, bcy - 6, cx - 56, bcy + 6, w); gx_tri(cx - 56, bcy, cx - 48, bcy - 6, cx - 48, bcy + 6, w);
    gx_tri(cx + 64, bcy, cx + 56, bcy - 6, cx + 56, bcy + 6, w); gx_tri(cx + 56, bcy, cx + 48, bcy - 6, cx + 48, bcy + 6, w);
    gx_circle_glow(cx, bcy, 19, au_mix(0.3f, 140), 4);
    gx_circle(cx, bcy, 19, au_a(255), au_b(255));
    int on = (src == SRC_FILE && playing && !paused) || (src == SRC_RADIO && state == ST_PLAY);
    if (on) { gx_rrect_fill(cx - 6.5f, bcy - 7, 4.5f, 14, 1.5f, w, w); gx_rrect_fill(cx + 2, bcy - 7, 4.5f, 14, 1.5f, w, w); }
    else gx_tri(cx - 4.5f, bcy - 8, cx - 4.5f, bcy + 8, cx + 8, bcy, w);
    gp_zone(cx - 75, bcy - 17, 34, 34, NULL, PAD_TRIGGER_L, GPZ_KEY);
    gp_zone(cx - 20, bcy - 20, 40, 40, NULL, PAD_BUTTON_Y, GPZ_KEY);
    gp_zone(cx + 41, bcy - 17, 34, 34, NULL, PAD_TRIGGER_R, GPZ_KEY);
    nv_raw = 0;
}

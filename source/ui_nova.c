// GC Station v101 : « Nouvelle interface » (START > Reglages > Style : Nouvelle). Inclus dans main.c.
// Seul le visuel change : memes onglets, memes touches, meme contenu. Le style Classique reste
// exactement comme avant (ui_skin 0), le style Moderne aussi (ui_skin 1).
//  - fond sombre bleute avec des lumieres douces qui bougent lentement ;
//  - en-tete : logo, dock d'icones des onglets (l'onglet choisi s'ouvre en pastille avec son nom,
//    les autres glissent en douceur), heure et reseau a droite ;
//  - cartes en verre (coins tres arrondis, ombre, reflet), legende en pastille flottante ;
//  - changement d'onglet : la page glisse et apparait en fondu (220 ms) ;
//  - couleurs : les violets d'origine deviennent des gris bleutes, la selection prend la couleur
//    d'accent du theme (Indigo : bleu-violet).


static u32 nv_frame = 0;                                 /* (images dessinees : selections qui glissent) */
static const char *const nv_tab_names[TAB_COUNT] = {
    "Musique", "Radio", "Web", "R\xC3\xA9tro", "Podcasts", "Photos", "Fichiers", "Infos", "T\xC3\xA9l\xC3\xA9phone", "Jeux", "T\xC3\xA9l\xC3\xA9" "chargements" };

static col_t nv_acc(u8 a) {
    if (theme_id == THEME_INDIGO) return RGBA(118, 112, 255, a);
    return RGBA(mod_acc[0], mod_acc[1], mod_acc[2], a);
}
static col_t nv_acc2(u8 a) {                             /* (deuxieme teinte des degrades) */
    col_t c = nv_acc(a);
    c.r = (u8)(c.r * 3 / 5); c.g = (u8)(c.g * 3 / 5); c.b = (u8)(c.b * 4 / 5);
    return c;
}

/* ---------- icones des onglets (dans un carre d'environ 18 px) ---------- */
static void nv_icon(int t, f32 x, f32 y, col_t c) {
    switch (t) {
    case TAB_MUSIC:                                         /* note */
        gx_ellipse(x - 4, y + 5, 3.6f, 2.8f, c, c);
        gx_ellipse(x + 5, y + 3, 3.6f, 2.8f, c, c);
        gx_rect(x - 1.2f, y - 7, 1.6f, 12, c);
        gx_rect(x + 7.8f, y - 9, 1.6f, 12, c);
        gx_poly4(x - 1.2f, y - 7, x + 9.4f, y - 9, x + 9.4f, y - 6, x - 1.2f, y - 4, c, c, c, c);
        break;
    case TAB_RADIO:                                         /* poste + antenne */
        gx_rrect_stroke(x - 9, y - 3, 18, 12, 2.5f, 1.6f, c);
        gx_circle(x + 4, y + 3, 2.8f, c, c);
        gx_rect(x - 6, y, 5, 1.3f, c); gx_rect(x - 6, y + 3, 5, 1.3f, c);
        gx_poly4(x - 5, y - 3, x + 5, y - 10, x + 6.2f, y - 9, x - 3.4f, y - 3, c, c, c, c);
        break;
    case TAB_WEB:                                           /* globe */
        gx_ring(x, y, 7.4f, 9, c);
        gx_rect(x - 8, y - 0.7f, 16, 1.4f, c);
        gx_rect(x - 0.7f, y - 8, 1.4f, 16, c);
        gx_rect(x - 6.5f, y - 4.5f, 13, 1.2f, c); gx_rect(x - 6.5f, y + 3.3f, 13, 1.2f, c);
        break;
    case TAB_RETRO:                                         /* manette */
        gx_rrect_fill(x - 10, y - 5, 20, 11, 5, c, c);
        gx_rect(x - 7, y - 0.8f, 6, 1.6f, RGBA(20, 22, 36, 255)); gx_rect(x - 4.8f, y - 3, 1.6f, 6, RGBA(20, 22, 36, 255));
        gx_circle(x + 4, y - 1.5f, 1.5f, RGBA(20, 22, 36, 255), RGBA(20, 22, 36, 255));
        gx_circle(x + 6.8f, y + 1.3f, 1.5f, RGBA(20, 22, 36, 255), RGBA(20, 22, 36, 255));
        break;
    case TAB_PODCAST:                                       /* micro */
        gx_rrect_fill(x - 3.5f, y - 10, 7, 12, 3.5f, c, c);
        gx_rect(x - 7, y - 2, 1.5f, 3, c); gx_rect(x + 5.5f, y - 2, 1.5f, 3, c);
        gx_poly4(x - 7, y + 1, x - 5.5f, y + 1, x - 0.5f, y + 5.5f, x - 0.5f, y + 7, c, c, c, c);
        gx_poly4(x + 5.5f, y + 1, x + 7, y + 1, x + 0.5f, y + 7, x + 0.5f, y + 5.5f, c, c, c, c);
        gx_rect(x - 0.8f, y + 6, 1.6f, 4, c);
        gx_rect(x - 4, y + 9, 8, 1.6f, c);
        break;
    case TAB_PHOTO:                                         /* paysage */
        gx_rrect_stroke(x - 10, y - 8, 20, 16, 3, 1.6f, c);
        gx_tri(x - 8, y + 6, x - 2, y - 2, x + 3, y + 6, c);
        gx_tri(x - 1, y + 6, x + 4, y + 1, x + 8, y + 6, c);
        gx_circle(x + 4.5f, y - 3.5f, 2, c, c);
        break;
    case TAB_FILES:                                         /* dossier */
        gx_rrect_fill(x - 10, y - 7, 9, 5, 1.5f, c, c);
        gx_rrect_fill(x - 10, y - 4, 20, 13, 2.5f, c, c);
        break;
    case TAB_INFO:                                          /* soleil et nuage */
        gx_circle(x - 3, y - 3, 4.5f, c, c);
        for (int k = 0; k < 6; k++) { f32 a = k * 1.047f; gx_circle(x - 3 + 7.5f * cosf(a), y - 3 + 7.5f * sinf(a), 1.1f, c, c); }
        gx_circle(x + 2, y + 4, 4.5f, c, c); gx_circle(x + 7, y + 5, 3.5f, c, c); gx_circle(x - 2, y + 6, 3, c, c);
        gx_rect(x - 2, y + 5, 9, 4, c);
        break;
    case TAB_PHONE:                                         /* telephone */
        gx_rrect_stroke(x - 6, y - 10, 12, 20, 3, 1.6f, c);
        gx_rect(x - 2, y + 6, 4, 1.4f, c);
        break;
    case TAB_GAMES: {                                       /* cube */
        f32 r = 8.5f, k = 0.866f;
        col_t d = RGBA(c.r * 7 / 10, c.g * 7 / 10, c.b * 7 / 10, c.a), e = RGBA(c.r / 2, c.g / 2, c.b / 2, c.a);
        gx_poly4(x, y - r, x + k * r, y - r / 2, x, y, x - k * r, y - r / 2, c, c, c, c);
        gx_poly4(x - k * r, y - r / 2, x, y, x, y + r, x - k * r, y + r / 2, d, d, d, d);
        gx_poly4(x, y, x + k * r, y - r / 2, x + k * r, y + r / 2, x, y + r, e, e, e, e);
        break;
    }
    default:                                                /* telechargements : fleche */
        gx_rect(x - 1, y - 9, 2.2f, 10, c);
        gx_tri(x - 6, y - 1, x + 6.2f, y - 1, x + 0.1f, y + 6, c);
        gx_rect(x - 9, y + 8, 18, 2, c);
        break;
    }
}

/* ---------- fond ---------- */
static void nv_background(void) {
    nv_raw = 1;
    gx_quad4(0, 0, 640, 480, RGBA(12, 14, 28, 255), RGBA(16, 18, 38, 255), RGBA(10, 11, 22, 255), RGBA(13, 14, 30, 255));
    f32 t = (f32)(now_ms() % 3600000) / 1000.0f;
    col_t a = nv_acc(46), a0 = nv_acc(0);
    gx_circle(120 + 60 * sinf(t * 0.11f), 400 + 30 * cosf(t * 0.13f), 300, a, a0);
    col_t b = RGBA(60, 170, 255, 26), b0 = RGBA(60, 170, 255, 0);
    gx_circle(540 + 50 * cosf(t * 0.09f), 90 + 40 * sinf(t * 0.12f), 260, b, b0);
    col_t m = RGBA(200, 80, 220, 18), m0 = RGBA(200, 80, 220, 0);
    gx_circle(380 + 90 * sinf(t * 0.07f + 1), 300 + 40 * sinf(t * 0.1f), 220, m, m0);
    nv_raw = 0;
}

/* ---------- carte en verre ---------- */
static void nv_panel(f32 x, f32 y, f32 w, f32 h) {
    nv_raw = 1;
    int wa = wall_active();
    gx_rrect_fill(x + 2, y + 6, w, h, 18, RGBA(0, 0, 0, 70), RGBA(0, 0, 0, 90));          /* ombre */
    gx_rrect_fill(x, y, w, h, 18, RGBA(34, 38, 64, wa ? 175 : 222), RGBA(22, 25, 44, wa ? 190 : 232));
    gx_rrect_stroke(x, y, w, h, 18, 1.0f, RGBA(255, 255, 255, 26));                     /* reflet du bord */
    gx_hgrad(x + 24, y + 1, w / 2 - 24, 1, RGBA(255, 255, 255, 0), RGBA(255, 255, 255, 40));
    gx_hgrad(x + w / 2, y + 1, w / 2 - 24, 1, RGBA(255, 255, 255, 40), RGBA(255, 255, 255, 0));
    nv_raw = 0;
}

/* ---------- legende (pastille flottante) ---------- */
static void nv_legend_box(f32 x, f32 w) {
    nv_raw = 1;
    gx_rrect_fill(x + 1, 423, w, 39, 19.5f, RGBA(0, 0, 0, 70), RGBA(0, 0, 0, 70));
    gx_rrect_fill(x, 419, w, 39, 19.5f, RGBA(30, 34, 58, 236), RGBA(22, 25, 44, 240));
    gx_rrect_stroke(x, 419, w, 39, 19.5f, 1.0f, RGBA(255, 255, 255, 24));
    nv_raw = 0;
}

/* ---------- titre de la page (a la place de la pastille de l'onglet) ---------- */
static int nv_draw_tabs(const char *count) {
    nv_raw = 1;
    const char *nm = TR(nv_tab_names[tab]);
    int w = gx_text1(F_MED, 26, 103, nm, RGBA(244, 246, 255, 255));
    gx_rrect_fill(26, 108, 22, 3, 1.5f, nv_acc(255), nv_acc(255));
    if (count) gx_text_r(F_SMALL, IS_PLAYER_TAB(tab) ? 266 : 614, 101, count, RGBA(150, 158, 190, 255));
    gx_rect(52, 109, (IS_PLAYER_TAB(tab) ? 241 : 590) - 27, 1, RGBA(255, 255, 255, 18));
    nv_raw = 0;
    return 26 + w + 12;
}

/* ---------- en-tete : logo, dock des onglets, heure ---------- */
static f32 nv_pos[TAB_COUNT], nv_wid[TAB_COUNT];
static int nv_init = 0;
static void nv_header(void) {
    nv_raw = 1;
    /* logo */
    draw_logo_cube(30, 33, 13);
    gx_text1(F_MED, 50, 39, "GC Station", RGBA(244, 246, 255, 255));
    /* heure, reseau, enregistrement */
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    char b[40];
    f32 rx = 616;
    if (tm && EL(EL_CLOCK)) { snprintf(b, sizeof(b), "%02d:%02d", tm->tm_hour, tm->tm_min); gx_text_r(F_MED, rx, 33, b, RGBA(244, 246, 255, 255)); }
    if (net_phase == NET_DONE) {
        col_t nc = net_ok ? RGBA(90, 220, 140, 255) : RGBA(255, 140, 90, 255);
        const char *nt = net_ok ? TR("En ligne") : TR("Hors ligne");
        int tw = text_width(F_SMALL, nt, 0);
        gx_text_r(F_SMALL, rx, 52, nt, RGBA(150, 158, 190, 255));
        gx_circle(rx - tw - 7, 48, 3, nc, nc);
    }
    if (rec_fp) { gx_circle(560, 28, 4, RGBA(255, 70, 70, 255), RGBA(255, 70, 70, 255)); gx_text_r(F_SMALL, 552, 32, "REC", RGBA(255, 120, 120, 255)); }
    /* dock */
    int n = 0;
    FOR_TABS(t) if (tab_shown(t)) n++;
    const f32 cy = 35, ih = 36;
    f32 lw = (f32)text_width(F_SMALL, TR(nv_tab_names[tab]), 0);
    f32 selw = ih + lw + 14, gap = 5, item = ih;
    f32 total = (n - 1) * (item + gap) + selw;
    const f32 maxw = 404;                                 /* (entre le logo et l'heure) */
    if (total > maxw) { gap = 2; item = (maxw - selw - (n - 1) * gap) / (n - 1); if (item < 24) item = 24; total = (n - 1) * (item + gap) + selw; }
    f32 x = 352 - total / 2;
    f32 k = 1.0f - expf(-ui_dt * 14.0f);
    if (!gl_anim) k = 1.0f;
    FOR_TABS(t) {
        if (!tab_shown(t)) continue;
        f32 w = t == tab ? selw : item;
        if (!nv_init) { nv_pos[t] = x; nv_wid[t] = w; }
        nv_pos[t] += (x - nv_pos[t]) * k;
        nv_wid[t] += (w - nv_wid[t]) * k;
        x += w + gap;
    }
    nv_init = 1;
    FOR_TABS(t) {
        if (!tab_shown(t)) continue;
        f32 px = nv_pos[t], pw = nv_wid[t];
        int on = t == tab;
        if (on) {
            gx_glow(px, cy - ih / 2, pw, ih, ih / 2, nv_acc(90), 3);
            gx_rrect_fill(px, cy - ih / 2, pw, ih, ih / 2, nv_acc(255), nv_acc2(255));
            gx_hgrad(px + 14, cy - ih / 2 + 1, pw - 28, 1, RGBA(255, 255, 255, 60), RGBA(255, 255, 255, 0));
            nv_icon(t, px + ih / 2, cy, RGBA(255, 255, 255, 255));
            if (pw > ih + 20) {                                /* (le nom apparait quand la pastille est ouverte) */
                scr_scissor(px, cy - ih / 2, pw - 6, ih);
                gx_text1(F_SMALL, px + ih - 2, cy + 5, TR(nv_tab_names[t]), RGBA(255, 255, 255, 255));
                scr_viewport();
            }
        } else {
            f32 r = (pw < ih ? pw : ih) / 2;
            gx_circle(px + pw / 2, cy, r, RGBA(255, 255, 255, 16), RGBA(255, 255, 255, 10));
            nv_icon(t, px + pw / 2, cy, RGBA(168, 176, 206, 255));
        }
        gp_zone(px, cy - ih / 2, pw, ih, NULL, t, GPZ_TAB);
    }
    nv_raw = 0;
}

/* ---------- transition entre onglets ---------- */
static int nv_prev_tab = -1;
static u64 nv_t0 = 0;
static f32 nv_dir = 1;
static int nv_order(int t) { int i = 0; FOR_TABS(u) { if (u == t) return i; i++; } return 0; }
static f32 nv_trans(void) {                              /* 0 = fini ; sinon avancement (1 -> 0) */
    if (nv_prev_tab != tab) {
        if (nv_prev_tab >= 0 && gl_anim) { nv_dir = nv_order(tab) >= nv_order(nv_prev_tab) ? 1.0f : -1.0f; nv_t0 = now_ms(); }
        nv_prev_tab = tab;
    }
    if (!nv_t0) return 0;
    f32 p = (now_ms() - nv_t0) / 220.0f;
    if (p >= 1) { nv_t0 = 0; return 0; }
    f32 e = 1 - p; return e * e * e;                     /* (sortie douce) */
}
static f32 nv_page_r = 0;
static void au_sel_paint(f32 x, f32 y, f32 w, f32 h, f32 r);   /* (ui_aurore.c) */
static void nv_shift(f32 dx) {
    Mtx m;
    guMtxIdentity(m);
    if (ui_skin >= 3 && nv_page_r > 0) {                 /* v102 : Aurore : la page arrive en zoom doux */
        f32 s = 1.0f - 0.035f * nv_page_r, cx = UX(320), cy = 270;
        m[0][0] = s; m[1][1] = s; m[0][3] = cx * (1 - s); m[1][3] = cy * (1 - s);
        GX_LoadPosMtxImm(m, GX_PNMTX0);
        return;
    }
    m[0][3] = dx * ux_k;
    GX_LoadPosMtxImm(m, GX_PNMTX0);
    nv_cdx = dx;
}
static void nv_page_begin(void) {                        /* avant le contenu de l'onglet */
    nv_frame++;
    nv_page_r = ui_skin >= 2 ? nv_trans() : 0;
    if (nv_page_r > 0) nv_shift(nv_dir * 46 * nv_page_r);
}
static void nv_page_end(void) {                          /* apres : fondu par-dessus, puis plus de decalage */
    if (nv_page_r <= 0) return;
    f32 r = nv_page_r;
    nv_page_r = 0;
    nv_shift(0);
    nv_raw = 1;
    gx_rect(0, 72, 640, 408, ui_skin >= 3 ? RGBA(4, 4, 8, (u8)(220 * r)) : RGBA(12, 14, 28, (u8)(200 * r)));
    nv_raw = 0;
}

/* ---------- selection qui glisse (toutes les listes et grilles) ----------
   Une selection = un rectangle de meme taille d'une image a l'autre : sa position suit la cible
   en douceur. Une selection pas dessinee depuis 3 images repart directement a sa place. */
typedef struct { f32 w, h, x, y; u32 frame; } nvsel_t;
static nvsel_t nv_sel[8];
static void nv_sel_draw(f32 x, f32 y, f32 w, f32 h, f32 r) {
    nvsel_t *s = NULL;
    for (int i = 0; i < 8 && !s; i++) if (nv_sel[i].frame && fabsf(nv_sel[i].w - w) < 0.5f && fabsf(nv_sel[i].h - h) < 0.5f && nv_sel[i].frame != nv_frame + 1) s = &nv_sel[i];
    if (!s) {                                              /* (nouvelle : la plus ancienne place) */
        int o = 0;
        for (int i = 1; i < 8; i++) if (nv_sel[i].frame < nv_sel[o].frame) o = i;
        s = &nv_sel[o]; s->w = w; s->h = h; s->frame = 0;
    }
    if (!s->frame || nv_frame - (s->frame - 1) > 3 || !gl_anim) { s->x = x; s->y = y; }
    else {
        f32 k = 1.0f - expf(-ui_dt * 20.0f);
        s->x += (x - s->x) * k; s->y += (y - s->y) * k;
        if (fabsf(x - s->x) < 0.3f) s->x = x;
        if (fabsf(y - s->y) < 0.3f) s->y = y;
    }
    s->frame = nv_frame + 1;
    nv_raw = 1;
    if (ui_skin >= 3) { au_sel_paint(s->x, s->y, w, h, r); nv_raw = 0; return; }   /* v102 : Aurore */
    gx_glow(s->x, s->y, w, h, r, nv_acc(70), 3);
    gx_rrect_fill(s->x, s->y, w, h, r, nv_acc(255), nv_acc2(255));
    if (w > 30) gx_hgrad(s->x + r, s->y + 1, w - 2 * r, 1, RGBA(255, 255, 255, 70), RGBA(255, 255, 255, 0));
    nv_raw = 0;
}

// GC Station v131 : pages propres aux styles Vitrine et Ilots (inclus a la fin de main.c, apres tab_infos.c).
//  - Vitrine, MUSIQUE : la liste devient une rangee de pochettes ; celle du milieu (la ligne choisie) en grand
//    avec son reflet, son titre en grand dessous, puis ce qui joue (barre d'avancement en degrade) ;
//    RADIO, RETRO, PODCASTS : la liste a gauche, ce qui joue en vitrine a droite ;
//  - Ilots, MUSIQUE : des tuiles : ce qui joue, l'heure, la meteo, la liste (« a suivre »), le spectre ;
//    RADIO, RETRO, PODCASTS : la liste dans une tuile, ce qui joue / l'heure / le spectre en tuiles ;
//    INFOS : heure, meteo, previsions, reglages du reveil et actualites en tuiles.
// Memes touches que les autres styles (Vitrine, MUSIQUE : gauche / droite = morceau precedent / suivant).

/* ---------- couleurs des pochettes dessinees (pas d'image) : une par artiste ou album ---------- */
static const u8 st_pals[8][6] = {
    { 255, 122, 150, 112, 60, 222 }, { 255, 170, 92, 228, 62, 122 }, { 64, 202, 214, 42, 82, 182 }, { 172, 112, 255, 62, 92, 232 },
    { 255, 202, 92, 222, 82, 62 }, { 92, 222, 152, 32, 132, 162 }, { 112, 122, 255, 42, 192, 242 }, { 255, 112, 202, 255, 152, 92 } };
static u32 st_hash(const char *s) { u32 h = 2166136261u; while (*s) { h ^= (u8)*s++; h *= 16777619u; } return h; }

/* une entree de la liste MUSIQUE : titre, sous-titre, couleur, image */
typedef struct { char title[160], sub[160]; u32 h; int kind, cover, k; } st_ent_t;   /* kind : 0 morceau, 1 groupe, 2 retour */
static void st_entry(int r, st_ent_t *o) {
    char raw[200];
    memset(o, 0, sizeof(*o));
    o->k = -1;
    if (r < 0 || r >= nents) return;
    lent_t e = ents[r];
    if (e.type == E_UP) {
        o->kind = 2;
        snprintf(o->title, sizeof(o->title), "%s", TR("Retour"));
        text_display(group_label(e.idx), o->sub, sizeof(o->sub));
        o->h = st_hash(o->sub);
        return;
    }
    if (e.type == E_GROUP) {
        o->kind = 1;
        text_display(group_label(e.idx), o->title, sizeof(o->title));
        snprintf(o->sub, sizeof(o->sub), grp_count[e.idx] > 1 ? "%d morceaux" : "%d morceau", grp_count[e.idx]);
        o->h = st_hash(o->title);
        return;
    }
    int k = e.idx;
    o->k = k;
    if (!tg_done[k] && tag_budget > 0) { tag_budget--; tag_scan_one(k); service_audio(); }   /* (une etiquette par image) */
    track_label(k, raw, sizeof(raw), 0);
    text_display(raw, o->title, sizeof(o->title));
    if (tg_done[k] && tg_artist[k][0]) {
        if (tg_album[k][0]) snprintf(raw, sizeof(raw), "%s \xC2\xB7 %s", tg_artist[k], tg_album[k]);
        else snprintf(raw, sizeof(raw), "%s", tg_artist[k]);
        text_display(raw, o->sub, sizeof(o->sub));
    }
    o->h = st_hash(tg_done[k] && tg_album[k][0] ? tg_album[k] : tg_done[k] && tg_artist[k][0] ? tg_artist[k] : o->title);
    o->cover = src == SRC_FILE && playing && k == cur_track && cover_ok;
}
static int st_have_cover(void) {
    return (src == SRC_FILE && playing && cover_ok) || (src == SRC_RADIO && cover_ok && cur_station >= 0 && state != ST_IDLE);
}
/* pochette carree (vraie image, ou dessinee : degrade, lueur, icone) ; refl = reflet dessous ; dim = assombrie */
static void st_cover(f32 x, f32 y, f32 s, int img, u32 h, int kind, f32 refl, u8 dim) {
    uxsave_t uz = ux_native(x + s / 2);
    f32 r = s * 0.11f;
    nv_raw = 1;
    if (img) {
        gx_set_cover();
        u8 c = (u8)(255 - dim);
        int n = refl > 0 ? 8 : 4;
        GX_Begin(GX_QUADS, GX_VTXFMT0, n);
        GX_Position2f32(UX(x), y);           GX_Color4u8(c, c, c, 255); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(UX(x + s), y);       GX_Color4u8(c, c, c, 255); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(UX(x + s), y + s);   GX_Color4u8(c, c, c, 255); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(UX(x), y + s);       GX_Color4u8(c, c, c, 255); GX_TexCoord2f32(0.0f, 1.0f);
        if (refl > 0) {
            f32 ry = y + s + 4;
            GX_Position2f32(UX(x), ry);          GX_Color4u8(c, c, c, 70); GX_TexCoord2f32(0.0f, 1.0f);
            GX_Position2f32(UX(x + s), ry);      GX_Color4u8(c, c, c, 70); GX_TexCoord2f32(1.0f, 1.0f);
            GX_Position2f32(UX(x + s), ry + refl); GX_Color4u8(c, c, c, 0); GX_TexCoord2f32(1.0f, 1.0f - refl / s);
            GX_Position2f32(UX(x), ry + refl);   GX_Color4u8(c, c, c, 0); GX_TexCoord2f32(0.0f, 1.0f - refl / s);
        }
        GX_End();
        gx_rrect_stroke(x, y, s, s, 2, 1.0f, RGBA(255, 255, 255, 40));
    } else {
        const u8 *p = st_pals[h % 8];
        f32 k = (255 - dim) / 255.0f;
        col_t a = RGBA((u8)(p[0] * k), (u8)(p[1] * k), (u8)(p[2] * k), 255), b = RGBA((u8)(p[3] * k), (u8)(p[4] * k), (u8)(p[5] * k), 255);
        gx_rrect_fill(x, y, s, s, r, a, b);
        if (s > 40) {                                         /* lueur douce en haut a gauche */
            f32 gx = x + s * (0.28f + 0.1f * ((h >> 8) % 4) / 3.0f), gy = y + s * 0.3f;
            gx_circle(gx, gy, s * 0.26f, RGBA(255, 236, 200, (u8)(60 * k)), RGBA(255, 236, 200, 0));
        }
        col_t ic = RGBA(255, 255, 255, (u8)(150 * k));
        f32 icx = x + s / 2, icy = y + s / 2;
        if (kind == 1) { if (view_mode == VIEW_FOLDERS) draw_folder_icon(icx, icy, ic); else if (view_mode == VIEW_ARTISTS) draw_person_icon(icx, icy, ic); else draw_disc_icon(icx, icy, ic); }
        else if (kind == 2) { gx_tri(icx - 9, icy, icx + 3, icy - 8, icx + 3, icy + 8, ic); gx_rect(icx + 2, icy - 2, 8, 4, ic); }
        else draw_note(icx, icy, ic);
        if (refl > 0) gx_rrect_fill(x, y + s + 4, s, refl, r, RGBA(b.r, b.g, b.b, 60), RGBA(b.r, b.g, b.b, 0));
    }
    nv_raw = 0;
    ux_restore(uz);
}
/* barre d'avancement fine en degrade (avec le temps) */
static void st_progress(f32 x, f32 y, f32 w, const np_t *n, int times) {
    f32 frac = (n->active && n->dur > 0) ? (f32)n->elapsed / (f32)n->dur : 0.0f;
    if (frac > 1) frac = 1;
    nv_raw = 1;
    gx_rrect_fill(x, y, w, 4, 2, RGBA(255, 255, 255, 34), RGBA(255, 255, 255, 34));
    if (frac > 0.01f) au_pill_h(x, y, w * frac, 4, 2, au_a(255), au_b(255));
    if (n->active && n->dur > 0) { gx_circle(x + w * frac, y + 2, 7, au_b(110), au_b(0)); gx_circle(x + w * frac, y + 2, 3.5f, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255)); }
    if (times) {
        char t1[16], t2[16];
        fmt_listen(t1, n->active ? n->elapsed : 0);
        fmt_time(t2, n->dur);
        gx_text1(F_SMALL, x, y + 20, t1, RGBA(160, 160, 182, 255));
        if (!(n->radio && n->dur <= 0)) gx_text_r(F_SMALL, x + w, y + 20, t2, RGBA(160, 160, 182, 255));
    }
    nv_raw = 0;
}
/* bas de la liste MUSIQUE : vue (stick C), recherche, aleatoire, repetition */
static void st_music_foot(f32 x, f32 y) {
    char foot[96];
    col_t fc = RGBA(150, 150, 172, 255);
    if ((view_mode == VIEW_ARTISTS || view_mode == VIEW_ALBUMS) && tg_scanned < nfiles) {
        snprintf(foot, sizeof(foot), "Lecture des \xC3\xA9tiquettes %d/%d", tg_scanned, nfiles);
        fc = RGBA(255, 210, 120, 255);
    } else if (lib_find[0]) snprintf(foot, sizeof(foot), "Recherche : %.40s  (B)", lib_find);
    else snprintf(foot, sizeof(foot), "< %s >", TR(view_names[view_mode]));
    nv_raw = 1;
    f32 w = (f32)gx_text1(F_SMALL, x, y, foot, fc) + 10;
    if (shuffle_on) { w += 4 + gx_text1(F_SMALL, x + w, y, "AL\xC3\x89" "A", au_a(255)) + 10; }
    if (repeat_mode != REP_OFF) gx_text1(F_SMALL, x + w, y, repeat_mode == REP_ONE ? "R\xC3\x89P. 1" : "R\xC3\x89P.", au_b(255));
    nv_raw = 0;
}
static void st_empty_music(f32 cx, f32 y) {
    nv_raw = 1;
    if (nfiles == 0 && sd_virtual) {
        gx_text_c(F_MED, cx, y, "Pas de carte SD", RGBA(255, 205, 110, 255));
        gx_text_c(F_SMALL, cx, y + 22, "Radio, podcasts et web marchent.", RGBA(170, 170, 190, 255));
    } else if (nfiles == 0) {
        gx_text_c(F_MED, cx, y, "Aucun fichier MP3 trouv\xC3\xA9", RGBA(255, 205, 110, 255));
        gx_text_c(F_SMALL, cx, y + 22, "Copie tes MP3 dans SD:/mp3", RGBA(170, 170, 190, 255));
    }
    nv_raw = 0;
}

/* ======================= Vitrine ======================= */
static f32 vm_pos = -1000;                                 /* position de la rangee (en douceur) */
static void vi_music(void) {
    uxsave_t uz = st_real();
    const f32 W = st_w, cx = W / 2, B = 172, S = 112, gap = 16, top = 84;
    if (nents <= 0) vm_pos = 0;
    else if (vm_pos < -999 || !gl_anim || fabsf(vm_pos - sel_row) > 12) vm_pos = (f32)sel_row;
    else { f32 k = 1.0f - expf(-ui_dt * 12.0f); vm_pos += (sel_row - vm_pos) * k; if (fabsf(sel_row - vm_pos) < 0.002f) vm_pos = (f32)sel_row; }
    /* titre de la page */
    char lab[64], cnt[32];
    int pl = src == SRC_FILE && playing;
    snprintf(cnt, sizeof(cnt), "%d / %d", nents ? sel_row + 1 : 0, nents);
    snprintf(lab, sizeof(lab), "%s", pl ? TR("En lecture") : TR("Biblioth\xC3\xA8que"));
    vi_page_label(lab, cnt);
    /* lueur de la couleur de la pochette du milieu */
    st_ent_t ec;
    st_entry(sel_row, &ec);
    {
        const u8 *p = st_pals[ec.h % 8];
        col_t g = ec.cover ? RGBA((u8)au_cr, (u8)au_cg, (u8)au_cb, 70) : RGBA(p[0], p[1], p[2], 56);
        if (ec.cover) au_cover_color(1);
        nv_raw = 1;
        uxsave_t u2 = ux_native(cx);
        gx_circle(cx, top + B / 2, 230, g, RGBA(g.r, g.g, g.b, 0));
        ux_restore(u2);
        nv_raw = 0;
    }
    /* rangee : du plus loin au plus pres */
    scr_scissor(0, 40, W, 300);
    int c0 = (int)floorf(vm_pos + 0.5f);
    for (int far = 4; far >= 0; far--) {
        for (int sd = -1; sd <= 1; sd += 2) {
            if (far == 0 && sd > 0) continue;
            int r = c0 + sd * far;
            if (r < 0 || r >= nents) continue;
            f32 d = r - vm_pos, ad = fabsf(d);
            f32 s = ad < 1 ? B + (S - B) * ad : S;
            f32 off = ad < 1 ? ad * (B / 2 + S / 2 + gap) : (B / 2 + S / 2 + gap) + (ad - 1) * (S + gap);
            f32 x = cx + (d < 0 ? -off : off) - s / 2, y = top + (B - s) / 2;
            if (x > W + 4 || x + s < -4) continue;
            st_ent_t e;
            if (r == sel_row) e = ec; else st_entry(r, &e);
            f32 dm = ad < 1 ? ad * 120 : 120 + (ad - 1) * 30;
            if (dm > 200) dm = 200;
            st_cover(x, y, s, e.cover, e.h, e.kind, ad < 1 ? 34 * (1 - ad) : 0, (u8)dm);
            gp_zone(x, y, s, s, &sel_row, r, GPZ_SEL);
        }
    }
    scr_viewport();
    /* titre et sous-titre du milieu */
    char buf[220];
    nv_raw = 1;
    if (nents > 0) {
        txt_scale = 1.3f;
        fit_text(F_LARGE, ec.title, (int)(W - 40), buf, sizeof(buf));
        gx_text_c(F_LARGE, cx, top + B + 64, buf, RGBA(255, 255, 255, 255));
        txt_scale = 1.0f;
        fit_text(F_MED, ec.sub[0] ? ec.sub : " ", (int)(W - 60), buf, sizeof(buf));
        gx_text_c(F_MED, cx, top + B + 88, buf, RGBA(170, 170, 194, 255));
    } else st_empty_music(cx, top + B / 2);
    nv_raw = 0;
    /* ce qui joue */
    np_t n;
    np_get(&n);
    if (n.active) {
        f32 bx = 46, bw = W - 92, by = top + B + 112;
        if (!(ents[sel_row].type == E_TRACK && ents[sel_row].idx == cur_track && src == SRC_FILE)) {
            nv_raw = 1;
            snprintf(buf, sizeof(buf), "%s", n.title);
            char f2[220];
            fit_text(F_SMALL, buf, (int)(bw - 120), f2, sizeof(f2));
            f32 tw = (f32)text_width(F_SMALL, f2, 0);
            f32 tx = cx - tw / 2;
            gx_tri(tx - 12, by - 14, tx - 12, by - 4, tx - 4, by - 9, au_a(255));
            gx_text1(F_SMALL, tx, by - 5, f2, RGBA(200, 200, 215, 255));
            nv_raw = 0;
            by += 8;
        }
        st_progress(bx, by, bw, &n, 1);
    }
    ux_restore(uz);
    st_music_foot(16, 447);
    draw_legend();
}
/* RADIO, RETRO, PODCASTS : ce qui joue, en vitrine, a droite de la liste */
static void vi_player(void) {
    np_t n;
    np_get(&n);
    f32 k = ux_k;
    uxsave_t uz = st_real();
    f32 x0 = 289 * k, x1 = 630 * k, cx = (x0 + x1) / 2, s = 150, y = 88;
    int have = st_have_cover();
    au_cover_color(have);
    nv_raw = 1;
    uxsave_t u2 = ux_native(cx);
    gx_circle(cx, y + s / 2, 190, au_cc(64), au_cc(0));
    ux_restore(u2);
    nv_raw = 0;
    st_cover(cx - s / 2, y, s, have, st_hash(n.title), tab == TAB_RADIO ? 1 : 0, 30, 0);
    char buf[220];
    nv_raw = 1;
    f32 tw = x1 - x0 - 20;
    txt_scale = 1.15f;
    fit_text(F_LARGE, n.title, (int)tw, buf, sizeof(buf));
    gx_text_c(F_LARGE, cx, y + s + 62, buf, RGBA(255, 255, 255, 255));
    txt_scale = 1.0f;
    if (n.sub[0]) { fit_text(F_MED, n.sub, (int)tw, buf, sizeof(buf)); gx_text_c(F_MED, cx, y + s + 84, buf, au_cc(255)); }
    if (n.info[0]) { fit_text(F_SMALL, n.info, (int)tw, buf, sizeof(buf)); gx_text_c(F_SMALL, cx, y + s + 140, buf, RGBA(130, 130, 152, 255)); }
    nv_raw = 0;
    if (n.active) st_progress(x0 + 16, y + s + 100, x1 - x0 - 32, &n, 1);
    ux_restore(uz);
}

/* ======================= Ilots ======================= */
static const char *const st_jours[7] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };
static const char *const st_mois[12] = { "janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin", "juillet",
                                         "ao\xC3\xBBt", "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre" };
/* tuile : ce qui joue (pochette, titre, avancement) */
static void il_np(f32 x, f32 y, f32 w, f32 h) {
    np_t n;
    np_get(&n);
    il_tile(x, y, w, h);
    int have = st_have_cover();
    f32 s = h - 32;
    st_cover(x + 16, y + 16, s, have, st_hash(n.title), tab == TAB_RADIO || tab == TAB_PODCAST ? 1 : 0, 0, 0);
    /* (la pochette garde sa taille reelle : le texte commence juste apres) */
    f32 px = x + 16 + s / ux_k + 18, tw = x + w - 18 - px;
    char buf[220];
    nv_raw = 1;
    txt_scale = 1.2f;
    fit_text(F_LARGE, n.title, (int)tw, buf, sizeof(buf));
    gx_text1(F_LARGE, px, y + h * 0.36f, buf, RGBA(255, 255, 255, 255));
    txt_scale = 1.0f;
    if (n.sub[0]) { fit_text(F_MED, n.sub, (int)tw, buf, sizeof(buf)); gx_text1(F_MED, px, y + h * 0.36f + 24, buf, RGBA(172, 172, 192, 255)); }
    nv_raw = 0;
    if (n.active) st_progress(px, y + h - 50, tw, &n, 1);
    else { nv_raw = 1; gx_text1(F_SMALL, px, y + h - 30, TR("Choisis un morceau et appuie sur A"), RGBA(130, 130, 150, 255)); nv_raw = 0; }
}
/* tuile : l'heure et la date (et la meteo si on a la place) */
static void il_clock(f32 x, f32 y, f32 w, f32 h, int wx) {
    il_tile(x, y, w, h);
    il_label(x + 16, y + 22, "Heure");
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) return;
    char b[64];
    snprintf(b, sizeof(b), "%02d:%02d", tm->tm_hour, tm->tm_min);
    nv_raw = 1;
    txt_scale = w > 180 ? 1.9f : 1.6f;
    gx_text1(F_LARGE, x + 14, y + 22 + 46, b, RGBA(250, 250, 255, 255));
    txt_scale = 1.0f;
    snprintf(b, sizeof(b), "%s %d %s", TR(st_jours[tm->tm_wday % 7]), tm->tm_mday, TR(st_mois[tm->tm_mon % 12]));
    char f[80];
    fit_text(F_SMALL, b, (int)(w - 30), f, sizeof(f));
    gx_text1(F_SMALL, x + 16, y + 22 + 66, f, RGBA(172, 172, 196, 255));
    if (wx && h > 130 && meteo_ok) {
        meteo_icon(x + 30, y + h - 30, 26, meteo_code);
        snprintf(b, sizeof(b), "%d\xC2\xB0", (int)floorf(meteo_temp + 0.5f));
        gx_text1(F_LARGE, x + 52, y + h - 22, b, RGBA(255, 255, 255, 255));
    }
    nv_raw = 0;
}
/* tuile : la meteo (icone, temperature, ville) */
static void il_meteo(f32 x, f32 y, f32 w, f32 h) {
    il_tile(x, y, w, h);
    il_label(x + 16, y + 21, "M\xC3\xA9t\xC3\xA9o");
    char b[120], d[120];
    nv_raw = 1;
    if (meteo_ok) {
        meteo_icon(x + 30, y + h - 22, 24, meteo_code);
        snprintf(b, sizeof(b), "%d\xC2\xB0", (int)floorf(meteo_temp + 0.5f));
        f32 tw = (f32)gx_text1(F_LARGE, x + 50, y + h - 13, b, RGBA(255, 255, 255, 255));
        text_display(geo_name, d, sizeof(d));
        fit_text(F_MED, d, (int)(w - 70 - tw), b, sizeof(b));
        gx_text1(F_MED, x + 58 + tw, y + h - 14, b, RGBA(220, 220, 236, 255));
    } else gx_text1(F_SMALL, x + 16, y + h - 16, city[0] ? (if_state != IF_IDLE ? "M\xC3\xA9t\xC3\xA9o..." : "M\xC3\xA9t\xC3\xA9o indisponible") : "Choisis ta ville (onglet INFOS)", RGBA(150, 150, 172, 255));
    nv_raw = 0;
}
/* tuile : le spectre (barres en degrade bleu -> rose) */
static void il_spectrum(f32 x, f32 y, f32 w, f32 h, int nb) {
    il_tile(x, y, w, h);
    il_label(x + 16, y + 22, "Spectre");
    f32 bx = x + 18, bw = w - 36, base = y + h - 18, mh = h - 54;
    f32 step = bw / nb, cw = step * 0.62f;
    nv_raw = 1;
    for (int i = 0; i < nb; i++) {
        int b0 = (i * NBARS) / nb, b1 = ((i + 1) * NBARS) / nb;
        f32 v = 0;
        for (int j = b0; j < b1 && j < NBARS; j++) if (bar_val[j] > v) v = bar_val[j];   /* (le plus haut de ses bandes) */
        if (paused || src == SRC_NONE) v *= 0.15f;
        f32 bh = 4 + v * mh;
        gx_rrect_fill(bx + i * step, base - bh, cw, bh, cw * 0.3f, au_b(255), au_a(255));
    }
    nv_raw = 0;
}
/* tuile : la liste MUSIQUE */
static void il_list(f32 x, f32 y, f32 w, f32 h) {
    il_tile(x, y, w, h);
    il_label(x + 16, y + 22, cur_group >= 0 || lib_find[0] ? "Liste" : (src == SRC_FILE && playing ? "\xC3\x80 suivre" : "Biblioth\xC3\xA8que"));
    nv_raw = 1;
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%d", nents);
    gx_text_r(F_SMALL, x + w - 16, y + 22, cnt, RGBA(130, 130, 150, 255));
    nv_raw = 0;
    const f32 rh = 23, y0 = y + 32;
    int rows = (int)((h - 32 - 26) / rh);
    if (rows < 1) rows = 1;
    int start = sel_row - rows / 2;
    if (start > nents - rows) start = nents - rows;
    if (start < 0) start = 0;
    for (int i = 0; i < rows; i++) {
        int r = start + i;
        if (r >= nents) break;
        f32 ry = y0 + i * rh;
        int on = r == sel_row;
        if (on) nv_sel_draw(x + 6, ry, w - 12, rh - 1, 8);
        st_ent_t e;
        st_entry(r, &e);
        int cur = e.kind == 0 && src == SRC_FILE && playing && e.k == cur_track;
        nv_raw = 1;
        col_t tc = on ? RGBA(255, 255, 255, 255) : RGBA(200, 200, 214, 255);
        f32 cy = ry + rh / 2;
        if (e.kind == 1) { if (view_mode == VIEW_FOLDERS) draw_folder_icon(x + 26, cy, tc); else if (view_mode == VIEW_ARTISTS) draw_person_icon(x + 26, cy, tc); else draw_disc_icon(x + 26, cy, tc); }
        else if (e.kind == 2) gx_tri(x + 30, cy - 5, x + 30, cy + 5, x + 22, cy, tc);
        else if (cur) {
            for (int j = 0; j < 3; j++) { f32 bh = 3.0f + (paused ? 2.0f : 8.0f * bar_val[3 + j * 9]); gx_rect(x + 20 + j * 4, cy + 5 - bh, 2.5f, bh, au_a(255)); }
        }
        char t[24], b[200];
        t[0] = 0;
        if (e.kind == 0) fmt_time(t, track_duration(e.k));
        else if (e.kind == 1) snprintf(t, sizeof(t), "%d", grp_count[ents[r].idx]);
        f32 tw = t[0] ? (f32)text_width(F_SMALL, t, 0) : 0;
        if (t[0]) gx_text_r(F_SMALL, x + w - 18, cy + 4, t, on ? RGBA(255, 255, 255, 255) : RGBA(150, 150, 168, 255));
        fit_text_sel(on, F_MED, e.kind == 2 ? e.sub : e.title, (int)(w - 60 - tw - 12), b, sizeof(b));
        gx_text1(F_MED, x + 42, cy + 5, b, tc);
        nv_raw = 0;
        gp_zone(x + 6, ry, w - 12, rh - 1, &sel_row, r, GPZ_SEL);
    }
    if (nents == 0) st_empty_music(x + w / 2, y + h / 2);
    st_music_foot(x + 16, y + h - 12);
}
static void il_music(void) {
    il_np(11, 73, 400, 160);
    il_clock(419, 73, 210, 96, 0);
    il_meteo(419, 177, 210, 56);
    il_list(11, 241, 300, 229);
    il_spectrum(319, 241, 310, 229, 22);
    gp_zone(11, 73, 400, 160, NULL, PAD_BUTTON_Y, GPZ_KEY);   /* (ce qui joue : A = pause) */
}
/* RADIO, RETRO, PODCASTS : a droite de la liste */
static void il_player(void) {
    il_np(289, 73, 341, 160);
    il_clock(289, 241, 164, 170, 1);
    il_spectrum(461, 241, 169, 170, 12);
}

/* INFOS en tuiles : heure, meteo (et previsions), reveil, actualites */
static void il_infos(void) {
    char buf[300], d[300];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    /* heure */
    il_tile(11, 73, 300, 150);
    il_label(27, 95, "Heure");
    nv_raw = 1;
    if (tm) {
        snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
        txt_scale = 2.5f;
        gx_text1(F_LARGE, 24, 172, buf, RGBA(250, 250, 255, 255));
        txt_scale = 1.0f;
        snprintf(buf, sizeof(buf), "%s %d %s", TR(st_jours[tm->tm_wday % 7]), tm->tm_mday, TR(st_mois[tm->tm_mon % 12]));
        gx_text1(F_MED, 28, 198, buf, RGBA(176, 176, 200, 255));
    }
    if (now_ms() - inf_msg_t < 3000) { fit_text(F_SMALL, inf_msg, 270, d, sizeof(d)); gx_text1(F_SMALL, 28, 214, d, RGBA(255, 210, 120, 255)); }
    nv_raw = 0;
    /* meteo */
    il_tile(11, 231, 300, 239);
    il_label(27, 253, "M\xC3\xA9t\xC3\xA9o");
    nv_raw = 1;
    if (meteo_ok) {
        text_display(geo_name, d, sizeof(d));
        fit_text(F_SMALL, d, 150, buf, sizeof(buf));
        gx_text_r(F_SMALL, 295, 253, buf, RGBA(150, 150, 172, 255));
        meteo_icon(56, 298, 54, meteo_code);
        snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)floorf(meteo_temp + 0.5f));
        txt_scale = 1.7f;
        gx_text1(F_LARGE, 98, 314, buf, RGBA(255, 255, 255, 255));
        txt_scale = 1.0f;
        gx_text1(F_MED, 180, 296, meteo_text(meteo_code), RGBA(220, 220, 236, 255));
        snprintf(buf, sizeof(buf), "Vent %d km/h", meteo_wind);
        gx_text1(F_SMALL, 180, 314, buf, RGBA(150, 150, 172, 255));
        snprintf(buf, sizeof(buf), "Humidit\xC3\xA9 %d %%", meteo_hum);
        gx_text1(F_SMALL, 180, 330, buf, RGBA(150, 150, 172, 255));
        static const char *const js[7] = { "dim", "lun", "mar", "mer", "jeu", "ven", "sam" };
        f32 fw = 268.0f / (ndays > 0 ? ndays : 1);
        for (int i = 0; i < ndays; i++) {
            f32 x = 27 + fw * i + fw / 2;
            int y = 0, m = 0, dd = 0;
            if (sscanf(day_date[i], "%d-%d-%d", &y, &m, &dd) == 3) {
                struct tm t2;
                memset(&t2, 0, sizeof(t2));
                t2.tm_year = y - 1900; t2.tm_mon = m - 1; t2.tm_mday = dd; t2.tm_hour = 12;
                mktime(&t2);
                gx_text_c(F_SMALL, x, 372, i == 0 ? "auj." : js[t2.tm_wday % 7], RGBA(150, 150, 172, 255));
            }
            meteo_icon(x, 400, 26, day_code[i]);
            snprintf(buf, sizeof(buf), "%d/%d", day_min[i], day_max[i]);
            gx_text_c(F_SMALL, x, 432, buf, RGBA(210, 210, 226, 255));
        }
    } else {
        gx_text1(F_MED, 27, 300, city[0] ? (if_state != IF_IDLE ? "M\xC3\xA9t\xC3\xA9o..." : "M\xC3\xA9t\xC3\xA9o indisponible")
                                       : "Choisis ta ville (A)", RGBA(255, 210, 120, 255));
    }
    nv_raw = 0;
    /* reveil (reglages) */
    il_tile(319, 73, 310, 128);
    il_label(335, 95, "R\xC3\xA9veil");
    const f32 rx = 335, rw = 278;
    for (int i = 0; i < 4; i++) {
        f32 ry = 104 + i * 23;
        int on = i == inf_sel;
        gp_zone(rx - 8, ry, rw + 16, 22, &inf_sel, i, GPZ_SEL);
        if (on) nv_sel_draw(rx - 8, ry, rw + 16, 22, 8);
        if (i == 0) snprintf(buf, sizeof(buf), "Ville : %s", city[0] ? city : "(choisir)");
        else if (i == 1) { if (alarm_on) snprintf(buf, sizeof(buf), "R\xC3\xA9veil : %02d:%02d", alarm_min / 60, alarm_min % 60); else snprintf(buf, sizeof(buf), "R\xC3\xA9veil : non"); }
        else if (i == 2) snprintf(buf, sizeof(buf), "Jours : %s", TR(alarm_days_names[alarm_days % 3]));
        else snprintf(buf, sizeof(buf), "Le r\xC3\xA9veil joue : %s", alarm_src > 0 && alarm_src <= nstations ? stations[alarm_src - 1].name : TR("ma musique (al\xC3\xA9" "atoire)"));
        text_display(buf, d, sizeof(d));
        nv_raw = 1;
        fit_text_sel(on, F_MED, d, (int)rw - (on && i ? 26 : 0), buf, sizeof(buf));
        gx_text1(F_MED, rx, ry + 16, buf, on ? RGBA(255, 255, 255, 255) : RGBA(206, 206, 222, 255));
        if (on && (i == 1 || i == 2 || i == 3)) {
            gx_tri(rx + rw - 4, ry + 6, rx + rw - 4, ry + 16, rx + rw + 2, ry + 11, RGBA(255, 255, 255, 220));
            gx_tri(rx + rw - 10, ry + 6, rx + rw - 10, ry + 16, rx + rw - 16, ry + 11, RGBA(255, 255, 255, 220));
        }
        nv_raw = 0;
    }
    /* actualites */
    il_tile(319, 209, 310, 261);
    il_label(335, 231, "Actualit\xC3\xA9s");
    nv_raw = 1;
    gx_text_r(F_SMALL, 613, 231, "X : actualiser", RGBA(120, 120, 140, 255));
    nv_raw = 0;
    const f32 nh = 21, ny0 = 240;
    int rows = (int)((470 - ny0 - 8) / nh);
    int first = 0;
    if (inf_sel >= 4) { first = inf_sel - 4 - rows / 2; if (first > nnews - rows) first = nnews - rows; if (first < 0) first = 0; }
    for (int j = 0; j < rows && first + j < nnews; j++) {
        int i = 4 + first + j;
        f32 ry = ny0 + j * nh;
        int on = i == inf_sel;
        gp_zone(rx - 8, ry, rw + 16, nh - 1, &inf_sel, i, GPZ_SEL);
        if (on) nv_sel_draw(rx - 8, ry, rw + 16, nh - 1, 8);
        nv_raw = 1;
        col_t c = on ? RGBA(255, 255, 255, 255) : RGBA(206, 206, 222, 255);
        gx_text1(F_SMALL, rx, ry + 14, news_time[i - 4], on ? c : au_b(255));
        text_display(news_title[i - 4], d, sizeof(d));
        fit_text_sel(on, F_SMALL, d, (int)rw - 40, buf, sizeof(buf));
        gx_text1(F_SMALL, rx + 40, ry + 14, buf, c);
        nv_raw = 0;
    }
    nv_raw = 1;
    if (nnews == 0) gx_text1(F_SMALL, rx, 258, if_state == IF_NEWS ? "Chargement..." : "Pas encore d'actualit\xC3\xA9s", RGBA(150, 150, 172, 255));
    nv_raw = 0;
}

/* ---------- appels depuis draw() ---------- */
/* INFOS : 1 = dessine */
static int st_infos_page(void) {
    if (ST_IL) { il_infos(); return 1; }
    return 0;
}
/* page entiere du lecteur (MUSIQUE) : 1 = dessinee */
static int st_music_page(void) {
    if (tab != TAB_MUSIC || player_layout != L_CLASSIC) return 0;
    if (ST_VI) { vi_music(); return 1; }
    if (ST_IL) { il_music(); if (kb_is_open()) draw_legend_web(); return 1; }
    return 0;
}
/* a droite de la liste (RADIO, RETRO, PODCASTS ; MUSIQUE en liste) : 1 = dessine */
static int st_player_side(void) {
    if (ST_VI) { vi_player(); return 1; }
    if (ST_IL) { il_player(); return 1; }
    return 0;
}

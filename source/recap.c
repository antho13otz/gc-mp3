/* GC Station v191 : ton recap (START > Outils > Ton recap, et la page /recap du telephone).
   Musique : ecoutes de la bibliotheque (play_cnt), morceaux et artistes preferes. Jeux (profil actuel) : temps de jeu
   mesure par Pulse, lancements, jeux preferes, succes, records des mini-jeux. */
typedef struct {
    u32 plays, tracks, games, runs, play, recs[5];
    int top_k[5], top_n, art_n, game_k[3], game_n, sc_got, sc_all;
    u32 top_c[5], art_c[3];
    char art[3][48];
} rcp_t;
static rcp_t rcp;
static int rcp_page = 0;
static const char *const rcp_recs[5] = { "jeu:snake", "jeu:snake-sans-murs", "jeu:casse-briques", "jeu:note-volante", "jeu:simon" };
static const char *const rcp_recn[5] = { "Snake", "Snake sans murs", "Casse-briques", "Note volante", "Simon" };
static void rcp_title(int k, char *o, int cap) {        /* "Titre - Artiste" (etiquettes), sinon le nom du fichier */
    if (tg_title[k][0]) { if (tg_artist[k][0]) snprintf(o, cap, "%s - %s", tg_title[k], tg_artist[k]); else snprintf(o, cap, "%s", tg_title[k]); return; }
    char t[NAME_LEN];
    text_display(base_of(names[k]), t, sizeof(t));
    strip_audio_ext(t);
    snprintf(o, cap, "%s", t);
}
static void rcp_build(void) {
    memset(&rcp, 0, sizeof(rcp));
    /* musique */
    for (int k = 0; k < nfiles; k++) {
        u32 c = play_cnt[k];
        if (!c) continue;
        rcp.plays += c; rcp.tracks++;
        int pos = rcp.top_n;                               /* 5 meilleurs */
        while (pos > 0 && rcp.top_c[pos - 1] < c) pos--;
        if (pos < 5) {
            for (int j = (rcp.top_n < 5 ? rcp.top_n : 4); j > pos; j--) { rcp.top_k[j] = rcp.top_k[j - 1]; rcp.top_c[j] = rcp.top_c[j - 1]; }
            rcp.top_k[pos] = k; rcp.top_c[pos] = c;
            if (rcp.top_n < 5) rcp.top_n++;
        }
    }
    {   /* artistes : total des ecoutes par artiste (etiquettes lues) */
        static char an[64][48]; static u32 ac[64]; int na = 0;
        for (int k = 0; k < nfiles; k++) {
            if (!play_cnt[k] || !tg_artist[k][0]) continue;
            int f = -1;
            for (int i = 0; i < na; i++) if (!strcasecmp(an[i], tg_artist[k])) f = i;
            if (f < 0 && na < 64) { f = na++; snprintf(an[f], sizeof(an[f]), "%s", tg_artist[k]); ac[f] = 0; }
            if (f >= 0) ac[f] += play_cnt[k];
        }
        for (int t = 0; t < 3 && t < na; t++) {
            int b = -1;
            for (int i = 0; i < na; i++) if (ac[i] && (b < 0 || ac[i] > ac[b])) b = i;
            if (b < 0) break;
            snprintf(rcp.art[rcp.art_n], sizeof(rcp.art[0]), "%s", an[b]); rcp.art_c[rcp.art_n++] = ac[b]; ac[b] = 0;
        }
    }
    /* jeux (profil actuel) */
    if (!gl_scanned) gl_scan_full(0);
    for (int k = 0; k < gl_n; k++) {
        if (gl[k].runs) { rcp.games++; rcp.runs += gl[k].runs; }
        rcp.play += gl[k].play;
        if (!gl[k].play) continue;
        int pos = rcp.game_n;
        while (pos > 0 && gl[rcp.game_k[pos - 1]].play < gl[k].play) pos--;
        if (pos < 3) {
            for (int j = (rcp.game_n < 3 ? rcp.game_n : 2); j > pos; j--) rcp.game_k[j] = rcp.game_k[j - 1];
            rcp.game_k[pos] = k;
            if (rcp.game_n < 3) rcp.game_n++;
        }
    }
    if (!sc_loaded) sc_load();
    for (int i = 0; i < SC_N; i++) if (sc_have[i]) rcp.sc_got++;
    rcp.sc_all = SC_N;
    for (int i = 0; i < 5; i++) { int r = rg_record_get(rcp_recs[i]); rcp.recs[i] = r > 0 ? (u32)r : 0; }
}
static void rcp_dur(u32 sec, char *o, int cap) { u32 m = sec / 60; if (m >= 60) snprintf(o, cap, "%u h %02u", (unsigned)(m / 60), (unsigned)(m % 60)); else snprintf(o, cap, "%u min", (unsigned)m); }
static void rcp_open(void) { rcp_build(); rcp_page = 0; tl_screen = TL_RECAP; crumb("recap"); }
static void rcp_input(u32 down, u32 nav, int fire) {
    if ((down & PAD_BUTTON_A) || (fire && (nav & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT)))) rcp_page ^= 1;
    if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) tl_screen = TL_NONE;
}
static void rcp_big(f32 x, f32 y, const char *num, const char *lab) {
    gx_text_c(F_LARGE, x, y, num, RGBA(255, 210, 120, 255));
    gx_text_c(F_SMALL, x, y + 20, TR(lab), RGBA(200, 190, 245, 230));
}
static void rcp_draw(void) {
    char b[200], fv[200], t[160];
    snprintf(t, sizeof(t), "%s%s%s", TR("Ton r\xC3\xA9" "cap"), gp_name[0] ? " - " : "", gp_name);
    tl_frame(t, rcp_page ? TR("Jeux  2 / 2") : TR("Musique  1 / 2"));
    if (!rcp_page) {
        snprintf(b, sizeof(b), "%u", (unsigned)rcp.plays); rcp_big(170, 118, b, "\xC3\xA9" "coutes");
        snprintf(b, sizeof(b), "%u", (unsigned)rcp.tracks); rcp_big(470, 118, b, "morceaux \xC3\xA9" "cout\xC3\xA9s");
        gx_text1(F_MED, 62, 176, TR("Tes morceaux pr\xC3\xA9" "f\xC3\xA9r\xC3\xA9s"), RGBA(255, 255, 255, 255));
        if (!rcp.top_n) gx_text1(F_SMALL, 74, 200, TR("Rien encore : \xC3\xA9" "coute ta musique (onglet MUSIQUE)"), RGBA(170, 155, 225, 255));
        for (int i = 0; i < rcp.top_n; i++) {
            rcp_title(rcp.top_k[i], t, sizeof(t));
            snprintf(b, sizeof(b), "%d.  %s", i + 1, t);
            fit_text(F_SMALL, b, 430, fv, sizeof(fv));
            gx_text1(F_SMALL, 74, 200 + i * 20, fv, RGBA(230, 222, 255, 255));
            snprintf(b, sizeof(b), "%u", (unsigned)rcp.top_c[i]);
            gx_text_r(F_SMALL, 572, 200 + i * 20, b, RGBA(190, 160, 255, 255));
        }
        gx_text1(F_MED, 62, 318, TR("Tes artistes"), RGBA(255, 255, 255, 255));
        for (int i = 0; i < rcp.art_n; i++) {
            snprintf(b, sizeof(b), "%d.  %s", i + 1, rcp.art[i]);
            fit_text(F_SMALL, b, 430, fv, sizeof(fv));
            gx_text1(F_SMALL, 74, 342 + i * 20, fv, RGBA(230, 222, 255, 255));
            snprintf(b, sizeof(b), "%u", (unsigned)rcp.art_c[i]);
            gx_text_r(F_SMALL, 572, 342 + i * 20, b, RGBA(190, 160, 255, 255));
        }
        if (!rcp.art_n) gx_text1(F_SMALL, 74, 342, TR("(artistes : d'apr\xC3\xA8s les \xC3\xA9tiquettes des morceaux)"), RGBA(170, 155, 225, 255));
    } else {
        rcp_dur(rcp.play, b, sizeof(b)); rcp_big(130, 118, b, "de jeu (Pulse)");
        snprintf(b, sizeof(b), "%u", (unsigned)rcp.games); rcp_big(320, 118, b, "jeux lanc\xC3\xA9s");
        snprintf(b, sizeof(b), "%d / %d", rcp.sc_got, rcp.sc_all); rcp_big(510, 118, b, "succ\xC3\xA8s");
        gx_text1(F_MED, 62, 176, TR("Tes jeux pr\xC3\xA9" "f\xC3\xA9r\xC3\xA9s"), RGBA(255, 255, 255, 255));
        if (!rcp.game_n) gx_text1(F_SMALL, 74, 200, TR("Rien encore : le temps de jeu est mesur\xC3\xA9 avec Pulse"), RGBA(170, 155, 225, 255));
        for (int i = 0; i < rcp.game_n; i++) {
            const gcg_t *g = &gl[rcp.game_k[i]];
            snprintf(b, sizeof(b), "%d.  %s", i + 1, g->title);
            fit_text(F_SMALL, b, 420, fv, sizeof(fv));
            gx_text1(F_SMALL, 74, 200 + i * 20, fv, RGBA(230, 222, 255, 255));
            rcp_dur(g->play, t, sizeof(t));
            gx_text_r(F_SMALL, 572, 200 + i * 20, t, RGBA(190, 160, 255, 255));
        }
        gx_text1(F_MED, 62, 282, TR("Tes records"), RGBA(255, 255, 255, 255));
        for (int i = 0; i < 5; i++) {
            gx_text1(F_SMALL, 74 + (i % 2) * 260, 306 + (i / 2) * 20, TR(rcp_recn[i]), RGBA(230, 222, 255, 255));
            snprintf(b, sizeof(b), "%u", (unsigned)rcp.recs[i]);
            gx_text_r(F_SMALL, 300 + (i % 2) * 272, 306 + (i / 2) * 20, rcp.recs[i] ? b : "-", RGBA(190, 160, 255, 255));
        }
        snprintf(b, sizeof(b), "%s : %u", TR("Lancements"), (unsigned)rcp.runs);
        gx_text1(F_SMALL, 74, 372, b, RGBA(170, 155, 225, 255));
    }
    gx_text_c(F_SMALL, 320, 404, TR("Aussi sur le t\xC3\xA9l\xC3\xA9phone : page /recap (onglet T\xC3\x89L\xC3\x89PHONE)"), RGBA(170, 155, 225, 230));
    static const char *const k[] = { "A", "B" }, *const l[] = { "PAGE SUIVANTE", "FERMER" };
    tl_legend(k, l, 2);
}
/* ---------- telephone : /api/recap (JSON, fait par la boucle) ---------- */
static void rcp_json(void) {
    rcp_build();
    sbuf_t s = { NULL, 0, 0 };
    char m[160], t[160];
    snprintf(m, sizeof(m), "{\"p\":\"%s\",\"plays\":%u,\"tracks\":%u,\"games\":%u,\"runs\":%u,\"play\":%u,\"sc\":%d,\"scn\":%d,\"top\":[", "", (unsigned)rcp.plays, (unsigned)rcp.tracks,
             (unsigned)rcp.games, (unsigned)rcp.runs, (unsigned)rcp.play, rcp.sc_got, rcp.sc_all);
    sb_add(&s, "{\"profil\":"); x73_json_raw(&s, gp_name); sb_add(&s, strchr(m, ',') ? strchr(m, ',') : ",");
    for (int i = 0; i < rcp.top_n; i++) { if (i) sb_add(&s, ","); rcp_title(rcp.top_k[i], t, sizeof(t)); sb_add(&s, "["); x73_json_raw(&s, t); snprintf(m, sizeof(m), ",%u]", (unsigned)rcp.top_c[i]); sb_add(&s, m); }
    sb_add(&s, "],\"art\":[");
    for (int i = 0; i < rcp.art_n; i++) { if (i) sb_add(&s, ","); sb_add(&s, "["); x73_json_raw(&s, rcp.art[i]); snprintf(m, sizeof(m), ",%u]", (unsigned)rcp.art_c[i]); sb_add(&s, m); }
    sb_add(&s, "],\"jeux\":[");
    for (int i = 0; i < rcp.game_n; i++) { const gcg_t *g = &gl[rcp.game_k[i]]; if (i) sb_add(&s, ","); sb_add(&s, "["); x73_json_raw(&s, g->title); snprintf(m, sizeof(m), ",%u,", (unsigned)g->play); sb_add(&s, m); x73_json_raw(&s, g->id); sb_add(&s, "]"); }
    sb_add(&s, "],\"rec\":[");
    for (int i = 0; i < 5; i++) { if (i) sb_add(&s, ","); sb_add(&s, "["); x73_json_raw(&s, TR(rcp_recn[i])); snprintf(m, sizeof(m), ",%u]", (unsigned)rcp.recs[i]); sb_add(&s, m); }
    sb_add(&s, "]}");
    char *old = x73_rj;
    x73_rj = s.b ? s.b : strdup("{}");
    x73_rj_ready = 1;
    free(old);
}

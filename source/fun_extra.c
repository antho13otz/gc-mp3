// GC-MP3 v18 (inclus dans main.c, apres gckb.c)
//  - Karaoke : les paroles en tres grand sur toute la tele, la ligne chantee se colore
//  - Jeu de rythme : des notes tombent sur le rythme de tes morceaux, A / B / X / Y au bon moment
//  - Quiz musical : 10 s d'un de tes morceaux, devine lequel (1 a 4 manettes)
//  - v20 : Snake et casse-briques musicaux (plus rapides quand la musique est forte)
//  - Reveil en douceur : le son monte pendant 3 minutes, l'heure et la meteo en grand
//  - La manette vibre sur les basses (mode soiree)
//  - Titres de la radio gardes (X dans l'onglet RADIO) : sd:/gc-mp3/gc-mp3-titres.txt
//  - Nouveaux episodes des podcasts telecharges tout seuls (sd:/gc-mp3/podcasts)
//  - Fond d'ecran : une de tes photos (PHOTOS, plein ecran, X)

enum { FUN_OFF, FUN_KARAOKE, FUN_RHYTHM, FUN_QUIZ, FUN_WAKE, FUN_SNAKE, FUN_BRICKS, FUN_PONG, FUN_FLAPPY, FUN_SIMON };
static int fun_mode = FUN_OFF;
static u64 fun_t0 = 0;
static u32 fun_pads = 1;                    /* manettes branchees (PAD_ScanPads) */

static const col_t lane_top[5] = { { 90, 230, 120, 255 }, { 245, 80, 95, 255 }, { 150, 190, 255, 255 }, { 255, 215, 80, 255 }, { 255, 150, 60, 255 } };
static const col_t lane_bot[5] = { { 30, 150, 60, 255 }, { 175, 30, 50, 255 }, { 70, 110, 210, 255 }, { 200, 150, 20, 255 }, { 200, 80, 20, 255 } };
static const char *const lane_btn[5] = { "A", "B", "X", "Y", "R" };
static int rg_lanes = 4;                      /* v73 : 4 touches (A B X Y) ou 5 (+ R) */

/* ---------- Vibrations ---------- */
static u64 rumble_until[4];
static void rumble(int ch, int ms) {
    if (!rumble_on || ch < 0 || ch > 3) return;
    if (!rumble_until[ch]) PAD_ControlMotor(ch, PAD_MOTOR_RUMBLE);
    u64 t = now_ms() + (u64)ms;
    if (t > rumble_until[ch]) rumble_until[ch] = t;
}
static void rumble_stop_all(void) {
    for (int ch = 0; ch < 4; ch++) if (rumble_until[ch]) { PAD_ControlMotor(ch, PAD_MOTOR_STOP); rumble_until[ch] = 0; }
}
static void rumble_pump(void) {
    u64 now = now_ms();
    for (int ch = 0; ch < 4; ch++)
        if (rumble_until[ch] && now >= rumble_until[ch]) { PAD_ControlMotor(ch, PAD_MOTOR_STOP); rumble_until[ch] = 0; }
}
/* mode soiree : un battement fort dans les basses */
static void party_rumble(float strength) {
    if (party_on) rumble(0, strength > 0.18f ? 120 : 70);
}

/* ---------- Outils communs ---------- */
static void fun_title_of(int k, char *out, int cap) {     /* nom du fichier (pas les etiquettes : quiz) */
    snprintf(out, cap, "%s", display_name(base_of(names[k])));
    strip_audio_ext(out);
}
static int fun_tracks(void) {
    int n = 0;
    for (int k = 0; k < nfiles; k++) n += !is_retro[k];
    return n;
}
static int fun_random_track(int avoid) {
    int n = fun_tracks();
    if (n == 0) return -1;
    for (int tries = 0; tries < 50; tries++) {
        int k = (int)(lib_rand() % (u32)nfiles);
        if (!is_retro[k] && (k != avoid || n == 1)) return k;
    }
    for (int k = 0; k < nfiles; k++) if (!is_retro[k] && k != avoid) return k;
    return -1;
}
static int fun_selected_track(void) {                  /* morceau choisi dans MUSIQUE */
    if (tab == TAB_MUSIC && sel_row >= 0 && sel_row < nents && ents[sel_row].type == E_TRACK) return ents[sel_row].idx;
    return -1;
}
static int fun_cp_count(const char *s) { int n = 0; while (*s) { next_cp(&s); n++; } return n; }
static void fun_cp_prefix(const char *s, int n, char *out, int cap) {
    const char *p = s;
    for (int i = 0; i < n && *p; i++) next_cp(&p);
    int len = (int)(p - s);
    if (len > cap - 1) len = cap - 1;
    memcpy(out, s, len);
    out[len] = 0;
}
static void fun_shadow_text(int f, f32 cx, f32 y, const char *s, f32 scale, col_t c) {
    txt_scale = scale;
    gx_text_c(f, cx + 2, y + 2, s, RGBA(0, 0, 0, 150));
    gx_text_c(f, cx, y, s, c);
    txt_scale = 1.0f;
}
static void fun_frame_begin(f32 hue, f32 lum) {
    scr_viewport();
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    col_t top = hsv(hue, 0.75f, 0.10f * lum, 255), bot = hsv(hue + 50, 0.85f, 0.03f * lum, 255);
    gx_quad4(0, 0, 640, 480, top, top, bot, bot);
}
static void fun_spectrum(f32 base, f32 hmax, f32 hue, u8 alpha) {   /* spectre discret en bas */
    const f32 bw = 640.0f / NBARS;
    for (int i = 0; i < NBARS; i++) {
        f32 h = bar_val[i] * hmax;
        if (h < 1) continue;
        col_t c = hsv(hue + i * 3.0f, 0.7f, 1.0f, alpha);
        gx_vgrad(i * bw, base - h, bw - 1, h, c, RGBA(c.r, c.g, c.b, 0));
    }
}
static void fun_footer(const char *s) { gx_text_c(F_SMALL, 320, 470, s, RGBA(255, 255, 255, 170)); }

static void fun_leave(void) {
    if (fun_mode == FUN_QUIZ || fun_mode == FUN_RHYTHM) stop_track();
    fun_mode = FUN_OFF;
    rumble_stop_all();
    last_input_ms = now_ms();
}
static void fun_enter(int m) {
    party_on = 0;
    saver_on = 0;
    am_open = 0;
    menu_open = 0;
    fun_mode = m;
    fun_t0 = now_ms();
}

/* ======================= Karaoke ======================= */
static int ka_line = -3;
static f32 ka_scroll = 0;

static void karaoke_start(int k) {
    if (k < 0 && !(src == SRC_FILE && playing)) k = fun_random_track(-1);
    if (k >= 0 && !(src == SRC_FILE && playing && cur_track == k)) { queue_build(k); start_track(k); }
    fun_enter(FUN_KARAOKE);
    ka_line = -3;
    ka_scroll = 0;
    if (lyrics_auto) net_start_async();
}

/* la ligne chantee : en tres grand, coloree de gauche a droite */
static void ka_big_line(const char *raw, f32 cy, f32 frac) {
    char d[300], a[300], b[300], p[300];
    text_display(raw, d, sizeof(d));
    if (!d[0]) snprintf(d, sizeof(d), "* * *");                 /* (passage sans paroles) */
    f32 sc = 1.9f;
    txt_scale = sc;
    while (text_width(F_LARGE, d, 0) > 600 && sc > 1.3f) { sc -= 0.05f; txt_scale = sc; }
    wrap2(F_LARGE, d, 600, a, b, sizeof(a));
    f32 lh = font_height[F_LARGE] * sc;
    int na = fun_cp_count(a), nb = fun_cp_count(b), lit = (int)(frac * (na + nb) + 0.5f);
    for (int l = 0; l < 2; l++) {
        const char *s = l ? b : a;
        if (!s[0]) continue;
        f32 y = (b[0] ? cy - lh * 0.45f : cy + lh * 0.1f) + l * lh;
        f32 x = UX(320) - text_width_px(F_LARGE, s, 0) / 2.0f;
        int n = l ? lit - na : lit;
        gx_text_raw(F_LARGE, x + 3, y + 3, s, RGBA(0, 0, 0, 170), RGBA(0, 0, 0, 170), 0);
        gx_text_raw(F_LARGE, x, y, s, RGBA(235, 230, 255, 255), RGBA(190, 180, 230, 255), 0);
        if (n > 0) {
            fun_cp_prefix(s, n, p, sizeof(p));
            gx_text_raw(F_LARGE, x, y, p, RGBA(255, 245, 130, 255), RGBA(255, 160, 40, 255), 0);
        }
    }
    txt_scale = 1.0f;
}

static void ka_small_line(int i, f32 y, u8 alpha, f32 scale) {
    if (i < 0 || i >= lrc_n) return;
    char d[300], t[300];
    text_display(lyrics_line(i), d, sizeof(d));
    if (!d[0]) return;
    txt_scale = scale;
    fit_text(F_LARGE, d, 600, t, sizeof(t));
    gx_text_c(F_LARGE, 322, y + 2, t, RGBA(0, 0, 0, alpha / 2));
    gx_text_c(F_LARGE, 320, y, t, RGBA(225, 215, 255, alpha));
    txt_scale = 1.0f;
}

static void draw_karaoke(void) {
    f32 hue = 250 + (f32)((now_ms() - fun_t0) % 120000) / 120000.0f * 360.0f;
    fun_frame_begin(hue, 1.0f + pt_beat * 0.5f);
    fun_spectrum(480, 70, hue, 70);
    char ti[200], sub[200], t[200];
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    fit_text(F_MED, ti, 560, t, sizeof(t));
    gx_text_c(F_MED, 320, 30, t, RGBA(255, 255, 255, 220));
    if (sub[0]) { fit_text(F_SMALL, sub, 560, t, sizeof(t)); gx_text_c(F_SMALL, 320, 48, t, RGBA(200, 190, 240, 200)); }
    gx_text(F_SMALL, 24, 30, "KARAOK\xC3\x89", RGBA(255, 210, 110, 255), RGBA(255, 160, 60, 255), 3);

    int have = src == SRC_FILE && playing && lrc_n > 0 && lrc_track == cur_track;
    if (!(src == SRC_FILE && playing)) {
        fun_shadow_text(F_LARGE, 320, 230, "Lance un morceau", 1.4f, RGBA(255, 255, 255, 255));
        gx_text_c(F_MED, 320, 270, "(onglet MUSIQUE, puis X > Karaok\xC3\xA9)", RGBA(210, 200, 245, 220));
    } else if (!have) {
        const char *m = !lyrics_auto ? "Paroles automatiques : Non (START > R\xC3\xA9glages)" :
                        (ly_state == LY_WAIT || ly_state == LY_FETCH) && ly_track == cur_track ? "Recherche des paroles..." :
                        "Pas de paroles pour ce morceau";
        fun_shadow_text(F_LARGE, 320, 225, m, 1.2f, RGBA(255, 255, 255, 255));
        gx_text_c(F_MED, 320, 268, "Paroles : LRCLIB sur Internet, ou un fichier .lrc \xC3\xA0 c\xC3\xB4t\xC3\xA9 du morceau", RGBA(210, 200, 245, 220));
    } else {
        int li = lyrics_current();
        if (li != ka_line) { ka_scroll = (li == ka_line + 1) ? 1.0f : 0.0f; ka_line = li; }
        ka_scroll *= expf(-ui_dt * 9.0f);
        f32 off = ka_scroll * 64.0f;
        u32 ms = (u32)((u64)consumed_heard() * 1000u / 48000u) + 150;
        f32 frac = 0;
        if (li >= 0) {
            u32 st = lrc_lines[li].ms, nx = li + 1 < lrc_n ? lrc_lines[li + 1].ms : st + 4000;
            int n = fun_cp_count(lyrics_line(li));
            f32 dur = (f32)(nx - st) * 0.92f, want = 900.0f + n * 120.0f;
            if (dur > want) dur = want;
            if (dur < 300) dur = 300;
            frac = (f32)(ms - st) / dur;
            if (frac > 1) frac = 1;
            if (frac < 0) frac = 0;
        }
        ka_small_line(li - 2, 112 + off, 70, 0.95f);
        ka_small_line(li - 1, 164 + off, 120, 1.05f);
        ka_big_line(li >= 0 ? lyrics_line(li) : "...", 252 + off * 0.5f, frac);
        ka_small_line(li + 1, 352 + off, 200, 1.15f);
        ka_small_line(li + 2, 404 + off, 110, 1.0f);
    }
    /* avancement */
    int d = cur_track >= 0 ? file_duration(names[cur_track]) : 0;
    if (src == SRC_FILE && d > 0) {
        f32 pf = (f32)(consumed / 48000u) / d;
        if (pf > 1) pf = 1;
        gx_rrect_fill(120, 444, 400, 4, 2, RGBA(255, 255, 255, 40), RGBA(255, 255, 255, 40));
        gx_rrect_fill(120, 444, 400 * pf, 4, 2, RGBA(255, 220, 110, 230), RGBA(255, 160, 60, 230));
    }
    fun_footer(paused ? "PAUSE   -   Y : reprendre   B : sortir" : "L / R : morceau     Y : pause     B : sortir");
}

static void karaoke_input(u32 down) {
    if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) { fun_leave(); return; }
    if ((down & (PAD_BUTTON_Y | PAD_BUTTON_A)) && src == SRC_FILE && playing) paused = !paused;
}

/* ======================= Jeu de rythme ======================= */
#define RG_NOTES 64
#define RG_FALL 1500                         /* ms pour descendre jusqu'a la ligne */
#define RG_HIT_Y 396.0f
#define RG_TOP_Y 70.0f
typedef struct { u64 t; u8 lane, st[2]; } rnote_t;      /* st (par joueur) : 0 a venir, 1 touchee, 2 ratee */
static rnote_t rg_n[RG_NOTES];
static int rg_phase = 0;                     /* 0 choix, 1 partie, 2 resultat */
static int rg_diff = 1, rg_track = -1, rg_record = 0, rg_new_record = 0;
static int rg_players = 1;                   /* v21 : 2 = duel (manettes 1 et 2) */
typedef struct { int score, combo, best_combo, hits, total, perfect; char judge[24]; u64 judge_t; col_t judge_c; f32 flash[5]; } rgp_t;
static rgp_t rgp[2];
static u64 rg_ons[16];
static int rg_non = 0, rg_lock = 0, rg_beatn = 0, rg_lastlane = -1, rg_samelane = 0;
static float rg_avg = 0, rg_period = 500;
static double rg_anchor = 0, rg_sched = 0;
static int rg_armed = 1;
static u64 rg_last_on = 0, rg_start_t = 0;
/* v70 : les coups sont cherches dans le son deja decode (jusqu'a 4 s d'avance) : chaque note tombe
   sur un vrai coup du morceau (avant : une grille de tempo devinee, en temps de la console : au
   moindre retard du son, tout se decalait). L'horloge est la position du son entendu (audio_ms). */
#define RG_FR 512                                 /* (une "image" d'analyse : 10,7 ms de son) */
/* analyse commune (jeu de rythme, mini-jeux) : coups trouves dans le son deja decode */
static u32 ba_scan = 0;
static float ba_lp = 0, ba_prev = 0, ba_lo_h[3], ba_hi_h[3], ba_od[5], ba_odlo[5], ba_odhi[5], ba_th_avg = 1, ba_th_dev = 1;
static void ba_reset(void) {
    ba_scan = 0; ba_lp = ba_prev = 0; ba_th_avg = ba_th_dev = 1;
    memset(ba_lo_h, 0, sizeof(ba_lo_h)); memset(ba_hi_h, 0, sizeof(ba_hi_h)); memset(ba_od, 0, sizeof(ba_od));
    memset(ba_odlo, 0, sizeof(ba_odlo)); memset(ba_odhi, 0, sizeof(ba_odhi));
}
/* cb(echantillon, graves, aigus, force, seuil) pour chaque coup */
static void ba_analyse(void (*cb)(u32, float, float, float, float)) {
    u32 lv = IRQ_Disable();
    u32 c = consumed, sk = consumed_skip, h = (u32)f_head, n = (u32)f_count;
    IRQ_Restore(lv);
    if (n <= sk) return;
    u32 end = c + (n - sk);
    if (ba_scan < c) ba_scan = c;                       /* (en retard : on saute) */
    for (int fr = 0; fr < 96 && ba_scan + RG_FR <= end; fr++) {
        float elo = 0, ehi = 0;
        u32 j = ba_scan - c + sk;
        for (int i = 0; i < RG_FR; i++) {
            const stereo_t *st = &fifo[(h + j + i) % FIFO_SIZE];
            float x = ((float)st->l + (float)st->r) * (1.0f / 65536.0f);
            ba_lp += 0.02f * (x - ba_lp);                   /* (graves : en dessous de ~150 Hz) */
            float d = x - ba_prev; ba_prev = x;               /* (aigus : caisse claire, charleston) */
            elo += ba_lp * ba_lp; ehi += d * d;
        }
        float ldb = 10.0f * log10f(elo / RG_FR + 1e-10f), hdb = 10.0f * log10f(ehi / RG_FR + 1e-10f);
        float pl = ba_lo_h[0] > ba_lo_h[1] ? ba_lo_h[0] : ba_lo_h[1]; if (ba_lo_h[2] > pl) pl = ba_lo_h[2];
        float ph = ba_hi_h[0] > ba_hi_h[1] ? ba_hi_h[0] : ba_hi_h[1]; if (ba_hi_h[2] > ph) ph = ba_hi_h[2];
        float rl = ldb - pl, rh = hdb - ph;
        if (rl < 0) rl = 0;
        if (rh < 0) rh = 0;
        if (ldb < -60.0f) rl = 0;
        if (hdb < -60.0f) rh = 0;
        ba_lo_h[2] = ba_lo_h[1]; ba_lo_h[1] = ba_lo_h[0]; ba_lo_h[0] = ldb;
        ba_hi_h[2] = ba_hi_h[1]; ba_hi_h[1] = ba_hi_h[0]; ba_hi_h[0] = hdb;
        float od = rl + 0.6f * rh;
        for (int k = 4; k > 0; k--) { ba_od[k] = ba_od[k - 1]; ba_odlo[k] = ba_odlo[k - 1]; ba_odhi[k] = ba_odhi[k - 1]; }
        ba_od[0] = od; ba_odlo[0] = rl; ba_odhi[0] = 0.6f * rh;
        float v = ba_od[2], thr = ba_th_avg + 1.5f * ba_th_dev;      /* (sommet 2 images plus tot) */
        if (thr < 1.5f) thr = 1.5f;
        if (v > thr && v > ba_od[3] && v > ba_od[4] && v >= ba_od[1] && v >= ba_od[0])
            cb(ba_scan - 2 * RG_FR + RG_FR / 2, ba_odlo[2], ba_odhi[2], v, ba_th_avg + 1.5f * ba_th_dev);
        ba_th_avg = ba_th_avg * 0.97f + od * 0.03f;
        float ad = od - ba_th_avg; if (ad < 0) ad = -ad;
        ba_th_dev = ba_th_dev * 0.97f + ad * 0.03f;
        ba_scan += RG_FR;
    }
}
/* v72 : pour les mini-jeux : les prochains coups (temps du son entendu, en ms) */
#define BA_N 32
static double ba_t[BA_N];
static u8 ba_strong[BA_N];
static int ba_w = 0, ba_track = -2;
static u32 ba_c0 = 0;
static double ba_seen = -1e9, ba_last_on = -1e9;
static void ba_on(u32 smp, float lo, float hi, float od, float thr) {
    double t = smp / 48.0 + rg_offset;
    if (t - ba_last_on < 100) return;
    ba_last_on = t;
    ba_t[ba_w % BA_N] = t; ba_strong[ba_w % BA_N] = lo >= hi || od > thr * 1.8f; ba_w++;
}
static int ba_ok(void) { return src == SRC_FILE && playing && !paused; }
static void ba_pump(void) {                          /* (mini-jeux : a chaque image) */
    if (!ba_ok()) return;
    if (cur_track != ba_track || consumed < ba_c0) { ba_reset(); ba_w = 0; ba_seen = ba_last_on = -1e9; ba_track = cur_track; }
    ba_c0 = consumed;
    ba_analyse(ba_on);
}
static int ba_beat_now(void) {                       /* un coup fort vient d'etre entendu ? (une fois) */
    double an = audio_ms();
    int hit = 0;
    for (int i = ba_w > BA_N ? ba_w - BA_N : 0; i < ba_w; i++) {
        double t = ba_t[i % BA_N];
        if (t > ba_seen && t <= an && ba_strong[i % BA_N]) { ba_seen = t; if (an - t < 150) hit = 1; }
    }
    return hit;
}
static double ba_next_strong(double after) {         /* prochain coup fort apres 'after' (-1 : pas encore connu) */
    double best = -1;
    for (int i = ba_w > BA_N ? ba_w - BA_N : 0; i < ba_w; i++) {
        double t = ba_t[i % BA_N];
        if (t > after && ba_strong[i % BA_N] && (best < 0 || t < best)) best = t;
    }
    return best;
}
static double rg_last_onset = -1e9, rg_last_note = -1e9, rg_last_low = -1e9;
static const char *const rg_diff_names[3] = { "Facile", "Normal", "Difficile" };
#define RG_FILE GCD "/gc-mp3-jeux.txt"

static int rg_record_get(const char *path) {
    FILE *f = fopen(RG_FILE, "r");
    if (!f) return 0;
    char line[NAME_LEN + 32];
    int best = 0;
    while (fgets(line, sizeof(line), f)) {
        char *bar = strchr(line, '|');
        if (!bar) continue;
        char *p = trim(bar + 1);
        if (!strcmp(p, path)) { int s = atoi(line); if (s > best) best = s; }
    }
    fclose(f);
    return best;
}
static void rg_record_set(const char *path, int score) {
    char *buf = (char *)malloc(64 * 1024);
    int len = 0;
    if (!buf) return;
    FILE *f = fopen(RG_FILE, "r");
    if (f) {
        char line[NAME_LEN + 32];
        while (fgets(line, sizeof(line), f)) {
            char *bar = strchr(line, '|');
            if (bar && !strcmp(trim(bar + 1), path)) continue;   /* (l'ancien record s'en va) */
            int n = (int)strlen(line);
            if (len + n + 2 < 60 * 1024) { memcpy(buf + len, line, n); len += n; if (n && line[n - 1] != '\n') buf[len++] = '\n'; }
        }
        fclose(f);
    }
    len += snprintf(buf + len, 64 * 1024 - len, "%d | %s\n", score, path);
    f = fopen(RG_FILE, "w");
    if (f) { fwrite(buf, 1, len, f); fclose(f); }
    free(buf);
}

static void rg_new_game(int k) {
    if (k < 0) k = fun_random_track(rg_track);
    if (k < 0) return;
    memset(rg_n, 0, sizeof(rg_n));
    memset(rgp, 0, sizeof(rgp));
    rg_non = rg_lock = rg_beatn = 0;
    rg_lastlane = -1;
    rg_avg = 0;
    rg_period = 500;
    rg_anchor = rg_sched = 0;
    rg_armed = 1;
    ba_reset();
    rg_last_onset = rg_last_note = rg_last_low = -1e9;
    rg_track = k;
    queue_build(k);
    start_track(k);
    rg_record = rg_record_get(names[k]);
    rg_new_record = 0;
    rg_start_t = now_ms();
    rg_phase = 1;
}

static void rhythm_start(int k) {
    fun_enter(FUN_RHYTHM);
    rg_phase = 0;
    rg_track = k;
}

/* tempo : histogramme des ecarts entre les coups de basse (ramenes entre 340 et 750 ms) */
static void rg_estimate(void) {
    static int hist[42];
    memset(hist, 0, sizeof(hist));
    int n = rg_non < 16 ? rg_non : 16;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            u64 a = rg_ons[i], b = rg_ons[j];
            double d = a > b ? (double)(a - b) : (double)(b - a);
            if (d < 150 || d > 4000) continue;
            while (d > 750) d /= 2;
            while (d < 340) d *= 2;
            int bin = (int)((d - 340) / 10);
            if (bin >= 0 && bin < 42) hist[bin] += 2;
            if (bin > 0) hist[bin - 1]++;
            if (bin + 1 < 42) hist[bin + 1]++;
        }
    int best = 0;
    for (int i = 1; i < 42; i++) if (hist[i] > hist[best]) best = i;
    if (hist[best] >= 6) {
        float p = 340 + best * 10 + 5;
        rg_period = rg_lock ? rg_period * 0.7f + p * 0.3f : p;
    }
}


static void rg_spawn(double t, int strong) {               /* strong : 1 = coup grave, 0 = aigu */
    int L = rg_lanes == 5 ? 5 : 4;
    int lane = strong ? (int)(lib_rand() % 2) : 2 + (int)(lib_rand() % (u32)(L - 2));
    if (lib_rand() % 3 == 0) lane = (int)(lib_rand() % (u32)L);
    if (lane == rg_lastlane && ++rg_samelane >= 2) { lane = (lane + 1 + (int)(lib_rand() % (u32)(L - 1))) % L; rg_samelane = 0; }
    else if (lane != rg_lastlane) rg_samelane = 0;
    rg_lastlane = lane;
    for (int i = 0; i < RG_NOTES; i++)
        if (rg_n[i].t == 0) { rg_n[i].t = (u64)t; rg_n[i].lane = (u8)lane; rg_n[i].st[0] = rg_n[i].st[1] = 0; return; }
    (void)strong;
}

static void rg_judge_set(int p, const char *s, col_t c) {
    snprintf(rgp[p].judge, sizeof(rgp[p].judge), "%s", s);
    rgp[p].judge_c = c;
    rgp[p].judge_t = now_ms();
}

static void rg_finish(void) {
    rg_phase = 2;
    int best = rgp[0].score > rgp[1].score || rg_players == 1 ? 0 : 1;
    if (rg_track >= 0 && rg_track < nfiles && rgp[best].score > rg_record && rgp[best].score > 0) {
        rg_new_record = 1;
        if (sd_ok) rg_record_set(names[rg_track], rgp[best].score);
    }
    stop_track();
    rumble(best, 250);
}

/* un coup trouve dans le son a venir (index d'echantillon dans le morceau) */
static void rg_onset(u32 smp, float lo, float hi, float od, float thr0) {
    double t = smp / 48.0 + rg_offset;
    if (t - rg_last_onset < 100) return;
    rg_last_onset = t;
    memmove(rg_ons + 1, rg_ons, sizeof(rg_ons) - sizeof(rg_ons[0]));      /* (tempo affiche) */
    rg_ons[0] = (u64)t; rg_non++;
    if (rg_non >= 6) rg_estimate();
    static const float gap[3] = { 430, 280, 165 }, need[3] = { 1.6f, 1.2f, 1.0f };
    if (od < thr0 * need[rg_diff]) return;                              /* (facile : seulement les gros coups) */
    /* (les coups graves d'abord : ils ne regardent que la note grave d'avant ; les aigus ne
       viennent qu'entre deux, s'il y a la place) */
    int low = lo >= hi;
    if (low) { if (t - rg_last_low < gap[rg_diff] || t - rg_last_note < 150) return; rg_last_low = t; }
    else if (t - rg_last_note < gap[rg_diff] * (rg_diff == 0 ? 2.2f : rg_diff == 1 ? 1.6f : 1.0f) || t - rg_last_low < gap[rg_diff] * 0.5f) return;
    if (t < audio_ms() + 700) return;                                   /* (trop tard pour la voir tomber) */
    rg_last_note = t;
    rg_lock = 1;
    rg_spawn(t, low);
}
static void rhythm_pump(void) {
    if (rg_phase != 1) return;
    if (src != SRC_FILE || !playing || cur_track != rg_track) { rg_finish(); return; }
    if (paused) return;
    ba_analyse(rg_onset);
    double now = audio_ms();
    /* notes ratees */
    for (int i = 0; i < RG_NOTES; i++) {
        if (!rg_n[i].t) continue;
        for (int p = 0; p < rg_players; p++)
            if (rg_n[i].st[p] == 0 && now > (double)rg_n[i].t + 170) {
                rg_n[i].st[p] = 2;
                rgp[p].total++;
                rgp[p].combo = 0;
                rg_judge_set(p, "Rat\xC3\xA9", RGBA(255, 110, 110, 255));
                rumble(p, 60);
            }
        if (now > (double)rg_n[i].t + 600) rg_n[i].t = 0;              /* (place libre) */
    }
    for (int p = 0; p < 2; p++) for (int l = 0; l < 5; l++) rgp[p].flash[l] *= expf(-ui_dt * 8.0f);
    last_input_ms = now_ms();
}

static void rg_press(int p, int lane) {
    double now = audio_ms();
    rgp_t *g = &rgp[p];
    g->flash[lane] = 1.0f;
    int best = -1;
    long bd = 1000000;
    for (int i = 0; i < RG_NOTES; i++) {
        if (!rg_n[i].t || rg_n[i].st[p] || rg_n[i].lane != lane) continue;
        long d = (long)(now - (double)rg_n[i].t);
        if (labs(d) < labs(bd)) { bd = d; best = i; }
    }
    if (best < 0 || labs(bd) > 170) {                       /* rien a toucher */
        if (g->combo > 0) rg_judge_set(p, "Trop t\xC3\xB4t", RGBA(255, 190, 120, 255));
        g->combo = 0;
        return;
    }
    int pts;
    if (labs(bd) <= 60) { pts = 100; g->perfect++; rg_judge_set(p, "Parfait !", RGBA(255, 240, 120, 255)); }
    else if (labs(bd) <= 110) { pts = 60; rg_judge_set(p, "Bien", RGBA(140, 255, 160, 255)); }
    else { pts = 25; rg_judge_set(p, "OK", RGBA(170, 200, 255, 255)); }
    rg_n[best].st[p] = 1;
    g->combo++;
    if (g->combo > g->best_combo) g->best_combo = g->combo;
    int mult = 1 + (g->combo / 10 > 3 ? 3 : g->combo / 10);
    g->score += pts * mult;
    g->hits++;
    g->total++;
}

static void rhythm_input(u32 down, u32 nav, int fire) {
    if (rg_phase == 0) {
        if (fire && (nav & PAD_BUTTON_LEFT)) rg_diff = (rg_diff + 2) % 3;
        if (fire && (nav & (PAD_BUTTON_RIGHT))) rg_diff = (rg_diff + 1) % 3;
        if (fire && (nav & (PAD_BUTTON_UP | PAD_BUTTON_DOWN))) rg_players = 3 - rg_players;   /* solo / duel */
        if (down & PAD_TRIGGER_L) { rg_offset -= 10; if (rg_offset < -200) rg_offset = -200; settings_dirty = 1; }
        if (down & PAD_TRIGGER_R) { rg_offset += 10; if (rg_offset > 200) rg_offset = 200; settings_dirty = 1; }
        if (down & PAD_BUTTON_Y) { rg_lanes = rg_lanes == 5 ? 4 : 5; settings_dirty = 1; }
        if (down & PAD_BUTTON_A) rg_new_game(rg_track);
        if (down & PAD_BUTTON_X) rg_new_game(-1);
        if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave();
        return;
    }
    if (rg_phase == 2) {
        if (down & PAD_BUTTON_A) rg_new_game(rg_track);
        if (down & PAD_BUTTON_X) rg_new_game(-1);
        if (down & PAD_BUTTON_Y) rg_phase = 0;
        if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave();
        return;
    }
    if (down & PAD_BUTTON_START) { rg_finish(); return; }
    if (down & PAD_TRIGGER_Z) { paused = !paused; return; }
    if (paused) return;
    for (int p = 0; p < rg_players; p++) {
        u32 d = p == 0 ? down : PAD_ButtonsDown(1);
        if (d & PAD_BUTTON_A) rg_press(p, 0);
        if (d & PAD_BUTTON_B) rg_press(p, 1);
        if (d & PAD_BUTTON_X) rg_press(p, 2);
        if (d & PAD_BUTTON_Y) rg_press(p, 3);
        if ((d & PAD_TRIGGER_R) && rg_lanes == 5) rg_press(p, 4);
    }
}

static void rg_button(f32 cx, f32 cy, f32 r, int lane, f32 glow) {
    if (glow > 0.02f) gx_circle_glow(cx, cy, r + 4, RGBA(lane_top[lane].r, lane_top[lane].g, lane_top[lane].b, (u8)(glow * 200)), 6);
    gx_circle(cx, cy, r, lane_top[lane], lane_bot[lane]);
    gx_text_c(F_MED, cx, cy + 6, lane_btn[lane], RGBA(255, 255, 255, 255));
}

/* une piste de 4 boutons (joueur p) */
static void rg_highway(int p, f32 lx, f32 lw) {
    double now = audio_ms();
    rgp_t *g = &rgp[p];
    f32 rb = lw * 0.31f;
    for (int l = 0; l < rg_lanes; l++) {
        f32 x = lx + l * lw;
        gx_vgrad(x + 2, RG_TOP_Y - 10, lw - 4, RG_HIT_Y - RG_TOP_Y + 40, RGBA(255, 255, 255, 8), RGBA(lane_top[l].r, lane_top[l].g, lane_top[l].b, (u8)(30 + g->flash[l] * 90)));
    }
    gx_rect(lx - 6, RG_HIT_Y - 1, lw * rg_lanes + 12, 3, RGBA(255, 255, 255, 120));
    for (int l = 0; l < rg_lanes; l++) {
        f32 cx = lx + l * lw + lw / 2;
        gx_ring(cx, RG_HIT_Y, rb, rb + 3, RGBA(lane_top[l].r, lane_top[l].g, lane_top[l].b, (u8)(120 + g->flash[l] * 135)));
        gx_text_c(F_SMALL, cx, RG_HIT_Y + 42, lane_btn[l], RGBA(255, 255, 255, 150));
    }
    for (int i = 0; i < RG_NOTES; i++) {
        if (!rg_n[i].t || rg_n[i].st[p] == 1) continue;
        f32 dt = (f32)((double)rg_n[i].t - now);
        f32 y = RG_HIT_Y - dt / RG_FALL * (RG_HIT_Y - RG_TOP_Y);
        if (y < RG_TOP_Y - 20 || y > 480) continue;
        int l = rg_n[i].lane;
        f32 cx = lx + l * lw + lw / 2, r = rb * 0.6f + rb * 0.32f * (y - RG_TOP_Y) / (RG_HIT_Y - RG_TOP_Y);
        if (rg_n[i].st[p] == 2) gx_circle(cx, y, r, RGBA(90, 80, 110, 160), RGBA(50, 40, 70, 160));
        else rg_button(cx, y, r, l, 0);
    }
}

static void draw_rhythm(void) {
    u64 now = now_ms();
    f32 hue = pt_hue;
    fun_frame_begin(hue, 1.0f + pt_beat);
    char t[200], b2[200];
    if (rg_phase == 0) {
        fun_shadow_text(F_LARGE, 320, 92, "Jeu de rythme", 1.6f, RGBA(255, 230, 140, 255));
        gx_text_c(F_MED, 320, 132, "Les notes tombent sur le rythme de ta musique.", RGBA(230, 220, 255, 255));
        gx_text_c(F_MED, 320, 154, "Appuie sur le bon bouton quand la note touche la ligne.", RGBA(230, 220, 255, 255));
        for (int l = 0; l < rg_lanes; l++) rg_button(320 - (rg_lanes - 1) * 35 + l * 70, 205, 22, l, 0.5f);
        snprintf(t, sizeof(t), "Difficult\xC3\xA9 :   <  %s  >", rg_diff_names[rg_diff]);
        fun_shadow_text(F_LARGE, 320, 264, t, 1.0f, RGBA(255, 255, 255, 255));
        snprintf(t, sizeof(t), "Joueurs :   %s", TR(rg_players == 2 ? "DUEL (2 manettes)" : "1 joueur"));
        fun_shadow_text(F_LARGE, 320, 296, t, 0.9f, rg_players == 2 ? RGBA(255, 210, 90, 255) : RGBA(255, 255, 255, 255));
        if (rg_track >= 0 && rg_track < nfiles) { fun_title_of(rg_track, b2, sizeof(b2)); fit_text(F_MED, b2, 520, t, sizeof(t)); }
        else snprintf(t, sizeof(t), "un morceau au hasard");
        gx_text_c(F_MED, 320, 330, t, RGBA(200, 190, 245, 255));
        gx_text_c(F_SMALL, 320, 352, rg_lanes == 5 ? "(haut / bas : solo ou duel ; Y : 5 touches avec R -> 4 touches)" : "(haut / bas : solo ou duel ; Y : passer \xC3\xA0 5 touches, avec R)", RGBA(170, 160, 220, 220));
        snprintf(t, sizeof(t), TR("L / R : d\xC3\xA9" "calage des notes  %+d ms  (si elles semblent en avance ou en retard)"), rg_offset);
        gx_text_c(F_SMALL, 320, 372, t, rg_offset ? RGBA(255, 220, 140, 230) : RGBA(170, 160, 220, 200));
        fun_footer(rg_track >= 0 ? "A : jouer ce morceau     X : un morceau au hasard     B : sortir" : "A : jouer     X : un autre morceau au hasard     B : sortir");
        return;
    }
    if (rg_phase == 2) {
        fun_shadow_text(F_LARGE, 320, 90, "R\xC3\xA9sultat", 1.6f, RGBA(255, 230, 140, 255));
        if (rg_track >= 0 && rg_track < nfiles) { fun_title_of(rg_track, b2, sizeof(b2)); fit_text(F_MED, b2, 520, t, sizeof(t)); gx_text_c(F_MED, 320, 124, t, RGBA(210, 200, 250, 255)); }
        if (rg_players == 2) {                                   /* duel */
            const char *w = rgp[0].score == rgp[1].score ? "\xC3\x89galit\xC3\xA9 !" : rgp[0].score > rgp[1].score ? "Joueur 1 gagne !" : "Joueur 2 gagne !";
            fun_shadow_text(F_LARGE, 320, 178, w, 1.6f, RGBA(255, 220, 90, 255));
            for (int p = 0; p < 2; p++) {
                f32 cx = p ? 470 : 170;
                int pc = rgp[p].total ? rgp[p].hits * 100 / rgp[p].total : 0;
                snprintf(t, sizeof(t), "Joueur %d", p + 1);
                gx_text_c(F_MED, cx, 222, t, p ? RGBA(255, 140, 160, 255) : RGBA(130, 190, 255, 255));
                snprintf(t, sizeof(t), "%d", rgp[p].score);
                fun_shadow_text(F_LARGE, cx, 262, t, 1.5f, RGBA(255, 255, 255, 255));
                snprintf(t, sizeof(t), "%d %%   combo %d", pc, rgp[p].best_combo);
                gx_text_c(F_SMALL, cx, 286, t, RGBA(220, 210, 250, 230));
            }
            if (rg_new_record) fun_shadow_text(F_LARGE, 320, 330, "Nouveau record !", 1.1f, RGBA(255, 220, 90, 255));
            fun_footer("A : revanche     X : autre morceau     Y : r\xC3\xA9glages     B : sortir");
            return;
        }
        snprintf(t, sizeof(t), "%d points", rgp[0].score);
        fun_shadow_text(F_LARGE, 320, 200, t, 2.2f, RGBA(255, 255, 255, 255));
        int pct = rgp[0].total ? rgp[0].hits * 100 / rgp[0].total : 0;
        snprintf(t, sizeof(t), "Notes touch\xC3\xA9" "es : %d / %d  (%d %%)     Parfait : %d     Meilleur combo : %d", rgp[0].hits, rgp[0].total, pct, rgp[0].perfect, rgp[0].best_combo);
        gx_text_c(F_MED, 320, 250, t, RGBA(230, 220, 255, 255));
        if (rg_new_record) fun_shadow_text(F_LARGE, 320, 310, "Nouveau record !", 1.3f, RGBA(255, 220, 90, 255));
        else if (rg_record > 0) { snprintf(t, sizeof(t), "Record sur ce morceau : %d", rg_record); gx_text_c(F_MED, 320, 305, t, RGBA(200, 190, 245, 255)); }
        const char *r = pct >= 90 ? "Incroyable !" : pct >= 70 ? "Tr\xC3\xA8s bien !" : pct >= 45 ? "Pas mal !" : "Entra\xC3\xAEne-toi encore !";
        gx_text_c(F_MED, 320, 350, r, RGBA(255, 200, 120, 255));
        fun_footer("A : rejouer     X : autre morceau     Y : difficult\xC3\xA9     B : sortir");
        return;
    }
    if (rg_players == 2) {                                       /* duel : deux pistes */
        if (rg_lanes == 5) { rg_highway(0, 30, 52); rg_highway(1, 350, 52); }
        else { rg_highway(0, 36, 62); rg_highway(1, 356, 62); }
        gx_rect(319, 70, 2, 380, RGBA(255, 255, 255, 40));
        for (int p = 0; p < 2; p++) {
            f32 cx = p ? 480 : 160;
            snprintf(t, sizeof(t), "J%d  %d", p + 1, rgp[p].score);
            fun_shadow_text(F_LARGE, cx, 66, t, 0.9f, p ? RGBA(255, 150, 170, 255) : RGBA(140, 200, 255, 255));
            int mult = 1 + (rgp[p].combo / 10 > 3 ? 3 : rgp[p].combo / 10);
            if (rgp[p].combo >= 5) { snprintf(t, sizeof(t), "combo %d%s", rgp[p].combo, mult > 1 ? (mult == 4 ? "  x4" : mult == 3 ? "  x3" : "  x2") : ""); gx_text_c(F_SMALL, cx, 84, t, RGBA(255, 220, 120, 230)); }
            if (rgp[p].judge[0] && now - rgp[p].judge_t < 700) {
                f32 a = 1.0f - (f32)(now - rgp[p].judge_t) / 700.0f;
                fun_shadow_text(F_LARGE, cx, 250, rgp[p].judge, 0.9f, RGBA(rgp[p].judge_c.r, rgp[p].judge_c.g, rgp[p].judge_c.b, (u8)(a * 255)));
            }
        }
    } else {
    if (rg_lanes == 5) rg_highway(0, 170, 60); else rg_highway(0, 180, 70);
    /* score */
    snprintf(t, sizeof(t), "%d", rgp[0].score);
    gx_text_c(F_SMALL, 90, 110, "SCORE", RGBA(200, 190, 245, 255));
    fun_shadow_text(F_LARGE, 90, 146, t, 1.4f, RGBA(255, 255, 255, 255));
    int mult = 1 + (rgp[0].combo / 10 > 3 ? 3 : rgp[0].combo / 10);
    snprintf(t, sizeof(t), "Combo %d", rgp[0].combo);
    gx_text_c(F_MED, 90, 190, t, RGBA(230, 220, 255, 255));
    if (mult > 1) { snprintf(t, sizeof(t), "x%d", mult); fun_shadow_text(F_LARGE, 90, 230, t, 1.3f, RGBA(255, 210, 90, 255)); }
    if (rg_record > 0) { snprintf(t, sizeof(t), "Record : %d", rg_record); gx_text_c(F_SMALL, 90, 270, t, RGBA(180, 170, 230, 220)); }
    gx_text_c(F_SMALL, 550, 110, rg_diff_names[rg_diff], RGBA(200, 190, 245, 255));
    if (rg_non >= 6) { snprintf(t, sizeof(t), "%d BPM", (int)(60000.0f / rg_period + 0.5f)); gx_text_c(F_MED, 550, 134, t, RGBA(230, 220, 255, 255)); }
    if (rgp[0].judge[0] && now - rgp[0].judge_t < 700) {
        f32 a = 1.0f - (f32)(now - rgp[0].judge_t) / 700.0f;
        fun_shadow_text(F_LARGE, 550, 240, rgp[0].judge, 1.2f, RGBA(rgp[0].judge_c.r, rgp[0].judge_c.g, rgp[0].judge_c.b, (u8)(a * 255)));
    }
    }
    if (!rg_lock) {
        u8 a = (u8)(150 + 100 * sinf((f32)(now % 1000) / 1000.0f * 6.283f));
        fun_shadow_text(F_LARGE, 320, 230, "\xC3\x89" "coute le rythme...", 1.2f, RGBA(255, 255, 255, a));
    }
    if (rg_track >= 0 && rg_track < nfiles) {
        fun_title_of(rg_track, b2, sizeof(b2));
        fit_text(F_SMALL, b2, 560, t, sizeof(t));
        gx_text_c(F_SMALL, 320, 40, t, RGBA(220, 210, 255, 220));
    }
    fun_footer(paused ? "PAUSE   -   Z : reprendre   START : arr\xC3\xAAter" : rg_players == 2 ? (rg_lanes == 5 ? "Chaque joueur : A  B  X  Y  R sur sa manette     Z : pause     START : arr\xC3\xAAter" : "Chaque joueur : A  B  X  Y sur sa manette     Z : pause     START : arr\xC3\xAAter")
                                         : rg_lanes == 5 ? "A  B  X  Y  R : jouer     Z : pause     START : arr\xC3\xAAter" : "A  B  X  Y : jouer     Z : pause     START : arr\xC3\xAAter");
}

/* ======================= Quiz musical ======================= */
#define QZ_ROUNDS 10
#define QZ_LISTEN 10000                      /* ms de musique */
#define QZ_ANSWER 15000                      /* ms pour repondre */
static int qz_phase = 0;                     /* 0 accueil, 1 ecoute, 2 reponse, 3 fin */
static int qz_round, qz_players = 1, qz_answer, qz_choice[4], qz_pick[4], qz_score[4], qz_first;
static int qz_used[QZ_ROUNDS];
static char qz_txt[4][96];
static u64 qz_t = 0, qz_pick_t[4];
/* v72 : plus de variete : question sur le titre OU sur l'artiste (si les etiquettes le disent),
   extrait de plus en plus court (10 s, puis 6 s, puis 3 s), et pas les morceaux des parties d'avant */
static int qz_kind = 0;                          /* 0 titre, 1 artiste */
#define QZ_HIST 48
static int qz_hist[QZ_HIST], qz_nhist = 0;
static u32 qz_listen_ms(void) { return qz_round < 4 ? 10000 : qz_round < 7 ? 6000 : 3000; }
static int qz_recent(int k) { for (int i = 0; i < qz_nhist && i < QZ_HIST; i++) if (qz_hist[i] == k) return 1; return 0; }
static const col_t qz_pcol[4] = { { 120, 180, 255, 255 }, { 255, 120, 140, 255 }, { 120, 230, 140, 255 }, { 255, 210, 90, 255 } };

static void quiz_start(void) {
    fun_enter(FUN_QUIZ);
    qz_phase = 0;
    int n = 0;
    for (int ch = 0; ch < 4; ch++) n += (fun_pads >> ch) & 1;
    qz_players = n < 1 ? 1 : n;
}

static void qz_round_begin(void) {
    int k = -1;
    for (int tries = 0; tries < 120 && k < 0; tries++) {
        int c = fun_random_track(-1), dup = 0;
        for (int r = 0; r < qz_round; r++) if (qz_used[r] == c) dup = 1;
        if (tries < 80 && qz_recent(c)) dup = 1;                 /* (deja tombe dans une partie d'avant) */
        if (!dup || tries > 100) k = c;
    }
    if (k < 0) { qz_phase = 3; return; }
    qz_used[qz_round] = k;
    qz_hist[qz_nhist % QZ_HIST] = k; qz_nhist++;
    qz_answer = (int)(lib_rand() % 4);
    char ta[96];
    fun_title_of(k, ta, sizeof(ta));
    /* artiste ? (seulement si 3 autres artistes differents sont connus) */
    qz_kind = 0;
    if (tg_artist[k][0] && lib_rand() % 5 < 2) {
        char other[3][48]; int no = 0;
        for (int tries = 0; tries < 200 && no < 3; tries++) {
            int x = fun_random_track(k);
            if (x < 0 || !tg_artist[x][0] || !strcasecmp(tg_artist[x], tg_artist[k])) continue;
            int dup = 0; for (int j = 0; j < no; j++) if (!strcasecmp(other[j], tg_artist[x])) dup = 1;
            if (!dup) snprintf(other[no++], sizeof(other[0]), "%s", tg_artist[x]);
        }
        if (no == 3) {
            qz_kind = 1;
            for (int i = 0, j = 0; i < 4; i++) {
                if (i == qz_answer) { qz_choice[i] = k; snprintf(qz_txt[i], sizeof(qz_txt[i]), "%s", tg_artist[k]); }
                else { qz_choice[i] = -1; snprintf(qz_txt[i], sizeof(qz_txt[i]), "%s", other[j++]); }
            }
        }
    }
    for (int i = 0; i < 4 && !qz_kind; i++) {
        if (i == qz_answer) { qz_choice[i] = k; snprintf(qz_txt[i], sizeof(qz_txt[i]), "%s", ta); continue; }
        int c = -1;
        for (int tries = 0; tries < 60 && c < 0; tries++) {
            int x = fun_random_track(k), bad = x == k;
            char tx[96];
            if (x >= 0) { fun_title_of(x, tx, sizeof(tx)); if (!strcasecmp(tx, ta)) bad = 1; }
            for (int j = 0; j < i; j++) if (j != qz_answer && qz_choice[j] == x) bad = 1;
            if (!bad) { c = x; snprintf(qz_txt[i], sizeof(qz_txt[i]), "%s", tx); }
        }
        if (c < 0) { c = k; snprintf(qz_txt[i], sizeof(qz_txt[i]), "?"); }
        qz_choice[i] = c;
    }
    for (int p = 0; p < 4; p++) qz_pick[p] = -1;
    qz_first = -1;
    queue_build(k);
    if (!start_track(k) || !playing) {                     /* morceau illisible : un autre */
        static int retry = 0;
        if (++retry < 6) { qz_round_begin(); retry = 0; return; }
        retry = 0;
    }
    int d = file_duration(names[k]);
    if (d > 40) track_seek(d * (25 + (int)(lib_rand() % 20)) / 100);   /* un passage au milieu */
    qz_t = now_ms();
    qz_phase = 1;
}

static void qz_reveal(void) {
    qz_phase = 2;
    qz_t = now_ms();
    for (int p = 0; p < qz_players; p++) if (qz_pick[p] == qz_answer) rumble(p, p == qz_first ? 220 : 120);
}

static void quiz_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    if (qz_phase == 1) {
        if (now - qz_t > qz_listen_ms() && src == SRC_FILE && playing) stop_track();
        int all = 1;
        for (int p = 0; p < qz_players; p++) if (qz_pick[p] < 0) all = 0;
        if (all || now - qz_t > QZ_ANSWER) qz_reveal();
    } else if (qz_phase == 2 && now - qz_t > 4000) {
        qz_round++;
        if (qz_round >= QZ_ROUNDS) {
            qz_phase = 3;
            stop_track();
            int best = 0;
            for (int p = 1; p < qz_players; p++) if (qz_score[p] > qz_score[best]) best = p;
            rumble(best, 400);
        } else qz_round_begin();
    }
}

static void qz_answer_by(int p, int c) {
    if (p >= qz_players || qz_pick[p] >= 0) return;
    qz_pick[p] = c;
    qz_pick_t[p] = now_ms();
    if (c == qz_answer) {
        u64 el = now_ms() - qz_t;
        int bonus = el < QZ_ANSWER ? (int)((QZ_ANSWER - el) * 100 / QZ_ANSWER) : 0;
        qz_score[p] += 50 + bonus;
        if (qz_first < 0) { qz_first = p; if (qz_players > 1) qz_score[p] += 25; }
    }
    if (qz_players == 1) qz_reveal();
}

static void quiz_input(u32 down0, u32 nav, int fire) {
    if (qz_phase == 0 || qz_phase == 3) {
        if (qz_phase == 0 && fire && (nav & PAD_BUTTON_LEFT) && qz_players > 1) qz_players--;
        if (qz_phase == 0 && fire && (nav & PAD_BUTTON_RIGHT) && qz_players < 4) qz_players++;
        if (down0 & PAD_BUTTON_A) {
            if (fun_tracks() < 4) { lib_note("Il faut au moins 4 morceaux"); fun_leave(); return; }
            memset(qz_score, 0, sizeof(qz_score));
            qz_round = 0;
            qz_round_begin();
        }
        if (down0 & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave();
        return;
    }
    if (down0 & PAD_BUTTON_START) { fun_leave(); return; }
    if (qz_phase != 1) return;
    static const u32 btn[4] = { PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X, PAD_BUTTON_Y };
    for (int p = 0; p < qz_players; p++) {
        u32 d = p == 0 ? down0 : PAD_ButtonsDown(p);
        for (int c = 0; c < 4; c++) if (d & btn[c]) { qz_answer_by(p, c); break; }
    }
}

static void draw_quiz(void) {
    u64 now = now_ms();
    char t[200];
    fun_frame_begin(200 + (f32)(now % 60000) / 60000.0f * 360.0f, 1.0f + pt_beat * 0.6f);
    if (qz_phase == 0) {
        fun_shadow_text(F_LARGE, 320, 100, "Quiz musical", 1.7f, RGBA(255, 230, 140, 255));
        gx_text_c(F_MED, 320, 142, "10 secondes d'un de tes morceaux : trouve lequel !", RGBA(230, 220, 255, 255));
        gx_text_c(F_MED, 320, 164, "Chaque joueur r\xC3\xA9pond avec A, B, X ou Y sur sa manette.", RGBA(230, 220, 255, 255));
        snprintf(t, sizeof(t), "Joueurs :   <  %d  >", qz_players);
        fun_shadow_text(F_LARGE, 320, 240, t, 1.2f, RGBA(255, 255, 255, 255));
        for (int p = 0; p < qz_players; p++) {
            f32 cx = 320 + (p - (qz_players - 1) / 2.0f) * 90;
            gx_circle(cx, 290, 16, qz_pcol[p], RGBA(qz_pcol[p].r / 2, qz_pcol[p].g / 2, qz_pcol[p].b / 2, 255));
            snprintf(t, sizeof(t), "J%d", p + 1);
            gx_text_c(F_SMALL, cx, 295, t, RGBA(255, 255, 255, 255));
        }
        gx_text_c(F_SMALL, 320, 336, "(manette 1 dans le port 1, manette 2 dans le port 2...)", RGBA(180, 170, 230, 220));
        snprintf(t, sizeof(t), "%d manches   -   le plus rapide gagne plus de points", QZ_ROUNDS);
        gx_text_c(F_SMALL, 320, 360, t, RGBA(180, 170, 230, 220));
        fun_footer("A : commencer     B : sortir");
        return;
    }
    if (qz_phase == 3) {
        fun_shadow_text(F_LARGE, 320, 80, "Fin du quiz !", 1.7f, RGBA(255, 230, 140, 255));
        int order[4] = { 0, 1, 2, 3 };
        for (int i = 0; i < qz_players; i++)
            for (int j = i + 1; j < qz_players; j++) if (qz_score[order[j]] > qz_score[order[i]]) { int x = order[i]; order[i] = order[j]; order[j] = x; }
        for (int i = 0; i < qz_players; i++) {
            int p = order[i];
            f32 y = 150 + i * 62;
            if (qz_players == 1) snprintf(t, sizeof(t), "%d points", qz_score[p]);
            else snprintf(t, sizeof(t), "%d.  Joueur %d  :  %d points", i + 1, p + 1, qz_score[p]);
            fun_shadow_text(F_LARGE, 320, y, t, i == 0 ? 1.4f : 1.1f, i == 0 ? RGBA(255, 220, 90, 255) : qz_pcol[p]);
        }
        if (qz_players > 1) { snprintf(t, sizeof(t), "Bravo Joueur %d !", order[0] + 1); gx_text_c(F_MED, 320, 410, t, RGBA(255, 255, 255, 230)); }
        fun_footer("A : rejouer     B : sortir");
        return;
    }
    snprintf(t, sizeof(t), TR(qz_kind ? "Manche %d / %d   -   Qui chante ?   (extrait de %d s)" : "Manche %d / %d   -   Quel morceau ?   (extrait de %d s)"), qz_round + 1, QZ_ROUNDS, (int)(qz_listen_ms() / 1000));
    gx_text_c(F_MED, 320, 34, t, RGBA(230, 220, 255, 255));
    /* minuteur rond, avec le spectre */
    f32 cx = 320, cy = 118, el = (f32)(now - qz_t);
    f32 left = qz_phase == 1 ? 1.0f - el / QZ_ANSWER : 0.0f;
    if (left < 0) left = 0;
    gx_circle(cx, cy, 52 + pt_bass * 10, hsv(pt_hue, 0.6f, 0.35f, 255), hsv(pt_hue + 40, 0.8f, 0.12f, 255));
    int segs = (int)(left * 40 + 0.5f);
    for (int i = 0; i < segs; i++) {
        float a0 = -PI_F / 2 + i * 2 * PI_F / 40, a1 = a0 + 2 * PI_F / 40 * 0.8f;
        gx_poly4(cx + cosf(a0) * 58, cy + sinf(a0) * 58, cx + cosf(a1) * 58, cy + sinf(a1) * 58,
                 cx + cosf(a1) * 64, cy + sinf(a1) * 64, cx + cosf(a0) * 64, cy + sinf(a0) * 64,
                 RGBA(255, 220, 120, 255), RGBA(255, 220, 120, 255), RGBA(255, 160, 60, 255), RGBA(255, 160, 60, 255));
    }
    if (qz_phase == 1) { snprintf(t, sizeof(t), "%d", (int)((QZ_ANSWER - el) / 1000 + 0.99f)); fun_shadow_text(F_LARGE, cx, cy + 14, t, 1.5f, RGBA(255, 255, 255, 255)); }
    else fun_shadow_text(F_LARGE, cx, cy + 12, qz_pick[0] == qz_answer && qz_players == 1 ? "Bravo !" : "R\xC3\xA9ponse", 0.9f, RGBA(255, 255, 255, 255));
    /* les 4 reponses (comme les boutons) */
    static const int pos[4][2] = { { 1, 1 }, { 0, 1 }, { 1, 0 }, { 0, 0 } };   /* A bas droite, B bas gauche, X haut droite, Y haut gauche */
    for (int c = 0; c < 4; c++) {
        f32 x = 34 + pos[c][0] * 292, y = 200 + pos[c][1] * 98, w = 280, h = 86;
        col_t ct = RGBA(30, 20, 70, 230), cb = RGBA(16, 10, 40, 230);
        if (qz_phase == 2 && c == qz_answer) { ct = RGBA(40, 170, 80, 255); cb = RGBA(20, 110, 50, 255); }
        else if (qz_phase == 2) { int wrong = 0; for (int p = 0; p < qz_players; p++) if (qz_pick[p] == c) wrong = 1; if (wrong) { ct = RGBA(170, 40, 60, 255); cb = RGBA(110, 20, 40, 255); } }
        gx_rrect_fill(x, y, w, h, 12, ct, cb);
        gx_rrect_stroke(x, y, w, h, 12, 1.5f, RGBA(lane_top[c].r, lane_top[c].g, lane_top[c].b, 200));
        gx_circle(x + 28, y + h / 2, 17, lane_top[c], lane_bot[c]);
        gx_text_c(F_MED, x + 28, y + h / 2 + 6, lane_btn[c], RGBA(255, 255, 255, 255));
        char a[96], b[96];
        wrap2(F_MED, qz_txt[c], (int)w - 66, a, b, sizeof(a));
        f32 ty = b[0] ? y + h / 2 - 3 : y + h / 2 + 6;
        gx_text1(F_MED, x + 54, ty, a, RGBA(255, 255, 255, 255));
        if (b[0]) { fit_text(F_MED, b, (int)w - 66, a, sizeof(a)); gx_text1(F_MED, x + 54, ty + 20, a, RGBA(255, 255, 255, 255)); }
        for (int p = 0; p < qz_players; p++)                      /* qui a choisi quoi (apres la reponse) */
            if (qz_phase == 2 && qz_pick[p] == c && qz_players > 1) gx_circle(x + w - 16 - p * 18, y + 14, 7, qz_pcol[p], qz_pcol[p]);
    }
    /* les joueurs */
    for (int p = 0; p < qz_players; p++) {
        f32 x = 320 + (p - (qz_players - 1) / 2.0f) * 150;
        if (qz_players == 1) snprintf(t, sizeof(t), "Score : %d", qz_score[p]);
        else snprintf(t, sizeof(t), "J%d : %d%s", p + 1, qz_score[p], qz_phase == 1 && qz_pick[p] >= 0 ? "  (ok)" : "");
        gx_text_c(F_MED, x, 422, t, qz_pcol[p]);
    }
    if (qz_phase == 2 && qz_first >= 0 && qz_players > 1) { snprintf(t, sizeof(t), "Joueur %d a trouv\xC3\xA9 le premier !", qz_first + 1); gx_text_c(F_SMALL, 320, 446, t, RGBA(255, 230, 140, 255)); }
    fun_footer(qz_phase == 1 ? (src == SRC_FILE && playing ? "\xC3\x89" "coute bien...     START : arr\xC3\xAAter" : "R\xC3\xA9ponds vite !     START : arr\xC3\xAAter") : "START : arr\xC3\xAAter");
}

/* ======================= Reveil ======================= */
static void wake_start(void) {
    night_on = 0;                                    /* (la veilleuse s'eteint au reveil) */
    fun_enter(FUN_WAKE);
}

static void draw_wake(void) {
    u64 now = now_ms();
    f32 p = (f32)(now - fun_t0) / 180000.0f;
    if (p > 1) p = 1;
    scr_viewport();
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    /* lever du soleil */
    col_t top = RGBA((u8)(8 + 40 * p), (u8)(10 + 60 * p), (u8)(40 + 100 * p), 255);
    col_t bot = RGBA((u8)(40 + 200 * p), (u8)(16 + 110 * p), (u8)(50 + 10 * p), 255);
    gx_quad4(0, 0, 640, 480, top, top, bot, bot);
    f32 sy = 560 - 150 * p;
    gx_circle_glow(320, sy, 120, RGBA(255, 190, 90, (u8)(60 + 80 * p)), 8);
    gx_circle(320, sy, 110, RGBA(255, 220, 120, (u8)(120 + 100 * p)), RGBA(255, 140, 60, (u8)(80 + 80 * p)));

    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    char t[200];
    fun_shadow_text(F_LARGE, 320, 46, "Bonjour !", 1.3f, RGBA(255, 240, 200, 255));
    if (tm) {
        char hm[8];
        snprintf(hm, sizeof(hm), "%02d:%02d", tm->tm_hour, tm->tm_min);
        const f32 dw = 58, dh = 100, gap = 14, colon = 30, y = 66;
        f32 tot = 4 * dw + 2 * gap + colon + gap, x = 320 - tot / 2;
        col_t ct = RGBA(255, 255, 255, 255), cb = RGBA(255, 220, 170, 255);
        seg_digit(x, y, dw, dh, hm[0] - '0', ct, cb);
        seg_digit(x + dw + gap, y, dw, dh, hm[1] - '0', ct, cb);
        f32 kx = x + 2 * dw + gap + (colon + gap) / 2 - 6;
        gx_rrect_fill(kx, y + dh * 0.30f, 12, 12, 4, ct, cb);
        gx_rrect_fill(kx, y + dh * 0.62f, 12, 12, 4, ct, cb);
        seg_digit(x + 2 * dw + 2 * gap + colon, y, dw, dh, hm[3] - '0', ct, cb);
        seg_digit(x + 3 * dw + 3 * gap + colon, y, dw, dh, hm[4] - '0', ct, cb);
        static const char *const jours[7] = { "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi" };
        static const char *const mois[12] = { "janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin", "juillet",
                                              "ao\xC3\xBBt", "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre" };
        snprintf(t, sizeof(t), "%s %d %s", jours[tm->tm_wday % 7], tm->tm_mday, mois[tm->tm_mon % 12]);
        fun_shadow_text(F_LARGE, 320, 200, t, 1.1f, RGBA(255, 255, 255, 240));
    }
    /* meteo */
    gx_rrect_fill(110, 222, 420, 128, 16, RGBA(0, 0, 0, 70), RGBA(0, 0, 0, 100));
    if (meteo_ok) {
        meteo_icon(200, 286, 96, meteo_code);
        snprintf(t, sizeof(t), "%d\xC2\xB0", (int)floorf(meteo_temp + 0.5f));
        txt_scale = 2.4f;
        gx_text(F_LARGE, 268, 300, t, RGBA(255, 255, 255, 255), RGBA(255, 225, 190, 255), 0);
        txt_scale = 1.0f;
        gx_text1(F_LARGE, 392, 270, meteo_text(meteo_code), RGBA(255, 255, 255, 255));
        snprintf(t, sizeof(t), "%s", city);
        char ct2[100];
        fit_text(F_MED, t, 130, ct2, sizeof(ct2));
        gx_text1(F_MED, 392, 294, ct2, RGBA(230, 220, 255, 255));
        snprintf(t, sizeof(t), "Vent %d km/h   Humidit\xC3\xA9 %d %%", meteo_wind, meteo_hum);
        gx_text1(F_SMALL, 392, 318, t, RGBA(220, 210, 240, 230));
    } else if (!city[0]) {
        gx_text_c(F_MED, 320, 280, "M\xC3\xA9t\xC3\xA9o : choisis ta ville dans l'onglet INFOS", RGBA(255, 255, 255, 230));
    } else {
        gx_text_c(F_MED, 320, 280, "M\xC3\xA9t\xC3\xA9o en cours de chargement...", RGBA(255, 255, 255, 230));
    }
    /* musique */
    char ti[200], sub[200];
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    if (ti[0]) { fit_text(F_MED, ti, 520, t, sizeof(t)); gx_text_c(F_MED, 320, 392, t, RGBA(255, 255, 255, 240)); }
    if (alarm_fade < 256) {
        gx_rrect_fill(250, 408, 140, 4, 2, RGBA(255, 255, 255, 50), RGBA(255, 255, 255, 50));
        gx_rrect_fill(250, 408, 140.0f * alarm_fade / 256, 4, 2, RGBA(255, 230, 160, 230), RGBA(255, 230, 160, 230));
        gx_text_c(F_SMALL, 320, 428, "le son monte doucement", RGBA(255, 240, 220, 200));
    }
    fun_footer("A : garder     X : +10 min     B : arr\xC3\xAAter     Y : pause");
}

static void wake_input(u32 down) {
    if (down & PAD_BUTTON_X) {                            /* v76 : report (+10 min) */
        stop_track(); stop_station(); alarm_t0 = 0; alarm_fade = 256;
        alarm_snooze_at = now_ms() + 10 * 60000;
        fun_mode = FUN_OFF; return;
    }
    if (down & (PAD_BUTTON_A | PAD_BUTTON_START)) { fun_mode = FUN_OFF; return; }
    if (down & PAD_BUTTON_B) { stop_track(); stop_station(); alarm_t0 = 0; alarm_fade = 256; alarm_snooze_at = 0; fun_mode = FUN_OFF; return; }
    if (down & PAD_BUTTON_Y) {
        if (src == SRC_FILE && playing) paused = !paused;
        else if (src == SRC_RADIO) muted = !muted;
    }
}

/* meteo prete pour le reveil : chargee 5 minutes avant */
static void wake_prefetch(void) {
    static int done_day = -1;
    if (!alarm_on || !city[0]) return;
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    if (!tm || tm->tm_year < 100) return;
    int m = tm->tm_hour * 60 + tm->tm_min, until = (alarm_min - m + 1440) % 1440;
    if (until > 5 || until == 0 || done_day == tm->tm_yday) return;
    done_day = tm->tm_yday;
    net_start_async();
    if (!meteo_t || now_ms() - meteo_t > 20 * 60000) meteo_t = 0;
}

/* ======================= Titres de la radio ======================= */
#define TITLES_FILE GCD "/gc-mp3-titres.txt"
static void radio_title_keep(void) {
    if (src != SRC_RADIO || cur_station < 0 || state == ST_IDLE || pod_on) { lib_note("\xC3\x89" "coute une radio d'abord"); return; }
    if (!now_title[0]) { lib_note("Cette radio n'annonce pas ses titres"); return; }
    static char last[160];
    if (!strcmp(last, now_title)) { lib_note("Titre d\xC3\xA9j\xC3\xA0 gard\xC3\xA9"); return; }
    if (!sd_ok) { lib_note("Pas de carte SD"); return; }
    FILE *f = fopen(TITLES_FILE, "a");
    if (!f) { lib_note("Impossible d'\xC3\xA9" "crire sur la carte SD"); return; }
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    char name[100], ti[200];                                /* (en UTF-8, lisible aussi sur le telephone) */
    text_display(stations[cur_station].name, name, sizeof(name));
    text_display(now_title, ti, sizeof(ti));
    for (char *p = name; *p; p++) if (*p == '|') *p = '/';
    if (tm && tm->tm_year >= 100) fprintf(f, "%02d/%02d/%04d %02d:%02d | %s | %s\n", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, tm->tm_hour, tm->tm_min, name, ti);
    else fprintf(f, "- | %s | %s\n", name, ti);
    fclose(f);
    snprintf(last, sizeof(last), "%s", now_title);
    char d[200], m[220];
    text_display(now_title, d, sizeof(d));
    snprintf(m, sizeof(m), "Titre gard\xC3\xA9 : %.150s", d);
    lib_note(m);
    rumble(0, 80);
}

/* liste des titres gardes (les plus recents d'abord) */
static void titles_open(void) {
    am_n = 0;
    am_sel = 0;
    am_track = -1;
    snprintf(am_title, sizeof(am_title), "Titres radio gard\xC3\xA9s  (A : chercher dans ma musique)");
    FILE *f = sd_ok ? fopen(TITLES_FILE, "r") : NULL;
    static char lines[PL_MAX][200];
    int n = 0;
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *t = trim(line);
            char *b1 = strchr(t, '|'), *b2 = b1 ? strchr(b1 + 1, '|') : NULL;
            if (!b2) continue;
            char *title = trim(b2 + 1);
            *b2 = 0;
            char *st = trim(b1 + 1);
            char d[160], ds[64];
            text_display(title, d, sizeof(d));
            text_display(st, ds, sizeof(ds));
            if (n == PL_MAX) { memmove(lines[0], lines[1], sizeof(lines[0]) * (PL_MAX - 1)); n--; }
            snprintf(lines[n++], sizeof(lines[0]), "%.120s  (%.40s)", d, ds);
        }
        fclose(f);
    }
    for (int i = n - 1; i >= 0 && am_n < PL_MAX; i--) am_item(lines[i], AC_TITLE, i);
    if (n == 0) am_item("(aucun : X dans l'onglet RADIO pendant une chanson)", AC_CLOSE, 0);
    else am_item("Effacer la liste", AC_TITLES_CLEAR, 0);
    am_item("Fermer", AC_CLOSE, 0);
    am_open = 1;
}

static void titles_search(const char *item) {          /* "Artiste - Titre  (radio)" -> recherche */
    char q[200];
    snprintf(q, sizeof(q), "%s", item);
    char *p = strstr(q, "  (");
    if (p) *p = 0;
    char *dash = strstr(q, " - ");
    const char *what = dash ? dash + 3 : q;
    tab = TAB_MUSIC;
    if (view_mode == VIEW_LISTS || view_mode == VIEW_FOLDERS) set_view(VIEW_ALL);
    lib_find_set(what);
}

/* ======================= Podcasts automatiques ======================= */
#define POD_AUTO_FILE GCD "/podcasts/auto.txt"
#define PA_Q 6
static hfetch_t paf = { .c = { -1, 0, 0 } };
static int pa_state = 0, pa_i = 0, pa_nq = 0;
static u64 pa_next = 0;
static char pa_q_url[PA_Q][URL_MAX], pa_q_title[PA_Q][160];

static int pa_seen_get(const char *feed, char *out, int cap) {
    FILE *f = fopen(POD_AUTO_FILE, "r");
    if (!f) return 0;
    char *line = (char *)malloc(2 * URL_MAX + 16);
    int found = 0;
    if (line) {
        while (fgets(line, 2 * URL_MAX + 16, f)) {
            char *bar = strstr(line, " | ");
            if (!bar) continue;
            *bar = 0;
            if (!strcmp(line, feed)) { snprintf(out, cap, "%s", trim(bar + 3)); found = 1; }
        }
        free(line);
    }
    fclose(f);
    return found;
}

static void pa_seen_set(const char *feed, const char *ep) {
    int cap = 256 * 1024, len = 0;
    char *buf = (char *)malloc(cap), *line = (char *)malloc(2 * URL_MAX + 16);
    if (!buf || !line) { free(buf); free(line); return; }
    FILE *f = fopen(POD_AUTO_FILE, "r");
    if (f) {
        while (fgets(line, 2 * URL_MAX + 16, f)) {
            char *bar = strstr(line, " | ");
            if (!bar) continue;
            int same = (int)(bar - line) == (int)strlen(feed) && !strncmp(line, feed, bar - line);
            int n = (int)strlen(line);
            if (!same && len + n + 2 < cap - 2 * URL_MAX) { memcpy(buf + len, line, n); len += n; if (line[n - 1] != '\n') buf[len++] = '\n'; }
        }
        fclose(f);
    }
    len += snprintf(buf + len, cap - len, "%s | %s\n", feed, ep);
    mkdir(GCD "/podcasts", 0777);
    f = fopen(POD_AUTO_FILE, "w");
    if (f) { fwrite(buf, 1, len, f); fclose(f); }
    free(buf);
    free(line);
}

static const char *pa_find(const unsigned char *b, int n, const char *s) {
    int sl = (int)strlen(s);
    for (int i = 0; i + sl <= n; i++) if (b[i] == (unsigned char)s[0] && !memcmp(b + i, s, sl)) return (const char *)b + i;
    return NULL;
}

/* le premier episode du flux (le plus recent) */
static int pa_first_item(const unsigned char *x, int len, char *title, int tcap, char *url, int ucap) {
    const char *p = pa_find(x, len, "<item");
    if (!p) return 0;
    const char *e = pa_find((const unsigned char *)p, len - (int)(p - (const char *)x), "</item>");
    if (!e) return 0;
    title[0] = url[0] = 0;
    xml_tag(p, e, "title", title, tcap);
    xml_attr(p, e, "enclosure", "url", url, ucap);
    return url[0] && !strncasecmp(url, "http", 4);
}

static void podauto_done(const unsigned char *x, int len) {
    char title[160], url[URL_MAX], seen[URL_MAX];
    if (pa_i < npods && pa_first_item(x, len, title, sizeof(title), url, sizeof(url))) {
        if (!pa_seen_get(pods[pa_i].url, seen, sizeof(seen))) pa_seen_set(pods[pa_i].url, url);   /* 1re fois : on note seulement */
        else if (strcmp(seen, url)) {
            pa_seen_set(pods[pa_i].url, url);
            if (pa_nq < PA_Q) {
                snprintf(pa_q_url[pa_nq], URL_MAX, "%s", url);
                snprintf(pa_q_title[pa_nq], 160, "%s", title[0] ? title : pods[pa_i].name);
                pa_nq++;
            }
        }
    }
    pa_i++;
}

static void podauto_kick(void) { pa_next = now_ms() + 5000; pa_i = 0; }

static void podauto_pump(void) {
    if (net_phase != NET_DONE || !net_ok || !sd_ok) return;
    u64 now = now_ms();
    if (pa_nq > 0 && !dl_active && tab != TAB_WEB && !wb_busy && fun_mode == FUN_OFF) {      /* un episode a telecharger */
        char m[220], d[160];
        snprintf(dl_name_hint, sizeof(dl_name_hint), "%s", pa_q_title[0]);
        snprintf(dl_dest_dir, sizeof(dl_dest_dir), GCD "/podcasts");
        mkdir(dl_dest_dir, 0777);
        text_display(pa_q_title[0], d, sizeof(d));
        snprintf(m, sizeof(m), "Nouvel \xC3\xA9pisode : %.120s", d);
        lib_note(m);
        char url[URL_MAX];
        snprintf(url, sizeof(url), "%s", pa_q_url[0]);
        pa_nq--;
        memmove(pa_q_url[0], pa_q_url[1], sizeof(pa_q_url[0]) * pa_nq);
        memmove(pa_q_title[0], pa_q_title[1], sizeof(pa_q_title[0]) * pa_nq);
        dl_force = 1;
        snprintf(dl_ep_url, sizeof(dl_ep_url), "%s", url);
        web_fetch(url, NAV_PUSH, 0);
        return;
    }
    if (!pod_auto) { if (pa_state) { hf_abort(&paf); hf_drop(&paf); pa_state = 0; } return; }
    if (pa_state == 0) {
        if (!pa_next) { pa_next = now + 45000; return; }    /* un peu apres le demarrage */
        if (now < pa_next || pf_state != PF_IDLE || fun_mode != FUN_OFF) return;
        if (!pods_loaded) pods_load();
        while (pa_i < npods && !pods[pa_i].url[0]) pa_i++;
        if (pa_i >= npods) { pa_i = 0; pa_next = now + 3ULL * 3600 * 1000; return; }   /* toutes les 3 heures */
        paf.image = 0;
        paf.maxbytes = 6 * 1024 * 1024;
        paf.accept = "application/rss+xml,application/xml,text/xml,*/*";
        hf_start(&paf, pods[pa_i].url, NULL);
        pa_state = 1;
        return;
    }
    hf_pump(&paf);
    if (paf.st == HF_FAIL) { hf_drop(&paf); pa_state = 0; pa_i++; return; }
    if (paf.st == HF_BODY && paf.buf && paf.len > 64 && pa_find(paf.buf, paf.len, "</item>")) {   /* le 1er episode suffit */
        podauto_done(paf.buf, paf.len);
        hf_abort(&paf);
        hf_drop(&paf);
        pa_state = 0;
        return;
    }
    if (paf.st != HF_DONE) return;
    paf.st = HF_IDLE;
    if (paf.buf) podauto_done(paf.buf, paf.len); else pa_i++;
    hf_drop(&paf);
    pa_state = 0;
}

/* ======================= Fond d'ecran ======================= */
#define DJ_OWNER_WALL 5
static u16 *wp_tex = NULL;
static GXTexObj wp_obj;
static int wp_tw, wp_th, wp_iw, wp_ih;
static int wp_state = 0;                     /* 0 rien, 1 a lire, 2 lecture, 3 attend le decodeur, 4 decodage, 5 pret, 6 illisible */
static FILE *wp_fp = NULL;
static unsigned char *wp_buf = NULL;
static long wp_len = 0, wp_size = 0;
static int wp_again = 0;                     /* (photo changee pendant un decodage) */

static void wall_reload(void) {
    if (wp_fp) { fclose(wp_fp); wp_fp = NULL; }
    if (wp_state != 4) { free(wp_buf); wp_buf = NULL; }
    if (wp_state == 4) { wp_again = 1; return; }             /* (le decodage en cours finira d'abord) */
    wp_state = wall_on && wall_path[0] ? 1 : 0;
}

static void wall_set(const char *path) {
    snprintf(wall_path, sizeof(wall_path), "%s", path);
    wall_on = 1;
    settings_dirty = 1;
    wall_reload();
    lib_note("Fond d'\xC3\xA9" "cran chang\xC3\xA9 (START > R\xC3\xA9glages pour le retirer)");
}

static void wall_pump(void) {
    if (wp_state == 1) {
        if (!sd_ok) { wp_state = 6; return; }
        wp_fp = fopen(wall_path, "rb");
        long size = -1;
        if (wp_fp && fseek(wp_fp, 0, SEEK_END) == 0) size = ftell(wp_fp);
        if (!wp_fp || size <= 0 || size > PHOTO_MAX_FILE || fseek(wp_fp, 0, SEEK_SET) != 0 || !(wp_buf = (unsigned char *)malloc(size))) {
            if (wp_fp) fclose(wp_fp);
            wp_fp = NULL;
            wp_state = 6;
            return;
        }
        wp_size = size;
        wp_len = 0;
        wp_state = 2;
        return;
    }
    if (wp_state == 2) {
        long want = wp_size - wp_len;
        if (want > 65536) want = 65536;
        size_t got = fread(wp_buf + wp_len, 1, want, wp_fp);
        if (got == 0) { fclose(wp_fp); wp_fp = NULL; free(wp_buf); wp_buf = NULL; wp_state = 6; return; }
        wp_len += (long)got;
        if (wp_len < wp_size) return;
        fclose(wp_fp);
        wp_fp = NULL;
        int iw, ih, ic;
        int ok = is_webp(wp_buf, (int)wp_len) ? WebPGetInfo(wp_buf, (size_t)wp_len, &iw, &ih)
                                               : stbi_info_from_memory(wp_buf, (int)wp_len, &iw, &ih, &ic);
        if (!ok || (long)iw * ih > PHOTO_MAX_PIXELS) { free(wp_buf); wp_buf = NULL; wp_state = 6; return; }
        wp_state = 3;
        return;
    }
    if (wp_state == 3) {
        if (dj_state != 0 || il_state == IL_DECODE || !dec_ensure()) return;
        dj_in = wp_buf;
        dj_len = (int)wp_len;
        wp_buf = NULL;
        dj_aw = 640;
        dj_ah = 480;
        dj_maxw = 640;
        dj_photo = 1;
        dj_owner = DJ_OWNER_WALL;
        dj_state = 1;
        wp_state = 4;
        return;
    }
    if (wp_state == 4 && dj_state == 2 && dj_owner == DJ_OWNER_WALL) {
        dj_state = 0;
        dj_owner = 0;
        if (!dj_ok || !dj_tex) { free(dj_tex); dj_tex = NULL; wp_state = 6; return; }
        DCFlushRange(dj_tex, (u32)dj_tw * dj_th * 2);
        free(wp_tex);
        wp_tex = dj_tex;
        dj_tex = NULL;
        wp_tw = dj_tw; wp_th = dj_th; wp_iw = dj_iw; wp_ih = dj_ih;
        GX_InitTexObj(&wp_obj, wp_tex, wp_tw, wp_th, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&wp_obj, GX_LINEAR, GX_LINEAR);
        GX_InvalidateTexAll();
        wp_state = 5;
        if (wp_again) { wp_again = 0; wp_state = wall_on && wall_path[0] ? 1 : 0; }
    }
}

static int wall_active(void) { return wall_on && wp_state == 5 && wp_tex; }

/* le fond : la photo sur tout l'ecran (recadree), assombrie pour lire le texte */
static int wall_draw(void) {
    if (!wall_on || wp_state != 5 || !wp_tex || wp_iw <= 0 || wp_ih <= 0) return 0;
    f32 T = 640.0f * ux_k / 480.0f, I = (f32)wp_iw / wp_ih;
    f32 u0 = 0, u1 = 1, v0 = 0, v1 = 1;
    if (I > T) { f32 k = T / I; u0 = (1 - k) / 2; u1 = u0 + k; }
    else { f32 k = I / T; v0 = (1 - k) / 2; v1 = v0 + k; }
    f32 su = (f32)wp_iw / wp_tw, sv = (f32)wp_ih / wp_th;
    gx_set_image(&wp_obj);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(UX(0), 0);     GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(u0 * su, v0 * sv);
    GX_Position2f32(UX(640), 0);   GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(u1 * su, v0 * sv);
    GX_Position2f32(UX(640), 480); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(u1 * su, v1 * sv);
    GX_Position2f32(UX(0), 480);   GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(u0 * su, v1 * sv);
    GX_End();
    gx_quad4(0, 0, 640, 480, RGBA(6, 4, 24, 115), RGBA(6, 4, 24, 115), RGBA(6, 4, 24, 160), RGBA(6, 4, 24, 160));
    return 1;
}


/* ======================= Mini-jeux musicaux (v20) =======================
   Snake et casse-briques : ils vont plus vite quand la musique est forte,
   et tout clignote sur les coups de basse. La musique continue en fond. */
#define AR_FILE_SNAKE  (sn_wrap ? "jeu:snake-sans-murs" : "jeu:snake")
#define AR_FILE_BRICKS "jeu:casse-briques"
static int ar_phase = 0;                     /* 0 partie, 1 perdu */
static int ar_score = 0, ar_record = 0, ar_new_record = 0;
static u64 ar_last = 0, ar_beat_seen = 0;
static f32 ar_flash = 0;                     /* eclair sur le coup de basse */

static void ar_music(void) {                 /* de la musique pour jouer */
    if ((src == SRC_FILE && playing) || (src == SRC_RADIO && state != ST_IDLE)) return;
    int k = fun_random_track(-1);
    if (k >= 0) { queue_build(k); start_track(k); }
}
static int ar_beat(void) {                   /* un coup de basse depuis la derniere fois ? */
    if (ba_ok()) { ba_pump(); return ba_beat_now(); }          /* v72 : musique : le coup exact, quand on l'entend */
    if (pt_last_beat != ar_beat_seen && now_ms() - pt_last_beat < 120) { ar_beat_seen = pt_last_beat; return 1; }
    return 0;
}
static f32 ar_avg = 0.2f;
static u64 ar_avg_t = 0;
/* intensite de la musique, 0 a 1 : par rapport au reste du morceau (le refrain,
   le moment ou ca part, accelere le jeu), plus un petit coup a chaque battement */
static f32 ar_drive(void) {
    if (paused || !((src == SRC_FILE && playing) || src == SRC_RADIO)) return 0;
    u64 n = now_ms();
    if (n != ar_avg_t) {
        f32 dt = ar_avg_t ? (f32)(n - ar_avg_t) / 1000.0f : 0.016f;
        if (dt > 0.1f) dt = 0.1f;
        ar_avg += (pt_energy - ar_avg) * dt / 8.0f;              /* moyenne sur ~8 s */
        ar_avg_t = n;
    }
    f32 e = 0.35f + (pt_energy - ar_avg) * 5.0f + pt_beat * 0.15f;
    return e < 0 ? 0 : e > 1 ? 1 : e;
}
static void ar_game_over(const char *key) {
    ar_phase = 1;
    ar_new_record = 0;
    if (ar_score > ar_record) { ar_new_record = ar_score > 0; ar_record = ar_score; if (sd_ok && ar_score > 0) rg_record_set(key, ar_score); }
    rumble(0, 350);
}
static void ar_hud(const char *title, const char *extra) {
    char t[160];
    gx_text(F_SMALL, 24, 30, title, RGBA(255, 210, 110, 255), RGBA(255, 160, 60, 255), 3);
    snprintf(t, sizeof(t), "Score %d", ar_score);
    gx_text1(F_MED, 24, 56, t, RGBA(255, 255, 255, 255));
    snprintf(t, sizeof(t), "Record %d", ar_record);
    gx_text_r(F_MED, 616, 56, t, RGBA(220, 210, 255, 230));
    if (extra && extra[0]) gx_text_c(F_MED, 320, 56, extra, RGBA(255, 230, 150, 255));
    char ti[200], sub[200], f[200];
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    if (ti[0]) { fit_text(F_SMALL, ti, 300, f, sizeof(f)); gx_text_r(F_SMALL, 616, 30, f, RGBA(200, 190, 245, 220)); }
}
static void ar_over_screen(void) {
    char t[100];
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
    fun_shadow_text(F_LARGE, 320, 190, "Perdu !", 1.8f, RGBA(255, 120, 120, 255));
    snprintf(t, sizeof(t), "%d points", ar_score);
    fun_shadow_text(F_LARGE, 320, 250, t, 1.4f, RGBA(255, 255, 255, 255));
    if (ar_new_record) fun_shadow_text(F_LARGE, 320, 300, "Nouveau record !", 1.2f, RGBA(255, 220, 90, 255));
}

/* ---------- Snake ---------- */
#define SN_W 32
#define SN_H 20
#define SN_CELL 16.0f
#define SN_X0 64.0f
#define SN_Y0 100.0f
#define SN_MAX (SN_W * SN_H)
static u8 sn_x[SN_MAX], sn_y[SN_MAX];
static u8 sn_ox[SN_MAX], sn_oy[SN_MAX];      /* v49 : positions d'avant le dernier pas (glissement fluide) */
static u32 sn_step_len = 150;
static int sn_len, sn_dir, sn_next, sn_grow, sn_fx, sn_fy, sn_gold, sn_gx, sn_gy;
static u64 sn_gold_until = 0, sn_step_t = 0;
static int sn_wrap = 0;                      /* v23 : sans murs (on ressort de l'autre cote) */
static f32 sn_pulse = 0;
static const int sn_dx[4] = { 1, 0, -1, 0 }, sn_dy[4] = { 0, 1, 0, -1 };

static int sn_on_body(int x, int y, int from) {
    for (int i = from; i < sn_len; i++) if (sn_x[i] == x && sn_y[i] == y) return 1;
    return 0;
}
static void sn_place(int *fx, int *fy) {
    for (int tries = 0; tries < 400; tries++) {
        int x = (int)(lib_rand() % SN_W), y = (int)(lib_rand() % SN_H);
        if (!sn_on_body(x, y, 0) && !(x == sn_fx && y == sn_fy && fx != &sn_fx)) { *fx = x; *fy = y; return; }
    }
}
static void snake_new(void) {
    sn_len = 4;
    for (int i = 0; i < sn_len; i++) { sn_x[i] = sn_ox[i] = (u8)(10 - i); sn_y[i] = sn_oy[i] = SN_H / 2; }
    sn_dir = sn_next = 0;
    sn_grow = 0;
    sn_gold = 0;
    sn_fx = -1;
    sn_place(&sn_fx, &sn_fy);
    ar_score = 0;
    ar_phase = 0;
    sn_step_t = now_ms() + 600;
    ar_record = rg_record_get(AR_FILE_SNAKE);
}
static void snake_start(void) {
    fun_enter(FUN_SNAKE);
    ar_music();
    snake_new();
}
static void snake_turn(int d) { if (d != (sn_dir + 2) % 4) sn_next = d; }

static void snake_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    if (ar_beat()) {
        ar_flash = 1;
        sn_pulse = 1;
        if (!sn_gold && ar_phase == 0 && lib_rand() % 4 == 0) {          /* une note doree, sur le temps */
            sn_gold = 1;
            sn_place(&sn_gx, &sn_gy);
            sn_gold_until = now + 4000;
        }
    }
    ar_flash *= expf(-ui_dt * 6);
    sn_pulse *= expf(-ui_dt * 5);
    if (sn_gold && now > sn_gold_until) sn_gold = 0;
    if (ar_phase != 0 || paused) return;
    int sx = PAD_StickX(0), sy = PAD_StickY(0);                 /* stick principal aussi */
    if (abs(sx) > 50 || abs(sy) > 50) {
        if (abs(sx) > abs(sy)) snake_turn(sx > 0 ? 0 : 2); else snake_turn(sy > 0 ? 3 : 1);
    }
    f32 e = ar_drive();
    int step = (int)(165 - e * 85 - sn_len * 0.8f);              /* plus la musique est forte, plus il file */
    if (step < 55) step = 55;
    if (now < sn_step_t) return;
    sn_step_t = now + (u64)step;
    sn_step_len = (u32)step;
    sn_dir = sn_next;
    int nx = sn_x[0] + sn_dx[sn_dir], ny = sn_y[0] + sn_dy[sn_dir];
    if (sn_wrap) { nx = (nx + SN_W) % SN_W; ny = (ny + SN_H) % SN_H; }
    if (nx < 0 || ny < 0 || nx >= SN_W || ny >= SN_H || sn_on_body(nx, ny, 0)) { ar_game_over(AR_FILE_SNAKE); return; }
    int keep = sn_len + (sn_grow > 0 ? 1 : 0);
    if (keep > SN_MAX) keep = SN_MAX;
    for (int i = 0; i < keep; i++) {                         /* v49 : d'ou vient chaque anneau */
        int j = i < sn_len ? i : sn_len - 1;
        sn_ox[i] = sn_x[j]; sn_oy[i] = sn_y[j];
    }
    for (int i = keep - 1; i > 0; i--) { sn_x[i] = sn_x[i - 1]; sn_y[i] = sn_y[i - 1]; }
    if (sn_grow > 0) sn_grow--;
    sn_len = keep;
    sn_x[0] = (u8)nx;
    sn_y[0] = (u8)ny;
    if (nx == sn_fx && ny == sn_fy) {
        ar_score += 10 + (int)(e * 10);
        sn_grow += 2;
        sn_place(&sn_fx, &sn_fy);
        rumble(0, 40);
    }
    if (sn_gold && nx == sn_gx && ny == sn_gy) {
        ar_score += 50;
        sn_grow += 4;
        sn_gold = 0;
        rumble(0, 90);
    }
}

static void snake_input(u32 down) {
    if (ar_phase == 1) {
        if (down & PAD_BUTTON_A) snake_new();
        if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave();
        return;
    }
    if (down & PAD_BUTTON_START) { fun_leave(); return; }
    if (down & PAD_BUTTON_Y) { if (src == SRC_FILE && playing) paused = !paused; }
    if (down & PAD_BUTTON_RIGHT) snake_turn(0);
    else if (down & PAD_BUTTON_DOWN) snake_turn(1);
    else if (down & PAD_BUTTON_LEFT) snake_turn(2);
    else if (down & PAD_BUTTON_UP) snake_turn(3);
}

static void draw_snake(void) {
    f32 hue = pt_hue;
    fun_frame_begin(hue, 1.0f + ar_flash * 0.8f);
    fun_spectrum(480, 60, hue, 60);
    /* terrain */
    gx_rrect_fill(SN_X0 - 6, SN_Y0 - 6, SN_W * SN_CELL + 12, SN_H * SN_CELL + 12, 10, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 160));
    gx_rrect_stroke(SN_X0 - 6, SN_Y0 - 6, SN_W * SN_CELL + 12, SN_H * SN_CELL + 12, 10, 2.0f, hsv(hue, 0.7f, 1.0f, (u8)(120 + ar_flash * 135)));
    for (int x = 1; x < SN_W; x += 2) gx_rect(SN_X0 + x * SN_CELL, SN_Y0, 1, SN_H * SN_CELL, RGBA(255, 255, 255, 8));
    /* nourriture : une note qui bat avec la musique */
    f32 fr = 6 + bar_val[3] * 4 + sn_pulse * 3;
    gx_circle_glow(SN_X0 + sn_fx * SN_CELL + 8, SN_Y0 + sn_fy * SN_CELL + 8, fr, hsv(hue + 180, 0.8f, 1.0f, 160), 4);
    gx_circle(SN_X0 + sn_fx * SN_CELL + 8, SN_Y0 + sn_fy * SN_CELL + 8, fr, hsv(hue + 180, 0.4f, 1.0f, 255), hsv(hue + 200, 0.9f, 0.8f, 255));
    if (sn_gold) {
        f32 gr = 7 + sn_pulse * 4;
        gx_circle_glow(SN_X0 + sn_gx * SN_CELL + 8, SN_Y0 + sn_gy * SN_CELL + 8, gr + 2, RGBA(255, 220, 80, 200), 6);
        gx_circle(SN_X0 + sn_gx * SN_CELL + 8, SN_Y0 + sn_gy * SN_CELL + 8, gr, RGBA(255, 250, 180, 255), RGBA(255, 180, 30, 255));
    }
    /* le serpent : ses anneaux suivent le spectre ; v49 : ils glissent d'une case a l'autre */
    f32 sf = 1.0f;
    if (ar_phase == 0 && !paused && sn_step_len) {
        u64 nw = now_ms();
        sf = sn_step_t > nw ? 1.0f - (f32)(sn_step_t - nw) / (f32)sn_step_len : 1.0f;
        if (sf < 0) sf = 0;
        if (sf > 1) sf = 1;
    }
    f32 hx0 = 0, hy0 = 0;
    for (int i = sn_len - 1; i >= 0; i--) {
        f32 gx = sn_x[i], gy = sn_y[i];
        if (abs((int)sn_x[i] - (int)sn_ox[i]) + abs((int)sn_y[i] - (int)sn_oy[i]) == 1) {   /* (pas au passage d'un bord) */
            gx = sn_ox[i] + (sn_x[i] - (f32)sn_ox[i]) * sf; gy = sn_oy[i] + (sn_y[i] - (f32)sn_oy[i]) * sf;
        }
        f32 cx = SN_X0 + gx * SN_CELL + 8, cy = SN_Y0 + gy * SN_CELL + 8;
        if (i == 0) { hx0 = cx; hy0 = cy; }
        f32 b = bar_val[i % NBARS];
        f32 r = (i == 0 ? 8.0f : 6.0f) + b * 2.0f + (i < 6 ? sn_pulse * 1.5f : 0);
        col_t c0 = hsv(hue + i * 6.0f, 0.6f, 1.0f, 255), c1 = hsv(hue + i * 6.0f + 30, 0.9f, 0.6f + b * 0.4f, 255);
        gx_circle(cx, cy, r, c0, c1);
    }
    {                                                            /* les yeux */
        f32 cx = hx0, cy = hy0;
        f32 ex = sn_dy[sn_dir] * 3.5f, ey = sn_dx[sn_dir] * 3.5f, fx = sn_dx[sn_dir] * 3, fy = sn_dy[sn_dir] * 3;
        gx_circle(cx + ex + fx, cy + ey + fy, 2, RGBA(20, 10, 40, 255), RGBA(20, 10, 40, 255));
        gx_circle(cx - ex + fx, cy - ey + fy, 2, RGBA(20, 10, 40, 255), RGBA(20, 10, 40, 255));
    }
    char ex[40] = "";
    if (ar_drive() > 0.8f) snprintf(ex, sizeof(ex), "\xC3\x87" "a acc\xC3\xA9l\xC3\xA8re !");
    ar_hud(sn_wrap ? "SNAKE SANS MURS" : "SNAKE", ex);
    if (now_ms() < sn_step_t && ar_phase == 0 && sn_len == 4 && ar_score == 0) gx_text_c(F_MED, 320, 300, "Mange les notes ! (les dor\xC3\xA9" "es arrivent sur le rythme)", RGBA(255, 255, 255, 230));
    if (ar_phase == 1) ar_over_screen();
    fun_footer(ar_phase == 1 ? "A : rejouer     B : sortir" : paused ? "PAUSE   -   Y : reprendre   START : sortir" : "Croix ou stick : diriger     Y : pause     START : sortir");
}

/* ---------- Casse-briques ---------- */
#define BR_COLS 10
#define BR_ROWS 6
#define BR_X0 40.0f
#define BR_Y0 92.0f
#define BR_W 56.0f
#define BR_H 18.0f
#define BR_PY 430.0f
static u8 br_alive[BR_ROWS][BR_COLS], br_gold[BR_ROWS][BR_COLS];
static f32 br_px = 320, br_pw = 90, br_bx, br_by, br_vx, br_vy, br_hit[BR_ROWS][BR_COLS];
static int br_lives = 3, br_level = 1, br_stuck = 1, br_left = 0;
static u64 br_wide_until = 0;                /* v23 : brique doree = raquette large pendant 8 s */

static void br_fill(void) {
    br_left = 0;
    for (int r = 0; r < BR_ROWS; r++)
        for (int c = 0; c < BR_COLS; c++) { br_alive[r][c] = 1; br_gold[r][c] = 0; br_hit[r][c] = 0; br_left++; }
    br_stuck = 1;
}
static void bricks_new(void) {
    br_wide_until = 0;
    br_lives = 3;
    br_level = 1;
    ar_score = 0;
    ar_phase = 0;
    br_px = 320;
    br_fill();
    ar_record = rg_record_get(AR_FILE_BRICKS);
}
static void bricks_start(void) {
    fun_enter(FUN_BRICKS);
    ar_music();
    bricks_new();
}
static f32 br_speed(void) { return 270.0f + br_level * 25.0f + ar_drive() * 230.0f; }
static void br_launch(void) {
    br_stuck = 0;
    f32 a = -1.1f + (f32)(lib_rand() % 100) / 100.0f * 0.5f;
    br_vx = sinf(a);
    br_vy = -cosf(a);
}

static void bricks_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    f32 dt = ar_last ? (f32)(now - ar_last) / 1000.0f : 0.016f;
    if (dt > 0.05f) dt = 0.05f;
    ar_last = now;
    if (ar_beat()) {
        ar_flash = 1;
        if (lib_rand() % 3 == 0 && br_left > 3) {                     /* une brique devient doree sur le temps */
            int r = (int)(lib_rand() % BR_ROWS), c = (int)(lib_rand() % BR_COLS);
            if (br_alive[r][c]) br_gold[r][c] = 1;
        }
    }
    ar_flash *= expf(-dt * 6);
    for (int r = 0; r < BR_ROWS; r++) for (int c = 0; c < BR_COLS; c++) br_hit[r][c] *= expf(-dt * 8);
    if (ar_phase != 0 || paused) return;
    br_pw = now < br_wide_until ? 140 : 90;
    /* raquette : stick (vitesse selon l'inclinaison) ou croix */
    u32 held = PAD_ButtonsHeld(0);
    int sx = PAD_StickX(0);
    f32 mv = 0;
    if (abs(sx) > 12) mv = sx / 80.0f;
    if (held & PAD_BUTTON_LEFT) mv = -1;
    if (held & PAD_BUTTON_RIGHT) mv = 1;
    if (mv > 1) mv = 1;
    if (mv < -1) mv = -1;
    br_px += mv * 520.0f * dt;
    if (br_px < 20 + br_pw / 2) br_px = 20 + br_pw / 2;
    if (br_px > 620 - br_pw / 2) br_px = 620 - br_pw / 2;
    if (br_stuck) { br_bx = br_px; br_by = BR_PY - 9; return; }
    /* la balle, en petits pas (pas de traversee) */
    f32 dist = br_speed() * dt;
    int steps = (int)(dist / 4.0f) + 1;
    f32 st = dist / steps;
    for (int s = 0; s < steps; s++) {
        f32 ox = br_bx, oy = br_by;
        br_bx += br_vx * st;
        br_by += br_vy * st;
        if (br_bx < 26) { br_bx = 26; br_vx = fabsf(br_vx); }
        if (br_bx > 614) { br_bx = 614; br_vx = -fabsf(br_vx); }
        if (br_by < 76) { br_by = 76; br_vy = fabsf(br_vy); }
        if (br_vy > 0 && br_by >= BR_PY - 6 && oy < BR_PY - 6 && fabsf(br_bx - br_px) <= br_pw / 2 + 6) {   /* raquette */
            f32 k = (br_bx - br_px) / (br_pw / 2);
            if (k > 1) k = 1;
            if (k < -1) k = -1;
            br_vx = k * 0.85f;
            br_vy = -sqrtf(1.0f - br_vx * br_vx);
            br_by = BR_PY - 6;
            rumble(0, 25);
        }
        if (br_by > 490) {                                          /* balle perdue */
            br_lives--;
            rumble(0, 200);
            if (br_lives <= 0) { ar_game_over(AR_FILE_BRICKS); return; }
            br_stuck = 1;
            return;
        }
        int c = (int)((br_bx - BR_X0) / BR_W), r = (int)((br_by - BR_Y0) / BR_H);
        if (br_bx >= BR_X0 && br_by >= BR_Y0 && c >= 0 && c < BR_COLS && r >= 0 && r < BR_ROWS && br_alive[r][c]) {
            br_alive[r][c] = 0;
            br_left--;
            ar_score += br_gold[r][c] ? 50 : 10 + (BR_ROWS - r) * 2;
            if (br_gold[r][c]) br_wide_until = now + 8000;
            br_hit[r][c] = 1;
            int oc = (int)((ox - BR_X0) / BR_W), orr = (int)((oy - BR_Y0) / BR_H);
            if (orr != r || oy < BR_Y0) br_vy = -br_vy; else if (oc != c) br_vx = -br_vx; else br_vy = -br_vy;
            br_bx = ox; br_by = oy;
            rumble(0, br_gold[r][c] ? 80 : 30);
            if (br_left <= 0) { br_level++; br_fill(); ar_score += 100; return; }
        }
    }
}

static void bricks_input(u32 down) {
    if (ar_phase == 1) {
        if (down & PAD_BUTTON_A) bricks_new();
        if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave();
        return;
    }
    if (down & PAD_BUTTON_START) { fun_leave(); return; }
    if (down & PAD_BUTTON_Y) { if (src == SRC_FILE && playing) paused = !paused; }
    if ((down & PAD_BUTTON_A) && br_stuck && !paused) br_launch();
}

static void draw_bricks(void) {
    f32 hue = pt_hue;
    fun_frame_begin(hue, 1.0f + ar_flash * 0.8f);
    fun_spectrum(480, 90, hue, 70);
    gx_rrect_stroke(20, 72, 600, 400, 10, 2.0f, hsv(hue, 0.6f, 1.0f, (u8)(90 + ar_flash * 160)));
    for (int r = 0; r < BR_ROWS; r++)
        for (int c = 0; c < BR_COLS; c++) {
            f32 x = BR_X0 + c * BR_W, y = BR_Y0 + r * BR_H;
            if (!br_alive[r][c]) {
                if (br_hit[r][c] > 0.05f) gx_rrect_fill(x + 2 - br_hit[r][c] * 4, y + 2 - br_hit[r][c] * 4, BR_W - 4 + br_hit[r][c] * 8, BR_H - 4 + br_hit[r][c] * 8, 4,
                                                        RGBA(255, 255, 255, (u8)(br_hit[r][c] * 200)), RGBA(255, 255, 255, 0));
                continue;
            }
            f32 b = bar_val[(c * 5 + 2) % NBARS];                   /* chaque colonne suit une bande du spectre */
            col_t t, bo;
            if (br_gold[r][c]) { t = RGBA(255, 245, 170, 255); bo = RGBA(230, 160, 20, 255); }
            else { t = hsv(hue + r * 28.0f, 0.55f, 0.75f + b * 0.25f, 255); bo = hsv(hue + r * 28.0f + 20, 0.9f, 0.45f + b * 0.35f + ar_flash * 0.2f, 255); }
            gx_rrect_fill(x + 2, y + 2, BR_W - 4, BR_H - 4, 4, t, bo);
        }
    /* raquette et balle */
    gx_rrect_fill(br_px - br_pw / 2, BR_PY, br_pw, 10, 5, hsv(hue + 180, 0.4f, 1.0f, 255), hsv(hue + 200, 0.9f, 0.7f, 255));
    gx_circle_glow(br_bx, br_by, 7 + ar_flash * 3, RGBA(255, 255, 255, (u8)(80 + ar_flash * 120)), 4);
    gx_circle(br_bx, br_by, 6, RGBA(255, 255, 255, 255), RGBA(200, 210, 255, 255));
    char ex[60];
    snprintf(ex, sizeof(ex), "Niveau %d     Vies %d%s%s", br_level, br_lives, ar_drive() > 0.8f ? "     TURBO !" : "", now_ms() < br_wide_until ? "     GRANDE RAQUETTE" : "");
    ar_hud("CASSE-BRIQUES", ex);
    if (br_stuck && ar_phase == 0) gx_text_c(F_MED, 320, 340, "A : lancer la balle", RGBA(255, 255, 255, 230));
    if (ar_phase == 1) ar_over_screen();
    fun_footer(ar_phase == 1 ? "A : rejouer     B : sortir" : paused ? "PAUSE   -   Y : reprendre   START : sortir" : "Stick ou croix : raquette     A : lancer     Y : pause     START : sortir");
}


/* ======================= Nouveaux mini-jeux (v23) ======================= */
/* ---------- Pong : contre l'ordinateur ou a deux (manette 2) ---------- */
static f32 po_ly = 260, po_ry = 260, po_bx = 320, po_by = 260, po_vx = 1, po_vy = 0.4f, po_serve_t = 0;
static int po_sl = 0, po_sr = 0, po_two = 0;
#define PO_X0 40.0f
#define PO_X1 600.0f
#define PO_Y0 84.0f
#define PO_Y1 440.0f
#define PO_PH 70.0f
static void po_serve(int dir) {
    po_bx = 320; po_by = (PO_Y0 + PO_Y1) / 2;
    f32 a = ((f32)(lib_rand() % 100) / 100.0f - 0.5f) * 0.9f;
    po_vx = dir * cosf(a); po_vy = sinf(a);
    po_serve_t = 1.0f;                                     /* (petite pause avant de repartir) */
}
static void pong_new(void) { po_sl = po_sr = 0; po_ly = po_ry = (PO_Y0 + PO_Y1) / 2; ar_phase = 0; ar_last = 0; po_serve(lib_rand() % 2 ? 1 : -1); }
static void pong_start(void) { fun_enter(FUN_PONG); ar_music(); po_two = (fun_pads & 2) != 0; pong_new(); }
static f32 po_stick(int ch) {
    u32 h = PAD_ButtonsHeld(ch);
    int sy = PAD_StickY(ch);
    f32 v = abs(sy) > 14 ? -sy / 75.0f : 0;
    if (h & PAD_BUTTON_UP) v = -1;
    if (h & PAD_BUTTON_DOWN) v = 1;
    return v > 1 ? 1 : v < -1 ? -1 : v;
}
static void pong_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    f32 dt = ar_last ? (f32)(now - ar_last) / 1000.0f : 0.016f;
    if (dt > 0.05f) dt = 0.05f;
    ar_last = now;
    if (ar_beat()) ar_flash = 1;
    ar_flash *= expf(-dt * 6);
    if (ar_phase || paused) return;
    po_ly += po_stick(0) * 430 * dt;
    if (po_two) po_ry += po_stick(1) * 430 * dt;
    else {                                                  /* l'ordinateur suit la balle, pas parfaitement */
        f32 want = po_vx > 0 ? po_by : (PO_Y0 + PO_Y1) / 2, d = want - po_ry, mx = (260 + ar_drive() * 80) * dt;
        po_ry += d > mx ? mx : d < -mx ? -mx : d;
    }
    for (int s = 0; s < 2; s++) { f32 *p = s ? &po_ry : &po_ly; if (*p < PO_Y0 + PO_PH / 2) *p = PO_Y0 + PO_PH / 2; if (*p > PO_Y1 - PO_PH / 2) *p = PO_Y1 - PO_PH / 2; }
    if (po_serve_t > 0) { po_serve_t -= dt; return; }
    f32 sp = 300 + ar_drive() * 260 + (po_sl + po_sr) * 12;   /* la balle accelere avec la musique */
    f32 dist = sp * dt;
    int steps = (int)(dist / 4) + 1;
    for (int k = 0; k < steps; k++) {
        po_bx += po_vx * dist / steps;
        po_by += po_vy * dist / steps;
        if (po_by < PO_Y0 + 6) { po_by = PO_Y0 + 6; po_vy = fabsf(po_vy); }
        if (po_by > PO_Y1 - 6) { po_by = PO_Y1 - 6; po_vy = -fabsf(po_vy); }
        for (int s = 0; s < 2; s++) {
            f32 px = s ? PO_X1 - 14 : PO_X0 + 14, py = s ? po_ry : po_ly;
            int dir = s ? -1 : 1;
            if ((s ? po_vx > 0 : po_vx < 0) && fabsf(po_bx - px) < 8 && fabsf(po_by - py) < PO_PH / 2 + 6) {
                f32 kk = (po_by - py) / (PO_PH / 2);
                po_vx = dir * cosf(kk * 0.9f);
                po_vy = sinf(kk * 0.9f);
                po_bx = px + dir * 8;
                rumble(s && po_two ? 1 : 0, 30);
            }
        }
        if (po_bx < PO_X0 - 10 || po_bx > PO_X1 + 10) {
            int right_scores = po_bx < PO_X0;
            if (right_scores) po_sr++; else po_sl++;
            rumble(0, 120);
            if (po_sl >= 7 || po_sr >= 7) { ar_phase = 1; return; }
            po_serve(right_scores ? -1 : 1);
            return;
        }
    }
}
static void pong_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) pong_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) { fun_leave(); return; }
    if ((down & PAD_BUTTON_Y) && src == SRC_FILE && playing) paused = !paused;
}
static void draw_pong(void) {
    f32 hue = pt_hue;
    char t[64];
    fun_frame_begin(hue, 1.0f + ar_flash * 0.7f);
    fun_spectrum(480, 60, hue, 60);
    gx_rrect_stroke(PO_X0 - 6, PO_Y0 - 6, PO_X1 - PO_X0 + 12, PO_Y1 - PO_Y0 + 12, 8, 2, hsv(hue, 0.6f, 1.0f, (u8)(90 + ar_flash * 160)));
    for (f32 y = PO_Y0; y < PO_Y1; y += 22) gx_rect(319, y, 2, 12, RGBA(255, 255, 255, 60));
    snprintf(t, sizeof(t), "%d", po_sl); fun_shadow_text(F_LARGE, 250, 70, t, 1.4f, RGBA(140, 200, 255, 255));
    snprintf(t, sizeof(t), "%d", po_sr); fun_shadow_text(F_LARGE, 390, 70, t, 1.4f, RGBA(255, 150, 170, 255));
    gx_rrect_fill(PO_X0 + 8, po_ly - PO_PH / 2, 10, PO_PH, 5, RGBA(160, 210, 255, 255), RGBA(80, 140, 255, 255));
    gx_rrect_fill(PO_X1 - 18, po_ry - PO_PH / 2, 10, PO_PH, 5, RGBA(255, 170, 190, 255), RGBA(230, 80, 120, 255));
    gx_circle_glow(po_bx, po_by, 8 + ar_flash * 3, RGBA(255, 255, 255, (u8)(80 + ar_flash * 120)), 4);
    gx_circle(po_bx, po_by, 7, RGBA(255, 255, 255, 255), RGBA(220, 220, 255, 255));
    gx_text(F_SMALL, 24, 30, "PONG", RGBA(255, 210, 110, 255), RGBA(255, 160, 60, 255), 3);
    gx_text_r(F_SMALL, 616, 30, po_two ? "Joueur 1  contre  Joueur 2" : "Toi  contre  la GameCube", RGBA(220, 210, 255, 220));
    if (ar_phase) {
        gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
        fun_shadow_text(F_LARGE, 320, 230, po_sl > po_sr ? (po_two ? "Joueur 1 gagne !" : "Gagn\xC3\xA9 !") : (po_two ? "Joueur 2 gagne !" : "Perdu !"), 1.7f, RGBA(255, 220, 90, 255));
    }
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "Stick ou croix : raquette (manette 2 pour le joueur 2)     Y : pause     START : sortir");
}

/* ---------- Note volante : A pour battre des ailes, les murs arrivent en rythme ---------- */
#define FL_N 6
static f32 fl_y, fl_v, fl_px[FL_N], fl_gap[FL_N], fl_sz[FL_N];
static u8 fl_pass[FL_N];
static u64 fl_next = 0;
static double fl_lastT = -1e9, fl_T[FL_N];
static f32 fl_vs[FL_N];
#define FL_X 150.0f
static void flappy_new(void) {
    fl_y = 240; fl_v = 0;
    for (int i = 0; i < FL_N; i++) { fl_px[i] = -100; fl_T[i] = -1; }
    ar_score = 0; ar_phase = 0; ar_last = 0; fl_next = now_ms() + 1200; fl_lastT = -1e9;
    ar_record = rg_record_get("jeu:note-volante");
}
static void flappy_start(void) { fun_enter(FUN_FLAPPY); ar_music(); flappy_new(); }
static void flappy_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    f32 dt = ar_last ? (f32)(now - ar_last) / 1000.0f : 0.016f;
    if (dt > 0.05f) dt = 0.05f;
    ar_last = now;
    int beat = ar_beat();
    if (beat) ar_flash = 1;
    ar_flash *= expf(-dt * 6);
    if (ar_phase || paused) return;
    f32 sp = 150 + ar_drive() * 120 + ar_score * 2;
    f32 spawn_x = -1;
    if (ba_ok() && ba_w > 0) {                                  /* v72 : le mur arrive sur l'oiseau pile sur un coup */
        double an = audio_ms(), tr = (680.0 - FL_X) / sp * 1000.0;
        double lo = an + tr * 0.85;                             /* (jamais au milieu de l'ecran) */
        double T = ba_next_strong(fl_lastT + 1000 > lo ? fl_lastT + 1000 : lo);
        if (T > 0 && T <= an + tr) { spawn_x = FL_X + sp * (f32)(T - an) / 1000.0f; fl_lastT = T; fl_next = now + 1100; }
        else if (now > fl_next + 1400) { spawn_x = 680; fl_next = now + 1100; }          /* (passage calme : un mur quand meme) */
    } else if ((beat && now > fl_next) || now > fl_next + 900) { spawn_x = 680; fl_next = now + 1100; }   /* (radio : sur le temps) */
    if (spawn_x > 0) {
        for (int i = 0; i < FL_N; i++) if (fl_px[i] < -60) {
            fl_px[i] = spawn_x; fl_gap[i] = 150 + (f32)(lib_rand() % 220); fl_sz[i] = 120 - (ar_score > 20 ? 20 : ar_score) ; fl_pass[i] = 0;
            fl_T[i] = spawn_x < 680 || ba_ok() ? fl_lastT : -1; fl_vs[i] = sp;      /* (cale sur le son : sa position suit l'horloge du son) */
            if (spawn_x >= 680) fl_T[i] = -1;
            break;
        }
    }
    fl_v += 900 * dt;
    fl_y += fl_v * dt;
    for (int i = 0; i < FL_N; i++) {
        if (fl_px[i] < -60) continue;
        if (fl_T[i] > 0 && ba_ok()) fl_px[i] = FL_X + fl_vs[i] * (f32)(fl_T[i] - audio_ms()) / 1000.0f;
        else fl_px[i] -= sp * dt;
        if (!fl_pass[i] && fl_px[i] + 30 < FL_X) { fl_pass[i] = 1; ar_score++; rumble(0, 25); }
        if (fabsf(fl_px[i] - FL_X) < 30 + 10 && fabsf(fl_y - fl_gap[i]) > fl_sz[i] / 2 - 10) { ar_game_over("jeu:note-volante"); return; }
    }
    if (fl_y < 76 || fl_y > 470) ar_game_over("jeu:note-volante");
}
static void flappy_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) flappy_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) { fun_leave(); return; }
    if ((down & PAD_BUTTON_Y) && src == SRC_FILE && playing) paused = !paused;
    if (down & (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X)) fl_v = -330;
}
static void draw_flappy(void) {
    f32 hue = pt_hue;
    fun_frame_begin(hue, 1.0f + ar_flash * 0.8f);
    fun_spectrum(480, 80, hue, 80);
    for (int i = 0; i < FL_N; i++) {
        if (fl_px[i] < -60) continue;
        f32 x = fl_px[i] - 30, top = fl_gap[i] - fl_sz[i] / 2, bot = fl_gap[i] + fl_sz[i] / 2;
        col_t c0 = hsv(hue + i * 40, 0.6f, 0.9f, 255), c1 = hsv(hue + i * 40 + 30, 0.9f, 0.5f + ar_flash * 0.3f, 255);
        gx_rrect_fill(x, 72, 60, top - 72, 6, c1, c0);
        gx_rrect_fill(x, bot, 60, 480 - bot, 6, c0, c1);
    }
    /* la note */
    f32 r = 11 + ar_flash * 2;
    gx_circle_glow(FL_X, fl_y, r + 3, RGBA(255, 230, 120, 120), 4);
    gx_ellipse(FL_X, fl_y, r * 1.2f, r, RGBA(255, 245, 170, 255), RGBA(255, 190, 60, 255));
    gx_rect(FL_X + r * 1.0f, fl_y - 30, 3, 30, RGBA(255, 220, 120, 255));
    gx_tri(FL_X + r + 3, fl_y - 30, FL_X + r + 14, fl_y - 22, FL_X + r + 3, fl_y - 18, RGBA(255, 220, 120, 255));
    ar_hud("NOTE VOLANTE", ar_drive() > 0.8f ? "\xC3\x87" "a acc\xC3\xA9l\xC3\xA8re !" : "");
    if (ar_score == 0 && !ar_phase) gx_text_c(F_MED, 320, 300, "A : battre des ailes  -  passe entre les murs, ils arrivent sur le rythme", RGBA(255, 255, 255, 220));
    if (ar_phase) ar_over_screen();
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "A : voler     Y : pause     START : sortir");
}

/* ---------- Simon musical : retiens la suite de boutons ---------- */
#define SI_MAX 64
static u8 si_seq[SI_MAX];
static int si_len = 0, si_pos = 0, si_show = 0, si_lit = -1;
static u64 si_t = 0;
static f32 si_glow[4];
static void si_add(void) { if (si_len < SI_MAX) si_seq[si_len++] = (u8)(lib_rand() % 4); si_show = 1; si_pos = 0; si_t = now_ms() + 700; }
static void simon_new(void) { si_len = 0; ar_score = 0; ar_phase = 0; memset(si_glow, 0, sizeof(si_glow)); si_add(); ar_record = rg_record_get("jeu:simon"); }
static void simon_start(void) { fun_enter(FUN_SIMON); ar_music(); simon_new(); }
static void simon_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    if (ar_beat()) ar_flash = 1;
    ar_flash *= expf(-ui_dt * 6);
    for (int i = 0; i < 4; i++) si_glow[i] *= expf(-ui_dt * 5);
    if (ar_phase || !si_show) return;
    int step = 650 - (si_len > 10 ? 250 : si_len * 25);       /* de plus en plus vite */
    if (now < si_t) return;
    if (si_pos < si_len) { si_glow[si_seq[si_pos]] = 1; si_lit = si_seq[si_pos]; si_pos++; si_t = now + (u64)step; }
    else { si_show = 0; si_pos = 0; si_lit = -1; }
}
static void simon_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) simon_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) { fun_leave(); return; }
    if (si_show) return;
    static const u32 btn[4] = { PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X, PAD_BUTTON_Y };
    for (int c = 0; c < 4; c++) if (down & btn[c]) {
        si_glow[c] = 1;
        if (si_seq[si_pos] != c) { ar_score = si_len - 1; ar_game_over("jeu:simon"); return; }
        if (++si_pos >= si_len) { ar_score = si_len; rumble(0, 60); si_add(); si_t = now_ms() + 900; }
        return;
    }
}
static void draw_simon(void) {
    f32 hue = pt_hue;
    char t[64];
    fun_frame_begin(hue, 1.0f + ar_flash * 0.6f);
    fun_spectrum(480, 60, hue, 60);
    static const f32 px[4] = { 400, 240, 400, 240 }, py[4] = { 330, 330, 170, 170 };   /* A bas droite, B bas gauche, X haut droite, Y haut gauche */
    for (int c = 0; c < 4; c++) {
        f32 g = si_glow[c], r = 62 + g * 8 + ar_flash * 3;
        if (g > 0.05f) gx_circle_glow(px[c], py[c], r + 6, RGBA(lane_top[c].r, lane_top[c].g, lane_top[c].b, (u8)(g * 220)), 8);
        col_t a = lerp_c(RGBA(lane_bot[c].r / 2, lane_bot[c].g / 2, lane_bot[c].b / 2, 255), lane_top[c], 0.25f + g * 0.75f);
        gx_circle(px[c], py[c], r, a, RGBA(lane_bot[c].r / 2, lane_bot[c].g / 2, lane_bot[c].b / 2, 255));
        fun_shadow_text(F_LARGE, px[c], py[c] + 12, lane_btn[c], 1.3f, RGBA(255, 255, 255, (u8)(150 + g * 105)));
    }
    snprintf(t, sizeof(t), "Niveau %d", si_len);
    ar_hud("SIMON", si_show ? "Regarde..." : "\xC3\x80 toi !");
    gx_text_c(F_MED, 320, 82, t, RGBA(255, 230, 150, 255));
    if (ar_phase) ar_over_screen();
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "Retiens l'ordre, puis rejoue-le avec A  B  X  Y     START : sortir");
}

/* ======================= Menu "Jeux et karaoke" ======================= */
static void games_open(void) {
    am_n = 0;
    am_sel = 0;
    am_track = fun_selected_track();
    snprintf(am_title, sizeof(am_title), "Jeux et karaok\xC3\xA9");
    am_item("Karaok\xC3\xA9 (paroles en grand)", AC_KARAOKE, 0);
    am_item("Jeu de rythme", AC_RHYTHM, 0);
    am_item("Quiz musical (1 \xC3\xA0 4 joueurs)", AC_QUIZ, 0);
    am_item("Snake musical", AC_SNAKE, 0);
    am_item("Casse-briques musical", AC_BRICKS, 0);
    am_item("Titres radio gard\xC3\xA9s", AC_TITLES, 0);
    am_item("Fermer", AC_CLOSE, 0);
    am_open = 1;
}

/* actions du popup (appelees par actions_do) */
static void fun_action(int code, int arg, int track, const char *txt) {
    switch (code) {
    case AC_KARAOKE: karaoke_start(track); break;
    case AC_RHYTHM:  rhythm_start(track); break;
    case AC_QUIZ:    quiz_start(); break;
    case AC_SNAKE:   sn_wrap = 0; snake_start(); break;
    case AC_BRICKS:  bricks_start(); break;
    case AC_TITLES:  titles_open(); break;
    case AC_TITLE:   titles_search(txt); break;
    case AC_TITLES_CLEAR: remove(TITLES_FILE); lib_note("Liste des titres effac\xC3\xA9" "e"); break;
    }
    (void)arg;
}

/* ======================= Boucle ======================= */
static void fun_input(u32 down, u32 nav, int fire) {
    switch (fun_mode) {
    case FUN_KARAOKE: karaoke_input(down); break;
    case FUN_RHYTHM:  rhythm_input(down, nav, fire); break;
    case FUN_QUIZ:    quiz_input(down, nav, fire); break;
    case FUN_WAKE:    wake_input(down); break;
    case FUN_SNAKE:   snake_input(down); break;
    case FUN_BRICKS:  bricks_input(down); break;
    case FUN_PONG:    pong_input(down); break;
    case FUN_FLAPPY:  flappy_input(down); break;
    case FUN_SIMON:   simon_input(down); break;
    }
}

static void fun_pump(u32 pads) {
    fun_pads = pads ? pads : 1;
    rumble_pump();
    wake_prefetch();
    wall_pump();
    if (tab_shown(TAB_PODCAST)) podauto_pump();     /* v128 */
    if (fun_mode == FUN_OFF) return;
    if (fun_mode == FUN_RHYTHM) { party_update(); rhythm_pump(); }
    else if (fun_mode == FUN_QUIZ) { party_update(); quiz_pump(); }
    else if (fun_mode == FUN_SNAKE) { party_update(); snake_pump(); }
    else if (fun_mode == FUN_BRICKS) { party_update(); bricks_pump(); }
    else if (fun_mode == FUN_PONG) { party_update(); pong_pump(); }
    else if (fun_mode == FUN_FLAPPY) { party_update(); flappy_pump(); }
    else if (fun_mode == FUN_SIMON) { party_update(); simon_pump(); }
    else { party_update(); last_input_ms = now_ms(); }
}

static void draw_fun(void) {
    int saved = th_identity;
    th_identity = 1;
    switch (fun_mode) {
    case FUN_KARAOKE: draw_karaoke(); break;
    case FUN_RHYTHM:  draw_rhythm(); break;
    case FUN_QUIZ:    draw_quiz(); break;
    case FUN_SNAKE:   draw_snake(); break;
    case FUN_BRICKS:  draw_bricks(); break;
    case FUN_PONG:    draw_pong(); break;
    case FUN_FLAPPY:  draw_flappy(); break;
    case FUN_SIMON:   draw_simon(); break;
    default:          draw_wake(); break;
    }
    th_identity = saved;
    draw_actions();                                  /* (petits messages) */
    service_audio();
    present_frame();
}

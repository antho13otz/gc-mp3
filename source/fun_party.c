// GC Station v235 : jeux de soiree, de 1 a 4 manettes (seul : contre la console ou contre son record).
// Inclus dans main.c apres fun_extra.c (on reprend la musique en fond, les eclairs sur les basses, les records).
//  - Degaine ! : au signal, le premier qui appuie sur A marque ; trop tot = point perdu. En 5 points.
//  - Course folle : on martele A pour faire avancer son coureur jusqu'a l'arrivee.
//  - Chasse aux bulles : un viseur au stick par joueur, A eclate les bulles ; 45 s, les dorees valent 3.

/* ---------- joueurs : les manettes branchees ; seul : la console joue aussi ---------- */
static int jx_np = 1, jx_ch[4] = { 0, 1, 2, 3 }, jx_cpu = 0;
static void jx_players(int with_cpu) {
    jx_np = 0;
    for (int c = 0; c < 4; c++) if ((fun_pads >> c) & 1) jx_ch[jx_np++] = c;
    if (!jx_np) { jx_ch[0] = 0; jx_np = 1; }
    jx_cpu = with_cpu && jx_np == 1;
}
static int jx_n(void) { return jx_np + jx_cpu; }
static int jx_is_cpu(int i) { return i >= jx_np; }
static col_t jx_col(int i) { return jx_is_cpu(i) ? RGBA(205, 205, 220, 255) : qz_pcol[jx_ch[i] & 3]; }
static void jx_name(int i, char *o, int cap) {
    if (jx_is_cpu(i)) snprintf(o, cap, "Console");
    else if (jx_np == 1 && !jx_cpu) snprintf(o, cap, "Toi");
    else snprintf(o, cap, "Joueur %d", jx_ch[i] + 1);
}
static u32 jx_down(int i) { return jx_is_cpu(i) ? 0 : PAD_ButtonsDown(jx_ch[i]); }
/* classement final (fin de partie) : score decroissant, ou temps croissant si lower */
static int jx_rank[5];
static void jx_sort(const int *val, int lower) {
    int n = jx_n();
    for (int i = 0; i < n; i++) jx_rank[i] = i;
    for (int i = 1; i < n; i++) for (int j = i; j > 0; j--) {
        int a = jx_rank[j - 1], b = jx_rank[j];
        int swap = lower ? (val[b] >= 0 && (val[a] < 0 || val[b] < val[a])) : val[b] > val[a];
        if (swap) { jx_rank[j - 1] = b; jx_rank[j] = a; } else break;
    }
}
static void jx_over(const char *title, const int *val, const char *unit, int is_time) {
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 160), RGBA(0, 0, 0, 160), RGBA(0, 0, 0, 160), RGBA(0, 0, 0, 160));
    fun_shadow_text(F_LARGE, 320, 150, title, 1.6f, RGBA(255, 220, 90, 255));
    jx_sort(val, is_time);
    char nm[32], t[96], v[48];
    for (int r = 0; r < jx_n(); r++) {
        int i = jx_rank[r];
        jx_name(i, nm, sizeof(nm));
        if (val[i] < 0) snprintf(v, sizeof(v), "-");
        else if (is_time) snprintf(v, sizeof(v), "%d,%d s", val[i] / 1000, val[i] % 1000 / 100);
        else snprintf(v, sizeof(v), "%d %s", val[i], unit);
        snprintf(t, sizeof(t), "%d.  %s", r + 1, nm);
        f32 y = 214 + r * 38;
        gx_circle(196, y - 7, 9, jx_col(i), RGBA(jx_col(i).r / 2, jx_col(i).g / 2, jx_col(i).b / 2, 255));
        fun_shadow_text(F_MED, 300, y, t, r == 0 ? 1.3f : 1.1f, r == 0 ? RGBA(255, 230, 120, 255) : RGBA(255, 255, 255, 255));
        fun_shadow_text(F_MED, 470, y, v, r == 0 ? 1.3f : 1.1f, RGBA(255, 255, 255, 230));
    }
    if (ar_new_record) fun_shadow_text(F_LARGE, 320, 214 + jx_n() * 38 + 10, "Nouveau record !", 1.1f, RGBA(255, 220, 90, 255));
}

/* ======================= Degaine ! ======================= */
#define DU_WIN 5
static int du_phase = 0;                   /* 0 attente du signal, 1 signal, 2 resultat de la manche, 3 fin */
static u64 du_t = 0, du_sig = 0;
static int du_score[5], du_ms[5], du_best[5], du_cpu_ms = 300, du_who = -1, du_early = 0;
static void du_round(void) {
    du_phase = 0;
    du_t = now_ms() + 1500 + lib_rand() % 3000;           /* signal entre 1,5 et 4,5 s */
    du_who = -1; du_early = 0;
    du_cpu_ms = 210 + lib_rand() % 190;                    /* la console : entre 210 et 400 ms */
    for (int i = 0; i < 5; i++) du_ms[i] = -1;
}
static void duel_new(void) {
    jx_players(1);
    memset(du_score, 0, sizeof(du_score));
    for (int i = 0; i < 5; i++) du_best[i] = -1;
    ar_phase = 0; ar_new_record = 0;
    ar_record = rg_record_get("jeu:degaine");              /* (garde 2000 - meilleur temps en ms) */
    du_round();
}
static void duel_start(void) { fun_enter(FUN_DUEL); ar_music(); duel_new(); }
static void du_point(int i, int early) {
    du_who = i; du_early = early;
    if (early) { if (du_score[i] > 0) du_score[i]--; rumble(jx_is_cpu(i) ? 0 : jx_ch[i], 300); }
    else { du_score[i]++; if (!jx_is_cpu(i)) rumble(jx_ch[i], 90); }
    du_phase = 2;
    du_t = now_ms() + 1700;
}
static void duel_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    if (ar_beat()) ar_flash = 1;
    ar_flash *= expf(-ui_dt * 6);
    if (ar_phase) return;
    if (du_phase == 0) {
        for (int i = 0; i < jx_np; i++) if (jx_down(i) & PAD_BUTTON_A) { du_point(i, 1); return; }
        if (now >= du_t) { du_phase = 1; du_sig = now; for (int c = 0; c < 4; c++) if ((fun_pads >> c) & 1) rumble(c, 60); }
    } else if (du_phase == 1) {
        for (int i = 0; i < jx_np; i++) if (jx_down(i) & PAD_BUTTON_A) {
            du_ms[i] = (int)(now - du_sig);
            if (du_best[i] < 0 || du_ms[i] < du_best[i]) du_best[i] = du_ms[i];
            du_point(i, 0);
            if (jx_np == 1 && du_ms[i] < 2000) {               /* seul : record du meilleur reflexe */
                ar_score = 2000 - du_ms[i];
                if (ar_score > ar_record) { ar_record = ar_score; ar_new_record = 1; if (sd_ok) rg_record_set("jeu:degaine", ar_score); }
            }
            return;
        }
        if (jx_cpu && now - du_sig >= (u64)du_cpu_ms) { du_ms[jx_np] = du_cpu_ms; du_point(jx_np, 0); return; }
        if (now - du_sig > 3000) { du_phase = 2; du_who = -1; du_t = now + 1200; }   /* (personne) */
    } else if (du_phase == 2 && now >= du_t) {
        for (int i = 0; i < jx_n(); i++) if (du_score[i] >= DU_WIN) { ar_phase = 1; du_phase = 3; return; }
        du_round();
    }
}
static void duel_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) duel_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) fun_leave();
}
static void draw_duel(void) {
    char t[96], nm[32];
    fun_frame_begin(du_phase == 1 ? 120 : 350, 1.0f + ar_flash * 0.5f);
    fun_spectrum(480, 50, pt_hue, 50);
    gx_text(F_SMALL, 24, 30, "D\xC3\x89GAINE !", RGBA(255, 210, 110, 255), RGBA(255, 160, 60, 255), 3);
    snprintf(t, sizeof(t), "Premier \xC3\xA0 %d points", DU_WIN);
    gx_text_c(F_MED, 320, 56, t, RGBA(255, 230, 150, 255));
    if (ar_record > 0) { snprintf(t, sizeof(t), "Meilleur r\xC3\xA9" "flexe %d ms", 2000 - ar_record); gx_text_r(F_SMALL, 616, 30, t, RGBA(220, 210, 255, 230)); }
    /* le signal */
    f32 cx = 320, cy = 200, r = 92;
    u64 now = now_ms();
    if (du_phase == 1) {
        gx_circle_glow(cx, cy, r + 14, RGBA(90, 255, 140, 200), 10);
        gx_circle(cx, cy, r, RGBA(120, 255, 160, 255), RGBA(30, 170, 80, 255));
        fun_shadow_text(F_LARGE, cx, cy + 18, "FEU !", 2.2f, RGBA(255, 255, 255, 255));
    } else {
        f32 pulse = du_phase == 0 ? 0.5f + 0.5f * sinf((f32)(now % 100000) / 1000.0f * 5) : 0;
        gx_circle(cx, cy, r, RGBA(150 + (int)(40 * pulse), 30, 50, 255), RGBA(70, 10, 25, 255));
        if (du_phase == 0) fun_shadow_text(F_LARGE, cx, cy + 14, "Attends...", 1.3f, RGBA(255, 220, 220, 255));
        else if (du_phase == 2) {
            if (du_who < 0) fun_shadow_text(F_LARGE, cx, cy + 12, "Personne !", 1.2f, RGBA(255, 255, 255, 255));
            else {
                jx_name(du_who, nm, sizeof(nm));
                fun_shadow_text(F_LARGE, cx, cy - 4, nm, 1.3f, jx_col(du_who));
                if (du_early) fun_shadow_text(F_MED, cx, cy + 30, "trop t\xC3\xB4t ! (-1)", 1.1f, RGBA(255, 200, 200, 255));
                else { snprintf(t, sizeof(t), "%d ms", du_ms[du_who]); fun_shadow_text(F_MED, cx, cy + 30, t, 1.2f, RGBA(255, 255, 255, 255)); }
            }
        }
    }
    /* les joueurs */
    int n = jx_n();
    f32 cw = 600.0f / n;
    for (int i = 0; i < n; i++) {
        f32 x = 20 + i * cw, y = 330;
        col_t c = jx_col(i);
        gx_rrect_fill(x + 6, y, cw - 12, 96, 12, RGBA(0, 0, 0, 110), RGBA(0, 0, 0, 140));
        jx_name(i, nm, sizeof(nm));
        gx_text_c(F_MED, x + cw / 2, y + 26, nm, c);
        for (int k = 0; k < DU_WIN; k++) {
            f32 dx = x + cw / 2 + (k - (DU_WIN - 1) / 2.0f) * 22;
            if (k < du_score[i]) gx_circle(dx, y + 48, 8, c, RGBA(c.r / 2, c.g / 2, c.b / 2, 255));
            else gx_circle(dx, y + 48, 8, RGBA(60, 50, 80, 255), RGBA(40, 30, 60, 255));
        }
        if (du_best[i] >= 0) { snprintf(t, sizeof(t), "meilleur : %d ms", du_best[i]); gx_text_c(F_SMALL, x + cw / 2, y + 80, t, RGBA(220, 210, 255, 230)); }
        else if (jx_is_cpu(i)) gx_text_c(F_SMALL, x + cw / 2, y + 80, "200 \xC3\xA0 400 ms", RGBA(180, 170, 210, 200));
    }
    if (ar_phase) {
        int val[5];
        for (int i = 0; i < n; i++) val[i] = du_score[i];
        jx_over("Partie finie !", val, "points", 0);
    }
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "Au signal FEU !, appuie sur A le premier (trop t\xC3\xB4t : -1)     START : sortir");
}

/* ======================= Course folle ======================= */
#define RC_STEPS 60
static int ru_phase = 0;                   /* 0 compte a rebours, 1 course, 2 fin */
static u64 ru_t0 = 0, ru_cpu_next = 0, ru_first = 0;
static int ru_pos[5], ru_time[5], ru_left;
static f32 ru_draw[5], ru_kick[5];
static void race_new(void) {
    jx_players(1);
    for (int i = 0; i < 5; i++) { ru_pos[i] = 0; ru_time[i] = -1; ru_draw[i] = 0; ru_kick[i] = 0; }
    ru_phase = 0; ru_t0 = now_ms() + 3000; ru_cpu_next = 0; ru_first = 0;
    ru_left = jx_n();
    ar_phase = 0; ar_new_record = 0;
    ar_record = rg_record_get("jeu:course");                /* (garde 60000 - meilleur temps en ms) */
}
static void race_start(void) { fun_enter(FUN_RACE); ar_music(); race_new(); }
static void ru_step(int i, u64 now) {
    if (ru_time[i] >= 0) return;
    ru_pos[i]++; ru_kick[i] = 1;
    if (ru_pos[i] >= RC_STEPS) {
        ru_time[i] = (int)(now - ru_t0);
        if (!ru_first) ru_first = now;
        if (!jx_is_cpu(i)) rumble(jx_ch[i], 150);
        if (jx_np == 1 && !jx_is_cpu(i) && ru_time[i] < 60000) {
            ar_score = 60000 - ru_time[i];
            if (ar_score > ar_record) { ar_record = ar_score; ar_new_record = 1; if (sd_ok) rg_record_set("jeu:course", ar_score); }
        }
        if (--ru_left <= 0 || (jx_cpu && !jx_is_cpu(i)) || (jx_cpu && ru_time[0] >= 0)) { ru_phase = 2; ar_phase = 1; }
    }
}
static void race_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    if (ar_beat()) ar_flash = 1;
    ar_flash *= expf(-ui_dt * 6);
    for (int i = 0; i < 5; i++) { ru_draw[i] += ((f32)ru_pos[i] - ru_draw[i]) * (1 - expf(-ui_dt * 14)); ru_kick[i] *= expf(-ui_dt * 10); }
    if (ar_phase) return;
    if (ru_phase == 0) { if (now >= ru_t0) ru_phase = 1; else return; }
    if (ru_first && now - ru_first > 8000) { ru_phase = 2; ar_phase = 1; return; }   /* (8 s apres le premier : fini) */
    for (int i = 0; i < jx_np; i++) if (jx_down(i) & PAD_BUTTON_A) ru_step(i, now);
    if (jx_cpu && ru_phase == 1 && now >= ru_cpu_next) {    /* la console : 7 a 9 appuis par seconde */
        ru_step(jx_np, now);
        ru_cpu_next = now + 105 + lib_rand() % 40;
    }
}
static void race_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) race_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) fun_leave();
}
static void draw_race(void) {
    char t[96], nm[32];
    u64 now = now_ms();
    fun_frame_begin(200, 1.0f + ar_flash * 0.5f);
    fun_spectrum(480, 40, pt_hue, 45);
    gx_text(F_SMALL, 24, 30, "COURSE FOLLE", RGBA(255, 210, 110, 255), RGBA(255, 160, 60, 255), 3);
    if (ar_record > 0) { int ms = 60000 - ar_record; snprintf(t, sizeof(t), "Meilleur temps %d,%d s", ms / 1000, ms % 1000 / 100); gx_text_r(F_SMALL, 616, 30, t, RGBA(220, 210, 255, 230)); }
    if (ru_phase >= 1) { int ms = (int)(now - ru_t0); if (ru_phase == 2 || ar_phase) ms = 0; if (ms > 0) { snprintf(t, sizeof(t), "%d,%d s", ms / 1000, ms % 1000 / 100); gx_text_c(F_MED, 320, 56, t, RGBA(255, 230, 150, 255)); } }
    int n = jx_n();
    const f32 x0 = 120, x1 = 580, ytop = 92, lh = n <= 2 ? 120 : n == 3 ? 92 : 72;
    /* arrivee en damier */
    for (int k = 0; k < (int)(lh * n / 10); k++) gx_rect(x1 + ((k & 1) ? 6 : 0), ytop + k * 10, 6, 10, RGBA(240, 240, 240, 230));
    for (int i = 0; i < n; i++) {
        f32 y = ytop + i * lh, cy = y + lh / 2;
        col_t c = jx_col(i);
        gx_rrect_fill(20, y + 4, 600, lh - 8, 10, RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 120));
        jx_name(i, nm, sizeof(nm));
        gx_text1(F_MED, 32, cy + 6, nm, c);
        f32 fx = x0 + (x1 - x0 - 20) * (ru_draw[i] / RC_STEPS);
        gx_rect(x0, cy - 1, fx - x0, 2, RGBA(c.r, c.g, c.b, 90));                  /* trace */
        f32 bob = ru_kick[i] * 6;
        gx_circle_glow(fx, cy - bob, 18, RGBA(c.r, c.g, c.b, 90), 4);
        gx_circle(fx, cy - bob, 15, c, RGBA(c.r / 2, c.g / 2, c.b / 2, 255));
        if (ru_time[i] >= 0) { snprintf(t, sizeof(t), "%d,%d s", ru_time[i] / 1000, ru_time[i] % 1000 / 100); gx_text_r(F_MED, x1 - 30, cy + 6, t, RGBA(255, 255, 255, 255)); }
    }
    if (ru_phase == 0 && !ar_phase) {
        int s = (int)((ru_t0 - now + 999) / 1000);
        snprintf(t, sizeof(t), "%d", s < 1 ? 1 : s);
        fun_shadow_text(F_LARGE, 320, 260, t, 3.0f, RGBA(255, 230, 120, 255));
    } else if (ru_phase == 1 && now - ru_t0 < 700) fun_shadow_text(F_LARGE, 320, 260, "PARTEZ !", 2.2f, RGBA(140, 255, 170, 255));
    if (ar_phase) {
        int val[5];
        for (int i = 0; i < n; i++) val[i] = ru_time[i];
        jx_over("Arriv\xC3\xA9" "e !", val, "", 1);
    }
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "Mart\xC3\xA8le A pour avancer !     START : sortir");
}

/* ======================= Chasse aux bulles ======================= */
#define BU_MAX 16
#define BU_TIME 45000
typedef struct { f32 x, y, r, vy, life; u8 on, gold; f32 pop; } bubble_t;
static bubble_t bu[BU_MAX];
static f32 bu_cx[4], bu_cy[4];
static int bu_score[5], bu_phase = 0;
static u64 bu_t0 = 0, bu_next = 0;
static u64 bu_cpu_at = 0;                  /* (la console eclate a ce moment : temps de reaction) */
static void bu_spawn(void) {
    for (int i = 0; i < BU_MAX; i++) if (!bu[i].on && bu[i].pop <= 0) {
        bubble_t *b = &bu[i];
        b->on = 1; b->pop = 0;
        b->gold = lib_rand() % 9 == 0;
        b->r = b->gold ? 18 : 20 + lib_rand() % 16;
        b->x = 50 + lib_rand() % 540; b->y = 150 + lib_rand() % 270;
        b->vy = 18 + lib_rand() % 30;
        b->life = 3.5f + (lib_rand() % 25) / 10.0f;
        return;
    }
}
static void bubbles_new(void) {
    jx_players(1);
    memset(bu, 0, sizeof(bu)); memset(bu_score, 0, sizeof(bu_score));
    for (int i = 0; i < 4; i++) { bu_cx[i] = 160 + i * 110; bu_cy[i] = 300; }
    bu_phase = 0; bu_t0 = now_ms() + 3000; bu_next = 0;
    ar_phase = 0; ar_new_record = 0; ar_score = 0;
    ar_record = rg_record_get("jeu:bulles");
}
static void bubbles_start(void) { fun_enter(FUN_BUBBLES); ar_music(); bubbles_new(); }
static int bu_try_pop(int who, f32 x, f32 y) {
    int best = -1; f32 bd = 1e9f;
    for (int i = 0; i < BU_MAX; i++) {
        if (!bu[i].on) continue;
        f32 dx = bu[i].x - x, dy = bu[i].y - y, d = dx * dx + dy * dy;
        if (d < (bu[i].r + 6) * (bu[i].r + 6) && d < bd) { bd = d; best = i; }
    }
    if (best < 0) return 0;
    bu[best].on = 0; bu[best].pop = 1;
    bu_score[who] += bu[best].gold ? 3 : 1;
    if (!jx_is_cpu(who)) rumble(jx_ch[who], bu[best].gold ? 120 : 40);
    return 1;
}
static void bubbles_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    f32 dt = ui_dt > 0.05f ? 0.05f : ui_dt;
    int beat = ar_beat();
    if (beat) ar_flash = 1;
    ar_flash *= expf(-dt * 6);
    for (int i = 0; i < BU_MAX; i++) if (bu[i].pop > 0) bu[i].pop -= dt * 3;
    if (ar_phase) return;
    if (bu_phase == 0) { if (now >= bu_t0) bu_phase = 1; else return; }
    if (now - bu_t0 >= BU_TIME) {
        bu_phase = 2; ar_phase = 1;
        if (jx_np == 1) { ar_score = bu_score[0]; ar_game_over("jeu:bulles"); ar_phase = 1; }
        return;
    }
    int alive = 0;
    for (int i = 0; i < BU_MAX; i++) if (bu[i].on) {
        bu[i].y -= bu[i].vy * dt; bu[i].life -= dt;
        if (bu[i].life <= 0 || bu[i].y < 90) { bu[i].on = 0; continue; }
        alive++;
    }
    if (alive < 9 && (now >= bu_next || beat)) { bu_spawn(); bu_next = now + 350 + lib_rand() % 300; }
    for (int i = 0; i < jx_np; i++) {
        int c = jx_ch[i];
        u32 h = PAD_ButtonsHeld(c);
        f32 sx = PAD_StickX(c), sy = PAD_StickY(c);
        f32 vx = fabsf(sx) > 12 ? sx / 80.0f : 0, vy = fabsf(sy) > 12 ? -sy / 80.0f : 0;
        if (h & PAD_BUTTON_LEFT) vx = -1; if (h & PAD_BUTTON_RIGHT) vx = 1;
        if (h & PAD_BUTTON_UP) vy = -1; if (h & PAD_BUTTON_DOWN) vy = 1;
        bu_cx[i] += vx * 420 * dt; bu_cy[i] += vy * 420 * dt;
        if (bu_cx[i] < 20) bu_cx[i] = 20; if (bu_cx[i] > 620) bu_cx[i] = 620;
        if (bu_cy[i] < 90) bu_cy[i] = 90; if (bu_cy[i] > 440) bu_cy[i] = 440;
        if (jx_down(i) & PAD_BUTTON_A) bu_try_pop(i, bu_cx[i], bu_cy[i]);
    }
    if (jx_cpu) {                                           /* la console : garde une bulle en vue, un peu lentement */
        static int tgt = -1;
        int k = jx_np;
        if (tgt < 0 || !bu[tgt].on) {
            tgt = -1; f32 bd = 1e9f;
            for (int i = 0; i < BU_MAX; i++) if (bu[i].on) { f32 dx = bu[i].x - bu_cx[k], dy = bu[i].y - bu_cy[k], d = dx * dx + dy * dy; if (d < bd) { bd = d; tgt = i; } }
            bu_cpu_at = 0;
        }
        if (tgt >= 0) {
            f32 dx = bu[tgt].x - bu_cx[k], dy = bu[tgt].y - bu_cy[k], d = sqrtf(dx * dx + dy * dy), sp = 125 * dt;
            if (d > sp) { bu_cx[k] += dx / d * sp; bu_cy[k] += dy / d * sp; }
            if (d < bu[tgt].r * 0.8f) {
                if (!bu_cpu_at) bu_cpu_at = now + 650 + lib_rand() % 450;
                else if (now >= bu_cpu_at) { bu_try_pop(k, bu_cx[k], bu_cy[k]); bu_cpu_at = 0; tgt = -1; }
            }
        }
    }
}
static void bubbles_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) bubbles_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) fun_leave();
}
static void draw_bubbles(void) {
    char t[96], nm[32];
    u64 now = now_ms();
    fun_frame_begin(190, 1.0f + ar_flash * 0.5f);
    fun_spectrum(480, 50, pt_hue, 50);
    gx_text(F_SMALL, 24, 30, "CHASSE AUX BULLES", RGBA(255, 210, 110, 255), RGBA(255, 160, 60, 255), 3);
    if (ar_record > 0) { snprintf(t, sizeof(t), "Record %d", ar_record); gx_text_r(F_SMALL, 616, 30, t, RGBA(220, 210, 255, 230)); }
    if (bu_phase == 1) { int s = (int)((BU_TIME - (now - bu_t0) + 999) / 1000); snprintf(t, sizeof(t), "%d s", s); gx_text_c(F_LARGE, 320, 60, t, s <= 10 ? RGBA(255, 140, 140, 255) : RGBA(255, 230, 150, 255)); }
    for (int i = 0; i < BU_MAX && !ar_phase; i++) {   /* (fin : plus de bulles, le classement se lit) */
        bubble_t *b = &bu[i];
        if (b->on) {
            col_t c = b->gold ? RGBA(255, 215, 90, 200) : RGBA(150, 220, 255, 150);
            gx_circle_glow(b->x, b->y, b->r + 3, RGBA(c.r, c.g, c.b, 60 + (u8)(ar_flash * 60)), 3);
            gx_circle(b->x, b->y, b->r, RGBA(c.r, c.g, c.b, 60), c);
            gx_circle(b->x - b->r * 0.35f, b->y - b->r * 0.35f, b->r * 0.22f, RGBA(255, 255, 255, 200), RGBA(255, 255, 255, 60));
        } else if (b->pop > 0) {                                /* eclat */
            for (int k = 0; k < 8; k++) {
                f32 a = k * 0.785f, d = b->r + (1 - b->pop) * 22;
                gx_circle(b->x + cosf(a) * d, b->y + sinf(a) * d, 3, RGBA(255, 255, 255, (u8)(b->pop * 220)), RGBA(255, 255, 255, 0));
            }
        }
    }
    int n = jx_n();
    for (int i = 0; i < n && !ar_phase; i++) {              /* viseurs */
        col_t c = jx_col(i);
        f32 x = bu_cx[i], y = bu_cy[i];
        gx_rrect_stroke(x - 13, y - 13, 26, 26, 13, 2.5f, c);
        gx_rect(x - 1, y - 18, 2, 8, c); gx_rect(x - 1, y + 10, 2, 8, c); gx_rect(x - 18, y - 1, 8, 2, c); gx_rect(x + 10, y - 1, 8, 2, c);
    }
    for (int i = 0; i < n; i++) {                           /* scores */
        jx_name(i, nm, sizeof(nm));
        snprintf(t, sizeof(t), "%s  %d", nm, bu_score[i]);
        f32 x = 320 + (i - (n - 1) / 2.0f) * 150;
        gx_text_c(F_MED, x, 452, t, jx_col(i));
    }
    if (bu_phase == 0 && !ar_phase) {
        int s = (int)((bu_t0 - now + 999) / 1000);
        snprintf(t, sizeof(t), "%d", s < 1 ? 1 : s);
        fun_shadow_text(F_LARGE, 320, 260, t, 3.0f, RGBA(255, 230, 120, 255));
    }
    if (ar_phase) {
        int val[5];
        for (int i = 0; i < n; i++) val[i] = bu_score[i];
        jx_over("Temps \xC3\xA9" "coul\xC3\xA9 !", val, "bulles", 0);
    }
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "Stick : viser     A : \xC3\xA9" "clater (dor\xC3\xA9" "e = 3)     START : sortir");
}

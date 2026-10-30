// GC Station v279 : jeux en duel (2 à 4 joueurs ; seul : contre la console). Inclus dans main.c après fun_party.c.
// Chaque joueur : une manette GameCube, ou un téléphone (appli > Plus > Mini-jeux : il devient la manette du joueur
// choisi, UDP 51238, pp_pump) ; les deux s'additionnent sur un même port.
//  - Motos lumineuses : chacun laisse un mur derrière lui ; le dernier en vie gagne la manche (en 3).
//  - Air hockey : un palet, deux maillets au stick ; le premier à 7 buts.
//  - Puissance 4 : aligner 4 jetons (ligne, colonne, diagonale) ; en 3 parties gagnées.
//  - Sumo : pousser les autres hors du ring ; A = élan (toutes les 1,2 s) ; le dernier dedans gagne la manche (en 3).

/* ---------- commun ---------- */
static u32 jd_held(int i) { return jx_is_cpu(i) ? 0 : PAD_ButtonsHeld(jx_ch[i]); }
static void jd_dir(int i, f32 *dx, f32 *dy) {           /* croix ou stick (-1..1) */
    *dx = *dy = 0;
    if (jx_is_cpu(i)) return;
    int c = jx_ch[i];
    u32 h = PAD_ButtonsHeld(c);
    f32 sx = PAD_StickX(c) / 80.0f, sy = -PAD_StickY(c) / 80.0f;
    if (sx > 1) sx = 1; if (sx < -1) sx = -1; if (sy > 1) sy = 1; if (sy < -1) sy = -1;
    if (fabsf(sx) > 0.2f || fabsf(sy) > 0.2f) { *dx = sx; *dy = sy; }
    if (h & PAD_BUTTON_LEFT) *dx = -1; if (h & PAD_BUTTON_RIGHT) *dx = 1;
    if (h & PAD_BUTTON_UP) *dy = -1; if (h & PAD_BUTTON_DOWN) *dy = 1;
}
static void jd_countdown(u64 t0, u64 now) {
    if (now >= t0) { if (now - t0 < 600) fun_shadow_text(F_LARGE, 320, 260, "PARTEZ !", 2.2f, RGBA(140, 255, 170, 255)); return; }
    char t[8]; int s = (int)((t0 - now + 999) / 1000);
    snprintf(t, sizeof(t), "%d", s < 1 ? 1 : s);
    fun_shadow_text(F_LARGE, 320, 260, t, 3.0f, RGBA(255, 230, 120, 255));
}
static void jd_scores(const int *sc, int n, int goal) {  /* pastilles des joueurs en haut */
    char nm[32], t[48];
    f32 w = 600.0f / n;
    for (int i = 0; i < n; i++) {
        col_t c = jx_col(i);
        f32 x = 20 + i * w;
        gx_rrect_fill(x + 4, 38, w - 8, 26, 10, RGBA(c.r / 4, c.g / 4, c.b / 4, 255), RGBA(c.r / 6, c.g / 6, c.b / 6, 255));
        gx_rrect_stroke(x + 4, 38, w - 8, 26, 10, 1.5f, c);
        jx_name(i, nm, sizeof(nm));
        snprintf(t, sizeof(t), "%s  %d / %d", nm, sc[i], goal);
        gx_text_c(F_SMALL, x + w / 2, 56, t, RGBA(255, 255, 255, 255));
    }
}

/* ======================= Motos lumineuses ======================= */
#define TR_W 80
#define TR_H 52
#define TR_CELL 7
#define TR_X0 40
#define TR_Y0 78
#define TR_WIN 3
static u8 tr_g[TR_H][TR_W];
static int tr_x[5], tr_y[5], tr_dx[5], tr_dy[5], tr_ndx[5], tr_ndy[5], tr_alive[5], tr_sc[5], tr_phase, tr_win;
static u64 tr_t0, tr_step_t;
static f32 tr_boom[5];
static void tr_round(void) {
    memset(tr_g, 0, sizeof(tr_g));
    int n = jx_n();
    static const int sx[4] = { 10, TR_W - 11, 10, TR_W - 11 }, sy[4] = { 10, TR_H - 11, TR_H - 11, 10 }, dx[4] = { 1, -1, 1, -1 };
    for (int i = 0; i < n; i++) {
        int k = n == 2 ? i : i;
        tr_x[i] = n == 2 ? (i ? TR_W - 11 : 10) : sx[k]; tr_y[i] = n == 2 ? TR_H / 2 : sy[k];
        tr_dx[i] = tr_ndx[i] = n == 2 ? (i ? -1 : 1) : dx[k]; tr_dy[i] = tr_ndy[i] = 0;
        tr_alive[i] = 1; tr_boom[i] = 0;
        tr_g[tr_y[i]][tr_x[i]] = (u8)(i + 1);
    }
    tr_phase = 0; tr_t0 = now_ms() + 2500; tr_step_t = tr_t0; tr_win = -1;
}
static void tron_new(void) { jx_players(1); memset(tr_sc, 0, sizeof(tr_sc)); ar_phase = 0; ar_new_record = 0; tr_round(); }
static void tron_start(void) { fun_enter(FUN_TRON); ar_music(); tron_new(); }
static int tr_free(int x, int y) { return x >= 0 && y >= 0 && x < TR_W && y < TR_H && !tr_g[y][x]; }
static int tr_run(int x, int y, int dx, int dy) { int n = 0; while (n < 30 && tr_free(x + dx * (n + 1), y + dy * (n + 1))) n++; return n; }
static void tr_cpu(int i) {                             /* la console : tout droit, sinon le côté le plus libre */
    int ax = tr_dx[i], ay = tr_dy[i];
    int f = tr_run(tr_x[i], tr_y[i], ax, ay), l = tr_run(tr_x[i], tr_y[i], ay, -ax), r = tr_run(tr_x[i], tr_y[i], -ay, ax);
    if (f < 3 || (lib_rand() % 40 == 0 && (l > f || r > f))) {
        if (l >= r && l > 0) { tr_ndx[i] = ay; tr_ndy[i] = -ax; } else if (r > 0) { tr_ndx[i] = -ay; tr_ndy[i] = ax; }
    }
}
static void tron_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    for (int i = 0; i < 5; i++) tr_boom[i] *= expf(-ui_dt * 3);
    if (ar_phase) return;
    int n = jx_n();
    for (int i = 0; i < jx_np; i++) {                   /* virage demandé (pas de demi-tour) */
        f32 dx, dy; jd_dir(i, &dx, &dy);
        int ix = dx > 0.5f ? 1 : dx < -0.5f ? -1 : 0, iy = dy > 0.5f ? 1 : dy < -0.5f ? -1 : 0;
        if (ix && iy) { if (fabsf(dx) > fabsf(dy)) iy = 0; else ix = 0; }
        if ((ix || iy) && !(ix == -tr_dx[i] && iy == -tr_dy[i])) { tr_ndx[i] = ix; tr_ndy[i] = iy; }
    }
    if (tr_phase == 2) { if (now - tr_step_t > 1800) { if (tr_win >= 0 && tr_sc[tr_win] >= TR_WIN) ar_phase = 1; else tr_round(); } return; }
    if (now < tr_t0) return;
    tr_phase = 1;
    u32 step = 62 - (u32)((now - tr_t0) / 2500 > 20 ? 20 : (now - tr_t0) / 2500);   /* (de plus en plus vite) */
    if (now < tr_step_t) return;
    tr_step_t = now + step;
    for (int i = jx_np; i < n; i++) if (tr_alive[i]) tr_cpu(i);
    int nx[5], ny[5];
    for (int i = 0; i < n; i++) if (tr_alive[i]) { tr_dx[i] = tr_ndx[i]; tr_dy[i] = tr_ndy[i]; nx[i] = tr_x[i] + tr_dx[i]; ny[i] = tr_y[i] + tr_dy[i]; }
    for (int i = 0; i < n; i++) {
        if (!tr_alive[i]) continue;
        int dead = !tr_free(nx[i], ny[i]);
        for (int j = 0; j < n; j++) if (j != i && tr_alive[j] && nx[j] == nx[i] && ny[j] == ny[i]) dead = 1;   /* (face à face) */
        if (dead) { tr_alive[i] = 2; tr_boom[i] = 1; if (!jx_is_cpu(i)) rumble(jx_ch[i], 400); }
    }
    int alive = 0, last = -1;
    for (int i = 0; i < n; i++) {
        if (tr_alive[i] == 2) tr_alive[i] = 0;
        if (tr_alive[i]) { tr_x[i] = nx[i]; tr_y[i] = ny[i]; tr_g[ny[i]][nx[i]] = (u8)(i + 1); alive++; last = i; }
    }
    if (alive <= 1) { tr_win = last; if (last >= 0) tr_sc[last]++; tr_phase = 2; tr_step_t = now; }
}
static void tron_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) tron_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) fun_leave();
}
static void draw_tron(void) {
    u64 now = now_ms();
    fun_frame_begin(250, 0.7f);
    gx_text(F_SMALL, 24, 28, "MOTOS LUMINEUSES", RGBA(120, 230, 255, 255), RGBA(60, 140, 255, 255), 3);
    int n = jx_n();
    jd_scores(tr_sc, n, TR_WIN);
    gx_rect(TR_X0 - 3, TR_Y0 - 3, TR_W * TR_CELL + 6, TR_H * TR_CELL + 6, RGBA(80, 200, 255, 160));
    gx_rect(TR_X0, TR_Y0, TR_W * TR_CELL, TR_H * TR_CELL, RGBA(6, 8, 22, 255));
    for (int x = 0; x <= TR_W; x += 8) gx_rect(TR_X0 + x * TR_CELL, TR_Y0, 1, TR_H * TR_CELL, RGBA(30, 60, 110, 120));
    for (int y = 0; y <= TR_H; y += 8) gx_rect(TR_X0, TR_Y0 + y * TR_CELL, TR_W * TR_CELL, 1, RGBA(30, 60, 110, 120));
    for (int y = 0; y < TR_H; y++)
        for (int x = 0; x < TR_W; x++) {
            u8 v = tr_g[y][x];
            if (!v) continue;
            col_t c = jx_col(v - 1);
            gx_rect(TR_X0 + x * TR_CELL, TR_Y0 + y * TR_CELL, TR_CELL, TR_CELL, RGBA(c.r * 3 / 4, c.g * 3 / 4, c.b * 3 / 4, 255));
        }
    for (int i = 0; i < n; i++) {
        col_t c = jx_col(i);
        f32 px = TR_X0 + tr_x[i] * TR_CELL + TR_CELL / 2.0f, py = TR_Y0 + tr_y[i] * TR_CELL + TR_CELL / 2.0f;
        if (tr_alive[i]) { gx_circle_glow(px, py, 9, RGBA(c.r, c.g, c.b, 140), 3); gx_circle(px, py, 5, RGBA(255, 255, 255, 255), c); }
        if (tr_boom[i] > 0.02f) gx_ring(px, py, 10 + (1 - tr_boom[i]) * 40, 13 + (1 - tr_boom[i]) * 40, RGBA(255, 220, 120, (u8)(255 * tr_boom[i])));
    }
    if (!ar_phase && tr_phase == 0) jd_countdown(tr_t0, now);
    if (tr_phase == 2 && !ar_phase) {
        char nm[32], t[64];
        if (tr_win >= 0) { jx_name(tr_win, nm, sizeof(nm)); snprintf(t, sizeof(t), "%s gagne la manche", nm); }
        else snprintf(t, sizeof(t), "Égalité !");
        fun_shadow_text(F_LARGE, 320, 260, t, 1.5f, tr_win >= 0 ? jx_col(tr_win) : RGBA(255, 255, 255, 255));
    }
    if (ar_phase) jx_over("Motos lumineuses", tr_sc, "manches", 0);
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "Croix ou stick : tourner (pas de demi-tour)     START : sortir");
}

/* ======================= Air hockey ======================= */
#define HK_X0 50.0f
#define HK_Y0 82.0f
#define HK_X1 590.0f
#define HK_Y1 412.0f
#define HK_GOAL 60.0f
#define HK_WIN 7
static f32 hk_px, hk_py, hk_vx, hk_vy, hk_mx[2], hk_my[2], hk_mvx[2], hk_mvy[2], hk_flash;
static int hk_sc[5], hk_last;
static u64 hk_t0, hk_goal_t;
static void hk_serve(int to) {
    hk_px = (HK_X0 + HK_X1) / 2 + (to == 0 ? -60 : to == 1 ? 60 : 0); hk_py = (HK_Y0 + HK_Y1) / 2; hk_vx = hk_vy = 0;
    hk_mx[0] = HK_X0 + 50; hk_mx[1] = HK_X1 - 50; hk_my[0] = hk_my[1] = (HK_Y0 + HK_Y1) / 2;
    hk_t0 = now_ms() + 1500; hk_goal_t = 0;
}
static void hockey_new(void) { jx_players(1); if (jx_np > 2) jx_np = 2; memset(hk_sc, 0, sizeof(hk_sc)); ar_phase = 0; ar_new_record = 0; hk_last = -1; hk_serve(-1); }
static void hockey_start(void) { fun_enter(FUN_HOCKEY); ar_music(); hockey_new(); }
static void hockey_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    hk_flash *= expf(-ui_dt * 4);
    if (ar_phase) return;
    f32 dt = ui_dt > 0.05f ? 0.05f : ui_dt;
    for (int p = 0; p < 2; p++) {                       /* maillets : au stick, chacun dans sa moitié */
        f32 dx = 0, dy = 0, ox = hk_mx[p], oy = hk_my[p];
        if (!jx_is_cpu(p)) jd_dir(p, &dx, &dy);
        else {                                         /* la console : vers le palet s'il est chez elle, sinon devant son but */
            f32 tx = hk_px > (HK_X0 + HK_X1) / 2 ? hk_px + 14 : HK_X1 - 50, ty = hk_px > (HK_X0 + HK_X1) / 2 ? hk_py : (HK_Y0 + HK_Y1) / 2 + (hk_py - (HK_Y0 + HK_Y1) / 2) * 0.5f;
            dx = (tx - hk_mx[p]) / 40; dy = (ty - hk_my[p]) / 40;
            f32 m = sqrtf(dx * dx + dy * dy); if (m > 0.85f) { dx *= 0.85f / m; dy *= 0.85f / m; }
        }
        hk_mx[p] += dx * 420 * dt; hk_my[p] += dy * 420 * dt;
        f32 mid = (HK_X0 + HK_X1) / 2;
        if (p == 0) { if (hk_mx[p] < HK_X0 + 22) hk_mx[p] = HK_X0 + 22; if (hk_mx[p] > mid - 22) hk_mx[p] = mid - 22; }
        else { if (hk_mx[p] > HK_X1 - 22) hk_mx[p] = HK_X1 - 22; if (hk_mx[p] < mid + 22) hk_mx[p] = mid + 22; }
        if (hk_my[p] < HK_Y0 + 22) hk_my[p] = HK_Y0 + 22; if (hk_my[p] > HK_Y1 - 22) hk_my[p] = HK_Y1 - 22;
        hk_mvx[p] = (hk_mx[p] - ox) / (dt > 0 ? dt : 0.016f); hk_mvy[p] = (hk_my[p] - oy) / (dt > 0 ? dt : 0.016f);
    }
    if (hk_goal_t) { if (now - hk_goal_t > 1400) { if (hk_last >= 0 && hk_sc[hk_last] >= HK_WIN) ar_phase = 1; else hk_serve(hk_last == 0 ? 1 : 0); } return; }
    if (now < hk_t0) return;
    hk_px += hk_vx * dt; hk_py += hk_vy * dt;
    f32 fr = expf(-dt * 0.35f); hk_vx *= fr; hk_vy *= fr;
    if (hk_py < HK_Y0 + 12) { hk_py = HK_Y0 + 12; hk_vy = fabsf(hk_vy); }
    if (hk_py > HK_Y1 - 12) { hk_py = HK_Y1 - 12; hk_vy = -fabsf(hk_vy); }
    f32 cy = (HK_Y0 + HK_Y1) / 2;
    int in_goal = fabsf(hk_py - cy) < HK_GOAL;
    if (hk_px < HK_X0 + 12) { if (in_goal && hk_px < HK_X0 - 6) { hk_sc[1]++; hk_last = 1; hk_goal_t = now; hk_flash = 1; if (!jx_is_cpu(0)) rumble(jx_ch[0], 300); return; } if (!in_goal) { hk_px = HK_X0 + 12; hk_vx = fabsf(hk_vx); } }
    if (hk_px > HK_X1 - 12) { if (in_goal && hk_px > HK_X1 + 6) { hk_sc[0]++; hk_last = 0; hk_goal_t = now; hk_flash = 1; if (!jx_is_cpu(1)) rumble(jx_ch[1], 300); return; } if (!in_goal) { hk_px = HK_X1 - 12; hk_vx = -fabsf(hk_vx); } }
    for (int p = 0; p < 2; p++) {                       /* choc maillet / palet */
        f32 dx = hk_px - hk_mx[p], dy = hk_py - hk_my[p], d = sqrtf(dx * dx + dy * dy);
        if (d < 34 && d > 0.01f) {
            f32 nx = dx / d, ny = dy / d;
            hk_px = hk_mx[p] + nx * 34; hk_py = hk_my[p] + ny * 34;
            f32 rv = (hk_vx - hk_mvx[p]) * nx + (hk_vy - hk_mvy[p]) * ny;
            if (rv < 0) { hk_vx -= 1.9f * rv * nx; hk_vy -= 1.9f * rv * ny; }
            f32 sp = sqrtf(hk_vx * hk_vx + hk_vy * hk_vy);
            if (sp < 220) { hk_vx += nx * 220; hk_vy += ny * 220; }
            if (sp > 900) { hk_vx *= 900 / sp; hk_vy *= 900 / sp; }
            if (!jx_is_cpu(p)) rumble(jx_ch[p], 40);
        }
    }
}
static void hockey_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) hockey_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) fun_leave();
}
static void draw_hockey(void) {
    u64 now = now_ms();
    fun_frame_begin(200, 0.6f);
    gx_text(F_SMALL, 24, 28, "AIR HOCKEY", RGBA(140, 230, 255, 255), RGBA(90, 160, 255, 255), 3);
    jd_scores(hk_sc, 2, HK_WIN);
    f32 cy = (HK_Y0 + HK_Y1) / 2, mid = (HK_X0 + HK_X1) / 2;
    gx_rrect_fill(HK_X0 - 8, HK_Y0 - 8, HK_X1 - HK_X0 + 16, HK_Y1 - HK_Y0 + 16, 26, RGBA(40, 60, 120, 255), RGBA(20, 30, 70, 255));
    gx_rrect_fill(HK_X0, HK_Y0, HK_X1 - HK_X0, HK_Y1 - HK_Y0, 20, RGBA(214, 232, 250, 255), RGBA(178, 204, 236, 255));
    gx_rect(mid - 1.5f, HK_Y0, 3, HK_Y1 - HK_Y0, RGBA(220, 60, 90, 200));
    gx_ring(mid, cy, 44, 47, RGBA(220, 60, 90, 200));
    gx_rect(HK_X0 - 8, cy - HK_GOAL, 10, HK_GOAL * 2, RGBA(20, 20, 40, 255));
    gx_rect(HK_X1 - 2, cy - HK_GOAL, 10, HK_GOAL * 2, RGBA(20, 20, 40, 255));
    gx_ring(HK_X0, cy, HK_GOAL, HK_GOAL + 3, RGBA(90, 120, 220, 200));
    gx_ring(HK_X1, cy, HK_GOAL, HK_GOAL + 3, RGBA(90, 120, 220, 200));
    for (int p = 0; p < 2; p++) {
        col_t c = jx_col(p);
        gx_circle(hk_mx[p] + 3, hk_my[p] + 4, 22, RGBA(0, 0, 0, 70), RGBA(0, 0, 0, 0));
        gx_circle(hk_mx[p], hk_my[p], 22, RGBA(c.r, c.g, c.b, 255), RGBA(c.r / 2, c.g / 2, c.b / 2, 255));
        gx_circle(hk_mx[p], hk_my[p], 9, RGBA(255, 255, 255, 230), c);
    }
    gx_circle(hk_px + 2, hk_py + 3, 12, RGBA(0, 0, 0, 80), RGBA(0, 0, 0, 0));
    gx_circle(hk_px, hk_py, 12, RGBA(40, 40, 50, 255), RGBA(10, 10, 16, 255));
    gx_ring(hk_px, hk_py, 8, 10, RGBA(255, 210, 90, 255));
    if (hk_flash > 0.05f) gx_quad4(0, 0, 640, 480, RGBA(255, 230, 120, (u8)(90 * hk_flash)), RGBA(255, 230, 120, (u8)(90 * hk_flash)), RGBA(255, 230, 120, (u8)(90 * hk_flash)), RGBA(255, 230, 120, (u8)(90 * hk_flash)));
    if (hk_goal_t && !ar_phase) fun_shadow_text(F_LARGE, 320, 260, "BUT !", 2.4f, jx_col(hk_last >= 0 ? hk_last : 0));
    else if (!ar_phase && now < hk_t0) jd_countdown(hk_t0, now);
    if (ar_phase) jx_over("Air hockey", hk_sc, "buts", 0);
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "Stick ou croix : bouger le maillet     START : sortir");
}

/* ======================= Puissance 4 ======================= */
#define P4_C 7
#define P4_R 6
#define P4_WIN 3
static s8 p4_g[P4_R][P4_C];
static int p4_turn, p4_col, p4_sc[5], p4_win, p4_first;
static int p4_line[4][2];
static f32 p4_drop_y, p4_drop_to; static int p4_drop_c = -1, p4_drop_r, p4_drop_p;
static u64 p4_end_t, p4_cpu_t;
static void p4_round(void) {
    memset(p4_g, -1, sizeof(p4_g));
    p4_turn = p4_first; p4_col = 3; p4_win = -1; p4_end_t = 0; p4_drop_c = -1; p4_cpu_t = 0;
}
static void p4_new(void) { jx_players(1); if (jx_np > 2) jx_np = 2; memset(p4_sc, 0, sizeof(p4_sc)); p4_first = 0; ar_phase = 0; ar_new_record = 0; p4_round(); }
static void p4_start(void) { fun_enter(FUN_P4); ar_music(); p4_new(); }
static int p4_row(int c) { for (int r = P4_R - 1; r >= 0; r--) if (p4_g[r][c] < 0) return r; return -1; }
static int p4_four(int p, int save) {                   /* 4 alignés pour p ? */
    static const int d[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, -1 } };
    for (int r = 0; r < P4_R; r++) for (int c = 0; c < P4_C; c++) for (int k = 0; k < 4; k++) {
        int ok = 1;
        for (int i = 0; i < 4 && ok; i++) { int rr = r + d[k][0] * i, cc = c + d[k][1] * i; if (rr < 0 || rr >= P4_R || cc < 0 || cc >= P4_C || p4_g[rr][cc] != p) ok = 0; }
        if (ok) { if (save) for (int i = 0; i < 4; i++) { p4_line[i][0] = r + d[k][0] * i; p4_line[i][1] = c + d[k][1] * i; } return 1; }
    }
    return 0;
}
static int p4_cpu_pick(void) {                          /* gagner, sinon bloquer, sinon pas de cadeau, plutôt au milieu */
    for (int pass = 0; pass < 2; pass++) {
        int who = pass == 0 ? 1 : 0;
        for (int c = 0; c < P4_C; c++) { int r = p4_row(c); if (r < 0) continue; p4_g[r][c] = (s8)who; int w = p4_four(who, 0); p4_g[r][c] = -1; if (w) return c; }
    }
    static const int pref[7] = { 3, 2, 4, 1, 5, 0, 6 };
    int fallback = -1;
    for (int k = 0; k < 7; k++) {
        int c = pref[(k + (int)(lib_rand() % 2)) % 7], r = p4_row(c);
        if (r < 0) continue;
        if (fallback < 0) fallback = c;
        if (r > 0) { p4_g[r][c] = 1; p4_g[r - 1][c] = 0; int bad = p4_four(0, 0); p4_g[r - 1][c] = -1; p4_g[r][c] = -1; if (bad) continue; }
        return c;
    }
    return fallback;
}
static void p4_drop(int c) {
    int r = p4_row(c);
    if (r < 0) return;
    p4_drop_c = c; p4_drop_r = r; p4_drop_p = p4_turn; p4_drop_y = -1; p4_drop_to = (f32)r;
}
static void p4_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    if (ar_phase) return;
    if (p4_drop_c >= 0) {                               /* jeton qui tombe */
        p4_drop_y += ui_dt * 16;
        if (p4_drop_y >= p4_drop_to) {
            p4_g[p4_drop_r][p4_drop_c] = (s8)p4_drop_p;
            if (!jx_is_cpu(p4_drop_p)) rumble(jx_ch[p4_drop_p], 50);
            p4_drop_c = -1;
            if (p4_four(p4_drop_p, 1)) { p4_win = p4_drop_p; p4_sc[p4_drop_p]++; p4_end_t = now; }
            else {
                int full = 1; for (int c = 0; c < P4_C; c++) if (p4_row(c) >= 0) full = 0;
                if (full) { p4_win = 2; p4_end_t = now; }
                else { p4_turn ^= 1; p4_cpu_t = now + 600; }
            }
        }
        return;
    }
    if (p4_end_t) { if (now - p4_end_t > 2200) { if (p4_win < 2 && p4_sc[p4_win] >= P4_WIN) ar_phase = 1; else { p4_first ^= 1; p4_round(); } } return; }
    if (jx_is_cpu(p4_turn)) { if (now >= p4_cpu_t) { int c = p4_cpu_pick(); if (c >= 0) { p4_col = c; p4_drop(c); } } return; }
    u32 d = jx_down(p4_turn);
    static f32 rep = 0;
    f32 dx, dy; jd_dir(p4_turn, &dx, &dy);
    if (d & PAD_BUTTON_LEFT) { p4_col = (p4_col + P4_C - 1) % P4_C; rep = 0.35f; }
    else if (d & PAD_BUTTON_RIGHT) { p4_col = (p4_col + 1) % P4_C; rep = 0.35f; }
    else if (fabsf(dx) > 0.6f) { rep -= ui_dt; if (rep <= 0) { p4_col = (p4_col + (dx > 0 ? 1 : P4_C - 1)) % P4_C; rep = 0.18f; } }
    else rep = 0;
    if ((d & (PAD_BUTTON_A | PAD_BUTTON_DOWN)) && p4_row(p4_col) >= 0) p4_drop(p4_col);
}
static void p4_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) p4_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) fun_leave();
}
static void draw_p4(void) {
    u64 now = now_ms();
    fun_frame_begin(230, 0.6f);
    gx_text(F_SMALL, 24, 28, "PUISSANCE 4", RGBA(255, 220, 110, 255), RGBA(255, 150, 60, 255), 3);
    jd_scores(p4_sc, 2, P4_WIN);
    const f32 cs = 50, bx = 320 - cs * P4_C / 2, by = 118;
    gx_rrect_fill(bx - 12, by - 8, cs * P4_C + 24, cs * P4_R + 20, 18, RGBA(40, 70, 210, 255), RGBA(22, 40, 140, 255));
    for (int r = 0; r < P4_R; r++) for (int c = 0; c < P4_C; c++) {
        f32 x = bx + c * cs + cs / 2, y = by + r * cs + cs / 2;
        int v = p4_g[r][c];
        if (v < 0) { gx_circle(x, y, 20, RGBA(10, 14, 40, 255), RGBA(20, 26, 60, 255)); continue; }
        col_t col = jx_col(v);
        gx_circle(x, y, 20, RGBA(col.r, col.g, col.b, 255), RGBA(col.r / 2, col.g / 2, col.b / 2, 255));
        gx_ring(x, y, 12, 14, RGBA(255, 255, 255, 70));
    }
    if (p4_drop_c >= 0) {
        col_t col = jx_col(p4_drop_p);
        f32 x = bx + p4_drop_c * cs + cs / 2, y = by + p4_drop_y * cs + cs / 2;
        gx_circle(x, y, 20, col, RGBA(col.r / 2, col.g / 2, col.b / 2, 255));
    }
    if (p4_end_t && p4_win < 2) {                       /* les 4 alignés brillent */
        f32 pulse = 0.5f + 0.5f * sinf(now * 0.012f);
        for (int i = 0; i < 4; i++) gx_ring(bx + p4_line[i][1] * cs + cs / 2, by + p4_line[i][0] * cs + cs / 2, 20, 24, RGBA(255, 255, 255, (u8)(120 + 135 * pulse)));
    }
    if (!p4_end_t && p4_drop_c < 0 && !ar_phase) {      /* jeton en main au-dessus de la colonne */
        col_t col = jx_col(p4_turn);
        f32 x = bx + p4_col * cs + cs / 2;
        gx_circle_glow(x, by - 26, 22, RGBA(col.r, col.g, col.b, 110), 3);
        gx_circle(x, by - 26, 18, col, RGBA(col.r / 2, col.g / 2, col.b / 2, 255));
        char nm[32], t[64]; jx_name(p4_turn, nm, sizeof(nm)); snprintf(t, sizeof(t), "À %s", nm);
        gx_text_c(F_MED, 320, 446, t, col);
    }
    if (p4_end_t && !ar_phase) {
        char nm[32], t[64];
        if (p4_win < 2) { jx_name(p4_win, nm, sizeof(nm)); snprintf(t, sizeof(t), "%s gagne !", nm); } else snprintf(t, sizeof(t), "Grille pleine : égalité");
        fun_shadow_text(F_LARGE, 320, 280, t, 1.6f, p4_win < 2 ? jx_col(p4_win) : RGBA(255, 255, 255, 255));
    }
    if (ar_phase) jx_over("Puissance 4", p4_sc, "parties", 0);
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "Gauche / droite : colonne     A ou bas : lâcher le jeton     START : sortir");
}

/* ======================= Sumo ======================= */
#define SU_CX 320.0f
#define SU_CY 262.0f
#define SU_R 168.0f
#define SU_WIN 3
static f32 su_x[5], su_y[5], su_vx[5], su_vy[5], su_dash[5], su_fall[5];
static int su_in[5], su_sc[5], su_win;
static u64 su_t0, su_end_t;
static void su_round(void) {
    int n = jx_n();
    for (int i = 0; i < n; i++) {
        f32 a = (f32)i / n * 6.2832f + 3.1416f;
        su_x[i] = SU_CX + cosf(a) * 95; su_y[i] = SU_CY + sinf(a) * 95; su_vx[i] = su_vy[i] = 0; su_dash[i] = 0; su_in[i] = 1; su_fall[i] = 0;
    }
    su_t0 = now_ms() + 2500; su_end_t = 0; su_win = -1;
}
static void sumo_new(void) { jx_players(1); memset(su_sc, 0, sizeof(su_sc)); ar_phase = 0; ar_new_record = 0; su_round(); }
static void sumo_start(void) { fun_enter(FUN_SUMO); ar_music(); sumo_new(); }
static void sumo_pump(void) {
    u64 now = now_ms();
    last_input_ms = now;
    int n = jx_n();
    f32 dt = ui_dt > 0.05f ? 0.05f : ui_dt;
    for (int i = 0; i < n; i++) { if (su_dash[i] > 0) su_dash[i] -= dt; if (!su_in[i] && su_fall[i] < 1) su_fall[i] += dt * 2; }
    if (ar_phase) return;
    if (su_end_t) { if (now - su_end_t > 1800) { if (su_win >= 0 && su_sc[su_win] >= SU_WIN) ar_phase = 1; else su_round(); } return; }
    if (now < su_t0) return;
    for (int i = 0; i < n; i++) {
        if (!su_in[i]) continue;
        f32 dx = 0, dy = 0; int dash = 0;
        if (!jx_is_cpu(i)) { jd_dir(i, &dx, &dy); dash = (jx_down(i) & PAD_BUTTON_A) != 0; }
        else {                                         /* la console : vers le plus proche, recule du bord, élan de près */
            int t = -1; f32 bd = 1e9f;
            for (int j = 0; j < n; j++) if (j != i && su_in[j]) { f32 d = (su_x[j] - su_x[i]) * (su_x[j] - su_x[i]) + (su_y[j] - su_y[i]) * (su_y[j] - su_y[i]); if (d < bd) { bd = d; t = j; } }
            if (t >= 0) { dx = su_x[t] - su_x[i]; dy = su_y[t] - su_y[i]; }
            f32 ex = su_x[i] - SU_CX, ey = su_y[i] - SU_CY, er = sqrtf(ex * ex + ey * ey);
            if (er > SU_R * 0.72f) { dx -= ex * 2; dy -= ey * 2; }
            f32 m = sqrtf(dx * dx + dy * dy); if (m > 0.01f) { dx /= m; dy /= m; }
            dx *= 0.8f; dy *= 0.8f;
            dash = t >= 0 && bd < 70 * 70 && lib_rand() % 20 == 0;
        }
        su_vx[i] += dx * 520 * dt; su_vy[i] += dy * 520 * dt;
        if (dash && su_dash[i] <= 0) {
            f32 m = sqrtf(dx * dx + dy * dy); if (m < 0.1f) { dx = su_vx[i]; dy = su_vy[i]; m = sqrtf(dx * dx + dy * dy); }
            if (m > 0.01f) { su_vx[i] += dx / m * 340; su_vy[i] += dy / m * 340; }
            su_dash[i] = 1.2f;
            if (!jx_is_cpu(i)) rumble(jx_ch[i], 60);
        }
        f32 fr = expf(-dt * 2.2f); su_vx[i] *= fr; su_vy[i] *= fr;
    }
    for (int i = 0; i < n; i++) if (su_in[i]) { su_x[i] += su_vx[i] * dt; su_y[i] += su_vy[i] * dt; }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {   /* chocs entre lutteurs */
        if (!su_in[i] || !su_in[j]) continue;
        f32 dx = su_x[j] - su_x[i], dy = su_y[j] - su_y[i], d = sqrtf(dx * dx + dy * dy);
        if (d < 48 && d > 0.01f) {
            f32 nx = dx / d, ny = dy / d, push = (48 - d) / 2;
            su_x[i] -= nx * push; su_y[i] -= ny * push; su_x[j] += nx * push; su_y[j] += ny * push;
            f32 rv = (su_vx[j] - su_vx[i]) * nx + (su_vy[j] - su_vy[i]) * ny;
            if (rv < 0) { f32 k = -1.6f * rv / 2; su_vx[i] -= k * nx; su_vy[i] -= k * ny; su_vx[j] += k * nx; su_vy[j] += k * ny;
                if (rv < -200) { if (!jx_is_cpu(i)) rumble(jx_ch[i], 80); if (!jx_is_cpu(j)) rumble(jx_ch[j], 80); } }
        }
    }
    int alive = 0, last = -1;
    for (int i = 0; i < n; i++) {
        if (!su_in[i]) continue;
        f32 ex = su_x[i] - SU_CX, ey = su_y[i] - SU_CY;
        if (ex * ex + ey * ey > SU_R * SU_R) { su_in[i] = 0; if (!jx_is_cpu(i)) rumble(jx_ch[i], 400); continue; }
        alive++; last = i;
    }
    if (alive <= 1) { su_win = last; if (last >= 0) su_sc[last]++; su_end_t = now; }
}
static void sumo_input(u32 down) {
    if (ar_phase) { if (down & PAD_BUTTON_A) sumo_new(); if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) fun_leave(); return; }
    if (down & PAD_BUTTON_START) fun_leave();
}
static void draw_sumo(void) {
    u64 now = now_ms();
    fun_frame_begin(20, 0.6f);
    gx_text(F_SMALL, 24, 28, "SUMO", RGBA(255, 190, 120, 255), RGBA(255, 120, 80, 255), 3);
    int n = jx_n();
    jd_scores(su_sc, n, SU_WIN);
    gx_circle(SU_CX, SU_CY + 10, SU_R + 26, RGBA(30, 20, 14, 255), RGBA(10, 6, 4, 255));
    gx_circle(SU_CX, SU_CY, SU_R + 14, RGBA(196, 160, 110, 255), RGBA(150, 116, 74, 255));
    gx_circle(SU_CX, SU_CY, SU_R, RGBA(232, 206, 156, 255), RGBA(206, 174, 120, 255));
    gx_ring(SU_CX, SU_CY, SU_R - 3, SU_R + 2, RGBA(250, 250, 240, 255));
    gx_rect(SU_CX - 34, SU_CY - 14, 10, 28, RGBA(120, 90, 60, 200)); gx_rect(SU_CX + 24, SU_CY - 14, 10, 28, RGBA(120, 90, 60, 200));
    for (int i = 0; i < n; i++) {
        col_t c = jx_col(i);
        f32 k = su_in[i] ? 1 : 1 - su_fall[i] * 0.8f;
        if (k <= 0.05f) continue;
        u8 a = (u8)(255 * k);
        gx_circle(su_x[i] + 3, su_y[i] + 6, 24 * k, RGBA(0, 0, 0, (u8)(80 * k)), RGBA(0, 0, 0, 0));
        if (su_dash[i] > 0.9f) gx_circle_glow(su_x[i], su_y[i], 30, RGBA(255, 255, 255, 120), 3);
        gx_circle(su_x[i], su_y[i], 24 * k, RGBA(c.r, c.g, c.b, a), RGBA(c.r / 2, c.g / 2, c.b / 2, a));
        gx_circle(su_x[i] - 7 * k, su_y[i] - 8 * k, 7 * k, RGBA(255, 255, 255, (u8)(90 * k)), RGBA(255, 255, 255, 0));
        if (su_in[i] && su_dash[i] <= 0) gx_ring(su_x[i], su_y[i], 27, 29, RGBA(255, 255, 255, 120));   /* (élan prêt) */
    }
    if (!ar_phase && now < su_t0) jd_countdown(su_t0, now);
    if (su_end_t && !ar_phase) {
        char nm[32], t[64];
        if (su_win >= 0) { jx_name(su_win, nm, sizeof(nm)); snprintf(t, sizeof(t), "%s reste sur le ring !", nm); } else snprintf(t, sizeof(t), "Tout le monde dehors !");
        fun_shadow_text(F_LARGE, 320, 120, t, 1.4f, su_win >= 0 ? jx_col(su_win) : RGBA(255, 255, 255, 255));
    }
    if (ar_phase) jx_over("Sumo", su_sc, "manches", 0);
    fun_footer(ar_phase ? "A : rejouer     B : sortir" : "Stick ou croix : bouger     A : élan     START : sortir");
}

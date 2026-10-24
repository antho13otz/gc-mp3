// GC Station v115 : decors de saison (inclus dans main.c ; avant : v21, Halloween / Noel / ete).
// Huit decors, plus riches, dessines par-dessus l'interface mais surtout sur les bords (bandeau du
// haut, bas de l'ecran) pour ne rien cacher d'important. Reglage « Decor de saison » :
// Non / Auto (selon la date) / un decor choisi (Printemps, Ete, Automne, Halloween, Hiver, Noel,
// Saint-Valentin, Nouvel An). Fichier gc-mp3.txt : decor_saison = non / auto / halloween / ...

enum { SEASON_NONE, SEASON_HALLOWEEN, SEASON_NOEL, SEASON_ETE, SEASON_HIVER, SEASON_PRINTEMPS, SEASON_AUTOMNE,
       SEASON_VALENTIN, SEASON_NOUVELAN, SEASON_N };
/* ordre du reglage : 0 Non, 1 Auto, puis les decors */
#define SEASON_MODES 10
static const u8 season_of_mode[SEASON_MODES] = { SEASON_NONE, SEASON_NONE, SEASON_PRINTEMPS, SEASON_ETE, SEASON_AUTOMNE,
                                                 SEASON_HALLOWEEN, SEASON_HIVER, SEASON_NOEL, SEASON_VALENTIN, SEASON_NOUVELAN };
static const char *const season_key[SEASON_MODES] = { "non", "auto", "printemps", "ete", "automne", "halloween", "hiver",
                                                      "noel", "valentin", "nouvelan" };
static const char *const season_name[SEASON_N] = { "rien en ce moment", "Halloween", "No\xC3\xABl", "\xC3\x89t\xC3\xA9", "Hiver",
                                                   "Printemps", "Automne", "Saint-Valentin", "Nouvel An" };
static int season_test = 0;                 /* (essais : saison imposee) */
static int season_by_date(void) {
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    if (!tm || tm->tm_year < 100) return SEASON_NONE;
    int m = tm->tm_mon + 1, d = tm->tm_mday;
    if ((m == 12 && d == 31) || (m == 1 && d == 1)) return SEASON_NOUVELAN;
    if (m == 12 || (m == 1 && d <= 6)) return SEASON_NOEL;
    if (m == 2 && d >= 10 && d <= 15) return SEASON_VALENTIN;
    if ((m == 10 && d >= 15) || (m == 11 && d <= 2)) return SEASON_HALLOWEEN;
    if (m == 1 || m == 2 || (m == 3 && d < 20)) return SEASON_HIVER;
    if (m < 6 || (m == 6 && d < 21)) return SEASON_PRINTEMPS;
    if (m < 9 || (m == 9 && d < 22)) return SEASON_ETE;
    return SEASON_AUTOMNE;
}
static int season_now(void) {
    static int env_done = 0;
    if (!env_done) { env_done = 1; const char *e = getenv("PCSIM_SAISON"); if (e) season_test = atoi(e); }   /* (simulateur : decor impose) */
    if (season_test) return season_test;
    if (season_on <= 0) return SEASON_NONE;
    if (season_on == 1) return season_by_date();
    return season_on < SEASON_MODES ? season_of_mode[season_on] : SEASON_NONE;
}
static const char *season_key_of(int m) { return m >= 0 && m < SEASON_MODES ? season_key[m] : "non"; }
static void season_parse(const char *v) {
    for (int i = 0; i < SEASON_MODES; i++) if (!strcasecmp(v, season_key[i])) { season_on = i; return; }
    if (!strncasecmp(v, "no\xC3\xABl", 4)) { season_on = 7; return; }
    season_on = yes(v) ? 1 : 0;                                    /* (ancien reglage : oui / non) */
}

/* ---------- petits outils de dessin ---------- */
static f32 sz_h(int i) {                                          /* hasard fixe dans [0, 1) */
    u32 x = (u32)i * 2654435761u; x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return (f32)(x & 0xFFFF) / 65536.0f;
}
/* ovale tourne (feuilles, petales, confettis) */
static void sz_blob(f32 cx, f32 cy, f32 rx, f32 ry, f32 ang, col_t mid, col_t edge) {
    enum { N = 12 };
    f32 ca = cosf(ang), sa = sinf(ang);
    gx_set_flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, N + 2);
    vtx(cx, cy, mid);
    for (int i = 0; i <= N; i++) {
        f32 a = (f32)i / N * 2 * PI_F, x = cosf(a) * rx, y = sinf(a) * ry;
        vtx(cx + (x * ca - y * sa) / ux_k, cy + x * sa + y * ca, edge);
    }
    GX_End();
}
static void sz_line(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, col_t c0, col_t c1) {
    f32 dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.01f) return;
    f32 nx = -dy / l * w / 2, ny = dx / l * w / 2;
    gx_poly4(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c0, c1, c1, c0);
}
static void sz_heart(f32 cx, f32 cy, f32 s, f32 ang, col_t mid, col_t edge) {
    enum { N = 28 };
    f32 ca = cosf(ang), sa = sinf(ang);
    gx_set_flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, N + 2);
    vtx(cx, cy - 1.5f * s * 0.06f, mid);
    for (int i = 0; i <= N; i++) {
        f32 t = (f32)i / N * 2 * PI_F, st = sinf(t);
        f32 x = 16 * st * st * st * s * 0.06f, y = -(13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t)) * s * 0.06f;
        vtx(cx + (x * ca - y * sa) / ux_k, cy + x * sa + y * ca, edge);
    }
    GX_End();
}
static col_t sz_a(col_t c, f32 a) { c.a = (u8)(c.a * (a < 0 ? 0 : a > 1 ? 1 : a)); return c; }

/* ---------- Halloween ---------- */
static void season_pumpkin(f32 cx, f32 cy, f32 r, f32 glow) {
    gx_circle(cx, cy, r * 1.8f, RGBA(255, 140, 30, (u8)(40 + glow * 60)), RGBA(255, 140, 30, 0));
    gx_ellipse(cx - r * 0.45f, cy, r * 0.62f, r * 0.9f, RGBA(255, 150, 40, 255), RGBA(200, 90, 10, 255));
    gx_ellipse(cx + r * 0.45f, cy, r * 0.62f, r * 0.9f, RGBA(255, 150, 40, 255), RGBA(200, 90, 10, 255));
    gx_ellipse(cx, cy, r * 0.7f, r * 0.95f, RGBA(255, 170, 60, 255), RGBA(220, 100, 20, 255));
    gx_rect(cx - r * 0.1f, cy - r * 1.2f, r * 0.2f, r * 0.35f, RGBA(70, 120, 40, 255));
    col_t f = RGBA(255, 240, (u8)(120 + glow * 100), 255);
    gx_tri(cx - r * 0.5f, cy - r * 0.15f, cx - r * 0.2f, cy - r * 0.15f, cx - r * 0.35f, cy - r * 0.45f, f);
    gx_tri(cx + r * 0.2f, cy - r * 0.15f, cx + r * 0.5f, cy - r * 0.15f, cx + r * 0.35f, cy - r * 0.45f, f);
    gx_poly4(cx - r * 0.5f, cy + r * 0.2f, cx + r * 0.5f, cy + r * 0.2f, cx + r * 0.3f, cy + r * 0.5f, cx - r * 0.3f, cy + r * 0.5f, f, f, f, f);
}
static void season_bat(f32 x, f32 y, f32 s, f32 flap) {
    gx_circle(x, y, 16 * s, RGBA(255, 140, 40, 140), RGBA(255, 140, 40, 0));           /* (lueur orange : visible sur le fond sombre) */
    col_t c = RGBA(25, 12, 35, 240);
    f32 w = 14 * s, up = flap * 6 * s;
    gx_ellipse(x, y, 3.5f * s, 4.5f * s, c, c);
    gx_tri(x - 2 * s, y - 1 * s, x - w, y - up, x - w * 0.6f, y + 4 * s, c);
    gx_tri(x + 2 * s, y - 1 * s, x + w, y - up, x + w * 0.6f, y + 4 * s, c);
    gx_tri(x - 3 * s, y - 3 * s, x - 1 * s, y - 3 * s, x - 2.5f * s, y - 6.5f * s, c);    /* (oreilles) */
    gx_tri(x + 1 * s, y - 3 * s, x + 3 * s, y - 3 * s, x + 2.5f * s, y - 6.5f * s, c);
}
static void season_ghost(f32 x, f32 y, f32 s, f32 t) {
    col_t w = RGBA(240, 240, 255, 170), w0 = RGBA(240, 240, 255, 0);
    gx_circle(x, y + 4 * s, 26 * s, RGBA(170, 150, 255, 50), RGBA(170, 150, 255, 0));
    gx_circle(x, y, 11 * s, w, w);
    gx_rect(x - 11 * s, y, 22 * s, 12 * s, w);
    for (int k = 0; k < 3; k++) gx_circle(x - 7.3f * s + k * 7.3f * s, y + 12 * s + 1.5f * s * sinf(t * 5 + k * 2), 3.7f * s, w, w);
    (void)w0;
    gx_ellipse(x - 4 * s, y - 1 * s, 2 * s, 3 * s, RGBA(30, 20, 50, 230), RGBA(30, 20, 50, 230));
    gx_ellipse(x + 4 * s, y - 1 * s, 2 * s, 3 * s, RGBA(30, 20, 50, 230), RGBA(30, 20, 50, 230));
    gx_ellipse(x, y + 5 * s, 2 * s, 2.5f * s, RGBA(30, 20, 50, 200), RGBA(30, 20, 50, 200));
}
static void season_spider(f32 x, f32 y, f32 t) {
    sz_line(x, 0, x, y - 4, 1, RGBA(220, 220, 230, 140), RGBA(220, 220, 230, 140));
    col_t c = RGBA(20, 10, 25, 255);
    for (int k = 0; k < 4; k++) {
        f32 a = 0.35f + k * 0.32f, wig = 0.1f * sinf(t * 6 + k);
        for (int sd = -1; sd <= 1; sd += 2) {
            f32 mx = x + sd * cosf(a + wig) * 7, my = y - 3 + sinf(a + wig) * 2 - 3;
            sz_line(x, y, mx, my, 1.4f, c, c);
            sz_line(mx, my, mx + sd * 4, my + 7, 1.4f, c, c);
        }
    }
    gx_circle(x, y, 7, RGBA(255, 120, 30, 60), RGBA(255, 120, 30, 0));
    gx_ellipse(x, y + 2, 4.5f, 5.5f, c, c);
    gx_circle(x, y - 4, 3, c, c);
    gx_circle(x - 1.2f, y - 4.5f, 0.8f, RGBA(255, 60, 60, 255), RGBA(255, 60, 60, 255));
    gx_circle(x + 1.2f, y - 4.5f, 0.8f, RGBA(255, 60, 60, 255), RGBA(255, 60, 60, 255));
}
static void sz_halloween(f32 t) {
    for (int i = 0; i < 7; i++) {                                  /* brume violette et verte en bas */
        f32 x = fmodf(t * (6 + i * 2) + i * 110, 840) - 100;
        col_t c = i % 2 ? RGBA(120, 60, 180, 45) : RGBA(70, 160, 90, 35);
        sz_blob(x, 468 + 6 * sinf(t * 0.4f + i), 90, 22, 0, c, sz_a(c, 0));
    }
    for (int i = 0; i < 7; i++) {                                  /* chauves-souris */
        f32 sp = 22 + i * 7, x = fmodf(t * sp + i * 173, 760) - 60;
        f32 y = 16 + (i % 4) * 12 + 9 * sinf(t * 1.3f + i * 2);
        season_bat(x, y, 1.0f + 0.2f * (i % 3), sinf(t * 9 + i));
    }
    season_spider(604, 46 + 22 * (0.5f + 0.5f * sinf(t * 0.6f)), t);
    season_ghost(40 + 10 * sinf(t * 0.8f), 400 + 8 * sinf(t * 1.6f), 0.9f, t);
    season_ghost(600 + 8 * sinf(t * 0.7f + 2), 380 + 7 * sinf(t * 1.4f + 1), 0.7f, t + 1);
    f32 g = 0.5f + 0.5f * sinf(t * 2.0f);
    f32 px = ui_skin >= 2 ? -100 : ui_skin ? 140 : 345, py = ui_skin ? 24 : 36, pr = ui_skin ? 10 : 15;   /* (Moderne : a cote du nom ; Nouvelle : pas en haut) */
    gx_circle(px, py, pr * 2.1f, RGBA(255, 120, 30, (u8)(50 + g * 50)), RGBA(255, 120, 30, 0));
    season_pumpkin(px, py, pr, g);
    season_pumpkin(620, 466, 10, 1 - g);
    season_pumpkin(596, 470, 7, g);
    season_pumpkin(22, 466, 9, g);
}

/* ---------- neige (Noel, Hiver) ---------- */
static void sz_snow(f32 t, int n, f32 speed) {
    for (int i = 0; i < n; i++) {
        int far = i % 3 == 0;
        f32 sp = (far ? 9 : 16) + sz_h(i) * 14 * speed, x0 = sz_h(i + 500) * 660 - 10;
        f32 y = fmodf(t * sp + sz_h(i + 900) * 520, 520) - 20, x = x0 + (far ? 6 : 14) * sinf(t * 0.7f + i);
        f32 r = far ? 1.0f : 1.4f + sz_h(i + 77) * 1.6f;
        gx_circle(x, y, r * 2.2f, RGBA(200, 220, 255, far ? 25 : 45), RGBA(200, 220, 255, 0));
        gx_circle(x, y, r, RGBA(255, 255, 255, far ? 120 : 200), RGBA(225, 238, 255, far ? 70 : 140));
    }
}
static void sz_snow_ground(void) {                                 /* neige tombee, en bas */
    for (int i = 0; i < 24; i++) {
        f32 x = i * 28 + 6, r = 11 + 6 * sz_h(i + 31);
        gx_circle(x, 482 - r * 0.15f, r, RGBA(250, 252, 255, 225), RGBA(215, 230, 250, 225));
    }
    gx_hgrad(0, 478, 640, 2, RGBA(255, 255, 255, 120), RGBA(255, 255, 255, 120));
}
static void sz_tree(f32 x, f32 y, f32 s, f32 t) {
    gx_circle(x, y - 22 * s, 40 * s, RGBA(255, 220, 120, 35), RGBA(255, 220, 120, 0));
    gx_rect(x - 3 * s, y - 4 * s, 6 * s, 8 * s, RGBA(110, 70, 40, 255));
    for (int k = 0; k < 3; k++) {
        f32 by = y - 4 * s - k * 12 * s, hw = (17 - k * 4.5f) * s;
        gx_tri(x - hw, by, x + hw, by, x, by - 17 * s, RGBA(40 + k * 12, 140 + k * 15, 70, 255));
    }
    static const u8 lc[4][3] = { { 255, 60, 60 }, { 255, 210, 60 }, { 90, 160, 255 }, { 255, 120, 220 } };
    for (int i = 0; i < 9; i++) {
        f32 lx = x + (sz_h(i + 40) - 0.5f) * (26 - (i % 3) * 8) * s, ly = y - 8 * s - (i % 3) * 12 * s - sz_h(i + 60) * 6 * s;
        int on = ((int)(t * 3) + i) % 3 != 0;
        const u8 *c = lc[i % 4];
        if (on) gx_circle(lx, ly, 4 * s, RGBA(c[0], c[1], c[2], 110), RGBA(c[0], c[1], c[2], 0));
        gx_circle(lx, ly, 1.4f * s, RGBA(c[0], c[1], c[2], on ? 255 : 110), RGBA(c[0], c[1], c[2], on ? 255 : 110));
    }
    f32 st = 0.5f + 0.5f * sinf(t * 3);                            /* etoile */
    f32 sy = y - 4 * s - 2 * 12 * s - 17 * s;
    gx_circle(x, sy, (8 + 4 * st) * s, RGBA(255, 230, 120, 120), RGBA(255, 230, 120, 0));
    for (int k = 0; k < 5; k++) {
        f32 a = -PI_F / 2 + k * 2 * PI_F / 5;
        gx_tri(x, sy, x + cosf(a - 0.35f) * 2.5f * s, sy + sinf(a - 0.35f) * 2.5f * s, x + cosf(a) * 6 * s, sy + sinf(a) * 6 * s, RGBA(255, 225, 90, 255));
        gx_tri(x, sy, x + cosf(a + 0.35f) * 2.5f * s, sy + sinf(a + 0.35f) * 2.5f * s, x + cosf(a) * 6 * s, sy + sinf(a) * 6 * s, RGBA(255, 225, 90, 255));
    }
}
static void sz_noel(f32 t) {
    static const u8 lc[4][3] = { { 255, 60, 60 }, { 60, 220, 90 }, { 255, 210, 60 }, { 90, 160, 255 } };
    for (int i = 0; i < 24; i++) {                                 /* guirlande en haut, qui pend */
        f32 x = 10 + i * 27, y = 5 + 4 * fabsf(sinf(i * 0.52f));
        if (i < 23) sz_line(x, y, x + 27, 5 + 4 * fabsf(sinf((i + 1) * 0.52f)), 1, RGBA(40, 90, 50, 200), RGBA(40, 90, 50, 200));
        int on = ((int)(t * 2) + i) % 3 != 0;
        const u8 *c = lc[i % 4];
        if (on) gx_circle_glow(x, y + 3, 5, RGBA(c[0], c[1], c[2], 140), 4);
        gx_ellipse(x, y + 3, 2.6f, 3.6f, RGBA(c[0], c[1], c[2], on ? 255 : 90), RGBA(c[0], c[1], c[2], on ? 255 : 90));
    }
    sz_snow(t, 60, 1.0f);
    sz_snow_ground();
    sz_tree(28, 470, 1.0f, t);
    f32 u = fmodf(t, 22.0f);                                       /* etoile filante de temps en temps */
    if (u < 1.4f) {
        f32 k = u / 1.4f, x = 520 - k * 380, y = 40 + k * 60;
        sz_line(x, y, x + 50, y - 8, 2, RGBA(255, 250, 220, (u8)(220 * (1 - k))), RGBA(255, 250, 220, 0));
        gx_circle(x, y, 5, RGBA(255, 250, 220, (u8)(200 * (1 - k))), RGBA(255, 250, 220, 0));
    }
}
static void sz_hiver(f32 t) {
    for (int i = 0; i < 34; i++) {                                 /* stalactites de glace en haut */
        f32 x = i * 19 + 4 + sz_h(i) * 6, l = 5 + sz_h(i + 9) * 13;
        gx_tri(x - 3, 0, x + 3, 0, x, l, RGBA(200, 230, 255, 170));
        gx_tri(x - 1, 0, x + 1, 0, x, l * 0.8f, RGBA(255, 255, 255, 200));
    }
    gx_circle(0, 0, 70, RGBA(200, 230, 255, 50), RGBA(200, 230, 255, 0));    /* givre dans les coins */
    gx_circle(640, 0, 70, RGBA(200, 230, 255, 50), RGBA(200, 230, 255, 0));
    sz_snow(t, 80, 1.3f);
    sz_snow_ground();
}

/* ---------- Printemps : petales de cerisier, fleurs, papillons ---------- */
static void sz_flower(f32 x, f32 y, f32 r, col_t c, f32 t) {
    sz_line(x, y + r, x + 1, 482, 1.5f, RGBA(80, 160, 70, 230), RGBA(60, 130, 50, 230));
    for (int k = 0; k < 5; k++) {
        f32 a = k * 2 * PI_F / 5 + 0.2f * sinf(t * 0.8f + x);
        sz_blob(x + cosf(a) * r * 0.75f, y + sinf(a) * r * 0.75f, r * 0.6f, r * 0.42f, a, c, c);
    }
    gx_circle(x, y, r * 0.35f, RGBA(255, 220, 90, 255), RGBA(240, 170, 40, 255));
}
static void sz_butterfly(f32 x, f32 y, f32 s, f32 flap, col_t c) {
    f32 w = 0.35f + 0.65f * fabsf(flap);
    sz_blob(x - 4 * s * w, y - 2 * s, 5 * s * w, 4 * s, -0.4f, c, sz_a(c, 0.7f));
    sz_blob(x + 4 * s * w, y - 2 * s, 5 * s * w, 4 * s, 0.4f, c, sz_a(c, 0.7f));
    sz_blob(x - 3 * s * w, y + 3 * s, 3.5f * s * w, 3 * s, 0.4f, c, sz_a(c, 0.7f));
    sz_blob(x + 3 * s * w, y + 3 * s, 3.5f * s * w, 3 * s, -0.4f, c, sz_a(c, 0.7f));
    gx_ellipse(x, y, 1 * s, 5 * s, RGBA(40, 30, 40, 255), RGBA(40, 30, 40, 255));
}
static void sz_printemps(f32 t) {
    for (int i = 0; i < 40; i++) {                                 /* petales qui tombent en tournant */
        f32 sp = 14 + sz_h(i) * 16, x0 = sz_h(i + 300) * 700 - 30;
        f32 y = fmodf(t * sp + sz_h(i + 600) * 520, 520) - 20;
        f32 x = fmodf(x0 + 30 * sinf(t * 0.5f + i) + t * 6 + 700, 700) - 30;
        f32 a = t * (1 + sz_h(i + 7)) + i, fl = 0.4f + 0.6f * fabsf(sinf(t * 2 + i));
        col_t c = sz_h(i + 3) < 0.5f ? RGBA(255, 190, 215, 220) : RGBA(255, 225, 235, 220);
        sz_blob(x, y, 4.5f * fl + 1, 3, a, c, RGBA(255, 150, 190, 200));
    }
    static const u8 fc[5][3] = { { 255, 120, 170 }, { 250, 250, 255 }, { 180, 130, 255 }, { 255, 200, 80 }, { 120, 190, 255 } };
    for (int i = 0; i < 16; i++) {                                 /* fleurs en bas */
        f32 x = 10 + i * 41 + sz_h(i + 50) * 14, y = 462 + sz_h(i + 70) * 10;
        const u8 *c = fc[i % 5];
        sz_flower(x, y, 6 + sz_h(i + 90) * 3, RGBA(c[0], c[1], c[2], 240), t);
    }
    for (int i = 0; i < 2; i++) {                                  /* papillons */
        f32 x = fmodf(t * (18 + i * 7) + i * 300, 760) - 60, y = 120 + i * 180 + 30 * sinf(t * 0.9f + i * 3);
        sz_butterfly(x, y, 1.3f, sinf(t * 10 + i), i ? RGBA(120, 190, 255, 220) : RGBA(255, 170, 60, 220));
    }
}

/* ---------- Ete : soleil, vagues, mouettes, eclats ---------- */
static void sz_ete(f32 t) {
    f32 p = 0.5f + 0.5f * sinf(t * 0.5f);
    f32 sx = ui_skin >= 2 ? 600 : 345, sy = ui_skin >= 2 ? 26 : 34;
    gx_circle(sx, sy, 60 + p * 10, RGBA(255, 200, 90, 70), RGBA(255, 200, 90, 0));
    gx_circle(sx, sy, 17, RGBA(255, 245, 170, 245), RGBA(255, 190, 60, 220));
    for (int k = 0; k < 12; k++) {
        f32 a = k * 0.5236f + t * 0.3f, r0 = 21, r1 = 30 + 4 * p * (k % 2 ? 1 : 0.5f);
        gx_poly4(sx + cosf(a - 0.08f) * r0, sy + sinf(a - 0.08f) * r0, sx + cosf(a + 0.08f) * r0, sy + sinf(a + 0.08f) * r0,
                 sx + cosf(a) * r1, sy + sinf(a) * r1, sx + cosf(a) * r1, sy + sinf(a) * r1,
                 RGBA(255, 220, 100, 220), RGBA(255, 220, 100, 220), RGBA(255, 200, 80, 120), RGBA(255, 200, 80, 120));
    }
    for (int i = 0; i < 3; i++) {                                  /* mouettes */
        f32 x = fmodf(t * (20 + i * 6) + i * 240, 760) - 60, y = 60 + i * 22 + 6 * sinf(t + i);
        f32 fl = 3 * sinf(t * 5 + i);
        col_t c = RGBA(250, 250, 255, 220);
        sz_line(x - 9, y - fl, x, y + 2, 1.6f, c, c); sz_line(x, y + 2, x + 9, y - fl, 1.6f, c, c);
    }
    for (int w = 0; w < 2; w++) {                                  /* vagues en bas */
        col_t top = w ? RGBA(90, 200, 255, 150) : RGBA(40, 140, 230, 130), bot = w ? RGBA(40, 120, 220, 170) : RGBA(20, 80, 180, 170);
        f32 base = w ? 470 : 462;
        for (int i = 0; i < 40; i++) {
            f32 x0 = i * 16.5f, x1 = x0 + 16.5f;
            f32 y0 = base + 4 * sinf(x0 * 0.04f + t * (w ? 1.6f : 1.1f) + w * 2), y1 = base + 4 * sinf(x1 * 0.04f + t * (w ? 1.6f : 1.1f) + w * 2);
            gx_poly4(x0, y0, x1, y1, x1, 480, x0, 480, top, top, bot, bot);
            if (w) sz_line(x0, y0, x1, y1, 1.5f, RGBA(255, 255, 255, 150), RGBA(255, 255, 255, 150));
        }
    }
    for (int i = 0; i < 16; i++) {                                 /* eclats de soleil */
        f32 x = fmodf(t * (8 + i % 4 * 3) + i * 131, 680) - 20, y = 450 - fmodf(t * (5 + i % 3 * 2) + i * 47, 440);
        f32 tw = 0.5f + 0.5f * sinf(t * 4 + i);
        gx_circle(x, y, 2 + tw, RGBA(255, 240, 180, (u8)(80 + 80 * tw)), RGBA(255, 240, 180, 0));
    }
}

/* ---------- Automne : feuilles qui tombent, tas de feuilles ---------- */
static void sz_leaf(f32 x, f32 y, f32 s, f32 a, int k) {
    static const u8 lc[4][3] = { { 230, 110, 30 }, { 200, 60, 40 }, { 240, 180, 50 }, { 150, 90, 40 } };
    const u8 *c = lc[k & 3];
    sz_blob(x, y, 7 * s, 3.6f * s, a, RGBA(c[0], c[1], c[2], 240), RGBA(c[0] * 3 / 4, c[1] * 3 / 4, c[2] * 3 / 4, 240));
    f32 ca = cosf(a), sa = sinf(a);
    sz_line(x - ca * 8 * s / ux_k, y - sa * 8 * s, x + ca * 6 * s / ux_k, y + sa * 6 * s, 0.8f, RGBA(90, 50, 20, 200), RGBA(90, 50, 20, 200));
}
static void sz_automne(f32 t) {
    for (int i = 0; i < 26; i++) {
        f32 sp = 18 + sz_h(i) * 18, x0 = sz_h(i + 300) * 700;
        f32 y = fmodf(t * sp + sz_h(i + 600) * 520, 520) - 20;
        f32 x = fmodf(x0 + t * 10 + 40 * sinf(t * 0.8f + i), 700) - 30;
        f32 a = sinf(t * (1.2f + sz_h(i + 5)) + i) * 1.4f + i;
        sz_leaf(x, y, 0.9f + sz_h(i + 11) * 0.7f, a, i);
    }
    for (int i = 0; i < 46; i++) {                                 /* tas de feuilles en bas */
        f32 x = sz_h(i + 200) * 660 - 10, y = 472 + sz_h(i + 260) * 10;
        sz_leaf(x, y, 1.0f + sz_h(i + 230) * 0.4f, sz_h(i + 290) * 6.28f, i);
    }
    gx_circle(600, 30, 80, RGBA(255, 160, 60, 40), RGBA(255, 160, 60, 0));     /* lumiere d'automne */
}

/* ---------- Saint-Valentin : coeurs qui montent ---------- */
static void sz_valentin(f32 t) {
    for (int i = 0; i < 22; i++) {
        f32 sp = 14 + sz_h(i) * 16, x0 = sz_h(i + 300) * 660 - 10;
        f32 y = 500 - fmodf(t * sp + sz_h(i + 600) * 540, 540), x = x0 + 16 * sinf(t * 0.9f + i);
        f32 s = 0.6f + sz_h(i + 9) * 0.9f, beat = 1 + 0.08f * expf(-fmodf(t + sz_h(i) * 3, 1.1f) * 9);
        col_t c = sz_h(i + 2) < 0.5f ? RGBA(255, 80, 140, 200) : RGBA(255, 150, 200, 200);
        f32 fade = y > 400 ? (480 - y) / 80 : 1;
        gx_circle(x, y, 16 * s, sz_a(RGBA(255, 90, 160, 60), fade), RGBA(255, 90, 160, 0));
        sz_heart(x, y, 10 * s * beat, 0.25f * sinf(t + i), sz_a(RGBA(255, 210, 230, 230), fade), sz_a(c, fade));
    }
    for (int i = 0; i < 20; i++) {                                 /* guirlande de petits coeurs en haut */
        f32 x = 16 + i * 32, y = 7 + 3 * sinf(i * 0.9f + t);
        sz_heart(x, y, 4.5f, 0, RGBA(255, 200, 225, 230), RGBA(255, 70, 140, 230));
    }
}

/* ---------- Nouvel An : feux d'artifice et confettis ---------- */
static void sz_nouvelan(f32 t) {
    static const u8 fc[6][3] = { { 255, 80, 120 }, { 255, 210, 80 }, { 90, 200, 255 }, { 160, 110, 255 }, { 120, 255, 150 }, { 255, 140, 60 } };
    for (int k = 0; k < 3; k++) {
        const f32 P = 2.8f;
        f32 tt = t + k * 0.95f, u = fmodf(tt, P) / P;
        int id = (int)(tt / P) * 3 + k;
        f32 bx = 80 + sz_h(id) * 480, by = 60 + sz_h(id + 1000) * 110;
        const u8 *c = fc[(int)(sz_h(id + 2000) * 6) % 6];
        if (u < 0.22f) {                                           /* la fusee monte */
            f32 q = u / 0.22f, y = 480 - (480 - by) * (1 - (1 - q) * (1 - q));
            sz_line(bx, y, bx, y + 26, 2, RGBA(255, 230, 180, 220), RGBA(255, 230, 180, 0));
            gx_circle(bx, y, 3, RGBA(255, 245, 210, 255), RGBA(255, 245, 210, 0));
        } else {                                                   /* l'explosion */
            f32 q = (u - 0.22f) / 0.78f, rr = 75 * (1 - (1 - q) * (1 - q) * (1 - q)), fall = 30 * q * q, al = 1 - q;
            if (q < 0.15f) gx_circle(bx, by, 40, RGBA(c[0], c[1], c[2], (u8)(160 * (1 - q / 0.15f))), RGBA(c[0], c[1], c[2], 0));
            for (int j = 0; j < 18; j++) {
                f32 a = j * 2 * PI_F / 18 + sz_h(id + j) * 0.2f, px = bx + cosf(a) * rr, py = by + sinf(a) * rr + fall;
                f32 tx = bx + cosf(a) * rr * 0.75f, ty = by + sinf(a) * rr * 0.75f + fall * 0.8f;
                sz_line(tx, ty, px, py, 1.6f, RGBA(c[0], c[1], c[2], 0), RGBA(c[0], c[1], c[2], (u8)(230 * al)));
                gx_circle(px, py, 2.2f, RGBA(255, 255, 255, (u8)(255 * al)), RGBA(c[0], c[1], c[2], (u8)(160 * al)));
            }
        }
    }
    for (int i = 0; i < 34; i++) {                                 /* confettis */
        f32 sp = 22 + sz_h(i) * 22, x0 = sz_h(i + 300) * 660 - 10;
        f32 y = fmodf(t * sp + sz_h(i + 600) * 520, 520) - 20, x = x0 + 10 * sinf(t * 1.3f + i);
        const u8 *c = fc[i % 6];
        sz_blob(x, y, 3.5f * fabsf(sinf(t * 3 + i)) + 0.8f, 1.6f, t * 2 + i, RGBA(c[0], c[1], c[2], 230), RGBA(c[0], c[1], c[2], 230));
    }
}

/* dessine par-dessus l'interface (surtout sur les bords, pour ne rien cacher d'important) */
static void season_overlay(void) {
    int sn = season_now();
    if (!sn) return;
    f32 t = (f32)(now_ms() % 600000) / 1000.0f;
    int sr = nv_raw;
    nv_raw = 1;                                                    /* (couleurs exactes, quel que soit le theme) */
    switch (sn) {
    case SEASON_HALLOWEEN: sz_halloween(t); break;
    case SEASON_NOEL:      sz_noel(t); break;
    case SEASON_ETE:       sz_ete(t); break;
    case SEASON_HIVER:     sz_hiver(t); break;
    case SEASON_PRINTEMPS: sz_printemps(t); break;
    case SEASON_AUTOMNE:   sz_automne(t); break;
    case SEASON_VALENTIN:  sz_valentin(t); break;
    case SEASON_NOUVELAN:  sz_nouvelan(t); break;
    }
    nv_raw = sr;
}

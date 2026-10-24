// GC Station v109 : logo de Pulse (le lanceur de GC Station, avant « Resident »). Inclus dans main.c.
// Hexagone du cube GameCube en neon (degrade rose -> violet -> bleu -> cyan), un coeur en degrade au
// centre qui bat, un trait de battement blanc qui le traverse, et des contours qui s'eloignent a chaque
// battement. Couleurs fixes (ce sont celles du logo), quel que soit le theme.

static col_t pl_grad(f32 t, u8 a) {                        /* rose -> violet -> bleu -> cyan */
    static const u8 p[4][3] = { { 255, 70, 170 }, { 160, 80, 255 }, { 70, 110, 255 }, { 40, 210, 255 } };
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    f32 s = t * 3; int i = (int)s; if (i > 2) i = 2; f32 f = s - i;
    return RGBA(p[i][0] + (p[i + 1][0] - p[i][0]) * f, p[i][1] + (p[i + 1][1] - p[i][1]) * f, p[i][2] + (p[i + 1][2] - p[i][2]) * f, a);
}
/* trait epais d'un point a un autre (couleur de chaque bout) */
static void pl_seg(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, col_t c0, col_t c1) {
    f32 dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.01f) return;
    f32 nx = -dy / l * w / 2, ny = dx / l * w / 2;
    gx_poly4(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c0, c1, c1, c0);
}
static void pl_hex(f32 cx, f32 cy, f32 r, f32 w, u8 a, f32 dim) {
    for (int i = 0; i < 6; i++) {
        f32 a0 = -PI_F / 2 + i * PI_F / 3, a1 = a0 + PI_F / 3;
        col_t c0 = pl_grad((f32)i / 6, a), c1 = pl_grad((f32)(i + 1) / 6, a);
        c0.r = (u8)(c0.r * dim); c0.g = (u8)(c0.g * dim); c0.b = (u8)(c0.b * dim);
        c1.r = (u8)(c1.r * dim); c1.g = (u8)(c1.g * dim); c1.b = (u8)(c1.b * dim);
        pl_seg(cx + r * cosf(a0), cy + r * sinf(a0), cx + r * cosf(a1), cy + r * sinf(a1), w, c0, c1);
        gx_circle(cx + r * cosf(a1), cy + r * sinf(a1), w / 2, c1, c1);     /* (coins arrondis) */
    }
}
/* coeur plein : centre clair, bord en degrade (rose en haut, bleu en bas) */
static void pl_heart(f32 cx, f32 cy, f32 s, u8 a) {
    enum { N = 48 };
    gx_set_flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, N + 2);
    vtx(cx, cy - 1.5f * s, RGBA(255, 215, 245, a));
    for (int i = 0; i <= N; i++) {
        f32 t = (f32)i / N * 2 * PI_F, st = sinf(t);
        f32 x = 16 * st * st * st, y = 13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t);
        vtx(cx + x * s, cy - y * s, pl_grad(0.15f + (0.6f - y / 30.0f) * 0.7f, a));
    }
    GX_End();
}
/* le logo, centre en (cx, cy), r = rayon de l'hexagone ; t = temps (s) pour l'animation (0 : fixe) */
static void pulse_logo(f32 cx, f32 cy, f32 r, f32 t) {
    int sr = nv_raw;
    nv_raw = 1;
    uxsave_t uz = ux_native(cx);
    f32 ph = t > 0 ? fmodf(t, 1.1f) / 1.1f : 0;           /* (un battement toutes les 1,1 s) */
    f32 beat = t > 0 ? expf(-ph * 9.0f) : 0;               /* (grossit d'un coup, puis revient) */
    /* contours qui s'eloignent */
    pl_hex(cx, cy, r * 1.42f, r * 0.05f, 60, 0.6f);
    pl_hex(cx, cy, r * 1.22f, r * 0.06f, 110, 0.8f);
    if (t > 0) pl_hex(cx, cy, r * (1.05f + 0.55f * ph), r * 0.07f, (u8)(150 * (1 - ph)), 1.0f);
    /* hexagone (lueur, puis trait) */
    pl_hex(cx, cy, r, r * 0.26f, 40, 1.0f);
    pl_hex(cx, cy, r, r * 0.15f, 90, 1.0f);
    pl_hex(cx, cy, r, r * 0.075f, 255, 1.0f);
    /* coeur */
    f32 hs = r * 0.033f * (1.0f + 0.10f * beat);
    gx_circle(cx, cy, r * 0.75f, RGBA(255, 120, 220, (u8)(50 + 60 * beat)), RGBA(255, 120, 220, 0));
    pl_heart(cx, cy + r * 0.04f, hs, 255);
    /* battement */
    static const f32 ecg[8][2] = { { -1.75f, 0 }, { -0.42f, 0 }, { -0.30f, 0 }, { -0.19f, -0.44f }, { -0.02f, 0.38f }, { 0.13f, -0.14f }, { 0.24f, 0 }, { 1.75f, 0 } };
    f32 ly = cy + r * 0.02f;
    for (int pass = 0; pass < 2; pass++) {
        f32 w = pass ? r * 0.06f : r * 0.17f;
        col_t c = pass ? RGBA(255, 255, 255, 255) : RGBA(255, 220, 250, 70);
        for (int i = 0; i < 7; i++) {
            f32 ax = cx + ecg[i][0] * r, ay = ly + ecg[i][1] * r, bx = cx + ecg[i + 1][0] * r, by = ly + ecg[i + 1][1] * r;
            col_t c0 = c, c1 = c;
            if (i == 0) c0.a = 0;                           /* (les bouts s'effacent) */
            if (i == 6) c1.a = 0;
            pl_seg(ax, ay, bx, by, w, c0, c1);
        }
    }
    ux_restore(uz);
    nv_raw = sr;
}
/* « Pulse » en degrade (rose en haut, bleu en bas), retourne la largeur */
static int pulse_word(int f, f32 x, f32 base, f32 scale) {
    int sr = nv_raw;
    nv_raw = 1;
    txt_scale = scale;
    int w = gx_text(f, x, base, "Pulse", RGBA(255, 150, 215, 255), RGBA(130, 150, 255, 255), 0);
    txt_scale = 1.0f;
    nv_raw = sr;
    return w;
}

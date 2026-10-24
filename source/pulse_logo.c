// GC Station v109 (logo refait en v113) : logo de Pulse (le lanceur de GC Station, avant « Resident »). Inclus dans main.c.
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
/* v113 : nouveau logo (choisi par l'utilisateur, « cardio realiste ») : cube aux 3 faces ecartees
   (rose, violet, bleu) dans l'hexagone en degrade diagonal ; un battement de cardio blanc traverse le
   cube et sort de l'hexagone en degrade (rose a gauche, cyan a droite) en s'effacant ; l'hexagone passe
   par-dessus le battement, avec une ouverture en degrade la ou il passe. */
static col_t pl_dcol(f32 x, f32 y, f32 cx, f32 cy, f32 r, u8 a, f32 dim) {   /* rose en haut a gauche -> cyan en bas a droite */
    col_t c = pl_grad(((x - cx) + (y - cy)) / (2.5f * r) + 0.5f, a);
    c.r = (u8)(c.r * dim); c.g = (u8)(c.g * dim); c.b = (u8)(c.b * dim);
    return c;
}
static f32 pl_open(f32 y, f32 ly, f32 r) {                  /* ouverture de l'hexagone autour du battement */
    f32 dy = fabsf(y - ly), gh = r * 0.073f, fd = r * 0.173f;
    if (dy <= gh) return 0;
    f32 f = (dy - gh) / fd;
    if (f >= 1) return 1;
    return f * sqrtf(f);
}
/* hexagone de rayon hr (logo de rayon r), ouvert autour de ly */
static void pl_hexo(f32 cx, f32 cy, f32 r, f32 hr, f32 w, u8 a, f32 dim, f32 ly) {
    enum { SUB = 8 };
    if (w < 1.0f) w = 1.0f;
    for (int i = 0; i < 6; i++) {
        f32 a0 = -PI_F / 2 + i * PI_F / 3, a1 = a0 + PI_F / 3;
        f32 x0 = cx + hr * cosf(a0), y0 = cy + hr * sinf(a0), x1 = cx + hr * cosf(a1), y1 = cy + hr * sinf(a1);
        for (int k = 0; k < SUB; k++) {
            f32 u0 = (f32)k / SUB, u1 = (f32)(k + 1) / SUB;
            f32 px = x0 + (x1 - x0) * u0, py = y0 + (y1 - y0) * u0, qx = x0 + (x1 - x0) * u1, qy = y0 + (y1 - y0) * u1;
            f32 o0 = pl_open(py, ly, r), o1 = pl_open(qy, ly, r);
            if (o0 <= 0 && o1 <= 0) continue;
            pl_seg(px, py, qx, qy, w, pl_dcol(px, py, cx, cy, r, (u8)(a * o0), dim), pl_dcol(qx, qy, cx, cy, r, (u8)(a * o1), dim));
        }
        f32 o = pl_open(y1, ly, r);                          /* (coins arrondis) */
        if (o > 0) { col_t c = pl_dcol(x1, y1, cx, cy, r, (u8)(a * o), dim); gx_circle(x1, y1, w / 2, c, c); }
    }
}
/* le cube aux faces ecartees, centre en (cx, cy) ; s = taille d'une arete, g = ecart */
static void pl_cube(f32 cx, f32 cy, f32 s, f32 g) {
    const f32 k = 0.866f;
    col_t ct = RGBA(255, 150, 220, 255), cl = RGBA(150, 90, 255, 255), cr = RGBA(60, 100, 230, 255);
    gx_poly4(cx, cy - s - g, cx + s * k, cy - s * 0.5f - g, cx, cy - g, cx - s * k, cy - s * 0.5f - g, ct, ct, ct, ct);
    f32 lx = -g * 0.87f, ry = g * 0.5f;
    gx_poly4(cx - s * k + lx, cy - s * 0.5f + ry, cx + lx, cy + ry, cx + lx, cy + s + ry, cx - s * k + lx, cy + s * 0.5f + ry, cl, cl, cl, cl);
    gx_poly4(cx - lx, cy + ry, cx + s * k - lx, cy - s * 0.5f + ry, cx + s * k - lx, cy + s * 0.5f + ry, cx - lx, cy + s + ry, cr, cr, cr, cr);
}
/* battement de cardio (petite bosse, grand pic, petite bosse), du bord gauche au bord droit du logo */
static void pl_beat(f32 cx, f32 ly, f32 r, f32 glow) {
    static const f32 e[13][2] = { { -1.42f, 0 }, { -0.5f, 0 }, { -0.44f, -0.08f }, { -0.38f, 0 }, { -0.2f, 0 }, { -0.15f, 0.08f },
                                  { -0.07f, -0.66f }, { 0.01f, 0.3f }, { 0.06f, 0 }, { 0.18f, 0 }, { 0.26f, -0.14f }, { 0.34f, 0 }, { 1.42f, 0 } };
    const f32 x0 = cx - 1.42f * r, x1 = cx + 1.42f * r;
    for (int pass = 0; pass < 2; pass++) {
        f32 w = pass ? r * 0.047f : r * 0.17f;
        if (w < 1.0f) w = 1.0f;
        for (int i = 0; i < 12; i++) {
            f32 ax = cx + e[i][0] * (i == 0 ? r : r * 1.05f), ay = ly + e[i][1] * r * 0.55f;
            f32 bx = cx + e[i + 1][0] * (i == 11 ? r : r * 1.05f), by = ly + e[i + 1][1] * r * 0.55f;
            f32 l = fabsf(bx - ax) + fabsf(by - ay);
            int n = (int)(l / (r * 0.08f)) + 1;
            if (n > 16) n = 16;
            for (int k = 0; k < n; k++) {
                f32 u0 = (f32)k / n, u1 = (f32)(k + 1) / n;
                f32 p[2][2] = { { ax + (bx - ax) * u0, ay + (by - ay) * u0 }, { ax + (bx - ax) * u1, ay + (by - ay) * u1 } };
                col_t c[2];
                for (int j = 0; j < 2; j++) {
                    f32 dx = fabsf(p[j][0] - cx);
                    f32 fa = (1.42f * r - dx) / (0.5f * r); if (fa > 1) fa = 1; if (fa < 0) fa = 0;   /* (s'efface vers les bords) */
                    col_t g2 = pl_grad((p[j][0] - x0) / (x1 - x0), 255);
                    if (pass) {
                        f32 wv = (dx - 0.48f * r) / (0.43f * r); if (wv > 1) wv = 1; if (wv < 0) wv = 0;   /* (blanc au centre) */
                        c[j] = RGBA(255 + (g2.r - 255) * wv, 255 + (g2.g - 255) * wv, 255 + (g2.b - 255) * wv, (u8)(255 * fa));
                    } else { g2.a = (u8)((60 + 70 * glow) * fa); c[j] = g2; }
                }
                pl_seg(p[0][0], p[0][1], p[1][0], p[1][1], w, c[0], c[1]);
            }
            if (pass && i > 0 && fabsf(e[i][0]) < 0.48f) gx_circle(ax, ay, w / 2, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
        }
    }
}
/* le logo, centre en (cx, cy), r = rayon de l'hexagone ; t = temps (s) pour l'animation (0 : fixe) */
static void pulse_logo(f32 cx, f32 cy, f32 r, f32 t) {
    int sr = nv_raw;
    nv_raw = 1;
    uxsave_t uz = ux_native(cx);
    f32 ph = t > 0 ? fmodf(t, 1.1f) / 1.1f : 0;           /* (un battement toutes les 1,1 s) */
    f32 beat = t > 0 ? expf(-ph * 9.0f) : 0;               /* (s'allume d'un coup, puis revient) */
    f32 ly = cy + r * 0.013f;
    /* contours exterieurs (et celui qui s'eloigne a chaque battement) */
    pl_hexo(cx, cy, r, r * 1.29f, r * 0.017f, 90, 0.5f, ly);
    pl_hexo(cx, cy, r, r * 1.15f, r * 0.027f, 150, 0.6f, ly);
    if (t > 0) pl_hexo(cx, cy, r, r * (1.05f + 0.45f * ph), r * 0.04f, (u8)(150 * (1 - ph)), 1.0f, ly);
    /* lueur rose au centre, puis le cube */
    gx_circle(cx, cy, r * 0.62f, RGBA(255, 120, 220, (u8)(110 + 70 * beat)), RGBA(255, 120, 220, 0));
    pl_cube(cx, cy, r * 0.552f * (1.0f + 0.03f * beat), r * 0.067f);
    /* battement */
    pl_beat(cx, ly, r, beat);
    /* hexagone par-dessus (lueur, puis trait), ouvert la ou passe le battement */
    pl_hexo(cx, cy, r, r, r * 0.22f, 35, 1.0f, ly);
    pl_hexo(cx, cy, r, r, r * 0.13f, 80, 1.0f, ly);
    pl_hexo(cx, cy, r, r, r * 0.073f, 255, 1.0f, ly);
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

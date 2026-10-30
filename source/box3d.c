// GC Station v282 : la boîte du jeu en 3D (fiche d'un jeu dans la vitrine). Inclus dans main.c, avant gspace.c.
// v283 : avec la jaquette complète de GameTDB (dos | tranche | avant) quand elle existe : les 3 faces sont les vraies.
// Une boîte GameCube aux proportions des jaquettes (3 x 4, comme le pack : 192 x 256), la jaquette devant, la tranche,
// le dos (résumé, genre, joueurs, PEGI, capture). Elle tourne doucement ; le stick C la fait tourner. ↑ : elle s'ouvre
// (le couvercle pivote sur la tranche), le disque s'allume dedans ; A : prendre le disque et jouer ; ↓ ou ↑ : fermer.
// Tout est projeté par le processeur (comme le globe) puis dessiné en 2D, de la face la plus loin à la plus proche.

static f32 bx_yaw = 28, bx_pitch = 7, bx_open = 0, bx_open_to = 0, bx_spin = 0;
static u64 bx_touch = 0;                                  /* (dernier coup de stick : la boîte arrête de tourner seule) */
static int bx_game = -1;
typedef struct { f32 x, y, z; } bv3;
static f32 bx_cx, bx_cy, bx_F = 760;

static bv3 bx_rot(bv3 p) {                                /* rotation de la boîte (lacet puis tangage) */
    f32 a = bx_yaw * (f32)M_PI / 180, b = bx_pitch * (f32)M_PI / 180;
    f32 x = p.x * cosf(a) - p.z * sinf(a), z = p.x * sinf(a) + p.z * cosf(a);
    f32 y = p.y * cosf(b) - z * sinf(b); z = p.y * sinf(b) + z * cosf(b);
    bv3 r = { x, y, z };
    return r;
}
static void bx_proj(bv3 p, f32 *sx, f32 *sy) { f32 s = bx_F / (bx_F + p.z); *sx = bx_cx + p.x * s; *sy = bx_cy + p.y * s; }
static bv3 bx_lid(bv3 p, f32 hx, f32 hz) {               /* couvercle : pivote autour de la charnière (x = hx, z = hz) */
    f32 a = -bx_open * 1.92f;                            /* (jusqu'a 110 degres) */
    f32 dx = p.x - hx, dz = p.z - hz;
    bv3 r = { hx + dx * cosf(a) - dz * sinf(a), p.y, hz + dx * sinf(a) + dz * cosf(a) };
    return r;
}
typedef struct { bv3 p[4]; GXTexObj *tex; f32 u0, v0, u1, v1; col_t c; f32 depth; u8 kind; } bx_face_t;   /* kind : 0 plein, 1 texture, 2 dos, 4 devant sans jaquette */
static bx_face_t bx_f[16];
static int bx_nf;
static void bx_add(bv3 a, bv3 b, bv3 c, bv3 d, int lid, f32 hx, f32 hz, u8 kind, GXTexObj *t, f32 u0, f32 v0, f32 u1, f32 v1, col_t col) {
    if (bx_nf >= 16) return;
    bx_face_t *f = &bx_f[bx_nf++];
    bv3 q[4] = { a, b, c, d };
    f32 z = 0;
    for (int i = 0; i < 4; i++) { if (lid) q[i] = bx_lid(q[i], hx, hz); f->p[i] = bx_rot(q[i]); z += f->p[i].z; }
    f->depth = z / 4; f->kind = kind; f->tex = t; f->u0 = u0; f->v0 = v0; f->u1 = u1; f->v1 = v1; f->c = col;
}
static f32 bx_area(const f32 *x, const f32 *y) { f32 a = 0; for (int i = 0; i < 4; i++) { int j = (i + 1) & 3; a += x[i] * y[j] - x[j] * y[i]; } return a / 2; }
/* une face : texture subdivisée (perspective) ou couleur ; ombrée selon son orientation ; rend sa visibilite (0-1) */
static f32 bx_draw_face(const bx_face_t *f) {
    f32 x[4], y[4];
    for (int i = 0; i < 4; i++) bx_proj(f->p[i], &x[i], &y[i]);
    f32 ar = bx_area(x, y);
    if (ar <= 0) return 0;                                /* (vue de dos) */
    bv3 e1 = { f->p[1].x - f->p[0].x, f->p[1].y - f->p[0].y, f->p[1].z - f->p[0].z }, e2 = { f->p[3].x - f->p[0].x, f->p[3].y - f->p[0].y, f->p[3].z - f->p[0].z };
    bv3 n = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
    f32 nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    f32 face = nl > 0 ? fabsf(n.z) / nl : 1;               /* (1 : face a nous) */
    if (f->kind == 1 && f->tex) {
        enum { S = 6 };
        gx_set_image(f->tex);
        GX_Begin(GX_QUADS, GX_VTXFMT0, S * S * 4);
        for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
            f32 uu[4] = { (f32)i / S, (f32)(i + 1) / S, (f32)(i + 1) / S, (f32)i / S }, vv[4] = { (f32)j / S, (f32)j / S, (f32)(j + 1) / S, (f32)(j + 1) / S };
            for (int k = 0; k < 4; k++) {
                f32 a = uu[k], b = vv[k];
                bv3 top = { f->p[0].x + (f->p[1].x - f->p[0].x) * a, f->p[0].y + (f->p[1].y - f->p[0].y) * a, f->p[0].z + (f->p[1].z - f->p[0].z) * a };
                bv3 bot = { f->p[3].x + (f->p[2].x - f->p[3].x) * a, f->p[3].y + (f->p[2].y - f->p[3].y) * a, f->p[3].z + (f->p[2].z - f->p[3].z) * a };
                bv3 pt = { top.x + (bot.x - top.x) * b, top.y + (bot.y - top.y) * b, top.z + (bot.z - top.z) * b };
                f32 sx, sy; bx_proj(pt, &sx, &sy);
                GX_Position2f32(UX(sx), sy); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(f->u0 + (f->u1 - f->u0) * a, f->v0 + (f->v1 - f->v0) * b);
            }
        }
        GX_End();
        u8 sh = (u8)(150 * (1 - face));                   /* (plus sombre de biais) */
        if (sh > 4) gx_poly4(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], RGBA(0, 0, 0, sh), RGBA(0, 0, 0, sh), RGBA(0, 0, 0, sh), RGBA(0, 0, 0, sh));
        gx_poly4(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], RGBA(255, 255, 255, 46), RGBA(255, 255, 255, 8), RGBA(255, 255, 255, 0), RGBA(255, 255, 255, 18));   /* (reflet du plastique) */
    } else if (f->kind == 4) {                            /* (pas de jaquette : degrade de la vitrine) */
        f32 l = 0.5f + 0.5f * face;
        col_t c = RGBA(70 * l, 40 * l, 150 * l, 255), c2 = RGBA(44 * l, 22 * l, 110 * l, 255);
        gx_poly4(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], c, c, c2, c2);
    } else {
        f32 l = 0.45f + 0.55f * face;
        col_t c = RGBA(f->c.r * l, f->c.g * l, f->c.b * l, 255), c2 = RGBA(f->c.r * l * 0.75f, f->c.g * l * 0.75f, f->c.b * l * 0.75f, 255);
        gx_poly4(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3], c, c, c2, c2);
    }
    return face;
}
/* le disque dans la boîte (cercle projeté), aux couleurs de la vitrine */
static void bx_disc(f32 r, f32 z, f32 glow) {
    enum { N = 48 };
    f32 px[N + 1], py[N + 1], cxp, cyp;
    bv3 c0 = { 0, 0, z };
    bx_proj(bx_rot(c0), &cxp, &cyp);
    static const f32 ring[4] = { 1.0f, 0.86f, 0.55f, 0.22f };
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i <= N; i++) { f32 a = (f32)i / N * 6.2832f; bv3 p = { cosf(a) * r * ring[k], sinf(a) * r * ring[k], z }; bx_proj(bx_rot(p), &px[i], &py[i]); }
        col_t cc = k == 0 ? RGBA(220, 222, 236, 255) : k == 1 ? au_mix(0.2f, 255) : k == 2 ? au_mix(0.8f, 255) : RGBA(230, 230, 240, 255);
        col_t ce = k == 0 ? RGBA(160, 160, 182, 255) : k == 1 ? au_mix(0.35f, 255) : k == 2 ? au_mix(0.6f, 255) : RGBA(190, 190, 205, 255);
        gx_set_flat();
        GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, N + 2);
        vtx(cxp, cyp, cc);
        for (int i = 0; i <= N; i++) vtx(px[i], py[i], ce);
        GX_End();
    }
    for (int i = 0; i <= N; i++) { f32 a = (f32)i / N * 6.2832f; bv3 p = { cosf(a) * r * 0.08f, sinf(a) * r * 0.08f, z }; bx_proj(bx_rot(p), &px[i], &py[i]); }
    gx_set_flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, N + 2);
    vtx(cxp, cyp, RGBA(16, 12, 24, 255));
    for (int i = 0; i <= N; i++) vtx(px[i], py[i], RGBA(16, 12, 24, 255));
    GX_End();
    if (glow > 0.05f) gx_circle_glow(cxp, cyp, r * 0.9f, au_mix(0.5f, (u8)(120 * glow)), 3);
}

/* la boîte du jeu k dans le rectangle (x, y, w, h) de la fiche */
static void bx_draw(int k, f32 x, f32 y, f32 w, f32 h, GXTexObj *cov, int big, GXTexObj *full) {   /* full : jaquette complete (dos | tranche | avant), v283 */
    if (k != bx_game) { bx_game = k; bx_open = bx_open_to = 0; }
    u64 now = now_ms();
    int sx = PAD_SubStickX(0), sy = PAD_SubStickY(0);
    if (abs(sx) > 30 || abs(sy) > 30) { bx_yaw += sx / 100.0f * 220 * ui_dt; bx_pitch -= sy / 100.0f * 120 * ui_dt; bx_touch = now; }
    if (bx_pitch > 40) bx_pitch = 40; if (bx_pitch < -30) bx_pitch = -30;
    if (now - bx_touch > 4000 && bx_open_to < 0.5f) {    /* (seule : un lent balancement autour de 3/4 face) */
        bx_spin += ui_dt;
        f32 target = 28 + sinf(bx_spin * 0.55f) * 16;
        while (bx_yaw - target > 180) bx_yaw -= 360;
        while (bx_yaw - target < -180) bx_yaw += 360;
        bx_yaw += (target - bx_yaw) * (1 - expf(-ui_dt * 1.5f));
        bx_pitch += (7 - bx_pitch) * (1 - expf(-ui_dt * 1.5f));
    }
    if (bx_open_to > 0.5f && now - bx_touch > 600) {     /* (ouverte : face a nous) */
        bx_yaw += (-6 - bx_yaw) * (1 - expf(-ui_dt * 4)); bx_pitch += (4 - bx_pitch) * (1 - expf(-ui_dt * 4));
    }
    bx_open += (bx_open_to - bx_open) * (1 - expf(-ui_dt * 6));
    /* taille : la jaquette du pack (192 x 256 : 3 x 4) remplit la face avant */
    f32 H = h, W = H * 0.75f;
    if (W > w) { W = w; H = W / 0.75f; }
    f32 sc = 1 - 0.28f * bx_open;                        /* (ouverte : plus petite et a droite, le couvercle reste a l'ecran) */
    H *= sc; W *= sc;
    f32 D = W * 0.105f;                                  /* (boitier GameCube : 14 mm pour 135) */
    bx_cx = x + w / 2 + bx_open * w * 0.2f; bx_cy = y + h / 2;
    f32 X = W / 2, Y = H / 2, Z = D / 2;
    bv3 A = { -X, -Y, -Z }, B = { X, -Y, -Z }, C = { X, Y, -Z }, Dd = { -X, Y, -Z };    /* (devant : z = -Z, vers nous) */
    bv3 A2 = { -X, -Y, Z }, B2 = { X, -Y, Z }, C2 = { X, Y, Z }, D2 = { -X, Y, Z };
    col_t plast = RGBA(30, 24, 40, 255), side = RGBA(44, 36, 60, 255), spine = au_mix(0.3f, 255);
    f32 u1 = big ? 192.0f / 256.0f : 96.0f / 128.0f, fu0 = 0;
    const f32 SP = 0.05f, ub = (1 - SP) / 2, uf = (1 + SP) / 2;   /* (jaquette complete : dos 0..ub, tranche ub..uf, avant uf..1) */
    if (full) { cov = full; fu0 = uf; u1 = 1; }
    u8 kb = full ? 1 : 2, ks = full ? 1 : 0;
    bx_nf = 0;
    /* ombre au sol */
    gx_ellipse(bx_cx + 6, bx_cy + H * 0.52f, W * 0.75f, H * 0.07f, RGBA(0, 0, 0, 110), RGBA(0, 0, 0, 0));
    if (bx_open < 0.02f) {                                /* fermée : 6 faces */
        bx_add(A, B, C, Dd, 0, 0, 0, cov ? 1 : 4, cov, fu0, 0, u1, 1, plast);          /* devant : la jaquette */
        bx_add(B2, A2, D2, C2, 0, 0, 0, kb, full, 0, 0, ub, 1, RGBA(36, 28, 62, 255)); /* dos */
        bx_add(A2, A, Dd, D2, 0, 0, 0, ks, full, ub, 0, uf, 1, spine);                   /* tranche (a gauche) */
        bx_add(B, B2, C2, C, 0, 0, 0, 0, NULL, 0, 0, 0, 0, side);
        bx_add(A2, B2, B, A, 0, 0, 0, 0, NULL, 0, 0, 0, 0, side);                     /* dessus */
        bx_add(Dd, C, C2, D2, 0, 0, 0, 0, NULL, 0, 0, 0, 0, plast);                   /* dessous */
    } else {                                              /* ouverte : le bac, et le couvercle qui pivote sur la tranche */
        f32 t = D * 0.18f;
        bv3 I0 = { -X, -Y, -Z + t }, I1 = { X, -Y, -Z + t }, I2 = { X, Y, -Z + t }, I3 = { -X, Y, -Z + t };
        bx_add(I0, I1, I2, I3, 0, 0, 0, 0, NULL, 0, 0, 0, 0, RGBA(18, 14, 26, 255));  /* fond du bac (le disque dessus) */
        bx_add(B2, A2, D2, C2, 0, 0, 0, kb, full, 0, 0, ub, 1, RGBA(36, 28, 62, 255));
        bx_add(A2, I0, I3, D2, 0, 0, 0, ks, full, ub, 0, ub + (uf - ub) * 0.82f, 1, spine);
        bx_add(I1, B2, C2, I2, 0, 0, 0, 0, NULL, 0, 0, 0, 0, side);
        bx_add(A2, B2, I1, I0, 0, 0, 0, 0, NULL, 0, 0, 0, 0, side);
        bx_add(I3, I2, C2, D2, 0, 0, 0, 0, NULL, 0, 0, 0, 0, plast);
        bv3 L0 = { -X, -Y, -Z }, L1 = { X, -Y, -Z }, L2 = { X, Y, -Z }, L3 = { -X, Y, -Z };   /* couvercle (exterieur : la jaquette) */
        bv3 M0 = { -X, -Y, -Z + t }, M1 = { X, -Y, -Z + t }, M2 = { X, Y, -Z + t }, M3 = { -X, Y, -Z + t };
        bx_add(L0, L1, L2, L3, 1, -X, -Z, cov ? 1 : 4, cov, fu0, 0, u1, 1, plast);
        bx_add(M1, M0, M3, M2, 1, -X, -Z, 0, NULL, 0, 0, 0, 0, RGBA(92, 70, 156, 255)); /* interieur du couvercle */
    }
    /* de la plus loin a la plus proche */
    for (int i = 1; i < bx_nf; i++) for (int j = i; j > 0 && bx_f[j].depth > bx_f[j - 1].depth; j--) { bx_face_t tmp = bx_f[j]; bx_f[j] = bx_f[j - 1]; bx_f[j - 1] = tmp; }
    int disc_done = 0;
    for (int i = 0; i < bx_nf; i++) {
        f32 vis = bx_draw_face(&bx_f[i]);
        if (bx_open >= 0.02f && !disc_done && bx_f[i].kind == 0 && bx_f[i].c.r == 18)   /* (le disque juste apres le fond du bac)*/ { bx_disc(W * 0.4f, -Z + D * 0.18f - 0.5f, bx_open); disc_done = 1; }
        if (bx_f[i].kind == 4 && vis > 0.6f) {           /* (devant sans jaquette : banniere, ID et nom) */
            f32 xs[4], ys[4], mx = 0, my = 0;
            for (int q = 0; q < 4; q++) { bx_proj(bx_f[i].p[q], &xs[q], &ys[q]); mx += xs[q] / 4; my += ys[q] / 4; }
            f32 fw = fabsf(xs[1] - xs[0]);
            u8 a = (u8)(255 * (vis - 0.6f) / 0.4f);
            GXTexObj *bn = gl_banner(k);
            if (bn) draw_tex_fit(bn, 96, 32, 96, 32, mx - fw / 2 + 6, my - 30, fw - 12, 30);
            gx_text_c(F_SMALL, mx, my + 20, gl[k].id, RGBA(255, 210, 120, a));
            char fv[120]; fit_cut(F_SMALL, gl[k].title, (int)fw - 10, 1, fv, sizeof(fv));
            gx_text_c(F_SMALL, mx, my + 40, fv, RGBA(225, 215, 250, a));
        }
        if (bx_f[i].kind == 2 && vis > 0.55f) {           /* dos : le résumé, quand il nous fait face */
            f32 xs[4], ys[4];
            for (int q = 0; q < 4; q++) bx_proj(bx_f[i].p[q], &xs[q], &ys[q]);
            f32 x0 = xs[0], x1 = xs[0], y0 = ys[0];
            for (int q = 1; q < 4; q++) { if (xs[q] < x0) x0 = xs[q]; if (xs[q] > x1) x1 = xs[q]; if (ys[q] < y0) y0 = ys[q]; }
            u8 a = (u8)(255 * (vis - 0.55f) / 0.45f);
            const char *syn = gl_fx_syn(gl[k].id);
            gl_wrap(F_SMALL, gl[k].title, x0 + 10, y0 + 22, (int)(x1 - x0 - 20), 15, 2, RGBA(255, 255, 255, a));
            if (gl_bigst.game == k && gl_bigst.tex) draw_tex_fit(&gl_bigst.obj, 320, 240, 320, 240, x0 + 10, y0 + 46, x1 - x0 - 20, (x1 - x0 - 20) * 0.6f);
            if (syn || gl[k].desc) gl_wrap(F_SMALL, syn ? syn : gl[k].desc, x0 + 10, y0 + 54 + (gl_bigst.game == k && gl_bigst.tex ? (x1 - x0 - 20) * 0.6f : 0), (int)(x1 - x0 - 20), 13, 6, RGBA(220, 212, 245, a));
        }
    }
    if (bx_open > 0.6f) {
        char fv[120]; fit_cut(F_SMALL, gl[k].title, (int)(W * 0.8f), 1, fv, sizeof(fv));
        gx_text_c(F_SMALL, bx_cx, bx_cy + W * 0.46f, fv, RGBA(255, 255, 255, (u8)(255 * (bx_open - 0.6f) / 0.4f)));
    }
}
/* (fiche, vitrine) ↑ : ouvrir / fermer ; rend 1 si la touche est prise */
static int bx_input(u32 down) {
    if (down & PAD_BUTTON_UP) { bx_open_to = bx_open_to > 0.5f ? 0 : 1; bx_touch = now_ms(); return 1; }
    if ((down & PAD_BUTTON_DOWN) && bx_open_to > 0.5f) { bx_open_to = 0; return 1; }
    return 0;
}

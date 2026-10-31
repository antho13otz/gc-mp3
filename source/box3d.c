// GC Station v282 : la boîte du jeu en 3D (fiche d'un jeu dans la vitrine). Inclus dans main.c, avant gspace.c.
// v290 : aspect d'un vrai boîtier (plastique noir) ; dedans : clips de la notice dans le couvercle, bac avec le support
//        du disque, la place de la carte mémoire, et l'image du vrai disque (GameTDB). Ouverture comme avant.
// v283 : avec la jaquette complète de GameTDB (dos | tranche | avant) quand elle existe : les 3 faces sont les vraies.
// Une boîte GameCube aux proportions des jaquettes (3 x 4, comme le pack : 192 x 256), la jaquette devant, la tranche,
// le dos (résumé, genre, joueurs, PEGI, capture). Elle tourne doucement ; le stick C la fait tourner. ↑ : elle s'ouvre
// (le couvercle pivote sur la tranche), le disque s'allume dedans ; A : prendre le disque et jouer ; ↓ ou ↑ : fermer.
// Tout est projeté par le processeur (comme le globe) puis dessiné en 2D, de la face la plus loin à la plus proche.

static f32 bx_yaw = 28, bx_pitch = 7, bx_open = 0, bx_open_to = 0, bx_spin = 0;
static f32 bx_zoom = 1, bx_zoom_to = 1, bx_panx = 0, bx_pany = 0;   /* v284 : zoom (stick C haut / bas), deplacement (stick) */
static u64 bx_touch = 0;                                  /* (dernier coup de stick : la boîte arrête de tourner seule) */
static int bx_game = -1;
typedef struct { f32 x, y, z; } bv3;
static GXTexObj *nt_cover(int k, int *tw, int *th);       /* v296 : la notice (notice.c) */
static f32 bx_cx, bx_cy, bx_F = 760, bx_gap = 0;
#define BV(x, y, z) ((bv3){ (x), (y), (z) })

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
static bx_face_t bx_f[20];
static int bx_nf;
static void bx_add(bv3 a, bv3 b, bv3 c, bv3 d, int lid, f32 hx, f32 hz, u8 kind, GXTexObj *t, f32 u0, f32 v0, f32 u1, f32 v1, col_t col) {
    if (bx_nf >= 20) return;
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
static void bx_disc(f32 r, f32 cy, f32 z, f32 glow) {
    enum { N = 48 };
    f32 px[N + 1], py[N + 1], cxp, cyp;
    bv3 c0 = { 0, cy, z };
    bx_proj(bx_rot(c0), &cxp, &cyp);
    static const f32 ring[4] = { 1.0f, 0.86f, 0.55f, 0.22f };
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i <= N; i++) { f32 a = (f32)i / N * 6.2832f; bv3 p = { cosf(a) * r * ring[k], cy + sinf(a) * r * ring[k], z }; bx_proj(bx_rot(p), &px[i], &py[i]); }
        col_t cc = k == 0 ? RGBA(220, 222, 236, 255) : k == 1 ? au_mix(0.2f, 255) : k == 2 ? au_mix(0.8f, 255) : RGBA(230, 230, 240, 255);
        col_t ce = k == 0 ? RGBA(160, 160, 182, 255) : k == 1 ? au_mix(0.35f, 255) : k == 2 ? au_mix(0.6f, 255) : RGBA(190, 190, 205, 255);
        gx_set_flat();
        GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, N + 2);
        vtx(cxp, cyp, cc);
        for (int i = 0; i <= N; i++) vtx(px[i], py[i], ce);
        GX_End();
    }
    for (int i = 0; i <= N; i++) { f32 a = (f32)i / N * 6.2832f; bv3 p = { cosf(a) * r * 0.18f, cy + sinf(a) * r * 0.18f, z }; bx_proj(bx_rot(p), &px[i], &py[i]); }
    gx_set_flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, N + 2);
    vtx(cxp, cyp, RGBA(16, 12, 24, 255));
    for (int i = 0; i <= N; i++) vtx(px[i], py[i], RGBA(16, 12, 24, 255));
    GX_End();
    (void)glow;
}
/* v290 : decor plat dans la boite (quadrilatere sur un plan z), du couvercle (lid) ou du bac */
static void bx_q(f32 x0, f32 y0, f32 x1, f32 y1, f32 z, int lid, f32 hx, f32 hz, col_t c, col_t c2) {
    bv3 q[4] = { BV(x0, y0, z), BV(x1, y0, z), BV(x1, y1, z), BV(x0, y1, z) };
    f32 sx[4], sy[4];
    for (int i = 0; i < 4; i++) { if (lid) q[i] = bx_lid(q[i], hx, hz); bx_proj(bx_rot(q[i]), &sx[i], &sy[i]); }
    gx_poly4(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], sx[3], sy[3], c, c, c2, c2);
}
/* v296 : image sur un plan de la boite (la notice dans le couvercle) ; u0 du cote x0 */
static void bx_tq(GXTexObj *t, f32 x0, f32 y0, f32 x1, f32 y1, f32 z, int lid, f32 hx, f32 hz) {
    enum { S = 4 };
    gx_set_image(t);
    GX_Begin(GX_QUADS, GX_VTXFMT0, S * S * 4);
    for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
        f32 uu[4] = { (f32)i / S, (f32)(i + 1) / S, (f32)(i + 1) / S, (f32)i / S }, vv[4] = { (f32)j / S, (f32)j / S, (f32)(j + 1) / S, (f32)(j + 1) / S };
        for (int q = 0; q < 4; q++) {
            bv3 p = BV(x0 + (x1 - x0) * uu[q], y0 + (y1 - y0) * vv[q], z);
            if (lid) p = bx_lid(p, hx, hz);
            f32 sx, sy; bx_proj(bx_rot(p), &sx, &sy);
            GX_Position2f32(UX(sx), sy); GX_Color4u8(235, 235, 235, 255); GX_TexCoord2f32(uu[q], vv[q]);
        }
    }
    GX_End();
}
/* v290 : le support du disque (hexagone), la carte memoire, et le disque (image GameTDB, ou dessine) */
static void bx_tray(f32 W, f32 H, f32 X, f32 Y, f32 zf, GXTexObj *disc, int k) {
    const f32 hy = -0.05f * H, ro = 0.40f * W, ri = 0.355f * W, z = zf - 0.4f;
    for (int i = 0; i < 6; i++) {                        /* hexagone en relief */
        f32 a0 = (f32)M_PI / 6 + i * (f32)M_PI / 3, a1 = a0 + (f32)M_PI / 3;
        bv3 q[4] = { BV(cosf(a0) * ro, hy + sinf(a0) * ro, z), BV(cosf(a1) * ro, hy + sinf(a1) * ro, z), BV(cosf(a1) * ri, hy + sinf(a1) * ri, z), BV(cosf(a0) * ri, hy + sinf(a0) * ri, z) };
        f32 sx[4], sy[4];
        for (int j = 0; j < 4; j++) bx_proj(bx_rot(q[j]), &sx[j], &sy[j]);
        u8 l = (u8)(58 + 14 * sinf(a0 + 0.5f));
        gx_poly4(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], sx[3], sy[3], RGBA(l + 12, l + 12, l + 18, 255), RGBA(l + 12, l + 12, l + 18, 255), RGBA(l - 10, l - 10, l - 6, 255), RGBA(l - 10, l - 10, l - 6, 255));
    }
    /* place de la carte memoire (en bas a gauche) */
    bx_q(-X + 0.09f * W, 0.19f * H, -X + 0.27f * W, 0.27f * H, z, 0, 0, 0, RGBA(74, 74, 82, 255), RGBA(60, 60, 68, 255));
    bx_q(-X + 0.10f * W, 0.20f * H, -X + 0.26f * W, 0.26f * H, z - 0.1f, 0, 0, 0, RGBA(18, 18, 22, 255), RGBA(26, 26, 30, 255));
    /* le disque (mini-DVD de 8 cm : 0,6 de la largeur) */
    const f32 r = 0.30f * W, zd = z - 0.6f;
    if (disc) {
        enum { S = 4 };
        gx_set_image(disc);
        GX_Begin(GX_QUADS, GX_VTXFMT0, S * S * 4);
        for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
            f32 uu[4] = { (f32)i / S, (f32)(i + 1) / S, (f32)(i + 1) / S, (f32)i / S }, vv[4] = { (f32)j / S, (f32)j / S, (f32)(j + 1) / S, (f32)(j + 1) / S };
            for (int q = 0; q < 4; q++) {
                f32 sx, sy; bx_proj(bx_rot(BV(-r + 2 * r * uu[q], hy - r + 2 * r * vv[q], zd)), &sx, &sy);
                GX_Position2f32(UX(sx), sy); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(uu[q], vv[q]);
            }
        }
        GX_End();
    } else {
        bx_disc(r, hy, zd, 1);
        char fv[120]; fit_cut(F_SMALL, gl[k].title, (int)(r * 1.3f), 1, fv, sizeof(fv));
        f32 sx, sy; bx_proj(bx_rot(BV(0, hy + r * 0.62f, zd)), &sx, &sy);
        gx_text_c(F_SMALL, sx, sy + 4, fv, RGBA(30, 24, 50, 255));
    }
    /* l'attache du centre */
    bx_q(-0.035f * W, hy - 0.05f * W, 0.035f * W, hy + 0.05f * W, zd - 0.3f, 0, 0, 0, RGBA(150, 150, 160, 255), RGBA(110, 110, 120, 255));
    bx_q(-0.012f * W, hy - 0.035f * W, 0.012f * W, hy + 0.035f * W, zd - 0.4f, 0, 0, 0, RGBA(40, 40, 46, 255), RGBA(30, 30, 36, 255));
}

/* la boîte du jeu k dans le rectangle (x, y, w, h) de la fiche */
static void bx_draw(int k, f32 x, f32 y, f32 w, f32 h, GXTexObj *cov, int big, GXTexObj *full, GXTexObj *disc) {   /* v290 : disc : image du disque   /* full : jaquette complete (dos | tranche | avant), v283 */
    if (k != bx_game) { bx_game = k; bx_open = bx_open_to = 0; bx_zoom_to = 1; bx_panx = bx_pany = 0; }
    u64 now = now_ms();
    int sx = PAD_SubStickX(0), sy = PAD_SubStickY(0);
    f32 dt = ui_dt > 0.1f ? 0.1f : ui_dt;
    if (abs(sx) > 30) { bx_yaw += sx / 100.0f * 220 * dt; bx_touch = now; }
    if (abs(sy) > 40) { bx_zoom_to += sy / 100.0f * 1.6f * dt; if (bx_zoom_to < 1) bx_zoom_to = 1; if (bx_zoom_to > 3) bx_zoom_to = 3; bx_touch = now; }   /* v284 : zoom */
    if (bx_zoom_to > 1.03f) {                            /* (zoome : le stick deplace la boite) */
        int mx = PAD_StickX(0), my = PAD_StickY(0);
        if (abs(mx) > 20) bx_panx -= mx / 100.0f * 420 * dt;
        if (abs(my) > 20) bx_pany += my / 100.0f * 420 * dt;
    } else { bx_panx *= 1 - (1 - expf(-dt * 8)); bx_pany *= 1 - (1 - expf(-dt * 8)); }
    bx_zoom += (bx_zoom_to - bx_zoom) * (1 - expf(-dt * 8));
    if (bx_pitch > 40) bx_pitch = 40; if (bx_pitch < -30) bx_pitch = -30;
    if (now - bx_touch > 4000 && bx_open_to < 0.5f && bx_zoom_to < 1.03f) {   /* (v287 : pas quand elle est zoomee) */    /* (seule : un lent balancement autour de 3/4 face) */
        bx_spin += ui_dt;
        f32 target = 28 + sinf(bx_spin * 0.55f) * 16;
        while (bx_yaw - target > 180) bx_yaw -= 360;
        while (bx_yaw - target < -180) bx_yaw += 360;
        bx_yaw += (target - bx_yaw) * (1 - expf(-ui_dt * 1.5f));
        bx_pitch += (7 - bx_pitch) * (1 - expf(-ui_dt * 1.5f));
    }
    if (bx_open_to > 0.5f && now - bx_touch > 600) {     /* (ouverte : face a nous) */
        while (bx_yaw > 180) bx_yaw -= 360;
        while (bx_yaw < -180) bx_yaw += 360;
        bx_yaw += (-6 - bx_yaw) * (1 - expf(-ui_dt * 4)); bx_pitch += (6 - bx_pitch) * (1 - expf(-ui_dt * 4));
    }
    bx_open += (bx_open_to - bx_open) * (1 - expf(-ui_dt * 6));
    /* taille : la jaquette du pack (192 x 256 : 3 x 4) remplit la face avant */
    f32 H = h, W = H * 0.75f;
    if (W > w) { W = w; H = W / 0.75f; }
    f32 sc = 1 - 0.28f * bx_open;                        /* (ouverte : plus petite et a droite, le couvercle reste a l'ecran) */
    H *= sc; W *= sc;
    f32 ez = bx_zoom;
    bx_F = 760 * (ez > 1 ? ez : 1);                      /* (meme perspective en grand) */
    bx_cx = x + w / 2 + bx_open * w * 0.2f; bx_cy = y + h / 2;
    f32 D = W * 0.105f;                                  /* (boitier GameCube : 14 mm pour 135) */
    if (ez > 1.001f) {                                   /* v284 : zoom : plus grande, au milieu de l'ecran */
        f32 t = (ez - 1) / 0.35f; if (t > 1) t = 1;
        H *= ez; W *= ez; D = W * 0.105f;
        f32 ow = 0;
        f32 lx = (W + ow) * 0.5f - 250 > 0 ? (W + ow) * 0.5f - 250 : 0, ly = H * 0.5f - 190 > 0 ? H * 0.5f - 190 : 0;   /* (deplacement : jusqu'aux bords) */
        if (bx_panx > lx) bx_panx = lx; if (bx_panx < -lx) bx_panx = -lx;
        if (bx_pany > ly) bx_pany = ly; if (bx_pany < -ly) bx_pany = -ly;
        bx_cx += (320 + ow * 0.5f + bx_panx - bx_cx) * t; bx_cy += (236 + bx_pany - bx_cy) * t;
    }
    f32 X = W / 2, Y = H / 2, Z = D / 2;
    bv3 A = { -X, -Y, -Z }, B = { X, -Y, -Z }, C = { X, Y, -Z }, Dd = { -X, Y, -Z };    /* (devant : z = -Z, vers nous) */
    bv3 A2 = { -X, -Y, Z }, B2 = { X, -Y, Z }, C2 = { X, Y, Z }, D2 = { -X, Y, Z };
    col_t plast = RGBA(26, 26, 30, 255), side = RGBA(40, 40, 46, 255), spine = RGBA(34, 34, 40, 255);   /* (v290 : plastique noir, comme le vrai) */
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
    } else {                                              /* v290 : ouverte : le bac (arriere), le couvercle qui pivote a plat sur la tranche */
        f32 t = D * 0.12f, zf = Z - t;                    /* (fond du bac : l'interieur de l'arriere) */
        bx_add(BV(-X, -Y, zf), BV(X, -Y, zf), BV(X, Y, zf), BV(-X, Y, zf), 0, 0, 0, 5, NULL, 0, 0, 0, 0, RGBA(38, 38, 44, 255));
        col_t rim = RGBA(62, 62, 70, 255);                /* (le haut des parois, vu de face) */
        bx_add(BV(-X, -Y, -Z), BV(X, -Y, -Z), BV(X, -Y + t, -Z), BV(-X, -Y + t, -Z), 0, 0, 0, 0, NULL, 0, 0, 0, 0, rim);
        bx_add(BV(-X, Y - t, -Z), BV(X, Y - t, -Z), BV(X, Y, -Z), BV(-X, Y, -Z), 0, 0, 0, 0, NULL, 0, 0, 0, 0, rim);
        bx_add(BV(-X, -Y + t, -Z), BV(-X + t, -Y + t, -Z), BV(-X + t, Y - t, -Z), BV(-X, Y - t, -Z), 0, 0, 0, 0, NULL, 0, 0, 0, 0, rim);
        bx_add(BV(X - t, -Y + t, -Z), BV(X, -Y + t, -Z), BV(X, Y - t, -Z), BV(X - t, Y - t, -Z), 0, 0, 0, 0, NULL, 0, 0, 0, 0, rim);
        bx_add(B2, A2, D2, C2, 0, 0, 0, kb, full, 0, 0, ub, 1, RGBA(36, 36, 42, 255));   /* dos (dehors) */
        bx_add(A2, A, Dd, D2, 0, 0, 0, ks, full, ub, 0, uf, 1, spine);                  /* tranche (dehors) */
        bx_add(B, B2, C2, C, 0, 0, 0, 0, NULL, 0, 0, 0, 0, side);
        bx_add(A2, B2, B, A, 0, 0, 0, 0, NULL, 0, 0, 0, 0, side);
        bx_add(Dd, C, C2, D2, 0, 0, 0, 0, NULL, 0, 0, 0, 0, plast);
        bx_add(A, B, C, Dd, 1, -X, -Z, cov ? 1 : 4, cov, fu0, 0, u1, 1, plast);         /* couvercle : dehors, la jaquette */
        bx_add(BV(X, -Y, -Z + t), BV(-X, -Y, -Z + t), BV(-X, Y, -Z + t), BV(X, Y, -Z + t), 1, -X, -Z, 6, NULL, 0, 0, 0, 0, RGBA(46, 46, 52, 255));   /* dedans */
    }
    /* de la plus loin a la plus proche */
    for (int i = 1; i < bx_nf; i++) for (int j = i; j > 0 && bx_f[j].depth > bx_f[j - 1].depth; j--) { bx_face_t tmp = bx_f[j]; bx_f[j] = bx_f[j - 1]; bx_f[j - 1] = tmp; }
    int disc_done = 0;
    for (int i = 0; i < bx_nf; i++) {
        f32 vis = bx_draw_face(&bx_f[i]);
        if (bx_f[i].kind == 5 && vis > 0 && !disc_done) { bx_tray(W, H, X, Y, Z - D * 0.12f, disc, k); disc_done = 1; }   /* v290 : support, carte, disque */
        if (bx_f[i].kind == 6 && vis > 0) {               /* v290 : couvercle dedans : les deux clips de la notice */
            f32 zc = -Z + D * 0.12f + 0.4f;
            int ntw, nth;
            GXTexObj *nc = nt_cover(k, &ntw, &nth);       /* v296 : la notice, tenue par les clips */
            if (nc) {
                f32 nh = H * 0.90f, nw = nh * ntw / nth;
                if (nw > W * 0.86f) { nw = W * 0.86f; nh = nw * nth / ntw; }
                bx_tq(nc, nw / 2, -nh / 2, -nw / 2, nh / 2, zc - 0.2f, 1, -X, -Z);   /* (ouverte : le cote x > 0 passe a gauche) */
            }
            bx_q(X - 0.20f * W, -Y + 0.05f * H, X - 0.05f * W, -Y + 0.11f * H, zc, 1, -X, -Z, RGBA(84, 84, 92, 255), RGBA(64, 64, 72, 255));
            bx_q(X - 0.20f * W, Y - 0.11f * H, X - 0.05f * W, Y - 0.05f * H, zc, 1, -X, -Z, RGBA(84, 84, 92, 255), RGBA(64, 64, 72, 255));
        }
        if (bx_f[i].kind == 7 && vis > 0) {               /* v290 : tranche dedans : une nervure */
            f32 xm = -X - bx_gap * bx_open * 0.5f, zt = -Z + D * 0.06f - 0.3f;
            bx_q(xm - 0.6f, -Y + 0.04f * H, xm + 0.6f, Y - 0.04f * H, zt, 0, 0, 0, RGBA(58, 58, 66, 255), RGBA(58, 58, 66, 255));
        }
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
}
/* (fiche, vitrine) ↑ : ouvrir / fermer ; rend 1 si la touche est prise */
static int bx_zoomed(void) { return bx_zoom > 1.03f || bx_zoom_to > 1.03f; }
static int bx_input(u32 down) {
    if (bx_zoomed()) {                                   /* v284 : zoome : B revient, A joue, le reste ne change pas de jeu */
        if (down & PAD_BUTTON_B) { bx_zoom_to = 1; return 1; }
        if (down & PAD_BUTTON_A) return 0;
        if (down & PAD_BUTTON_UP) { bx_open_to = bx_open_to > 0.5f ? 0 : 1; bx_touch = now_ms(); }
        if ((down & PAD_BUTTON_DOWN) && bx_open_to > 0.5f) bx_open_to = 0;
        return 1;
    }
    if (down & PAD_BUTTON_UP) { bx_open_to = bx_open_to > 0.5f ? 0 : 1; bx_touch = now_ms(); return 1; }
    if ((down & PAD_BUTTON_DOWN) && bx_open_to > 0.5f) { bx_open_to = 0; return 1; }
    return 0;
}

// GC-MP3 v39 : styles du spectre du lecteur (inclus dans main.c, avant les lecteurs)
// Le spectre (55 bandes, echelle mel) est dessine dans une zone x, y (bas), largeur w, hauteur h.
// Styles : LED (classique), barres, miroir, courbe, spectrogramme (defilant),
// oscilloscope (forme d'onde), cercle. START > Reglages > GC-MP3 > Style du spectre.

static float vz_disp[NBARS], vz_peak[NBARS], vz_peak_v[NBARS];
static u64 vz_last = 0;
#define VZ_HW 96                                   /* colonnes gardees pour le spectrogramme */
#define VZ_HR 28                                   /* lignes (bandes regroupees) */
static u8 vz_hist[VZ_HW][VZ_HR];
static int vz_hpos = 0;
static u64 vz_hlast = 0;

/* lissage a l'affichage (60 images / s) + crete qui retombe doucement */
static void vz_step(void) {
    u64 t = now_ms();
    if (t == vz_last) return;
    f32 dt = vz_last ? (f32)(t - vz_last) / 16.67f : 1.0f;
    if (dt > 4.0f) dt = 4.0f;
    vz_last = t;
    for (int i = 0; i < NBARS; i++) {
        f32 v = bar_val[i];
        f32 k = v > vz_disp[i] ? 0.65f : 0.22f;             /* monte vite, descend en douceur */
        vz_disp[i] += (v - vz_disp[i]) * (k * dt > 1.0f ? 1.0f : k * dt);
        if (vz_disp[i] >= vz_peak[i]) { vz_peak[i] = vz_disp[i]; vz_peak_v[i] = 0.0f; }
        else { vz_peak_v[i] += 0.0009f * dt; vz_peak[i] -= vz_peak_v[i] * dt; if (vz_peak[i] < 0.0f) vz_peak[i] = 0.0f; }
    }
    /* spectrogramme : une colonne toutes les 33 ms */
    if (t - vz_hlast >= 33) {
        vz_hlast = t;
        int any = 0;
        for (int r = 0; r < VZ_HR; r++) {
            int b0 = r * NBARS / VZ_HR, b1 = (r + 1) * NBARS / VZ_HR;
            f32 m = 0.0f;
            for (int b = b0; b < b1 && b < NBARS; b++) if (vz_disp[b] > m) m = vz_disp[b];
            vz_hist[vz_hpos][r] = (u8)(m * 255.0f);
            if (m > 0.02f) any = 1;
        }
        (void)any;
        vz_hpos = (vz_hpos + 1) % VZ_HW;
    }
}
/* couleurs : violet -> bleu de gauche a droite (comme avant), plus clair en haut */
static col_t vz_col(f32 t) { return lerp_c(RGBA(156, 60, 255, 255), RGBA(70, 130, 255, 255), t * 1.25f - 0.15f); }
static col_t vz_heat(f32 v) {                      /* spectrogramme : nuit -> violet -> bleu -> cyan -> blanc */
    if (v < 0.25f) return lerp_c(RGBA(10, 6, 30, 255), RGBA(90, 30, 170, 255), v / 0.25f);
    if (v < 0.5f) return lerp_c(RGBA(90, 30, 170, 255), RGBA(70, 110, 255, 255), (v - 0.25f) / 0.25f);
    if (v < 0.8f) return lerp_c(RGBA(70, 110, 255, 255), RGBA(110, 230, 255, 255), (v - 0.5f) / 0.3f);
    return lerp_c(RGBA(110, 230, 255, 255), RGBA(255, 250, 255, 255), (v - 0.8f) / 0.2f);
}
static col_t vz_alpha(col_t c, int a) { c.a = (u8)(a < 0 ? 0 : a > 255 ? 255 : a); return c; }

static void draw_viz(f32 x, f32 base, f32 w, f32 h) {
    vz_step();
    const f32 bw = w / NBARS;
    switch (viz_style) {
    default:
    case VZ_LED: {                                   /* LED : cases empilees + crete + reflet */
        int segs = (int)(h / 3.0f);
        for (int i = 0; i < NBARS; i++) {
            f32 bx = x + i * bw, tcol = (f32)i / (NBARS - 1);
            col_t cb = vz_col(tcol);
            int nb = 1 + (int)(vz_disp[i] * segs);
            if (nb > segs) nb = segs;
            for (int j = 0; j < nb; j++) gx_rect(bx, base - 3.0f * (j + 1), bw - 1.6f, 2, lerp_c(cb, RGBA(255, 196, 255, 255), (f32)j / (segs + 2)));
            int pj = (int)(vz_peak[i] * segs);
            if (pj >= nb && pj < segs) gx_rect(bx, base - 3.0f * (pj + 1), bw - 1.6f, 2, RGBA(255, 230, 255, 220));
            for (int j = 0; j < nb && j < 7; j++) gx_rect(bx, base + 2.0f + 3.0f * j, bw - 1.6f, 2, vz_alpha(cb, 80 - j * 11));
        }
        gx_hgrad(x, base, w, 1, RGBA(130, 80, 255, 70), RGBA(80, 140, 255, 70));
        break; }
    case VZ_BARS: {                                  /* barres pleines en degrade + crete */
        for (int i = 0; i < NBARS; i++) {
            f32 bx = x + i * bw, tcol = (f32)i / (NBARS - 1), bh = 2.0f + vz_disp[i] * (h - 2.0f);
            col_t cb = vz_col(tcol), ct = lerp_c(cb, RGBA(255, 210, 255, 255), vz_disp[i] * 0.8f);
            gx_quad4(bx, base - bh, bx + bw - 1.2f, base, ct, ct, cb, cb);
            f32 py = base - 2.0f - vz_peak[i] * (h - 2.0f);
            gx_rect(bx, py - 2.0f, bw - 1.2f, 2, RGBA(255, 235, 255, 230));
            f32 rh = bh * 0.35f;                     /* reflet */
            gx_quad4(bx, base + 2, bx + bw - 1.2f, base + 2 + rh, vz_alpha(cb, 90), vz_alpha(cb, 90), vz_alpha(cb, 0), vz_alpha(cb, 0));
        }
        break; }
    case VZ_MIRROR: {                                /* miroir : de part et d'autre d'une ligne */
        f32 cy = base - h * 0.5f + 6.0f, hh = h * 0.5f + 4.0f;
        for (int i = 0; i < NBARS; i++) {
            f32 bx = x + i * bw, tcol = (f32)i / (NBARS - 1), bh = 1.0f + vz_disp[i] * hh;
            col_t cb = vz_col(tcol), ct = lerp_c(cb, RGBA(255, 220, 255, 255), vz_disp[i]);
            gx_quad4(bx, cy - bh, bx + bw - 1.2f, cy, ct, ct, cb, cb);
            gx_quad4(bx, cy, bx + bw - 1.2f, cy + bh, cb, cb, vz_alpha(ct, 160), vz_alpha(ct, 160));
        }
        gx_hgrad(x, cy, w, 1, RGBA(255, 220, 255, 120), RGBA(200, 230, 255, 120));
        break; }
    case VZ_CURVE: {                                 /* courbe remplie et lissee */
        f32 px = x, py = base - vz_disp[0] * h;
        for (int i = 1; i <= NBARS * 2; i++) {
            f32 f = (f32)i / 2.0f; int a = (int)f; f32 t = f - a;
            if (a >= NBARS - 1) { a = NBARS - 2; t = 1.0f; }
            f32 v = vz_disp[a] * (1 - t) + vz_disp[a + 1] * t;
            t = t * t * (3 - 2 * t);                     /* (lissage) */
            v = vz_disp[a] * (1 - t) + vz_disp[a + 1] * t;
            f32 nx = x + w * i / (NBARS * 2), ny = base - v * h;
            col_t c = vz_col(nx / w - x / w), cf = vz_alpha(c, 70);
            gx_poly4(px, py, nx, ny, nx, base, px, base, cf, cf, vz_alpha(c, 10), vz_alpha(c, 10));
            col_t cl = lerp_c(c, RGBA(255, 230, 255, 255), v);
            gx_poly4(px, py - 1.2f, nx, ny - 1.2f, nx, ny + 1.2f, px, py + 1.2f, cl, cl, cl, cl);
            px = nx; py = ny;
        }
        gx_hgrad(x, base, w, 1, RGBA(130, 80, 255, 90), RGBA(80, 140, 255, 90));
        break; }
    case VZ_SGRAM: {                                 /* spectrogramme : le temps defile vers la gauche */
        f32 cw = w / VZ_HW, rh = h / VZ_HR;
        for (int c = 0; c < VZ_HW; c++) {
            int col = (vz_hpos + c) % VZ_HW;
            f32 cx = x + c * cw;
            for (int r = 0; r < VZ_HR; r++) {
                u8 v = vz_hist[col][r];
                if (v < 6) continue;
                gx_rect(cx, base - (r + 1) * rh, cw + 0.3f, rh + 0.3f, vz_heat(v / 255.0f));
            }
        }
        gx_hgrad(x, base, w, 1, RGBA(130, 80, 255, 90), RGBA(80, 140, 255, 90));
        break; }
    case VZ_SCOPE: {                                 /* oscilloscope : la forme d'onde */
        int n = 512, start = (viz_pos - n - 600) & (VIZ_N - 1);
        for (int k = 0; k < 500; k++) {              /* (cale sur un passage par zero, pour une image stable) */
            int a = (start + k) & (VIZ_N - 1), b = (a + 1) & (VIZ_N - 1);
            if (viz[a] < 0 && viz[b] >= 0) { start = b; break; }
        }
        int peak = 1;
        for (int k = 0; k < n; k++) { int v = viz[(start + k) & (VIZ_N - 1)]; if (v < 0) v = -v; if (v > peak) peak = v; }
        f32 gain = (h * 0.48f) / (peak < 3000 ? 3000 : peak);
        f32 cy = base - h * 0.5f;
        f32 px = x, py = cy - viz[start & (VIZ_N - 1)] * gain;
        for (int k = 1; k < n; k += 2) {
            f32 nx = x + w * k / n, ny = cy - viz[(start + k) & (VIZ_N - 1)] * gain;
            col_t c = vz_col((f32)k / n);
            f32 y0 = py < ny ? py : ny, y1 = py < ny ? ny : py;
            gx_rect(px, y0 - 1.0f, nx - px + 0.6f, y1 - y0 + 2.0f, lerp_c(c, RGBA(230, 220, 255, 255), 0.35f));
            px = nx; py = ny;
        }
        gx_hgrad(x, cy, w, 1, RGBA(130, 80, 255, 50), RGBA(80, 140, 255, 50));
        break; }
    case VZ_CIRCLE: {                                /* cercle : les bandes tout autour */
        f32 cx = x + w * 0.5f, cy = base - h * 0.5f + 2.0f, r0 = h * 0.26f + bass_lvl * 3.0f, len = h * 0.46f;
        gx_circle(cx, cy, r0 - 1.0f, RGBA(60, 30, 130, 200), RGBA(40, 20, 100, 120));
        for (int side = 0; side < 2; side++)
            for (int i = 0; i < NBARS; i++) {
                f32 a = -PI_F / 2 + (side ? -1.0f : 1.0f) * ((f32)i + 0.5f) / NBARS * PI_F;
                f32 ca = cosf(a), sa = sinf(a), l = 1.0f + vz_disp[i] * len;
                f32 ox = -sa * 1.1f, oy = ca * 1.1f;
                f32 x0 = cx + ca * r0, y0 = cy + sa * r0, x1 = cx + ca * (r0 + l), y1 = cy + sa * (r0 + l);
                col_t c = vz_col((f32)i / NBARS), ct = lerp_c(c, RGBA(255, 220, 255, 255), vz_disp[i]);
                gx_poly4(x0 - ox, y0 - oy, x1 - ox, y1 - oy, x1 + ox, y1 + oy, x0 + ox, y0 + oy, c, ct, ct, c);
            }
        break; }
    }
}

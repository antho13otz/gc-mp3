// GC Station v296 : la notice du jeu, rangée dans la boîte 3D. Inclus dans main.c, après box3d.c (avant gspace.c).
// Pages en JPEG sur la carte : SD:/gc-mp3/notices/<ID>/01.jpg, 02.jpg... (01 = la couverture). La couverture apparaît
// dans le couvercle de la boîte ouverte ; X (boîte ouverte) : la notice en grand. Gauche / droite : pages ; stick C :
// zoom ; stick : se déplacer ; B : refermer la notice. Lecture et décodage dans le thread de fond (pas de saccade).
// Aussi pour l'appli : /api/notice?id=<ID> (nombre de pages), &p=<n> (la page en JPEG) dans tab_phone.c.

static f32 gs_key(f32 x, f32 y, const char *k, const char *label, col_t c);   /* (gspace.c) */
#define NT_DIR GCD "/notices"
#define NT_MAXW 448
#define NT_MAXH 768
static char nt_id[8] = "";
static int nt_n = -1, nt_cnt_busy = 0;                    /* pages de la notice de nt_id (-1 : pas encore compte) */
static char nt_cid[8];
static int nt_cres;
static struct { int page; u16 *tex; GXTexObj obj; int tw, th; } nt_pg = { 0, NULL, {{0}}, 0, 0 }, nt_cv = { 0, NULL, {{0}}, 0, 0 };   /* page affichee ; couverture */
static int nt_busy = 0, nt_rpage, nt_rtw, nt_rth;
static char nt_rid[8];
static u16 *nt_rtex;
static int nt_on = 0, nt_cur = 1;
static f32 nt_zoom = 1, nt_zoom_to = 1, nt_px = 0, nt_py = 0, nt_slide = 0;

static void nt_path(const char *id, int p, char *o, int cap) { snprintf(o, cap, NT_DIR "/%.6s/%02d.jpg", id, p); }
static void nt_count_job(void) {                          /* (thread de fond) */
    char p[NAME_LEN]; struct stat st;
    int n = 0;
    while (n < 99) { nt_path(nt_cid, n + 1, p, sizeof(p)); if (stat(p, &st) != 0 || st.st_size <= 0) break; n++; }
    nt_cres = n;
}
static void nt_count_done(void) { nt_cnt_busy = 0; if (!strcmp(nt_cid, nt_id)) nt_n = nt_cres; }
/* JPEG -> texture RGB565 (au plus 448 x 768, multiple de 4) */
static void nt_read_job(void) {                           /* (thread de fond) */
    char p[NAME_LEN];
    nt_rtex = NULL;
    nt_path(nt_rid, nt_rpage, p, sizeof(p));
    long n = 0;
    u8 *d = read_file(p, 3 * 1024 * 1024, &n);
    if (!d) return;
    int w = 0, h = 0, c = 0;
    unsigned char *px = stbi_load_from_memory(d, (int)n, &w, &h, &c, 3);
    free(d);
    if (!px || w < 8 || h < 8) { if (px) stbi_image_free(px); return; }
    f32 s = 1;
    if (w * s > NT_MAXW) s = (f32)NT_MAXW / w;
    if (h * s > NT_MAXH) s = (f32)NT_MAXH / h;
    int tw = (int)(w * s) & ~3, th = (int)(h * s) & ~3;
    if (tw < 4 || th < 4) { stbi_image_free(px); return; }
    u16 *t = (u16 *)memalign(32, tw * th * 2);
    if (!t) { stbi_image_free(px); return; }
    for (int y = 0; y < th; y++) {
        int sy = (int)((y + 0.5f) * h / th);
        for (int x = 0; x < tw; x++) {
            const unsigned char *q = px + ((size_t)sy * w + (int)((x + 0.5f) * w / tw)) * 3;
            t[((y >> 2) * (tw >> 2) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)] = (u16)(((q[0] >> 3) << 11) | ((q[1] >> 2) << 5) | (q[2] >> 3));
        }
    }
    stbi_image_free(px);
    nt_rtex = t; nt_rtw = tw; nt_rth = th;
}
static void nt_read_done(void) {
    nt_busy = 0;
    u16 *t = nt_rtex; nt_rtex = NULL;
    if (!t) { if (!strcmp(nt_rid, nt_id) && nt_rpage == 1) nt_cv.page = -1; return; }
    if (strcmp(nt_rid, nt_id)) { free(t); return; }
    DCFlushRange(t, nt_rtw * nt_rth * 2);
    if (nt_rpage == 1 && !nt_cv.tex) {                   /* (la couverture : gardee pour la boite) */
        nt_cv.tex = t; nt_cv.page = 1; nt_cv.tw = nt_rtw; nt_cv.th = nt_rth;
        GX_InitTexObj(&nt_cv.obj, t, nt_rtw, nt_rth, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&nt_cv.obj, GX_LINEAR, GX_LINEAR);
    } else {
        free(nt_pg.tex);
        nt_pg.tex = t; nt_pg.page = nt_rpage; nt_pg.tw = nt_rtw; nt_pg.th = nt_rth;
        GX_InitTexObj(&nt_pg.obj, t, nt_rtw, nt_rth, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&nt_pg.obj, GX_LINEAR, GX_LINEAR);
    }
    GX_InvalidateTexAll();
}
static void nt_set_game(int k) {
    if (k < 0 || k >= gl_n || !strncmp(nt_id, gl[k].id, 6)) return;
    snprintf(nt_id, sizeof(nt_id), "%.6s", gl[k].id);
    nt_n = -1; nt_on = 0;
    free(nt_pg.tex); nt_pg.tex = NULL; nt_pg.page = 0;
    free(nt_cv.tex); nt_cv.tex = NULL; nt_cv.page = 0;
}
/* nombre de pages de la notice du jeu k (0 : pas de notice ; -1 : pas encore su) */
static int nt_pages(int k) {
    if (k < 0 || k >= gl_n || gl[k].dvd || !sd_ok || sd_virtual) return 0;
    nt_set_game(k);
    if (nt_n < 0 && !nt_cnt_busy) { snprintf(nt_cid, sizeof(nt_cid), "%s", nt_id); if (gj_post_fn(nt_count_job, nt_count_done)) nt_cnt_busy = 1; }
    return nt_n;
}
static void nt_want(int page) {                           /* demander une page (une lecture a la fois) */
    if (nt_busy || nt_n <= 0) return;
    snprintf(nt_rid, sizeof(nt_rid), "%s", nt_id); nt_rpage = page;
    if (gj_post_fn(nt_read_job, nt_read_done)) nt_busy = 1;
}
/* la couverture de la notice du jeu k (dans le couvercle) ; NULL : pas de notice ou pas encore lue */
static GXTexObj *nt_cover(int k, int *tw, int *th) {
    if (nt_pages(k) <= 0) return NULL;
    if (nt_cv.tex) { *tw = nt_cv.tw; *th = nt_cv.th; return &nt_cv.obj; }
    if (nt_cv.page == 0) nt_want(1);
    return NULL;
}
static int nt_input(u32 down, u32 nav, int fire) {
    if (!nt_on) {
        if ((down & PAD_BUTTON_X) && bx_open_to > 0.5f && nt_pages(gl_sel) > 0) {
            nt_on = 1; nt_cur = 1; nt_zoom = nt_zoom_to = 1; nt_px = nt_py = 0; nt_slide = 0;
            return 1;
        }
        return 0;
    }
    if (down & PAD_BUTTON_B) { nt_on = 0; return 1; }
    if (fire && (nav & PAD_BUTTON_RIGHT) && nt_cur < nt_n) { nt_cur++; nt_slide = 1; nt_px = nt_py = 0; }
    if (fire && (nav & PAD_BUTTON_LEFT) && nt_cur > 1) { nt_cur--; nt_slide = -1; nt_px = nt_py = 0; }
    return 1;                                            /* (le reste ne fait rien pendant la notice) */
}
/* la notice en grand ; rend 1 si elle est a l'ecran */
static int nt_draw(int k) {
    if (!nt_on) return 0;
    if (nt_pages(k) <= 0) { nt_on = 0; return 0; }
    f32 dt = ui_dt > 0.1f ? 0.1f : ui_dt;
    int sy = PAD_SubStickY(0);
    if (abs(sy) > 40) { nt_zoom_to += sy / 100.0f * 1.6f * dt; if (nt_zoom_to < 1) nt_zoom_to = 1; if (nt_zoom_to > 3) nt_zoom_to = 3; }
    if (nt_zoom_to > 1.03f) {
        int mx = PAD_StickX(0), my = PAD_StickY(0);
        if (abs(mx) > 20) nt_px -= mx / 100.0f * 420 * dt;
        if (abs(my) > 20) nt_py += my / 100.0f * 420 * dt;
    } else { nt_px *= expf(-dt * 8); nt_py *= expf(-dt * 8); }
    nt_zoom += (nt_zoom_to - nt_zoom) * (1 - expf(-dt * 8));
    nt_slide *= expf(-dt * 12); if (fabsf(nt_slide) < 0.003f) nt_slide = 0;
    GXTexObj *o = NULL; int tw = 0, th = 0;
    if (nt_pg.tex && nt_pg.page == nt_cur) { o = &nt_pg.obj; tw = nt_pg.tw; th = nt_pg.th; }
    else if (nt_cur == 1 && nt_cv.tex) { o = &nt_cv.obj; tw = nt_cv.tw; th = nt_cv.th; }
    else nt_want(nt_cur);
    gx_poly4(0, 0, 640, 0, 640, 480, 0, 480, RGBA(8, 6, 16, 250), RGBA(8, 6, 16, 250), RGBA(4, 3, 10, 250), RGBA(4, 3, 10, 250));
    if (o) {
        f32 h = 420 * nt_zoom, w = h * tw / th;
        f32 lx = w / 2 - 300 > 0 ? w / 2 - 300 : 0, ly = h / 2 - 205 > 0 ? h / 2 - 205 : 0;
        if (nt_px > lx) nt_px = lx; if (nt_px < -lx) nt_px = -lx;
        if (nt_py > ly) nt_py = ly; if (nt_py < -ly) nt_py = -ly;
        f32 x = 320 + nt_px - w / 2 + nt_slide * 160, y = 222 + nt_py - h / 2;
        gx_rrect_fill(x - 4, y - 4, w + 8, h + 8, 4, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120));
        draw_tex_fit(o, tw, th, tw, th, x, y, w, h);
    } else gx_text_c(F_MED, 320, 230, "Lecture de la page...", RGBA(200, 190, 235, 255));
    char b[48];
    snprintf(b, sizeof(b), nt_cur == 1 ? "Couverture  (1 / %d)" : "Page %d / %d", nt_cur == 1 ? nt_n : nt_cur, nt_n);
    gx_rrect_fill(20, 438, 600, 30, 15, RGBA(22, 16, 44, 245), RGBA(14, 10, 30, 245));
    gx_rrect_stroke(20, 438, 600, 30, 15, 1.0f, RGBA(255, 255, 255, 40));
    f32 x = 34;
    x = gs_key(x, 453, "\xE2\x86\x92", "Gauche / droite : pages", RGBA(150, 200, 255, 255));
    x = gs_key(x, 453, "C", "Zoom", RGBA(255, 210, 90, 255));
    x = gs_key(x, 453, "S", "D\xC3\xA9placer", RGBA(235, 235, 245, 255));
    gs_key(x, 453, "B", "Fermer la notice", RGBA(240, 110, 110, 255));
    gx_text_r(F_SMALL, 610, 458, b, RGBA(255, 210, 120, 255));
    return 1;
}

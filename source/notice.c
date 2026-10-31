// GC Station v296 : la notice du jeu, rangée dans la boîte 3D. Inclus dans main.c, après box3d.c (avant gspace.c).
// Pages en JPEG sur la carte : SD:/gc-mp3/notices/<ID>/01.jpg, 02.jpg... (01 = la couverture). La couverture apparaît
// dans le couvercle de la boîte ouverte ; X (boîte ouverte) : la notice en grand. Gauche / droite : pages ; stick C :
// zoom ; stick : se déplacer ; B : refermer la notice. Lecture et décodage dans le thread de fond (pas de saccade).
// v300 : la notice sort du couvercle en s'ouvrant (et y retourne) ; les pages tournent comme dans un vrai livret.
// Aussi pour l'appli : /api/notice?id=<ID> (nombre de pages), &p=<n> (la page en JPEG) dans tab_phone.c.
// v302 : la notice est un vrai livret en 3D qu'on manipule, fermee comme ouverte : stick : la tourner dans tous les
// sens ; C : zoom (zoomee, le stick la deplace) ; gauche / droite : tourner une feuille (elle se courbe en tournant
// autour de la reliure) ; A : ouvrir / refermer ; Y : de face ; B : la ranger. Ouverte : deux pages face a face.
// Dessin : feuilles projetees en perspective, faces vers nous seulement, de la plus loin a la plus proche.

static f32 gs_key(f32 x, f32 y, const char *k, const char *label, col_t c);   /* (gspace.c) */
#define NT_DIR GCD "/notices"
#define NT_MAXW 360                                        /* (v302 : 4 pages lues a la fois : un peu plus petites) */
#define NT_MAXH 616
#define NT_SLOTS 4
static char nt_id[8] = "";
static int nt_n = -1, nt_cnt_busy = 0;                    /* pages de la notice de nt_id (-1 : pas encore compte) */
static char nt_cid[8];
static int nt_cres;
typedef struct { int page; u16 *tex; GXTexObj obj; int tw, th; } ntpg_t;
static ntpg_t nt_cv = { 0, NULL, {{0}}, 0, 0 }, nt_s[NT_SLOTS];   /* couverture ; v302 : 4 pages (les deux ouvertes, la feuille qui tourne) */
static int nt_busy = 0, nt_rpage, nt_rtw, nt_rth, nt_rslot;
static char nt_rid[8];
static u16 *nt_rtex;
static int nt_on = 0, nt_closing = 0;
static f32 nt_zoom = 1, nt_zoom_to = 1, nt_px = 0, nt_py = 0, nt_open = 0;   /* v300 : ouverture (0-1) */
static f32 nt_pos = 0, nt_yaw = 0, nt_pitch = 0;          /* v302 : feuilles tournees (continu), orientation du livret */
static int nt_pos_to = 0, nt_face = 0;                    /* (feuilles a tourner ; 1 : revient de face) */
static int nt_need[6], nt_nneed = 0;                      /* (pages a l'ecran : leurs places ne sont pas reprises) */

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
    ntpg_t *d = nt_rpage == 1 && !nt_cv.tex ? &nt_cv : &nt_s[nt_rslot];   /* (la couverture : gardee pour la boite) */
    free(d->tex);
    d->tex = t; d->page = nt_rpage; d->tw = nt_rtw; d->th = nt_rth;
    GX_InitTexObj(&d->obj, t, nt_rtw, nt_rth, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&d->obj, GX_LINEAR, GX_LINEAR);
    GX_InvalidateTexAll();
}
static void nt_set_game(int k) {
    if (k < 0 || k >= gl_n || !strncmp(nt_id, gl[k].id, 6)) return;
    snprintf(nt_id, sizeof(nt_id), "%.6s", gl[k].id);
    nt_n = -1; nt_on = 0;
    for (int i = 0; i < NT_SLOTS; i++) { free(nt_s[i].tex); nt_s[i].tex = NULL; nt_s[i].page = 0; }
    free(nt_cv.tex); nt_cv.tex = NULL; nt_cv.page = 0;
}
/* nombre de pages de la notice du jeu k (0 : pas de notice ; -1 : pas encore su) */
static int nt_pages(int k) {
    if (k < 0 || k >= gl_n || gl[k].dvd || !sd_ok || sd_virtual) return 0;
    nt_set_game(k);
    if (nt_n < 0 && !nt_cnt_busy) { snprintf(nt_cid, sizeof(nt_cid), "%s", nt_id); if (gj_post_fn(nt_count_job, nt_count_done)) nt_cnt_busy = 1; }
    return nt_n;
}
/* la texture d'une page si elle est lue ; NULL : pas encore */
static GXTexObj *nt_tex(int page, int *tw, int *th) {
    if (page == 1 && nt_cv.tex) { *tw = nt_cv.tw; *th = nt_cv.th; return &nt_cv.obj; }
    for (int i = 0; i < NT_SLOTS; i++) if (nt_s[i].tex && nt_s[i].page == page) { *tw = nt_s[i].tw; *th = nt_s[i].th; return &nt_s[i].obj; }
    return NULL;
}
static void nt_want(int page) {                           /* demander une page (une lecture a la fois) */
    int tw, th;
    if (nt_busy || nt_n <= 0 || page < 1 || page > nt_n || nt_tex(page, &tw, &th)) return;
    int sl = -1;                                         /* (place : une libre, sinon une page qui n'est pas a l'ecran) */
    for (int i = 0; i < NT_SLOTS && sl < 0; i++) if (!nt_s[i].tex) sl = i;
    for (int i = 0; i < NT_SLOTS && sl < 0; i++) {
        int vu = 0;
        for (int j = 0; j < nt_nneed; j++) if (nt_need[j] == nt_s[i].page) vu = 1;
        if (!vu) sl = i;
    }
    if (sl < 0) return;
    snprintf(nt_rid, sizeof(nt_rid), "%s", nt_id); nt_rpage = page; nt_rslot = sl;
    if (gj_post_fn(nt_read_job, nt_read_done)) nt_busy = 1;
}
/* la couverture de la notice du jeu k (dans le couvercle) ; NULL : pas de notice ou pas encore lue */
static GXTexObj *nt_cover(int k, int *tw, int *th) {
    if (nt_pages(k) <= 0) return NULL;
    if (nt_cv.tex) { *tw = nt_cv.tw; *th = nt_cv.th; return &nt_cv.obj; }
    if (nt_cv.page == 0) nt_want(1);
    return NULL;
}
static int nt_leaves(void) { return nt_n > 1 ? (nt_n + 1) / 2 : 1; }
static int nt_input(u32 down, u32 nav, int fire) {
    if (!nt_on) {
        if ((down & PAD_BUTTON_X) && bx_open_to > 0.5f && nt_pages(gl_sel) > 0) {
            nt_on = 1; nt_closing = 0; nt_open = 0; nt_zoom = nt_zoom_to = 1; nt_px = nt_py = 0;
            nt_pos = 0; nt_pos_to = 0; nt_yaw = nt_pitch = 0; nt_face = 0;
            return 1;
        }
        return 0;
    }
    if (nt_closing) return 1;
    if (down & PAD_BUTTON_B) { nt_closing = 1; nt_zoom_to = 1; nt_face = 1; return 1; }
    if (nt_open < 0.9f) return 1;
    if (fire && (nav & (PAD_BUTTON_RIGHT | PAD_BUTTON_LEFT))) {   /* une feuille (la precedente finit de tourner d'abord) */
        int to = nt_pos_to + ((nav & PAD_BUTTON_RIGHT) ? 1 : -1);
        if (to >= 0 && to <= nt_leaves()) { nt_pos_to = to; nt_zoom_to = 1; }
    }
    if (down & PAD_BUTTON_A) nt_pos_to = nt_pos_to == 0 ? 1 : 0;   /* ouvrir / refermer */
    if (down & PAD_BUTTON_Y) { nt_face = 1; nt_zoom_to = 1; }     /* de face */
    return 1;                                            /* (le reste ne fait rien pendant la notice) */
}
static f32 nt_ease(f32 t) { if (t < 0) t = 0; if (t > 1) t = 1; return t * t * (3 - 2 * t); }

/* ---------- v302 : le livret en 3D ----------
   Unites : une page fait 2 de haut (y de -1 a 1) et 2 x r de large ; reliure en x = 0 ; on regarde vers +z (les pages
   lisibles regardent vers -z). Feuille i : recto page 2i+1, verso page 2i+2. Les feuilles tournees sont a gauche. */
typedef struct { f32 x, y, z; } nt3;
typedef struct { f32 x[4], y[4], u[4], v[4], z; GXTexObj *t; col_t c; u8 sh; } ntq;
#define NT_QMAX 160
static ntq nt_q[NT_QMAX];
static int nt_nq;
static f32 nt_cyw, nt_syw, nt_cpt, nt_spt, nt_ccx, nt_ccz, nt_sc, nt_ox, nt_oy;
static nt3 nt_rot(nt3 p) {                               /* (lacet autour de y, puis tangage autour de x) */
    f32 x = p.x * nt_cyw + p.z * nt_syw, z = -p.x * nt_syw + p.z * nt_cyw;
    nt3 r = { x, p.y * nt_cpt - z * nt_spt, p.y * nt_spt + z * nt_cpt };
    return r;
}
static int nt_qcmp(const void *a, const void *b) { f32 d = ((const ntq *)b)->z - ((const ntq *)a)->z; return d > 0 ? 1 : d < 0 ? -1 : 0; }
/* une face (4 coins, sa normale) : gardee seulement si elle est tournee vers nous */
static void nt_face4(const nt3 *p, nt3 n, GXTexObj *t, const f32 *u, const f32 *v, col_t c) {
    if (nt_nq >= NT_QMAX) return;
    nt3 rn = nt_rot(n);
    if (rn.z > -0.02f) return;
    ntq *q = &nt_q[nt_nq];
    f32 zs = 0;
    const f32 F = 900;
    for (int i = 0; i < 4; i++) {
        nt3 a = { p[i].x - nt_ccx, p[i].y, p[i].z - nt_ccz };
        nt3 r = nt_rot(a);
        f32 k = F / (F + r.z * nt_sc);
        q->x[i] = nt_ox + r.x * nt_sc * k; q->y[i] = nt_oy - r.y * nt_sc * k;
        q->u[i] = u[i]; q->v[i] = v[i]; zs += r.z;
    }
    f32 ar = 0;                                          /* (sens des coins a l'ecran : toujours le meme) */
    for (int i = 0; i < 4; i++) { int j = (i + 1) & 3; ar += q->x[i] * q->y[j] - q->x[j] * q->y[i]; }
    if (ar < 0) { f32 t2; t2 = q->x[1]; q->x[1] = q->x[3]; q->x[3] = t2; t2 = q->y[1]; q->y[1] = q->y[3]; q->y[3] = t2;
                  t2 = q->u[1]; q->u[1] = q->u[3]; q->u[3] = t2; t2 = q->v[1]; q->v[1] = q->v[3]; q->v[3] = t2; }
    q->z = zs / 4; q->t = t; q->c = c;
    f32 l = -rn.z; l = 0.55f + 0.45f * l;                /* (plus sombre de biais) */
    q->sh = (u8)(255 * l);
    nt_nq++;
}
/* une page (texture, sinon papier blanc et on la demande) */
static GXTexObj *nt_pg(int page) {
    int tw, th;
    if (page < 1 || page > nt_n) return NULL;
    GXTexObj *o = nt_tex(page, &tw, &th);
    if (!o && nt_nneed < 6) nt_need[nt_nneed++] = page;
    return o;
}
/* pages d'une feuille ; nombre impair de pages : la derniere est le dos de la notice (verso de la derniere feuille) */
static int nt_recto(int i) { return (nt_n & 1) && nt_n > 1 && i == nt_leaves() - 1 ? 0 : 2 * i + 1; }
static int nt_verso(int i) { return (nt_n & 1) && nt_n > 1 && i == nt_leaves() - 1 ? nt_n : 2 * i + 2; }
static void nt_rect(f32 x0, f32 x1, f32 z, int vers_nous, int page, int u_inv) {   /* page a plat, x0 < x1 */
    nt3 p[4] = { { x0, 1, z }, { x1, 1, z }, { x1, -1, z }, { x0, -1, z } };
    f32 u[4], v[4] = { 0, 0, 1, 1 };
    f32 a = u_inv ? 1 : 0, b = u_inv ? 0 : 1;
    u[0] = a; u[1] = b; u[2] = b; u[3] = a;
    nt3 n = { 0, 0, vers_nous ? -1.f : 1.f };
    nt_face4(p, n, nt_pg(page), u, v, RGBA(244, 240, 230, 255));
}
static void nt_tranche(f32 x0, f32 x1, f32 z0, f32 z1, int dehors) {   /* epaisseur d'une pile (bord libre et haut / bas) */
    col_t c = RGBA(222, 216, 204, 255);
    f32 u[4] = { 0, 0, 0, 0 }, v[4] = { 0, 0, 0, 0 };
    f32 xe = dehors > 0 ? x1 : x0;
    nt3 a[4] = { { xe, 1, z0 }, { xe, 1, z1 }, { xe, -1, z1 }, { xe, -1, z0 } }; nt3 na = { (f32)dehors, 0, 0 };
    nt_face4(a, na, NULL, u, v, c);
    nt3 b[4] = { { x0, 1, z0 }, { x1, 1, z0 }, { x1, 1, z1 }, { x0, 1, z1 } }; nt3 nb = { 0, 1, 0 };
    nt_face4(b, nb, NULL, u, v, c);
    nt3 d[4] = { { x0, -1, z0 }, { x1, -1, z0 }, { x1, -1, z1 }, { x0, -1, z1 } }; nt3 nd = { 0, -1, 0 };
    nt_face4(d, nd, NULL, u, v, c);
}
static void nt_book(f32 r) {
    int L = nt_leaves();
    f32 W = 2 * r, e = 0.06f / L; if (e > 0.006f) e = 0.006f;
    int fl = (int)nt_pos; f32 f = nt_pos - fl;
    if (fl >= L) { fl = L; f = 0; }
    int turning = f > 0.002f;
    int nl = fl, nr = L - fl - (turning ? 1 : 0);
    if (nr > 0) {                                        /* pile de droite : dessus = recto de la 1re feuille pas tournee */
        f32 zt = -e * nr;
        nt_rect(0, W, zt, 1, nt_recto(L - nr), 0);
        nt_rect(0, W, 0, 0, nt_verso(L - 1), 1);         /* (dessous : le dos de la notice) */
        nt_tranche(0, W, 0, zt, 1);
    }
    if (nl > 0) {                                        /* pile de gauche : dessus = verso de la derniere tournee */
        f32 zt = -e * nl;
        nt_rect(-W, 0, zt, 1, nt_verso(nl - 1), 0);
        nt_rect(-W, 0, 0, 0, 1, 1);                      /* (dessous : la couverture) */
        nt_tranche(-W, 0, 0, zt, -1);
    }
    if (turning) {                                       /* la feuille qui tourne, courbee (son bord libre suit en retard) */
        enum { S = 12 };
        int i = fl;
        f32 souple = (i == 0 || i == L - 1) ? 0.18f : 0.6f, pi = (f32)M_PI;
        f32 zb = -e * (nl > nr ? nl : nr) - 0.03f * sinf(pi * f);
        f32 px[S + 1], pz[S + 1], nx[S + 1], nz[S + 1], uu[S + 1];
        px[0] = 0; pz[0] = zb;
        for (int j = 0; j <= S; j++) {
            f32 u = (f32)j / S, a = pi * f - souple * sinf(pi * f) * powf(u, 1.6f);
            nx[j] = -sinf(a); nz[j] = -cosf(a); uu[j] = u;
            if (j < S) {
                f32 um = (j + 0.5f) / S, am = pi * f - souple * sinf(pi * f) * powf(um, 1.6f);
                px[j + 1] = px[j] + cosf(am) * W / S; pz[j + 1] = pz[j] - sinf(am) * W / S;
            }
        }
        GXTexObj *tf = nt_pg(nt_recto(i)), *tb = nt_pg(nt_verso(i));
        for (int j = 0; j < S; j++) {
            for (int cote = 0; cote < 2; cote++) {
                f32 sg = cote ? -1.f : 1.f, d = e * 0.5f * sg;
                nt3 p[4] = { { px[j] + nx[j] * d, 1, pz[j] + nz[j] * d }, { px[j + 1] + nx[j + 1] * d, 1, pz[j + 1] + nz[j + 1] * d },
                             { px[j + 1] + nx[j + 1] * d, -1, pz[j + 1] + nz[j + 1] * d }, { px[j] + nx[j] * d, -1, pz[j] + nz[j] * d } };
                f32 u0 = cote ? 1 - uu[j] : uu[j], u1 = cote ? 1 - uu[j + 1] : uu[j + 1];
                f32 u[4] = { u0, u1, u1, u0 }, v[4] = { 0, 0, 1, 1 };
                nt3 n = { (nx[j] + nx[j + 1]) * 0.5f * sg, 0, (nz[j] + nz[j + 1]) * 0.5f * sg };
                nt_face4(p, n, cote ? tb : tf, u, v, RGBA(244, 240, 230, 255));
            }
        }
    }
}
static void nt_draw_q(void) {
    qsort(nt_q, nt_nq, sizeof(ntq), nt_qcmp);          /* (de la plus loin a la plus proche) */
    for (int k = 0; k < nt_nq; k++) {
        ntq *q = &nt_q[k];
        if (q->t) {
            gx_set_image(q->t);
            GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
            for (int i = 0; i < 4; i++) { GX_Position2f32(UX(q->x[i]), q->y[i]); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(q->u[i], q->v[i]); }
            GX_End();
            u8 o = (u8)(255 - q->sh);
            if (o > 3) gx_poly4(q->x[0], q->y[0], q->x[1], q->y[1], q->x[2], q->y[2], q->x[3], q->y[3], RGBA(0, 0, 0, o), RGBA(0, 0, 0, o), RGBA(0, 0, 0, o), RGBA(0, 0, 0, o));
        } else {
            col_t c = q->c;
            u32 R = c.r * q->sh / 255, G = c.g * q->sh / 255, B = c.b * q->sh / 255;
            col_t d = RGBA(R, G, B, 255);
            gx_poly4(q->x[0], q->y[0], q->x[1], q->y[1], q->x[2], q->y[2], q->x[3], q->y[3], d, d, d, d);
        }
    }
}
/* la notice en grand ; rend 1 si elle est a l'ecran */
static int nt_draw(int k) {
    if (!nt_on) return 0;
    if (nt_pages(k) <= 0) { nt_on = 0; return 0; }
    f32 dt = ui_dt > 0.1f ? 0.1f : ui_dt;
    /* v300 : ouverture (la notice sort du couvercle) et fermeture (elle y retourne) */
    nt_open += (nt_closing ? -dt : dt) * 2.2f;
    if (nt_open > 1) nt_open = 1;
    if (nt_open <= 0 && nt_closing) { nt_on = 0; nt_closing = 0; nt_open = 0; return 0; }
    f32 e = nt_ease(nt_open);
    /* v302 : stick : tourner le livret (zoome : le deplacer) ; C : zoom ; feuilles qui tournent */
    int sy = PAD_SubStickY(0), mx = PAD_StickX(0), my = PAD_StickY(0);
    if (nt_open >= 1 && abs(sy) > 40) { nt_zoom_to += sy / 100.0f * 1.6f * dt; if (nt_zoom_to < 1) nt_zoom_to = 1; if (nt_zoom_to > 3) nt_zoom_to = 3; }
    if (nt_zoom_to > 1.03f) {
        if (abs(mx) > 20) nt_px -= mx / 100.0f * 420 * dt;
        if (abs(my) > 20) nt_py += my / 100.0f * 420 * dt;
    } else {
        nt_px *= expf(-dt * 8); nt_py *= expf(-dt * 8);
        if (nt_open >= 1 && !nt_closing) {
            if (abs(mx) > 20) { nt_yaw += mx / 100.0f * 160 * dt; nt_face = 0; }
            if (abs(my) > 20) { nt_pitch += my / 100.0f * 120 * dt; if (nt_pitch > 85) nt_pitch = 85; if (nt_pitch < -85) nt_pitch = -85; nt_face = 0; }
        }
    }
    if (nt_face) {                                       /* (revient doucement de face) */
        f32 c0 = 360 * roundf(nt_yaw / 360), g = 1 - expf(-dt * 7);
        nt_yaw += (c0 - nt_yaw) * g; nt_pitch -= nt_pitch * g;
        if (fabsf(c0 - nt_yaw) < 0.05f && fabsf(nt_pitch) < 0.05f) { nt_yaw = 0; nt_pitch = 0; nt_face = 0; }
    }
    nt_zoom += (nt_zoom_to - nt_zoom) * (1 - expf(-dt * 8));
    { f32 d = nt_pos_to - nt_pos; nt_pos += fabsf(d) < 0.003f ? d : d * (1 - expf(-dt * 5.5f)); }
    int L = nt_leaves();
    u8 fa = (u8)(250 * e);
    gx_poly4(0, 0, 640, 0, 640, 480, 0, 480, RGBA(8, 6, 16, fa), RGBA(8, 6, 16, fa), RGBA(4, 3, 10, fa), RGBA(4, 3, 10, fa));
    int cw = 0, chh = 0;
    GXTexObj *cv = nt_tex(1, &cw, &chh);
    f32 r = cv && chh ? (f32)cw / chh : 0.588f;
    /* centre du livret (ferme : la page seule ; ouvert : les deux) ; depuis le couvercle de la boite en s'ouvrant */
    f32 W = 2 * r;
    nt_ccx = nt_pos < 1 ? W / 2 * (1 - nt_pos) : nt_pos > L - 1 ? -W / 2 * (nt_pos - (L - 1)) : 0;
    if (L == 1) nt_ccx = W / 2 * (1 - nt_pos) - W / 2 * nt_pos;
    nt_ccz = -0.03f;
    f32 sc = 182 * nt_zoom, ox = 320 + nt_px, oy = 218 + nt_py;
    if (e < 1) { sc = 75 + (sc - 75) * e; ox = 92 + (ox - 92) * e; oy = 216 + (oy - 216) * e; }
    f32 yw = (nt_yaw + (1 - e) * 55) * (f32)M_PI / 180, pt = nt_pitch * (f32)M_PI / 180;
    nt_cyw = cosf(yw); nt_syw = sinf(yw); nt_cpt = cosf(pt); nt_spt = sinf(pt);
    nt_sc = sc; nt_ox = ox; nt_oy = oy;
    nt_nq = 0; nt_nneed = 0;
    nt_book(r);
    nt_draw_q();
    for (int i = 0; i < nt_nneed; i++) nt_want(nt_need[i]);   /* (les pages a l'ecran d'abord, une lecture a la fois) */
    if (!nt_nneed) {                                     /* (puis celles de la feuille suivante, d'avance) */
        int f2 = nt_pos_to < L ? nt_pos_to : L - 1;
        nt_want(nt_recto(f2)); nt_want(nt_verso(f2));
    }
    if (e > 0.6f) {
        char b[48];
        if (nt_pos_to == 0) snprintf(b, sizeof(b), "Couverture  (%d pages)", nt_n);
        else if (nt_pos_to >= L) snprintf(b, sizeof(b), "Dos  (%d pages)", nt_n);
        else if (nt_recto(nt_pos_to) > 0) snprintf(b, sizeof(b), "Pages %d-%d / %d", nt_verso(nt_pos_to - 1), nt_recto(nt_pos_to), nt_n);
        else snprintf(b, sizeof(b), "Page %d / %d", nt_verso(nt_pos_to - 1), nt_n);
        gx_rrect_fill(20, 438, 600, 30, 15, RGBA(22, 16, 44, 245), RGBA(14, 10, 30, 245));
        gx_rrect_stroke(20, 438, 600, 30, 15, 1.0f, RGBA(255, 255, 255, 40));
        f32 kx = 30;
        kx = gs_key(kx, 453, "\xE2\x86\x92", "Pages", RGBA(150, 200, 255, 255));
        kx = gs_key(kx, 453, "A", nt_pos_to ? "Fermer" : "Ouvrir", RGBA(120, 230, 150, 255));
        kx = gs_key(kx, 453, "S", nt_zoom_to > 1.03f ? "D\xC3\xA9placer" : "Tourner", RGBA(235, 235, 245, 255));
        kx = gs_key(kx, 453, "C", "Zoom", RGBA(255, 210, 90, 255));
        kx = gs_key(kx, 453, "Y", "De face", RGBA(200, 180, 255, 255));
        gs_key(kx, 453, "B", "Ranger", RGBA(240, 110, 110, 255));
        gx_rrect_fill(220, 14, 200, 26, 13, RGBA(22, 16, 44, 220), RGBA(14, 10, 30, 220));
        gx_text_c(F_SMALL, 320, 32, b, RGBA(255, 210, 120, 255));
    }
    return 1;
}

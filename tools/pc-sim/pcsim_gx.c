/* GC Station sur PC : GX (processeur graphique de la GameCube) en logiciel.
   Ce que GC Station utilise : projection orthographique, triangles / quads / bandes /
   eventails, couleur par sommet, une texture (RGB5A3, RGB565, I4, I8, IA4, IA8, RGBA8),
   un etage TEV (formule generale d + (1-c)a + cb), melange alpha, ciseaux.
   Pour aller vite, on ne dessine que les images a enregistrer (pcsim_capture) :
   l'image est alors ecrite en PNG a la copie vers l'ecran (GX_CopyDisp). */
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include "pcsim.h"

#define EW 640
#define EH 576
static float efb[EH][EW][4];                       /* couleur + alpha, 0..1 */
static GXColor clear_c = { 0, 0, 0, 255 };
static int efb_h = 480;
/* etat */
static float proj[4][4], posm[3][4];
static int proj_ortho = 1;
static float vp_x = 0, vp_y = 0, vp_w = 640, vp_h = 480;
static int sc_x0 = 0, sc_y0 = 0, sc_x1 = 640, sc_y1 = 480;
static int blend_type = GX_BM_BLEND, blend_src = GX_BL_SRCALPHA, blend_dst = GX_BL_INVSRCALPHA;
static int color_upd = 1, alpha_upd = 1, ntexgen = 0, ntev = 1;
typedef struct { void *img; int w, h, fmt, ws, wt, filt; } tex_t;
static tex_t texmap[8];
typedef struct { int map, cin[4], ain[4], cop, aop, cbias, abias, cscale, ascale, cclamp, aclamp; } tev_t;
static tev_t tev[16];
/* sommets */
typedef struct { float x, y, z, r, g, b, a, s, t; int has_c, has_t; } vtx_t;
static vtx_t vb[4096 + 8];
static int vn = 0, vprim = 0, vexp = 0;
static vtx_t cur;
static int vd_pos = 1, vd_clr = 0, vd_tex = 0;

static void ident34(float m[3][4]) { memset(m, 0, sizeof(float) * 12); m[0][0] = m[1][1] = m[2][2] = 1; }
GXFifoObj *GX_Init(void *base, u32 size) {
    (void)base; (void)size;
    static GXFifoObj f;
    ident34(posm); memset(proj, 0, sizeof(proj)); proj[0][0] = proj[1][1] = proj[2][2] = proj[3][3] = 1;
    for (int i = 0; i < 16; i++) { tev[i].map = 0xff; tev[i].cin[0] = tev[i].cin[1] = tev[i].cin[2] = GX_CC_ZERO; tev[i].cin[3] = GX_CC_RASC;
        tev[i].ain[0] = tev[i].ain[1] = tev[i].ain[2] = GX_CA_ZERO; tev[i].ain[3] = GX_CA_RASA; tev[i].cclamp = tev[i].aclamp = 1; }
    return &f;
}
void GX_SetCopyClear(GXColor c, u32 z) { (void)z; clear_c = c; }
void GX_SetViewport(f32 x, f32 y, f32 w, f32 h, f32 n, f32 f) { (void)n; (void)f; vp_x = x; vp_y = y; vp_w = w; vp_h = h; }
f32 GX_GetYScaleFactor(u16 efb_hh, u16 xfb) { return (f32)xfb / (f32)efb_hh; }
u32 GX_SetDispCopyYScale(f32 s) { return (u32)(efb_h * s); }
void GX_SetScissor(u32 x, u32 y, u32 w, u32 h) { sc_x0 = (int)x; sc_y0 = (int)y; sc_x1 = (int)(x + w); sc_y1 = (int)(y + h); }
void GX_SetDispCopySrc(u16 l, u16 t, u16 w, u16 h) { (void)l; (void)t; (void)w; efb_h = h > EH ? EH : h; }
void GX_SetDispCopyDst(u16 w, u16 h) { (void)w; (void)h; }
void GX_SetCopyFilter(u8 aa, u8 sp[12][2], u8 vf, u8 vfilter[7]) { (void)aa; (void)sp; (void)vf; (void)vfilter; }
void GX_SetFieldMode(u8 f, u8 h) { (void)f; (void)h; }
void GX_SetPixelFmt(u8 p, u8 z) { (void)p; (void)z; }
void GX_SetCullMode(u8 m) { (void)m; }
void GX_SetDispCopyGamma(u8 g) { (void)g; }
void GX_ClearVtxDesc(void) { vd_pos = vd_clr = vd_tex = 0; }
void GX_SetVtxDesc(u8 a, u8 t) { if (a == GX_VA_POS) vd_pos = t != GX_NONE; else if (a == GX_VA_CLR0) vd_clr = t != GX_NONE; else if (a == GX_VA_TEX0) vd_tex = t != GX_NONE; }
void GX_SetVtxAttrFmt(u8 f, u32 a, u32 c, u32 t, u32 fr) { (void)f; (void)a; (void)c; (void)t; (void)fr; }
void GX_SetNumTexGens(u32 n) { ntexgen = (int)n; }
void GX_SetTexCoordGen(u16 tc, u32 ty, u32 s, u32 m) { (void)tc; (void)ty; (void)s; (void)m; }
void GX_SetTevOrder(u8 st, u8 tc, u32 map, u8 c) { (void)tc; (void)c; if (st < 16) tev[st].map = (int)map; }
void GX_SetTevOp(u8 st, u8 mode) {
    if (st >= 16) return;
    tev_t *t = &tev[st];
    int c[4] = { GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC }, a[4] = { GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA };
    if (mode == GX_MODULATE) { c[0] = GX_CC_ZERO; c[1] = GX_CC_TEXC; c[2] = GX_CC_RASC; c[3] = GX_CC_ZERO; a[0] = GX_CA_ZERO; a[1] = GX_CA_TEXA; a[2] = GX_CA_RASA; a[3] = GX_CA_ZERO; }
    else if (mode == GX_REPLACE) { c[3] = GX_CC_TEXC; a[3] = GX_CA_TEXA; }
    else if (mode == GX_DECAL) { c[0] = GX_CC_RASC; c[1] = GX_CC_TEXC; c[2] = GX_CC_TEXA; c[3] = GX_CC_ZERO; }
    else if (mode == GX_BLEND) { c[0] = GX_CC_RASC; c[1] = GX_CC_ONE; c[2] = GX_CC_TEXC; c[3] = GX_CC_ZERO; a[0] = GX_CA_ZERO; a[1] = GX_CA_TEXA; a[2] = GX_CA_RASA; a[3] = GX_CA_ZERO; }
    memcpy(t->cin, c, sizeof(c)); memcpy(t->ain, a, sizeof(a));
    t->cop = t->aop = GX_TEV_ADD; t->cbias = t->abias = 0; t->cscale = t->ascale = 0; t->cclamp = t->aclamp = 1;
}
void GX_SetTevColorIn(u8 st, u8 a, u8 b, u8 c, u8 d) { if (st < 16) { tev[st].cin[0] = a; tev[st].cin[1] = b; tev[st].cin[2] = c; tev[st].cin[3] = d; } }
void GX_SetTevAlphaIn(u8 st, u8 a, u8 b, u8 c, u8 d) { if (st < 16) { tev[st].ain[0] = a; tev[st].ain[1] = b; tev[st].ain[2] = c; tev[st].ain[3] = d; } }
void GX_SetTevColorOp(u8 st, u8 op, u8 bias, u8 scale, u8 clamp, u8 reg) { (void)reg; if (st < 16) { tev[st].cop = op; tev[st].cbias = bias; tev[st].cscale = scale; tev[st].cclamp = clamp; } }
void GX_SetTevAlphaOp(u8 st, u8 op, u8 bias, u8 scale, u8 clamp, u8 reg) { (void)reg; if (st < 16) { tev[st].aop = op; tev[st].abias = bias; tev[st].ascale = scale; tev[st].aclamp = clamp; } }
void GX_SetNumChans(u8 n) { (void)n; }
void GX_SetChanCtrl(s32 ch, u8 en, u8 amb, u8 mat, u8 lit, u8 diff, u8 att) { (void)ch; (void)en; (void)amb; (void)mat; (void)lit; (void)diff; (void)att; }
void GX_SetNumTevStages(u8 n) { ntev = n ? n : 1; }
void GX_SetBlendMode(u8 type, u8 src, u8 dst, u8 op) { (void)op; blend_type = type; blend_src = src; blend_dst = dst; }
void GX_SetAlphaUpdate(u8 e) { alpha_upd = e; }
void GX_SetColorUpdate(u8 e) { color_upd = e; }
void GX_SetZMode(u8 e, u8 f, u8 u) { (void)e; (void)f; (void)u; }
void GX_LoadProjectionMtx(Mtx44 m, u8 type) { memcpy(proj, m, sizeof(proj)); proj_ortho = type == GX_ORTHOGRAPHIC; }
void GX_LoadPosMtxImm(Mtx m, u32 idx) { (void)idx; memcpy(posm, m, sizeof(posm)); }
void GX_SetCurrentMtx(u32 idx) { (void)idx; }
void GX_InvVtxCache(void) {}
void GX_InvalidateTexAll(void) {}
void GX_InitTexObj(GXTexObj *o, void *img, u16 w, u16 h, u8 fmt, u8 ws, u8 wt, u8 mip) {
    (void)mip;
    tex_t *t = (tex_t *)o;                                 /* (GXTexObj : 32 octets, assez pour tex_t) */
    _Static_assert(sizeof(tex_t) <= sizeof(GXTexObj), "GXTexObj trop petit");
    t->img = img; t->w = w; t->h = h; t->fmt = fmt; t->ws = ws; t->wt = wt; t->filt = GX_LINEAR;
}
void GX_InitTexObjFilterMode(GXTexObj *o, u8 minf, u8 magf) { (void)minf; ((tex_t *)o)->filt = magf; }
void GX_LoadTexObj(GXTexObj *o, u8 map) { if (map < 8) texmap[map] = *(tex_t *)o; }
static GXDrawDoneCallback dd_cb = NULL;
void GX_DrawDone(void) {}
void GX_SetDrawDone(void) { if (dd_cb) dd_cb(); }         /* (dessin logiciel : deja fini) */
GXDrawDoneCallback GX_SetDrawDoneCallback(GXDrawDoneCallback cb) { GXDrawDoneCallback o = dd_cb; dd_cb = cb; return o; }
void GX_GetGPStatus(u8 *ovh, u8 *undh, u8 *rdy, u8 *ci, u8 *brkpt) { if (ovh) *ovh = 0; if (undh) *undh = 1; if (rdy) *rdy = 1; if (ci) *ci = 1; if (brkpt) *brkpt = 0; }
void GX_Flush(void) {}

/* ---------- matrices (libogc : gu) ---------- */
void guMtxIdentity(Mtx m) { ident34(m); }
void guOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    memset(m, 0, sizeof(Mtx44));
    m[0][0] = 2.0f / (r - l); m[0][3] = -(r + l) / (r - l);
    m[1][1] = 2.0f / (t - b); m[1][3] = -(t + b) / (t - b);
    m[2][2] = -1.0f / (f - n); m[2][3] = -f / (f - n);
    m[3][3] = 1.0f;
}
void guMtxConcat(Mtx a, Mtx b, Mtx ab) {
    Mtx t;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (j == 3 ? a[i][3] : 0);
    memcpy(ab, t, sizeof(Mtx));
}
void guMtxTrans(Mtx m, f32 x, f32 y, f32 z) { ident34(m); m[0][3] = x; m[1][3] = y; m[2][3] = z; }
void guMtxScale(Mtx m, f32 x, f32 y, f32 z) { ident34(m); m[0][0] = x; m[1][1] = y; m[2][2] = z; }

/* ---------- textures (formats en tuiles de la GameCube) ---------- */
static inline float c5(u32 v) { return (float)((v << 3) | (v >> 2)) / 255.0f; }
static inline float c6(u32 v) { return (float)((v << 2) | (v >> 4)) / 255.0f; }
static inline float c4(u32 v) { return (float)(v * 17) / 255.0f; }
static inline float c3(u32 v) { return (float)((v << 5) | (v << 2) | (v >> 1)) / 255.0f; }
static void texel(const tex_t *t, int x, int y, float out[4]) {
    const u8 *d = (const u8 *)t->img;
    if (!d) { out[0] = out[1] = out[2] = out[3] = 1; return; }
    int w = t->w;
    switch (t->fmt) {
    case GX_TF_RGB5A3: case GX_TF_RGB565: case GX_TF_IA8: {   /* tuiles 4x4, 2 octets */
        int tw = (w + 3) / 4, ti = (y / 4) * tw + x / 4, o = ti * 32 + ((y & 3) * 4 + (x & 3)) * 2;
        u32 v = *(const u16 *)(d + o);                     /* (ecrit par le programme : ordre du processeur) */
        if (t->fmt == GX_TF_RGB565) { out[0] = c5(v >> 11); out[1] = c6((v >> 5) & 63); out[2] = c5(v & 31); out[3] = 1; }
        else if (t->fmt == GX_TF_IA8) { out[0] = out[1] = out[2] = (v & 0xFF) / 255.0f; out[3] = (v >> 8) / 255.0f; }
        else if (v & 0x8000) { out[0] = c5((v >> 10) & 31); out[1] = c5((v >> 5) & 31); out[2] = c5(v & 31); out[3] = 1; }
        else { out[3] = c3((v >> 12) & 7); out[0] = c4((v >> 8) & 15); out[1] = c4((v >> 4) & 15); out[2] = c4(v & 15); }
        return; }
    case GX_TF_I4: {                                       /* tuiles 8x8, 4 bits */
        int tw = (w + 7) / 8, ti = (y / 8) * tw + x / 8, p = (y & 7) * 8 + (x & 7);
        u32 b = d[(ti * 32 + p / 2) ^ 3]; u32 v = p & 1 ? b & 15 : b >> 4;   /* (police : tableau de u32, ordre du PC) */
        out[0] = out[1] = out[2] = out[3] = c4(v); return; }
    case GX_TF_I8: case GX_TF_IA4: {                       /* tuiles 8x4, 1 octet */
        int tw = (w + 7) / 8, ti = (y / 4) * tw + x / 8, o = ti * 32 + (y & 3) * 8 + (x & 7);
        u32 v = d[o];
        if (t->fmt == GX_TF_I8) { out[0] = out[1] = out[2] = out[3] = v / 255.0f; }
        else { out[0] = out[1] = out[2] = c4(v & 15); out[3] = c4(v >> 4); }
        return; }
    case GX_TF_RGBA8: {                                    /* tuiles 4x4 : 32 octets AR puis 32 octets GB */
        int tw = (w + 3) / 4, ti = (y / 4) * tw + x / 4, o = ti * 64 + ((y & 3) * 4 + (x & 3)) * 2;
        out[3] = d[o] / 255.0f; out[0] = d[o + 1] / 255.0f; out[1] = d[o + 32] / 255.0f; out[2] = d[o + 33] / 255.0f;
        return; }
    }
    out[0] = 1; out[1] = 0; out[2] = 1; out[3] = 1;        /* (format pas gere : magenta) */
}
static int wrapc(int v, int n, int mode) {
    if (mode == GX_REPEAT) { v %= n; return v < 0 ? v + n : v; }
    if (mode == GX_MIRROR) { int p = n * 2; v %= p; if (v < 0) v += p; return v < n ? v : p - 1 - v; }
    return v < 0 ? 0 : v >= n ? n - 1 : v;
}
static void sample(const tex_t *t, float s, float tt, float out[4]) {
    if (!t->img || t->w <= 0 || t->h <= 0) { out[0] = out[1] = out[2] = out[3] = 1; return; }
    float u = s * t->w - 0.5f, v = tt * t->h - 0.5f;
    if (t->filt == GX_NEAR) { texel(t, wrapc((int)floorf(u + 0.5f), t->w, t->ws), wrapc((int)floorf(v + 0.5f), t->h, t->wt), out); return; }
    int x0 = (int)floorf(u), y0 = (int)floorf(v);
    float fx = u - x0, fy = v - y0, a[4], b[4], c[4], d[4];
    texel(t, wrapc(x0, t->w, t->ws), wrapc(y0, t->h, t->wt), a);
    texel(t, wrapc(x0 + 1, t->w, t->ws), wrapc(y0, t->h, t->wt), b);
    texel(t, wrapc(x0, t->w, t->ws), wrapc(y0 + 1, t->h, t->wt), c);
    texel(t, wrapc(x0 + 1, t->w, t->ws), wrapc(y0 + 1, t->h, t->wt), d);
    for (int i = 0; i < 4; i++) out[i] = (a[i] * (1 - fx) + b[i] * fx) * (1 - fy) + (c[i] * (1 - fx) + d[i] * fx) * fy;
}

/* ---------- TEV ---------- */
static float cin_v(int sel, int ch, const float prev[4], const float tex[4], const float ras[4]) {
    switch (sel) {
    case GX_CC_CPREV: return prev[ch]; case GX_CC_APREV: return prev[3];
    case GX_CC_TEXC: return tex[ch]; case GX_CC_TEXA: return tex[3];
    case GX_CC_RASC: return ras[ch]; case GX_CC_RASA: return ras[3];
    case GX_CC_ONE: return 1; case GX_CC_HALF: return 0.5f;
    default: return 0;
    }
}
static float ain_v(int sel, const float prev[4], const float tex[4], const float ras[4]) {
    switch (sel) { case GX_CA_APREV: return prev[3]; case GX_CA_TEXA: return tex[3]; case GX_CA_RASA: return ras[3]; default: return 0; }
}
static float tev_op(float a, float b, float c, float d, int op, int bias, int scale, int clamp) {
    float v = (op == GX_TEV_SUB ? d - (a * (1 - c) + b * c) : d + (a * (1 - c) + b * c));
    if (bias == GX_TB_ADDHALF) v += 0.5f; else if (bias == GX_TB_SUBHALF) v -= 0.5f;
    if (scale == GX_CS_SCALE_2) v *= 2; else if (scale == GX_CS_SCALE_4) v *= 4; else if (scale == GX_CS_DIVIDE_2) v *= 0.5f;
    if (clamp) v = v < 0 ? 0 : v > 1 ? 1 : v;
    return v;
}
static void shade(float s, float t, const float ras[4], float out[4]) {
    float prev[4] = { 0, 0, 0, 0 }, tx[4] = { 1, 1, 1, 1 };
    for (int st = 0; st < ntev; st++) {
        const tev_t *e = &tev[st];
        if (ntexgen > 0 && e->map != 0xff && e->map < 8) sample(&texmap[e->map], s, t, tx); else { tx[0] = tx[1] = tx[2] = tx[3] = 1; }
        float n[4];
        for (int ch = 0; ch < 3; ch++)
            n[ch] = tev_op(cin_v(e->cin[0], ch, prev, tx, ras), cin_v(e->cin[1], ch, prev, tx, ras), cin_v(e->cin[2], ch, prev, tx, ras), cin_v(e->cin[3], ch, prev, tx, ras), e->cop, e->cbias, e->cscale, e->cclamp);
        n[3] = tev_op(ain_v(e->ain[0], prev, tx, ras), ain_v(e->ain[1], prev, tx, ras), ain_v(e->ain[2], prev, tx, ras), ain_v(e->ain[3], prev, tx, ras), e->aop, e->abias, e->ascale, e->aclamp);
        memcpy(prev, n, sizeof(n));
    }
    memcpy(out, prev, sizeof(prev));
}
static float bfac(int f, const float s[4], const float d[4], int ch) {
    switch (f) {
    case GX_BL_ZERO: return 0; case GX_BL_ONE: return 1;
    case GX_BL_SRCCLR: return ch < 3 ? d[ch] : d[3]; case GX_BL_INVSRCCLR: return 1 - (ch < 3 ? d[ch] : d[3]);   /* (cote source : couleur de destination, comme GX) */
    case GX_BL_SRCALPHA: return s[3]; case GX_BL_INVSRCALPHA: return 1 - s[3];
    case GX_BL_DSTALPHA: return d[3]; case GX_BL_INVDSTALPHA: return 1 - d[3];
    }
    return 0;
}
static void plot(int x, int y, const float c[4]) {
    float *d = efb[y][x];
    float o[4];
    if (blend_type == GX_BM_BLEND) for (int i = 0; i < 4; i++) o[i] = c[i] * bfac(blend_src, c, d, i) + d[i] * bfac(blend_dst, c, d, i);
    else if (blend_type == GX_BM_SUBTRACT) for (int i = 0; i < 4; i++) o[i] = d[i] - c[i];
    else memcpy(o, c, sizeof(o));
    for (int i = 0; i < 4; i++) o[i] = o[i] < 0 ? 0 : o[i] > 1 ? 1 : o[i];
    if (color_upd) { d[0] = o[0]; d[1] = o[1]; d[2] = o[2]; }
    if (alpha_upd) d[3] = o[3];
}

/* ---------- triangles ---------- */
typedef struct { float x, y, r, g, b, a, s, t; } sv_t;
static void xform(const vtx_t *v, sv_t *o) {
    float p[3];
    for (int i = 0; i < 3; i++) p[i] = posm[i][0] * v->x + posm[i][1] * v->y + posm[i][2] * v->z + posm[i][3];
    float cx = proj[0][0] * p[0] + proj[0][1] * p[1] + proj[0][2] * p[2] + proj[0][3];
    float cy = proj[1][0] * p[0] + proj[1][1] * p[1] + proj[1][2] * p[2] + proj[1][3];
    float w = proj_ortho ? 1.0f : -p[2];
    if (w == 0) w = 1;
    cx /= w; cy /= w;
    o->x = vp_x + (cx + 1) * vp_w * 0.5f;
    o->y = vp_y + (1 - cy) * vp_h * 0.5f;
    o->r = v->r; o->g = v->g; o->b = v->b; o->a = v->a; o->s = v->s; o->t = v->t;
}
static void tri(const vtx_t *a, const vtx_t *b, const vtx_t *c) {
    sv_t p[3]; xform(a, &p[0]); xform(b, &p[1]); xform(c, &p[2]);
    float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    if (fabsf(area) < 1e-6f) return;
    int x0 = (int)floorf(fminf(p[0].x, fminf(p[1].x, p[2].x))), x1 = (int)ceilf(fmaxf(p[0].x, fmaxf(p[1].x, p[2].x)));
    int y0 = (int)floorf(fminf(p[0].y, fminf(p[1].y, p[2].y))), y1 = (int)ceilf(fmaxf(p[0].y, fmaxf(p[1].y, p[2].y)));
    int cx0 = sc_x0 < 0 ? 0 : sc_x0, cy0 = sc_y0 < 0 ? 0 : sc_y0, cx1 = sc_x1 > EW ? EW : sc_x1, cy1 = sc_y1 > efb_h ? efb_h : sc_y1;
    if (x0 < cx0) x0 = cx0; if (y0 < cy0) y0 = cy0; if (x1 > cx1) x1 = cx1; if (y1 > cy1) y1 = cy1;
    float inv = 1.0f / area;
    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f;
            float w0 = ((p[1].x - px) * (p[2].y - py) - (p[2].x - px) * (p[1].y - py)) * inv;
            float w1 = ((p[2].x - px) * (p[0].y - py) - (p[0].x - px) * (p[2].y - py)) * inv;
            float w2 = 1 - w0 - w1;
            if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
            float ras[4] = { w0 * p[0].r + w1 * p[1].r + w2 * p[2].r, w0 * p[0].g + w1 * p[1].g + w2 * p[2].g,
                             w0 * p[0].b + w1 * p[1].b + w2 * p[2].b, w0 * p[0].a + w1 * p[1].a + w2 * p[2].a };
            float s = w0 * p[0].s + w1 * p[1].s + w2 * p[2].s, t = w0 * p[0].t + w1 * p[1].t + w2 * p[2].t, c[4];
            shade(s, t, ras, c);
            plot(x, y, c);
        }
    }
}
void GX_Begin(u8 prim, u8 fmt, u16 n) { (void)fmt; vprim = prim; vexp = n; vn = 0; memset(&cur, 0, sizeof(cur)); cur.r = cur.g = cur.b = cur.a = 1; }
static void vtx_done(void) { if (vn < (int)(sizeof(vb) / sizeof(vb[0]))) vb[vn++] = cur; }
/* un sommet est complet quand son dernier attribut est envoye (ordre GX : position, couleur, texture) */
void pcsim_gx_pos(f32 x, f32 y, f32 z) { cur.x = x; cur.y = y; cur.z = z; if (!vd_clr && !vd_tex) vtx_done(); }
void pcsim_gx_clr(u8 r, u8 g, u8 b, u8 a) { cur.r = r / 255.0f; cur.g = g / 255.0f; cur.b = b / 255.0f; cur.a = a / 255.0f; if (!vd_tex) vtx_done(); }
void pcsim_gx_tex(f32 s, f32 t) { cur.s = s; cur.t = t; vtx_done(); }
/* charge du processeur graphique (toutes les images, meme non dessinees) */
static double gp_tri = 0, gp_pix = 0, gp_tex_pix = 0;
static double gp_tri_max = 0, gp_pix_max = 0, gp_tri_sum = 0, gp_pix_sum = 0, gp_tex_sum = 0; static unsigned long long gp_frames = 0;
static void gp_count(const vtx_t *a, const vtx_t *b, const vtx_t *c) {
    sv_t p[3]; xform(a, &p[0]); xform(b, &p[1]); xform(c, &p[2]);
    double ar = fabs((p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y)) * 0.5;
    if (ar > 640.0 * 480.0) ar = 640.0 * 480.0;
    gp_tri++; gp_pix += ar;
    if (ntexgen > 0) gp_tex_pix += ar;
}
void pcsim_gp_report(void) {
    if (!gp_frames) return;
    printf("[pcsim] processeur graphique : %.0f triangles et %.0f pixels par image en moyenne (%.1f ecrans ; %.0f%% avec texture), au plus %.0f triangles et %.0f pixels\n",
           gp_tri_sum / gp_frames, gp_pix_sum / gp_frames, gp_pix_sum / gp_frames / (640.0 * 480.0), gp_pix_sum ? 100.0 * gp_tex_sum / gp_pix_sum : 0, gp_tri_max, gp_pix_max);
}
static unsigned long long gx_now(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return (unsigned long long)t.tv_sec * 1000000000ull + t.tv_nsec; }
void GX_End(void) {
    switch (vprim) {                                           /* (compte toujours : charge du GPU) */
    case GX_QUADS: for (int i = 0; i + 3 < vn; i += 4) { gp_count(&vb[i], &vb[i + 1], &vb[i + 2]); gp_count(&vb[i], &vb[i + 2], &vb[i + 3]); } break;
    case GX_TRIANGLES: for (int i = 0; i + 2 < vn; i += 3) gp_count(&vb[i], &vb[i + 1], &vb[i + 2]); break;
    case GX_TRIANGLESTRIP: for (int i = 0; i + 2 < vn; i++) gp_count(&vb[i], &vb[i + 1], &vb[i + 2]); break;
    case GX_TRIANGLEFAN: for (int i = 1; i + 1 < vn; i++) gp_count(&vb[0], &vb[i], &vb[i + 1]); break;
    }
    if (!pcsim_capture) { vn = 0; return; }
    unsigned long long t0 = gx_now();
    switch (vprim) {
    case GX_QUADS: for (int i = 0; i + 3 < vn; i += 4) { tri(&vb[i], &vb[i + 1], &vb[i + 2]); tri(&vb[i], &vb[i + 2], &vb[i + 3]); } break;
    case GX_TRIANGLES: for (int i = 0; i + 2 < vn; i += 3) tri(&vb[i], &vb[i + 1], &vb[i + 2]); break;
    case GX_TRIANGLESTRIP: for (int i = 0; i + 2 < vn; i++) tri(&vb[i], &vb[i + 1], &vb[i + 2]); break;
    case GX_TRIANGLEFAN: for (int i = 1; i + 1 < vn; i++) tri(&vb[0], &vb[i], &vb[i + 1]); break;
    }
    vn = 0;
    pcsim_gx_ns += gx_now() - t0;                              /* (travail du processeur graphique : pas compte au Gekko) */
}

/* ---------- PNG (sans compression : blocs "stockes" de zlib) ---------- */
static u32 crc_t[256];
static u32 crc32b(u32 c, const u8 *p, size_t n) {
    if (!crc_t[1]) for (u32 i = 0; i < 256; i++) { u32 v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_t[i] = v; }
    c = ~c; while (n--) c = crc_t[(c ^ *p++) & 0xFF] ^ (c >> 8); return ~c;
}
static void be32w(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *ty, const u8 *d, u32 n) {
    u8 h[8]; be32w(h, n); memcpy(h + 4, ty, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    u32 c = crc32b(0, (const u8 *)ty, 4); c = crc32b(c, d, n); u8 t[4]; be32w(t, c); fwrite(t, 1, 4, f);
}
static int png_write(const char *path, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    u8 ih[13]; be32w(ih, w); be32w(ih + 4, h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13);
    size_t row = (size_t)w * 3 + 1, raw = row * h;
    u8 *r = malloc(raw);
    for (int y = 0; y < h; y++) {
        r[y * row] = 0;
        for (int x = 0; x < w; x++) for (int c = 0; c < 3; c++) r[y * row + 1 + x * 3 + c] = (u8)(efb[y][x][c] * 255.0f + 0.5f);
    }
    size_t nb = (raw + 65534) / 65535, zn = 2 + raw + nb * 5 + 4;
    u8 *z = malloc(zn), *o = z;
    *o++ = 0x78; *o++ = 0x01;
    u32 a = 1, b = 0;
    for (size_t i = 0; i < raw; i++) { a = (a + r[i]) % 65521; b = (b + a) % 65521; }
    for (size_t i = 0; i < raw; i += 65535) {
        u32 n = (u32)(raw - i < 65535 ? raw - i : 65535);
        *o++ = i + n >= raw; *o++ = n & 0xFF; *o++ = n >> 8; *o++ = ~n & 0xFF; *o++ = (~n >> 8) & 0xFF;
        memcpy(o, r + i, n); o += n;
    }
    be32w(o, b << 16 | a); o += 4;
    chunk(f, "IDAT", z, (u32)(o - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f); free(r); free(z);
    return 0;
}

/* ---------- copie vers l'ecran : fin de l'image ---------- */
static void efb_clear(void) {
    float c[4] = { clear_c.r / 255.0f, clear_c.g / 255.0f, clear_c.b / 255.0f, clear_c.a / 255.0f };
    for (int y = 0; y < EH; y++) for (int x = 0; x < EW; x++) memcpy(efb[y][x], c, sizeof(c));
}
static int efb_inited = 0;
/* fluidite : intervalle entre deux images (temps reel) */
static unsigned long long fr_last = 0, fr_n = 0, fr_slow = 0, fr_vslow = 0, fr_worst = 0, fr_t0 = 0;
static unsigned long long mono_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long long)t.tv_sec * 1000000ull + t.tv_nsec / 1000; }
void pcsim_fps_report(void) {
    if (fr_n < 2) return;
    double sec = (fr_last - fr_t0) / 1e6;
    printf("[pcsim] fluidite : %llu images en %.1f s (%.1f images/s), %llu images ratees (> 20 ms), %llu tres lentes (> 34 ms), pire %.0f ms\n",
           fr_n, sec, (fr_n - 1) / sec, fr_slow, fr_vslow, fr_worst / 1000.0);
}
void GX_CopyDisp(void *dst, u8 clear) {
    (void)dst;
    {   /* (les images dessinees pour une capture ne comptent pas : le dessin logiciel est lent, pas GC Station) */
        static int was_cap = 0;
        unsigned long long n = mono_us();
        if (fr_last && !was_cap && !pcsim_capture) {
            unsigned long long d = n - fr_last;
            if (d > 20000) fr_slow++;
            if (d > 34000) { fr_vslow++; printf("[pcsim] image lente : %.0f ms a %.1f s (%s)\n", d / 1000.0, (n - fr_t0) / 1e6, pcsim_script_where()); }
            if (d > fr_worst) fr_worst = d;
        }
        if (!fr_last) fr_t0 = n;
        fr_last = n; fr_n++; was_cap = pcsim_capture != 0;
        if (gp_tri) { gp_frames++; gp_tri_sum += gp_tri; gp_pix_sum += gp_pix; gp_tex_sum += gp_tex_pix;
            if (gp_tri > gp_tri_max) gp_tri_max = gp_tri; if (gp_pix > gp_pix_max) gp_pix_max = gp_pix; }
        gp_tri = gp_pix = gp_tex_pix = 0;
    }
    unsigned long long t0 = gx_now();
    if (!efb_inited) { efb_clear(); efb_inited = 1; }
    if (pcsim_capture == 1) pcsim_capture = 2;              /* (1 : demande faite pendant cette image : on dessine la suivante en entier) */
    else if (pcsim_capture == 2) {
        const char *od = getenv("PCSIM_OUT"); if (!od) od = "shots";
        mkdir(od, 0777);
        char p[512]; snprintf(p, sizeof(p), "%s/%s.png", od, pcsim_capture_name);
        if (png_write(p, 640, efb_h) == 0) printf("[pcsim] capture : %s\n", p);
        pcsim_capture = 0;
    }
    if (clear && pcsim_capture) efb_clear();
    pcsim_gx_ns += gx_now() - t0;
}

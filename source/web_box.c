// GC Station v201 : moteur de page web, 3e partie : la mise en page en boites (inclus par web_layout.c).
// Arbre HTML + styles -> boites (blocs, lignes de texte, tableaux, flex, grille, flottants, inline-block),
// puis positions -> ce que l'affichage connait deja : wpage_t (mots, fonds / bordures, liens, champs, images).
// Marges qui se fondent (comme les navigateurs), largeurs auto / fixes / %, min / max, centrage (margin: auto),
// tableaux en largeur automatique (colonnes selon leur contenu), cases sur plusieurs colonnes / lignes,
// flottants a gauche / a droite (le texte les contourne), puces et numeros de liste, ::before / ::after.

enum { WB_BLOCK = 1, WB_ANON, WB_TABLE, WB_ROWGRP, WB_ROW, WB_CELL, WB_CAPTION, WB_FLEX, WB_GRID, WB_IMG, WB_FIELD, WB_SPACE };
typedef struct {
    s32 parent, first, last, next;
    s32 node, cs;                               /* noeud HTML ; style (wbs) */
    s32 i0, i1;                                 /* WB_ANON : mots et elements en ligne (liste wii) */
    u8 kind, atom, list, flt;                   /* atom : dans une ligne (inline-block, image...) */
    u8 absb;                                    /* position: absolute (avec une image) : par-dessus le haut de son parent */
    s16 colspan, rowspan;
    s16 link, field, img, ts;                   /* image / champ : lien, champ, image, style du texte */
    f32 minw, maxw;                             /* largeurs intrinseques (< 0 : pas encore calculees) */
    f32 aw, ah;                                 /* image, champ : taille */
    s32 marker;                                 /* puce / numero (texte dans wd.txt, -1 : aucun) */
} wbx_t;

enum { II_TEXT = 1, II_OPEN, II_CLOSE, II_ATOM, II_BR, II_WBR };
typedef struct { s32 next; s32 a, n; s16 ts; u8 type; u8 pad; f32 sp; } wii_t;   /* a/n : texte (wd.txt) ou boite (a) ; sp : espace (OPEN/CLOSE) */

typedef struct {
    u32 col, bg;
    s16 link;
    u8 font, fx, ws, valign, transform, pad;
    f32 lh;                                     /* hauteur de ligne (px, 0 : normale) */
} wts_t;
enum { FX_ITAL = 4, FX_STRIKE = 8 };

#define WB_MAX_BOXES  40000
#define WB_MAX_II     200000
#define WB_MAX_TS     4000

static wbx_t *wbx; static int nwbx, cwbx;
static wcs_t *wbs; static int nwbs, cwbs;
static wii_t *wii; static int nwii, cwii;
static wts_t *wtss; static int nwts, cwts;
static wpage_t *wbp;                            /* page en cours de mise en page */
static int wb_fail;

static void wb_free(void) {
    free(wbx); free(wbs); free(wii); free(wtss); free(wels);
    wbx = NULL; wbs = NULL; wii = NULL; wtss = NULL; wels = NULL;
    nwbx = cwbx = nwbs = cwbs = nwii = cwii = nwts = cwts = 0;
}

/* police de l'atlas pour une taille / un gras (en attendant plus de polices) */
static int wb_font(f32 fs, int bold, int mono, int *fx) {
    if (mono) { if (bold) *fx |= FX_BOLD; return fs <= 11.5f ? F_SMALL : F_MONO; }
    if (fs >= 20.5f && (bold || fs >= 23)) return F_LARGE;   /* (titres : la grande police est grasse) */
    if (bold) *fx |= FX_BOLD;
    return fs <= 13.5f ? F_SMALL : fs <= 16.6f ? F_MED : F_W18;
}
static int wb_ts_from(const wcs_t *c, int link) {
    wts_t t;
    memset(&t, 0, sizeof(t));
    int fx = 0;
    t.font = (u8)wb_font(c->fsize, c->weight >= 600, c->mono, &fx);
    if (c->deco == 1) fx |= FX_ULINE;
    if (c->deco == 2) fx |= FX_STRIKE;
    if (c->italic) fx |= FX_ITAL;
    t.fx = (u8)fx;
    t.col = c->color;
    t.bg = 0;
    t.link = (s16)link;
    t.ws = c->ws;
    t.valign = c->valign;
    t.transform = c->transform;
    t.lh = c->lh > 0 ? c->lh : c->lh < 0 ? -c->lh * c->fsize : 0;
    if (nwts > 0) {                             /* (le meme que l'un des derniers : on le reprend) */
        for (int i = nwts - 1; i >= 0 && i >= nwts - 8; i--) if (!memcmp(&wtss[i], &t, sizeof(t))) return i;
    }
    if (!grow((void **)&wtss, &cwts, nwts + 1, sizeof(wts_t), WB_MAX_TS)) return nwts > 0 ? nwts - 1 : 0;
    wtss[nwts] = t;
    return nwts++;
}

static int wb_new(int kind, int node, int parent) {
    if (!grow((void **)&wbx, &cwbx, nwbx + 1, sizeof(wbx_t), WB_MAX_BOXES)) { wb_fail = 1; return -1; }
    wbx_t *b = &wbx[nwbx];
    memset(b, 0, sizeof(*b));
    b->parent = parent; b->first = b->last = b->next = -1;
    b->node = node; b->cs = -1; b->kind = (u8)kind;
    b->i0 = b->i1 = -1; b->colspan = b->rowspan = 1;
    b->link = b->field = b->img = -1; b->ts = 0;
    b->minw = b->maxw = -1; b->marker = -1;
    if (parent >= 0) {
        wbx_t *p = &wbx[parent];
        if (p->last >= 0) wbx[p->last].next = nwbx; else p->first = nwbx;
        p->last = nwbx;
    }
    return nwbx++;
}
static int wb_style(const wcs_t *c) {
    if (!grow((void **)&wbs, &cwbs, nwbs + 1, sizeof(wcs_t), WB_MAX_BOXES)) { wb_fail = 1; return 0; }
    wbs[nwbs] = *c;
    return nwbs++;
}
static int wb_ii(int box, int type, s32 a, s32 n, int ts, f32 sp) {
    if (box < 0) return -1;
    if (!grow((void **)&wii, &cwii, nwii + 1, sizeof(wii_t), WB_MAX_II)) { wb_fail = 1; return -1; }
    wii_t *x = &wii[nwii];
    x->next = -1; x->a = a; x->n = n; x->ts = (s16)ts; x->type = (u8)type; x->sp = sp;
    wbx_t *b = &wbx[box];
    if (b->i1 >= 0) wii[b->i1].next = nwii; else b->i0 = nwii;
    b->i1 = nwii;
    return nwii++;
}

/* ---------- construction des boites ---------- */
typedef struct {
    int cont;                                   /* bloc qui recoit les enfants */
    int anon;                                   /* boite de lignes en cours (-1 : aucune) */
    int ts;                                     /* style du texte en cours */
    int link;                                   /* lien en cours */
    int form;                                   /* formulaire en cours */
} wbctx_t;

static int wb_anon(wbctx_t *x) {
    if (x->anon < 0) {
        x->anon = wb_new(WB_ANON, -1, x->cont);
        if (x->anon >= 0) wbx[x->anon].cs = wbx[x->cont].cs;
    }
    return x->anon;
}
static void wb_text_node(wbctx_t *x, int k) {
    const wnode_t *n = &wd.n[k];
    if (!n->tn) return;
    const wts_t *t = &wtss[x->ts];
    if (t->ws == WSP_NORMAL || t->ws == WSP_NOWRAP) {     /* que des espaces : seulement s'il y a deja une ligne */
        int all = 1;
        for (u32 i = 0; i < n->tn && all; i++) { char c = wd.txt[n->t0 + i]; all = c == ' ' || c == '\n' || c == '\t' || c == '\f'; }
        if (all && x->anon < 0) return;
    }
    wb_ii(wb_anon(x), II_TEXT, (s32)n->t0, (s32)n->tn, x->ts, 0);
}
/* texte ajoute par le moteur (puces, ::before) : dans wd.txt */
static void wb_text_str(wbctx_t *x, const char *s, int ts) {
    int n = (int)strlen(s);
    if (!n) return;
    int o = wd_text_add(s, n);
    if (o < 0) return;
    wd.tlen++;                                  /* (garde le 0 final) */
    wb_ii(wb_anon(x), II_TEXT, o, n, ts, 0);
}

static char ws_pe_content[256];
/* contenu de ::before / ::after (chaines, attr()) -> ws_pe_content */
__attribute__((noinline)) static int wb_pe_content(int k, int pe) {
    ws_pe_content[0] = 0;
    int got = 0;
    for (int m = 0; m < nwm; m++) {
        const wrule_t *r = &ws.r[wm[m].rule];
        for (int i = 0; i < r->dn; i++) {
            const wdecl_t *d = &ws.d[r->d0 + i];
            if (d->prop != WP_CONTENT) continue;
            static char vb[512];
            int vn;
            const char *v = ws_decl_val(d, &vn, vb, sizeof(vb));
            if (!v) continue;
            char out[256]; int o = 0;
            int j = 0;
            while (j < vn && o < 240) {
                if (v[j] == '"' || v[j] == '\'') {
                    char q = v[j++];
                    while (j < vn && v[j] != q && o < 240) {
                        unsigned cp = (u8)v[j];
                        if (v[j] == '\\' && j + 1 < vn) {
                            j++;
                            unsigned h = 0; int hn = 0;
                            while (j < vn && hn < 6 && css_hexv(v[j]) >= 0) { h = h * 16 + css_hexv(v[j]); j++; hn++; }
                            if (hn) { if (j < vn && v[j] == ' ') j++; cp = h; }
                            else { cp = (u8)v[j++]; }
                        } else if (cp >= 0x80) {          /* UTF-8 */
                            int pos = j;
                            cp = read_cp((const unsigned char *)v, vn, &pos, 0);
                            j = pos;
                        } else j++;
                        if (cp >= 0xE000 && cp <= 0xF8FF) continue;   /* (icones d'une police speciale) */
                        if (cp == '\n' || cp == 0xA) { out[o++] = ' '; continue; }
                        if (cp < 0x80) out[o++] = (char)cp;
                        else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
                        else if (cp < 0x10000) { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
                    }
                    j++;
                } else if (vn - j > 5 && !strncasecmp(v + j, "attr(", 5)) {
                    int e = j + 5; while (e < vn && v[e] != ')') e++;
                    char an[40]; int al = e - j - 5; if (al > 39) al = 39;
                    memcpy(an, v + j + 5, al); an[al] = 0;
                    char av[200];
                    if (wd_attr_s(k, trim(an), av, sizeof(av))) { int l = (int)strlen(av); if (o + l > 240) l = 240 - o; memcpy(out + o, av, l); o += l; }
                    j = e + 1;
                } else if (css_word_is(v + j, vn - j, "none") || css_word_is(v + j, vn - j, "normal")) { o = 0; break; }
                else j++;
            }
            out[o] = 0;
            memcpy(ws_pe_content, out, o + 1);
            got = 1;
        }
    }
    (void)pe;
    return got && ws_pe_content[0];
}

/* texte d'une puce / d'un numero */
__attribute__((noinline)) static void wb_marker_text(int type, int num, char *out, int cap) {
    static const char *const rm[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    static const int rv[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    switch (type) {
    case LT_DISC: snprintf(out, cap, "\xE2\x80\xA2"); return;
    case LT_CIRCLE: snprintf(out, cap, "\xE2\x97\xA6"); return;
    case LT_SQUARE: snprintf(out, cap, "\xE2\x96\xAA"); return;
    case LT_DEC: snprintf(out, cap, "%d.", num); return;
    case LT_DEC0: snprintf(out, cap, "%02d.", num); return;
    case LT_LALPHA: case LT_UALPHA:
        if (num >= 1 && num <= 26) snprintf(out, cap, "%c.", (type == LT_LALPHA ? 'a' : 'A') + num - 1); else snprintf(out, cap, "%d.", num);
        return;
    case LT_LROMAN: case LT_UROMAN: {
        int o = 0, v = num;
        if (v <= 0 || v > 3999) { snprintf(out, cap, "%d.", num); return; }
        for (int i = 0; i < 13 && o < cap - 3; i++) while (v >= rv[i] && o < cap - 3) { for (const char *r = rm[i]; *r; r++) out[o++] = type == LT_UROMAN ? (char)toupper((u8)*r) : *r; v -= rv[i]; }
        out[o++] = '.'; out[o] = 0;
        return;
    }
    default: out[0] = 0;
    }
}

static int wb_is_inline_level(int d) { return d == DSP_INLINE || d == DSP_IBLOCK || d == DSP_ITABLE || d == DSP_IFLEX || d == DSP_IGRID; }
static int wb_box_kind(int d) {
    switch (d) {
    case DSP_TABLE: case DSP_ITABLE: return WB_TABLE;
    case DSP_ROWGRP: return WB_ROWGRP;
    case DSP_ROW: return WB_ROW;
    case DSP_CELL: return WB_CELL;
    case DSP_CAPTION: return WB_CAPTION;
    case DSP_FLEX: case DSP_IFLEX: return WB_FLEX;
    case DSP_GRID: case DSP_IGRID: return WB_GRID;
    default: return WB_BLOCK;
    }
}

static void wb_build(int k, wbctx_t *x, const wcs_t *par, int depth);
static int wb_has_img(int k, int depth) {       /* une image dans cet element ? (images « en absolu » des cadres a proportions fixes) */
    if (wd.n[k].tid == WT_IMG) return 1;
    if (depth > 6) return 0;
    for (int c = wd.n[k].first; c >= 0; c = wd.n[c].next) if (wd.n[c].type == 1 && wb_has_img(c, depth + 1)) return 1;
    return 0;
}
static int wb_style_anon_table(void) {
    wcs_t t; ws_init(&t, NULL); t.display = DSP_TABLE; t.bspacing = 0; t.collapse = 1; t.color = 0;
    t.w = (wl_t){ 0, 100, 0 };                  /* (tableau anonyme : toute la largeur, comme une rangee de colonnes) */
    return wb_style(&t);
}

/* <input>, <select>, <textarea>, <button type=submit> -> champ (comme avant) */
__attribute__((noinline)) static int wb_field(int k, wbctx_t *x, const wcs_t *c) {
    const wnode_t *n = &wd.n[k];
    wpage_t *p = wbp;
    char type[24] = "text", nm[128] = "", v[256] = "";
    if (n->tid == WT_INPUT) { wd_attr_s(k, "type", type, sizeof(type)); for (char *t = type; *t; t++) *t = (char)tolower((u8)*t); }
    wd_attr_s(k, "name", nm, sizeof(nm));
    wd_attr_s(k, "value", v, sizeof(v));
    if (x->form < 0) return 0;
    int fld = -1, lk = -1, w = 0, style = WS_FIELD;
    char label[200] = "";
    if (n->tid == WT_TEXTAREA) {
        char init[256] = "";
        int c0 = n->first;
        if (c0 >= 0 && wd.n[c0].type == 2) { int l = (int)wd.n[c0].tn; if (l > 255) l = 255; memcpy(init, wd.txt + wd.n[c0].t0, l); init[l] = 0; }
        fld = new_field(p, x->form, FD_TEXT, nm, init);
        if (fld < 0) return 0;
        lk = new_link(p, LK_INPUT, NULL, fld, x->form);
        w = 300;
    } else if (n->tid == WT_SELECT) {
        if (!nm[0]) return 0;
        fld = new_field(p, x->form, FD_SELECT, nm, "");
        if (fld < 0) return 0;
        p->fields[fld].opt0 = (s16)p->nopts;
        /* choix */
        int maxlab = 60;
        for (int o = n->first; o >= 0; o = wd.n[o].next) {
            int list[2] = { o, -1 };
            if (wd.n[o].tid == WT_OPTGROUP) { list[0] = wd.n[o].first; list[1] = -2; }
            for (int q = list[0]; q >= 0; q = list[1] == -2 ? wd.n[q].next : -1) {
                if (wd.n[q].tid != WT_OPTION) continue;
                char lab[200] = "", val[256];
                int t0 = wd.n[q].first;
                if (t0 >= 0 && wd.n[t0].type == 2) { int l = (int)wd.n[t0].tn; if (l > 190) l = 190; memcpy(lab, wd.txt + wd.n[t0].t0, l); lab[l] = 0; }
                char *lt = trim(lab);
                for (char *z = lt; *z; z++) if (*z == '\n' || *z == '\t') *z = ' ';
                if (!wd_attr_s(q, "value", val, sizeof(val))) snprintf(val, sizeof(val), "%s", lt);
                wfield_t *f = &p->fields[fld];
                if (new_opt(p, val, lt[0] ? lt : val) >= 0) {
                    if (f->nopt == 0) f->opt0 = (s16)(p->nopts - 1);
                    if (f->opt0 + f->nopt == p->nopts - 1) {
                        if (wd_attr(q, "selected", NULL) || f->osel < 0) f->osel = f->nopt;
                        f->nopt++;
                    }
                }
                int lw = text_width_px(F_MED, lt, 0);
                if (lw > maxlab) maxlab = lw;
            }
        }
        wfield_t *f = &p->fields[fld];
        if (f->nopt > 0) { if (f->osel < 0 || f->osel >= f->nopt) f->osel = 0; snprintf(f->value, sizeof(f->value), "%s", p->pool + p->opts[(f->opt0 + f->osel) * 2]); }
        lk = new_link(p, LK_SELECT, NULL, fld, x->form);
        w = maxlab + 34; if (w > 260) w = 260;
    } else if (!strcmp(type, "hidden")) { if (nm[0]) new_field(p, x->form, FD_HIDDEN, nm, v); return 1; }
    else if (!strcmp(type, "checkbox") || !strcmp(type, "radio")) {
        if (!nm[0]) return 1;
        fld = new_field(p, x->form, type[0] == 'c' ? FD_CHECK : FD_RADIO, nm, v[0] ? v : "on");
        if (fld < 0) return 0;
        p->fields[fld].checked = (u8)(wd_attr(k, "checked", NULL) != NULL);
        lk = new_link(p, LK_TOGGLE, NULL, fld, x->form);
        w = 22;
    } else if (!strcmp(type, "submit") || !strcmp(type, "image") || !strcmp(type, "button") || !strcmp(type, "reset")) {
        if (strcmp(type, "submit") && strcmp(type, "image")) return 1;
        fld = new_field(p, x->form, FD_SUBMIT, nm, v);
        lk = new_link(p, LK_SUBMIT, NULL, fld, x->form);
        text_display(v[0] ? v : (char *)"Envoyer", label, sizeof(label));
        if (!strcmp(type, "image") && !v[0]) { char alt[120]; if (wd_attr_s(k, "alt", alt, sizeof(alt)) && alt[0]) text_display(alt, label, sizeof(label)); }
        w = text_width_px(F_MED, label, 0) + 20;
        style = WS_BUTTON;
    } else if (!strcmp(type, "text") || !strcmp(type, "search") || !strcmp(type, "email") || !strcmp(type, "url") || !strcmp(type, "number") ||
               !strcmp(type, "tel") || !strcmp(type, "password") || !strcmp(type, "") || !strcmp(type, "date")) {
        fld = new_field(p, x->form, FD_TEXT, nm, v);
        if (fld < 0) return 0;
        p->fields[fld].secret = (u8)!strcmp(type, "password");
        lk = new_link(p, LK_INPUT, NULL, fld, x->form);
        char sz[16];
        w = wd_attr_s(k, "size", sz, sizeof(sz)) && atoi(sz) > 0 ? atoi(sz) * 8 + 12 : 220;
    } else return 1;
    if (lk < 0) return 0;
    if (c->w.k == 0 && wl_res(c->w, WS_VW) > 16) w = (int)wl_res(c->w, WS_VW);
    if (w > 560) w = 560;
    int b = wb_new(WB_FIELD, k, x->cont);
    if (b < 0) return 0;
    wbx[b].atom = 1;
    wbx[b].link = (s16)lk; wbx[b].field = (s16)fld;
    wbx[b].aw = (f32)w; wbx[b].ah = (f32)(font_height[F_MED] + 6);
    wbx[b].img = (s16)style;                    /* (style de l'element : champ ou bouton) */
    wbx[b].cs = wb_style(c);
    if (label[0]) { int o = wd_text_add(label, (int)strlen(label)); if (o >= 0) { wd.tlen++; wbx[b].marker = o; } }
    /* (la boite est enfant du bloc, et rangee dans la ligne) */
    wb_ii(wb_anon(x), II_ATOM, b, 0, x->ts, 0);
    return 1;
}

/* <img> -> boite image ; la taille vient des attributs, du CSS, ou de l'image une fois lue */
__attribute__((noinline)) static void wb_img(int k, wbctx_t *x, const wcs_t *c) {
    char v[URL_MAX], u2[URL_MAX];
    int idx = -1;
    /* <picture><source srcset> : la premiere source lisible */
    int par = wd.n[k].parent;
    if (par >= 0 && wd.n[par].tid == WT_PICTURE) {
        for (int s = wd.n[par].first; s >= 0 && idx < 0; s = wd.n[s].next) {
            if (wd.n[s].tid != WT_SOURCE) continue;
            if (wd_attr_s(s, "media", v, sizeof(v)) && !css_media_ok(v, (int)strlen(v))) continue;
            if (wd_attr_s(s, "type", v, sizeof(v)) && !(ci_find(v, "webp") || ci_find(v, "jpeg") || ci_find(v, "png") || ci_find(v, "gif"))) continue;
            if (wd_attr_s(s, "srcset", v, sizeof(v)) && img_srcset_pick(v, wv_lw, u2, sizeof(u2)) && url_join(wd.base, u2, v, sizeof(v))) idx = img_get(v);
        }
    }
    static const char *const srcs[] = { "srcset", "data-srcset", "src", "data-src", "data-original", "data-lazy-src", "data-url", NULL };
    for (int i = 0; srcs[i] && idx < 0; i++) {
        if (!wd_attr_s(k, srcs[i], v, sizeof(v)) || !v[0] || !strncasecmp(v, "data:", 5)) continue;
        if (strstr(srcs[i], "srcset")) { if (!img_srcset_pick(v, wv_lw, u2, sizeof(u2))) continue; snprintf(v, sizeof(v), "%s", u2); }
        if (url_join(wd.base, v, u2, sizeof(u2))) idx = img_get(u2);
    }
    char alt[200] = "", disp[200] = "";
    if (wd_attr_s(k, "alt", alt, sizeof(alt)) && strlen(alt) < 150) text_display(alt, disp, sizeof(disp));
    wimg_t *im = idx >= 0 ? &wimgs[idx] : NULL;
    f32 aw = 0, ah = 0;
    if (wd_attr_s(k, "width", v, sizeof(v)) && !strchr(v, '%') && atoi(v) > 0) aw = (f32)atoi(v);
    if (wd_attr_s(k, "height", v, sizeof(v)) && !strchr(v, '%') && atoi(v) > 0) ah = (f32)atoi(v);
    if (c->w.k == 0 && c->w.pct == 0 && c->w.px > 0) aw = c->w.px;
    if (c->h.k == 0 && c->h.pct == 0 && c->h.px > 0) ah = c->h.px;
    if (im) { if (aw > 0) im->aw = (s16)aw; if (ah > 0) im->ah = (s16)ah; }
    if (im && im->st == IMG_FAIL) im = NULL;
    if (!im) {                                  /* pas d'image : le texte « alt » (si utile) */
        if (disp[0] && (x->link >= 0 || strlen(disp) > 2)) {
            char t2[210];
            snprintf(t2, sizeof(t2), "[%s]", disp);
            wb_text_str(x, t2, x->ts);
        }
        return;
    }
    int b = wb_new(WB_IMG, k, x->cont);
    if (b < 0) return;
    wbx[b].atom = 1;
    wbx[b].img = (s16)idx;
    wbx[b].link = (s16)x->link;
    wbx[b].aw = aw; wbx[b].ah = ah;
    wbx[b].cs = wb_style(c);
    if (disp[0]) { int o = wd_text_add(disp, (int)strlen(disp)); if (o >= 0) { wd.tlen++; wbx[b].marker = o; } }
    if (c->position == POS_ABS) { wbx[b].absb = 1; wbx[b].atom = 0; return; }       /* (image en absolu dans un cadre) */
    if (c->flt) { wbx[b].flt = c->flt; wbx[b].atom = 0; x->anon = -1; return; }   /* (image flottante : bloc a cote du texte) */
    wb_ii(wb_anon(x), II_ATOM, b, 0, x->ts, 0);
}

static void wb_children(int k, wbctx_t *x, const wcs_t *c, int depth) {
    for (int ch = wd.n[k].first; ch >= 0 && !wb_fail; ch = wd.n[ch].next) {
        if (wd.n[ch].type == 2) wb_text_node(x, ch);
        else wb_build(ch, x, c, depth + 1);
    }
}

static int wb_list_num(int k) {                 /* numero d'un <li> : value, ou rang dans la liste (start) */
    char v[16];
    if (wd_attr_s(k, "value", v, sizeof(v))) return atoi(v);
    int par = wd.n[k].parent, start = 1;
    if (par >= 0 && wd_attr_s(par, "start", v, sizeof(v))) start = atoi(v);
    int n = 0;
    for (int q = par >= 0 ? wd.n[par].first : k; q >= 0; q = wd.n[q].next) {
        if (wd.n[q].type == 1 && wd.n[q].tid == WT_LI) {
            n++;
            if (q != k && wd_attr_s(q, "value", v, sizeof(v))) { start = atoi(v) - n + 1; }
        }
        if (q == k) break;
    }
    return start + n - 1;
}

static int wb_maxdepth = 0;
static void wb_build(int k, wbctx_t *x, const wcs_t *par, int depth) {
    if (depth > wb_maxdepth) wb_maxdepth = depth;
    if (depth > 80 || wb_fail) return;          /* (pages imbriquees a l extreme : la suite est ignoree ; les vraies : 40 au plus) */
    const wnode_t *n = &wd.n[k];
    if (n->type != 1) return;
    int t = n->tid;
    if (t == WT_SCRIPT || t == WT_STYLE || t == WT_TEMPLATE || t == WT_HEAD || t == WT_TITLE || t == WT_META || t == WT_LINK ||
        t == WT_OPTION || t == WT_OPTGROUP || t == WT_DATALIST || t == WT_PARAM || t == WT_SOURCE || t == WT_TRACK || t == WT_AREA || t == WT_MAP) return;
    int save_vars = nwvars;
    static wcs_t cstk[112];
    wcs_t *c = &cstk[depth < 111 ? depth : 111];
    ws_compute(k, par, c, 0);
    int absb = 0;
    if (c->position == POS_ABS && c->display != DSP_NONE && !(c->hide & ~1) && wb_has_img(k, 0)) { absb = 1; if (c->display == DSP_INLINE || c->display == DSP_IBLOCK) c->display = DSP_BLOCK; c->flt = 0; }
    if (c->display == DSP_NONE || c->display == DSP_COL || (c->hide & ~1) || ((c->position == POS_ABS || c->position == POS_FIXED) && !absb)) { nwvars = save_vars; return; }
    if (c->hide & 1) {                          /* visibility: hidden : la place reste, pas le contenu (on simplifie : rien) */
        nwvars = save_vars; return;
    }
    if (x->cont >= 0 && (wbx[x->cont].kind == WB_FLEX || wbx[x->cont].kind == WB_GRID) && x->anon < 0) {
        /* enfant d'un flex / d'une grille : toujours une boite (blockify) */
        if (c->display == DSP_INLINE || c->display == DSP_IBLOCK || c->display == DSP_LIST) c->display = DSP_BLOCK;
        else if (c->display == DSP_ITABLE) c->display = DSP_TABLE;
        else if (c->display == DSP_IFLEX) c->display = DSP_FLEX;
        else if (c->display == DSP_IGRID) c->display = DSP_GRID;
        else if (c->display == DSP_CELL || c->display == DSP_ROW || c->display == DSP_ROWGRP) c->display = DSP_BLOCK;
    }
    if (c->flt && (c->display == DSP_CELL || c->display == DSP_ROW)) c->display = DSP_BLOCK;
    int cont_ov = -1;
    if (x->cont >= 0) {                         /* case / rangee hors tableau : tableau anonyme autour */
        int pk = wbx[x->cont].kind, d0 = c->display;
        if (d0 == DSP_CELL && pk != WB_ROW) {
            int cont = x->cont;
            if (pk != WB_TABLE && pk != WB_ROWGRP) {
                int last = wbx[cont].last;
                if (!(last >= 0 && wbx[last].node == -2 && x->anon < 0)) { last = wb_new(WB_TABLE, -2, cont); if (last >= 0) wbx[last].cs = wb_style_anon_table(); }
                cont = last;
            }
            if (cont >= 0) {
                int lr = wbx[cont].last;
                if (!(lr >= 0 && wbx[lr].node == -3 && wbx[lr].kind == WB_ROW)) { lr = wb_new(WB_ROW, -3, cont); if (lr >= 0) wbx[lr].cs = wbx[cont].cs; }
                if (lr >= 0) { cont_ov = lr; x->anon = -1; }
            }
        } else if (d0 == DSP_ROW && pk != WB_TABLE && pk != WB_ROWGRP) {
            int last = wbx[x->cont].last;
            if (!(last >= 0 && wbx[last].node == -2 && x->anon < 0)) { last = wb_new(WB_TABLE, -2, x->cont); if (last >= 0) wbx[last].cs = wb_style_anon_table(); }
            if (last >= 0) { cont_ov = last; x->anon = -1; }
        }
    }
    if ((t == WT_SVG || t == WT_IFRAME || t == WT_VIDEO || t == WT_CANVAS || t == WT_OBJECT || t == WT_EMBED || t == WT_AUDIO || t == WT_PROGRESS || t == WT_METER)) {
        /* pas affiche ; une petite case vide garde la place d'une icone (svg avec sa taille) */
        if (t == WT_SVG) {
            f32 w = c->w.k == 0 && !c->w.pct ? c->w.px : 0, h = c->h.k == 0 && !c->h.pct ? c->h.px : 0;
            char v[16];
            if (w <= 0 && wd_attr_s(k, "width", v, sizeof(v)) && !strchr(v, '%')) w = (f32)atof(v);
            if (h <= 0 && wd_attr_s(k, "height", v, sizeof(v)) && !strchr(v, '%')) h = (f32)atof(v);
            if (w > 0 && h > 0 && w <= 48 && h <= 48 && wb_is_inline_level(c->display)) {
                int b = wb_new(WB_SPACE, k, x->cont);
                if (b >= 0) { wbx[b].atom = 1; wbx[b].aw = w; wbx[b].ah = h; wbx[b].cs = wb_style(c); wb_ii(wb_anon(x), II_ATOM, b, 0, x->ts, 0); }
            }
        }
        nwvars = save_vars; return;
    }
    /* lien */
    int link = x->link;
    if (t == WT_A) {
        static char v[URL_MAX], v2[URL_MAX];   /* (statiques : la pile des fils est petite, et wb_build s'appelle lui-meme) */
        if (wd_attr_s(k, "href", v, sizeof(v)) && url_join(wd.base, v, v2, sizeof(v2))) link = new_link(wbp, LK_HREF, v2, -1, -1);
    }
    int form = x->form;
    if (t == WT_FORM && wbp->nforms < W_MAX_FORMS) {
        static char v[URL_MAX], act[URL_MAX];
        if (!wd_attr_s(k, "action", v, sizeof(v)) || !v[0] || !url_join(wd.base, v, act, sizeof(act))) snprintf(act, sizeof(act), "%s", wbp->url);
        wbp->forms[wbp->nforms].action = pool_add(wbp, act);
        wbp->forms[wbp->nforms].post = wd_attr_s(k, "method", v, sizeof(v)) && !strcasecmp(v, "post");
        form = wbp->nforms++;
    }
    if (t == WT_BODY || t == WT_HTML) {         /* fond de la page */
        if (c->bg && (t == WT_BODY || !wbp->pagebg || wbp->pagebg == WEB_PAGEBG)) { if (t == WT_HTML || wbp->pagebg == WEB_PAGEBG) wbp->pagebg = c->bg; }
    }
    if (t == WT_IMG) { wb_img(k, x, c); nwvars = save_vars; return; }
    if (t == WT_INPUT || t == WT_SELECT || t == WT_TEXTAREA) {
        wbctx_t xx = *x; xx.form = form;
        wb_field(k, &xx, c);
        x->anon = xx.anon;
        nwvars = save_vars; return;
    }
    if (t == WT_BUTTON) {                       /* bouton : son contenu, cliquable (envoie le formulaire) */
        char ty[16] = "submit";
        wd_attr_s(k, "type", ty, sizeof(ty));
        if (form >= 0 && !strcasecmp(ty, "submit")) {
            static char nm[128], v[256];
            nm[0] = v[0] = 0;
            wd_attr_s(k, "name", nm, sizeof(nm)); wd_attr_s(k, "value", v, sizeof(v));
            int f = new_field(wbp, form, FD_SUBMIT, nm, v);
            link = new_link(wbp, LK_SUBMIT, NULL, f, form);
        }
    }
    int ts = wb_ts_from(c, link);
    int d = c->display;
    if (wb_is_inline_level(d) && t == WT_BR) { wb_ii(wb_anon(x), II_BR, 0, 0, x->ts, 0); nwvars = save_vars; return; }
    if (t == WT_WBR) { if (x->anon >= 0) wb_ii(x->anon, II_WBR, 0, 0, x->ts, 0); nwvars = save_vars; return; }
    if (d == DSP_CONTENTS) {
        wbctx_t xx = *x; xx.ts = ts; xx.link = link; xx.form = form;
        wb_children(k, &xx, c, depth);
        x->anon = xx.anon;
        nwvars = save_vars; return;
    }
    if (d == DSP_INLINE && !c->flt) {
        /* element en ligne : ses mots prennent son style ; marges / padding / bordure a gauche et a droite */
        f32 lsp = (c->m[3].k ? 0 : wl_res(c->m[3], WS_VW)) + wl_res(c->p[3], WS_VW) + (c->bst[3] ? c->bw[3] : 0);
        f32 rsp = (c->m[1].k ? 0 : wl_res(c->m[1], WS_VW)) + wl_res(c->p[1], WS_VW) + (c->bst[1] ? c->bw[1] : 0);
        if (c->bg && c->bg != 0) {                  /* fond des mots */
            wts_t tt = wtss[ts]; tt.bg = c->bg;
            if (grow((void **)&wtss, &cwts, nwts + 1, sizeof(wts_t), WB_MAX_TS)) { wtss[nwts] = tt; ts = nwts++; }
        } else if (x->ts >= 0 && wtss[x->ts].bg) {
            wts_t tt = wtss[ts]; tt.bg = wtss[x->ts].bg;
            if (grow((void **)&wtss, &cwts, nwts + 1, sizeof(wts_t), WB_MAX_TS)) { wtss[nwts] = tt; ts = nwts++; }
        }
        if (lsp > 0.5f) wb_ii(wb_anon(x), II_OPEN, 0, 0, ts, lsp);
        wbctx_t xx = *x; xx.ts = ts; xx.link = link; xx.form = form;
        if (ws.has_pe && wb_pe_content(k, 1)) {
            static wcs_t pc; ws_compute(k, c, &pc, 1);
            if (wb_pe_content(k, 1)) wb_text_str(&xx, ws_pe_content, wb_ts_from(&pc, link));
        }
        wb_children(k, &xx, c, depth);
        if (ws.has_pe) {
            static wcs_t pc;
            if (ws_compute(k, c, &pc, 2) && wb_pe_content(k, 2)) wb_text_str(&xx, ws_pe_content, wb_ts_from(&pc, link));
        }
        x->anon = xx.anon;
        if (link >= 0 && link != x->link && wbp->links[link].y0 < 0) {
            /* lien sans texte : [lien] (seulement s'il n'a rien donne) */
        }
        if (rsp > 0.5f && x->anon >= 0) wb_ii(x->anon, II_CLOSE, 0, 0, ts, rsp);
        nwvars = save_vars;
        return;
    }
    /* boite (bloc, case, ligne de tableau, flex, inline-block...) */
    int is_atom = wb_is_inline_level(d) && !c->flt && !absb;
    if (c->flt && d == DSP_INLINE) d = DSP_BLOCK;
    if (c->flt && d == DSP_IBLOCK) d = DSP_BLOCK;
    int parent_box = cont_ov >= 0 ? cont_ov : x->cont;
    if (!is_atom && !absb) x->anon = -1;        /* (bloc : la ligne en cours s'arrete) */
    int b = wb_new(wb_box_kind(d), k, parent_box);
    if (b < 0) { nwvars = save_vars; return; }
    wbx[b].absb = (u8)absb;
    wbx[b].atom = (u8)is_atom;
    wbx[b].flt = c->flt;
    wbx[b].link = (s16)link;
    wbx[b].ts = (s16)ts;
    if (t == WT_TD || t == WT_TH) {
        char v[16];
        if (wd_attr_s(k, "colspan", v, sizeof(v))) { int s = atoi(v); wbx[b].colspan = (s16)(s < 1 ? 1 : s > 40 ? 40 : s); }
        if (wd_attr_s(k, "rowspan", v, sizeof(v))) { int s = atoi(v); wbx[b].rowspan = (s16)(s < 0 ? 1 : s > 200 ? 200 : s == 0 ? 50 : s); }
    }
    if (d == DSP_LIST && c->list != LT_NONE) {
        char mk[24];
        wb_marker_text(c->list, wb_list_num(k), mk, sizeof(mk));
        if (mk[0]) { int o = wd_text_add(mk, (int)strlen(mk)); if (o >= 0) { wd.tlen++; wbx[b].marker = o; wbx[b].list = (u8)(1 + c->list_inside); } }
    }
    wbx[b].cs = wb_style(c);
    if (is_atom) wb_ii(wb_anon(x), II_ATOM, b, 0, x->ts, 0);
    wbctx_t xx;
    xx.cont = b; xx.anon = -1; xx.ts = ts; xx.link = link; xx.form = form;
    if (ws.has_pe) {
        static wcs_t pc;
        if (ws_compute(k, c, &pc, 1) && wb_pe_content(k, 1) && pc.display != DSP_NONE) wb_text_str(&xx, ws_pe_content, wb_ts_from(&pc, link));
    }
    wb_children(k, &xx, c, depth);
    if (ws.has_pe) {
        static wcs_t pc;
        if (ws_compute(k, c, &pc, 2) && wb_pe_content(k, 2) && pc.display != DSP_NONE) wb_text_str(&xx, ws_pe_content, wb_ts_from(&pc, link));
    }
    if (!is_atom && !absb) x->anon = -1;
    nwvars = save_vars;
}

/* ---------- mesure du texte ---------- */
/* UTF-8 -> texte affichable (police), avec majuscules / minuscules ; -> octets ecrits */
static int wb_disp(const char *s, int n, char *out, int cap, int transform, int *cap_next) {
    int o = 0, pos = 0;
    while (pos < n && o < cap - 8) {
        unsigned cp = read_cp((const unsigned char *)s, n, &pos, 0);
        if (transform == 1 || (transform == 3 && *cap_next)) { if (cp < 128) cp = (unsigned)toupper((int)cp); else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 32; }
        else if (transform == 2) { if (cp < 128) cp = (unsigned)tolower((int)cp); else if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 32; }
        if (transform == 3) *cap_next = 0;
        o += cp_out(cp, out + o);
    }
    out[o] = 0;
    return o;
}
static int wb_tw(int f, const char *s) {        /* largeur (sans traduction ni echelle) */
    int w = 0;
    while (*s) { int cp = next_cp(&s); w += font_glyphs[f][glyph_index(cp)].adv; }
    return w;
}

/* ---------- sortie : mots, fonds, images ---------- */
static u32 wb_effbg = WEB_PAGEBG;               /* fond sous ce qui est mis en page (lisibilite du texte) */
static int wb_out_item(f32 x, f32 ybase, f32 w, int font, int style, int fx, u32 col, u32 bg, int link, const char *txt, int n) {
    wpage_t *p = wbp;
    if (!grow((void **)&p->items, &p->icap, p->nitems + 1, sizeof(witem_t), W_MAX_ITEMS)) { p->truncated = 1; return -1; }
    if (!text_append(p, txt, n, 0)) return -1;
    witem_t *it = &p->items[p->nitems++];
    memset(it, 0, sizeof(*it));
    it->off = p->tlen - n - 1;
    it->x = (s16)(x < -30000 ? -30000 : x > 30000 ? 30000 : x);
    it->y = (s32)(ybase + 0.5f);
    it->w = (u16)(w < 0 ? 0 : w > 65000 ? 65000 : w + 0.5f);
    it->font = (u8)font;
    it->style = (u8)style;
    it->fx = (u8)fx;
    it->col = col;
    it->bg = bg;
    it->link = (s16)link;
    it->img = -1;
    return p->nitems - 1;
}
static int wb_out_box(f32 x0, f32 y0, f32 x1, f32 y1, u32 bg, u32 border, int sides) {
    wpage_t *p = wbp;
    if (!grow((void **)&p->boxes, &p->bcap, p->nboxes + 1, sizeof(wbox_t), 30000)) return -1;
    wbox_t *b = &p->boxes[p->nboxes];
    b->x0 = (s16)(x0 + 0.5f); b->x1 = (s16)(x1 + 0.5f); b->y0 = (s32)(y0 + 0.5f); b->y1 = (s32)(y1 + 0.5f);
    b->bg = bg; b->border = border; b->sides = (u8)sides;
    return p->nboxes++;
}
static void wb_shift(int it0, int it1, int bx0, int bx1, f32 dx, f32 dy) {
    int ix = (int)(dx + (dx >= 0 ? 0.5f : -0.5f)), iy = (int)(dy + (dy >= 0 ? 0.5f : -0.5f));
    if (!ix && !iy) return;
    for (int i = it0; i < it1 && i < wbp->nitems; i++) { wbp->items[i].x = (s16)(wbp->items[i].x + ix); wbp->items[i].y += iy; }
    for (int i = bx0; i < bx1 && i < wbp->nboxes; i++) { wbox_t *b = &wbp->boxes[i]; b->x0 = (s16)(b->x0 + ix); b->x1 = (s16)(b->x1 + ix); b->y0 += iy; b->y1 += iy; }
}
static u32 wb_readable(u32 col, u32 bg) {
    int dl = col_luma(col) - col_luma(bg ? bg : WEB_PAGEBG);
    if (dl < 0) dl = -dl;
    if (dl < 45) return col_luma(bg ? bg : WEB_PAGEBG) > 140 ? WEB_FG : 0xf2f2f2ffu;
    return col;
}

/* ---------- flottants ---------- */
typedef struct { f32 x0, x1, y0, y1; u8 side; } wflt_t;
#define WF_MAX 512
static wflt_t wflts[WF_MAX];
static int nwflts = 0, wflt_base = 0;
static void wb_fbounds(f32 y0, f32 y1, f32 cx0, f32 cx1, f32 *l, f32 *r) {
    *l = cx0; *r = cx1;
    for (int i = wflt_base; i < nwflts; i++) {
        const wflt_t *f = &wflts[i];
        if (f->y1 <= y0 || f->y0 >= y1) continue;
        if (f->side == 1) { if (f->x1 > *l && f->x0 < cx1) *l = f->x1; }
        else { if (f->x0 < *r && f->x1 > cx0) *r = f->x0; }
    }
}
static f32 wb_fbottom(int side, f32 y) {         /* bas des flottants (cote 1, 2 ou 3) */
    f32 b = y;
    for (int i = wflt_base; i < nwflts; i++) if ((wflts[i].side & side) && wflts[i].y1 > b) b = wflts[i].y1;
    return b;
}
static f32 wb_fnext(f32 y) {                     /* prochain bas de flottant sous y */
    f32 b = 1e9f;
    for (int i = wflt_base; i < nwflts; i++) if (wflts[i].y1 > y + 0.5f && wflts[i].y1 < b) b = wflts[i].y1;
    return b < 1e9f ? b : y;
}

/* ---------- marges qui se fondent ---------- */
static f32 mp_pos = 0, mp_neg = 0;
#define WT_WAIT 128
static s32 wb_wait[WT_WAIT];                    /* fonds dont le haut attend les marges */
static int nwb_wait = 0;
static void mp_add(f32 m) { if (m > mp_pos) mp_pos = m; if (m < mp_neg) mp_neg = m; }
static void wb_flush(f32 *y) {
    *y += mp_pos + mp_neg;
    mp_pos = mp_neg = 0;
    for (int i = 0; i < nwb_wait; i++) if (wb_wait[i] >= 0 && wb_wait[i] < wbp->nboxes) wbp->boxes[wb_wait[i]].y0 = (s32)(*y + 0.5f);
    nwb_wait = 0;
}

/* ---------- tailles ---------- */
static const wcs_t *WCS(int b) { static wcs_t def; static int init = 0; if (!init) { ws_init(&def, NULL); def.display = DSP_BLOCK; init = 1; } return wbx[b].cs >= 0 ? &wbs[wbx[b].cs] : &def; }
static f32 wb_pad(const wcs_t *c, int s, f32 cb) { f32 v = wl_res(c->p[s], cb); return v > 0 ? v : 0; }
static f32 wb_bw(const wcs_t *c, int s) { return c->bst[s] ? c->bw[s] : 0; }
static f32 wb_mar(const wcs_t *c, int s, f32 cb) { return c->m[s].k ? 0 : wl_res(c->m[s], cb); }
static f32 wb_hextra(const wcs_t *c, f32 cb) { return wb_pad(c, 1, cb) + wb_pad(c, 3, cb) + wb_bw(c, 1) + wb_bw(c, 3); }
static int wb_spec_w(const wcs_t *c, f32 cb, f32 *out) {   /* largeur du contenu donnee par la page */
    if (c->w.k != 0) return 0;
    if (c->w.pct != 0 && cb <= 0) return 0;
    f32 w = wl_res(c->w, cb);
    if (c->boxsz) w -= wb_hextra(c, cb);
    *out = w < 0 ? 0 : w;
    return 1;
}
static f32 wb_clamp_w(const wcs_t *c, f32 w, f32 cb) {
    if (c->maxw.k == 0) { f32 m = wl_res(c->maxw, cb); if (c->boxsz) m -= wb_hextra(c, cb); if (w > m) w = m; }
    if (c->minw.k == 0) { f32 m = wl_res(c->minw, cb); if (c->boxsz) m -= wb_hextra(c, cb); if (w < m) w = m; }
    return w < 0 ? 0 : w;
}

/* taille d'une image (attributs / CSS / taille reelle), au plus maxw de large */
static void wb_img_size(int b, f32 maxw, f32 *w, f32 *h) {
    const wbx_t *x = &wbx[b];
    const wcs_t *c = WCS(b);
    wimg_t *im = x->img >= 0 ? &wimgs[x->img] : NULL;
    f32 aw = x->aw, ah = x->ah;
    if (c->w.k == 0 && c->w.pct > 0 && maxw > 0) aw = c->w.pct * maxw * 0.01f;
    int iw = 0, ih = 0;
    f32 lim = maxw;
    if (c->maxw.k == 0) { f32 m = wl_res(c->maxw, maxw); if (m > 0 && m < lim) lim = m; }
    img_box((int)aw, (int)ah, im ? im->nw : 0, im ? im->nh : 0, (int)(lim > 1 ? lim : 1), &iw, &ih);
    if (iw > 0 && ih > 0) { *w = (f32)iw; *h = (f32)ih; return; }
    /* pas encore lue : petite case avec le texte */
    const char *lab = x->marker >= 0 ? wd.txt + x->marker : "";
    char fit[48];
    fit_text(F_SMALL, lab, 160, fit, sizeof(fit));
    f32 tw = (f32)wb_tw(F_SMALL, fit) + 12;
    *w = tw < 24 ? 24 : tw;
    *h = 18;
    if (aw > 0) *w = aw > lim ? lim : aw;
    if (ah > 0 && aw > 0) *h = ah * (*w) / aw;
    else if (ah > 0) *h = ah;
}

static void wb_intrinsic(int b, f32 *mn, f32 *mx);
/* mots et elements en ligne : plus long mot (min), ligne la plus longue sans retour (max) */
static void wb_inline_intrinsic(int anon, f32 *mn, f32 *mx) {
    f32 lmin = 0, lmax = 0, cur = 0, word = 0;
    int pend = 0;
    static char buf[1300];                      /* (pas garde pendant les appels imbriques : statique) */
    for (int i = wbx[anon].i0; i >= 0; i = wii[i].next) {
        const wii_t *t = &wii[i];
        const wts_t *s = &wtss[t->ts];
        if (t->type == II_TEXT) {
            const char *tx = wd.txt + t->a;
            int n = t->n, j = 0;
            int pre = s->ws == WSP_PRE || s->ws == WSP_PREWRAP || s->ws == WSP_PRELINE;
            int nowrap = s->ws == WSP_NOWRAP || s->ws == WSP_PRE;
            f32 spw = (f32)font_glyphs[s->font][0].adv;
            while (j < n) {
                char ch = tx[j];
                if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '\f') {
                    if (pre && ch == '\n') { if (cur > lmax) lmax = cur; cur = 0; if (word > lmin) lmin = word; word = 0; pend = 0; j++; continue; }
                    if (!nowrap) { if (word > lmin) lmin = word; word = 0; }
                    else word += spw;
                    pend = 1; j++;
                    continue;
                }
                int s0 = j;
                while (j < n && tx[j] != ' ' && tx[j] != '\n' && tx[j] != '\t' && tx[j] != '\r' && tx[j] != '\f') j++;
                int cn = 0;
                wb_disp(tx + s0, j - s0, buf, sizeof(buf), s->transform, &cn);
                f32 w = (f32)wb_tw(s->font, buf) + ((s->fx & FX_BOLD) ? 1 : 0);
                if (pend && cur > 0) cur += spw;
                cur += w;
                word += w;
                pend = 0;
            }
        } else if (t->type == II_OPEN || t->type == II_CLOSE) { cur += t->sp; word += t->sp; }
        else if (t->type == II_ATOM) {
            f32 a, z;
            wb_intrinsic(t->a, &a, &z);
            if (pend && cur > 0) cur += 4;
            cur += z;
            if (word > lmin) lmin = word;
            word = 0;
            if (a > lmin) lmin = a;
            pend = 0;
        } else if (t->type == II_BR) { if (cur > lmax) lmax = cur; cur = 0; if (word > lmin) lmin = word; word = 0; pend = 0; }
    }
    if (cur > lmax) lmax = cur;
    if (word > lmin) lmin = word;
    *mn = lmin; *mx = lmax;
}
static void wb_intrinsic(int b, f32 *mn, f32 *mx) {
    wbx_t *x = &wbx[b];
    if (x->minw >= 0) { *mn = x->minw; *mx = x->maxw; return; }
    x->minw = 0; x->maxw = 0;                   /* (contre les boucles) */
    const wcs_t *c = WCS(b);
    f32 a = 0, z = 0;
    if (x->kind == WB_ANON) { wb_inline_intrinsic(b, &a, &z); x->minw = a; x->maxw = z; *mn = a; *mx = z; return; }
    if (x->kind == WB_IMG) { f32 w, h; wb_img_size(b, 560, &w, &h); a = z = w; }
    else if (x->kind == WB_FIELD || x->kind == WB_SPACE) a = z = x->aw;
    else if (x->kind == WB_TABLE) {
        /* rangees : somme des cases ; la plus large */
        f32 sp = c->collapse ? 0 : c->bspacing;
        for (int g = x->first; g >= 0; g = wbx[g].next) {
            int rows[2] = { g, -1 };
            int multi = wbx[g].kind == WB_ROWGRP;
            for (int r = multi ? wbx[g].first : g; r >= 0; r = multi ? wbx[r].next : -1) {
                if (wbx[r].kind == WB_CAPTION) { f32 p, q; wb_intrinsic(r, &p, &q); if (p > a) a = p; if (q > z) z = q; continue; }
                f32 ra = sp, rz = sp;
                for (int cl = wbx[r].first; cl >= 0; cl = wbx[cl].next) { f32 p, q; wb_intrinsic(cl, &p, &q); ra += p + sp; rz += q + sp; }
                if (wbx[r].kind != WB_ROW) { f32 p, q; wb_intrinsic(r, &p, &q); ra = p; rz = q; }
                if (ra > a) a = ra;
                if (rz > z) z = rz;
            }
            (void)rows;
        }
    } else if (x->kind == WB_FLEX && !c->fdir) {
        int n = 0;
        for (int ch = x->first; ch >= 0; ch = wbx[ch].next) {
            f32 p, q;
            wb_intrinsic(ch, &p, &q);
            if (wbx[ch].flt) continue;
            if (c->fwrap) { if (p > a) a = p; } else a += p;
            z += q;
            n++;
        }
        if (n > 1) { z += c->gap_c * (n - 1); if (!c->fwrap) a += c->gap_c * (n - 1); }
    } else if (x->kind == WB_GRID && c->gcols > 1) {
        f32 cm = 0, cz = 0;
        for (int ch = x->first; ch >= 0; ch = wbx[ch].next) { f32 p, q; wb_intrinsic(ch, &p, &q); if (p > cm) cm = p; if (q > cz) cz = q; }
        a = cm; z = cz * c->gcols + c->gap_c * (c->gcols - 1);
    } else {
        f32 fl = 0;                              /* (flottants cote a cote : leurs largeurs s'ajoutent) */
        for (int ch = x->first; ch >= 0; ch = wbx[ch].next) {
            f32 p, q;
            wb_intrinsic(ch, &p, &q);
            if (p > a) a = p;
            if (wbx[ch].flt) { fl += q; if (fl > z) z = fl; } else { fl = 0; if (q > z) z = q; }
        }
    }
    f32 sw;
    if (x->kind != WB_IMG && x->kind != WB_FIELD && wb_spec_w(c, 0, &sw)) { a = z = sw; }
    else if (x->kind != WB_IMG) { if (c->minw.k == 0 && !c->minw.pct) { f32 m = c->minw.px; if (a < m) a = m; if (z < m) z = m; } if (c->maxw.k == 0 && !c->maxw.pct) { f32 m = c->maxw.px; if (z > m) z = m; if (a > m) a = m; } }
    f32 ex = wb_hextra(c, 0) + wb_mar(c, 1, 0) + wb_mar(c, 3, 0);
    if (x->kind == WB_IMG) ex = wb_mar(c, 1, 0) + wb_mar(c, 3, 0);
    a += ex; z += ex;
    if (z < a) z = a;
    x->minw = a; x->maxw = z;
    *mn = a; *mx = z;
}

/* ---------- lignes de texte ---------- */
static void wb_layout_box(int b, f32 cx, f32 cw, f32 *y);
static f32 wb_atom_layout(int b, f32 avail, f32 *w, f32 *h, int *it0, int *it1, int *bx0, int *bx1);

typedef struct {
    u8 kind;                                    /* 1 mot, 2 boite, 3 espace (OPEN / CLOSE) */
    s16 ts;
    f32 x, w;
    u16 t0, tn;                                 /* mot : dans L->buf */
    u8 space;                                   /* espace avant */
    s32 box;
    f32 h;                                      /* boite : hauteur */
    s32 it0, it1, bx0, bx1;                     /* boite : ce qu'elle a produit (place a l'origine) */
} wpc_t;
#define WPC_MAX 600
#define WLB_MAX 16384
static int wb_marker_box = -1;                  /* puce en attente : sur la premiere ligne de l'element de liste */

typedef struct {
    f32 cx0, cx1;                               /* bords du bloc */
    f32 y;                                      /* haut de la ligne en cours */
    f32 l, r;                                   /* bords de la ligne (flottants) */
    f32 x;                                      /* position dans la ligne */
    int first;                                  /* premiere ligne (retrait) */
    int align;
    f32 indent;
    f32 lh_min;                                 /* hauteur de ligne du bloc */
    int pend_space;
    int empty_br;
    wpc_t *pc; int npc;                         /* pieces de la ligne (a chaque appel : les inline-block ont les leurs) */
    char *buf; int blen;
} wline_t;

static void wb_line_begin(wline_t *L) {
    f32 est = L->lh_min > 0 ? L->lh_min : 18;
    wb_fbounds(L->y, L->y + est, L->cx0, L->cx1, &L->l, &L->r);
    L->x = L->l + (L->first ? L->indent : 0);
    L->npc = 0;
    L->blen = 0;
    L->pend_space = 0;
}
static void wb_emit_marker(f32 xleft, f32 base, int ts) {
    int b = wb_marker_box;
    wb_marker_box = -1;
    if (b < 0 || wbx[b].marker < 0) return;
    const wts_t *s = &wtss[ts >= 0 ? ts : 0];
    char d[32];
    int cn = 0;
    wb_disp(wd.txt + wbx[b].marker, (int)strlen(wd.txt + wbx[b].marker), d, sizeof(d), 0, &cn);
    int f = s->font == F_LARGE ? F_MED : s->font;
    f32 w = (f32)wb_tw(f, d);
    u32 col = wb_readable(s->col ? s->col : WEB_FG, wb_effbg);
    wb_out_item(xleft - w - 6, base, w, f, 0, 0, col, 0, -1, d, (int)strlen(d));
}
/* fin de ligne : hauteur, alignement, sortie */
static void wb_line_end(wline_t *L, int forced) {
    if (L->npc == 0 && !(forced && L->empty_br)) { return; }
    f32 asc = 0, desc = 0;
    int deftf = F_MED;
    for (int i = 0; i < L->npc; i++) {
        const wpc_t *p = &L->pc[i];
        const wts_t *s = &wtss[p->ts];
        if (p->kind == 2) {
            const wcs_t *c = WCS(p->box);
            f32 mt = wb_mar(c, 0, 0), mb = wb_mar(c, 2, 0);
            if (s->valign == VA_MIDDLE || c->valign == VA_MIDDLE) { f32 hh = (p->h + mt + mb) / 2; if (hh + 4 > asc) asc = hh + 4; if (hh - 4 > desc) desc = hh - 4; }
            else { if (p->h + mt + mb > asc) asc = p->h + mt + mb; }
            continue;
        }
        if (p->kind == 3 && p->w <= 0) continue;
        int f = s->font;
        f32 a = font_ascent[f], dd = font_height[f] - font_ascent[f];
        if (s->lh > 0) { f32 extra = (s->lh - font_height[f]) / 2; if (extra > 0 && extra < 40) { a += extra; dd += extra; } }
        if (s->valign == VA_SUPER) a += font_height[f] * 0.3f;
        if (s->valign == VA_SUB) dd += font_height[f] * 0.25f;
        if (a > asc) asc = a;
        if (dd > desc) desc = dd;
        deftf = f;
    }
    if (L->npc == 0 || (asc == 0 && desc == 0)) { asc = font_ascent[deftf]; desc = font_height[deftf] - asc; }
    f32 lh = asc + desc + 3;
    if (L->lh_min > lh) { f32 d = (L->lh_min - lh) / 2; asc += d; lh = L->lh_min; }
    /* alignement */
    f32 end = L->x, shift = 0;
    while (L->npc > 0 && end > L->l && 0) {}
    f32 room = L->r - end;
    if (room > 0 && L->align == 2) shift = room / 2;
    else if (room > 0 && L->align == 3) shift = room;
    f32 base = L->y + asc;
    if (wb_marker_box >= 0) wb_emit_marker(L->l, base, L->npc > 0 ? L->pc[0].ts : 0);
    /* sortie : mots de meme style colles en un element */
    int i = 0;
    while (i < L->npc) {
        wpc_t *p = &L->pc[i];
        const wts_t *s = &wtss[p->ts];
        if (p->kind == 2) {
            const wcs_t *c = WCS(p->box);
            f32 top;
            if (s->valign == VA_MIDDLE || c->valign == VA_MIDDLE) top = base - 4 - p->h / 2;
            else if (s->valign == VA_TOP || c->valign == VA_TOP) top = L->y;
            else top = base - p->h - wb_mar(c, 2, 0);
            wbx_t *bx = &wbx[p->box];
            if (bx->kind == WB_IMG) {
                f32 ix = p->x + shift + wb_mar(c, 3, 0);
                int it = wb_out_item(ix, top + p->h, p->w - wb_mar(c, 3, 0) - wb_mar(c, 1, 0), F_SMALL, WS_IMG | (bx->link >= 0 ? WS_LINK : 0), 0, 0, 0, bx->link,
                                     bx->marker >= 0 ? wd.txt + bx->marker : "", bx->marker >= 0 ? (int)strlen(wd.txt + bx->marker) : 0);
                if (it >= 0) { wbp->items[it].img = bx->img; wbp->items[it].h = (u16)p->h; }
            } else if (bx->kind == WB_FIELD) {
                const char *lab = bx->marker >= 0 ? wd.txt + bx->marker : "";
                wb_out_item(p->x + shift + wb_mar(c, 3, 0), top + 3 + font_ascent[F_MED], bx->aw, F_MED, bx->img, 0, 0, 0, bx->link, lab, (int)strlen(lab));
            } else if (bx->kind != WB_SPACE) {
                wb_shift(p->it0, p->it1, p->bx0, p->bx1, p->x + shift, top);
            }
            i++;
            continue;
        }
        if (p->kind == 3) { i++; continue; }
        /* mots qui se suivent avec le meme style */
        static char run[2048];
        int rn = 0;
        f32 x0 = p->x + shift, x1 = p->x + p->w + shift;
        int j = i;
        while (j < L->npc && L->pc[j].kind == 1 && L->pc[j].ts == p->ts) {
            if (j > i && L->pc[j].space && rn < (int)sizeof(run) - 2) run[rn++] = ' ';
            else if (j > i && !L->pc[j].space && L->pc[j].x > L->pc[j - 1].x + L->pc[j - 1].w + 0.5f) break;
            int l = L->pc[j].tn;
            if (rn + l > (int)sizeof(run) - 2) break;
            memcpy(run + rn, L->buf + L->pc[j].t0, l);
            rn += l;
            x1 = L->pc[j].x + L->pc[j].w + shift;
            j++;
        }
        if (j == i) j = i + 1;
        f32 yb = base;
        if (s->valign == VA_SUPER) yb -= font_height[s->font] * 0.33f;
        else if (s->valign == VA_SUB) yb += font_height[s->font] * 0.22f;
        u32 col = s->col;
        if (col) {
            col = wb_readable(col, s->bg ? s->bg : wb_effbg);
            int style = s->link >= 0 ? WS_LINK : 0;
            wb_out_item(x0, yb, x1 - x0, s->font, style, s->fx, col, s->bg, s->link, run, rn);
        }
        i = j;
    }
    L->y += lh;
    L->first = 0;
    L->empty_br = 0;
    L->npc = 0;
    L->blen = 0;
    wb_line_begin(L);
}
/* une piece (mot ou boite) ; passe a la ligne si besoin */
static wpc_t *wb_piece(wline_t *L, int kind, f32 w, int space, int nobreak) {
    f32 sw = space && L->npc > 0 ? 4 : 0;
    if (kind == 1 && space && L->npc > 0) sw = 0;     /* (le mot compte son espace lui-meme) */
    if (L->x + sw + w > L->r + 0.5f && L->npc > 0 && !nobreak) wb_line_end(L, 0);
    if (L->npc == 0 && L->x + w > L->r + 0.5f && L->r - L->l < L->cx1 - L->cx0 - 1) {
        /* a cote d'un flottant, ca ne tient pas : sous le flottant */
        int guard = 0;
        while (L->x + w > L->r + 0.5f && guard++ < 50) {
            f32 ny = wb_fnext(L->y);
            if (ny <= L->y + 0.5f) break;
            L->y = ny;
            wb_line_begin(L);
        }
    }
    if (L->npc >= WPC_MAX) wb_line_end(L, 0);
    wpc_t *p = &L->pc[L->npc++];
    memset(p, 0, sizeof(*p));
    p->kind = (u8)kind;
    p->space = (u8)(space && L->npc > 1);
    p->w = w;
    return p;
}
static void wb_word(wline_t *L, const char *d, int dn, int ts, int space, int nobreak) {
    const wts_t *s = &wtss[ts];
    int f = s->font;
    f32 spw = (space && L->npc > 0) ? (f32)font_glyphs[f][0].adv : 0;
    f32 w = (f32)wb_tw(f, d) + ((s->fx & FX_BOLD) ? 1 : 0);
    f32 avail = L->cx1 - L->cx0;
    if (w > avail && dn > 1) {                  /* mot plus long qu'une ligne : coupe */
        int cut = 0; f32 ws_ = 0;
        const char *q = d;
        f32 room = L->r - L->x - spw;
        if (room < avail * 0.25f) room = avail;
        while (*q) {
            const char *q0 = q;
            int cp = next_cp(&q);
            f32 a = font_glyphs[f][glyph_index(cp)].adv;
            if (ws_ + a > room && cut > 0) { q = q0; break; }
            ws_ += a; cut = (int)(q - d);
        }
        if (cut <= 0 || cut >= dn) { cut = dn / 2; while (cut > 0 && ((u8)d[cut] & 0xC0) == 0x80) cut--; if (cut <= 0) cut = dn; }
        if (cut >= dn) { /* (un seul caractere) */ }
        else if (L->x + spw + ws_ > L->r && L->npc > 0) { wb_line_end(L, 0); spw = 0; }
        if (cut >= dn) goto whole;
        char a[600];
        memcpy(a, d, cut); a[cut] = 0;
        wb_word(L, a, cut, ts, space, 1);
        wb_line_end(L, 0);
        wb_word(L, d + cut, dn - cut, ts, 0, 0);
        return;
    }
whole:
    if (L->x + spw + w > L->r + 0.5f && L->npc > 0 && !nobreak) { wb_line_end(L, 0); spw = 0; }
    if (L->blen + dn + 1 > (int)WLB_MAX) wb_line_end(L, 0);
    wpc_t *p = wb_piece(L, 1, w, space, 1);
    p->ts = (s16)ts;
    p->t0 = (u16)L->blen; p->tn = (u16)dn;
    memcpy(L->buf + L->blen, d, dn);
    L->blen += dn;
    if (p->space) L->x += (f32)font_glyphs[f][0].adv;
    p->x = L->x;
    L->x += w;
}
static void wb_inline(int anon, f32 cx, f32 cw, f32 *y) {
    const wcs_t *c = WCS(anon);
    wline_t L;
    memset(&L, 0, sizeof(L));
    L.cx0 = cx; L.cx1 = cx + cw;
    L.y = *y;
    L.first = 1;
    L.align = c->align == 4 ? 1 : c->align;
    L.indent = c->tindent.k ? 0 : wl_res(c->tindent, cw);
    if (L.indent < -cw / 2) L.indent = 0;
    L.lh_min = 0;
    L.pc = (wpc_t *)malloc(sizeof(wpc_t) * WPC_MAX);
    L.buf = (char *)malloc(WLB_MAX);
    if (!L.pc || !L.buf) { free(L.pc); free(L.buf); wb_fail = 1; return; }
    wb_line_begin(&L);
    char *d = (char *)malloc(1300);
    if (!d) { free(L.pc); free(L.buf); wb_fail = 1; return; }
    int started = 0;
    for (int i = wbx[anon].i0; i >= 0 && !wb_fail; i = wii[i].next) {
        const wii_t *t = &wii[i];
        const wts_t *s = &wtss[t->ts];
        if (t->type == II_TEXT) {
            const char *tx = wd.txt + t->a;
            int n = t->n, j = 0;
            int pre = s->ws == WSP_PRE || s->ws == WSP_PREWRAP;
            int nl_break = pre || s->ws == WSP_PRELINE;
            int nowrap = s->ws == WSP_NOWRAP || s->ws == WSP_PRE;
            int cap_next = 1;
            while (j < n) {
                char ch = tx[j];
                if (ch == '\n' && nl_break) {
                    if (!started) { wb_flush(y); L.y = *y; wb_line_begin(&L); started = 1; }
                    L.empty_br = 1;
                    if (L.npc == 0) { wpc_t *p = wb_piece(&L, 3, 0, 0, 1); p->ts = t->ts; }
                    wb_line_end(&L, 1);
                    j++;
                    continue;
                }
                if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '\f') {
                    if (pre) {                       /* espaces gardes */
                        int k = j; int cnt = 0;
                        while (k < n && (tx[k] == ' ' || tx[k] == '\t')) { cnt += tx[k] == '\t' ? 4 : 1; k++; }
                        if (k == j) { j++; continue; }
                        if (!started) { wb_flush(y); L.y = *y; wb_line_begin(&L); started = 1; }
                        f32 w = (f32)font_glyphs[s->font][0].adv * cnt;
                        wpc_t *p = wb_piece(&L, 3, w, 0, 1);
                        p->ts = t->ts; p->x = L.x; L.x += w;
                        j = k;
                        continue;
                    }
                    L.pend_space = 1;
                    cap_next = 1;
                    j++;
                    continue;
                }
                int s0 = j;
                while (j < n && tx[j] != ' ' && tx[j] != '\n' && tx[j] != '\t' && tx[j] != '\r' && tx[j] != '\f') j++;
                if (!started) { wb_flush(y); L.y = *y; wb_line_begin(&L); started = 1; }
                int dn = wb_disp(tx + s0, j - s0, d, 1300, s->transform, &cap_next);
                if (dn <= 0) continue;
                int space = L.pend_space && L.npc > 0;
                if (nowrap && space) {           /* (pas de retour) */
                    wb_word(&L, d, dn, t->ts, 1, 1);
                } else wb_word(&L, d, dn, t->ts, space, 0);
                L.pend_space = 0;
            }
        } else if (t->type == II_OPEN || t->type == II_CLOSE) {
            if (!started) continue;
            if (t->type == II_OPEN && L.pend_space && L.npc > 0) { L.x += (f32)font_glyphs[s->font][0].adv; L.pend_space = 0; }
            wpc_t *p = wb_piece(&L, 3, t->sp, 0, 1);
            p->ts = t->ts; p->x = L.x; L.x += t->sp;
        } else if (t->type == II_ATOM) {
            if (!started) { wb_flush(y); L.y = *y; wb_line_begin(&L); started = 1; }
            f32 w = 0, h = 0;
            int it0 = 0, it1 = 0, bx0 = 0, bx1 = 0;
            const wcs_t *ac = WCS(t->a);
            f32 avail = L.cx1 - L.cx0;
            wb_atom_layout(t->a, avail, &w, &h, &it0, &it1, &bx0, &bx1);
            f32 mw = w + wb_mar(ac, 1, cw) + wb_mar(ac, 3, cw);
            int space = L.pend_space && L.npc > 0;
            if (space) { if (L.x + 4 + mw > L.r + 0.5f) wb_line_end(&L, 0); else L.x += (f32)font_glyphs[s->font][0].adv; }
            wpc_t *p = wb_piece(&L, 2, mw, 0, 0);
            p->ts = t->ts; p->box = t->a; p->h = h;
            p->it0 = it0; p->it1 = it1; p->bx0 = bx0; p->bx1 = bx1;
            p->x = L.x;
            L.x += mw;
            L.pend_space = 0;
        } else if (t->type == II_BR) {
            if (!started) { wb_flush(y); L.y = *y; wb_line_begin(&L); started = 1; }
            L.empty_br = 1;
            if (L.npc == 0) { wpc_t *p = wb_piece(&L, 3, 0, 0, 1); p->ts = t->ts; }
            wb_line_end(&L, 1);
        }
    }
    if (started) { wb_line_end(&L, 0); *y = L.y; }
    free(L.pc); free(L.buf); free(d);
}

/* ---------- blocs ---------- */
static void wb_flow(int b, f32 cx, f32 cw, f32 *y);
static void wb_table(int b, f32 cx, f32 cw, f32 *y, f32 *outw);
static void wb_flex(int b, f32 cx, f32 cw, f32 *y);
static void wb_grid(int b, f32 cx, f32 cw, f32 *y);
static f32 wb_item_at(int b, f32 x, f32 bw, f32 y, f32 cb);

/* contenu d'une boite (selon son genre) dans la zone [cx, cx + cw) a partir de *y */
static void wb_content(int b, f32 cx, f32 cw, f32 *y) {
    switch (wbx[b].kind) {
    case WB_FLEX: wb_flex(b, cx, cw, y); break;
    case WB_GRID: wb_grid(b, cx, cw, y); break;
    case WB_TABLE: { f32 w; wb_table(b, cx, cw, y, &w); break; }
    default: wb_flow(b, cx, cw, y); break;
    }
}

/* nouveau contexte de mise en page (case, inline-block, flottant, flex...) : flottants et marges a part */
typedef struct { int base, n; f32 mpp, mpn; int nwait; } wbfc_t;
static void wb_bfc_begin(wbfc_t *s) { s->base = wflt_base; s->n = nwflts; s->mpp = mp_pos; s->mpn = mp_neg; s->nwait = nwb_wait; wflt_base = nwflts; mp_pos = mp_neg = 0; nwb_wait = 0; }
static void wb_bfc_end(wbfc_t *s, f32 *y) {
    wb_flush(y);
    f32 fb = wb_fbottom(3, *y);                 /* (le contexte contient ses flottants) */
    if (fb > *y) *y = fb;
    nwflts = s->n; wflt_base = s->base; mp_pos = s->mpp; mp_neg = s->mpn; nwb_wait = s->nwait;
}

/* boite entiere (bordure, padding, fond) a la position x (bord gauche de la bordure), largeur du contenu w ;
   bfc : contexte a part (pas de marges partagees) ; -> hauteur de la bordure */
static f32 wb_box_at(int b, f32 x, f32 w, f32 *y, int bfc, f32 cb) {
    const wcs_t *c = WCS(b);
    f32 bt = wb_bw(c, 0), bb = wb_bw(c, 2), bl = wb_bw(c, 3), br = wb_bw(c, 1);
    f32 pt = wb_pad(c, 0, cb), pb = wb_pad(c, 2, cb), pl = wb_pad(c, 3, cb), pr = wb_pad(c, 1, cb);
    u32 bc = 0; int sides = 0;
    for (int i = 0; i < 4; i++) if (c->bst[i] && c->bw[i] >= 0.5f) { u32 cc = c->bc[i] == 1 || c->bc[i] == 0 ? (c->bc[i] == 1 ? c->color : 0) : c->bc[i]; if (cc) { if (!bc) bc = cc; sides |= i == 0 ? 1 : i == 1 ? 2 : i == 2 ? 4 : 8; } }
    int bx = -1;
    u32 save_bg = wb_effbg;
    if (c->bg || bc) {
        bx = wb_out_box(x, *y, x + w + pl + pr + bl + br, *y, c->bg, bc, bc ? sides : 0);
        if (c->bg) wb_effbg = c->bg;
    }
    wbfc_t s;
    if (bfc) wb_bfc_begin(&s);
    int has_top = bt > 0 || pt > 0 || bfc;
    if (has_top) { wb_flush(y); if (bx >= 0) wbp->boxes[bx].y0 = (s32)(*y + 0.5f); }
    else if (bx >= 0 && nwb_wait < WT_WAIT) wb_wait[nwb_wait++] = bx;
    f32 top = *y;
    *y += bt + pt;
    f32 cy0 = *y;
    if (wbx[b].list && wbx[b].marker >= 0) wb_marker_box = b;
    wb_content(b, x + bl + pl, w, y);
    if (wb_marker_box == b) {                   /* (pas de ligne : la puce seule, en haut) */
        wb_flush(y);
        f32 base = (*y > cy0 ? cy0 : *y) + font_ascent[F_MED];
        wb_emit_marker(x + bl + pl, base, wbx[b].ts);
        if (*y < cy0 + font_height[F_MED] + 3) *y = cy0 + font_height[F_MED] + 3;
    }
    /* hauteur donnee */
    f32 ch = *y - cy0;
    f32 sh = -1;
    if (c->h.k == 0 && c->h.pct == 0) { sh = c->h.px; if (c->boxsz) sh -= pt + pb + bt + bb; }
    if (c->minh.k == 0 && c->minh.pct == 0) { f32 m = c->minh.px; if (c->boxsz) m -= pt + pb + bt + bb; if (m > sh) sh = m; }
    if (sh > ch && sh < 3000) { wb_flush(y); *y = cy0 + sh; }
    if (c->aspect > 0 && ch < 1 && w > 0) { wb_flush(y); *y = cy0 + w / c->aspect; }
    {   /* enfants en absolu (images) : en haut a gauche de la zone de padding, sur toute sa largeur */
        int any = 0;
        for (int ch = wbx[b].first; ch >= 0; ch = wbx[ch].next) if (wbx[ch].absb) { any = 1; break; }
        if (any) {
            f32 py0 = cy0 - pt;
            f32 sm = mp_pos, sn = mp_neg; int sw_ = nwb_wait;
            mp_pos = mp_neg = 0; nwb_wait = 0;
            f32 hmax = 0;
            for (int ch = wbx[b].first; ch >= 0; ch = wbx[ch].next) {
                if (!wbx[ch].absb) continue;
                f32 aw = w + pl + pr;
                const wcs_t *ac = WCS(ch);
                f32 sw2;
                if (wb_spec_w(ac, aw, &sw2) && sw2 + wb_hextra(ac, aw) < aw) aw = sw2 + wb_hextra(ac, aw);
                f32 hh = wb_item_at(ch, x + bl, aw, py0, aw);
                if (hh > hmax) hmax = hh;
            }
            mp_pos = sm; mp_neg = sn; nwb_wait = sw_;
            f32 inner = *y + pb - py0;           /* (hauteur de la zone de padding) */
            if (hmax > inner + 0.5f) *y += hmax - inner;   /* (l'image depasse : la place est faite) */
        }
    }
    int has_bot = bb > 0 || pb > 0 || bfc || sh > 0;
    if (has_bot) wb_flush(y);
    *y += pb + bb;
    if (bfc) wb_bfc_end(&s, y);
    if (bx >= 0) {
        wbox_t *o = &wbp->boxes[bx];
        if (!has_top && nwb_wait > 0 && wb_wait[nwb_wait - 1] == bx) {   /* (vide : rien n'a fixe son haut) */
            nwb_wait--;
            o->y0 = (s32)(top + 0.5f);
        }
        o->y1 = (s32)(*y + 0.5f);
        if (o->y1 <= o->y0) o->y1 = o->y0;
    }
    wb_effbg = save_bg;
    return *y - top;
}

/* largeur de la bordure d'une boite qui se retrecit a son contenu (flottant, inline-block, case de flex...) */
static f32 wb_fit_w(int b, f32 avail) {
    const wcs_t *c = WCS(b);
    f32 sw;
    f32 ex = wb_hextra(c, avail);
    if (wb_spec_w(c, avail, &sw)) { sw = wb_clamp_w(c, sw, avail); if (sw + ex > avail) sw = avail - ex; return (sw < 0 ? 0 : sw) + ex; }
    f32 mn, mx;
    wb_intrinsic(b, &mn, &mx);
    f32 mar = wb_mar(c, 1, avail) + wb_mar(c, 3, avail);
    mn -= mar; mx -= mar;
    f32 w = mx < avail - mar ? mx : avail - mar;
    if (w < mn) w = mn;
    if (w > avail - mar) w = avail - mar;
    f32 cw = wb_clamp_w(c, w - ex, avail);
    return (cw < 0 ? 0 : cw) + ex;
}

/* boite dans une ligne : mise en page a l'origine, deplacee ensuite avec sa ligne */
static f32 wb_atom_layout(int b, f32 avail, f32 *w, f32 *h, int *it0, int *it1, int *bx0, int *bx1) {
    wbx_t *x = &wbx[b];
    const wcs_t *c = WCS(b);
    *it0 = *it1 = wbp->nitems; *bx0 = *bx1 = wbp->nboxes;
    if (x->kind == WB_IMG) { wb_img_size(b, avail, w, h); return *h; }
    if (x->kind == WB_FIELD || x->kind == WB_SPACE) { *w = x->aw > avail ? avail : x->aw; *h = x->ah; return *h; }
    f32 bw = wb_fit_w(b, avail);
    f32 ex = wb_hextra(c, avail);
    f32 y = 0;
    f32 sm = mp_pos, sn = mp_neg;
    mp_pos = mp_neg = 0;
    if (x->kind == WB_TABLE) {
        f32 tw = 0;
        wbfc_t s; wb_bfc_begin(&s);
        wb_table(b, 0, avail, &y, &tw);
        wb_bfc_end(&s, &y);
        *w = tw;
    } else {
        wb_box_at(b, 0, bw - ex, &y, 1, avail);
        *w = bw;
    }
    mp_pos = sm; mp_neg = sn;
    *h = y;
    *it1 = wbp->nitems; *bx1 = wbp->nboxes;
    return y;
}

/* flottant : place a gauche / a droite a partir de *y ; le texte le contourne */
static void wb_float(int b, f32 cx, f32 cw, f32 y) {
    const wcs_t *c = WCS(b);
    f32 bw, h = 0;
    int it0 = wbp->nitems, bx0 = wbp->nboxes;
    f32 ml = wb_mar(c, 3, cw), mr = wb_mar(c, 1, cw), mt = wb_mar(c, 0, cw), mb = wb_mar(c, 2, cw);
    if (wbx[b].kind == WB_IMG) { f32 ih; wb_img_size(b, cw - ml - mr, &bw, &ih); h = ih; }
    else bw = wb_fit_w(b, cw);
    f32 mw = bw + ml + mr;
    if (mw > cw) mw = cw;
    /* place : la ou ca tient */
    f32 l, r;
    int guard = 0;
    for (;;) {
        wb_fbounds(y, y + 10, cx, cx + cw, &l, &r);
        if (r - l >= mw - 0.5f || guard++ > 40) break;
        f32 ny = wb_fnext(y);
        if (ny <= y + 0.5f) break;
        y = ny;
    }
    f32 fx = wbx[b].flt == 2 ? r - mw : l;
    if (wbx[b].kind == WB_IMG) {
        int it = wb_out_item(fx + ml, y + mt + h, bw, F_SMALL, WS_IMG | (wbx[b].link >= 0 ? WS_LINK : 0), 0, 0, 0, wbx[b].link,
                             wbx[b].marker >= 0 ? wd.txt + wbx[b].marker : "", wbx[b].marker >= 0 ? (int)strlen(wd.txt + wbx[b].marker) : 0);
        if (it >= 0) { wbp->items[it].img = wbx[b].img; wbp->items[it].h = (u16)h; }
    } else {
        f32 yy = y + mt;
        f32 sm = mp_pos, sn = mp_neg; int sw_ = nwb_wait;
        mp_pos = mp_neg = 0; nwb_wait = 0;
        if (wbx[b].kind == WB_TABLE) { f32 tw; wbfc_t s; wb_bfc_begin(&s); wb_table(b, fx + ml, bw, &yy, &tw); wb_bfc_end(&s, &yy); }
        else wb_box_at(b, fx + ml, bw - wb_hextra(c, cw), &yy, 1, cw);
        mp_pos = sm; mp_neg = sn; nwb_wait = sw_;
        h = yy - (y + mt);
    }
    (void)it0; (void)bx0;
    if (nwflts < WF_MAX) { wflt_t *f = &wflts[nwflts++]; f->x0 = fx; f->x1 = fx + mw; f->y0 = y; f->y1 = y + mt + h + mb; f->side = wbx[b].flt == 2 ? 2 : 1; }
}

/* boite de bloc dans son bloc conteneur */
static void wb_layout_box(int b, f32 cx, f32 cw, f32 *y) {
    wbx_t *x = &wbx[b];
    if (x->kind == WB_ANON) { wb_inline(b, cx, cw, y); return; }
    if (x->flt) { wb_float(b, cx, cw, *y + mp_pos + mp_neg); return; }
    const wcs_t *c = WCS(b);
    if (c->clear) {
        f32 fb = wb_fbottom(c->clear, *y + mp_pos + mp_neg);
        if (fb > *y + mp_pos + mp_neg) { wb_flush(y); *y = fb; }
    }
    if (x->kind == WB_IMG || x->kind == WB_FIELD || x->kind == WB_SPACE) {   /* (image seule en bloc) */
        f32 w, h; int a, bb_, cc, d;
        wb_flush(y);
        wb_atom_layout(b, cw, &w, &h, &a, &bb_, &cc, &d);
        f32 ml = wb_mar(c, 3, cw);
        if (c->m[3].k == 1 && c->m[1].k == 1) ml = (cw - w) / 2;
        if (x->kind == WB_IMG) {
            int it = wb_out_item(cx + ml, *y + h, w, F_SMALL, WS_IMG | (x->link >= 0 ? WS_LINK : 0), 0, 0, 0, x->link, x->marker >= 0 ? wd.txt + x->marker : "", x->marker >= 0 ? (int)strlen(wd.txt + x->marker) : 0);
            if (it >= 0) { wbp->items[it].img = x->img; wbp->items[it].h = (u16)h; }
        } else if (x->kind == WB_FIELD) {
            const char *lab = x->marker >= 0 ? wd.txt + x->marker : "";
            wb_out_item(cx + ml, *y + 3 + font_ascent[F_MED], x->aw, F_MED, x->img, 0, 0, 0, x->link, lab, (int)strlen(lab));
        }
        *y += h + 3;
        return;
    }
    f32 mt = c->m[0].k ? 0 : wl_res(c->m[0], cw), mb = c->m[2].k ? 0 : wl_res(c->m[2], cw);
    if (mt > 400) mt = 400;
    if (mb > 400) mb = 400;
    f32 ml = c->m[3].k ? 0 : wl_res(c->m[3], cw), mr = c->m[1].k ? 0 : wl_res(c->m[1], cw);
    if (ml < -cw / 2) ml = 0;
    if (mr < -cw / 2) mr = 0;
    if (ml + mr > cw * 0.6f && cw < 500) { f32 k = cw * 0.3f / (ml + mr); ml *= k; mr *= k; }   /* (petites zones : marges reduites) */
    f32 ex = wb_hextra(c, cw);
    int bfc = c->ovf || x->kind == WB_FLEX || x->kind == WB_GRID || x->kind == WB_CELL || c->display == DSP_IBLOCK;
    mp_add(mt);
    f32 w;
    int specified = 0;
    f32 avail = cw - ml - mr;
    if (x->kind == WB_TABLE) {
        wb_flush(y);
        f32 tw = 0;
        f32 y0 = *y;
        int it0 = wbp->nitems, bx0 = wbp->nboxes;
        wbfc_t s; wb_bfc_begin(&s);
        f32 ty = *y;
        wb_table(b, cx + ml, avail, &ty, &tw);
        wb_bfc_end(&s, &ty);
        *y = ty;
        if (c->m[3].k == 1 && c->m[1].k == 1 && tw < avail) wb_shift(it0, wbp->nitems, bx0, wbp->nboxes, (avail - tw) / 2, 0);
        else if (c->m[3].k == 1 && tw < avail) wb_shift(it0, wbp->nitems, bx0, wbp->nboxes, avail - tw, 0);
        (void)y0;
        mp_add(mb);
        return;
    }
    if (wb_spec_w(c, cw, &w)) specified = 1;
    else w = avail - ex;
    f32 w0 = w;
    w = wb_clamp_w(c, w, cw);
    if (w != w0) specified = 1;
    if (w + ex > cw) w = cw - ex;               /* (jamais plus large que l'ecran : pas de defilement de cote) */
    if (w < 0) w = 0;
    f32 bx = cx + ml;
    if (specified) {
        f32 room = cw - (w + ex);
        if (c->m[3].k == 1 && c->m[1].k == 1) bx = cx + (room > 0 ? room / 2 : 0);
        else if (c->m[3].k == 1) bx = cx + (room - mr > 0 ? room - mr : 0);
        if (bx + w + ex > cx + cw) bx = cx + cw - w - ex;
        if (bx < cx) bx = cx;
    }
    if (bfc && nwflts > wflt_base) {            /* contexte a part a cote d'un flottant : il se met a cote */
        f32 l, r;
        wb_flush(y);
        wb_fbounds(*y, *y + 20, cx, cx + cw, &l, &r);
        if (l > cx || r < cx + cw) {
            f32 room = r - l - ml - mr;
            if (!specified) { if (room > 80 || room > (w + ex) * 0.5f) { w = room - ex; bx = l + ml; } else { *y = wb_fnext(*y); } }
            else if (w + ex <= r - l) { if (bx < l) bx = l; }
            else *y = wb_fbottom(3, *y);
        }
    }
    wb_box_at(b, bx, w, y, bfc, cw);
    mp_add(mb);
}

/* enfants d'un bloc, les uns sous les autres */
static void wb_flow(int b, f32 cx, f32 cw, f32 *y) {
    if (cw < 1) cw = 1;
    for (int ch = wbx[b].first; ch >= 0 && !wb_fail; ch = wbx[ch].next) {
        if ((wbx[ch].atom || wbx[ch].absb) && wbx[ch].kind != WB_ANON) continue;   /* (dans une ligne / en absolu) */
        wb_layout_box(ch, cx, cw, y);
        if (lay_in_bg ? rl_cancel : 0) return;
    }
}

/* ---------- tableaux ---------- */
#define WT_COLS 64
#define WT_ROWS 2000
typedef struct { s32 box; s16 row, col, cs, rs; f32 h, py; s32 it0, it1, bx0, bx1, bg; } wcell_t;
static void wb_table(int b, f32 cx, f32 cw, f32 *y, f32 *outw) {
    const wcs_t *tc = WCS(b);
    f32 sp = tc->collapse ? 0 : tc->bspacing;
    if (sp > 20) sp = 20;
    /* rangees (tableaux dans des tableaux : tout est a chaque appel) */
    int maxr = 0, maxc = 0;
    for (int g = wbx[b].first; g >= 0; g = wbx[g].next) {
        if (wbx[g].kind == WB_ROWGRP) { for (int r = wbx[g].first; r >= 0; r = wbx[r].next) { maxr++; for (int q = wbx[r].first; q >= 0; q = wbx[q].next) maxc++; maxc++; } }
        else { maxr++; for (int q = wbx[g].first; q >= 0; q = wbx[q].next) maxc++; maxc++; }
    }
    if (maxr > WT_ROWS) maxr = WT_ROWS;
    if (maxc > 6000) maxc = 6000;
    s32 *rows = (s32 *)malloc(sizeof(s32) * (maxr + 1) * 2);
    s32 *rowbg = rows ? rows + maxr + 1 : NULL;
    wcell_t *cells = (wcell_t *)malloc(sizeof(wcell_t) * (maxc + 1));
    f32 *rowy = (f32 *)malloc(sizeof(f32) * (maxr + 1) * 2);
    f32 *rowh = rowy ? rowy + maxr + 1 : NULL;
    f32 *cols = (f32 *)malloc(sizeof(f32) * (WT_COLS + 1) * 6);
    if (!rows || !cells || !rowy || !cols) { free(rows); free(cells); free(rowy); free(cols); wb_fail = 1; *outw = 0; return; }
    f32 *cmin = cols, *cmax = cols + WT_COLS + 1, *cfix = cols + 2 * (WT_COLS + 1), *cpct = cols + 3 * (WT_COLS + 1), *cwid = cols + 4 * (WT_COLS + 1), *colx = cols + 5 * (WT_COLS + 1);
    int nrows = 0;
    s32 caps[16]; int ncaps = 0;
    for (int g = wbx[b].first; g >= 0; g = wbx[g].next) {
        int k = wbx[g].kind;
        if (k == WB_ROWGRP) { for (int r = wbx[g].first; r >= 0; r = wbx[r].next) if (nrows < maxr) rows[nrows++] = r; }
        else if (k == WB_ROW || k == WB_CELL || k == WB_ANON) { if (nrows < maxr) rows[nrows++] = g; }
        else if (ncaps < 16) caps[ncaps++] = g;
    }
    /* cases : grille avec colspan / rowspan */
    int ncells = 0, ncols = 0;
    s16 occ[WT_COLS];                           /* lignes encore prises par un rowspan */
    memset(occ, 0, sizeof(occ));
    for (int r = 0; r < nrows; r++) {
        int col = 0;
        int rb = rows[r];
        int single = wbx[rb].kind != WB_ROW;     /* (case ou texte hors rangee : une case seule) */
        for (int cl = single ? rb : wbx[rb].first; cl >= 0; cl = single ? -1 : wbx[cl].next) {
            if (wbx[cl].kind == WB_ANON) {          /* (texte entre les cases : seulement s'il y a des mots) */
                int any = 0;
                for (int i = wbx[cl].i0; i >= 0 && !any; i = wii[i].next) {
                    if (wii[i].type == II_ATOM) any = 1;
                    else if (wii[i].type == II_TEXT) for (int j = 0; j < wii[i].n && !any; j++) { char ch = wd.txt[wii[i].a + j]; any = ch != ' ' && ch != '\n' && ch != '\t' && ch != '\r'; }
                }
                if (!any) continue;
            }
            if (wbx[cl].atom && wbx[cl].kind != WB_ANON) continue;
            while (col < WT_COLS && occ[col] > 0) col++;
            if (col >= WT_COLS || ncells >= maxc) break;
            int cs = wbx[cl].colspan, rs = wbx[cl].rowspan;
            if (col + cs > WT_COLS) cs = WT_COLS - col;
            if (rs > nrows - r) rs = nrows - r;
            if (rs < 1) rs = 1;
            wcell_t *c = &cells[ncells++];
            memset(c, 0, sizeof(*c));
            c->box = cl; c->row = (s16)r; c->col = (s16)col; c->cs = (s16)cs; c->rs = (s16)rs; c->bg = -1;
            for (int q = col; q < col + cs; q++) occ[q] = (s16)rs;
            col += cs;
            if (col > ncols) ncols = col;
        }
        for (int q = 0; q < WT_COLS; q++) if (occ[q] > 0) occ[q]--;
    }
    f32 ex_t = wb_hextra(tc, cw);
    /* largeur des colonnes */
    for (int i = 0; i < ncols; i++) { cmin[i] = cmax[i] = 0; cfix[i] = -1; cpct[i] = 0; }
    f32 tw_spec = -1;
    { f32 sw; if (wb_spec_w(tc, cw, &sw)) tw_spec = sw; }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < ncells; i++) {
            wcell_t *c = &cells[i];
            if ((pass == 0) != (c->cs == 1)) continue;
            f32 mn, mx;
            wb_intrinsic(c->box, &mn, &mx);
            const wcs_t *cc = WCS(c->box);
            if (cc->ws == WSP_NOWRAP) mn = mx;
            if (c->cs == 1) {
                if (mn > cmin[c->col]) cmin[c->col] = mn;
                if (mx > cmax[c->col]) cmax[c->col] = mx;
                if (cc->w.k == 0) {
                    if (cc->w.pct > 0) { if (cc->w.pct > cpct[c->col]) cpct[c->col] = cc->w.pct; }
                    else { f32 fw = cc->w.px + (cc->boxsz ? 0 : wb_hextra(cc, cw)); if (fw > cfix[c->col]) cfix[c->col] = fw; }
                }
            } else {                            /* sur plusieurs colonnes : le surplus se partage */
                f32 smn = 0, smx = 0;
                for (int q = c->col; q < c->col + c->cs; q++) { smn += cmin[q]; smx += cmax[q]; }
                smn += sp * (c->cs - 1); smx += sp * (c->cs - 1);
                if (mn > smn) { f32 d = (mn - smn) / c->cs; for (int q = c->col; q < c->col + c->cs; q++) cmin[q] += d; }
                if (mx > smx) { f32 d = (mx - smx) / c->cs; for (int q = c->col; q < c->col + c->cs; q++) cmax[q] += d; }
            }
        }
    }
    for (int i = 0; i < ncols; i++) {
        if (cfix[i] >= 0) { if (cfix[i] > cmin[i]) cmin[i] = cfix[i] > cw ? cmin[i] : cfix[i]; cmax[i] = cmin[i] > cfix[i] ? cmin[i] : cfix[i]; }
        if (cmax[i] < cmin[i]) cmax[i] = cmin[i];
    }
    f32 smin = 0, smax = 0;
    for (int i = 0; i < ncols; i++) { smin += cmin[i]; smax += cmax[i]; }
    f32 gaps = sp * (ncols + 1);
    f32 avail = cw - ex_t - gaps;
    if (avail < ncols * 8) avail = ncols * 8;
    f32 target;
    if (tw_spec >= 0) { target = tw_spec - (tc->boxsz ? ex_t : 0) - gaps; if (target > avail) target = avail; if (target < smin && smin <= avail) target = smin; }
    else target = smax < avail ? smax : avail;
    /* pourcentages : une part de la largeur */
    f32 pct_sum = 0;
    for (int i = 0; i < ncols; i++) pct_sum += cpct[i];
    if (pct_sum > 0 && tw_spec < 0 && pct_sum <= 100) { f32 need = 0; for (int i = 0; i < ncols; i++) if (cpct[i] > 0) { f32 v = cmax[i] * 100 / cpct[i]; if (v > need) need = v; } if (need > target) target = need < avail ? need : avail; }
    if (ncols > 0) {
        if (target >= smax) {                   /* de la place : chacune son maximum, le reste partage */
            f32 extra = target - smax;
            f32 base = 0;
            for (int i = 0; i < ncols; i++) { cwid[i] = cmax[i]; base += cfix[i] >= 0 && cpct[i] == 0 ? 0 : cmax[i]; }
            for (int i = 0; i < ncols; i++) {
                if (pct_sum > 0) { if (cpct[i] > 0) cwid[i] = cmax[i] + extra * cpct[i] / pct_sum; }
                else if (base > 0) { if (!(cfix[i] >= 0)) cwid[i] += extra * cmax[i] / base; }
                else cwid[i] += extra / ncols;
            }
            if (pct_sum > 0) { f32 s = 0; for (int i = 0; i < ncols; i++) s += cwid[i]; if (s < target) { f32 d = (target - s) / ncols; for (int i = 0; i < ncols; i++) cwid[i] += d; } }
        } else if (target >= smin) {            /* entre les deux : au prorata */
            f32 k = smax > smin ? (target - smin) / (smax - smin) : 0;
            for (int i = 0; i < ncols; i++) cwid[i] = cmin[i] + (cmax[i] - cmin[i]) * k;
        } else {                                /* trop etroit : tout se serre (les mots se coupent) */
            f32 k = smin > 0 ? target / smin : 1;
            for (int i = 0; i < ncols; i++) cwid[i] = cmin[i] * k;
        }
    }
    if (tc->tlayout && tw_spec >= 0 && ncols > 0) {   /* table-layout: fixed : premiere rangee, sinon a parts egales */
        f32 fixed = 0; int nfree = 0;
        for (int i = 0; i < ncols; i++) { if (cfix[i] >= 0) fixed += cfix[i]; else nfree++; }
        for (int i = 0; i < ncols; i++) cwid[i] = cfix[i] >= 0 ? cfix[i] : (nfree ? (target - fixed) / nfree : 0);
        for (int i = 0; i < ncols; i++) if (cwid[i] < 4) cwid[i] = 4;
    }
    f32 inner = 0;
    for (int i = 0; i < ncols; i++) inner += cwid[i];
    inner += gaps;
    f32 tw = inner + ex_t;
    /* legende(s) en haut */
    f32 yy = *y;
    for (int i = 0; i < ncaps; i++) wb_layout_box(caps[i], cx, tw > 60 ? tw : cw, &yy);
    wb_flush(&yy);
    /* fond et bordure du tableau */
    int tb = -1;
    u32 tbc = 0; int tsides = 0;
    for (int i = 0; i < 4; i++) if (tc->bst[i] && tc->bw[i] >= 0.5f) { u32 cc = tc->bc[i] == 1 ? tc->color : tc->bc[i]; if (cc) { if (!tbc) tbc = cc; tsides |= i == 0 ? 1 : i == 1 ? 2 : i == 2 ? 4 : 8; } }
    if (tc->bg || tbc) tb = wb_out_box(cx, yy, cx + tw, yy, tc->bg, tbc, tsides);
    u32 save_bg = wb_effbg;
    if (tc->bg) wb_effbg = tc->bg;
    f32 x0 = cx + wb_bw(tc, 3) + wb_pad(tc, 3, cw) + sp;
    f32 ytop = yy + wb_bw(tc, 0) + wb_pad(tc, 0, cw) + sp;
    for (int r = 0; r < nrows; r++) rowh[r] = 0;
    colx[0] = x0;
    for (int i = 0; i < ncols; i++) colx[i + 1] = colx[i] + cwid[i] + sp;
    /* rangees : fond de la rangee, puis ses cases */
    int ci = 0;
    f32 ry = ytop;
    for (int r = 0; r < nrows; r++) {
        rowy[r] = ry;
        rowbg[r] = -1;
        const wcs_t *rc = wbx[rows[r]].kind == WB_ROW ? WCS(rows[r]) : NULL;
        if (rc && rc->bg) rowbg[r] = wb_out_box(x0 - sp, ry, colx[ncols] , ry, rc->bg, 0, 0);
        u32 save2 = wb_effbg;
        if (rc && rc->bg) wb_effbg = rc->bg;
        f32 rmax = 0;
        if (rc && rc->h.k == 0 && rc->h.pct == 0 && rc->h.px > 0 && rc->h.px < 2000) rmax = rc->h.px;
        for (; ci < ncells && cells[ci].row == r; ci++) {
            wcell_t *c = &cells[ci];
            f32 cwd = colx[c->col + c->cs] - colx[c->col] - sp;
            const wcs_t *cc = WCS(c->box);
            c->it0 = wbp->nitems; c->bx0 = wbp->nboxes;
            c->py = ry;
            f32 cy = ry;
            wbfc_t s; wb_bfc_begin(&s);
            if (wbx[c->box].kind == WB_ANON) { wb_inline(c->box, colx[c->col], cwd, &cy); }
            else {
                f32 ex = wb_hextra(cc, cw);
                c->bg = wbp->nboxes;
                wb_box_at(c->box, colx[c->col], cwd - ex > 0 ? cwd - ex : 0, &cy, 1, cwd);
                if (c->bg >= wbp->nboxes || wbp->boxes[c->bg].x0 != (s16)(colx[c->col] + 0.5f)) c->bg = -1;
            }
            wb_bfc_end(&s, &cy);
            c->it1 = wbp->nitems; c->bx1 = wbp->nboxes;
            c->h = cy - ry;
            if (cc->h.k == 0 && cc->h.pct == 0 && cc->h.px > c->h && cc->h.px < 2000) c->h = cc->h.px;
            if (c->rs == 1 && c->h > rmax) rmax = c->h;
        }
        wb_effbg = save2;
        rowh[r] = rmax;
        ry += rmax + sp;
    }
    /* cases sur plusieurs lignes : la derniere ligne grandit si besoin */
    for (int i = 0; i < ncells; i++) {
        wcell_t *c = &cells[i];
        if (c->rs <= 1) continue;
        int last = c->row + c->rs - 1;
        f32 tot = 0;
        for (int r = c->row; r <= last; r++) tot += rowh[r] + (r < last ? sp : 0);
        if (c->h > tot) rowh[last] += c->h - tot;
    }
    ry = ytop;
    for (int r = 0; r < nrows; r++) { rowy[r] = ry; ry += rowh[r] + sp; }
    rowy[nrows] = ry;
    /* positions finales : rangee a sa vraie place, alignement vertical (milieu par defaut), fond a la hauteur */
    for (int i = 0; i < ncells; i++) {
        wcell_t *c = &cells[i];
        int last = c->row + c->rs - 1;
        f32 top = rowy[c->row], bot = rowy[last] + rowh[last];
        const wcs_t *cc = WCS(c->box);
        int va = cc->valign;
        const wcs_t *rc = wbx[rows[c->row]].kind == WB_ROW ? WCS(rows[c->row]) : NULL;
        if (va == VA_BASE && rc && rc->valign) va = rc->valign;
        f32 room = (bot - top) - c->h, dy = 0;
        if (room > 0.5f) { if (va == VA_MIDDLE || va == VA_BASE) dy = room / 2; else if (va == VA_BOTTOM) dy = room; }
        if (va == VA_TOP) dy = 0;
        wb_shift(c->it0, c->it1, c->bx0, c->bx1, 0, (top - c->py) + dy);
        if (c->bg >= 0 && c->bg < wbp->nboxes) { wbp->boxes[c->bg].y0 = (s32)(top + 0.5f); wbp->boxes[c->bg].y1 = (s32)(bot + 0.5f); }
    }
    for (int r = 0; r < nrows; r++) if (rowbg[r] >= 0) { wbp->boxes[rowbg[r]].y0 = (s32)(rowy[r] + 0.5f); wbp->boxes[rowbg[r]].y1 = (s32)(rowy[r] + rowh[r] + 0.5f); }
    f32 tbot = rowy[nrows] + wb_pad(tc, 2, cw) + wb_bw(tc, 2);
    if (nrows == 0) tbot = ytop + wb_pad(tc, 2, cw) + wb_bw(tc, 2);
    if (tb >= 0) wbp->boxes[tb].y1 = (s32)(tbot + 0.5f);
    wb_effbg = save_bg;
    *y = tbot;
    *outw = tw;
    free(rows); free(cells); free(rowy); free(cols);
}

/* ---------- flex et grille ---------- */
#define WFX_MAX 400
typedef struct { s32 box; f32 base, mn, w, h, ml, mr, mt, mb, py; s32 it0, it1, bx0, bx1; s16 order; u8 grow0; } wfi_t;
static int wfi_cmp(const void *a, const void *b) { const wfi_t *x = (const wfi_t *)a, *y = (const wfi_t *)b; return x->order != y->order ? x->order - y->order : x->box - y->box; }
/* une case (flex / grille) : mise en page a (x, y) avec la largeur de sa bordure w */
static f32 wb_item_at(int b, f32 x, f32 bw, f32 y, f32 cb) {
    f32 yy = y;
    wbx_t *bx = &wbx[b];
    if (bx->kind == WB_ANON) {
        wbfc_t s; wb_bfc_begin(&s);
        wb_inline(b, x, bw, &yy);
        wb_bfc_end(&s, &yy);
        return yy - y;
    }
    if (bx->kind == WB_IMG || bx->kind == WB_FIELD || bx->kind == WB_SPACE) {
        f32 w, h; int a, b2, c2, d2;
        wb_atom_layout(b, bw, &w, &h, &a, &b2, &c2, &d2);
        if (bx->kind == WB_IMG) {
            int it = wb_out_item(x, y + h, w, F_SMALL, WS_IMG | (bx->link >= 0 ? WS_LINK : 0), 0, 0, 0, bx->link, bx->marker >= 0 ? wd.txt + bx->marker : "", bx->marker >= 0 ? (int)strlen(wd.txt + bx->marker) : 0);
            if (it >= 0) { wbp->items[it].img = bx->img; wbp->items[it].h = (u16)h; }
        } else if (bx->kind == WB_FIELD) {
            const char *lab = bx->marker >= 0 ? wd.txt + bx->marker : "";
            wb_out_item(x, y + 3 + font_ascent[F_MED], bx->aw, F_MED, bx->img, 0, 0, 0, bx->link, lab, (int)strlen(lab));
        }
        return h;
    }
    const wcs_t *c = WCS(b);
    f32 sm = mp_pos, sn = mp_neg; int sw_ = nwb_wait;
    mp_pos = mp_neg = 0; nwb_wait = 0;
    if (bx->kind == WB_TABLE) { f32 tw; wbfc_t s; wb_bfc_begin(&s); wb_table(b, x, bw, &yy, &tw); wb_bfc_end(&s, &yy); }
    else { f32 ex = wb_hextra(c, cb); wb_box_at(b, x, bw - ex > 0 ? bw - ex : 0, &yy, 1, cb); }
    mp_pos = sm; mp_neg = sn; nwb_wait = sw_;
    return yy - y;
}
static int wb_items(int b, wfi_t *it, f32 cw, int flex) {
    int n = 0;
    for (int ch = wbx[b].first; ch >= 0 && n < WFX_MAX; ch = wbx[ch].next) {
        wbx_t *x = &wbx[ch];
        if (x->kind == WB_ANON) {               /* texte direct : une case (s'il y a autre chose que des espaces) */
            int any = 0;
            for (int i = x->i0; i >= 0 && !any; i = wii[i].next) {
                if (wii[i].type == II_ATOM || wii[i].type == II_BR) any = 1;
                else if (wii[i].type == II_TEXT) for (int j = 0; j < wii[i].n && !any; j++) { char c = wd.txt[wii[i].a + j]; any = c != ' ' && c != '\n' && c != '\t' && c != '\r'; }
            }
            if (!any) continue;
        } else if (x->atom) continue;           /* (deja dans une ligne de texte direct) */
        wfi_t *f = &it[n++];
        memset(f, 0, sizeof(*f));
        f->box = ch;
        const wcs_t *c = WCS(ch);
        f->order = x->kind == WB_ANON ? 0 : c->order;
        if (x->kind != WB_ANON) { f->ml = wb_mar(c, 3, cw); f->mr = wb_mar(c, 1, cw); f->mt = wb_mar(c, 0, cw); f->mb = wb_mar(c, 2, cw); }
        f32 mn, mx;
        wb_intrinsic(ch, &mn, &mx);
        mn -= f->ml + f->mr; mx -= f->ml + f->mr;
        if (x->kind == WB_IMG) { f32 w, h; wb_img_size(ch, cw, &w, &h); mn = mx = w; }
        f32 base = mx;
        if (flex && x->kind != WB_ANON) {
            f32 sw;
            if (c->fbasis.k == 0) { base = wl_res(c->fbasis, cw); if (!c->boxsz && c->fbasis.px + c->fbasis.pct > 0) base += wb_hextra(c, cw); }
            else if (wb_spec_w(c, cw, &sw)) base = sw + wb_hextra(c, cw);
            if (c->ovf) mn = 0;
            if (c->minw.k == 0) { f32 m = wl_res(c->minw, cw) + (c->boxsz ? 0 : wb_hextra(c, cw)); mn = m; }
            if (c->maxw.k == 0) { f32 m = wl_res(c->maxw, cw) + (c->boxsz ? 0 : wb_hextra(c, cw)); if (base > m) base = m; }
        }
        if (mn > cw) mn = cw;
        f->base = base; f->mn = mn;
    }
    if (n > 1) qsort(it, n, sizeof(wfi_t), wfi_cmp);
    return n;
}
static wfi_t *wb_items_alloc(int b) {
    int n = 1;
    for (int ch = wbx[b].first; ch >= 0; ch = wbx[ch].next) n++;
    if (n > WFX_MAX) n = WFX_MAX;
    wfi_t *it = (wfi_t *)malloc(sizeof(wfi_t) * n);
    if (!it) wb_fail = 1;
    return it;
}
static void wb_flex_in(int b, f32 cx, f32 cw, f32 *y, wfi_t *it);
static void wb_flex(int b, f32 cx, f32 cw, f32 *y) {
    wfi_t *it = wb_items_alloc(b);
    if (!it) return;
    wb_flex_in(b, cx, cw, y, it);
    free(it);
}
static void wb_flex_in(int b, f32 cx, f32 cw, f32 *y, wfi_t *it) {
    const wcs_t *c = WCS(b);
    wb_flush(y);
    int n = wb_items(b, it, cw, 1);
    f32 gc = c->gap_c, gr = c->gap_r;
    if (c->fdir) {                              /* colonne : les unes sous les autres */
        for (int i = 0; i < n; i++) {
            wfi_t *f = &it[i];
            const wcs_t *ic = WCS(f->box);
            int al = ic->aself != AI_AUTO ? ic->aself : c->aitems;
            f32 bw = cw - f->ml - f->mr, x = cx + f->ml;
            f32 sw;
            int spec = wbx[f->box].kind != WB_ANON && wb_spec_w(ic, cw, &sw);
            if (spec) { bw = wb_clamp_w(ic, sw, cw) + wb_hextra(ic, cw); if (bw > cw) bw = cw; }
            else if (al == AI_CENTER || al == AI_END || al == AI_START) { bw = f->base < cw - f->ml - f->mr ? f->base : cw - f->ml - f->mr; if (bw < f->mn) bw = f->mn; }
            else if (wbx[f->box].kind != WB_ANON) { f32 m = wb_clamp_w(ic, bw - wb_hextra(ic, cw), cw) + wb_hextra(ic, cw); if (m < bw) bw = m; }
            if (al == AI_CENTER) x = cx + (cw - bw) / 2; else if (al == AI_END) x = cx + cw - bw - f->mr;
            if (wbx[f->box].kind != WB_ANON && ic->m[3].k == 1 && ic->m[1].k == 1) x = cx + (cw - bw) / 2;
            *y += f->mt;
            *y += wb_item_at(f->box, x, bw, *y, cw);
            *y += f->mb + (i < n - 1 ? gr : 0);
        }
        return;
    }
    /* ligne(s) */
    int i0 = 0;
    while (i0 < n) {
        int i1 = i0;
        f32 sum = 0;
        if (c->fwrap) {
            while (i1 < n) {
                f32 add = it[i1].base + it[i1].ml + it[i1].mr + (i1 > i0 ? gc : 0);
                if (i1 > i0 && sum + add > cw + 0.5f) break;
                sum += add; i1++;
            }
        } else { for (i1 = i0; i1 < n; i1++) sum += it[i1].base + it[i1].ml + it[i1].mr + (i1 > i0 ? gc : 0); }
        int cnt = i1 - i0;
        /* tailles : grandir / retrecir */
        f32 free_ = cw - sum, gsum = 0, ssum = 0;
        for (int i = i0; i < i1; i++) { const wcs_t *ic = WCS(it[i].box); it[i].w = it[i].base; if (wbx[it[i].box].kind != WB_ANON) { gsum += ic->fgrow; ssum += ic->fshrink * it[i].base; } else ssum += it[i].base; }
        if (free_ > 0 && gsum > 0) {
            for (int i = i0; i < i1; i++) { const wcs_t *ic = WCS(it[i].box); if (wbx[it[i].box].kind != WB_ANON) it[i].w += free_ * ic->fgrow / gsum; }
            free_ = 0;
        } else if (free_ < 0) {
            for (int pass = 0; pass < 3 && free_ < -0.5f; pass++) {
                f32 ss = 0;
                for (int i = i0; i < i1; i++) { const wcs_t *ic = WCS(it[i].box); f32 sh = wbx[it[i].box].kind == WB_ANON ? 1 : ic->fshrink; if (it[i].w > it[i].mn + 0.5f) ss += sh * it[i].base; }
                if (ss <= 0) break;
                f32 over = -free_;
                free_ = 0;
                for (int i = i0; i < i1; i++) {
                    const wcs_t *ic = WCS(it[i].box);
                    f32 sh = wbx[it[i].box].kind == WB_ANON ? 1 : ic->fshrink;
                    if (it[i].w <= it[i].mn + 0.5f) continue;
                    f32 nw = it[i].w - over * sh * it[i].base / ss;
                    if (nw < it[i].mn) nw = it[i].mn;
                    it[i].w = nw;
                }
                f32 tot = 0;
                for (int i = i0; i < i1; i++) tot += it[i].w + it[i].ml + it[i].mr + (i > i0 ? gc : 0);
                free_ = cw - tot;
            }
            if (free_ < -0.5f) {                /* toujours trop large : on serre tout */
                f32 tot = 0;
                for (int i = i0; i < i1; i++) tot += it[i].w;
                f32 k = tot > 0 ? (tot + free_) / tot : 1;
                if (k < 0.1f) k = 0.1f;
                for (int i = i0; i < i1; i++) it[i].w *= k;
                free_ = 0;
            }
        }
        /* auto margins / justify */
        f32 off = 0, between = gc;
        int autom = 0;
        for (int i = i0; i < i1; i++) { const wcs_t *ic = WCS(it[i].box); if (wbx[it[i].box].kind != WB_ANON && (ic->m[3].k == 1 || ic->m[1].k == 1)) autom++; }
        if (free_ > 0 && !autom) {
            switch (c->justify) {
            case JC_CENTER: off = free_ / 2; break;
            case JC_END: off = free_; break;
            case JC_BETWEEN: if (cnt > 1) between += free_ / (cnt - 1); break;
            case JC_AROUND: off = free_ / cnt / 2; between += free_ / cnt; break;
            case JC_EVENLY: off = free_ / (cnt + 1); between += free_ / (cnt + 1); break;
            }
        }
        /* mise en page des cases */
        f32 x = cx + off, lh = 0;
        for (int i = i0; i < i1; i++) {
            wfi_t *f = &it[i];
            const wcs_t *ic = WCS(f->box);
            if (free_ > 0 && autom && wbx[f->box].kind != WB_ANON) {
                f32 share = free_ / autom;
                if (ic->m[3].k == 1 && ic->m[1].k == 1) { f->ml += share / 2; f->mr += share / 2; }
                else if (ic->m[3].k == 1) f->ml += share; else if (ic->m[1].k == 1) f->mr += share;
            }
            x += f->ml;
            f->it0 = wbp->nitems; f->bx0 = wbp->nboxes;
            f->py = *y + f->mt;
            f->h = wb_item_at(f->box, x, f->w, f->py, cw);
            f->it1 = wbp->nitems; f->bx1 = wbp->nboxes;
            f32 full = f->h + f->mt + f->mb;
            if (full > lh) lh = full;
            x += f->w + f->mr + between;
        }
        /* alignement vertical ; « stretch » : les fonds a la hauteur de la ligne */
        for (int i = i0; i < i1; i++) {
            wfi_t *f = &it[i];
            const wcs_t *ic = WCS(f->box);
            int al = ic->aself != AI_AUTO && wbx[f->box].kind != WB_ANON ? ic->aself : c->aitems;
            f32 room = lh - (f->h + f->mt + f->mb);
            if (room > 0.5f) {
                if (al == AI_CENTER) wb_shift(f->it0, f->it1, f->bx0, f->bx1, 0, room / 2);
                else if (al == AI_END) wb_shift(f->it0, f->it1, f->bx0, f->bx1, 0, room);
                else if (al == AI_STRETCH && f->bx1 > f->bx0) {
                    wbox_t *o = &wbp->boxes[f->bx0];
                    if (o->y0 <= (s32)(f->py + 1.5f) && o->y1 >= (s32)(f->py + f->h - 1.5f)) o->y1 = (s32)(f->py + lh - f->mt - f->mb + 0.5f);
                }
            }
        }
        *y += lh + (i1 < n ? gr : 0);
        i0 = i1;
        if (cnt <= 0) break;
    }
}
static void wb_grid_in(int b, f32 cx, f32 cw, f32 *y, wfi_t *it);
static void wb_grid(int b, f32 cx, f32 cw, f32 *y) {
    wfi_t *it = wb_items_alloc(b);
    if (!it) return;
    wb_grid_in(b, cx, cw, y, it);
    free(it);
}
static void wb_grid_in(int b, f32 cx, f32 cw, f32 *y, wfi_t *it) {
    const wcs_t *c = WCS(b);
    wb_flush(y);
    int n = wb_items(b, it, cw, 0);
    int nc = c->gcols;
    f32 gc = c->gap_c, gr = c->gap_r;
    if (nc <= 1) {                              /* une colonne : les unes sous les autres */
        for (int i = 0; i < n; i++) {
            wfi_t *f = &it[i];
            *y += f->mt;
            *y += wb_item_at(f->box, cx + f->ml, cw - f->ml - f->mr, *y, cw);
            *y += f->mb + (i < n - 1 ? gr : 0);
        }
        return;
    }
    /* largeurs : px d'abord, le reste en parts (fr) */
    f32 cwid[12], fixed = 0, frs = 0;
    for (int i = 0; i < nc; i++) { if (c->gfr[i] < 0) fixed += -c->gfr[i]; else frs += c->gfr[i]; }
    f32 rest = cw - fixed - gc * (nc - 1);
    if (rest < 0) rest = 0;
    for (int i = 0; i < nc; i++) cwid[i] = c->gfr[i] < 0 ? -c->gfr[i] : (frs > 0 ? rest * c->gfr[i] / frs : 0);
    f32 tot = gc * (nc - 1);
    for (int i = 0; i < nc; i++) tot += cwid[i];
    if (tot > cw && tot > 0) { f32 k = cw / tot; for (int i = 0; i < nc; i++) cwid[i] *= k; gc *= k; }
    /* trop etroit pour le contenu : moins de colonnes */
    int col = 0, rowstart = 0;
    f32 x = cx, lh = 0;
    for (int i = 0; i < n; i++) {
        wfi_t *f = &it[i];
        const wcs_t *ic = WCS(f->box);
        int span = wbx[f->box].kind != WB_ANON && ic->gspan ? ic->gspan : 1;
        if (span > nc) span = nc;
        if (col + span > nc) {                  /* ligne suivante */
            for (int j = rowstart; j < i; j++) {
                wfi_t *g = &it[j];
                if (g->bx1 > g->bx0) { wbox_t *o = &wbp->boxes[g->bx0]; if (o->y0 <= (s32)(g->py + 1.5f)) o->y1 = (s32)(g->py + lh - g->mt - g->mb + 0.5f); }
            }
            *y += lh + gr; col = 0; lh = 0; rowstart = i;
        }
        x = cx; for (int q = 0; q < col; q++) x += cwid[q] + gc;
        f32 w = 0; for (int q = col; q < col + span; q++) w += cwid[q] + (q > col ? gc : 0);
        f->py = *y + f->mt;
        f->bx0 = wbp->nboxes;
        f->h = wb_item_at(f->box, x + f->ml, w - f->ml - f->mr, f->py, cw);
        f->bx1 = wbp->nboxes;
        if (f->h + f->mt + f->mb > lh) lh = f->h + f->mt + f->mb;
        col += span;
    }
    for (int j = rowstart; j < n; j++) {
        wfi_t *g = &it[j];
        if (g->bx1 > g->bx0) { wbox_t *o = &wbp->boxes[g->bx0]; if (o->y0 <= (s32)(g->py + 1.5f)) o->y1 = (s32)(g->py + lh - g->mt - g->mb + 0.5f); }
    }
    if (n) *y += lh;
}

/* liens dans l'ordre de l'ecran (haut -> bas, gauche -> droite) : la croix passe de l'un au suivant comme on les voit */
typedef struct { s32 y, x; int k; } wlord_t;
static int wlord_cmp(const void *a, const void *b) {
    const wlord_t *u = (const wlord_t *)a, *v = (const wlord_t *)b;
    if (u->y != v->y) return u->y < v->y ? -1 : 1;
    if (u->x != v->x) return u->x < v->x ? -1 : 1;
    return u->k - v->k;
}
static void wb_links_visual(wpage_t *p) {
    int n = p->nlinks;
    if (n < 2) return;
    wlord_t *o = (wlord_t *)malloc(sizeof(wlord_t) * n);
    int *map = (int *)malloc(sizeof(int) * n);
    wlink_t *nl = (wlink_t *)malloc(sizeof(wlink_t) * n);
    if (!o || !map || !nl) { free(o); free(map); free(nl); return; }
    for (int i = 0; i < n; i++) { o[i].y = 0x7fffffff; o[i].x = 0x7fffffff; o[i].k = i; }
    for (int i = 0; i < p->nitems; i++) {
        const witem_t *it = &p->items[i];
        if (it->link < 0 || it->link >= n) continue;
        s32 top = (it->style & WS_IMG) ? it->y - it->h : it->y - font_ascent[it->font];
        wlord_t *q = &o[it->link];
        /* (meme ligne a quelques points pres : la plus a gauche) */
        if (top < q->y - 4 || (top <= q->y + 4 && it->x < q->x)) { if (top < q->y - 4 || top < q->y) q->y = top; q->x = it->x; }
    }
    for (int i = 0; i < n; i++) o[i].y = o[i].y == 0x7fffffff ? 0x7fffffff : o[i].y / 6;   /* (rangees de 6 points : meme ligne) */
    qsort(o, n, sizeof(wlord_t), wlord_cmp);
    for (int i = 0; i < n; i++) { map[o[i].k] = i; nl[i] = p->links[o[i].k]; }
    memcpy(p->links, nl, sizeof(wlink_t) * n);
    for (int i = 0; i < p->nitems; i++) if (p->items[i].link >= 0 && p->items[i].link < n) p->items[i].link = (s16)map[p->items[i].link];
    free(o); free(map); free(nl);
}

/* ---------- la page ---------- */
static u32 wb_prof[5];                         /* (simulateur : duree des etapes, us) */
static char web_text_url[URL_MAX];              /* v201 : page en « mode texte » (sans le style du site) */
static int web_layout_v2(wpage_t *p, const unsigned char *s, int len, int latin) {
    u32 t0 = perf_us();
    wb_free();
    wb_fail = 0;
    wbp = p;
    p->pagebg = WEB_PAGEBG;
    p->ncss = 0; p->css_wait = 0;
    if (!wd_parse(s, len, latin, p->url)) { wd_free(); return 0; }
    if (lay_in_bg && rl_cancel) { wd_free(); return 0; }
    u32 t1 = perf_us();
    snprintf(p->title, sizeof(p->title), "%s", wd.title);
    if (wd.refresh[0]) { snprintf(p->refresh, sizeof(p->refresh), "%s", wd.refresh); p->refresh_delay = wd.refresh_delay; }
    /* feuilles de style : navigateur, puis la page dans l'ordre */
    ws_reset();
    ws.origin = 0;
    ws_parse_sheet(ws_ua_css, (int)sizeof(ws_ua_css) - 1, 0);
    ws.origin = 1;
    int textmode = web_text_url[0] && !strcmp(web_text_url, p->url);
    ws_textmode = textmode;
    for (int i = 0; i < wd.nsheets && !textmode; i++) {
        if (!wd.sheets[i].is_link) { ws_parse_sheet(wd.txt + wd.sheets[i].off, (int)wd.sheets[i].len, 0); continue; }
        const char *u = wd.txt + wd.sheets[i].off;
        if (p->ncss < 6) p->css_url[p->ncss++] = pool_add(p, u);
        const cssc_t *cc = cssc_get(u);
        if (cc) ws_parse_sheet(cc->data, cc->len, 0);
        else p->css_wait++;
    }
    u32 t2 = perf_us();
    /* infos de chaque element (id, classes) */
    wels = (wel_t *)malloc(sizeof(wel_t) * (wd.nn > 0 ? wd.nn : 1));
    if (!wels) { ws_free(); wd_free(); return 0; }
    for (int k = 0; k < wd.nn; k++) if (wd.n[k].type == 1) ws_el_info(k); else memset(&wels[k], 0, sizeof(wel_t));
    ws_root_fs = 16; ws_cur_fs = 16; ws_cur_cb = WS_VW;
    nwvars = 0;
    /* boites */
    int root = wb_new(WB_BLOCK, -1, -1);
    wcs_t rc; ws_init(&rc, NULL); rc.display = DSP_BLOCK;
    if (root >= 0) wbx[root].cs = wb_style(&rc);
    wb_ts_from(&rc, -1);
    wbctx_t x;
    x.cont = root; x.anon = -1; x.ts = 0; x.link = -1; x.form = -1;
    wb_build(0, &x, NULL, 0);
    if (wb_fail || (lay_in_bg && rl_cancel)) { wb_free(); ws_free(); wd_free(); return 0; }
    u32 t3 = perf_us();
    /* positions */
    nwflts = 0; wflt_base = 0; mp_pos = mp_neg = 0; nwb_wait = 0;
    wb_effbg = p->pagebg;
    wb_marker_box = -1;
    f32 y = 0;
    wb_flow(root, 0, (f32)wv_lw, &y);
    wb_flush(&y);
    f32 fb = wb_fbottom(3, y);
    if (fb > y) y = fb;
    if (p->truncated) {
        int f = F_MED;
        const char *m = "(page trop longue : la suite n'est pas affich\xC3\xA9" "e)";
        wb_out_item(4, y + font_ascent[f] + 6, (f32)wb_tw(f, m), f, WS_DIM, 0, 0x72777dffu, 0, -1, m, (int)strlen(m));
        y += font_height[f] + 10;
    }
    {   /* page faite pour JavaScript (presque vide ici) : un mot et le mode texte ; en mode texte : le retour */
        long tl = 0;
        for (int i = 0; i < p->nitems && tl < 2000; i++) if (!(p->items[i].style & (WS_IMG | WS_FIELD))) tl += (long)strlen(p->text + p->items[i].off);
        int need_js = wd.scripts > 0 && tl < 600 && strncmp(p->url, "gc:", 3);
        if (id_pend.used && !lay_in_bg) {      /* v201 : enregistrer l'identifiant qu'on vient d'envoyer ? */
            char m1[220];
            snprintf(m1, sizeof(m1), "Enregistrer l'identifiant%s%s pour %s ?", id_pend.user[0] ? " " : "", id_pend.user, id_pend.host);
            int l1 = new_link(p, LK_HREF, "gc:id-oui", -1, -1), l2 = new_link(p, LK_HREF, "gc:id-non", -1, -1);
            f32 h = font_height[F_SMALL] + font_height[F_MED] + 22;
            wb_shift(0, p->nitems, 0, p->nboxes, 0, h);
            wb_out_box(0, 0, (f32)wv_lw, h - 6, 0xe3f0ffffu, 0x7aa6e0ff, 15);
            char m1d[240]; int cn = 0; wb_disp(m1, (int)strlen(m1), m1d, sizeof(m1d), 0, &cn);
            wb_out_item(8, 6 + font_ascent[F_SMALL], (f32)wb_tw(F_SMALL, m1d), F_SMALL, 0, 0, 0x1d3a66ffu, 0, -1, m1d, (int)strlen(m1d));
            const char *o1 = "Oui, l'enregistrer", *o2 = "Non";
            f32 w1 = (f32)wb_tw(F_MED, o1);
            wb_out_item(8, 10 + font_height[F_SMALL] + font_ascent[F_MED], w1, F_MED, WS_LINK, FX_ULINE, WEB_LINK, 0, l1, o1, (int)strlen(o1));
            wb_out_item(8 + w1 + 24, 10 + font_height[F_SMALL] + font_ascent[F_MED], (f32)wb_tw(F_MED, o2), F_MED, WS_LINK, FX_ULINE, WEB_LINK, 0, l2, o2, (int)strlen(o2));
            y += h;
        }
        if (need_js || textmode) {
            const char *m1 = textmode ? "Mode texte : la page sans la mise en forme du site." : "Cette page a besoin de JavaScript (pas disponible sur la GameCube).";
            const char *m2 = textmode ? "Revenir \xC3\xA0 la page normale" : "Essayer le mode texte";
            int lk = new_link(p, LK_HREF, "gc:texte", -1, -1);
            f32 h = font_height[F_SMALL] + font_height[F_MED] + 22;
            wb_shift(0, p->nitems, 0, p->nboxes, 0, h);
            wb_out_box(0, 0, (f32)wv_lw, h - 6, 0xfff4d6ffu, 0xe0c27aff, 15);
            wb_out_item(8, 6 + font_ascent[F_SMALL], (f32)wb_tw(F_SMALL, m1), F_SMALL, 0, 0, 0x5a4300ffu, 0, -1, m1, (int)strlen(m1));
            wb_out_item(8, 10 + font_height[F_SMALL] + font_ascent[F_MED], (f32)wb_tw(F_MED, m2), F_MED, WS_LINK, FX_ULINE, WEB_LINK, 0, lk, m2, (int)strlen(m2));
            y += h;
        }
    }
    p->height = (s32)(y + 8);
    id_fill(p);                                 /* v201 : formulaire de connexion d'un site connu : rempli */
    items_sort(p);
    /* liens : haut / bas d'apres leurs elements ; images : position (ordre de lecture) */
    for (int i = 0; i < p->nlinks; i++) p->links[i].y0 = p->links[i].y1 = -1;
    for (int i = 0; i < nwimg; i++) wimgs[i].y0 = wimgs[i].y1 = -1;
    for (int i = 0; i < p->nitems; i++) {
        witem_t *it = &p->items[i];
        s32 top, bot;
        if (it->style & WS_IMG) { top = it->y - it->h; bot = it->y; }
        else if (it->style & (WS_FIELD | WS_BUTTON)) { top = it->y - font_ascent[F_MED] - 3; bot = top + font_height[F_MED] + 6; }
        else { top = it->y - font_ascent[it->font]; bot = top + font_height[it->font]; }
        if (it->link >= 0 && it->link < p->nlinks) {
            wlink_t *l = &p->links[it->link];
            if (l->y0 < 0 || top < l->y0) l->y0 = top;
            if (bot > l->y1) l->y1 = bot;
        }
        if ((it->style & WS_IMG) && it->img >= 0 && it->img < nwimg) {
            wimg_t *im = &wimgs[it->img];
            if (im->y1 < 0 || im->y0 > top) { im->y0 = top; im->y1 = bot; }
        }
    }
    u32 t4 = perf_us();
    wb_prof[0] = t1 - t0; wb_prof[1] = t2 - t1; wb_prof[2] = t3 - t2; wb_prof[3] = t4 - t3; wb_prof[4] = wd.nn; wb_prof[1] = wb_maxdepth; wb_maxdepth = 0;
    wb_links_visual(p);
    wb_free(); ws_free(); wd_free();
    return 1;
}

// GC Station v118 : mise en page des pages web (inclus dans main.c ; avant : directement dans main.c).
// Nouveau en v118 : pile des elements ouverts et style calcule (web_css.c), couleurs du texte et des fonds,
// encadres, alignement (centre / droite), gras, taille du texte, liens soulignes ou non, ce qui est cache
// par la page, vrais tableaux en colonnes, feuilles de style externes (lues en tache de fond).
#include "web_css.c"

/* v118 : decompresse une reponse gzip (zlib de stb_image) ; NULL si illisible ou plus grande que maxout */
static unsigned char *web_gunzip(const unsigned char *d, int len, int *outlen, int maxout) {
    if (len < 18 || d[0] != 0x1f || d[1] != 0x8b || d[2] != 8) return NULL;
    int flg = d[3];
    long p = 10;
    if (flg & 4) { if (p + 2 > len) return NULL; p += 2 + (d[p] | (d[p + 1] << 8)); }
    if (flg & 8) { while (p < len && d[p]) p++; p++; }
    if (flg & 16) { while (p < len && d[p]) p++; p++; }
    if (flg & 2) p += 2;
    if (p >= len) return NULL;
    u32 isz = (u32)d[len - 4] | (u32)d[len - 3] << 8 | (u32)d[len - 2] << 16 | (u32)d[len - 1] << 24;   /* (taille annoncee) */
    if (isz > (u32)maxout) return NULL;
    int ol = 0;
    char *out = stbi_zlib_decode_malloc_guesssize_headerflag((const char *)d + p, (int)(len - p), (int)isz + 16, &ol, 0);
    if (!out || ol > maxout) { free(out); return NULL; }
    *outlen = ol;
    return (unsigned char *)out;
}

#define WEB_FG      0x202122ffu             /* texte par defaut (page claire, comme un navigateur) */
#define WEB_LINK    0x1a4fbfffu             /* liens */
#define WEB_PAGEBG  0xffffffffu

typedef struct {
    wpage_t *p;
    s32 line_top;
    int line_first;                         // premier element de la ligne en cours
    int x, indent, right;                   // v118 : right = bord droit (colonnes de tableau, encadres)
    int space;                              // espace a inserer avant le prochain mot
    int brk;                                // 0 = rien, 1 = ligne, 2 = paragraphe
    int font, style, link;
    int run;                                // element ouvert (on y ajoute les mots suivants)
    int blank;                              // lignes vides consecutives
    u32 srcpos;                             // position courante dans le HTML
    u32 col, ibg;                           // v118 : couleur du texte, fond des mots (0 = aucun)
    int fx, align;                          // v118 : FX_* ; 1 gauche 2 centre 3 droite
} wlay_t;

static int font_space(int f) { return font_glyphs[f][0].adv; }

static int col_luma(u32 c) { return (int)(((c >> 24) & 255) * 299 + ((c >> 16) & 255) * 587 + ((c >> 8) & 255) * 114) / 1000; }

static void lay_newline(wlay_t *L) {
    wpage_t *p = L->p;
    if (L->line_first < p->nitems) {
        int asc = 0, h = 0;
        for (int i = L->line_first; i < p->nitems; i++) {
            int f = p->items[i].font;
            int ih = (p->items[i].style & WS_FIELD) ? font_height[F_MED] + 6 : font_height[f];
            int ia = font_ascent[f];
            if (p->items[i].style & WS_IMG) { ia = p->items[i].h; ih = p->items[i].h + 3; }
            if (ia > asc) asc = ia;
            if (ih > h) h = ih;
        }
        if (p->items[L->line_first].style & WS_HR) { asc = 6; h = 12; }
        if (L->align >= 2) {                                  /* v118 : ligne centree / a droite */
            int end = L->indent;
            for (int i = L->line_first; i < p->nitems; i++) if (p->items[i].x + p->items[i].w > end) end = p->items[i].x + p->items[i].w;
            int sh = L->right - end;
            if (L->align == 2) sh /= 2;
            if (sh > 0) for (int i = L->line_first; i < p->nitems; i++) p->items[i].x = (s16)(p->items[i].x + sh);
        }
        for (int i = L->line_first; i < p->nitems; i++) {
            p->items[i].y = L->line_top + asc;
            int lk = p->items[i].link;
            if (lk >= 0) {
                if (p->links[lk].y0 < 0 || p->links[lk].y0 > L->line_top) p->links[lk].y0 = L->line_top;
                if (p->links[lk].y1 < L->line_top + h) p->links[lk].y1 = L->line_top + h;
            }
        }
        L->line_top += h + 3;
        L->blank = 0;
    }
    L->line_first = p->nitems;
    L->x = L->indent;
    L->space = 0;
    L->run = -1;
}

static void lay_break(wlay_t *L, int para) {
    lay_newline(L);
    if (para && L->brk < 2 && L->p->nitems > 0) L->line_top += 7;
    if (L->brk < (para ? 2 : 1)) L->brk = para ? 2 : 1;
}

static void lay_br(wlay_t *L) {
    if (L->line_first < L->p->nitems) lay_newline(L);
    else if (L->blank < 1) { L->line_top += font_height[L->font] + 3; L->blank++; }
    L->brk = 1;
}

static witem_t *lay_new_item(wlay_t *L, int x, int w) {
    wpage_t *p = L->p;
    if (!grow((void **)&p->items, &p->icap, p->nitems + 1, sizeof(witem_t), W_MAX_ITEMS)) { p->truncated = 1; return NULL; }
    witem_t *it = &p->items[p->nitems++];
    memset(it, 0, sizeof(*it));
    it->x = (s16)x;
    it->w = (u16)w;
    it->link = (s16)L->link;
    it->font = (u8)L->font;
    it->style = (u8)L->style;
    it->fx = (u8)L->fx;
    it->col = L->col;
    it->bg = L->ibg;
    it->off = p->tlen;
    it->src = L->srcpos;
    it->img = -1;
    L->brk = 0;
    return it;
}

static int text_append(wpage_t *p, const char *s, int n, int join) {
    int cap = (int)p->tcap;
    if (!grow((void **)&p->text, &cap, (int)p->tlen + n + 2, 1, 8 * 1024 * 1024)) { p->truncated = 1; return 0; }
    p->tcap = (u32)cap;
    if (join && p->tlen > 0) p->tlen--;                     /* recolle a la chaine precedente */
    memcpy(p->text + p->tlen, s, n);
    p->tlen += n;
    p->text[p->tlen++] = 0;
    return 1;
}

/* ajoute un mot (UTF-8 affichable, n octets) */
static void lay_word(wlay_t *L, const char *w, int n) {
    if (n <= 0 || L->p->truncated) return;
    char buf[300];
    if (n > (int)sizeof(buf) - 2) n = (int)sizeof(buf) - 2;
    memcpy(buf, w, n);
    buf[n] = 0;
    int ww = text_width_px(L->font, buf, 0) + ((L->fx & FX_BOLD) ? 1 : 0);
    int sw = (L->space && L->x > L->indent) ? font_space(L->font) : 0;
    int maxw = L->right - L->indent;
    if (maxw < 24) maxw = 24;
    if (ww > maxw) {                                         /* mot plus long qu'une ligne : on le coupe */
        const char *s = buf;
        int cut = 0, wsum = 0;
        while (s[cut]) {
            const char *q = s + cut;
            int cp = next_cp(&q);
            int a = font_glyphs[L->font][glyph_index(cp)].adv;
            if (wsum + a > maxw - (L->x - L->indent) - sw && cut > 0) break;
            wsum += a;
            cut = (int)(q - s);
        }
        if (cut <= 0 || cut >= n) cut = n > 1 ? n / 2 : n;
        lay_word(L, buf, cut);
        lay_newline(L);
        lay_word(L, buf + cut, n - cut);
        return;
    }
    if (L->x + sw + ww > L->right && L->x > L->indent) { lay_newline(L); sw = 0; }
    wpage_t *p = L->p;
    if (L->run >= 0) {
        witem_t *r = &p->items[L->run];
        if (r->font == L->font && r->style == L->style && r->link == L->link && r->fx == L->fx && r->col == L->col && r->bg == L->ibg) {
            if (sw) { if (!text_append(p, " ", 1, 1)) return; }
            if (!text_append(p, buf, n, 1)) return;
            r->w = (u16)(L->x + sw + ww - r->x);
            L->x += sw + ww;
            L->space = 0;
            return;
        }
    }
    witem_t *it = lay_new_item(L, L->x + sw, ww);
    if (!it) return;
    if (!text_append(p, buf, n, 0)) { p->nitems--; return; }
    L->run = p->nitems - 1;
    L->x += sw + ww;
    L->space = 0;
}

static void lay_text(wlay_t *L, const char *s) {           /* plusieurs mots */
    while (*s) {
        while (*s == ' ') { L->space = 1; s++; }
        int n = (int)strcspn(s, " ");
        if (n) lay_word(L, s, n);
        s += n;
    }
}

/* champ de saisie ou bouton (largeur fixe) */
static void lay_box(wlay_t *L, int w, int style, const char *label) {
    int sw = (L->space && L->x > L->indent) ? font_space(L->font) : 0;
    if (w > L->right - L->indent) w = L->right - L->indent;
    if (w < 16) w = 16;
    if (L->x + sw + w > L->right && L->x > L->indent) { lay_newline(L); sw = 0; }
    int save = L->style;
    L->style = style;
    witem_t *it = lay_new_item(L, L->x + sw, w);
    L->style = save;
    if (!it) return;
    it->font = F_MED;
    if (!text_append(L->p, label ? label : "", label ? (int)strlen(label) : 0, 0)) { L->p->nitems--; return; }
    L->x += sw + w;
    L->space = 1;
    L->run = -1;
}

static void lay_hr(wlay_t *L) {
    lay_newline(L);
    int save = L->style;
    L->style = WS_HR;
    witem_t *it = lay_new_item(L, L->indent, L->right - L->indent);
    L->style = save;
    if (it && !text_append(L->p, "", 0, 0)) L->p->nitems--;
    lay_newline(L);
}

/* image (ou sa case d'attente avec le texte label) */
static void lay_img(wlay_t *L, int img, int w, int h, const char *label) {
    int sw = (L->space && L->x > L->indent) ? font_space(L->font) : 0;
    if (w > L->right - L->indent) w = L->right - L->indent;
    if (w < 4) w = 4;
    if (L->x + sw + w > L->right && L->x > L->indent) { lay_newline(L); sw = 0; }
    int save = L->style;
    L->style = WS_IMG | (L->link >= 0 ? WS_LINK : 0);
    witem_t *it = lay_new_item(L, L->x + sw, w);
    L->style = save;
    if (!it) return;
    it->img = (s16)img;
    it->h = (u16)h;
    it->font = F_SMALL;
    if (!text_append(L->p, label ? label : "", label ? (int)strlen(label) : 0, 0)) { L->p->nitems--; return; }
    L->x += sw + w;
    L->space = 0;
    L->run = -1;
}

/* v118 : un fond / une bordure de bloc (reserve a l'ouverture : les blocs parents sont dessines avant) */
static int lay_box_open(wpage_t *p, s32 y0, int x0, int x1, u32 bg, u32 border, int sides) {
    if (!grow((void **)&p->boxes, &p->bcap, p->nboxes + 1, sizeof(wbox_t), 20000)) return -1;
    wbox_t *b = &p->boxes[p->nboxes];
    b->y0 = y0; b->y1 = y0; b->x0 = (s16)x0; b->x1 = (s16)x1; b->bg = bg; b->border = border; b->sides = (u8)sides;
    return p->nboxes++;
}

// ---------- Analyse du HTML ----------
static int new_link(wpage_t *p, int kind, const char *url, int field, int form) {
    if (!grow((void **)&p->links, &p->lcap, p->nlinks + 1, sizeof(wlink_t), W_MAX_LINKS)) { p->truncated = 1; return -1; }
    wlink_t *l = &p->links[p->nlinks];
    l->url = url ? pool_add(p, url) : 0;
    l->y0 = -1; l->y1 = -1;
    l->field = (s16)field;
    l->kind = (u8)kind;
    l->form = (s8)form;
    return p->nlinks++;
}

static int new_field(wpage_t *p, int form, int type, const char *name, const char *value) {
    if (!p->fields) p->fields = (wfield_t *)calloc(W_MAX_FIELDS, sizeof(wfield_t));
    if (!p->fields || p->nfields >= W_MAX_FIELDS) return -1;
    wfield_t *f = &p->fields[p->nfields];
    f->form = (s8)form;
    f->type = (u8)type;
    f->name = pool_add(p, name);
    snprintf(f->value, sizeof(f->value), "%s", value ? value : "");
    f->osel = -1;
    return p->nfields++;
}

/* choix d'une liste deroulante */
static int new_opt(wpage_t *p, const char *value, const char *label) {
    if (p->nopts >= W_MAX_OPTS) return -1;
    if (!grow((void **)&p->opts, &p->ocap, (p->nopts + 1) * 2, sizeof(u32), W_MAX_OPTS * 2)) return -1;
    p->opts[p->nopts * 2] = pool_add(p, value);
    p->opts[p->nopts * 2 + 1] = pool_add(p, label);
    return p->nopts++;
}

static char *ci_find(char *h, const char *n) {
    int nl = (int)strlen(n);
    for (; *h; h++) if (strncasecmp(h, n, nl) == 0) return h;
    return NULL;
}

static int tag_is(const char *name, const char *const *list) {
    for (int i = 0; list[i]; i++) if (strcmp(name, list[i]) == 0) return 1;
    return 0;
}

/* cherche "</name" (sans tenir compte des majuscules) a partir de pos */
static int find_close(const unsigned char *s, int len, int pos, const char *name) {
    int nl = (int)strlen(name);
    for (int i = pos; i + nl + 2 <= len; i++)
        if (s[i] == '<' && s[i + 1] == '/' && strncasecmp((const char *)s + i + 2, name, nl) == 0) return i;
    return len;
}

/* texte brut (titre, zone de texte) : entites decodees, espaces reduits */
static void raw_text(const unsigned char *s, int a, int b, char *out, int outsz, int latin) {
    char tmp[1024], col[1024];
    int n = b - a;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    attr_decode((const char *)s + a, n, tmp, sizeof(tmp), latin);
    int k = 0, sp = 0;
    for (char *c = tmp; *c && k < (int)sizeof(col) - 2; c++) {
        if (*c == ' ') { sp = 1; continue; }
        if (sp && k > 0) col[k++] = ' ';
        sp = 0;
        col[k++] = *c;
    }
    col[k] = 0;
    text_display(col, out, outsz);
}

/* v118 : element ouvert (pile) */
typedef struct {
    char name[12];
    u8 block;                               /* 1 = bloc (passage a la ligne avant / apres) */
    u8 bold, uline, size, align, hasfx, nolist;
    u32 col, ibg;                           /* couleur du texte, fond des mots (inline) */
    u32 effbg;                              /* fond sous cet element (pour le contraste) */
    s16 box;                                /* fond / bordure de bloc (-1 : aucun) */
    s16 save_indent, save_right;
} wst_t;
#define WST_MAX 64

/* style d'un element : herite du parent, valeurs par defaut de la balise, attributs, puis CSS */
static void ws_style(wst_t *e, const wst_t *par, const char *name, const char *tag, int tl, int latin,
                     css_el_t *els, int depth, css_decl_t *out, int *hide) {
    char v[600];
    e->col = par ? par->col : WEB_FG;
    e->bold = par ? par->bold : 0;
    e->uline = par ? par->uline : 0;
    e->size = par ? par->size : 2;
    e->align = par ? par->align : 1;
    e->nolist = par ? par->nolist : 0;
    e->ibg = par && !par->block ? par->ibg : 0;
    e->effbg = par ? par->effbg : WEB_PAGEBG;
    /* balise */
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) { e->bold = 1; e->size = name[1] <= '2' ? 3 : 2; }
    else if (!strcmp(name, "b") || !strcmp(name, "strong") || !strcmp(name, "th") || !strcmp(name, "dt")) e->bold = 1;
    else if (!strcmp(name, "small") || !strcmp(name, "sub") || !strcmp(name, "sup") || !strcmp(name, "figcaption")) e->size = 1;
    else if (!strcmp(name, "big")) e->size = 3;
    else if (!strcmp(name, "center")) e->align = 2;
    else if (!strcmp(name, "mark")) e->ibg = 0xfff59dffu;
    else if (!strcmp(name, "a") && tag_attr(tag, tl, "href", NULL, 0, latin)) { e->col = WEB_LINK; e->uline = 1; }
    /* attributs d'autrefois */
    if (tag_attr(tag, tl, "align", v, sizeof(v), latin)) e->align = !strcasecmp(v, "center") ? 2 : !strcasecmp(v, "right") ? 3 : e->align;
    u32 c;
    if (!strcmp(name, "font") && tag_attr(tag, tl, "color", v, sizeof(v), latin) && css_color(v, (int)strlen(v), &c) && c) e->col = c;
    if (!strcmp(name, "body") && tag_attr(tag, tl, "text", v, sizeof(v), latin) && css_color(v, (int)strlen(v), &c) && c) e->col = c;
    if (!strcmp(name, "font") && tag_attr(tag, tl, "size", v, sizeof(v), latin)) {
        int k = atoi(v);
        if (v[0] == '+') e->size = k >= 1 ? 3 : e->size; else if (v[0] == '-') e->size = 1; else e->size = k <= 2 ? 1 : k >= 5 ? 3 : 2;
    }
    if (tag_attr(tag, tl, "hidden", NULL, 0, latin)) *hide = 1;
    /* CSS */
    css_el_t *el = &els[depth - 1];
    memset(el, 0, sizeof(*el));
    el->tag = css_hash(name, (int)strlen(name));
    if (tag_attr(tag, tl, "id", v, sizeof(v), latin) && v[0]) el->id = css_hash(v, (int)strlen(v));
    if (tag_attr(tag, tl, "class", v, sizeof(v), latin)) {
        const char *q = v;
        while (*q && el->ncls < 8) {
            while (*q == ' ') q++;
            int n = (int)strcspn(q, " ");
            if (n) el->cls[el->ncls++] = css_hash(q, n);
            q += n;
        }
    }
    int il = tag_attr(tag, tl, "style", v, sizeof(v), latin) ? (int)strlen(v) : 0;
    css_compute(els, depth, il ? v : NULL, il, out);
    if (!strcmp(name, "td") || !strcmp(name, "th") || !strcmp(name, "tr") || !strcmp(name, "table") || !strcmp(name, "body")) {
        if (!(out->set & CP_BG) && tag_attr(tag, tl, "bgcolor", v, sizeof(v), latin) && css_color(v, (int)strlen(v), &c)) { out->bg = c; out->set |= CP_BG; }
    }
    if ((out->set & CP_DISPLAY) && out->display == CD_NONE) *hide = 1;
    if ((out->set & CP_VIS) && out->vis) *hide = 1;
    if (out->set & CP_COLOR) e->col = out->color;
    if (out->set & CP_WEIGHT) e->bold = out->weight == 2;
    if (out->set & CP_SIZE) e->size = out->size;
    if (out->set & CP_ALIGN) e->align = out->align;
    if (out->set & CP_DECO) e->uline = out->deco == 2;
    if (out->set & CP_LIST) e->nolist = out->list == 1;
}

/* v118 : srcset="a.jpg 480w, b.jpg 960w" ou "a.jpg 1x, b.jpg 2x" : l'image qui convient a la largeur
   (la plus petite assez large ; sinon la plus grande) */
static int img_srcset_pick(const char *v, int want, char *out, int cap) {
    int best_w = 0, best_ok = 0, n = 0;
    char cand[URL_MAX];
    const char *q = v;
    out[0] = 0;
    while (*q) {
        while (*q == ' ' || *q == ',') q++;
        if (!*q) break;
        int ul = 0;
        while (q[ul] && q[ul] != ' ' && !(q[ul] == ',' && (q[ul + 1] == ' ' || !q[ul + 1]))) ul++;
        if (ul <= 0 || ul >= (int)sizeof(cand)) { q += ul > 0 ? ul : 1; continue; }
        memcpy(cand, q, ul); cand[ul] = 0;
        q += ul;
        while (*q == ' ') q++;
        int w = 0;
        if (*q && *q != ',') {
            const char *e; f32 x = css_num(q, &e);
            if (e && (*e == 'w' || *e == 'W')) w = (int)x;
            else if (e && (*e == 'x' || *e == 'X')) w = (int)(x * want + 0.5f);
            while (*q && *q != ',') q++;
        } else w = want;
        if (!strncasecmp(cand, "data:", 5) || w <= 0) continue;
        int ok = w >= want;
        if (!n || (ok && (!best_ok || w < best_w)) || (!ok && !best_ok && w > best_w)) {
            snprintf(out, cap, "%s", cand);
            best_w = w; best_ok = ok;
        }
        n++;
    }
    return out[0] != 0;
}

/* v118 : nombre de colonnes de la premiere ligne d'un tableau (0 : pas en colonnes) */
static int table_cols(const unsigned char *s, int len, int pos, int latin) {
    int cols = 0, rows = 0;
    char tag[256];
    for (int i = pos; i < len && i < pos + 60000; i++) {
        if (s[i] != '<') continue;
        int cl = s[i + 1] == '/';
        int ns = i + 1 + cl, e = ns;
        while (e < len && s[e] != '>' && e - ns < 250) e++;
        int tl = e - ns;
        memcpy(tag, s + ns, tl); tag[tl] = 0;
        char nm[8]; int k = 0;
        while (k < tl && k < 7 && isalpha((u8)tag[k])) { nm[k] = (char)tolower((u8)tag[k]); k++; }
        nm[k] = 0;
        if (!strcmp(nm, "table")) return cl ? cols : 0;    /* (tableau dans le tableau : pas de colonnes) */
        if (!strcmp(nm, "tr")) { if (cl) { if (cols) return cols; } else if (rows++ && cols) return cols; }
        if (!cl && (!strcmp(nm, "td") || !strcmp(nm, "th"))) {
            char v[16];
            int sp = tag_attr(tag, tl, "colspan", v, sizeof(v), latin) ? atoi(v) : 1;
            cols += sp < 1 ? 1 : sp > 8 ? 8 : sp;
        }
        i = e;
    }
    return cols;
}

/* v118 : les elements passent par ligne, mais les cases d'un tableau reviennent en haut de leur ligne :
   on remet les elements dans l'ordre de leur position (l'affichage cherche par position) */
static void items_sort(wpage_t *p) {
    int n = p->nitems, sorted = 1;
    for (int i = 1; i < n && sorted; i++) if (p->items[i].y < p->items[i - 1].y) sorted = 0;
    if (sorted) return;
    witem_t *tmp = (witem_t *)malloc(sizeof(witem_t) * n);
    if (!tmp) return;
    for (int w = 1; w < n; w *= 2) {                       /* tri par fusion (garde l'ordre a egalite) */
        for (int a = 0; a < n; a += 2 * w) {
            int m = a + w < n ? a + w : n, b = a + 2 * w < n ? a + 2 * w : n, i = a, j = m, k = a;
            while (i < m && j < b) tmp[k++] = p->items[j].y < p->items[i].y ? p->items[j++] : p->items[i++];
            while (i < m) tmp[k++] = p->items[i++];
            while (j < b) tmp[k++] = p->items[j++];
        }
        memcpy(p->items, tmp, sizeof(witem_t) * n);
    }
    free(tmp);
}

static void web_layout_html_v1(wpage_t *p, const unsigned char *s, int len, int latin) {
    static const char *const para_tags[] = { "p", "ul", "ol", "dl", "table", "blockquote", "pre", "form",
        "h1", "h2", "h3", "h4", "h5", "h6", "figure", "fieldset", "details", "center", "address", NULL };
    static const char *const line_tags[] = { "div", "section", "article", "header", "footer", "nav", "main",
        "aside", "tr", "li", "dt", "dd", "figcaption", "summary", "caption", "legend", "tbody", "thead", "option", "hr", NULL };
    static const char *const hide_tags[] = { "svg", "select", "template", "iframe", "object", "canvas",
        "audio", "video", "math", "datalist", "map", NULL };
    static const char *const void_tags[] = { "br", "img", "input", "meta", "link", "hr", "wbr", "area", "base", "col",
        "embed", "param", "source", "track", "keygen", NULL };

    wlay_t L;
    memset(&L, 0, sizeof(L));
    L.p = p;
    L.font = F_MED;
    L.link = -1;
    L.run = -1;
    L.brk = 2;
    L.right = wv_lw;
    L.col = WEB_FG;
    L.align = 1;
    p->pagebg = WEB_PAGEBG;
    char base[URL_MAX];
    snprintf(base, sizeof(base), "%s", p->url);
    css_reset();
    p->ncss = 0; p->css_wait = 0;

    char word[300];
    int wn = 0;
    int pre = 0, in_head = 0;
    int hide = 0, hide_n = 0;
    char hide_name[16] = "";
    int cur_form = -1, cur_select = -1, link_items = 0, link_open = 0;
    int list_depth = 0, cell = 0;
    int ol_num[8] = { 0 }, ol_is[8] = { 0 };
    static char tag[2048];
    static char val[URL_MAX], val2[URL_MAX];
    char name[16];
    int pos = 0;
    u32 tick = 0;
    /* v118 : pile des elements */
    static wst_t st[WST_MAX];
    static css_el_t els[WST_MAX];
    int sd = 0;
    /* v118 : tableau en colonnes (un seul niveau) */
    struct { int on, ncols, col, x0, w, rowbox[8], nrb, cellopen, depth; s32 row_top, row_bot; int si, sr; u32 border; } tb;
    memset(&tb, 0, sizeof(tb));

#define FLUSH_WORD() do { if (wn) { lay_word(&L, word, wn); wn = 0; } } while (0)
#define SET_STYLE() (L.style = (link_open ? WS_LINK : 0) | (L.link >= 0 && p->links[L.link].kind == LK_SUBMIT ? WS_BUTTON : 0))
    /* style du haut de la pile -> mise en page */
#define APPLY_TOP() do { \
        const wst_t *t_ = sd > 0 ? &st[sd - 1] : NULL; \
        L.col = t_ ? t_->col : WEB_FG; L.ibg = t_ ? t_->ibg : 0; \
        L.font = !t_ ? F_MED : t_->size == 1 ? F_SMALL : t_->size == 3 ? F_LARGE : F_MED; \
        L.fx = (t_ && t_->bold ? FX_BOLD : 0) | (t_ && t_->uline ? FX_ULINE : 0); \
        L.align = t_ ? t_->align : 1; \
        if (pre) L.align = 1; \
        SET_STYLE(); \
    } while (0)
    /* retire le haut de la pile (fin de bloc, fond / bordure) */
#define POP_ONE() do { wst_t *t_ = &st[sd - 1]; \
        if (t_->block && !(tb.on && (!strcmp(t_->name, "tr") || !strcmp(t_->name, "td") || !strcmp(t_->name, "th")))) lay_break(&L, 0); \
        if (t_->box >= 0) { lay_newline(&L); L.line_top += 4; p->boxes[t_->box].y1 = L.line_top; \
            L.indent = t_->save_indent; L.right = t_->save_right; L.x = L.indent; L.line_top += 2; } \
        sd--; } while (0)
#define TB_END_CELL() do { if (tb.on && tb.cellopen) { lay_newline(&L); if (L.line_top > tb.row_bot) tb.row_bot = L.line_top; tb.cellopen = 0; } } while (0)
#define TB_END_ROW() do { if (tb.on) { TB_END_CELL(); for (int q_ = 0; q_ < tb.nrb; q_++) if (tb.rowbox[q_] >= 0) p->boxes[tb.rowbox[q_]].y1 = tb.row_bot; tb.nrb = 0; \
        L.line_top = tb.row_bot; tb.row_top = tb.row_bot; tb.col = 0; } } while (0)

    for (int i = 0; i < nwimg; i++) wimgs[i].y0 = wimgs[i].y1 = -1;
    while (pos < len && !p->truncated) {
        if ((++tick & 1023) == 0) { if (lay_in_bg) { if (rl_cancel) break; } else bg_tick(); }   /* (v118 : en fond : pas de son ici) */
        L.srcpos = (u32)pos;
        unsigned char c = s[pos];
        if (c == '<') {
            /* commentaire */
            if (pos + 3 < len && s[pos + 1] == '!' && s[pos + 2] == '-' && s[pos + 3] == '-') {
                int e = pos + 4;
                while (e + 2 < len && !(s[e] == '-' && s[e + 1] == '-' && s[e + 2] == '>')) e++;
                pos = e + 3;
                continue;
            }
            int close = (pos + 1 < len && s[pos + 1] == '/');
            int ns = pos + 1 + close;
            if (ns >= len || !(isalpha(s[ns]) || s[ns] == '!' || s[ns] == '?')) {
                /* '<' isole : simple texte */
                if (!hide && !in_head && wn < (int)sizeof(word) - 4) word[wn++] = '<';
                pos++;
                continue;
            }
            /* fin de la balise (en sautant les guillemets) */
            int e = ns;
            char q = 0;
            while (e < len) {
                if (q) { if (s[e] == q) q = 0; }
                else if (s[e] == '"' || s[e] == '\'') { if (e > ns && s[e - 1] == '=') q = (char)s[e]; }
                else if (s[e] == '>') break;
                e++;
            }
            int tl = e - ns;
            if (tl > (int)sizeof(tag) - 1) tl = (int)sizeof(tag) - 1;
            memcpy(tag, s + ns, tl);
            tag[tl] = 0;
            pos = e + 1;
            if (tag[0] == '!' || tag[0] == '?') continue;     /* <!DOCTYPE>, <?xml?> */
            int nl = 0;
            while (nl < tl && nl < 15 && (isalnum((unsigned char)tag[nl]) || tag[nl] == '-')) {
                name[nl] = (char)tolower((unsigned char)tag[nl]);
                nl++;
            }
            name[nl] = 0;
            if (!nl) continue;

            /* contenus a sauter ou a lire en bloc */
            if (!close && strcmp(name, "script") == 0) { pos = find_close(s, len, pos, name); continue; }
            if (!close && strcmp(name, "style") == 0) {                 /* v118 : feuille de style de la page */
                int ce = find_close(s, len, pos, name);
                if (!hide && (!tag_attr(tag, tl, "media", val, sizeof(val), latin) || css_media_ok(val, (int)strlen(val))))
                    css_add_sheet((const char *)s + pos, ce - pos);
                pos = ce;
                continue;
            }
            if (!close && strcmp(name, "link") == 0) {                  /* v118 : <link rel="stylesheet" href="..."> */
                if (tag_attr(tag, tl, "rel", val, sizeof(val), latin) && ci_find(val, "stylesheet") && !ci_find(val, "alternate") &&
                    (!tag_attr(tag, tl, "media", val, sizeof(val), latin) || css_media_ok(val, (int)strlen(val))) &&
                    tag_attr(tag, tl, "href", val, sizeof(val), latin) && url_join(base, val, val2, sizeof(val2)) && p->ncss < 6) {
                    p->css_url[p->ncss++] = pool_add(p, val2);
                    const cssc_t *cc = cssc_get(val2);
                    if (cc) css_add_sheet(cc->data, cc->len);
                    else p->css_wait++;
                }
                continue;
            }
            if (!close && strcmp(name, "title") == 0) {
                int ce = find_close(s, len, pos, "title");
                if (!p->title[0]) raw_text(s, pos, ce, p->title, sizeof(p->title), latin);
                pos = ce;
                continue;
            }
            if (!close && strcmp(name, "textarea") == 0) {
                int ce = find_close(s, len, pos, "textarea");
                if (!hide && cur_form >= 0) {
                    char init[256];
                    raw_text(s, pos, ce, init, sizeof(init), latin);
                    if (!tag_attr(tag, tl, "name", val, sizeof(val), latin)) val[0] = 0;
                    FLUSH_WORD();
                    int f = new_field(p, cur_form, FD_TEXT, val, init);
                    if (f >= 0) {
                        int save = L.link;
                        L.link = new_link(p, LK_INPUT, NULL, f, cur_form);
                        if (L.link >= 0) lay_box(&L, 300, WS_FIELD, NULL);
                        L.link = save;
                    }
                }
                pos = ce;
                continue;
            }
            /* <option> : un choix de la liste deroulante en cours */
            if (!close && strcmp(name, "option") == 0 && cur_select >= 0) {
                int nx = pos;
                while (nx < len && s[nx] != '<') nx++;
                char lab[200];
                raw_text(s, pos, nx, lab, sizeof(lab), latin);
                char *lt = trim(lab);
                if (!tag_attr(tag, tl, "value", val, sizeof(val), latin)) snprintf(val, sizeof(val), "%s", lt);
                wfield_t *f = &p->fields[cur_select];
                if (new_opt(p, val, lt[0] ? lt : val) >= 0) {
                    if (f->nopt == 0) f->opt0 = (s16)(p->nopts - 1);
                    if (f->opt0 + f->nopt == p->nopts - 1) {
                        if (tag_attr(tag, tl, "selected", NULL, 0, latin) || f->osel < 0) f->osel = f->nopt;
                        f->nopt++;
                    }
                }
                continue;
            }
            if (close && strcmp(name, "select") == 0 && cur_select >= 0) {
                wfield_t *f = &p->fields[cur_select];
                if (f->nopt > 0) {
                    if (f->osel < 0 || f->osel >= f->nopt) f->osel = 0;
                    snprintf(f->value, sizeof(f->value), "%s", p->pool + p->opts[(f->opt0 + f->osel) * 2]);
                }
                cur_select = -1;
            }
            /* elements caches */
            if (hide) {
                if (strcmp(name, hide_name) == 0) {
                    if (close) { if (--hide_n <= 0) { hide = 0; hide_name[0] = 0; } }
                    else if (!(tl > 0 && tag[tl - 1] == '/')) hide_n++;
                }
                continue;
            }
            if (!close && tag_is(name, hide_tags)) {
                if (strcmp(name, "select") == 0 && cur_form >= 0 && tag_attr(tag, tl, "name", val, sizeof(val), latin)) {
                    FLUSH_WORD();
                    cur_select = new_field(p, cur_form, FD_SELECT, val, "");
                    if (cur_select >= 0) {                        /* case cliquable : le choix en cours */
                        p->fields[cur_select].opt0 = (s16)p->nopts;
                        int save = L.link;
                        L.link = new_link(p, LK_SELECT, NULL, cur_select, cur_form);
                        if (L.link >= 0) lay_box(&L, 220, WS_FIELD, NULL);
                        L.link = save;
                    }
                }
                int selfclose = (tl > 0 && tag[tl - 1] == '/');
                if (!selfclose) { hide = 1; hide_n = 1; snprintf(hide_name, sizeof(hide_name), "%s", name); }
                continue;
            }
            if (strcmp(name, "head") == 0) { in_head = !close; continue; }
            if (strcmp(name, "meta") == 0) {
                if (tag_attr(tag, tl, "http-equiv", val, sizeof(val), latin) && strcasecmp(val, "refresh") == 0 &&
                    tag_attr(tag, tl, "content", val, sizeof(val), latin)) {
                    int d = atoi(val);
                    char *u = ci_find(val, "url=");
                    if (u && !p->refresh[0]) {
                        u += 4;
                        if (*u == '\'' || *u == '"') u++;
                        u[strcspn(u, "'\"")] = 0;
                        if (url_join(base, u, p->refresh, sizeof(p->refresh))) p->refresh_delay = d;
                    }
                }
                continue;
            }
            if (strcmp(name, "base") == 0) {
                if (tag_attr(tag, tl, "href", val, sizeof(val), latin) && url_join(p->url, val, val2, sizeof(val2)))
                    snprintf(base, sizeof(base), "%s", val2);
                continue;
            }
            if (strcmp(name, "body") == 0 || strcmp(name, "html") == 0) in_head = 0;
            if (in_head) continue;

            FLUSH_WORD();
            int is_void = tag_is(name, void_tags) || (tl > 0 && tag[tl - 1] == '/' && strcmp(name, "a") && strcmp(name, "div"));
            int is_h = (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]);
            int ua_block = tag_is(name, para_tags) || tag_is(name, line_tags);
            int near_tbl = sd - 1;
            while (near_tbl >= 0 && strcmp(st[near_tbl].name, "table")) near_tbl--;
            int col_mode_tag = tb.on && near_tbl == tb.depth && (!strcmp(name, "tr") || !strcmp(name, "td") || !strcmp(name, "th") ||
                                                                 !strcmp(name, "tbody") || !strcmp(name, "thead") || !strcmp(name, "tfoot"));

            /* ---- v118 : ouverture : style calcule, element cache ? ---- */
            css_decl_t cd;
            memset(&cd, 0, sizeof(cd));
            int el_block = ua_block;
            if (!close) {
                /* fermetures implicites : <p> ferme par un bloc, <li> par le <li> suivant, cases et lignes de tableau */
                if (ua_block && sd > 0 && !strcmp(st[sd - 1].name, "p")) POP_ONE();
                {   /* <li> sans </li>, cases et lignes de tableau, <dt>/<dd>, <option> : le precedent se ferme */
                    static const char *const grp[][4] = { { "li", "ul", "ol", NULL }, { "td", "tr", "table", NULL }, { "th", "tr", "table", NULL },
                                                          { "tr", "table", "tbody", NULL }, { "dt", "dl", NULL, NULL }, { "dd", "dl", NULL, NULL } };
                    for (unsigned g = 0; g < sizeof(grp) / sizeof(grp[0]); g++) {
                        if (strcmp(name, grp[g][0])) continue;
                        int k = sd - 1;
                        while (k >= 0 && sd - k <= 8) {
                            const char *kn = st[k].name;
                            if (!strcmp(kn, grp[g][1]) || (grp[g][2] && !strcmp(kn, grp[g][2])) || !strcmp(kn, "table")) { k = -1; break; }
                            if (!strcmp(kn, name) || (g <= 2 && g >= 1 && (!strcmp(kn, "td") || !strcmp(kn, "th"))) ||
                                (g >= 4 && (!strcmp(kn, "dt") || !strcmp(kn, "dd")))) break;
                            k--;
                        }
                        if (k >= 0 && sd - k <= 8) while (sd > k) POP_ONE();
                        break;
                    }
                }
                if (sd < WST_MAX && !(is_void && strcmp(name, "img") && strcmp(name, "br") && strcmp(name, "hr") && strcmp(name, "input"))) {
                    int hid = 0;
                    wst_t e;
                    memset(&e, 0, sizeof(e));
                    snprintf(e.name, sizeof(e.name), "%s", name);
                    e.box = -1;
                    ws_style(&e, sd > 0 ? &st[sd - 1] : NULL, name, tag, tl, latin, els, sd + 1, &cd, &hid);
                    if (hid) {                                   /* cache par la page : tout le contenu est saute */
                        if (!is_void) { hide = 1; hide_n = 1; snprintf(hide_name, sizeof(hide_name), "%s", name); }
                        continue;
                    }
                    if (cd.set & CP_DISPLAY) el_block = cd.display == CD_BLOCK ? 1 : cd.display == CD_INLINE ? 0 : el_block;
                    if (!strcmp(name, "body") || !strcmp(name, "html")) {
                        if ((cd.set & CP_BG) && cd.bg) { p->pagebg = cd.bg; e.effbg = cd.bg; }
                        el_block = 0;
                    }
                    if (!is_void) {
                        e.block = (u8)el_block;
                        if (!el_block && (cd.set & CP_BG) && cd.bg && strcmp(name, "body") && strcmp(name, "html")) { e.ibg = cd.bg; e.effbg = cd.bg; }
                        /* contraste : texte lisible sur son fond */
                        u32 bgc = (el_block && (cd.set & CP_BG) && cd.bg) ? cd.bg : e.effbg;
                        int dl = col_luma(e.col) - col_luma(bgc);
                        if (dl < 0) dl = -dl;
                        if (dl < 70) e.col = col_luma(bgc) > 140 ? WEB_FG : 0xf2f2f2ffu;
                        int pushed_block = el_block && !col_mode_tag;
                        if (pushed_block) lay_break(&L, tag_is(name, para_tags) || is_h);
                        st[sd] = e;
                        sd++;
                        /* fond ou bordure de bloc */
                        u32 bbg = (cd.set & CP_BG) ? cd.bg : 0, bbd = (cd.set & CP_BORDER) && cd.bside ? cd.border : 0;
                        if (!strcmp(name, "table") && tag_attr(tag, tl, "border", val, sizeof(val), latin) && atoi(val) > 0) bbd = 0xa2a9b1ffu;
                        if (pushed_block && (bbg || bbd) && strcmp(name, "body") && strcmp(name, "html") && strcmp(name, "table")) {
                            st[sd - 1].box = (s16)lay_box_open(p, L.line_top, L.indent, L.right, bbg, bbd, bbd ? cd.bside : 0);
                            if (bbg) st[sd - 1].effbg = bbg;
                            st[sd - 1].save_indent = (s16)L.indent; st[sd - 1].save_right = (s16)L.right;
                            if (L.right - L.indent > 60) { L.indent += 6; L.right -= 6; }
                            L.line_top += 4; L.x = L.indent;
                        }
                        if (!strcmp(name, "table")) tb.border = bbd;
                    }
                }
            }
            if (close && !is_void) {
                /* ---- v118 : fermeture : on retire de la pile jusqu'a l'element du meme nom ---- */
                int k = sd - 1;
                while (k >= 0 && strcmp(st[k].name, name)) k--;
                if (k >= 0 && sd - k <= 12) {
                    while (sd > k) POP_ONE();
                    el_block = 0;
                }
            }
            APPLY_TOP();

            if (!col_mode_tag) {
                if (close && tag_is(name, para_tags)) lay_break(&L, 1);
                else if (!close && is_h) lay_break(&L, 1);
            }

            if (strcmp(name, "br") == 0) { lay_br(&L); continue; }
            if (strcmp(name, "hr") == 0 && !close) { lay_hr(&L); continue; }
            if (strcmp(name, "pre") == 0) { pre = !close; continue; }
            /* ---- v118 : tableau en colonnes ---- */
            if (strcmp(name, "table") == 0) {
                if (!close && !tb.on) {
                    int nc = table_cols(s, len, pos, latin);
                    if (nc >= 2 && nc <= 8 && L.right - L.indent >= nc * 50) {
                        memset(&tb, 0, sizeof(tb));
                        tb.on = 1; tb.ncols = nc; tb.depth = sd - 1;
                        tb.si = L.indent; tb.sr = L.right;
                        tb.x0 = L.indent;
                        tb.w = L.right - L.indent;
                        char wv[24];
                        if (tag_attr(tag, tl, "width", wv, sizeof(wv), latin) && !strchr(wv, '%') && atoi(wv) > 0 && atoi(wv) < tb.w) tb.w = atoi(wv);
                        if (tag_attr(tag, tl, "style", val, sizeof(val), latin)) {     /* style="width: 300px" */
                            char *wq = ci_find(val, "width");
                            while (wq && wq > val && (wq[-1] == '-' || isalpha((u8)wq[-1]))) wq = ci_find(wq + 5, "width");   /* (pas max-width) */
                            if (wq) { wq += 5; while (*wq == ' ' || *wq == ':') wq++; int wpx = atoi(wq); if (wpx > 60 && strstr(wq, "px") && wpx < tb.w) tb.w = wpx; }
                        }
                        lay_newline(&L);
                        tb.row_top = tb.row_bot = L.line_top;
                        tb.border = (sd > 0 && st[sd - 1].box < 0) ? tb.border : 0;
                        { u32 bb = 0; if (tag_attr(tag, tl, "border", val, sizeof(val), latin) && atoi(val) > 0) bb = 0xa2a9b1ffu; if (cd.set & CP_BORDER) bb = cd.border; tb.border = bb; }
                    }
                } else if (close && tb.on && sd <= tb.depth) {
                    TB_END_ROW();
                    lay_newline(&L);
                    L.indent = tb.si; L.right = tb.sr; L.x = L.indent;
                    L.line_top = tb.row_bot;
                    tb.on = 0;
                    lay_break(&L, 1);
                }
                continue;
            }
            if (col_mode_tag) {
                if (!strcmp(name, "tr")) { if (!close) { TB_END_ROW(); } else TB_END_CELL(); continue; }
                if (!strcmp(name, "td") || !strcmp(name, "th")) {
                    if (close) { TB_END_CELL(); continue; }
                    TB_END_CELL();
                    char v[16];
                    int span = tag_attr(tag, tl, "colspan", v, sizeof(v), latin) ? atoi(v) : 1;
                    if (span < 1) span = 1;
                    if (tb.col >= tb.ncols) TB_END_ROW();
                    if (tb.col + span > tb.ncols) span = tb.ncols - tb.col;
                    int cx0 = tb.x0 + tb.w * tb.col / tb.ncols, cx1 = tb.x0 + tb.w * (tb.col + span) / tb.ncols;
                    tb.col += span;
                    lay_newline(&L);
                    L.line_top = tb.row_top;
                    u32 cbg = (cd.set & CP_BG) ? cd.bg : 0;
                    if ((cbg || tb.border) && tb.nrb < 8) tb.rowbox[tb.nrb++] = lay_box_open(p, tb.row_top, cx0, cx1, cbg, tb.border, 15);
                    if (cbg && sd > 0) st[sd - 1].effbg = cbg;
                    L.indent = cx0 + 4; L.right = cx1 - 4; L.x = L.indent;
                    L.line_top += 3;
                    L.space = 0; L.run = -1; L.brk = 2;
                    tb.cellopen = 1;
                    continue;
                }
                continue;                                          /* (tbody, thead...) */
            }
            if (strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0 || strcmp(name, "blockquote") == 0 ||
                strcmp(name, "dd") == 0) {
                int d = close ? -1 : 1;
                if (strcmp(name, "dd") != 0 && strcmp(name, "blockquote") != 0) {
                    if (!close) {
                        if (list_depth < 8) { ol_is[list_depth] = (name[0] == 'o'); ol_num[list_depth] = 0; }
                        list_depth++;
                    } else if (list_depth > 0) list_depth--;
                }
                L.indent += d * 18;
                if (L.indent < 0) L.indent = 0;
                if (L.indent > 180) L.indent = 180;
                if (L.indent > L.right - 60) L.indent = L.right - 60 > 0 ? L.right - 60 : 0;
                L.x = L.indent;
                continue;
            }
            if (strcmp(name, "li") == 0 && !close) {
                int d = list_depth > 0 ? list_depth - 1 : 0;
                if (sd > 0 && !st[sd - 1].block) { L.space = 1; continue; }   /* (liste en ligne : menu horizontal) */
                if (sd > 0 && st[sd - 1].nolist) continue;                      /* (list-style: none : pas de puce) */
                char bullet[16];
                if (d < 8 && ol_is[d]) snprintf(bullet, sizeof(bullet), "%d.", ++ol_num[d]);
                else snprintf(bullet, sizeof(bullet), "\xC2\xB7");
                int save = L.style, sl = L.link;
                L.style = WS_DIM;
                L.link = -1;
                L.x = L.indent - 5 - text_width_px(L.font, bullet, 0);
                if (L.x < 0) L.x = 0;
                L.space = 0;
                lay_word(&L, bullet, (int)strlen(bullet));
                L.style = save;
                L.link = sl;
                L.run = -1;
                if (L.x < L.indent + 3) L.x = L.indent + 3;
                continue;
            }
            if (strcmp(name, "tr") == 0) { cell = 0; continue; }
            if ((strcmp(name, "td") == 0 || strcmp(name, "th") == 0) && !close) {
                if (cell++ > 0) { L.x += 16; L.space = 0; L.run = -1; }
                continue;
            }
            if (strcmp(name, "a") == 0) {
                if (link_open) {
                    if (link_items == p->nitems) lay_text(&L, "[lien]");
                    link_open = 0;
                    L.link = -1;
                }
                if (!close && tag_attr(tag, tl, "href", val, sizeof(val), latin) && url_join(base, val, val2, sizeof(val2))) {
                    L.link = new_link(p, LK_HREF, val2, -1, -1);
                    if (L.link >= 0) { link_open = 1; link_items = p->nitems; }
                }
                SET_STYLE();
                continue;
            }
            if (strcmp(name, "img") == 0 || (strcmp(name, "source") == 0 && sd > 0 && !strcmp(st[sd - 1].name, "picture"))) {
                /* v118 : <picture><source srcset type> : la premiere source lisible remplace l'<img> qui suit */
                static char pic_src[URL_MAX];
                static int pic_have = 0;
                if (strcmp(name, "source") == 0) {
                    if (!pic_have && (!tag_attr(tag, tl, "media", val, sizeof(val), latin) || css_media_ok(val, (int)strlen(val)))) {
                        int ok_type = !tag_attr(tag, tl, "type", val, sizeof(val), latin) || ci_find(val, "webp") || ci_find(val, "jpeg") ||
                                      ci_find(val, "png") || ci_find(val, "gif");
                        if (ok_type && tag_attr(tag, tl, "srcset", val, sizeof(val), latin) && img_srcset_pick(val, wv_lw, val2, sizeof(val2))) {
                            snprintf(pic_src, sizeof(pic_src), "%s", val2);
                            pic_have = 1;
                        }
                    }
                    continue;
                }
                char alt[200] = "";
                if (tag_attr(tag, tl, "alt", val, sizeof(val), latin) && strlen(val) < 120) text_display(val, alt, sizeof(alt));
                /* adresse : <picture>, srcset (la taille qui convient a l'ecran), src, ou data-src... */
                int idx = -1;
                if (pic_have && sd > 0 && !strcmp(st[sd - 1].name, "picture") && url_join(base, pic_src, val2, sizeof(val2))) idx = img_get(val2);
                pic_have = 0;
                static const char *const srcs[] = { "srcset", "data-srcset", "src", "data-src", "data-original", "data-lazy-src", NULL };
                for (int k = 0; srcs[k] && idx < 0; k++) {
                    if (!tag_attr(tag, tl, srcs[k], val, sizeof(val), latin) || !val[0] || strncasecmp(val, "data:", 5) == 0) continue;
                    if (strstr(srcs[k], "srcset")) { if (!img_srcset_pick(val, wv_lw, val2, sizeof(val2))) continue; snprintf(val, sizeof(val), "%s", val2); }
                    if (url_join(base, val, val2, sizeof(val2))) idx = img_get(val2);
                }
                wimg_t *im = idx >= 0 ? &wimgs[idx] : NULL;
                if (im) {
                    char wv[24], hv[24];
                    if (tag_attr(tag, tl, "width", wv, sizeof(wv), latin) && !strchr(wv, '%') && atoi(wv) > 0) im->aw = (s16)atoi(wv);
                    if (tag_attr(tag, tl, "height", hv, sizeof(hv), latin) && !strchr(hv, '%') && atoi(hv) > 0) im->ah = (s16)atoi(hv);
                }
                int dw = 0, dh = 0;
                if (im && im->st != IMG_FAIL) img_box(im->aw, im->ah, im->nw, im->nh, L.right - L.indent, &dw, &dh);
                if (im && im->st != IMG_FAIL && dw > 0) {
                    if (dw >= 3 && dh >= 3) {                       /* sinon : image d'espacement */
                        if (im->y0 < 0) im->y0 = L.line_top;          /* (position approchee, pour l'ordre de lecture des images) */
                        lay_img(&L, idx, dw, dh, alt);
                    }
                } else if (im && im->st != IMG_FAIL && im->st != IMG_OK) {
                    /* taille inconnue tant que l'image n'est pas lue : petite case */
                    char lab[48];
                    fit_text(F_SMALL, alt, 160, lab, sizeof(lab));
                    int w = text_width_px(F_SMALL, lab, 0) + 12;
                    if (im->y0 < 0) im->y0 = L.line_top;
                    lay_img(&L, idx, w < 24 ? 24 : w, 18, lab);
                } else if (alt[0] && (link_open || strlen(alt) > 2)) {
                    char t2[210];
                    snprintf(t2, sizeof(t2), "[%s]", alt);
                    int save = L.style;
                    if (!link_open) L.style |= WS_DIM;
                    lay_text(&L, t2);
                    L.style = save;
                }
                continue;
            }
            if (strcmp(name, "form") == 0) {
                cur_form = -1;
                if (!close && p->nforms < W_MAX_FORMS) {
                    char act[URL_MAX];
                    if (!tag_attr(tag, tl, "action", val, sizeof(val), latin) || !val[0] ||
                        !url_join(base, val, act, sizeof(act)))
                        snprintf(act, sizeof(act), "%s", p->url);
                    p->forms[p->nforms].action = pool_add(p, act);
                    p->forms[p->nforms].post = tag_attr(tag, tl, "method", val, sizeof(val), latin) && strcasecmp(val, "post") == 0;
                    cur_form = p->nforms++;
                }
                continue;
            }
            if (strcmp(name, "input") == 0 && cur_form >= 0) {
                char type[24] = "text", nm[128] = "", v[256] = "";
                tag_attr(tag, tl, "type", type, sizeof(type), latin);
                tag_attr(tag, tl, "name", nm, sizeof(nm), latin);
                tag_attr(tag, tl, "value", v, sizeof(v), latin);
                for (char *t = type; *t; t++) *t = (char)tolower((unsigned char)*t);
                if (strcmp(type, "hidden") == 0) { if (nm[0]) new_field(p, cur_form, FD_HIDDEN, nm, v); }
                else if (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0) {
                    if (nm[0]) {                                  /* case a cocher / bouton radio */
                        int f = new_field(p, cur_form, type[0] == 'c' ? FD_CHECK : FD_RADIO, nm, v[0] ? v : "on");
                        if (f >= 0) {
                            p->fields[f].checked = (u8)(tag_attr(tag, tl, "checked", NULL, 0, latin) != 0);
                            int save = L.link;
                            L.link = new_link(p, LK_TOGGLE, NULL, f, cur_form);
                            if (L.link >= 0) lay_box(&L, 22, WS_FIELD, NULL);
                            L.link = save;
                        }
                    }
                } else if (strcmp(type, "submit") == 0 || strcmp(type, "image") == 0) {
                    int f = new_field(p, cur_form, FD_SUBMIT, nm, v);
                    int save = L.link;
                    L.link = new_link(p, LK_SUBMIT, NULL, f, cur_form);
                    if (L.link >= 0) {
                        char disp[200];
                        text_display(v[0] ? v : "Envoyer", disp, sizeof(disp));
                        lay_box(&L, text_width_px(F_MED, disp, 0) + 20, WS_BUTTON, disp);
                    }
                    L.link = save;
                } else if (strcmp(type, "text") == 0 || strcmp(type, "search") == 0 || strcmp(type, "email") == 0 ||
                           strcmp(type, "url") == 0 || strcmp(type, "number") == 0 || strcmp(type, "tel") == 0 ||
                           strcmp(type, "password") == 0) {
                    int f = new_field(p, cur_form, FD_TEXT, nm, v);
                    if (f >= 0) {
                        p->fields[f].secret = (u8)(strcmp(type, "password") == 0);
                        int save = L.link;
                        L.link = new_link(p, LK_INPUT, NULL, f, cur_form);
                        if (L.link >= 0) lay_box(&L, 240, WS_FIELD, NULL);
                        L.link = save;
                    }
                }
                continue;
            }
            if (strcmp(name, "button") == 0) {
                if (close) {
                    if (link_open && L.link >= 0 && p->links[L.link].kind == LK_SUBMIT) {
                        if (link_items == p->nitems) lay_text(&L, "[Envoyer]");
                        link_open = 0;
                        L.link = -1;
                        SET_STYLE();
                    }
                } else if (cur_form >= 0 && !link_open) {
                    char type[24] = "submit", nm[128] = "", v[256] = "";
                    tag_attr(tag, tl, "type", type, sizeof(type), latin);
                    if (strcasecmp(type, "submit") == 0) {
                        tag_attr(tag, tl, "name", nm, sizeof(nm), latin);
                        tag_attr(tag, tl, "value", v, sizeof(v), latin);
                        int f = new_field(p, cur_form, FD_SUBMIT, nm, v);
                        L.link = new_link(p, LK_SUBMIT, NULL, f, cur_form);
                        if (L.link >= 0) { link_open = 1; link_items = p->nitems; }
                        SET_STYLE();
                    }
                }
                continue;
            }
            continue;
        }
        if (hide || in_head) { pos++; continue; }

        /* texte */
        unsigned cp = (c == '&') ? read_entity(s, len, &pos) : read_cp(s, len, &pos, latin);
        if (cp == '\r') continue;
        if (cp == ' ' || cp == '\n' || cp == '\t' || cp == '\f') {
            FLUSH_WORD();
            if (pre && cp == '\n') lay_br(&L);
            else L.space = 1;
            continue;
        }
        if (cp == 0xA0) cp = ' ';                                    /* espace insecable : reste dans le mot */
        if (wn > (int)sizeof(word) - 8) FLUSH_WORD();
        wn += cp_out(cp, word + wn);
    }
    FLUSH_WORD();
    if (link_open && link_items == p->nitems) lay_text(&L, "[lien]");
    if (tb.on) { TB_END_ROW(); L.indent = tb.si; L.right = tb.sr; tb.on = 0; }
    while (sd > 0) {                                                 /* blocs encore ouverts (page coupee) */
        if (st[sd - 1].box >= 0) { lay_newline(&L); p->boxes[st[sd - 1].box].y1 = L.line_top + 4; L.indent = st[sd - 1].save_indent; L.right = st[sd - 1].save_right; }
        sd--;
    }
    if (p->truncated) {
        lay_break(&L, 1);
        L.style = WS_DIM;
        L.link = -1;
        L.col = 0x72777dffu;
        lay_text(&L, "(page trop longue : la suite n'est pas affichée)");
    }
    lay_newline(&L);
    p->height = L.line_top;
    items_sort(p);
    for (int i = 0; i < p->nitems; i++) {                           /* position des images (ordre de lecture) */
        witem_t *it = &p->items[i];
        if (!(it->style & WS_IMG) || it->img < 0) continue;
        wimg_t *im = &wimgs[it->img];
        if (im->y1 < 0 || im->y0 > it->y - it->h) { im->y0 = it->y - it->h; im->y1 = it->y; }
    }
#undef FLUSH_WORD
#undef SET_STYLE
#undef APPLY_TOP
#undef TB_END_CELL
#undef TB_END_ROW
#undef POP_ONE
}

/* v201 : nouveau moteur (arbre HTML, CSS complet, boites) ; l'ancien si la memoire manque */
#include "web_dom.c"
#include "web_style.c"
#include "web_box.c"
static void web_layout_html(wpage_t *p, const unsigned char *s, int len, int latin) {
#ifndef GEKKO
    if (getenv("PCSIM_WEBV1")) { web_layout_html_v1(p, s, len, latin); return; }
#endif
    if (web_layout_v2(p, s, len, latin)) return;
    if (lay_in_bg && rl_cancel) return;
    /* (repart de zero : ce que le nouveau moteur a deja mis) */
    char url[URL_MAX]; snprintf(url, sizeof(url), "%s", p->url);
    unsigned char *src = p->src; int srclen = p->srclen, kind = p->kind, lat = p->latin;
    p->src = NULL;
    wpage_free(p);
    snprintf(p->url, sizeof(p->url), "%s", url);
    p->src = src; p->srclen = srclen; p->kind = kind; p->latin = lat;
    web_layout_html_v1(p, s, len, latin);
}

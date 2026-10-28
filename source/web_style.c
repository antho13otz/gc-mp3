// GC Station v201 : moteur de page web, 2e partie : les styles (CSS) de chaque element (inclus par web_layout.c).
// Selecteurs : balise, .classe, #id, *, [attribut] (=, ~=, ^=, $=, *=, |=), descendant, enfant (>), frere (+, ~),
// :first-child, :last-child, :only-child, :nth-child(an+b), :nth-last-child, :first-of-type, :last-of-type,
// :nth-of-type, :not(...), :is/:where(...), :root, :empty, :link ; ::before / ::after (texte de « content »).
// Proprietes : boite (marges, padding, bordures, largeur / hauteur et leurs limites, box-sizing), affichage
// (display, position, float, clear, visibility), texte (couleur, fond, police, taille, gras, italique,
// chasse fixe, hauteur de ligne, alignement, decoration, majuscules, white-space, vertical-align, retrait),
// listes, flex (direction, retour a la ligne, grow / shrink / basis, justify, align, gap), grille (colonnes),
// tableaux (table-layout, border-spacing). Valeurs : px, em, rem, %, vw, vh, pt..., calc(), min(), max(),
// clamp(), var(--x, defaut) et les proprietes personnalisees (heritees).
// Feuille du navigateur (UA) d'abord, puis attributs d'autrefois (bgcolor, width...), puis la page.

#define WS_VW 600.0f                            /* largeur supposee pour % dans min()/max() et vw */
#define WS_VH 480.0f

enum {
    WP_NONE = 0, WP_DISPLAY, WP_POSITION, WP_FLOAT, WP_CLEAR, WP_VISIBILITY, WP_COLOR, WP_BG, WP_BGIMG,
    WP_FONT_SIZE, WP_FONT_WEIGHT, WP_FONT_STYLE, WP_FONT_FAMILY, WP_FONT, WP_LINE_HEIGHT, WP_TEXT_ALIGN,
    WP_TEXT_DECO, WP_TEXT_TRANSFORM, WP_WHITE_SPACE, WP_VALIGN, WP_LIST_STYLE, WP_LIST_TYPE, WP_TEXT_INDENT,
    WP_WIDTH, WP_HEIGHT, WP_MIN_W, WP_MAX_W, WP_MIN_H, WP_MAX_H,
    WP_MARGIN, WP_MT, WP_MR, WP_MB, WP_ML, WP_PADDING, WP_PT, WP_PR, WP_PB, WP_PL,
    WP_BORDER, WP_BT, WP_BR, WP_BB, WP_BL, WP_BW, WP_BTW, WP_BRW, WP_BBW, WP_BLW, WP_BC, WP_BTC, WP_BRC, WP_BBC, WP_BLC,
    WP_BS, WP_BTS, WP_BRS, WP_BBS, WP_BLS, WP_BOX_SIZING, WP_OVERFLOW,
    WP_FLEX_DIR, WP_FLEX_WRAP, WP_FLEX_FLOW, WP_FLEX, WP_FLEX_GROW, WP_FLEX_SHRINK, WP_FLEX_BASIS, WP_JUSTIFY, WP_ALIGN_ITEMS,
    WP_ALIGN_SELF, WP_GAP, WP_ROW_GAP, WP_COL_GAP, WP_GRID_COLS, WP_GRID_COL, WP_ORDER, WP_CONTENT, WP_OPACITY, WP_CLIP,
    WP_CLIP_PATH, WP_TABLE_LAYOUT, WP_BORDER_SPACING, WP_BORDER_COLLAPSE, WP_LEFT, WP_TOP, WP_ASPECT, WP_MARGIN_INLINE,
    WP_MARGIN_BLOCK, WP_PADDING_INLINE, WP_PADDING_BLOCK, WP_INSET,
    WP_CUSTOM, WP_COUNT
};
static const struct { const char *n; u8 id; } ws_props[] = {
    { "display", WP_DISPLAY }, { "position", WP_POSITION }, { "float", WP_FLOAT }, { "clear", WP_CLEAR },
    { "visibility", WP_VISIBILITY }, { "color", WP_COLOR }, { "background-color", WP_BG }, { "background", WP_BG },
    { "background-image", WP_BGIMG }, { "font-size", WP_FONT_SIZE }, { "font-weight", WP_FONT_WEIGHT }, { "font-style", WP_FONT_STYLE },
    { "font-family", WP_FONT_FAMILY }, { "font", WP_FONT }, { "line-height", WP_LINE_HEIGHT }, { "text-align", WP_TEXT_ALIGN },
    { "text-decoration", WP_TEXT_DECO }, { "text-decoration-line", WP_TEXT_DECO }, { "text-transform", WP_TEXT_TRANSFORM },
    { "white-space", WP_WHITE_SPACE }, { "white-space-collapse", WP_WHITE_SPACE }, { "vertical-align", WP_VALIGN }, { "list-style", WP_LIST_STYLE },
    { "list-style-type", WP_LIST_TYPE }, { "text-indent", WP_TEXT_INDENT }, { "width", WP_WIDTH }, { "height", WP_HEIGHT },
    { "min-width", WP_MIN_W }, { "max-width", WP_MAX_W }, { "min-height", WP_MIN_H }, { "max-height", WP_MAX_H },
    { "inline-size", WP_WIDTH }, { "max-inline-size", WP_MAX_W }, { "min-inline-size", WP_MIN_W },
    { "margin", WP_MARGIN }, { "margin-top", WP_MT }, { "margin-right", WP_MR }, { "margin-bottom", WP_MB }, { "margin-left", WP_ML },
    { "margin-block-start", WP_MT }, { "margin-block-end", WP_MB }, { "margin-inline-start", WP_ML }, { "margin-inline-end", WP_MR },
    { "margin-inline", WP_MARGIN_INLINE }, { "margin-block", WP_MARGIN_BLOCK },
    { "padding", WP_PADDING }, { "padding-top", WP_PT }, { "padding-right", WP_PR }, { "padding-bottom", WP_PB }, { "padding-left", WP_PL },
    { "padding-block-start", WP_PT }, { "padding-block-end", WP_PB }, { "padding-inline-start", WP_PL }, { "padding-inline-end", WP_PR },
    { "padding-inline", WP_PADDING_INLINE }, { "padding-block", WP_PADDING_BLOCK },
    { "border", WP_BORDER }, { "border-top", WP_BT }, { "border-right", WP_BR }, { "border-bottom", WP_BB }, { "border-left", WP_BL },
    { "border-block-start", WP_BT }, { "border-block-end", WP_BB }, { "border-inline-start", WP_BL }, { "border-inline-end", WP_BR },
    { "border-width", WP_BW }, { "border-top-width", WP_BTW }, { "border-right-width", WP_BRW }, { "border-bottom-width", WP_BBW },
    { "border-left-width", WP_BLW }, { "border-color", WP_BC }, { "border-top-color", WP_BTC }, { "border-right-color", WP_BRC },
    { "border-bottom-color", WP_BBC }, { "border-left-color", WP_BLC }, { "border-style", WP_BS }, { "border-top-style", WP_BTS },
    { "border-right-style", WP_BRS }, { "border-bottom-style", WP_BBS }, { "border-left-style", WP_BLS },
    { "box-sizing", WP_BOX_SIZING }, { "overflow", WP_OVERFLOW }, { "overflow-x", WP_OVERFLOW },
    { "flex-direction", WP_FLEX_DIR }, { "flex-wrap", WP_FLEX_WRAP }, { "flex-flow", WP_FLEX_FLOW }, { "flex", WP_FLEX },
    { "flex-grow", WP_FLEX_GROW }, { "flex-shrink", WP_FLEX_SHRINK }, { "flex-basis", WP_FLEX_BASIS },
    { "justify-content", WP_JUSTIFY }, { "align-items", WP_ALIGN_ITEMS }, { "align-self", WP_ALIGN_SELF },
    { "gap", WP_GAP }, { "grid-gap", WP_GAP }, { "row-gap", WP_ROW_GAP }, { "grid-row-gap", WP_ROW_GAP }, { "column-gap", WP_COL_GAP },
    { "grid-column-gap", WP_COL_GAP }, { "grid-template-columns", WP_GRID_COLS }, { "grid-column", WP_GRID_COL }, { "order", WP_ORDER },
    { "content", WP_CONTENT }, { "opacity", WP_OPACITY }, { "clip", WP_CLIP }, { "clip-path", WP_CLIP_PATH },
    { "table-layout", WP_TABLE_LAYOUT }, { "border-spacing", WP_BORDER_SPACING }, { "border-collapse", WP_BORDER_COLLAPSE },
    { "left", WP_LEFT }, { "top", WP_TOP }, { "aspect-ratio", WP_ASPECT }, { "inset", WP_INSET },
};
static u32 ws_prop_h[sizeof(ws_props) / sizeof(ws_props[0])];

/* longueur : px + pct (% du bloc conteneur) ; auto / none */
typedef struct { f32 px, pct; u8 k; } wl_t;    /* k : 0 valeur, 1 auto, 2 none */
#define WL_AUTO ((wl_t){ 0, 0, 1 })
#define WL_NONE ((wl_t){ 0, 0, 2 })
#define WL_ZERO ((wl_t){ 0, 0, 0 })
static f32 wl_res(wl_t l, f32 cb) { return l.px + l.pct * cb * 0.01f; }

enum { DSP_NONE = 0, DSP_INLINE, DSP_BLOCK, DSP_IBLOCK, DSP_LIST, DSP_TABLE, DSP_ITABLE, DSP_ROWGRP, DSP_ROW, DSP_CELL,
       DSP_CAPTION, DSP_COL, DSP_FLEX, DSP_IFLEX, DSP_GRID, DSP_IGRID, DSP_CONTENTS };
enum { POS_STATIC = 0, POS_REL, POS_ABS, POS_FIXED, POS_STICKY };
enum { WSP_NORMAL = 0, WSP_NOWRAP, WSP_PRE, WSP_PREWRAP, WSP_PRELINE };
enum { VA_BASE = 0, VA_SUB, VA_SUPER, VA_MIDDLE, VA_TOP, VA_BOTTOM, VA_TTOP, VA_TBOT };
enum { LT_NONE = 0, LT_DISC, LT_CIRCLE, LT_SQUARE, LT_DEC, LT_LALPHA, LT_UALPHA, LT_LROMAN, LT_UROMAN, LT_DEC0 };
enum { JC_START = 0, JC_CENTER, JC_END, JC_BETWEEN, JC_AROUND, JC_EVENLY };
enum { AI_STRETCH = 0, AI_START, AI_CENTER, AI_END, AI_BASE, AI_AUTO };

typedef struct {
    u8 display, position, flt, clear, ws, align, deco, transform;
    u8 italic, mono, valign, list, list_inside, boxsz, ovf, hide;
    u8 fdir, fwrap, justify, aitems, aself, tlayout, collapse, bstyle_set;
    u8 gcols, gspan, has_before, has_after;
    u16 weight;
    s16 order;
    f32 fsize;                                  /* taille du texte (px) */
    f32 lh;                                     /* hauteur de ligne : > 0 px, < 0 facteur (-1.2 = 1.2 x), 0 normale */
    f32 fgrow, fshrink;
    f32 gap_r, gap_c, bspacing, aspect;
    wl_t fbasis;
    wl_t w, h, minw, maxw, minh, maxh, tindent;
    wl_t m[4], p[4];                            /* haut, droite, bas, gauche */
    f32 bw[4];
    u32 bc[4];                                  /* 0 : couleur du texte */
    u8 bst[4];                                  /* 0 aucune, 1 trait */
    u32 color, bg;
    f32 gfr[12];                                /* colonnes de grille (fr ; < 0 : px) */
} wcs_t;

/* ---------- feuilles de style ---------- */
typedef struct { u16 prop; u8 imp; u8 pad; u32 cname; u32 v0; u16 vn; } wdecl_t;   /* valeur brute dans ws.txt */
typedef struct {                                /* un test d'un selecteur compose */
    u8 type;                                    /* 1 attribut, 2 pseudo-classe, 3 :not(compose) / :is(...) */
    u8 op;                                      /* attribut : 0 present, 1 =, 2 ~=, 3 ^=, 4 $=, 5 *=, 6 |= ; pseudo : WPC_* */
    s16 a, b;                                   /* an+b */
    u32 name;
    u32 v0; u16 vn;                             /* valeur (attribut) ; :not / :is -> vn = nombre de choix (alt) */
    u8 icase;
    u32 alt[4];                                 /* :not / :is : composes (un par choix) */
} wtest_t;
enum { WPC_FIRST = 1, WPC_LAST, WPC_ONLY, WPC_NTH, WPC_NTHLAST, WPC_FTYPE, WPC_LTYPE, WPC_NTHTYPE, WPC_NTHLTYPE, WPC_OTYPE,
       WPC_ROOT, WPC_EMPTY, WPC_LINK, WPC_NEVER, WPC_ALWAYS, WPC_CHECKED, WPC_DISABLED };
typedef struct {                                /* selecteur compose (de droite a gauche) */
    u32 tag, id;
    u32 cls[4]; u8 ncls;
    u8 comb;                                    /* lien avec le compose suivant (a gauche) : 0 aucun, 1 descendant, 2 enfant, 3 frere +, 4 freres ~ */
    u16 nt; u32 t0;                             /* tests : ws.tests[t0 .. t0 + nt) */
} wcomp_t;
typedef struct {
    u32 c0; u8 nc;                              /* composes : ws.comps[c0 .. c0 + nc), le 1er = le plus a droite */
    u8 origin;                                  /* 0 navigateur, 1 page */
    u8 pe;                                      /* 0 element, 1 ::before, 2 ::after */
    u32 spec, order;
    u32 d0; u16 dn;
    s32 next;                                   /* suivant dans la meme case */
} wrule_t;

#define WS_BUCKETS 2048
#define WS_MAX_RULES 30000
typedef struct {
    wrule_t *r; int nr, rcap;
    wcomp_t *c; int nc, ccap;
    wtest_t *t; int nt, tscap;
    wdecl_t *d; int nd, dcap;
    char *txt; int tlen, tcap;                  /* valeurs, noms */
    s32 bucket[WS_BUCKETS], any;
    u32 order;
    int origin;
    int has_pe;
} wsheet_t;
static wsheet_t ws;

static void ws_free(void) { free(ws.r); free(ws.c); free(ws.t); free(ws.d); free(ws.txt); memset(&ws, 0, sizeof(ws)); }
static void ws_reset(void) {
    ws_free();
    for (int i = 0; i < WS_BUCKETS; i++) ws.bucket[i] = -1;
    ws.any = -1;
}
static int ws_txt(const char *s, int n) {
    if (!grow((void **)&ws.txt, &ws.tcap, ws.tlen + n + 1, 1, 4 * 1024 * 1024)) return -1;
    int o = ws.tlen;
    memcpy(ws.txt + o, s, n);
    ws.txt[o + n] = 0;
    ws.tlen += n + 1;
    return o;
}
static int ws_prop_id(const char *p, int n) {
    if (n > 2 && p[0] == '-' && p[1] == '-') return WP_CUSTOM;
    if (!ws_prop_h[0]) for (unsigned i = 0; i < sizeof(ws_props) / sizeof(ws_props[0]); i++) ws_prop_h[i] = css_hash(ws_props[i].n, (int)strlen(ws_props[i].n));
    if (n > 1 && p[0] == '-') {                 /* -webkit-box-sizing... : sans prefixe */
        const char *q = (const char *)memchr(p + 1, '-', n - 1);
        if (q && (!strncasecmp(p, "-webkit-", 8) || !strncasecmp(p, "-moz-", 5) || !strncasecmp(p, "-ms-", 4))) { n -= (int)(q + 1 - p); p = q + 1; }
    }
    u32 h = css_hash(p, n);
    for (unsigned i = 0; i < sizeof(ws_props) / sizeof(ws_props[0]); i++) if (ws_prop_h[i] == h && (int)strlen(ws_props[i].n) == n) return ws_props[i].id;
    return WP_NONE;
}

/* "prop: valeur; ..." -> declarations (ws.d) ; -> nombre */
static int ws_parse_decls(const char *s, int n, u32 *d0) {
    *d0 = (u32)ws.nd;
    int cnt = 0, i = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == ';' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
        int ps = i;
        while (i < n && s[i] != ':' && s[i] != ';' && s[i] != '{' && s[i] != '}') i++;
        if (i >= n || s[i] != ':') {
            if (i < n && s[i] == '{') { int dd = 0; while (i < n) { if (s[i] == '{') dd++; else if (s[i] == '}' && --dd <= 0) { i++; break; } i++; } }
            else while (i < n && s[i] != ';') i++;
            continue;
        }
        int pe = i;
        while (pe > ps && (s[pe - 1] == ' ' || s[pe - 1] == '\t' || s[pe - 1] == '\n' || s[pe - 1] == '\r')) pe--;
        i++;
        int vs = i, depth = 0;
        char q = 0;
        while (i < n) {
            char ch = s[i];
            if (q) { if (ch == '\\') i++; else if (ch == q) q = 0; }
            else if (ch == '"' || ch == '\'') q = ch;
            else if (ch == '(' || ch == '[') depth++;
            else if (ch == ')' || ch == ']') depth--;
            else if ((ch == ';' || ch == '}') && depth <= 0) break;
            i++;
        }
        int ve = i;
        while (vs < ve && (s[vs] == ' ' || s[vs] == '\t' || s[vs] == '\n' || s[vs] == '\r')) vs++;
        while (ve > vs && (s[ve - 1] == ' ' || s[ve - 1] == '\t' || s[ve - 1] == '\n' || s[ve - 1] == '\r')) ve--;
        int imp = 0;
        for (int k = ve - 1; k > vs; k--) {
            if (s[k] == '!') { int r = k + 1; while (r < ve && s[r] == ' ') r++; if (ve - r >= 9 && !strncasecmp(s + r, "important", 9)) { imp = 1; ve = k; while (ve > vs && s[ve - 1] == ' ') ve--; } break; }
            if (!isalpha((u8)s[k]) && s[k] != ' ') break;
        }
        int pid = pe > ps ? ws_prop_id(s + ps, pe - ps) : WP_NONE;
        if (pid != WP_NONE && ve > vs && ve - vs < 4000) {
            if (!grow((void **)&ws.d, &ws.dcap, ws.nd + 1, sizeof(wdecl_t), 200000)) break;
            int o = ws_txt(s + vs, ve - vs);
            if (o < 0) break;
            wdecl_t *d = &ws.d[ws.nd++];
            d->prop = (u16)pid; d->imp = (u8)imp; d->v0 = (u32)o; d->vn = (u16)(ve - vs);
            d->cname = pid == WP_CUSTOM ? css_hash(s + ps, pe - ps) : 0;
            /* (valeurs : les fins de ligne deviennent des espaces) */
            for (int k = 0; k < ve - vs; k++) if (ws.txt[o + k] == '\n' || ws.txt[o + k] == '\r' || ws.txt[o + k] == '\t') ws.txt[o + k] = ' ';
            cnt++;
        }
        if (i < n && s[i] == '}') i++;
    }
    return cnt;
}

/* ---------- selecteurs ---------- */
static int ws_ident(const char *s, int n, int i) {      /* fin d'un identifiant */
    while (i < n && (isalnum((u8)s[i]) || s[i] == '-' || s[i] == '_' || (u8)s[i] >= 0x80 || s[i] == '\\')) {
        if (s[i] == '\\' && i + 1 < n) i++;
        i++;
    }
    return i;
}
static u32 ws_hash_esc(const char *s, int n) {          /* identifiant avec \ : sans les \ */
    char b[128]; int k = 0;
    for (int i = 0; i < n && k < 127; i++) { if (s[i] == '\\' && i + 1 < n) i++; b[k++] = s[i]; }
    return css_hash(b, k);
}
static int ws_new_test(void) {
    if (!grow((void **)&ws.t, &ws.tscap, ws.nt + 1, sizeof(wtest_t), 100000)) return -1;
    memset(&ws.t[ws.nt], 0, sizeof(wtest_t));
    return ws.nt++;
}
static int ws_new_comp(void) {
    if (!grow((void **)&ws.c, &ws.ccap, ws.nc + 1, sizeof(wcomp_t), 150000)) return -1;
    memset(&ws.c[ws.nc], 0, sizeof(wcomp_t));
    return ws.nc++;
}
/* an+b : odd, even, 3, 2n+1, -n+3 */
static void ws_anb(const char *s, int n, s16 *a, s16 *b) {
    while (n > 0 && *s == ' ') { s++; n--; }
    while (n > 0 && s[n - 1] == ' ') n--;
    if (n >= 3 && !strncasecmp(s, "odd", 3)) { *a = 2; *b = 1; return; }
    if (n >= 4 && !strncasecmp(s, "even", 4)) { *a = 2; *b = 0; return; }
    const char *nn = (const char *)memchr(s, 'n', n);
    if (!nn) nn = (const char *)memchr(s, 'N', n);
    if (!nn) { *a = 0; *b = (s16)atoi(s); return; }
    if (nn == s || (nn == s + 1 && s[0] == '+')) *a = 1;
    else if (nn == s + 1 && s[0] == '-') *a = -1;
    else *a = (s16)atoi(s);
    const char *r = nn + 1;
    char t[16]; int k = 0;
    while (r < s + n && k < 15) { if (*r != ' ') t[k++] = *r; r++; }
    t[k] = 0;
    *b = (s16)(k ? atoi(t) : 0);
}
static int ws_parse_selector(const char *s, int n, u32 *c0, u8 *nc, u32 *spec, u8 *pe, int depth);
/* un compose : s[*pi] -> comps ; specificite ajoutee ; 0 si non gere (regle ignoree) */
static int ws_parse_compound(const char *s, int n, int *pi, int ci, u32 *spec, u8 *pe, int depth) {
    int i = *pi, any = 0;
    wcomp_t *c = &ws.c[ci];
    c->t0 = (u32)ws.nt;
    int pend_t[4], pend_a[4], pend_e[4], pend_where[4], npend = 0;    /* :not(...) : lus apres les tests de ce compose */
    while (i < n) {
        char ch = s[i];
        if (ch == ' ' || ch == '>' || ch == '+' || ch == '~' || ch == '\n' || ch == '\t' || ch == '\r' || ch == ',' || ch == ')') break;
        if (ch == '*') { i++; any = 1; continue; }
        if (ch == '.' || ch == '#') {
            int ws_ = i + 1, we = ws_ident(s, n, ws_);
            if (we == ws_) return 0;
            u32 h = ws_hash_esc(s + ws_, we - ws_);
            c = &ws.c[ci];
            if (ch == '.') { if (c->ncls < 4) c->cls[c->ncls++] = h; *spec += 1 << 10; }
            else { c->id = h; *spec += 1 << 20; }
            i = we; any = 1;
            continue;
        }
        if (ch == '[') {
            int e = i + 1, q = 0;
            while (e < n && (q || s[e] != ']')) { if (q) { if (s[e] == q) q = 0; } else if (s[e] == '"' || s[e] == '\'') q = s[e]; e++; }
            if (e >= n) return 0;
            int ti = ws_new_test();
            if (ti < 0) return 0;
            wtest_t *t = &ws.t[ti];
            t->type = 1;
            int a = i + 1;
            while (a < e && s[a] == ' ') a++;
            int ne = ws_ident(s, n, a);
            if (ne > e) ne = e;
            t->name = css_hash(s + a, ne - a);
            int o = ne;
            while (o < e && s[o] == ' ') o++;
            if (o < e) {
                char op = s[o];
                t->op = op == '=' ? 1 : op == '~' ? 2 : op == '^' ? 3 : op == '$' ? 4 : op == '*' ? 5 : op == '|' ? 6 : 0;
                o += op == '=' ? 1 : 2;
                while (o < e && s[o] == ' ') o++;
                int ve = e;
                while (ve > o && s[ve - 1] == ' ') ve--;
                if (ve - 2 > o && (s[ve - 1] == 'i' || s[ve - 1] == 'I' || s[ve - 1] == 's') && s[ve - 2] == ' ') { t->icase = s[ve - 1] != 's'; ve -= 2; while (ve > o && s[ve - 1] == ' ') ve--; }
                if (ve > o && (s[o] == '"' || s[o] == '\'')) { o++; if (ve > o && (s[ve - 1] == '"' || s[ve - 1] == '\'')) ve--; }
                int vo = ws_txt(s + o, ve - o);
                if (vo < 0) return 0;
                t = &ws.t[ti];
                t->v0 = (u32)vo; t->vn = (u16)(ve - o);
            }
            ws.c[ci].nt++;
            *spec += 1 << 10;
            i = e + 1; any = 1;
            continue;
        }
        if (ch == ':') {
            int dbl = i + 1 < n && s[i + 1] == ':';
            int ks = i + 1 + dbl, ke = ws_ident(s, n, ks);
            const char *pn = s + ks; int pl = ke - ks;
            int args = -1, arge = -1;
            if (ke < n && s[ke] == '(') {
                int dd = 0, e = ke;
                while (e < n) { if (s[e] == '(') dd++; else if (s[e] == ')' && --dd == 0) break; e++; }
                if (e >= n) return 0;
                args = ke + 1; arge = e; ke = e + 1;
            }
#define PN(x) (pl == (int)strlen(x) && !strncasecmp(pn, x, pl))
            if (dbl || PN("before") || PN("after") || PN("first-line") || PN("first-letter")) {
                if (PN("before")) *pe = 1;
                else if (PN("after")) *pe = 2;
                else return 0;                  /* ::marker, ::placeholder, ::selection... : non */
                *spec += 1;
                i = ke; any = 1;
                continue;
            }
            int ti = ws_new_test();
            if (ti < 0) return 0;
            wtest_t *t = &ws.t[ti];
            t->type = 2;
            if (PN("first-child")) t->op = WPC_FIRST;
            else if (PN("last-child")) t->op = WPC_LAST;
            else if (PN("only-child")) t->op = WPC_ONLY;
            else if (PN("first-of-type")) t->op = WPC_FTYPE;
            else if (PN("last-of-type")) t->op = WPC_LTYPE;
            else if (PN("only-of-type")) t->op = WPC_OTYPE;
            else if (PN("root")) t->op = WPC_ROOT;
            else if (PN("empty")) t->op = WPC_EMPTY;
            else if (PN("link") || PN("any-link")) t->op = WPC_LINK;
            else if (PN("checked")) t->op = WPC_CHECKED;
            else if (PN("disabled")) t->op = WPC_DISABLED;
            else if (PN("enabled") || PN("defined") || PN("scope") || PN("read-write") || PN("optional") || PN("valid") || PN("lang") || PN("dir") || PN("required") || PN("placeholder-shown")) t->op = WPC_ALWAYS;
            else if (PN("nth-child") || PN("nth-last-child") || PN("nth-of-type") || PN("nth-last-of-type")) {
                if (args < 0) return 0;
                t->op = PN("nth-child") ? WPC_NTH : PN("nth-last-child") ? WPC_NTHLAST : PN("nth-of-type") ? WPC_NTHTYPE : WPC_NTHLTYPE;
                int ae = arge;
                const char *of = NULL;
                for (int k = args; k + 3 < arge; k++) if (!strncasecmp(s + k, " of ", 4)) { of = s + k; break; }
                if (of) return 0;
                ws_anb(s + args, ae - args, &t->a, &t->b);
            } else if (PN("not") || PN("is") || PN("where") || PN("matches") || PN("-webkit-any") || PN("-moz-any")) {
                if (args < 0 || depth > 1 || npend >= 4) return 0;
                t->type = 3;
                t->op = PN("not") ? 1 : 0;
                pend_t[npend] = ti; pend_a[npend] = args; pend_e[npend] = arge; pend_where[npend] = PN("where"); npend++;
            } else if (PN("hover") || PN("focus") || PN("active") || PN("visited") || PN("focus-within") || PN("focus-visible") ||
                       PN("target") || PN("invalid") || PN("indeterminate") || PN("fullscreen") || PN("modal") || PN("autofill") ||
                       PN("popover-open") || PN("has") || PN("host") || PN("open") || PN("playing") || PN("paused")) t->op = WPC_NEVER;
            else return 0;
#undef PN
            ws.c[ci].nt++;
            if (t->type == 2) *spec += 1 << 10;
            i = ke; any = 1;
            continue;
        }
        if (isalpha((u8)ch) || ch == '_' || ch == '\\' || (u8)ch >= 0x80) {
            int we = ws_ident(s, n, i);
            ws.c[ci].tag = ws_hash_esc(s + i, we - i);
            *spec += 1;
            i = we; any = 1;
            continue;
        }
        return 0;
    }
    for (int q = 0; q < npend; q++) {           /* :not(a, b) / :is(...) : un compose par choix (les autres : regle oubliee) */
        int k = pend_a[q], arge = pend_e[q], cnt = 0;
        u32 best = 0;
        while (k < arge) {
            int ks2 = k, dd = 0;
            while (k < arge && (s[k] != ',' || dd)) { if (s[k] == '(') dd++; else if (s[k] == ')') dd--; k++; }
            u32 sc = 0, c02 = 0; u8 pe2 = 0, nc2 = 0;
            if (ws_parse_selector(s + ks2, k - ks2, &c02, &nc2, &sc, &pe2, depth + 1) && nc2 == 1 && !pe2 && cnt < 4) {
                ws.t[pend_t[q]].alt[cnt++] = c02;
                if (sc > best) best = sc;
            } else if (ws.t[pend_t[q]].op == 0) return 0;       /* (:is avec un choix complique : non) */
            k++;
        }
        wtest_t *t = &ws.t[pend_t[q]];
        t->vn = (u16)cnt;
        if (!cnt) { if (t->op == 1) { t->type = 2; t->op = WPC_ALWAYS; } else return 0; }
        if (!pend_where[q]) *spec += best;
    }
    *pi = i;
    return any;
}
/* selecteur complexe (sans virgule) -> composes, du plus a droite au plus a gauche */
static int ws_parse_selector(const char *s, int n, u32 *c0, u8 *nc, u32 *spec, u8 *pe, int depth) {
    int list[16], cn = 0, combs[16];
    int i = 0, comb = 0;
    while (i < n) {
        int sp = 0;
        while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) { i++; sp = 1; }
        if (i >= n) break;
        if (s[i] == '>' || s[i] == '+' || s[i] == '~') { comb = s[i] == '>' ? 2 : s[i] == '+' ? 3 : 4; i++; continue; }
        if (cn > 0 && !comb) comb = sp ? 1 : 1;
        if (cn >= 16) return 0;
        int ci = ws_new_comp();
        if (ci < 0) return 0;
        if (!ws_parse_compound(s, n, &i, ci, spec, pe, depth)) return 0;
        combs[cn] = comb;
        list[cn++] = ci;
        comb = 0;
        if (*pe && i < n) { int j = i; while (j < n && s[j] == ' ') j++; if (j < n) return 0; }   /* (::before au milieu : non) */
    }
    if (!cn) return 0;
    /* range de droite a gauche : on recopie a la fin */
    u32 first = (u32)ws.nc;
    for (int k = cn - 1; k >= 0; k--) {
        int ci = ws_new_comp();
        if (ci < 0) return 0;
        ws.c[ci] = ws.c[list[k]];
        ws.c[ci].comb = (u8)(k > 0 ? combs[k] : 0);
    }
    /* (les composes d'origine restent : un peu de place perdue, pas grave) */
    *c0 = first;
    *nc = (u8)cn;
    return 1;
}
static u32 ws_bkey(u32 kind, u32 h) { return (h * 2654435761u ^ kind * 40503u) & (WS_BUCKETS - 1); }
static void ws_add_rule(u32 c0, u8 nc, u32 spec, u8 pe, u32 d0, u16 dn) {
    if (ws.nr >= WS_MAX_RULES) return;
    if (!grow((void **)&ws.r, &ws.rcap, ws.nr + 1, sizeof(wrule_t), WS_MAX_RULES)) return;
    wrule_t *r = &ws.r[ws.nr];
    r->c0 = c0; r->nc = nc; r->spec = spec; r->pe = pe; r->origin = (u8)ws.origin; r->order = ws.order++;
    r->d0 = d0; r->dn = dn;
    if (pe) ws.has_pe = 1;
    const wcomp_t *k = &ws.c[c0];
    s32 *head;
    if (k->id) head = &ws.bucket[ws_bkey(1, k->id)];
    else if (k->ncls) head = &ws.bucket[ws_bkey(2, k->cls[0])];
    else if (k->tag) head = &ws.bucket[ws_bkey(3, k->tag)];
    else head = &ws.any;
    r->next = *head;
    *head = ws.nr++;
}

static void ws_parse_sheet(const char *s, int n, int depth) {
    int i = 0;
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ';' || c == '}') { i++; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { int e = i + 2; while (e + 1 < n && !(s[e] == '*' && s[e + 1] == '/')) e++; i = e + 2; continue; }
        if (c == '<' && i + 3 < n && !strncmp(s + i, "<!--", 4)) { i += 4; continue; }
        if (c == '-' && i + 2 < n && !strncmp(s + i, "-->", 3)) { i += 3; continue; }
        if (c == '@') {
            int k = i + 1;
            while (k < n && (isalnum((u8)s[k]) || s[k] == '-')) k++;
            const char *kw = s + i + 1; int kl = k - i - 1;
            int b = k;
            while (b < n && s[b] != '{' && s[b] != ';') b++;
            if (b >= n) return;
            if (s[b] == ';') { i = b + 1; continue; }
            int e = css_block_end(s, n, b);
            if (depth < 4 && ((css_word_is(kw, kl, "media") && css_media_ok(s + k, b - k)) || css_word_is(kw, kl, "supports") ||
                              css_word_is(kw, kl, "layer") || css_word_is(kw, kl, "container") || css_word_is(kw, kl, "scope") || css_word_is(kw, kl, "document")))
                ws_parse_sheet(s + b + 1, e - b - 2 > 0 ? e - b - 2 : 0, depth + 1);
            i = e;
            continue;
        }
        int b = i;
        while (b < n && s[b] != '{') b++;
        if (b >= n) return;
        int e = css_block_end(s, n, b);
        int bs = b + 1, bn = e - b - 2;
        if (bn > 0) {
            u32 d0;
            int dn = ws_parse_decls(s + bs, bn, &d0);
            if (dn > 0) {
                int k = i;
                while (k < b) {
                    int ks = k, dd = 0;
                    while (k < b && (s[k] != ',' || dd)) { if (s[k] == '(' || s[k] == '[') dd++; else if (s[k] == ')' || s[k] == ']') dd--; k++; }
                    u32 c0 = 0, spec = 0; u8 nc = 0, pe = 0;
                    int save_c = ws.nc, save_t = ws.nt, save_x = ws.tlen;
                    if (ws_parse_selector(s + ks, k - ks, &c0, &nc, &spec, &pe, 0)) ws_add_rule(c0, nc, spec, pe, d0, (u16)dn);
                    else { ws.nc = save_c; ws.nt = save_t; ws.tlen = save_x; }
                    k++;
                }
            }
        }
        i = e;
    }
}

/* feuille du navigateur : l'aspect par defaut de chaque balise */
static const char ws_ua_css[] =
    "html,body,div,p,ul,ol,li,dl,dt,dd,h1,h2,h3,h4,h5,h6,blockquote,pre,form,fieldset,legend,figure,figcaption,"
    "section,article,aside,header,footer,nav,main,address,details,summary,hgroup,center,menu,dialog,listing,plaintext,optgroup{display:block}"
    "head,script,style,title,meta,link,base,template,noembed,noframes,datalist,param,area,map,track,source,option,col,colgroup,[hidden],input[type=hidden],dialog:not([open]){display:none}"
    "li{display:list-item}table{display:table;border-spacing:2px}caption{display:table-caption;text-align:center}"
    "thead,tbody,tfoot{display:table-row-group}tr{display:table-row}td,th{display:table-cell;padding:1px}"
    "th{font-weight:bold;text-align:center}img,input,button,select,textarea,iframe,video,svg,canvas,embed,object,progress,meter{display:inline-block}"
    "body{margin:8px}p,dl,blockquote,figure,fieldset{margin:1em 0}blockquote,figure{margin-left:40px;margin-right:40px}"
    "h1{font-size:2em;margin:.67em 0;font-weight:bold}h2{font-size:1.5em;margin:.83em 0;font-weight:bold}"
    "h3{font-size:1.17em;margin:1em 0;font-weight:bold}h4{margin:1.33em 0;font-weight:bold}"
    "h5{font-size:.83em;margin:1.67em 0;font-weight:bold}h6{font-size:.67em;margin:2.33em 0;font-weight:bold}"
    "ul,ol,menu{margin:1em 0;padding-left:40px}ul{list-style-type:disc}ol{list-style-type:decimal}"
    "ul ul,ol ul,ul ol,ol ol{margin-top:0;margin-bottom:0}ul ul,ol ul{list-style-type:circle}ul ul ul,ul ol ul,ol ul ul{list-style-type:square}"
    "dd{margin-left:40px}b,strong{font-weight:bold}i,em,cite,var,dfn,address{font-style:italic}"
    "pre,code,kbd,samp,tt,listing,plaintext{font-family:monospace}pre,listing,plaintext{white-space:pre;margin:1em 0}"
    "small{font-size:.83em}big{font-size:1.2em}sub{vertical-align:sub;font-size:.83em}sup{vertical-align:super;font-size:.83em}"
    "u,ins{text-decoration:underline}s,strike,del{text-decoration:line-through}mark{background-color:#fff59d;color:#000}"
    "a:link{color:#1a4fbf;text-decoration:underline}center{text-align:center}hr{display:block;margin:.5em 0;border:1px solid #a2a9b1}"
    "nobr{white-space:nowrap}fieldset{border:1px solid #c0c0c0;padding:.35em .75em .625em;margin-left:2px;margin-right:2px}"
    "legend{padding:0 2px}summary{font-weight:bold}details:not([open])>:not(summary){display:none}"
    "abbr[title]{text-decoration:none}q:before{content:'\\201C'}q:after{content:'\\201D'}"
    "noscript{display:block}";

/* ---------- valeurs ---------- */
typedef struct { f32 px, pct; int ok; } wnum_t;
static f32 ws_cur_fs = 16, ws_root_fs = 16, ws_cur_cb = WS_VW;
static int ws_textmode = 0;                     /* v201 : mode texte : ni feuilles, ni style="" de la page */
/* une valeur (nombre + unite, ou calc/min/max/clamp) ; *pe = fin */
static wnum_t ws_expr(const char *s, int n, int *pe);
static wnum_t ws_atom(const char *s, int n, int *pe) {
    wnum_t r = { 0, 0, 0 };
    int i = 0;
    while (i < n && s[i] == ' ') i++;
    if (i < n && (s[i] == '(')) {
        int e = 0;
        r = ws_expr(s + i + 1, n - i - 1, &e);
        i += 1 + e;
        if (i < n && s[i] == ')') i++;
        *pe = i;
        return r;
    }
    if (n - i > 5 && (!strncasecmp(s + i, "calc(", 5) || !strncasecmp(s + i, "-webkit-calc(", 13) || !strncasecmp(s + i, "min(", 4) || !strncasecmp(s + i, "max(", 4) || !strncasecmp(s + i, "clamp(", 6))) {
        int fk = !strncasecmp(s + i, "min(", 4) ? 1 : !strncasecmp(s + i, "max(", 4) ? 2 : !strncasecmp(s + i, "clamp(", 6) ? 3 : 0;
        const char *op = (const char *)memchr(s + i, '(', n - i);
        int j = (int)(op - s) + 1;
        wnum_t a[3]; int na = 0;
        while (j < n && na < 3) {
            int e = 0;
            a[na] = ws_expr(s + j, n - j, &e);
            if (!a[na].ok) { *pe = n; return r; }
            na++;
            j += e;
            while (j < n && s[j] == ' ') j++;
            if (j < n && s[j] == ',') { j++; continue; }
            if (j < n && s[j] == ')') { j++; break; }
            break;
        }
        *pe = j;
        if (!na) return r;
        if (fk == 0) return a[0];
        /* min / max / clamp : % compte avec la largeur supposee */
        f32 v[3];
        for (int k = 0; k < na; k++) v[k] = a[k].px + a[k].pct * ws_cur_cb * 0.01f;
        f32 x = v[0];
        if (fk == 1) for (int k = 1; k < na; k++) { if (v[k] < x) x = v[k]; }
        else if (fk == 2) for (int k = 1; k < na; k++) { if (v[k] > x) x = v[k]; }
        else if (na == 3) { x = v[1]; if (x < v[0]) x = v[0]; if (x > v[2]) x = v[2]; }
        r.px = x; r.pct = 0; r.ok = 1;
        return r;
    }
    if (n - i > 4 && !strncasecmp(s + i, "var(", 4)) { *pe = n; return r; }   /* (var non remplace : rien) */
    const char *e;
    f32 x = css_num(s + i, &e);
    if (!e) { *pe = i; return r; }
    int k = (int)(e - s);
    const char *u = s + k;
    int ul = 0;
    while (k + ul < n && (isalpha((u8)u[ul]) || u[ul] == '%')) ul++;
#define U(t) (ul == (int)strlen(t) && !strncasecmp(u, t, ul))
    r.ok = 1;
    if (ul == 0 || U("px")) r.px = x;
    else if (U("%")) r.pct = x;
    else if (U("em")) r.px = x * ws_cur_fs;
    else if (U("rem")) r.px = x * ws_root_fs;
    else if (U("ex") || U("ch") || U("cap")) r.px = x * ws_cur_fs * 0.5f;
    else if (U("pt")) r.px = x * 4.0f / 3.0f;
    else if (U("pc")) r.px = x * 16;
    else if (U("in")) r.px = x * 96;
    else if (U("cm")) r.px = x * 37.8f;
    else if (U("mm")) r.px = x * 3.78f;
    else if (U("q")) r.px = x * 0.945f;
    else if (U("vw") || U("svw") || U("lvw") || U("dvw") || U("vi")) r.px = x * WS_VW * 0.01f;
    else if (U("vh") || U("svh") || U("lvh") || U("dvh") || U("vb")) r.px = x * WS_VH * 0.01f;
    else if (U("vmin")) r.px = x * WS_VH * 0.01f;
    else if (U("vmax")) r.px = x * WS_VW * 0.01f;
    else if (U("fr")) r.px = x;
    else if (U("lh") || U("rlh")) r.px = x * ws_cur_fs * 1.2f;
    else r.ok = 0;
#undef U
    *pe = k + ul;
    return r;
}
/* somme / produit : a + b - c * 2 / 3 (lineaire en px et %) */
static wnum_t ws_expr(const char *s, int n, int *pe) {
    int i = 0;
    wnum_t acc = { 0, 0, 1 };
    int sign = 1, first = 1;
    while (i < n) {
        while (i < n && s[i] == ' ') i++;
        if (i >= n || s[i] == ',' || s[i] == ')') break;
        if (!first) {
            if (s[i] == '+') { sign = 1; i++; }
            else if (s[i] == '-' && i + 1 < n && s[i + 1] == ' ') { sign = -1; i++; }
            else break;
        }
        int e = 0;
        wnum_t t = ws_atom(s + i, n - i, &e);
        if (!t.ok) { acc.ok = 0; *pe = i + e; return acc; }
        i += e;
        for (;;) {                              /* * et / */
            int j = i;
            while (j < n && s[j] == ' ') j++;
            if (j < n && (s[j] == '*' || s[j] == '/')) {
                char op = s[j];
                int e2 = 0;
                wnum_t f = ws_atom(s + j + 1, n - j - 1, &e2);
                if (!f.ok) { acc.ok = 0; *pe = j; return acc; }
                if (op == '*') {
                    f32 k = (f.px != 0 && f.pct == 0 && t.pct == 0 && t.px != 0) ? 0 : 0;
                    (void)k;
                    if (t.pct == 0 && f.pct == 0) { /* nombre * longueur */ t.px = t.px * f.px; }
                    else if (f.pct == 0) { t.px *= f.px; t.pct *= f.px; }
                    else { f.px *= t.px; f.pct *= t.px; t = f; }
                } else if (f.px != 0) { t.px /= f.px; t.pct /= f.px; }
                i = j + 1 + e2;
            } else break;
        }
        acc.px += sign * t.px; acc.pct += sign * t.pct;
        first = 0;
    }
    *pe = i;
    return acc;
}
/* longueur ; 0 si pas comprise */
static int ws_len(const char *v, int n, wl_t *out) {
    while (n > 0 && *v == ' ') { v++; n--; }
    while (n > 0 && v[n - 1] == ' ') n--;
    if (n <= 0) return 0;
    if (css_word_is(v, n, "auto")) { *out = WL_AUTO; return 1; }
    if (css_word_is(v, n, "none")) { *out = WL_NONE; return 1; }
    if (css_word_is(v, n, "fit-content") || css_word_is(v, n, "max-content") || css_word_is(v, n, "min-content") || css_word_is(v, n, "-webkit-fill-available") || css_word_is(v, n, "stretch")) {
        *out = css_word_is(v, n, "max-content") || css_word_is(v, n, "fit-content") ? (wl_t){ 0, 0, 3 } : css_word_is(v, n, "min-content") ? (wl_t){ 0, 0, 4 } : WL_AUTO;
        return 1;
    }
    if (css_word_is(v, n, "inherit") || css_word_is(v, n, "initial") || css_word_is(v, n, "unset") || css_word_is(v, n, "revert")) return 0;
    int e = 0;
    wnum_t r = ws_expr(v, n, &e);
    if (!r.ok || e < n) return 0;
    out->px = r.px; out->pct = r.pct; out->k = 0;
    return 1;
}
/* valeurs separees par des espaces (en sautant les parentheses) -> debuts / longueurs */
static int ws_split(const char *v, int n, int *st, int *ln, int max) {
    int i = 0, k = 0;
    while (i < n && k < max) {
        while (i < n && (v[i] == ' ' || v[i] == ',')) i++;
        if (i >= n) break;
        int s = i, d = 0;
        while (i < n && ((v[i] != ' ' && v[i] != ',') || d)) { if (v[i] == '(') d++; else if (v[i] == ')') d--; i++; }
        st[k] = s; ln[k] = i - s; k++;
    }
    return k;
}
static int ws_split_sp(const char *v, int n, int *st, int *ln, int max) {    /* espaces seulement */
    int i = 0, k = 0;
    while (i < n && k < max) {
        while (i < n && v[i] == ' ') i++;
        if (i >= n) break;
        int s = i, d = 0;
        while (i < n && (v[i] != ' ' || d)) { if (v[i] == '(') d++; else if (v[i] == ')') d--; i++; }
        st[k] = s; ln[k] = i - s; k++;
    }
    return k;
}
/* taille du texte */
static int ws_fsize(const char *v, int n, f32 parent, f32 *out) {
    static const struct { const char *n; f32 px; } kw[] = { { "xx-small", 9 }, { "x-small", 10 }, { "small", 13 }, { "medium", 16 },
        { "large", 18 }, { "x-large", 24 }, { "xx-large", 32 }, { "xxx-large", 48 } };
    for (unsigned i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) if (css_word_is(v, n, kw[i].n)) { *out = kw[i].px; return 1; }
    if (css_word_is(v, n, "smaller")) { *out = parent * 0.83f; return 1; }
    if (css_word_is(v, n, "larger")) { *out = parent * 1.2f; return 1; }
    f32 save = ws_cur_fs, save_cb = ws_cur_cb;
    ws_cur_fs = parent; ws_cur_cb = parent * 100;   /* (% et em : par rapport au parent) */
    wl_t l;
    int ok = ws_len(v, n, &l);
    ws_cur_fs = save; ws_cur_cb = save_cb;
    if (!ok || l.k) return 0;
    f32 x = l.px + l.pct * parent * 0.01f;
    if (x <= 0) return 0;
    if (x > 120) x = 120;
    *out = x;
    return 1;
}
static int ws_is_mono(const char *v, int n) {
    int st[8], ln[8], k = ws_split(v, n, st, ln, 8);
    for (int i = 0; i < k; i++) {
        const char *w = v + st[i]; int l = ln[i];
        if (l >= 2 && (w[0] == '"' || w[0] == '\'')) { w++; l -= 2; }
        if (css_has(w, l, "mono") || css_has(w, l, "courier") || css_has(w, l, "consolas") || css_has(w, l, "menlo") || css_has(w, l, "code")) return 1;
        if (css_word_is(w, l, "serif") || css_word_is(w, l, "sans-serif") || css_has(w, l, "arial") || css_has(w, l, "helvetica") || css_has(w, l, "georgia") || css_has(w, l, "system-ui")) return 0;
    }
    return 0;
}
static u16 ws_weight(const char *v, int n, u16 parent) {
    if (css_word_is(v, n, "bold") || css_word_is(v, n, "bolder")) return css_word_is(v, n, "bolder") && parent >= 600 ? 900 : 700;
    if (css_word_is(v, n, "normal")) return 400;
    if (css_word_is(v, n, "lighter")) return parent >= 600 ? 400 : 100;
    const char *e; f32 x = css_num(v, &e);
    if (e && x >= 1 && x <= 1000) return (u16)x;
    return 0;
}

/* ---------- application d'une declaration ---------- */
static int ws_border_side(const char *v, int n, f32 *w, u32 *c, u8 *st) {   /* "1px solid #ccc" */
    if (css_word_is(v, n, "none") || css_word_is(v, n, "0") || css_word_is(v, n, "hidden")) { *w = 0; *st = 0; return 1; }
    int ss[6], sl[6], k = ws_split_sp(v, n, ss, sl, 6), got = 0;
    f32 ww = 3; u32 cc = 1; u8 s = 1;           /* (cc 1 : couleur du texte) */
    for (int i = 0; i < k; i++) {
        const char *x = v + ss[i]; int l = sl[i];
        u32 col; wl_t len;
        if (css_word_is(x, l, "solid") || css_word_is(x, l, "dashed") || css_word_is(x, l, "dotted") || css_word_is(x, l, "double") ||
            css_word_is(x, l, "groove") || css_word_is(x, l, "ridge") || css_word_is(x, l, "inset") || css_word_is(x, l, "outset")) { s = 1; got = 1; }
        else if (css_word_is(x, l, "none") || css_word_is(x, l, "hidden")) { s = 0; got = 1; }
        else if (css_word_is(x, l, "thin")) { ww = 1; got = 1; }
        else if (css_word_is(x, l, "medium")) { ww = 3; got = 1; }
        else if (css_word_is(x, l, "thick")) { ww = 5; got = 1; }
        else if (css_word_is(x, l, "currentcolor")) { cc = 1; got = 1; }
        else if (css_color(x, l, &col)) { cc = col ? col : 0; got = 1; }
        else if (ws_len(x, l, &len) && !len.k) { ww = len.px; got = 1; }
    }
    if (!got) return 0;
    *w = ww; *c = cc; *st = s;
    return 1;
}
static void ws_box4(const char *v, int n, wl_t *out, int allow_neg) {    /* margin / padding : 1 a 4 valeurs */
    int st[4], ln[4], k = ws_split_sp(v, n, st, ln, 4);
    wl_t l[4];
    for (int i = 0; i < k; i++) if (!ws_len(v + st[i], ln[i], &l[i])) return;
    if (!k) return;
    if (k == 1) l[1] = l[2] = l[3] = l[0];
    else if (k == 2) { l[2] = l[0]; l[3] = l[1]; }
    else if (k == 3) l[3] = l[1];
    for (int i = 0; i < 4; i++) { if (!allow_neg && (l[i].px < 0 || l[i].k == 1)) l[i] = WL_ZERO; out[i] = l[i]; }
}
static int ws_display(const char *v, int n) {
    int st[3], ln[3], k = ws_split_sp(v, n, st, ln, 3);
    int outer_inline = 0, inner = -1;
    for (int i = 0; i < k; i++) {
        const char *x = v + st[i]; int l = ln[i];
        if (css_word_is(x, l, "none")) return DSP_NONE;
        if (css_word_is(x, l, "contents")) return DSP_CONTENTS;
        if (css_word_is(x, l, "inline")) outer_inline = 1;
        else if (css_word_is(x, l, "block") || css_word_is(x, l, "flow-root") || css_word_is(x, l, "flow") || css_word_is(x, l, "run-in")) { if (inner < 0) inner = 0; }
        else if (css_word_is(x, l, "inline-block")) { outer_inline = 1; inner = 0; }
        else if (css_word_is(x, l, "list-item")) inner = 9;
        else if (css_word_is(x, l, "table")) inner = 1;
        else if (css_word_is(x, l, "inline-table")) { outer_inline = 1; inner = 1; }
        else if (css_word_is(x, l, "flex") || css_word_is(x, l, "-webkit-box") || css_word_is(x, l, "-webkit-flex") || css_word_is(x, l, "-ms-flexbox")) inner = 2;
        else if (css_word_is(x, l, "inline-flex") || css_word_is(x, l, "-webkit-inline-box") || css_word_is(x, l, "-webkit-inline-flex")) { outer_inline = 1; inner = 2; }
        else if (css_word_is(x, l, "grid")) inner = 3;
        else if (css_word_is(x, l, "inline-grid")) { outer_inline = 1; inner = 3; }
        else if (css_word_is(x, l, "table-row-group") || css_word_is(x, l, "table-header-group") || css_word_is(x, l, "table-footer-group")) return DSP_ROWGRP;
        else if (css_word_is(x, l, "table-row")) return DSP_ROW;
        else if (css_word_is(x, l, "table-cell")) return DSP_CELL;
        else if (css_word_is(x, l, "table-caption")) return DSP_CAPTION;
        else if (css_word_is(x, l, "table-column") || css_word_is(x, l, "table-column-group")) return DSP_COL;
        else return -1;
    }
    if (inner == 9) return DSP_LIST;
    if (inner == 1) return outer_inline ? DSP_ITABLE : DSP_TABLE;
    if (inner == 2) return outer_inline ? DSP_IFLEX : DSP_FLEX;
    if (inner == 3) return outer_inline ? DSP_IGRID : DSP_GRID;
    if (inner == 0) return outer_inline ? DSP_IBLOCK : DSP_BLOCK;
    if (outer_inline) return DSP_INLINE;
    return -1;
}
static int ws_list_type(const char *x, int l) {
    if (css_word_is(x, l, "none")) return LT_NONE;
    if (css_word_is(x, l, "disc")) return LT_DISC;
    if (css_word_is(x, l, "circle")) return LT_CIRCLE;
    if (css_word_is(x, l, "square")) return LT_SQUARE;
    if (css_word_is(x, l, "decimal")) return LT_DEC;
    if (css_word_is(x, l, "decimal-leading-zero")) return LT_DEC0;
    if (css_word_is(x, l, "lower-alpha") || css_word_is(x, l, "lower-latin")) return LT_LALPHA;
    if (css_word_is(x, l, "upper-alpha") || css_word_is(x, l, "upper-latin")) return LT_UALPHA;
    if (css_word_is(x, l, "lower-roman")) return LT_LROMAN;
    if (css_word_is(x, l, "upper-roman")) return LT_UROMAN;
    if (l > 0 && (x[0] == '"' || x[0] == '\'')) return LT_DISC;
    return -1;
}

/* proprietes personnalisees (--x) : pile pendant le parcours de l'arbre */
#define WV_MAX 4096
static struct { u32 name; u32 v0; u16 vn; } wvars[WV_MAX];
static int nwvars = 0;
static const char *ws_var_get(u32 h, int *vn) {
    for (int i = nwvars - 1; i >= 0; i--) if (wvars[i].name == h) { *vn = wvars[i].vn; return ws.txt + wvars[i].v0; }
    return NULL;
}
/* remplace var(--x, defaut) dans v -> out ; 0 si une variable manque sans defaut */
static int ws_subst(const char *v, int n, char *out, int cap, int depth) {
    int o = 0, i = 0;
    while (i < n && o < cap - 1) {
        if (i + 4 < n && !strncasecmp(v + i, "var(", 4)) {
            int j = i + 4, d = 1;
            while (j < n && d) { if (v[j] == '(') d++; else if (v[j] == ')') d--; if (d) j++; }
            /* var( nom , defaut ) */
            int a = i + 4;
            while (a < j && v[a] == ' ') a++;
            int ne = a;
            while (ne < j && v[ne] != ',' && v[ne] != ' ' && v[ne] != ')') ne++;
            int vn = 0;
            const char *val = ws_var_get(css_hash(v + a, ne - a), &vn);
            if (!val) {
                int c = ne; while (c < j && v[c] != ',') c++;
                if (c >= j) return 0;
                val = v + c + 1; vn = j - c - 1;
            }
            if (depth < 6) {
                int k = ws_subst(val, vn, out + o, cap - o, depth + 1);
                if (!k && vn) return 0;
                o += (int)strlen(out + o);
            }
            i = j + 1;
            continue;
        }
        out[o++] = v[i++];
    }
    out[o] = 0;
    return 1;
}

static void ws_apply(wcs_t *c, const wcs_t *par, int prop, const char *v, int n, int pass) {
    /* passe 0 : la taille du texte seulement (les em en dependent) ; passe 1 : le reste */
    if (n <= 0) return;
    int inh = css_word_is(v, n, "inherit"), ini = css_word_is(v, n, "initial") || css_word_is(v, n, "unset") || css_word_is(v, n, "revert") || css_word_is(v, n, "revert-layer");
    if (pass == 0) {
        if (prop == WP_FONT_SIZE) {
            if (inh || ini) { c->fsize = par ? par->fsize : 16; return; }
            f32 x; if (ws_fsize(v, n, par ? par->fsize : 16, &x)) c->fsize = x;
        } else if (prop == WP_FONT) {
            int st[10], ln[10], k = ws_split_sp(v, n, st, ln, 10);
            for (int i = 0; i < k; i++) {
                const char *x = v + st[i]; int l = ln[i];
                const char *sl = (const char *)memchr(x, '/', l);
                int l2 = sl ? (int)(sl - x) : l;
                f32 fs;
                if (l2 > 0 && (isdigit((u8)x[0]) || x[0] == '.') && !(l2 == 3 && x[1] == '0' && x[2] == '0') && ws_fsize(x, l2, par ? par->fsize : 16, &fs)) { c->fsize = fs; break; }
                if (css_word_is(x, l2, "small") || css_word_is(x, l2, "large") || css_word_is(x, l2, "medium") || css_word_is(x, l2, "x-large") || css_word_is(x, l2, "smaller") || css_word_is(x, l2, "larger")) { if (ws_fsize(x, l2, par ? par->fsize : 16, &fs)) c->fsize = fs; break; }
            }
        }
        return;
    }
    if (inh && par) {
        switch (prop) {
        case WP_COLOR: c->color = par->color; return;
        case WP_BG: c->bg = par->bg; return;
        case WP_DISPLAY: c->display = par->display; return;
        case WP_TEXT_ALIGN: c->align = par->align; return;
        case WP_WHITE_SPACE: c->ws = par->ws; return;
        case WP_FONT_WEIGHT: c->weight = par->weight; return;
        case WP_WIDTH: c->w = par->w; return;
        case WP_MAX_W: c->maxw = par->maxw; return;
        case WP_LINE_HEIGHT: c->lh = par->lh; return;
        case WP_TEXT_DECO: c->deco = par->deco; return;
        case WP_VISIBILITY: c->hide = par->hide; return;
        default: return;
        }
    }
    u32 col; wl_t l; f32 x;
    int st[8], ln[8], k;
    switch (prop) {
    case WP_DISPLAY: { if (ini) { c->display = DSP_INLINE; return; } int d = ws_display(v, n); if (d >= 0) c->display = (u8)d; return; }
    case WP_POSITION:
        c->position = css_word_is(v, n, "absolute") ? POS_ABS : css_word_is(v, n, "fixed") ? POS_FIXED : css_word_is(v, n, "relative") ? POS_REL :
                      css_has(v, n, "sticky") ? POS_STICKY : POS_STATIC;
        return;
    case WP_FLOAT: c->flt = css_word_is(v, n, "left") || css_word_is(v, n, "inline-start") ? 1 : css_word_is(v, n, "right") || css_word_is(v, n, "inline-end") ? 2 : 0; return;
    case WP_CLEAR: c->clear = css_word_is(v, n, "left") ? 1 : css_word_is(v, n, "right") ? 2 : (css_word_is(v, n, "both") || css_word_is(v, n, "all")) ? 3 : 0; return;
    case WP_VISIBILITY: c->hide = (c->hide & ~1) | (css_word_is(v, n, "hidden") || css_word_is(v, n, "collapse")); return;
    case WP_OPACITY: { const char *e; x = css_num(v, &e); if (e && x < 0.05f && (*e != '%' || x < 5)) c->hide |= 2; else c->hide &= ~2; return; }
    case WP_CLIP: if (css_has(v, n, "rect(0") || css_has(v, n, "rect(1px") || css_has(v, n, "rect(0px")) c->hide |= 4; return;
    case WP_CLIP_PATH: if (css_has(v, n, "inset(50%") || css_has(v, n, "inset(100%") || css_has(v, n, "circle(0")) c->hide |= 4; return;
    case WP_LEFT: case WP_TOP: case WP_INSET: { const char *e; x = css_num(v, &e); if (e && x <= -999) c->hide |= 8; return; }
    case WP_COLOR:
        if (ini) { c->color = 0x202122ffu; return; }
        if (css_word_is(v, n, "currentcolor")) return;
        if (css_color(v, n, &col)) c->color = col ? col : 0;   /* (transparent : 0 -> texte invisible) */
        return;
    case WP_BG: {
        if (ini || css_word_is(v, n, "none") || css_word_is(v, n, "transparent")) { c->bg = 0; return; }
        if (css_color(v, n, &col)) { c->bg = col; return; }
        k = ws_split(v, n, st, ln, 8);
        for (int i = k - 1; i >= 0; i--) {              /* raccourci : la couleur est a la fin */
            if (css_color(v + st[i], ln[i], &col)) { c->bg = col; return; }
            if (ln[i] > 16 && (!strncasecmp(v + st[i], "linear-gradient(", 16) || !strncasecmp(v + st[i], "radial-gradient(", 16))) {
                /* degrade : sa premiere couleur */
                const char *q = (const char *)memchr(v + st[i], '(', ln[i]) + 1;
                int qn = (int)(v + st[i] + ln[i] - q), s2[10], l2[10], k2 = ws_split(q, qn, s2, l2, 10);
                for (int j = 0; j < k2; j++) { int ll = l2[j]; if (ll > 0 && q[s2[j] + ll - 1] == ')' && !memchr(q + s2[j], '(', ll)) ll--; if (css_color(q + s2[j], ll, &col)) { c->bg = col; return; } }
            }
        }
        return;
    }
    case WP_FONT_WEIGHT: { if (ini) { c->weight = 400; return; } u16 w = ws_weight(v, n, par ? par->weight : 400); if (w) c->weight = w; return; }
    case WP_FONT_STYLE: c->italic = css_has(v, n, "italic") || css_has(v, n, "oblique"); return;
    case WP_FONT_FAMILY: c->mono = (u8)ws_is_mono(v, n); return;
    case WP_FONT:
        k = ws_split_sp(v, n, st, ln, 8);
        c->italic = 0; c->weight = 400;
        for (int i = 0; i < k; i++) {
            const char *xx = v + st[i]; int ll = ln[i];
            if (css_word_is(xx, ll, "italic") || css_word_is(xx, ll, "oblique")) c->italic = 1;
            else { u16 w = ws_weight(xx, ll, par ? par->weight : 400); if (w && !(ll > 3 && isdigit((u8)xx[0]) && !(ll == 3))) { if (!(isdigit((u8)xx[0]) && i == k - 2)) c->weight = w; } }
            const char *sl = (const char *)memchr(xx, '/', ll);
            if (sl) {                                  /* taille/hauteur de ligne */
                const char *lv = sl + 1; int lvn = (int)(xx + ll - lv);
                const char *e; f32 y = css_num(lv, &e);
                if (e && e == lv + lvn && y > 0) c->lh = -y;
                else if (ws_len(lv, lvn, &l) && !l.k) c->lh = l.px + l.pct * c->fsize * 0.01f;
            }
        }
        if (k > 0) c->mono = (u8)ws_is_mono(v + st[k - 1], (int)(v + n - (v + st[k - 1])));
        return;
    case WP_LINE_HEIGHT: {
        if (css_word_is(v, n, "normal") || ini) { c->lh = 0; return; }
        const char *e; f32 y = css_num(v, &e);
        if (e && e == v + n && y > 0) { c->lh = -y; return; }
        if (ws_len(v, n, &l) && !l.k) c->lh = l.px + l.pct * c->fsize * 0.01f;
        return;
    }
    case WP_TEXT_ALIGN:
        c->align = css_has(v, n, "center") ? 2 : (css_has(v, n, "right") || css_word_is(v, n, "end")) ? 3 : css_has(v, n, "justify") ? 4 : 1;
        return;
    case WP_TEXT_DECO: c->deco = css_has(v, n, "underline") ? 1 : css_has(v, n, "line-through") ? 2 : 0; return;
    case WP_TEXT_TRANSFORM: c->transform = css_has(v, n, "upper") ? 1 : css_has(v, n, "lower") ? 2 : css_has(v, n, "capitalize") ? 3 : 0; return;
    case WP_WHITE_SPACE:
        c->ws = css_word_is(v, n, "nowrap") ? WSP_NOWRAP : css_word_is(v, n, "pre") || css_word_is(v, n, "preserve") || css_word_is(v, n, "break-spaces") ? WSP_PRE :
                css_word_is(v, n, "pre-wrap") ? WSP_PREWRAP : css_word_is(v, n, "pre-line") ? WSP_PRELINE : WSP_NORMAL;
        return;
    case WP_VALIGN:
        c->valign = css_word_is(v, n, "sub") ? VA_SUB : css_word_is(v, n, "super") ? VA_SUPER : css_word_is(v, n, "middle") ? VA_MIDDLE :
                    css_word_is(v, n, "top") ? VA_TOP : css_word_is(v, n, "bottom") ? VA_BOTTOM : css_word_is(v, n, "text-top") ? VA_TTOP :
                    css_word_is(v, n, "text-bottom") ? VA_TBOT : VA_BASE;
        return;
    case WP_LIST_TYPE: { int t = ws_list_type(v, n); if (t >= 0) c->list = (u8)t; return; }
    case WP_LIST_STYLE:
        k = ws_split_sp(v, n, st, ln, 4);
        for (int i = 0; i < k; i++) {
            int t = ws_list_type(v + st[i], ln[i]);
            if (t >= 0) c->list = (u8)t;
            if (css_word_is(v + st[i], ln[i], "inside")) c->list_inside = 1;
            if (css_word_is(v + st[i], ln[i], "outside")) c->list_inside = 0;
        }
        return;
    case WP_TEXT_INDENT: if (ws_len(v, n, &l) && !l.k) { c->tindent = l; if (l.px <= -999) c->hide |= 8; } return;
    case WP_WIDTH: if (ws_len(v, n, &l)) c->w = l.k == 2 ? WL_AUTO : l; return;
    case WP_HEIGHT: if (ws_len(v, n, &l)) c->h = l.k == 2 ? WL_AUTO : l; return;
    case WP_MIN_W: if (ws_len(v, n, &l)) c->minw = l.k ? WL_ZERO : l; return;
    case WP_MAX_W: if (ws_len(v, n, &l)) c->maxw = l.k == 1 ? WL_NONE : l; return;
    case WP_MIN_H: if (ws_len(v, n, &l)) c->minh = l.k ? WL_ZERO : l; return;
    case WP_MAX_H: if (ws_len(v, n, &l)) c->maxh = l.k == 1 ? WL_NONE : l; return;
    case WP_MARGIN: ws_box4(v, n, c->m, 1); return;
    case WP_PADDING: ws_box4(v, n, c->p, 0); return;
    case WP_MARGIN_INLINE: case WP_PADDING_INLINE: case WP_MARGIN_BLOCK: case WP_PADDING_BLOCK: {
        wl_t b4[4] = { WL_ZERO, WL_ZERO, WL_ZERO, WL_ZERO };
        k = ws_split_sp(v, n, st, ln, 2);
        if (!k || !ws_len(v + st[0], ln[0], &b4[0])) return;
        b4[1] = b4[0];
        if (k > 1 && !ws_len(v + st[1], ln[1], &b4[1])) return;
        wl_t *dst = (prop == WP_MARGIN_INLINE || prop == WP_MARGIN_BLOCK) ? c->m : c->p;
        if (prop == WP_MARGIN_INLINE || prop == WP_PADDING_INLINE) { dst[3] = b4[0]; dst[1] = b4[1]; } else { dst[0] = b4[0]; dst[2] = b4[1]; }
        return;
    }
    case WP_MT: case WP_MR: case WP_MB: case WP_ML: if (ws_len(v, n, &l)) c->m[prop - WP_MT] = l; return;
    case WP_PT: case WP_PR: case WP_PB: case WP_PL: if (ws_len(v, n, &l) && !l.k && l.px >= 0) c->p[prop - WP_PT] = l; return;
    case WP_BORDER: case WP_BT: case WP_BR: case WP_BB: case WP_BL: {
        f32 w; u32 cc; u8 s;
        if (!ws_border_side(v, n, &w, &cc, &s)) return;
        for (int i = 0; i < 4; i++) if (prop == WP_BORDER || prop - WP_BT == i) { c->bw[i] = w; c->bc[i] = cc; c->bst[i] = s; }
        return;
    }
    case WP_BW: case WP_BTW: case WP_BRW: case WP_BBW: case WP_BLW: {
        k = ws_split_sp(v, n, st, ln, 4);
        f32 w4[4];
        for (int i = 0; i < k; i++) {
            const char *xx = v + st[i]; int ll = ln[i];
            w4[i] = css_word_is(xx, ll, "thin") ? 1 : css_word_is(xx, ll, "medium") ? 3 : css_word_is(xx, ll, "thick") ? 5 : (ws_len(xx, ll, &l) && !l.k) ? l.px : -1;
            if (w4[i] < 0) return;
        }
        if (!k) return;
        if (prop != WP_BW) { c->bw[prop - WP_BTW] = w4[0]; return; }
        if (k == 1) w4[1] = w4[2] = w4[3] = w4[0]; else if (k == 2) { w4[2] = w4[0]; w4[3] = w4[1]; } else if (k == 3) w4[3] = w4[1];
        for (int i = 0; i < 4; i++) c->bw[i] = w4[i];
        return;
    }
    case WP_BC: case WP_BTC: case WP_BRC: case WP_BBC: case WP_BLC: {
        k = ws_split(v, n, st, ln, 4);
        u32 c4[4];
        for (int i = 0; i < k; i++) { if (css_word_is(v + st[i], ln[i], "currentcolor")) c4[i] = 1; else if (css_color(v + st[i], ln[i], &col)) c4[i] = col; else return; }
        if (!k) return;
        if (prop != WP_BC) { c->bc[prop - WP_BTC] = c4[0]; return; }
        if (k == 1) c4[1] = c4[2] = c4[3] = c4[0]; else if (k == 2) { c4[2] = c4[0]; c4[3] = c4[1]; } else if (k == 3) c4[3] = c4[1];
        for (int i = 0; i < 4; i++) c->bc[i] = c4[i];
        return;
    }
    case WP_BS: case WP_BTS: case WP_BRS: case WP_BBS: case WP_BLS: {
        k = ws_split_sp(v, n, st, ln, 4);
        u8 s4[4];
        for (int i = 0; i < k; i++) s4[i] = !(css_word_is(v + st[i], ln[i], "none") || css_word_is(v + st[i], ln[i], "hidden"));
        if (!k) return;
        if (prop != WP_BS) { c->bst[prop - WP_BTS] = s4[0]; return; }
        if (k == 1) s4[1] = s4[2] = s4[3] = s4[0]; else if (k == 2) { s4[2] = s4[0]; s4[3] = s4[1]; } else if (k == 3) s4[3] = s4[1];
        for (int i = 0; i < 4; i++) c->bst[i] = s4[i];
        return;
    }
    case WP_BOX_SIZING: c->boxsz = css_has(v, n, "border"); return;
    case WP_OVERFLOW: c->ovf = !(css_word_is(v, n, "visible") || css_word_is(v, n, "clip")); return;
    case WP_FLEX_DIR: c->fdir = css_has(v, n, "column") ? 1 : 0; return;
    case WP_FLEX_WRAP: c->fwrap = css_has(v, n, "wrap") && !css_has(v, n, "nowrap"); return;
    case WP_FLEX_FLOW: c->fdir = css_has(v, n, "column") ? 1 : 0; c->fwrap = css_has(v, n, "wrap") && !css_has(v, n, "nowrap"); return;
    case WP_FLEX: {
        if (css_word_is(v, n, "none")) { c->fgrow = 0; c->fshrink = 0; c->fbasis = WL_AUTO; return; }
        if (css_word_is(v, n, "auto")) { c->fgrow = 1; c->fshrink = 1; c->fbasis = WL_AUTO; return; }
        k = ws_split_sp(v, n, st, ln, 3);
        int nn = 0;
        c->fbasis = WL_ZERO; c->fshrink = 1;
        for (int i = 0; i < k; i++) {
            const char *e; f32 y = css_num(v + st[i], &e);
            if (e && e == v + st[i] + ln[i]) { if (nn == 0) c->fgrow = y; else c->fshrink = y; nn++; }
            else if (ws_len(v + st[i], ln[i], &l)) c->fbasis = l.k == 3 ? WL_AUTO : l;
        }
        return;
    }
    case WP_FLEX_GROW: { const char *e; x = css_num(v, &e); if (e) c->fgrow = x; return; }
    case WP_FLEX_SHRINK: { const char *e; x = css_num(v, &e); if (e) c->fshrink = x; return; }
    case WP_FLEX_BASIS: if (ws_len(v, n, &l)) c->fbasis = (l.k == 3 || l.k == 2) ? WL_AUTO : l; return;
    case WP_JUSTIFY:
        c->justify = css_has(v, n, "center") ? JC_CENTER : css_has(v, n, "between") ? JC_BETWEEN : css_has(v, n, "around") ? JC_AROUND :
                     css_has(v, n, "evenly") ? JC_EVENLY : (css_has(v, n, "end") || css_has(v, n, "right")) ? JC_END : JC_START;
        return;
    case WP_ALIGN_ITEMS: case WP_ALIGN_SELF: {
        int a = css_has(v, n, "center") ? AI_CENTER : (css_has(v, n, "end") || css_has(v, n, "bottom")) ? AI_END : (css_has(v, n, "start") || css_has(v, n, "top")) ? AI_START :
                css_has(v, n, "baseline") ? AI_BASE : css_word_is(v, n, "auto") ? AI_AUTO : AI_STRETCH;
        if (prop == WP_ALIGN_ITEMS) c->aitems = (u8)(a == AI_AUTO ? AI_STRETCH : a); else c->aself = (u8)a;
        return;
    }
    case WP_GAP: case WP_ROW_GAP: case WP_COL_GAP:
        k = ws_split_sp(v, n, st, ln, 2);
        if (!k) return;
        if (!ws_len(v + st[0], ln[0], &l) || l.k) { if (css_word_is(v + st[0], ln[0], "normal")) l = WL_ZERO; else return; }
        if (prop == WP_GAP) {
            c->gap_r = l.px;
            if (k > 1 && ws_len(v + st[1], ln[1], &l) && !l.k) {}
            c->gap_c = l.px + l.pct * 6;
        } else if (prop == WP_ROW_GAP) c->gap_r = l.px; else c->gap_c = l.px + l.pct * 6;
        return;
    case WP_GRID_COLS: {
        /* repeat(3, 1fr) ; 1fr 2fr ; 200px 1fr ; repeat(auto-fill, minmax(200px, 1fr)) */
        c->gcols = 0;
        if (css_word_is(v, n, "none")) return;
        int i = 0;
        while (i < n && c->gcols < 12) {
            while (i < n && v[i] == ' ') i++;
            if (i >= n) break;
            int s = i, d = 0;
            while (i < n && (v[i] != ' ' || d)) { if (v[i] == '(') d++; else if (v[i] == ')') d--; i++; }
            const char *t = v + s; int tl = i - s;
            if (tl > 7 && !strncasecmp(t, "repeat(", 7)) {
                const char *cm = (const char *)memchr(t, ',', tl);
                if (!cm) continue;
                int cnt = atoi(t + 7);
                const char *inner = cm + 1; int inl = (int)(t + tl - 1 - inner);
                while (inl > 0 && *inner == ' ') { inner++; inl--; }
                f32 minpx = 0;
                if (cnt <= 0) {                     /* auto-fill / auto-fit : autant que la largeur permet */
                    const char *mm = inl > 7 && !strncasecmp(inner, "minmax(", 7) ? inner + 7 : inner;
                    wl_t ml; int ee = 0; (void)ee;
                    const char *cm2 = (const char *)memchr(mm, ',', (int)(inner + inl - mm));
                    if (ws_len(mm, cm2 ? (int)(cm2 - mm) : (int)(inner + inl - mm), &ml) && !ml.k) minpx = ml.px + ml.pct * 6;
                    if (minpx < 40) minpx = 40;
                    cnt = (int)(WS_VW / minpx);
                    if (cnt < 1) cnt = 1;
                }
                for (int r = 0; r < cnt && c->gcols < 12; r++) c->gfr[c->gcols++] = 1;
                continue;
            }
            const char *e; f32 y = css_num(t, &e);
            if (e && !strncasecmp(e, "fr", 2)) c->gfr[c->gcols++] = y > 0 ? y : 1;
            else if (ws_len(t, tl, &l) && !l.k) c->gfr[c->gcols++] = -(l.px + l.pct * 6 > 1 ? l.px + l.pct * 6 : 1);
            else c->gfr[c->gcols++] = 1;           /* (minmax, auto... : une part) */
        }
        return;
    }
    case WP_GRID_COL: {
        const char *sp = NULL;
        for (int i = 0; i + 4 <= n; i++) if (!strncasecmp(v + i, "span", 4)) { sp = v + i + 4; break; }
        if (sp) c->gspan = (u8)atoi(sp);
        else if (css_has(v, n, "1 / -1") || css_has(v, n, "1/-1")) c->gspan = 99;
        return;
    }
    case WP_ORDER: c->order = (s16)atoi(v); return;
    case WP_TABLE_LAYOUT: c->tlayout = css_word_is(v, n, "fixed"); return;
    case WP_BORDER_COLLAPSE: c->collapse = css_word_is(v, n, "collapse"); return;
    case WP_BORDER_SPACING: if (ws_len(v, n, &l) && !l.k) c->bspacing = l.px; else { k = ws_split_sp(v, n, st, ln, 2); if (k && ws_len(v + st[0], ln[0], &l) && !l.k) c->bspacing = l.px; } return;
    case WP_ASPECT: { const char *e; x = css_num(v, &e); if (e && x > 0) { const char *sl = strchr(v, '/'); f32 y = sl ? css_num(sl + 1, NULL) : 1; if (y > 0) c->aspect = x / y; } return; }
    default: return;
    }
}

/* ---------- correspondance ---------- */
typedef struct { u32 id; u32 cls[8]; u8 ncls; } wel_t;
static wel_t *wels;                             /* un par noeud (elements) */
static void ws_el_info(int k) {
    wel_t *e = &wels[k];
    memset(e, 0, sizeof(*e));
    int vn = 0;
    const char *v = wd_attr(k, "id", &vn);
    if (v && vn) e->id = css_hash(v, vn);
    v = wd_attr(k, "class", &vn);
    if (v) {
        int i = 0;
        while (i < vn && e->ncls < 8) {
            while (i < vn && (v[i] == ' ' || v[i] == '\t' || v[i] == '\n')) i++;
            int s = i;
            while (i < vn && v[i] != ' ' && v[i] != '\t' && v[i] != '\n') i++;
            if (i > s) e->cls[e->ncls++] = css_hash(v + s, i - s);
        }
    }
}
static int ws_elem_prev(int k) { k = wd.n[k].prev; while (k >= 0 && wd.n[k].type != 1) k = wd.n[k].prev; return k; }
static int ws_elem_next(int k) { k = wd.n[k].next; while (k >= 0 && wd.n[k].type != 1) k = wd.n[k].next; return k; }
static int ws_nth_ok(int a, int b, int pos) {   /* pos = a*n + b pour un n >= 0 */
    if (a == 0) return pos == b;
    int d = pos - b;
    if (d % a) return 0;
    return d / a >= 0;
}
static int ws_attr_test(const wtest_t *t, int k) {
    const wnode_t *x = &wd.n[k];
    for (u32 i = 0; i < x->na; i++) {
        const wattr_t *a = &wd.a[x->a0 + i];
        if (a->name != t->name) continue;
        if (t->op == 0) return 1;
        const char *v = wd.txt + a->v0; int vn = (int)a->vn;
        const char *w = ws.txt + t->v0; int wn = t->vn;
        int (*cmp)(const char *, const char *, size_t) = t->icase ? strncasecmp : strncmp;
        switch (t->op) {
        case 1: return vn == wn && !cmp(v, w, wn);
        case 2: { int p = 0; while (p < vn) { while (p < vn && v[p] == ' ') p++; int s = p; while (p < vn && v[p] != ' ') p++; if (p - s == wn && !cmp(v + s, w, wn)) return 1; } return 0; }
        case 3: return wn > 0 && vn >= wn && !cmp(v, w, wn);
        case 4: return wn > 0 && vn >= wn && !cmp(v + vn - wn, w, wn);
        case 5: { if (!wn) return 0; for (int p = 0; p + wn <= vn; p++) if (!cmp(v + p, w, wn)) return 1; return 0; }
        case 6: return (vn == wn && !cmp(v, w, wn)) || (vn > wn && !cmp(v, w, wn) && v[wn] == '-');
        }
        return 0;
    }
    return 0;
}
static int ws_comp_match(const wcomp_t *c, int k, int depth);
static int ws_test(const wtest_t *t, int k, int depth) {
    const wnode_t *x = &wd.n[k];
    if (t->type == 1) return ws_attr_test(t, k);
    if (t->type == 3) {
        int any = 0;
        for (u32 i = 0; i < t->vn && !any; i++) any = ws_comp_match(&ws.c[t->alt[i]], k, depth + 1);
        return t->op ? !any : any;
    }
    switch (t->op) {
    case WPC_ALWAYS: return 1;
    case WPC_NEVER: return 0;
    case WPC_ROOT: return k == 0;
    case WPC_LINK: return (x->tid == WT_A || x->tid == WT_AREA) && wd_attr(k, "href", NULL);
    case WPC_CHECKED: return wd_attr(k, "checked", NULL) || wd_attr(k, "selected", NULL);
    case WPC_DISABLED: return wd_attr(k, "disabled", NULL) != NULL;
    case WPC_EMPTY: { int c = x->first; while (c >= 0) { if (wd.n[c].type == 1 || wd.n[c].tn) return 0; c = wd.n[c].next; } return 1; }
    case WPC_FIRST: return ws_elem_prev(k) < 0;
    case WPC_LAST: return ws_elem_next(k) < 0;
    case WPC_ONLY: return ws_elem_prev(k) < 0 && ws_elem_next(k) < 0;
    case WPC_NTH: return ws_nth_ok(t->a, t->b, x->nidx);
    case WPC_NTHLAST: { int n = 1, q = ws_elem_next(k); while (q >= 0) { n++; q = ws_elem_next(q); } return ws_nth_ok(t->a, t->b, n); }
    case WPC_FTYPE: case WPC_NTHTYPE: case WPC_OTYPE: {
        int n = 1, q = ws_elem_prev(k);
        while (q >= 0) { if (wd.n[q].name == x->name) n++; q = ws_elem_prev(q); }
        if (t->op == WPC_FTYPE) return n == 1;
        if (t->op == WPC_NTHTYPE) return ws_nth_ok(t->a, t->b, n);
        if (n != 1) return 0;
    }   /* (only-of-type : premier et dernier) */
    /* fallthrough */
    case WPC_LTYPE: case WPC_NTHLTYPE: {
        int n = 1, q = ws_elem_next(k);
        while (q >= 0) { if (wd.n[q].name == x->name) n++; q = ws_elem_next(q); }
        if (t->op == WPC_NTHLTYPE) return ws_nth_ok(t->a, t->b, n);
        return n == 1;
    }
    }
    return 0;
}
static int ws_comp_match(const wcomp_t *c, int k, int depth) {
    const wnode_t *x = &wd.n[k];
    if (x->type != 1) return 0;
    if (c->tag && c->tag != x->name) return 0;
    const wel_t *e = &wels[k];
    if (c->id && c->id != e->id) return 0;
    for (int i = 0; i < c->ncls; i++) {
        int f = 0;
        for (int j = 0; j < e->ncls && !f; j++) f = e->cls[j] == c->cls[i];
        if (!f) return 0;
    }
    if (depth > 3) return 1;
    for (u32 i = 0; i < c->nt; i++) if (!ws_test(&ws.t[c->t0 + i], k, depth)) return 0;
    return 1;
}
/* regle complete : composes de droite a gauche avec leurs liens */
static int ws_rule_match_from(const wrule_t *r, int ci, int k) {
    if (!ws_comp_match(&ws.c[r->c0 + ci], k, 0)) return 0;
    if (ci + 1 >= r->nc) return 1;
    int comb = ws.c[r->c0 + ci].comb;
    if (comb == 2) { int p = wd.n[k].parent; return p >= 0 && ws_rule_match_from(r, ci + 1, p); }
    if (comb == 3) { int p = ws_elem_prev(k); return p >= 0 && ws_rule_match_from(r, ci + 1, p); }
    if (comb == 4) { int p = ws_elem_prev(k); int g = 0; while (p >= 0 && g++ < 200) { if (ws_rule_match_from(r, ci + 1, p)) return 1; p = ws_elem_prev(p); } return 0; }
    /* descendant : le premier ancetre qui va (sans revenir en arriere : rapide, presque toujours juste) */
    int p = wd.n[k].parent, g = 0;
    while (p >= 0 && g++ < 64) {
        if (ws_rule_match_from(r, ci + 1, p)) return 1;
        if (ci + 2 >= r->nc) { /* (dernier compose : un seul essai suffit) */ }
        p = wd.n[p].parent;
        if (g > 24 && ci + 2 < r->nc) break;
    }
    return 0;
}

typedef struct { u32 key; s32 rule; } wmatch_t;
#define WM_MAX 512
static wmatch_t wm[WM_MAX];
static int nwm;
static void ws_try(s32 i, int k, int pe) {
    int guard = 0;
    while (i >= 0 && guard++ < 6000) {
        const wrule_t *r = &ws.r[i];
        if (r->pe == pe && nwm < WM_MAX && ws_rule_match_from(r, 0, k)) {
            wm[nwm].key = (u32)r->origin << 31 | (r->spec > 0x3FFFFF ? 0x3FFFFF : r->spec) << 9 | 0;
            wm[nwm].rule = i;
            nwm++;
        }
        i = r->next;
    }
}
static int wm_cmp(const void *a, const void *b) {
    const wmatch_t *x = (const wmatch_t *)a, *y = (const wmatch_t *)b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    u32 ox = ws.r[x->rule].order, oy = ws.r[y->rule].order;
    return ox < oy ? -1 : ox > oy;
}
static int ws_collect(int k, int pe) {
    nwm = 0;
    const wel_t *e = &wels[k];
    if (e->id) ws_try(ws.bucket[ws_bkey(1, e->id)], k, pe);
    for (int i = 0; i < e->ncls; i++) {
        int dup = 0;
        for (int j = 0; j < i && !dup; j++) dup = e->cls[j] == e->cls[i] || ws_bkey(2, e->cls[j]) == ws_bkey(2, e->cls[i]);
        if (!dup) ws_try(ws.bucket[ws_bkey(2, e->cls[i])], k, pe);
    }
    s32 tb = ws.bucket[ws_bkey(3, wd.n[k].name)];
    int dupt = 0;
    if (e->id && ws_bkey(1, e->id) == ws_bkey(3, wd.n[k].name)) dupt = 1;
    for (int i = 0; i < e->ncls && !dupt; i++) dupt = ws_bkey(2, e->cls[i]) == ws_bkey(3, wd.n[k].name);
    if (!dupt) ws_try(tb, k, pe);
    ws_try(ws.any, k, pe);
    /* (cases partagees : une regle peut venir deux fois -> on enleve les doubles) */
    if (nwm > 1) {
        qsort(wm, nwm, sizeof(wmatch_t), wm_cmp);
        int o = 1;
        for (int i = 1; i < nwm; i++) if (wm[i].rule != wm[o - 1].rule) wm[o++] = wm[i];
        nwm = o;
    }
    return nwm;
}

/* valeur d'une declaration (var() remplace) */
static const char *ws_decl_val(const wdecl_t *d, int *vn, char *buf, int cap) {
    const char *v = ws.txt + d->v0;
    *vn = d->vn;
    if (d->vn > 4 && memchr(v, '(', d->vn) && css_has(v, d->vn, "var(")) {
        if (!ws_subst(v, d->vn, buf, cap, 0)) return NULL;
        *vn = (int)strlen(buf);
        return buf;
    }
    return v;
}

/* style initial / herite d'un element */
static void ws_init(wcs_t *c, const wcs_t *par) {
    memset(c, 0, sizeof(*c));
    c->display = DSP_INLINE;
    c->fsize = par ? par->fsize : 16;
    c->weight = par ? par->weight : 400;
    c->italic = par ? par->italic : 0;
    c->mono = par ? par->mono : 0;
    c->lh = par ? par->lh : 0;
    c->align = par ? par->align : 1;
    c->ws = par ? par->ws : WSP_NORMAL;
    c->color = par ? par->color : 0x202122ffu;
    c->list = par ? par->list : LT_DISC;
    c->list_inside = par ? par->list_inside : 0;
    c->transform = par ? par->transform : 0;
    c->tindent = par ? par->tindent : WL_ZERO;
    c->hide = par ? (par->hide & 1) : 0;        /* (visibility s'herite ; le reste non) */
    c->w = c->h = WL_AUTO;
    c->maxw = c->maxh = WL_NONE;
    c->fbasis = WL_AUTO;
    c->fshrink = 1;
    c->aself = AI_AUTO;
    c->aitems = AI_STRETCH;
    c->gap_r = c->gap_c = 0;
}

/* attributs d'autrefois -> style (avant la page, apres le navigateur) */
static void ws_hints(wcs_t *c, int k, int pass) {
    const wnode_t *x = &wd.n[k];
    char v[64];
    u32 col;
    int t = x->tid;
    if (pass == 0) {
        if (t == WT_FONT && wd_attr_s(k, "size", v, sizeof(v))) {
            static const f32 fs[8] = { 10, 13, 16, 18, 24, 32, 48, 48 };
            int sz = atoi(v + (v[0] == '+' || v[0] == '-'));
            if (v[0] == '+') sz = 3 + sz; else if (v[0] == '-') sz = 3 - sz;
            if (sz < 1) sz = 1;
            if (sz > 7) sz = 7;
            c->fsize = fs[sz - 1];
        }
        return;
    }
    if ((t == WT_BODY || t == WT_TABLE || t == WT_TD || t == WT_TH || t == WT_TR) && wd_attr_s(k, "bgcolor", v, sizeof(v)) && css_color(v, (int)strlen(v), &col)) c->bg = col;
    if (t == WT_FONT) {
        if (wd_attr_s(k, "color", v, sizeof(v)) && css_color(v, (int)strlen(v), &col) && col) c->color = col;
        if (wd_attr_s(k, "face", v, sizeof(v))) c->mono = (u8)ws_is_mono(v, (int)strlen(v));
    }
    if (t == WT_BODY && wd_attr_s(k, "text", v, sizeof(v)) && css_color(v, (int)strlen(v), &col) && col) c->color = col;
    if (wd_attr_s(k, "align", v, sizeof(v))) {
        if (t == WT_TABLE || t == WT_IMG) {
            if (!strcasecmp(v, "center") && t == WT_TABLE) { c->m[1] = c->m[3] = WL_AUTO; }
            else if (!strcasecmp(v, "left")) c->flt = 1;
            else if (!strcasecmp(v, "right")) c->flt = 2;
        } else c->align = !strcasecmp(v, "center") || !strcasecmp(v, "middle") ? 2 : !strcasecmp(v, "right") ? 3 : !strcasecmp(v, "justify") ? 4 : 1;
    }
    if ((t == WT_TD || t == WT_TH || t == WT_TR) && wd_attr_s(k, "valign", v, sizeof(v))) c->valign = !strcasecmp(v, "top") ? VA_TOP : !strcasecmp(v, "bottom") ? VA_BOTTOM : VA_MIDDLE;
    if ((t == WT_TD || t == WT_TH) && wd_attr(k, "nowrap", NULL)) c->ws = WSP_NOWRAP;
    if (t == WT_TABLE || t == WT_TD || t == WT_TH || t == WT_IMG || t == WT_HR || t == WT_COL || t == WT_IFRAME || t == WT_EMBED || t == WT_OBJECT || t == WT_CANVAS || t == WT_VIDEO) {
        if (wd_attr_s(k, "width", v, sizeof(v))) { wl_t l; f32 xx = (f32)atof(v); if (strchr(v, '%')) l = (wl_t){ 0, xx, 0 }; else l = (wl_t){ xx, 0, 0 }; if (xx > 0) c->w = l; }
        if (wd_attr_s(k, "height", v, sizeof(v)) && !strchr(v, '%')) { f32 xx = (f32)atof(v); if (xx > 0) c->h = (wl_t){ xx, 0, 0 }; }
    }
    if (t == WT_TABLE) {
        if (wd_attr_s(k, "border", v, sizeof(v)) && atoi(v) > 0) { for (int i = 0; i < 4; i++) { c->bw[i] = 1; c->bc[i] = 0x808080ffu; c->bst[i] = 1; } }
        if (wd_attr_s(k, "cellspacing", v, sizeof(v))) c->bspacing = (f32)atoi(v);
    }
    if (t == WT_IMG && wd_attr_s(k, "hspace", v, sizeof(v))) { f32 xx = (f32)atoi(v); c->m[1] = c->m[3] = (wl_t){ xx, 0, 0 }; }
    if (wd_attr(k, "hidden", NULL)) c->display = DSP_NONE;
}

/* style d'un element : cascade complete ; pe = 0 l'element, 1 ::before, 2 ::after (0 si rien)
   Ordre : navigateur, attributs d'autrefois, page (par specificite puis ordre), style="", puis les !important. */
static int ws_compute(int k, const wcs_t *par, wcs_t *c, int pe) {
    ws_init(c, par);
    if (!pe && (wd.n[k].tid == WT_HTML || wd.n[k].tid == WT_BODY)) c->display = DSP_BLOCK;
    int nm = ws_collect(k, pe);
    if (pe && !nm) return 0;
    int iln = 0;
    const char *il = pe || ws_textmode ? NULL : wd_attr(k, "style", &iln);
    u32 ild0 = 0; int ildn = 0;
    if (il && iln > 0) ildn = ws_parse_decls(il, iln, &ild0);
    int nua = 0;
    while (nua < nm && ws.r[wm[nua].rule].origin == 0) nua++;
    for (int pass = -1; pass < 2; pass++) {
        f32 save_fs = ws_cur_fs;
        if (pass == 1) ws_cur_fs = c->fsize;
        for (int imp = 0; imp < 2; imp++) {
            for (int m = 0; m <= nm; m++) {
                if (!imp && m == nua && pass >= 0 && !pe) ws_hints(c, k, pass);
                u32 d0; int dn;
                if (m < nm) { const wrule_t *r = &ws.r[wm[m].rule]; if (imp && r->origin == 0) continue; d0 = r->d0; dn = r->dn; }
                else { d0 = ild0; dn = ildn; }
                for (int i = 0; i < dn; i++) {
                    const wdecl_t *d = &ws.d[d0 + i];
                    if (d->imp != imp) continue;
                    if (pass == -1) {
                        if (d->prop == WP_CUSTOM && nwvars < WV_MAX) { wvars[nwvars].name = d->cname; wvars[nwvars].v0 = d->v0; wvars[nwvars].vn = d->vn; nwvars++; }
                        continue;
                    }
                    if (d->prop == WP_CUSTOM) continue;
                    if (pass == 0 && d->prop != WP_FONT_SIZE && d->prop != WP_FONT) continue;
                    static char vb[2048];
                    int vn;
                    const char *v = ws_decl_val(d, &vn, vb, sizeof(vb));
                    if (v) ws_apply(c, par, d->prop, v, vn, pass);
                }
            }
        }
        ws_cur_fs = save_fs;
    }
    if (!pe && k == 0) ws_root_fs = c->fsize;
    return 1;
}

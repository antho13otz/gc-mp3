// GC Station v118 : un petit moteur CSS pour le navigateur (inclus dans main.c, avant la mise en page).
// Ce qui compte pour qu'une page ressemble a celle d'un vrai navigateur, sans JavaScript :
//  - ce qui est cache (display:none, visibility:hidden, attribut hidden) : menus d'ordinateur, bandeaux de
//    cookies, fenetres, textes pour lecteurs d'ecran ;
//  - les couleurs (texte, fonds des blocs et des mots), le gras, la taille du texte, l'alignement, le
//    souligne, les bordures (encadres, tableaux) ;
//  - les regles @media evaluees pour un ecran de 640 points (souvent la mise en page « telephone »).
// Selecteurs : balise, .classe, #id, *, combines (div.a.b#c) et descendants (« a b », « a > b » traite comme
// « a b »). Les regles avec [attribut], :hover, :nth-child... sont ignorees (elles ne s'appliquent pas
// sans souris ni JavaScript, ou rarement) ; :root = html, :link garde la regle.

#define CSS_VW          640                     /* largeur d'ecran pour @media */
#define CSS_MAX_RULES   6000
#define CSS_MAX_DECLS   4000
#define CSS_NCOMP       5
#define CSS_NCLS        3
#define CSS_BUCKETS     1024

enum { CP_DISPLAY = 1, CP_COLOR = 2, CP_BG = 4, CP_WEIGHT = 8, CP_SIZE = 16, CP_ALIGN = 32, CP_DECO = 64, CP_VIS = 128, CP_BORDER = 256, CP_LIST = 512 };
enum { CD_NONE = 1, CD_BLOCK, CD_INLINE };
typedef struct {
    u16 set, imp;                               /* proprietes donnees ; !important */
    u8 display, weight, size, align, deco, vis; /* weight 1 normal 2 gras ; size 1 petit 2 moyen 3 grand ; align 1 g 2 c 3 d ; deco 1 aucun 2 souligne */
    u32 color, bg, border;                      /* 0xRRGGBBAA ; bg/border 0 = transparent */
    u8 bside;                                   /* bordure : 1 haut 2 droite 4 bas 8 gauche */
    u8 list;                                    /* puces : 1 aucune 2 oui */
} css_decl_t;
typedef struct { u32 tag, id; u32 cls[CSS_NCLS]; u8 ncls; } css_comp_t;
typedef struct { css_comp_t c[CSS_NCOMP]; u8 n; u16 spec; u16 decl; s32 next; u32 order; } css_rule_t;

typedef struct {
    css_rule_t *r; int nr;
    css_decl_t *d; int nd;
    s32 bucket[CSS_BUCKETS];                    /* premiere regle de chaque case (cle de droite) */
    s32 any;                                    /* regles sans cle (*) */
    u32 order;
} css_sheet_t;
static css_sheet_t css_s;                       /* feuilles de la page en cours (toutes fusionnees) */

static u32 css_hash(const char *s, int n) {     /* FNV-1a, sans tenir compte des majuscules ; jamais 0 */
    u32 h = 2166136261u;
    for (int i = 0; i < n; i++) { u8 c = (u8)s[i]; if (c >= 'A' && c <= 'Z') c += 32; h = (h ^ c) * 16777619u; }
    return h ? h : 1;
}
static u32 css_bkey(u32 kind, u32 h) { return (h * 2654435761u ^ kind * 40503u) & (CSS_BUCKETS - 1); }

static void css_reset(void) {
    free(css_s.r); free(css_s.d);
    memset(&css_s, 0, sizeof(css_s));
    for (int i = 0; i < CSS_BUCKETS; i++) css_s.bucket[i] = -1;
    css_s.any = -1;
}

/* ---------- couleurs ---------- */
static const struct { const char *n; u32 c; } css_named[] = {
    { "black", 0x000000 }, { "white", 0xffffff }, { "red", 0xff0000 }, { "green", 0x008000 }, { "blue", 0x0000ff },
    { "gray", 0x808080 }, { "grey", 0x808080 }, { "silver", 0xc0c0c0 }, { "maroon", 0x800000 }, { "purple", 0x800080 },
    { "fuchsia", 0xff00ff }, { "magenta", 0xff00ff }, { "lime", 0x00ff00 }, { "olive", 0x808000 }, { "yellow", 0xffff00 },
    { "navy", 0x000080 }, { "teal", 0x008080 }, { "aqua", 0x00ffff }, { "cyan", 0x00ffff }, { "orange", 0xffa500 },
    { "darkgray", 0xa9a9a9 }, { "darkgrey", 0xa9a9a9 }, { "lightgray", 0xd3d3d3 }, { "lightgrey", 0xd3d3d3 },
    { "whitesmoke", 0xf5f5f5 }, { "gainsboro", 0xdcdcdc }, { "darkblue", 0x00008b }, { "darkred", 0x8b0000 },
    { "darkgreen", 0x006400 }, { "brown", 0xa52a2a }, { "pink", 0xffc0cb }, { "gold", 0xffd700 }, { "beige", 0xf5f5dc },
    { "ivory", 0xfffff0 }, { "lightblue", 0xadd8e6 }, { "lightyellow", 0xffffe0 }, { "lightgreen", 0x90ee90 },
    { "dimgray", 0x696969 }, { "dimgrey", 0x696969 }, { "darkslategray", 0x2f4f4f }, { "slategray", 0x708090 },
    { "steelblue", 0x4682b4 }, { "royalblue", 0x4169e1 }, { "dodgerblue", 0x1e90ff }, { "crimson", 0xdc143c },
    { "tomato", 0xff6347 }, { "coral", 0xff7f50 }, { "salmon", 0xfa8072 }, { "indigo", 0x4b0082 }, { "violet", 0xee82ee },
    { "tan", 0xd2b48c }, { "khaki", 0xf0e68c }, { "aliceblue", 0xf0f8ff }, { "ghostwhite", 0xf8f8ff }, { "snow", 0xfffafa },
    { "linen", 0xfaf0e6 }, { "honeydew", 0xf0fff0 }, { "lavender", 0xe6e6fa }, { "midnightblue", 0x191970 },
    { "darkorange", 0xff8c00 }, { "forestgreen", 0x228b22 }, { "seagreen", 0x2e8b57 }, { "firebrick", 0xb22222 },
    { "chocolate", 0xd2691e }, { "skyblue", 0x87ceeb }, { "lightgoldenrodyellow", 0xfafad2 }, { "mintcream", 0xf5fffa },
};
static int css_hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static f32 css_num(const char *s, const char **end) {      /* nombre (avec . et -) */
    f32 v = 0, sg = 1, fr = 0.1f;
    while (*s == ' ') s++;
    if (*s == '-') { sg = -1; s++; } else if (*s == '+') s++;
    int any = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { v += (*s - '0') * fr; fr *= 0.1f; s++; any = 1; } }
    if (end) *end = any ? s : NULL;
    return v * sg;
}
static f32 css_clampf(f32 v, f32 a, f32 b) { return v < a ? a : v > b ? b : v; }
/* une couleur ; 1 si comprise (*out : 0xRRGGBBAA, 0 = transparent) */
static int css_color(const char *v, int n, u32 *out) {
    while (n > 0 && *v == ' ') { v++; n--; }
    while (n > 0 && v[n - 1] == ' ') n--;
    if (n <= 0) return 0;
    if (v[0] == '#') {
        int k = n - 1, d[8];
        if (k != 3 && k != 4 && k != 6 && k != 8) return 0;
        for (int i = 0; i < k; i++) if ((d[i] = css_hexv(v[1 + i])) < 0) return 0;
        u32 r, g, b, a = 255;
        if (k <= 4) { r = d[0] * 17; g = d[1] * 17; b = d[2] * 17; if (k == 4) a = d[3] * 17; }
        else { r = d[0] * 16 + d[1]; g = d[2] * 16 + d[3]; b = d[4] * 16 + d[5]; if (k == 8) a = d[6] * 16 + d[7]; }
        *out = a < 48 ? 0 : (r << 24 | g << 16 | b << 8 | 255);
        return 1;
    }
    if ((n > 4 && !strncasecmp(v, "rgb(", 4)) || (n > 5 && !strncasecmp(v, "rgba(", 5))) {
        const char *p = strchr(v, '(') + 1, *e;
        f32 c[4] = { 0, 0, 0, 1 };
        for (int i = 0; i < 4; i++) {
            while (p < v + n && (*p == ' ' || *p == ',' || *p == '/')) p++;
            if (p >= v + n || *p == ')') { if (i < 3) return 0; break; }
            c[i] = css_num(p, &e);
            if (!e) return 0;
            if (*e == '%') { c[i] = i < 3 ? c[i] * 2.55f : c[i] / 100; e++; }
            p = e;
        }
        if (c[3] < 0.19f) { *out = 0; return 1; }
        *out = (u32)css_clampf(c[0], 0, 255) << 24 | (u32)css_clampf(c[1], 0, 255) << 16 | (u32)css_clampf(c[2], 0, 255) << 8 | 255;
        return 1;
    }
    if ((n > 4 && !strncasecmp(v, "hsl(", 4)) || (n > 5 && !strncasecmp(v, "hsla(", 5))) {
        const char *p = strchr(v, '(') + 1, *e;
        f32 c[4] = { 0, 0, 0, 1 };
        for (int i = 0; i < 4; i++) {
            while (p < v + n && (*p == ' ' || *p == ',' || *p == '/')) p++;
            if (p >= v + n || *p == ')') { if (i < 3) return 0; break; }
            c[i] = css_num(p, &e);
            if (!e) return 0;
            if (!strncasecmp(e, "deg", 3)) e += 3;
            if (*e == '%') { if (i == 3) c[i] /= 100; e++; }
            p = e;
        }
        if (c[3] < 0.19f) { *out = 0; return 1; }
        f32 h = fmodf(c[0], 360) / 360, s = css_clampf(c[1] / 100, 0, 1), l = css_clampf(c[2] / 100, 0, 1);
        if (h < 0) h += 1;
        f32 q = l < 0.5f ? l * (1 + s) : l + s - l * s, pp = 2 * l - q, rgb[3];
        for (int i = 0; i < 3; i++) {
            f32 t = h + (1 - i) / 3.0f;
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            rgb[i] = t < 1 / 6.0f ? pp + (q - pp) * 6 * t : t < 0.5f ? q : t < 2 / 3.0f ? pp + (q - pp) * (2 / 3.0f - t) * 6 : pp;
        }
        *out = (u32)(rgb[0] * 255) << 24 | (u32)(rgb[1] * 255) << 16 | (u32)(rgb[2] * 255) << 8 | 255;
        return 1;
    }
    if (n == 11 && !strncasecmp(v, "transparent", 11)) { *out = 0; return 1; }
    for (unsigned i = 0; i < sizeof(css_named) / sizeof(css_named[0]); i++)
        if ((int)strlen(css_named[i].n) == n && !strncasecmp(v, css_named[i].n, n)) { *out = css_named[i].c << 8 | 255; return 1; }
    return 0;
}

/* ---------- declarations ---------- */
static int css_word_is(const char *v, int n, const char *w) { int l = (int)strlen(w); return n == l && !strncasecmp(v, w, l); }
static int css_has(const char *v, int n, const char *w) {
    int l = (int)strlen(w);
    for (int i = 0; i + l <= n; i++) if (!strncasecmp(v + i, w, l)) return 1;
    return 0;
}
/* taille du texte -> 1 petit, 2 moyen, 3 grand (0 : rien) */
static int css_fsize(const char *v, int n) {
    const char *e;
    if (n <= 0) return 0;
    if (css_word_is(v, n, "small") || css_word_is(v, n, "x-small") || css_word_is(v, n, "xx-small") || css_word_is(v, n, "smaller")) return 1;
    if (css_word_is(v, n, "medium")) return 2;
    if (css_word_is(v, n, "large") || css_word_is(v, n, "x-large") || css_word_is(v, n, "xx-large") || css_word_is(v, n, "larger")) return 3;
    if (v[0] == 'c' || v[0] == 'm' || v[0] == 'C' || v[0] == 'M') {        /* calc(1.3rem + .6vw), clamp(...), min(), max() : le premier nombre */
        const char *q = (const char *)memchr(v, '(', n);
        if (!q) return 0;
        q++;
        while (q < v + n && !((*q >= '0' && *q <= '9') || *q == '.')) q++;
        if (q >= v + n) return 0;
        int k = 0;
        while (q + k < v + n && ((q[k] >= '0' && q[k] <= '9') || q[k] == '.' || (q[k] >= 'a' && q[k] <= 'z') || q[k] == '%')) k++;
        return css_fsize(q, k);
    }
    f32 x = css_num(v, &e);
    if (!e || x <= 0) return 0;
    if (!strncasecmp(e, "px", 2)) x /= 16;
    else if (!strncasecmp(e, "pt", 2)) x /= 12;
    else if (*e == '%') x /= 100;
    else if (!strncasecmp(e, "em", 2) || !strncasecmp(e, "rem", 3)) { /* (deja en em) */ }
    else return 0;
    return x <= 0.84f ? 1 : x >= 1.24f ? 3 : 2;
}
/* bordure : epaisseur > 0 et couleur */
static int css_border(const char *v, int n, u32 *out) {
    if (css_word_is(v, n, "none") || css_word_is(v, n, "0") || css_has(v, n, "hidden")) { *out = 0; return 1; }
    u32 c = 0xa2a9b1ff;                                  /* (gris, si la couleur n'est pas donnee) */
    int i = 0, have = 0;
    while (i < n) {
        while (i < n && v[i] == ' ') i++;
        int s = i, depth = 0;
        while (i < n && (v[i] != ' ' || depth)) { if (v[i] == '(') depth++; else if (v[i] == ')') depth--; i++; }
        u32 cc;
        if (i > s && css_color(v + s, i - s, &cc)) { c = cc; have = 1; }
        else if (i > s && (v[s] >= '1' && v[s] <= '9')) have = 1;
        else if (i > s && (css_word_is(v + s, i - s, "solid") || css_word_is(v + s, i - s, "dashed") || css_word_is(v + s, i - s, "dotted"))) have = 1;
    }
    if (!have) return 0;
    *out = c;
    return 1;
}
static void css_decl_one(css_decl_t *d, const char *p, int pn, const char *v, int vn) {
    int imp = 0;
    for (int i = 0; i + 9 < vn + 1; i++)
        if (v[i] == '!' && vn - i >= 10 && !strncasecmp(v + i + 1, "important", 9)) { imp = 1; vn = i; break; }
    while (vn > 0 && (v[vn - 1] == ' ' || v[vn - 1] == '\t')) vn--;
    while (vn > 0 && (*v == ' ' || *v == '\t')) { v++; vn--; }
    if (vn <= 0 || css_has(v, vn, "var(")) return;
    u32 c;
    u16 bit = 0;
    if (css_word_is(p, pn, "display")) {
        bit = CP_DISPLAY;
        if (css_word_is(v, vn, "none")) d->display = CD_NONE;
        else if (css_has(v, vn, "inline") || css_word_is(v, vn, "contents")) d->display = CD_INLINE;
        else d->display = CD_BLOCK;
    } else if (css_word_is(p, pn, "visibility")) {
        bit = CP_VIS; d->vis = css_word_is(v, vn, "hidden") || css_word_is(v, vn, "collapse");
    } else if (css_word_is(p, pn, "opacity") || css_word_is(p, pn, "clip") || css_word_is(p, pn, "clip-path") ||
               css_word_is(p, pn, "text-indent") || css_word_is(p, pn, "left") || css_word_is(p, pn, "top")) {
        /* v118 : astuces pour cacher (lecteurs d'ecran, liens d'evitement, texte remplace par un logo) */
        const char *e; f32 x = css_num(v, &e);
        int h = 0;
        if (css_word_is(p, pn, "opacity")) h = e && x < 0.05f && (e == v + vn || *e == ' ' || *e == '!');
        else if (css_word_is(p, pn, "clip")) h = css_has(v, vn, "rect(0") || css_has(v, vn, "rect(1px") || css_has(v, vn, "rect(0px");
        else if (css_word_is(p, pn, "clip-path")) h = css_has(v, vn, "inset(50%") || css_has(v, vn, "inset(100%");
        else h = e && x <= -999;                          /* text-indent / left / top : -9999px */
        if (!h) return;
        bit = CP_VIS; d->vis = 1;
    } else if (css_word_is(p, pn, "color")) {
        if (!css_color(v, vn, &c) || !c) return;
        bit = CP_COLOR; d->color = c;
    } else if (css_word_is(p, pn, "background-color") || css_word_is(p, pn, "background")) {
        int i = 0, got = 0;
        if (css_word_is(v, vn, "none")) { c = 0; got = 1; }
        while (!got && i < vn) {                         /* premiere couleur trouvee (aussi dans un degrade) */
            while (i < vn && (v[i] == ' ' || v[i] == ',')) i++;
            int s = i, depth = 0;
            while (i < vn && ((v[i] != ' ' && v[i] != ',') || depth)) { if (v[i] == '(') depth++; else if (v[i] == ')') depth--; i++; }
            if (i <= s) break;
            if (css_color(v + s, i - s, &c)) got = 1;
            else if (i - s > 9 && !strncasecmp(v + s, "linear-gradient(", 16)) {   /* (degrade : sa premiere couleur) */
                const char *q = v + s + 16; int k = 0;
                while (q + k < v + i && !got) {
                    while (q + k < v + i && (q[k] == ' ' || q[k] == ',')) k++;
                    int ks = k, dd = 0;
                    while (q + k < v + i && ((q[k] != ' ' && q[k] != ',' && q[k] != ')') || dd)) { if (q[k] == '(') dd++; else if (q[k] == ')') dd--; k++; }
                    if (k > ks && css_color(q + ks, k - ks, &c)) got = 1;
                    if (q + k < v + i && q[k] == ')') break;
                    if (k == ks) k++;
                }
            }
        }
        if (!got) return;
        bit = CP_BG; d->bg = c;
    } else if (css_word_is(p, pn, "font-weight")) {
        const char *e; f32 x = css_num(v, &e);
        bit = CP_WEIGHT;
        d->weight = (e ? x >= 600 : (css_has(v, vn, "bold"))) ? 2 : 1;
    } else if (css_word_is(p, pn, "font-size")) {
        int s = css_fsize(v, vn);
        if (!s) return;
        bit = CP_SIZE; d->size = (u8)s;
    } else if (css_word_is(p, pn, "font")) {             /* raccourci : gras et taille */
        int i = 0;
        while (i < vn) {
            while (i < vn && v[i] == ' ') i++;
            int s = i;
            while (i < vn && v[i] != ' ' && v[i] != '/') i++;
            if (i > s && (css_word_is(v + s, i - s, "bold") || ((v[s] >= '6' && v[s] <= '9') && i - s == 3))) { d->weight = 2; d->set |= CP_WEIGHT; if (imp) d->imp |= CP_WEIGHT; }
            int fs = i > s ? css_fsize(v + s, i - s) : 0;
            if (fs && (v[i - 1] < '0' || v[i - 1] > '9')) { d->size = (u8)fs; d->set |= CP_SIZE; if (imp) d->imp |= CP_SIZE; }
            while (i < vn && v[i] == '/') { i++; while (i < vn && v[i] != ' ') i++; }
        }
        return;
    } else if (css_word_is(p, pn, "list-style") || css_word_is(p, pn, "list-style-type")) {
        bit = CP_LIST; d->list = css_has(v, vn, "none") ? 1 : 2;
    } else if (css_word_is(p, pn, "text-align")) {
        bit = CP_ALIGN;
        d->align = css_has(v, vn, "center") ? 2 : (css_has(v, vn, "right") || css_has(v, vn, "end")) ? 3 : 1;
    } else if (css_word_is(p, pn, "text-decoration") || css_word_is(p, pn, "text-decoration-line")) {
        bit = CP_DECO; d->deco = css_has(v, vn, "underline") ? 2 : 1;
    } else if (css_word_is(p, pn, "border") || css_word_is(p, pn, "border-top") || css_word_is(p, pn, "border-bottom") ||
               css_word_is(p, pn, "border-left") || css_word_is(p, pn, "border-right")) {
        if (!css_border(v, vn, &c)) return;
        if (pn > 6 && !c) return;                        /* (« border-left: none » ne retire pas le reste) */
        u8 sd = pn == 6 ? 15 : css_has(p, pn, "top") ? 1 : css_has(p, pn, "right") ? 2 : css_has(p, pn, "bottom") ? 4 : 8;
        if (!c) { d->bside = 0; } else d->bside = (u8)((d->set & CP_BORDER) && d->border ? d->bside | sd : sd);
        bit = CP_BORDER; d->border = c;
    } else if (css_word_is(p, pn, "border-color")) {
        if (!css_color(v, vn, &c)) return;
        if (!(d->set & CP_BORDER)) return;
        bit = CP_BORDER; d->border = c;
    } else return;
    d->set |= bit;
    if (imp) d->imp |= bit;
}
/* "prop: valeur; prop: valeur" */
static void css_decls(css_decl_t *d, const char *s, int n) {
    int i = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == ';' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
        int ps = i;
        while (i < n && s[i] != ':' && s[i] != ';') i++;
        if (i >= n || s[i] != ':') { while (i < n && s[i] != ';') i++; continue; }
        int pe = i;
        while (pe > ps && (s[pe - 1] == ' ' || s[pe - 1] == '\t' || s[pe - 1] == '\n')) pe--;
        i++;
        int vs = i, depth = 0;
        char q = 0;
        while (i < n) {
            char ch = s[i];
            if (q) { if (ch == q) q = 0; }
            else if (ch == '"' || ch == '\'') q = ch;
            else if (ch == '(') depth++;
            else if (ch == ')') depth--;
            else if (ch == ';' && depth <= 0) break;
            i++;
        }
        char vb[256];
        int vn = i - vs;
        if (vn > (int)sizeof(vb) - 1) vn = (int)sizeof(vb) - 1;
        for (int k = 0; k < vn; k++) { char ch = s[vs + k]; vb[k] = (ch == '\n' || ch == '\r' || ch == '\t') ? ' ' : ch; }
        vb[vn] = 0;
        if (pe > ps) css_decl_one(d, s + ps, pe - ps, vb, vn);
    }
}

/* ---------- selecteurs ---------- */
/* un selecteur (sans virgule) -> 1 si gardee */
static int css_sel_parse(const char *s, int n, css_rule_t *r) {
    memset(r, 0, sizeof(*r));
    int i = 0, nc = 0, ids = 0, cls = 0, tags = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r' || s[i] == '>')) i++;
        if (i >= n) break;
        if (s[i] == '+' || s[i] == '~') return 0;         /* freres : non gere (la regle est oubliee) */
        if (nc >= CSS_NCOMP) return 0;
        css_comp_t *c = &r->c[nc];
        int any = 0;
        while (i < n && s[i] != ' ' && s[i] != '>' && s[i] != '+' && s[i] != '~' && s[i] != '\n' && s[i] != '\t' && s[i] != '\r') {
            char ch = s[i];
            if (ch == '*') { i++; any = 1; continue; }
            if (ch == '[') return 0;
            if (ch == ':') {
                int k = i + 1 + (i + 1 < n && s[i + 1] == ':');
                int ks = k;
                while (k < n && (isalnum((u8)s[k]) || s[k] == '-')) k++;
                if (k - ks == 4 && !strncasecmp(s + ks, "root", 4) && !c->tag) { c->tag = css_hash("html", 4); tags++; i = k; any = 1; continue; }
                if ((k - ks == 4 && !strncasecmp(s + ks, "link", 4))) { i = k; continue; }
                return 0;
            }
            int pfx = (ch == '.' || ch == '#');
            int ws = i + pfx, we = ws;
            while (we < n && (isalnum((u8)s[we]) || s[we] == '-' || s[we] == '_' || (u8)s[we] >= 0x80 || s[we] == '\\')) {
                if (s[we] == '\\' && we + 1 < n) we++;   /* (caractere echappe) */
                we++;
            }
            if (we == ws) return 0;
            u32 h = css_hash(s + ws, we - ws);
            if (ch == '.') { if (c->ncls >= CSS_NCLS) return 0; c->cls[c->ncls++] = h; cls++; }
            else if (ch == '#') { c->id = h; ids++; }
            else { c->tag = h; tags++; }
            i = we;
            any = 1;
        }
        if (!any) return 0;
        nc++;
    }
    if (!nc) return 0;
    r->n = (u8)nc;
    int sp = ids * 100 + cls * 10 + tags;
    r->spec = (u16)(sp > 4000 ? 4000 : sp);
    return 1;
}
static void css_add_rule(css_rule_t *r) {
    if (css_s.nr >= CSS_MAX_RULES) return;
    if (!css_s.r) { css_s.r = (css_rule_t *)malloc(sizeof(css_rule_t) * 512); if (!css_s.r) return; }
    else if ((css_s.nr & 511) == 0) {
        css_rule_t *nr = (css_rule_t *)realloc(css_s.r, sizeof(css_rule_t) * (css_s.nr + 512));
        if (!nr) return;
        css_s.r = nr;
    }
    r->order = css_s.order++;
    const css_comp_t *k = &r->c[r->n - 1];
    s32 *head;
    if (k->id) head = &css_s.bucket[css_bkey(1, k->id)];
    else if (k->ncls) head = &css_s.bucket[css_bkey(2, k->cls[0])];
    else if (k->tag) head = &css_s.bucket[css_bkey(3, k->tag)];
    else head = &css_s.any;
    r->next = *head;
    css_s.r[css_s.nr] = *r;
    *head = css_s.nr++;
}
static int css_new_decl(void) {
    if (css_s.nd >= CSS_MAX_DECLS) return -1;
    if (!css_s.d) { css_s.d = (css_decl_t *)malloc(sizeof(css_decl_t) * 256); if (!css_s.d) return -1; }
    else if ((css_s.nd & 255) == 0) {
        css_decl_t *nd = (css_decl_t *)realloc(css_s.d, sizeof(css_decl_t) * (css_s.nd + 256));
        if (!nd) return -1;
        css_s.d = nd;
    }
    memset(&css_s.d[css_s.nd], 0, sizeof(css_decl_t));
    return css_s.nd++;
}

/* @media : vrai pour un ecran de CSS_VW points sans souris */
static int css_media_ok(const char *s, int n) {
    int i = 0, any = 0;
    while (i <= n) {
        int st = i;
        while (i < n && s[i] != ',') i++;
        const char *q = s + st; int ql = i - st, ok = 1, neg = 0;
        while (ql > 0 && *q == ' ') { q++; ql--; }
        if (ql >= 4 && !strncasecmp(q, "not ", 4)) { neg = 1; q += 4; ql -= 4; }
        if (css_has(q, ql, "print") || css_has(q, ql, "speech")) ok = 0;
        for (int k = 0; k < ql && ok; k++) {
            if (q[k] != '(') continue;
            int e = k; while (e < ql && q[e] != ')') e++;
            const char *f = q + k + 1; int fl = e - k - 1;
            const char *c = (const char *)memchr(f, ':', fl);
            if (c) {
                int nl = (int)(c - f);
                while (nl > 0 && f[nl - 1] == ' ') nl--;
                const char *ve; f32 x = css_num(c + 1, &ve);
                if (ve && !strncasecmp(ve, "em", 2)) x *= 16;
                if (css_word_is(f, nl, "max-width") || css_word_is(f, nl, "max-device-width")) ok = ve && CSS_VW <= x;
                else if (css_word_is(f, nl, "min-width") || css_word_is(f, nl, "min-device-width")) ok = ve && CSS_VW >= x;
                else if (css_word_is(f, nl, "max-height")) ok = ve && 480 <= x;
                else if (css_word_is(f, nl, "min-height")) ok = ve && 480 >= x;
                else if (css_word_is(f, nl, "prefers-color-scheme")) ok = css_has(c, (int)(f + fl - c), "light");
                else if (css_word_is(f, nl, "hover")) ok = css_has(c, (int)(f + fl - c), "none");
                else if (css_word_is(f, nl, "pointer") || css_word_is(f, nl, "any-pointer")) ok = !css_has(c, (int)(f + fl - c), "fine");
                else if (css_word_is(f, nl, "orientation")) ok = css_has(c, (int)(f + fl - c), "landscape");
            } else if (fl > 0 && css_has(f, fl, "width")) {   /* (syntaxe « 400px <= width <= 700px » : on la croit) */
            }
            k = e;
        }
        if (neg) ok = !ok;
        if (ok) any = 1;
        i++;
    }
    return any;
}

/* fin du bloc { ... } qui commence a s[i] == '{' (-> position apres '}') */
static int css_block_end(const char *s, int n, int i) {
    int depth = 0;
    char q = 0;
    for (; i < n; i++) {
        char c = s[i];
        if (q) { if (c == q) q = 0; else if (c == '\\') i++; continue; }
        if (c == '"' || c == '\'') q = c;
        else if (c == '{') depth++;
        else if (c == '}') { if (--depth <= 0) return i + 1; }
        else if (c == '/' && i + 1 < n && s[i + 1] == '*') { const char *e = strstr(s + i + 2, "*/"); i = e && e < s + n ? (int)(e - s) + 1 : n; }
    }
    return n;
}
static void css_parse_rules(const char *s, int n, int depth) {
    int i = 0;
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ';' || c == '}') { i++; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            int e = i + 2;
            while (e + 1 < n && !(s[e] == '*' && s[e + 1] == '/')) e++;
            i = e + 2;
            continue;
        }
        if (c == '<' && i + 3 < n && !strncmp(s + i, "<!--", 4)) { i += 4; continue; }
        if (c == '-' && i + 2 < n && !strncmp(s + i, "-->", 3)) { i += 3; continue; }
        if (c == '@') {
            int k = i + 1;
            while (k < n && (isalnum((u8)s[k]) || s[k] == '-')) k++;
            const char *kw = s + i + 1; int kl = k - i - 1;
            int b = k;
            while (b < n && s[b] != '{' && s[b] != ';') b++;
            if (b >= n) return;
            if (s[b] == ';') { i = b + 1; continue; }         /* @import, @charset... */
            int e = css_block_end(s, n, b);
            if (depth < 4 && ((css_word_is(kw, kl, "media") && css_media_ok(s + k, b - k)) || css_word_is(kw, kl, "supports") ||
                              css_word_is(kw, kl, "layer") || css_word_is(kw, kl, "container")))
                css_parse_rules(s + b + 1, e - b - 2 > 0 ? e - b - 2 : 0, depth + 1);
            i = e;
            continue;
        }
        int b = i;
        while (b < n && s[b] != '{') b++;
        if (b >= n) return;
        int e = css_block_end(s, n, b);
        int body_s = b + 1, body_n = e - b - 2;
        if (body_n > 0) {
            css_decl_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            css_decls(&tmp, s + body_s, body_n);
            if (tmp.set) {
                int di = -1;
                int k = i;
                while (k < b) {                              /* liste de selecteurs */
                    int ks = k, dd = 0;
                    while (k < b && (s[k] != ',' || dd)) { if (s[k] == '(') dd++; else if (s[k] == ')') dd--; k++; }
                    css_rule_t r;
                    if (css_sel_parse(s + ks, k - ks, &r)) {
                        if (di < 0) { di = css_new_decl(); if (di < 0) return; css_s.d[di] = tmp; }
                        r.decl = (u16)di;
                        css_add_rule(&r);
                    }
                    k++;
                }
            }
        }
        i = e;
    }
}
/* ajoute une feuille (<style> ou fichier .css) */
static void css_add_sheet(const char *s, int n) { css_parse_rules(s, n, 0); }

/* ---------- application a un element ---------- */
typedef struct { u32 tag, id; u32 cls[8]; u8 ncls; } css_el_t;
static int css_comp_match(const css_comp_t *c, const css_el_t *e) {
    if (c->tag && c->tag != e->tag) return 0;
    if (c->id && c->id != e->id) return 0;
    for (int i = 0; i < c->ncls; i++) {
        int f = 0;
        for (int j = 0; j < e->ncls && !f; j++) f = e->cls[j] == c->cls[i];
        if (!f) return 0;
    }
    return 1;
}
/* el = pile des elements (el[depth - 1] = l'element) */
static int css_rule_match(const css_rule_t *r, const css_el_t *el, int depth) {
    if (!css_comp_match(&r->c[r->n - 1], &el[depth - 1])) return 0;
    int a = depth - 2;
    for (int k = r->n - 2; k >= 0; k--) {
        while (a >= 0 && !css_comp_match(&r->c[k], &el[a])) a--;
        if (a < 0) return 0;
        a--;
    }
    return 1;
}
typedef struct { css_decl_t v; u32 pr[10]; } css_acc_t;
static void css_acc_apply(css_acc_t *a, const css_decl_t *d, u32 prio) {
    static const u16 bits[10] = { CP_DISPLAY, CP_COLOR, CP_BG, CP_WEIGHT, CP_SIZE, CP_ALIGN, CP_DECO, CP_VIS, CP_BORDER, CP_LIST };
    for (int i = 0; i < 10; i++) {
        if (!(d->set & bits[i])) continue;
        u32 p = prio | ((d->imp & bits[i]) ? 0x80000000u : 0);
        if ((a->v.set & bits[i]) && a->pr[i] > p) continue;
        a->pr[i] = p;
        a->v.set |= bits[i];
        switch (bits[i]) {
        case CP_DISPLAY: a->v.display = d->display; break;
        case CP_COLOR: a->v.color = d->color; break;
        case CP_BG: a->v.bg = d->bg; break;
        case CP_WEIGHT: a->v.weight = d->weight; break;
        case CP_SIZE: a->v.size = d->size; break;
        case CP_ALIGN: a->v.align = d->align; break;
        case CP_DECO: a->v.deco = d->deco; break;
        case CP_VIS: a->v.vis = d->vis; break;
        case CP_BORDER: a->v.border = d->border; a->v.bside = d->bside; break;
        case CP_LIST: a->v.list = d->list; break;
        }
    }
}
static void css_try_list(css_acc_t *a, s32 i, const css_el_t *el, int depth) {
    int guard = 0;
    while (i >= 0 && guard++ < 4000) {
        const css_rule_t *r = &css_s.r[i];
        if (css_rule_match(r, el, depth)) css_acc_apply(a, &css_s.d[r->decl], (u32)r->spec << 16 | (r->order > 0xFFFF ? 0xFFFF : r->order));
        i = r->next;
    }
}
/* style calcule par les feuilles + style="" (inl) ; out->set dit ce qui est donne */
static void css_compute(const css_el_t *el, int depth, const char *inl, int inl_n, css_decl_t *out) {
    css_acc_t a;
    memset(&a, 0, sizeof(a));
    if (css_s.nr > 0) {
        const css_el_t *e = &el[depth - 1];
        if (e->id) css_try_list(&a, css_s.bucket[css_bkey(1, e->id)], el, depth);
        for (int k = 0; k < e->ncls; k++) {
            int dup = 0;
            for (int j = 0; j < k && !dup; j++) dup = e->cls[j] == e->cls[k];
            if (!dup) css_try_list(&a, css_s.bucket[css_bkey(2, e->cls[k])], el, depth);
        }
        if (e->tag) css_try_list(&a, css_s.bucket[css_bkey(3, e->tag)], el, depth);
        css_try_list(&a, css_s.any, el, depth);
    }
    if (inl && inl_n > 0) {
        css_decl_t d;
        memset(&d, 0, sizeof(d));
        css_decls(&d, inl, inl_n);
        css_acc_apply(&a, &d, 0x7FFF0000u);
    }
    *out = a.v;
}

/* ---------- feuilles externes (<link rel=stylesheet>) gardees en memoire ---------- */
#define CSSC_N 12
#define CSSC_BUDGET (1536 * 1024)
typedef struct { char *url; char *data; int len; u32 used; } cssc_t;
static cssc_t cssc[CSSC_N];
static u32 cssc_clock = 0;
static long cssc_bytes = 0;
static const cssc_t *cssc_get(const char *url) {
    for (int i = 0; i < CSSC_N; i++) if (cssc[i].url && !strcmp(cssc[i].url, url)) { cssc[i].used = ++cssc_clock; return &cssc[i]; }
    return NULL;
}
static void cssc_put(const char *url, const char *d, int n) {
    if (n < 0 || n > 900 * 1024 || cssc_get(url)) return;
    for (;;) {
        int fr = -1, old = -1;
        for (int i = 0; i < CSSC_N; i++) {
            if (!cssc[i].url) { if (fr < 0) fr = i; continue; }
            if (old < 0 || cssc[i].used < cssc[old].used) old = i;
        }
        if (fr >= 0 && cssc_bytes + n <= CSSC_BUDGET) {
            char *u = strdup(url), *c = (char *)malloc(n + 1);
            if (!u || !c) { free(u); free(c); return; }
            memcpy(c, d, n); c[n] = 0;
            cssc[fr].url = u; cssc[fr].data = c; cssc[fr].len = n; cssc[fr].used = ++cssc_clock;
            cssc_bytes += n;
            return;
        }
        if (old < 0) return;
        cssc_bytes -= cssc[old].len;
        free(cssc[old].url); free(cssc[old].data);
        memset(&cssc[old], 0, sizeof(cssc[old]));
    }
}

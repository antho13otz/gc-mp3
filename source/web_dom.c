// GC Station v201 : moteur de page web, 1re partie : le HTML lu en arbre (inclus par web_layout.c).
// Comme un vrai navigateur (en plus simple) : fermetures implicites (<p>, <li>, cases de tableau...), balises
// vides, contenu brut de <script>/<style>/<textarea>/<title>, entites, Latin-1 ou UTF-8 -> UTF-8.
// <noscript> est lu normalement (pas de JavaScript ici : c'est ce qu'un navigateur sans JavaScript montre).

enum {                                          /* balises connues (les autres : WT_OTHER, nom en hachage) */
    WT_OTHER = 0, WT_HTML, WT_HEAD, WT_BODY, WT_TITLE, WT_META, WT_LINK, WT_BASE, WT_STYLE, WT_SCRIPT, WT_NOSCRIPT,
    WT_TEMPLATE, WT_SVG, WT_MATH, WT_IFRAME, WT_OBJECT, WT_EMBED, WT_CANVAS, WT_VIDEO, WT_AUDIO, WT_MAP,
    WT_P, WT_DIV, WT_SPAN, WT_A, WT_BR, WT_WBR, WT_HR, WT_IMG, WT_PICTURE, WT_SOURCE, WT_FIGURE, WT_FIGCAPTION,
    WT_H1, WT_H2, WT_H3, WT_H4, WT_H5, WT_H6, WT_UL, WT_OL, WT_LI, WT_DL, WT_DT, WT_DD, WT_MENU,
    WT_TABLE, WT_CAPTION, WT_THEAD, WT_TBODY, WT_TFOOT, WT_TR, WT_TD, WT_TH, WT_COL, WT_COLGROUP,
    WT_FORM, WT_INPUT, WT_BUTTON, WT_SELECT, WT_OPTION, WT_OPTGROUP, WT_TEXTAREA, WT_LABEL, WT_FIELDSET, WT_LEGEND,
    WT_PRE, WT_CODE, WT_KBD, WT_SAMP, WT_TT, WT_BLOCKQUOTE, WT_Q, WT_CITE, WT_EM, WT_I, WT_STRONG, WT_B, WT_U, WT_S,
    WT_STRIKE, WT_DEL, WT_INS, WT_SMALL, WT_BIG, WT_SUB, WT_SUP, WT_MARK, WT_ABBR, WT_FONT, WT_CENTER,
    WT_SECTION, WT_ARTICLE, WT_ASIDE, WT_HEADER, WT_FOOTER, WT_NAV, WT_MAIN, WT_ADDRESS, WT_DETAILS, WT_SUMMARY,
    WT_DIALOG, WT_AREA, WT_PARAM, WT_TRACK, WT_DATALIST, WT_NOBR, WT_VAR, WT_DFN, WT_TIME, WT_HGROUP, WT_XMP,
    WT_PLAINTEXT, WT_NOEMBED, WT_NOFRAMES, WT_FRAMESET, WT_FRAME, WT_LISTING, WT_OUTPUT, WT_PROGRESS, WT_METER,
    WT_COUNT
};
static const char *const wt_names[WT_COUNT] = {
    "", "html", "head", "body", "title", "meta", "link", "base", "style", "script", "noscript",
    "template", "svg", "math", "iframe", "object", "embed", "canvas", "video", "audio", "map",
    "p", "div", "span", "a", "br", "wbr", "hr", "img", "picture", "source", "figure", "figcaption",
    "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "dl", "dt", "dd", "menu",
    "table", "caption", "thead", "tbody", "tfoot", "tr", "td", "th", "col", "colgroup",
    "form", "input", "button", "select", "option", "optgroup", "textarea", "label", "fieldset", "legend",
    "pre", "code", "kbd", "samp", "tt", "blockquote", "q", "cite", "em", "i", "strong", "b", "u", "s",
    "strike", "del", "ins", "small", "big", "sub", "sup", "mark", "abbr", "font", "center",
    "section", "article", "aside", "header", "footer", "nav", "main", "address", "details", "summary",
    "dialog", "area", "param", "track", "datalist", "nobr", "var", "dfn", "time", "hgroup", "xmp",
    "plaintext", "noembed", "noframes", "frameset", "frame", "listing", "output", "progress", "meter",
};
static u32 wt_hash[WT_COUNT];

typedef struct {
    s32 parent, first, last, next, prev;
    u32 name;                                   /* css_hash du nom de la balise (0 : texte) */
    u16 tid;                                    /* WT_* */
    u8 type;                                    /* 1 element, 2 texte */
    u8 flags;
    u32 a0; u16 na;                             /* attributs : wd.attrs[a0 .. a0 + na) */
    u16 nidx;                                   /* rang parmi les elements freres (1 = premier), pour :nth-child */
    u32 t0, tn;                                 /* texte (UTF-8) : wd.txt + t0, tn octets */
} wnode_t;
typedef struct { u32 name; u32 v0; u32 vn; } wattr_t;   /* name = css_hash ; valeur UTF-8 dans wd.txt */

#define WD_MAX_NODES  90000
#define WD_MAX_ATTRS  160000
#define WD_MAX_TXT    (6 * 1024 * 1024)
#define WD_STACK      256

typedef struct {
    wnode_t *n; int nn, ncap;
    wattr_t *a; int na, acap;
    char *txt; int tlen, tcap;
    s32 stack[WD_STACK]; int sd;
    int fail;
    int scripts;                                /* la page a du JavaScript (pour l'avis « mode texte ») */
    int noscript_body;                          /* texte dans <noscript> */
    /* feuilles de style, dans l'ordre : <style> (texte dans wd.txt) ou <link> (adresse dans wd.txt) */
    struct { u32 off, len; u8 is_link; } sheets[48]; int nsheets;
    char base[URL_MAX];
    char title[160];
    char refresh[URL_MAX]; int refresh_delay;
} wdom_t;
static wdom_t wd;

static void wd_free(void) {
    free(wd.n); free(wd.a); free(wd.txt);
    memset(&wd, 0, sizeof(wd));
}

static int wd_text_add(const char *s, int n) {      /* -> position dans wd.txt (ou -1) */
    if (!grow((void **)&wd.txt, &wd.tcap, wd.tlen + n + 1, 1, WD_MAX_TXT)) { wd.fail = 1; return -1; }
    int o = wd.tlen;
    memcpy(wd.txt + o, s, n);
    wd.tlen += n;
    wd.txt[wd.tlen] = 0;                        /* (pas compte : la suite peut s'y coller) */
    return o;
}

static int wt_lookup(const char *nm, int n) {
    if (!wt_hash[1]) for (int i = 1; i < WT_COUNT; i++) wt_hash[i] = css_hash(wt_names[i], (int)strlen(wt_names[i]));
    u32 h = css_hash(nm, n);
    for (int i = 1; i < WT_COUNT; i++) if (wt_hash[i] == h && (int)strlen(wt_names[i]) == n && !strncasecmp(wt_names[i], nm, n)) return i;
    return WT_OTHER;
}
static int wt_void(int t) {
    return t == WT_BR || t == WT_WBR || t == WT_HR || t == WT_IMG || t == WT_INPUT || t == WT_META || t == WT_LINK ||
           t == WT_BASE || t == WT_SOURCE || t == WT_COL || t == WT_AREA || t == WT_PARAM || t == WT_TRACK || t == WT_EMBED || t == WT_FRAME;
}
/* balises qui ferment un <p> ouvert */
static int wt_closes_p(int t) {
    switch (t) {
    case WT_ADDRESS: case WT_ARTICLE: case WT_ASIDE: case WT_BLOCKQUOTE: case WT_DETAILS: case WT_DIV: case WT_DL:
    case WT_FIELDSET: case WT_FIGCAPTION: case WT_FIGURE: case WT_FOOTER: case WT_FORM: case WT_H1: case WT_H2:
    case WT_H3: case WT_H4: case WT_H5: case WT_H6: case WT_HEADER: case WT_HR: case WT_MAIN: case WT_MENU: case WT_NAV:
    case WT_OL: case WT_P: case WT_PRE: case WT_SECTION: case WT_TABLE: case WT_UL: case WT_HGROUP: case WT_DIALOG:
    case WT_CENTER: case WT_LISTING: case WT_XMP:
        return 1;
    }
    return 0;
}
static int wt_heading(int t) { return t >= WT_H1 && t <= WT_H6; }

static wnode_t *wd_new(int type) {
    if (!grow((void **)&wd.n, &wd.ncap, wd.nn + 1, sizeof(wnode_t), WD_MAX_NODES)) { wd.fail = 1; return NULL; }
    wnode_t *x = &wd.n[wd.nn];
    memset(x, 0, sizeof(*x));
    x->parent = x->first = x->last = x->next = x->prev = -1;
    x->type = (u8)type;
    return x;
}
static void wd_append(int par, int k) {
    wnode_t *x = &wd.n[k];
    x->parent = par;
    if (par < 0) return;
    wnode_t *p = &wd.n[par];
    if (p->last >= 0) { wd.n[p->last].next = k; x->prev = p->last; x->nidx = (u16)(wd.n[p->last].nidx + (x->type == 1)); }
    else { p->first = k; x->nidx = (u16)(x->type == 1); }
    if (x->type == 1 && x->prev >= 0 && wd.n[x->prev].type != 1) {  /* (rang : seuls les elements comptent) */
        int q = x->prev; while (q >= 0 && wd.n[q].type != 1) q = wd.n[q].prev;
        x->nidx = (u16)(q >= 0 ? wd.n[q].nidx + 1 : 1);
    }
    p->last = k;
}
static int wd_cur(void) { return wd.sd > 0 ? wd.stack[wd.sd - 1] : 0; }

/* texte : colle au noeud texte precedent s'il est juste avant */
static void wd_add_text(const char *s, int n) {
    if (n <= 0 || wd.fail) return;
    int par = wd_cur();
    wnode_t *p = &wd.n[par];
    if (p->last >= 0 && wd.n[p->last].type == 2 && wd.n[p->last].t0 + wd.n[p->last].tn == (u32)wd.tlen) {
        if (wd_text_add(s, n) >= 0) wd.n[p->last].tn += n;
        return;
    }
    int o = wd_text_add(s, n);
    if (o < 0) return;
    wnode_t *x = wd_new(2);
    if (!x) return;
    x->t0 = (u32)o; x->tn = (u32)n;
    int k = wd.nn++;
    wd_append(par, k);
}

/* attribut d'un noeud (nom en minuscules) -> valeur, ou NULL */
static const char *wd_attr(int k, const char *name, int *vn) {
    u32 h = css_hash(name, (int)strlen(name));
    const wnode_t *x = &wd.n[k];
    for (u32 i = 0; i < x->na; i++) {
        const wattr_t *a = &wd.a[x->a0 + i];
        if (a->name == h) { if (vn) *vn = (int)a->vn; return wd.txt + a->v0; }
    }
    return NULL;
}
static int wd_attr_s(int k, const char *name, char *out, int cap) {   /* copie terminee par 0 ; 1 si present */
    int vn = 0;
    const char *v = wd_attr(k, name, &vn);
    if (!v) { if (cap) out[0] = 0; return 0; }
    if (vn > cap - 1) vn = cap - 1;
    memcpy(out, v, vn); out[vn] = 0;
    return 1;
}

static void wd_pop_to(int idx) { if (idx >= 0 && idx < wd.sd) wd.sd = idx; }
/* cherche une balise dans la pile, sans passer certaines limites ; -> rang dans la pile ou -1 */
static int wd_find(int tid, u32 name, const int *stop) {
    for (int i = wd.sd - 1; i >= 1; i--) {
        const wnode_t *x = &wd.n[wd.stack[i]];
        if (tid ? x->tid == tid : x->name == name) return i;
        if (stop) for (int k = 0; stop[k]; k++) if (x->tid == stop[k]) return -1;
    }
    return -1;
}
static const int wd_stop_scope[] = { WT_TABLE, WT_TD, WT_TH, WT_CAPTION, WT_HTML, WT_TEMPLATE, WT_OBJECT, WT_BUTTON, 0 };
static const int wd_stop_list[] = { WT_UL, WT_OL, WT_MENU, WT_TABLE, WT_TD, WT_TH, 0 };
static const int wd_stop_dl[] = { WT_DL, WT_TABLE, WT_TD, WT_TH, 0 };
static const int wd_stop_table[] = { WT_TABLE, WT_HTML, 0 };
static const int wd_stop_row[] = { WT_TR, WT_TABLE, WT_HTML, 0 };

static void wd_close_p(void) { int i = wd_find(WT_P, 0, wd_stop_scope); if (i >= 0) wd_pop_to(i); }

static void wd_open(int tid, const char *nm, int nl, const char *tag, int tl, int latin) {
    if (wd.fail) return;
    /* fermetures implicites */
    if (wt_closes_p(tid) || tid == WT_LI || tid == WT_DT || tid == WT_DD) wd_close_p();
    if (wt_heading(tid) && wd.sd > 0 && wt_heading(wd.n[wd_cur()].tid)) wd.sd--;
    if (tid == WT_LI) { int i = wd_find(WT_LI, 0, wd_stop_list); if (i >= 0) wd_pop_to(i); }
    if (tid == WT_DT || tid == WT_DD) {
        int i = wd_find(WT_DD, 0, wd_stop_dl), j = wd_find(WT_DT, 0, wd_stop_dl);
        if (j > i) i = j;
        if (i >= 0) wd_pop_to(i);
    }
    if (tid == WT_TR) { int i = wd_find(WT_TR, 0, wd_stop_table); if (i >= 0) wd_pop_to(i); }
    if (tid == WT_TD || tid == WT_TH) {
        int i = wd_find(WT_TD, 0, wd_stop_row), j = wd_find(WT_TH, 0, wd_stop_row);
        if (j > i) i = j;
        if (i >= 0) wd_pop_to(i);
    }
    if (tid == WT_THEAD || tid == WT_TBODY || tid == WT_TFOOT) {
        static const int grp[] = { WT_THEAD, WT_TBODY, WT_TFOOT };
        int best = -1;
        for (int g = 0; g < 3; g++) { int i = wd_find(grp[g], 0, wd_stop_table); if (i > best) best = i; }
        if (best >= 0) wd_pop_to(best);
        else { int i = wd_find(WT_TR, 0, wd_stop_table); if (i >= 0) wd_pop_to(i); }
    }
    if (tid == WT_OPTION || tid == WT_OPTGROUP) {
        if (wd.sd > 0 && wd.n[wd_cur()].tid == WT_OPTION) wd.sd--;
        if (tid == WT_OPTGROUP && wd.sd > 0 && wd.n[wd_cur()].tid == WT_OPTGROUP) wd.sd--;
    }
    if (tid == WT_A) { int i = wd_find(WT_A, 0, wd_stop_scope); if (i >= 0) wd_pop_to(i); }
    if (tid == WT_TABLE && wd.sd > 0 && wd.n[wd_cur()].tid == WT_TABLE) wd.sd--;   /* (table dans table sans case) */
    if (tid == WT_FORM && wd_find(WT_FORM, 0, NULL) >= 0) return;                   /* (formulaires imbriques : ignore) */
    if (tid == WT_HTML || tid == WT_BODY || tid == WT_HEAD) {
        /* une seule fois : les attributs s'ajoutent (<body bgcolor>), rien n'est cree */
        return;
    }
    wnode_t *x = wd_new(1);
    if (!x) return;
    x->tid = (u16)tid;
    x->name = css_hash(nm, nl);
    /* attributs */
    x->a0 = (u32)wd.na;
    int p = 0;
    while (p < tl && !isspace((u8)tag[p]) && tag[p] != '/') p++;
    while (p < tl && x->na < 64) {
        while (p < tl && (isspace((u8)tag[p]) || tag[p] == '/')) p++;
        int ns = p;
        while (p < tl && !isspace((u8)tag[p]) && tag[p] != '=' && tag[p] != '/') p++;
        int nlen = p - ns;
        while (p < tl && isspace((u8)tag[p])) p++;
        int vs = -1, vl = 0;
        if (p < tl && tag[p] == '=') {
            p++;
            while (p < tl && isspace((u8)tag[p])) p++;
            if (p < tl && (tag[p] == '"' || tag[p] == '\'')) {
                char q = tag[p++];
                vs = p;
                while (p < tl && tag[p] != q) p++;
                vl = p - vs;
                if (p < tl) p++;
            } else { vs = p; while (p < tl && !isspace((u8)tag[p])) p++; vl = p - vs; }
        }
        if (nlen <= 0) { if (vs < 0) p++; continue; }
        if (!grow((void **)&wd.a, &wd.acap, wd.na + 1, sizeof(wattr_t), WD_MAX_ATTRS)) { wd.fail = 1; return; }
        wattr_t *a = &wd.a[wd.na];
        a->name = css_hash(tag + ns, nlen);
        static char dec[URL_MAX + 512];
        int dn = 0;
        if (vs >= 0) {
            int cap = vl * 3 + 8 < (int)sizeof(dec) ? vl * 3 + 8 : (int)sizeof(dec);
            attr_decode(tag + vs, vl, dec, cap, latin);
            dn = (int)strlen(dec);
        }
        int o = wd_text_add(dec, dn);
        if (o < 0) return;
        a->v0 = (u32)o; a->vn = (u32)dn;
        wd.na++;
        x = &wd.n[wd.nn];                       /* (wd.n n'a pas bouge, mais par prudence) */
        x->na++;
    }
    int k = wd.nn++;
    wd_append(wd_cur(), k);
    if (!wt_void(tid) && !(tl > 0 && tag[tl - 1] == '/' && (tid == WT_OTHER || tid == WT_SVG))) {
        if (wd.sd < WD_STACK) wd.stack[wd.sd++] = k;
    }
}

static void wd_close(int tid, u32 name) {
    if (tid == WT_HTML || tid == WT_BODY || tid == WT_HEAD) return;
    if (tid == WT_BR) { wd_open(WT_BR, "br", 2, "br", 2, 0); return; }
    if (tid == WT_P && wd_find(WT_P, 0, wd_stop_scope) < 0) { wd_open(WT_P, "p", 1, "p", 1, 0); }
    const int *stop = (tid == WT_TD || tid == WT_TH || tid == WT_TR || tid == WT_TBODY || tid == WT_THEAD || tid == WT_TFOOT || tid == WT_CAPTION) ? wd_stop_table :
                      (tid == WT_TABLE) ? NULL : (tid == WT_LI) ? wd_stop_list : wd_stop_scope;
    if (wt_heading(tid)) {                      /* </h3> ferme n'importe quel titre ouvert */
        for (int i = wd.sd - 1; i >= 1; i--) { if (wt_heading(wd.n[wd.stack[i]].tid)) { wd_pop_to(i); return; } if (wd.sd - i > 40) break; }
        return;
    }
    int i = tid ? wd_find(tid, 0, stop) : wd_find(0, name, stop);
    if (i >= 0 && wd.sd - i <= 64) wd_pop_to(i);
}

/* lit le texte entre a et b (entites, charset) et l'ajoute ; brut = sans entites (script) */
static void wd_text_range(const unsigned char *s, int a, int b, int latin) {
    static char buf[2048];
    int n = 0, pos = a;
    while (pos < b) {
        unsigned cp = s[pos] == '&' ? read_entity(s, b, &pos) : read_cp(s, b, &pos, latin);
        if (cp == '\r') { if (pos < b && s[pos] == '\n') continue; cp = '\n'; }
        if (cp == 0) continue;
        if (cp < 0x80) buf[n++] = (char)cp;
        else if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { buf[n++] = (char)(0xE0 | (cp >> 12)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
        else { buf[n++] = (char)(0xF0 | (cp >> 18)); buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
        if (n > (int)sizeof(buf) - 8) { wd_add_text(buf, n); n = 0; }
    }
    if (n) wd_add_text(buf, n);
}

static int wd_find_close(const unsigned char *s, int len, int pos, const char *name) {
    int nl = (int)strlen(name);
    for (int i = pos; i + nl + 2 <= len; i++)
        if (s[i] == '<' && s[i + 1] == '/' && !strncasecmp((const char *)s + i + 2, name, nl) && (i + 2 + nl >= len || !isalnum(s[i + 2 + nl]))) return i;
    return len;
}

/* fin d'un element (svg dans svg...) : position du '<' de sa fermeture */
static int wd_skip_elem(const unsigned char *s, int len, int pos, const char *name) {
    int nl = (int)strlen(name), depth = 1;
    for (int i = pos; i + nl + 1 < len; i++) {
        if (s[i] != '<') continue;
        if (s[i + 1] == '/' && i + 2 + nl <= len && !strncasecmp((const char *)s + i + 2, name, nl) && (i + 2 + nl >= len || !isalnum(s[i + 2 + nl]))) {
            if (--depth == 0) return i;
        } else if (!strncasecmp((const char *)s + i + 1, name, nl) && !isalnum(s[i + 1 + nl])) {
            int e = i + 1; while (e < len && s[e] != '>') e++;
            if (!(e > 0 && s[e - 1] == '/')) depth++;
        }
    }
    return len;
}

/* le HTML -> wd (arbre) ; 0 si la memoire manque */
static int wd_parse(const unsigned char *s, int len, int latin, const char *url) {
    wd_free();
    snprintf(wd.base, sizeof(wd.base), "%s", url);
    wnode_t *root = wd_new(1);                  /* noeud 0 : <html> (et <body> : tout y va) */
    if (!root) return 0;
    root->tid = WT_HTML; root->name = css_hash("html", 4);
    wd.nn = 1;
    wnode_t *body = wd_new(1);
    if (!body) return 0;
    body->tid = WT_BODY; body->name = css_hash("body", 4);
    wd.nn = 2;
    wd_append(0, 1);
    wd.stack[0] = 0; wd.stack[1] = 1; wd.sd = 2;
    int in_head = 0;
    static char tag[4096];
    int pos = 0, ts = 0;                        /* ts : debut du texte en attente */
    u32 tick = 0;
    while (pos < len && !wd.fail) {
        if ((++tick & 4095) == 0) { if (lay_in_bg) { if (rl_cancel) return 0; } else bg_tick(); }
        if (s[pos] != '<') { pos++; continue; }
        /* texte en attente */
        int lt = pos;
        int close = pos + 1 < len && s[pos + 1] == '/';
        int ns = pos + 1 + close;
        if (ns >= len) break;
        if (s[ns] == '!' || s[ns] == '?') {
            if (lt > ts && !in_head) wd_text_range(s, ts, lt, latin);
            if (ns + 2 < len && s[ns] == '!' && s[ns + 1] == '-' && s[ns + 2] == '-') {
                int e = ns + 3;
                while (e + 2 < len && !(s[e] == '-' && s[e + 1] == '-' && s[e + 2] == '>')) e++;
                pos = ts = e + 3;
            } else if (ns + 7 < len && !strncmp((const char *)s + ns, "![CDATA[", 8)) {
                int e = ns + 8;
                while (e + 2 < len && !(s[e] == ']' && s[e + 1] == ']' && s[e + 2] == '>')) e++;
                pos = ts = e + 3;
            } else {
                int e = ns; while (e < len && s[e] != '>') e++;
                pos = ts = e + 1;
            }
            continue;
        }
        if (!isalpha(s[ns])) { pos++; continue; }                 /* « < » tout seul : du texte */
        if (lt > ts && !in_head) wd_text_range(s, ts, lt, latin);
        /* fin de la balise (guillemets sautes) */
        int e = ns;
        char q = 0;
        while (e < len) {
            if (q) { if (s[e] == q) q = 0; }
            else if (s[e] == '"' || s[e] == '\'') { if (e > ns && (s[e - 1] == '=' || s[e - 1] == ' ')) q = (char)s[e]; }
            else if (s[e] == '>') break;
            e++;
        }
        int tl = e - ns;
        if (tl > (int)sizeof(tag) - 1) tl = (int)sizeof(tag) - 1;
        memcpy(tag, s + ns, tl); tag[tl] = 0;
        pos = ts = e + 1;
        int nl = 0;
        while (nl < tl && nl < 40 && (isalnum((u8)tag[nl]) || tag[nl] == '-' || tag[nl] == ':' || tag[nl] == '_')) nl++;
        if (!nl) continue;
        int tid = wt_lookup(tag, nl);
        if (close) {
            if (tid == WT_HEAD) in_head = 0;
            if (!in_head) wd_close(tid, css_hash(tag, nl));
            continue;
        }
        /* contenus lus en bloc */
        if (tid == WT_SCRIPT) { wd.scripts++; pos = ts = wd_find_close(s, len, pos, "script"); continue; }
        if (tid == WT_STYLE) {
            int ce = wd_find_close(s, len, pos, "style");
            char mv[300];
            int ok = !tag_attr(tag, tl, "media", mv, sizeof(mv), latin) || css_media_ok(mv, (int)strlen(mv));
            if (ok && wd.nsheets < 48) {
                int o = wd_text_add((const char *)s + pos, ce - pos);
                if (o >= 0) { wd.sheets[wd.nsheets].off = (u32)o; wd.sheets[wd.nsheets].len = (u32)(ce - pos); wd.sheets[wd.nsheets].is_link = 0; wd.nsheets++; wd_text_add("", 0); wd.tlen++; }
            }
            pos = ts = ce;
            continue;
        }
        if (tid == WT_TITLE) {
            int ce = wd_find_close(s, len, pos, "title");
            if (!wd.title[0]) raw_text(s, pos, ce, wd.title, sizeof(wd.title), latin);
            pos = ts = ce;
            continue;
        }
        if (tid == WT_TEMPLATE || tid == WT_SVG || tid == WT_MATH || tid == WT_XMP || tid == WT_NOEMBED || tid == WT_NOFRAMES || tid == WT_IFRAME) {
            if (tid == WT_SVG && in_head) continue;
            if (tl > 0 && tag[tl - 1] == '/') continue;
            int p2 = wd_skip_elem(s, len, pos, wt_names[tid]);
            if (tid == WT_XMP) { wd_open(WT_PRE, "pre", 3, "pre", 3, latin); wd_text_range(s, pos, p2, latin); wd_close(WT_PRE, 0); }
            else if (tid == WT_IFRAME || tid == WT_SVG) {
                /* case vide (une image de la page, un lecteur...) : on garde l'element sans contenu, pour la place / le lien */
                wd_open(tid, tag, nl, tag, tl, latin);
                if (wd.sd > 0 && wd.n[wd_cur()].tid == tid) wd.sd--;
            }
            int ce = p2;
            if (ce < len) { while (ce < len && s[ce] != '>') ce++; ce++; }
            pos = ts = ce;
            continue;
        }
        if (tid == WT_TEXTAREA) {
            int ce = wd_find_close(s, len, pos, "textarea");
            wd_open(tid, tag, nl, tag, tl, latin);
            int k = wd.sd > 0 ? wd_cur() : -1;
            if (k >= 0 && wd.n[k].tid == WT_TEXTAREA) { wd_text_range(s, pos, ce, latin); wd.sd--; }
            pos = ts = ce;
            continue;
        }
        if (tid == WT_HEAD) { in_head = 1; continue; }
        if (tid == WT_BODY || tid == WT_HTML) {
            in_head = 0;
            /* attributs de <body> / <html> : sur les noeuds 1 / 0 */
            int k = tid == WT_BODY ? 1 : 0;
            if (wd.n[k].na == 0) {
                int save_nn = wd.nn;
                wd_open(WT_SPAN, "span", 4, tag, tl, latin);        /* (lit les attributs dans un noeud jetable) */
                if (wd.nn > save_nn) {
                    wd.n[k].a0 = wd.n[save_nn].a0; wd.n[k].na = wd.n[save_nn].na;
                    /* retire le noeud jetable */
                    int par = wd.n[save_nn].parent;
                    if (par >= 0) { if (wd.n[par].last == save_nn) { wd.n[par].last = wd.n[save_nn].prev; if (wd.n[save_nn].prev >= 0) wd.n[wd.n[save_nn].prev].next = -1; else wd.n[par].first = -1; } }
                    wd.nn = save_nn;
                    if (wd.sd > 0 && wd.stack[wd.sd - 1] == save_nn) wd.sd--;
                }
            }
            continue;
        }
        if (tid == WT_META) {
            char v[URL_MAX];
            if (tag_attr(tag, tl, "http-equiv", v, sizeof(v), latin) && !strcasecmp(v, "refresh") && tag_attr(tag, tl, "content", v, sizeof(v), latin)) {
                int d = atoi(v);
                char *u = ci_find(v, "url=");
                if (u && !wd.refresh[0]) {
                    u += 4;
                    if (*u == '\'' || *u == '"') u++;
                    u[strcspn(u, "'\"")] = 0;
                    if (url_join(wd.base, u, wd.refresh, sizeof(wd.refresh))) wd.refresh_delay = d;
                }
            }
            continue;
        }
        if (tid == WT_BASE) {
            char v[URL_MAX], v2[URL_MAX];
            if (tag_attr(tag, tl, "href", v, sizeof(v), latin) && url_join(url, v, v2, sizeof(v2))) snprintf(wd.base, sizeof(wd.base), "%s", v2);
            continue;
        }
        if (tid == WT_LINK) {
            char v[URL_MAX], v2[URL_MAX];
            if (tag_attr(tag, tl, "rel", v, sizeof(v), latin) && ci_find(v, "stylesheet") && !ci_find(v, "alternate") &&
                (!tag_attr(tag, tl, "media", v, sizeof(v), latin) || css_media_ok(v, (int)strlen(v))) &&
                tag_attr(tag, tl, "href", v, sizeof(v), latin) && url_join(wd.base, v, v2, sizeof(v2)) && wd.nsheets < 48) {
                int o = wd_text_add(v2, (int)strlen(v2) + 1);
                if (o >= 0) { wd.sheets[wd.nsheets].off = (u32)o; wd.sheets[wd.nsheets].len = (u32)strlen(v2); wd.sheets[wd.nsheets].is_link = 1; wd.nsheets++; }
            }
            continue;
        }
        if (in_head) {                          /* contenu dans <head> : un element du corps ferme <head> */
            if (tid == WT_NOSCRIPT || tid == WT_OTHER) continue;
            in_head = 0;
        }
        wd_open(tid, tag, nl, tag, tl, latin);
    }
    if (len > ts && !in_head && !wd.fail) wd_text_range(s, ts, len, latin);
    return !wd.fail;
}

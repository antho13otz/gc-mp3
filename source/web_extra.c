// Navigateur, v13 : images gardees en memoire, cookies, fichiers audio ecoutes
// sans les telecharger, listes deroulantes, pointeur au stick, adresse envoyee
// depuis le telephone.

/* ===================== Images gardees en memoire ===================== */
/* Les fichiers d'images deja telecharges (compresses) restent en memoire :
   revenir sur une page ou retrouver le logo d'un site ne coute plus rien. */
#define ICACHE_N       48
#define ICACHE_BUDGET  (2 * 1024 * 1024)    /* v118 : 3 Mo avant (images plus grandes en memoire) */
#define ICACHE_MAX_ONE (640 * 1024)
typedef struct { char *url; unsigned char *data; int len; u32 used; } icache_t;
static icache_t icache[ICACHE_N];
static long icache_bytes = 0;
static u32 icache_clock = 0;

static void icache_drop(int i) {
    icache_bytes -= icache[i].len;
    free(icache[i].url); free(icache[i].data);
    memset(&icache[i], 0, sizeof(icache[i]));
}

/* copie du fichier (a liberer), ou NULL */
static unsigned char *icache_get(const char *url, int *len) {
    for (int i = 0; i < ICACHE_N; i++) {
        if (!icache[i].url || strcmp(icache[i].url, url)) continue;
        unsigned char *c = (unsigned char *)malloc(icache[i].len);
        if (!c) return NULL;
        memcpy(c, icache[i].data, icache[i].len);
        *len = icache[i].len;
        icache[i].used = ++icache_clock;
        return c;
    }
    return NULL;
}

static void icache_put(const char *url, const unsigned char *d, int len) {
    if (len <= 0 || len > ICACHE_MAX_ONE) return;
    for (int i = 0; i < ICACHE_N; i++) if (icache[i].url && !strcmp(icache[i].url, url)) return;
    for (;;) {                                         /* place : on oublie les plus anciennes */
        int free_slot = -1, old = -1;
        for (int i = 0; i < ICACHE_N; i++) {
            if (!icache[i].url) { if (free_slot < 0) free_slot = i; continue; }
            if (old < 0 || icache[i].used < icache[old].used) old = i;
        }
        if (free_slot >= 0 && icache_bytes + len <= ICACHE_BUDGET) {
            char *u = strdup(url);
            unsigned char *c = (unsigned char *)malloc(len);
            if (!u || !c) { free(u); free(c); return; }
            memcpy(c, d, len);
            icache[free_slot].url = u; icache[free_slot].data = c; icache[free_slot].len = len;
            icache[free_slot].used = ++icache_clock;
            icache_bytes += len;
            return;
        }
        if (old < 0) return;
        icache_drop(old);
    }
}

/* ============================== Cookies ============================== */
/* Garde les cookies des sites (connexion, preferences) ; ceux qui ont une date
   d'expiration sont enregistres dans sd:/gc-mp3/cookies.txt. */
#define CK_FILE GCD "/cookies.txt"
#define CK_MAX  160
typedef struct { char domain[80]; char path[64]; char name[64]; char value[400]; u32 expires; u8 host_only, secure, used; } cookie_t;
static cookie_t cks[CK_MAX];
static int ck_loaded = 0, ck_dirty = 0;
static u64 ck_dirty_t = 0;

static u32 ck_now(void) { return (u32)time(NULL); }

/* "Wed, 21 Oct 2026 07:28:00 GMT" (ou 21-Oct-2026) -> secondes depuis 1970 */
static u32 ck_parse_date(const char *s) {
    static const char *const mon[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
    int d = -1, m = -1, y = -1, hh = 0, mm = 0, ss = 0;
    for (const char *p = s; *p; ) {
        if (isdigit((unsigned char)*p)) {
            int n = 0, k = 0;
            while (isdigit((unsigned char)p[k])) { n = n * 10 + (p[k] - '0'); k++; if (k > 6) break; }
            if (p[k] == ':') { sscanf(p, "%d:%d:%d", &hh, &mm, &ss); while (*p && (isdigit((unsigned char)*p) || *p == ':')) p++; continue; }
            if (k >= 4 || n > 31) y = n; else if (d < 0) d = n; else y = n;
            p += k;
        } else if (isalpha((unsigned char)*p)) {
            for (int i = 0; i < 12; i++) if (!strncasecmp(p, mon[i], 3)) m = i;
            while (isalpha((unsigned char)*p)) p++;
        } else p++;
    }
    if (y >= 0 && y < 70) y += 2000; else if (y >= 70 && y < 100) y += 1900;
    if (d < 1 || m < 0 || y < 1970 || y > 2105) return 0;
    long days = 0;                                     /* jours depuis 1970 (calendrier civil) */
    int yy = y, mo = m + 1;
    if (mo <= 2) { yy--; mo += 12; }
    days = 365L * yy + yy / 4 - yy / 100 + yy / 400 + (153 * (mo - 3) + 2) / 5 + d - 719469L;
    long t = days * 86400L + hh * 3600L + mm * 60L + ss;
    return t > 0 ? (u32)t : 0;
}

static int ck_domain_match(const char *host, const char *dom) {
    size_t hl = strlen(host), dl = strlen(dom);
    if (!dl) return 0;
    if (hl == dl) return !strcasecmp(host, dom);
    return hl > dl && host[hl - dl - 1] == '.' && !strcasecmp(host + hl - dl, dom);
}

static void ck_save(void) {
    ck_dirty = 0;
    if (!sd_ok) return;
    FILE *f = fopen(CK_FILE, "w");
    if (!f) return;
    fputs("# Cookies de l'onglet WEB (effacables depuis l'accueil). Ne partage pas ce fichier.\n", f);
    u32 now = ck_now();
    for (int i = 0; i < CK_MAX; i++)
        if (cks[i].used && cks[i].expires > now)
            fprintf(f, "%s\t%d\t%s\t%d\t%lu\t%s\t%s\n", cks[i].domain, cks[i].host_only, cks[i].path, cks[i].secure,
                    (unsigned long)cks[i].expires, cks[i].name, cks[i].value);
    fclose(f);
}

static void ck_load(void) {
    if (ck_loaded) return;
    ck_loaded = 1;
    FILE *f = sd_ok ? fopen(CK_FILE, "r") : NULL;
    if (!f) return;
    char line[800];
    int n = 0;
    u32 now = ck_now();
    while (n < CK_MAX && fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        line[strcspn(line, "\r\n")] = 0;
        char *fld[7];
        int k = 0;
        for (char *p = line; k < 7; ) { fld[k++] = p; char *t = strchr(p, '\t'); if (!t) break; *t = 0; p = t + 1; }
        if (k != 7) continue;
        cookie_t *c = &cks[n];
        u32 ex = (u32)strtoul(fld[4], NULL, 10);
        if (ex <= now) continue;
        snprintf(c->domain, sizeof(c->domain), "%s", fld[0]);
        c->host_only = (u8)atoi(fld[1]);
        snprintf(c->path, sizeof(c->path), "%s", fld[2]);
        c->secure = (u8)atoi(fld[3]);
        c->expires = ex;
        snprintf(c->name, sizeof(c->name), "%s", fld[5]);
        snprintf(c->value, sizeof(c->value), "%s", fld[6]);
        c->used = 1;
        n++;
    }
    fclose(f);
}

static int ck_count(void) { int n = 0; for (int i = 0; i < CK_MAX; i++) n += cks[i].used; return n; }

static void ck_clear(void) {
    memset(cks, 0, sizeof(cks));
    ck_loaded = 1;
    if (sd_ok) remove(CK_FILE);
}

/* ligne "Set-Cookie:" recue de host pour la page path */
static void ck_set(const char *host, const char *path, int tls, const char *line) {
    ck_load();
    char buf[1100];
    snprintf(buf, sizeof(buf), "%s", line);
    char *semi = strchr(buf, ';');
    if (semi) *semi = 0;
    char *eq = strchr(buf, '=');
    if (!eq) return;
    *eq = 0;
    char *name = trim(buf), *value = trim(eq + 1);
    if (!name[0] || strlen(name) >= sizeof(cks[0].name) || strlen(value) >= sizeof(cks[0].value)) return;
    if (strpbrk(name, " \t\"")) return;
    char dom[80] = "", pth[64] = "";
    int host_only = 1, secure = 0, has_exp = 0;
    u32 expires = 0;
    long maxage = -1;
    int has_maxage = 0;
    for (char *a = semi ? semi + 1 : NULL; a && *a; ) {
        char *next = strchr(a, ';');
        if (next) *next = 0;
        char *v = strchr(a, '=');
        if (v) *v++ = 0;
        char *k = trim(a);
        if (!strcasecmp(k, "domain") && v) { v = trim(v); while (*v == '.') v++; snprintf(dom, sizeof(dom), "%s", v); }
        else if (!strcasecmp(k, "path") && v) snprintf(pth, sizeof(pth), "%s", trim(v));
        else if (!strcasecmp(k, "expires") && v) { expires = ck_parse_date(v); has_exp = 1; }
        else if (!strcasecmp(k, "max-age") && v) { maxage = atol(trim(v)); has_maxage = 1; }
        else if (!strcasecmp(k, "secure")) secure = 1;
        a = next ? next + 1 : NULL;
    }
    if (dom[0]) {
        for (char *p = dom; *p; p++) *p = (char)tolower((unsigned char)*p);
        if (!ck_domain_match(host, dom) || !strchr(dom, '.')) return;      /* domaine d'un autre site */
        host_only = 0;
    } else snprintf(dom, sizeof(dom), "%s", host);
    if (pth[0] != '/') {                                 /* dossier de la page */
        snprintf(pth, sizeof(pth), "%s", path[0] == '/' ? path : "/");
        pth[strcspn(pth, "?#")] = 0;
        char *sl = strrchr(pth, '/');
        if (sl && sl != pth) *sl = 0; else snprintf(pth, sizeof(pth), "/");
    }
    if (secure && !tls) return;
    u32 now = ck_now();
    if (has_maxage) expires = maxage <= 0 ? 1 : now + (u32)(maxage > 400L * 86400 ? 400L * 86400 : maxage);
    else if (!has_exp) expires = 0;                      /* cookie de session */
    int slot = -1, empty = -1, oldest = -1;
    for (int i = 0; i < CK_MAX; i++) {
        cookie_t *c = &cks[i];
        if (!c->used) { if (empty < 0) empty = i; continue; }
        if (!strcmp(c->name, name) && !strcasecmp(c->domain, dom) && !strcmp(c->path, pth)) { slot = i; break; }
        if (oldest < 0 || (c->expires && c->expires < cks[oldest].expires)) oldest = i;
    }
    int del = expires && expires <= now;
    if (del) { if (slot >= 0) { memset(&cks[slot], 0, sizeof(cookie_t)); ck_dirty = 1; ck_dirty_t = now_ms(); } return; }
    if (slot < 0) slot = empty >= 0 ? empty : oldest;
    if (slot < 0) return;
    cookie_t *c = &cks[slot];
    int was_persistent = c->used && c->expires;
    memset(c, 0, sizeof(*c));
    snprintf(c->domain, sizeof(c->domain), "%s", dom);
    snprintf(c->path, sizeof(c->path), "%s", pth);
    snprintf(c->name, sizeof(c->name), "%s", name);
    snprintf(c->value, sizeof(c->value), "%s", value);
    c->expires = expires;
    c->host_only = (u8)host_only;
    c->secure = (u8)secure;
    c->used = 1;
    if (expires || was_persistent) { ck_dirty = 1; ck_dirty_t = now_ms(); }
}

/* tous les "Set-Cookie:" d'une reponse */
static void ck_from_headers(const char *hdrs, const char *host, const char *path, int tls) {
    for (const char *p = hdrs; *p; ) {
        const char *eol = strstr(p, "\r\n");
        if (!eol) eol = p + strlen(p);
        if (!strncasecmp(p, "Set-Cookie:", 11)) {
            char line[1100];
            int n = (int)(eol - (p + 11));
            if (n > (int)sizeof(line) - 1) n = sizeof(line) - 1;
            memcpy(line, p + 11, n);
            line[n] = 0;
            ck_set(host, path, tls, trim(line));
        }
        if (!*eol) break;
        p = eol + 2;
    }
}

/* en-tete "Cookie: ..." a envoyer (vide si aucun) */
static void ck_header(const char *host, const char *path, int tls, char *out, int cap) {
    ck_load();
    out[0] = 0;
    int n = 0;
    u32 now = ck_now();
    for (int i = 0; i < CK_MAX; i++) {
        cookie_t *c = &cks[i];
        if (!c->used) continue;
        if (c->expires && c->expires <= now) { memset(c, 0, sizeof(*c)); continue; }
        if (c->secure && !tls) continue;
        if (c->host_only ? strcasecmp(host, c->domain) : !ck_domain_match(host, c->domain)) continue;
        size_t pl = strlen(c->path);
        if (strncmp(path, c->path, pl) || (pl > 1 && c->path[pl - 1] != '/' && path[pl] && path[pl] != '/' && path[pl] != '?')) continue;
        int need = (int)(strlen(c->name) + strlen(c->value) + 4);
        if (n + need + 12 >= cap) break;
        n += snprintf(out + n, cap - n, "%s%s=%s", n ? "; " : "Cookie: ", c->name, c->value);
    }
    if (n) snprintf(out + n, cap - n, "\r\n");
}

static void ck_pump(void) {
    if (ck_dirty && now_ms() - ck_dirty_t > 3000) ck_save();
}

/* ================= Fichier audio trouve sur une page ================= */
/* A : ecouter tout de suite (comme une radio ou un podcast)
   X : telecharger sur la carte SD   B : annuler */
static void podcast_play_url(const char *title, const char *url, long bytes);      /* tab_podcast.c */
static struct {
    int on;
    char url[URL_MAX], ctype[128], disp[300], name[160];
    unsigned char *body; int n;
    long clen; int chunked, live;
} au;

static int au_is_audio(const char *ctype, const char *url) {
    char path[URL_MAX];
    snprintf(path, sizeof(path), "%s", url);
    path[strcspn(path, "?#")] = 0;
    const char *dot = strrchr(path, '.');
    if (dot && strchr(dot, '/')) dot = NULL;
    static const char *const ext[] = { ".mp3", ".ogg", ".oga", ".aac", ".m4a", ".m4b", ".m3u", ".m3u8", ".pls", NULL };
    for (int i = 0; dot && ext[i]; i++) if (!strcasecmp(dot, ext[i])) return 1;
    return !strncmp(ctype, "audio/", 6) || strstr(ctype, "application/ogg") || strstr(ctype, "mpegurl") || strstr(ctype, "scpls");
}

static void au_clear(void) { free(au.body); memset(&au, 0, sizeof(au)); }

static void au_offer(const unsigned char *body, int n, const char *ctype, const char *disp, const char *url, long clen, int chunked, int icy) {
    au_clear();
    au.body = (unsigned char *)malloc(n > 0 ? n : 1);
    if (!au.body) { conn_close(PCONN); return; }
    if (n > 0) memcpy(au.body, body, n);
    au.n = n;
    snprintf(au.url, sizeof(au.url), "%s", url);
    snprintf(au.ctype, sizeof(au.ctype), "%s", ctype);
    snprintf(au.disp, sizeof(au.disp), "%s", disp);
    au.clen = clen;
    au.chunked = chunked;
    au.live = icy || clen <= 0 || strstr(ctype, "mpegurl") || strstr(ctype, "scpls") || ci_find(au.url, ".m3u") || ci_find(au.url, ".pls");
    char fn[128];
    dl_filename(disp, url, ctype, fn, sizeof(fn));
    snprintf(au.name, sizeof(au.name), "%s", display_name(fn));
    au.on = 1;
    crumb("web : fichier audio %s", au.name);
}

static void au_choose(u32 down) {
    if (down & PAD_BUTTON_A) {                          /* ecouter */
        conn_close(PCONN);
        if (au.live) {                                  /* flux : comme une radio d'essai */
            snprintf(stations[nstations].name, sizeof(stations[0].name), "%s", au.name);
            snprintf(stations[nstations].url, sizeof(stations[0].url), "%s", au.url);
            start_station(nstations, 0);
        } else podcast_play_url(au.name, au.url, au.clen);   /* fichier : avance / recul, reprise */
        web_note("Lecture en cours (L / R : reculer / avancer dans un fichier)", 0);
        au_clear();
    } else if (down & PAD_BUTTON_X) {                   /* telecharger */
        unsigned char *b = au.body;
        au.body = NULL;
        dl_begin(b, au.n, au.ctype, au.disp, au.url, au.chunked ? -1 : au.clen, au.chunked);
        free(b);
        au_clear();
    } else if (down & (PAD_BUTTON_B | PAD_BUTTON_Y)) {
        conn_close(PCONN);
        au_clear();
    }
}

static void draw_audio_offer(void) {
    if (!au.on) return;
    char t[200], fit[200];
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 130), RGBA(0, 0, 0, 130), RGBA(0, 0, 0, 130), RGBA(0, 0, 0, 130));
    gx_rrect_fill(110, 170, 420, 120, 10, RGBA(14, 8, 44, 255), RGBA(8, 5, 26, 255));
    draw_panel(110, 170, 420, 120);
    gx_text1(F_LARGE, 130, 202, au.live ? "Flux audio" : "Fichier audio", RGBA(255, 255, 255, 255));
    text_display(au.name, t, sizeof(t));
    fit_text(F_MED, t, 380, fit, sizeof(fit));
    gx_text1(F_MED, 130, 230, fit, RGBA(200, 190, 240, 255));
    draw_mini_badge(140, 262, "A"); gx_text1(F_MED, 154, 267, "\xC3\x89" "couter", RGBA(230, 222, 255, 255));
    if (!au.live) { draw_mini_badge(262, 262, "X"); gx_text1(F_MED, 276, 267, "T\xC3\xA9l\xC3\xA9" "charger", RGBA(230, 222, 255, 255)); }
    draw_mini_badge(420, 262, "B"); gx_text1(F_MED, 434, 267, "Annuler", RGBA(230, 222, 255, 255));
}

/* ======================= Liste deroulante (select) ====================== */
static int sp_field = -1, sp_sel = 0, sp_top = 0;

static const char *opt_label(int k) { return pg.pool + pg.opts[k * 2 + 1]; }
static const char *opt_value(int k) { return pg.pool + pg.opts[k * 2]; }

static void sp_open(int field) {
    if (field < 0 || field >= pg.nfields || pg.fields[field].nopt <= 0) return;
    sp_field = field;
    sp_sel = pg.fields[field].osel;
    sp_top = sp_sel > 4 ? sp_sel - 4 : 0;
}

static void sp_input(u32 down, u32 nav, int fire) {
    wfield_t *f = &pg.fields[sp_field];
    if (fire && (nav & PAD_BUTTON_DOWN) && sp_sel < f->nopt - 1) sp_sel++;
    if (fire && (nav & PAD_BUTTON_UP) && sp_sel > 0) sp_sel--;
    if (fire && (nav & PAD_BUTTON_RIGHT)) { sp_sel += 8; if (sp_sel > f->nopt - 1) sp_sel = f->nopt - 1; }
    if (fire && (nav & PAD_BUTTON_LEFT)) { sp_sel -= 8; if (sp_sel < 0) sp_sel = 0; }
    if (down & PAD_BUTTON_A) {
        f->osel = (s16)sp_sel;
        snprintf(f->value, sizeof(f->value), "%s", opt_value(f->opt0 + sp_sel));
        sp_field = -1;
    }
    if (down & (PAD_BUTTON_B | PAD_BUTTON_Y)) sp_field = -1;
}

static void draw_select_picker(void) {
    if (sp_field < 0 || sp_field >= pg.nfields) return;
    wfield_t *f = &pg.fields[sp_field];
    const int vis = 10;
    if (sp_sel < sp_top) sp_top = sp_sel;
    if (sp_sel >= sp_top + vis) sp_top = sp_sel - vis + 1;
    int rows = f->nopt < vis ? f->nopt : vis;
    f32 h = 30 + rows * 24, y = 240 - h / 2;
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 130), RGBA(0, 0, 0, 130), RGBA(0, 0, 0, 130), RGBA(0, 0, 0, 130));
    gx_rrect_fill(130, y, 380, h, 10, RGBA(14, 8, 44, 255), RGBA(8, 5, 26, 255));
    draw_panel(130, y, 380, h);
    char t[300], fit[200];
    for (int i = 0; i < rows; i++) {
        int k = sp_top + i;
        f32 ry = y + 16 + i * 24;
        if (k == sp_sel) gx_rrect_fill(140, ry, 360, 22, 6, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        text_display(opt_label(f->opt0 + k), t, sizeof(t));
        fit_text(F_MED, t, 340, fit, sizeof(fit));
        gx_text1(F_MED, 152, ry + 16, fit, k == f->osel ? RGBA(255, 220, 140, 255) : RGBA(235, 230, 255, 255));
    }
    if (f->nopt > vis) {
        char m[32];
        snprintf(m, sizeof(m), "%d / %d", sp_sel + 1, f->nopt);
        gx_text_r(F_SMALL, 500, y + h - 2, m, RGBA(150, 135, 210, 255));
    }
}

/* ========================== Pointeur au stick ========================== */
/* Le stick principal deplace une fleche sur la page ; elle selectionne le lien
   (ou le champ, le bouton) qu'elle survole, A l'ouvre. Le stick C fait defiler. */
static f32 ptr_x = WV_X + WV_W / 2, ptr_y = 116 + 272 / 2;
static u64 ptr_t = 0;

/* lien sous un point de l'ecran, -1 = aucun */
static int web_link_at(f32 x, f32 y) {
    x = WV_X + (x - WV_X) * web_kx;                       /* (16:9, Vitrine : coordonnees de la page) */
    int lo = 0, hi = pg.nitems;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (pg.items[mid].y + 8 < pg_scroll) lo = mid + 1; else hi = mid;
    }
    for (int i = lo; i < pg.nitems; i++) {
        witem_t *it = &pg.items[i];
        f32 by = (f32)(WV_Y + it->y - pg_scroll);
        if (by - IMG_MAX_H > WV_Y + WV_H) break;
        if (it->link < 0) continue;
        f32 bx = (f32)(WV_X + it->x), x1 = bx + it->w, y0, y1;
        if (it->style & WS_IMG) { y0 = by - it->h; y1 = by; }
        else { y0 = by - font_ascent[it->font] - 3; y1 = by - font_ascent[it->font] + font_height[it->font] + 3; }
        if (x >= bx - 2 && x <= x1 + 2 && y >= y0 && y <= y1) return it->link;
    }
    return -1;
}

/* renvoie 1 si le pointeur est actif (le stick principal ne defile plus) */
static int ptr_update(void) {
    int mx = PAD_StickX(0), my = PAD_StickY(0);
    int ax = abs(mx), ay = abs(my);
    if (ax > 18 || ay > 18) {
        if (!ptr_on) { ptr_on = 1; }
        ptr_t = now_ms();
        f32 sx = ax > 18 ? (f32)(mx > 0 ? mx - 18 : mx + 18) : 0, sy = ay > 18 ? (f32)(my > 0 ? my - 18 : my + 18) : 0;
        f32 k = 0.06f + (ax + ay > 120 ? 0.05f : 0);       /* plus vite quand on pousse a fond */
        ptr_x += sx * k * 1.4f;
        ptr_y -= sy * k * 1.4f;
        if (ptr_x < WV_X) ptr_x = WV_X;
        if (ptr_x > WV_X + WV_W) ptr_x = WV_X + WV_W;
        /* au bord haut / bas : la page defile */
        if (ptr_y < WV_Y + 6) { pg_scroll -= (s32)((WV_Y + 6 - ptr_y) * 0.6f) + 2; ptr_y = WV_Y + 6; web_scroll_clamp(); }
        if (ptr_y > WV_Y + WV_H - 6) { pg_scroll += (s32)((ptr_y - (WV_Y + WV_H - 6)) * 0.6f) + 2; ptr_y = WV_Y + WV_H - 6; web_scroll_clamp(); }
    } else if (ptr_on && now_ms() - ptr_t > 6000) ptr_on = 0;   /* repos : la fleche disparait */
    if (ptr_on) pg_sel = web_link_at(ptr_x, ptr_y);
    return ptr_on;
}

static void draw_pointer(void) {
    if (!ptr_on) return;
    f32 x = ptr_x, y = ptr_y;
    uxsave_t uz = ux_native(x);
    gx_tri(x + 1, y + 1, x + 1, y + 19, x + 14, y + 14, RGBA(0, 0, 0, 150));             /* ombre */
    gx_tri(x, y, x, y + 18, x + 13, y + 13, RGBA(255, 255, 255, 255));
    gx_tri(x + 2, y + 4, x + 2, y + 14, x + 9, y + 11, pg_sel >= 0 ? RGBA(150, 80, 255, 255) : RGBA(90, 80, 130, 255));
    ux_restore(uz);
}

/* ===================== Adresse envoyee par le telephone ===================== */
static void web_enter_tab(void);
static void web_open_from_phone(const char *t) {
    char u[URL_MAX];
    snprintf(u, sizeof(u), "%s", t);
    char *s = trim(u);
    if (!*s) return;
    tab = TAB_WEB;
    saver_on = 0;
    night_on = 0;
    web_enter_tab();
    crumb("telephone : ouvrir %.70s", s);
    snprintf(kb_text, sizeof(kb_text), "%s", s);       /* meme logique que la barre d'adresse */
    kb_cur = (int)strlen(kb_text);
    kb_mode = KB_URL;
    kb_validate();
}

/* fenetres par-dessus la page (fichier audio, liste deroulante) : 1 = touches prises */
static int web_overlay_input(u32 down, u32 nav, int fire) {
    if (au.on) { au_choose(down); return 1; }
    if (sp_field >= 0) {
        if (sp_field >= pg.nfields) { sp_field = -1; return 0; }
        sp_input(down, nav, fire);
        return 1;
    }
    return 0;
}

static void draw_web_overlays(void) {
    draw_pointer();
    draw_select_picker();
    draw_audio_offer();
}

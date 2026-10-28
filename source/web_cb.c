// GC Station v209 : l'onglet WEB affiche Cube Browser (source/cb : navigateur d'Arial-Bold, licence MIT,
// adapte pour vivre dans GC Station). Ce fichier (inclus dans main.c, apres assistant.c) donne au navigateur
// ce dont il a besoin (reseau, ecran, certificats, dossiers) et lui fait afficher les pages de GC Station :
// assistant IA (gc:ia), musique libre d'archive.org (gc:archive)... Les pages que GC Station fabriquait pour
// son ancien navigateur (web_show) passent maintenant par Cube Browser.

static void music_add_file(const char *path);            /* (plus bas) */
static int cbh_capture = 0;                              /* 1 : web_show garde la page (demandee par le navigateur) */
static char *cbh_cap = NULL;
/* pages faites par GC Station, gardees pour que le navigateur puisse les rouvrir (retour, recharger) */
#define CBH_CACHE 8
static struct { char url[URL_MAX]; char *html; } cbh_cache[CBH_CACHE];
static int cbh_cache_next = 0;

static void cbh_cache_put(const char *url, const char *html) {
    for (int i = 0; i < CBH_CACHE; i++)
        if (cbh_cache[i].html && !strcmp(cbh_cache[i].url, url)) { free(cbh_cache[i].html); cbh_cache[i].html = strdup(html); return; }
    int i = cbh_cache_next;
    cbh_cache_next = (cbh_cache_next + 1) % CBH_CACHE;
    free(cbh_cache[i].html);
    snprintf(cbh_cache[i].url, sizeof(cbh_cache[i].url), "%s", url);
    cbh_cache[i].html = strdup(html);
}
static const char *cbh_cache_get(const char *url) {
    for (int i = 0; i < CBH_CACHE; i++) if (cbh_cache[i].html && !strcmp(cbh_cache[i].url, url)) return cbh_cache[i].html;
    return NULL;
}

/* --- ce que le navigateur demande --- */
int cb_host_net_ready(void) { if (net_phase != NET_DONE) return 0; return net_ok ? 1 : -1; }
const char *cb_host_ip(void) { return ip_str; }
const char *cb_host_gateway(void) { return gw_str; }
const char *cb_host_cacert(size_t *len) { *len = sizeof(cacert_pem) - 1; return cacert_pem; }
const char *cb_host_data_dir(void) { return sd_ok ? GCD "/web" : ""; }
const char *cb_host_download_dir(void) { return DL_DIR; }
int cb_host_wide(void) { return scr_wide; }
void cb_host_screen(float *ox, float *oy, float *sx, float *sy, int *fb_w, int *efb_h, float *virt_x0) {
    f32 vw = scr_wide ? 854.0f : 640.0f;
    *ox = (float)scr_ox; *oy = (float)scr_oy;
    *sx = scr_sx * scr_w / vw;                            /* (points de l'ecran par unite du navigateur) */
    *sy = scr_sy;
    *fb_w = rmode->fbWidth; *efb_h = rmode->efbHeight;
    *virt_x0 = 0;
}
static int cbh_wants_start = 0;
static int am_open;                                      /* (lib_extra.c) */
u32 cb_host_reserved_buttons(void) { return PAD_TRIGGER_Z | (cbh_wants_start ? 0 : PAD_BUTTON_START); }
int cb_host_input_enabled(void) { return tab == TAB_WEB && !menu_open && !kb_open && !tl_screen && !ic_on && !am_open && !sh_on; }
void cb_host_restore_gx(void) {
    Mtx id;
    guMtxIdentity(id);
    GX_LoadPosMtxImm(id, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GX_LoadTexObj(&font_texobj, GX_TEXMAP0);              /* (le navigateur met ses polices en carte 0) */
    gx_mode = -1;
    scr_proj_dirty = 1;                                   /* (sa projection : on remet celle de GC Station) */
    scr_viewport();
}
void cb_host_open_menu(void) { menu_open = 1; }
void cb_host_exit(void) { tab = TAB_MUSIC; tab_enter(tab); }
void cb_host_note(const char *msg) { lib_note(msg); }
void cb_host_download_done(const char *path) {
    if (audio_ext_len(path)) music_add_file(path);       /* (la musique et l'onglet RETRO le voient tout de suite) */
}

static int cbh_hex(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void cbh_query(const char *url, const char *key, char *out, int cap) {   /* valeur d'un parametre, decodee */
    out[0] = 0;
    const char *q = strchr(url, '?');
    if (!q) return;
    size_t kl = strlen(key);
    for (const char *p = q + 1; *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1;
            int o = 0;
            while (*p && *p != '&' && o < cap - 1) {
                if (*p == '+') { out[o++] = ' '; p++; }
                else if (*p == '%' && cbh_hex(p[1]) >= 0 && cbh_hex(p[2]) >= 0) { out[o++] = (char)(cbh_hex(p[1]) * 16 + cbh_hex(p[2])); p += 3; }
                else out[o++] = *p++;
            }
            out[o] = 0;
            return;
        }
        p = strchr(p, '&');
        if (!p) break;
        p++;
    }
}
static char *cbh_wait_page(const char *what) {
    char t[400];
    snprintf(t, sizeof(t), "<html><head><title>GC Station</title></head><body><h2>%s</h2><p>Un instant...</p></body></html>", what);
    return strdup(t);
}

/* page "gc:..." demandee par le navigateur (lien, formulaire, retour) */
char *cb_host_internal(const char *url, const char *post, size_t *len) {
    (void)post;
    char *r = NULL;
    const char *c = cbh_cache_get(url);
    if (!strncmp(url, "gc:http", 7)) {                    /* donnees lues par GC Station (archive.org) */
        if (c) r = strdup(c);
        else { web_fetch(url + 3, NAV_PUSH, 0); r = cbh_wait_page("Chargement"); }
    } else {
        cbh_capture = 1;
        free(cbh_cap); cbh_cap = NULL;
        if (!strncmp(url, "gc:ia-envoyer", 13)) {         /* question a l'assistant (une seule fois : retour = pas de 2e envoi) */
            static char last[URL_MAX];
            char q[1200];
            cbh_query(url, "q", q, sizeof(q));
            if (q[0] && strcmp(last, url)) { snprintf(last, sizeof(last), "%s", url); ia_ask(q); }
            else ia_render(NAV_REPLACE, 0);
        } else web_go(url, NAV_PUSH);
        cbh_capture = 0;
        r = cbh_cap; cbh_cap = NULL;
        if (!r && c) r = strdup(c);
        if (!r && !strcmp(url, "gc:archive")) r = cbh_wait_page("Musique libre : tape ta recherche");
    }
    if (r) { cbh_cache_put(url, r); *len = strlen(r); }
    return r;
}

/* une page faite par GC Station (ancien web_show) : au navigateur */
static void cbh_show(unsigned char *data, int len, const char *url, int kind) {
    char *html = NULL;
    if (kind == WK_ARCHIVE || kind == WK_ARCHMETA) {
        html = kind == WK_ARCHIVE ? archive_to_html((const char *)data, len) : archive_meta_to_html((const char *)data, len, url);
        free(data);
    } else if (kind == WK_TEXT) {
        sbuf_t s = { NULL, 0, 0 };
        sb_add(&s, "<html><body><pre>");
        char *t = (char *)malloc(len + 1);
        if (t) { memcpy(t, data, len); t[len] = 0; sb_esc(&s, t); free(t); }
        sb_add(&s, "</pre></body></html>");
        html = s.b;
        free(data);
    } else {
        html = (char *)realloc(data, len + 1);
        if (html) html[len] = 0; else free(data);
    }
    if (!html) return;
    if (cbh_capture) { free(cbh_cap); cbh_cap = html; return; }
    char u[URL_MAX];
    snprintf(u, sizeof(u), "%s%s", strncmp(url, "gc:", 3) ? "gc:" : "", url);
    cbh_cache_put(u, html);
    free(html);
    if (tab != TAB_WEB && tab_shown(TAB_WEB)) { tab = TAB_WEB; tab_enter(tab); }
    const char *cur = cb_current_url();
    if (cur && (!strcmp(cur, u) || (!strncmp(u, "gc:ia", 5) && !strncmp(cur, "gc:ia", 5)))) cb_reload_internal(cur);   /* (reponse de l'assistant : meme page) */
    else cb_navigate(u);
}

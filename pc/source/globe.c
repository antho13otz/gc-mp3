// GC Station v207 : le globe des radios (inclus dans main.c, avant tools.c).
// Onglet RADIO > "Globe des radios" : la Terre en 3D ; chaque point lumineux est une radio (annuaire
// radio-browser.info, ~3500 stations placees sur la carte). Stick : tourner ; stick C : zoom ; A : ecouter la
// radio visee ; L / R : les autres radios du meme endroit ; X : une radio au hasard (le globe y va) ;
// Y : garder dans mes radios ; Z : chercher (radio, ville, pays) ; B : fermer.
// v213 : chargement bien plus rapide (lecture du JSON en une passe, page suivante demandee pendant la lecture,
// doublons enleves, prechargement des l'onglet RADIO, vieille liste gardee pendant sa mise a jour) ;
// navigation douce (inertie, vol d'un point a l'autre, viseur aimante, noms des radios proches, recherche).
// Les stations sont gardees sur la carte SD (SD:/gc-mp3/reglages/globe.bin), mises a jour chaque semaine.

#define GB_FILE  GCD_REG "/globe.bin"
#define GB_PAGES 12
#define GB_PAGE  300                            /* (v210 : petites pages : les premieres radios arrivent vite) */
#define GB_STALE (7L * 24 * 3600)              /* (v213 : liste plus vieille : mise a jour en fond) */
typedef struct { f32 x, y, z; f32 lat, lon; u32 name, url, place; u16 kbps; char cc[3]; u8 hls; u8 seen; } gbst_t;
static gbst_t *gb = NULL;
static int gb_n = 0, gb_cap = 0;
static char *gb_pool = NULL;
static int gb_pool_n = 0, gb_pool_cap = 0;
static int gb_loaded = 0;                       /* 1 = stations en memoire */
enum { GBL_IDLE, GBL_LOAD, GBL_DONE, GBL_ERR, GBL_FILE };
static int gb_state = GBL_IDLE, gb_page = 0, gb_srv = 0;
static hfetch_t gb_hf = { .c = { -1, 0, 0 } };
static f32 gb_lon = 2.35f, gb_lat = 30.0f, gb_r = 168.0f;   /* vue : centre (degres), rayon du globe (points) */
static f32 gb_vx = 0, gb_vy = 0, gb_vz = 0;     /* v213 : vitesses (inertie : demarrages et arrets en douceur) */
static int gb_travel = 0, gb_travel_play = -1;
static f32 gb_t0x, gb_t0y, gb_t0z, gb_t1x, gb_t1y, gb_t1z, gb_tom, gb_tt, gb_tdur, gb_tr0, gb_tr1;   /* v213 : vol */
static u64 gb_idle_t = 0, gb_stick_t = 0;
static int gb_pick = -1, gb_pick_k = 0;         /* radio visee ; L / R : la k-ieme du meme endroit */
static int gb_playing = -1;                     /* radio du globe en cours d'ecoute */
static char gb_msg[160]; static u64 gb_msg_t = 0;
static int gb_refresh = 0;                      /* v213 : mise a jour d'une liste deja affichee */
static int gb_open_now = 0;
static int gb_mag = -1;                         /* v213 : radio la plus proche du viseur (aimant) */
#define GB_SRES 200
static int gb_sres[GB_SRES], gb_sn = 0, gb_sk = 0, gb_force = -1;   /* v213 : resultats de recherche */
static char gb_sq[64];

static void gb_map_free(void);
static u8 *volatile gb_map_raw = NULL;          /* (decompressee par le fil ; reprise par la boucle) */
static f32 *gb_dx = NULL, *gb_dy = NULL; static u8 *gb_da = NULL; static int gb_dcap = 0;   /* (points a l'ecran : rendus a la fermeture) */
static void gb_net_start(void);
static volatile int gb_thr_busy = 0, gb_thr_file = 0, gb_thr_file_ok = 0, gb_thr_map = 0, gb_thr_stale = 0, gb_thr_file_done = 0;
static int gb_want = 0, gb_stale_todo = 0, gb_resume = 0;   /* (gb_resume : chargement a reprendre a la page gb_page) */     /* v213 : radios voulues (onglet RADIO) ; liste a mettre a jour */
static int gb_screen_on(void);
static void gb_note(const char *m) { snprintf(gb_msg, sizeof(gb_msg), "%s", m); gb_msg_t = now_ms(); }
static u32 gb_str(const char *s) {              /* -> position dans le reservoir de textes */
    int l = (int)strlen(s) + 1;
    if (gb_pool_n + l > gb_pool_cap) {
        int nc = gb_pool_cap ? gb_pool_cap * 2 : 128 * 1024;
        while (nc < gb_pool_n + l) nc *= 2;
        char *np = (char *)realloc(gb_pool, nc);
        if (!np) return 0;
        gb_pool = np; gb_pool_cap = nc;
    }
    memcpy(gb_pool + gb_pool_n, s, l);
    gb_pool_n += l;
    return (u32)(gb_pool_n - l);
}
static const char *gb_s(u32 o) { return gb_pool && o < (u32)gb_pool_n ? gb_pool + o : ""; }
static void gb_xyz(gbst_t *e) {
    f32 la = e->lat * (f32)M_PI / 180.0f, lo = e->lon * (f32)M_PI / 180.0f;
    e->x = cosf(la) * cosf(lo); e->y = cosf(la) * sinf(lo); e->z = sinf(la);
}

/* --- v213 : doublons (meme adresse, ou meme nom au meme endroit : souvent plusieurs debits d'une radio) --- */
#define GB_HS 16384
static int *gb_hu = NULL, *gb_hn = NULL;        /* case -> rang + 1 */
static u32 gb_fnv(const char *s, u32 h) { while (*s) { u8 c = (u8)*s++; if (c >= 'A' && c <= 'Z') c += 32; h = (h ^ c) * 16777619u; } return h; }
static u32 gb_key_u(const char *u) { return gb_fnv(u, 2166136261u); }
static u32 gb_key_n(const char *nm, f32 lat, f32 lon) {
    return gb_fnv(nm, 2166136261u) ^ ((u32)(int)floorf(lat * 4) * 73856093u) ^ ((u32)(int)floorf(lon * 4) * 19349663u);
}
static int gb_hfind(int *t, u32 k, int what, const char *s, f32 lat, f32 lon) {
    if (!t) return -1;
    for (u32 j = k & (GB_HS - 1); t[j]; j = (j + 1) & (GB_HS - 1)) {
        int i = t[j] - 1;
        if (i >= gb_n) continue;
        if (what == 0 ? !strcasecmp(gb_s(gb[i].url), s)
                      : !strcasecmp(gb_s(gb[i].name), s) && floorf(gb[i].lat * 4) == floorf(lat * 4) && floorf(gb[i].lon * 4) == floorf(lon * 4)) return i;
    }
    return -1;
}
static void gb_hput(int *t, u32 k, int i) {
    if (!t) return;
    u32 j = k & (GB_HS - 1);
    while (t[j]) j = (j + 1) & (GB_HS - 1);
    t[j] = i + 1;
}
static void gb_hrebuild(void) {
    if (!gb_hu) gb_hu = (int *)malloc(sizeof(int) * GB_HS);
    if (!gb_hn) gb_hn = (int *)malloc(sizeof(int) * GB_HS);
    if (!gb_hu || !gb_hn) { free(gb_hu); free(gb_hn); gb_hu = gb_hn = NULL; return; }
    memset(gb_hu, 0, sizeof(int) * GB_HS); memset(gb_hn, 0, sizeof(int) * GB_HS);
    for (int i = 0; i < gb_n && i < GB_HS / 2; i++) {
        gb_hput(gb_hu, gb_key_u(gb_s(gb[i].url)), i);
        gb_hput(gb_hn, gb_key_n(gb_s(gb[i].name), gb[i].lat, gb[i].lon), i);
    }
}
static void gb_clear(void) {
    free(gb); gb = NULL; gb_n = gb_cap = 0;
    free(gb_pool); gb_pool = NULL; gb_pool_n = gb_pool_cap = 0;
    gb_str("");                                  /* (position 0 : texte vide) */
    if (gb_hu) memset(gb_hu, 0, sizeof(int) * GB_HS);
    if (gb_hn) memset(gb_hn, 0, sizeof(int) * GB_HS);
    gb_playing = -1; gb_pick = -1; gb_force = -1; gb_sn = 0;
}
/* -> rang de la station (nouvelle, ou celle qui existait deja) ; -1 : plus de memoire */
static int gb_add(const gbst_t *e0, const char *name, const char *url, const char *place) {
    if (!gb_hu) gb_hrebuild();
    u32 ku = gb_key_u(url), kn = gb_key_n(name, e0->lat, e0->lon);
    int d = gb_hfind(gb_hu, ku, 0, url, 0, 0);
    if (d < 0) d = gb_hfind(gb_hn, kn, 1, name, e0->lat, e0->lon);
    if (d >= 0) { gb[d].seen = 1; return d; }
    if (gb_n == gb_cap) {
        int nc = gb_cap ? gb_cap * 2 : 1024;
        gbst_t *nb = (gbst_t *)realloc(gb, sizeof(gbst_t) * nc);
        if (!nb) return -1;
        gb = nb; gb_cap = nc;
    }
    gbst_t e = *e0;
    e.name = gb_str(name); e.url = gb_str(url); e.place = gb_str(place);
    e.seen = 1;
    gb_xyz(&e);
    gb[gb_n] = e;
    if (gb_n < GB_HS / 2) { gb_hput(gb_hu, ku, gb_n); gb_hput(gb_hn, kn, gb_n); }
    return gb_n++;
}
/* v213 : fin d'une mise a jour : les radios disparues de l'annuaire sont enlevees (textes recopies) */
static void gb_compact(void) {
    int keep = 0;
    for (int i = 0; i < gb_n; i++) keep += gb[i].seen;
    if (keep == gb_n || keep < 200) return;
    gbst_t *nb = (gbst_t *)malloc(sizeof(gbst_t) * keep);
    char *np = (char *)malloc(gb_pool_n + 1);
    if (!nb || !np) { free(nb); free(np); return; }
    int n = 0, pn = 1, play = -1;
    np[0] = 0;
    for (int i = 0; i < gb_n; i++) {
        if (!gb[i].seen) continue;
        nb[n] = gb[i];
        u32 *f[3] = { &nb[n].name, &nb[n].url, &nb[n].place };
        for (int k = 0; k < 3; k++) { const char *t = gb_s(*f[k]); int l = (int)strlen(t) + 1; memcpy(np + pn, t, l); *f[k] = (u32)pn; pn += l; }
        if (i == gb_playing) play = n;
        n++;
    }
    free(gb); free(gb_pool);
    gb = nb; gb_n = gb_cap = n;
    gb_pool = np; gb_pool_n = pn; gb_pool_cap = gb_pool_n + 1;
    gb_playing = play; gb_pick = -1; gb_force = -1; gb_sn = 0;
    gb_hrebuild();
}

/* --- carte SD --- */
typedef struct { f32 lat, lon; u32 name, url, place; u16 kbps; char cc[3]; u8 hls; } gbrec_t;
static void gb_save(void) {                     /* (ecrit par le fil de fond : pas d'image figee) */
    if (!sd_ok || !gb_n) return;
    long n = 16 + (long)sizeof(gbrec_t) * gb_n + gb_pool_n;
    unsigned char *d = (unsigned char *)malloc(n);
    if (!d) return;
    u32 h[4] = { 0x47434742u, 1, (u32)gb_n, (u32)gb_pool_n };   /* "GCGB" */
    memcpy(d, h, 16);
    gbrec_t *r = (gbrec_t *)(d + 16);
    for (int i = 0; i < gb_n; i++) {
        memset(&r[i], 0, sizeof(r[i]));
        r[i].lat = gb[i].lat; r[i].lon = gb[i].lon; r[i].name = gb[i].name; r[i].url = gb[i].url; r[i].place = gb[i].place;
        r[i].kbps = gb[i].kbps; memcpy(r[i].cc, gb[i].cc, 3); r[i].hls = gb[i].hls;
    }
    memcpy(d + 16 + sizeof(gbrec_t) * gb_n, gb_pool, gb_pool_n);
    if (!bgf_own(BGF_REPL, 1, GB_FILE, d, n)) free(d);
}
/* (fil de fond) -> 1 : stations lues ; *stale = plus vieilles qu'une semaine */
static gbst_t *gbf_st = NULL; static char *gbf_pool = NULL; static int gbf_n = 0, gbf_pool_n = 0;
static int gb_load_file(int *stale) {
    *stale = 0;
    if (!sd_ok) return 0;
    struct stat st;
    if (stat(GB_FILE, &st) != 0) return 0;
    time_t now = time(NULL);
    if (now > 1600000000 && st.st_mtime > 0 && now - st.st_mtime > GB_STALE) *stale = 1;   /* (v213 : gardee, mise a jour en fond) */
    FILE *f = fopen(GB_FILE, "rb");
    if (!f) return 0;
    u32 h[4];
    gbst_t *a = NULL; char *pool = NULL; int n = 0;
    int ok = fread(h, sizeof(h), 1, f) == 1 && h[0] == 0x47434742u && h[1] == 1 && h[2] > 0 && h[2] < 20000 && h[3] < (8u << 20);
    if (ok) {
        a = (gbst_t *)malloc(sizeof(gbst_t) * h[2]);
        pool = (char *)malloc(h[3] + 1);
        ok = a && pool;
        gbrec_t *rr = ok ? (gbrec_t *)malloc(sizeof(gbrec_t) * h[2]) : NULL;   /* (tout d'un coup : bien plus rapide) */
        if (ok && rr && fread(rr, sizeof(gbrec_t), h[2], f) == h[2]) {
            for (u32 i = 0; i < h[2]; i++) {
                const gbrec_t *r = &rr[i];
                gbst_t e; memset(&e, 0, sizeof(e));
                e.lat = r->lat; e.lon = r->lon; e.name = r->name; e.url = r->url; e.place = r->place; e.kbps = r->kbps;
                memcpy(e.cc, r->cc, 3); e.hls = r->hls;
                if (e.name >= h[3] || e.url >= h[3] || e.place >= h[3]) { ok = 0; break; }
                gb_xyz(&e);
                a[n++] = e;
            }
        } else ok = 0;
        free(rr);
        if (ok) ok = fread(pool, 1, h[3], f) == h[3];
        if (ok) { pool[h[3]] = 0; if (h[3]) pool[h[3] - 1] = 0; }
    }
    fclose(f);
    if (!ok) { free(a); free(pool); return 0; }
    gbf_st = a; gbf_pool = pool; gbf_n = n; gbf_pool_n = (int)h[3];
    return 1;
}
static void gb_take_file(void) {                 /* (boucle : les stations lues par le fil deviennent la liste) */
    free(gb); free(gb_pool);
    gb = gbf_st; gb_n = gb_cap = gbf_n;
    gb_pool = gbf_pool; gb_pool_n = gbf_pool_n; gb_pool_cap = gbf_pool_n + 1;
    gbf_st = NULL; gbf_pool = NULL;
    gb_playing = -1; gb_pick = -1; gb_force = -1; gb_sn = 0;
    free(gb_hu); free(gb_hn); gb_hu = gb_hn = NULL;   /* (tables des doublons : faites seulement si l'annuaire est relu) */
}

/* --- annuaire (pages de 300 stations, les plus ecoutees d'abord) --- */
static void gb_fetch(void) {
    char url[URL_MAX];
    snprintf(url, sizeof(url), RB_SCHEME "://%s/json/stations/search?has_geo_info=true&hidebroken=true&order=clickcount&reverse=true&limit=%d&offset=%d",
             rb_servers[gb_srv], GB_PAGE, gb_page * GB_PAGE);
    gb_hf.image = 0;
    gb_hf.maxbytes = 3 * 1024 * 1024;
    gb_hf.accept = "application/json";
    gb_hf.tmo_ms = 30000;
    gb_hf.gz = 1;                                    /* (v209 : compresse : 5 a 8 fois moins a recevoir) */
    hf_start(&gb_hf, url, NULL);
    gb_state = GBL_LOAD;
}
/* v213 : une station lue en une seule passe (avant : chaque champ cherche depuis le debut : 10 fois plus lent) */
typedef struct { char codec[16], url[URL_MAX], url2[URL_MAX], nm[200], la[32], lo[32], kb[16], hls[8], st[80], co[80], cc[8]; } gbrow_t;
static gbrow_t gb_row;
static const char *gb_read_obj(const char *p) {           /* p sur '{' -> apres '}' */
    gbrow_t *r = &gb_row;
    r->codec[0] = r->url[0] = r->url2[0] = r->nm[0] = r->la[0] = r->lo[0] = r->kb[0] = r->hls[0] = r->st[0] = r->co[0] = r->cc[0] = 0;
    p = js_ws(p + 1);
    while (*p == '"') {
        const char *k = p + 1, *ke = k;
        while (*ke && *ke != '"') ke++;
        if (!*ke) return ke;
        int kl = (int)(ke - k);
        p = js_ws(ke + 1);
        if (*p == ':') p = js_ws(p + 1);
        char *dst = NULL; int cap = 0;
#define GBK(name, f) else if (kl == (int)sizeof(name) - 1 && !memcmp(k, name, kl)) { dst = r->f; cap = sizeof(r->f); }
        if (0) {}
        GBK("codec", codec) GBK("url_resolved", url) GBK("url", url2) GBK("name", nm) GBK("geo_lat", la) GBK("geo_long", lo)
        GBK("bitrate", kb) GBK("hls", hls) GBK("state", st) GBK("country", co) GBK("countrycode", cc)
#undef GBK
        const char *e = js_skip(p);
        if (dst) {
            if (*p == '"') js_str(p, dst, cap);
            else { int n = (int)(e - p); if (n > cap - 1) n = cap - 1; memcpy(dst, p, n); dst[n] = 0; }
        }
        p = js_ws(e);
        if (*p == ',') p = js_ws(p + 1);
    }
    if (*p == '}') p++;
    return p;
}
static char *gb_js = NULL;                      /* page recue, lue petit a petit (quelques ms par image) */
static const char *gb_jp = NULL;
static int gb_jgot = 0, gb_short = 0;
static int gb_parse_some(u32 budget_us) {       /* -> 1 quand la page est finie */
    const char *p = js_ws(gb_jp);
    if (*p == '[') p = js_ws(p + 1);                    /* (debut de la liste) */
    u32 t0 = perf_us();
    int got = 0;
    while (*p == '{') {
        if ((got & 7) == 7 && perf_us() - t0 > budget_us) break;
        p = gb_read_obj(p);
        got++; gb_jgot++;
        gbrow_t *r = &gb_row;
        if (!r->url[0]) memcpy(r->url, r->url2, sizeof(r->url));
        int ok = (!strcasecmp(r->codec, "MP3") || !strncasecmp(r->codec, "AAC", 3) || !strcasecmp(r->codec, "OGG")) &&
                 !strncasecmp(r->url, "http", 4) && strlen(r->url) < URL_MAX - 1 && !ci_find(r->url, "opus") && !ci_find(r->url, "flac") &&
                 r->la[0] && r->lo[0] && strcmp(r->la, "null") && strcmp(r->lo, "null");
        if (ok) {
            gbst_t e; memset(&e, 0, sizeof(e));
            e.lat = (f32)atof(r->la); e.lon = (f32)atof(r->lo);
            if (e.lat >= -90 && e.lat <= 90 && e.lon >= -180 && e.lon <= 180 && (e.lat != 0 || e.lon != 0)) {
                char *tn = trim(r->nm);
                for (char *c = tn; *c; c++) if (*c == '|') *c = '/';
                char place[170];
                char *ts = trim(r->st), *tc = trim(r->co);
                snprintf(place, sizeof(place), "%s%s%s", ts, ts[0] && tc[0] ? ", " : "", tc);
                e.kbps = (u16)atoi(r->kb);
                e.hls = r->hls[0] == '1' || r->hls[0] == 't';
                memcpy(e.cc, r->cc, 2);
                gb_add(&e, tn[0] ? tn : "Radio", r->url, place);
            }
        }
        p = js_ws(p);
        if (*p == ',') p = js_ws(p + 1);
    }
    gb_jp = p;
    return *p != '{';
}
static void gb_finish(void) {
    if (gb_refresh) { gb_compact(); gb_refresh = 0; }
    if (gb_n > 0 && gb_cap > gb_n) {                 /* v213 : juste la place utilisee (la memoire de la console est comptee) */
        gbst_t *nb = (gbst_t *)realloc(gb, sizeof(gbst_t) * gb_n);
        if (nb) { gb = nb; gb_cap = gb_n; }
    }
    if (gb_pool_n > 0 && gb_pool_cap > gb_pool_n + 1) {
        char *np = (char *)realloc(gb_pool, gb_pool_n + 1);
        if (np) { gb_pool = np; gb_pool_cap = gb_pool_n + 1; }
    }
    gb_loaded = gb_n > 0;
    gb_state = gb_loaded ? GBL_DONE : GBL_ERR;
    if (gb_loaded) gb_save();
}
static void gb_page_parsed(void) {
    free(gb_js); gb_js = NULL; gb_jp = NULL;
    if (gb_n > 0) gb_loaded = 1;                     /* (v210 : on peut deja viser et ecouter pendant la suite) */
    if (gb_jgot < GB_PAGE) gb_short = 1;             /* (derniere page de l'annuaire) */
    if (gb_short || gb_hf.st == HF_IDLE) {               /* (plus rien a venir) */
        if (gb_hf.st != HF_IDLE) { hf_abort(&gb_hf); hf_drop(&gb_hf); gb_hf.st = HF_IDLE; }
        gb_finish();
    }
}
/* v213 : la page suivante est demandee des que la precedente est arrivee (reseau et lecture en meme temps) */
static void gb_pump(u32 budget_us) {
    if (gb_state == GBL_FILE && gb_thr_file && gb_thr_file_done) {   /* (fil : stations de la carte SD lues, ou rien) */
        gb_thr_file = 0;
        if (gb_thr_file_ok) {
            gb_take_file();
            gb_loaded = 1; gb_state = GBL_DONE;
            gb_stale_todo = gb_thr_stale;                 /* (vieille liste : mise a jour en fond, la liste reste) */
        } else if (net_phase == NET_DONE && net_ok) { gb_state = GBL_IDLE; gb_net_start(); }
        else gb_state = GBL_IDLE;                         /* (reseau pas pret : gb_bg reessaie) */
    }
    if (gb_stale_todo && gb_state == GBL_DONE && net_phase == NET_DONE && net_ok) { gb_stale_todo = 0; gb_refresh = 1; gb_net_start(); }
    if (gb_state != GBL_LOAD) return;
    if (gb_js && gb_parse_some(budget_us)) gb_page_parsed();
    if (gb_state != GBL_LOAD) return;
    if (gb_hf.st == HF_IDLE) { if (!gb_js) gb_finish(); return; }
    hf_pump(&gb_hf);                                      /* (la page suivante arrive pendant la lecture de celle-ci) */
    if (gb_hf.st == HF_FAIL) {
        if (gb_page == 0 && !gb_js && !gb_n && rb_servers[gb_srv + 1]) { gb_srv++; gb_fetch(); return; }
        hf_drop(&gb_hf); gb_hf.st = HF_IDLE;
        if (gb_js) return;                                /* (on finit de lire ce qui est arrive) */
        if (gb_n > 0) { gb_finish(); return; }            /* (ce qui est arrive suffit) */
        gb_state = GBL_ERR; gb_refresh = 0;
        return;
    }
    if (gb_hf.st != HF_DONE || gb_js) return;
    gb_hf.st = HF_IDLE;
    gb_jgot = 0;
    if (gb_hf.buf) {
        char *js = (char *)realloc(gb_hf.buf, gb_hf.len + 1);
        if (js) { js[gb_hf.len] = 0; gb_js = js; gb_jp = js; gb_hf.buf = NULL; gb_hf.len = gb_hf.cap = 0; }
    }
    hf_drop(&gb_hf);
    gb_page++;
    if (gb_page < GB_PAGES && !gb_short) gb_fetch();      /* (la suivante, pendant qu'on lit celle-ci) */
    if (!gb_js) { gb_short = 1; gb_page_parsed(); }
}

/* --- projection --- */
static f32 gb_cl0, gb_sl0, gb_co0, gb_so0;     /* cos / sin de la latitude et de la longitude du centre */
static void gb_view(void) {
    f32 la = gb_lat * (f32)M_PI / 180.0f, lo = gb_lon * (f32)M_PI / 180.0f;
    gb_cl0 = cosf(la); gb_sl0 = sinf(la); gb_co0 = cosf(lo); gb_so0 = sinf(lo);
}
/* point (vecteur unite) -> ecran ; rend la profondeur (> 0 : face visible) */
static inline f32 gb_proj(f32 x, f32 y, f32 z, f32 *sx, f32 *sy) {
    f32 xr = x * gb_co0 + y * gb_so0, yr = -x * gb_so0 + y * gb_co0;
    f32 d = xr * gb_cl0 + z * gb_sl0, v = -xr * gb_sl0 + z * gb_cl0;
    *sx = 320.0f + yr * gb_r / ux_k;                 /* (16:9 : la Terre reste ronde, comme les cercles) */
    *sy = 228.0f - v * gb_r;
    return d;
}
#define GB_CX 320.0f
#define GB_CY 228.0f
static void gb_latlon_v(f32 lat, f32 lon, f32 *x, f32 *y, f32 *z) {
    f32 la = lat * (f32)M_PI / 180.0f, lo = lon * (f32)M_PI / 180.0f;
    *x = cosf(la) * cosf(lo); *y = cosf(la) * sinf(lo); *z = sinf(la);
}
static int gb_nearv[16], gb_nn = 0;             /* (radios pres du viseur, triees : calculees avec les points) */

static void gb_play(int i) {
    if (i < 0 || i >= gb_n || nstations >= MAX_STATIONS) return;
    snprintf(stations[nstations].name, sizeof(stations[0].name), "%s", gb_s(gb[i].name));
    snprintf(stations[nstations].url, sizeof(stations[0].url), "%s", gb_s(gb[i].url));
    start_station(nstations, 0);
    gb_playing = i;
}
static void gb_fav(int i) {
    if (i < 0 || i >= gb_n) return;
    const char *u = gb_s(gb[i].url), *nm = gb_s(gb[i].name);
    for (int k = 0; k < nstations; k++) if (!strcmp(stations[k].url, u)) { gb_note("D\xC3\xA9j\xC3\xA0 dans tes radios"); return; }
    if (nstations >= MAX_STATIONS - 1) { gb_note("Liste de radios pleine"); return; }
    snprintf(stations[nstations].name, sizeof(stations[0].name), "%s", nm);
    snprintf(stations[nstations].url, sizeof(stations[0].url), "%s", u);
    nstations++;
    if (sd_ok) { FILE *f = fopen(RADIO_LIST, "a"); if (f) { fprintf(f, "%s | %s\n", nm, u); fclose(f); } }
    char m[160];
    snprintf(m, sizeof(m), "Ajout\xC3\xA9" "e \xC3\xA0 tes radios : %s", nm);
    gb_note(m);
}

/* --- v213 : vol d'un point a l'autre (arc de grand cercle ; la Terre recule un peu pendant les longs vols) --- */
static void gb_fly(f32 lat, f32 lon, f32 r_end, int play) {
    gb_latlon_v(gb_lat, gb_lon, &gb_t0x, &gb_t0y, &gb_t0z);
    gb_latlon_v(lat, lon, &gb_t1x, &gb_t1y, &gb_t1z);
    f32 c = gb_t0x * gb_t1x + gb_t0y * gb_t1y + gb_t0z * gb_t1z;
    if (c > 1) c = 1;
    if (c < -1) c = -1;
    gb_tom = acosf(c);
    gb_tt = 0;
    gb_tdur = 0.7f + gb_tom * 0.55f;                  /* (s : ~2,4 s pour l'autre cote de la Terre) */
    gb_tr0 = gb_r; gb_tr1 = r_end;
    gb_travel = 1; gb_travel_play = play;
    gb_vx = gb_vy = gb_vz = 0;
}
static void gb_fly_step(void) {
    gb_tt += ui_dt / gb_tdur;
    if (gb_tt > 1) gb_tt = 1;
    f32 e = gb_tt * gb_tt * (3 - 2 * gb_tt);
    f32 x, y, z;
    if (gb_tom < 0.001f) { x = gb_t1x; y = gb_t1y; z = gb_t1z; }
    else {
        f32 s = sinf(gb_tom), a = sinf((1 - e) * gb_tom) / s, b = sinf(e * gb_tom) / s;
        x = a * gb_t0x + b * gb_t1x; y = a * gb_t0y + b * gb_t1y; z = a * gb_t0z + b * gb_t1z;
    }
    gb_lat = asinf(z < -1 ? -1 : z > 1 ? 1 : z) * 180.0f / (f32)M_PI;
    gb_lon = atan2f(y, x) * 180.0f / (f32)M_PI;
    f32 dip = gb_tom / (f32)M_PI * 0.55f;            /* (recul : jusqu'a 55 % pour les plus longs vols) */
    f32 rb = gb_tr0 + (gb_tr1 - gb_tr0) * e;
    gb_r = rb * (1 - dip * sinf((f32)M_PI * e));
    if (gb_r < 120) gb_r = 120;
    if (gb_tt >= 1) {
        gb_travel = 0;
        if (gb_travel_play >= 0) { gb_play(gb_travel_play); gb_pick_k = 0; }
        gb_travel_play = -1;
    }
}

/* --- v213 : recherche (Z) : nom de radio, ville / region, pays (aussi en francais) --- */
static void gb_fold(const char *s, char *o, int cap) {    /* minuscules sans accents */
    int n = 0;
    for (const u8 *p = (const u8 *)s; *p && n < cap - 1; p++) {
        u8 c = *p;
        if (c == 0xC3 && p[1]) {
            u8 d = p[1] | 0x20; p++;
            c = d >= 0xA0 && d <= 0xA5 ? 'a' : d == 0xA7 ? 'c' : d >= 0xA8 && d <= 0xAB ? 'e' : d >= 0xAC && d <= 0xAF ? 'i' :
                d == 0xB1 ? 'n' : (d >= 0xB2 && d <= 0xB6) || d == 0xB8 ? 'o' : d >= 0xB9 && d <= 0xBC ? 'u' : d == 0xBD || d == 0xBF ? 'y' : '?';
        } else if (c >= 0x80) { if ((c & 0xC0) == 0x80) continue; c = '?'; }
        else if (c >= 'A' && c <= 'Z') c += 32;
        o[n++] = (char)c;
    }
    o[n] = 0;
}
static const char *const gb_pays[][2] = {
    { "france", "FR" }, { "allemagne", "DE" }, { "espagne", "ES" }, { "italie", "IT" }, { "belgique", "BE" }, { "suisse", "CH" },
    { "royaume-uni", "GB" }, { "royaume uni", "GB" }, { "angleterre", "GB" }, { "ecosse", "GB" }, { "irlande", "IE" },
    { "portugal", "PT" }, { "pays-bas", "NL" }, { "pays bas", "NL" }, { "hollande", "NL" }, { "luxembourg", "LU" },
    { "autriche", "AT" }, { "pologne", "PL" }, { "grece", "GR" }, { "suede", "SE" }, { "norvege", "NO" }, { "danemark", "DK" },
    { "finlande", "FI" }, { "islande", "IS" }, { "russie", "RU" }, { "ukraine", "UA" }, { "roumanie", "RO" }, { "hongrie", "HU" },
    { "republique tcheque", "CZ" }, { "tchequie", "CZ" }, { "slovaquie", "SK" }, { "croatie", "HR" }, { "serbie", "RS" },
    { "bulgarie", "BG" }, { "turquie", "TR" }, { "etats-unis", "US" }, { "etats unis", "US" }, { "usa", "US" }, { "amerique", "US" },
    { "canada", "CA" }, { "quebec", "CA" }, { "mexique", "MX" }, { "bresil", "BR" }, { "argentine", "AR" }, { "chili", "CL" },
    { "colombie", "CO" }, { "perou", "PE" }, { "venezuela", "VE" }, { "cuba", "CU" }, { "japon", "JP" }, { "chine", "CN" },
    { "coree", "KR" }, { "coree du sud", "KR" }, { "inde", "IN" }, { "thailande", "TH" }, { "vietnam", "VN" }, { "indonesie", "ID" },
    { "philippines", "PH" }, { "australie", "AU" }, { "nouvelle-zelande", "NZ" }, { "nouvelle zelande", "NZ" }, { "maroc", "MA" },
    { "algerie", "DZ" }, { "tunisie", "TN" }, { "egypte", "EG" }, { "senegal", "SN" }, { "cote d'ivoire", "CI" }, { "cameroun", "CM" },
    { "afrique du sud", "ZA" }, { "nigeria", "NG" }, { "kenya", "KE" }, { "israel", "IL" }, { "liban", "LB" }, { "iran", "IR" },
    { "arabie saoudite", "SA" }, { "emirats", "AE" }, { "pakistan", "PK" }, { "taiwan", "TW" }, { "singapour", "SG" },
    { "malaisie", "MY" }, { "jamaique", "JM" }, { "haiti", "HT" }, { "reunion", "RE" }, { "martinique", "MQ" }, { "guadeloupe", "GP" },
    { "monaco", "MC" }, { "andorre", "AD" }, { "malte", "MT" }, { "chypre", "CY" }, { "estonie", "EE" }, { "lettonie", "LV" },
    { "lituanie", "LT" }, { "slovenie", "SI" }, { "bosnie", "BA" }, { "albanie", "AL" }, { "macedoine", "MK" }, { "moldavie", "MD" },
    { "bielorussie", "BY" }, { "georgie", "GE" }, { "armenie", "AM" }, { "kazakhstan", "KZ" }, { "mongolie", "MN" }, { "nepal", "NP" },
};
static void gb_goto_res(int k) {
    if (gb_sn <= 0) return;
    gb_sk = (k % gb_sn + gb_sn) % gb_sn;
    int i = gb_sres[gb_sk];
    gb_force = i;
    gb_fly(gb[i].lat, gb[i].lon, gb_r < 520 ? 520 : gb_r, -1);
}
static void gb_search(const char *q) {
    char f[64], t[220];
    gb_fold(q, f, sizeof(f));
    char *fq = trim(f);
    gb_sn = 0; gb_force = -1;
    if (!fq[0] || !gb_n) return;
    snprintf(gb_sq, sizeof(gb_sq), "%s", q);
    const char *cc = NULL;
    for (int i = 0; i < (int)(sizeof(gb_pays) / sizeof(gb_pays[0])); i++) if (!strcmp(fq, gb_pays[i][0])) { cc = gb_pays[i][1]; break; }
    if (!cc && strlen(fq) == 2) for (int i = 0; i < gb_n; i++) if (!strncasecmp(gb[i].cc, fq, 2)) { cc = gb[i].cc; break; }
    for (int pass = 0; pass < 3 && gb_sn < GB_SRES; pass++)      /* (pays, puis nom de la radio, puis lieu) */
        for (int i = 0; i < gb_n && gb_sn < GB_SRES; i++) {
            int m = 0;
            if (pass == 0) m = cc && !strncasecmp(gb[i].cc, cc, 2);
            else {
                gb_fold(gb_s(pass == 1 ? gb[i].name : gb[i].place), t, sizeof(t));
                m = strstr(t, fq) != NULL;
                if (m) for (int k = 0; k < gb_sn; k++) if (gb_sres[k] == i) { m = 0; break; }
            }
            if (m) gb_sres[gb_sn++] = i;
        }
    char m[160];
    if (!gb_sn) { snprintf(m, sizeof(m), "Rien trouv\xC3\xA9 pour \xC2\xAB %s \xC2\xBB", q); gb_note(m); return; }
    gb_goto_res(0);
}
static void gb_kb_done(const char *t) { gb_search(t); }

/* v208 : la carte (1 Mo a decompresser) et les stations de la carte SD sont lues par un fil a part :
   l'ecran s'ouvre tout de suite et reste a 50 images/s (avant : une image de 220 ms a l'ouverture). */
static int gb_map_load_raw(void);
static lwp_t gb_thr_h = LWP_THREAD_NULL;
static u8 gb_thr_stack[32 * 1024] __attribute__((aligned(32)));
static void *gb_thread(void *arg) {
    (void)arg;
    if (gb_thr_file) { int st = 0; gb_thr_file_ok = gb_load_file(&st); gb_thr_stale = st; gb_thr_file_done = 1; }   /* (les radios d'abord) */
    if (gb_thr_map) gb_map_load_raw();
    gb_thr_busy = 0;
    return NULL;
}
static void gb_thr_go(int map) {
    if (gb_thr_busy) return;
    if (gb_thr_h != LWP_THREAD_NULL) { LWP_JoinThread(gb_thr_h, NULL); gb_thr_h = LWP_THREAD_NULL; }
    int want_file = !gb_loaded && gb_state != GBL_LOAD && gb_state != GBL_FILE;
    if (want_file) gb_state = GBL_FILE;
    if (!want_file && !map) return;
    gb_thr_file = want_file; gb_thr_file_ok = 0; gb_thr_file_done = 0; gb_thr_map = map; gb_thr_busy = 1;
    if (LWP_CreateThread(&gb_thr_h, gb_thread, NULL, gb_thr_stack, sizeof(gb_thr_stack), 30) < 0) {   /* (pas de fil : tout de suite) */
        gb_thr_h = LWP_THREAD_NULL;
        gb_thread(NULL);
    }
}
/* v213 : onglet RADIO : les radios du globe sont preparees en fond (le globe s'ouvre deja rempli) */
static void gb_prefetch(void) {
    gb_want = 1;
    if (gb_loaded || gb_state == GBL_LOAD || gb_state == GBL_FILE) return;
    gb_thr_go(0);
}
static int gb_streaming(void) { return src == SRC_RADIO && state != ST_IDLE; }
static void gb_bg(void) {                        /* (chaque image, globe ferme : le chargement continue) */
    if (gb_open_now) return;
    if (gb_map_raw && !gb_thr_busy) gb_map_free();     /* (carte decompressee apres la fermeture : rendue) */
    if (!gb_want) return;
    if (gb_streaming()) {                            /* v213 : une radio joue : rien en fond (reseau et memoire pour elle) */
        if (gb_state == GBL_LOAD) {
            hf_abort(&gb_hf); hf_drop(&gb_hf); gb_hf.st = HF_IDLE;
            if (gb_js && gb_page > 0) gb_page--;          /* (page a moitie lue : redemandee ; les doublons sont ignores) */
            free(gb_js); gb_js = NULL; gb_jp = NULL;
            if (gb_refresh) { gb_refresh = 0; gb_stale_todo = 1; gb_state = gb_n ? GBL_DONE : GBL_IDLE; }
            else { gb_loaded = gb_n > 0; gb_state = GBL_IDLE; gb_resume = gb_n > 0; }
        }
        return;
    }
    if (gb_state == GBL_IDLE && !gb_thr_busy && net_phase == NET_DONE && net_ok) {
        if (gb_resume) { gb_resume = 0; gb_short = 0; gb_fetch(); }
        else if (!gb_loaded) gb_net_start();
    }
    if (gb_state == GBL_FILE || gb_state == GBL_LOAD || gb_stale_todo) gb_pump(2500);
}
static void gb_open(void) {
    tl_screen = TL_GLOBE;
    gb_open_now = 1; gb_want = 1;
    gb_pick = -1; gb_pick_k = 0; gb_travel = 0; gb_vx = gb_vy = gb_vz = 0;
    gb_idle_t = now_ms();
    if (gb_resume && gb_state == GBL_IDLE && net_phase == NET_DONE && net_ok) { gb_resume = 0; gb_short = 0; gb_fetch(); }   /* (suite du chargement) */
    else if (!gb_loaded && gb_state == GBL_IDLE && !gb_thr_busy && net_phase == NET_DONE && net_ok && gb_thr_file_done && !gb_thr_file_ok) gb_net_start();
    gb_thr_go(1);
}
static void gb_net_start(void) {
    if (net_phase != NET_DONE || !net_ok) {
        net_start_async();
        if (gb_refresh) { gb_refresh = 0; gb_state = GBL_DONE; return; }
        gb_state = GBL_ERR; gb_note("R\xC3\xA9seau pas encore pr\xC3\xAAt : r\xC3\xA9" "essaie (A)"); return;
    }
    if (gb_refresh) { for (int i = 0; i < gb_n; i++) gb[i].seen = 0; }
    else gb_clear();
    gb_page = 0; gb_srv = 0; gb_short = 0;
    gb_fetch();
}
static void gb_close(void) {
    tl_screen = TL_NONE;
    gb_open_now = 0;
    if (!gb_thr_busy) gb_map_free();             /* (1 Mo rendu ; la carte est redecompressee a la prochaine ouverture) */
    free(gb_dx); free(gb_dy); free(gb_da); gb_dx = gb_dy = NULL; gb_da = NULL; gb_dcap = 0;   /* v213 : tout est rendu */
    free(gb_hu); free(gb_hn); gb_hu = gb_hn = NULL;   /* (refaites a la demande) */
    gx_mode = -1;
}

static void gb_input(u32 down, u32 nav, int fire) {
    (void)nav; (void)fire;
    if (down & PAD_BUTTON_B) { gb_close(); return; }
    if (!gb_loaded) {
        if ((down & PAD_BUTTON_A) && gb_state != GBL_LOAD && gb_state != GBL_FILE) { gb_state = GBL_IDLE; gb_net_start(); }
        return;
    }
    if (down) gb_idle_t = now_ms();
    if (down & PAD_TRIGGER_Z) { kb_start(KB_GLOBE, "", -1, -1); return; }
    int *near = gb_nearv, nn = gb_nn;
    if (gb_sn > 0 && gb_force >= 0) {                     /* (apres une recherche : L / R = resultat precedent / suivant) */
        if (down & PAD_TRIGGER_R) gb_goto_res(gb_sk + 1);
        if (down & PAD_TRIGGER_L) gb_goto_res(gb_sk - 1);
    } else {
        if (down & PAD_TRIGGER_R) { if (nn) gb_pick_k = (gb_pick_k + 1) % nn; }
        if (down & PAD_TRIGGER_L) { if (nn) gb_pick_k = (gb_pick_k + nn - 1) % nn; }
        if (gb_pick_k >= nn) gb_pick_k = 0;
        gb_pick = nn ? near[gb_pick_k] : -1;
    }
    if ((down & PAD_BUTTON_A) && gb_pick >= 0) gb_play(gb_pick);
    if ((down & PAD_BUTTON_Y) && gb_pick >= 0) gb_fav(gb_pick);
    if ((down & PAD_BUTTON_X) && gb_n) {                 /* au hasard : le globe y va, puis la radio joue */
        int i = (int)(lib_rand() % (u32)gb_n);
        gb_sn = 0; gb_force = -1;
        gb_fly(gb[i].lat, gb[i].lon, gb_r > 400 ? 400 : gb_r < 240 ? 240 : gb_r, i);
    }
}
/* (chaque image, ecran ouvert) */
static void gb_step(void) {
    f32 tx = 0, ty = 0, tz = 0;                         /* vitesses voulues (-1..1) */
    int touched = 0;
    if (!kb_is_open()) {                                /* stick : tourner ; stick C : zoom (meme pendant le chargement) */
        int mx = PAD_StickX(0), my = PAD_StickY(0), cy = PAD_SubStickY(0);
        u32 held = PAD_ButtonsHeld(0);
        #define GBC(v) (abs(v) <= 14 ? 0.0f : ((v) > 0 ? 1.0f : -1.0f) * powf(fminf(1.0f, (abs(v) - 14) / 58.0f), 1.5f))
        tx = GBC(mx); ty = GBC(my); tz = GBC(cy);
        #undef GBC
        if (held & PAD_BUTTON_LEFT) tx = -1;
        if (held & PAD_BUTTON_RIGHT) tx = 1;
        if (held & PAD_BUTTON_UP) ty = 1;
        if (held & PAD_BUTTON_DOWN) ty = -1;
        touched = tx != 0 || ty != 0 || tz != 0;
        if (touched) { gb_idle_t = gb_stick_t = now_ms(); gb_travel = 0; gb_pick_k = 0; gb_force = -1; }
    }
    /* v213 : inertie : la vitesse rejoint le stick en ~0,1 s (demarrage et arret en douceur, pas de saccade) */
    f32 k = 1.0f - expf(-ui_dt * 11.0f);
    gb_vx += (tx - gb_vx) * k; gb_vy += (ty - gb_vy) * k; gb_vz += (tz - gb_vz) * k;
    if (fabsf(gb_vx) < 0.002f && !tx) gb_vx = 0;
    if (fabsf(gb_vy) < 0.002f && !ty) gb_vy = 0;
    if (fabsf(gb_vz) < 0.002f && !tz) gb_vz = 0;
    f32 sp = 80.0f * 168.0f / gb_r;                      /* (degres / s ; plus lent quand on zoome) */
    if (!gb_travel) {
        f32 cl = cosf(gb_lat * (f32)M_PI / 180.0f);
        gb_lon += gb_vx * sp * ui_dt / (cl > 0.25f ? cl : 0.25f) * 0.85f;   /* (meme vitesse a l'ecran, meme pres des poles) */
        gb_lat += gb_vy * sp * ui_dt;
        gb_r *= expf(gb_vz * 1.7f * ui_dt);
    }
    if (gb_r < 120) gb_r = 120;
    if (gb_r > 2400) gb_r = 2400;
    while (gb_lon > 180) gb_lon -= 360;
    while (gb_lon < -180) gb_lon += 360;
    if (gb_travel) gb_fly_step();
    else if (gb_loaded && now_ms() - gb_idle_t > 8000) gb_lon += 4.0f * ui_dt;   /* (rien touche : la Terre tourne doucement) */
    else if (!touched && gb_mag >= 0 && gb_mag < gb_n && now_ms() - gb_stick_t > 150 && fabsf(gb_vx) + fabsf(gb_vy) < 0.05f) {
        /* v213 : viseur aimante : une radio tout pres se place doucement au centre */
        f32 dl = gb[gb_mag].lon - gb_lon;
        while (dl > 180) dl -= 360;
        while (dl < -180) dl += 360;
        f32 km = 1.0f - expf(-ui_dt * 7.0f);
        gb_lon += dl * km; gb_lat += (gb[gb_mag].lat - gb_lat) * km;
    }
    if (gb_lat > 85) gb_lat = 85;
    if (gb_lat < -85) gb_lat = -85;
    gb_view();
    if (!gb_loaded) { gb_nn = 0; gb_pick = -1; }        /* (radio visee : calculee avec les points, dans draw_globe) */
}

/* --- v208 : la carte du monde (Natural Earth, domaine public : globe_map.h) --- */
#include "globe_map.h"
static u8 *gb_map = NULL;                       /* deux textures I4 1024 x 1024 (ouest, est) */
static GXTexObj gb_mt[2];
static int gb_map_fail = 0;
static int gb_map_load_raw(void) {
    if (gb_map || gb_map_raw || gb_map_fail) return gb_map || gb_map_raw;
    u8 *m = (u8 *)memalign(32, GLOBE_MAP_RAW);
    if (!m || stbi_zlib_decode_buffer((char *)m, GLOBE_MAP_RAW, (const char *)globe_map_z, GLOBE_MAP_Z) != GLOBE_MAP_RAW) { free(m); gb_map_fail = 1; return 0; }
    DCFlushRange(m, GLOBE_MAP_RAW);
    gb_map_raw = m;
    return 1;
}
static int gb_map_load(void) {
    if (gb_map) return 1;
    if (!gb_map_raw) { if (!gb_thr_busy && !gb_map_fail) gb_thr_go(1); return 0; }   /* (pas encore prete : le globe sans les terres) */
    gb_map = gb_map_raw; gb_map_raw = NULL;
    GX_InvalidateTexAll();
    for (int i = 0; i < 2; i++) {
        GX_InitTexObj(&gb_mt[i], gb_map + i * (GLOBE_MAP_RAW / 2), 1024, 1024, GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&gb_mt[i], GX_LINEAR, GX_LINEAR);
    }
    return 1;
}
static void gb_map_free(void) { free(gb_map); gb_map = NULL; free(gb_map_raw); gb_map_raw = NULL; }
static void gb_set_map(int half) {              /* couleur = sommet ; opacite = carte x sommet (carte 3) */
    if (gx_mode != 7) {
        gx_mode = 7;
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP3, GX_COLOR0A0);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    }
    GX_LoadTexObj(&gb_mt[half], GX_TEXMAP3);
}
#define GB_ML 37                                /* grille : 5 degres */
#define GB_MO 73
static f32 gb_gv[GB_ML][GB_MO][3];               /* (vecteurs de la grille, calcules une fois : pas de sinus a chaque image) */
static int gb_gv_ok = 0;
static void gb_grid_init(void) {
    if (gb_gv_ok) return;
    for (int i = 0; i < GB_ML; i++) {
        f32 la = (90.0f - i * 5.0f) * (f32)M_PI / 180.0f, cl = cosf(la), sl = sinf(la);
        for (int j = 0; j < GB_MO; j++) {
            f32 lo = (-180.0f + j * 5.0f) * (f32)M_PI / 180.0f;
            gb_gv[i][j][0] = cl * cosf(lo); gb_gv[i][j][1] = cl * sinf(lo); gb_gv[i][j][2] = sl;
        }
    }
    gb_gv_ok = 1;
}
static inline f32 gb_day(f32 x, f32 y, f32 z);   /* (plus bas : le soleil) */
static void gb_draw_land(void) {
    if (!gb_map_load()) return;
    static f32 px[GB_ML][GB_MO], py[GB_ML][GB_MO], pd[GB_ML][GB_MO];
    gb_grid_init();
    for (int i = 0; i < GB_ML; i++)
        for (int j = 0; j < GB_MO; j++) pd[i][j] = gb_proj(gb_gv[i][j][0], gb_gv[i][j][1], gb_gv[i][j][2], &px[i][j], &py[i][j]);
    for (int half = 0; half < 2; half++) {
        gb_set_map(half);
        int j0 = half ? 36 : 0, n = 0;
        static u8 qi[GB_ML * 36], qj[GB_ML * 36];
        for (int i = 0; i < GB_ML - 1; i++)
            for (int j = j0; j < j0 + 36; j++) {
                if (pd[i][j] < -0.06f || pd[i + 1][j] < -0.06f || pd[i][j + 1] < -0.06f || pd[i + 1][j + 1] < -0.06f) continue;
                if (pd[i][j] <= 0 && pd[i + 1][j] <= 0 && pd[i][j + 1] <= 0 && pd[i + 1][j + 1] <= 0) continue;
                f32 mnx = px[i][j], mxx = mnx, mny = py[i][j], mxy = mny;
                f32 xs[3] = { px[i + 1][j], px[i][j + 1], px[i + 1][j + 1] }, ys[3] = { py[i + 1][j], py[i][j + 1], py[i + 1][j + 1] };
                for (int k = 0; k < 3; k++) { if (xs[k] < mnx) mnx = xs[k]; if (xs[k] > mxx) mxx = xs[k]; if (ys[k] < mny) mny = ys[k]; if (ys[k] > mxy) mxy = ys[k]; }
                if (mxx < 0 || mnx > 640 || mxy < 0 || mny > 480) continue;   /* (hors de l'ecran) */
                qi[n] = (u8)i; qj[n] = (u8)j; n++;
            }
        for (int b = 0; b < n; b += 1000) {
            int m = n - b < 1000 ? n - b : 1000;
            GX_Begin(GX_QUADS, GX_VTXFMT0, m * 4);
            for (int q = b; q < b + m; q++) {
                int i = qi[q], j = qj[q];
                int ii[4] = { i, i, i + 1, i + 1 }, jj[4] = { j, j + 1, j + 1, j };
                for (int k = 0; k < 4; k++) {
                    f32 d = pd[ii[k]][jj[k]];
                    if (d < 0) d = 0;
                    f32 l = (0.45f + 0.55f * d) * (0.38f + 0.62f * gb_day(gb_gv[ii[k]][jj[k]][0], gb_gv[ii[k]][jj[k]][1], gb_gv[ii[k]][jj[k]][2]));   /* (clair au centre ; la nuit plus sombre) */
                    GX_Position2f32(UX(px[ii[k]][jj[k]]), py[ii[k]][jj[k]]);
                    col_t c = th_col(RGBA(52 * l + 14, 104 * l + 20, 70 * l + 18, 255));
                    GX_Color4u8(c.r, c.g, c.b, c.a);
                    GX_TexCoord2f32((f32)(jj[k] - j0) / 36.0f, (f32)ii[k] / 36.0f);
                }
            }
            GX_End();
        }
    }
}

/* trait epais (a mettre dans un GX_Begin(GX_QUADS)) */
static inline void gb_seg(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, col_t c0, col_t c1) {
    f32 dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.01f) l = 0.01f;
    f32 nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    vtx(x0 + nx, y0 + ny, c0); vtx(x1 + nx, y1 + ny, c1); vtx(x1 - nx, y1 - ny, c1); vtx(x0 - nx, y0 - ny, c0);
}
/* v213 : le soleil (heure UTC de la console) : la nuit assombrit les terres, les radios y brillent comme des villes */
static f32 gb_sx_ = 0, gb_sy_ = 0, gb_sz_ = 0;
static int gb_sun = 0;
static void gb_sun_calc(void) {
    time_t t = time(NULL);
    gb_sun = t > 1600000000;
    if (!gb_sun) return;
    f32 days = (f32)((t / 86400) % 365), hrs = (f32)(t % 86400) / 3600.0f;
    f32 dec = -23.44f * cosf(2.0f * (f32)M_PI / 365.0f * (days + 10.0f));
    f32 lon = -15.0f * (hrs - 12.0f);
    gb_latlon_v(dec, lon, &gb_sx_, &gb_sy_, &gb_sz_);
}
static inline f32 gb_day(f32 x, f32 y, f32 z) {          /* 1 : jour ; 0 : nuit (crepuscule doux) */
    if (!gb_sun) return 1;
    f32 d = (x * gb_sx_ + y * gb_sy_ + z * gb_sz_) * 6.0f + 0.5f;
    return d < 0 ? 0 : d > 1 ? 1 : d;
}
static void draw_globe(void) {
    gb_pump(5000);
    gb_step();
    static u64 sun_t = 0;
    if (now_ms() - sun_t > 20000 || !sun_t) { sun_t = now_ms(); gb_sun_calc(); }
    f32 R = gb_r;
    const int cover = R > 410.0f;                         /* (la Terre remplit tout l'ecran : pas d'espace a dessiner) */
    if (!cover) {
        /* l'espace */
        gx_quad4(0, 0, 640, 480, RGBA(2, 3, 14, 255), RGBA(4, 4, 22, 255), RGBA(8, 6, 30, 255), RGBA(3, 4, 18, 255));
        static f32 stars[90][3];
        static int stars_ok = 0;
        if (!stars_ok) { for (int i = 0; i < 90; i++) { stars[i][0] = (f32)(lib_rand() % 640); stars[i][1] = (f32)(lib_rand() % 480); stars[i][2] = (f32)(lib_rand() % 100) / 100.0f; } stars_ok = 1; }
        f32 tnow = now_ms() / 700.0f;
        gx_set_flat();
        GX_Begin(GX_QUADS, GX_VTXFMT0, 90 * 4);                /* (toutes les etoiles d'un coup) */
        for (int i = 0; i < 90; i++) {
            f32 tw = 0.55f + 0.45f * sinf(tnow + stars[i][2] * 20.0f);
            col_t c = RGBA(220, 225, 255, (u8)(60 + 150 * stars[i][2] * tw));
            f32 sz = stars[i][2] > 0.85f ? 2.0f : 1.2f, x = stars[i][0], y = stars[i][1];
            vtx(x, y, c); vtx(x + sz, y, c); vtx(x + sz, y + sz, c); vtx(x, y + sz, c);
        }
        GX_End();
        /* la Terre */
        gx_circle_glow(GB_CX, GB_CY, R + 1, RGBA(90, 150, 255, 120), 10);
        gx_circle(GB_CX, GB_CY, R + 3, RGBA(60, 110, 230, 60), RGBA(60, 110, 230, 0));
        gx_circle(GB_CX, GB_CY, R, RGBA(18, 34, 82, 255), RGBA(6, 12, 34, 255));
    } else gx_rect(0, 0, 640, 480, RGBA(12, 22, 58, 255));
    gb_draw_land();
    /* meridiens et paralleles */
    gx_set_flat();
    static f32 gx_[2600], gy_[2600]; static u8 ga_[2600];
    int nseg = 0;
    gb_grid_init();
    {
        static f32 qx[GB_ML][GB_MO], qy[GB_ML][GB_MO], qd[GB_ML][GB_MO];
        for (int i = 0; i < GB_ML; i++)
            for (int j = 0; j < GB_MO; j++)
                if (j % 6 == 0 || (i % 6 == 0 && i > 0 && i < GB_ML - 1)) qd[i][j] = gb_proj(gb_gv[i][j][0], gb_gv[i][j][1], gb_gv[i][j][2], &qx[i][j], &qy[i][j]);
        for (int j = 0; j < GB_MO - 1; j += 6)                     /* meridiens */
            for (int i = 0; i < GB_ML - 1 && nseg < 1300; i++) {
                f32 d0 = qd[i][j], d1 = qd[i + 1][j];
                if (d0 <= 0 || d1 <= 0) continue;
                gx_[nseg * 2] = qx[i][j]; gy_[nseg * 2] = qy[i][j]; gx_[nseg * 2 + 1] = qx[i + 1][j]; gy_[nseg * 2 + 1] = qy[i + 1][j];
                ga_[nseg++] = (u8)(18 + 40 * (d0 < d1 ? d0 : d1));
            }
        for (int i = 6; i < GB_ML - 1; i += 6)                     /* paralleles */
            for (int j = 0; j < GB_MO - 1 && nseg < 1300; j++) {
                f32 d0 = qd[i][j], d1 = qd[i][j + 1];
                if (d0 <= 0 || d1 <= 0) continue;
                gx_[nseg * 2] = qx[i][j]; gy_[nseg * 2] = qy[i][j]; gx_[nseg * 2 + 1] = qx[i][j + 1]; gy_[nseg * 2 + 1] = qy[i][j + 1];
                ga_[nseg++] = (u8)(18 + 40 * (d0 < d1 ? d0 : d1));
            }
    }
    for (int b = 0; b < nseg; b += 1000) {              /* (par paquets de 1000 : 4000 sommets) */
        int m = nseg - b < 1000 ? nseg - b : 1000;
        GX_Begin(GX_QUADS, GX_VTXFMT0, m * 4);
        for (int i = b; i < b + m; i++) gb_seg(gx_[i * 2], gy_[i * 2], gx_[i * 2 + 1], gy_[i * 2 + 1], 1.0f, RGBA(120, 170, 255, ga_[i]), RGBA(120, 170, 255, ga_[i]));
        GX_End();
    }
    /* les radios */
    if (gb_loaded && gb_n) {
        f32 *dx = gb_dx, *dy = gb_dy; u8 *da = gb_da;
        int dcap = gb_dcap;
        if (dcap < gb_n) {
            free(dx); free(dy); free(da);
            dx = (f32 *)malloc(sizeof(f32) * gb_n); dy = (f32 *)malloc(sizeof(f32) * gb_n); da = (u8 *)malloc(gb_n);
            dcap = dx && dy && da ? gb_n : 0;
            gb_dx = dx; gb_dy = dy; gb_da = da; gb_dcap = dcap;
        }
        int nv = 0;
        int nn = 0; f32 nd[16];                          /* (en meme temps : les radios pres du viseur, une seule passe) */
        int mag = -1; f32 magd = 30.0f * 30.0f;          /* (aimant : la plus proche a moins de 30 points) */
        int nl = 0; int li[6]; f32 ld[6], lx_[6], ly_[6];   /* (noms affiches : les 6 plus proches, zoome) */
        const int labels = R >= 420;
        for (int i = 0; i < gb_n && i < dcap; i++) {
            f32 sx, sy, d = gb_proj(gb[i].x, gb[i].y, gb[i].z, &sx, &sy);
            if (d <= 0.02f || sx < -4 || sx > 644 || sy < -4 || sy > 484) continue;
            f32 dn = gb_day(gb[i].x, gb[i].y, gb[i].z);
            dx[nv] = sx; dy[nv] = sy; da[nv] = (u8)((70 + 185 * d) * (1.0f - 0.25f * dn) + 60 * (1.0f - dn) * d);
            if (da[nv] < 60) da[nv] = 60;
            nv++;
            f32 ex = sx - GB_CX, ey = sy - GB_CY, e = ex * ex + ey * ey;
            if (d > 0.05f && e < magd) { magd = e; mag = i; }
            if (labels && d > 0.2f && e < 130.0f * 130.0f) {
                int k = nl < 6 ? nl++ : 6;
                if (k < 6 || e < ld[5]) {
                    if (k == 6) k = 5;
                    while (k > 0 && ld[k - 1] > e) { ld[k] = ld[k - 1]; li[k] = li[k - 1]; lx_[k] = lx_[k - 1]; ly_[k] = ly_[k - 1]; k--; }
                    ld[k] = e; li[k] = i; lx_[k] = sx; ly_[k] = sy;
                }
            }
            if (d > 0.05f && e <= 16.0f * 16.0f) {
                int k = nn < 16 ? nn++ : 16;
                if (k == 16) { if (e >= nd[15]) continue; k = 15; }
                while (k > 0 && nd[k - 1] > e) { nd[k] = nd[k - 1]; gb_nearv[k] = gb_nearv[k - 1]; k--; }
                nd[k] = e; gb_nearv[k] = i;
            }
        }
        gb_nn = nn;
        gb_mag = mag;
        if (gb_pick_k >= gb_nn) gb_pick_k = 0;
        gb_pick = gb_nn ? gb_nearv[gb_pick_k] : -1;
        if (gb_force >= 0 && gb_force < gb_n) gb_pick = gb_force;   /* (resultat de recherche) */
        f32 ds = R > 600 ? 3.0f : R > 300 ? 2.4f : 2.0f;
        for (int b = 0; b < nv; b += 1000) {
            int m = nv - b < 1000 ? nv - b : 1000;
            GX_Begin(GX_QUADS, GX_VTXFMT0, m * 4);
            for (int i = b; i < b + m; i++) {
                col_t c = RGBA(255, 200 + da[i] / 5, 90 + da[i] / 3, da[i]);
                vtx(dx[i] - ds / 2, dy[i] - ds / 2, c); vtx(dx[i] + ds / 2, dy[i] - ds / 2, c);
                vtx(dx[i] + ds / 2, dy[i] + ds / 2, c); vtx(dx[i] - ds / 2, dy[i] + ds / 2, c);
            }
            GX_End();
        }
        /* noms des radios proches (zoome) */
        for (int q = 0; q < nl; q++) {
            if (li[q] == gb_pick) continue;
            int clash = 0;
            for (int r2 = 0; r2 < q; r2++) if (fabsf(ly_[r2] - ly_[q]) < 13 && fabsf(lx_[r2] - lx_[q]) < 120) { clash = 1; break; }
            if (clash) continue;
            char nb[64], nf[64];
            text_display(gb_s(gb[li[q]].name), nb, sizeof(nb));
            fit_text(F_SMALL, nb, 130, nf, sizeof(nf));
            f32 a = 1.0f - sqrtf(ld[q]) / 130.0f;
            gx_text1(F_SMALL, lx_[q] + 6, ly_[q] + 4, nf, RGBA(225, 232, 255, (u8)(80 + 150 * a)));
        }
        /* radio en cours */
        if (gb_playing >= 0 && gb_playing < gb_n && src == SRC_RADIO && state != ST_IDLE) {
            f32 sx, sy;
            if (gb_proj(gb[gb_playing].x, gb[gb_playing].y, gb[gb_playing].z, &sx, &sy) > 0.02f) {
                f32 ph = (now_ms() % 1400) / 1400.0f;
                gx_ring(sx, sy, 3 + ph * 14, 4.5f + ph * 14, RGBA(255, 210, 110, (u8)(220 * (1 - ph))));
                gx_circle(sx, sy, 3.5f, RGBA(255, 230, 150, 255), RGBA(255, 190, 80, 255));
            }
        }
        /* radio visee */
        if (gb_pick >= 0 && gb_pick < gb_n) {
            f32 sx, sy;
            if (gb_proj(gb[gb_pick].x, gb[gb_pick].y, gb[gb_pick].z, &sx, &sy) > 0.02f) {
                gx_ring(sx, sy, 6, 7.5f, RGBA(255, 255, 255, 230));
                gx_circle(sx, sy, 2.6f, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
            }
        }
    }
    /* viseur */
    gx_ring(GB_CX, GB_CY, 15, 16, RGBA(255, 255, 255, 70));
    gx_rect(GB_CX - 22, GB_CY - 0.5f, 6, 1, RGBA(255, 255, 255, 120)); gx_rect(GB_CX + 16, GB_CY - 0.5f, 6, 1, RGBA(255, 255, 255, 120));
    gx_rect(GB_CX - 0.5f, GB_CY - 22, 1, 6, RGBA(255, 255, 255, 120)); gx_rect(GB_CX - 0.5f, GB_CY + 16, 1, 6, RGBA(255, 255, 255, 120));
    /* titre (sur un voile : lisible meme zoome) */
    gx_vgrad(0, 0, 640, 76, RGBA(2, 3, 14, 230), RGBA(2, 3, 14, 0));
    gx_text1(F_LARGE, 24, 40, "Globe des radios", RGBA(255, 255, 255, 255));
    char buf[220], fv[220];
    int pct = gb_page * 100 / GB_PAGES;
    if (gb_js && gb_jgot && gb_page > 0) pct = ((gb_page - 1) * GB_PAGE + gb_jgot) * 100 / (GB_PAGES * GB_PAGE);
    if (gb_loaded && gb_state == GBL_LOAD) snprintf(buf, sizeof(buf), "%d radios (chargement : %d %%)", gb_n, pct);
    else if (gb_loaded) snprintf(buf, sizeof(buf), "%d radios", gb_n);
    else if (gb_state == GBL_LOAD) snprintf(buf, sizeof(buf), "Chargement des radios du monde : %d %%", pct);
    else if (gb_state == GBL_FILE) snprintf(buf, sizeof(buf), "Lecture des radios sur la carte SD...");
    else snprintf(buf, sizeof(buf), "%s", "Annuaire injoignable (A : r\xC3\xA9" "essayer)");
    if (gb_state == GBL_LOAD && gb_refresh) snprintf(buf, sizeof(buf), "%d radios (mise \xC3\xA0 jour de la liste...)", gb_n);
    gx_text1(F_SMALL, 24, 60, buf, gb_state == GBL_ERR && !gb_loaded ? RGBA(255, 160, 110, 255) : RGBA(170, 190, 240, 255));
    if (gb_state == GBL_LOAD) {                           /* v213 : barre de chargement */
        f32 fr = (f32)gb_page / GB_PAGES;
        if (gb_js && gb_jgot) fr = ((f32)(gb_page - 1) + (f32)gb_jgot / GB_PAGE) / GB_PAGES;
        if (fr < 0.02f) fr = 0.02f;
        if (fr > 1) fr = 1;
        gx_rrect_fill(24, 68, 200, 4, 2, RGBA(40, 50, 100, 200), RGBA(40, 50, 100, 200));
        gx_rrect_fill(24, 68, 200 * fr, 4, 2, RGBA(120, 190, 255, 255), RGBA(90, 150, 255, 255));
    }
    {                                                     /* coordonnees du centre */
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0%c  %.0f\xC2\xB0%c", fabsf(gb_lat), gb_lat >= 0 ? 'N' : 'S', fabsf(gb_lon), gb_lon >= 0 ? 'E' : 'O');
        gx_text_r(F_SMALL, 616, 40, buf, RGBA(150, 170, 220, 255));
    }
    /* fiche de la radio visee */
    if (gb_pick >= 0 && gb_pick < gb_n) {
        const gbst_t *e = &gb[gb_pick];
        gx_rrect_fill(120, 352, 400, 58, 12, RGBA(16, 22, 56, 235), RGBA(10, 14, 40, 235));
        gx_rrect_stroke(120, 352, 400, 58, 12, 1.0f, RGBA(120, 170, 255, 120));
        text_display(gb_s(e->name), buf, sizeof(buf));
        fit_text(F_MED, buf, 290, fv, sizeof(fv));
        gx_text1(F_MED, 136, 376, fv, RGBA(255, 255, 255, 255));
        text_display(gb_s(e->place), buf, sizeof(buf));
        fit_text(F_SMALL, buf, 290, fv, sizeof(fv));
        gx_text1(F_SMALL, 136, 398, fv, RGBA(170, 190, 240, 255));
        if (e->kbps) snprintf(buf, sizeof(buf), "%s%d kbit/s", e->hls ? "HLS " : "", e->kbps); else snprintf(buf, sizeof(buf), "%s", e->hls ? "HLS" : "");
        gx_text_r(F_SMALL, 504, 376, buf, RGBA(150, 170, 220, 255));
        int nn = gb_nn;
        if (gb_force >= 0 && gb_sn > 0) { snprintf(buf, sizeof(buf), "%d / %d trouv\xC3\xA9" "es (L / R)", gb_sk + 1, gb_sn); gx_text_r(F_SMALL, 504, 398, buf, RGBA(255, 210, 120, 255)); }
        else if (nn > 1) { snprintf(buf, sizeof(buf), "%d / %d ici (L / R)", gb_pick_k + 1, nn); gx_text_r(F_SMALL, 504, 398, buf, RGBA(255, 210, 120, 255)); }
    } else if (gb_loaded) gx_text_c(F_SMALL, 320, 392, "Vise un point lumineux : chaque point est une radio", RGBA(150, 170, 220, 200));
    /* v213 : la radio du globe en cours : connexion, ecoute, ou panne */
    if (gb_playing >= 0 && gb_playing < gb_n && src == SRC_RADIO && state != ST_IDLE && cur_station == nstations) {
        const char *what = state == ST_CONNECT ? "Connexion..." : state == ST_BUFFER ? "Mise en m\xC3\xA9moire..." :
                           state == ST_PLAY ? "En \xC3\xA9" "coute" : "Ne r\xC3\xA9pond pas (R : une autre radio ici)";
        char nb[120];
        text_display(gb_s(gb[gb_playing].name), nb, sizeof(nb));
        snprintf(buf, sizeof(buf), "%s : %s", nb, what);
        fit_text(F_SMALL, buf, 560, fv, sizeof(fv));
        gx_text_c(F_SMALL, 320, gb_pick >= 0 ? 342 : 372, fv, state == ST_ERROR ? RGBA(255, 160, 110, 255) : RGBA(150, 230, 170, 255));
    }
    if (now_ms() - gb_msg_t < 4000) gx_text_c(F_SMALL, 320, 322, gb_msg, RGBA(255, 210, 120, 255));
    static const char *const k[] = { "A", "X", "Y", "Z", "C", "B" };
    static const char *const l[] = { "\xC3\x89" "COUTER", "HASARD", "GARDER", "CHERCHER", "ZOOM", "FERMER" };
    tl_legend(k, l, 6);
}

static int gb_screen_on(void) { return tl_screen == TL_GLOBE; }   /* (le curseur au stick de GC Station se tait) */

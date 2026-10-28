// GC Station v207 : le globe des radios (inclus dans main.c, avant tools.c).
// Onglet RADIO > "Globe des radios" : la Terre en 3D ; chaque point lumineux est une radio (annuaire
// radio-browser.info, ~4000 stations placees sur la carte). Les continents se dessinent tout seuls avec les
// radios. Stick : tourner ; stick C : zoom ; A : ecouter la radio visee ; L / R : les autres radios du meme
// endroit ; X : une radio au hasard (le globe y va) ; Y : garder dans mes radios ; B : fermer.
// Les stations sont gardees sur la carte SD (SD:/gc-mp3/reglages/globe.bin), relues tous les 30 jours.

#define GB_FILE  GCD_REG "/globe.bin"
#define GB_PAGES 4
#define GB_PAGE  1000
typedef struct { f32 x, y, z; f32 lat, lon; u32 name, url, place; u16 kbps; char cc[3]; u8 hls; } gbst_t;
static gbst_t *gb = NULL;
static int gb_n = 0, gb_cap = 0;
static char *gb_pool = NULL;
static int gb_pool_n = 0, gb_pool_cap = 0;
static int gb_loaded = 0;                       /* 1 = stations en memoire */
enum { GBL_IDLE, GBL_LOAD, GBL_DONE, GBL_ERR, GBL_FILE };
static int gb_state = GBL_IDLE, gb_page = 0, gb_srv = 0;
static hfetch_t gb_hf = { .c = { -1, 0, 0 } };
static f32 gb_lon = 2.35f, gb_lat = 30.0f, gb_r = 168.0f;   /* vue : centre (degres), rayon du globe (points) */
static f32 gb_tlon = 0, gb_tlat = 0;            /* voyage vers (X) */
static int gb_travel = 0, gb_travel_play = -1;
static u64 gb_idle_t = 0;
static int gb_pick = -1, gb_pick_k = 0;         /* radio visee ; L / R : la k-ieme du meme endroit */
static int gb_playing = -1;                     /* radio du globe en cours d'ecoute */
static char gb_msg[160]; static u64 gb_msg_t = 0;

static void gb_map_free(void);
static void gb_net_start(void);
static volatile int gb_thr_busy = 0, gb_thr_file = 0, gb_thr_file_ok = 0;
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
static void gb_clear(void) {
    free(gb); gb = NULL; gb_n = gb_cap = 0;
    free(gb_pool); gb_pool = NULL; gb_pool_n = gb_pool_cap = 0;
    gb_str("");                                  /* (position 0 : texte vide) */
}
static int gb_add(const gbst_t *e) {
    if (gb_n == gb_cap) {
        int nc = gb_cap ? gb_cap * 2 : 1024;
        gbst_t *nb = (gbst_t *)realloc(gb, sizeof(gbst_t) * nc);
        if (!nb) return 0;
        gb = nb; gb_cap = nc;
    }
    gb[gb_n++] = *e;
    return 1;
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
static int gb_load_file(void) {
    if (!sd_ok) return 0;
    struct stat st;
    if (stat(GB_FILE, &st) != 0) return 0;
    time_t now = time(NULL);
    if (now > 1600000000 && st.st_mtime > 0 && now - st.st_mtime > 30L * 24 * 3600) return 0;   /* (plus d'un mois : on relit l'annuaire) */
    FILE *f = fopen(GB_FILE, "rb");
    if (!f) return 0;
    u32 h[4];
    int ok = fread(h, sizeof(h), 1, f) == 1 && h[0] == 0x47434742u && h[1] == 1 && h[2] > 0 && h[2] < 20000 && h[3] < (8u << 20);
    if (ok) {
        gb_clear();
        gb_pool_n = 0;
        gb = (gbst_t *)malloc(sizeof(gbst_t) * h[2]);
        gb_pool = (char *)malloc(h[3] + 1);
        ok = gb && gb_pool;
        if (ok) { gb_cap = (int)h[2]; gb_pool_cap = (int)h[3] + 1; }
        for (u32 i = 0; i < h[2] && ok; i++) {
            gbrec_t r;
            if (fread(&r, sizeof(r), 1, f) != 1) { ok = 0; break; }
            gbst_t e; memset(&e, 0, sizeof(e));
            e.lat = r.lat; e.lon = r.lon; e.name = r.name; e.url = r.url; e.place = r.place; e.kbps = r.kbps;
            memcpy(e.cc, r.cc, 3); e.hls = r.hls;
            if (e.name >= h[3] || e.url >= h[3] || e.place >= h[3]) { ok = 0; break; }
            gb_xyz(&e);
            gb[gb_n++] = e;
        }
        if (ok) ok = fread(gb_pool, 1, h[3], f) == h[3];
        if (ok) { gb_pool_n = (int)h[3]; gb_pool[gb_pool_n] = 0; if (gb_pool_n) gb_pool[gb_pool_n - 1] = 0; }
    }
    fclose(f);
    if (!ok) { gb_clear(); return 0; }
    return 1;
}

/* --- annuaire (pages de 1000 stations, les plus ecoutees d'abord) --- */
static void gb_fetch(void) {
    char url[URL_MAX];
    snprintf(url, sizeof(url), RB_SCHEME "://%s/json/stations/search?has_geo_info=true&hidebroken=true&order=clickcount&reverse=true&limit=%d&offset=%d",
             rb_servers[gb_srv], GB_PAGE, gb_page * GB_PAGE);
    gb_hf.image = 0;
    gb_hf.maxbytes = 3 * 1024 * 1024;
    gb_hf.accept = "application/json";
    gb_hf.tmo_ms = 30000;
    hf_start(&gb_hf, url, NULL);
    gb_state = GBL_LOAD;
}
static void gb_field(const char *obj, const char *k, char *out, int cap) {
    const char *pth[1] = { k };
    out[0] = 0;
    const char *v = js_path(obj, pth, 1);
    if (!v) return;
    if (*v == '"') js_str(v, out, cap);
    else { int i = 0; while (v[i] && v[i] != ',' && v[i] != '}' && i < cap - 1) { out[i] = v[i]; i++; } out[i] = 0; }
}
static char *gb_js = NULL;                      /* page recue, lue petit a petit (20 stations par image) */
static const char *gb_jp = NULL;
static int gb_jgot = 0;
static int gb_parse_some(int max) {             /* -> 1 quand la page est finie */
    const char *p = js_ws(gb_jp);
    if (*p == '[') p = js_ws(p + 1);                    /* (debut de la liste) */
    int got = 0;
    while (*p == '{' && got < max) {
        const char *end = js_skip(p);
        char codec[16], url[URL_MAX], nm[200], la[32], lo[32], kb[16], hls[8], st[80], co[80], cc[8];
        gb_field(p, "codec", codec, sizeof(codec));
        gb_field(p, "url_resolved", url, sizeof(url));
        if (!url[0]) gb_field(p, "url", url, sizeof(url));
        gb_field(p, "geo_lat", la, sizeof(la));
        gb_field(p, "geo_long", lo, sizeof(lo));
        got++; gb_jgot++;
        int ok = (!strcasecmp(codec, "MP3") || !strncasecmp(codec, "AAC", 3) || !strcasecmp(codec, "OGG")) &&
                 !strncasecmp(url, "http", 4) && strlen(url) < URL_MAX - 1 && !ci_find(url, "opus") && !ci_find(url, "flac") &&
                 la[0] && lo[0] && strcmp(la, "null") && strcmp(lo, "null");
        if (ok) {
            gbst_t e; memset(&e, 0, sizeof(e));
            e.lat = (f32)atof(la); e.lon = (f32)atof(lo);
            if (e.lat >= -90 && e.lat <= 90 && e.lon >= -180 && e.lon <= 180 && (e.lat != 0 || e.lon != 0)) {
                gb_field(p, "name", nm, sizeof(nm));
                char *tn = trim(nm);
                for (char *c = tn; *c; c++) if (*c == '|') *c = '/';
                gb_field(p, "bitrate", kb, sizeof(kb));
                gb_field(p, "hls", hls, sizeof(hls));
                gb_field(p, "state", st, sizeof(st));
                gb_field(p, "country", co, sizeof(co));
                gb_field(p, "countrycode", cc, sizeof(cc));
                char place[170];
                char *ts = trim(st), *tc = trim(co);
                snprintf(place, sizeof(place), "%s%s%s", ts, ts[0] && tc[0] ? ", " : "", tc);
                e.name = gb_str(tn[0] ? tn : "Radio");
                e.url = gb_str(url);
                e.place = gb_str(place);
                e.kbps = (u16)atoi(kb);
                e.hls = hls[0] == '1' || hls[0] == 't';
                memcpy(e.cc, cc, 2);
                gb_xyz(&e);
                gb_add(&e);
            }
        }
        p = js_ws(end);
        if (*p == ',') p = js_ws(p + 1);
    }
    gb_jp = p;
    return *p != '{';
}
static void gb_page_done(void) {
    free(gb_js); gb_js = NULL; gb_jp = NULL;
    gb_page++;
    if (gb_jgot >= GB_PAGE && gb_page < GB_PAGES) { gb_fetch(); return; }
    gb_loaded = gb_n > 0;
    gb_state = gb_loaded ? GBL_DONE : GBL_ERR;
    if (gb_loaded) gb_save();
}
static void gb_pump(void) {
    if (gb_state == GBL_FILE && !gb_thr_busy) {           /* (fil fini : stations de la carte SD, ou l'annuaire) */
        if (gb_thr_file_ok) { gb_loaded = 1; gb_state = GBL_DONE; }
        else { gb_state = GBL_IDLE; gb_net_start(); }
    }
    if (gb_state != GBL_LOAD) return;
    if (gb_js) { if (gb_parse_some(20)) gb_page_done(); return; }
    hf_pump(&gb_hf);
    if (gb_hf.st == HF_FAIL) {
        if (gb_page == 0 && rb_servers[gb_srv + 1]) { gb_srv++; gb_fetch(); return; }
        hf_drop(&gb_hf);
        if (gb_n > 0) { gb_loaded = 1; gb_state = GBL_DONE; gb_save(); return; }   /* (ce qui est arrive suffit) */
        gb_state = GBL_ERR;
        return;
    }
    if (gb_hf.st != HF_DONE) return;
    gb_hf.st = HF_IDLE;
    gb_jgot = 0;
    if (gb_hf.buf) {
        char *js = (char *)realloc(gb_hf.buf, gb_hf.len + 1);
        if (js) { js[gb_hf.len] = 0; gb_js = js; gb_jp = js; gb_hf.buf = NULL; gb_hf.len = gb_hf.cap = 0; }
    }
    hf_drop(&gb_hf);
    if (!gb_js) gb_page_done();
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
    *sx = 320.0f + yr * gb_r;
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

/* v208 : la carte (1 Mo a decompresser) et les stations de la carte SD sont lues par un fil a part :
   l'ecran s'ouvre tout de suite et reste a 50 images/s (avant : une image de 220 ms a l'ouverture). */
static int gb_map_load_raw(void);
static lwp_t gb_thr_h = LWP_THREAD_NULL;
static u8 gb_thr_stack[32 * 1024] __attribute__((aligned(32)));
static void *gb_thread(void *arg) {
    (void)arg;
    gb_map_load_raw();
    if (gb_thr_file) gb_thr_file_ok = gb_load_file();
    gb_thr_busy = 0;
    return NULL;
}
static void gb_open(void) {
    tl_screen = TL_GLOBE;
    gb_pick = -1; gb_pick_k = 0; gb_travel = 0;
    gb_idle_t = now_ms();
    if (gb_thr_busy) return;
    if (gb_thr_h != LWP_THREAD_NULL) { LWP_JoinThread(gb_thr_h, NULL); gb_thr_h = LWP_THREAD_NULL; }
    int want_file = !gb_loaded && gb_state != GBL_LOAD;
    if (want_file) gb_state = GBL_FILE;
    gb_thr_file = want_file; gb_thr_file_ok = 0; gb_thr_busy = 1;
    if (LWP_CreateThread(&gb_thr_h, gb_thread, NULL, gb_thr_stack, sizeof(gb_thr_stack), 30) < 0) {   /* (pas de fil : tout de suite) */
        gb_thr_h = LWP_THREAD_NULL;
        gb_thread(NULL);
    }
}
static void gb_net_start(void) {
    if (net_phase != NET_DONE || !net_ok) { net_start_async(); gb_state = GBL_ERR; gb_note("R\xC3\xA9seau pas encore pr\xC3\xAAt : r\xC3\xA9" "essaie (A)"); return; }
    gb_clear();
    gb_page = 0; gb_srv = 0;
    gb_fetch();
}
static void gb_close(void) {
    tl_screen = TL_NONE;
    if (!gb_thr_busy) gb_map_free();             /* (1 Mo rendu ; la carte est redecompressee a la prochaine ouverture) */
    if (gb_state == GBL_LOAD) { hf_abort(&gb_hf); hf_drop(&gb_hf); free(gb_js); gb_js = NULL; gb_state = gb_n ? GBL_DONE : GBL_IDLE; gb_loaded = gb_n > 0; }
}

static void gb_input(u32 down, u32 nav, int fire) {
    (void)nav; (void)fire;
    if (down & PAD_BUTTON_B) { gb_close(); return; }
    if (!gb_loaded) {
        if ((down & PAD_BUTTON_A) && gb_state != GBL_LOAD && gb_state != GBL_FILE) { gb_state = GBL_IDLE; gb_net_start(); }
        return;
    }
    if (down) gb_idle_t = now_ms();
    int *near = gb_nearv, nn = gb_nn;
    if (down & PAD_TRIGGER_R) { if (nn) gb_pick_k = (gb_pick_k + 1) % nn; }
    if (down & PAD_TRIGGER_L) { if (nn) gb_pick_k = (gb_pick_k + nn - 1) % nn; }
    if (gb_pick_k >= nn) gb_pick_k = 0;
    gb_pick = nn ? near[gb_pick_k] : -1;
    if ((down & PAD_BUTTON_A) && gb_pick >= 0) gb_play(gb_pick);
    if ((down & PAD_BUTTON_Y) && gb_pick >= 0) gb_fav(gb_pick);
    if ((down & PAD_BUTTON_X) && gb_n) {                 /* au hasard : le globe y va, puis la radio joue */
        int i = (int)(lib_rand() % (u32)gb_n);
        gb_tlon = gb[i].lon; gb_tlat = gb[i].lat;
        gb_travel = 1; gb_travel_play = i;
        if (gb_r > 400) gb_r = 400;
    }
}
/* (chaque image, ecran ouvert) */
static void gb_step(void) {
    if (gb_loaded && !kb_is_open()) {                   /* stick : tourner ; stick C : zoom */
        int mx = PAD_StickX(0), my = PAD_StickY(0), cy = PAD_SubStickY(0);
        u32 held = PAD_ButtonsHeld(0);
        f32 vx = 0, vy = 0;
        if (abs(mx) > 14) vx = (f32)mx / 72.0f;
        if (abs(my) > 14) vy = (f32)my / 72.0f;
        if (held & PAD_BUTTON_LEFT) vx = -1;
        if (held & PAD_BUTTON_RIGHT) vx = 1;
        if (held & PAD_BUTTON_UP) vy = 1;
        if (held & PAD_BUTTON_DOWN) vy = -1;
        f32 sp = 70.0f * 168.0f / gb_r;                  /* (degres / s ; plus lent quand on zoome) */
        if (vx != 0 || vy != 0 || abs(cy) > 14) { gb_idle_t = now_ms(); gb_travel = 0; gb_pick_k = 0; }
        gb_lon += vx * sp * ui_dt;
        gb_lat += vy * sp * ui_dt;
        if (abs(cy) > 14) gb_r *= expf((f32)cy / 72.0f * 1.6f * ui_dt);
        if (gb_r < 120) gb_r = 120;
        if (gb_r > 2400) gb_r = 2400;
    }
    while (gb_lon > 180) gb_lon -= 360;
    while (gb_lon < -180) gb_lon += 360;
    if (gb_travel) {
        f32 dl = gb_tlon - gb_lon;
        while (dl > 180) dl -= 360;
        while (dl < -180) dl += 360;
        f32 k = 1.0f - expf(-ui_dt * 3.2f);
        gb_lon += dl * k;
        gb_lat += (gb_tlat - gb_lat) * k;
        if (fabsf(dl) < 0.05f && fabsf(gb_tlat - gb_lat) < 0.05f) {
            gb_travel = 0;
            if (gb_travel_play >= 0) { gb_play(gb_travel_play); gb_pick_k = 0; }
            gb_travel_play = -1;
        }
    } else if (gb_loaded && now_ms() - gb_idle_t > 8000) gb_lon += 4.0f * ui_dt;   /* (rien touche : la Terre tourne doucement) */
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
static u8 *volatile gb_map_raw = NULL;          /* (decompressee par le fil ; reprise par la boucle) */
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
    if (gb_thr_busy || !gb_map_raw) return 0;       /* (pas encore prete : le globe sans les terres) */
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
                    f32 l = 0.45f + 0.55f * d;                     /* (plus clair au centre : la lumiere vient de face) */
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
static void draw_globe(void) {
    gb_pump();
    gb_step();
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
        static f32 *dx = NULL, *dy = NULL; static u8 *da = NULL; static int dcap = 0;
        if (dcap < gb_n) {
            free(dx); free(dy); free(da);
            dx = (f32 *)malloc(sizeof(f32) * gb_n); dy = (f32 *)malloc(sizeof(f32) * gb_n); da = (u8 *)malloc(gb_n);
            dcap = dx && dy && da ? gb_n : 0;
        }
        int nv = 0;
        int nn = 0; f32 nd[16];                          /* (en meme temps : les radios pres du viseur, une seule passe) */
        for (int i = 0; i < gb_n && i < dcap; i++) {
            f32 sx, sy, d = gb_proj(gb[i].x, gb[i].y, gb[i].z, &sx, &sy);
            if (d <= 0.02f || sx < -4 || sx > 644 || sy < -4 || sy > 484) continue;
            dx[nv] = sx; dy[nv] = sy; da[nv] = (u8)(70 + 185 * d);
            nv++;
            f32 ex = sx - GB_CX, ey = sy - GB_CY, e = ex * ex + ey * ey;
            if (d > 0.05f && e <= 16.0f * 16.0f) {
                int k = nn < 16 ? nn++ : 16;
                if (k == 16) { if (e >= nd[15]) continue; k = 15; }
                while (k > 0 && nd[k - 1] > e) { nd[k] = nd[k - 1]; gb_nearv[k] = gb_nearv[k - 1]; k--; }
                nd[k] = e; gb_nearv[k] = i;
            }
        }
        gb_nn = nn;
        if (gb_pick_k >= gb_nn) gb_pick_k = 0;
        gb_pick = gb_nn ? gb_nearv[gb_pick_k] : -1;
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
    if (gb_loaded) snprintf(buf, sizeof(buf), "%d radios", gb_n);
    else if (gb_state == GBL_LOAD) snprintf(buf, sizeof(buf), "Chargement des radios du monde... %d", gb_n);
    else snprintf(buf, sizeof(buf), "%s", "Annuaire injoignable (A : r\xC3\xA9" "essayer)");
    gx_text1(F_SMALL, 24, 60, buf, gb_state == GBL_ERR && !gb_loaded ? RGBA(255, 160, 110, 255) : RGBA(170, 190, 240, 255));
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
        if (nn > 1) { snprintf(buf, sizeof(buf), "%d / %d ici (L / R)", gb_pick_k + 1, nn); gx_text_r(F_SMALL, 504, 398, buf, RGBA(255, 210, 120, 255)); }
    } else if (gb_loaded) gx_text_c(F_SMALL, 320, 392, "Vise un point lumineux : chaque point est une radio", RGBA(150, 170, 220, 200));
    if (now_ms() - gb_msg_t < 4000) gx_text_c(F_SMALL, 320, 342, gb_msg, RGBA(255, 210, 120, 255));
    static const char *const k[] = { "A", "X", "Y", "C", "B" };
    static const char *const l[] = { "\xC3\x89" "COUTER", "AU HASARD", "GARDER", "ZOOM", "FERMER" };
    tl_legend(k, l, 5);
}

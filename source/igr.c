// GC-MP3 v39 : retour en jeu (A + Z + START) vers GC Station, pour les jeux lances avec le
// lanceur de secours (inclus dans main.c, apres jeux_cfg.c). Reglage "IGRType" = Apploader :
// en jeu, A + Z + START charge SD:/gc-mp3/lanceur/patches/apploader.img et le lance.
// Le workflow GitHub fabrique ce fichier avec GC Station dedans (gc-mp3-igr-bN.img, release
// "dernier") : GC Station le telecharge. (v90 : plus dans SD:/swiss/patches.)
// Resident (touche A) a son propre retour en jeu (natres.c), sans ce fichier.
#define IGR_IMG     LD_DIR "/patches/apploader.img"
#define IGR_OLD     SD_ROOT "/swiss/patches/apploader.img"      /* (v89 et avant : repris une fois, mg_pump, lanceur.c) */
#define IGR_MINE    GCD "/igr.img"

enum { IGR_IDLE, IGR_CHECK, IGR_DL, IGR_W1, IGR_W2 };   /* v93 : W1 / W2 = ecriture par morceaux (igr.img, puis apploader.img) */
static int igr_state = IGR_IDLE, igr_cache = -1, igr_auto_done = 0, igr_enable_after = 0;
static int igr_have_b = 0;                    /* v81 : verification auto : numero installe (pas de telechargement si GitHub n'a pas plus recent) */
static char igr_msg[120] = "";
static char igr_url[URL_MAX];
static hfetch_t igr_hf = { .c = { -1, 0, 0 } };

static u32 igr_crc32(const u8 *p, long n) {
    static u32 tab[256];
    static int ready = 0;
    if (!ready) { for (u32 i = 0; i < 256; i++) { u32 c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; tab[i] = c; } ready = 1; }
    u32 c = 0xFFFFFFFFu;
    for (long i = 0; i < n; i++) c = tab[(c ^ p[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
/* en-tete : 16 octets de nom ("*gc-mp3-b123"), 8 octets, taille, crc32 des donnees */
static int igr_valid(const u8 *d, long n) {
    if (n < 0x40) return 0;
    u32 len = (u32)d[0x18] << 24 | (u32)d[0x19] << 16 | (u32)d[0x1A] << 8 | d[0x1B];
    u32 crc = (u32)d[0x1C] << 24 | (u32)d[0x1D] << 16 | (u32)d[0x1E] << 8 | d[0x1F];
    if (len + 0x20 != (u32)n || len > 6u * 1024 * 1024) return 0;
    return igr_crc32(d + 0x20, len) == crc;
}
/* build de GC-MP3 dans un apploader.img (0 = pas le notre) */
static int igr_build_of(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char h[17] = { 0 };
    int ok = fread(h, 1, 16, f) == 16;
    fclose(f);
    if (!ok || memcmp(h, "*gc-mp3", 7)) return 0;
    const char *b = strstr(h, "-b");
    return b ? (atoi(b + 2) > 0 ? atoi(b + 2) : 1) : 1;
}
static int igr_on(void) {
    if (igr_cache < 0) igr_cache = igr_build_of(IGR_IMG) > 0;
    return igr_cache;
}
/* v93 : ecriture d'un fichier par morceaux de 32 Ko, un par image (avant : tout d'un coup, ~1,5 s d'ecran
   fige pour 900 Ko, une fois apres chaque nouvelle version du lanceur ou du retour en jeu) */
typedef struct { FILE *f; const u8 *d; long n, o; char path[NAME_LEN + 8]; } fw_t;
static int fw_open(fw_t *w, const char *path, const u8 *d, long n) {
    snprintf(w->path, sizeof(w->path), "%s", path);
    w->f = fopen(path, "wb"); w->d = d; w->n = n; w->o = 0;
    return w->f != NULL;
}
static int fw_step(fw_t *w) {                        /* 1 = fini (ferme), 0 = encore, -1 = erreur (efface) */
    if (!w->f) return -1;
    long k = w->n - w->o < 32768 ? w->n - w->o : 32768;
    if (k > 0 && fwrite(w->d + w->o, 1, k, w->f) != (size_t)k) { fclose(w->f); w->f = NULL; remove(w->path); return -1; }
    w->o += k;
    if (w->o < w->n) return 0;
    int ok = fclose(w->f) == 0;
    w->f = NULL;
    if (!ok) { remove(w->path); return -1; }
    return 1;
}
static int igr_copy(const char *src, const char *dst) {
    long n = 0;
    u8 *d = read_file(src, 8 * 1024 * 1024, &n);
    if (!d) return 0;
    char tmp[NAME_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    FILE *f = fopen(tmp, "wb");
    int ok = f != NULL;
    for (long o = 0; ok && o < n; o += 65536) {
        long k = n - o < 65536 ? n - o : 65536;
        if (fwrite(d + o, 1, k, f) != (size_t)k) ok = 0;
        service_audio();
    }
    if (f && fclose(f) != 0) ok = 0;
    free(d);
    if (!ok) { remove(tmp); return 0; }
    remove(dst);
    if (rename(tmp, dst) != 0) { remove(tmp); return 0; }
    return 1;
}
/* reglage "IGRType" du lanceur de secours (global.ini) */
static void igr_set_type(const char *v) {
    int k = ic_find("IGRType");
    if (k < 0) return;
    if (ic_menu_loaded) { ic_set(k, v); return; }
    ic_use_global(); ic_load(); ic_set(k, v); ic_save();
}
static void igr_install(void) {
    mkdir(GCD, 0777); mkdir(LD_DIR, 0777); mkdir(LD_DIR "/patches", 0777);
    if (!igr_copy(IGR_MINE, IGR_IMG)) { snprintf(igr_msg, sizeof(igr_msg), "%s", TR("\xC3\x89" "criture sur la carte SD impossible")); return; }
    igr_set_type("Apploader");
    igr_cache = 1;
    snprintf(igr_msg, sizeof(igr_msg), "%s", TR("A + Z + START en jeu : retour sur GC Station"));
    crumb("retour en jeu : GC Station");
}
static void igr_disable(void) {
    remove(IGR_IMG);
    igr_set_type("Reboot");                                        /* (plus d'apploader : redemarrage) */
    snprintf(igr_msg, sizeof(igr_msg), "%s", TR("A + Z + START en jeu : red\xC3\xA9marrage de la console"));
    igr_cache = 0;
    crumb("retour en jeu (lanceur de secours) : non");
}
/* telecharge gc-mp3-igr-bN.img depuis la release "dernier" */
static int igr_via_api = 0;
static void igr_fetch(int enable_after) {
    if (igr_state != IGR_IDLE) return;
    if (net_phase != NET_DONE) { net_start_async(); snprintf(igr_msg, sizeof(igr_msg), "%s", TR("R\xC3\xA9seau en cours de connexion, r\xC3\xA9" "essaie")); return; }
    if (!net_ok) { snprintf(igr_msg, sizeof(igr_msg), "%s", TR("Pas de r\xC3\xA9seau")); return; }
    char url[256];
    igr_via_api = 0;
    upd_rel_url(url, sizeof(url), 0);                              /* v112 : release.json d'abord */
    igr_hf.maxbytes = 512 * 1024; igr_hf.image = 0; igr_hf.accept = "application/vnd.github+json";
    hf_start(&igr_hf, url, NULL);
    igr_state = IGR_CHECK; igr_enable_after = enable_after;
    snprintf(igr_msg, sizeof(igr_msg), "%s", TR("T\xC3\xA9l\xC3\xA9" "chargement..."));
}
static void igr_pump(void) {
    /* une fois par session : met a jour le fichier si GC-MP3 a ete mis a jour */
    if (!igr_auto_done && sd_ok && net_phase == NET_DONE && net_ok && now_ms() > 20000 && igr_state == IGR_IDLE && up_state != UP_DL && up_state != UP_WRITE && up_state != UP_CHECK) {
        igr_auto_done = 1;
        int b = igr_build_of(IGR_IMG);
        if (b > 0 && b < GCMP3_BUILD) { igr_fetch(1); if (igr_state == IGR_CHECK) igr_have_b = b; }   /* v81 : seulement s'il a change sur GitHub */
    }
    if (igr_state == IGR_IDLE) return;
    if (igr_state == IGR_W1 || igr_state == IGR_W2) {      /* v93 : un morceau par image */
        static fw_t w;
        if (!w.f) {
            mkdir(GCD, 0777); mkdir(LD_DIR, 0777); mkdir(LD_DIR "/patches", 0777);
            if (!fw_open(&w, igr_state == IGR_W1 ? IGR_MINE : IGR_IMG ".tmp", igr_hf.buf, igr_hf.len)) goto werr;
        }
        int r = fw_step(&w);
        if (r == 0) return;
        if (r < 0) goto werr;
        if (igr_state == IGR_W1 && igr_enable_after) { igr_state = IGR_W2; return; }
        if (igr_state == IGR_W2) {
            remove(IGR_IMG);
            if (rename(IGR_IMG ".tmp", IGR_IMG) != 0) goto werr;
            igr_set_type("Apploader");
            igr_cache = 1;
            snprintf(igr_msg, sizeof(igr_msg), "%s", TR("A + Z + START en jeu : retour sur GC Station"));
            crumb("retour en jeu : GC Station");
        }
        hf_drop(&igr_hf); igr_hf.st = HF_IDLE; igr_state = IGR_IDLE;
        return;
    werr:
        remove(IGR_IMG ".tmp");
        hf_drop(&igr_hf); igr_hf.st = HF_IDLE; igr_state = IGR_IDLE;
        snprintf(igr_msg, sizeof(igr_msg), "%s", TR("\xC3\x89" "criture sur la carte SD impossible"));
        return;
    }
    hf_pump(&igr_hf);
    if (igr_hf.st == HF_FAIL) {
        if (igr_state == IGR_CHECK && !igr_via_api) {               /* v112 : puis l'API */
            char url[256];
            hf_drop(&igr_hf); igr_hf.st = HF_IDLE;
            igr_via_api = 1;
            upd_rel_url(url, sizeof(url), 1);
            hf_start(&igr_hf, url, NULL);
            return;
        }
        hf_drop(&igr_hf); igr_hf.st = HF_IDLE; igr_state = IGR_IDLE; igr_have_b = 0;
        snprintf(igr_msg, sizeof(igr_msg), "%s", TR(upd_fail_msg(igr_hf.code)));
        return;
    }
    if (igr_hf.st != HF_DONE) return;
    if (igr_state == IGR_CHECK) {
        char *js = (char *)malloc(igr_hf.len + 1);
        igr_url[0] = 0;
        if (js) {
            memcpy(js, igr_hf.buf, igr_hf.len); js[igr_hf.len] = 0;
            const char *end = js + igr_hf.len;
            for (const char *p = js; p && p < end; p += 22) {
                p = strstr(p, "\"browser_download_url\"");
                if (!p) break;
                char u[URL_MAX];
                json_field(p, end, "browser_download_url", u, sizeof(u));
                int ul = (int)strlen(u);
                if (ul > 4 && !strcasecmp(u + ul - 4, ".img")) { snprintf(igr_url, sizeof(igr_url), "%s", u); break; }
            }
            free(js);
        }
        hf_drop(&igr_hf); igr_hf.st = HF_IDLE;
        if (!igr_url[0]) { igr_state = IGR_IDLE; igr_have_b = 0; snprintf(igr_msg, sizeof(igr_msg), "%s", TR("Pas encore de fichier pour le retour en jeu")); return; }
        if (igr_have_b) {                                    /* v81 : "gc-mp3-igr-b<N>.img" : N = version ou il a vraiment change */
            const char *nb = strstr(igr_url, "gc-mp3-igr-b");
            int n = nb ? atoi(nb + 12) : 0;
            int have = igr_have_b;
            igr_have_b = 0;
            if (n > 0 && n <= have) { igr_state = IGR_IDLE; igr_msg[0] = 0; crumb("retour en jeu : deja a jour (b%d, GitHub b%d)", have, n); return; }
            crumb("retour en jeu : mise a jour b%d -> b%d", have, n);
        }
        igr_hf.maxbytes = 6 * 1024 * 1024; igr_hf.image = 0; igr_hf.accept = "application/octet-stream";
        hf_start(&igr_hf, igr_url, NULL);
        igr_state = IGR_DL;
        return;
    }
    /* fichier recu : ecrit par morceaux (IGR_W1, puis IGR_W2 si on l'active) */
    if (!igr_valid(igr_hf.buf, igr_hf.len)) {
        igr_state = IGR_IDLE;
        hf_drop(&igr_hf); igr_hf.st = HF_IDLE;
        snprintf(igr_msg, sizeof(igr_msg), "%s", TR("Fichier re\xC3\xA7u invalide"));
        return;
    }
    igr_state = IGR_W1;
}
/* menu : A ou Gauche / Droite = changer */
static void igr_toggle(void) {
    if (igr_state != IGR_IDLE) return;
    if (igr_on()) { igr_disable(); return; }
    long n = 0;
    u8 *d = read_file(IGR_MINE, 8 * 1024 * 1024, &n);
    int have = d && igr_valid(d, n);              /* v81 : la copie gardee sert (son numero = version ou il a change) */
    free(d);
    if (have) igr_install();
    else igr_fetch(1);
}
static void igr_value(char *out, int cap) {
    if (igr_state != IGR_IDLE) snprintf(out, cap, "%s", TR("T\xC3\xA9l\xC3\xA9" "chargement..."));
    else snprintf(out, cap, "%s", igr_on() ? "Oui" : "Non (red\xC3\xA9marrage)");
}
static const char *igr_hint(void) {
    if (igr_msg[0]) return igr_msg;
    return "Jeux lanc\xC3\xA9s avec le lanceur de secours : A + Z + START = retour \xC3\xA0 GC Station";
}

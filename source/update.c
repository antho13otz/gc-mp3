// Mise a jour en un clic : le workflow GitHub publie chaque version reussie
// dans la release "dernier" du depot (gc-mp3player.dol + "build=N" dans le
// texte). GC-MP3 compare ce numero au sien, telecharge le .dol, le verifie,
// garde l'ancien (.ancien) et le remplace sur la carte SD.

#ifndef GCMP3_BUILD
#define GCMP3_BUILD 0
#endif
#ifndef UPD_API
#define UPD_API "https://api.github.com"
#endif

enum { UP_IDLE, UP_CHECK, UP_NONE, UP_AVAIL, UP_DL, UP_DONE, UP_ERR };
static int up_state = UP_IDLE;
static int up_build = 0;                       /* build disponible */
static char up_url[URL_MAX];
static long up_size = 0;
static char up_msg[160] = "";
static char up_target[NAME_LEN] = "";
static int up_auto_done = 0;
static u64 up_notice_until = 0;
static hfetch_t up_hf = { .c = { -1, 0, 0 } };
static const char *up_argv0 = NULL;

static void up_fail(const char *m) {
    snprintf(up_msg, sizeof(up_msg), "%s", m);
    up_state = UP_ERR;
    crumb("mise a jour : %s", m);
}

static void update_check(void) {
    if (up_state == UP_CHECK || up_state == UP_DL) return;
    if (net_phase != NET_DONE) { net_start_async(); up_fail("R\xC3\xA9seau en cours de connexion, r\xC3\xA9" "essaie"); return; }
    if (!net_ok) { up_fail("Pas de r\xC3\xA9seau"); return; }
    char url[256];
    snprintf(url, sizeof(url), UPD_API "/repos/%s/releases/tags/dernier", upd_repo);
    up_hf.maxbytes = 512 * 1024;
    up_hf.image = 0;
    up_hf.accept = "application/vnd.github+json";
    hf_start(&up_hf, url, NULL);
    up_state = UP_CHECK;
    crumb("mise a jour : verification");
}

/* le fichier .dol qui tourne : reglage, chemin de lancement, sinon recherche */
static int up_find_dol(const char *dir, int depth, char *out, int cap) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[NAME_LEN];
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        if (!strcasecmp(e->d_name, "gc-mp3player.dol")) { snprintf(out, cap, "%s", p); found = 1; break; }
        if (depth > 0) {
            struct stat st;
            if (stat(p, &st) == 0 && S_ISDIR(st.st_mode) && strcasecmp(e->d_name, "mp3") && strcasecmp(e->d_name, "retro")
                && strcasecmp(e->d_name, "photos") && strcasecmp(e->d_name, "videos") && strcasecmp(e->d_name, "podcasts"))
                found = up_find_dol(p, depth - 1, out, cap);
        }
    }
    closedir(d);
    return found;
}

static void up_pick_target(void) {
    struct stat st;
    if (upd_file[0]) { snprintf(up_target, sizeof(up_target), "%s", upd_file); return; }
    if (up_argv0 && strlen(up_argv0) > 4 && !strcasecmp(up_argv0 + strlen(up_argv0) - 4, ".dol") && stat(up_argv0, &st) == 0) {
        snprintf(up_target, sizeof(up_target), "%s", up_argv0);
        return;
    }
    if (up_find_dol(SD_ROOT, 2, up_target, sizeof(up_target))) return;
    snprintf(up_target, sizeof(up_target), SD_ROOT "/gc-mp3player.dol");
}

/* meme verification que le workflow : sections dans le fichier et en memoire */
static int up_dol_valid(const u8 *d, long n) {
    if (n < 0x100) return 0;
    #define R32(o) ((u32)d[o] << 24 | (u32)d[(o) + 1] << 16 | (u32)d[(o) + 2] << 8 | d[(o) + 3])
    u32 entry = R32(0xE0);
    int text_ok = 0, nsec = 0;
    for (int i = 0; i < 18; i++) {
        u32 off = R32(i * 4), adr = R32(0x48 + i * 4), len = R32(0x90 + i * 4);
        if (!len) continue;
        if (off < 0x100 || off + len > (u32)n || off + len < off) return 0;
        if (adr < 0x80000000u || adr + len > 0x81800000u) return 0;
        if (i < 7 && entry >= adr && entry < adr + len) text_ok = 1;
        nsec++;
    }
    #undef R32
    return nsec > 0 && text_ok;
}

static void up_install(const u8 *d, long n) {
    char part[NAME_LEN + 8], old[NAME_LEN + 8];
    up_pick_target();
    snprintf(part, sizeof(part), "%s.part", up_target);
    snprintf(old, sizeof(old), "%s.ancien", up_target);
    FILE *f = fopen(part, "wb");
    if (!f) { up_fail("Impossible d'\xC3\xA9" "crire sur la carte SD"); return; }
    size_t w = fwrite(d, 1, n, f);
    if (fclose(f) != 0 || w != (size_t)n) { remove(part); up_fail("\xC3\x89" "criture incompl\xC3\xA8te (carte pleine ?)"); return; }
    struct stat st;
    if (stat(part, &st) != 0 || st.st_size != n) { remove(part); up_fail("Fichier \xC3\xA9" "crit incorrect"); return; }
    remove(old);
    int had = stat(up_target, &st) == 0;
    if (had && rename(up_target, old) != 0) { remove(part); up_fail("Impossible de garder l'ancienne version"); return; }
    if (rename(part, up_target) != 0) {
        if (had) rename(old, up_target);
        up_fail("Impossible de remplacer le fichier");
        return;
    }
    up_state = UP_DONE;
    snprintf(up_msg, sizeof(up_msg), "Install\xC3\xA9" "e (build %d) : quitte et relance GC-MP3", up_build);
    crumb("mise a jour installee : build %d -> %s", up_build, up_target);
}

static void update_pump(void) {
    /* verification automatique, une fois, quand le reseau est pret */
    if (!up_auto_done && upd_auto && net_phase == NET_DONE && net_ok && now_ms() > 15000 && up_state == UP_IDLE) {
        up_auto_done = 1;
        update_check();
    }
    if (up_state != UP_CHECK && up_state != UP_DL) return;
    hf_pump(&up_hf);
    if (up_hf.st == HF_FAIL) {
        hf_drop(&up_hf);
        up_hf.st = HF_IDLE;
        if (up_state == UP_CHECK) {
            if (up_hf.code == 404) { up_state = UP_NONE; snprintf(up_msg, sizeof(up_msg), "Aucune version publi\xC3\xA9" "e"); }
            else up_fail("GitHub ne r\xC3\xA9pond pas");
        } else up_fail("T\xC3\xA9l\xC3\xA9" "chargement interrompu");
        return;
    }
    if (up_hf.st != HF_DONE) return;
    if (up_state == UP_CHECK) {
        const char *js = (const char *)up_hf.buf, *end = js + up_hf.len;
        char body[200];
        json_field(js, end, "body", body, sizeof(body));
        const char *b = strstr(body, "build=");
        up_build = b ? atoi(b + 6) : 0;
        up_url[0] = 0;
        up_size = 0;
        /* le .dol de ce build ("gc-mp3player-b123.dol"), sinon le dernier .dol de la liste */
        char want[24];
        snprintf(want, sizeof(want), "-b%d.dol", up_build);
        for (const char *p = js; p && p < end; ) {
            p = strstr(p, "\"browser_download_url\"");
            if (!p || p >= end) break;
            char u[URL_MAX];
            json_field(p, end, "browser_download_url", u, sizeof(u));
            int ul = (int)strlen(u), wl = (int)strlen(want);
            if (ul > 4 && !strcasecmp(u + ul - 4, ".dol")) {
                snprintf(up_url, sizeof(up_url), "%s", u);
                if (ul > wl && !strcasecmp(u + ul - wl, want)) break;
            }
            p += 22;
        }
        hf_drop(&up_hf);
        up_hf.st = HF_IDLE;
        if (!up_build || !up_url[0]) { up_fail("Version publi\xC3\xA9" "e illisible"); return; }
        if (up_build <= GCMP3_BUILD) {
            up_state = UP_NONE;
            snprintf(up_msg, sizeof(up_msg), "\xC3\x80 jour (build %d)", GCMP3_BUILD);
        } else {
            up_state = UP_AVAIL;
            snprintf(up_msg, sizeof(up_msg), "Build %d disponible : A pour installer", up_build);
            up_notice_until = now_ms() + 10000;
            crumb("mise a jour disponible : build %d", up_build);
        }
        return;
    }
    /* telechargement fini */
    if (!up_dol_valid(up_hf.buf, up_hf.len)) { hf_drop(&up_hf); up_hf.st = HF_IDLE; up_fail("Fichier re\xC3\xA7u invalide"); return; }
    up_install(up_hf.buf, up_hf.len);
    hf_drop(&up_hf);
    up_hf.st = HF_IDLE;
}

static void update_start_download(void) {
    if (up_state != UP_AVAIL) return;
    up_hf.maxbytes = 16 * 1024 * 1024;
    up_hf.image = 0;
    up_hf.accept = "application/octet-stream";
    hf_start(&up_hf, up_url, NULL);
    up_state = UP_DL;
    crumb("mise a jour : telechargement build %d", up_build);
}

/* touche A sur la ligne du menu */
static void update_activate(void) {
    if (sd_virtual) { up_fail("Pas de carte SD : mise \xC3\xA0 jour impossible ici"); return; }
    if (up_state == UP_AVAIL) update_start_download();
    else if (up_state == UP_DONE) quit_requested = 1;
    else update_check();
}

static void update_value(char *out, int cap) {
    switch (up_state) {
    case UP_CHECK: snprintf(out, cap, "V\xC3\xA9rification..."); break;
    case UP_DL:
        if (up_hf.st == HF_BODY && up_hf.expect > 0)
            snprintf(out, cap, "T\xC3\xA9l\xC3\xA9" "chargement %ld %%", (long)((long long)up_hf.len * 100 / up_hf.expect));
        else snprintf(out, cap, "T\xC3\xA9l\xC3\xA9" "chargement...");
        break;
    case UP_IDLE: snprintf(out, cap, "Build %d  (A : v\xC3\xA9rifier)", GCMP3_BUILD); break;
    default: snprintf(out, cap, "%s", up_msg); break;
    }
}

static void draw_update_notice(void) {
    if (!up_notice_until || now_ms() >= up_notice_until || up_state != UP_AVAIL) return;
    char m[120];
    snprintf(m, sizeof(m), "Nouvelle version de GC-MP3 (build %d) : START > Mise \xC3\xA0 jour", up_build);
    int w = text_width(F_SMALL, m, 0) + 30;
    gx_rrect_fill(320 - w / 2.0f, 92, (f32)w, 26, 9, RGBA(20, 70, 40, 240), RGBA(20, 70, 40, 240));
    gx_text_c(F_SMALL, 320, 110, m, RGBA(255, 255, 255, 255));
}

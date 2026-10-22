// GC-MP3 v44 : lanceur de jeux GC-MP3 (inclus dans main.c, apres igr.c)
// Le moteur de Swiss (licence GPL v2), modifie pour GC-MP3 (tools/mkloader.py) : le jeu
// demarre tout de suite, sans menu Swiss, aux couleurs de GC-MP3 ; en cas d'erreur
// (ou B maintenu puis B), retour sur GC-MP3. Il est compile par GitHub
// (gc-mp3-loader-bN.bin, release "dernier") : GC-MP3 le telecharge dans
// SD:/gc-mp3/lanceur.dol et le tient a jour. Reglage : START > Reglages > Lanceur de jeux.
/* (LD_DOL, LD_VER : dans gclib.c) */

enum { LD_IDLE, LD_CHECK, LD_DL };
static int ld_state = LD_IDLE, ld_auto_done = 0, ld_cache = -1, ld_new_b = 0, ld_loud = 0;
/* (ld_mode : dans main.c) */
static char ld_msg[120] = "";
static char ld_url[URL_MAX];
static hfetch_t ld_hf = { .c = { -1, 0, 0 } };

/* numero du lanceur installe (0 = aucun) */
static int ld_build(void) {
    if (ld_cache >= 0) return ld_cache;
    ld_cache = 0;
    struct stat st;
    if (!sd_ok || stat(LD_DOL, &st) != 0 || st.st_size < 0x100) return 0;
    long n = 0;
    u8 *d = read_file(LD_VER, 64, &n);
    if (d) {
        char b[16] = { 0 };
        memcpy(b, d, n < 15 ? n : 15);
        ld_cache = atoi(b) > 0 ? atoi(b) : 1;
        free(d);
    } else ld_cache = 1;
    return ld_cache;
}
static int ld_ready(void) { return ld_mode == 0 && ld_build() > 0; }

/* un .dol GameCube correct (sections dans le fichier et en memoire, point d'entree, "_arg") */
static int ld_dol_ok(const u8 *d, long n) {
    if (n < 0x100 || n > 8 * 1024 * 1024) return 0;
    u32 entry = be32(d + 0xE0);
    int text = 0, args = 0;
    for (int i = 0; i < 18; i++) {
        u32 off = be32(d + i * 4), addr = be32(d + 0x48 + i * 4), sz = be32(d + 0x90 + i * 4);
        if (!sz) continue;
        if (off < 0x100 || off + (u64)sz > (u64)n || addr < 0x80000000u || addr + (u64)sz > 0x81800000u) return 0;
        if (i < 7 && entry >= addr && entry < addr + sz) {
            text = 1;
            if (entry + 8 <= addr + sz) args = !memcmp(d + off + (entry + 4 - addr), "_arg", 4);
        }
    }
    return text && args;
}

static void ld_fetch(int loud) {
    if (ld_state != LD_IDLE) return;
    ld_loud = loud;
    if (net_phase != NET_DONE) { net_start_async(); if (loud) snprintf(ld_msg, sizeof(ld_msg), "%s", TR("R\xC3\xA9seau en cours de connexion, r\xC3\xA9" "essaie")); return; }
    if (!net_ok) { if (loud) snprintf(ld_msg, sizeof(ld_msg), "%s", TR("Pas de r\xC3\xA9seau")); return; }
    char url[256];
    snprintf(url, sizeof(url), UPD_API "/repos/%s/releases/tags/dernier", upd_repo);
    ld_hf.maxbytes = 512 * 1024; ld_hf.image = 0; ld_hf.accept = "application/vnd.github+json";
    hf_start(&ld_hf, url, NULL);
    ld_state = LD_CHECK;
    if (loud) snprintf(ld_msg, sizeof(ld_msg), "%s", TR("Recherche du lanceur..."));
}

static void ld_pump(void) {
    /* une fois par session : lanceur absent ou plus ancien que celui de GitHub */
    if (!ld_auto_done && ld_mode == 0 && sd_ok && net_phase == NET_DONE && net_ok && now_ms() > 25000 && ld_state == LD_IDLE
        && igr_state == IGR_IDLE && up_state != UP_DL && up_state != UP_WRITE) {
        ld_auto_done = 1;
        ld_fetch(0);
    }
    if (ld_state == LD_IDLE) return;
    hf_pump(&ld_hf);
    if (ld_hf.st == HF_FAIL) {
        hf_drop(&ld_hf); ld_hf.st = HF_IDLE; ld_state = LD_IDLE;
        if (ld_loud) snprintf(ld_msg, sizeof(ld_msg), "%s", TR("GitHub ne r\xC3\xA9pond pas"));
        return;
    }
    if (ld_hf.st != HF_DONE) return;
    if (ld_state == LD_CHECK) {
        char *js = (char *)malloc(ld_hf.len + 1);
        ld_url[0] = 0; ld_new_b = 0;
        if (js) {
            memcpy(js, ld_hf.buf, ld_hf.len); js[ld_hf.len] = 0;
            const char *end = js + ld_hf.len;
            for (const char *p = js; p && p < end; p += 22) {
                p = strstr(p, "\"browser_download_url\"");
                if (!p) break;
                char u[URL_MAX];
                json_field(p, end, "browser_download_url", u, sizeof(u));
                int ul = (int)strlen(u);
                const char *b = strstr(u, "gc-mp3-loader-b");
                if (b && ul > 4 && !strcasecmp(u + ul - 4, ".bin")) {
                    snprintf(ld_url, sizeof(ld_url), "%s", u);
                    ld_new_b = atoi(b + 15);
                    break;
                }
            }
            free(js);
        }
        hf_drop(&ld_hf); ld_hf.st = HF_IDLE;
        if (!ld_url[0]) { ld_state = LD_IDLE; if (ld_loud) snprintf(ld_msg, sizeof(ld_msg), "%s", TR("Pas encore de lanceur sur GitHub")); return; }
        if (ld_build() > 0 && ld_new_b > 0 && ld_new_b <= ld_build()) {
            ld_state = LD_IDLE;
            if (ld_loud) snprintf(ld_msg, sizeof(ld_msg), "%s", TR("Lanceur d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour"));
            return;
        }
        ld_hf.maxbytes = 8 * 1024 * 1024; ld_hf.image = 0; ld_hf.accept = "application/octet-stream";
        hf_start(&ld_hf, ld_url, NULL);
        ld_state = LD_DL;
        snprintf(ld_msg, sizeof(ld_msg), "%s", TR("T\xC3\xA9l\xC3\xA9" "chargement du lanceur..."));
        crumb("lanceur : telechargement b%d", ld_new_b);
        return;
    }
    /* fichier recu */
    ld_state = LD_IDLE;
    int ok = ld_dol_ok(ld_hf.buf, ld_hf.len);
    if (ok) {
        mkdir(GCD, 0777);
        FILE *f = fopen(LD_DOL ".tmp", "wb");
        ok = f && fwrite(ld_hf.buf, 1, ld_hf.len, f) == (size_t)ld_hf.len;
        if (f && fclose(f) != 0) ok = 0;
        if (ok) { remove(LD_DOL); ok = rename(LD_DOL ".tmp", LD_DOL) == 0; }
        if (!ok) remove(LD_DOL ".tmp");
    }
    hf_drop(&ld_hf); ld_hf.st = HF_IDLE;
    if (!ok) { snprintf(ld_msg, sizeof(ld_msg), "%s", TR("Fichier re\xC3\xA7u invalide")); return; }
    FILE *v = fopen(LD_VER, "w");
    if (v) { fprintf(v, "%d\n", ld_new_b > 0 ? ld_new_b : 1); fclose(v); }
    ld_cache = -1;
    snprintf(ld_msg, sizeof(ld_msg), "%s", TR("Lanceur GC-MP3 pr\xC3\xAAt : les jeux d\xC3\xA9marrent sans Swiss"));
    crumb("lanceur : installe b%d", ld_new_b);
}

/* menu : Gauche / Droite = GC-MP3 ou Swiss ; A = telecharger / mettre a jour */
static void ld_change(int a) {
    if (!a) { ld_mode = !ld_mode; settings_dirty = 1; ld_msg[0] = 0; return; }
    if (ld_mode == 1) { ld_mode = 0; settings_dirty = 1; }
    ld_fetch(1);
}
static void ld_value(char *out, int cap) {
    if (ld_mode == 1) { snprintf(out, cap, "Swiss / Indigo"); return; }
    if (ld_state != LD_IDLE) snprintf(out, cap, "%s", TR("T\xC3\xA9l\xC3\xA9" "chargement..."));
    else if (ld_build() > 0) snprintf(out, cap, "GC-MP3 (b%d)", ld_build());
    else snprintf(out, cap, "%s", TR("GC-MP3 (A : t\xC3\xA9l\xC3\xA9" "charger)"));
}
static const char *ld_hint(void) {
    if (ld_msg[0]) return ld_msg;
    if (ld_mode == 1) return "Les jeux d\xC3\xA9marrent avec ton Swiss / Indigo";
    return "Jeux lanc\xC3\xA9s sans le menu de Swiss (B maintenu : \xC3\xA9" "cran du jeu). A : mettre \xC3\xA0 jour";
}

// Mise a jour en un clic : le workflow GitHub publie chaque version reussie
// dans la release "dernier" du depot (gc-mp3player.dol + "build=N" dans le
// texte). GC-MP3 compare ce numero au sien, telecharge le .dol, le verifie,
// garde l'ancien (.ancien) et le remplace sur la carte SD.

#ifndef GCMP3_BUILD
#define GCMP3_BUILD 0
#endif
#ifndef UPD_API
#define UPD_API "https://api.github.com"
/* v112 : la release est lue d'abord dans le fichier release.json publie avec elle (github.com, sans la limite
   de l'API : 60 demandes par heure et par connexion, vite depassee quand GC Station redemarre souvent),
   puis par l'API si ce fichier manque. */
static void upd_rel_url(char *url, int cap, int api) {
    if (api) snprintf(url, cap, UPD_API "/repos/%s/releases/tags/dernier", upd_repo);
    else snprintf(url, cap, "https://github.com/%s/releases/download/dernier/release.json", upd_repo);
}
static const char *upd_fail_msg(int code) {
    return code == 403 || code == 429 ? "GitHub : trop de v\xC3\xA9rifications, r\xC3\xA9" "essaie dans une heure" : "GitHub ne r\xC3\xA9pond pas";
}
static int up_via_api = 0;
#endif

enum { UP_IDLE, UP_CHECK, UP_NONE, UP_AVAIL, UP_DL, UP_DONE, UP_ERR, UP_WRITE };
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
    if (up_state == UP_CHECK || up_state == UP_DL || up_state == UP_WRITE) return;
    if (net_phase != NET_DONE) { net_start_async(); up_fail("R\xC3\xA9seau en cours de connexion, r\xC3\xA9" "essaie"); return; }
    if (!net_ok) { up_fail("Pas de r\xC3\xA9seau"); return; }
    char url[256];
    up_via_api = 0;
    upd_rel_url(url, sizeof(url), 0);
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
        if (depth > 0 && (depth < 2 || !strcasecmp(e->d_name, "apps")) && is_dir(p, e))   /* (v36 : racine + apps seulement) */
            found = up_find_dol(p, depth - 1, out, cap);
    }
    closedir(d);
    return found;
}

/* v30 : GC-MP3 renomme (ex. ipl.dol pour demarrer directement) : on le reconnait
   a son contenu (texte "INDIGO AUDIO PLAYER" dans le .dol) */
/* v92 : la reponse est gardee dans SD:/gc-mp3/cache/dol-id.txt (chemin|taille|date|0-1) : sans ca, chaque
   demarrage relisait ipl.dol en entier (plusieurs Mo : quelques secondes avant le premier ecran) */
static int up_is_me_scan(const char *path);
static void up_idc_store(const char *path, int r);
#define UP_IDC GCD "/cache/dol-id.txt"
static int up_is_me(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    char key[NAME_LEN + 48];
    int kl = snprintf(key, sizeof(key), "%s|%ld|%ld|", path, (long)st.st_size, (long)st.st_mtime);
    long n = 0;
    char *c = (char *)read_file(UP_IDC, 16 * 1024, &n);
    if (c) {
        char *t = (char *)realloc(c, n + 1);
        if (t) {
            t[n] = 0;
            for (char *l = t; l && *l; ) {
                char *e = strchr(l, '\n');
                if (e) *e = 0;
                if (!strncmp(l, key, kl)) { int r = l[kl] == '1'; free(t); return r; }
                l = e ? e + 1 : NULL;
            }
            free(t);
        } else free(c);
    }
    int r = up_is_me_scan(path);
    up_idc_store(path, r);
    return r;
}
/* nouvelle ligne pour ce fichier (les anciennes lignes du meme chemin sont enlevees) */
static void up_idc_store(const char *path, int r) {
    struct stat st;
    if (stat(path, &st) != 0) return;
    char key[NAME_LEN + 48];
    snprintf(key, sizeof(key), "%s|%ld|%ld|", path, (long)st.st_size, (long)st.st_mtime);
    long n = 0;
    char *c = (char *)read_file(UP_IDC, 16 * 1024, &n);
    mkdir(GCD, 0777); mkdir(GCD "/cache", 0777);
    FILE *f = fopen(UP_IDC ".tmp", "w");
    if (f) {
        fprintf(f, "%s%d\n", key, r);
        size_t pl = strlen(path);
        for (long i = 0; c && i < n; ) {
            long s0 = i;
            while (i < n && c[i] != '\n') i++;
            if (i - s0 > 0 && !(i - s0 > (long)pl && !memcmp(c + s0, path, pl) && c[s0 + pl] == '|')) { fwrite(c + s0, 1, i - s0, f); fputc('\n', f); }
            i++;
        }
        if (fclose(f) == 0) { remove(UP_IDC); rename(UP_IDC ".tmp", UP_IDC); }
    }
    free(c);
}
static int up_is_me_scan(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static u8 b[65536 + 32];
    const char *pat = "INDIGO AUDIO PLAYER";
    int m = (int)strlen(pat), found = 0;
    long tot = 0;
    size_t n;
    memset(b, 0, 32);
    while (!found && tot < (6L << 20) && (n = fread(b + 32, 1, 65536, f)) > 0) {
        for (size_t i = 0; i + m <= n + 32; i++) if (b[i] == 'I' && !memcmp(b + i, pat, m)) { found = 1; break; }
        memmove(b, b + n, 32);
        tot += (long)n;
        service_audio();
    }
    fclose(f);
    return found;
}
/* v92 : numero de build ecrit par GitHub dans les 32 derniers octets du .dol ("GCSTATION-BUILD=393") :
   lu sans relire tout le fichier (0 = inconnu) */
static int up_dol_build(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char t[33] = { 0 };
    int ok = fseek(f, -32, SEEK_END) == 0 && fread(t, 1, 32, f) == 32;
    fclose(f);
    return ok && !memcmp(t, "GCSTATION-BUILD=", 16) ? atoi(t + 16) : 0;
}
static char up_found[NAME_LEN] = "";                  /* (v57 : remis a zero quand ipl.dol change) */
static void up_pick_target(void) {
    struct stat st;
    if (upd_file[0]) { snprintf(up_target, sizeof(up_target), "%s", upd_file); return; }
    {   /* deja trouve une fois (le contenu est long a verifier) */
        char *found = up_found;
        static const char *const boot[] = { SD_ROOT "/ipl.dol", SD_ROOT "/boot.dol", SD_ROOT "/autoexec.dol", SD_ROOT "/a.dol", NULL };
        if (!found[0])
            for (int i = 0; boot[i]; i++) if (stat(boot[i], &st) == 0 && up_is_me(boot[i])) { snprintf(found, NAME_LEN, "%s", boot[i]); break; }
        if (found[0]) { snprintf(up_target, sizeof(up_target), "%s", found); return; }
    }
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

/* v27 : le .dol est ecrit par morceaux, une partie a chaque image (avant : tout
   d'un coup a 99 %, l'ecran se figeait quelques secondes) */
static u8 *uw_buf = NULL;
static long uw_len = 0, uw_pos = 0;
static FILE *uw_f = NULL;
static char uw_part[NAME_LEN + 8];
static void up_install_finish(const u8 *d, long n);
static u8 *up_mem = NULL;                                /* v117 : sans carte SD (Dolphin) : la version telechargee, en memoire */
static long up_mem_len = 0;
/* v117 / v119 : pas de carte SD, ou carte ou l'on ne peut pas ecrire (Dolphin...) : la version telechargee
   reste en memoire et se lance directement (A), sans etre enregistree */
static void up_to_memory(u8 *d, long n, const char *why) {
    if (up_mem && up_mem != d) free(up_mem);
    up_mem = d; up_mem_len = n;
    up_state = UP_DONE;
    snprintf(up_msg, sizeof(up_msg), "Build %d pr\xC3\xAAte (%s : non enregistr\xC3\xA9" "e) : A pour la lancer", up_build, why);
    crumb("mise a jour en memoire (%s) : build %d, %ld octets", why, up_build, n);
}
static void up_install(u8 *d, long n) {                  /* garde d (libere a la fin) */
    up_pick_target();
    if (sd_virtual) { up_to_memory(d, n, "pas de carte SD"); return; }
    snprintf(uw_part, sizeof(uw_part), "%s.part", up_target);
    uw_f = fopen(uw_part, "wb");
    if (!uw_f) { up_to_memory(d, n, "carte SD en lecture seule"); return; }
    uw_buf = d; uw_len = n; uw_pos = 0;
    up_state = UP_WRITE;
}
static void up_write_pump(void) {
    if (up_state != UP_WRITE || !uw_f) return;
    u64 t0 = now_ms();
    while (uw_pos < uw_len && now_ms() - t0 < 12) {
        long k = uw_len - uw_pos < 32768 ? uw_len - uw_pos : 32768;
        if (fwrite(uw_buf + uw_pos, 1, k, uw_f) != (size_t)k) break;
        uw_pos += k;
        service_audio();
    }
    if (uw_pos < uw_len && now_ms() - t0 < 12) {          /* ecriture refusee */
        fclose(uw_f); uw_f = NULL; remove(uw_part);
        up_to_memory(uw_buf, uw_len, "carte SD pleine ou en lecture seule");
        uw_buf = NULL;
        return;
    }
    if (uw_pos < uw_len) return;                          /* (la suite a l'image suivante) */
    int ok = fclose(uw_f) == 0;
    uw_f = NULL;
    if (!ok) { remove(uw_part); up_to_memory(uw_buf, uw_len, "carte SD pleine ou en lecture seule"); uw_buf = NULL; return; }
    up_install_finish(uw_buf, uw_len);
    if (up_mem == uw_buf) uw_buf = NULL;                 /* (gardee en memoire : l'ecriture a echoue) */
    free(uw_buf); uw_buf = NULL;
}
/* v33 : apres l'installation, les autres copies de GC-MP3 sur la carte sont aussi
   remplacees (ex. ipl.dol a la racine ET gc-mp3player.dol dans apps) */
#define UP_MAXCOPY 8
static char up_copies[UP_MAXCOPY][NAME_LEN];
static int up_ncopies = 0;
static void up_copy_add(const char *p) {
    if (up_ncopies >= UP_MAXCOPY || !strcasecmp(p, up_target)) return;
    for (int i = 0; i < up_ncopies; i++) if (!strcasecmp(up_copies[i], p)) return;
    snprintf(up_copies[up_ncopies++], NAME_LEN, "%s", p);
}
static int up_dol_name(const char *n) {                  /* nom qui peut etre GC-MP3 */
    size_t l = strlen(n);
    if (l < 5 || strcasecmp(n + l - 4, ".dol")) return 0;
    char low[NAME_LEN]; size_t i = 0;
    for (; n[i] && i < sizeof(low) - 1; i++) low[i] = (char)tolower((u8)n[i]);
    low[i] = 0;
    return strstr(low, "mp3") || !strcmp(low, "boot.dol") || !strcmp(low, "ipl.dol") || !strcmp(low, "autoexec.dol") || !strcmp(low, "a.dol");
}
static void up_find_copies(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && up_ncopies < UP_MAXCOPY) {
        if (e->d_name[0] == '.') continue;
        char p[NAME_LEN];
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        /* v36 : le nom d'abord, sans stat() sur chaque fichier (dossiers de milliers de ROM = tres lent),
           et seulement la racine + le dossier apps */
        if (is_dir(p, e)) {
            if (depth > 0 && (depth < 2 || !strcasecmp(e->d_name, "apps"))) up_find_copies(p, depth - 1);
            continue;
        }
        if (!up_dol_name(e->d_name) || !strcasecmp(p, up_target)) continue;
        struct stat st;
        if (stat(p, &st) == 0 && st.st_size > 0x100 && up_is_me(p)) up_copy_add(p);
    }
    closedir(d);
}
static int up_write_copy(const char *path, const u8 *d, long n) {
    char part[NAME_LEN + 8];
    snprintf(part, sizeof(part), "%s.part", path);
    FILE *f = fopen(part, "wb");
    if (!f) return 0;
    long pos = 0;
    while (pos < n) {
        long k = n - pos < 65536 ? n - pos : 65536;
        if (fwrite(d + pos, 1, k, f) != (size_t)k) break;
        pos += k;
        service_audio();
    }
    if (fclose(f) != 0 || pos < n) { remove(part); return 0; }
    remove(path);
    if (rename(part, path) != 0) { remove(part); return 0; }
    return 1;
}
static void up_install_finish(const u8 *d, long n) {
    char part[NAME_LEN + 8], old[NAME_LEN + 8];
    snprintf(part, sizeof(part), "%s", uw_part);
    snprintf(old, sizeof(old), "%s.ancien", up_target);
    struct stat st;
    if (stat(part, &st) != 0 || st.st_size != n) { remove(part); up_to_memory((u8 *)d, n, "fichier mal \xC3\xA9" "crit sur la carte"); return; }
    remove(old);
    int had = stat(up_target, &st) == 0;
    if (had && rename(up_target, old) != 0) { remove(part); up_to_memory((u8 *)d, n, "fichier impossible \xC3\xA0 remplacer"); return; }
    if (rename(part, up_target) != 0) {
        if (had) rename(old, up_target);
        up_to_memory((u8 *)d, n, "fichier impossible \xC3\xA0 remplacer");
        return;
    }
    up_idc_store(up_target, 1);                          /* v92 : c'est GC Station (pas besoin de le relire au demarrage) */
    /* les autres copies (ipl.dol, apps...) */
    ui_status("Mise \xC3\xA0 jour des autres copies de GC Station...");
    up_ncopies = 0;
    up_find_copies(SD_ROOT, 2);
    int nok = 0;
    for (int i = 0; i < up_ncopies; i++) {
        int ok = up_write_copy(up_copies[i], d, n);
        nok += ok;
        if (ok) up_idc_store(up_copies[i], 1);
        crumb("mise a jour : copie %s %s", up_copies[i], ok ? "ok" : "ECHEC");
    }
    up_state = UP_DONE;
    snprintf(up_msg, sizeof(up_msg), "Install\xC3\xA9" "e (build %d) : A pour relancer GC Station", up_build);   /* v38 */
    (void)nok;
    crumb("mise a jour installee : build %d -> %s (+%d copies)", up_build, up_target, nok);
}

static void update_pump(void) {
    /* verification automatique, une fois, quand le reseau est pret */
    if (!up_auto_done && upd_auto && net_phase == NET_DONE && net_ok && now_ms() > 15000 && up_state == UP_IDLE) {
        up_auto_done = 1;
        update_check();
    }
    if (up_state == UP_WRITE) { up_write_pump(); return; }
    if (up_state != UP_CHECK && up_state != UP_DL) return;
    hf_pump(&up_hf);
    if (up_hf.st == HF_FAIL) {
        hf_drop(&up_hf);
        up_hf.st = HF_IDLE;
        if (up_state == UP_CHECK) {
            if (!up_via_api) {                                    /* v112 : release.json absent : l'API */
                char url[256];
                up_via_api = 1;
                upd_rel_url(url, sizeof(url), 1);
                hf_start(&up_hf, url, NULL);
                return;
            }
            if (up_hf.code == 404) { up_state = UP_NONE; snprintf(up_msg, sizeof(up_msg), "Aucune version publi\xC3\xA9" "e"); }
            else up_fail(upd_fail_msg(up_hf.code));
        } else up_fail("T\xC3\xA9l\xC3\xA9" "chargement interrompu");
        return;
    }
    if (up_hf.st != HF_DONE) return;
    if (up_state == UP_CHECK) {
        char *jsb = (char *)malloc(up_hf.len + 1);              /* (texte termine par un zero) */
        if (!jsb) { hf_drop(&up_hf); up_hf.st = HF_IDLE; up_fail("M\xC3\xA9moire insuffisante"); return; }
        memcpy(jsb, up_hf.buf, up_hf.len);
        jsb[up_hf.len] = 0;
        const char *js = jsb, *end = js + up_hf.len;
        /* numero du build : "build=N" dans le texte de la release (cherche dans tout
           le texte, meme s'il a ete modifie a la main), sinon dans le nom du .dol */
        up_build = 0;
        for (const char *p = js; p + 6 < end; p++)
            if (!memcmp(p, "build=", 6) && isdigit((unsigned char)p[6])) { int v = atoi(p + 6); if (v > up_build) up_build = v; }
        up_url[0] = 0;
        up_size = 0;
        int dols = 0, best_b = -1;
        char want[24];
        snprintf(want, sizeof(want), "-b%d.dol", up_build);
        for (const char *p = js; p && p < end; ) {
            p = strstr(p, "\"browser_download_url\"");
            if (!p || p >= end) break;
            char u[URL_MAX];
            json_field(p, end, "browser_download_url", u, sizeof(u));
            int ul = (int)strlen(u), wl = (int)strlen(want);
            if (ul > 4 && !strcasecmp(u + ul - 4, ".dol")) {
                dols++;
                int nb = -1;                                         /* "gc-mp3player-b123.dol" */
                for (const char *q = u; (q = strstr(q, "-b")) != NULL; q += 2) if (isdigit((unsigned char)q[2])) nb = atoi(q + 2);
                if (up_build && ul > wl && !strcasecmp(u + ul - wl, want)) { snprintf(up_url, sizeof(up_url), "%s", u); best_b = up_build; break; }
                if (nb > best_b || !up_url[0]) { if (nb > best_b) best_b = nb; snprintf(up_url, sizeof(up_url), "%s", u); }
            }
            p += 22;
        }
        if (!up_build && best_b > 0) up_build = best_b;          /* (texte de la release illisible) */
        free(jsb);
        hf_drop(&up_hf);
        up_hf.st = HF_IDLE;
        crumb("maj : build=%d dol=%d url=%.60s", up_build, dols, up_url);
        if (!dols) { up_fail("Pas encore de fichier .dol (compilation en cours ?)"); return; }
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
    u8 *b = up_hf.buf; long bl = up_hf.len;
    up_hf.buf = NULL; up_hf.len = up_hf.cap = 0;        /* (le tampon passe a l'ecriture) */
    up_hf.st = HF_IDLE;
    up_install(b, bl);
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
/* v38 : relance la nouvelle version directement (sans redemarrer la console : un
   redemarrage peut ouvrir le lecteur de disque au lieu de GC-MP3) */
static const char *gl_boot_dol(const char *dolpath, const char *a1, const char *a2, const char *what);
static const char *gl_boot_mem(unsigned char *dol, long len, const char *dolpath, const char *a1, const char *a2);
static void update_relaunch(void) {
    crumb("relance apres mise a jour : %s", up_target);
    if (bib_dirty) bib_save();
    if (stats_dirty) stats_save();
    resume_note(1);
    if (settings_dirty) save_settings();
    const char *e;
    if (up_mem) { e = gl_boot_mem(up_mem, up_mem_len, up_target, NULL, NULL); up_mem = NULL; }   /* (v117 : sans carte SD) */
    else e = gl_boot_dol(up_target, NULL, NULL, "GC Station");     /* (ne revient pas si ca marche) */
    if (e) crumb("relance apres mise a jour impossible : %s", e);
    if (e) { snprintf(up_msg, sizeof(up_msg), "%s", TR(e)); quit_requested = 1; }
}
/* v38 : bouton Reset de la console pendant GC-MP3 : relance GC-MP3 (sans passer par
   le menu de la GameCube / le lecteur de disque) */
static volatile int reset_pressed = 0;
#ifdef GEKKO
static void reset_cb(u32 irq, void *ctx) { (void)irq; (void)ctx; reset_pressed = 1; }
#endif
static void reset_init(void) {
#ifdef GEKKO
    SYS_SetResetCallback(reset_cb);
#endif
}
static void reset_pump(void) {
    if (!reset_pressed) return;
    reset_pressed = 0;
    if (up_state == UP_DL || up_state == UP_WRITE) return;      /* (pas pendant une mise a jour) */
    up_pick_target();
    crumb("bouton Reset : relance %s", up_target);
    ui_status("Red\xC3\xA9marrage de GC Station...");
    if (bib_dirty) bib_save();
    if (stats_dirty) stats_save();
    resume_note(1);
    if (settings_dirty) save_settings();
    const char *e = gl_boot_dol(up_target, NULL, NULL, "GC Station");
    if (e) lib_note(e);
}
static void update_activate(void) {
    if (up_state == UP_AVAIL) update_start_download();
    else if (up_state == UP_DONE) update_relaunch();
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
    case UP_WRITE: snprintf(out, cap, "Installation %ld %%", uw_len ? (long)((long long)uw_pos * 100 / uw_len) : 0L); break;
    case UP_IDLE: snprintf(out, cap, "Build %d  (A : v\xC3\xA9rifier)", GCMP3_BUILD); break;
    default: snprintf(out, cap, "%s", up_msg); break;
    }
}

static void draw_update_notice(void) {
    if (!up_notice_until || now_ms() >= up_notice_until || up_state != UP_AVAIL) return;
    char m[120];
    snprintf(m, sizeof(m), "Nouvelle version de GC Station (build %d) : START > Mise \xC3\xA0 jour", up_build);
    int w = text_width(F_SMALL, m, 0) + 30;
    gx_rrect_fill(320 - w / 2.0f, 92, (f32)w, 26, 9, RGBA(20, 70, 40, 240), RGBA(20, 70, 40, 240));
    gx_text_c(F_SMALL, 320, 110, m, RGBA(255, 255, 255, 255));
}

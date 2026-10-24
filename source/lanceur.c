// GC-MP3 v44 : lanceur de secours (inclus dans main.c, apres igr.c)
// Moteur GPL v2 (swiss-gc) modifie pour GC Station (tools/mkloader.py) : le jeu demarre tout
// de suite, aux couleurs de GC Station ; en cas d'erreur, retour sur GC Station. Il est compile
// par GitHub (gc-mp3-loader-bN.bin, release "dernier") : GC Station le telecharge dans
// SD:/gc-mp3/lanceur.dol et le tient a jour. v90 : ses reglages, sauvegardes et patches sont
// dans SD:/gc-mp3/lanceur, ses cheats dans SD:/gc-mp3/cheats. Resident (natif.c) est le
// lanceur principal (touche A) ; celui-ci sert de secours (touche X, ou cheats actives).
/* (LD_DOL, LD_VER : dans gclib.c) */

enum { LD_IDLE, LD_CHECK, LD_DL, LD_WRITE };            /* v93 : LD_WRITE = ecriture par morceaux */
static int ld_state = LD_IDLE, ld_auto_done = 0, ld_cache = -1, ld_new_b = 0, ld_loud = 0;
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
static int ld_ready(void) { return ld_build() > 0; }

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

static char ld_note[160];
static int ld_note_shown = 0;
static void ld_pump(void) {
    if (ld_note[0] && !ld_note_shown && now_ms() > 2500) {     /* v45 : pourquoi le jeu n'a pas demarre */
        ld_note_shown = 1;
        lib_note(ld_note);
        lib_note_until = now_ms() + 10000;
    }
    /* une fois par session : lanceur absent ou plus ancien que celui de GitHub */
    if (!ld_auto_done && sd_ok && net_phase == NET_DONE && net_ok && now_ms() > 25000 && ld_state == LD_IDLE
        && igr_state == IGR_IDLE && up_state != UP_DL && up_state != UP_WRITE && up_state != UP_CHECK && now_ms() - last_input_ms > 3000) {
        ld_auto_done = 1;
        ld_fetch(0);
    }
    if (ld_state == LD_IDLE) return;
    if (ld_state == LD_WRITE) {                            /* v93 : un morceau par image */
        static fw_t w;
        int r = w.f ? fw_step(&w) : (fw_open(&w, LD_DOL ".tmp", ld_hf.buf, ld_hf.len) ? 0 : -1);
        if (r == 0) return;
        int ok = r > 0;
        if (ok) { remove(LD_DOL); ok = rename(LD_DOL ".tmp", LD_DOL) == 0; }
        if (!ok) remove(LD_DOL ".tmp");
        ld_state = LD_IDLE;
        hf_drop(&ld_hf); ld_hf.st = HF_IDLE;
        if (!ok) { snprintf(ld_msg, sizeof(ld_msg), "%s", TR("\xC3\x89" "criture sur la carte SD impossible")); return; }
        FILE *v = fopen(LD_VER, "w");
        if (v) { fprintf(v, "%d\n", ld_new_b > 0 ? ld_new_b : 1); fclose(v); }
        ld_cache = -1;
        snprintf(ld_msg, sizeof(ld_msg), "%s", TR("Lanceur de secours pr\xC3\xAAt (touche X dans la fiche d'un jeu)"));
        crumb("lanceur : installe b%d", ld_new_b);
        return;
    }
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
    /* fichier recu : ecrit par morceaux (LD_WRITE) */
    if (!ld_dol_ok(ld_hf.buf, ld_hf.len)) {
        ld_state = LD_IDLE;
        hf_drop(&ld_hf); ld_hf.st = HF_IDLE;
        snprintf(ld_msg, sizeof(ld_msg), "%s", TR("Fichier re\xC3\xA7u invalide"));
        return;
    }
    mkdir(GCD, 0777);
    ld_state = LD_WRITE;
}

/* menu : A (ou Gauche / Droite) = telecharger / mettre a jour */
static void ld_change(int a) { (void)a; ld_fetch(1); }
static void ld_value(char *out, int cap) {
    if (ld_state != LD_IDLE) snprintf(out, cap, "%s", TR("T\xC3\xA9l\xC3\xA9" "chargement..."));
    else if (ld_build() > 0) snprintf(out, cap, "Build %d", ld_build());
    else snprintf(out, cap, "%s", TR("A : t\xC3\xA9l\xC3\xA9" "charger"));
}
static const char *ld_hint(void) {
    if (ld_msg[0]) return ld_msg;
    return "Secours : X dans la fiche d'un jeu, et les jeux avec cheats. A : mettre \xC3\xA0 jour";
}

/* v45 : au demarrage de GC-MP3 :
   - SD:/gc-mp3/retour.txt = chemin de GC-MP3 (le lanceur y revient apres A + Z + START en jeu) ;
   - SD:/gc-mp3/lanceur.log = journal du lanceur : s'il n'a pas pu lancer le jeu, on affiche pourquoi. */
static void ld_boot(void) {
    if (!sd_ok || sd_virtual) return;
    char me[NAME_LEN + 8];
    as_self(me, sizeof(me));
    long n = 0;
    u8 *d = read_file(GCD "/retour.txt", 1024, &n);
    int same = d && n >= (long)strlen(me) && !memcmp(d, me, strlen(me)) && (n == (long)strlen(me) || d[strlen(me)] == '\n' || d[strlen(me)] == '\r');
    free(d);
    if (!same) {
        mkdir(GCD, 0777);
        FILE *f = fopen(GCD "/retour.txt", "w");
        if (f) { fprintf(f, "%s\n", me); fclose(f); }
    }
    d = read_file(GCD "/lanceur.log", 64 * 1024, &n);
    if (!d) return;
    char *t = (char *)malloc(n + 1);
    if (t) {
        memcpy(t, d, n); t[n] = 0;
        const char *msg = NULL, *p = t;
        while ((p = strstr(p, "MSG: ")) != NULL) { msg = p + 5; p += 5; }
        if (msg) {
            int l = (int)strcspn(msg, "\r\n");
            char m[120];
            snprintf(m, sizeof(m), "%.*s", l > 110 ? 110 : l, msg);
            for (char *c = m; *c; c++) if ((unsigned char)*c < 32) *c = ' ';
            snprintf(ld_note, sizeof(ld_note), "%s %s", TR("Le lanceur n'a pas pu lancer le jeu :"), m);
        } else if (strstr(t, "jeu non lance") || strstr(t, "retour impossible")) {
            snprintf(ld_note, sizeof(ld_note), "%s", TR("Le lanceur n'a pas pu lancer le jeu (journal : SD:/gc-mp3/lanceur-dernier.log)"));
        }
        if (ld_note[0]) { snprintf(ld_msg, sizeof(ld_msg), "%s", ld_note); crumb("lanceur : %s", ld_note); }
        free(t);
    }
    free(d);
    remove(GCD "/lanceur-dernier.log");
    rename(GCD "/lanceur.log", GCD "/lanceur-dernier.log");
}

/* ======================= v90 : plus rien dans SD:/swiss =======================
   Une seule fois (SD:/gc-mp3/lanceur/migration.txt) : ce que GC Station utilisait dans SD:/swiss
   passe dans SD:/gc-mp3. Rien n'est efface dans SD:/swiss, sauf les jaquettes (deplacees : gros
   fichiers) et ipl-swiss.dol (renomme ipl-avant-gcstation.dol). */
#define SW_OLD SD_ROOT "/swiss"
#define MG_MARK LD_DIR "/migration.txt"
static int mg_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
/* copie par morceaux (la musique continue) ; skip = ligne a ne pas recopier (ex. "Autoload=") */
static int mg_copy(const char *src, const char *dst, const char *skip) {
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    char part[NAME_LEN + 8];
    snprintf(part, sizeof(part), "%s.part", dst);
    FILE *out = fopen(part, "wb");
    if (!out) { fclose(in); return 0; }
    int ok = 1;
    if (skip) {                                                /* (petit fichier texte, ligne par ligne) */
        char line[512];
        size_t sl = strlen(skip);
        while (ok && fgets(line, sizeof(line), in)) if (strncmp(line, skip, sl) && fputs(line, out) < 0) ok = 0;
    } else {
        static u8 b[65536];
        size_t n;
        while (ok && (n = fread(b, 1, sizeof(b), in)) > 0) { if (fwrite(b, 1, n, out) != n) ok = 0; service_audio(); }
    }
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (!ok) { remove(part); return 0; }
    remove(dst);
    if (rename(part, dst) != 0) { remove(part); return 0; }
    return 1;
}
/* v91 : la reprise se fait en fond, un fichier a la fois, depuis la boucle principale (v90 : tout au
   demarrage, avant l'ecran : la console restait figee). Le fichier migration.txt est ecrit avant de
   commencer : si quelque chose bloque, la reprise ne recommence pas au demarrage suivant.
   v96 : les gros fichiers (sauvegardes, jaquettes) sont copies par morceaux de 32 Ko, un par image
   (v91 : deplaces : Swiss, s'il sert encore, perdait ses jaquettes). */
#define MG_MAXJ 160
#define MG_BIG  (64 * 1024)                        /* (v96 : au-dela, copie par morceaux) */
enum { MG_COPYINI, MG_COPY, MG_MOVE };
typedef struct { char src[NAME_LEN], dst[NAME_LEN]; u8 mode, kind; } mg_job_t;
enum { MK_REG, MK_GAME, MK_CH, MK_SAV, MK_COV, MK_IGR, MK_IPL, MK_N };
static mg_job_t *mg_jobs = NULL;
static int mg_nj = 0, mg_pos = 0, mg_state = 0;              /* 0 = pas commence, 1 = en cours, 2 = fini */
static FILE *mg_in = NULL, *mg_out = NULL;                    /* v96 : gros fichier en cours de copie */
static int mg_cnt[MK_N];
static void mg_add(const char *src, const char *dst, int mode, int kind) {
    if (!mg_jobs || mg_nj >= MG_MAXJ || !mg_exists(src) || mg_exists(dst)) return;
    mg_job_t *j = &mg_jobs[mg_nj++];
    snprintf(j->src, sizeof(j->src), "%s", src);
    snprintf(j->dst, sizeof(j->dst), "%s", dst);
    j->mode = (u8)mode; j->kind = (u8)kind;
}
static void mg_add_dir(const char *from, const char *to, int kind) {
    DIR *d = opendir(from);
    if (!d) return;
    struct dirent *e;
    char a[NAME_LEN], b[NAME_LEN];
    while ((e = readdir(d)) != NULL && mg_nj < MG_MAXJ) {
        if (e->d_name[0] == '.') continue;
        snprintf(a, sizeof(a), "%s/%s", from, e->d_name);
        snprintf(b, sizeof(b), "%s/%s", to, e->d_name);
        mg_add(a, b, MG_COPY, kind);
    }
    closedir(d);
}
static void mg_mark(int done) {
    FILE *f = fopen(MG_MARK, "w");
    if (!f) return;
    fprintf(f, "# GC Station : fichiers repris de SD:/swiss (une seule fois ; effacer ce fichier pour recommencer)\n"
               "etat = %s\nfichiers = %d / %d\n"
               "reglages_lanceur = %d\nreglages_jeux = %d\ncheats = %d\nsauvegardes = %d\njaquettes = %d\nretour_en_jeu = %d\nipl = %d\n",
            done ? "fini" : "en cours", mg_pos, mg_nj,
            mg_cnt[MK_REG], mg_cnt[MK_GAME], mg_cnt[MK_CH], mg_cnt[MK_SAV], mg_cnt[MK_COV], mg_cnt[MK_IGR], mg_cnt[MK_IPL]);
    fclose(f);
}
static void mg_start(void) {
    mg_state = 2;
    if (!sd_ok || sd_virtual || mg_exists(MG_MARK)) return;
    mkdir(GCD, 0777); mkdir(LD_DIR, 0777);
    memset(mg_cnt, 0, sizeof(mg_cnt));
    mg_nj = mg_pos = 0;
    mg_mark(0);                                               /* (d'abord : jamais deux fois) */
    if (!mg_exists(MG_MARK)) return;                          /* (carte en lecture seule) */
    mkdir(LD_DIR "/settings", 0777); mkdir(LD_DIR "/settings/game", 0777);
    mkdir(LD_DIR "/patches", 0777); mkdir(LD_DIR "/saves", 0777); mkdir(CH_DIR, 0777);
    mg_jobs = (mg_job_t *)calloc(MG_MAXJ, sizeof(mg_job_t));
    if (mg_jobs && mg_exists(SW_OLD)) {
        mg_add(SW_OLD "/settings/global.ini", LD_INI, MG_COPYINI, MK_REG);
        mg_add(SW_OLD "/settings/recent.ini", LD_DIR "/settings/recent.ini", MG_COPY, MK_REG);
        if (igr_build_of(IGR_OLD) > 0) mg_add(IGR_OLD, IGR_IMG, MG_COPY, MK_IGR);
        mg_add_dir(SW_OLD "/cheats", CH_DIR, MK_CH);
        mg_add_dir(SW_OLD "/settings/game", LD_DIR "/settings/game", MK_GAME);
        mg_add_dir(SW_OLD "/saves", LD_DIR "/saves", MK_SAV);
        mkdir(GL_UI, 0777);
        mg_add(SW_OLD "/ui/posters.pak", GL_UI "/posters.pak", MG_COPY, MK_COV);   /* v96 : copies (Swiss garde les siens) */
        mg_add(SW_OLD "/ui/stills.pak", GL_UI "/stills.pak", MG_COPY, MK_COV);
        mg_add(SW_OLD "/ui/descriptions.txt", GL_UI "/descriptions.txt", MG_COPY, MK_COV);
    }
    if (mg_jobs) mg_add(IPL_OLD_V89, IPL_OLD, MG_MOVE, MK_IPL);
    crumb("reprise de SD:/swiss : %d fichiers", mg_nj);
    if (mg_nj) mg_state = 1; else { free(mg_jobs); mg_jobs = NULL; mg_mark(1); }
}
/* appele a chaque image (boucle principale) : un fichier a la fois */
static void mg_pump(void) {
    if (mg_state == 2) return;
    if (mg_state == 0) { if (now_ms() > 6000) mg_start(); return; }
    if (mg_pos < mg_nj) {
        mg_job_t *j = &mg_jobs[mg_pos];
        struct stat st;
        int mode = j->mode;
        if (mode == MG_COPY && (mg_in || (stat(j->src, &st) == 0 && st.st_size > MG_BIG))) {   /* v96 : gros fichier : copie par morceaux (plus jamais deplace) */
            if (!mg_in) {
                char part[NAME_LEN + 8];
                snprintf(part, sizeof(part), "%s.part", j->dst);
                mg_in = fopen(j->src, "rb");
                mg_out = mg_in && !mg_exists(j->dst) ? fopen(part, "wb") : NULL;
                if (!mg_out) { if (mg_in) fclose(mg_in); mg_in = NULL; mg_pos++; return; }
                crumb("reprise : %s (copie par morceaux)", j->src);
                return;
            }
            static u8 b[4096];                                /* (4 Ko par image, rien pendant qu'on bouge : l'ecran reste fluide) */
            if (now_ms() - last_input_ms < 400) return;
            size_t n = fread(b, 1, sizeof(b), mg_in);
            int ok = !n || fwrite(b, 1, n, mg_out) == n;
            if (ok && n) return;                             /* (suite a l'image suivante) */
            fclose(mg_in); mg_in = NULL;
            ok = fclose(mg_out) == 0 && ok;
            mg_out = NULL;
            char part[NAME_LEN + 8];
            snprintf(part, sizeof(part), "%s.part", j->dst);
            if (ok && rename(part, j->dst) == 0) mg_cnt[j->kind]++; else remove(part);
            mg_pos++;
            if (mg_pos % 8 == 0) mg_mark(0);
            return;
        }
        crumb("reprise : %s (%s)", j->src, mode == MG_MOVE ? "deplace" : "copie");
        int ok = 0;
        if (!mg_exists(j->dst)) {
            if (mode == MG_MOVE) ok = rename(j->src, j->dst) == 0;
            if (!ok && (mode != MG_MOVE || (stat(j->src, &st) == 0 && st.st_size <= MG_BIG))) ok = mg_copy(j->src, j->dst, mode == MG_COPYINI ? "Autoload=" : NULL);
        }
        if (ok) { mg_cnt[j->kind]++; if (j->kind == MK_IGR) igr_cache = -1; }
        mg_pos++;
        if (mg_pos % 8 == 0) mg_mark(0);
        return;
    }
    mg_mark(1);
    free(mg_jobs); mg_jobs = NULL;
    mg_state = 2;
    int tot = 0;
    for (int k = 0; k < MK_N; k++) tot += mg_cnt[k];
    crumb("reprise de SD:/swiss finie : %d fichiers", tot);
    if (tot) lib_note("Fichiers de Swiss repris dans SD:/gc-mp3 : GC Station n'utilise plus SD:/swiss");
}

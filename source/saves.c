// GC Station v141 : sauvegardes des jeux. Inclus dans main.c (apres extras73.c).
//  - apres chaque partie avec Pulse et une carte memoire virtuelle : les sauvegardes du jeu sont copiees
//    (.gci) dans SD:/gc-mp3/sauvegardes/<ID>/<date-heure>/ (10 dernieres gardees ; rien si rien n'a change) ;
//  - Outils > « Historique des sauvegardes » : par jeu, chaque copie (date, nombre de fichiers), aussi celles
//    des autres GC Station du Salon : remettre une copie sur la carte virtuelle du jeu ou sur une vraie carte
//    (= reprendre sa partie sur cette console) ;
//  - « Tout sauvegarder maintenant » : une copie de chaque jeu de toutes les cartes virtuelles.

#define SV_DIR  GCD "/sauvegardes"
#define SV_KEEP 10
#define SV_MAXF 16                                  /* sauvegardes d'un meme jeu dans une copie */
typedef struct { char name[80]; u8 *g; long len; } sv_file;

static int sv_open_card(const char *name) {         /* carte virtuelle -> vc_cur, chargee */
    vc_scan();
    for (int i = 0; i < vc_nf; i++) if (!strcasecmp(vc_files[i], name)) { vc_cur = i; return vc_load(); }
    return 0;
}
static int sv_cmp_desc(const void *a, const void *b) { return -strcmp((const char *)a, (const char *)b); }
/* copies d'un jeu (noms de dossiers "AAAAMMJJ-HHMMSS"), de la plus recente a la plus ancienne */
static int sv_stamps(const char *root, const char *id, char (*out)[20], int cap) {
    static char all[96][20];
    char p[NAME_LEN];
    snprintf(p, sizeof(p), "%s/%.6s", root, id);
    DIR *d = opendir(p);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 96) {
        if (strlen(e->d_name) != 15 || e->d_name[8] != '-') continue;
        snprintf(all[n++], 20, "%s", e->d_name);
    }
    closedir(d);
    qsort(all, n, 20, sv_cmp_desc);                   /* (toutes lues, puis les plus recentes gardees) */
    if (n > cap) n = cap;
    memcpy(out, all, (size_t)n * 20);
    return n;
}
static void sv_rmdir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char p[NAME_LEN];
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        remove(p);
    }
    closedir(d);
    rmdir(dir);
}
static u8 *sv_read(const char *path, long *len) { return (u8 *)read_file(path, 8L * 1024 * 1024 + 64, len); }
/* la copie la plus recente contient-elle exactement ces fichiers ? */
static int sv_same(const char *id, sv_file *f, int n) {
    char st[1][20];
    if (!sv_stamps(SV_DIR, id, st, 1)) return 0;
    char dir[NAME_LEN], p[NAME_LEN];
    snprintf(dir, sizeof(dir), "%s/%.6s/%s", SV_DIR, id, st[0]);
    int cnt = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) if (e->d_name[0] != '.') cnt++;
    closedir(d);
    if (cnt != n) return 0;
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof(p), "%s/%s.gci", dir, f[i].name);
        long l = 0;
        u8 *b = sv_read(p, &l);
        int same = b && l == f[i].len && !memcmp(b, f[i].g, l);
                free(b);
        if (!same) return 0;
    }
    return 1;
}
/* copie des sauvegardes du jeu id (4 1ers caracteres + editeur) de la carte virtuelle card.
   Retour : nombre de fichiers copies, 0 = rien de nouveau, -1 = erreur */
static int sv_backup(const char *id, const char *card) {
    if (!sd_ok || !id || strlen(id) < 6 || !sv_open_card(card)) return -1;
    static sv_file f[SV_MAXF];
    int n = 0;
    for (int i = 0; i < vc_n && n < SV_MAXF; i++) {
        if (memcmp(vc_e[i].de, id, 4) || memcmp(vc_e[i].de + 4, id + 4, 2)) continue;
        long l = 0;
        u8 *g = vc_export(i, &l);
        if (!g) continue;
        mc_safe_name(vc_e[i].de, f[n].name, sizeof(f[n].name));
        for (int k = 0; k < n; k++) if (!strcmp(f[k].name, f[n].name)) { size_t q = strlen(f[n].name); snprintf(f[n].name + q, sizeof(f[n].name) - q, "_%d", n); }
        f[n].g = g; f[n].len = l; n++;
    }
    int r = 0;
    if (n && !sv_same(id, f, n)) {
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        char dir[NAME_LEN], p[NAME_LEN];
        mkdir(SV_DIR, 0777);
        snprintf(dir, sizeof(dir), "%s/%.6s", SV_DIR, id);
        mkdir(dir, 0777);
        snprintf(dir + strlen(dir), sizeof(dir) - strlen(dir), "/%04d%02d%02d-%02d%02d%02d", tm ? tm->tm_year + 1900 : 2000, tm ? tm->tm_mon + 1 : 1, tm ? tm->tm_mday : 1,
                 tm ? tm->tm_hour : 0, tm ? tm->tm_min : 0, tm ? tm->tm_sec : 0);
        mkdir(dir, 0777);
        r = n;
        for (int i = 0; i < n; i++) {
            snprintf(p, sizeof(p), "%s/%s.gci", dir, f[i].name);
            FILE *o = fopen(p, "wb");
            int ok = o && fwrite(f[i].g, 1, f[i].len, o) == (size_t)f[i].len;
            if (o && fclose(o) != 0) ok = 0;
            if (!ok) r = -1;
            service_audio();
        }
        static char st[64][20];                           /* (les plus anciennes au-dela de 10 : effacees) */
        int ns = sv_stamps(SV_DIR, id, st, 64);
        for (int i = SV_KEEP; i < ns; i++) { snprintf(p, sizeof(p), "%s/%.6s/%s", SV_DIR, id, st[i]); sv_rmdir(p); }
        crumb("sauvegardes %s : %d fichier(s) copies (%s)", id, n, card);
    }
    for (int i = 0; i < n; i++) free(f[i].g);
    return r;
}
/* (natif.c) apres une partie : la carte utilisee est dans le journal du lancement */
static void sv_after_game(const char *id, const char *launch_log) {
    static const char *const key[2] = { "Carte memoire virtuelle (slot A) : active (gc-mp3/resident/", "Carte memoire virtuelle (slot B) : active (gc-mp3/resident/" };
    for (int s = 0; s < 2; s++) {
        const char *q = strstr(launch_log, key[s]);
        if (!q) continue;
        q += strlen(key[s]);
        char name[64];
        int n = 0;
        while (q[n] && q[n] != ',' && q[n] != '\n' && n < 63) { name[n] = q[n]; n++; }
        name[n] = 0;
        if (n > 4) sv_backup(id, name);
    }
}

/* ---------- ecran : Outils > Historique des sauvegardes ---------- */
#define SV_GMAX 200
#define SV_SMAX 48
typedef struct { char st[20]; int peer; int nfiles; } sv_snap;     /* peer : -1 = cette console */
static char (*sv_games)[8] = NULL;
static int sv_ng = 0, sv_gsel = 0, sv_lv = 0, sv_ssel = 0, sv_ns = 0, sv_pop = 0, sv_psel = 0;
static sv_snap sv_snaps[SV_SMAX];
static char sv_gid[8];
static int sv_gcmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static void sv_scan_games(void) {
    if (!sv_games) sv_games = malloc(sizeof(*sv_games) * SV_GMAX);
    sv_ng = 0;
    if (!sv_games) return;
    DIR *d = opendir(SV_DIR);
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL && sv_ng < SV_GMAX) if (strlen(e->d_name) == 6 && e->d_name[0] != '.') snprintf(sv_games[sv_ng++], 8, "%s", e->d_name);
    if (d) closedir(d);
    qsort(sv_games, sv_ng, 8, sv_gcmp);
}
static int sv_count(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) { const char *x = strrchr(e->d_name, '.'); if (x && !strcasecmp(x, ".gci")) n++; }
    closedir(d);
    return n;
}
static void sv_peer_host(int k) {                    /* les fichiers d'une autre console : gcs:/ (netfs.c) */
    snprintf(nfs_cfg[NFS_GCS].host, sizeof(nfs_cfg[NFS_GCS].host), "%s", salon_peers[k].ip);
    nfs_cfg[NFS_GCS].port = salon_peers[k].port;
}
static void sv_scan_snaps(void) {
    sv_ns = 0;
    char st[SV_SMAX][20], p[NAME_LEN];
    int n = sv_stamps(SV_DIR, sv_gid, st, SV_SMAX);
    for (int i = 0; i < n && sv_ns < SV_SMAX; i++) {
        sv_snap *s = &sv_snaps[sv_ns++];
        snprintf(s->st, sizeof(s->st), "%s", st[i]); s->peer = -1;
        snprintf(p, sizeof(p), "%s/%s/%s", SV_DIR, sv_gid, st[i]);
        s->nfiles = sv_count(p);
    }
    if (salon_on && salon_n) {                       /* les copies des autres consoles du Salon */
        tl_status("Copies des autres consoles...");
        for (int k = 0; k < salon_n && sv_ns < SV_SMAX; k++) {
            sv_peer_host(k);
            int m = sv_stamps("gcs:/gc-mp3/sauvegardes", sv_gid, st, 4);
            for (int i = 0; i < m && sv_ns < SV_SMAX; i++) {
                sv_snap *s = &sv_snaps[sv_ns++];
                snprintf(s->st, sizeof(s->st), "%s", st[i]); s->peer = k; s->nfiles = -1;
            }
        }
        gl_busy[0] = 0;
    }
    if (sv_ssel >= sv_ns) sv_ssel = sv_ns ? sv_ns - 1 : 0;
}
static const char *sv_title(const char *id) {
    for (int k = 0; k < gl_n; k++) if (!strncmp(gl[k].id, id, 6)) return gl[k].title;
    return id;
}
static void sv_when(const char *st, char *out, int cap) {
    snprintf(out, cap, "%.2s/%.2s/%.4s %.2s:%.2s", st + 6, st + 4, st, st + 9, st + 11);
}
/* remet une copie : sur la carte virtuelle du jeu (dest -1) ou une vraie carte (slot 0 / 1) */
static void sv_restore(const sv_snap *s, int dest) {
    char dir[NAME_LEN], p[NAME_LEN];
    if (s->peer >= 0) { if (s->peer >= salon_n) { tl_note("Console partie du Salon"); return; } sv_peer_host(s->peer); snprintf(dir, sizeof(dir), "gcs:/gc-mp3/sauvegardes/%s/%s", sv_gid, s->st); }
    else snprintf(dir, sizeof(dir), "%s/%s/%s", SV_DIR, sv_gid, s->st);
    tl_status("Lecture de la copie...");
    static sv_file f[SV_MAXF];
    int n = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL && n < SV_MAXF) {
        const char *x = strrchr(e->d_name, '.');
        if (!x || strcasecmp(x, ".gci")) continue;
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        long l = 0;
        u8 *g = sv_read(p, &l);
        if (!g || l < 64 + 8192 || (l - 64) % 8192) { free(g); continue; }
        f[n].g = g; f[n].len = l; n++;
    }
    if (d) closedir(d);
    if (!n) { gl_busy[0] = 0; tl_note("Copie vide ou illisible"); return; }
    const char *err = NULL;
    tl_status("Copie sur la carte...");
    if (dest < 0) {
        char id[8]; snprintf(id, sizeof(id), "%s", sv_gid);
        const char *card = na_card_name(0, id);
        if (!sv_open_card(card)) err = vc_blank ? "Carte virtuelle du jeu pas encore format\xC3\xA9" "e" : "Carte virtuelle du jeu pas encore cr\xC3\xA9\xC3\xA9" "e (lance le jeu une fois)";
        for (int i = 0; i < n && !err; i++) {
            for (int j = vc_n - 1; j >= 0; j--) if (!memcmp(vc_e[j].de, f[i].g, 6) && !memcmp(vc_e[j].de + 8, f[i].g + 8, 32)) { err = vc_delete(j); break; }
            if (!err) err = vc_import(f[i].g, f[i].len);
        }
    } else {
        if (!mc_mount(dest)) err = mc_errtxt(mc_err);
        for (int i = 0; i < n && !err; i++) {
            for (int j = 0; j < mc_n; j++) if (!memcmp(mc[j].de, f[i].g, 6) && !memcmp(mc[j].de + 8, f[i].g + 8, 32)) {
                int r = CARD_DeleteEntry(dest, &mc[j].dir);
                if (r < 0) err = mc_errtxt(r);
                mc_mount(dest);
                break;
            }
            if (!err) { int r = mc_import(dest, f[i].g, f[i].len); if (r < 0) err = mc_errtxt(r); }
        }
        mc_unmount();
    }
    for (int i = 0; i < n; i++) free(f[i].g);
    gl_busy[0] = 0;
    if (err) { tl_note(err); return; }
    char m[160];
    snprintf(m, sizeof(m), "%d sauvegarde%s remise%s %s", n, n > 1 ? "s" : "", n > 1 ? "s" : "", dest < 0 ? "sur la carte virtuelle du jeu" : dest ? "sur la carte du slot B" : "sur la carte du slot A");
    snprintf(tl_msg, sizeof(tl_msg), "%s", m); tl_msg_t = now_ms();
}
static void sv_backup_all(void) {                    /* une copie de chaque jeu de toutes les cartes virtuelles */
    tl_status("Copie des sauvegardes...");
    vc_scan();
    static char cards[VC_MAXF][64];
    int nc = vc_nf, total = 0, games = 0;
    for (int i = 0; i < nc; i++) snprintf(cards[i], sizeof(cards[0]), "%s", vc_files[i]);
    for (int c = 0; c < nc; c++) {
        if (!sv_open_card(cards[c])) continue;
        static char ids[127][8];
        int ni = 0;
        for (int i = 0; i < vc_n; i++) {
            char id[8]; memcpy(id, vc_e[i].de, 6); id[6] = 0;
            int seen = 0;
            for (int k = 0; k < ni; k++) if (!strcmp(ids[k], id)) seen = 1;
            if (!seen && ni < 127) snprintf(ids[ni++], 8, "%s", id);
        }
        for (int k = 0; k < ni; k++) { int r = sv_backup(ids[k], cards[c]); if (r > 0) { total += r; games++; } if (!sv_open_card(cards[c])) break; }
    }
    gl_busy[0] = 0;
    sv_scan_games();
    char m[120];
    if (total) snprintf(m, sizeof(m), "%d sauvegarde%s copi\xC3\xA9" "e%s (%d jeu%s)", total, total > 1 ? "s" : "", total > 1 ? "s" : "", games, games > 1 ? "x" : "");
    else snprintf(m, sizeof(m), "Rien de nouveau \xC3\xA0 copier");
    snprintf(tl_msg, sizeof(tl_msg), "%s", m); tl_msg_t = now_ms();
}
static void sv_open(void) { tl_screen = TL_SAVES; sv_lv = 0; sv_pop = 0; sv_gsel = 0; sv_scan_games(); }
enum { SVP_VC, SVP_A, SVP_B, SVP_N };
static const char *const svp_names[SVP_N] = { "Remettre sur la carte virtuelle du jeu (Pulse)", "Remettre sur la vraie carte (slot A)", "Remettre sur la vraie carte (slot B)" };
static void sv_input(u32 down, u32 nav, int fire) {
    if (sv_pop) {
        if (fire && (nav & PAD_BUTTON_DOWN)) sv_psel = (sv_psel + 1) % SVP_N;
        if (fire && (nav & PAD_BUTTON_UP)) sv_psel = (sv_psel + SVP_N - 1) % SVP_N;
        if (down & PAD_BUTTON_A) { sv_pop = 0; if (sv_ssel < sv_ns) sv_restore(&sv_snaps[sv_ssel], sv_psel == SVP_VC ? -1 : sv_psel - SVP_A); }
        if (down & PAD_BUTTON_B) sv_pop = 0;
        return;
    }
    if (sv_lv == 1) {
        if (fire && (nav & PAD_BUTTON_DOWN) && sv_ssel < sv_ns - 1) sv_ssel++;
        if (fire && (nav & PAD_BUTTON_UP) && sv_ssel > 0) sv_ssel--;
        if ((down & PAD_BUTTON_A) && sv_ns) { sv_pop = 1; sv_psel = 0; }
        if (down & PAD_BUTTON_B) sv_lv = 0;
        return;
    }
    int rows = sv_ng + 1;                            /* (ligne 0 : tout sauvegarder maintenant) */
    if (fire && (nav & PAD_BUTTON_DOWN) && sv_gsel < rows - 1) sv_gsel++;
    if (fire && (nav & PAD_BUTTON_UP) && sv_gsel > 0) sv_gsel--;
    if (fire && (nav & PAD_BUTTON_RIGHT)) { sv_gsel += 8; if (sv_gsel > rows - 1) sv_gsel = rows - 1; }
    if (fire && (nav & PAD_BUTTON_LEFT)) { sv_gsel -= 8; if (sv_gsel < 0) sv_gsel = 0; }
    if (down & PAD_BUTTON_A) {
        if (sv_gsel == 0) sv_backup_all();
        else { snprintf(sv_gid, sizeof(sv_gid), "%s", sv_games[sv_gsel - 1]); sv_lv = 1; sv_ssel = 0; sv_scan_snaps(); }
    }
    if (down & PAD_BUTTON_B) tl_screen = TL_NONE;
}
static void draw_saves(void) {
    char buf[200], fv[200];
    tl_frame("Historique des sauvegardes", NULL);
    const int vis = 10;
    const f32 rh = 28;
    if (sv_lv == 0) {
        int rows = sv_ng + 1, start = sv_gsel - vis / 2;
        if (start > rows - vis) start = rows - vis;
        if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < rows; i++) {
            int r = start + i; f32 ry = 90 + i * rh;
            int on = r == sv_gsel;
            gp_zone(52, ry, 536, rh - 3, &sv_gsel, r, GPZ_SEL);
            if (on) gx_rrect_fill(52, ry, 536, rh - 3, 6, TL_SEL1, TL_SEL2);
            if (r == 0) { gx_text1(F_MED, 64, ry + 17, "Tout sauvegarder maintenant (toutes les cartes virtuelles)", on ? RGBA(255, 255, 255, 255) : RGBA(130, 230, 160, 255)); continue; }
            const char *id = sv_games[r - 1];
            text_display(sv_title(id), buf, sizeof(buf)); fit_text_sel(on, F_MED, buf, 380, fv, sizeof(fv));
            gx_text1(F_MED, 64, ry + 17, fv, on ? RGBA(255, 255, 255, 255) : RGBA(230, 222, 255, 255));
            char st[1][20], p[NAME_LEN];
            snprintf(p, sizeof(p), "%s", SV_DIR);
            if (sv_stamps(p, id, st, 1)) { sv_when(st[0], buf, sizeof(buf)); gx_text_r(F_SMALL, 578, ry + 17, buf, RGBA(150, 135, 210, 255)); }
        }
        if (!sv_ng) gx_text1(F_SMALL, 64, 132, "Aucune copie pour l'instant : elles se font toutes seules apr\xC3\xA8s chaque partie (Pulse).", RGBA(170, 155, 225, 255));
    } else {
        text_display(sv_title(sv_gid), buf, sizeof(buf)); fit_text(F_MED, buf, 520, fv, sizeof(fv));
        gx_text1(F_MED, 58, 100, fv, RGBA(255, 210, 120, 255));
        int start = sv_ssel - (vis - 1) / 2;
        if (start > sv_ns - (vis - 1)) start = sv_ns - (vis - 1);
        if (start < 0) start = 0;
        for (int i = 0; i < vis - 1 && start + i < sv_ns; i++) {
            int r = start + i; f32 ry = 116 + i * rh;
            int on = r == sv_ssel;
            sv_snap *s = &sv_snaps[r];
            gp_zone(52, ry, 536, rh - 3, &sv_ssel, r, GPZ_SEL);
            if (on) gx_rrect_fill(52, ry, 536, rh - 3, 6, TL_SEL1, TL_SEL2);
            sv_when(s->st, buf, sizeof(buf));
            gx_text1(F_MED, 64, ry + 17, buf, on ? RGBA(255, 255, 255, 255) : RGBA(230, 222, 255, 255));
            if (s->peer >= 0 && s->peer < salon_n) snprintf(buf, sizeof(buf), "GC Station : %s", salon_peers[s->peer].name);
            else snprintf(buf, sizeof(buf), "cette console%s%d fichier%s", s->nfiles >= 0 ? " - " : "", s->nfiles, s->nfiles > 1 ? "s" : "");
            fit_text(F_SMALL, buf, 300, fv, sizeof(fv));
            gx_text_r(F_SMALL, 578, ry + 17, fv, s->peer >= 0 ? RGBA(120, 200, 255, 255) : RGBA(150, 135, 210, 255));
        }
        if (!sv_ns) gx_text1(F_SMALL, 64, 140, "Aucune copie.", RGBA(170, 155, 225, 255));
        if (sv_pop) {
            const f32 w = 420, h = 40 + SVP_N * 26, x = 110, y = 170;
            gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
            draw_panel(x, y, w, h);
            for (int a = 0; a < SVP_N; a++) {
                f32 ry = y + 14 + a * 26;
                if (a == sv_psel) gx_rrect_fill(x + 10, ry, w - 20, 24, 5, TL_SEL1, TL_SEL2);
                gx_text1(F_MED, x + 22, ry + 17, svp_names[a], RGBA(240, 232, 255, 255));
            }
        }
    }
    tl_msgbox();
    static const char *const k[] = { "A", "B" }, *const l0[] = { "OUVRIR", "RETOUR" }, *const l1[] = { "REMETTRE", "RETOUR" };
    tl_legend(k, sv_lv ? l1 : l0, 2);
}

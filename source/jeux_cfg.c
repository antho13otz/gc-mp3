// GC Station v75 : reglages des jeux (inclus dans main.c, apres gclib.c). Remplace indigo_cfg.c :
// les pages de reglages d'Indigo / Swiss ne sont plus dans GC Station.
//  - Reglages de GC Station qui comptent aussi pour les jeux : ils sont recopies, sans rien montrer,
//    dans SD:/gc-mp3/lanceur/settings/global.ini, le fichier que lit le lanceur de secours :
//    vibrations, mode video, langue et son de la console, position de l'image, retour en jeu.
//    Les autres lignes du fichier ne changent pas. (v90 : plus rien dans SD:/swiss.)
//  - v90 : au premier demarrage, les fichiers de SD:/swiss utiles a GC Station sont repris une fois
//    dans SD:/gc-mp3 (mg_pump, lanceur.c).
//  - Reglages d'un jeu (Y dans la fiche d'un jeu) : langue, son, carte memoire virtuelle (Resident).

static char vc_active[64];                                    /* (natif.c / extras73.c) */
static char vc_files[16][64];
static int vc_nf;
static void vc_scan(void);
typedef struct { const char *key; } ic_item;
static const ic_item ic_items[] = {
    { "Disable PAD Rumble" }, { "Apps Video Mode" }, { "ShowHiddenFiles" }, { "Hide Unknown file types" },
    { "BBAUseDHCP" }, { "BBALocalIP" }, { "BBANetmask" }, { "BBAGateway" },
    { "Screen Position" }, { "System Sound" }, { "System Language" }, { "IGRType" } };
#define IC_N ((int)(sizeof(ic_items) / sizeof(ic_items[0])))
#define IC_VLEN 96
static int ic_on = 0, ic_dirty = 0, ic_menu_loaded = 0;
static char ic_val[IC_N][IC_VLEN];
static unsigned char ic_has[IC_N], ic_chg[IC_N];
static int ic_file_ok = 0;
static char ic_path[NAME_LEN] = LD_INI;
static void ic_use_global(void) { snprintf(ic_path, sizeof(ic_path), "%s", LD_INI); }
static int ic_find(const char *k) { for (int i = 0; i < IC_N; i++) if (!strcmp(ic_items[i].key, k)) return i; return -1; }
static const char *ic_get(const char *k) { int i = ic_find(k); return i >= 0 && ic_has[i] ? ic_val[i] : NULL; }

static void ic_load(void) {
    memset(ic_has, 0, sizeof(ic_has)); memset(ic_chg, 0, sizeof(ic_chg));
    for (int i = 0; i < IC_N; i++) ic_val[i][0] = 0;
    ic_dirty = 0; ic_file_ok = 0;
    long len = 0;
    unsigned char *d = read_file(ic_path, 256 * 1024, &len);
    if (!d) return;
    ic_file_ok = 1;
    long i = 0;
    while (i < len) {
        long s = i;
        while (i < len && d[i] != '\n' && d[i] != '\r') i++;
        long n = i - s;
        while (i < len && (d[i] == '\n' || d[i] == '\r')) i++;
        if (n <= 0 || d[s] == '#') continue;
        const unsigned char *eq = memchr(d + s, '=', n);
        if (!eq) continue;
        int kl = (int)(eq - (d + s));
        for (int k = 0; k < IC_N; k++) {
            if ((int)strlen(ic_items[k].key) != kl || memcmp(ic_items[k].key, d + s, kl)) continue;
            int vl = (int)(n - kl - 1);
            if (vl > IC_VLEN - 1) vl = IC_VLEN - 1;
            memcpy(ic_val[k], eq + 1, vl); ic_val[k][vl] = 0;
            ic_has[k] = 1;                 /* (la derniere ligne gagne, comme dans le lanceur) */
        }
    }
    free(d);
}

/* ecrit global.ini : remplace les lignes modifiees, ajoute les nouvelles avant la fin des reglages */
static int ic_save(void) {
    long len = 0;
    unsigned char *d = read_file(ic_path, 256 * 1024, &len);
    int crlf = 1;
    if (d) { crlf = 0; for (long i = 0; i < len; i++) if (d[i] == '\n') { crlf = i > 0 && d[i - 1] == '\r'; break; } }
    const char *nl = crlf ? "\r\n" : "\n";
    mkdir(GCD, 0777); mkdir(LD_DIR, 0777); mkdir(LD_DIR "/settings", 0777);
    char tmp[NAME_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", ic_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(d); return 0; }
    unsigned char done[IC_N];
    memset(done, 0, sizeof(done));
    int ok = 1;
    long i = 0;
    while (d && i < len && ok) {
        long s = i;
        while (i < len && d[i] != '\n' && d[i] != '\r') i++;
        long n = i - s;
        long e = i;
        while (i < len && (d[i] == '\n' || d[i] == '\r') && i - e < 2) { i++; if (d[i - 1] == '\n') break; }
        int rep = -1;
        if (n > 0 && d[s] != '#') {
            const unsigned char *eq = memchr(d + s, '=', n);
            if (eq) {
                int kl = (int)(eq - (d + s));
                for (int k = 0; k < IC_N; k++)
                    if (ic_chg[k] && (int)strlen(ic_items[k].key) == kl && !memcmp(ic_items[k].key, d + s, kl)) { rep = k; break; }
            }
        }
        if (n >= 23 && !memcmp(d + s, "#!!Swiss Settings End!!", 23)) {   /* (fin des reglages, format du lanceur) */
            for (int k = 0; k < IC_N && ok; k++)
                if (ic_chg[k] && !done[k]) { if (fprintf(f, "%s=%s%s", ic_items[k].key, ic_val[k], nl) < 0) ok = 0; done[k] = 1; }
        }
        if (rep >= 0) {
            if (!done[rep]) { if (fprintf(f, "%s=%s", ic_items[rep].key, ic_val[rep]) < 0) ok = 0; done[rep] = 1; }
            else continue;                                    /* (doublon : on ne garde que la premiere) */
        } else if (n > 0 && fwrite(d + s, 1, n, f) != (size_t)n) ok = 0;
        if (i > e && fwrite(d + e, 1, i - e, f) != (size_t)(i - e)) ok = 0;
    }
    if (d && len > 0 && d[len - 1] != '\n' && d[len - 1] != '\r') {
        int more = 0; for (int k = 0; k < IC_N; k++) if (ic_chg[k] && !done[k]) more = 1;
        if (more && fputs(nl, f) < 0) ok = 0;
    }
    for (int k = 0; k < IC_N && ok; k++)
        if (ic_chg[k] && !done[k]) { if (fprintf(f, "%s=%s%s", ic_items[k].key, ic_val[k], nl) < 0) ok = 0; done[k] = 1; }
    free(d);
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); return 0; }
    remove(ic_path);
    if (rename(tmp, ic_path) != 0) return 0;
    memset(ic_chg, 0, sizeof(ic_chg));
    ic_dirty = 0; ic_file_ok = 1;
    return 1;
}
static void ic_set(int k, const char *v) {
    if (k < 0 || k >= IC_N) return;
    if (strcmp(ic_val[k], v) || !ic_has[k]) {
        snprintf(ic_val[k], IC_VLEN, "%s", v);
        ic_has[k] = 1; ic_chg[k] = 1; ic_dirty = 1;
    }
}
/* un reglage de GC Station change : meme valeur dans global.ini (force = meme si la ligne n'y est pas) */
static void ic_put2(const char *key, const char *val, int force) {
    if (!sd_ok || sd_virtual) return;
    ic_use_global();
    ic_load();
    int k = ic_find(key);
    if (k < 0 || (!force && (!ic_file_ok || !ic_has[k])) || (ic_has[k] && !strcmp(ic_val[k], val))) return;
    ic_set(k, val);
    ic_save();
}
static void ic_put(const char *key, const char *val) { ic_put2(key, val, 0); }

/* ---------- langue et son de la console (SRAM : les jeux les lisent) ---------- */
static const char *const gc_lang_ini[6] = { "English", "German", "French", "Spanish", "Italian", "Dutch" };
static const char *const gc_lang_keys[6] = { "en", "de", "fr", "es", "it", "nl" };
static const char *const gc_lang_names[6] = { "English", "Deutsch", "Fran\xC3\xA7" "ais", "Espa\xC3\xB1ol", "Italiano", "Nederlands" };
static void gc_lang_set(int l, int sync) {                   /* sync = aussi pour le lanceur (global.ini) */
    if (l < 0 || l > 5) return;
    if (SYS_GetLanguage() != l) SYS_SetLanguage((u8)l);
    ui_lang_apply();
    if (sync) ic_put2("System Language", gc_lang_ini[l], 1);
}
static void gc_sound_set(int stereo, int sync) {
    u8 m = stereo ? SYS_SOUND_STEREO : SYS_SOUND_MONO;
    if (SYS_GetSoundMode() != m) SYS_SetSoundMode(m);
    audio_mono = !stereo;
    if (sync) ic_put2("System Sound", stereo ? "Stereo" : "Mono", 1);
}
static void gc_screenx_set(int h) {                          /* position horizontale de l'image (aussi les jeux) */
    if (h < -32) h = -32;
    if (h > 32) h = 32;
    if (h != SYS_GetDisplayOffsetH()) SYS_SetDisplayOffsetH((s8)h);
    if (rmode) { scr_hlive = h - scr_hboot; scr_vi(); }
    char v[8]; snprintf(v, sizeof(v), "%d", h);
    ic_put2("Screen Position", v, 1);
}

static void files_refresh_opts(void) { if (fe) fe_load(); }   /* (onglet FICHIERS deja ouvert : relu) */

/* ---------- demarrage ---------- */
static int net_cfg_seen = 0;                                  /* (gc-mp3.txt a deja ses reglages reseau) */
static int sram_back_lang = -1, sram_back_sound = -1;        /* reglages d'un jeu a annuler au retour */
static char vc_keep[64] = "";                                /* v76 : carte generale, pendant qu'un jeu utilise la sienne (jamais enregistree a sa place) */
static void net_cfg_apply(void);
static void jeux_cfg_boot(void) {
    if (sram_back_lang >= 0) { gc_lang_set(sram_back_lang, 1); sram_back_lang = -1; settings_dirty = 1; }   /* (langue d'un jeu : on remet celle de la console) */
    if (sram_back_sound >= 0) { gc_sound_set(sram_back_sound, 1); sram_back_sound = -1; settings_dirty = 1; }
    audio_mono = SYS_GetSoundMode() == SYS_SOUND_MONO;
    net_cfg_apply();
}

/* ---------- reglages d'un jeu (Y dans la fiche) : SD:/gc-mp3/jeux/<ID>.txt ---------- */
#define JC_DIR GCD "/jeux"
enum { JC_LANG, JC_SOUND, JC_CARD, JC_WS, JC_VIDEO, JC_FLOU, JC_WIDE, JC_CSTICK, JC_TRIG, JC_POLL, JC_SPEED, JC_TRAP, JC_NOCHEAT, JC_N };   /* v121 : JC_WS = 16:9 force ; v122 : video, manette, lecteur (Pulse) */
static char jc_id[8], jc_title[96];
static int jc_lang = -1, jc_sound = -1, jc_sel = 0, jc_changed = 0;   /* -1 = comme la console */
static int jc_ws = 0;                                        /* v121 : 16:9 force avec Pulse */
static int nat_ws;                                           /* (natif.c) */
/* v122 : jc_v[] = reglages Pulse du jeu (0 = comme le jeu), dans l'ordre JC_VIDEO ... JC_SPEED */
#define JC_V0 JC_VIDEO
static int jc_v[JC_N - JC_VIDEO];
static const int jc_vmax[JC_N - JC_VIDEO] = { 4, 2, 2, 4, 4, 3, 3, 2, 2 };   /* (v133 : vitesse : auto / vrai lecteur / rapide) */
static const char *const jc_vkey[JC_N - JC_VIDEO] = { "video", "sans_flou", "image_large", "stick_c", "gachettes", "frequence_manette", "vitesse_lecteur", "acces_exception", "cheats_coupes" };
static int nat_vmode, nat_deflick, nat_wide, nat_cstick, nat_trig, nat_poll, nat_speed, nat_trap, nat_nocard, nat_nocheat;   /* (natif.c) */
static void rep_apply(const char *id);                       /* v140 : reparation automatique (repair.c) */
static void rep_pre(const char *id);
static void rep_diag_set(const char *id, int crashed, int noimg, int stuck, int skipped, int cheats, int ws, int card, int vmode, int slow, int trap);
static char rep_msg[120];                                     /* (essai en cours, pour le journal du lancement) */
static int nat_cheats_prep(const char *id);                  /* (natif.c) */
/* v133 : jeux qui se bloquent quand le disque repond trop vite (une lecture finie avant que le jeu soit pret) :
   ceux que Swiss corrige pour cette raison (rappel du lecteur, transfert vers l'ARAM). Reglage "Auto" :
   vitesse d'un vrai lecteur pour eux, rapide pour les autres. */
static int nat_speed_auto = 0;                               /* (journal : 1 = choisi automatiquement) */
static int jc_slow_known(const char *id) {
    static const char *const slow[] = { "GS2", "GDR", "GHA", "GLE" };   /* (v136 : GYQ retire : la vitesse ne suffisait pas, corrige dans le code du jeu) */
    for (unsigned i = 0; i < sizeof(slow) / sizeof(slow[0]); i++) if (!strncmp(id, slow[i], 3)) return 1;
    return 0;
}
static char jc_card[64] = "";                                 /* "" = carte du reglage general ; v140 : "-" = aucune (vraie carte) */
static void jc_path(const char *id, char *out, int cap) { snprintf(out, cap, JC_DIR "/%.6s.txt", id); }
static void jc_load(const char *id) {
    jc_lang = jc_sound = -1; jc_card[0] = 0; jc_ws = 0; memset(jc_v, 0, sizeof(jc_v));
    char p[NAME_LEN];
    jc_path(id, p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (!strcasecmp(k, "langue")) { for (int i = 0; i < 6; i++) if (!strcasecmp(v, gc_lang_keys[i])) jc_lang = i; }
        else if (!strcasecmp(k, "son")) jc_sound = !strcasecmp(v, "stereo") ? 1 : !strcasecmp(v, "mono") ? 0 : -1;
        else if (!strcasecmp(k, "carte") && strlen(v) < sizeof(jc_card) && !strchr(v, '/') && !strstr(v, "..")) snprintf(jc_card, sizeof(jc_card), "%s", v);
        else if (!strcasecmp(k, "ecran169")) jc_ws = !strcasecmp(v, "oui");
        else for (int i = 0; i < JC_N - JC_V0; i++) if (!strcasecmp(k, jc_vkey[i])) { int x = atoi(v); jc_v[i] = x >= 0 && x < jc_vmax[i] ? x : 0; }
    }
    fclose(f);
}
static void jc_save(void) {
    if (!sd_ok || !jc_id[0]) return;
    char p[NAME_LEN];
    jc_path(jc_id, p, sizeof(p));
    int anyv = 0;
    for (int i = 0; i < JC_N - JC_V0; i++) if (jc_v[i]) anyv = 1;
    if (jc_lang < 0 && jc_sound < 0 && !jc_card[0] && !jc_ws && !anyv) { remove(p); return; }   /* (tout par defaut : pas de fichier) */
    mkdir(JC_DIR, 0777);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "# GC Station : reglages du jeu %s (%s)\n", jc_id, jc_title);
    fprintf(f, "langue = %s\nson = %s\ncarte = %s\n", jc_lang >= 0 ? gc_lang_keys[jc_lang] : "console",
            jc_sound < 0 ? "console" : jc_sound ? "stereo" : "mono", jc_card);
    fprintf(f, "ecran169 = %s\n", jc_ws ? "oui" : "non");
    for (int i = 0; i < JC_N - JC_V0; i++) fprintf(f, "%s = %d\n", jc_vkey[i], jc_v[i]);
    fclose(f);
}
/* juste avant de lancer un jeu : ses reglages (resident = lance avec Resident) */
static void jeux_cfg_apply(const char *id, int resident) {
    jc_load(id);
    nat_ws = jc_ws;                                           /* v121 : 16:9 force (Pulse) */
    nat_vmode = jc_v[JC_VIDEO - JC_V0]; nat_deflick = jc_v[JC_FLOU - JC_V0]; nat_wide = jc_v[JC_WIDE - JC_V0];   /* v122 */
    nat_cstick = jc_v[JC_CSTICK - JC_V0]; nat_trig = jc_v[JC_TRIG - JC_V0]; nat_poll = jc_v[JC_POLL - JC_V0];
    nat_speed = jc_v[JC_SPEED - JC_V0] == 1 || (jc_v[JC_SPEED - JC_V0] == 0 && jc_slow_known(id));   /* v133 : 0 = auto, 1 = vrai lecteur, 2 = rapide */
    nat_speed_auto = jc_v[JC_SPEED - JC_V0] == 0 && nat_speed;
    nat_trap = jc_v[JC_TRAP - JC_V0] == 1;                   /* v139 : Pulse universel (experimental) */
    /* v146 : Star Wars Rogue Leader (GLR) et GWXJ13 : la plus grande partie du programme est compressee dans main.dol
       (decompressee par le jeu en 0x80200000) : ses acces au lecteur sont invisibles avant le lancement. Swiss la
       decompresse pour la corriger ; Pulse les prend au vol (Pulse universel), automatiquement pour ces jeux.
       v152 : GC Station le decompresse aussi, le corrige et le recompresse (natif.c) ; Pulse universel est alors coupe
       au lancement, et reste seulement si le programme corrige ne tient plus a sa place. */
    if (!nat_trap && (!strncmp(id, "GLR", 3) || !strncmp(id, "GWXJ13", 6))) nat_trap = 1;
    nat_nocheat = jc_v[JC_NOCHEAT - JC_V0] == 1;             /* v140 */
    nat_nocard = !strcmp(jc_card, "-");
    if (resident) rep_apply(id);                              /* v140 : essai de reparation (pour ce lancement) */
    int changed = 0;
    if (jc_lang >= 0 && jc_lang != SYS_GetLanguage()) { sram_back_lang = SYS_GetLanguage(); gc_lang_set(jc_lang, !resident); changed = 1; }
    int st = SYS_GetSoundMode() != SYS_SOUND_MONO;
    if (jc_sound >= 0 && jc_sound != st) { sram_back_sound = st; gc_sound_set(jc_sound, !resident); changed = 1; }
    if (resident && jc_card[0] && strcmp(jc_card, "-") && strcmp(jc_card, vc_active)) {   /* (pour ce lancement seulement : pas enregistre) */
        snprintf(vc_keep, sizeof(vc_keep), "%s", vc_active);
        snprintf(vc_active, sizeof(vc_active), "%s", jc_card);
    }
    if (changed) save_settings();                             /* (on remettra la console comme avant au retour) */
}
static void indigo_game_open(const char *id, const char *title) {     /* (nom garde : appele depuis la fiche d'un jeu) */
    snprintf(jc_id, sizeof(jc_id), "%.6s", id);
    snprintf(jc_title, sizeof(jc_title), "%s", title);
    jc_load(jc_id);
    jc_sel = 0; jc_changed = 0;
    ic_on = 1;
    crumb("reglages du jeu %.6s", id);
}
static void jc_close(void) {
    if (jc_changed) { jc_save(); gl_note("R\xC3\xA9glages du jeu enregistr\xC3\xA9s"); }
    ic_on = 0;
}
static void jc_card_step(int d) {                             /* "" -> carte-A.raw -> carte-2.raw ... -> "-" (aucune) */
    vc_scan();
    int cur = -1;                                             /* (-1 = reglage general, vc_nf = aucune) */
    if (!strcmp(jc_card, "-")) cur = vc_nf;
    for (int i = 0; i < vc_nf; i++) if (!strcmp(vc_files[i], jc_card)) cur = i;
    cur += d;
    if (cur < -1) cur = vc_nf;
    if (cur > vc_nf) cur = -1;
    if (cur < 0) jc_card[0] = 0; else if (cur == vc_nf) snprintf(jc_card, sizeof(jc_card), "-"); else snprintf(jc_card, sizeof(jc_card), "%s", vc_files[cur]);
}
static void indigo_cfg_input(u32 down, u32 nav, int fire) {
    if (fire && (nav & PAD_BUTTON_DOWN)) jc_sel = (jc_sel + 1) % JC_N;
    if (fire && (nav & PAD_BUTTON_UP)) jc_sel = (jc_sel + JC_N - 1) % JC_N;
    int d = (fire && (nav & PAD_BUTTON_RIGHT)) || (down & PAD_BUTTON_A) ? 1 : (fire && (nav & PAD_BUTTON_LEFT)) ? -1 : 0;
    if (d) {
        jc_changed = 1;
        if (jc_sel == JC_LANG) { jc_lang += d; if (jc_lang > 5) jc_lang = -1; if (jc_lang < -1) jc_lang = 5; }
        else if (jc_sel == JC_SOUND) { jc_sound += d; if (jc_sound > 1) jc_sound = -1; if (jc_sound < -1) jc_sound = 1; }
        else if (jc_sel == JC_WS) jc_ws = !jc_ws;
        else if (jc_sel >= JC_V0) { int i = jc_sel - JC_V0; jc_v[i] = (jc_v[i] + d + jc_vmax[i]) % jc_vmax[i]; }
        else jc_card_step(d);
    }
    if (down & PAD_BUTTON_Y) { jc_lang = jc_sound = -1; jc_card[0] = 0; jc_ws = 0; memset(jc_v, 0, sizeof(jc_v)); jc_changed = 1; gl_note("Tout comme la console"); }
    if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) jc_close();
}
static void draw_indigo_cfg(void) {
    char buf[160], fv[160];
    tl_frame("R\xC3\xA9glages du jeu", NULL);
    fit_text(F_MED, jc_title, 500, fv, sizeof(fv));
    gx_text1(F_MED, 58, 100, fv, RGBA(255, 210, 120, 255));
    static const char *const lab[JC_N] = { "Langue du jeu", "Son", "Carte m\xC3\xA9moire (Pulse)", "16:9 forc\xC3\xA9 (Pulse)",
        "Mode vid\xC3\xA9o (Pulse)", "Sans flou (Pulse)", "Image plus large (Pulse)", "Stick C (Pulse)", "G\xC3\xA2" "chettes L / R (Pulse)",
        "Lecture des manettes (Pulse)", "Vitesse du lecteur (Pulse)", "Pulse universel (exp\xC3\xA9rimental)", "Cheats (Pulse)" };
    static const char *const help[JC_N] = {
        "Langue des jeux multilingues (PAL), pour ce jeu seulement. La console revient \xC3\xA0 la sienne apr\xC3\xA8s.",
        "St\xC3\xA9r\xC3\xA9o ou mono pour ce jeu seulement.",
        "Carte virtuelle utilis\xC3\xA9" "e par ce jeu avec Pulse (Outils > Cartes m\xC3\xA9moire virtuelles).",
        "Image 3D \xC3\xA9largie pour une t\xC3\xA9l\xC3\xA9 16:9 (mets la t\xC3\xA9l\xC3\xA9 en 16:9). Les menus 2D restent \xC3\xA9tir\xC3\xA9s.",
        "480p : c\xC3\xA2" "ble composante. PAL 60 Hz : jeu PAL en 480 lignes. 240p : t\xC3\xA9l\xC3\xA9s cathodiques, \xC3\xA9" "crans r\xC3\xA9tro.",
        "Enl\xC3\xA8ve le flou anti-scintillement de l'image (plus net, peut scintiller en entrelac\xC3\xA9).",
        "Image sur 704 points au lieu de 640 : moins de bords noirs sur les c\xC3\xB4t\xC3\xA9s.",
        "Invers\xC3\xA9 : haut / bas (ou tout) ; \xC3\xA9" "chang\xC3\xA9 : le stick C devient le stick principal et inversement.",
        "L et R comptent comme enfonc\xC3\xA9s d\xC3\xA8s que la g\xC3\xA2" "chette d\xC3\xA9passe ce seuil.",
        "Manettes lues plus souvent : moins de d\xC3\xA9lai (jeux de combat, de rythme).",
        "Auto : vitesse d'un vrai lecteur pour les jeux connus pour bloquer, rapide sinon.",
        "Jeu qui ne marche pas : les acc\xC3\xA8s au lecteur oubli\xC3\xA9s sont attrap\xC3\xA9s par le processeur.",
        "Coup\xC3\xA9s : le jeu est lanc\xC3\xA9 sans ses cheats (ils restent choisis)." };
    static const char *const vv[JC_N - JC_V0][4] = {
        { "Comme le jeu", "480p forc\xC3\xA9", "PAL 60 Hz", "240p" }, { "Non", "Oui" }, { "Non", "Oui (704)" },
        { "Normal", "Invers\xC3\xA9 haut / bas", "Invers\xC3\xA9 (tout)", "\xC3\x89" "chang\xC3\xA9 avec le stick" },
        { "Comme le jeu", "Seuil bas (30 %)", "Seuil moyen (50 %)", "Seuil haut (75 %)" }, { "Normale", "2 fois plus", "4 fois plus" },
        { "Auto", "Comme un vrai lecteur", "Rapide (carte SD)" }, { "Non", "Oui" }, { "Comme choisis", "Coup\xC3\xA9s" } };
    const int rh = 24, vis = 11;                          /* (v122 : 11 lignes, la liste defile au-dela) */
    static int top = 0;
    if (jc_sel < top) top = jc_sel;
    if (jc_sel >= top + vis) top = jc_sel - vis + 1;
    for (int r = 0; r < vis && top + r < JC_N; r++) {
        int i = top + r;
        f32 ry = 116 + r * rh;
        int on = i == jc_sel;
        gp_zone(52, ry, 536, rh - 2, &jc_sel, i, GPZ_SEL);
        if (on) gx_rrect_fill(52, ry, 536, rh - 2, 6, TL_SEL1, TL_SEL2);
        gx_text1(F_MED, 66, ry + 16, TR(lab[i]), on ? RGBA(255, 255, 255, 255) : RGBA(230, 222, 255, 255));
        if (i == JC_LANG) snprintf(buf, sizeof(buf), "%s", jc_lang < 0 ? TR("Comme la console") : gc_lang_names[jc_lang]);
        else if (i == JC_SOUND) snprintf(buf, sizeof(buf), "%s", jc_sound < 0 ? TR("Comme la console") : jc_sound ? TR("St\xC3\xA9r\xC3\xA9o") : "Mono");
        else if (i == JC_WS) snprintf(buf, sizeof(buf), "%s", jc_ws ? TR("Oui") : TR("Non"));
        else if (i == JC_SPEED && !jc_v[i - JC_V0]) snprintf(buf, sizeof(buf), "%s", jc_slow_known(jc_id) ? TR("Auto : vrai lecteur") : TR("Auto : rapide"));   /* v133 */
        else if (i >= JC_V0) snprintf(buf, sizeof(buf), "%s", TR(vv[i - JC_V0][jc_v[i - JC_V0]]));
        else snprintf(buf, sizeof(buf), "%s", !strcmp(jc_card, "-") ? TR("Aucune (vraie carte)") : jc_card[0] ? jc_card : TR("R\xC3\xA9glage g\xC3\xA9n\xC3\xA9ral"));
        fit_text_sel(on, F_MED, buf, 240, fv, sizeof(fv));
        gx_text_r(F_MED, 576, ry + 16, fv, on ? RGBA(255, 255, 255, 255) : jc_sel >= 0 && i >= JC_V0 && jc_v[i - JC_V0] ? RGBA(130, 230, 160, 255) : RGBA(190, 160, 255, 255));
    }
    if (top > 0) gx_tri(316, 112, 324, 112, 320, 107, RGBA(255, 255, 255, 180));
    if (top + vis < JC_N) gx_tri(316, 116 + vis * rh, 324, 116 + vis * rh, 320, 121 + vis * rh, RGBA(255, 255, 255, 180));
    fit_text(F_SMALL, TR(help[jc_sel]), 540, fv, sizeof(fv));
    gx_text1(F_SMALL, 58, 116 + vis * rh + 18, fv, RGBA(210, 200, 240, 255));
    tl_msgbox();
    static const char *const k[] = { "A", "Y", "B" }, *const l[] = { "CHANGER", "TOUT PAR D\xC3\x89" "FAUT", "FERMER" };
    tl_legend(k, l, 3);
}

/* ---------- lancer un jeu avec ses reglages (ils sont annules si le lancement echoue) ---------- */
static void jeux_cfg_undo(const char *card) {
    if (sram_back_lang >= 0) { gc_lang_set(sram_back_lang, 1); sram_back_lang = -1; }
    if (sram_back_sound >= 0) { gc_sound_set(sram_back_sound, 1); sram_back_sound = -1; }
    snprintf(vc_active, sizeof(vc_active), "%s", card);
    vc_keep[0] = 0;
    save_settings();
}
static const char *gcl_boot(int k);                           /* v121 : GC Loader (gcl.c) */
static int gcl_path(const char *p);
static const char *game_boot_auto(int k) {                   /* v90 : Resident ; le lanceur de secours si des cheats sont actives */
    if (k >= 0 && k < gl_n && gcl_path(gl[k].path)) { const char *e = gcl_boot(k); nat_ld_on = 0; return e; }   /* v121 : jeu du GC Loader */
    if (k >= 0 && k < gl_n && gl[k].dvd) { const char *e = nat_boot_disc(k); nat_ld_on = 0; return e; }   /* v97 : vrai lecteur */
    if (k >= 0 && k < gl_n) rep_pre(gl[k].id);                /* v140 : cheats coupes pour ce jeu (reglage ou essai de reparation) */
    int sec = k >= 0 && k < gl_n && gl_cheats_on(k) > 0 && nat_cheats_prep(gl[k].id) < 0 && ld_ready();   /* (v121 : Pulse applique les cheats qu'il comprend) */
    if (sec) gl_status("Lancement du jeu...");
    const char *e = game_boot(k, !sec);
    if (!sec) nat_ld_on = 0;                                 /* (fin de l'ecran de chargement de Resident) */
    return e;
}
static const char *game_boot(int k, int resident) {
    if (k < 0 || k >= gl_n) return "Jeu introuvable";
    if (gl[k].dvd) { const char *e = nat_boot_disc(k); nat_ld_on = 0; return e; }   /* v97 : vrai lecteur */
    if (gcl_path(gl[k].path)) { const char *e = gcl_boot(k); nat_ld_on = 0; return e; }   /* v121 : GC Loader */
    char card[64];
    snprintf(card, sizeof(card), "%s", vc_active);
    jeux_cfg_apply(gl[k].id, resident);
    const char *e = resident ? nat_boot(k) : gl_launch(k);     /* (ne reviennent pas si ca marche) */
    if (e) jeux_cfg_undo(card);
    return e;
}

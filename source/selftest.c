// GC Station v259 : START > Reglages > Outils > Test de la console (inclus dans main.c, apres themes.c).
// Sur la vraie GameCube : des mesures, puis GC Station se parcourt toute seule (chaque onglet affiche, listes qui
// defilent, menu START), un morceau en fond, avec la mesure de fluidite (v69) et le compte des trous du son (v154).
// Resultat : a l'ecran (resume), dans SD:/gc-mp3/journaux/test-console.txt (detail ; le precedent est garde en
// test-console-ancien.txt pour comparer) et sur le telephone (/test.txt, dans le rapport de bug /rapport).
// v261 : test complet (A ; Y : le test rapide d'avant) :
//   materiel : memoire principale (chaque mot verifie, debits, temps d'acces), ARAM (16 Mo, contenu remis),
//              processeur graphique (remplissage, petits dessins), carte SD (au hasard, dossiers, place), manettes
//              au repos (derive des sticks), DNS, GitHub (connexion chiffree, debit, connexion gardee) ;
//   son : chaque format present sur la carte SD (charge du decodage, trous), une radio (temps de connexion) ;
//   parcours en deux passages (memoire perdue entre les deux = fuite), puis stress (onglets tres vite, menu,
//   outils) ; chaque image bloquee (150 ms et plus) est notee avec ce qui se passait (partie du dessin,
//   processeur pris, travaux en fond) ; diagnostic en clair et note sur 100.
// (textes du diagnostic en UTF-8 : ils vont a l'ecran et dans le rapport)

#define SX_FILE GCD_LOG "/test-console.txt"
#define SX_OLD GCD_LOG "/test-console-ancien.txt"
enum { SX_IDLE, SX_INTRO, SX_BENCH, SX_NET, SX_SOUND, SX_RADIO, SX_NETUSE, SX_TOUR, SX_SCRIPT, SX_STRESS, SX_DONE };   /* (v265 : SX_NETUSE) */
static int sx_state = SX_IDLE, sx_full = 1, sx_page = 0;
static u64 sx_t = 0, sx_t0 = 0;
static int sx_tabs[TAB_COUNT], sx_ntabs = 0, sx_i = 0, sx_sub = 0, sx_music = 0, sx_aborted = 0, sx_tab0 = 0, sx_pass = 0;
static u32 sx_cpu_ms = 0, sx_int_ms = 0, sx_dbl_ms = 0, sx_cpy_mbs = 0, sx_free0 = 0, sx_big0 = 0, sx_rkbs = 0, sx_wkbs = 0, sx_stat_x10 = 0;
static u32 sx_gap0 = 0, sx_gapev0 = 0, sx_tot0 = 0, sx_gap = 0, sx_gapev = 0, sx_played = 0;
static u32 sx_frames = 0, sx_slow = 0, sx_worst = 0;
static int sx_worst_tab = -1;
static char sx_prev[300];                       /* (mesures du test d'avant : "cpu=.. sdr=.. sdw=.. net=.. ok=..") */
static hfetch_t sx_hf = { .c = { -1, 0, 0 } };
static char *sx_rep = NULL;
static int sx_bstep = 0, sx_bsub = 0;            /* (etape des mesures : une par image, l'ecran avance) */

/* materiel (test complet) */
static u32 sx_ram_kb = 0, sx_ram_err = 0, sx_ram_w = 0, sx_ram_r = 0, sx_ram_ns = 0;
static int sx_aram = 0;                          /* 0 pas teste, 1 fait, 2 simulateur */
static u32 sx_aram_kb = 0, sx_aram_err = 0, sx_aram_mbs = 0;
static u32 sx_gpu_mpx = 0, sx_gpu_tri = 0;
static u32 sx_rnd_x10 = 0, sx_dir_ms = 0, sx_dir_n = 0, sx_sd_free = 0, sx_sd_tot = 0;   /* (Mo) */
static u32 sx_pad_mask = 0;
static u8 sx_pad_dev[4][4];                      /* (ecart max au repos : stick X, Y, C X, Y) */
static u8 sx_pad_trg[4];
static u32 sx_pad_type[4];
static int sx_dns_ms = -1;                       /* -1 pas fait, -2 echec */

/* reseau : 0 GitHub (versions), 1 grosse image, 2 petite image (meme connexion) */
#define SX_NN 3
static const char *const sx_net_name[SX_NN] = { "GitHub (fichier des versions)", "grosse image (350 Ko)", "petite image, connexion gardée" };
static int sx_ni = 0, sx_nn = 1;
static int sx_net_st[SX_NN];                     /* 0 en cours, 1 ok, 2 echec, 3 pas de reseau, 4 trop lent (partiel) */
static u32 sx_net_ms[SX_NN], sx_net_head[SX_NN], sx_net_bytes[SX_NN], sx_net_kbs[SX_NN];

/* son : un morceau de chaque format */
#define SX_NF 9
static const char *const sx_fmt_name[SX_NF] = { "MP3", "FLAC", "OGG", "WAV", "M4A", "retro (SPC, NSF...)", "modules (MOD, XM...)", "GameCube (DSP, HPS...)", "MIDI" };
static int sx_fmt_k[SX_NF], sx_fmt_n = 0, sx_fmt_i = 0;
static u32 sx_fmt_cpu[SX_NF], sx_fmt_gap[SX_NF], sx_fmt_slow[SX_NF], sx_fmt_ok[SX_NF];
static u32 sx_mark_dec = 0, sx_mark_gap = 0, sx_mark_tot = 0, sx_mark_c = 0, sx_seek_ms = 0;
static u64 sx_mark_t = 0;
/* radio */
static int sx_radio = 0;                         /* 0 pas essaye, 1 ok, 2 pas de son en 20 s, 3 pas de reseau / station, 4 erreur */
static u32 sx_radio_ms = 0, sx_radio_gap = 0;

/* images par moment du test */
enum { SB_SOUND, SB_RADIO, SB_NETU, SB_TOUR1, SB_TOUR2, SB_SCRIPT, SB_STRESS, SB_N };
static const char *const sx_sb_name[SB_N] = { "formats audio", "radio", "réseau en vrai", "parcours 1", "parcours 2", "GC Station", "stress" };
static u32 sx_sb_frames[SB_N], sx_sb_slow[SB_N], sx_sb_worst[SB_N];
static int sx_sb = -1;
static u32 sx_fb_frames = 0, sx_fb_slow = 0;     /* (format en cours) */
static u32 sx_mem_p1 = 0, sx_mem_p2 = 0, sx_mem_end = 0;   /* memoire libre apres chaque passage, apres le stress */

/* images bloquees (150 ms et plus) */
#define SX_NS 24
typedef struct { u32 ms, top_us, stolen_us, dsec_us; u16 bg; u8 tab, sb, top; const char *dsec;
                 u16 dma, dexp; u32 sdk, netk; } sxs_t;      /* v265 : pendant l'image : fins de bloc du son (et attendues), Ko lus/ecrits sur la SD, Ko recus du reseau */
static u32 sx_f_cb = 0, sx_f_sd = 0, sx_f_net = 0;  /* (compteurs a l'image d'avant) */
static sxs_t sx_st[SX_NS];
static int sx_nst = 0;
static u32 sx_nst_all = 0, sx_st_500 = 0;
enum { BG_COVER = 1, BG_LIST = 2, BG_PHONE = 4, BG_IMG = 8, BG_CONN = 16, BG_RADIO = 32, BG_WEB = 64 };

/* diagnostic */
#define SX_ND 22
static char sx_diag[SX_ND][128];
static u8 sx_diag_lv[SX_ND];                    /* 0 bien, 1 a surveiller, 2 probleme */
static int sx_nd = 0, sx_score = -1;

static u32 sx_prev_val(const char *k) {
    const char *p = strstr(sx_prev, k);
    return p ? (u32)strtoul(p + strlen(k), NULL, 10) : 0;
}

static void sx_open(void) {
    menu_open = 0;
    tl_screen = TL_SELFTEST;
    if (sx_state == SX_IDLE || (sx_state == SX_DONE && !sx_rep)) sx_state = SX_INTRO;
}

static u32 sx_free_now(void) {
    struct mallinfo mi = mallinfo();
    return (u32)((uintptr_t)SYS_GetArena1Hi() - (uintptr_t)SYS_GetArena1Lo() + mi.fordblks);
}
/* texte UTF-8 complete a w caracteres (les noms des onglets ont des accents) */
static void sx_pad(char *o, int cap, const char *s, int w) {
    int n = snprintf(o, cap, "%s", s), c = 0;
    if (n >= cap) n = cap - 1;
    for (const char *p = o; *p; p++) if (((unsigned char)*p & 0xC0) != 0x80) c++;
    while (c < w && n < cap - 1) { o[n++] = ' '; o[n] = 0; c++; }
}
static void sx_mo(char *o, int cap, u32 b) { snprintf(o, cap, "%u,%u Mo", (unsigned)(b >> 20), (unsigned)(((b & 0xFFFFF) * 10) >> 20)); }

/* ======================= v263 : TEST TOTAL =======================
   Tout le materiel (carte SD du SD2SP2 avec petits fichiers, cartes memoire A et B : lecture, ecriture puis effacement
   d'un fichier de test, horloge comparee a Internet, image, horloge du son, couvercle, serveur du telephone) et tout
   GC Station : chaque outil, chaque mini-jeu, chaque style sur chaque onglet, chaque couleur, disposition, spectre,
   economiseur, decor, l'anglais, chaque reglage change puis remis, photos et video. Chaque element : sa pire image,
   ses images ratees, la memoire qu'il garde. Pendant le test rien n'est enregistre (sx_quiet) ; a la fin, les
   reglages d'avant sont remis (et compares). */
static int sx_total = 0;
static char *sx_cfg0 = NULL;                     /* (reglages avant le test, texte de gc-mp3.txt) */
static size_t sx_cfg0_n = 0;
static char sx_cfg_diff[6][72];
static int sx_cfg_nd = 0;

enum { IP_FMT, IP_SEEK, IP_NET, IP_TOOL, IP_GAME, IP_SKIN, IP_THEME, IP_LAYOUT, IP_VIZ, IP_SAVER, IP_SEASON, IP_LANG, IP_SET, IP_MEDIA, IP_N };
static const char *const sx_ip_name[IP_N] = { "Formats audio", "Sauts dans les morceaux", "Réseau en vrai (musique en fond)", "Outils", "Mini-jeux", "Styles sur chaque onglet", "Couleurs", "Dispositions du lecteur",
    "Spectres", "Économiseurs d'écran", "Décors", "Interface en anglais", "Réglages (changés puis remis)", "Photos et vidéos" };
#define SX_NI 260
typedef struct { char nm[96]; u8 ph, st; u16 slow; u32 worst_us, frames; s32 dmem; } sxi_t;   /* st : 0 ok, 1 probleme, 2 pas disponible */
static sxi_t *sx_it = NULL;
static int sx_nit = 0, sx_ci = -1;
static u32 sx_ci_mem = 0;
static void sx_item_begin(int ph, const char *fmt, ...) {
    sx_ci = -1;
    if (!sx_it || sx_nit >= SX_NI) return;
    sxi_t *it = &sx_it[sx_nit];
    memset(it, 0, sizeof(*it));
    va_list ap; va_start(ap, fmt); vsnprintf(it->nm, sizeof(it->nm), fmt, ap); va_end(ap);
    it->ph = (u8)ph;
    sx_ci = sx_nit++;
    sx_ci_mem = sx_free_now();
    crumb("test : %s", it->nm);                  /* (si la console plante : on sait sur quoi) */
}
static void sx_item_end(int st) {
    if (sx_ci < 0) return;
    sxi_t *it = &sx_it[sx_ci];
    if (st > it->st) it->st = (u8)st;
    it->dmem = (s32)sx_ci_mem - (s32)sx_free_now();
    if (it->st == 0 && it->worst_us >= 500000u) it->st = 1;       /* (une image figee une demi-seconde : a regarder) */
    sx_ci = -1;
}

/* ---------- materiel en plus ---------- */
static u32 sx_sm_cr_x10 = 0, sx_sm_rm_x10 = 0, sx_sm_n = 0;     /* petits fichiers : creation, effacement (dixiemes de ms) */
typedef struct { int st, err; u16 mbit; u32 sect; u16 freeb; int files; u32 rd_kbs; int wr; u32 wr_ms, rd_ms; u32 verr; } sxc_t;
static sxc_t sx_card[2];                         /* st : 0 vide, 1 ok, 2 erreur, 3 adaptateur SD ; wr : 0 pas fait, 1 ok, 2 erreur, 3 pas de place */
static int sx_clk_ok = 0;                        /* heure lue sur Internet */
static s32 sx_clk_diff = 0, sx_clk_tz = 0;       /* (secondes : console - heure universelle ; fuseau devine) */
static u32 sx_vi_r0 = 0, sx_vi_x100 = 0;         /* images de l'ecran : mesure de la frequence (centiemes de Hz) */
static u64 sx_vi_t0 = 0;
static u32 sx_ac_samples = 0, sx_ac_ms = 0;      /* horloge du son : echantillons joues / temps */
static int sx_cover = -1;                        /* couvercle du lecteur : 0 ferme, 1 ouvert, -1 inconnu */

static void sx_bench_small(void) {
    sx_sm_cr_x10 = sx_sm_rm_x10 = sx_sm_n = 0;
    if (!sd_ok || sd_virtual) return;
    const char *dir = GCD_LOG "/.test-petits";
    mkdir(dir, 0777);
    static u8 b[4096];
    memset(b, 0x3C, sizeof(b));
    char p[96];
    u32 t0 = perf_us();
    int n = 0;
    for (int i = 0; i < 30; i++) {
        snprintf(p, sizeof(p), "%s/f%02d.bin", dir, i);
        FILE *f = fopen(p, "wb");
        if (!f) break;
        fwrite(b, 1, sizeof(b), f); fclose(f); n++;
        if ((i & 7) == 7) service_audio();
    }
    u32 t1 = perf_us();
    for (int i = 0; i < n; i++) { snprintf(p, sizeof(p), "%s/f%02d.bin", dir, i); remove(p); if ((i & 7) == 7) service_audio(); }
    u32 t2 = perf_us();
    rmdir(dir);
    if (n) { sx_sm_n = (u32)n; sx_sm_cr_x10 = (t1 - t0) / 100u / (u32)n; sx_sm_rm_x10 = (t2 - t1) / 100u / (u32)n; }
}

#define SX_CARD_TEST "GCSTATION-TEST"
static void sx_bench_cards(void) {
    memset(sx_card, 0, sizeof(sx_card));
    if (!mc_work) mc_work = (u8 *)memalign(32, CARD_WORKAREA_SIZE);
    if (!mc_work) return;
    if (!mc_inited) { CARD_Init("GMP3", "GC"); mc_inited = 1; }
    if (mc_mounted >= 0) mc_unmount();
    u8 *buf = (u8 *)memalign(32, 65536), *rb = (u8 *)memalign(32, 65536);
    if (!buf || !rb) { free(buf); free(rb); return; }
    for (int slot = 0; slot < 2; slot++) {
        sxc_t *c = &sx_card[slot];
        if (sd_slot == slot + 1) { c->st = 3; continue; }          /* (adaptateur SD dans ce slot : on n'y touche pas) */
        int r = CARD_ERROR_BUSY;
        for (int t = 0; t < 10 && r == CARD_ERROR_BUSY; t++) { r = CARD_Mount(slot, mc_work, NULL); service_audio(); }
        if (r < 0) { c->err = r; c->st = r == CARD_ERROR_NOCARD ? 0 : r == CARD_ERROR_WRONGDEVICE ? 3 : 2; if (r != CARD_ERROR_NOCARD) CARD_Unmount(slot); continue; }
        c->st = 1;
        { s32 mb = 0, ss = 0;                                      /* (taille en Mbit : libogc n'a pas CARD_GetMemSize) */
          CARD_ProbeEx(slot, &mb, &ss);
          c->mbit = (u16)(mb > 0 ? mb : 0); }
        CARD_GetSectorSize(slot, &c->sect);
        if (c->sect < 8192 || c->sect > 65536) c->sect = 8192;
        CARD_GetFreeBlocks(slot, &c->freeb);
        card_dir d, d0; memset(&d, 0, sizeof(d)); memset(&d0, 0, sizeof(d0));
        int k = CARD_FindFirst(slot, &d, true);
        while (k == CARD_ERROR_READY) { if (!c->files) d0 = d; c->files++; k = CARD_FindNext(&d); }
        if (c->files) {                                            /* lecture : le debut de la premiere sauvegarde */
            card_file f;
            if (CARD_OpenEntry(slot, &d0, &f) == CARD_ERROR_READY) {
                u32 len = d0.filelen < 65536 ? d0.filelen : 65536;
                len -= len % c->sect;
                u32 t0 = perf_us(), got = 0;
                for (u32 o = 0; o < len; o += c->sect) { if (CARD_Read(&f, buf, c->sect, o) != CARD_ERROR_READY) break; got += c->sect; service_audio(); }
                u32 dt = perf_us() - t0;
                if (got && dt) c->rd_kbs = (u32)((u64)got * 1000000ull / 1024ull / dt);
                CARD_Close(&f);
            }
        }
        if (sd_virtual) { CARD_Unmount(slot); continue; }          /* (la carte garde les reglages : pas d'ecriture) */
        if (c->freeb < 1) { c->wr = 3; CARD_Unmount(slot); continue; }
        /* ecriture d'un fichier de test d'un bloc, relecture, verification, effacement */
        CARD_SetGamecode("GMP3"); CARD_SetCompany("GC");
        CARD_Delete(slot, SX_CARD_TEST);                           /* (reste d'un test interrompu) */
        card_file f;
        int ok = CARD_Create(slot, SX_CARD_TEST, c->sect, &f) == CARD_ERROR_READY;
        if (ok) {
            for (u32 i = 0; i < c->sect; i++) buf[i] = (u8)(i * 13 + 7);
            DCFlushRange(buf, c->sect);
            u32 t0 = perf_us();
            ok = CARD_Write(&f, buf, c->sect, 0) == CARD_ERROR_READY;
            c->wr_ms = (perf_us() - t0) / 1000u;
            CARD_Close(&f);
            service_audio();
        }
        if (ok && CARD_Open(slot, SX_CARD_TEST, &f) == CARD_ERROR_READY) {
            memset(rb, 0, c->sect);
            DCFlushRange(rb, c->sect);
            u32 t0 = perf_us();
            if (CARD_Read(&f, rb, c->sect, 0) == CARD_ERROR_READY) {
                c->rd_ms = (perf_us() - t0) / 1000u;
                DCInvalidateRange(rb, c->sect);
                for (u32 i = 0; i < c->sect; i++) if (rb[i] != buf[i]) c->verr++;
            } else ok = 0;
            CARD_Close(&f);
        } else ok = 0;
        int del = CARD_Delete(slot, SX_CARD_TEST) == CARD_ERROR_READY;
        u16 fr2 = 0; CARD_GetFreeBlocks(slot, &fr2);
        c->wr = ok && del && !c->verr && fr2 == c->freeb ? 1 : 2;
        CARD_Unmount(slot);
        service_audio();
    }
    free(buf); free(rb);
}

/* heure d'Internet (en-tete Date de GitHub) : la console avance ou retarde-t-elle ? */
static s64 sx_days_civil(int y, int m, int d) {          /* (jours depuis le 1er janvier 1970) */
    y -= m <= 2;
    s64 era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (s64)doe - 719468;
}
static void sx_clock_check(const unsigned char *hdr, int hlen) {
    sx_clk_ok = 0;
    static const char *const mo = "JanFebMarAprMayJunJulAugSepOctNovDec";
    for (int i = 0; i + 6 < hlen; i++) {
        if ((i && hdr[i - 1] != '\n') || strncasecmp((const char *)hdr + i, "date:", 5)) continue;
        char line[64]; int n = 0;
        for (int j = i + 5; j < hlen && hdr[j] != '\r' && hdr[j] != '\n' && n < 63; j++) line[n++] = (char)hdr[j];
        line[n] = 0;
        int d, y, H, M, S; char mon[4];
        const char *c = strchr(line, ',');
        if (!c || sscanf(c + 1, "%d %3s %d %d:%d:%d", &d, mon, &y, &H, &M, &S) != 6) return;
        const char *mp = strstr(mo, mon);
        if (!mp) return;
        int m = (int)(mp - mo) / 3 + 1;
        s64 utc = sx_days_civil(y, m, d) * 86400 + H * 3600 + M * 60 + S;
        s64 diff = (s64)time(NULL) - utc;
        s64 tz = (diff >= 0 ? diff + 450 : diff - 450) / 900 * 900;   /* (fuseau : au quart d'heure pres) */
        sx_clk_diff = (s32)diff; sx_clk_tz = (s32)tz; sx_clk_ok = 1;
        return;
    }
}

static void sx_hw_info(void) {
#ifdef GEKKO
    sx_cover = (*(volatile u32 *)0xCC006004) & 1;   /* (registre du couvercle du lecteur, juste lu) */
#else
    sx_cover = -1;
#endif
    sx_vi_r0 = retrace_count; sx_vi_t0 = perf_us();
}

/* ---------- GC Station : la liste de tout ce qui est essaye ---------- */
enum { K_TOOL, K_GAME, K_SKINTAB, K_THEME, K_LAYOUT, K_VIZ, K_SAVER, K_SEASON, K_LANGTAB, K_RESTORE, K_SET, K_PHOTOTAB, K_PHOTO, K_VIDEO };
typedef struct { u8 k; s16 a, b; u16 ms; } sxk_t;
#define SX_NK 300
static sxk_t *sx_ks = NULL;
static int sx_nk = 0, sx_k = 0, sx_kph = 0;
static char sx_v0[96];
static int sx_set_dir = 1;

static void sx_pj0(void) { pj_open(0); }
static void sx_nfs0(void) { nfs_ui_open(0); }
typedef void (*sx_fn)(void);
static const struct { const char *nm; sx_fn fn; } sx_tools[] = {
    { "Derniers jeux lancés", rc_open }, { "Profils de joueurs", sx_pj0 }, { "Ton récap", rcp_open }, { "Mon compagnon", pet_open },
    { "Succès", sc_open }, { "Mode soirée", so_open }, { "Guide de démarrage", gde_open }, { "Catalogue d'applis", ct_open },
    { "Cheats", cheats_open }, { "Cartes mémoire virtuelles", vc_open }, { "Historique des sauvegardes", sv_open },
    { "Infos console", info_open }, { "Mémoire en direct", mem_open }, { "Cartes mémoire", mc_open }, { "Applis", ap_open },
    { "Historique radio", rh_open }, { "Globe des radios", gb_open }, { "Thèmes en ligne", thm_open }, { "Partages réseau", sx_nfs0 } };
#define SX_NTOOLS ((int)(sizeof(sx_tools) / sizeof(sx_tools[0])))
static const s8 sx_set_safe[] = { MI_SHUFFLE, MI_REPEAT, MI_XFADE, MI_VIEW, MI_VOLUME, MI_RG, MI_EQ, MI_BASS, MI_LOWMID, MI_MID, MI_PRES, MI_TREBLE,
    MI_SKIN, MI_THEME, MI_WALL, MI_SEASON, MI_SAVER, MI_SAVERSTYLE, MI_COVERS, MI_LYRICS, MI_PODSPEED, MI_PODSIL, MI_RESUME, MI_RADIORES, MI_GAPLESS,
    MI_UILANG, MI_LANG, MI_GLVIEW, MI_GLANIM, MI_GLAUTO, MI_NATIGR, MI_NATCARD, MI_NATCARDREG, MI_NATCARDGAME, MI_PULSELD, MI_PARTY, MI_NIGHT,
    MI_LAYOUT, MI_VIZ, MI_SGPAL, MI_LYRSHOW, MI_FLASH, MI_HIDDEN, MI_UNKNOWN, MI_MASK, MI_APNOIMG, MI_UPDAUTO, MI_RECAT, MI_RECLEN };
static const int sx_skins[4] = { 0, 3, 4, 5 };
static const char *const sx_skin_nm[4] = { "Classique", "Aurore", "Vitrine", "Îlots" };

static void sx_k_add(int k, int a, int b, int ms) { if (sx_ks && sx_nk < SX_NK) { sxk_t *s = &sx_ks[sx_nk++]; s->k = (u8)k; s->a = (s16)a; s->b = (s16)b; s->ms = (u16)ms; } }
static void sx_script_build(void) {
    free(sx_ks); sx_ks = (sxk_t *)calloc(SX_NK, sizeof(sxk_t)); sx_nk = 0;
    for (int i = 0; i < SX_NTOOLS; i++) sx_k_add(K_TOOL, i, 0, 2600);
    for (int g = 0; g < GM_COUNT; g++) sx_k_add(K_GAME, g, 0, 3500);
    for (int s = 0; s < 4; s++) for (int i = 0; i < sx_ntabs; i++) sx_k_add(K_SKINTAB, s, sx_tabs[i], 1100);
    sx_k_add(K_RESTORE, 0, 0, 400);
    for (int i = 0; i < THEME_COUNT; i++) sx_k_add(K_THEME, i, 0, 1200);
    for (int i = 0; i < LAYOUT_COUNT; i++) sx_k_add(K_LAYOUT, i, 0, 1700);
    for (int i = 0; i < VZ_COUNT; i++) sx_k_add(K_VIZ, i, 0, 1700);
    for (int i = 0; i < SV_COUNT; i++) sx_k_add(K_SAVER, i, 0, 2600);
    for (int i = 0; i < SEASON_MODES; i++) sx_k_add(K_SEASON, i, 0, 1200);
    sx_k_add(K_RESTORE, 0, 0, 400);
    for (int i = 0; i < sx_ntabs; i++) sx_k_add(K_LANGTAB, sx_tabs[i], 0, 1100);
    sx_k_add(K_RESTORE, 0, 0, 400);
    for (int i = 0; i < (int)sizeof(sx_set_safe); i++) sx_k_add(K_SET, sx_set_safe[i], 0, 700);
    sx_k_add(K_RESTORE, 0, 0, 400);
    sx_k_add(K_PHOTOTAB, 0, 0, 3000);
    for (int i = 0; i < 3; i++) sx_k_add(K_PHOTO, i, 0, 2500);
    sx_k_add(K_VIDEO, 0, 0, 6000);
    sx_k_add(K_RESTORE, 0, 0, 400);
}

/* reglages d'avant le test : remis (et compares) */
static void sx_cfg_restore(int final) {
    if (!sx_cfg0) return;
    char *b = (char *)malloc(sx_cfg0_n + 1);
    if (!b) return;
    memcpy(b, sx_cfg0, sx_cfg0_n); b[sx_cfg0_n] = 0;
    for (char *l = b; l && *l;) {
        char *nl = strchr(l, '\n');
        if (nl) *nl = 0;
        char *t = trim(l);
        if (t[0] && t[0] != '#') { char *eq = strchr(t, '='); if (eq) { *eq = 0; setting_apply(trim(t), trim(eq + 1)); } }
        l = nl ? nl + 1 : NULL;
    }
    free(b);
    eq_setup();
    upd_xz = upd_xz_set ? upd_xz_file : 1;
    saver_on = 0;
    settings_dirty = 0;
    if (!final) return;
    /* comparaison : le texte des reglages doit etre exactement celui d'avant */
    sx_cfg_nd = 0;
    char *now_ = NULL; size_t nn = 0;
    if (!cfgw_build(&now_, &nn)) return;
    if (nn != sx_cfg0_n || memcmp(now_, sx_cfg0, nn)) {
        const char *a = sx_cfg0, *c = now_;
        while (*a && *c && sx_cfg_nd < 6) {
            const char *ae = strchr(a, '\n'), *ce = strchr(c, '\n');
            size_t al = ae ? (size_t)(ae - a) : strlen(a), cl = ce ? (size_t)(ce - c) : strlen(c);
            if (al != cl || memcmp(a, c, al)) snprintf(sx_cfg_diff[sx_cfg_nd++], sizeof(sx_cfg_diff[0]), "%.*s", (int)(al > 70 ? 70 : al), a);
            a = ae ? ae + 1 : a + al; c = ce ? ce + 1 : c + cl;
        }
        if (!sx_cfg_nd) snprintf(sx_cfg_diff[sx_cfg_nd++], sizeof(sx_cfg_diff[0]), "(longueur differente)");
    }
    free(now_);
}

static void sx_reset_ui(void) {
    if (fun_mode) fun_leave();
    party_on = 0; saver_on = 0;
    if (mc_mounted >= 0) mc_unmount();                   /* (carte memoire laissee montee par l'outil Cartes) */
    tl_screen = TL_NONE; menu_open = 0;
    last_input_ms = now_ms();
}
static void sx_music_on(void) {                          /* (un morceau en fond, comme a l'usage) */
    if ((src == SRC_FILE && playing) || (src == SRC_RADIO && state != ST_IDLE)) return;
    for (int i = 0; i < nfiles; i++) if (!is_retro[i] && !strncasecmp(names[i], SD_ROOT, strlen(SD_ROOT))) { if (start_track(i)) sx_music = 1; return; }
}
static int sx_menu_sec_of(int mi) {
    for (int s = 0; s < SEC_TOOLS; s++) for (int i = 0; i < sec_n(s); i++) if (sec_items[s][i] == mi) return s;
    return 0;
}
static int sx_photo_idx(int nth, int video) {
    if (npf <= 0) return -1;
    for (int i = 0; i < npf; i++) if (!pf[i].bad && (pf[i].video != 0) == (video != 0) && nth-- == 0) return i;
    return -1;
}

/* debut d'une etape : 0 = pas disponible */
static int sx_k_start(const sxk_t *s) {
    switch (s->k) {
    case K_TOOL:
        sx_item_begin(IP_TOOL, "%s", sx_tools[s->a].nm);
        sx_reset_ui(); sx_tools[s->a].fn(); return 1;
    case K_GAME:
        sx_item_begin(IP_GAME, "%s", gm_names[s->a]);
        sx_reset_ui(); tab = TAB_GAMES; tab_enter(tab);
        if (s->a == GM_QUIZ && fun_tracks() < 4) return 0;
        games_tab_launch(s->a);
        return fun_mode != FUN_OFF;
    case K_SKINTAB:
        sx_item_begin(IP_SKIN, "%s : %s", sx_skin_nm[s->a], tab_names[s->b]);
        sx_reset_ui(); ui_skin = sx_skins[s->a]; tab = s->b; tab_enter(tab); return 1;
    case K_THEME:
        sx_item_begin(IP_THEME, "%s", theme_names[s->a]);
        sx_reset_ui(); theme_apply(s->a); tab = TAB_MUSIC; tab_enter(tab); return 1;
    case K_LAYOUT:
        sx_item_begin(IP_LAYOUT, "%s", layout_names[s->a]);
        sx_reset_ui(); tab = TAB_MUSIC; tab_enter(tab); layout_tab[TAB_MUSIC] = s->a; return 1;
    case K_VIZ:
        sx_item_begin(IP_VIZ, "%s", viz_names[s->a]);
        sx_reset_ui(); tab = TAB_MUSIC; tab_enter(tab); layout_tab[TAB_MUSIC] = L_BIGVIZ; viz_style = s->a; return 1;
    case K_SAVER:
        sx_item_begin(IP_SAVER, "%s", saver_names[s->a]);
        sx_reset_ui(); saver_style = s->a; saver_on = 1; return 1;
    case K_SEASON:
        sx_item_begin(IP_SEASON, "décor %d / %d", s->a + 1, SEASON_MODES);
        sx_reset_ui(); season_on = s->a; tab = TAB_MUSIC; tab_enter(tab); return 1;
    case K_LANGTAB:
        sx_item_begin(IP_LANG, "%s", tab_names[s->a]);
        sx_reset_ui(); ui_lang = 1; ui_lang_apply(); tab = s->a; tab_enter(tab); return 1;
    case K_RESTORE:
        sx_reset_ui(); sx_cfg_restore(0); tab = TAB_MUSIC; tab_enter(tab); sx_music_on(); return 1;
    case K_SET: {
        int mi = s->a;
        sx_reset_ui(); tab = TAB_MUSIC;
        menu_open = 1; menu_sec = sx_menu_sec_of(mi); menu_sel = mi;
        menu_value(mi, sx_v0, sizeof(sx_v0));
        sx_item_begin(IP_SET, "%s", menu_labels[mi]);       /* (avant le changement : si ca plante, on le sait) */
        sx_set_dir = +1;
        menu_change(mi, +1);
        settings_dirty = 0;
        char v1[96]; menu_value(mi, v1, sizeof(v1));
        if (!strcmp(sx_v0, v1)) { sx_set_dir = -1; menu_change(mi, -1); settings_dirty = 0; menu_value(mi, v1, sizeof(v1)); }   /* (deja au bout : dans l'autre sens) */
        if (sx_ci >= 0) snprintf(sx_it[sx_ci].nm, sizeof(sx_it[0].nm), "%.28s : %.15s > %.15s", menu_labels[mi], sx_v0, v1);
        return strcmp(sx_v0, v1) != 0;
    }
    case K_PHOTOTAB:
        sx_reset_ui(); tab = TAB_PHOTO; tab_enter(tab); return 1;
    case K_PHOTO: case K_VIDEO: {
        int i = sx_photo_idx(s->a, s->k == K_VIDEO);
        sx_item_begin(IP_MEDIA, "%s %s", s->k == K_VIDEO ? "vidéo" : "photo", i >= 0 ? base_of(pf[i].path) : "(aucune sur la carte SD)");
        if (i < 0) return 0;
        sx_reset_ui(); tab = TAB_PHOTO; pf_sel = i; pv_open(i);
        return s->k == K_VIDEO ? pv_mode == 2 : pv_mode == 1;
    }
    }
    return 0;
}
static void sx_k_end(const sxk_t *s) {
    switch (s->k) {
    case K_GAME: if (fun_mode) fun_leave(); break;
    case K_SAVER: saver_on = 0; break;
    case K_SET: menu_change(s->a, -sx_set_dir); settings_dirty = 0; menu_open = 0; break;
    case K_PHOTO: pv_mode = 0; pv_slide = 0; break;
    case K_VIDEO: if (pv_mode == 2) video_stop(); pv_mode = 0; break;
    default: break;
    }
}
static void sx_script_tick(u64 n) {
    if (n < sx_t) return;
    if (sx_k >= sx_nk) { sx_reset_ui(); sx_cfg_restore(0); sx_state = SX_STRESS; sx_sb = SB_STRESS; sx_sub = 0; sx_t = n; return; }
    const sxk_t *s = &sx_ks[sx_k];
    if (sx_kph == 0) {
        sx_music_on();
        int ok = sx_k_start(s);
        if (!ok) { sx_k_end(s); sx_item_end(2); sx_reset_ui(); sx_k++; sx_t = n + 150; return; }
        sx_kph = 1; sx_t = n + s->ms; return;
    }
    if (sx_kph == 1) {
        if (s->k == K_TOOL) { tp_keys |= PAD_BUTTON_B; sx_kph = 2; sx_t = n + 450; return; }   /* (fermeture normale, par B) */
        sx_k_end(s);
        sx_item_end(0);
        sx_kph = 0; sx_k++; sx_t = n + 250; return;
    }
    sx_reset_ui();                                       /* (outil : apres B) */
    sx_item_end(0);
    sx_kph = 0; sx_k++; sx_t = n + 250;
}


/* ---------- images : appele par perf_frame (main.c) a chaque image mesuree ---------- */
static void sx_frame_hook(u32 tot, int tb, int top, u32 top_us) {
    u32 cbn = cb_n, sdn = sd_rd_sec + sd_wr_sec, nbn = net_rx_bytes;
    u32 d_cb = cbn - sx_f_cb, d_sd = sdn - sx_f_sd, d_net = nbn - sx_f_net;   /* (v265 : ce qui s'est passe pendant cette image) */
    sx_f_cb = cbn; sx_f_sd = sdn; sx_f_net = nbn;
    if (sx_state < SX_SOUND || sx_state > SX_STRESS) return;
    int slow = tot > VID_SLOW_US;
    if (sx_sb >= 0) { sx_sb_frames[sx_sb]++; if (slow) sx_sb_slow[sx_sb]++; if (tot > sx_sb_worst[sx_sb]) sx_sb_worst[sx_sb] = tot; }
    sx_fb_frames++; if (slow) sx_fb_slow++;
    if (sx_ci >= 0) { sxi_t *it = &sx_it[sx_ci]; it->frames++; if (slow && it->slow < 65535) it->slow++; if (tot > it->worst_us) it->worst_us = tot; }
    if (tot < 150000u) return;
    sx_nst_all++;
    if (tot >= 500000u) sx_st_500++;
    sxs_t *s;
    if (sx_nst < SX_NS) s = &sx_st[sx_nst++];
    else {                                       /* (plein : remplace la plus courte si celle-ci est plus longue) */
        int m = 0;
        for (int i = 1; i < SX_NS; i++) if (sx_st[i].ms < sx_st[m].ms) m = i;
        if (sx_st[m].ms >= tot / 1000u) return;
        s = &sx_st[m];
    }
    memset(s, 0, sizeof(*s));
    s->ms = tot / 1000u; s->tab = (u8)tb; s->sb = (u8)(sx_sb < 0 ? 0 : sx_sb); s->top = (u8)top; s->top_us = top_us;
    s->stolen_us = pfx_stolen_acc;                   /* (pris pendant cette image : pas encore range dans pfx_cur) */
    s->dsec = dp_top_n; s->dsec_us = dp_top_us;
    s->dma = (u16)(d_cb > 65535u ? 65535u : d_cb);
    { u32 e = (u32)((u64)tot * 48u / 1000u / DMA_SAMPLES); s->dexp = (u16)(e > 65535u ? 65535u : e); }   /* (un bloc = DMA_SAMPLES / 48 ms) */
    s->sdk = d_sd / 2u; s->netk = d_net / 1024u;
    u16 bg = 0;
    if (cw_state == 1 || cw_state == 2) bg |= BG_COVER;
    if (lsc_qh != lsc_qt) bg |= BG_LIST;
    if (tp_qh != tp_qt || tp_up_active || tp_open_busy) bg |= BG_PHONE;
    if (dj_state == 1 || cv_state == 1 || gj_state == 1 || tj_state == 1 || rl_state == 1) bg |= BG_IMG;
    for (int k = 0; k < AC_N; k++) if (ac_job[k].st == 1 || ac_job[k].st == 2) bg |= BG_CONN;
    if (src == SRC_RADIO && (state == ST_CONNECT || state == ST_BUFFER)) bg |= BG_RADIO;
    if (tb == TAB_WEB) bg |= BG_WEB;
    s->bg = bg;
}
static void sx_bg_text(char *o, int cap, u16 bg) {
    o[0] = 0;
    static const char *const nm[] = { "pochette", "liste (durées)", "téléphone", "image à décoder", "connexion en cours", "radio qui se connecte", "page web" };
    int n = 0;
    for (int i = 0; i < 7; i++) if ((bg & (1 << i)) && n < cap) n += snprintf(o + n, cap - n, "%s%s", n ? ", " : "", nm[i]);
}

static void sx_perf_reset(void) {
    memset(pfx_frames, 0, sizeof(pfx_frames)); memset(pfx_slow, 0, sizeof(pfx_slow)); memset(pfx_vslow, 0, sizeof(pfx_vslow)); memset(pfx_worst, 0, sizeof(pfx_worst));
    memset(pfx_sum, 0, sizeof(pfx_sum)); memset(pfx_max, 0, sizeof(pfx_max)); memset(pfx_top, 0, sizeof(pfx_top));
}

/* ---------- 1. mesures directes (une etape par image) ---------- */
static void sx_bench_cpu(void) {
    sx_free0 = sx_free_now();
    sx_big0 = mv_largest();
    volatile float acc = 0;
    u32 t0 = perf_us();
    for (int i = 0; i < 300000; i++) acc += sinf((float)i * 0.001f) * 1.0001f;
    u32 t1 = perf_us();
    volatile u32 h = 1;
    for (int i = 0; i < 4000000; i++) h = (h * 1664525u + 1013904223u) ^ (h >> 7);
    u32 t2 = perf_us();
    sx_cpu_ms = (t1 - t0) / 1000u; sx_int_ms = (t2 - t1) / 1000u;
    service_audio();
    if (sx_full) {
        volatile double d = 1.0;
        u32 t3 = perf_us();
        for (int i = 1; i < 400000; i++) d = d * 1.0000001 + 1.0 / (double)i;
        sx_dbl_ms = (perf_us() - t3) / 1000u;
        service_audio();
    }
    u8 *a = (u8 *)malloc(1 << 20), *b = (u8 *)malloc(1 << 20);
    if (a && b) {
        memset(a, 0x5A, 1 << 20);
        u32 c0 = perf_us();
        volatile u32 sink = 0;
        for (int k = 0; k < 16; k++) { memcpy(b, a, 1 << 20); sink += b[(k * 65537) & 0xFFFFF]; }   /* (relue : pas supprimee par le compilateur) */
        u32 dt = perf_us() - c0;
        (void)sink;
        sx_cpy_mbs = dt ? (u32)(16ull * 1000000ull / dt) : 0;
    }
    free(a); free(b);
    service_audio();
}

/* memoire principale : chaque mot ecrit puis verifie (deux motifs), debits, temps d'acces au hasard */
static void sx_bench_ram(void) {
    sx_ram_kb = sx_ram_err = sx_ram_w = sx_ram_r = sx_ram_ns = 0;
    u32 big = mv_largest(), sz = big > (3u << 20) ? big - (3u << 19) : big / 2;
    if (sz > (6u << 20)) sz = 6u << 20;
    sz &= ~0xFFFFu;
    u32 *m = NULL;
    while (sz >= (1u << 20) && !(m = (u32 *)malloc(sz))) sz -= 1u << 20;
    if (!m) return;
    sx_ram_kb = sz >> 10;
    u32 nw = sz / 4, err = 0, tw = 0, tr = 0;
    for (int pass = 0; pass < 2; pass++) {
        u32 x = pass ? 0xFFFFFFFFu : 0;
        u32 t0 = perf_us();
        for (u32 i = 0; i < nw; i++) m[i] = (i * 2654435761u) ^ x;
        DCFlushRange(m, sz);
        u32 t1 = perf_us();
        DCInvalidateRange(m, sz);                    /* (relu depuis la memoire, pas depuis le cache) */
        volatile u32 *vm = m;
        for (u32 i = 0; i < nw; i++) if (vm[i] != ((i * 2654435761u) ^ x)) err++;
        u32 t2 = perf_us();
        tw += t1 - t0; tr += t2 - t1;
        service_audio();
    }
    sx_ram_err = err;
    sx_ram_w = tw ? (u32)((2ull * sz * 1000000ull / tw) >> 20) : 0;
    sx_ram_r = tr ? (u32)((2ull * sz * 1000000ull / tr) >> 20) : 0;
    /* temps d'acces : une chaine au hasard de cases (une ligne de cache chacune), sur tout le bloc */
    u32 nn = sz / 32;
    if (nn > 65536) nn = 65536;
    u32 stride = sz / 4 / nn;                       /* (en mots) */
    for (u32 i = 0; i < nn; i++) m[i * stride] = i;
    u32 r = 12345;
    for (u32 i = nn - 1; i > 0; i--) {              /* (Sattolo : un seul grand cycle) */
        r = r * 1664525u + 1013904223u;
        u32 j = (r >> 8) % i;
        u32 t = m[i * stride]; m[i * stride] = m[j * stride]; m[j * stride] = t;
    }
    DCFlushRange(m, sz);
    service_audio();
    volatile u32 *vm = m;
    u32 p = 0, steps = 200000, t0 = perf_us();
    for (u32 k = 0; k < steps; k++) p = vm[p * stride];
    u32 dt = perf_us() - t0;
    volatile u32 sink = p; (void)sink;
    sx_ram_ns = (u32)((u64)dt * 1000ull / steps);
    free(m);
    service_audio();
}

/* ARAM (16 Mo de memoire du son, que GC Station n'utilise pas) : chaque morceau lu, ecrit, relu, verifie, remis */
static void sx_bench_aram(void) {
#ifdef GEKKO
    sx_aram_kb = sx_aram_err = sx_aram_mbs = 0;
    const u32 CH = 64 * 1024;
    u8 *o = (u8 *)memalign(32, CH), *w = (u8 *)memalign(32, CH), *r = (u8 *)memalign(32, CH);
    if (!o || !w || !r) { free(o); free(w); free(r); return; }
    AR_Init(NULL, 0);
    u32 size = AR_GetSize();
    if (size > 16u << 20 || size < (1u << 20)) size = 16u << 20;
    u32 tdma = 0, moved = 0;
    for (u32 a = 0x10000; a + CH <= size; a += CH) {
        DCInvalidateRange(o, CH);
        AR_StartDMA(AR_ARAMTOMRAM, (u32)o, a, CH); while (AR_GetDMAStatus());
        for (u32 i = 0; i < CH / 4; i++) ((u32 *)w)[i] = (a + i * 4) ^ 0xA55AC33Cu;
        DCFlushRange(w, CH);
        DCInvalidateRange(r, CH);
        u32 t0 = perf_us();
        AR_StartDMA(AR_MRAMTOARAM, (u32)w, a, CH); while (AR_GetDMAStatus());
        AR_StartDMA(AR_ARAMTOMRAM, (u32)r, a, CH); while (AR_GetDMAStatus());
        tdma += perf_us() - t0; moved += 2 * CH;
        for (u32 i = 0; i < CH / 4; i++) if (((u32 *)r)[i] != ((u32 *)w)[i]) sx_aram_err++;
        DCFlushRange(o, CH);
        AR_StartDMA(AR_MRAMTOARAM, (u32)o, a, CH); while (AR_GetDMAStatus());   /* (contenu d'avant remis) */
        sx_aram_kb += CH >> 10;
        if (((a >> 16) & 15) == 0) service_audio();
    }
    sx_aram_mbs = tdma ? (u32)(((u64)moved * 1000000ull / tdma) >> 20) : 0;
    free(o); free(w); free(r);
    sx_aram = 1;
#else
    sx_aram = 2;
#endif
}

/* processeur graphique : appele pendant le dessin, avant le cadre (rectangles presque transparents : invisibles) */
static void sx_bench_gpu(void) {
    GX_DrawDone();
    u32 t0 = perf_us();
    for (int i = 0; i < 40; i++) gx_rect(0, 0, 640, 480, RGBA(0, 0, 0, 1));
    GX_DrawDone();
    u32 t1 = perf_us();
    for (int i = 0; i < 4000; i++) { f32 x = (f32)(i % 80) * 8, y = (f32)(i / 80) * 9; gx_tri(x, y, x + 6, y, x, y + 6, RGBA(0, 0, 0, 1)); }
    GX_DrawDone();
    u32 t2 = perf_us();
#ifdef GEKKO
    sx_gpu_mpx = t1 > t0 ? (u32)(40ull * 640 * 480 / (t1 - t0)) : 0;     /* (millions de points par seconde) */
    sx_gpu_tri = t2 > t1 ? (u32)(4000ull * 1000ull / (t2 - t1)) : 0;      /* (milliers par seconde) */
#else
    (void)t0; (void)t1; (void)t2;                    /* (simulateur : le dessin n'attend rien, pas de mesure) */
    sx_gpu_mpx = sx_gpu_tri = 0;
#endif
}

static void sx_bench_sd(void) {
    sx_rkbs = sx_wkbs = sx_stat_x10 = sx_rnd_x10 = sx_dir_ms = sx_dir_n = 0;
    if (!sd_ok || sd_virtual) return;
    static u8 blk[65536];
    for (int i = 0; i < (int)sizeof(blk); i++) blk[i] = (u8)(i * 7);
    const char *tf = GCD_LOG "/.test-carte";
    int nblk = sx_full ? 64 : 32;                    /* (4 Mo ; rapide : 2 Mo) */
    u32 w0 = perf_us();
    FILE *f = fopen(tf, "wb");
    int ok = f != NULL;
    for (int k = 0; ok && k < nblk; k++) { if (fwrite(blk, 1, sizeof(blk), f) != sizeof(blk)) ok = 0; service_audio(); }
    if (f && fclose(f) != 0) ok = 0;
    u32 wdt = perf_us() - w0;
    if (ok && wdt) sx_wkbs = (u32)((u64)nblk * 64ull * 1000000ull / wdt);
    f = ok ? fopen(tf, "rb") : NULL;
    if (f) {
        u32 r0 = perf_us();
        long got = 0; size_t n;
        while ((n = fread(blk, 1, sizeof(blk), f)) > 0) { got += (long)n; service_audio(); }
        u32 rdt = perf_us() - r0;
        if (rdt && got > 0) sx_rkbs = (u32)((u64)got * 1000000ull / 1024ull / rdt);
        if (sx_full) {                               /* 100 lectures de 4 Ko au hasard */
            u32 r = 777, q0 = perf_us();
            for (int k = 0; k < 100; k++) {
                r = r * 1664525u + 1013904223u;
                fseek(f, (long)((r >> 8) % (u32)(nblk * 16)) * 4096, SEEK_SET);
                if (fread(blk, 1, 4096, f) != 4096) break;
                if ((k & 15) == 15) service_audio();
            }
            sx_rnd_x10 = (perf_us() - q0) / 1000u;   /* (100 lectures : dixiemes de ms par lecture) */
        }
        fclose(f);
    }
    remove(tf);
    int ns = nfiles < 40 ? nfiles : 40;
    if (ns > 0) {
        struct stat st;
        u32 s0 = perf_us();
        for (int k = 0; k < ns; k++) stat(names[k], &st);
        sx_stat_x10 = (perf_us() - s0) / 100u / (u32)ns;
    }
    if (sx_full) {
        char dir[NAME_LEN];                          /* le dossier du premier morceau */
        snprintf(dir, sizeof(dir), "%s", nfiles > 0 ? names[0] : SD_ROOT "/");
        char *sl = strrchr(dir, '/');
        if (sl && sl > dir && nfiles > 0) *sl = 0;
        u32 d0 = perf_us();
        DIR *d = opendir(dir);
        if (d) { while (readdir(d)) { sx_dir_n++; if ((sx_dir_n & 63) == 0) service_audio(); } closedir(d); }
        sx_dir_ms = (perf_us() - d0) / 1000u;
        unsigned long long tot = 0, fre = 0;
        if (sd_space(&tot, &fre)) { sx_sd_tot = (u32)(tot >> 20); sx_sd_free = (u32)(fre >> 20); }
    }
}

static void sx_bench_pads(int first) {           /* (appele sur 40 images : au repos, sans toucher) */
    if (first) {
        memset(sx_pad_dev, 0, sizeof(sx_pad_dev)); memset(sx_pad_trg, 0, sizeof(sx_pad_trg));
        sx_pad_mask = sx_padmask & 15;
        for (int p = 0; p < 4; p++) sx_pad_type[p] = (sx_pad_mask & (1u << p)) ? SI_DecodeType(SI_GetType(p)) : 0;
    }
    for (int p = 0; p < 4; p++) {
        if (!(sx_pad_mask & (1u << p))) continue;
        int v[4] = { PAD_StickX(p), PAD_StickY(p), PAD_SubStickX(p), PAD_SubStickY(p) };
        for (int i = 0; i < 4; i++) { int a = v[i] < 0 ? -v[i] : v[i]; if (a > sx_pad_dev[p][i]) sx_pad_dev[p][i] = (u8)(a > 255 ? 255 : a); }
        int t = PAD_TriggerL(p) > PAD_TriggerR(p) ? PAD_TriggerL(p) : PAD_TriggerR(p);
        if (t > sx_pad_trg[p]) sx_pad_trg[p] = (u8)t;
    }
}

static void sx_bench_dns(void) {
    sx_dns_ms = -1;
    if (net_phase != NET_DONE || !net_ok || !gw_str[0]) return;
    u32 ip = 0, t0 = perf_us();
    int ok = dns_query(inet_addr(gw_str), "raw.githubusercontent.com", &ip);
    sx_dns_ms = ok ? (int)((perf_us() - t0) / 1000u) : -2;
    service_audio();
}

/* ---------- 2. reseau ---------- */
static void sx_net_start(void) {
    if (sx_ni >= sx_nn) return;
    int i = sx_ni;
    sx_net_st[i] = 0; sx_net_ms[i] = sx_net_head[i] = sx_net_bytes[i] = sx_net_kbs[i] = 0;
    if (net_phase != NET_DONE || !net_ok) { sx_net_st[i] = 3; return; }
    char url[256];
    if (i == 0) upd_rel_url(url, sizeof(url), 0);
    else if (i == 1) snprintf(url, sizeof(url), "https://raw.githubusercontent.com/%s/main/docs/logo/banniere-1280x640.png", upd_repo);
    else snprintf(url, sizeof(url), "https://raw.githubusercontent.com/%s/main/themes/nebuleuse.jpg", upd_repo);
    sx_hf.maxbytes = 1024 * 1024;
    sx_hf.image = 0;
    sx_hf.accept = i ? "*/*" : "application/json";
    hf_start(&sx_hf, url, NULL);
    sx_t0 = now_ms();
}
static int sx_net_pump(void) {                              /* 1 = tout fini */
    for (;;) {
        if (sx_ni >= sx_nn) return 1;
        int i = sx_ni;
        if (!sx_net_st[i]) {
            hf_pump(&sx_hf);
            u64 n = now_ms();
            if (!sx_net_head[i] && (sx_hf.st == HF_HEAD || sx_hf.st == HF_BODY || sx_hf.st == HF_DONE)) sx_net_head[i] = (u32)(n - sx_t0);
            if (sx_hf.st == HF_DONE) { sx_net_st[i] = 1; sx_net_bytes[i] = (u32)sx_hf.len; if (i == 0) sx_clock_check(sx_hf.hdr, sx_hf.hlen); }
            else if (sx_hf.st == HF_FAIL) sx_net_st[i] = 2;
            else if (n - sx_t0 > (i == 1 ? 30000u : 20000u)) {
                sx_net_bytes[i] = sx_hf.st == HF_BODY && sx_hf.len > 0 ? (u32)sx_hf.len : 0;
                hf_abort(&sx_hf);
                sx_net_st[i] = sx_net_bytes[i] ? 4 : 2;
            } else return 0;
            sx_net_ms[i] = (u32)(n - sx_t0);
            u32 body = sx_net_ms[i] > sx_net_head[i] ? sx_net_ms[i] - sx_net_head[i] : sx_net_ms[i];
            u32 base = i == 0 ? sx_net_ms[i] : body;     /* (grosse image : debit apres la reponse) */
            if (sx_net_bytes[i]) sx_net_kbs[i] = (u32)((u64)sx_net_bytes[i] * 1000ull / 1024ull / (base ? base : 1));
            if (sx_net_st[i] == 1) hf_drop(&sx_hf);       /* (la connexion reste ouverte : la petite image la reprend) */
            sx_hf.st = HF_IDLE;
        }
        sx_ni++;
        if (sx_ni < sx_nn) { sx_net_start(); if (!sx_net_st[sx_ni]) return 0; }
    }
}

/* ---------- 3. son : chaque format, une radio ---------- */
static void sx_radio_start(void);
static void sx_tour_start(int pass);
static void sx_netuse_start(void);
static void sx_sound_start(void) {
    sx_fmt_n = 0; sx_fmt_i = -1; sx_sub = 0;
    static const int fm[SX_NF] = { FMT_MP3, FMT_FLAC, FMT_OGG, FMT_WAV, FMT_M4A, FMT_GME, FMT_XMP, FMT_GCN, FMT_MID };
    for (int f = 0; f < SX_NF; f++) {
        sx_fmt_k[f] = -1; sx_fmt_cpu[f] = sx_fmt_gap[f] = sx_fmt_slow[f] = sx_fmt_ok[f] = 0;
        for (int k = 0; k < nfiles; k++)
            if (!strncasecmp(names[k], SD_ROOT, strlen(SD_ROOT)) && audio_fmt(names[k]) == fm[f]) { sx_fmt_k[f] = k; sx_fmt_n++; break; }
    }
    if (src == SRC_RADIO) stop_station();
    sx_perf_reset();
    if (!perf_on) perf_toggle();                      /* (images mesurees des maintenant) */
    menu_open = 0; tl_screen = TL_NONE;
    tab = TAB_MUSIC; tab_enter(tab);
    sx_sb = SB_SOUND;
    sx_state = SX_SOUND;
    sx_t = now_ms();
}
static void sx_sound_tick(u64 n) {
    if (n < sx_t) return;
    if (sx_sub == 1) {                               /* mesure de ce format finie */
        int f = sx_fmt_i;
        u32 el = (u32)(n - sx_mark_t) * 1000u;
        sx_fmt_cpu[f] = el ? (u32)((u64)(dec_cost_us - sx_mark_dec) * 1000ull / el) : 0;   /* (pour mille du processeur) */
        sx_fmt_gap[f] = au_gap_ev - sx_mark_gap;
        sx_fmt_slow[f] = sx_fb_slow;
        sx_fmt_ok[f] = src == SRC_FILE && playing ? 1 : 0;
        if (sx_fmt_ok[f]) { sx_ac_samples += au_tot - sx_mark_tot; sx_ac_ms += (u32)(n - sx_mark_t); }   /* (horloge du son) */
        sx_item_end(sx_fmt_ok[f] ? (sx_fmt_gap[f] ? 1 : 0) : 1);
        if (sx_total && sx_fmt_ok[f]) {              /* saut au milieu du morceau */
            int d = dur_known(sx_fmt_k[f]);
            sx_item_begin(IP_SEEK, "%s : saut à %d s", sx_fmt_name[f], d > 4 ? d / 2 : 0);
            if (d > 6) {
                u32 t0 = perf_us();
                track_seek(d / 2);
                sx_seek_ms = (perf_us() - t0) / 1000u;
                sx_mark_gap = au_gap_ev; sx_mark_c = consumed;
                sx_sub = 3; sx_t = n + 2000; return;
            }
            sx_item_end(2);
        }
        sx_sub = 0;
    }
    if (sx_sub == 3) {                               /* apres le saut : ca joue, depuis le milieu ? */
        int f = sx_fmt_i, d = dur_known(sx_fmt_k[f]);
        int ok = src == SRC_FILE && playing && consumed / 48000u + 2u >= (u32)(d / 2) && consumed > sx_mark_c;   /* (saut approche : 2 s pres) */
        if (sx_ci >= 0) { size_t l = strlen(sx_it[sx_ci].nm); snprintf(sx_it[sx_ci].nm + l, sizeof(sx_it[0].nm) - l, " (%u ms)", (unsigned)sx_seek_ms); }
        sx_item_end(ok ? (au_gap_ev != sx_mark_gap ? 1 : 0) : 1);
        sx_sub = 0;
    }
    if (sx_sub == 0) {                               /* format suivant */
        do sx_fmt_i++; while (sx_fmt_i < SX_NF && sx_fmt_k[sx_fmt_i] < 0);
        if (sx_fmt_i >= SX_NF) { stop_track(); sx_radio_start(); return; }
        sx_item_begin(IP_FMT, "%s (%s)", sx_fmt_name[sx_fmt_i], base_of(names[sx_fmt_k[sx_fmt_i]]));
        if (!start_track(sx_fmt_k[sx_fmt_i])) { sx_fmt_ok[sx_fmt_i] = 0; sx_item_end(1); sx_t = n + 200; return; }
        sx_sub = 2; sx_t = n + 1500;                 /* (le morceau demarre) */
        return;
    }
    if (sx_sub == 2) {                               /* debut de la mesure */
        sx_mark_dec = dec_cost_us; sx_mark_gap = au_gap_ev; sx_mark_t = n; sx_mark_tot = au_tot;
        sx_fb_frames = sx_fb_slow = 0;
        sx_sub = 1; sx_t = n + 6000;
    }
}
static void sx_radio_start(void) {
    sx_radio = 3; sx_radio_ms = sx_radio_gap = 0;
    if (net_phase != NET_DONE || !net_ok || nstations <= 0) { sx_tour_start(1); return; }
    sx_sb = SB_RADIO;
    int st = cur_station >= 0 && cur_station < nstations ? cur_station : 0;
    start_station(st, 0);
    tab = TAB_RADIO; tab_enter(tab);
    sx_state = SX_RADIO; sx_sub = 0; sx_t0 = now_ms(); sx_t = sx_t0;
}
static void sx_radio_tick(u64 n) {
    if (sx_sub == 0) {                               /* connexion */
        if (src == SRC_RADIO && state == ST_PLAY) { sx_radio_ms = (u32)(n - sx_t0); sx_mark_gap = au_gap_ev; sx_sub = 1; sx_t = n + 10000; return; }
        if (n - sx_t0 > 20000 || state == ST_ERROR || src != SRC_RADIO) { sx_radio = n - sx_t0 > 20000 ? 2 : 4; stop_station(); sx_netuse_start(); }
        return;
    }
    if (n < sx_t) return;
    sx_radio_gap = au_gap_ev - sx_mark_gap;
    sx_radio = 1;
    stop_station();
    sx_netuse_start();
}

/* ---------- 3 bis. v265 : reseau en vrai, avec de la musique en fond ----------
   Un gros telechargement ecrit sur la carte SD, une page web dans Cube Browser, une radio en AAC : pour chacun,
   les trous du son et les images bloquees (avec leur cause, voir sxs_t). */
#define SX_DL_URL  "https://raw.githubusercontent.com/%s/main/android/app/src/main/res/raw/terre_sdf.bin"   /* (3,5 Mo) */
#define SX_DL_FILE GCD "/cache/test-telechargement.bin"
#define SX_WEB_URL "https://fr.wikipedia.org/wiki/Nintendo_GameCube"
#define SX_AAC_URL "https://icecast.radiofrance.fr/fip-hifi.aac?id=radiofrance"
static hfetch_t sx_dl;
static FILE *sx_dl_f = NULL;
static u32 sx_nu_ms = 0;
static void sx_nu_note(const char *fmt, ...) {               /* (ajoute au nom de l'element en cours) */
    if (sx_ci < 0) return;
    char *nm = sx_it[sx_ci].nm;
    size_t l = strlen(nm);
    va_list ap; va_start(ap, fmt); vsnprintf(nm + l, sizeof(sx_it[0].nm) - l, fmt, ap); va_end(ap);
}
static void sx_nu_close(void) {                              /* (arret : telechargement, fichier, radio) */
    if (sx_dl.st != HF_IDLE) { if (sx_dl.st == HF_DONE || sx_dl.st == HF_FAIL) hf_drop(&sx_dl); else hf_abort(&sx_dl); sx_dl.st = HF_IDLE; }
    sx_dl.sink = NULL;
    if (sx_dl_f) { fclose(sx_dl_f); sx_dl_f = NULL; remove(SX_DL_FILE); }
}
static void sx_netuse_start(void) {
    if (!sx_total || net_phase != NET_DONE || !net_ok) { sx_tour_start(1); return; }   /* (test total seulement : resultats dans la liste des elements) */
    sx_sb = SB_NETU;
    sx_state = SX_NETUSE; sx_sub = 0; sx_t = now_ms();
}
static void sx_netuse_tick(u64 n) {
    if (n < sx_t) return;
    switch (sx_sub) {
    case 0: {                                                /* gros telechargement, ecrit sur la carte SD */
        if (src == SRC_RADIO) stop_station();
        if (!(src == SRC_FILE && playing) && nfiles > 0) {
            int k = -1;
            for (int i = 0; i < nfiles && k < 0; i++) if (!is_retro[i]) k = i;
            if (k >= 0) start_track(k);
        }
        menu_open = 0; tl_screen = TL_NONE; tab = TAB_MUSIC; tab_enter(tab);
        sx_item_begin(IP_NET, "Téléchargement 3,5 Mo sur SD");
        mkdir(GCD "/cache", 0777);
        sx_dl_f = sd_ok && !sd_virtual ? fopen(SX_DL_FILE, "wb") : NULL;
        if (!sx_dl_f) { sx_nu_note(" : pas de carte SD"); sx_item_end(2); sx_sub = 2; return; }
        memset(&sx_dl, 0, sizeof(sx_dl));
        sx_dl.maxbytes = 8 * 1024 * 1024; sx_dl.accept = "*/*"; sx_dl.sink = sx_dl_f;
        char url[256];
        snprintf(url, sizeof(url), SX_DL_URL, upd_repo);
        hf_start(&sx_dl, url, NULL);
        sx_t0 = n; sx_mark_gap = au_gap_ev; sx_sub = 1;
        return; }
    case 1: {
        hf_pump(&sx_dl);
        if (sx_dl.st != HF_DONE && sx_dl.st != HF_FAIL && n - sx_t0 < 60000) return;
        u32 ms = (u32)(n - sx_t0), b = sx_dl.len > 0 ? (u32)sx_dl.len : 0, gaps = au_gap_ev - sx_mark_gap;
        int ok = sx_dl.st == HF_DONE && !sx_dl.sink_err;
        sx_nu_close();
        sx_nu_note(" : %u Ko, %u,%u s, %u Ko/s, %u trous%s", (unsigned)(b / 1024u), (unsigned)(ms / 1000u), (unsigned)(ms % 1000u / 100u),
                   (unsigned)(ms ? (u64)b * 1000ull / 1024ull / ms : 0), (unsigned)gaps, ok ? "" : ms >= 60000 ? ", pas fini en 60 s" : ", échec");
        sx_item_end(ok ? (gaps ? 1 : 0) : 1);
        sx_sub = 2;
        return; }
    case 2:                                                  /* une page web (Cube Browser) */
        if (!tab_shown(TAB_WEB)) { sx_item_begin(IP_NET, "Page web (onglet caché)"); sx_item_end(2); sx_sub = 4; return; }
        sx_item_begin(IP_NET, "Page web (Wikipédia, Cube Browser)");
        menu_open = 0; tl_screen = TL_NONE; tab = TAB_WEB; tab_enter(tab);
        cb_navigate(SX_WEB_URL);
        sx_mark_gap = au_gap_ev; sx_t = n + 15000; sx_sub = 3;
        return;
    case 3: {
        u32 gaps = au_gap_ev - sx_mark_gap;
        sx_nu_note(" : 15 s, %u trous du son", (unsigned)gaps);
        sx_item_end(gaps ? 1 : 0);
        tab = TAB_MUSIC; tab_enter(tab);
        sx_sub = 4; sx_t = n + 800;
        return; }
    case 4:                                                  /* une radio en AAC */
        stop_track();
        sx_item_begin(IP_NET, "Radio en AAC (FIP)");
        if (nstations >= MAX_STATIONS) { sx_item_end(2); sx_sub = 7; return; }
        snprintf(stations[nstations].name, sizeof(stations[0].name), "%s", "FIP (test)");   /* (comme une radio du globe : pas ajoutee a la liste) */
        snprintf(stations[nstations].url, sizeof(stations[0].url), "%s", SX_AAC_URL);
        start_station(nstations, 0);
        tab = TAB_RADIO; tab_enter(tab);
        sx_t0 = n; sx_sub = 5;
        return;
    case 5:
        if (src == SRC_RADIO && state == ST_PLAY) { sx_nu_ms = (u32)(n - sx_t0); sx_mark_gap = au_gap_ev; sx_sub = 6; sx_t = n + 10000; return; }
        if (n - sx_t0 > 20000 || state == ST_ERROR || src != SRC_RADIO) { sx_nu_note(" : pas de son"); sx_item_end(1); stop_station(); sx_sub = 7; }
        return;
    case 6: {
        u32 gaps = au_gap_ev - sx_mark_gap;
        sx_nu_note(" : %s, connectée en %u ms, %u trous sur 10 s", stream_codec == 1 ? "AAC" : stream_codec == 2 ? "OGG" : stream_codec == 3 ? "M4A" : "MP3", (unsigned)sx_nu_ms, (unsigned)gaps);
        sx_item_end(gaps ? 1 : 0);
        stop_station();
        sx_sub = 7;
        return; }
    default:
        tab = TAB_MUSIC; tab_enter(tab);
        sx_tour_start(1);
    }
}

/* ---------- 4. parcours (deux passages en test complet), puis stress ---------- */
static void sx_tour_start(int pass) {
    sx_pass = pass;
    sx_ntabs = 0;
    FOR_TABS(t) if (tab_shown(t)) sx_tabs[sx_ntabs++] = t;
    sx_i = -1; sx_sub = 0;
    if (pass == 1) {
        if (!perf_on) perf_toggle();
        sx_perf_reset();                             /* (les chiffres par onglet : seulement les parcours) */
        for (int t = 0; t < TAB_COUNT; t++) mv_tab_seen[t] = 0;
        sx_music = 0;
        if (!((src == SRC_FILE && playing) || (src == SRC_RADIO && state != ST_IDLE)) && nfiles > 0) {   /* un morceau en fond (comme a l'usage) */
            int k = -1;
            for (int i = 0; i < nfiles && k < 0; i++) if (!is_retro[i]) k = i;
            if (k >= 0 && start_track(k)) sx_music = 1;
        }
        sx_gap0 = au_gap; sx_gapev0 = au_gap_ev; sx_tot0 = au_tot;
    }
    sx_sb = pass == 1 ? SB_TOUR1 : SB_TOUR2;
    tl_screen = TL_NONE;
    menu_open = 0;
    sx_state = SX_TOUR;
    sx_t = now_ms();
}
static void sx_finish(void);
static void sx_stress_tick(u64 n) {
    if (n < sx_t) return;
    if (sx_sub < 70) {                               /* onglets tres vite (8 s) */
        menu_open = 0; tl_screen = TL_NONE;
        tab = sx_tabs[(sx_sub * 7 + 3) % (sx_ntabs ? sx_ntabs : 1)]; tab_enter(tab);
        if (sx_sub & 1) tp_keys |= PAD_BUTTON_DOWN;
        sx_sub++; sx_t = n + 115; return;
    }
    if (sx_sub < 82) {                               /* menu START ouvert, ferme */
        tp_keys |= (sx_sub & 1) ? PAD_BUTTON_B : PAD_BUTTON_START;
        sx_sub++; sx_t = n + 350; return;
    }
    if (sx_sub == 82) { menu_open = 0; mem_open(); sx_sub++; sx_t = n + 2000; return; }
    if (sx_sub == 83) { info_open(); sx_sub++; sx_t = n + 2000; return; }
    if (sx_sub == 84) { tl_screen = TL_NONE; menu_open = 0; tab = sx_ntabs ? sx_tabs[0] : TAB_MUSIC; tab_enter(tab); sx_sub++; sx_t = n + 1500; return; }
    sx_mem_end = sx_free_now();
    sx_finish();
}

static void sx_tick(void) {
    if (sx_state == SX_NET) {
        if (!sx_net_pump()) return;
        if (sx_full) sx_sound_start(); else sx_tour_start(1);
        return;
    }
    if (sx_state < SX_SOUND || sx_state > SX_STRESS) return;
    u64 n = now_ms();
    last_input_ms = n;                                     /* (pas d'economiseur pendant le test) */
    if (PAD_ButtonsDown(0) & PAD_BUTTON_B) {
        if (sx_state == SX_SOUND) stop_track();
        if (sx_state == SX_RADIO) stop_station();
        if (sx_state == SX_NETUSE) { sx_nu_close(); if (src == SRC_RADIO) stop_station(); }
        if (sx_state == SX_SCRIPT) { if (sx_ks && sx_k < sx_nk) sx_k_end(&sx_ks[sx_k]); sx_item_end(0); sx_reset_ui(); }
        sx_aborted = 1; sx_finish(); return;
    }
    if (sx_state == SX_SOUND) { sx_sound_tick(n); return; }
    if (sx_state == SX_RADIO) { sx_radio_tick(n); return; }
    if (sx_state == SX_NETUSE) { sx_netuse_tick(n); return; }
    if (sx_state == SX_STRESS) { sx_stress_tick(n); return; }
    if (sx_state == SX_SCRIPT) { sx_script_tick(n); return; }
    if (n < sx_t) return;
    int wait = sx_pass == 1 ? 2500 : 1200, downs = sx_pass == 1 ? 6 : 3;
    if (sx_i < sx_ntabs) {
        if (sx_sub == 0) {                                 /* onglet suivant */
            sx_i++;
            if (sx_i >= sx_ntabs) { sx_sub = 0; sx_t = n; return; }
            menu_open = 0; tl_screen = TL_NONE;
            tab = sx_tabs[sx_i]; tab_enter(tab);
            sx_sub = 1; sx_t = n + wait;                   /* (l'onglet s'ouvre, se charge) */
        } else if (sx_sub <= downs) {                      /* la liste defile */
            tp_keys |= PAD_BUTTON_DOWN;
            sx_sub++; sx_t = n + 220;
        } else { sx_sub = 0; sx_t = n + 700; }
        return;
    }
    /* menu START, retour au premier onglet, memoire libre */
    if (sx_sub == 0) { tp_keys |= PAD_BUTTON_START; sx_sub = 1; sx_t = n + 1200; return; }
    if (sx_sub <= 4) { tp_keys |= PAD_BUTTON_DOWN; sx_sub++; sx_t = n + 300; return; }
    if (sx_sub == 5) { tp_keys |= PAD_BUTTON_B; sx_sub = 6; sx_t = n + 800; return; }
    if (sx_sub == 6) { menu_open = 0; tab = sx_ntabs ? sx_tabs[0] : TAB_MUSIC; tab_enter(tab); sx_sub = 7; sx_t = n + 1500; return; }
    if (sx_pass == 1) sx_mem_p1 = sx_free_now(); else sx_mem_p2 = sx_free_now();
    if (!sx_full) { sx_finish(); return; }
    if (sx_pass == 1) { sx_tour_start(2); return; }
    if (sx_total) { sx_script_build(); sx_k = 0; sx_kph = 0; sx_state = SX_SCRIPT; sx_sb = SB_SCRIPT; sx_t = n; return; }
    sx_state = SX_STRESS; sx_sb = SB_STRESS; sx_sub = 0; sx_t = n;
}

/* ---------- 5. diagnostic, note ---------- */
static void sx_d(int lv, const char *fmt, ...) {
    if (sx_nd >= SX_ND) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(sx_diag[sx_nd], sizeof(sx_diag[0]), fmt, ap);
    va_end(ap);
    sx_diag_lv[sx_nd++] = (u8)lv;
}
static const char *sx_tabn(int t) { return t >= 0 && t < TAB_COUNT ? tab_names[t] : "menu / outils"; }
/* cause d'une image bloquee, en clair */
static void sx_cause(char *o, int cap, const sxs_t *s) {
    if (s->stolen_us * 2 >= s->ms * 1000u) {         /* v265 : pris par quoi ? */
        if (s->dexp >= 4 && s->dma * 2u < s->dexp) snprintf(o, cap, "interruptions coupées (le son lui-même s'est arrêté)");
        else if (s->netk >= 24) snprintf(o, cap, "réseau : %u Ko reçus pendant l'image", (unsigned)s->netk);
        else if (s->sdk >= 32) snprintf(o, cap, "carte SD : %u Ko lus ou écrits pendant l'image", (unsigned)s->sdk);
        else snprintf(o, cap, "un fil plus prioritaire (pile réseau ou autre ; ni SD ni données reçues)");
    }
    else if (s->top == PH_DRAW && s->dsec && s->dsec_us * 2 >= s->top_us) snprintf(o, cap, "dessin : %s", s->dsec);
    else if (s->top == PH_DRAW) snprintf(o, cap, "dessin (réparti)");
    else snprintf(o, cap, "%s", s->top < PH_N ? pfx_names[s->top] : "?");
}
static void sx_diagnose(u32 ok) {
    sx_nd = 0;
    /* blocages : regroupes par cause et onglet, les plus longs d'abord */
    if (sx_nst_all == 0) sx_d(0, "Aucune image bloquée (150 ms et plus) pendant le test.");
    else {
        static char gname[SX_NS][80];
        int cnt[SX_NS], tb[SX_NS], ng = 0; u32 worst[SX_NS]; u16 bg[SX_NS];
        for (int i = 0; i < sx_nst; i++) {
            char c[80]; sx_cause(c, sizeof(c), &sx_st[i]);
            int g = -1;
            for (int j = 0; j < ng; j++) if (tb[j] == sx_st[i].tab && !strcmp(gname[j], c)) g = j;
            if (g < 0) { g = ng++; snprintf(gname[g], sizeof(gname[g]), "%s", c); cnt[g] = 0; tb[g] = sx_st[i].tab; worst[g] = 0; bg[g] = 0; }
            cnt[g]++; bg[g] |= sx_st[i].bg;
            if (sx_st[i].ms > worst[g]) worst[g] = sx_st[i].ms;
        }
        sx_d(sx_st_500 ? 2 : 1, "%u images bloquées (%u de plus d'une demi-seconde). Causes :", (unsigned)sx_nst_all, (unsigned)sx_st_500);
        for (int k = 0; k < 4; k++) {
            int b = -1;
            for (int j = 0; j < ng; j++) if (cnt[j] && (b < 0 || worst[j] > worst[b])) b = j;
            if (b < 0) break;
            char bgt[120]; sx_bg_text(bgt, sizeof(bgt), bg[b]);
            sx_d(worst[b] >= 500 ? 2 : 1, " - %s : %s, %d fois, jusqu'à %u ms%s%s", sx_tabn(tb[b]), gname[b], cnt[b], (unsigned)worst[b], bgt[0] ? " ; en fond : " : "", bgt);
            cnt[b] = 0;
        }
    }
    if (sx_gapev) sx_d(sx_gapev >= 4 ? 2 : 1, "Son : %u trous pendant le parcours (le décodage n'a pas suivi).", (unsigned)sx_gapev);
    if (sx_full) {
        for (int f = 0; f < SX_NF; f++) if (sx_fmt_k[f] >= 0 && sx_fmt_ok[f] && sx_fmt_cpu[f] >= 600)
            sx_d(sx_fmt_cpu[f] >= 850 ? 2 : 1, "Le %s prend %u %% du processeur à décoder : peu de marge.", sx_fmt_name[f], (unsigned)(sx_fmt_cpu[f] / 10));
        for (int f = 0; f < SX_NF; f++) if (sx_fmt_k[f] >= 0 && sx_fmt_ok[f] && sx_fmt_gap[f])
            sx_d(1, "Le %s a eu %u trous dans le son en 6 s.", sx_fmt_name[f], (unsigned)sx_fmt_gap[f]);
        if (sx_ram_err) sx_d(2, "MÉMOIRE PRINCIPALE : %u erreurs de lecture ! (console à faire vérifier)", (unsigned)sx_ram_err);
        if (sx_aram == 1 && sx_aram_err) sx_d(2, "ARAM : %u erreurs de lecture ! (mémoire du son)", (unsigned)sx_aram_err);
        if (sx_mem_p1 && sx_mem_p2 && sx_mem_p1 > sx_mem_p2 + 256 * 1024)
            sx_d(1, "Mémoire perdue entre les deux parcours : %u Ko (fuite possible).", (unsigned)((sx_mem_p1 - sx_mem_p2) >> 10));
        else if (sx_mem_p1 && sx_mem_p2) sx_d(0, "Pas de fuite de mémoire entre les deux parcours.");
        for (int p = 0; p < 4; p++) if (sx_pad_mask & (1u << p)) {
            int d = sx_pad_dev[p][0] > sx_pad_dev[p][1] ? sx_pad_dev[p][0] : sx_pad_dev[p][1];
            int c = sx_pad_dev[p][2] > sx_pad_dev[p][3] ? sx_pad_dev[p][2] : sx_pad_dev[p][3];
            if (d >= 15 || c >= 15) sx_d(1, "Manette %d : stick %s décentré au repos (%d) : dérive possible.", p + 1, d >= c ? "principal" : "C", d >= c ? d : c);
        }
        if (sx_radio == 2) sx_d(1, "La radio n'a pas démarré en 20 s.");
        else if (sx_radio == 4) sx_d(1, "La radio n'a pas pu se connecter (station injoignable ?).");
        else if (sx_radio == 1 && sx_radio_ms > 8000) sx_d(1, "La radio met %u,%u s à démarrer.", (unsigned)(sx_radio_ms / 1000), (unsigned)(sx_radio_ms % 1000 / 100));
        if (sx_dns_ms > 800) sx_d(1, "La box répond lentement pour trouver les sites (%d ms).", sx_dns_ms);
        if (sx_net_st[1] == 1 && sx_net_kbs[1] && sx_net_kbs[1] < 40) sx_d(1, "Débit Internet faible : %u Ko/s.", (unsigned)sx_net_kbs[1]);
        if (sx_net_st[0] == 1 && sx_net_st[2] == 1 && sx_net_ms[0] > sx_net_ms[2] + 1500)
            sx_d(0, "Ouvrir une connexion chiffrée coûte environ %u,%u s (normal sur GameCube).", (unsigned)((sx_net_ms[0] - sx_net_ms[2]) / 1000), (unsigned)((sx_net_ms[0] - sx_net_ms[2]) % 1000 / 100));
    }
    if (sx_total) {
        for (int k = 0; k < 2; k++) {
            const sxc_t *c = &sx_card[k];
            if (c->st == 2) sx_d(1, "Carte mémoire %c : illisible (%s).", 'A' + k, mc_errtxt(c->err));
            else if (c->wr == 2) sx_d(2, "Carte mémoire %c : l'écriture de test a échoué (%u octets faux) : carte à surveiller.", 'A' + k, (unsigned)c->verr);
            else if (c->wr == 1) sx_d(0, "Carte mémoire %c : écriture, relecture et effacement ok.", 'A' + k);
        }
        int np = 0, nmem = 0;
        for (int i = 0; i < sx_nit; i++) { if (sx_it[i].st == 1) np++; if (sx_it[i].dmem > 300 * 1024) nmem++; }
        if (!np) sx_d(0, "GC Station : les %d éléments essayés fonctionnent sans blocage.", sx_nit);
        else {
            sx_d(np > 5 ? 2 : 1, "GC Station : %d éléments sur %d à regarder :", np, sx_nit);
            for (int k = 0; k < 4; k++) {                 /* (les pires d'abord) */
                int b = -1;
                for (int i = 0; i < sx_nit; i++) if (sx_it[i].st == 1 && (b < 0 || sx_it[i].worst_us > sx_it[b].worst_us)) b = i;
                if (b < 0) break;
                sx_d(sx_it[b].worst_us >= 1000000u ? 2 : 1, " - %s : %s, pire image %u ms", sx_ip_name[sx_it[b].ph], sx_it[b].nm, (unsigned)(sx_it[b].worst_us / 1000u));
                sx_it[b].st = 3;                          /* (deja cite ; remis a 1 juste apres) */
            }
            for (int i = 0; i < sx_nit; i++) if (sx_it[i].st == 3) sx_it[i].st = 1;
        }
        if (nmem) sx_d(1, "%d éléments gardent plus de 300 Ko de mémoire après leur fermeture (voir le rapport).", nmem);
    }
    if (sx_clk_ok && (sx_clk_diff - sx_clk_tz > 120 || sx_clk_diff - sx_clk_tz < -120))
        sx_d(1, "L'horloge de la console a %d min d'écart avec Internet : à remettre à l'heure.", (int)((sx_clk_diff - sx_clk_tz) / 60));
    if (sx_cfg0) { if (sx_cfg_nd) sx_d(1, "Réglages : %d lignes ne sont pas revenues toutes seules (remises ; fichier intact).", sx_cfg_nd); else sx_d(0, "Réglages : tous revenus exactement comme avant."); }
    if (sd_ok && !sd_virtual && sx_rkbs && sx_rkbs < 600) sx_d(1, "Carte SD lente en lecture (%u Ko/s) : pochettes et listes chargent lentement.", (unsigned)sx_rkbs);
    if (sx_big0 && sx_big0 < (4u << 20)) sx_d(1, "Peu de mémoire libre d'un seul bloc (%u Ko).", (unsigned)(sx_big0 >> 10));
    /* note sur 100 */
    int pts = 0, max = 0;
    max += 35; pts += ok >= 990 ? 35 : ok > 800 ? (int)((ok - 800) * 35 / 190) : 0;
    if (sx_played) { max += 20; pts += sx_gapev >= 4 ? 0 : 20 - 5 * (int)sx_gapev; }
    max += 15; { int s = 15 - 3 * (int)sx_st_500 - (int)((sx_nst_all - sx_st_500) / 3); pts += s > 0 ? s : 0; }
    if (sd_ok && !sd_virtual) { max += 10; pts += sx_rkbs >= 1000 ? 10 : (int)(sx_rkbs / 100); }
    if (sx_full) {
        max += 10; pts += sx_ram_err || (sx_aram == 1 && sx_aram_err) ? 0 : (sx_mem_p1 && sx_mem_p1 > sx_mem_p2 + 256 * 1024 ? 5 : 10);
        if (sx_net_st[0] != 3) { max += 10; pts += sx_net_st[0] == 1 ? (sx_net_ms[0] < 3000 ? 10 : sx_net_ms[0] < 6000 ? 7 : 4) : 0; }
    }
    if (sx_total) {
        int np = 0; for (int i = 0; i < sx_nit; i++) if (sx_it[i].st == 1) np++;
        max += 15; pts += np >= 7 ? 0 : 15 - 2 * np;
        int cbad = 0; for (int k = 0; k < 2; k++) if (sx_card[k].wr == 2) cbad = 1;
        if (cbad) pts -= 5;
    }
    sx_score = max ? pts * 100 / max : 0;
    if (sx_score < 0) sx_score = 0;
    if (sx_score >= 90 && sx_nd <= 2) sx_d(0, "Rien d'inquiétant : la console tient bien la charge.");
}

/* ---------- 6. rapport ---------- */
static void sx_finish(void) {
    perf_on = 0;
    sx_sb = -1;
    sx_item_end(0);
    sx_gap = au_gap - sx_gap0; sx_gapev = au_gap_ev - sx_gapev0; sx_played = (au_tot - sx_tot0) / 48000u;
    sx_reset_ui();
    if (pv_mode == 2) video_stop();
    pv_mode = 0; pv_slide = 0;
    if (sx_music && src == SRC_FILE) stop_track();
    sx_music = 0;
    menu_open = 0;
    sx_cfg_restore(1);                                   /* v263 : les reglages d'avant (compares) */
    sx_quiet = 0;
    { u32 dt = perf_us() - (u32)sx_vi_t0, nr = retrace_count - sx_vi_r0;   /* frequence de l'ecran, mesuree */
      sx_vi_x100 = dt > 2000000u ? (u32)((u64)nr * 100000000ull / dt) : 0; }
    tab = sx_tab0; tab_enter(tab);
    sx_frames = sx_slow = sx_worst = 0; sx_worst_tab = -1;
    for (int t = 0; t < PF_TABS; t++) {
        sx_frames += pfx_frames[t]; sx_slow += pfx_slow[t];
        if (pfx_frames[t] && pfx_worst[t] > sx_worst) { sx_worst = pfx_worst[t]; sx_worst_tab = t; }
    }
    u32 ok = sx_frames ? (sx_frames - sx_slow) * 1000u / sx_frames : 0;
    /* le test d'avant (pour comparer) */
    sx_prev[0] = 0;
    FILE *pf = sd_ok ? fopen(SX_FILE, "r") : NULL;
    if (pf) {
        char l[300];
        while (fgets(l, sizeof(l), pf)) if (!strncmp(l, "#mesures ", 9)) { snprintf(sx_prev, sizeof(sx_prev), "%s", l + 9); sx_prev[strcspn(sx_prev, "\r\n")] = 0; break; }
        fclose(pf);
        remove(SX_OLD);
        rename(SX_FILE, SX_OLD);
    }
    sx_diagnose(ok);
    free(sx_rep);
    int cap = 96000, n = 0;
    sx_rep = (char *)malloc(cap);
    if (!sx_rep) { sx_state = SX_DONE; tl_screen = TL_SELFTEST; return; }
    #define SXP(...) do { if (n < cap - 1) { int w_ = snprintf(sx_rep + n, cap - n, __VA_ARGS__); n += w_ > 0 ? w_ : 0; if (n > cap - 1) n = cap - 1; } } while (0)
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char a[48], b[48], c[160];
    SXP("#mesures cpu=%u int=%u cpy=%u sdr=%u sdw=%u stat=%u net=%u ok=%u gaps=%u note=%d bloc=%u ram=%u aram=%u gpu=%u rnd=%u dns=%d radio=%u\n",
        (unsigned)sx_cpu_ms, (unsigned)sx_int_ms, (unsigned)sx_cpy_mbs, (unsigned)sx_rkbs, (unsigned)sx_wkbs, (unsigned)sx_stat_x10,
        (unsigned)sx_net_kbs[0], (unsigned)ok, (unsigned)sx_gapev, sx_score, (unsigned)sx_nst_all, (unsigned)sx_ram_r, (unsigned)sx_aram_mbs,
        (unsigned)sx_gpu_mpx, (unsigned)sx_rnd_x10, sx_dns_ms, (unsigned)sx_radio_ms);
    SXP("GC Station %s (build %d) - test %s de la console, %02d/%02d/%04d %02d:%02d%s\n", GCMP3_VERSION, GCMP3_BUILD, sx_total ? "total" : sx_full ? "complet" : "rapide",
        tmv ? tmv->tm_mday : 0, tmv ? tmv->tm_mon + 1 : 0, tmv ? tmv->tm_year + 1900 : 0, tmv ? tmv->tm_hour : 0, tmv ? tmv->tm_min : 0,
        sx_aborted ? " (arrêté avant la fin)" : "");
    SXP("Écran %d Hz ; une image ratée = plus de %u ms\n", VID_HZ, (unsigned)(VID_SLOW_US / 1000u));
    SXP("\nNOTE : %d / 100", sx_score);
    if (sx_prev_val("note=")) SXP("   (test d'avant : %u)", (unsigned)sx_prev_val("note="));
    SXP("\n\n== Diagnostic ==\n");
    for (int i = 0; i < sx_nd; i++) SXP("%s %s\n", sx_diag_lv[i] == 2 ? "[!!]" : sx_diag_lv[i] == 1 ? "[! ]" : "[ok]", sx_diag[i]);
    SXP("\n== Processeur ==\ncalcul (virgule flottante) : %u ms   calcul (entiers) : %u ms", (unsigned)sx_cpu_ms, (unsigned)sx_int_ms);
    if (sx_full) SXP("   double précision : %u ms", (unsigned)sx_dbl_ms);
    SXP("   copie mémoire : %u Mo/s\n", (unsigned)sx_cpy_mbs);
    sx_mo(a, sizeof(a), sx_free0); sx_mo(b, sizeof(b), sx_big0);
    SXP("\n== Mémoire ==\nlibre au début : %s   plus grand bloc : %s\n", a, b);
    if (sx_full) {
        if (sx_ram_kb) SXP("mémoire principale : %u Ko vérifiés deux fois, %u erreurs ; écriture %u Mo/s, lecture %u Mo/s, accès au hasard %u ns\n",
                           (unsigned)sx_ram_kb, (unsigned)sx_ram_err, (unsigned)sx_ram_w, (unsigned)sx_ram_r, (unsigned)sx_ram_ns);
        if (sx_aram == 1) SXP("ARAM : %u Ko vérifiés, %u erreurs, %u Mo/s (contenu remis)\n", (unsigned)sx_aram_kb, (unsigned)sx_aram_err, (unsigned)sx_aram_mbs);
        else if (sx_aram == 2) SXP("ARAM : pas sur le simulateur\n");
        if (sx_mem_p1) {
            sx_mo(a, sizeof(a), sx_mem_p1); sx_mo(b, sizeof(b), sx_mem_p2);
            SXP("libre après le parcours 1 : %s, après le parcours 2 : %s", a, sx_mem_p2 ? b : "-");
            if (sx_mem_end) { sx_mo(c, sizeof(c), sx_mem_end); SXP(", après le stress : %s", c); }
            SXP("\n");
        }
    }
    for (int t = 0; t < TAB_COUNT; t++) if (mv_tab_seen[t]) { sx_pad(c, sizeof(c), tab_names[t], 16); sx_mo(a, sizeof(a), mv_tab_min[t]); SXP("   %s le moins vu : %s\n", c, a); }
    if (sx_full && sx_gpu_mpx) SXP("\n== Processeur graphique ==\nremplissage : %u millions de points/s   petits triangles : %u milliers/s\n", (unsigned)sx_gpu_mpx, (unsigned)sx_gpu_tri);
    SXP("\n== Carte SD ==\n");
    if (sd_ok && !sd_virtual) {
        SXP("lecture : %u Ko/s   écriture : %u Ko/s   recherche d'un fichier : %u,%u ms\n", (unsigned)sx_rkbs, (unsigned)sx_wkbs, (unsigned)(sx_stat_x10 / 10), (unsigned)(sx_stat_x10 % 10));
        if (sx_full) {
            SXP("lecture de 4 Ko au hasard : %u,%u ms   dossier de la musique : %u fichiers en %u ms\n", (unsigned)(sx_rnd_x10 / 10), (unsigned)(sx_rnd_x10 % 10), (unsigned)sx_dir_n, (unsigned)sx_dir_ms);
            if (sx_sd_tot) SXP("place : %u Mo libres sur %u Mo\n", (unsigned)sx_sd_free, (unsigned)sx_sd_tot);
        }
    } else SXP("pas de carte SD\n");
    SXP("\n== Réseau ==\n");
    if (sx_full) { if (sx_dns_ms >= 0) SXP("nom d'un site (DNS, par la box) : %d ms\n", sx_dns_ms); else if (sx_dns_ms == -2) SXP("nom d'un site (DNS) : pas de réponse de la box\n"); }
    for (int i = 0; i < sx_nn; i++) {
        if (sx_net_st[i] == 3) { SXP("pas connecté\n"); break; }
        if (sx_net_st[i] == 1 || sx_net_st[i] == 4)
            SXP("%s : %u ms (réponse après %u ms), %u Ko, %u Ko/s%s\n", sx_net_name[i], (unsigned)sx_net_ms[i], (unsigned)sx_net_head[i], (unsigned)(sx_net_bytes[i] >> 10), (unsigned)sx_net_kbs[i], sx_net_st[i] == 4 ? " (trop lent : arrêté)" : "");
        else SXP("%s : échec\n", sx_net_name[i]);
    }
    if (sx_full) {
        SXP("\n== Manettes (au repos) ==\n");
        int any = 0;
        for (int p = 0; p < 4; p++) if (sx_pad_mask & (1u << p)) {
            any = 1;
            const char *ty = sx_pad_type[p] == SI_GC_WAVEBIRD ? "WaveBird" : sx_pad_type[p] == SI_GC_KEYBOARD ? "clavier" : "manette";
            SXP("port %d (%s) : écart du stick %u/%u, stick C %u/%u, gâchettes %u\n", p + 1, ty, sx_pad_dev[p][0], sx_pad_dev[p][1], sx_pad_dev[p][2], sx_pad_dev[p][3], sx_pad_trg[p]);
        }
        if (!any) SXP("aucune\n");
        SXP("\n== Formats audio (6 s chacun) ==\n");
        if (!sx_fmt_n) SXP("aucune musique sur la carte SD\n");
        for (int f = 0; f < SX_NF; f++) {
            if (sx_fmt_k[f] < 0) continue;
            sx_pad(c, sizeof(c), sx_fmt_name[f], 24);
            if (!sx_fmt_ok[f]) { SXP("%s illisible (%s)\n", c, base_of(names[sx_fmt_k[f]])); continue; }
            SXP("%s décodage %u,%u %% du processeur, %u trous, %u images ratées  (%s)\n", c, (unsigned)(sx_fmt_cpu[f] / 10), (unsigned)(sx_fmt_cpu[f] % 10),
                (unsigned)sx_fmt_gap[f], (unsigned)sx_fmt_slow[f], base_of(names[sx_fmt_k[f]]));
        }
        SXP("\n== Radio ==\n");
        if (sx_radio == 1) SXP("connexion en %u ms, %u trous sur 10 s\n", (unsigned)sx_radio_ms, (unsigned)sx_radio_gap);
        else SXP("%s\n", sx_radio == 2 ? "pas de son au bout de 20 s" : sx_radio == 4 ? "connexion impossible" : "pas testée (pas de réseau ou pas de station)");
    }
    SXP("\n== Carte SD : adaptateur ==\n%s\n", sd_virtual ? "aucune (réglages sur la carte mémoire)" : !sd_ok ? "aucune" : sd_slot == 0 ? "SD2SP2 (port série 2)" : sd_slot == 1 ? "SD Gecko, slot A" : sd_slot == 2 ? "SD Gecko, slot B" : "?");
    if (sx_sm_n) SXP("petits fichiers (4 Ko) : création %u,%u ms, effacement %u,%u ms chacun (%u fichiers)\n", (unsigned)(sx_sm_cr_x10 / 10), (unsigned)(sx_sm_cr_x10 % 10), (unsigned)(sx_sm_rm_x10 / 10), (unsigned)(sx_sm_rm_x10 % 10), (unsigned)sx_sm_n);
    if (sx_total) {
        SXP("\n== Cartes mémoire ==\n");
        for (int k = 0; k < 2; k++) {
            const sxc_t *c = &sx_card[k];
            SXP("slot %c : ", 'A' + k);
            if (c->st == 0) { SXP("vide\n"); continue; }
            if (c->st == 3) { SXP("adaptateur SD (pas touché)\n"); continue; }
            if (c->st == 2) { SXP("erreur %d : %s\n", c->err, mc_errtxt(c->err)); continue; }
            SXP("%u blocs (%u Mbit), %u libres, %d sauvegardes, secteur %u Ko", (unsigned)(c->mbit * 16u - 5u), (unsigned)c->mbit, (unsigned)c->freeb, c->files, (unsigned)(c->sect >> 10));
            if (c->rd_kbs) SXP(", lecture %u Ko/s", (unsigned)c->rd_kbs);
            SXP("\n   écriture d'un bloc de test : %s", c->wr == 1 ? "ok" : c->wr == 2 ? "ERREUR" : c->wr == 3 ? "pas de place (pas testé)" : "pas testée");
            if (c->wr == 1 || c->wr == 2) SXP(" (écrit en %u ms, relu en %u ms, %u octets faux, effacé ensuite)", (unsigned)c->wr_ms, (unsigned)c->rd_ms, (unsigned)c->verr);
            SXP("\n");
        }
    }
    SXP("\n== Horloge ==\n");
    { time_t t_ = time(NULL); struct tm *m_ = localtime(&t_);
      SXP("console : %02d/%02d/%04d %02d:%02d:%02d\n", m_ ? m_->tm_mday : 0, m_ ? m_->tm_mon + 1 : 0, m_ ? m_->tm_year + 1900 : 0, m_ ? m_->tm_hour : 0, m_ ? m_->tm_min : 0, m_ ? m_->tm_sec : 0); }
    if (sx_clk_ok) {
        s32 dr = sx_clk_diff - sx_clk_tz;
        SXP("par rapport à Internet : fuseau %+d h %02d, écart %+d s%s\n", (int)(sx_clk_tz / 3600), (int)((sx_clk_tz < 0 ? -sx_clk_tz : sx_clk_tz) % 3600 / 60), (int)dr, dr > 120 || dr < -120 ? " (l'horloge dérive : à remettre à l'heure)" : "");
    } else SXP("heure d'Internet : pas lue\n");
    SXP("\n== Image et son ==\n");
    { static const char *const vn[VID_COUNT] = { "auto", "480i", "480p", "576i", "576p" };
      SXP("mode %s, %d Hz, câble %s", vn[vid_active >= 0 && vid_active < VID_COUNT ? vid_active : 0], VID_HZ, VIDEO_HaveComponentCable() ? "composante (YUV)" : "composite / S-Vidéo");
      if (sx_vi_x100) SXP(", mesuré %u,%02u images/s", (unsigned)(sx_vi_x100 / 100), (unsigned)(sx_vi_x100 % 100));
      SXP("\n"); }
    if (sx_ac_ms > 3000) SXP("horloge du son : %u échantillons/s joués (attendu 48000)\n", (unsigned)((u64)sx_ac_samples * 1000ull / sx_ac_ms));
    if (sx_cover >= 0) SXP("couvercle du lecteur : %s\n", sx_cover ? "ouvert" : "fermé");
    SXP("serveur du téléphone : %s%s%s\n", tp_state == PS_ON ? "en marche (http://" : tp_state == PS_ERR ? "en erreur" : tp_state == PS_START ? "démarre" : "arrêté", tp_state == PS_ON ? ip_str : "", tp_state == PS_ON ? ")" : "");
    if (sx_total && sx_it) {
        SXP("\n== GC Station : tout essayé (%d éléments) ==\n", sx_nit);
        for (int ph = 0; ph < IP_N; ph++) {
            int c0 = 0, c1 = 0, c2 = 0;
            for (int i = 0; i < sx_nit; i++) if (sx_it[i].ph == ph) { if (sx_it[i].st == 1) c1++; else if (sx_it[i].st == 2) c2++; else c0++; }
            if (!c0 && !c1 && !c2) continue;
            SXP("-- %s : %d ok, %d à regarder, %d pas disponibles\n", sx_ip_name[ph], c0, c1, c2);
            for (int i = 0; i < sx_nit; i++) {
                const sxi_t *it = &sx_it[i];
                if (it->ph != ph) continue;
                char nm[128]; sx_pad(nm, sizeof(nm), it->nm, 44);
                SXP("%s %s pire %4u ms, %3u ratées / %4u", it->st == 1 ? "[! ]" : it->st == 2 ? "[--]" : "[ok]", nm, (unsigned)(it->worst_us / 1000u), (unsigned)it->slow, (unsigned)it->frames);
                if (it->dmem > 64 * 1024 || it->dmem < -64 * 1024) SXP(", mémoire %+d Ko", (int)(it->dmem / 1024));
                SXP("\n");
            }
        }
    }
    if (sx_cfg0) {
        SXP("\n== Réglages ==\n");
        if (!sx_cfg_nd) SXP("tous revenus exactement comme avant le test (rien n'a été enregistré pendant le test)\n");
        else { SXP("remis, mais ces lignes diffèrent encore de gc-mp3.txt (le fichier, lui, n'a pas été touché) :\n"); for (int i = 0; i < sx_cfg_nd; i++) SXP("   %s\n", sx_cfg_diff[i]); }
    }
    SXP("\n== Son (parcours) ==\n%u trous (%u ms) sur %u s joués%s\n", (unsigned)sx_gapev, (unsigned)(sx_gap / 48), (unsigned)sx_played, sx_played ? "" : " (pas de musique sur la carte SD)");
    if (sx_full) {
        SXP("\n== Images par étape ==\n");
        for (int i = 0; i < SB_N; i++) if (sx_sb_frames[i]) {
            sx_pad(c, sizeof(c), sx_sb_name[i], 14);
            SXP("%s %5u images, %3u ratées (%u%%), pire %u ms\n", c, (unsigned)sx_sb_frames[i], (unsigned)sx_sb_slow[i], (unsigned)(sx_sb_slow[i] * 100u / sx_sb_frames[i]), (unsigned)(sx_sb_worst[i] / 1000u));
        }
    }
    SXP("\n== Fluidité (parcours%s) ==\n%u images, %u ratées (%u,%u %% à l'heure)", sx_full ? " et stress" : "", (unsigned)sx_frames, (unsigned)sx_slow, (unsigned)(ok / 10), (unsigned)(ok % 10));
    if (sx_worst_tab >= 0) SXP(", pire %u ms (%s)", (unsigned)(sx_worst / 1000u), sx_worst_tab < TAB_COUNT ? tab_names[sx_worst_tab] : "menu");
    SXP("\n");
    for (int t = 0; t < PF_TABS; t++) {
        if (!pfx_frames[t]) continue;
        SXP("[%s] %u images, %u ratées (%u%%), %u très lentes, pire %u ms\n", t < TAB_COUNT ? tab_names[t] : "menu / autre",
            (unsigned)pfx_frames[t], (unsigned)pfx_slow[t], (unsigned)(pfx_slow[t] * 100u / pfx_frames[t]), (unsigned)pfx_vslow[t], (unsigned)(pfx_worst[t] / 1000u));
        for (int i = 0; i < PH_N; i++) {
            u32 avg = (u32)(pfx_sum[t][i] / pfx_frames[t]);
            if (avg < 300 && pfx_max[t][i] < 8000 && !pfx_top[t][i]) continue;
            sx_pad(c, sizeof(c), pfx_names[i], 25);
            SXP("   %s moyenne %4u,%u ms  pire %5u ms  cause de %u images ratées\n", c, (unsigned)(avg / 1000), (unsigned)(avg % 1000 / 100), (unsigned)(pfx_max[t][i] / 1000u), (unsigned)pfx_top[t][i]);
        }
    }
    if (sx_nst) {
        SXP("\n== Images bloquées (150 ms et plus : %u ; les %d plus longues) ==\n", (unsigned)sx_nst_all, sx_nst);
        for (int k = 0; k < sx_nst; k++) {                 /* (de la plus longue a la plus courte) */
            int b2 = k;
            for (int j = k + 1; j < sx_nst; j++) if (sx_st[j].ms > sx_st[b2].ms) b2 = j;
            sxs_t t_ = sx_st[k]; sx_st[k] = sx_st[b2]; sx_st[b2] = t_;
            const sxs_t *s = &sx_st[k];
            char cs[80], bgt[120], sb[40]; sx_cause(cs, sizeof(cs), s); sx_bg_text(bgt, sizeof(bgt), s->bg);
            sx_pad(sb, sizeof(sb), sx_sb_name[s->sb < SB_N ? s->sb : 0], 14);
            SXP("%5u ms  %s %s : %s", (unsigned)s->ms, sb, sx_tabn(s->tab), cs);
            if (s->stolen_us >= 20000) SXP(" ; processeur pris %u ms", (unsigned)(s->stolen_us / 1000u));
            SXP(" ; son %u/%u blocs, SD %u Ko, réseau %u Ko", (unsigned)s->dma, (unsigned)s->dexp, (unsigned)s->sdk, (unsigned)s->netk);   /* v265 */
            if (s->dsec && s->dsec_us >= 20000) SXP(" ; dessin le plus long : %s %u ms", s->dsec, (unsigned)(s->dsec_us / 1000u));
            if (bgt[0]) SXP(" ; en fond : %s", bgt);
            SXP("\n");
        }
    }
    if (mv_bt_total) {
        SXP("\n== Dernier démarrage : %u ms ==\n", (unsigned)mv_bt_total);
        for (int i = 0; i < mv_bt_k; i++) SXP("   %-24s %5u ms\n", mv_bt_n[i], (unsigned)mv_bt_ms[i]);
    }
    if (sx_prev[0]) SXP("\n(test d'avant : %s)", sx_prev);
    #undef SXP
    FILE *f = sd_ok && !sd_virtual ? fopen(SX_FILE, "w") : NULL;
    if (f) { fwrite(sx_rep, 1, n, f); fclose(f); }
    crumb("test de la console : note %d, %u%% a l'heure, %u blocages, %u trous", sx_score, (unsigned)(ok / 10), (unsigned)sx_nst_all, (unsigned)sx_gapev);
    sx_page = 0;
    sx_state = SX_DONE;
    tl_screen = TL_SELFTEST;
}

static void sx_begin(int lvl) {                      /* 0 rapide, 1 complet, 2 total */
    int full = lvl >= 1;
    sx_full = full; sx_total = lvl >= 2;
    free(sx_cfg0); sx_cfg0 = NULL; sx_cfg0_n = 0; sx_cfg_nd = 0;
    if (!cfgw_build(&sx_cfg0, &sx_cfg0_n)) { sx_cfg0 = NULL; sx_cfg0_n = 0; }   /* (reglages d'avant : remis a la fin) */
    sx_quiet = 1;
    free(sx_it); sx_it = sx_total ? (sxi_t *)calloc(SX_NI, sizeof(sxi_t)) : NULL; sx_nit = 0; sx_ci = -1;
    memset(sx_card, 0, sizeof(sx_card)); sx_sm_n = 0; sx_clk_ok = 0; sx_ac_samples = sx_ac_ms = 0; sx_vi_x100 = 0;
    sx_hw_info();
    sx_state = SX_BENCH; sx_bstep = 0; sx_bsub = 0; sx_t = now_ms();
    sx_tab0 = tab; sx_aborted = 0; sx_music = 0; sx_pass = 0; sx_sb = -1;
    sx_nst = 0; sx_nst_all = sx_st_500 = 0; sx_score = -1; sx_nd = 0;
    memset(sx_sb_frames, 0, sizeof(sx_sb_frames)); memset(sx_sb_slow, 0, sizeof(sx_sb_slow)); memset(sx_sb_worst, 0, sizeof(sx_sb_worst));
    sx_mem_p1 = sx_mem_p2 = sx_mem_end = 0;
    sx_ram_kb = 0; sx_aram = 0; sx_gpu_mpx = sx_gpu_tri = 0; sx_dns_ms = -1; sx_radio = 0; sx_fmt_n = 0; sx_pad_mask = 0;
    for (int f = 0; f < SX_NF; f++) sx_fmt_k[f] = -1;
    sx_dbl_ms = 0; sx_sd_tot = sx_sd_free = sx_rnd_x10 = sx_dir_n = sx_dir_ms = 0;
    sx_ni = 0; sx_nn = full ? SX_NN : 1;
    memset(sx_net_st, 0, sizeof(sx_net_st));
    sx_gap0 = au_gap; sx_gapev0 = au_gap_ev; sx_tot0 = au_tot;
}

static void sx_input(u32 down, u32 nav, int fire) {
    (void)nav; (void)fire;
    if (sx_state == SX_INTRO || sx_state == SX_DONE) {
        int np = sx_total ? 4 : sx_full ? 3 : 2;
        if (sx_state == SX_DONE && (down & PAD_BUTTON_A)) { sx_state = SX_INTRO; return; }   /* (choisir un autre test) */
        if (down & PAD_BUTTON_A) { sx_begin(2); return; }
        if (sx_state == SX_INTRO && (down & PAD_BUTTON_X)) { sx_begin(1); return; }
        if (down & PAD_BUTTON_Y) { sx_begin(0); return; }
        if (sx_state == SX_DONE && (down & (PAD_BUTTON_X | PAD_BUTTON_RIGHT | PAD_TRIGGER_R))) { sx_page = (sx_page + 1) % np; return; }
        if (sx_state == SX_DONE && (down & (PAD_BUTTON_LEFT | PAD_TRIGGER_L))) { sx_page = (sx_page + np - 1) % np; return; }
        if (down & PAD_BUTTON_B) { tl_screen = TL_NONE; menu_open = 1; }
    }
}

/* une ligne du resume : nom, valeur, comparaison avec le test d'avant */
static f32 sx_line(f32 y, const char *k, const char *v, const char *cmp, col_t c) {   /* (rend la ligne suivante) */
    gx_text1(F_MED, 64, y, k, RGBA(200, 190, 240, 255));
    char fv[200]; fit_text(F_MED, v, 380, fv, sizeof(fv));
    gx_text1(F_MED, 200, y, fv, c);
    if (cmp && cmp[0]) { gx_text1(F_SMALL, 200, y + 15, cmp, RGBA(150, 140, 190, 255)); return y + 35; }
    return y + 23;
}
static void sx_cmp(char *o, int cap, u32 now, u32 before, int lower_better, const char *unit) {
    if (!before) { o[0] = 0; return; }
    int better = lower_better ? now < before * 95 / 100 : now > before * 105 / 100, worse = lower_better ? now > before * 105 / 100 : now < before * 95 / 100;
    snprintf(o, cap, "avant %u%s %s", (unsigned)before, unit, better ? "(mieux)" : worse ? "(moins bien)" : "(pareil)");
}
#define SX_OK RGBA(140, 255, 170, 255)
#define SX_MID RGBA(255, 220, 120, 255)
#define SX_BAD RGBA(255, 140, 140, 255)
#define SX_TXT RGBA(220, 212, 250, 255)

static void sx_page_summary(void) {
    char v[160], c[96];
    u32 ok = sx_frames ? (sx_frames - sx_slow) * 1000u / sx_frames : 0;
    f32 y = 98;
    snprintf(v, sizeof(v), "%d / 100", sx_score);
    sx_cmp(c, sizeof(c), (u32)(sx_score > 0 ? sx_score : 0), sx_prev_val("note="), 0, "");
    y = sx_line(y, "Note", v, c, sx_score >= 85 ? SX_OK : sx_score >= 60 ? SX_MID : SX_BAD);
    snprintf(v, sizeof(v), "%u,%u %% des images à l'heure", (unsigned)(ok / 10), (unsigned)(ok % 10));
    sx_cmp(c, sizeof(c), ok / 10, sx_prev_val("ok=") / 10, 0, " %");
    y = sx_line(y, "Fluidité", v, c, ok >= 950 ? SX_OK : ok >= 850 ? SX_MID : SX_BAD);
    snprintf(v, sizeof(v), "%u images bloquées (%u > 0,5 s)", (unsigned)sx_nst_all, (unsigned)sx_st_500);
    y = sx_line(y, "", v, "", sx_st_500 ? SX_BAD : sx_nst_all ? SX_MID : SX_OK);
    snprintf(v, sizeof(v), "%u trous (%u ms) sur %u s", (unsigned)sx_gapev, (unsigned)(sx_gap / 48), (unsigned)sx_played);
    y = sx_line(y, "Son", sx_played ? v : "pas de musique testée", "", sx_gapev == 0 ? SX_OK : sx_gapev < 4 ? SX_MID : SX_BAD);
    snprintf(v, sizeof(v), "lecture %u Ko/s, écriture %u Ko/s", (unsigned)sx_rkbs, (unsigned)sx_wkbs);
    sx_cmp(c, sizeof(c), sx_rkbs, sx_prev_val("sdr="), 0, " Ko/s");
    y = sx_line(y, "Carte SD", sd_ok && !sd_virtual ? v : "absente", c, sx_rkbs >= 800 || !sd_ok ? SX_TXT : SX_MID);
    char m1[24], m2[24];
    sx_mo(m1, sizeof(m1), sx_free0); sx_mo(m2, sizeof(m2), sx_big0);
    snprintf(v, sizeof(v), "%s libres (bloc %s)", m1, m2);
    if (sx_full && sx_mem_p1 && sx_mem_p2) {
        size_t l = strlen(v);
        if (sx_mem_p1 > sx_mem_p2 + 256 * 1024) snprintf(v + l, sizeof(v) - l, ", fuite %u Ko", (unsigned)((sx_mem_p1 - sx_mem_p2) >> 10));
        else snprintf(v + l, sizeof(v) - l, ", pas de fuite");
    }
    y = sx_line(y, "Mémoire", v, "", sx_ram_err || sx_aram_err ? SX_BAD : sx_big0 >= 6u << 20 ? SX_OK : SX_MID);
    snprintf(v, sizeof(v), "%u ms (calcul), copie %u Mo/s", (unsigned)sx_cpu_ms, (unsigned)sx_cpy_mbs);
    sx_cmp(c, sizeof(c), sx_cpu_ms, sx_prev_val("cpu="), 1, " ms");
    y = sx_line(y, "Processeur", v, c, SX_TXT);
    if (sx_net_st[0] == 1) snprintf(v, sizeof(v), "GitHub : %u ms", (unsigned)sx_net_ms[0]);
    else snprintf(v, sizeof(v), "%s", sx_net_st[0] == 3 ? "pas connecté" : "GitHub injoignable");
    if (sx_full && sx_net_st[1] == 1) { size_t l = strlen(v); snprintf(v + l, sizeof(v) - l, ", débit %u Ko/s", (unsigned)sx_net_kbs[1]); }
    y = sx_line(y, "Réseau", v, "", sx_net_st[0] == 1 ? SX_TXT : RGBA(255, 190, 140, 255));
    (void)y;
}
/* texte sur une ou deux lignes (coupe a un espace) ; rend la ligne suivante */
static f32 sx_wrap(f32 x, f32 y, int w, const char *s, col_t c) {
    char fv[200];
    fit_text(F_SMALL, s, w, fv, sizeof(fv));
    size_t l = strlen(fv), sl = strlen(s);
    if (l >= sl || l < 4) { gx_text1(F_SMALL, x, y, fv, c); return y + 18; }
    size_t cut = l > 3 ? l - 3 : l;                   /* (fit_text met "..." a la fin) */
    while (cut > 0 && s[cut] != ' ') cut--;
    if (cut == 0) cut = l > 3 ? l - 3 : l;
    char a[200]; snprintf(a, sizeof(a), "%.*s", (int)cut, s);
    gx_text1(F_SMALL, x, y, a, c);
    fit_text(F_SMALL, s + cut + (s[cut] == ' '), w - 16, fv, sizeof(fv));
    gx_text1(F_SMALL, x + 16, y + 17, fv, c);
    return y + 36;
}
static void sx_page_diag(void) {
    f32 y = 96;
    for (int i = 0; i < sx_nd && y < 398; i++) {
        col_t c = sx_diag_lv[i] == 2 ? SX_BAD : sx_diag_lv[i] == 1 ? SX_MID : SX_OK;
        int sub = sx_diag[i][0] == ' ';
        y = sx_wrap(sub ? 72 : 60, y, sub ? 508 : 520, sx_diag[i] + (sub ? 3 : 0), c);
    }
}
static void sx_page_gcs(void) {
    f32 y = 96;
    char v[200], f_[200];
    for (int ph = 0; ph < IP_N && y < 392; ph++) {
        int c0 = 0, c1 = 0, c2 = 0, w = -1;
        for (int i = 0; i < sx_nit; i++) if (sx_it[i].ph == ph) {
            if (sx_it[i].st == 1) c1++; else if (sx_it[i].st == 2) c2++; else c0++;
            if (w < 0 || sx_it[i].worst_us > sx_it[w].worst_us) w = i;
        }
        if (w < 0) continue;
        snprintf(v, sizeof(v), "%s : %d ok%s%s", sx_ip_name[ph], c0, c1 ? ", à regarder " : "", "");
        if (c1) { size_t l = strlen(v); snprintf(v + l, sizeof(v) - l, "%d", c1); }
        if (c2) { size_t l = strlen(v); snprintf(v + l, sizeof(v) - l, ", %d absents", c2); }
        { size_t l = strlen(v); snprintf(v + l, sizeof(v) - l, "  -  pire %u ms (%s)", (unsigned)(sx_it[w].worst_us / 1000u), sx_it[w].nm); }
        fit_text(F_SMALL, v, 520, f_, sizeof(f_));
        gx_text1(F_SMALL, 60, y, f_, c1 ? SX_MID : SX_OK);
        y += 21;
    }
    if (!sx_nit) gx_text1(F_SMALL, 60, y, "Seulement dans le test total.", SX_TXT);
}
static void sx_page_hw(void) {
    char v[200];
    f32 y = 96;
    #define HW(col, ...) do { snprintf(v, sizeof(v), __VA_ARGS__); char f_[200]; fit_text(F_SMALL, v, 520, f_, sizeof(f_)); gx_text1(F_SMALL, 60, y, f_, col); y += 18; } while (0)
    if (sx_total) for (int k = 0; k < 2; k++) {
        const sxc_t *c = &sx_card[k];
        if (c->st == 0) HW(SX_TXT, "Carte mémoire %c : vide", 'A' + k);
        else if (c->st == 3) HW(SX_TXT, "Slot %c : adaptateur SD", 'A' + k);
        else if (c->st == 2) HW(SX_MID, "Carte mémoire %c : erreur %d", 'A' + k, c->err);
        else HW(c->wr == 2 ? SX_BAD : SX_OK, "Carte mémoire %c : %u blocs, %u libres, écriture %s", 'A' + k, (unsigned)(c->mbit * 16u - 5u), (unsigned)c->freeb, c->wr == 1 ? "ok" : c->wr == 2 ? "ERREUR" : "pas testée");
    }
    if (sx_clk_ok) { s32 dr = sx_clk_diff - sx_clk_tz; HW(dr > 120 || dr < -120 ? SX_MID : SX_OK, "Horloge : %+d s d'écart avec Internet", (int)dr); }
    if (sx_vi_x100) HW(SX_TXT, "Écran : %u,%02u images/s mesurées%s", (unsigned)(sx_vi_x100 / 100), (unsigned)(sx_vi_x100 % 100), VIDEO_HaveComponentCable() ? ", câble composante" : "");
    if (sx_sm_n) HW(SX_TXT, "Carte SD : petit fichier créé en %u,%u ms, effacé en %u,%u ms", (unsigned)(sx_sm_cr_x10 / 10), (unsigned)(sx_sm_cr_x10 % 10), (unsigned)(sx_sm_rm_x10 / 10), (unsigned)(sx_sm_rm_x10 % 10));
    if (sx_ram_kb) HW(sx_ram_err ? SX_BAD : SX_OK, "Mémoire : %u Mo vérifiés, %u erreurs, lecture %u Mo/s, accès %u ns", (unsigned)(sx_ram_kb >> 10), (unsigned)sx_ram_err, (unsigned)sx_ram_r, (unsigned)sx_ram_ns);
    if (sx_aram == 1) HW(sx_aram_err ? SX_BAD : SX_OK, "ARAM : %u Mo vérifiés, %u erreurs, %u Mo/s", (unsigned)(sx_aram_kb >> 10), (unsigned)sx_aram_err, (unsigned)sx_aram_mbs);
    else if (sx_aram == 2) HW(SX_TXT, "ARAM : pas sur le simulateur");
    if (sx_gpu_mpx) HW(SX_TXT, "Processeur graphique : %u millions de points/s, %u milliers de triangles/s", (unsigned)sx_gpu_mpx, (unsigned)sx_gpu_tri);
    if (sd_ok && !sd_virtual) HW(SX_TXT, "Carte SD : 4 Ko au hasard %u,%u ms, dossier de %u fichiers en %u ms, %u Mo libres", (unsigned)(sx_rnd_x10 / 10), (unsigned)(sx_rnd_x10 % 10), (unsigned)sx_dir_n, (unsigned)sx_dir_ms, (unsigned)sx_sd_free);
    if (sx_dns_ms >= 0) HW(sx_dns_ms > 800 ? SX_MID : SX_TXT, "Noms des sites (DNS, box) : %d ms", sx_dns_ms);
    else if (sx_dns_ms == -2) HW(SX_MID, "Noms des sites (DNS, box) : pas de réponse");
    if (sx_net_st[2] == 1) HW(SX_TXT, "Connexion gardée : %u ms (première, chiffrement compris : %u ms)", (unsigned)sx_net_ms[2], (unsigned)sx_net_ms[0]);
    int anyp = 0;
    for (int p = 0; p < 4; p++) if (sx_pad_mask & (1u << p)) {
        anyp = 1;
        int d = sx_pad_dev[p][0] > sx_pad_dev[p][1] ? sx_pad_dev[p][0] : sx_pad_dev[p][1], c = sx_pad_dev[p][2] > sx_pad_dev[p][3] ? sx_pad_dev[p][2] : sx_pad_dev[p][3];
        HW(d >= 15 || c >= 15 ? SX_MID : SX_OK, "Manette %d%s : stick écart %d, stick C %d, gâchettes %d", p + 1, sx_pad_type[p] == SI_GC_WAVEBIRD ? " (WaveBird)" : "", d, c, sx_pad_trg[p]);
    }
    if (!anyp) HW(SX_TXT, "Manettes : aucune vue");
    for (int f = 0; f < SX_NF && y < 384; f++) {
        if (sx_fmt_k[f] < 0) continue;
        if (!sx_fmt_ok[f]) { HW(SX_MID, "%s : illisible", sx_fmt_name[f]); continue; }
        HW(sx_fmt_gap[f] ? SX_BAD : sx_fmt_cpu[f] >= 600 ? SX_MID : SX_OK, "%s : décodage %u %% du processeur, %u trous", sx_fmt_name[f], (unsigned)(sx_fmt_cpu[f] / 10), (unsigned)sx_fmt_gap[f]);
    }
    if (sx_radio == 1) HW(sx_radio_gap ? SX_MID : SX_OK, "Radio : connectée en %u,%u s, %u trous", (unsigned)(sx_radio_ms / 1000), (unsigned)(sx_radio_ms % 1000 / 100), (unsigned)sx_radio_gap);
    else if (sx_radio == 2) HW(SX_MID, "Radio : pas de son au bout de 20 s");
    else if (sx_radio == 4) HW(SX_MID, "Radio : connexion impossible");
    #undef HW
}

static void draw_selftest(void) {
    if (sx_state == SX_BENCH && sx_bstep == 3 && sx_full && now_ms() - sx_t >= 120) sx_bench_gpu();   /* (avant le cadre : invisible) */
    static const char *const pg[] = { "résumé", "diagnostic", "matériel", "GC Station" };
    char right[80];
    if (sx_state == SX_DONE) snprintf(right, sizeof(right), "%s%s  -  %s %d/%d", sx_total ? "total" : sx_full ? "complet" : "rapide", sx_aborted ? " (arrêté)" : "", pg[sx_page], sx_page + 1, sx_total ? 4 : sx_full ? 3 : 2);
    else right[0] = 0;
    tl_frame("Test de la console", right);
    if (sx_state == SX_INTRO) {
        static const char *const l[] = {
            "A : TEST TOTAL (environ 15 minutes) : tout le matériel et tout GC Station",
            "  mémoire, ARAM, graphique, carte SD, cartes mémoire A et B (un fichier de test",
            "  écrit, relu puis effacé), manettes, horloge, réseau, chaque format audio,",
            "  chaque outil, mini-jeu, style, couleur, disposition, spectre, économiseur,",
            "  décor, l'anglais, chaque réglage, photos et vidéo. Tes réglages sont remis.",
            "X : test complet (environ 5 minutes)       Y : test rapide (1 minute)",
            "",
            "Ne touche pas la manette pendant le test (B : arrêter).",
            "Rapport : SD:/gc-mp3/journaux/test-console.txt et sur le téléphone",
            "(page Rapport de bug). Le test d'avant est gardé pour comparer." };
        for (int i = 0; i < 10; i++) gx_text1(F_SMALL, 60, 104 + i * 23, l[i], i == 0 || i == 5 ? RGBA(255, 230, 150, 255) : SX_TXT);
        static const char *const k[] = { "A", "X", "Y", "B" }, *const lb[] = { "TOTAL", "COMPLET", "RAPIDE", "RETOUR" };
        tl_legend(k, lb, 4);
        return;
    }
    if (sx_state == SX_BENCH) {
        static const char *const nm[] = { "processeur", "mémoire principale", "ARAM", "processeur graphique", "carte SD", "manettes (ne touche pas)", "réseau (DNS)",
                                          "carte SD : petits fichiers", "cartes mémoire A et B" };
        const int ns = sx_total ? 9 : 7;
        char b[120];
        snprintf(b, sizeof(b), "Mesures : %s (%d / %d)...", nm[sx_bstep < ns ? sx_bstep : ns - 1], sx_bstep + 1, ns);
        gx_text_c(F_MED, 320, 220, b, RGBA(255, 230, 150, 255));
        gx_rrect_fill(170, 240, 300, 8, 4, RGBA(40, 30, 80, 255), RGBA(40, 30, 80, 255));
        gx_rrect_fill(170, 240, 300.0f * (sx_bstep + 1) / ns, 8, 4, RGBA(150, 100, 255, 255), RGBA(110, 60, 230, 255));
        if (now_ms() - sx_t < 120) return;               /* (apres une image affichee) */
        switch (sx_bstep) {
        case 0: sx_bench_cpu(); break;
        case 1: if (sx_full) sx_bench_ram(); break;
        case 2: if (sx_full) sx_bench_aram(); break;
        case 3: break;                                   /* (fait plus haut, avant le cadre) */
        case 4: sx_bench_sd(); break;
        case 5: if (sx_full) { sx_bench_pads(sx_bsub == 0); if (++sx_bsub < 40) return; } break;
        case 6: if (sx_full) sx_bench_dns(); break;
        case 7: sx_bench_small(); break;
        case 8: sx_bench_cards(); break;
        }
        sx_bstep++; sx_bsub = 0;
        if (sx_bstep >= ns) { sx_state = SX_NET; sx_t0 = now_ms(); sx_net_start(); }
        return;
    }
    if (sx_state == SX_NET) {
        char b[120];
        snprintf(b, sizeof(b), "Réseau : %s...", sx_ni < sx_nn ? sx_net_name[sx_ni] : "");
        gx_text_c(F_MED, 320, 220, b, RGBA(255, 230, 150, 255));
        return;
    }
    if (sx_state != SX_DONE) return;
    if (sx_page == 0) sx_page_summary();
    else if (sx_page == 1) sx_page_diag();
    else if (sx_page == 2) sx_page_hw();
    else sx_page_gcs();
    gx_text1(F_SMALL, 64, 408, "Détail : SD:/gc-mp3/journaux/test-console.txt  -  téléphone : Rapport de bug", RGBA(170, 155, 225, 255));
    static const char *const k[] = { "X", "A", "Y", "B" }, *const lb[] = { "PAGE", "AUTRE TEST", "RAPIDE", "RETOUR" };
    tl_legend(k, lb, 4);
}

/* bandeau pendant le test (par-dessus les onglets) */
static void sx_overlay(void) {
    if (sx_state < SX_SOUND || sx_state > SX_STRESS) return;
    char b[160];
    if (sx_state == SX_SOUND) snprintf(b, sizeof(b), "Test : format audio %s  -  B : arrêter", sx_fmt_i >= 0 && sx_fmt_i < SX_NF ? sx_fmt_name[sx_fmt_i] : "");
    else if (sx_state == SX_RADIO) snprintf(b, sizeof(b), "Test : radio (%s)  -  B : arrêter", sx_sub ? "écoute" : "connexion");
    else if (sx_state == SX_NETUSE) snprintf(b, sizeof(b), "Test : réseau en vrai (%s)  -  B : arrêter", sx_sub < 2 ? "téléchargement" : sx_sub < 4 ? "page web" : "radio AAC");
    else if (sx_state == SX_SCRIPT) snprintf(b, sizeof(b), "Test total : %s (%d / %d)  -  B : arrêter", sx_ci >= 0 ? sx_it[sx_ci].nm : "...", sx_k + 1, sx_nk);
    else if (sx_state == SX_STRESS) snprintf(b, sizeof(b), "Test : stress (onglets très vite, menu, outils)  -  B : arrêter");
    else if (sx_i >= 0 && sx_i < sx_ntabs) snprintf(b, sizeof(b), "Test : parcours %d%s, onglet %d / %d  -  B : arrêter", sx_pass, sx_full ? "/2" : "", sx_i + 1, sx_ntabs);
    else snprintf(b, sizeof(b), "Test : menu START  -  B : arrêter");
    f32 w = (f32)text_width(F_SMALL, b, 0) + 24;
    gx_rrect_fill(320 - w / 2, 440, w, 24, 8, RGBA(40, 10, 60, 230), RGBA(40, 10, 60, 230));
    gx_text_c(F_SMALL, 320, 457, b, RGBA(255, 230, 150, 255));
}

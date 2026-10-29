// GC Station v259 : START > Reglages > Outils > Test de la console (inclus dans main.c, apres themes.c).
// Sur la vraie GameCube : des mesures, puis GC Station se parcourt toute seule (chaque onglet affiche, listes qui
// defilent, menu START), un morceau en fond, avec la mesure de fluidite (v69) et le compte des trous du son (v154).
// Resultat : a l'ecran (resume), dans SD:/gc-mp3/journaux/test-console.txt (detail ; le precedent est garde en
// test-console-ancien.txt pour comparer) et sur le telephone (/test.txt, dans le rapport de bug /rapport).

#define SX_FILE GCD_LOG "/test-console.txt"
#define SX_OLD GCD_LOG "/test-console-ancien.txt"
enum { SX_IDLE, SX_INTRO, SX_BENCH, SX_NET, SX_TOUR, SX_DONE };
static int sx_state = SX_IDLE;
static u64 sx_t = 0, sx_t0 = 0;
static int sx_tabs[TAB_COUNT], sx_ntabs = 0, sx_i = 0, sx_sub = 0, sx_music = 0, sx_aborted = 0, sx_tab0 = 0;
static u32 sx_cpu_ms = 0, sx_int_ms = 0, sx_cpy_mbs = 0, sx_free0 = 0, sx_big0 = 0, sx_rkbs = 0, sx_wkbs = 0, sx_stat_x10 = 0;
static u32 sx_net_ms = 0, sx_net_kbs = 0, sx_gap0 = 0, sx_gapev0 = 0, sx_tot0 = 0, sx_gap = 0, sx_gapev = 0, sx_played = 0;
static int sx_net_state = 0;                    /* 0 pas essaye, 1 ok, 2 echec, 3 pas de reseau */
static u32 sx_frames = 0, sx_slow = 0, sx_worst = 0;
static int sx_worst_tab = -1;
static char sx_prev[200];                       /* (mesures du test d'avant : "cpu=.. sdr=.. sdw=.. net=.. ok=..") */
static hfetch_t sx_hf = { .c = { -1, 0, 0 } };
static char *sx_rep = NULL;

static u32 sx_prev_val(const char *k) {
    const char *p = strstr(sx_prev, k);
    return p ? (u32)strtoul(p + strlen(k), NULL, 10) : 0;
}

static void sx_open(void) {
    menu_open = 0;
    tl_screen = TL_SELFTEST;
    if (sx_state != SX_TOUR) sx_state = sx_rep ? SX_DONE : SX_INTRO;
    if (sx_state == SX_DONE && !sx_rep) sx_state = SX_INTRO;
}

/* ---------- 1. mesures directes (quelques secondes, ecran fige) ---------- */
static void sx_bench(void) {
    struct mallinfo mi = mallinfo();
    sx_free0 = (u32)((uintptr_t)SYS_GetArena1Hi() - (uintptr_t)SYS_GetArena1Lo() + mi.fordblks);
    sx_big0 = mv_largest();
    /* processeur : calcul en virgule flottante, puis en entiers */
    volatile float acc = 0;
    u32 t0 = perf_us();
    for (int i = 0; i < 300000; i++) acc += sinf((float)i * 0.001f) * 1.0001f;
    u32 t1 = perf_us();
    volatile u32 h = 1;
    for (int i = 0; i < 4000000; i++) h = (h * 1664525u + 1013904223u) ^ (h >> 7);
    u32 t2 = perf_us();
    sx_cpu_ms = (t1 - t0) / 1000u; sx_int_ms = (t2 - t1) / 1000u;
    service_audio();
    /* memoire : copies de 1 Mo */
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
    /* carte SD : ecrire 2 Mo, les relire, puis 40 recherches de fichiers */
    sx_rkbs = sx_wkbs = sx_stat_x10 = 0;
    if (sd_ok && !sd_virtual) {
        static u8 blk[65536];
        for (int i = 0; i < (int)sizeof(blk); i++) blk[i] = (u8)(i * 7);
        const char *tf = GCD_LOG "/.test-carte";
        u32 w0 = perf_us();
        FILE *f = fopen(tf, "wb");
        int ok = f != NULL;
        for (int k = 0; ok && k < 32; k++) { if (fwrite(blk, 1, sizeof(blk), f) != sizeof(blk)) ok = 0; service_audio(); }
        if (f && fclose(f) != 0) ok = 0;
        u32 wdt = perf_us() - w0;
        if (ok && wdt) sx_wkbs = (u32)(2048ull * 1000000ull / wdt);
        f = ok ? fopen(tf, "rb") : NULL;
        if (f) {
            u32 r0 = perf_us();
            long got = 0; size_t n;
            while ((n = fread(blk, 1, sizeof(blk), f)) > 0) { got += (long)n; service_audio(); }
            fclose(f);
            u32 rdt = perf_us() - r0;
            if (rdt && got > 0) sx_rkbs = (u32)((u64)got * 1000000ull / 1024ull / rdt);
        }
        remove(tf);
        int ns = nfiles < 40 ? nfiles : 40;
        if (ns > 0) {
            struct stat st;
            u32 s0 = perf_us();
            for (int k = 0; k < ns; k++) stat(names[k], &st);
            sx_stat_x10 = (perf_us() - s0) / 100u / (u32)ns;     /* (dixiemes de ms) */
        }
    }
}

/* ---------- 2. reseau : le fichier des versions de GitHub ---------- */
static void sx_net_start(void) {
    sx_net_state = 0; sx_net_ms = sx_net_kbs = 0;
    if (net_phase != NET_DONE || !net_ok) { sx_net_state = 3; return; }
    char url[256];
    upd_rel_url(url, sizeof(url), 0);
    sx_hf.maxbytes = 512 * 1024;
    sx_hf.image = 0;
    sx_hf.accept = "application/json";
    hf_start(&sx_hf, url, NULL);
    sx_t0 = now_ms();
}
static int sx_net_pump(void) {                              /* 1 = fini */
    if (sx_net_state) return 1;
    hf_pump(&sx_hf);
    u64 n = now_ms();
    if (sx_hf.st == HF_DONE) {
        sx_net_ms = (u32)(n - sx_t0);
        sx_net_kbs = sx_net_ms ? (u32)((u64)sx_hf.len * 1000ull / 1024ull / sx_net_ms) : 0;
        sx_net_state = 1;
    } else if (sx_hf.st == HF_FAIL || n - sx_t0 > 20000) {
        if (sx_hf.st != HF_FAIL) hf_abort(&sx_hf);
        sx_net_state = 2;
    } else return 0;
    hf_drop(&sx_hf);
    sx_hf.st = HF_IDLE;
    return 1;
}

/* ---------- 3. parcours ---------- */
static void sx_tour_start(void) {
    sx_ntabs = 0;
    FOR_TABS(t) if (tab_shown(t)) sx_tabs[sx_ntabs++] = t;
    sx_i = -1; sx_sub = 0;
    sx_tab0 = tab;
    sx_aborted = 0;
    if (perf_on) perf_toggle();                            /* (une mesure deja en cours : son rapport, puis on repart de zero) */
    perf_toggle();
    for (int t = 0; t < TAB_COUNT; t++) mv_tab_seen[t] = 0;
    sx_music = 0;
    if (!((src == SRC_FILE && playing) || (src == SRC_RADIO && state != ST_IDLE)) && nfiles > 0) {   /* un morceau en fond (comme a l'usage) */
        int k = -1;
        for (int i = 0; i < nfiles && k < 0; i++) if (!is_retro[i]) k = i;
        if (k >= 0 && start_track(k)) sx_music = 1;
    }
    sx_gap0 = au_gap; sx_gapev0 = au_gap_ev; sx_tot0 = au_tot;
    tl_screen = TL_NONE;
    menu_open = 0;
    sx_state = SX_TOUR;
    sx_t = now_ms();
}

static void sx_finish(void);
static void sx_tick(void) {
    if (sx_state == SX_NET) { if (sx_net_pump()) sx_tour_start(); return; }
    if (sx_state != SX_TOUR) return;
    u64 n = now_ms();
    last_input_ms = n;                                     /* (pas d'economiseur pendant le test) */
    if (PAD_ButtonsDown(0) & PAD_BUTTON_B && n - sx_t > 300) { sx_aborted = 1; sx_finish(); return; }
    if (n < sx_t) return;
    if (sx_i < sx_ntabs) {
        if (sx_sub == 0) {                                 /* onglet suivant */
            sx_i++;
            if (sx_i >= sx_ntabs) { sx_sub = 0; sx_t = n; return; }
            menu_open = 0; tl_screen = TL_NONE;
            tab = sx_tabs[sx_i]; tab_enter(tab);
            sx_sub = 1; sx_t = n + 2500;                   /* (l'onglet s'ouvre, se charge) */
        } else if (sx_sub <= 6) {                          /* la liste defile */
            tp_keys |= PAD_BUTTON_DOWN;
            sx_sub++; sx_t = n + 220;
        } else { sx_sub = 0; sx_t = n + 700; }
        return;
    }
    /* menu START, puis fin */
    if (sx_sub == 0) { tp_keys |= PAD_BUTTON_START; sx_sub = 1; sx_t = n + 1200; return; }
    if (sx_sub <= 4) { tp_keys |= PAD_BUTTON_DOWN; sx_sub++; sx_t = n + 300; return; }
    if (sx_sub == 5) { tp_keys |= PAD_BUTTON_B; sx_sub = 6; sx_t = n + 800; return; }
    sx_finish();
}

static void sx_mo(char *o, int cap, u32 b) { snprintf(o, cap, "%u,%u Mo", (unsigned)(b >> 20), (unsigned)(((b & 0xFFFFF) * 10) >> 20)); }

static void sx_finish(void) {
    perf_on = 0;
    sx_gap = au_gap - sx_gap0; sx_gapev = au_gap_ev - sx_gapev0; sx_played = (au_tot - sx_tot0) / 48000u;
    if (sx_music) stop_track();
    menu_open = 0;
    tab = sx_tab0; tab_enter(tab);
    sx_frames = sx_slow = sx_worst = 0; sx_worst_tab = -1;
    for (int t = 0; t < PF_TABS; t++) {
        sx_frames += pfx_frames[t]; sx_slow += pfx_slow[t];
        if (pfx_frames[t] && pfx_worst[t] > sx_worst) { sx_worst = pfx_worst[t]; sx_worst_tab = t; }
    }
    /* le test d'avant (pour comparer) */
    sx_prev[0] = 0;
    FILE *pf = sd_ok ? fopen(SX_FILE, "r") : NULL;
    if (pf) {
        char l[256];
        while (fgets(l, sizeof(l), pf)) if (!strncmp(l, "#mesures ", 9)) { snprintf(sx_prev, sizeof(sx_prev), "%s", l + 9); break; }
        fclose(pf);
        remove(SX_OLD);
        rename(SX_FILE, SX_OLD);
    }
    /* rapport */
    free(sx_rep);
    int cap = 16000, n = 0;
    sx_rep = (char *)malloc(cap);
    if (!sx_rep) { sx_state = SX_DONE; tl_screen = TL_SELFTEST; return; }
    #define SXP(...) do { if (n < cap - 1) n += snprintf(sx_rep + n, cap - n, __VA_ARGS__); } while (0)
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char a[24], b[24];
    u32 ok = sx_frames ? (sx_frames - sx_slow) * 1000u / sx_frames : 0;
    SXP("#mesures cpu=%u int=%u cpy=%u sdr=%u sdw=%u stat=%u net=%u ok=%u gaps=%u\n", (unsigned)sx_cpu_ms, (unsigned)sx_int_ms, (unsigned)sx_cpy_mbs,
        (unsigned)sx_rkbs, (unsigned)sx_wkbs, (unsigned)sx_stat_x10, (unsigned)sx_net_kbs, (unsigned)ok, (unsigned)sx_gapev);
    SXP("GC Station %s (build %d) - test de la console, %02d/%02d/%04d %02d:%02d%s\n", GCMP3_VERSION, GCMP3_BUILD,
        tmv ? tmv->tm_mday : 0, tmv ? tmv->tm_mon + 1 : 0, tmv ? tmv->tm_year + 1900 : 0, tmv ? tmv->tm_hour : 0, tmv ? tmv->tm_min : 0,
        sx_aborted ? " (arrete avant la fin)" : "");
    SXP("Ecran %d Hz ; une image ratee = plus de %u ms\n\n", VID_HZ, (unsigned)(VID_SLOW_US / 1000u));
    SXP("== Processeur ==\ncalcul (virgule flottante) : %u ms   calcul (entiers) : %u ms   copie memoire : %u Mo/s\n", (unsigned)sx_cpu_ms, (unsigned)sx_int_ms, (unsigned)sx_cpy_mbs);
    sx_mo(a, sizeof(a), sx_free0); sx_mo(b, sizeof(b), sx_big0);
    SXP("\n== Memoire ==\nlibre au debut : %s   plus grand bloc : %s\n", a, b);
    for (int t = 0; t < TAB_COUNT; t++) if (mv_tab_seen[t]) { sx_mo(a, sizeof(a), mv_tab_min[t]); SXP("   %-16s le moins vu : %s\n", tab_names[t], a); }
    SXP("\n== Carte SD ==\n");
    if (sd_ok && !sd_virtual) SXP("lecture : %u Ko/s   ecriture : %u Ko/s   recherche d'un fichier : %u,%u ms\n", (unsigned)sx_rkbs, (unsigned)sx_wkbs, (unsigned)(sx_stat_x10 / 10), (unsigned)(sx_stat_x10 % 10));
    else SXP("pas de carte SD\n");
    SXP("\n== Reseau ==\n");
    if (sx_net_state == 1) SXP("GitHub (fichier des versions) : %u ms, %u Ko/s\n", (unsigned)sx_net_ms, (unsigned)sx_net_kbs);
    else SXP("%s\n", sx_net_state == 3 ? "pas connecte" : "GitHub injoignable");
    SXP("\n== Son ==\n%u trous (%u ms) sur %u s joues%s\n", (unsigned)sx_gapev, (unsigned)(sx_gap / 48), (unsigned)sx_played, sx_played ? "" : " (pas de musique sur la carte SD)");
    SXP("\n== Fluidite (parcours) ==\n%u images, %u ratees (%u,%u %% a l'heure)", (unsigned)sx_frames, (unsigned)sx_slow, (unsigned)(ok / 10), (unsigned)(ok % 10));
    if (sx_worst_tab >= 0) SXP(", pire %u ms (%s)", (unsigned)(sx_worst / 1000u), sx_worst_tab < TAB_COUNT ? tab_names[sx_worst_tab] : "menu");
    SXP("\n");
    for (int t = 0; t < PF_TABS; t++) {
        if (!pfx_frames[t]) continue;
        SXP("[%s] %u images, %u ratees (%u%%), %u tres lentes, pire %u ms\n", t < TAB_COUNT ? tab_names[t] : "menu / autre",
            (unsigned)pfx_frames[t], (unsigned)pfx_slow[t], (unsigned)(pfx_slow[t] * 100u / pfx_frames[t]), (unsigned)pfx_vslow[t], (unsigned)(pfx_worst[t] / 1000u));
        for (int i = 0; i < PH_N; i++) {
            u32 avg = (u32)(pfx_sum[t][i] / pfx_frames[t]);
            if (avg < 300 && pfx_max[t][i] < 8000 && !pfx_top[t][i]) continue;
            SXP("   %-22s moyenne %4u,%u ms  pire %5u ms  cause de %u images ratees\n", pfx_names[i], (unsigned)(avg / 1000), (unsigned)(avg % 1000 / 100), (unsigned)(pfx_max[t][i] / 1000u), (unsigned)pfx_top[t][i]);
        }
    }
    if (mv_bt_total) {
        SXP("\n== Dernier demarrage : %u ms ==\n", (unsigned)mv_bt_total);
        for (int i = 0; i < mv_bt_k; i++) SXP("   %-24s %5u ms\n", mv_bt_n[i], (unsigned)mv_bt_ms[i]);
    }
    if (sx_prev[0]) SXP("\n(test d'avant : %s)", sx_prev);
    #undef SXP
    FILE *f = sd_ok && !sd_virtual ? fopen(SX_FILE, "w") : NULL;
    if (f) { fwrite(sx_rep, 1, n, f); fclose(f); }
    crumb("test de la console : %u%% a l'heure, SD %u/%u Ko/s, %u trous", (unsigned)(ok / 10), (unsigned)sx_rkbs, (unsigned)sx_wkbs, (unsigned)sx_gapev);
    sx_state = SX_DONE;
    tl_screen = TL_SELFTEST;
}

static void sx_input(u32 down, u32 nav, int fire) {
    (void)nav; (void)fire;
    if (sx_state == SX_INTRO || sx_state == SX_DONE) {
        if (down & PAD_BUTTON_A) { sx_state = SX_BENCH; sx_t = now_ms(); return; }
        if (down & PAD_BUTTON_B) { tl_screen = TL_NONE; menu_open = 1; }
    }
}

/* une ligne du resume : nom, valeur, comparaison avec le test d'avant */
static f32 sx_line(f32 y, const char *k, const char *v, const char *cmp, col_t c) {   /* (rend la ligne suivante) */
    gx_text1(F_MED, 64, y, k, RGBA(200, 190, 240, 255));
    gx_text1(F_MED, 200, y, v, c);
    if (cmp && cmp[0]) { gx_text1(F_SMALL, 200, y + 15, cmp, RGBA(150, 140, 190, 255)); return y + 37; }
    return y + 24;
}
static void sx_cmp(char *o, int cap, u32 now, u32 before, int lower_better, const char *unit) {
    if (!before) { o[0] = 0; return; }
    int better = lower_better ? now < before * 95 / 100 : now > before * 105 / 100, worse = lower_better ? now > before * 105 / 100 : now < before * 95 / 100;
    snprintf(o, cap, "avant %u%s %s", (unsigned)before, unit, better ? "(mieux)" : worse ? "(moins bien)" : "(pareil)");
}

static void draw_selftest(void) {
    tl_frame("Test de la console", sx_state == SX_DONE ? (sx_aborted ? "arr\xC3\xAAt\xC3\xA9" : "termin\xC3\xA9") : "");
    if (sx_state == SX_INTRO) {
        static const char *const l[] = {
            "Mesure ta GameCube en vrai : processeur, m\xC3\xA9moire, carte SD, r\xC3\xA9seau,",
            "puis GC Station se parcourt toute seule (chaque onglet, les listes, le menu)",
            "avec un morceau en fond : fluidit\xC3\xA9, ce qui ralentit, trous dans le son.",
            "", "Dure environ une minute. Ne touche pas la manette (B : arr\xC3\xAAter).",
            "Rapport : SD:/gc-mp3/journaux/test-console.txt et sur le t\xC3\xA9l\xC3\xA9phone",
            "(page Rapport de bug). Le test d'avant est gard\xC3\xA9 pour comparer." };
        for (int i = 0; i < 7; i++) gx_text1(F_SMALL, 64, 112 + i * 22, l[i], RGBA(220, 212, 250, 255));
        static const char *const k[] = { "A", "B" }, *const lb[] = { "LANCER", "RETOUR" };
        tl_legend(k, lb, 2);
        return;
    }
    if (sx_state == SX_BENCH) {
        gx_text_c(F_MED, 320, 220, "Mesures en cours (processeur, m\xC3\xA9moire, carte SD)...", RGBA(255, 230, 150, 255));
        if (now_ms() - sx_t > 120) { sx_bench(); sx_state = SX_NET; sx_net_start(); }   /* (apres une image affichee) */
        return;
    }
    if (sx_state == SX_NET) { gx_text_c(F_MED, 320, 220, "Mesure du r\xC3\xA9seau (GitHub)...", RGBA(255, 230, 150, 255)); return; }
    if (sx_state != SX_DONE) return;
    char v[96], c[96];
    u32 ok = sx_frames ? (sx_frames - sx_slow) * 1000u / sx_frames : 0;
    f32 y = 100;
    snprintf(v, sizeof(v), "%u,%u %% des images \xC3\xA0 l'heure", (unsigned)(ok / 10), (unsigned)(ok % 10));
    sx_cmp(c, sizeof(c), ok / 10, sx_prev_val("ok=") / 10, 0, " %");
    y = sx_line(y, "Fluidit\xC3\xA9", v, c, ok >= 950 ? RGBA(140, 255, 170, 255) : ok >= 850 ? RGBA(255, 220, 120, 255) : RGBA(255, 140, 140, 255));
    if (sx_worst_tab >= 0) {
        snprintf(v, sizeof(v), "pire image %u ms (%s)", (unsigned)(sx_worst / 1000u), sx_worst_tab < TAB_COUNT ? TR(tab_names[sx_worst_tab]) : "menu");
        y = sx_line(y, "", v, "", RGBA(220, 212, 250, 255));
    }
    snprintf(v, sizeof(v), "%u trous (%u ms) sur %u s", (unsigned)sx_gapev, (unsigned)(sx_gap / 48), (unsigned)sx_played);
    y = sx_line(y, "Son", sx_played ? v : "pas de musique test\xC3\xA9" "e", "", sx_gapev == 0 ? RGBA(140, 255, 170, 255) : sx_gapev < 4 ? RGBA(255, 220, 120, 255) : RGBA(255, 140, 140, 255));
    snprintf(v, sizeof(v), "lecture %u Ko/s, \xC3\xA9" "criture %u Ko/s", (unsigned)sx_rkbs, (unsigned)sx_wkbs);
    sx_cmp(c, sizeof(c), sx_rkbs, sx_prev_val("sdr="), 0, " Ko/s");
    y = sx_line(y, "Carte SD", sd_ok && !sd_virtual ? v : "absente", c, RGBA(220, 212, 250, 255));
    snprintf(v, sizeof(v), "recherche d'un fichier %u,%u ms", (unsigned)(sx_stat_x10 / 10), (unsigned)(sx_stat_x10 % 10));
    if (sx_stat_x10) y = sx_line(y, "", v, "", RGBA(220, 212, 250, 255));
    char m1[24], m2[24];
    sx_mo(m1, sizeof(m1), sx_free0); sx_mo(m2, sizeof(m2), sx_big0);
    snprintf(v, sizeof(v), "%s libres (bloc %s)", m1, m2);
    y = sx_line(y, "M\xC3\xA9moire", v, "", sx_big0 >= 6u << 20 ? RGBA(140, 255, 170, 255) : RGBA(255, 220, 120, 255));
    snprintf(v, sizeof(v), "%u ms (calcul), copie %u Mo/s", (unsigned)sx_cpu_ms, (unsigned)sx_cpy_mbs);
    sx_cmp(c, sizeof(c), sx_cpu_ms, sx_prev_val("cpu="), 1, " ms");
    y = sx_line(y, "Processeur", v, c, RGBA(220, 212, 250, 255));
    if (sx_net_state == 1) snprintf(v, sizeof(v), "GitHub : %u ms, %u Ko/s", (unsigned)sx_net_ms, (unsigned)sx_net_kbs);
    else snprintf(v, sizeof(v), "%s", sx_net_state == 3 ? "pas connect\xC3\xA9" : "GitHub injoignable");
    y = sx_line(y, "R\xC3\xA9seau", v, "", sx_net_state == 1 ? RGBA(220, 212, 250, 255) : RGBA(255, 190, 140, 255));
    gx_text1(F_SMALL, 64, y + 6, "D\xC3\xA9tail : SD:/gc-mp3/journaux/test-console.txt  -  t\xC3\xA9l\xC3\xA9phone : page Rapport de bug", RGBA(170, 155, 225, 255));
    static const char *const k[] = { "A", "B" }, *const lb[] = { "REFAIRE", "RETOUR" };
    tl_legend(k, lb, 2);
}

/* bandeau pendant le parcours (par-dessus les onglets) */
static void sx_overlay(void) {
    if (sx_state != SX_TOUR) return;
    char b[120];
    if (sx_i >= 0 && sx_i < sx_ntabs) snprintf(b, sizeof(b), "Test de la console : onglet %d / %d  -  B : arr\xC3\xAAter", sx_i + 1, sx_ntabs);
    else snprintf(b, sizeof(b), "Test de la console : menu START  -  B : arr\xC3\xAAter");
    f32 w = (f32)text_width(F_SMALL, b, 0) + 24;
    gx_rrect_fill(320 - w / 2, 440, w, 24, 8, RGBA(40, 10, 60, 230), RGBA(40, 10, 60, 230));
    gx_text_c(F_SMALL, 320, 457, b, RGBA(255, 230, 150, 255));
}

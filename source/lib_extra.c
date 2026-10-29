// GC-MP3 : listes de lecture, recherche dans la bibliotheque, morceaux les plus
// ecoutes, et petit menu d'actions (touche X de l'onglet MUSIQUE). Inclus dans main.c.
//  - listes : SD:/gc-mp3/listes/<Nom>.m3u (une adresse de fichier par ligne) ;
//  - ecoutes : SD:/gc-mp3/bibliotheque/gc-mp3-ecoutes.txt (nombre|ordre|chemin), un morceau compte
//    quand on l'a ecoute 30 s (ou la moitie s'il est court).

#define PL_DIR    GCD "/listes"
#define PL_MAX    40
#define PL_ITEMS  600
#define ST_FILE   GCD_BIB "/gc-mp3-ecoutes.txt"
#define AUTO_MAX  50

typedef struct { char name[48]; s16 *it; int n; } plist_t;
static plist_t pls[PL_MAX];
static int npls = 0;
static int stats_dirty = 0;
static int stat_counted = 0;
static u64 stats_saved_t = 0;
static char lib_note_txt[180] = "";
static u64 lib_note_until = 0;

static void lib_note(const char *m) { snprintf(lib_note_txt, sizeof(lib_note_txt), "%s", m); lib_note_until = now_ms() + 2500; }

/* ---------- listes de lecture ---------- */
static void pl_free_all(void) {
    for (int i = 0; i < npls; i++) free(pls[i].it);
    memset(pls, 0, sizeof(pls));
    npls = 0;
}

static int pl_cmp(const void *a, const void *b) { return strcasecmp(((const plist_t *)a)->name, ((const plist_t *)b)->name); }

static void pl_path(const char *name, char *out, int cap) { snprintf(out, cap, PL_DIR "/%s.m3u", name); }

/* nom de liste utilisable comme nom de fichier */
static void pl_clean_name(const char *in, char *out, int cap) {
    int n = 0;
    for (; *in && n < cap - 1; in++) {
        unsigned char c = (unsigned char)*in;
        if (c < 0x20 || strchr("/\\:*?\"<>|", c)) c = '-';
        out[n++] = (char)c;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) n--;
    out[n] = 0;
}

static void pl_load_all(void) {
    pl_free_all();
    if (!sd_ok) return;
    DIR *d = opendir(PL_DIR);
    if (!d) return;
    struct dirent *e;
    static char line[NAME_LEN + 16];
    while ((e = readdir(d)) != NULL && npls < PL_MAX) {
        size_t l = strlen(e->d_name);
        if (l < 5 || strcasecmp(e->d_name + l - 4, ".m3u") != 0 || e->d_name[0] == '.') continue;
        plist_t *p = &pls[npls];
        snprintf(p->name, sizeof(p->name), "%.*s", (int)(l - 4), e->d_name);
        p->it = (s16 *)malloc(sizeof(s16) * PL_ITEMS);
        p->n = 0;
        if (!p->it) break;
        char path[NAME_LEN + 16];
        snprintf(path, sizeof(path), PL_DIR "/%s", e->d_name);
        FILE *f = fopen(path, "r");
        if (f) {
            while (fgets(line, sizeof(line), f) && p->n < PL_ITEMS) {
                char *t = trim(line);
                if (!*t || *t == '#') continue;
                int k = bib_find(t);
                if (k >= 0 && !is_retro[k]) p->it[p->n++] = (s16)k;
            }
            fclose(f);
        }
        npls++;
    }
    closedir(d);
    qsort(pls, npls, sizeof(plist_t), pl_cmp);
}

static int pl_save(int i) {
    if (i < 0 || i >= npls || !sd_ok) return 0;
    mkdir(PL_DIR, 0777);
    char path[NAME_LEN + 16];
    pl_path(pls[i].name, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "#EXTM3U\n");
    for (int k = 0; k < pls[i].n; k++) fprintf(f, "%s\n", names[pls[i].it[k]]);
    return fclose(f) == 0;
}

static int pl_find(const char *name) {
    for (int i = 0; i < npls; i++) if (!strcasecmp(pls[i].name, name)) return i;
    return -1;
}

static int pl_create(const char *raw) {
    char name[48];
    pl_clean_name(raw, name, sizeof(name));
    if (!name[0]) return -1;
    int i = pl_find(name);
    if (i >= 0) return i;
    if (npls >= PL_MAX) { lib_note("Trop de listes (40 au maximum)"); return -1; }
    s16 *it = (s16 *)malloc(sizeof(s16) * PL_ITEMS);
    if (!it) return -1;
    plist_t *p = &pls[npls++];
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->it = it;
    p->n = 0;
    if (!pl_save(npls - 1)) lib_note("Impossible d'\xC3\xA9" "crire dans SD:/gc-mp3/listes");
    /* garde l'ordre alphabetique */
    qsort(pls, npls, sizeof(plist_t), pl_cmp);
    return pl_find(name);
}

static void pl_add(int i, int track) {
    if (i < 0 || i >= npls || track < 0 || track >= nfiles) return;
    for (int k = 0; k < pls[i].n; k++) if (pls[i].it[k] == track) { lib_note("D\xC3\xA9j\xC3\xA0 dans cette liste"); return; }
    if (pls[i].n >= PL_ITEMS) { lib_note("Liste pleine"); return; }
    pls[i].it[pls[i].n++] = (s16)track;
    char m[96];
    snprintf(m, sizeof(m), "Ajout\xC3\xA9 \xC3\xA0 \xC2\xAB %s \xC2\xBB", pls[i].name);
    lib_note(pl_save(i) ? m : "Impossible d'\xC3\xA9" "crire la liste");
    lib_dirty = 1;
}

static void pl_remove_at(int i, int pos) {
    if (i < 0 || i >= npls || pos < 0 || pos >= pls[i].n) return;
    memmove(pls[i].it + pos, pls[i].it + pos + 1, sizeof(s16) * (pls[i].n - pos - 1));
    pls[i].n--;
    pl_save(i);
    lib_note("Retir\xC3\xA9 de la liste");
    lib_dirty = 1;
}

static void pl_move(int i, int pos, int d) {
    if (i < 0 || i >= npls || pos < 0 || pos >= pls[i].n || pos + d < 0 || pos + d >= pls[i].n) return;
    s16 t = pls[i].it[pos]; pls[i].it[pos] = pls[i].it[pos + d]; pls[i].it[pos + d] = t;
    pl_save(i);
    lib_dirty = 1;
}

static void pl_delete(int i) {
    if (i < 0 || i >= npls) return;
    char path[NAME_LEN + 16];
    pl_path(pls[i].name, path, sizeof(path));
    remove(path);
    free(pls[i].it);
    memmove(pls + i, pls + i + 1, sizeof(plist_t) * (npls - i - 1));
    npls--;
    memset(&pls[npls], 0, sizeof(plist_t));
    lib_note("Liste supprim\xC3\xA9" "e");
    lib_dirty = 1;
}

/* ---------- morceaux les plus ecoutes ---------- */
static u16 play_cnt[MAX_FILES];
static u32 play_last[MAX_FILES];
static u32 play_seq = 0;

/* ---------- v76 : notes (etoiles) des morceaux ---------- */
/* rating[] est declare dans main.c (avant ui_extra.c qui l'affiche) */
#define RT_FILE GCD_BIB "/gc-mp3-notes.txt"
static int rating_dirty = 0;
static u64 rating_saved_t = 0;
static void rating_load(void) {
    memset(rating, 0, sizeof(rating));
    FILE *f = sd_ok ? fopen(RT_FILE, "r") : NULL;
    if (!f) return;
    static char line[NAME_LEN + 16];
    while (fgets(line, sizeof(line), f)) {
        char *a = strchr(line, '|');
        if (!a) continue;
        char *p = trim(a + 1);
        int k = bib_find(p);
        if (k < 0) continue;
        int n = atoi(line);
        rating[k] = (u8)(n < 0 ? 0 : n > 5 ? 5 : n);
    }
    fclose(f);
}
static void rating_save_x(int bg) {                         /* v195 : bg = ecrites par le fil */
    rating_dirty = 0;
    rating_saved_t = now_ms();
    if (!sd_ok) return;
    if (!bg) bgf_wait();
    FILE *f = bg ? bgw_begin() : NULL;
    int via_bg = f != NULL;
    if (!f) f = fopen(RT_FILE, "w");
    if (!f) return;
    for (int k = 0; k < nfiles; k++) if (rating[k]) fprintf(f, "%u|%s\n", rating[k], names[k]);
    if (via_bg) bgw_end(f, RT_FILE); else fclose(f);
}
static void rating_save(void) { rating_save_x(0); }
static void rating_set(int k, int n) {
    if (k < 0 || k >= nfiles) return;
    rating[k] = (u8)(n < 0 ? 0 : n > 5 ? 5 : n);
    rating_dirty = 1;
    if (view_mode == VIEW_LISTS) lib_dirty = 1;
}

static void stats_load(void) {
    memset(play_cnt, 0, sizeof(play_cnt));
    memset(play_last, 0, sizeof(play_last));
    play_seq = 0;
    FILE *f = sd_ok ? fopen(ST_FILE, "r") : NULL;
    if (!f) return;
    static char line[NAME_LEN + 40];
    while (fgets(line, sizeof(line), f)) {
        char *a = strchr(line, '|');
        if (!a) continue;
        char *b = strchr(a + 1, '|');
        if (!b) continue;
        char *p = trim(b + 1);
        int k = bib_find(p);
        if (k < 0) continue;
        long c = atol(line), s = atol(a + 1);
        play_cnt[k] = (u16)(c < 0 ? 0 : c > 60000 ? 60000 : c);
        play_last[k] = (u32)(s < 0 ? 0 : s);
        if (play_last[k] > play_seq) play_seq = play_last[k];
    }
    fclose(f);
}

static void stats_save_x(int bg) {                          /* v195 : bg = ecrites par le fil */
    stats_dirty = 0;
    stats_saved_t = now_ms();
    if (!sd_ok) return;
    if (!bg) bgf_wait();
    FILE *f = bg ? bgw_begin() : NULL;
    int via_bg = f != NULL;
    if (!f) f = fopen(ST_FILE, "w");
    if (!f) return;
    for (int k = 0; k < nfiles; k++)
        if (play_cnt[k] || play_last[k]) fprintf(f, "%u|%lu|%s\n", play_cnt[k], (unsigned long)play_last[k], names[k]);
    if (via_bg) bgw_end(f, ST_FILE); else fclose(f);
}
static void stats_save(void) { stats_save_x(0); }

static void add_pump(void);
/* boucle principale : compte le morceau en cours, enregistre quand rien ne bouge */
static void stats_pump(void) {
    if (!sx_quiet && src == SRC_FILE && playing && !stat_counted && cur_track >= 0 && cur_track < nfiles && !is_retro[cur_track]) {
        int d = dur_cache[cur_track];
        u32 need = 30u * 48000u;
        if (d > 0 && d < 60) need = (u32)d * 24000u;               /* morceau court : la moitie */
        if (consumed >= need) {
            stat_counted = 1;
            if (play_cnt[cur_track] < 60000) play_cnt[cur_track]++;
            play_last[cur_track] = ++play_seq;
            stats_dirty = 1;
            if (view_mode == VIEW_LISTS) lib_dirty = 1;
        }
    }
    if (stats_dirty && now_ms() - last_input_ms > 4000 && now_ms() - stats_saved_t > 20000) stats_save_x(1);   /* v195 : par le fil */
    if (rating_dirty && now_ms() - last_input_ms > 2000 && now_ms() - rating_saved_t > 8000) rating_save_x(1);
    add_pump();                                                  /* v73 : dates d'ajout (liste "Ajoutes recemment") */
}

/* v76 : morceaux notes, du mieux note au moins bien (puis les plus ecoutes) */
static int rated_list(s16 *out, int max) {
    int n = 0;
    for (int k = 0; k < nfiles && n < MAX_FILES; k++) if (!is_retro[k] && rating[k] > 0) out[n++] = (s16)k;
    for (int i = 1; i < n; i++) {
        s16 v = out[i];
        int j = i - 1;
        while (j >= 0 && (rating[out[j]] < rating[v] || (rating[out[j]] == rating[v] && play_cnt[out[j]] < play_cnt[v]))) { out[j + 1] = out[j]; j--; }
        out[j + 1] = v;
    }
    return n < max ? n : max;
}

/* listes automatiques : 0 = les plus ecoutes, 1 = recemment ecoutes */
static int auto_list(int which, s16 *out, int max) {
    int n = 0;
    for (int k = 0; k < nfiles && n < MAX_FILES; k++)
        if (!is_retro[k] && (which == 0 ? play_cnt[k] > 0 : play_last[k] > 0)) out[n++] = (s16)k;
    /* tri par insertion (quelques centaines au plus) */
    for (int i = 1; i < n; i++) {
        s16 v = out[i];
        int j = i - 1;
        while (j >= 0) {
            int a = out[j], better;
            if (which == 0) better = play_cnt[v] > play_cnt[a] || (play_cnt[v] == play_cnt[a] && play_last[v] > play_last[a]);
            else better = play_last[v] > play_last[a];
            if (!better) break;
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = v;
    }
    return n < max ? n : max;
}

/* ---------- v73 : listes intelligentes : jamais ecoutes, ajoutes recemment ---------- */
/* date d'ajout = date du fichier, lue une fois (quelques fichiers par image) et gardee dans
   SD:/gc-mp3/cache/ajouts.txt ; la liste se remplit pendant qu'on navigue */
#define ADD_FILE GCD "/cache/ajouts.txt"
static u32 *add_t = NULL;                 /* date du fichier (0 = pas encore lue, 1 = inconnue) */
static int add_state = 0, add_k = 0, add_nf = -1, add_dirty = 0;
static void add_reset(void) { add_state = 0; add_k = 0; }
static void add_pump(void) {
    if (view_mode != VIEW_LISTS || !sd_ok || nfiles <= 0 || fun_mode) return;
    if (add_nf != nfiles) { free(add_t); add_t = NULL; add_nf = nfiles; add_reset(); }
    if (!add_t) { add_t = (u32 *)calloc(nfiles, sizeof(u32)); if (!add_t) return; }
    if (add_state == 0) {                                     /* le cache d'abord */
        FILE *f = fopen(ADD_FILE, "r");
        static char line[NAME_LEN + 24];
        while (f && fgets(line, sizeof(line), f)) {
            char *bar = strchr(line, '|');
            if (!bar) continue;
            int k = bib_find(trim(bar + 1));
            if (k >= 0) add_t[k] = (u32)strtoul(line, NULL, 10);
        }
        if (f) fclose(f);
        add_state = 1;
        return;
    }
    if (add_state != 1) return;
    u64 t0 = now_ms();
    int got = 0;
    while (add_k < nfiles && now_ms() - t0 < 3) {             /* (3 ms par image au plus) */
        int k = add_k++;
        if (add_t[k] || is_retro[k]) continue;
        struct stat sb;
        if (stat(names[k], &sb) != 0) { add_t[k] = 1; continue; }
#ifdef GEKKO
        time_t tm = sb.st_ctime > sb.st_mtime ? sb.st_ctime : sb.st_mtime;   /* (FAT : date de creation = arrivee sur la carte) */
#else
        time_t tm = sb.st_mtime;                                             /* (PC : st_ctime n'est pas la creation) */
#endif
        add_t[k] = tm > 1 ? (u32)tm : 1;
        got++;
    }
    if (got) add_dirty = 1;
    if (add_k >= nfiles) {
        add_state = 2;
        lib_dirty = 1;                                       /* (la liste "Ajoutes recemment" se met a jour) */
        if (add_dirty && !sd_virtual) {
            mkdir(GCD "/cache", 0777);
            FILE *f = fopen(ADD_FILE ".part", "w");
            int ok = f != NULL;
            for (int k = 0; ok && k < nfiles; k++) if (add_t[k] > 1) { if (fprintf(f, "%lu|%s\n", (unsigned long)add_t[k], names[k]) < 0) ok = 0; }
            if (f && fclose(f) != 0) ok = 0;
            if (ok) { remove(ADD_FILE); rename(ADD_FILE ".part", ADD_FILE); } else remove(ADD_FILE ".part");
        }
        add_dirty = 0;
    }
}
/* 2 = jamais ecoutes (melanges, toujours dans le meme ordre le meme jour), 3 = ajoutes recemment */
static int smart_list(int which, s16 *out, int max) {
    int n = 0;
    if (which == 2) {
        for (int k = 0; k < nfiles && n < MAX_FILES; k++) if (!is_retro[k] && !play_cnt[k] && !play_last[k]) out[n++] = (s16)k;
        time_t t = time(NULL);
        u32 seed = (u32)(t / 86400) * 2654435761u + 1;
        for (int i = n - 1; i > 0; i--) { seed = seed * 1103515245u + 12345u; int j = (int)((seed >> 8) % (u32)(i + 1)); s16 v = out[i]; out[i] = out[j]; out[j] = v; }
        return n < max ? n : max;
    }
    if (!add_t || add_nf != nfiles) return 0;
    for (int k = 0; k < nfiles && n < MAX_FILES; k++) if (!is_retro[k] && add_t[k] > 1) out[n++] = (s16)k;
    for (int i = 1; i < n; i++) {                             /* les plus recents d'abord */
        s16 v = out[i];
        int j = i - 1;
        while (j >= 0 && add_t[out[j]] < add_t[v]) { out[j + 1] = out[j]; j--; }
        out[j + 1] = v;
    }
    return n < max ? n : max;
}

/* ---------- vue "Mes listes" ---------- */
#define LG_USER 5                        /* groupes 0 a 4 : listes automatiques (v73 : jamais ecoutes, ajoutes recemment ; v76 : mes preferes) */
static void lists_build_groups(void) {
    static s16 tmp[MAX_FILES];
    snprintf(grp_name[0], sizeof(grp_name[0]), "Mes pr\xC3\xA9" "f\xC3\xA9r\xC3\xA9s");
    grp_count[0] = (s16)rated_list(tmp, AUTO_MAX);
    snprintf(grp_name[1], sizeof(grp_name[1]), "Les plus \xC3\xA9" "cout\xC3\xA9s");
    grp_count[1] = (s16)auto_list(0, tmp, AUTO_MAX);
    snprintf(grp_name[2], sizeof(grp_name[2]), "\xC3\x89" "cout\xC3\xA9s r\xC3\xA9" "cemment");
    grp_count[2] = (s16)auto_list(1, tmp, AUTO_MAX);
    snprintf(grp_name[3], sizeof(grp_name[3]), "Jamais \xC3\xA9" "cout\xC3\xA9s");
    grp_count[3] = (s16)smart_list(2, tmp, AUTO_MAX);
    snprintf(grp_name[4], sizeof(grp_name[4]), "Ajout\xC3\xA9s r\xC3\xA9" "cemment");
    grp_count[4] = (s16)smart_list(3, tmp, AUTO_MAX);
    for (int i = 0; i < npls && LG_USER + i < MAX_FILES; i++) {
        snprintf(grp_name[LG_USER + i], sizeof(grp_name[0]), "%s", pls[i].name);
        grp_count[LG_USER + i] = (s16)pls[i].n;
    }
    ngroups = LG_USER + npls;
    for (int k = 0; k < nfiles; k++) grp_of[k] = -1;
}

static int lists_tracks(int g, s16 *out, int max) {
    if (g == 0) return rated_list(out, max < AUTO_MAX ? max : AUTO_MAX);
    if (g == 1 || g == 2) return auto_list(g - 1, out, max < AUTO_MAX ? max : AUTO_MAX);
    if (g == 3 || g == 4) return smart_list(g - 1, out, max < AUTO_MAX ? max : AUTO_MAX);
    int i = g - LG_USER, n = 0;
    if (i < 0 || i >= npls) return 0;
    for (int k = 0; k < pls[i].n && n < max; k++) out[n++] = pls[i].it[k];
    return n;
}

/* ---------- recherche ---------- */
static char lib_find[64] = "";

/* minuscules sans accents (UTF-8 francais courant) */
static void fold(const char *in, char *out, int cap) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && n < cap - 1) {
        unsigned c = *p++;
        if (c == 0xC3 && *p) {
            unsigned d = *p++;
            const char *m = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";        /* 0xA0..0xBF */
            if (d >= 0xA0 && d <= 0xBF) c = (unsigned char)m[d - 0xA0];
            else if (d >= 0x80 && d <= 0x9F) c = (unsigned char)tolower((unsigned char)m[d - 0x80]);
            else c = '?';
        } else c = (unsigned)tolower((int)c);
        out[n++] = (char)c;
    }
    out[n] = 0;
}

static int lib_match(int k) {
    char q[64], h[NAME_LEN];
    fold(lib_find, q, sizeof(q));
    if (!q[0]) return 1;
    fold(display_name(base_of(names[k])), h, sizeof(h));
    if (strstr(h, q)) return 1;
    if (tg_done[k]) {
        fold(tg_artist[k], h, sizeof(h));
        if (strstr(h, q)) return 1;
        fold(tg_album[k], h, sizeof(h));
        if (strstr(h, q)) return 1;
        fold(tg_title[k], h, sizeof(h));
        if (strstr(h, q)) return 1;
    }
    return 0;
}

/* ---------- menu d'actions (X) ---------- */
enum { AC_ADD = 1, AC_ADDTO, AC_NEWLIST, AC_REMOVE, AC_UP, AC_DOWN, AC_DELLIST, AC_FIND, AC_CLEARFIND, AC_RESCAN, AC_CLOSE,
       AC_KARAOKE, AC_RHYTHM, AC_QUIZ, AC_TITLES, AC_TITLE, AC_TITLES_CLEAR, AC_SNAKE, AC_BRICKS, AC_RATE };   /* (v18 : fun_extra.c ; v76 : AC_RATE = note) */
/* le texte de la ligne (les etoiles sont dessinees a part dans draw_actions) */
static void rating_label(int k, char *out, int cap) {
    (void)k;
    snprintf(out, cap, "Note :");
}
static void fun_action(int code, int arg, int track, const char *txt);
static int am_open = 0, am_sel = 0, am_n = 0, am_track = -1, am_list = -1, am_pos = -1, am_pending = -1;
static char am_title[96];
static char am_txt[PL_MAX + 8][160];
static int am_code[PL_MAX + 8], am_arg[PL_MAX + 8];

static void am_item(const char *t, int code, int arg) {
    if (am_n >= PL_MAX + 8) return;
    snprintf(am_txt[am_n], sizeof(am_txt[0]), "%s", t);
    am_code[am_n] = code;
    am_arg[am_n] = arg;
    am_n++;
}

static void kb_start(int mode, const char *init, int field, int form);

static void actions_open(void) {
    am_n = 0;
    am_sel = 0;
    am_track = -1;
    am_list = -1;
    am_pos = -1;
    lent_t e = (sel_row >= 0 && sel_row < nents) ? ents[sel_row] : (lent_t){ 255, -1 };
    if (e.type == E_TRACK) {
        am_track = e.idx;
        char t[NAME_LEN];
        track_title(e.idx, t, sizeof(t));
        snprintf(am_title, sizeof(am_title), "%.90s", t);
        char rl[48];
        rating_label(am_track, rl, sizeof(rl));
        am_item(rl, AC_RATE, 0);
        am_item("Ajouter \xC3\xA0 une liste...", AC_ADD, 0);
        if (view_mode == VIEW_LISTS && !lib_find[0] && cur_group >= LG_USER) {
            am_list = cur_group - LG_USER;
            am_pos = sel_row - 1;                          /* (ligne 0 : retour) */
            am_item("Retirer de cette liste", AC_REMOVE, 0);
            if (am_pos > 0) am_item("Monter dans la liste", AC_UP, 0);
            if (am_list < npls && am_pos + 1 < pls[am_list].n) am_item("Descendre dans la liste", AC_DOWN, 0);
        }
    } else if (e.type == E_GROUP && view_mode == VIEW_LISTS && e.idx >= LG_USER) {
        am_list = e.idx - LG_USER;
        snprintf(am_title, sizeof(am_title), "Liste \xC2\xAB %.60s \xC2\xBB", grp_name[e.idx]);
        am_item("Supprimer cette liste", AC_DELLIST, 0);
    } else snprintf(am_title, sizeof(am_title), "Musique");
    am_item("Nouvelle liste...", AC_NEWLIST, 0);
    am_item("Chercher un morceau...", AC_FIND, 0);
    if (lib_find[0]) am_item("Effacer la recherche", AC_CLEARFIND, 0);
    if (am_track >= 0) {
        am_item("Karaok\xC3\xA9 sur ce morceau", AC_KARAOKE, 0);
        am_item("Jeu de rythme sur ce morceau", AC_RHYTHM, 0);
    }
    am_item("Quiz musical", AC_QUIZ, 0);
    am_item("Snake musical", AC_SNAKE, 0);
    am_item("Casse-briques musical", AC_BRICKS, 0);
    am_item("Actualiser la biblioth\xC3\xA8que", AC_RESCAN, 0);
    am_item("Fermer", AC_CLOSE, 0);
    am_open = 1;
}

static void actions_choose_list(void) {
    int t = am_track;
    am_n = 0;
    am_sel = 0;
    snprintf(am_title, sizeof(am_title), "Ajouter \xC3\xA0 quelle liste ?");
    for (int i = 0; i < npls; i++) am_item(pls[i].name, AC_ADDTO, i);
    am_item("Nouvelle liste...", AC_NEWLIST, 0);
    am_item("Annuler", AC_CLOSE, 0);
    am_track = t;
}

static void music_rescan(void);
static void lib_find_set(const char *q) {
    snprintf(lib_find, sizeof(lib_find), "%s", q);
    sel_row = 0;
    lib_rebuild();
    if (lib_find[0]) {
        char m[96];
        int n = 0;
        for (int i = 0; i < nents; i++) n += ents[i].type == E_TRACK;
        snprintf(m, sizeof(m), "%d morceau%s trouv\xC3\xA9%s", n, n > 1 ? "x" : "", n > 1 ? "s" : "");
        lib_note(m);
    }
}

static void actions_do(int i) {
    if (i < 0 || i >= am_n) { am_open = 0; return; }
    int code = am_code[i], arg = am_arg[i];
    am_open = 0;
    switch (code) {
    case AC_ADD:
        if (npls == 0) { am_pending = am_track; kb_start(KB_PLNAME, "", -1, -1); }
        else { actions_choose_list(); am_open = 1; }
        break;
    case AC_ADDTO: pl_add(arg, am_track); lib_rebuild(); break;
    case AC_NEWLIST: am_pending = am_track; kb_start(KB_PLNAME, "", -1, -1); break;
    case AC_REMOVE: pl_remove_at(am_list, am_pos); lib_rebuild(); break;
    case AC_UP: pl_move(am_list, am_pos, -1); lib_rebuild(); sel_row--; break;
    case AC_DOWN: pl_move(am_list, am_pos, 1); lib_rebuild(); sel_row++; break;
    case AC_DELLIST: pl_delete(am_list); lib_rebuild(); break;
    case AC_FIND: kb_start(KB_LIBFIND, lib_find, -1, -1); break;
    case AC_CLEARFIND: lib_find_set(""); break;
    case AC_RESCAN: music_rescan(); lib_note("Biblioth\xC3\xA8que actualis\xC3\xA9" "e"); break;
    case AC_RATE:                                         /* A : une etoile de plus (5 -> 0) */
        if (am_track >= 0) { rating_set(am_track, (rating[am_track] + 1) % 6); rating_label(am_track, am_txt[i], sizeof(am_txt[0])); am_open = 1; }
        break;
    case AC_CLOSE: break;
    default: if (cp_action(code) || fe_am_action(code)) break; fun_action(code, arg, am_track, am_txt[i]); break;   /* (v137 : base de compatibilite ; v143 : jeux du reseau) */
    }
}

/* clavier : nom de la nouvelle liste ou texte cherche */
static void lib_kb_done(int mode, const char *t) {
    if (mode == KB_LIBFIND) { lib_find_set(t); return; }
    if (!*t) return;
    int i = pl_create(t);
    if (i < 0) return;
    if (am_pending >= 0) pl_add(i, am_pending);
    else lib_note("Liste cr\xC3\xA9\xC3\xA9" "e");
    am_pending = -1;
    lib_rebuild();
}

static void actions_input(u32 down, u32 nav, int fire) {
    if (fire && (nav & PAD_BUTTON_DOWN)) am_sel = (am_sel + 1) % am_n;
    if (fire && (nav & PAD_BUTTON_UP)) am_sel = (am_sel + am_n - 1) % am_n;
    if (am_sel >= 0 && am_sel < am_n && am_code[am_sel] == AC_RATE && am_track >= 0 && fire && (nav & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT))) {
        rating_set(am_track, rating[am_track] + ((nav & PAD_BUTTON_RIGHT) ? 1 : -1));
        rating_label(am_track, am_txt[am_sel], sizeof(am_txt[0]));
        return;
    }
    if (down & PAD_BUTTON_A) actions_do(am_sel);
    else if (down & (PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_START)) am_open = 0;
}

static void draw_actions(void) {
    if (lib_note_txt[0] && now_ms() < lib_note_until && !am_open) {
        char nt[200];
        fit_text(F_MED, lib_note_txt, 580, nt, sizeof(nt));
        int w = text_width(F_MED, nt, 0) + 30;
        gx_rrect_fill(320 - w / 2.0f, 440, (f32)w, 28, 10, RGBA(10, 6, 30, 235), RGBA(10, 6, 30, 235));
        gx_text_c(F_MED, 320, 459, nt, RGBA(255, 255, 255, 255));
    }
    if (!am_open) return;
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 140), RGBA(0, 0, 0, 140), RGBA(0, 0, 0, 140), RGBA(0, 0, 0, 140));
    const int vis = 11;
    f32 rh = 25, w = 400, x = 120;
    int shown = am_n < vis ? am_n : vis;
    f32 h = 66 + shown * rh, y = 240 - h / 2;
    gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 255), RGBA(8, 5, 26, 255));
    draw_panel(x, y, w, h);
    char t[96];
    fit_text(F_MED, am_title, (int)w - 40, t, sizeof(t));
    gx_text1(F_MED, x + 20, y + 28, t, RGBA(255, 255, 255, 255));
    int top = am_sel - vis / 2;
    if (top > am_n - vis) top = am_n - vis;
    if (top < 0) top = 0;
    static f32 sel_anim = -2000;
    f32 sy = smooth_to(&sel_anim, y + 42 + (am_sel - top) * rh);
    gx_rrect_fill(x + 12, sy, w - 24, rh - 3, 6, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
    for (int i = top; i < am_n && i < top + vis; i++) {
        f32 ry = y + 42 + (i - top) * rh;
        gp_zone(x + 12, ry, w - 24, rh - 3, &am_sel, i, GPZ_SEL);
        fit_text(F_MED, am_txt[i], (int)w - 50, t, sizeof(t));
        col_t c = am_code[i] == AC_DELLIST || am_code[i] == AC_REMOVE || am_code[i] == AC_TITLES_CLEAR ? RGBA(255, 180, 180, 255) : RGBA(235, 228, 255, 255);
        int tw = gx_text1(F_MED, x + 26, ry + 17, t, i == am_sel ? RGBA(255, 255, 255, 255) : c);
        if (am_code[i] == AC_RATE && am_track >= 0)
            draw_stars(x + 26 + tw + 6, ry + (rh - 3) / 2, 6.0f, rating[am_track], RGBA(255, 210, 90, 255), RGBA(110, 96, 150, 255));
    }
    gx_text_c(F_SMALL, 320, y + h - 6, "A : choisir     B : fermer", RGBA(255, 210, 120, 255));
}

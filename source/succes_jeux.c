/* GC Station v268 : succes dans les jeux GameCube (inclus dans main.c, avant natif.c).
   Un fichier par jeu : SD:/gc-mp3/succes/<ID>.txt, une ligne par succes :
       Titre | Description | condition [| images]
   condition : m8(adresse), m16(adresse) ou m32(adresse) compare a une valeur (== != >= <= > <), plusieurs reunies
   par && ; pointeur : m16(p32(0x80xxxxxx)+0x10) ; images : la condition doit rester vraie N images de suite (30 par
   defaut). Pulse verifie les conditions une fois par image pendant le jeu (natres.c, ach_run) ; un succes n'est
   obtenu qu'apres avoir vu sa condition fausse au moins une fois (pas d'un coup au demarrage).
   Quelques jeux ont des succes fournis (sj_builtin) : ecrits d'apres des adresses connues des codes de triche,
   PAS VERIFIES SUR CONSOLE ; le fichier est cree sur la carte au 1er lancement du jeu et peut etre modifie.
   Obtenus : mes-parties/succes-jeux.txt (ou profils/<nom>/succes-jeux.txt) : "ID|titre|date". */
#define SJ_DIR    GCD "/succes"
#define SJ_LANCE  GCD "/natif/succes-lance.txt"           /* (au lancement : profil, jeu, titres dans l'ordre de Pulse) */
#define SJ_MAX    48
#define SJ_TERMS  8
typedef struct { char title[48], desc[96]; u32 need; int nt; u32 t[SJ_TERMS][4]; } sj_def_t;

static const char sj_head[] =
    "# Succes GC Station pour ce jeu. Une ligne par succes :  Titre | Description | condition [| images]\n"
    "# condition : m8(adresse), m16(adresse) ou m32(adresse) compare (== != >= <= > <) a une valeur ; plusieurs : &&\n"
    "#   pointeur : m16(p32(0x80xxxxxx)+0x10) ; images : vraie N images de suite (30 par defaut, 60 = 1 s)\n"
    "# Pulse les verifie pendant le jeu ; un succes n'est obtenu qu'apres avoir vu sa condition fausse une fois.\n";
static const struct { const char *id, *text; } sj_builtin[] = {
    { "GZLE01",
      "# The Legend of Zelda : The Wind Waker (USA). Adresses des codes de triche connus : PAS ENCORE VERIFIE SUR CONSOLE.\n"
      "Bourse bien garnie | Avoir 200 rubis | m16(0x803C4C0C) >= 200\n"
      "Grand portefeuille | Obtenir le portefeuille de 1000 rubis | m8(0x803C4C1A) >= 1\n"
      "Fortune | Avoir 1000 rubis | m16(0x803C4C0C) >= 1000\n"
      "Coeur vaillant | Avoir 4 coeurs | m8(0x803C4C09) >= 16\n"
      "Coeur de heros | Avoir 10 coeurs | m8(0x803C4C09) >= 40\n" },
    { "GMPE01",
      "# Mario Party 4 (USA). Adresses des codes de triche connus : PAS ENCORE VERIFIE SUR CONSOLE.\n"
      "Tirelire | Joueur 1 : avoir 50 pieces | m16(0x8018FC54) >= 50\n"
      "Premiere etoile | Joueur 1 : obtenir une etoile | m16(0x8018FC62) >= 1 && m16(0x8018FC62) <= 99\n"
      "Superstar | Joueur 1 : avoir 5 etoiles | m16(0x8018FC62) >= 5 && m16(0x8018FC62) <= 99\n" },
    { "GMSE01",
      "# Super Mario Sunshine (USA). Adresse du code de triche des vies : PAS ENCORE VERIFIE SUR CONSOLE.\n"
      "Vies de rechange | Avoir 10 vies | m32(0x80578A04) >= 10 && m32(0x80578A04) <= 99\n"
      "Collection de vies | Avoir 30 vies | m32(0x80578A04) >= 30 && m32(0x80578A04) <= 99\n" },
};
#define SJ_NB ((int)(sizeof(sj_builtin) / sizeof(sj_builtin[0])))

static char *sj_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}
static int sj_num(const char **p, u32 *v) {               /* 0x... ou decimal */
    const char *s = *p;
    while (*s == ' ') s++;
    char *e;
    unsigned long x = strtoul(s, &e, 0);
    if (e == s) return 0;
    *v = (u32)x; *p = e;
    return 1;
}
static int sj_lit(const char **p, const char *w) {         /* mot attendu (espaces avant ignores) */
    const char *s = *p;
    while (*s == ' ') s++;
    size_t n = strlen(w);
    if (strncasecmp(s, w, n)) return 0;
    *p = s + n;
    return 1;
}
/* un terme : m16(0x803C4C0C) >= 200 ou m16(p32(0x80xxxxxx)+0x10) == 3 */
static int sj_term(const char *s, u32 *t) {
    u32 sz = 0, ptr = 0, ad = 0, off = 0, val = 0, cmp;
    if (sj_lit(&s, "m8(")) sz = 1; else if (sj_lit(&s, "m16(")) sz = 2; else if (sj_lit(&s, "m32(")) sz = 4; else return 0;
    if (sj_lit(&s, "p32(")) {
        ptr = 1;
        if (!sj_num(&s, &ad) || !sj_lit(&s, ")")) return 0;
        if (sj_lit(&s, "+") && !sj_num(&s, &off)) return 0;
    } else if (!sj_num(&s, &ad)) return 0;
    if (!sj_lit(&s, ")")) return 0;
    /* (pour Pulse : masque des resultats qui rendent le terme vrai : bit 0 plus petit, bit 1 egal, bit 2 plus grand) */
    if (sj_lit(&s, "==")) cmp = 2; else if (sj_lit(&s, "!=")) cmp = 5; else if (sj_lit(&s, ">=")) cmp = 6;
    else if (sj_lit(&s, "<=")) cmp = 3; else if (sj_lit(&s, ">")) cmp = 4; else if (sj_lit(&s, "<")) cmp = 1; else return 0;
    if (!sj_num(&s, &val)) return 0;
    while (*s == ' ') s++;
    if (*s) return 0;
    if (!ptr && (ad < 0x80000000u || ad >= 0x81800000u)) return 0;
    if (ptr && (ad < 0x80000000u || ad >= 0x81800000u || (ad & 3))) return 0;
    t[0] = ptr << 24 | sz << 16 | cmp << 8; t[1] = ad; t[2] = off; t[3] = val;
    return 1;
}
/* une ligne -> un succes (0 : commentaire / vide, -1 : erreur) */
static int sj_line(char *l, sj_def_t *d) {
    char *s = sj_trim(l);
    if (!*s || *s == '#') return 0;
    char *f[4] = { s, NULL, NULL, NULL };
    int nf = 1;
    for (char *p = s; *p && nf < 4; p++) if (*p == '|') { *p = 0; f[nf++] = p + 1; }
    if (nf < 3) return -1;
    memset(d, 0, sizeof(*d));
    snprintf(d->title, sizeof(d->title), "%s", sj_trim(f[0]));
    snprintf(d->desc, sizeof(d->desc), "%s", sj_trim(f[1]));
    d->need = 30;
    if (nf == 4) { u32 v = 0; const char *q = sj_trim(f[3]); sj_lit(&q, "images"); if (sj_num(&q, &v) && v) d->need = v; }
    char *c = f[2];
    while (c && d->nt < SJ_TERMS) {
        char *a = strstr(c, "&&");
        if (a) *a = 0;
        if (!sj_term(sj_trim(c), d->t[d->nt])) return -1;
        d->nt++;
        c = a ? a + 2 : NULL;
    }
    return d->title[0] && d->nt ? 1 : -1;
}
static const char *sj_builtin_text(const char *id) {
    for (int i = 0; i < SJ_NB; i++) if (!strncmp(sj_builtin[i].id, id, 6)) return sj_builtin[i].text;
    return NULL;
}
/* succes d'un jeu (fichier de la carte, cree d'apres sj_builtin s'il n'existe pas) ; rend le nombre, *bad = lignes refusees */
static int sj_load(const char *id, sj_def_t *out, int cap, int *bad) {
    char path[96];
    snprintf(path, sizeof(path), SJ_DIR "/%.6s.txt", id);
    if (bad) *bad = 0;
    long n = 0;
    char *t = sd_ok ? (char *)read_file(path, 64 * 1024, &n) : NULL;
    if (!t) {
        const char *b = sj_builtin_text(id);
        if (!b) return 0;
        if (sd_ok) {                                       /* (sur la carte : modifiable, et lisible par le telephone) */
            mkdir(SJ_DIR, 0777);
            FILE *f = fopen(path, "wb");
            if (f) { fputs(sj_head, f); fputs(b, f); fclose(f); }
        }
        n = (long)strlen(b);
        t = (char *)malloc(n + 1);
        if (!t) return 0;
        memcpy(t, b, n);
    } else {
        char *q = (char *)realloc(t, n + 1);
        if (!q) { free(t); return 0; }
        t = q;
    }
    t[n] = 0;
    int k = 0;
    for (char *l = t; l && *l && k < cap; ) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        int r = sj_line(l, &out[k]);
        if (r > 0) k++; else if (r < 0 && bad) (*bad)++;
        l = e ? e + 1 : NULL;
    }
    free(t);
    return k;
}

/* ---------- au lancement (natif.c) : section "ACH1" pour Pulse ---------- */
static u32 na_ach[2 + SJ_MAX * (5 + SJ_TERMS * 4)];
static int na_ach_w = 0, na_ach_n = 0, na_ach_bad = 0;
static void sj_prepare(const char *id) {
    na_ach_w = na_ach_n = na_ach_bad = 0;
    static sj_def_t d[SJ_MAX];
    int n = sj_load(id, d, SJ_MAX, &na_ach_bad);
    if (n <= 0) return;
    u32 *w = na_ach;
    *w++ = 0x41434831u;                                   /* "ACH1" */
    *w++ = (u32)n;
    for (int i = 0; i < n; i++) {
        *w++ = (u32)d[i].nt; *w++ = d[i].need; *w++ = 0; *w++ = 0; *w++ = 0;
        for (int k = 0; k < d[i].nt; k++) for (int j = 0; j < 4; j++) *w++ = d[i].t[k][j];
    }
    na_ach_w = (int)(w - na_ach);
    na_ach_n = n;
    FILE *f = fopen(SJ_LANCE, "wb");                      /* (pour le retour : quels titres, quel profil) */
    if (f) {
        fprintf(f, "%s\n%.6s\n", gp_name, id);
        for (int i = 0; i < n; i++) fprintf(f, "%s\n", d[i].title);
        fclose(f);
    }
}

/* ---------- au retour (traduction du journal) ---------- */
static u32 sj_new_t = 0;                                  /* (date des derniers succes obtenus : notification du telephone) */
static char sj_new_txt[200] = "";
static void sj_got_path(const char *prof, char *o, int cap) {
    if (prof && prof[0]) snprintf(o, cap, GCD_PROF "/%s/succes-jeux.txt", prof); else snprintf(o, cap, GCD_PARTIES "/succes-jeux.txt");
}
static int sj_have(const char *gotf, const char *id, const char *title) {
    long n = 0;
    char *t = (char *)read_file(gotf, 128 * 1024, &n);
    if (!t) return 0;
    char key[64];
    int kl = snprintf(key, sizeof(key), "%.6s|%s|", id, title), found = 0;
    for (long i = 0; i + kl <= n && !found; ) {
        if (!memcmp(t + i, key, kl)) found = 1;
        while (i < n && t[i] != '\n') i++;
        i++;
    }
    free(t);
    return found;
}
/* sec = 6e secteur du journal ("GCSA") ; f = journal en texte (section ajoutee) */
static void sj_after_game(const u8 *sec, FILE *f) {
    long n = 0;
    char *t = (char *)read_file(SJ_LANCE, 32 * 1024, &n);
    if (!t) return;
    char *q = (char *)realloc(t, n + 1);
    if (!q) { free(t); return; }
    t = q; t[n] = 0;
    char *line[2 + SJ_MAX];
    int nl = 0;
    for (char *l = t; l && nl < 2 + SJ_MAX; ) { char *e = strchr(l, '\n'); if (e) *e = 0; line[nl++] = l; l = e ? e + 1 : NULL; }
    if (nl < 2) { free(t); return; }
    const char *prof = line[0], *id = line[1];
    char gotf[160];
    sj_got_path(prof, gotf, sizeof(gotf));
    u32 cnt = be32(sec + 4), hz = id[3] == 'P' ? 50u : 60u;
    int nnew = 0, nok = 0;
    char last[48] = "";
    if (f) fprintf(f, "\n========== Succes (%u surveilles par Pulse) ==========\n", (unsigned)cnt);
    for (u32 i = 0; i < cnt && i < 63 && (int)i + 2 < nl; i++) {
        u32 st = be32(sec + 8 + i * 8), fr = be32(sec + 12 + i * 8);
        const char *ti = line[2 + i];
        if (f) fprintf(f, "  %-40s %s", ti, st & 2 ? "OBTENU" : st & 1 ? "pas encore" : "pas surveille (condition jamais fausse ?)");
        if (st & 2) {
            nok++;
            if (f) fprintf(f, " (apres %u min %02u s de jeu)", (unsigned)(fr / hz / 60), (unsigned)(fr / hz % 60));
            if (!sj_have(gotf, id, ti)) {
                if (prof[0]) { char d[96]; snprintf(d, sizeof(d), GCD_PROF "/%s", prof); mkdir(GCD_PROF, 0777); mkdir(d, 0777); }
                else mkdir(GCD_PARTIES, 0777);
                FILE *g = fopen(gotf, "ab");
                if (g) { fprintf(g, "%.6s|%s|%u\n", id, ti, (unsigned)time(NULL)); fclose(g); }
                nnew++;
                snprintf(last, sizeof(last), "%s", ti);
                if (f) fprintf(f, "  <- NOUVEAU");
            }
        }
        if (f) fputc('\n', f);
    }
    if (nnew) {
        sj_new_t = (u32)time(NULL);
        if (nnew == 1) snprintf(sj_new_txt, sizeof(sj_new_txt), "Succ\xC3\xA8s obtenu : %s", last);
        else snprintf(sj_new_txt, sizeof(sj_new_txt), "%d succ\xC3\xA8s obtenus (dernier : %s)", nnew, last);
        lib_note(sj_new_txt);
    }
    (void)nok;
    free(t);
}

/* ---------- liste (START > Outils > Succes) : succes des jeux qui en ont ---------- */
typedef struct { char id[8], title[48], desc[96]; u8 got; } sj_row_t;
static sj_row_t *sj_rows = NULL;
static int sj_nrows = 0;
static void sj_list_load(void) {
    free(sj_rows); sj_rows = NULL; sj_nrows = 0;
    if (!sd_ok) return;
    for (int i = 0; i < SJ_NB; i++) {                      /* (les jeux fournis : fichiers crees s'ils manquent) */
        char p[96]; struct stat st;
        snprintf(p, sizeof(p), SJ_DIR "/%s.txt", sj_builtin[i].id);
        if (stat(p, &st) != 0) { int b; static sj_def_t tmp[SJ_MAX]; sj_load(sj_builtin[i].id, tmp, SJ_MAX, &b); }
    }
    DIR *dd = opendir(SJ_DIR);
    if (!dd) return;
    char gotf[160];
    sj_got_path(gp_name, gotf, sizeof(gotf));
    struct dirent *e;
    static sj_def_t d[SJ_MAX];
    int cap = 0;
    while ((e = readdir(dd)) != NULL) {
        const char *nm = e->d_name;
        if (strlen(nm) != 10 || strcasecmp(nm + 6, ".txt")) continue;
        char id[8]; memcpy(id, nm, 6); id[6] = 0;
        int b, n = sj_load(id, d, SJ_MAX, &b);
        for (int i = 0; i < n; i++) {
            if (sj_nrows >= cap) { int nc = cap ? cap * 2 : 32; sj_row_t *r = (sj_row_t *)realloc(sj_rows, nc * sizeof(sj_row_t)); if (!r) break; sj_rows = r; cap = nc; }
            sj_row_t *r = &sj_rows[sj_nrows++];
            snprintf(r->id, sizeof(r->id), "%s", id);
            snprintf(r->title, sizeof(r->title), "%s", d[i].title);
            snprintf(r->desc, sizeof(r->desc), "%s", d[i].desc);
            r->got = (u8)sj_have(gotf, id, d[i].title);
        }
    }
    closedir(dd);
}

/* (fil du serveur du telephone) [["ID","titre","description",obtenu (0/1)],...] : fichiers seulement, rien de partage */
static char *sj_json(void) {
    sbuf_t s = { NULL, 0, 0 };
    sb_add(&s, "[");
    DIR *dd = sd_ok ? opendir(SJ_DIR) : NULL;
    char gotf[160];
    sj_got_path(gp_name, gotf, sizeof(gotf));
    sj_def_t *d = (sj_def_t *)malloc(SJ_MAX * sizeof(sj_def_t));
    int first = 1;
    struct dirent *e;
    while (dd && d && (e = readdir(dd)) != NULL) {
        const char *nm = e->d_name;
        if (strlen(nm) != 10 || strcasecmp(nm + 6, ".txt")) continue;
        char id[8]; memcpy(id, nm, 6); id[6] = 0;
        int b, n = sj_load(id, d, SJ_MAX, &b);
        for (int i = 0; i < n; i++) {
            char u8[300];
            sb_add(&s, first ? "[" : ",["); first = 0;
            sb_json_str(&s, id); sb_add(&s, ",");
            tp_any_utf8(d[i].title, u8, sizeof(u8)); sb_json_str(&s, u8); sb_add(&s, ",");
            tp_any_utf8(d[i].desc, u8, sizeof(u8)); sb_json_str(&s, u8);
            sb_add(&s, sj_have(gotf, id, d[i].title) ? ",1]" : ",0]");
        }
    }
    if (dd) closedir(dd);
    free(d);
    sb_add(&s, "]");
    return s.b;
}

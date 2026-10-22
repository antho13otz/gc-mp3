// GC-MP3 v25 : editeur de cheats Swiss (inclus dans main.c, apres tabs_extra.c)
// Swiss lit les cheats dans SD:/swiss/cheats/<ID du jeu>.txt, par exemple GALE01.txt :
//     Nom du cheat
//     XXXXXXXX YYYYYYYY        (une ou plusieurs lignes de code Action Replay / Gecko)
//     (ligne vide entre deux cheats)
// Ouverture : onglet FICHIERS > X > Cheats Swiss, START > Cheats Swiss, ou un .txt
// de SD:/swiss/cheats ouvert dans l'onglet FICHIERS.
// Le fichier est garde ligne par ligne : ce que l'editeur ne comprend pas reste intact.

#define CH_DIR      SD_ROOT "/swiss/cheats"
#define CH_MAXL     3000                    /* lignes par fichier */
#define CH_LW       128                     /* longueur d'une ligne */
#define CH_MAXF     300                     /* fichiers dans le dossier */
#define CH_MAXC     400                     /* cheats par fichier */

enum { CHV_LIST, CHV_FILE, CHV_CODES };
static int ch_view = CHV_LIST;
static char (*ch_fn)[64] = NULL;           /* fichiers .txt du dossier */
static long *ch_fsz = NULL;
static int ch_nf = 0, ch_fsel = 0, ch_fconfirm = 0;
static char (*ch_l)[CH_LW] = NULL;         /* lignes du fichier ouvert */
static int ch_nl = 0, ch_crlf = 0, ch_dirty = 0, ch_quit_warn = 0;
static char ch_path[NAME_LEN], ch_name[64];
static int ch_cs[CH_MAXC], ch_ce[CH_MAXC], ch_nc = 0;    /* cheat = lignes [cs, ce[ */
static int ch_csel = 0, ch_lsel = 0;
static int ch_edit = 0, ch_dig = 0;        /* edition chiffre par chiffre */
static int ch_pop = 0, ch_psel = 0, ch_pconfirm = 0;
static char ch_msg[160];
static u64 ch_msg_t = 0;

static void ch_note(const char *m) { snprintf(ch_msg, sizeof(ch_msg), "%s", TR(m)); ch_msg_t = now_ms(); }

static int ch_hex(int c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
/* ligne de code : 8 chiffres hexa, espace, 8 chiffres hexa (espaces autour acceptes) */
static int ch_is_code(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    for (int i = 0; i < 8; i++) if (!ch_hex((u8)s[i])) return 0;
    if (s[8] != ' ' && s[8] != '\t') return 0;
    const char *v = s + 9;
    while (*v == ' ' || *v == '\t') v++;
    for (int i = 0; i < 8; i++) if (!ch_hex((u8)v[i])) return 0;
    v += 8;
    while (*v == ' ' || *v == '\t' || *v == '\r') v++;
    return *v == 0;
}
static int ch_blank(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\r') s++; return *s == 0; }
/* ressemble a une ligne de code (commence par un chiffre hexa et ne fait presque que de l'hexa) */
static int ch_codeish(const char *s) {
    int hx = 0, other = 0;
    for (; *s; s++) { if (ch_hex((u8)*s)) hx++; else if (*s != ' ' && *s != '\t' && *s != '\r') other++; }
    return hx >= 8 && other == 0;
}

/* decoupe le fichier en cheats : un bloc = lignes non vides entre deux lignes vides */
static void ch_index(void) {
    ch_nc = 0;
    int i = 0;
    while (i < ch_nl && ch_nc < CH_MAXC) {
        while (i < ch_nl && ch_blank(ch_l[i])) i++;
        if (i >= ch_nl) break;
        int s = i;
        while (i < ch_nl && !ch_blank(ch_l[i])) i++;
        ch_cs[ch_nc] = s; ch_ce[ch_nc] = i; ch_nc++;
    }
    if (ch_csel > ch_nc) ch_csel = ch_nc;
    if (ch_csel < 0) ch_csel = 0;
}
static int ch_ncodes(int c, int *bad) {
    int n = 0; if (bad) *bad = 0;
    int odd = 0, odd_hex = 0;
    for (int i = ch_cs[c]; i < ch_ce[c]; i++) {
        if (ch_is_code(ch_l[i])) n++;
        else if (i > ch_cs[c]) { odd++; if (ch_codeish(ch_l[i])) odd_hex++; }
    }
    /* ligne fausse : pas du code, dans un cheat qui a du code (l'en-tete ID + nom du jeu n'en a pas) */
    if (bad) *bad = n ? odd : odd_hex;
    return n;
}
/* nom d'un cheat : sa premiere ligne si ce n'est pas du code */
static const char *ch_cname(int c) {
    if (ch_is_code(ch_l[ch_cs[c]])) return "(sans nom)";
    return ch_l[ch_cs[c]];
}
/* premiere ligne de code (ou de contenu apres le nom) d'un cheat */
static int ch_first_line(int c) { return ch_is_code(ch_l[ch_cs[c]]) ? ch_cs[c] : ch_cs[c] + 1; }

static int ch_fcmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static void ch_scan(void) {
    if (!ch_fn) { ch_fn = malloc(sizeof(*ch_fn) * CH_MAXF); ch_fsz = malloc(sizeof(long) * CH_MAXF); }
    ch_nf = 0;
    if (!ch_fn || !ch_fsz || !sd_ok) return;
    mkdir(SD_ROOT "/swiss", 0777);
    mkdir(CH_DIR, 0777);
    DIR *d = opendir(CH_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && ch_nf < CH_MAXF) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcasecmp(dot, ".txt") || strlen(e->d_name) >= 64) continue;
        snprintf(ch_fn[ch_nf], 64, "%s", e->d_name);
        ch_nf++;
    }
    closedir(d);
    qsort(ch_fn, ch_nf, 64, ch_fcmp);
    for (int i = 0; i < ch_nf; i++) {
        char p[NAME_LEN]; struct stat st;
        snprintf(p, sizeof(p), "%s/%s", CH_DIR, ch_fn[i]);
        ch_fsz[i] = stat(p, &st) == 0 ? (long)st.st_size : 0;
    }
    if (ch_fsel > ch_nf) ch_fsel = ch_nf;
}

static int ch_load(const char *path) {
    if (!ch_l) ch_l = malloc(sizeof(*ch_l) * CH_MAXL);
    if (!ch_l) { ch_note("M\xC3\xA9moire insuffisante"); return 0; }
    long len = 0;
    unsigned char *d = read_file(path, 1024 * 1024, &len);
    ch_nl = 0; ch_crlf = 0;
    if (d) {
        long i = 0;
        if (len >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) i = 3;     /* (BOM UTF-8) */
        while (i < len && ch_nl < CH_MAXL) {
            long s = i;
            while (i < len && d[i] != '\n') i++;
            long n = i - s;
            if (n > 0 && d[s + n - 1] == '\r') { n--; ch_crlf = 1; }
            if (n > CH_LW - 1) n = CH_LW - 1;
            memcpy(ch_l[ch_nl], d + s, n); ch_l[ch_nl][n] = 0;
            ch_nl++;
            i++;
        }
        if (len > 0 && d[len - 1] == '\n' && ch_nl > 0 && !ch_l[ch_nl - 1][0]) { /* (derniere ligne vide = fin du fichier) */ }
        free(d);
        if (ch_nl >= CH_MAXL) ch_note("Fichier trop long : seul le d\xC3\xA9" "but est modifiable");
    }
    snprintf(ch_path, sizeof(ch_path), "%s", path);
    snprintf(ch_name, sizeof(ch_name), "%s", base_of(path));
    ch_dirty = 0; ch_quit_warn = 0; ch_csel = 0; ch_lsel = 0; ch_edit = 0; ch_pop = 0;
    ch_index();
    return 1;
}

static int ch_save(void) {
    if (!ch_l || !ch_path[0]) return 0;
    char tmp[NAME_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", ch_path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { ch_note("Impossible d'\xC3\xA9" "crire sur la carte SD"); return 0; }
    int n = ch_nl;
    while (n > 0 && ch_blank(ch_l[n - 1])) n--;               /* pas de lignes vides a la fin */
    int ok = 1;
    for (int i = 0; i < n && ok; i++) {
        if (fputs(ch_l[i], f) < 0 || fputs(ch_crlf ? "\r\n" : "\n", f) < 0) ok = 0;
    }
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); ch_note("\xC3\x89" "criture incompl\xC3\xA8te (carte pleine ?)"); return 0; }
    remove(ch_path);
    if (rename(tmp, ch_path) != 0) { ch_note("Impossible de remplacer le fichier"); return 0; }
    ch_dirty = 0; ch_quit_warn = 0;
    ch_note("Enregistr\xC3\xA9 : Swiss l'utilisera au prochain lancement du jeu");
    return 1;
}

/* insere une ligne avant la position at */
static int ch_ins(int at, const char *s) {
    if (ch_nl >= CH_MAXL) { ch_note("Fichier plein (3000 lignes)"); return 0; }
    if (at < 0) at = 0;
    if (at > ch_nl) at = ch_nl;
    memmove(ch_l[at + 1], ch_l[at], (size_t)(ch_nl - at) * CH_LW);
    snprintf(ch_l[at], CH_LW, "%s", s);
    ch_nl++; ch_dirty = 1;
    return 1;
}
static void ch_del(int at, int n) {
    if (at < 0 || n <= 0 || at >= ch_nl) return;
    if (at + n > ch_nl) n = ch_nl - at;
    memmove(ch_l[at], ch_l[at + n], (size_t)(ch_nl - at - n) * CH_LW);
    ch_nl -= n; ch_dirty = 1;
}

static void cheats_open(void) {                 /* liste des fichiers */
    ch_on = 1; ch_view = CHV_LIST; ch_fconfirm = 0; ch_pop = 0;
    tab = TAB_FILES;
    ch_scan();
    crumb("cheats");
}
static void cheats_open_file(const char *path) {
    ch_on = 1;
    tab = TAB_FILES;
    if (ch_load(path)) ch_view = CHV_FILE;
    else { ch_view = CHV_LIST; ch_scan(); }
}

/* -------- clavier -------- */
static int ch_kb_target = -1;                  /* ligne a remplacer (-1 = nouvelle) */
static void ch_norm_code(const char *t, char *out, int cap) {
    char hx[17]; int n = 0;
    for (const char *p = t; *p && n < 16; p++) if (ch_hex((u8)*p)) hx[n++] = (char)toupper((u8)*p);
    hx[n] = 0;
    int other = 0;
    for (const char *p = t; *p; p++) if (!ch_hex((u8)*p) && *p != ' ' && *p != '\t') other++;
    if (n == 16 && !other) snprintf(out, cap, "%.8s %.8s", hx, hx + 8);
    else snprintf(out, cap, "%s", t);                         /* garde tel quel (en rouge si faux) */
}
static void cheats_kb_done(int mode, const char *t) {
    if (mode == KB_CHEATID) {
        char id[64]; int n = 0;
        for (const char *p = t; *p && n < 40; p++) {
            char c = *p;
            if (isalnum((u8)c) || c == '_' || c == '-') id[n++] = (char)toupper((u8)c);
        }
        id[n] = 0;
        if (!n) { ch_note("Tape l'ID du jeu (6 caract\xC3\xA8res, ex. GALE01)"); return; }
        char path[NAME_LEN]; struct stat st;
        snprintf(path, sizeof(path), "%s/%s.txt", CH_DIR, id);
        if (stat(path, &st) != 0) {
            mkdir(SD_ROOT "/swiss", 0777); mkdir(CH_DIR, 0777);
            FILE *f = fopen(path, "wb");
            if (!f) { ch_note("Impossible d'\xC3\xA9" "crire sur la carte SD"); return; }
            fclose(f);
        }
        cheats_open_file(path);
        if (n != 6) ch_note("Attention : un ID de jeu GameCube fait 6 caract\xC3\xA8res (ex. GALE01)");
        return;
    }
    if (!ch_l) return;
    if (mode == KB_CHEATNAME) {
        char nm[CH_LW];
        snprintf(nm, sizeof(nm), "%s", t);
        if (ch_kb_target < 0) {                                /* nouveau cheat a la fin */
            int at = ch_nl;
            while (at > 0 && ch_blank(ch_l[at - 1])) at--;
            ch_nl = at;
            if (at > 0) ch_ins(ch_nl, "");
            ch_ins(ch_nl, nm);
            ch_ins(ch_nl, "00000000 00000000");
            ch_index();
            ch_csel = ch_nc - 1;
            ch_view = CHV_CODES; ch_lsel = 0; ch_edit = 1; ch_dig = 0;
            ch_note("Nouveau cheat : modifie la ligne avec la croix, A pour finir");
        } else {
            if (ch_is_code(ch_l[ch_kb_target])) ch_ins(ch_kb_target, nm);   /* (pas encore de nom) */
            else { snprintf(ch_l[ch_kb_target], CH_LW, "%s", nm); ch_dirty = 1; }
            ch_index();
        }
        return;
    }
    if (mode == KB_CHEATCODE) {
        char ln[CH_LW];
        ch_norm_code(t, ln, sizeof(ln));
        if (ch_kb_target >= 0 && ch_kb_target < ch_nl) { snprintf(ch_l[ch_kb_target], CH_LW, "%s", ln); ch_dirty = 1; }
        else if (ch_csel < ch_nc) { ch_ins(ch_ce[ch_csel], ln); }
        ch_index();
        if (!ch_is_code(ln)) ch_note("Ligne pas au format XXXXXXXX YYYYYYYY (en rouge)");
    }
}

/* -------- actions -------- */
enum { CHP_RENAME, CHP_DUP, CHP_DEL, CHP_UP, CHP_DOWN, CHP_COUNT };
static const char *const chp_names[CHP_COUNT] = { "Renommer", "Dupliquer", "Supprimer", "Monter", "Descendre" };
enum { CHL_ADD, CHL_TYPE, CHL_DEL, CHL_DUP, CHL_COUNT };
static const char *const chl_names[CHL_COUNT] = { "Ajouter une ligne", "Taper la ligne au clavier", "Supprimer la ligne", "Dupliquer la ligne" };

/* deplace le cheat c d'un cran (d = -1 / +1) */
static void ch_move(int c, int d) {
    int o = c + d;
    if (o < 0 || o >= ch_nc) return;
    int a = d < 0 ? o : c, b = d < 0 ? c : o;           /* a avant b */
    int as = ch_cs[a], ae = ch_ce[a], bs = ch_cs[b], be = ch_ce[b];
    int na = ae - as, nb = be - bs, gap = bs - ae;
    int tot = be - as;
    char (*tmp)[CH_LW] = malloc((size_t)tot * CH_LW);
    if (!tmp) return;
    memcpy(tmp, ch_l[bs], (size_t)nb * CH_LW);
    memcpy(tmp + nb, ch_l[ae], (size_t)gap * CH_LW);
    memcpy(tmp + nb + gap, ch_l[as], (size_t)na * CH_LW);
    memcpy(ch_l[as], tmp, (size_t)tot * CH_LW);
    free(tmp);
    ch_dirty = 1;
    ch_index();
    ch_csel = o;
}

static void ch_file_action(int a) {
    int c = ch_csel;
    if (c >= ch_nc) { ch_pop = 0; return; }
    switch (a) {
    case CHP_RENAME:
        ch_kb_target = ch_cs[c];
        kb_start(KB_CHEATNAME, ch_is_code(ch_l[ch_cs[c]]) ? "" : ch_l[ch_cs[c]], -1, -1);
        break;
    case CHP_DUP: {
        int n = ch_ce[c] - ch_cs[c];
        if (ch_nl + n + 1 >= CH_MAXL) { ch_note("Fichier plein (3000 lignes)"); break; }
        int at = ch_ce[c];
        ch_ins(at, "");
        for (int i = 0; i < n; i++) ch_ins(at + 1 + i, ch_l[ch_cs[c] + i]);
        if (!ch_is_code(ch_l[at + 1])) { char nm[CH_LW]; snprintf(nm, sizeof(nm), "%.110s (copie)", ch_l[at + 1]); snprintf(ch_l[at + 1], CH_LW, "%s", nm); }
        ch_index(); ch_csel = c + 1;
        break; }
    case CHP_DEL:
        if (!ch_pconfirm) { ch_pconfirm = 1; return; }
        ch_pconfirm = 0;
        {   int s = ch_cs[c], e = ch_ce[c];
            while (e < ch_nl && ch_blank(ch_l[e])) e++;           /* avec les lignes vides qui suivent */
            ch_del(s, e - s);
            ch_index();
            ch_note("Cheat supprim\xC3\xA9 (Y pour enregistrer)"); }
        break;
    case CHP_UP: ch_move(c, -1); break;
    case CHP_DOWN: ch_move(c, 1); break;
    }
    ch_pop = 0;
}

static void ch_line_action(int a) {
    int c = ch_csel;
    if (c >= ch_nc) { ch_pop = 0; return; }
    int f = ch_first_line(c), n = ch_ce[c] - f, li = f + ch_lsel;
    switch (a) {
    case CHL_ADD:
        ch_ins(ch_lsel < n ? li + 1 : ch_ce[c], "00000000 00000000");
        ch_index();
        if (ch_lsel < n) ch_lsel++;
        ch_edit = 1; ch_dig = 0;
        break;
    case CHL_TYPE:
        ch_kb_target = ch_lsel < n ? li : -1;
        kb_start(KB_CHEATCODE, ch_lsel < n ? ch_l[li] : "", -1, -1);
        break;
    case CHL_DEL:
        if (ch_lsel >= n) break;
        if (ch_ce[c] - ch_cs[c] <= 1) { ch_note("Derni\xC3\xA8re ligne : supprime plut\xC3\xB4t le cheat"); break; }
        ch_del(li, 1);
        ch_index();
        if (ch_lsel > 0 && ch_lsel >= ch_ce[c] - ch_first_line(c)) ch_lsel--;
        break;
    case CHL_DUP:
        if (ch_lsel >= n) break;
        ch_ins(li + 1, ch_l[li]); ch_index(); ch_lsel++;
        break;
    }
    ch_pop = 0;
}

/* change le chiffre hexa sous le curseur (d = +1 / -1) */
static void ch_digit_step(char *ln, int dig, int d) {
    char nrm[CH_LW];
    if (!ch_is_code(ln)) { ch_norm_code(ln, nrm, sizeof(nrm)); if (!ch_is_code(nrm)) snprintf(nrm, sizeof(nrm), "00000000 00000000"); snprintf(ln, CH_LW, "%s", nrm); }
    else { ch_norm_code(ln, nrm, sizeof(nrm)); snprintf(ln, CH_LW, "%s", nrm); }
    int pos = dig < 8 ? dig : dig + 1;
    char c = ln[pos];
    int v = c >= 'A' ? c - 'A' + 10 : c >= 'a' ? c - 'a' + 10 : c - '0';
    v = (v + d + 16) & 15;
    ln[pos] = "0123456789ABCDEF"[v];
    ch_dirty = 1;
}

static void ch_leave_file(void) {
    if (ch_dirty && !ch_quit_warn) { ch_quit_warn = 1; ch_note("Pas enregistr\xC3\xA9 : Y pour enregistrer, B encore pour quitter sans enregistrer"); return; }
    ch_view = CHV_LIST; ch_quit_warn = 0; ch_dirty = 0;
    ch_scan();
}

static void cheats_input(u32 down, u32 nav, int fire) {
    if (ch_pop) {                                         /* petite fenetre d'actions */
        int cnt = ch_view == CHV_FILE ? CHP_COUNT : CHL_COUNT;
        if (fire && (nav & PAD_BUTTON_DOWN)) { ch_psel = (ch_psel + 1) % cnt; ch_pconfirm = 0; }
        if (fire && (nav & PAD_BUTTON_UP)) { ch_psel = (ch_psel + cnt - 1) % cnt; ch_pconfirm = 0; }
        if (down & PAD_BUTTON_A) { if (ch_view == CHV_FILE) ch_file_action(ch_psel); else ch_line_action(ch_psel); }
        if (down & (PAD_BUTTON_B | PAD_BUTTON_X)) { ch_pop = 0; ch_pconfirm = 0; }
        return;
    }
    if (ch_view == CHV_LIST) {
        int rows = ch_nf + 1;
        if (fire && (nav & PAD_BUTTON_DOWN)) { ch_fsel++; ch_fconfirm = 0; }
        if (fire && (nav & PAD_BUTTON_UP)) { ch_fsel--; ch_fconfirm = 0; }
        if (fire && (nav & PAD_BUTTON_RIGHT)) ch_fsel += 12;
        if (fire && (nav & PAD_BUTTON_LEFT)) ch_fsel -= 12;
        if (ch_fsel > rows - 1) ch_fsel = rows - 1;
        if (ch_fsel < 0) ch_fsel = 0;
        if (down & PAD_BUTTON_A) {
            ch_fconfirm = 0;
            if (ch_fsel == 0) kb_start(KB_CHEATID, "", -1, -1);
            else { char p[NAME_LEN]; snprintf(p, sizeof(p), "%s/%s", CH_DIR, ch_fn[ch_fsel - 1]); cheats_open_file(p); }
        }
        if ((down & PAD_BUTTON_X) && ch_fsel > 0) {
            if (!ch_fconfirm) { ch_fconfirm = 1; ch_note("X encore une fois pour supprimer ce fichier"); }
            else {
                char p[NAME_LEN]; snprintf(p, sizeof(p), "%s/%s", CH_DIR, ch_fn[ch_fsel - 1]);
                ch_fconfirm = 0;
                if (remove(p) == 0) ch_note("Fichier effac\xC3\xA9"); else ch_note("Impossible d'effacer ce fichier");
                ch_scan();
            }
        }
        if (down & PAD_BUTTON_B) { ch_on = 0; fe_load(); }
        return;
    }
    if (ch_view == CHV_FILE) {
        int rows = ch_nc + 1;
        if (fire && (nav & PAD_BUTTON_DOWN)) ch_csel++;
        if (fire && (nav & PAD_BUTTON_UP)) ch_csel--;
        if (fire && (nav & PAD_BUTTON_RIGHT)) ch_csel += 11;
        if (fire && (nav & PAD_BUTTON_LEFT)) ch_csel -= 11;
        if (ch_csel > rows - 1) ch_csel = rows - 1;
        if (ch_csel < 0) ch_csel = 0;
        if (fire && (nav & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT))) ch_quit_warn = 0;
        if (down & PAD_BUTTON_A) {
            if (ch_csel >= ch_nc) { ch_kb_target = -1; kb_start(KB_CHEATNAME, "", -1, -1); }
            else { ch_view = CHV_CODES; ch_lsel = 0; ch_edit = 0; }
        }
        if ((down & PAD_BUTTON_X) && ch_csel < ch_nc) { ch_pop = 1; ch_psel = 0; ch_pconfirm = 0; }
        if (down & PAD_BUTTON_Y) ch_save();
        if (down & PAD_BUTTON_B) ch_leave_file();
        return;
    }
    /* CHV_CODES : les lignes d'un cheat */
    if (ch_csel >= ch_nc) { ch_view = CHV_FILE; return; }
    int f = ch_first_line(ch_csel), n = ch_ce[ch_csel] - f;
    if (ch_edit) {                                        /* croix : chiffre par chiffre */
        if (ch_lsel >= n) { ch_edit = 0; return; }
        char *ln = ch_l[f + ch_lsel];
        if (fire && (nav & PAD_BUTTON_LEFT)) ch_dig = (ch_dig + 15) % 16;
        if (fire && (nav & PAD_BUTTON_RIGHT)) ch_dig = (ch_dig + 1) % 16;
        if (fire && (nav & PAD_BUTTON_UP)) ch_digit_step(ln, ch_dig, 1);
        if (fire && (nav & PAD_BUTTON_DOWN)) ch_digit_step(ln, ch_dig, -1);
        if (down & (PAD_BUTTON_A | PAD_BUTTON_B)) ch_edit = 0;
        if (down & PAD_BUTTON_Y) { ch_edit = 0; ch_save(); }
        return;
    }
    int rows = n + 1;
    if (fire && (nav & PAD_BUTTON_DOWN)) ch_lsel++;
    if (fire && (nav & PAD_BUTTON_UP)) ch_lsel--;
    if (ch_lsel > rows - 1) ch_lsel = rows - 1;
    if (ch_lsel < 0) ch_lsel = 0;
    if (down & PAD_BUTTON_A) {
        if (ch_lsel >= n) ch_line_action(CHL_ADD);
        else if (ch_is_code(ch_l[f + ch_lsel]) || ch_codeish(ch_l[f + ch_lsel])) { ch_edit = 1; ch_dig = 0; }
        else { ch_kb_target = f + ch_lsel; kb_start(KB_CHEATCODE, ch_l[f + ch_lsel], -1, -1); }
    }
    if (down & PAD_BUTTON_X) { ch_pop = 1; ch_psel = 0; ch_pconfirm = 0; }
    if (down & PAD_BUTTON_Y) ch_save();
    if (down & PAD_BUTTON_B) ch_view = CHV_FILE;
}

/* -------- affichage -------- */
static void ch_draw_code(f32 x, f32 base, const char *ln, int cursor, col_t c, col_t cc) {
    /* chiffres a pas fixe, pour que le curseur tombe pile */
    const f32 step = 14;
    char one[2] = { 0, 0 };
    int k = 0;
    for (int i = 0; ln[i] && i < 17; i++) {
        if (ln[i] == ' ') { x += step; continue; }
        one[0] = ln[i];
        if (k == cursor) {
            gx_rrect_fill(x - 2, base - 17, step, 22, 4, RGBA(255, 210, 120, 255), RGBA(230, 160, 60, 255));
            gx_text1(F_MED, x, base, one, RGBA(30, 16, 60, 255));
        } else gx_text1(F_MED, x, base, one, c);
        x += step; k++;
    }
    (void)cc;
}

static void draw_cheats(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    draw_panel(11, 73, 618, 338);
    int tx = draw_tabs(NULL);
    const char *where = ch_view == CHV_LIST ? "Cheats Swiss : SD:/swiss/cheats" : ch_name;
    if (ch_view == CHV_CODES && ch_csel < ch_nc) {
        text_display(ch_cname(ch_csel), d, sizeof(d));
        snprintf(buf, sizeof(buf), "%s  >  %s", ch_name, d);
        fit_text(F_SMALL, buf, 600 - tx, d, sizeof(d));
        where = d;
    }
    gx_text1(F_SMALL, tx + 6, 99, where, RGBA(200, 190, 240, 255));
    if (ch_view != CHV_LIST && ch_dirty) gx_text_r(F_SMALL, 614, 99, "modifi\xC3\xA9", RGBA(255, 210, 120, 255));
    const col_t sel1 = RGBA(116, 46, 236, 255), sel2 = RGBA(74, 22, 172, 255);
    const int vis = 11;
    if (ch_view == CHV_LIST) {
        int rows = ch_nf + 1, start = ch_fsel - vis / 2;
        if (start > rows - vis) start = rows - vis;
        if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < rows; i++) {
            int r = start + i;
            f32 ry = 114.0f + i * 24.0f, cy = ry + 12.0f;
            int on = r == ch_fsel;
            gp_zone(18, ry + 1, 604, 22, &ch_fsel, r, GPZ_SEL);
            if (on) gx_rrect_fill(20, ry + 1, 600, 22, 5, sel1, sel2);
            col_t txt = on ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
            if (r == 0) { gx_text1(F_MED, 34, cy + 5, "+ Nouveau fichier (ID du jeu, ex. GALE01)", on ? txt : RGBA(255, 210, 120, 255)); continue; }
            draw_note(36, cy, on ? txt : RGBA(120, 100, 190, 255));
            text_display(ch_fn[r - 1], d, sizeof(d));
            fit_text(F_MED, d, 470, buf, sizeof(buf));
            gx_text1(F_MED, 52, cy + 5, buf, txt);
            fmt_size(buf, sizeof(buf), ch_fsz[r - 1]);
            gx_text_r(F_SMALL, 612, cy + 4, buf, on ? RGBA(235, 225, 255, 255) : RGBA(140, 125, 200, 255));
        }
        if (!ch_nf) {
            gx_text1(F_SMALL, 34, 172, "Aucun fichier de cheats pour l'instant.", RGBA(170, 155, 225, 255));
            gx_text1(F_SMALL, 34, 190, "Un fichier par jeu, nomm\xC3\xA9 avec l'ID du jeu (affich\xC3\xA9 par Swiss), ex. GALE01.txt", RGBA(170, 155, 225, 255));
        }
    } else if (ch_view == CHV_FILE) {
        int rows = ch_nc + 1, start = ch_csel - vis / 2;
        if (start > rows - vis) start = rows - vis;
        if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < rows; i++) {
            int r = start + i;
            f32 ry = 114.0f + i * 24.0f, cy = ry + 12.0f;
            int on = r == ch_csel;
            gp_zone(18, ry + 1, 604, 22, &ch_csel, r, GPZ_SEL);
            if (on) gx_rrect_fill(20, ry + 1, 600, 22, 5, sel1, sel2);
            col_t txt = on ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
            if (r == ch_nc) { gx_text1(F_MED, 34, cy + 5, "+ Ajouter un cheat", on ? txt : RGBA(255, 210, 120, 255)); continue; }
            int bad = 0, nc = ch_ncodes(r, &bad);
            text_display(ch_cname(r), d, sizeof(d));
            fit_text(F_MED, d, 440, buf, sizeof(buf));
            gx_text1(F_MED, 34, cy + 5, buf, txt);
            if (!nc && r == 0 && !bad) snprintf(d, sizeof(d), "en-t\xC3\xAAte (ID et nom du jeu)");
            else if (bad) snprintf(d, sizeof(d), "%d ligne(s) fausse(s)", bad);
            else snprintf(d, sizeof(d), nc == 1 ? "1 ligne" : "%d lignes", nc);
            gx_text_r(F_SMALL, 612, cy + 4, d, bad ? RGBA(255, 140, 140, 255) : on ? RGBA(235, 225, 255, 255) : RGBA(140, 125, 200, 255));
        }
        if (!ch_nc) gx_text1(F_SMALL, 34, 172, "Fichier vide : A sur \xC2\xAB + Ajouter un cheat \xC2\xBB", RGBA(170, 155, 225, 255));
    } else if (ch_csel < ch_nc) {
        int f = ch_first_line(ch_csel), n = ch_ce[ch_csel] - f;
        int rows = n + 1, start = ch_lsel - vis / 2;
        if (start > rows - vis) start = rows - vis;
        if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < rows; i++) {
            int r = start + i;
            f32 ry = 114.0f + i * 24.0f, cy = ry + 12.0f;
            int on = r == ch_lsel;
            gp_zone(18, ry + 1, 604, 22, &ch_lsel, r, GPZ_SEL);
            if (on) gx_rrect_fill(20, ry + 1, 600, 22, 5, ch_edit ? RGBA(70, 40, 140, 255) : sel1, ch_edit ? RGBA(50, 26, 110, 255) : sel2);
            col_t txt = on ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
            if (r == n) { gx_text1(F_MED, 34, cy + 5, "+ Ajouter une ligne", on ? txt : RGBA(255, 210, 120, 255)); continue; }
            const char *ln = ch_l[f + r];
            snprintf(buf, sizeof(buf), "%d", r + 1);
            gx_text_r(F_SMALL, 52, cy + 4, buf, RGBA(140, 125, 200, 255));
            if (ch_is_code(ln) || (on && ch_edit)) {
                char nrm[CH_LW];
                ch_norm_code(ln, nrm, sizeof(nrm));
                ch_draw_code(66, cy + 6, nrm, on && ch_edit ? ch_dig : -1, txt, txt);
            } else {
                text_display(ln, d, sizeof(d));
                fit_text(F_MED, d, 530, buf, sizeof(buf));
                gx_text1(F_MED, 66, cy + 5, buf, (ch_codeish(ln) || ch_ncodes(ch_csel, NULL)) ? RGBA(255, 140, 140, 255) : RGBA(190, 220, 255, 255));
            }
        }
        if (ch_edit) gx_text1(F_SMALL, 330, 404 - 18, "Gauche / Droite : chiffre   Haut / Bas : valeur", RGBA(190, 180, 235, 255));
    }
    if (now_ms() - ch_msg_t < 4000) gx_text1(F_SMALL, 30, 404, ch_msg, RGBA(255, 210, 120, 255));
    if (ch_pop) {
        int cnt = ch_view == CHV_FILE ? CHP_COUNT : CHL_COUNT;
        const char *const *nm = ch_view == CHV_FILE ? chp_names : chl_names;
        const f32 w = 290, h = 46 + cnt * 24, x = 330, y = 130;
        gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
        draw_panel(x, y, w, h);
        for (int a = 0; a < cnt; a++) {
            f32 ry = y + 14 + a * 24;
            gp_zone(x + 10, ry, w - 20, 22, &ch_psel, a, GPZ_SEL);
            if (a == ch_psel) gx_rrect_fill(x + 10, ry, w - 20, 22, 5, sel1, sel2);
            int conf = ch_view == CHV_FILE && a == CHP_DEL && ch_pconfirm && a == ch_psel;
            gx_text1(F_MED, x + 22, ry + 17, conf ? "Confirmer : A pour supprimer" : nm[a], conf ? RGBA(255, 160, 160, 255) : RGBA(240, 232, 255, 255));
        }
        gx_text_c(F_SMALL, x + w / 2, y + h - 8, "A : valider   B : fermer", RGBA(255, 210, 120, 255));
    }
    if (ch_view == CHV_LIST) {
        static const char *const k[] = { "A", "X", "B" };
        static const char *const l[] = { "OUVRIR", "SUPPRIMER", "RETOUR" };
        draw_legend_keys(k, l, 3);
    } else if (ch_view == CHV_CODES && ch_edit) {
        static const char *const k[] = { "A", "Y" };
        static const char *const l[] = { "FINI", "ENREGISTRER" };
        draw_legend_keys(k, l, 2);
    } else {
        static const char *const k[] = { "A", "X", "Y", "B" };
        static const char *const l[] = { "MODIFIER", "ACTIONS", "ENREGISTRER", "RETOUR" };
        draw_legend_keys(k, l, 4);
    }
}

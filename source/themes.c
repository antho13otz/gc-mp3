// GC Station v233 : themes a telecharger (START > Reglages > Affichage > Themes en ligne). Inclus dans main.c
// apres memview.c. La liste est le fichier themes/index.txt du depot des mises a jour (GitHub, sans mettre a
// jour l'appli) ; une ligne par theme :
//   id|Nom|description|accent1|accent2|aurore1,aurore2,aurore3,aurore4|fond_haut|fond_bas|rubans|image|base
// couleurs en hexadecimal (ff7a3c), rubans = intensite des aurores en % (100 = normal), image = fichier .jpg
// du dossier themes/ (fond d'ecran, facultatif), base = theme de GC Station pour le reste (indigo, ocean...).
// Le theme choisi est garde dans SD:/gc-mp3/themes/actif.txt (marche sans reseau). Styles Aurore, Vitrine, Ilots.
// (thd_t et thd : declares en haut de main.c, utilises par ui_aurore.c)

#define THM_DIR GCD "/themes"
#define THM_MAX 32
static thd_t thm_list[THM_MAX];
static char thm_desc[THM_MAX][100];
static char thm_line[THM_MAX][300];
static int thm_n = 0, thm_sel = 0, thm_top = 0;
enum { THM_IDLE, THM_LIST, THM_READY, THM_IMG, THM_ERR };
static int thm_state = THM_IDLE;
static char thm_msg[140];
static thd_t thm_saved;                                   /* (le theme d'avant : B l'annule) */
static int thm_saved_base = 0;
static char thm_img_path[NAME_LEN];
static hfetch_t thm_hf = { .c = { -1, 0, 0 } };

static int thm_hex(const char *s, u8 *rgb) {
    unsigned v;
    while (*s == ' ' || *s == '#') s++;
    if (sscanf(s, "%6x", &v) != 1) return 0;
    rgb[0] = (u8)(v >> 16); rgb[1] = (u8)(v >> 8); rgb[2] = (u8)v;
    return 1;
}
/* une ligne de index.txt -> theme (desc : facultatif) */
static int thm_parse(const char *line, thd_t *t, char *desc, int dcap) {
    char buf[300], *f[11];
    snprintf(buf, sizeof(buf), "%s", line);
    buf[strcspn(buf, "\r\n")] = 0;
    if (!buf[0] || buf[0] == '#') return 0;
    int n = 0;
    char *p = buf;
    while (n < 11) { f[n++] = p; char *q = strchr(p, '|'); if (!q) break; *q = 0; p = q + 1; }
    if (n < 9) return 0;
    memset(t, 0, sizeof(*t));
    snprintf(t->id, sizeof(t->id), "%s", trim(f[0]));
    snprintf(t->name, sizeof(t->name), "%s", trim(f[1]));
    if (desc) snprintf(desc, dcap, "%s", trim(f[2]));
    for (const char *c = t->id; *c; c++) if (!isalnum((u8)*c) && *c != '-' && *c != '_') return 0;   /* (nom de fichier sur) */
    if (!t->id[0] || !thm_hex(f[3], t->a) || !thm_hex(f[4], t->b)) return 0;
    char *m = f[5];
    for (int i = 0; i < 4; i++) {
        if (!thm_hex(m, t->mix[i])) return 0;
        m = strchr(m, ',');
        if (!m && i < 3) return 0;
        if (m) m++;
    }
    if (!thm_hex(f[6], t->top) || !thm_hex(f[7], t->bot)) return 0;
    t->rib = atoi(f[8]);
    if (t->rib < 0) t->rib = 0;
    if (t->rib > 250) t->rib = 250;
    if (n > 9) { snprintf(t->img, sizeof(t->img), "%s", trim(f[9])); for (char *c = t->img; *c; c++) if (*c == '/' || *c == '\\') { t->img[0] = 0; break; } }
    t->base = -1;
    if (n > 10) { const char *b = trim(f[10]); for (int i = 0; i < THEME_COUNT; i++) if (!strcasecmp(b, theme_keys[i])) t->base = i; }
    t->on = 1;
    return 1;
}
static void thm_base_url(char *out, int cap, const char *file) {
#ifndef GEKKO
    const char *e = getenv("PCSIM_THEMES");                /* (simulateur : serveur local) */
    if (e && *e) { snprintf(out, cap, "%s/%s", e, file); return; }
#endif
    snprintf(out, cap, "https://raw.githubusercontent.com/%s/main/themes/%s", upd_repo, file);
}
static int thm_wall_is_theme(void) { return wall_on && strstr(wall_path, "/gc-mp3/themes/") != NULL; }

/* appliquer (sans rien enregistrer) : couleurs, theme de base */
static void thm_apply(const thd_t *t) {
    if (t && t->on) { thd = *t; if (t->base >= 0) theme_apply(t->base); }
    else { thd.on = 0; }
}
/* au demarrage (apres les reglages) */
static void thm_boot(void) {
    if (!sd_ok) return;
    FILE *f = fopen(THM_DIR "/actif.txt", "r");
    if (!f) return;
    char l[300];
    thd_t t;
    if (fgets(l, sizeof(l), f) && thm_parse(l, &t, NULL, 0)) { thd = t; crumb("theme en ligne : %s", t.id); }
    fclose(f);
}
static void thm_load_cache(void) {
    thm_n = 0;
    FILE *f = sd_ok ? fopen(THM_DIR "/index.txt", "r") : NULL;
    if (!f) return;
    char l[300];
    while (thm_n < THM_MAX && fgets(l, sizeof(l), f))
        if (thm_parse(l, &thm_list[thm_n], thm_desc[thm_n], sizeof(thm_desc[0]))) { snprintf(thm_line[thm_n], sizeof(thm_line[0]), "%s", l); thm_line[thm_n][strcspn(thm_line[thm_n], "\r\n")] = 0; thm_n++; }
    fclose(f);
}
static void thm_fetch_list(void) {
    if (net_phase != NET_DONE) { net_start_async(); snprintf(thm_msg, sizeof(thm_msg), "R\xC3\xA9seau en cours de connexion..."); thm_state = THM_LIST; return; }
    if (!net_ok) { thm_state = THM_ERR; snprintf(thm_msg, sizeof(thm_msg), "Pas de r\xC3\xA9seau : th\xC3\xA8mes d\xC3\xA9j\xC3\xA0 vus seulement"); return; }
    char url[300];
    thm_base_url(url, sizeof(url), "index.txt");
    thm_hf.maxbytes = 64 * 1024;
    thm_hf.image = 0;
    thm_hf.accept = "text/plain";
    hf_start(&thm_hf, url, NULL);
    thm_state = THM_LIST;
    snprintf(thm_msg, sizeof(thm_msg), "Liste des th\xC3\xA8mes...");
}
static void thm_open(void) {
    menu_open = 0;
    tl_screen = TL_THEMES;
    thm_saved = thd;
    thm_saved_base = theme_id;
    thm_load_cache();
    thm_sel = 0;
    for (int i = 0; i < thm_n; i++) if (thd.on && !strcmp(thm_list[i].id, thd.id)) thm_sel = i + 1;
    thm_top = 0;
    thm_msg[0] = 0;
    thm_fetch_list();
}
static void thm_close(int keep) {
    if (thm_hf.st != HF_IDLE) { hf_abort(&thm_hf); hf_drop(&thm_hf); thm_hf.st = HF_IDLE; }
    if (!keep) { thm_apply(&thm_saved); thd = thm_saved; theme_apply(thm_saved_base); }
    thm_state = THM_IDLE;
    tl_screen = TL_NONE;
    menu_open = 1;                                         /* (retour aux reglages) */
}
static void thm_preview(void) {
    if (thm_sel == 0) { thd.on = 0; theme_apply(thm_saved_base); }
    else thm_apply(&thm_list[thm_sel - 1]);
}
/* garder le theme choisi (et son image, deja telechargee) */
static void thm_keep(void) {
    if (thm_sel == 0) {
        remove(THM_DIR "/actif.txt");
        thd.on = 0;
        if (thm_wall_is_theme()) { wall_on = 0; wall_reload(); settings_dirty = 1; }
        theme_apply(thm_saved_base);
        lib_note("Th\xC3\xA8me d'origine remis");
        thm_close(1);
        return;
    }
    const thd_t *t = &thm_list[thm_sel - 1];
    mkdir(THM_DIR, 0777);
    FILE *f = fopen(THM_DIR "/actif.txt", "w");
    if (f) { fprintf(f, "%s\n", thm_line[thm_sel - 1]); fclose(f); }
    thm_apply(t);
    if (t->base >= 0) settings_dirty = 1;                  /* (le theme de base est un reglage) */
    if (t->img[0] && thm_img_path[0]) { snprintf(wall_path, sizeof(wall_path), "%s", thm_img_path); wall_on = 1; wall_reload(); settings_dirty = 1; }
    else if (!t->img[0] && thm_wall_is_theme()) { wall_on = 0; wall_reload(); settings_dirty = 1; }
    char m[120];
    snprintf(m, sizeof(m), "Th\xC3\xA8me : %s", t->name);
    lib_note(m);
    crumb("theme en ligne choisi : %s", t->id);
    thm_close(1);
}
static void thm_choose(void) {
    if (thm_state == THM_IMG) return;
    thm_img_path[0] = 0;
    if (thm_sel == 0) { thm_keep(); return; }
    const thd_t *t = &thm_list[thm_sel - 1];
    if (!t->img[0]) { thm_keep(); return; }
    snprintf(thm_img_path, sizeof(thm_img_path), THM_DIR "/%s", t->img);
    struct stat st;
    if (stat(thm_img_path, &st) == 0 && st.st_size > 0) { thm_keep(); return; }   /* (deja telechargee) */
    if (net_phase != NET_DONE || !net_ok) { snprintf(thm_msg, sizeof(thm_msg), "Pas de r\xC3\xA9seau pour l'image du th\xC3\xA8me"); thm_img_path[0] = 0; return; }
    char url[300];
    thm_base_url(url, sizeof(url), t->img);
    thm_hf.maxbytes = 2 * 1024 * 1024;
    thm_hf.image = 0;
    thm_hf.accept = "image/*";
    hf_start(&thm_hf, url, NULL);
    thm_state = THM_IMG;
    snprintf(thm_msg, sizeof(thm_msg), "T\xC3\xA9l\xC3\xA9" "chargement de l'image de fond...");
}
static void thm_pump(void) {
    if (thm_state == THM_LIST && thm_hf.st == HF_IDLE && net_phase == NET_DONE) { thm_fetch_list(); return; }   /* (le reseau est pret) */
    if (thm_state != THM_LIST && thm_state != THM_IMG) return;
    if (thm_hf.st == HF_IDLE) return;
    hf_pump(&thm_hf);
    if (thm_hf.st == HF_FAIL) {
        hf_drop(&thm_hf); thm_hf.st = HF_IDLE;
        if (thm_state == THM_IMG) { thm_state = THM_READY; thm_img_path[0] = 0; snprintf(thm_msg, sizeof(thm_msg), "Image du th\xC3\xA8me introuvable : r\xC3\xA9" "essaie"); return; }
        thm_state = thm_n ? THM_READY : THM_ERR;
        snprintf(thm_msg, sizeof(thm_msg), thm_n ? "Liste pas \xC3\xA0 jour (GitHub injoignable)" : "Liste des th\xC3\xA8mes introuvable (GitHub injoignable)");
        return;
    }
    if (thm_hf.st != HF_DONE) return;
    thm_hf.st = HF_IDLE;
    if (thm_state == THM_IMG) {
        mkdir(THM_DIR, 0777);
        FILE *f = fopen(thm_img_path, "wb");
        int ok = f && thm_hf.buf && fwrite(thm_hf.buf, 1, thm_hf.len, f) == (size_t)thm_hf.len;
        if (f && fclose(f) != 0) ok = 0;
        hf_drop(&thm_hf);
        thm_state = THM_READY;
        if (!ok) { remove(thm_img_path); thm_img_path[0] = 0; snprintf(thm_msg, sizeof(thm_msg), "Image impossible \xC3\xA0 enregistrer (carte SD pleine ?)"); return; }
        thm_keep();
        return;
    }
    /* la liste */
    char cur[24] = "";
    if (thm_sel > 0 && thm_sel <= thm_n) snprintf(cur, sizeof(cur), "%s", thm_list[thm_sel - 1].id);
    int n = 0;
    if (thm_hf.buf && thm_hf.len > 0) {
        char *txt = (char *)malloc(thm_hf.len + 1);
        if (txt) {
            memcpy(txt, thm_hf.buf, thm_hf.len); txt[thm_hf.len] = 0;
            char *l = txt;
            while (l && *l && n < THM_MAX) {
                char *e = strchr(l, '\n'); if (e) *e = 0;
                if (thm_parse(l, &thm_list[n], thm_desc[n], sizeof(thm_desc[0]))) { snprintf(thm_line[n], sizeof(thm_line[0]), "%s", l); thm_line[n][strcspn(thm_line[n], "\r")] = 0; n++; }
                l = e ? e + 1 : NULL;
            }
            if (n && sd_ok) {
                mkdir(THM_DIR, 0777);
                FILE *f = fopen(THM_DIR "/index.txt", "w");
                if (f) { for (int i = 0; i < n; i++) fprintf(f, "%s\n", thm_line[i]); fclose(f); }
            }
            free(txt);
        }
    }
    hf_drop(&thm_hf);
    if (!n) { thm_state = thm_n ? THM_READY : THM_ERR; snprintf(thm_msg, sizeof(thm_msg), "Liste des th\xC3\xA8mes vide ou illisible"); return; }
    thm_n = n;
    thm_sel = 0;
    for (int i = 0; i < n; i++) if (cur[0] ? !strcmp(thm_list[i].id, cur) : (thd.on && !strcmp(thm_list[i].id, thd.id))) thm_sel = i + 1;
    thm_state = THM_READY;
    snprintf(thm_msg, sizeof(thm_msg), "%d th\xC3\xA8mes", n);
}

static void thm_input(u32 down, u32 nav, int fire) {
    int rows = thm_n + 1;
    if (fire && (nav & PAD_BUTTON_UP)) { thm_sel = (thm_sel + rows - 1) % rows; thm_preview(); }
    if (fire && (nav & PAD_BUTTON_DOWN)) { thm_sel = (thm_sel + 1) % rows; thm_preview(); }
    if (down & PAD_BUTTON_A) thm_choose();
    if (down & PAD_BUTTON_Y) thm_fetch_list();
    if (down & PAD_BUTTON_B) thm_close(0);
}

static void draw_thm(void) {
    thm_pump();
    /* a gauche : la liste ; a droite : le fond se voit (apercu en direct) */
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 60), RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 60), RGBA(0, 0, 0, 0));
    draw_panel(24, 40, 300, 372);
    gx_text1(F_LARGE, 42, 74, "Th\xC3\xA8mes en ligne", RGBA(255, 255, 255, 255));
    gx_text1(F_SMALL, 42, 94, thm_msg[0] ? thm_msg : "", thm_state == THM_ERR ? RGBA(255, 190, 140, 255) : RGBA(170, 155, 225, 255));
    int rows = thm_n + 1, vis = 11;
    if (thm_sel < thm_top) thm_top = thm_sel;
    if (thm_sel >= thm_top + vis) thm_top = thm_sel - vis + 1;
    for (int r = 0; r < vis && thm_top + r < rows; r++) {
        int i = thm_top + r;
        f32 y = 108 + r * 26;
        int on = i == thm_sel;
        if (on) gx_rrect_fill(34, y, 280, 24, 6, TL_SEL1, TL_SEL2);
        const char *nm = i == 0 ? "Aurore d'origine" : thm_list[i - 1].name;
        if (i > 0) {                                         /* pastille aux couleurs du theme */
            const thd_t *t = &thm_list[i - 1];
            gx_rrect_fill(44, y + 7, 22, 10, 5, RGBA(t->a[0], t->a[1], t->a[2], 255), RGBA(t->b[0], t->b[1], t->b[2], 255));
        } else gx_rrect_fill(44, y + 7, 22, 10, 5, RGBA(255, 70, 170, 255), RGBA(70, 110, 255, 255));
        gx_text1(F_MED, 76, y + 18, nm, on ? RGBA(255, 255, 255, 255) : RGBA(225, 215, 250, 255));
        int cur = i == 0 ? !thm_saved.on : (thm_saved.on && !strcmp(thm_saved.id, thm_list[i - 1].id));
        if (cur) gx_text_r(F_SMALL, 306, y + 17, "actuel", RGBA(150, 230, 170, 255));
    }
    /* a droite : un apercu (aurores, selection, bouton) aux couleurs du theme montre */
    {
        f32 bx = 344, by = 112, bw = 272;
        gx_rrect_fill(bx, by, bw, 168, 12, RGBA(0, 0, 0, 110), RGBA(0, 0, 0, 140));
        gx_text1(F_SMALL, bx + 14, by + 22, "Aper\xC3\xA7u", RGBA(200, 190, 235, 255));
        int sr = nv_raw; nv_raw = 1;                     /* (couleurs exactes du theme) */
        gx_set_flat();
        for (int i = 0; i < 24; i++) {                    /* bande des aurores */
            f32 x0 = bx + 14 + (bw - 28) * i / 24.0f, x1 = bx + 14 + (bw - 28) * (i + 1) / 24.0f;
            col_t c0 = au_mix(i / 24.0f, 255), c1 = au_mix((i + 1) / 24.0f, 255);
            gx_quad4(x0, by + 34, x1 + 0.5f, by + 48, c0, c1, c1, c0);
        }
        au_pill_h(bx + 14, by + 60, bw - 28, 26, 9, au_a(235), au_b(235));   /* ligne choisie */
        gx_text1(F_MED, bx + 26, by + 79, "Morceau choisi", RGBA(255, 255, 255, 255));
        gx_text1(F_MED, bx + 26, by + 108, "Autre morceau", RGBA(225, 215, 250, 255));
        au_pill_h(bx + 14, by + 124, 110, 28, 14, au_a(255), au_b(255));     /* bouton */
        gx_text_c(F_SMALL, bx + 69, by + 143, "Lecture", RGBA(255, 255, 255, 255));
        gx_rrect_fill(bx + 136, by + 136, bw - 150, 6, 3, RGBA(60, 50, 90, 255), RGBA(60, 50, 90, 255));
        au_pill_h(bx + 136, by + 136, (bw - 150) * 0.6f, 6, 3, au_a(255), au_b(255));   /* barre de lecture */
        nv_raw = sr;
    }
    /* a droite : nom et description du theme montre */
    const char *nm = thm_sel == 0 ? "Aurore d'origine" : thm_list[thm_sel - 1].name;
    const char *ds = thm_sel == 0 ? "Les couleurs de GC Station (rose, violet, bleu)." : thm_desc[thm_sel - 1];
    gx_rrect_fill(344, 300, 272, 84, 10, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120));
    gx_text1(F_MED, 358, 324, nm, RGBA(255, 255, 255, 255));
    {   /* description sur deux lignes */
        char l1[120], l2[120]; int k = 0, cut = 0;
        snprintf(l1, sizeof(l1), "%s", ds); l2[0] = 0;
        while (l1[k]) {
            if (l1[k] == ' ') { char t[120]; memcpy(t, l1, k); t[k] = 0; if (text_width(F_SMALL, t, 0) > 244) break; cut = k; }
            k++;
        }
        if (l1[k] && cut > 0) { snprintf(l2, sizeof(l2), "%s", l1 + cut + 1); l1[cut] = 0; }
        gx_text1(F_SMALL, 358, 346, l1, RGBA(210, 200, 240, 255));
        gx_text1(F_SMALL, 358, 364, l2, RGBA(210, 200, 240, 255));
    }
    if (thm_sel > 0 && thm_list[thm_sel - 1].img[0]) gx_text1(F_SMALL, 358, 380, "+ une image de fond (t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9" "e avec A)", RGBA(255, 210, 120, 255));
    if (ui_skin < 3) gx_text1(F_SMALL, 344, 290, "(couleurs pour les styles Aurore, Vitrine et \xC3\x8Elots)", RGBA(255, 200, 140, 255));
    static const char *const k[] = { "A", "B", "Y" }, *const lb[] = { "CHOISIR", "ANNULER", "ACTUALISER" };
    tl_legend(k, lb, 3);
}

// GC Station v376 : accueil de l'onglet JEUX en Aurore (« A la une »). Inclus dans main.c (apres recap.c).
// En haut a gauche, le dernier jeu en grand (sa jaquette de posters.pak, son titre, son temps de jeu, A : le lancer) ;
// a droite, Jeux GameCube, Applis (avant : cachees dans l'espace jeux, touche Z), Cartes memoire, Mes stats (le recap) ;
// en bas, les mini-jeux sur une rangee qui defile. Les autres styles gardent l'accueil d'avant (tabs_extra.c).
// Rien n'est lu sur la carte SD pendant le dessin : la jaquette, les titres, le nombre de jeux et d'applis sont lus par le
// fil de fond (gj_post_fn), une fois en entrant dans l'onglet.

typedef struct {
    char path[NAME_LEN];                               /* (le dernier jeu) */
    char title[96], top[96], topid[8], id[8];
    int ngames, napps;
    char apn[2][32];
    u8 *buf; int iw, ih;                               /* jaquette (CMPR 256 x 256) */
} jhd_t;
static jhd_t jhw;                                      /* (rempli par le fil de fond) */
static struct {
    volatile int st;                                   /* 0 a lire, 1 en cours, 2 pret */
    jhd_t d;                                           /* (ce qui est affiche : copie faite dans la boucle) */
    char path[NAME_LEN];
    u8 *tex; GXTexObj obj; int iw, ih;
} jh;
static int jh_z = 0;                                   /* 0 le dernier jeu, 1 a 4 les tuiles, 100 + g un mini-jeu */
static f32 jh_off = -1.0f;                             /* (premiere case de la rangee montree, glisse) */

/* fil de fond : liste des jeux gardee (titre du dernier jeu, du plus joue, nombre), applis de SD:/apps, jaquette */
static void jh_job(void) {
    jhw.title[0] = jhw.top[0] = jhw.id[0] = 0; jhw.ngames = -1; jhw.napps = -1; jhw.apn[0][0] = jhw.apn[1][0] = 0;
    jhw.buf = NULL;
    long n = 0;
    char *d = (char *)read_file(GL_LIST, 2 * 1024 * 1024, &n);
    if (d) {
        int cnt = 0;
        for (long i = 0; i < n; ) {
            long s = i;
            while (i < n && d[i] != '\n') i++;
            long e = i++;
            if (e - s < 8 || d[s] == '#' || !memcmp(d + s, "GLC1", 4)) continue;
            cnt++;
            char *f[7]; int nf = 0; long p = s;                /* chemin, taille, ID, disque, ciso, titre, description */
            f[nf++] = d + s;
            for (long q = s; q < e && nf < 7; q++) if (d[q] == '\t') f[nf++] = d + q + 1;
            if (nf < 6) continue;
            int pl = (int)(f[1] - f[0]) - 1, tl = (int)((nf > 6 ? f[6] - 1 : d + e) - f[5]);
            (void)p;
            if (tl > 95) tl = 95;
            if (pl == (int)strlen(jhw.path) && !memcmp(f[0], jhw.path, pl)) {
                memcpy(jhw.title, f[5], tl); jhw.title[tl] = 0;
                int il = (int)(f[3] - f[2]) - 1; if (il > 6) il = 6; memcpy(jhw.id, f[2], il); jhw.id[il] = 0;
            }
            if (jhw.topid[0] && !jhw.top[0] && !memcmp(f[2], jhw.topid, 6)) { memcpy(jhw.top, f[5], tl); jhw.top[tl] = 0; }
        }
        jhw.ngames = cnt;
        free(d);
    }
    DIR *ad = opendir(SD_ROOT "/apps");                   /* applis : dossiers de SD:/apps (sans lire leurs images) */
    if (ad) {
        struct dirent *e; int c = 0;
        while ((e = readdir(ad)) != NULL && c < 500) {
            if (e->d_name[0] == '.') continue;
            if (c < 2) snprintf(jhw.apn[c], sizeof(jhw.apn[c]), "%s", e->d_name);
            c++;
        }
        closedir(ad);
        jhw.napps = c;
    }
    if (!jhw.id[0]) return;
    FILE *fp = fopen(GL_UI "/posters.pak", "rb");          /* jaquette : comme swpk_open / swpk_find, sans garder le pack ouvert */
    if (!fp) return;
    u8 h[64];
    u8 *idx = NULL;
    if (fread(h, 1, 64, fp) == 64 && !memcmp(h, "SWPK", 4) && be32(h + 4) == 1 && h[0x2D] == 14 && be16(h + 0x24) == 256 && be16(h + 0x26) == 256) {
        int np = (int)be32(h + 0x0C);
        u32 il = be32(h + 0x14);
        if (np >= 1 && np <= 4096 && be32(h + 0x10) == 64 && il == (u32)np * 32 && (idx = (u8 *)malloc(il)) && fread(idx, 1, il, fp) == il) {
            int r = -1, lo = 0, hi = np - 1;
            while (lo <= hi && r < 0) { int m = (lo + hi) / 2, c = memcmp(idx + m * 32, jhw.id, 6); if (!c) r = m; else if (c < 0) lo = m + 1; else hi = m - 1; }
            for (int i = 0; r < 0 && i < np; i++) if (!memcmp(idx + i * 32, jhw.id, 4) && (idx[i * 32 + 6] & 1)) r = i;
            if (r >= 0) {
                u8 *b = (u8 *)memalign(32, 32768);
                if (b && fseek(fp, (long)be32(idx + r * 32 + 8), SEEK_SET) == 0 && fread(b, 1, 32768, fp) == 32768) {
                    DCFlushRange(b, 32768);
                    jhw.iw = be16(h + 0x28); jhw.ih = be16(h + 0x2A);
                    if (jhw.iw <= 0 || jhw.iw > 256) jhw.iw = 256;
                    if (jhw.ih <= 0 || jhw.ih > 256) jhw.ih = 256;
                    jhw.buf = b;
                } else free(b);
            }
        }
    }
    free(idx);
    fclose(fp);
}
static void jh_job_done(void) {
    u8 *nb = jhw.buf; jhw.buf = NULL;
    jh.d = jhw;
    if (nb) {
        free(jh.tex); jh.tex = nb; jh.iw = jhw.iw; jh.ih = jhw.ih;
        GX_InitTexObj(&jh.obj, jh.tex, 256, 256, GX_TF_CMPR, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&jh.obj, GX_LINEAR, GX_LINEAR);
        GX_InvalidateTexAll();
    }
    jh.st = 2;
}
/* le plus joue (temps mesure par Pulse) : son ID, dans jeux_stats.txt (petit fichier) */
static void jh_top_load(void) {
    jhw.topid[0] = 0;
    long n = 0;
    char *d = sd_ok ? (char *)read_file(gp_path("jeux_stats.txt"), 64 * 1024, &n) : NULL;
    if (!d) return;
    u32 best = 0;
    for (long i = 0; i < n; ) {
        long s = i;
        while (i < n && d[i] != '\n') i++;
        char l[96]; long ln = i - s < 95 ? i - s : 95; memcpy(l, d + s, ln); l[ln] = 0; i++;
        char id[8]; int disc; unsigned runs, last, play = 0;
        if (sscanf(l, "%6[^|]|%d|%u|%u|%u", id, &disc, &runs, &last, &play) >= 5 && play > best) { best = play; snprintf(jhw.topid, sizeof(jhw.topid), "%s", id); }
    }
    free(d);
}
/* a l'entree dans l'onglet (gm_recent_load vient d'etre fait) : relire en fond */
static void jh_refresh(void) {
    if (jh.st == 1) return;
    snprintf(jh.path, sizeof(jh.path), "%s", gm_last_path);
    snprintf(jhw.path, sizeof(jhw.path), "%s", gm_last_path);
    jh_top_load();
    if (!jh.path[0]) { free(jh.tex); jh.tex = NULL; }
    jh.st = 0;
}
static void jh_pump(void) {
    if (jh.st == 0 && sd_ok && !sd_virtual) {
        if (gj_post_fn(jh_job, jh_job_done)) jh.st = 1;   /* (fil occupe : on reessaie a l'image suivante) */
    }
}

/* titre en deux lignes : "Serie: Titre" ou "Serie - Titre" */
static void jh_title_split(const char *t, char *a, int ac, char *b, int bc) {
    a[0] = 0;
    const char *c = strstr(t, ": ");
    if (!c) c = strstr(t, " - ");
    if (c && c > t) {
        int n = (int)(c - t); if (n > ac - 1) n = ac - 1;
        memcpy(a, t, n); a[n] = 0;
        c += c[0] == ':' ? 2 : 3;
        snprintf(b, bc, "%s", c);
    } else snprintf(b, bc, "%s", t);
}
/* nom de fichier sans "(Player's Choice)", "[...]" ; "Legend of Zelda, The - X" -> "The Legend of Zelda - X" */
static void jh_name_clean(const char *s, char *o, int oc) {
    char t[128]; int n = 0, depth = 0;
    for (const char *p = s; *p && n < (int)sizeof(t) - 1; p++) {
        if (*p == '(' || *p == '[') { depth++; continue; }
        if (*p == ')' || *p == ']') { if (depth) depth--; continue; }
        if (!depth) t[n++] = *p;
    }
    while (n && t[n - 1] == ' ') n--;
    t[n] = 0;
    char *the = strstr(t, ", The");
    if (the && (the[5] == 0 || the[5] == ' ')) {
        *the = 0;
        snprintf(o, oc, "The %s%s", t, the + 5);
    } else snprintf(o, oc, "%s", t);
}
static void jh_when(char *o, int oc) {
    o[0] = 0;
    if (!gm_last_t) return;
    time_t now = time(NULL), w = (time_t)gm_last_t;
    struct tm a, b;
    struct tm *pa = localtime(&now); if (!pa) return; a = *pa;
    struct tm *pb = localtime(&w); if (!pb) return; b = *pb;
    if (a.tm_year < 101 || b.tm_year < 101) return;        /* (horloge de la console pas reglee) */
    int dd = (a.tm_year - b.tm_year) * 366 + a.tm_yday - b.tm_yday;
    if (dd == 0) snprintf(o, oc, "Aujourd'hui, %02d:%02d", b.tm_hour, b.tm_min);
    else if (dd == 1) snprintf(o, oc, "Hier, %02d:%02d", b.tm_hour, b.tm_min);
    else snprintf(o, oc, "%02d/%02d", b.tm_mday, b.tm_mon + 1);
}
static f32 jh_chip(f32 x, f32 y, const char *t, col_t c) {   /* petite etiquette ; rend la fin */
    f32 w = (f32)text_width(F_SMALL, t, 1) + 16;
    nv_raw = 1;
    gx_rrect_fill(x, y, w, 18, 9, RGBA(255, 255, 255, 30), RGBA(255, 255, 255, 18));
    nv_raw = 0;
    gx_text1(F_SMALL, x + 8, y + 13, t, c);
    return x + w;
}
static void jh_tile_icon(int k, f32 cx, f32 cy) {
    col_t w = RGBA(225, 225, 240, 255), d = RGBA(130, 130, 155, 255), ink = RGBA(30, 20, 60, 255);
    switch (k) {
    case 1: gx_circle(cx, cy, 10, w, d); gx_circle(cx, cy, 3, ink, ink); break;   /* disque */
    case 2:                                                                          /* applis : 4 carres de couleur */
        gx_rrect_fill(cx - 10, cy - 10, 9, 9, 2, RGBA(255, 111, 168, 255), RGBA(220, 70, 130, 255));
        gx_rrect_fill(cx + 1, cy - 10, 9, 9, 2, RGBA(79, 240, 255, 255), RGBA(40, 180, 220, 255));
        gx_rrect_fill(cx - 10, cy + 1, 9, 9, 2, RGBA(255, 201, 74, 255), RGBA(220, 150, 30, 255));
        gx_rrect_fill(cx + 1, cy + 1, 9, 9, 2, RGBA(150, 120, 255, 255), RGBA(100, 70, 220, 255));
        break;
    case 3: gx_rrect_fill(cx - 7, cy - 10, 14, 19, 2, w, d); gx_rect(cx - 4, cy - 7, 8, 4, ink); break;   /* carte memoire */
    default:                                                                         /* stats : barres */
        gx_rect(cx - 10, cy + 2, 5, 8, RGBA(79, 240, 255, 255));
        gx_rect(cx - 3, cy - 5, 5, 15, RGBA(255, 111, 168, 255));
        gx_rect(cx + 4, cy - 10, 5, 20, RGBA(255, 201, 74, 255));
        break;
    }
}
static void jh_rec_text(int g, char *o, int oc) {          /* (comme l'accueil d'avant) */
    o[0] = 0;
    if (gm_record[g] <= 0) return;
    if (g == GM_DUEL) snprintf(o, oc, "%d ms", 2000 - gm_record[g]);
    else if (g == GM_RACE) { int ms = 60000 - gm_record[g]; snprintf(o, oc, "%d,%d s", ms / 1000, ms % 1000 / 100); }
    else snprintf(o, oc, "Record %d", gm_record[g]);
}

/* 1 : dessine (Aurore) ; 0 : l'accueil d'avant */
static int jh_draw(void) {
    if (ui_skin != 3) return 0;
    char buf[160], a[96], b[96];
    f32 t = gl_anim ? (f32)(now_ms() % 100000) / 1000.0f : 0.0f;
    static int init = 0;
    if (!init) { init = 1; jh.d.ngames = jh.d.napps = -1; }
    gl_job_done();                                         /* (resultat du fil de fond : sinon recupere seulement dans la bibliotheque) */
    jh_pump();
    if (jh_z >= 100 + GM_COUNT || (jh_z > 4 && jh_z < 100)) jh_z = 0;
    /* ---- le dernier jeu ---- */
    const f32 hx = 22, hy = 102, hw = 378, hh = 166;
    int on = jh_z == 0;
    gm_au_card(hx, hy, hw, hh, 16, on);
    gp_zone(hx, hy, hw, hh, &jh_z, 0, GPZ_SEL);
    const f32 cx = hx + 12, cy = hy + 10, cw = 110, ch = 146, tx = hx + 138, tw = hw - 150;
    if (jh.tex) draw_tex_fit(&jh.obj, 256, 256, jh.iw, jh.ih, cx, cy, cw, ch);
    else {                                                 /* (pas encore lue, ou pas de jaquette) : un disque */
        gx_rrect_fill(cx, cy, cw, ch, 8, RGBA(70, 140, 255, 255), RGBA(20, 40, 120, 255));
        gx_circle(cx + cw / 2, cy + ch / 2, 30, RGBA(220, 222, 240, 255), RGBA(140, 140, 165, 255));
        gx_circle(cx + cw / 2, cy + ch / 2, 8, RGBA(30, 24, 70, 255), RGBA(30, 24, 70, 255));
    }
    if (gm_last_path[0]) {
        gx_text1(F_SMALL, tx, hy + 28, TR("REPRENDRE"), RGBA(190, 172, 255, 255));
        if (jh.d.title[0] && !strcmp(jh.d.path, gm_last_path)) snprintf(buf, sizeof(buf), "%s", jh.d.title);
        else jh_name_clean(gm_last_name, buf, sizeof(buf));
        jh_title_split(buf, a, sizeof(a), b, sizeof(b));
        char f1[100], f2[100];
        if (a[0]) { fit_text(F_MED, a, (int)tw, f1, sizeof(f1)); gx_text1(F_MED, tx, hy + 54, f1, RGBA(210, 212, 255, 255)); }
        fit_text(F_LARGE, b, (int)tw, f2, sizeof(f2));
        gx_text1(F_LARGE, tx, hy + (a[0] ? 84 : 70), f2, RGBA(255, 255, 255, 255));
        f32 x = tx;
        if (gm_play_last >= 60) { gm_hms(a, sizeof(a), gm_play_last); x = jh_chip(x, hy + 98, a, RGBA(235, 240, 255, 255)) + 6; }
        jh_when(a, sizeof(a));
        if (a[0]) jh_chip(x, hy + 98, a, RGBA(235, 240, 255, 255));
        const f32 px = tx, py = hy + 124, pw = 104, ph = 30;  /* bouton Jouer */
        gx_glow(px, py, pw, ph, 15, RGBA(255, 79, 216, on ? 150 : 70), 3);
        nv_raw = 1;
        gx_rrect_fill(px, py, pw, ph, 15, RGBA(255, 86, 214, 255), RGBA(140, 92, 255, 255));
        nv_raw = 0;
        draw_mini_badge(px + 18, py + 15, "A");
        gx_text1(F_MED, px + 34, py + 21, TR("Jouer"), RGBA(255, 255, 255, 255));
    } else {
        gx_text1(F_SMALL, tx, hy + 28, TR("BIBLIOTH\xC3\x88QUE"), RGBA(190, 172, 255, 255));
        gx_text1(F_LARGE, tx, hy + 64, TR("Mes jeux"), RGBA(255, 255, 255, 255));
        fit_text(F_SMALL, TR("Aucun jeu lanc\xC3\xA9 pour l'instant : A pour choisir"), (int)tw, a, sizeof(a));
        gx_text1(F_SMALL, tx, hy + 90, a, RGBA(210, 212, 255, 255));
    }
    /* ---- les quatre tuiles ---- */
    static const char *const tn[4] = { "Jeux GameCube", "Applis", "Cartes m\xC3\xA9moire", "Mes stats" };
    const f32 sx = 408, sw = 210, sh = 37.5f;
    for (int k = 1; k <= 4; k++) {
        f32 y = hy + (k - 1) * (sh + 5.33f);
        int o = jh_z == k;
        gm_au_card(sx, y, sw, sh, 12, o);
        gp_zone(sx, y, sw, sh, &jh_z, k, GPZ_SEL);
        jh_tile_icon(k, sx + 20, y + sh / 2);
        fit_text(F_MED, TR(tn[k - 1]), (int)sw - 50, a, sizeof(a));
        gx_text1(F_MED, sx + 40, y + 17, a, RGBA(255, 255, 255, 255));
        buf[0] = 0;
        if (k == 1) {
            char tt[24]; gm_hms(tt, sizeof(tt), gm_play_total);
            if (jh.d.ngames >= 0) snprintf(buf, sizeof(buf), "%d jeu%s%s%s", jh.d.ngames, jh.d.ngames > 1 ? "x" : "", gm_play_total >= 60 ? " \xC2\xB7 " : "", gm_play_total >= 60 ? tt : "");
            else if (gm_play_total >= 60) snprintf(buf, sizeof(buf), "%s de jeu", tt);
        } else if (k == 2) {
            if (jh.d.napps > 0) snprintf(buf, sizeof(buf), "%d appli%s \xC2\xB7 %s%s%s", jh.d.napps, jh.d.napps > 1 ? "s" : "", jh.d.apn[0], jh.d.apn[1][0] ? ", " : "", jh.d.apn[1]);
            else if (jh.d.napps == 0) snprintf(buf, sizeof(buf), "%s", TR("Aucune dans SD:/apps"));
        } else if (k == 3) snprintf(buf, sizeof(buf), "%s", TR("Slots A et B, copies"));
        else if (jh.d.top[0]) snprintf(buf, sizeof(buf), "%s %s", TR("Le plus jou\xC3\xA9 :"), jh.d.top);
        else snprintf(buf, sizeof(buf), "%s", TR("Temps de jeu, succ\xC3\xA8s, records"));
        if (buf[0]) { fit_text_sel(o, F_SMALL, buf, (int)sw - 50, b, sizeof(b)); gx_text1(F_SMALL, sx + 40, y + 31, b, o ? RGBA(235, 225, 255, 255) : RGBA(175, 168, 225, 255)); }
    }
    /* ---- les mini-jeux ---- */
    int sel = jh_z >= 100 ? jh_z - 100 : gm_sel < GM_COUNT ? gm_sel : 0;
    const int vis = 8;
    f32 target = (f32)(sel - 3); if (target < 0) target = 0; if (target > GM_COUNT - vis) target = (f32)(GM_COUNT - vis);
    if (jh_off < 0.0f || !gl_anim) jh_off = target;
    else { f32 k = 1.0f - expf(-ui_dt * 12.0f); jh_off += (target - jh_off) * k; if (fabsf(target - jh_off) < 0.002f) jh_off = target; }
    gx_text1(F_SMALL, 24, 290, TR("MINI-JEUX"), RGBA(190, 172, 255, 255));
    {
        char rec[32]; jh_rec_text(sel, rec, sizeof(rec));
        snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s%s%s", gm_names[sel], gm_desc[sel], rec[0] ? " \xC2\xB7 " : "", rec);
        fit_text(F_SMALL, buf, 500, a, sizeof(a));
        gx_text1(F_SMALL, 104, 290, a, jh_z >= 100 ? RGBA(255, 255, 255, 255) : RGBA(190, 185, 230, 255));
    }
    const f32 rx = 22, step = 71, nw = 64, nh = 76, bw = 84, bh = 92, ry = 342;
    for (int g = 0; g < GM_COUNT; g++) {
        int o = jh_z == 100 + g;
        int big = g == sel && jh_z >= 100;
        f32 x = rx + (g - jh_off) * step + (g > sel && jh_z >= 100 ? bw - nw : 0);
        f32 w = big ? bw : nw, h = big ? bh : nh, y = ry - h / 2;
        if (x < 14 || x + w > 626) continue;
        gm_au_card(x, y, w, h, 14, o);
        gp_zone(x, y, w, h, &jh_z, 100 + g, GPZ_SEL);
        gm_icon(g, x + w / 2, y + h * 0.40f, t, w / 2 - 8, h * 0.22f);
        fit_text_sel(o, F_SMALL, gm_names[g], (int)w - 8, a, sizeof(a));
        gx_text_c(F_SMALL, x + w / 2, y + h - 11, a, RGBA(255, 255, 255, 255));
    }
    if (jh_z >= 100) gm_sel = sel;
    /* ---- les touches ---- */
    gx_text_c(F_SMALL, 320, 406, gm_last_path[0] ? "A : choisir     X : relancer le dernier     Y : pause de la musique     B : arr\xC3\xAAter la musique"
                                                  : "A : choisir     Y : pause de la musique     B : arr\xC3\xAAter la musique", RGBA(200, 190, 245, 230));
    return 1;
}
/* 1 : la touche est prise (Aurore) */
static int jh_input(u32 down, u32 nav, int fire) {
    if (ui_skin != 3) return 0;
    int z = jh_z;
    if (fire && (nav & PAD_BUTTON_RIGHT)) { if (z == 0) z = 1; else if (z >= 100 && z < 100 + GM_COUNT - 1) z++; }
    if (fire && (nav & PAD_BUTTON_LEFT)) { if (z >= 1 && z <= 4) z = 0; else if (z > 100) z--; }
    if (fire && (nav & PAD_BUTTON_DOWN)) { if (z == 0 || z == 4) z = 100 + (gm_sel < GM_COUNT ? gm_sel : 0); else if (z >= 1 && z <= 3) z++; }
    if (fire && (nav & PAD_BUTTON_UP)) {
        if (z >= 100) { f32 x = 22 + (z - 100 - jh_off) * 71; z = x < 380 ? 0 : 4; }
        else if (z >= 2 && z <= 4) z--;
    }
    jh_z = z;
    if (z >= 100) gm_sel = z - 100;
    if (down & PAD_BUTTON_A) {
        if (z == 0) { if (gm_last_path[0]) gclib_open_recent(gm_last_path); else gclib_open(); }
        else if (z == 1) gclib_open();
        else if (z == 2) ap_open();
        else if (z == 3) mc_open();
        else if (z == 4) rcp_open();
        else games_tab_launch(z - 100);
    }
    return 1;
}

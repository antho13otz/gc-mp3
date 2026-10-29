// GC Station v207 : le compagnon (inclus dans main.c, apres tools.c).
// Un petit etre qui vit dans GC Station et grandit avec ce que tu ecoutes :
//   - il eclot puis grandit avec le temps d'ecoute (oeuf, bebe, petit, grand, legende) ;
//   - son style (couleur, accessoire) suit ce que tu ecoutes le plus : musique (casque), radio (antenne),
//     retro (lunettes pixel), musiques de jeux GameCube (manette), podcasts (noeud papillon) ;
//   - il danse sur la musique en cours, dort la nuit, s'ennuie si rien ne joue pendant des jours.
// START > Outils > Mon compagnon. A : caresser ; X : le renommer ; B : fermer.
// Garde dans SD:/gc-mp3/reglages/compagnon.txt.

#define PET_FILE GCD_REG "/compagnon.txt"
enum { PT_MUSIC, PT_RADIO, PT_RETRO, PT_GAMES, PT_PODCAST, PT_N };
static const char *const pet_ckey[PT_N] = { "musique", "radio", "retro", "jeux", "podcasts" };
static const char *const pet_cname[PT_N] = { "la musique", "la radio", "la musique r\xC3\xA9tro", "les musiques de jeux GameCube", "les podcasts" };
static struct { char name[24]; u32 born, seen; f32 total, cat[PT_N]; u32 pets; } pet;
static int pet_loaded = 0, pet_dirty = 0, pet_stage_last = -1;
static f32 pet_unsaved = 0;
static u64 pet_heart_t = 0;
static f32 pet_bounce = 0;
static const u32 pet_stage_s[] = { 0, 10 * 60, 2 * 3600, 10 * 3600, 50 * 3600 };
static const char *const pet_stage_name[] = { "\xC5\x92uf", "B\xC3\xA9" "b\xC3\xA9", "Petit", "Grand", "L\xC3\xA9gende" };

static u32 pet_now(void) { time_t t = time(NULL); return t > 0 ? (u32)t : 0; }
static int pet_stage(void) { int s = 0; for (int i = 0; i < 5; i++) if (pet.total >= pet_stage_s[i]) s = i; return s; }
static int pet_fav(void) { int b = 0; for (int i = 1; i < PT_N; i++) if (pet.cat[i] > pet.cat[b]) b = i; return pet.cat[b] > 60 ? b : -1; }

static void pet_load(void) {
    if (pet_loaded) return;
    pet_loaded = 1;
    memset(&pet, 0, sizeof(pet));
    snprintf(pet.name, sizeof(pet.name), "Cubi");
    long len = 0;
    char *b = sd_ok ? (char *)read_file(PET_FILE, 8192, &len) : NULL;
    if (b) {
        char *save = NULL;
        for (char *l = strtok_r(b, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            char *eq = strchr(l, '=');
            if (!eq || l[0] == '#') continue;
            *eq = 0;
            char *k = trim(l), *v = trim(eq + 1);
            if (!strcmp(k, "nom") && v[0]) snprintf(pet.name, sizeof(pet.name), "%s", v);
            else if (!strcmp(k, "ne")) pet.born = (u32)strtoul(v, NULL, 10);
            else if (!strcmp(k, "vu")) pet.seen = (u32)strtoul(v, NULL, 10);
            else if (!strcmp(k, "ecoute")) pet.total = (f32)atof(v);
            else if (!strcmp(k, "caresses")) pet.pets = (u32)strtoul(v, NULL, 10);
            else for (int i = 0; i < PT_N; i++) if (!strcmp(k, pet_ckey[i])) pet.cat[i] = (f32)atof(v);
        }
        free(b);
    }
    if (!pet.born) { pet.born = pet_now(); pet_dirty = 1; }
    pet_stage_last = pet_stage();
}
static void pet_save(void) {
    if (!sd_ok || !pet_loaded) return;
    char t[600];
    int n = snprintf(t, sizeof(t), "# GC Station : ton compagnon (START > Outils > Mon compagnon)\nnom = %s\nne = %u\nvu = %u\necoute = %.0f\ncaresses = %u\n",
                     pet.name, (unsigned)pet.born, (unsigned)pet.seen, pet.total, (unsigned)pet.pets);
    for (int i = 0; i < PT_N && n < (int)sizeof(t); i++) n += snprintf(t + n, sizeof(t) - n, "%s = %.0f\n", pet_ckey[i], pet.cat[i]);
    if (n >= (int)sizeof(t)) n = (int)sizeof(t) - 1;
    if (bgf_put_mode(BGF_REPL, 1, PET_FILE, t, n)) { pet_dirty = 0; pet_unsaved = 0; }
}

/* (chaque image) : ce qui joue nourrit le compagnon */
static void pet_tick(void) {
    if (sx_quiet) return;                                  /* v261 : (le test de la console ne nourrit pas le compagnon) */
    pet_load();
    int cat = -1;
    if (src == SRC_FILE && playing && !paused && cur_track >= 0 && cur_track < nfiles) {
        const char *p = names[cur_track];
        if (is_retro[cur_track]) cat = jb_iso_path(p) ? PT_GAMES : PT_RETRO;
        else if (strstr(p, "/podcasts/")) cat = PT_PODCAST;
        else cat = PT_MUSIC;
    } else if (src == SRC_RADIO && state == ST_PLAY && !muted) cat = PT_RADIO;
    if (cat >= 0) {
        f32 dt = ui_dt > 0.2f ? 0.2f : ui_dt;
        pet.total += dt; pet.cat[cat] += dt; pet_unsaved += dt;
        pet.seen = pet_now();
        int st = pet_stage();
        if (st > pet_stage_last) {
            char m[120];
            if (pet_stage_last == 0) snprintf(m, sizeof(m), "%s vient d'\xC3\xA9" "clore ! (START > Outils > Mon compagnon)", pet.name);
            else snprintf(m, sizeof(m), "%s a grandi : %s !", pet.name, pet_stage_name[st]);
            lib_note(m);
            pet_dirty = 1;
        }
        pet_stage_last = st;
    }
    if ((pet_unsaved > 60 || (pet_dirty && cat < 0)) && lsc_ready()) pet_save();
}

static void pet_open(void) { pet_load(); tl_screen = TL_PET; }
static void pet_input(u32 down, u32 nav, int fire) {
    (void)nav; (void)fire;
    if (down & PAD_BUTTON_B) { tl_screen = TL_NONE; if (pet_dirty) pet_save(); return; }
    if ((down & PAD_BUTTON_A) && pet_stage() > 0) { pet.pets++; pet_heart_t = now_ms(); pet_dirty = 1; }
    if (down & PAD_BUTTON_X) kb_start(KB_PETNAME, pet.name, -1, -1);
}
static void pet_rename(const char *t) {
    if (!*t) return;
    snprintf(pet.name, sizeof(pet.name), "%s", t);
    pet_dirty = 1;
    pet_save();
}

/* --- dessin --- */
static col_t pet_col(int fav, int k) {         /* k : 0 clair, 1 fonce */
    static const u8 c[PT_N + 1][2][3] = {
        { { 196, 150, 255 }, { 120, 70, 220 } },  /* musique : violet */
        { { 130, 200, 255 }, { 40, 110, 220 } },  /* radio : bleu */
        { { 140, 240, 150 }, { 40, 160, 80 } },   /* retro : vert */
        { { 255, 190, 110 }, { 225, 110, 30 } },  /* jeux : orange */
        { { 255, 160, 200 }, { 210, 70, 130 } },  /* podcasts : rose */
        { { 235, 230, 245 }, { 170, 160, 200 } }, /* (pas encore de style) */
    };
    int i = fav < 0 ? PT_N : fav;
    return RGBA(c[i][k][0], c[i][k][1], c[i][k][2], 255);
}
static void pet_draw_body(f32 cx, f32 fy, f32 sz, int stage, int fav, int sleeping, int sad, f32 t) {
    /* ombre */
    gx_ellipse(cx, fy + 2, sz * 0.55f, sz * 0.12f, RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 0));
    if (stage == 0) {                                           /* oeuf : il bouge un peu, et se fend avant d'eclore */
        f32 w = sinf(t * 2.5f) * (pet.total > 300 ? 3.0f : 1.0f);
        f32 ex = cx + w, ey = fy - sz * 0.62f;
        gx_ellipse(ex, ey, sz * 0.42f, sz * 0.6f, RGBA(255, 250, 235, 255), RGBA(225, 215, 190, 255));
        for (int i = 0; i < 5; i++) gx_circle(ex - sz * 0.2f + i * sz * 0.1f, ey - sz * 0.1f + (i & 1) * sz * 0.14f, sz * 0.045f, RGBA(190, 160, 255, 255), RGBA(150, 110, 240, 255));
        if (pet.total > 300) { gx_poly4(ex - 8, ey - 20, ex - 2, ey - 12, ex - 6, ey - 4, ex - 9, ey - 12, RGBA(150, 130, 110, 255), RGBA(150, 130, 110, 255), RGBA(150, 130, 110, 255), RGBA(150, 130, 110, 255)); }
        return;
    }
    f32 bass = 0;
    for (int i = 0; i < 4; i++) bass += bar_val[i];
    bass = bass / 4;
    int music = (src == SRC_FILE && playing && !paused) || (src == SRC_RADIO && state == ST_PLAY);
    f32 target = music && !sleeping ? bass : 0;
    pet_bounce += (target - pet_bounce) * (1.0f - expf(-ui_dt * 14.0f));
    f32 squash = 1.0f + (music ? pet_bounce * 0.18f : 0.03f * sinf(t * 2.0f));
    f32 w = sz * squash, h = sz / squash;
    f32 sway = music && !sleeping ? sinf(t * 4.0f) * sz * 0.06f : 0;
    f32 x0 = cx - w / 2 + sway, y0 = fy - h - (music ? pet_bounce * sz * 0.25f : 0);
    col_t c1 = pet_col(fav, 0), c2 = pet_col(fav, 1);
    if (sad) { c1 = lerp_c(c1, RGBA(150, 150, 170, 255), 0.5f); c2 = lerp_c(c2, RGBA(90, 90, 110, 255), 0.5f); }
    /* pieds */
    gx_ellipse(x0 + w * 0.28f, fy - 2, w * 0.13f, sz * 0.07f, c2, c2);
    gx_ellipse(x0 + w * 0.72f, fy - 2, w * 0.13f, sz * 0.07f, c2, c2);
    /* corps (un cube tout rond) */
    gx_rrect_fill(x0, y0, w, h, sz * 0.26f, c1, c2);
    gx_rrect_fill(x0 + w * 0.12f, y0 + h * 0.08f, w * 0.34f, h * 0.16f, sz * 0.08f, RGBA(255, 255, 255, 70), RGBA(255, 255, 255, 10));
    if (stage >= 4) gx_rrect_stroke(x0 - 3, y0 - 3, w + 6, h + 6, sz * 0.28f, 2.0f, RGBA(255, 220, 120, (u8)(150 + 100 * sinf(t * 3))));   /* legende : aura doree */
    /* yeux */
    f32 ey = y0 + h * 0.42f, ex1 = x0 + w * 0.33f, ex2 = x0 + w * 0.67f, er = sz * 0.085f;
    int blink = fmodf(t, 4.2f) < 0.12f;
    if (sleeping || blink) {
        gx_rect(ex1 - er, ey, er * 2, 2.5f, RGBA(40, 30, 70, 255));
        gx_rect(ex2 - er, ey, er * 2, 2.5f, RGBA(40, 30, 70, 255));
    } else {
        gx_circle(ex1, ey, er, RGBA(40, 30, 70, 255), RGBA(40, 30, 70, 255));
        gx_circle(ex2, ey, er, RGBA(40, 30, 70, 255), RGBA(40, 30, 70, 255));
        gx_circle(ex1 + er * 0.35f, ey - er * 0.35f, er * 0.32f, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
        gx_circle(ex2 + er * 0.35f, ey - er * 0.35f, er * 0.32f, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
    }
    /* joues et bouche */
    gx_circle(x0 + w * 0.2f, ey + sz * 0.13f, sz * 0.06f, RGBA(255, 120, 150, 90), RGBA(255, 120, 150, 0));
    gx_circle(x0 + w * 0.8f, ey + sz * 0.13f, sz * 0.06f, RGBA(255, 120, 150, 90), RGBA(255, 120, 150, 0));
    f32 my = ey + sz * 0.17f, mx = x0 + w / 2;
    if (sleeping) gx_circle(mx, my, sz * 0.03f, RGBA(40, 30, 70, 255), RGBA(40, 30, 70, 255));
    else if (sad) gx_poly4(mx - sz * 0.08f, my + 3, mx, my - 1, mx + sz * 0.08f, my + 3, mx, my + 1, RGBA(40, 30, 70, 255), RGBA(40, 30, 70, 255), RGBA(40, 30, 70, 255), RGBA(40, 30, 70, 255));
    else if (music) gx_ellipse(mx, my + 2, sz * 0.06f, sz * (0.04f + pet_bounce * 0.05f), RGBA(120, 30, 60, 255), RGBA(80, 20, 40, 255));
    else gx_poly4(mx - sz * 0.08f, my - 1, mx, my + 4, mx + sz * 0.08f, my - 1, mx, my + 1.5f, RGBA(40, 30, 70, 255), RGBA(40, 30, 70, 255), RGBA(40, 30, 70, 255), RGBA(40, 30, 70, 255));
    /* accessoire (ce que tu ecoutes le plus) ; a partir de "Petit" */
    if (stage < 2 || fav < 0) return;
    col_t k = RGBA(40, 34, 60, 255);
    switch (fav) {
    case PT_MUSIC:                                             /* casque */
        for (int i = 0; i <= 16; i++) {
            f32 a = (f32)M_PI * (1.0f + i / 16.0f);
            gx_circle(x0 + w / 2 + cosf(a) * w * 0.5f, y0 + h * 0.4f + sinf(a) * h * 0.5f, 2.6f, k, k);
        }
        gx_rrect_fill(x0 - w * 0.08f, y0 + h * 0.3f, w * 0.16f, h * 0.26f, 5, RGBA(255, 90, 140, 255), RGBA(200, 40, 100, 255));
        gx_rrect_fill(x0 + w * 0.92f, y0 + h * 0.3f, w * 0.16f, h * 0.26f, 5, RGBA(255, 90, 140, 255), RGBA(200, 40, 100, 255));
        break;
    case PT_RADIO: {                                           /* antenne qui capte */
        f32 ax = x0 + w * 0.62f;
        gx_rect(ax - 1.2f, y0 - sz * 0.32f, 2.4f, sz * 0.32f, k);
        gx_circle(ax, y0 - sz * 0.34f, sz * 0.06f, RGBA(255, 90, 90, 255), RGBA(220, 40, 40, 255));
        f32 ph = fmodf(t, 1.2f) / 1.2f;
        gx_ring(ax, y0 - sz * 0.34f, sz * (0.08f + ph * 0.2f), sz * (0.08f + ph * 0.2f) + 1.5f, RGBA(255, 255, 255, (u8)(200 * (1 - ph))));
        break;
    }
    case PT_RETRO:                                             /* lunettes pixel */
        for (int i = 0; i < 2; i++) {
            f32 gx0 = (i ? ex2 : ex1) - er * 1.6f;
            gx_rect(gx0, ey - er * 1.1f, er * 3.2f, er * 2.0f, RGBA(20, 20, 30, 255));
            gx_rect(gx0 + er * 0.4f, ey - er * 0.8f, er * 0.8f, er * 0.6f, RGBA(255, 255, 255, 200));
        }
        gx_rect(ex1 + er * 1.6f, ey - er * 0.6f, ex2 - ex1 - er * 3.2f, 3, RGBA(20, 20, 30, 255));
        break;
    case PT_GAMES: {                                           /* manette GameCube dans la main */
        f32 px = x0 + w * 0.95f, py = y0 + h * 0.7f;
        gx_rrect_fill(px - 4, py - 8, 30, 18, 8, RGBA(110, 90, 200, 255), RGBA(80, 60, 160, 255));
        gx_circle(px + 17, py, 4, RGBA(80, 220, 120, 255), RGBA(50, 180, 90, 255));
        gx_circle(px + 5, py - 1, 3, RGBA(230, 230, 240, 255), RGBA(180, 180, 200, 255));
        break;
    }
    case PT_PODCAST:                                           /* noeud papillon */
        gx_tri(mx, y0 + h * 0.9f, mx - sz * 0.16f, y0 + h * 0.8f, mx - sz * 0.16f, y0 + h * 1.0f, RGBA(220, 50, 90, 255));
        gx_tri(mx, y0 + h * 0.9f, mx + sz * 0.16f, y0 + h * 0.8f, mx + sz * 0.16f, y0 + h * 1.0f, RGBA(220, 50, 90, 255));
        gx_circle(mx, y0 + h * 0.9f, sz * 0.035f, RGBA(255, 120, 150, 255), RGBA(200, 40, 80, 255));
        break;
    }
}
static void draw_pet(void) {
    pet_load();
    f32 t = now_ms() / 1000.0f;
    gx_quad4(0, 0, 640, 480, RGBA(28, 18, 60, 255), RGBA(28, 18, 60, 255), RGBA(70, 40, 110, 255), RGBA(70, 40, 110, 255));
    gx_quad4(0, 330, 640, 480, RGBA(60, 36, 100, 255), RGBA(60, 36, 100, 255), RGBA(40, 24, 70, 255), RGBA(40, 24, 70, 255));
    gx_rect(0, 330, 640, 2, RGBA(255, 255, 255, 30));
    u32 now = pet_now();
    time_t tt = (time_t)now;
    struct tm *tm = localtime(&tt);
    int hour = tm ? tm->tm_hour : 12;
    int music = (src == SRC_FILE && playing && !paused) || (src == SRC_RADIO && state == ST_PLAY);
    int sleeping = !music && (hour >= 23 || hour < 7);
    u32 idle = pet.seen && now > pet.seen ? now - pet.seen : 0;
    int sad = !music && pet.seen && idle > 3 * 86400;
    int stage = pet_stage(), fav = pet_fav();
    /* fenetre : jour / nuit */
    int night = hour >= 20 || hour < 7;
    gx_rrect_fill(470, 70, 120, 110, 8, night ? RGBA(10, 14, 50, 255) : RGBA(120, 190, 255, 255), night ? RGBA(30, 30, 80, 255) : RGBA(190, 225, 255, 255));
    gx_rrect_stroke(470, 70, 120, 110, 8, 3, RGBA(140, 110, 200, 255));
    gx_rect(529, 70, 3, 110, RGBA(140, 110, 200, 255)); gx_rect(470, 124, 120, 3, RGBA(140, 110, 200, 255));
    if (night) gx_circle(560, 98, 12, RGBA(255, 250, 220, 255), RGBA(240, 230, 180, 255));
    else gx_circle(500, 98, 13, RGBA(255, 240, 150, 255), RGBA(255, 210, 90, 255));
    /* le compagnon */
    static const f32 szs[] = { 120, 70, 95, 120, 140 };
    f32 sz = szs[stage];
    pet_draw_body(250, 345, sz, stage, fav, sleeping, sad, t);
    if (sleeping && stage > 0) for (int i = 0; i < 3; i++) {
        f32 p = fmodf(t * 0.5f + i / 3.0f, 1.0f);
        gx_text1(i == 2 ? F_LARGE : F_MED, 250 + sz * 0.4f + p * 40, 345 - sz - p * 60, "z", RGBA(220, 220, 255, (u8)(255 * (1 - p))));
    }
    if (music && !sleeping && stage > 0) for (int i = 0; i < 2; i++) {     /* notes de musique */
        f32 p = fmodf(t * 0.6f + i * 0.5f, 1.0f);
        f32 nx = 250 - sz * 0.7f - p * 30 + i * (sz * 1.4f + 60 * p), ny = 330 - sz - p * 70;
        gx_circle(nx, ny, 4, RGBA(255, 255, 255, (u8)(230 * (1 - p))), RGBA(255, 255, 255, (u8)(230 * (1 - p))));
        gx_rect(nx + 3, ny - 14, 1.6f, 14, RGBA(255, 255, 255, (u8)(230 * (1 - p))));
    }
    u64 hd = now_ms() - pet_heart_t;
    if (hd < 1600) for (int i = 0; i < 4; i++) {                  /* caresse : des coeurs */
        f32 p = hd / 1600.0f;
        f32 hx = 250 + (i - 1.5f) * 26 + sinf(p * 6 + i) * 6, hy = 345 - sz - 10 - p * 80 - i * 6;
        col_t c = RGBA(255, 90, 130, (u8)(255 * (1 - p)));
        gx_circle(hx - 4, hy, 5, c, c); gx_circle(hx + 4, hy, 5, c, c); gx_tri(hx - 9, hy + 1, hx + 9, hy + 1, hx, hy + 12, c);
    }
    /* fiche */
    char b[160], d[160];
    text_display(pet.name, d, sizeof(d));
    gx_text1(F_LARGE, 40, 60, d, RGBA(255, 255, 255, 255));
    snprintf(b, sizeof(b), "%s", pet_stage_name[stage]);
    gx_text1(F_MED, 40, 88, b, RGBA(255, 210, 120, 255));
    f32 y = 120;
    u32 days = now > pet.born ? (now - pet.born) / 86400 : 0;
    snprintf(b, sizeof(b), days == 0 ? "N\xC3\xA9 aujourd'hui" : days == 1 ? "\xC3\x82g\xC3\xA9 d'un jour" : "\xC3\x82g\xC3\xA9 de %u jours", (unsigned)days);
    gx_text1(F_SMALL, 40, y, b, RGBA(220, 210, 255, 255)); y += 20;
    u32 tot = (u32)pet.total;
    if (tot >= 3600) snprintf(b, sizeof(b), "Nourri de %u h %02u de son", (unsigned)(tot / 3600), (unsigned)(tot / 60 % 60));
    else snprintf(b, sizeof(b), "Nourri de %u min de son", (unsigned)(tot / 60));
    gx_text1(F_SMALL, 40, y, b, RGBA(220, 210, 255, 255)); y += 20;
    if (fav >= 0) { snprintf(b, sizeof(b), "Adore %s", pet_cname[fav]); gx_text1(F_SMALL, 40, y, b, pet_col(fav, 0)); y += 20; }
    if (pet.pets) { snprintf(b, sizeof(b), "%u caresse%s", (unsigned)pet.pets, pet.pets > 1 ? "s" : ""); gx_text1(F_SMALL, 40, y, b, RGBA(255, 150, 180, 255)); y += 20; }
    const char *mood = stage == 0 ? (pet.total > 300 ? "L'\xC5\x93uf bouge... il va bient\xC3\xB4t \xC3\xA9" "clore" : "Un \xC5\x93uf. Mets de la musique pour le faire \xC3\xA9" "clore")
                     : sleeping ? "Il dort (mets de la musique pour le r\xC3\xA9veiller)"
                     : music ? "Il danse !"
                     : sad ? "Il s'ennuie beaucoup : \xC3\xA7" "a fait des jours sans musique"
                     : idle > 86400 ? "Il s'ennuie un peu" : "Content";
    fit_text(F_SMALL, mood, 380, d, sizeof(d));
    gx_text1(F_SMALL, 40, y + 6, d, RGBA(255, 255, 255, 255));
    /* prochaine etape */
    if (stage < 4) {
        f32 a = pet_stage_s[stage], z = pet_stage_s[stage + 1];
        f32 pr = (pet.total - a) / (z - a);
        if (pr < 0) pr = 0;
        if (pr > 1) pr = 1;
        gx_rrect_fill(400, 300, 200, 10, 5, RGBA(255, 255, 255, 40), RGBA(255, 255, 255, 30));
        if (pr > 0.02f) gx_rrect_fill(400, 300, 200 * pr, 10, 5, RGBA(255, 210, 120, 255), RGBA(255, 160, 80, 255));
        u32 left = (u32)(z - pet.total);
        if (left >= 3600) snprintf(b, sizeof(b), "%s dans %u h %02u d'\xC3\xA9" "coute", pet_stage_name[stage + 1], (unsigned)(left / 3600), (unsigned)(left / 60 % 60));
        else snprintf(b, sizeof(b), "%s dans %u min d'\xC3\xA9" "coute", pet_stage_name[stage + 1], (unsigned)(left / 60 + 1));
        gx_text_r(F_SMALL, 600, 292, b, RGBA(220, 210, 255, 255));
    } else gx_text_r(F_SMALL, 600, 300, "Au sommet de sa forme", RGBA(255, 220, 120, 255));
    static const char *const k[] = { "A", "X", "B" };
    static const char *const l[] = { "CARESSER", "RENOMMER", "FERMER" };
    tl_legend(k, l, 3);
}

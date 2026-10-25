// GC Station v132 : vignettes des pochettes (64x64) pour la rangee de pochettes (style Vitrine) et les listes.
// Inclus dans main.c juste avant le fil de decodage. Une pochette par album (etiquettes : album + artiste),
// sinon par morceau. Le travail lourd (lire l'image, la decoder, la reduire) est fait par le fil de decodage,
// une vignette a la fois, seulement quand le son a de l'avance : l'interface ne s'arrete jamais.
// Le resultat 128x128 est aussi range dans le cache des pochettes (sd:/gc-mp3/cache/pochettes) : le meme
// que celui du lecteur, donc la pochette du morceau s'affiche aussitot quand on le lance.
#define TH_SZ 64
#define TH_N 40
typedef struct { u32 key; int state; u32 used; GXTexObj obj; } th_slot_t;   /* state : 0 attendue, 1 prete, 2 pas d'image */
static th_slot_t th_s[TH_N];
static u16 th_tex[TH_N][TH_SZ * TH_SZ] ATTRIBUTE_ALIGN(32);
static u32 vg_clock = 1;
static volatile int tj_state = 0;                          /* 0 libre, 1 a faire (fil), 2 fini */
static char tj_path[NAME_LEN];
static int tj_slot = -1, tj_ok = 0;
static u32 tj_key = 0;
static u16 vg_out[TH_SZ * TH_SZ] ATTRIBUTE_ALIGN(32);
static u32 th_req_frame = 0;

static u32 th_hash(const char *a, const char *b) {
    u32 h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)a; *p; p++) h = (h ^ *p) * 16777619u;
    h = (h ^ '|') * 16777619u;
    for (const unsigned char *p = (const unsigned char *)b; *p; p++) h = (h ^ *p) * 16777619u;
    return h ? h : 1;
}
static u32 th_key_of(int k) {
    if (tg_done[k] && tg_album[k][0]) return th_hash(tg_album[k], tg_artist[k]);
    return th_hash(names[k], "");
}

/* ---- fil de decodage ---- */
static int th_decode(const unsigned char *data, long len, u16 *big) {
    int w, h, n;
    if (len <= 0 || !stbi_info_from_memory(data, (int)len, &w, &h, &n) || w <= 0 || h <= 0 || (long)w * (long)h > COVER_MAX_PX) return 0;
    unsigned char *px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 3);
    if (!px) return 0;
    cover_scale(px, w, h, big, 0);
    stbi_image_free(px);
    return 1;
}
static int th_from_file(const char *path, u16 *big) {
    long len = 0;
    unsigned char *b = read_file(path, COVER_MAX_FILE, &len);
    if (!b) return 0;
    int ok = th_decode(b, len, big);
    free(b);
    return ok;
}
static void tj_run(void) {
    tj_ok = 0;
    u16 *big = (u16 *)memalign(32, COVER_SZ * COVER_SZ * 2);
    if (!big) return;
    struct stat st;
    long sz = stat(tj_path, &st) == 0 ? (long)st.st_size : 0;
    char key[NAME_LEN + 32], cpath[NAME_LEN];
    snprintf(key, sizeof(key), "%s|%ld", tj_path, sz);
    cv_cache_name(key, cpath, sizeof(cpath));
    int have = 0;
    FILE *f = fopen(cpath, "rb");
    if (f) {                                                /* deja dans le cache des pochettes */
        char hd[4];
        have = fread(hd, 1, 4, f) == 4 && !memcmp(hd, "GCV1", 4) && fread(big, 1, COVER_SZ * COVER_SZ * 2, f) == COVER_SZ * COVER_SZ * 2;
        fclose(f);
    }
    if (!have) {                                            /* comme load_cover : image a cote, dans le fichier, du dossier */
        static const char *exts[] = { ".jpg", ".jpeg", ".png" };
        static const char *common[] = { "cover.jpg", "folder.jpg", "cover.png", "folder.png" };
        char noext[NAME_LEN], dir[NAME_LEN], p[NAME_LEN + 16];
        snprintf(noext, sizeof(noext), "%s", tj_path);
        strip_audio_ext(noext);
        snprintf(dir, sizeof(dir), "%s", tj_path);
        char *sl = strrchr(dir, '/');
        if (sl) *sl = 0;
        for (int i = 0; i < 3 && !have; i++) { snprintf(p, sizeof(p), "%s%s", noext, exts[i]); have = th_from_file(p, big); }
        if (!have) {
            int fm = audio_fmt(tj_path);
            if (fm == FMT_MP3) {
                unsigned char *tag; const unsigned char *im; long il;
                if (id3_pic(tj_path, &tag, &im, &il)) { have = th_decode(im, il, big); free(tag); }
            } else if (fm == FMT_FLAC) {
                FILE *fp = fopen(tj_path, "rb");
                unsigned char *im = NULL; long il = 0;
                if (fp) { flac_meta_pic(fp, NULL, 1, &im, &il); fclose(fp); }
                if (im) { have = th_decode(im, il, big); free(im); }
            }
        }
        for (int i = 0; i < 4 && !have; i++) { snprintf(p, sizeof(p), "%s/%s", dir, common[i]); have = th_from_file(p, big); }
        if (have) cv_cache_write(key, big);
    }
    if (have) {                                             /* 128 -> 64 (moyenne de 2x2), en tuiles 4x4 */
        for (int y = 0; y < TH_SZ; y++)
            for (int x = 0; x < TH_SZ; x++) {
                u32 r = 0, g = 0, b = 0;
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++) {
                        int X = x * 2 + dx, Y = y * 2 + dy;
                        u16 v = big[((Y >> 2) * (COVER_SZ / 4) + (X >> 2)) * 16 + (Y & 3) * 4 + (X & 3)];
                        r += v >> 11; g += (v >> 5) & 63; b += v & 31;
                    }
                vg_out[((y >> 2) * (TH_SZ / 4) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)] = (u16)(((r / 4) << 11) | ((g / 4) << 5) | (b / 4));
            }
        tj_ok = 1;
    }
    free(big);
}

/* ---- fil principal ---- */
static void th_pump(void) {
    if (tj_state != 2) return;
    if (tj_slot >= 0 && th_s[tj_slot].key == tj_key) {
        th_slot_t *s = &th_s[tj_slot];
        if (tj_ok) {
            memcpy(th_tex[tj_slot], vg_out, sizeof(vg_out));
            DCFlushRange(th_tex[tj_slot], sizeof(th_tex[0]));
            GX_InitTexObj(&s->obj, th_tex[tj_slot], TH_SZ, TH_SZ, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
            GX_InvalidateTexAll();
            s->state = 1;
        } else s->state = 2;
    }
    tj_slot = -1;
    tj_state = 0;
}
/* vignette du morceau k : numero de la case si elle est prete, sinon -1 (et, si want, elle est demandee) */
static int th_get(int k, int want) {
    if (k < 0 || k >= nfiles || sd_virtual) return -1;
    th_pump();
    u32 key = th_key_of(k);
    int lru = -1;
    for (int i = 0; i < TH_N; i++) {
        if (th_s[i].key == key) { th_s[i].used = vg_clock; return th_s[i].state == 1 ? i : -1; }
        if (i != tj_slot && (lru < 0 || th_s[i].used < th_s[lru].used)) lru = i;
    }
    if (!want || tj_state != 0 || gj_hold || lru < 0) return -1;
    if (th_req_frame == retrace_count) return -1;           /* (une demande par image) */
    if (!io_safe() || !dec_ensure()) return -1;
    th_req_frame = retrace_count;
    th_slot_t *s = &th_s[lru];
    s->key = key; s->state = 0; s->used = vg_clock;
    snprintf(tj_path, sizeof(tj_path), "%s", names[k]);
    tj_key = key; tj_slot = lru;
    tj_state = 1;
    return -1;
}
static void th_frame(void) { vg_clock++; }

// GC-MP3 : onglet PHOTOS & VIDEO (inclus dans main.c)
// Photos (.jpg, .png) de sd:/photos, sd:/ecran et sd:/downloads, en vignettes
// puis en plein ecran (diaporama avec la musique). Videos .avi (MJPEG + son PCM)
// de sd:/videos, par exemple converties avec :
//   ffmpeg -i film.mp4 -vf scale=320:-2 -r 20 -c:v mjpeg -q:v 5 -c:a pcm_s16le -ar 32000 film.avi
#define PH_MAXF     600
#define TH_W        128
#define TH_H        96
#define TH_CACHE    40
#define GRID_COLS   4
#define GRID_ROWS   3
#define SLIDE_MS    6000

typedef struct { char path[NAME_LEN]; u8 video; u8 bad; u32 date; long size; } pfile_t;   /* (v165 : size -1 = pas encore lue) */
static pfile_t *pf = NULL;
static int npf = -1, pf_sel = 0, pf_top = 0;
static int pf_sort = 0;                     /* v76 : 0 = par nom, 1 = par date (recentes d'abord) */
static int pv_mode = 0;                     /* 0 = vignettes, 1 = plein ecran, 2 = video */
static int pv_slide = 0;
static u64 pv_slide_t = 0;

/* vignettes en memoire */
typedef struct { int file; u16 *tex; GXTexObj obj; int tw, th, iw, ih; u32 used; } thumb_t;
static thumb_t thumbs[TH_CACHE];
static u32 th_clock = 0;
/* image plein ecran */
static u16 *big_tex = NULL;
static GXTexObj big_obj;
static int big_tw, big_th, big_iw, big_ih, big_file = -1;

/* chargement asynchrone : lecture par morceaux, puis decodage par le thread des images */
enum { PJ_IDLE, PJ_READ, PJ_DEC, PJ_CREAD, PJ_CWRITE };   /* (v120 : vignettes en cache lues / ecrites par morceaux) */
static int pj_state = PJ_IDLE, pj_file = -1, pj_big = 0;
static FILE *pj_fp = NULL;
static char pj_cpath[NAME_LEN];                         /* v120 : fichier de vignette en cours */
static int pj_ctw, pj_cth, pj_ciw, pj_cih;
static thumb_t *pj_ct;
static u16 *pj_ctex;
static unsigned char *pj_buf = NULL;
static long pj_len = 0, pj_size = 0;
#define DJ_OWNER_PHOTO 3
#define DJ_OWNER_VIDEO 4

static int img_ext(const char *n) {
    const char *d = strrchr(n, '.');
    return d && (!strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg") || !strcasecmp(d, ".png") || !strcasecmp(d, ".webp"));
}

static void pf_scan_dir(const char *dir, int depth, int video) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char path[NAME_LEN];
    while ((e = readdir(d)) != NULL && npf < PH_MAXF) {
        if (e->d_name[0] == '.') continue;
        int n = snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (n <= 0 || n >= (int)sizeof(path)) continue;
        const char *dot = strrchr(e->d_name, '.');
        int add = 0;
        if (!video && img_ext(e->d_name)) { pf[npf].video = 0; add = 1; }
        else if (video && dot && !strcasecmp(dot, ".avi")) { pf[npf].video = 1; add = 1; }
        if (add) {
            snprintf(pf[npf].path, NAME_LEN, "%s", path);
            pf[npf].bad = 0;
            pf[npf].date = 0; pf[npf].size = -1;
            if (pf_sort == 1) {                                /* v165 : la date seulement pour le tri par date (1,5 ms par photo sur
                                                                  la carte SD, toutes a l'entree de l'onglet : saccade) */
                struct stat sb;
                if (stat(path, &sb) == 0) { pf[npf].date = sb.st_mtime > 1 ? (u32)sb.st_mtime : 0; pf[npf].size = (long)sb.st_size; }
            }
            npf++;
        }
        else if (depth < 2 && is_dir(path, e)) pf_scan_dir(path, depth + 1, video);
        if ((npf & 15) == 0) service_audio();
    }
    closedir(d);
}

static int pf_cmp(const void *a, const void *b) {
    const pfile_t *x = (const pfile_t *)a, *y = (const pfile_t *)b;
    if (x->video != y->video) return y->video - x->video;           /* videos en premier */
    if (pf_sort == 1 && x->date != y->date) return x->date < y->date ? 1 : -1;   /* v76 : recentes d'abord */
    return strcasecmp(x->path, y->path);
}

static void thumbs_clear(void) {
    for (int i = 0; i < TH_CACHE; i++) { free(thumbs[i].tex); memset(&thumbs[i], 0, sizeof(thumbs[i])); thumbs[i].file = -1; }
}

/* v165 : a l'entree de l'onglet, un dossier par image (avant : les 4 d'un coup, ~15 ms : une image ratee) */
static int ph_scan = 0, ph_scan_n = 0;                    /* 1..5 : prochaine etape ; npf reste a -1 pendant ce temps */
/* v316 : chaque dossier est lu par petits bouts (4 ms par image au plus) : 250 photos dans un dossier figeaient
   l'entree de l'onglet (60 a 180 ms d'un coup). Pile des dossiers ouverts (profondeur 2 au plus, comme pf_scan_dir). */
static DIR *pss_d[3];
static int pss_dep = -1, pss_video = 0;
static void pss_open(const char *dir, int video) { pss_video = video; pss_dep = -1; DIR *d = opendir(dir); if (d) { pss_d[0] = d; pss_dep = 0; } }
static char pss_dir[3][NAME_LEN];
static void pss_stop(void) { while (pss_dep >= 0) closedir(pss_d[pss_dep--]); }
static int pss_slice(void) {                              /* 1 = dossier fini */
    u32 t0 = perf_us();
    char path[NAME_LEN];
    while (pss_dep >= 0) {
        struct dirent *e = npf < PH_MAXF ? readdir(pss_d[pss_dep]) : NULL;
        if (!e) { closedir(pss_d[pss_dep--]); continue; }
        if (e->d_name[0] == '.') continue;
        int n = snprintf(path, sizeof(path), "%s/%s", pss_dir[pss_dep], e->d_name);
        if (n <= 0 || n >= (int)sizeof(path)) continue;
        const char *dot = strrchr(e->d_name, '.');
        int add = 0;
        if (!pss_video && img_ext(e->d_name)) { pf[npf].video = 0; add = 1; }
        else if (pss_video && dot && !strcasecmp(dot, ".avi")) { pf[npf].video = 1; add = 1; }
        if (add) {
            snprintf(pf[npf].path, NAME_LEN, "%s", path);
            pf[npf].bad = 0;
            pf[npf].date = 0; pf[npf].size = -1;
            if (pf_sort == 1) {
                struct stat sb;
                if (stat(path, &sb) == 0) { pf[npf].date = sb.st_mtime > 1 ? (u32)sb.st_mtime : 0; pf[npf].size = (long)sb.st_size; }
            }
            npf++;
        } else if (pss_dep < 2 && is_dir(path, e)) {
            DIR *d = opendir(path);
            if (d) { pss_dep++; pss_d[pss_dep] = d; snprintf(pss_dir[pss_dep], NAME_LEN, "%s", path); }
        }
        if (perf_us() - t0 > 4000) return 0;
    }
    return 1;
}
static void photo_rescan_end(void);
static void photo_scan_step(void) {
    static const char *const dirs[5] = { NULL, SD_ROOT "/videos", SD_ROOT "/photos", SD_ROOT "/ecran", DL_DIR };
    int st = ph_scan;
    npf = ph_scan_n;
    if (st == 1 && pss_dep < 0) { if (!pf) pf = (pfile_t *)malloc(sizeof(pfile_t) * PH_MAXF); if (!pf || !sd_ok) st = 5; else mkdir(SD_ROOT "/photos", 0777); }
    if (st >= 1 && st <= 4) {
        if (pss_dep < 0) { pss_open(dirs[st], st == 1); snprintf(pss_dir[0], NAME_LEN, "%s", dirs[st]); }
        if (!pss_slice()) { ph_scan_n = npf; npf = -1; return; }   /* (meme dossier a l'image suivante) */
    }
    if (st >= 4 || !pf) { ph_scan = 0; if (!pf) npf = 0; photo_rescan_end(); return; }
    ph_scan_n = npf; npf = -1; ph_scan = st + 1;
}
static void photo_rescan(void) {
    ph_scan = 0; pss_stop();
    if (!pf) pf = (pfile_t *)malloc(sizeof(pfile_t) * PH_MAXF);
    if (!pf) { npf = 0; return; }
    npf = 0;
    if (sd_ok) {
        mkdir(SD_ROOT "/photos", 0777);
        pf_scan_dir(SD_ROOT "/videos", 0, 1);
        pf_scan_dir(SD_ROOT "/photos", 0, 0);
        pf_scan_dir(SD_ROOT "/ecran", 0, 0);
        pf_scan_dir(DL_DIR, 0, 0);
    }
    photo_rescan_end();
}
static void photo_rescan_end(void) {
    qsort(pf, npf, sizeof(pfile_t), pf_cmp);
    thumbs_clear();
    if (pj_state == PJ_READ || pj_state == PJ_CREAD || pj_state == PJ_CWRITE) {
        if (pj_fp) fclose(pj_fp);
        if (pj_state == PJ_CWRITE) remove(pj_cpath);           /* (vignette a moitie ecrite) */
        pj_fp = NULL; free(pj_buf); pj_buf = NULL; pj_state = PJ_IDLE;
    }
    pj_file = -1;
    big_file = -1;
    if (pf_sel >= npf) pf_sel = npf > 0 ? npf - 1 : 0;
}

static void photo_enter(void) { if (npf < 0 && !ph_scan) { ph_scan = 1; ph_scan_n = 0; } }

static thumb_t *thumb_find(int f) {
    for (int i = 0; i < TH_CACHE; i++) if (thumbs[i].file == f && thumbs[i].tex) { thumbs[i].used = ++th_clock; return &thumbs[i]; }
    return NULL;
}

/* ---- v19 : vignettes rapides ----
   1. Les photos d'appareil photo / de telephone contiennent deja une petite
      image (EXIF, environ 160 x 120) : on la prend au lieu de decoder toute la
      photo (souvent 50 a 100 fois plus rapide, et les grandes photos ont
      enfin leur vignette).
   2. Chaque vignette faite est gardee dans sd:/gc-mp3/cache/vignettes : la fois
      suivante, l'onglet PHOTOS s'affiche tout de suite. */
#define TH_DIR GCD "/cache/vignettes"
#define PJ_HEAD (128 * 1024)                 /* debut du fichier lu pour chercher l'image EXIF */
static int pj_exif_done = 0;
static long pj_fsize = 0;

static u32 rd16(const unsigned char *p, int le) { return le ? (u32)(p[0] | p[1] << 8) : (u32)(p[0] << 8 | p[1]); }
static u32 rd32(const unsigned char *p, int le) { return le ? (u32)(p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24) : ((u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }
/* petite image EXIF d'un JPEG : position et taille dans b (0 = aucune) */
static long exif_thumb(const unsigned char *b, long n, long *tlen) {
    if (n < 4 || b[0] != 0xFF || b[1] != 0xD8) return 0;
    long p = 2;
    while (p + 4 <= n && b[p] == 0xFF) {
        int m = b[p + 1];
        long len = (long)(b[p + 2] << 8 | b[p + 3]);
        if (m == 0xDA || m == 0xD9 || len < 2) return 0;
        if (m == 0xE1 && p + 10 <= n && !memcmp(b + p + 4, "Exif\0\0", 6)) {
            long t = p + 10, end = p + 2 + len;
            if (end > n || t + 8 > end) return 0;
            int le = b[t] == 'I';
            if (!le && b[t] != 'M') return 0;
            u32 ifd = rd32(b + t + 4, le);
            if (t + ifd + 2 > end) return 0;
            u32 cnt = rd16(b + t + ifd, le);
            long nx = t + ifd + 2 + cnt * 12;
            if (nx + 4 > end) return 0;
            u32 ifd1 = rd32(b + nx, le);
            if (!ifd1 || t + ifd1 + 2 > end) return 0;
            cnt = rd16(b + t + ifd1, le);
            u32 off = 0, sz = 0;
            for (u32 i = 0; i < cnt; i++) {
                const unsigned char *e = b + t + ifd1 + 2 + i * 12;
                if (e + 12 > b + end) break;
                u32 tag = rd16(e, le), typ = rd16(e + 2, le);
                u32 v = typ == 3 ? rd16(e + 8, le) : rd32(e + 8, le);
                if (tag == 0x0201) off = v;
                if (tag == 0x0202) sz = v;
            }
            if (!off || sz < 100 || t + off + sz > end || b[t + off] != 0xFF || b[t + off + 1] != 0xD8) return 0;
            *tlen = sz;
            return t + off;
        }
        p += 2 + len;
    }
    return 0;
}

static void th_cache_name(int f, char *out, int cap) {
    u32 h1 = 2166136261u, h2 = 5381;
    for (const unsigned char *p = (const unsigned char *)pf[f].path; *p; p++) { h1 = (h1 ^ *p) * 16777619u; h2 = h2 * 33 + *p; }
    h2 ^= (u32)pj_fsize * 2654435761u;
    snprintf(out, cap, TH_DIR "/%08x%08x.th", (unsigned)h1, (unsigned)h2);
}
static thumb_t *thumb_slot(void) {
    int slot = 0;
    for (int i = 1; i < TH_CACHE; i++) if (thumbs[i].used < thumbs[slot].used) slot = i;
    free(thumbs[slot].tex);
    thumbs[slot].tex = NULL;
    return &thumbs[slot];
}
/* v120 : vignette deja prete : on ouvre le fichier et on lit l'en-tete ; l'image elle-meme est lue par
   morceaux dans pj_pump (avant : 32 Ko d'un coup, ~25 ms sur la carte SD : une image ratee par vignette) */
static int th_cache_open(int f) {
    th_cache_name(f, pj_cpath, sizeof(pj_cpath));
    FILE *fp = fopen(pj_cpath, "rb");
    if (!fp) return 0;
    unsigned char hd[12];
    if (fread(hd, 1, 12, fp) == 12 && !memcmp(hd, "GCT1", 4)) {
        int tw = hd[4] << 8 | hd[5], th = hd[6] << 8 | hd[7], iw = hd[8] << 8 | hd[9], ih = hd[10] << 8 | hd[11];
        if (tw > 0 && th > 0 && tw <= 256 && th <= 256 && iw <= tw && ih <= th) {
            u16 *tex = (u16 *)memalign(32, (size_t)tw * th * 2);
            if (tex) {
                pj_ctex = tex; pj_ctw = tw; pj_cth = th; pj_ciw = iw; pj_cih = ih;
                pj_fp = fp; pj_file = f; pj_len = 0; pj_size = (long)tw * th * 2;
                pj_state = PJ_CREAD;
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}
/* v120 : ecrite par morceaux (8 Ko par image) dans pj_pump au lieu d'un coup */
static void th_cache_write(int f, thumb_t *t) {
    if (!sd_ok || sd_virtual || !t->tex) return;
    mkdir(GCD "/cache", 0777);
    mkdir(TH_DIR, 0777);
    th_cache_name(f, pj_cpath, sizeof(pj_cpath));
    FILE *fp = fopen(pj_cpath, "wb");
    if (!fp) return;
    setvbuf(fp, NULL, _IOFBF, 2048);                     /* (tampon de 2 Ko : chaque morceau part tout de suite) */
    unsigned char hd[12] = { 'G', 'C', 'T', '1', (u8)(t->tw >> 8), (u8)t->tw, (u8)(t->th >> 8), (u8)t->th, (u8)(t->iw >> 8), (u8)t->iw, (u8)(t->ih >> 8), (u8)t->ih };
    if (fwrite(hd, 1, 12, fp) != 12) { fclose(fp); remove(pj_cpath); return; }
    pj_fp = fp; pj_ct = t; pj_ctex = t->tex; pj_file = f; pj_len = 0; pj_size = (long)t->tw * t->th * 2;
    pj_state = PJ_CWRITE;
}

/* lance le chargement de l'image f (vignette ou plein ecran) */
static void pj_start(int f, int big) {
    if (pj_state != PJ_IDLE || f < 0 || f >= npf || pf[f].video) return;
    long size = -1;
    if (!big) {                                                       /* vignette deja prete ? */
        /* v165 : taille par stat (1,5 ms) au lieu d'ouvrir la photo (3 ms) ; la photo n'est ouverte que s'il faut
           faire la vignette (avant : photo ouverte, fermee, vignette ouverte, et photo rouverte : 2 a 3 ouvertures) */
        if (pf[f].size < 0) { struct stat sb; pf[f].size = stat(pf[f].path, &sb) == 0 ? (long)sb.st_size : 0; }
        size = pf[f].size;
        pj_fsize = size;
        if (size > 0 && th_cache_open(f)) return;
        pj_fp = size > 0 ? fopen(pf[f].path, "rb") : NULL;
    } else {
        pj_fp = fopen(pf[f].path, "rb");
        if (pj_fp && fseek(pj_fp, 0, SEEK_END) == 0) size = ftell(pj_fp);
        pj_fsize = size;
    }
    long want = big || size <= PJ_HEAD ? size : PJ_HEAD;              /* vignette : le debut d'abord (EXIF) */
    long maxf = big ? PHOTO_MAX_FILE : 64L * 1024 * 1024;
    if (!pj_fp || size <= 0 || size > maxf || fseek(pj_fp, 0, SEEK_SET) != 0 || !(pj_buf = (unsigned char *)malloc(want))) {
        if (pj_fp) fclose(pj_fp);
        pj_fp = NULL;
        pf[f].bad = 1;
        return;
    }
    pj_size = want;
    pj_len = 0;
    pj_file = f;
    pj_big = big;
    pj_exif_done = big;
    pj_state = PJ_READ;
}

static void pj_pump(void) {
    if (pj_state == PJ_CREAD || pj_state == PJ_CWRITE) {
        int wr = pj_state == PJ_CWRITE;
        if (wr && (pj_ct->tex != pj_ctex || pj_ct->file != pj_file)) {   /* (vignette jetee entre-temps) */
            fclose(pj_fp); pj_fp = NULL; remove(pj_cpath); pj_state = PJ_IDLE; return;
        }
        u32 t0 = perf_us();
        do {
            long n = pj_size - pj_len, ck = wr ? 2048 : 4096;     /* (ecriture SD ~600 Ko/s : 2 Ko = ~3 ms) */
            if (n > ck) n = ck;
            size_t got = wr ? fwrite((u8 *)pj_ctex + pj_len, 1, n, pj_fp) : fread((u8 *)pj_ctex + pj_len, 1, n, pj_fp);
            if (got != (size_t)n) {
                fclose(pj_fp); pj_fp = NULL;
                if (wr) remove(pj_cpath); else free(pj_ctex);
                pj_ctex = NULL; pj_state = PJ_IDLE; return;
            }
            pj_len += n;
            if (wr) { fflush(pj_fp); break; }          /* (ecriture : un morceau par image, envoye tout de suite) */
        } while (pj_len < pj_size && perf_us() - t0 < 5000);
        if (pj_len < pj_size) return;
        fclose(pj_fp); pj_fp = NULL;
        pj_state = PJ_IDLE;
        if (!wr) {
            DCFlushRange(pj_ctex, (u32)pj_size);
            thumb_t *t = thumb_slot();
            t->file = pj_file; t->tex = pj_ctex; t->tw = pj_ctw; t->th = pj_cth; t->iw = pj_ciw; t->ih = pj_cih; t->used = ++th_clock;
            GX_InitTexObj(&t->obj, t->tex, t->tw, t->th, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
            GX_InitTexObjFilterMode(&t->obj, GX_LINEAR, GX_LINEAR);
            GX_InvalidateTexAll();
        }
        pj_ctex = NULL;
        return;
    }
    if (pj_state == PJ_READ) {
        /* v80 : lecture par morceaux de 8 Ko, au plus 6 ms par image (avant : 32 Ko d'un coup,
           ~21 ms sur une carte SD de GameCube : chaque image ratait le balayage, ~40 ms) */
        u32 t0 = perf_us();
        do {
            long want = pj_size - pj_len;
            if (want > 4096) want = 4096;                /* (v120 : 4 Ko : ~3 ms ; 8 Ko depassaient le budget) */
            size_t got = fread(pj_buf + pj_len, 1, want, pj_fp);
            if (got == 0) { fclose(pj_fp); pj_fp = NULL; free(pj_buf); pj_buf = NULL; pf[pj_file].bad = 1; pj_state = PJ_IDLE; return; }
            pj_len += (long)got;
        } while (pj_len < pj_size && perf_us() - t0 < 5000);
        if (pj_len < pj_size) return;
        if (!pj_exif_done) {                             /* vignette : petite image EXIF ? */
            pj_exif_done = 1;
            long tl = 0, to = exif_thumb(pj_buf, pj_len, &tl);
            if (to > 0) {
                memmove(pj_buf, pj_buf + to, tl);
                pj_len = pj_size = tl;
            } else if (pj_fsize > pj_size) {             /* non : toute la photo */
                unsigned char *nb = pj_fsize <= PHOTO_MAX_FILE ? (unsigned char *)realloc(pj_buf, pj_fsize) : NULL;
                if (!nb) { fclose(pj_fp); pj_fp = NULL; free(pj_buf); pj_buf = NULL; pf[pj_file].bad = 1; pj_state = PJ_IDLE; return; }
                pj_buf = nb;
                pj_size = pj_fsize;
                return;
            }
        }
        fclose(pj_fp);
        pj_fp = NULL;
        int iw, ih, ic;
        int info = is_webp(pj_buf, (int)pj_len) ? WebPGetInfo(pj_buf, (size_t)pj_len, &iw, &ih)
                                                 : stbi_info_from_memory(pj_buf, (int)pj_len, &iw, &ih, &ic);
        if (!info || (long)iw * ih > PHOTO_MAX_PIXELS) {
            free(pj_buf); pj_buf = NULL; pf[pj_file].bad = 1; pj_state = PJ_IDLE; return;
        }
        pj_state = PJ_DEC;
    }
    if (pj_state == PJ_DEC && pj_buf) {                 /* attend que le decodeur soit libre */
        if (dj_state != 0 || il_state == IL_DECODE || ph_state == PH_DEC || !dec_ensure()) return;
        dj_in = pj_buf;
        dj_len = (int)pj_len;
        pj_buf = NULL;
        dj_aw = pj_big ? 640 : TH_W;
        dj_ah = pj_big ? 448 : TH_H;
        dj_maxw = dj_aw;
        dj_photo = 1;
        dj_owner = DJ_OWNER_PHOTO;
        dj_state = 1;
        return;
    }
    if (pj_state == PJ_DEC && !pj_buf && dj_state == 2 && dj_owner == DJ_OWNER_PHOTO) {
        dj_state = 0;
        dj_owner = 0;
        pj_state = PJ_IDLE;
        if (!dj_ok || !dj_tex) { free(dj_tex); dj_tex = NULL; if (pj_file >= 0 && pj_file < npf) pf[pj_file].bad = 1; return; }
        DCFlushRange(dj_tex, (u32)dj_tw * dj_th * 2);
        if (pj_big) {
            free(big_tex);
            big_tex = dj_tex;
            big_tw = dj_tw; big_th = dj_th; big_iw = dj_iw; big_ih = dj_ih;
            big_file = pj_file;
            GX_InitTexObj(&big_obj, big_tex, big_tw, big_th, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
            GX_InitTexObjFilterMode(&big_obj, GX_LINEAR, GX_LINEAR);
        } else {
            thumb_t *t = thumb_slot();
            t->file = pj_file; t->tex = dj_tex; t->tw = dj_tw; t->th = dj_th; t->iw = dj_iw; t->ih = dj_ih; t->used = ++th_clock;
            GX_InitTexObj(&t->obj, t->tex, t->tw, t->th, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
            GX_InitTexObjFilterMode(&t->obj, GX_LINEAR, GX_LINEAR);
            th_cache_write(pj_file, t);
        }
        dj_tex = NULL;
        GX_InvalidateTexAll();
    }
}

/* ---------- Video AVI (MJPEG + PCM) ---------- */
static FILE *av_fp = NULL;
static int av_on = 0, av_w = 0, av_h = 0, av_rate = 0, av_ch = 0, av_bits = 16, av_eof = 0;
static u32 av_usf = 50000;                   /* microsecondes par image */
static long av_movi_end = 0;
static int av_vframe = 0;                    /* numero de l'image video lue */
static unsigned char *av_jpg = NULL;         /* image en attente de decodage */
static int av_jpg_len = 0, av_jpg_frame = 0;
static int av_dec_frame = -1;                /* image en cours de decodage */
static u16 *av_tex = NULL;
static GXTexObj av_obj;
static int av_tw, av_th, av_iw, av_ih, av_shown = 0, av_dropped = 0;
static u64 av_t0 = 0;

static void video_stop_u(void);
static void video_stop(void) { DEC_LOCK(); video_stop_u(); DEC_UNLOCK(); }   /* v157 */
static void video_stop_u(void) {
    if (av_fp) { fclose(av_fp); av_fp = NULL; }
    free(av_jpg); av_jpg = NULL;
    av_on = 0;
    if (video_playing && src == SRC_FILE && cur_track < 0) { src = SRC_NONE; playing = 0; f_head = f_tail = f_count = 0; }
    video_playing = 0;
}

static int video_open_u(const char *path);
static int video_open(const char *path) { DEC_LOCK(); int r_ = video_open_u(path); DEC_UNLOCK(); return r_; }   /* v157 */
static int video_open_u(const char *path) {
    crumb("video : %s", path);
    stop_track();
    stop_station();
    av_fp = fopen(path, "rb");
    if (!av_fp) return 0;
    u8 h[12];
    if (fread(h, 1, 12, av_fp) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "AVI ", 4)) { fclose(av_fp); av_fp = NULL; return 0; }
    av_w = av_h = av_rate = av_ch = 0;
    av_movi_end = 0;
    /* parcourt les listes : hdrl (formats) puis movi (donnees) */
    long pos = 12, fsz;
    fseek(av_fp, 0, SEEK_END); fsz = ftell(av_fp);
    int in_strl_type = 0;
    while (pos + 8 <= fsz) {
        u8 c[12];
        if (fseek(av_fp, pos, SEEK_SET) != 0 || fread(c, 1, 8, av_fp) != 8) break;
        u32 sz = le32(c + 4);
        if (sz > (u32)(fsz - pos - 8)) sz = (u32)(fsz - pos - 8);   /* taille hors fichier : on borne */
        if (!memcmp(c, "LIST", 4)) {
            if (fread(c + 8, 1, 4, av_fp) != 4) break;
            if (!memcmp(c + 8, "movi", 4)) { av_movi_end = pos + 8 + sz; fseek(av_fp, pos + 12, SEEK_SET); break; }
            pos += 12;                                       /* on entre dans la liste */
            continue;
        }
        u8 d[64];
        int m = sz < sizeof(d) ? (int)sz : (int)sizeof(d);
        if (fread(d, 1, m, av_fp) != (size_t)m) break;
        if (!memcmp(c, "avih", 4) && m >= 40) { av_usf = le32(d); av_w = (int)le32(d + 32); av_h = (int)le32(d + 36); }
        else if (!memcmp(c, "strh", 4) && m >= 8) in_strl_type = !memcmp(d, "vids", 4) ? 1 : !memcmp(d, "auds", 4) ? 2 : 0;
        else if (!memcmp(c, "strf", 4) && in_strl_type == 2 && m >= 16) {
            int tag = (int)le16(d);
            av_ch = (int)le16(d + 2); av_rate = (int)le32(d + 4); av_bits = (int)le16(d + 14);
            if (tag != 1 || (av_bits != 8 && av_bits != 16) || av_ch < 1 || av_ch > 2
                || av_rate < 4000 || av_rate > 96000) av_rate = 0;   /* son PCM 8/16 bits seulement */
        }
        pos += 8 + sz + (sz & 1);
    }
    if (!av_movi_end || av_w <= 0 || av_h <= 0 || av_w > 640 || av_h > 480) { fclose(av_fp); av_fp = NULL; return 0; }
    if (av_usf < 10000 || av_usf > 1000000) av_usf = 50000;
    av_on = 1;
    av_eof = 0;
    av_vframe = 0;
    av_dec_frame = -1;
    av_shown = av_dropped = 0;
    av_t0 = now_ms();
    /* le son passe par la file habituelle (comme un morceau) */
    f_head = f_tail = f_count = 0;
    rpos = 0.0; prevL = prevR = 0;
    consumed = 0;
    src = SRC_FILE; playing = 1; paused = 0; cur_track = -1; eof = 0;
    video_playing = 1;
    return 1;
}

/* position de lecture (secondes x 1000) : l'horloge du son, sinon le temps qui passe */
static u32 av_clock_ms(void) {
    if (av_rate > 0) return (u32)((u64)consumed_heard() * 1000 / 48000);
    return (u32)(now_ms() - av_t0);
}

static void video_pump(void) {
    if (!av_on) return;
    last_input_ms = now_ms();                           /* pas d'economiseur pendant une video */
    /* image decodee : on l'affiche */
    if (dj_state == 2 && dj_owner == DJ_OWNER_VIDEO) {
        dj_state = 0;
        dj_owner = 0;
        if (dj_ok && dj_tex) {
            free(av_tex);
            av_tex = dj_tex;
            av_tw = dj_tw; av_th = dj_th; av_iw = dj_iw; av_ih = dj_ih;
            DCFlushRange(av_tex, (u32)av_tw * av_th * 2);
            GX_InitTexObj(&av_obj, av_tex, av_tw, av_th, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
            GX_InitTexObjFilterMode(&av_obj, GX_LINEAR, GX_LINEAR);
            GX_InvalidateTexAll();
            av_shown++;
        } else free(dj_tex);
        dj_tex = NULL;
    }
    /* lecture du fichier : assez de son d'avance, et l'image du moment */
    int guard = 0;
    while (!av_eof && guard++ < 64) {
        u32 clock = av_clock_ms();
        u32 vt = (u32)((u64)av_vframe * av_usf / 1000);
        int need_audio = av_rate > 0 && f_count < 48000;
        int need_video = vt <= clock + 300;
        if (!need_audio && !need_video) break;
        if (FIFO_SIZE - f_count < 48000) break;
        u8 c[8];
        long pos = ftell(av_fp);
        if (pos + 8 > av_movi_end || fread(c, 1, 8, av_fp) != 8) { av_eof = 1; break; }
        u32 sz = le32(c + 4);
        if (!memcmp(c, "LIST", 4)) { fseek(av_fp, 4, SEEK_CUR); continue; }     /* "rec " */
        if (sz > 4 * 1024 * 1024) { av_eof = 1; break; }
        if (c[2] == 'd' && (c[3] == 'c' || c[3] == 'b')) {                   /* image video */
            int fr = av_vframe++;
            u32 ft = (u32)((u64)fr * av_usf / 1000);
            if (ft + 200 < clock || (av_jpg && av_jpg_frame > fr)) { fseek(av_fp, sz + (sz & 1), SEEK_CUR); av_dropped++; continue; }
            unsigned char *b = (unsigned char *)malloc(sz ? sz : 1);
            if (!b || fread(b, 1, sz, av_fp) != sz) { free(b); av_eof = 1; break; }
            if (sz & 1) fseek(av_fp, 1, SEEK_CUR);
            free(av_jpg);
            av_jpg = b; av_jpg_len = (int)sz; av_jpg_frame = fr;
        } else if (c[2] == 'w' && c[3] == 'b' && av_rate > 0) {              /* son PCM */
            static u8 ab[16384];
            static s16 L[8192], R[8192];
            u32 left = sz;
            while (left > 0) {
                u32 m = left > sizeof(ab) ? sizeof(ab) : left;
                if (fread(ab, 1, m, av_fp) != m) { av_eof = 1; break; }
                left -= m;
                int bps = av_bits / 8 * av_ch, n = (int)(m / bps);
                for (int i = 0; i < n; i++) {
                    const u8 *s = ab + i * bps;
                    if (av_bits == 8) { L[i] = (s16)(((int)s[0] - 128) * 256); R[i] = av_ch > 1 ? (s16)(((int)s[1] - 128) * 256) : L[i]; }
                    else { L[i] = (s16)le16(s); R[i] = av_ch > 1 ? (s16)le16(s + 2) : L[i]; }
                }
                if (n > 0) push_frame(L, R, n, (unsigned)av_rate);
            }
            if (sz & 1) fseek(av_fp, 1, SEEK_CUR);
        } else fseek(av_fp, sz + (sz & 1), SEEK_CUR);
    }
    /* image suivante au decodeur */
    if (av_jpg && dj_state == 0 && il_state != IL_DECODE && ph_state != PH_DEC && dec_ensure()) {
        dj_in = av_jpg;
        dj_len = av_jpg_len;
        av_jpg = NULL;
        dj_aw = av_w; dj_ah = av_h; dj_maxw = av_w;
        dj_photo = 1;
        dj_owner = DJ_OWNER_VIDEO;
        av_dec_frame = av_jpg_frame;
        dj_state = 1;
    }
    if (av_eof && f_count == 0 && !av_jpg) { video_stop(); pv_mode = 0; }
}

/* v191 : meme chose, avec une opacite en haut et en bas (0 - 255 : alpha, sur le fond) : economiseur "Jaquettes" (fondu) */
static void draw_tex_fit_shade(GXTexObj *o, int tw, int th, int iw, int ih, f32 x, f32 y, f32 w, f32 h, int lt, int lb) {
    f32 cxd = x + w / 2;
    w *= ux_k;
    uxsave_t uz = ux_native(cxd);
    x = cxd - w / 2;
    f32 s = w / iw;
    if (ih * s > h) s = h / ih;
    f32 dw = iw * s, dh = ih * s, x0 = x + (w - dw) / 2, y0 = y + (h - dh) / 2;
    f32 u1 = (f32)iw / tw, v1 = (f32)ih / th;
    u8 a = (u8)(lt < 0 ? 0 : lt > 255 ? 255 : lt), b = (u8)(lb < 0 ? 0 : lb > 255 ? 255 : lb);
    gx_set_image(o);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(UX(x0), y0);           GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(0.0f, 0.0f);
    GX_Position2f32(UX(x0 + dw), y0);      GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(u1, 0.0f);
    GX_Position2f32(UX(x0 + dw), y0 + dh); GX_Color4u8(255, 255, 255, b); GX_TexCoord2f32(u1, v1);
    GX_Position2f32(UX(x0), y0 + dh);      GX_Color4u8(255, 255, 255, b); GX_TexCoord2f32(0.0f, v1);
    GX_End();
    ux_restore(uz);
}
static void draw_tex_fit(GXTexObj *o, int tw, int th, int iw, int ih, f32 x, f32 y, f32 w, f32 h) {
    f32 cxd = x + w / 2;                                 /* (16:9 : image sans deformation, place reelle) */
    w *= ux_k;
    uxsave_t uz = ux_native(cxd);
    x = cxd - w / 2;
    f32 s = w / iw;
    if (ih * s > h) s = h / ih;
    f32 dw = iw * s, dh = ih * s, x0 = x + (w - dw) / 2, y0 = y + (h - dh) / 2;
    f32 u1 = (f32)iw / tw, v1 = (f32)ih / th;
    gx_set_image(o);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(UX(x0), y0);           GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f);
    GX_Position2f32(UX(x0 + dw), y0);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(u1, 0.0f);
    GX_Position2f32(UX(x0 + dw), y0 + dh); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(u1, v1);
    GX_Position2f32(UX(x0), y0 + dh);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, v1);
    GX_End();
    ux_restore(uz);
}

static void pv_open(int f) {
    if (f < 0 || f >= npf) return;
    crumb("photo : %s", pf[f].path);
    pf_sel = f;
    if (pf[f].video) {
        if (video_open(pf[f].path)) pv_mode = 2;
        return;
    }
    pv_mode = 1;
    pv_slide_t = now_ms();
}

static void photo_input(u32 down, u32 nav, int fire) {
    if (ph_scan) return;                                  /* v165 : (liste en cours de lecture : quelques images) */
    if (npf < 0) photo_rescan();
    if (pv_mode == 2) {                                   /* video */
        if (down & (PAD_BUTTON_B | PAD_BUTTON_A)) { video_stop(); pv_mode = 0; }
        if (down & PAD_BUTTON_Y) paused = !paused;
        return;
    }
    if (pv_mode == 1 && npf <= 0) pv_mode = 0;            /* (v72 : plus de photo : jamais de division par zero) */
    if (pv_mode == 1) {                                   /* plein ecran */
        if (fire && (nav & (PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN))) { pf_sel = (pf_sel + 1) % npf; pv_slide_t = now_ms(); }
        if (fire && (nav & (PAD_BUTTON_LEFT | PAD_BUTTON_UP))) { pf_sel = (pf_sel + npf - 1) % npf; pv_slide_t = now_ms(); }
        if (down & PAD_BUTTON_A) { pv_slide = !pv_slide; pv_slide_t = now_ms(); }
        if (down & PAD_BUTTON_B) { pv_mode = 0; pv_slide = 0; }
        if ((down & PAD_BUTTON_X) && pf_sel >= 0 && pf_sel < npf && !pf[pf_sel].video && !pf[pf_sel].bad) wall_set(pf[pf_sel].path);
        if (pf_sel >= 0 && pf_sel < npf && pf[pf_sel].video) { pv_mode = 0; pv_slide = 0; }
        if ((down & PAD_BUTTON_Y) && src == SRC_FILE && playing) paused = !paused;
        return;
    }
    if (fire && npf > 0) {
        if (nav & PAD_BUTTON_RIGHT) pf_sel++;
        if (nav & PAD_BUTTON_LEFT) pf_sel--;
        if (nav & PAD_BUTTON_DOWN) pf_sel += GRID_COLS;
        if (nav & PAD_BUTTON_UP) pf_sel -= GRID_COLS;
        if (pf_sel < 0) pf_sel = 0;
        if (pf_sel > npf - 1) pf_sel = npf - 1;
    }
    if ((down & PAD_BUTTON_A) && npf > 0) pv_open(pf_sel);
    if (down & PAD_BUTTON_X) photo_rescan();
    if ((down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) && npf > 0) {   /* v76 : trier par nom / par date */
        char keep[NAME_LEN];
        snprintf(keep, sizeof(keep), "%s", pf[pf_sel >= 0 && pf_sel < npf ? pf_sel : 0].path);
        pf_sort ^= 1;
        if (pf_sort == 1)                                  /* v165 : (dates lues seulement maintenant) */
            for (int f = 0; f < npf; f++) if (pf[f].size < 0) { struct stat sb; if (stat(pf[f].path, &sb) == 0) { pf[f].date = sb.st_mtime > 1 ? (u32)sb.st_mtime : 0; pf[f].size = (long)sb.st_size; } }
        qsort(pf, npf, sizeof(pfile_t), pf_cmp);
        thumbs_clear();
        for (int f = 0; f < npf; f++) if (!strcmp(pf[f].path, keep)) { pf_sel = f; break; }
    }
    if ((down & PAD_BUTTON_Y) && npf > 0) { pv_open(pf_sel); if (pv_mode == 1) pv_slide = 1; }
}

static void photo_pump_tab(void) {
    video_pump();
    if (ph_scan) { if (tab == TAB_PHOTO) photo_scan_step(); else { ph_scan = 0; npf = -1; pss_stop(); } return; }   /* v165 */
    if (tab != TAB_PHOTO || npf <= 0) { if (pj_state != PJ_IDLE) pj_pump(); return; }
    if (pv_mode == 1) {
        if (pv_slide && now_ms() - pv_slide_t > SLIDE_MS) {     /* diaporama : photo suivante (pas les videos) */
            for (int i = 1; i <= npf; i++) { int f = (pf_sel + i) % npf; if (!pf[f].video) { pf_sel = f; break; } }
            pv_slide_t = now_ms();
        }
        if (big_file != pf_sel && pj_state == PJ_IDLE && !pf[pf_sel].bad) pj_start(pf_sel, 1);
        last_input_ms = pv_slide ? now_ms() : last_input_ms;   /* pas d'economiseur pendant le diaporama */
    } else if (pj_state == PJ_IDLE) {
        /* vignettes visibles d'abord, puis celles d'a cote */
        for (int i = 0; i < GRID_COLS * (GRID_ROWS + 1); i++) {
            int f = pf_top + i;
            if (f >= npf) break;
            if (!pf[f].bad && !pf[f].video && !thumb_find(f)) { pj_start(f, 0); return; }   /* v165 : (lecture a l'image suivante :
                                                                                               ouverture + lecture dans la meme image = image ratee) */
        }
    }
    pj_pump();
}

static void draw_tab_photo(void) {
    char buf[NAME_LEN], d[NAME_LEN];
    if (pv_mode == 2) {                                   /* video plein ecran */
        gx_rect(0, 0, 640, 480, RGBA(0, 0, 0, 255));
        if (av_tex) draw_tex_fit(&av_obj, av_tw, av_th, av_iw, av_ih, 0, 0, 640, 480);
        else gx_text_c(F_MED, 320, 240, "Chargement...", RGBA(255, 255, 255, 255));
        return;
    }
    if (pv_mode == 1) {                                   /* photo plein ecran */
        gx_rect(0, 0, 640, 480, RGBA(0, 0, 0, 255));
        if (big_tex && big_file == pf_sel) draw_tex_fit(&big_obj, big_tw, big_th, big_iw, big_ih, 0, 0, 640, 448);
        else if (pf[pf_sel].bad) gx_text_c(F_MED, 320, 230, "Photo illisible ou trop grande (1920 x 1080 maximum)", RGBA(255, 210, 120, 255));
        else gx_text_c(F_MED, 320, 230, "Chargement...", RGBA(255, 255, 255, 255));
        text_display(base_of(pf[pf_sel].path), d, sizeof(d));
        snprintf(buf, sizeof(buf), "%d / %d   %s%s     X : fond d'\xC3\xA9" "cran", pf_sel + 1, npf, d, pv_slide ? "   (diaporama)" : "");
        gx_text_c(F_SMALL, 320, 468, buf, RGBA(200, 200, 220, 220));
        return;
    }
    draw_panel(11, 73, 618, 338);
    snprintf(buf, sizeof(buf), "%d", npf > 0 ? npf : 0);
    draw_tabs(buf);
    if (ph_scan) { }                                      /* v165 : (lecture de la liste : 4 images) */
    else if (npf <= 0) {
        gx_text1(F_MED, 34, 150, "Aucune photo ni vid\xC3\xA9o", RGBA(255, 205, 110, 255));
        gx_text1(F_SMALL, 34, 172, "Photos (.jpg, .png, 1920 x 1080 maximum) : SD:/photos", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 34, 190, "Vid\xC3\xA9os .avi (MJPEG + son PCM, 640 x 480 maximum) : SD:/videos", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 34, 208, "Le plus simple : onglet T\xC3\x89L\xC3\x89PHONE, Envoyer.", RGBA(190, 180, 235, 255));
    } else {
        int row = pf_sel / GRID_COLS;
        if (row < pf_top / GRID_COLS) pf_top = row * GRID_COLS;
        if (row >= pf_top / GRID_COLS + GRID_ROWS) pf_top = (row - GRID_ROWS + 1) * GRID_COLS;
        for (int i = 0; i < GRID_COLS * GRID_ROWS; i++) {
            int f = pf_top + i;
            if (f >= npf) break;
            f32 x = 26 + (i % GRID_COLS) * 150, y = 116 + (i / GRID_COLS) * 96;
            int on = (f == pf_sel);
            gp_zone(x - 4, y - 4, TH_W + 8, TH_H, &pf_sel, f, GPZ_SEL);
            if (on) gx_rrect_fill(x - 4, y - 4, TH_W + 8, TH_H - 12 + 8, 6, RGBA(124, 54, 240, 255), RGBA(80, 26, 180, 255));
            gx_rect(x, y, TH_W, TH_H - 22, RGBA(20, 14, 50, 255));
            thumb_t *t = thumb_find(f);
            if (t) draw_tex_fit(&t->obj, t->tw, t->th, t->iw, t->ih, x, y, TH_W, TH_H - 22);
            else if (pf[f].video) {
                gx_tri(x + 54, y + 22, x + 54, y + 52, x + 80, y + 37, RGBA(230, 220, 255, 255));
                gx_text_c(F_SMALL, x + TH_W / 2, y + 68, "VID\xC3\x89O", RGBA(200, 185, 250, 255));
            } else if (pf[f].bad) gx_text_c(F_SMALL, x + TH_W / 2, y + 40, "trop grande", RGBA(255, 190, 120, 255));
            text_display(base_of(pf[f].path), d, sizeof(d));
            fit_text_sel(on, F_SMALL, d, TH_W, buf, sizeof(buf));
            gx_text1(F_SMALL, x, y + TH_H - 6, buf, on ? RGBA(255, 255, 255, 255) : RGBA(190, 180, 235, 255));
        }
    }
    if (npf > 0) {
        snprintf(buf, sizeof(buf), "L / R : tri %s", pf_sort ? TR("par date") : TR("par nom"));
        char nb[16]; snprintf(nb, sizeof(nb), "%d", npf);       /* v316 : (styles 1 a 3 : le nombre est deja a droite) */
        f32 xr = (ui_skin >= 1 && ui_skin <= 3) ? 614 - text_width(F_SMALL, nb, 0) - 14 : 614;
        gx_text_r(F_SMALL, xr, 99, buf, RGBA(150, 135, 210, 255));
    }
    static const char *const k[] = { "A", "Y", "X" };
    static const char *const l[] = { "OUVRIR", "DIAPORAMA", "ACTUALISER" };
    draw_legend_keys(k, l, 3);
}

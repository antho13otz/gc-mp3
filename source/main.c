// GC-MP3 "Indigo Audio Player" - lecteur MP3 pour GameCube (libogc + GX + minimp3)
// Lit les fichiers .mp3 du dossier /mp3 de la carte SD.
// Commandes : Haut/Bas = choisir (maintenir = defiler), Gauche/Droite = page,
//             A = lire, B = stop, Y = pause, L/R = piste precedente/suivante,
//             START = quitter
#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/system.h>
#include <ogc/lwp_watchdog.h>
#include <fat.h>
#include <sdcard/gcsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <dirent.h>

// Decodeur MP3 : minimp3 (fichier unique, domaine public), telecharge par le workflow
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

// Atlas de polices lissees (genere par tools/gen_font.py)
#include "font_atlas.h"

#define MP3_DIR        "sd:/mp3"
#define MAX_FILES      512
#define NAME_LEN       128
#define MAX_FILE_SIZE  (18 * 1024 * 1024)   // un MP3 est charge entierement en RAM
#define DMA_SAMPLES    2048                 // echantillons stereo par tampon (~43 ms)
#define NUM_BUFS       3                    // anneau de 3 tampons DMA
#define DMA_BYTES      (DMA_SAMPLES * 4)
#define FIFO_SIZE      16384
#define FIFO_MARGIN    8192                 // reserve suffisante avant le decodage d'une trame
#define GX_FIFO_SIZE   (256 * 1024)         // FIFO GX alignee, taille multiple de 32 octets
#define LIST_ROWS      14
#define PI_F           3.14159265f

typedef struct { s16 l, r; } stereo_t;
typedef struct { u8 r, g, b, a; } col_t;
#define RGBA(r, g, b, a) ((col_t){ (u8)(r), (u8)(g), (u8)(b), (u8)(a) })

static void *xfb[2] = { NULL, NULL };
static void *gx_fifo = NULL;
static GXRModeObj *rmode = NULL;
static int displayed_fb = 0;

// ---------- Liste des fichiers ----------
static char names[MAX_FILES][NAME_LEN];
static int  dur_cache[MAX_FILES];          // duree en s ; -2 = pas encore lue, -1 = inconnue
static int  nfiles = 0;
static int  dur_budget = 0;                // lectures de duree autorisees pour l'image en cours

static int cmp_names(const void *a, const void *b) {
    return strcasecmp((const char *)a, (const char *)b);
}

static void scan_files(void) {
    nfiles = 0;
    for (int i = 0; i < MAX_FILES; i++) dur_cache[i] = -2;
    DIR *d = opendir(MP3_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && nfiles < MAX_FILES) {
        size_t l = strlen(e->d_name);
        if (l > 4 && l < NAME_LEN && strcasecmp(e->d_name + l - 4, ".mp3") == 0) {
            strcpy(names[nfiles++], e->d_name);
        }
    }
    closedir(d);
    qsort(names, nfiles, NAME_LEN, cmp_names);
}

static int mount_sd(void) {
    if (fatMountSimple("sd", &__io_gcsd2)) return 1;   // SD2SP2 (port serie 2)
    if (fatMountSimple("sd", &__io_gcsda)) return 1;   // SD Gecko slot A
    if (fatMountSimple("sd", &__io_gcsdb)) return 1;   // SD Gecko slot B
    return 0;
}

// ---------- Duree d'un MP3 (lue dans l'en-tete, sans decoder) ----------
static u32 be32(const unsigned char *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static u32 synchsafe(const unsigned char *p) {
    return ((u32)(p[0] & 0x7f) << 21) | ((u32)(p[1] & 0x7f) << 14) |
           ((u32)(p[2] & 0x7f) << 7)  |  (u32)(p[3] & 0x7f);
}

static int mp3_duration(const char *path) {
    static const int br1[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const int br2[16] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
    static const int sr1[4]  = {44100, 48000, 32000, 0};
    static const int sr2[4]  = {22050, 24000, 16000, 0};
    static const int sr25[4] = {11025, 12000, 8000, 0};
    static unsigned char b[4096];

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long size = ftell(fp);
    long off = 0;
    if (size <= 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return -1; }

    size_t n = fread(b, 1, sizeof(b), fp);
    if (n >= 10 && memcmp(b, "ID3", 3) == 0) {
        off = 10 + (long)synchsafe(b + 6) + ((b[5] & 0x10) ? 10 : 0);
        if (off >= size || fseek(fp, off, SEEK_SET) != 0) { fclose(fp); return -1; }
        n = fread(b, 1, sizeof(b), fp);
    }
    fclose(fp);
    if (n < 8) return -1;

    for (size_t i = 0; i + 4 < n && i < 2048; i++) {
        if (b[i] != 0xFF || (b[i + 1] & 0xE0) != 0xE0) continue;
        int ver   = (b[i + 1] >> 3) & 3;           // 0 = 2.5, 2 = 2, 3 = 1
        int layer = (b[i + 1] >> 1) & 3;           // 1 = Layer III
        int bri   = b[i + 2] >> 4;
        int sri   = (b[i + 2] >> 2) & 3;
        if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3) continue;
        int mono = ((b[i + 3] >> 6) == 3);
        int kbps = (ver == 3) ? br1[bri] : br2[bri];
        int hz   = (ver == 3) ? sr1[sri] : (ver == 2 ? sr2[sri] : sr25[sri]);
        int spf  = (ver == 3) ? 1152 : 576;
        int side = (ver == 3) ? (mono ? 17 : 32) : (mono ? 9 : 17);
        size_t xo = i + 4 + side + ((b[i + 1] & 1) ? 0 : 2);   // CRC eventuel
        if (xo + 12 <= n && (memcmp(b + xo, "Xing", 4) == 0 || memcmp(b + xo, "Info", 4) == 0)) {
            u32 flags = be32(b + xo + 4);
            if ((flags & 1) && hz > 0)
                return (int)(((double)be32(b + xo + 8) * spf) / hz + 0.5);
        }
        if (kbps <= 0) return -1;
        long bytes = size - off - (long)i;
        if (bytes <= 0) return -1;
        return (int)(((double)bytes * 8.0) / (kbps * 1000.0) + 0.5);
    }
    return -1;
}

/* Duree de la piste k. Une seule lecture de carte SD par image au maximum,
   pour ne pas bloquer la boucle (et donc l'audio) en faisant defiler la liste. */
static int track_duration(int k) {
    if (k < 0 || k >= nfiles) return -1;
    if (dur_cache[k] == -2 && dur_budget > 0) {
        char path[256];
        dur_budget--;
        snprintf(path, sizeof(path), "%s/%s", MP3_DIR, names[k]);
        dur_cache[k] = mp3_duration(path);
    }
    return dur_cache[k];
}

// ---------- Audio (DMA, anneau de 3 tampons, 48 kHz) ----------
static s16 dmabuf[NUM_BUFS][DMA_SAMPLES * 2] ATTRIBUTE_ALIGN(32);
static volatile u32 cb_n = 0;                    // prochain bloc a programmer
static volatile u32 filled_blk = 0xFFFFFFFFu;    // dernier bloc rempli par la boucle principale

static stereo_t fifo[FIFO_SIZE];
static int f_head = 0, f_tail = 0, f_count = 0;

#define VIZ_N 512                                // fenetre d'analyse du spectre
static s16 viz[VIZ_N];
static int viz_pos = 0;
static u32 consumed = 0;                         // echantillons 48 kHz joues depuis le debut de la piste

// Appelee a chaque fin de bloc : programme le bloc suivant. Si la boucle
// principale n'a pas rempli ce bloc a temps (chargement d'un fichier...),
// on joue du silence plutot que de rejouer d'anciennes donnees en boucle.
static void dma_cb(void) {
    u32 blk = cb_n;
    s16 *buf = dmabuf[blk % NUM_BUFS];
    if (filled_blk != blk) {
        memset(buf, 0, DMA_BYTES);
        DCFlushRange(buf, DMA_BYTES);
    }
    AUDIO_InitDMA((u32)buf, DMA_BYTES);
    cb_n = blk + 1;
}

static int paused = 0;

static void fill_buf(int idx) {
    s16 *d = dmabuf[idx];
    for (int i = 0; i < DMA_SAMPLES; i++) {
        s16 l = 0, r = 0;
        if (!paused && f_count > 0) {
            l = fifo[f_head].l;
            r = fifo[f_head].r;
            f_head = (f_head + 1) % FIFO_SIZE;
            f_count--;
            consumed++;
        }
        d[2 * i] = l;
        d[2 * i + 1] = r;
        viz[viz_pos] = (s16)(((int)l + (int)r) / 2);
        viz_pos = (viz_pos + 1) & (VIZ_N - 1);
    }
    DCFlushRange(d, DMA_BYTES);
}

// A appeler souvent depuis la boucle principale.
static void service_audio(void) {
    u32 level = IRQ_Disable();
    u32 blk = cb_n;
    int do_fill = (filled_blk != blk);
    IRQ_Restore(level);
    if (do_fill) {
        fill_buf((int)(blk % NUM_BUFS));
        filled_blk = blk;
    }
}

static int fifo_put(s16 l, s16 r) {
    /* Ne jamais ecraser des echantillons non lus : FIFO pleine = on attend. */
    if (f_count >= FIFO_SIZE) return 0;
    fifo[f_tail].l = l;
    fifo[f_tail].r = r;
    f_tail = (f_tail + 1) % FIFO_SIZE;
    f_count++;
    return 1;
}

// ---------- Analyseur de spectre (Goertzel sur les derniers echantillons joues) ----------
#define NBARS 55
static float bar_val[NBARS];
static float bar_coef[NBARS];
static float win[VIZ_N];

static void init_spectrum(void) {
    for (int i = 0; i < VIZ_N; i++)
        win[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i / (float)(VIZ_N - 1));
    for (int b = 0; b < NBARS; b++) {
        float f = 60.0f * powf(12000.0f / 60.0f, (float)b / (float)(NBARS - 1));
        bar_coef[b] = 2.0f * cosf(2.0f * PI_F * f / 48000.0f);
        bar_val[b] = 0.0f;
    }
}

/* Retourne 1 tant que les barres bougent encore. */
static int update_spectrum(void) {
    static float x[VIZ_N];
    int active = 0;
    for (int i = 0; i < VIZ_N; i++)
        x[i] = (float)viz[(viz_pos + i) & (VIZ_N - 1)] * win[i];
    for (int b = 0; b < NBARS; b++) {
        float c = bar_coef[b], s1 = 0.0f, s2 = 0.0f;
        for (int i = 0; i < VIZ_N; i++) {
            float s0 = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        float p = s1 * s1 + s2 * s2 - c * s1 * s2;
        if (p < 0.0f) p = 0.0f;
        float frac = sqrtf(p) / ((float)VIZ_N * 0.25f * 32768.0f);   // 1.0 = sinus pleine echelle
        float db = 20.0f * log10f(frac + 1e-6f) + 0.10f * (float)b;   // leger relevement des aigus
        float v = (db + 62.0f) / 50.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        bar_val[b] = (v > bar_val[b]) ? v : (bar_val[b] * 0.82f + v * 0.18f);
        if (bar_val[b] < 0.01f) bar_val[b] = 0.0f; else active = 1;
    }
    return active;
}

// ---------- Decodage MP3 (minimp3) ----------
static mp3dec_t dec;
static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static unsigned char *fdata = NULL;
static long fsize = 0, fpos = 0;
static int playing = 0, eof = 0, current = -1;
static const char *err_msg = NULL;   // message affiche si un fichier ne peut pas etre lu
static char artist[64];              // artiste lu dans les etiquettes ID3 (UTF-8)

// Reechantillonnage lineaire vers 48 kHz (la GameCube ne sort que 32 ou 48 kHz)
static double rpos = 0.0;
static s16 prevL = 0, prevR = 0;

static void push_frame(const s16 *L, const s16 *R, int n, unsigned sr) {
    double step = (double)sr / 48000.0;
    while (rpos < n - 1 && f_count < FIFO_SIZE) {
        int i = (rpos < 0.0) ? -1 : (int)rpos;
        double fr = rpos - i;
        s16 al = (i < 0) ? prevL : L[i];
        s16 ar = (i < 0) ? prevR : R[i];
        s16 bl = L[i + 1];
        s16 br = R[i + 1];
        if (!fifo_put((s16)(al + (bl - al) * fr), (s16)(ar + (br - ar) * fr)))
            break;
        rpos += step;
    }
    /* FIFO_MARGIN garantit qu'une trame entiere tient toujours dans la FIFO. */
    rpos -= n;
    prevL = L[n - 1];
    prevR = R[n - 1];
}

static void decode_one(void) {
    static s16 L[1152], R[1152];
    if (fpos >= fsize) { eof = 1; return; }

    mp3dec_frame_info_t info;
    int n = mp3dec_decode_frame(&dec, fdata + fpos, (int)(fsize - fpos), pcm, &info);
    if (info.frame_bytes <= 0) { eof = 1; return; }   // plus de donnees exploitables
    fpos += info.frame_bytes;
    if (n <= 0) return;                                // etiquette ID3 ou donnees ignorees
    if (n > 1152) n = 1152;
    if (info.hz <= 0) return;

    for (int i = 0; i < n; i++) {
        if (info.channels == 2) {
            L[i] = pcm[2 * i];
            R[i] = pcm[2 * i + 1];
        } else {
            L[i] = R[i] = pcm[i];
        }
    }
    push_frame(L, R, n, (unsigned)info.hz);
}

/* Ajoute un caractere (Latin-1 ou UTF-16 <= 0xFF) a la chaine, en UTF-8. */
static void utf8_put(char *out, int cap, int *len, unsigned cp) {
    if (cp < 0x80) {
        if (*len + 1 < cap) out[(*len)++] = (char)cp;
    } else if (cp <= 0xFF) {
        if (*len + 2 < cap) {
            out[(*len)++] = (char)(0xC0 | (cp >> 6));
            out[(*len)++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (*len + 1 < cap) {
        out[(*len)++] = '?';
    }
}

/* Lit l'artiste (ID3v2 TPE1/TP1, sinon ID3v1) dans le fichier deja charge en RAM. */
static void parse_artist(void) {
    int len = 0;
    artist[0] = 0;
    if (!fdata || fsize < 128) return;

    if (fsize > 10 && memcmp(fdata, "ID3", 3) == 0 && fdata[3] >= 2 && fdata[3] <= 4) {
        int ver = fdata[3];
        long end = 10 + (long)synchsafe(fdata + 6);
        long pos = 10;
        int hdr = (ver == 2) ? 6 : 10;
        if (end > fsize) end = fsize;
        if (ver >= 3 && (fdata[5] & 0x40) && pos + 4 <= end)       // en-tete etendu
            pos += (ver == 4) ? (long)synchsafe(fdata + pos) : (long)be32(fdata + pos) + 4;
        while (pos + hdr <= end) {
            const unsigned char *f = fdata + pos;
            if (f[0] == 0) break;
            long sz = (ver == 2) ? (long)(((u32)f[3] << 16) | ((u32)f[4] << 8) | f[5])
                                 : (ver == 4 ? (long)synchsafe(f + 4) : (long)be32(f + 4));
            if (sz <= 1 || pos + hdr + sz > end) break;
            int is_artist = (ver == 2) ? (memcmp(f, "TP1", 3) == 0) : (memcmp(f, "TPE1", 4) == 0);
            if (is_artist) {
                const unsigned char *t = f + hdr;
                int enc = t[0];
                long i = 1;
                if (enc == 1 || enc == 2) {                         // UTF-16
                    int le = 0;
                    if (sz >= 3 && t[1] == 0xFF && t[2] == 0xFE) { le = 1; i = 3; }
                    else if (sz >= 3 && t[1] == 0xFE && t[2] == 0xFF) { i = 3; }
                    for (; i + 1 < sz && len < 60; i += 2) {
                        unsigned cu = le ? (unsigned)(t[i] | (t[i + 1] << 8))
                                         : (unsigned)((t[i] << 8) | t[i + 1]);
                        if (cu == 0) break;
                        utf8_put(artist, (int)sizeof(artist), &len, cu);
                    }
                } else if (enc == 3) {                              // UTF-8
                    for (; i < sz && t[i] && len < 60; i++) artist[len++] = (char)t[i];
                } else {                                            // Latin-1
                    for (; i < sz && t[i] && len < 60; i++)
                        utf8_put(artist, (int)sizeof(artist), &len, t[i]);
                }
                artist[len] = 0;
                if (len > 0) return;
            }
            pos += hdr + sz;
        }
    }
    if (memcmp(fdata + fsize - 128, "TAG", 3) == 0) {              // ID3v1 : artiste = octets 33..62
        const unsigned char *a = fdata + fsize - 128 + 33;
        for (int i = 0; i < 30 && a[i] && len < 60; i++)
            utf8_put(artist, (int)sizeof(artist), &len, a[i]);
        while (len > 0 && artist[len - 1] == ' ') len--;
        artist[len] = 0;
    }
}

static void stop_track(void) {
    if (fdata) { free(fdata); fdata = NULL; }
    playing = 0;
    paused = 0;
    f_head = f_tail = f_count = 0;
}

static int start_track(int idx) {
    if (idx < 0 || idx >= nfiles) return 0;
    stop_track();
    err_msg = NULL;
    artist[0] = 0;

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", MP3_DIR, names[idx]);
    FILE *fp = fopen(path, "rb");
    if (!fp) { err_msg = "Ouverture impossible"; return 0; }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); err_msg = "Lecture impossible"; return 0; }
    long size = ftell(fp);
    if (size <= 0) { fclose(fp); err_msg = "Fichier vide"; return 0; }
    if (size > MAX_FILE_SIZE) { fclose(fp); err_msg = "Fichier trop gros (18 Mo max)"; return 0; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); err_msg = "Lecture impossible"; return 0; }
    fdata = (unsigned char *)malloc(size);
    if (!fdata) { fclose(fp); err_msg = "M\xC3\xA9moire insuffisante"; return 0; }
    if (fread(fdata, 1, size, fp) != (size_t)size) {
        fclose(fp); free(fdata); fdata = NULL;
        err_msg = "Lecture impossible";
        return 0;
    }
    fclose(fp);

    mp3dec_init(&dec);
    fsize = size;
    fpos = 0;
    f_head = f_tail = f_count = 0;
    rpos = 0.0; prevL = prevR = 0;
    consumed = 0;
    parse_artist();
    eof = 0; playing = 1; paused = 0; current = idx;
    return 1;
}

// ---------- GX : initialisation (sequence des exemples officiels libogc) ----------
static GXTexObj font_texobj;
static int gx_mode = -1;                 // 0 = couleurs (unies/degrades), 1 = texte (texture)
#define NSEG 40
static f32 seg_c[NSEG + 1], seg_s[NSEG + 1];

static void gx_set_flat(void) {
    if (gx_mode == 0) return;
    gx_mode = 0;
    GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
}

static void gx_set_text(void) {
    if (gx_mode == 1) return;
    gx_mode = 1;
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    /* couleur = couleur du sommet ; alpha = alpha de la texture x alpha du sommet */
    GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
    GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
}

static void gx_init(void) {
    Mtx44 projection;
    Mtx identity;
    GXColor background = { 7, 5, 26, 255 };

    gx_fifo = memalign(32, GX_FIFO_SIZE);
    memset(gx_fifo, 0, GX_FIFO_SIZE);
    GX_Init(gx_fifo, GX_FIFO_SIZE);

    GX_SetCopyClear(background, 0x00ffffff);
    GX_SetViewport(0.0f, 0.0f, rmode->fbWidth, rmode->efbHeight, 0.0f, 1.0f);
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfb_height = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering,
                    ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
    if (rmode->aa)
        GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    else
        GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    /* Rendu 2D : repere logique 640x480, origine en haut a gauche
       (le viewport adapte a la hauteur reelle en PAL). */
    guOrtho(projection, 0.0f, 480.0f, 0.0f, 640.0f, 0.0f, 1.0f);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    guMtxIdentity(identity);
    GX_LoadPosMtxImm(identity, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTevStages(1);
    GX_ClearVtxDesc();
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    /* Texture de police (I4 : l'alpha vaut l'intensite). */
    DCFlushRange((void *)font_tex, sizeof(font_tex));
    GX_InitTexObj(&font_texobj, (void *)font_tex, FONT_TEX_W, FONT_TEX_H,
                  GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&font_texobj, GX_LINEAR, GX_LINEAR);
    GX_InvalidateTexAll();
    GX_LoadTexObj(&font_texobj, GX_TEXMAP0);

    for (int i = 0; i <= NSEG; i++) {
        seg_c[i] = cosf(2.0f * PI_F * (float)i / (float)NSEG);
        seg_s[i] = sinf(2.0f * PI_F * (float)i / (float)NSEG);
    }
    gx_mode = -1;
    gx_set_flat();
    GX_InvVtxCache();
}

static int init_video(void) {
    VIDEO_Init();
    PAD_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    if (rmode == NULL) return 0;

    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    if (xfb[0] == NULL || xfb[1] == NULL) return 0;
    VIDEO_Configure(rmode);
    VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
    displayed_fb = 0;
    VIDEO_SetNextFramebuffer(xfb[displayed_fb]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    /* Message texte tant que GX n'a pas affiche sa premiere image :
       s'il reste a l'ecran, c'est que GX a bloque. */
    CON_Init(xfb[0], 20, 20, rmode->fbWidth - 40, rmode->xfbHeight - 40,
             rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    printf("\n\n   GC-MP3\n\n   Video OK\n   Initialisation de GX...\n");
    return 1;
}

/* Copie l'image GX vers le framebuffer inactif et l'affiche. */
static void present_frame(void) {
    int next_fb = 1 - displayed_fb;
    GX_DrawDone();
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(xfb[next_fb], GX_TRUE);
    GX_DrawDone();
    VIDEO_SetNextFramebuffer(xfb[next_fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    displayed_fb = next_fb;
}

// ---------- Primitives de dessin ----------
static col_t lerp_c(col_t a, col_t b, f32 t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return RGBA(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

static inline void vtx(f32 x, f32 y, col_t c) {
    GX_Position2f32(x, y);
    GX_Color4u8(c.r, c.g, c.b, c.a);
}

static void gx_quad4(f32 x0, f32 y0, f32 x1, f32 y1,
                     col_t tl, col_t tr, col_t br, col_t bl) {
    gx_set_flat();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vtx(x0, y0, tl); vtx(x1, y0, tr); vtx(x1, y1, br); vtx(x0, y1, bl);
    GX_End();
}
static void gx_rect(f32 x, f32 y, f32 w, f32 h, col_t c) { gx_quad4(x, y, x + w, y + h, c, c, c, c); }
static void gx_vgrad(f32 x, f32 y, f32 w, f32 h, col_t top, col_t bot) {
    gx_quad4(x, y, x + w, y + h, top, top, bot, bot);
}
static void gx_hgrad(f32 x, f32 y, f32 w, f32 h, col_t l, col_t r) {
    gx_quad4(x, y, x + w, y + h, l, r, r, l);
}

/* Quadrilatere quelconque (4 sommets dans l'ordre). */
static void gx_poly4(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy, f32 dx, f32 dy,
                     col_t ca, col_t cb, col_t cc, col_t cd) {
    gx_set_flat();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    vtx(ax, ay, ca); vtx(bx, by, cb); vtx(cx, cy, cc); vtx(dx, dy, cd);
    GX_End();
}

static void gx_tri(f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2, col_t c) {
    gx_set_flat();
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
    vtx(x0, y0, c); vtx(x1, y1, c); vtx(x2, y2, c);
    GX_End();
}

static void gx_ellipse(f32 cx, f32 cy, f32 rx, f32 ry, col_t center, col_t edge) {
    gx_set_flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, NSEG + 2);
    vtx(cx, cy, center);
    for (int i = 0; i <= NSEG; i++) vtx(cx + rx * seg_c[i], cy + ry * seg_s[i], edge);
    GX_End();
}
static void gx_circle(f32 cx, f32 cy, f32 r, col_t center, col_t edge) {
    gx_ellipse(cx, cy, r, r, center, edge);
}

/* Anneau ; avec n != NSEG, polygone creux a n cotes tourne de rot. */
static void gx_ring_n(f32 cx, f32 cy, f32 r0, f32 r1, int n, f32 rot, col_t c) {
    gx_set_flat();
    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (n + 1));
    for (int i = 0; i <= n; i++) {
        f32 co, si;
        if (n == NSEG && rot == 0.0f) { co = seg_c[i]; si = seg_s[i]; }
        else { f32 a = rot + 2.0f * PI_F * (f32)i / (f32)n; co = cosf(a); si = sinf(a); }
        vtx(cx + r1 * co, cy + r1 * si, c);
        vtx(cx + r0 * co, cy + r0 * si, c);
    }
    GX_End();
}
static void gx_ring(f32 cx, f32 cy, f32 r0, f32 r1, col_t c) { gx_ring_n(cx, cy, r0, r1, NSEG, 0.0f, c); }

/* Rectangle arrondi : 4 coins x 6 points. */
#define RR_N 24
static void rr_points(f32 x, f32 y, f32 w, f32 h, f32 r, f32 *px, f32 *py) {
    static const f32 cs[6] = { 1.0f, 0.951057f, 0.809017f, 0.587785f, 0.309017f, 0.0f };
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0.5f) r = 0.5f;
    int n = 0;
    /* haut droit, bas droit, bas gauche, haut gauche (sens horaire a l'ecran) */
    for (int s = 0; s < 6; s++) { px[n] = x + w - r + r * cs[5 - s]; py[n] = y + r - r * cs[s];     n++; }
    for (int s = 0; s < 6; s++) { px[n] = x + w - r + r * cs[s];     py[n] = y + h - r + r * cs[5 - s]; n++; }
    for (int s = 0; s < 6; s++) { px[n] = x + r - r * cs[5 - s];     py[n] = y + h - r + r * cs[s]; n++; }
    for (int s = 0; s < 6; s++) { px[n] = x + r - r * cs[s];         py[n] = y + r - r * cs[5 - s]; n++; }
}

static void gx_rrect_fill(f32 x, f32 y, f32 w, f32 h, f32 r, col_t top, col_t bot) {
    f32 px[RR_N], py[RR_N];
    rr_points(x, y, w, h, r, px, py);
    gx_set_flat();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, RR_N + 2);
    vtx(x + w / 2, y + h / 2, lerp_c(top, bot, 0.5f));
    for (int i = 0; i <= RR_N; i++) {
        int k = i % RR_N;
        vtx(px[k], py[k], lerp_c(top, bot, (py[k] - y) / h));
    }
    GX_End();
}

static void gx_rrect_stroke(f32 x, f32 y, f32 w, f32 h, f32 r, f32 t, col_t c) {
    f32 ox[RR_N], oy[RR_N], ix[RR_N], iy[RR_N];
    rr_points(x, y, w, h, r, ox, oy);
    rr_points(x + t, y + t, w - 2 * t, h - 2 * t, r - t, ix, iy);
    gx_set_flat();
    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * (RR_N + 1));
    for (int i = 0; i <= RR_N; i++) {
        int k = i % RR_N;
        vtx(ox[k], oy[k], c);
        vtx(ix[k], iy[k], c);
    }
    GX_End();
}

/* Halo lumineux autour d'un rectangle arrondi ou d'un cercle. */
static void gx_glow(f32 x, f32 y, f32 w, f32 h, f32 r, col_t c, int layers) {
    for (int k = layers; k >= 1; k--) {
        col_t cc = c;
        cc.a = (u8)((int)c.a * (layers + 1 - k) / (layers + 1));
        gx_rrect_stroke(x - k, y - k, w + 2 * k, h + 2 * k, r + k, 1.0f, cc);
    }
}
static void gx_circle_glow(f32 cx, f32 cy, f32 r, col_t c, int layers) {
    for (int k = layers; k >= 1; k--) {
        col_t cc = c;
        cc.a = (u8)((int)c.a * (layers + 1 - k) / (layers + 1));
        gx_ring(cx, cy, r + k - 1.0f, r + k, cc);
    }
}

// ---------- Texte (atlas I4, un quad texture par caractere) ----------
enum { F_SMALL = 0, F_MED = 1, F_LARGE = 2, F_LOGO = 3 };

/* Lit un caractere UTF-8 (les octets isoles sont pris comme du Latin-1). */
static int next_cp(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    int c = *s++;
    int cp = c;
    if (c >= 0x80) {
        if ((c & 0xE0) == 0xC0 && (s[0] & 0xC0) == 0x80) {
            cp = ((c & 0x1F) << 6) | (s[0] & 0x3F); s += 1;
        } else if ((c & 0xF0) == 0xE0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
            cp = '?'; s += 2;
        } else if ((c & 0xF8) == 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80 &&
                   (s[2] & 0xC0) == 0x80) {
            cp = '?'; s += 3;
        }
    }
    *ps = (const char *)s;
    return cp;
}

static int glyph_index(int cp) {
    if (cp >= 32 && cp <= 126) return cp - 32;
    if (cp >= 160 && cp <= 255) return 95 + (cp - 160);
    return '?' - 32;
}

static int text_width(int f, const char *s, int sp) {
    int w = 0, n = 0;
    while (*s) {
        int cp = next_cp(&s);
        w += font_glyphs[f][glyph_index(cp)].adv + sp;
        n++;
    }
    return n ? w - sp : 0;
}

/* Ecrit s avec sa ligne de base en y = base (degrade vertical top -> bot).
   sp = espacement supplementaire entre lettres. Retourne la largeur. */
static int gx_text(int f, f32 x, f32 base, const char *s, col_t top, col_t bot, int sp) {
    int count = 0;
    for (const char *p = s; *p;) {
        int cp = next_cp(&p);
        if (font_glyphs[f][glyph_index(cp)].w) count++;
    }
    int width = text_width(f, s, sp);
    if (count == 0) return width;

    gx_set_text();
    GX_Begin(GX_QUADS, GX_VTXFMT0, count * 4);
    f32 pen = x;
    f32 ty = base - font_ascent[f];
    f32 fh = (f32)font_height[f];
    for (const char *p = s; *p;) {
        int cp = next_cp(&p);
        const fglyph_t *g = &font_glyphs[f][glyph_index(cp)];
        if (g->w) {
            f32 x0 = pen + g->xoff, y0 = ty + g->yoff;
            f32 x1 = x0 + g->w,     y1 = y0 + g->h;
            f32 u0 = (f32)g->x / FONT_TEX_W, v0 = (f32)g->y / FONT_TEX_H;
            f32 u1 = (f32)(g->x + g->w) / FONT_TEX_W, v1 = (f32)(g->y + g->h) / FONT_TEX_H;
            col_t ct = lerp_c(top, bot, (f32)g->yoff / fh);
            col_t cb = lerp_c(top, bot, (f32)(g->yoff + g->h) / fh);
            GX_Position2f32(x0, y0); GX_Color4u8(ct.r, ct.g, ct.b, ct.a); GX_TexCoord2f32(u0, v0);
            GX_Position2f32(x1, y0); GX_Color4u8(ct.r, ct.g, ct.b, ct.a); GX_TexCoord2f32(u1, v0);
            GX_Position2f32(x1, y1); GX_Color4u8(cb.r, cb.g, cb.b, cb.a); GX_TexCoord2f32(u1, v1);
            GX_Position2f32(x0, y1); GX_Color4u8(cb.r, cb.g, cb.b, cb.a); GX_TexCoord2f32(u0, v1);
        }
        pen += g->adv + sp;
    }
    GX_End();
    return width;
}
static int gx_text1(int f, f32 x, f32 base, const char *s, col_t c) {
    return gx_text(f, x, base, s, c, c, 0);
}
static void gx_text_r(int f, f32 xr, f32 base, const char *s, col_t c) {
    gx_text(f, xr - text_width(f, s, 0), base, s, c, c, 0);
}
static void gx_text_c(int f, f32 xc, f32 base, const char *s, col_t c) {
    gx_text(f, xc - text_width(f, s, 0) / 2, base, s, c, c, 0);
}

/* Copie s dans out, tronque avec "..." pour tenir dans maxw pixels. */
static void fit_text(int f, const char *s, int maxw, char *out, int outsz) {
    if (text_width(f, s, 0) <= maxw) { snprintf(out, outsz, "%s", s); return; }
    int dots = text_width(f, "...", 0);
    int w = 0, len = 0;
    const char *p = s;
    while (*p) {
        const char *q = p;
        int cp = next_cp(&q);
        w += font_glyphs[f][glyph_index(cp)].adv;
        if (w + dots > maxw || len + (int)(q - p) + 4 >= outsz) break;
        memcpy(out + len, p, q - p);
        len += (int)(q - p);
        p = q;
    }
    memcpy(out + len, "...", 4);
}

static void fmt_time(char *buf, int sec) {
    if (sec < 0) snprintf(buf, 16, "--:--");
    else snprintf(buf, 16, "%d:%02d", sec / 60, sec % 60);
}

// ---------- Interface "Indigo" ----------
static const col_t C_PANEL_TOP = { 22, 12, 66, 228 };
static const col_t C_PANEL_BOT = { 10, 6, 34, 238 };
static const col_t C_BORDER    = { 112, 62, 235, 255 };
static const col_t C_GLOW      = { 120, 60, 255, 120 };

static void draw_background(void) {
    gx_quad4(0, 0, 640, 480, RGBA(7, 5, 26, 255), RGBA(13, 8, 44, 255),
             RGBA(9, 6, 34, 255), RGBA(18, 8, 60, 255));
    /* halos violets diffus */
    gx_circle(30, 470, 260, RGBA(70, 30, 170, 70), RGBA(70, 30, 170, 0));
    gx_circle(610, 40, 230, RGBA(60, 40, 190, 60), RGBA(60, 40, 190, 0));
    /* rubans lumineux en bas a gauche */
    gx_poly4(0, 420, 120, 380, 120, 386, 0, 430, RGBA(110, 60, 230, 0), RGBA(110, 60, 230, 50),
             RGBA(110, 60, 230, 50), RGBA(110, 60, 230, 0));
    gx_poly4(0, 452, 90, 430, 90, 434, 0, 458, RGBA(110, 60, 230, 0), RGBA(110, 60, 230, 40),
             RGBA(110, 60, 230, 40), RGBA(110, 60, 230, 0));
}

static void draw_logo_cube(f32 cx, f32 cy, f32 R) {
    gx_ring_n(cx, cy, R - 3.5f, R, 6, -PI_F / 2.0f, RGBA(150, 110, 255, 255));
    f32 r = R * 0.62f, k = 0.866f;
    f32 P0x = cx,         P0y = cy - r;
    f32 P1x = cx + k * r, P1y = cy - r / 2;
    f32 P2x = cx + k * r, P2y = cy + r / 2;
    f32 P3x = cx,         P3y = cy + r;
    f32 P4x = cx - k * r, P4y = cy + r / 2;
    f32 P5x = cx - k * r, P5y = cy - r / 2;
    gx_poly4(P0x, P0y, P1x, P1y, cx, cy, P5x, P5y,
             RGBA(205, 180, 255, 255), RGBA(170, 135, 255, 255), RGBA(150, 110, 250, 255), RGBA(170, 135, 255, 255));
    gx_poly4(P5x, P5y, cx, cy, P3x, P3y, P4x, P4y,
             RGBA(130, 80, 245, 255), RGBA(120, 70, 240, 255), RGBA(96, 50, 210, 255), RGBA(104, 56, 220, 255));
    gx_poly4(cx, cy, P1x, P1y, P2x, P2y, P3x, P3y,
             RGBA(86, 40, 190, 255), RGBA(80, 36, 180, 255), RGBA(60, 26, 150, 255), RGBA(66, 28, 160, 255));
}

static void draw_console_corner(void) {
    /* coin de console GameCube stylise (haut droite) */
    gx_poly4(470, 10, 556, 0, 640, 12, 566, 64, RGBA(74, 50, 196, 255), RGBA(60, 40, 176, 255),
             RGBA(36, 24, 110, 255), RGBA(46, 30, 140, 255));
    gx_poly4(470, 10, 566, 64, 566, 96, 472, 36, RGBA(36, 22, 116, 255), RGBA(28, 16, 92, 255),
             RGBA(16, 10, 56, 255), RGBA(22, 14, 74, 255));
    gx_poly4(566, 64, 640, 12, 640, 46, 566, 96, RGBA(30, 18, 98, 255), RGBA(26, 16, 86, 255),
             RGBA(14, 8, 50, 255), RGBA(16, 10, 56, 255));
    gx_ellipse(552, 26, 38, 15, RGBA(12, 8, 38, 255), RGBA(22, 14, 64, 255));
    gx_ring(564, 52, 4, 6.5f, RGBA(210, 190, 255, 255));
}

static void draw_header(void) {
    draw_logo_cube(54, 38, 23);
    gx_text(F_LOGO, 88, 46, "GC-MP3", RGBA(240, 232, 255, 255), RGBA(150, 84, 255, 255), 0);
    gx_text(F_SMALL, 91, 63, "INDIGO AUDIO PLAYER", RGBA(170, 150, 225, 255), RGBA(170, 150, 225, 255), 4);

    /* ligne + onde decorative (reagit au spectre) */
    gx_hgrad(300, 46, 170, 1, RGBA(110, 70, 255, 0), RGBA(110, 70, 255, 200));
    for (int i = 0; i < 28; i++) {
        f32 env = (i < 20) ? 0.15f + 0.85f * (f32)i / 19.0f : 1.0f - (f32)(i - 20) / 10.0f;
        f32 v = bar_val[(i * 2) % NBARS];
        f32 h = 2.0f + 22.0f * env * (0.30f + 0.70f * v);
        gx_rect(392.0f + i * 3.0f, 46.0f - h, 1.5f, 2.0f * h,
                RGBA(150 + i * 3, 90, 255, 90 + (int)(120 * env)));
    }
    draw_console_corner();
}

static void draw_note(f32 cx, f32 cy, col_t c) {
    gx_ellipse(cx - 2.0f, cy + 5.0f, 3.4f, 2.6f, c, c);
    gx_rect(cx + 0.6f, cy - 6.0f, 1.6f, 11.0f, c);
    gx_tri(cx + 0.6f, cy - 6.0f, cx + 5.5f, cy - 3.0f, cx + 0.6f, cy - 1.0f, c);
}

static void draw_panel(f32 x, f32 y, f32 w, f32 h) {
    gx_glow(x, y, w, h, 10, C_GLOW, 4);
    gx_rrect_fill(x, y, w, h, 10, C_PANEL_TOP, C_PANEL_BOT);
    gx_rrect_stroke(x, y, w, h, 10, 1.5f, C_BORDER);
}

static void draw_list_panel(int sel) {
    int start = sel - LIST_ROWS / 2;
    if (start > nfiles - LIST_ROWS) start = nfiles - LIST_ROWS;
    if (start < 0) start = 0;
    char buf[160], t[16];

    draw_panel(11, 73, 271, 385);

    /* en-tete : icone fichier, dossier, nombre de pistes */
    gx_rrect_stroke(30, 87, 11, 15, 2, 1.2f, RGBA(205, 195, 245, 255));
    gx_rect(33, 93, 5, 1, RGBA(205, 195, 245, 255));
    gx_rect(33, 96, 5, 1, RGBA(205, 195, 245, 255));
    gx_text1(F_MED, 50, 100, "SD:/MP3", RGBA(240, 235, 255, 255));
    snprintf(buf, sizeof(buf), "%d TRACKS", nfiles);
    gx_text_r(F_SMALL, 262, 99, buf, RGBA(205, 195, 245, 255));
    gx_rect(25, 110, 241, 1, RGBA(76, 52, 150, 200));

    for (int i = 0; i < LIST_ROWS; i++) {
        int k = start + i;
        if (k >= nfiles) break;
        f32 ry = 114.0f + i * 24.0f;
        f32 cy = ry + 12.0f;
        int is_sel = (k == sel);
        int is_cur = (playing && k == current);

        if (is_sel) {
            gx_glow(20, ry + 1, 244, 22, 5, RGBA(150, 80, 255, 120), 3);
            gx_rrect_fill(20, ry + 1, 244, 22, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
            gx_rrect_stroke(20, ry + 1, 244, 22, 5, 1.2f, RGBA(180, 104, 255, 255));
        } else if (i + 1 < LIST_ROWS && k + 1 < nfiles && k + 1 != sel) {
            gx_rect(25, ry + 24, 241, 1, RGBA(60, 40, 120, 110));
        }
        col_t dim = is_sel ? RGBA(235, 225, 255, 255) : RGBA(128, 112, 190, 255);
        col_t txt = is_sel ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);

        if (is_cur) gx_tri(31, cy - 6, 31, cy + 6, 41, cy, RGBA(255, 255, 255, 255));
        else draw_note(36, cy, is_sel ? RGBA(235, 225, 255, 255) : RGBA(150, 120, 235, 255));

        snprintf(t, sizeof(t), "%02d", k + 1);
        gx_text1(F_SMALL, 50, cy + 4, t, dim);

        fmt_time(t, track_duration(k));
        f32 dr = is_cur ? 240.0f : 256.0f;
        int name_w = (int)dr - text_width(F_SMALL, t, 0) - 8 - 76;   /* place libre pour le nom */
        gx_text_r(F_SMALL, dr, cy + 4, t, is_sel ? RGBA(235, 225, 255, 255) : RGBA(150, 135, 210, 255));
        if (is_cur) {
            for (int j = 0; j < 3; j++) {
                f32 bh = 3.0f + (paused ? 2.0f : 8.0f * bar_val[3 + j * 9]);
                gx_rect(246.0f + j * 4.0f, cy + 5.0f - bh, 2.5f, bh, RGBA(255, 255, 255, 255));
            }
        }
        char nm[NAME_LEN];
        fit_text(F_MED, names[k], name_w, nm, sizeof(nm));
        gx_text1(F_MED, 76, cy + 5, nm, txt);
    }

    if (nfiles == 0) {
        gx_text1(F_MED, 30, 138, "Aucun fichier MP3 trouv\xC3\xA9", RGBA(255, 205, 110, 255));
        gx_text1(F_SMALL, 30, 160, "Copie tes MP3 dans le dossier SD:/mp3", RGBA(190, 180, 235, 255));
    }

    /* ascenseur */
    if (nfiles > LIST_ROWS) {
        f32 track_h = 330.0f;
        f32 th = track_h * LIST_ROWS / nfiles;
        if (th < 18.0f) th = 18.0f;
        f32 ty = 116.0f + (track_h - th) * (f32)start / (f32)(nfiles - LIST_ROWS);
        gx_rrect_fill(270, 116, 3, track_h, 1.5f, RGBA(60, 40, 130, 130), RGBA(60, 40, 130, 130));
        gx_rrect_fill(270, ty, 3, th, 1.5f, RGBA(150, 96, 255, 255), RGBA(110, 60, 230, 255));
    }
}

static void draw_art(void) {
    /* disque vinyle qui depasse derriere la pochette */
    const f32 vx = 381, vy = 172;
    gx_circle(vx, vy, 62, RGBA(28, 24, 48, 255), RGBA(8, 6, 18, 255));
    gx_ring(vx, vy, 52, 53, RGBA(70, 60, 120, 110));
    gx_ring(vx, vy, 44, 45, RGBA(70, 60, 120, 90));
    gx_ring(vx, vy, 36, 37, RGBA(70, 60, 120, 80));
    gx_ring(vx, vy, 60.5f, 62, RGBA(120, 70, 255, 170));

    /* pochette : lune sur un lac */
    const f32 x = 305, y = 108, w = 104, h = 128, hz = 188;
    gx_vgrad(x, y, w, hz - y, RGBA(8, 6, 34, 255), RGBA(74, 32, 156, 255));
    static const u8 stars[12][2] = {{12,10},{30,22},{52,8},{74,18},{92,12},{20,40},
                                    {88,36},{64,30},{40,48},{98,52},{8,60},{58,56}};
    for (int i = 0; i < 12; i++)
        gx_rect(x + stars[i][0], y + stars[i][1], 1.3f, 1.3f, RGBA(225, 215, 255, 170));
    gx_circle(357, 164, 36, RGBA(150, 100, 255, 80), RGBA(150, 100, 255, 0));
    gx_circle(357, 164, 21, RGBA(240, 232, 255, 255), RGBA(150, 96, 235, 255));
    gx_ring(357, 164, 20, 21.5f, RGBA(200, 170, 255, 255));
    gx_tri(x, hz + 1, x + 24, hz - 20, x + 52, hz + 1, RGBA(22, 12, 60, 255));
    gx_tri(x + 30, hz + 1, x + 66, hz - 13, x + w, hz + 1, RGBA(16, 8, 46, 255));
    gx_tri(x + 62, hz + 1, x + 88, hz - 22, x + w, hz - 6, RGBA(26, 14, 70, 255));
    gx_vgrad(x, hz, w, y + h - hz, RGBA(46, 20, 112, 255), RGBA(10, 6, 34, 255));
    gx_ellipse(357, 206, 22, 14, RGBA(190, 140, 255, 130), RGBA(120, 70, 220, 0));
    for (int i = 0; i < 6; i++)
        gx_rect(x + 18 + i * 4, hz + 6 + i * 6, 68 - i * 8, 1, RGBA(205, 175, 255, 70));
    gx_rrect_stroke(x, y, w, h, 2, 1.2f, RGBA(124, 80, 240, 235));
}

static void draw_round_button(f32 cx, f32 cy, f32 r) {
    gx_circle(cx, cy, r, RGBA(36, 22, 86, 235), RGBA(16, 10, 44, 240));
    gx_circle_glow(cx, cy, r, RGBA(130, 70, 255, 110), 3);
    gx_ring(cx, cy, r - 1.5f, r, C_BORDER);
}

static void draw_player_panel(void) {
    char buf[160], t1[16], t2[16];
    draw_panel(289, 73, 341, 338);

    gx_text(F_SMALL, 308, 98, "NOW PLAYING", RGBA(140, 122, 210, 255), RGBA(140, 122, 210, 255), 3);
    snprintf(buf, sizeof(buf), "%02d / %02d", (playing && current >= 0) ? current + 1 : 0, nfiles);
    gx_text_r(F_MED, 614, 99, buf, RGBA(165, 145, 235, 255));

    draw_art();

    int dur = playing ? track_duration(current) : -1;
    int elapsed = (int)(consumed / 48000u);
    if (playing && current >= 0 && current < nfiles) {
        fit_text(F_LARGE, names[current], 160, buf, sizeof(buf));
        gx_text1(F_LARGE, 456, 140, buf, RGBA(255, 255, 255, 255));
        if (err_msg) {
            gx_text1(F_MED, 456, 160, err_msg, RGBA(255, 150, 80, 255));
        } else if (artist[0]) {
            fit_text(F_MED, artist, 160, buf, sizeof(buf));
            gx_text1(F_MED, 456, 160, buf, RGBA(156, 116, 255, 255));
        }
    } else {
        gx_text1(F_LARGE, 456, 140, "Aucune piste", RGBA(228, 218, 255, 255));
        if (err_msg) {
            fit_text(F_MED, err_msg, 160, buf, sizeof(buf));
            gx_text1(F_MED, 456, 160, buf, RGBA(255, 150, 80, 255));
        } else {
            gx_text1(F_MED, 456, 160, "Appuie sur A", RGBA(156, 116, 255, 255));
        }
    }

    fmt_time(t1, playing ? elapsed : 0);
    fmt_time(t2, dur);
    snprintf(buf, sizeof(buf), "%s / %s", t1, t2);
    gx_text1(F_MED, 456, 196, buf, RGBA(180, 160, 240, 255));

    f32 frac = (playing && dur > 0) ? (f32)consumed / (48000.0f * (f32)dur) : 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    const f32 px = 457, py = 207, pw = 156;
    gx_rrect_fill(px, py, pw, 3, 1.5f, RGBA(70, 50, 140, 230), RGBA(70, 50, 140, 230));
    if (playing) {
        if (frac > 0.0f) gx_hgrad(px, py, pw * frac, 3, RGBA(120, 64, 255, 255), RGBA(180, 100, 255, 255));
        f32 kx = px + pw * frac;
        gx_circle(kx, py + 1.5f, 9, RGBA(150, 90, 255, 100), RGBA(150, 90, 255, 0));
        gx_circle(kx, py + 1.5f, 5.5f, RGBA(240, 232, 255, 255), RGBA(240, 232, 255, 255));
        gx_circle(kx, py + 1.5f, 3, RGBA(124, 66, 255, 255), RGBA(124, 66, 255, 255));
    }

    /* egaliseur (spectre reel de ce qui est joue) + reflet */
    const f32 base = 295.0f;
    for (int i = 0; i < NBARS; i++) {
        f32 bx = (f32)(306 + (int)(i * 5.6f));
        f32 tcol = (f32)i / (f32)(NBARS - 1);
        col_t cb = lerp_c(RGBA(156, 60, 255, 255), RGBA(70, 130, 255, 255), tcol * 1.25f - 0.15f);
        int nb = 1 + (int)(bar_val[i] * 16.0f);
        for (int j = 0; j < nb; j++) {
            col_t cj = lerp_c(cb, RGBA(255, 196, 255, 255), (f32)j / 18.0f);
            gx_rect(bx, base - 3.0f * (j + 1), 4, 2, cj);
        }
        for (int j = 0; j < nb && j < 7; j++)
            gx_rect(bx, base + 2.0f + 3.0f * j, 4, 2, RGBA(cb.r, cb.g, cb.b, 80 - j * 11));
    }
    gx_hgrad(306, base, 308, 1, RGBA(130, 80, 255, 70), RGBA(80, 140, 255, 70));

    /* boutons de transport */
    const f32 cy = 364.0f;
    col_t icon = RGBA(230, 220, 255, 255);
    draw_round_button(347, cy, 21);
    draw_round_button(499, cy, 21);
    draw_round_button(567, cy, 21);
    gx_tri(337, cy, 346, cy - 7, 346, cy + 7, icon);
    gx_tri(346, cy, 355, cy - 7, 355, cy + 7, icon);
    gx_tri(509, cy, 500, cy - 7, 500, cy + 7, icon);
    gx_tri(500, cy, 491, cy - 7, 491, cy + 7, icon);
    gx_rrect_fill(560, cy - 7, 14, 14, 2.5f, icon, icon);

    gx_circle_glow(423, cy, 29, RGBA(170, 90, 255, 150), 5);
    gx_circle(423, cy, 29, RGBA(150, 78, 255, 255), RGBA(92, 36, 206, 255));
    gx_ring(423, cy, 27.5f, 29, RGBA(210, 140, 255, 255));
    if (playing && !paused) {
        gx_rrect_fill(414, cy - 10, 6, 20, 2, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
        gx_rrect_fill(426, cy - 10, 6, 20, 2, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
    } else {
        gx_tri(417, cy - 11, 417, cy + 11, 434, cy, RGBA(255, 255, 255, 255));
    }
}

static void draw_badge(f32 cx, f32 cy, const char *letter, col_t c1, col_t c2) {
    gx_circle(cx, cy, 8, c1, c2);
    gx_ring(cx, cy, 7, 8, RGBA(255, 255, 255, 80));
    gx_text_c(F_SMALL, cx, cy + 4.5f, letter, RGBA(255, 255, 255, 255));
}

/* Legende des touches : icones + textes repartis sur toute la largeur du cadre. */
static void draw_legend(void) {
    static const char *labels[5] = { "PARCOURIR", "LIRE", "PAUSE", "STOP", "QUITTER" };
    static const f32 icon_w[5] = { 15, 16, 16, 16, 20 };
    const f32 x0 = 289, w = 341, pad = 10, gap = 4, cy = 438.5f;
    col_t tx = RGBA(208, 198, 240, 255);
    col_t dpad = RGBA(175, 165, 232, 255);

    gx_rrect_fill(x0, 419, w, 39, 9, C_PANEL_TOP, C_PANEL_BOT);
    gx_rrect_stroke(x0, 419, w, 39, 9, 1.2f, RGBA(100, 56, 220, 255));

    f32 total = 0;
    for (int i = 0; i < 5; i++) total += icon_w[i] + gap + text_width(F_SMALL, labels[i], 0);
    f32 space = (w - 2 * pad - total) / 4.0f;
    f32 x = x0 + pad;
    for (int i = 0; i < 5; i++) {
        f32 cx = x + icon_w[i] / 2;
        switch (i) {
        case 0:   /* croix directionnelle */
            gx_rrect_fill(cx - 3.0f, cy - 7.5f, 6, 15, 2, dpad, dpad);
            gx_rrect_fill(cx - 7.5f, cy - 3.0f, 15, 6, 2, dpad, dpad);
            gx_rect(cx - 1.5f, cy - 1.5f, 3, 3, RGBA(40, 26, 100, 255));
            break;
        case 1: draw_badge(cx, cy, "A", RGBA(80, 210, 100, 255), RGBA(30, 140, 60, 255)); break;
        case 2: draw_badge(cx, cy, "Y", RGBA(140, 90, 255, 255), RGBA(84, 40, 204, 255)); break;
        case 3: draw_badge(cx, cy, "B", RGBA(236, 70, 90, 255), RGBA(170, 30, 50, 255)); break;
        default:  /* bouton START (pastille grise) */
            gx_rrect_fill(x, cy - 6.5f, 20, 13, 6.5f, RGBA(140, 140, 165, 255), RGBA(84, 84, 110, 255));
            gx_text_c(F_SMALL, cx, cy + 4.0f, "ST", RGBA(255, 255, 255, 255));
            break;
        }
        x += icon_w[i] + gap;
        x += gx_text1(F_SMALL, x, cy + 4.5f, labels[i], tx) + space;
    }
}

static void begin_frame(void) {
    GX_SetViewport(0.0f, 0.0f, rmode->fbWidth, rmode->efbHeight, 0.0f, 1.0f);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    draw_background();
}

static void draw(int sel) {
    dur_budget = 1;
    begin_frame();
    draw_header();
    draw_list_panel(sel);
    draw_player_panel();
    draw_legend();
    service_audio();
    present_frame();
}

/* Ecran de message (demarrage, erreur de carte SD). */
static void draw_message(const char *l1, const char *l2, const char *l3) {
    begin_frame();
    draw_header();
    draw_panel(60, 150, 520, 170);
    gx_text_c(F_LARGE, 320, 207, l1, RGBA(255, 255, 255, 255));
    if (l2) gx_text_c(F_MED, 320, 245, l2, RGBA(195, 180, 242, 255));
    if (l3) gx_text_c(F_MED, 320, 283, l3, RGBA(156, 116, 255, 255));
    present_frame();
}

// ---------- Programme ----------
int main(void) {
    if (!init_video()) return 1;
    gx_init();
    init_spectrum();

    draw_message("Montage de la carte SD...", NULL, NULL);
    if (!mount_sd()) {
        draw_message("Carte SD introuvable",
                     "V\xC3\xA9rifie la SD2SP2 ou la SD Gecko", "START pour quitter");
        while (1) {
            PAD_ScanPads();
            if (PAD_ButtonsDown(0) & PAD_BUTTON_START) return 0;
            VIDEO_WaitVSync();
        }
    }

    scan_files();

    AUDIO_Init(NULL);
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
    AUDIO_RegisterDMACallback(dma_cb);
    memset(dmabuf, 0, sizeof(dmabuf));
    DCFlushRange(dmabuf, sizeof(dmabuf));
    dma_cb();
    AUDIO_StartDMA();

    int sel = 0;
    int redraw = 1;
    int spec_active = 0;
    u32 rep_mask = 0;
    u64 rep_next = 0;

    while (1) {
        PAD_ScanPads();
        u32 down = PAD_ButtonsDown(0);
        u32 held = PAD_ButtonsHeld(0);
        u64 now = ticks_to_millisecs(gettime());

        if (down & PAD_BUTTON_START) break;

        /* navigation, avec repetition quand on maintient la croix */
        u32 nav = held & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
        int fire = 0;
        if (down & nav) { fire = 1; rep_mask = nav; rep_next = now + 400; }
        else if (nav && nav == rep_mask && now >= rep_next) { fire = 1; rep_next = now + 70; }
        else if (!nav) rep_mask = 0;
        if (fire && nfiles > 0) {
            if (nav & PAD_BUTTON_DOWN)  sel++;
            if (nav & PAD_BUTTON_UP)    sel--;
            if (nav & PAD_BUTTON_RIGHT) sel += LIST_ROWS;
            if (nav & PAD_BUTTON_LEFT)  sel -= LIST_ROWS;
            if (sel < 0) sel = 0;
            if (sel > nfiles - 1) sel = nfiles - 1;
            redraw = 1;
        }
        if ((down & PAD_BUTTON_A) && nfiles > 0) {
            start_track(sel);   // quelques secondes de chargement : l'audio joue du silence
            redraw = 1;
        }
        if (down & PAD_BUTTON_B) {
            stop_track();
            err_msg = NULL;
            redraw = 1;
        }
        if ((down & PAD_BUTTON_Y) && playing) {
            paused = !paused;
            redraw = 1;
        }
        if ((down & PAD_TRIGGER_R) && playing && current + 1 < nfiles) {
            sel = current + 1;
            start_track(sel);
            redraw = 1;
        }
        if ((down & PAD_TRIGGER_L) && playing && current > 0) {
            sel = current - 1;
            start_track(sel);
            redraw = 1;
        }

        service_audio();

        if (playing && !paused) {
            int k = 0;
            while (!eof && k < 2 && (FIFO_SIZE - f_count) >= FIFO_MARGIN) {
                decode_one();
                service_audio();
                k++;
            }
            if (eof && f_count == 0) {
                int nx = current + 1;
                if (nx < nfiles) {
                    sel = nx;
                    start_track(nx);
                } else {
                    stop_track();
                }
                redraw = 1;
            }
        }

        service_audio();

        /* Durees pas encore lues : on continue a redessiner jusqu'a les avoir. */
        if (!redraw) {
            int start = sel - LIST_ROWS / 2;
            if (start > nfiles - LIST_ROWS) start = nfiles - LIST_ROWS;
            if (start < 0) start = 0;
            for (int i = start; i < start + LIST_ROWS && i < nfiles; i++)
                if (dur_cache[i] == -2) { redraw = 1; break; }
        }

        if (playing || spec_active || redraw) {
            spec_active = update_spectrum();
            draw(sel);
            redraw = 0;
        } else {
            VIDEO_WaitVSync();   /* a l'arret : inutile de tourner a vide */
        }
    }

    AUDIO_RegisterDMACallback(NULL);
    stop_track();
    AUDIO_StopDMA();
    return 0;
}

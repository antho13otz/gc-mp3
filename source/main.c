// GC MP3 Player - lecteur MP3 simple pour GameCube (libogc + minimp3)
// Lit les fichiers .mp3 du dossier /mp3 de la carte SD.
// Commandes : Haut/Bas = choisir, A = lire, B = stop, Y = pause, START = quitter
#define VIDEO_WaitForRetrace VIDEO_WaitVSync
#include <gccore.h>
#include <fat.h>
#include <sdcard/gcsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

// Decodeur MP3 : minimp3 (fichier unique, domaine public), telecharge par le workflow
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

#define MP3_DIR        "sd:/mp3"
#define MAX_FILES      512
#define NAME_LEN       128
#define MAX_FILE_SIZE  (18 * 1024 * 1024)   // un MP3 est chargé entièrement en RAM
#define DMA_SAMPLES    2048                 // échantillons stéréo par tampon (~43 ms)
#define NUM_BUFS       3                    // 3 tampons : sûr quelle que soit la façon dont le matériel enchaîne les blocs
#define DMA_BYTES      (DMA_SAMPLES * 4)
#define FIFO_SIZE      16384
#define FIFO_MARGIN    7200                 // place libre mini avant de décoder une trame
#define GX_FIFO_SIZE   (256 * 1024)         // FIFO GX alignée, taille multiple de 32 octets

typedef struct { s16 l, r; } stereo_t;

static void *xfb[2] = { NULL, NULL };
static void *gx_fifo = NULL;
static GXRModeObj *rmode = NULL;
static u32 xfb_height = 0;
static int displayed_fb = 0;

// ---------- Liste des fichiers ----------
static char names[MAX_FILES][NAME_LEN];
static int nfiles = 0;

static int cmp_names(const void *a, const void *b) {
    return strcasecmp((const char *)a, (const char *)b);
}

static void scan_files(void) {
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
    if (fatMountSimple("sd", &__io_gcsd2)) return 1;   // SD2SP2 (port série 2)
    if (fatMountSimple("sd", &__io_gcsda)) return 1;   // SD Gecko slot A
    if (fatMountSimple("sd", &__io_gcsdb)) return 1;   // SD Gecko slot B
    return 0;
}

// ---------- Audio (DMA double tampon, 48 kHz) ----------
static s16 dmabuf[NUM_BUFS][DMA_SAMPLES * 2] ATTRIBUTE_ALIGN(32);
static volatile u32 cb_n = 0;        // nombre de blocs programmés
static volatile int need_fill = 0;

static stereo_t fifo[FIFO_SIZE];
static int f_head = 0, f_tail = 0, f_count = 0;

// Appelée à chaque fin de bloc : programme le bloc suivant dans l'anneau de 3 tampons.
// Le tampon à remplir ensuite est dmabuf[cb_n % NUM_BUFS] : il n'est ni en cours de
// lecture, ni déjà programmé, que le matériel applique le nouveau bloc tout de suite
// ou à la fin du bloc en cours.
static void dma_cb(void) {
    AUDIO_InitDMA((u32)dmabuf[cb_n % NUM_BUFS], DMA_BYTES);
    cb_n++;
    need_fill = 1;
}

static int paused = 0;

static void fill_buf(int idx) {
    s16 *d = dmabuf[idx];
    for (int i = 0; i < DMA_SAMPLES; i++) {
        if (!paused && f_count > 0) {
            d[2 * i]     = fifo[f_head].l;
            d[2 * i + 1] = fifo[f_head].r;
            f_head = (f_head + 1) % FIFO_SIZE;
            f_count--;
        } else {
            d[2 * i] = 0;
            d[2 * i + 1] = 0;
        }
    }
    DCFlushRange(d, DMA_BYTES);
}

static void service_audio(void) {
    if (need_fill) {
        need_fill = 0;
        fill_buf((int)(cb_n % NUM_BUFS));
    }
}

static void fifo_put(s16 l, s16 r) {
    fifo[f_tail].l = l;
    fifo[f_tail].r = r;
    f_tail = (f_tail + 1) % FIFO_SIZE;
    f_count++;
}

// ---------- Décodage MP3 (minimp3) ----------
static mp3dec_t dec;
static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static unsigned char *fdata = NULL;
static long fsize = 0, fpos = 0;
static int playing = 0, eof = 0, current = -1;

// Rééchantillonnage linéaire vers 48 kHz (la GameCube ne sort que 32 ou 48 kHz)
static double rpos = 0.0;
static s16 prevL = 0, prevR = 0;

static void push_frame(const s16 *L, const s16 *R, int n, unsigned sr) {
    double step = (double)sr / 48000.0;
    while (rpos < n - 1) {
        int i = (rpos < 0.0) ? -1 : (int)rpos;
        double fr = rpos - i;
        s16 al = (i < 0) ? prevL : L[i];
        s16 ar = (i < 0) ? prevR : R[i];
        s16 bl = L[i + 1];
        s16 br = R[i + 1];
        fifo_put((s16)(al + (bl - al) * fr), (s16)(ar + (br - ar) * fr));
        rpos += step;
    }
    rpos -= n;
    prevL = L[n - 1];
    prevR = R[n - 1];
}

static void decode_one(void) {
    static s16 L[1152], R[1152];
    if (fpos >= fsize) { eof = 1; return; }

    mp3dec_frame_info_t info;
    int n = mp3dec_decode_frame(&dec, fdata + fpos, (int)(fsize - fpos), pcm, &info);
    if (info.frame_bytes <= 0) { eof = 1; return; }   // plus de données exploitables
    fpos += info.frame_bytes;
    if (n <= 0) return;                                // étiquette ID3 ou données ignorées
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

static void stop_track(void) {
    if (fdata) { free(fdata); fdata = NULL; }
    playing = 0;
    paused = 0;
    f_head = f_tail = f_count = 0;
}

static int start_track(int idx) {
    stop_track();
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", MP3_DIR, names[idx]);
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > MAX_FILE_SIZE) { fclose(fp); return 0; }
    fdata = (unsigned char *)malloc(size);
    if (!fdata) { fclose(fp); return 0; }
    if (fread(fdata, 1, size, fp) != (size_t)size) {
        fclose(fp); free(fdata); fdata = NULL; return 0;
    }
    fclose(fp);

    mp3dec_init(&dec);
    fsize = size;
    fpos = 0;
    f_head = f_tail = f_count = 0;
    rpos = 0.0; prevL = prevR = 0;
    eof = 0; playing = 1; paused = 0; current = idx;
    return 1;
}

// ---------- Interface graphique GX (sans console texte) ----------
#define UI_W 640
#define UI_H 480

static f32 sx, sy;

static void ui_rect(f32 x, f32 y, f32 w, f32 h,
                    u8 r, u8 g, u8 b, u8 a) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);         GX_Color4u8(r, g, b, a);
    GX_Position2f32(x+w, y);       GX_Color4u8(r, g, b, a);
    GX_Position2f32(x+w, y+h);     GX_Color4u8(r, g, b, a);
    GX_Position2f32(x, y+h);       GX_Color4u8(r, g, b, a);
    GX_End();
}

/* Compact 5x7 font. Lowercase is displayed as uppercase for a clean console UI. */
static const u8 font5x7[][5] = {
    {0,0,0,0,0},                 // space
    {0,0,0x5f,0,0},              // !
    {0,7,0,7,0},                 // "
    {0x14,0x7f,0x14,0x7f,0x14},  // #
    {0x24,0x2a,0x7f,0x2a,0x12},  // $
    {0x23,0x13,8,0x64,0x62},     // %
    {0x36,0x49,0x55,0x22,0x50},  // &
    {0,5,3,0,0},                 // '
    {0,0x1c,0x22,0x41,0},       // (
    {0,0x41,0x22,0x1c,0},       // )
    {0x14,8,0x3e,8,0x14},       // *
    {8,8,0x3e,8,8},              // +
    {0,0x50,0x30,0,0},           // ,
    {8,8,8,8,8},                 // -
    {0,0x60,0x60,0,0},           // .
    {0x20,0x10,8,4,2},           // /
    {0x3e,0x51,0x49,0x45,0x3e}, // 0
    {0,0x42,0x7f,0x40,0},        // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4b,0x31}, // 3
    {0x18,0x14,0x12,0x7f,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3c,0x4a,0x49,0x49,0x30}, // 6
    {1,0x71,9,5,3},              // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {6,0x49,0x49,0x29,0x1e},    // 9
    {0,0x36,0x36,0,0},           // :
    {0,0x56,0x36,0,0},           // ;
    {8,0x14,0x22,0x41,0},       // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0,0x41,0x22,0x14,8},       // >
    {2,1,0x51,9,6},              // ?
    {0x32,0x49,0x79,0x41,0x3e}, // @
    {0x7e,0x11,0x11,0x11,0x7e}, // A
    {0x7f,0x49,0x49,0x49,0x36}, // B
    {0x3e,0x41,0x41,0x41,0x22}, // C
    {0x7f,0x41,0x41,0x22,0x1c}, // D
    {0x7f,0x49,0x49,0x49,0x41}, // E
    {0x7f,9,9,9,1},              // F
    {0x3e,0x41,0x49,0x49,0x7a}, // G
    {0x7f,8,8,8,0x7f},           // H
    {0,0x41,0x7f,0x41,0},        // I
    {0x20,0x40,0x41,0x3f,1},     // J
    {0x7f,8,0x14,0x22,0x41},    // K
    {0x7f,0x40,0x40,0x40,0x40}, // L
    {0x7f,2,0x0c,2,0x7f},       // M
    {0x7f,4,8,0x10,0x7f},       // N
    {0x3e,0x41,0x41,0x41,0x3e}, // O
    {0x7f,9,9,9,6},              // P
    {0x3e,0x41,0x51,0x21,0x5e}, // Q
    {0x7f,9,0x19,0x29,0x46},    // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {1,1,0x7f,1,1},              // T
    {0x3f,0x40,0x40,0x40,0x3f}, // U
    {0x1f,0x20,0x40,0x20,0x1f},// V
    {0x3f,0x40,0x38,0x40,0x3f},// W
    {0x63,0x14,8,0x14,0x63},   // X
    {7,8,0x70,8,7},              // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0,0x7f,0x41,0x41,0},       // [
    {2,4,8,0x10,0x20},            // backslash
    {0,0x41,0x41,0x7f,0},       // ]
    {4,2,1,2,4},                  // ^
    {0x40,0x40,0x40,0x40,0x40}, // _
    {0,1,2,4,0},                  // `
    {8,0x54,0x54,0x54,0x20},    // a
    {0x7f,0x48,0x44,0x44,0x38}, // b
    {0x38,0x44,0x44,0x44,0x20}, // c
    {0x38,0x44,0x44,0x48,0x7f}, // d
    {0x38,0x54,0x54,0x54,0x18}, // e
    {8,0x7e,9,1,2},               // f
    {0x0c,0x52,0x52,0x52,0x3e}, // g
    {0x7f,8,4,4,0x78},            // h
    {0,0x44,0x7d,0x40,0},        // i
    {0x20,0x40,0x44,0x3d,0},    // j
    {0x7f,0x10,0x28,0x44,0},    // k
    {0,0x41,0x7f,0x40,0},        // l
    {0x7c,4,0x18,4,0x78},       // m
    {0x7c,8,4,4,0x78},            // n
    {0x38,0x44,0x44,0x44,0x38}, // o
    {0x7c,0x14,0x14,0x14,8},    // p
    {8,0x14,0x14,0x18,0x7c},    // q
    {0x7c,8,4,4,8},               // r
    {0x48,0x54,0x54,0x54,0x20}, // s
    {4,0x3f,0x44,0x40,0x20},    // t
    {0x3c,0x40,0x40,0x20,0x7c}, // u
    {0x1c,0x20,0x40,0x20,0x1c},// v
    {0x3c,0x40,0x30,0x40,0x3c},// w
    {0x44,0x28,0x10,0x28,0x44},// x
    {0x0c,0x50,0x50,0x50,0x3c},// y
    {0x44,0x64,0x54,0x4c,0x44},// z
    {0,8,0x36,0x41,0},           // {
    {0,0,0x7f,0,0},              // |
    {0,0x41,0x36,8,0},           // }
    {8,4,8,0x10,8}                // ~
};

static void ui_text(f32 x, f32 y, const char *str, f32 scale,
                    u8 r, u8 g, u8 b) {
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        if (c < 32 || c > 126) c = '?';
        const u8 *glyph = font5x7[c - 32];
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (glyph[col] & (1u << row))
                    ui_rect(x + col * scale, y + row * scale,
                            scale * 0.82f, scale * 0.82f, r, g, b, 255);
            }
        }
        x += 6 * scale;
    }
}

static void ui_outline(f32 x, f32 y, f32 w, f32 h,
                       u8 r, u8 g, u8 b) {
    ui_rect(x, y, w, 2, r, g, b, 255);
    ui_rect(x, y+h-2, w, 2, r, g, b, 255);
    ui_rect(x, y, 2, h, r, g, b, 255);
    ui_rect(x+w-2, y, 2, h, r, g, b, 255);
}

static int gx_init(void) {
    Mtx44 projection;
    GXColor clear = { 4, 3, 18, 255 };

    /* GX_Init exige une FIFO valide, alignée sur 32 octets. */
    gx_fifo = memalign(32, GX_FIFO_SIZE);
    if (gx_fifo == NULL) return 0;
    memset(gx_fifo, 0, GX_FIFO_SIZE);
    GX_Init(gx_fifo, GX_FIFO_SIZE);
    GX_SetCopyClear(clear, 0x00ffffff);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    guOrtho(projection, 0, (f32)UI_H, 0, (f32)UI_W, 0, 1);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX,
                   0, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    /* Ne conserver aucun descripteur de sommet hérité. */
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    /* La hauteur retournée doit être utilisée comme hauteur de destination. */
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    xfb_height = GX_SetDispCopyYScale(yscale);
    if (xfb_height == 0 || xfb_height > rmode->xfbHeight) return 0;
    GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering,
                    (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_TRUE : GX_FALSE);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_InvVtxCache();

    /* Start from a known EFB color before the first frame is copied to VI. */
    GX_SetCopyClear(clear, 0x00ffffff);
    GX_SetColorUpdate(GX_TRUE);
    GX_DrawDone();
    return 1;
}

static int init_video(void) {
    VIDEO_Init();
    PAD_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    if (rmode == NULL) return 0;

    /* Deux XFB pour alterner affichage et rendu sans réécrire l'image visible. */
    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    if (xfb[0] == NULL || xfb[1] == NULL) return 0;

    VIDEO_Configure(rmode);
    displayed_fb = 0;
    VIDEO_SetNextFramebuffer(xfb[displayed_fb]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitForRetrace();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitForRetrace();

    sx = (f32)rmode->fbWidth / UI_W;
    sy = (f32)rmode->efbHeight / UI_H;
    return gx_init();
}

static void draw(int sel, unsigned int anim) {
    const int rows = 14;
    int start = sel - rows / 2;
    if (start > nfiles - rows) start = nfiles - rows;
    if (start < 0) start = 0;

    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);

    /* Draw in a 640x480 logical canvas; scale coordinates to the active video mode. */
    ui_rect(0, 0, UI_W, UI_H, 4, 3, 18, 255);

    /* Header with neon accent */
    ui_rect(20, 22, 600, 2, 65, 35, 155, 255);
    ui_text(28, 31, "GC-MP3", 3.0f, 225, 211, 255);
    ui_text(160, 39, "INDIGO AUDIO PLAYER", 1.2f, 155, 126, 225);
    ui_text(495, 33, "AUDIO", 1.5f, 70, 75, 255);

    /* Main panels */
    ui_rect(18, 76, 274, 344, 8, 6, 25, 255);
    ui_outline(18, 76, 274, 344, 126, 45, 255);
    ui_rect(306, 76, 316, 344, 7, 5, 24, 255);
    ui_outline(306, 76, 316, 344, 65, 79, 255);

    ui_text(30, 88, "SD:/MP3", 1.45f, 220, 213, 255);
    char count[32];
    snprintf(count, sizeof(count), "%d TRACKS", nfiles);
    ui_text(195, 90, count, 1.0f, 70, 225, 255);
    ui_rect(28, 107, 252, 1, 50, 35, 115, 255);

    ui_text(320, 88, "NOW PLAYING", 1.3f, 164, 128, 245);
    snprintf(count, sizeof(count), "%02d/%02d",
             (playing && current >= 0) ? current + 1 : 0, nfiles);
    ui_text(565, 89, count, 1.0f, 90, 205, 255);

    /* Playlist */
    for (int i = 0; i < rows; i++) {
        int k = start + i;
        int y = 116 + i * 20;
        if (k >= nfiles) break;

        if (k == sel) {
            ui_rect(26, y - 2, 256, 18, 75, 18, 150, 255);
            ui_outline(26, y - 2, 256, 18, 191, 73, 255);
            ui_text(32, y + 2, ">", 1.3f, 255, 255, 255);
        } else {
            ui_text(34, y + 2, "♪", 1.0f, 145, 100, 220);
        }

        char line[NAME_LEN + 16];
        snprintf(line, sizeof(line), "%02d %.30s", k + 1, names[k]);
        ui_text(k == sel ? 48 : 48, y + 2, line, 1.0f,
                k == sel ? 255 : 190, k == sel ? 245 : 187, 255);

        if (playing && k == current) {
            ui_text(257, y + 2, paused ? "||" : ">", 1.0f, 70, 225, 255);
        }
    }

    if (nfiles == 0)
        ui_text(34, 124, "NO MP3 FILES FOUND", 1.1f, 255, 205, 100);

    /* Track info and pseudo-cover tile */
    ui_rect(322, 119, 96, 104, 18, 10, 45, 255);
    ui_outline(322, 119, 96, 104, 150, 65, 255);
    ui_rect(338, 139, 64, 2, 80, 45, 180, 255);
    ui_rect(350, 150, 40, 40, 55, 26, 120, 255);
    ui_rect(359, 158, 22, 22, 150, 70, 255, 255);
    ui_rect(367, 166, 6, 6, 230, 210, 255, 255);

    if (playing && current >= 0 && current < nfiles) {
        ui_text(432, 132, names[current], 1.65f, 242, 233, 255);
        ui_text(432, 154, paused ? "PAUSED" : "PLAYING", 1.2f,
                paused ? 255 : 75, paused ? 190 : 230, 255);
    } else {
        ui_text(432, 135, "SELECT A TRACK", 1.25f, 220, 210, 255);
        ui_text(432, 156, "PRESS A TO PLAY", 1.0f, 100, 205, 255);
    }

    ui_rect(432, 185, 160, 2, 50, 35, 115, 255);
    ui_text(432, 194, "GC AUDIO", 1.0f, 145, 125, 205);

    /* Animated equalizer (decorative; not an FFT of the audio signal). */
    for (int i = 0; i < 36; i++) {
        int bh = (playing && !paused)
            ? 8 + ((i * 13 + anim * 3 + (f_count / 350)) % 42)
            : 6 + ((i * 7) % 12);
        f32 x = 322 + i * 8.0f;
        ui_rect(x, 286 - bh, 4, bh, (i < 18) ? 174 : 65,
                (i < 18) ? 55 : 95, 255, 255);
        ui_rect(x, 288, 4, 2, 48, 30, 100, 255);
    }

    /* Playback controls */
    ui_outline(330, 321, 48, 48, 135, 80, 255);
    ui_text(344, 339, "<<", 1.2f, 220, 210, 255);
    ui_rect(400, 317, 58, 56, 66, 13, 135, 255);
    ui_outline(400, 317, 58, 56, 204, 83, 255);
    ui_text(420, 337, (playing && !paused) ? "||" : ">", 2.0f, 255, 245, 255);
    ui_outline(480, 321, 48, 48, 135, 80, 255);
    ui_text(493, 339, ">>", 1.2f, 220, 210, 255);
    ui_outline(550, 321, 48, 48, 135, 80, 255);
    ui_rect(568, 337, 12, 14, 220, 210, 255, 255);

    /* Bottom controller legend */
    ui_rect(18, 432, 604, 30, 8, 6, 25, 255);
    ui_outline(18, 432, 604, 30, 105, 44, 230);
    ui_text(30, 443, "D-PAD BROWSE", 1.0f, 180, 155, 225);
    ui_rect(180, 439, 18, 18, 12, 150, 55, 255);
    ui_text(185, 444, "A", 1.0f, 255, 255, 255);
    ui_text(204, 443, "PLAY", 1.0f, 220, 210, 255);
    ui_rect(263, 439, 18, 18, 100, 20, 220, 255);
    ui_text(268, 444, "Y", 1.0f, 255, 255, 255);
    ui_text(287, 443, "PAUSE", 1.0f, 220, 210, 255);
    ui_rect(365, 439, 18, 18, 180, 24, 55, 255);
    ui_text(370, 444, "B", 1.0f, 255, 255, 255);
    ui_text(389, 443, "STOP", 1.0f, 220, 210, 255);
    ui_text(480, 443, "START QUIT", 1.0f, 175, 155, 220);

    GX_DrawDone();
    int next_fb = 1 - displayed_fb;
    GX_CopyDisp(xfb[next_fb], GX_TRUE);
    GX_DrawDone();
    VIDEO_SetNextFramebuffer(xfb[next_fb]);
    VIDEO_Flush();
    displayed_fb = next_fb;
}


// ---------- Programme ----------
int main(void) {
    if (!init_video()) return 1;

    if (!mount_sd()) {
        GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
        GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
        ui_rect(0, 0, UI_W, UI_H, 4, 3, 18, 255);
        ui_outline(30, 80, 580, 280, 126, 45, 255);
        ui_text(55, 110, "GC-MP3", 3.0f, 225, 211, 255);
        ui_text(55, 180, "SD CARD NOT FOUND", 2.0f, 255, 190, 90);
        ui_text(55, 220, "CHECK SD2SP2 OR SD GECKO", 1.3f, 180, 165, 220);
        ui_text(55, 260, "PRESS START TO QUIT", 1.3f, 80, 220, 255);
        GX_DrawDone();
        GX_CopyDisp(xfb, GX_TRUE);
        GX_DrawDone();
        VIDEO_SetNextFramebuffer(xfb);
        VIDEO_Flush();

        while (1) {
            PAD_ScanPads();
            if (PAD_ButtonsDown(0) & PAD_BUTTON_START) return 0;
            VIDEO_WaitForRetrace();
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
    unsigned int ui_anim = 0;

    while (1) {
        PAD_ScanPads();
        u32 down = PAD_ButtonsDown(0);

        if (down & PAD_BUTTON_START) break;
        if ((down & PAD_BUTTON_DOWN) && sel < nfiles - 1) {
            sel++;
            redraw = 1;
        }
        if ((down & PAD_BUTTON_UP) && sel > 0) {
            sel--;
            redraw = 1;
        }
        if ((down & PAD_BUTTON_A) && nfiles > 0) {
            start_track(sel);
            redraw = 1;
        }
        if (down & PAD_BUTTON_B) {
            stop_track();
            redraw = 1;
        }
        if ((down & PAD_BUTTON_Y) && playing) {
            paused = !paused;
            redraw = 1;
        }

        service_audio();

        if (playing && !paused) {
            int k = 0;
            while (!eof && k < 2 &&
                   (FIFO_SIZE - f_count) >= FIFO_MARGIN) {
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

        /* Refresh regularly for the animated equalizer; keep audio serviced. */
        if ((++ui_anim % 5) == 0) redraw = 1;
        if (redraw) {
            draw(sel, ui_anim);
            redraw = 0;
            VIDEO_WaitForRetrace();
        }

        LWP_YieldThread();
    }

    stop_track();
    AUDIO_StopDMA();
    return 0;
}

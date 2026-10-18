// GC MP3 Player - lecteur MP3 simple pour GameCube (libogc + minimp3)
// Lit les fichiers .mp3 du dossier /mp3 de la carte SD.
// Commandes : Haut/Bas = choisir, A = lire, B = stop, Y = pause, START = quitter
#define VIDEO_WaitForRetrace VIDEO_WaitVSync
#include <gccore.h>
#include <fat.h>
#include <sdcard/gcsd.h>
#include <stdio.h>
#include <stdlib.h>
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

typedef struct { s16 l, r; } stereo_t;

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

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

// ---------- Affichage ----------
static void init_video(void) {
    VIDEO_Init();
    PAD_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitForRetrace();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitForRetrace();
}

static void draw(int sel) {
    const int rows = 14;
    int start = sel - rows / 2;
    if (start > nfiles - rows) start = nfiles - rows;
    if (start < 0) start = 0;

    printf("\x1b[2J");
    printf("\x1b[2;2HGC MP3 Player  (%d fichiers dans %s)", nfiles, MP3_DIR);
    for (int i = 0; i < rows && start + i < nfiles; i++) {
        int k = start + i;
        printf("\x1b[%d;2H%c%c %-.48s", 4 + i,
               (k == sel) ? '>' : ' ',
               (playing && k == current) ? (paused ? '=' : '*') : ' ',
               names[k]);
    }
    if (nfiles == 0) printf("\x1b[4;2HAucun .mp3 trouve dans %s", MP3_DIR);
    printf("\x1b[19;2HHaut/Bas: choisir   A: lire   B: stop");
    printf("\x1b[20;2HY: pause   START: quitter");
}

// ---------- Programme ----------
int main(void) {
    init_video();

    if (!mount_sd()) {
        printf("\x1b[2;2HCarte SD introuvable.\n");
        printf("\x1b[4;2HAppuie sur START pour quitter.\n");
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
    dma_cb();            // programme le premier tampon (silence)
    AUDIO_StartDMA();

    int sel = 0, redraw = 1;
    while (1) {
        PAD_ScanPads();
        u32 down = PAD_ButtonsDown(0);
        if (down & PAD_BUTTON_START) break;
        if ((down & PAD_BUTTON_DOWN) && sel < nfiles - 1) { sel++; redraw = 1; }
        if ((down & PAD_BUTTON_UP) && sel > 0)            { sel--; redraw = 1; }
        if ((down & PAD_BUTTON_A) && nfiles > 0)          { start_track(sel); redraw = 1; }
        if (down & PAD_BUTTON_B)                          { stop_track(); redraw = 1; }
        if ((down & PAD_BUTTON_Y) && playing)             { paused = !paused; redraw = 1; }

        service_audio();

        if (playing && !paused) {
            int k = 0;
            while (!eof && k < 2 && (FIFO_SIZE - f_count) >= FIFO_MARGIN) {
                decode_one();
                service_audio();     // on remplit tout de suite si un tampon se libère
                k++;
            }
            if (eof && f_count == 0) {          // morceau terminé : on passe au suivant
                int nx = current + 1;
                if (nx < nfiles) { sel = nx; start_track(nx); }
                else stop_track();
                redraw = 1;
            }
        }

        service_audio();

        if (redraw) { draw(sel); redraw = 0; }
        LWP_YieldThread();
    }

    stop_track();
    AUDIO_StopDMA();
    return 0;
}

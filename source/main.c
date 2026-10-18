// GC-MP3 - Indigo Neon Audio Player
#define VIDEO_WaitForRetrace VIDEO_WaitVSync

#include <gccore.h>
#include <fat.h>
#include <sdcard/gcsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

#define MP3_DIR        "sd:/mp3"
#define MAX_FILES      512
#define NAME_LEN       128
#define MAX_FILE_SIZE  (18 * 1024 * 1024)
#define DMA_SAMPLES    2048
#define NUM_BUFS       3
#define DMA_BYTES      (DMA_SAMPLES * 4)
#define FIFO_SIZE      16384
#define FIFO_MARGIN    7200

typedef struct { s16 l, r; } stereo_t;

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

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

        if (l > 4 && l < NAME_LEN &&
            strcasecmp(e->d_name + l - 4, ".mp3") == 0) {
            strcpy(names[nfiles++], e->d_name);
        }
    }

    closedir(d);
    qsort(names, nfiles, NAME_LEN, cmp_names);
}

static int mount_sd(void) {
    if (fatMountSimple("sd", &__io_gcsd2)) return 1;
    if (fatMountSimple("sd", &__io_gcsda)) return 1;
    if (fatMountSimple("sd", &__io_gcsdb)) return 1;
    return 0;
}

/* AUDIO DMA */

static s16 dmabuf[NUM_BUFS][DMA_SAMPLES * 2]
    ATTRIBUTE_ALIGN(32);

static volatile u32 cb_n = 0;
static volatile int need_fill = 0;

static stereo_t fifo[FIFO_SIZE];
static int f_head = 0, f_tail = 0, f_count = 0;

static int paused = 0;

static void dma_cb(void) {
    AUDIO_InitDMA((u32)dmabuf[cb_n % NUM_BUFS], DMA_BYTES);
    cb_n++;
    need_fill = 1;
}

static void fill_buf(int idx) {
    s16 *d = dmabuf[idx];

    for (int i = 0; i < DMA_SAMPLES; i++) {
        if (!paused && f_count > 0) {
            d[2 * i] = fifo[f_head].l;
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

/* MP3 DECODER */

static mp3dec_t dec;
static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

static unsigned char *fdata = NULL;
static long fsize = 0, fpos = 0;

static int playing = 0, eof = 0, current = -1;

static double rpos = 0.0;
static s16 prevL = 0, prevR = 0;

static void push_frame(
    const s16 *L, const s16 *R, int n, unsigned sr
) {
    double step = (double)sr / 48000.0;

    while (rpos < n - 1) {
        int i = (rpos < 0.0) ? -1 : (int)rpos;
        double fr = rpos - i;

        s16 al = (i < 0) ? prevL : L[i];
        s16 ar = (i < 0) ? prevR : R[i];

        s16 bl = L[i + 1];
        s16 br = R[i + 1];

        fifo_put(
            (s16)(al + (bl - al) * fr),
            (s16)(ar + (br - ar) * fr)
        );

        rpos += step;
    }

    rpos -= n;
    prevL = L[n - 1];
    prevR = R[n - 1];
}

static void decode_one(void) {
    static s16 L[1152], R[1152];

    if (fpos >= fsize) {
        eof = 1;
        return;
    }

    mp3dec_frame_info_t info;

    int n = mp3dec_decode_frame(
        &dec, fdata + fpos,
        (int)(fsize - fpos), pcm, &info
    );

    if (info.frame_bytes <= 0) {
        eof = 1;
        return;
    }

    fpos += info.frame_bytes;

    if (n <= 0) return;
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
    if (fdata) {
        free(fdata);
        fdata = NULL;
    }

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

    if (size <= 0 || size > MAX_FILE_SIZE) {
        fclose(fp);
        return 0;
    }

    fdata = (unsigned char *)malloc(size);

    if (!fdata) {
        fclose(fp);
        return 0;
    }

    if (fread(fdata, 1, size, fp) != (size_t)size) {
        fclose(fp);
        free(fdata);
        fdata = NULL;
        return 0;
    }

    fclose(fp);

    mp3dec_init(&dec);

    fsize = size;
    fpos = 0;

    f_head = f_tail = f_count = 0;

    rpos = 0.0;
    prevL = prevR = 0;

    eof = 0;
    playing = 1;
    paused = 0;
    current = idx;

    return 1;
}

/* VIDEO */

static void init_video(void) {
    VIDEO_Init();
    PAD_Init();

    rmode = VIDEO_GetPreferredMode(NULL);

    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

    console_init(
        xfb, 20, 20,
        rmode->fbWidth, rmode->xfbHeight,
        rmode->fbWidth * VI_DISPLAY_PIX_SZ
    );

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitForRetrace();

    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitForRetrace();
}

/* NEON INDIGO INTERFACE */

static void draw(int sel) {
    const int rows = 11;

    int start = sel - rows / 2;

    if (start > nfiles - rows)
        start = nfiles - rows;

    if (start < 0)
        start = 0;

    printf("\x1b[2J\x1b[0m");

    printf("\x1b[1;35m+--------------------------------------------------------------------------+\x1b[0m");
    printf("\x1b[2;2H\x1b[1;35m|\x1b[0m  \x1b[1;36mGC-MP3\x1b[0m  \x1b[90m/ INDIGO AUDIO SYSTEM\x1b[0m");
    printf("\x1b[2;78H\x1b[1;35m|\x1b[0m");

    printf("\x1b[3;2H\x1b[1;35m|\x1b[0m  \x1b[90mSD2SP2  \x1b[35m*\x1b[0m  \x1b[90mLIBRARY\x1b[0m");
    printf("\x1b[3;67H\x1b[36m%3d TRACKS\x1b[0m", nfiles);
    printf("\x1b[3;78H\x1b[1;35m|\x1b[0m");

    printf("\x1b[4;2H\x1b[1;35m+--------------------------------------------------------------------------+\x1b[0m");

    printf("\x1b[5;3H\x1b[90mNOW PLAYING\x1b[0m");

    if (playing && current >= 0 && current < nfiles) {
        printf(
            "\x1b[5;17H\x1b[1;37m%-42.42s\x1b[0m",
            names[current]
        );

        printf(
            "\x1b[5;65H%s",
            paused
                ? "\x1b[1;33m|| PAUSED\x1b[0m"
                : "\x1b[1;32m> PLAYING\x1b[0m"
        );
    } else {
        printf("\x1b[5;17H\x1b[90mNo track selected\x1b[0m");
        printf("\x1b[5;65H\x1b[90mSTANDBY\x1b[0m");
    }

    printf("\x1b[6;3H\x1b[35m------------------------------------------------------------------------\x1b[0m");
    printf("\x1b[7;3H\x1b[90mTRACK LIST\x1b[0m");

    printf(
        "\x1b[7;67H\x1b[90m%02d-%02d / %02d\x1b[0m",
        nfiles ? start + 1 : 0,
        nfiles
            ? (start + rows < nfiles ? start + rows : nfiles)
            : 0,
        nfiles
    );

    for (int i = 0; i < rows; i++) {
        int k = start + i;
        int y = 8 + i;

        printf(
            "\x1b[%d;3H\x1b[0m                                                                          ",
            y
        );

        if (k < nfiles) {
            if (k == sel) {
                printf(
                    "\x1b[%d;3H\x1b[45m\x1b[1;37m >  %02d  %-49.49s \x1b[0m",
                    y, k + 1, names[k]
                );
            } else {
                printf(
                    "\x1b[%d;5H\x1b[90m%02d\x1b[0m  \x1b[37m%-49.49s\x1b[0m",
                    y, k + 1, names[k]
                );
            }

            if (playing && k == current) {
                printf(
                    "\x1b[%d;68H%s",
                    y,
                    paused
                        ? "\x1b[33m|| PAUSE\x1b[0m"
                        : "\x1b[36m> PLAY\x1b[0m"
                );
            }
        }
    }

    if (nfiles == 0) {
        printf("\x1b[9;5H\x1b[1;33mNo MP3 files found\x1b[0m");
        printf("\x1b[10;5H\x1b[90mPlace your files in %s\x1b[0m", MP3_DIR);
    }

    printf("\x1b[20;2H\x1b[1;35m+--------------------------------------------------------------------------+\x1b[0m");
    printf("\x1b[21;2H\x1b[90m D-PAD \x1b[0m Browse   \x1b[1;36mA\x1b[0m Play   \x1b[1;36mY\x1b[0m Pause   \x1b[1;36mB\x1b[0m Stop   \x1b[1;36mSTART\x1b[0m Exit");
}

/* MAIN */

int main(void) {
    init_video();

    if (!mount_sd()) {
        printf("\x1b[2J\x1b[1;35m+------------------------------------------+\x1b[0m");
        printf("\x1b[3;3H\x1b[1;36mGC-MP3\x1b[0m  \x1b[90m/ INDIGO AUDIO SYSTEM\x1b[0m");
        printf("\x1b[5;3H\x1b[1;33mSD CARD NOT FOUND\x1b[0m");
        printf("\x1b[7;3H\x1b[90mCheck SD2SP2 or SD Gecko connection.\x1b[0m");
        printf("\x1b[9;3HPress START to exit.");

        while (1) {
            PAD_ScanPads();

            if (PAD_ButtonsDown(0) & PAD_BUTTON_START)
                return 0;

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

    while (1) {
        PAD_ScanPads();

        u32 down = PAD_ButtonsDown(0);

        if (down & PAD_BUTTON_START)
            break;

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

        if (redraw) {
            draw(sel);
            redraw = 0;
        }

        LWP_YieldThread();
    }

    stop_track();
    AUDIO_StopDMA();

    return 0;
}

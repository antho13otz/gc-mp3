// GC-MP3 "Indigo Audio Player" - musique (carte SD) + webradios + navigateur web pour GameCube
// (libogc + GX + minimp3 + Broadband Adapter + mbedTLS)
//
// Onglet MUSIQUE : fichiers .mp3 de sd:/mp3 et sd:/downloads (sous-dossiers compris)
//   Pochettes : <meme nom>.jpg/.png, pochette integree (ID3), ou cover.jpg/folder.jpg du dossier
// Onglet RADIO : liste sd:/radio.txt (creee au premier lancement), une station par ligne :
//   Nom | http://adresse/du/flux       Logo facultatif : sd:/radio/<Nom>.jpg ou .png
//   Flux MP3 en http:// ou https:// (pas AAC/OGG). Listes .m3u/.pls et redirections suivies.
// Onglet WEB : navigateur en mode texte (sans JavaScript ni images). Les liens vers des
//   fichiers sont telecharges dans sd:/downloads. Favoris : sd:/favoris.txt (Nom | adresse).
//
// Commandes : Z = changer d'onglet (la lecture continue), Haut/Bas = choisir,
//             Gauche/Droite = page, A = lire, B = arreter, Y = pause (musique) / muet (radio),
//             X = actualiser la liste (musique), L/R = piste ou station precedente/suivante,
//             START = quitter
//   WEB : A = ouvrir le lien, B = page precedente, Y = adresse / recherche, X = accueil,
//         stick C = defiler
// Le reseau n'est initialise qu'au premier passage sur l'onglet RADIO ou WEB.
#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/system.h>
#include <ogc/lwp_watchdog.h>
#include <network.h>
#include <fat.h>
#include <sdcard/gcsd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

// Decodeur MP3 : minimp3 (fichier unique, domaine public), telecharge par le workflow
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

// Decodeur JPEG/PNG (logos des stations) : stb_image, telecharge par le workflow
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

// HTTPS : mbedTLS 3.6 (telecharge et compile par le workflow, reglages dans gc_tls_config.h)
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform_time.h"
#include "psa/crypto.h"
// Certificats racines (liste Mozilla, convertie en cacert.h par le workflow)
#include "cacert.h"

// Atlas de polices lissees (genere par tools/gen_font.py)
#include "font_atlas.h"

#ifndef SD_ROOT
#define SD_ROOT        "sd:"
#endif
#define RADIO_LIST     SD_ROOT "/radio.txt"
#define LOGO_DIR       SD_ROOT "/radio"
#define MP3_DIR        SD_ROOT "/mp3"
#define MAX_FILES      512
#define NAME_LEN       256                  // chemin complet (sd:/dossier/.../fichier.mp3)
#define DL_DIR         SD_ROOT "/downloads" // telechargements (CubeBrowser...)
#define SCAN_DEPTH     3                    // sous-dossiers explores
#define MAX_FILE_SIZE  (16 * 1024 * 1024)   // un MP3 est charge entierement en RAM
#define MAX_STATIONS   128
#define LIST_ROWS      14
#define DMA_SAMPLES    2048                 // echantillons stereo par tampon DMA (~43 ms)
#define NUM_BUFS       3
#define DMA_BYTES      (DMA_SAMPLES * 4)
#define FIFO_SIZE      (48000 * 6)          // 6 s de son decode d'avance (48 kHz stereo)
#define FIFO_MARGIN    8192                 // place minimale avant de decoder une trame
#define PREBUFFER      (48000 * 2)          // 2 s de son avant de commencer a jouer
#define CBUF_SIZE      (192 * 1024)         // donnees MP3 recues, pas encore decodees
#define MP3_MIN_BYTES  16384                // minimp3 a besoin de plusieurs trames pour se caler
#define GX_FIFO_SIZE   (256 * 1024)
#define COVER_SZ       128
#define COVER_MAX_PX   (1500 * 1500)
#define COVER_MAX_FILE (8 * 1024 * 1024)
#define PI_F           3.14159265f

typedef struct { s16 l, r; } stereo_t;
typedef struct { u8 r, g, b, a; } col_t;
#define RGBA(r, g, b, a) ((col_t){ (u8)(r), (u8)(g), (u8)(b), (u8)(a) })

static void *xfb[2] = { NULL, NULL };
static void *gx_fifo = NULL;
static GXRModeObj *rmode = NULL;
static int displayed_fb = 0;

static u64 now_ms(void) { return ticks_to_millisecs(gettime()); }

/* Compteur d'images, incremente a chaque balayage vertical (interruption video). */
static volatile u32 retrace_count = 0;
static void retrace_cb(u32 count) { (void)count; retrace_count++; }

// ---------- Etat de la lecture ----------
enum { ST_IDLE, ST_CONNECT, ST_BUFFER, ST_PLAY, ST_ERROR };
static int state = ST_IDLE;
static int muted = 0;
enum { SRC_NONE, SRC_FILE, SRC_RADIO };
static int src = SRC_NONE;              /* ce qui joue en ce moment */
enum { TAB_MUSIC, TAB_RADIO, TAB_WEB };
static int tab = TAB_MUSIC;             /* onglet affiche */
static int paused = 0;                  /* pause de la musique */
static void stop_track(void);
static void bg_tick(void);              /* musique/radio pendant les attentes reseau */
static int cur_station = -1;                 // station en cours
static char err_msg[96];                 // message d'erreur affiche
static char now_title[160];              // titre en cours (metadonnees ICY)
static char icy_name[64];                // nom annonce par le serveur
static int  stream_kbps = 0, stream_hz = 0, stream_ch = 0;
static int  rebuffers = 0;

// ---------- Audio (DMA, anneau de 3 tampons, 48 kHz) ----------
static s16 dmabuf[NUM_BUFS][DMA_SAMPLES * 2] ATTRIBUTE_ALIGN(32);
static volatile u32 cb_n = 0;
static volatile u32 filled_blk = 0xFFFFFFFFu;

static stereo_t fifo[FIFO_SIZE];
static int f_head = 0, f_tail = 0, f_count = 0;

#define VIZ_N 512
static s16 viz[VIZ_N];
static int viz_pos = 0;
static u32 consumed = 0;                 // echantillons joues depuis le debut de l'ecoute

/* Fin d'un bloc DMA : programme le suivant. S'il n'a pas ete rempli a temps,
   on joue du silence plutot que de rejouer d'anciennes donnees. */
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

static void fill_buf(int idx) {
    s16 *d = dmabuf[idx];
    int live = (src == SRC_RADIO && state == ST_PLAY) || (src == SRC_FILE && !paused);
    for (int i = 0; i < DMA_SAMPLES; i++) {
        s16 l = 0, r = 0;
        if (live && f_count > 0) {
            l = fifo[f_head].l;
            r = fifo[f_head].r;
            f_head = (f_head + 1) % FIFO_SIZE;
            f_count--;
            consumed++;
        }
        viz[viz_pos] = (s16)(((int)l + (int)r) / 2);
        viz_pos = (viz_pos + 1) & (VIZ_N - 1);
        if (muted) l = r = 0;
        d[2 * i] = l;
        d[2 * i + 1] = r;
    }
    DCFlushRange(d, DMA_BYTES);
}

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
    if (f_count >= FIFO_SIZE) return 0;
    fifo[f_tail].l = l;
    fifo[f_tail].r = r;
    f_tail = (f_tail + 1) % FIFO_SIZE;
    f_count++;
    return 1;
}

// ---------- Analyseur de spectre (Goertzel) ----------
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
        float frac = sqrtf(p) / ((float)VIZ_N * 0.25f * 32768.0f);
        float db = 20.0f * log10f(frac + 1e-6f) + 0.10f * (float)b;
        float v = (db + 62.0f) / 50.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        bar_val[b] = (v > bar_val[b]) ? v : (bar_val[b] * 0.82f + v * 0.18f);
        if (bar_val[b] < 0.01f) bar_val[b] = 0.0f; else active = 1;
    }
    return active;
}

// ---------- Donnees MP3 recues (tampon lineaire) ----------
static unsigned char cbuf[CBUF_SIZE];
static int cb_start = 0, cb_end = 0;

static int cbuf_avail(void) { return cb_end - cb_start; }
static int cbuf_free(void)  { return CBUF_SIZE - (cb_end - cb_start); }

static void cbuf_append(const unsigned char *d, int n) {
    if (n <= 0) return;
    if (cb_end + n > CBUF_SIZE) {                     /* on recolle au debut */
        memmove(cbuf, cbuf + cb_start, cb_end - cb_start);
        cb_end -= cb_start;
        cb_start = 0;
    }
    if (n > CBUF_SIZE - cb_end) n = CBUF_SIZE - cb_end;
    memcpy(cbuf + cb_end, d, n);
    cb_end += n;
}

// ---------- Decodage MP3 (minimp3) + reechantillonnage 48 kHz ----------
static mp3dec_t dec;
static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
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
    rpos -= n;
    prevL = L[n - 1];
    prevR = R[n - 1];
}

/* Decode une trame. Retourne 0 s'il faut attendre plus de donnees. */
static int decode_stream_one(int stream_closed) {
    static s16 L[1152], R[1152];
    int avail = cbuf_avail();
    if (avail <= 0) return 0;
    if (avail < MP3_MIN_BYTES && !stream_closed) return 0;

    mp3dec_frame_info_t info;
    int n = mp3dec_decode_frame(&dec, cbuf + cb_start, avail, pcm, &info);
    if (info.frame_bytes <= 0) {
        /* Aucune trame trouvee : donnees illisibles. On les jette en gardant
           la fin (une trame peut commencer juste avant la limite). */
        int drop = stream_closed ? avail : avail - 2048;
        if (drop > 0) cb_start += drop;
        return 0;
    }
    cb_start += info.frame_bytes;
    if (n <= 0 || info.hz <= 0) return 1;
    if (n > 1152) n = 1152;
    stream_kbps = info.bitrate_kbps;
    stream_hz = info.hz;
    stream_ch = info.channels;
    for (int i = 0; i < n; i++) {
        if (info.channels == 2) { L[i] = pcm[2 * i]; R[i] = pcm[2 * i + 1]; }
        else { L[i] = R[i] = pcm[i]; }
    }
    push_frame(L, R, n, (unsigned)info.hz);
    return 1;
}

static int mount_sd(void) {
    if (fatMountSimple("sd", &__io_gcsd2)) return 1;   // SD2SP2 (port serie 2)
    if (fatMountSimple("sd", &__io_gcsda)) return 1;   // SD Gecko slot A
    if (fatMountSimple("sd", &__io_gcsdb)) return 1;   // SD Gecko slot B
    return 0;
}

// ---------- Pochettes ----------
static u16 cover_tex[COVER_SZ * COVER_SZ] ATTRIBUTE_ALIGN(32);
static int cover_ok = 0;

/* Decode une image JPEG/PNG en memoire, la recadre au carre (centre) et la
   reduit en 128x128 dans la texture GX (RGB565, tuiles 4x4). */
static int cover_from_memory(const unsigned char *data, long len) {
    int w, h, n;
    if (len <= 0 || len > 0x7fffffffL) return 0;
    if (!stbi_info_from_memory(data, (int)len, &w, &h, &n)) return 0;
    if (w <= 0 || h <= 0 || (long)w * (long)h > COVER_MAX_PX) return 0;
    unsigned char *px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 3);
    if (!px) return 0;

    int side = (w < h) ? w : h;
    int ox = (w - side) / 2, oy = (h - side) / 2;
    for (int y = 0; y < COVER_SZ; y++) {
        int y0 = oy + y * side / COVER_SZ, y1 = oy + (y + 1) * side / COVER_SZ;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < COVER_SZ; x++) {
            int x0 = ox + x * side / COVER_SZ, x1 = ox + (x + 1) * side / COVER_SZ;
            if (x1 <= x0) x1 = x0 + 1;
            u32 r = 0, g = 0, b = 0, cnt = 0;
            for (int yy = y0; yy < y1; yy++) {
                const unsigned char *p = px + ((size_t)yy * w + x0) * 3;
                for (int xx = x0; xx < x1; xx++, p += 3) { r += p[0]; g += p[1]; b += p[2]; cnt++; }
            }
            r /= cnt; g /= cnt; b /= cnt;
            int blk = (y >> 2) * (COVER_SZ / 4) + (x >> 2);
            cover_tex[blk * 16 + (y & 3) * 4 + (x & 3)] =
                (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
        if ((y & 15) == 0) service_audio();
    }
    stbi_image_free(px);
    DCFlushRange(cover_tex, sizeof(cover_tex));
    GX_InvalidateTexAll();
    return 1;
}

static unsigned char *read_file(const char *path, long maxsz, long *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    long size = -1;
    if (fseek(fp, 0, SEEK_END) == 0) size = ftell(fp);
    if (size <= 0 || size > maxsz || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    unsigned char *buf = (unsigned char *)malloc(size);
    if (buf && fread(buf, 1, size, fp) != (size_t)size) { free(buf); buf = NULL; }
    fclose(fp);
    if (buf) *out_len = size;
    return buf;
}

static int cover_from_file(const char *path) {
    long len = 0;
    unsigned char *buf = read_file(path, COVER_MAX_FILE, &len);
    if (!buf) return 0;
    int ok = cover_from_memory(buf, len);
    free(buf);
    return ok;
}
// ---------- Liste des stations (sd:/radio.txt) ----------
typedef struct { char name[48]; char url[256]; } station_t;
static station_t stations[MAX_STATIONS];
static int nstations = 0;
static int sd_ok = 0;
static int list_created = 0;            /* 1 si radio.txt vient d'etre cree */

static const char *DEFAULT_LIST =
    "# GC-Radio : une station par ligne, au format   Nom | URL\n"
    "# Flux MP3 en http:// ou https:// (pas AAC/OGG).\n"
    "# Les listes .m3u et .pls sont acceptees. Les lignes qui commencent\n"
    "# par # sont ignorees. Logo facultatif : sd:/radio/<Nom>.jpg ou .png\n"
    "# Conseil : preferer des serveurs proches (France/Europe). Le reseau de\n"
    "# la GameCube est lent avec les serveurs lointains (USA) en 128 kbps.\n"
    "FIP | http://icecast.radiofrance.fr/fip-midfi.mp3\n"
    "France Inter | http://icecast.radiofrance.fr/franceinter-midfi.mp3\n"
    "franceinfo | http://icecast.radiofrance.fr/franceinfo-midfi.mp3\n"
    "France Culture | http://icecast.radiofrance.fr/franceculture-midfi.mp3\n"
    "Mouv' | http://icecast.radiofrance.fr/mouv-midfi.mp3\n"
    "SomaFM Groove Salad | http://ice1.somafm.com/groovesalad-128-mp3\n"
    "SomaFM Drone Zone | http://ice1.somafm.com/dronezone-128-mp3\n"
    "Radio Paradise | http://stream.radioparadise.com/mp3-128\n";

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static void parse_station_line(char *line) {
    char *s = trim(line);
    if (!*s || *s == '#' || nstations >= MAX_STATIONS) return;
    /* retire un eventuel BOM UTF-8 */
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s = trim(s + 3);
    station_t *st = &stations[nstations];
    char *bar = strchr(s, '|');
    char *url = bar ? trim(bar + 1) : s;
    if (bar) *bar = 0;
    if (strncasecmp(url, "http://", 7) != 0 && strncasecmp(url, "https://", 8) != 0) return;
    snprintf(st->url, sizeof(st->url), "%s", url);
    if (bar && *trim(s)) {
        snprintf(st->name, sizeof(st->name), "%s", trim(s));
    } else {                                   /* pas de nom : on prend le nom du serveur */
        const char *h = strstr(url, "://") + 3;
        int n = (int)strcspn(h, ":/");
        if (n > (int)sizeof(st->name) - 1) n = sizeof(st->name) - 1;
        memcpy(st->name, h, n);
        st->name[n] = 0;
    }
    nstations++;
}

static void load_stations(void) {
    char line[512];
    nstations = 0;
    FILE *fp = sd_ok ? fopen(RADIO_LIST, "r") : NULL;
    if (!fp && sd_ok) {                        /* premier lancement : on cree la liste */
        FILE *w = fopen(RADIO_LIST, "w");
        if (w) {
            fputs(DEFAULT_LIST, w);
            fclose(w);
            list_created = 1;
            fp = fopen(RADIO_LIST, "r");
        }
    }
    if (fp) {
        while (fgets(line, sizeof(line), fp)) parse_station_line(line);
        fclose(fp);
    }
    if (nstations == 0) {                      /* pas de SD ou liste vide : liste integree */
        const char *p = DEFAULT_LIST;
        while (*p) {
            int n = (int)strcspn(p, "\n");
            if (n > (int)sizeof(line) - 1) n = sizeof(line) - 1;
            memcpy(line, p, n);
            line[n] = 0;
            parse_station_line(line);
            p += n;
            if (*p == '\n') p++;
        }
    }
}

// ---------- Reseau ----------
static char ip_str[16] = "", mask_str[16] = "", gw_str[16] = "";
static int net_ok = 0;
enum { NET_NONE, NET_BUSY, NET_DONE };
static volatile int net_phase = NET_NONE;
static lwp_t net_thread_h;
static u8 net_thread_stack[64 * 1024] ATTRIBUTE_ALIGN(32);

/* Initialisation du reseau (DHCP : jusqu'a 20 x 0,5 s) dans un thread a part,
   pour que la musique continue pendant ce temps. */
static void *net_thread(void *arg) {
    (void)arg;
    s32 r = if_config(ip_str, mask_str, gw_str, TRUE, 20);
    net_ok = (r >= 0 && ip_str[0] && strcmp(ip_str, "0.0.0.0") != 0);
    net_phase = NET_DONE;
    return NULL;
}

static void net_start_async(void) {
    if (net_phase != NET_NONE) return;
    net_phase = NET_BUSY;
    if (LWP_CreateThread(&net_thread_h, net_thread, NULL, net_thread_stack,
                         sizeof(net_thread_stack), 50) < 0) {
        net_ok = 0;
        net_phase = NET_DONE;
    }
}

static void set_nonblock(s32 s, int on) {
    u32 v = on ? 1 : 0;
    net_ioctl(s, FIONBIO, &v);
}

/* ---------- DNS : libogc ne fournit pas gethostbyname sur GameCube ----------
   Petite requete DNS (type A) en UDP : d'abord la box (passerelle DHCP), puis
   des serveurs publics. */
static int dns_name(const unsigned char *msg, int len, int pos) {    /* saute un nom */
    while (pos < len) {
        int l = msg[pos];
        if (l == 0) return pos + 1;
        if ((l & 0xC0) == 0xC0) return pos + 2;                   /* pointeur de compression */
        pos += l + 1;
    }
    return -1;
}

static int dns_query(u32 server, const char *host, u32 *out) {
    unsigned char q[300], a[512];
    int qlen = 12;
    static u16 qid = 0x4743;
    qid++;
    memset(q, 0, 12);
    q[0] = qid >> 8; q[1] = qid & 0xFF;
    q[2] = 0x01;                                                   /* recursion demandee */
    q[5] = 1;                                                      /* 1 question */
    const char *h = host;
    while (*h) {
        int n = (int)strcspn(h, ".");
        if (n == 0 || n > 63 || qlen + n + 6 > (int)sizeof(q)) return 0;
        q[qlen++] = (unsigned char)n;
        memcpy(q + qlen, h, n);
        qlen += n;
        h += n;
        if (*h == '.') h++;
    }
    q[qlen++] = 0;
    q[qlen++] = 0; q[qlen++] = 1;                                  /* type A */
    q[qlen++] = 0; q[qlen++] = 1;                                  /* classe IN */

    s32 s = net_socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);
    sa.sin_addr.s_addr = server;
    set_nonblock(s, 1);

    int found = 0;
    for (int attempt = 0; attempt < 2 && !found; attempt++) {
        if (net_sendto(s, q, qlen, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) break;
        u64 t0 = now_ms();
        while (!found && now_ms() - t0 < 1500) {
            s32 n = net_recvfrom(s, a, sizeof(a), 0, NULL, NULL);
            if (n == -EAGAIN || n == -EWOULDBLOCK) { bg_tick(); usleep(2000); continue; }
            if (n < 12) break;
            if (a[0] != q[0] || a[1] != q[1] || !(a[2] & 0x80)) continue;   /* pas notre reponse */
            if ((a[3] & 0x0F) != 0) { found = -1; break; }                  /* nom inconnu */
            int qd = (a[4] << 8) | a[5], an = (a[6] << 8) | a[7];
            int pos = 12;
            for (int i = 0; i < qd && pos > 0; i++) { pos = dns_name(a, n, pos); if (pos > 0) pos += 4; }
            for (int i = 0; i < an && pos > 0 && pos < n; i++) {
                pos = dns_name(a, n, pos);
                if (pos < 0 || pos + 10 > n) break;
                int type = (a[pos] << 8) | a[pos + 1];
                int rdlen = (a[pos + 8] << 8) | a[pos + 9];
                pos += 10;
                if (pos + rdlen > n) break;
                if (type == 1 && rdlen == 4) {
                    memcpy(out, a + pos, 4);                                /* deja en ordre reseau */
                    found = 1;
                    break;
                }
                pos += rdlen;
            }
            if (!found) found = -1;
        }
    }
    net_close(s);
    return found == 1;
}

static int resolve_host(const char *host, u32 *out) {
    /* adresse IP ecrite directement ? */
    int dots = 0, digits = 1;
    for (const char *p = host; *p; p++) {
        if (*p == '.') dots++;
        else if (!isdigit((unsigned char)*p)) digits = 0;
    }
    if (digits && dots == 3) { *out = inet_addr(host); return 1; }

    u32 servers[3];
    int ns = 0;
    if (gw_str[0]) servers[ns++] = inet_addr(gw_str);
    servers[ns++] = inet_addr("1.1.1.1");
    servers[ns++] = inet_addr("8.8.8.8");
    for (int i = 0; i < ns; i++)
        if (dns_query(servers[i], host, out)) return 1;
    return 0;
}

// ---------- Connexion http:// ou https:// ----------
/* Horloge en millisecondes demandee par mbedTLS. */
mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t)now_ms(); }

/* Source d'entropie pour le chiffrement. La GameCube n'a pas de generateur
   aleatoire materiel : on mesure la gigue entre l'horloge du processeur et des
   boucles de duree variable (cache, interruptions, DMA audio/video, reseau).
   Suffisant pour ecouter une radio ; ne pas s'en servir pour des donnees sensibles. */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    static u32 pool = 0x9E3779B9u;
    for (size_t i = 0; i < len; i++) {
        u32 acc = 0;
        for (int k = 0; k < 8; k++) {
            u64 t0 = gettime();
            volatile u32 spin = (u32)(t0 & 0x3F) + 16;
            while (spin--) pool = pool * 1664525u + 1013904223u + (u32)gettime();
            u64 t1 = gettime();
            acc = (acc << 1 | acc >> 31) ^ (u32)(t1 - t0) ^ (u32)t1 ^ retrace_count ^ (u32)f_count;
        }
        pool ^= acc;
        output[i] = (unsigned char)(acc ^ (acc >> 8) ^ (acc >> 16) ^ (acc >> 24) ^ pool);
    }
    *olen = len;
    return 0;
}

/* Une connexion = une socket + (en https) son propre contexte TLS. Plusieurs
   connexions peuvent etre ouvertes en meme temps : radio, page web, telechargement. */
typedef struct {
    s32 fd;
    int tls;
    int ssl_inited;
    mbedtls_ssl_context ssl;
} conn_t;
static conn_t cn = { -1, 0, 0 };          /* radio */

static int tls_ready = 0;                 /* 1 = generateur et certificats prets */
static mbedtls_entropy_context tls_entropy;
static mbedtls_ctr_drbg_context tls_drbg;
static mbedtls_x509_crt tls_ca;
static mbedtls_ssl_config tls_conf;

/* Les certificats sont verifies (chaine + nom du serveur), mais on ignore leurs
   dates de validite : l'horloge de la GameCube est souvent fausse. */
static int tls_verify_cb(void *p, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void)p; (void)crt; (void)depth;
    *flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
    return 0;
}

static int tls_global_init(void) {
    if (tls_ready) return 1;
    static const char pers[] = "GC-MP3 radio";
    mbedtls_entropy_init(&tls_entropy);
    mbedtls_ctr_drbg_init(&tls_drbg);
    mbedtls_x509_crt_init(&tls_ca);
    mbedtls_ssl_config_init(&tls_conf);
    if (psa_crypto_init() != PSA_SUCCESS) return 0;
    if (mbedtls_ctr_drbg_seed(&tls_drbg, mbedtls_entropy_func, &tls_entropy,
                              (const unsigned char *)pers, sizeof(pers) - 1) != 0) return 0;
    /* liste Mozilla : on garde les certificats lisibles (le resultat > 0 indique
       seulement combien n'ont pas pu etre lus) */
    if (mbedtls_x509_crt_parse(&tls_ca, (const unsigned char *)cacert_pem, sizeof(cacert_pem)) < 0) return 0;
    if (mbedtls_ssl_config_defaults(&tls_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) return 0;
    mbedtls_ssl_conf_authmode(&tls_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls_conf, &tls_ca, NULL);
    mbedtls_ssl_conf_rng(&tls_conf, mbedtls_ctr_drbg_random, &tls_drbg);
    mbedtls_ssl_conf_verify(&tls_conf, tls_verify_cb, NULL);
    tls_ready = 1;
    return 1;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    s32 r = net_send(*(s32 *)ctx, buf, (s32)len, 0);
    if (r == -EAGAIN || r == -EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)r;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    s32 r = net_recv(*(s32 *)ctx, buf, (s32)len, 0);
    if (r == -EAGAIN || r == -EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
    return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)r;     /* 0 = connexion fermee */
}

static void conn_close(conn_t *c) {
    if (c->ssl_inited) { mbedtls_ssl_free(&c->ssl); c->ssl_inited = 0; }
    if (c->fd >= 0) net_close(c->fd);
    c->fd = -1;
    c->tls = 0;
}

/* Recoit sans bloquer : > 0 octets lus, 0 fin du flux, -EAGAIN rien pour l'instant, < 0 erreur. */
static int conn_recv(conn_t *c, unsigned char *buf, int len) {
    if (c->fd < 0) return -EIO;
    if (!c->tls) return net_recv(c->fd, buf, len, 0);
    for (;;) {
        int r = mbedtls_ssl_read(&c->ssl, buf, (size_t)len);
        if (r > 0) return r;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return -EAGAIN;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;   /* message TLS 1.3 sans donnees */
#endif
        if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        return -EIO;
    }
}

static int conn_send_all(conn_t *c, const void *data, int len) {
    const unsigned char *p = (const unsigned char *)data;
    u64 t0 = now_ms();
    while (len > 0) {
        int r = c->tls ? mbedtls_ssl_write(&c->ssl, p, (size_t)len) : net_send(c->fd, p, len, 0);
        if (r > 0) { p += r; len -= r; continue; }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE || r == -EAGAIN || r == -EWOULDBLOCK) {
            if (now_ms() - t0 > 8000) return 0;
            bg_tick();
            usleep(2000);
            continue;
        }
        return 0;
    }
    return 1;
}

/* Negociation TLS avec le serveur (socket deja connectee, non bloquante).
   En cas d'echec, *err recoit le message a afficher. */
static int conn_start_tls(conn_t *c, const char *host, const char **err) {
    if (!tls_global_init()) { *err = "Erreur d'initialisation HTTPS"; return 0; }
    mbedtls_ssl_init(&c->ssl);
    c->ssl_inited = 1;
    if (mbedtls_ssl_setup(&c->ssl, &tls_conf) != 0 || mbedtls_ssl_set_hostname(&c->ssl, host) != 0) {
        *err = "Erreur d'initialisation HTTPS";
        return 0;
    }
    mbedtls_ssl_set_bio(&c->ssl, &c->fd, bio_send, bio_recv, NULL);
    c->tls = 1;
    u64 t0 = now_ms();
    for (;;) {
        int r = mbedtls_ssl_handshake(&c->ssl);
        if (r == 0) return 1;
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED || mbedtls_ssl_get_verify_result(&c->ssl) != 0)
                *err = "Certificat du serveur refusé";
            else
                *err = "Échec de la connexion sécurisée";
            return 0;
        }
        if (now_ms() - t0 > 15000) { *err = "Connexion sécurisée trop lente"; return 0; }
        bg_tick();
        usleep(1000);
    }
}

static void ui_status(const char *text);   /* redessine l'ecran pendant la connexion */

/* Ouvre une connexion TCP (et TLS si tls = 1) vers host:port.
   Retourne 1 si c'est bon ; sinon 0 et *err = message. *refused = 1 si le
   serveur n'accepte pas la connexion (utile pour reessayer en http://). */
static int conn_open(conn_t *c, const char *host, int port, int tls, const char **err, int *refused) {
    char msg[200];
    if (refused) *refused = 0;
    conn_close(c);
    snprintf(msg, sizeof(msg), "Recherche de %s...", host);
    ui_status(msg);
    u32 addr;
    if (!resolve_host(host, &addr)) { *err = "Serveur introuvable (DNS)"; return 0; }

    snprintf(msg, sizeof(msg), "Connexion à %s...", host);
    ui_status(msg);
    s32 s = net_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { *err = "Erreur réseau (socket)"; return 0; }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u16)port);
    sa.sin_addr.s_addr = addr;
    if (net_connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        net_close(s);
        *err = "Connexion refusée par le serveur";
        if (refused) *refused = 1;
        return 0;
    }
    c->fd = s;
    c->tls = 0;
    set_nonblock(s, 1);
    if (tls) {
        snprintf(msg, sizeof(msg), "Connexion sécurisée à %s...", host);
        ui_status(msg);
        if (!conn_start_tls(c, host, err)) { conn_close(c); return 0; }
    }
    return 1;
}

// ---------- HTTP + metadonnees ICY ----------
static int stream_closed = 0;
static int icy_metaint = 0, icy_count = 0, icy_state = 0, icy_mlen = 0, icy_mpos = 0;
static char icy_meta[16 * 255 + 1];
static int reconnects = 0;
static u64 last_data_ms = 0;
static u64 connect_ms = 0;            /* debut de la derniere connexion */

static void parse_icy_title(void) {
    const char *p = strstr(icy_meta, "StreamTitle='");
    if (!p) return;
    p += 13;
    const char *e = strstr(p, "';");
    if (!e) e = p + strlen(p);
    int n = (int)(e - p);
    if (n > (int)sizeof(now_title) - 1) n = sizeof(now_title) - 1;
    memcpy(now_title, p, n);
    now_title[n] = 0;
}

/* Separe le son des metadonnees ICY (inserees toutes les icy_metaint octets). */
static void stream_feed(const unsigned char *d, int n) {
    while (n > 0) {
        if (icy_metaint == 0) { cbuf_append(d, n); return; }
        if (icy_state == 0) {
            int take = icy_metaint - icy_count;
            if (take > n) take = n;
            cbuf_append(d, take);
            d += take; n -= take; icy_count += take;
            if (icy_count >= icy_metaint) { icy_state = 1; icy_count = 0; }
        } else if (icy_state == 1) {
            icy_mlen = d[0] * 16;
            icy_mpos = 0;
            d++; n--;
            icy_state = icy_mlen ? 2 : 0;
        } else {
            int take = icy_mlen - icy_mpos;
            if (take > n) take = n;
            memcpy(icy_meta + icy_mpos, d, take);
            icy_mpos += take; d += take; n -= take;
            if (icy_mpos >= icy_mlen) {
                icy_meta[icy_mlen] = 0;
                parse_icy_title();
                icy_state = 0;
            }
        }
    }
}

static void stream_close(void) {
    conn_close(&cn);
}

/* Recoit avec un delai maximum (pendant la connexion). */
static int recv_timeout(unsigned char *buf, int len, u32 ms) {
    u64 t0 = now_ms();
    while (now_ms() - t0 < ms) {
        s32 r = conn_recv(&cn, buf, len);
        if (r > 0) return r;
        if (r == 0) return 0;                                  /* connexion fermee */
        if (r != -EAGAIN && r != -EWOULDBLOCK) return -1;
        bg_tick();
        usleep(2000);
    }
    return -2;                                                 /* delai depasse */
}

static const char *header_value(char *hdr, const char *name) {
    size_t nl = strlen(name);
    for (char *p = hdr; *p; ) {
        char *eol = strstr(p, "\r\n");
        if (!eol) break;
        if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            char *v = p + nl + 1;
            while (*v == ' ') v++;
            static char val[300];
            int n = (int)(eol - v);
            if (n > (int)sizeof(val) - 1) n = sizeof(val) - 1;
            memcpy(val, v, n);
            val[n] = 0;
            return val;
        }
        p = eol + 2;
    }
    return NULL;
}

/* Cherche la premiere adresse http(s) dans une liste .m3u / .pls. */
static int playlist_first_url(const char *body, char *out, int outsz) {
    const char *p = body;
    while (*p) {
        const char *h = NULL;
        const char *a = strstr(p, "http://"), *b = strstr(p, "https://");
        if (a && (!b || a < b)) h = a; else h = b;
        if (!h) return 0;
        int n = (int)strcspn(h, "\r\n \t\"'<>");
        if (n > 10 && n < outsz) {
            memcpy(out, h, n);
            out[n] = 0;
            return 1;
        }
        p = h + 1;
    }
    return 0;
}

static void set_error(const char *msg) {
    snprintf(err_msg, sizeof(err_msg), "%s", msg);
    state = ST_ERROR;
    stream_close();
}

/* Ouvre un flux http:// ou https://. Suit les redirections et les listes .m3u/.pls. */
static int http_open(const char *url_in, int depth) {
    char url[300], host[128], path[256], req[600];
    static unsigned char hdr[8192];
    int tls = 0, port;

    if (depth > 5) { set_error("Trop de redirections"); return 0; }
    snprintf(url, sizeof(url), "%s", url_in);
    const char *h;
    if (strncasecmp(url, "https://", 8) == 0) { tls = 1; h = url + 8; }
    else if (strncasecmp(url, "http://", 7) == 0) { h = url + 7; }
    else { set_error("Adresse invalide (http:// ou https://)"); return 0; }
    port = tls ? 443 : 80;

    int hl = (int)strcspn(h, ":/?");
    if (hl <= 0 || hl >= (int)sizeof(host)) { set_error("Adresse invalide"); return 0; }
    memcpy(host, h, hl);
    host[hl] = 0;
    const char *rest = h + hl;
    if (*rest == ':') { port = atoi(rest + 1); rest += strcspn(rest, "/?"); }
    if (*rest == '/') snprintf(path, sizeof(path), "%s", rest);
    else snprintf(path, sizeof(path), "/%s", rest);
    if (port <= 0 || port > 65535) port = tls ? 443 : 80;

    const char *cerr = NULL;
    if (!conn_open(&cn, host, port, tls, &cerr, NULL)) { set_error(cerr); return 0; }

    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: GC-MP3/1.0 (GameCube)\r\n"
                      "Icy-MetaData: 1\r\nAccept: */*\r\nConnection: close\r\n\r\n", path, host);
    if (!conn_send_all(&cn, req, rl)) { set_error("Erreur d'envoi de la requête"); return 0; }

    /* lecture des en-tetes */
    int hlen = 0, body_off = -1;
    while (hlen < (int)sizeof(hdr) - 1) {
        int r = recv_timeout(hdr + hlen, (int)sizeof(hdr) - 1 - hlen, 8000);
        if (r <= 0) { set_error(r == -2 ? "Le serveur ne répond pas" : "Connexion coupée"); return 0; }
        hlen += r;
        hdr[hlen] = 0;
        char *e = strstr((char *)hdr, "\r\n\r\n");
        if (e) { body_off = (int)(e - (char *)hdr) + 4; e[2] = 0; break; }
        char *e2 = strstr((char *)hdr, "\n\n");                /* serveurs approximatifs */
        if (e2) { body_off = (int)(e2 - (char *)hdr) + 2; e2[1] = 0; break; }
    }
    if (body_off < 0) { set_error("Réponse du serveur invalide"); return 0; }

    int code = 0;
    if (sscanf((char *)hdr, "HTTP/%*d.%*d %d", &code) != 1 && sscanf((char *)hdr, "ICY %d", &code) != 1) {
        set_error("Réponse du serveur invalide");
        return 0;
    }
    char *hdrs = (char *)hdr;
    int body_len = hlen - body_off;
    unsigned char *body = hdr + body_off;

    if (code >= 300 && code < 400) {
        const char *loc = header_value(hdrs, "Location");
        if (!loc) { set_error("Redirection sans adresse"); return 0; }
        char next[300];
        if (loc[0] == '/') snprintf(next, sizeof(next), "%s://%s:%d%s", tls ? "https" : "http", host, port, loc);
        else snprintf(next, sizeof(next), "%s", loc);
        stream_close();
        return http_open(next, depth + 1);
    }
    if (code != 200) {
        char m[64];
        snprintf(m, sizeof(m), "Erreur du serveur (HTTP %d)", code);
        set_error(m);
        return 0;
    }

    const char *ct = header_value(hdrs, "Content-Type");
    char ctype[96] = "";
    if (ct) {
        snprintf(ctype, sizeof(ctype), "%s", ct);
        for (char *p = ctype; *p; p++) *p = (char)tolower((unsigned char)*p);
    }
    int is_playlist = strstr(ctype, "mpegurl") || strstr(ctype, "scpls") || strstr(ctype, "x-pls") ||
                      (body_len >= 7 && (memcmp(body, "#EXTM3U", 7) == 0 || memcmp(body, "[playli", 7) == 0));
    if (!is_playlist) {
        const char *lp = path + strlen(path);
        while (lp > path && *lp != '.') lp--;
        if (strncasecmp(lp, ".m3u", 4) == 0 || strncasecmp(lp, ".pls", 4) == 0) is_playlist = 1;
    }
    if (is_playlist) {
        /* petite liste de lecture : on lit tout et on suit la premiere adresse */
        static char pl[8192];
        int pn = body_len < (int)sizeof(pl) - 1 ? body_len : (int)sizeof(pl) - 1;
        memcpy(pl, body, pn);
        while (pn < (int)sizeof(pl) - 1) {
            int r = recv_timeout((unsigned char *)pl + pn, (int)sizeof(pl) - 1 - pn, 5000);
            if (r <= 0) break;
            pn += r;
        }
        pl[pn] = 0;
        char next[300];
        stream_close();
        if (!playlist_first_url(pl, next, sizeof(next))) { set_error("Liste de lecture vide"); return 0; }
        return http_open(next, depth + 1);
    }
    if (strstr(ctype, "aac") || strstr(ctype, "mp4") || strstr(ctype, "ogg") ||
        strstr(ctype, "opus") || strstr(ctype, "flac")) {
        set_error("Format non géré (MP3 uniquement)");
        return 0;
    }
    if (strstr(ctype, "text/html")) { set_error("Ce n'est pas un flux audio (page web)"); return 0; }

    const char *mi = header_value(hdrs, "icy-metaint");
    icy_metaint = mi ? atoi(mi) : 0;
    if (icy_metaint < 0 || icy_metaint > 1000000) icy_metaint = 0;
    icy_count = icy_state = icy_mlen = icy_mpos = 0;
    const char *nm = header_value(hdrs, "icy-name");
    snprintf(icy_name, sizeof(icy_name), "%s", nm ? nm : "");
    const char *br = header_value(hdrs, "icy-br");
    stream_kbps = br ? atoi(br) : 0;

    stream_feed(body, body_len);
    return 1;
}

static void cover_for_station(int idx);

static void stop_station(void) {
    stream_close();
    state = ST_IDLE;
    if (src == SRC_RADIO) src = SRC_NONE;
    f_head = f_tail = f_count = 0;
    cb_start = cb_end = 0;
}

static void start_station(int idx, int is_reconnect) {
    if (idx < 0 || idx >= nstations) return;
    if (src == SRC_FILE) stop_track();
    stop_station();
    src = SRC_RADIO;
    cur_station = idx;
    if (!is_reconnect) {
        reconnects = 0;
        rebuffers = 0;
        consumed = 0;
        now_title[0] = 0;
        cover_for_station(idx);
    }
    err_msg[0] = 0;
    icy_name[0] = 0;
    stream_kbps = stream_hz = stream_ch = 0;
    stream_closed = 0;
    mp3dec_init(&dec);
    rpos = 0.0; prevL = prevR = 0;
    state = ST_CONNECT;
    if (net_phase != NET_DONE) { set_error("Réseau en cours de connexion..."); return; }
    if (!net_ok) { set_error("Pas de réseau (Broadband Adapter)"); return; }
    if (http_open(stations[idx].url, 0)) {
        state = ST_BUFFER;
        last_data_ms = connect_ms = now_ms();
    }
}

/* Reception sans bloquer. Appelee souvent (y compris pendant l'attente de
   l'ecran) : la fenetre TCP de libogc ne fait qu'un paquet, chaque lecture
   rapide permet au serveur d'envoyer le paquet suivant plus tot. */
static void stream_recv(void) {
    static unsigned char tmp[4096];
    if (state != ST_BUFFER && state != ST_PLAY) return;
    int got = 0;
    while (cn.fd >= 0 && !stream_closed && cbuf_free() > (int)sizeof(tmp) && got < 64 * 1024) {
        s32 r = conn_recv(&cn, tmp, sizeof(tmp));
        if (r > 0) { stream_feed(tmp, r); got += r; last_data_ms = now_ms(); continue; }
        if (r == -EAGAIN || r == -EWOULDBLOCK) break;
        stream_closed = 1;                                     /* fin ou erreur */
        stream_close();
    }
}

/* Appele a chaque tour de boucle : recoit, decode, gere le tampon. */
static void stream_pump(void) {
    if (state != ST_BUFFER && state != ST_PLAY) return;

    /* 1. reception (sans bloquer) */
    stream_recv();
    if (!stream_closed && now_ms() - last_data_ms > 10000) {   /* plus rien depuis 10 s */
        stream_closed = 1;
        stream_close();
    }

    /* 2. decodage */
    int k = 0;
    while (k < 6 && FIFO_SIZE - f_count >= FIFO_MARGIN && decode_stream_one(stream_closed)) {
        service_audio();
        k++;
    }

    /* Une connexion stable depuis 2 min remet le compteur de reconnexions a zero :
       une coupure de temps en temps ne doit jamais arreter la radio. */
    if (state == ST_PLAY && reconnects > 0 && now_ms() - connect_ms > 120000) reconnects = 0;

    /* 3. tampon */
    if (state == ST_BUFFER && (f_count >= PREBUFFER || (stream_closed && cbuf_avail() == 0 && f_count > 0)))
        state = ST_PLAY;
    else if (state == ST_PLAY && f_count == 0) {
        if (stream_closed && cbuf_avail() < MP3_MIN_BYTES) {
            if (reconnects < 3) {                              /* coupure : on se reconnecte */
                reconnects++;
                start_station(cur_station, 1);
            } else {
                set_error("Flux interrompu");
            }
        } else {
            state = ST_BUFFER;
            rebuffers++;
        }
    } else if (state == ST_BUFFER && stream_closed && cbuf_avail() < MP3_MIN_BYTES && f_count == 0) {
        if (reconnects < 3) { reconnects++; start_station(cur_station, 1); }
        else set_error("Flux interrompu ou illisible");
    }
}

// ---------- Liste des fichiers ----------
/* names[] contient le chemin complet de chaque MP3 : sd:/mp3/... puis sd:/downloads/... */
static char names[MAX_FILES][NAME_LEN];
static int  dur_cache[MAX_FILES];          // duree en s ; -2 = pas encore lue, -1 = inconnue
static int  nfiles = 0;
static int  dur_budget = 0;                // lectures de duree autorisees pour l'image en cours

/* Nom du fichier seul (apres le dernier '/') */
static const char *base_of(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

/* Tri : d'abord sd:/mp3, puis sd:/downloads ; dans chaque dossier, ordre alphabetique
   (les morceaux d'un meme album restent donc groupes). */
static int cmp_names(const void *a, const void *b) {
    const char *x = (const char *)a, *y = (const char *)b;
    int rx = strncasecmp(x, MP3_DIR "/", strlen(MP3_DIR) + 1) == 0 ? 0 : 1;
    int ry = strncasecmp(y, MP3_DIR "/", strlen(MP3_DIR) + 1) == 0 ? 0 : 1;
    if (rx != ry) return rx - ry;
    return strcasecmp(x, y);
}

static int is_dir(const char *path, const struct dirent *e) {
#ifdef DT_DIR
    if (e->d_type == DT_DIR) return 1;
    if (e->d_type != DT_UNKNOWN) return 0;
#endif
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void scan_dir(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char path[NAME_LEN];
    while ((e = readdir(d)) != NULL && nfiles < MAX_FILES) {
        if (e->d_name[0] == '.') continue;                 /* ".", "..", fichiers caches, "._xxx" */
        int n = snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (n <= 0 || n >= (int)sizeof(path)) continue;    /* chemin trop long */
        size_t l = strlen(e->d_name);
        if (l > 4 && strcasecmp(e->d_name + l - 4, ".mp3") == 0) {
            strcpy(names[nfiles++], path);
        } else if (depth < SCAN_DEPTH && is_dir(path, e)) {
            scan_dir(path, depth + 1);
        }
    }
    closedir(d);
}

static void scan_files(void) {
    nfiles = 0;
    for (int i = 0; i < MAX_FILES; i++) dur_cache[i] = -2;
    scan_dir(MP3_DIR, 0);
    scan_dir(DL_DIR, 0);
    qsort(names, nfiles, NAME_LEN, cmp_names);
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
        dur_budget--;
        dur_cache[k] = mp3_duration(names[k]);
    }
    return dur_cache[k];
}


// ---------- Lecture des fichiers MP3 (onglet MUSIQUE) ----------
static unsigned char *fdata = NULL;
static long fsize = 0, fpos = 0;
static int playing = 0, eof = 0, cur_track = -1;
static const char *track_err = NULL;   // message si un fichier ne peut pas etre lu
static char artist[64];                // artiste lu dans les etiquettes ID3 (UTF-8)

static void decode_file_one(void) {
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

// ---------- Pochettes des MP3 ----------
/* Retire la "desynchronisation" ID3 (FF 00 -> FF). Retourne la nouvelle taille. */
static long id3_unsync(unsigned char *d, long n) {
    long o = 0;
    for (long i = 0; i < n; i++) {
        d[o++] = d[i];
        if (d[i] == 0xFF && i + 1 < n && d[i + 1] == 0x00) i++;
    }
    return o;
}

/* Pochette integree (cadre APIC / PIC), lue sans charger tout le MP3. */
static int cover_from_id3(const char *path) {
    unsigned char hdr[10];
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    if (fread(hdr, 1, 10, fp) != 10 || memcmp(hdr, "ID3", 3) != 0 || hdr[3] < 2 || hdr[3] > 4) {
        fclose(fp);
        return 0;
    }
    long tsz = (long)synchsafe(hdr + 6);
    if (tsz <= 10 || tsz > COVER_MAX_FILE + 65536) { fclose(fp); return 0; }
    unsigned char *tag = (unsigned char *)malloc(tsz);
    if (!tag) { fclose(fp); return 0; }
    if (fread(tag, 1, tsz, fp) != (size_t)tsz) { fclose(fp); free(tag); return 0; }
    fclose(fp);

    int ver = hdr[3];
    int fhdr = (ver == 2) ? 6 : 10;
    long pos = 0;
    if ((hdr[5] & 0x80) && ver < 4) tsz = id3_unsync(tag, tsz);
    if (ver >= 3 && (hdr[5] & 0x40) && tsz >= 4)
        pos = (ver == 4) ? (long)synchsafe(tag) : (long)be32(tag) + 4;

    const unsigned char *best = NULL;
    long best_len = 0;
    int best_front = 0;
    while (pos + fhdr <= tsz) {
        unsigned char *f = tag + pos;
        if (f[0] == 0) break;
        long sz = (ver == 2) ? (long)(((u32)f[3] << 16) | ((u32)f[4] << 8) | f[5])
                             : (ver == 4 ? (long)synchsafe(f + 4) : (long)be32(f + 4));
        if (sz <= 0 || pos + fhdr + sz > tsz) break;
        int is_pic = (ver == 2) ? (memcmp(f, "PIC", 3) == 0) : (memcmp(f, "APIC", 4) == 0);
        if (is_pic) {
            unsigned char *b = f + fhdr;
            long bl = sz;
            if (ver == 4 && (f[9] & 0x01) && bl > 4) { b += 4; bl -= 4; }   // taille d'origine
            if (ver == 4 && (f[9] & 0x02)) bl = id3_unsync(b, bl);
            int enc = b[0];
            long i = 1;
            if (ver == 2) i += 3;                                          // format "JPG"/"PNG"
            else { while (i < bl && b[i]) i++; i++; }                      // type MIME
            int ptype = (i < bl) ? b[i] : 0;
            i++;
            if (enc == 1 || enc == 2) {                                    // description UTF-16
                while (i + 1 < bl && (b[i] || b[i + 1])) i += 2;
                i += 2;
            } else {
                while (i < bl && b[i]) i++;
                i++;
            }
            if (i < bl && (!best || (ptype == 3 && !best_front))) {
                best = b + i;
                best_len = bl - i;
                best_front = (ptype == 3);                                 // 3 = couverture avant
            }
        }
        pos += fhdr + sz;
    }
    int ok = best ? cover_from_memory(best, best_len) : 0;
    free(tag);
    return ok;
}

static void load_cover(int idx) {
    static const char *exts[] = { ".jpg", ".jpeg", ".png" };
    static const char *common[] = { "cover.jpg", "folder.jpg", "cover.png", "folder.png" };
    char noext[NAME_LEN], dir[NAME_LEN], path[NAME_LEN + 16];

    /* les images sont cherchees dans le dossier du MP3 (un dossier par album) */
    cover_ok = 0;
    snprintf(noext, sizeof(noext), "%s", names[idx]);
    size_t l = strlen(noext);
    if (l > 4) noext[l - 4] = 0;                                           // retire ".mp3"
    snprintf(dir, sizeof(dir), "%s", names[idx]);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;

    for (int i = 0; i < 3 && !cover_ok; i++) {
        snprintf(path, sizeof(path), "%s%s", noext, exts[i]);
        cover_ok = cover_from_file(path);
    }
    if (!cover_ok) cover_ok = cover_from_id3(names[idx]);
    for (int i = 0; i < 4 && !cover_ok; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, common[i]);
        cover_ok = cover_from_file(path);
    }
}

static void stop_track(void) {
    if (fdata) { free(fdata); fdata = NULL; }
    if (src == SRC_FILE) src = SRC_NONE;
    playing = 0;
    paused = 0;
    f_head = f_tail = f_count = 0;
}

static int start_track(int idx) {
    if (idx < 0 || idx >= nfiles) return 0;
    stop_track();
    stop_station();
    track_err = NULL;
    artist[0] = 0;
    muted = 0;
    load_cover(idx);          /* avant le MP3 : la memoire du decodage est deja liberee */

    FILE *fp = fopen(names[idx], "rb");
    if (!fp) { track_err = "Ouverture impossible"; return 0; }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); track_err = "Lecture impossible"; return 0; }
    long size = ftell(fp);
    if (size <= 0) { fclose(fp); track_err = "Fichier vide"; return 0; }
    if (size > MAX_FILE_SIZE) { fclose(fp); track_err = "Fichier trop gros (16 Mo max)"; return 0; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); track_err = "Lecture impossible"; return 0; }
    fdata = (unsigned char *)malloc(size);
    if (!fdata) { fclose(fp); track_err = "M\xC3\xA9moire insuffisante"; return 0; }
    if (fread(fdata, 1, size, fp) != (size_t)size) {
        fclose(fp); free(fdata); fdata = NULL;
        track_err = "Lecture impossible";
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
    eof = 0; playing = 1; paused = 0; cur_track = idx;
    src = SRC_FILE;
    return 1;
}


// ---------- GX : initialisation (sequence des exemples officiels libogc) ----------
static GXTexObj font_texobj;
static GXTexObj cover_texobj;
static int gx_mode = -1;                 // 0 = couleurs, 1 = texte (police), 2 = pochette
#define NSEG 40
static f32 seg_c[NSEG + 1], seg_s[NSEG + 1];

/* ---------- Diagnostic GX ----------
   Toutes les attentes GX ont un delai maximum : si le processeur graphique ne
   repond plus, on affiche ou ca bloque au lieu de rester fige. */
static volatile int gx_done_flag = 0;
static int ui_started = 0;          /* 1 des que l'ecran affiche des images GX */

static void gx_done_cb(void) { gx_done_flag = 1; }

static void say(const char *fmt, const char *arg) {
    printf(fmt, arg);
    fflush(stdout);
}

static void gx_fatal(const char *where) {
    u8 overhi = 0, underlow = 0, readidle = 0, cmdidle = 0, brkpt = 0;
    GX_GetGPStatus(&overhi, &underlow, &readidle, &cmdidle, &brkpt);
    if (ui_started) {          /* l'ecran montrait l'interface : on revient a la console */
        VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
        CON_Init(xfb[0], 20, 20, rmode->fbWidth - 40, rmode->xfbHeight - 40,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
        VIDEO_SetNextFramebuffer(xfb[0]);
        VIDEO_Flush();
    }
    printf("BLOQUE\n\n   ERREUR : GX ne repond plus (%s)\n", where);
    printf("   GP : overhi=%d underlow=%d readIdle=%d cmdIdle=%d brkpt=%d\n",
           overhi, underlow, readidle, cmdidle, brkpt);
    printf("\n   Prends une photo de cet ecran.\n   START = quitter\n");
    fflush(stdout);
    while (1) {
        PAD_ScanPads();
        if (PAD_ButtonsDown(0) & PAD_BUTTON_START) exit(0);
        VIDEO_WaitVSync();
    }
}

/* Equivalent de GX_DrawDone(), mais abandonne apres timeout_ms. */
static int gx_finish(u32 timeout_ms) {
    gx_done_flag = 0;
    GX_SetDrawDone();
    u64 t0 = gettime();
    while (!gx_done_flag) {
        if (ticks_to_millisecs(gettime() - t0) > timeout_ms) return 0;
    }
    return 1;
}

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

static void gx_set_cover(void) {
    if (gx_mode == 2) return;
    gx_mode = 2;
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP1, GX_COLOR0A0);
    /* couleur = texture ; alpha = alpha du sommet */
    GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
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

    say("   GX_Init... %s", "");
    gx_fifo = memalign(32, GX_FIFO_SIZE);
    if (gx_fifo == NULL) { say("%s\n   ERREUR : memoire insuffisante pour GX\n", "ECHEC"); while (1) VIDEO_WaitVSync(); }
    memset(gx_fifo, 0, GX_FIFO_SIZE);
    /* La FIFO est ecrite par le "write gather pipe", hors cache : on vide le cache
       pour qu'aucune ligne restee du memset n'ecrase plus tard des commandes GX. */
    DCFlushRange(gx_fifo, GX_FIFO_SIZE);
    GX_Init(gx_fifo, GX_FIFO_SIZE);
    GX_SetDrawDoneCallback(gx_done_cb);
    say("%s\n", "OK");
    say("   Reglages GX... %s", "");

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

    say("%s\n", "OK");
    say("   Textures... %s", "");
    /* Texture de police (I4 : l'alpha vaut l'intensite). */
    DCFlushRange((void *)font_tex, sizeof(font_tex));
    GX_InitTexObj(&font_texobj, (void *)font_tex, FONT_TEX_W, FONT_TEX_H,
                  GX_TF_I4, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&font_texobj, GX_LINEAR, GX_LINEAR);
    GX_LoadTexObj(&font_texobj, GX_TEXMAP0);

    /* Texture de pochette (RGB565 128x128, mise a jour a chaque piste). */
    DCFlushRange(cover_tex, sizeof(cover_tex));
    GX_InitTexObj(&cover_texobj, cover_tex, COVER_SZ, COVER_SZ,
                  GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&cover_texobj, GX_LINEAR, GX_LINEAR);
    GX_LoadTexObj(&cover_texobj, GX_TEXMAP1);
    GX_InvalidateTexAll();

    for (int i = 0; i <= NSEG; i++) {
        seg_c[i] = cosf(2.0f * PI_F * (float)i / (float)NSEG);
        seg_s[i] = sinf(2.0f * PI_F * (float)i / (float)NSEG);
    }
    gx_mode = -1;
    gx_set_flat();
    GX_InvVtxCache();
    say("%s\n", "OK");
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
    VIDEO_SetPostRetraceCallback(retrace_cb);
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
    if (!gx_finish(1500)) gx_fatal("dessin d'une image");
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(xfb[next_fb], GX_TRUE);
    if (!gx_finish(1500)) gx_fatal("copie de l'image vers l'ecran");
    VIDEO_SetNextFramebuffer(xfb[next_fb]);
    VIDEO_Flush();
    /* attente de l'image suivante, en continuant a lire le reseau */
    u32 rc = retrace_count;
    u64 t0 = now_ms();
    while (retrace_count == rc && now_ms() - t0 < 40) {
        stream_recv();
        service_audio();
        usleep(500);
    }
    displayed_fb = next_fb;
    ui_started = 1;
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

static void draw_panel(f32 x, f32 y, f32 w, f32 h) {
    gx_glow(x, y, w, h, 10, C_GLOW, 4);
    gx_rrect_fill(x, y, w, h, 10, C_PANEL_TOP, C_PANEL_BOT);
    gx_rrect_stroke(x, y, w, h, 10, 1.5f, C_BORDER);
}

static void draw_round_button(f32 cx, f32 cy, f32 r) {
    gx_circle(cx, cy, r, RGBA(36, 22, 86, 235), RGBA(16, 10, 44, 240));
    gx_circle_glow(cx, cy, r, RGBA(130, 70, 255, 110), 3);
    gx_ring(cx, cy, r - 1.5f, r, C_BORDER);
}

static void draw_badge(f32 cx, f32 cy, const char *letter, col_t c1, col_t c2) {
    gx_circle(cx, cy, 8, c1, c2);
    gx_ring(cx, cy, 7, 8, RGBA(255, 255, 255, 80));
    gx_text_c(F_SMALL, cx, cy + 4.5f, letter, RGBA(255, 255, 255, 255));
}

/* Legende des touches : icones + textes repartis sur toute la largeur du cadre. */
static void begin_frame(void) {
    GX_SetViewport(0.0f, 0.0f, rmode->fbWidth, rmode->efbHeight, 0.0f, 1.0f);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    draw_background();
}

// ---------- Interface GC-Radio ----------
static int sel_station = 0, sel_track = 0;
static char conn_msg[160];

static void cover_for_station(int idx) {
    static const char *exts[] = { ".jpg", ".jpeg", ".png" };
    char path[128];
    cover_ok = 0;
    if (!sd_ok) return;
    for (int i = 0; i < 3 && !cover_ok; i++) {
        snprintf(path, sizeof(path), "%s/%s%s", LOGO_DIR, stations[idx].name, exts[i]);
        cover_ok = cover_from_file(path);
    }
}

static void draw_header(void) {
    draw_logo_cube(54, 38, 23);
    gx_text(F_LOGO, 88, 46, "GC-MP3", RGBA(240, 232, 255, 255), RGBA(150, 84, 255, 255), 0);
    gx_text(F_SMALL, 91, 63, "INDIGO AUDIO PLAYER", RGBA(170, 150, 225, 255), RGBA(170, 150, 225, 255), 4);

    gx_hgrad(330, 46, 140, 1, RGBA(110, 70, 255, 0), RGBA(110, 70, 255, 200));
    for (int i = 0; i < 28; i++) {
        f32 env = (i < 20) ? 0.15f + 0.85f * (f32)i / 19.0f : 1.0f - (f32)(i - 20) / 10.0f;
        f32 v = bar_val[(i * 2) % NBARS];
        f32 h = 2.0f + 22.0f * env * (0.30f + 0.70f * v);
        gx_rect(392.0f + i * 3.0f, 46.0f - h, 1.5f, 2.0f * h,
                RGBA(150 + i * 3, 90, 255, 90 + (int)(120 * env)));
    }
    if (net_phase == NET_DONE) {
        char ip[40];
        if (net_ok) snprintf(ip, sizeof(ip), "IP %s", ip_str);
        else snprintf(ip, sizeof(ip), "HORS LIGNE");
        gx_text_r(F_SMALL, 470, 66, ip, net_ok ? RGBA(120, 105, 190, 255) : RGBA(255, 140, 90, 255));
    }
    draw_console_corner();
}

/* petite icone "radio" (antenne + poste) */
static void draw_radio_icon(f32 cx, f32 cy, col_t c) {
    gx_rrect_stroke(cx - 6, cy - 2, 12, 8, 1.5f, 1.2f, c);
    gx_circle(cx + 2.5f, cy + 2, 1.8f, c, c);
    gx_rect(cx - 4, cy + 1, 3, 1, c);
    gx_rect(cx - 4, cy + 3, 3, 1, c);
    gx_poly4(cx - 4, cy - 2, cx + 3, cy - 8, cx + 4, cy - 7, cx - 3, cy - 2, c, c, c, c);
}

/* Onglets MUSIQUE / RADIO / WEB en haut de la liste (Z pour changer).
   Retourne la position x apres le rappel de la touche Z. */
static int draw_tabs(const char *count) {
    static const char *names[3] = { "MUSIQUE", "RADIO", "WEB" };
    f32 x = 20;
    for (int t = 0; t < 3; t++) {
        int w = text_width(F_SMALL, names[t], 1) + 14;
        int on = (tab == t);
        if (on) {
            gx_glow(x, 86, (f32)w, 18, 9, RGBA(150, 80, 255, 110), 2);
            gx_rrect_fill(x, 86, (f32)w, 18, 9, RGBA(124, 54, 240, 255), RGBA(80, 26, 180, 255));
        }
        gx_rrect_stroke(x, 86, (f32)w, 18, 9, 1.2f, on ? RGBA(190, 120, 255, 255) : RGBA(90, 64, 170, 255));
        gx_text(F_SMALL, x + 7, 99, names[t], on ? RGBA(255, 255, 255, 255) : RGBA(150, 135, 210, 255),
                on ? RGBA(255, 255, 255, 255) : RGBA(150, 135, 210, 255), 1);
        x += w + 4;
    }
    /* rappel de la touche Z */
    gx_rrect_fill(x + 1, 88, 14, 14, 4, RGBA(110, 90, 190, 255), RGBA(70, 50, 140, 255));
    gx_text_c(F_SMALL, x + 8, 99, "Z", RGBA(255, 255, 255, 255));
    if (count && x + 20 + text_width(F_SMALL, count, 0) <= 266)
        gx_text_r(F_SMALL, 266, 99, count, RGBA(205, 195, 245, 255));
    gx_rect(25, 110, tab == TAB_WEB ? 590 : 241, 1, RGBA(76, 52, 150, 200));
    return (int)x + 16;
}

static void draw_list_radio(void) {
    int start = sel_station - LIST_ROWS / 2;
    if (start > nstations - LIST_ROWS) start = nstations - LIST_ROWS;
    if (start < 0) start = 0;
    char buf[160], t[16];

    draw_panel(11, 73, 271, 385);
    snprintf(buf, sizeof(buf), "%d", nstations);
    draw_tabs(buf);

    for (int i = 0; i < LIST_ROWS; i++) {
        int k = start + i;
        if (k >= nstations) break;
        f32 ry = 114.0f + i * 24.0f;
        f32 cy = ry + 12.0f;
        int is_sel = (k == sel_station);
        int is_cur = (k == cur_station && state != ST_IDLE);

        if (is_sel) {
            gx_glow(20, ry + 1, 244, 22, 5, RGBA(150, 80, 255, 120), 3);
            gx_rrect_fill(20, ry + 1, 244, 22, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
            gx_rrect_stroke(20, ry + 1, 244, 22, 5, 1.2f, RGBA(180, 104, 255, 255));
        } else if (i + 1 < LIST_ROWS && k + 1 < nstations && k + 1 != sel_station) {
            gx_rect(25, ry + 24, 241, 1, RGBA(60, 40, 120, 110));
        }
        col_t dim = is_sel ? RGBA(235, 225, 255, 255) : RGBA(128, 112, 190, 255);
        col_t txt = is_sel ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);

        if (is_cur) gx_tri(31, cy - 6, 31, cy + 6, 41, cy, RGBA(255, 255, 255, 255));
        else draw_radio_icon(36, cy, is_sel ? RGBA(235, 225, 255, 255) : RGBA(150, 120, 235, 255));

        snprintf(t, sizeof(t), "%02d", k + 1);
        gx_text1(F_SMALL, 50, cy + 4, t, dim);

        int name_w = 256 - 76;
        if (is_cur) {
            name_w = 236 - 8 - 76;
            if (state == ST_PLAY) {
                for (int j = 0; j < 3; j++) {
                    f32 bh = 3.0f + (muted ? 1.0f : 8.0f * bar_val[3 + j * 9]);
                    gx_rect(246.0f + j * 4.0f, cy + 5.0f - bh, 2.5f, bh, RGBA(255, 255, 255, 255));
                }
            } else if (state == ST_ERROR) {
                gx_text_r(F_SMALL, 256, cy + 4, "!", RGBA(255, 150, 80, 255));
            } else {                                          /* connexion / tampon : 3 points */
                int ph = (int)((now_ms() / 250) % 3);
                for (int j = 0; j < 3; j++)
                    gx_rect(244.0f + j * 5.0f, cy + 2, 3, 3, j == ph ? RGBA(255, 255, 255, 255) : RGBA(150, 130, 220, 255));
            }
        }
        char nm[64];
        fit_text(F_MED, stations[k].name, name_w, nm, sizeof(nm));
        gx_text1(F_MED, 76, cy + 5, nm, txt);
    }

    if (nstations == 0)
        gx_text1(F_MED, 30, 138, "Aucune station", RGBA(255, 205, 110, 255));

    if (nstations > LIST_ROWS) {
        f32 track_h = 330.0f;
        f32 th = track_h * LIST_ROWS / nstations;
        if (th < 18.0f) th = 18.0f;
        f32 ty = 116.0f + (track_h - th) * (f32)start / (f32)(nstations - LIST_ROWS);
        gx_rrect_fill(270, 116, 3, track_h, 1.5f, RGBA(60, 40, 130, 130), RGBA(60, 40, 130, 130));
        gx_rrect_fill(270, ty, 3, th, 1.5f, RGBA(150, 96, 255, 255), RGBA(110, 60, 230, 255));
    }
}

static void draw_art_radio(void) {
    const f32 x = 303, y = 107, sz = 120;
    int live = (state == ST_PLAY);

    /* ondes radio derriere le visuel */
    const f32 vx = x + sz - 22, vy = y + sz / 2;
    for (int i = 0; i < 4; i++) {
        f32 r = 26.0f + i * 10.0f + (live ? (f32)((now_ms() / 40) % 10) : 0.0f);
        u8 a = (u8)(live ? 150 - i * 30 : 70 - i * 12);
        gx_ring(vx, vy, r - 1.5f, r, RGBA(120, 80, 255, a));
    }

    if (cover_ok && cur_station >= 0 && state != ST_IDLE) {
        gx_set_cover();
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(x, y);           GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(x + sz, y);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(x + sz, y + sz); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(x, y + sz);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f);
        GX_End();
        gx_rrect_stroke(x, y, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 235));
        return;
    }

    /* visuel par defaut : poste de radio retro */
    gx_rrect_fill(x, y, sz, sz, 3, RGBA(36, 18, 96, 255), RGBA(12, 7, 40, 255));
    f32 bx = x + 14, by = y + 34, bw = sz - 28, bh = 66;
    gx_poly4(x + 34, by, x + 80, y + 12, x + 82, y + 14, x + 37, by, RGBA(200, 170, 255, 255),
             RGBA(200, 170, 255, 255), RGBA(200, 170, 255, 255), RGBA(200, 170, 255, 255));
    gx_circle(x + 81, y + 13, 3, RGBA(240, 230, 255, 255), RGBA(200, 170, 255, 255));
    gx_rrect_fill(bx, by, bw, bh, 8, RGBA(130, 70, 245, 255), RGBA(72, 30, 170, 255));
    gx_rrect_stroke(bx, by, bw, bh, 8, 1.5f, RGBA(200, 160, 255, 255));
    gx_circle(bx + 26, by + 34, 20, RGBA(40, 20, 100, 255), RGBA(24, 12, 64, 255));
    for (int r = 6; r <= 18; r += 6) gx_ring(bx + 26, by + 34, r - 1.0f, (f32)r, RGBA(150, 110, 255, 200));
    gx_rrect_fill(bx + 52, by + 12, 32, 14, 3, RGBA(20, 10, 56, 255), RGBA(20, 10, 56, 255));
    gx_rect(bx + 58 + (f32)((live ? now_ms() / 60 : 8) % 20), by + 13, 2, 12, RGBA(255, 120, 160, 255));
    gx_circle(bx + 60, by + 46, 7, RGBA(230, 215, 255, 255), RGBA(150, 110, 240, 255));
    gx_circle(bx + 78, by + 46, 7, RGBA(230, 215, 255, 255), RGBA(150, 110, 240, 255));
    gx_rrect_stroke(x, y, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 235));
}

/* Coupe un texte en deux lignes de largeur maxw (au dernier espace possible). */
static void wrap2(int f, const char *s, int maxw, char *a, char *b, int sz) {
    a[0] = b[0] = 0;
    if (text_width(f, s, 0) <= maxw) { snprintf(a, sz, "%s", s); return; }
    int cut = -1;
    for (int i = 0; s[i] && i < sz - 1; i++) {
        if (s[i] != ' ') continue;
        char tmp[200];
        int n = i < (int)sizeof(tmp) - 1 ? i : (int)sizeof(tmp) - 1;
        memcpy(tmp, s, n); tmp[n] = 0;
        if (text_width(f, tmp, 0) <= maxw) cut = i; else break;
    }
    if (cut < 0) { fit_text(f, s, maxw, a, sz); return; }
    memcpy(a, s, cut); a[cut] = 0;
    fit_text(f, s + cut + 1, maxw, b, sz);
}

static void fmt_listen(char *buf, int sec) {
    if (sec >= 3600) snprintf(buf, 16, "%d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    else fmt_time(buf, sec);
}

static void draw_player_radio(void) {
    char buf[200], l1[160], l2[160];
    draw_panel(289, 73, 341, 338);

    int live = (state == ST_PLAY);
    if (live) {
        int blink = (int)((now_ms() / 600) % 2);
        gx_circle(312, 94, 4, blink ? RGBA(255, 70, 100, 255) : RGBA(200, 40, 70, 255), RGBA(170, 30, 60, 255));
    }
    gx_text(F_SMALL, live ? 322 : 308, 98, "EN DIRECT", RGBA(140, 122, 210, 255), RGBA(140, 122, 210, 255), 3);
    snprintf(buf, sizeof(buf), "%02d / %02d", cur_station >= 0 ? cur_station + 1 : 0, nstations);
    gx_text_r(F_MED, 614, 99, buf, RGBA(165, 145, 235, 255));

    draw_art_radio();

    /* nom de la station + titre en cours (ICY "Artiste - Titre") */
    if (cur_station >= 0 && state == ST_ERROR) {
        fit_text(F_LARGE, stations[cur_station].name, 160, buf, sizeof(buf));
        gx_text1(F_LARGE, 456, 132, buf, RGBA(255, 255, 255, 255));
        wrap2(F_MED, err_msg, 160, l1, l2, sizeof(l1));
        gx_text1(F_MED, 456, 152, l1, RGBA(255, 150, 80, 255));
        gx_text1(F_MED, 456, 170, l2, RGBA(255, 150, 80, 255));
    } else if (cur_station >= 0 && state != ST_IDLE) {
        fit_text(F_LARGE, stations[cur_station].name, 160, buf, sizeof(buf));
        gx_text1(F_LARGE, 456, 132, buf, RGBA(255, 255, 255, 255));
        const char *dash = strstr(now_title, " - ");
        if (now_title[0] && dash) {
            int n = (int)(dash - now_title);
            if (n > (int)sizeof(l1) - 1) n = sizeof(l1) - 1;
            memcpy(l1, now_title, n); l1[n] = 0;
            snprintf(l2, sizeof(l2), "%s", dash + 3);
        } else {
            snprintf(l1, sizeof(l1), "%s", now_title[0] ? now_title : icy_name);
            l2[0] = 0;
        }
        fit_text(F_MED, l1, 160, buf, sizeof(buf));
        gx_text1(F_MED, 456, 152, buf, RGBA(156, 116, 255, 255));
        fit_text(F_MED, l2, 160, buf, sizeof(buf));
        gx_text1(F_MED, 456, 170, buf, RGBA(225, 215, 250, 255));
    } else {
        gx_text1(F_LARGE, 456, 132, "Aucune station", RGBA(228, 218, 255, 255));
        gx_text1(F_MED, 456, 152, "Choisis une radio", RGBA(156, 116, 255, 255));
        gx_text1(F_MED, 456, 170, "et appuie sur A", RGBA(180, 165, 235, 255));
    }

    /* etat */
    col_t sc = RGBA(180, 160, 240, 255);
    switch (state) {
    case ST_CONNECT: snprintf(buf, sizeof(buf), "%s", conn_msg[0] ? conn_msg : "Connexion..."); break;
    case ST_BUFFER: snprintf(buf, sizeof(buf), "Mise en tampon... %d %%", f_count * 100 / PREBUFFER); break;
    case ST_PLAY:
        if (stream_kbps > 0 && stream_hz > 0)
            snprintf(buf, sizeof(buf), "%d kbps  %d,%d kHz%s", stream_kbps, stream_hz / 1000, (stream_hz % 1000) / 100,
                     muted ? "  (muet)" : "");
        else snprintf(buf, sizeof(buf), "En direct%s", muted ? "  (muet)" : "");
        break;
    case ST_ERROR: snprintf(buf, sizeof(buf), "A : réessayer"); break;
    default:
        if (net_phase == NET_BUSY) snprintf(buf, sizeof(buf), "Connexion au réseau...");
        else if (net_phase == NET_DONE && !net_ok) { snprintf(buf, sizeof(buf), "Pas de réseau (Broadband Adapter)"); sc = RGBA(255, 150, 80, 255); }
        else snprintf(buf, sizeof(buf), "Prêt");
        break;
    }
    char fb[200];
    fit_text(F_SMALL, buf, 170, fb, sizeof(fb));
    gx_text1(F_SMALL, 456, 192, fb, sc);

    /* jauge du tampon */
    const f32 px = 457, py = 203, pw = 156;
    f32 frac = (f32)f_count / (f32)FIFO_SIZE;
    if (state == ST_BUFFER) frac = (f32)f_count / (f32)PREBUFFER;
    if (frac > 1.0f) frac = 1.0f;
    gx_rrect_fill(px, py, pw, 3, 1.5f, RGBA(70, 50, 140, 230), RGBA(70, 50, 140, 230));
    if (state == ST_PLAY || state == ST_BUFFER) {
        if (frac > 0.0f) gx_hgrad(px, py, pw * frac, 3, RGBA(120, 64, 255, 255), RGBA(180, 100, 255, 255));
        char tb[16], lb[64];
        fmt_listen(tb, (int)(consumed / 48000u));
        snprintf(lb, sizeof(lb), "Tampon %d,%d s", f_count / 48000, (f_count % 48000) / 4800);
        gx_text1(F_SMALL, 457, 222, lb, RGBA(130, 115, 200, 255));
        gx_text_r(F_SMALL, 613, 222, tb, RGBA(130, 115, 200, 255));
    }

    /* egaliseur (spectre reel) + reflet */
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

    /* boutons : station precedente, ecouter/arreter, station suivante, son */
    const f32 cy = 364.0f;
    col_t icon = RGBA(230, 220, 255, 255);
    draw_round_button(347, cy, 21);
    draw_round_button(499, cy, 21);
    draw_round_button(567, cy, 21);
    gx_tri(337, cy, 346, cy - 7, 346, cy + 7, icon);
    gx_tri(346, cy, 355, cy - 7, 355, cy + 7, icon);
    gx_tri(509, cy, 500, cy - 7, 500, cy + 7, icon);
    gx_tri(500, cy, 491, cy - 7, 491, cy + 7, icon);
    /* haut-parleur (barre si son coupe) */
    gx_rect(557, cy - 4, 5, 8, icon);
    gx_poly4(562, cy - 4, 569, cy - 9, 569, cy + 9, 562, cy + 4, icon, icon, icon, icon);
    if (muted) {
        gx_poly4(572, cy - 6, 574, cy - 6, 580, cy + 6, 578, cy + 6, RGBA(255, 110, 130, 255),
                 RGBA(255, 110, 130, 255), RGBA(255, 110, 130, 255), RGBA(255, 110, 130, 255));
        gx_poly4(578, cy - 6, 580, cy - 6, 574, cy + 6, 572, cy + 6, RGBA(255, 110, 130, 255),
                 RGBA(255, 110, 130, 255), RGBA(255, 110, 130, 255), RGBA(255, 110, 130, 255));
    } else {
        gx_rect(573, cy - 3, 2, 6, icon);
        gx_rect(577, cy - 6, 2, 12, icon);
    }

    gx_circle_glow(423, cy, 29, RGBA(170, 90, 255, 150), 5);
    gx_circle(423, cy, 29, RGBA(150, 78, 255, 255), RGBA(92, 36, 206, 255));
    gx_ring(423, cy, 27.5f, 29, RGBA(210, 140, 255, 255));
    if (state == ST_IDLE || state == ST_ERROR)
        gx_tri(417, cy - 11, 417, cy + 11, 434, cy, RGBA(255, 255, 255, 255));
    else
        gx_rrect_fill(414, cy - 9, 18, 18, 3, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
}



/* Legende des touches, selon l'onglet affiche. */
static void draw_legend(void) {
    const char *labels[5];
    labels[0] = "LIRE";
    labels[1] = (tab == TAB_MUSIC) ? "PAUSE" : "MUET";
    labels[2] = "STOP";
    labels[3] = "ONGLET";
    labels[4] = "QUITTER";
    static const f32 icon_w[5] = { 16, 16, 16, 16, 20 };
    const f32 x0 = 289, w = 341, pad = 10, gap = 4, cy = 438.5f;
    col_t tx = RGBA(208, 198, 240, 255);

    gx_rrect_fill(x0, 419, w, 39, 9, C_PANEL_TOP, C_PANEL_BOT);
    gx_rrect_stroke(x0, 419, w, 39, 9, 1.2f, RGBA(100, 56, 220, 255));

    f32 total = 0;
    for (int i = 0; i < 5; i++) total += icon_w[i] + gap + text_width(F_SMALL, labels[i], 0);
    f32 space = (w - 2 * pad - total) / 4.0f;
    f32 x = x0 + pad;
    for (int i = 0; i < 5; i++) {
        f32 cx = x + icon_w[i] / 2;
        switch (i) {
        case 0: draw_badge(cx, cy, "A", RGBA(80, 210, 100, 255), RGBA(30, 140, 60, 255)); break;
        case 1: draw_badge(cx, cy, "Y", RGBA(140, 90, 255, 255), RGBA(84, 40, 204, 255)); break;
        case 2: draw_badge(cx, cy, "B", RGBA(236, 70, 90, 255), RGBA(170, 30, 50, 255)); break;
        case 3:
            gx_rrect_fill(cx - 8, cy - 7, 16, 14, 4, RGBA(110, 90, 190, 255), RGBA(70, 50, 140, 255));
            gx_text_c(F_SMALL, cx, cy + 4.5f, "Z", RGBA(255, 255, 255, 255));
            break;
        default:
            gx_rrect_fill(x, cy - 6.5f, 20, 13, 6.5f, RGBA(140, 140, 165, 255), RGBA(84, 84, 110, 255));
            gx_text_c(F_SMALL, cx, cy + 4.0f, "ST", RGBA(255, 255, 255, 255));
            break;
        }
        x += icon_w[i] + gap;
        x += gx_text1(F_SMALL, x, cy + 4.5f, labels[i], tx) + space;
    }
}

static void draw_list_music(int sel);
static void draw_player_music(void);

static void draw_web(void);
static void draw_legend_web(void);
static void draw_keyboard(void);
static int kb_is_open(void);

static void draw(void) {
    dur_budget = 1;
    begin_frame();
    draw_header();
    if (tab == TAB_WEB) {
        draw_web();
        draw_legend_web();
        if (kb_is_open()) draw_keyboard();
        service_audio();
        present_frame();
        return;
    }
    if (tab == TAB_MUSIC) draw_list_music(sel_track);
    else draw_list_radio();
    /* panneau de droite : ce qui joue, sinon l'onglet affiche */
    int view = (src != SRC_NONE) ? src : (tab == TAB_RADIO ? SRC_RADIO : SRC_FILE);
    if (view == SRC_RADIO) draw_player_radio();
    else draw_player_music();
    draw_legend();
    service_audio();
    present_frame();
}

static void ui_status(const char *text) {
    snprintf(conn_msg, sizeof(conn_msg), "%s", text);
    update_spectrum();
    draw();
}

static void draw_message(const char *l1, const char *l2, const char *l3) {
    begin_frame();
    draw_header();
    draw_panel(60, 150, 520, 170);
    gx_text_c(F_LARGE, 320, 207, l1, RGBA(255, 255, 255, 255));
    if (l2) gx_text_c(F_MED, 320, 245, l2, RGBA(195, 180, 242, 255));
    if (l3) gx_text_c(F_MED, 320, 283, l3, RGBA(156, 116, 255, 255));
    present_frame();
}

// ---------- Interface : onglet MUSIQUE ----------
/* Nom a afficher : sans le numero de piste en tete du nom de fichier
   ("01. Stan.mp3", "01 - Stan.mp3", "01_Stan.mp3" -> "Stan.mp3"). */
static const char *display_name(const char *n) {
    int d = 0;
    while (d < 3 && n[d] >= '0' && n[d] <= '9') d++;
    if (d == 0 || d > 2) return n;             /* 1 ou 2 chiffres seulement */
    const char *p = n + d;
    while (*p == ' ') p++;
    if (*p != '.' && *p != '-' && *p != '_') return n;   /* "50 Cent" reste intact */
    p++;
    while (*p == ' ' || *p == '-' || *p == '_') p++;
    if (strlen(p) <= 4) return n;              /* il ne resterait que ".mp3" */
    return p;
}

static void draw_note(f32 cx, f32 cy, col_t c) {
    gx_ellipse(cx - 2.0f, cy + 5.0f, 3.4f, 2.6f, c, c);
    gx_rect(cx + 0.6f, cy - 6.0f, 1.6f, 11.0f, c);
    gx_tri(cx + 0.6f, cy - 6.0f, cx + 5.5f, cy - 3.0f, cx + 0.6f, cy - 1.0f, c);
}

static void draw_list_music(int sel) {
    int start = sel - LIST_ROWS / 2;
    if (start > nfiles - LIST_ROWS) start = nfiles - LIST_ROWS;
    if (start < 0) start = 0;
    char buf[160], t[16];

    draw_panel(11, 73, 271, 385);
    snprintf(buf, sizeof(buf), "%d", nfiles);
    draw_tabs(buf);

    for (int i = 0; i < LIST_ROWS; i++) {
        int k = start + i;
        if (k >= nfiles) break;
        f32 ry = 114.0f + i * 24.0f;
        f32 cy = ry + 12.0f;
        int is_sel = (k == sel);
        int is_cur = (playing && k == cur_track);

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
        fit_text(F_MED, display_name(base_of(names[k])), name_w, nm, sizeof(nm));
        gx_text1(F_MED, 76, cy + 5, nm, txt);
    }

    if (nfiles == 0) {
        gx_text1(F_MED, 30, 138, "Aucun fichier MP3 trouv\xC3\xA9", RGBA(255, 205, 110, 255));
        gx_text1(F_SMALL, 30, 160, "Copie tes MP3 dans SD:/mp3", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 30, 178, "ou tÃ©lÃ©" "charge-les dans SD:/downloads", RGBA(190, 180, 235, 255));
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

static void draw_art_music(void) {
    const f32 x = 303, y = 107, sz = 120;

    /* disque vinyle qui depasse derriere la pochette */
    const f32 vx = x + sz - 28, vy = y + sz / 2, vr = 56;
    gx_circle(vx, vy, vr, RGBA(28, 24, 48, 255), RGBA(8, 6, 18, 255));
    gx_ring(vx, vy, vr - 9, vr - 8, RGBA(70, 60, 120, 110));
    gx_ring(vx, vy, vr - 16, vr - 15, RGBA(70, 60, 120, 90));
    gx_ring(vx, vy, vr - 23, vr - 22, RGBA(70, 60, 120, 80));
    gx_ring(vx, vy, vr - 1.5f, vr, RGBA(120, 70, 255, 170));

    if (playing && cover_ok) {
        /* vraie pochette */
        gx_set_cover();
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(x, y);           GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(x + sz, y);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(x + sz, y + sz); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(x, y + sz);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f);
        GX_End();
        gx_rrect_stroke(x, y, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 235));
        return;
    }

    /* pochette par defaut : lune sur un lac */
    const f32 hz = y + 78, mx = x + sz / 2, my = y + 52;
    gx_vgrad(x, y, sz, hz - y, RGBA(8, 6, 34, 255), RGBA(74, 32, 156, 255));
    static const u8 stars[12][2] = {{12,10},{34,24},{58,8},{84,18},{106,12},{20,42},
                                    {100,38},{74,32},{44,50},{112,54},{8,62},{66,60}};
    for (int i = 0; i < 12; i++)
        gx_rect(x + stars[i][0], y + stars[i][1], 1.3f, 1.3f, RGBA(225, 215, 255, 170));
    gx_circle(mx, my, 38, RGBA(150, 100, 255, 80), RGBA(150, 100, 255, 0));
    gx_circle(mx, my, 23, RGBA(240, 232, 255, 255), RGBA(150, 96, 235, 255));
    gx_ring(mx, my, 22, 23.5f, RGBA(200, 170, 255, 255));
    gx_tri(x, hz + 1, x + 28, hz - 22, x + 60, hz + 1, RGBA(22, 12, 60, 255));
    gx_tri(x + 34, hz + 1, x + 76, hz - 14, x + sz, hz + 1, RGBA(16, 8, 46, 255));
    gx_tri(x + 72, hz + 1, x + 100, hz - 24, x + sz, hz - 7, RGBA(26, 14, 70, 255));
    gx_vgrad(x, hz, sz, y + sz - hz, RGBA(46, 20, 112, 255), RGBA(10, 6, 34, 255));
    gx_ellipse(mx, hz + 18, 24, 13, RGBA(190, 140, 255, 130), RGBA(120, 70, 220, 0));
    for (int i = 0; i < 6; i++)
        gx_rect(x + 22 + i * 4, hz + 6 + i * 5, 76 - i * 8, 1, RGBA(205, 175, 255, 70));
    gx_rrect_stroke(x, y, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 235));
}

static void draw_player_music(void) {
    char buf[160], t1[16], t2[16];
    draw_panel(289, 73, 341, 338);

    gx_text(F_SMALL, 308, 98, "NOW PLAYING", RGBA(140, 122, 210, 255), RGBA(140, 122, 210, 255), 3);
    snprintf(buf, sizeof(buf), "%02d / %02d", (playing && cur_track >= 0) ? cur_track + 1 : 0, nfiles);
    gx_text_r(F_MED, 614, 99, buf, RGBA(165, 145, 235, 255));

    draw_art_music();

    int dur = playing ? track_duration(cur_track) : -1;
    int elapsed = (int)(consumed / 48000u);
    if (playing && cur_track >= 0 && cur_track < nfiles) {
        fit_text(F_LARGE, display_name(base_of(names[cur_track])), 160, buf, sizeof(buf));
        gx_text1(F_LARGE, 456, 140, buf, RGBA(255, 255, 255, 255));
        if (track_err) {
            gx_text1(F_MED, 456, 160, track_err, RGBA(255, 150, 80, 255));
        } else if (artist[0]) {
            fit_text(F_MED, artist, 160, buf, sizeof(buf));
            gx_text1(F_MED, 456, 160, buf, RGBA(156, 116, 255, 255));
        }
    } else {
        gx_text1(F_LARGE, 456, 140, "Aucune piste", RGBA(228, 218, 255, 255));
        if (track_err) {
            fit_text(F_MED, track_err, 160, buf, sizeof(buf));
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

// ====================================================================
// ---------- Navigateur web (onglet WEB) ----------
// Navigateur en mode texte : HTML sans JavaScript ni images. Les liens vers
// des fichiers (MP3...) sont telecharges dans sd:/downloads.
// ====================================================================
#define URL_MAX        1024
#define WEB_PAGE_MAX   (1536 * 1024)        // taille maxi d'une page HTML
#define FAV_FILE       SD_ROOT "/favoris.txt"
#define SEARCH_URL     "https://www.mojeek.com/search?q="
#define HIST_MAX       20
#define WV_X           26                   // zone d'affichage de la page
#define WV_Y           116
#define WV_W           586
#define WV_H           272
#define W_MAX_ITEMS    60000
#define W_MAX_LINKS    12000
#define W_MAX_FIELDS   64
#define W_MAX_FORMS    16

enum { WS_LINK = 1, WS_HEAD = 2, WS_BOLD = 4, WS_DIM = 8, WS_FIELD = 16, WS_BUTTON = 32, WS_HR = 64 };
enum { LK_HREF, LK_INPUT, LK_SUBMIT };
enum { FD_TEXT, FD_HIDDEN, FD_SUBMIT };

typedef struct { u32 off; s32 y; s16 x; u16 w; s16 link; u8 font; u8 style; } witem_t;
typedef struct { u32 url; s32 y0, y1; s16 field; u8 kind; s8 form; } wlink_t;
typedef struct { s8 form; u8 type; u32 name; char value[256]; } wfield_t;
typedef struct { u32 action; u8 post; } wform_t;

typedef struct {
    char *text;  u32 tlen, tcap;            // textes affiches (chaines terminees par 0)
    char *pool;  u32 plen, pcap;            // adresses des liens, noms des champs
    witem_t *items; int nitems, icap;
    wlink_t *links; int nlinks, lcap;
    wfield_t *fields; int nfields;
    wform_t forms[W_MAX_FORMS]; int nforms;
    s32 height;
    int truncated;
    char url[URL_MAX];
    char title[160];
    char refresh[URL_MAX];
    int refresh_delay;
} wpage_t;

static wpage_t pg;                          // page affichee
static s32 pg_scroll = 0;
static int pg_sel = -1;                     // lien selectionne

static void wpage_free(wpage_t *p) {
    free(p->text); free(p->pool); free(p->items); free(p->links); free(p->fields);
    memset(p, 0, sizeof(*p));
}

/* agrandit un tableau dynamique ; 0 si plus de memoire ou limite atteinte */
static int grow(void **buf, int *cap, int need, int elsz, int maxn) {
    if (need <= *cap) return 1;
    if (need > maxn) return 0;
    int nc = *cap ? *cap * 2 : 256;
    while (nc < need) nc *= 2;
    if (nc > maxn) nc = maxn;
    void *nb = realloc(*buf, (size_t)nc * elsz);
    if (!nb) return 0;
    *buf = nb;
    *cap = nc;
    return 1;
}

static u32 pool_add(wpage_t *p, const char *s) {
    int n = (int)strlen(s) + 1, cap = (int)p->pcap;
    if (!grow((void **)&p->pool, &cap, (int)p->plen + n, 1, 4 * 1024 * 1024)) { p->truncated = 1; return 0; }
    p->pcap = (u32)cap;
    u32 off = p->plen;
    memcpy(p->pool + off, s, n);
    p->plen += n;
    return off;
}

// ---------- Adresses (URL) ----------
static int hexv(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(const char *in, char *out, int outsz, int plus_space) {
    int n = 0;
    while (*in && n < outsz - 1) {
        if (*in == '%' && hexv(in[1]) >= 0 && hexv(in[2]) >= 0) {
            out[n++] = (char)(hexv(in[1]) * 16 + hexv(in[2]));
            in += 3;
        } else if (*in == '+' && plus_space) { out[n++] = ' '; in++; }
        else out[n++] = *in++;
    }
    out[n] = 0;
}

/* encode pour une requete (formulaire) : espace -> '+', le reste en %XX */
static int url_encode(const char *in, char *out, int outsz) {
    static const char hx[] = "0123456789ABCDEF";
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        if (n > outsz - 4) return 0;
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') out[n++] = (char)*p;
        else if (*p == ' ') out[n++] = '+';
        else { out[n++] = '%'; out[n++] = hx[*p >> 4]; out[n++] = hx[*p & 15]; }
    }
    out[n] = 0;
    return 1;
}

typedef struct { int tls; char host[128]; int port; char path[URL_MAX]; } urlp_t;

static int url_parse(const char *url, urlp_t *u) {
    const char *h;
    if (strncasecmp(url, "https://", 8) == 0) { u->tls = 1; h = url + 8; }
    else if (strncasecmp(url, "http://", 7) == 0) { u->tls = 0; h = url + 7; }
    else return 0;
    int hl = (int)strcspn(h, ":/?#");
    if (hl <= 0 || hl >= (int)sizeof(u->host)) return 0;
    memcpy(u->host, h, hl);
    u->host[hl] = 0;
    for (char *p = u->host; *p; p++) *p = (char)tolower((unsigned char)*p);
    const char *rest = h + hl;
    u->port = u->tls ? 443 : 80;
    if (*rest == ':') { u->port = atoi(rest + 1); rest += strcspn(rest, "/?#"); }
    if (u->port <= 0 || u->port > 65535) return 0;
    int pl = (int)strcspn(rest, "#");
    if (pl >= (int)sizeof(u->path) - 2) pl = (int)sizeof(u->path) - 2;
    if (*rest == '/') { memcpy(u->path, rest, pl); u->path[pl] = 0; }
    else { u->path[0] = '/'; memcpy(u->path + 1, rest, pl); u->path[pl + 1] = 0; }
    return 1;
}

/* retire les segments "." et ".." d'un chemin (sans toucher a la requete ?...) */
static void path_normalize(char *path) {
    char *q = strchr(path, '?');
    char query[URL_MAX] = "";
    if (q) { snprintf(query, sizeof(query), "%s", q); *q = 0; }
    char out[URL_MAX];
    int n = 0;
    const char *p = path;
    while (*p) {
        const char *seg = p + 1;                     /* p pointe sur un '/' */
        int sl = (int)strcspn(seg, "/");
        int last = (seg[sl] == 0);
        if (sl == 1 && seg[0] == '.') {
            if (last) out[n++] = '/';
        } else if (sl == 2 && seg[0] == '.' && seg[1] == '.') {
            while (n > 0 && out[n - 1] != '/') n--;
            if (n > 0) n--;
            while (n > 0 && out[n - 1] != '/') n--;
            if (n > 0) n--;
            if (last) out[n++] = '/';
        } else if (n + sl + 2 < (int)sizeof(out)) {
            out[n++] = '/';
            memcpy(out + n, seg, sl);
            n += sl;
        }
        p = seg + sl;
    }
    if (n == 0) out[n++] = '/';
    out[n] = 0;
    snprintf(path, URL_MAX, "%s%s", out, query);
}

/* encode les espaces et les octets non ASCII trouves dans les liens */
static void url_clean(const char *in, char *out, int outsz) {
    static const char hx[] = "0123456789ABCDEF";
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n < outsz - 4; p++) {
        if (*p <= 0x20 || *p >= 0x7F || *p == '"' || *p == '<' || *p == '>') {
            if (*p == '\n' || *p == '\r' || *p == '\t') continue;
            out[n++] = '%'; out[n++] = hx[*p >> 4]; out[n++] = hx[*p & 15];
        } else out[n++] = (char)*p;
    }
    out[n] = 0;
}

/* Lien des resultats DuckDuckGo (//duckduckgo.com/l/?uddg=...) -> vraie adresse */
static void url_unwrap(char *url, int sz) {
    if (!strstr(url, "duckduckgo.com/l/")) return;
    const char *u = strstr(url, "uddg=");
    if (!u) return;
    char enc[URL_MAX], dec[URL_MAX];
    snprintf(enc, sizeof(enc), "%s", u + 5);
    enc[strcspn(enc, "&")] = 0;
    url_decode(enc, dec, sizeof(dec), 0);
    if (strncasecmp(dec, "http", 4) == 0) snprintf(url, sz, "%s", dec);
}

/* Adresse complete d'un lien href trouve sur la page base. 0 = lien inutilisable. */
static int url_join(const char *base, const char *href_in, char *out, int outsz) {
    char href[URL_MAX], tmp[URL_MAX * 2];
    while (*href_in == ' ' || *href_in == '\n' || *href_in == '\r' || *href_in == '\t') href_in++;
    url_clean(href_in, href, sizeof(href));
    href[strcspn(href, "#")] = 0;
    if (!href[0]) return 0;                                  /* "#..." : meme page */
    int sl = 0;
    while (isalnum((unsigned char)href[sl]) || href[sl] == '+' || href[sl] == '-' || href[sl] == '.') sl++;
    if (sl > 0 && href[sl] == ':' && isalpha((unsigned char)href[0])) {
        if (strncasecmp(href, "http:", 5) && strncasecmp(href, "https:", 6) && strncasecmp(href, "gc:", 3))
            return 0;                                        /* javascript:, mailto:, ... */
        if (strncasecmp(href, "gc:", 3) == 0) { snprintf(out, outsz, "%s", href); return 1; }
        snprintf(tmp, sizeof(tmp), "%s", href);
    } else {
        urlp_t b;
        if (!url_parse(base, &b)) return 0;
        char origin[200];
        if (b.port == (b.tls ? 443 : 80)) snprintf(origin, sizeof(origin), "%s://%s", b.tls ? "https" : "http", b.host);
        else snprintf(origin, sizeof(origin), "%s://%s:%d", b.tls ? "https" : "http", b.host, b.port);
        if (href[0] == '/' && href[1] == '/') snprintf(tmp, sizeof(tmp), "%s:%s", b.tls ? "https" : "http", href);
        else if (href[0] == '/') snprintf(tmp, sizeof(tmp), "%s%s", origin, href);
        else if (href[0] == '?') {
            b.path[strcspn(b.path, "?")] = 0;
            snprintf(tmp, sizeof(tmp), "%s%s%s", origin, b.path, href);
        } else {
            b.path[strcspn(b.path, "?")] = 0;
            char *sl2 = strrchr(b.path, '/');
            if (sl2) sl2[1] = 0;
            snprintf(tmp, sizeof(tmp), "%s%s%s", origin, b.path, href);
        }
    }
    urlp_t u;
    if (!url_parse(tmp, &u)) return 0;
    path_normalize(u.path);
    if (u.port == (u.tls ? 443 : 80)) snprintf(tmp, sizeof(tmp), "%s://%s%s", u.tls ? "https" : "http", u.host, u.path);
    else snprintf(tmp, sizeof(tmp), "%s://%s:%d%s", u.tls ? "https" : "http", u.host, u.port, u.path);
    if ((int)strlen(tmp) >= outsz) return 0;
    snprintf(out, outsz, "%s", tmp);
    url_unwrap(out, outsz);
    return 1;
}

// ---------- Caracteres : la police couvre ASCII + Latin-1 ----------
/* ecrit le caractere cp en UTF-8 (2 octets max), avec un equivalent pour les
   caracteres absents de la police (guillemets, tirets, ...) */
static int cp_out(unsigned cp, char *o) {
    const char *alt = NULL;
    if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A || cp == 0x2032) cp = '\'';
    else if (cp == 0x201C || cp == 0x201D || cp == 0x201E || cp == 0x2033) cp = '"';
    else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2015 || cp == 0x2212 || cp == 0x2010 || cp == 0x2011) cp = '-';
    else if (cp == 0x2022 || cp == 0x25CF || cp == 0x25AA || cp == 0x2219) cp = 0xB7;
    else if (cp == 0x2026) alt = "...";
    else if (cp == 0x20AC) alt = "EUR";
    else if (cp == 0x2122) alt = "TM";
    else if (cp == 0x2192 || cp == 0x203A || cp == 0x25B6 || cp == 0x25BA) alt = ">";
    else if (cp == 0x2190 || cp == 0x2039 || cp == 0x25C0) alt = "<";
    else if (cp == 0x0152) alt = "OE";
    else if (cp == 0x0153) alt = "oe";
    else if (cp == 0x2009 || cp == 0x200A || cp == 0x202F || cp == 0x2002 || cp == 0x2003) cp = ' ';
    else if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF || cp == 0xAD) return 0;
    if (alt) { int n = (int)strlen(alt); memcpy(o, alt, n); return n; }
    if (cp < 0x20) return 0;
    if (cp < 0x7F) { o[0] = (char)cp; return 1; }
    if (cp >= 0xA0 && cp <= 0xFF) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    o[0] = '?';
    return 1;
}

/* Windows-1252 (0x80-0x9F) */
static const u16 cp1252[32] = {
    0x20AC, 0x3F, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x3F, 0x017D, 0x3F,
    0x3F, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x3F, 0x017E, 0x0178 };

/* lit un caractere du texte HTML (UTF-8, ou Latin-1/Windows-1252) */
static unsigned read_cp(const unsigned char *s, int len, int *pos, int latin) {
    unsigned c = s[*pos];
    (*pos)++;
    if (c < 0x80) return c;
    if (latin) return (c < 0xA0) ? cp1252[c - 0x80] : c;
    int need = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (need < 0 || *pos + need > len) return (c < 0xA0) ? '?' : c;          /* octet isole */
    unsigned cp = c & (0x3F >> need);
    for (int i = 0; i < need; i++) {
        unsigned d = s[*pos + i];
        if ((d & 0xC0) != 0x80) return (c < 0xA0) ? '?' : c;
        cp = (cp << 6) | (d & 0x3F);
    }
    *pos += need;
    return cp;
}

static const struct { const char *n; u16 cp; } entities[] = {
    {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",0xA0},{"eacute",0xE9},{"egrave",0xE8},
    {"ecirc",0xEA},{"euml",0xEB},{"agrave",0xE0},{"acirc",0xE2},{"auml",0xE4},{"ccedil",0xE7},{"icirc",0xEE},
    {"iuml",0xEF},{"ocirc",0xF4},{"ouml",0xF6},{"ugrave",0xF9},{"ucirc",0xFB},{"uuml",0xFC},{"Eacute",0xC9},
    {"Egrave",0xC8},{"Ecirc",0xCA},{"Agrave",0xC0},{"Acirc",0xC2},{"Ccedil",0xC7},{"Ocirc",0xD4},{"aacute",0xE1},
    {"iacute",0xED},{"oacute",0xF3},{"uacute",0xFA},{"ntilde",0xF1},{"Ntilde",0xD1},{"atilde",0xE3},{"otilde",0xF5},
    {"aring",0xE5},{"oslash",0xF8},{"szlig",0xDF},{"copy",0xA9},{"reg",0xAE},{"deg",0xB0},{"laquo",0xAB},
    {"raquo",0xBB},{"middot",0xB7},{"times",0xD7},{"divide",0xF7},{"euro",0x20AC},{"hellip",0x2026},{"mdash",0x2014},
    {"ndash",0x2013},{"rsquo",0x2019},{"lsquo",0x2018},{"rdquo",0x201D},{"ldquo",0x201C},{"bull",0x2022},
    {"trade",0x2122},{"iexcl",0xA1},{"iquest",0xBF},{"sect",0xA7},{"para",0xB6},{"plusmn",0xB1},{"frac12",0xBD},
    {"frac14",0xBC},{"frac34",0xBE},{"sup2",0xB2},{"pound",0xA3},{"yen",0xA5},{"cent",0xA2},{"shy",0xAD},
    {"rarr",0x2192},{"larr",0x2190},{"oelig",0x153},{"OElig",0x152},{"thinsp",0x2009},{"ensp",0x2002},{"emsp",0x2003},
    {"zwnj",0x200C},{"zwj",0x200D},{"Uuml",0xDC},{"Ouml",0xD6},{"Auml",0xC4},{"yacute",0xFD},{"yuml",0xFF},
};

/* s[*pos] == '&' : decode une entite. Retourne le caractere (ou '&' si inconnue). */
static unsigned read_entity(const unsigned char *s, int len, int *pos) {
    int p = *pos + 1;
    if (p < len && s[p] == '#') {
        p++;
        unsigned v = 0;
        int hex = (p < len && (s[p] == 'x' || s[p] == 'X'));
        if (hex) p++;
        int st = p;
        while (p < len && p - st < 8 && (hex ? hexv(s[p]) >= 0 : isdigit(s[p]))) {
            v = v * (hex ? 16 : 10) + (unsigned)(hex ? hexv(s[p]) : s[p] - '0');
            p++;
        }
        if (p == st) { (*pos)++; return '&'; }
        if (p < len && s[p] == ';') p++;
        *pos = p;
        if (v >= 0x80 && v < 0xA0) v = cp1252[v - 0x80];
        return v ? v : '?';
    }
    char name[12];
    int n = 0;
    while (p < len && n < 10 && isalnum(s[p])) name[n++] = (char)s[p++];
    name[n] = 0;
    if (n > 0) {
        for (unsigned i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
            if (strcmp(entities[i].n, name) == 0) {
                if (p < len && s[p] == ';') p++;
                *pos = p;
                return entities[i].cp;
            }
        }
    }
    (*pos)++;
    return '&';
}

/* decode les entites d'une valeur d'attribut, resultat en UTF-8 */
static void attr_decode(const char *in, int inlen, char *out, int outsz, int latin) {
    int n = 0, pos = 0;
    const unsigned char *s = (const unsigned char *)in;
    while (pos < inlen && n < outsz - 4) {
        unsigned cp = (s[pos] == '&') ? read_entity(s, inlen, &pos) : read_cp(s, inlen, &pos, latin);
        if (cp == '\n' || cp == '\r' || cp == '\t') cp = ' ';
        if (cp > 0xFF) {
            /* hors Latin-1 : on garde l'UTF-8 d'origine (utile pour les adresses) */
            char tmp[8];
            int k = 0;
            if (cp < 0x800) { tmp[k++] = (char)(0xC0 | (cp >> 6)); }
            else if (cp < 0x10000) { tmp[k++] = (char)(0xE0 | (cp >> 12)); tmp[k++] = (char)(0x80 | ((cp >> 6) & 0x3F)); }
            else { tmp[k++] = (char)(0xF0 | (cp >> 18)); tmp[k++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                   tmp[k++] = (char)(0x80 | ((cp >> 6) & 0x3F)); }
            tmp[k++] = (char)(0x80 | (cp & 0x3F));
            if (n + k < outsz - 1) { memcpy(out + n, tmp, k); n += k; }
            continue;
        }
        if (cp < 0x80) out[n++] = (char)cp;
        else { out[n++] = (char)(0xC0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 0x3F)); }
    }
    out[n] = 0;
}

/* texte UTF-8 -> texte affichable (equivalents pour les caracteres absents de la police) */
static void text_display(const char *in, char *out, int outsz) {
    const unsigned char *s = (const unsigned char *)in;
    int len = (int)strlen(in), pos = 0, n = 0;
    while (pos < len && n < outsz - 4) {
        char tmp[4];
        int k = cp_out(read_cp(s, len, &pos, 0), tmp);
        memcpy(out + n, tmp, k);
        n += k;
    }
    out[n] = 0;
}

/* attribut d'une balise : tag = texte entre '<' et '>' */
static int tag_attr(const char *tag, int tlen, const char *name, char *out, int outsz, int latin) {
    int nl = (int)strlen(name), p = 0;
    while (p < tlen && !isspace((unsigned char)tag[p]) && tag[p] != '/') p++;   /* nom de la balise */
    while (p < tlen) {
        while (p < tlen && (isspace((unsigned char)tag[p]) || tag[p] == '/')) p++;
        int ns = p;
        while (p < tlen && !isspace((unsigned char)tag[p]) && tag[p] != '=' && tag[p] != '/') p++;
        int nlen = p - ns;
        while (p < tlen && isspace((unsigned char)tag[p])) p++;
        int vs = -1, vl = 0;
        if (p < tlen && tag[p] == '=') {
            p++;
            while (p < tlen && isspace((unsigned char)tag[p])) p++;
            if (p < tlen && (tag[p] == '"' || tag[p] == '\'')) {
                char q = tag[p++];
                vs = p;
                while (p < tlen && tag[p] != q) p++;
                vl = p - vs;
                if (p < tlen) p++;
            } else {
                vs = p;
                while (p < tlen && !isspace((unsigned char)tag[p])) p++;
                vl = p - vs;
            }
        }
        if (nlen == nl && strncasecmp(tag + ns, name, nl) == 0) {
            if (out) {
                if (vs >= 0) attr_decode(tag + vs, vl, out, outsz, latin);
                else out[0] = 0;
            }
            return 1;
        }
        if (nlen == 0 && vs < 0) p++;
    }
    return 0;
}

// ---------- Mise en page ----------
typedef struct {
    wpage_t *p;
    s32 line_top;
    int line_first;                         // premier element de la ligne en cours
    int x, indent;
    int space;                              // espace a inserer avant le prochain mot
    int brk;                                // 0 = rien, 1 = ligne, 2 = paragraphe
    int font, style, link;
    int run;                                // element ouvert (on y ajoute les mots suivants)
    int blank;                              // lignes vides consecutives
} wlay_t;

static int font_space(int f) { return font_glyphs[f][0].adv; }

static void lay_newline(wlay_t *L) {
    wpage_t *p = L->p;
    if (L->line_first < p->nitems) {
        int asc = 0, h = 0;
        for (int i = L->line_first; i < p->nitems; i++) {
            int f = p->items[i].font;
            int ih = (p->items[i].style & WS_FIELD) ? font_height[F_MED] + 6 : font_height[f];
            if (font_ascent[f] > asc) asc = font_ascent[f];
            if (ih > h) h = ih;
        }
        if (p->items[L->line_first].style & WS_HR) { asc = 6; h = 12; }
        for (int i = L->line_first; i < p->nitems; i++) {
            p->items[i].y = L->line_top + asc;
            int lk = p->items[i].link;
            if (lk >= 0) {
                if (p->links[lk].y0 < 0 || p->links[lk].y0 > L->line_top) p->links[lk].y0 = L->line_top;
                if (p->links[lk].y1 < L->line_top + h) p->links[lk].y1 = L->line_top + h;
            }
        }
        L->line_top += h + 3;
        L->blank = 0;
    }
    L->line_first = p->nitems;
    L->x = L->indent;
    L->space = 0;
    L->run = -1;
}

static void lay_break(wlay_t *L, int para) {
    lay_newline(L);
    if (para && L->brk < 2 && L->p->nitems > 0) L->line_top += 7;
    if (L->brk < (para ? 2 : 1)) L->brk = para ? 2 : 1;
}

static void lay_br(wlay_t *L) {
    if (L->line_first < L->p->nitems) lay_newline(L);
    else if (L->blank < 1) { L->line_top += font_height[L->font] + 3; L->blank++; }
    L->brk = 1;
}

static witem_t *lay_new_item(wlay_t *L, int x, int w) {
    wpage_t *p = L->p;
    if (!grow((void **)&p->items, &p->icap, p->nitems + 1, sizeof(witem_t), W_MAX_ITEMS)) { p->truncated = 1; return NULL; }
    witem_t *it = &p->items[p->nitems++];
    memset(it, 0, sizeof(*it));
    it->x = (s16)x;
    it->w = (u16)w;
    it->link = (s16)L->link;
    it->font = (u8)L->font;
    it->style = (u8)L->style;
    it->off = p->tlen;
    L->brk = 0;
    return it;
}

static int text_append(wpage_t *p, const char *s, int n, int join) {
    int cap = (int)p->tcap;
    if (!grow((void **)&p->text, &cap, (int)p->tlen + n + 2, 1, 8 * 1024 * 1024)) { p->truncated = 1; return 0; }
    p->tcap = (u32)cap;
    if (join && p->tlen > 0) p->tlen--;                     /* recolle a la chaine precedente */
    memcpy(p->text + p->tlen, s, n);
    p->tlen += n;
    p->text[p->tlen++] = 0;
    return 1;
}

/* ajoute un mot (UTF-8 affichable, n octets) */
static void lay_word(wlay_t *L, const char *w, int n) {
    if (n <= 0 || L->p->truncated) return;
    char buf[300];
    if (n > (int)sizeof(buf) - 2) n = (int)sizeof(buf) - 2;
    memcpy(buf, w, n);
    buf[n] = 0;
    int ww = text_width(L->font, buf, 0);
    int sw = (L->space && L->x > L->indent) ? font_space(L->font) : 0;
    int maxw = WV_W - L->indent;
    if (ww > maxw) {                                         /* mot plus long qu'une ligne : on le coupe */
        const char *s = buf;
        int cut = 0, wsum = 0;
        while (s[cut]) {
            const char *q = s + cut;
            int cp = next_cp(&q);
            int a = font_glyphs[L->font][glyph_index(cp)].adv;
            if (wsum + a > maxw - (L->x - L->indent) - sw && cut > 0) break;
            wsum += a;
            cut = (int)(q - s);
        }
        lay_word(L, buf, cut);
        lay_newline(L);
        lay_word(L, buf + cut, n - cut);
        return;
    }
    if (L->x + sw + ww > WV_W && L->x > L->indent) { lay_newline(L); sw = 0; }
    wpage_t *p = L->p;
    if (L->run >= 0) {
        witem_t *r = &p->items[L->run];
        if (r->font == L->font && r->style == L->style && r->link == L->link) {
            if (sw) { if (!text_append(p, " ", 1, 1)) return; }
            if (!text_append(p, buf, n, 1)) return;
            r->w = (u16)(L->x + sw + ww - r->x);
            L->x += sw + ww;
            L->space = 0;
            return;
        }
    }
    witem_t *it = lay_new_item(L, L->x + sw, ww);
    if (!it) return;
    if (!text_append(p, buf, n, 0)) { p->nitems--; return; }
    L->run = p->nitems - 1;
    L->x += sw + ww;
    L->space = 0;
}

static void lay_text(wlay_t *L, const char *s) {           /* plusieurs mots */
    while (*s) {
        while (*s == ' ') { L->space = 1; s++; }
        int n = (int)strcspn(s, " ");
        if (n) lay_word(L, s, n);
        s += n;
    }
}

/* champ de saisie ou bouton (largeur fixe) */
static void lay_box(wlay_t *L, int w, int style, const char *label) {
    int sw = (L->space && L->x > L->indent) ? font_space(L->font) : 0;
    if (w > WV_W - L->indent) w = WV_W - L->indent;
    if (L->x + sw + w > WV_W && L->x > L->indent) { lay_newline(L); sw = 0; }
    int save = L->style;
    L->style = style;
    witem_t *it = lay_new_item(L, L->x + sw, w);
    L->style = save;
    if (!it) return;
    it->font = F_MED;
    if (!text_append(L->p, label ? label : "", label ? (int)strlen(label) : 0, 0)) { L->p->nitems--; return; }
    L->x += sw + w;
    L->space = 1;
    L->run = -1;
}

static void lay_hr(wlay_t *L) {
    lay_newline(L);
    int save = L->style;
    L->style = WS_HR;
    witem_t *it = lay_new_item(L, L->indent, WV_W - L->indent);
    L->style = save;
    if (it && !text_append(L->p, "", 0, 0)) L->p->nitems--;
    lay_newline(L);
}

// ---------- Analyse du HTML ----------
static int new_link(wpage_t *p, int kind, const char *url, int field, int form) {
    if (!grow((void **)&p->links, &p->lcap, p->nlinks + 1, sizeof(wlink_t), W_MAX_LINKS)) { p->truncated = 1; return -1; }
    wlink_t *l = &p->links[p->nlinks];
    l->url = url ? pool_add(p, url) : 0;
    l->y0 = -1; l->y1 = -1;
    l->field = (s16)field;
    l->kind = (u8)kind;
    l->form = (s8)form;
    return p->nlinks++;
}

static int new_field(wpage_t *p, int form, int type, const char *name, const char *value) {
    if (!p->fields) p->fields = (wfield_t *)calloc(W_MAX_FIELDS, sizeof(wfield_t));
    if (!p->fields || p->nfields >= W_MAX_FIELDS) return -1;
    wfield_t *f = &p->fields[p->nfields];
    f->form = (s8)form;
    f->type = (u8)type;
    f->name = pool_add(p, name);
    snprintf(f->value, sizeof(f->value), "%s", value ? value : "");
    return p->nfields++;
}

static char *ci_find(char *h, const char *n) {
    int nl = (int)strlen(n);
    for (; *h; h++) if (strncasecmp(h, n, nl) == 0) return h;
    return NULL;
}

static int tag_is(const char *name, const char *const *list) {
    for (int i = 0; list[i]; i++) if (strcmp(name, list[i]) == 0) return 1;
    return 0;
}

/* cherche "</name" (sans tenir compte des majuscules) a partir de pos */
static int find_close(const unsigned char *s, int len, int pos, const char *name) {
    int nl = (int)strlen(name);
    for (int i = pos; i + nl + 2 <= len; i++)
        if (s[i] == '<' && s[i + 1] == '/' && strncasecmp((const char *)s + i + 2, name, nl) == 0) return i;
    return len;
}

/* texte brut (titre, zone de texte) : entites decodees, espaces reduits */
static void raw_text(const unsigned char *s, int a, int b, char *out, int outsz, int latin) {
    char tmp[1024], col[1024];
    int n = b - a;
    if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
    attr_decode((const char *)s + a, n, tmp, sizeof(tmp), latin);
    int k = 0, sp = 0;
    for (char *c = tmp; *c && k < (int)sizeof(col) - 2; c++) {
        if (*c == ' ') { sp = 1; continue; }
        if (sp && k > 0) col[k++] = ' ';
        sp = 0;
        col[k++] = *c;
    }
    col[k] = 0;
    text_display(col, out, outsz);
}

static void web_layout_html(wpage_t *p, const unsigned char *s, int len, int latin) {
    static const char *const para_tags[] = { "p", "ul", "ol", "dl", "table", "blockquote", "pre", "form",
        "h1", "h2", "h3", "h4", "h5", "h6", "figure", "fieldset", "details", "center", "address", NULL };
    static const char *const line_tags[] = { "div", "section", "article", "header", "footer", "nav", "main",
        "aside", "tr", "li", "dt", "dd", "figcaption", "summary", "caption", "legend", "tbody", "thead", "option", NULL };
    static const char *const hide_tags[] = { "svg", "select", "template", "iframe", "object", "canvas",
        "audio", "video", "math", "datalist", "map", NULL };

    wlay_t L;
    memset(&L, 0, sizeof(L));
    L.p = p;
    L.font = F_MED;
    L.link = -1;
    L.run = -1;
    L.brk = 2;
    char base[URL_MAX];
    snprintf(base, sizeof(base), "%s", p->url);

    char word[300];
    int wn = 0;
    int pre = 0, bold = 0, in_head = 0;
    int hide = 0, hide_n = 0;
    char hide_name[16] = "";
    int heading = 0;
    int cur_form = -1, cur_select = -1, link_items = 0, link_open = 0;
    int list_depth = 0, cell = 0;
    int ol_num[8] = { 0 }, ol_is[8] = { 0 };
    char tag[2048], name[16], val[URL_MAX], val2[URL_MAX];
    int pos = 0;
    u32 tick = 0;

#define FLUSH_WORD() do { if (wn) { lay_word(&L, word, wn); wn = 0; } } while (0)
#define SET_STYLE() (L.style = (link_open ? WS_LINK : 0) | (heading ? WS_HEAD : 0) | (bold ? WS_BOLD : 0) | \
                     (L.link >= 0 && p->links[L.link].kind == LK_SUBMIT ? WS_BUTTON : 0))

    while (pos < len && !p->truncated) {
        if ((++tick & 1023) == 0) bg_tick();
        unsigned char c = s[pos];
        if (c == '<') {
            /* commentaire */
            if (pos + 3 < len && s[pos + 1] == '!' && s[pos + 2] == '-' && s[pos + 3] == '-') {
                int e = pos + 4;
                while (e + 2 < len && !(s[e] == '-' && s[e + 1] == '-' && s[e + 2] == '>')) e++;
                pos = e + 3;
                continue;
            }
            int close = (pos + 1 < len && s[pos + 1] == '/');
            int ns = pos + 1 + close;
            if (ns >= len || !(isalpha(s[ns]) || s[ns] == '!' || s[ns] == '?')) {
                /* '<' isole : simple texte */
                if (!hide && !in_head && wn < (int)sizeof(word) - 4) word[wn++] = '<';
                pos++;
                continue;
            }
            /* fin de la balise (en sautant les guillemets) */
            int e = ns;
            char q = 0;
            while (e < len) {
                if (q) { if (s[e] == q) q = 0; }
                else if (s[e] == '"' || s[e] == '\'') { if (e > ns && s[e - 1] == '=') q = (char)s[e]; }
                else if (s[e] == '>') break;
                e++;
            }
            int tl = e - ns;
            if (tl > (int)sizeof(tag) - 1) tl = (int)sizeof(tag) - 1;
            memcpy(tag, s + ns, tl);
            tag[tl] = 0;
            pos = e + 1;
            if (tag[0] == '!' || tag[0] == '?') continue;     /* <!DOCTYPE>, <?xml?> */
            int nl = 0;
            while (nl < tl && nl < 15 && (isalnum((unsigned char)tag[nl]) || tag[nl] == '-')) {
                name[nl] = (char)tolower((unsigned char)tag[nl]);
                nl++;
            }
            name[nl] = 0;
            if (!nl) continue;

            /* contenus a sauter ou a lire en bloc */
            if (!close && (strcmp(name, "script") == 0 || strcmp(name, "style") == 0)) {
                int ce = find_close(s, len, pos, name);
                pos = ce;
                continue;
            }
            if (!close && strcmp(name, "title") == 0) {
                int ce = find_close(s, len, pos, "title");
                if (!p->title[0]) raw_text(s, pos, ce, p->title, sizeof(p->title), latin);
                pos = ce;
                continue;
            }
            if (!close && strcmp(name, "textarea") == 0) {
                int ce = find_close(s, len, pos, "textarea");
                if (!hide && cur_form >= 0) {
                    char init[256];
                    raw_text(s, pos, ce, init, sizeof(init), latin);
                    if (!tag_attr(tag, tl, "name", val, sizeof(val), latin)) val[0] = 0;
                    FLUSH_WORD();
                    int f = new_field(p, cur_form, FD_TEXT, val, init);
                    if (f >= 0) {
                        int save = L.link;
                        L.link = new_link(p, LK_INPUT, NULL, f, cur_form);
                        if (L.link >= 0) lay_box(&L, 300, WS_FIELD, NULL);
                        L.link = save;
                    }
                }
                pos = ce;
                continue;
            }
            /* <option> : valeur choisie d'une liste deroulante (meme cachee) */
            if (!close && strcmp(name, "option") == 0 && cur_select >= 0) {
                if (tag_attr(tag, tl, "value", val, sizeof(val), latin)) {
                    wfield_t *f = &p->fields[cur_select];
                    if (tag_attr(tag, tl, "selected", NULL, 0, latin) || f->type == FD_HIDDEN + 10) {
                        snprintf(f->value, sizeof(f->value), "%s", val);
                        f->type = FD_HIDDEN;
                    }
                }
                continue;
            }
            if (close && strcmp(name, "select") == 0 && cur_select >= 0) {
                if (p->fields[cur_select].type == FD_HIDDEN + 10) p->fields[cur_select].type = FD_HIDDEN;
                cur_select = -1;
            }
            /* elements caches */
            if (hide) {
                if (strcmp(name, hide_name) == 0) {
                    if (close) { if (--hide_n <= 0) { hide = 0; hide_name[0] = 0; } }
                    else hide_n++;
                }
                continue;
            }
            if (!close && tag_is(name, hide_tags)) {
                if (strcmp(name, "select") == 0 && cur_form >= 0 && tag_attr(tag, tl, "name", val, sizeof(val), latin)) {
                    cur_select = new_field(p, cur_form, FD_HIDDEN, val, "");
                    if (cur_select >= 0) p->fields[cur_select].type = FD_HIDDEN + 10;   /* 1re option a prendre */
                }
                int selfclose = (tl > 0 && tag[tl - 1] == '/');
                if (!selfclose) { hide = 1; hide_n = 1; snprintf(hide_name, sizeof(hide_name), "%s", name); }
                continue;
            }
            if (strcmp(name, "head") == 0) { in_head = !close; continue; }
            if (strcmp(name, "body") == 0) { in_head = 0; continue; }
            if (strcmp(name, "meta") == 0) {
                if (tag_attr(tag, tl, "http-equiv", val, sizeof(val), latin) && strcasecmp(val, "refresh") == 0 &&
                    tag_attr(tag, tl, "content", val, sizeof(val), latin)) {
                    int d = atoi(val);
                    char *u = ci_find(val, "url=");
                    if (u && !p->refresh[0]) {
                        u += 4;
                        if (*u == '\'' || *u == '"') u++;
                        u[strcspn(u, "'\"")] = 0;
                        if (url_join(base, u, p->refresh, sizeof(p->refresh))) p->refresh_delay = d;
                    }
                }
                continue;
            }
            if (strcmp(name, "base") == 0) {
                if (tag_attr(tag, tl, "href", val, sizeof(val), latin) && url_join(p->url, val, val2, sizeof(val2)))
                    snprintf(base, sizeof(base), "%s", val2);
                continue;
            }
            if (in_head) continue;

            FLUSH_WORD();
            int is_h = (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]);

            if (tag_is(name, para_tags)) lay_break(&L, 1);
            else if (tag_is(name, line_tags)) lay_break(&L, 0);

            if (strcmp(name, "br") == 0) { lay_br(&L); continue; }
            if (strcmp(name, "hr") == 0 && !close) { lay_hr(&L); continue; }
            if (is_h) {
                heading = !close;
                L.font = (!close && name[1] <= '2') ? F_LARGE : F_MED;
                SET_STYLE();
                continue;
            }
            if (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0 || strcmp(name, "th") == 0) {
                bold += close ? -1 : 1;
                if (bold < 0) bold = 0;
                SET_STYLE();
                if (strcmp(name, "th") != 0) continue;
            }
            if (strcmp(name, "pre") == 0) { pre = !close; continue; }
            if (strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0 || strcmp(name, "blockquote") == 0 ||
                strcmp(name, "dd") == 0) {
                int d = close ? -1 : 1;
                if (strcmp(name, "dd") != 0 && strcmp(name, "blockquote") != 0) {
                    if (!close) {
                        if (list_depth < 8) { ol_is[list_depth] = (name[0] == 'o'); ol_num[list_depth] = 0; }
                        list_depth++;
                    } else if (list_depth > 0) list_depth--;
                }
                L.indent += d * 18;
                if (L.indent < 0) L.indent = 0;
                if (L.indent > 180) L.indent = 180;
                L.x = L.indent;
                continue;
            }
            if (strcmp(name, "li") == 0 && !close) {
                int d = list_depth > 0 ? list_depth - 1 : 0;
                char bullet[16];
                if (d < 8 && ol_is[d]) snprintf(bullet, sizeof(bullet), "%d.", ++ol_num[d]);
                else snprintf(bullet, sizeof(bullet), "\xC2\xB7");
                int save = L.style, sl = L.link;
                L.style = WS_DIM;
                L.link = -1;
                L.x = L.indent - 5 - text_width(L.font, bullet, 0);
                if (L.x < 0) L.x = 0;
                L.space = 0;
                lay_word(&L, bullet, (int)strlen(bullet));
                L.style = save;
                L.link = sl;
                L.run = -1;
                if (L.x < L.indent + 3) L.x = L.indent + 3;
                continue;
            }
            if (strcmp(name, "tr") == 0) { cell = 0; continue; }
            if ((strcmp(name, "td") == 0 || strcmp(name, "th") == 0) && !close) {
                if (cell++ > 0) { L.x += 16; L.space = 0; L.run = -1; }
                continue;
            }
            if (strcmp(name, "a") == 0) {
                if (link_open) {
                    if (link_items == p->nitems) lay_text(&L, "[lien]");
                    link_open = 0;
                    L.link = -1;
                }
                if (!close && tag_attr(tag, tl, "href", val, sizeof(val), latin) && url_join(base, val, val2, sizeof(val2))) {
                    L.link = new_link(p, LK_HREF, val2, -1, -1);
                    if (L.link >= 0) { link_open = 1; link_items = p->nitems; }
                }
                SET_STYLE();
                continue;
            }
            if (strcmp(name, "img") == 0) {
                if (link_open && tag_attr(tag, tl, "alt", val, sizeof(val), latin) && val[0] && strlen(val) < 80) {
                    char disp[200], t2[210];
                    text_display(val, disp, sizeof(disp));
                    snprintf(t2, sizeof(t2), "[%s]", disp);
                    lay_text(&L, t2);
                }
                continue;
            }
            if (strcmp(name, "form") == 0) {
                cur_form = -1;
                if (!close && p->nforms < W_MAX_FORMS) {
                    char act[URL_MAX];
                    if (!tag_attr(tag, tl, "action", val, sizeof(val), latin) || !val[0] ||
                        !url_join(base, val, act, sizeof(act)))
                        snprintf(act, sizeof(act), "%s", p->url);
                    p->forms[p->nforms].action = pool_add(p, act);
                    p->forms[p->nforms].post = tag_attr(tag, tl, "method", val, sizeof(val), latin) && strcasecmp(val, "post") == 0;
                    cur_form = p->nforms++;
                }
                continue;
            }
            if (strcmp(name, "input") == 0 && cur_form >= 0) {
                char type[24] = "text", nm[128] = "", v[256] = "";
                tag_attr(tag, tl, "type", type, sizeof(type), latin);
                tag_attr(tag, tl, "name", nm, sizeof(nm), latin);
                tag_attr(tag, tl, "value", v, sizeof(v), latin);
                for (char *t = type; *t; t++) *t = (char)tolower((unsigned char)*t);
                if (strcmp(type, "hidden") == 0) { if (nm[0]) new_field(p, cur_form, FD_HIDDEN, nm, v); }
                else if (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0) {
                    if (nm[0] && tag_attr(tag, tl, "checked", NULL, 0, latin)) new_field(p, cur_form, FD_HIDDEN, nm, v[0] ? v : "on");
                } else if (strcmp(type, "submit") == 0 || strcmp(type, "image") == 0) {
                    int f = new_field(p, cur_form, FD_SUBMIT, nm, v);
                    int save = L.link;
                    L.link = new_link(p, LK_SUBMIT, NULL, f, cur_form);
                    if (L.link >= 0) {
                        char disp[200];
                        text_display(v[0] ? v : "Envoyer", disp, sizeof(disp));
                        lay_box(&L, text_width(F_MED, disp, 0) + 20, WS_BUTTON, disp);
                    }
                    L.link = save;
                } else if (strcmp(type, "text") == 0 || strcmp(type, "search") == 0 || strcmp(type, "email") == 0 ||
                           strcmp(type, "url") == 0 || strcmp(type, "number") == 0 || strcmp(type, "tel") == 0 ||
                           strcmp(type, "password") == 0) {
                    int f = new_field(p, cur_form, FD_TEXT, nm, v);
                    if (f >= 0) {
                        int save = L.link;
                        L.link = new_link(p, LK_INPUT, NULL, f, cur_form);
                        if (L.link >= 0) lay_box(&L, 240, WS_FIELD, NULL);
                        L.link = save;
                    }
                }
                continue;
            }
            if (strcmp(name, "button") == 0) {
                if (close) {
                    if (link_open && L.link >= 0 && p->links[L.link].kind == LK_SUBMIT) {
                        if (link_items == p->nitems) lay_text(&L, "[Envoyer]");
                        link_open = 0;
                        L.link = -1;
                        SET_STYLE();
                    }
                } else if (cur_form >= 0 && !link_open) {
                    char type[24] = "submit", nm[128] = "", v[256] = "";
                    tag_attr(tag, tl, "type", type, sizeof(type), latin);
                    if (strcasecmp(type, "submit") == 0) {
                        tag_attr(tag, tl, "name", nm, sizeof(nm), latin);
                        tag_attr(tag, tl, "value", v, sizeof(v), latin);
                        int f = new_field(p, cur_form, FD_SUBMIT, nm, v);
                        L.link = new_link(p, LK_SUBMIT, NULL, f, cur_form);
                        if (L.link >= 0) { link_open = 1; link_items = p->nitems; }
                        SET_STYLE();
                    }
                }
                continue;
            }
            continue;
        }
        if (hide || in_head) { pos++; continue; }

        /* texte */
        unsigned cp = (c == '&') ? read_entity(s, len, &pos) : read_cp(s, len, &pos, latin);
        if (cp == '\r') continue;
        if (cp == ' ' || cp == '\n' || cp == '\t' || cp == '\f') {
            FLUSH_WORD();
            if (pre && cp == '\n') lay_br(&L);
            else L.space = 1;
            continue;
        }
        if (cp == 0xA0) cp = ' ';                                    /* espace insecable : reste dans le mot */
        if (wn > (int)sizeof(word) - 8) FLUSH_WORD();
        wn += cp_out(cp, word + wn);
    }
    FLUSH_WORD();
    if (link_open && link_items == p->nitems) lay_text(&L, "[lien]");
    if (p->truncated) {
        lay_break(&L, 1);
        L.style = WS_DIM;
        L.link = -1;
        lay_text(&L, "(page trop longue : la suite n'est pas affichée)");
    }
    lay_newline(&L);
    p->height = L.line_top;
#undef FLUSH_WORD
#undef SET_STYLE
}

/* texte brut (text/plain) : une ligne par ligne */
static void web_layout_text(wpage_t *p, const unsigned char *s, int len, int latin) {
    wlay_t L;
    memset(&L, 0, sizeof(L));
    L.p = p;
    L.font = F_SMALL;
    L.link = -1;
    L.run = -1;
    char word[300];
    int wn = 0, pos = 0;
    while (pos < len && !p->truncated) {
        unsigned cp = read_cp(s, len, &pos, latin);
        if (cp == '\r') continue;
        if (cp == ' ' || cp == '\t' || cp == '\n') {
            if (wn) { lay_word(&L, word, wn); wn = 0; }
            if (cp == '\n') lay_br(&L);
            else L.space = 1;
            continue;
        }
        if (wn > (int)sizeof(word) - 8) { lay_word(&L, word, wn); wn = 0; }
        wn += cp_out(cp, word + wn);
    }
    if (wn) lay_word(&L, word, wn);
    lay_newline(&L);
    p->height = L.line_top;
}

// ---------- Navigateur : chargement des pages, telechargements ----------
static conn_t wconn[2] = { { -1, 0, 0 }, { -1, 0, 0 } };
static int wpi = 0;                         // wconn[wpi] = page, wconn[1 - wpi] = telechargement
#define PCONN (&wconn[wpi])
#define DCONN (&wconn[1 - wpi])

enum { WK_HTML, WK_TEXT, WK_ARCHIVE };
enum { NAV_PUSH, NAV_BACK, NAV_REPLACE };

typedef struct { int on, st, ext; long rem; } dechunk_t;

/* "Transfer-Encoding: chunked" : retire les tailles de blocs (sur place). */
static int dechunk(dechunk_t *d, unsigned char *buf, int n) {
    int o = 0, i = 0;
    while (i < n) {
        if (d->st == 0) {
            int c = buf[i++];
            if (c == '\n') { d->st = d->rem ? 1 : 3; d->ext = 0; }
            else if (c == ';') d->ext = 1;
            else if (!d->ext && hexv(c) >= 0) {
                if (d->rem > (1L << 26)) d->st = 3;          /* taille absurde : on arrete */
                else d->rem = d->rem * 16 + hexv(c);
            }
        } else if (d->st == 1) {
            int take = (int)((long)(n - i) < d->rem ? (long)(n - i) : d->rem);
            memmove(buf + o, buf + i, take);
            o += take; i += take; d->rem -= take;
            if (d->rem == 0) d->st = 2;
        } else if (d->st == 2) {
            if (buf[i++] == '\n') { d->st = 0; d->rem = 0; }
        } else i = n;                       /* fin (st 3) */
    }
    return o;
}

typedef struct { char url[URL_MAX]; char title[96]; s32 scroll; int sel; } whist_t;
static whist_t hist[HIST_MAX];
static int hist_n = 0;

static int wb_loading = 0, wb_busy = 0;     // corps de page en reception / connexion en cours
static unsigned char *wb_buf = NULL;
static int wb_len = 0, wb_cap = 0, wb_kind = WK_HTML, wb_latin = 0, wb_mode = NAV_PUSH, wb_trunc = 0;
static long wb_expect = -1;
static u64 wb_last = 0;
static char wb_url[URL_MAX];
static dechunk_t wb_dc;
static int wb_refreshes = 0;

static char web_msg[200];
static int web_msg_err = 0;
static u64 web_msg_t = 0;
static char arch_query[200];

static void web_note(const char *m, int err) {
    snprintf(web_msg, sizeof(web_msg), "%s", m);
    web_msg_err = err;
    web_msg_t = now_ms();
}

static void web_cancel_load(void) {
    if (wb_loading) conn_close(PCONN);
    wb_loading = 0;
    free(wb_buf);
    wb_buf = NULL;
    wb_len = wb_cap = 0;
}

static int crecv_timeout(conn_t *c, unsigned char *buf, int len, u32 ms) {
    u64 t0 = now_ms();
    while (now_ms() - t0 < ms) {
        int r = conn_recv(c, buf, len);
        if (r > 0) return r;
        if (r == 0) return 0;
        if (r != -EAGAIN && r != -EWOULDBLOCK) return -1;
        bg_tick();
        usleep(2000);
    }
    return -2;
}

static int is_latin_charset(const char *cs) {
    return ci_find((char *)cs, "8859") || ci_find((char *)cs, "1252") || ci_find((char *)cs, "latin");
}

// ---------- Telechargements (sd:/downloads) ----------
static int dl_active = 0;
static FILE *dl_fp = NULL;
static char dl_name[128], dl_path[NAME_LEN], dl_part[NAME_LEN + 8];
static long dl_total = -1, dl_got = 0;
static u64 dl_t0 = 0, dl_last = 0;
static dechunk_t dl_dc;
static void music_rescan(void);

static void dl_stop(const char *msg, int err) {
    conn_close(DCONN);
    if (dl_fp) { fclose(dl_fp); dl_fp = NULL; }
    if (err) remove(dl_part);
    dl_active = 0;
    if (msg) web_note(msg, err);
}

static void sanitize_name(char *s) {
    for (char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || strchr("\\/:*?\"<>|", c)) *p = '_';
    }
    while (*s == ' ' || *s == '.') memmove(s, s + 1, strlen(s));
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '.')) s[--n] = 0;
}

static void dl_filename(const char *disp, const char *url, const char *ctype, char *out, int outsz) {
    char raw[URL_MAX] = "", dec[URL_MAX];
    if (disp) {
        const char *f = ci_find((char *)disp, "filename*=");
        if (f) {
            f += 10;
            const char *q = strstr(f, "''");
            if (q) f = q + 2;
            snprintf(raw, sizeof(raw), "%s", f);
            raw[strcspn(raw, ";")] = 0;
        } else if ((f = ci_find((char *)disp, "filename=")) != NULL) {
            f += 9;
            if (*f == '"') { f++; snprintf(raw, sizeof(raw), "%s", f); raw[strcspn(raw, "\"")] = 0; }
            else { snprintf(raw, sizeof(raw), "%s", f); raw[strcspn(raw, "; ")] = 0; }
        }
    }
    if (!raw[0]) {
        urlp_t u;
        if (url_parse(url, &u)) {
            u.path[strcspn(u.path, "?")] = 0;
            const char *b = strrchr(u.path, '/');
            snprintf(raw, sizeof(raw), "%s", b ? b + 1 : u.path);
        }
    }
    url_decode(raw, dec, sizeof(dec), 0);
    const char *b = strrchr(dec, '/');
    if (b) memmove(dec, b + 1, strlen(b));
    sanitize_name(dec);
    if (!dec[0]) snprintf(dec, sizeof(dec), "fichier");
    int n = (int)strlen(dec);
    if (ctype && ci_find((char *)ctype, "audio/mpeg") && !(n > 4 && strcasecmp(dec + n - 4, ".mp3") == 0))
        snprintf(dec + n, sizeof(dec) - n, ".mp3");
    /* nom trop long : on garde l'extension */
    n = (int)strlen(dec);
    if (n > 100) {
        const char *e = strrchr(dec, '.');
        char ext[16] = "";
        if (e && strlen(e) < sizeof(ext)) snprintf(ext, sizeof(ext), "%s", e);
        int keep = 100 - (int)strlen(ext);
        while (keep > 0 && ((unsigned char)dec[keep] & 0xC0) == 0x80) keep--;   /* pas au milieu d'un caractere */
        snprintf(dec + keep, sizeof(dec) - keep, "%s", ext);
    }
    snprintf(out, outsz, "%s", dec);
}

/* La reponse en cours (sur PCONN) est un fichier : on le telecharge en tache de fond. */
static void dl_begin(const unsigned char *body, int n, const char *ctype, const char *disp,
                     const char *url, long clen, int chunked) {
    if (dl_active) { conn_close(PCONN); web_note("Un téléchargement est déjà en cours (X : accueil pour l'annuler)", 1); return; }
    if (!sd_ok) { conn_close(PCONN); web_note("Carte SD absente : téléchargement impossible", 1); return; }
    mkdir(DL_DIR, 0777);
    char name[128];
    dl_filename(disp, url, ctype, name, sizeof(name));
    snprintf(dl_path, sizeof(dl_path), "%s/%s", DL_DIR, name);
    struct stat st;
    for (int k = 2; k < 100 && stat(dl_path, &st) == 0; k++) {          /* nom deja pris : "nom (2).mp3" */
        const char *e = strrchr(name, '.');
        int bl = e ? (int)(e - name) : (int)strlen(name);
        snprintf(dl_path, sizeof(dl_path), "%s/%.*s (%d)%s", DL_DIR, bl, name, k, e ? e : "");
    }
    snprintf(dl_name, sizeof(dl_name), "%s", base_of(dl_path));
    snprintf(dl_part, sizeof(dl_part), "%s.part", dl_path);
    dl_fp = fopen(dl_part, "wb");
    if (!dl_fp) { conn_close(PCONN); web_note("Impossible de créer le fichier sur la carte SD", 1); return; }
    wpi = 1 - wpi;                          /* la connexion devient celle du telechargement */
    dl_active = 1;
    dl_total = clen;
    dl_got = 0;
    dl_t0 = dl_last = now_ms();
    memset(&dl_dc, 0, sizeof(dl_dc));
    dl_dc.on = chunked;
    if (n > 0) {
        static unsigned char first[16384];
        memcpy(first, body, n);
        int m = chunked ? dechunk(&dl_dc, first, n) : n;
        if (m > 0 && fwrite(first, 1, m, dl_fp) != (size_t)m) { dl_stop("Écriture impossible sur la carte SD (pleine ?)", 1); return; }
        dl_got += m;
    }
    char msg[200];
    snprintf(msg, sizeof(msg), "Téléchargement de %s...", dl_name);
    web_note(msg, 0);
}

static void dl_finish(void) {
    conn_close(DCONN);
    fclose(dl_fp);
    dl_fp = NULL;
    dl_active = 0;
    if (dl_total > 0 && dl_got < dl_total) { remove(dl_part); web_note("Téléchargement interrompu (fichier incomplet)", 1); return; }
    if (dl_got == 0) { remove(dl_part); web_note("Le fichier reçu est vide", 1); return; }
    remove(dl_path);
    if (rename(dl_part, dl_path) != 0) { web_note("Fichier reçu mais impossible de le renommer", 1); return; }
    int n = (int)strlen(dl_path);
    char msg[220];
    if (n > 4 && strcasecmp(dl_path + n - 4, ".mp3") == 0) {
        music_rescan();
        snprintf(msg, sizeof(msg), "Téléchargé : %s (dans l'onglet MUSIQUE)", dl_name);
    } else snprintf(msg, sizeof(msg), "Enregistré : SD:/downloads/%s", dl_name);
    web_note(msg, 0);
}

static void dl_pump(void) {
    static unsigned char tmp[16384];
    if (!dl_active) return;
    int got = 0;
    while (got < 256 * 1024) {
        int r = conn_recv(DCONN, tmp, sizeof(tmp));
        if (r > 0) {
            int m = dl_dc.on ? dechunk(&dl_dc, tmp, r) : r;
            if (m > 0 && fwrite(tmp, 1, m, dl_fp) != (size_t)m) { dl_stop("Écriture impossible sur la carte SD (pleine ?)", 1); return; }
            dl_got += m;
            got += r;
            dl_last = now_ms();
            if ((dl_dc.on && dl_dc.st == 3) || (dl_total > 0 && dl_got >= dl_total)) { dl_finish(); return; }
            service_audio();
            continue;
        }
        if (r == -EAGAIN || r == -EWOULDBLOCK) break;
        dl_finish();                        /* fin du fichier (ou coupure) */
        return;
    }
    if (now_ms() - dl_last > 30000) dl_stop("Téléchargement interrompu (le serveur ne répond plus)", 1);
}

// ---------- Pages internes (accueil, favoris) ----------
typedef struct { char *b; int n, cap; } sbuf_t;

static void sb_add(sbuf_t *s, const char *t) {
    int l = (int)strlen(t);
    if (s->n + l + 1 > s->cap) {
        int nc = s->cap ? s->cap * 2 : 8192;
        while (nc < s->n + l + 1) nc *= 2;
        char *nb = (char *)realloc(s->b, nc);
        if (!nb) return;
        s->b = nb;
        s->cap = nc;
    }
    memcpy(s->b + s->n, t, l + 1);
    s->n += l;
}

static void sb_esc(sbuf_t *s, const char *t) {
    char one[2] = { 0, 0 };
    for (; *t; t++) {
        if (*t == '&') sb_add(s, "&amp;");
        else if (*t == '<') sb_add(s, "&lt;");
        else if (*t == '>') sb_add(s, "&gt;");
        else if (*t == '"') sb_add(s, "&quot;");
        else { one[0] = *t; sb_add(s, one); }
    }
}

static const char DEFAULT_FAVS[] =
    "Recherche web (Mojeek) | https://www.mojeek.com/\n"
    "Wikipédia (version mobile) | https://fr.m.wikipedia.org/\n"
    "FrogFind (le web en version simple) | http://frogfind.com/\n"
    "68k.news (actualités en texte) | http://68k.news/\n";

static void add_favorites(sbuf_t *s) {
    char line[URL_MAX + 200];
    FILE *f = sd_ok ? fopen(FAV_FILE, "r") : NULL;
    if (!f && sd_ok) {
        FILE *w = fopen(FAV_FILE, "w");
        if (w) { fputs(DEFAULT_FAVS, w); fclose(w); }
        f = fopen(FAV_FILE, "r");
    }
    const char *mem = f ? NULL : DEFAULT_FAVS;
    int count = 0;
    sb_add(s, "<ul>");
    while (count < 200) {
        if (f) { if (!fgets(line, sizeof(line), f)) break; }
        else {
            if (!*mem) break;
            int n = (int)strcspn(mem, "\n");
            snprintf(line, sizeof(line), "%.*s", n, mem);
            mem += n + (mem[n] == '\n');
        }
        char *bar = strchr(line, '|');
        if (!bar) continue;
        *bar = 0;
        char *nm = trim(line), *u = trim(bar + 1);
        if (!*u || (strncasecmp(u, "http", 4) != 0)) continue;
        sb_add(s, "<li><a href=\"");
        sb_esc(s, u);
        sb_add(s, "\">");
        sb_esc(s, *nm ? nm : u);
        sb_add(s, "</a></li>");
        count++;
    }
    if (f) fclose(f);
    if (!count) sb_add(s, "<li>(aucun favori)</li>");
    sb_add(s, "</ul>");
}

/* derniere vraie page web dans l'historique (avant l'accueil) */
static const whist_t *last_web_page(void) {
    for (int i = hist_n - 1; i >= 0; i--)
        if (strncmp(hist[i].url, "gc:", 3) != 0) return &hist[i];
    return NULL;
}

static char *build_home(void) {
    sbuf_t s = { NULL, 0, 0 };
    char t[300];
    sb_add(&s, "<html><head><title>Accueil</title></head><body><h1>GC-MP3 Web</h1>");
    sb_add(&s, "<p>Y : taper une adresse ou une recherche. X : revenir à l'accueil. B : page précédente. "
               "Croix : choisir un lien, A : l'ouvrir. Stick C : faire défiler.</p>");
    if (dl_active) {
        sb_add(&s, "<h2>Téléchargement en cours</h2><p>");
        sb_esc(&s, dl_name);
        if (dl_total > 0) snprintf(t, sizeof(t), " : %ld %%", (long)((long long)dl_got * 100 / dl_total));
        else snprintf(t, sizeof(t), " : %ld Ko", dl_got / 1024);
        sb_add(&s, t);
        sb_add(&s, "</p><p><a href=\"gc:annuler\">Annuler le téléchargement</a></p>");
    }
    sb_add(&s, "<h2>Rechercher</h2><ul>"
               "<li><a href=\"gc:recherche\">Rechercher sur le web</a></li>"
               "<li><a href=\"gc:archive\">Chercher de la musique libre sur archive.org</a></li>"
               "<li><a href=\"gc:adresse\">Taper une adresse</a></li></ul>");
    sb_add(&s, "<h2>Favoris</h2>");
    add_favorites(&s);
    const whist_t *lp = last_web_page();
    if (lp && sd_ok) {
        sb_add(&s, "<p><a href=\"gc:favori\">+ Ajouter aux favoris : ");
        sb_esc(&s, lp->title[0] ? lp->title : lp->url);
        sb_add(&s, "</a></p>");
    }
    sb_add(&s, "<p>Les favoris sont dans SD:/favoris.txt (une ligne par site : Nom | adresse).</p>");
    sb_add(&s, "<h2>Téléchargements</h2><p>Un lien vers un fichier (MP3, ZIP...) est enregistré dans SD:/downloads. "
               "Les MP3 apparaissent ensuite dans l'onglet MUSIQUE. La musique continue pendant le téléchargement.</p>"
               "<p>Ce navigateur n'exécute pas le JavaScript : les sites simples marchent, "
               "pas YouTube, Spotify ni SoundCloud.</p></body></html>");
    return s.b;
}

/* recherche archive.org (reponse JSON) -> page HTML de resultats */
static int json_string(const char *p, const char *end, char *out, int outsz) {
    int n = 0;
    if (p >= end || *p != '"') return 0;
    p++;
    while (p < end && *p != '"' && n < outsz - 4) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            char c = *p++;
            if (c == 'n' || c == 't' || c == 'r') out[n++] = ' ';
            else if (c == 'u' && p + 4 <= end) {
                unsigned v = 0;
                for (int i = 0; i < 4; i++) v = v * 16 + (unsigned)(hexv(p[i]) < 0 ? 0 : hexv(p[i]));
                p += 4;
                if (v >= 0xD800 && v < 0xE000) { out[n++] = '?'; if (p + 6 <= end && p[0] == '\\' && p[1] == 'u') p += 6; }
                else if (v < 0x80) out[n++] = (char)v;
                else if (v < 0x800) { out[n++] = (char)(0xC0 | (v >> 6)); out[n++] = (char)(0x80 | (v & 0x3F)); }
                else { out[n++] = (char)(0xE0 | (v >> 12)); out[n++] = (char)(0x80 | ((v >> 6) & 0x3F)); out[n++] = (char)(0x80 | (v & 0x3F)); }
            } else out[n++] = c;
        } else out[n++] = *p++;
    }
    out[n] = 0;
    return 1;
}

static int json_field(const char *a, const char *b, const char *key, char *out, int outsz) {
    char k[40];
    snprintf(k, sizeof(k), "\"%s\"", key);
    int kl = (int)strlen(k);
    for (const char *p = a; p + kl < b; p++) {
        if (memcmp(p, k, kl) != 0) continue;
        p += kl;
        while (p < b && (*p == ' ' || *p == ':')) p++;
        if (p < b && *p == '[') { p++; while (p < b && *p == ' ') p++; }
        return json_string(p, b, out, outsz);
    }
    out[0] = 0;
    return 0;
}

static char *archive_to_html(const char *js, int len) {
    sbuf_t s = { NULL, 0, 0 };
    char id[200], title[300], creator[200];
    sb_add(&s, "<html><head><title>archive.org : ");
    sb_esc(&s, arch_query);
    sb_add(&s, "</title></head><body><h1>archive.org : ");
    sb_esc(&s, arch_query);
    sb_add(&s, "</h1><p>Ouvre un résultat, puis choisis un fichier .mp3 pour le télécharger.</p><ul>");
    const char *end = js + len;
    const char *p = js;
    const char *d = NULL;
    for (; p + 6 < end; p++) if (memcmp(p, "\"docs\"", 6) == 0) { d = p + 6; break; }
    int count = 0;
    while (d && d < end) {
        while (d < end && *d != '{' && *d != ']') d++;
        if (d >= end || *d == ']') break;
        const char *o = d + 1, *e = o;
        int instr = 0;
        while (e < end && (instr || *e != '}')) {
            if (*e == '\\' && instr) e++;
            else if (*e == '"') instr = !instr;
            e++;
        }
        if (json_field(o, e, "identifier", id, sizeof(id)) && id[0]) {
            json_field(o, e, "title", title, sizeof(title));
            json_field(o, e, "creator", creator, sizeof(creator));
            sb_add(&s, "<li><a href=\"https://archive.org/download/");
            sb_esc(&s, id);
            sb_add(&s, "/\">");
            sb_esc(&s, title[0] ? title : id);
            sb_add(&s, "</a>");
            if (creator[0]) { sb_add(&s, " - "); sb_esc(&s, creator); }
            sb_add(&s, "</li>");
            count++;
        }
        d = e + 1;
    }
    sb_add(&s, "</ul>");
    if (!count) sb_add(&s, "<p>Aucun résultat. Essaie avec d'autres mots (en anglais par exemple).</p>");
    sb_add(&s, "</body></html>");
    return s.b;
}

// ---------- Affichage d'une page chargee ----------
static void web_scroll_clamp(void) {
    s32 mx = pg.height - WV_H + 8;
    if (mx < 0) mx = 0;
    if (pg_scroll > mx) pg_scroll = mx;
    if (pg_scroll < 0) pg_scroll = 0;
}

static int link_visible(int i) {
    if (i < 0 || i >= pg.nlinks || pg.links[i].y0 < 0) return 0;
    return pg.links[i].y0 < pg_scroll + WV_H && pg.links[i].y1 > pg_scroll;
}

static void select_first_visible(void) {
    pg_sel = -1;
    for (int i = 0; i < pg.nlinks; i++)
        if (pg.links[i].y0 >= pg_scroll && pg.links[i].y1 <= pg_scroll + WV_H) { pg_sel = i; return; }
}

static void web_go(const char *url, int mode);

static void web_show(const unsigned char *data, int len, const char *url, int kind, int latin, int mode) {
    wpage_t np;
    memset(&np, 0, sizeof(np));
    snprintf(np.url, sizeof(np.url), "%s", url);
    if (kind == WK_ARCHIVE) {
        char *h = archive_to_html((const char *)data, len);
        if (h) { web_layout_html(&np, (const unsigned char *)h, (int)strlen(h), 0); free(h); }
    } else if (kind == WK_TEXT) web_layout_text(&np, data, len, latin);
    else web_layout_html(&np, data, len, latin);
    if (!np.title[0]) {
        urlp_t u;
        if (url_parse(url, &u)) snprintf(np.title, sizeof(np.title), "%s", u.host);
    }

    /* historique */
    if (hist_n > 0) { hist[hist_n - 1].scroll = pg_scroll; hist[hist_n - 1].sel = pg_sel; }
    s32 scroll = 0;
    int sel = -2;
    if (mode == NAV_BACK && hist_n > 1) { hist_n--; scroll = hist[hist_n - 1].scroll; sel = hist[hist_n - 1].sel; }
    else if (mode == NAV_REPLACE && hist_n > 0) { /* meme place */ }
    else {
        if (hist_n == HIST_MAX) { memmove(&hist[0], &hist[1], sizeof(whist_t) * (HIST_MAX - 1)); hist_n--; }
        hist_n++;
    }
    if (mode == NAV_REPLACE && hist_n == 0) hist_n = 1;
    whist_t *h = &hist[hist_n - 1];
    snprintf(h->url, sizeof(h->url), "%s", url);
    snprintf(h->title, sizeof(h->title), "%s", np.title);

    wpage_free(&pg);
    pg = np;
    pg_scroll = scroll;
    web_scroll_clamp();
    if (sel >= -1 && sel < pg.nlinks) pg_sel = sel;
    else select_first_visible();

    if (pg.refresh[0] && pg.refresh_delay <= 5 && wb_refreshes < 3) {    /* redirection <meta refresh> */
        char next[URL_MAX];
        snprintf(next, sizeof(next), "%s", pg.refresh);
        wb_refreshes++;
        web_go(next, NAV_REPLACE);
    }
}

static void web_finish_load(void) {
    conn_close(PCONN);
    wb_loading = 0;
    if (!wb_buf) return;
    if (wb_len == 0) { web_note("Page vide", 1); web_cancel_load(); return; }
    if (wb_kind == WK_HTML && !wb_latin) {                      /* <meta charset=...> */
        int n = wb_len < 4096 ? wb_len : 4096;
        char head[4097];
        memcpy(head, wb_buf, n);
        head[n] = 0;
        for (int i = 0; i < n; i++) if (!head[i]) head[i] = ' ';
        char *c = ci_find(head, "charset=");
        if (c) {
            c += 8;
            while (*c == '"' || *c == '\'' || *c == ' ') c++;
            char cs[24];
            snprintf(cs, sizeof(cs), "%s", c);
            cs[strcspn(cs, "\"'; />")] = 0;
            wb_latin = is_latin_charset(cs);
        }
    }
    unsigned char *buf = wb_buf;
    int len = wb_len;
    wb_buf = NULL;
    wb_len = wb_cap = 0;
    web_show(buf, len, wb_url, wb_kind, wb_latin, wb_mode);
    if (wb_trunc) web_note("Page trop grande : seul le début est affiché", 1);
    free(buf);
}

/* Ouvre une adresse http(s) : connexion + en-tetes (quelques secondes au plus),
   puis le corps arrive en tache de fond (web_pump). typed = adresse tapee sans
   "https://" : si le site n'accepte pas https, on reessaie en http. */
static void web_fetch(const char *url_in, int mode, int typed) {
    static unsigned char hdr[16384];
    char url[URL_MAX], req[URL_MAX + 400], hosthdr[160];
    web_cancel_load();
    if (net_phase != NET_DONE) { net_start_async(); web_note("Réseau en cours de connexion, réessaie dans quelques secondes", 1); return; }
    if (!net_ok) { web_note("Pas de réseau (vérifie le Broadband Adapter et le câble)", 1); return; }
    snprintf(url, sizeof(url), "%s", url_in);
    web_msg[0] = 0;

    for (int hop = 0; hop < 8; hop++) {
        urlp_t u;
        if (!url_parse(url, &u)) { web_note("Adresse invalide", 1); return; }
        conn_t *c = PCONN;
        const char *err = "Erreur réseau";
        int refused = 0;
        wb_busy = 1;
        if (!conn_open(c, u.host, u.port, u.tls, &err, &refused)) {
            wb_busy = 0;
            if (typed && u.tls && hop == 0 && (refused || strstr(err, "sécurisée"))) {
                char alt[URL_MAX];
                snprintf(alt, sizeof(alt), "http://%s", url + 8);
                snprintf(url, sizeof(url), "%s", alt);
                typed = 0;
                hop--;
                continue;
            }
            web_note(err, 1);
            return;
        }
        if (u.port == (u.tls ? 443 : 80)) snprintf(hosthdr, sizeof(hosthdr), "%s", u.host);
        else snprintf(hosthdr, sizeof(hosthdr), "%s:%d", u.host, u.port);
        int rl = snprintf(req, sizeof(req),
                          "GET %s HTTP/1.0\r\nHost: %s\r\n"
                          "User-Agent: Mozilla/5.0 (compatible; GC-MP3/1.0; Nintendo GameCube)\r\n"
                          "Accept: text/html,application/xhtml+xml,*/*;q=0.8\r\n"
                          "Accept-Language: fr,en;q=0.5\r\nConnection: close\r\n\r\n", u.path, hosthdr);
        ui_status("Envoi de la demande...");
        if (rl >= (int)sizeof(req) || !conn_send_all(c, req, rl)) { conn_close(c); wb_busy = 0; web_note("Erreur d'envoi de la requête", 1); return; }
        ui_status("Attente de la réponse...");

        int hlen = 0, body_off = -1;
        while (hlen < (int)sizeof(hdr) - 1) {
            int r = crecv_timeout(c, hdr + hlen, (int)sizeof(hdr) - 1 - hlen, 15000);
            if (r <= 0) { conn_close(c); wb_busy = 0; web_note(r == -2 ? "Le serveur ne répond pas" : "Connexion coupée", 1); return; }
            hlen += r;
            hdr[hlen] = 0;
            char *e = strstr((char *)hdr, "\r\n\r\n");
            if (e) { body_off = (int)(e - (char *)hdr) + 4; e[2] = 0; break; }
            char *e2 = strstr((char *)hdr, "\n\n");
            if (e2) { body_off = (int)(e2 - (char *)hdr) + 2; e2[1] = 0; break; }
        }
        wb_busy = 0;
        int code = 0;
        if (body_off < 0 || sscanf((char *)hdr, "HTTP/%*d.%*d %d", &code) != 1) {
            conn_close(c);
            web_note("Réponse du serveur invalide", 1);
            return;
        }
        char *hdrs = (char *)hdr;
        if (code >= 300 && code < 400 && code != 304) {
            const char *loc = header_value(hdrs, "Location");
            char next[URL_MAX];
            conn_close(c);
            if (!loc || !url_join(url, loc, next, sizeof(next))) { web_note("Redirection invalide", 1); return; }
            snprintf(url, sizeof(url), "%s", next);
            typed = 0;
            continue;
        }

        char ctype[128] = "", disp[300] = "";
        const char *v = header_value(hdrs, "Content-Type");
        if (v) snprintf(ctype, sizeof(ctype), "%s", v);
        for (char *p = ctype; *p; p++) *p = (char)tolower((unsigned char)*p);
        v = header_value(hdrs, "Content-Disposition");
        if (v) snprintf(disp, sizeof(disp), "%s", v);
        v = header_value(hdrs, "Content-Length");
        long clen = v ? atol(v) : -1;
        v = header_value(hdrs, "Transfer-Encoding");
        int chunked = v && ci_find((char *)v, "chunked");
        int body_len = hlen - body_off;
        unsigned char *body = hdr + body_off;

        int kind = -1;
        u.path[strcspn(u.path, "?")] = 0;
        int pl = (int)strlen(u.path);
        if (ci_find(disp, "attachment")) kind = -1;
        else if (strstr(ctype, "text/html") || strstr(ctype, "xhtml")) kind = WK_HTML;
        else if (strstr(ctype, "json") && strcmp(u.host, "archive.org") == 0 && strstr(u.path, "advancedsearch")) kind = WK_ARCHIVE;
        else if (strstr(ctype, "text/plain")) kind = WK_TEXT;
        else if (!ctype[0] && (pl == 0 || u.path[pl - 1] == '/' || (pl > 4 && strcasecmp(u.path + pl - 4, ".htm") == 0) ||
                                (pl > 5 && strcasecmp(u.path + pl - 5, ".html") == 0))) kind = WK_HTML;

        if (code != 200 && (kind < 0 || code >= 500 || code == 204)) {
            char m[64];
            conn_close(c);
            snprintf(m, sizeof(m), code == 404 ? "Page introuvable (erreur 404)" : "Erreur du serveur (HTTP %d)", code);
            web_note(m, 1);
            return;
        }
        if (kind < 0) { dl_begin(body, body_len, ctype, disp, url, chunked ? -1 : clen, chunked); return; }

        /* page : le reste arrive dans web_pump() */
        wb_cap = 64 * 1024;
        wb_buf = (unsigned char *)malloc(wb_cap);
        if (!wb_buf) { conn_close(c); web_note("Mémoire insuffisante", 1); return; }
        memset(&wb_dc, 0, sizeof(wb_dc));
        wb_dc.on = chunked;
        if (body_len > 0) {
            memcpy(wb_buf, body, body_len);
            wb_len = chunked ? dechunk(&wb_dc, wb_buf, body_len) : body_len;
        }
        snprintf(wb_url, sizeof(wb_url), "%s", url);
        wb_kind = kind;
        wb_latin = is_latin_charset(ctype);
        wb_mode = mode;
        wb_expect = chunked ? -1 : clen;
        wb_trunc = 0;
        wb_last = now_ms();
        wb_loading = 1;
        if (code != 200) {
            char m[64];
            snprintf(m, sizeof(m), "Le serveur a répondu : erreur %d", code);
            web_note(m, 1);
        }
        if ((wb_expect >= 0 && wb_len >= wb_expect) || (wb_dc.on && wb_dc.st == 3)) web_finish_load();
        return;
    }
    web_note("Trop de redirections", 1);
}

static void web_pump(void) {
    static unsigned char tmp[16384];
    if (wb_loading) {
        int got = 0;
        while (got < 192 * 1024) {
            int r = conn_recv(PCONN, tmp, sizeof(tmp));
            if (r > 0) {
                int m = wb_dc.on ? dechunk(&wb_dc, tmp, r) : r;
                if (wb_len + m > WEB_PAGE_MAX) { m = WEB_PAGE_MAX - wb_len; wb_trunc = 1; }
                if (wb_len + m > wb_cap) {
                    int nc = wb_cap * 2;
                    while (nc < wb_len + m) nc *= 2;
                    if (nc > WEB_PAGE_MAX) nc = WEB_PAGE_MAX;
                    unsigned char *nb = (unsigned char *)realloc(wb_buf, nc);
                    if (!nb) { wb_trunc = 1; web_finish_load(); return; }
                    wb_buf = nb;
                    wb_cap = nc;
                }
                memcpy(wb_buf + wb_len, tmp, m);
                wb_len += m;
                got += r;
                wb_last = now_ms();
                if (wb_trunc || (wb_dc.on && wb_dc.st == 3) || (wb_expect >= 0 && wb_len >= wb_expect)) { web_finish_load(); return; }
                service_audio();
                continue;
            }
            if (r == -EAGAIN || r == -EWOULDBLOCK) break;
            web_finish_load();
            return;
        }
        if (wb_loading && now_ms() - wb_last > 20000) {
            if (wb_len > 0) web_finish_load();
            else { web_cancel_load(); web_note("Le serveur ne répond plus", 1); }
        }
    }
    dl_pump();
}

// ---------- Navigateur : actions ----------
enum { KB_URL, KB_SEARCH, KB_ARCHIVE, KB_FIELD };
static int kb_open = 0, kb_mode = KB_URL, kb_field = -1, kb_form = -1;
static char kb_text[256];
static int kb_row = 1, kb_col = 0, kb_shift = 0;

static void kb_start(int mode, const char *init, int field, int form) {
    kb_open = 1;
    kb_mode = mode;
    kb_field = field;
    kb_form = form;
    snprintf(kb_text, sizeof(kb_text), "%s", init ? init : "");
    kb_shift = 0;
}

static void web_render_home(int mode) {
    char *h = build_home();
    if (h) { web_show((const unsigned char *)h, (int)strlen(h), "gc:accueil", WK_HTML, 0, mode); free(h); }
}

static void add_favorite(void) {
    const whist_t *lp = last_web_page();
    if (!lp || !sd_ok) return;
    FILE *f = fopen(FAV_FILE, "a");
    if (!f) { web_note("Impossible d'écrire SD:/favoris.txt", 1); return; }
    char name[96];
    snprintf(name, sizeof(name), "%s", lp->title[0] ? lp->title : lp->url);
    for (char *p = name; *p; p++) if (*p == '|' || *p == '\n' || *p == '\r') *p = '-';
    fprintf(f, "%s | %s\n", name, lp->url);
    fclose(f);
    web_note("Ajouté aux favoris", 0);
}

static void web_go(const char *url, int mode) {
    if (strncmp(url, "gc:", 3) == 0) {
        const char *w = url + 3;
        if (strcmp(w, "recherche") == 0) kb_start(KB_SEARCH, "", -1, -1);
        else if (strcmp(w, "archive") == 0) kb_start(KB_ARCHIVE, "", -1, -1);
        else if (strcmp(w, "adresse") == 0) kb_start(KB_URL, "", -1, -1);
        else if (strcmp(w, "favori") == 0) { add_favorite(); web_render_home(NAV_REPLACE); }
        else if (strcmp(w, "annuler") == 0) { if (dl_active) dl_stop("Téléchargement annulé", 0); web_render_home(NAV_REPLACE); }
        else web_render_home(mode);
        return;
    }
    web_fetch(url, mode, 0);
}

static void web_search(const char *q) {
    char enc[600], url[URL_MAX];
    if (!url_encode(q, enc, sizeof(enc))) return;
    snprintf(url, sizeof(url), "%s%s", SEARCH_URL, enc);
    web_fetch(url, NAV_PUSH, 0);
}

static void web_archive_search(const char *q) {
    char full[400], enc[900], url[URL_MAX];
    snprintf(arch_query, sizeof(arch_query), "%s", q);
    snprintf(full, sizeof(full), "(%s) AND mediatype:(audio)", q);
    if (!url_encode(full, enc, sizeof(enc))) return;
    snprintf(url, sizeof(url), "https://archive.org/advancedsearch.php?q=%s"
             "&fl%%5B%%5D=identifier&fl%%5B%%5D=title&fl%%5B%%5D=creator&rows=60&output=json", enc);
    web_fetch(url, NAV_PUSH, 0);
}

/* envoi d'un formulaire (methode GET) */
static void web_submit(int form, int button) {
    if (form < 0 || form >= pg.nforms) return;
    if (pg.forms[form].post) { web_note("Ce formulaire n'est pas géré (envoi POST)", 1); return; }
    char url[URL_MAX], enc[800];
    snprintf(url, sizeof(url), "%s", pg.pool + pg.forms[form].action);
    url[strcspn(url, "?")] = 0;
    int n = (int)strlen(url), first = 1;
    for (int i = 0; i < pg.nfields; i++) {
        wfield_t *f = &pg.fields[i];
        if (f->form != form || !pg.pool[f->name]) continue;
        if (f->type == FD_SUBMIT && i != button) continue;
        if (!url_encode(pg.pool + f->name, enc, sizeof(enc))) return;
        n += snprintf(url + n, sizeof(url) - n, "%c%s=", first ? '?' : '&', enc);
        if (!url_encode(f->value, enc, sizeof(enc)) || n >= (int)sizeof(url) - 1) { web_note("Formulaire trop long", 1); return; }
        n += snprintf(url + n, sizeof(url) - n, "%s", enc);
        if (n >= (int)sizeof(url) - 1) { web_note("Formulaire trop long", 1); return; }
        first = 0;
    }
    wb_refreshes = 0;
    web_fetch(url, NAV_PUSH, 0);
}

static void kb_validate(void) {
    char *t = trim(kb_text);
    kb_open = 0;
    wb_refreshes = 0;
    if (kb_mode == KB_FIELD) {
        if (kb_field >= 0 && kb_field < pg.nfields) {
            snprintf(pg.fields[kb_field].value, sizeof(pg.fields[kb_field].value), "%s", t);
            web_submit(kb_form, -1);
        }
        return;
    }
    if (!*t) return;
    if (kb_mode == KB_ARCHIVE) { web_archive_search(t); return; }
    if (kb_mode == KB_SEARCH) { web_search(t); return; }
    if (strncasecmp(t, "gc:", 3) == 0) { web_go(t, NAV_PUSH); return; }
    if (strstr(t, "://")) { web_fetch(t, NAV_PUSH, 0); return; }
    if (!strchr(t, ' ') && strchr(t, '.') && t[0] != '.') {
        char url[URL_MAX];
        snprintf(url, sizeof(url), "https://%s", t);
        web_fetch(url, NAV_PUSH, 1);
        return;
    }
    web_search(t);
}

static void web_activate(void) {
    if (!link_visible(pg_sel)) { select_first_visible(); return; }
    wlink_t *l = &pg.links[pg_sel];
    wb_refreshes = 0;
    if (l->kind == LK_HREF) web_go(pg.pool + l->url, NAV_PUSH);
    else if (l->kind == LK_INPUT && l->field >= 0) kb_start(KB_FIELD, pg.fields[l->field].value, l->field, l->form);
    else if (l->kind == LK_SUBMIT) web_submit(l->form, l->field);
}

/* Haut / Bas : lien precedent / suivant ; s'il est trop loin, on fait defiler. */
static void web_nav(int dir) {
    int n = pg.nlinks, cand = -1;
    s32 top = pg_scroll, bot = pg_scroll + WV_H;
    if (link_visible(pg_sel)) cand = pg_sel + dir;
    else if (dir > 0) { for (int i = 0; i < n; i++) if (pg.links[i].y0 >= top) { cand = i; break; } }
    else { for (int i = n - 1; i >= 0; i--) if (pg.links[i].y0 >= 0 && pg.links[i].y1 <= bot) { cand = i; break; } }
    while (cand >= 0 && cand < n && pg.links[cand].y0 < 0) cand += dir;
    if (cand >= 0 && cand < n) {
        wlink_t *l = &pg.links[cand];
        if (dir > 0 ? (l->y0 < bot + WV_H / 2) : (l->y1 > top - WV_H / 2)) {
            pg_sel = cand;
            if (l->y1 > pg_scroll + WV_H) pg_scroll = l->y1 - WV_H + 6;
            if (l->y0 < pg_scroll) pg_scroll = l->y0 - 6;
            web_scroll_clamp();
            return;
        }
    }
    s32 before = pg_scroll;
    pg_scroll += dir * (WV_H * 2 / 3);
    web_scroll_clamp();
    if (pg_scroll == before && cand >= 0 && cand < n) pg_sel = cand;   /* bout de page */
}

static void web_page(int dir) {
    pg_scroll += dir * (WV_H - 40);
    web_scroll_clamp();
    if (!link_visible(pg_sel)) select_first_visible();
}

static void web_enter_tab(void) {
    net_start_async();
    if (pg.nitems == 0 && !wb_loading) web_render_home(NAV_PUSH);
}

static void web_input(u32 down, u32 nav, int fire) {
    if (fire) {
        if (nav & PAD_BUTTON_DOWN) web_nav(1);
        else if (nav & PAD_BUTTON_UP) web_nav(-1);
        else if (nav & PAD_BUTTON_RIGHT) web_page(1);
        else if (nav & PAD_BUTTON_LEFT) web_page(-1);
    }
    /* sticks : defilement continu */
    int sy = PAD_SubStickY(0), my = PAD_StickY(0);
    int v = abs(sy) > abs(my) ? sy : my;
    if (v > 24 || v < -24) {
        pg_scroll -= (v > 0 ? v - 24 : v + 24) / 5;
        web_scroll_clamp();
        if (pg_sel >= 0 && !link_visible(pg_sel)) pg_sel = -1;
    }
    if (down & PAD_BUTTON_A) web_activate();
    if (down & PAD_BUTTON_B) {
        if (wb_loading) { web_cancel_load(); web_note("Chargement annulé", 0); }
        else if (hist_n > 1) { wb_refreshes = 0; web_go(hist[hist_n - 2].url, NAV_BACK); }
    }
    if (down & PAD_BUTTON_X) web_go("gc:accueil", NAV_PUSH);
    if (down & PAD_BUTTON_Y) kb_start(KB_URL, "", -1, -1);
}

// ---------- Clavier virtuel ----------
#define KB_COLS 10
#define KB_ROWS 7
static const char *const kb_keys[6][KB_COLS] = {
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "a", "z", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "q", "s", "d", "f", "g", "h", "j", "k", "l", "m" },
    { "w", "x", "c", "v", "b", "n", ".", "-", "_", "/" },
    { ":", "?", "&", "=", "@", "'", "+", "#", "%", "~" },
    { "\xC3\xA9", "\xC3\xA8", "\xC3\xAA", "\xC3\xA0", "\xC3\xA2", "\xC3\xA7", "\xC3\xB9", "\xC3\xB4", "\xC3\xAE", "\xC3\xAB" },
};
static const char *const kb_special[6] = { "MAJ", "ESPACE", "EFFACER", ".com", ".fr", "OK" };
static const int kb_special_w[6] = { 2, 3, 2, 1, 1, 1 };   /* en colonnes (total 10) */

static void kb_key_label(int r, int c, char *out, int sz) {
    const char *k = kb_keys[r][c];
    snprintf(out, sz, "%s", k);
    if (kb_shift) {
        if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
        else if ((unsigned char)out[0] == 0xC3) out[1] = (char)((unsigned char)out[1] - 0x20);   /* é -> É */
    }
}

static int kb_special_at(int col) {         /* touche speciale sous la colonne col */
    int x = 0;
    for (int i = 0; i < 6; i++) { if (col < x + kb_special_w[i]) return i; x += kb_special_w[i]; }
    return 5;
}

static int kb_special_col(int i) {
    int x = 0;
    for (int k = 0; k < i; k++) x += kb_special_w[k];
    return x;
}

static void kb_append(const char *s) {
    int n = (int)strlen(kb_text), l = (int)strlen(s);
    if (n + l < (int)sizeof(kb_text) - 1) memcpy(kb_text + n, s, l + 1);
}

static void kb_backspace(void) {
    int n = (int)strlen(kb_text);
    if (n == 0) return;
    n--;
    while (n > 0 && ((unsigned char)kb_text[n] & 0xC0) == 0x80) n--;
    kb_text[n] = 0;
}

static void kb_press(void) {
    if (kb_row < 6) {
        char k[8];
        kb_key_label(kb_row, kb_col, k, sizeof(k));
        kb_append(k);
        return;
    }
    switch (kb_special_at(kb_col)) {
    case 0: kb_shift = !kb_shift; break;
    case 1: kb_append(" "); break;
    case 2: kb_backspace(); break;
    case 3: kb_append(".com"); break;
    case 4: kb_append(".fr"); break;
    default: kb_validate(); break;
    }
}

static void kb_input(u32 down, u32 nav, int fire) {
    if (fire) {
        if (nav & PAD_BUTTON_UP) {
            if (kb_row == 6) kb_col = kb_special_col(kb_special_at(kb_col)) + kb_special_w[kb_special_at(kb_col)] / 2;
            kb_row = (kb_row + KB_ROWS - 1) % KB_ROWS;
            if (kb_row == 6) kb_col = kb_special_col(kb_special_at(kb_col));
        } else if (nav & PAD_BUTTON_DOWN) {
            if (kb_row == 6) kb_col = kb_special_col(kb_special_at(kb_col)) + kb_special_w[kb_special_at(kb_col)] / 2;
            kb_row = (kb_row + 1) % KB_ROWS;
            if (kb_row == 6) kb_col = kb_special_col(kb_special_at(kb_col));
        } else if (nav & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) {
            int d = (nav & PAD_BUTTON_RIGHT) ? 1 : -1;
            if (kb_row == 6) {
                int i = (kb_special_at(kb_col) + d + 6) % 6;
                kb_col = kb_special_col(i);
            } else kb_col = (kb_col + d + KB_COLS) % KB_COLS;
        }
    }
    if (down & PAD_BUTTON_A) kb_press();
    if (down & PAD_BUTTON_B) kb_backspace();
    if (down & PAD_BUTTON_X) kb_append(" ");
    if (down & PAD_BUTTON_Y) kb_open = 0;
    if (down & PAD_BUTTON_START) kb_validate();
}

// ---------- Navigateur : affichage ----------
static void draw_mini_badge(f32 cx, f32 cy, const char *k) {
    if (k[0] == 'A') draw_badge(cx, cy, "A", RGBA(80, 210, 100, 255), RGBA(30, 140, 60, 255));
    else if (k[0] == 'B') draw_badge(cx, cy, "B", RGBA(236, 70, 90, 255), RGBA(170, 30, 50, 255));
    else if (k[0] == 'X' || k[0] == 'Y') draw_badge(cx, cy, k, RGBA(150, 150, 175, 255), RGBA(90, 90, 120, 255));
    else if (k[0] == 'Z') {
        gx_rrect_fill(cx - 8, cy - 7, 16, 14, 4, RGBA(110, 90, 190, 255), RGBA(70, 50, 140, 255));
        gx_text_c(F_SMALL, cx, cy + 4.5f, "Z", RGBA(255, 255, 255, 255));
    } else {
        gx_rrect_fill(cx - 10, cy - 6.5f, 20, 13, 6.5f, RGBA(140, 140, 165, 255), RGBA(84, 84, 110, 255));
        gx_text_c(F_SMALL, cx, cy + 4.0f, "ST", RGBA(255, 255, 255, 255));
    }
}

static void draw_legend_web(void) {
    static const char *const keys_web[6] = { "A", "B", "Y", "X", "Z", "ST" };
    static const char *const lab_web[6] = { "OUVRIR", "RETOUR", "ADRESSE", "ACCUEIL", "ONGLET", "QUITTER" };
    static const char *const keys_kb[6] = { "A", "B", "X", "Y", "ST", "" };
    static const char *const lab_kb[6] = { "TAPER", "EFFACER", "ESPACE", "ANNULER", "VALIDER", "" };
    const char *const *keys = kb_open ? keys_kb : keys_web;
    const char *const *lab = kb_open ? lab_kb : lab_web;
    int cnt = kb_open ? 5 : 6;
    const f32 x0 = 11, w = 618, cy = 438.5f;
    gx_rrect_fill(x0, 419, w, 39, 9, C_PANEL_TOP, C_PANEL_BOT);
    gx_rrect_stroke(x0, 419, w, 39, 9, 1.2f, RGBA(100, 56, 220, 255));
    f32 total = 0;
    for (int i = 0; i < cnt; i++) total += 20 + 5 + text_width(F_SMALL, lab[i], 1);
    f32 space = (w - 40 - total) / (cnt - 1);
    f32 x = x0 + 20;
    for (int i = 0; i < cnt; i++) {
        draw_mini_badge(x + 10, cy, keys[i]);
        x += 25;
        x += gx_text(F_SMALL, x, cy + 4.5f, lab[i], RGBA(208, 198, 240, 255), RGBA(208, 198, 240, 255), 1) + space;
    }
}

static void fmt_size(char *b, int sz, long v) {
    if (v >= 1024 * 1024) snprintf(b, sz, "%ld,%ld Mo", v / (1024 * 1024), (v % (1024 * 1024)) * 10 / (1024 * 1024));
    else snprintf(b, sz, "%ld Ko", v / 1024);
}

static int draw_tabs(const char *count);

static void draw_web(void) {
    char buf[URL_MAX], fit[400];
    draw_panel(11, 73, 618, 340);
    int tx = draw_tabs(NULL);

    /* barre d'adresse */
    f32 ax = (f32)tx + 8, aw = 616 - ax;
    gx_rrect_fill(ax, 86, aw, 18, 6, RGBA(8, 5, 26, 255), RGBA(16, 10, 44, 255));
    gx_rrect_stroke(ax, 86, aw, 18, 6, 1.0f, RGBA(90, 64, 170, 255));
    col_t ac = RGBA(200, 190, 240, 255);
    if (wb_busy) { snprintf(buf, sizeof(buf), "%s", conn_msg); ac = RGBA(255, 210, 120, 255); }
    else if (wb_loading) {
        char sz[32];
        fmt_size(sz, sizeof(sz), wb_len);
        urlp_t u;
        snprintf(buf, sizeof(buf), "Chargement de %s... %s", url_parse(wb_url, &u) ? u.host : "la page", sz);
        ac = RGBA(255, 210, 120, 255);
    } else if (strcmp(pg.url, "gc:accueil") == 0) snprintf(buf, sizeof(buf), "Accueil");
    else snprintf(buf, sizeof(buf), "%s", pg.url);
    char disp[URL_MAX];
    text_display(buf, disp, sizeof(disp));
    fit_text(F_SMALL, disp, (int)aw - 14, fit, sizeof(fit));
    gx_text1(F_SMALL, ax + 7, 99, fit, ac);
    if (wb_loading) {                                          /* petite barre animee */
        f32 ph = (f32)(retrace_count % 60) / 60.0f;
        gx_rect(ax + 4 + ph * (aw - 40), 102, 32, 2, RGBA(255, 190, 90, 200));
    }

    /* contenu */
    GX_SetScissor(WV_X - 6, WV_Y - 4, WV_W + 12, WV_H + 6);
    int lo = 0, hi = pg.nitems;
    while (lo < hi) {                                          /* premier element visible */
        int mid = (lo + hi) / 2;
        if (pg.items[mid].y + 8 < pg_scroll) lo = mid + 1; else hi = mid;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = lo; i < pg.nitems; i++) {
            witem_t *it = &pg.items[i];
            int f = it->font;
            f32 by = (f32)(WV_Y + it->y - pg_scroll);
            if (by - font_ascent[f] > WV_Y + WV_H + 4) break;
            f32 bx = (f32)(WV_X + it->x);
            int sel = (it->link >= 0 && it->link == pg_sel);
            f32 top = by - font_ascent[f];
            if (pass == 0) {                                   /* fonds : selection, boutons, champs */
                if (it->style & WS_FIELD) {
                    gx_rrect_fill(bx, top - 3, it->w, font_height[F_MED] + 6, 4,
                                  sel ? RGBA(60, 30, 140, 255) : RGBA(6, 4, 20, 255), sel ? RGBA(40, 20, 100, 255) : RGBA(14, 9, 36, 255));
                    gx_rrect_stroke(bx, top - 3, it->w, font_height[F_MED] + 6, 4, sel ? 2.0f : 1.0f,
                                    sel ? RGBA(200, 140, 255, 255) : RGBA(110, 80, 200, 255));
                } else if ((it->style & WS_BUTTON) && !(it->style & WS_LINK)) {
                    gx_rrect_fill(bx, top - 3, it->w, font_height[F_MED] + 6, 6,
                                  sel ? RGBA(150, 80, 255, 255) : RGBA(90, 50, 190, 255), sel ? RGBA(100, 40, 210, 255) : RGBA(60, 30, 140, 255));
                } else if (sel) {
                    gx_rrect_fill(bx - 3, top - 2, it->w + 6, font_height[f] + 3, 4, RGBA(124, 54, 240, 255), RGBA(80, 26, 180, 255));
                } else if (it->style & WS_BUTTON) {
                    gx_rrect_fill(bx - 4, top - 2, it->w + 8, font_height[f] + 3, 5, RGBA(70, 40, 160, 255), RGBA(46, 24, 110, 255));
                }
                continue;
            }
            const char *t = pg.text + it->off;
            if (it->style & WS_HR) { gx_rect(bx, by - 1, it->w, 1, RGBA(90, 64, 170, 255)); continue; }
            if (it->style & WS_FIELD) {
                const char *v = "";
                if (it->link >= 0 && pg.links[it->link].field >= 0) v = pg.fields[pg.links[it->link].field].value;
                char dv[300];
                text_display(v, dv, sizeof(dv));
                fit_text(F_MED, dv, it->w - 12, fit, sizeof(fit));
                gx_text1(F_MED, bx + 6, by, fit, RGBA(235, 230, 255, 255));
                continue;
            }
            if ((it->style & WS_BUTTON) && !(it->style & WS_LINK)) {
                gx_text1(F_MED, bx + 10, by, t, RGBA(255, 255, 255, 255));
                continue;
            }
            col_t c;
            if (sel) c = RGBA(255, 255, 255, 255);
            else if (it->style & WS_LINK) c = RGBA(150, 178, 255, 255);
            else if (it->style & WS_HEAD) c = RGBA(255, 255, 255, 255);
            else if (it->style & WS_DIM) c = RGBA(140, 122, 200, 255);
            else if (it->style & WS_BOLD) c = RGBA(246, 242, 255, 255);
            else c = RGBA(206, 198, 238, 255);
            if (it->style & WS_HEAD) gx_text(f, bx, by, t, RGBA(255, 255, 255, 255), RGBA(190, 150, 255, 255), 0);
            else gx_text1(f, bx, by, t, c);
            if ((it->style & WS_LINK) && !sel && !(it->style & WS_BUTTON)) gx_rect(bx, by + 2, it->w, 1, RGBA(120, 140, 240, 110));
        }
    }
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    if (pg.nitems == 0 && !wb_loading && !wb_busy)
        gx_text1(F_MED, WV_X, WV_Y + 20, "(page sans texte)", RGBA(140, 122, 200, 255));

    /* barre de defilement */
    if (pg.height > WV_H) {
        f32 h = (f32)WV_H * WV_H / pg.height;
        if (h < 16) h = 16;
        f32 y = WV_Y + (WV_H - h) * pg_scroll / (f32)(pg.height - WV_H + 8);
        gx_rect(619, WV_Y, 2, WV_H, RGBA(50, 34, 110, 255));
        gx_rrect_fill(618, y, 4, h, 2, RGBA(170, 110, 255, 255), RGBA(120, 70, 230, 255));
    }

    /* ligne d'etat : telechargement / message / morceau en cours */
    gx_rect(25, 392, 590, 1, RGBA(76, 52, 150, 200));
    int msg_on = web_msg[0] && now_ms() - web_msg_t < 8000;
    char st[300];
    col_t sc = RGBA(170, 160, 220, 255);
    if (dl_active) {
        char a[32], b[40];
        fmt_size(a, sizeof(a), dl_got);
        u64 el = now_ms() - dl_t0;
        if (el > 1500) {
            fmt_size(b + 1, sizeof(b) - 1, (long)(dl_got * 1000 / (long long)el));
            b[0] = '(';
            strcat(b, "/s)");
        } else b[0] = 0;
        if (dl_total > 0) snprintf(st, sizeof(st), "%ld %%  %s  %s  %s", (long)((long long)dl_got * 100 / dl_total), a, b, dl_name);
        else snprintf(st, sizeof(st), "%s  %s  %s", a, b, dl_name);
        f32 frac = dl_total > 0 ? (f32)dl_got / dl_total : (f32)(retrace_count % 120) / 120.0f;
        gx_rrect_fill(26, 398, 80, 8, 4, RGBA(30, 20, 70, 255), RGBA(30, 20, 70, 255));
        gx_rrect_fill(26, 398, 6 + 74 * frac, 8, 4, RGBA(120, 230, 140, 255), RGBA(60, 170, 90, 255));
        fit_text(F_SMALL, st, 300, fit, sizeof(fit));
        gx_text1(F_SMALL, 114, 406, fit, RGBA(170, 240, 180, 255));
    } else if (msg_on) {
        text_display(web_msg, st, sizeof(st));
        sc = web_msg_err ? RGBA(255, 160, 110, 255) : RGBA(170, 240, 180, 255);
        fit_text(F_SMALL, st, 400, fit, sizeof(fit));
        gx_text1(F_SMALL, 26, 406, fit, sc);
    }
    if (dl_active && msg_on && web_msg_err) {                 /* erreur pendant un telechargement */
        text_display(web_msg, st, sizeof(st));
        fit_text(F_SMALL, st, 180, fit, sizeof(fit));
        gx_text_r(F_SMALL, 614, 406, fit, RGBA(255, 160, 110, 255));
    } else if (src != SRC_NONE) {
        if (src == SRC_FILE && cur_track >= 0) snprintf(st, sizeof(st), "%s", display_name(base_of(names[cur_track])));
        else if (src == SRC_RADIO && cur_station >= 0) snprintf(st, sizeof(st), "%s", now_title[0] ? now_title : stations[cur_station].name);
        else st[0] = 0;
        int maxw = (dl_active || msg_on) ? 180 : 560;
        char d2[300];
        text_display(st, d2, sizeof(d2));
        fit_text(F_SMALL, d2, maxw, fit, sizeof(fit));
        int w = text_width(F_SMALL, fit, 0);
        draw_note((f32)(614 - w - 10), 400, RGBA(150, 120, 235, 255));
        gx_text_r(F_SMALL, 614, 406, fit, RGBA(150, 135, 210, 255));
    }
}

static void draw_keyboard(void) {
    char k[16];
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120));
    gx_rrect_fill(30, 120, 580, 296, 10, RGBA(14, 8, 44, 255), RGBA(8, 5, 26, 255));   /* fond opaque */
    draw_panel(30, 120, 580, 296);
    const char *title = kb_mode == KB_SEARCH ? "Rechercher sur le web" :
                        kb_mode == KB_ARCHIVE ? "Musique libre sur archive.org" :
                        kb_mode == KB_FIELD ? "Saisie" : "Adresse ou recherche";
    gx_text1(F_MED, 46, 143, title, RGBA(255, 255, 255, 255));
    if (kb_shift) gx_text_r(F_SMALL, 594, 142, "MAJUSCULES", RGBA(255, 210, 120, 255));
    /* zone de texte */
    gx_rrect_fill(42, 151, 556, 26, 6, RGBA(6, 4, 20, 255), RGBA(14, 9, 36, 255));
    gx_rrect_stroke(42, 151, 556, 26, 6, 1.5f, RGBA(170, 110, 255, 255));
    const char *t = kb_text;
    while (text_width(F_MED, t, 0) > 530 && *t) { const char *q = t; next_cp(&q); t = q; }   /* fin du texte visible */
    int w = gx_text1(F_MED, 50, 170, t, RGBA(240, 236, 255, 255));
    if ((retrace_count / 30) & 1) gx_rect(51 + w, 156, 2, 17, RGBA(200, 160, 255, 255));
    if (!kb_text[0]) {
        const char *hint = kb_mode == KB_URL ? "ex. : fr.m.wikipedia.org  ou  musique libre" : "";
        gx_text1(F_MED, 56, 170, hint, RGBA(110, 96, 170, 255));
    }
    /* touches */
    const f32 kx = 42, ky = 186, kw = 52, kh = 28, g = 4.4f;
    for (int r = 0; r < KB_ROWS; r++) {
        int cols = (r == 6) ? 6 : KB_COLS;
        for (int c = 0; c < cols; c++) {
            f32 x, wdt;
            if (r == 6) { x = kx + kb_special_col(c) * (kw + g); wdt = kb_special_w[c] * (kw + g) - g; }
            else { x = kx + c * (kw + g); wdt = kw; }
            f32 y = ky + r * (kh + 4);
            int on = (r == kb_row) && (r == 6 ? kb_special_at(kb_col) == c : kb_col == c);
            if (on) {
                gx_glow(x, y, wdt, kh, 5, RGBA(150, 80, 255, 120), 2);
                gx_rrect_fill(x, y, wdt, kh, 5, RGBA(124, 54, 240, 255), RGBA(80, 26, 180, 255));
            } else gx_rrect_fill(x, y, wdt, kh, 5, RGBA(34, 22, 84, 255), RGBA(20, 12, 54, 255));
            gx_rrect_stroke(x, y, wdt, kh, 5, 1.0f, on ? RGBA(200, 140, 255, 255) : RGBA(80, 56, 160, 255));
            if (r == 6) snprintf(k, sizeof(k), "%s", kb_special[c]);
            else kb_key_label(r, c, k, sizeof(k));
            int ff = (r == 6) ? F_SMALL : F_MED;
            col_t kc = (r == 6 && c == 0 && kb_shift) ? RGBA(255, 210, 120, 255) : RGBA(240, 234, 255, 255);
            gx_text_c(ff, x + wdt / 2, y + kh / 2 + (ff == F_MED ? 5 : 4.5f), k, kc);
        }
    }
}

static int kb_is_open(void) { return kb_open; }

/* Relit la liste des MP3 (apres un telechargement ou X), sans couper le morceau en cours. */
static void music_rescan(void) {
    if (!sd_ok) return;
    char playing_path[NAME_LEN] = "";
    if (src == SRC_FILE && cur_track >= 0) snprintf(playing_path, sizeof(playing_path), "%s", names[cur_track]);
    char sel_path[NAME_LEN] = "";
    if (sel_track >= 0 && sel_track < nfiles) snprintf(sel_path, sizeof(sel_path), "%s", names[sel_track]);
    scan_files();
    cur_track = -1;
    for (int i = 0; i < nfiles && playing_path[0]; i++)
        if (strcmp(names[i], playing_path) == 0) { cur_track = i; break; }
    for (int i = 0; i < nfiles && sel_path[0]; i++)
        if (strcmp(names[i], sel_path) == 0) { sel_track = i; break; }
    if (src == SRC_FILE && cur_track < 0) stop_track();     /* fichier supprime entre-temps */
    if (sel_track >= nfiles) sel_track = nfiles > 0 ? nfiles - 1 : 0;
}

/* Pendant les attentes reseau (connexion, chiffrement...) : la musique et la
   radio continuent d'etre decodees. */
#define FILE_LEAD (48000 * 4)                  /* 4 s de son decode d'avance */
static int in_bg = 0;
static void bg_tick(void) {
    service_audio();
    if (in_bg) return;
    in_bg = 1;
    if (src == SRC_FILE && playing && !paused) {
        int k = 0;
        while (!eof && k < 2 && f_count < FILE_LEAD && (FIFO_SIZE - f_count) >= FIFO_MARGIN) {
            decode_file_one();
            service_audio();
            k++;
        }
    }
    if (src == SRC_RADIO && (state == ST_BUFFER || state == ST_PLAY)) {
        stream_recv();
        int k = 0;
        while (k < 2 && FIFO_SIZE - f_count >= FIFO_MARGIN && decode_stream_one(stream_closed)) { service_audio(); k++; }
        if (state == ST_BUFFER && f_count >= PREBUFFER) state = ST_PLAY;
    }
    in_bg = 0;
}

// ---------- Programme ----------
int main(void) {
    if (!init_video()) return 1;
    gx_init();
    init_spectrum();

    draw_message("Montage de la carte SD...", NULL, NULL);
    sd_ok = mount_sd();
    if (sd_ok) scan_files();
    load_stations();

    AUDIO_Init(NULL);
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
    AUDIO_RegisterDMACallback(dma_cb);
    memset(dmabuf, 0, sizeof(dmabuf));
    DCFlushRange(dmabuf, sizeof(dmabuf));
    dma_cb();
    AUDIO_StartDMA();

    u32 rep_mask = 0;
    u64 rep_next = 0;

    while (1) {
        PAD_ScanPads();
        u32 down = PAD_ButtonsDown(0);
        u32 held = PAD_ButtonsHeld(0);
        u64 now = now_ms();

        /* croix, avec repetition quand on la maintient */
        u32 nav = held & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
        int fire = 0;
        if (down & nav) { fire = 1; rep_mask = nav; rep_next = now + 400; }
        else if (nav && nav == rep_mask && now >= rep_next) { fire = 1; rep_next = now + 70; }
        else if (!nav) rep_mask = 0;

        if (tab == TAB_WEB && kb_open) {
            kb_input(down, nav, fire);          /* clavier : START = valider, pas quitter */
            down = 0;
            fire = 0;
        }
        if (down & PAD_BUTTON_START) break;

        /* onglets : la lecture en cours continue */
        if (down & PAD_TRIGGER_Z) {
            tab = (tab + 1) % 3;
            if (tab == TAB_RADIO) net_start_async();
            if (tab == TAB_WEB) web_enter_tab();
        }

        if (tab == TAB_WEB) {
            web_input(down, nav, fire);
        } else {
            /* navigation dans la liste de l'onglet */
            int count = (tab == TAB_MUSIC) ? nfiles : nstations;
            int *sel = (tab == TAB_MUSIC) ? &sel_track : &sel_station;
            if (fire && count > 0) {
                if (nav & PAD_BUTTON_DOWN)  (*sel)++;
                if (nav & PAD_BUTTON_UP)    (*sel)--;
                if (nav & PAD_BUTTON_RIGHT) *sel += LIST_ROWS;
                if (nav & PAD_BUTTON_LEFT)  *sel -= LIST_ROWS;
                if (*sel < 0) *sel = 0;
                if (*sel > count - 1) *sel = count - 1;
            }
            if ((down & PAD_BUTTON_A) && count > 0) {
                if (tab == TAB_MUSIC) start_track(sel_track);   /* chargement : l'audio joue du silence */
                else start_station(sel_station, 0);
            }
            if (down & PAD_BUTTON_B) {
                stop_track();
                stop_station();
                track_err = NULL;
                err_msg[0] = 0;
            }
            if ((down & PAD_BUTTON_X) && tab == TAB_MUSIC && sd_ok) music_rescan();
            if (down & PAD_BUTTON_Y) {
                if (src == SRC_FILE && playing) paused = !paused;
                else if (src == SRC_RADIO) muted = !muted;
            }
        }
        if (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) {
            int d = (down & PAD_TRIGGER_R) ? 1 : -1;
            if (src == SRC_FILE && nfiles > 0) {
                int nx = cur_track + d;
                if (nx >= 0 && nx < nfiles) { sel_track = nx; start_track(nx); }
            } else if (src == SRC_RADIO && nstations > 0) {
                int nx = (cur_station + d + nstations) % nstations;
                sel_station = nx;
                start_station(nx, 0);
            }
        }

        /* navigateur : reception des pages et des telechargements */
        web_pump();

        service_audio();

        /* musique : decodage du fichier en cours */
        if (src == SRC_FILE && playing && !paused) {
            int k = 0;
            while (!eof && k < 2 && f_count < FILE_LEAD && (FIFO_SIZE - f_count) >= FIFO_MARGIN) {
                decode_file_one();
                service_audio();
                k++;
            }
            if (eof && f_count == 0) {                 /* piste suivante */
                int nx = cur_track + 1;
                if (nx < nfiles) { sel_track = nx; start_track(nx); }
                else stop_track();
            }
        }

        /* radio : reception, decodage, tampon */
        stream_pump();
        service_audio();

        update_spectrum();
        draw();
    }

    AUDIO_RegisterDMACallback(NULL);
    stop_track();
    stop_station();
    AUDIO_StopDMA();
    return 0;
}

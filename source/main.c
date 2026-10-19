// GC-MP3 "Indigo Audio Player" - musique (carte SD) + webradios pour GameCube
// (libogc + GX + minimp3 + Broadband Adapter)
//
// Onglet MUSIQUE : fichiers .mp3 du dossier sd:/mp3
//   Pochettes : sd:/mp3/<meme nom>.jpg/.png, pochette integree (ID3), ou sd:/mp3/cover.jpg
// Onglet RADIO : liste sd:/radio.txt (creee au premier lancement), une station par ligne :
//   Nom | http://adresse/du/flux       Logo facultatif : sd:/radio/<Nom>.jpg ou .png
//   Flux MP3 en http:// (pas https, pas AAC/OGG). Listes .m3u/.pls et redirections suivies.
//
// Commandes : Z = changer d'onglet (la lecture continue), Haut/Bas = choisir,
//             Gauche/Droite = page, A = lire, B = arreter, Y = pause (musique) / muet (radio),
//             L/R = piste ou station precedente/suivante, START = quitter
// Le reseau n'est initialise qu'au premier passage sur l'onglet RADIO.
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

// Atlas de polices lissees (genere par tools/gen_font.py)
#include "font_atlas.h"

#ifndef SD_ROOT
#define SD_ROOT        "sd:"
#endif
#define RADIO_LIST     SD_ROOT "/radio.txt"
#define LOGO_DIR       SD_ROOT "/radio"
#define MP3_DIR        SD_ROOT "/mp3"
#define MAX_FILES      512
#define NAME_LEN       128
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
enum { TAB_MUSIC, TAB_RADIO };
static int tab = TAB_MUSIC;             /* onglet affiche */
static int paused = 0;                  /* pause de la musique */
static void stop_track(void);
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
    "# Flux MP3 en http:// uniquement (pas https, pas AAC/OGG).\n"
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
            if (n == -EAGAIN || n == -EWOULDBLOCK) { service_audio(); usleep(2000); continue; }
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

// ---------- HTTP + metadonnees ICY ----------
static s32 sock = -1;
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
    if (sock >= 0) net_close(sock);
    sock = -1;
}

/* Recoit avec un delai maximum (pendant la connexion). */
static int recv_timeout(s32 s, unsigned char *buf, int len, u32 ms) {
    u64 t0 = now_ms();
    while (now_ms() - t0 < ms) {
        s32 r = net_recv(s, buf, len, 0);
        if (r > 0) return r;
        if (r == 0) return 0;                                  /* connexion fermee */
        if (r != -EAGAIN && r != -EWOULDBLOCK) return -1;
        service_audio();
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

static void ui_status(const char *text);   /* redessine l'ecran pendant la connexion */

/* Ouvre un flux http. Suit les redirections et les listes .m3u/.pls. */
static int http_open(const char *url_in, int depth) {
    char url[300], host[128], path[256], req[600];
    static unsigned char hdr[8192];
    int port = 80;

    if (depth > 5) { set_error("Trop de redirections"); return 0; }
    snprintf(url, sizeof(url), "%s", url_in);
    if (strncasecmp(url, "https://", 8) == 0) {
        set_error("Radio en HTTPS : non gérée");
        return 0;
    }
    if (strncasecmp(url, "http://", 7) != 0) { set_error("Adresse invalide (doit commencer par http://)"); return 0; }

    const char *h = url + 7;
    int hl = (int)strcspn(h, ":/?");
    if (hl <= 0 || hl >= (int)sizeof(host)) { set_error("Adresse invalide"); return 0; }
    memcpy(host, h, hl);
    host[hl] = 0;
    const char *rest = h + hl;
    if (*rest == ':') { port = atoi(rest + 1); rest += strcspn(rest, "/?"); }
    if (*rest == '/') snprintf(path, sizeof(path), "%s", rest);
    else snprintf(path, sizeof(path), "/%s", rest);
    if (port <= 0 || port > 65535) port = 80;

    char msg[160];
    snprintf(msg, sizeof(msg), "Recherche de %s...", host);
    ui_status(msg);
    u32 addr;
    if (!resolve_host(host, &addr)) { set_error("Serveur introuvable (DNS)"); return 0; }

    snprintf(msg, sizeof(msg), "Connexion à %s...", host);
    ui_status(msg);
    s32 s = net_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { set_error("Erreur réseau (socket)"); return 0; }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u16)port);
    sa.sin_addr.s_addr = addr;
    if (net_connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        net_close(s);
        set_error("Connexion refusée par le serveur");
        return 0;
    }
    sock = s;

    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: GC-Radio/1.0 (GameCube)\r\n"
                      "Icy-MetaData: 1\r\nAccept: */*\r\nConnection: close\r\n\r\n", path, host);
    if (net_send(s, req, rl, 0) != rl) { set_error("Erreur d'envoi de la requête"); return 0; }

    /* lecture des en-tetes */
    set_nonblock(s, 1);
    int hlen = 0, body_off = -1;
    while (hlen < (int)sizeof(hdr) - 1) {
        int r = recv_timeout(s, hdr + hlen, (int)sizeof(hdr) - 1 - hlen, 8000);
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
        if (loc[0] == '/') snprintf(next, sizeof(next), "http://%s:%d%s", host, port, loc);
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
            int r = recv_timeout(s, (unsigned char *)pl + pn, (int)sizeof(pl) - 1 - pn, 5000);
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
    while (sock >= 0 && !stream_closed && cbuf_free() > (int)sizeof(tmp) && got < 64 * 1024) {
        s32 r = net_recv(sock, tmp, sizeof(tmp), 0);
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
    char base[NAME_LEN], path[NAME_LEN + 32];

    cover_ok = 0;
    snprintf(base, sizeof(base), "%s", names[idx]);
    size_t l = strlen(base);
    if (l > 4) base[l - 4] = 0;                                            // retire ".mp3"

    for (int i = 0; i < 3 && !cover_ok; i++) {
        snprintf(path, sizeof(path), "%s/%s%s", MP3_DIR, base, exts[i]);
        cover_ok = cover_from_file(path);
    }
    if (!cover_ok) {
        snprintf(path, sizeof(path), "%s/%s", MP3_DIR, names[idx]);
        cover_ok = cover_from_id3(path);
    }
    for (int i = 0; i < 4 && !cover_ok; i++) {
        snprintf(path, sizeof(path), "%s/%s", MP3_DIR, common[i]);
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

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", MP3_DIR, names[idx]);
    FILE *fp = fopen(path, "rb");
    if (!fp) { track_err = "Ouverture impossible"; return 0; }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); track_err = "Lecture impossible"; return 0; }
    long size = ftell(fp);
    if (size <= 0) { fclose(fp); track_err = "Fichier vide"; return 0; }
    if (size > MAX_FILE_SIZE) { fclose(fp); track_err = "Fichier trop gros (18 Mo max)"; return 0; }
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

/* Onglets MUSIQUE / RADIO en haut de la liste (Z pour changer). */
static void draw_tabs(const char *count) {
    static const char *names[2] = { "MUSIQUE", "RADIO" };
    f32 x = 22;
    for (int t = 0; t < 2; t++) {
        int w = text_width(F_SMALL, names[t], 1) + 22;
        int on = (tab == t);
        if (on) {
            gx_glow(x, 86, (f32)w, 18, 9, RGBA(150, 80, 255, 110), 2);
            gx_rrect_fill(x, 86, (f32)w, 18, 9, RGBA(124, 54, 240, 255), RGBA(80, 26, 180, 255));
        }
        gx_rrect_stroke(x, 86, (f32)w, 18, 9, 1.2f, on ? RGBA(190, 120, 255, 255) : RGBA(90, 64, 170, 255));
        gx_text(F_SMALL, x + 11, 99, names[t], on ? RGBA(255, 255, 255, 255) : RGBA(150, 135, 210, 255),
                on ? RGBA(255, 255, 255, 255) : RGBA(150, 135, 210, 255), 1);
        x += w + 6;
    }
    /* rappel de la touche Z */
    gx_rrect_fill(x + 2, 88, 14, 14, 4, RGBA(110, 90, 190, 255), RGBA(70, 50, 140, 255));
    gx_text_c(F_SMALL, x + 9, 99, "Z", RGBA(255, 255, 255, 255));
    gx_text_r(F_SMALL, 262, 99, count, RGBA(205, 195, 245, 255));
    gx_rect(25, 110, 241, 1, RGBA(76, 52, 150, 200));
}

static void draw_list_radio(void) {
    int start = sel_station - LIST_ROWS / 2;
    if (start > nstations - LIST_ROWS) start = nstations - LIST_ROWS;
    if (start < 0) start = 0;
    char buf[160], t[16];

    draw_panel(11, 73, 271, 385);
    snprintf(buf, sizeof(buf), "%d STATIONS", nstations);
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

static void draw(void) {
    dur_budget = 1;
    begin_frame();
    draw_header();
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
    snprintf(buf, sizeof(buf), "%d PISTES", nfiles);
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
        fit_text(F_MED, display_name(names[k]), name_w, nm, sizeof(nm));
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
        fit_text(F_LARGE, display_name(names[cur_track]), 160, buf, sizeof(buf));
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

        if (down & PAD_BUTTON_START) break;

        /* onglets : la lecture en cours continue */
        if (down & PAD_TRIGGER_Z) {
            tab = (tab == TAB_MUSIC) ? TAB_RADIO : TAB_MUSIC;
            if (tab == TAB_RADIO) net_start_async();
        }

        /* navigation dans la liste de l'onglet, avec repetition */
        int count = (tab == TAB_MUSIC) ? nfiles : nstations;
        int *sel = (tab == TAB_MUSIC) ? &sel_track : &sel_station;
        u32 nav = held & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
        int fire = 0;
        if (down & nav) { fire = 1; rep_mask = nav; rep_next = now + 400; }
        else if (nav && nav == rep_mask && now >= rep_next) { fire = 1; rep_next = now + 70; }
        else if (!nav) rep_mask = 0;
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
        if (down & PAD_BUTTON_Y) {
            if (src == SRC_FILE && playing) paused = !paused;
            else if (src == SRC_RADIO) muted = !muted;
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

        service_audio();

        /* musique : decodage du fichier en cours */
        if (src == SRC_FILE && playing && !paused) {
            int k = 0;
            while (!eof && k < 2 && f_count < 48000 * 2 && (FIFO_SIZE - f_count) >= FIFO_MARGIN) {
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

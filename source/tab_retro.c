// GC-MP3 : onglet RETRO (inclus dans main.c)
// Musiques de jeux video, rangees dans sd:/retro (sous-dossiers compris) :
//  - puces sonores emulees par Game Music Emu : SPC (Super Nintendo), NSF/NSFE (NES),
//    GBS (Game Boy), VGM/VGZ/GYM (Mega Drive, Master System...), HES, KSS, AY, SAP
//  - formats des jeux GameCube : DSP (ADPCM Nintendo, mono ou paire _L/_R),
//    HPS (HAL : Melee, Kirby Air Ride), ADP (flux DTK : Zelda Wind Waker...)
//  - modules tracker : MOD, XM, IT, S3M (libxmp-lite)
// Les morceaux qui bouclent sont joues deux fois puis s'eteignent en fondu.
#include "gme.h"
#include "xmp.h"

#define RETRO_FADE_MS   8000
#define RETRO_DEFAULT_S 150                 /* duree par defaut d'un morceau sans fin (NSF...) */

/* --- etat du decodeur en cours --- */
static Music_Emu *gme = NULL;
static int gme_track = 0, gme_tracks = 0;
static xmp_context xmpc = NULL;
static int xmp_loaded = 0;
static int retro_len_ms = 0;                /* duree jouee (fondu compris) */
static long retro_pos = 0;                  /* echantillons de sortie produits */
static u32 retro_rate = 48000;
static char retro_sys[40];                  /* console */

/* --- DSP / HPS / ADP (GameCube) --- */
enum { GC_DSP, GC_HPS, GC_ADP };
typedef struct { s16 coef[16]; int h1, h2; } dspch_t;
static int gc_kind = -1, gc_ch = 1;
static dspch_t gc_d[2];
static FILE *gc_f[2] = { NULL, NULL };       /* DSP en paire : un fichier par voie */
static long gc_data = 0, gc_end = 0;        /* zone des donnees (DSP) */
static long gc_loop = -1;                   /* debut de boucle (octets, DSP / HPS) */
static int gc_loops_done = 0;
static long gc_block = 0, gc_first_block = 0, gc_next_block = 0;  /* HPS */
static u8 gc_buf[2][16384];
static int gc_len = 0, gc_pos = 0;          /* donnees du bloc courant (par voie) */
static int adp_h[2][2];                     /* ADP : historique par voie */
static long gc_samples_total = 0;

static u16 rb16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static u32 rb32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

/* v26 : libxmp complete = plus de 50 formats de modules (Amiga, PC...) */
static const char *const retro_xmp_ext[] = { ".mod", ".m15", ".nst", ".wow", ".st", ".xm", ".oxm", ".it", ".s3m", ".stm", ".stx",
    ".mtm", ".669", ".far", ".ult", ".ptm", ".okt", ".okta", ".med", ".mmd0", ".mmd1", ".mmd2", ".mmd3", ".mmdc", ".dbm", ".digi",
    ".dsym", ".dtm", ".emod", ".fnk", ".gdm", ".imf", ".ims", ".liq", ".mdl", ".mfp", ".mgt", ".psm", ".j2b", ".rtm", ".sfx",
    ".sym", ".umx", ".amf", ".abk", ".xmf", ".ice", ".flt", ".hmn", ".mus", NULL };
static int retro_ext_fmt(const char *path) {
    static const char *const g[] = { ".spc", ".nsf", ".nsfe", ".gbs", ".vgm", ".vgz", ".gym", ".hes", ".kss", ".ay", ".sap" };
    static const char *const c[] = { ".dsp", ".hps", ".adp" };
    const char *dot = strrchr(path, '.');
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (dot) {
        for (unsigned i = 0; i < sizeof(g) / sizeof(g[0]); i++) if (!strcasecmp(dot, g[i])) return FMT_GME;
        for (int i = 0; retro_xmp_ext[i]; i++) if (!strcasecmp(dot, retro_xmp_ext[i])) return FMT_XMP;
        for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) if (!strcasecmp(dot, c[i])) return FMT_GCN;
    }
    /* noms a l'Amiga : le format AVANT le nom ("mod.ma_musique", "med.titre") */
    static const char *const pre[] = { "mod.", "med.", "mmd0.", "mmd1.", "mmd2.", "mmd3.", "okt.", "okta.", "dbm.", "digi.", "dsym.",
                                       "emod.", "fnk.", "ims.", "mfp.", "sfx.", "xm.", "it.", "s3m.", "stk.", "nst.", "ust.", NULL };
    static const char *const other[] = { ".mp3", ".flac", ".ogg", ".oga", ".wav", ".m4a", ".m4b", ".jpg", ".jpeg", ".png", ".gif",
                                         ".bmp", ".txt", ".lrc", ".nfo", ".zip", ".rar", ".7z", ".pdf", ".avi", ".dol", ".iso", NULL };
    if (dot) for (int i = 0; other[i]; i++) if (!strcasecmp(dot, other[i])) return -1;    /* ("it.mp3" reste un MP3) */
    for (int i = 0; pre[i]; i++) if (!strncasecmp(base, pre[i], strlen(pre[i])) && base[strlen(pre[i])]) return FMT_XMP;
    return -1;
}

static const char *retro_system(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "";
    if (!strcasecmp(dot, ".spc")) return "Super Nintendo";
    if (!strcasecmp(dot, ".nsf") || !strcasecmp(dot, ".nsfe")) return "NES";
    if (!strcasecmp(dot, ".gbs")) return "Game Boy";
    if (!strcasecmp(dot, ".vgm") || !strcasecmp(dot, ".vgz") || !strcasecmp(dot, ".gym")) return "Sega";
    if (!strcasecmp(dot, ".hes")) return "PC Engine";
    if (!strcasecmp(dot, ".kss")) return "MSX";
    if (!strcasecmp(dot, ".ay")) return "ZX Spectrum";
    if (!strcasecmp(dot, ".sap")) return "Atari";
    if (!strcasecmp(dot, ".dsp") || !strcasecmp(dot, ".hps") || !strcasecmp(dot, ".adp")) return "GameCube";
    return "Module";
}

/* --- decodage DSP ADPCM : 8 octets = 14 echantillons --- */
static void dsp_frame(const u8 *f, dspch_t *c, s16 *out) {
    int scale = 1 << (f[0] & 15), idx = (f[0] >> 4) & 7;
    int c1 = c->coef[idx * 2], c2 = c->coef[idx * 2 + 1];
    for (int i = 0; i < 14; i++) {
        int b = f[1 + i / 2];
        int n = (i & 1) ? (b & 15) : (b >> 4);
        if (n >= 8) n -= 16;
        int s = n * scale * 2048 + 1024 + c1 * c->h1 + c2 * c->h2;
        s >>= 11;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        c->h2 = c->h1;
        c->h1 = s;
        out[i] = (s16)s;
    }
}

/* ADP (DTK) : algorithme du flux audio du lecteur de disque GameCube */
static s16 adp_sample(int bits, int q, int *h) {
    int hist = 0;
    switch (q >> 4) {
    case 1: hist = h[0] * 0x3c; break;
    case 2: hist = h[0] * 0x73 - h[1] * 0x34; break;
    case 3: hist = h[0] * 0x62 - h[1] * 0x37; break;
    }
    hist = (hist + 0x20) >> 6;
    if (hist > 0x1fffff) hist = 0x1fffff;
    if (hist < -0x200000) hist = -0x200000;
    int cur = (((s16)(bits << 12) >> (q & 15)) * 64) + hist;
    h[1] = h[0];
    h[0] = cur;
    cur >>= 6;
    if (cur > 32767) cur = 32767;
    if (cur < -32768) cur = -32768;
    return (s16)cur;
}

static void gc_close(void) {
    for (int i = 0; i < 2; i++) if (gc_f[i]) { fclose(gc_f[i]); gc_f[i] = NULL; }
    gc_kind = -1;
}

/* en-tete DSP standard (0x60 octets, gros-boutiste) */
static int dsp_header(FILE *f, dspch_t *c, u32 *rate, u32 *nib, int *loop, u32 *ls, u32 *le) {
    u8 h[0x60];
    if (fseek(f, 0, SEEK_SET) != 0 || fread(h, 1, 0x60, f) != 0x60) return 0;
    u32 ns = rb32(h), nn = rb32(h + 4), sr = rb32(h + 8);
    if (ns == 0 || nn == 0 || sr < 4000 || sr > 96000 || rb16(h + 0x0E) != 0) return 0;
    for (int i = 0; i < 16; i++) c->coef[i] = (s16)rb16(h + 0x1C + i * 2);
    c->h1 = (s16)rb16(h + 0x40);
    c->h2 = (s16)rb16(h + 0x42);
    *rate = sr; *nib = nn; *loop = rb16(h + 0x0C) != 0;
    *ls = rb32(h + 0x10); *le = rb32(h + 0x14);
    gc_samples_total = ns;
    return 1;
}

/* paire de fichiers "nom_L.dsp" / "nom_R.dsp" (ou "L.dsp" / "R.dsp") */
static int dsp_pair(const char *path, char *other, int cap) {
    int n = (int)strlen(path);
    if (n < 6) return 0;
    char c = path[n - 5];
    if (c != 'L' && c != 'l' && c != 'R' && c != 'r') return 0;
    snprintf(other, cap, "%s", path);
    other[n - 5] = (c == 'L') ? 'R' : (c == 'l') ? 'r' : (c == 'R') ? 'L' : 'l';
    struct stat st;
    return stat(other, &st) == 0;
}

static int hps_block(long off);

static int gc_open(const char *path, tags_t *tg, const char **err) {
    const char *dot = strrchr(path, '.');
    gc_close();
    gc_loops_done = 0;
    gc_len = gc_pos = 0;
    memset(adp_h, 0, sizeof(adp_h));
    if (!strcasecmp(dot, ".dsp")) {
        char other[NAME_LEN];
        u32 rate, nib, ls, le, rate2, nib2, ls2, le2;
        int loop, loop2;
        gc_f[0] = fopen(path, "rb");
        if (!gc_f[0] || !dsp_header(gc_f[0], &gc_d[0], &rate, &nib, &loop, &ls, &le)) { gc_close(); *err = "DSP illisible"; return 0; }
        gc_ch = 1;
        if (dsp_pair(path, other, sizeof(other))) {
            gc_f[1] = fopen(other, "rb");
            if (gc_f[1] && dsp_header(gc_f[1], &gc_d[1], &rate2, &nib2, &loop2, &ls2, &le2) && rate2 == rate) {
                gc_ch = 2;
                char c = path[strlen(path) - 5];
                if (c == 'R' || c == 'r') {                       /* toujours gauche puis droite */
                    FILE *t = gc_f[0]; gc_f[0] = gc_f[1]; gc_f[1] = t;
                    dspch_t d = gc_d[0]; gc_d[0] = gc_d[1]; gc_d[1] = d;
                }
            } else if (gc_f[1]) { fclose(gc_f[1]); gc_f[1] = NULL; }
        }
        retro_rate = rate;
        gc_kind = GC_DSP;
        gc_data = 0x60;
        gc_end = 0x60 + (long)(nib + 1) / 2;
        gc_loop = loop ? 0x60 + (long)(ls / 16) * 8 : -1;
        for (int i = 0; i < gc_ch; i++) fseek(gc_f[i], 0x60, SEEK_SET);
        double ms = (double)(u32)gc_samples_total * 1000 / rate;
        if (ms > 30 * 60 * 1000) ms = 30 * 60 * 1000;          /* en-tete farfelu : 30 min maxi */
        retro_len_ms = (int)(gc_loop >= 0 ? ms * 2 + RETRO_FADE_MS : ms);
        snprintf(tg->title, sizeof(tg->title), "%s", display_name(base_of(path)));
        return 1;
    }
    gc_f[0] = fopen(path, "rb");
    if (!gc_f[0]) { *err = "Ouverture impossible"; return 0; }
    if (!strcasecmp(dot, ".hps")) {
        u8 h[0x80];
        if (fread(h, 1, 0x80, gc_f[0]) != 0x80 || memcmp(h, " HALPST", 7) != 0) { gc_close(); *err = "HPS illisible"; return 0; }
        retro_rate = rb32(h + 8);
        gc_ch = (int)rb32(h + 12);
        if (gc_ch < 1 || gc_ch > 2 || retro_rate < 4000 || retro_rate > 96000) { gc_close(); *err = "HPS non pris en charge"; return 0; }
        for (int c = 0; c < gc_ch; c++) {
            for (int i = 0; i < 16; i++) gc_d[c].coef[i] = (s16)rb16(h + 0x20 + 0x38 * c + i * 2);
            gc_d[c].h1 = gc_d[c].h2 = 0;
        }
        gc_samples_total = (long)(rb32(h + 0x18) / 16 * 14);
        gc_kind = GC_HPS;
        gc_first_block = 0x80;
        /* boucle : le dernier bloc renvoie vers un bloc precedent */
        long off = gc_first_block, last = 0;
        gc_loop = -1;
        for (int guard = 0; guard < 100000 && off > last; guard++) {
            u8 b[12];
            last = off;
            if (fseek(gc_f[0], off, SEEK_SET) != 0 || fread(b, 1, 12, gc_f[0]) != 12) break;
            off = (long)(s32)rb32(b + 8);
        }
        if (off >= gc_first_block && off < last) gc_loop = off;
        double ms = (double)(u32)gc_samples_total * 1000 / retro_rate;
        if (ms > 30 * 60 * 1000) ms = 30 * 60 * 1000;
        retro_len_ms = (int)(gc_loop >= 0 ? ms * 2 + RETRO_FADE_MS : ms);
        if (!hps_block(gc_first_block)) { gc_close(); *err = "HPS illisible"; return 0; }
        snprintf(tg->title, sizeof(tg->title), "%s", display_name(base_of(path)));
        return 1;
    }
    /* ADP : blocs de 32 octets, stereo, 48 kHz */
    fseek(gc_f[0], 0, SEEK_END);
    long size = ftell(gc_f[0]);
    fseek(gc_f[0], 0, SEEK_SET);
    if (size < 32) { gc_close(); *err = "ADP illisible"; return 0; }
    gc_kind = GC_ADP;
    gc_ch = 2;
    retro_rate = 48000;
    gc_data = 0;
    gc_end = size / 32 * 32;
    gc_loop = -1;
    gc_samples_total = size / 32 * 28;
    retro_len_ms = (int)((double)gc_samples_total * 1000 / retro_rate);
    snprintf(tg->title, sizeof(tg->title), "%s", display_name(base_of(path)));
    return 1;
}

/* HPS : charge le bloc a l'adresse off (donnees de chaque voie) */
static int hps_block(long off) {
    u8 h[0x20];
    if (off <= 0 || fseek(gc_f[0], off, SEEK_SET) != 0 || fread(h, 1, 0x20, gc_f[0]) != 0x20) return 0;
    long size = (long)rb32(h);
    long per = size / gc_ch;
    if (per <= 0 || per > (long)sizeof(gc_buf[0])) return 0;
    for (int c = 0; c < gc_ch; c++)
        if (fread(gc_buf[c], 1, per, gc_f[0]) != (size_t)per) return 0;
    gc_block = off;
    gc_next_block = (long)(s32)rb32(h + 8);
    gc_len = (int)(per / 8 * 8);
    gc_pos = 0;
    return 1;
}

/* produit jusqu'a 'max' echantillons stereo ; 0 = fin */
static int gc_decode(s16 *L, s16 *R, int max) {
    int n = 0;
    s16 a[14], b[14];
    while (n + 28 <= max) {
        if (gc_kind == GC_ADP) {
            u8 f[32];
            if (ftell(gc_f[0]) >= gc_end || fread(f, 1, 32, gc_f[0]) != 32) break;
            for (int i = 0; i < 28; i++) {
                L[n + i] = adp_sample(f[4 + i] & 15, f[0], adp_h[0]);
                R[n + i] = adp_sample(f[4 + i] >> 4, f[1], adp_h[1]);
            }
            n += 28;
            continue;
        }
        if (gc_kind == GC_DSP) {
            u8 f[2][8];
            long pos = ftell(gc_f[0]);
            if (pos + 8 > gc_end) {
                if (gc_loop >= 0 && gc_loops_done < 20) {          /* on reboucle (fondu de fin) */
                    gc_loops_done++;
                    for (int c = 0; c < gc_ch; c++) fseek(gc_f[c], gc_loop, SEEK_SET);
                    continue;
                }
                break;
            }
            int ok = 1;
            for (int c = 0; c < gc_ch; c++) if (fread(f[c], 1, 8, gc_f[c]) != 8) ok = 0;
            if (!ok) break;
            dsp_frame(f[0], &gc_d[0], a);
            if (gc_ch == 2) dsp_frame(f[1], &gc_d[1], b);
            for (int i = 0; i < 14; i++) { L[n + i] = a[i]; R[n + i] = gc_ch == 2 ? b[i] : a[i]; }
            n += 14;
            continue;
        }
        /* HPS */
        if (gc_pos >= gc_len) {
            long nx = gc_next_block;
            if (nx <= gc_block) {                                 /* fin du flux : boucle ou arret */
                if (gc_loop >= 0 && gc_loops_done < 20) { gc_loops_done++; if (!hps_block(gc_loop)) break; continue; }
                break;
            }
            if (!hps_block(nx)) break;
            continue;
        }
        dsp_frame(gc_buf[0] + gc_pos, &gc_d[0], a);
        if (gc_ch == 2) dsp_frame(gc_buf[1] + gc_pos, &gc_d[1], b);
        gc_pos += 8;
        for (int i = 0; i < 14; i++) { L[n + i] = a[i]; R[n + i] = gc_ch == 2 ? b[i] : a[i]; }
        n += 14;
    }
    return n;
}

/* --- VGZ : VGM compresse (gzip), decompresse avec le zlib de stb_image --- */
static unsigned char *gunzip_file(const char *path, int *outlen) {
    long len = 0;
    unsigned char *d = read_file(path, 8 * 1024 * 1024, &len);
    if (!d) return NULL;
    if (len < 18 || d[0] != 0x1f || d[1] != 0x8b || d[2] != 8) { *outlen = (int)len; return d; }   /* pas compresse */
    int flg = d[3];
    long p = 10;
    if (flg & 4) { if (p + 2 > len) { free(d); return NULL; } p += 2 + (d[p] | (d[p + 1] << 8)); }
    if (flg & 8) { while (p < len && d[p]) p++; p++; }
    if (flg & 16) { while (p < len && d[p]) p++; p++; }
    if (flg & 2) p += 2;
    if (p >= len) { free(d); return NULL; }
    char *out = stbi_zlib_decode_noheader_malloc((const char *)d + p, (int)(len - p), outlen);
    free(d);
    if (out && *outlen > 32 * 1024 * 1024) { free(out); return NULL; }
    return (unsigned char *)out;
}

/* --- VGM : game-music-emu fait confiance au fichier (positions, tailles).
   On verifie tout avant : horloges, positions de l'en-tete, chaque commande
   et les lectures d'echantillons. Un fichier abime est coupe ou refuse. --- */
static u32 rl32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void wl32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static int vgm_cmd_len(const u8 *d, long p, long n) {
    int c = d[p];
    switch (c) {
    case 0x66: case 0x62: case 0x63: return 1;
    case 0x4F: case 0x50: case 0x3F: case 0x30: case 0x64: return 2;
    case 0x61: case 0x51: case 0x52: case 0x53: case 0xA1: case 0xA2: case 0xA3: return 3;
    case 0xE0: return 5;
    case 0x67:
        if (p + 7 > n) return -1;
        { u32 sz = rl32(d + p + 3); if (sz > (u32)(n - p - 7)) return -1; return 7 + (int)sz; }
    }
    switch (c >> 4) {
    case 0x3: case 0x4: return 2;
    case 0x5: case 0xA: case 0xB: return 3;
    case 0xC: case 0xD: return 4;
    case 0xE: case 0xF: return 5;
    }
    return 1;
}

/* parcourt le flux de 'from' a 'end' ; 0 si une lecture sort du fichier */
static int vgm_walk(const u8 *d, long n, long from, long end, long *pcm_base, long *pcm_pos, u8 *bound, long *bad) {
    long p = from;
    while (p < end) {
        int len = vgm_cmd_len(d, p, n);
        if (len < 0 || p + len > n) { *bad = p; return 0; }
        if (bound) bound[p] = 1;
        int c = d[p];
        if (c == 0x66) return 1;
        if (c == 0x67 && d[p + 2] == 0) *pcm_base = p + 7;
        else if (c == 0xE0) {
            u32 off = rl32(d + p + 1);
            if (off >= (u32)(n - *pcm_base)) { *bad = p; return 0; }
            *pcm_pos = *pcm_base + (long)off;
        } else if ((c & 0xF0) == 0x80) {
            if (*pcm_pos < 0 || *pcm_pos >= n) { *bad = p; return 0; }
            (*pcm_pos)++;
        }
        p += len;
    }
    return 1;
}

static int vgm_sanitize(u8 *d, int n) {
    if (n < 0x40) return 0;
    /* horloges des puces : 0 (absente) ou entre 1 et 10 MHz, comme les vraies */
    u32 psg = rl32(d + 0x0C), ym2413 = rl32(d + 0x10) & 0x3FFFFFFF, ym2612 = rl32(d + 0x2C) & 0x3FFFFFFF;
    if (psg && (psg < 1000000 || psg > 10000000)) return 0;
    if (ym2413 && (ym2413 < 1000000 || ym2413 > 10000000)) return 0;
    if (ym2612 && (ym2612 < 1000000 || ym2612 > 10000000)) return 0;
    if (d[0x2A] > 32) d[0x2A] = 0;                            /* largeur du bruit : valeur par defaut */
    long start = 0x40;
    if (rl32(d + 0x08) >= 0x150 && rl32(d + 0x34)) {
        u32 off = rl32(d + 0x34);
        if (off > (u32)n) return 0;
        start = 0x34 + (long)off;
    }
    if (start < 0x40 || start >= n) return 0;
    u32 g = rl32(d + 0x14);
    if (g && (g > (u32)n || 0x14 + (long)g + 12 > n)) wl32(d + 0x14, 0);
    u8 *bound = (u8 *)calloc((size_t)n, 1);
    if (!bound) return 0;
    long base = start, pos = start, bad = -1, end = n;
    if (!vgm_walk(d, n, start, n, &base, &pos, bound, &bad)) { d[bad] = 0x66; end = bad; }   /* on coupe ici */
    /* puces vraiment actives (meme logique que game-music-emu) : les ecritures
       vers une puce absente deviennent une commande neutre de meme taille
       (0x5F ou 0x31). Le double PSG n'est pas accepte (horloge <= 10 MHz). */
    u32 r2612 = rl32(d + 0x2C), r2413 = rl32(d + 0x10);
    if (r2413 && rl32(d + 0x08) < 0x110) {
        for (long p = 0x40; p < end && d[p] != 0x66; p += vgm_cmd_len(d, p, n)) {
            if (!bound[p]) break;
            if (d[p] == 0x51) { r2612 = 0; break; }
            if (d[p] == 0x52 || d[p] == 0x53) { r2612 = r2413; r2413 = 0; break; }
            if (d[p] == 0x54) { r2612 = r2413 = 0; break; }
        }
    }
    int fm12a = r2612 != 0, fm12b = fm12a && (r2612 & 0x40000000);
    int fm13a = !fm12a && r2413 != 0, fm13b = fm13a && (r2413 & 0x40000000);
    for (long p = start; p < end; p += vgm_cmd_len(d, p, n)) {
        int c = d[p];
        if (c == 0x66) break;
        if (((c == 0x52 || c == 0x53) && !fm12a) || ((c == 0xA2 || c == 0xA3) && !fm12b)
            || (c == 0x51 && !fm13a) || (c == 0xA1 && !fm13b)) d[p] = 0x5F;
        else if (c == 0x30 || c == 0x3F) d[p] = 0x31;            /* second PSG : jamais actif ici */
    }
    /* boucle : sur une commande deja verifiee, et rejouable sans sortir du fichier */
    u32 lo = rl32(d + 0x1C);
    if (lo) {
        long lp = 0x1C + (long)lo;
        int ok = lo < (u32)n && lp < end && bound[lp];
        if (ok) {
            /* une lecture d'echantillon dans la boucle doit suivre un saut (E0) de la boucle */
            int seek = 0, wait = 0;
            for (long p = lp; p < end && d[p] != 0x66; p += vgm_cmd_len(d, p, n)) {
                int c = d[p];
                if (c == 0xE0) seek = 1;
                if ((c & 0xF0) == 0x80 && !seek) { ok = 0; break; }
                if (c == 0x62 || c == 0x63 || (c & 0xF0) == 0x70 || ((c & 0xF0) == 0x80 && (c & 15))
                    || (c == 0x61 && (d[p + 1] | d[p + 2])) || (c == 0x64 && d[p + 1])) wait = 1;
            }
            if (!wait) ok = 0;                                    /* boucle sans attente : sans fin */
            long b2 = base, p2 = pos, bad2 = -1;
            if (ok && !vgm_walk(d, n, lp, end, &b2, &p2, NULL, &bad2)) ok = 0;
        }
        if (!ok) wl32(d + 0x1C, 0);
    }
    free(bound);
    return 1;
}

/* --- ouverture d'un morceau retro (appelee par start_track) --- */
static void retro_close(void) {
    if (gme) { gme_delete(gme); gme = NULL; }
    if (xmpc) {
        if (xmp_loaded) { xmp_end_player(xmpc); xmp_release_module(xmpc); }
        xmp_free_context(xmpc);
        xmpc = NULL;
        xmp_loaded = 0;
    }
    gc_close();
}

static int gme_start(int track, tags_t *tg) {
    if (gme_start_track(gme, track) != NULL) return 0;
    gme_track = track;
    gme_info_t *info = NULL;
    int len = RETRO_DEFAULT_S * 1000;
    if (gme_track_info(gme, &info, track) == NULL && info) {
        if (info->length > 0) len = info->length;
        else if (info->loop_length > 0) len = info->intro_length + info->loop_length * 2;
        if (tg) {
            if (info->song[0]) snprintf(tg->title, sizeof(tg->title), "%s", info->song);
            snprintf(tg->artist, sizeof(tg->artist), "%s", info->game[0] ? info->game : info->system);
            if (info->author[0]) snprintf(tg->album, sizeof(tg->album), "%s", info->author);
            if (info->copyright[0]) snprintf(tg->year, sizeof(tg->year), "%.4s", info->copyright);
        }
        gme_free_info(info);
    }
    if (len < 5000) len = 5000;
    if (len > 30 * 60 * 1000) len = 30 * 60 * 1000;
    gme_set_fade_msecs(gme, len, RETRO_FADE_MS);
    retro_len_ms = len + RETRO_FADE_MS;
    return 1;
}

static int retro_open(const char *path, int fmt, tags_t *tg, const char **err) {
    retro_close();
    retro_pos = 0;
    retro_rate = 48000;
    snprintf(retro_sys, sizeof(retro_sys), "%s", retro_system(path));
    if (fmt == FMT_GME) {
        int n = 0;
        unsigned char *data = gunzip_file(path, &n);
        if (!data) { *err = "Fichier illisible"; return 0; }
        if (n >= 4 && !memcmp(data, "Vgm ", 4) && !vgm_sanitize(data, n)) { free(data); *err = "Fichier VGM invalide"; return 0; }
        gme_err_t e = gme_open_data(data, n, &gme, 48000);
        free(data);
        if (e || !gme) { gme = NULL; *err = "Format non reconnu"; return 0; }
        gme_tracks = gme_track_count(gme);
        snprintf(tg->title, sizeof(tg->title), "%s", display_name(base_of(path)));
        if (!gme_start(0, tg)) { retro_close(); *err = "Lecture impossible"; return 0; }
        return 1;
    }
    if (fmt == FMT_XMP) {
        xmpc = xmp_create_context();
        if (!xmpc) { *err = "M\xC3\xA9moire insuffisante"; return 0; }
        if (xmp_load_module(xmpc, (char *)path) != 0) { retro_close(); *err = "Module illisible"; return 0; }
        xmp_loaded = 1;
        if (xmp_start_player(xmpc, 48000, 0) != 0) { retro_close(); *err = "Lecture impossible"; return 0; }
        xmp_set_player(xmpc, XMP_PLAYER_INTERP, XMP_INTERP_LINEAR);
        struct xmp_module_info mi;
        struct xmp_frame_info fi;
        xmp_get_module_info(xmpc, &mi);
        xmp_get_frame_info(xmpc, &fi);
        retro_len_ms = fi.total_time > 0 ? fi.total_time : RETRO_DEFAULT_S * 1000;
        if (mi.mod && mi.mod->name[0]) snprintf(tg->title, sizeof(tg->title), "%s", mi.mod->name);
        else snprintf(tg->title, sizeof(tg->title), "%s", display_name(base_of(path)));
        snprintf(tg->artist, sizeof(tg->artist), "%s", mi.mod ? mi.mod->type : "Module");
        gme_tracks = 1;
        return 1;
    }
    gme_tracks = 1;
    if (!gc_open(path, tg, err)) return 0;
    snprintf(tg->artist, sizeof(tg->artist), "GameCube (%s)", gc_kind == GC_DSP ? "DSP" : gc_kind == GC_HPS ? "HPS" : "ADP");
    return 1;
}

/* produit le son du morceau en cours */
static void retro_decode(void) {
    static s16 buf[2048 * 2], L[2048], R[2048];
    int n = 0;
    if (gme) {
        if (gme_track_ended(gme)) { eof = 1; return; }
        if (gme_play(gme, 2048 * 2, buf) != NULL) { eof = 1; return; }
        n = 2048;
        for (int i = 0; i < n; i++) { L[i] = buf[2 * i]; R[i] = buf[2 * i + 1]; }
        push_frame(L, R, n, 48000);
    } else if (xmpc) {
        int r = xmp_play_buffer(xmpc, buf, 2048 * 4, 1);
        if (r != 0) { eof = 1; return; }
        n = 2048;
        for (int i = 0; i < n; i++) { L[i] = buf[2 * i]; R[i] = buf[2 * i + 1]; }
        push_frame(L, R, n, 48000);
    } else if (gc_kind >= 0) {
        n = gc_decode(L, R, 2048);
        if (n <= 0) { eof = 1; return; }
        /* fondu de fin quand le morceau boucle */
        long fade_start = (long)((double)(retro_len_ms - RETRO_FADE_MS) * retro_rate / 1000);
        if (gc_loop >= 0 && retro_pos + n > fade_start) {
            long fade_len = (long)RETRO_FADE_MS * retro_rate / 1000;
            for (int i = 0; i < n; i++) {
                long p = retro_pos + i - fade_start;
                if (p < 0) continue;
                float g = 1.0f - (float)p / (float)fade_len;
                if (g <= 0) { n = i; eof = 1; break; }
                L[i] = (s16)(L[i] * g);
                R[i] = (s16)(R[i] * g);
            }
        }
        if (n > 0) push_frame(L, R, n, retro_rate);
    } else { eof = 1; return; }
    retro_pos += n;
}

static int retro_seek(int sec) {
    if (gme) { gme_seek(gme, sec * 1000); return 1; }
    if (xmpc) { xmp_seek_time(xmpc, sec * 1000); return 1; }
    return 0;                                                 /* (DSP / HPS / ADP : pas de saut) */
}

/* duree d'un fichier retro sans le decoder (pour la liste) */
static int retro_quick_duration(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return -1;
    if (!strcasecmp(dot, ".dsp")) {
        u8 h[16];
        FILE *f = fopen(path, "rb");
        if (!f) return -1;
        int ok = fread(h, 1, 16, f) == 16;
        fclose(f);
        if (!ok) return -1;
        u32 ns = rb32(h), sr = rb32(h + 8);
        if (sr < 4000 || sr > 96000) return -1;
        return (int)(ns / sr) * ((h[12] | h[13]) ? 2 : 1) + ((h[12] | h[13]) ? RETRO_FADE_MS / 1000 : 0);
    }
    if (!strcasecmp(dot, ".spc")) {                             /* etiquette ID666 (texte) */
        u8 h[0xB0];
        FILE *f = fopen(path, "rb");
        if (!f) return -1;
        int ok = fread(h, 1, sizeof(h), f) == sizeof(h);
        fclose(f);
        if (!ok || h[0x23] != 26) return -1;
        char s[4] = { (char)h[0xA9], (char)h[0xAA], (char)h[0xAB], 0 };
        int v = atoi(s);
        return v > 0 ? v + RETRO_FADE_MS / 1000 : -1;
    }
    return -1;
}

/* L / R : piste suivante dans un NSF / GBS a plusieurs morceaux */
static int retro_subsong_step_u(int d);
static int retro_subsong_step(int d) { DEC_LOCK(); int r_ = retro_subsong_step_u(d); DEC_UNLOCK(); return r_; }   /* v157 */
static int retro_subsong_step_u(int d) {
    if (!gme || gme_tracks <= 1) return 0;
    int t = gme_track + d;
    if (t < 0 || t >= gme_tracks) return 0;
    tags_t tg;
    tags_clear(&tg);
    snprintf(tg.title, sizeof(tg.title), "Piste %d", t + 1);
    if (!gme_start(t, &tg)) return 0;
    f_head = f_tail = f_count = 0;
    rpos = 0.0; prevL = prevR = 0;
    consumed = 0;
    retro_pos = 0;
    eof = 0;
    snprintf(id3_title, sizeof(id3_title), "%s", tg.title);
    if (tg.artist[0]) snprintf(artist, sizeof(artist), "%s", tg.artist);
    if (cur_track >= 0) dur_cache[cur_track] = retro_len_ms / 1000;
    return 1;
}

/* --- liste de l'onglet --- */
static s16 retro_list[MAX_FILES];
static int retro_n = 0, retro_sel = 0;

static int rz_n = 0;                                  /* v26 : musiques sorties des .zip au dernier passage */
static void retro_rescan(void) {
    retro_n = 0;
    if (rz_n) { char b[96]; snprintf(b, sizeof(b), "%d musiques sorties des archives .zip", rz_n); lib_note(b); rz_n = 0; }
    for (int k = 0; k < nfiles; k++) {
        if (!is_retro[k]) continue;
        int n = (int)strlen(names[k]);
        char other[NAME_LEN];
        if (n > 5 && !strcasecmp(names[k] + n - 4, ".dsp") && (names[k][n - 5] == 'R' || names[k][n - 5] == 'r') &&
            dsp_pair(names[k], other, sizeof(other))) continue;          /* voie droite d'une paire : cachee */
        retro_list[retro_n++] = (s16)k;
    }
    if (retro_sel >= retro_n) retro_sel = retro_n > 0 ? retro_n - 1 : 0;
}

static void retro_enter(void) { retro_rescan(); }

static void retro_input(u32 down, u32 nav, int fire) {
    if (fire && retro_n > 0) {
        if (nav & PAD_BUTTON_DOWN) retro_sel++;
        if (nav & PAD_BUTTON_UP) retro_sel--;
        if (nav & PAD_BUTTON_RIGHT) retro_sel += MUSIC_ROWS;
        if (nav & PAD_BUTTON_LEFT) retro_sel -= MUSIC_ROWS;
        if (retro_sel < 0) retro_sel = 0;
        if (retro_sel > retro_n - 1) retro_sel = retro_n - 1;
    }
    if ((down & PAD_BUTTON_A) && retro_n > 0) { int k = retro_list[retro_sel]; queue_build(k); start_track(k); }
    if (down & PAD_BUTTON_B) { stop_track(); stop_station(); track_err = NULL; err_msg[0] = 0; }
    if ((down & PAD_BUTTON_X) && sd_ok) { music_rescan(); retro_rescan(); }
    if ((down & PAD_BUTTON_Y) && src == SRC_FILE && playing) paused = !paused;
}

static void retro_pump(void) {}

static void draw_list_retro(void) {
    char buf[NAME_LEN], t[32];
    draw_panel(11, 73, 271, 385);
    snprintf(buf, sizeof(buf), "%d", retro_n);
    draw_tabs(buf);
    int start = retro_sel - MUSIC_ROWS / 2;
    if (start > retro_n - MUSIC_ROWS) start = retro_n - MUSIC_ROWS;
    if (start < 0) start = 0;
    static f32 sel_anim = -2000;
    if (retro_sel >= start && retro_sel < start + MUSIC_ROWS && retro_sel < retro_n) draw_sel_bar(&sel_anim, 114.0f + (retro_sel - start) * 24.0f);
    for (int i = 0; i < MUSIC_ROWS; i++) {
        int r = start + i;
        if (r >= retro_n) break;
        int k = retro_list[r];
        f32 ry = 114.0f + i * 24.0f, cy = ry + 12.0f;
        int is_sel = (r == retro_sel), is_cur = (playing && k == cur_track);
        gp_zone(18, ry + 1, 250, 22, &retro_sel, r, GPZ_SEL);
        col_t txt = is_sel ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
        col_t dim = is_sel ? RGBA(235, 225, 255, 255) : RGBA(128, 112, 190, 255);
        if (is_cur) gx_tri(26, cy - 6, 26, cy + 6, 36, cy, RGBA(255, 255, 255, 255));
        const char *sys = retro_system(names[k]);
        snprintf(t, sizeof(t), "%s", !strcmp(sys, "Super Nintendo") ? "SNES" : !strcmp(sys, "GameCube") ? "GC" :
                 !strcmp(sys, "Game Boy") ? "GB" : !strcmp(sys, "ZX Spectrum") ? "ZX" : !strcmp(sys, "PC Engine") ? "PCE" : sys);
        int tw = text_width(F_SMALL, t, 0) + 8;
        gx_rrect_fill(256 - tw, cy - 7, (f32)tw, 14, 4, RGBA(70, 46, 150, 255), RGBA(50, 30, 110, 255));
        gx_text_c(F_SMALL, 256 - tw / 2.0f, cy + 4, t, RGBA(230, 220, 255, 255));
        char raw[NAME_LEN], d[NAME_LEN];
        snprintf(raw, sizeof(raw), "%s", display_name(base_of(names[k])));
        char *dot = strrchr(raw, '.');
        if (dot) *dot = 0;
        text_display(raw, d, sizeof(d));
        fit_text_sel(is_sel, F_MED, d, 210 - tw, buf, sizeof(buf));
        gx_text1(F_MED, 42, cy + 5, buf, txt);
        (void)dim;
    }
    if (retro_n == 0) {
        gx_text1(F_MED, 30, 138, "Aucune musique r\xC3\xA9tro", RGBA(255, 205, 110, 255));
        gx_text1(F_SMALL, 30, 160, "Copie tes fichiers dans SD:/retro :", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 30, 178, "SPC, NSF, GBS, VGM/VGZ, HES, KSS, AY, SAP,", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 30, 196, "DSP, HPS, ADP (GameCube),", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 30, 214, "MOD, XM, IT, S3M (modules)", RGBA(190, 180, 235, 255));
    }
    gx_rect(25, 430, 241, 1, RGBA(76, 52, 150, 200));
    if (src == SRC_FILE && playing && gme && gme_tracks > 1) snprintf(buf, sizeof(buf), "Piste %d / %d   (L / R)", gme_track + 1, gme_tracks);
    else snprintf(buf, sizeof(buf), "%s", playing && cur_track >= 0 && is_retro[cur_track] ? retro_sys : "Musiques de jeux vid\xC3\xA9o");
    gx_text1(F_SMALL, 26, 447, buf, RGBA(170, 155, 225, 255));
    if (retro_n > MUSIC_ROWS) {
        f32 th = 308.0f * MUSIC_ROWS / retro_n, ty = 116.0f + (308.0f - th) * (f32)start / (f32)(retro_n - MUSIC_ROWS);
        gx_rrect_fill(270, ty, 3, th, 1.5f, RGBA(150, 96, 255, 255), RGBA(110, 60, 230, 255));
    }
}

/* ---------- v26 : archives .zip dans SD:/retro ----------
   Les packs de musiques (SPC, NSF, VGM...) arrivent souvent en .zip : au
   premier passage, ils sont decompresses dans un dossier du meme nom, a cote
   (l'archive reste ; si le dossier existe deja, on n'y touche plus). */

static u32 rz_le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u16 rz_le16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
/* v201 : decompresse une archive .zip dans dir (cree) ; music : seulement les musiques, a plat (SD:/retro) ;
   sinon tous les fichiers avec leurs dossiers (« .. » et noms interdits nettoyes). -> nombre de fichiers ecrits ;
   *skipped : fichiers sautes (trop gros pour la memoire, methode inconnue) */
static int zip_extract(const char *zpath, const char *dir, int music, int *skipped) {
    if (skipped) *skipped = 0;
    FILE *f = fopen(zpath, "rb");
    if (!f) return -1;
    long fsz = 0;
    if (fseek(f, 0, SEEK_END) == 0) fsz = ftell(f);
    long tail = fsz < 65557 ? fsz : 65557;
    u8 *t = tail > 22 ? (u8 *)malloc(tail) : NULL;
    int out = 0;
    if (!t || fseek(f, fsz - tail, SEEK_SET) != 0 || fread(t, 1, tail, f) != (size_t)tail) { free(t); fclose(f); return -1; }
    long e = -1;
    for (long i = tail - 22; i >= 0; i--) if (rz_le32(t + i) == 0x06054b50u) { e = i; break; }
    if (e < 0) { free(t); fclose(f); return -1; }
    u32 cnt = rz_le16(t + e + 10), cds = rz_le32(t + e + 12), cdo = rz_le32(t + e + 16);
    free(t);
    if (!cnt || cds > (8u << 20) || (u64)cdo + cds > (u64)fsz) { fclose(f); return -1; }
    u8 *cd = (u8 *)malloc(cds);
    if (!cd || fseek(f, cdo, SEEK_SET) != 0 || fread(cd, 1, cds, f) != cds) { free(cd); fclose(f); return -1; }
    int made = 0;
    u32 p = 0;
    for (u32 n = 0; n < cnt && p + 46 <= cds; n++) {
        if (rz_le32(cd + p) != 0x02014b50u) break;
        u16 meth = rz_le16(cd + p + 10), nl = rz_le16(cd + p + 28), el = rz_le16(cd + p + 30), cl = rz_le16(cd + p + 32);
        u32 csz = rz_le32(cd + p + 20), usz = rz_le32(cd + p + 24), lho = rz_le32(cd + p + 42);
        if (p + 46 + nl > cds) break;
        char name[NAME_LEN];
        int k = nl < (int)sizeof(name) - 1 ? nl : (int)sizeof(name) - 1;
        memcpy(name, cd + p + 46, k); name[k] = 0;
        p += 46 + nl + el + cl;
        for (char *q = name; *q; q++) if (*q == '\\') *q = '/';
        const char *b = strrchr(name, '/');
        b = b ? b + 1 : name;
        if (!b[0]) continue;                                    /* (dossier) */
        if (music && (b[0] == '.' || (retro_ext_fmt(b) < 0 && !audio_ext_len(b)))) continue;     /* seulement de la musique */
        if (!music && (!strcmp(b, ".DS_Store") || !strncmp(name, "__MACOSX/", 9))) continue;
        u32 lim = music ? (24u << 20) : (12u << 20);
        if ((meth != 0 && meth != 8) || usz > lim || csz > lim) { if (skipped) (*skipped)++; continue; }
        /* chemin de sortie : dossiers de l'archive (sans « .. ») */
        char op[NAME_LEN];
        int ol = snprintf(op, sizeof(op), "%s", dir);
        if (!music) {
            char comp[NAME_LEN]; const char *s0 = name; int bad = 0;
            while (*s0 && !bad) {
                const char *sl = strchr(s0, '/');
                int cl2 = sl ? (int)(sl - s0) : (int)strlen(s0);
                if (cl2 > 0 && !(cl2 == 1 && s0[0] == '.') && !(cl2 == 2 && s0[0] == '.' && s0[1] == '.')) {
                    if (cl2 > (int)sizeof(comp) - 1) cl2 = (int)sizeof(comp) - 1;
                    memcpy(comp, s0, cl2); comp[cl2] = 0;
                    sanitize_name(comp);
                    if (!comp[0]) snprintf(comp, sizeof(comp), "_");
                    if (!made) { mkdir(dir, 0777); made = 1; }
                    int r = snprintf(op + ol, sizeof(op) - ol, "/%s", comp);
                    if (r >= (int)sizeof(op) - ol) { bad = 1; break; }
                    ol += r;
                    if (sl) mkdir(op, 0777);
                }
                if (!sl) break;
                s0 = sl + 1;
            }
            if (bad) { if (skipped) (*skipped)++; continue; }
        } else {
            char safe[200];
            snprintf(safe, sizeof(safe), "%s", b);
            sanitize_name(safe);
            if (!safe[0] || snprintf(op + ol, sizeof(op) - ol, "/%s", safe) >= (int)sizeof(op) - ol) continue;
        }
        u8 lh[30];
        if (fseek(f, lho, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30 || rz_le32(lh) != 0x04034b50u) continue;
        if (fseek(f, lho + 30 + rz_le16(lh + 26) + rz_le16(lh + 28), SEEK_SET) != 0) continue;
        u8 *c = (u8 *)malloc(csz ? csz : 1), *u = meth == 8 ? (u8 *)malloc(usz ? usz : 1) : c;
        if (!c || !u || fread(c, 1, csz, f) != csz) { free(c); if (u != c) free(u); if (skipped) (*skipped)++; continue; }
        int ok = meth == 0 ? csz == usz : (usz == 0 || stbi_zlib_decode_noheader_buffer((char *)u, (int)usz, (const char *)c, (int)csz) == (int)usz);
        if (ok) {
            if (!made) { mkdir(dir, 0777); made = 1; }
            FILE *w = fopen(op, "wb");
            if (w) { if (usz == 0 || fwrite(u, 1, usz, w) == usz) out++; fclose(w); }
        } else if (skipped) (*skipped)++;
        free(c); if (u != c) free(u);
        service_audio();
    }
    free(cd);
    fclose(f);
    return out;
}
static int retro_unzip(const char *zpath) {
    char dir[NAME_LEN];
    int zl = (int)strlen(zpath);
    if (zl < 5 || zl - 4 >= (int)sizeof(dir)) return 0;
    memcpy(dir, zpath, zl - 4); dir[zl - 4] = 0;
    struct stat st;
    if (stat(dir, &st) == 0) return 0;                  /* deja fait */
    int r = zip_extract(zpath, dir, 1, NULL);
    return r > 0 ? r : 0;
}
static void retro_unzip_tree(const char *dir, int depth) {
    if (!sd_ok) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char path[NAME_LEN];
    char (*zips)[NAME_LEN] = malloc(16 * NAME_LEN), (*subs)[NAME_LEN] = malloc(16 * NAME_LEN);   /* (pas sur la pile) */
    int nz = 0, ns = 0;
    if (!zips || !subs) { free(zips); free(subs); closedir(d); return; }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path)) continue;
        const char *dot = strrchr(e->d_name, '.');
        if (dot && !strcasecmp(dot, ".zip") && nz < 16) snprintf(zips[nz++], NAME_LEN, "%s", path);
        else if (depth < 2 && ns < 16 && is_dir(path, e)) snprintf(subs[ns++], NAME_LEN, "%s", path);
    }
    closedir(d);
    for (int i = 0; i < nz; i++) rz_n += retro_unzip(zips[i]);
    for (int i = 0; i < ns; i++) retro_unzip_tree(subs[i], depth + 1);
    free(zips); free(subs);
}

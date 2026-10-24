// GC-MP3 "Indigo Audio Player" - musique (carte SD) + webradios + navigateur web pour GameCube
// (libogc + GX + minimp3 + Helix AAC + dr_flac + stb_vorbis + Broadband Adapter + mbedTLS)
//
// Onglet MUSIQUE : fichiers .mp3 .flac .ogg .wav de sd:/mp3 et sd:/downloads (sous-dossiers compris)
//   Pochettes : <meme nom>.jpg/.png, pochette integree (ID3 / FLAC), ou cover.jpg/folder.jpg du dossier
//   Paroles : <meme nom>.lrc, sinon cherchees sur lrclib.net. Volume egalise (ReplayGain),
//   egaliseur graves / aigus et fondu enchaine : menu START.
// Onglet RADIO : liste sd:/gc-mp3/radio.txt (creee au premier lancement), une station par ligne :
//   Nom | http://adresse/du/flux       Logo facultatif : sd:/gc-mp3/radio/<Nom>.jpg ou .png
//   Flux MP3, AAC ou OGG Vorbis en http:// ou https://, listes .m3u/.pls, radios HLS (.m3u8).
// Onglet WEB : navigateur en mode texte (sans JavaScript ni images). Les liens vers des
//   fichiers sont telecharges dans sd:/downloads. Favoris : sd:/gc-mp3/favoris.txt (Nom | adresse).
//
// Commandes : Z = changer d'onglet (la lecture continue), Haut/Bas = choisir,
//             Gauche/Droite = page, A = lire, B = arreter, Y = pause (musique) / muet (radio),
//             X = actualiser la liste (musique), L/R = piste ou station precedente/suivante,
//             START = menu (reglages, Quitter)
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
#include <locale.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

// Decodeur MP3 : minimp3 (fichier unique, domaine public), telecharge par le workflow
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

// Decodeur AAC / HE-AAC (webradios) : Helix (RealNetworks, licence RPSL), telecharge par le workflow
#include "aacdec.h"
// FLAC (dr_flac) et OGG Vorbis (stb_vorbis), domaine public, compiles a part dans codecs.c
#include "dr_flac.h"
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_PULLDATA_API
#include "stb_vorbis.c"
#include <time.h>

// Decodeur JPEG/PNG (logos des stations) : stb_image, telecharge par le workflow
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"
#include "src/webp/decode.h"                 /* WebP (libwebp, telecharge par le workflow) */

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
#define GCD            SD_ROOT "/gc-mp3"     // v22 : fichiers de GC-MP3 ranges dans un seul dossier
#define RADIO_LIST     GCD "/radio.txt"
#define LOGO_DIR       GCD "/radio"
#define MP3_DIR        SD_ROOT "/mp3"
#define MAX_FILES      2000                 /* v24 : 2000 morceaux (avant 512) */
#define NAME_LEN       256                  // chemin complet (sd:/dossier/.../fichier.mp3)
#define URL_MAX        2048                 // adresse web la plus longue acceptee
#define DL_DIR         SD_ROOT "/downloads" // telechargements (CubeBrowser...)
#define SCAN_DEPTH     3                    // sous-dossiers explores
#define FBUF_SIZE      (64 * 1024)          // MP3 lu par blocs depuis la carte SD
#define MAX_STATIONS   128
#define LIST_ROWS      14
#define DMA_SAMPLES    2048                 // echantillons stereo par tampon DMA (~43 ms)
#define NUM_BUFS       8                    // v22 : 8 tampons, dont 5 remplis d'avance (~210 ms)
#define AUDIO_AHEAD    5
#define DMA_BYTES      (DMA_SAMPLES * 4)
#define FIFO_SIZE      (48000 * 10)         // 10 s de son decode d'avance (48 kHz stereo)
#define FIFO_MARGIN    8192                 // place minimale avant de decoder une trame
#define PREBUFFER      (rebuffers == 0 ? 48000 : rebuffers < 3 ? 48000 * 2 : 48000 * 4)   // v71 : 1 s pour demarrer vite ; plus si le flux a deja coupe
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

/* ---------- Themes : toutes les couleurs de l'interface passent par une matrice
   (rotation de teinte + saturation). Indigo = couleurs d'origine. ---------- */
enum { THEME_INDIGO, THEME_OCEAN, THEME_EMERAUDE, THEME_RUBIS, THEME_AMBRE, THEME_DOUX, THEME_GRAPHITE,
       THEME_LAVANDE, THEME_FORET, THEME_CREPUSCULE, THEME_COUNT };
static const char *const theme_names[THEME_COUNT] = { "Indigo", "Oc\xC3\xA9" "an", "\xC3\x89meraude", "Rubis", "Ambre", "Couleurs douces", "Graphite",
                                                      "Lavande", "For\xC3\xAAt", "Cr\xC3\xA9puscule" };
static const char *const theme_keys[THEME_COUNT] = { "indigo", "ocean", "emeraude", "rubis", "ambre", "doux", "graphite",
                                                     "lavande", "foret", "crepuscule" };
static int theme_id = THEME_INDIGO;
static int th_m[9] = { 256, 0, 0, 0, 256, 0, 0, 0, 256 };   /* matrice 8.8 */
static int th_identity = 1;

static void skin_accent(void);
static void theme_apply(int id) {
    static const float hue[THEME_COUNT] = { 0, -55, -145, 65, 140, 0, 0, -28, -120, 40 };
    static const float sat[THEME_COUNT] = { 1, 1, 1, 1, 1, 0.55f, 0.12f, 0.75f, 0.8f, 1.15f };
    theme_id = (id % THEME_COUNT + THEME_COUNT) % THEME_COUNT;
    float a = hue[theme_id] * 3.14159265f / 180.0f, c = cosf(a), sn = sinf(a), sv = sat[theme_id];
    float m[9] = {
        0.213f + c * 0.787f - sn * 0.213f, 0.715f - c * 0.715f - sn * 0.715f, 0.072f - c * 0.072f + sn * 0.928f,
        0.213f - c * 0.213f + sn * 0.143f, 0.715f + c * 0.285f + sn * 0.140f, 0.072f - c * 0.072f - sn * 0.283f,
        0.213f - c * 0.213f - sn * 0.787f, 0.715f - c * 0.715f + sn * 0.715f, 0.072f + c * 0.928f + sn * 0.072f };
    static const float lum[3] = { 0.213f, 0.715f, 0.072f };
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++)
            th_m[r * 3 + k] = (int)((m[r * 3 + k] * sv + lum[k] * (1.0f - sv)) * 256.0f + (m[r * 3 + k] >= 0 ? 0.5f : -0.5f));
    th_identity = (theme_id == THEME_INDIGO);
    skin_accent();
}

/* v28 : style de l'interface. 0 = Indigo (d'origine), 1 = Moderne (gris
   sombre, cartes plates, une seule couleur d'accent, onglets en haut).
   En Moderne, les couleurs violettes deviennent du gris (fonds, textes) ou la
   couleur d'accent (selection) ; le theme choisit la couleur d'accent. */
static int ui_skin = 0;                                 /* v101 : 2 = Nouvelle interface (ui_nova.c) */
static int nv_raw = 0;                                  /* v101 : 1 = couleurs telles quelles (Nouvelle : en-tete, fond) */
static float nv_cdx = 0;                                /* v101 : decalage de la page pendant une transition */
static void nv_background(void);                        /* (ui_nova.c) */
static void nv_panel(float x, float y, float w, float h);
static void nv_legend_box(float x, float w);
static int nv_draw_tabs(const char *count);
static void nv_header(void);
static void nv_page_begin(void);
static void nv_page_end(void);
static void au_background(void);                        /* v102 : Aurore (ui_aurore.c) */
static void au_panel(float x, float y, float w, float h);
static void au_legend_box(float x, float w);
static int au_draw_tabs(const char *count);
static void au_header(void);
static void au_player(void);                            /* v103 : Aurore : lecteur de MUSIQUE (ui_aurore2.c) */
static int gl_view;                                     /* gclib.c : affichage de Mes jeux GameCube */
static int gl_anim = 1;                                 /* v45 : animations de JEUX / APPS */
static int gapless_on = 1;                              /* v72 : enchainement sans blanc (meme album / dossier) */
static int radio_resume = 1;                            /* v72 : la derniere radio reprend au demarrage */
/* v73 : nouveautes (extras73.c, inclus a la fin) */
static int x73_setting(const char *k, const char *v);
static void x73_save(FILE *f);
static int nfs_setting(const char *k, const char *v);   /* v86 : serveurs SMB / FTP / FSP (netfs.c) */
static void nfs_menu_value(int p, char *out, int cap);  /* (tools.c) */
static void nfs_ui_open(int p);
static void nfs_kb_done(const char *t);
static void nfs_save(FILE *f);
static void x73_menu_value(int i, char *out, int cap);
static void x73_menu_change(int i, int d);
static void x73_pump(void);
static void rh_open(void);
static void saver_tableau(float t);
static char radio_last_url[URL_MAX], radio_last_name[48];
static int nat_card = 1;                                /* v65 : carte memoire virtuelle (slot A) pour Resident */
static int nat_igr = 1;                                 /* v59 : A+Z+START dans Resident (natif.c) ; v63 : de nouveau actif (lecture H + L : la manette marche) */
static int gl_sort = 0;                                 /* v45 : tri des jeux : A-Z, derniers lances, taille, region */
static const char *const gl_sort_keys[4] = { "az", "recents", "taille", "region" };
static const char *const gl_sort_names[4] = { "A \xE2\x86\x92 Z", "Derniers lanc\xC3\xA9s", "Taille", "R\xC3\xA9gion" };
static const char *const skin_keys[4] = { "indigo", "moderne", "nouvelle", "aurore" };
static const char *const glview_keys[6] = { "grille", "carrousel", "liste", "mur", "vitrine", "etagere" };
static u8 mod_acc[3] = { 0, 190, 230 };
static void skin_accent(void) {
    static const u8 acc[THEME_COUNT][3] = { { 0, 190, 230 }, { 40, 120, 255 }, { 0, 200, 120 }, { 235, 60, 70 },
                                            { 255, 150, 30 }, { 240, 110, 180 }, { 225, 225, 230 },
                                            { 180, 120, 255 }, { 70, 200, 90 }, { 255, 110, 90 } };
    memcpy(mod_acc, acc[theme_id], 3);
}
static inline col_t mod_col(col_t c) {
    int mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    int mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    int sat = mx ? (mx - mn) * 255 / mx : 0;
    int purple = c.b >= c.r && c.b >= c.g && c.r + 8 >= c.g;          /* violets / bleus-violets */
    if (!purple || sat < 30) {
        if (sat < 30) return c;                                       /* gris, blanc : tels quels */
        return c;                                                     /* orange, rouge, vert... : tels quels */
    }
    if (sat > 120 && mx > 140) {                                      /* selection, lueurs : accent */
        c.r = (u8)(mod_acc[0] * mx / 255); c.g = (u8)(mod_acc[1] * mx / 255); c.b = (u8)(mod_acc[2] * mx / 255);
        return c;
    }
    int lum = (c.r * 54 + c.g * 183 + c.b * 19) >> 8;
    int gr = lum + (mx - lum) / 2;                                     /* gris neutre, un peu plus clair */
    if (gr > 255) gr = 255;
    c.r = c.g = (u8)gr; c.b = (u8)(gr + (gr < 245 ? 6 : 0));
    return c;
}
/* v101 : Nouvelle interface : les violets deviennent des gris bleutes (verre), la selection l'accent */
static inline col_t nova_col(col_t c) {
    int mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    int mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    int sat = mx ? (mx - mn) * 255 / mx : 0;
    int purple = c.b >= c.r && c.b >= c.g && c.r + 8 >= c.g;
    if (!purple || sat < 30) return c;
    if (sat > 120 && mx > 140) {
        int ar = theme_id == THEME_INDIGO ? 118 : mod_acc[0], ag = theme_id == THEME_INDIGO ? 112 : mod_acc[1], ab = theme_id == THEME_INDIGO ? 255 : mod_acc[2];
        c.r = (u8)(ar * mx / 255); c.g = (u8)(ag * mx / 255); c.b = (u8)(ab * mx / 255);
        return c;
    }
    int lum = (c.r * 54 + c.g * 183 + c.b * 19) >> 8;
    int gr = lum + (mx - lum) / 2, b = gr * 106 / 100 + 10;
    c.r = (u8)(gr * 88 / 100); c.g = (u8)(gr * 93 / 100); c.b = (u8)(b > 255 ? 255 : b);
    return c;
}
/* v102 : Aurore : les violets deviennent du graphite neutre, la selection du rose-violet */
static inline col_t aur_col(col_t c) {
    int mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    int mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    int sat = mx ? (mx - mn) * 255 / mx : 0;
    int purple = c.b >= c.r && c.b >= c.g && c.r + 8 >= c.g;
    if (!purple || sat < 30) return c;
    if (sat > 120 && mx > 140) {
        int ar = theme_id == THEME_INDIGO ? 220 : mod_acc[0], ag = theme_id == THEME_INDIGO ? 90 : mod_acc[1], ab = theme_id == THEME_INDIGO ? 255 : mod_acc[2];
        c.r = (u8)(ar * mx / 255); c.g = (u8)(ag * mx / 255); c.b = (u8)(ab * mx / 255);
        return c;
    }
    int lum = (c.r * 54 + c.g * 183 + c.b * 19) >> 8;
    int gr = lum + (mx - lum) / 3;
    c.r = c.g = (u8)(gr * 96 / 100); c.b = (u8)(gr * 96 / 100 + (gr < 240 ? 6 : 0));
    return c;
}
static inline col_t th_col(col_t c) {
    if (nv_raw) return c;                                /* (v109 : logos et decors : couleurs exactes, tous styles) */
    if (ui_skin == 3) return nv_raw ? c : aur_col(c);
    if (ui_skin == 2) return nv_raw ? c : nova_col(c);
    if (ui_skin) return mod_col(c);
    if (th_identity) return c;
    int r = (th_m[0] * c.r + th_m[1] * c.g + th_m[2] * c.b) >> 8;
    int g = (th_m[3] * c.r + th_m[4] * c.g + th_m[5] * c.b) >> 8;
    int b = (th_m[6] * c.r + th_m[7] * c.g + th_m[8] * c.b) >> 8;
    c.r = (u8)(r < 0 ? 0 : r > 255 ? 255 : r);
    c.g = (u8)(g < 0 ? 0 : g > 255 ? 255 : g);
    c.b = (u8)(b < 0 ? 0 : b > 255 ? 255 : b);
    return c;
}

static void *xfb[2] = { NULL, NULL };
static void *gx_fifo = NULL;
static GXRModeObj *rmode = NULL;
static int displayed_fb = 0;

static u64 now_ms(void) { return ticks_to_millisecs(gettime()); }

/* ---------- v69 : mesure de fluidite (START > « Mesure de fluidite ») ----------
   Chaque image : temps de chaque tache de la boucle (reseau, pochettes, decodage, dessin,
   processeur graphique, attente de l'ecran). A l'ecran : images/s, pire image, tache la plus
   lente. Quand on coupe la mesure : rapport SD:/gc-mp3/fluidite.txt (par onglet). */
enum { PH_INPUT, PH_WEB, PH_COVERS, PH_LYRICS, PH_RETRO, PH_PODCAST, PH_PHOTO, PH_FILES, PH_INFOS, PH_PHONE, PH_DL, PH_UPDATE,
       PH_MISC, PH_LIB, PH_SETTINGS, PH_AUDIO, PH_MUSIC, PH_STREAM, PH_SPECTRUM, PH_DRAW, PH_GPU, PH_WAIT, PH_N };
static const char *const pfx_names[PH_N] = { "manette", "web", "pochettes", "paroles", "retro", "podcasts", "photos", "fichiers", "infos",
    "telephone", "telechargements", "mise a jour", "divers", "bibliotheque", "reglages", "son", "musique", "radio", "spectre",
    "dessin", "processeur graphique", "attente de l'ecran" };
static int perf_on = 0;
static u32 pfx_cur[PH_N], pfx_frame_t0 = 0;
#define PF_TABS 12
static u32 pfx_frames[PF_TABS], pfx_slow[PF_TABS], pfx_vslow[PF_TABS], pfx_worst[PF_TABS];
static u64 pfx_sum[PF_TABS][PH_N];
static u32 pfx_max[PF_TABS][PH_N], pfx_top[PF_TABS][PH_N];
static u32 pfx_sec_t0 = 0, pfx_sec_n = 0, pfx_sec_worst = 0, pfx_fps = 0, pfx_worst_ms = 0, pfx_last_top = PH_N, pfx_last_top_us = 0;
static u32 perf_us(void) {
#ifdef GEKKO
    return (u32)ticks_to_microsecs(gettime());
#else
    extern u64 pcsim_now_us(void);                       /* (simulateur sur PC : a la microseconde) */
    return (u32)pcsim_now_us();
#endif
}
#define PF(ph, call) do { if (perf_on) { u32 pf_t0_ = perf_us(); call; pfx_cur[ph] += perf_us() - pf_t0_; } else { call; } } while (0)
static void perf_frame(int tb) {                    /* debut d'une image : on compte la precedente */
    u32 n = perf_us();
    if (!perf_on) { pfx_frame_t0 = n; return; }
    if (tb < 0 || tb >= PF_TABS) tb = PF_TABS - 1;
    if (pfx_frame_t0) {
        u32 tot = n - pfx_frame_t0;
        pfx_frames[tb]++;
        if (tot > pfx_worst[tb]) pfx_worst[tb] = tot;
        int top = PH_N; u32 tv = 0;
        for (int i = 0; i < PH_N; i++) {
            pfx_sum[tb][i] += pfx_cur[i];
            if (pfx_cur[i] > pfx_max[tb][i]) pfx_max[tb][i] = pfx_cur[i];
            if (i != PH_WAIT && pfx_cur[i] > tv) { tv = pfx_cur[i]; top = i; }
        }
        if (tot > 20000u) {                              /* (une image ratee : plus de 1/60 s + marge) */
            pfx_slow[tb]++;
            if (tot > 34000u) pfx_vslow[tb]++;
            if (top < PH_N) pfx_top[tb][top]++;
            pfx_last_top = (u32)top; pfx_last_top_us = tv;
        }
        pfx_sec_n++;
        if (tot > pfx_sec_worst) pfx_sec_worst = tot;
    }
    if (n - pfx_sec_t0 >= 1000000u) { pfx_fps = pfx_sec_n; pfx_worst_ms = pfx_sec_worst / 1000u; pfx_sec_n = 0; pfx_sec_worst = 0; pfx_sec_t0 = n; }
    for (int i = 0; i < PH_N; i++) pfx_cur[i] = 0;
    pfx_frame_t0 = n;
}

/* Compteur d'images, incremente a chaque balayage vertical (interruption video). */
static volatile u32 retrace_count = 0;
static void retrace_cb(u32 count) { (void)count; retrace_count++; }

// ---------- Etat de la lecture ----------
enum { ST_IDLE, ST_CONNECT, ST_BUFFER, ST_PLAY, ST_ERROR };
static int state = ST_IDLE;
static int muted = 0;
static int volume_pct = 100;                /* volume (reglages), 0 a 100 */
/* mode veilleuse (night.c) et mise a jour (update.c) : reglages */
static int night_on = 0, night_lum = 2, night_radio = 1, night_pending_radio = 0;
static u64 night_hint_until = 0;
static int upd_auto = 1;
static char upd_repo[96] = "antho13otz/gc-mp3";
static char upd_file[256] = "";
static int stream_codec = 0;                /* radio : 0 = MP3, 1 = AAC, 2 = OGG Vorbis */
static int hls_on = 0;                      /* radio HLS (.m3u8) en cours */
static void hls_pump(void);
static void hls_stop(void);
static int hls_begin(const char *url, const char *pl);
static void ogg_stream_reset(void);
static int ogg_bos_at = -1, ogg_scan = 0;   /* radio OGG : debut de flux a venir (positions dans cbuf) */
static unsigned char *ogg_hdr = NULL;       /* en-tetes du flux OGG (pour l'enregistrement) */
static int ogg_hdr_len = 0;
enum { SRC_NONE, SRC_FILE, SRC_RADIO };
static int src = SRC_NONE;              /* ce qui joue en ce moment */
enum { TAB_MUSIC, TAB_RADIO, TAB_WEB, TAB_RETRO, TAB_PODCAST, TAB_PHOTO, TAB_FILES, TAB_INFO, TAB_PHONE, TAB_GAMES, TAB_DL, TAB_COUNT };
static int tab = TAB_MUSIC;             /* onglet affiche */
static u32 tab_hidden = 0;              /* onglets masques (bits) */
static const char *const tab_names[TAB_COUNT] = {
    "MUSIQUE", "RADIO", "WEB", "R\xC3\x89TRO", "PODCASTS", "PHOTOS", "FICHIERS", "INFOS", "T\xC3\x89L\xC3\x89PHONE", "JEUX / APPS", "T\xC3\x89L\xC3\x89" "CHARGEMENTS" };
static const char *const tab_keys[TAB_COUNT] = {
    "musique", "radio", "web", "retro", "podcasts", "photos", "fichiers", "infos", "telephone", "jeux", "telechargements" };
static int tab_shown(int t) { return t == TAB_MUSIC || !(tab_hidden & (1u << t)); }
/* v43 : ordre des onglets (START > Reglages > Onglets affiches : X / Y) */
static u8 tab_order[TAB_COUNT] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
#define FOR_TABS(t) for (int t##_k = 0, t = tab_order[0]; t##_k < TAB_COUNT; t = ++t##_k < TAB_COUNT ? tab_order[t##_k] : 0)
/* elements du lecteur (START > Elements du lecteur) */
enum { EL_COVER, EL_VIZ, EL_BUTTONS, EL_INFO, EL_CLOCK, EL_COUNT };
static u32 el_mask = 0x0F;
#define EL(e) ((el_mask >> (e)) & 1)
/* disposition du lecteur (stick C vers le bas) */
enum { L_CLASSIC, L_COVER, L_LIST, L_MINIMAL, L_BIGVIZ, LAYOUT_COUNT };
static int layout_tab[TAB_COUNT];               /* v43 : une disposition par onglet du lecteur */
#define player_layout layout_tab[IS_PLAYER_TAB(tab) ? tab : TAB_MUSIC]
static int pod_on;                      /* podcast en cours (onglet PODCASTS) */
static int pod_open, npods;
typedef struct { char name[96]; char url[384]; } pod_t;
static pod_t pods[64];
static int pod_dur;
static int pod_pos_sec(void);
#define IS_PLAYER_TAB(t) ((t) == TAB_MUSIC || (t) == TAB_RADIO || (t) == TAB_RETRO || (t) == TAB_PODCAST)
/* v23 : zones cliquables avec le curseur au stick (tabs_extra.c) */
enum { GPZ_SEL = 1, GPZ_KEY, GPZ_TAB, GPZ_STEP };
typedef struct { f32 x, y, w, h; int *sel; int val, code; } gpzone_t;
#define GP_ZONES 64
static gpzone_t gp_z[GP_ZONES], gp_zl[GP_ZONES];          /* image en cours, image precedente */
static int gp_nz = 0, gp_nzl = 0;
static f32 gp_x = 320, gp_y = 240;
static int gp_on = 0;
static void gp_player_zones(void);
static u64 gp_t = 0;

static void gp_zone(f32 x, f32 y, f32 w, f32 h, int *sel, int val, int code) {
    if (gp_nz >= GP_ZONES) return;
    gp_z[gp_nz++] = (gpzone_t){ x + nv_cdx, y, w, h, sel, val, code };   /* (v103 : page qui glisse) */
}
static void gp_frame_begin(void) {                          /* (debut de chaque image) */
    memcpy(gp_zl, gp_z, sizeof(gpzone_t) * gp_nz);
    gp_nzl = gp_nz;
    gp_nz = 0;
}

static int paused = 0;                  /* pause de la musique */
static int video_playing = 0;           /* une video (onglet PHOTOS) joue son son dans la file */
static void video_stop(void);
static void stop_track(void);
static int stat_counted;                    /* morceau en cours deja compte (lib_extra.c) */
static void lyrics_load(int track);
static void bg_tick(void);              /* musique/radio pendant les attentes reseau */
static void crumb(const char *fmt, ...);   /* journal de plantage (crashlog.c) */
static void crash_beat(void);
static void draw_crash_notice(void);
static void update_value(char *out, int cap);   /* update.c */
static void update_activate(void);
static void night_start(void);                  /* night.c */
static int cur_station = -1;                 // station en cours
static char err_msg[96];                 // message d'erreur affiche
static char now_title[160];              // titre en cours (metadonnees ICY)
static char icy_name[64];                // nom annonce par le serveur
static int  stream_kbps = 0, stream_hz = 0, stream_ch = 0;
static int  rebuffers = 0;

// ---------- Audio (DMA, anneau de 3 tampons, 48 kHz) ----------
static s16 dmabuf[NUM_BUFS][DMA_SAMPLES * 2] ATTRIBUTE_ALIGN(32);
static volatile u32 cb_n = 0;              /* prochain bloc a jouer */
static volatile u32 fill_n = 0;            /* prochain bloc a remplir */
static s16 vizbuf[NUM_BUFS][DMA_SAMPLES];          /* (spectre : ce qui sera entendu avec ce bloc) */

static stereo_t fifo[FIFO_SIZE];
static int f_head = 0, f_tail = 0, f_count = 0;

#define VIZ_BLK DMA_SAMPLES               /* v29 : tout le bloc DMA (avant : ses 512 premiers echantillons) */
#define VIZ_N 2048                        /* v29 : 2048 derniers echantillons (43 ms) : basses bien separees */
static s16 viz[VIZ_N];
static int viz_pos = 0;
static u32 consumed = 0;                 // echantillons joues depuis le debut de l'ecoute
static int sleep_fade = 256;             // minuterie : baisse du volume dans les 30 dernieres secondes
static int sleep_eot = 0;                // v76 : minuterie "a la fin du morceau"
static int party_on;                     // (defini plus bas ; utilise des track_next)
static int alarm_fade;                   // reveil : volume qui monte (onglet INFOS)
static u32 consumed_skip;

/* Fin d'un bloc DMA : programme le suivant.
   v22 : plusieurs blocs sont remplis d'avance (~210 ms) ; un moment ou le
   programme est occupe (carte SD, reseau, image a decoder...) ne fait plus
   de trou dans le son. S'il n'y a vraiment plus rien, on joue du silence. */
static volatile int in_fill = 0;           /* v42 : la boucle principale remplit un bloc en ce moment */
static void fill_buf(int idx);
/* v70 : horloge du son entendu (jeu de rythme) : position dans le morceau du bloc qui vient de
   commencer a jouer, + le temps passe depuis (interpole a la microseconde) */
static u32 blk_pos[NUM_BUFS], blk_cnt[NUM_BUFS];
static volatile u32 aclk_base = 0, aclk_cnt = 0, aclk_us = 0;
static double audio_ms(void) {
    u32 lv = IRQ_Disable();
    u32 b = aclk_base, c = aclk_cnt, t0 = aclk_us;
    IRQ_Restore(lv);
    u32 el = (u32)((u64)(perf_us() - t0) * 48u / 1000u);
    if (el > c) el = c;
    return (double)(b + el) / 48.0;
}
static void dma_cb(void) {
    u32 blk = cb_n;
    s16 *buf = dmabuf[blk % NUM_BUFS];
    {                                                       /* (le bloc programme avant celui-ci vient de commencer) */
        u32 pi = (blk + NUM_BUFS - 1) % NUM_BUFS;
        aclk_base = blk_pos[pi]; aclk_cnt = blk_cnt[pi]; aclk_us = perf_us();
    }
    /* v42 : plus de bloc pret (la boucle principale est occupee : reseau securise, carte SD...) :
       on remplit ici directement depuis le son deja decode (jusqu'a 10 s d'avance), au lieu d'un trou */
    if ((s32)(fill_n - blk) <= 0 && !in_fill && f_count > DMA_SAMPLES) {
        fill_buf((int)(blk % NUM_BUFS));
        fill_n = blk + 1;
    }
    if ((s32)(fill_n - blk) <= 0) {
        blk_pos[blk % NUM_BUFS] = consumed; blk_cnt[blk % NUM_BUFS] = 0;
        memset(buf, 0, DMA_BYTES);
        DCFlushRange(buf, DMA_BYTES);
        memset(viz + viz_pos, 0, VIZ_BLK * sizeof(s16));
    } else memcpy(viz + viz_pos, vizbuf[blk % NUM_BUFS], VIZ_BLK * sizeof(s16));   /* le spectre suit ce qu'on entend */
    viz_pos = (viz_pos + VIZ_BLK) & (VIZ_N - 1);            /* (anneau : viz_pos = le plus ancien) */
    AUDIO_InitDMA((u32)buf, DMA_BYTES);
    cb_n = blk + 1;
}

static int audio_mono = 0;                   /* v34 : son de la console en mono (reglage Indigo / SRAM) */
static volatile u32 ts_rate_q8, ts_credit;   /* v73 : podcasts plus rapides (plus bas) */
static u32 ts_frac = 0;
static void fill_buf(int idx) {
    s16 *d = dmabuf[idx];
    s16 *vz = vizbuf[idx];
    u32 c0 = consumed;
    int live = (src == SRC_RADIO && state == ST_PLAY) || (src == SRC_FILE && !paused);
    for (int i = 0; i < DMA_SAMPLES; i++) {
        s16 l = 0, r = 0;
        if (live && f_count > 0) {
            l = fifo[f_head].l;
            r = fifo[f_head].r;
            f_head = (f_head + 1) % FIFO_SIZE;
            f_count--;
            if (consumed_skip) consumed_skip--;     /* fin du morceau precedent (fondu) */
            else if (ts_rate_q8 == 256) consumed++;
            else { ts_frac += ts_rate_q8; consumed += ts_frac >> 8; ts_frac &= 255; }   /* v73 : podcast plus rapide */
        }
        if (i < VIZ_BLK) vz[i] = (s16)(((int)l + (int)r) / 2);
        if (audio_mono) { s16 m = (s16)(((int)l + (int)r) / 2); l = r = m; }
        if (muted) l = r = 0;
        else if (volume_pct < 100 || sleep_fade < 256 || alarm_fade < 256) {
            int v = volume_pct * 256 / 100 * sleep_fade / 256 * alarm_fade / 256;
            l = (s16)((l * v) >> 8);
            r = (s16)((r * v) >> 8);
        }
        d[2 * i] = l;
        d[2 * i + 1] = r;
    }
    if (ts_credit && live) { consumed += ts_credit; ts_credit = 0; }   /* (blancs sautes) */
    blk_pos[idx] = c0; blk_cnt[idx] = consumed - c0;
    DCFlushRange(d, DMA_BYTES);
}

static void service_audio(void) {
    for (int guard = 0; guard < AUDIO_AHEAD; guard++) {
        u32 level = IRQ_Disable();
        u32 blk = cb_n, f = fill_n;
        if ((s32)(f - blk) < 0) { f = blk; fill_n = f; }         /* (en retard : on repart du bloc a venir) */
        IRQ_Restore(level);
        if ((s32)(f - blk) >= AUDIO_AHEAD) return;
        in_fill = 1;
        level = IRQ_Disable();
        if ((s32)(fill_n - cb_n) < 0) { fill_n = cb_n; f = fill_n; }   /* (v42 : l'interruption a peut-etre rempli entre-temps) */
        f = fill_n;
        IRQ_Restore(level);
        fill_buf((int)(f % NUM_BUFS));
        in_fill = 0;
        level = IRQ_Disable();
        if (fill_n == f) fill_n = f + 1;
        IRQ_Restore(level);
    }
}
/* son deja prepare mais pas encore entendu (en echantillons) */
static u32 audio_queued(void) {
    s32 n = (s32)(fill_n - cb_n) + 1;
    if (n < 0) n = 0;
    return (u32)n * DMA_SAMPLES;
}
/* position entendue (consumed compte ce qui est prepare) */
static u32 consumed_heard(void) {
    u32 q = audio_queued();
    return consumed > q ? consumed - q : 0;
}

static int fifo_put(s16 l, s16 r) {
    if (f_count >= FIFO_SIZE) return 0;
    fifo[f_tail].l = l;
    fifo[f_tail].r = r;
    f_tail = (f_tail + 1) % FIFO_SIZE;
    u32 lv = IRQ_Disable();                     /* v42 : f_count peut aussi baisser dans l'interruption du son */
    f_count++;
    IRQ_Restore(lv);
    return 1;
}

// ---------- Analyseur de spectre (v29 : FFT 2048 points, echelle "mel") ----------
/* 55 barres de 40 Hz a 16 kHz sur une echelle mel : les basses prennent peu de
   place, les mediums et les aigus sont bien etales (comme les analyseurs audio).
   Niveau automatique : la barre la plus forte touche le haut, le reste suit
   (plus de spectre "tout blanc" sur une musique tres forte).
   A cote : detection des coups de grosse caisse (bande 40-140 Hz) pour le mode
   soiree, les vibrations et les jeux : kick_count augmente a chaque coup. */
#define NBARS 55
static float bar_val[NBARS];
/* v39 : styles du spectre (dessin dans viz_styles.c) */
enum { VZ_LED, VZ_BARS, VZ_MIRROR, VZ_CURVE, VZ_SGRAM, VZ_SCOPE, VZ_CIRCLE, VZ_COUNT };
static int viz_style = VZ_LED;
static const char *const viz_keys[VZ_COUNT] = { "led", "barres", "miroir", "courbe", "spectrogramme", "oscilloscope", "cercle" };
/* v40 : couleurs du spectrogramme (plein ecran, mode soiree, style du lecteur) */
enum { SGP_FEU, SGP_MAGMA, SGP_ARC, SGP_OCEAN, SGP_VERT, SGP_GRIS, SGP_COUNT };
static int sg_pal_id = SGP_FEU;
static const char *const sgp_keys[SGP_COUNT] = { "feu", "violet", "arc-en-ciel", "ocean", "vert", "gris" };
static const char *const sgp_names[SGP_COUNT] = { "Feu", "Violet", "Arc-en-ciel", "Oc\xC3\xA9" "an", "Vert", "Noir et blanc" };
static const u8 sgp_stops[SGP_COUNT][6][3] = {
    { { 0, 0, 0 }, { 80, 0, 0 }, { 190, 30, 0 }, { 250, 110, 0 }, { 255, 200, 40 }, { 255, 255, 220 } },   /* feu */
    { { 0, 0, 6 }, { 48, 12, 96 }, { 132, 30, 110 }, { 214, 62, 60 }, { 250, 152, 24 }, { 252, 252, 170 } }, /* violet (avant) */
    { { 0, 0, 0 }, { 20, 40, 160 }, { 0, 170, 200 }, { 60, 210, 60 }, { 250, 220, 0 }, { 255, 40, 20 } },    /* arc-en-ciel */
    { { 0, 0, 0 }, { 0, 20, 70 }, { 0, 70, 160 }, { 0, 150, 220 }, { 90, 220, 255 }, { 240, 255, 255 } },     /* ocean */
    { { 0, 0, 0 }, { 0, 50, 10 }, { 0, 120, 30 }, { 40, 200, 40 }, { 170, 255, 60 }, { 240, 255, 200 } },     /* vert */
    { { 0, 0, 0 }, { 40, 40, 40 }, { 90, 90, 90 }, { 150, 150, 150 }, { 210, 210, 210 }, { 255, 255, 255 } }, /* gris */
};
static void sgp_rgb(f32 v, int *r, int *g, int *b) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    f32 x = v * 5; int a = (int)x; if (a > 4) a = 4;
    f32 t = x - a;
    const u8 (*st)[3] = sgp_stops[sg_pal_id >= 0 && sg_pal_id < SGP_COUNT ? sg_pal_id : 0];
    *r = (int)(st[a][0] + (st[a + 1][0] - st[a][0]) * t);
    *g = (int)(st[a][1] + (st[a + 1][1] - st[a][1]) * t);
    *b = (int)(st[a][2] + (st[a + 1][2] - st[a][2]) * t);
}
static const char *const viz_names[VZ_COUNT] = { "LED (classique)", "Barres", "Miroir", "Courbe", "Spectrogramme", "Oscilloscope", "Cercle" };
static float win[VIZ_N];
static short bar_lo[NBARS], bar_hi[NBARS];      /* cases de la FFT de chaque barre */
static float bar_fc[NBARS];                       /* (frequence centrale, en case de FFT) */
static float fft_cs[VIZ_N / 2], fft_sn[VIZ_N / 2];
static u16 fft_rev[VIZ_N];
static float bass_lvl = 0.0f;                     /* basses 0..1 (grosse caisse) */
static u32 kick_count = 0;                        /* coups de grosse caisse detectes */
static float spec_ref = -30.0f;                   /* niveau de reference (dB) du controle automatique */

static float mel_of(float f) { return 2595.0f * log10f(1.0f + f / 700.0f); }
static float hz_of_mel(float m) { return 700.0f * (powf(10.0f, m / 2595.0f) - 1.0f); }
static void init_spectrum(void) {
    for (int i = 0; i < VIZ_N; i++)
        win[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i / (float)(VIZ_N - 1));
    for (int i = 0; i < VIZ_N / 2; i++) { fft_cs[i] = cosf(2.0f * PI_F * i / VIZ_N); fft_sn[i] = -sinf(2.0f * PI_F * i / VIZ_N); }
    int bits = 0; while ((1 << bits) < VIZ_N) bits++;
    for (int i = 0; i < VIZ_N; i++) { int r = 0; for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b); fft_rev[i] = (u16)r; }
    const float df = 48000.0f / VIZ_N, m0 = mel_of(40.0f), m1 = mel_of(16000.0f);
    for (int b = 0; b < NBARS; b++) {
        float flo = hz_of_mel(m0 + (m1 - m0) * b / NBARS), fhi = hz_of_mel(m0 + (m1 - m0) * (b + 1) / NBARS);
        bar_lo[b] = (short)(flo / df + 0.5f); bar_hi[b] = (short)(fhi / df + 0.5f);
        if (bar_hi[b] < bar_lo[b]) bar_hi[b] = bar_lo[b];
        bar_fc[b] = (flo + fhi) * 0.5f / df;
        bar_val[b] = 0.0f;
    }
}

static int update_spectrum(void) {
    static float re[VIZ_N], im[VIZ_N], mag[VIZ_N / 2];
    static float kick_fast = -90, kick_slow = -90, kick_dev = 3;
    static u64 kick_last = 0;
    int active = 0;
    {                                                     /* silence : pas de calcul, les barres retombent */
        int loud = 0;
        for (int i = 0; i < VIZ_N && !loud; i++) loud = viz[i] > 3 || viz[i] < -3;
        if (!loud) {
            for (int b = 0; b < NBARS; b++) {
                bar_val[b] *= 0.82f;
                if (bar_val[b] < 0.01f) bar_val[b] = 0.0f; else active = 1;
            }
            bass_lvl *= 0.82f;
            return active;
        }
    }
    {                                                     /* meme bloc de son qu'a l'image d'avant : rien a recalculer */
        static u32 last_sig = 0;
        u32 sig = 2166136261u;
        for (int i = 0; i < VIZ_N; i += 61) { sig ^= (u16)viz[i]; sig *= 16777619u; }
        if (sig == last_sig) {
            for (int b = 0; b < NBARS; b++) if (bar_val[b] > 0.0f) active = 1;
            return active;
        }
        last_sig = sig;
    }
    for (int i = 0; i < VIZ_N; i++) { int r = fft_rev[i]; re[r] = (float)viz[(viz_pos + i) & (VIZ_N - 1)] * win[i]; im[r] = 0.0f; }
    for (int len = 2; len <= VIZ_N; len <<= 1) {          /* FFT radix 2 */
        int half = len >> 1, step = VIZ_N / len;
        for (int i = 0; i < VIZ_N; i += len)
            for (int k = 0; k < half; k++) {
                float wr = fft_cs[k * step], wi = fft_sn[k * step];
                int a = i + k, c = a + half;
                float tr = re[c] * wr - im[c] * wi, ti = re[c] * wi + im[c] * wr;
                re[c] = re[a] - tr; im[c] = im[a] - ti;
                re[a] += tr; im[a] += ti;
            }
    }
    for (int k = 0; k < VIZ_N / 2; k++) mag[k] = re[k] * re[k] + im[k] * im[k];
    const float norm = 1.0f / ((float)VIZ_N * 0.25f * 32768.0f);
    float db[NBARS], top = -200.0f;
    for (int b = 0; b < NBARS; b++) {
        float p = 0.0f;
        if (bar_hi[b] > bar_lo[b] + 1) { for (int k = bar_lo[b]; k < bar_hi[b] && k < VIZ_N / 2; k++) if (mag[k] > p) p = mag[k]; }
        else {                                            /* barre plus fine qu'une case : interpolation */
            float f = bar_fc[b]; int k = (int)f; float t = f - k;
            if (k + 1 < VIZ_N / 2) p = mag[k] * (1 - t) + mag[k + 1] * t;
        }
        db[b] = 20.0f * log10f(sqrtf(p) * norm + 1e-6f) + 0.09f * (float)b;   /* (les aigus sont plus faibles) */
        if (db[b] > top) top = db[b];
    }
    /* niveau automatique : suit vite la montee, redescend lentement */
    if (top > spec_ref) spec_ref = top; else spec_ref -= 0.08f;
    if (spec_ref < -48.0f) spec_ref = -48.0f;
    const float range = 50.0f, floor_db = spec_ref - range;
    for (int b = 0; b < NBARS; b++) {
        float v = (db[b] - floor_db) / range;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        v = v * 0.96f;                                    /* (un peu de marge en haut) */
        bar_val[b] = (v > bar_val[b]) ? v : (bar_val[b] * 0.82f + v * 0.18f);
        if (bar_val[b] < 0.01f) bar_val[b] = 0.0f; else active = 1;
    }
    /* grosse caisse : "flux" des basses (40-140 Hz) = combien elles MONTENT d'un bloc
       a l'autre (une basse tenue ne compte pas), compare a sa moyenne recente */
    {
        static float prev_db[16];
        static float flux_avg = 1.0f, flux_dev = 1.0f, flux_prev = 0.0f;
        int k0 = (int)(40.0f * VIZ_N / 48000.0f), k1 = (int)(140.0f * VIZ_N / 48000.0f);
        if (k1 - k0 > 15) k1 = k0 + 15;
        float flux = 0.0f, e = 0.0f;
        for (int k = k0; k <= k1; k++) {
            float kdb = 10.0f * log10f(mag[k] * norm * norm + 1e-12f);
            float up = kdb - prev_db[k - k0];
            if (up > 0) flux += up > 30.0f ? 30.0f : up;
            prev_db[k - k0] = kdb;
            e += mag[k];
        }
        flux /= (float)(k1 - k0 + 1);
        float kdb = 10.0f * log10f(e * norm * norm + 1e-12f);
        u64 now = now_ms();
        float thr = flux_avg + 1.6f * flux_dev;
        if (thr < 2.5f) thr = 2.5f;
        if (flux > thr && flux >= flux_prev && now - kick_last > 230 && kdb > spec_ref - 38.0f) { kick_count++; kick_last = now; }
        flux_prev = flux;
        flux_avg = flux_avg * 0.95f + flux * 0.05f;
        float ad = flux - flux_avg; if (ad < 0) ad = -ad;
        flux_dev = flux_dev * 0.95f + ad * 0.05f;
        float bl = (kdb - (spec_ref - 45.0f)) / 45.0f;
        if (bl < 0) bl = 0;
        if (bl > 1) bl = 1;
        bass_lvl = bl > bass_lvl ? bl : bass_lvl * 0.8f + bl * 0.2f;
        (void)kick_fast; (void)kick_slow; (void)kick_dev;
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
        ogg_scan -= cb_start;
        if (ogg_scan < 0) ogg_scan = 0;
        if (ogg_bos_at >= 0) ogg_bos_at -= cb_start;
        cb_end -= cb_start;
        cb_start = 0;
    }
    if (n > CBUF_SIZE - cb_end) n = CBUF_SIZE - cb_end;
    memcpy(cbuf + cb_end, d, n);
    cb_end += n;
}

// ---------- Traitement du son : volume egalise, egaliseur, fondu enchaine ----------
/* Tout le son decode passe par out_put() avant d'entrer dans la file (48 kHz). */
static float rg_gain = 1.0f;                 /* volume egalise (ReplayGain) du morceau en cours */
static int eq_bass = 0, eq_treble = 0;       /* egaliseur : graves / aigus en dB (-12..+12) */
static int eq_mid = 0;                       /* mediums (voix), en cloche autour de 1,2 kHz */
static int eq_lowmid = 0, eq_pres = 0;       /* v73 : bas-mediums (350 Hz) et presence (4 kHz) : egaliseur 5 bandes */
static int eq_preset = 0;                    /* reglage tout pret (0 = personnalise) */
static int eq_on = 0;
static float eq_c[5][5];                     /* filtres : graves, aigus (plateau), mediums, bas-mediums, presence (cloche) : b0 b1 b2 a1 a2 */
static float eq_s[5][2][4];                  /* [filtre][voie] : x1 x2 y1 y2 */
#define EQ_PRESETS 16
static const char *const eq_names[EQ_PRESETS] = { "Personnalis\xC3\xA9", "Plat", "Rap", "Rock", "Pop", "Basses", "Voix", "\xC3\x89lectro", "Classique", "Jazz",
                                                  "M\xC3\xA9tal", "Reggae", "Acoustique", "Soir\xC3\xA9" "e", "R&B / Soul", "Petit volume" };
static const char *const eq_keys[EQ_PRESETS] = { "perso", "plat", "rap", "rock", "pop", "basses", "voix", "electro", "classique", "jazz",
                                                 "metal", "reggae", "acoustique", "soiree", "rnb", "petit_volume" };
static const s8 eq_vals[EQ_PRESETS][5] = {   /* v73 : graves, bas-mediums, mediums, presence, aigus */
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 8, 2, -1, 1, 2 }, { 5, -1, -2, 3, 4 }, { 2, -1, 2, 3, 3 }, { 10, 3, 0, 0, 0 }, { -3, -1, 4, 3, 1 },
    { 6, 0, -2, 2, 5 }, { 0, 0, 0, 1, 3 }, { 3, 2, 1, -1, 2 }, { 4, -2, -3, 4, 5 }, { 6, 2, -2, 0, 2 }, { 1, 2, 1, 2, 2 }, { 7, 1, -2, 2, 4 },
    { 5, 3, 1, 1, 2 }, { 6, 2, 0, 2, 5 } };
static int xf_sec = 0;                       /* fondu enchaine (secondes, 0 = non) */
static int xf_request = 0;                   /* longueur du fondu demande pour le prochain morceau */
static int xf_left = 0, xf_total = 0, xf_pos = 0;   /* zone de la file en cours de melange */
static u32 consumed_skip = 0;                /* fin du morceau precedent encore a jouer (fondu) */

/* filtres en plateau (Robert Bristow-Johnson, "Audio EQ Cookbook") */
static void eq_shelf(float *c, int high, float f0, float db) {
    float A = powf(10.0f, db / 40.0f), w = 2.0f * PI_F * f0 / 48000.0f;
    float cs = cosf(w), al = sinf(w) / 2.0f * sqrtf(2.0f), sa = 2.0f * sqrtf(A) * al;
    float b0, b1, b2, a0, a1, a2;
    if (!high) {
        b0 = A * ((A + 1) - (A - 1) * cs + sa);  b1 = 2 * A * ((A - 1) - (A + 1) * cs);
        b2 = A * ((A + 1) - (A - 1) * cs - sa);  a0 = (A + 1) + (A - 1) * cs + sa;
        a1 = -2 * ((A - 1) + (A + 1) * cs);      a2 = (A + 1) + (A - 1) * cs - sa;
    } else {
        b0 = A * ((A + 1) + (A - 1) * cs + sa);  b1 = -2 * A * ((A - 1) + (A + 1) * cs);
        b2 = A * ((A + 1) + (A - 1) * cs - sa);  a0 = (A + 1) - (A - 1) * cs + sa;
        a1 = 2 * ((A - 1) - (A + 1) * cs);       a2 = (A + 1) - (A - 1) * cs - sa;
    }
    c[0] = b0 / a0; c[1] = b1 / a0; c[2] = b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0;
}

/* cloche (peaking) : f0 et largeur fixes */
static void eq_peak(float *c, float f0, float db) {
    float A = powf(10.0f, db / 40.0f), w = 2.0f * PI_F * f0 / 48000.0f;
    float al = sinf(w) / (2.0f * 0.9f), cs = cosf(w);
    float b0 = 1 + al * A, b1 = -2 * cs, b2 = 1 - al * A, a0 = 1 + al / A, a1 = -2 * cs, a2 = 1 - al / A;
    c[0] = b0 / a0; c[1] = b1 / a0; c[2] = b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0;
}

static void eq_setup(void) {
    if (eq_bass < -12) eq_bass = -12;
    if (eq_bass > 12) eq_bass = 12;
    if (eq_treble < -12) eq_treble = -12;
    if (eq_treble > 12) eq_treble = 12;
    eq_shelf(eq_c[0], 0, 200.0f, (float)eq_bass);
    eq_shelf(eq_c[1], 1, 8000.0f, (float)eq_treble);   /* (v73 : 8 kHz, la presence est a 4 kHz) */
    if (eq_mid < -12) eq_mid = -12;
    if (eq_mid > 12) eq_mid = 12;
    eq_peak(eq_c[2], 1200.0f, (float)eq_mid);
    if (eq_lowmid < -12) eq_lowmid = -12;
    if (eq_lowmid > 12) eq_lowmid = 12;
    if (eq_pres < -12) eq_pres = -12;
    if (eq_pres > 12) eq_pres = 12;
    eq_peak(eq_c[3], 350.0f, (float)eq_lowmid);
    eq_peak(eq_c[4], 4000.0f, (float)eq_pres);
    eq_on = (eq_bass != 0 || eq_treble != 0 || eq_mid != 0 || eq_lowmid != 0 || eq_pres != 0);
}

static inline float eq_run(int f, int ch, float x) {
    float *c = eq_c[f], *s = eq_s[f][ch];
    float y = c[0] * x + c[1] * s[0] + c[2] * s[1] - c[3] * s[2] - c[4] * s[3];
    if (y > -1e-12f && y < 1e-12f) y = 0.0f;          /* pas de nombres denormalises */
    s[1] = s[0]; s[0] = x; s[3] = s[2]; s[2] = y;
    return y;
}

static inline s16 clamp16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (s16)v;
}

/* ---------- v72 : volume egalise sans etiquette ReplayGain : la sonie du morceau est mesuree
   pendant qu'il joue (EBU R128 / ReplayGain 2 : filtre K, blocs de 400 ms, portes -70 LUFS et
   -10 LU), gardee dans SD:/gc-mp3/volumes.txt, et appliquee aux ecoutes suivantes (cible -18 LUFS) ---------- */
#define RGM_MAXB 3000                                /* (20 min de blocs de 400 ms) */
static float rgm_blk[RGM_MAXB];
static int rgm_nb = 0, rgm_on = 0, rgm_n = 0, rgm_track = -1;
static double rgm_acc = 0;
static float rgm_s[2][2][4], rgm_peak = 0;
static inline float rgm_bq(float *s, const float *b, const float *a, float x) {
    float y = b[0] * x + b[1] * s[0] + b[2] * s[1] - a[1] * s[2] - a[2] * s[3];
    if (y > -1e-15f && y < 1e-15f) y = 0;
    s[1] = s[0]; s[0] = x; s[3] = s[2]; s[2] = y;
    return y;
}
static inline void rgm_put(s16 l, s16 r) {
    static const float b1[3] = { 1.53512485958697f, -2.69169618940638f, 1.19839281085285f }, a1[3] = { 1, -1.69065929318241f, 0.73248077421585f };
    static const float b2[3] = { 1.0f, -2.0f, 1.0f }, a2[3] = { 1, -1.99004745483398f, 0.99007225036621f };
    float xl = l * (1.0f / 32768.0f), xr = r * (1.0f / 32768.0f);
    float al = xl < 0 ? -xl : xl, ar = xr < 0 ? -xr : xr;
    if (al > rgm_peak) rgm_peak = al;
    if (ar > rgm_peak) rgm_peak = ar;
    float zl = rgm_bq(rgm_s[0][1], b2, a2, rgm_bq(rgm_s[0][0], b1, a1, xl));
    float zr = rgm_bq(rgm_s[1][1], b2, a2, rgm_bq(rgm_s[1][0], b1, a1, xr));
    rgm_acc += (double)zl * zl + (double)zr * zr;
    if (++rgm_n >= 19200) {                              /* (400 ms a 48 kHz) */
        if (rgm_nb < RGM_MAXB) rgm_blk[rgm_nb++] = (float)(rgm_acc / 19200.0);
        rgm_acc = 0; rgm_n = 0;
    }
}
static int out_put2(s16 l, s16 r);
/* ---------- v73 : podcasts plus rapides (x1,25 / x1,5) sans voix aigue, et blancs sautes ----------
   Etirement du temps WSOLA : des morceaux de 32 ms (fenetre de Hann, recouvrement de moitie) sont
   pris dans le son a la vitesse voulue ; chacun est decale de +-5 ms pour se raccorder au mieux au
   precedent (correlation), puis additionne : le debit change, pas la hauteur de la voix.
   Les blancs (plus de 0,25 s sous -42 dB) ne sont plus mis dans la file. La position du morceau
   (consumed) avance de la vitesse a chaque echantillon entendu, plus ce qui a ete saute. */
#define TS_N 1536
#define TS_H (TS_N / 2)
#define TS_T 240
#define TS_CAP 16384
static int ts_want = 0;                      /* 1 = podcast en cours avec vitesse ou blancs (x73_pump) */
static int ts_speed = 100, ts_sil = 0;       /* reglages : 100 / 125 / 150 %, sauter les blancs */
static volatile u32 ts_rate_q8 = 256;        /* vitesse du son dans la file (fill_buf), en 1/256 */
static volatile u32 ts_credit = 0;           /* echantillons de blanc sautes, a ajouter a consumed */
static s16 ts_L[TS_CAP], ts_R[TS_CAP];
static u32 ts_base = 0, ts_end = 0;          /* indices absolus : ts_L[0] = ts_base */
static double ts_pos = 0;                    /* debut nominal du prochain morceau */
static u32 ts_nat = 0;                       /* suite naturelle du morceau precedent */
static int ts_first = 1, ts_on = 0, ts_q = 0, ts_qi = 0, ts_quiet = 0;
static float ts_tail[TS_H][2], ts_win[TS_N];
static s16 ts_out[TS_H][2];
static void ts_reset(void) {
    ts_base = ts_end = 0; ts_pos = 0; ts_nat = 0; ts_first = 1; ts_q = ts_qi = 0;
    memset(ts_tail, 0, sizeof(ts_tail));
}
static inline float ts_m(u32 i) { u32 k = i - ts_base; return (float)ts_L[k] + (float)ts_R[k]; }
static void ts_step(void) {
    if (ts_win[TS_H] == 0.0f) for (int i = 0; i < TS_N; i++) ts_win[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * i / TS_N);
    u32 s = (u32)ts_pos;
    if (!ts_first) {                                      /* meilleur raccord avec la suite du precedent */
        double best = -1e30;
        int bd = 0;
        u32 lo = s >= ts_base + TS_T ? s - TS_T : ts_base;
        for (u32 c = lo; c <= s + TS_T; c += 3) {
            float xy = 0, xx = 1.0f;
            for (int j = 0; j < TS_H; j += 6) { float a = ts_m(c + j), b = ts_m(ts_nat + j); xy += a * b; xx += a * a; }
            double sc = xy / sqrtf(xx);
            if (sc > best) { best = sc; bd = (int)(c - s); }
        }
        s = (u32)((int)s + bd);
    }
    for (int i = 0; i < TS_H; i++) {
        u32 k = s + i - ts_base, k2 = k + TS_H;
        float w = ts_win[i], w2 = ts_win[i + TS_H];
        ts_out[i][0] = clamp16(ts_tail[i][0] + ts_L[k] * w);
        ts_out[i][1] = clamp16(ts_tail[i][1] + ts_R[k] * w);
        ts_tail[i][0] = ts_L[k2] * w2; ts_tail[i][1] = ts_R[k2] * w2;
    }
    ts_first = 0;
    ts_nat = s + TS_H;
    ts_pos += TS_H * ts_speed / 100.0;
    ts_q = TS_H; ts_qi = 0;
    u32 keep = (u32)ts_pos > ts_base + TS_T ? (u32)ts_pos - TS_T : ts_base;   /* ce qui ne sert plus */
    if (ts_nat < keep) keep = ts_nat;
    if (keep > ts_base + TS_CAP / 2) {
        u32 d = keep - ts_base;
        memmove(ts_L, ts_L + d, (ts_end - keep) * sizeof(s16));
        memmove(ts_R, ts_R + d, (ts_end - keep) * sizeof(s16));
        ts_base = keep;
    }
}
static int out_put(s16 l, s16 r) {
    if (!ts_want) {
        if (ts_on) { ts_on = 0; ts_reset(); ts_rate_q8 = 256; }
        return out_put2(l, r);
    }
    if (!ts_on) { ts_on = 1; ts_reset(); ts_quiet = 0; }
    while (ts_q > 0) {                                    /* d'abord ce qui attend */
        if (!out_put2(ts_out[ts_qi][0], ts_out[ts_qi][1])) return 0;
        ts_qi++; ts_q--;
    }
    if (ts_sil) {                                         /* blancs */
        int a = l < 0 ? -l : l, b = r < 0 ? -r : r;
        if ((a > b ? a : b) < 260) ts_quiet++; else ts_quiet = 0;
        if (ts_quiet > 12000) { ts_credit++; return 1; }
    }
    ts_rate_q8 = (u32)(ts_speed * 256 / 100);
    if (ts_speed == 100) return out_put2(l, r);
    if (ts_end - ts_base >= TS_CAP) ts_reset();          /* (ne devrait pas arriver) */
    ts_L[ts_end - ts_base] = l; ts_R[ts_end - ts_base] = r; ts_end++;
    if (ts_end >= (u32)ts_pos + TS_T + TS_N && ts_end >= ts_nat + TS_H + TS_T) ts_step();
    while (ts_q > 0) {
        if (!out_put2(ts_out[ts_qi][0], ts_out[ts_qi][1])) break;
        ts_qi++; ts_q--;
    }
    return 1;
}
static int out_put2(s16 l, s16 r) {
    if (rgm_on) rgm_put(l, r);
    if (rg_gain != 1.0f || eq_on) {
        float fl = (float)l * rg_gain, fr = (float)r * rg_gain;
        if (eq_on) {
            fl = eq_run(1, 0, eq_run(0, 0, fl));
            fr = eq_run(1, 1, eq_run(0, 1, fr));
            if (eq_mid) { fl = eq_run(2, 0, fl); fr = eq_run(2, 1, fr); }
            if (eq_lowmid) { fl = eq_run(3, 0, fl); fr = eq_run(3, 1, fr); }
            if (eq_pres) { fl = eq_run(4, 0, fl); fr = eq_run(4, 1, fr); }
        }
        l = clamp16(fl);
        r = clamp16(fr);
    }
    if (xf_left > 0) {
        /* fondu enchaine : le debut du nouveau morceau se melange a la fin de
           l'ancien, deja dans la file. Ce qui a deja ete joue est saute. */
        int pos = xf_pos;
        int d = (pos - f_head + FIFO_SIZE) % FIFO_SIZE;
        float p = 1.0f - (float)xf_left / (float)xf_total;
        xf_pos = (xf_pos + 1) % FIFO_SIZE;
        xf_left--;
        if (d < f_count) {
            fifo[pos].l = clamp16(fifo[pos].l * (1.0f - p) + l * p);
            fifo[pos].r = clamp16(fifo[pos].r * (1.0f - p) + r * p);
        }
        return 1;
    }
    return fifo_put(l, r);
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
        if (!out_put((s16)(al + (bl - al) * fr), (s16)(ar + (br - ar) * fr)))
            break;
        rpos += step;
    }
    rpos -= n;
    prevL = L[n - 1];
    prevR = R[n - 1];
}

/* Decode une trame. Retourne 0 s'il faut attendre plus de donnees. */
/* AAC / HE-AAC (flux ADTS) avec le decodeur Helix */
static HAACDecoder aac_dec = NULL;
static int decode_aac_one(int closed) {
    static short out[AAC_MAX_NCHANS * AAC_MAX_NSAMPS * 2];
    static s16 L[2048], R[2048];
    if (!aac_dec) {
        aac_dec = AACInitDecoder();
        if (!aac_dec) return 0;
    }
    int avail = cbuf_avail();
    if (avail <= 0) return 0;
    if (avail < 6144 && !closed) return 0;                  /* une trame complete au moins */
    unsigned char *p = cbuf + cb_start;
    int off = AACFindSyncWord(p, avail);
    if (off < 0) {
        int drop = closed ? avail : avail - 8;
        if (drop > 0) cb_start += drop;
        return 0;
    }
    if (off > 0) { cb_start += off; return 1; }
    unsigned char *ptr = p;
    int left = avail;
    int err = AACDecode(aac_dec, &ptr, &left, out);
    int used = avail - left;
    if (err) {
        if (err == ERR_AAC_INDATA_UNDERFLOW && !closed) return 0;
        cb_start += used > 0 ? used : 1;                    /* on se recale plus loin */
        return 1;
    }
    cb_start += used > 0 ? used : 1;
    AACFrameInfo fi;
    AACGetLastFrameInfo(aac_dec, &fi);
    int ch = fi.nChans > 0 ? fi.nChans : 1;
    int n = fi.outputSamps / ch;
    if (n <= 0 || fi.sampRateOut <= 0) return 1;
    if (n > 2048) n = 2048;
    for (int i = 0; i < n; i++) {
        L[i] = out[i * ch];
        R[i] = ch > 1 ? out[i * ch + 1] : L[i];
    }
    if (fi.bitRate > 0) stream_kbps = fi.bitRate / 1000;
    stream_hz = fi.sampRateOut;
    stream_ch = ch;
    push_frame(L, R, n, (unsigned)fi.sampRateOut);
    return 1;
}

static int decode_ogg_one(int closed);
static int decode_mp4_stream_one(int closed);       /* M4A en direct (mp4.c) */
static int decode_stream_one(int stream_closed) {
    static s16 L[1152], R[1152];
    if (stream_codec == 1) return decode_aac_one(stream_closed);
    if (stream_codec == 3) return decode_mp4_stream_one(stream_closed);
    if (stream_codec == 2) return decode_ogg_one(stream_closed);
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

/* v22 : les fichiers de GC-MP3 qui etaient a la racine de la carte SD vont
   dans SD:/gc-mp3 (une seule fois, au demarrage). Les dossiers de musique,
   photos, videos, retro, ecran, telechargements et apps restent a la racine. */
static int gcd_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static int gcd_moved = 0;
static void gcd_move_dir(const char *from, const char *to) {
    DIR *d = opendir(from);
    if (!d) return;
    mkdir(to, 0777);
    struct dirent *e;
    char a[NAME_LEN], b[NAME_LEN];
    int left = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(a, sizeof(a), "%s/%s", from, e->d_name);
        snprintf(b, sizeof(b), "%s/%s", to, e->d_name);
        if (gcd_exists(b) || rename(a, b) != 0) left++; else gcd_moved++;
    }
    closedir(d);
    if (!left) remove(from);                             /* (dossier vide) */
}
static void gcd_migrate(void) {
    static const char *const files[] = { "gc-mp3.txt", "gc-mp3-bib.txt", "gc-mp3-ecoutes.txt", "gc-mp3-jeux.txt", "gc-mp3-titres.txt",
        "gc-mp3-crash.txt", "gc-mp3-crash-ancien.txt", "cookies.txt", "favoris.txt", "podcasts.txt", "radio.txt", NULL };
    static const char *const dirs[] = { "listes", "cache", "podcasts", "radio", NULL };
    char a[NAME_LEN], b[NAME_LEN];
    mkdir(GCD, 0777);
    for (int i = 0; files[i]; i++) {
        snprintf(a, sizeof(a), SD_ROOT "/%s", files[i]);
        snprintf(b, sizeof(b), GCD "/%s", files[i]);
        if (gcd_exists(a) && !gcd_exists(b) && rename(a, b) == 0) gcd_moved++;
    }
    for (int i = 0; dirs[i]; i++) {
        snprintf(a, sizeof(a), SD_ROOT "/%s", dirs[i]);
        snprintf(b, sizeof(b), GCD "/%s", dirs[i]);
        if (!gcd_exists(a)) continue;
        if (!gcd_exists(b) && rename(a, b) == 0) { gcd_moved++; continue; }
        gcd_move_dir(a, b);                              /* (le nouveau dossier existe deja) */
        if (!strcmp(dirs[i], "radio")) {                 /* sous-dossier des enregistrements */
            char c[NAME_LEN];
            snprintf(c, sizeof(c), GCD "/radio/enregistrements");
            snprintf(a, sizeof(a), SD_ROOT "/radio/enregistrements");
            if (gcd_exists(a)) { gcd_move_dir(a, c); snprintf(a, sizeof(a), SD_ROOT "/radio"); remove(a); }
        }
    }
}

static int sd_slot = -1;                               /* v26 : 0 = SD2SP2, 1 = slot A, 2 = slot B (pour le lanceur de secours) */
static int mount_sd(void) {
    /* v97 : cache de libfat double (8 pages de 64 secteurs = 256 Ko au lieu de 128) : sur une grosse carte
       (1 To, FAT de 120 Mo), les chaines de clusters et les dossiers sont relus bien moins souvent */
    if (fatMount("sd", &__io_gcsd2, 0, 8, 64)) { sd_slot = 0; return 1; }   // SD2SP2 (port serie 2)
    if (fatMount("sd", &__io_gcsda, 0, 8, 64)) { sd_slot = 1; return 1; }   // SD Gecko slot A
    if (fatMount("sd", &__io_gcsdb, 0, 8, 64)) { sd_slot = 2; return 1; }   // SD Gecko slot B
    return 0;
}

#include "lang.c"                                      /* v24 : interface en francais ou en anglais */
#include "ramfs.c"                                     /* pas de carte SD (Dolphin) : SD virtuelle */

// ---------- Pochettes ----------
static u16 cover_tex[COVER_SZ * COVER_SZ] ATTRIBUTE_ALIGN(32);
static int cover_ok = 0;

/* Recadre au carre (centre) et reduit en 128x128 (RGB565, tuiles 4x4).
   'pump' = continuer a servir le son (fil principal seulement). */
static void cover_scale(const unsigned char *px, int w, int h, u16 *out, int pump) {
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
            out[blk * 16 + (y & 3) * 4 + (x & 3)] =
                (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
        if (pump && (y & 15) == 0) service_audio();
    }
}

/* ---- v19 : pochettes rapides ----
   Au changement de morceau, l'image n'est plus decodee tout de suite (jusqu'a
   1 s pour une grande pochette) : le son demarre aussitot, l'image est decodee
   par le fil de decodage, puis gardee toute prete (128x128, 32 Ko) dans
   sd:/gc-mp3/cache/pochettes. La fois suivante, elle s'affiche immediatement. */
#define CV_DIR GCD "/cache/pochettes"
static int cv_defer = 0;                    /* 1 = decoder en tache de fond (load_cover) */
static volatile int cv_state = 0;           /* 0 libre, 1 a decoder (fil), 2 fini */
static unsigned char *volatile cv_in = NULL;   /* travail du fil */
static volatile long cv_len = 0;
static u16 *volatile cv_out = NULL;
static volatile int cv_ok = 0;
static u32 cv_gen = 0, cv_job_gen = 0;      /* (le morceau a peut-etre change entre-temps) */
static char cv_job_key[NAME_LEN + 32];
static unsigned char *cv_pend = NULL;       /* image en attente que le fil soit libre */
static long cv_pend_len = 0;
static u32 cv_pend_gen = 0;
static char cv_pend_key[NAME_LEN + 32];
static char cv_key[NAME_LEN + 32];          /* cache du morceau en cours ("" = pas de cache) */
static int sd_ok;
static int dec_ensure(void);

static void cv_cache_name(const char *key, char *out, int cap) {
    u32 h1 = 2166136261u, h2 = 5381;
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) { h1 = (h1 ^ *p) * 16777619u; h2 = h2 * 33 + *p; }
    snprintf(out, cap, CV_DIR "/%08x%08x.565", (unsigned)h1, (unsigned)h2);
}
static int cv_cache_read(const char *key) {
    if (!key[0]) return 0;
    char path[NAME_LEN];
    cv_cache_name(key, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char hd[4];
    int ok = fread(hd, 1, 4, f) == 4 && !memcmp(hd, "GCV1", 4) && fread(cover_tex, 1, sizeof(cover_tex), f) == sizeof(cover_tex);
    fclose(f);
    if (!ok) return 0;
    DCFlushRange(cover_tex, sizeof(cover_tex));
    GX_InvalidateTexAll();
    return 1;
}
static void cv_cache_write(const char *key, const u16 *tex) {
    if (!key[0] || !sd_ok || sd_virtual) return;
    char path[NAME_LEN];
    mkdir(GCD "/cache", 0777);
    mkdir(CV_DIR, 0777);
    cv_cache_name(key, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int ok = fwrite("GCV1", 1, 4, f) == 4 && fwrite(tex, 1, COVER_SZ * COVER_SZ * 2, f) == COVER_SZ * COVER_SZ * 2;
    fclose(f);
    if (!ok) remove(path);
}

/* travail du fil de decodage */
static void cover_job(void) {
    int w, h, n;
    cv_ok = 0;
    if (cv_len <= 0 || !stbi_info_from_memory(cv_in, (int)cv_len, &w, &h, &n) || w <= 0 || h <= 0 || (long)w * (long)h > COVER_MAX_PX) return;
    unsigned char *px = stbi_load_from_memory(cv_in, (int)cv_len, &w, &h, &n, 3);
    if (!px) return;
    u16 *out = (u16 *)memalign(32, COVER_SZ * COVER_SZ * 2);
    if (out) { cover_scale(px, w, h, out, 0); cv_out = out; cv_ok = 1; }
    stbi_image_free(px);
}

/* chaque image : resultat du fil, puis image suivante */
static void cover_async_pump(void) {
    if (cv_state == 2) {
        if (cv_ok && cv_out && cv_job_gen == cv_gen) {
            memcpy(cover_tex, cv_out, sizeof(cover_tex));
            DCFlushRange(cover_tex, sizeof(cover_tex));
            GX_InvalidateTexAll();
            cover_ok = 1;
            cv_cache_write(cv_job_key, cv_out);
        }
        free(cv_out);
        cv_out = NULL;
        free(cv_in);
        cv_in = NULL;
        cv_state = 0;
    }
    if (cv_state == 0 && cv_pend) {
        if (cv_pend_gen != cv_gen || !dec_ensure()) { free(cv_pend); cv_pend = NULL; return; }
        cv_in = cv_pend;
        cv_len = cv_pend_len;
        cv_job_gen = cv_pend_gen;
        snprintf(cv_job_key, sizeof(cv_job_key), "%s", cv_pend_key);
        cv_pend = NULL;
        cv_state = 1;
    }
}
static int cover_busy(void) { return cv_pend != NULL || (cv_state != 0 && cv_job_gen == cv_gen); }

/* Decode une image JPEG/PNG en memoire et la met dans la texture de la pochette. */
static int cover_from_memory(const unsigned char *data, long len) {
    int w, h, n;
    if (len <= 0 || len > 0x7fffffffL) return 0;
    if (!stbi_info_from_memory(data, (int)len, &w, &h, &n)) return 0;
    if (w <= 0 || h <= 0 || (long)w * (long)h > COVER_MAX_PX) return 0;
    if (cv_defer) {                                       /* en tache de fond */
        unsigned char *copy = (unsigned char *)malloc(len);
        if (copy) {
            memcpy(copy, data, len);
            free(cv_pend);
            cv_pend = copy;
            cv_pend_len = len;
            cv_pend_gen = cv_gen;
            snprintf(cv_pend_key, sizeof(cv_pend_key), "%s", cv_key);
            return 2;                                     /* (trouvee, image a venir) */
        }
    }
    unsigned char *px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 3);
    if (!px) return 0;
    cover_scale(px, w, h, cover_tex, 1);
    stbi_image_free(px);
    DCFlushRange(cover_tex, sizeof(cover_tex));
    GX_InvalidateTexAll();
    if (cv_key[0]) cv_cache_write(cv_key, cover_tex);
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
// ---------- Liste des stations (sd:/gc-mp3/radio.txt) ----------
typedef struct { char name[48]; char url[URL_MAX]; } station_t;
static station_t stations[MAX_STATIONS];
static int nstations = 0;
static int sd_ok = 0;
static int list_created = 0;            /* 1 si radio.txt vient d'etre cree */

static const char *DEFAULT_LIST =
    "# GC-Radio : une station par ligne, au format   Nom | URL\n"
    "# Flux MP3, AAC ou OGG Vorbis en http:// ou https://, et radios HLS (.m3u8).\n"
    "# Les listes .m3u et .pls sont acceptees. Les lignes qui commencent\n"
    "# par # sont ignorees. Logo facultatif : sd:/gc-mp3/radio/<Nom>.jpg ou .png\n"
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

static void radio_fav_load(void);
static void load_stations(void) {
    static char line[URL_MAX + 128];
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
    radio_fav_load();                          /* v72 : favorites en haut */
}

// ---------- Reseau ----------
static char ip_str[16] = "", mask_str[16] = "", gw_str[16] = "";
static int net_static = 0;                   /* v34 : adresse fixe (v75 : reglage de GC Station, section Reseau) */
static void net_kb_done(const char *t);       /* v75 (extras73.c) */
static int net_dhcp = 1, net_cfg_bits = 24;  /* v75 : adresse automatique (DHCP), ou adresse fixe : */
static char net_cfg_ip[16] = "", net_cfg_gw[16] = "";
static int net_ok = 0;
enum { NET_NONE, NET_BUSY, NET_DONE };
static volatile int net_phase = NET_NONE;
static lwp_t net_thread_h;
static u8 net_thread_stack[64 * 1024] ATTRIBUTE_ALIGN(32);

/* Initialisation du reseau (DHCP : jusqu'a 20 x 0,5 s) dans un thread a part,
   pour que la musique continue pendant ce temps. */
static void *net_thread(void *arg) {
    (void)arg;
    s32 r = if_config(ip_str, mask_str, gw_str, net_static ? FALSE : TRUE, 20);
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

/* v83 : verrous (mbedTLS, cache DNS) : actifs des que le fil des connexions existe */
static mutex_t tls_mx = LWP_MUTEX_NULL, dns_mx = LWP_MUTEX_NULL;
static volatile int tls_mx_ok = 0;
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

static int resolve_host_raw(const char *host, u32 *out);
static int resolve_host(const char *host, u32 *out) {          /* v83 : (2 fils : un verrou pour le cache) */
    if (tls_mx_ok) LWP_MutexLock(dns_mx);
    int r = resolve_host_raw(host, out);
    if (tls_mx_ok) LWP_MutexUnlock(dns_mx);
    return r;
}
static int resolve_host_raw(const char *host, u32 *out) {
    /* adresse IP ecrite directement ? */
    int dots = 0, digits = 1;
    for (const char *p = host; *p; p++) {
        if (*p == '.') dots++;
        else if (!isdigit((unsigned char)*p)) digits = 0;
    }
    if (digits && dots == 3) { if (out) *out = inet_addr(host); return out != NULL; }   /* (v72 : "oublier" une adresse IP : rien a faire, avant : plantage) */

    /* petit cache : evite une requete DNS par image d'un meme site */
    static struct { char host[96]; u32 addr; u64 t; } cache[16];   /* (v19 : 16 sites, 30 min) */
    static int cnext = 0;
    if (out == NULL) {                       /* v24 : oublier ce site (adresse peut-etre perimee) */
        for (int i = 0; i < 16; i++) if (!strcmp(cache[i].host, host)) cache[i].host[0] = 0;
        return 0;
    }
    for (int i = 0; i < 16; i++)
        if (cache[i].host[0] && strcmp(cache[i].host, host) == 0 && now_ms() - cache[i].t < 1800000) {
            *out = cache[i].addr;
            return 1;
        }

    u32 servers[3];
    int ns = 0;
    if (gw_str[0]) servers[ns++] = inet_addr(gw_str);
    servers[ns++] = inet_addr("1.1.1.1");
    servers[ns++] = inet_addr("8.8.8.8");
    for (int i = 0; i < ns; i++)
        if (dns_query(servers[i], host, out)) {
            if (strlen(host) < sizeof(cache[0].host)) {
                snprintf(cache[cnext].host, sizeof(cache[0].host), "%s", host);
                cache[cnext].addr = *out;
                cache[cnext].t = now_ms();
                cnext = (cnext + 1) % 16;
            }
            return 1;
        }
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
    char host[96];                          /* "site:port" (reprise de session TLS) */
    int sc_used;                            /* 1 = poignee de main avec une session gardee */
} conn_t;
static conn_t cn = { -1, 0, 0 };          /* radio */

/* v83 : mbedTLS (et son PSA) n'est pas prevu pour plusieurs fils : un verrou le protege des
   que le fil des connexions en tache de fond existe (avant : un seul fil, pas de verrou). La
   boucle principale ne l'attend pas pour lire / ecrire : elle reessaie a l'image suivante. */
#define TLS_LOCK()   do { if (tls_mx_ok) LWP_MutexLock(tls_mx); } while (0)
#define TLS_UNLOCK() do { if (tls_mx_ok) LWP_MutexUnlock(tls_mx); } while (0)
static int tls_try(void) { return !tls_mx_ok || LWP_MutexTryLock(tls_mx) == 0; }
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

static int tls_global_init_raw(void);
static int tls_global_init(void) {
    if (tls_ready) return 1;
    TLS_LOCK();
    int r = tls_global_init_raw();
    TLS_UNLOCK();
    return r;
}
static int tls_global_init_raw(void) {
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
#if defined(MBEDTLS_SSL_SESSION_TICKETS) && defined(MBEDTLS_SSL_PROTO_TLS1_3)
    mbedtls_ssl_conf_tls13_enable_signal_new_session_tickets(&tls_conf, MBEDTLS_SSL_TLS1_3_SIGNAL_NEW_SESSION_TICKETS_ENABLED);
#endif
    tls_ready = 1;
    return 1;
}

/* v19 : reprise de session TLS. La premiere connexion a un site coute cher a la
   GameCube (calculs de cles, verification des certificats : jusqu'a 1 s) ; on
   garde la session des derniers sites pour que les suivantes soient bien plus
   rapides (paroles, pochettes, meteo, podcasts...). */
#define TLS_SC 8
typedef struct { char host[96]; int ok; u32 used; mbedtls_ssl_session s; } tls_sc_t;
static tls_sc_t tls_sc[TLS_SC];
static u32 tls_sc_clock = 0;
static void tls_sc_put_raw(mbedtls_ssl_context *ssl, const char *host);
static void tls_sc_put(mbedtls_ssl_context *ssl, const char *host) { TLS_LOCK(); tls_sc_put_raw(ssl, host); TLS_UNLOCK(); }
static void tls_sc_put_raw(mbedtls_ssl_context *ssl, const char *host) {
    if (!host || !host[0]) return;
    int k = -1;
    for (int i = 0; i < TLS_SC; i++) if (tls_sc[i].ok && !strcmp(tls_sc[i].host, host)) k = i;
    if (k < 0) for (int i = 0; i < TLS_SC; i++) if (!tls_sc[i].ok) { k = i; break; }
    if (k < 0) { k = 0; for (int i = 1; i < TLS_SC; i++) if (tls_sc[i].used < tls_sc[k].used) k = i; }
    mbedtls_ssl_session tmp;
    mbedtls_ssl_session_init(&tmp);
    if (mbedtls_ssl_get_session(ssl, &tmp) != 0) { mbedtls_ssl_session_free(&tmp); return; }
    if (tls_sc[k].ok) mbedtls_ssl_session_free(&tls_sc[k].s);
    tls_sc[k].s = tmp;
    snprintf(tls_sc[k].host, sizeof(tls_sc[k].host), "%s", host);
    tls_sc[k].ok = 1;
    tls_sc[k].used = ++tls_sc_clock;
}
static int tls_sc_use(mbedtls_ssl_context *ssl, const char *host) {
    int r = 0;
    TLS_LOCK();
    for (int i = 0; i < TLS_SC; i++)
        if (tls_sc[i].ok && !strcmp(tls_sc[i].host, host)) {
            if (mbedtls_ssl_set_session(ssl, &tls_sc[i].s) == 0) { tls_sc[i].used = ++tls_sc_clock; r = 1; }
            break;
        }
    TLS_UNLOCK();
    return r;
}
static void tls_sc_drop(const char *host) {          /* session refusee : on l'oublie */
    TLS_LOCK();
    for (int i = 0; i < TLS_SC; i++)
        if (tls_sc[i].ok && !strcmp(tls_sc[i].host, host)) { mbedtls_ssl_session_free(&tls_sc[i].s); tls_sc[i].ok = 0; }
    TLS_UNLOCK();
}
static void tls_sc_done(conn_t *c) {                 /* poignee de main finie (TLS 1.2 : session a garder) */
    TLS_LOCK();
    if (mbedtls_ssl_get_version_number(&c->ssl) == MBEDTLS_SSL_VERSION_TLS1_2) tls_sc_put(&c->ssl, c->host);
    TLS_UNLOCK();
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
    if (c->ssl_inited) { TLS_LOCK(); mbedtls_ssl_free(&c->ssl); c->ssl_inited = 0; TLS_UNLOCK(); }
    if (c->fd >= 0) net_close(c->fd);
    c->fd = -1;
    c->tls = 0;
    c->sc_used = 0;
}

/* Recoit sans bloquer : > 0 octets lus, 0 fin du flux, -EAGAIN rien pour l'instant, < 0 erreur. */
static int conn_recv(conn_t *c, unsigned char *buf, int len) {
    if (c->fd < 0) return -EIO;
    if (!c->tls) return net_recv(c->fd, buf, len, 0);
    for (;;) {
        if (!tls_try()) return -EAGAIN;                    /* v83 : (le fil des connexions calcule : a l'image suivante) */
        int r = mbedtls_ssl_read(&c->ssl, buf, (size_t)len);
        TLS_UNLOCK();
        if (r > 0) return r;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return -EAGAIN;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) { tls_sc_put(&c->ssl, c->host); continue; }   /* TLS 1.3 : ticket a garder */
#endif
        if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        return -EIO;
    }
}

static int conn_send_all(conn_t *c, const void *data, int len) {
    const unsigned char *p = (const unsigned char *)data;
    u64 t0 = now_ms();
    while (len > 0) {
        int r;
        if (c->tls) {
            if (tls_try()) { r = mbedtls_ssl_write(&c->ssl, p, (size_t)len); TLS_UNLOCK(); }
            else r = MBEDTLS_ERR_SSL_WANT_WRITE;           /* v83 : (verrou pris : on reessaie) */
        } else r = net_send(c->fd, p, len, 0);
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
    TLS_LOCK();
    mbedtls_ssl_init(&c->ssl);
    c->ssl_inited = 1;
    int su = mbedtls_ssl_setup(&c->ssl, &tls_conf) != 0 || mbedtls_ssl_set_hostname(&c->ssl, host) != 0;
    TLS_UNLOCK();
    if (su) {
        *err = "Erreur d'initialisation HTTPS";
        return 0;
    }
    c->sc_used = tls_sc_use(&c->ssl, c->host);
    TLS_LOCK();
    mbedtls_ssl_set_bio(&c->ssl, &c->fd, bio_send, bio_recv, NULL);
    TLS_UNLOCK();
    c->tls = 1;
    u64 t0 = now_ms();
    for (;;) {
        TLS_LOCK();
        int r = mbedtls_ssl_handshake(&c->ssl);
        int vr = r ? (int)mbedtls_ssl_get_verify_result(&c->ssl) : 0;
        TLS_UNLOCK();
        if (r == 0) { tls_sc_done(c); return 1; }
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) { tls_sc_put(&c->ssl, c->host); continue; }
#endif
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED || vr != 0)
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
/* Connexion TCP seule (show = afficher l'avancement a l'ecran). */
static int conn_tcp(conn_t *c, const char *host, int port, const char **err, int *refused, int show) {
    char msg[200];
    if (refused) *refused = 0;
    conn_close(c);
    snprintf(msg, sizeof(msg), "Recherche de %s...", host);
    if (show) ui_status(msg);
    u32 addr;
    if (!resolve_host(host, &addr)) { *err = "Serveur introuvable (DNS)"; return 0; }

    snprintf(msg, sizeof(msg), "Connexion à %s...", host);
    if (show) ui_status(msg);
    s32 s = net_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { *err = "Erreur réseau (socket)"; return 0; }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u16)port);
    sa.sin_addr.s_addr = addr;
    s32 cr = net_connect(s, (struct sockaddr *)&sa, sizeof(sa));
    if (cr < 0 && cr != -ECONNREFUSED) {
        /* v24 : un 2e essai (premier paquet perdu, adresse du cache perimee...) */
        net_close(s);
        resolve_host(host, NULL);
        usleep(250000);
        if (!resolve_host(host, &addr)) { *err = "Serveur introuvable (DNS)"; return 0; }
        sa.sin_addr.s_addr = addr;
        s = net_socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) { *err = "Erreur réseau (socket)"; return 0; }
        cr = net_connect(s, (struct sockaddr *)&sa, sizeof(sa));
    }
    if (cr < 0) {
        net_close(s);
        *err = "Connexion refusée par le serveur";
        if (refused) *refused = 1;
        return 0;
    }
    c->fd = s;
    c->tls = 0;
    set_nonblock(s, 1);
    return 1;
}

static int conn_open(conn_t *c, const char *host, int port, int tls, const char **err, int *refused) {
    char msg[200];
    if (!conn_tcp(c, host, port, err, refused, 1)) return 0;
    if (tls) {
        snprintf(msg, sizeof(msg), "Connexion sécurisée à %s...", host);
        ui_status(msg);
        snprintf(c->host, sizeof(c->host), "%s:%d", host, port);
        if (!conn_start_tls(c, host, err)) {
            /* v24 : on reessaie une fois (session refusee, ou paquet perdu pendant
               la negociation), mais pas si le certificat est mauvais ou trop lent */
            int retry = c->sc_used || (*err && strstr(*err, "chec de la connexion"));
            conn_close(c);
            if (!retry) return 0;
            tls_sc_drop(c->host);                         /* (le site ne veut plus de l'ancienne session) */
            if (!conn_tcp(c, host, port, err, refused, 1)) return 0;
            snprintf(c->host, sizeof(c->host), "%s:%d", host, port);
            if (!conn_start_tls(c, host, err)) { conn_close(c); return 0; }
        }
    }
    return 1;
}

/* ---------- v83 : connexions en tache de fond ----------
   La recherche DNS, la connexion TCP et la poignee de main HTTPS (calculs de cles et
   verification des certificats : jusqu'a 1 s pour le Gekko) se faisaient pendant une image :
   l'ecran sautait de 200 a 300 ms a chaque parole, pochette, meteo, verification de mise a
   jour... Un fil de faible priorite (sous la boucle principale ET sous le decodage du son :
   il ne prend que le temps libre) les fait maintenant ; la boucle principale reprend la
   connexion prete. Il utilise la pile du fil du demarrage du reseau (fini avant). */
#define AC_N 6
typedef struct {
    volatile int st;                        /* 0 libre, 1 en attente, 2 en cours, 3 fini (a reprendre) */
    volatile int cancel;                    /* 1 = plus personne n'en veut : fermee par la boucle principale */
    int ok, port, tls;
    char host[128];
    conn_t c;
} acjob_t;
static acjob_t ac_job[AC_N];
static int ac_on = 0;                       /* 1 = fil lance ; -1 = impossible (on fait comme avant) */
static sem_t ac_sem;
static lwp_t ac_h;
static int ac_open(acjob_t *j) {
    const char *err = NULL;
    j->c.fd = -1; j->c.tls = 0; j->c.ssl_inited = 0; j->c.sc_used = 0;
    if (!conn_tcp(&j->c, j->host, j->port, &err, NULL, 0)) return 0;
    if (!j->tls) return 1;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!tls_global_init()) { conn_close(&j->c); return 0; }
        snprintf(j->c.host, sizeof(j->c.host), "%s:%d", j->host, j->port);
        TLS_LOCK();
        mbedtls_ssl_init(&j->c.ssl);
        j->c.ssl_inited = 1;
        int ok = mbedtls_ssl_setup(&j->c.ssl, &tls_conf) == 0 && mbedtls_ssl_set_hostname(&j->c.ssl, j->host) == 0;
        if (ok) { mbedtls_ssl_set_bio(&j->c.ssl, &j->c.fd, bio_send, bio_recv, NULL); j->c.tls = 1; }
        TLS_UNLOCK();
        if (!ok) { conn_close(&j->c); return 0; }
        j->c.sc_used = tls_sc_use(&j->c.ssl, j->c.host);
        u64 t0 = now_ms();
        int r;
        for (;;) {
            if (j->cancel) { conn_close(&j->c); return 0; }
            TLS_LOCK();
            r = mbedtls_ssl_handshake(&j->c.ssl);
            TLS_UNLOCK();
            if (r == 0) { tls_sc_done(&j->c); return 1; }
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
            if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) { tls_sc_put(&j->c.ssl, j->c.host); continue; }
#endif
            if ((r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) || now_ms() - t0 > 15000) break;
            usleep(2000);
        }
        int retry = attempt == 0 && j->c.sc_used && now_ms() - t0 <= 15000;
        if (retry) tls_sc_drop(j->c.host);                 /* session refusee : sans elle */
        conn_close(&j->c);
        if (!retry || !conn_tcp(&j->c, j->host, j->port, &err, NULL, 0)) return 0;
    }
    conn_close(&j->c);
    return 0;
}
static void *ac_thread(void *arg) {
    (void)arg;
    for (;;) {
        LWP_SemWait(ac_sem);
        for (int k = 0; k < AC_N; k++) {
            acjob_t *j = &ac_job[k];
            if (j->st != 1) continue;
            j->st = 2;
            j->ok = j->cancel ? 0 : ac_open(j);
            j->st = 3;                                     /* (la boucle principale reprend ou ferme) */
        }
    }
    return NULL;
}
static void ac_sweep(void) {                               /* (boucle principale) connexions abandonnees */
    for (int k = 0; k < AC_N; k++) if (ac_job[k].st == 3 && ac_job[k].cancel) { conn_close(&ac_job[k].c); ac_job[k].cancel = 0; ac_job[k].st = 0; }
}
static int ac_start(void) {
    if (ac_on) return ac_on > 0;
    if (net_phase != NET_DONE) return 0;
    ac_on = -1;
    LWP_JoinThread(net_thread_h, NULL);                    /* (sa pile sert au nouveau fil) */
    if (LWP_MutexInit(&tls_mx, true) < 0 || LWP_MutexInit(&dns_mx, true) < 0 || LWP_SemInit(&ac_sem, 0, 64) < 0) return 0;
    tls_mx_ok = 1;
    if (LWP_CreateThread(&ac_h, ac_thread, NULL, net_thread_stack, sizeof(net_thread_stack), 15) < 0) { tls_mx_ok = 0; return 0; }
    ac_on = 1;
    return 1;
}
/* rend le numero de la demande + 1 (0 = pas possible : connexion comme avant) */
static int ac_submit(const char *host, int port, int tls) {
    if (!ac_start()) return 0;
    ac_sweep();
    for (int k = 0; k < AC_N; k++) {
        acjob_t *j = &ac_job[k];
        if (j->st) continue;
        snprintf(j->host, sizeof(j->host), "%s", host);
        j->port = port; j->tls = tls; j->ok = 0; j->cancel = 0;
        j->st = 1;
        LWP_SemPost(ac_sem);
        return k + 1;
    }
    return 0;
}
static void ac_cancel(int id) {
    if (id < 1 || id > AC_N) return;
    ac_job[id - 1].cancel = 1;
    ac_sweep();
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
/* ---------- Enregistrement de la radio sur la carte SD ---------- */
static FILE *rec_fp = NULL;
static long rec_bytes = 0;
static int rec_sync = 0;                     /* 1 = on attend le debut d'une trame */
static char rec_name[160] = "";
static char rec_msg[160] = "";
static u64 rec_msg_t = 0;

static void rec_stop(void) {
    if (!rec_fp) return;
    fclose(rec_fp);
    rec_fp = NULL;
    snprintf(rec_msg, sizeof(rec_msg), "Enregistr\xC3\xA9 : SD:/gc-mp3/radio/enregistrements/%s", rec_name);
    rec_msg_t = now_ms();
}

static void rec_start(const char *station) {
    if (rec_fp || !sd_ok) return;
    mkdir(GCD "/radio", 0777);
    mkdir(GCD "/radio/enregistrements", 0777);
    char base[64], when[32] = "";
    snprintf(base, sizeof(base), "%s", station && station[0] ? station : "Radio");
    for (char *p = base; *p; p++) if (strchr("\\/:*?\"<>|", *p) || (unsigned char)*p < 32) *p = '_';
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (tm && tm->tm_year > 100) strftime(when, sizeof(when), " %Y-%m-%d %Hh%M", tm);
    char path[NAME_LEN];
    for (int k = 1; k < 100; k++) {
        const char *ext = stream_codec == 2 ? "ogg" : stream_codec == 1 ? "aac" : "mp3";
        if (k == 1) snprintf(rec_name, sizeof(rec_name), "%s%s.%s", base, when, ext);
        else snprintf(rec_name, sizeof(rec_name), "%s%s (%d).%s", base, when, k, ext);
        snprintf(path, sizeof(path), GCD "/radio/enregistrements/%s", rec_name);
        struct stat st;
        if (stat(path, &st) != 0) break;
    }
    rec_fp = fopen(path, "wb");
    rec_bytes = 0;
    rec_sync = 1;

    if (!rec_fp) { snprintf(rec_msg, sizeof(rec_msg), "Impossible de cr\xC3\xA9" "er le fichier d'enregistrement"); rec_msg_t = now_ms(); }
}

/* son recu (sans les metadonnees ICY) : vers le decodeur, et le fichier si on enregistre */
static void stream_audio(const unsigned char *d, int n) {
    cbuf_append(d, n);
    if (stream_codec == 3) return;           /* (M4A : pas d'enregistrement) */
    if (rec_fp && rec_sync) {                /* le fichier commence par une trame entiere */
        int i = 0;
        if (stream_codec == 2) {
            /* OGG : une page du flux dont on a les en-tetes (on les ecrit d'abord),
               ou le debut d'un nouveau flux (il contient ses en-tetes) */
            for (; i + 27 <= n; i++) {
                if (memcmp(d + i, "OggS", 4) != 0) continue;
                if (d[i + 5] & 0x02) break;
                if (ogg_hdr && ogg_hdr_len > 27 && memcmp(d + i + 14, ogg_hdr + 14, 4) == 0) {
                    if (fwrite(ogg_hdr, 1, ogg_hdr_len, rec_fp) == (size_t)ogg_hdr_len) rec_bytes += ogg_hdr_len;
                    break;
                }
            }
            if (i + 27 > n) return;
        } else while (i + 1 < n && !(d[i] == 0xFF && (d[i + 1] & 0xE0) == 0xE0 && (stream_codec != 1 || (d[i + 1] & 0xF6) == 0xF0))) i++;
        if (i + 1 >= n) return;
        d += i; n -= i;
        rec_sync = 0;
    }
    if (rec_fp && n > 0) {
        if (fwrite(d, 1, n, rec_fp) != (size_t)n) {
            fclose(rec_fp);
            rec_fp = NULL;
            snprintf(rec_msg, sizeof(rec_msg), "Enregistrement arr\xC3\xAAt\xC3\xA9 : carte SD pleine ?");
            rec_msg_t = now_ms();
        } else rec_bytes += n;
    }
}

static void stream_feed(const unsigned char *d, int n) {
    while (n > 0) {
        if (icy_metaint == 0) { stream_audio(d, n); return; }
        if (icy_state == 0) {
            int take = icy_metaint - icy_count;
            if (take > n) take = n;
            stream_audio(d, take);
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
            static char val[2112];                /* (Location des liens signes : tres longs) */
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
static int hexv(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
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

static long http_range = 0;                 /* podcasts : commencer a cet octet */
static char http11_host[128] = "";              /* v74 : serveur qui refuse HTTP/1.0 (code 426) : HTTP/1.1 */
static dechunk_t st_dc;                          /* v74 : flux en "Transfer-Encoding: chunked" */
static void podcast_leave(void);
static void podcast_finished(void);
static void podcast_play_at(int sec);
static int pod_pos_sec(void);
static int pod_starting;
static int ms_reopening;                     /* (mp4.c) reprise d'un M4A a une autre position */
static int mp4_url_looks(const char *path, const char *ctype);
static void mp4s_begin(long pos, const char *key);
static void ms_want_clear(void);
static char http_final_url[URL_MAX];       /* adresse finale (apres les redirections) */
static int http_last_code = 0;               /* 200, ou 206 si la plage demandee est respectee */
static int http_open(const char *url_in, int depth) {
    /* (statiques : appels en chaine pour les redirections, sans rien relire apres) */
    static char url[URL_MAX], path[URL_MAX], req[URL_MAX + 640];
    char host[128];
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
    {                                                    /* v72 : espaces et caracteres speciaux dans l'adresse (radio.txt) */
        static char enc[URL_MAX];
        int o = 0;
        for (const unsigned char *q = (const unsigned char *)path; *q && o < (int)sizeof(enc) - 4; q++) {
            if (*q <= ' ' || *q >= 0x7F || *q == '"' || *q == '<' || *q == '>') { static const char hx[] = "0123456789ABCDEF"; enc[o++] = '%'; enc[o++] = hx[*q >> 4]; enc[o++] = hx[*q & 15]; }
            else enc[o++] = (char)*q;
        }
        enc[o] = 0;
        snprintf(path, sizeof(path), "%s", enc);
    }
    if (port <= 0 || port > 65535) port = tls ? 443 : 80;

    const char *cerr = NULL;
    if (!conn_open(&cn, host, port, tls, &cerr, NULL)) { set_error(cerr); return 0; }

    char range[48] = "";
    if (http_range > 0) snprintf(range, sizeof(range), "Range: bytes=%ld-\r\n", http_range);
    char hosthdr[160];
    int v11 = !strcmp(host, http11_host);
    if (port == (tls ? 443 : 80)) snprintf(hosthdr, sizeof(hosthdr), "%s", host);
    else snprintf(hosthdr, sizeof(hosthdr), "%s:%d", host, port);
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.%d\r\nHost: %s\r\nUser-Agent: GC-MP3/1.0 (GameCube)\r\n"
                      "%sIcy-MetaData: 1\r\nAccept: */*\r\nConnection: close\r\n\r\n", path, v11, hosthdr, range);
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

#ifdef HTTP_TRACE
    printf("HTTP> %s%s\n<<<\n%s>>>\n", host, path, (char *)hdr);
#endif
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
        static char next[URL_MAX];
        if (loc[0] == '/' && loc[1] == '/') snprintf(next, sizeof(next), "%s:%s", tls ? "https" : "http", loc);   /* (//serveur/chemin) */
        else if (loc[0] == '/') snprintf(next, sizeof(next), "%s://%s:%d%s", tls ? "https" : "http", host, port, loc);
        else if (strncasecmp(loc, "http://", 7) && strncasecmp(loc, "https://", 8)) {   /* v72 : adresse relative au dossier */
            char dir[URL_MAX];
            snprintf(dir, sizeof(dir), "%s", path);
            dir[strcspn(dir, "?")] = 0;
            char *sl = strrchr(dir, '/');
            if (sl) sl[1] = 0; else snprintf(dir, sizeof(dir), "/");
            snprintf(next, sizeof(next), "%s://%s:%d%s%s", tls ? "https" : "http", host, port, dir, loc);
        } else snprintf(next, sizeof(next), "%s", loc);
        stream_close();
        return http_open(next, depth + 1);
    }
    if ((code == 426 || code == 505) && !v11) {               /* v74 : le serveur veut HTTP/1.1 */
        static char again[URL_MAX];
        snprintf(again, sizeof(again), "%s", url);
        snprintf(http11_host, sizeof(http11_host), "%s", host);
        stream_close();
        return http_open(again, depth + 1);
    }
    if (code != 200 && code != 206) {
        char m[64];
        snprintf(m, sizeof(m), "Erreur du serveur (HTTP %d)", code);
        set_error(m);
        return 0;
    }
    http_last_code = code;
    snprintf(http_final_url, sizeof(http_final_url), "%s", url);
    memset(&st_dc, 0, sizeof(st_dc));
    {                                                    /* v74 : reponse HTTP/1.1 en morceaux */
        const char *te = header_value(hdrs, "Transfer-Encoding");
        st_dc.on = te && strstr(te, "hunked") != NULL;
    }
    if (st_dc.on) body_len = dechunk(&st_dc, body, body_len);

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
        static char pl[32768];
        int pn = body_len < (int)sizeof(pl) - 1 ? body_len : (int)sizeof(pl) - 1;
        memcpy(pl, body, pn);
        while (pn < (int)sizeof(pl) - 1) {
            int r = recv_timeout((unsigned char *)pl + pn, (int)sizeof(pl) - 1 - pn, 5000);
            if (r <= 0) break;
            if (st_dc.on) r = dechunk(&st_dc, (unsigned char *)pl + pn, r);
            pn += r;
        }
        pl[pn] = 0;
        if (pn == (int)sizeof(pl) - 1) { char *nl = strrchr(pl, '\n'); if (nl) nl[1] = 0; }   /* derniere ligne coupee */
        static char next[URL_MAX];
        stream_close();
        if (strstr(pl, "#EXT-X-")) {                     /* radio HLS (.m3u8 en segments) */
            icy_metaint = 0;
            icy_name[0] = 0;
            return hls_begin(url, pl);
        }
        if (!playlist_first_url(pl, next, sizeof(next))) { set_error("Liste de lecture vide"); return 0; }
        return http_open(next, depth + 1);
    }
    if (strstr(ctype, "text/html") && !ms_reopening) { set_error("Ce n'est pas un flux audio (page web)"); return 0; }
    if (ms_reopening || mp4_url_looks(path, ctype) || (body_len >= 8 && !memcmp(body + 4, "ftyp", 4))) {
        stream_codec = 3;                                  /* M4A / MP4 (AAC) */
        mp4s_begin(code == 206 ? http_range : 0, stations[cur_station].url);
    } else {
        stream_codec = strstr(ctype, "aac") ? 1 : (strstr(ctype, "ogg") || strstr(ctype, "vorbis")) ? 2 : 0;
        if (strstr(ctype, "opus") || strstr(ctype, "flac")) {
            set_error("Format non géré (MP3, AAC, M4A ou OGG Vorbis)");
            return 0;
        }
    }

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
    rec_stop();
    stream_close();
    hls_stop();
    ogg_stream_reset();
    state = ST_IDLE;
    if (src == SRC_RADIO) src = SRC_NONE;
    f_head = f_tail = f_count = 0;
    cb_start = cb_end = 0;
}

static char rad_final[URL_MAX];              /* v71 : adresse finale de la station (reconnexions rapides) */
static int rad_final_idx = -1, rad_played = 0, rt_n = 0;
static u64 rt_at = 0;                         /* v71 : prochaine reconnexion automatique */
static void start_station(int idx, int is_reconnect) {
    if (idx < 0 || idx > nstations || idx >= MAX_STATIONS) return;   /* (nstations = station d'essai) */
    crumb("radio : %s | %.60s", stations[idx].name, stations[idx].url);
    if (!is_reconnect) podcast_leave();
    if (!pod_starting) { ms_want_clear(); }               /* (M4A : pas de position demandee) */
    if (src == SRC_FILE) stop_track();
    stop_station();
    src = SRC_RADIO;
    cur_station = idx;
    if (!is_reconnect) {
        reconnects = 0;
        rebuffers = 0;
        rad_played = 0; rt_n = 0; rt_at = 0;
        if (rad_final_idx != idx) rad_final[0] = 0;
        consumed = 0;
        now_title[0] = 0;
        cover_for_station(idx);
    }
    err_msg[0] = 0;
    icy_name[0] = 0;
    stream_kbps = stream_hz = stream_ch = 0;
    stream_closed = 0;
    stream_codec = 0;
    rg_gain = 1.0f;                          /* (volume egalise : fichiers seulement) */
    if (aac_dec) { AACFreeDecoder(aac_dec); aac_dec = NULL; }
    mp3dec_init(&dec);
    rpos = 0.0; prevL = prevR = 0;
    state = ST_CONNECT;
    if (net_phase != NET_DONE) { set_error("Réseau en cours de connexion..."); return; }
    if (!net_ok) { set_error("Pas de réseau (Broadband Adapter)"); return; }
    /* v71 : reconnexion : d'abord l'adresse finale deja trouvee (sans redirections ni liste .m3u) */
    int ok = 0;
    if (is_reconnect && rad_final_idx == idx && rad_final[0]) { ok = http_open(rad_final, 0); if (!ok) { stream_close(); hls_stop(); state = ST_CONNECT; } }
    if (!ok) ok = http_open(stations[idx].url, 0);
    if (ok) {
        if (!hls_on) { snprintf(rad_final, sizeof(rad_final), "%s", http_final_url); rad_final_idx = idx; }
        err_msg[0] = 0;
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
    if (hls_on) { hls_pump(); return; }
    int got = 0;
    while (cn.fd >= 0 && !stream_closed && cbuf_free() > (int)sizeof(tmp) && got < 64 * 1024) {
        s32 r = conn_recv(&cn, tmp, sizeof(tmp));
        if (r > 0) { int m = st_dc.on ? dechunk(&st_dc, tmp, r) : r; if (m > 0) stream_feed(tmp, m); got += r; last_data_ms = now_ms(); continue; }
        if (r == -EAGAIN || r == -EWOULDBLOCK) break;
        stream_closed = 1;                                     /* fin ou erreur */
        stream_close();
    }
}

/* Appele a chaque tour de boucle : recoit, decode, gere le tampon. */
static void stream_pump(void) {
    /* v71 : la radio a deja joue puis s'est arretee (reseau, serveur) : on reessaie tout seul,
       de plus en plus espace (1, 2, 4, 8, 15 puis 30 s), au lieu de rester sur une erreur */
    if (state == ST_ERROR && src == SRC_RADIO && !pod_on && rad_played && reconnects != 99 && rt_n < 30) {
        static const u32 dl[6] = { 1000, 2000, 4000, 8000, 15000, 30000 };
        u64 n = now_ms();
        if (!rt_at) rt_at = n + dl[rt_n < 6 ? rt_n : 5];
        else if (n >= rt_at && net_ok) { rt_n++; rt_at = 0; reconnects = 0; start_station(cur_station, 1); }
        return;
    }
    if (state != ST_BUFFER && state != ST_PLAY) return;
    if (state == ST_PLAY && src == SRC_RADIO) { rad_played = 1; if (rt_n && now_ms() - connect_ms > 60000) rt_n = 0; }

    /* 1. reception (sans bloquer) */
    stream_recv();
    if (!hls_on && !stream_closed && now_ms() - last_data_ms > 10000) {   /* plus rien depuis 10 s */
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
        if (stream_closed && cbuf_avail() < MP3_MIN_BYTES && pod_on) {   /* podcast : fin, ou coupure */
            if (pod_dur <= 0 || pod_pos_sec() > pod_dur - 20 || reconnects >= 3) podcast_finished();
            else { int r = reconnects + 1; podcast_play_at(pod_pos_sec()); reconnects = r; }
        } else if (stream_closed && cbuf_avail() < MP3_MIN_BYTES) {
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
static int  tag_budget = 0;                /* v29 : etiquettes lues pour l'image en cours (1 au plus) */
static int  bib_dirty;                      // bibliotheque en cache a enregistrer (voir bib_save)

/* Nom du fichier seul (apres le dernier '/') */
static const char *base_of(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

/* Formats lus dans l'onglet MUSIQUE */
enum { FMT_MP3, FMT_FLAC, FMT_OGG, FMT_WAV, FMT_M4A, FMT_GME, FMT_XMP, FMT_GCN };
static int retro_ext_fmt(const char *path);       /* onglet RETRO (tab_retro.c) */
static u8 is_retro[MAX_FILES];
static u8 rating[MAX_FILES];            /* v76 : note (etoiles) de chaque morceau, 0 a 5 (lib_extra.c) */

/* longueur de l'extension audio (".mp3" = 4, ".flac" = 5), 0 si ce n'est pas un son */
static int audio_ext_len(const char *path) {
    static const char *const ext[] = { ".mp3", ".flac", ".ogg", ".oga", ".wav", ".m4a", ".m4b" };
    size_t l = strlen(path);
    if (retro_ext_fmt(path) >= 0) return (int)strlen(strrchr(path, '.'));
    for (int i = 0; i < 7; i++) {
        size_t e = strlen(ext[i]);
        if (l > e && strcasecmp(path + l - e, ext[i]) == 0) return (int)e;
    }
    return 0;
}

static int audio_fmt(const char *path) {
    size_t l = strlen(path);
    int r = retro_ext_fmt(path);
    if (r >= 0) return r;
    if (l > 5 && !strcasecmp(path + l - 5, ".flac")) return FMT_FLAC;
    if (l > 4 && (!strcasecmp(path + l - 4, ".ogg") || !strcasecmp(path + l - 4, ".oga"))) return FMT_OGG;
    if (l > 4 && !strcasecmp(path + l - 4, ".wav")) return FMT_WAV;
    if (l > 4 && (!strcasecmp(path + l - 4, ".m4a") || !strcasecmp(path + l - 4, ".m4b"))) return FMT_M4A;
    return FMT_MP3;
}

/* chemin sans l'extension audio */
static void strip_audio_ext(char *path) {
    int e = audio_ext_len(path);
    if (e) path[strlen(path) - e] = 0;
}

static u32 le32(const unsigned char *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static u32 le16(const unsigned char *p) { return (u32)p[0] | ((u32)p[1] << 8); }

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
        (void)l;
        if (audio_ext_len(e->d_name)) {
            strcpy(names[nfiles++], path);
        } else if (depth < SCAN_DEPTH && is_dir(path, e)) {
            scan_dir(path, depth + 1);
        }
    }
    closedir(d);
}

static void retro_unzip_tree(const char *dir, int depth);   /* tab_retro.c (v26 : .zip de musiques retro) */
static int nsorted = 0;                                    /* v27 : names[0..nsorted[ tries, ajouts recents ensuite */
static void scan_files(void) {
    nfiles = 0;
    retro_unzip_tree(SD_ROOT "/retro", 0);
    for (int i = 0; i < MAX_FILES; i++) dur_cache[i] = -2;
    scan_dir(MP3_DIR, 0);
    scan_dir(DL_DIR, 0);
    scan_dir(SD_ROOT "/retro", 0);
    scan_dir(GCD "/podcasts", 0);                      /* episodes telecharges */
    qsort(names, nfiles, NAME_LEN, cmp_names);
    nsorted = nfiles;
    for (int k = 0; k < nfiles; k++) is_retro[k] = retro_ext_fmt(names[k]) >= 0;
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

/* ---------- Duree et en-tetes des autres formats ---------- */
/* saute une etiquette ID3v2 en tete de fichier (certains FLAC en ont une) ;
   retourne la position du debut des donnees */
static long skip_id3(FILE *fp) {
    unsigned char h[10];
    if (fseek(fp, 0, SEEK_SET) != 0 || fread(h, 1, 10, fp) != 10) return -1;
    if (memcmp(h, "ID3", 3) != 0) return 0;
    return 10 + (long)synchsafe(h + 6) + ((h[5] & 0x10) ? 10 : 0);
}

/* FLAC : duree lue dans le bloc STREAMINFO */
static int flac_duration(const char *path) {
    unsigned char b[42];
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    long off = skip_id3(fp);
    int ok = off >= 0 && fseek(fp, off, SEEK_SET) == 0 && fread(b, 1, 42, fp) == 42;
    fclose(fp);
    if (!ok || memcmp(b, "fLaC", 4) != 0 || (b[4] & 0x7f) != 0) return -1;
    const unsigned char *si = b + 8;
    u32 rate = ((u32)si[10] << 12) | ((u32)si[11] << 4) | (si[12] >> 4);
    unsigned long long total = ((unsigned long long)(si[13] & 0x0F) << 32) | be32(si + 14);
    if (rate == 0 || total == 0) return -1;
    return (int)(total / rate);
}

/* OGG Vorbis : frequence dans le premier paquet, position (granule) de la derniere page */
static int ogg_duration(const char *path) {
    static unsigned char b[16384];
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    size_t n = fread(b, 1, 64, fp);
    if (n < 64 || memcmp(b, "OggS", 4) != 0) { fclose(fp); return -1; }
    int hdr = 27 + b[26];
    if (hdr + 16 > 64 || memcmp(b + hdr, "\x01vorbis", 7) != 0) { fclose(fp); return -1; }
    u32 rate = le32(b + hdr + 12);
    if (rate == 0 || fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long size = ftell(fp);
    long start = size > (long)sizeof(b) ? size - (long)sizeof(b) : 0;
    if (fseek(fp, start, SEEK_SET) != 0) { fclose(fp); return -1; }
    n = fread(b, 1, sizeof(b), fp);
    fclose(fp);
    for (long i = (long)n - 27; i >= 0; i--) {
        if (b[i] != 'O' || memcmp(b + i, "OggS", 4) != 0 || b[i + 4] != 0) continue;
        u32 lo = le32(b + i + 6), hi = le32(b + i + 10);
        if (lo == 0xFFFFFFFFu && hi == 0xFFFFFFFFu) continue;       /* page sans position */
        unsigned long long g = ((unsigned long long)hi << 32) | lo;
        return (int)(g / rate);
    }
    return -1;
}

/* WAV : format et emplacement des donnees */
typedef struct { int ch, bits, isfloat, align; u32 rate; long data, len; } wavinfo_t;

static int wav_parse(FILE *fp, long size, wavinfo_t *w) {
    unsigned char h[40];
    memset(w, 0, sizeof(*w));
    if (fseek(fp, 0, SEEK_SET) != 0 || fread(h, 1, 12, fp) != 12) return 0;
    if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return 0;
    long pos = 12;
    int tag = 0, have_fmt = 0;
    for (int guard = 0; guard < 64 && pos + 8 <= size; guard++) {
        if (fseek(fp, pos, SEEK_SET) != 0 || fread(h, 1, 8, fp) != 8) return 0;
        u32 csz = le32(h + 4);
        if (!memcmp(h, "fmt ", 4)) {
            int m = csz < sizeof(h) ? (int)csz : (int)sizeof(h);
            if (m < 16 || fread(h, 1, m, fp) != (size_t)m) return 0;
            tag = (int)le16(h);
            w->ch = (int)le16(h + 2);
            w->rate = le32(h + 4);
            w->align = (int)le16(h + 12);
            w->bits = (int)le16(h + 14);
            if (tag == 0xFFFE && m >= 26) tag = (int)le16(h + 24);   /* WAVE_FORMAT_EXTENSIBLE */
            have_fmt = 1;
        } else if (!memcmp(h, "data", 4)) {
            w->data = pos + 8;
            w->len = (long)csz;
            if (csz == 0xFFFFFFFFu || w->len > size - w->data || w->len <= 0) w->len = size - w->data;
            break;
        }
        pos += 8 + (long)csz + (csz & 1);
        if (csz > 0x7FFFFFF0u) return 0;
    }
    if (!have_fmt || w->data <= 0) return 0;
    w->isfloat = (tag == 3);
    if (!(tag == 1 || tag == 3)) return 0;
    if (w->isfloat && w->bits != 32) return 0;
    if (!w->isfloat && w->bits != 8 && w->bits != 16 && w->bits != 24 && w->bits != 32) return 0;
    if (w->ch < 1 || w->ch > 8 || w->rate < 4000 || w->rate > 384000) return 0;
    if (w->align != w->ch * w->bits / 8) return 0;
    return 1;
}

static int wav_duration(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    long size = (fseek(fp, 0, SEEK_END) == 0) ? ftell(fp) : -1;
    wavinfo_t w;
    int ok = size > 0 && wav_parse(fp, size, &w);
    fclose(fp);
    if (!ok) return -1;
    return (int)((double)w.len / w.align / w.rate);
}

static int retro_quick_duration(const char *path);
static int m4a_duration(const char *path);           /* M4A (mp4.c) */
static void m4a_close(void);
static void m4a_decode_one(void);
static int m4a_seek(int sec);
static int file_duration(const char *path) {
    switch (audio_fmt(path)) {
    case FMT_M4A:  return m4a_duration(path);
    case FMT_FLAC: return flac_duration(path);
    case FMT_OGG:  return ogg_duration(path);
    case FMT_WAV:  return wav_duration(path);
    case FMT_MP3:  return mp3_duration(path);
    default:       return retro_quick_duration(path);
    }
}

/* Duree de la piste k. Une seule lecture de carte SD par image au maximum,
   pour ne pas bloquer la boucle (et donc l'audio) en faisant defiler la liste. */
static int track_duration(int k) {
    if (k < 0 || k >= nfiles) return -1;
    if (dur_cache[k] == -2 && dur_budget > 0) {
        dur_budget--;
        dur_cache[k] = file_duration(names[k]);
        bib_dirty = 1;
    }
    return dur_cache[k];
}


// ---------- Lecture des fichiers MP3 (onglet MUSIQUE) ----------
static FILE *ffp = NULL;                     // fichier en cours (lu par blocs)
static unsigned char fbuf[FBUF_SIZE];
static int fb_start = 0, fb_end = 0, f_file_eof = 0;
static long fsize = 0, f_audio = 0;           // taille du fichier, debut du son (apres ID3)
static int playing = 0, eof = 0, cur_track = -1;
/* v29 : lire un fichier (etiquettes, duree) sans couper la musique : seulement
   quand au moins 2 s de son sont deja decodees d'avance */
static int io_safe(void) { return !(src == SRC_FILE && playing && !paused) || eof || f_count > 48000 * 2; }
static const char *track_err = NULL;   // message si un fichier ne peut pas etre lu
static char artist[64];                // artiste lu dans les etiquettes ID3 (UTF-8)
static char id3_title[128], id3_album[128], id3_year[16];

/* complete le tampon depuis la carte SD */
static void fbuf_fill(void) {
    if (!ffp || f_file_eof) return;
    if (fb_start > 0) {
        memmove(fbuf, fbuf + fb_start, fb_end - fb_start);
        fb_end -= fb_start;
        fb_start = 0;
    }
    size_t want = FBUF_SIZE - fb_end;
    if (want > 8192) want = 8192;                       /* v85 : par 8 Ko (48 Ko d'un coup : des dizaines de ms sur la carte SD) */
    size_t n = fread(fbuf + fb_end, 1, want, ffp);
    fb_end += (int)n;
    if (n == 0) f_file_eof = 1;
}

/* ---------- Decodage FLAC / OGG Vorbis / WAV ---------- */
static int file_fmt = FMT_MP3;
static drflac *flac = NULL;
static stb_vorbis *vorb = NULL;
static u32 vorb_rate = 0;
static wavinfo_t wav;
static long wav_left = 0;
static int ogg_resync = 0;                  /* position a recaler apres un saut (OGG) */

static size_t fl_read(void *u, void *buf, size_t n) { return fread(buf, 1, n, (FILE *)u); }
static drflac_bool32 fl_seek(void *u, int off, drflac_seek_origin o) {
    int wh = (o == DRFLAC_SEEK_SET) ? SEEK_SET : (o == DRFLAC_SEEK_END) ? SEEK_END : SEEK_CUR;
    return fseek((FILE *)u, off, wh) == 0;
}
static drflac_bool32 fl_tell(void *u, drflac_int64 *cur) {
    long p = ftell((FILE *)u);
    if (p < 0) return DRFLAC_FALSE;
    *cur = p;
    return DRFLAC_TRUE;
}

static int ogg_header_ok(const unsigned char *b, long n);
static void retro_close(void);
static void retro_decode(void);
static int retro_seek(int sec);
static void other_close(void) {
    retro_close();
    m4a_close();
    if (flac) { drflac_close(flac); flac = NULL; }
    if (vorb) { stb_vorbis_close(vorb); vorb = NULL; }
}

/* Ouvre le decodeur Vorbis : les en-tetes (parfois gros, avec une image) sont
   lus en entier, puis la lecture continue par blocs dans fbuf. */
static int ogg_open(FILE *fp, long start) {
    long cap = 64 * 1024, len = 0;
    unsigned char *b = NULL;
    for (;;) {
        unsigned char *nb = (unsigned char *)realloc(b, cap);
        if (!nb) { free(b); return 0; }
        b = nb;
        if (fseek(fp, start + len, SEEK_SET) != 0) { free(b); return 0; }
        len += (long)fread(b + len, 1, cap - len, fp);
        int hv = ogg_header_ok(b, len);
        if (hv == 0 || (hv < 0 && len < cap)) { free(b); return 0; }
        if (hv < 0) { if (cap >= 8 * 1024 * 1024) { free(b); return 0; } cap *= 2; continue; }
        int used = 0, err = 0;
        vorb = stb_vorbis_open_pushdata(b, (int)len, &used, &err, NULL);
        if (vorb) {
            stb_vorbis_info vi = stb_vorbis_get_info(vorb);
            vorb_rate = vi.sample_rate;
            free(b);
            if (vorb_rate == 0 || fseek(fp, start + used, SEEK_SET) != 0) { other_close(); return 0; }
            return 1;
        }
        if (err != VORBIS_need_more_data || len < cap || cap >= 8 * 1024 * 1024) { free(b); return 0; }
        cap *= 2;
        service_audio();
    }
}

static void decode_other_one(void) {
    static s16 L[2048], R[2048];
    if (file_fmt >= FMT_GME) { retro_decode(); return; }
    if (file_fmt == FMT_M4A) { m4a_decode_one(); return; }
    if (file_fmt == FMT_FLAC) {
        static s16 in[1024 * 8];
        if (!flac) { eof = 1; return; }
        int ch = flac->channels;
        if (ch < 1 || ch > 8) { eof = 1; return; }
        drflac_uint64 n = drflac_read_pcm_frames_s16(flac, 1024, in);
        if (n == 0) { eof = 1; return; }
        for (drflac_uint64 i = 0; i < n; i++) {
            L[i] = in[i * ch];
            R[i] = (ch > 1) ? in[i * ch + 1] : in[i * ch];
        }
        push_frame(L, R, (int)n, flac->sampleRate);
        return;
    }
    if (fb_end - fb_start < 16384) fbuf_fill();
    int avail = fb_end - fb_start;
    if (file_fmt == FMT_OGG) {
        if (!vorb) { eof = 1; return; }
        if (avail <= 0) { eof = 1; return; }
        int ch = 0, samples = 0;
        float **out = NULL;
        int used = stb_vorbis_decode_frame_pushdata(vorb, fbuf + fb_start, avail, &ch, &out, &samples);
        if (used == 0 && samples == 0) {
            if (f_file_eof || avail >= FBUF_SIZE) { eof = 1; return; }   /* trame illisible */
            fbuf_fill();
            if (fb_end - fb_start == avail && f_file_eof) eof = 1;
            return;
        }
        fb_start += used;
        if (samples <= 0 || ch <= 0 || !out) return;
        if (ogg_resync) {                                    /* position exacte connue */
            int off = stb_vorbis_get_sample_offset(vorb);
            if (off >= samples) { consumed = (u32)((double)(off - samples) * 48000.0 / vorb_rate); ogg_resync = 0; }
        }
        int done = 0;
        while (done < samples) {
            int m = samples - done;
            if (m > 2048) m = 2048;
            for (int i = 0; i < m; i++) {
                L[i] = clamp16(out[0][done + i] * 32767.0f);
                R[i] = clamp16(out[ch > 1 ? 1 : 0][done + i] * 32767.0f);
            }
            push_frame(L, R, m, vorb_rate);
            done += m;
        }
        return;
    }
    /* WAV */
    if (avail > wav_left) avail = (int)wav_left;
    int frames = avail / wav.align;
    if (frames <= 0) { eof = 1; return; }
    if (frames > 1024) frames = 1024;
    const unsigned char *p = fbuf + fb_start;
    int bps = wav.bits / 8;
    for (int i = 0; i < frames; i++, p += wav.align) {
        for (int c = 0; c < 2; c++) {
            const unsigned char *s = p + ((c == 1 && wav.ch > 1) ? bps : 0);
            s16 v;
            if (wav.isfloat) {
                u32 bits = le32(s);
                float f;
                memcpy(&f, &bits, 4);
                v = clamp16(f * 32767.0f);
            } else if (bps == 1) v = (s16)(((int)s[0] - 128) * 256);
            else v = (s16)(s[bps - 2] | (s[bps - 1] << 8));        /* les 16 bits de poids fort */
            if (c == 0) L[i] = v; else R[i] = v;
        }
    }
    fb_start += frames * wav.align;
    wav_left -= (long)frames * wav.align;
    push_frame(L, R, frames, wav.rate);
}

/* positionnement a 'sec' secondes (d = duree du morceau) */
static int other_seek(int sec, int d) {
    if (file_fmt >= FMT_GME) return retro_seek(sec);
    if (file_fmt == FMT_M4A) return m4a_seek(sec);
    if (file_fmt == FMT_FLAC) {
        return flac && drflac_seek_to_pcm_frame(flac, (drflac_uint64)sec * flac->sampleRate);
    }
    if (file_fmt == FMT_WAV) {
        long off = (long)((double)sec * wav.rate) * wav.align;
        if (off >= wav.len) return 0;
        if (fseek(ffp, wav.data + off, SEEK_SET) != 0) return 0;
        wav_left = wav.len - off;
        fb_start = fb_end = 0;
        f_file_eof = 0;
        return 1;
    }
    if (file_fmt == FMT_OGG && vorb && d > 0) {               /* position approchee */
        long off = f_audio + (long)((double)(fsize - f_audio) * sec / d);
        if (fseek(ffp, off, SEEK_SET) != 0) return 0;
        fb_start = fb_end = 0;
        f_file_eof = 0;
        stb_vorbis_flush_pushdata(vorb);
        ogg_resync = 1;
        return 1;
    }
    return 0;
}

/* v72 : MP3 sans blanc : l'en-tete LAME (trame Xing/Info) donne le silence ajoute par l'encodeur
   au debut (delai) et a la fin (remplissage) : on les retire, et la trame Xing (muette) est sautee */
static long mp3_skip = 0, mp3_left = -1;
static void decode_file_one(void) {
    static s16 L[1152], R[1152];
    if (file_fmt != FMT_MP3) { decode_other_one(); return; }
    if (fb_end - fb_start < 16384) fbuf_fill();
    int avail = fb_end - fb_start;
    if (avail <= 0) { eof = 1; return; }

    mp3dec_frame_info_t info;
    int n = mp3dec_decode_frame(&dec, fbuf + fb_start, avail, pcm, &info);
    if (info.frame_bytes <= 0) {
        /* rien de lisible ici : on saute ces donnees (en gardant la fin), ou c'est fini */
        if (f_file_eof || avail <= 2048) { eof = 1; return; }
        fb_start += avail - 2048;
        return;
    }
    fb_start += info.frame_bytes;
    if (n <= 0) return;                                // etiquette ID3 ou donnees ignorees
    if (n > 1152) n = 1152;
    if (info.hz <= 0) return;

    int s0 = 0;
    if (mp3_skip > 0) { s0 = mp3_skip < n ? (int)mp3_skip : n; mp3_skip -= s0; }   /* v72 : silence de l'encodeur */
    int cnt = n - s0;
    if (mp3_left >= 0) { if (cnt > mp3_left) cnt = (int)mp3_left; mp3_left -= cnt; if (mp3_left == 0) eof = 1; }
    if (cnt <= 0) return;
    for (int i = 0; i < cnt; i++) {
        int j = s0 + i;
        if (info.channels == 2) {
            L[i] = pcm[2 * j];
            R[i] = pcm[2 * j + 1];
        } else {
            L[i] = R[i] = pcm[j];
        }
    }
    push_frame(L, R, cnt, (unsigned)info.hz);
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
/* texte d'une trame ID3 (encodage Latin-1, UTF-16 ou UTF-8) -> UTF-8 */
static void id3_text(const unsigned char *t, long sz, char *out, int cap) {
    int len = 0, enc = t[0];
    long i = 1;
    if (enc == 1 || enc == 2) {                                 // UTF-16
        int le = 0;
        if (sz >= 3 && t[1] == 0xFF && t[2] == 0xFE) { le = 1; i = 3; }
        else if (sz >= 3 && t[1] == 0xFE && t[2] == 0xFF) { i = 3; }
        for (; i + 1 < sz && len < cap - 4; i += 2) {
            unsigned cu = le ? (unsigned)(t[i] | (t[i + 1] << 8)) : (unsigned)((t[i] << 8) | t[i + 1]);
            if (cu == 0) break;
            utf8_put(out, cap, &len, cu);
        }
    } else if (enc == 3) {                                      // UTF-8
        for (; i < sz && t[i] && len < cap - 1; i++) out[len++] = (char)t[i];
    } else {                                                    // Latin-1
        for (; i < sz && t[i] && len < cap - 4; i++) utf8_put(out, cap, &len, t[i]);
    }
    while (len > 0 && out[len - 1] == ' ') len--;
    out[len] = 0;
}

static void id3v1_field(const unsigned char *a, int n, char *out, int cap) {
    int len = 0;
    for (int i = 0; i < n && a[i] && len < cap - 4; i++) utf8_put(out, cap, &len, a[i]);
    while (len > 0 && out[len - 1] == ' ') len--;
    out[len] = 0;
}

/* Etiquettes du MP3 : artiste, titre, album, annee (ID3v2 au debut du fichier,
   sinon ID3v1 dans les 128 derniers octets). */
typedef struct {
    char artist[64], title[128], album[128], year[16];
    float rg_track, rg_album, pk_track, pk_album;      /* volume egalise (ReplayGain) */
    int has_lame_rg; float lame_rg, lame_pk;           /* v72 : en-tete LAME du MP3 */
    u8 has_rgt, has_rga;
} tags_t;
/* ---------- Etiquettes FLAC / OGG (commentaires Vorbis) et volume egalise ---------- */
static int key_is(const char *k, int kl, const char *name) {
    return (int)strlen(name) == kl && strncasecmp(k, name, kl) == 0;
}

/* "REPLAYGAIN_TRACK_GAIN" etc. ; valeur du type "-6.54 dB" ou "0.988" */
static void rg_tag(const char *k, int kl, const char *v, tags_t *tg) {
    float x = (float)strtod(v, NULL);
    if (key_is(k, kl, "REPLAYGAIN_TRACK_GAIN")) { tg->rg_track = x; tg->has_rgt = 1; }
    else if (key_is(k, kl, "REPLAYGAIN_ALBUM_GAIN")) { tg->rg_album = x; tg->has_rga = 1; }
    else if (key_is(k, kl, "REPLAYGAIN_TRACK_PEAK")) tg->pk_track = x;
    else if (key_is(k, kl, "REPLAYGAIN_ALBUM_PEAK")) tg->pk_album = x;
}

/* une ligne "CLE=valeur" (UTF-8) */
static void vc_line(const char *c, int len, tags_t *tg) {
    const char *eq = (const char *)memchr(c, '=', len);
    if (!eq) return;
    int kl = (int)(eq - c), vl = len - kl - 1;
    char v[160];
    if (vl > (int)sizeof(v) - 1) vl = (int)sizeof(v) - 1;
    memcpy(v, eq + 1, vl);
    v[vl] = 0;
    if (key_is(c, kl, "ARTIST")) { if (!tg->artist[0]) snprintf(tg->artist, sizeof(tg->artist), "%s", v); }
    else if (key_is(c, kl, "ALBUMARTIST")) { if (!tg->artist[0]) snprintf(tg->artist, sizeof(tg->artist), "%s", v); }
    else if (key_is(c, kl, "TITLE")) { if (!tg->title[0]) snprintf(tg->title, sizeof(tg->title), "%s", v); }
    else if (key_is(c, kl, "ALBUM")) { if (!tg->album[0]) snprintf(tg->album, sizeof(tg->album), "%s", v); }
    else if (key_is(c, kl, "DATE") || key_is(c, kl, "YEAR")) { if (!tg->year[0]) snprintf(tg->year, sizeof(tg->year), "%.4s", v); }
    else if (kl > 11 && strncasecmp(c, "REPLAYGAIN_", 11) == 0) rg_tag(c, kl, v, tg);
}

/* bloc de commentaires Vorbis : vendeur, nombre, puis "CLE=valeur" (longueurs en petit-boutiste) */
static void vorbis_comments(const unsigned char *p, long n, tags_t *tg) {
    if (n < 8) return;
    u32 vl = le32(p);
    if (vl > (u32)(n - 8)) return;
    long pos = 4 + (long)vl;
    u32 cnt = le32(p + pos);
    pos += 4;
    for (u32 i = 0; i < cnt && pos + 4 <= n; i++) {
        u32 len = le32(p + pos);
        pos += 4;
        if (len > (u32)(n - pos)) break;
        vc_line((const char *)p + pos, (int)len, tg);
        pos += (long)len;
    }
}

static void tags_clear(tags_t *tg) {
    memset(tg, 0, sizeof(*tg));
}

/* Image d'un bloc PICTURE (FLAC) : type, type MIME, description, dimensions, donnees */
static int flac_picture(const unsigned char *b, long n, const unsigned char **img, long *ilen, int *ptype) {
    if (n < 32) return 0;
    *ptype = (int)be32(b);
    long pos = 4;
    long ml = (long)be32(b + pos); pos += 4;
    if (ml < 0 || ml > n - pos - 4) return 0;
    pos += ml;
    long dl = (long)be32(b + pos); pos += 4;
    if (dl < 0 || dl > n - pos - 20) return 0;
    pos += dl + 16;
    long len = (long)be32(b + pos); pos += 4;
    if (len <= 0 || len > n - pos) return 0;
    *img = b + pos;
    *ilen = (long)len;
    return 1;
}

/* Parcourt les blocs de metadonnees d'un FLAC : etiquettes, et pochette si want_pic. */
static int flac_meta(FILE *fp, tags_t *tg, int want_pic) {
    unsigned char h[4];
    long off = skip_id3(fp);
    if (off < 0 || fseek(fp, off, SEEK_SET) != 0 || fread(h, 1, 4, fp) != 4 || memcmp(h, "fLaC", 4) != 0) return 0;
    int got = 0, best_front = 0;
    unsigned char *best = NULL;
    for (int guard = 0; guard < 128; guard++) {
        if (fread(h, 1, 4, fp) != 4) break;
        int last = h[0] & 0x80, type = h[0] & 0x7f;
        u32 len = ((u32)h[1] << 16) | ((u32)h[2] << 8) | h[3];
        int used = 0;
        if (type == 4 && tg && len < 1024 * 1024) {
            unsigned char *b = (unsigned char *)malloc(len ? len : 1);
            if (b && fread(b, 1, len, fp) == len) { vorbis_comments(b, (long)len, tg); used = 1; }
            free(b);
            if (!used) break;
        } else if (type == 6 && want_pic && !best_front && len < COVER_MAX_FILE + 65536) {
            unsigned char *b = (unsigned char *)malloc(len ? len : 1);
            if (b && fread(b, 1, len, fp) == len) {
                const unsigned char *img; long il; int pt;
                if (flac_picture(b, (long)len, &img, &il, &pt) && (!best || pt == 3)) {
                    free(best);
                    best = (unsigned char *)malloc(il);
                    if (best) { memcpy(best, img, il); got = (int)il; best_front = (pt == 3); }
                }
                used = 1;
            }
            free(b);
            if (!used) break;
        }
        if (!used && fseek(fp, (long)len, SEEK_CUR) != 0) break;
        if (last) break;
    }
    int ok = 0;
    if (best) { ok = cover_from_memory(best, got); free(best); }
    return ok;
}

static int cover_from_flac(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    int ok = flac_meta(fp, NULL, 1);
    fclose(fp);
    return ok;
}

/* OGG : paquet numero k du premier flux logique (les paquets peuvent couvrir plusieurs pages) */
static long ogg_packet(const unsigned char *b, long n, int k, unsigned char **out) {
    long pos = 0, plen = 0, cap = 0;
    int idx = 0;
    unsigned char *pk = NULL;
    while (pos + 27 <= n && memcmp(b + pos, "OggS", 4) == 0) {
        int nseg = b[pos + 26];
        long data = pos + 27 + nseg;
        if (data > n) break;
        for (int s = 0; s < nseg; s++) {
            int sl = b[pos + 27 + s];
            if (data + sl > n) { free(pk); return -1; }
            if (idx == k) {
                if (plen + sl > cap) {
                    long nc = cap ? cap * 2 : 4096;
                    while (nc < plen + sl) nc *= 2;
                    unsigned char *np = (unsigned char *)realloc(pk, nc);
                    if (!np) { free(pk); return -1; }
                    pk = np;
                    cap = nc;
                }
                if (sl) memcpy(pk + plen, b + data, sl);
                plen += sl;
            }
            data += sl;
            if (sl < 255) {
                if (idx == k) { *out = pk; return plen; }
                idx++;
            }
        }
        pos = data;
    }
    free(pk);
    return -1;
}

/* En-tetes Vorbis coherents ? (stb_vorbis supporte mal des longueurs absurdes)
   1 = oui, 0 = non, -1 = il faut plus de donnees */
static int ogg_header_ok(const unsigned char *b, long n) {
    unsigned char *pk = NULL;
    long l = ogg_packet(b, n, 0, &pk);
    if (l < 0) return n < 65536 ? -1 : 0;
    int ok = (l >= 30 && memcmp(pk, "\x01vorbis", 7) == 0);
    free(pk);
    if (!ok) return 0;
    pk = NULL;
    l = ogg_packet(b, n, 1, &pk);
    if (l < 0) return -1;
    ok = 0;
    if (l >= 7 + 8 && memcmp(pk, "\x03vorbis", 7) == 0) {
        const unsigned char *p = pk + 7;
        long m = l - 7, pos = 0;
        u32 vl = le32(p);
        if (vl <= (u32)(m - 8)) {
            pos = 4 + (long)vl;
            u32 cnt = le32(p + pos);
            pos += 4;
            ok = cnt <= (u32)((m - pos) / 4);
            for (u32 i = 0; ok && i < cnt; i++) {
                if (pos + 4 > m) { ok = 0; break; }
                u32 cl = le32(p + pos);
                pos += 4;
                if (cl > (u32)(m - pos)) ok = 0;
                else pos += (long)cl;
            }
        }
    }
    free(pk);
    return ok;
}

/* etiquettes d'un OGG Vorbis a partir du debut du fichier */
static void ogg_tags(const unsigned char *b, long n, tags_t *tg) {
    unsigned char *pk = NULL;
    long l = ogg_packet(b, n, 1, &pk);
    if (l > 7 && memcmp(pk, "\x03vorbis", 7) == 0) vorbis_comments(pk + 7, l - 7, tg);
    free(pk);
}

/* ---------- Radio OGG Vorbis (Icecast) ----------
   Les radios OGG enchainent les morceaux en recommencant un flux complet
   (nouvelle page "debut de flux" avec de nouveaux en-tetes) : on rouvre alors
   le decodeur, et le titre est lu dans les commentaires Vorbis. */
static void set_error(const char *msg);
static stb_vorbis *svorb = NULL;
static u32 svorb_rate = 0;

static void ogg_stream_reset(void) {
    if (svorb) { stb_vorbis_close(svorb); svorb = NULL; }
    ogg_bos_at = -1;
    ogg_scan = 0;
}

/* page Ogg qui commence un flux (drapeau "BOS") ? */
static int ogg_is_bos(const unsigned char *p, int n) {
    return n >= 27 && p[0] == 'O' && p[1] == 'g' && p[2] == 'g' && p[3] == 'S' && p[4] == 0 && (p[5] & 0x02);
}

/* cherche la prochaine page de debut de flux apres la position courante */
static void ogg_find_bos(void) {
    if (ogg_bos_at >= 0) return;
    if (ogg_scan < cb_start + 1) ogg_scan = cb_start + 1;
    for (; ogg_scan + 27 <= cb_end; ogg_scan++)
        if (ogg_is_bos(cbuf + ogg_scan, cb_end - ogg_scan)) { ogg_bos_at = ogg_scan; return; }
}

static void ogg_stream_title(void) {
    stb_vorbis_comment c = stb_vorbis_get_comment(svorb);
    tags_t tg;
    tags_clear(&tg);
    for (int i = 0; i < c.comment_list_length; i++)
        if (c.comment_list[i]) vc_line(c.comment_list[i], (int)strlen(c.comment_list[i]), &tg);
    if (tg.artist[0] && tg.title[0]) snprintf(now_title, sizeof(now_title), "%s - %s", tg.artist, tg.title);
    else if (tg.title[0]) snprintf(now_title, sizeof(now_title), "%s", tg.title);
}

static int decode_ogg_one(int closed) {
    static s16 L[2048], R[2048];
    int avail = cbuf_avail();
    if (avail <= 0) return 0;
    if (!svorb) {
        /* se placer sur un debut de flux, puis lire les trois en-tetes */
        if (!ogg_is_bos(cbuf + cb_start, avail)) {
            ogg_bos_at = -1;
            ogg_find_bos();
            if (ogg_bos_at < 0) {
                int drop = closed ? avail : avail - 27;
                if (drop > 0) cb_start += drop;
                return 0;
            }
            cb_start = ogg_bos_at;
            ogg_bos_at = -1;
            return 1;
        }
        if (avail >= 36 && memcmp(cbuf + cb_start + 28, "OpusHead", 8) == 0) {
            set_error("Radio Opus : format non g\xC3\xA9r\xC3\xA9");
            cb_start = cb_end;
            return 0;
        }
        int hv = ogg_header_ok(cbuf + cb_start, avail);
        if (hv < 0 && !closed && avail < CBUF_SIZE / 2) return 0;
        if (hv <= 0) { cb_start += 1; return 1; }            /* en-tetes abimes : flux suivant */
        int used = 0, err = 0;
        svorb = stb_vorbis_open_pushdata(cbuf + cb_start, avail, &used, &err, NULL);
        if (!svorb) {
            if (err == VORBIS_need_more_data && !closed && avail < CBUF_SIZE / 2) return 0;
            cb_start += 1;                                   /* en-tetes illisibles : flux suivant */
            return 1;
        }
        stb_vorbis_info vi = stb_vorbis_get_info(svorb);
        svorb_rate = vi.sample_rate;
        stream_hz = (int)vi.sample_rate;
        stream_ch = vi.channels;
        free(ogg_hdr);
        ogg_hdr = (unsigned char *)malloc(used);
        if (ogg_hdr) { memcpy(ogg_hdr, cbuf + cb_start, used); ogg_hdr_len = used; } else ogg_hdr_len = 0;
        cb_start += used;
        ogg_scan = cb_start + 1;
        ogg_bos_at = -1;
        ogg_stream_title();
        if (svorb_rate == 0) { ogg_stream_reset(); return 0; }
        return 1;
    }
    ogg_find_bos();
    int lim = (ogg_bos_at >= 0) ? ogg_bos_at - cb_start : avail;
    if (lim <= 0) { ogg_stream_reset(); return 1; }          /* morceau suivant : nouveaux en-tetes */
    int ch = 0, samples = 0;
    float **out = NULL;
    int used = stb_vorbis_decode_frame_pushdata(svorb, cbuf + cb_start, lim, &ch, &out, &samples);
    if (used == 0 && samples == 0) {
        if (ogg_bos_at >= 0 || closed) { cb_start += lim; ogg_stream_reset(); return 1; }
        if (lim >= CBUF_SIZE / 2) { cb_start += lim; ogg_stream_reset(); return 1; }   /* illisible */
        return 0;                                            /* attendre la suite */
    }
    cb_start += used;
    if (samples <= 0 || ch <= 0 || !out) return 1;
    int done = 0;
    while (done < samples) {
        int m = samples - done;
        if (m > 2048) m = 2048;
        for (int i = 0; i < m; i++) {
            L[i] = clamp16(out[0][done + i] * 32767.0f);
            R[i] = clamp16(out[ch > 1 ? 1 : 0][done + i] * 32767.0f);
        }
        push_frame(L, R, m, svorb_rate);
        done += m;
    }
    return 1;
}

/* ReplayGain dans une trame ID3v2 TXXX (description puis valeur) */
static void id3_txxx(const unsigned char *t, long sz, tags_t *tg) {
    char d[64], v[32];
    int enc = t[0], dl = 0, vl = 0;
    long i = 1;
    int wide = (enc == 1 || enc == 2);
    if (wide && i + 1 < sz && ((t[i] == 0xFF && t[i + 1] == 0xFE) || (t[i] == 0xFE && t[i + 1] == 0xFF))) i += 2;
    int le = wide && t[1] == 0xFF;
    /* description */
    while (i < sz) {
        unsigned c;
        if (wide) { if (i + 1 >= sz) break; c = le ? t[i] : t[i + 1]; unsigned c2 = le ? t[i + 1] : t[i]; i += 2; if (!c && !c2) break; if (c2) c = '?'; }
        else { c = t[i++]; if (!c) break; }
        if (dl < (int)sizeof(d) - 1) d[dl++] = (char)c;
    }
    d[dl] = 0;
    if (wide && i + 1 < sz && ((t[i] == 0xFF && t[i + 1] == 0xFE) || (t[i] == 0xFE && t[i + 1] == 0xFF))) { le = (t[i] == 0xFF); i += 2; }
    while (i < sz) {
        unsigned c;
        if (wide) { if (i + 1 >= sz) break; c = le ? t[i] : t[i + 1]; unsigned c2 = le ? t[i + 1] : t[i]; i += 2; if (!c && !c2) break; if (c2) c = '?'; }
        else { c = t[i++]; if (!c) break; }
        if (vl < (int)sizeof(v) - 1) v[vl++] = (char)c;
    }
    v[vl] = 0;
    if (dl > 11 && strncasecmp(d, "REPLAYGAIN_", 11) == 0) rg_tag(d, dl, v, tg);
}

static void parse_tags(const unsigned char *fdata, long fsize, const unsigned char *v1tag, tags_t *tg) {
    tags_clear(tg);

    if (fdata && fsize > 10 && memcmp(fdata, "ID3", 3) == 0 && fdata[3] >= 2 && fdata[3] <= 4) {
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
            const unsigned char *t = f + hdr;
            if (ver == 2) {
                if (!memcmp(f, "TP1", 3) && !tg->artist[0]) id3_text(t, sz, tg->artist, sizeof(tg->artist));
                else if (!memcmp(f, "TT2", 3)) id3_text(t, sz, tg->title, sizeof(tg->title));
                else if (!memcmp(f, "TAL", 3)) id3_text(t, sz, tg->album, sizeof(tg->album));
                else if (!memcmp(f, "TYE", 3)) id3_text(t, sz, tg->year, sizeof(tg->year));
            } else {
                if (!memcmp(f, "TPE1", 4) && !tg->artist[0]) id3_text(t, sz, tg->artist, sizeof(tg->artist));
                else if (!memcmp(f, "TIT2", 4)) id3_text(t, sz, tg->title, sizeof(tg->title));
                else if (!memcmp(f, "TALB", 4)) id3_text(t, sz, tg->album, sizeof(tg->album));
                else if (!memcmp(f, "TYER", 4) || !memcmp(f, "TDRC", 4)) id3_text(t, sz, tg->year, sizeof(tg->year));
                else if (!memcmp(f, "TXXX", 4)) id3_txxx(t, sz, tg);
            }
            pos += hdr + sz;
        }
    }
    if (v1tag && memcmp(v1tag, "TAG", 3) == 0) {                    // ID3v1
        const unsigned char *v1 = v1tag;
        if (!tg->title[0]) id3v1_field(v1 + 3, 30, tg->title, sizeof(tg->title));
        if (!tg->artist[0]) id3v1_field(v1 + 33, 30, tg->artist, sizeof(tg->artist));
        if (!tg->album[0]) id3v1_field(v1 + 63, 30, tg->album, sizeof(tg->album));
        if (!tg->year[0]) id3v1_field(v1 + 93, 4, tg->year, sizeof(tg->year));
    }
    if (strlen(tg->year) > 4) tg->year[4] = 0;                 /* "2003-05-01" -> "2003" */
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

#include "mp4.c"                             /* M4A / MP4 (son AAC) */

static void load_cover(int idx) {
    static const char *exts[] = { ".jpg", ".jpeg", ".png" };
    static const char *common[] = { "cover.jpg", "folder.jpg", "cover.png", "folder.png" };
    char noext[NAME_LEN], dir[NAME_LEN], path[NAME_LEN + 16];

    /* les images sont cherchees dans le dossier du MP3 (un dossier par album) */
    cover_ok = 0;
    cv_gen++;
    free(cv_pend);
    cv_pend = NULL;
    {                                                     /* deja prete dans le cache ? */
        struct stat st;
        long sz = stat(names[idx], &st) == 0 ? (long)st.st_size : 0;
        snprintf(cv_key, sizeof(cv_key), "%s|%ld", names[idx], sz);
        if (cv_cache_read(cv_key)) { cover_ok = 1; return; }
    }
    cv_defer = 1;
    snprintf(noext, sizeof(noext), "%s", names[idx]);
    strip_audio_ext(noext);
    snprintf(dir, sizeof(dir), "%s", names[idx]);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;

    for (int i = 0; i < 3 && !cover_ok; i++) {
        snprintf(path, sizeof(path), "%s%s", noext, exts[i]);
        cover_ok = cover_from_file(path);
    }
    if (!cover_ok) {
        int fm = audio_fmt(names[idx]);
        if (fm == FMT_MP3) cover_ok = cover_from_id3(names[idx]);
        else if (fm == FMT_FLAC) cover_ok = cover_from_flac(names[idx]);
        else if (fm == FMT_M4A) cover_ok = m4a_meta(names[idx], NULL, 1);
    }
    for (int i = 0; i < 4 && !cover_ok; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, common[i]);
        cover_ok = cover_from_file(path);
    }
    cv_defer = 0;
    if (cover_ok == 2) cover_ok = 0;                      /* (l'image arrive dans un instant) */
}

static int retro_open(const char *path, int fmt, tags_t *tg, const char **err);
static int retro_len_ms;
static int rg_mode = 0;                     /* volume egalise : 0 non, 1 par morceau, 2 par album */
static float rg_db = 0.0f;                  /* gain applique au morceau en cours (affichage) */
static const char *rg_cur_path = NULL;      /* v72 : fichier en cours (gain mesure) */
static int rg_has = 0;

static void mp3_lame_info(FILE *fp, long *audio, long size, tags_t *tg) {
    static unsigned char b[1024];
    mp3_skip = 0; mp3_left = -1;
    if (fseek(fp, *audio, SEEK_SET) != 0) return;
    long n = (long)fread(b, 1, sizeof(b), fp);
    long o = 0;
    while (o + 4 < n && !(b[o] == 0xFF && (b[o + 1] & 0xE0) == 0xE0)) o++;     /* (premiere trame) */
    if (o + 200 > n) return;
    const unsigned char *h = b + o;
    int ver = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3, bri = h[2] >> 4, sri = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1, mono = (h[3] >> 6) == 3;
    if (layer != 1 || bri == 0 || bri == 15 || sri == 3 || ver == 1) return;
    static const int br1[15] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
    static const int br2[15] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 };
    static const int sr1[3] = { 44100, 48000, 32000 };
    int v1 = ver == 3, sr = sr1[sri] >> (v1 ? 0 : ver == 2 ? 1 : 2), spf = v1 ? 1152 : 576;
    int flen = (v1 ? 144 : 72) * (v1 ? br1[bri] : br2[bri]) * 1000 / sr + pad;
    int xo = 4 + (v1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
    if (memcmp(h + xo, "Xing", 4) && memcmp(h + xo, "Info", 4)) return;
    u32 fl = be32(h + xo + 4), frames = 0;
    int p = xo + 8;
    if (fl & 1) { frames = be32(h + p); p += 4; }
    if (fl & 2) p += 4;
    if (fl & 4) p += 100;
    if (fl & 8) p += 4;
    *audio += o + flen;                                    /* (la trame Xing est muette : sautee) */
    if (p + 24 > n - o || (memcmp(h + p, "LAME", 4) && memcmp(h + p, "Lavc", 4) && memcmp(h + p, "Lavf", 4))) return;
    const unsigned char *lt = h + p;
    int delay = (lt[21] << 4) | (lt[22] >> 4), padding = ((lt[22] & 15) << 8) | lt[23];
    if (delay > 3000 || padding > 3000) return;
    mp3_skip = delay + 529;                                /* (+ le delai du decodeur) */
    if (frames > 0) { long tot = (long)frames * spf - delay - padding; if (tot > 0) mp3_left = tot; }
    u32 pkraw = be32(lt + 11);                             /* (gain "radio" calcule par LAME) */
    int rgw = (lt[15] << 8) | lt[16];
    if (((rgw >> 13) & 7) == 1 && (rgw & 0x1FF)) {
        tg->has_lame_rg = 1;
        tg->lame_rg = ((rgw >> 9) & 1 ? -1.0f : 1.0f) * (rgw & 0x1FF) / 10.0f + 1.0f;   /* (LAME vise 89 dB : +1 dB vers ReplayGain 2) */
        tg->lame_pk = pkraw ? (float)pkraw / (float)(1 << 23) : 0.0f;
    }
    (void)size;
}
#define VOL_FILE GCD "/volumes.txt"
static int rg_lookup(const char *path, float *g, float *pk) {     /* gain deja mesure pour ce fichier */
    FILE *f = sd_ok ? fopen(VOL_FILE, "r") : NULL;
    if (!f) return 0;
    static char l[NAME_LEN + 48];
    int found = 0;
    while (fgets(l, sizeof(l), f)) {
        char *b1 = strchr(l, '|'), *b2 = b1 ? strchr(b1 + 1, '|') : NULL;
        if (!b2) continue;
        char *p = trim(b2 + 1);
        if (!strcmp(p, path)) { *g = (float)atof(l); *pk = (float)atof(b1 + 1); found = 1; }
    }
    fclose(f);
    return found;
}
static void rg_store(const char *path, float g, float pk) {
    if (!sd_ok) return;
    FILE *f = fopen(VOL_FILE, "a");
    if (f) { fprintf(f, "%.2f|%.4f|%s\n", g, pk, path); fclose(f); }
}
static float rgm_result(float *pk) {                       /* sonie integree -> gain (dB) vers -18 LUFS */
    *pk = rgm_peak;
    double sum = 0; int n = 0;
    for (int i = 0; i < rgm_nb; i++) if (rgm_blk[i] > 1.1724e-7f) { sum += rgm_blk[i]; n++; }   /* (porte absolue : -70 LUFS) */
    if (n < 5) return 99;
    double rel = sum / n * 0.1;                             /* (porte relative : -10 LU) */
    sum = 0; n = 0;
    for (int i = 0; i < rgm_nb; i++) if (rgm_blk[i] > rel) { sum += rgm_blk[i]; n++; }
    if (n < 3) return 99;
    float lufs = -0.691f + 10.0f * log10f((float)(sum / n));
    return -18.0f - lufs;
}
static void rgm_start(int k) {                             /* mesurer ce morceau (s'il n'a pas de gain connu) */
    rgm_on = 1; rgm_nb = 0; rgm_n = 0; rgm_acc = 0; rgm_peak = 0; rgm_track = k;
    memset(rgm_s, 0, sizeof(rgm_s));
}
static void rgm_finish(int complete) {                      /* fin du decodage du morceau mesure */
    if (!rgm_on) return;
    rgm_on = 0;
    if (!complete || rgm_track < 0 || rgm_track >= nfiles) return;
    float pk, g = rgm_result(&pk);
    if (g < 90) rg_store(names[rgm_track], g, pk);
}
static int rg_auto = 0;                                    /* gain du morceau en cours : mesure (pas d'etiquette) */
static void rg_set(const tags_t *tg) {
    rg_gain = 1.0f;
    rg_db = 0.0f;
    rg_has = 0;
    rg_auto = 0;
    if (!rg_mode) return;
    float g = 0.0f, pk = 0.0f;
    if (rg_mode == 2 && tg->has_rga) { g = tg->rg_album; pk = tg->pk_album; rg_has = 1; }
    else if (tg->has_rgt) { g = tg->rg_track; pk = tg->pk_track; rg_has = 1; }
    else if (tg->has_rga) { g = tg->rg_album; pk = tg->pk_album; rg_has = 1; }
    if (!rg_has && tg->has_lame_rg) { g = tg->lame_rg; pk = tg->lame_pk; rg_has = 1; }   /* (gain calcule par l'encodeur LAME) */
    if (!rg_has && rg_cur_path && rg_lookup(rg_cur_path, &g, &pk)) { rg_has = 1; rg_auto = 1; }
    if (!rg_has) return;
    if (g > 15.0f) g = 15.0f;
    if (g < -25.0f) g = -25.0f;
    float lin = powf(10.0f, g / 20.0f);
    if (pk > 0.01f && lin * pk > 1.0f) lin = 1.0f / pk;         /* pas de saturation */
    rg_gain = lin;
    rg_db = 20.0f * log10f(lin);
}

static void stop_track(void) {
    if (video_playing) video_stop();
    if (ffp) { fclose(ffp); ffp = NULL; }
    other_close();
    playing = 0;
    paused = 0;
    if (xf_request != 0) return;             /* fondu enchaine (> 0) ou sans blanc (< 0) : la fin du morceau continue */
    if (src == SRC_FILE) src = SRC_NONE;
    f_head = f_tail = f_count = 0;
    xf_left = 0;
    consumed_skip = 0;
}

/* v85 : etiquettes ID3v2 sans lire la pochette integree : les cadres sont lus un par un dans le
   fichier, les gros (APIC : image, souvent des centaines de Ko, 0,3 s de lecture sur la carte SD
   de la GameCube) sont sautes. Rend un en-tete ID3 recompose (pour parse_tags), ou NULL :
   on lit alors tout, comme avant (desynchronisation de tout l'en-tete, format inattendu). */
static unsigned char *id3_text_only(FILE *fp, const unsigned char *head, long tagend, long *outlen) {
    int ver = head[3];
    if (ver < 2 || ver > 4 || (head[5] & 0x80)) return NULL;          /* (desynchronisation : lecture complete) */
    int hdr = ver == 2 ? 6 : 10;
    long pos = 10;
    if (ver >= 3 && (head[5] & 0x40)) {                              /* en-tete etendu : saute */
        unsigned char e[4];
        if (fseek(fp, 10, SEEK_SET) != 0 || fread(e, 1, 4, fp) != 4) return NULL;
        pos += ver == 4 ? (long)synchsafe(e) : (long)be32(e) + 4;
    }
    long cap = 16384, n = 10;
    unsigned char *b = (unsigned char *)malloc(cap);
    if (!b) return NULL;
    memcpy(b, head, 10);
    b[5] &= (unsigned char)~0x40;                                     /* (plus d'en-tete etendu) */
    for (int guard = 0; guard < 256 && pos + hdr <= tagend; guard++) {
        unsigned char f[10];
        if (fseek(fp, pos, SEEK_SET) != 0 || fread(f, 1, hdr, fp) != (size_t)hdr || f[0] == 0) break;
        long sz = ver == 2 ? (long)(((u32)f[3] << 16) | ((u32)f[4] << 8) | f[5]) : (ver == 4 ? (long)synchsafe(f + 4) : (long)be32(f + 4));
        if (sz <= 0 || pos + hdr + sz > tagend) break;
        int text = f[0] == 'T' && sz <= 4096;                         /* (titre, artiste, album, annee, TXXX : petits) */
        if (text) {
            if (n + hdr + sz > cap) {
                long nc = (n + hdr + sz) * 2;
                unsigned char *nb = (unsigned char *)realloc(b, nc);
                if (!nb) break;
                b = nb; cap = nc;
            }
            memcpy(b + n, f, hdr);
            if (fread(b + n + hdr, 1, sz, fp) != (size_t)sz) break;
            n += hdr + sz;
        }
        pos += hdr + sz;
    }
    long body = n - 10;                                               /* taille de l'en-tete recompose (synchsafe) */
    b[6] = (unsigned char)((body >> 21) & 0x7F); b[7] = (unsigned char)((body >> 14) & 0x7F);
    b[8] = (unsigned char)((body >> 7) & 0x7F);  b[9] = (unsigned char)(body & 0x7F);
    *outlen = n;
    return b;
}
static int start_track(int idx) {
    if (idx < 0 || idx >= nfiles) { xf_request = 0; return 0; }
    mp3_skip = 0; mp3_left = -1; rgm_on = 0;
    crumb("morceau : %s", names[idx]);
    podcast_leave();
    int xf = xf_request;
    stop_track();
    if (!xf) stop_station();
    track_err = NULL;
    artist[0] = 0;
    muted = 0;
    load_cover(idx);          /* avant le son : la memoire du decodage est deja liberee */

    FILE *fp = fopen(names[idx], "rb");
    long size = -1;
    if (fp && fseek(fp, 0, SEEK_END) == 0) size = ftell(fp);
    if (!fp || size <= 0) {
        if (fp) fclose(fp);
        track_err = fp ? "Fichier vide" : "Ouverture impossible";
        xf_request = 0;
        if (xf) { src = SRC_NONE; f_head = f_tail = f_count = 0; xf_left = 0; consumed_skip = 0; }
        return 0;
    }
    int fmt = audio_fmt(names[idx]);
    tags_t tg;
    tags_clear(&tg);
    long audio = 0;
    const char *err = NULL;

    if (fmt == FMT_MP3) {
        /* etiquettes : en-tete ID3v2 (lu a part) et ID3v1 (128 derniers octets) */
        unsigned char head[10], v1[128];
        int has_v1 = 0;
        if (size >= 128 && fseek(fp, size - 128, SEEK_SET) == 0 && fread(v1, 1, 128, fp) == 128) has_v1 = 1;
        unsigned char *tag = NULL;
        long taglen = 0;
        if (fseek(fp, 0, SEEK_SET) == 0 && fread(head, 1, 10, fp) == 10 && memcmp(head, "ID3", 3) == 0) {
            audio = 10 + (long)synchsafe(head + 6) + ((head[5] & 0x10) ? 10 : 0);
            taglen = audio < size ? audio : size;
            if (taglen > 65536) tag = id3_text_only(fp, head, taglen, &taglen);   /* v85 : gros en-tete : sans la pochette integree */
            if (!tag) {                                               /* (comme avant : tout l'en-tete) */
                taglen = audio < size ? audio : size;
                if (taglen > 4 * 1024 * 1024) taglen = 4 * 1024 * 1024;   /* (grosse pochette integree) */
                tag = (unsigned char *)malloc(taglen);
                if (tag && (fseek(fp, 0, SEEK_SET) != 0 || fread(tag, 1, taglen, fp) != (size_t)taglen)) { free(tag); tag = NULL; }
            }
        }
        parse_tags(tag, tag ? taglen : 0, has_v1 ? v1 : NULL, &tg);
        free(tag);
        if (audio >= size) audio = 0;
        mp3_lame_info(fp, &audio, size, &tg);            /* v72 : sans blanc, gain LAME */
        if (audio >= size) audio = 0;
        mp3dec_init(&dec);
    } else if (fmt == FMT_FLAC) {
        flac_meta(fp, &tg, 0);
        if (fseek(fp, 0, SEEK_SET) != 0) err = "Lecture impossible";
        else {
            flac = drflac_open(fl_read, fl_seek, fl_tell, fp, NULL);
            if (!flac) err = "FLAC illisible";
            else if (flac->channels < 1 || flac->channels > 8 || flac->sampleRate == 0) err = "FLAC non pris en charge";
        }
    } else if (fmt == FMT_OGG) {
        static unsigned char hb[65536];
        if (fseek(fp, 0, SEEK_SET) == 0) {
            size_t n = fread(hb, 1, sizeof(hb), fp);
            ogg_tags(hb, (long)n, &tg);
            if (n >= 36 && memcmp(hb, "OggS", 4) == 0 && memcmp(hb + 28, "OpusHead", 8) == 0) err = "Opus non pris en charge";
        }
        if (!err && !ogg_open(fp, 0)) err = "OGG illisible (Vorbis seulement)";
        if (!err) audio = ftell(fp);
    } else if (fmt == FMT_M4A) {
        if (!m4a_open(fp, size, &tg, &err)) { if (!err) err = "M4A illisible"; }
    } else if (fmt >= FMT_GME) {
        if (!retro_open(names[idx], fmt, &tg, &err)) { if (!err) err = "Fichier illisible"; }
        else dur_cache[idx] = retro_len_ms / 1000;
    } else {
        if (!wav_parse(fp, size, &wav)) err = "WAV non pris en charge";
        else { audio = wav.data; wav_left = wav.len; }
    }
    if (!err && fmt != FMT_FLAC && fmt < FMT_GME && fseek(fp, audio, SEEK_SET) != 0) err = "Lecture impossible";
    if (err) {
        other_close();
        fclose(fp);
        track_err = err;
        xf_request = 0;
        if (xf) { src = SRC_NONE; f_head = f_tail = f_count = 0; xf_left = 0; consumed_skip = 0; }
        return 0;
    }
    snprintf(artist, sizeof(artist), "%s", tg.artist);
    snprintf(id3_title, sizeof(id3_title), "%s", tg.title);
    snprintf(id3_album, sizeof(id3_album), "%s", tg.album);
    snprintf(id3_year, sizeof(id3_year), "%s", tg.year);
    rg_cur_path = names[idx];
    rg_set(&tg);
    if (rg_mode && !rg_has) rgm_start(idx); else rgm_on = 0;     /* v72 : pas de gain connu : on le mesure */

    file_fmt = fmt;
    ogg_resync = 0;
    ffp = fp;
    fsize = size;
    f_audio = audio;
    fb_start = fb_end = 0;
    f_file_eof = 0;
    rpos = 0.0; prevL = prevR = 0;
    if (xf < 0 && f_count > 0) {                         /* v72 : sans blanc : ce morceau suit le precedent, encore dans la file */
        xf_left = 0;
        consumed_skip = (u32)f_count;
    } else if (xf > 0 && f_count > 0) {
        /* fondu enchaine : le debut de ce morceau se melange aux 'xf' derniers
           echantillons du precedent, encore dans la file */
        if (xf > f_count) xf = f_count;
        xf_total = xf_left = xf;
        xf_pos = (f_tail - xf + FIFO_SIZE) % FIFO_SIZE;
        consumed_skip = (u32)(f_count - xf);
    } else {
        f_head = f_tail = f_count = 0;
        xf_left = 0;
        consumed_skip = 0;
    }
    xf_request = 0;
    consumed = 0;
    stat_counted = 0;
    eof = 0; playing = 1; paused = 0; cur_track = idx;
    src = SRC_FILE;
    lyrics_load(idx);
    return 1;
}

// ---------- Bibliotheque : vues, file de lecture, aleatoire, repetition ----------
enum { VIEW_ALL, VIEW_FOLDERS, VIEW_ARTISTS, VIEW_ALBUMS, VIEW_LISTS, VIEW_COUNT };
enum { REP_OFF, REP_ALL, REP_ONE };
static int view_mode = VIEW_ALL, shuffle_on = 0, repeat_mode = REP_OFF;
static const char *const view_names[VIEW_COUNT] = { "Tous les morceaux", "Dossiers", "Artistes", "Albums", "Mes listes" };

/* etiquettes de tous les fichiers, lues en fond (vues Artistes et Albums) */
static char tg_artist[MAX_FILES][48], tg_album[MAX_FILES][64], tg_title[MAX_FILES][72];
static u8 tg_done[MAX_FILES];
static int tg_scanned = 0;

static void tags_reset(void) {
    memset(tg_done, 0, sizeof(tg_done));
    tg_scanned = 0;
}

static void tag_scan_one(int k) {
    static unsigned char head[16384];
    unsigned char v1[128];
    tags_t tg;
    tg_done[k] = 1;
    tg_scanned++;
    bib_dirty = 1;
    tg_artist[k][0] = tg_album[k][0] = tg_title[k][0] = 0;
    FILE *fp = fopen(names[k], "rb");
    if (!fp) return;
    int fm = audio_fmt(names[k]);
    tags_clear(&tg);
    if (fm == FMT_FLAC) flac_meta(fp, &tg, 0);
    else if (fm == FMT_OGG) { size_t n = fread(head, 1, sizeof(head), fp); ogg_tags(head, (long)n, &tg); }
    else if (fm == FMT_M4A) m4a_meta(names[k], &tg, 0);
    else if (fm == FMT_MP3) {
        size_t n = fread(head, 1, sizeof(head), fp);
        int has_v1 = 0;
        if (fseek(fp, -128, SEEK_END) == 0 && fread(v1, 1, 128, fp) == 128) has_v1 = 1;
        long tl = 0;
        unsigned char *t2 = NULL;                                 /* v85 : cadres de texte meme apres une grosse pochette */
        if (n >= 10 && !memcmp(head, "ID3", 3) && 10 + (long)synchsafe(head + 6) > (long)n) t2 = id3_text_only(fp, head, 10 + (long)synchsafe(head + 6), &tl);
        parse_tags(t2 ? t2 : head, t2 ? tl : (long)n, has_v1 ? v1 : NULL, &tg);
        free(t2);
    }
    fclose(fp);
    snprintf(tg_artist[k], sizeof(tg_artist[k]), "%s", tg.artist);
    snprintf(tg_album[k], sizeof(tg_album[k]), "%s", tg.album);
    snprintf(tg_title[k], sizeof(tg_title[k]), "%s", tg.title);
}

/* ---------- Bibliotheque en cache (SD:/gc-mp3/gc-mp3-bib.txt) ----------
   Durees et etiquettes deja lues : au demarrage suivant, plus besoin de relire
   chaque fichier (la liste defile sans a-coups, les vues Artistes / Albums sont
   pretes tout de suite). Une ligne par fichier : duree|artiste|album|chemin */
#define BIB_FILE GCD "/gc-mp3-bib.txt"
static int bib_dirty = 0;                  // (declaree plus haut)
static int lib_dirty;
static u64 bib_saved_t = 0;
static int bib_find(const char *path) {
    int lo = 0, hi = (nsorted < nfiles ? nsorted : nfiles) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = cmp_names(names[mid], path);
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    for (int k = nsorted; k < nfiles; k++) if (!strcmp(names[k], path)) return k;   /* (ajouts recents) */
    return -1;
}
static void bib_load(void) {
    if (!sd_ok || nfiles == 0) return;
    FILE *f = fopen(BIB_FILE, "r");
    if (!f) return;
    static char line[NAME_LEN + 200];
    while (fgets(line, sizeof(line), f)) {
        /* v22 : duree|artiste|album|titre|chemin (anciennes lignes : sans titre) */
        char *p[5];
        int n = 0;
        p[n++] = line;
        int v2 = !strncmp(line, "2:", 2);
        if (v2) p[0] = line + 2;
        for (char *c = p[0]; *c && n < (v2 ? 5 : 4); c++) if (*c == '|') { *c = 0; p[n++] = c + 1; }
        if (n < (v2 ? 5 : 4)) continue;
        char *path = p[v2 ? 4 : 3];
        path[strcspn(path, "\r\n")] = 0;
        int k = bib_find(path);
        if (k < 0) continue;
        int d = atoi(p[0]);
        if (d >= -1 && dur_cache[k] == -2) dur_cache[k] = d;
        if (v2 && !tg_done[k] && !is_retro[k]) {               /* (ancien format : etiquettes relues) */
            snprintf(tg_artist[k], sizeof(tg_artist[k]), "%s", p[1]);
            snprintf(tg_album[k], sizeof(tg_album[k]), "%s", p[2]);
            snprintf(tg_title[k], sizeof(tg_title[k]), "%s", p[3]);
            tg_done[k] = 1;
            tg_scanned++;
        }
    }
    fclose(f);
    lib_dirty = 1;
}
static void bib_save(void) {
    bib_dirty = 0;
    bib_saved_t = now_ms();
    if (!sd_ok) return;
    FILE *f = fopen(BIB_FILE ".part", "w");
    if (!f) return;
    int ok = 1;
    for (int k = 0; k < nfiles && ok; k++) {
        if (dur_cache[k] == -2 && !tg_done[k]) continue;
        char a[48], b[64], ti[72];
        snprintf(a, sizeof(a), "%s", tg_done[k] ? tg_artist[k] : "");
        snprintf(b, sizeof(b), "%s", tg_done[k] ? tg_album[k] : "");
        snprintf(ti, sizeof(ti), "%s", tg_done[k] ? tg_title[k] : "");
        for (char *c = a; *c; c++) if (*c == '|' || *c == '\n' || *c == '\r') *c = ' ';
        for (char *c = b; *c; c++) if (*c == '|' || *c == '\n' || *c == '\r') *c = ' ';
        for (char *c = ti; *c; c++) if (*c == '|' || *c == '\n' || *c == '\r') *c = ' ';
        if (!tg_done[k]) { if (fprintf(f, "%d|||%s\n", dur_cache[k] == -2 ? -3 : dur_cache[k], names[k]) < 0) ok = 0; continue; }
        if (fprintf(f, "2:%d|%s|%s|%s|%s\n", dur_cache[k] == -2 ? -3 : dur_cache[k], a, b, ti, names[k]) < 0) ok = 0;
    }
    if (fclose(f) != 0) ok = 0;
    if (ok) { remove(BIB_FILE); rename(BIB_FILE ".part", BIB_FILE); }
    else remove(BIB_FILE ".part");
}
static u64 last_input_ms;
/* enregistre quand rien ne bouge (pas pendant qu'on fait defiler une liste) */
static void bib_pump(void) {
    if (!bib_dirty) return;
    u64 n = now_ms();
    if (n - last_input_ms > 4000 && n - bib_saved_t > 20000) bib_save();
}

/* entrees de la liste affichee */
enum { E_TRACK, E_GROUP, E_UP };
typedef struct { u8 type; s16 idx; } lent_t;
static lent_t ents[MAX_FILES + 1];
static int nents = 0, sel_row = 0;
static char grp_name[MAX_FILES][64];
static s16 grp_count[MAX_FILES], grp_of[MAX_FILES];
static int ngroups = 0, cur_group = -1;     /* -1 = liste des groupes */
static int lib_dirty = 1;                   /* (declaree plus haut) */

static void group_key(int k, char *out, int cap) {
    if (view_mode == VIEW_FOLDERS) {
        const char *p = names[k];
        if (strncmp(p, SD_ROOT "/", strlen(SD_ROOT) + 1) == 0) p += strlen(SD_ROOT) + 1;
        snprintf(out, cap, "%s", p);
        char *sl = strrchr(out, '/');
        if (sl) *sl = 0; else snprintf(out, cap, "/");
    } else if (view_mode == VIEW_ARTISTS) {
        snprintf(out, cap, "%s", tg_artist[k][0] ? tg_artist[k] : "\x01");
    } else {
        snprintf(out, cap, "%s", tg_album[k][0] ? tg_album[k] : "\x01");
    }
}

static int grp_order[MAX_FILES];
static int cmp_groups(const void *a, const void *b) {
    const char *x = grp_name[*(const int *)a], *y = grp_name[*(const int *)b];
    if (x[0] == 1) return y[0] == 1 ? 0 : 1;         /* "inconnu" a la fin */
    if (y[0] == 1) return -1;
    return strcasecmp(x, y);
}

static void build_groups(void) {
    static char tmpn[MAX_FILES][64];
    static s16 tmpc[MAX_FILES];
    int n = 0;
    char key[64];
    for (int k = 0; k < nfiles; k++) {
        grp_of[k] = -1;
        if (is_retro[k]) continue;                               /* (onglet RETRO) */
        if (view_mode != VIEW_FOLDERS && !tg_done[k]) continue;   /* etiquettes pas encore lues */
        group_key(k, key, sizeof(key));
        int g = -1;
        for (int i = 0; i < n; i++) if (strcmp(tmpn[i], key) == 0) { g = i; break; }
        if (g < 0) { g = n++; snprintf(tmpn[g], 64, "%s", key); tmpc[g] = 0; }
        tmpc[g]++;
        grp_of[k] = (s16)g;
    }
    for (int i = 0; i < n; i++) grp_order[i] = i;
    memcpy(grp_name, tmpn, sizeof(grp_name[0]) * n);
    qsort(grp_order, n, sizeof(int), cmp_groups);
    static int rank[MAX_FILES];
    for (int i = 0; i < n; i++) rank[grp_order[i]] = i;
    for (int i = 0; i < n; i++) { snprintf(grp_name[i], 64, "%s", tmpn[grp_order[i]]); grp_count[i] = tmpc[grp_order[i]]; }
    for (int k = 0; k < nfiles; k++) if (grp_of[k] >= 0) grp_of[k] = (s16)rank[grp_of[k]];
    ngroups = n;
}

static const char *group_label(int g) {
    if (g < 0 || g >= ngroups) return "";
    if (grp_name[g][0] == 1) return view_mode == VIEW_ARTISTS ? "(artiste inconnu)" : "(album inconnu)";
    return grp_name[g];
}

/* listes de lecture et recherche (lib_extra.c) */
static char lib_find[64];
static void lists_build_groups(void);
static int lists_tracks(int g, s16 *out, int max);
static int lib_match(int k);
static void draw_actions(void);
static void pl_load_all(void);
static void stats_load(void);
static void stats_save(void);
static int stats_dirty;
static void rating_load(void);
static void rating_save(void);
static int rating_dirty;

/* reconstruit la liste en gardant l'element selectionne */
static void lib_rebuild(void) {
    lent_t keep = { 255, -1 };
    if (sel_row >= 0 && sel_row < nents) keep = ents[sel_row];
    char keepname[64] = "";
    if (keep.type == E_GROUP && keep.idx < ngroups) snprintf(keepname, sizeof(keepname), "%s", grp_name[keep.idx]);
    char curname[64] = "";
    if (cur_group >= 0 && cur_group < ngroups) snprintf(curname, sizeof(curname), "%s", grp_name[cur_group]);

    nents = 0;
    if (lib_find[0]) {                                  /* recherche : les morceaux qui correspondent */
        ngroups = 0;
        cur_group = -1;
        for (int k = 0; k < nfiles; k++) if (!is_retro[k] && lib_match(k)) { ents[nents].type = E_TRACK; ents[nents++].idx = (s16)k; }
    } else if (view_mode == VIEW_ALL) {
        ngroups = 0;
        cur_group = -1;
        for (int k = 0; k < nfiles; k++) if (!is_retro[k]) { ents[nents].type = E_TRACK; ents[nents++].idx = (s16)k; }
    } else if (view_mode == VIEW_LISTS) {             /* mes listes (+ les plus ecoutes) */
        lists_build_groups();
        if (cur_group >= 0) {
            cur_group = -1;
            for (int g = 0; g < ngroups; g++) if (strcmp(grp_name[g], curname) == 0) { cur_group = g; break; }
        }
        if (cur_group < 0) {
            for (int g = 0; g < ngroups; g++) { ents[nents].type = E_GROUP; ents[nents++].idx = (s16)g; }
        } else {
            static s16 lt[MAX_FILES];
            int n = lists_tracks(cur_group, lt, MAX_FILES);
            ents[nents].type = E_UP; ents[nents++].idx = (s16)cur_group;
            for (int i = 0; i < n && nents < MAX_FILES + 1; i++) { ents[nents].type = E_TRACK; ents[nents++].idx = lt[i]; }
        }
    } else {
        build_groups();
        if (cur_group >= 0) {                           /* le groupe a pu changer de numero */
            cur_group = -1;
            for (int g = 0; g < ngroups; g++) if (strcmp(grp_name[g], curname) == 0) { cur_group = g; break; }
        }
        if (cur_group < 0) {
            for (int g = 0; g < ngroups; g++) { ents[nents].type = E_GROUP; ents[nents++].idx = (s16)g; }
        } else {
            ents[nents].type = E_UP; ents[nents++].idx = (s16)cur_group;
            for (int k = 0; k < nfiles; k++)
                if (grp_of[k] == cur_group) { ents[nents].type = E_TRACK; ents[nents++].idx = (s16)k; }
        }
    }
    for (int i = 0; i < nents; i++) {
        if (keep.type == E_TRACK && ents[i].type == E_TRACK && ents[i].idx == keep.idx) { sel_row = i; break; }
        if (keep.type == E_GROUP && ents[i].type == E_GROUP && strcmp(grp_name[ents[i].idx], keepname) == 0) { sel_row = i; break; }
    }
    if (sel_row >= nents) sel_row = nents > 0 ? nents - 1 : 0;
    if (sel_row < 0) sel_row = 0;
    lib_dirty = 0;
}

/* --- file de lecture --- */
static s16 queue[MAX_FILES];
static int qn = 0, qpos = -1;
static u32 rnd_state = 0x12345678u;

static u32 lib_rand(void) {
    rnd_state ^= (u32)gettime();
    rnd_state = rnd_state * 1664525u + 1013904223u;
    return rnd_state >> 8;
}

static void queue_shuffle_from(int first) {   /* melange en gardant 'first' en tete */
    for (int i = qn - 1; i > 0; i--) {
        int j = (int)(lib_rand() % (u32)(i + 1));
        s16 t = queue[i]; queue[i] = queue[j]; queue[j] = t;
    }
    for (int i = 0; i < qn; i++) if (queue[i] == first) { queue[i] = queue[0]; queue[0] = (s16)first; break; }
    qpos = 0;
}

/* la file = les morceaux de la liste en cours (tous, ou ceux du groupe ouvert) */
static void queue_build(int track) {
    qn = 0;
    if (track >= 0 && track < nfiles && is_retro[track]) {     /* onglet RETRO : sa propre liste */
        for (int k = 0; k < nfiles; k++) if (is_retro[k]) queue[qn++] = (s16)k;
    } else {
        for (int i = 0; i < nents; i++) if (ents[i].type == E_TRACK) queue[qn++] = ents[i].idx;
        if (qn == 0) for (int k = 0; k < nfiles; k++) if (!is_retro[k]) queue[qn++] = (s16)k;
    }
    qpos = 0;
    for (int i = 0; i < qn; i++) if (queue[i] == track) { qpos = i; break; }
    if (shuffle_on) queue_shuffle_from(track);
}

static void select_track_row(int track) {
    for (int i = 0; i < nents; i++) if (ents[i].type == E_TRACK && ents[i].idx == track) { sel_row = i; return; }
}

static void play_queue_pos(int p) {
    if (p < 0 || p >= qn) return;
    qpos = p;
    start_track(queue[p]);
    select_track_row(queue[p]);
}

/* morceau suivant (dir = 1) ou precedent (-1) ; automatic = fin du morceau */
static void track_next(int dir, int automatic) {
    if (qn == 0 || qpos < 0 || qpos >= qn || queue[qpos] != cur_track) {
        if (cur_track < 0) return;
        queue_build(cur_track);
    }
    if (automatic && sleep_eot) { sleep_eot = 0; sleep_fade = 256; stop_track(); stop_station(); party_on = 0; return; }   /* v76 : arret a la fin du morceau */
    if (automatic && repeat_mode == REP_ONE) { play_queue_pos(qpos); return; }
    int p = qpos + dir;
    if (p >= qn) {
        if (repeat_mode == REP_OFF && automatic) { stop_track(); return; }
        if (repeat_mode == REP_OFF) p = qn - 1;
        else { if (shuffle_on) queue_shuffle_from(queue[lib_rand() % (u32)qn]); p = 0; }
    }
    if (p < 0) p = (repeat_mode == REP_OFF) ? 0 : qn - 1;
    play_queue_pos(p);
}

/* y aura-t-il un morceau apres celui-ci (fin de morceau automatique) ? */
static int auto_next_exists(void) {
    if (cur_track < 0) return 0;
    if (qn == 0 || qpos < 0 || qpos >= qn || queue[qpos] != cur_track) queue_build(cur_track);
    return repeat_mode != REP_OFF || qpos + 1 < qn;
}

static void set_shuffle(int on) {
    shuffle_on = on;
    if (qn <= 0) return;
    int cur = (qpos >= 0 && qpos < qn) ? queue[qpos] : -1;
    if (on) { if (cur >= 0) queue_shuffle_from(cur); }
    else {
        /* ordre normal de la liste */
        for (int i = 1; i < qn; i++) {
            s16 v = queue[i]; int j = i - 1;
            while (j >= 0 && queue[j] > v) { queue[j + 1] = queue[j]; j--; }
            queue[j + 1] = v;
        }
        for (int i = 0; i < qn; i++) if (queue[i] == cur) { qpos = i; break; }
    }
}

static void set_view(int v) {
    view_mode = (v % VIEW_COUNT + VIEW_COUNT) % VIEW_COUNT;
    cur_group = -1;
    sel_row = 0;
    lib_rebuild();
    if (cur_track >= 0) select_track_row(cur_track);
}

/* lecture des etiquettes en fond : quelques fichiers par image, seulement
   quand une vue Artistes / Albums les attend */
static void lib_pump(void) {
    if (lib_dirty) lib_rebuild();
    if (view_mode != VIEW_ARTISTS && view_mode != VIEW_ALBUMS) return;
    if (tg_scanned >= nfiles) return;
    u64 t0 = now_ms();
    int done = 0;
    for (int k = 0; k < nfiles && now_ms() - t0 < 12 && io_safe(); k++)
        if (!tg_done[k]) { tag_scan_one(k); done++; service_audio(); }
    if (done && (tg_scanned >= nfiles || (tg_scanned % 24) < done)) lib_rebuild();
}

/* touche A dans l'onglet MUSIQUE */
static void lib_activate(void) {
    if (sel_row < 0 || sel_row >= nents) return;
    lent_t e = ents[sel_row];
    if (e.type == E_GROUP) { cur_group = e.idx; sel_row = 1; lib_rebuild(); sel_row = nents > 1 ? 1 : 0; return; }
    if (e.type == E_UP) {
        int g = cur_group;
        cur_group = -1;
        lib_rebuild();
        for (int i = 0; i < nents; i++) if (ents[i].type == E_GROUP && ents[i].idx == g) { sel_row = i; break; }
        return;
    }
    queue_build(e.idx);
    start_track(e.idx);
}

// ---------- Paroles synchronisees (fichier .lrc a cote du MP3) ----------
#define LRC_MAX 500
typedef struct { u32 ms; u32 off; } lrc_line_t;
static lrc_line_t lrc_lines[LRC_MAX];
static int lrc_n = 0, lrc_track = -1;
static char *lrc_text = NULL;
static void text_display(const char *in, char *out, int outsz);

static int cmp_lrc(const void *a, const void *b) {
    u32 x = ((const lrc_line_t *)a)->ms, y = ((const lrc_line_t *)b)->ms;
    return x < y ? -1 : x > y;
}

/* "[01:23.45]Paroles" (plusieurs [temps] possibles par ligne) */
static void lyrics_parse(const unsigned char *d, long len);
static void lyrics_load(int track) {
    free(lrc_text);
    lrc_text = NULL;
    lrc_n = 0;
    lrc_track = track;
    if (track < 0 || track >= nfiles) return;
    char path[NAME_LEN + 8];
    snprintf(path, sizeof(path), "%s", names[track]);
    strip_audio_ext(path);
    strcat(path, ".lrc");
    long len = 0;
    unsigned char *d = read_file(path, 256 * 1024, &len);
    if (!d) return;
    lyrics_parse(d, len);
    free(d);
}

/* texte LRC : "[mm:ss.cc] paroles" (plusieurs temps possibles par ligne) */
static void lyrics_parse(const unsigned char *d, long len) {
    free(lrc_text);
    lrc_text = NULL;
    lrc_n = 0;
    lrc_text = (char *)malloc(len + 1);
    if (!lrc_text) return;
    u32 tl = 0;
    long i = (len >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) ? 3 : 0;   /* BOM UTF-8 */
    while (i < len && lrc_n < LRC_MAX) {
        long e = i;
        while (e < len && d[e] != '\n') e++;
        long p = i;
        u32 times[16];
        int nt = 0;
        while (p < e && d[p] == '[') {
            int mm = 0, ss = 0, cs = 0, digits = 0;
            long q = p + 1;
            while (q < e && isdigit(d[q])) { mm = mm * 10 + (d[q] - '0'); q++; }
            if (q < e && d[q] == ':') {
                q++;
                while (q < e && isdigit(d[q])) { ss = ss * 10 + (d[q] - '0'); q++; }
                if (q < e && (d[q] == '.' || d[q] == ':')) {
                    q++;
                    while (q < e && isdigit(d[q]) && digits < 3) { cs = cs * 10 + (d[q] - '0'); q++; digits++; }
                    while (q < e && isdigit(d[q])) q++;
                }
                if (q < e && d[q] == ']') {
                    int ms = digits == 1 ? cs * 100 : digits == 2 ? cs * 10 : cs;
                    if (nt < 16) times[nt++] = (u32)(mm * 60000 + ss * 1000 + ms);
                    p = q + 1;
                    continue;
                }
            }
            break;                                            /* [ar:...] et autres balises */
        }
        while (p < e && d[p] == ' ') p++;
        if (nt > 0) {
            long te = e;
            while (te > p && (d[te - 1] == '\r' || d[te - 1] == ' ')) te--;
            u32 off = tl;
            memcpy(lrc_text + tl, d + p, te - p);
            tl += (u32)(te - p);
            lrc_text[tl++] = 0;
            for (int k = 0; k < nt && lrc_n < LRC_MAX; k++) { lrc_lines[lrc_n].ms = times[k]; lrc_lines[lrc_n].off = off; lrc_n++; }
        }
        i = e + 1;
    }
    qsort(lrc_lines, lrc_n, sizeof(lrc_line_t), cmp_lrc);
}

/* ligne en cours (-1 avant la premiere) */
static int lyrics_current(void) {
    if (lrc_n == 0 || lrc_track != cur_track) return -1;
    u32 ms = (u32)((u64)consumed_heard() * 1000u / 48000u) + 150;     /* petite avance : on lit avant d'entendre */
    int lo = 0, hi = lrc_n - 1, r = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (lrc_lines[mid].ms <= ms) { r = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return r;
}

static const char *lyrics_line(int i) {
    if (i < 0 || i >= lrc_n || !lrc_text) return "";
    return lrc_text + lrc_lines[i].off;
}

// ---------- Reglages (sd:/gc-mp3/gc-mp3.txt) et reprise de la lecture ----------
#define SETTINGS_FILE GCD "/gc-mp3.txt"
enum { VID_AUTO, VID_480I, VID_480P, VID_COUNT };
static int covers_auto = 1;
static int lyrics_auto = 1;                 /* paroles cherchees sur LRCLIB */
static char city[64];                       /* onglet INFOS (tab_infos.c) */
static char news_url[256];
static int alarm_on, alarm_min, alarm_src, alarm_days;   /* v76 : alarm_days 0=tous,1=semaine,2=week-end */
static int phone_auto;                      /* onglet TELEPHONE (tab_phone.c) */
static int rumble_on = 1;                   /* v18 : la manette vibre (mode soiree, jeux) */
static int pod_auto = 1;                    /* v18 : nouveaux episodes telecharges tout seuls */
static int wall_on = 0;                     /* v18 : fond d'ecran = une photo */
static int season_on = 1;                   /* v21 : decor de saison (Halloween, Noel, ete) */
static char wall_path[NAME_LEN];
#define PARTY_STYLES 4
static int party_style = 0;                 /* mode soiree : style du visualiseur */
static const char *const party_keys[PARTY_STYLES] = { "pulsation", "barres", "tunnel", "spectrogramme" };
static const char *const layout_names[LAYOUT_COUNT] = { "Classique", "Pochette g\xC3\xA9" "ante", "Liste \xC3\xA9tendue", "Minimaliste", "Spectre g\xC3\xA9" "ant" };
static const char *const layout_keys[LAYOUT_COUNT] = { "classique", "pochette", "liste", "minimal", "spectre" };
/* v43 : paroles dans le lecteur (0 spectre seul, 1 paroles a la place du spectre, 2 les deux) ;
   eclair blanc sur les basses du mode soiree (0 partout, 1 sauf spectrogramme, 2 jamais) */
static int lyr_show = 1, flash_mode = 1;
static int rg_offset = 0;                     /* v70 : jeu de rythme : decalage des notes (ms) */
static const char *const lyr_keys[3] = { "spectre", "paroles", "les_deux" };
static const char *const lyr_names[3] = { "Spectre seulement", "Paroles (sinon spectre)", "Paroles + spectre" };
static const char *const flash_keys[3] = { "partout", "sauf_spectrogramme", "jamais" };
static const char *const flash_names[3] = { "Partout", "Sauf spectrogramme", "Jamais" };
/* le mot 'w' (en minuscules) apparait-il dans v ? */
static int has_word(const char *v, const char *w) {
    char low[256];
    int n = 0;
    for (; *v && n < 255; v++) low[n++] = (char)tolower((unsigned char)*v);
    low[n] = 0;
    return strstr(low, w) != NULL;
}
static const char *const party_names[PARTY_STYLES] = { "Pulsation", "Barres", "Tunnel", "Spectrogramme" };
static void party_start(void);
static int saver_min = 5;                   /* economiseur d'ecran (minutes, 0 = jamais) */
enum { SV_SPECTRE, SV_POCHETTE, SV_HORLOGE, SV_ETOILES, SV_PHOTOS, SV_NOIR, SV_AQUARIUM, SV_SPECTRO, SV_TABLEAU, SV_COUNT };
static int saver_style = SV_SPECTRE;        /* ce qu'affiche l'economiseur */
static int sleep_min = 0;                   /* minuterie : arret apres N minutes (0 = non, -1 = a la fin du morceau) */
static u64 sleep_at = 0;
static const char *const saver_keys[SV_COUNT] = { "spectre", "pochette", "horloge", "etoiles", "photos", "noir", "aquarium", "spectrogramme", "horloge_meteo" };
static const char *const saver_names[SV_COUNT] = { "Spectre", "Pochette", "Horloge", "\xC3\x89toiles",
                                                   "Photos (sd:/ecran)", "\xC3\x89" "cran noir", "Aquarium", "Spectrogramme", "Horloge + m\xC3\xA9t\xC3\xA9o + pochette" };
static int video_pref = VID_AUTO;
static int vid_active = VID_480I;           /* mode vraiment utilise depuis le demarrage */
static int tv_crt = 0;                      /* v22 : tele cathodique (480i, 4:3) */
static int scr_pct = 100;                   /* taille de l'image : 80 a 112 % (au-dela de 100 : plus large, par la sortie video) */
#define SCR_MAX 112
static int scr_hpct = 100;                  /* hauteur de l'image : 80 a 100 % (bords haut et bas coupes) */
static int scr_vpos = 0;                    /* decalage vertical en points (-24 a +24, positif = plus bas) */
static int scr_wide = 0;                    /* 1 = televiseur en 16:9 (image etiree par la tele) */
#define WV_X           26                   // zone d'affichage de la page
#define WV_Y           116
#define WV_W           586
#define WV_H           272
static int wv_lw = WV_W;                    // largeur de mise en page (16:9 : plus large)
static f32 scr_x0 = 0, scr_w = 640;         /* bords gauche et largeur du repere (16:9 : -107 et 853) */
/* 16:9 : l'interface (dessinee pour 0..640) est elargie a tout l'ecran : les
   positions et les largeurs sont etirees, mais le texte, les cercles, les
   pochettes et les images gardent leurs vraies proportions.
   X ecran = ux_o + x * ux_k ; une "zone native" (ux_native) garde k = 1. */
static f32 ux_k = 1, ux_o = 0;
static inline f32 UX(f32 x) { return ux_o + x * ux_k; }
typedef struct { f32 k, o; } uxsave_t;
static uxsave_t ux_native(f32 anchor) {                /* autour de 'anchor', taille reelle */
    uxsave_t s = { ux_k, ux_o };
    ux_o = UX(anchor) - anchor;
    ux_k = 1;
    return s;
}
static void ux_restore(uxsave_t s) { ux_k = s.k; ux_o = s.o; }
/* Langue du contenu (recherche de podcasts, radios du pays, sites web, meteo).
   -1 = celle de la console (reglage de l'IPL), sinon l'ordre de SYS_LANG_* */
#define LANG_COUNT 6
static int lang_pref = -1;
static int lang_sys = -2;                   /* langue de la console (lue une fois) */
static const char *const lang_keys[LANG_COUNT] = { "en", "de", "fr", "es", "it", "nl" };
static const char *const lang_names[LANG_COUNT] = { "English", "Deutsch", "Fran\xC3\xA7" "ais", "Espa\xC3\xB1ol", "Italiano", "Nederlands" };
static int lang_console(void) {
    if (lang_sys == -2) {
        lang_sys = (int)SYS_GetLanguage();
        if (lang_sys < 0 || lang_sys >= LANG_COUNT) lang_sys = SYS_LANG_FRENCH;
    }
    return lang_sys;
}
static int lang_cur(void) { return lang_pref >= 0 && lang_pref < LANG_COUNT ? lang_pref : lang_console(); }
static const char *lang_code(void) { return lang_keys[lang_cur()]; }
/* pays (code ISO) : anglais = Royaume-Uni, ou Etats-Unis sur une console NTSC */
static const char *lang_country(void) {
    static const char *const cc[LANG_COUNT] = { "GB", "DE", "FR", "ES", "IT", "NL" };
    int l = lang_cur();
    if (l == SYS_LANG_ENGLISH && VIDEO_GetCurrentTvMode() == VI_NTSC) return "US";
    return cc[l];
}
static const char *lang_country_name(void) {       /* "en France", "au Royaume-Uni"... */
    static const char *const nm[LANG_COUNT] = { "au Royaume-Uni", "en Allemagne", "en France", "en Espagne", "en Italie", "aux Pays-Bas" };
    if (!strcmp(lang_country(), "US")) return TR("aux \xC3\x89tats-Unis");
    return TR(nm[lang_cur()]);
}
static int resume_on = 1;
static char resume_path[NAME_LEN] = "";
static int resume_pos = 0;
static int settings_dirty = 0;

static int yes(const char *v) {
    return !(strncasecmp(v, "non", 3) == 0 || strcmp(v, "0") == 0 || strncasecmp(v, "no", 2) == 0);
}

static void save_settings(void);

/* un reglage "cle = valeur" (fichier gc-mp3.txt, ou page du telephone) */
static void setting_apply(const char *k, char *v) {
        if (!strcasecmp(k, "pochettes_auto")) covers_auto = yes(v);
        else if (!strcasecmp(k, "aleatoire")) shuffle_on = yes(v);
        else if (!strcasecmp(k, "repetition")) repeat_mode = !strncasecmp(v, "tout", 4) ? REP_ALL : !strncasecmp(v, "un", 2) ? REP_ONE : REP_OFF;
        else if (!strcasecmp(k, "affichage"))
            view_mode = !strncasecmp(v, "dossier", 7) ? VIEW_FOLDERS : !strncasecmp(v, "artiste", 7) ? VIEW_ARTISTS :
                        !strncasecmp(v, "album", 5) ? VIEW_ALBUMS : !strncasecmp(v, "liste", 5) ? VIEW_LISTS : VIEW_ALL;
        else if (!strcasecmp(k, "volume")) { volume_pct = atoi(v); if (volume_pct < 0) volume_pct = 0; if (volume_pct > 100) volume_pct = 100; }
        else if (!strcasecmp(k, "theme")) { for (int i = 0; i < THEME_COUNT; i++) if (!strcasecmp(v, theme_keys[i])) theme_apply(i); }
        else if (!strcasecmp(k, "paroles_auto")) lyrics_auto = yes(v);
        else if (!strcasecmp(k, "ville")) snprintf(city, sizeof(city), "%s", v);
        else if (!strcasecmp(k, "actualites")) { if (!strncasecmp(v, "http", 4)) snprintf(news_url, sizeof(news_url), "%s", v); }
        else if (!strcasecmp(k, "reveil")) { int h, m; if (sscanf(v, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) { alarm_on = 1; alarm_min = h * 60 + m; } else alarm_on = 0; }
        else if (!strcasecmp(k, "reveil_joue")) alarm_src = atoi(v) < 0 ? 0 : atoi(v);
        else if (!strcasecmp(k, "reveil_jours")) { alarm_days = atoi(v); if (alarm_days < 0 || alarm_days > 2) alarm_days = 0; }
        else if (!strcasecmp(k, "telephone")) phone_auto = yes(v);
        else if (!strcasecmp(k, "veilleuse_luminosite")) { night_lum = atoi(v); if (night_lum < 1) night_lum = 1; if (night_lum > 5) night_lum = 5; }
        else if (!strcasecmp(k, "veilleuse_radio")) night_radio = yes(v);
        else if (!strcasecmp(k, "maj_auto")) upd_auto = yes(v);
        else if (!strcasecmp(k, "maj_depot")) { if (strchr(v, '/') && !strchr(v, ' ')) snprintf(upd_repo, sizeof(upd_repo), "%s", v); }
        else if (!strcasecmp(k, "maj_fichier")) snprintf(upd_file, sizeof(upd_file), "%s", v);
        else if (!strcasecmp(k, "disposition")) { for (int i = 0; i < LAYOUT_COUNT; i++) if (!strcasecmp(v, layout_keys[i])) for (int t = 0; t < TAB_COUNT; t++) layout_tab[t] = i; }
        else if (!strncasecmp(k, "disposition_", 12)) {
            for (int t = 0; t < TAB_COUNT; t++) if (IS_PLAYER_TAB(t) && !strcasecmp(k + 12, tab_keys[t]))
                for (int i = 0; i < LAYOUT_COUNT; i++) if (!strcasecmp(v, layout_keys[i])) layout_tab[t] = i;
        }
        else if (!strcasecmp(k, "paroles_affichage")) { for (int i = 0; i < 3; i++) if (!strcasecmp(v, lyr_keys[i])) lyr_show = i; }
        else if (!strcasecmp(k, "rythme_decalage")) { rg_offset = atoi(v); if (rg_offset < -200 || rg_offset > 200) rg_offset = 0; }
        else if (!strcasecmp(k, "eclair_basses")) { for (int i = 0; i < 3; i++) if (!strcasecmp(v, flash_keys[i])) flash_mode = i; }
        else if (!strcasecmp(k, "ordre_onglets")) {
            u8 o[TAB_COUNT]; int n = 0; u32 seen = 0;
            const char *q = v;
            while (*q && n < TAB_COUNT) {
                char w[24]; int l = 0;
                while (*q == ',' || *q == ' ' || *q == ';') q++;
                while (*q && *q != ',' && *q != ' ' && *q != ';' && l < 23) w[l++] = *q++;
                while (*q && *q != ',' && *q != ' ' && *q != ';') q++;
                w[l] = 0;
                for (int t = 0; t < TAB_COUNT && l; t++) if (!strcasecmp(w, tab_keys[t]) && !(seen & (1u << t))) { o[n++] = (u8)t; seen |= 1u << t; }
            }
            for (int t = 0; t < TAB_COUNT; t++) if (!(seen & (1u << t))) o[n++] = (u8)t;
            memcpy(tab_order, o, sizeof(tab_order));
        }
        else if (!strcasecmp(k, "onglets_caches")) {
            tab_hidden = 0;
            for (int i = 1; i < TAB_COUNT; i++) if (has_word(v, tab_keys[i])) tab_hidden |= 1u << i;
        }
        else if (!strcasecmp(k, "elements")) {
            static const char *const ek[EL_COUNT] = { "pochette", "paroles", "boutons", "infos", "horloge" };
            el_mask = 0;
            for (int i = 0; i < EL_COUNT; i++) if (has_word(v, ek[i])) el_mask |= 1u << i;
        }
        else if (!strcasecmp(k, "soiree")) { for (int i = 0; i < PARTY_STYLES; i++) if (!strcasecmp(v, party_keys[i])) party_style = i; }
        else if (!strcasecmp(k, "graves")) eq_bass = atoi(v);
        else if (!strcasecmp(k, "aigus")) eq_treble = atoi(v);
        else if (!strcasecmp(k, "mediums")) eq_mid = atoi(v);
        else if (!strcasecmp(k, "bas_mediums")) eq_lowmid = atoi(v);
        else if (!strcasecmp(k, "presence")) eq_pres = atoi(v);
        else if (x73_setting(k, v)) { }
        else if (nfs_setting(k, v)) { }                  /* v86 : serveurs du reseau */                  /* v73 : podcasts, enregistrement programme, cartes virtuelles... */
        else if (!strcasecmp(k, "egaliseur")) { eq_preset = 0; for (int i = 0; i < EQ_PRESETS; i++) if (!strcasecmp(v, eq_keys[i])) eq_preset = i; }
        else if (!strcasecmp(k, "fondu")) { xf_sec = atoi(v); if (xf_sec < 0) xf_sec = 0; if (xf_sec > 8) xf_sec = 8; }
        else if (!strcasecmp(k, "volume_egalise")) rg_mode = !strncasecmp(v, "album", 5) ? 2 : (!strncasecmp(v, "morceau", 7) || !strncasecmp(v, "oui", 3)) ? 1 : 0;
        else if (!strcasecmp(k, "economiseur")) { saver_min = atoi(v); if (saver_min < 0) saver_min = 0; }
        else if (!strcasecmp(k, "economiseur_style")) { for (int i = 0; i < SV_COUNT; i++) if (!strcasecmp(v, saver_keys[i])) saver_style = i; }
        else if (!strcasecmp(k, "ecran_format")) { scr_wide = strstr(v, "16") != NULL; wv_lw = scr_wide ? WV_W * 4 / 3 : WV_W; }
        else if (!strcasecmp(k, "ecran_hauteur")) { scr_hpct = atoi(v); if (scr_hpct < 80) scr_hpct = 80; if (scr_hpct > 100) scr_hpct = 100; }
        else if (!strcasecmp(k, "ecran_position")) { scr_vpos = atoi(v); if (scr_vpos < -24) scr_vpos = -24; if (scr_vpos > 24) scr_vpos = 24; }
        else if (!strcasecmp(k, "ecran_taille")) { scr_pct = atoi(v); if (scr_pct < 80) scr_pct = 80; if (scr_pct > SCR_MAX) scr_pct = SCR_MAX; }
        else if (!strcasecmp(k, "style")) ui_skin = !strncasecmp(v, "mod", 3) ? 1 : !strncasecmp(v, "nouv", 4) ? 2 : !strncasecmp(v, "aur", 3) ? 3 : 0;
        else if (!strcasecmp(k, "spectrogramme_couleurs")) { for (int i = 0; i < SGP_COUNT; i++) if (!strcasecmp(v, sgp_keys[i])) sg_pal_id = i; }
        else if (!strcasecmp(k, "spectre")) { for (int i = 0; i < VZ_COUNT; i++) if (!strcasecmp(v, viz_keys[i])) viz_style = i; }
        else if (!strcasecmp(k, "jeux_affichage")) { for (int i = 0; i < 6; i++) if (!strcasecmp(v, glview_keys[i])) gl_view = i; }
        else if (!strcasecmp(k, "jeux_tri")) { for (int i = 0; i < 4; i++) if (!strcasecmp(v, gl_sort_keys[i])) gl_sort = i; }
        else if (!strcasecmp(k, "jeux_animations")) gl_anim = !has_word(v, "non");
        else if (!strcasecmp(k, "lanceur_maison_retour")) nat_igr = !has_word(v, "non");
        else if (!strcasecmp(k, "resident_carte_virtuelle")) nat_card = !has_word(v, "non");
        else if (!strcasecmp(k, "interface")) { ui_lang = !strncasecmp(v, "en", 2) ? 1 : !strncasecmp(v, "fr", 2) ? 0 : -1; ui_lang_apply(); }
        else if (!strcasecmp(k, "langue")) { lang_pref = -1; for (int i = 0; i < LANG_COUNT; i++) if (!strncasecmp(v, lang_keys[i], 2)) lang_pref = i; }
        else if (!strcasecmp(k, "tele")) tv_crt = !strncasecmp(v, "cath", 4) || !strncasecmp(v, "crt", 3);
        else if (!strcasecmp(k, "video")) video_pref = !strcasecmp(v, "480p") ? VID_480P : !strcasecmp(v, "480i") ? VID_480I : VID_AUTO;
        else if (!strcasecmp(k, "reprise")) resume_on = yes(v);
        else if (!strcasecmp(k, "reprise_fichier")) snprintf(resume_path, sizeof(resume_path), "%s", v);
        else if (!strcasecmp(k, "reprise_position")) resume_pos = atoi(v);
        else if (!strcasecmp(k, "vibrations")) rumble_on = yes(v);
        else if (!strcasecmp(k, "decor_saison")) season_on = yes(v);
        else if (!strcasecmp(k, "podcasts_auto")) pod_auto = yes(v);
        else if (!strcasecmp(k, "radio_reprise")) radio_resume = yes(v);
        else if (!strcasecmp(k, "enchainement_sans_blanc")) gapless_on = yes(v);
        else if (!strcasecmp(k, "radio_derniere")) snprintf(radio_last_url, sizeof(radio_last_url), "%s", v);
        else if (!strcasecmp(k, "radio_derniere_nom")) snprintf(radio_last_name, sizeof(radio_last_name), "%s", v);
        else if (!strcasecmp(k, "fond_ecran")) { if (!strcasecmp(v, "non") || !v[0]) wall_on = 0; else { wall_on = 1; snprintf(wall_path, sizeof(wall_path), "%s", v); } }
        else if (!strcasecmp(k, "fond_ecran_photo")) snprintf(wall_path, sizeof(wall_path), "%s", v);
}

static void load_settings(void) {
    ui_lang_apply();
    if (!sd_ok) return;
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) { save_settings(); return; }
    char line[NAME_LEN + 64];
    while (fgets(line, sizeof(line), f)) {
        char *t = trim(line);
        if (t[0] == '#') continue;
        char *eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        setting_apply(trim(t), trim(eq + 1));
    }
    fclose(f);
    eq_setup();
}

static void save_settings(void) {
    static const char *const rep[3] = { "non", "tout", "un" };
    static const char *const aff[VIEW_COUNT] = { "tous", "dossiers", "artistes", "albums", "listes" };
    static const char *const vid[VID_COUNT] = { "auto", "480i", "480p" };
    settings_dirty = 0;
    if (!sd_ok) return;
    static const char *const ek[EL_COUNT] = { "pochette", "paroles", "boutons", "infos", "horloge" };
    char els[96] = "", hid[160] = "";
    for (int i = 0; i < EL_COUNT; i++) if (EL(i)) { if (els[0]) strcat(els, ", "); strcat(els, ek[i]); }
    for (int i = 1; i < TAB_COUNT; i++) if (!tab_shown(i)) { if (hid[0]) strcat(hid, ", "); strcat(hid, tab_keys[i]); }
    char alarm_txt[16];
    if (alarm_on) snprintf(alarm_txt, sizeof(alarm_txt), "%02d:%02d", alarm_min / 60, alarm_min % 60);
    else snprintf(alarm_txt, sizeof(alarm_txt), "non");
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "# Reglages de GC Station (aussi modifiables avec START > Reglages)\n"
               "# Pochettes manquantes cherchees sur Internet (MusicBrainz) : oui / non\n"
               "pochettes_auto = %s\n"
               "# Paroles synchronisees cherchees sur Internet (LRCLIB) : oui / non\n"
               "paroles_auto = %s\n"
               "# Egaliseur : reglage tout pret (perso / plat / rap / rock / pop / basses / voix / electro / classique / jazz)\n"
               "# puis graves, mediums, aigus en dB (-12 a +12)\n"
               "egaliseur = %s\ngraves = %d\nmediums = %d\naigus = %d\n"
               "# Fondu enchaine entre les morceaux, en secondes (0 = non, 8 maximum)\n"
               "fondu = %d\n"
               "# Volume egalise (ReplayGain) : non / morceau / album\n"
               "volume_egalise = %s\n"
               "# Mode soiree (stick C vers le haut) : pulsation / barres / tunnel\n"
               "soiree = %s\n"
               "# Disposition du lecteur (stick C vers le bas) : classique / pochette / liste / minimal\n"
               "disposition = %s\n"
               "# Elements du lecteur : pochette, paroles, boutons, infos, horloge\n"
               "elements = %s\n"
               "# Onglets caches : radio, web, retro, podcasts, photos, fichiers, infos, telephone\n"
               "onglets_caches = %s\n"
               "# Onglet INFOS : ville de la meteo, flux RSS des actualites\n"
               "ville = %s\nactualites = %s\n"
               "# Reveil : heure (ex. 07:30) ou non ; reveil_joue = 0 (ma musique) ou numero de la radio\n"
               "reveil = %s\nreveil_joue = %d\n"
               "# Telecommande depuis le telephone lancee au demarrage : oui / non\n"
               "telephone = %s\n"
               "# Lecture : aleatoire = oui / non ; repetition = non / tout / un\n"
               "aleatoire = %s\nrepetition = %s\n"
               "# Liste MUSIQUE : tous / dossiers / artistes / albums\n"
               "affichage = %s\n"
               "volume = %d\n"
               "# Theme : indigo / ocean / emeraude / rubis / ambre / doux / graphite / lavande / foret / crepuscule\n"
               "theme = %s\n"
               "# Economiseur d'ecran apres N minutes sans toucher la manette (0 = jamais)\n"
               "economiseur = %d\n"
               "# Style de l'economiseur : spectre / pochette / horloge / etoiles / photos / noir\n"
               "# (photos : mets tes images .jpg / .png dans sd:/ecran, 1920x1080 maximum)\n"
               "economiseur_style = %s\n"
               "# Video : auto (480p si cable composante) / 480i / 480p\n"
               "video = %s\n"
               "# Type de tele : plat / cathodique (cathodique = 480i et 4:3)\n"
               "tele = %s\n"
               "# Langue du contenu (podcasts, radios du pays, sites web, meteo) :\n"
               "# auto (celle de la console) / fr / en / de / es / it / nl\n"
               "langue = %s\n"
               "# Langue de l'interface / interface language : auto / fr / en\n"
               "interface = %s\n"
               "# Style de l'interface : indigo / moderne ; affichage des jeux : grille / carrousel / liste / mur / vitrine / etagere\n"
               "style = %s\njeux_affichage = %s\n"
               "# Taille de l'image (80 a 100 %%) : a reduire si les bords sont coupes sur la tele\n"
               "ecran_taille = %d\n"
               "# Hauteur de l'image (80 a 100 %%) et position verticale (-24 a 24, positif = plus bas)\n"
               "ecran_hauteur = %d\necran_position = %d\n"
               "# Format de la tele : 4:3 ou 16:9 (16:9 : regle aussi la tele en mode large / plein ecran)\n"
               "ecran_format = %s\n"
               "# Reprendre le dernier morceau au demarrage : oui / non\n"
               "reprise = %s\nreprise_fichier = %s\nreprise_position = %d\n",
            covers_auto ? "oui" : "non", lyrics_auto ? "oui" : "non", eq_keys[eq_preset], eq_bass, eq_mid, eq_treble, xf_sec,
            rg_mode == 2 ? "album" : rg_mode == 1 ? "morceau" : "non", party_keys[party_style],
            layout_keys[layout_tab[TAB_MUSIC]], els, hid, city, news_url, alarm_txt, alarm_src, phone_auto ? "oui" : "non", shuffle_on ? "oui" : "non", rep[repeat_mode], aff[view_mode],
            volume_pct, theme_keys[theme_id], saver_min, saver_keys[saver_style], vid[video_pref], tv_crt ? "cathodique" : "plat",
            lang_pref >= 0 ? lang_keys[lang_pref] : "auto", ui_lang == 1 ? "en" : ui_lang == 0 ? "fr" : "auto", skin_keys[ui_skin >= 0 && ui_skin <= 3 ? ui_skin : 0], glview_keys[gl_view >= 0 && gl_view < 6 ? gl_view : 0], scr_pct, scr_hpct, scr_vpos, scr_wide ? "16:9" : "4:3", resume_on ? "oui" : "non",
            resume_path, resume_pos);
    fprintf(f, "# Reveil, jours : 0 = tous les jours, 1 = du lundi au vendredi, 2 = samedi et dimanche\nreveil_jours = %d\n", alarm_days);
    fprintf(f, "# Style du spectre : led / barres / miroir / courbe / spectrogramme / oscilloscope / cercle\nspectre = %s\n", viz_keys[viz_style]);
    fprintf(f, "# Couleurs du spectrogramme : feu / violet / arc-en-ciel / ocean / vert / gris\nspectrogramme_couleurs = %s\n", sgp_keys[sg_pal_id]);
    fprintf(f, "# Disposition des onglets RADIO / RETRO / PODCASTS (classique / pochette / minimal / spectre)\n"
               "disposition_radio = %s\ndisposition_retro = %s\ndisposition_podcasts = %s\n",
            layout_keys[layout_tab[TAB_RADIO]], layout_keys[layout_tab[TAB_RETRO]], layout_keys[layout_tab[TAB_PODCAST]]);
    fprintf(f, "# Paroles dans le lecteur : spectre / paroles / les_deux ; eclair sur les basses (mode soiree) : partout / sauf_spectrogramme / jamais\n"
               "paroles_affichage = %s\neclair_basses = %s\nrythme_decalage = %d\n", lyr_keys[lyr_show], flash_keys[flash_mode], rg_offset);
    fprintf(f, "# Jeux GameCube : tri (az / recents / taille / region) ; animations de JEUX / APPS (oui / non)\n"
               "jeux_tri = %s\njeux_animations = %s\n", gl_sort_keys[gl_sort & 3], gl_anim ? "oui" : "non");
    fprintf(f, "# Pulse (X) : A+Z+START = retour a GC Station (oui / non)\nlanceur_maison_retour = %s\n", nat_igr ? "oui" : "non");
    fprintf(f, "# Pulse (X) : carte memoire virtuelle du slot A (gc-mp3/resident/carte-A.raw)\nresident_carte_virtuelle = %s\n", nat_card ? "oui" : "non");
    fprintf(f, "# Ordre des onglets\nordre_onglets = ");
    for (int i = 0; i < TAB_COUNT; i++) fprintf(f, "%s%s", i ? "," : "", tab_keys[tab_order[i]]);
    fprintf(f, "\n");
    fprintf(f, "# Mode veilleuse : luminosite 1 a 5 ; radio lancee si rien ne joue : oui / non\n"
               "veilleuse_luminosite = %d\nveilleuse_radio = %s\n"
               "# Mise a jour : verification au demarrage (oui / non), depot GitHub,\n"
               "# fichier .dol a remplacer (vide = trouve tout seul)\n"
               "maj_auto = %s\nmaj_depot = %s\nmaj_fichier = %s\n",
            night_lum, night_radio ? "oui" : "non", upd_auto ? "oui" : "non", upd_repo, upd_file);
    fprintf(f, "# La manette vibre sur les basses (mode soiree) et dans les jeux : oui / non\n"
               "vibrations = %s\n"
               "# Nouveaux episodes des podcasts telecharges tout seuls dans sd:/gc-mp3/podcasts : oui / non\n"
               "podcasts_auto = %s\n"
               "# Fond d'ecran : chemin d'une photo (PHOTOS, plein ecran, X) ou non\n"
               "fond_ecran = %s\nfond_ecran_photo = %s\n"
               "# Decor de saison (Halloween, Noel, ete) selon la date : oui / non\n"
               "decor_saison = %s\n",
            rumble_on ? "oui" : "non", pod_auto ? "oui" : "non", wall_on && wall_path[0] ? wall_path : "non", wall_path, season_on ? "oui" : "non");
    fprintf(f, "# La derniere radio ecoutee reprend au demarrage : oui / non\nradio_reprise = %s\nradio_derniere = %s\nradio_derniere_nom = %s\n",
            radio_resume ? "oui" : "non", radio_last_url, radio_last_name);
    fprintf(f, "# Morceaux d'un meme album (dossier) enchaines sans blanc : oui / non\nenchainement_sans_blanc = %s\n", gapless_on ? "oui" : "non");
    fprintf(f, "# Egaliseur 5 bandes (v73) : bas-mediums (350 Hz) et presence (4 kHz), en dB\nbas_mediums = %d\npresence = %d\n", eq_lowmid, eq_pres);
    x73_save(f);
    nfs_save(f);
    fclose(f);
}

/* memorise ou on en est (enregistre au plus toutes les 30 s, et en quittant) */
static void resume_note(int force) {
    static u64 last = 0;
    if (src == SRC_FILE && playing && cur_track >= 0) {
        snprintf(resume_path, sizeof(resume_path), "%s", names[cur_track]);
        resume_pos = (int)(consumed / 48000u);
    }
    if (force || (resume_on && now_ms() - last > 30000 && now_ms() - last_input_ms > 1500 && src == SRC_FILE && playing)) {   /* (v45 : pas pendant qu'on navigue) */
        last = now_ms();
        save_settings();
    }
}

/* se place a 'sec' secondes dans le morceau en cours (position approchee) */
static void track_seek(int sec) {
    if (!ffp || cur_track < 0 || sec <= 0) return;
    mp3_skip = 0; mp3_left = -1; rgm_on = 0;             /* (v72 : mesure de sonie incomplete : abandonnee) */
    int d = file_duration(names[cur_track]);
    if (d <= 0 || sec >= d - 2) return;
    if (file_fmt != FMT_MP3) {
        if (!other_seek(sec, d)) return;
        f_head = f_tail = f_count = 0;
        xf_left = 0;
        rpos = 0.0; prevL = prevR = 0;
        consumed = (u32)sec * 48000u;
        return;
    }
    long off = f_audio + (long)((double)(fsize - f_audio) * sec / d);
    if (fseek(ffp, off, SEEK_SET) != 0) return;
    fb_start = fb_end = 0;
    f_file_eof = 0;
    mp3dec_init(&dec);
    f_head = f_tail = f_count = 0;
    rpos = 0.0; prevL = prevR = 0;
    consumed = (u32)sec * 48000u;
}

static void resume_start(void) {
    if (!resume_on || !resume_path[0]) return;
    for (int k = 0; k < nfiles; k++) {
        if (strcmp(names[k], resume_path) != 0) continue;
        lib_rebuild();
        if (view_mode == VIEW_FOLDERS) {                    /* ouvrir le dossier du morceau */
            int g = grp_of[k];
            if (g >= 0) { cur_group = g; lib_rebuild(); }
        }
        queue_build(k);
        if (start_track(k)) {
            track_seek(resume_pos);
            select_track_row(k);
        }
        return;
    }
}




// ---------- GX : initialisation (sequence des exemples officiels libogc) ----------
static GXTexObj font_texobj;
static GXTexObj cover_texobj;
static int gx_mode = -1;                 // 0 = couleurs, 1 = texte (police), 2 = pochette, 3 = image web
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
    for (;;) {
        if (gx_done_flag) return 1;
        if (ticks_to_millisecs(gettime() - t0) > timeout_ms) {
            /* le fil principal a pu etre mis de cote longtemps (reseau tres
               charge, envoi depuis le telephone) : on reverifie avant de conclure */
            if (gx_done_flag) return 1;
            u8 overhi = 0, underlow = 0, readidle = 0, cmdidle = 0, brkpt = 0;
            GX_GetGPStatus(&overhi, &underlow, &readidle, &cmdidle, &brkpt);
            if (readidle && cmdidle) { crumb("GX : fin de dessin non signalee (GP au repos)"); return 1; }
            return 0;
        }
    }
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

/* images du navigateur : couleur et transparence de la texture (carte 2) */
static void gx_set_image(GXTexObj *obj) {
    if (gx_mode != 3) {
        gx_mode = 3;
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP2, GX_COLOR0A0);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    }
    GX_LoadTexObj(obj, GX_TEXMAP2);
}

static void gx_init(void) {
    Mtx44 projection;
    Mtx identity;
    GXColor background = { 0, 0, 0, 255 };            /* (bords quand l'image est reduite) */

    say("   GX_Init... %s", "");
    gx_fifo = memalign(32, GX_FIFO_SIZE);
    if (gx_fifo == NULL) { say("%s\n   ERREUR : memoire insuffisante pour GX\n", "ECHEC"); while (1) VIDEO_WaitVSync(); }
    memset(gx_fifo, 0, GX_FIFO_SIZE);
    /* La FIFO est ecrite par le "write gather pipe", hors cache : on vide le cache
       pour qu'aucune ligne restee du memset n'ecrase plus tard des commandes GX. */
    DCFlushRange(gx_fifo, GX_FIFO_SIZE);
    GX_AbortFrame();                                    /* v108 : processeur graphique remis a zero AVANT GX_Init (retour d'un jeu,
                                                           A+Z+START : il pouvait etre arrete au milieu d'une commande du jeu) */
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

/* Taille d'ecran au-dela de 100 % : la sortie video etale les 640 points de
   l'image sur une ligne plus large (jusqu'a 716 sur 720), ce qui reduit les
   bandes noires des cotes que laissent beaucoup de teles. */
static int scr_hboot = 0, scr_hlive = 0;    /* v34 : position horizontale (SRAM) au demarrage / changee depuis */
static void scr_vi_set(int apply) {
    int w = 640;
    if (scr_pct > 100) w = (640 * scr_pct / 100) & ~1;
    if (w > 716) w = 716;
    rmode->viWidth = (u16)w;
    int xo = (720 - w) / 2 + scr_hlive;
    if (xo < 0) xo = 0;
    if (xo > 720 - w) xo = 720 - w;
    rmode->viXOrigin = (u16)xo;
    if (apply) { VIDEO_Configure(rmode); VIDEO_Flush(); }
}
static void scr_vi(void) { if (rmode) scr_vi_set(1); }

static int init_video(void) {
    VIDEO_Init();
    PAD_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    if (rmode == NULL) return 0;
    scr_hboot = SYS_GetDisplayOffsetH();      /* v34 : position de la console (libogc l'applique deja) */
    /* 480p (progressif) : cable composante (ou GCVideo) ; B maintenu au demarrage = 480i */
    PAD_ScanPads();
    int hold_b = (PAD_ButtonsHeld(0) & PAD_BUTTON_B) != 0;
    if (!hold_b && (video_pref == VID_480P || (video_pref == VID_AUTO && VIDEO_HaveComponentCable()))) {
        u32 tv = VIDEO_GetCurrentTvMode();
        rmode = (tv == VI_PAL || tv == VI_EURGB60) ? &TVEurgb60Hz480Prog : &TVNtsc480Prog;
    } else if ((hold_b || video_pref == VID_480I) && (rmode->viTVMode & 3) == VI_PROGRESSIVE) {
        /* v22 : 480i demande, mais la console (reglage progressif garde par l'IPL
           ou Swiss) proposait le 480p : on force l'entrelace (PVM, teles cathodiques) */
        u32 tv = VIDEO_GetCurrentTvMode();
        rmode = tv == VI_PAL ? &TVPal528IntDf : tv == VI_EURGB60 ? &TVEurgb60Hz480IntDf : tv == VI_MPAL ? &TVMpal480IntDf : &TVNtsc480IntDf;
    }
    vid_active = (rmode->viTVMode & 3) == VI_PROGRESSIVE ? VID_480P : VID_480I;

    static GXRModeObj vmode;                 /* copie modifiable (largeur de sortie, voir scr_vi) */
    vmode = *rmode;
    rmode = &vmode;
    scr_vi_set(0);
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
    printf("\n\n   GC Station\n\n   Video OK\n   Initialisation de GX...\n");
    return 1;
}

/* v85 : la musique se decode pendant l'attente de l'ecran (le temps libre de chaque image),
   au lieu de passer avant le dessin : l'image n'attend plus le decodeur. */
static u32 frame_t0_us = 0;                     /* debut de l'image en cours (boucle principale) */
static int music_idle_decode(void);
/* Copie l'image GX vers le framebuffer inactif et l'affiche. */
static int present_nowait = 0;                  /* v72 : ecran de chargement : pas d'attente de l'ecran (le travail continue) */
static void present_frame(void) {
    int next_fb = 1 - displayed_fb;
    /* (v19 : une seule attente du processeur graphique par image : la copie
       vers l'ecran passe apres le dessin dans la file de commandes) */
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(xfb[next_fb], GX_TRUE);
    u32 pg0 = perf_on ? perf_us() : 0;
    if (!gx_finish(1500)) gx_fatal("copie de l'image vers l'ecran");
    VIDEO_SetNextFramebuffer(xfb[next_fb]);
    VIDEO_Flush();
    u32 pw0 = perf_on ? perf_us() : 0;
    if (perf_on) pfx_cur[PH_GPU] += pw0 - pg0;
    /* attente de l'image suivante, en continuant a lire le reseau */
    u32 rc = retrace_count;
    u64 t0 = now_ms();
    while (!present_nowait && retrace_count == rc && now_ms() - t0 < 40) {
        stream_recv();
        service_audio();
        if (music_idle_decode()) continue;               /* v85 : (s'il reste le temps d'un bloc avant l'image suivante) */
        usleep(500);
    }
    if (perf_on) pfx_cur[PH_WAIT] += perf_us() - pw0;
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
    GX_Position2f32(UX(x), y);
    c = th_col(c);
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
    rx /= ux_k;                                          /* (16:9 : reste rond) */
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
        vtx(cx + r1 * co / ux_k, cy + r1 * si, c);
        vtx(cx + r0 * co / ux_k, cy + r0 * si, c);
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
    f32 rx = r / ux_k;                                   /* (16:9 : coins ronds) */
    if (rx > w / 2) rx = w / 2;
    int n = 0;
    /* haut droit, bas droit, bas gauche, haut gauche (sens horaire a l'ecran) */
    for (int s = 0; s < 6; s++) { px[n] = x + w - rx + rx * cs[5 - s]; py[n] = y + r - r * cs[s];     n++; }
    for (int s = 0; s < 6; s++) { px[n] = x + w - rx + rx * cs[s];     py[n] = y + h - r + r * cs[5 - s]; n++; }
    for (int s = 0; s < 6; s++) { px[n] = x + rx - rx * cs[5 - s];     py[n] = y + h - r + r * cs[s]; n++; }
    for (int s = 0; s < 6; s++) { px[n] = x + rx - rx * cs[s];         py[n] = y + r - r * cs[5 - s]; n++; }
}

static void nv_sel_draw(f32 x, f32 y, f32 w, f32 h, f32 r);   /* (ui_nova.c) */
static void gx_rrect_fill(f32 x, f32 y, f32 w, f32 h, f32 r, col_t top, col_t bot) {
    /* v101 : Nouvelle interface : toutes les selections (meme degrade violet partout) glissent d'une
       ligne / d'une case a l'autre, en couleur d'accent */
    if (ui_skin >= 2 && !nv_raw && top.r == 116 && top.g == 46 && top.b == 236 && bot.r == 74 && bot.g == 22 && bot.b == 172) { nv_sel_draw(x, y, w, h, r); return; }
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

static f32 txt_scale = 1.0f;                 /* agrandissement du texte (mode soiree) */
static int text_width_px(int f, const char *s, int sp) {      /* largeur reelle a l'ecran */
    int w = 0, n = 0;
    s = tr(s);
    while (*s) {
        int cp = next_cp(&s);
        w += font_glyphs[f][glyph_index(cp)].adv + sp;
        n++;
    }
    w = n ? w - sp : 0;
    return txt_scale == 1.0f ? w : (int)(w * txt_scale + 0.5f);
}

/* largeur dans le repere de l'interface (16:9 : le texte n'est pas etire,
   il occupe donc moins de place que ses positions) */
static int text_width(int f, const char *s, int sp) {
    int w = text_width_px(f, s, sp);
    return ux_k == 1.0f ? w : (int)(w / ux_k + 0.5f);
}

/* Ecrit s avec sa ligne de base en y = base (degrade vertical top -> bot).
   sp = espacement supplementaire entre lettres. Retourne la largeur. */
/* x deja converti en position ecran */
static int gx_text_raw(int f, f32 x, f32 base, const char *s, col_t top, col_t bot, int sp) {
    int count = 0;
    s = tr(s);
    for (const char *p = s; *p;) {
        int cp = next_cp(&p);
        if (font_glyphs[f][glyph_index(cp)].w) count++;
    }
    int width = text_width_px(f, s, sp);
    if (count == 0) return width;

    gx_set_text();
    GX_Begin(GX_QUADS, GX_VTXFMT0, count * 4);
    f32 pen = 0, sc = txt_scale;
    f32 ty = -(f32)font_ascent[f];
    f32 fh = (f32)font_height[f];
    for (const char *p = s; *p;) {
        int cp = next_cp(&p);
        const fglyph_t *g = &font_glyphs[f][glyph_index(cp)];
        if (g->w) {
            /* un texel transparent de plus autour de la lettre : les traits fins
               (i, l, barres) ne disparaissent plus quand la lettre tombe entre
               deux pixels ou quand l'image est reduite (taille d'ecran, 16:9) */
            f32 x0 = x + (pen + g->xoff - 1) * sc, y0 = base + (ty + g->yoff - 1) * sc;
            f32 x1 = x0 + (g->w + 2) * sc,         y1 = y0 + (g->h + 2) * sc;
            f32 u0 = (f32)(g->x - 1) / FONT_TEX_W, v0 = (f32)(g->y - 1) / FONT_TEX_H;
            f32 u1 = (f32)(g->x + g->w + 1) / FONT_TEX_W, v1 = (f32)(g->y + g->h + 1) / FONT_TEX_H;
            col_t ct = th_col(lerp_c(top, bot, (f32)g->yoff / fh));
            col_t cb = th_col(lerp_c(top, bot, (f32)(g->yoff + g->h) / fh));
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
static int gx_text(int f, f32 x, f32 base, const char *s, col_t top, col_t bot, int sp) {
    int w = gx_text_raw(f, UX(x), base, s, top, bot, sp);
    return ux_k == 1.0f ? w : (int)(w / ux_k + 0.5f);          /* (largeur dans le repere de l'interface) */
}
static int gx_text1(int f, f32 x, f32 base, const char *s, col_t c) {
    return gx_text(f, x, base, s, c, c, 0);
}
static void gx_text_r(int f, f32 xr, f32 base, const char *s, col_t c) {
    gx_text_raw(f, UX(xr) - text_width_px(f, s, 0), base, s, c, c, 0);
}
static void gx_text_c(int f, f32 xc, f32 base, const char *s, col_t c) {
    gx_text_raw(f, UX(xc) - text_width_px(f, s, 0) / 2, base, s, c, c, 0);
}

/* Copie s dans out, tronque avec "..." pour tenir dans maxw pixels. */
static void fit_text(int f, const char *s, int maxw, char *out, int outsz) {
    s = tr(s);
    maxw = (int)(maxw * ux_k);                           /* (16:9 : la place reelle) */
    if (text_width_px(f, s, 0) <= maxw) { snprintf(out, outsz, "%s", s); return; }
    int dots = text_width_px(f, "...", 0);
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

/* v18 (fun_extra.c) */
static int fun_mode;
static void draw_fun(void);
static int wall_draw(void);
static int wall_active(void);
static void wake_start(void);
static void party_rumble(float strength);
static int rb_mode;
static void podauto_kick(void);
static void wall_reload(void);
static void rumble(int ch, int ms);
static void games_open(void);
static void titles_open(void);
static void radio_title_keep(void);
static void lib_note(const char *m);
static void wall_set(const char *path);
/* ---- Decor de saison (v21) : Halloween, Noel, ete, selon la date ---- */
enum { SEASON_NONE, SEASON_HALLOWEEN, SEASON_NOEL, SEASON_ETE };
static int season_test = 0;                 /* (essais : saison imposee) */
static int season_now(void) {
    if (!season_on) return SEASON_NONE;
    if (season_test) return season_test;
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    if (!tm || tm->tm_year < 100) return SEASON_NONE;
    int m = tm->tm_mon + 1, d = tm->tm_mday;
    if ((m == 10 && d >= 15) || (m == 11 && d <= 2)) return SEASON_HALLOWEEN;
    if (m == 12 || (m == 1 && d <= 6)) return SEASON_NOEL;
    if ((m == 6 && d >= 21) || m == 7 || m == 8) return SEASON_ETE;
    return SEASON_NONE;
}
static void season_pumpkin(f32 cx, f32 cy, f32 r, f32 glow) {
    gx_circle(cx, cy, r * 1.8f, RGBA(255, 140, 30, (u8)(40 + glow * 60)), RGBA(255, 140, 30, 0));
    gx_ellipse(cx - r * 0.45f, cy, r * 0.62f, r * 0.9f, RGBA(255, 150, 40, 255), RGBA(200, 90, 10, 255));
    gx_ellipse(cx + r * 0.45f, cy, r * 0.62f, r * 0.9f, RGBA(255, 150, 40, 255), RGBA(200, 90, 10, 255));
    gx_ellipse(cx, cy, r * 0.7f, r * 0.95f, RGBA(255, 170, 60, 255), RGBA(220, 100, 20, 255));
    gx_rect(cx - r * 0.1f, cy - r * 1.2f, r * 0.2f, r * 0.35f, RGBA(70, 120, 40, 255));
    col_t f = RGBA(255, 240, (u8)(120 + glow * 100), 255);
    gx_tri(cx - r * 0.5f, cy - r * 0.15f, cx - r * 0.2f, cy - r * 0.15f, cx - r * 0.35f, cy - r * 0.45f, f);
    gx_tri(cx + r * 0.2f, cy - r * 0.15f, cx + r * 0.5f, cy - r * 0.15f, cx + r * 0.35f, cy - r * 0.45f, f);
    gx_poly4(cx - r * 0.5f, cy + r * 0.2f, cx + r * 0.5f, cy + r * 0.2f, cx + r * 0.3f, cy + r * 0.5f, cx - r * 0.3f, cy + r * 0.5f, f, f, f, f);
}
static void season_bat(f32 x, f32 y, f32 s, f32 flap) {
    gx_circle(x, y, 12 * s, RGBA(255, 140, 40, 90), RGBA(255, 140, 40, 0));           /* (lueur orange : visible sur le fond sombre) */
    col_t c = RGBA(25, 12, 35, 240);
    f32 w = 14 * s, up = flap * 6 * s;
    gx_ellipse(x, y, 3.5f * s, 4.5f * s, c, c);
    gx_tri(x - 2 * s, y - 1 * s, x - w, y - up, x - w * 0.6f, y + 4 * s, c);
    gx_tri(x + 2 * s, y - 1 * s, x + w, y - up, x + w * 0.6f, y + 4 * s, c);
}
/* dessine par-dessus l'interface (petit, pour ne rien cacher d'important) */
static void season_overlay(void) {
    int sn = season_now();
    if (!sn) return;
    f32 t = (f32)(now_ms() % 600000) / 1000.0f;
    if (sn == SEASON_HALLOWEEN) {
        for (int i = 0; i < 4; i++) {                              /* chauves-souris dans le haut */
            f32 sp = 22 + i * 9, x = fmodf(t * sp + i * 173, 760) - 60;
            f32 y = 16 + i * 11 + 8 * sinf(t * 1.3f + i * 2);
            season_bat(x, y, 0.8f + 0.15f * (i % 2), sinf(t * 9 + i));
        }
        f32 g = 0.5f + 0.5f * sinf(t * 2.0f);
        f32 px = ui_skin >= 2 ? -100 : ui_skin ? 140 : 345, py = ui_skin ? 24 : 36, pr = ui_skin ? 10 : 15;   /* (Moderne : a cote du nom ; Nouvelle : pas en haut) */
        gx_circle(px, py, pr * 2.1f, RGBA(255, 120, 30, (u8)(50 + g * 50)), RGBA(255, 120, 30, 0));   /* citrouille dans le bandeau du haut */
        season_pumpkin(px, py, pr, g);
        season_pumpkin(618, 466, 9, 1 - g);
        season_pumpkin(22, 466, 8, g);
    } else if (sn == SEASON_NOEL) {
        static const u8 lc[4][3] = { { 255, 60, 60 }, { 60, 220, 90 }, { 255, 210, 60 }, { 90, 160, 255 } };
        for (int i = 0; i < 24; i++) {                              /* guirlande en haut */
            f32 x = 10 + i * 27, y = 4 + 3 * sinf(i * 0.9f);
            int on = ((int)(t * 2) + i) % 3 != 0;
            const u8 *c = lc[i % 4];
            if (on) gx_circle_glow(x, y, 4, RGBA(c[0], c[1], c[2], 120), 3);
            gx_circle(x, y, 2.5f, RGBA(c[0], c[1], c[2], on ? 255 : 90), RGBA(c[0], c[1], c[2], on ? 255 : 90));
        }
        for (int i = 0; i < 46; i++) {                              /* flocons */
            f32 sp = 14 + (i * 7) % 20, x0 = (f32)((i * 97) % 640);
            f32 y = fmodf(t * sp + i * 53, 520) - 20, x = x0 + 12 * sinf(t * 0.7f + i);
            f32 r = 1.2f + (i % 3) * 0.7f;
            gx_circle(x, y, r, RGBA(255, 255, 255, 170), RGBA(220, 235, 255, 110));
        }
    } else {
        f32 p = 0.5f + 0.5f * sinf(t * 0.5f);                      /* soleil d'ete dans le coin */
        gx_circle(345, 34, 40 + p * 6, RGBA(255, 210, 90, 80), RGBA(255, 210, 90, 0));         /* soleil dans le bandeau du haut */
        gx_circle(345, 34, 17, RGBA(255, 245, 170, 240), RGBA(255, 190, 60, 200));
        for (int k = 0; k < 10; k++) {
            f32 a = k * 0.628f + t * 0.3f, r0 = 21, r1 = 28 + 3 * p;
            gx_poly4(345 + cosf(a - 0.08f) * r0, 34 + sinf(a - 0.08f) * r0, 345 + cosf(a + 0.08f) * r0, 34 + sinf(a + 0.08f) * r0,
                     345 + cosf(a) * r1, 34 + sinf(a) * r1, 345 + cosf(a) * r1, 34 + sinf(a) * r1,
                     RGBA(255, 220, 100, 220), RGBA(255, 220, 100, 220), RGBA(255, 200, 80, 120), RGBA(255, 200, 80, 120));
        }
        for (int i = 0; i < 12; i++) {
            f32 x = fmodf(t * (8 + i % 4 * 3) + i * 131, 680) - 20, y = 470 - fmodf(t * (5 + i % 3 * 2) + i * 47, 480);
            gx_circle(x, y, 1.5f, RGBA(255, 240, 180, 120), RGBA(255, 240, 180, 0));
        }
    }
}

static void draw_background(void) {
    if (wall_draw()) return;
    const f32 L = 0, R = 640;
    if (ui_skin == 3) { au_background(); return; }                    /* v102 : Aurore */
    if (ui_skin == 2) { nv_background(); return; }                    /* v101 : Nouvelle */
    if (ui_skin) {                                                    /* Moderne : gris uni */
        gx_quad4(L, 0, R, 480, RGBA(45, 45, 48, 255), RGBA(45, 45, 48, 255), RGBA(33, 33, 36, 255), RGBA(33, 33, 36, 255));
        return;
    }
    gx_quad4(L, 0, R, 480, RGBA(7, 5, 26, 255), RGBA(13, 8, 44, 255),
             RGBA(9, 6, 34, 255), RGBA(18, 8, 60, 255));
    /* halos violets diffus */
    gx_circle(L + 30, 470, 260, RGBA(70, 30, 170, 70), RGBA(70, 30, 170, 0));
    gx_circle(R - 30, 40, 230, RGBA(60, 40, 190, 60), RGBA(60, 40, 190, 0));
    /* rubans lumineux en bas a gauche */
    gx_poly4(L, 420, L + 120, 380, L + 120, 386, L, 430, RGBA(110, 60, 230, 0), RGBA(110, 60, 230, 50),
             RGBA(110, 60, 230, 50), RGBA(110, 60, 230, 0));
    gx_poly4(L, 452, L + 90, 430, L + 90, 434, L, 458, RGBA(110, 60, 230, 0), RGBA(110, 60, 230, 40),
             RGBA(110, 60, 230, 40), RGBA(110, 60, 230, 0));
}

static void draw_logo_cube(f32 cx, f32 cy, f32 R) {
    uxsave_t uz = ux_native(cx);
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
    ux_restore(uz);
}

static void draw_console_corner(void) {
    /* coin de console GameCube stylise (haut droite) */
    const f32 d = 0;
    gx_poly4(d + 470, 10, d + 556, 0, d + 640, 12, d + 566, 64, RGBA(74, 50, 196, 255), RGBA(60, 40, 176, 255),
             RGBA(36, 24, 110, 255), RGBA(46, 30, 140, 255));
    gx_poly4(d + 470, 10, d + 566, 64, d + 566, 96, d + 472, 36, RGBA(36, 22, 116, 255), RGBA(28, 16, 92, 255),
             RGBA(16, 10, 56, 255), RGBA(22, 14, 74, 255));
    gx_poly4(d + 566, 64, d + 640, 12, d + 640, 46, d + 566, 96, RGBA(30, 18, 98, 255), RGBA(26, 16, 86, 255),
             RGBA(14, 8, 50, 255), RGBA(16, 10, 56, 255));
    gx_ellipse(d + 552, 26, 38, 15, RGBA(12, 8, 38, 255), RGBA(22, 14, 64, 255));
    gx_ring(d + 564, 52, 4, 6.5f, RGBA(210, 190, 255, 255));
}

static void draw_panel(f32 x, f32 y, f32 w, f32 h) {
    if (ui_skin == 3) { au_panel(x, y, w, h); return; }
    if (ui_skin == 2) { nv_panel(x, y, w, h); return; }
    if (ui_skin) {                                                    /* Moderne : carte plate */
        col_t t = RGBA(58, 58, 62, 236), b = RGBA(52, 52, 56, 236);
        if (wall_active()) { t.a = 190; b.a = 200; }
        gx_rrect_fill(x + 1, y + 3, w, h, 14, RGBA(0, 0, 0, 60), RGBA(0, 0, 0, 60));   /* ombre */
        gx_rrect_fill(x, y, w, h, 14, t, b);
        return;
    }
    gx_glow(x, y, w, h, 10, C_GLOW, 4);
    if (wall_active()) {                                   /* fond d'ecran : la photo se voit a travers */
        col_t t = C_PANEL_TOP, b = C_PANEL_BOT;
        t.a = 170; b.a = 190;
        gx_rrect_fill(x, y, w, h, 10, t, b);
    } else
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
/* Taille de l'ecran : l'image (repere 640x480) est reduite et centree, pour
   qu'elle rentre en entier sur les televiseurs qui coupent les bords. */
static f32 scr_sx = 1, scr_sy = 1, scr_ox = 0, scr_oy = 0;
static void scr_viewport(void) {
    if (scr_pct < 80) scr_pct = 80;
    if (scr_pct > SCR_MAX) scr_pct = SCR_MAX;
    if (scr_hpct < 80) scr_hpct = 80;
    if (scr_hpct > 100) scr_hpct = 100;
    if (scr_vpos < -24) scr_vpos = -24;
    if (scr_vpos > 24) scr_vpos = 24;
    f32 s = scr_pct > 100 ? 1.0f : scr_pct / 100.0f;          /* (au-dela : voir scr_vi) */
    f32 w = rmode->fbWidth * s, h = rmode->efbHeight * s * scr_hpct / 100.0f;
    scr_ox = (int)((rmode->fbWidth - w) / 2);
    scr_oy = (int)((rmode->efbHeight - h) / 2) + scr_vpos;
    if (scr_oy < 0) scr_oy = 0;
    if (scr_oy + h > rmode->efbHeight) scr_oy = rmode->efbHeight - h;
    /* 16:9 : la tele etire l'image ; le repere s'elargit (853 x 480) et
       l'interface (0..640) reste au centre, avec ses vraies proportions */
    scr_w = scr_wide ? 640.0f * 4.0f / 3.0f : 640.0f;
    scr_x0 = (640.0f - scr_w) / 2.0f;
    static int proj_wide = -1;
    if (proj_wide != scr_wide) {
        Mtx44 pm;
        guOrtho(pm, 0.0f, 480.0f, scr_x0, scr_x0 + scr_w, 0.0f, 1.0f);
        GX_LoadProjectionMtx(pm, GX_ORTHOGRAPHIC);
        proj_wide = scr_wide;
    }
    scr_sx = w / scr_w;
    scr_sy = h / 480.0f;
    ux_k = scr_w / 640.0f;
    ux_o = scr_x0;
    GX_SetViewport(scr_ox, scr_oy, w, h, 0.0f, 1.0f);
    GX_SetScissor((u32)scr_ox, (u32)scr_oy, (u32)(w + 0.5f), (u32)(h + 0.5f));
}
/* zone de dessin (coordonnees du repere 640x480) */
static void scr_scissor(f32 x, f32 y, f32 w, f32 h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    x = UX(x + nv_cdx) - scr_x0;                         /* (v101 : page qui glisse) */
    w *= ux_k;
    GX_SetScissor((u32)(scr_ox + x * scr_sx), (u32)(scr_oy + y * scr_sy), (u32)(w * scr_sx + 0.5f), (u32)(h * scr_sy + 0.5f));
}

/* Animations : la barre de selection glisse vers sa place (au lieu de sauter) */
static f32 ui_dt = 1.0f / 60.0f;            /* duree de l'image precedente (s) */
static f32 smooth_to(f32 *v, f32 target) {
    if (*v < -1000.0f || fabsf(target - *v) > 220.0f) *v = target;   /* premier affichage, grand saut */
    else {
        f32 k = 1.0f - expf(-ui_dt * 22.0f);
        *v += (target - *v) * k;
        if (fabsf(target - *v) < 0.3f) *v = target;
    }
    return *v;
}
/* barre de selection d'une liste (onglets MUSIQUE, RADIO, PODCASTS, RETRO) */
static void draw_sel_bar(f32 *slot, f32 target_y) {
    f32 ry = smooth_to(slot, target_y);
    gx_glow(20, ry + 1, 244, 22, 5, RGBA(150, 80, 255, 120), 3);
    gx_rrect_fill(20, ry + 1, 244, 22, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
    gx_rrect_stroke(20, ry + 1, 244, 22, 5, 1.2f, RGBA(180, 104, 255, 255));
}

static void begin_frame(void) {
    scr_viewport();
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    draw_background();
}

// ---------- Interface GC-Radio ----------
static int sel_station = 0;
#define RS_N 3                                     /* v73 : lignes speciales en haut de la liste RADIO (recherche, populaires, historique) */
static char conn_msg[160];

static void cover_for_station(int idx) {
    static const char *exts[] = { ".jpg", ".jpeg", ".png" };
    char path[128];
    cover_ok = 0;
    cv_gen++;
    free(cv_pend);
    cv_pend = NULL;
    cv_key[0] = 0;
    if (!sd_ok) return;
    cv_defer = 1;
    for (int i = 0; i < 3 && !cover_ok; i++) {
        snprintf(path, sizeof(path), "%s/%s%s", LOGO_DIR, stations[idx].name, exts[i]);
        snprintf(cv_key, sizeof(cv_key), "logo|%s", path);
        if (cv_cache_read(cv_key)) { cover_ok = 1; break; }
        cover_ok = cover_from_file(path);
    }
    cv_defer = 0;
    if (cover_ok == 2) cover_ok = 0;
}

/* v28 : en-tete du style Moderne : nom, heure, et tous les onglets en haut */
static void draw_mini_badge(f32 cx, f32 cy, const char *k);
static int gx_text_r_w(int f, f32 xr, f32 base, const char *s, col_t c) { gx_text_r(f, xr, base, s, c); return text_width(f, s, 0); }
static void draw_header_modern(void) {
    col_t acc = RGBA(mod_acc[0], mod_acc[1], mod_acc[2], 255);
    draw_logo_cube(34, 25, 12);
    gx_text1(F_MED, 54, 31, "GC Station", RGBA(240, 240, 245, 255));
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    char b[40];
    f32 rx = 616;
    if (tm && EL(EL_CLOCK)) { snprintf(b, sizeof(b), "%02d:%02d", tm->tm_hour, tm->tm_min); rx -= gx_text_r_w(F_MED, rx, 31, b, RGBA(240, 240, 245, 255)) + 14; }
    if (net_phase == NET_DONE) {
        if (net_ok) snprintf(b, sizeof(b), "IP %s", ip_str); else snprintf(b, sizeof(b), "HORS LIGNE");
        rx -= gx_text_r_w(F_SMALL, rx, 30, b, net_ok ? RGBA(170, 170, 178, 255) : RGBA(255, 140, 90, 255)) + 14;
    }
    if (rec_fp) { gx_circle(rx - 4, 26, 4, RGBA(255, 70, 70, 255), RGBA(255, 70, 70, 255)); gx_text_r(F_SMALL, rx - 12, 30, "REC", RGBA(255, 120, 120, 255)); }
    /* onglets */
    int total = 0;
    FOR_TABS(t) if (tab_shown(t)) { total += text_width(F_SMALL, tab_names[t], 1) + 18; }
    f32 sc = total > 560 ? 560.0f / total : 1.0f;
    txt_scale = sc;
    total = 0;
    FOR_TABS(t) if (tab_shown(t)) total += text_width(F_SMALL, tab_names[t], 1) + (int)(18 * sc);
    f32 x = 320 - total / 2.0f + 14;
    draw_mini_badge(x - 16, 56, "Z");
    FOR_TABS(t) {
        if (!tab_shown(t)) continue;
        int w = text_width(F_SMALL, tab_names[t], 1) + (int)(18 * sc);
        int on = t == tab;
        gx_text(F_SMALL, x + 9 * sc, 61, tab_names[t], on ? RGBA(255, 255, 255, 255) : RGBA(150, 150, 158, 255),
                on ? RGBA(255, 255, 255, 255) : RGBA(150, 150, 158, 255), 1);
        if (on) gx_rrect_fill(x + 6 * sc, 65, w - 12 * sc, 3, 1.5f, acc, acc);
        gp_zone(x, 46, (f32)w, 22, NULL, t, GPZ_TAB);
        x += w;
    }
    txt_scale = 1.0f;
    gx_rect(22, 70, 596, 1, RGBA(80, 80, 86, 255));
}
#include "ui_nova.c"                            /* v101 : Nouvelle interface */
#include "ui_aurore.c"                          /* v102 : style Aurore */
#include "pulse_logo.c"                         /* v109 : logo de Pulse (le lanceur, avant Resident) */
static void draw_viz(f32 x, f32 base, f32 w, f32 h);
static void draw_header(void) {
    if (ui_skin == 3) { au_header(); return; }
    if (ui_skin == 2) { nv_header(); return; }
    if (ui_skin) { draw_header_modern(); return; }
    draw_logo_cube(54, 38, 23);
    txt_scale = 0.84f;                       /* v47 : GC STATION (plus long que GC-MP3) */
    gx_text(F_LOGO, 88, 46, "GC STATION", RGBA(240, 232, 255, 255), RGBA(150, 84, 255, 255), 0);
    txt_scale = 1.0f;
    gx_text(F_SMALL, 91, 63, "GAMECUBE MEDIA CENTER", RGBA(170, 150, 225, 255), RGBA(170, 150, 225, 255), 4);

    gx_hgrad(330, 46, 140, 1, RGBA(110, 70, 255, 0), RGBA(110, 70, 255, 200));
    if (viz_style != VZ_LED) draw_viz(390, 64, 86, 38);     /* v43 : le style choisi aussi en haut */
    else for (int i = 0; i < 28; i++) {
        f32 env = (i < 20) ? 0.15f + 0.85f * (f32)i / 19.0f : 1.0f - (f32)(i - 20) / 10.0f;
        f32 v = bar_val[(i * 2) % NBARS];
        f32 h = 2.0f + 22.0f * env * (0.30f + 0.70f * v);
        gx_rect(392.0f + i * 3.0f, 46.0f - h, 1.5f, 2.0f * h,
                RGBA(150 + i * 3, 90, 255, 90 + (int)(120 * env)));
    }
    if (rec_fp) {
        gx_circle(306, 62, 4, RGBA(255, 70, 70, 255), RGBA(255, 70, 70, 255));
        gx_text1(F_SMALL, 314, 66, "REC", RGBA(255, 120, 120, 255));
    }
    if (net_phase == NET_DONE && (net_ok || tab != TAB_MUSIC)) {
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
    if (ui_skin == 3) return au_draw_tabs(count);
    if (ui_skin == 2) return nv_draw_tabs(count);
    if (ui_skin) {                                                    /* Moderne : onglets deja en haut */
        if (count) gx_text_r(F_SMALL, IS_PLAYER_TAB(tab) ? 266 : 614, 99, count, RGBA(170, 170, 178, 255));
        gx_rect(25, 110, IS_PLAYER_TAB(tab) ? 241 : 590, 1, RGBA(80, 80, 86, 255));
        gx_text(F_SMALL, 26, 99, tab_names[tab], RGBA(240, 240, 245, 255), RGBA(240, 240, 245, 255), 1);
        return 26 + text_width(F_SMALL, tab_names[tab], 1) + 10;
    }
    f32 x = 20;
    int nshown = 0, pos = 0;
    FOR_TABS(t) if (tab_shown(t)) { if (t == tab) pos = nshown; nshown++; }
    for (int t = tab; t == tab; t++) {
        const char *const *names = tab_names;
        int w = text_width(F_SMALL, names[t], 1) + 14;
        int on = 1;
        if (on) {
            gx_glow(x, 86, (f32)w, 18, 9, RGBA(150, 80, 255, 110), 2);
            gx_rrect_fill(x, 86, (f32)w, 18, 9, RGBA(124, 54, 240, 255), RGBA(80, 26, 180, 255));
        }
        gx_rrect_stroke(x, 86, (f32)w, 18, 9, 1.2f, on ? RGBA(190, 120, 255, 255) : RGBA(90, 64, 170, 255));
        gx_text(F_SMALL, x + 7, 99, names[t], on ? RGBA(255, 255, 255, 255) : RGBA(150, 135, 210, 255),
                on ? RGBA(255, 255, 255, 255) : RGBA(150, 135, 210, 255), 1);
        x += w + 4;
    }
    gp_zone(18, 84, (f32)x - 14, 22, NULL, PAD_TRIGGER_Z, GPZ_KEY);   /* (curseur : onglet suivant) */
    /* rappel de la touche Z, et position parmi les onglets */
    gx_rrect_fill(x + 1, 88, 14, 14, 4, RGBA(110, 90, 190, 255), RGBA(70, 50, 140, 255));
    gx_text_c(F_SMALL, x + 8, 99, "Z", RGBA(255, 255, 255, 255));
    for (int i = 0; i < nshown && i < 9; i++)
        gx_circle(x + 24 + i * 7, 95, 2.2f, i == pos ? RGBA(230, 210, 255, 255) : RGBA(90, 70, 160, 255), i == pos ? RGBA(230, 210, 255, 255) : RGBA(90, 70, 160, 255));
    x += nshown * 7 + 6;
    if (count && x + 20 + text_width(F_SMALL, count, 0) <= 266)
        gx_text_r(F_SMALL, 266, 99, count, RGBA(205, 195, 245, 255));
    gx_rect(25, 110, IS_PLAYER_TAB(tab) ? 241 : 590, 1, RGBA(76, 52, 150, 200));
    return (int)x + 16;
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
        uxsave_t uz = ux_native(x + sz / 2);             /* (16:9 : pochette carree) */
        gx_set_cover();
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(UX(x), y);           GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(UX(x + sz), y);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(UX(x + sz), y + sz); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(UX(x), y + sz);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f);
        GX_End();
        gx_rrect_stroke(x, y, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 235));
        ux_restore(uz);
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
    s = tr(s);
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

#include "viz_styles.c"                       /* v39 : styles du spectre */
static void draw_player_radio(void) {
    char buf[200], l1[160], l2[160];
    draw_panel(289, 73, 341, 338);

    int live = (state == ST_PLAY);
    if (live) {
        int blink = (int)((now_ms() / 600) % 2);
        gx_circle(312, 94, 4, blink ? RGBA(255, 70, 100, 255) : RGBA(200, 40, 70, 255), RGBA(170, 30, 60, 255));
    }
    gx_text(F_SMALL, live ? 322 : 308, 98, pod_on ? "PODCAST" : "EN DIRECT", RGBA(140, 122, 210, 255), RGBA(140, 122, 210, 255), 3);
    if (pod_on) buf[0] = 0;
    else if (cur_station >= nstations) snprintf(buf, sizeof(buf), "ANNUAIRE");
    else snprintf(buf, sizeof(buf), "%02d / %02d", cur_station >= 0 ? cur_station + 1 : 0, nstations);
    gx_text_r(F_MED, 614, 99, buf, RGBA(165, 145, 235, 255));

    if (EL(EL_COVER)) draw_art_radio();

    /* nom de la station + titre en cours (ICY "Artiste - Titre") */
    if (cur_station >= 0 && state == ST_ERROR) {
        fit_text(F_LARGE, stations[cur_station].name, 160, buf, sizeof(buf));
        gx_text1(F_LARGE, 456, 132, buf, RGBA(255, 255, 255, 255));
        wrap2(F_MED, err_msg, 160, l1, l2, sizeof(l1));
        gx_text1(F_MED, 456, 152, l1, RGBA(255, 150, 80, 255));
        gx_text1(F_MED, 456, 170, l2, RGBA(255, 150, 80, 255));
    } else if (cur_station >= 0 && state != ST_IDLE && pod_on) {         /* podcast : titre sur deux lignes */
        char t1[200];
        text_display(stations[cur_station].name, t1, sizeof(t1));
        wrap2(F_MED, t1, 160, l1, l2, sizeof(l1));
        gx_text1(F_MED, 456, 128, l1, RGBA(255, 255, 255, 255));
        gx_text1(F_MED, 456, 148, l2, RGBA(255, 255, 255, 255));
        fit_text(F_SMALL, pod_open >= 0 && pod_open < npods ? pods[pod_open].name : "", 160, buf, sizeof(buf));
        gx_text1(F_SMALL, 456, 168, buf, RGBA(156, 116, 255, 255));
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
            snprintf(buf, sizeof(buf), "%s%s%d kbps  %d,%d kHz%s", hls_on ? "HLS  " : "",
                     stream_codec == 2 ? "OGG  " : stream_codec ? "AAC  " : "", stream_kbps, stream_hz / 1000,
                     (stream_hz % 1000) / 100, muted ? "  (muet)" : "");
        else if (stream_hz > 0)
            snprintf(buf, sizeof(buf), "%s%s  %d,%d kHz%s", hls_on ? "HLS  " : "",
                     stream_codec == 2 ? "OGG Vorbis" : stream_codec ? "AAC" : "MP3", stream_hz / 1000, (stream_hz % 1000) / 100,
                     muted ? "  (muet)" : "");
        else snprintf(buf, sizeof(buf), "En direct%s", muted ? "  (muet)" : "");
        break;
    case ST_ERROR:
        if (rt_at && src == SRC_RADIO) { u64 n = now_ms(); snprintf(buf, sizeof(buf), TR("Reconnexion dans %d s... (essai %d)"), rt_at > n ? (int)((rt_at - n + 999) / 1000) : 0, rt_n + 1); }
        else snprintf(buf, sizeof(buf), "A : réessayer");
        break;
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
    if (pod_on && (state == ST_PLAY || state == ST_BUFFER)) {      /* podcast : avancement de l'episode */
        int el = pod_pos_sec();
        f32 pf = pod_dur > 0 ? (f32)el / (f32)pod_dur : 0.0f;
        if (pf > 1.0f) pf = 1.0f;
        if (pf > 0.0f) gx_hgrad(px, py, pw * pf, 3, RGBA(120, 64, 255, 255), RGBA(180, 100, 255, 255));
        char tb[16], td[16], lb[64];
        fmt_listen(tb, el);
        fmt_listen(td, pod_dur);
        snprintf(lb, sizeof(lb), "%s / %s", tb, pod_dur > 0 ? td : "--:--");
        gx_text1(F_SMALL, 457, 222, lb, RGBA(170, 155, 225, 255));
        gx_text1(F_SMALL, 457, 238, "L / R : -15 s / +30 s", RGBA(130, 115, 200, 255));
    } else if (state == ST_PLAY || state == ST_BUFFER) {
        if (frac > 0.0f) gx_hgrad(px, py, pw * frac, 3, RGBA(120, 64, 255, 255), RGBA(180, 100, 255, 255));
        char tb[16], lb[64];
        fmt_listen(tb, (int)(consumed / 48000u));
        snprintf(lb, sizeof(lb), "Tampon %d,%d s", f_count / 48000, (f_count % 48000) / 4800);
        gx_text1(F_SMALL, 457, 222, lb, RGBA(130, 115, 200, 255));
        gx_text_r(F_SMALL, 613, 222, tb, RGBA(130, 115, 200, 255));
        if (now_title[0] && tab == TAB_RADIO && !rb_mode) gx_text1(F_SMALL, 457, 238, "X : garder ce titre", RGBA(255, 205, 110, 255));
    }

    /* spectre (v39 : plusieurs styles, START > Reglages > Style du spectre) */
    if (EL(EL_VIZ)) draw_viz(306, 295, 308, 48);

    /* boutons : station precedente, ecouter/arreter, station suivante, son */
    if (!EL(EL_BUTTONS)) return;
    gp_player_zones();
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
/* cadre de legende (en bas) ; v28 : plat en style Moderne */
static void legend_box(f32 x, f32 w) {
    if (ui_skin == 3) { au_legend_box(x, w); return; }
    if (ui_skin == 2) { nv_legend_box(x, w); return; }
    if (ui_skin) { gx_rrect_fill(x, 419, w, 39, 12, RGBA(52, 52, 56, 230), RGBA(52, 52, 56, 230)); return; }
    gx_rrect_fill(x, 419, w, 39, 9, C_PANEL_TOP, C_PANEL_BOT);
    gx_rrect_stroke(x, 419, w, 39, 9, 1.2f, RGBA(100, 56, 220, 255));
}
static void draw_legend(void) {
    const char *labels[5];
    labels[0] = "LIRE";
    labels[1] = (tab == TAB_MUSIC) ? "PAUSE" : "MUET";
    labels[2] = "STOP";
    labels[3] = "ONGLET";
    labels[4] = "MENU";
    static const f32 icon_w[5] = { 16, 16, 16, 16, 20 };
    const f32 x0 = 289, w = 341, pad = 10, gap = 4, cy = 438.5f;
    col_t tx = RGBA(208, 198, 240, 255);

    legend_box(x0, w);

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
static int cover_info(char *out, int cap);

static void draw_web(void);
static void draw_list_radio(void);
static void draw_legend_web(void);
static void draw_keyboard(void);
static int kb_is_open(void);

static void draw_menu(void);
static int ic_on;                               /* v32 : reglages d'Indigo (indigo_cfg.c) */
static void draw_indigo_cfg(void);
static void indigo_cfg_input(u32 down, u32 nav, int fire);
static void indigo_cfg_open(void);
static void jeux_cfg_boot(void);
static int tl_screen;                           /* v35 : outils (tools.c) */
static void tools_input(u32 down, u32 nav, int fire);
static void draw_tools(void);
static void ic_put(const char *key, const char *val);
static void draw_saver(void);
static int menu_open, saver_on;

static void draw_party(void);
static int party_on;
static void draw_header_clock(void);
static void draw_layout_cover(void);
static void draw_layout_minimal(void);
static void draw_layout_bigviz(void);
static void draw_layout_list(void);
static void draw_layout_toast(void);
static void draw_layout_hint(void);
static void draw_tab_strip(void);
static void draw_checklist(void);
static void draw_list_retro(void);
static void draw_list_podcast(void);
static void draw_tab_photo(void);
static void draw_tab_files(void);
static void draw_tab_infos(void);
static void draw_tab_phone(void);
static void draw_tab_games(void);
static void draw_tab_dl(void);
static void gp_draw(void);
static void gp_player_zones(void);
static void draw_night(void);
static void draw_update_notice(void);
static int nat_ld_on = 0;                       /* v64 : ecran de chargement de Resident (natif.c) */
#ifndef GEKKO
static void nat_sim_loading(void);                     /* v109 : simulateur : PCSIM_CHARGEMENT=1 montre l'ecran de chargement */
#endif
static void nat_draw_loading(void);
static void perf_overlay(void) {
    char b[160];
    snprintf(b, sizeof(b), "%u img/s  pire %u ms%s%s", (unsigned)pfx_fps, (unsigned)pfx_worst_ms,
             pfx_last_top < PH_N ? "  lent : " : "", pfx_last_top < PH_N ? pfx_names[pfx_last_top] : "");
    f32 w = (f32)text_width(F_SMALL, b, 1) + 16;
    gx_rrect_fill(320 - w / 2, 2, w, 18, 6, RGBA(0, 0, 0, 220), RGBA(0, 0, 0, 220));
    gx_text_c(F_SMALL, 320, 15, b, pfx_fps >= 58 ? RGBA(140, 255, 160, 255) : pfx_fps >= 45 ? RGBA(255, 220, 120, 255) : RGBA(255, 120, 120, 255));
}
static const char *perf_ver(void);
static void perf_report(void) {                      /* SD:/gc-mp3/fluidite.txt */
    if (!sd_ok) return;
    FILE *f = fopen(GCD "/fluidite.txt", "w");
    if (!f) return;
    fprintf(f, "GC Station %s - mesure de fluidite (une image ratee = plus de 20 ms ; tres lente = plus de 34 ms)\n\n", perf_ver());
    for (int t = 0; t < PF_TABS; t++) {
        if (!pfx_frames[t]) continue;
        fprintf(f, "[%s] %u images, %u ratees (%u%%), %u tres lentes, pire %u ms\n", t < TAB_COUNT ? tab_names[t] : "autre",
                (unsigned)pfx_frames[t], (unsigned)pfx_slow[t], (unsigned)(pfx_slow[t] * 100u / pfx_frames[t]), (unsigned)pfx_vslow[t], (unsigned)(pfx_worst[t] / 1000u));
        for (int i = 0; i < PH_N; i++) {
            u32 avg = (u32)(pfx_sum[t][i] / pfx_frames[t]);
            if (avg < 200 && pfx_max[t][i] < 5000 && !pfx_top[t][i]) continue;
            fprintf(f, "   %-22s moyenne %5.1f ms  pire %5.1f ms  cause de %u images ratees\n", pfx_names[i], avg / 1000.0f, pfx_max[t][i] / 1000.0f, (unsigned)pfx_top[t][i]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
}
static void perf_toggle(void) {
    if (perf_on) { perf_on = 0; perf_report(); return; }
    memset(pfx_frames, 0, sizeof(pfx_frames)); memset(pfx_slow, 0, sizeof(pfx_slow)); memset(pfx_vslow, 0, sizeof(pfx_vslow)); memset(pfx_worst, 0, sizeof(pfx_worst));
    memset(pfx_sum, 0, sizeof(pfx_sum)); memset(pfx_max, 0, sizeof(pfx_max)); memset(pfx_top, 0, sizeof(pfx_top));
    pfx_frame_t0 = 0; pfx_last_top = PH_N; perf_on = 1;
}
#ifndef GEKKO
void gcs_perf_sim(int on) { if (!on != !perf_on) perf_toggle(); }   /* (simulateur sur PC : PCSIM_PERF=1) */
#endif
static void draw(void) {
    {
        static u64 last = 0;
        u64 n = now_ms();
        ui_dt = last ? (n - last) / 1000.0f : 1.0f / 60.0f;
        if (ui_dt > 0.1f) ui_dt = 0.1f;
        last = n;
    }
    gp_frame_begin();
#ifndef GEKKO
    nat_sim_loading();
#endif
    if (nat_ld_on) { begin_frame(); nat_draw_loading(); service_audio(); present_frame(); return; }
    if (night_on) { draw_night(); return; }
    if (saver_on) { draw_saver(); return; }
    if (fun_mode && !menu_open && !kb_is_open()) { draw_fun(); return; }
    if (party_on && !menu_open) { draw_party(); return; }
    /* (pas pendant le defilement, et seulement s'il reste au moins 2 s de son
       d'avance : la lecture d'un fichier ne coupe plus la musique - v29) */
    dur_budget = now_ms() - last_input_ms > 250 && io_safe() ? 1 : 0;
    tag_budget = dur_budget;
    begin_frame();
    draw_header();
    draw_header_clock();
    nv_page_begin();                               /* v101 : Nouvelle : la page glisse en changeant d'onglet */
    if (menu_open || tl_screen || ic_on) {
        /* v42 : une fenetre plein ecran est ouverte (Reglages, outils...) : l'onglet derriere
           n'est pas redessine (cache par la fenetre) : beaucoup moins de travail par image */
    } else if (tab == TAB_WEB) {
        draw_web();
        draw_legend_web();
    } else if (IS_PLAYER_TAB(tab) && player_layout == L_COVER) {
        draw_layout_cover();
        if (kb_is_open()) draw_legend_web(); else draw_legend();
        draw_layout_hint();
    } else if (IS_PLAYER_TAB(tab) && player_layout == L_MINIMAL) {
        draw_layout_minimal();
    } else if (IS_PLAYER_TAB(tab) && player_layout == L_BIGVIZ) {
        draw_layout_bigviz();
    } else if (tab == TAB_MUSIC && player_layout == L_LIST) {
        draw_layout_list();
    } else if (IS_PLAYER_TAB(tab)) {
        if (tab == TAB_MUSIC) draw_list_music(sel_row);
        else if (tab == TAB_RADIO) draw_list_radio();
        else if (tab == TAB_RETRO) draw_list_retro();
        else draw_list_podcast();
        /* panneau de droite : ce qui joue, sinon l'onglet affiche */
        int view = (src != SRC_NONE) ? src : ((tab == TAB_RADIO || tab == TAB_PODCAST) ? SRC_RADIO : SRC_FILE);
        if (ui_skin == 3 && tab == TAB_MUSIC) au_player();   /* v103 : Aurore */
        else if (view == SRC_RADIO) draw_player_radio();
        else draw_player_music();
        if (kb_is_open()) draw_legend_web();
        else draw_legend();
    } else {
        switch (tab) {
        case TAB_PHOTO: draw_tab_photo(); break;
        case TAB_FILES: draw_tab_files(); break;
        case TAB_INFO:  draw_tab_infos(); break;
        case TAB_GAMES: draw_tab_games(); break;
        case TAB_DL:    draw_tab_dl(); break;
        default:        draw_tab_phone(); break;
        }
    }
    nv_page_end();
    draw_layout_toast();
    draw_crash_notice();
    draw_update_notice();
    draw_tab_strip();
    if (tl_screen) draw_tools();                   /* v35 : outils */
    if (ic_on) draw_indigo_cfg();                  /* v32 : reglages d'un jeu (par-dessus l'onglet) */
    if (menu_open) { au_menu_begin(); draw_menu(); au_menu_end(); }   /* v103 : Aurore : le menu s'ouvre en douceur */
    else au_menu_closed();
    if (kb_is_open()) draw_keyboard();             /* (v35 : par-dessus la page Reglages) */
    draw_checklist();
    if (!menu_open && !kb_is_open()) season_overlay();
    draw_actions();
    gp_draw();
    if (perf_on) perf_overlay();
    service_audio();
    present_frame();
}

static void ui_status(const char *text) {
    snprintf(conn_msg, sizeof(conn_msg), "%s", text);
    update_spectrum();
    draw();
}


// ---------- Interface : onglet MUSIQUE ----------
/* Nom a afficher : sans le numero de piste en tete du nom de fichier
   ("01. Stan.mp3", "01 - Stan.mp3", "01_Stan.mp3" -> "Stan.mp3"). */
static const char *display_name(const char *n) {
    const char *s0 = n;
    if (n[0] >= '1' && n[0] <= '9' && (n[1] == '-' || n[1] == '.') && n[2] >= '0' && n[2] <= '9' && n[3] >= '0' && n[3] <= '9')
        n += 2;                                /* "1-01 Titre" (disque 1, piste 01) */
    int d = 0;
    while (d < 4 && n[d] >= '0' && n[d] <= '9') d++;
    if (d == 0 || d > 3) return s0;            /* 1 a 3 chiffres seulement */
    const char *p = n + d;
    int sp = 0;
    while (*p == ' ') { p++; sp++; }
    if (*p == '.' || *p == '-' || *p == '_' || *p == ')') p++;
    else if (!(sp && n[0] == '0' && d >= 2)) return s0;  /* "50 Cent" reste intact ; "01 Titre" est coupe */
    while (*p == ' ' || *p == '-' || *p == '_' || *p == '.') p++;
    if (!*p || (strlen(p) <= 4 && p[-1] == '.')) return s0;   /* il ne resterait que ".mp3" */
    return p;
}

/* v22 : nom d'un morceau dans les listes : "Artiste - Titre" d'apres ses
   etiquettes, sinon le nom du fichier sans numero, sans extension, sans "_" */
static void track_label(int k, char *out, int cap, int with_artist) {
    if (k >= 0 && k < nfiles && tg_done[k] && tg_title[k][0]) {
        if (with_artist && tg_artist[k][0]) snprintf(out, cap, "%s - %s", tg_artist[k], tg_title[k]);
        else snprintf(out, cap, "%s", tg_title[k]);
        return;
    }
    snprintf(out, cap, "%s", display_name(base_of(names[k])));
    strip_audio_ext(out);
    for (char *c = out; *c; c++) if (*c == '_') *c = ' ';
    const char *b = display_name(out);                  /* ("Artiste - 01 - Titre") */
    if (b != out) memmove(out, b, strlen(b) + 1);
    char *m = strstr(out, " - ");
    if (m) {
        const char *after = display_name(m + 3);
        if (after != m + 3) memmove(m + 3, after, strlen(after) + 1);
    }
}

/* titre a afficher : etiquette du fichier, sinon son nom sans l'extension */
static void track_title(int k, char *out, int cap) {
    if (k == cur_track && id3_title[0]) { snprintf(out, cap, "%s", id3_title); return; }
    snprintf(out, cap, "%s", display_name(base_of(names[k])));
    strip_audio_ext(out);
}

static void draw_note(f32 cx, f32 cy, col_t c) {
    gx_ellipse(cx - 2.0f, cy + 5.0f, 3.4f, 2.6f, c, c);
    gx_rect(cx + 0.6f, cy - 6.0f, 1.6f, 11.0f, c);
    gx_tri(cx + 0.6f, cy - 6.0f, cx + 5.5f, cy - 3.0f, cx + 0.6f, cy - 1.0f, c);
}

/* v76 : une etoile a 5 branches (pleine), centree en (cx, cy), rayon r */
static void draw_star1(f32 cx, f32 cy, f32 r, col_t c) {
    f32 px[10], py[10];
    for (int i = 0; i < 10; i++) {
        f32 a = -1.5707963f + i * 0.62831853f;             /* pointe en haut, tous les 36 deg */
        f32 rr = (i & 1) ? r * 0.42f : r;
        px[i] = cx + rr * cosf(a);
        py[i] = cy + rr * sinf(a);
    }
    for (int i = 0; i < 10; i++) gx_tri(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], c);
}
/* n etoiles pleines sur 5 : les pleines en 'hot', les vides en 'dim' */
static void draw_stars(f32 x, f32 cy, f32 r, int n, col_t hot, col_t dim) {
    for (int i = 0; i < 5; i++) draw_star1(x + i * (r * 2.0f + 2.0f) + r, cy, r, i < n ? hot : dim);
}

#define MUSIC_ROWS 13
static void draw_folder_icon(f32 cx, f32 cy, col_t c) {
    gx_rrect_fill(cx - 7, cy - 4, 14, 10, 1.5f, c, c);
    gx_rrect_fill(cx - 7, cy - 6, 6, 3, 1, c, c);
}

static void draw_person_icon(f32 cx, f32 cy, col_t c) {
    gx_circle(cx, cy - 3, 3.2f, c, c);
    gx_rrect_fill(cx - 6, cy + 1, 12, 6, 3, c, c);
}

static void draw_disc_icon(f32 cx, f32 cy, col_t c) {
    gx_ring(cx, cy, 2.0f, 6.5f, c);
}

static void draw_list_music(int sel) {
    int start = sel - MUSIC_ROWS / 2;
    if (start > nents - MUSIC_ROWS) start = nents - MUSIC_ROWS;
    if (start < 0) start = 0;
    char buf[NAME_LEN], t[24];

    draw_panel(11, 73, 271, 385);
    snprintf(buf, sizeof(buf), "%d", lib_find[0] || view_mode == VIEW_ALL || cur_group >= 0 ? (cur_group >= 0 ? nents - 1 : nents) : ngroups);
    draw_tabs(buf);

    static f32 sel_anim = -2000;
    if (sel >= start && sel < start + MUSIC_ROWS && sel < nents) draw_sel_bar(&sel_anim, 114.0f + (sel - start) * 24.0f);
    for (int i = 0; i < MUSIC_ROWS; i++) {
        int r = start + i;
        if (r >= nents) break;
        lent_t e = ents[r];
        f32 ry = 114.0f + i * 24.0f;
        f32 cy = ry + 12.0f;
        int is_sel = (r == sel);
        gp_zone(18, ry + 1, 250, 22, &sel_row, r, GPZ_SEL);
        int is_cur = (e.type == E_TRACK && playing && e.idx == cur_track);

        if (is_sel) {
        } else if (i + 1 < MUSIC_ROWS && r + 1 < nents && r + 1 != sel) {
            gx_rect(25, ry + 24, 241, 1, RGBA(60, 40, 120, 110));
        }
        col_t dim = is_sel ? RGBA(235, 225, 255, 255) : RGBA(128, 112, 190, 255);
        col_t txt = is_sel ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
        col_t ic = is_sel ? RGBA(235, 225, 255, 255) : RGBA(150, 120, 235, 255);

        if (e.type == E_UP) {
            gx_tri(38, cy - 6, 38, cy + 6, 30, cy, ic);
            char nm[NAME_LEN];
            fit_text(F_MED, group_label(e.idx), 196, nm, sizeof(nm));
            gx_text1(F_MED, 50, cy + 5, nm, txt);
            continue;
        }
        if (e.type == E_GROUP) {
            if (view_mode == VIEW_FOLDERS) draw_folder_icon(36, cy, ic);
            else if (view_mode == VIEW_ARTISTS) draw_person_icon(36, cy, ic);
            else draw_disc_icon(36, cy, ic);
            snprintf(t, sizeof(t), "%d", grp_count[e.idx]);
            gx_text_r(F_SMALL, 256, cy + 4, t, dim);
            char nm[NAME_LEN];
            fit_text(F_MED, group_label(e.idx), 196 - text_width(F_SMALL, t, 0), nm, sizeof(nm));
            gx_text1(F_MED, 52, cy + 5, nm, txt);
            continue;
        }
        int k = e.idx;
        if (is_cur) gx_tri(31, cy - 6, 31, cy + 6, 41, cy, RGBA(255, 255, 255, 255));
        else draw_note(36, cy, ic);

        (void)dim;
        fmt_time(t, track_duration(k));
        f32 dr = is_cur ? 240.0f : 256.0f;
        int name_w = (int)dr - text_width(F_SMALL, t, 0) - 8 - 52;   /* place libre pour le nom */
        gx_text_r(F_SMALL, dr, cy + 4, t, is_sel ? RGBA(235, 225, 255, 255) : RGBA(150, 135, 210, 255));
        if (is_cur) {
            for (int j = 0; j < 3; j++) {
                f32 bh = 3.0f + (paused ? 2.0f : 8.0f * bar_val[3 + j * 9]);
                gx_rect(246.0f + j * 4.0f, cy + 5.0f - bh, 2.5f, bh, RGBA(255, 255, 255, 255));
            }
        }
        char nm[NAME_LEN], lb[NAME_LEN], ld[NAME_LEN];
        if (!tg_done[k] && tag_budget > 0) { tag_budget--; tag_scan_one(k); service_audio(); }   /* (une par image) */
        track_label(k, lb, sizeof(lb), !(cur_group >= 0 && (view_mode == VIEW_ARTISTS || view_mode == VIEW_ALBUMS)));
        text_display(lb, ld, sizeof(ld));
        fit_text(F_MED, ld, name_w, nm, sizeof(nm));
        gx_text1(F_MED, 52, cy + 5, nm, txt);
    }

    if (nfiles == 0 && sd_virtual) {                    /* Dolphin, ou pas d'adaptateur SD */
        gx_text1(F_MED, 30, 138, "Pas de carte SD", RGBA(255, 205, 110, 255));
        gx_text1(F_SMALL, 30, 160, "Radio, podcasts et web marchent.", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 30, 178, "Les morceaux t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9s restent", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 30, 196, "jusqu'\xC3\xA0 la fermeture de GC Station.", RGBA(190, 180, 235, 255));
        if (rf_card_msg[0]) {
            char m[96];
            fit_text(F_SMALL, rf_card_msg, 236, m, sizeof(m));
            gx_text1(F_SMALL, 30, 222, m, RGBA(150, 200, 160, 255));
        }
    } else if (nfiles == 0) {
        gx_text1(F_MED, 30, 138, "Aucun fichier MP3 trouv\xC3\xA9", RGBA(255, 205, 110, 255));
        gx_text1(F_SMALL, 30, 160, "Copie tes MP3 dans SD:/mp3", RGBA(190, 180, 235, 255));
        gx_text1(F_SMALL, 30, 178, "ou t\xC3\xA9l\xC3\xA9" "charge-les dans SD:/downloads", RGBA(190, 180, 235, 255));
    }

    /* bas de la liste : vue (stick C gauche/droite), modes de lecture */
    gx_rect(25, 430, 241, 1, RGBA(76, 52, 150, 200));
    char foot[96];
    col_t fc = RGBA(170, 155, 225, 255);
    if ((view_mode == VIEW_ARTISTS || view_mode == VIEW_ALBUMS) && tg_scanned < nfiles) {
        snprintf(foot, sizeof(foot), "Lecture des \xC3\xA9tiquettes %d/%d", tg_scanned, nfiles);
        fc = RGBA(255, 210, 120, 255);
    } else if (lib_find[0]) snprintf(foot, sizeof(foot), "Recherche : %.40s  (B)", lib_find);
    else snprintf(foot, sizeof(foot), "< %s >", TR(view_names[view_mode]));
    gx_text1(F_SMALL, 26, 447, foot, fc);
    f32 rx = 264;
    if (repeat_mode != REP_OFF) {
        const char *rl = repeat_mode == REP_ONE ? "R\xC3\x89P. 1" : "R\xC3\x89P.";
        int w = text_width(F_SMALL, rl, 0) + 10;
        gx_rrect_fill(rx - w, 436, w, 14, 4, RGBA(110, 60, 230, 255), RGBA(80, 36, 180, 255));
        gx_text_c(F_SMALL, rx - w / 2.0f, 447, rl, RGBA(255, 255, 255, 255));
        rx -= w + 4;
    }
    if (shuffle_on) {
        const char *sl = "AL\xC3\x89" "A";
        int w = text_width(F_SMALL, sl, 0) + 10;
        gx_rrect_fill(rx - w, 436, w, 14, 4, RGBA(110, 60, 230, 255), RGBA(80, 36, 180, 255));
        gx_text_c(F_SMALL, rx - w / 2.0f, 447, sl, RGBA(255, 255, 255, 255));
    }

    /* ascenseur */
    if (nents > MUSIC_ROWS) {
        f32 track_h = 308.0f;
        f32 th = track_h * MUSIC_ROWS / nents;
        if (th < 18.0f) th = 18.0f;
        f32 ty = 116.0f + (track_h - th) * (f32)start / (f32)(nents - MUSIC_ROWS);
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
        uxsave_t uz = ux_native(x + sz / 2);             /* (16:9 : pochette carree) */
        gx_set_cover();
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(UX(x), y);           GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(UX(x + sz), y);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(UX(x + sz), y + sz); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(UX(x), y + sz);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f);
        GX_End();
        gx_rrect_stroke(x, y, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 235));
        ux_restore(uz);
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
    if (playing && qn > 0 && qpos >= 0 && qpos < qn && queue[qpos] == cur_track) snprintf(buf, sizeof(buf), "%02d / %02d", qpos + 1, qn);
    else snprintf(buf, sizeof(buf), "%02d / %02d", (playing && cur_track >= 0) ? cur_track + 1 : 0, nfiles);
    gx_text_r(F_MED, 614, 99, buf, RGBA(165, 145, 235, 255));

    if (EL(EL_COVER)) draw_art_music();
    const f32 TX = EL(EL_COVER) ? 456 : 308;          /* sans pochette, le texte prend la place */
    const int TW = EL(EL_COVER) ? 160 : 300;

    int dur = playing ? track_duration(cur_track) : -1;
    int elapsed = (int)(consumed / 48000u);
    if (playing && cur_track >= 0 && cur_track < nfiles) {
        char ttl[200];
        track_title(cur_track, ttl, sizeof(ttl));
        fit_text(F_LARGE, ttl, TW, buf, sizeof(buf));
        gx_text1(F_LARGE, TX, 140, buf, RGBA(255, 255, 255, 255));
        if (track_err) {
            gx_text1(F_MED, TX, 160, track_err, RGBA(255, 150, 80, 255));
        } else if (artist[0]) {
            fit_text(F_MED, artist, TW, buf, sizeof(buf));
            gx_text1(F_MED, TX, 160, buf, RGBA(156, 116, 255, 255));
        }
        /* album et annee (etiquettes du MP3, sinon MusicBrainz) */
        char info[200];
        int searching = cover_info(info, sizeof(info));
        if (!track_err && info[0] && EL(EL_INFO)) {
            fit_text(F_SMALL, info, TW, buf, sizeof(buf));
            gx_text1(F_SMALL, TX, 178, buf, searching ? RGBA(255, 210, 120, 255) : RGBA(140, 122, 200, 255));
        }
    } else {
        gx_text1(F_LARGE, TX, 140, "Aucune piste", RGBA(228, 218, 255, 255));
        if (track_err) {
            fit_text(F_MED, track_err, TW, buf, sizeof(buf));
            gx_text1(F_MED, TX, 160, buf, RGBA(255, 150, 80, 255));
        } else {
            gx_text1(F_MED, TX, 160, "Appuie sur A", RGBA(156, 116, 255, 255));
        }
    }

    fmt_time(t1, playing ? elapsed : 0);
    fmt_time(t2, dur);
    snprintf(buf, sizeof(buf), "%s / %s", t1, t2);
    gx_text1(F_MED, TX, 196, buf, RGBA(180, 160, 240, 255));

    f32 frac = (playing && dur > 0) ? (f32)consumed / (48000.0f * (f32)dur) : 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    const f32 px = TX + 1, py = 207, pw = EL(EL_COVER) ? 156 : 304;
    gx_rrect_fill(px, py, pw, 3, 1.5f, RGBA(70, 50, 140, 230), RGBA(70, 50, 140, 230));
    if (playing) {
        if (frac > 0.0f) gx_hgrad(px, py, pw * frac, 3, RGBA(120, 64, 255, 255), RGBA(180, 100, 255, 255));
        f32 kx = px + pw * frac;
        gx_circle(kx, py + 1.5f, 9, RGBA(150, 90, 255, 100), RGBA(150, 90, 255, 0));
        gx_circle(kx, py + 1.5f, 5.5f, RGBA(240, 232, 255, 255), RGBA(240, 232, 255, 255));
        gx_circle(kx, py + 1.5f, 3, RGBA(124, 66, 255, 255), RGBA(124, 66, 255, 255));
    }

    /* paroles (fichier .lrc) a la place de l'egaliseur */
    int li = playing ? lyrics_current() : -2;
    if (!EL(EL_VIZ)) { }
    else if (playing && lrc_n > 0 && lrc_track == cur_track && lyr_show) {
        char a[300], b[300], d1[300];
        if (lyr_show == 2) {                                  /* v43 : paroles + spectre (attenue dessous) */
            draw_viz(306, 295, 308, 48);
            gx_rect(300, 228, 320, 84, RGBA(8, 5, 26, 150));
        }
        if (li > 0) {
            text_display(lyrics_line(li - 1), d1, sizeof(d1));
            fit_text(F_SMALL, d1, 300, a, sizeof(a));
            gx_text_c(F_SMALL, 460, 240, a, RGBA(120, 104, 180, 255));
        }
        text_display(li >= 0 ? lyrics_line(li) : "...", d1, sizeof(d1));
        wrap2(F_MED, d1, 300, a, b, sizeof(a));
        gx_text_c(F_MED, 460, b[0] ? 262 : 270, a, RGBA(255, 255, 255, 255));
        if (b[0]) gx_text_c(F_MED, 460, 281, b, RGBA(255, 255, 255, 255));
        if (li + 1 < lrc_n) {
            text_display(lyrics_line(li + 1), d1, sizeof(d1));
            fit_text(F_SMALL, d1, 300, a, sizeof(a));
            gx_text_c(F_SMALL, 460, 304, a, RGBA(120, 104, 180, 255));
        }
    } else {
    /* spectre de ce qui est joue (v39 : plusieurs styles) */
    draw_viz(306, 295, 308, 48);
    }

    /* boutons de transport */
    if (!EL(EL_BUTTONS)) return;
    gp_player_zones();
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

// ---------- Onglets, liste a cocher, dispositions du lecteur ----------
static void tag_scan_one(int k);
static void save_settings(void);
static int settings_dirty;
#include "ui_extra.c"
#include "ui_aurore2.c"                         /* v103 : Aurore : lecteur de MUSIQUE */

// ====================================================================
// ---------- Navigateur web (onglet WEB) ----------
// Navigateur en mode texte : HTML sans JavaScript ni images. Les liens vers
// des fichiers (MP3...) sont telecharges dans sd:/downloads.
// ====================================================================
#define WEB_PAGE_MAX   (1536 * 1024)        // taille maxi d'une page HTML
#define FAV_FILE       GCD "/favoris.txt"
#define SEARCH_URL     "https://html.duckduckgo.com/html/?q="   /* v74 : Mojeek refuse souvent (403) */
static const char *const ws_engines[] = { "https://html.duckduckgo.com/html/?q=", "https://www.bing.com/search?q=",
                                          "https://www.mojeek.com/search?q=", "https://search.marginalia.nu/search?query=", NULL };
static char ws_q[700] = "";                    /* derniere recherche (encodee) */
#define HIST_MAX       20
#define W_MAX_ITEMS    60000
#define W_MAX_LINKS    12000
#define W_MAX_FIELDS   64
#define W_MAX_FORMS    16

enum { WS_LINK = 1, WS_HEAD = 2, WS_BOLD = 4, WS_DIM = 8, WS_FIELD = 16, WS_BUTTON = 32, WS_HR = 64, WS_IMG = 128 };
enum { LK_HREF, LK_INPUT, LK_SUBMIT, LK_TOGGLE, LK_SELECT };
enum { FD_TEXT, FD_HIDDEN, FD_SUBMIT, FD_CHECK, FD_RADIO, FD_SELECT };

/* element affiche : off = texte, src = position dans le HTML, img/h = image */
typedef struct { u32 off; u32 src; s32 y; s16 x; u16 w; s16 link; s16 img; u16 h; u8 font; u8 style; } witem_t;
typedef struct { u32 url; s32 y0, y1; s16 field; u8 kind; s8 form; } wlink_t;
typedef struct { s8 form; u8 type; u32 name; char value[256]; u8 checked, secret; s16 opt0, nopt, osel; } wfield_t;
#define W_MAX_OPTS 2000                     /* choix des listes deroulantes */
typedef struct { u32 action; u8 post; } wform_t;

typedef struct {
    char *text;  u32 tlen, tcap;            // textes affiches (chaines terminees par 0)
    char *pool;  u32 plen, pcap;            // adresses des liens, noms des champs
    witem_t *items; int nitems, icap;
    wlink_t *links; int nlinks, lcap;
    wfield_t *fields; int nfields;
    u32 *opts; int nopts, ocap;             // listes deroulantes : (valeur, texte) dans pool
    wform_t forms[W_MAX_FORMS]; int nforms;
    s32 height;
    int truncated;
    char url[URL_MAX];
    char title[160];
    char refresh[URL_MAX];
    int refresh_delay;
    unsigned char *src; int srclen;         // HTML d'origine (pour remettre en page)
    int kind, latin;
} wpage_t;

// ---------- Images de la page ----------
#define W_MAX_IMGS     96
#define IMG_MAX_H      320                  // hauteur maxi affichee
#define IMG_MAX_BYTES  (2 * 1024 * 1024)    // fichier image maxi
#define IMG_MAX_PIXELS (1200 * 1000)        // image decodee maxi
#define IMG_TEX_BUDGET (4 * 1024 * 1024)    // memoire des textures d'images
enum { IMG_NONE, IMG_LOADING, IMG_OK, IMG_FAIL };
typedef struct {
    char *url;
    s16 aw, ah;                             // taille donnee par la page (0 = inconnue)
    s16 nw, nh;                             // taille reelle (une fois lue)
    u8 st;
    u16 tw, th, iw, ih;                     // texture (multiple de 4) et partie utile
    u16 *tex;
    GXTexObj obj;
    s32 y0, y1;                             // position dans la page
} wimg_t;
static wimg_t wimgs[W_MAX_IMGS];
static int nwimg = 0;
static u32 img_gen = 1;                     // change a chaque nouvelle page
static long img_bytes = 0;
static int img_tex_dirty = 0;
static int relayout_needed = 0;
static void img_loader_reset(void);

static void img_reset(void) {
    img_loader_reset();
    for (int i = 0; i < nwimg; i++) { free(wimgs[i].url); free(wimgs[i].tex); }
    memset(wimgs, 0, sizeof(wimgs));
    nwimg = 0;
    img_bytes = 0;
    img_gen++;
    relayout_needed = 0;
}

static int img_get(const char *url) {
    for (int i = 0; i < nwimg; i++) if (strcmp(wimgs[i].url, url) == 0) return i;
    if (nwimg >= W_MAX_IMGS) return -1;
    char *u = strdup(url);
    if (!u) return -1;
    memset(&wimgs[nwimg], 0, sizeof(wimg_t));
    wimgs[nwimg].url = u;
    wimgs[nwimg].y0 = -1;
    return nwimg++;
}

/* taille d'affichage d'une image : attributs width/height, sinon taille reelle,
   reduite pour tenir dans maxw x IMG_MAX_H. 0 = encore inconnue. */
static void img_box(int aw, int ah, int nw, int nh, int maxw, int *dw, int *dh) {
    int w = aw, h = ah;
    if (nw > 0 && nh > 0) {
        if (w > 0 && h <= 0) h = (int)((long)nh * w / nw);
        else if (h > 0 && w <= 0) w = (int)((long)nw * h / nh);
        else if (w <= 0 || h <= 0) { w = nw; h = nh; }
    } else if (w <= 0 || h <= 0) { *dw = *dh = 0; return; }
    if (w > maxw) { h = (int)((long)h * maxw / w); w = maxw; }
    if (h > IMG_MAX_H) { w = (int)((long)w * IMG_MAX_H / h); h = IMG_MAX_H; }
    *dw = w;
    *dh = h;
}

static wpage_t pg;                          // page affichee
static s32 pg_scroll = 0;
static int pg_sel = -1;                     // lien selectionne

static void wpage_free(wpage_t *p) {
    free(p->text); free(p->pool); free(p->items); free(p->links); free(p->fields); free(p->src); free(p->opts);
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
    if (strstr(url, "bing.com/ck/a")) {                    /* v74 : liens des resultats Bing */
        const char *u = strstr(url, "&u=a1");
        if (!u) u = strstr(url, "?u=a1");
        if (!u) return;
        u += 5;
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        char dec[URL_MAX];
        int n = 0, bits = 0; u32 acc = 0;
        for (; *u && *u != '&' && n < (int)sizeof(dec) - 1; u++) {
            const char *k = strchr(b64, *u == '+' ? '-' : *u == '/' ? '_' : *u);
            if (!k) break;
            acc = acc << 6 | (u32)(k - b64); bits += 6;
            if (bits >= 8) { bits -= 8; dec[n++] = (char)(acc >> bits & 0xFF); }
        }
        dec[n] = 0;
        if (strncasecmp(dec, "http", 4) == 0) snprintf(url, sz, "%s", dec);
        return;
    }
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
static int utf8_ok(const unsigned char *s) {           /* v26 : texte UTF-8 valide ? (sinon : Latin-1) */
    while (*s) {
        if (*s < 0x80) { s++; continue; }
        int n = (*s & 0xE0) == 0xC0 ? 1 : (*s & 0xF0) == 0xE0 ? 2 : (*s & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || (*s == 0xC0 || *s == 0xC1)) return 0;
        for (int i = 1; i <= n; i++) if ((s[i] & 0xC0) != 0x80) return 0;
        s += n + 1;
    }
    return 1;
}
static void text_display(const char *in, char *out, int outsz) {
    const unsigned char *s = (const unsigned char *)in;
    int len = (int)strlen(in), pos = 0, n = 0;
    int latin = !utf8_ok(s);
    while (pos < len && n < outsz - 4) {
        char tmp[4];
        int k = cp_out(read_cp(s, len, &pos, latin), tmp);
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
    u32 srcpos;                             // position courante dans le HTML
} wlay_t;

static int font_space(int f) { return font_glyphs[f][0].adv; }

static void lay_newline(wlay_t *L) {
    wpage_t *p = L->p;
    if (L->line_first < p->nitems) {
        int asc = 0, h = 0;
        for (int i = L->line_first; i < p->nitems; i++) {
            int f = p->items[i].font;
            int ih = (p->items[i].style & WS_FIELD) ? font_height[F_MED] + 6 : font_height[f];
            int ia = font_ascent[f];
            if (p->items[i].style & WS_IMG) { ia = p->items[i].h; ih = p->items[i].h + 3; }
            if (ia > asc) asc = ia;
            if (ih > h) h = ih;
        }
        if (p->items[L->line_first].style & WS_HR) { asc = 6; h = 12; }
        for (int i = L->line_first; i < p->nitems; i++) {
            p->items[i].y = L->line_top + asc;
            if ((p->items[i].style & WS_IMG) && p->items[i].img >= 0) {
                wimg_t *im = &wimgs[p->items[i].img];
                if (im->y0 < 0) { im->y0 = L->line_top + asc - p->items[i].h; im->y1 = L->line_top + asc; }
            }
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
    it->src = L->srcpos;
    it->img = -1;
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
    int ww = text_width_px(L->font, buf, 0);
    int sw = (L->space && L->x > L->indent) ? font_space(L->font) : 0;
    int maxw = wv_lw - L->indent;
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
    if (L->x + sw + ww > wv_lw && L->x > L->indent) { lay_newline(L); sw = 0; }
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
    if (w > wv_lw - L->indent) w = wv_lw - L->indent;
    if (L->x + sw + w > wv_lw && L->x > L->indent) { lay_newline(L); sw = 0; }
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
    witem_t *it = lay_new_item(L, L->indent, wv_lw - L->indent);
    L->style = save;
    if (it && !text_append(L->p, "", 0, 0)) L->p->nitems--;
    lay_newline(L);
}

/* image (ou sa case d'attente avec le texte label) */
static void lay_img(wlay_t *L, int img, int w, int h, const char *label) {
    int sw = (L->space && L->x > L->indent) ? font_space(L->font) : 0;
    if (w > wv_lw - L->indent) w = wv_lw - L->indent;
    if (L->x + sw + w > wv_lw && L->x > L->indent) { lay_newline(L); sw = 0; }
    int save = L->style;
    L->style = WS_IMG | (L->link >= 0 ? WS_LINK : 0);
    witem_t *it = lay_new_item(L, L->x + sw, w);
    L->style = save;
    if (!it) return;
    it->img = (s16)img;
    it->h = (u16)h;
    it->font = F_SMALL;
    if (!text_append(L->p, label ? label : "", label ? (int)strlen(label) : 0, 0)) { L->p->nitems--; return; }
    L->x += sw + w;
    L->space = 0;
    L->run = -1;
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
    f->osel = -1;
    return p->nfields++;
}

/* choix d'une liste deroulante */
static int new_opt(wpage_t *p, const char *value, const char *label) {
    if (p->nopts >= W_MAX_OPTS) return -1;
    if (!grow((void **)&p->opts, &p->ocap, (p->nopts + 1) * 2, sizeof(u32), W_MAX_OPTS * 2)) return -1;
    p->opts[p->nopts * 2] = pool_add(p, value);
    p->opts[p->nopts * 2 + 1] = pool_add(p, label);
    return p->nopts++;
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

    for (int i = 0; i < nwimg; i++) wimgs[i].y0 = wimgs[i].y1 = -1;
    while (pos < len && !p->truncated) {
        if ((++tick & 1023) == 0) bg_tick();
        L.srcpos = (u32)pos;
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
            /* <option> : un choix de la liste deroulante en cours */
            if (!close && strcmp(name, "option") == 0 && cur_select >= 0) {
                int nx = pos;
                while (nx < len && s[nx] != '<') nx++;
                char lab[200];
                raw_text(s, pos, nx, lab, sizeof(lab), latin);
                char *lt = trim(lab);
                if (!tag_attr(tag, tl, "value", val, sizeof(val), latin)) snprintf(val, sizeof(val), "%s", lt);
                wfield_t *f = &p->fields[cur_select];
                if (new_opt(p, val, lt[0] ? lt : val) >= 0) {
                    if (f->nopt == 0) f->opt0 = (s16)(p->nopts - 1);
                    if (f->opt0 + f->nopt == p->nopts - 1) {
                        if (tag_attr(tag, tl, "selected", NULL, 0, latin) || f->osel < 0) f->osel = f->nopt;
                        f->nopt++;
                    }
                }
                continue;
            }
            if (close && strcmp(name, "select") == 0 && cur_select >= 0) {
                wfield_t *f = &p->fields[cur_select];
                if (f->nopt > 0) {
                    if (f->osel < 0 || f->osel >= f->nopt) f->osel = 0;
                    snprintf(f->value, sizeof(f->value), "%s", p->pool + p->opts[(f->opt0 + f->osel) * 2]);
                }
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
                    FLUSH_WORD();
                    cur_select = new_field(p, cur_form, FD_SELECT, val, "");
                    if (cur_select >= 0) {                        /* case cliquable : le choix en cours */
                        p->fields[cur_select].opt0 = (s16)p->nopts;
                        int save = L.link;
                        L.link = new_link(p, LK_SELECT, NULL, cur_select, cur_form);
                        if (L.link >= 0) lay_box(&L, 220, WS_FIELD, NULL);
                        L.link = save;
                    }
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
                L.x = L.indent - 5 - text_width_px(L.font, bullet, 0);
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
                char alt[200] = "";
                if (tag_attr(tag, tl, "alt", val, sizeof(val), latin) && strlen(val) < 120) text_display(val, alt, sizeof(alt));
                /* adresse : src, ou data-src... (images chargees par JavaScript) */
                static const char *const srcs[] = { "src", "data-src", "data-original", "data-lazy-src", NULL };
                int idx = -1;
                for (int k = 0; srcs[k] && idx < 0; k++) {
                    if (!tag_attr(tag, tl, srcs[k], val, sizeof(val), latin) || !val[0] ||
                        strncasecmp(val, "data:", 5) == 0) continue;
                    if (url_join(base, val, val2, sizeof(val2))) idx = img_get(val2);
                }
                wimg_t *im = idx >= 0 ? &wimgs[idx] : NULL;
                if (im) {
                    char wv[24], hv[24];
                    if (tag_attr(tag, tl, "width", wv, sizeof(wv), latin) && !strchr(wv, '%') && atoi(wv) > 0) im->aw = (s16)atoi(wv);
                    if (tag_attr(tag, tl, "height", hv, sizeof(hv), latin) && !strchr(hv, '%') && atoi(hv) > 0) im->ah = (s16)atoi(hv);
                }
                int dw = 0, dh = 0;
                if (im && im->st != IMG_FAIL) img_box(im->aw, im->ah, im->nw, im->nh, wv_lw - L.indent, &dw, &dh);
                if (im && im->st != IMG_FAIL && dw > 0) {
                    if (dw >= 3 && dh >= 3) lay_img(&L, idx, dw, dh, alt);   /* sinon : image d'espacement */
                } else if (im && im->st != IMG_FAIL && im->st != IMG_OK) {
                    /* taille inconnue tant que l'image n'est pas lue : petite case */
                    char lab[48];
                    fit_text(F_SMALL, alt, 160, lab, sizeof(lab));
                    int w = text_width_px(F_SMALL, lab, 0) + 12;
                    lay_img(&L, idx, w < 24 ? 24 : w, 18, lab);
                } else if (alt[0] && (link_open || strlen(alt) > 2)) {
                    char t2[210];
                    snprintf(t2, sizeof(t2), "[%s]", alt);
                    int save = L.style;
                    if (!link_open) L.style |= WS_DIM;
                    lay_text(&L, t2);
                    L.style = save;
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
                    if (nm[0]) {                                  /* case a cocher / bouton radio */
                        FLUSH_WORD();
                        int f = new_field(p, cur_form, type[0] == 'c' ? FD_CHECK : FD_RADIO, nm, v[0] ? v : "on");
                        if (f >= 0) {
                            p->fields[f].checked = (u8)(tag_attr(tag, tl, "checked", NULL, 0, latin) != 0);
                            int save = L.link;
                            L.link = new_link(p, LK_TOGGLE, NULL, f, cur_form);
                            if (L.link >= 0) lay_box(&L, 22, WS_FIELD, NULL);
                            L.link = save;
                        }
                    }
                } else if (strcmp(type, "submit") == 0 || strcmp(type, "image") == 0) {
                    int f = new_field(p, cur_form, FD_SUBMIT, nm, v);
                    int save = L.link;
                    L.link = new_link(p, LK_SUBMIT, NULL, f, cur_form);
                    if (L.link >= 0) {
                        char disp[200];
                        text_display(v[0] ? v : "Envoyer", disp, sizeof(disp));
                        lay_box(&L, text_width_px(F_MED, disp, 0) + 20, WS_BUTTON, disp);
                    }
                    L.link = save;
                } else if (strcmp(type, "text") == 0 || strcmp(type, "search") == 0 || strcmp(type, "email") == 0 ||
                           strcmp(type, "url") == 0 || strcmp(type, "number") == 0 || strcmp(type, "tel") == 0 ||
                           strcmp(type, "password") == 0) {
                    int f = new_field(p, cur_form, FD_TEXT, nm, v);
                    if (f >= 0) {
                        p->fields[f].secret = (u8)(strcmp(type, "password") == 0);
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

enum { WK_HTML, WK_TEXT, WK_ARCHIVE, WK_ARCHMETA };
enum { NAV_PUSH, NAV_BACK, NAV_REPLACE };

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
static char *wb_post = NULL;                // formulaire POST : corps a envoyer
static int wb_post_len = 0;
static int wb_partial = 0, wb_shown_len = 0; // page affichee avant la fin du chargement
static u64 wb_partial_t = 0;
static int web_no_refresh = 0;             // pas de <meta refresh> sur une page incomplete
static int ptr_on = 0;                     // pointeur au stick affiche (web_extra.c)
/* web_extra.c (inclus plus loin) */
static unsigned char *icache_get(const char *url, int *len);
static void icache_put(const char *url, const unsigned char *d, int len);
static void ck_from_headers(const char *hdrs, const char *host, const char *path, int tls);
static void ck_header(const char *host, const char *path, int tls, char *out, int cap);
static void ck_pump(void);
static int ck_count(void);
static void ck_clear(void);
static int au_is_audio(const char *ctype, const char *url);
static void au_offer(const unsigned char *body, int n, const char *ctype, const char *disp, const char *url, long clen, int chunked, int icy);
static void sp_open(int field);
static int web_overlay_input(u32 down, u32 nav, int fire);
static int ptr_update(void);
static void draw_web_overlays(void);

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
static char dl_dest_dir[NAME_LEN] = "";     /* (podcasts) dossier du prochain telechargement */
static char dl_dest_dir_used[NAME_LEN] = "";
static char dl_name_hint[160] = "";         /* (podcasts) nom du prochain fichier */
static int dl_force = 0;                    /* (podcasts) telecharger sans demander "ecouter ou telecharger" */
static char dl_name[128], dl_path[NAME_LEN], dl_part[NAME_LEN + 8];
static char dl_ep_url[URL_MAX], dl_ep_used[URL_MAX];   /* v71 : episode de podcast telecharge (son adresse) */
static void pod_dl_note(const char *url, const char *path);
static long dl_total = -1, dl_got = 0;
static u64 dl_t0 = 0, dl_last = 0;
static dechunk_t dl_dc;
static void music_rescan(void);
static void music_add_file(const char *path);

static void dlh_add(const char *from, const char *path);
static int dlh_loaded;
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
    else if (ctype && (ci_find((char *)ctype, "audio/mp4") || ci_find((char *)ctype, "audio/x-m4a")) && !strchr(dec, '.'))
        snprintf(dec + n, sizeof(dec) - n, ".m4a");
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
    snprintf(dl_dest_dir_used, sizeof(dl_dest_dir_used), "%s", dl_dest_dir[0] ? dl_dest_dir : DL_DIR);
    const char *dir = dl_dest_dir_used;
    mkdir(dir, 0777);
    char name[128];
    dl_filename(disp, url, ctype, name, sizeof(name));
    if (dl_name_hint[0]) {                                  /* titre de l'episode + extension du fichier */
        char hint[160], ext[12] = ".mp3";
        const char *e = strrchr(name, '.');
        if (e && strlen(e) < sizeof(ext)) snprintf(ext, sizeof(ext), "%s", e);
        snprintf(hint, sizeof(hint), "%.100s", dl_name_hint);
        sanitize_name(hint);
        if (hint[0]) snprintf(name, sizeof(name), "%s%s", hint, ext);
    }
    dl_dest_dir[0] = dl_name_hint[0] = 0;
    snprintf(dl_ep_used, sizeof(dl_ep_used), "%s", dl_ep_url); dl_ep_url[0] = 0;
    snprintf(dl_path, sizeof(dl_path), "%s/%s", dir, name);
    struct stat st;
    for (int k = 2; k < 100 && stat(dl_path, &st) == 0; k++) {          /* nom deja pris : "nom (2).mp3" */
        const char *e = strrchr(name, '.');
        int bl = e ? (int)(e - name) : (int)strlen(name);
        snprintf(dl_path, sizeof(dl_path), "%s/%.*s (%d)%s", dir, bl, name, k, e ? e : "");
    }
    snprintf(dl_name, sizeof(dl_name), "%s", base_of(dl_path));
    crumb("telechargement : %s", dl_name);
    snprintf(dl_part, sizeof(dl_part), "%s.part", dl_path);
    dl_fp = fopen(dl_part, "wb");
    if (!dl_fp) { conn_close(PCONN); web_note("Impossible de créer le fichier sur la carte SD", 1); return; }
    static char dl_vbuf[65536];                 /* ecritures SD par gros blocs : plus rapide */
    setvbuf(dl_fp, dl_vbuf, _IOFBF, sizeof(dl_vbuf));
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
    char msg[260], spd[40] = "";
    u64 el = now_ms() - dl_t0;
    if (el > 0 && dl_got > 65536) snprintf(spd, sizeof(spd), ", %ld Ko/s", (long)((long long)dl_got * 1000 / 1024 / (long long)el));
    dlh_add(strstr(dl_dest_dir_used, "podcasts") ? "podcast" : "web", dl_path);   /* (onglet TELECHARGEMENTS) */
    if (dl_ep_used[0]) { pod_dl_note(dl_ep_used, dl_path); dl_ep_used[0] = 0; }
    if (n > 4 && audio_ext_len(dl_path)) {
        music_add_file(dl_path);
        snprintf(msg, sizeof(msg), "Téléchargé : %s (dans l'onglet MUSIQUE%s)", dl_name, spd);
    } else snprintf(msg, sizeof(msg), "Enregistré : SD:/downloads/%s%s", dl_name, spd[0] ? spd + 1 : "");
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
    t = tr(t);                                          /* v24 : pages de GC-MP3 en anglais */
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
    "Recherche web (DuckDuckGo) | https://html.duckduckgo.com/html/\n"
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
        if (!strcmp(u, "https://www.mojeek.com/") && !strcmp(nm, "Recherche web (Mojeek)")) {   /* v74 : Mojeek refuse (403) */
            static char ddg[] = "https://html.duckduckgo.com/html/", ddgn[] = "Recherche web (DuckDuckGo)";
            u = ddg; nm = ddgn;
        }
        sb_add(s, "<li><a href=\"");
        sb_esc(s, u);
        sb_add(s, "\">");
        sb_esc(s, *nm ? TR(nm) : u);
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
    sb_add(&s, "<html><head><title>Accueil</title></head><body><h1>GC Station Web</h1>");
    sb_add(&s, "<p>Y : taper une adresse ou une recherche. X : revenir à l'accueil. B : page précédente. "
               "Croix : choisir un lien, A : l'ouvrir. Stick principal : la flèche (pointer un lien, A pour l'ouvrir). "
               "Stick C : faire défiler. Depuis le téléphone : « Ouvrir sur la télé ».</p>");
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
    sb_add(&s, "<p>Les favoris sont dans SD:/gc-mp3/favoris.txt (une ligne par site : Nom | adresse).</p>");
    {
        int nck = ck_count();
        sb_add(&s, "<h2>Cookies</h2><p>Les sites peuvent garder des informations (connexion, pr\xC3\xA9" "f\xC3\xA9rences). ");
        if (nck > 0) { snprintf(t, sizeof(t), "<a href=\"gc:cookies\">Effacer les cookies (%d)</a>", nck); sb_add(&s, t); }
        else sb_add(&s, "Aucun cookie pour l'instant.");
        sb_add(&s, "</p>");
    }
    sb_add(&s, "<h2>Téléchargements</h2><p>Un lien vers un fichier (MP3, ZIP...) est enregistré dans SD:/downloads. "
               "Les MP3 apparaissent ensuite dans l'onglet MUSIQUE. La musique continue pendant le téléchargement.</p>"
               "<p>Ce navigateur n'exécute pas le JavaScript : les sites simples marchent, "
               "pas YouTube, Spotify ni SoundCloud.</p></body></html>");
    return s.b;
}

/* recherche archive.org (reponse JSON) -> page HTML de resultats */
static int json_nl = 0;                     /* 1 = garder les retours a la ligne (paroles) */
static int json_string(const char *p, const char *end, char *out, int outsz) {
    int n = 0;
    if (p >= end || *p != '"') return 0;
    p++;
    while (p < end && *p != '"' && n < outsz - 4) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            char c = *p++;
            if (json_nl && (c == 'n' || c == 'r')) out[n++] = (c == 'n') ? '\n' : '\r';
            else if (c == 'n' || c == 't' || c == 'r') out[n++] = ' ';
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

/* --- lecture JSON minimale (jetons dans l'ordre du texte) --- */
enum { JT_OBJ = 1, JT_ARR, JT_STR, JT_PRIM };
typedef struct { u8 type; int start, end; } jtok_t;

static int json_tokens(const char *js, int len, jtok_t *t, int maxt) {
    int n = 0, stack[64], sp = 0, i = 0;
    while (i < len) {
        char c = js[i];
        if (c == '{' || c == '[') {
            if (n >= maxt || sp >= 64) break;
            t[n].type = (c == '{') ? JT_OBJ : JT_ARR;
            t[n].start = i;
            t[n].end = -1;
            stack[sp++] = n++;
            i++;
        } else if (c == '}' || c == ']') {
            if (sp == 0) return -1;
            t[stack[--sp]].end = i + 1;
            i++;
        } else if (c == '"') {
            int s = ++i;
            while (i < len && js[i] != '"') { if (js[i] == '\\') i++; i++; }
            if (i >= len || n >= maxt) break;
            t[n].type = JT_STR; t[n].start = s; t[n].end = i; n++;
            i++;
        } else if (c == '-' || (c >= '0' && c <= '9') || c == 't' || c == 'f' || c == 'n') {
            int s = i;
            while (i < len && js[i] != ',' && js[i] != '}' && js[i] != ']' && !isspace((unsigned char)js[i])) i++;
            if (n >= maxt) break;
            t[n].type = JT_PRIM; t[n].start = s; t[n].end = i; n++;
        } else i++;
    }
    while (sp > 0) t[stack[--sp]].end = len;   /* texte coupe : on ferme ce qui reste ouvert */
    return n;
}

/* jeton suivant apres celui-ci et tout son contenu */
static int jskip(const jtok_t *t, int n, int i) {
    int end = t[i].end;
    int j = i + 1;
    while (j < n && t[j].start < end) j++;
    return j;
}

static int jkey(const char *js, const jtok_t *t, int i, const char *k) {
    int l = (int)strlen(k);
    return t[i].type == JT_STR && t[i].end - t[i].start == l && memcmp(js + t[i].start, k, l) == 0;
}

/* valeur de la cle k dans l'objet obj, -1 si absente */
static int jget(const char *js, const jtok_t *t, int n, int obj, const char *k) {
    if (obj < 0 || t[obj].type != JT_OBJ) return -1;
    int j = obj + 1;
    while (j + 1 < n && t[j].start < t[obj].end) {
        if (jkey(js, t, j, k)) return j + 1;
        j = jskip(t, n, j + 1);
    }
    return -1;
}

static void jstr(const char *js, const jtok_t *t, int i, char *out, int cap) {
    out[0] = 0;
    if (i < 0) return;
    if (t[i].type == JT_STR) json_string(js + t[i].start - 1, js + t[i].end + 1, out, cap);
    else if (t[i].type == JT_PRIM) {
        int l = t[i].end - t[i].start;
        if (l > cap - 1) l = cap - 1;
        memcpy(out, js + t[i].start, l);
        out[l] = 0;
    }
}

/* adresse d'un fichier : tout est encode sauf les caracteres surs et '/' */
static void path_encode(const char *in, char *out, int cap) {
    static const char hx[] = "0123456789ABCDEF";
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n < cap - 4; p++) {
        if (isalnum(*p) || strchr("-_.~/!$'()*,;=:@", *p)) out[n++] = (char)*p;
        else { out[n++] = '%'; out[n++] = hx[*p >> 4]; out[n++] = hx[*p & 15]; }
    }
    out[n] = 0;
}

/* archive.org/metadata/ID (JSON) -> page avec les MP3 de l'element */
static char *archive_meta_to_html(const char *js, int len, const char *url) {
    sbuf_t s = { NULL, 0, 0 };
    char id[200] = "", title[300] = "", creator[200] = "", name[600], size[32], fmt[64], enc[1500], t[200];
    const char *m = strstr(url, "/metadata/");
    if (m) { snprintf(id, sizeof(id), "%s", m + 10); id[strcspn(id, "/?#")] = 0; }
    int maxt = len / 6 + 64;
    if (maxt > 120000) maxt = 120000;
    jtok_t *tk = (jtok_t *)malloc(sizeof(jtok_t) * maxt);
    int n = tk ? json_tokens(js, len, tk, maxt) : -1;
    int meta = -1, files = -1;
    if (n > 0 && tk[0].type == JT_OBJ) {
        meta = jget(js, tk, n, 0, "metadata");
        files = jget(js, tk, n, 0, "files");
        jstr(js, tk, jget(js, tk, n, meta, "title"), title, sizeof(title));
        int cr = jget(js, tk, n, meta, "creator");
        if (cr >= 0 && tk[cr].type == JT_ARR && cr + 1 < n) cr = cr + 1;
        jstr(js, tk, cr, creator, sizeof(creator));
    }
    sb_add(&s, "<html><head><title>");
    sb_esc(&s, title[0] ? title : id);
    sb_add(&s, "</title></head><body><h1>");
    sb_esc(&s, title[0] ? title : id);
    sb_add(&s, "</h1>");
    if (creator[0]) { sb_add(&s, "<p>"); sb_esc(&s, creator); sb_add(&s, "</p>"); }
    sb_add(&s, "<p>Choisis un fichier MP3 : il sera enregistré dans SD:/downloads.</p><ul>");
    int count = 0;
    if (files >= 0 && tk[files].type == JT_ARR) {
        for (int f = files + 1; f < n && tk[f].start < tk[files].end; f = jskip(tk, n, f)) {
            jstr(js, tk, jget(js, tk, n, f, "name"), name, sizeof(name));
            int l = (int)strlen(name);
            if (l < 5 || strcasecmp(name + l - 4, ".mp3") != 0) continue;
            jstr(js, tk, jget(js, tk, n, f, "size"), size, sizeof(size));
            jstr(js, tk, jget(js, tk, n, f, "format"), fmt, sizeof(fmt));
            path_encode(id, enc, sizeof(enc));
            sb_add(&s, "<li><a href=\"https://archive.org/download/");
            sb_add(&s, enc);
            sb_add(&s, "/");
            path_encode(name, enc, sizeof(enc));
            sb_esc(&s, enc);
            sb_add(&s, "\">");
            const char *b = strrchr(name, '/');
            sb_esc(&s, b ? b + 1 : name);
            sb_add(&s, "</a>");
            long sz = atol(size);
            if (sz > 0) {
                snprintf(t, sizeof(t), " (%ld,%ld Mo)", sz / 1048576, (sz % 1048576) * 10 / 1048576);
                sb_add(&s, t);
            }
            sb_add(&s, "</li>");
            count++;
        }
    }
    sb_add(&s, "</ul>");
    if (!count) sb_add(&s, "<p>Aucun fichier MP3 dans cet élément.</p>");
    sb_add(&s, "<p><a href=\"https://archive.org/download/");
    path_encode(id, enc, sizeof(enc));
    sb_add(&s, enc);
    sb_add(&s, "/\">Voir tous les fichiers (page archive.org)</a></p></body></html>");
    free(tk);
    return s.b;
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
            sb_add(&s, "<li><a href=\"https://archive.org/metadata/");
            sb_esc(&s, id);
            sb_add(&s, "\">");
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

/* Affiche une page. data (alloue avec malloc) appartient ensuite a la page. */
static void web_show(unsigned char *data, int len, const char *url, int kind, int latin, int mode) {
    wpage_t np;
    memset(&np, 0, sizeof(np));
    snprintf(np.url, sizeof(np.url), "%s", url);
    img_reset();                            /* nouvelle page : nouvelles images */
    if (kind == WK_ARCHIVE || kind == WK_ARCHMETA) {
        char *h = kind == WK_ARCHIVE ? archive_to_html((const char *)data, len)
                                     : archive_meta_to_html((const char *)data, len, url);
        free(data);
        data = (unsigned char *)h;
        len = h ? (int)strlen(h) : 0;
        kind = WK_HTML;
        latin = 0;
    }
    np.src = data;
    np.srclen = len;
    np.kind = kind;
    np.latin = latin;
    if (!data) { /* plus de memoire */ }
    else if (kind == WK_TEXT) web_layout_text(&np, data, len, latin);
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

    if (!web_no_refresh && pg.refresh[0] && pg.refresh_delay <= 5 && wb_refreshes < 3) {    /* redirection <meta refresh> */
        char next[URL_MAX];
        snprintf(next, sizeof(next), "%s", pg.refresh);
        wb_refreshes++;
        web_go(next, NAV_REPLACE);
    }
}

/* <meta charset=...> au debut de la page */
static void wb_detect_charset(void) {
    if (wb_kind == WK_HTML && !wb_latin && wb_buf) {
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
}

static void web_relayout(void);

/* meta refresh d'une page affichee */
static void web_check_refresh(void) {
    if (pg.refresh[0] && pg.refresh_delay <= 5 && wb_refreshes < 3) {
        char next[URL_MAX];
        snprintf(next, sizeof(next), "%s", pg.refresh);
        wb_refreshes++;
        web_go(next, NAV_REPLACE);
    }
}

/* afficher le debut de la page pendant que la suite arrive */
static void web_partial_show(void) {
    unsigned char *copy = (unsigned char *)malloc(wb_len);
    if (!copy) return;
    memcpy(copy, wb_buf, wb_len);
    wb_detect_charset();
    if (!wb_partial) {
        web_no_refresh = 1;
        web_show(copy, wb_len, wb_url, wb_kind, wb_latin, wb_mode);
        web_no_refresh = 0;
        wb_partial = 1;
        wb_mode = NAV_REPLACE;
    } else {
        free(pg.src);
        pg.src = copy;
        pg.srclen = wb_len;
        web_relayout();
    }
    wb_shown_len = wb_len;
    wb_partial_t = now_ms();
}

static void web_finish_load(void) {
    conn_close(PCONN);
    wb_loading = 0;
    if (!wb_buf) return;
    if (wb_len == 0) { web_note("Page vide", 1); web_cancel_load(); return; }
    wb_detect_charset();
    unsigned char *buf = wb_buf;
    int len = wb_len;
    wb_buf = NULL;
    wb_len = wb_cap = 0;
    if (wb_partial && pg.src && wb_kind == WK_HTML && !strcmp(pg.url, wb_url)) {
        /* deja affichee en partie : on complete sans bouger ce qu'on lit */
        free(pg.src);
        pg.src = buf;
        pg.srclen = len;
        pg.latin = wb_latin;
        web_relayout();
        if (hist_n > 0) snprintf(hist[hist_n - 1].title, sizeof(hist[0].title), "%s", pg.title);
        wb_partial = 0;
        web_check_refresh();
    } else web_show(buf, len, wb_url, wb_kind, wb_latin, wb_mode);
    wb_partial = 0;
    if (wb_trunc) web_note("Page trop grande : seul le début est affiché", 1);
}

/* Remet la page en page (une image a revele sa taille) sans bouger ce qu'on lit. */
static void web_relayout(void) {
    if (!pg.src || pg.kind != WK_HTML) return;
    s32 delta = 0;
    u32 anchor = 0;
    int lo = 0, hi = pg.nitems;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (pg.items[mid].y < pg_scroll) lo = mid + 1; else hi = mid;
    }
    if (lo < pg.nitems) { anchor = pg.items[lo].src; delta = pg.items[lo].y - pg_scroll; }
    wpage_t np;
    memset(&np, 0, sizeof(np));
    snprintf(np.url, sizeof(np.url), "%s", pg.url);
    np.src = pg.src; np.srclen = pg.srclen; np.kind = pg.kind; np.latin = pg.latin;
    pg.src = NULL;
    web_layout_html(&np, np.src, np.srclen, np.latin);
    if (!np.title[0]) snprintf(np.title, sizeof(np.title), "%s", pg.title);
    for (int i = 0; i < np.nfields && i < pg.nfields; i++) {   /* garder ce qui a ete tape / coche / choisi */
        if (np.fields[i].type != pg.fields[i].type) continue;
        memcpy(np.fields[i].value, pg.fields[i].value, sizeof(np.fields[i].value));
        np.fields[i].checked = pg.fields[i].checked;
        if (pg.fields[i].osel >= 0 && pg.fields[i].osel < np.fields[i].nopt) np.fields[i].osel = pg.fields[i].osel;
    }
    int sel = pg_sel;
    wpage_free(&pg);
    pg = np;
    if (anchor) {
        for (int i = 0; i < pg.nitems; i++)
            if (pg.items[i].src >= anchor) { pg_scroll = pg.items[i].y - delta; break; }
    }
    web_scroll_clamp();
    pg_sel = (sel < pg.nlinks) ? sel : -1;
}

/* Ouvre une adresse http(s) : connexion + en-tetes (quelques secondes au plus),
   puis le corps arrive en tache de fond (web_pump). typed = adresse tapee sans
   "https://" : si le site n'accepte pas https, on reessaie en http. */
static void web_fetch(const char *url_in, int mode, int typed) {
    static unsigned char hdr[16384];
    static char req[URL_MAX + 4600];
    char url[URL_MAX], hosthdr[160], cookie[4096];
    static char *post = NULL;                   /* formulaire POST (garde jusqu'a la demande suivante) */
    static int post_len = 0;
    int force_dl = dl_force;
    dl_force = 0;
    free(post);
    post = wb_post; post_len = wb_post_len;
    wb_post = NULL; wb_post_len = 0;
    web_cancel_load();
    wb_partial = 0;
    wb_shown_len = 0;
    crumb("web : %.80s", url_in);
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
        ck_header(u.host, u.path, u.tls, cookie, sizeof(cookie));
        char post_hdr[120] = "";
        if (post) snprintf(post_hdr, sizeof(post_hdr), "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n", post_len);
        int rl = snprintf(req, sizeof(req),
                          "%s %s HTTP/1.1\r\nHost: %s\r\n"
                          "User-Agent: Mozilla/5.0 (compatible; GC-MP3/1.0; Nintendo GameCube)\r\n"
                          "Accept: text/html,application/xhtml+xml,*/*;q=0.8\r\n"
                          "Accept-Language: %s%s\r\n%s%sConnection: close\r\n\r\n",
                          post ? "POST" : "GET", u.path, hosthdr, lang_code(), lang_cur() == SYS_LANG_ENGLISH ? "" : ",en;q=0.5", cookie, post_hdr);
        ui_status("Envoi de la demande...");
        int sent_ok = rl < (int)sizeof(req) && conn_send_all(c, req, rl);
        if (sent_ok && post && post_len > 0) sent_ok = conn_send_all(c, post, post_len);
        if (!sent_ok) { conn_close(c); wb_busy = 0; web_note("Erreur d'envoi de la requête", 1); return; }
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
        ck_from_headers(hdrs, u.host, u.path, u.tls);       /* cookies (aussi sur une redirection) */
        if (code >= 300 && code < 400 && code != 304) {
            const char *loc = header_value(hdrs, "Location");
            char next[URL_MAX];
            conn_close(c);
            if (!loc || !url_join(url, loc, next, sizeof(next))) { web_note("Redirection invalide", 1); return; }
            snprintf(url, sizeof(url), "%s", next);
            typed = 0;
            if (code != 307 && code != 308) { free(post); post = NULL; post_len = 0; }   /* POST -> GET */
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
        else if (strstr(ctype, "json") && strcmp(u.host, "archive.org") == 0 && strncmp(u.path, "/metadata/", 10) == 0) kind = WK_ARCHMETA;
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
        if (kind < 0 && !force_dl && !ci_find(disp, "attachment") && au_is_audio(ctype, url)) {   /* ecouter ou telecharger ? */
            au_offer(body, body_len, ctype, disp, url, chunked ? -1 : clen, chunked, header_value(hdrs, "icy-name") != NULL || header_value(hdrs, "icy-metaint") != NULL);
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
        if (code >= 400 && ws_q[0]) {                       /* v74 : moteur de recherche qui refuse : le suivant */
            for (int e = 0; ws_engines[e]; e++) {
                size_t el = strlen(ws_engines[e]);
                if (strncmp(url, ws_engines[e], el) || strcmp(url + el, ws_q) || !ws_engines[e + 1]) continue;
                char next[URL_MAX];
                snprintf(next, sizeof(next), "%s%s", ws_engines[e + 1], ws_q);
                crumb("web : moteur %d refuse (%d), essai suivant", e, code);
                web_cancel_load();
                web_fetch(next, mode, 0);
                return;
            }
        }
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

// ---------- Petit client HTTP en tache de fond (images, pochettes) ----------
/* Avance par petites etapes a chaque image affichee : rien ne bloque l'ecran
   longtemps. Garde la connexion ouverte pour la demande suivante au meme site. */
enum { HF_IDLE, HF_HANDSHAKE, HF_HEAD, HF_BODY, HF_DONE, HF_FAIL, HF_CONNECT };   /* v83 : HF_CONNECT = connexion en tache de fond */
typedef struct {
    conn_t c;
    int st, alive, reused, close_after, redirs, port, tls, code;
    int image;                              /* 1 = refuser ce qui n'est pas une image lisible */
    int maxbytes;
    const char *accept;
    char host[128];
    char url[URL_MAX];
    char referer[URL_MAX];
    unsigned char hdr[8192];
    int hlen;
    int hskip;                              /* v77 : ligne d'en-tete trop longue en cours d'oubli (jusqu'a la fin de ligne) */
    unsigned char *buf;
    int len, cap;
    long expect;
    dechunk_t dc;
    u64 t0, last_use;
    int job;                                /* v83 : demande au fil des connexions (+1 ; 0 = aucune) */
} hfetch_t;

/* v77 : en-tetes plus gros que le tampon (GitHub : 11 Ko, dont une tres longue
   "Content-Security-Policy") : on oublie les lignes dont on ne se sert pas.
   Retourne 1 si de la place a ete faite. */
static char *crlf_in(char *p, int n) { for (int i = 0; i + 1 < n; i++) if (p[i] == '\r' && p[i + 1] == '\n') return p + i; return NULL; }
static int hf_hdr_trim(hfetch_t *h) {
    static const char *const keep[] = { "Location:", "Content-Length:", "Content-Type:", "Transfer-Encoding:", "Connection:", NULL };
    char *b = (char *)h->hdr;
    int n = h->hlen;
    char *first = crlf_in(b, n);
    if (!first) return 0;                               /* (ligne d'etat demesuree : ce n'est pas du HTTP) */
    int w = (int)(first - b) + 2, r = w;
    while (r < n) {
        char *e = crlf_in(b + r, n - r);
        if (!e) break;
        int ll = (int)(e - (b + r)) + 2, k = 0;
        for (int i = 0; keep[i] && !k; i++) k = ll >= (int)strlen(keep[i]) && !strncasecmp(b + r, keep[i], strlen(keep[i]));
        if (k) { memmove(b + w, b + r, ll); w += ll; }
        r += ll;
    }
    int tail = n - r;
    if (w + tail < n) { memmove(b + w, b + r, tail); h->hlen = w + tail; b[h->hlen] = 0; return 1; }
    if (tail > 0) { h->hlen = w; b[w] = 0; h->hskip = 1; return 1; }   /* une seule ligne enorme : oubliee jusqu'a sa fin */
    return 0;
}

static void hf_close(hfetch_t *h) { conn_close(&h->c); h->alive = 0; }
static void hf_drop(hfetch_t *h) { free(h->buf); h->buf = NULL; h->len = h->cap = 0; }
static void hf_fail(hfetch_t *h) { hf_drop(h); h->st = HF_FAIL; }
static void hf_abort(hfetch_t *h) {
    if (h->job) { ac_cancel(h->job); h->job = 0; }
    if (h->st == HF_HANDSHAKE || h->st == HF_HEAD || h->st == HF_BODY) hf_close(h);
    hf_drop(h);
    h->st = HF_IDLE;
}

static int hf_send(hfetch_t *h) {
    urlp_t u;
    char req[URL_MAX * 2 + 500], hosthdr[160], ref[URL_MAX + 16] = "";
    if (!url_parse(h->url, &u)) return 0;
    if (strncmp(h->referer, "http", 4) == 0) snprintf(ref, sizeof(ref), "Referer: %s\r\n", h->referer);
    if (u.port == (u.tls ? 443 : 80)) snprintf(hosthdr, sizeof(hosthdr), "%s", u.host);
    else snprintf(hosthdr, sizeof(hosthdr), "%s:%d", u.host, u.port);
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\nHost: %s\r\n"
                      "User-Agent: GC-MP3/1.0 (Nintendo GameCube; https://github.com/antho13otz/gc-mp3)\r\n"
                      "Accept: %s\r\n%sConnection: keep-alive\r\n\r\n", u.path, hosthdr,
                      h->accept ? h->accept : "*/*", ref);
    if (rl >= (int)sizeof(req) || !conn_send_all(&h->c, req, rl)) return 0;
    h->hlen = 0;
    h->hskip = 0;
    h->st = HF_HEAD;
    h->t0 = now_ms();
    return 1;
}

/* Demande h->url (en reutilisant la connexion si c'est le meme site). */
static void hf_request(hfetch_t *h) {
    urlp_t u;
    if (!url_parse(h->url, &u)) { hf_fail(h); return; }
    h->reused = h->alive && h->c.fd >= 0 && strcmp(u.host, h->host) == 0 && u.port == h->port && u.tls == h->tls;
    if (!h->reused) {
        const char *err = NULL;
        hf_close(h);
        if (h->job) { ac_cancel(h->job); h->job = 0; }
        int id = ac_submit(u.host, u.port, u.tls);             /* v83 : en tache de fond (l'ecran continue) */
        if (id) {
            snprintf(h->host, sizeof(h->host), "%s", u.host);
            h->port = u.port;
            h->tls = u.tls;
            h->job = id;
            h->st = HF_CONNECT;
            h->t0 = now_ms();
            return;
        }
        if (!conn_tcp(&h->c, u.host, u.port, &err, NULL, 0)) { hf_fail(h); return; }
        snprintf(h->host, sizeof(h->host), "%s", u.host);
        h->port = u.port;
        h->tls = u.tls;
        h->alive = 1;
        if (u.tls) {
            if (!tls_global_init()) { hf_close(h); hf_fail(h); return; }
            TLS_LOCK();
            mbedtls_ssl_init(&h->c.ssl);
            h->c.ssl_inited = 1;
            int su = mbedtls_ssl_setup(&h->c.ssl, &tls_conf) != 0 || mbedtls_ssl_set_hostname(&h->c.ssl, u.host) != 0;
            TLS_UNLOCK();
            if (su) { hf_close(h); hf_fail(h); return; }
            snprintf(h->c.host, sizeof(h->c.host), "%s:%d", u.host, u.port);
            h->c.sc_used = tls_sc_use(&h->c.ssl, h->c.host);
            TLS_LOCK();
            mbedtls_ssl_set_bio(&h->c.ssl, &h->c.fd, bio_send, bio_recv, NULL);
            TLS_UNLOCK();
            h->c.tls = 1;
            h->st = HF_HANDSHAKE;
            h->t0 = now_ms();
            return;
        }
    }
    if (!hf_send(h)) {
        if (h->reused) { hf_close(h); hf_request(h); return; }  /* connexion gardee mais fermee par le site */
        hf_close(h);
        hf_fail(h);
    }
}

static void hf_start(hfetch_t *h, const char *url, const char *referer) {
    if (h->job) { ac_cancel(h->job); h->job = 0; }
    hf_drop(h);
    h->redirs = 0;
    h->code = 0;
    snprintf(h->url, sizeof(h->url), "%s", url);
    snprintf(h->referer, sizeof(h->referer), "%s", referer ? referer : "");
    if (h->alive && now_ms() - h->last_use > 15000) hf_close(h);     /* vieille connexion */
    h->last_use = now_ms();
    hf_request(h);
}

static void hf_done(hfetch_t *h) {
    if (h->close_after) hf_close(h);
    h->last_use = now_ms();
    h->st = HF_DONE;
}

static void hf_pump(hfetch_t *h) {
    if (h->st == HF_CONNECT) {                               /* v83 : connexion prete ? */
        ac_sweep();
        acjob_t *j = h->job ? &ac_job[h->job - 1] : NULL;
        if (j && j->st != 3) {
            if (now_ms() - h->t0 > 40000) { ac_cancel(h->job); h->job = 0; hf_fail(h); }
            return;
        }
        int ok = j && j->ok;
        if (ok) {
            h->c = j->c;                                     /* (la connexion change de place : le TLS suit) */
            if (h->c.tls) { TLS_LOCK(); mbedtls_ssl_set_bio(&h->c.ssl, &h->c.fd, bio_send, bio_recv, NULL); TLS_UNLOCK(); }
            j->c.fd = -1; j->c.ssl_inited = 0; j->c.tls = 0;
        }
        if (j) j->st = 0;
        h->job = 0;
        if (!ok) { hf_fail(h); return; }
        h->alive = 1;
        if (!hf_send(h)) { hf_close(h); hf_fail(h); }
        return;
    }
    if (h->st == HF_HANDSHAKE) {
        if (!tls_try()) return;                              /* v83 : (le fil des connexions calcule) */
        int r = mbedtls_ssl_handshake(&h->c.ssl);
        TLS_UNLOCK();
        if (r == 0) { tls_sc_done(&h->c); if (!hf_send(h)) { hf_close(h); hf_fail(h); } return; }
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) { tls_sc_put(&h->c.ssl, h->c.host); return; }
#endif
        if ((r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) || now_ms() - h->t0 > 15000) {
            int retry = h->c.sc_used && now_ms() - h->t0 <= 15000;
            if (retry) tls_sc_drop(h->c.host);            /* session refusee : sans elle */
            hf_close(h);
            if (retry) { hf_request(h); return; }
            hf_fail(h);
        }
        return;
    }
    if (h->st == HF_HEAD) {
        for (;;) {
            if (h->hlen >= (int)sizeof(h->hdr) - 1 && !hf_hdr_trim(h)) { hf_close(h); hf_fail(h); return; }
            int before = h->hlen;
            int r = conn_recv(&h->c, h->hdr + h->hlen, (int)sizeof(h->hdr) - 1 - h->hlen);
            if (r == -EAGAIN || r == -EWOULDBLOCK) {
                if (now_ms() - h->t0 > 15000) { hf_close(h); hf_fail(h); }
                return;
            }
            if (r <= 0) {
                int retry = h->reused && h->hlen == 0;
                hf_close(h);
                if (retry) { hf_request(h); return; }                 /* le site avait ferme la connexion */
                hf_fail(h);
                return;
            }
            h->hlen += r;
            if (h->hskip) {                                          /* v77 : fin de la ligne trop longue ? */
                char *nl = (char *)memchr(h->hdr + before, '\n', h->hlen - before);
                if (!nl) { h->hlen = before; h->hdr[before] = 0; continue; }
                int cut = (int)(nl - (char *)h->hdr) + 1;
                memmove(h->hdr + before, h->hdr + cut, h->hlen - cut);
                h->hlen -= cut - before;
                h->hskip = 0;
            }
            h->hdr[h->hlen] = 0;
            char *e = strstr((char *)h->hdr, "\r\n\r\n");
            if (!e) continue;
            int body_off = (int)(e - (char *)h->hdr) + 4;
            e[2] = 0;
            int code = 0, minor = 0;
            if (sscanf((char *)h->hdr, "HTTP/1.%d %d", &minor, &code) != 2) { hf_close(h); hf_fail(h); return; }
            h->code = code;
            char *hd = (char *)h->hdr;
            const char *v = header_value(hd, "Connection");
            h->close_after = (minor == 0) || (v && ci_find((char *)v, "close"));
            v = header_value(hd, "Transfer-Encoding");
            int chunked = v && ci_find((char *)v, "chunked");
            v = header_value(hd, "Content-Length");
            long clen = (v && !chunked) ? atol(v) : -1;
            if (clen < 0 && !chunked) h->close_after = 1;            /* fin = fermeture */
            if (code >= 300 && code < 400) {
                v = header_value(hd, "Location");
                char next[URL_MAX];
                hf_close(h);
                if (!v || h->redirs >= 5 || !url_join(h->url, v, next, sizeof(next))) { hf_fail(h); return; }
                h->redirs++;
                snprintf(h->url, sizeof(h->url), "%s", next);
                hf_request(h);
                return;
            }
            v = header_value(hd, "Content-Type");
            if (code != 200 || clen > h->maxbytes ||
                (h->image && v && (ci_find((char *)v, "text/") || ci_find((char *)v, "svg")))) {
                hf_close(h);
                hf_fail(h);
                return;
            }
            h->cap = clen > 0 ? (int)clen + 16 : 65536;
            h->buf = (unsigned char *)malloc(h->cap);
            if (!h->buf) { hf_close(h); hf_fail(h); return; }
            h->len = 0;
            memset(&h->dc, 0, sizeof(h->dc));
            h->dc.on = chunked;
            h->expect = clen;
            int bl = h->hlen - body_off;
            if (bl > 0) {
                memcpy(h->buf, h->hdr + body_off, bl);
                h->len = chunked ? dechunk(&h->dc, h->buf, bl) : bl;
            }
            h->st = HF_BODY;
            h->t0 = now_ms();
            if ((h->expect >= 0 && h->len >= h->expect) || (h->dc.on && h->dc.st == 3)) hf_done(h);
            return;
        }
    }
    if (h->st == HF_BODY) {
        static unsigned char tmp[16384];
        int got = 0;
        while (got < 256 * 1024) {
            int r = conn_recv(&h->c, tmp, sizeof(tmp));
            if (r > 0) {
                int m = h->dc.on ? dechunk(&h->dc, tmp, r) : r;
                if (h->len + m > h->maxbytes) { hf_close(h); hf_fail(h); return; }
                if (h->len + m > h->cap) {
                    int nc = h->cap * 2;
                    while (nc < h->len + m) nc *= 2;
                    if (nc > h->maxbytes) nc = h->maxbytes;
                    unsigned char *nb = (unsigned char *)realloc(h->buf, nc);
                    if (!nb) { hf_close(h); hf_fail(h); return; }
                    h->buf = nb;
                    h->cap = nc;
                }
                memcpy(h->buf + h->len, tmp, m);
                h->len += m;
                got += r;
                h->t0 = now_ms();
                if ((h->expect >= 0 && h->len >= h->expect) || (h->dc.on && h->dc.st == 3)) { hf_done(h); return; }
                continue;
            }
            if (r == -EAGAIN || r == -EWOULDBLOCK) break;
            hf_close(h);                                                /* fin du fichier */
            if ((h->expect > 0 && h->len < h->expect) || h->len == 0) { hf_fail(h); return; }
            hf_done(h);
            return;
        }
        if (now_ms() - h->t0 > 20000) { hf_close(h); hf_fail(h); }
    }
}

// ---------- Radios HLS (listes .m3u8 decoupees en segments) ----------
/* La liste donne des segments de quelques secondes (MPEG-TS, ou AAC / MP3 bruts)
   qu'on telecharge l'un apres l'autre. Le son en est extrait puis decode comme
   une radio normale. Les flux chiffres et le format fMP4 ne sont pas geres. */
#define HLS_MAXSEG 48
static hfetch_t hhf = { .c = { -1, 0, 0 } };
enum { HS_IDLE, HS_PLAYLIST, HS_SEGMENT };
static int hs_st = HS_IDLE;
static char hs_url[URL_MAX];                /* liste des segments (media playlist) */
static char *hs_seg[HLS_MAXSEG];
static int hs_nseg = 0, hs_target = 6, hs_endlist = 0, hs_fails = 0, hs_codec_set = 0;
static long long hs_first = 0, hs_next = -1;
static u64 hs_reload_at = 0;
static unsigned char *hs_pend = NULL;       /* son extrait, en attente de place dans cbuf */
static int hs_plen = 0, hs_ppos = 0;
static int ts_pmt = -1, ts_apid = -1;

static void hls_free_segs(void) {
    for (int i = 0; i < hs_nseg; i++) free(hs_seg[i]);
    hs_nseg = 0;
}

static void hls_stop(void) {
    if (hs_st != HS_IDLE) hf_abort(&hhf);
    hf_drop(&hhf);
    hf_close(&hhf);
    hls_free_segs();
    free(hs_pend);
    hs_pend = NULL;
    hs_plen = hs_ppos = 0;
    hs_st = HS_IDLE;
    hls_on = 0;
}

/* attribut d'une ligne #EXT-X-STREAM-INF (ex. BANDWIDTH=128000) */
static long hls_attr(const char *line, const char *name) {
    const char *p = line;
    size_t nl = strlen(name);
    while ((p = strstr(p, name)) != NULL) {
        if ((p == line || p[-1] == ':' || p[-1] == ',') && p[nl] == '=') return atol(p + nl + 1);
        p += nl;
    }
    return -1;
}

/* Liste principale : choisit la variante (son seul de preference, debit raisonnable) */
static int hls_pick_variant(const char *pl, const char *base, char *out, int cap) {
    char line[1024], best[URL_MAX] = "";
    long bestbw = -1;
    int bestscore = -1, want_uri = 0, score = 0;
    long bw = 0;
    const char *p = pl;
    while (*p) {
        int n = (int)strcspn(p, "\r\n");
        int m = n < (int)sizeof(line) - 1 ? n : (int)sizeof(line) - 1;
        memcpy(line, p, m);
        line[m] = 0;
        p += n;
        while (*p == '\r' || *p == '\n') p++;
        if (!strncmp(line, "#EXT-X-STREAM-INF:", 18)) {
            bw = hls_attr(line, "BANDWIDTH");
            if (bw < 0) bw = 0;
            score = 0;
            char *cd = strstr(line, "CODECS=\"");
            if (cd) {
                int audio_ok = strstr(cd, "mp4a") || strstr(cd, "mp3") || strstr(cd, ".6b");
                int video = strstr(cd, "avc") || strstr(cd, "hvc") || strstr(cd, "hev");
                score = audio_ok ? (video ? 1 : 3) : 0;
            } else score = 2;
            if (bw > 0 && bw <= 200000) score += 2;          /* la GameCube preferera un petit debit */
            want_uri = 1;
        } else if (want_uri && line[0] && line[0] != '#') {
            int better = score > bestscore ||
                         (score == bestscore && ((bw <= 200000 && bw > bestbw) || (bestbw > 200000 && bw < bestbw)));
            if (better) {
                char abs[URL_MAX];
                if (url_join(base, line, abs, sizeof(abs))) { snprintf(best, sizeof(best), "%s", abs); bestscore = score; bestbw = bw; }
            }
            want_uri = 0;
        }
    }
    if (!best[0]) return 0;
    snprintf(out, cap, "%s", best);
    return 1;
}

/* Liste des segments : retourne 0 si illisible, -1 si non geree (chiffree / fMP4) */
static int hls_parse_media(const char *pl, const char *base) {
    char line[1024];
    long long seq = 0;
    int have_seq = 0;
    hls_free_segs();
    hs_endlist = 0;
    const char *p = pl;
    while (*p) {
        int n = (int)strcspn(p, "\r\n");
        int m = n < (int)sizeof(line) - 1 ? n : (int)sizeof(line) - 1;
        memcpy(line, p, m);
        line[m] = 0;
        p += n;
        while (*p == '\r' || *p == '\n') p++;
        if (!strncmp(line, "#EXT-X-TARGETDURATION:", 22)) { hs_target = atoi(line + 22); if (hs_target < 1) hs_target = 1; if (hs_target > 30) hs_target = 30; }
        else if (!strncmp(line, "#EXT-X-MEDIA-SEQUENCE:", 22)) { seq = atoll(line + 22); have_seq = 1; }
        else if (!strncmp(line, "#EXT-X-ENDLIST", 14)) hs_endlist = 1;
        else if (!strncmp(line, "#EXT-X-KEY:", 11) && !strstr(line, "METHOD=NONE")) return -1;
        else if (!strncmp(line, "#EXT-X-MAP:", 11)) return -2;
        else if (line[0] && line[0] != '#') {
            char abs[URL_MAX];
            if (hs_nseg >= HLS_MAXSEG) {                     /* on garde les derniers */
                free(hs_seg[0]);
                memmove(hs_seg, hs_seg + 1, sizeof(char *) * (HLS_MAXSEG - 1));
                hs_nseg--;
                seq++;
            }
            if (url_join(base, line, abs, sizeof(abs))) hs_seg[hs_nseg++] = strdup(abs);
            else hs_seg[hs_nseg++] = strdup("");
        }
    }
    (void)have_seq;
    hs_first = seq;
    return hs_nseg > 0;
}

static void hs_append(const unsigned char *d, int n) {
    if (n <= 0) return;
    if (hs_ppos > 0) {                                  /* on retire ce qui est deja passe */
        memmove(hs_pend, hs_pend + hs_ppos, hs_plen - hs_ppos);
        hs_plen -= hs_ppos;
        hs_ppos = 0;
    }
    unsigned char *nb = (unsigned char *)realloc(hs_pend, hs_plen + n);
    if (!nb) return;
    hs_pend = nb;
    memcpy(hs_pend + hs_plen, d, n);
    hs_plen += n;
}

/* MPEG-TS : garde la charge utile du flux audio (PES) */
static int ts_demux(const unsigned char *b, int len) {
    int got = 0, i = 0, atype = 0;
    while (i + 188 <= len) {
        if (b[i] != 0x47) { i++; continue; }
        const unsigned char *p = b + i;
        i += 188;
        int pid = ((p[1] & 0x1f) << 8) | p[2], pusi = p[1] & 0x40, afc = (p[3] >> 4) & 3;
        int off = 4;
        if (afc & 2) off += 1 + p[4];
        if (!(afc & 1) || off >= 188) continue;
        const unsigned char *pl = p + off;
        int n = 188 - off;
        if (pid == 0 && pusi) {                                  /* PAT */
            int ptr = pl[0];
            if (1 + ptr + 8 > n) continue;
            const unsigned char *t = pl + 1 + ptr;
            int sl = ((t[1] & 0x0f) << 8) | t[2];
            int end = 3 + sl - 4;
            if (end > n - 1 - ptr) end = n - 1 - ptr;
            for (int k = 8; k + 4 <= end; k += 4) {
                int prog = (t[k] << 8) | t[k + 1];
                if (prog != 0) { ts_pmt = ((t[k + 2] & 0x1f) << 8) | t[k + 3]; break; }
            }
        } else if (pid == ts_pmt && pusi) {                      /* PMT */
            int ptr = pl[0];
            if (1 + ptr + 12 > n) continue;
            const unsigned char *t = pl + 1 + ptr;
            int sl = ((t[1] & 0x0f) << 8) | t[2];
            int pil = ((t[10] & 0x0f) << 8) | t[11];
            int end = 3 + sl - 4;
            if (end > n - 1 - ptr) end = n - 1 - ptr;
            for (int k = 12 + pil; k + 5 <= end;) {
                int st = t[k], epid = ((t[k + 1] & 0x1f) << 8) | t[k + 2], eil = ((t[k + 3] & 0x0f) << 8) | t[k + 4];
                if (st == 0x0F || st == 0x03 || st == 0x04) { ts_apid = epid; atype = st; break; }
                k += 5 + eil;
            }
        } else if (pid == ts_apid) {
            if (pusi) {
                if (n < 9 || pl[0] != 0 || pl[1] != 0 || pl[2] != 1) continue;
                int h = 9 + pl[8];
                if (h >= n) continue;
                pl += h; n -= h;
            }
            hs_append(pl, n);
            got += n;
        }
    }
    (void)atype;
    return got;
}

/* Segment recu : extrait le son. 0 = format inconnu, -1 = fMP4 */
static int hls_demux(const unsigned char *b, int len) {
    if (len >= 8 && (!memcmp(b + 4, "ftyp", 4) || !memcmp(b + 4, "styp", 4) || !memcmp(b + 4, "moof", 4) || !memcmp(b + 4, "sidx", 4)))
        return -1;
    int start = hs_plen;
    if (len >= 376 && b[0] == 0x47 && b[188] == 0x47) {
        if (ts_demux(b, len) <= 0) return 0;
    } else {
        int off = 0;
        while (len - off > 10 && !memcmp(b + off, "ID3", 3))        /* horodatage ID3 des segments AAC */
            off += 10 + (int)synchsafe(b + off + 6);
        if (off >= len) return 0;
        hs_append(b + off, len - off);
    }
    if (!hs_codec_set && hs_plen > start + 2) {
        const unsigned char *d = hs_pend + start;
        int n = hs_plen - start, i = 0;
        while (i + 1 < n && !(d[i] == 0xFF && (d[i + 1] & 0xE0) == 0xE0)) i++;
        if (i + 1 >= n) return 0;
        stream_codec = ((d[i + 1] & 0xF6) == 0xF0) ? 1 : 0;      /* ADTS (AAC) ou MP3 */
        hs_codec_set = 1;
    }
    return 1;
}

static void hls_fetch_playlist(void) {
    hhf.image = 0;
    hhf.maxbytes = 512 * 1024;
    hhf.accept = "application/vnd.apple.mpegurl,*/*";
    hf_start(&hhf, hs_url, NULL);
    hs_st = HS_PLAYLIST;
}

/* Liste .m3u8 recue par http_open (url = son adresse) */
static int hls_begin(const char *url, const char *pl) {
    hls_stop();
    hls_on = 1;
    hs_fails = 0;
    hs_codec_set = 0;
    hs_next = -1;
    ts_pmt = ts_apid = -1;
    if (strstr(pl, "#EXT-X-STREAM-INF")) {
        if (!hls_pick_variant(pl, url, hs_url, sizeof(hs_url))) { hls_stop(); set_error("Liste HLS vide"); return 0; }
        hls_fetch_playlist();
        return 1;
    }
    snprintf(hs_url, sizeof(hs_url), "%s", url);
    int r = hls_parse_media(pl, url);
    if (r <= 0) {
        hls_stop();
        set_error(r == -1 ? "Radio HLS chiffr\xC3\xA9" "e : non g\xC3\xA9r\xC3\xA9" "e" :
                  r == -2 ? "Radio HLS au format fMP4 : non g\xC3\xA9r\xC3\xA9" "e" : "Liste HLS illisible");
        return 0;
    }
    hs_next = hs_endlist ? hs_first : hs_first + (hs_nseg > 3 ? hs_nseg - 3 : 0);
    hs_reload_at = now_ms() + (u64)hs_target * 1000;
    return 1;
}

static void hls_pump(void) {
    if (!hls_on) return;
    /* 1. le son extrait entre dans cbuf, au rythme ou il est decode */
    if (hs_pend && hs_ppos < hs_plen) {
        int room = cbuf_free() - 4096;
        int n = hs_plen - hs_ppos;
        if (n > room) n = room;
        if (n > 0) {
            stream_audio(hs_pend + hs_ppos, n);
            hs_ppos += n;
            last_data_ms = now_ms();
        }
        if (hs_ppos >= hs_plen) { free(hs_pend); hs_pend = NULL; hs_plen = hs_ppos = 0; }
    }
    if (hs_fails >= 4) { stream_closed = 1; hls_stop(); return; }   /* reconnexion par stream_pump */

    if (hs_st == HS_PLAYLIST) {
        hf_pump(&hhf);
        if (hhf.st == HF_FAIL) { hs_fails++; hs_st = HS_IDLE; hs_reload_at = now_ms() + 2000; hf_drop(&hhf); return; }
        if (hhf.st != HF_DONE) return;
        hhf.st = HF_IDLE;
        char *txt = (char *)malloc(hhf.len + 1);
        int r = 0;
        if (txt) {
            memcpy(txt, hhf.buf, hhf.len);
            txt[hhf.len] = 0;
            if (strstr(txt, "#EXT-X-STREAM-INF")) {             /* encore une liste principale */
                char v[URL_MAX];
                if (hls_pick_variant(txt, hs_url, v, sizeof(v))) { snprintf(hs_url, sizeof(hs_url), "%s", v); free(txt); hf_drop(&hhf); hls_fetch_playlist(); return; }
            } else r = hls_parse_media(txt, hs_url);
            free(txt);
        }
        hf_drop(&hhf);
        hs_st = HS_IDLE;
        if (r < 0) {
            set_error(r == -1 ? "Radio HLS chiffr\xC3\xA9" "e : non g\xC3\xA9r\xC3\xA9" "e" : "Radio HLS au format fMP4 : non g\xC3\xA9r\xC3\xA9" "e");
            hls_stop();
            stream_closed = 1;
            reconnects = 99;                                     /* inutile de reessayer */
            return;
        }
        if (r == 0) { hs_fails++; hs_reload_at = now_ms() + 2000; return; }
        hs_fails = 0;
        long long last = hs_first + hs_nseg - 1;
        if (hs_next < 0) hs_next = hs_endlist ? hs_first : (last - 2 > hs_first ? last - 2 : hs_first);
        if (hs_next < hs_first) hs_next = hs_first;                /* trop de retard */
        hs_reload_at = now_ms() + (u64)(hs_next > last ? hs_target * 500 : hs_target * 1000);
        return;
    }
    if (hs_st == HS_SEGMENT) {
        hf_pump(&hhf);
        if (hhf.st == HF_FAIL) { hs_fails++; hs_next++; hs_st = HS_IDLE; hf_drop(&hhf); return; }
        if (hhf.st != HF_DONE) return;
        hhf.st = HF_IDLE;
        int r = hls_demux(hhf.buf, hhf.len);
        hf_drop(&hhf);
        hs_st = HS_IDLE;
        hs_next++;
        if (r < 0) {
            set_error("Radio HLS au format fMP4 : non g\xC3\xA9r\xC3\xA9" "e");
            hls_stop();
            stream_closed = 1;
            reconnects = 99;
            return;
        }
        if (r == 0) hs_fails++; else hs_fails = 0;
        return;
    }
    /* HS_IDLE : segment suivant, ou relire la liste */
    if (hs_next < 0) return;
    long long last = hs_first + hs_nseg - 1;
    if (hs_next >= hs_first && hs_next <= last) {
        if (hs_plen - hs_ppos > 256 * 1024) return;               /* assez d'avance */
        if (f_count > FIFO_SIZE - 48000 * 2) return;
        const char *u = hs_seg[hs_next - hs_first];
        if (!u || !u[0]) { hs_next++; return; }
        hhf.image = 0;
        hhf.maxbytes = 4 * 1024 * 1024;
        hhf.accept = "*/*";
        hf_start(&hhf, u, NULL);
        hs_st = HS_SEGMENT;
        return;
    }
    if (hs_next > last && hs_endlist) {
        if (!hs_pend) stream_closed = 1;                          /* fin d'une liste fixe */
        return;
    }
    if (now_ms() >= hs_reload_at) hls_fetch_playlist();
}

// ---------- Chargement des images de la page ----------
/* Le decodage JPEG/PNG/GIF se fait dans un thread de faible priorite qui
   travaille pendant que le programme attend l'image suivante de l'ecran. */
enum { IL_IDLE, IL_FETCH, IL_DECODE };
static hfetch_t ihf = { .c = { -1, 0, 0 } };
static int il_state = IL_IDLE, il_img = -1;
static u32 il_gen = 0;

/* travail du thread de decodage */
static volatile int dj_state = 0;           /* 0 libre, 1 a faire, 2 fini */
static unsigned char *dj_in = NULL;
static int dj_len = 0, dj_aw = 0, dj_ah = 0, dj_maxw = 0;
static u16 *dj_tex = NULL;
static int dj_tw, dj_th, dj_iw, dj_ih, dj_nw, dj_nh, dj_ok;
static lwp_t dec_thread_h;
static u8 dec_stack[64 * 1024] ATTRIBUTE_ALIGN(32);
static int dec_started = 0;
static void *dec_thread_fn(void *arg);
static volatile int gj_state = 0;           /* v92 : lecture pour l'onglet JEUX (gclib.c) : 0 libre, 1 a faire, 2 fini */
static volatile int gj_hold = 0;            /* v96 : plus de nouvelle lecture en fond (la carte SD va etre demontee) */
static void bg_quiesce(void);
static void gl_job_run(void);
static int dj_photo = 0;                    /* 1 = photo de l'economiseur (RGB, ajustee a dj_aw x dj_ah) */
static int dj_owner = 0;                    /* qui attend le resultat : 1 web, 2 economiseur, 3 photos, 4 video */
#define PHOTO_MAX_PIXELS (1920 * 1088)

static int dec_ensure(void) {
    if (dec_started) return 1;
    if (LWP_CreateThread(&dec_thread_h, dec_thread_fn, NULL, dec_stack, sizeof(dec_stack), 20) < 0) return 0;
    dec_started = 1;
    return 1;
}

static u16 rgb5a3(int r, int g, int b, int a) {
    if (a >= 0xE0) return (u16)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    return (u16)(((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
}

/* WebP (images du web) : decodeur libwebp */
static int is_webp(const unsigned char *d, int n) {
    return n >= 16 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4);
}

static void img_decode_job(void) {
    int w, h, c;
    dj_ok = 0;
    dj_nw = dj_nh = 0;
    dj_tex = NULL;
    int webp = is_webp(dj_in, dj_len);
    if (webp) { if (!WebPGetInfo(dj_in, (size_t)dj_len, &w, &h) || w <= 0 || h <= 0) return; }
    else if (!stbi_info_from_memory(dj_in, dj_len, &w, &h, &c) || w <= 0 || h <= 0) return;
    dj_nw = w;
    dj_nh = h;
    int dw, dh;
    if (dj_photo) {
        if ((long)w * h > PHOTO_MAX_PIXELS) return;
        dw = w; dh = h;                                     /* tient dans dj_aw x dj_ah */
        if (dw > dj_aw) { dh = (int)((long)dh * dj_aw / dw); dw = dj_aw; }
        if (dh > dj_ah) { dw = (int)((long)dw * dj_ah / dh); dh = dj_ah; }
    } else {
        if ((long)w * h > IMG_MAX_PIXELS) return;
        img_box(dj_aw, dj_ah, w, h, dj_maxw, &dw, &dh);
    }
    if (dw < 1 || dh < 1) return;
    if (dw > w) { dh = (int)((long)dh * w / dw); dw = w; }   /* jamais plus grand que l'original */
    if (dh > h) { dw = (int)((long)dw * h / dh); dh = h; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ch = dj_photo ? 3 : 4;
    unsigned char *px;
    if (webp) {
        int ww = 0, hh = 0;
        px = dj_photo ? WebPDecodeRGB(dj_in, (size_t)dj_len, &ww, &hh) : WebPDecodeRGBA(dj_in, (size_t)dj_len, &ww, &hh);
        if (px && (ww != w || hh != h)) { WebPFree(px); return; }
    } else px = stbi_load_from_memory(dj_in, dj_len, &w, &h, &c, ch);
    if (!px) return;
    int tw = (dw + 3) & ~3, th = (dh + 3) & ~3;
    u16 *tex = (u16 *)memalign(32, (size_t)tw * th * 2);
    if (!tex) { if (webp) WebPFree(px); else stbi_image_free(px); return; }
    memset(tex, 0, (size_t)tw * th * 2);
    for (int y = 0; y < dh; y++) {
        int sy0 = (int)((long)y * h / dh), sy1 = (int)((long)(y + 1) * h / dh);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        int ys = (sy1 - sy0 + 3) / 4;                      /* au plus 4x4 points par pixel */
        for (int x = 0; x < dw; x++) {
            int sx0 = (int)((long)x * w / dw), sx1 = (int)((long)(x + 1) * w / dw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            int xs = (sx1 - sx0 + 3) / 4;
            unsigned r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = sy0; sy < sy1; sy += ys)
                for (int sx = sx0; sx < sx1; sx += xs) {
                    const unsigned char *q = px + ((size_t)sy * w + sx) * ch;
                    r += q[0]; g += q[1]; b += q[2]; a += (ch == 4) ? q[3] : 255; n++;
                }
            tex[((y >> 2) * (tw >> 2) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)] =
                rgb5a3(r / n, g / n, b / n, a / n);
        }
    }
    if (webp) WebPFree(px); else stbi_image_free(px);
    dj_tex = tex;
    dj_tw = tw; dj_th = th; dj_iw = dw; dj_ih = dh;
    dj_ok = 1;
}

static void *dec_thread_fn(void *arg) {
    (void)arg;
    for (;;) {
        if (dj_state == 1) {
            img_decode_job();
            free(dj_in);
            dj_in = NULL;
            dj_state = 2;
        } else if (cv_state == 1) {                        /* pochette du morceau (v19) */
            cover_job();
            cv_state = 2;
        } else if (gj_state == 1) {                        /* v92 : banniere / grande image d'un jeu */
            gl_job_run();
            gj_state = 2;
        } else usleep(4000);
    }
    return NULL;
}

/* v96 : avant de demonter la carte SD (lancement d'un jeu, d'une appli, essai de Resident) : la lecture
   en fond en cours doit finir. Sinon le fil de decodage fermait son fichier sur une carte demontee :
   ecran rouge (DSI dans _lseek_r <- fclose <- dec_thread_fn), vu en lancant Wind Waker. */
static void bg_quiesce(void) {
    gj_hold = 1;
    u64 t0 = now_ms();
    while (gj_state == 1 && now_ms() - t0 < 3000) usleep(2000);
}
static void img_loader_reset(void) {
    if (il_state == IL_FETCH) { hf_abort(&ihf); il_state = IL_IDLE; }
    il_img = -1;                            /* un decodage en cours sera jete (generation) */
}

static void img_fail(void) {
    if (il_img >= 0 && il_gen == img_gen) { wimgs[il_img].st = IMG_FAIL; relayout_needed = 1; }
    hf_drop(&ihf);
    il_state = IL_IDLE;
    il_img = -1;
}

static int img_dist(const wimg_t *im) {
    s32 top = pg_scroll, bot = pg_scroll + WV_H;
    if (im->y1 < top) return top - im->y1;
    if (im->y0 > bot) return im->y0 - bot;
    return 0;
}

static void img_pump(void) {
    if (tab != TAB_WEB && il_state == IL_IDLE) return;
    if (il_state == IL_FETCH && il_gen != img_gen) img_loader_reset();

    if (il_state == IL_DECODE) {
        if (dj_state != 2) return;
        dj_state = 0;
        il_state = IL_IDLE;
        if (il_gen != img_gen || il_img < 0) { free(dj_tex); return; }   /* page changee */
        wimg_t *im = &wimgs[il_img];
        il_img = -1;
        int unsized = (im->aw <= 0 || im->ah <= 0);
        im->nw = (s16)(dj_nw > 32767 ? 32767 : dj_nw);
        im->nh = (s16)(dj_nh > 32767 ? 32767 : dj_nh);
        if (!dj_ok) { im->st = IMG_FAIL; free(dj_tex); relayout_needed = 1; return; }
        im->tex = dj_tex;
        im->tw = (u16)dj_tw; im->th = (u16)dj_th; im->iw = (u16)dj_iw; im->ih = (u16)dj_ih;
        DCFlushRange(im->tex, (u32)im->tw * im->th * 2);
        GX_InitTexObj(&im->obj, im->tex, im->tw, im->th, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&im->obj, GX_LINEAR, GX_LINEAR);
        img_bytes += (long)im->tw * im->th * 2;
        img_tex_dirty = 1;
        im->st = IMG_OK;
        if (unsized) relayout_needed = 1;
        return;
    }

    if (il_state == IL_IDLE) {
        if (dj_state != 0) return;
        int best = -1, bestd = 2 * WV_H + 1;
        for (int i = 0; i < nwimg; i++) {
            if (wimgs[i].st != IMG_NONE || wimgs[i].y0 < 0) continue;
            int d = img_dist(&wimgs[i]);
            if (d < bestd) { bestd = d; best = i; }
        }
        if (best < 0) return;
        /* memoire : on oublie les images loin de l'ecran */
        while (img_bytes > IMG_TEX_BUDGET) {
            int far = -1, fd = 2 * WV_H;
            for (int i = 0; i < nwimg; i++)
                if (wimgs[i].st == IMG_OK && img_dist(&wimgs[i]) > fd) { fd = img_dist(&wimgs[i]); far = i; }
            if (far < 0) return;                                      /* tout est proche : on attend */
            img_bytes -= (long)wimgs[far].tw * wimgs[far].th * 2;
            free(wimgs[far].tex);
            wimgs[far].tex = NULL;
            wimgs[far].st = IMG_NONE;
        }
        il_img = best;
        il_gen = img_gen;
        wimgs[best].st = IMG_LOADING;
        {                                               /* deja vue : directement au decodeur */
            int clen = 0;
            unsigned char *cd = dj_state == 0 && dec_ensure() ? icache_get(wimgs[best].url, &clen) : NULL;
            if (cd) {
                dj_in = cd; dj_len = clen;
                dj_aw = wimgs[best].aw; dj_ah = wimgs[best].ah; dj_maxw = wv_lw;
                dj_photo = 0; dj_owner = 1;
                il_state = IL_DECODE;
                dj_state = 1;
                return;
            }
        }
        ihf.image = 1;
        ihf.maxbytes = IMG_MAX_BYTES;
        ihf.accept = "image/webp,image/jpeg,image/png,image/gif,image/*;q=0.8,*/*;q=0.5";
        hf_start(&ihf, wimgs[best].url, pg.url);
        il_state = IL_FETCH;
    }

    if (il_state == IL_FETCH) {
        hf_pump(&ihf);
        if (ihf.st == HF_FAIL) { img_fail(); return; }
        if (ihf.st != HF_DONE) return;
        if (dj_state != 0) return;                      /* decodeur occupe (photo de l'economiseur) */
        if (!dec_ensure()) { img_fail(); return; }
        wimg_t *im = &wimgs[il_img];
        icache_put(im->url, ihf.buf, ihf.len);         /* gardee pour la prochaine fois */
        dj_in = ihf.buf;
        dj_len = ihf.len;
        ihf.buf = NULL;
        ihf.len = ihf.cap = 0;
        ihf.st = HF_IDLE;
        dj_aw = im->aw;
        dj_ah = im->ah;
        dj_maxw = wv_lw;
        dj_photo = 0;
        dj_owner = 1;
        il_state = IL_DECODE;
        dj_state = 1;
    }
}

// ---------- Pochettes automatiques (MusicBrainz + Cover Art Archive) ----------
/* Quand un morceau n'a pas de pochette, on cherche l'album sur MusicBrainz
   (artiste + titre), puis on telecharge la pochette sur le Cover Art Archive.
   Elle est enregistree a cote du MP3 (meme nom en .jpg) : la fois suivante elle
   s'affiche sans reseau. Reglage : sd:/gc-mp3/gc-mp3.txt (pochettes_auto = oui/non). */
#ifndef MB_BASE
#define MB_BASE  "https://musicbrainz.org"
#endif
#ifndef CAA_BASE
#define CAA_BASE "https://coverartarchive.org"
#endif
/* --- recherche --- */
enum { CL_IDLE, CL_WAIT, CL_MB, CL_ART, CL_DONE, CL_IT };   /* (v74 : CL_IT = iTunes en renfort) */
#ifndef ITUNES_BASE
#define ITUNES_BASE "https://itunes.apple.com"
#endif
static hfetch_t chf = { .c = { -1, 0, 0 } };
static int cl_state = CL_IDLE, cl_track = -1, cl_try = 0, cl_mb_retry = 0;
static u64 cl_t0 = 0, cl_next_mb = 0;
static char cl_rgid[40], cl_relid[40];
static char cl_album[128], cl_year[8];
static char cl_qart[128];                       /* v74 : artiste cherche (album d'un autre artiste : en dernier) */
static int cl_best = -100;                      /* v74 : note du meilleur album MusicBrainz (< 5 : compilation, autre artiste...) */
static char cl_art_url[URL_MAX];                /* v74 : image trouvee par iTunes */
/* albums trouves pendant la session (chemin du MP3 -> album, annee) */
static struct { char path[NAME_LEN]; char album[128]; char year[8]; } cl_info[32];
static int cl_ninfo = 0;
static char cl_failed[32][NAME_LEN];       /* morceaux sans resultat (pour ne pas redemander) */
static int cl_nfailed = 0;

static int cl_is_failed(const char *path) {
    for (int i = 0; i < cl_nfailed && i < 32; i++) if (strcmp(cl_failed[i], path) == 0) return 1;
    return 0;
}

static void cl_give_up(void) {
    if (cl_track >= 0 && cl_track < nfiles) {
        snprintf(cl_failed[cl_nfailed % 32], NAME_LEN, "%s", names[cl_track]);
        cl_nfailed++;
    }
    hf_drop(&chf);
    cl_state = CL_DONE;
}

/* artiste et titre a chercher : etiquettes du MP3, sinon "Artiste - Titre.mp3" */
static int cl_query_terms(char *art, int acap, char *tit, int tcap) {
    snprintf(art, acap, "%s", artist);
    snprintf(tit, tcap, "%s", id3_title);
    if (!tit[0]) {
        char nm[NAME_LEN];
        snprintf(nm, sizeof(nm), "%s", display_name(base_of(names[cur_track])));
        strip_audio_ext(nm);
        char *dash = strstr(nm, " - ");
        if (dash) {
            *dash = 0;
            if (!art[0]) snprintf(art, acap, "%s", trim(nm));
            snprintf(tit, tcap, "%s", trim(dash + 3));
        } else snprintf(tit, tcap, "%s", trim(nm));
    }
    for (char *p = tit; *p; p++) if (*p == '_') *p = ' ';
    return tit[0] != 0;
}

static void lucene_quote(const char *in, char *out, int cap) {
    int n = 0;
    out[n++] = '"';
    for (; *in && n < cap - 4; in++) {
        if (*in == '"' || *in == '\\') out[n++] = '\\';
        out[n++] = *in;
    }
    out[n++] = '"';
    out[n] = 0;
}

static void cl_start_mb(void) {
    char art[128], tit[160], qa[300], qt[340], q[700], enc[1500], url[URL_MAX];
    if (!cl_query_terms(art, sizeof(art), tit, sizeof(tit))) { cl_give_up(); return; }
    snprintf(cl_qart, sizeof(cl_qart), "%s", art);
    lucene_quote(tit, qt, sizeof(qt));
    if (art[0]) { lucene_quote(art, qa, sizeof(qa)); snprintf(q, sizeof(q), "recording:%s AND artist:%s", qt, qa); }
    else snprintf(q, sizeof(q), "recording:%s", qt);
    if (!url_encode(q, enc, sizeof(enc))) { cl_give_up(); return; }
    snprintf(url, sizeof(url), MB_BASE "/ws/2/recording/?query=%s&limit=25&fmt=json", enc);   /* (v74 : 25 : l'album original n'est souvent pas dans les 5 premiers) */
    chf.image = 0;
    chf.maxbytes = 3 * 1024 * 1024;
    chf.accept = "application/json";
    cl_next_mb = now_ms() + 1100;           /* MusicBrainz : 1 demande par seconde au plus */
    hf_start(&chf, url, NULL);
    cl_state = CL_MB;
}

/* Choisit l'album le plus probable : officiel, album studio, le plus ancien. */
static int cl_pick(const char *js, int len) {
    int maxt = len / 5 + 64;                           /* (v74 : avant 12000 : trop peu pour un tube a 100 sorties) */
    if (maxt > 120000) maxt = 120000;
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * maxt);
    if (!t) return 0;
    int n = json_tokens(js, len, t, maxt);
    int found = 0;
    if (n > 0 && t[0].type == JT_OBJ) {
        int recs = jget(js, t, n, 0, "recordings");
        if (recs >= 0 && t[recs].type == JT_ARR) {
            /* v74 : le meilleur album parmi TOUS les enregistrements bien notes (avant : le premier
               seulement, souvent un remix sur une compilation) */
            int best = -1, bestscore = -1, top = -1;
            char bestdate[16] = "";
            for (int r = recs + 1; r < n && t[r].start < t[recs].end; r = jskip(t, n, r)) {
                char sc[16];
                jstr(js, t, jget(js, t, n, r, "score"), sc, sizeof(sc));
                int rs = sc[0] ? atoi(sc) : 100;
                if (top < 0) top = rs;
                if (rs < 70 || rs < top - 15) break;        /* resultats tries par score */
                int rels = jget(js, t, n, r, "releases");
                if (rels < 0 || t[rels].type != JT_ARR) continue;
                for (int e = rels + 1; e < n && t[e].start < t[rels].end; e = jskip(t, n, e)) {
                    char status[24], ptype[24], date[16];
                    jstr(js, t, jget(js, t, n, e, "status"), status, sizeof(status));
                    jstr(js, t, jget(js, t, n, e, "date"), date, sizeof(date));
                    int rg = jget(js, t, n, e, "release-group");
                    jstr(js, t, jget(js, t, n, rg, "primary-type"), ptype, sizeof(ptype));
                    int sec = jget(js, t, n, rg, "secondary-types");
                    int nsec = (sec >= 0 && t[sec].type == JT_ARR && jskip(t, n, sec) > sec + 1);
                    int compil = 0;                         /* v74 : compilations et mixes : en dernier */
                    for (int q = sec + 1; nsec && q < n && t[q].start < t[sec].end; q++) {
                        char st2[24];
                        jstr(js, t, q, st2, sizeof(st2));
                        if (!strcasecmp(st2, "Compilation") || !strcasecmp(st2, "DJ-mix") || !strcasecmp(st2, "Remix") || !strcasecmp(st2, "Live")) compil = 1;
                    }
                    int score = (strcmp(status, "Official") == 0 ? 4 : 0) +
                                (strcmp(ptype, "Album") == 0 ? 3 : (ptype[0] ? 1 : 0)) + (nsec ? 0 : 2) - (compil ? 4 : 0);
                    {                                       /* v74 : compilation "Various Artists" : en dernier */
                        int ac = jget(js, t, n, e, "artist-credit");
                        char acn[64] = "";
                        if (ac >= 0 && t[ac].type == JT_ARR && ac + 1 < n && t[ac + 1].start < t[ac].end) {
                            jstr(js, t, jget(js, t, n, ac + 1, "name"), acn, sizeof(acn));
                            if (!acn[0]) jstr(js, t, jget(js, t, n, jget(js, t, n, ac + 1, "artist"), "name"), acn, sizeof(acn));
                        }
                        if (!strcasecmp(acn, "Various Artists")) score -= 6;
                        else if (acn[0] && cl_qart[0] && strcasecmp(acn, cl_qart)) score -= 3;
                    }
                    int earlier = date[0] && (!bestdate[0] || strncmp(date, bestdate, 4) < 0);   /* (annee : une date partielle n'est pas plus ancienne) */
                    if (score > bestscore || (score == bestscore && earlier)) {
                        best = e;
                        bestscore = score;
                        snprintf(bestdate, sizeof(bestdate), "%s", date);
                    }
                }
            }
            cl_best = bestscore;
            if (best >= 0) {
                jstr(js, t, jget(js, t, n, best, "id"), cl_relid, sizeof(cl_relid));
                jstr(js, t, jget(js, t, n, jget(js, t, n, best, "release-group"), "id"), cl_rgid, sizeof(cl_rgid));
                jstr(js, t, jget(js, t, n, best, "title"), cl_album, sizeof(cl_album));
                snprintf(cl_year, sizeof(cl_year), "%.4s", bestdate);
                found = cl_relid[0] || cl_rgid[0];
            }
        }
    }
    free(t);
    return found;
}

/* v74 : iTunes : la pochette du morceau (quand MusicBrainz ne trouve que des compilations) */
static void cl_start_it(void) {
    char art[128], tit[160], q[300], enc[900], url[URL_MAX];
    if (!cl_query_terms(art, sizeof(art), tit, sizeof(tit))) { cl_give_up(); return; }
    snprintf(q, sizeof(q), "%s %s", art, tit);
    if (!url_encode(q, enc, sizeof(enc))) { cl_give_up(); return; }
    snprintf(url, sizeof(url), ITUNES_BASE "/search?media=music&entity=song&limit=5&term=%s", enc);
    chf.image = 0;
    chf.maxbytes = 512 * 1024;
    chf.accept = "application/json";
    hf_start(&chf, url, NULL);
    cl_state = CL_IT;
}
static int cl_it_pick(const char *js, int len) {
    int maxt = len / 6 + 64;
    if (maxt > 30000) maxt = 30000;
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * maxt);
    if (!t) return 0;
    int n = json_tokens(js, len, t, maxt), ok = 0;
    int res = (n > 0 && t[0].type == JT_OBJ) ? jget(js, t, n, 0, "results") : -1;
    if (res >= 0 && t[res].type == JT_ARR && res + 1 < n && t[res + 1].start < t[res].end) {
        int r = res + 1;
        char a[URL_MAX], an[128], al[128], d[32];
        jstr(js, t, jget(js, t, n, r, "artworkUrl100"), a, sizeof(a));
        jstr(js, t, jget(js, t, n, r, "artistName"), an, sizeof(an));
        jstr(js, t, jget(js, t, n, r, "collectionName"), al, sizeof(al));
        jstr(js, t, jget(js, t, n, r, "releaseDate"), d, sizeof(d));
        char *sz = strstr(a, "100x100bb");
        if (sz) memcpy(sz, "300x300", 7);                    /* (plus net : 300 x 300) */
        int same = !cl_qart[0] || ci_find(an, cl_qart) || ci_find(cl_qart, an);
        if (!strncmp(a, "http", 4) && same) {
            snprintf(cl_art_url, sizeof(cl_art_url), "%s", a);
            snprintf(cl_album, sizeof(cl_album), "%s", al);
            snprintf(cl_year, sizeof(cl_year), "%.4s", d);
            ok = 1;
        }
    }
    free(t);
    return ok;
}

static void cl_start_art(void) {
    char url[URL_MAX];
    if (cl_art_url[0]) {                                     /* v74 : image iTunes */
        chf.image = 1;
        chf.maxbytes = 2 * 1024 * 1024;
        chf.accept = "image/jpeg,image/*;q=0.8";
        hf_start(&chf, cl_art_url, NULL);
        cl_state = CL_ART;
        return;
    }
    const char *id = (cl_try == 0 && cl_rgid[0]) ? cl_rgid : cl_relid;
    if (!id[0]) { cl_give_up(); return; }
    for (const char *p = id; *p; p++)
        if (!isxdigit((unsigned char)*p) && *p != '-') { cl_give_up(); return; }
    snprintf(url, sizeof(url), CAA_BASE "/%s/%s/front-250", (cl_try == 0 && cl_rgid[0]) ? "release-group" : "release", id);
    chf.image = 1;
    chf.maxbytes = 2 * 1024 * 1024;
    chf.accept = "image/jpeg,image/*;q=0.8";
    hf_start(&chf, url, NULL);
    cl_state = CL_ART;
}

static void cl_save_cover(const unsigned char *d, int n) {
    if (!sd_ok || cl_track < 0 || cl_track >= nfiles) return;
    char path[NAME_LEN + 8];
    snprintf(path, sizeof(path), "%s", names[cl_track]);
    strip_audio_ext(path);
    strcat(path, ".jpg");
    struct stat st;
    if (stat(path, &st) == 0) return;      /* ne jamais ecraser un fichier existant */
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int ok = fwrite(d, 1, n, f) == (size_t)n;
    fclose(f);
    if (!ok) remove(path);
}

/* ligne d'info du lecteur : "Album (annee)", ou la recherche en cours (retour 1) */
static int cover_info(char *out, int cap) {
    const char *alb = id3_album, *yr = id3_year;
    for (int i = 0; i < cl_ninfo && i < 32 && cur_track >= 0; i++)
        if (strcmp(cl_info[i].path, names[cur_track]) == 0) {
            if (!alb[0]) alb = cl_info[i].album;
            if (!yr[0]) yr = cl_info[i].year;
        }
    char raw[200];
    int searching = 0;
    if (alb[0] && yr[0]) snprintf(raw, sizeof(raw), "%s (%s)", alb, yr);
    else snprintf(raw, sizeof(raw), "%s%s", alb, yr);
    if (covers_auto && !cover_ok && cl_track == cur_track && ((cl_state >= CL_WAIT && cl_state <= CL_ART) || cl_state == CL_IT)) {
        snprintf(raw, sizeof(raw), "Recherche de la pochette...");
        searching = 1;
    }
    text_display(raw, out, cap);
    return searching;
}

static void cover_pump(void) {
    if (!covers_auto) return;
    /* le morceau a change : on abandonne la recherche en cours */
    if (cl_state != CL_IDLE && (src != SRC_FILE || cur_track != cl_track)) {
        hf_abort(&chf);
        cl_state = CL_IDLE;
    }
    if (cl_state == CL_IDLE) {
        if (src != SRC_FILE || !playing || cur_track < 0 || cover_ok || cover_busy() || track_err || is_retro[cur_track]) return;
        if (cl_track == cur_track || cl_is_failed(names[cur_track])) return;
        if (net_phase != NET_DONE || !net_ok) return;
        cl_track = cur_track;
        cl_rgid[0] = cl_relid[0] = 0;
        cl_art_url[0] = 0;
        cl_best = -100;
        cl_try = 0;
        cl_mb_retry = 0;
        cl_t0 = now_ms() + 1500;            /* on attend un peu : l'utilisateur zappe peut-etre */
        cl_state = CL_WAIT;
        return;
    }
    if (cl_state == CL_WAIT) {
        u64 now = now_ms();
        if (now >= cl_t0 && now >= cl_next_mb) cl_start_mb();
        return;
    }
    if (cl_state == CL_MB) {
        hf_pump(&chf);
        if (chf.st == HF_FAIL && cl_mb_retry < 2 && (chf.code == 503 || chf.code == 429 || chf.code == 0)) {   /* v74 : MusicBrainz occupe : on reessaie */
            cl_mb_retry++;
            hf_drop(&chf);
            cl_next_mb = now_ms() + 2500u * cl_mb_retry;
            cl_t0 = 0;
            cl_state = CL_WAIT;
            return;
        }
        if (chf.st == HF_FAIL) { hf_drop(&chf); cl_start_it(); return; }   /* (v74 : MusicBrainz en panne : iTunes) */
        if (chf.st != HF_DONE) return;
        chf.st = HF_IDLE;
        cl_album[0] = cl_year[0] = 0;
        int ok = cl_pick((const char *)chf.buf, chf.len);
        hf_drop(&chf);
        if (!ok || cl_best < 5) { cl_start_it(); return; }   /* v74 : rien de sur (compilation...) : iTunes */
        int k = cl_ninfo++ % 32;
        snprintf(cl_info[k].path, NAME_LEN, "%s", names[cl_track]);
        snprintf(cl_info[k].album, sizeof(cl_info[k].album), "%s", cl_album);
        snprintf(cl_info[k].year, sizeof(cl_info[k].year), "%s", cl_year);
        cl_start_art();
        return;
    }
    if (cl_state == CL_IT) {                                   /* v74 : iTunes */
        hf_pump(&chf);
        if (chf.st != HF_DONE && chf.st != HF_FAIL) return;
        int ok = chf.st == HF_DONE && cl_it_pick((const char *)chf.buf, chf.len);
        chf.st = HF_IDLE;
        hf_drop(&chf);
        if (!ok && !cl_relid[0] && !cl_rgid[0]) { cl_give_up(); return; }
        if (ok) {
            int k = cl_ninfo++ % 32;
            snprintf(cl_info[k].path, NAME_LEN, "%s", names[cl_track]);
            snprintf(cl_info[k].album, sizeof(cl_info[k].album), "%s", cl_album);
            snprintf(cl_info[k].year, sizeof(cl_info[k].year), "%s", cl_year);
        }
        cl_start_art();                                          /* (sinon : l'album MusicBrainz, meme moyen) */
        return;
    }
    if (cl_state == CL_ART) {
        hf_pump(&chf);
        if (chf.st == HF_FAIL) {
            if (cl_art_url[0] && (cl_relid[0] || cl_rgid[0])) { cl_art_url[0] = 0; cl_start_art(); return; }   /* (v74 : iTunes refuse : MusicBrainz) */
            if (cl_try == 0 && cl_rgid[0] && cl_relid[0]) { cl_try = 1; cl_start_art(); return; }
            cl_give_up();
            return;
        }
        if (chf.st != HF_DONE) return;
        chf.st = HF_IDLE;
        if (!cover_ok && cur_track == cl_track) {
            cv_defer = 1;                                  /* (decodee en tache de fond) */
            int r = cover_from_memory(chf.buf, chf.len);
            cv_defer = 0;
            cover_ok = r == 1;
            if (r) cl_save_cover(chf.buf, chf.len);
        }
        hf_drop(&chf);
        cl_state = CL_DONE;
    }
}

// ---------- Paroles automatiques (LRCLIB) ----------
/* Quand un morceau n'a pas de fichier .lrc, on cherche ses paroles synchronisees
   sur lrclib.net (gratuit, sans compte). Elles sont enregistrees a cote du
   fichier (meme nom en .lrc) : la fois suivante, plus besoin du reseau.
   Reglage : sd:/gc-mp3/gc-mp3.txt (paroles_auto = oui/non). */
#ifndef LRC_BASE
#define LRC_BASE "https://lrclib.net"
#endif
enum { LY_IDLE, LY_WAIT, LY_FETCH, LY_DONE };
static hfetch_t lhf = { .c = { -1, 0, 0 } };
static int ly_state = LY_IDLE, ly_track = -1, ly_retry = 0;
static u64 ly_t0 = 0;
static char ly_failed[32][NAME_LEN];       /* morceaux sans paroles (pour ne pas redemander) */
static int ly_nfailed = 0;

static int ly_is_failed(const char *path) {
    for (int i = 0; i < ly_nfailed && i < 32; i++) if (strcmp(ly_failed[i], path) == 0) return 1;
    return 0;
}

static void ly_give_up(void) {
    if (ly_track >= 0 && ly_track < nfiles) {
        snprintf(ly_failed[ly_nfailed % 32], NAME_LEN, "%s", names[ly_track]);
        ly_nfailed++;
    }
    hf_drop(&lhf);
    ly_state = LY_DONE;
}

static void ly_start(void) {
    char art[128], tit[160], ea[400], et[480], url[URL_MAX];
    if (!cl_query_terms(art, sizeof(art), tit, sizeof(tit))) { ly_give_up(); return; }
    if (!url_encode(tit, et, sizeof(et))) { ly_give_up(); return; }
    if (art[0] && url_encode(art, ea, sizeof(ea)))
        snprintf(url, sizeof(url), LRC_BASE "/api/search?track_name=%s&artist_name=%s", et, ea);
    else
        snprintf(url, sizeof(url), LRC_BASE "/api/search?q=%s", et);
    lhf.image = 0;
    lhf.maxbytes = 2 * 1024 * 1024;
    lhf.accept = "application/json";
    hf_start(&lhf, url, NULL);
    ly_state = LY_FETCH;
}

/* Choisit le resultat avec des paroles synchronisees et la duree la plus proche.
   Retourne le texte LRC (a liberer) ou NULL. */
static char *ly_pick(const char *js, int len, int dur) {
    int maxt = len / 8 + 64;
    if (maxt > 60000) maxt = 60000;
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * maxt);
    if (!t) return NULL;
    int n = json_tokens(js, len, t, maxt);
    int best = -1, bestd = 1 << 30;
    if (n > 0 && t[0].type == JT_ARR) {
        for (int r = 1; r < n && t[r].start < t[0].end; r = jskip(t, n, r)) {
            if (t[r].type != JT_OBJ) continue;
            int sl = jget(js, t, n, r, "syncedLyrics");
            if (sl < 0 || t[sl].type != JT_STR || t[sl].end - t[sl].start < 10) continue;
            char ds[24];
            jstr(js, t, jget(js, t, n, r, "duration"), ds, sizeof(ds));
            int d = ds[0] ? (int)(atof(ds) + 0.5) : -1;
            int diff = (dur > 0 && d > 0) ? abs(d - dur) : 1000;
            if (dur > 0 && d > 0 && diff > 15) continue;          /* autre version du morceau */
            if (diff < bestd) { bestd = diff; best = sl; }
        }
    }
    char *out = NULL;
    if (best >= 0) {
        int cap = (t[best].end - t[best].start) + 16;
        out = (char *)malloc(cap);
        if (out) { json_nl = 1; jstr(js, t, best, out, cap); json_nl = 0; }
    }
    free(t);
    return out;
}

static void ly_save(const char *txt) {
    if (!sd_ok || ly_track < 0 || ly_track >= nfiles) return;
    char path[NAME_LEN + 8];
    snprintf(path, sizeof(path), "%s", names[ly_track]);
    strip_audio_ext(path);
    strcat(path, ".lrc");
    struct stat st;
    if (stat(path, &st) == 0) return;      /* ne jamais ecraser un fichier existant */
    FILE *f = fopen(path, "wb");
    if (!f) return;
    size_t n = strlen(txt);
    int ok = fwrite(txt, 1, n, f) == n;
    fclose(f);
    if (!ok) remove(path);
}

static void lyrics_pump(void) {
    if (!lyrics_auto) return;
    if (ly_state != LY_IDLE && ly_state != LY_DONE && (src != SRC_FILE || cur_track != ly_track)) {
        hf_abort(&lhf);                     /* le morceau a change */
        ly_state = LY_IDLE;
    }
    if (ly_state == LY_IDLE || ly_state == LY_DONE) {
        if (src != SRC_FILE || !playing || cur_track < 0 || track_err || is_retro[cur_track]) return;
        if (cur_track == ly_track) return;
        if (lrc_track == cur_track && lrc_n > 0) return;        /* deja un .lrc */
        if (ly_is_failed(names[cur_track])) return;
        if (net_phase != NET_DONE || !net_ok) return;
        ly_track = cur_track;
        ly_retry = 0;
        ly_t0 = now_ms() + 2500;            /* on attend un peu : l'utilisateur zappe peut-etre */
        ly_state = LY_WAIT;
        return;
    }
    if (ly_state == LY_WAIT) {
        if (now_ms() >= ly_t0 && cl_state != CL_MB && cl_state != CL_ART && cl_state != CL_IT) ly_start();   /* une demande a la fois */
        return;
    }
    if (ly_state == LY_FETCH) {
        hf_pump(&lhf);
        if (lhf.st == HF_FAIL && ly_retry < 2 && (lhf.code == 0 || lhf.code >= 500 || lhf.code == 429)) {   /* v74 : LRCLIB occupe : on reessaie */
            ly_retry++;
            hf_drop(&lhf);
            ly_t0 = now_ms() + 3000u * ly_retry;
            ly_state = LY_WAIT;
            return;
        }
        if (lhf.st == HF_FAIL) { ly_give_up(); return; }
        if (lhf.st != HF_DONE) return;
        lhf.st = HF_IDLE;
        int dur = (ly_track >= 0 && ly_track < nfiles) ? dur_cache[ly_track] : -1;
        if (dur == -2) dur = dur_cache[ly_track] = file_duration(names[ly_track]);
        char *txt = ly_pick((const char *)lhf.buf, lhf.len, dur);
        hf_drop(&lhf);
        if (!txt) { ly_give_up(); return; }
        if (cur_track == ly_track) lyrics_parse((const unsigned char *)txt, (long)strlen(txt));
        ly_save(txt);
        free(txt);
        ly_state = LY_DONE;
    }
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
        /* le texte tout de suite : debut de la page affiche pendant le chargement */
        if (wb_loading && wb_kind == WK_HTML && wb_buf && wb_len >= 3000 && wb_len < 600 * 1024 &&
            wb_len - wb_shown_len >= (wb_partial ? 24 * 1024 : 3000) && now_ms() - wb_partial_t > (wb_partial ? 1200 : 300))
            web_partial_show();
    }
    ck_pump();
    dl_pump();
    img_pump();
    if (relayout_needed && !wb_loading) {                /* une image a donne sa taille */
        static u64 last = 0;
        u64 wait = pg.srclen > 300000 ? 3000 : 800;
        if (now_ms() - last > wait) {
            relayout_needed = 0;
            last = now_ms();
            web_relayout();
        }
    }
}

// ---------- Navigateur : actions ----------
enum { KB_URL, KB_SEARCH, KB_ARCHIVE, KB_FIELD, KB_RADIO, KB_PODCAST, KB_CITY, KB_RENAME, KB_MKDIR, KB_LIBFIND, KB_PLNAME, KB_CHEATID, KB_CHEATNAME, KB_CHEATCODE, KB_NETTXT, KB_NFS };
static void cheats_kb_done(int mode, const char *t);
static void ic_kb_done(const char *t);
static void cheats_open(void);
static void lib_kb_done(int mode, const char *t);
static void files_kb_done(int mode, const char *t);
static void podcast_search(const char *q);
static void infos_set_city(const char *c);
static void rb_search_name(const char *q);
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
    if (h) web_show((unsigned char *)h, (int)strlen(h), "gc:accueil", WK_HTML, 0, mode);
}

static void add_favorite(void) {
    const whist_t *lp = last_web_page();
    if (!lp || !sd_ok) return;
    FILE *f = fopen(FAV_FILE, "a");
    if (!f) { web_note("Impossible d'écrire SD:/gc-mp3/favoris.txt", 1); return; }
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
        else if (strcmp(w, "cookies") == 0) { ck_clear(); web_note("Cookies effacés", 0); web_render_home(NAV_REPLACE); }
        else web_render_home(mode);
        return;
    }
    web_fetch(url, mode, 0);
}

static void web_search(const char *q) {
    char enc[600], url[URL_MAX];
    if (!url_encode(q, enc, sizeof(enc))) return;
    snprintf(ws_q, sizeof(ws_q), "%s", enc);
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
    static char body[8192];
    char url[URL_MAX], enc[800];
    int n = 0;
    body[0] = 0;
    for (int i = 0; i < pg.nfields; i++) {
        wfield_t *f = &pg.fields[i];
        if (f->form != form || !pg.pool[f->name]) continue;
        if (f->type == FD_SUBMIT && i != button) continue;
        if ((f->type == FD_CHECK || f->type == FD_RADIO) && !f->checked) continue;
        if (f->type == FD_SELECT && f->nopt <= 0) continue;
        if (!url_encode(pg.pool + f->name, enc, sizeof(enc))) return;
        n += snprintf(body + n, sizeof(body) - n, "%s%s=", n ? "&" : "", enc);
        if (!url_encode(f->value, enc, sizeof(enc)) || n >= (int)sizeof(body) - 1) { web_note("Formulaire trop long", 1); return; }
        n += snprintf(body + n, sizeof(body) - n, "%s", enc);
        if (n >= (int)sizeof(body) - 1) { web_note("Formulaire trop long", 1); return; }
    }
    snprintf(url, sizeof(url), "%s", pg.pool + pg.forms[form].action);
    wb_refreshes = 0;
    crumb("formulaire %s", pg.forms[form].post ? "POST" : "GET");
    if (pg.forms[form].post) {                          /* envoi dans le corps de la requete */
        free(wb_post);
        wb_post = strdup(body);
        wb_post_len = wb_post ? n : 0;
        web_fetch(url, NAV_PUSH, 0);
        return;
    }
    url[strcspn(url, "?#")] = 0;
    int ul = (int)strlen(url);
    if (ul + 1 + n >= (int)sizeof(url)) { web_note("Formulaire trop long", 1); return; }
    if (n) snprintf(url + ul, sizeof(url) - ul, "?%s", body);
    web_fetch(url, NAV_PUSH, 0);
}

static void kb_validate(void) {
    char *t = trim(kb_text);
    kb_open = 0;
    wb_refreshes = 0;
    if (kb_mode == KB_FIELD) {
        if (kb_field >= 0 && kb_field < pg.nfields) {
            snprintf(pg.fields[kb_field].value, sizeof(pg.fields[kb_field].value), "%s", t);
            int ntext = 0;                              /* un seul champ (recherche) : on envoie tout de suite */
            for (int i = 0; i < pg.nfields; i++) if (pg.fields[i].form == kb_form && pg.fields[i].type == FD_TEXT) ntext++;
            if (ntext <= 1) web_submit(kb_form, -1);
            else web_note("Rempli. Choisis le bouton du formulaire pour l'envoyer", 0);
        }
        return;
    }
    if (kb_mode == KB_LIBFIND || kb_mode == KB_PLNAME) { lib_kb_done(kb_mode, t); return; }
    if (kb_mode == KB_CHEATID || kb_mode == KB_CHEATNAME || kb_mode == KB_CHEATCODE) { if (*t || kb_mode == KB_CHEATNAME) cheats_kb_done(kb_mode, t); return; }
    if (kb_mode == KB_NETTXT) { net_kb_done(t); return; }      /* v75 : adresse IP / passerelle */
    if (kb_mode == KB_NFS) { nfs_kb_done(t); return; }         /* v86 : serveur SMB / FTP / FSP */
    if (!*t) return;
    if (kb_mode == KB_RADIO) { rb_search_name(t); return; }
    if (kb_mode == KB_PODCAST) { podcast_search(t); return; }
    if (kb_mode == KB_CITY) { infos_set_city(t); return; }
    if (kb_mode == KB_RENAME || kb_mode == KB_MKDIR) { files_kb_done(kb_mode, t); return; }
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
    else if (l->kind == LK_TOGGLE && l->field >= 0) {
        wfield_t *f = &pg.fields[l->field];
        if (f->type == FD_CHECK) f->checked = !f->checked;
        else {                                          /* bouton radio : un seul du groupe */
            for (int i = 0; i < pg.nfields; i++)
                if (pg.fields[i].type == FD_RADIO && pg.fields[i].form == f->form && !strcmp(pg.pool + pg.fields[i].name, pg.pool + f->name))
                    pg.fields[i].checked = 0;
            f->checked = 1;
        }
    }
    else if (l->kind == LK_SELECT && l->field >= 0) sp_open(l->field);
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
    if (web_overlay_input(down, nav, fire)) return;
    int pointer = ptr_update();                         /* stick principal : la fleche */
    if (fire) {
        if (nav) ptr_on = 0;
        if (nav & PAD_BUTTON_DOWN) web_nav(1);
        else if (nav & PAD_BUTTON_UP) web_nav(-1);
        else if (nav & PAD_BUTTON_RIGHT) web_page(1);
        else if (nav & PAD_BUTTON_LEFT) web_page(-1);
    }
    /* sticks : defilement continu (stick C ; le stick principal quand la fleche est cachee) */
    int sy = PAD_SubStickY(0), my = pointer ? 0 : PAD_StickY(0);
    int v = abs(sy) > abs(my) ? sy : my;
    if (v > 24 || v < -24) {
        pg_scroll -= (v > 0 ? v - 24 : v + 24) / 5;
        web_scroll_clamp();
        if (pg_sel >= 0 && !link_visible(pg_sel)) pg_sel = -1;
    }
    if (down & PAD_BUTTON_A) { if (!pointer || pg_sel >= 0) web_activate(); }
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
    } else if (k[0] == 'C' && !k[1]) {                                /* v84 : stick C (jaune) */
        gx_circle(cx, cy, 7.5f, RGBA(255, 214, 70, 255), RGBA(205, 150, 20, 255));
        gx_text_c(F_SMALL, cx, cy + 4.5f, "C", RGBA(70, 45, 0, 255));
    } else if ((k[0] == 'L' || k[0] == 'R') && !k[1]) {                /* gachettes */
        gx_rrect_fill(cx - 9, cy - 7, 18, 14, 6, RGBA(150, 150, 175, 255), RGBA(90, 90, 120, 255));
        gx_text_c(F_SMALL, cx, cy + 4.5f, k, RGBA(255, 255, 255, 255));
    } else {
        gx_rrect_fill(cx - 10, cy - 6.5f, 20, 13, 6.5f, RGBA(140, 140, 165, 255), RGBA(84, 84, 110, 255));
        gx_text_c(F_SMALL, cx, cy + 4.0f, "ST", RGBA(255, 255, 255, 255));
    }
}

static void draw_legend_web(void) {
    static const char *const keys_web[6] = { "A", "B", "Y", "X", "Z", "ST" };
    static const char *const lab_web[6] = { "OUVRIR", "RETOUR", "ADRESSE", "ACCUEIL", "ONGLET", "MENU" };
    static const char *const keys_kb[6] = { "A", "B", "X", "Y", "ST", "" };
    static const char *const lab_kb[6] = { "TAPER", "EFFACER", "ESPACE", "ANNULER", "VALIDER", "" };
    const char *const *keys = kb_open ? keys_kb : keys_web;
    const char *const *lab = kb_open ? lab_kb : lab_web;
    int cnt = kb_open ? 5 : 6;
    const f32 x0 = 11, w = 618, cy = 438.5f;
    legend_box(x0, w);
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
#include "web_extra.c"

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
    if (img_tex_dirty) { GX_InvalidateTexAll(); img_tex_dirty = 0; }
    scr_scissor(WV_X - 6, WV_Y - 4, WV_W + 12, WV_H + 6);
    uxsave_t wz = ux_native(WV_X);                         /* (16:9 : la page en taille reelle, plus large) */
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
            if (by - font_ascent[f] > WV_Y + WV_H + 4 && by - it->h > WV_Y + WV_H + 4) break;
            f32 bx = (f32)(WV_X + it->x);
            int sel = (it->link >= 0 && it->link == pg_sel);
            f32 top = by - font_ascent[f];
            if (it->style & WS_IMG) {                          /* image ou case d'attente */
                f32 ih = it->h, iy = by - ih;
                if (iy > WV_Y + WV_H + 4 || by < WV_Y - 4) continue;
                wimg_t *im = it->img >= 0 ? &wimgs[it->img] : NULL;
                int ok = im && im->st == IMG_OK && im->tex;
                if (pass == 0) {
                    if (sel) gx_rrect_fill(bx - 4, iy - 4, it->w + 8, ih + 8, 5, RGBA(150, 80, 255, 255), RGBA(100, 40, 210, 255));
                    if (!ok) {
                        gx_rrect_fill(bx, iy, it->w, ih, 3, RGBA(30, 20, 70, 255), RGBA(20, 12, 50, 255));
                        gx_rrect_stroke(bx, iy, it->w, ih, 3, 1.0f, RGBA(80, 60, 150, 255));
                    }
                    continue;
                }
                if (ok) {
                    /* image centree dans sa case, proportions respectees */
                    f32 sc = (f32)it->w / im->iw, sc2 = ih / im->ih;
                    if (sc2 < sc) sc = sc2;
                    f32 w = im->iw * sc, h = im->ih * sc;
                    f32 x0 = bx + (it->w - w) / 2, y0 = iy + (ih - h) / 2;
                    f32 u1 = (f32)im->iw / im->tw, v1 = (f32)im->ih / im->th;
                    gx_set_image(&im->obj);
                    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
                    GX_Position2f32(UX(x0), y0);         GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0, 0);
                    GX_Position2f32(UX(x0 + w), y0);     GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(u1, 0);
                    GX_Position2f32(UX(x0 + w), y0 + h); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(u1, v1);
                    GX_Position2f32(UX(x0), y0 + h);     GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0, v1);
                    GX_End();
                } else if (pg.text[it->off]) {
                    char lab[64];
                    fit_text(F_SMALL, pg.text + it->off, it->w - 8, lab, sizeof(lab));
                    gx_text1(F_SMALL, bx + 5, iy + ih / 2 + 4, lab, RGBA(150, 135, 210, 255));
                }
                continue;
            }
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
                wfield_t *fd = (it->link >= 0 && pg.links[it->link].field >= 0) ? &pg.fields[pg.links[it->link].field] : NULL;
                if (fd && (fd->type == FD_CHECK || fd->type == FD_RADIO)) {      /* case cochee / bouton radio */
                    if (fd->checked) {
                        f32 cx = bx + it->w / 2.0f, cy = top - 3 + (font_height[F_MED] + 6) / 2.0f;
                        if (fd->type == FD_RADIO) gx_rrect_fill(cx - 5, cy - 5, 10, 10, 5, RGBA(200, 150, 255, 255), RGBA(150, 90, 255, 255));
                        else { gx_tri(cx - 6, cy, cx - 2, cy + 5, cx - 1, cy + 1, RGBA(200, 150, 255, 255)); gx_tri(cx - 2, cy + 5, cx + 7, cy - 6, cx - 1, cy + 1, RGBA(200, 150, 255, 255)); }
                    }
                    continue;
                }
                char dv[300];
                if (fd && fd->type == FD_SELECT) {                               /* liste : le choix en cours */
                    v = (fd->nopt > 0 && fd->osel >= 0) ? pg.pool + pg.opts[(fd->opt0 + fd->osel) * 2 + 1] : "";
                    f32 ax2 = bx + it->w - 14, ay = top + font_height[F_MED] / 2.0f - 2;
                    gx_tri(ax2, ay, ax2 + 9, ay, ax2 + 4.5f, ay + 6, RGBA(200, 170, 255, 255));
                    text_display(v, dv, sizeof(dv));
                    fit_text(F_MED, dv, it->w - 28, fit, sizeof(fit));
                } else {
                    if (fd) v = fd->value;
                    if (fd && fd->secret) {                                      /* mot de passe : masque */
                        int n = (int)strlen(v);
                        if (n > 40) n = 40;
                        memset(dv, '*', n);
                        dv[n] = 0;
                    } else text_display(v, dv, sizeof(dv));
                    fit_text(F_MED, dv, it->w - 12, fit, sizeof(fit));
                }
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
    ux_restore(wz);
    scr_viewport();
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
    draw_web_overlays();
}

static void draw_keyboard(void) {
    char k[16];
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120));
    gx_rrect_fill(30, 120, 580, 296, 10, RGBA(14, 8, 44, 255), RGBA(8, 5, 26, 255));   /* fond opaque */
    draw_panel(30, 120, 580, 296);
    const char *title = kb_mode == KB_SEARCH ? "Rechercher sur le web" :
                        kb_mode == KB_ARCHIVE ? "Musique libre sur archive.org" :
                        kb_mode == KB_FIELD ? "Saisie" : kb_mode == KB_RADIO ? "Rechercher une radio (nom)" :
                        kb_mode == KB_PODCAST ? "Rechercher un podcast" : kb_mode == KB_CITY ? "Ta ville (m\xC3\xA9t\xC3\xA9o)" :
                        kb_mode == KB_RENAME ? "Nouveau nom" : kb_mode == KB_MKDIR ? "Nom du nouveau dossier" :
                        kb_mode == KB_LIBFIND ? "Chercher un morceau (titre, artiste, album)" : kb_mode == KB_PLNAME ? "Nom de la nouvelle liste" :
                        kb_mode == KB_CHEATID ? "ID du jeu (6 caract\xC3\xA8res, ex. GALE01)" : kb_mode == KB_CHEATNAME ? "Nom du cheat" :
                        kb_mode == KB_CHEATCODE ? "Ligne de code (XXXXXXXX YYYYYYYY)" :
                        kb_mode == KB_NETTXT ? "Adresse (ex. 192.168.1.32)" :
                        kb_mode == KB_NFS ? "Serveur du r\xC3\xA9seau (SMB / FTP / FSP)" :
                        "Adresse ou recherche";
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

// ---------- Annuaire de radios (radio-browser.info) ----------
/* Recherche de stations par nom, ou les plus ecoutees en France. Les resultats
   s'ecoutent directement ; X les ajoute a sd:/gc-mp3/radio.txt. */
#define RB_MAX 80
typedef struct { char name[48]; char url[256]; char codec[8]; int kbps; char cc[4]; u8 hls; } rbres_t;
static rbres_t rb_res[RB_MAX];
static int rb_n = 0, rb_mode = 0;           /* rb_mode = 1 : on affiche les resultats */
enum { RB_IDLE, RB_LOADING, RB_DONE, RB_ERROR };
static int rb_state = RB_IDLE, rb_server = 0;
static char rb_query[200], rb_title[120];
static hfetch_t rhf = { .c = { -1, 0, 0 } };
#ifndef RB_SCHEME
#define RB_SCHEME "https"
static const char *const rb_servers[] = { "all.api.radio-browser.info", "de1.api.radio-browser.info",
                                          "de2.api.radio-browser.info", NULL };       /* (v74 : fi1 n'existe plus) */
#else
static const char *const rb_servers[] = { RB_TEST_HOST, NULL };       /* (essais sur PC) */
#endif
static char radio_note[160] = "";
static u64 radio_note_t = 0;

static void rb_fetch(void) {
    char url[URL_MAX];
    snprintf(url, sizeof(url), RB_SCHEME "://%s/json/stations/search?%s&hidebroken=true&order=clickcount&reverse=true&limit=%d",
             rb_servers[rb_server], rb_query, RB_MAX);
    rhf.image = 0;
    rhf.maxbytes = 1024 * 1024;
    rhf.accept = "application/json";
    hf_start(&rhf, url, NULL);
    rb_state = RB_LOADING;
}

static void rb_search(const char *query_params, const char *title) {
    if (net_phase != NET_DONE || !net_ok) {
        net_start_async();
        snprintf(radio_note, sizeof(radio_note), "R\xC3\xA9seau pas encore pr\xC3\xAAt, r\xC3\xA9" "essaie dans quelques secondes");
        radio_note_t = now_ms();
        return;
    }
    snprintf(rb_query, sizeof(rb_query), "%s", query_params);
    snprintf(rb_title, sizeof(rb_title), "%s", title);
    rb_n = 0;
    rb_mode = 1;
    sel_station = 0;
    rb_server = 0;
    rb_fetch();
}

static void rb_search_name(const char *q) {
    char enc[400], params[460], title[120];
    if (!url_encode(q, enc, sizeof(enc))) return;
    snprintf(params, sizeof(params), "name=%s", enc);
    snprintf(title, sizeof(title), "\xC2\xAB %s \xC2\xBB", q);
    rb_search(params, title);
}

static void rb_parse(const char *js, int len) {
    int maxt = len / 6 + 64;
    if (maxt > 60000) maxt = 60000;
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * maxt);
    if (!t) return;
    int n = json_tokens(js, len, t, maxt);
    rb_n = 0;
    if (n > 0 && t[0].type == JT_ARR) {
        for (int i = 1; i < n && t[i].start < t[0].end && rb_n < RB_MAX; i = jskip(t, n, i)) {
            rbres_t *r = &rb_res[rb_n];
            char kb[16], hls[8], url[300];
            jstr(js, t, jget(js, t, n, i, "codec"), r->codec, sizeof(r->codec));
            jstr(js, t, jget(js, t, n, i, "hls"), hls, sizeof(hls));
            if (strcasecmp(r->codec, "MP3") != 0 && strncasecmp(r->codec, "AAC", 3) != 0 && strcasecmp(r->codec, "OGG") != 0) continue;
            r->hls = (hls[0] == '1');
            jstr(js, t, jget(js, t, n, i, "url_resolved"), url, sizeof(url));
            if (!url[0]) jstr(js, t, jget(js, t, n, i, "url"), url, sizeof(url));
            if (strncasecmp(url, "http", 4) != 0 || strlen(url) >= sizeof(r->url)) continue;
            if (ci_find(url, "opus") || ci_find(url, "flac")) continue;   /* v74 : Opus / FLAC annonces "OGG" : illisibles ici */
            snprintf(r->url, sizeof(r->url), "%s", url);
            char nm[200];
            jstr(js, t, jget(js, t, n, i, "name"), nm, sizeof(nm));
            char *tn = trim(nm);
            for (char *c = tn; *c; c++) if (*c == '|') *c = '/';
            snprintf(r->name, sizeof(r->name), "%s", tn[0] ? tn : "Radio");
            jstr(js, t, jget(js, t, n, i, "bitrate"), kb, sizeof(kb));
            r->kbps = atoi(kb);
            jstr(js, t, jget(js, t, n, i, "countrycode"), r->cc, sizeof(r->cc));
            rb_n++;
        }
    }
    free(t);
}

static void rb_pump(void) {
    if (rb_state != RB_LOADING) return;
    hf_pump(&rhf);
    if (rhf.st == HF_FAIL) {
        if (rb_servers[rb_server + 1]) { rb_server++; rb_fetch(); return; }   /* autre serveur */
        rb_state = RB_ERROR;
        return;
    }
    if (rhf.st != HF_DONE) return;
    rhf.st = HF_IDLE;
    rb_parse((const char *)rhf.buf, rhf.len);
    hf_drop(&rhf);
    rb_state = RB_DONE;
}

/* ecoute d'un resultat : il occupe la case libre apres la liste */
static void rb_play(int i) {
    if (i < 0 || i >= rb_n || nstations >= MAX_STATIONS) return;
    snprintf(stations[nstations].name, sizeof(stations[0].name), "%s", rb_res[i].name);
    snprintf(stations[nstations].url, sizeof(stations[0].url), "%s", rb_res[i].url);
    start_station(nstations, 0);
}

static void rb_add_favorite(int i) {
    if (i < 0 || i >= rb_n) return;
    for (int k = 0; k < nstations; k++)
        if (strcmp(stations[k].url, rb_res[i].url) == 0) {
            snprintf(radio_note, sizeof(radio_note), "D\xC3\xA9j\xC3\xA0 dans tes radios");
            radio_note_t = now_ms();
            return;
        }
    if (nstations >= MAX_STATIONS - 1) return;
    int playing_temp = (src == SRC_RADIO && cur_station == nstations);
    snprintf(stations[nstations].name, sizeof(stations[0].name), "%s", rb_res[i].name);
    snprintf(stations[nstations].url, sizeof(stations[0].url), "%s", rb_res[i].url);
    if (playing_temp && strcmp(stations[nstations].url, rb_res[i].url) == 0) { /* reste la station en cours */ }
    nstations++;
    if (sd_ok) {
        FILE *f = fopen(RADIO_LIST, "a");
        if (f) { fprintf(f, "%s | %s\n", rb_res[i].name, rb_res[i].url); fclose(f); }
    }
    snprintf(radio_note, sizeof(radio_note), "Ajout\xC3\xA9" "e \xC3\xA0 tes radios : %s", rb_res[i].name);
    radio_note_t = now_ms();
}

/* ---------- v72 : radios favorites (en haut de la liste, etoile) ----------
   X maintenu sur une de tes radios : favorite / plus favorite (X court : garder le titre, comme avant).
   SD:/gc-mp3/radio-favoris.txt : une adresse par ligne. */
#define RADIO_FAV GCD "/radio-favoris.txt"
static u8 st_fav[MAX_STATIONS];
static int radio_fav_has(const char *url) {
    FILE *f = sd_ok ? fopen(RADIO_FAV, "r") : NULL;
    if (!f) return 0;
    static char l[URL_MAX + 8];
    int r = 0;
    while (!r && fgets(l, sizeof(l), f)) { char *s = trim(l); if (!strcmp(s, url)) r = 1; }
    fclose(f);
    return r;
}
static void radio_fav_sort(void) {                       /* favorites d'abord (ordre garde), station en cours suivie */
    static station_t tmp[MAX_STATIONS];
    static u8 tf[MAX_STATIONS];
    int n = 0, cur = cur_station, ncur = cur, sel = sel_station - RS_N, nsel = sel;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < nstations; i++) if (st_fav[i] == (pass == 0)) {
            if (i == cur) ncur = n;
            if (i == sel) nsel = n;
            tmp[n] = stations[i]; tf[n] = st_fav[i]; n++;
        }
    memcpy(stations, tmp, sizeof(station_t) * n); memcpy(st_fav, tf, n);
    if (cur >= 0 && cur < nstations) cur_station = ncur;
    if (sel >= 0 && sel < nstations && !rb_mode) sel_station = nsel + RS_N;
}
static void radio_fav_load(void) {
    memset(st_fav, 0, sizeof(st_fav));
    for (int i = 0; i < nstations; i++) st_fav[i] = (u8)radio_fav_has(stations[i].url);
    radio_fav_sort();
}
static void radio_fav_toggle(int i) {
    if (i < 0 || i >= nstations || !sd_ok) return;
    st_fav[i] = !st_fav[i];
    FILE *f = fopen(RADIO_FAV, "w");
    if (f) { for (int k = 0; k < nstations; k++) if (st_fav[k]) fprintf(f, "%s\n", stations[k].url); fclose(f); }
    snprintf(radio_note, sizeof(radio_note), st_fav[i] ? "\xE2\x98\x85 Favorite : %s" : "Plus favorite : %s", stations[i].name);
    radio_note_t = now_ms();
    radio_fav_sort();
}
static void draw_star(f32 cx, f32 cy, f32 r, col_t c) {   /* (etoile a 5 branches) */
    f32 px[10], py[10];
    for (int k = 0; k < 10; k++) { f32 a = -PI_F / 2 + k * PI_F / 5, rr = (k & 1) ? r * 0.45f : r; px[k] = cx + cosf(a) * rr; py[k] = cy + sinf(a) * rr; }
    for (int k = 0; k < 10; k += 2) gx_tri(cx, cy, px[k], py[k], px[(k + 1) % 10], py[(k + 1) % 10], c), gx_tri(cx, cy, px[k], py[k], px[(k + 9) % 10], py[(k + 9) % 10], c);
}

/* ---------- v72 : une radio qui ne repond plus (404, serveur introuvable...) : on cherche sa
   nouvelle adresse dans l'annuaire radio-browser (meme nom), on l'essaie, et si elle marche
   on corrige radio.txt tout seul ---------- */
static int rfx_state = 0, rfx_station = -1, rfx_save = 0;
static char rfx_old[URL_MAX], rfx_new[URL_MAX], rfx_name[48];
static hfetch_t rfx_hf = { .c = { -1, 0, 0 } };
static void radio_list_replace_url(const char *oldu, const char *newu) {
    if (!sd_ok) return;
    FILE *f = fopen(RADIO_LIST, "r");
    if (!f) return;
    int cap = 128 * 1024, len = 0;
    char *buf = (char *)malloc(cap);
    static char l[URL_MAX + 128];
    int done = 0;
    while (buf && fgets(l, sizeof(l), f)) {
        char *p = strstr(l, oldu);
        int n;
        if (p && !done && (p[strlen(oldu)] == '\r' || p[strlen(oldu)] == '\n' || p[strlen(oldu)] == 0 || p[strlen(oldu)] == ' ')) {
            n = snprintf(buf + len, cap - len, "%.*s%s\n", (int)(p - l), l, newu); done = 1;
        } else n = snprintf(buf + len, cap - len, "%s", l);
        if (n < 0 || len + n >= cap - 1) { free(buf); buf = NULL; break; }
        len += n;
    }
    fclose(f);
    if (buf && done) { f = fopen(RADIO_LIST, "w"); if (f) { fwrite(buf, 1, len, f); fclose(f); } }
    free(buf);
}
static void radio_repair_pump(void) {
    if (rfx_state == 0) {
        /* (une de tes radios, jamais entendue cette fois, en erreur ; une seule recherche par station) */
        if (state != ST_ERROR || src != SRC_RADIO || pod_on || rad_played || cur_station < 0 || cur_station >= nstations || rfx_station == cur_station) return;
        if (net_phase != NET_DONE || !net_ok || strstr(err_msg, "Format") || strstr(err_msg, "web")) return;
        rfx_station = cur_station;
        snprintf(rfx_old, sizeof(rfx_old), "%s", stations[cur_station].url);
        snprintf(rfx_name, sizeof(rfx_name), "%s", stations[cur_station].name);
        char enc[200], url[URL_MAX];
        if (!url_encode(rfx_name, enc, sizeof(enc))) return;
        snprintf(url, sizeof(url), RB_SCHEME "://%s/json/stations/search?name=%s&hidebroken=true&order=clickcount&reverse=true&limit=8", rb_servers[0], enc);
        rfx_hf.image = 0; rfx_hf.maxbytes = 256 * 1024; rfx_hf.accept = "application/json";
        hf_start(&rfx_hf, url, NULL);
        rfx_state = 1;
        snprintf(conn_msg, sizeof(conn_msg), "Recherche de la nouvelle adresse de %s...", rfx_name);
        return;
    }
    if (rfx_state == 1) {
        hf_pump(&rfx_hf);
        if (rfx_hf.st == HF_FAIL) { hf_drop(&rfx_hf); rfx_state = 0; return; }
        if (rfx_hf.st != HF_DONE) return;
        rfx_hf.st = HF_IDLE;
        rfx_state = 0;
        rfx_new[0] = 0;
        const char *js = (const char *)rfx_hf.buf;
        int len = rfx_hf.len, maxt = len / 6 + 64;
        if (maxt > 20000) maxt = 20000;
        jtok_t *t = js ? (jtok_t *)malloc(sizeof(jtok_t) * maxt) : NULL;
        int n = t ? json_tokens(js, len, t, maxt) : 0;
        if (n > 0 && t[0].type == JT_ARR)
            for (int i = 1; i < n && t[i].start < t[0].end && !rfx_new[0]; i = jskip(t, n, i)) {
                char nm[200], u[URL_MAX], codec[16];
                jstr(js, t, jget(js, t, n, i, "name"), nm, sizeof(nm));
                jstr(js, t, jget(js, t, n, i, "codec"), codec, sizeof(codec));
                if (strcasecmp(trim(nm), rfx_name) != 0) continue;              /* (le meme nom seulement) */
                if (strcasecmp(codec, "MP3") && strncasecmp(codec, "AAC", 3) && strcasecmp(codec, "OGG")) continue;
                jstr(js, t, jget(js, t, n, i, "url_resolved"), u, sizeof(u));
                if (!u[0] || !strcmp(u, rfx_old) || strlen(u) >= sizeof(stations[0].url)) jstr(js, t, jget(js, t, n, i, "url"), u, sizeof(u));
                if (!strncasecmp(u, "http", 4) && strcmp(u, rfx_old) && strlen(u) < sizeof(stations[0].url)) snprintf(rfx_new, sizeof(rfx_new), "%s", u);
            }
        free(t);
        hf_drop(&rfx_hf);
        if (!rfx_new[0] || rfx_station != cur_station || state != ST_ERROR) return;
        snprintf(stations[rfx_station].url, sizeof(stations[0].url), "%s", rfx_new);
        rfx_save = 1;
        start_station(rfx_station, 1);
        return;
    }
}
/* (appelee quand la radio joue) : la nouvelle adresse marche : radio.txt corrige */
static void radio_repair_ok(void) {
    if (!rfx_save || cur_station != rfx_station || state != ST_PLAY) return;
    rfx_save = 0;
    radio_list_replace_url(rfx_old, rfx_new);
    if (radio_fav_has(rfx_old)) { st_fav[rfx_station] = 0; radio_fav_toggle(rfx_station); }
    snprintf(radio_note, sizeof(radio_note), "Adresse de %s mise \xC3\xA0 jour (radio.txt)", rfx_name);
    radio_note_t = now_ms();
}

/* ---------- v72 : la derniere radio ecoutee reprend au demarrage (reglage) ---------- */
static void radio_last_note(void) {                         /* (la radio joue : on la retient) */
    if (src != SRC_RADIO || state != ST_PLAY || pod_on || cur_station < 0 || cur_station > nstations) return;
    if (!strcmp(radio_last_url, stations[cur_station].url)) return;
    snprintf(radio_last_url, sizeof(radio_last_url), "%s", stations[cur_station].url);
    snprintf(radio_last_name, sizeof(radio_last_name), "%s", stations[cur_station].name);
    settings_dirty = 1;
}
static int rr_done = 0;
static void radio_resume_pump(void) {
    int done = rr_done;
    if (done) return;
    if (!radio_resume || !radio_last_url[0] || src != SRC_NONE || now_ms() - last_input_ms < 1500) { if (!radio_resume || !radio_last_url[0] || src != SRC_NONE) rr_done = 1; return; }
    if (net_phase != NET_DONE) return;
    rr_done = 1;
    if (!net_ok) return;
    int k = -1;
    for (int i = 0; i < nstations && k < 0; i++) if (!strcmp(stations[i].url, radio_last_url)) k = i;
    if (k < 0) {                                            /* (une radio de l'annuaire : la case libre) */
        if (nstations >= MAX_STATIONS) return;
        k = nstations;
        snprintf(stations[k].url, sizeof(stations[0].url), "%s", radio_last_url);
        snprintf(stations[k].name, sizeof(stations[0].name), "%s", radio_last_name[0] ? radio_last_name : "Radio");
    }
    crumb("radio : reprise au demarrage");
    start_station(k, 0);
    snprintf(radio_note, sizeof(radio_note), "Reprise de ta derni\xC3\xA8re radio : %s", stations[k].name);
    radio_note_t = now_ms();
}

/* lignes de la liste RADIO : 2 raccourcis + les stations, ou "retour" + les resultats */
static int radio_rows(void) { return rb_mode ? 1 + rb_n : RS_N + nstations; }

static void radio_activate(void) {
    int r = sel_station;
    if (rb_mode) {
        if (r == 0) { rb_mode = 0; sel_station = 0; hf_abort(&rhf); rb_state = RB_IDLE; return; }
        rb_play(r - 1);
        return;
    }
    if (r == 0) { kb_start(KB_RADIO, "", -1, -1); return; }
    if (r == 2) { rh_open(); return; }                       /* v73 : historique des titres */
    if (r == 1) {
        char q[32], t[96];
        snprintf(q, sizeof(q), "countrycode=%s", lang_country());
        snprintf(t, sizeof(t), "Les plus \xC3\xA9" "cout\xC3\xA9" "es %s", lang_country_name());
        rb_search(q, t);
        return;
    }
    start_station(r - RS_N, 0);
}

static void draw_list_radio(void) {
    int rows = radio_rows();
    int start = sel_station - MUSIC_ROWS / 2;
    if (start > rows - MUSIC_ROWS) start = rows - MUSIC_ROWS;
    if (start < 0) start = 0;
    char buf[160], t[24];

    draw_panel(11, 73, 271, 385);
    snprintf(buf, sizeof(buf), "%d", rb_mode ? rb_n : nstations);
    draw_tabs(buf);

    static f32 sel_anim = -2000;
    if (sel_station >= start && sel_station < start + MUSIC_ROWS && sel_station < rows) draw_sel_bar(&sel_anim, 114.0f + (sel_station - start) * 24.0f);
    for (int i = 0; i < MUSIC_ROWS; i++) {
        int r = start + i;
        if (r >= rows) break;
        f32 ry = 114.0f + i * 24.0f;
        f32 cy = ry + 12.0f;
        int is_sel = (r == sel_station);
        gp_zone(18, ry + 1, 250, 22, &sel_station, r, GPZ_SEL);
        if (is_sel) {
        } else if (i + 1 < MUSIC_ROWS && r + 1 < rows && r + 1 != sel_station) {
            gx_rect(25, ry + 24, 241, 1, RGBA(60, 40, 120, 110));
        }
        col_t dim = is_sel ? RGBA(235, 225, 255, 255) : RGBA(128, 112, 190, 255);
        col_t txt = is_sel ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
        col_t ic = is_sel ? RGBA(235, 225, 255, 255) : RGBA(150, 120, 235, 255);
        char nm[64];

        /* lignes speciales */
        if ((rb_mode && r == 0) || (!rb_mode && r < RS_N)) {
            char popl[96];
            snprintf(popl, sizeof(popl), "Les plus \xC3\xA9" "cout\xC3\xA9" "es %s", lang_country_name());
            const char *lab = rb_mode ? rb_title : (r == 0 ? "Rechercher une radio..." : r == 2 ? "Titres pass\xC3\xA9s (historique)" : popl);
            if (rb_mode) gx_tri(38, cy - 6, 38, cy + 6, 30, cy, ic);
            else if (r == 0) { gx_ring(34, cy - 1.5f, 3.5f, 5.2f, ic); gx_poly4(37, cy + 2, 39, cy + 1, 42, cy + 5, 40, cy + 6, ic, ic, ic, ic); }
            else if (r == 2) { gx_ring(36, cy, 5.0f, 6.6f, ic); gx_rect(35.4f, cy - 4, 1.4f, 4.4f, ic); gx_rect(35.4f, cy - 0.4f, 4, 1.4f, ic); }
            else { gx_tri(36, cy - 7, 31, cy + 5, 41, cy + 5, ic); }
            fit_text(F_MED, lab, 196, nm, sizeof(nm));
            gx_text1(F_MED, 50, cy + 5, nm, is_sel ? txt : RGBA(190, 160, 255, 255));
            continue;
        }
        const char *name;
        int is_cur;
        if (rb_mode) {
            rbres_t *res = &rb_res[r - 1];
            name = res->name;
            is_cur = (src == SRC_RADIO && cur_station == nstations && state != ST_IDLE &&
                      strcmp(stations[nstations].url, res->url) == 0);
            const char *cn = strncasecmp(res->codec, "AAC", 3) == 0 ? "AAC" : strcasecmp(res->codec, "OGG") == 0 ? "OGG" : "MP3";
            if (res->kbps > 0) snprintf(t, sizeof(t), "%s%s %d", res->hls ? "HLS " : "", cn, res->kbps);
            else snprintf(t, sizeof(t), "%s%s", res->hls ? "HLS " : "", cn);
        } else {
            int k = r - RS_N;
            name = stations[k].name;
            is_cur = (k == cur_station && state != ST_IDLE && src == SRC_RADIO);
            if (st_fav[k]) t[0] = 0; else snprintf(t, sizeof(t), "%02d", k + 1);
        }
        if (is_cur) gx_tri(31, cy - 6, 31, cy + 6, 41, cy, RGBA(255, 255, 255, 255));
        else draw_radio_icon(36, cy, ic);
        int name_w;
        if (rb_mode) {
            gx_text_r(F_SMALL, is_cur ? 236 : 256, cy + 4, t, dim);
            name_w = (is_cur ? 236 : 256) - text_width(F_SMALL, t, 0) - 8 - 50;
        } else {
            if (st_fav[r - RS_N]) draw_star(60, cy, 7, is_sel ? RGBA(255, 230, 120, 255) : RGBA(255, 205, 80, 255));
            else gx_text1(F_SMALL, 50, cy + 4, t, dim);
            name_w = (is_cur ? 228 : 256) - 76;
        }
        if (is_cur) {
            if (state == ST_PLAY) {
                for (int j = 0; j < 3; j++) {
                    f32 bh = 3.0f + (muted ? 1.0f : 8.0f * bar_val[3 + j * 9]);
                    gx_rect(246.0f + j * 4.0f, cy + 5.0f - bh, 2.5f, bh, RGBA(255, 255, 255, 255));
                }
            } else if (state == ST_ERROR) {
                gx_text_r(F_SMALL, 258, cy + 4, "!", RGBA(255, 150, 80, 255));
            } else {
                int ph = (int)((now_ms() / 250) % 3);
                for (int j = 0; j < 3; j++)
                    gx_rect(244.0f + j * 5.0f, cy + 2, 3, 3, j == ph ? RGBA(255, 255, 255, 255) : RGBA(150, 130, 220, 255));
            }
        }
        fit_text(F_MED, name, name_w, nm, sizeof(nm));
        gx_text1(F_MED, rb_mode ? 50 : 76, cy + 5, nm, txt);
    }

    if (rb_mode && rows == 1) {
        const char *m = rb_state == RB_LOADING ? "Recherche en cours..." :
                        rb_state == RB_ERROR ? "Annuaire injoignable" : "Aucune radio MP3 / AAC trouv\xC3\xA9" "e";
        gx_text1(F_MED, 30, 160, m, rb_state == RB_ERROR ? RGBA(255, 160, 110, 255) : RGBA(190, 180, 235, 255));
    }
    if (rb_mode && rb_n > 0)
        gx_text1(F_SMALL, 26, 447, "A : \xC3\xA9" "couter   X : ajouter \xC3\xA0 mes radios", RGBA(150, 135, 210, 255));
    else if (radio_note[0] && now_ms() - radio_note_t < 5000)
        gx_text1(F_SMALL, 26, 447, radio_note, RGBA(170, 240, 180, 255));
    else if (rec_fp)
        { gx_circle(30, 443, 4, RGBA(255, 80, 80, 255), RGBA(255, 80, 80, 255));
          gx_text1(F_SMALL, 38, 447, "Enregistrement en cours", RGBA(255, 120, 120, 255)); }
    else if (rec_msg[0] && now_ms() - rec_msg_t < 6000) {
        char rm[160];
        fit_text(F_SMALL, rec_msg, 236, rm, sizeof(rm));
        gx_text1(F_SMALL, 26, 447, rm, RGBA(170, 240, 180, 255));
    }
    gx_rect(25, 430, 241, 1, RGBA(76, 52, 150, 200));

    if (rows > MUSIC_ROWS) {
        f32 track_h = 308.0f;
        f32 th = track_h * MUSIC_ROWS / rows;
        if (th < 18.0f) th = 18.0f;
        f32 ty = 116.0f + (track_h - th) * (f32)start / (f32)(rows - MUSIC_ROWS);
        gx_rrect_fill(270, 116, 3, track_h, 1.5f, RGBA(60, 40, 130, 130), RGBA(60, 40, 130, 130));
        gx_rrect_fill(270, ty, 3, th, 1.5f, RGBA(150, 96, 255, 255), RGBA(110, 60, 230, 255));
    }
}

// ---------- Menu (START) : reglages et Quitter ----------
enum { MI_SHUFFLE, MI_REPEAT, MI_XFADE, MI_VIEW, MI_VOLUME, MI_RG, MI_EQ, MI_BASS, MI_LOWMID, MI_MID, MI_PRES, MI_TREBLE, MI_SKIN, MI_THEME, MI_WALL, MI_SEASON, MI_SAVER, MI_SAVERSTYLE,
       MI_COVERS, MI_LYRICS, MI_PODAUTO, MI_PODSPEED, MI_PODSIL, MI_RESUME, MI_RADIORES, MI_GAPLESS, MI_TV, MI_VIDEO, MI_SCREEN, MI_SCREENH, MI_SCREENY, MI_SCREENX, MI_WIDE, MI_UILANG, MI_LANG, MI_GAMES, MI_GLVIEW, MI_GLANIM, MI_TITLES, MI_PARTY, MI_RUMBLE, MI_SLEEP, MI_NIGHT, MI_LAYOUT, MI_VIZ, MI_SGPAL, MI_LYRSHOW, MI_FLASH, MI_ELEMS, MI_TABS, MI_REC, MI_RECAT, MI_RECLEN, MI_AUTOSTART, MI_IGR, MI_NATIGR, MI_NATCARD, MI_PERF, MI_LAUNCHER, MI_UPDATE,
       MI_HIDDEN, MI_UNKNOWN, MI_GLANG, MI_GSOUND, MI_DHCP, MI_IP, MI_MASK, MI_GW, MI_PHONEAUTO, MI_UPDAUTO, MI_NETSMB, MI_NETFTP, MI_NETFSP, MI_QUIT, MI_COUNT };
static int ipl_get(void);                                  /* v57 : la console demarre sur GC Station (gclib.c) */
static const char *ipl_set(int on);
/* v75 : les reglages de GC Station, ranges par sections (v90 : plus rien de Swiss) */
enum { SEC_MUSIC, SEC_RADIO, SEC_DISPLAY, SEC_VIDEO, SEC_GAMES, SEC_NET, SEC_TOOLS, SEC_COUNT };
static const s8 sec_items[6][24] = {
    { MI_SHUFFLE, MI_REPEAT, MI_XFADE, MI_GAPLESS, MI_VIEW, MI_VOLUME, MI_RG, MI_EQ, MI_BASS, MI_LOWMID, MI_MID, MI_PRES, MI_TREBLE,
      MI_COVERS, MI_LYRICS, MI_RESUME, MI_PARTY, MI_GAMES, MI_SLEEP, -1 },
    { MI_RADIORES, MI_TITLES, MI_REC, MI_RECAT, MI_RECLEN, MI_PODAUTO, MI_PODSPEED, MI_PODSIL, -1 },
    { MI_UILANG, MI_LANG, MI_SKIN, MI_THEME, MI_WALL, MI_SEASON, MI_LAYOUT, MI_ELEMS, MI_TABS, MI_VIZ, MI_SGPAL, MI_LYRSHOW, MI_FLASH,
      MI_SAVER, MI_SAVERSTYLE, MI_NIGHT, MI_HIDDEN, MI_UNKNOWN, MI_PERF, -1 },
    { MI_TV, MI_VIDEO, MI_WIDE, MI_SCREEN, MI_SCREENH, MI_SCREENY, MI_SCREENX, -1 },
    { MI_NATIGR, MI_NATCARD, MI_GLANG, MI_GSOUND, MI_RUMBLE, MI_AUTOSTART, MI_LAUNCHER, MI_IGR, MI_GLVIEW, MI_GLANIM, -1 },
    { MI_DHCP, MI_IP, MI_MASK, MI_GW, MI_NETSMB, MI_NETFTP, MI_NETFSP, MI_PHONEAUTO, MI_UPDAUTO, MI_UPDATE, -1 } };
static int sec_n(int sec) { int n = 0; if (sec < 0 || sec >= SEC_TOOLS) return 0; while (n < 24 && sec_items[sec][n] >= 0) n++; return n; }
static int sec_pos(int sec, int mi) { for (int i = 0; i < sec_n(sec); i++) if (sec_items[sec][i] == mi) return i; return -1; }
static int fe_show_hidden, fe_hide_unknown;                   /* (tab_files.c) */
static void files_refresh_opts(void);                         /* (jeux_cfg.c) */
static const char *const gc_lang_names[6];                     /* (jeux_cfg.c) */
static void ic_put2(const char *key, const char *val, int force);
static void gc_lang_set(int l, int sync);
static void gc_sound_set(int stereo, int sync);
static void gc_screenx_set(int h);

static int menu_sec = 0;

static void igr_value(char *out, int cap);                  /* v39 : retour en jeu vers GC-MP3 (igr.c) */
static void igr_toggle(void);
static void ld_boot(void);
static void ld_value(char *out, int cap);                   /* v44 : lanceur de jeux GC-MP3 (lanceur.c) */
static void ld_change(int a);
static const char *ld_hint(void);
static const char *igr_hint(void);

static void tools_menu_input(u32 down, u32 nav, int fire);
static void tools_menu_draw(f32 x, f32 y, f32 w, int vis, f32 rh, f32 help_y);
#define MENU_VIS 13                         /* lignes visibles (le menu defile) */
static int menu_top = 0;
static int menu_sel = 0, quit_requested = 0;

static void menu_value(int i, char *out, int cap) {
    static const char *const rep[3] = { "Non", "Tout", "Un morceau" };
    static const char *const vid[VID_COUNT] = { "Auto (480p si c\xC3\xA2" "ble composante)", "480i", "480p" };
    switch (i) {
    case MI_SHUFFLE: snprintf(out, cap, "%s", shuffle_on ? "Oui" : "Non"); break;
    case MI_REPEAT:  snprintf(out, cap, "%s", rep[repeat_mode]); break;
    case MI_VIEW:    snprintf(out, cap, "%s", view_names[view_mode]); break;
    case MI_VOLUME:  snprintf(out, cap, "%d %%", volume_pct); break;
    case MI_XFADE:   if (xf_sec) snprintf(out, cap, "%d s", xf_sec); else snprintf(out, cap, "Non"); break;
    case MI_RG:      snprintf(out, cap, "%s", rg_mode == 2 ? "Par album" : rg_mode == 1 ? "Par morceau" : "Non"); break;
    case MI_EQ:      snprintf(out, cap, "%s", eq_names[eq_preset]); break;
    case MI_BASS:    if (eq_bass) snprintf(out, cap, "%+d dB", eq_bass); else snprintf(out, cap, "0"); break;
    case MI_MID:     if (eq_mid) snprintf(out, cap, "%+d dB", eq_mid); else snprintf(out, cap, "0"); break;
    case MI_LOWMID:  if (eq_lowmid) snprintf(out, cap, "%+d dB", eq_lowmid); else snprintf(out, cap, "0"); break;
    case MI_PRES:    if (eq_pres) snprintf(out, cap, "%+d dB", eq_pres); else snprintf(out, cap, "0"); break;
    case MI_PODSPEED: case MI_PODSIL: case MI_RECAT: case MI_RECLEN: x73_menu_value(i, out, cap); break;
    case MI_SCREENX: { int h = SYS_GetDisplayOffsetH(); if (h) snprintf(out, cap, "%s %d", TR(h > 0 ? "Plus \xC3\xA0 droite" : "Plus \xC3\xA0 gauche"), abs(h)); else snprintf(out, cap, "Centr\xC3\xA9" "e"); break; }
    case MI_HIDDEN:  snprintf(out, cap, "%s", fe_show_hidden ? "Affich\xC3\xA9s" : "Cach\xC3\xA9s"); break;
    case MI_UNKNOWN: snprintf(out, cap, "%s", fe_hide_unknown ? "Oui" : "Non"); break;
    case MI_GLANG:   { int l = SYS_GetLanguage(); snprintf(out, cap, "%s", l >= 0 && l < 6 ? gc_lang_names[l] : "?"); break; }
    case MI_GSOUND:  snprintf(out, cap, "%s", SYS_GetSoundMode() == SYS_SOUND_MONO ? "Mono" : "St\xC3\xA9r\xC3\xA9o"); break;
    case MI_DHCP:    snprintf(out, cap, "%s", net_dhcp ? "Oui" : "Non (adresse fixe)"); break;
    case MI_IP:      snprintf(out, cap, "%s", net_dhcp ? (ip_str[0] ? ip_str : "-") : net_cfg_ip[0] ? net_cfg_ip : "(A : taper)"); break;
    case MI_MASK:    { u32 m = net_cfg_bits ? 0xFFFFFFFFu << (32 - net_cfg_bits) : 0; snprintf(out, cap, "%u.%u.%u.%u", m >> 24, (m >> 16) & 255, (m >> 8) & 255, m & 255); break; }
    case MI_GW:      snprintf(out, cap, "%s", net_dhcp ? (gw_str[0] ? gw_str : "-") : net_cfg_gw[0] ? net_cfg_gw : "(A : taper)"); break;
    case MI_PHONEAUTO: snprintf(out, cap, "%s", phone_auto ? "Oui" : "Non"); break;
    case MI_UPDAUTO: snprintf(out, cap, "%s", upd_auto ? "Oui" : "Non"); break;
    case MI_NETSMB: case MI_NETFTP: case MI_NETFSP: nfs_menu_value(i - MI_NETSMB, out, cap); break;   /* v86 */
    case MI_TREBLE:  if (eq_treble) snprintf(out, cap, "%+d dB", eq_treble); else snprintf(out, cap, "0"); break;
    case MI_LYRICS:  snprintf(out, cap, "%s", lyrics_auto ? "Oui" : "Non"); break;
    case MI_PODAUTO: snprintf(out, cap, "%s", pod_auto ? "T\xC3\xA9l\xC3\xA9" "charg\xC3\xA9s tout seuls" : "Non"); break;
    case MI_WALL:    snprintf(out, cap, "%s", wall_on && wall_path[0] ? "Ma photo" : "Violet"); break;
    case MI_SEASON: {
        static const char *const sn[4] = { "Oui (rien en ce moment)", "Oui (Halloween)", "Oui (No\xC3\xABl)", "Oui (\xC3\xA9t\xC3\xA9)" };
        int k = season_on ? season_now() : 0;
        snprintf(out, cap, "%s", season_on ? sn[k] : "Non");
        break;
    }
    case MI_GAMES:   snprintf(out, cap, "(A)"); break;
    case MI_TITLES:  snprintf(out, cap, "(A)"); break;
    case MI_SKIN:    snprintf(out, cap, "%s", ui_skin == 3 ? "Aurore" : ui_skin == 2 ? "Nouvelle" : ui_skin ? "Moderne" : "Classique"); break;
    case MI_GLVIEW:  { static const char *const nm[6] = { "Grille", "Carrousel", "Liste", "Mur de jaquettes", "Vitrine", "\xC3\x89tag\xC3\xA8re" };
                       snprintf(out, cap, "%s", nm[gl_view >= 0 && gl_view < 6 ? gl_view : 0]); } break;
    case MI_RUMBLE:  snprintf(out, cap, "%s", rumble_on ? "Oui" : "Non"); break;
    case MI_PARTY:   snprintf(out, cap, "%s", party_names[party_style]); break;
    case MI_NIGHT:   snprintf(out, cap, "Luminosit\xC3\xA9 %d / 5", night_lum); break;
    case MI_UPDATE:  update_value(out, cap); break;
    case MI_AUTOSTART: snprintf(out, cap, "%s", ipl_get() ? "Oui" : "Non"); break;
    case MI_IGR:     igr_value(out, cap); break;
    case MI_LAUNCHER: ld_value(out, cap); break;
    case MI_LAYOUT:  snprintf(out, cap, "%s (%s)", TR(layout_names[player_layout]), TR(tab_names[IS_PLAYER_TAB(tab) ? tab : TAB_MUSIC])); break;
    case MI_GLANIM:  snprintf(out, cap, "%s", gl_anim ? "Oui" : "Non"); break;
    case MI_NATIGR:  snprintf(out, cap, "%s", nat_igr ? "Oui" : "Non"); break;
    case MI_NATCARD: snprintf(out, cap, "%s", nat_card ? "Oui (slot A)" : "Non"); break;
    case MI_PERF:    snprintf(out, cap, "%s", perf_on ? "Oui" : "Non"); break;
    case MI_LYRSHOW: snprintf(out, cap, "%s", lyr_names[lyr_show]); break;
    case MI_FLASH:   snprintf(out, cap, "%s", flash_names[flash_mode]); break;
    case MI_VIZ:     snprintf(out, cap, "%s", viz_names[viz_style]); break;
    case MI_SGPAL:   snprintf(out, cap, "%s", sgp_names[sg_pal_id]); break;
    case MI_ELEMS: { int c = 0; for (int i = 0; i < EL_COUNT; i++) c += EL(i); snprintf(out, cap, "%d / %d  (A)", c, EL_COUNT); break; }
    case MI_TABS:  { int c = 0; for (int i = 0; i < TAB_COUNT; i++) c += tab_shown(i); snprintf(out, cap, "%d / %d  (A)", c, TAB_COUNT); break; }
    case MI_SLEEP:
        if (sleep_eot) snprintf(out, cap, "\xC3\x80 la fin du morceau");
        else if (!sleep_at) snprintf(out, cap, "Non");
        else { u64 n = now_ms(); int m = n >= sleep_at ? 0 : (int)((sleep_at - n + 59999) / 60000); snprintf(out, cap, "Arr\xC3\xAAt dans %d min", m); }
        break;
    case MI_THEME:   snprintf(out, cap, "%s", theme_names[theme_id]); break;
    case MI_SAVER:   if (saver_min) snprintf(out, cap, "Apr\xC3\xA8s %d min", saver_min); else snprintf(out, cap, "Jamais"); break;
    case MI_SAVERSTYLE: snprintf(out, cap, "%s", saver_names[saver_style]); break;
    case MI_COVERS:  snprintf(out, cap, "%s", covers_auto ? "Oui" : "Non"); break;
    case MI_RESUME:  snprintf(out, cap, "%s", resume_on ? "Oui" : "Non"); break;
    case MI_RADIORES: snprintf(out, cap, "%s", radio_resume ? "Oui" : "Non"); break;
    case MI_GAPLESS: snprintf(out, cap, "%s", gapless_on ? "Oui (m\xC3\xAAme album)" : "Non"); break;
    case MI_VIDEO: {
        int want = video_pref == VID_AUTO ? -1 : video_pref;
        snprintf(out, cap, "%s%s", vid[video_pref], want >= 0 && want != vid_active ? "  (au red\xC3\xA9marrage)" : "");
        break;
    }
    case MI_TV:      snprintf(out, cap, "%s", tv_crt ? "Cathodique (480i, 4:3)" : "\xC3\x89" "cran plat"); break;
    case MI_SCREEN:  snprintf(out, cap, scr_pct > 100 ? "%d %% (plus large)" : "%d %%", scr_pct); break;
    case MI_SCREENH: snprintf(out, cap, "%d %%", scr_hpct); break;
    case MI_SCREENY: if (scr_vpos) snprintf(out, cap, "%s %d", TR(scr_vpos > 0 ? "Plus bas" : "Plus haut"), abs(scr_vpos)); else snprintf(out, cap, "Centr\xC3\xA9" "e"); break;
    case MI_WIDE:    snprintf(out, cap, "%s", scr_wide ? "16:9 (\xC3\xA9" "cran large)" : "4:3"); break;
    case MI_UILANG:
        if (ui_lang < 0) snprintf(out, cap, "Auto (%s)", ui_en ? "English" : "Fran\xC3\xA7" "ais");
        else snprintf(out, cap, "%s", ui_lang ? "English" : "Fran\xC3\xA7" "ais");
        break;
    case MI_LANG:
        if (lang_pref < 0) snprintf(out, cap, "Auto (%s)", lang_names[lang_console()]);
        else snprintf(out, cap, "%s", lang_names[lang_pref]);
        break;
    case MI_REC:
        if (rec_fp) snprintf(out, cap, "En cours (%ld Ko)", rec_bytes / 1024);
        else snprintf(out, cap, "%s", src == SRC_RADIO && state != ST_IDLE ? "Arr\xC3\xAAt\xC3\xA9" : "(\xC3\xA9" "coute une radio)");
        break;
    default: out[0] = 0;
    }
}

static const char *const menu_labels[MI_COUNT] = {
    "Lecture al\xC3\xA9" "atoire", "R\xC3\xA9p\xC3\xA9tition", "Fondu encha\xC3\xAEn\xC3\xA9", "Liste MUSIQUE", "Volume",
    "Volume \xC3\xA9galis\xC3\xA9", "\xC3\x89galiseur", "Graves", "Bas-m\xC3\xA9" "diums", "M\xC3\xA9" "diums", "Pr\xC3\xA9sence", "Aigus", "Style de l'interface", "Th\xC3\xA8me", "Fond d'\xC3\xA9" "cran", "D\xC3\xA9" "cor de saison",
    "\xC3\x89" "conomiseur d'\xC3\xA9" "cran", "Style de l'\xC3\xA9" "conomiseur", "Pochettes automatiques", "Paroles automatiques", "Nouveaux podcasts", "Vitesse des podcasts", "Podcasts : sauter les silences",
    "Reprendre au d\xC3\xA9marrage", "Reprendre la radio au d\xC3\xA9marrage", "Encha\xC3\xAEnement sans blanc", "Type de t\xC3\xA9l\xC3\xA9", "Vid\xC3\xA9o", "Taille de l'\xC3\xA9" "cran", "Hauteur de l'\xC3\xA9" "cran", "Position verticale", "Position horizontale", "Format de l'\xC3\xA9" "cran", "Langue / Language", "Langue du contenu",
    "Jeux et karaok\xC3\xA9", "Affichage des jeux GameCube", "Animations (jeux / applis)", "Titres radio gard\xC3\xA9s", "Mode soir\xC3\xA9" "e", "Vibrations de la manette", "Minuterie", "Mode veilleuse", "Disposition du lecteur", "Style du spectre", "Couleurs du spectrogramme", "Paroles dans le lecteur", "\xC3\x89" "clair sur les basses",
    "\xC3\x89l\xC3\xA9ments du lecteur", "Onglets affich\xC3\xA9s", "Enregistrer la radio", "Enregistrement programm\xC3\xA9", "Dur\xC3\xA9" "e de l'enregistrement programm\xC3\xA9", "D\xC3\xA9marrer la console sur GC Station", "A+Z+START (lanceur de secours)", "A+Z+START (Pulse)", "Carte m\xC3\xA9moire virtuelle (Pulse)", "Mesure de fluidit\xC3\xA9", "Lanceur de secours", "Mise \xC3\xA0 jour",
    "Fichiers cach\xC3\xA9s (FICHIERS)", "Cacher les fichiers inconnus", "Langue des jeux (console)", "Son (console)",
    "Adresse automatique (DHCP)", "Adresse IP de la console", "Masque", "Passerelle (box)", "T\xC3\xA9l\xC3\xA9" "commande au d\xC3\xA9marrage",
    "Chercher les mises \xC3\xA0 jour au d\xC3\xA9marrage", "Partage SMB (PC, NAS)", "Serveur FTP", "Serveur FSP", "Quitter GC Station" };

static void web_relayout(void);
static void scr_set_wide(int w) {
    scr_wide = w;
    wv_lw = w ? WV_W * 4 / 3 : WV_W;
    if (pg.nitems) web_relayout();
}

static void menu_change(int i, int d) {
    static const int savers[] = { 0, 2, 5, 10, 30 };
    switch (i) {
    case MI_SHUFFLE: set_shuffle(!shuffle_on); break;
    case MI_REPEAT:  repeat_mode = (repeat_mode + (d > 0 ? 1 : 2)) % 3; break;
    case MI_VIEW:    set_view(view_mode + d); break;
    case MI_VOLUME:  volume_pct += d * 10; if (volume_pct < 0) volume_pct = 0; if (volume_pct > 100) volume_pct = 100; break;
    case MI_THEME:   theme_apply(theme_id + d); break;
    case MI_SAVER: {
        int k = 0;
        for (int j = 0; j < 5; j++) if (savers[j] == saver_min) k = j;
        k = (k + d + 5) % 5;
        saver_min = savers[k];
        break;
    }
    case MI_SAVERSTYLE: saver_style = (saver_style + d + SV_COUNT) % SV_COUNT; break;
    case MI_XFADE: {
        static const int xs[] = { 0, 2, 4, 6, 8 };
        int k = 0;
        for (int j = 0; j < 5; j++) if (xs[j] == xf_sec) k = j;
        xf_sec = xs[(k + d + 5) % 5];
        break;
    }
    case MI_RG:      rg_mode = (rg_mode + d + 3) % 3; break;   /* pris en compte au morceau suivant */
    case MI_EQ:
        eq_preset = (eq_preset + d + EQ_PRESETS) % EQ_PRESETS;
        if (eq_preset) { eq_bass = eq_vals[eq_preset][0]; eq_lowmid = eq_vals[eq_preset][1]; eq_mid = eq_vals[eq_preset][2]; eq_pres = eq_vals[eq_preset][3]; eq_treble = eq_vals[eq_preset][4]; }
        eq_setup();
        break;
    case MI_MID:     eq_mid += 2 * d; eq_preset = 0; eq_setup(); break;
    case MI_LOWMID:  eq_lowmid += 2 * d; eq_preset = 0; eq_setup(); break;
    case MI_PRES:    eq_pres += 2 * d; eq_preset = 0; eq_setup(); break;
    case MI_PODSPEED: case MI_PODSIL: case MI_RECAT: case MI_RECLEN: x73_menu_change(i, d); break;
    case MI_SCREENX: gc_screenx_set(SYS_GetDisplayOffsetH() + 2 * d); return;
    case MI_HIDDEN:  fe_show_hidden = !fe_show_hidden; files_refresh_opts(); break;
    case MI_UNKNOWN: fe_hide_unknown = !fe_hide_unknown; files_refresh_opts(); break;
    case MI_GLANG:   gc_lang_set((SYS_GetLanguage() + d + 6) % 6, 1); return;
    case MI_GSOUND:  gc_sound_set(SYS_GetSoundMode() == SYS_SOUND_MONO, 1); return;
    case MI_DHCP:    net_dhcp = !net_dhcp; lib_note("Pris en compte au prochain d\xC3\xA9marrage de GC Station"); break;
    case MI_MASK:    net_cfg_bits += d; if (net_cfg_bits < 8) net_cfg_bits = 8; if (net_cfg_bits > 30) net_cfg_bits = 30; break;
    case MI_IP: case MI_GW:
        if (net_dhcp) { lib_note("Adresse automatique : mets d'abord DHCP sur Non"); return; }
        kb_start(KB_NETTXT, i == MI_IP ? net_cfg_ip : net_cfg_gw, i, -1);
        return;
    case MI_PHONEAUTO: phone_auto = !phone_auto; break;
    case MI_UPDAUTO: upd_auto = !upd_auto; break;
    case MI_NETSMB: case MI_NETFTP: case MI_NETFSP: nfs_ui_open(i - MI_NETSMB); return;   /* v86 */
    case MI_BASS:    eq_bass += 2 * d; eq_preset = 0; eq_setup(); break;
    case MI_TREBLE:  eq_treble += 2 * d; eq_preset = 0; eq_setup(); break;
    case MI_LYRICS:  lyrics_auto = !lyrics_auto; if (lyrics_auto) net_start_async(); break;
    case MI_PODAUTO: pod_auto = !pod_auto; if (pod_auto) { net_start_async(); podauto_kick(); } break;
    case MI_WALL:
        if (!wall_path[0]) { wall_on = 0; lib_note("Choisis une photo : onglet PHOTOS, plein \xC3\xA9" "cran, X"); return; }
        wall_on = !wall_on;
        wall_reload();
        break;
    case MI_SEASON:  season_on = !season_on; break;
    case MI_GAMES:   return;
    case MI_TITLES:  return;
    case MI_SKIN:    ui_skin = (ui_skin + (d < 0 ? 3 : 1)) % 4; break;
    case MI_GLVIEW:  gl_view = (gl_view + d + 6) % 6; break;
    case MI_GLANIM:  gl_anim = !gl_anim; break;
    case MI_NATIGR:  nat_igr = !nat_igr; break;
    case MI_NATCARD: nat_card = !nat_card; break;
    case MI_PERF:    perf_toggle(); return;
    case MI_RUMBLE:  rumble_on = !rumble_on; if (rumble_on) rumble(0, 200); ic_put2("Disable PAD Rumble", rumble_on ? "No" : "Yes", 1); break;
    case MI_PARTY:   party_style = (party_style + d + PARTY_STYLES) % PARTY_STYLES; break;
    case MI_NIGHT:   night_lum += d; if (night_lum < 1) night_lum = 1; if (night_lum > 5) night_lum = 5; break;
    case MI_UPDATE:  return;
    case MI_AUTOSTART: { const char *e = ipl_set(!ipl_get()); if (e) lib_note(e); } return;
    case MI_IGR:     igr_toggle(); return;
    case MI_LAUNCHER: ld_change(0); return;
    case MI_LAYOUT:  player_layout = (player_layout + d + LAYOUT_COUNT) % LAYOUT_COUNT;
                     if (player_layout == L_LIST && IS_PLAYER_TAB(tab) && tab != TAB_MUSIC) player_layout = (player_layout + d + LAYOUT_COUNT) % LAYOUT_COUNT;
                     break;
    case MI_LYRSHOW: lyr_show = (lyr_show + d + 3) % 3; break;
    case MI_FLASH:   flash_mode = (flash_mode + d + 3) % 3; break;
    case MI_VIZ:     viz_style = (viz_style + d + VZ_COUNT) % VZ_COUNT; break;
    case MI_SGPAL:   sg_pal_id = (sg_pal_id + d + SGP_COUNT) % SGP_COUNT; break;
    case MI_ELEMS:   checklist_open(&ck_elems); return;
    case MI_TABS:    checklist_open(&ck_tabs); return;
    case MI_SLEEP: {
        static const int sl[] = { 0, -1, 5, 10, 20, 30, 45, 60, 90, 120 };   /* -1 = fin du morceau */
        const int N = (int)(sizeof(sl) / sizeof(sl[0]));
        int k = 0;
        for (int j = 0; j < N; j++) if (sl[j] == sleep_min) k = j;
        sleep_min = sl[(k + d + N) % N];
        sleep_eot = (sleep_min == -1);
        sleep_at = sleep_min > 0 ? now_ms() + (u64)sleep_min * 60000 : 0;
        sleep_fade = 256;
        return;                                      /* (pas enregistre dans les reglages) */
    }
    case MI_COVERS:  covers_auto = !covers_auto; if (covers_auto) net_start_async(); break;
    case MI_RESUME:  resume_on = !resume_on; break;
    case MI_RADIORES: radio_resume = !radio_resume; break;
    case MI_GAPLESS: gapless_on = !gapless_on; break;
    case MI_VIDEO:   video_pref = (video_pref + d + VID_COUNT) % VID_COUNT; if (video_pref != VID_480I) tv_crt = 0;
                     ic_put("Apps Video Mode", video_pref == VID_480I ? "480i" : video_pref == VID_480P ? "480p" : "Auto"); break;
    case MI_TV:
        tv_crt = !tv_crt;
        if (tv_crt) { video_pref = VID_480I; scr_set_wide(0); }
        else if (video_pref == VID_480I) video_pref = VID_AUTO;
        if (tv_crt && vid_active != VID_480I) lib_note("Red\xC3\xA9marre GC Station pour passer en 480i");
        break;
    case MI_WIDE:    scr_set_wide(!scr_wide); break;
    case MI_SCREENH: scr_hpct += 2 * d; if (scr_hpct < 80) scr_hpct = 80; if (scr_hpct > 100) scr_hpct = 100; break;
    case MI_SCREENY: scr_vpos += 2 * d; if (scr_vpos < -24) scr_vpos = -24; if (scr_vpos > 24) scr_vpos = 24; break;
    case MI_SCREEN:  scr_pct += 2 * d; if (scr_pct < 80) scr_pct = 80; if (scr_pct > SCR_MAX) scr_pct = SCR_MAX; scr_vi(); break;
    case MI_UILANG:  ui_lang = (ui_lang + 1 + d + 3) % 3 - 1; ui_lang_apply(); break;
    case MI_LANG:    lang_pref = (lang_pref + 1 + d + LANG_COUNT + 1) % (LANG_COUNT + 1) - 1; break;
    case MI_REC:
        if (rec_fp) rec_stop();
        else if (src == SRC_RADIO && state != ST_IDLE && cur_station >= 0) rec_start(stations[cur_station].name);
        break;
    }
    settings_dirty = 1;
}

static void menu_input_gcmp3(u32 down, u32 nav, int fire);
static void menu_input(u32 down, u32 nav, int fire) {
    if (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) {                /* v35 : sections */
        static s8 last[SEC_COUNT] = { -1, -1, -1, -1, -1, -1, -1 };
        if (menu_sec < SEC_TOOLS) last[menu_sec] = (s8)menu_sel;
        menu_sec = (menu_sec + ((down & PAD_TRIGGER_R) ? 1 : SEC_COUNT - 1)) % SEC_COUNT;
        if (menu_sec < SEC_TOOLS) { menu_sel = last[menu_sec] >= 0 ? last[menu_sec] : sec_items[menu_sec][0]; menu_top = 0; }
        return;
    }
    if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) {
        menu_open = 0;
        if (settings_dirty) save_settings();
        return;
    }
    if (menu_sec == SEC_TOOLS) tools_menu_input(down, nav, fire);
    else menu_input_gcmp3(down, nav, fire);
}
static void menu_input_gcmp3(u32 down, u32 nav, int fire) {
    int n = sec_n(menu_sec), p = sec_pos(menu_sec, menu_sel);
    if (p < 0) { p = 0; menu_sel = sec_items[menu_sec][0]; }
    if (fire && (nav & PAD_BUTTON_DOWN)) menu_sel = sec_items[menu_sec][(p + 1) % n];
    if (fire && (nav & PAD_BUTTON_UP)) menu_sel = sec_items[menu_sec][(p + n - 1) % n];
    if (fire && (nav & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) && menu_sel != MI_QUIT)
        menu_change(menu_sel, (nav & PAD_BUTTON_RIGHT) ? 1 : -1);
    if (down & PAD_BUTTON_A) {
        crumb("menu : %s", menu_labels[menu_sel]);
        if (menu_sel == MI_QUIT) quit_requested = 1;
        else if (menu_sel >= MI_NETSMB && menu_sel <= MI_NETFSP) { nfs_ui_open(menu_sel - MI_NETSMB); return; }   /* v86 */
        else if (menu_sel == MI_PARTY) {                      /* A = lancer le mode soiree */
            menu_open = 0;
            if (settings_dirty) save_settings();
            party_start();
            return;
        }
        else if (menu_sel == MI_NIGHT) {                      /* A = lancer la veilleuse */
            menu_open = 0;
            if (settings_dirty) save_settings();
            night_start();
            return;
        }
        else if (menu_sel == MI_UPDATE) { update_activate(); return; }
        else if (menu_sel == MI_IP || menu_sel == MI_GW) { menu_change(menu_sel, 1); return; }
        else if (menu_sel == MI_AUTOSTART) { menu_change(MI_AUTOSTART, 1); return; }
        else if (menu_sel == MI_IGR) { igr_toggle(); return; }
        else if (menu_sel == MI_LAUNCHER) { ld_change(1); return; }
        else if (menu_sel == MI_GAMES || menu_sel == MI_TITLES) {    /* A = ouvrir la liste */
            menu_open = 0;
            if (settings_dirty) save_settings();
            if (menu_sel == MI_GAMES) games_open(); else titles_open();
            return;
        }
        else if (menu_sel == MI_SAVERSTYLE) {                 /* A = apercu de l'economiseur */
            menu_open = 0;
            if (settings_dirty) save_settings();
            saver_on = 1;
            return;
        }
        else menu_change(menu_sel, 1);
    }
}

/* v35 : legende de la page Reglages (remplace celle de l'onglet) */
static void draw_mini_badge(f32 cx, f32 cy, const char *k);
static void draw_menu_legend(int kind) {
    const char *K[7], *L[7];
    int cnt = 0;
    K[cnt] = "A"; L[cnt++] = kind == 2 ? "OUVRIR" : "CHANGER";
    if (kind == 1) { K[cnt] = "Y"; L[cnt++] = "ENREGISTRER"; }
    K[cnt] = "B"; L[cnt++] = "FERMER";
    K[cnt] = "L"; L[cnt++] = "";
    K[cnt] = "R"; L[cnt++] = "SECTION";
    const f32 lx = 11, lw = 618, cy = 438.5f;
    gx_rrect_fill(lx, 419, lw, 39, 9, RGBA(10, 6, 30, 255), RGBA(10, 6, 30, 255));
    legend_box(lx, lw);
    f32 total = 0;
    int gaps = 0;
    for (int i = 0; i < cnt; i++) { total += (L[i][0] ? 25 : 22) + text_width(F_SMALL, L[i], 1); if (L[i][0]) gaps++; }
    f32 space = (lw - 40 - total) / (gaps > 1 ? gaps - 1 : 1), cx = lx + 20;
    for (int i = 0; i < cnt; i++) {
        draw_mini_badge(cx + 10, cy, K[i]);
        cx += L[i][0] ? 25 : 22;
        if (L[i][0]) cx += gx_text(F_SMALL, cx, cy + 4.5f, L[i], RGBA(208, 198, 240, 255), RGBA(208, 198, 240, 255), 1) + space;
    }
}
static void draw_menu(void) {
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
    /* v35 : page plus large, sections en haut (L / R) */
    const f32 x = 40, y = 40, w = 560, rh = 24, h = 384;
    const int vis = 10;
    const f32 ly = y + 84, help_y = y + h - 40;
    gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 255), RGBA(8, 5, 26, 255));
    draw_panel(x, y, w, h);
    gx_text1(F_LARGE, x + 18, y + 32, "R\xC3\xA9glages", RGBA(255, 255, 255, 255));
    gx_text1(F_SMALL, x + 150, y + 30, ui_en ? "L / R : sections" : "L / R : sections", RGBA(150, 135, 210, 255));

    {   /* v57 : sections sur une 2e ligne (7 sections) */
        static const char *const sec_fr[SEC_COUNT] = { "Musique", "Radio", "Affichage", "Vid\xC3\xA9o", "Jeux", "R\xC3\xA9seau", "Outils" };
        static const char *const sec_en[SEC_COUNT] = { "Music", "Radio", "Display", "Video", "Games", "Network", "Tools" };
        f32 tws[SEC_COUNT], tot = 0, avail = w - 36;
        for (int p = 0; p < SEC_COUNT; p++) { tws[p] = (f32)text_width(F_SMALL, ui_en ? sec_en[p] : sec_fr[p], 0); tot += tws[p]; }
        f32 pad = (avail - tot - 5 * (SEC_COUNT - 1)) / SEC_COUNT;
        if (pad > 26) pad = 26;
        if (pad < 6) pad = 6;
        f32 px = x + 18;
        for (int p = 0; p < SEC_COUNT; p++) {
            const char *nm = ui_en ? sec_en[p] : sec_fr[p];
            f32 tw = tws[p] + pad;
            if (p == menu_sec) {
                gx_glow(px, y + 44, tw, 24, 8, RGBA(150, 80, 255, 120), 2);
                gx_rrect_fill(px, y + 44, tw, 24, 8, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
            } else gx_rrect_fill(px, y + 44, tw, 24, 8, RGBA(34, 20, 80, 255), RGBA(26, 14, 64, 255));
            gp_zone(px, y + 44, tw, 24, &menu_sec, p, GPZ_SEL);
            gx_text1(F_SMALL, px + pad / 2, y + 61, nm, p == menu_sec ? RGBA(255, 255, 255, 255) : RGBA(170, 155, 225, 255));
            px += tw + 5;
        }
    }
    gx_rect(x + 18, y + h - 58, w - 36, 1, RGBA(76, 52, 150, 200));

    if (menu_sec == SEC_TOOLS) {
        tools_menu_draw(x + 12, ly, w - 24, vis, rh, help_y);
        draw_menu_legend(2);
        return;
    }
    const s8 *L = sec_items[menu_sec];
    int LN = sec_n(menu_sec), ps = sec_pos(menu_sec, menu_sel);
    if (ps < 0) { ps = 0; menu_sel = L[0]; }
    if (ps < menu_top) menu_top = ps;
    if (ps >= menu_top + vis) menu_top = ps - vis + 1;
    if (menu_top > LN - vis) menu_top = LN - vis;
    if (menu_top < 0) menu_top = 0;
    char v[96], fv[96];
    if (menu_top > 0)                                   /* fleches : il y a d'autres reglages */
        gx_tri(x + w / 2 - 7, ly - 2, x + w / 2 + 7, ly - 2, x + w / 2, ly - 8, RGBA(190, 160, 255, 220));
    if (menu_top + vis < LN)
        gx_tri(x + w / 2 - 7, ly + vis * rh, x + w / 2 + 7, ly + vis * rh, x + w / 2, ly + vis * rh + 6, RGBA(190, 160, 255, 220));
    {
        static f32 sel_anim = -2000;
        f32 sy = smooth_to(&sel_anim, ly + (ps - menu_top) * rh);
        gx_glow(x + 12, sy, w - 24, rh - 2, 6, RGBA(150, 80, 255, 120), 2);
        gx_rrect_fill(x + 12, sy, w - 24, rh - 2, 6, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
    }
    for (int r = menu_top; r < LN && r < menu_top + vis; r++) {
        int i = L[r];
        f32 ry = ly + (r - menu_top) * rh;
        int on = (i == menu_sel);
        gp_zone(x + 12, ry, w - 24, rh - 2, &menu_sel, i, GPZ_SEL);
        if (i == MI_QUIT) gx_rect(x + 18, ry - 2, w - 36, 1, RGBA(76, 52, 150, 200));
        col_t lc = (i == MI_QUIT) ? RGBA(255, 170, 170, 255) : RGBA(230, 222, 255, 255);
        gx_text1(F_MED, x + 26, ry + 17, menu_labels[i], on ? RGBA(255, 255, 255, 255) : lc);
        menu_value(i, v, sizeof(v));
        if (v[0]) {
            fit_text(F_MED, v, 230, fv, sizeof(fv));
            int vw = text_width(F_MED, fv, 0);
            f32 vx = x + w - 34 - vw;
            gx_text1(F_MED, vx, ry + 17, fv, on ? RGBA(255, 255, 255, 255) : RGBA(190, 160, 255, 255));
            if (on && i != MI_REC) {
                gx_tri(vx - 12, ry + 12, vx - 6, ry + 7, vx - 6, ry + 17, RGBA(255, 255, 255, 200));
                gx_tri(x + w - 28, ry + 7, x + w - 28, ry + 17, x + w - 22, ry + 12, RGBA(255, 255, 255, 200));
            }
        }
    }
    const char *hint = NULL;
    if (menu_sel == MI_SAVERSTYLE) hint = "Gauche / Droite : changer   A : aper\xC3\xA7u (une touche pour revenir)";
    if (menu_sel == MI_PARTY) hint = "A : lancer   (ou stick C vers le haut, dans MUSIQUE et RADIO)";
    if (menu_sel == MI_NIGHT) hint = "A : lancer   Gauche / Droite : luminosit\xC3\xA9   (F2 sur le clavier)";
    if (menu_sel == MI_UPDATE) hint = "Nouvelle version depuis GitHub, l'ancienne est gard\xC3\xA9" "e (.ancien)";
    if (menu_sel == MI_SLEEP) hint = "La musique baisse doucement puis s'arr\xC3\xAAte";
    if (menu_sel == MI_RG) hint = "Selon les \xC3\xA9tiquettes ReplayGain des fichiers (au morceau suivant)";
    if (menu_sel == MI_XFADE) hint = "Les morceaux se m\xC3\xA9langent en fin de lecture (pas avec L / R)";
    if (menu_sel == MI_VIDEO) hint = "Pris en compte au prochain lancement (B maintenu au d\xC3\xA9marrage = 480i)";
    if (menu_sel == MI_AUTOSTART) hint = "Copie GC Station dans ipl.dol (PicoLoader) : la console et le bouton Reset ouvrent GC Station";
    if (menu_sel == MI_IGR) hint = igr_hint();
    if (menu_sel == MI_GAPLESS) hint = "Les morceaux d'un m\xC3\xAAme album s'encha\xC3\xAEnent sans silence (albums live, mixes)";
    if (menu_sel == MI_RADIORES) hint = "La derni\xC3\xA8re radio \xC3\xA9" "cout\xC3\xA9" "e repart toute seule (si rien d'autre ne joue)";
    if (menu_sel == MI_PODSPEED) hint = "Plus rapide sans voix aigu\xC3\xAB (podcasts seulement)";
    if (menu_sel == MI_PODSIL) hint = "Les blancs de plus d'un quart de seconde sont saut\xC3\xA9s (podcasts seulement)";
    if (menu_sel == MI_RECAT) hint = "\xC3\x80 cette heure, la radio choisie est lanc\xC3\xA9" "e et enregistr\xC3\xA9" "e (SD:/gc-mp3/radio/enregistrements)";
    if (menu_sel == MI_RECLEN) hint = "La radio enregistr\xC3\xA9" "e est celle qui joue quand tu r\xC3\xA8gles l'heure";
    if (menu_sel == MI_LOWMID || menu_sel == MI_PRES) hint = "\xC3\x89galiseur 5 bandes : graves, bas-m\xC3\xA9" "diums, m\xC3\xA9" "diums, pr\xC3\xA9sence, aigus";
    if (menu_sel == MI_PERF) hint = "Images/s en haut de l'\xC3\xA9" "cran ; en coupant : rapport SD:/gc-mp3/fluidite.txt";
    if (menu_sel == MI_NATCARD) hint = "Jeux lanc\xC3\xA9s avec Pulse : sauvegardes dans SD:/gc-mp3/resident (choix : Outils > Cartes virtuelles)";
    if (menu_sel == MI_NATIGR) hint = "Jeux lanc\xC3\xA9s avec Pulse (A) : A+Z+START tenus 1 s = retour \xC3\xA0 GC Station";
    if (menu_sel == MI_LAUNCHER) hint = ld_hint();
    if (menu_sel == MI_GLANIM) hint = "D\xC3\xA9" "filement doux, cadre qui glisse, carrousel (onglet JEUX / APPS)";
    if (menu_sel == MI_QUIT) hint = "Ouvre le menu de la GameCube (lecteur de disque). Bouton Reset = relancer GC Station";
    if (menu_sel == MI_LAYOUT) hint = "Une disposition par onglet (MUSIQUE, RADIO, R\xC3\x89TRO, PODCASTS) : change d'onglet pour r\xC3\xA9gler les autres";
    if (menu_sel == MI_LYRSHOW) hint = "Paroles des musiques (fichiers .lrc) : \xC3\xA0 la place du spectre ou par-dessus";
    if (menu_sel == MI_FLASH) hint = "Le flash blanc du mode soir\xC3\xA9" "e sur chaque coup de basse";
    if (menu_sel == MI_TABS) hint = "A : afficher / cacher un onglet   X / Y : changer l'ordre";
    if (menu_sel == MI_SGPAL) hint = "Spectrogramme plein \xC3\xA9" "cran (\xC3\xA9" "conomiseur, mode soir\xC3\xA9" "e) et style Spectrogramme";
    if (menu_sel == MI_RUMBLE) hint = "Mode soir\xC3\xA9" "e, mini-jeux, et les jeux lanc\xC3\xA9s avec le lanceur de secours";
    if (menu_sel == MI_SCREENX) hint = "D\xC3\xA9" "cale l'image de GC Station et des jeux (r\xC3\xA9glage de la console)";
    if (menu_sel == MI_GLANG) hint = "Langue de la console : les jeux multilingues (PAL) l'utilisent. Un jeu peut avoir la sienne (Y dans sa fiche)";
    if (menu_sel == MI_GSOUND) hint = "R\xC3\xA9glage de la console : GC Station et les jeux";
    if (menu_sel == MI_HIDDEN || menu_sel == MI_UNKNOWN) hint = "Onglet FICHIERS : fichiers commen\xC3\xA7" "ant par un point / autres que musique, images, jeux, .dol";
    if (menu_sel == MI_DHCP) hint = "Non : adresse fixe (lignes suivantes). Pris en compte au prochain d\xC3\xA9marrage";
    if (menu_sel == MI_IP || menu_sel == MI_GW) hint = "A : taper l'adresse (DHCP sur Non). Pris en compte au prochain d\xC3\xA9marrage";
    if (menu_sel == MI_PHONEAUTO) hint = "La page du t\xC3\xA9l\xC3\xA9phone (onglet T\xC3\x89L\xC3\x89PHONE) est pr\xC3\xAAte d\xC3\xA8s le d\xC3\xA9marrage";
    if (menu_sel >= MI_NETSMB && menu_sel <= MI_NETFSP) hint = "A : adresse, utilisateur, mot de passe, test ; puis onglet FICHIERS (B depuis la carte SD)";
    if (menu_sel == MI_UPDAUTO) hint = "Une nouvelle version est signal\xC3\xA9" "e au d\xC3\xA9marrage (rien n'est install\xC3\xA9 sans toi)";
    if (hint) gx_text1(F_SMALL, x + 20, help_y + 8, hint, RGBA(255, 210, 120, 255));
    draw_menu_legend(0);
}
/* ---------- Economiseur d'ecran ---------- */
static u64 last_input_ms = 0;                /* (aussi declare plus haut, bib_pump) */

/* Ce qui joue en ce moment : titre (et artiste ou station) */
static void saver_title(char *t, int tcap, char *sub, int scap) {
    char raw[200];
    t[0] = sub[0] = 0;
    raw[0] = 0;
    if (src == SRC_FILE && playing && cur_track >= 0) {
        track_title(cur_track, raw, sizeof(raw));
        if (artist[0]) text_display(artist, sub, scap);
    } else if (src == SRC_RADIO && cur_station >= 0 && state != ST_IDLE) {
        snprintf(raw, sizeof(raw), "%s", now_title[0] ? now_title : stations[cur_station].name);
        if (now_title[0]) text_display(stations[cur_station].name, sub, scap);
    }
    if (raw[0]) text_display(raw, t, tcap);
}

static int saver_has_cover(void) {
    if (!cover_ok) return 0;
    if (src == SRC_FILE) return playing && cur_track >= 0;
    if (src == SRC_RADIO) return cur_station >= 0 && state != ST_IDLE;
    return 0;
}

/* titre centre (ajuste a la largeur) */
static void saver_line(int f, f32 cx, f32 y, const char *s, int maxw, col_t c) {
    char buf[200];
    if (!s[0]) return;
    fit_text(f, s, maxw, buf, sizeof(buf));
    gx_text_c(f, cx, y, buf, c);
}

/* ---- Spectre (style d'origine) ---- */
static void saver_spectre(f32 t) {
    f32 cx = 320 + 150 * sinf(t * 0.050f), cy = 250 + 90 * sinf(t * 0.037f + 1.0f);
    for (int i = 0; i < NBARS; i++) {
        f32 bx = cx - NBARS * 3.0f + i * 6.0f;
        f32 bh = 4 + bar_val[i] * 90;
        col_t c = lerp_c(RGBA(156, 60, 255, 255), RGBA(70, 130, 255, 255), (f32)i / NBARS);
        gx_rect(bx, cy - bh, 4, bh, RGBA(c.r, c.g, c.b, 200));
        gx_rect(bx, cy + 2, 4, bh * 0.35f, RGBA(c.r, c.g, c.b, 50));
    }
    char ti[200], sub[200];
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    saver_line(F_MED, cx, cy + 44, ti, 400, RGBA(200, 185, 245, 200));
}

/* ---- Heure et date ---- */
static int saver_clock(char *hm, int hcap, char *date, int dcap) {
    static const char *const jours[7] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };
    static const char *const mois[12] = { "janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin", "juillet",
                                          "ao\xC3\xBBt", "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre" };
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm) { hm[0] = date[0] = 0; return 0; }
    snprintf(hm, hcap, "%02d:%02d", tm->tm_hour, tm->tm_min);
    snprintf(date, dcap, "%s %d %s %d", jours[tm->tm_wday % 7], tm->tm_mday, mois[tm->tm_mon % 12], tm->tm_year + 1900);
    return tm->tm_sec;
}

/* chiffre a 7 segments (les segments eteints restent a peine visibles) */
static void seg_digit(f32 x, f32 y, f32 w, f32 h, int d, col_t top, col_t bot) {
    static const u8 mask[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    const f32 t = w * 0.2f, hh = h * 0.5f, r = t * 0.45f;
    const f32 sx[7] = { x + t, x + w - t, x + w - t, x + t, x, x, x + t };
    const f32 sy[7] = { y, y + t, y + hh + t * 0.5f, y + h - t, y + hh + t * 0.5f, y + t, y + hh - t * 0.5f };
    const f32 sw[7] = { w - 2 * t, t, t, w - 2 * t, t, t, w - 2 * t };
    const f32 sh[7] = { t, hh - 1.5f * t, hh - 1.5f * t, t, hh - 1.5f * t, hh - 1.5f * t, t };
    int m = (d >= 0 && d <= 9) ? mask[d] : 0;
    for (int i = 0; i < 7; i++) {
        if (m & (1 << i)) gx_rrect_fill(sx[i] + 1, sy[i] + 1, sw[i] - 2, sh[i] - 2, r, top, bot);
        else gx_rrect_fill(sx[i] + 1, sy[i] + 1, sw[i] - 2, sh[i] - 2, r, RGBA(120, 100, 200, 16), RGBA(120, 100, 200, 16));
    }
}

static void saver_horloge(f32 t) {
    char hm[16], date[64], ti[200], sub[200];
    int sec = saver_clock(hm, sizeof(hm), date, sizeof(date));
    f32 cx = 320 + 110 * sinf(t * 0.043f), cy = 210 + 60 * sinf(t * 0.031f + 0.7f);
    uxsave_t uz = ux_native(cx);
    const f32 dw = 70, dh = 128, gap = 16, colon = 40;
    const f32 total = 4 * dw + 2 * gap + colon;
    f32 x = cx - total / 2, y = cy - dh / 2;
    col_t top = RGBA(200, 170, 255, 235), bot = RGBA(110, 70, 240, 235);
    if (hm[0]) {
        seg_digit(x, y, dw, dh, hm[0] - '0', top, bot);
        seg_digit(x + dw + gap, y, dw, dh, hm[1] - '0', top, bot);
        f32 kx = x + 2 * dw + gap + colon / 2 - 6;
        u8 a = (sec & 1) ? 90 : 230;                          /* les deux points clignotent */
        gx_rrect_fill(kx, y + dh * 0.30f, 12, 12, 4, RGBA(200, 170, 255, a), RGBA(110, 70, 240, a));
        gx_rrect_fill(kx, y + dh * 0.62f, 12, 12, 4, RGBA(200, 170, 255, a), RGBA(110, 70, 240, a));
        seg_digit(x + 2 * dw + gap + colon, y, dw, dh, hm[3] - '0', top, bot);
        seg_digit(x + 3 * dw + 2 * gap + colon, y, dw, dh, hm[4] - '0', top, bot);
    }
    saver_line(F_LARGE, cx, y + dh + 44, date, 520, RGBA(220, 210, 255, 220));
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    saver_line(F_MED, cx, y + dh + 80, ti, 520, RGBA(190, 170, 245, 190));
    saver_line(F_SMALL, cx, y + dh + 102, sub, 520, RGBA(150, 135, 210, 170));
    ux_restore(uz);
}

/* ---- Pochette en grand ---- */
static void saver_pochette(f32 t) {
    const f32 sz = 220, tw = 280;
    f32 cx = 320 + 40 * sinf(t * 0.047f), cy = 225 + 60 * sinf(t * 0.033f + 0.4f);
    uxsave_t uz = ux_native(cx);                         /* (16:9 : proportions reelles) */
    f32 bx = cx - (sz + 26 + tw) / 2, by = cy - sz / 2;
    if (saver_has_cover()) {
        gx_set_cover();
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(UX(bx), by);           GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(UX(bx + sz), by);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(UX(bx + sz), by + sz); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(UX(bx), by + sz);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f);
        GX_End();
        /* reflet sous la pochette */
        const f32 rh = 60;
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(UX(bx), by + sz + 4);           GX_Color4u8(255, 255, 255, 70); GX_TexCoord2f32(0.0f, 1.0f);
        GX_Position2f32(UX(bx + sz), by + sz + 4);      GX_Color4u8(255, 255, 255, 70); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(UX(bx + sz), by + sz + 4 + rh); GX_Color4u8(255, 255, 255, 0);  GX_TexCoord2f32(1.0f, 1.0f - rh / sz);
        GX_Position2f32(UX(bx), by + sz + 4 + rh);      GX_Color4u8(255, 255, 255, 0);  GX_TexCoord2f32(0.0f, 1.0f - rh / sz);
        GX_End();
        gx_rrect_stroke(bx, by, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 200));
    } else {
        draw_logo_cube(bx + sz / 2, by + sz / 2, 70);
    }
    char ti[200], sub[200], buf[200], hm[16], date[64];
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    saver_clock(hm, sizeof(hm), date, sizeof(date));
    f32 tx = bx + sz + 26, ty = by + 70;
    if (ti[0]) {
        fit_text(F_LARGE, ti, (int)tw, buf, sizeof(buf));
        gx_text1(F_LARGE, tx, ty, buf, RGBA(255, 255, 255, 230));
    } else gx_text1(F_LARGE, tx, ty, "GC Station", RGBA(255, 255, 255, 230));
    if (sub[0]) {
        fit_text(F_MED, sub, (int)tw, buf, sizeof(buf));
        gx_text1(F_MED, tx, ty + 30, buf, RGBA(200, 180, 250, 210));
    }
    if (hm[0]) {
        gx_text1(F_LARGE, tx, ty + 96, hm, RGBA(190, 160, 255, 220));
        fit_text(F_SMALL, date, (int)tw, buf, sizeof(buf));
        gx_text1(F_SMALL, tx, ty + 118, buf, RGBA(150, 135, 210, 180));
    }
    ux_restore(uz);
}

/* ---- Champ d'etoiles (accelere avec la musique) ---- */
#define NSTARS 150
static f32 st_x[NSTARS], st_y[NSTARS], st_z[NSTARS];
static int st_ready = 0;
static u64 st_last = 0;

static void star_reset(int i, int far) {
    st_x[i] = (f32)(rand() % 2001 - 1000) / 1000.0f;
    st_y[i] = (f32)(rand() % 2001 - 1000) / 1000.0f * 0.8f;
    st_z[i] = far ? 1.0f : 0.05f + (f32)(rand() % 950) / 1000.0f;
}

static void saver_etoiles(f32 t) {
    u64 now = now_ms();
    if (!st_ready) { for (int i = 0; i < NSTARS; i++) star_reset(i, 0); st_ready = 1; st_last = now; }
    f32 dt = (f32)(now - st_last) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    st_last = now;
    f32 e = 0;
    for (int i = 0; i < NBARS; i++) e += bar_val[i];
    e /= NBARS;
    f32 speed = 0.10f + e * 0.9f;
    f32 cx = 320 + 40 * sinf(t * 0.05f), cy = 240 + 30 * sinf(t * 0.04f);
    for (int i = 0; i < NSTARS; i++) {
        st_z[i] -= speed * dt;
        if (st_z[i] < 0.03f) { star_reset(i, 1); continue; }
        f32 sx = cx + st_x[i] / st_z[i] * 240, sy = cy + st_y[i] / st_z[i] * 240;
        if (sx < -10 || sx > 650 || sy < -10 || sy > 490) { star_reset(i, 1); continue; }
        f32 k = 1.0f - st_z[i];
        f32 sz = 0.8f + k * 3.2f;
        col_t c = lerp_c(RGBA(120, 80, 255, 255), RGBA(255, 255, 255, 255), k);
        /* petite trainee vers le centre */
        f32 pz = st_z[i] + 0.04f;
        f32 px = cx + st_x[i] / pz * 240, py = cy + st_y[i] / pz * 240;
        gx_rect(px, py, sz * 0.7f, sz * 0.7f, RGBA(c.r, c.g, c.b, (u8)(60 * k)));
        gx_rect(sx - sz / 2, sy - sz / 2, sz, sz, RGBA(c.r, c.g, c.b, (u8)(80 + 170 * k)));
    }
    char ti[200], sub[200];
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    saver_line(F_MED, 320 + 120 * sinf(t * 0.021f), 430 + 12 * sinf(t * 0.05f), ti, 460, RGBA(210, 200, 250, 190));
}

/* ---- Photos de la carte SD (sd:/ecran) ----
   Chaque photo est lue petit a petit (la musique continue), decodee par le
   thread des images, puis affichee 12 s avec un lent zoom et un fondu. */
#define PHOTO_DIR       SD_ROOT "/ecran"
#define PHOTO_MAX       400
#define PHOTO_MAX_FILE  (6 * 1024 * 1024)
#define PHOTO_SHOW_MS   12000
#define PHOTO_FADE_MS   1500
#define PHOTO_CHUNK     (32 * 1024)
typedef struct { u16 *tex; GXTexObj obj; int tw, th, iw, ih; } ptex_t;
enum { PH_IDLE, PH_READ, PH_QUEUED, PH_DEC };
static char **ph_names = NULL;
static int ph_n = -1, ph_next = 0, ph_fails = 0, ph_state = PH_IDLE;
static FILE *ph_fp = NULL;
static unsigned char *ph_buf = NULL;
static long ph_len = 0, ph_size = 0;
static ptex_t ph[2];
static int ph_cur = -1, ph_prev = -1, ph_ready = -1;
static u64 ph_shown = 0;

static void photo_free_names(void) {
    for (int i = 0; i < ph_n; i++) free(ph_names[i]);
    free(ph_names);
    ph_names = NULL;
    ph_n = -1;
}

static void photo_scan(void) {
    photo_free_names();
    ph_n = 0;
    ph_next = 0;
    ph_fails = 0;
    if (!sd_ok) return;
    DIR *d = opendir(PHOTO_DIR);
    if (!d) { mkdir(PHOTO_DIR, 0777); return; }            /* cree le dossier pour qu'on le trouve */
    ph_names = (char **)malloc(sizeof(char *) * PHOTO_MAX);
    if (!ph_names) { closedir(d); return; }
    struct dirent *e;
    while ((e = readdir(d)) && ph_n < PHOTO_MAX) {
        if (e->d_name[0] == '.') continue;
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || !(strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".png") == 0)) continue;
        char path[NAME_LEN];
        snprintf(path, sizeof(path), "%s/%s", PHOTO_DIR, e->d_name);
        char *cp = strdup(path);
        if (cp) ph_names[ph_n++] = cp;
        if ((ph_n & 15) == 0) service_audio();
    }
    closedir(d);
    for (int i = ph_n - 1; i > 0; i--) {                    /* ordre melange */
        int j = rand() % (i + 1);
        char *tmp = ph_names[i]; ph_names[i] = ph_names[j]; ph_names[j] = tmp;
    }
}

static void ptex_free(ptex_t *p) { free(p->tex); memset(p, 0, sizeof(*p)); }

static void photo_stop_read(void) {
    if (ph_fp) { fclose(ph_fp); ph_fp = NULL; }
    free(ph_buf);
    ph_buf = NULL;
    ph_len = ph_size = 0;
}

/* appele a chaque image, meme hors economiseur (pour liberer la memoire) */
static void photo_pump(void) {
    int active = saver_on && saver_style == SV_PHOTOS;

    if (ph_state == PH_DEC && dj_state == 2) {               /* une photo vient d'etre decodee */
        dj_state = 0;
        ph_state = PH_IDLE;
        if (active && dj_ok && dj_tex) {
            int slot = (ph_cur < 0) ? 0 : 1 - ph_cur;
            ptex_free(&ph[slot]);
            ptex_t *p = &ph[slot];
            p->tex = dj_tex; p->tw = dj_tw; p->th = dj_th; p->iw = dj_iw; p->ih = dj_ih;
            DCFlushRange(p->tex, (u32)p->tw * p->th * 2);
            GX_InitTexObj(&p->obj, p->tex, p->tw, p->th, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
            GX_InitTexObjFilterMode(&p->obj, GX_LINEAR, GX_LINEAR);
            GX_InvalidateTexAll();
            ph_fails = 0;
            if (ph_cur < 0) { ph_cur = slot; ph_prev = -1; ph_shown = now_ms(); }
            else ph_ready = slot;
        } else {
            free(dj_tex);
            if (active && !dj_ok) ph_fails++;
        }
        dj_tex = NULL;
    }

    if (!active) {
        if (ph_state == PH_READ || ph_state == PH_QUEUED) { photo_stop_read(); ph_state = PH_IDLE; }
        if (ph[0].tex || ph[1].tex) { ptex_free(&ph[0]); ptex_free(&ph[1]); }
        ph_cur = ph_prev = ph_ready = -1;
        if (ph_n >= 0) photo_free_names();                  /* relu a la prochaine activation */
        return;
    }

    if (ph_n < 0) photo_scan();
    if (ph_n == 0) return;
    u64 now = now_ms();

    /* photo suivante prete et temps ecoule : on passe a elle (fondu) */
    if (ph_ready >= 0 && now - ph_shown >= PHOTO_SHOW_MS) {
        ph_prev = ph_cur;
        ph_cur = ph_ready;
        ph_ready = -1;
        ph_shown = now;
    }

    if (ph_state == PH_IDLE) {
        if (ph_fails >= ph_n) return;                       /* aucune photo lisible */
        if (ph_cur >= 0 && (ph_n == 1 || ph_ready >= 0 || now - ph_shown < PHOTO_SHOW_MS - 4000)) return;
        if (ph_cur >= 0 && ph_prev >= 0 && now - ph_shown < PHOTO_FADE_MS) return;   /* fondu en cours */
        const char *path = ph_names[ph_next % ph_n];
        ph_next++;
        ph_fp = fopen(path, "rb");
        long size = -1;
        if (ph_fp && fseek(ph_fp, 0, SEEK_END) == 0) size = ftell(ph_fp);
        if (!ph_fp || size <= 0 || size > PHOTO_MAX_FILE || fseek(ph_fp, 0, SEEK_SET) != 0 ||
            !(ph_buf = (unsigned char *)malloc(size))) {
            photo_stop_read();
            ph_fails++;
            return;
        }
        ph_size = size;
        ph_len = 0;
        ph_state = PH_READ;
        return;
    }

    if (ph_state == PH_READ) {                               /* un morceau du fichier par image */
        long want = ph_size - ph_len;
        if (want > PHOTO_CHUNK) want = PHOTO_CHUNK;
        size_t got = fread(ph_buf + ph_len, 1, (size_t)want, ph_fp);
        if (got == 0) { photo_stop_read(); ph_state = PH_IDLE; ph_fails++; return; }
        ph_len += (long)got;
        if (ph_len >= ph_size) { fclose(ph_fp); ph_fp = NULL; ph_state = PH_QUEUED; }
        return;
    }

    if (ph_state == PH_QUEUED) {                             /* attend que le decodeur soit libre */
        if (dj_state != 0 || il_state == IL_DECODE) return;
        if (tab == TAB_PHOTO) return;                        /* (l'onglet PHOTOS s'en sert) */
        if (!dec_ensure()) { photo_stop_read(); ph_state = PH_IDLE; ph_fails = ph_n; return; }
        /* trop grande, ou pas assez de memoire libre pour la decoder : photo suivante */
        int iw, ih, ic;
        if (!stbi_info_from_memory(ph_buf, (int)ph_len, &iw, &ih, &ic) || iw <= 0 || ih <= 0 ||
            (long)iw * ih > PHOTO_MAX_PIXELS) {
            photo_stop_read(); ph_state = PH_IDLE; ph_fails++; return;
        }
        void *probe = malloc((size_t)iw * ih * 7 + 512 * 1024);
        if (!probe) { photo_stop_read(); ph_state = PH_IDLE; ph_fails++; return; }
        free(probe);
        dj_in = ph_buf;
        dj_len = (int)ph_len;
        ph_buf = NULL;
        ph_len = ph_size = 0;
        dj_aw = 640;
        dj_ah = 480;
        dj_maxw = 640;
        dj_photo = 1;
        dj_owner = 2;
        ph_state = PH_DEC;
        dj_state = 1;
    }
}

static void draw_photo(const ptex_t *p, u8 alpha, f32 age, int dir) {
    if (!p->tex || p->iw <= 0 || p->ih <= 0) return;
    f32 fw = 640.0f * ux_k;                             /* (16:9 : toute la largeur) */
    uxsave_t uz = ux_native(320);
    f32 s = fw / p->iw;
    if (p->ih * s > 480.0f) s = 480.0f / p->ih;
    f32 z = 1.0f + 0.07f * (age / (PHOTO_SHOW_MS / 1000.0f));    /* lent zoom */
    f32 w = p->iw * s * z, h = p->ih * s * z;
    f32 cx = 320 + dir * (z - 1.0f) * 160, cy = 240 + dir * (z - 1.0f) * 60;
    f32 x0 = cx - w / 2, y0 = cy - h / 2;
    f32 u1 = (f32)p->iw / p->tw, v1 = (f32)p->ih / p->th;
    gx_set_image((GXTexObj *)&p->obj);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(UX(x0), y0);         GX_Color4u8(255, 255, 255, alpha); GX_TexCoord2f32(0.0f, 0.0f);
    GX_Position2f32(UX(x0 + w), y0);     GX_Color4u8(255, 255, 255, alpha); GX_TexCoord2f32(u1, 0.0f);
    GX_Position2f32(UX(x0 + w), y0 + h); GX_Color4u8(255, 255, 255, alpha); GX_TexCoord2f32(u1, v1);
    GX_Position2f32(UX(x0), y0 + h);     GX_Color4u8(255, 255, 255, alpha); GX_TexCoord2f32(0.0f, v1);
    GX_End();
    ux_restore(uz);
}

static void saver_photos(f32 t) {
    u64 now = now_ms();
    if (ph_cur < 0) {
        const char *msg = (ph_n == 0) ? "Ajoute des photos .jpg / .png dans sd:/ecran" :
                          (ph_n > 0 && ph_fails >= ph_n) ? "Photos illisibles (1920 x 1080 maximum)" : "Chargement des photos...";
        if (ph_n == 0 || (ph_n > 0 && ph_fails >= ph_n)) saver_spectre(t);
        saver_line(F_MED, 320 + 140 * sinf(t * 0.03f), 120 + 40 * sinf(t * 0.045f), msg, 560, RGBA(255, 210, 120, 200));
        return;
    }
    f32 age = (f32)(now - ph_shown) / 1000.0f;
    int dir = (ph_next & 1) ? 1 : -1;
    if (ph_prev >= 0 && now - ph_shown < PHOTO_FADE_MS) {
        draw_photo(&ph[ph_prev], 255, age + PHOTO_SHOW_MS / 1000.0f, -dir);
        draw_photo(&ph[ph_cur], (u8)(255 * (now - ph_shown) / PHOTO_FADE_MS), age, dir);
    } else {
        draw_photo(&ph[ph_cur], 255, age, dir);
    }
    char ti[200], sub[200];
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    if (ti[0]) {
        f32 ly = 440 + 8 * sinf(t * 0.05f), lx = 320 + 60 * sinf(t * 0.02f);
        gx_rrect_fill(lx - 250, ly - 20, 500, 28, 10, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120));
        saver_line(F_MED, lx, ly, ti, 470, RGBA(255, 255, 255, 220));
    }
}

/* ---- Aquarium (v21) : poissons et bulles qui suivent la musique ---- */
#define AQ_FISH 9
#define AQ_BUB 40
static f32 aq_x[AQ_FISH], aq_y[AQ_FISH], aq_s[AQ_FISH], aq_v[AQ_FISH], aq_h[AQ_FISH], aq_ph[AQ_FISH];
static f32 bq_x[AQ_BUB], bq_y[AQ_BUB], bq_r[AQ_BUB], bq_v[AQ_BUB];
static int aq_ready = 0;
static u64 aq_last = 0;
static f32 aq_bavg = 0;
static col_t aq_col(f32 h, f32 l, u8 a) {                 /* couleur vive de poisson */
    static const u8 pal[6][3] = { { 255, 140, 40 }, { 255, 220, 60 }, { 90, 200, 255 }, { 255, 90, 140 }, { 120, 255, 160 }, { 200, 130, 255 } };
    int i = ((int)h) % 6;
    return RGBA((u8)(pal[i][0] * l), (u8)(pal[i][1] * l), (u8)(pal[i][2] * l), a);
}
static void aq_fish_reset(int i, int side) {
    aq_s[i] = 0.6f + (f32)(rand() % 100) / 100.0f * 0.9f;
    aq_v[i] = (rand() % 2 ? 1 : -1) * (30 + rand() % 40);
    aq_x[i] = side ? (aq_v[i] > 0 ? -60 : 700) : (f32)(rand() % 640);
    aq_y[i] = 80 + rand() % 300;
    aq_h[i] = (f32)(rand() % 6);
    aq_ph[i] = (f32)(rand() % 628) / 100.0f;
}
static void aq_bub_reset(int i, f32 x) {
    bq_x[i] = x >= 0 ? x : (f32)(rand() % 640);
    bq_y[i] = 470 + rand() % 40;
    bq_r[i] = 1.5f + (f32)(rand() % 40) / 10.0f;
    bq_v[i] = 30 + rand() % 50;
}
static void saver_aquarium(f32 t) {
    u64 now = now_ms();
    if (!aq_ready) { for (int i = 0; i < AQ_FISH; i++) aq_fish_reset(i, 0); for (int i = 0; i < AQ_BUB; i++) { aq_bub_reset(i, -1); bq_y[i] = (f32)(rand() % 480); } aq_ready = 1; aq_last = now; }
    f32 dt = (f32)(now - aq_last) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    aq_last = now;
    f32 e = 0, b = 0;
    for (int i = 0; i < NBARS; i++) e += bar_val[i];
    static u32 aq_kick = 0;
    b = bass_lvl;
    e /= NBARS;
    int beat = kick_count != aq_kick;
    aq_kick = kick_count;
    aq_bavg = aq_bavg * 0.95f + b * 0.05f;
    f32 speed = 0.5f + e * 2.2f;                                  /* la musique fait nager plus vite */
    /* eau, rayons de lumiere, sable */
    gx_quad4(0, 0, 640, 480, RGBA(10, 70, 120, 255), RGBA(10, 70, 120, 255), RGBA(2, 20, 50, 255), RGBA(2, 20, 50, 255));
    for (int k = 0; k < 5; k++) {
        f32 x = 80 + k * 130 + 30 * sinf(t * 0.2f + k);
        u8 a = (u8)(18 + 14 * sinf(t * 0.3f + k * 1.7f) + e * 30);
        gx_poly4(x, 0, x + 40, 0, x + 120, 480, x + 40, 480, RGBA(180, 230, 255, a), RGBA(180, 230, 255, a), RGBA(180, 230, 255, 0), RGBA(180, 230, 255, 0));
    }
    gx_quad4(0, 440, 640, 480, RGBA(150, 130, 80, 255), RGBA(150, 130, 80, 255), RGBA(90, 75, 45, 255), RGBA(90, 75, 45, 255));
    for (int k = 0; k < 9; k++) {                                     /* algues : elles dansent sur les basses */
        f32 x0 = 30 + k * 72 + (k % 3) * 9, sway = 10 * sinf(t * 0.8f + k) + bar_val[k * 5 % NBARS] * 14;
        f32 h = 80 + (k % 4) * 25;
        for (int s = 0; s < 6; s++) {
            f32 y0 = 445 - s * h / 6, y1 = 445 - (s + 1) * h / 6;
            f32 a0 = sway * s / 6.0f, a1 = sway * (s + 1) / 6.0f;
            gx_poly4(x0 + a0 - 5, y0, x0 + a0 + 5, y0, x0 + a1 + 3, y1, x0 + a1 - 3, y1,
                     RGBA(40, 150, 70, 230), RGBA(40, 150, 70, 230), RGBA(80, 200, 100, 230), RGBA(80, 200, 100, 230));
        }
    }
    /* bulles : plus nombreuses sur les coups de basse */
    for (int i = 0; i < AQ_BUB; i++) {
        bq_y[i] -= bq_v[i] * speed * dt;
        bq_x[i] += 6 * sinf(t * 2 + i) * dt;
        if (bq_y[i] < -10) { if (beat || rand() % 3 == 0) aq_bub_reset(i, -1); else bq_y[i] = 600; }
        if (bq_y[i] < 480) gx_ring(bq_x[i], bq_y[i], bq_r[i], bq_r[i] + 1.2f, RGBA(220, 245, 255, 170));
    }
    if (beat) for (int n = 0, i = 0; i < AQ_BUB && n < 6; i++) if (bq_y[i] > 480) { aq_bub_reset(i, (f32)(100 + rand() % 440)); n++; }
    /* poissons */
    for (int i = 0; i < AQ_FISH; i++) {
        aq_x[i] += aq_v[i] * speed * dt;
        aq_ph[i] += dt * (3 + speed * 3);
        if (aq_x[i] < -80 || aq_x[i] > 720) aq_fish_reset(i, 1);
        f32 y = aq_y[i] + 8 * sinf(aq_ph[i] * 0.5f);
        f32 d = aq_v[i] > 0 ? 1.0f : -1.0f, s = aq_s[i];
        f32 L = 26 * s, H = 13 * s, wag = 5 * s * sinf(aq_ph[i] * 2);
        col_t c = aq_col(aq_h[i], 1.0f, 255), c2 = aq_col(aq_h[i], 0.6f, 255);
        gx_tri(aq_x[i] - d * L * 0.8f, y, aq_x[i] - d * L * 1.4f, y - H * 0.8f + wag, aq_x[i] - d * L * 1.4f, y + H * 0.8f + wag, c2);   /* queue */
        gx_ellipse(aq_x[i], y, L, H, c, c2);
        gx_tri(aq_x[i] - d * L * 0.1f, y - H * 0.8f, aq_x[i] + d * L * 0.3f, y - H * 0.9f, aq_x[i] - d * L * 0.4f, y - H * 1.5f, c2);   /* nageoire */
        gx_circle(aq_x[i] + d * L * 0.55f, y - H * 0.2f, 2.6f * s, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
        gx_circle(aq_x[i] + d * L * 0.6f, y - H * 0.2f, 1.3f * s, RGBA(10, 10, 20, 255), RGBA(10, 10, 20, 255));
    }
    char ti[200], sub[200];
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    if (ti[0]) saver_line(F_MED, 320 + 80 * sinf(t * 0.03f), 465, ti, 520, RGBA(255, 255, 255, 210));
}

/* ---- Spectrogramme (v22) : les frequences defilent de droite a gauche ----
   (idee d'un testeur sur Reddit) en bas les graves, en haut les aigus, la
   couleur donne la force : noir, violet, rouge, orange, jaune. Une texture de
   512 x 64 en anneau : une colonne nouvelle a chaque image. */
#define SG_W 512
#define SG_H 64
static u16 sg_tex[SG_W * SG_H] ATTRIBUTE_ALIGN(32);
static GXTexObj sg_obj;
static int sg_ready = 0, sg_head = 0;
static u16 sg_pal[256];
static u32 sg_frame = 0;
static u8 sg_raw[SG_W * SG_H];                 /* v40 : forces gardees : on recolore tout en changeant de couleurs */
static int sg_pal_done = -1;
static void sg_palette(void) {
    for (int i = 0; i < 256; i++) {
        int r, g, b;
        sgp_rgb(i / 255.0f, &r, &g, &b);
        sg_pal[i] = (u16)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    }
    sg_pal_done = sg_pal_id;
}
static void sg_recolor(void) {
    sg_palette();
    for (int i = 0; i < SG_W * SG_H; i++) sg_tex[i] = sg_pal[sg_raw[i]];
    DCFlushRange(sg_tex, sizeof(sg_tex));
    GX_InvalidateTexAll();
}
static void sg_setup(void) {
    sg_palette();
    for (int i = 0; i < SG_W * SG_H; i++) sg_tex[i] = sg_pal[0];
    DCFlushRange(sg_tex, sizeof(sg_tex));
    GX_InitTexObj(&sg_obj, sg_tex, SG_W, SG_H, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&sg_obj, GX_LINEAR, GX_LINEAR);
    sg_ready = 1;
}
/* une colonne de plus (une fois par image) */
static void sg_update(void) {
    if (!sg_ready) sg_setup();
    if (sg_pal_done != sg_pal_id) sg_recolor();
    if (sg_frame == retrace_count) return;
    sg_frame = retrace_count;
    int x = sg_head;
    for (int y = 0; y < SG_H; y++) {
        f32 f = (f32)(SG_H - 1 - y) / (SG_H - 1) * (NBARS - 1);
        int i0 = (int)f; if (i0 > NBARS - 2) i0 = NBARS - 2;
        f32 t = f - i0, v = bar_val[i0] * (1 - t) + bar_val[i0 + 1] * t;
        int c = (int)(v * v * 255);                         /* (le bruit de fond reste sombre ; v29 : plus de saturation) */
        if (c > 255) c = 255;
        int o = ((y >> 2) * (SG_W >> 2) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3);
        sg_raw[o] = (u8)c;
        sg_tex[o] = sg_pal[c];
    }
    for (int ty = 0; ty < SG_H / 4; ty++) DCFlushRange(&sg_tex[(ty * (SG_W >> 2) + (x >> 2)) * 16], 32);
    GX_InvalidateTexAll();
    sg_head = (sg_head + 1) % SG_W;
}
static void sg_quad(f32 x0, f32 x1, f32 y, f32 h, f32 u0, f32 u1, u8 a) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(UX(x0), y);     GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(u0, 0);
    GX_Position2f32(UX(x1), y);     GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(u1, 0);
    GX_Position2f32(UX(x1), y + h); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(u1, 1);
    GX_Position2f32(UX(x0), y + h); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(u0, 1);
    GX_End();
}
/* dessine le spectrogramme : le plus recent a droite */
static void sg_draw(f32 x, f32 y, f32 w, f32 h, u8 a) {
    if (!sg_ready) sg_setup();
    if (sg_pal_done != sg_pal_id) sg_recolor();
    gx_set_image(&sg_obj);
    f32 hu = (f32)sg_head / SG_W, split = x + w * (1.0f - hu);
    sg_quad(x, split, y, h, hu, 1.0f, a);                   /* des plus anciennes... */
    if (sg_head > 0) sg_quad(split, x + w, y, h, 0.0f, hu, a);   /* ...aux plus recentes */
}
static void sg_labels(f32 x, f32 y, f32 h, col_t c) {        /* reperes de frequence */
    static const struct { int hz; const char *t; } m[] = { { 100, "100 Hz" }, { 1000, "1 kHz" }, { 5000, "5 kHz" }, { 10000, "10 kHz" } };
    for (int i = 0; i < 4; i++) {
        f32 pos = (mel_of((float)m[i].hz) - mel_of(40.0f)) / (mel_of(16000.0f) - mel_of(40.0f));   /* (v29 : echelle mel, 40 Hz a 16 kHz) */
        f32 yy = y + h * (1.0f - pos);
        gx_rect(x, yy, 10, 1, c);
        gx_text1(F_SMALL, x + 14, yy + 4, m[i].t, c);
    }
}
static void saver_spectro(f32 t) {
    sg_update();
    sg_draw(0, 40, 640, 380, 255);
    sg_labels(8, 40, 380, RGBA(255, 255, 255, 150));
    char ti[200], sub[200];
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    if (ti[0]) saver_line(F_MED, 320 + 60 * sinf(t * 0.03f), 454, ti, 560, RGBA(255, 255, 255, 220));
}

static void draw_saver(void) {
    scr_viewport();
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    gx_quad4(0, 0, 640, 480, RGBA(2, 1, 8, 255), RGBA(2, 1, 8, 255), RGBA(4, 2, 14, 255), RGBA(4, 2, 14, 255));
    /* tout se deplace lentement : pas d'image fixe sur l'ecran */
    f32 t = (f32)(now_ms() % 600000) / 1000.0f;
    switch (saver_style) {
    case SV_POCHETTE: saver_pochette(t); break;
    case SV_HORLOGE:  saver_horloge(t); break;
    case SV_ETOILES:  saver_etoiles(t); break;
    case SV_PHOTOS:   saver_photos(t); break;
    case SV_NOIR:     break;
    case SV_AQUARIUM: saver_aquarium(t); break;
    case SV_SPECTRO:  saver_spectro(t); break;
    case SV_TABLEAU:  saver_tableau(t); break;
    default:          saver_spectre(t); break;
    }
    service_audio();
    present_frame();
}

/* ---------- Mode soiree : visualiseur plein ecran ----------
   Les basses donnent le rythme (pulsations, eclairs, anneaux), la couleur
   tourne avec la musique, et les paroles (ou le titre) s'affichent en grand.
   Stick C haut : entrer ; stick C gauche / droite : changer de style ;
   B ou START : sortir. L / R et Y marchent comme d'habitude. */
static int party_on = 0;
static u64 party_t0 = 0, pt_last = 0, pt_last_beat = 0, pt_hint_until = 0;
static float pt_bass = 0, pt_avg = 0, pt_beat = 0, pt_hue = 260, pt_energy = 0, pt_high = 0;
static float pt_peak[NBARS];
#define PT_RINGS 10
static float ring_r[PT_RINGS], ring_a[PT_RINGS], ring_h[PT_RINGS];
#define PT_PARTS 72
static float pp_x[PT_PARTS], pp_y[PT_PARTS], pp_vx[PT_PARTS], pp_vy[PT_PARTS], pp_life[PT_PARTS], pp_h[PT_PARTS];

static col_t hsv(float h, float s, float v, u8 a) {
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
    float r, g, b;
    if (h < 60) { r = c; g = x; b = 0; } else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; } else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; } else { r = c; g = 0; b = x; }
    return RGBA((u8)((r + m) * 255), (u8)((g + m) * 255), (u8)((b + m) * 255), a);
}

static void party_start(void) {
    party_on = 1;
    party_t0 = pt_last = now_ms();
    pt_hint_until = party_t0 + 5000;
    memset(ring_a, 0, sizeof(ring_a));
    memset(pp_life, 0, sizeof(pp_life));
    memset(pt_peak, 0, sizeof(pt_peak));
}

static void party_beat(float strength) {
    pt_beat = 1.0f;
    party_rumble(strength);
    pt_hue += 35.0f + (float)(rand() % 50);
    for (int i = 0; i < PT_RINGS; i++)                              /* un anneau qui s'eloigne */
        if (ring_a[i] <= 0.01f) { ring_r[i] = 40.0f; ring_a[i] = 0.9f; ring_h[i] = pt_hue; break; }
    int n = 10 + (int)(strength * 14);                               /* une gerbe de particules */
    for (int i = 0; i < PT_PARTS && n > 0; i++) {
        if (pp_life[i] > 0) continue;
        float a = (float)(rand() % 6283) / 1000.0f, sp = 120.0f + (float)(rand() % 260);
        pp_x[i] = 320; pp_y[i] = 192;
        pp_vx[i] = cosf(a) * sp; pp_vy[i] = sinf(a) * sp;
        pp_life[i] = 1.0f;
        pp_h[i] = pt_hue + (float)(rand() % 60);
        n--;
    }
}

static void party_update(void) {
    u64 now = now_ms();
    float dt = (float)(now - pt_last) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    pt_last = now;
    float b = 0, m = 0, h = 0;
    for (int i = 11; i < 36; i++) m += bar_val[i];
    for (int i = 36; i < NBARS; i++) h += bar_val[i];
    b = bass_lvl; m /= 25.0f; h /= (float)(NBARS - 36);
    pt_bass = b;
    pt_energy = pt_energy * 0.85f + (b + m + h) / 3.0f * 0.15f;
    pt_high = pt_high * 0.9f + h * 0.1f;
    /* battement : les basses passent nettement au-dessus de leur moyenne */
    /* v29 : un coup de grosse caisse (detecte dans update_spectrum) = un battement */
    static u32 kick_seen = 0;
    if (kick_count != kick_seen) {
        kick_seen = kick_count;
        if (now - pt_last_beat > 200) { party_beat(0.3f + 0.7f * b); pt_last_beat = now; }
    }
    pt_avg = pt_avg * 0.96f + b * 0.04f;
    pt_beat *= expf(-dt * 5.0f);
    pt_hue += dt * (8.0f + pt_high * 70.0f);                          /* les aigus font tourner la couleur */
    if (pt_hue > 3600.0f) pt_hue -= 3600.0f;
    for (int i = 0; i < PT_RINGS; i++) {
        if (ring_a[i] <= 0) continue;
        ring_r[i] += dt * (180.0f + pt_energy * 420.0f);
        ring_a[i] -= dt * 0.45f;
    }
    for (int i = 0; i < PT_PARTS; i++) {
        if (pp_life[i] <= 0) continue;
        pp_x[i] += pp_vx[i] * dt; pp_y[i] += pp_vy[i] * dt;
        pp_vy[i] += 40.0f * dt;
        pp_life[i] -= dt * 0.8f;
    }
    for (int i = 0; i < NBARS; i++) {
        pt_peak[i] -= dt * 0.6f;
        if (bar_val[i] > pt_peak[i]) pt_peak[i] = bar_val[i];
    }
    /* musique calme : un anneau de temps en temps quand meme */
    if (now - pt_last_beat > 1500 && pt_energy > 0.05f) { pt_last_beat = now; for (int i = 0; i < PT_RINGS; i++) if (ring_a[i] <= 0.01f) { ring_r[i] = 40; ring_a[i] = 0.5f; ring_h[i] = pt_hue; break; } }
}

static void party_particles(void) {
    for (int i = 0; i < PT_PARTS; i++) {
        if (pp_life[i] <= 0) continue;
        float s = 2.0f + pp_life[i] * 4.0f;
        gx_rect(pp_x[i] - s / 2, pp_y[i] - s / 2, s, s, hsv(pp_h[i], 0.6f, 1.0f, (u8)(pp_life[i] * 230)));
    }
}

static void party_pulsation(void) {
    const f32 cx = 320, cy = 192;
    f32 r0 = 62.0f + pt_bass * 46.0f + pt_beat * 22.0f;
    gx_circle_glow(cx, cy, r0 + 6, hsv(pt_hue, 0.9f, 1.0f, (u8)(90 + pt_beat * 120)), 6);
    gx_circle(cx, cy, r0, hsv(pt_hue + 30, 0.7f, 0.35f + pt_beat * 0.5f, 255), hsv(pt_hue, 0.9f, 0.12f, 255));
    for (int side = 0; side < 2; side++)
        for (int i = 0; i < NBARS; i++) {
            float a = -PI_F / 2 + (side ? -1.0f : 1.0f) * ((float)i + 0.5f) / NBARS * PI_F;
            float len = 6.0f + bar_val[i] * 120.0f * (1.0f + pt_beat * 0.25f);
            float ca = cosf(a), sa = sinf(a), w = 0.022f;
            float ca2 = cosf(a + w), sa2 = sinf(a + w), ca1 = cosf(a - w), sa1 = sinf(a - w);
            f32 rin = r0 + 8, rout = r0 + 8 + len;
            col_t c0 = hsv(pt_hue + i * 3.0f, 0.85f, 0.9f, 230), c1 = hsv(pt_hue + i * 3.0f + 40, 0.6f, 1.0f, 120);
            gx_poly4(cx + ca1 * rin, cy + sa1 * rin, cx + ca2 * rin, cy + sa2 * rin,
                     cx + ca2 * rout, cy + sa2 * rout, cx + ca1 * rout, cy + sa1 * rout, c0, c0, c1, c1);
            (void)ca; (void)sa;
        }
    party_particles();
}

static void party_barres(void) {
    const f32 base = 262, bw = 600.0f / NBARS;
    for (int i = 0; i < NBARS; i++) {
        f32 x = 20 + i * bw;
        f32 hgt = 8 + bar_val[i] * 175.0f * (1.0f + pt_beat * 0.25f);
        col_t top = hsv(pt_hue + i * 4.0f, 0.75f, 1.0f, 255), bot = hsv(pt_hue + i * 4.0f + 50, 0.95f, 0.45f, 255);
        gx_vgrad(x, base - hgt, bw - 2, hgt, top, bot);
        gx_vgrad(x, base + 3, bw - 2, hgt * 0.45f, RGBA(bot.r, bot.g, bot.b, 110), RGBA(bot.r, bot.g, bot.b, 0));
        f32 py = base - 8 - pt_peak[i] * 175.0f * (1.0f + pt_beat * 0.25f);
        gx_rect(x, py - 3, bw - 2, 3, RGBA(255, 255, 255, 220));
    }
    party_particles();
}

static void party_tunnel(void) {
    const f32 cx = 320, cy = 192;
    float rot = (float)(now_ms() - party_t0) / 1000.0f * (0.3f + pt_energy);
    for (int i = 0; i < PT_RINGS; i++) {
        if (ring_a[i] <= 0) continue;
        f32 r = ring_r[i], w = 3.0f + r * 0.03f;
        gx_ring_n(cx, cy, r, r + w, 6, rot + r * 0.004f, hsv(ring_h[i], 0.8f, 1.0f, (u8)(ring_a[i] * 255)));
    }
    f32 r0 = 30.0f + pt_bass * 40.0f + pt_beat * 25.0f;
    gx_ring_n(cx, cy, r0, r0 + 5, 6, -rot, hsv(pt_hue, 0.5f, 1.0f, 255));
    gx_circle_glow(cx, cy, r0 * 0.6f, hsv(pt_hue + 180, 0.8f, 1.0f, (u8)(80 + pt_beat * 150)), 5);
    party_particles();
}

/* texte en grand, avec une ombre */
/* y = ligne de base d'une seule ligne ; sur deux lignes, le bloc remonte */
static void party_text(f32 cx, f32 y, const char *s, float scale, col_t c, int maxw) {
    char a[300], b[300];
    txt_scale = scale;
    wrap2(F_LARGE, s, maxw, a, b, sizeof(a));
    f32 lh = font_height[F_LARGE] * scale;
    f32 y0 = b[0] ? y - lh : y;
    gx_text_c(F_LARGE, cx + 2, y0 + 2, a, RGBA(0, 0, 0, 160));
    gx_text_c(F_LARGE, cx, y0, a, c);
    if (b[0]) {
        gx_text_c(F_LARGE, cx + 2, y0 + lh + 2, b, RGBA(0, 0, 0, 160));
        gx_text_c(F_LARGE, cx, y0 + lh, b, c);
    }
    txt_scale = 1.0f;
}

static void draw_party(void) {
    int saved = th_identity;
    th_identity = 1;                                     /* les couleurs du mode soiree, pas celles du theme */
    party_update();
    scr_viewport();
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    f32 bt = (flash_mode == 2 || (flash_mode == 1 && party_style == 3)) ? 0.0f : pt_beat;
    col_t top = hsv(pt_hue, 0.9f, 0.10f + bt * 0.18f, 255), bot = hsv(pt_hue + 60, 0.9f, 0.04f, 255);
    gx_quad4(0, 0, 640, 480, top, top, bot, bot);
    if (party_style == 1) party_barres();
    else if (party_style == 2) party_tunnel();
    else if (party_style == 3) { sg_update(); sg_draw(0, 0, 640, 330, 255); gx_quad4(0, 250, 640, 330, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120)); }
    else party_pulsation();
    int flash = flash_mode == 0 || (flash_mode == 1 && party_style != 3);   /* v43 : eclair reglable */
    if (flash && pt_beat > 0.05f) gx_rect(0, 0, 640, 480, RGBA(255, 255, 255, (u8)(pt_beat * 28)));   /* eclair */
    gx_quad4(0, 290, 640, 480, RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 0), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));   /* lisibilite du texte */

    /* paroles en grand, sinon titre et artiste */
    char line[300], d1[300];
    int li = (src == SRC_FILE && playing) ? lyrics_current() : -2;
    col_t lc = hsv(pt_hue + 180, 0.25f, 1.0f, 255);
    if (src == SRC_FILE && playing && lrc_n > 0 && lrc_track == cur_track && lyr_show) {
        char a[300], b[300];
        text_display(li >= 0 ? lyrics_line(li) : "...", d1, sizeof(d1));
        txt_scale = 1.45f;
        wrap2(F_LARGE, d1, 590, a, b, sizeof(a));               /* une ou deux lignes ? */
        txt_scale = 1.0f;
        int two = b[0] != 0;
        if (li > 0) {
            char p1[300];
            text_display(lyrics_line(li - 1), p1, sizeof(p1));
            fit_text(F_MED, p1, 560, line, sizeof(line));
            gx_text_c(F_MED, 320, two ? 318 : 352, line, RGBA(255, 255, 255, 110));
        }
        party_text(320, 408, d1, 1.45f, lc, 590);
        if (li + 1 < lrc_n) { text_display(lyrics_line(li + 1), d1, sizeof(d1)); fit_text(F_MED, d1, 560, line, sizeof(line)); gx_text_c(F_MED, 320, 450, line, RGBA(255, 255, 255, 110)); }
    } else {
        char ti[200], sub[200];
        saver_title(ti, sizeof(ti), sub, sizeof(sub));
        if (ti[0]) party_text(320, 408, ti, 1.35f, lc, 590);
        else party_text(320, 408, "Lance une musique ou une radio", 1.0f, lc, 560);
        if (sub[0]) { fit_text(F_MED, sub, 560, line, sizeof(line)); gx_text_c(F_MED, 320, 440, line, RGBA(255, 255, 255, 170)); }
    }
    if (now_ms() < pt_hint_until) {
        snprintf(line, sizeof(line), "Mode soir\xC3\xA9" "e : %s   -   Stick C : changer   B : sortir", party_names[party_style]);
        gx_text_c(F_SMALL, 320, 24, line, RGBA(255, 255, 255, 200));
    }
    th_identity = saved;
    service_audio();
    present_frame();
}

// ---------- Onglets supplementaires (fichiers a part) ----------
#include "lib_extra.c"                         /* listes, recherche, ecoutes */
#include "tab_retro.c"
#include "tab_podcast.c"
#include "tab_photo.c"
#include "netfs.c"                             /* v86 : fichiers sur le reseau (SMB, FTP, FSP) */
static int sd_space(unsigned long long *tot, unsigned long long *fre);   /* v97 (natif.c) */
#include "tab_files.c"
#include "tab_infos.c"
#include "tab_phone.c"
#include "crashlog.c"
static const char *perf_ver(void) { return GCMP3_VERSION; }
#include "update.c"

static void tab_enter(int t) {
    if (t >= 0 && t < TAB_COUNT) crumb("onglet %s", tab_names[t]);
    switch (t) {
    case TAB_RADIO:   net_start_async(); break;
    case TAB_WEB:     web_enter_tab(); break;
    case TAB_RETRO:   retro_enter(); break;
    case TAB_PODCAST: net_start_async(); podcast_enter(); break;
    case TAB_PHOTO:   photo_enter(); break;
    case TAB_FILES:   files_enter(); break;
    case TAB_INFO:    net_start_async(); infos_enter(); break;
    case TAB_PHONE:   net_start_async(); phone_enter(); break;
    case TAB_DL:      dlh_loaded = 0; break;
    }
}

static int kb_is_open(void) { return kb_open; }

/* Relit la liste des MP3 (apres un telechargement ou X), sans couper le morceau en cours. */
/* v27 : un seul fichier en plus (telechargement fini) : ajoute a la fin de la
   liste, sans relire toute la carte SD (avant : l'ecran se figeait plusieurs secondes) */
static void retro_rescan(void);
static void music_add_file(const char *path) {
    if (!sd_ok || !audio_ext_len(path)) return;
    if (bib_find(path) >= 0) return;
    if (nfiles >= MAX_FILES) return;
    int k = nfiles;
    snprintf(names[k], NAME_LEN, "%s", path);
    dur_cache[k] = -2;
    is_retro[k] = retro_ext_fmt(path) >= 0;
    tg_done[k] = 0; tg_artist[k][0] = tg_album[k][0] = tg_title[k][0] = 0;
    play_cnt[k] = 0; play_last[k] = 0;
    nfiles++;
    lib_rebuild();
    retro_rescan();
}
static void music_rescan(void) {
    if (!sd_ok) return;
    char playing_path[NAME_LEN] = "";
    if (src == SRC_FILE && cur_track >= 0) snprintf(playing_path, sizeof(playing_path), "%s", names[cur_track]);
    if (bib_dirty) bib_save();                             /* garde ce qui est deja lu */
    if (stats_dirty) stats_save();
    if (rating_dirty) rating_save();
    scan_files();
    tags_reset();
    bib_load();
    pl_load_all();
    stats_load();
    rating_load();
    cur_track = -1;
    for (int i = 0; i < nfiles && playing_path[0]; i++)
        if (strcmp(names[i], playing_path) == 0) { cur_track = i; break; }
    if (src == SRC_FILE && cur_track < 0) stop_track();     /* fichier supprime entre-temps */
    if (cur_track >= 0) lrc_track = cur_track;
    cur_group = -1;
    lib_rebuild();
    qn = 0;                                                  /* la file sera refaite au besoin */
    if (cur_track >= 0) { queue_build(cur_track); select_track_row(cur_track); }
}

/* Pendant les attentes reseau (connexion, chiffrement...) : la musique et la
   radio continuent d'etre decodees. */
/* son decode d'avance : 4 s, ou la duree du fondu enchaine + 1 s */
#define FILE_LEAD (xf_sec > 0 ? (xf_sec + 1) * 48000 : 48000 * 4)
/* v80 : combien de trames decoder maintenant ? 2 par tour quand l'avance est confortable ;
   sous 2 s d'avance (images lentes : photos, pages web, carte SD...), on rattrape : autant
   que possible en 8 ms. Avant, avec 2 trames par image seulement, une interface a 20 images/s
   ne suivait plus un MP3 ou un FLAC et le son finissait par couper (mesure : simulateur). */
static int decode_more(int k, u32 t0) {
    if (k < 2) return 1;
    return f_count < 48000 * 2 && k < 64 && perf_us() - t0 < 8000;
}
/* v85 : un bloc decode pendant l'attente de l'ecran (rend 1 si on a decode) */
static int in_bg;
static int music_idle_decode(void) {
    if (video_playing || src != SRC_FILE || !playing || paused || eof || in_bg) return 0;
    if (f_count >= FILE_LEAD || (FIFO_SIZE - f_count) < FIFO_MARGIN) return 0;
    static u32 cost = 2000;                              /* duree d'un bloc (moyenne glissante, us) */
    u32 t = perf_us();
    if (t - frame_t0_us + cost > 15500) return 0;       /* (pas le temps avant l'image suivante : 16,7 ms) */
    in_bg = 1;
    decode_file_one();
    service_audio();
    in_bg = 0;
    u32 d = perf_us() - t;
    cost = d > cost ? d : (cost * 7 + d) / 8;            /* (monte vite, redescend doucement) */
    return 1;
}
static int in_bg = 0;   /* (declare plus haut : decodage pendant l'attente de l'ecran) */
static void bg_tick(void) {
    main_beat++;
    service_audio();
    if (in_bg) return;
    in_bg = 1;
    if (src == SRC_FILE && playing && !paused) {
        int k = 0;
        u32 t0 = perf_us();
        while (!eof && decode_more(k, t0) && f_count < FILE_LEAD && (FIFO_SIZE - f_count) >= FIFO_MARGIN) {
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

/* L / R : morceau ou station suivante / precedente (aussi depuis le telephone) */
static void transport_step(int d) {
    if (pod_on && src == SRC_RADIO) { podcast_skip(d > 0 ? 30 : -15); return; }
    if (src == SRC_FILE && retro_subsong_step(d)) return;          /* piste suivante d'un NSF / GBS */
    if (src == SRC_FILE && nfiles > 0) track_next(d, 0);
    else if (src == SRC_RADIO && nstations > 0) {
        int base = (cur_station >= 0 && cur_station < nstations) ? cur_station : (d > 0 ? -1 : 0);
        int nx = (base + d + nstations) % nstations;
        if (!rb_mode) sel_station = nx + RS_N;
        start_station(nx, 0);
    }
}

/* musique : decodage d'avance, puis morceau suivant (avec fondu enchaine) */
/* v72 : le morceau suivant (fin automatique) est-il du meme album (meme dossier) ? */
static int gapless_next_ok(void) {
    if (!auto_next_exists() || cur_track < 0) return 0;
    int nx = -1;
    if (repeat_mode == REP_ONE) nx = cur_track;
    else if (qpos + 1 < qn) nx = queue[qpos + 1];
    else if (repeat_mode == REP_ALL && !shuffle_on && qn > 0) nx = queue[0];
    if (nx < 0 || nx >= nfiles || is_retro[nx]) return 0;
    const char *a = names[cur_track], *b = names[nx], *sa = strrchr(a, '/'), *sb = strrchr(b, '/');
    if (!sa || !sb || sa - a != sb - b || strncmp(a, b, sa - a)) return 0;
    if (id3_album[0] && tg_album[nx][0] && strcasecmp(id3_album, tg_album[nx])) return 0;
    return 1;
}
static void music_pump(void) {
    if (video_playing) return;                          /* (le son vient de la video) */
    if (src != SRC_FILE || !playing || paused) return;
    int k = 0;
    u32 t0 = perf_us();
    /* v85 : le gros du decodage se fait pendant l'attente de l'ecran (music_idle_decode) ; ici,
       seulement si le son risque de manquer (moins de 0,5 s d'avance : debut d'un morceau...) */
    while (!eof && f_count < 24000 && decode_more(k, t0) && (FIFO_SIZE - f_count) >= FIFO_MARGIN) {
        decode_file_one();
        service_audio();
        k++;
    }
    if (eof && rgm_on) rgm_finish(rgm_track == cur_track);   /* v72 : sonie mesuree sur tout le morceau */
    if (eof && f_count > 0 && xf_sec == 0 && gapless_on && xf_left == 0 && gapless_next_ok()) {
        xf_request = -1;                                /* v72 : meme album : le suivant enchaine sans blanc */
        track_next(1, 1);
    }
    else if (eof && f_count == 0) track_next(1, 1);    /* morceau suivant (selon aleatoire / repetition) */
    else if (eof && xf_sec > 0 && xf_left == 0 && auto_next_exists()) {
        xf_request = xf_sec * 48000;                    /* fondu enchaine avec le suivant */
        track_next(1, 1);
    }
}

// ---------- Programme ----------
#include "night.c"
#include "gckb.c"
#include "fun_extra.c"                         /* v18 : karaoke, jeux, reveil, titres radio... */
#include "tabs_extra.c"                        /* v23 : onglets JEUX et TELECHARGEMENTS, curseur partout */
#include "cheats.c"                            /* v25 : editeur de cheats (SD:/gc-mp3/cheats) */
#include "gclib.c"                             /* v25 : Mes jeux GameCube (jaquettes : SD:/gc-mp3/jaquettes) */
#include "tools.c"                             /* v35 : cartes memoire, applis, recents, infos */
#include "jeux_cfg.c"                          /* v75 : reglages des jeux */
#include "igr.c"                               /* v39 : A + Z + START en jeu -> GC-MP3 */
#include "lanceur.c"                           /* v44 : lanceur de secours ; v90 : reprise de SD:/swiss */
#include "natif.c"                             /* v50 : lanceur natif GC Station, etape 1 : analyse */
#include "extras73.c"                          /* v73 : podcasts plus rapides, historique radio, cartes virtuelles... */

/* v76 : petit controle au demarrage (memoire libre, carte SD, duree), note dans le
   journal (Outils > Infos console > journal) ; un message bref si un souci. */
static u64 boot_t0 = 0;
static char boot_check[160] = "";
/* v92 : GC Station lance depuis un disque (mini-DVD grave) ou une vieille copie : s'il y a sur la carte SD
   une version plus recente (numero ecrit par GitHub a la fin du .dol), on la lance a la place. B maintenu au
   demarrage : on reste sur cette version. */
static const char *gl_boot_dol(const char *dolpath, const char *a1, const char *a2, const char *what);
static void disc_chain(void) {
    if (!sd_ok || sd_virtual) return;
    PAD_ScanPads();
    if (PAD_ButtonsHeld(0) & PAD_BUTTON_B) { crumb("demarrage : B maintenu, pas de version de la carte SD"); return; }
    up_pick_target();
    int b = up_dol_build(up_target);
    if (b <= GCMP3_BUILD) return;
    if (up_argv0 && !strcasecmp(up_argv0, up_target)) return;            /* (c'est deja ce fichier) */
    crumb("demarrage : version plus recente sur la carte SD : %s (build %d > %d)", up_target, b, GCMP3_BUILD);
    say("   Version plus recente sur la carte SD : lancement... %s", "");
    const char *e = gl_boot_dol(up_target, NULL, NULL, "GC Station");   /* (ne revient pas si ca marche) */
    if (e) crumb("demarrage : lancement de %s impossible : %s", up_target, e);
}
/* v92 : temps de chaque etape du demarrage (SD:/gc-mp3/demarrage.txt) */
static char bt_txt[640];
static int bt_len = 0;
static u64 bt_prev = 0;
static void bt_step(const char *what) {
    u64 n = ticks_to_millisecs(gettime());
    if (!bt_prev) bt_prev = boot_t0;
    if (bt_len < (int)sizeof(bt_txt) - 64) bt_len += snprintf(bt_txt + bt_len, sizeof(bt_txt) - bt_len, "%-24s %6u ms\n", what, (unsigned)(n - bt_prev));
    bt_prev = n;
}
static void bt_save(void) {
    crumb("demarrage : %s", bt_txt);
    if (!sd_ok || sd_virtual) return;
    FILE *f = fopen(GCD "/demarrage.txt", "w");
    if (!f) return;
    fprintf(f, "GC Station %s : temps du dernier demarrage (total %u ms)\n%s", GCMP3_VERSION, (unsigned)(ticks_to_millisecs(gettime()) - boot_t0), bt_txt);
    fclose(f);
}
static void boot_selfcheck(void) {
    /* memoire libre, sans rien reserver (v80 : un essai de 12 Mo faussait la mesure du simulateur pc-sim) :
       place jamais donnee (arene) + memoire rendue au tas, comme Outils > Infos console */
    struct mallinfo mi = mallinfo();
    size_t freekb = (size_t)(((uintptr_t)SYS_GetArena1Hi() - (uintptr_t)SYS_GetArena1Lo() + mi.fordblks) / 1024);
    int sd_wr = 0;
    if (sd_ok) {
        mkdir(GCD, 0777);
        FILE *f = fopen(GCD "/.ecriture", "wb");
        if (f) { sd_wr = (fwrite("ok", 1, 2, f) == 2); fclose(f); remove(GCD "/.ecriture"); }
    }
    u32 ms = boot_t0 ? (u32)(now_ms() - boot_t0) : 0;
    crumb("demarrage : %u ms, memoire libre %u Ko, carte SD %s", (unsigned)ms, (unsigned)freekb,
          !sd_ok ? "absente" : sd_wr ? "ecriture OK" : "LECTURE SEULE");
    if (sd_ok && !sd_wr) snprintf(boot_check, sizeof(boot_check), "Carte SD en lecture seule : les r\xC3\xA9glages ne seront pas gard\xC3\xA9s (languette de protection ?)");
    else if (freekb < 2048) snprintf(boot_check, sizeof(boot_check), "M\xC3\xA9moire presque pleine : certaines images peuvent ne pas s'afficher");
}

int main(int argc, char **argv) {
    boot_t0 = ticks_to_millisecs(gettime());
    up_argv0 = (argc > 0 && argv && argv[0]) ? argv[0] : NULL;
    if (!setlocale(LC_CTYPE, "C.UTF-8")) setlocale(LC_CTYPE, "C-UTF-8");   /* v26 : noms de fichiers en UTF-8 (accents, tirets longs...) : sans ca, libfat s'arrete sur certains noms */
    sd_ok = mount_sd();                     /* d'abord les reglages (mode video, theme...) */
    if (!sd_ok) sd_ok = ramfs_mount();      /* (Dolphin : SD en memoire, reglages sur la carte memoire) */
    bt_step("carte SD");
    if (sd_ok) gcd_migrate();               /* v22 : tout dans SD:/gc-mp3 */
    crash_init();                           /* journal de plantage */
    ui_lang_apply();                        /* v24 : langue de la console, sauf reglage */
    load_settings();
    bt_step("reglages");
    nfs_mount();
    jeux_cfg_boot();                        /* v75 : langue / son de la console, reseau */
    if (sd_ok) ld_boot();                   /* v45 : retour.txt (retour en jeu) + journal du lanceur */
    bt_step("jeux et lanceur");
    if (!init_video()) return 1;
    bt_step("video");
    gx_init();
    init_spectrum();
    bt_step("gx");
    disc_chain();                            /* v92 : lance depuis un disque : la version plus recente de la carte SD */

    /* v92 : chaque etape du demarrage a l'ecran et dans SD:/gc-mp3/demarrage.txt (temps en ms) */
    if (sd_ok) { say("   Musique de la carte SD... %s", ""); scan_files(); { char nb[16]; snprintf(nb, sizeof(nb), "%d", nfiles); say("%s\n", nb); } bt_step("liste de la musique"); }
    if (sd_ok) { say("   Bibliotheque... %s", ""); bib_load(); pl_load_all(); stats_load(); rating_load(); say("%s\n", "OK"); bt_step("bibliotheque"); }
    if (sd_ok) nat_journal_convert();       /* v54 : journal du dernier jeu lance par le lanceur maison */
    bt_step("journal Pulse");
    say("   Radios... %s", "");
    load_stations();
    say("%s\n", "OK");
    bt_step("radios");
    lib_rebuild();
    bt_step("tri de la musique");
    boot_selfcheck();                        /* v76 : controle memoire / carte SD / duree */
    bt_save();
    if (boot_check[0]) lib_note(boot_check);
    if (covers_auto) net_start_async();     /* reseau en fond, pour les pochettes */

    AUDIO_Init(NULL);
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
    AUDIO_RegisterDMACallback(dma_cb);
    memset(dmabuf, 0, sizeof(dmabuf));
    DCFlushRange(dmabuf, sizeof(dmabuf));
    dma_cb();
    AUDIO_StartDMA();

    reset_init();                            /* v38 : bouton Reset */
    resume_start();                          /* reprise du dernier morceau */
    last_input_ms = now_ms();

    u32 rep_mask = 0;
    u64 rep_next = 0;
    int cs_prev = 0;
    u64 settings_t = 0;

    while (1) {
        frame_t0_us = perf_us();                                    /* v85 : (decodage pendant l'attente de l'ecran) */
        perf_frame(tl_screen || menu_open ? PF_TABS - 1 : tab);     /* v69 : mesure de fluidite */
        u32 pf_in0 = perf_on ? perf_us() : 0;
        crash_beat();
        u32 pads = PAD_ScanPads();
        u32 down = PAD_ButtonsDown(0);
        u32 held = PAD_ButtonsHeld(0);
        u64 now = now_ms();
        /* clavier GameCube : touches traduites en boutons (ou tapees dans le clavier a l'ecran) */
        u32 kdown = 0, knav = 0;
        int kfire = 0;
        gk_pump(pads, &kdown, &knav, &kfire);
        down |= kdown;
        down = gp_input(down);                              /* v23 : curseur au stick dans tous les onglets */

        /* activite de la manette (economiseur d'ecran) */
        int sx = PAD_SubStickX(0), sy = PAD_SubStickY(0), mx = PAD_StickX(0), my = PAD_StickY(0);
        int moved = abs(sx) > 40 || abs(sy) > 40 || abs(mx) > 40 || abs(my) > 40;
        if (down || moved || kfire) last_input_ms = now;
        if (saver_on) {
            if (down || moved || kfire) { saver_on = 0; down = 0; held = 0; kfire = 0; }
        } else if (saver_min > 0 && now - last_input_ms > (u64)saver_min * 60000 && !kb_open && !menu_open && !party_on && !night_on && !fun_mode) {
            saver_on = 1;
        }

        /* croix, avec repetition quand on la maintient */
        u32 nav = held & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
        int fire = 0;
        static u64 rep_start = 0;
        if (down & nav) { fire = 1; rep_mask = nav; rep_next = now + 320; rep_start = now; }
        else if (nav && nav == rep_mask && now >= rep_next) {      /* de plus en plus vite */
            fire = 1;
            rep_next = now + (now - rep_start > 1500 ? 35 : now - rep_start > 800 ? 50 : 70);
        }
        else if (!nav) rep_mask = 0;
        if (kfire && !saver_on) { fire = 1; nav |= knav; }
        if (saver_on) fire = 0;
        if (night_on && !menu_open && !kb_open) { night_input(down, nav, fire); down = 0; fire = 0; }

        if (ck) { checklist_input(down, nav, fire); down = 0; fire = 0; }
        else if (kb_open) { kb_input(down, nav, fire); down = 0; fire = 0; }   /* START = valider (v35 : avant le menu : clavier ouvert depuis les reglages) */
        else if (menu_open) { menu_input(down, nav, fire); down = 0; fire = 0; }
        else if (ic_on) { indigo_cfg_input(down, nav, fire); down = 0; fire = 0; }   /* v32 : reglages d'un jeu */
        else if (tl_screen) { tools_input(down, nav, fire); down = 0; fire = 0; }   /* v35 : outils (cartes memoire, applis...) */
        else if (am_open) { actions_input(down, nav, fire); down = 0; fire = 0; }
        else if (fun_mode && !night_on) {                     /* karaoke, jeux, reveil */
            u32 keep = fun_mode == FUN_KARAOKE ? (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) : 0;
            fun_input(down, nav, fire);
            down = keep; fire = 0;
        }
        fun_pump(pads);
        if (quit_requested) break;

        /* minuterie : le son baisse pendant 30 s, puis tout s'arrete */
        if (sleep_at) {
            if (now >= sleep_at) { stop_track(); stop_station(); sleep_at = 0; sleep_min = 0; sleep_fade = 256; party_on = 0; }
            else sleep_fade = (sleep_at - now < 30000) ? (int)((sleep_at - now) * 256 / 30000) : 256;
        }
        if (sleep_eot && src == SRC_NONE) sleep_eot = 0;    /* v76 : plus rien ne joue : minuterie finie */

        /* mode soiree : B / START pour sortir, stick C pour le style */
        if (party_on && !menu_open && !kb_open) {
            int pc = (sx > 60) ? 1 : (sx < -60) ? -1 : 0;
            if (pc && pc != cs_prev) { party_style = (party_style + pc + PARTY_STYLES) % PARTY_STYLES; pt_hint_until = now + 2500; settings_dirty = 1; }
            cs_prev = pc;
            if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) party_on = 0;
            if ((down & PAD_BUTTON_X) && src == SRC_RADIO) radio_title_keep();
            if (down & PAD_BUTTON_Y) {
                if (src == SRC_FILE && playing) paused = !paused;
                else if (src == SRC_RADIO) muted = !muted;
            }
            down &= (PAD_TRIGGER_L | PAD_TRIGGER_R);
            fire = 0;
        } else if (!menu_open && !kb_open && !night_on && !fun_mode && (tab == TAB_MUSIC || tab == TAB_RADIO)) {
            static int cu_prev = 0;
            int cu = sy > 60;
            if (cu && !cu_prev) party_start();            /* stick C vers le haut */
            cu_prev = cu;
        }
        if (down & PAD_BUTTON_START) { menu_open = 1; down = 0; }

        /* onglets : la lecture en cours continue */
        if (down & PAD_TRIGGER_Z) tab_next(1);

        /* stick C vers le bas : disposition du lecteur */
        {
            static int cd_prev = 0;
            int cdn = sy < -60;
            if (cdn && !cd_prev && !party_on && !night_on && !menu_open && !kb_open && !ic_on && !tl_screen && !fun_mode && IS_PLAYER_TAB(tab)) layout_next();
            cd_prev = cdn;
        }

        /* stick C gauche / droite : vue de la liste MUSIQUE */
        int cdir = (sx > 60) ? 1 : (sx < -60) ? -1 : 0;
        if (!party_on) {
            if (cdir && cdir != cs_prev && tab == TAB_MUSIC && !night_on && !menu_open && !kb_open && !ic_on && !tl_screen && !fun_mode) { set_view(view_mode + cdir); settings_dirty = 1; }
            cs_prev = cdir;
        }

        int nolist = IS_PLAYER_TAB(tab) && (player_layout == L_COVER || player_layout == L_MINIMAL || player_layout == L_BIGVIZ);
        if (nolist && (down & PAD_BUTTON_A)) {                  /* sans liste : A = pause / lecture */
            if (src == SRC_FILE && playing) paused = !paused;
            else if (src == SRC_RADIO) muted = !muted;
            down &= ~PAD_BUTTON_A;
        }
        if (nolist) fire = 0;
        if (tab == TAB_WEB) {
            if (!menu_open && !kb_open) web_input(down, nav, fire);
        } else if (tab == TAB_RETRO) {
            retro_input(down, nav, fire);
        } else if (tab == TAB_PODCAST) {
            podcast_input(down, nav, fire);
        } else if (tab == TAB_PHOTO) {
            photo_input(down, nav, fire);
        } else if (tab == TAB_FILES) {
            files_input(down, nav, fire);
        } else if (tab == TAB_INFO) {
            infos_input(down, nav, fire);
        } else if (tab == TAB_PHONE) {
            phone_input(down, nav, fire);
        } else if (tab == TAB_GAMES) {
            games_tab_input(down, nav, fire);
            if (gl_on) down &= ~(PAD_TRIGGER_L | PAD_TRIGGER_R);     /* (L / R : affichage des jeux) */
        } else if (tab == TAB_DL) {
            dltab_input(down, nav, fire);
        } else if (tab == TAB_MUSIC) {
            if (fire && nents > 0) {
                if (nav & PAD_BUTTON_DOWN)  sel_row++;
                if (nav & PAD_BUTTON_UP)    sel_row--;
                if (nav & PAD_BUTTON_RIGHT) sel_row += MUSIC_ROWS;
                if (nav & PAD_BUTTON_LEFT)  sel_row -= MUSIC_ROWS;
                if (sel_row < 0) sel_row = 0;
                if (sel_row > nents - 1) sel_row = nents - 1;
            }
            if (down & PAD_BUTTON_A) lib_activate();       /* chargement : l'audio joue du silence */
            if ((down & PAD_BUTTON_B) && lib_find[0]) { lib_find_set(""); down &= ~PAD_BUTTON_B; }   /* B : fin de la recherche */
            if (down & PAD_BUTTON_B) { stop_track(); stop_station(); track_err = NULL; err_msg[0] = 0; }
            if ((down & PAD_BUTTON_X) && sd_ok) actions_open();     /* listes, recherche, actualiser */
            if ((down & PAD_BUTTON_Y) && src == SRC_FILE && playing) paused = !paused;
            if ((down & PAD_BUTTON_Y) && src == SRC_RADIO) muted = !muted;
        } else if (tab == TAB_RADIO) {
            int rows = radio_rows();
            if (fire && rows > 0) {
                if (nav & PAD_BUTTON_DOWN)  sel_station++;
                if (nav & PAD_BUTTON_UP)    sel_station--;
                if (nav & PAD_BUTTON_RIGHT) sel_station += MUSIC_ROWS;
                if (nav & PAD_BUTTON_LEFT)  sel_station -= MUSIC_ROWS;
                if (sel_station < 0) sel_station = 0;
                if (sel_station > rows - 1) sel_station = rows - 1;
            }
            if (down & PAD_BUTTON_A) radio_activate();
            if (down & PAD_BUTTON_B) { stop_track(); stop_station(); track_err = NULL; err_msg[0] = 0; }
            if ((down & PAD_BUTTON_X) && rb_mode && sel_station > 0) rb_add_favorite(sel_station - 1);
            else if (!rb_mode) {                                  /* v72 : X court : garder le titre ; X maintenu : radio favorite */
                static u64 x_t0 = 0; static int x_armed = 0;
                if (down & PAD_BUTTON_X) { x_t0 = now; x_armed = 1; }
                if (x_armed && (held & PAD_BUTTON_X) && now - x_t0 > 600) { x_armed = 0; if (sel_station >= RS_N) radio_fav_toggle(sel_station - RS_N); }
                if (x_armed && !(held & PAD_BUTTON_X)) { x_armed = 0; radio_title_keep(); }   /* garder le titre qui passe */
            }
            if (down & PAD_BUTTON_Y) {
                if (src == SRC_FILE && playing) paused = !paused;
                else if (src == SRC_RADIO) muted = !muted;
            }
        }
        if (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) transport_step((down & PAD_TRIGGER_R) ? 1 : -1);

        /* v49 : pendant un mini-jeu, rien de lourd en fond (reseau https, carte SD, pochettes,
           etiquettes) : chaque image doit tenir dans 1/60 s pour que le jeu reste fluide */
        int in_game = fun_mode != FUN_OFF && fun_mode != FUN_KARAOKE && fun_mode != FUN_WAKE;
        if (perf_on) pfx_cur[PH_INPUT] += perf_us() - pf_in0;
        /* navigateur : reception des pages et des telechargements */
        PF(PH_WEB, web_pump());
        if (!in_game) {
            PF(PH_COVERS, cover_async_pump(); cover_pump());
            PF(PH_LYRICS, lyrics_pump());
            PF(PH_RETRO, retro_pump());
            PF(PH_PODCAST, podcast_pump());
            PF(PH_PHOTO, photo_pump_tab());
            PF(PH_FILES, files_pump());
            PF(PH_INFOS, infos_pump());
        }
        PF(PH_PHONE, phone_pump());
        PF(PH_DL, dltab_pump());
        if (!in_game) PF(PH_UPDATE, update_pump());
        PF(PH_MISC, cheats_tick(); reset_pump());
        if (!in_game) {
            PF(PH_MISC, igr_pump(); ld_pump(); mg_pump());   /* v91 : reprise de SD:/swiss en fond */
        }
        PF(PH_MISC, night_pump(); rb_pump(); stats_pump(); x73_pump());
        if (!in_game) {
            PF(PH_LIB, lib_pump(); bib_pump(); ramfs_pump());
            PF(PH_PHOTO, photo_pump());
            PF(PH_LIB, resume_note(0));
        }
        if (settings_dirty && !in_game) {                   /* reglages changes : enregistres 2 s apres */
            if (!settings_t) settings_t = now;
            else if (now - settings_t > 2000) { PF(PH_SETTINGS, save_settings()); settings_t = 0; }
        }

        PF(PH_AUDIO, service_audio());

        /* musique : decodage du fichier en cours */
        PF(PH_MUSIC, music_pump());

        /* radio : reception, decodage, tampon */
        PF(PH_STREAM, stream_pump(); radio_repair_ok(); radio_last_note(); radio_repair_pump(); radio_resume_pump());
        PF(PH_AUDIO, service_audio());

        PF(PH_SPECTRUM, update_spectrum());
        {
            u32 pd0 = perf_on ? perf_us() : 0, pg = pfx_cur[PH_GPU] + pfx_cur[PH_WAIT];
            draw();
            if (perf_on) pfx_cur[PH_DRAW] += perf_us() - pd0 - (pfx_cur[PH_GPU] + pfx_cur[PH_WAIT] - pg);   /* (dessin seul) */
        }
    }

    resume_note(1);                          /* garde la position pour la prochaine fois */
    if (bib_dirty) bib_save();
    if (stats_dirty) stats_save();
    if (rating_dirty) rating_save();
    if (sd_virtual && rf_dirty) rf_card_save();
    AUDIO_RegisterDMACallback(NULL);
    stop_track();
    stop_station();
    AUDIO_StopDMA();
    return 0;
}

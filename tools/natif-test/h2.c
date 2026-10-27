#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>
#include <sys/stat.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int32_t s32; typedef int16_t s16;
#define GCD "out"
#define NAME_LEN 256
#define ATTRIBUTE_ALIGN(n) __attribute__((aligned(n)))
static int nat_igr = 1, nat_card = 1;
static char vc_keep[64] = "";                                 /* (v122 : jeux_cfg.c) */
static int nat_speed_auto = 0; static char rep_msg[120];   /* (v140 : jeux_cfg.c) */                              /* (v133 : jeux_cfg.c) */
static int sd_virtual = 0, sd_slot = 0, sd_ok = 1, settings_dirty = 0;
static u32 be32(const unsigned char *p){return (u32)p[0]<<24|p[1]<<16|p[2]<<8|p[3];}
static void service_audio(void){}
#include <time.h>
static u64 now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (u64)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static int nat_ld_on = 0;
static void draw(void) {}
static volatile u32 retrace_count = 0; static int present_nowait = 0;
#define TR(x) (x)
static const char *nat_self_path(void) { return getenv("NATIF_SELF"); }   /* (v64 : .dol "GC Station" pour le retour) */

static void *read_file(const char *p, long max, long *len) { FILE *f = fopen(p, "rb"); if (!f) return NULL; void *b = malloc(max); *len = b ? (long)fread(b, 1, max, f) : 0; fclose(f); return b; }
static void crumb(const char*f,...){}
static void stop_track(void){} static volatile int gj_hold; static void bg_quiesce(void){} /* v96 */
static int mount_sd(void){return 1;}
typedef struct { char path[NAME_LEN]; u8 ciso; long size; char title[72]; char id[7]; u8 disc, dvd; } gcg_t;   /* (v121 : id, disque) */
static gcg_t gl[2]; static int gl_n=1; static char gl_busy[96];
static void gl_status(const char*m){} static void gl_note(const char*m){printf("NOTE: %s\n",m);}
#define GL_STUB_AT 0
#define GL_TAB_AT 0
static const unsigned gl_stub[1];
#include "gd.inc"
/* v121 : cheats du banc d'essai : mots hexa dans le fichier NATIF_CHEATS */
static int ch_collect(const char *id, u32 *out, int cap) { (void)id; const char *p = getenv("NATIF_CHEATS"); FILE *f = p ? fopen(p, "r") : NULL; int n = 0; unsigned v; if (!f) return 0; while (n < cap && fscanf(f, "%x", &v) == 1) out[n++] = v; fclose(f); return n; }
static FILE *img;
static u32 first_clus;
static int host_stat(const char *p, struct stat *st) { int r = stat(p, st); if (!r) st->st_ino = strstr(p, "disc2") ? 10000 : strstr(p, "carte-B") ? 55000 : strstr(p, "journal-jeu") ? 20000 : strstr(p, "memoire-jeu") ? 60000 : strstr(p, "selfdol") ? 30000 : strstr(p, "carte-A") ? 40000 : strstr(p, "captures") ? 90000 : first_clus; return r; }   /* (journal : un cluster libre de mkfat.py) */
#define stat(p, s) host_stat(p, s)
#include <zlib.h>
static int stbi_zlib_decode_buffer(char *o, int ol, const char *i, int il) { uLongf d = (uLongf)ol; return uncompress((Bytef *)o, &d, (const Bytef *)i, (uLong)il) == Z_OK ? (int)d : -1; }   /* (v152) */
#include "natif.c"
#undef stat
static int na_rawsec(u32 sec, void *buf) { return fseek(img, (long)sec * 512, SEEK_SET) || fread(buf, 1, 512, img) != 512 ? -1 : 0; }
int main(int c, char **v) {
    img = fopen(v[2], "rb"); first_clus = atoi(v[3]);
    strcpy(gl[0].path, v[1]); struct stat st; host_stat(v[1], &st); gl[0].size = st.st_size; gl[0].ciso = c > 4;
    { gdisc_t d; u8 h[8]; if (gd_open(&d, v[1])) { if (gd_read(&d, 0, h, 8)) { memcpy(gl[0].id, h, 6); gl[0].disc = h[6]; } gd_close(&d); } }
    if (getenv("NATIF_DISC2")) {                          /* v121 : l'autre disque */
        const char *p2 = getenv("NATIF_DISC2"); strcpy(gl[1].path, p2); host_stat(p2, &st); gl[1].size = st.st_size;
        gdisc_t d; u8 h[8]; if (gd_open(&d, p2)) { if (gd_read(&d, 0, h, 8)) { memcpy(gl[1].id, h, 6); gl[1].disc = h[6]; } gd_close(&d); }
        gl_n = 2;
    }
    nat_ws = getenv("NATIF_WS") != NULL;
    if (getenv("NATIF_CARD")) nat_card = atoi(getenv("NATIF_CARD"));          /* v122 : 2 = slots A et B */
    if (getenv("NATIF_VMODE")) nat_vmode = atoi(getenv("NATIF_VMODE"));
    nat_wide = getenv("NATIF_WIDE") != NULL; nat_deflick = getenv("NATIF_DEFLICK") != NULL;
    if (getenv("NATIF_CSTICK")) nat_cstick = atoi(getenv("NATIF_CSTICK"));
    if (getenv("NATIF_TRIG")) nat_trig = atoi(getenv("NATIF_TRIG"));
    if (getenv("NATIF_POLL")) nat_poll = atoi(getenv("NATIF_POLL"));
    nat_speed = getenv("NATIF_SPEED") != NULL;
    nat_trap = getenv("NATIF_TRAP") != NULL;                 /* v139 */
    if (getenv("NATIF_NET")) {                                 /* v143 : ISO lue par le reseau (serveur simule par le banc) */
        na_net_on = 1; memset(&na_netc, 0, sizeof(na_netc));
        na_netc.magic = NATRES_NET_MAGIC; na_netc.ip = 0xC0A80132u; na_netc.srv = 0xC0A8010Au; na_netc.port = 51235;
        na_netc.tok[0] = 7; na_netc.size[0] = (u32)gl[0].size; na_netc.mask = 0xFFFFFF00u;
    }
    nat_run(0);
    printf("%s\n", nat_boot(0));
    return 0;
}

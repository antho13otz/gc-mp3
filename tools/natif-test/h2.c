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
static void stop_track(void){}
static int mount_sd(void){return 1;}
typedef struct { char path[NAME_LEN]; u8 ciso; long size; char title[72]; } gcg_t;
static gcg_t gl[1]; static int gl_n=1; static char gl_busy[96];
static void gl_status(const char*m){} static void gl_note(const char*m){printf("NOTE: %s\n",m);}
#define GL_STUB_AT 0
#define GL_TAB_AT 0
static const unsigned gl_stub[1];
#include "gd.inc"
static FILE *img;
static u32 first_clus;
static int host_stat(const char *p, struct stat *st) { int r = stat(p, st); if (!r) st->st_ino = strstr(p, "journal-jeu") ? 20000 : strstr(p, "memoire-jeu") ? 60000 : strstr(p, "selfdol") ? 30000 : strstr(p, "carte-A") ? 40000 : first_clus; return r; }   /* (journal : un cluster libre de mkfat.py) */
#define stat(p, s) host_stat(p, s)
#include "natif.c"
#undef stat
static int na_rawsec(u32 sec, void *buf) { return fseek(img, (long)sec * 512, SEEK_SET) || fread(buf, 1, 512, img) != 512 ? -1 : 0; }
int main(int c, char **v) {
    img = fopen(v[2], "rb"); first_clus = atoi(v[3]);
    strcpy(gl[0].path, v[1]); struct stat st; host_stat(v[1], &st); gl[0].size = st.st_size; gl[0].ciso = c > 4;
    nat_run(0);
    printf("%s\n", nat_boot(0));
    return 0;
}

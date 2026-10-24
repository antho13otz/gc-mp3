#define GEKKO 1
#include <gccore.h>
#include <fat.h>
#include <sdcard/gcsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#define GCD "sd:/gc-mp3"
#define NAME_LEN 256
static int nat_igr = 1, nat_card = 1;
static int sd_virtual = 0, sd_slot = 0, sd_ok = 1, settings_dirty = 0;
static u32 be32(const unsigned char *p){return (u32)p[0]<<24|p[1]<<16|p[2]<<8|p[3];}
static void service_audio(void){}
static int nat_ld_on = 0;
static void draw(void) {}
static volatile u32 retrace_count = 0; static int present_nowait = 0;
#define TR(x) (x)
static const char *nat_self_path(void) { return getenv("NATIF_SELF"); }   /* (v64 : .dol "GC Station" pour le retour) */

#include <ogc/lwp_watchdog.h>
static u64 now_ms(void){ return ticks_to_millisecs(gettime()); }
static void *read_file(const char *p, long max, long *len) { FILE *f = fopen(p, "rb"); if (!f) return NULL; void *b = malloc(max); *len = b ? (long)fread(b, 1, max, f) : 0; fclose(f); return b; }
static void crumb(const char*f,...){}
static void stop_track(void){} static void stop_station(void){} static void save_settings(void){} static volatile int gj_hold; static void bg_quiesce(void){} /* v96 */
static int mount_sd(void){return 1;}
typedef struct { char path[NAME_LEN]; u8 ciso; long size; char title[72]; } gcg_t;
static gcg_t gl[1]; static int gl_n=1; static char gl_busy[96];
static void gl_status(const char*m){} static void gl_note(const char*m){}
static const u32 gl_stub[] = {0};
#define GL_STUB_AT  0x80001800u
#define GL_TAB_AT   0x80002000u
#include "gd.inc"
#include "natif.c"
int main(void){ nat_run(0); return nat_boot(0) != 0; }

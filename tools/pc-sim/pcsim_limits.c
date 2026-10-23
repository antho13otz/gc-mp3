/* GC Station sur PC : les limites de la vraie GameCube (sauf PCSIM_FAST=1).
   - MEMOIRE : le tas de GC Station = ce qui reste entre la fin du programme charge
     (.dol : code + donnees + bss) et 0x81700000 (haut de l'arene de libogc).
     v79 : programme 80003100 - 80A572E0 -> tas de 12,66 Mo. PCSIM_DOL=fichier.dol le
     recalcule, PCSIM_HEAP=octets l'impose. Au-dela, malloc rend NULL, comme sur la console.
     mallinfo() et SYS_GetArena1Lo/Hi decrivent ce tas (memoire libre affichee par GC Station).
   - PROCESSEUR : Gekko 486 MHz. Le temps de calcul des threads de GC Station est multiplie
     par PCSIM_CPU (12 par defaut : Xeon 2,1 GHz -> Gekko ; a recaler avec fluidite.txt).
     Le dessin logiciel (fait par le processeur graphique sur la console) n'est pas compte.
   - CARTE SD : debit de l'adaptateur (SD2SP2 / SD Gecko, libfat en SPI a 16 MHz) :
     PCSIM_SD_KBS (lecture, 1500 Ko/s), PCSIM_SD_WKBS (ecriture, 600 Ko/s), plus un temps
     d'acces par ouverture de fichier, stat, dossier (recherche dans la FAT).
   - RESEAU (Broadband Adapter) : PCSIM_NET_KBS (reception, 500 Ko/s). */
#define _GNU_SOURCE
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>
#include "pcsim.h"

int pcsim_fast = 0;
double pcsim_cpu_k = 12.0;
static size_t heap_budget = 0x81700000u - 0x80A572E0u;
static double sd_rkbs = 1500, sd_wkbs = 600, net_kbs = 500;
static size_t heap_used = 0, heap_peak = 0, heap_fail = 0;
static pthread_mutex_t heap_m = PTHREAD_MUTEX_INITIALIZER;

static u32 be32p(const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
void pcsim_limits_init(void) {
    const char *e;
    pcsim_fast = (e = getenv("PCSIM_FAST")) && *e && *e != '0';
    if ((e = getenv("PCSIM_CPU"))) pcsim_cpu_k = atof(e) > 1 ? atof(e) : 1;
    if ((e = getenv("PCSIM_SD_KBS"))) sd_rkbs = atof(e);
    if ((e = getenv("PCSIM_SD_WKBS"))) sd_wkbs = atof(e);
    if ((e = getenv("PCSIM_NET_KBS"))) net_kbs = atof(e);
    if ((e = getenv("PCSIM_DOL"))) {                        /* fin du programme charge : sections + bss */
        FILE *f = fopen(e, "rb"); u8 h[0x100];
        if (f && fread(h, 1, sizeof(h), f) == sizeof(h)) {
            u32 end = 0;
            for (int i = 0; i < 18; i++) { u32 a = be32p(h + 0x48 + i * 4), s = be32p(h + 0x90 + i * 4); if (s && a + s > end) end = a + s; }
            u32 b = be32p(h + 0xD8), bs = be32p(h + 0xDC);
            if (b + bs > end) end = b + bs;
            if (end > 0x80003000u && end < 0x81700000u) heap_budget = 0x81700000u - end;
        }
        if (f) fclose(f);
    }
    if ((e = getenv("PCSIM_HEAP"))) heap_budget = (size_t)atof(e);
    if (pcsim_fast) printf("[pcsim] mode rapide : pas de limites GameCube\n");
    else printf("[pcsim] GameCube : tas %.2f Mo, processeur x%.0f plus lent, carte SD %.0f / %.0f Ko/s, reseau %.0f Ko/s\n",
                heap_budget / 1048576.0, pcsim_cpu_k, sd_rkbs, sd_wkbs, net_kbs);
}
void pcsim_limits_report(void) {
    if (pcsim_fast) return;
    printf("[pcsim] memoire : pic %.2f Mo sur %.2f Mo, %zu reservations refusees\n", heap_peak / 1048576.0, heap_budget / 1048576.0, heap_fail);
}

/* ======================= memoire ======================= */
void *__real_malloc(size_t); void __real_free(void *); void *__real_calloc(size_t, size_t); void *__real_realloc(void *, size_t);
void *__real_memalign(size_t, size_t); int __real_posix_memalign(void **, size_t, size_t); void *__real_aligned_alloc(size_t, size_t);
#define CHUNK_OVH 8                                           /* (en-tete d'un bloc, comme newlib) */
static int heap_take(void *p) {                               /* 1 = gardee ; 0 = refusee (rendue) */
    if (!p) return 0;
    size_t n = malloc_usable_size(p) + CHUNK_OVH;
    pthread_mutex_lock(&heap_m);
    if (!pcsim_fast && heap_used + n > heap_budget) {
        heap_fail++;
        pthread_mutex_unlock(&heap_m);
        if (getenv("PCSIM_MEMLOG")) printf("[pcsim] memoire pleine : %zu octets refuses (utilise %.2f Mo)\n", n, heap_used / 1048576.0);
        __real_free(p);
        return 0;
    }
    heap_used += n; if (heap_used > heap_peak) heap_peak = heap_used;
    pthread_mutex_unlock(&heap_m);
    return 1;
}
static void heap_give(void *p) {
    if (!p) return;
    size_t n = malloc_usable_size(p) + CHUNK_OVH;
    pthread_mutex_lock(&heap_m); heap_used = heap_used > n ? heap_used - n : 0; pthread_mutex_unlock(&heap_m);
}
void *__wrap_malloc(size_t n) { PCSIM_LIB_ENTER(); void *p = __real_malloc(n); if (!heap_take(p)) p = NULL; PCSIM_LIB_EXIT(); return p; }
void __wrap_free(void *p) { PCSIM_LIB_ENTER(); heap_give(p); __real_free(p); PCSIM_LIB_EXIT(); }
void *__wrap_calloc(size_t a, size_t b) { PCSIM_LIB_ENTER(); void *p = __real_calloc(a, b); if (!heap_take(p)) p = NULL; PCSIM_LIB_EXIT(); return p; }
void *__wrap_memalign(size_t al, size_t n) { PCSIM_LIB_ENTER(); void *p = __real_memalign(al, n); if (!heap_take(p)) p = NULL; PCSIM_LIB_EXIT(); return p; }
void *__wrap_aligned_alloc(size_t al, size_t n) { PCSIM_LIB_ENTER(); void *p = __real_aligned_alloc(al, n); if (!heap_take(p)) p = NULL; PCSIM_LIB_EXIT(); return p; }
int __wrap_posix_memalign(void **o, size_t al, size_t n) { PCSIM_LIB_ENTER(); int r = __real_posix_memalign(o, al, n); if (!r && !heap_take(*o)) { *o = NULL; r = ENOMEM; } PCSIM_LIB_EXIT(); return r; }
void *__wrap_realloc(void *p, size_t n) {
    if (!p) return __wrap_malloc(n);
    if (!n) { __wrap_free(p); return NULL; }
    size_t old = malloc_usable_size(p);
    if (n <= old) return p;                                    /* (reste en place) */
    void *q = __wrap_malloc(n);                                /* (sinon : nouveau bloc d'abord, comme si le tas ne pouvait pas grandir sur place) */
    if (!q) return NULL;
    memcpy(q, p, old); __wrap_free(p);
    return q;
}
char *__wrap_strdup(const char *s) { size_t n = strlen(s) + 1; char *p = __wrap_malloc(n); if (p) memcpy(p, s, n); return p; }
char *__wrap_strndup(const char *s, size_t m) { size_t n = strnlen(s, m); char *p = __wrap_malloc(n + 1); if (p) { memcpy(p, s, n); p[n] = 0; } return p; }
/* ce que GC Station lit pour afficher la memoire libre (newlib : blocs pris / libres ; arene : pas encore pris) */
struct mallinfo __wrap_mallinfo(void) {
    struct mallinfo m; memset(&m, 0, sizeof(m));
    pthread_mutex_lock(&heap_m);
    m.arena = (int)heap_peak; m.uordblks = (int)heap_used; m.fordblks = (int)(heap_peak - heap_used);
    pthread_mutex_unlock(&heap_m);
    return m;
}
size_t pcsim_heap_left(void) { return heap_budget > heap_peak ? heap_budget - heap_peak : 0; }

/* ======================= carte SD ======================= */
static __thread double io_debt_us = 0;
static void io_wait(double us) {                               /* (le thread qui lit attend : libfat en SPI occupe le processeur) */
    if (pcsim_fast) return;
    io_debt_us += us;
    if (io_debt_us >= 1000) { struct timespec t = { (time_t)(io_debt_us / 1e6), (long)((long long)io_debt_us % 1000000) * 1000 }; nanosleep(&t, NULL); io_debt_us = 0; }
}
#define NSDF 64
static struct { FILE *outer, *inner; } sdf[NSDF];               /* (fileno : celui du vrai fichier) */
static pthread_mutex_t sdf_m = PTHREAD_MUTEX_INITIALIZER;
int __real_fileno(FILE *f);
int __wrap_fileno(FILE *f) {
    pthread_mutex_lock(&sdf_m);
    for (int i = 0; i < NSDF; i++) if (sdf[i].outer == f) { FILE *in = sdf[i].inner; pthread_mutex_unlock(&sdf_m); return __real_fileno(in); }
    pthread_mutex_unlock(&sdf_m);
    return __real_fileno(f);
}
static int is_sd(const char *p) { return p && !strncmp(p, "sd:", 3); }
typedef struct { FILE *f; } sdc_t;
static ssize_t sdc_read(void *c, char *b, size_t n) { PCSIM_LIB_ENTER(); size_t r = fread(b, 1, n, ((sdc_t *)c)->f); PCSIM_LIB_EXIT(); io_wait(r * 1000.0 / sd_rkbs); return (ssize_t)r; }
static ssize_t sdc_write(void *c, const char *b, size_t n) { PCSIM_LIB_ENTER(); size_t r = fwrite(b, 1, n, ((sdc_t *)c)->f); PCSIM_LIB_EXIT(); io_wait(r * 1000.0 / sd_wkbs); return r == n ? (ssize_t)r : -1; }
static int sdc_seek(void *c, off64_t *o, int w) { FILE *f = ((sdc_t *)c)->f; if (fseeko(f, *o, w)) return -1; *o = ftello(f); return 0; }
static int sdc_close(void *c) {
    FILE *in = ((sdc_t *)c)->f;
    pthread_mutex_lock(&sdf_m); for (int i = 0; i < NSDF; i++) if (sdf[i].inner == in) sdf[i].outer = sdf[i].inner = NULL; pthread_mutex_unlock(&sdf_m);
    int r = fclose(in); __real_free(c); return r;
}
FILE *__real_fopen(const char *p, const char *m);
FILE *__wrap_fopen(const char *p, const char *m) {
    PCSIM_LIB_ENTER(); FILE *f = __real_fopen(p, m); PCSIM_LIB_EXIT();
    if (!f || pcsim_fast || !is_sd(p)) return f;
    io_wait(3000);                                             /* (recherche du fichier dans la FAT) */
    sdc_t *c = __real_malloc(sizeof(sdc_t)); c->f = f;
    cookie_io_functions_t io = { sdc_read, sdc_write, sdc_seek, sdc_close };
    PCSIM_LIB_ENTER(); FILE *g = fopencookie(c, m, io); PCSIM_LIB_EXIT();
    if (!g) { __real_free(c); return f; }
    pthread_mutex_lock(&sdf_m); for (int i = 0; i < NSDF; i++) if (!sdf[i].outer) { sdf[i].outer = g; sdf[i].inner = f; break; } pthread_mutex_unlock(&sdf_m);
    return g;
}
int __real_stat(const char *p, struct stat *s);
int __wrap_stat(const char *p, struct stat *s) { if (is_sd(p)) io_wait(1500); PCSIM_LIB_ENTER(); int r = __real_stat(p, s); PCSIM_LIB_EXIT(); return r; }
DIR *__real_opendir(const char *p);
DIR *__wrap_opendir(const char *p) { if (is_sd(p)) io_wait(2000); PCSIM_LIB_ENTER(); DIR *d = __real_opendir(p); PCSIM_LIB_EXIT(); return d; }
struct dirent *__real_readdir(DIR *d);
struct dirent *__wrap_readdir(DIR *d) { io_wait(150); PCSIM_LIB_ENTER(); struct dirent *e = __real_readdir(d); PCSIM_LIB_EXIT(); return e; }

/* ======================= reseau (reception) ======================= */
static double net_tokens = 64 * 1024; static u64 net_t = 0;
int pcsim_net_allow(int want) {                                /* octets permis maintenant (0 = attendre) */
    if (pcsim_fast || net_kbs <= 0) return want;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    u64 now = (u64)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
    if (net_t) { net_tokens += (now - net_t) * net_kbs * 1.024 / 1000.0; if (net_tokens > 64 * 1024) net_tokens = 64 * 1024; }
    net_t = now;
    int ok = net_tokens >= want ? want : (int)net_tokens;
    return ok < 0 ? 0 : ok;
}
void pcsim_net_used(int n) { if (n > 0) net_tokens -= n; }

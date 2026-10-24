/* GC Station sur PC : remplacant de libogc (tout sauf GX, voir pcsim_gx.c).
   - threads : vrais threads du PC, mais UN SEUL tourne a la fois (verrou global, comme
     le processeur unique de la GameCube) ; un thread cede la main quand il attend
     (usleep, reseau, attente d'image, verrous...) ;
   - temps : temps reel en millisecondes ; ecran a 60 Hz (rappel de balayage) ;
   - manettes : pilotees par un scenario (PCSIM_SCRIPT, voir README) ;
   - son : tampons DMA consommes au rythme reel, enregistres en WAV si PCSIM_WAV ;
   - carte SD : le dossier "sd:" du dossier courant ; carte memoire : absente ;
   - reseau : vrais sockets du PC (net_* rend -errno comme libogc). */
#define _GNU_SOURCE
#include <gccore.h>
#include <fat.h>
#include <network.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <stdarg.h>
#include <ctype.h>
#include <malloc.h>
#include "pcsim.h"
void *__real_malloc(size_t); void __real_free(void *);

/* ======================= un seul processeur, des priorites (comme les threads LWP) =======================
   Un seul thread de GC Station tourne a la fois (verrou global). Quand il attend (usleep, reseau,
   image...), il rend la main au thread pret le PLUS PRIORITAIRE. Comme sur la console, un thread
   plus prioritaire qui se reveille interrompt tout de suite un thread moins prioritaire (signal
   SIGUSR1 : le thread interrompu rend la main, sauf au milieu d'une fonction du simulateur, ou il
   la rend juste apres). Priorites : principal 64 (libogc), puis celles de LWP_CreateThread. */
#include <signal.h>
static pthread_mutex_t big_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t big_c = PTHREAD_COND_INITIALIZER;
static int big_active = 0, big_owner_prio = 0;
static pthread_t big_owner;
static volatile int preempt_req = 0;
#define NWQ 64
static struct { int used, prio; unsigned seq; } wq[NWQ];
static unsigned wq_seq = 0;
static __thread int big_held = 0;
static __thread int my_prio = 64;
__thread volatile int pcsim_in_lib = 0, pcsim_preempt_pending = 0;
/* processeur de la GameCube : le temps de calcul (PC) d'un thread est multiplie par PCSIM_CPU ;
   le surplus est une "dette" payee en attendant, verrou garde (un seul processeur), sauf si un
   thread plus prioritaire demande la main : il passe devant, la dette sera payee plus tard. */
static __thread u64 cpu_t0 = 0, gx_t0 = 0, debt_ns = 0;
__thread unsigned long long pcsim_gx_ns = 0;
static u64 cpu_host_ns = 0;                                /* temps de calcul (PC) de GC Station */
static u64 cpu_by_prio[128];
static u64 n_preempt = 0;
static u64 thread_ns(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return (u64)t.tv_sec * 1000000000ull + t.tv_nsec; }
static int wq_best(void) {
    int b = -1;
    for (int i = 0; i < NWQ; i++) if (wq[i].used && (b < 0 || wq[i].prio > wq[b].prio || (wq[i].prio == wq[b].prio && (int)(wq[i].seq - wq[b].seq) < 0))) b = i;
    return b;
}
static void acquire_raw(void) {
    pthread_mutex_lock(&big_m);
    int me = -1;
    for (int i = 0; i < NWQ; i++) if (!wq[i].used) { me = i; break; }
    if (me < 0) me = 0;                                    /* (jamais : 64 threads) */
    wq[me].used = 1; wq[me].prio = my_prio; wq[me].seq = wq_seq++;
    for (;;) {
        if (!big_active && wq_best() == me) break;
        if (big_active && my_prio > big_owner_prio && !preempt_req && !getenv("PCSIM_NOPREEMPT")) { preempt_req = 1; pthread_kill(big_owner, SIGUSR1); }
        struct timespec t; clock_gettime(CLOCK_REALTIME, &t); t.tv_nsec += 1000000; if (t.tv_nsec >= 1000000000) { t.tv_sec++; t.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&big_c, &big_m, &t);
    }
    wq[me].used = 0;
    big_active = 1; big_owner = pthread_self(); big_owner_prio = my_prio; preempt_req = 0;
    pthread_mutex_unlock(&big_m);
}
static void release_raw(void) {
    pthread_mutex_lock(&big_m);
    big_active = 0;
    pthread_cond_broadcast(&big_c);
    pthread_mutex_unlock(&big_m);
}
static void cpu_account(void) {
    if (pcsim_fast || !cpu_t0) return;
    u64 now = thread_ns(), used = now - cpu_t0, gx = pcsim_gx_ns - gx_t0;
    used = used > gx ? used - gx : 0;
    debt_ns += (u64)(used * (pcsim_cpu_k - 1.0));
    __atomic_add_fetch(&cpu_host_ns, used, __ATOMIC_RELAXED);
    __atomic_add_fetch(&cpu_by_prio[my_prio & 127], used, __ATOMIC_RELAXED);
    cpu_t0 = now; gx_t0 = pcsim_gx_ns;
}
static void pay_debt(void) {                               /* (interrompu si un thread plus prioritaire attend) */
    if (pcsim_fast) { debt_ns = 0; return; }
    while (debt_ns > 200000 && !preempt_req) {
        u64 d = debt_ns > 2000000 ? 2000000 : debt_ns;
        struct timespec t = { 0, (long)d }; nanosleep(&t, NULL);
        debt_ns -= d;
    }
}
void pcsim_lock(void) {
    if (big_held++) return;
    pcsim_in_lib++;
    acquire_raw();
    cpu_t0 = thread_ns(); gx_t0 = pcsim_gx_ns;
    pay_debt();
    pcsim_preempt_pending = 0;
    pcsim_in_lib--;
}
void pcsim_unlock(void) {
    if (--big_held) return;
    pcsim_in_lib++;
    cpu_account();
    pay_debt();
    release_raw();
    pcsim_in_lib--;
}
/* rendre la main a un thread plus prioritaire, puis la reprendre */
void pcsim_yield_now(void) {
    if (big_held <= 0) return;
    pcsim_in_lib++;
    int h = big_held;
    cpu_account();
    big_held = 0; release_raw();
    acquire_raw(); big_held = h;
    cpu_t0 = thread_ns(); gx_t0 = pcsim_gx_ns;
    pay_debt();
    pcsim_preempt_pending = 0;
    pcsim_in_lib--;
}
static void preempt_sig(int sig) {
    (void)sig;
    if (big_held <= 0) return;
    if (pcsim_in_lib) { pcsim_preempt_pending = 1; return; }
    int e = errno;
    n_preempt++;
    pcsim_yield_now();
    errno = e;
}
/* attente : on rend la main pendant l'appel bloquant */
#define BLOCKING(call) ({ int h_ = big_held; while (big_held) pcsim_unlock(); __typeof__(call) r_ = (call); while (big_held < h_) pcsim_lock(); r_; })
#define BLOCKINGV(call) do { int h_ = big_held; while (big_held) pcsim_unlock(); call; while (big_held < h_) pcsim_lock(); } while (0)

/* ======================= temps ======================= */
static struct timespec t_start;
static u64 real_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)(t.tv_sec - t_start.tv_sec) * 1000000ull + (u64)((t.tv_nsec - t_start.tv_nsec) / 1000);
}
/* v85 : le temps de la GameCube = le temps reel + le calcul pas encore "paye" (la dette du
   thread : on ralentit le PC en dormant au prochain appel bloquant). Sans elle, un calcul long
   ne faisait pas avancer l'horloge vue par GC Station (mesures de duree fausses). */
static u64 pending_us(void) {
    if (pcsim_fast) return 0;
    u64 p = debt_ns;
    if (big_held && cpu_t0) {
        u64 used = thread_ns() - cpu_t0, gx = pcsim_gx_ns - gx_t0;
        used = used > gx ? used - gx : 0;
        p += (u64)(used * (pcsim_cpu_k - 1.0));
    }
    return p / 1000ull;
}
u64 pcsim_now_us(void) { return real_us() + pending_us(); }
u64 gettime(void) { return pcsim_now_us() / 1000ull + 1000ull; }   /* (ms ; jamais 0) */
u32 gettick(void) { return (u32)gettime(); }
void settime(u64 t) { (void)t; }
int usleep(useconds_t us) {
    struct timespec ts = { us / 1000000, (long)(us % 1000000) * 1000 };
    BLOCKINGV(nanosleep(&ts, NULL));
    return 0;
}
unsigned int sleep(unsigned int s) { usleep(s * 1000000u); return 0; }
void usleep_ogc(u32 us) { usleep(us); }

/* ======================= demarrage ======================= */
static VIRetraceCallback post_cb = NULL, pre_cb = NULL;
static volatile u32 vsync_n = 0;
static void *vsync_thread(void *a);
static void *audio_thread(void *a);
static void script_load(void);
__attribute__((constructor)) static void pcsim_boot(void) {
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    mallopt(M_MMAP_MAX, 0);                                /* (toute la memoire sous 4 Go : le programme range des adresses sur 32 bits, comme sur la console) */
    mallopt(M_TRIM_THRESHOLD, -1);
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct sigaction sa; memset(&sa, 0, sizeof(sa)); sa.sa_handler = preempt_sig; sa.sa_flags = SA_RESTART; sigaction(SIGUSR1, &sa, NULL);
    pcsim_lock();                                          /* le thread principal a la main */
    script_load();
    pcsim_limits_init();
    pthread_t t;
    pthread_create(&t, NULL, vsync_thread, NULL);
    pthread_create(&t, NULL, audio_thread, NULL);
}

/* ======================= cache, interruptions ======================= */
void DCFlushRange(void *p, u32 n) { (void)p; (void)n; }
void DCInvalidateRange(void *p, u32 n) { (void)p; (void)n; }
void DCStoreRange(void *p, u32 n) { (void)p; (void)n; }
void DCFlushRangeNoSync(void *p, u32 n) { (void)p; (void)n; }
void DCStoreRangeNoSync(void *p, u32 n) { (void)p; (void)n; }
void ICInvalidateRange(void *p, u32 n) { (void)p; (void)n; }
/* interruptions : le son (DMA) tourne a cote, comme une vraie interruption ; IRQ_Disable
   empeche seulement qu'il passe pendant une section protegee */
static pthread_mutex_t irq_m = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
u32 IRQ_Disable(void) { pthread_mutex_lock(&irq_m); return 1; }
void IRQ_Restore(u32 l) { if (l) pthread_mutex_unlock(&irq_m); }

/* ======================= systeme ======================= */
static u32 sys_lang = SYS_LANG_FRENCH, sys_sound = SYS_SOUND_STEREO, sys_rumble = 1, sys_vmode = 0, sys_prog = 0, sys_eu60 = 0;
static s8 sys_offh = 0;
void SYS_ResetSystem(s32 reset, u32 code, s32 force) {
    (void)code; (void)force;
    if (reset == SYS_SHUTDOWN) return;                    /* (avant de lancer un autre .dol) */
    printf("[pcsim] la console redemarre (SYS_ResetSystem %d) : fin\n", (int)reset);
    pcsim_finish(0);
}
resetcallback SYS_SetResetCallback(resetcallback cb) { (void)cb; return NULL; }
powercallback SYS_SetPowerCallback(powercallback cb) { (void)cb; return NULL; }
u32 SYS_GetLanguage(void) { return sys_lang; }
void SYS_SetLanguage(u32 l) { sys_lang = l; }
u32 SYS_GetSoundMode(void) { return sys_sound; }
void SYS_SetSoundMode(u32 m) { sys_sound = m; }
u32 SYS_GetFontEncoding(void) { return SYS_FONTENC_ANSI; }
s8 SYS_GetDisplayOffsetH(void) { return sys_offh; }
void SYS_SetDisplayOffsetH(s8 o) { sys_offh = o; }
u32 SYS_GetVideoMode(void) { return sys_vmode; }
void SYS_SetVideoMode(u32 m) { sys_vmode = m; }
u32 SYS_GetProgressiveScan(void) { return sys_prog; }
void SYS_SetProgressiveScan(u32 o) { sys_prog = o; }
u32 SYS_GetEuRGB60(void) { return sys_eu60; }
void SYS_SetEuRGB60(u32 o) { sys_eu60 = o; }
u32 SYS_GetRumbleMode(void) { return sys_rumble; }
void SYS_SetRumbleMode(u32 m) { sys_rumble = m; }
void *SYS_AllocateFramebuffer(void *mode) { GXRModeObj *m = mode; return memalign(32, m ? (size_t)m->fbWidth * m->xfbHeight * 2 : 640 * 576 * 2); }   /* (libogc : memalign, dans le tas) */
/* arene : la memoire pas encore prise par le tas (voir pcsim_limits.c) */
static u8 arena_mark[16];
void *SYS_GetArena1Lo(void) { return arena_mark; }
void *SYS_GetArena1Hi(void) { return (void *)((uintptr_t)arena_mark + pcsim_heap_left()); }
void SYS_SetArena1Lo(void *p) { (void)p; }
void SYS_SetArena1Hi(void *p) { (void)p; }
u32 SYS_GetArena1Size(void) { return (u32)pcsim_heap_left(); }
void *SYS_GetArenaLo(void) { return SYS_GetArena1Lo(); }
void *SYS_GetArenaHi(void) { return SYS_GetArena1Hi(); }
s32 SYS_ResetButtonDown(void) { return 0; }
void SYS_Init(void) {}
void SYS_SwitchFiber(u32 a, u32 b, u32 c, u32 d, u32 pc, u32 sp) { (void)a; (void)b; (void)c; (void)d; (void)pc; (void)sp; }
void SYS_ProtectRange(u32 chan, void *addr, u32 bytes, u32 cntrlbits) { (void)chan; (void)addr; (void)bytes; (void)cntrlbits; }

/* ======================= threads ======================= */
typedef struct { void *(*fn)(void *); void *arg; int prio; } th_start_t;
static void *th_tramp(void *p) {
    th_start_t s = *(th_start_t *)p; __real_free(p);
    my_prio = s.prio;
    pcsim_lock();
    void *r = s.fn(s.arg);
    pcsim_unlock();
    return r;
}
s32 LWP_CreateThread(lwp_t *t, void *(*fn)(void *), void *arg, void *stack, u32 size, u8 prio) {
    (void)stack; (void)size;
    th_start_t *s = __real_malloc(sizeof(*s)); s->fn = fn; s->arg = arg; s->prio = prio;
    pthread_t pt;
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 1 << 20);
    if (pthread_create(&pt, &at, th_tramp, s)) { free(s); return -1; }
    pthread_detach(pt);
    if (t) *t = (lwp_t)(uintptr_t)pt;
    return 0;
}
s32 LWP_JoinThread(lwp_t t, void **ret) { (void)t; if (ret) *ret = NULL; return 0; }
lwp_t LWP_GetSelf(void) { return (lwp_t)(uintptr_t)pthread_self(); }
void LWP_YieldThread(void) { BLOCKINGV(sched_yield()); }
s32 LWP_SuspendThread(lwp_t t) { (void)t; return 0; }
s32 LWP_ResumeThread(lwp_t t) { (void)t; return 0; }
void LWP_SetThreadPriority(lwp_t t, u32 p) { if (t == LWP_THREAD_NULL || t == LWP_GetSelf()) my_prio = (int)p; }
/* verrous du jeu : par-dessus le verrou global (on le rend en attendant) */
#define NMX 256
static struct { int used, rec, depth; pthread_t owner; } mx[NMX];
s32 LWP_MutexInit(mutex_t *m, bool rec) {
    for (int i = 1; i < NMX; i++) if (!mx[i].used) { mx[i].used = 1; mx[i].rec = rec; mx[i].depth = 0; *m = i; return 0; }
    return -1;
}
s32 LWP_MutexLock(mutex_t m) {
    if (m == LWP_MUTEX_NULL || m >= NMX) return -1;
    for (;;) {
        if (!mx[m].depth) { mx[m].depth = 1; mx[m].owner = pthread_self(); return 0; }
        if (pthread_equal(mx[m].owner, pthread_self())) { mx[m].depth++; return 0; }
        usleep(100);
    }
}
s32 LWP_MutexTryLock(mutex_t m) {
    if (m == LWP_MUTEX_NULL || m >= NMX) return -1;
    if (!mx[m].depth) { mx[m].depth = 1; mx[m].owner = pthread_self(); return 0; }
    if (pthread_equal(mx[m].owner, pthread_self())) { mx[m].depth++; return 0; }
    return 1;
}
s32 LWP_MutexUnlock(mutex_t m) { if (m < NMX && mx[m].depth) mx[m].depth--; return 0; }
s32 LWP_MutexDestroy(mutex_t m) { if (m < NMX) mx[m].used = 0; return 0; }
/* files d'attente / semaphores / conditions : compteurs + attente cooperative */
#define NQ 256
static volatile u32 q_sig[NQ]; static int q_used[NQ];
s32 LWP_InitQueue(lwpq_t *q) { for (int i = 1; i < NQ; i++) if (!q_used[i]) { q_used[i] = 1; q_sig[i] = 0; *q = i; return 0; } return -1; }
void LWP_CloseQueue(lwpq_t q) { if (q < NQ) q_used[q] = 0; }
s32 LWP_ThreadSleep(lwpq_t q) { if (q >= NQ) return -1; u32 s = q_sig[q]; while (q_sig[q] == s) usleep(200); return 0; }
void LWP_ThreadSignal(lwpq_t q) { if (q < NQ) q_sig[q]++; }
void LWP_ThreadBroadcast(lwpq_t q) { if (q < NQ) q_sig[q]++; }
static volatile s32 sem_v[NQ]; static int sem_used[NQ];
s32 LWP_SemInit(sem_t *s, u32 start, u32 max) { (void)max; for (int i = 1; i < NQ; i++) if (!sem_used[i]) { sem_used[i] = 1; sem_v[i] = (s32)start; *s = i; return 0; } return -1; }
s32 LWP_SemWait(sem_t s) { if (s >= NQ) return -1; while (sem_v[s] <= 0) usleep(200); sem_v[s]--; return 0; }
s32 LWP_SemPost(sem_t s) { if (s < NQ) sem_v[s]++; return 0; }
s32 LWP_SemDestroy(sem_t s) { if (s < NQ) sem_used[s] = 0; return 0; }
s32 LWP_CondInit(cond_t *c) { return LWP_InitQueue(c); }
s32 LWP_CondWait(cond_t c, mutex_t m) { u32 s = q_sig[c]; LWP_MutexUnlock(m); while (q_sig[c] == s) usleep(200); return LWP_MutexLock(m); }
s32 LWP_CondTimedWait(cond_t c, mutex_t m, const struct timespec *t) {
    u64 lim = gettime() + (t ? (u64)t->tv_sec * 1000 + (u64)t->tv_nsec / 1000000 : 0);
    u32 s = q_sig[c]; LWP_MutexUnlock(m);
    while (q_sig[c] == s && gettime() < lim) usleep(200);
    LWP_MutexLock(m);
    return q_sig[c] == s ? ETIMEDOUT : 0;
}
s32 LWP_CondSignal(cond_t c) { LWP_ThreadSignal(c); return 0; }
s32 LWP_CondBroadcast(cond_t c) { LWP_ThreadSignal(c); return 0; }

/* ======================= video ======================= */
GXRModeObj TVNtsc480IntDf = { VI_TVMODE_NTSC_INT, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, 0, 0, {{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}}, {0,0,21,22,21,0,0} };
GXRModeObj TVNtsc480Prog = { VI_TVMODE_NTSC_PROG, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_SF, 0, 0, {{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}}, {0,0,21,22,21,0,0} };
GXRModeObj TVEurgb60Hz480IntDf = { VI_TVMODE_EURGB60_INT, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, 0, 0, {{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}}, {0,0,21,22,21,0,0} };
GXRModeObj TVEurgb60Hz480Prog = { VI_TVMODE_EURGB60_PROG, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_SF, 0, 0, {{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}}, {0,0,21,22,21,0,0} };
GXRModeObj TVPal528IntDf = { VI_TVMODE_PAL_INT, 640, 528, 528, 40, 23, 640, 528, VI_XFBMODE_DF, 0, 0, {{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}}, {0,0,21,22,21,0,0} };
GXRModeObj TVPal576IntDfScale = { VI_TVMODE_PAL_INT, 640, 480, 576, 40, 0, 640, 576, VI_XFBMODE_DF, 0, 0, {{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}}, {0,0,21,22,21,0,0} };
GXRModeObj TVPal576ProgScale = { VI_TVMODE(VI_PAL, VI_PROGRESSIVE), 640, 480, 576, 40, 0, 640, 576, VI_XFBMODE_SF, 0, 0, {{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}}, {0,0,21,22,21,0,0} };
GXRModeObj TVMpal480IntDf = { VI_TVMODE(VI_MPAL, VI_INTERLACE), 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, 0, 0, {{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6},{6,6}}, {0,0,21,22,21,0,0} };
GXRModeObj TVPal524IntAa, TVNtsc480IntAa;
static GXRModeObj *vi_mode = &TVEurgb60Hz480IntDf;
static void *vi_next_fb = NULL, *vi_cur_fb = NULL;
void VIDEO_Init(void) {}
void VIDEO_Configure(GXRModeObj *m) { if (m) vi_mode = m; }
void VIDEO_Flush(void) { vi_cur_fb = vi_next_fb; }
void VIDEO_WaitVSync(void) { u32 n = vsync_n; while (vsync_n == n) usleep(500); }
void VIDEO_SetBlack(bool b) { (void)b; }
void VIDEO_SetNextFramebuffer(void *fb) { vi_next_fb = fb; }
void VIDEO_ClearFrameBuffer(GXRModeObj *m, void *fb, u32 color) { (void)m; (void)fb; (void)color; }
GXRModeObj *VIDEO_GetPreferredMode(GXRModeObj *m) { (void)m; return getenv("PCSIM_PAL") ? &TVPal528IntDf : &TVEurgb60Hz480IntDf; }
u32 VIDEO_GetCurrentTvMode(void) { return getenv("PCSIM_PAL") ? VI_PAL : VI_EURGB60; }
u32 VIDEO_HaveComponentCable(void) { return 0; }
u32 VIDEO_GetNextField(void) { return vsync_n & 1; }
u32 VIDEO_GetCurrentLine(void) { return 0; }
VIRetraceCallback VIDEO_SetPostRetraceCallback(VIRetraceCallback cb) { VIRetraceCallback o = post_cb; post_cb = cb; return o; }
VIRetraceCallback VIDEO_SetPreRetraceCallback(VIRetraceCallback cb) { VIRetraceCallback o = pre_cb; pre_cb = cb; return o; }
void *VIDEO_GetCurrentFramebuffer(void) { return vi_cur_fb; }
void *VIDEO_GetNextFramebuffer(void) { return vi_next_fb; }
u32 VIDEO_GetFrameBufferSize(GXRModeObj *m) { return (u32)m->fbWidth * m->xfbHeight * 2; }
void CON_Init(void *fb, int x, int y, int w, int h, int stride) { (void)fb; (void)x; (void)y; (void)w; (void)h; (void)stride; }
s32 CON_InitEx(GXRModeObj *m, s32 x, s32 y, s32 w, s32 h) { (void)m; (void)x; (void)y; (void)w; (void)h; return 0; }
void CON_GetMetrics(int *c, int *r) { if (c) *c = 80; if (r) *r = 25; }
void CON_GetPosition(int *c, int *r) { if (c) *c = 0; if (r) *r = 0; }
/* ecran a 60 Hz : rappels de balayage (comme l'interruption de la console) */
static void *vsync_thread(void *a) {
    (void)a;
    u64 next = real_us();
    for (;;) {
        next += 16667;
        u64 now = real_us();
        if (next > now) { struct timespec ts = { 0, (long)(next - now) * 1000 }; nanosleep(&ts, NULL); }
        else next = now;
        vsync_n++;
        if (pre_cb) pre_cb(vsync_n);
        if (post_cb) post_cb(vsync_n);
    }
    return NULL;
}

/* ======================= son ======================= */
static AIDCallback ai_cb = NULL;
static volatile u32 ai_start = 0, ai_len = 0, ai_on = 0, ai_rate = 48000;
static FILE *wav = NULL; static u32 wav_bytes = 0;
static void wav_header(void) {
    if (!wav) return;
    u8 h[44]; u32 sr = ai_rate, br = sr * 4;
    memcpy(h, "RIFF", 4); u32 v = 36 + wav_bytes; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4); u16 w = 1; memcpy(h + 20, &w, 2); w = 2; memcpy(h + 22, &w, 2);
    memcpy(h + 24, &sr, 4); memcpy(h + 28, &br, 4); w = 4; memcpy(h + 32, &w, 2); w = 16; memcpy(h + 34, &w, 2);
    memcpy(h + 36, "data", 4); memcpy(h + 40, &wav_bytes, 4);
    long pos = ftell(wav); fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fseek(wav, pos, SEEK_SET);
}
static void *audio_thread(void *a) {
    (void)a;
    const char *wp = getenv("PCSIM_WAV");
    if (wp) {                                              /* (cree le dossier du fichier si besoin) */
        char dir[512]; snprintf(dir, sizeof(dir), "%s", wp); char *sl = strrchr(dir, '/'); if (sl) { *sl = 0; mkdir(dir, 0777); }
    }
    if (wp) { wav = fopen(wp, "wb"); if (wav) { wav_header(); fseek(wav, 44, SEEK_SET); } }
    u64 next = real_us();
    for (;;) {
        u32 len = ai_len ? ai_len : 4096;
        next += (u64)len * 1000000ull / ((u64)ai_rate * 4);
        u64 now = real_us();
        if (next > now) { struct timespec ts = { 0, (long)(next - now) * 1000 }; nanosleep(&ts, NULL); }
        else next = now;
        if (!ai_on) continue;
        pthread_mutex_lock(&irq_m);                         /* (interruption du son : pas le verrou global) */
        if (wav && ai_start && ai_len) {                    /* (s16 ecrits par le programme : deja dans l'ordre du PC) */
            u32 n = ai_len;
            fwrite((const void *)(uintptr_t)ai_start, 1, n, wav); wav_bytes += n;
            if ((wav_bytes & 0xFFFF) < n) { wav_header(); fflush(wav); }
        }
        if (ai_cb) ai_cb();
        pthread_mutex_unlock(&irq_m);
    }
    return NULL;
}
void AUDIO_Init(u8 *stack) { (void)stack; }
void AUDIO_InitDMA(u32 start, u32 len) { ai_start = start; ai_len = len; }
void AUDIO_StartDMA(void) { ai_on = 1; }
void AUDIO_StopDMA(void) { ai_on = 0; }
u32 AUDIO_GetDMABytesLeft(void) { return 0; }
u32 AUDIO_GetDMAEnableFlag(void) { return ai_on; }
u32 AUDIO_GetDMAStartAddr(void) { return ai_start; }
u32 AUDIO_GetDMALength(void) { return ai_len; }
AIDCallback AUDIO_RegisterDMACallback(AIDCallback cb) { AIDCallback o = ai_cb; ai_cb = cb; return o; }
void AUDIO_SetDSPSampleRate(u8 r) { ai_rate = r == AI_SAMPLERATE_32KHZ ? 32000 : 48000; }
u32 AUDIO_GetDSPSampleRate(void) { return ai_rate == 32000 ? AI_SAMPLERATE_32KHZ : AI_SAMPLERATE_48KHZ; }
void AUDIO_SetStreamVolLeft(u8 v) { (void)v; }
void AUDIO_SetStreamVolRight(u8 v) { (void)v; }
void AUDIO_SetStreamSampleRate(u32 r) { (void)r; }
void AUDIO_SetStreamPlayState(u32 s) { (void)s; }

/* ======================= manettes (scenario) ======================= */
/* Scenario (PCSIM_SCRIPT) : une commande par ligne, # = commentaire.
     wait 2s | wait 500ms | wait 30f      attendre (secondes, millisecondes, images)
     press A [n]                          appuyer (n images, 3 par defaut), puis relacher 3 images
     hold A 2s                            maintenir
     combo A+Z+START [n]                  plusieurs boutons a la fois
     stick main|c X Y 1s                  stick principal ou C (-128..127)
     shot nom                             capture de l'image suivante -> PCSIM_OUT/nom.png
     log texte                            ecrit dans la sortie
     quit                                 fin (sinon : fin du scenario = fin)
   Boutons : A B X Y Z L R START UP DOWN LEFT RIGHT. */
typedef struct { int op; u32 btn; int a, b; u32 dur; char s[96]; } sc_cmd;
enum { SC_WAIT_MS, SC_WAIT_F, SC_PRESS, SC_HOLD, SC_STICK, SC_CSTICK, SC_SHOT, SC_LOG, SC_QUIT, SC_MARK };
void pcsim_seg_report(const char *name);
static sc_cmd *sc = NULL; static int sc_n = 0, sc_i = 0;
static u32 sc_scan = 0;
static u16 pad_held = 0, pad_prev = 0, pad_down = 0, pad_up = 0;
static s8 st_x = 0, st_y = 0, cst_x = 0, cst_y = 0;
static int sc_phase = 0;                                   /* press : 1 = appuye, 2 = relache */
int pcsim_capture = 0;                                     /* image a dessiner et enregistrer */
char pcsim_capture_name[96];
static u32 btn_of(const char *s) {
    u32 b = 0; char tmp[96]; snprintf(tmp, sizeof(tmp), "%s", s);
    for (char *t = strtok(tmp, "+"); t; t = strtok(NULL, "+")) {
        static const struct { const char *n; u32 v; } k[] = { {"A",PAD_BUTTON_A},{"B",PAD_BUTTON_B},{"X",PAD_BUTTON_X},{"Y",PAD_BUTTON_Y},{"Z",PAD_TRIGGER_Z},
            {"L",PAD_TRIGGER_L},{"R",PAD_TRIGGER_R},{"START",PAD_BUTTON_START},{"UP",PAD_BUTTON_UP},{"DOWN",PAD_BUTTON_DOWN},{"LEFT",PAD_BUTTON_LEFT},{"RIGHT",PAD_BUTTON_RIGHT} };
        int f = 0;
        for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) if (!strcasecmp(t, k[i].n)) { b |= k[i].v; f = 1; }
        if (!f) fprintf(stderr, "[pcsim] bouton inconnu : %s\n", t);
    }
    return b;
}
static u32 dur_of(const char *s, u32 def_frames) {          /* -> millisecondes (ou images si 'f', bit 31) */
    if (!s || !*s) return 0x80000000u | def_frames;
    double v = atof(s); const char *u = s; while (*u && (isdigit((unsigned char)*u) || *u == '.')) u++;
    if (!strcmp(u, "f")) return 0x80000000u | (u32)v;
    if (!strcmp(u, "ms")) return (u32)v;
    return (u32)(v * 1000.0);
}
static void script_load(void) {
    const char *p = getenv("PCSIM_SCRIPT");
    if (!p) return;
    FILE *f = fopen(p, "r");
    if (!f) { fprintf(stderr, "[pcsim] scenario introuvable : %s\n", p); exit(2); }
    char l[256];
    while (fgets(l, sizeof(l), f)) {
        char *h = strchr(l, '#'); if (h) *h = 0;
        char w[4][96] = { "", "", "", "" };
        int n = sscanf(l, "%95s %95s %95s %95s", w[0], w[1], w[2], w[3]);
        if (n <= 0) continue;
        { void *__real_realloc(void *, size_t); sc = __real_realloc(sc, sizeof(sc_cmd) * (sc_n + 1)); }   /* (memoire du simulateur) */
        sc_cmd *c = &sc[sc_n]; memset(c, 0, sizeof(*c));
        if (!strcasecmp(w[0], "wait")) { u32 d = dur_of(w[1], 1); c->op = d & 0x80000000u ? SC_WAIT_F : SC_WAIT_MS; c->dur = d & 0x7FFFFFFFu; }
        else if (!strcasecmp(w[0], "press") || !strcasecmp(w[0], "combo")) { c->op = SC_PRESS; c->btn = btn_of(w[1]); c->dur = n > 2 ? (u32)atoi(w[2]) : 3; }
        else if (!strcasecmp(w[0], "hold")) { c->op = SC_HOLD; c->btn = btn_of(w[1]); c->dur = dur_of(w[2], 30); }
        else if (!strcasecmp(w[0], "stick")) { c->op = strcasecmp(w[1], "c") ? SC_STICK : SC_CSTICK; c->a = atoi(w[2]); c->b = atoi(w[3]); char d[32] = "", z[4][96]; sscanf(l, "%95s %95s %95s %95s %31s", z[0], z[1], z[2], z[3], d); c->dur = dur_of(d, 10); }   /* (v84 : la duree est le 5e mot) */
        else if (!strcasecmp(w[0], "shot")) { c->op = SC_SHOT; snprintf(c->s, sizeof(c->s), "%s", w[1]); }
        else if (!strcasecmp(w[0], "log")) { c->op = SC_LOG; char *q = strstr(l, w[0]) + 3; while (*q == ' ') q++; snprintf(c->s, sizeof(c->s), "%s", q); c->s[strcspn(c->s, "\r\n")] = 0; }
        else if (!strcasecmp(w[0], "quit")) c->op = SC_QUIT;
        else if (!strcasecmp(w[0], "mark")) { c->op = SC_MARK; snprintf(c->s, sizeof(c->s), "%s", w[1]); }
        else { fprintf(stderr, "[pcsim] commande inconnue : %s\n", w[0]); continue; }
        sc_n++;
    }
    fclose(f);
    printf("[pcsim] scenario : %d commandes\n", sc_n);
}
static int dur_done(u32 d, u64 t0, u32 f0) {               /* duree ecoulee ? (ms, ou images si bit 31) */
    if (d & 0x80000000u) return sc_scan - f0 >= (d & 0x7FFFFFFFu);
    return gettime() - t0 >= d;
}
/* avance le scenario : appele a chaque lecture des manettes (une fois par tour de boucle) */
static u64 sc_t0 = 0; static u32 sc_f0 = 0; static int sc_started = 0;
void gcs_perf_sim(int on) __attribute__((weak));
static void script_step(void) {
    sc_scan++;
    { static int pf = 0; if (!pf && getenv("PCSIM_PERF") && gcs_perf_sim) { pf = 1; gcs_perf_sim(1); } }
    if (!sc || pcsim_capture) return;                      /* (capture en cours : le scenario attend) */
    for (int guard = 0; guard < 64; guard++) {
        if (sc_i >= sc_n) { if (!pcsim_capture) { printf("[pcsim] fin du scenario\n"); pcsim_finish(0); } return; }
        sc_cmd *c = &sc[sc_i];
        if (!sc_started) { sc_started = 1; sc_t0 = gettime(); sc_f0 = sc_scan; sc_phase = 1; }
        int next = 0;
        switch (c->op) {
        case SC_WAIT_MS: next = gettime() - sc_t0 >= c->dur; break;
        case SC_WAIT_F: next = sc_scan - sc_f0 >= c->dur; break;
        case SC_PRESS:
            if (sc_phase == 1) { pad_held = (u16)c->btn; if (sc_scan - sc_f0 >= c->dur) { sc_phase = 2; sc_f0 = sc_scan; pad_held = 0; } }
            else { pad_held = 0; next = sc_scan - sc_f0 >= 3; }
            break;
        case SC_HOLD: pad_held = (u16)c->btn; if (dur_done(c->dur, sc_t0, sc_f0)) { pad_held = 0; next = 1; } break;
        case SC_STICK: st_x = (s8)c->a; st_y = (s8)c->b; if (dur_done(c->dur, sc_t0, sc_f0)) { st_x = st_y = 0; next = 1; } break;
        case SC_CSTICK: cst_x = (s8)c->a; cst_y = (s8)c->b; if (dur_done(c->dur, sc_t0, sc_f0)) { cst_x = cst_y = 0; next = 1; } break;
        case SC_SHOT:
            if (!pcsim_capture) { snprintf(pcsim_capture_name, sizeof(pcsim_capture_name), "%s", c->s); pcsim_capture = 1; }
            next = 1; break;
        case SC_LOG: printf("[scenario] %s\n", c->s); next = 1; break;
        case SC_QUIT: printf("[pcsim] quit\n"); pcsim_finish(0); return;
        case SC_MARK: pcsim_seg_report(c->s); next = 1; break;
        }
        if (!next) return;
        sc_i++; sc_started = 0;
        if (c->op == SC_SHOT) return;                     /* (la capture prend l'image de ce tour) */
    }
}
const char *pcsim_script_where(void) {
    static char b[128];
    if (!sc || !sc_n) return "pas de scenario";
    int i = sc_i > 0 ? sc_i - 1 : 0;
    static const char *const nm[] = { "wait", "wait", "press", "hold", "stick", "stick c", "shot", "log", "quit", "mark" };
    sc_cmd *c = &sc[i];
    if (c->op == SC_SHOT || c->op == SC_LOG) snprintf(b, sizeof(b), "apres %s %s", nm[c->op], c->s);
    else if (c->op == SC_PRESS || c->op == SC_HOLD) {
        const char *bn = c->btn & PAD_TRIGGER_Z ? "Z" : c->btn & PAD_BUTTON_A ? "A" : c->btn & PAD_BUTTON_B ? "B" : c->btn & PAD_BUTTON_START ? "START" : "...";
        snprintf(b, sizeof(b), "apres %s %s, ligne %d", nm[c->op], bn, i + 1);
    } else snprintf(b, sizeof(b), "apres %s, ligne %d", nm[c->op], i + 1);
    return b;
}
u32 PAD_Init(void) { return 1; }
u32 PAD_ScanPads(void) {
    script_step();
    pad_down = pad_held & ~pad_prev; pad_up = pad_prev & ~pad_held; pad_prev = pad_held;
    return 1;                                              /* (manette 1 branchee) */
}
u32 PAD_Read(PADStatus *s) { memset(s, 0, sizeof(PADStatus) * 4); s[0].button = pad_held; s[0].stickX = st_x; s[0].stickY = st_y; s[0].substickX = cst_x; s[0].substickY = cst_y; for (int i = 1; i < 4; i++) s[i].err = PAD_ERR_NO_CONTROLLER; return 1; }
u32 PAD_Reset(u32 m) { (void)m; return 1; }
u32 PAD_Recalibrate(u32 m) { (void)m; return 1; }
u16 PAD_ButtonsUp(int p) { return p ? 0 : pad_up; }
u16 PAD_ButtonsDown(int p) { return p ? 0 : pad_down; }
u16 PAD_ButtonsHeld(int p) { return p ? 0 : pad_held; }
s8 PAD_StickX(int p) { return p ? 0 : st_x; }
s8 PAD_StickY(int p) { return p ? 0 : st_y; }
s8 PAD_SubStickX(int p) { return p ? 0 : cst_x; }
s8 PAD_SubStickY(int p) { return p ? 0 : cst_y; }
u8 PAD_TriggerL(int p) { return p ? 0 : (pad_held & PAD_TRIGGER_L) ? 200 : 0; }
u8 PAD_TriggerR(int p) { return p ? 0 : (pad_held & PAD_TRIGGER_R) ? 200 : 0; }
void PAD_ControlMotor(s32 chan, u32 cmd) { (void)chan; (void)cmd; }
void PAD_Clamp(PADStatus *s) { (void)s; }
/* SI : une manette en 1, rien ailleurs (pas de clavier GameCube) */
u32 SI_GetType(s32 c) { return c == 0 ? SI_GC_CONTROLLER : SI_ERROR_NO_RESPONSE; }
u32 SI_GetTypeAsync(s32 c, SICallback cb) { u32 t = SI_GetType(c); if (cb) cb(c, t); return t; }
u32 SI_DecodeType(u32 t) { return t & 0xFFFFFF00u; }
u32 SI_Transfer(s32 c, void *o, u32 ol, void *i, u32 il, SICallback cb, u32 d) { (void)c; (void)o; (void)ol; (void)d; if (i) memset(i, 0, il); if (cb) cb(c, 0); return 0; }
u32 SI_Sync(void) { return 0; }
u32 SI_GetStatus(s32 c) { (void)c; return 0; }
void SI_SetCommand(s32 c, u32 cmd) { (void)c; (void)cmd; }
void SI_EnablePolling(u32 p) { (void)p; }
void SI_DisablePolling(u32 p) { (void)p; }
u32 SI_IsChanBusy(s32 c) { (void)c; return 0; }
void SI_TransferCommands(void) {}
u32 SI_GetResponse(s32 c, void *b) { (void)c; if (b) memset(b, 0, 8); return 0; }

/* ======================= carte memoire : absente ======================= */
s32 CARD_Init(const char *g, const char *c) { (void)g; (void)c; return CARD_ERROR_READY; }
s32 CARD_Mount(s32 chn, void *w, cardcallback cb) { (void)chn; (void)w; (void)cb; return CARD_ERROR_NOCARD; }
s32 CARD_Unmount(s32 chn) { (void)chn; return CARD_ERROR_READY; }
s32 CARD_Probe(s32 chn) { (void)chn; return 0; }
s32 CARD_ProbeEx(s32 chn, s32 *m, s32 *s) { (void)chn; if (m) *m = 0; if (s) *s = 0; return CARD_ERROR_NOCARD; }
s32 CARD_Open(s32 chn, const char *n, card_file *f) { (void)chn; (void)n; (void)f; return CARD_ERROR_NOCARD; }
s32 CARD_OpenEntry(s32 chn, card_dir *e, card_file *f) { (void)chn; (void)e; (void)f; return CARD_ERROR_NOCARD; }
s32 CARD_Close(card_file *f) { (void)f; return CARD_ERROR_READY; }
s32 CARD_Create(s32 chn, const char *n, u32 s, card_file *f) { (void)chn; (void)n; (void)s; (void)f; return CARD_ERROR_NOCARD; }
s32 CARD_Read(card_file *f, void *b, u32 l, u32 o) { (void)f; (void)b; (void)l; (void)o; return CARD_ERROR_NOCARD; }
s32 CARD_Write(card_file *f, void *b, u32 l, u32 o) { (void)f; (void)b; (void)l; (void)o; return CARD_ERROR_NOCARD; }
s32 CARD_Delete(s32 chn, const char *n) { (void)chn; (void)n; return CARD_ERROR_NOCARD; }
s32 CARD_DeleteEntry(s32 chn, card_dir *d) { (void)chn; (void)d; return CARD_ERROR_NOCARD; }
s32 CARD_FindFirst(s32 chn, card_dir *d, bool all) { (void)chn; (void)d; (void)all; return CARD_ERROR_NOFILE; }
s32 CARD_FindNext(card_dir *d) { (void)d; return CARD_ERROR_NOFILE; }
s32 CARD_GetStatus(s32 chn, s32 n, card_stat *s) { (void)chn; (void)n; (void)s; return CARD_ERROR_NOCARD; }
s32 CARD_SetStatus(s32 chn, s32 n, card_stat *s) { (void)chn; (void)n; (void)s; return CARD_ERROR_NOCARD; }
s32 CARD_GetStatusEx(s32 chn, s32 n, card_direntry *e) { (void)chn; (void)n; (void)e; return CARD_ERROR_NOCARD; }
s32 CARD_SetStatusEx(s32 chn, s32 n, card_direntry *e) { (void)chn; (void)n; (void)e; return CARD_ERROR_NOCARD; }
s32 CARD_GetSectorSize(s32 chn, u32 *s) { (void)chn; if (s) *s = 8192; return CARD_ERROR_NOCARD; }
s32 CARD_GetBlockCount(s32 chn, u32 *b) { (void)chn; if (b) *b = 0; return CARD_ERROR_NOCARD; }
s32 CARD_GetFreeBlocks(s32 chn, u16 *f) { (void)chn; if (f) *f = 0; return CARD_ERROR_NOCARD; }
s32 CARD_GetMemSize(s32 chn, u16 *m) { (void)chn; if (m) *m = 0; return CARD_ERROR_NOCARD; }
s32 CARD_SetCompany(const char *c) { (void)c; return 0; }
s32 CARD_SetGamecode(const char *g) { (void)g; return 0; }
s32 CARD_Format(s32 chn) { (void)chn; return CARD_ERROR_NOCARD; }
s32 CARD_GetSerialNo(s32 chn, u32 *a, u32 *b) { (void)chn; if (a) *a = 0; if (b) *b = 0; return CARD_ERROR_NOCARD; }
/* EXI : rien de branche (lanceur maison et pilotes bas niveau : pas sur PC) */
s32 EXI_Lock(s32 c, s32 d, void *cb) { (void)c; (void)d; (void)cb; return 0; }
s32 EXI_Unlock(s32 c) { (void)c; return 1; }
s32 EXI_Select(s32 c, s32 d, s32 f) { (void)c; (void)d; (void)f; return 0; }
s32 EXI_Deselect(s32 c) { (void)c; return 1; }
s32 EXI_Imm(s32 c, void *p, u32 l, u32 m, void *cb) { (void)c; (void)p; (void)l; (void)m; (void)cb; return 0; }
s32 EXI_ImmEx(s32 c, void *p, u32 l, u32 m) { (void)c; (void)p; (void)l; (void)m; return 0; }
s32 EXI_Sync(s32 c) { (void)c; return 1; }
s32 EXI_Dma(s32 c, void *p, u32 l, u32 m, void *cb) { (void)c; (void)p; (void)l; (void)m; (void)cb; return 0; }
s32 EXI_Probe(s32 c) { (void)c; return 0; }
s32 EXI_ProbeEx(s32 c) { (void)c; return 0; }
s32 EXI_GetID(s32 c, s32 d, u32 *id) { (void)c; (void)d; if (id) *id = 0; return 0; }
s32 EXI_Attach(s32 c, void *cb) { (void)c; (void)cb; return 0; }
s32 EXI_Detach(s32 c) { (void)c; return 1; }

/* ======================= carte SD : le dossier "sd:" ======================= */
static bool sd_startup(void) { struct stat st; return stat("sd:", &st) == 0; }
static bool sd_inserted(void) { return sd_startup(); }
static bool sd_read(sec_t s, sec_t n, void *b) { (void)s; (void)n; (void)b; return false; }   /* (pas de secteurs bruts sur PC) */
static bool sd_write(sec_t s, sec_t n, const void *b) { (void)s; (void)n; (void)b; return false; }
static bool sd_ok_fn(void) { return true; }
static bool sd_none(void) { return false; }
static bool sd_none_r(sec_t s, sec_t n, void *b) { (void)s; (void)n; (void)b; return false; }
static bool sd_none_w(sec_t s, sec_t n, const void *b) { (void)s; (void)n; (void)b; return false; }
const DISC_INTERFACE __io_gcsd2 = { DEVICE_TYPE_GAMECUBE_SD(2), 3, sd_startup, sd_inserted, sd_read, sd_write, sd_ok_fn, sd_ok_fn };
const DISC_INTERFACE __io_gcsda = { DEVICE_TYPE_GAMECUBE_SD(0), 3, sd_none, sd_none, sd_none_r, sd_none_w, sd_ok_fn, sd_ok_fn };
const DISC_INTERFACE __io_gcsdb = { DEVICE_TYPE_GAMECUBE_SD(1), 3, sd_none, sd_none, sd_none_r, sd_none_w, sd_ok_fn, sd_ok_fn };
bool fatInitDefault(void) { return sd_startup(); }
bool fatMountSimple(const char *name, const DISC_INTERFACE *i) { (void)name; return i && i->startup(); }
bool fatMount(const char *name, const DISC_INTERFACE *i, sec_t st, u32 c, u32 s) { (void)st; (void)c; (void)s; return fatMountSimple(name, i); }
void fatUnmount(const char *name) { (void)name; }
void fatGetVolumeLabel(const char *name, char *label) { (void)name; if (label) strcpy(label, "PCSIM"); }
int FAT_getAttr(const char *f) { (void)f; return 0; }
int FAT_setAttr(const char *f, uint8_t a) { (void)f; (void)a; return 0; }

/* ======================= reseau : vrais sockets ======================= */
#define NE(r) ((r) < 0 ? -errno : (r))
/* DNS : GC Station fait ses propres requetes DNS (UDP, port 53). Le simulateur y repond
   lui-meme, selon PCSIM_NET :
     proxy  (par defaut si HTTPS_PROXY existe) : adresse fictive 198.18.x.y, et la connexion
            passe par le proxy HTTPS (CONNECT nom:port) : marche dans les sessions cloud ;
     direct (sinon) : vraie adresse (getaddrinfo) ;
     off    : "nom inconnu" tout de suite (GC Station hors ligne, sans attente).
   PCSIM_HOSTS="nom=1.2.3.4,autre=127.0.0.1" : reponses imposees (serveurs de test locaux). */
static char dns_names[1024][128]; static int dns_n = 0;
static struct { int fd, len; u8 buf[512]; } dns_slot[16];
static int net_mode(void) {
    const char *m = getenv("PCSIM_NET");
    if (m && !strcmp(m, "off")) return 0;
    if (m && !strcmp(m, "direct")) return 1;
    if (m && !strcmp(m, "proxy")) return 2;
    return getenv("HTTPS_PROXY") || getenv("https_proxy") ? 2 : 1;
}
static int dns_answer(const char *name, u32 *ip) {          /* 1 = adresse (ordre reseau), 0 = inconnu */
    const char *h = getenv("PCSIM_HOSTS");
    if (h) {
        char tmp[1024]; snprintf(tmp, sizeof(tmp), "%s", h);
        for (char *e = strtok(tmp, ","); e; e = strtok(NULL, ",")) {
            char *eq = strchr(e, '='); if (!eq) continue; *eq = 0;
            if (!strcasecmp(e, name)) { *ip = inet_addr(eq + 1); return 1; }
        }
    }
    if (!strcasecmp(name, "localhost")) { *ip = htonl(0x7F000001); return 1; }
    int m = net_mode();
    if (m == 0) return 0;
    if (m == 1) {
        struct addrinfo hints, *r = NULL; memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_INET;
        int e = BLOCKING(getaddrinfo(name, NULL, &hints, &r));
        if (e || !r) return 0;
        *ip = ((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr; freeaddrinfo(r); return 1;
    }
    int i;
    for (i = 0; i < dns_n; i++) if (!strcasecmp(dns_names[i], name)) break;
    if (i == dns_n) { if (dns_n >= 1024) return 0; snprintf(dns_names[dns_n++], 128, "%s", name); }
    *ip = htonl(0xC6120000u | (u32)(i + 1));                 /* 198.18.0.0/15 : reseau de test */
    return 1;
}
static int dns_fake(s32 s, const u8 *q, int ql) {            /* requete DNS du programme -> reponse prete */
    if (ql < 17) return 0;
    char name[128]; int o = 12, n = 0;
    while (o < ql && q[o]) { int l = q[o++]; if (o + l > ql || n + l + 1 >= (int)sizeof(name)) return 0; if (n) name[n++] = '.'; memcpy(name + n, q + o, l); n += l; o += l; }
    name[n] = 0; o += 5;                                     /* (fin du nom, type, classe) */
    if (o > ql) return 0;
    u32 ip = 0; int ok = dns_answer(name, &ip);
    int k; for (k = 0; k < 16 && dns_slot[k].len && dns_slot[k].fd != s; k++) ;
    if (k == 16) k = 0;
    u8 *r = dns_slot[k].buf; memcpy(r, q, o);
    r[2] = 0x81; r[3] = ok ? 0x80 : 0x83; r[6] = 0; r[7] = ok ? 1 : 0; r[8] = r[9] = r[10] = r[11] = 0;
    int rl = o;
    if (ok) { u8 an[16] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4 }; memcpy(an + 12, &ip, 4); memcpy(r + rl, an, 16); rl += 16; }
    dns_slot[k].fd = s; dns_slot[k].len = rl;
    if (getenv("PCSIM_NETLOG")) printf("[pcsim] DNS %s -> %s\n", name, ok ? inet_ntoa(*(struct in_addr *)&ip) : "inconnu");
    return 1;
}
static char denied[64][128]; static int n_denied = 0;    /* (sites refuses par le proxy : refus immediat ensuite) */
static int proxy_connect(s32 s, const char *host, int port) {   /* tunnel CONNECT par le proxy HTTPS */
    for (int i = 0; i < n_denied; i++) if (!strcasecmp(denied[i], host)) return -ECONNREFUSED;
    const char *p = getenv("HTTPS_PROXY"); if (!p) p = getenv("https_proxy"); if (!p) return -ECONNREFUSED;
    if (!strncmp(p, "http://", 7)) p += 7;
    char ph[128]; snprintf(ph, sizeof(ph), "%s", p); char *c = strchr(ph, ':'); int pp = 80;
    if (c) { *c = 0; pp = atoi(c + 1); } char *sl = strchr(c ? c + 1 : ph, '/'); if (sl) *sl = 0;
    struct sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET; a.sin_port = htons(pp); a.sin_addr.s_addr = inet_addr(ph);
    int fl = fcntl(s, F_GETFL); fcntl(s, F_SETFL, fl & ~O_NONBLOCK);
    int r = BLOCKING(connect(s, (struct sockaddr *)&a, sizeof(a)));
    if (r < 0) { fcntl(s, F_SETFL, fl); return -ECONNREFUSED; }
    char req[300]; int n = snprintf(req, sizeof(req), "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n\r\n", host, port, host, port);
    BLOCKING((int)send(s, req, n, MSG_NOSIGNAL));
    char resp[1024]; int got = 0;
    struct timeval tv = { 15, 0 }; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (got < (int)sizeof(resp) - 1) {                      /* (octet par octet : rien de plus que l'en-tete) */
        int k = BLOCKING((int)recv(s, resp + got, 1, 0));
        if (k <= 0) break;
        got++; resp[got] = 0;
        if (got >= 4 && !memcmp(resp + got - 4, "\r\n\r\n", 4)) break;
    }
    struct timeval z = { 0, 0 }; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &z, sizeof(z));
    fcntl(s, F_SETFL, fl);
    int ok = got > 12 && !strncmp(resp, "HTTP/1.", 7) && !strncmp(resp + 9, "200", 3);
    if (getenv("PCSIM_NETLOG")) printf("[pcsim] proxy %s:%d -> %.12s\n", host, port, got > 12 ? resp + 9 : "pas de reponse");
    if (!ok && got > 12 && n_denied < 64) snprintf(denied[n_denied++], 128, "%s", host);
    return ok ? 0 : -ECONNREFUSED;
}
s32 net_init(void) { return 0; }
s32 net_init_async(void *cb, void *usr) { (void)cb; (void)usr; return 0; }
void net_deinit(void) {}
s32 net_socket(u32 d, u32 t, u32 p) { int s = socket((int)d, (int)t, (int)p); if (s >= 0) { int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)); } return NE(s); }
s32 net_close(s32 s) { for (int k = 0; k < 16; k++) if (dns_slot[k].fd == s) dns_slot[k].len = 0; return NE(close(s)); }
s32 net_connect(s32 s, struct sockaddr *a, socklen_t l) {
    if (a->sa_family == AF_INET) {
        u32 ip = ntohl(((struct sockaddr_in *)a)->sin_addr.s_addr);
        if ((ip & 0xFFFE0000u) == 0xC6120000u) {               /* adresse fictive : par le proxy */
            u32 i = (ip & 0x1FFFF) - 1;
            if (i >= (u32)dns_n) return -EHOSTUNREACH;
            return proxy_connect(s, dns_names[i], ntohs(((struct sockaddr_in *)a)->sin_port));
        }
    }
    int r = BLOCKING(connect(s, a, l)); return NE(r);
}
s32 net_bind(s32 s, struct sockaddr *a, socklen_t l) {
    int r = bind(s, a, l);
    if (r < 0 && errno == EACCES && a->sa_family == AF_INET && ntohs(((struct sockaddr_in *)a)->sin_port) < 1024) {   /* (port < 1024 sans droits : +8000) */
        struct sockaddr_in b = *(struct sockaddr_in *)a; b.sin_port = htons(ntohs(b.sin_port) + 8000);
        r = bind(s, (struct sockaddr *)&b, sizeof(b));
        if (r == 0) printf("[pcsim] port %d -> %d\n", ntohs(((struct sockaddr_in *)a)->sin_port), ntohs(b.sin_port));
    }
    return NE(r);
}
s32 net_listen(s32 s, u32 b) { return NE(listen(s, (int)b)); }
s32 net_accept(s32 s, struct sockaddr *a, socklen_t *l) { int r = BLOCKING(accept(s, a, l)); return NE(r); }
s32 net_send(s32 s, const void *d, s32 l, u32 f) { int r = BLOCKING((int)send(s, d, l, (int)f | MSG_NOSIGNAL)); return NE(r); }
s32 net_recv(s32 s, void *d, s32 l, u32 f) {               /* (debit du Broadband Adapter : pcsim_limits.c) */
    int ok;
    while ((ok = pcsim_net_allow(l)) <= 0) {
        if ((f & MSG_DONTWAIT) || (fcntl(s, F_GETFL) & O_NONBLOCK)) return -EAGAIN;
        usleep(2000);
    }
    int r = BLOCKING((int)recv(s, d, ok, (int)f));
    if (r > 0) pcsim_net_used(r);
    return NE(r);
}
s32 net_write(s32 s, const void *d, s32 l) { return net_send(s, d, l, 0); }
s32 net_read(s32 s, void *d, s32 l) { return net_recv(s, d, l, 0); }
s32 net_sendto(s32 s, const void *d, s32 l, u32 f, struct sockaddr *a, socklen_t al) {
    if (a && a->sa_family == AF_INET && ntohs(((struct sockaddr_in *)a)->sin_port) == 53 && dns_fake(s, d, l)) return l;   /* (DNS : reponse du simulateur) */
    int r = BLOCKING((int)sendto(s, d, l, (int)f | MSG_NOSIGNAL, a, al)); return NE(r);
}
s32 net_recvfrom(s32 s, void *d, s32 l, u32 f, struct sockaddr *a, socklen_t *al) {
    for (int k = 0; k < 16; k++) if (dns_slot[k].len && dns_slot[k].fd == s) {
        int n = dns_slot[k].len < l ? dns_slot[k].len : l; memcpy(d, dns_slot[k].buf, n); dns_slot[k].len = 0; return n;
    }
    int r = BLOCKING((int)recvfrom(s, d, l, (int)f, a, al)); return NE(r);
}
s32 net_setsockopt(s32 s, u32 lv, u32 o, const void *v, socklen_t l) { return NE(setsockopt(s, (int)lv, (int)o, v, l)); }
s32 net_getsockopt(s32 s, u32 lv, u32 o, void *v, socklen_t *l) { return NE(getsockopt(s, (int)lv, (int)o, v, l)); }
s32 net_ioctl(s32 s, u32 cmd, void *arg) {
    if (cmd == (u32)FIONBIO) { int on = arg ? *(u32 *)arg != 0 : 0; return NE(ioctl(s, FIONBIO, &on)); }
    return NE(ioctl(s, cmd, arg));
}
s32 net_fcntl(s32 s, u32 cmd, u32 fl) { return NE(fcntl(s, (int)cmd, (int)fl)); }
s32 net_select(s32 m, fd_set *r, fd_set *w, fd_set *e, struct timeval *t) { int x = BLOCKING(select(m, r, w, e, t)); return NE(x); }
s32 net_poll(struct pollfd *f, u32 n, s32 t) { int x = BLOCKING(poll(f, n, t)); return NE(x); }
s32 net_shutdown(s32 s, u32 how) { return NE(shutdown(s, (int)how)); }
struct hostent *net_gethostbyname(const char *n) { return gethostbyname(n); }
u32 net_gethostip(void) { return htonl(0x7F000001); }
s32 if_config(char *ip, char *mask, char *gw, bool dhcp, int retries) {
    (void)dhcp; (void)retries;
    const char *e = getenv("PCSIM_NOIP");
    if (e) { if (ip) strcpy(ip, "0.0.0.0"); return -1; }
    if (ip) strcpy(ip, "127.0.0.1");
    if (mask) strcpy(mask, "255.0.0.0");
    if (gw) strcpy(gw, "127.0.0.1");
    return 0;
}
s32 if_configex(struct in_addr *ip, struct in_addr *mask, struct in_addr *gw, bool dhcp, int retries) {
    (void)dhcp; (void)retries;
    if (ip) ip->s_addr = htonl(0x7F000001);
    if (mask) mask->s_addr = htonl(0xFF000000);
    if (gw) gw->s_addr = htonl(0x7F000001);
    return 0;
}

/* ======================= fin ======================= */
void pcsim_fps_report(void);
void pcsim_gp_report(void);
void pcsim_finish(int code) {
    if (getenv("PCSIM_PERF") && gcs_perf_sim) gcs_perf_sim(0);   /* (rapport SD:/gc-mp3/fluidite.txt) */
    if (!pcsim_fast) printf("[pcsim] processeur : %.2f s de calcul sur le PC = %.1f s sur la GameCube (sur %.1f s)\n", cpu_host_ns / 1e9, cpu_host_ns * pcsim_cpu_k / 1e9, gettime() / 1000.0 - 1);
    if (!pcsim_fast) {
        printf("[pcsim] calcul par thread (priorite : secondes GameCube) :");
        for (int i = 127; i >= 0; i--) if (cpu_by_prio[i] > 1000000) printf("  %d : %.1f s", i, cpu_by_prio[i] * pcsim_cpu_k / 1e9);
        printf("  ; %llu interruptions par un thread prioritaire\n", (unsigned long long)n_preempt);
    }
    pcsim_limits_report();
    pcsim_fps_report();
    pcsim_gp_report();
    if (wav) { wav_header(); fclose(wav); wav = NULL; }
    fflush(stdout);
    _exit(code);
}

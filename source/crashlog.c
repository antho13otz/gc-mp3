#include <stdarg.h>
// Journal de plantage : quand GC-MP3 plante (exception du processeur) ou se
// bloque (la boucle principale ne tourne plus pendant 20 s), un rapport est
// ecrit dans sd:/gc-mp3/gc-mp3-crash.txt : type d'erreur, fonction en cause (grace a
// la table des symboles ajoutee a la compilation), pile d'appels, onglet,
// morceau ou radio en cours, memoire libre et les 16 dernieres actions.
// Au demarrage suivant, le fichier devient gc-mp3-crash-ancien.txt et un
// message le signale ; il est aussi lisible depuis le telephone (/crash.txt).

#define CRASH_FILE  GCD "/gc-mp3-crash.txt"
#define CRASH_OLD   GCD "/gc-mp3-crash-ancien.txt"
#define GCMP3_VERSION "v171"

#if defined(__has_include)
#if __has_include(<tuxedo/ppc/exception.h>) && __has_include(<tuxedo/thread.h>)
#include <tuxedo/ppc/exception.h>
#include <tuxedo/thread.h>
#define CRASH_HOOK 1
#endif
#endif

/* table des symboles (build/symtab.c, generee par tools/mksym.py) ; absente = adresses seules */
extern const unsigned gcmp3_symtab_n __attribute__((weak));
extern const unsigned gcmp3_sym_addr[] __attribute__((weak));
extern const unsigned gcmp3_sym_off[] __attribute__((weak));
extern const char gcmp3_sym_names[] __attribute__((weak));

/* ---- dernieres actions ---- */
#define NCRUMB 16
static char crumbs[NCRUMB][100];
static u32 crumb_t[NCRUMB];
static volatile u32 crumb_n = 0;
static u64 crash_boot_ms = 0;
static volatile u32 main_beat = 0;
static volatile u32 mem_free_kb = 0;
static u64 crash_notice_until = 0;

static void crumb(const char *fmt, ...) {
    u32 i = crumb_n % NCRUMB;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(crumbs[i], sizeof(crumbs[i]), fmt, ap);
    va_end(ap);
    crumb_t[i] = (u32)(now_ms() - crash_boot_ms);
    crumb_n++;
}

/* ---- symboles ---- */
#ifndef CRASH_CODE_MIN                                    /* (le simulateur PC change ces bornes) */
#define CRASH_CODE_MIN 0x80003000u
#define CRASH_CODE_MAX 0x81800000u
#define CRASH_DATA_MIN 0x80000000u
#define CRASH_DATA_MAX 0x817FFFF8u
#endif
static int code_addr_ok(u32 a) { return a >= CRASH_CODE_MIN && a < CRASH_CODE_MAX && !(a & 3); }
static int data_addr_ok(u32 a) { return a >= CRASH_DATA_MIN && a < CRASH_DATA_MAX && !(a & 3); }

static const char *sym_find(u32 a, u32 *off) {
    if (!&gcmp3_symtab_n || !gcmp3_sym_addr || !gcmp3_sym_off || !gcmp3_sym_names) return NULL;
    unsigned n = gcmp3_symtab_n;
    if (!n || a < gcmp3_sym_addr[0]) return NULL;
    unsigned lo = 0, hi = n - 1;
    while (lo < hi) {                                  /* plus grande adresse <= a */
        unsigned mid = (lo + hi + 1) / 2;
        if (gcmp3_sym_addr[mid] <= a) lo = mid; else hi = mid - 1;
    }
    if (a - gcmp3_sym_addr[lo] > 0x20000) return NULL;
    *off = a - gcmp3_sym_addr[lo];
    return gcmp3_sym_names + gcmp3_sym_off[lo];
}

/* ---- construction du rapport (sans malloc : utilisable pendant un plantage) ---- */
static char crash_txt[8192];
static int crash_len = 0;
static const char *crash_names[24];
static int crash_nnames = 0;

static void cr_add(const char *fmt, ...) {
    if (crash_len >= (int)sizeof(crash_txt) - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(crash_txt + crash_len, sizeof(crash_txt) - crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) crash_len += n;
    if (crash_len > (int)sizeof(crash_txt) - 1) crash_len = sizeof(crash_txt) - 1;
}

static void cr_addr(const char *label, u32 a) {
    u32 off = 0;
    const char *s = code_addr_ok(a) ? sym_find(a, &off) : NULL;
    if (s) {
        cr_add("  %-6s %08X  %s + 0x%X\n", label, (unsigned)a, s, (unsigned)off);
        if (crash_nnames < 24) crash_names[crash_nnames++] = s;
    } else cr_add("  %-6s %08X\n", label, (unsigned)a);
}

static void crash_build(const char *kind, u32 pc, u32 lr, u32 sp, int has_dar, u32 dar, const u32 *gpr) {
    crash_len = 0;
    crash_nnames = 0;
    u32 up = (u32)((now_ms() - crash_boot_ms) / 1000);
    cr_add("GC Station %s - journal de plantage\n", GCMP3_VERSION);
    cr_add("Type : %s\n", kind);
    cr_add("Depuis le demarrage : %u min %02u s\n", (unsigned)(up / 60), (unsigned)(up % 60));
    if (!&gcmp3_symtab_n || !gcmp3_symtab_n) cr_add("(pas de table des symboles : adresses seules)\n");
    cr_add("\nEndroit :\n");
    cr_addr("PC", pc);
    cr_addr("LR", lr);
    if (has_dar) cr_add("  DAR    %08X (adresse memoire en cause)\n", (unsigned)dar);
    cr_add("\nPile d'appels :\n");
    u32 p = sp;
    for (int i = 0; i < 14 && data_addr_ok(p); i++) {
        u32 next = *(volatile u32 *)p;
        u32 ret = *(volatile u32 *)(p + 4);
        if (code_addr_ok(ret)) { char l[8]; snprintf(l, sizeof(l), "#%d", i); cr_addr(l, ret); }
        if (next <= p) break;
        p = next;
    }
    cr_add("\nEtat :\n");
    cr_add("  onglet %s, source %s, lecture %d, pause %d, volume %d %%\n",
           (tab >= 0 && tab < TAB_COUNT) ? tab_names[tab] : "?",
           src == SRC_FILE ? "fichier" : src == SRC_RADIO ? "radio" : "aucune", playing, paused, volume_pct);
    if (src == SRC_FILE && cur_track >= 0 && cur_track < nfiles) cr_add("  morceau : %s\n", names[cur_track]);
    if (src == SRC_RADIO && cur_station >= 0 && cur_station < nstations) cr_add("  radio : %s\n", stations[cur_station].name);
    cr_add("  memoire libre : %u Ko\n", (unsigned)mem_free_kb);
    cr_add("\nDernieres actions (la plus recente en bas) :\n");
    u32 n = crumb_n, first = n > NCRUMB ? n - NCRUMB : 0;
    for (u32 k = first; k < n; k++) {
        u32 i = k % NCRUMB;
        cr_add("  %5u.%u s  %s\n", (unsigned)(crumb_t[i] / 1000), (unsigned)(crumb_t[i] % 1000 / 100), crumbs[i]);
    }
    if (gpr) {
        cr_add("\nRegistres :\n");
        for (int i = 0; i < 32; i += 4)
            cr_add("  r%-2d %08X  r%-2d %08X  r%-2d %08X  r%-2d %08X\n", i, (unsigned)gpr[i], i + 1, (unsigned)gpr[i + 1],
                   i + 2, (unsigned)gpr[i + 2], i + 3, (unsigned)gpr[i + 3]);
    }
}

/* la carte SD est-elle utilisable ? pas si le plantage a eu lieu pendant un
   acces a la carte ou dans l'allocateur memoire (on bloquerait la console) */
static int crash_sd_safe(void) {
    static const char *const bad[] = { "FAT", "fat", "sdgecko", "__io_", "EXI", "card_", "malloc", "_free_r", "realloc",
                                       "__sfp", "fwrite", "fflush", "__swrite", "_write_r", "memalign", "sbrk" };
    for (int i = 0; i < crash_nnames; i++)
        for (unsigned j = 0; j < sizeof(bad) / sizeof(bad[0]); j++)
            if (strstr(crash_names[i], bad[j])) return 0;
    return sd_ok;
}

static void crash_write(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    static char fbuf[1024];
    setvbuf(f, fbuf, _IOFBF, sizeof(fbuf));
    fwrite(crash_txt, 1, crash_len, f);
    fclose(f);
}

/* ---- plantage du processeur (libogc 3) ---- */
#ifdef CRASH_HOOK
static PPCExcptPanicFn crash_prev = NULL;
static volatile int crash_in = 0;

static const char *crash_excpt_name(unsigned e) {
    switch (e) {
    case PPC_EXCPT_DSI:   return "DSI (acces memoire invalide)";
    case PPC_EXCPT_ISI:   return "ISI (saut vers une adresse invalide)";
    case PPC_EXCPT_ALIGN: return "Alignement";
    case PPC_EXCPT_UNDEF: return "Program (instruction illegale)";
    case PPC_EXCPT_FPU:   return "Virgule flottante";
    case PPC_EXCPT_MCHK:  return "Machine Check";
    default:              return "Exception";
    }
}

static void crash_panic(unsigned exid, PPCContext *ctx) {
    if (!crash_in) {
        crash_in = 1;
        u32 dar = 0;
        if (exid == PPC_EXCPT_DSI) __asm__ volatile("mfspr %0,19" : "=r"(dar));
        char kind[96];
        snprintf(kind, sizeof(kind), "%s, exception %u", crash_excpt_name(exid), exid);
        crash_build(kind, ctx->pc, ctx->lr, ctx->gpr[1], exid == PPC_EXCPT_DSI, dar, ctx->gpr);
        if (crash_sd_safe()) crash_write(CRASH_FILE);
    }
    if (crash_prev) crash_prev(exid, ctx);                /* ecran rouge habituel de libogc */
    for (;;) { }
}
#endif

/* ---- blocage : la boucle principale ne tourne plus ---- */
static lwp_t crash_wd_h;
static u8 crash_wd_stack[16384] ATTRIBUTE_ALIGN(32);
#ifdef CRASH_HOOK
static KThread *crash_main_thr = NULL;
#endif

static void *crash_watchdog(void *arg) {
    (void)arg;
    u32 last = main_beat;
    u64 t_same = now_ms();
    int reported = 0;
    for (;;) {
        usleep(1000000);
        if (main_beat != last) {
            if (reported) remove(CRASH_FILE);            /* simple lenteur : pas de journal */
            reported = 0;
            last = main_beat;
            t_same = now_ms();
            continue;
        }
        if (!reported && now_ms() - t_same > 20000) {
            reported = 1;
            u32 pc = 0, lr = 0, sp = 0;
            const u32 *gpr = NULL;
#ifdef CRASH_HOOK
            if (crash_main_thr) { pc = crash_main_thr->ctx.pc; lr = crash_main_thr->ctx.lr; sp = crash_main_thr->ctx.gpr[1]; gpr = crash_main_thr->ctx.gpr; }
#endif
            crash_build("Blocage : la boucle principale ne repond plus depuis 20 s", pc, lr, sp, 0, 0, gpr);
            if (crash_sd_safe()) crash_write(CRASH_FILE);
        }
    }
    return NULL;
}

/* au demarrage, apres le montage de la carte SD */
static void crash_init(void) {
    crash_boot_ms = now_ms();
    if (sd_ok) {
        struct stat st;
        if (stat(CRASH_FILE, &st) == 0) {
            remove(CRASH_OLD);
            if (rename(CRASH_FILE, CRASH_OLD) == 0) crash_notice_until = now_ms() + 15000;
        }
    }
#ifdef CRASH_HOOK
    crash_prev = PPCExcptCurPanicFn;
    PPCExcptCurPanicFn = crash_panic;
    crash_main_thr = KThreadGetSelf();
#endif
    LWP_CreateThread(&crash_wd_h, crash_watchdog, NULL, crash_wd_stack, sizeof(crash_wd_stack), 100);
    crumb("demarrage (SD %s)", sd_ok ? "ok" : "absente");
}

/* a chaque tour de la boucle principale */
static void crash_beat(void) {
    main_beat++;
    static u64 t_mem = 0;
    u64 now = now_ms();
    if (now - t_mem > 3000) {
        t_mem = now;
        struct mallinfo mi = mallinfo();
        mem_free_kb = (u32)(mi.fordblks / 1024);
    }
}

static void draw_crash_notice(void) {
    if (!crash_notice_until || now_ms() >= crash_notice_until) return;
    gx_rrect_fill(40, 396, 560, 44, 10, RGBA(70, 10, 30, 240), RGBA(70, 10, 30, 240));
    gx_text_c(F_SMALL, 320, 414, "GC Station s'est arr\xC3\xAAt\xC3\xA9 la derni\xC3\xA8re fois : journal dans SD:/gc-mp3/gc-mp3-crash-ancien.txt",
              RGBA(255, 255, 255, 255));
    gx_text_c(F_SMALL, 320, 432, "(lisible aussi sur le t\xC3\xA9l\xC3\xA9phone : onglet T\xC3\x89L\xC3\x89PHONE, adresse /crash.txt)",
              RGBA(255, 200, 210, 255));
}

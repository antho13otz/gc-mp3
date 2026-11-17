/* bouchons du banc : ce que natif.c prend a main.c / tab_phone.c depuis v239 et v262 (carte par jeu, JSON du dernier lancement) */
#include <ctype.h>
static int nat_card_game = 0;
static int nat_xv = 1;                                   /* (v437 : lecture par interruptions ; v461 : reglage remis) */
static int nat_fx = 0;                                   /* (v512 : patchs de secours, jeux_cfg.c) */
#define NFX_COMPACT 2
#define NFX_NOLOG 4
#define NFX_SELF 8
#define NFX_SYNC 16
#define NFX_DIAG 32
static int nat_jcomplet = 0;                             /* (v467 : journal complet) */
static int sdx_used = 0, sdx_frq[2], sdx_sfrq[2], sdx_wbad = 0, sdx_wfrq[2], sdx_nc[2], sdx_crcon[2];                     /* (v471 : pilote SD des slots, sd_slot.c) */
typedef struct { char *b; int n, cap; } sbuf_t;
static void sb_add(sbuf_t *s, const char *t) {
    int l = (int)strlen(t);
    if (s->n + l + 1 > s->cap) { int c = (s->n + l + 1) * 2; char *b = (char *)realloc(s->b, c); if (!b) return; s->b = b; s->cap = c; }
    memcpy(s->b + s->n, t, l + 1); s->n += l;
}
static void sb_json_str(sbuf_t *s, const char *t) { sb_add(s, "\""); sb_add(s, t); sb_add(s, "\""); }
static void tp_any_utf8(const char *in, char *out, int cap) { snprintf(out, cap, "%s", in); }
/* v268 : succes (succes_jeux.c) */
static u32 na_ach[1];
static int na_ach_w = 0, na_ach_n = 0, na_ach_bad = 0;
static void sj_prepare(const char *id) { (void)id; }
/* (prive) manettes du telephone (main.c) */
static int net_ok = 0, nat_padtel = 0;
static char ip_str[16] = "", mask_str[16] = "", gw_str[16] = "";
/* v306 : chercheur de codes (main.c) ; v278 : test des touches (selftest_keys.c) */
static int nat_memtel = 0;
static int nat_pmode = 0;                                /* (v503 : mode de Pulse) */
static int nat_gbatel = 0;                               /* (v331 : GBA du telephone) */
static int nat_gbaport = 2;                              /* (v334 : son port) */
static int nat_mictel = 0;                               /* (v332 : micro du telephone) */
static int app_envoi = 1;                               /* (v330 : envoi vers l'appli, main.c) */
static int sx_safe = 0;
static void sx_block(const char *what) { (void)what; }
/* v397 : bandeau des succes sur la tele (main.c, succes_jeux.c) */
static int nat_tvsucc = 0;
static int nat_tvbas = 0;                               /* (v400 : bandeau en haut) */
static char na_ach_tit[64][48];
static u8 na_ach_deja[64];
static volatile int lsc_hold = 0, lsc_busy = 0;          /* (v390 / v391 : fil de la bibliotheque, main.c ; manquait au banc) */

/* GC-MP3 v24 : langue de l'interface (francais / anglais).
   Les textes restent ecrits en francais dans le code ; a l'affichage,
   tr() les remplace par leur traduction anglaise (table lang_tab.h).
   - gx_text / text_width / fit_text / wrap2 traduisent le texte affiche ;
   - snprintf traduit son format quand il contient un %% (ex. "Piste %d") ;
   - TR("...") traduit un morceau de texte a la main (ex. un argument %s).
   Les fichiers de reglages ne passent pas par ici : ils restent identiques. */

static int ui_lang = -1;                 /* -1 = auto (langue de la console), 0 = francais, 1 = anglais */
static int ui_en;                        /* 1 = interface en anglais */

static const char *const tr_tab[][2] = {
#include "lang_tab.h"
};
#define TR_N ((int)(sizeof(tr_tab) / sizeof(tr_tab[0])))
#define TR_H 4096                       /* table de hachage (puissance de 2, > 2 x TR_N) */
static short tr_hash[TR_H];
static int tr_ready;
#ifdef SIM_TRLOG
static void sim_trlog(const char *s) {          /* simulateur : textes pas traduits */
    static FILE *lf; static u32 seen[8192];
    u32 h = 2166136261u; for (const char *p = s; *p; p++) { h ^= (u8)*p; h *= 16777619u; }
    u32 i = h & 8191; while (seen[i] && seen[i] != h) i = (i + 1) & 8191;
    if (seen[i] == h) return; seen[i] = h;
    if (!lf) lf = fopen(getenv("SIM_TRLOG_FILE") ? getenv("SIM_TRLOG_FILE") : "trlog.txt", "a");
    if (lf) { fprintf(lf, "%s\n", s); fflush(lf); }
}
#endif

static u32 tr_fnv(const char *s) {
    u32 h = 2166136261u;
    while (*s) { h ^= (u8)*s++; h *= 16777619u; }
    return h;
}
static void tr_build(void) {
    for (int i = 0; i < TR_H; i++) tr_hash[i] = -1;
    for (int i = 0; i < TR_N; i++) {
        u32 h = tr_fnv(tr_tab[i][0]) & (TR_H - 1);
        while (tr_hash[h] >= 0) {
            if (!strcmp(tr_tab[tr_hash[h]][0], tr_tab[i][0])) break;   /* doublon : le premier gagne */
            h = (h + 1) & (TR_H - 1);
        }
        if (tr_hash[h] < 0) tr_hash[h] = (short)i;
    }
    tr_ready = 1;
}
static const char *tr(const char *s) {
    if (!ui_en || !s || !s[0]) return s;
    if (!tr_ready) tr_build();
    u32 h = tr_fnv(s) & (TR_H - 1);
    while (tr_hash[h] >= 0) {
        const char *const *e = tr_tab[tr_hash[h]];
        if (!strcmp(e[0], s)) return e[1];
        h = (h + 1) & (TR_H - 1);
    }
#ifdef SIM_TRLOG
    sim_trlog(s);
#endif
    return s;
}
#define TR(s) tr(s)
/* format de snprintf : traduit seulement les vrais formats (avec %), pour ne
   jamais toucher aux valeurs ecrites dans les fichiers ("non", "oui"...) */
__attribute__((format_arg(1))) static const char *trf(const char *f) {
    if (!ui_en || !f || !strchr(f, '%')) return f;
    return tr(f);
}
static void ui_lang_apply(void) {
    ui_en = ui_lang == 1 || (ui_lang < 0 && SYS_GetLanguage() != SYS_LANG_FRENCH);
}
#define snprintf(b, n, ...) snprintf(b, n, TRF_FIRST(__VA_ARGS__))
#define TRF_FIRST(f, ...) trf(f), ##__VA_ARGS__

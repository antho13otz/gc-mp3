/* bouchons du banc : ce que natif.c prend a main.c / tab_phone.c depuis v239 et v262 (carte par jeu, JSON du dernier lancement) */
#include <ctype.h>
static int nat_card_game = 0;
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

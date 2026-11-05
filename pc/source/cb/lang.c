/* lang.c - GC Station : traduction francaise de Cube Browser (table : lang_fr.h, faite par tools/cb-lang). */
#include "lang.h"
#include "settings.h"
#include "lang_fr.h"
#include <string.h>

#define TR_N ((int)(sizeof(tr_fr) / sizeof(tr_fr[0])))
#define TR_SLOTS 1024                                   /* (puissance de 2, > 2 x TR_N) */
static short slot[TR_SLOTS];
static int built;

static unsigned hash(const char *s) {
    unsigned x = 2166136261u;
    while (*s) x = (x ^ (unsigned char)*s++) * 16777619u;
    return x;
}

const char *tr(const char *en) {
    if (!en || g_set.lang == 1) return en;
    if (!built) {
        built = 1;
        for (int i = 0; i < TR_N; i++) {
            unsigned k = hash(tr_fr[i][0]) & (TR_SLOTS - 1);
            while (slot[k]) k = (k + 1) & (TR_SLOTS - 1);
            slot[k] = (short)(i + 1);
        }
    }
    for (unsigned k = hash(en) & (TR_SLOTS - 1); slot[k]; k = (k + 1) & (TR_SLOTS - 1))
        if (!strcmp(tr_fr[slot[k] - 1][0], en)) return tr_fr[slot[k] - 1][1];
    return en;
}

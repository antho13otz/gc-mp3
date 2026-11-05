// Clavier ASCII de la GameCube (celui de Phantasy Star Online), branche sur
// n'importe quel port manette. Il est interroge par la commande 0x54 : la
// reponse (8 octets) donne jusqu'a 3 touches enfoncees + un controle.
// Disposition japonaise (JIS) : les symboles suivent les inscriptions du clavier.
//   texte : tape directement dans le clavier a l'ecran (Entree = valider,
//   Echap = annuler, Retour arriere = effacer)
//   ailleurs : fleches = croix, Entree = A, Echap / Retour arriere = B,
//   Espace = pause, Tab = onglet suivant, F1 = menu, F2 = veilleuse,
//   Page haut / bas = morceau precedent / suivant ; dans l'onglet WEB, taper
//   une lettre ouvre la barre d'adresse ; dans MUSIQUE, la recherche.

#include <ogc/si.h>

static int gk_chan = -1;                       /* port du clavier, -1 = aucun */
static u64 gk_probe_t = 0;
static volatile int gk_busy = 0, gk_ok = 0;
static u8 gk_out[4] ATTRIBUTE_ALIGN(32) = { 0x54, 0x00, 0x00, 0x00 };
static u8 gk_in[8] ATTRIBUTE_ALIGN(32);
static u8 gk_prev[3];                          /* touches de l'image precedente */
static int gk_caps = 0;
static u8 gk_rep_key = 0;
static u64 gk_rep_next = 0;
static u32 gk_fail = 0;

static void gk_cb(s32 chan, u32 st) {
    (void)chan;
    gk_ok = (st & 0x0f) == 0;                  /* pas d'erreur de transfert */
    gk_busy = 0;
}

/* caractere tape (0 = touche speciale) */
static char gk_char(u8 k, int shift) {
    if (k >= 0x10 && k <= 0x29) {              /* A .. Z */
        char c = (char)('a' + (k - 0x10));
        return (shift ^ gk_caps) ? (char)(c - 32) : c;
    }
    static const char dig[10] = "1234567890", sdig[10] = "!\"#$%&'() ";
    if (k >= 0x2A && k <= 0x33) { char c = shift ? sdig[k - 0x2A] : dig[k - 0x2A]; return c == ' ' ? '0' : c; }
    switch (k) {
    case 0x34: return shift ? '=' : '-';
    case 0x35: return shift ? '~' : '^';
    case 0x36: return shift ? '|' : '\\';      /* touche yen */
    case 0x37: return shift ? '`' : '@';
    case 0x38: return shift ? '{' : '[';
    case 0x39: return shift ? '+' : ';';
    case 0x3A: return shift ? '*' : ':';
    case 0x3B: return shift ? '}' : ']';
    case 0x3C: return shift ? '<' : ',';
    case 0x3D: return shift ? '>' : '.';
    case 0x3E: return shift ? '?' : '/';
    case 0x3F: return shift ? '_' : '\\';
    case 0x59: return ' ';
    }
    return 0;
}

static int gk_repeatable(u8 k) {
    return (k >= 0x10 && k <= 0x3F) || k == 0x50 || k == 0x59 || (k >= 0x5C && k <= 0x5F);
}

/* une touche vient d'etre enfoncee (ou se repete) */
static void gk_key(u8 k, int shift, u32 *down, u32 *nav, int *fire) {
    if (kb_open) {
        char c = gk_char(k, shift);
        if (c) { char s[2] = { c, 0 }; kb_append(s); return; }
        if (k == 0x50) kb_backspace();
        else if (k == 0x61) kb_validate();
        else if (k == 0x4C) kb_open = 0;
        return;
    }
    switch (k) {
    case 0x5C: *nav |= PAD_BUTTON_LEFT;  *fire = 1; return;
    case 0x5D: *nav |= PAD_BUTTON_DOWN;  *fire = 1; return;
    case 0x5E: *nav |= PAD_BUTTON_UP;    *fire = 1; return;
    case 0x5F: *nav |= PAD_BUTTON_RIGHT; *fire = 1; return;
    case 0x61: *down |= PAD_BUTTON_A; return;
    case 0x4C: case 0x50: *down |= PAD_BUTTON_B; return;
    case 0x59: *down |= PAD_BUTTON_Y; return;
    case 0x51: *down |= PAD_TRIGGER_Z; return;
    case 0x40: *down |= PAD_BUTTON_START; return;
    case 0x41: if (!night_on && !menu_open) night_start(); return;
    case 0x08: *down |= PAD_TRIGGER_L; return;
    case 0x09: *down |= PAD_TRIGGER_R; return;
    }
    /* onglet WEB : une lettre ouvre la barre d'adresse */
    char c = gk_char(k, shift);
    if (c && c != ' ' && tab == TAB_WEB && !menu_open && !ck) {
        char s[2] = { c, 0 };
        kb_start(KB_URL, s, -1, -1);
    }
    else if (c && c != ' ' && tab == TAB_MUSIC && !menu_open && !ck && !am_open) {   /* MUSIQUE : recherche */
        char s[2] = { c, 0 };
        kb_start(KB_LIBFIND, s, -1, -1);
    }
}

/* a chaque image : cherche le clavier, lit les touches, ajoute leurs effets */
static void gk_pump(u32 pads, u32 *down, u32 *nav, int *fire) {
    u64 now = now_ms();
    if (gk_chan < 0) {
        if (now - gk_probe_t < 1000) return;
        gk_probe_t = now;
        for (int ch = 0; ch < 4; ch++) {
            if (pads & (1u << ch)) continue;                 /* une manette est la */
            if (SI_DecodeType(SI_GetType(ch)) == SI_GC_KEYBOARD) {
                gk_chan = ch; gk_fail = 0; gk_busy = 0; gk_ok = 0;
                memset(gk_prev, 0, sizeof(gk_prev));
                crumb("clavier GameCube sur le port %d", ch + 1);
                break;
            }
        }
        return;
    }
    if (gk_busy) return;
    if (gk_ok) {
        gk_ok = 0;
        u8 k[3] = { gk_in[4], gk_in[5], gk_in[6] };
        u8 sum = (u8)(gk_in[4] ^ gk_in[5] ^ gk_in[6] ^ (gk_in[0] & 0x0f));
        if (sum == gk_in[7]) {
            gk_fail = 0;
            int shift = 0;
            for (int i = 0; i < 3; i++) if (k[i] == 0x54 || k[i] == 0x55) shift = 1;
            for (int i = 0; i < 3; i++) {
                u8 c = k[i];
                if (!c || c == 0x54 || c == 0x55) continue;
                int was = (c == gk_prev[0] || c == gk_prev[1] || c == gk_prev[2]);
                if (!was) {
                    if (c == 0x53) { gk_caps = !gk_caps; continue; }
                    gk_key(c, shift, down, nav, fire);
                    if (gk_repeatable(c)) { gk_rep_key = c; gk_rep_next = now + 450; }
                } else if (c == gk_rep_key && now >= gk_rep_next) {
                    gk_key(c, shift, down, nav, fire);
                    gk_rep_next = now + 55;
                }
            }
            if (gk_rep_key && gk_rep_key != k[0] && gk_rep_key != k[1] && gk_rep_key != k[2]) gk_rep_key = 0;
            memcpy(gk_prev, k, 3);
        }
    } else if (++gk_fail > 120) {                            /* plus de reponse : debranche */
        crumb("clavier GameCube debranche");
        gk_chan = -1;
        gk_probe_t = now;
        return;
    }
    gk_busy = 1;
    if (!SI_Transfer(gk_chan, gk_out, 3, gk_in, 8, gk_cb, 0)) gk_busy = 0;
}

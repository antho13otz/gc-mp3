// GC Station v276 : jeu de cartes entre potes : le 8 américain (inclus dans main.c, après selftest.c)
// Chacun a l'appli sur son téléphone (Plus > Jeu de cartes) : il y voit SA main et joue ; la télé montre la table
// (le tas, la pioche, les joueurs, à qui le tour, les scores). Tout se passe dans la console : l'appli demande l'état
// (/api/cartes?op=etat) et envoie ses coups (op=jouer, piocher, passer...) ; la console vérifie les règles.
//
// Règles (8 américain, version courte) :
//  - jeu de 52 cartes ; 2 joueurs : 8 cartes chacun, 3-4 : 7, 5-6 : 5 ; une carte retournée commence le tas ;
//  - on pose une carte de la même couleur ou de la même valeur que celle du dessus ;
//  - un 8 se pose sur tout et fait choisir la couleur suivante ;
//  - un 2 : le suivant pioche 2 cartes (ou pose un 2 à son tour : ça s'ajoute) ;
//  - un valet : le suivant passe son tour ; un as : on change de sens (à 2 : le suivant passe) ;
//  - pas de carte possible : on pioche 1 carte, puis on la pose si on peut, sinon on passe ;
//  - le premier qui n'a plus de cartes gagne la manche ; les autres marquent les points de leur main
//    (8 : 50, roi / dame / valet : 10, as : 1, les autres : leur valeur). Le moins de points gagne la partie.
// Le fil du téléphone (tab_phone.c) modifie l'état ; la boucle de l'écran ne fait que le dessiner.

#define C8_MAX 6
typedef struct { char nom[20]; u32 tok; int n, score, on; u8 main[52]; u64 vu; } c8_joueur_t;
static struct {
    int phase;                         /* 0 rien, 1 salon (on attend les joueurs), 2 partie, 3 fin de manche */
    c8_joueur_t j[C8_MAX];
    int nj;
    u8 pioche[52], tas[52];
    int np, nt;
    int tour, sens, coul, plus2, a_pioche, gagnant, manche, premier;
    char msg[120];
    u32 ver;                           /* (change a chaque coup : la tele redessine, l'appli vibre a son tour) */
    u64 msg_t;
} c8;
static int c8_open_req = 0;            /* (fil du telephone -> boucle : ouvrir la table sur la tele) */

static const char *const c8_rang[13] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "V", "D", "R" };
static const char *const c8_coul_nom[4] = { "pique", "cœur", "carreau", "trèfle" };
static int c8_r(int c) { return c % 13; }
static int c8_s(int c) { return c / 13; }
static int c8_pts(int c) { int r = c8_r(c); return r == 7 ? 50 : r >= 10 ? 10 : r == 0 ? 1 : r + 1; }
static u32 c8_rand(void) { static u32 x = 0; if (!x) x = (u32)gettime() | 1u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
static void c8_shuffle(u8 *a, int n) { for (int i = n - 1; i > 0; i--) { int k = (int)(c8_rand() % (u32)(i + 1)); u8 t = a[i]; a[i] = a[k]; a[k] = t; } }
static void c8_say(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(c8.msg, sizeof(c8.msg), fmt, ap); va_end(ap);
    c8.msg_t = now_ms(); c8.ver++;
}
static int c8_top(void) { return c8.nt ? c8.tas[c8.nt - 1] : -1; }
static int c8_cur_suit(void) { return c8.coul >= 0 ? c8.coul : c8_s(c8_top()); }
static int c8_can(int c) {
    if (c8.plus2) return c8_r(c) == 1;                    /* (des 2 a piocher : seulement un autre 2) */
    if (c8_r(c) == 7) return 1;
    return c8_s(c) == c8_cur_suit() || (c8.coul < 0 && c8_r(c) == c8_r(c8_top()));
}
static int c8_draw1(void) {                               /* une carte de la pioche (le tas est remélangé si besoin) */
    if (!c8.np && c8.nt > 1) {
        int top = c8.tas[c8.nt - 1];
        for (int i = 0; i < c8.nt - 1; i++) c8.pioche[i] = c8.tas[i];
        c8.np = c8.nt - 1; c8.tas[0] = (u8)top; c8.nt = 1;
        c8_shuffle(c8.pioche, c8.np);
    }
    return c8.np ? c8.pioche[--c8.np] : -1;
}
static void c8_give(int k, int n) { while (n-- > 0) { int c = c8_draw1(); if (c < 0) break; c8.j[k].main[c8.j[k].n++] = (u8)c; } }
static int c8_next(int k) { return ((k + c8.sens) % c8.nj + c8.nj) % c8.nj; }
static void c8_deal(void) {
    for (int i = 0; i < 52; i++) c8.pioche[i] = (u8)i;
    c8.np = 52; c8.nt = 0;
    c8_shuffle(c8.pioche, 52);
    int n = c8.nj <= 2 ? 8 : c8.nj <= 4 ? 7 : 5;
    for (int k = 0; k < c8.nj; k++) { c8.j[k].n = 0; c8_give(k, n); }
    c8.tas[c8.nt++] = (u8)c8_draw1();
    c8.sens = 1; c8.coul = -1; c8.plus2 = 0; c8.a_pioche = 0; c8.gagnant = -1;
    c8.premier = c8.manche ? (c8.premier + 1) % c8.nj : 0;
    c8.tour = c8.premier;
    c8.manche++;
    c8.phase = 2;
    c8_say("Manche %d : %s commence", c8.manche, c8.j[c8.tour].nom);
}
static void c8_remove(int k) {                            /* un joueur s'en va : ses cartes sous la pioche */
    for (int i = 0; i < c8.j[k].n && c8.np < 52; i++) { memmove(c8.pioche + 1, c8.pioche, c8.np); c8.pioche[0] = c8.j[k].main[i]; c8.np++; }
    char nom[20]; snprintf(nom, sizeof(nom), "%s", c8.j[k].nom);
    for (int i = k; i < c8.nj - 1; i++) c8.j[i] = c8.j[i + 1];
    c8.nj--;
    if (c8.tour > k) c8.tour--;
    if (c8.nj && c8.tour >= c8.nj) c8.tour = 0;
    if (c8.gagnant == k) c8.gagnant = -1; else if (c8.gagnant > k) c8.gagnant--;
    if (c8.premier >= c8.nj) c8.premier = 0;
    if (!c8.nj) { c8.phase = 0; return; }
    if (c8.phase >= 2 && c8.nj < 2) { c8.phase = 1; c8_say("%s est parti : on attend un autre joueur", nom); return; }
    c8.a_pioche = 0;
    c8_say("%s a quitté la partie", nom);
}
static void c8_end_round(int k) {
    c8.gagnant = k; c8.phase = 3;
    for (int i = 0; i < c8.nj; i++) if (i != k) for (int m = 0; m < c8.j[i].n; m++) c8.j[i].score += c8_pts(c8.j[i].main[m]);
    c8_say("%s gagne la manche !", c8.j[k].nom);
}
/* un coup ; rend NULL si c'est bon, sinon pourquoi c'est refusé */
static const char *c8_play(int k, int c, int suit) {
    if (c8.phase != 2) return "La partie n'a pas commencé";
    if (k != c8.tour) return "Ce n'est pas ton tour";
    int at = -1;
    for (int i = 0; i < c8.j[k].n; i++) if (c8.j[k].main[i] == c) at = i;
    if (at < 0) return "Tu n'as pas cette carte";
    if (!c8_can(c)) return c8.plus2 ? "Pose un 2 ou pioche" : "Cette carte ne va pas : même couleur ou même valeur (ou un 8)";
    if (c8_r(c) == 7 && (suit < 0 || suit > 3)) return "Choisis une couleur";
    c8.j[k].main[at] = c8.j[k].main[--c8.j[k].n];
    c8.tas[c8.nt++] = (u8)c;
    c8.coul = -1; c8.a_pioche = 0;
    int r = c8_r(c), skip = 0;
    char extra[48] = "";
    if (r == 7) { c8.coul = suit; snprintf(extra, sizeof(extra), " : %s demandé", c8_coul_nom[suit]); }
    else if (r == 1) { c8.plus2 += 2; snprintf(extra, sizeof(extra), " : +%d pour le suivant", c8.plus2); }
    else if (r == 10) { skip = 1; snprintf(extra, sizeof(extra), " : le suivant passe"); }
    else if (r == 0) { c8.sens = -c8.sens; if (c8.nj == 2) skip = 1; snprintf(extra, sizeof(extra), " : changement de sens"); }
    if (!c8.j[k].n) { c8_end_round(k); return NULL; }
    c8.tour = c8_next(k);
    if (skip) c8.tour = c8_next(c8.tour);
    c8_say("%s pose %s %s%s%s", c8.j[k].nom, c8_rang[r], c8_coul_nom[c8_s(c)], extra, c8.j[k].n == 1 ? " · plus qu'une carte !" : "");
    return NULL;
}
static const char *c8_pick(int k) {
    if (c8.phase != 2) return "La partie n'a pas commencé";
    if (k != c8.tour) return "Ce n'est pas ton tour";
    if (c8.plus2) {
        int n = c8.plus2;
        c8_give(k, n); c8.plus2 = 0; c8.a_pioche = 0;
        c8.tour = c8_next(k);
        c8_say("%s pioche %d cartes", c8.j[k].nom, n);
        return NULL;
    }
    if (c8.a_pioche) return "Tu as déjà pioché : pose une carte ou passe";
    c8_give(k, 1); c8.a_pioche = 1;
    c8_say("%s pioche", c8.j[k].nom);
    return NULL;
}
static const char *c8_pass(int k) {
    if (c8.phase != 2 || k != c8.tour) return "Ce n'est pas ton tour";
    if (!c8.a_pioche) return "Pioche d'abord";
    c8.a_pioche = 0; c8.tour = c8_next(k);
    c8_say("%s passe", c8.j[k].nom);
    return NULL;
}
/* joueurs qui ne demandent plus rien depuis 90 s : partis (téléphone éteint, appli fermée) */
static void c8_reap(void) {
    u64 t = now_ms();
    for (int k = c8.nj - 1; k >= 0; k--) if (t - c8.j[k].vu > 90000) c8_remove(k);
}

static int c8_cur_suit_safe(void) { return c8.nt ? c8_cur_suit() : -1; }
static void c8_json_str(char *o, int cap, int *n, const char *s) {
    if (*n < cap) o[(*n)++] = '"';
    for (; *s && *n < cap - 8; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { o[(*n)++] = '\\'; o[(*n)++] = (char)ch; }
        else if (ch >= 0x20) o[(*n)++] = (char)ch;
    }
    if (*n < cap) o[(*n)++] = '"';
}
#define C8_ADD(...) do { if (n < cap) n += snprintf(o + n, cap - n, __VA_ARGS__); } while (0)
static int c8_json(char *o, int cap, int k, const char *err) {
    int n = 0;
    C8_ADD("{\"ok\":%d,\"ph\":%d,\"moi\":%d,\"tour\":%d,\"sens\":%d,\"coul\":%d,\"p2\":%d,\"pi\":%d,\"haut\":%d,\"np\":%d,\"gag\":%d,\"manche\":%d,\"ver\":%u,\"msg\":",
           err ? 0 : 1, c8.phase, k, c8.tour, c8.sens, c8_cur_suit_safe(), c8.plus2, c8.a_pioche, c8_top(), c8.np, c8.gagnant, c8.manche, (unsigned)c8.ver);
    c8_json_str(o, cap, &n, c8.msg);
    if (err) { C8_ADD(",\"err\":"); c8_json_str(o, cap, &n, err); }
    C8_ADD(",\"j\":[");
    for (int i = 0; i < c8.nj; i++) {
        C8_ADD("%s{\"n\":", i ? "," : "");
        c8_json_str(o, cap, &n, c8.j[i].nom);
        C8_ADD(",\"c\":%d,\"s\":%d}", c8.j[i].n, c8.j[i].score);
    }
    C8_ADD("],\"main\":[");
    if (k >= 0 && k < c8.nj) for (int i = 0; i < c8.j[k].n; i++) C8_ADD("%s%d", i ? "," : "", c8.j[k].main[i]);
    C8_ADD("],\"jouables\":[");
    int first = 1;
    if (k >= 0 && k < c8.nj && c8.phase == 2 && k == c8.tour)
        for (int i = 0; i < c8.j[k].n; i++) if (c8_can(c8.j[k].main[i])) { C8_ADD("%s%d", first ? "" : ",", c8.j[k].main[i]); first = 0; }
    C8_ADD("]}");
    return n < cap ? n : cap - 1;
}

/* /api/cartes?op=...&id=<rang>&tok=<jeton> : rejoindre (nom), etat, jouer (c, s), piocher, passer, debut, nouvelle,
   quitter, fin (tout arrêter), tele (montrer la table) */
static void c8_api(s32 c, const char *q) {
    char op[12], v[48];
    tp_param(q, "op", op, sizeof(op));
    tp_param(q, "tok", v, sizeof(v));
    u32 tok = (u32)strtoul(v, NULL, 10);
    int k = -1;
    for (int i = 0; i < c8.nj; i++) if (tok && c8.j[i].tok == tok) k = i;
    const char *err = NULL;
    c8_reap();
    if (!strcmp(op, "join")) {
        if (k < 0) {
            char nom[48];
            tp_param(q, "nom", nom, sizeof(nom));
            int l = 0;                                    /* (nom : 16 octets au plus, sans couper un caractere) */
            for (int i = 0; nom[i] && i < 16; ) {
                int len = (unsigned char)nom[i] < 0x80 ? 1 : (unsigned char)nom[i] < 0xE0 ? 2 : (unsigned char)nom[i] < 0xF0 ? 3 : 4;
                if (i + len > 16) break;
                i += len; l = i;
            }
            nom[l] = 0;
            if (!nom[0]) snprintf(nom, sizeof(nom), "Joueur %d", c8.nj + 1);
            if (c8.phase >= 2) err = "Une partie est en cours : attends la fin de la manche";
            else if (c8.nj >= C8_MAX) err = "La table est pleine (6 joueurs)";
            else {
                k = c8.nj++;
                memset(&c8.j[k], 0, sizeof(c8.j[k]));
                snprintf(c8.j[k].nom, sizeof(c8.j[k].nom), "%s", nom);
                do c8.j[k].tok = c8_rand() % 2000000000u + 1u; while (!c8.j[k].tok);
                if (!c8.phase) { c8.phase = 1; c8.manche = 0; }
                c8_say("%s rejoint la table", nom);
                c8_open_req = 1;
            }
        }
    } else if (k < 0 && strcmp(op, "etat")) err = "Tu n'es pas à la table : rejoins-la";
    else if (!strcmp(op, "jouer")) {
        tp_param(q, "c", v, sizeof(v)); int cc = atoi(v);
        tp_param(q, "s", v, sizeof(v)); int s = v[0] ? atoi(v) : -1;
        err = c8_play(k, cc, s);
    }
    else if (!strcmp(op, "piocher")) err = c8_pick(k);
    else if (!strcmp(op, "passer")) err = c8_pass(k);
    else if (!strcmp(op, "debut")) {
        if (c8.phase != 1) err = "La partie a déjà commencé";
        else if (c8.nj < 2) err = "Il faut au moins 2 joueurs";
        else { for (int i = 0; i < c8.nj; i++) c8.j[i].score = 0; c8.manche = 0; c8.premier = 0; c8_deal(); c8_open_req = 1; }
    }
    else if (!strcmp(op, "nouvelle")) {
        if (c8.phase != 3) err = "La manche n'est pas finie";
        else { c8_deal(); c8_open_req = 1; }
    }
    else if (!strcmp(op, "quitter")) { c8_remove(k); k = -1; }
    else if (!strcmp(op, "fin")) { c8.nj = 0; c8.phase = 0; k = -1; c8_say("Partie terminée"); }
    else if (!strcmp(op, "tele")) c8_open_req = 1;
    if (k >= 0 && k < c8.nj) c8.j[k].vu = now_ms();
    static char o[2600];
    int n = c8_json(o, sizeof(o) - 40, k, err);
    if (k >= 0 && k < c8.nj && !strcmp(op, "join")) {    /* (le jeton a garder) */
        o[n - 1] = 0;
        n = (int)strlen(o);
        n += snprintf(o + n, sizeof(o) - n, ",\"tok\":%u}", (unsigned)c8.j[k].tok);
    }
    tp_reply(c, 200, "application/json; charset=utf-8", o, n);
}

/* ======================= la table sur la télé ======================= */
static col_t c8_red(void) { return RGBA(214, 36, 60, 255); }
static col_t c8_black(void) { return RGBA(24, 22, 36, 255); }
static void c8_suit(f32 cx, f32 cy, f32 r, int s, col_t col) {
    if (s == 1) {                                         /* cœur */
        gx_circle(cx - r * 0.5f, cy - r * 0.28f, r * 0.55f, col, col);
        gx_circle(cx + r * 0.5f, cy - r * 0.28f, r * 0.55f, col, col);
        gx_tri(cx - r * 1.03f, cy - r * 0.12f, cx + r * 1.03f, cy - r * 0.12f, cx, cy + r * 1.0f, col);
    } else if (s == 2) {                                  /* carreau */
        gx_poly4(cx, cy - r, cx + r * 0.72f, cy, cx, cy + r, cx - r * 0.72f, cy, col, col, col, col);
    } else if (s == 0) {                                  /* pique */
        gx_circle(cx - r * 0.5f, cy + r * 0.18f, r * 0.52f, col, col);
        gx_circle(cx + r * 0.5f, cy + r * 0.18f, r * 0.52f, col, col);
        gx_tri(cx - r * 1.0f, cy + r * 0.05f, cx + r * 1.0f, cy + r * 0.05f, cx, cy - r * 1.0f, col);
        gx_tri(cx, cy + r * 0.25f, cx - r * 0.38f, cy + r * 1.0f, cx + r * 0.38f, cy + r * 1.0f, col);
    } else {                                              /* trèfle */
        gx_circle(cx, cy - r * 0.45f, r * 0.45f, col, col);
        gx_circle(cx - r * 0.5f, cy + r * 0.12f, r * 0.45f, col, col);
        gx_circle(cx + r * 0.5f, cy + r * 0.12f, r * 0.45f, col, col);
        gx_tri(cx, cy, cx - r * 0.38f, cy + r * 1.0f, cx + r * 0.38f, cy + r * 1.0f, col);
    }
}
static void c8_card(f32 x, f32 y, f32 w, f32 h, int c) {
    gx_rrect_fill(x + 3, y + 4, w, h, w * 0.1f, RGBA(0, 0, 0, 110), RGBA(0, 0, 0, 110));
    if (c < 0) {                                          /* dos */
        gx_rrect_fill(x, y, w, h, w * 0.1f, RGBA(120, 70, 230, 255), RGBA(60, 26, 150, 255));
        gx_rrect_stroke(x + 4, y + 4, w - 8, h - 8, w * 0.07f, 2, RGBA(220, 200, 255, 200));
        gx_text_c(F_MED, x + w / 2, y + h / 2 + 7, "GC", RGBA(240, 230, 255, 230));
        return;
    }
    int r = c8_r(c), s = c8_s(c);
    col_t col = s == 1 || s == 2 ? c8_red() : c8_black();
    gx_rrect_fill(x, y, w, h, w * 0.1f, RGBA(255, 255, 255, 255), RGBA(232, 230, 242, 255));
    gx_rrect_stroke(x, y, w, h, w * 0.1f, 1.5f, RGBA(150, 140, 190, 255));
    int big = w >= 70;
    gx_text1(big ? F_LARGE : F_MED, x + w * 0.1f, y + (big ? 30 : 20), c8_rang[r], col);
    c8_suit(x + w * 0.2f, y + (big ? 44 : 30), big ? 7 : 5, s, col);
    c8_suit(x + w / 2, y + h * 0.6f, w * (big ? 0.22f : 0.2f), s, col);
}
static void c8_input(u32 down, u32 nav, int fire) {
    (void)nav; (void)fire;
    if (down & PAD_BUTTON_B) tl_screen = TL_NONE;
    if (down & PAD_BUTTON_X) { c8.nj = 0; c8.phase = 0; c8_say("Partie terminée (console)"); tl_screen = TL_NONE; }
}
static void c8_draw(void) {
    char t[160];
    snprintf(t, sizeof(t), c8.phase >= 2 ? "Manche %d" : "Salon", c8.manche);
    tl_frame("8 américain", t);
    if (c8.phase <= 1) {
        gx_text_c(F_MED, 320, 120, "Sur ton téléphone : appli GC Station > Plus > Jeu de cartes", RGBA(255, 210, 120, 255));
        gx_text_c(F_SMALL, 320, 146, "2 à 6 joueurs ; un joueur appuie sur « Commencer » quand tout le monde est là", RGBA(200, 190, 240, 255));
        for (int i = 0; i < c8.nj; i++) {
            f32 y = 176 + i * 34;
            gx_rrect_fill(170, y, 300, 28, 8, RGBA(40, 24, 96, 255), RGBA(28, 16, 70, 255));
            gx_text1(F_MED, 186, y + 20, c8.j[i].nom, RGBA(255, 255, 255, 255));
        }
        if (!c8.nj) gx_text_c(F_MED, 320, 230, "Personne à la table pour l'instant", RGBA(160, 150, 200, 255));
    } else {
        /* pioche et tas */
        for (int i = 0; i < (c8.np > 3 ? 3 : c8.np); i++) c8_card(84 - i * 2, 120 - i * 2, 78, 110, -1);
        snprintf(t, sizeof(t), "Pioche : %d", c8.np);
        gx_text_c(F_SMALL, 123, 252, t, RGBA(200, 190, 240, 255));
        int top = c8_top();
        if (c8.nt > 1) c8_card(196, 126, 92, 130, c8.tas[c8.nt - 2]);
        if (top >= 0) c8_card(210, 116, 92, 130, top);
        if (c8.coul >= 0) {                               /* couleur demandée par un 8 */
            gx_rrect_fill(316, 120, 72, 72, 12, RGBA(255, 255, 255, 255), RGBA(230, 226, 245, 255));
            c8_suit(352, 152, 20, c8.coul, c8.coul == 1 || c8.coul == 2 ? c8_red() : c8_black());
            gx_text_c(F_SMALL, 352, 210, "demandé", RGBA(255, 210, 120, 255));
        }
        if (c8.plus2) { snprintf(t, sizeof(t), "+%d", c8.plus2); gx_text_c(F_LARGE, 352, 248, t, RGBA(255, 90, 110, 255)); }
        gx_text_c(F_SMALL, 256, 274, c8.sens > 0 ? "sens : ↓ (vers le bas de la liste)" : "sens : ↑ (vers le haut de la liste)", RGBA(170, 160, 220, 255));
        /* joueurs */
        for (int i = 0; i < c8.nj; i++) {
            f32 y = 96 + i * 46;
            int cur = c8.phase == 2 && i == c8.tour, win = c8.phase == 3 && i == c8.gagnant;
            if (cur) gx_glow(404, y, 180, 40, 10, RGBA(255, 120, 230, 120), 3);
            gx_rrect_fill(404, y, 180, 40, 10, cur ? RGBA(150, 60, 220, 255) : win ? RGBA(40, 140, 90, 255) : RGBA(36, 22, 86, 255),
                          cur ? RGBA(100, 30, 170, 255) : win ? RGBA(24, 100, 60, 255) : RGBA(24, 14, 60, 255));
            char nb[64]; fit_text(F_MED, c8.j[i].nom, 110, nb, sizeof(nb));
            gx_text1(F_MED, 414, y + 18, nb, RGBA(255, 255, 255, 255));
            snprintf(t, sizeof(t), "%d carte%s · %d pts", c8.j[i].n, c8.j[i].n > 1 ? "s" : "", c8.j[i].score);
            gx_text1(F_SMALL, 414, y + 34, t, RGBA(220, 210, 255, 255));
            int show = c8.j[i].n < 8 ? c8.j[i].n : 8;
            for (int m = 0; m < show; m++) gx_rrect_fill(560 - m * 5, y + 8, 14, 22, 3, RGBA(130, 80, 240, 255), RGBA(70, 30, 160, 255));
        }
        if (c8.phase == 3) {
            int best = 0;
            for (int i = 1; i < c8.nj; i++) if (c8.j[i].score < c8.j[best].score) best = i;
            snprintf(t, sizeof(t), "En tête : %s (%d pts) · « Manche suivante » sur un téléphone", c8.j[best].nom, c8.j[best].score);
            gx_text_c(F_SMALL, 320, 300, t, RGBA(150, 255, 190, 255));
        } else if (c8.nj) {
            snprintf(t, sizeof(t), "À %s de jouer", c8.j[c8.tour].nom);
            gx_text_c(F_LARGE, 256, 318, t, RGBA(255, 255, 255, 255));
        }
    }
    if (c8.msg[0]) {
        char fv[200]; fit_text(F_MED, c8.msg, 520, fv, sizeof(fv));
        gx_rrect_fill(54, 352, 532, 30, 8, RGBA(40, 20, 90, 250), RGBA(30, 14, 70, 250));
        gx_text_c(F_MED, 320, 373, fv, RGBA(255, 210, 120, 255));
    }
    static const char *const k[] = { "B", "X" };
    static const char *const l[] = { "FERMER (la partie continue)", "TERMINER LA PARTIE" };
    tl_legend(k, l, 2);
}
/* (boucle) un joueur a rejoint, la partie commence : la table s'ouvre sur la télé */
static void c8_tick(void) {
    if (!c8_open_req) return;
    c8_open_req = 0;
    if (tl_screen == TL_NONE || tl_screen == TL_CARTES) { menu_open = 0; kb_open = 0; saver_on = 0; tl_screen = TL_CARTES; }
}

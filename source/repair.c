// GC Station v140 : reparation automatique des jeux lances avec Pulse. Inclus dans main.c (apres compat.c).
//  - quand un jeu plante, reste sur un ecran noir ou marche mal (base de compatibilite, compat.c), GC Station
//    regarde le journal du jeu et le journal du lancement (diagnostic : rep_diag_set, appele par natif.c) et
//    prepare une liste d'essais, du plus probable au moins probable : vitesse d'un vrai lecteur, Pulse universel,
//    sans carte virtuelle, sans cheats, sans 16:9 force, mode video du jeu... ;
//  - au lancement suivant de ce jeu, l'essai en cours est applique (pour ce lancement seulement) ;
//  - au retour : s'il marche (ou marche mieux), le reglage est garde dans les reglages du jeu
//    (SD:/gc-mp3/jeux/<ID>.txt) ; sinon, essai suivant. Tout est garde dans SD:/gc-mp3/reparation.txt.

#define RP_FILE GCD "/reparation.txt"
#define RP_MAX  64
enum { RPS_SPEED = 'S', RPS_FAST = 'F', RPS_TRAP = 'T', RPS_NOCARD = 'C', RPS_NOCHEAT = 'H', RPS_NOWS = 'W', RPS_VIDEO = 'V', RPS_BOTH = 'X' };
typedef struct { char id[7]; char steps[12]; u8 cur, tried, base; } rep_t;   /* base : verdict avant les essais */
static rep_t rep_tab[RP_MAX];
static int rep_n = -1;
static struct { char id[7]; int ok, crashed, noimg, stuck, skipped, cheats, ws, card, vmode, slow, trap; } rep_d;

static const char *rep_name(int c) {
    switch (c) {
    case RPS_SPEED:   return "vitesse d'un vrai lecteur";
    case RPS_FAST:    return "lecteur rapide (carte SD)";
    case RPS_TRAP:    return "Pulse universel";
    case RPS_NOCARD:  return "sans carte m\xC3\xA9moire virtuelle";
    case RPS_NOCHEAT: return "sans les cheats";
    case RPS_NOWS:    return "sans le 16:9 forc\xC3\xA9";
    case RPS_VIDEO:   return "mode vid\xC3\xA9o du jeu";
    case RPS_BOTH:    return "vrai lecteur + Pulse universel";
    }
    return "?";
}
static void rep_load(void) {
    rep_n = 0;
    FILE *f = sd_ok ? fopen(RP_FILE, "r") : NULL;
    if (!f) return;
    char l[96];
    while (fgets(l, sizeof(l), f) && rep_n < RP_MAX) {
        if (l[0] == '#') continue;
        char id[8] = "", st[16] = "";
        int cur = 0, tr = 0, base = 0;
        if (sscanf(l, "%7[^|]|%15[^|]|%d|%d|%d", id, st, &cur, &tr, &base) < 3 || strlen(id) != 6) continue;
        rep_t *p = &rep_tab[rep_n++];
        memset(p, 0, sizeof(*p));
        snprintf(p->id, sizeof(p->id), "%s", id); snprintf(p->steps, sizeof(p->steps), "%s", st);
        p->cur = (u8)cur; p->tried = (u8)(tr != 0); p->base = (u8)base;
    }
    fclose(f);
}
static void rep_save(void) {
    if (!sd_ok || rep_n < 0) return;
    if (!rep_n) { remove(RP_FILE); return; }
    FILE *f = fopen(RP_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "# GC Station : essais de reparation (code | essais | essai en cours | essaye au dernier lancement | verdict de depart)\n");
    for (int i = 0; i < rep_n; i++) fprintf(f, "%s|%s|%d|%d|%d\n", rep_tab[i].id, rep_tab[i].steps, rep_tab[i].cur, rep_tab[i].tried, rep_tab[i].base);
    if (fclose(f) == 0) { remove(RP_FILE); rename(RP_FILE ".tmp", RP_FILE); }
}
static rep_t *rep_find(const char *id) {
    if (rep_n < 0) rep_load();
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < rep_n; i++) if (!strncmp(rep_tab[i].id, id, 6)) return &rep_tab[i];
    return NULL;
}
static void rep_drop(const char *id) {
    rep_t *p = rep_find(id);
    if (!p) return;
    *p = rep_tab[--rep_n];
    rep_save();
}
/* (natif.c) diagnostic du dernier lancement, avant la question de compat.c */
static void rep_diag_set(const char *id, int crashed, int noimg, int stuck, int skipped, int cheats, int ws, int card, int vmode, int slow, int trap) {
    memset(&rep_d, 0, sizeof(rep_d));
    snprintf(rep_d.id, sizeof(rep_d.id), "%.6s", id);
    rep_d.ok = 1; rep_d.crashed = crashed; rep_d.noimg = noimg; rep_d.stuck = stuck; rep_d.skipped = skipped;
    rep_d.cheats = cheats; rep_d.ws = ws; rep_d.card = card; rep_d.vmode = vmode; rep_d.slow = slow; rep_d.trap = trap;
}
static int rep_add(char *s, int n, int c) { if (n < 10 && !strchr(s, c)) { s[n++] = (char)c; s[n] = 0; } return n; }
/* liste des essais, du plus probable au moins probable, selon ce qui s'est passe */
static int rep_plan(const char *id, int verdict, char *s) {
    int n = 0, d = rep_d.ok && !strncmp(rep_d.id, id, 6);
    s[0] = 0;
    int slow = d && rep_d.slow, trap = d && rep_d.trap;
    if (verdict == CP_PLANTE) {
        if (!d || rep_d.cheats) n = rep_add(s, n, RPS_NOCHEAT);
        if (!d || rep_d.ws) n = rep_add(s, n, RPS_NOWS);
        if (d && rep_d.vmode) n = rep_add(s, n, RPS_VIDEO);
        if (d && rep_d.skipped && !trap) n = rep_add(s, n, RPS_TRAP);
        n = rep_add(s, n, slow ? RPS_FAST : RPS_SPEED);
        if (!trap) n = rep_add(s, n, RPS_TRAP);
        if (!d || rep_d.card) n = rep_add(s, n, RPS_NOCARD);
    } else if (verdict == CP_NOIR) {
        if (d && rep_d.skipped && !trap) n = rep_add(s, n, RPS_TRAP);   /* (des acces au lecteur n'ont pas ete branches) */
        n = rep_add(s, n, slow ? RPS_FAST : RPS_SPEED);               /* (le plus frequent : le disque repond trop vite) */
        if (!trap) n = rep_add(s, n, RPS_TRAP);
        if (d && rep_d.noimg && rep_d.vmode) n = rep_add(s, n, RPS_VIDEO);   /* (pas une image : le mode video ?) */
        if (!d || rep_d.card) n = rep_add(s, n, RPS_NOCARD);
        if (!d || rep_d.cheats) n = rep_add(s, n, RPS_NOCHEAT);
        if (d && rep_d.vmode) n = rep_add(s, n, RPS_VIDEO);
        if (!slow && !trap) n = rep_add(s, n, RPS_BOTH);
    } else if (verdict == CP_MAL) {
        n = rep_add(s, n, slow ? RPS_FAST : RPS_SPEED);               /* (son haché, ralentis : la vitesse du lecteur) */
        if (d && rep_d.ws) n = rep_add(s, n, RPS_NOWS);
        if (d && rep_d.vmode) n = rep_add(s, n, RPS_VIDEO);
        if (!d || rep_d.cheats) n = rep_add(s, n, RPS_NOCHEAT);
        if (!trap) n = rep_add(s, n, RPS_TRAP);
    }
    if (!d) {                                                         /* (sans journal : tout ce qui peut aider) */
        n = rep_add(s, n, RPS_SPEED); n = rep_add(s, n, RPS_TRAP); n = rep_add(s, n, RPS_NOCARD);
    }
    return n;
}
/* (jeux_cfg_apply, lancement avec Pulse) l'essai en cours, pour ce lancement seulement */
static void rep_apply(const char *id) {
    rep_msg[0] = 0;
    rep_t *p = rep_find(id);
    if (!p && cp_verdict(id) == CP_NONE && rep_n < RP_MAX) {   /* v145 : jamais essaye ici, marche sur une autre console avec un reglage */
        cpp_t *q = cpp_find(id);
        int c = q && q->v == CP_OK ? (q->fix ? q->fix : q->slow ? RPS_SPEED : 0) : 0;
        if (c && rep_name(c)[0] != '?') {
            p = &rep_tab[rep_n++];
            memset(p, 0, sizeof(*p));
            snprintf(p->id, sizeof(p->id), "%.6s", id); p->steps[0] = (char)c; p->base = CP_NONE;   /* (base 0 : repris du Salon) */
            rep_save();
        }
    }
    if (!p || p->cur >= strlen(p->steps)) return;
    int c = p->steps[p->cur];
    switch (c) {
    case RPS_SPEED:   nat_speed = 1; nat_speed_auto = 0; break;
    case RPS_FAST:    nat_speed = 0; nat_speed_auto = 0; break;
    case RPS_TRAP:    nat_trap = 1; break;
    case RPS_NOCARD:  nat_nocard = 1; break;
    case RPS_NOCHEAT: nat_nocheat = 1; break;
    case RPS_NOWS:    nat_ws = 0; break;
    case RPS_VIDEO:   nat_vmode = 0; nat_deflick = 0; nat_wide = 0; break;
    case RPS_BOTH:    nat_speed = 1; nat_speed_auto = 0; nat_trap = 1; break;
    }
    if (p->base == CP_NONE) snprintf(rep_msg, sizeof(rep_msg), "r\xC3\xA9glage d'une autre console : %s", rep_name(c));   /* v145 */
    else snprintf(rep_msg, sizeof(rep_msg), "essai %d / %d : %s", p->cur + 1, (int)strlen(p->steps), rep_name(c));
    if (!p->tried) { p->tried = 1; rep_save(); }
    crumb("reparation %s : %s", id, rep_msg);
}
/* avant de choisir entre Pulse et le lanceur de secours : les cheats sont-ils coupes pour ce lancement ? */
static void rep_pre(const char *id) {
    jc_load(id);
    nat_nocheat = jc_v[JC_NOCHEAT - JC_V0] == 1;
    rep_t *p = rep_find(id);
    if (p && p->cur < strlen(p->steps) && p->steps[p->cur] == RPS_NOCHEAT) nat_nocheat = 1;
}
/* le reglage qui a marche : garde dans les reglages du jeu */
static void rep_keep(const char *id, const char *title, int c) {
    snprintf(jc_id, sizeof(jc_id), "%.6s", id);
    snprintf(jc_title, sizeof(jc_title), "%s", title && title[0] ? title : id);
    jc_load(jc_id);
    switch (c) {
    case RPS_SPEED:   jc_v[JC_SPEED - JC_V0] = 1; break;
    case RPS_FAST:    jc_v[JC_SPEED - JC_V0] = 2; break;
    case RPS_TRAP:    jc_v[JC_TRAP - JC_V0] = 1; break;
    case RPS_NOCARD:  snprintf(jc_card, sizeof(jc_card), "-"); break;
    case RPS_NOCHEAT: jc_v[JC_NOCHEAT - JC_V0] = 1; break;
    case RPS_NOWS:    jc_ws = 0; break;
    case RPS_VIDEO:   jc_v[JC_VIDEO - JC_V0] = 0; jc_v[JC_FLOU - JC_V0] = 0; jc_v[JC_WIDE - JC_V0] = 0; break;
    case RPS_BOTH:    jc_v[JC_SPEED - JC_V0] = 1; jc_v[JC_TRAP - JC_V0] = 1; break;
    }
    jc_save();
}
/* (compat.c) le verdict du dernier lancement : old = verdict d'avant */
static void rep_verdict(const char *id, const char *title, int v, int old) {
    rep_t *p = rep_find(id);
    rep_note_txt[0] = 0;
    if (p && p->base == CP_NONE && p->steps[0]) {         /* v145 : reglage repris d'une autre console du Salon */
        int c = p->steps[0], tried = p->tried;
        rep_drop(id); p = NULL;
        if (tried && (v == CP_OK || v == CP_MAL)) {
            rep_keep(id, title, c); cp_set_fix(id, c);
            snprintf(rep_note_txt, sizeof(rep_note_txt), "R\xC3\xA9glage d'une autre console : %s (gard\xC3\xA9)", rep_name(c));
            if (v == CP_OK) return;
        }
    }
    if (p && p->tried && p->cur < strlen(p->steps)) {
        int c = p->steps[p->cur];
        int better = v == CP_OK || (v == CP_MAL && (p->base == CP_NOIR || p->base == CP_PLANTE));
        if (better) {
            rep_keep(id, title, c); cp_set_fix(id, c);
            if (v == CP_OK) {
                snprintf(rep_note_txt, sizeof(rep_note_txt), "R\xC3\xA9par\xC3\xA9 avec : %s (gard\xC3\xA9)", rep_name(c));
                rep_drop(id);
                return;
            }
            p->base = (u8)v;                                  /* (mieux mais pas encore bien : on continue a partir de la) */
            char s[12];
            int n = rep_plan(id, v, s);
            int k = 0;
            for (int i = 0; i < n; i++) if (s[i] != c && !(c == RPS_BOTH && (s[i] == RPS_SPEED || s[i] == RPS_TRAP))) s[k++] = s[i];
            s[k] = 0;
            snprintf(p->steps, sizeof(p->steps), "%s", s); p->cur = 0; p->tried = 0;
            if (!k) { rep_drop(id); snprintf(rep_note_txt, sizeof(rep_note_txt), "Mieux avec %s (gard\xC3\xA9) ; plus d'autre essai", rep_name(c)); return; }
            rep_save();
            snprintf(rep_note_txt, sizeof(rep_note_txt), "Mieux avec %s (gard\xC3\xA9). Prochain essai : %s", rep_name(c), rep_name(s[0]));
            return;
        }
        if (v == CP_OK) { rep_drop(id); return; }
        p->cur++; p->tried = 0;
        if (p->cur >= strlen(p->steps)) {
            rep_drop(id);
            snprintf(rep_note_txt, sizeof(rep_note_txt), "Aucun essai n'a suffi : envoie le journal (gc-mp3/natif/journal-jeu.txt)");
            return;
        }
        rep_save();
        snprintf(rep_note_txt, sizeof(rep_note_txt), "Au prochain lancement, essai %d / %d : %s", p->cur + 1, (int)strlen(p->steps), rep_name(p->steps[p->cur]));
        return;
    }
    if (v == CP_OK) { if (p) rep_drop(id); return; }
    if (p) { p->tried = 0; rep_save(); snprintf(rep_note_txt, sizeof(rep_note_txt), "Au prochain lancement, essai %d / %d : %s", p->cur + 1, (int)strlen(p->steps), rep_name(p->steps[p->cur])); return; }
    if (v != CP_NOIR && v != CP_PLANTE && v != CP_MAL) return;
    char s[12];
    if (!rep_plan(id, v, s)) return;
    if (rep_n >= RP_MAX) return;
    p = &rep_tab[rep_n++];
    memset(p, 0, sizeof(*p));
    snprintf(p->id, sizeof(p->id), "%.6s", id); snprintf(p->steps, sizeof(p->steps), "%s", s);
    p->base = (u8)v;
    rep_save();
    snprintf(rep_note_txt, sizeof(rep_note_txt), "Au prochain lancement, essai 1 / %d : %s", (int)strlen(s), rep_name(s[0]));
    (void)old;
}
/* ligne de la fenetre de lancement */
static int rep_line(const char *id, char *out, int cap) {
    rep_t *p = rep_find(id);
    if (!p || p->cur >= strlen(p->steps)) return 0;
    if (p->base == CP_NONE) { snprintf(out, cap, "R\xC3\xA9glage d'une autre console : %s", rep_name(p->steps[p->cur])); return 1; }
    snprintf(out, cap, "R\xC3\xA9paration, essai %d / %d : %s", p->cur + 1, (int)strlen(p->steps), rep_name(p->steps[p->cur]));
    return 1;
}

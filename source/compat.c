// GC Station v137 : base de compatibilite des jeux lances avec Pulse. Inclus dans main.c (apres natif.c).
//  - apres chaque lancement : plantage note tout seul ; sinon, au retour, GC Station demande comment ca
//    s'est passe (marche / marche mal / ecran noir) ;
//  - garde le dernier verdict de chaque jeu dans SD:/gc-mp3/compat.txt (code du jeu, verdict, build, date,
//    vitesse du lecteur et mode video utilises) ;
//  - pastille de couleur sur les jaquettes, ligne dans la fenetre de lancement ;
//  - le journal du jeu s'envoie sur le telephone par QR code (partage, sans copier de fichier).

#define CP_FILE GCD "/compat.txt"
#define CP_MAX  512
enum { CP_NONE, CP_OK, CP_MAL, CP_NOIR, CP_PLANTE, CP_N };
static const char *const cp_keys[CP_N] = { "", "ok", "mal", "noir", "plantage" };
static const char *const cp_names[CP_N] = { "jamais essay\xC3\xA9", "marche", "marche mal", "\xC3\xA9" "cran noir / fig\xC3\xA9", "plantage" };
typedef struct { char id[7]; u8 v; u8 slow; u8 vmode; int build; u32 day; char fix; } cp_t;   /* v145 : fix = reglage garde par la reparation (RPS_...) */
static cp_t cp_tab[CP_MAX];
static void rep_verdict(const char *id, const char *title, int v, int old);   /* v140 : reparation automatique (repair.c) */
static char rep_note_txt[200];
static int rep_show = 0;
static int cp_n = -1;
static char cp_ask_id[7] = "", cp_ask_title[72] = "";
static int cp_ask = 0, cp_ask_slow = 0, cp_ask_vmode = 0;
static u64 cp_ask_t = 0;

static col_t cp_col(int v, u8 a) {
    switch (v) {
    case CP_OK:     return RGBA(70, 220, 120, a);
    case CP_MAL:    return RGBA(255, 190, 60, a);
    case CP_NOIR:   return RGBA(255, 120, 60, a);
    case CP_PLANTE: return RGBA(255, 70, 80, a);
    default:        return RGBA(120, 120, 140, a);
    }
}
static u32 cp_today(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    return tm ? (u32)((tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday) : 0;
}
static void cp_load(void) {
    cp_n = 0;
    FILE *f = sd_ok ? fopen(CP_FILE, "r") : NULL;
    if (!f) return;
    char l[160];
    while (fgets(l, sizeof(l), f) && cp_n < CP_MAX) {
        if (l[0] == '#' || strlen(l) < 8) continue;
        char id[8] = "", vk[16] = "";
        int b = 0, sl = 0, vm = 0; unsigned day = 0;
        if (sscanf(l, "%7[^|]|%15[^|]|%d|%u|%d|%d", id, vk, &b, &day, &sl, &vm) < 2) continue;
        cp_t *c = &cp_tab[cp_n];
        memset(c, 0, sizeof(*c));
        snprintf(c->id, sizeof(c->id), "%.6s", id);
        for (int k = 1; k < CP_N; k++) if (!strcmp(vk, cp_keys[k])) c->v = (u8)k;
        if (!c->v) continue;
        c->build = b; c->day = day; c->slow = (u8)(sl != 0); c->vmode = (u8)vm;
        const char *q = l;                               /* v145 : 7e champ : reglage qui a marche */
        for (int k = 0; k < 6 && q; k++) { q = strchr(q, '|'); if (q) q++; }
        if (q && *q > ' ' && *q != '-' && *q != '|') c->fix = *q;
        cp_n++;
    }
    fclose(f);
}
static void cp_save(void) {
    if (!sd_ok || cp_n < 0) return;
    FILE *f = fopen(CP_FILE ".tmp", "w");
    if (!f) return;
    fprintf(f, "# GC Station : jeux essayes avec Pulse (code | verdict | build | date | vitesse vrai lecteur | mode video | reglage qui a marche)\n");
    for (int i = 0; i < cp_n; i++) fprintf(f, "%s|%s|%d|%u|%d|%d|%c\n", cp_tab[i].id, cp_keys[cp_tab[i].v], cp_tab[i].build, (unsigned)cp_tab[i].day, cp_tab[i].slow, cp_tab[i].vmode, cp_tab[i].fix ? cp_tab[i].fix : '-');
    if (fclose(f) == 0) { remove(CP_FILE); rename(CP_FILE ".tmp", CP_FILE); }
}
static cp_t *cp_find(const char *id) {
    if (cp_n < 0) cp_load();
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < cp_n; i++) if (!strncmp(cp_tab[i].id, id, 6)) return &cp_tab[i];
    return NULL;
}
static int cp_verdict(const char *id) { cp_t *c = cp_find(id); return c ? c->v : CP_NONE; }
static void cp_set(const char *id, int v, int slow, int vmode) {
    if (!id || strlen(id) < 6 || v <= CP_NONE || v >= CP_N) return;
    cp_t *c = cp_find(id);
    int old = c ? c->v : CP_NONE;
    if (!c) {
        if (cp_n >= CP_MAX) return;
        c = &cp_tab[cp_n++];
        memset(c, 0, sizeof(*c));
        snprintf(c->id, sizeof(c->id), "%.6s", id);
    }
    if (c->v != v) c->fix = 0;
    c->v = (u8)v; c->build = GCMP3_BUILD; c->day = cp_today(); c->slow = (u8)(slow != 0); c->vmode = (u8)vmode;
    cp_save();
    crumb("compat : %s = %s", id, cp_keys[v]);
    rep_verdict(id, cp_ask_title, v, old);                 /* v140 : essai suivant, ou reglage garde */
    if (rep_note_txt[0]) rep_show = 1;
}
/* (natif.c) apres la conversion du journal : un jeu vient d'etre lance avec Pulse */
static void cp_after_game(const char *id, const char *title, int crashed, int slow, int vmode) {
    if (!id || strlen(id) < 6) return;
    if (crashed) { snprintf(cp_ask_id, sizeof(cp_ask_id), "%.6s", id); snprintf(cp_ask_title, sizeof(cp_ask_title), "%s", title && title[0] ? title : id); cp_set(id, CP_PLANTE, slow, vmode); return; }
    snprintf(cp_ask_id, sizeof(cp_ask_id), "%.6s", id);
    snprintf(cp_ask_title, sizeof(cp_ask_title), "%s", title && title[0] ? title : id);
    cp_ask = 1; cp_ask_slow = slow; cp_ask_vmode = vmode; cp_ask_t = 0;
}
/* v145 : la reparation a garde un reglage pour ce jeu (repair.c) : note avec le verdict (partage avec le Salon) */
static void cp_set_fix(const char *id, int c) {
    cp_t *e = cp_find(id);
    if (!e || e->fix == c) return;
    e->fix = (char)c;
    cp_save();
}
/* ---------- v145 : compatibilite partagee : les verdicts des autres GC Station du Salon ----------
   Toutes les 10 min au plus (onglet JEUX ouvert, Salon actif), un fil a part lit SD:/gc-mp3/compat.txt de chaque
   console vue (/s/get, comme la copie de fichiers du Salon). Un jeu jamais essaye ici montre le resultat obtenu
   ailleurs (pastille en anneau, ligne dans la fiche) ; s'il marche ailleurs avec un reglage, ce reglage est essaye
   au 1er lancement ici (repair.c), et garde s'il marche. */
#define CPP_MAX 512
typedef struct { char id[7]; u8 v, slow; char fix; char who[24]; int build; u32 day; } cpp_t;
static cpp_t cpp_tab[CPP_MAX], cpp_new[CPP_MAX];
static int cpp_n = 0, cpp_new_n = 0;
static volatile int cpp_state = 0;                        /* 0 libre, 1 fil en cours, 2 fini (a publier) */
static u64 cpp_last = 0;
static struct { char ip[16], name[24]; int port; } cpp_h[SALON_MAX];
static int cpp_hn = 0;
static lwp_t cpp_th;
static u8 cpp_stack[16384] __attribute__((aligned(32)));
static int cpp_better(int a, int b) {                     /* (meme jour) verdict a meilleur que b ? */
    static const u8 rank[CP_N] = { 9, 0, 1, 2, 3 };
    return rank[a] < rank[b];
}
static void cpp_parse(char *t, const char *who) {
    for (char *l = strtok(t, "\n"); l; l = strtok(NULL, "\n")) {
        if (l[0] == '#') continue;
        char id[8] = "", vk[16] = "";
        int b = 0, sl = 0, vm = 0; unsigned day = 0;
        if (sscanf(l, "%7[^|]|%15[^|]|%d|%u|%d|%d", id, vk, &b, &day, &sl, &vm) < 2 || strlen(id) != 6) continue;
        int v = 0;
        for (int k = 1; k < CP_N; k++) if (!strcmp(vk, cp_keys[k])) v = k;
        if (!v) continue;
        const char *q = l;
        for (int k = 0; k < 6 && q; k++) { q = strchr(q, '|'); if (q) q++; }
        char fix = q && *q > ' ' && *q != '-' && *q != '|' ? *q : 0;
        cpp_t *e = NULL;
        for (int i = 0; i < cpp_new_n; i++) if (!strcmp(cpp_new[i].id, id)) e = &cpp_new[i];
        if (e && (e->day > day || (e->day == day && !cpp_better(v, e->v)))) continue;   /* (le plus recent, puis le meilleur) */
        if (!e) { if (cpp_new_n >= CPP_MAX) continue; e = &cpp_new[cpp_new_n++]; }
        memset(e, 0, sizeof(*e));
        snprintf(e->id, sizeof(e->id), "%s", id); snprintf(e->who, sizeof(e->who), "%s", who);
        e->v = (u8)v; e->slow = (u8)(sl != 0); e->fix = fix; e->build = b; e->day = day;
    }
}
static void *cpp_thread(void *arg) {
    (void)arg;
    cpp_new_n = 0;
    char *t = (char *)malloc(48 * 1024);
    for (int h = 0; t && h < cpp_hn; h++) {
        s32 fd = nfs_tcp(cpp_h[h].ip, cpp_h[h].port);
        if (fd < 0) continue;
        char req[160];
        int rl = snprintf(req, sizeof(req), "GET /s/get?p=gc-mp3/compat.txt HTTP/1.0\r\nHost: %s\r\n\r\n", cpp_h[h].ip);
        int got = 0;
        if (nfs_send(fd, req, rl))
            while (got < 48 * 1024 - 1) { int r = nfs_recv(fd, t + got, 48 * 1024 - 1 - got, 3000); if (r <= 0) break; got += r; }
        net_close(fd);
        t[got] = 0;
        char *body = strstr(t, "\r\n\r\n");
        int code = 0;
        if (!body || sscanf(t, "HTTP/%*d.%*d %d", &code) != 1 || code != 200) continue;
        cpp_parse(body + 4, cpp_h[h].name);
    }
    free(t);
    cpp_state = 2;
    return NULL;
}
static void cpp_pump(void) {
    if (cpp_state == 2) {                                 /* resultats du fil : publies ici (fil principal) */
        memcpy(cpp_tab, cpp_new, sizeof(cpp_t) * cpp_new_n); cpp_n = cpp_new_n;
        cpp_state = 0;
        crumb("compat : %d jeux vus sur %d autre(s) console(s)", cpp_n, cpp_hn);
        if (gl_sort == 4) gl_apply_sort();
        return;
    }
    if (cpp_state || !salon_on || !net_ok || tab != TAB_GAMES) return;
    if (cpp_last && now_ms() - cpp_last < 600000ull) return;
    cpp_hn = 0;
    for (int i = 0; i < salon_n && cpp_hn < SALON_MAX; i++) {
        if (now_ms() - salon_peers[i].seen > 20000ull || !salon_peers[i].ip[0]) continue;
        snprintf(cpp_h[cpp_hn].ip, sizeof(cpp_h[0].ip), "%s", salon_peers[i].ip);
        snprintf(cpp_h[cpp_hn].name, sizeof(cpp_h[0].name), "%s", salon_peers[i].name);
        cpp_h[cpp_hn].port = salon_peers[i].port ? salon_peers[i].port : 80;
        cpp_hn++;
    }
    if (!cpp_hn) return;
    cpp_last = now_ms();
    cpp_state = 1;
    if (LWP_CreateThread(&cpp_th, cpp_thread, NULL, cpp_stack, sizeof(cpp_stack), 25) < 0) cpp_state = 0;
}
static cpp_t *cpp_find(const char *id) {
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < cpp_n; i++) if (!strncmp(cpp_tab[i].id, id, 6)) return &cpp_tab[i];
    return NULL;
}
/* tri par etat : marche, marche mal, (marche ailleurs), jamais essaye, ecran noir, plantage */
static int cp_rank(const char *id) {
    int v = cp_verdict(id);
    if (v == CP_NONE) { cpp_t *p = cpp_find(id); return p && p->v == CP_OK ? 2 : 3; }
    return v == CP_OK ? 0 : v == CP_MAL ? 1 : v == CP_NOIR ? 4 : 5;
}
/* la question, avec le menu d'actions (lib_extra.c) : 3 s apres le demarrage, si rien d'autre n'est ouvert */
enum { AC_CP_OK = 200, AC_CP_MAL, AC_CP_NOIR, AC_CP_QR, AC_CP_LATER, AC_RP_OK, AC_RP_STOP };
static void rep_drop(const char *id);
static char rep_show_id[7];
static void cp_pump(void) {
    cpp_pump();                                            /* v145 */
    if (rep_show && !am_open && !menu_open && !kb_is_open() && !cp_ask) {   /* v140 : reparation : ce qui va se passer */
        rep_show = 0;
        snprintf(rep_show_id, sizeof(rep_show_id), "%s", cp_ask_id[0] ? cp_ask_id : "");
        am_n = 0; am_sel = 0; am_track = -1; am_list = -1; am_pos = -1;
        snprintf(am_title, sizeof(am_title), "R\xC3\xA9paration automatique : %.50s", cp_ask_title);
        am_item(rep_note_txt, AC_RP_OK, 0);
        if (strstr(rep_note_txt, "prochain")) am_item("Arr\xC3\xAAter les essais pour ce jeu", AC_RP_STOP, 0);
        am_open = 1;
        return;
    }
    if (!cp_ask || am_open || menu_open || kb_is_open()) return;
    if (!cp_ask_t) cp_ask_t = now_ms();
    if (now_ms() - cp_ask_t < 3000) return;
    cp_ask = 0;
    am_n = 0; am_sel = 0; am_track = -1; am_list = -1; am_pos = -1;
    snprintf(am_title, sizeof(am_title), "%.60s : comment ca s'est pass\xC3\xA9 ?", cp_ask_title);
    am_item("Il marche bien", AC_CP_OK, 0);
    am_item("Il marche mal (son, image, ralenti...)", AC_CP_MAL, 0);
    am_item("\xC3\x89" "cran noir ou jeu fig\xC3\xA9", AC_CP_NOIR, 0);
    am_item("Envoyer le journal au t\xC3\xA9l\xC3\xA9phone (QR code)", AC_CP_QR, 0);
    am_item("Plus tard", AC_CP_LATER, 0);
    am_open = 1;
}
static int cp_action(int code) {                         /* 1 = action de la base de compatibilite */
    switch (code) {
    case AC_CP_OK:   cp_set(cp_ask_id, CP_OK, cp_ask_slow, cp_ask_vmode); lib_note("Not\xC3\xA9 : le jeu marche"); return 1;
    case AC_CP_MAL:  cp_set(cp_ask_id, CP_MAL, cp_ask_slow, cp_ask_vmode); lib_note("Not\xC3\xA9 : marche mal (le journal est dans gc-mp3/natif)"); return 1;
    case AC_CP_NOIR: cp_set(cp_ask_id, CP_NOIR, cp_ask_slow, cp_ask_vmode); share_open(NA_JTXT, 0); return 1;
    case AC_CP_QR:   share_open(NA_JTXT, 0); cp_ask = 1; cp_ask_t = now_ms() + 600000ull; return 1;   /* (la question revient apres : pas avant 10 min) */
    case AC_CP_LATER: return 1;
    case AC_RP_OK: return 1;
    case AC_RP_STOP: if (rep_show_id[0]) rep_drop(rep_show_id); lib_note("Essais de r\xC3\xA9paration arr\xC3\xAAt\xC3\xA9s"); return 1;
    }
    return 0;
}
/* pastille sur une jaquette (coin en haut a droite) */
static void cp_badge(const char *id, f32 x, f32 y, f32 r) {
    int v = cp_verdict(id);
    if (v == CP_NONE) {                                    /* v145 : essaye seulement sur une autre console : anneau */
        cpp_t *p = cpp_find(id);
        if (!p) return;
        gx_circle(x, y, r + 1.5f, RGBA(0, 0, 0, 200), RGBA(0, 0, 0, 200));
        gx_circle(x, y, r, cp_col(p->v, 255), cp_col(p->v, 255));
        gx_circle(x, y, r * 0.5f, RGBA(0, 0, 0, 220), RGBA(0, 0, 0, 220));
        return;
    }
    gx_circle(x, y, r + 1.5f, RGBA(0, 0, 0, 200), RGBA(0, 0, 0, 200));
    gx_circle(x, y, r, cp_col(v, 255), cp_col(v, 255));
}
/* ligne pour la fenetre de lancement : "Dernier essai : marche (build 491, 25/10/2026)" */
static const char *rep_name(int c);
static int cp_line(const char *id, char *out, int cap, col_t *c) {
    cp_t *e = cp_find(id);
    if (!e) {                                              /* v145 : sur une autre console du Salon */
        cpp_t *p = cpp_find(id);
        if (!p) return 0;
        int n = snprintf(out, cap, "Sur %s : %s (build %d)", p->who, cp_names[p->v], p->build);
        if (p->fix && n < cap) snprintf(out + n, cap - n, " avec %s", rep_name(p->fix));
        else if (p->slow && n < cap) snprintf(out + n, cap - n, " - vitesse vrai lecteur");
        *c = cp_col(p->v, 255);
        return 1;
    }
    snprintf(out, cap, "Dernier essai : %s (build %d, %02u/%02u/%04u)%s%s", cp_names[e->v], e->build,
             (unsigned)(e->day % 100), (unsigned)(e->day / 100 % 100), (unsigned)(e->day / 10000), e->slow ? " - vitesse vrai lecteur" : "",
             e->fix ? " - r\xC3\xA9" "par\xC3\xA9" : "");
    *c = cp_col(e->v, 255);
    return 1;
}

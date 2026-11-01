// GC-MP3 : onglet TELEPHONE (inclus dans main.c)
// La GameCube sert une petite page web sur le reseau local : on y envoie des
// fichiers sur la carte SD et on telecommande la lecture depuis le telephone.
// Le serveur tourne dans son propre thread : une connexion lente ne bloque
// jamais l'ecran ni le son. Les commandes passent par une petite file lue par
// la boucle principale.
#ifndef PHONE_PORT
#define PHONE_PORT 80
#endif
#include "phone_page.h"
#include "memoire_page.h"                       /* v230 : /memoire */
#include "rapport_page.h"                       /* v234 : /rapport */
#include "qrcodegen.h"
#ifndef SOL_SOCKET
#define SOL_SOCKET 0xfff
#endif

enum { PS_OFF, PS_START, PS_ON, PS_ERR };
static volatile int tp_state = PS_OFF;
static int tp_wanted = 0;                          /* demarrer des que le reseau est pret */
static int phone_auto = 1;                         /* reglage : serveur lance au demarrage */
static lwp_t tp_thread_h;
static u8 tp_stack[48 * 1024] ATTRIBUTE_ALIGN(32);
static char tp_url[64] = "";
static u8 tp_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
static int tp_qr_ok = 0;

/* etat de la lecture pour la page (double tampon : la boucle ecrit, le thread lit) */
static char tp_json[2][3072];
static volatile int tp_json_cur = 0;
static u64 tp_json_t = 0;

/* commandes envoyees par la page */
enum { PC_TOGGLE = 1, PC_NEXT, PC_PREV, PC_STOP, PC_VOLUP, PC_VOLDOWN, PC_PLAY, PC_RADIO, PC_PARTY, PC_LAYOUT, PC_OPEN, PC_MENU, PC_CHECK, PC_SET, PC_UPDATE, PC_SHOW, PC_SALON, PC_TYPE, PC_SHOWNAME, PC_PLAYURL, PC_RADIOADD, PC_TEST, PC_MINIJEU, PC_QPLAY };   /* v142 : photo sur la tele, ecouter avec (Salon) */
static volatile u32 tp_keys = 0;                /* v142 : boutons de la manette du telephone (pris par la boucle) */
static u8 *gl_cover_raw(int k, int *w, int *h, int *iw, int *ih, u32 *len);   /* (gclib.c) jaquette telle qu'elle est dans posters.pak */
static int tl_screen;                                        /* (tools.c : 0 = aucun outil ouvert) */
static char tp_open_url[2048];                 /* "ouvrir sur la tele" (ecrit par le fil du serveur) */
static volatile int tp_open_busy = 0;
typedef struct { int c, i; } phcmd_t;
static phcmd_t tp_q[16];
static volatile int tp_qh = 0, tp_qt = 0;

/* envois en cours / termines (affiches sur la tele) */
static volatile long tp_up_done = 0, tp_up_total = 0;
static volatile int tp_up_active = 0;
static char tp_up_name[96];
static char tp_done_path[4][NAME_LEN];         /* v23 : fichiers recus (onglet TELECHARGEMENTS) */
static volatile int tp_done_w = 0;
static char tp_log[5][128];
static volatile int tp_nlog = 0;
static volatile int tp_rescan = 0;                 /* 1 = musique, 2 = photos, 4 = retro (v224) */
static volatile u64 tp_up_end = 0;                 /* v224 : fin du dernier envoi (relectures apres la serie de fichiers) */
static volatile int tp_apps_new = 0;               /* v224 : un .dol est arrive dans SD:/apps (liste Applis a relire) */
static char tp_add_path[8][NAME_LEN];               /* v27 : morceaux recus a ajouter (sans tout relire) */
static volatile int tp_add_w = 0;
static int tp_add_r = 0;
static volatile u64 tp_last_req = 0;
static volatile long tp_free_mb = -1;
static volatile u64 tp_up_t0 = 0;
/* fourni par la pile reseau recompilee (tools/fast_net.py) ; absent sinon */
extern const int gcmp3_tcp_wnd __attribute__((weak));

/* v222 : le telephone sert de clavier (clavier de GC Station ou de Cube Browser ouvert sur la tele),
   et une photo envoyee du telephone s'affiche tout de suite */
static char tp_type_txt[600];
static int tp_type_go = 0;
static volatile int tp_type_busy = 0;
static char tp_shown_name[NAME_LEN];
static volatile int tp_shown_busy = 0;
int osk_phone_get(char *title, int tcap, char *out, int cap);   /* (cb/osk.c) */
void osk_phone_set(const char *latin1, int accept);
const char *cb_current_url(void);                              /* (cb/browser.c) */
static void tp_lat_utf8(const char *in, char *out, int cap) {      /* Latin-1 (Cube Browser) -> UTF-8 */
    int o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o < cap - 3; p++) {
        if (*p < 0x80) out[o++] = (char)*p;
        else { out[o++] = (char)(0xC0 | (*p >> 6)); out[o++] = (char)(0x80 | (*p & 0x3F)); }
    }
    out[o] = 0;
}
static void tp_any_utf8(const char *in, char *out, int cap) {      /* v262 : deja UTF-8 valide : tel quel ; sinon Latin-1 -> UTF-8 */
    const unsigned char *p = (const unsigned char *)in;
    int ok = 1;
    while (*p && ok) {
        int n = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0) { ok = 0; break; }
        p++;
        for (int k = 0; k < n; k++, p++) if ((*p & 0xC0) != 0x80) { ok = 0; break; }
    }
    if (ok) snprintf(out, cap, "%s", in); else tp_lat_utf8(in, out, cap);
}
static void tp_utf8_lat(const char *in, char *out, int cap) {      /* UTF-8 -> Latin-1 (au-dela : ?) */
    int o = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && o < cap - 1) {
        u32 c = *p++;
        if (c >= 0xC0) {
            int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            c &= 0x3F >> n;
            while (n-- && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
        } else if (c >= 0x80) continue;
        if (c == 0x2019 || c == 0x2018) c = '\'';               /* (apostrophe du telephone) */
        else if (c == 0x201C || c == 0x201D) c = '"';
        out[o++] = (char)(c < 256 ? c : '?');
    }
    out[o] = 0;
}
static const char *tp_kb_label(void) {
    switch (kb_mode) {
    case KB_URL: return "Adresse ou recherche";
    case KB_SEARCH: return "Recherche sur le web";
    case KB_ARCHIVE: return "Recherche sur archive.org";
    case KB_FIELD: return "Champ de la page";
    case KB_RADIO: return "Chercher une radio";
    case KB_PODCAST: return "Chercher un podcast";
    case KB_CITY: return "Ville de la m\xC3\xA9t\xC3\xA9o";
    case KB_RENAME: return "Nouveau nom";
    case KB_MKDIR: return "Nouveau dossier";
    case KB_LIBFIND: return "Chercher dans la musique";
    case KB_PLNAME: return "Nom de la liste";
    case KB_PROFILE: return "Nom du profil";
    case KB_PETNAME: return "Nom du compagnon";
    case KB_GLOBE: return "Chercher sur le globe";
    default: return "Texte";
    }
}
static void tp_type_apply(void) {
    if (kb_open) {
        snprintf(kb_text, sizeof(kb_text), "%s", tp_type_txt);
        int l = (int)strlen(kb_text);
        if (l == (int)sizeof(kb_text) - 1) while (l > 0 && ((unsigned char)kb_text[l - 1] & 0xC0) == 0x80) kb_text[--l] = 0;   /* (lettre coupee) */
        if (l > 0 && (unsigned char)kb_text[l - 1] >= 0xC0) kb_text[--l] = 0;
        kb_cur = l;
        if (tp_type_go) kb_validate();
        return;
    }
    char t[64], lat[520];
    if (tab == TAB_WEB && osk_phone_get(t, sizeof(t), lat, sizeof(lat))) {
        tp_utf8_lat(tp_type_txt, lat, sizeof(lat));
        osk_phone_set(lat, tp_type_go);
    }
}
static int tp_find_photo(const char *name) {
    for (int i = 0; pf && i < npf; i++) if (!strcmp(base_of(pf[i].path), name)) return i;
    return -1;
}

static char *mv_json(void);
void input_phone(float dx, float dy, float wheel, u32 buttons, int acts);   /* (cb/input.c) */
void input_set_cursor(float x, float y);                  /* v303 : (cb/input.c) */
float gfx_width(void);                                    /* (cb/gfx.c : 640 ou 854) */
static int gp_hit(void);                                  /* (tabs_extra.c) */
static int gp_usable(void);
static int crash_recent(char *out, int cap);                     /* v234 : crashlog.c */
static char tp_rurl[600], tp_rname[160];                         /* v253 : radio envoyee par le globe du telephone */
static volatile int tp_rurl_busy = 0;
static volatile int tp_cap = 0;                                  /* v234 : capture de la tele demandee (simulateur : image a dessiner) */
#ifndef GEKKO
extern int pcsim_capture;
extern char pcsim_capture_name[96];
extern void *pcsim_xfb_last;
extern int pcsim_xfb_w, pcsim_xfb_h;
#endif
/* v234 : ce qu'affiche la tele (tampon video YUYV) -> image BMP, envoyee ligne par ligne (sans grosse reservation) */
static int tp_send(s32 c, const void *d, int n);
static void tp_reply(s32 c, int code, const char *ctype, const void *body, int len);
static char nt_id[8];                                      /* v297 : (notice.c) */
static void tp_send_capture(s32 c) {
    const u8 *fb; int w, h;
#ifndef GEKKO
    tp_cap = 1;
    for (int k = 0; k < 400 && tp_cap; k++) usleep(10000);
    fb = (const u8 *)pcsim_xfb_last; w = pcsim_xfb_w; h = pcsim_xfb_h;
#else
    fb = (const u8 *)xfb[displayed_fb]; w = rmode ? rmode->fbWidth : 0; h = rmode ? rmode->xfbHeight : 0;
#endif
    if (!fb || w <= 0 || h <= 0 || w > 720 || h > 576) { tp_reply(c, 503, "text/plain", "capture impossible", 18); return; }
    int stride = (w * 3 + 3) & ~3;
    u32 size = 54 + (u32)stride * h;
    char hd[300];
    int hn = snprintf(hd, sizeof(hd), "HTTP/1.1 200 OK\r\nContent-Type: image/bmp\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", (unsigned)size);
    tp_send(c, hd, hn);
    u8 bh[54]; memset(bh, 0, sizeof(bh));
    #define LE32(p, v) do { (p)[0] = (u8)(v); (p)[1] = (u8)((v) >> 8); (p)[2] = (u8)((v) >> 16); (p)[3] = (u8)((v) >> 24); } while (0)
    bh[0] = 'B'; bh[1] = 'M'; LE32(bh + 2, size); LE32(bh + 10, 54u); LE32(bh + 14, 40u); LE32(bh + 18, (u32)w); LE32(bh + 22, (u32)h);
    bh[26] = 1; bh[28] = 24; LE32(bh + 34, (u32)stride * h);
    #undef LE32
    if (tp_send(c, bh, 54) < 0) return;
    static u8 row[720 * 3 + 4];
    memset(row, 0, sizeof(row));
    for (int y = h - 1; y >= 0; y--) {                         /* (BMP : de bas en haut, BGR) */
        const u8 *s = fb + (size_t)y * w * 2;
        for (int x = 0; x < w; x += 2, s += 4) {
            int u = s[1] - 128, v = s[3] - 128;
            for (int q = 0; q < 2; q++) {
                int yy = (s[q * 2] - 16) * 298;
                int r = (yy + 409 * v + 128) >> 8, g = (yy - 100 * u - 208 * v + 128) >> 8, b = (yy + 516 * u + 128) >> 8;
                u8 *o = row + (x + q) * 3;
                o[0] = (u8)(b < 0 ? 0 : b > 255 ? 255 : b); o[1] = (u8)(g < 0 ? 0 : g > 255 ? 255 : g); o[2] = (u8)(r < 0 ? 0 : r > 255 ? 255 : r);
            }
        }
        if (tp_send(c, row, stride) < 0) return;
    }
}
#include "ecran_jpeg.c"                                         /* v285 : second ecran */
static u8 *tp_ej_buf = NULL;
static int tp_param(const char *q, const char *key, char *out, int cap);
static u32 tp_ej_n = 0;                                          /* (images envoyees : le journal le dit) */
/* v285 : second ecran : GET /api/ecran?w=320|640&q=10..95 -> ce qu'affiche la tele en JPEG */
static void tp_send_ecran(s32 c, const char *q) {
    const u8 *fb; int w, h;
#ifndef GEKKO
    tp_cap = 3;                                                  /* (simulateur : une image dessinee, sans fichier PNG) */
    for (int k = 0; k < 400 && tp_cap; k++) usleep(5000);
    fb = (const u8 *)pcsim_xfb_last; w = pcsim_xfb_w; h = pcsim_xfb_h;
#else
    fb = (const u8 *)xfb[displayed_fb]; w = rmode ? rmode->fbWidth : 0; h = rmode ? rmode->xfbHeight : 0;
#endif
    if (!fb || w <= 0 || h <= 0 || w > 720 || h > 576) { tp_reply(c, 503, "text/plain", "ecran indisponible", 18); return; }
    char v[12];
    tp_param(q ? q : "", "w", v, sizeof(v));
    int s = atoi(v) >= 600 ? 1 : 2;
    tp_param(q ? q : "", "q", v, sizeof(v));
    int qual = v[0] ? atoi(v) : 60;
    int cap = s == 1 ? 256 * 1024 : 96 * 1024;
    if (!tp_ej_buf) tp_ej_buf = (u8 *)malloc(256 * 1024);
    if (!tp_ej_buf) { tp_reply(c, 503, "text/plain", "memoire", 7); return; }
#ifdef GEKKO
    /* (le tampon video est lu par son adresse en cache, apres invalidation : bien plus rapide que sans cache) */
    const u8 *cfb = (const u8 *)MEM_K1_TO_K0(fb);
    DCInvalidateRange((void *)cfb, (u32)w * h * 2);
    fb = cfb;
#endif
    u64 t0 = now_ms();
    int n = ej_encode(fb, w, h, s, qual, tp_ej_buf, cap);
    if (!n) n = ej_encode(fb, w, h, s, qual / 2, tp_ej_buf, cap);   /* (image trop chargee : moins de qualite) */
    if (!n) { tp_reply(c, 503, "text/plain", "image trop grosse", 17); return; }
    tp_ej_n++;
    char hd[300];
    int hn = snprintf(hd, sizeof(hd), "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\nX-Ecran: %dx%d\r\nX-Ms: %u\r\nX-Web: %d\r\n"
                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n", n, w / s, h / s, (unsigned)(now_ms() - t0), tab == TAB_WEB ? 1 : 0);
    if (tp_send(c, hd, hn)) tp_send(c, tp_ej_buf, n);
}
/* v294 : images partielles : GET /api/ecranz?w=320|640&q=&s=<derniere image recue> (voir ecran_jpeg.c, ej_partiel)
   v295 : image suivante preparee d'avance : des qu'une image est envoyee, la console prepare la suivante (le temps que
   le telephone la recoive et redemande) ; a la demande suivante, elle part tout de suite. Le telephone n'attend plus
   que la console capture et compresse : plus d'images par seconde. (Preparee il y a plus de 40 ms et vide : refaite.) */
static u8 *tp_pf_buf = NULL;
static int tp_pf_len = -1, tp_pf_plein = 0, tp_pf_nb = 0, tp_pf_s = 0, tp_pf_q = 0, tp_pf_ms = 0;
static u32 tp_pf_base = 0;
static u64 tp_pf_t = 0;
static const u8 *tp_ecran_fb(int *w, int *h) {
    const u8 *fb;
#ifndef GEKKO
    tp_cap = 3;
    for (int k = 0; k < 400 && tp_cap; k++) usleep(5000);
    fb = (const u8 *)pcsim_xfb_last; *w = pcsim_xfb_w; *h = pcsim_xfb_h;
#else
    fb = (const u8 *)xfb[displayed_fb]; *w = rmode ? rmode->fbWidth : 0; *h = rmode ? rmode->xfbHeight : 0;
#endif
    if (!fb || *w <= 0 || *h <= 0 || *w > 720 || *h > 576) return NULL;
#ifdef GEKKO
    const u8 *cfb = (const u8 *)MEM_K1_TO_K0(fb);
    DCInvalidateRange((void *)cfb, (u32)*w * *h * 2);
    fb = cfb;
#endif
    return fb;
}
static void tp_send_ecranz(s32 c, const char *q) {
    char v[16];
    tp_param(q ? q : "", "w", v, sizeof(v));
    int s = atoi(v) >= 600 ? 1 : 2;
    tp_param(q ? q : "", "q", v, sizeof(v));
    int qual = v[0] ? atoi(v) : 60;
    tp_param(q ? q : "", "s", v, sizeof(v));
    u32 sc = (u32)strtoul(v, NULL, 10);
    if (!tp_ej_buf) tp_ej_buf = (u8 *)malloc(256 * 1024);
    if (!tp_pf_buf) tp_pf_buf = (u8 *)malloc(256 * 1024);
    if (!tp_ej_buf || !tp_pf_buf) { tp_reply(c, 503, "text/plain", "memoire", 7); return; }
    int w = 0, h = 0, plein = 0, nb = 0, n, ms;
    u8 *buf;
    if (tp_pf_len >= 0 && tp_pf_s == s && tp_pf_q == qual && sc == tp_pf_base && !(tp_pf_len == 0 && now_ms() - tp_pf_t > 40)) {
        buf = tp_pf_buf; n = tp_pf_len; plein = tp_pf_plein; nb = tp_pf_nb; ms = tp_pf_ms;   /* (preparee d'avance) */
        w = ej_rw * s; h = ej_rh * s;
    } else {
        const u8 *fb = tp_ecran_fb(&w, &h);
        if (!fb) { tp_reply(c, 503, "text/plain", "ecran indisponible", 18); return; }
        u64 t0 = now_ms();
        buf = tp_ej_buf;
        n = ej_partiel(fb, w, h, s, qual, sc, buf, 256 * 1024, &plein, &nb);
        ms = (int)(now_ms() - t0);
        if (n < 0) { tp_reply(c, 503, "text/plain", "image trop grosse", 17); return; }
    }
    tp_pf_len = -1;
    char hd[360];
    int hn = snprintf(hd, sizeof(hd), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nX-Ecran: %dx%d\r\nX-Seq: %u\r\nX-Plein: %d\r\nX-N: %d\r\n"
                      "X-Ms: %d\r\nX-Web: %d\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", n ? "200 OK" : "204 No Content",
                      plein ? "image/jpeg" : "application/octet-stream", n, w / s, h / s, (unsigned)ej_seq, plein, nb, ms, tab == TAB_WEB ? 1 : 0);
    tp_ej_n++;
    if (!tp_send(c, hd, hn) || (n > 0 && !tp_send(c, buf, n))) return;
    /* la suivante, d'avance (dans l'autre tampon) */
    const u8 *fb = tp_ecran_fb(&w, &h);
    if (!fb) return;
    u64 t0 = now_ms();
    tp_pf_base = ej_seq;
    tp_pf_len = ej_partiel(fb, w, h, s, qual, ej_seq, tp_pf_buf, 256 * 1024, &tp_pf_plein, &tp_pf_nb);
    tp_pf_ms = (int)(now_ms() - t0);
    tp_pf_t = now_ms(); tp_pf_s = s; tp_pf_q = qual;
}
static volatile int tp_touch = 0, tp_tc = 0;             /* v303 : tele tactile : toucher a un endroit de l'image */
static int tp_tap_retry = 0;                              /* (zone pas encore la, ex. barre des onglets qui apparait : reessaye) */
static volatile f32 tp_tu = 0, tp_tv = 0;
static volatile int tp_mdx = 0, tp_mdy = 0, tp_mw = 0, tp_mc = 0;          /* v231 : souris tactile (cumul) */                                     /* v230 : memview.c */
static void tp_push(int c, int i) {
    int n = (tp_qt + 1) % 16;
    if (n == tp_qh) return;                        /* file pleine : on ignore */
    tp_q[tp_qt].c = c;
    tp_q[tp_qt].i = i;
    tp_qt = n;
}

static void tp_logf(const char *m) {
    int k = tp_nlog % 5;
    snprintf(tp_log[k], sizeof(tp_log[k]), "%s", m);
    tp_nlog++;
}

/* --- entrees / sorties sur une connexion (non bloquantes, avec delai) --- */
static int tp_recv(s32 c, void *b, int n, u32 ms) {
    u64 t0 = now_ms();
    for (;;) {
        s32 r = net_recv(c, b, n, 0);
        if (r > 0) return r;
        if (r == 0) return 0;
        if (r != -EAGAIN && r != -EWOULDBLOCK) return -1;
        if (now_ms() - t0 > ms) return -2;
        usleep(3000);
    }
}

static int tp_send(s32 c, const void *d, int n) {
    const char *p = (const char *)d;
    u64 t0 = now_ms();
    while (n > 0) {
        s32 r = net_send(c, p, n > 8192 ? 8192 : n, 0);
        if (r > 0) { p += r; n -= r; t0 = now_ms(); continue; }
        if (r != -EAGAIN && r != -EWOULDBLOCK) return 0;
        if (now_ms() - t0 > 15000) return 0;
        usleep(3000);
    }
    return 1;
}

static void tp_reply(s32 c, int code, const char *ctype, const void *body, int len) {
    char h[256];
    const char *msg = code == 200 ? "OK" : code == 404 ? "Not Found" : code == 400 ? "Bad Request" : "Error";
    int hl = snprintf(h, sizeof(h), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n", code, msg, ctype, len);
    if (tp_send(c, h, hl) && len > 0) tp_send(c, body, len);
}

/* ---------- v55 : partager un fichier (ou un dossier) de la carte SD par QR code ----------
   Onglet FICHIERS, X > Partager : un lien secret (jeton de 8 lettres) vers ce fichier,
   valable tant que GC Station tourne. Le telephone (meme Wi-Fi) l'ouvre en scannant le QR code :
   /p/<jeton>            page avec le nom, la taille, un apercu et le bouton Telecharger
   /p/<jeton>/dl         le fichier (telechargement)       /p/<jeton>/voir   (affiche dans le navigateur)
   dossier : /p/<jeton>/dl/<nom>  et  /p/<jeton>/voir/<nom>  pour chaque fichier du dossier */
#define SH_MAX 8
typedef struct { char tok[12]; char path[NAME_LEN]; u8 dir; } share_t;
static share_t sh_tab[SH_MAX];
static int sh_next = 0;

static const char *sh_mime(const char *name) {
    const char *e = strrchr(name, '.');
    if (!e) return "application/octet-stream";
    static const char *const m[][2] = {
        { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".png", "image/png" }, { ".gif", "image/gif" }, { ".bmp", "image/bmp" },
        { ".webp", "image/webp" }, { ".mp3", "audio/mpeg" }, { ".ogg", "audio/ogg" }, { ".flac", "audio/flac" }, { ".wav", "audio/wav" },
        { ".m4a", "audio/mp4" }, { ".mp4", "video/mp4" }, { ".txt", "text/plain; charset=utf-8" }, { ".ini", "text/plain; charset=utf-8" },
        { ".log", "text/plain; charset=utf-8" }, { ".cfg", "text/plain; charset=utf-8" }, { ".xml", "text/plain; charset=utf-8" },
        { ".json", "text/plain; charset=utf-8" }, { ".m3u", "text/plain; charset=utf-8" }, { ".pdf", "application/pdf" }, { ".zip", "application/zip" } };
    for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++) if (!strcasecmp(e, m[i][0])) return m[i][1];
    return "application/octet-stream";
}
static int sh_kind(const char *name) {                  /* 1 image, 2 son, 3 video, 4 texte, 0 autre */
    const char *t = sh_mime(name);
    return !strncmp(t, "image/", 6) ? 1 : !strncmp(t, "audio/", 6) ? 2 : !strncmp(t, "video/", 6) ? 3 : !strncmp(t, "text/", 5) ? 4 : 0;
}
static void sh_esc(sbuf_t *s, const char *t) {          /* texte -> HTML */
    char b[8];
    for (; *t; t++) {
        if (*t == '<') sb_add(s, "&lt;"); else if (*t == '>') sb_add(s, "&gt;"); else if (*t == '&') sb_add(s, "&amp;");
        else if (*t == '"') sb_add(s, "&quot;"); else { b[0] = *t; b[1] = 0; sb_add(s, b); }
    }
}
static void sh_pct(char *o, int cap, const char *t) {   /* nom -> morceau d'adresse (%XX) */
    int n = 0;
    for (; *t && n < cap - 4; t++) {
        unsigned char ch = (unsigned char)*t;
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o[n++] = (char)ch;
        else n += snprintf(o + n, cap - n, "%%%02X", ch);
    }
    o[n] = 0;
}
static const share_t *sh_find(const char *tok, int tl) {
    if (tl < 6 || tl > 10) return NULL;
    for (int i = 0; i < SH_MAX; i++)
        if (sh_tab[i].tok[0] && (int)strlen(sh_tab[i].tok) == tl && !strncmp(sh_tab[i].tok, tok, tl)) return &sh_tab[i];
    return NULL;
}

/* envoie un fichier par morceaux (avec "Range" : les videos se lisent sur iPhone / Android) */
static void sh_send_file(s32 c, const char *path, int inl, char *req) {
    static u8 fb[32768];
    struct stat st;
    FILE *f = NULL;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode) || (f = fopen(path, "rb")) == NULL) {
        tp_reply(c, 404, "text/plain; charset=utf-8", "Fichier introuvable", 19);
        return;
    }
    long size = (long)st.st_size, a = 0, b = size - 1;
    int part = 0;
    char *r = ci_find(req, "\nrange:");
    if (r && size > 0) {
        r += 7;
        while (*r == ' ') r++;
        if (!strncasecmp(r, "bytes=", 6)) {
            r += 6;
            if (*r == '-') { long n = atol(r + 1); if (n > 0) { a = n > size ? 0 : size - n; part = 1; } }
            else if (isdigit((unsigned char)*r)) {
                char *e;
                a = strtol(r, &e, 10); part = 1;
                if (*e == '-' && isdigit((unsigned char)e[1])) b = strtol(e + 1, NULL, 10);
                if (b > size - 1) b = size - 1;
            }
        }
        if (part && a > b) {
            fclose(f);
            char h[200];
            int hl = snprintf(h, sizeof(h), "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%ld\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", size);
            tp_send(c, h, hl);
            return;
        }
    }
    const char *nm = base_of(path);
    char ascii[160], pct[400];
    int k = 0;
    for (const char *p = nm; *p && k < (int)sizeof(ascii) - 1; p++) ascii[k++] = ((unsigned char)*p < 0x20 || (unsigned char)*p >= 0x7F || *p == '"' || *p == '\\') ? '_' : *p;
    ascii[k] = 0;
    sh_pct(pct, sizeof(pct), nm);
    char h[1024];
    long len = size ? b - a + 1 : 0;
    int hl = snprintf(h, sizeof(h), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\nAccept-Ranges: bytes\r\n",
                      part ? "206 Partial Content" : "200 OK", inl ? sh_mime(nm) : "application/octet-stream", len);
    if (part) hl += snprintf(h + hl, sizeof(h) - hl, "Content-Range: bytes %ld-%ld/%ld\r\n", a, b, size);
    hl += snprintf(h + hl, sizeof(h) - hl, "Content-Disposition: %s; filename=\"%s\"; filename*=UTF-8''%s\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                   inl ? "inline" : "attachment", ascii, pct);
    if (!tp_send(c, h, hl) || !len) { fclose(f); return; }
    if (a && fseek(f, a, SEEK_SET) != 0) { fclose(f); return; }
    while (len > 0) {
        size_t want = len > (long)sizeof(fb) ? sizeof(fb) : (size_t)len;
        size_t n = fread(fb, 1, want, f);
        if (n == 0 || !tp_send(c, fb, (int)n)) break;
        len -= (long)n;
        tp_last_req = now_ms();
    }
    fclose(f);
}

static void sh_page_head(sbuf_t *s, const char *title) {
    sb_add(s, "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
    sh_esc(s, title);
    sb_add(s, "</title><style>body{font-family:sans-serif;background:#120a2e;color:#eee;margin:0;padding:18px}"
              "h2{word-break:break-all;margin:6px 0}.m{color:#a99be0;font-size:14px}"
              ".b{display:block;text-align:center;background:#7a3cf0;color:#fff;text-decoration:none;font-size:20px;padding:16px;border-radius:12px;margin:16px 0}"
              "img,video{max-width:100%;border-radius:8px;margin-top:8px}audio{width:100%;margin-top:8px}"
              ".f{display:flex;justify-content:space-between;gap:10px;padding:12px 0;border-bottom:1px solid #2c2058}"
              ".f a{color:#d7c2ff;text-decoration:none;word-break:break-all}.f span{color:#8f80c8;white-space:nowrap;font-size:14px}"
              ".t{color:#ffd278;font-size:13px;margin-bottom:6px}</style></head><body><div class=\"t\">GC STATION</div>");
}
static void sh_preview(sbuf_t *s, const char *href, const char *name) {
    int k = sh_kind(name);
    if (k == 1) { sb_add(s, "<img src=\""); sb_add(s, href); sb_add(s, "\" alt=\"\">"); }
    else if (k == 2) { sb_add(s, "<audio controls preload=\"none\" src=\""); sb_add(s, href); sb_add(s, "\"></audio>"); }
    else if (k == 3) { sb_add(s, "<video controls preload=\"metadata\" src=\""); sb_add(s, href); sb_add(s, "\"></video>"); }
    else if (k == 4) { sb_add(s, "<p><a style=\"color:#d7c2ff\" href=\""); sb_add(s, href); sb_add(s, ui_en ? "\">Read in the browser</a></p>" : "\">Lire dans le navigateur</a></p>"); }
}
static int sh_cmp(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

/* /p/... ; rend 1 si la requete etait pour le partage */
static int sh_route(s32 c, const char *target, char *req) {
    if (strncmp(target, "/p/", 3)) return 0;
    const char *t = target + 3, *sl = strchr(t, '/');
    int tl = sl ? (int)(sl - t) : (int)strlen(t);
    char tok[12];
    snprintf(tok, sizeof(tok), "%.*s", tl < 11 ? tl : 11, t);
    const share_t *sh = sh_find(t, tl);
    if (!sh || !sd_ok) {
        static const char m[] = "Lien de partage expir\xC3\xA9 (GC Station a \xC3\xA9t\xC3\xA9 relanc\xC3\xA9 ?)";
        tp_reply(c, 404, "text/plain; charset=utf-8", m, (int)sizeof(m) - 1);
        return 1;
    }
    char path[NAME_LEN];
    snprintf(path, sizeof(path), "%s", sh->path);
    int dir = sh->dir;
    const char *rest = sl ? sl : "";
    char sz[32];
    if (!dir) {
        if (!strcmp(rest, "/dl")) { sh_send_file(c, path, 0, req); return 1; }
        if (!strcmp(rest, "/voir")) { sh_send_file(c, path, 1, req); return 1; }
        if (rest[0] && strcmp(rest, "/")) { tp_reply(c, 404, "text/plain; charset=utf-8", "Introuvable", 11); return 1; }
        struct stat st;
        if (stat(path, &st) != 0) { tp_reply(c, 404, "text/plain; charset=utf-8", "Fichier introuvable", 19); return 1; }
        sbuf_t s = { 0 };
        const char *nm = base_of(path);
        char href[40];
        sh_page_head(&s, nm);
        sb_add(&s, "<h2>"); sh_esc(&s, nm); sb_add(&s, "</h2><div class=\"m\">");
        fmt_size(sz, sizeof(sz), (long)st.st_size); sb_add(&s, sz);
        sb_add(&s, "</div><a class=\"b\" href=\"/p/"); sb_add(&s, tok); sb_add(&s, ui_en ? "/dl\">Download</a>" : "/dl\">T\xC3\xA9l\xC3\xA9" "charger</a>");
        snprintf(href, sizeof(href), "/p/%s/voir", tok);
        sh_preview(&s, href, nm);
        sb_add(&s, "</body></html>");
        tp_reply(c, 200, "text/html; charset=utf-8", s.b ? s.b : "", s.b ? (int)strlen(s.b) : 0);
        free(s.b);
        return 1;
    }
    /* dossier */
    int inl = !strncmp(rest, "/voir/", 6);
    if (inl || !strncmp(rest, "/dl/", 4)) {
        char nm[256];
        url_decode(rest + (inl ? 6 : 4), nm, sizeof(nm), 0);
        if (!nm[0] || strchr(nm, '/') || strchr(nm, '\\') || !strcmp(nm, ".") || !strcmp(nm, "..")) {
            tp_reply(c, 400, "text/plain; charset=utf-8", "Nom invalide", 12);
            return 1;
        }
        char p[NAME_LEN];
        snprintf(p, sizeof(p), "%s/%s", path, nm);
        sh_send_file(c, p, inl, req);
        return 1;
    }
    sbuf_t s = { 0 };
    sh_page_head(&s, base_of(path));
    sb_add(&s, "<h2>"); sh_esc(&s, base_of(path)); sb_add(&s, "</h2>");
    DIR *d = opendir(path);
    char *names[500];
    int n = 0;
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL && n < 500) {
        if (e->d_name[0] == '.') continue;
        char p[NAME_LEN];
        snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        if (is_dir(p, e)) continue;
        names[n] = strdup(e->d_name);
        if (names[n]) n++;
    }
    if (d) closedir(d);
    qsort(names, n, sizeof(char *), sh_cmp);
    snprintf(sz, sizeof(sz), ui_en ? "%d files" : "%d fichier%s", n, n > 1 ? "s" : "");
    sb_add(&s, "<div class=\"m\">"); sb_add(&s, sz); sb_add(&s, "</div>");
    for (int i = 0; i < n; i++) {
        char p[NAME_LEN], pct[600], line[700];
        struct stat st;
        snprintf(p, sizeof(p), "%s/%s", path, names[i]);
        sh_pct(pct, sizeof(pct), names[i]);
        snprintf(line, sizeof(line), "<div class=\"f\"><a href=\"/p/%s/%s/%s\">", tok, sh_kind(names[i]) ? "voir" : "dl", pct);
        sb_add(&s, line); sh_esc(&s, names[i]);
        fmt_size(sz, sizeof(sz), stat(p, &st) == 0 ? (long)st.st_size : 0);
        snprintf(line, sizeof(line), "</a><span>%s &nbsp;<a href=\"/p/%s/dl/%s\">\xE2\xAC\x87</a></span></div>", sz, tok, pct);
        sb_add(&s, line);
        free(names[i]);
        if ((i & 31) == 31) tp_last_req = now_ms();
    }
    if (!n) sb_add(&s, ui_en ? "<p>No files in this folder.</p>" : "<p>Aucun fichier dans ce dossier.</p>");
    sb_add(&s, "</body></html>");
    tp_reply(c, 200, "text/html; charset=utf-8", s.b ? s.b : "", s.b ? (int)strlen(s.b) : 0);
    free(s.b);
    return 1;
}

/* --- JSON --- */
static void sb_json_str(sbuf_t *s, const char *t) {
    char e[8];
    t = tr(t);
    sb_add(s, "\"");
    for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
        if (*p == '"' || *p == '\\') { e[0] = '\\'; e[1] = (char)*p; e[2] = 0; sb_add(s, e); }
        else if (*p < 0x20) { snprintf(e, sizeof(e), "\\u%04x", *p); sb_add(s, e); }
        else { e[0] = (char)*p; e[1] = 0; sb_add(s, e); }
    }
    sb_add(s, "\"");
}

/* --- reglages (page "Reglages" du telephone) : construits par la boucle principale --- */
static char *tp_cfg[2] = { NULL, NULL };
static volatile int tp_cfg_cur = 0, tp_cfg_now = 0, tp_cfg_reading = 0;
static u64 tp_cfg_t = 0;
static char tp_set_k[32], tp_set_v[600];
static volatile int tp_set_busy = 0;
static const char *const tp_text_keys[] = { "ville", "actualites", "reveil", "reveil_joue", "telephone", "veilleuse_radio", "maj_auto", "maj_depot", NULL };

static void tp_cfg_build(void) {
    sbuf_t s = { NULL, 0, 0 };
    char v[160], num[32];
    sb_add(&s, "{\"m\":[");
    int first = 1;
    for (int i = 0; i < MI_COUNT; i++) {
        if (i == MI_QUIT || i == MI_ELEMS || i == MI_TABS || i == MI_GAMES || i == MI_TITLES || i == MI_AUTOSTART || i == MI_IP || i == MI_GW) continue;
        menu_value(i, v, sizeof(v));
        if (!first) sb_add(&s, ",");
        first = 0;
        snprintf(num, sizeof(num), "[%d,", i);
        sb_add(&s, num);
        sb_json_str(&s, menu_labels[i]);
        sb_add(&s, ",");
        sb_json_str(&s, v);
        sb_add(&s, i == MI_UPDATE ? ",1]" : ",0]");
    }
    sb_add(&s, "],\"el\":[");
    for (int i = 0; i < EL_COUNT; i++) {
        if (i) sb_add(&s, ",");
        sb_add(&s, "[");
        sb_json_str(&s, el_names[i]);
        sb_add(&s, ck_el_get(i) ? ",1]" : ",0]");
    }
    sb_add(&s, "],\"tb\":[");
    for (int i = 0; i < TAB_COUNT; i++) {
        if (i) sb_add(&s, ",");
        sb_add(&s, "[");
        sb_json_str(&s, tab_names[i]);
        sb_add(&s, tab_shown(i) ? ",1" : ",0");
        sb_add(&s, i == TAB_MUSIC ? ",1]" : ",0]");
    }
    char rv[16];
    if (alarm_on) snprintf(rv, sizeof(rv), "%02d:%02d", alarm_min / 60, alarm_min % 60); else snprintf(rv, sizeof(rv), "non");
    snprintf(num, sizeof(num), "%d", alarm_src);
    const char *tv[] = { city, news_url, rv, num, phone_auto ? "oui" : "non", night_radio ? "oui" : "non", upd_auto ? "oui" : "non", upd_repo };
    sb_add(&s, "],\"t\":{");
    for (int i = 0; tp_text_keys[i]; i++) {
        if (i) sb_add(&s, ",");
        sb_json_str(&s, tp_text_keys[i]);
        sb_add(&s, ":");
        sb_json_str(&s, tv[i]);
    }
    snprintf(num, sizeof(num), "},\"sdv\":%d}", sd_virtual);
    sb_add(&s, num);
    if (s.b) {
        int w = 1 - tp_cfg_cur;
        free(tp_cfg[w]);
        tp_cfg[w] = s.b;
        tp_cfg_cur = w;
    }
}

static u32 nat_pj_stamp(int *r);                         /* v262 : (natif.c) dernier lancement avec Pulse */
static u32 sj_new_t;                                     /* v268 : (succes_jeux.c) derniers succes obtenus dans un jeu */
static char sj_new_txt[200];
static char *sj_json(void);
static char *nat_pj_json(void);
static void tp_json_build(void) {
    np_t n;
    np_get(&n);
    sbuf_t s = { NULL, 0, 0 };
    char num[64], lyr[300] = "";
    if (src == SRC_FILE && playing && lrc_n > 0 && lrc_track == cur_track) {
        int li = lyrics_current();
        if (li >= 0) text_display(lyrics_line(li), lyr, sizeof(lyr));
    }
    int playing_now = (src == SRC_FILE && playing && !paused) || (src == SRC_RADIO && state == ST_PLAY);
    sb_add(&s, "{\"title\":"); sb_json_str(&s, n.title);
    sb_add(&s, ",\"sub\":"); sb_json_str(&s, n.sub);
    sb_add(&s, ",\"info\":"); sb_json_str(&s, n.info);
    sb_add(&s, ",\"lyr\":"); sb_json_str(&s, lyr);
    snprintf(num, sizeof(num), ",\"el\":%d,\"dur\":%d,\"vol\":%d", n.active ? n.elapsed : 0, n.dur, volume_pct); sb_add(&s, num);
    snprintf(num, sizeof(num), ",\"playing\":%d,\"cov\":%d,\"free\":%ld", playing_now, cover_ok && n.active, tp_free_mb); sb_add(&s, num);
    {   /* v262 : dernier lancement avec Pulse et dernier telechargement (notifications du telephone) */
        int pr = 0;
        u32 pw = nat_pj_stamp(&pr);
        snprintf(num, sizeof(num), ",\"pj\":%u,\"pjr\":%d,\"dl\":%u,\"dln\":", (unsigned)pw, pr, (unsigned)ev_dl_n); sb_add(&s, num);
        char dn[200]; tp_any_utf8(ev_dl_name, dn, sizeof(dn)); sb_json_str(&s, dn);
        if (sj_new_t) { snprintf(num, sizeof(num), ",\"ach\":%u,\"acht\":", (unsigned)sj_new_t); sb_add(&s, num); sb_json_str(&s, sj_new_txt); }   /* v268 */
    }
    {   /* v222 : clavier ouvert sur la tele -> le telephone propose de taper */
        char kt[64] = "", lat[520], u8[1100];
        int k = 0;
        if (kb_open) { k = 1; snprintf(u8, sizeof(u8), "%s", kb_text); snprintf(kt, sizeof(kt), "%s", tp_kb_label()); }
        else if (tab == TAB_WEB && !menu_open && !tl_screen && (k = osk_phone_get(kt, sizeof(kt), lat, sizeof(lat))) != 0) { tp_lat_utf8(lat, u8, sizeof(u8)); char t2[64]; snprintf(t2, sizeof(t2), "%s", kt); tp_lat_utf8(t2, kt, sizeof(kt)); }
        if (k) {
            if (strlen(u8) > 700) u8[700] = 0;
            snprintf(num, sizeof(num), ",\"kb\":%d,\"kbh\":", k); sb_add(&s, num); sb_json_str(&s, kt);
            sb_add(&s, ",\"kbt\":"); sb_json_str(&s, u8);
        }
    }
    if (tab == TAB_WEB) {                               /* v222 : la page ouverte dans Cube Browser */
        const char *cu = cb_current_url();
        if (cu && (!strncmp(cu, "http://", 7) || !strncmp(cu, "https://", 8))) { char u8[1100]; tp_lat_utf8(cu, u8, sizeof(u8)); sb_add(&s, ",\"web\":"); sb_json_str(&s, u8); }
    }
    sb_add(&s, "}");
    if (s.b) {
        int w = 1 - tp_json_cur;
        snprintf(tp_json[w], sizeof(tp_json[w]), "%s", s.b);
        tp_json_cur = w;
        free(s.b);
    }
}

/* liste des morceaux : [["titre","artiste"],...] */
static void tp_send_tracks(s32 c) {
    sbuf_t s = { NULL, 0, 0 };
    char raw[NAME_LEN], d[NAME_LEN];
    sb_add(&s, "[");
    int n = nfiles;
    for (int k = 0; k < n; k++) {
        track_label(k, raw, sizeof(raw), 1);
        text_display(raw, d, sizeof(d));
        sb_add(&s, k ? ",[" : "[");
        sb_json_str(&s, d);
        sb_add(&s, ",");
        text_display(tg_done[k] ? tg_artist[k] : "", d, sizeof(d));
        sb_json_str(&s, d);
        sb_add(&s, "]");
    }
    sb_add(&s, "]");
    tp_reply(c, 200, "application/json", s.b, s.b ? s.n : 0);
    free(s.b);
}

static void tp_send_stations(s32 c) {
    sbuf_t s = { NULL, 0, 0 };
    char d[200];
    sb_add(&s, "[");
    for (int k = 0; k < nstations; k++) {
        if (k) sb_add(&s, ",");
        text_display(stations[k].name, d, sizeof(d));
        sb_json_str(&s, d);
    }
    sb_add(&s, "]");
    tp_reply(c, 200, "application/json", s.b, s.b ? s.n : 0);
    free(s.b);
}

/* pochette en cours (texture 128x128) -> image BMP */
static void tp_send_cover(s32 c) {
    if (!cover_ok) { tp_reply(c, 404, "text/plain", "", 0); return; }
    const int W = COVER_SZ, H = COVER_SZ, row = W * 3, size = 54 + row * H;
    unsigned char *b = (unsigned char *)malloc(size);
    if (!b) { tp_reply(c, 500, "text/plain", "", 0); return; }
    memset(b, 0, 54);
    b[0] = 'B'; b[1] = 'M';
    b[2] = size & 255; b[3] = (size >> 8) & 255; b[4] = (size >> 16) & 255;
    b[10] = 54; b[14] = 40;
    b[18] = W & 255; b[19] = W >> 8; b[22] = H & 255; b[23] = H >> 8;
    b[26] = 1; b[28] = 24;
    for (int y = 0; y < H; y++) {
        unsigned char *o = b + 54 + (H - 1 - y) * row;
        for (int x = 0; x < W; x++) {
            u16 t = cover_tex[((y >> 2) * (W / 4) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)];
            o[x * 3 + 2] = (u8)(((t >> 11) & 31) * 255 / 31);
            o[x * 3 + 1] = (u8)(((t >> 5) & 63) * 255 / 63);
            o[x * 3 + 0] = (u8)((t & 31) * 255 / 31);
        }
    }
    tp_reply(c, 200, "image/bmp", b, size);
    free(b);
}

/* ---------- v73 : fichiers de la carte SD et jeux, depuis le telephone ----------
   Les chemins voyagent en hexadecimal (les noms de la carte ne sont pas toujours en UTF-8) ;
   on entre dans un dossier par son numero dans la liste : rien ne sort de la carte SD. */
static void x73_json_raw(sbuf_t *s, const char *t) {          /* (texte deja en UTF-8, sans traduction) */
    char e[8];
    sb_add(s, "\"");
    for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
        if (*p == '"' || *p == '\\') { e[0] = '\\'; e[1] = (char)*p; e[2] = 0; sb_add(s, e); }
        else if (*p < 0x20) { snprintf(e, sizeof(e), "\\u%04x", *p); sb_add(s, e); }
        else { e[0] = (char)*p; e[1] = 0; sb_add(s, e); }
    }
    sb_add(s, "\"");
}
static int tp_param(const char *q, const char *key, char *out, int cap);
static void c8_api(s32 c, const char *q);              /* v276 : jeu de cartes (cartes.c) */
static void c8_tick(void);
static void sx_phone_start(int m);                     /* v278 : test de la console lance depuis le telephone (selftest_keys.c) */
static int sx_phone_state(char *o, int cap);
static int gm_phone_list(char *o, int cap);              /* v279 : mini-jeux (tabs_extra.c) */
static void gm_phone_launch(int g);
static char *x73_gj = NULL;                              /* JSON des jeux (construit par la boucle : extras73.c) */
/* v301 : jaquettes et disques envoyes par le telephone (l'appli les telecharge sur GameTDB) :
   x73_jq : "<ID><0|1>\n" par jeu (1 : jaquette de la liste deja la), construit par la boucle (extras73.c) ;
   /api/jaqliste regarde le reste (complete, HD, disque) dans le fil du serveur ; PUT /upload?dir=jaquette&nid=<ID>&kind=c|f|h|d */
static char *x73_jq = NULL;
static volatile int x73_jq_want = 0, x73_jq_ready = 0;
static volatile int tp_jaq_new = 0;                       /* (une image est arrivee : la boucle relit les jaquettes) */
static void gl_art_recu(void);                            /* (gclib.c) */
static volatile int x73_gj_want = 0, x73_gj_ready = 0;
static volatile int x73_launch_k = -1, x73_launch_m = 0;
static char x73_launch_id[8] = "";
static char x73_launch_fx[12] = "";                       /* v250 : patchs de secours (lettres SFTCHWV) */
/* v191 : ton recap (page /recap du telephone ; JSON fait par la boucle : recap.c) */
static char *x73_rj = NULL;
static volatile int x73_rj_want = 0, x73_rj_ready = 0;
/* v191 : ma collection (page /collection du telephone, a imprimer) */
static const char collection_page[] =
"<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Ma collection GameCube</title>\n"
"<style>body{margin:0;font-family:system-ui,sans-serif;background:#120c2c;color:#fff}.w{max-width:980px;margin:auto;padding:18px 14px 40px}\n"
"h1{margin:4px 0}.s{opacity:.75;margin:4px 0 14px}.bar{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}.bar *{font:inherit;padding:8px 12px;border-radius:10px;border:1px solid #4b3a8a;background:#241a55;color:#fff}\n"
".g{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:12px}.g div{background:#1d1545;border-radius:10px;overflow:hidden;text-align:center;padding-bottom:8px;break-inside:avoid}\n"
".g canvas{width:100%;display:block;background:#2a2150;aspect-ratio:3/4}.g b{display:block;font-size:13px;margin:6px 6px 2px}.g small{display:block;font-size:11px;opacity:.75}\n"
"@media print{body{background:#fff;color:#000}.bar{display:none}.g div{background:#fff;border:1px solid #bbb}.g canvas{background:#eee}}</style></head>\n"
"<body><div class=\"w\"><h1>Ma collection GameCube</h1><div class=\"s\" id=\"n\">Chargement...</div>\n"
"<div class=\"bar\"><select id=\"o\" onchange=\"r()\"><option value=\"0\">A à Z</option><option value=\"1\">Temps de jeu</option><option value=\"2\">Favoris d'abord</option></select><button onclick=\"print()\">Imprimer / PDF</button></div>\n"
"<div class=\"g\" id=\"g\"></div></div>\n"
"<script>\n"
"function rgb(v){return[(v>>11&31)*255/31|0,(v>>5&63)*255/63|0,(v&31)*255/31|0]}\n"
"function mx(a,b,x,y,z){return[(a[0]*x+b[0]*y)/z|0,(a[1]*x+b[1]*y)/z|0,(a[2]*x+b[2]*y)/z|0]}\n"
"function cmpr(buf,w,h){var d=new DataView(buf),im=new ImageData(w,h),p=im.data,o=0;for(var ty=0;ty<h;ty+=8)for(var tx=0;tx<w;tx+=8)for(var s=0;s<4;s++){var bx=tx+(s&1)*4,by=ty+(s>>1)*4,c0=d.getUint16(o),c1=d.getUint16(o+2),bt=d.getUint32(o+4);o+=8;var a=rgb(c0),b=rgb(c1),cs=c0>c1?[a,b,mx(a,b,2,1,3),mx(a,b,1,2,3)]:[a,b,mx(a,b,1,1,2),[0,0,0,0]];for(var y=0;y<4;y++)for(var x=0;x<4;x++){var c=cs[(bt>>>(30-2*(y*4+x)))&3],q=((by+y)*w+bx+x)*4;p[q]=c[0];p[q+1]=c[1];p[q+2]=c[2];p[q+3]=c.length>3?c[3]:255}}return im}\n"
"var J=[],Q=[];function d(m){return m>=60?Math.floor(m/60)+\" h \"+String(m%60).padStart(2,\"0\"):m+\" min\"}\n"
"function nx(){var x=Q.shift();if(!x)return;fetch(\"/api/cover?i=\"+x[0]).then(r=>{if(!r.ok)throw 0;var s=(r.headers.get(\"X-Size\")||\"256x256x192x256\").split(\"x\");return r.arrayBuffer().then(b=>[b,+s[0],+s[1],+s[2]||+s[0],+s[3]||+s[1]])}).then(a=>{var im=cmpr(a[0],a[1],a[2]),c=x[1];c.width=a[3];c.height=a[4];c.getContext(\"2d\").putImageData(im,0,0)}).catch(()=>{}).finally(nx)}\n"
"function r(){var o=+document.getElementById(\"o\").value,L=J.slice();L.sort(o==1?(a,b)=>b[3]-a[3]:o==2?(a,b)=>(b[4]-a[4])||a[0].localeCompare(b[0]):(a,b)=>a[0].localeCompare(b[0]));var G=document.getElementById(\"g\");G.innerHTML=\"\";Q=[];\n"
"L.forEach(g=>{var e=document.createElement(\"div\"),c=document.createElement(\"canvas\");c.width=96;c.height=128;e.appendChild(c);var b=document.createElement(\"b\");b.textContent=(g[4]?\"\\u2605 \":\"\")+g[0];e.appendChild(b);var s=document.createElement(\"small\");s.textContent=g[1]+(g[3]?\" \\u00b7 \"+d(g[3]):\"\")+(g[2]?\" \\u00b7 \"+g[2]+\"x\":\"\");e.appendChild(s);G.appendChild(e);Q.push([g[6],c])});nx()}\n"
"fetch(\"/api/games\").then(r=>r.json()).then(j=>{J=j.map((g,i)=>g.concat([i])).filter(g=>g[1]);var t=J.reduce((a,g)=>a+g[3],0);document.getElementById(\"n\").textContent=J.length+\" jeux \\u00b7 \"+d(t)+\" de jeu (Pulse) \\u00b7 GC Station\";r()})\n"
"</script></body></html>\n"
;
static const char recap_page[] =
"<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Ton récap - GC Station</title>\n"
"<style>body{margin:0;font-family:system-ui,sans-serif;background:linear-gradient(160deg,#1b0f4a,#3b0d5c 45%,#0d2a5c);color:#fff;min-height:100vh}\n"
".w{max-width:520px;margin:auto;padding:22px 16px 40px}h1{font-size:30px;margin:6px 0 2px}.s{opacity:.75;margin-bottom:18px}\n"
".k{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin:14px 0}.k div{background:rgba(255,255,255,.09);border-radius:14px;padding:14px 6px;text-align:center}\n"
".k b{display:block;font-size:24px;color:#ffd27a}.k span{font-size:12px;opacity:.8}.c{background:rgba(255,255,255,.08);border-radius:16px;padding:14px 16px;margin:12px 0}\n"
".c h2{font-size:17px;margin:0 0 8px;color:#c9b6ff}ol{margin:0;padding-left:22px}li{margin:6px 0}li small{float:right;opacity:.7}.f{text-align:center;opacity:.6;font-size:13px;margin-top:20px}</style></head>\n"
"<body><div class=\"w\"><h1 id=\"t\">Ton récap</h1><div class=\"s\">GC Station sur ta GameCube</div>\n"
"<div class=\"k\"><div><b id=\"pl\">-</b><span>écoutes</span></div><div><b id=\"tj\">-</b><span>de jeu</span></div><div><b id=\"sc\">-</b><span>succès</span></div></div>\n"
"<div class=\"c\"><h2>Tes morceaux préférés</h2><ol id=\"mt\"></ol></div><div class=\"c\"><h2>Tes artistes</h2><ol id=\"ar\"></ol></div>\n"
"<div class=\"c\"><h2>Tes jeux préférés</h2><ol id=\"je\"></ol></div><div class=\"c\"><h2>Tes records</h2><ol id=\"re\"></ol></div>\n"
"<div class=\"f\">Fais une capture d'écran pour le partager.</div></div>\n"
"<script>function d(s){var m=Math.floor(s/60);return m>=60?Math.floor(m/60)+\" h \"+String(m%60).padStart(2,\"0\"):m+\" min\"}\n"
"function L(id,a,f){var o=document.getElementById(id);o.innerHTML=\"\";if(!a.length){o.innerHTML=\"<li>(rien encore)</li>\";return}a.forEach(x=>{var li=document.createElement(\"li\");li.textContent=x[0];var s=document.createElement(\"small\");s.textContent=f(x);li.appendChild(s);o.appendChild(li)})}\n"
"fetch(\"/api/recap\").then(r=>r.json()).then(j=>{if(j.profil)document.getElementById(\"t\").textContent=\"Ton récap : \"+j.profil;document.getElementById(\"pl\").textContent=j.plays;document.getElementById(\"tj\").textContent=d(j.play);document.getElementById(\"sc\").textContent=j.sc+\"/\"+j.scn;\n"
"L(\"mt\",j.top,x=>x[1]+\" fois\");L(\"ar\",j.art,x=>x[1]+\" fois\");L(\"je\",j.jeux,x=>d(x[1]));L(\"re\",j.rec.filter(x=>x[1]>0),x=>x[1])})</script></body></html>\n"
;
/* v240 : cheats d'un jeu depuis le telephone (avant de le lancer) ; travail fait par la boucle (cheats.c) */
#define CH_PHONE_MAX 400                                 /* (= CH_MAXC : cheats par fichier) */
#define CH_PHONE_MAX_NAME 128
/* v242 : autres demandes du telephone faites par la boucle principale (extras73.c : x73_misc_pump).
   op : 1 cheats modifies, 2 reglages du jeu, 3 favori, 4 paroles, 5 ajout a la file, 6 copie des reglages, 7 restauration */
#define TP_REGTEL GCD_REG "/sauvegarde-telephone.txt"
static volatile int x73_mo_op = 0, x73_mo_ready = 0;
static int x73_mo_i = 0, x73_mo_d = 0, x73_mo_n = 0;
static char x73_mo_id[8], x73_mo_name[CH_PHONE_MAX_NAME], x73_mo_codes[2100];
static char *x73_mo_res = NULL;
static char *x73_mo_run(int op) {                         /* (fil du serveur) attend la boucle, au plus 10 s ; rend une copie */
    x73_mo_ready = 0; x73_mo_op = op;
    for (int k = 0; k < 500 && !x73_mo_ready; k++) usleep(20000);
    if (!x73_mo_ready) return NULL;
    char *t = x73_mo_res ? strdup(x73_mo_res) : NULL;
    return t;
}
static char *x73_chj = NULL;                             /* reponse : JSON de la liste */
static char x73_ch_id[8], x73_ch_set[CH_PHONE_MAX + 1];
static volatile int x73_ch_want = 0, x73_ch_ready = 0;  /* want : 1 lire la liste, 2 ecrire le choix (x73_ch_set) */
/* v190 : sauvegardes des cartes memoire virtuelles depuis le telephone (travail fait par la boucle : extras73.c) */
static char *x73_cj = NULL;                              /* JSON des cartes et de leurs sauvegardes */
static volatile int x73_cj_want = 0, x73_cj_ready = 0;
static char x73_vj_card[64], x73_vj_name[200], x73_vj_res[NAME_LEN + 96];
static volatile int x73_vj_op = 0, x73_vj_i = 0, x73_vj_done = 0, x73_vj_ok = 0;   /* op : 1 sauvegarde -> .gci, 2 .gci -> carte, 3 carte remplacee */
static int x73_vj_run(int op, const char *card, int i, const char *name) {   /* (fil du serveur) attend la boucle, au plus 30 s */
    if (x73_vj_op) return 0;
    snprintf(x73_vj_card, sizeof(x73_vj_card), "%s", card); snprintf(x73_vj_name, sizeof(x73_vj_name), "%s", name ? name : "");
    x73_vj_i = i; x73_vj_done = 0; x73_vj_ok = 0; x73_vj_res[0] = 0; x73_vj_op = op;
    for (int k = 0; k < 1500 && !x73_vj_done; k++) usleep(20000);
    if (!x73_vj_done) { x73_vj_op = 0; return 0; }
    return 1;
}
static void tp_hex_enc(const char *s, char *o, int cap) {
    static const char hx[] = "0123456789abcdef";
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n + 2 < cap; p++) { o[n++] = hx[*p >> 4]; o[n++] = hx[*p & 15]; }
    o[n] = 0;
}
static int tp_hex_dec(const char *h, char *o, int cap) {
    int n = 0;
    for (; h[0] && h[1]; h += 2) {
        int a = isdigit((unsigned char)h[0]) ? h[0] - '0' : (tolower((unsigned char)h[0]) - 'a' + 10);
        int b = isdigit((unsigned char)h[1]) ? h[1] - '0' : (tolower((unsigned char)h[1]) - 'a' + 10);
        if (a < 0 || a > 15 || b < 0 || b > 15 || n >= cap - 1) return 0;
        o[n++] = (char)(a * 16 + b);
    }
    o[n] = 0;
    return 1;
}
typedef struct { char *name; u8 dir; long size; } tpf_t;
static int tpf_cmp(const void *a, const void *b) {
    const tpf_t *x = (const tpf_t *)a, *y = (const tpf_t *)b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}
/* liste d'un dossier de la carte SD (dossiers d'abord) ; retourne le nombre */
static int tp_list_dir(const char *dir, tpf_t **out) {
    *out = NULL;
    int sl = (int)strlen(SD_ROOT);
    if (strncmp(dir, SD_ROOT, sl) || (dir[sl] && dir[sl] != '/') || strstr(dir, "/..")) return -1;
    char od[NAME_LEN + 4];
    snprintf(od, sizeof(od), "%s%s", dir, dir[sl] ? "" : "/");
    DIR *d = opendir(od);
    if (!d) return -1;
    int n = 0, cap = 64;
    tpf_t *v = (tpf_t *)malloc(cap * sizeof(tpf_t));
    struct dirent *e;
    while (v && (e = readdir(d)) != NULL && n < 3000) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n == cap) { tpf_t *nv = (tpf_t *)realloc(v, cap * 2 * sizeof(tpf_t)); if (!nv) break; v = nv; cap *= 2; }
        char p[NAME_LEN + 260];
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        struct stat st;
        int isd = 0; long sz = 0;
        if (stat(p, &st) == 0) { isd = S_ISDIR(st.st_mode); sz = (long)st.st_size; }
        v[n].name = strdup(e->d_name); v[n].dir = (u8)isd; v[n].size = sz;
        if (v[n].name) n++;
    }
    closedir(d);
    if (v) qsort(v, n, sizeof(tpf_t), tpf_cmp);
    *out = v;
    return v ? n : -1;
}
static void tp_free_list(tpf_t *v, int n) { for (int i = 0; i < n; i++) free(v[i].name); free(v); }
/* chemin d'apres h (dossier, en hexadecimal) et i (numero d'une entree, ou absent) */
static int tp_fs_path(const char *q, char *out, int cap, int *isdir) {
    char h[2 * NAME_LEN + 4], is[16], dir[NAME_LEN];
    tp_param(q, "h", h, sizeof(h));
    tp_param(q, "i", is, sizeof(is));
    if (!h[0]) snprintf(dir, sizeof(dir), "%s", SD_ROOT);
    else if (!tp_hex_dec(h, dir, sizeof(dir))) return 0;
    if (!is[0]) { snprintf(out, cap, "%s", dir); *isdir = 1; return 1; }
    tpf_t *v; int n = tp_list_dir(dir, &v), i = atoi(is), ok = 0;
    if (n > 0 && i >= 0 && i < n && (int)(strlen(dir) + strlen(v[i].name) + 2) < cap) { snprintf(out, cap, "%s/%s", dir, v[i].name); *isdir = v[i].dir; ok = 1; }
    if (n >= 0) tp_free_list(v, n);
    return ok;
}
static void tp_send_files(s32 c, const char *q) {
    char dir[NAME_LEN], hx[2 * NAME_LEN + 4], d[NAME_LEN * 2];
    int isdir = 0;
    if (!tp_fs_path(q, dir, sizeof(dir), &isdir) || !isdir) { tp_reply(c, 400, "application/json", "{}", 2); return; }
    tpf_t *v; int n = tp_list_dir(dir, &v);
    if (n < 0) { tp_reply(c, 404, "application/json", "{}", 2); return; }
    sbuf_t s = { NULL, 0, 0 };
    text_display(dir, d, sizeof(d));
    sb_add(&s, "{\"d\":"); x73_json_raw(&s, d);
    tp_hex_enc(dir, hx, sizeof(hx));
    sb_add(&s, ",\"h\":\""); sb_add(&s, hx); sb_add(&s, "\",\"uh\":");
    int sl = (int)strlen(SD_ROOT);
    if (dir[sl]) {                                         /* dossier parent */
        char up[NAME_LEN];
        snprintf(up, sizeof(up), "%s", dir);
        char *k = strrchr(up, '/');
        if (k && k - up >= sl) *k = 0;
        tp_hex_enc(up, hx, sizeof(hx));
        sb_add(&s, "\""); sb_add(&s, hx); sb_add(&s, "\"");
    } else sb_add(&s, "null");
    sb_add(&s, ",\"e\":[");
    for (int i = 0; i < n; i++) {
        char m[48];
        if (i) sb_add(&s, ",");
        text_display(v[i].name, d, sizeof(d));
        sb_add(&s, "["); x73_json_raw(&s, d);
        snprintf(m, sizeof(m), ",%d,%ld]", v[i].dir, v[i].size);
        sb_add(&s, m);
    }
    sb_add(&s, "]}");
    tp_free_list(v, n);
    tp_reply(c, 200, "application/json", s.b ? s.b : "{}", s.b ? (int)strlen(s.b) : 2);
    free(s.b);
}

static void tp_update_free(void);
/* ---- v236 : gerer la carte SD depuis le telephone : supprimer, renommer, nouveau dossier, couper / coller ----
   Chaque element est designe comme pour la liste : dossier (h, en hexadecimal) + numero dans la liste (i), relu
   ici. Toujours sur la carte SD, jamais "..", jamais le dossier gc-mp3 lui-meme ni le morceau en cours. */
static int tp_fs_ok(const char *p) {
    int sl = (int)strlen(SD_ROOT);
    return !strncmp(p, SD_ROOT, sl) && (!p[sl] || p[sl] == '/') && !strstr(p, "/..");
}
static int tp_fs_protected(const char *p) {             /* la racine, SD:/gc-mp3 (reglages...), le morceau qui joue */
    int sl = (int)strlen(SD_ROOT);
    if (!p[sl] || !p[sl + 1]) return 1;
    if (!strcasecmp(p + sl, "/gc-mp3")) return 1;
    if (src == SRC_FILE && cur_track >= 0 && cur_track < nfiles) {
        size_t l = strlen(p);
        if (!strcasecmp(names[cur_track], p) || (!strncasecmp(names[cur_track], p, l) && names[cur_track][l] == '/')) return 1;
    }
    return 0;
}
static int tp_fs_name(const char *in, char *out, int cap) {   /* nom seul, sans caracteres interdits sur une carte FAT */
    int n = 0;
    for (const char *b = in; *b && n < cap - 1; b++) {
        unsigned char ch = (unsigned char)*b;
        if (ch < 32 || strchr("/\\:*?\"<>|", ch)) continue;
        out[n++] = (char)ch;
    }
    out[n] = 0;
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) out[--n] = 0;
    while (out[0] == ' ' || out[0] == '.') memmove(out, out + 1, strlen(out));
    return out[0] != 0;
}
static int tp_rm_tree(const char *p, int depth) {        /* (les noms sont lus d'abord : pas de suppression pendant la lecture du dossier) */
    struct stat st;
    if (stat(p, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return remove(p);
    if (depth > 12) return -1;
    tpf_t *v; int n = tp_list_dir(p, &v), r = 0;
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        char c[NAME_LEN];
        if (snprintf(c, sizeof(c), "%s/%s", p, v[i].name) >= (int)sizeof(c)) { r = -1; continue; }
        if (tp_rm_tree(c, depth + 1)) r = -1;
    }
    tp_free_list(v, n);
    return r ? r : remove(p);
}
static void tp_fs_touched(const char *p) {               /* listes a relire (apres la serie d'operations) */
    int sl = (int)strlen(SD_ROOT);
    const char *t = p + sl + (p[sl] == '/');
    if (!strncasecmp(t, "mp3", 3) || !strncasecmp(t, "downloads", 9) || !strncasecmp(t, "gc-mp3/podcasts", 15)) tp_rescan |= 1;
    if (!strncasecmp(t, "retro", 5)) tp_rescan |= 1 | 4;
    if (!strncasecmp(t, "photos", 6) || !strncasecmp(t, "videos", 6) || !strncasecmp(t, "ecran", 5)) tp_rescan |= 2;
    if (!strncasecmp(t, "apps", 4) || !strchr(t, '/')) tp_apps_new = 1;
    tp_up_end = now_ms();
}
static int tp_fs_check_name(const char *q, const char *path) {   /* x = nom affiche par le telephone : 1 si ce n'est plus lui */
    char x[200], d[NAME_LEN * 2];
    tp_param(q, "x", x, sizeof(x));
    if (!x[0]) return 0;
    text_display(base_of(path), d, sizeof(d));
    return strcmp(x, d) != 0;
}
static void tp_fsop(s32 c, const char *q) {
    char op[8], nm[200], clean[200], src[NAME_LEN], dir[NAME_LEN], dst[NAME_LEN], h[2 * NAME_LEN + 4];
    const char *m = "Op\xC3\xA9ration impossible";
    int ok = 0, isdir = 1;
    tp_param(q, "op", op, sizeof(op));
    tp_param(q, "n", nm, sizeof(nm));
    tp_param(q, "h", h, sizeof(h));
    if (!h[0]) snprintf(dir, sizeof(dir), "%s", SD_ROOT); else if (!tp_hex_dec(h, dir, sizeof(dir))) dir[0] = 0;
    if (!sd_ok || !dir[0] || !tp_fs_ok(dir)) m = "Dossier introuvable";
    else if (!strcmp(op, "mkdir")) {
        if (!tp_fs_name(nm, clean, sizeof(clean))) m = "Nom invalide";
        else if (snprintf(dst, sizeof(dst), "%s/%s", dir, clean) >= (int)sizeof(dst)) m = "Nom trop long";
        else { struct stat st; if (stat(dst, &st) == 0) m = "Ce nom existe d\xC3\xA9j\xC3\xA0"; else if (mkdir(dst, 0777) == 0) { ok = 1; m = "Dossier cr\xC3\xA9\xC3\xA9"; tp_fs_touched(dst); } }
    } else if (!tp_fs_path(q, src, sizeof(src), &isdir) || !strcmp(src, dir)) m = "\xC3\x89l\xC3\xA9ment introuvable (liste chang\xC3\xA9" "e ?)";
    else if (tp_fs_check_name(q, src)) m = "La liste a chang\xC3\xA9 : rouvre le dossier";
    else if (!tp_fs_ok(src) || tp_fs_protected(src)) m = "Prot\xC3\xA9g\xC3\xA9 : la racine, le dossier gc-mp3 et le morceau en cours ne se touchent pas";
    else if (!strcmp(op, "del")) {
        crumb("telephone : suppression %s", src);
        if (tp_rm_tree(src, 0) == 0) { ok = 1; m = "Supprim\xC3\xA9"; } else m = "Suppression incompl\xC3\xA8te";
        tp_fs_touched(src);
    } else if (!strcmp(op, "ren") || !strcmp(op, "mv")) {
        char to[NAME_LEN];
        if (!strcmp(op, "ren")) {
            snprintf(to, sizeof(to), "%s", dir);
            if (!tp_fs_name(nm, clean, sizeof(clean))) { m = "Nom invalide"; goto out; }
        } else {
            char th[2 * NAME_LEN + 4];
            tp_param(q, "t", th, sizeof(th));
            if (!th[0]) snprintf(to, sizeof(to), "%s", SD_ROOT); else if (!tp_hex_dec(th, to, sizeof(to))) { m = "Destination introuvable"; goto out; }
            if (!tp_fs_ok(to)) { m = "Destination introuvable"; goto out; }
            snprintf(clean, sizeof(clean), "%s", base_of(src));
            size_t sl = strlen(src);
            if (!strncmp(to, src, sl) && (to[sl] == 0 || to[sl] == '/')) { m = "Un dossier ne va pas dans lui-m\xC3\xAAme"; goto out; }
        }
        struct stat st;
        if (snprintf(dst, sizeof(dst), "%s/%s", to, clean) >= (int)sizeof(dst)) m = "Nom trop long";
        else if (!strcmp(dst, src)) { ok = 1; m = "Rien \xC3\xA0 changer"; }
        else if (stat(dst, &st) == 0 && strcasecmp(dst, src)) m = "Ce nom existe d\xC3\xA9j\xC3\xA0 ici";
        else if (rename(src, dst) == 0) { ok = 1; m = !strcmp(op, "ren") ? "Renomm\xC3\xA9" : "D\xC3\xA9plac\xC3\xA9"; tp_fs_touched(src); tp_fs_touched(dst); crumb("telephone : %s -> %s", src, dst); }
    }
out:;
    tp_update_free();
    sbuf_t s = { NULL, 0, 0 };
    sb_add(&s, ok ? "{\"ok\":1,\"m\":" : "{\"ok\":0,\"m\":"); x73_json_raw(&s, m); sb_add(&s, "}");
    tp_reply(c, 200, "application/json", s.b ? s.b : "{}", s.b ? (int)strlen(s.b) : 2);
    free(s.b);
}

static void tp_update_free(void) {
    unsigned long long st_, sf_;
    if (sd_ok && sd_space(&st_, &sf_)) tp_free_mb = (long)(sf_ >> 20);
}

/* parametre d'une adresse ("?dir=mp3&name=x") */
static int tp_param(const char *q, const char *key, char *out, int cap) {
    size_t kl = strlen(key);
    out[0] = 0;
    for (const char *p = q; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            char raw[2100];
            int n = (int)strcspn(p + kl + 1, "&");
            if (n > (int)sizeof(raw) - 1) n = sizeof(raw) - 1;
            memcpy(raw, p + kl + 1, n);
            raw[n] = 0;
            url_decode(raw, out, cap, 1);
            return 1;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return 0;
}

/* envoi d'un fichier : le corps de la requete est ecrit directement sur la SD */
static volatile int tp_busy = 0;
static volatile u64 tp_park_t = 0;
/* v272 : envoi par morceaux (appli build 39 et plus) : PUT /upload?dir=&name=&id=&off=&total= (256 Ko chacun). Le fichier
   s'ecrit dans "<nom>.part" ; un morceau perdu est renvoye seul (GET /api/upstat?id= : taille deja recue) ; au dernier,
   "<nom>.part" devient le fichier. (Avant : tout le fichier d'un seul coup, et a la moindre coupure, tout etait perdu.) */
static struct { char id[40], path[NAME_LEN + 16], part[NAME_LEN + 24], dir[32], clean[200]; long total; } tpk;
static void tp_upload(s32 c, const char *q, long len, const unsigned char *pre, int prelen) {
    static const char *const dirs[] = { "mp3", "downloads", "retro", "photos", "ecran", "videos", "radio", "podcasts", "gci", "regtel" };   /* (v242 : regtel = copie des reglages du telephone) */   /* (v190 : gci = sauvegardes) */
    char dir[32], name[200], path[NAME_LEN + 16];
    tp_param(q, "dir", dir, sizeof(dir));
    tp_param(q, "name", name, sizeof(name));
    char kid[40] = "", koff[24] = "", ktot[24] = "";
    tp_param(q, "id", kid, sizeof(kid)); tp_param(q, "off", koff, sizeof(koff)); tp_param(q, "total", ktot, sizeof(ktot));
    long off = atol(koff), total = atol(ktot);
    int chunk = kid[0] && total > 0;
    if (chunk && (off < 0 || off + len > total)) { tp_reply(c, 400, "text/plain", "morceau", 7); return; }
    if (chunk && off > 0) {                                     /* (morceau suivant : meme fichier) */
        char sz[24];
        if (strcmp(kid, tpk.id) || total != tpk.total) { tp_reply(c, 409, "text/plain", "-1", 2); return; }
        struct stat ps;
        long have = stat(tpk.part, &ps) == 0 ? (long)ps.st_size : -1;
        if (have != off) { int n = snprintf(sz, sizeof(sz), "%ld", have); tp_reply(c, 409, "text/plain", sz, n); return; }   /* (l'appli reprend la) */
        snprintf(dir, sizeof(dir), "%s", tpk.dir);
        snprintf(name, sizeof(name), "%s", tpk.clean);
    }
    int okdir = 0;
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) if (!strcmp(dir, dirs[i])) okdir = 1;
    /* nom de fichier seul, sans caracteres interdits sur une carte FAT */
    char clean[200];
    int n = 0;
    const char *b = name;
    for (const char *p = name; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    for (; *b && n < (int)sizeof(clean) - 1; b++) {
        unsigned char ch = (unsigned char)*b;
        if (ch < 32 || strchr(":*?\"<>|", ch)) continue;
        clean[n++] = (char)ch;
    }
    clean[n] = 0;
    while (clean[0] == '.' || clean[0] == ' ') memmove(clean, clean + 1, strlen(clean));
    if (!strcmp(dir, "apps")) okdir = 1;                    /* (v272 : morceaux suivants d'un .dol) */
    char nid[8] = "";                                       /* v297 : page de notice : dir=notice&nid=<ID> -> SD:/gc-mp3/notices/<ID>/<nom> */
    tp_param(q, "nid", nid, sizeof(nid));
    int idok = strlen(nid) == 6;
    for (int i = 0; idok && i < 6; i++) if (!isalnum((unsigned char)nid[i])) idok = 0;
    int notice = !strcmp(dir, "notice") && idok;
    if (notice) okdir = 1;
    char jk[4] = "";                                        /* v301 : jaquette d'un jeu : dir=jaquette&nid=<ID>&kind= */
    tp_param(q, "kind", jk, sizeof(jk));                    /*  c : liste (ajoutees), f : complete, h : complete HD, d : disque */
    static const char *const jsub[4] = { "ajoutees", "completes", "completes", "disques" };
    int jkk = jk[0] && !jk[1] && strchr("cfhd", jk[0]) ? (int)(strchr("cfhd", jk[0]) - "cfhd") : -1;
    int jaq = !strcmp(dir, "jaquette") && idok && jkk >= 0;
    if (jaq) okdir = 1;
    if (!okdir || !clean[0] || len <= 0 || !sd_ok) { tp_reply(c, 400, "text/plain", "refus", 5); return; }
    { const char *dt = strrchr(clean, '.');                    /* v224 : un programme va toujours dans SD:/apps (liste Applis) */
      if (dt && !strcasecmp(dt, ".dol")) snprintf(dir, sizeof(dir), "apps"); }
    const char *gpre = (!strcmp(dir, "radio") || !strcmp(dir, "podcasts")) ? "gc-mp3/" : "";   /* (v22 : rangees dans gc-mp3) */
    mkdir(GCD, 0777);
    int regtel = !strcmp(dir, "regtel");
    if (regtel) { mkdir(GCD_REG, 0777); snprintf(path, sizeof(path), "%s", TP_REGTEL); remove(path); }   /* (toujours le meme nom, remplace) */
    else if (jaq) {                                         /* (v301 : remplace l'image d'avant ; convertie par la console a la 1re vue) */
        mkdir(GCD "/jaquettes", 0777);
        snprintf(path, sizeof(path), GCD "/jaquettes/%s", jsub[jkk]); mkdir(path, 0777);
        snprintf(path, sizeof(path), GCD "/jaquettes/%s/%s%s.png", jsub[jkk], nid, jkk == 2 ? "-hd" : "");
        if (!(chunk && off > 0)) remove(path);
    }
    else if (notice) {                                      /* (v297 : la page remplace l'ancienne) */
        mkdir(GCD "/notices", 0777);
        snprintf(path, sizeof(path), GCD "/notices/%s", nid); mkdir(path, 0777);
        snprintf(path, sizeof(path), GCD "/notices/%s/%s", nid, clean);
        if (!(chunk && off > 0)) remove(path);
    } else {
    snprintf(path, sizeof(path), SD_ROOT "/%s%s", gpre, dir);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), SD_ROOT "/%s%s/%s", gpre, dir, clean);
    }
    struct stat st;
    if (chunk && off > 0) snprintf(path, sizeof(path), "%s", tpk.path);   /* (nom choisi au 1er morceau) */
    else for (int k = 2; !regtel && !notice && !jaq && k < 100 && stat(path, &st) == 0; k++) {     /* deja la : "nom (2).mp3" */
        char base[200], ext[16] = "";
        snprintf(base, sizeof(base), "%s", clean);
        char *dot = strrchr(base, '.');
        if (dot && strlen(dot) < sizeof(ext)) { snprintf(ext, sizeof(ext), "%s", dot); *dot = 0; }
        snprintf(path, sizeof(path), SD_ROOT "/%s%s/%s (%d)%s", gpre, dir, base, k, ext);
    }
    const char *wpath = path;
    if (chunk) {
        if (off == 0) {
            snprintf(tpk.id, sizeof(tpk.id), "%s", kid); tpk.total = total;
            snprintf(tpk.path, sizeof(tpk.path), "%s", path); snprintf(tpk.part, sizeof(tpk.part), "%s.part", path);
            snprintf(tpk.dir, sizeof(tpk.dir), "%s", dir); snprintf(tpk.clean, sizeof(tpk.clean), "%s", clean);
            remove(tpk.part);
        }
        wpath = tpk.part;
    }
    FILE *f = fopen(wpath, chunk && off > 0 ? "r+b" : "wb");
    if (f && chunk && off > 0 && fseek(f, off, SEEK_SET) != 0) { fclose(f); f = NULL; }
    if (!f) { tp_reply(c, 500, "text/plain", "ecriture impossible", 19); return; }
    static char vbuf[65536];                    /* ecritures SD par gros blocs */
    setvbuf(f, vbuf, _IOFBF, sizeof(vbuf));
    if (!chunk || off == 0) tp_up_t0 = now_ms();
    snprintf(tp_up_name, sizeof(tp_up_name), "%s", clean);
    if (!chunk || off == 0) crumb("telephone : envoi %s/%s (%ld Ko)", dir, clean, (chunk ? total : len) / 1024);
    long base = chunk ? off : 0;                            /* (progression : tout le fichier, pas seulement ce morceau) */
    tp_up_total = chunk ? total : len;
    tp_up_done = base;
    tp_up_active = 1;
    static unsigned char buf[32768];
    int ok = 1;
    if (prelen > 0) {
        int m = prelen > len ? (int)len : prelen;
        if (fwrite(pre, 1, m, f) != (size_t)m) ok = 0;
        tp_up_done = base + m;
    }
    while (ok && tp_up_done - base < len) {
        long want = len - (tp_up_done - base);
        if (tp_park_t > tp_up_t0) { ok = 0; break; }        /* v271 : la carte va etre demontee (demande pendant l'envoi) */
        int r = tp_recv(c, buf, want > (long)sizeof(buf) ? (int)sizeof(buf) : (int)want, 20000);
        if (r <= 0) { ok = 0; break; }
        if (fwrite(buf, 1, r, f) != (size_t)r) { ok = 0; break; }
        tp_up_done += r;
    }
    if (fclose(f) != 0) ok = 0;
    tp_up_active = 0;
    tp_up_end = now_ms();
    char m[160];
    if (!ok && chunk) {                                         /* (morceau : le debut reste, l'appli reprend) */
        FILE *tf = tpk.id[0] ? fopen(tpk.part, "r+b") : NULL;   /* (ce qui est arrive de ce morceau est enleve) */
        if (tf) { if (ftruncate(fileno(tf), base) != 0) { } fclose(tf); }
        tp_reply(c, 500, "text/plain", "morceau interrompu", 18);
        return;
    }
    if (chunk && off + len < total) { tp_reply(c, 200, "text/plain", "suite", 5); return; }   /* (d'autres morceaux viennent) */
    if (chunk) {                                                 /* dernier morceau : le fichier est complet */
        tpk.id[0] = 0;
        if (rename(tpk.part, path) != 0) { remove(tpk.part); tp_reply(c, 500, "text/plain", "renommage impossible", 20); return; }
    }
    if (!ok) {
        remove(path);
        snprintf(m, sizeof(m), "\xC3\x89" "chec : %s", clean);
        tp_logf(m);
        tp_reply(c, 500, "text/plain", "envoi interrompu", 16);
        return;
    }
    if (jaq) {                                              /* (v301 : l'ancienne image deja convertie est enlevee : la nouvelle sert) */
        char tp[NAME_LEN];
        snprintf(tp, sizeof(tp), GCD "/jaquettes/%s/%s%s.tex", jsub[jkk], nid, jkk == 2 ? "-hd" : ""); remove(tp);
        if (jkk == 0) { snprintf(tp, sizeof(tp), GCD "/jaquettes/ajoutees/%s.jpg", nid); remove(tp); }
        tp_jaq_new = 1;
        tp_reply(c, 200, "text/plain", "ok", 2);
        return;
    }
    snprintf(m, sizeof(m), "Re\xC3\xA7u : %s/%s", dir, clean);
    tp_logf(m);
    snprintf(tp_done_path[tp_done_w % 4], NAME_LEN, "%s", path);
    tp_done_w++;
    if (!strcmp(dir, "mp3") || !strcmp(dir, "downloads") || !strcmp(dir, "retro")) {
        const char *dot = strrchr(clean, '.');
        int au = audio_ext_len(path) && !(dot && !strcasecmp(dot, ".zip"));
        if (au && tp_add_w - tp_add_r < 8) {
            snprintf(tp_add_path[tp_add_w % 8], NAME_LEN, "%s", path);
            tp_add_w++;
        } else if (au) tp_rescan |= 1;                  /* (beaucoup de musiques d'un coup : tout relire) */
        else if (!strcmp(dir, "retro")) tp_rescan |= 4; /* v224 : un jeu retro : la liste RETRO seulement (avant : toute la carte, a chaque fichier) */
    }
    if (!strcmp(dir, "photos") || !strcmp(dir, "videos")) tp_rescan |= 2;
    tp_update_free();
    if (!strcmp(dir, "apps")) {                                 /* v224 : on dit au telephone ou il est range */
        tp_apps_new = 1;
        char r[NAME_LEN + 16];
        snprintf(r, sizeof(r), "SD:/apps/%s", base_of(path));
        tp_reply(c, 200, "text/plain; charset=utf-8", r, (int)strlen(r));
        return;
    }
    if (!strcmp(dir, "gci")) { const char *bn = base_of(path); tp_reply(c, 200, "text/plain; charset=utf-8", bn, (int)strlen(bn)); return; }   /* (v190 : le nom garde) */
    tp_reply(c, 200, "text/plain", "ok", 2);
}

/* v271 : fil des envois (PUT /upload) */
static volatile s32 tpu_c = -1;
static volatile int tp_taken = 0;                         /* (la connexion est passee au fil des envois : ne pas la fermer) */
static char tpu_q[600];
static long tpu_len;
static unsigned char tpu_pre[8192];
static int tpu_prelen;
static lwp_t tpu_h;
static u8 tpu_stack[24 * 1024] ATTRIBUTE_ALIGN(32);
static void *tpu_thread(void *a) {
    (void)a;
    for (;;) {
        s32 c = tpu_c;
        if (c < 0) { usleep(5000); continue; }
        tp_upload(c, tpu_q, tpu_len, tpu_pre, tpu_prelen);
        net_close(c);
        tpu_c = -1;
    }
    return NULL;
}
static void tp_handle(s32 c) {
    static unsigned char req[8192];
    int len = 0, hend = -1;
    while (len < (int)sizeof(req) - 1) {
        int r = tp_recv(c, req + len, (int)sizeof(req) - 1 - len, 10000);
        if (r <= 0) return;
        len += r;
        req[len] = 0;
        char *e = strstr((char *)req, "\r\n\r\n");
        if (e) { hend = (int)(e - (char *)req) + 4; break; }
    }
    if (hend < 0) return;
    tp_last_req = now_ms();
    char method[8] = "", target[2600] = "";
    if (sscanf((char *)req, "%7s %2599s", method, target) != 2) return;
    char *q = strchr(target, '?');
    if (q) *q++ = 0;
    if (!strcmp(method, "PUT") || !strcmp(method, "POST")) {
        long cl = -1;
        const char *h = ci_find((char *)req, "\ncontent-length:");
        if (h) cl = atol(h + 16);
        if (!strcmp(target, "/upload") && cl > 0) {
            /* v271 : l'envoi dans son propre fil : le serveur continue de repondre (etat, commandes) pendant l'envoi.
               Avant : plus aucune reponse pendant tout l'envoi (63 s pour 30 Mo) : "console injoignable" sur le telephone */
            if (tpu_c == -2) { tp_upload(c, q ? q : "", cl, req + hend, len - hend); return; }   /* (fil pas cree : comme avant) */
            if (tpu_c >= 0) { tp_reply(c, 503, "text/plain", "envoi en cours", 14); return; }   /* (un envoi a la fois) */
            snprintf(tpu_q, sizeof(tpu_q), "%s", q ? q : "");
            tpu_len = cl;
            tpu_prelen = len - hend > (int)sizeof(tpu_pre) ? (int)sizeof(tpu_pre) : len - hend;
            memcpy(tpu_pre, req + hend, tpu_prelen);
            tp_taken = 1;
            tpu_c = c;
        }
        else tp_reply(c, 400, "text/plain", "", 0);
        return;
    }
    if (sh_route(c, target, (char *)req)) return;          /* v55 : fichiers partages (QR code) */
    if (salon_route(c, target, q, (char *)req)) return;    /* v138 : le Salon (autres GC Station) */
    /* (v246 : onglet TELEPHONE cache : la telecommande reste active ; avant v246 : coupee) */
    if (ia_phone_route(c, target, q)) return;             /* v206 : assistant IA (/ia) */
    if (!strcmp(target, "/") || !strcmp(target, "/index.html"))
        tp_reply(c, 200, "text/html; charset=utf-8", ui_en ? phone_page_en : phone_page, (int)strlen(ui_en ? phone_page_en : phone_page));
    else if (!strcmp(target, "/api/state")) {
        char tmp[sizeof(tp_json[0])];
        snprintf(tmp, sizeof(tmp), "%s", tp_json[tp_json_cur]);
        tp_reply(c, 200, "application/json", tmp, (int)strlen(tmp));
    } else if (!strcmp(target, "/api/tracks")) tp_send_tracks(c);
    else if (!strcmp(target, "/api/stations")) tp_send_stations(c);
    else if (!strcmp(target, "/api/titres")) {             /* titres de la radio gardes (v18) */
        FILE *f = sd_ok ? fopen(GCD_BIB "/gc-mp3-titres.txt", "rb") : NULL;
        char *b = (char *)malloc(64 * 1024);
        int n = 0;
        if (f && b) {
            if (fseek(f, 0, SEEK_END) == 0) { long sz = ftell(f); if (sz > 64 * 1024) fseek(f, sz - 64 * 1024, SEEK_SET); else fseek(f, 0, SEEK_SET); }
            n = (int)fread(b, 1, 64 * 1024, f);
        }
        if (f) fclose(f);
        tp_reply(c, 200, "text/plain; charset=utf-8", b ? b : "", b ? n : 0);
        free(b);
    }
    else if (!strcmp(target, "/cover.bmp")) tp_send_cover(c);
    else if (!strcmp(target, "/api/open")) {                 /* ouvrir une page sur la tele */
        char u[2048];
        tp_param(q ? q : "", "u", u, sizeof(u));
        int ok = u[0] && !tp_open_busy;
        if (ok) { snprintf(tp_open_url, sizeof(tp_open_url), "%s", u); tp_open_busy = 1; tp_push(PC_OPEN, 0); }
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/natif") || !strcmp(target, "/natif/")) {   /* v51 : rapports d'analyse des jeux */
        sbuf_t s = { 0 };
        sb_add(&s, "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
                   "<title>GC Station - analyses</title><style>body{font-family:sans-serif;background:#120a2e;color:#eee;padding:16px}"
                   "a{color:#c9a8ff;display:block;padding:10px 0;font-size:18px}</style></head><body><h2>Analyses des jeux</h2>");
        int n = 0;
        DIR *d = sd_ok ? opendir(GCD "/natif") : NULL;
        struct dirent *e;
        while (d && (e = readdir(d)) != NULL && n < 400) {
            int l = (int)strlen(e->d_name);
            if (l < 5 || strcasecmp(e->d_name + l - 4, ".txt")) continue;
            char line[400];
            snprintf(line, sizeof(line), "<a href=\"/natif/%s\">%s</a>", e->d_name, e->d_name);
            sb_add(&s, line); n++;
        }
        if (d) closedir(d);
        if (!n) sb_add(&s, "<p>Aucune analyse pour l'instant : fiche d'un jeu, touche R.</p>");
        sb_add(&s, "</body></html>");
        tp_reply(c, 200, "text/html; charset=utf-8", s.b ? s.b : "", s.b ? (int)strlen(s.b) : 0);
        free(s.b);
    }
    else if (!strncmp(target, "/natif/", 7)) {               /* v51 : un rapport (nom sur : lettres, chiffres, .txt) */
        const char *nm = target + 7;
        int l = (int)strlen(nm), ok = l >= 5 && l <= 40 && !strcasecmp(nm + l - 4, ".txt");
        for (int i = 0; ok && i < l - 4; i++) if (!isalnum((unsigned char)nm[i]) && nm[i] != '_' && nm[i] != '-') ok = 0;
        char p[NAME_LEN];
        long sz = 0;
        u8 *b = NULL;
        if (ok && sd_ok) { snprintf(p, sizeof(p), "%s/natif/%s", GCD, nm); b = read_file(p, 256 * 1024, &sz); }
        if (b) tp_reply(c, 200, "text/plain; charset=utf-8", b, (int)sz);
        else tp_reply(c, 404, "text/plain; charset=utf-8", "Rapport introuvable", 19);
        free(b);
    }
    else if (!strcmp(target, "/crash.txt")) {               /* journal de plantage */
        static char cb[9000];
        int n = 0;
        FILE *f = sd_ok ? fopen(GCD_LOG "/gc-mp3-crash.txt", "rb") : NULL;
        if (!f && sd_ok) f = fopen(GCD_LOG "/gc-mp3-crash-ancien.txt", "rb");
        if (f) { n = (int)fread(cb, 1, sizeof(cb), f); fclose(f); }
        if (n <= 0) { n = snprintf(cb, sizeof(cb), "Aucun plantage enregistre.\n"); }
        tp_reply(c, 200, "text/plain; charset=utf-8", cb, n);
    }
    else if (!strcmp(target, "/api/settings")) {
        if (!tp_cfg[tp_cfg_cur]) { tp_cfg_now = 1; for (int k = 0; k < 50 && !tp_cfg[tp_cfg_cur]; k++) usleep(20000); }
        tp_cfg_reading = 1;                                    /* (la boucle ne la remplace pas pendant la copie) */
        const char *cfg = tp_cfg[tp_cfg_cur];
        char *tmp = cfg ? strdup(cfg) : NULL;
        tp_cfg_reading = 0;
        if (tmp) { tp_reply(c, 200, "application/json", tmp, (int)strlen(tmp)); free(tmp); }
        else tp_reply(c, 500, "text/plain", "", 0);
    }
    else if (!strcmp(target, "/api/menu")) {                /* < ou > sur une ligne des reglages */
        char is[16], ds[8];
        tp_param(q ? q : "", "i", is, sizeof(is));
        tp_param(q ? q : "", "d", ds, sizeof(ds));
        int i = atoi(is), ok = is[0] && i >= 0 && i < MI_COUNT && i != MI_QUIT;
        if (ok) tp_push(PC_MENU, i * 2 + (atoi(ds) > 0));
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/api/check")) {               /* case a cocher (elements, onglets) */
        char w[8], is[16];
        tp_param(q ? q : "", "w", w, sizeof(w));
        tp_param(q ? q : "", "i", is, sizeof(is));
        int i = atoi(is), ok = is[0] && ((!strcmp(w, "el") && i >= 0 && i < EL_COUNT) || (!strcmp(w, "tb") && i > 0 && i < TAB_COUNT));
        if (ok) tp_push(PC_CHECK, (w[0] == 't' ? 100 : 0) + i);
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/api/set")) {                 /* reglage tape (ville, reveil...) */
        char k[32], v[600];
        tp_param(q ? q : "", "k", k, sizeof(k));
        tp_param(q ? q : "", "v", v, sizeof(v));
        int ok = 0;
        for (int i = 0; tp_text_keys[i]; i++) if (!strcmp(k, tp_text_keys[i])) ok = 1;
        for (char *p = v; *p; p++) if (*p == '\r' || *p == '\n') *p = ' ';
        if (ok && !tp_set_busy) {
            snprintf(tp_set_k, sizeof(tp_set_k), "%s", k);
            snprintf(tp_set_v, sizeof(tp_set_v), "%s", v);
            tp_set_busy = 1;
            tp_push(PC_SET, 0);
        } else ok = 0;
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/api/files")) tp_send_files(c, q ? q : "");      /* v73 : fichiers de la carte SD */
    else if (!strcmp(target, "/api/fsop")) tp_fsop(c, q ? q : "");             /* v236 : supprimer, renommer, dossier, deplacer */
    else if (!strcmp(target, "/api/get")) {
        char p[NAME_LEN];
        int isdir = 1;
        if (tp_fs_path(q ? q : "", p, sizeof(p), &isdir) && !isdir) sh_send_file(c, p, 0, (char *)req);
        else tp_reply(c, 404, "text/plain", "", 0);
    }
    else if (!strcmp(target, "/api/games")) {                /* v73 : jeux GameCube (liste faite par la boucle) */
        x73_gj_ready = 0; x73_gj_want = 1;
        for (int k = 0; k < 400 && !x73_gj_ready; k++) usleep(20000);
        char *t = x73_gj_ready && x73_gj ? strdup(x73_gj) : NULL;
        if (t) { tp_reply(c, 200, "application/json", t, (int)strlen(t)); free(t); }
        else tp_reply(c, 503, "application/json", "[]", 2);
    }
    else if (!strcmp(target, "/api/cheats") || !strcmp(target, "/api/cheatset")) {   /* v240 : cheats du jeu ?id=GALE01 (&s=0101...) */
        char id[16], st[CH_PHONE_MAX + 2];
        tp_param(q ? q : "", "id", id, sizeof(id));
        st[0] = 0;
        int set = !strcmp(target, "/api/cheatset");
        if (set) tp_param(q ? q : "", "s", st, sizeof(st));
        int ok = strlen(id) == 6 && !x73_ch_want;
        for (int k = 0; ok && id[k]; k++) if (!isalnum((unsigned char)id[k])) ok = 0;
        for (int k = 0; ok && st[k]; k++) if (st[k] != '0' && st[k] != '1') ok = 0;
        if (ok && set && strlen(st) > CH_PHONE_MAX) ok = 0;
        if (!ok) { tp_reply(c, 400, "application/json", "{}", 2); return; }
        memcpy(x73_ch_id, id, 7);
        snprintf(x73_ch_set, sizeof(x73_ch_set), "%s", st);
        x73_ch_ready = 0; x73_ch_want = set ? 2 : 1;
        for (int k = 0; k < 400 && !x73_ch_ready; k++) usleep(20000);
        char *t = x73_ch_ready && x73_chj ? strdup(x73_chj) : NULL;
        if (t) { tp_reply(c, 200, "application/json", t, (int)strlen(t)); free(t); }
        else tp_reply(c, 503, "application/json", "{}", 2);
    }
    else if (!strcmp(target, "/api/cheatedit") || !strcmp(target, "/api/gamecfg") || !strcmp(target, "/api/gamefav") || !strcmp(target, "/api/lyrics")
             || !strcmp(target, "/api/queueadd") || !strcmp(target, "/api/regpack") || !strcmp(target, "/api/regrestore")
             || !strcmp(target, "/api/compat") || !strcmp(target, "/api/compatset") || !strcmp(target, "/api/succesadd")) {   /* v242 ; v255 : pastilles des jeux ; v306 : succes */
        char id[16] = "", v[16];
        int op = !strcmp(target, "/api/cheatedit") ? 1 : !strcmp(target, "/api/gamecfg") ? 2 : !strcmp(target, "/api/gamefav") ? 3
               : !strcmp(target, "/api/lyrics") ? 4 : !strcmp(target, "/api/queueadd") ? 5 : !strcmp(target, "/api/regpack") ? 6
               : !strcmp(target, "/api/regrestore") ? 7 : !strcmp(target, "/api/compat") ? 8 : !strcmp(target, "/api/compatset") ? 9 : 10;
        tp_param(q ? q : "", "id", id, sizeof(id));
        int idok = strlen(id) == 6;
        for (int k = 0; idok && id[k]; k++) if (!isalnum((unsigned char)id[k])) idok = 0;
        if (((op <= 3 || op >= 9) && !idok) || x73_mo_op) { tp_reply(c, 400, "application/json", "{}", 2); return; }
        memcpy(x73_mo_id, id, 7);
        v[0] = 0; tp_param(q ? q : "", op == 1 ? "n" : "i", v, sizeof(v)); x73_mo_i = v[0] ? atoi(v) : -1;
        v[0] = 0; tp_param(q ? q : "", "d", v, sizeof(v)); x73_mo_d = v[0] ? atoi(v) : op == 3 ? -1 : 0;   /* (favori sans d : inverse) */
        v[0] = 0; tp_param(q ? q : "", op == 1 ? "op" : "row", v, sizeof(v)); x73_mo_n = op == 1 ? v[0] : v[0] ? atoi(v) : -1;
        x73_mo_name[0] = 0; tp_param(q ? q : "", "name", x73_mo_name, sizeof(x73_mo_name));
        x73_mo_codes[0] = 0; tp_param(q ? q : "", "codes", x73_mo_codes, sizeof(x73_mo_codes));
        char *t = x73_mo_run(op);
        if (op == 6 && t && t[0] == '@') {                    /* copie des reglages : le fichier ecrit par la boucle */
            sh_send_file(c, t + 1, 0, (char *)req);
            free(t);
            return;
        }
        if (t) { tp_reply(c, 200, "application/json", t, (int)strlen(t)); free(t); }
        else tp_reply(c, 503, "application/json", "{}", 2);
    }
    else if (!strcmp(target, "/api/launch")) {               /* v73 : lancer un jeu (m=1 : Resident) */
        char is[16], ms[8];
        tp_param(q ? q : "", "i", is, sizeof(is));
        tp_param(q ? q : "", "m", ms, sizeof(ms));
        int ok = is[0] && atoi(is) >= 0;
        { char id[16]; tp_param(q ? q : "", "id", id, sizeof(id)); snprintf(x73_launch_id, sizeof(x73_launch_id), "%.6s", id); }
        {   /* v250 : &fx=ST... : patchs de secours pour ce lancement (seulement les lettres connues) */
            char fx[16]; int n = 0;
            tp_param(q ? q : "", "fx", fx, sizeof(fx));
            for (int k = 0; fx[k] && n < (int)sizeof(x73_launch_fx) - 1; k++) if (strchr("SFTCHWV", fx[k])) x73_launch_fx[n++] = fx[k];
            x73_launch_fx[n] = 0;
        }   /* v242 : (l'ID suit le jeu si la liste a change d'ordre) */
        if (ok) { x73_launch_m = atoi(ms) == 1; x73_launch_k = atoi(is); }
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/collection")) tp_reply(c, 200, "text/html; charset=utf-8", collection_page, (int)strlen(collection_page));   /* v191 */
    else if (!strcmp(target, "/memoire")) tp_reply(c, 200, "text/html; charset=utf-8", memoire_page, (int)strlen(memoire_page));   /* v230 */
    else if (!strcmp(target, "/api/memoire")) { char *j = mv_json(); tp_reply(c, 200, "application/json", j ? j : "{}", j ? (int)strlen(j) : 2); free(j); }
    else if (!strcmp(target, "/recap")) tp_reply(c, 200, "text/html; charset=utf-8", recap_page, (int)strlen(recap_page));   /* v191 */
    else if (!strcmp(target, "/api/recap")) {
        x73_rj_ready = 0; x73_rj_want = 1;
        for (int k = 0; k < 500 && !x73_rj_ready; k++) usleep(20000);
        char *t = x73_rj_ready && x73_rj ? strdup(x73_rj) : NULL;
        if (t) { tp_reply(c, 200, "application/json", t, (int)strlen(t)); free(t); }
        else tp_reply(c, 503, "application/json", "{}", 2);
    }
    else if (!strcmp(target, "/api/cards")) {                /* v190 : cartes memoire virtuelles et leurs sauvegardes */
        x73_cj_ready = 0; x73_cj_want = 1;
        for (int k = 0; k < 1000 && !x73_cj_ready; k++) usleep(20000);
        char *t = x73_cj_ready && x73_cj ? strdup(x73_cj) : NULL;
        if (t) { tp_reply(c, 200, "application/json", t, (int)strlen(t)); free(t); }
        else tp_reply(c, 503, "application/json", "[]", 2);
    }
    else if (!strncmp(target, "/api/cardfile/", 14)) {       /* v190 : la carte entiere (.raw) */
        const char *n = target + 14;
        int l = (int)strlen(n);
        char p[NAME_LEN];
        if (l < 5 || l > 60 || strchr(n, '/') || strstr(n, "..") || strcasecmp(n + l - 4, ".raw")) { tp_reply(c, 400, "text/plain", "", 0); return; }
        snprintf(p, sizeof(p), GCD "/resident/%s", n);   /* (NA_CARD_DIR de natif.c) */
        sh_send_file(c, p, 0, (char *)req);
    }
    else if (!strncmp(target, "/api/gci/", 9)) {             /* v190 : une sauvegarde (.gci) : /api/gci/<carte>/<n>/<nom>.gci */
        char card[64] = "", *sl;
        snprintf(card, sizeof(card), "%s", target + 9);
        sl = strchr(card, '/');
        int i = sl ? atoi(sl + 1) : -1;
        if (sl) *sl = 0;
        if (i < 0 || !x73_vj_run(1, card, i, NULL) || !x73_vj_ok) { tp_reply(c, 404, "text/plain; charset=utf-8", x73_vj_res, (int)strlen(x73_vj_res)); return; }
        sh_send_file(c, x73_vj_res, 0, (char *)req);
    }
    else if (!strcmp(target, "/api/gcidel")) {               /* v262 : effacer une sauvegarde : ?c=<carte>&i=<n>&n=<nom> */
        char card[64], n[120], is[12];
        tp_param(q ? q : "", "c", card, sizeof(card));
        tp_param(q ? q : "", "n", n, sizeof(n));
        tp_param(q ? q : "", "i", is, sizeof(is));
        int done = is[0] && x73_vj_run(4, card, atoi(is), n);
        sbuf_t sb = { NULL, 0, 0 };
        sb_add(&sb, done && x73_vj_ok ? "{\"ok\":1,\"m\":" : "{\"ok\":0,\"m\":");
        x73_json_raw(&sb, done ? x73_vj_res : "GC Station occup\xC3\xA9" "e, r\xC3\xA9" "essaie");
        sb_add(&sb, "}");
        tp_reply(c, 200, "application/json", sb.b ? sb.b : "{}", sb.b ? (int)strlen(sb.b) : 2);
        free(sb.b);
    }
    else if (!strcmp(target, "/api/gciimp") || !strcmp(target, "/api/cardput")) {   /* v190 : .gci envoye -> carte ; ou carte remplacee */
        char card[64], n[200];
        tp_param(q ? q : "", "c", card, sizeof(card));
        tp_param(q ? q : "", "n", n, sizeof(n));
        int done = x73_vj_run(target[5] == 'g' ? 2 : 3, card, 0, n);
        char m[NAME_LEN + 140];
        sbuf_t sb = { NULL, 0, 0 };
        sb_add(&sb, done && x73_vj_ok ? "{\"ok\":1,\"m\":" : "{\"ok\":0,\"m\":");
        x73_json_raw(&sb, done ? x73_vj_res : "GC Station occup\xC3\xA9" "e, r\xC3\xA9" "essaie");
        sb_add(&sb, "}");
        snprintf(m, sizeof(m), "%s", sb.b ? sb.b : "{}");
        free(sb.b);
        tp_reply(c, 200, "application/json", m, (int)strlen(m));
    }
    else if (!strcmp(target, "/api/type")) {                 /* v222 : texte tape sur le telephone (go=1 : valider) */
        char g[4];
        int ok = !tp_type_busy;
        if (ok) {
            tp_param(q ? q : "", "t", tp_type_txt, sizeof(tp_type_txt));
            tp_param(q ? q : "", "go", g, sizeof(g));
            tp_type_go = g[0] == '1';
            tp_type_busy = 1;
            tp_push(PC_TYPE, 0);
        }
        tp_reply(c, 200, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/api/showname")) {             /* v222 : photo envoyee par le telephone -> sur la tele */
        int ok = !tp_shown_busy;
        if (ok) { tp_param(q ? q : "", "n", tp_shown_name, sizeof(tp_shown_name)); ok = tp_shown_name[0] != 0; }
        if (ok) { tp_shown_busy = 1; tp_push(PC_SHOWNAME, 0); }
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/test.txt")) {                 /* v259 : dernier test de la console (Outils) */
        long sz = 0;
        u8 *b = sd_ok ? read_file(GCD_LOG "/test-console.txt", 64 * 1024, &sz) : NULL;
        if (b) tp_reply(c, 200, "text/plain; charset=utf-8", b, (int)sz);
        else tp_reply(c, 404, "text/plain; charset=utf-8", "Pas encore de test (START > Outils > Test de la console)", 56);
        free(b);
    }
    else if (!strcmp(target, "/api/pulse")) {                /* v262 : resume du dernier lancement avec Pulse */
        char *j = nat_pj_json();
        tp_reply(c, 200, "application/json", j ? j : "{}", j ? (int)strlen(j) : 2);
        free(j);
    }
    else if (!strcmp(target, "/api/succes")) {               /* v268 : succes des jeux (definitions et obtenus) */
        char *j = sj_json();
        tp_reply(c, 200, "application/json", j ? j : "[]", j ? (int)strlen(j) : 2);
        free(j);
    }
    else if (!strcmp(target, "/api/upstat")) {               /* v272 : envoi par morceaux : octets deja recus (-1 : inconnu) */
        char id[40], t[24];
        tp_param(q ? q : "", "id", id, sizeof(id));
        struct stat ps;
        long have = id[0] && !strcmp(id, tpk.id) && stat(tpk.part, &ps) == 0 ? (long)ps.st_size : -1;
        int n = snprintf(t, sizeof(t), "%ld", have);
        tp_reply(c, 200, "text/plain", t, n);
    }
    else if (!strcmp(target, "/api/up")) {                   /* v272 : l'envoi en cours vu par la console (barre de l'appli) */
        char t[300], nm[200];
        tp_any_utf8(tp_up_name, nm, sizeof(nm));
        for (char *p = nm; *p; p++) if (*p == '"' || *p == '\\') *p = '_';
        int n = snprintf(t, sizeof(t), "{\"a\":%d,\"d\":%ld,\"t\":%ld,\"ms\":%u,\"n\":\"%s\"}", tp_up_active, tp_up_done, tp_up_total,
                         (unsigned)(tp_up_active ? now_ms() - tp_up_t0 : tp_up_end - tp_up_t0), nm);
        tp_reply(c, 200, "application/json", t, n);
    }
    else if (!strcmp(target, "/api/pulse.txt")) {            /* v262 : le journal complet de ce lancement */
        long sz = 0;
        u8 *b = sd_ok ? read_file(GCD "/natif/journal-jeu.txt", 256 * 1024, &sz) : NULL;
        if (b) tp_reply(c, 200, "text/plain; charset=utf-8", b, (int)sz);
        else tp_reply(c, 404, "text/plain; charset=utf-8", "Aucun journal", 13);
        free(b);
    }
    else if (!strcmp(target, "/capture.bmp")) tp_send_capture(c);
    else if (!strcmp(target, "/api/ecranz")) tp_send_ecranz(c, q);  /* v294 : seulement les blocs qui ont change */
    else if (!strcmp(target, "/api/ecran")) tp_send_ecran(c, q);    /* v285 : second ecran (JPEG) */   /* v234 : ce qu'affiche la tele */
    else if (!strcmp(target, "/rapport")) tp_reply(c, 200, "text/html; charset=utf-8", rapport_page, (int)strlen(rapport_page));   /* v234 */
    else if (!strcmp(target, "/api/journal")) {              /* v234 : dernieres actions + journal de plantage */
        static char jb[12000];
        int n = snprintf(jb, sizeof(jb), "== Derni\xC3\xA8res actions (secondes depuis le d\xC3\xA9marrage) ==\n");
        n += crash_recent(jb + n, sizeof(jb) - n);
        FILE *f = sd_ok ? fopen(GCD_LOG "/gc-mp3-crash.txt", "rb") : NULL;
        const char *which = "en cours";
        if (!f && sd_ok) { f = fopen(GCD_LOG "/gc-mp3-crash-ancien.txt", "rb"); which = "pr\xC3\xA9" "c\xC3\xA9" "dent"; }
        if (f) { n += snprintf(jb + n, sizeof(jb) - n, "\n== Journal de plantage (%s) ==\n", which); n += (int)fread(jb + n, 1, sizeof(jb) - n - 1, f); fclose(f); }
        else n += snprintf(jb + n, sizeof(jb) - n, "\n== Journal de plantage ==\nAucun plantage enregistr\xC3\xA9.\n");
        if (n > (int)sizeof(jb) - 1) n = sizeof(jb) - 1;
        tp_reply(c, 200, "text/plain; charset=utf-8", jb, n);
    }
    else if (!strcmp(target, "/api/minijeux")) {             /* v279 : liste des mini-jeux ; ?g=N : le lancer sur la tele */
        char g[8];
        static char o[2400];
        tp_param(q ? q : "", "g", g, sizeof(g));
        if (g[0]) tp_push(PC_MINIJEU, atoi(g));
        int n = gm_phone_list(o, sizeof(o));
        tp_reply(c, 200, "application/json; charset=utf-8", o, n);
    }
    else if (!strcmp(target, "/api/test")) {                 /* v278 : lancer un test (m=0 rapide, 1 complet, 2 total, 3 touches) ; sans m : ou il en est */
        char m[8], o[320];
        tp_param(q ? q : "", "m", m, sizeof(m));
        if (m[0]) tp_push(PC_TEST, atoi(m));
        int n = sx_phone_state(o, sizeof(o));
        tp_reply(c, 200, "application/json; charset=utf-8", o, n);
    }
    else if (!strcmp(target, "/api/cartes")) c8_api(c, q ? q : "");   /* v276 : jeu de cartes (8 americain) */
    else if (!strcmp(target, "/api/mouse")) {                /* v231 : souris tactile -> pointeur de Cube Browser */
        char v[16];
        tp_param(q ? q : "", "dx", v, sizeof(v)); tp_mdx += atoi(v);
        tp_param(q ? q : "", "dy", v, sizeof(v)); tp_mdy += atoi(v);
        tp_param(q ? q : "", "w", v, sizeof(v)); tp_mw += atoi(v);
        tp_param(q ? q : "", "c", v, sizeof(v)); tp_mc |= atoi(v) & 3;
        tp_reply(c, 200, "application/json", "{\"ok\":1}", 8);
    }
    else if (!strcmp(target, "/api/toucher")) {             /* v303 : tele tactile : ?u=&v= (0 a 1 sur l'image de la tele) &c=0 deplacer, 1 cliquer, 2 retour */
        char v[16];
        tp_param(q ? q : "", "u", v, sizeof(v)); f32 u = (f32)atof(v);
        tp_param(q ? q : "", "v", v, sizeof(v)); f32 w = (f32)atof(v);
        tp_param(q ? q : "", "c", v, sizeof(v)); int cc = atoi(v);
        int ok = v[0] && u >= 0 && u <= 1 && w >= 0 && w <= 1 && cc >= 0 && cc <= 2;
        if (ok) { tp_tu = u; tp_tv = w; tp_tc = cc; tp_touch = 1; }
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/api/key")) {                  /* v142 : manette sur le telephone */
        static const char *const kn[] = { "up", "down", "left", "right", "a", "b", "x", "y", "z", "l", "r", "start", NULL };
        static const u32 km[] = { PAD_BUTTON_UP, PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT, PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X,
                                  PAD_BUTTON_Y, PAD_TRIGGER_Z, PAD_TRIGGER_L, PAD_TRIGGER_R, PAD_BUTTON_START };
        char k[12];
        tp_param(q ? q : "", "k", k, sizeof(k));
        int ok = 0;
        for (int i = 0; kn[i]; i++) if (!strcmp(k, kn[i])) { tp_keys |= km[i]; ok = 1; }
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/api/cover")) {                /* v142 : jaquette d'un jeu (CMPR 256x256, decodee par la page) */
        char is[12];
        tp_param(q ? q : "", "i", is, sizeof(is));
        int cw = 0, ch = 0, iw = 0, ih = 0;
        u32 len = 0;
        u8 *b = is[0] ? gl_cover_raw(atoi(is), &cw, &ch, &iw, &ih, &len) : NULL;
        if (b) {
            char h[320];
            int hn = snprintf(h, sizeof(h), "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nX-Size: %dx%dx%dx%d\r\nCache-Control: max-age=86400\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", cw, ch, iw, ih, (unsigned)len);
            tp_send(c, h, hn); tp_send(c, b, (int)len);
            free(b);
        } else tp_reply(c, 404, "text/plain", "", 0);
    }
    else if (!strcmp(target, "/api/photos")) {               /* v142 : photos et videos (liste de l'onglet PHOTOS) */
        if (npf < 0) { tp_rescan |= 2; for (int k = 0; k < 150 && npf < 0; k++) usleep(20000); }
        sbuf_t sb = { NULL, 0, 0 };
        char d[NAME_LEN], m[32];
        sb_add(&sb, "[");
        int n = npf;
        for (int i = 0; i < n && pf; i++) {
            if (i) sb_add(&sb, ",");
            snprintf(m, sizeof(m), "[%d,", i); sb_add(&sb, m);
            text_display(base_of(pf[i].path), d, sizeof(d)); sb_json_str(&sb, d);
            sb_add(&sb, pf[i].video ? ",1]" : ",0]");
        }
        sb_add(&sb, "]");
        tp_reply(c, 200, "application/json", sb.b, sb.b ? sb.n : 0);
        free(sb.b);
    }
    else if (!strcmp(target, "/api/jaqliste")) {            /* v301 : [["GALE01",c,f,h,d],...] : 1 = image deja sur la console */
        x73_jq_ready = 0; x73_jq_want = 1;
        for (int k = 0; k < 400 && !x73_jq_ready; k++) usleep(20000);
        char *t = x73_jq_ready && x73_jq ? strdup(x73_jq) : NULL;
        if (!t) { tp_reply(c, 503, "application/json", "[]", 2); return; }
        sbuf_t sb = { NULL, 0, 0 };
        sb_add(&sb, "[");
        int first = 1;
        for (char *l = t; *l; ) {
            char *e = strchr(l, '\n'); if (!e) break;
            if (e - l == 7) {
                char id[8], p[NAME_LEN], it[48]; struct stat st;
                memcpy(id, l, 6); id[6] = 0;
                int has[3] = { 0, 0, 0 };
                static const char *const fmt[3][2] = { { GCD "/jaquettes/completes/%s.tex", GCD "/jaquettes/completes/%s.png" },
                    { GCD "/jaquettes/completes/%s-hd.tex", GCD "/jaquettes/completes/%s-hd.png" }, { GCD "/jaquettes/disques/%s.tex", GCD "/jaquettes/disques/%s.png" } };
                for (int a = 0; a < 3; a++) for (int b = 0; b < 2 && !has[a]; b++) { snprintf(p, sizeof(p), fmt[a][b], id); if (stat(p, &st) == 0 && st.st_size > 0) has[a] = 1; }
                snprintf(it, sizeof(it), "%s[\"%s\",%c,%d,%d,%d]", first ? "" : ",", id, l[6], has[0], has[1], has[2]);
                sb_add(&sb, it); first = 0;
            }
            l = e + 1;
        }
        sb_add(&sb, "]");
        free(t);
        tp_reply(c, 200, "application/json", sb.b ? sb.b : "[]", sb.b ? (int)sb.n : 2);
        free(sb.b);
    }
    else if (!strcmp(target, "/api/notice")) {              /* v296 : notice d'un jeu : ?id=GALE01 -> {"n":pages} ; &p=N -> la page (JPEG) */
        char id[8], ps[8], p[NAME_LEN];
        tp_param(q ? q : "", "id", id, sizeof(id));
        tp_param(q ? q : "", "p", ps, sizeof(ps));
        int ok = strlen(id) == 6;
        for (int i = 0; ok && i < 6; i++) ok = isalnum((unsigned char)id[i]);
        char ds[4], fs[4];
        tp_param(q ? q : "", "del", ds, sizeof(ds));
        tp_param(q ? q : "", "fin", fs, sizeof(fs));
        if (!ok) tp_reply(c, 400, "text/plain", "", 0);
        else if (ds[0] || fs[0]) {                           /* v297 : del=1 : effacer les pages (avant d'en envoyer d'autres) ; fin=1 : relire */
            if (ds[0]) for (int k = 1; k <= 99; k++) { snprintf(p, sizeof(p), GCD "/notices/%s/%02d.jpg", id, k); remove(p); }
            nt_id[0] = 0;                                      /* (la fiche ouverte sur la tele : recompter) */
            tp_reply(c, 200, "application/json", "{\"ok\":1}", 8);
        }
        else if (ps[0]) {
            int n = atoi(ps);
            struct stat st;
            snprintf(p, sizeof(p), GCD "/notices/%s/%02d.jpg", id, n);
            if (n >= 1 && n <= 99 && stat(p, &st) == 0) sh_send_file(c, p, 0, (char *)req);
            else tp_reply(c, 404, "text/plain", "", 0);
        } else {
            struct stat st;
            int n = 0;
            while (n < 99) { snprintf(p, sizeof(p), GCD "/notices/%s/%02d.jpg", id, n + 1); if (stat(p, &st) != 0) break; n++; }
            char j[24]; int l = snprintf(j, sizeof(j), "{\"n\":%d}", n);
            tp_reply(c, 200, "application/json", j, l);
        }
    }
    else if (!strcmp(target, "/api/photo")) {
        char is[12];
        tp_param(q ? q : "", "i", is, sizeof(is));
        int i = atoi(is);
        if (is[0] && pf && i >= 0 && i < npf && !pf[i].video) { char p[NAME_LEN]; snprintf(p, sizeof(p), "%s", pf[i].path); sh_send_file(c, p, 0, (char *)req); }
        else tp_reply(c, 404, "text/plain", "", 0);
    }
    else if (!strcmp(target, "/api/viz")) {                  /* v309 : le spectre (barres 0..255, de gauche a droite) pour l'appli */
        u8 vb[NBARS];
        for (int i = 0; i < NBARS; i++) { float v = bar_val[i]; vb[i] = v <= 0.0f ? 0 : v >= 1.0f ? 255 : (u8)(v * 255.0f); }
        tp_reply(c, 200, "application/octet-stream", (const char *)vb, NBARS);
    }
    else if (!strcmp(target, "/api/show")) {                 /* v142 : la photo (ou video) sur la tele */
        char is[12];
        tp_param(q ? q : "", "i", is, sizeof(is));
        int ok = is[0] && atoi(is) >= 0 && atoi(is) < npf, cache = !tab_shown(TAB_PHOTO);   /* v308 : onglet PHOTOS cache : dit au telephone */
        if (ok && !cache) tp_push(PC_SHOW, atoi(is));
        const char *rp = cache ? "{\"ok\":0,\"err\":\"onglet\"}" : ok ? "{\"ok\":1}" : "{\"ok\":0}";
        tp_reply(c, ok || cache ? 200 : 400, "application/json", rp, (int)strlen(rp));
    }
    else if (!strcmp(target, "/api/queue")) {                /* v142 : la file de lecture (a suivre) */
        sbuf_t sb = { NULL, 0, 0 };
        char m[24];
        sb_add(&sb, "[");
        int p = qpos, n = qn, w = 0;
        for (int i = p + 1; i < n && w < 40 && p >= 0; i++, w++) { snprintf(m, sizeof(m), w ? ",%d" : "%d", queue[i]); sb_add(&sb, m); }
        sb_add(&sb, "]");
        tp_reply(c, 200, "application/json", sb.b, sb.b ? sb.n : 0);
        free(sb.b);
    }
    else if (!strcmp(target, "/api/salon")) {                /* v142 : les autres GC Station (Salon) */
        sbuf_t sb = { NULL, 0, 0 };
        sb_add(&sb, salon_on ? "{\"on\":1,\"c\":[" : "{\"on\":0,\"c\":[");
        for (int k = 0; k < salon_n; k++) {
            if (k) sb_add(&sb, ",");
            sb_add(&sb, "["); sb_json_str(&sb, salon_peers[k].name); sb_add(&sb, ","); sb_json_str(&sb, salon_peers[k].ip);
            sb_add(&sb, ","); sb_json_str(&sb, salon_peers[k].now); sb_add(&sb, "]");
        }
        sb_add(&sb, "]}");
        tp_reply(c, 200, "application/json", sb.b, sb.b ? sb.n : 0);
        free(sb.b);
    }
    else if (!strcmp(target, "/api/listen")) {
        char is[12];
        tp_param(q ? q : "", "i", is, sizeof(is));
        int ok = is[0] && atoi(is) >= 0 && atoi(is) < salon_n;
        if (ok) tp_push(PC_SALON, atoi(is));
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/api/playurl") || !strcmp(target, "/api/radioadd")) {   /* v253 : radio du globe du telephone */
        int ok = !tp_rurl_busy;
        if (ok) {
            tp_param(q ? q : "", "u", tp_rurl, sizeof(tp_rurl));
            tp_param(q ? q : "", "n", tp_rname, sizeof(tp_rname));
            for (char *k = tp_rname; *k; k++) if (*k == '|' || *k == '\n' || *k == '\r') *k = ' ';
            ok = !strncasecmp(tp_rurl, "http", 4) && !strchr(tp_rurl, '|') && !strchr(tp_rurl, '\n');
            if (ok) { tp_rurl_busy = 1; tp_push(!strcmp(target, "/api/playurl") ? PC_PLAYURL : PC_RADIOADD, 0); }
        }
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
    }
    else if (!strcmp(target, "/api/cmd")) {
        static const char *const cmds[] = { "", "toggle", "next", "prev", "stop", "volup", "voldown", "play", "radio", "party", "layout" };
        char cmd[16], is[16];
        tp_param(q ? q : "", "c", cmd, sizeof(cmd));
        tp_param(q ? q : "", "i", is, sizeof(is));
        int found = 0;
        for (int k = 1; k <= PC_LAYOUT; k++) if (!strcmp(cmd, cmds[k])) { tp_push(k, is[0] ? atoi(is) : -1); found = 1; }
        if (!strcmp(cmd, "qplay") && is[0]) { tp_push(PC_QPLAY, atoi(is)); found = 1; }   /* v311 : un morceau de la file "a suivre" (sa place), sans la refaire */
        tp_reply(c, found ? 200 : 400, "application/json", found ? "{\"ok\":1}" : "{\"ok\":0}", found ? 8 : 8);
    } else tp_reply(c, 404, "text/plain", "", 0);
}

/* v247 : lancement d'un jeu demande depuis le telephone : l'appli continuait a demander des jaquettes, et le fil du
   serveur lisait la carte SD pendant qu'elle etait demontee (ecran rouge : DSI dans _fstat_r <- fseek <- tp_handle).
   Avant de demonter la carte (bg_quiesce) : plus de nouvelle demande pendant 20 s, et celle en cours finit d'abord
   (v271 : l'envoi en cours aussi : il s'arrete, fichier efface). */
static volatile s32 tpu_c;
static void tp_park(void) {
    tp_park_t = now_ms();
    u64 t0 = now_ms();
    while ((tp_busy || tpu_c >= 0) && now_ms() - t0 < 3000) usleep(2000);
}
static void *phone_thread(void *arg) {
    (void)arg;
    s32 ls = net_socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) { tp_state = PS_ERR; return NULL; }
    u32 one = 1;
    net_setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(PHONE_PORT);
    sa.sin_addr.s_addr = 0;                           /* toutes les adresses */
    if (net_bind(ls, (struct sockaddr *)&sa, sizeof(sa)) < 0 || net_listen(ls, 4) < 0) {
        net_close(ls);
        tp_state = PS_ERR;
        return NULL;
    }
    tp_state = PS_ON;
    tp_update_free();
    /* v272 : priorite 66, au-dessus de la boucle (64) : il dort tant que rien n'arrive, mais des que des donnees sont la,
       il les prend (avant, 30 : il ne passait qu'entre deux images et l'envoi se trainait) */
    if (LWP_CreateThread(&tpu_h, tpu_thread, NULL, tpu_stack, sizeof(tpu_stack), 66) < 0) tpu_c = -2;   /* v271 (sans fil : 503) */
    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        s32 c = net_accept(ls, (struct sockaddr *)&ca, &cl);
        if (c < 0) { usleep(50000); continue; }
        set_nonblock(c, 1);
        tp_busy = 1;                                     /* (avant le test : tp_park attend la fin) */
        if (tp_park_t && now_ms() - tp_park_t < 20000) { tp_busy = 0; net_close(c); continue; }   /* v247 : carte SD demontee */
        tp_taken = 0;
        tp_handle(c);                                    /* (v137 : onglet enleve : seulement les partages, voir tp_handle) */
        tp_busy = 0;
        if (!tp_taken) net_close(c);
    }
    return NULL;
}

static void phone_start(void) {
    if (tp_state != PS_OFF) return;
    tp_state = PS_START;
    tp_json_build();
    if (LWP_CreateThread(&tp_thread_h, phone_thread, NULL, tp_stack, sizeof(tp_stack), 30) < 0) tp_state = PS_ERR;
}

static void phone_enter(void) { tp_wanted = 1; }

/* ---------- v266 : l'appli du telephone en direct (UDP 51236, pris par la boucle, serveur du telephone actif) ----------
   - "GCSQ1" (a tout le reseau) -> "GCSA1\t<nom>\t<port http>\t<build>\n" : l'appli trouve la console tout de suite ;
   - "GCSS1" (toutes les 10 s) : abonnement de cet appareil pour 30 s -> "GCSK1" ;
   - aux abonnes : "GCSN1\t<numero>" quand l'etat change (titre, pause, volume, paroles, position sautee...) : l'appli
     relit /api/state tout de suite au lieu de demander chaque seconde ;
     "GCSG1\t<id>\t<nom>\t<minutes restantes, -1 sans limite>\t<profil>" juste avant de lancer un jeu (la console ne
     repond plus pendant le jeu : l'appli affiche la partie en cours) ; "GCSE1" si le lancement a echoue. */
#define TPL_PORT 51236
#define TPL_MAX 6
static s32 tpl_fd = -1;
static const char *salon_myname(void);                   /* (salon.c) */
static struct { u32 ip; u16 port; u64 seen; } tpl_sub[TPL_MAX];
static int tpl_n = 0;
static u32 tpl_seq = 0, tpl_hash = 0;
static int tpl_el = -1;
static u64 tpl_el_t = 0, tpl_sent_t = 0;
static int tpl_pending = 0;
static void tpl_send_to(u32 ip, u16 port, const char *m, int n) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(ip);
    net_sendto(tpl_fd, m, n, 0, (struct sockaddr *)&a, sizeof(a));
}
static void tpl_send_all(const char *m) {
    if (tpl_fd < 0) return;
    u64 now = now_ms();
    for (int i = 0; i < tpl_n; i++) if (now - tpl_sub[i].seen < 30000) tpl_send_to(tpl_sub[i].ip, tpl_sub[i].port, m, (int)strlen(m));
}
/* l'etat a-t-il change (sans compter la position qui avance toute seule) ? */
static void tpl_check_state(void) {
    const char *j = tp_json[tp_json_cur];
    u32 h = 2166136261u;
    int el = -1, pl = 0;
    for (const char *p = j; *p; p++) {
        if (*p == ',' && !strncmp(p, ",\"el\":", 6)) { el = atoi(p + 6); p += 6; while (p[1] >= '0' && p[1] <= '9') p++; continue; }
        if (*p == ',' && !strncmp(p, ",\"free\":", 8)) { p += 8; while (p[1] == '-' || (p[1] >= '0' && p[1] <= '9')) p++; continue; }
        h = (h ^ (u8)*p) * 16777619u;
    }
    const char *q = strstr(j, ",\"playing\":");
    if (q) pl = atoi(q + 11);
    u64 now = now_ms();
    int jump = 0;
    if (el >= 0 && tpl_el >= 0) {                          /* position sautee (avance ou recul) */
        int dt = (int)((now - tpl_el_t) / 1000);
        if (el < tpl_el || el > tpl_el + (pl ? dt + 2 : 1)) jump = 1;
    }
    if (h != tpl_hash || jump) tpl_pending = 1;
    tpl_hash = h; tpl_el = el; tpl_el_t = now;
    if (tpl_pending && now - tpl_sent_t > 150) {
        tpl_pending = 0; tpl_sent_t = now;
        char m[32];
        snprintf(m, sizeof(m), "GCSN1\t%u\n", (unsigned)++tpl_seq);
        tpl_send_all(m);
    }
}
static int tpl_live(void) {                               /* un appareil abonne ? */
    u64 now = now_ms();
    for (int i = 0; i < tpl_n; i++) if (now - tpl_sub[i].seen < 30000) return 1;
    return 0;
}
static void tpl_pump(void) {
    if (tpl_fd < 0) {
        static u64 tried = 0;
        if (tried && now_ms() - tried < 10000) return;
        tried = now_ms();
        tpl_fd = net_socket(AF_INET, SOCK_DGRAM, 0);
        if (tpl_fd < 0) return;
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_port = htons(TPL_PORT); a.sin_addr.s_addr = INADDR_ANY;
        if (net_bind(tpl_fd, (struct sockaddr *)&a, sizeof(a)) < 0) { net_close(tpl_fd); tpl_fd = -1; return; }
        set_nonblock(tpl_fd, 1);
    }
    for (int k = 0; k < 8; k++) {
        char m[64];
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        s32 r = net_recvfrom(tpl_fd, m, sizeof(m) - 1, 0, (struct sockaddr *)&from, &fl);
        if (r <= 0) break;
        m[r] = 0;
        u32 ip = ntohl(from.sin_addr.s_addr);
        u16 port = ntohs(from.sin_port);
        if (!strncmp(m, "GCSQ1", 5)) {
            char a[120];
            int n = snprintf(a, sizeof(a), "GCSA1\t%s\t%d\t%d\n", salon_myname(), PHONE_PORT, (int)GCMP3_BUILD);
            tpl_send_to(ip, port, a, n);
        } else if (!strncmp(m, "GCSS1", 5)) {
            int i = 0;
            while (i < tpl_n && !(tpl_sub[i].ip == ip && tpl_sub[i].port == port)) i++;
            if (i == tpl_n) {
                if (tpl_n < TPL_MAX) tpl_n++;
                else { u64 o = ~0ull; for (int j = 0; j < TPL_MAX; j++) if (tpl_sub[j].seen < o) { o = tpl_sub[j].seen; i = j; } }
                crumb("telephone : appareil en direct %u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)(ip >> 16 & 255), (unsigned)(ip >> 8 & 255), (unsigned)(ip & 255));
            }
            tpl_sub[i].ip = ip; tpl_sub[i].port = port; tpl_sub[i].seen = now_ms();
            char a[32];
            int n = snprintf(a, sizeof(a), "GCSK1\t%u\n", (unsigned)tpl_seq);
            tpl_send_to(ip, port, a, n);
        }
    }
}
/* (jeux_cfg.c) un jeu va se lancer / le lancement a echoue */
static int pj_left_min(void);
static u32 na_padmask(void);                             /* (prive, natif.c) */
static int na_mem_on(void);                              /* (v306, natif.c) */
static void tpl_game(const char *id, const char *name) {
    if (tpl_fd < 0 || !tpl_live()) return;
    static char last[8] = "";
    static u64 last_t = 0;
    if (!strcmp(last, id ? id : "") && now_ms() - last_t < 5000) return;   /* (deja dit : appels imbriques) */
    snprintf(last, sizeof(last), "%s", id ? id : ""); last_t = now_ms();
    char nm[160], m[260];
    tp_any_utf8(name ? name : "", nm, sizeof(nm));
    for (char *p = nm; *p; p++) if (*p == '\t' || *p == '\n') *p = ' ';
    snprintf(m, sizeof(m), "GCSG1\t%s\t%s\t%d\t%s\t%u\t%d\n", id ? id : "", nm, pj_left_min(), gp_name, (unsigned)na_padmask(), na_mem_on());   /* (+ ports des manettes du telephone ; v306 : chercheur de codes) */
    for (int k = 0; k < 2; k++) { tpl_send_all(m); usleep(20000); }   /* (deux fois : UDP, et le jeu coupe le reseau juste apres) */
}
static void tpl_game_fail(void) { tpl_send_all("GCSE1\n"); }

static void transport_step(int d);

/* v279 : manettes du telephone pour GC Station (mini-jeux, menus) : UDP 51238, "GCPD" {1, joueur 0-3, 0, 0} {numero}
   {INBUFH} {INBUFL} (meme format que pour Pulse) ; une fois par image */
#define PP_PORT 51238
static s32 pp_fd = -1;
static u32 pp_seq[4];
static void pp_pump(void) {
    for (int c = 0; c < 4; c++) pp_prev[c] = pp_held[c];
    if (pp_fd < 0) {
        static u64 tried = 0;
        if (net_phase != NET_DONE || !net_ok || (tried && now_ms() - tried < 10000)) return;
        tried = now_ms();
        pp_fd = net_socket(AF_INET, SOCK_DGRAM, 0);
        if (pp_fd < 0) return;
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_port = htons(PP_PORT); a.sin_addr.s_addr = INADDR_ANY;
        if (net_bind(pp_fd, (struct sockaddr *)&a, sizeof(a)) < 0) { net_close(pp_fd); pp_fd = -1; return; }
        set_nonblock(pp_fd, 1);
    }
    u64 now = now_ms();
    for (int k = 0; k < 16; k++) {
        u8 m[32];
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        s32 r = net_recvfrom(pp_fd, m, sizeof(m), 0, (struct sockaddr *)&from, &fl);
        if (r <= 0) break;
        if (r < 20 || memcmp(m, "GCPD", 4)) continue;
        int c = m[5] & 3;
        u32 seq = (u32)m[8] << 24 | (u32)m[9] << 16 | (u32)m[10] << 8 | m[11];
        u32 h = (u32)m[12] << 24 | (u32)m[13] << 16 | (u32)m[14] << 8 | m[15];
        u32 l = (u32)m[16] << 24 | (u32)m[17] << 16 | (u32)m[18] << 8 | m[19];
        if (seq == pp_seq[c] && pp_seen[c]) continue;
        if (!pp_seen[c] || now - pp_seen[c] > 1500) crumb("telephone : manette du joueur %d", c + 1);
        pp_seq[c] = seq; pp_seen[c] = now;
        pp_held[c] = (h >> 16) & 0x1F7Fu;                /* (START Y X B A, L R Z, croix : comme PAD_BUTTON_*) */
        int mx = (int)((h >> 8) & 0xFF) - 128, my = (int)(h & 0xFF) - 128, cx = (int)(l >> 24) - 128, cy = (int)((l >> 16) & 0xFF) - 128;
        pp_mx[c] = (s8)(mx < -127 ? -127 : mx > 127 ? 127 : mx); pp_my[c] = (s8)(my < -127 ? -127 : my > 127 ? 127 : my);
        pp_cx[c] = (s8)(cx < -127 ? -127 : cx > 127 ? 127 : cx); pp_cy[c] = (s8)(cy < -127 ? -127 : cy > 127 ? 127 : cy);
        if (((l >> 8) & 0xFF) > 0xC0) pp_held[c] |= PAD_TRIGGER_L;
        if ((l & 0xFF) > 0xC0) pp_held[c] |= PAD_TRIGGER_R;
    }
    for (int c = 0; c < 4; c++) if (pp_seen[c] && now - pp_seen[c] > 1500) { pp_held[c] = 0; pp_mx[c] = pp_my[c] = pp_cx[c] = pp_cy[c] = 0; }   /* (telephone parti) */
}
static void phone_pump(void) {
    pp_pump();                                           /* v279 */
    c8_tick();                                           /* v276 : la table du jeu de cartes s'ouvre sur la tele */
    if (tp_state == PS_OFF && (tp_wanted || phone_auto) && net_phase == NET_DONE && net_ok) phone_start();
    if (tp_state != PS_ON) return;
    if (!tp_qr_ok && ip_str[0]) {
        if (PHONE_PORT == 80) snprintf(tp_url, sizeof(tp_url), "http://%s/", ip_str);
        else snprintf(tp_url, sizeof(tp_url), "http://%s:%d/", ip_str, PHONE_PORT);
        static u8 tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
        tp_qr_ok = qrcodegen_encodeText(tp_url, tmp, tp_qr, qrcodegen_Ecc_MEDIUM, 1, 8, qrcodegen_Mask_AUTO, true);
        if (!tp_qr_ok) tp_qr_ok = -1;
    }
    u64 now = now_ms();
    /* v45 : etat pour le telephone : souvent s'il est connecte, sinon toutes les 3 s (moins de travail) */
    tpl_pump();                                           /* v266 : l'appli du telephone en direct */
    int live = tpl_live();
    if (now - tp_json_t > (live || (tp_last_req && now - tp_last_req < 15000) ? 300u : 3000u)) { tp_json_t = now; tp_json_build(); if (live) tpl_check_state(); }
    if (!tp_cfg_reading && (tp_cfg_now || (now - tp_cfg_t > 1000 && now - tp_last_req < 15000))) { tp_cfg_now = 0; tp_cfg_t = now; tp_cfg_build(); }
#ifndef GEKKO
    if ((tp_cap == 1 || tp_cap == 3) && !pcsim_capture) { snprintf(pcsim_capture_name, 96, tp_cap == 3 ? "#ecran" : "capture-telephone"); pcsim_capture = 1; tp_cap = 2; }   /* (v234 ; # : sans fichier) */
    else if (tp_cap == 2 && !pcsim_capture) tp_cap = 0;
#endif
    if (tp_tap_retry > 0 && !tp_touch) {                  /* v303 : la zone touchee est apparue ? */
        if (gp_on && gp_usable() && gp_hit() >= 0) { tp_keys |= PAD_BUTTON_A; tp_tap_retry = 0; } else tp_tap_retry--;
    }
    if (tp_touch) {                                       /* v303 : tele tactile : la fleche (ou le pointeur du web) va la ou le doigt touche */
        tp_touch = 0;
        f32 u = tp_tu, v = tp_tv;
        int cc = tp_tc;
        saver_on = 0; last_input_ms = now_ms();
        if (tab == TAB_WEB) {
            input_set_cursor(u * gfx_width(), v * 480);
            if (cc) input_phone(0, 0, 0, 0, cc == 1 ? 1 : 2);
        } else if (gp_usable()) {
            f32 sx = scr_x0 + u * scr_w;                      /* (image de la tele -> repere de l'interface ; 16:9 compris) */
            gp_x = (sx - ux_o) / ux_k; gp_y = v * 480;
            if (gp_x < 2) gp_x = 2; if (gp_x > 636) gp_x = 636;
            if (gp_y < 2) gp_y = 2; if (gp_y > 476) gp_y = 476;
            gp_on = 1; gp_t = now_ms();
            tp_tap_retry = 0;
            if (cc == 1 && gp_hit() >= 0) tp_keys |= PAD_BUTTON_A;   /* (seulement sur quelque chose de cliquable) */
            else if (cc == 1) tp_tap_retry = 8;                       /* (rien encore : quelques images de plus) */
            else if (cc == 2) tp_keys |= PAD_BUTTON_B;
        } else if (cc) tp_keys |= cc == 1 ? PAD_BUTTON_A : PAD_BUTTON_B;   /* (pas de fleche ici : comme avant) */
    }
    if (tp_mdx || tp_mdy || tp_mw || tp_mc) {             /* v231 : souris tactile */
        int dx = tp_mdx, dy = tp_mdy, w = tp_mw, mc = tp_mc;
        tp_mdx -= dx; tp_mdy -= dy; tp_mw -= w; tp_mc &= ~mc;
        if (tab != TAB_WEB && tab_shown(TAB_WEB) && !menu_open && !tl_screen && !kb_open) { saver_on = 0; tab = TAB_WEB; tab_enter(TAB_WEB); }
        if (tab == TAB_WEB) { saver_on = 0; last_input_ms = now_ms(); input_phone(dx * 0.8f, dy * 0.8f, (f32)w, 0, mc); }
    }
    while (tp_qh != tp_qt) {
        phcmd_t cm = tp_q[tp_qh];
        tp_qh = (tp_qh + 1) % 16;
        crumb("telephone : commande %d (%d)", cm.c, cm.i);
        switch (cm.c) {
        case PC_TOGGLE:
            if (src == SRC_FILE && playing) paused = !paused;
            else if (src == SRC_RADIO && state != ST_IDLE) stop_station();
            else if (src == SRC_NONE && cur_track >= 0 && cur_track < nfiles) { queue_build(cur_track); start_track(cur_track); }
            break;
        case PC_NEXT: transport_step(1); break;
        case PC_PREV: transport_step(-1); break;
        case PC_STOP: stop_track(); stop_station(); break;
        case PC_VOLUP: volume_pct = volume_pct + 10 > 100 ? 100 : volume_pct + 10; settings_dirty = 1; break;
        case PC_VOLDOWN: volume_pct = volume_pct - 10 < 0 ? 0 : volume_pct - 10; settings_dirty = 1; break;
        case PC_PLAY: if (cm.i >= 0 && cm.i < nfiles) { queue_build(cm.i); start_track(cm.i); select_track_row(cm.i); } break;
        case PC_QPLAY: {                                 /* v311 : i = place dans "a suivre" (0 = le suivant) : la file reste */
            int p = qpos + 1 + cm.i;
            if (cm.i >= 0 && qn > 0 && qpos >= 0 && qpos < qn && cur_track >= 0 && queue[qpos] == cur_track && p < qn) { play_queue_pos(p); select_track_row(queue[p]); }
            break; }
        case PC_RADIO: if (cm.i >= 0 && cm.i < nstations) { net_start_async(); start_station(cm.i, 0); } break;
        case PC_PARTY: if (party_on) party_on = 0; else party_start(); break;
        case PC_LAYOUT: layout_next(); break;
        case PC_OPEN: web_open_from_phone(tp_open_url); tp_open_busy = 0; break;
        case PC_MENU:
            if (cm.i / 2 == MI_UPDATE) update_activate();
            else if (cm.i / 2 != MI_ELEMS && cm.i / 2 != MI_TABS) menu_change(cm.i / 2, (cm.i & 1) ? 1 : -1);
            tp_cfg_now = 1;
            break;
        case PC_CHECK:
            if (cm.i >= 100) ck_tab_toggle(cm.i - 100); else ck_el_toggle(cm.i);
            settings_dirty = 1;
            tp_cfg_now = 1;
            break;
        case PC_SET: {
            char v[600];
            snprintf(v, sizeof(v), "%s", tp_set_v);
            char *t = trim(v);
            if (!strcmp(tp_set_k, "ville")) infos_set_city(t);
            else {
                setting_apply(tp_set_k, t);
                if (!strcmp(tp_set_k, "actualites")) news_t = 0;
            }
            settings_dirty = 1;
            tp_set_busy = 0;
            tp_cfg_now = 1;
            break;
        }
        case PC_SHOW:                                    /* v142 : photo choisie sur le telephone */
            if (!tab_shown(TAB_PHOTO)) break;
            if (npf < 0) photo_rescan();
            /* (v222 : clavier ferme ; v308 : economiseur d'ecran et veilleuse coupes : la photo s'ouvrait derriere) */
            if (cm.i >= 0 && cm.i < npf) { saver_on = 0; night_on = 0; last_input_ms = now_ms(); menu_open = 0; tl_screen = 0; kb_open = 0; tab = TAB_PHOTO; tab_enter(TAB_PHOTO); pv_open(cm.i); }
            break;
        case PC_TYPE: tp_type_apply(); tp_type_busy = 0; break;
        case PC_TEST: sx_phone_start(cm.i); break;   /* v278 */
        case PC_MINIJEU: gm_phone_launch(cm.i); break;   /* v279 */
        case PC_SHOWNAME: {
            if (tab_shown(TAB_PHOTO)) {
                tp_rescan &= ~2;
                photo_rescan();
                int k = tp_find_photo(tp_shown_name);
                if (k >= 0) { saver_on = 0; night_on = 0; last_input_ms = now_ms(); menu_open = 0; tl_screen = 0; kb_open = 0; tab = TAB_PHOTO; tab_enter(TAB_PHOTO); pv_open(k); }
            }
            tp_shown_busy = 0;
            break;
        }
        case PC_PLAYURL:                                 /* v253 : une radio choisie sur le globe du telephone */
            if (nstations < MAX_STATIONS) {
                snprintf(stations[nstations].name, sizeof(stations[0].name), "%s", tp_rname[0] ? tp_rname : "Radio");
                snprintf(stations[nstations].url, sizeof(stations[0].url), "%s", tp_rurl);
                net_start_async();
                start_station(nstations, 0);
            }
            tp_rurl_busy = 0;
            break;
        case PC_RADIOADD: {                              /* v253 : ajoutee a tes radios */
            int have = 0;
            for (int k = 0; k < nstations; k++) if (!strcmp(stations[k].url, tp_rurl)) have = 1;
            if (!have && nstations < MAX_STATIONS - 1) {
                snprintf(stations[nstations].name, sizeof(stations[0].name), "%s", tp_rname[0] ? tp_rname : "Radio");
                snprintf(stations[nstations].url, sizeof(stations[0].url), "%s", tp_rurl);
                nstations++;
                FILE *f = sd_ok ? fopen(RADIO_LIST, "a") : NULL;
                if (f) { fprintf(f, "%s | %s\n", tp_rname[0] ? tp_rname : "Radio", tp_rurl); fclose(f); }
                char m[200]; snprintf(m, sizeof(m), "Ajout\xC3\xA9" "e \xC3\xA0 tes radios : %s", tp_rname);
                lib_note(m);
            }
            tp_rurl_busy = 0;
            break;
        }
        case PC_SALON: { const char *e = salon_listen(cm.i); if (e) lib_note(e); break; }
        }
    }
    /* v224 : les relectures de la carte attendent la fin de la serie d'envois (elles figeaient l'ecran pendant les envois) */
    if ((tp_rescan & 5) && !tp_up_active && now_ms() - tp_up_end > 2000) {
        int w = tp_rescan & 5;
        tp_rescan &= ~5;
        if (w & 1) { tp_add_r = tp_add_w; music_rescan(); }
        retro_rescan();
    }
    while (tp_add_r != tp_add_w) { music_add_file(tp_add_path[tp_add_r % 8]); tp_add_r++; }
    if (tp_rescan & 2) { tp_rescan &= ~2; photo_rescan(); }
    if (tp_jaq_new && !tp_up_active) { tp_jaq_new = 0; gl_art_recu(); }   /* v301 */
}

/* v51 : n'importe quel QR code (aussi celui du rapport d'analyse d'un jeu) */
static void draw_qr_buf(const u8 *qr, f32 x, f32 y, f32 size) {
    int n = qrcodegen_getSize(qr);
    f32 m = size / (n + 8);
    uxsave_t uz = ux_native(x + size / 2);
    gx_rect(x, y, size, size, RGBA(255, 255, 255, 255));
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            if (qrcodegen_getModule(qr, i, j)) gx_rect(x + (i + 4) * m, y + (j + 4) * m, m + 0.3f, m + 0.3f, RGBA(0, 0, 0, 255));
    ux_restore(uz);
}
/* ---------- v55 : partage, cote GameCube (fenetre du QR code) ---------- */
static int sh_on = 0, sh_cur = -1, sh_qr_ok = 0;
static char sh_url[96];
static u8 sh_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
static void share_open(const char *path, int dir) {
    if (!path || !path[0]) return;
    int k = -1;
    for (int i = 0; i < SH_MAX; i++) if (sh_tab[i].tok[0] && !strcmp(sh_tab[i].path, path)) k = i;   /* (deja partage : meme lien) */
    if (k < 0) {
        static const char al[] = "abcdefghjkmnpqrstuvwxyz23456789";
        static u32 seed = 0;
        seed ^= (u32)gettime() ^ (u32)(gettime() >> 32) ^ (u32)(now_ms() * 2654435761u);
        k = sh_next; sh_next = (sh_next + 1) % SH_MAX;
        share_t *s = &sh_tab[k];
        s->tok[0] = 0;                                  /* (le fil du serveur ne voit pas un lien a moitie ecrit) */
        snprintf(s->path, sizeof(s->path), "%s", path);
        s->dir = (u8)(dir != 0);
        char t[9];
        for (int i = 0; i < 8; i++) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; t[i] = al[seed % (sizeof(al) - 1)]; }
        t[8] = 0;
        memcpy(s->tok + 1, t + 1, 8);
        s->tok[0] = t[0];
    }
    sh_cur = k; sh_on = 1; sh_qr_ok = 0; sh_url[0] = 0;
    if (tp_state != PS_ON) { tp_wanted = 1; net_start_async(); }   /* (le serveur demarre tout seul) */
}
static int share_input(u32 down) {                      /* 1 = touche prise par la fenetre */
    if (!sh_on) return 0;
    if (down & (PAD_BUTTON_B | PAD_BUTTON_A)) sh_on = 0;
    return 1;
}
static void share_draw(void) {
    if (!sh_on || sh_cur < 0) return;
    const share_t *s = &sh_tab[sh_cur];
    if (!sh_qr_ok && tp_state == PS_ON && ip_str[0]) {
        if (PHONE_PORT == 80) snprintf(sh_url, sizeof(sh_url), "http://%s/p/%s", ip_str, s->tok);
        else snprintf(sh_url, sizeof(sh_url), "http://%s:%d/p/%s", ip_str, PHONE_PORT, s->tok);
        static u8 tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
        sh_qr_ok = qrcodegen_encodeText(sh_url, tmp, sh_qr, qrcodegen_Ecc_MEDIUM, 1, 8, qrcodegen_Mask_AUTO, true) ? 1 : -1;
    }
    const f32 w = 460, h = 320, x = 90, y = 86;
    char buf[NAME_LEN], d[NAME_LEN];
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
    gx_rrect_fill(x, y, w, h, 12, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
    draw_panel(x, y, w, h);
    gx_text_c(F_MED, 320, y + 28, s->dir ? "Partager ce dossier" : "Partager ce fichier", RGBA(255, 255, 255, 255));
    text_display(base_of(s->path), d, sizeof(d));
    fit_text(F_SMALL, d, (int)w - 40, buf, sizeof(buf));
    gx_text_c(F_SMALL, 320, y + 48, buf, RGBA(200, 185, 255, 255));
    if (sh_qr_ok > 0) {
        draw_qr_buf(sh_qr, 320 - 90, y + 60, 180);
        gx_text_c(F_SMALL, 320, y + 262, sh_url, RGBA(255, 210, 120, 255));
        gx_text_c(F_SMALL, 320, y + 282, "Scanne le QR code avec ton t\xC3\xA9l\xC3\xA9phone (m\xC3\xAAme Wi-Fi)", RGBA(190, 180, 235, 255));
    } else if (tp_state == PS_ERR || (net_phase == NET_DONE && !net_ok)) {
        gx_text_c(F_SMALL, 320, y + 150, "Pas de r\xC3\xA9seau : branche le Broadband Adapter", RGBA(255, 160, 120, 255));
    } else {
        gx_text_c(F_SMALL, 320, y + 150, "D\xC3\xA9marrage du serveur...", RGBA(190, 180, 235, 255));
    }
    gx_text_c(F_SMALL, 320, y + h - 10, "B : fermer   (le lien marche tant que GC Station est allum\xC3\xA9)", RGBA(255, 210, 120, 255));
}

static void draw_qr(f32 x, f32 y, f32 size) {
    int n = qrcodegen_getSize(tp_qr);
    f32 m = size / (n + 8);                             /* marge blanche de 4 modules */
    uxsave_t uz = ux_native(x + size / 2);              /* (16:9 : QR code carre) */
    gx_rect(x, y, size, size, RGBA(255, 255, 255, 255));
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            if (qrcodegen_getModule(tp_qr, i, j)) gx_rect(x + (i + 4) * m, y + (j + 4) * m, m + 0.3f, m + 0.3f, RGBA(0, 0, 0, 255));
    ux_restore(uz);
}

static void phone_input(u32 down, u32 nav, int fire) {
    (void)nav; (void)fire;
    if (down & PAD_BUTTON_A) { tp_wanted = 1; net_start_async(); }
}

/* v131 : une ligne qui tient dans maxw (sinon plus petite, puis coupee) : page plus etroite (style Vitrine) */
static void ph_text(int f, f32 x, f32 y, f32 maxw, const char *s, col_t c) {
    if (f == F_LARGE && text_width(F_LARGE, s, 0) > maxw) f = F_MED;
    if (f == F_MED && text_width(F_MED, s, 0) > maxw) f = F_SMALL;
    char b[200];
    fit_text(f, s, (int)maxw, b, sizeof(b));
    gx_text1(f, x, y, b, c);
}
static void draw_tab_phone(void) {
    char buf[200];
    draw_panel(11, 73, 618, 338);
    draw_tabs(NULL);
    if (tp_state == PS_ON && tp_qr_ok > 0) {
        int narrow = ux_k < 0.95f;                       /* (v131 : Vitrine : la page est plus etroite) */
        f32 qs = narrow ? 172 : 220, tx = narrow ? 34 + qs / 2 + (qs / 2 + 22) / ux_k : 280, tw = 616 - tx;
        draw_qr(34, 124, qs);
        ph_text(F_LARGE, tx, 148, tw, "Depuis ton t\xC3\xA9l\xC3\xA9phone", RGBA(255, 255, 255, 255));
        ph_text(F_MED, tx, 178, tw, "1. Connecte-le au m\xC3\xAAme Wi-Fi que la GameCube", RGBA(210, 200, 240, 255));
        ph_text(F_MED, tx, 200, tw, "2. Scanne le QR code, ou ouvre :", RGBA(210, 200, 240, 255));
        ph_text(F_LARGE, tx + 20, 230, tw - 20, tp_url, RGBA(190, 160, 255, 255));
        ph_text(F_SMALL, tx, 256, tw, "Envoie musique, photos et vid\xC3\xA9os sur la carte SD,", RGBA(160, 145, 215, 255));
        ph_text(F_SMALL, tx, 272, tw, "et pilote la lecture sans la manette.", RGBA(160, 145, 215, 255));
        if (tp_up_active && tp_up_total > 0) {
            char nm[120];
            fit_text(F_MED, tp_up_name, (int)(tw - 90), nm, sizeof(nm));
            snprintf(buf, sizeof(buf), "R\xC3\xA9" "ception : %s", nm);
            gx_text1(F_MED, tx, 304, buf, RGBA(255, 210, 120, 255));
            f32 frac = (f32)tp_up_done / (f32)tp_up_total;
            gx_rrect_fill(tx, 314, tw - 16, 6, 3, RGBA(70, 50, 140, 230), RGBA(70, 50, 140, 230));
            gx_hgrad(tx, 314, (tw - 16) * frac, 6, RGBA(120, 64, 255, 255), RGBA(180, 100, 255, 255));
            u64 el = now_ms() - tp_up_t0;
            long kbs = el > 500 ? (long)((long long)tp_up_done * 1000 / 1024 / (long long)el) : 0;
            snprintf(buf, sizeof(buf), "%ld / %ld Ko   %ld Ko/s", tp_up_done / 1024, tp_up_total / 1024, kbs);
            gx_text1(F_SMALL, tx, 334, buf, RGBA(180, 165, 235, 255));
        } else {
            int nl = tp_nlog;
            for (int i = 0; i < 3 && i < nl; i++) {
                char nm[140];
                fit_text(F_SMALL, tp_log[(nl - 1 - i) % 5], (int)tw, nm, sizeof(nm));
                gx_text1(F_SMALL, tx, 304 + i * 16, nm, RGBA(150, 220, 170, 255));
            }
        }
        if (tp_free_mb >= 0) {
            snprintf(buf, sizeof(buf), "Place libre : %ld Mo", tp_free_mb);
            gx_text1(F_SMALL, 34, 370, buf, RGBA(150, 135, 210, 255));
        }
        if (&gcmp3_tcp_wnd && gcmp3_tcp_wnd > 1460)
            snprintf(buf, sizeof(buf), "R\xC3\xA9seau rapide (fen\xC3\xAAtre %d Ko)", gcmp3_tcp_wnd / 1024);
        else snprintf(buf, sizeof(buf), "R\xC3\xA9seau standard");
        gx_text1(F_SMALL, tx, 370, buf, RGBA(150, 135, 210, 255));
        if (tp_last_req && now_ms() - tp_last_req < 5000)
            gx_text1(F_SMALL, 34, 388, "T\xC3\xA9l\xC3\xA9phone connect\xC3\xA9", RGBA(120, 230, 150, 255));
    } else {
        const char *m = tp_state == PS_ERR ? "Le serveur n'a pas pu d\xC3\xA9marrer" :
                        (net_phase == NET_DONE && !net_ok) ? "Pas de r\xC3\xA9seau : branche le Broadband Adapter" :
                        "D\xC3\xA9marrage du r\xC3\xA9seau...";
        gx_text1(F_LARGE, 40, 170, m, RGBA(255, 210, 120, 255));
        gx_text1(F_MED, 40, 200, "A : r\xC3\xA9" "essayer", RGBA(210, 200, 240, 255));
    }
    static const char *const k[] = { "A" };
    static const char *const l[] = { "D\xC3\x89MARRER" };
    draw_legend_keys(k, l, 1);
}

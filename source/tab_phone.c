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
enum { PC_TOGGLE = 1, PC_NEXT, PC_PREV, PC_STOP, PC_VOLUP, PC_VOLDOWN, PC_PLAY, PC_RADIO, PC_PARTY, PC_LAYOUT, PC_OPEN, PC_MENU, PC_CHECK, PC_SET, PC_UPDATE };
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
static volatile int tp_rescan = 0;                 /* 1 = musique, 2 = photos */
static volatile u64 tp_last_req = 0;
static volatile long tp_free_mb = -1;
static volatile u64 tp_up_t0 = 0;
/* fourni par la pile reseau recompilee (tools/fast_net.py) ; absent sinon */
extern const int gcmp3_tcp_wnd __attribute__((weak));

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
        if (i == MI_QUIT || i == MI_ELEMS || i == MI_TABS || i == MI_GAMES || i == MI_TITLES || i == MI_CHEATS) continue;
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
    snprintf(num, sizeof(num), ",\"playing\":%d,\"cov\":%d,\"free\":%ld}", playing_now, cover_ok && n.active, tp_free_mb); sb_add(&s, num);
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

static void tp_update_free(void) {
    struct statvfs sv;
    if (sd_ok && statvfs(SD_ROOT "/", &sv) == 0)
        tp_free_mb = (long)((unsigned long long)sv.f_bavail * sv.f_frsize / (1024 * 1024));
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
static void tp_upload(s32 c, const char *q, long len, const unsigned char *pre, int prelen) {
    static const char *const dirs[] = { "mp3", "downloads", "retro", "photos", "ecran", "videos", "radio", "podcasts" };
    char dir[32], name[200], path[NAME_LEN + 16];
    tp_param(q, "dir", dir, sizeof(dir));
    tp_param(q, "name", name, sizeof(name));
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
    if (!okdir || !clean[0] || len <= 0 || !sd_ok) { tp_reply(c, 400, "text/plain", "refus", 5); return; }
    const char *gpre = (!strcmp(dir, "radio") || !strcmp(dir, "podcasts")) ? "gc-mp3/" : "";   /* (v22 : rangees dans gc-mp3) */
    mkdir(GCD, 0777);
    snprintf(path, sizeof(path), SD_ROOT "/%s%s", gpre, dir);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), SD_ROOT "/%s%s/%s", gpre, dir, clean);
    struct stat st;
    for (int k = 2; k < 100 && stat(path, &st) == 0; k++) {     /* deja la : "nom (2).mp3" */
        char base[200], ext[16] = "";
        snprintf(base, sizeof(base), "%s", clean);
        char *dot = strrchr(base, '.');
        if (dot && strlen(dot) < sizeof(ext)) { snprintf(ext, sizeof(ext), "%s", dot); *dot = 0; }
        snprintf(path, sizeof(path), SD_ROOT "/%s%s/%s (%d)%s", gpre, dir, base, k, ext);
    }
    FILE *f = fopen(path, "wb");
    if (!f) { tp_reply(c, 500, "text/plain", "ecriture impossible", 19); return; }
    static char vbuf[65536];                    /* ecritures SD par gros blocs */
    setvbuf(f, vbuf, _IOFBF, sizeof(vbuf));
    tp_up_t0 = now_ms();
    snprintf(tp_up_name, sizeof(tp_up_name), "%s", clean);
    crumb("telephone : envoi %s/%s (%ld Ko)", dir, clean, len / 1024);
    tp_up_total = len;
    tp_up_done = 0;
    tp_up_active = 1;
    static unsigned char buf[32768];
    int ok = 1;
    if (prelen > 0) {
        int m = prelen > len ? (int)len : prelen;
        if (fwrite(pre, 1, m, f) != (size_t)m) ok = 0;
        tp_up_done = m;
    }
    while (ok && tp_up_done < len) {
        long want = len - tp_up_done;
        int r = tp_recv(c, buf, want > (long)sizeof(buf) ? (int)sizeof(buf) : (int)want, 20000);
        if (r <= 0) { ok = 0; break; }
        if (fwrite(buf, 1, r, f) != (size_t)r) { ok = 0; break; }
        tp_up_done += r;
    }
    fclose(f);
    tp_up_active = 0;
    char m[160];
    if (!ok) {
        remove(path);
        snprintf(m, sizeof(m), "\xC3\x89" "chec : %s", clean);
        tp_logf(m);
        tp_reply(c, 500, "text/plain", "envoi interrompu", 16);
        return;
    }
    snprintf(m, sizeof(m), "Re\xC3\xA7u : %s/%s", dir, clean);
    tp_logf(m);
    snprintf(tp_done_path[tp_done_w % 4], NAME_LEN, "%s", path);
    tp_done_w++;
    if (!strcmp(dir, "mp3") || !strcmp(dir, "downloads") || !strcmp(dir, "retro")) tp_rescan |= 1;
    if (!strcmp(dir, "photos") || !strcmp(dir, "videos")) tp_rescan |= 2;
    tp_update_free();
    tp_reply(c, 200, "text/plain", "ok", 2);
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
        if (!strcmp(target, "/upload") && cl > 0) tp_upload(c, q ? q : "", cl, req + hend, len - hend);
        else tp_reply(c, 400, "text/plain", "", 0);
        return;
    }
    if (!strcmp(target, "/") || !strcmp(target, "/index.html"))
        tp_reply(c, 200, "text/html; charset=utf-8", ui_en ? phone_page_en : phone_page, (int)strlen(ui_en ? phone_page_en : phone_page));
    else if (!strcmp(target, "/api/state")) {
        char tmp[sizeof(tp_json[0])];
        snprintf(tmp, sizeof(tmp), "%s", tp_json[tp_json_cur]);
        tp_reply(c, 200, "application/json", tmp, (int)strlen(tmp));
    } else if (!strcmp(target, "/api/tracks")) tp_send_tracks(c);
    else if (!strcmp(target, "/api/stations")) tp_send_stations(c);
    else if (!strcmp(target, "/api/titres")) {             /* titres de la radio gardes (v18) */
        FILE *f = sd_ok ? fopen(GCD "/gc-mp3-titres.txt", "rb") : NULL;
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
    else if (!strcmp(target, "/crash.txt")) {               /* journal de plantage */
        static char cb[9000];
        int n = 0;
        FILE *f = sd_ok ? fopen(GCD "/gc-mp3-crash.txt", "rb") : NULL;
        if (!f && sd_ok) f = fopen(GCD "/gc-mp3-crash-ancien.txt", "rb");
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
    else if (!strcmp(target, "/api/cmd")) {
        static const char *const cmds[] = { "", "toggle", "next", "prev", "stop", "volup", "voldown", "play", "radio", "party", "layout" };
        char cmd[16], is[16];
        tp_param(q ? q : "", "c", cmd, sizeof(cmd));
        tp_param(q ? q : "", "i", is, sizeof(is));
        int found = 0;
        for (int k = 1; k <= PC_LAYOUT; k++) if (!strcmp(cmd, cmds[k])) { tp_push(k, is[0] ? atoi(is) : -1); found = 1; }
        tp_reply(c, found ? 200 : 400, "application/json", found ? "{\"ok\":1}" : "{\"ok\":0}", found ? 8 : 8);
    } else tp_reply(c, 404, "text/plain", "", 0);
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
    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        s32 c = net_accept(ls, (struct sockaddr *)&ca, &cl);
        if (c < 0) { usleep(50000); continue; }
        set_nonblock(c, 1);
        tp_handle(c);
        net_close(c);
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

static void transport_step(int d);

static void phone_pump(void) {
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
    if (now - tp_json_t > 300) { tp_json_t = now; tp_json_build(); }
    if (!tp_cfg_reading && (tp_cfg_now || (now - tp_cfg_t > 1000 && now - tp_last_req < 15000))) { tp_cfg_now = 0; tp_cfg_t = now; tp_cfg_build(); }
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
        }
    }
    if (tp_rescan & 1) { tp_rescan &= ~1; music_rescan(); retro_rescan(); }
    if (tp_rescan & 2) { tp_rescan &= ~2; photo_rescan(); }
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

static void draw_tab_phone(void) {
    char buf[200];
    draw_panel(11, 73, 618, 338);
    draw_tabs(NULL);
    if (tp_state == PS_ON && tp_qr_ok > 0) {
        draw_qr(34, 124, 220);
        gx_text1(F_LARGE, 280, 148, "Depuis ton t\xC3\xA9l\xC3\xA9phone", RGBA(255, 255, 255, 255));
        gx_text1(F_MED, 280, 178, "1. Connecte-le au m\xC3\xAAme Wi-Fi que la GameCube", RGBA(210, 200, 240, 255));
        gx_text1(F_MED, 280, 200, "2. Scanne le QR code, ou ouvre :", RGBA(210, 200, 240, 255));
        gx_text1(F_LARGE, 300, 230, tp_url, RGBA(190, 160, 255, 255));
        gx_text1(F_SMALL, 280, 256, "Envoie musique, photos et vid\xC3\xA9os sur la carte SD,", RGBA(160, 145, 215, 255));
        gx_text1(F_SMALL, 280, 272, "et pilote la lecture sans la manette.", RGBA(160, 145, 215, 255));
        if (tp_up_active && tp_up_total > 0) {
            char nm[120];
            fit_text(F_MED, tp_up_name, 300, nm, sizeof(nm));
            snprintf(buf, sizeof(buf), "R\xC3\xA9" "ception : %s", nm);
            gx_text1(F_MED, 280, 304, buf, RGBA(255, 210, 120, 255));
            f32 frac = (f32)tp_up_done / (f32)tp_up_total;
            gx_rrect_fill(280, 314, 320, 6, 3, RGBA(70, 50, 140, 230), RGBA(70, 50, 140, 230));
            gx_hgrad(280, 314, 320 * frac, 6, RGBA(120, 64, 255, 255), RGBA(180, 100, 255, 255));
            u64 el = now_ms() - tp_up_t0;
            long kbs = el > 500 ? (long)((long long)tp_up_done * 1000 / 1024 / (long long)el) : 0;
            snprintf(buf, sizeof(buf), "%ld / %ld Ko   %ld Ko/s", tp_up_done / 1024, tp_up_total / 1024, kbs);
            gx_text1(F_SMALL, 280, 334, buf, RGBA(180, 165, 235, 255));
        } else {
            int nl = tp_nlog;
            for (int i = 0; i < 3 && i < nl; i++) {
                char nm[140];
                fit_text(F_SMALL, tp_log[(nl - 1 - i) % 5], 330, nm, sizeof(nm));
                gx_text1(F_SMALL, 280, 304 + i * 16, nm, RGBA(150, 220, 170, 255));
            }
        }
        if (tp_free_mb >= 0) {
            snprintf(buf, sizeof(buf), "Place libre : %ld Mo", tp_free_mb);
            gx_text1(F_SMALL, 34, 370, buf, RGBA(150, 135, 210, 255));
        }
        if (&gcmp3_tcp_wnd && gcmp3_tcp_wnd > 1460)
            snprintf(buf, sizeof(buf), "R\xC3\xA9seau rapide (fen\xC3\xAAtre %d Ko)", gcmp3_tcp_wnd / 1024);
        else snprintf(buf, sizeof(buf), "R\xC3\xA9seau standard");
        gx_text1(F_SMALL, 280, 370, buf, RGBA(150, 135, 210, 255));
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

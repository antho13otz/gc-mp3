// GC Station v86 : fichiers sur le reseau, comme des disques : smb:/  ftp:/  fsp:/
// (inclus dans main.c, apres les fonctions de connexion). Clients ecrits pour GC Station :
// FTP (TCP, mode passif), FSP (UDP, protocole leger) et SMB2 (smb2.c).
// Les reglages se font dans START > Reglages > Reseau ; ils sont gardes dans gc-mp3.txt
// (v90 : plus de reprise depuis SD:/swiss).
// Sur la console, chaque protocole est un "disque" de newlib (devoptab) : fopen, opendir,
// stat... marchent comme sur la carte SD (musique, photos, videos, copie vers la carte).
// Lecture seulement.

enum { NFS_SMB, NFS_FTP, NFS_FSP, NFS_GCS, NFS_N };   /* v138 : gcs:/ = une autre GC Station (Salon) */
#define NFS_NAME 128
typedef struct { char name[NFS_NAME]; u8 dir; u32 size, mtime; } nfs_ent_t;
static const char *nfs_cistr(const char *h, const char *n) {      /* (strcasestr) */
    size_t l = strlen(n);
    for (; *h; h++) if (!strncasecmp(h, n, l)) return h;
    return NULL;
}
static const char *const nfs_scheme[NFS_N] = { "smb", "ftp", "fsp", "gcs" };
static const char *const nfs_label[NFS_N] = { "Partage SMB", "Serveur FTP", "Serveur FSP", "GC Station" };

/* ---------- reglages ---------- */
typedef struct { char host[64], share[64], user[64], pass[64]; int port, pasv, mtu; } nfs_cfg_t;
static nfs_cfg_t nfs_cfg[NFS_N] = { { "", "", "", "", 445, 0, 0 }, { "", "", "", "", 21, 1, 0 }, { "", "", "", "", 21, 0, 0 }, { "", "", "", "", 80, 0, 0 } };
static char nfs_err[120] = "";
static int nfs_cfg_ok(int p) { return nfs_cfg[p].host[0] && (p != NFS_SMB || nfs_cfg[p].share[0]); }
static void nfs_seterr(const char *m) { snprintf(nfs_err, sizeof(nfs_err), "%s", m); }


/* ---------- utilitaires ---------- */
static const char *nfs_rel(const char *path, int *proto) {        /* "ftp:/a/b" -> "/a/b" */
    for (int p = 0; p < NFS_N; p++) {
        size_t l = strlen(nfs_scheme[p]);
        if (!strncasecmp(path, nfs_scheme[p], l) && path[l] == ':') {
            if (proto) *proto = p;
            const char *r = path + l + 1;
            return *r ? r : "/";
        }
    }
    return NULL;
}
static int nfs_is_net(const char *path) { return nfs_rel(path, NULL) != NULL; }
/* reception TCP avec delai (ms) : > 0 octets, 0 fin, -1 erreur ou delai */
static int nfs_recv(s32 fd, void *buf, int len, int ms) {
    u64 t0 = now_ms();
    for (;;) {
        s32 r = net_recv(fd, buf, len, 0);
        if (r > 0) return r;
        if (r == 0) return 0;
        if (r != -EAGAIN && r != -EWOULDBLOCK) return -1;
        if (now_ms() - t0 > (u64)ms) return -1;
        service_audio();
        usleep(500);
    }
}
static int nfs_send(s32 fd, const void *buf, int len) {
    const u8 *p = (const u8 *)buf;
    u64 t0 = now_ms();
    while (len > 0) {
        s32 r = net_send(fd, p, len, 0);
        if (r > 0) { p += r; len -= r; continue; }
        if ((r != -EAGAIN && r != -EWOULDBLOCK) || now_ms() - t0 > 8000) return 0;
        service_audio();
        usleep(500);
    }
    return 1;
}
static s32 nfs_tcp(const char *host, int port) {                 /* socket non bloquante, ou -1 (nfs_err) */
    conn_t c = { -1, 0, 0 };
    const char *err = NULL;
    if (net_phase != NET_DONE || !net_ok) { nfs_seterr(net_phase != NET_DONE ? "R\xC3\xA9seau en cours de connexion" : "Pas de r\xC3\xA9seau"); return -1; }
    for (int t = 0; t < 3; t++) {                                   /* v119 : 3 essais (serveur occupe, paquet perdu) */
        if (conn_tcp(&c, host, port, &err, NULL, 0)) return c.fd;
        if (t < 2) { u64 t0 = now_ms(); while (now_ms() - t0 < (t ? 700u : 300u)) { service_audio(); usleep(10000); } }
    }
    nfs_seterr(err ? err : "Connexion impossible");
    return -1;
}

/* ======================= FTP ======================= */
typedef struct {
    s32 ctl, data;
    char line[512];
    u8 rbuf[1024]; int rlen;                        /* (reponses du serveur deja recues) */
} ftp_t;
static int ftp_resp(ftp_t *f, char *out, int cap) {               /* code de la reponse (derniere ligne), -1 erreur */
    char code[4] = "";
    int first = 1;
    for (;;) {
        char *nl;
        while (!(nl = memchr(f->rbuf, '\n', f->rlen))) {
            if (f->rlen >= (int)sizeof(f->rbuf) - 1) f->rlen = 0;       /* (ligne trop longue : oubliee) */
            int r = nfs_recv(f->ctl, f->rbuf + f->rlen, (int)sizeof(f->rbuf) - 1 - f->rlen, 6000);
            if (r <= 0) { nfs_seterr("Le serveur FTP ne r\xC3\xA9pond plus"); return -1; }
            f->rlen += r;
        }
        int n = (int)(nl - (char *)f->rbuf) + 1;
        char l[512];
        int m = n < (int)sizeof(l) - 1 ? n : (int)sizeof(l) - 1;
        memcpy(l, f->rbuf, m); l[m] = 0;
        memmove(f->rbuf, f->rbuf + n, f->rlen - n); f->rlen -= n;
        l[strcspn(l, "\r\n")] = 0;
        if (first) { if (strlen(l) < 3) continue; memcpy(code, l, 3); code[3] = 0; first = 0; }
        if (!strncmp(l, code, 3) && l[3] == ' ') {                    /* (fin d'une reponse sur plusieurs lignes : "123 ") */
            if (out) snprintf(out, cap, "%s", l);
            return atoi(code);
        }
        if (l[3] != '-' && strncmp(l, code, 3)) continue;           /* (ligne du milieu) */
        if (l[3] == ' ') { if (out) snprintf(out, cap, "%s", l); return atoi(code); }
    }
}
static int ftp_cmd(ftp_t *f, const char *cmd, char *out, int cap) {
    char b[600];
    int n = snprintf(b, sizeof(b), "%s\r\n", cmd);
    if (n <= 0 || n >= (int)sizeof(b) || !nfs_send(f->ctl, b, n)) { nfs_seterr("Envoi impossible au serveur FTP"); return -1; }
    return ftp_resp(f, out, cap);
}
static void ftp_close(ftp_t *f) {
    if (f->data >= 0) net_close(f->data);
    if (f->ctl >= 0) { nfs_send(f->ctl, "QUIT\r\n", 6); net_close(f->ctl); }
    f->data = f->ctl = -1; f->rlen = 0;
}
static int ftp_login(ftp_t *f);
/* v119 : sessions gardees ouvertes et reutilisees (avant : une connexion + identification a chaque liste,
   chaque fichier cherche, chaque fichier ouvert : les petits serveurs finissaient par refuser) */
#define FTP_POOL 2
static ftp_t ftp_pool[FTP_POOL] = { { -1, -1 }, { -1, -1 } };
static u8 ftp_pool_busy[FTP_POOL];
static u64 ftp_pool_t[FTP_POOL];
static int ftp_reused = 0;                                        /* (la derniere session donnee etait deja ouverte) */
static ftp_t *ftp_get(void) {
    u64 now = now_ms();
    for (int i = 0; i < FTP_POOL; i++) {                          /* une session libre deja identifiee */
        if (ftp_pool_busy[i] || ftp_pool[i].ctl < 0) continue;
        if (now - ftp_pool_t[i] > 50000) { ftp_close(&ftp_pool[i]); continue; }   /* (trop vieille : le serveur l'a peut-etre fermee) */
        ftp_pool_busy[i] = 1; ftp_reused = 1;
        return &ftp_pool[i];
    }
    for (int i = 0; i < FTP_POOL; i++) {
        if (ftp_pool_busy[i]) continue;
        ftp_reused = 0;
        if (!ftp_login(&ftp_pool[i])) return NULL;
        ftp_pool_busy[i] = 1;
        return &ftp_pool[i];
    }
    ftp_t *t = (ftp_t *)malloc(sizeof(ftp_t));                    /* (toutes prises : une de plus, fermee apres) */
    if (!t) { nfs_seterr("M\xC3\xA9moire pleine"); return NULL; }
    ftp_reused = 0;
    if (!ftp_login(t)) { free(t); return NULL; }
    return t;
}
static void ftp_put(ftp_t *f, int ok) {                           /* ok = 0 : la session a eu un probleme : fermee */
    if (!f) return;
    for (int i = 0; i < FTP_POOL; i++) {
        if (f != &ftp_pool[i]) continue;
        if (f->data >= 0) { net_close(f->data); f->data = -1; }
        if (!ok) ftp_close(f);
        ftp_pool_busy[i] = 0; ftp_pool_t[i] = now_ms();
        return;
    }
    ftp_close(f); free(f);
}
static int ftp_login(ftp_t *f) {
    const nfs_cfg_t *c = &nfs_cfg[NFS_FTP];
    f->ctl = f->data = -1; f->rlen = 0;
    f->ctl = nfs_tcp(c->host, c->port > 0 ? c->port : 21);
    if (f->ctl < 0) return 0;
    char l[512], b[200];
    int r = ftp_resp(f, l, sizeof(l));
    if (r != 220) { if (r > 0) nfs_seterr("Le serveur FTP refuse la connexion"); ftp_close(f); return 0; }
    snprintf(b, sizeof(b), "USER %s", c->user[0] ? c->user : "anonymous");
    r = ftp_cmd(f, b, l, sizeof(l));
    if (r == 331) { snprintf(b, sizeof(b), "PASS %s", c->pass[0] ? c->pass : "gcstation@"); r = ftp_cmd(f, b, l, sizeof(l)); }
    if (r != 230) { if (r > 0) nfs_seterr("FTP : utilisateur ou mot de passe refus\xC3\xA9"); ftp_close(f); return 0; }
    ftp_cmd(f, "TYPE I", l, sizeof(l));
    ftp_cmd(f, "OPTS UTF8 ON", l, sizeof(l));
    return 1;
}
/* connexion des donnees (mode passif, EPSV puis PASV) */
static int ftp_pasv(ftp_t *f) {
    char l[512];
    int port = -1;
    u32 ip = 0;
    if (ftp_cmd(f, "EPSV", l, sizeof(l)) == 229) {
        char *p = strstr(l, "(|||");
        if (p) port = atoi(p + 4);
    }
    if (port <= 0) {
        if (ftp_cmd(f, "PASV", l, sizeof(l)) != 227) { nfs_seterr("FTP : mode passif refus\xC3\xA9"); return 0; }
        char *p = strchr(l, '(');
        int a, b, cc, d, e, g;
        if (!p || sscanf(p + 1, "%d,%d,%d,%d,%d,%d", &a, &b, &cc, &d, &e, &g) != 6) { nfs_seterr("FTP : reponse PASV illisible"); return 0; }
        port = e * 256 + g;
        ip = (u32)a << 24 | (u32)b << 16 | (u32)cc << 8 | (u32)d;
    }
    char host[64];
    if (ip && (ip >> 24) != 0 && (ip >> 24) != 127) snprintf(host, sizeof(host), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    else snprintf(host, sizeof(host), "%s", nfs_cfg[NFS_FTP].host);    /* (EPSV, ou adresse privee du serveur : celle qu'on connait) */
    f->data = nfs_tcp(host, port);
    return f->data >= 0;
}
/* chemin pour le serveur : ftp:/a/b -> /a/b (UTF-8 tel quel) */
static int ftp_nomlsd = 0;                                        /* (le serveur ne connait pas MLSD : LIST directement) */
static int ftp_list(const char *rel, nfs_ent_t **out, int *n) {
    ftp_t *f = NULL;
    char l[512], cmd[600];
    *out = NULL; *n = 0;
    int cap = 0, mlsd = !ftp_nomlsd, r = -1;
    for (int tries = 0; tries < 2; tries++) {
        f = ftp_get();
        if (!f) return 0;
        int reused = ftp_reused;
        mlsd = !ftp_nomlsd;
        r = -1;
        if (ftp_pasv(f)) {
            snprintf(cmd, sizeof(cmd), "%s %s", mlsd ? "MLSD" : "LIST", rel);
            r = ftp_cmd(f, cmd, l, sizeof(l));
            if (mlsd && (r == 500 || r == 502 || r == 504)) {             /* (pas de MLSD : LIST) */
                net_close(f->data); f->data = -1; mlsd = 0; ftp_nomlsd = 1;
                r = -1;
                if (ftp_pasv(f)) { snprintf(cmd, sizeof(cmd), "LIST %s", rel); r = ftp_cmd(f, cmd, l, sizeof(l)); }
            }
        }
        if (r == 150 || r == 125) break;
        int lost = r < 0 || r == 421;
        ftp_put(f, !lost);
        f = NULL;
        if (!(lost && reused)) break;                                 /* (session gardee mais fermee par le serveur : on recommence) */
    }
    if (!f) { if (r > 0) nfs_seterr(r == 550 ? "FTP : dossier introuvable" : "FTP : liste refus\xC3\xA9" "e"); return 0; }
    /* toute la liste (texte), puis decoupee en lignes */
    int blen = 0, bcap = 16384;
    char *buf = (char *)malloc(bcap);
    for (;;) {
        if (!buf) break;
        if (blen + 4096 > bcap) { char *nb = (char *)realloc(buf, bcap * 2); if (!nb) break; buf = nb; bcap *= 2; }
        int k = nfs_recv(f->data, buf + blen, bcap - blen - 1, 6000);
        if (k <= 0) break;
        blen += k;
    }
    net_close(f->data); f->data = -1;
    ftp_put(f, ftp_resp(f, l, sizeof(l)) > 0);                       /* 226 */
    if (!buf) { nfs_seterr("M\xC3\xA9moire pleine"); return 0; }
    buf[blen] = 0;
    for (char *ln = strtok(buf, "\r\n"); ln; ln = strtok(NULL, "\r\n")) {
        nfs_ent_t e; memset(&e, 0, sizeof(e));
        if (mlsd) {                                                   /* "type=dir;size=12;modify=...; nom" */
            char *sp = strchr(ln, ' ');
            if (!sp) continue;
            *sp = 0;
            const char *name = sp + 1;
            if (nfs_cistr(ln, "type=cdir") || nfs_cistr(ln, "type=pdir")) continue;
            e.dir = nfs_cistr(ln, "type=dir") != NULL;
            const char *sz = nfs_cistr(ln, "size=");
            if (sz) e.size = (u32)strtoul(sz + 5, NULL, 10);
            snprintf(e.name, sizeof(e.name), "%s", name);
        } else if (ln[0] == 'd' || ln[0] == '-' || ln[0] == 'l') {    /* Unix : "drwxr-xr-x 1 u g 4096 Jan 1 12:00 nom" */
            int fld = 0; char *p = ln, *name = NULL;
            unsigned long size = 0;
            while (*p && fld < 8) {
                while (*p == ' ') p++;
                char *s = p;
                while (*p && *p != ' ') p++;
                if (fld == 4) size = strtoul(s, NULL, 10);
                fld++;
            }
            while (*p == ' ') p++;
            name = p;
            if (fld < 8 || !*name) continue;
            if (ln[0] == 'l') { char *ar = strstr(name, " -> "); if (ar) *ar = 0; }
            e.dir = ln[0] == 'd';
            e.size = (u32)size;
            snprintf(e.name, sizeof(e.name), "%s", name);
        } else if (isdigit((unsigned char)ln[0])) {                   /* Windows (IIS) : "01-02-24 10:00AM <DIR> nom" */
            char a[32], b[32], c[64];
            int pos = 0;
            if (sscanf(ln, "%31s %31s %63s %n", a, b, c, &pos) < 3 || !pos) continue;
            e.dir = !strcmp(c, "<DIR>");
            if (!e.dir) e.size = (u32)strtoul(c, NULL, 10);
            snprintf(e.name, sizeof(e.name), "%s", ln + pos);
        } else continue;
        if (!e.name[0] || !strcmp(e.name, ".") || !strcmp(e.name, "..")) continue;
        if (*n >= cap) { int nc = cap ? cap * 2 : 64; nfs_ent_t *ne = (nfs_ent_t *)realloc(*out, sizeof(nfs_ent_t) * nc); if (!ne) break; *out = ne; cap = nc; }
        (*out)[(*n)++] = e;
    }
    free(buf);
    return 1;
}
static int ftp_stat(const char *rel, int *dir, u32 *size) {
    char l[512], cmd[600];
    for (int tries = 0; tries < 2; tries++) {
        ftp_t *f = ftp_get();
        if (!f) return 0;
        int reused = ftp_reused;
        snprintf(cmd, sizeof(cmd), "SIZE %s", rel);
        int r = ftp_cmd(f, cmd, l, sizeof(l));
        if (r == 213) { *dir = 0; *size = (u32)strtoul(l + 4, NULL, 10); ftp_put(f, 1); return 1; }
        if (r > 0 && r != 421) {
            snprintf(cmd, sizeof(cmd), "CWD %s", rel);
            r = ftp_cmd(f, cmd, l, sizeof(l));
            if (r == 250) { *dir = 1; *size = 0; ftp_cmd(f, "CWD /", l, sizeof(l)); ftp_put(f, 1); return 1; }
            if (r > 0 && r != 421) { nfs_seterr("FTP : fichier introuvable"); ftp_put(f, 1); return 0; }
        }
        ftp_put(f, 0);
        if (!reused) return 0;
    }
    return 0;
}
/* fichier ouvert : une session par fichier, connexion des donnees gardee (lecture a la suite) */
typedef struct { ftp_t *s; char rel[NAME_LEN]; u32 size, pos, dpos; int live; } ftp_file_t;
static int ftp_fopen(ftp_file_t *h, const char *rel, u32 known_size, int known) {
    memset(h, 0, sizeof(*h));
    snprintf(h->rel, sizeof(h->rel), "%s", rel);
    char l[512], cmd[600];
    for (int tries = 0; tries < 2; tries++) {
        h->s = ftp_get();
        if (!h->s) return 0;
        if (known) { h->size = known_size; return 1; }             /* v119 : taille deja connue (liste du dossier) : pas de SIZE */
        int reused = ftp_reused;
        snprintf(cmd, sizeof(cmd), "SIZE %s", rel);
        int r = ftp_cmd(h->s, cmd, l, sizeof(l));
        if (r == 213) { h->size = (u32)strtoul(l + 4, NULL, 10); return 1; }
        ftp_put(h->s, r > 0 && r != 421);
        h->s = NULL;
        if (r > 0 && r != 421) { nfs_seterr("FTP : fichier introuvable"); return 0; }
        if (!reused) return 0;
    }
    return 0;
}
static void ftp_data_stop(ftp_file_t *h) {                         /* fin d'un transfert commence (saut ailleurs) */
    if (!h->live || !h->s) return;
    h->live = 0;
    net_close(h->s->data); h->s->data = -1;
    char l[512];
    /* une seule reponse pour ce transfert : 226 (fini) ou 426 / 451 (interrompu : connexion fermee).
       (pas d'ABOR : si le transfert etait deja fini, certains serveurs repondent deux fois) */
    if (ftp_resp(h->s, l, sizeof(l)) < 0) ftp_close(h->s);
}
static int ftp_fread(ftp_file_t *h, u8 *dst, int len) {
    if (h->pos >= h->size) return 0;
    if (len > (int)(h->size - h->pos)) len = (int)(h->size - h->pos);
    if (h->live && h->dpos != h->pos) ftp_data_stop(h);
    if (!h->live) {
        char l[512], cmd[600];
        for (int tries = 0; tries < 2; tries++) {
            if (!h->s) { h->s = ftp_get(); if (!h->s) return -1; }
            else if (h->s->ctl < 0 && !ftp_login(h->s)) return -1;
            int ok = ftp_pasv(h->s);
            if (ok) {
                snprintf(cmd, sizeof(cmd), "REST %u", (unsigned)h->pos);
                int r = h->pos ? ftp_cmd(h->s, cmd, l, sizeof(l)) : 350;
                snprintf(cmd, sizeof(cmd), "RETR %s", h->rel);
                if (r == 350 && ((r = ftp_cmd(h->s, cmd, l, sizeof(l))) == 150 || r == 125)) { h->live = 1; h->dpos = h->pos; break; }
                ok = 0;
            }
            ftp_close(h->s);                                         /* (connexion perdue ? on reessaie une fois) */
        }
        if (!h->live) { nfs_seterr("FTP : lecture refus\xC3\xA9" "e"); return -1; }
    }
    int got = 0;
    while (got < len) {
        int r = nfs_recv(h->s->data, dst + got, len - got, 6000);
        if (r <= 0) break;
        got += r;
    }
    h->pos += got; h->dpos += got;
    if (h->dpos >= h->size) ftp_data_stop(h);
    return got;
}
static void ftp_fclose(ftp_file_t *h) {
    if (!h->s) return;
    int ok = h->s->ctl >= 0;
    if (h->live) {                                                    /* (transfert pas fini : on l'arrete, la session reste) */
        ftp_data_stop(h);
        ok = h->s->ctl >= 0;
    }
    ftp_put(h->s, ok);
    h->s = NULL;
}

/* ======================= FSP (UDP) =======================
   Paquet : commande, somme, cle, numero, longueur, position (12 octets), donnees, extra.
   Le serveur donne une cle a reprendre dans la demande suivante ; on renvoie si pas de reponse. */
#define FSP_HDR 12
#define FSP_SPACE 1024
enum { FSP_CC_VERSION = 0x10, FSP_CC_ERR = 0x40, FSP_CC_GET_DIR = 0x41, FSP_CC_GET_FILE = 0x42, FSP_CC_BYE = 0x4A, FSP_CC_STAT = 0x4D };
typedef struct { s32 fd; u16 key, seq; struct sockaddr_in sa; } fsp_t;
static fsp_t fsp_s = { -1, 0, 0 };
static int fsp_open(void) {
    if (fsp_s.fd >= 0) return 1;
    const nfs_cfg_t *c = &nfs_cfg[NFS_FSP];
    if (net_phase != NET_DONE || !net_ok) { nfs_seterr("Pas de r\xC3\xA9seau"); return 0; }
    u32 addr;
    if (!resolve_host(c->host, &addr)) { nfs_seterr("FSP : serveur introuvable"); return 0; }
    s32 s = net_socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { nfs_seterr("Erreur r\xC3\xA9seau (socket)"); return 0; }
    set_nonblock(s, 1);
    memset(&fsp_s.sa, 0, sizeof(fsp_s.sa));
    fsp_s.sa.sin_family = AF_INET;
    fsp_s.sa.sin_port = htons((u16)(c->port > 0 ? c->port : 21));
    fsp_s.sa.sin_addr.s_addr = addr;
    fsp_s.fd = s; fsp_s.key = 0; fsp_s.seq = (u16)(now_ms() & 0xFFF8);
    return 1;
}
static void fsp_close_s(void) { if (fsp_s.fd >= 0) net_close(fsp_s.fd); fsp_s.fd = -1; }
/* une demande, une reponse (meme numero) ; rend la longueur des donnees recues, -1 erreur */
static int fsp_req(u8 cmd, u32 pos, const u8 *data, int dlen, const u8 *xtra, int xlen, u8 *rbuf, u32 *rpos) {
    if (!fsp_open()) return -1;
    static u8 pk[FSP_HDR + FSP_SPACE], rp[FSP_HDR + FSP_SPACE + 64];
    if (dlen + xlen > FSP_SPACE) { nfs_seterr("FSP : chemin trop long"); return -1; }
    int to = 400;
    for (int tries = 0; tries < 7; tries++) {
        u16 seq = (u16)((fsp_s.seq & 0xFFF8) | (tries & 7));
        pk[0] = cmd; pk[1] = 0;
        pk[2] = (u8)(fsp_s.key >> 8); pk[3] = (u8)fsp_s.key;
        pk[4] = (u8)(seq >> 8); pk[5] = (u8)seq;
        pk[6] = (u8)(dlen >> 8); pk[7] = (u8)dlen;
        pk[8] = (u8)(pos >> 24); pk[9] = (u8)(pos >> 16); pk[10] = (u8)(pos >> 8); pk[11] = (u8)pos;
        memcpy(pk + FSP_HDR, data, dlen);
        if (xlen) memcpy(pk + FSP_HDR + dlen, xtra, xlen);
        int used = FSP_HDR + dlen + xlen;
        u32 sum = 0;
        for (int i = 0; i < used; i++) sum += pk[i];
        sum += (u32)used;
        pk[1] = (u8)(sum + (sum >> 8));
        net_sendto(fsp_s.fd, pk, used, 0, (struct sockaddr *)&fsp_s.sa, sizeof(fsp_s.sa));
        u64 t0 = now_ms();
        while (now_ms() - t0 < (u64)to) {
            s32 r = net_recv(fsp_s.fd, rp, sizeof(rp), 0);
            if (r == -EAGAIN || r == -EWOULDBLOCK || r == 0) { service_audio(); usleep(300); continue; }
            if (r < FSP_HDR) break;
            u32 s2 = 0;
            for (int i = 0; i < r; i++) s2 += rp[i];
            s2 -= rp[1];
            if ((u8)((s2 + (s2 >> 8)) & 0xFF) != rp[1]) continue;    /* (somme fausse : ignore) */
            u16 rseq = (u16)(rp[4] << 8 | rp[5]);
            if ((rseq & 0xFFF8) != (fsp_s.seq & 0xFFF8)) continue;    /* (reponse a une ancienne demande) */
            fsp_s.key = (u16)(rp[2] << 8 | rp[3]);
            int rl = rp[6] << 8 | rp[7];
            if (rl > r - FSP_HDR) rl = r - FSP_HDR;
            if (rpos) *rpos = (u32)rp[8] << 24 | (u32)rp[9] << 16 | (u32)rp[10] << 8 | rp[11];
            fsp_s.seq += 8;
            if (rp[0] == FSP_CC_ERR) {
                char m[100];
                int ml = rl < 80 ? rl : 80;
                memcpy(m, rp + FSP_HDR, ml); m[ml] = 0;
                snprintf(nfs_err, sizeof(nfs_err), "FSP : %s", m[0] ? m : "erreur du serveur");
                return -1;
            }
            if (rp[0] != cmd) continue;
            memcpy(rbuf, rp + FSP_HDR, rl);
            return rl;
        }
        to = to * 2 > 3000 ? 3000 : to * 2;
    }
    fsp_s.seq += 8;
    nfs_seterr("Le serveur FSP ne r\xC3\xA9pond pas");
    fsp_close_s();
    return -1;
}
static int fsp_name(const char *rel, u8 *out) {                    /* chemin (+ "\nmot de passe"), 0 compris */
    const char *p = rel;
    while (*p == '/') p++;
    int n = snprintf((char *)out, FSP_SPACE - 2, "%s", *p ? p : ".");
    if (nfs_cfg[NFS_FSP].pass[0]) n += snprintf((char *)out + n, FSP_SPACE - 2 - n, "\n%s", nfs_cfg[NFS_FSP].pass);
    return n + 1;
}
static int fsp_list(const char *rel, nfs_ent_t **out, int *n) {
    u8 name[FSP_SPACE], blk[FSP_SPACE + 64];
    int nl = fsp_name(rel, name), cap = 0;
    u32 pos = 0, bs = 0;
    *out = NULL; *n = 0;
    for (int guard = 0; guard < 4096; guard++) {
        u32 rpos = 0;
        int r = fsp_req(FSP_CC_GET_DIR, pos, name, nl, NULL, 0, blk, &rpos);
        if (r < 0) { free(*out); *out = NULL; *n = 0; return 0; }
        if (r == 0) break;
        if (!bs) bs = (u32)r;
        int i = 0, end = 0;
        while (i + 9 <= r) {
            u8 type = blk[i + 8];
            if (type == 0x00) { end = 1; break; }                    /* fin de la liste */
            if (type == 0x2A) break;                                  /* suite dans le bloc suivant */
            u32 size = (u32)blk[i + 4] << 24 | (u32)blk[i + 5] << 16 | (u32)blk[i + 6] << 8 | blk[i + 7];
            const char *nm = (const char *)blk + i + 9;
            int ln = (int)strnlen(nm, r - i - 9);
            if (type == 0x01 || type == 0x02) {
                if (*n >= cap) { int nc = cap ? cap * 2 : 64; nfs_ent_t *ne = (nfs_ent_t *)realloc(*out, sizeof(nfs_ent_t) * nc); if (!ne) return 1; *out = ne; cap = nc; }
                nfs_ent_t *e = &(*out)[*n];
                memset(e, 0, sizeof(*e));
                snprintf(e->name, sizeof(e->name), "%.*s", ln, nm);
                e->dir = type == 0x02; e->size = size;
                e->mtime = (u32)blk[i] << 24 | (u32)blk[i + 1] << 16 | (u32)blk[i + 2] << 8 | blk[i + 3];
                if (strcmp(e->name, ".") && strcmp(e->name, "..")) (*n)++;
            }
            i += 9 + ln + 1;
            i = (i + 3) & ~3;                                         /* (entrees alignees sur 4 octets) */
        }
        if (end) break;
        pos += bs;
        service_audio();
    }
    return 1;
}
static int fsp_stat(const char *rel, int *dir, u32 *size) {
    u8 name[FSP_SPACE], b[FSP_SPACE + 64];
    int nl = fsp_name(rel, name);
    int r = fsp_req(FSP_CC_STAT, 0, name, nl, NULL, 0, b, NULL);
    if (r < 9 || b[8] == 0) { if (r >= 0) nfs_seterr("FSP : fichier introuvable"); return 0; }
    *size = (u32)b[4] << 24 | (u32)b[5] << 16 | (u32)b[6] << 8 | b[7];
    *dir = b[8] == 0x02;
    return 1;
}
typedef struct { char rel[NAME_LEN]; u32 size, pos; u8 cache[FSP_SPACE]; u32 cpos; int clen; } fsp_file_t;
static int fsp_fopen(fsp_file_t *h, const char *rel) {
    memset(h, 0, sizeof(*h));
    snprintf(h->rel, sizeof(h->rel), "%s", rel);
    int dir = 0;
    if (!fsp_stat(rel, &dir, &h->size) || dir) { if (dir) nfs_seterr("C'est un dossier"); return 0; }
    return 1;
}
static int fsp_fread(fsp_file_t *h, u8 *dst, int len) {
    u8 name[FSP_SPACE];
    int nl = fsp_name(h->rel, name), got = 0;
    while (got < len && h->pos < h->size) {
        if (h->clen > 0 && h->pos >= h->cpos && h->pos < h->cpos + (u32)h->clen) {   /* (dans le dernier bloc recu) */
            int k = (int)(h->cpos + h->clen - h->pos);
            if (k > len - got) k = len - got;
            memcpy(dst + got, h->cache + (h->pos - h->cpos), k);
            got += k; h->pos += k;
            continue;
        }
        u8 xt[2] = { (u8)(FSP_SPACE >> 8), (u8)FSP_SPACE };
        int r = fsp_req(FSP_CC_GET_FILE, h->pos, name, nl, xt, 2, h->cache, NULL);
        if (r <= 0) { h->clen = 0; return got ? got : (r < 0 ? -1 : 0); }
        h->cpos = h->pos; h->clen = r;
    }
    return got;
}

/* ======================= SMB2 ======================= */
#include "smb2.c"

/* ---------- v119 : listes des 8 derniers dossiers (60 s) : stat() et fopen() d'un fichier repondent tout de suite
   (avant : seulement le dernier dossier ; chercher une pochette ou des paroles ouvrait une connexion par essai) ---------- */
#define NFS_CDIRS 8
typedef struct { char dir[NAME_LEN]; nfs_ent_t *e; int n; u64 t; } nfs_cdir_t;
static nfs_cdir_t nfs_cd[NFS_CDIRS];
static void nfs_cdir_norm(char *d) { size_t l = strlen(d); while (l > 1 && d[l - 1] == '/') d[--l] = 0; }
static void nfs_cache_put(const char *dir, const nfs_ent_t *e, int n) {
    char d[NAME_LEN];
    snprintf(d, sizeof(d), "%s", dir);
    nfs_cdir_norm(d);
    int k = -1;
    for (int i = 0; i < NFS_CDIRS && k < 0; i++) if (nfs_cd[i].dir[0] && !strcmp(nfs_cd[i].dir, d)) k = i;
    if (k < 0) {                                                   /* place libre, sinon la plus ancienne */
        k = 0;
        for (int i = 0; i < NFS_CDIRS; i++) {
            if (!nfs_cd[i].dir[0]) { k = i; break; }
            if (nfs_cd[i].t < nfs_cd[k].t) k = i;
        }
    }
    free(nfs_cd[k].e);
    memset(&nfs_cd[k], 0, sizeof(nfs_cd[k]));
    if (n < 0) return;
    nfs_cd[k].e = n > 0 ? (nfs_ent_t *)malloc(sizeof(nfs_ent_t) * n) : NULL;
    if (n > 0 && !nfs_cd[k].e) return;
    if (n > 0) memcpy(nfs_cd[k].e, e, sizeof(nfs_ent_t) * n);
    nfs_cd[k].n = n;
    nfs_cd[k].t = now_ms();
    snprintf(nfs_cd[k].dir, sizeof(nfs_cd[k].dir), "%s", d);
}
/* 1 = trouve dans un dossier deja lu, 0 = absent de ce dossier, -1 = dossier pas en memoire */
static int nfs_cache_find(const char *path, int *dir, u32 *size) {
    const char *sl = strrchr(path, '/');
    if (!sl) return -1;
    char d[NAME_LEN];
    int dl = (int)(sl - path);
    if (dl >= (int)sizeof(d)) return -1;
    memcpy(d, path, dl); d[dl] = 0;
    if (dl > 0 && d[dl - 1] == ':') { d[dl] = '/'; d[dl + 1] = 0; }   /* ("ftp:/nom" : racine "ftp:/") */
    nfs_cdir_norm(d);
    u64 now = now_ms();
    for (int i = 0; i < NFS_CDIRS; i++) {
        nfs_cdir_t *c = &nfs_cd[i];
        if (!c->dir[0] || strcmp(c->dir, d)) continue;
        if (now - c->t > 60000) return -1;                         /* (trop vieux : le dossier a pu changer) */
        for (int k = 0; k < c->n; k++)
            if (!strcmp(c->e[k].name, sl + 1)) { if (dir) *dir = c->e[k].dir; if (size) *size = c->e[k].size; return 1; }
        return 0;
    }
    return -1;
}


/* ======================= v138 : GC Station (Salon) =======================
   L'autre console sert sa carte SD par son serveur du telephone : /s/list, /s/stat, /s/get (morceaux, en-tete
   Range), en HTTP tout simple (une connexion par demande). Chemins relatifs a sa carte SD. */
typedef struct { char rel[NAME_LEN]; u32 size, pos; } gcs_file_t;
static void gcs_pct(char *o, int cap, const char *t) {           /* chemin -> morceau d'adresse (%XX) */
    static const char hx[] = "0123456789ABCDEF";
    int n = 0;
    for (const unsigned char *q = (const unsigned char *)t; *q && n < cap - 4; q++) {
        if (isalnum(*q) || *q == '/' || *q == '.' || *q == '-' || *q == '_') o[n++] = (char)*q;
        else { o[n++] = '%'; o[n++] = hx[*q >> 4]; o[n++] = hx[*q & 15]; }
    }
    o[n] = 0;
}
/* demande HTTP ; corps dans out (cap octets au plus) ; rend la taille lue, -1 si erreur ; *total : taille entiere
   (Content-Range, sinon Content-Length) */
static int gcs_http(const char *what, const char *rel, u32 from, u32 len, u8 *out, int cap, u32 *total) {
    char q[NAME_LEN * 3 + 16], req[NAME_LEN * 3 + 256];
    gcs_pct(q, sizeof(q), rel);
    s32 fd = nfs_tcp(nfs_cfg[NFS_GCS].host, nfs_cfg[NFS_GCS].port);
    if (fd < 0) return -1;
    int rl = len ? snprintf(req, sizeof(req), "GET /s/%s?p=%s HTTP/1.0\r\nHost: %s\r\nRange: bytes=%u-%u\r\n\r\n", what, q, nfs_cfg[NFS_GCS].host, (unsigned)from, (unsigned)(from + len - 1))
                 : snprintf(req, sizeof(req), "GET /s/%s?p=%s HTTP/1.0\r\nHost: %s\r\n\r\n", what, q, nfs_cfg[NFS_GCS].host);
    if (!nfs_send(fd, req, rl)) { net_close(fd); nfs_seterr("Envoi impossible"); return -1; }
    static char h[2048];
    int hl = 0, body = -1;
    while (hl < (int)sizeof(h) - 1) {                    /* en-tetes (le debut du corps peut suivre) */
        int r = nfs_recv(fd, h + hl, (int)sizeof(h) - 1 - hl, 8000);
        if (r <= 0) break;
        hl += r; h[hl] = 0;
        char *e = strstr(h, "\r\n\r\n");
        if (e) { body = (int)(e - h) + 4; break; }
    }
    int code = 0;
    if (body < 0 || sscanf(h, "HTTP/%*d.%*d %d", &code) != 1 || (code != 200 && code != 206)) {
        net_close(fd);
        nfs_seterr(code == 404 ? "Introuvable sur l'autre console" : code == 403 ? "L'autre console n'a pas le Salon actif" : "L'autre console ne r\xC3\xA9pond pas");
        return -1;
    }
    if (total) {
        const char *cr = nfs_cistr(h, "\ncontent-range:"), *cl = nfs_cistr(h, "\ncontent-length:");
        const char *sl = cr ? strchr(cr, '/') : NULL;
        *total = sl && sl < h + body ? (u32)strtoul(sl + 1, NULL, 10) : cl ? (u32)strtoul(cl + 16, NULL, 10) : 0;
    }
    int got = hl - body;
    if (got > cap) got = cap;
    if (got > 0) memcpy(out, h + body, got);
    while (got < cap) {
        int r = nfs_recv(fd, out + got, cap - got, 8000);
        if (r <= 0) break;
        got += r;
    }
    net_close(fd);
    return got;
}
static int gcs_list(const char *rel, nfs_ent_t **out, int *n) {
    char *t = (char *)malloc(256 * 1024);
    if (!t) return 0;
    int len = gcs_http("list", rel, 0, 0, (u8 *)t, 256 * 1024 - 1, NULL);
    if (len < 0) { free(t); return 0; }
    t[len] = 0;
    int cap = 64, k = 0;
    nfs_ent_t *e = (nfs_ent_t *)malloc(sizeof(nfs_ent_t) * cap);
    for (char *l = strtok(t, "\n"); l && e; l = strtok(NULL, "\n")) {   /* "d\ttaille\tnom" */
        char *a = strchr(l, '\t'), *b = a ? strchr(a + 1, '\t') : NULL;
        if (!a || !b || !b[1]) continue;
        if (k == cap) { nfs_ent_t *ne = (nfs_ent_t *)realloc(e, sizeof(nfs_ent_t) * cap * 2); if (!ne) break; e = ne; cap *= 2; }
        memset(&e[k], 0, sizeof(e[k]));
        e[k].dir = l[0] == 'd';
        e[k].size = (u32)strtoul(a + 1, NULL, 10);
        snprintf(e[k].name, sizeof(e[k].name), "%s", b + 1);
        k++;
    }
    free(t);
    *out = e; *n = k;
    return e != NULL;
}
static int gcs_stat(const char *rel, int *dir, u32 *size) {
    char t[64];
    int len = gcs_http("stat", rel, 0, 0, (u8 *)t, sizeof(t) - 1, NULL);
    if (len <= 0) return 0;
    t[len] = 0;
    if (dir) *dir = t[0] == 'd';
    if (size) *size = (u32)strtoul(t + 2, NULL, 10);
    return 1;
}
static int gcs_fopen(gcs_file_t *h, const char *rel) {
    int d = 0; u32 sz = 0;
    if (!gcs_stat(rel, &d, &sz) || d) return 0;
    snprintf(h->rel, sizeof(h->rel), "%s", rel);
    h->size = sz; h->pos = 0;
    return 1;
}
static int gcs_fread(gcs_file_t *h, u8 *dst, int len) {
    if (h->pos >= h->size) return 0;
    if ((u32)len > h->size - h->pos) len = (int)(h->size - h->pos);
    u32 tot = 0;
    int got = gcs_http("get", h->rel, h->pos, (u32)len, dst, len, &tot);
    if (got <= 0) return got;
    h->pos += got;
    return got;
}

/* v138 : le Salon (salon.c) : consoles trouvees sur le reseau */
#define SALON_MAX 8
typedef struct { char name[40]; char ip[16]; char now[80]; u64 seen; int port; } salon_peer_t;   /* (v143 : port HTTP annonce : 80, ou 8080 pour tools/pulse-serveur.py) */
static salon_peer_t salon_peers[SALON_MAX];
static int salon_n = 0;
static int salon_gen = 0;                              /* (change quand une console arrive ou part : liste des appareils a refaire) */
static int salon_on = 0;                                  /* (reglage : salon = oui) */
static char salon_name[40] = "";                          /* (reglage : salon_nom) */
static const char *salon_listen(int k);
static int salon_route(s32 c, const char *target, const char *q, char *req);

/* ======================= commun ======================= */
static int nfs_list(const char *path, nfs_ent_t **out, int *n) {
    int p = -1;
    const char *rel = nfs_rel(path, &p);
    *out = NULL; *n = 0;
    if (!rel) return 0;
    if (!nfs_cfg_ok(p)) { nfs_seterr("Pas r\xC3\xA9gl\xC3\xA9 : START > R\xC3\xA9glages > R\xC3\xA9seau"); return 0; }
    int ok = p == NFS_FTP ? ftp_list(rel, out, n) : p == NFS_FSP ? fsp_list(rel, out, n) : p == NFS_GCS ? gcs_list(rel, out, n) : smb_list(rel, out, n);
    if (ok) nfs_cache_put(path, *out, *n);
    return ok;
}
static int nfs_stat(const char *path, int *dir, u32 *size) {
    int p = -1;
    const char *rel = nfs_rel(path, &p);
    if (!rel || !nfs_cfg_ok(p)) return 0;
    if (!strcmp(rel, "/")) { *dir = 1; *size = 0; return 1; }
    int c = nfs_cache_find(path, dir, size);
    if (c >= 0) return c;
    if (p == NFS_FTP) return ftp_stat(rel, dir, size);
    if (p == NFS_FSP) return fsp_stat(rel, dir, size);
    if (p == NFS_GCS) return gcs_stat(rel, dir, size);
    return smb_stat(rel, dir, size);
}
/* v119 : FTP et FSP : le fichier est lu par morceaux de 32 Ko gardes en memoire (4 par fichier). Lire les
   etiquettes au debut, puis la fin, puis revenir au debut ne redemande plus le fichier au serveur. */
#define NFS_BLK  32768
#define NFS_NBLK 4
typedef struct {
    int proto;
    union { ftp_file_t ftp; fsp_file_t fsp; smb_file_t smb; gcs_file_t gcs; } u;
    u32 lpos;                                   /* position lue par le programme (FTP / FSP) */
    u8 *blk[NFS_NBLK]; u32 bpos[NFS_NBLK]; int blen[NFS_NBLK]; u32 bused[NFS_NBLK], bclock;
} nfile_t;
static nfile_t *nfs_fopen(const char *path) {
    int p = -1;
    const char *rel = nfs_rel(path, &p);
    if (!rel || !nfs_cfg_ok(p)) return NULL;
    if (nfs_cache_find(path, NULL, NULL) == 0) { nfs_seterr("Fichier introuvable"); return NULL; }   /* (absent du dossier deja lu) */
    nfile_t *f = (nfile_t *)malloc(sizeof(nfile_t));
    if (!f) return NULL;
    memset(f, 0, sizeof(*f));
    f->proto = p;
    int kd = 0; u32 ksz = 0;
    int known = p == NFS_FTP && nfs_cache_find(path, &kd, &ksz) == 1 && !kd;   /* (taille deja lue dans la liste) */
    int ok = p == NFS_FTP ? ftp_fopen(&f->u.ftp, rel, ksz, known) : p == NFS_FSP ? fsp_fopen(&f->u.fsp, rel) : p == NFS_GCS ? gcs_fopen(&f->u.gcs, rel) : smb_fopen(&f->u.smb, rel);
    if (!ok) { free(f); return NULL; }
    return f;
}
static u32 nfs_fsize(nfile_t *f) { return f->proto == NFS_FTP ? f->u.ftp.size : f->proto == NFS_FSP ? f->u.fsp.size : f->proto == NFS_GCS ? f->u.gcs.size : f->u.smb.size; }
static u32 *nfs_fpos(nfile_t *f) { return f->proto == NFS_SMB ? &f->u.smb.pos : &f->lpos; }
/* lit un morceau entier a la position bp (FTP : a la suite du transfert en cours si c'est la suite) */
static int nfs_blk_fill(nfile_t *f, u32 bp, u8 *dst) {
    u32 size = nfs_fsize(f);
    int want = size - bp < NFS_BLK ? (int)(size - bp) : NFS_BLK;
    int got = 0;
    if (f->proto == NFS_FTP) f->u.ftp.pos = bp; else if (f->proto == NFS_GCS) f->u.gcs.pos = bp; else f->u.fsp.pos = bp;
    while (got < want) {
        int k = f->proto == NFS_FTP ? ftp_fread(&f->u.ftp, dst + got, want - got) : f->proto == NFS_GCS ? gcs_fread(&f->u.gcs, dst + got, want - got) : fsp_fread(&f->u.fsp, dst + got, want - got);
        if (k <= 0) return got ? got : k;
        got += k;
    }
    return got;
}
static int nfs_fread(nfile_t *f, void *dst, int len) {
    if (f->proto == NFS_SMB) return smb_fread(&f->u.smb, (u8 *)dst, len);
    u8 *o = (u8 *)dst;
    u32 size = nfs_fsize(f);
    int got = 0;
    while (got < len && f->lpos < size) {
        u32 bp = f->lpos & ~(u32)(NFS_BLK - 1);
        int k = -1;
        for (int i = 0; i < NFS_NBLK; i++) if (f->blk[i] && f->blen[i] > 0 && f->bpos[i] == bp) { k = i; break; }
        if (k < 0) {                                                  /* pas en memoire : le morceau entier */
            k = 0;
            for (int i = 0; i < NFS_NBLK; i++) { if (!f->blk[i]) { k = i; break; } if (f->bused[i] < f->bused[k]) k = i; }
            if (!f->blk[k]) { f->blk[k] = (u8 *)malloc(NFS_BLK); if (!f->blk[k]) return got ? got : -1; }
            f->blen[k] = 0;
            int n = nfs_blk_fill(f, bp, f->blk[k]);
            if (n <= 0) return got ? got : n;
            f->bpos[k] = bp; f->blen[k] = n;
        }
        f->bused[k] = ++f->bclock;
        u32 off = f->lpos - bp;
        if (off >= (u32)f->blen[k]) break;                             /* (fin du fichier plus tot que prevu) */
        int c = f->blen[k] - (int)off;
        if (c > len - got) c = len - got;
        memcpy(o + got, f->blk[k] + off, c);
        got += c; f->lpos += c;
    }
    return got;
}
static long nfs_fseek(nfile_t *f, long off, int whence) {
    long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)*nfs_fpos(f) : (long)nfs_fsize(f);
    long np = base + off;
    if (np < 0) return -1;
    *nfs_fpos(f) = (u32)np;
    return np;
}
static void nfs_fclose(nfile_t *f) {
    if (!f) return;
    for (int i = 0; i < NFS_NBLK; i++) free(f->blk[i]);
    if (f->proto == NFS_FTP) ftp_fclose(&f->u.ftp);
    else if (f->proto == NFS_SMB) smb_fclose(&f->u.smb);
    free(f);
}

#ifdef GEKKO
/* ---------- disques de newlib (devoptab) : smb: ftp: fsp: ---------- */
#include <sys/iosupport.h>
#include <fcntl.h>
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
typedef struct { nfile_t *f; } nfs_fd_t;
typedef struct { nfs_ent_t *e; int n, i; } nfs_dir_t;
static int nfs_open_r(struct _reent *r, void *fs, const char *path, int flags, int mode) {
    (void)mode;
    if ((flags & 3) != O_RDONLY) { r->_errno = EROFS; return -1; }
    nfile_t *f = nfs_fopen(path);
    if (!f) { r->_errno = ENOENT; return -1; }
    ((nfs_fd_t *)fs)->f = f;
    return 0;
}
static int nfs_close_r(struct _reent *r, void *fd) { (void)r; nfs_fclose(((nfs_fd_t *)fd)->f); return 0; }
static ssize_t nfs_read_r(struct _reent *r, void *fd, char *ptr, size_t len) {
    int k = nfs_fread(((nfs_fd_t *)fd)->f, ptr, (int)len);
    if (k < 0) { r->_errno = EIO; return -1; }
    return k;
}
static off_t nfs_seek_r(struct _reent *r, void *fd, off_t pos, int dir) {
    long k = nfs_fseek(((nfs_fd_t *)fd)->f, (long)pos, dir);
    if (k < 0) { r->_errno = EINVAL; return -1; }
    return (off_t)k;
}
static void nfs_fill_stat(struct stat *st, int dir, u32 size) {
    memset(st, 0, sizeof(*st));
    st->st_mode = dir ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    st->st_size = size;
    st->st_nlink = 1;
}
static int nfs_fstat_r(struct _reent *r, void *fd, struct stat *st) { (void)r; nfs_fill_stat(st, 0, nfs_fsize(((nfs_fd_t *)fd)->f)); return 0; }
static int nfs_stat_r(struct _reent *r, const char *path, struct stat *st) {
    int dir = 0; u32 size = 0;
    if (!nfs_stat(path, &dir, &size)) { r->_errno = ENOENT; return -1; }
    nfs_fill_stat(st, dir, size);
    return 0;
}
static DIR_ITER *nfs_diropen_r(struct _reent *r, DIR_ITER *ds, const char *path) {
    nfs_dir_t *d = (nfs_dir_t *)ds->dirStruct;
    d->e = NULL; d->n = d->i = 0;
    if (!nfs_list(path, &d->e, &d->n)) { r->_errno = ENOENT; return NULL; }
    return ds;
}
static int nfs_dirreset_r(struct _reent *r, DIR_ITER *ds) { (void)r; ((nfs_dir_t *)ds->dirStruct)->i = 0; return 0; }
static int nfs_dirnext_r(struct _reent *r, DIR_ITER *ds, char *name, struct stat *st) {
    nfs_dir_t *d = (nfs_dir_t *)ds->dirStruct;
    if (d->i >= d->n) { r->_errno = ENOENT; return -1; }
    nfs_ent_t *e = &d->e[d->i++];
    snprintf(name, NAME_MAX + 1, "%s", e->name);
    if (st) nfs_fill_stat(st, e->dir, e->size);
    return 0;
}
static int nfs_dirclose_r(struct _reent *r, DIR_ITER *ds) { (void)r; nfs_dir_t *d = (nfs_dir_t *)ds->dirStruct; free(d->e); d->e = NULL; return 0; }
static devoptab_t nfs_dt[NFS_N];
static void nfs_mount(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    for (int p = 0; p < NFS_N; p++) {
        devoptab_t *t = &nfs_dt[p];
        memset(t, 0, sizeof(*t));
        t->name = nfs_scheme[p];
        t->structSize = sizeof(nfs_fd_t);
        t->open_r = nfs_open_r; t->close_r = nfs_close_r; t->read_r = nfs_read_r; t->seek_r = nfs_seek_r;
        t->fstat_r = nfs_fstat_r; t->stat_r = nfs_stat_r;
        t->dirStateSize = sizeof(nfs_dir_t);
        t->diropen_r = nfs_diropen_r; t->dirreset_r = nfs_dirreset_r; t->dirnext_r = nfs_dirnext_r; t->dirclose_r = nfs_dirclose_r;
        AddDevice(t);
    }
}
#else
/* simulateur sur PC : fopen / opendir / stat de tools/pc-sim passent par ici pour smb: ftp: fsp: */
static void nfs_mount(void) {}
int gcs_nfs_is(const char *path) { return nfs_is_net(path); }
void *gcs_nfs_fopen(const char *path) { return nfs_fopen(path); }
int gcs_nfs_fread(void *f, void *b, int n) { return nfs_fread((nfile_t *)f, b, n); }
long gcs_nfs_fseek(void *f, long o, int w) { return nfs_fseek((nfile_t *)f, o, w); }
void gcs_nfs_fclose(void *f) { nfs_fclose((nfile_t *)f); }
int gcs_nfs_stat(const char *path, int *dir, unsigned *size) { u32 s = 0; int r = nfs_stat(path, dir, &s); *size = s; return r; }
int gcs_nfs_list(const char *path, void **ents, int *n, int *esz) { *esz = (int)sizeof(nfs_ent_t); return nfs_list(path, (nfs_ent_t **)ents, n); }
#endif

/* ---------- reglages dans gc-mp3.txt ---------- */
static const char *const nfs_key[NFS_N] = { "smb", "ftp", "fsp", "gcs" };
static int nfs_setting(const char *k, const char *v) {
    for (int p = 0; p < NFS_GCS; p++) {                   /* (gcs : choisi dans le Salon, pas un reglage) */
        size_t l = strlen(nfs_key[p]);
        if (strncasecmp(k, nfs_key[p], l) || k[l] != '_') continue;
        const char *f = k + l + 1;
        nfs_cfg_t *c = &nfs_cfg[p];
        if (!strcasecmp(f, "adresse")) snprintf(c->host, sizeof(c->host), "%s", v);
        else if (!strcasecmp(f, "partage")) snprintf(c->share, sizeof(c->share), "%s", v);
        else if (!strcasecmp(f, "utilisateur")) snprintf(c->user, sizeof(c->user), "%s", v);
        else if (!strcasecmp(f, "mot_de_passe")) snprintf(c->pass, sizeof(c->pass), "%s", v);
        else if (!strcasecmp(f, "port")) { if (atoi(v) > 0) c->port = atoi(v); }
        else return 0;
        return 1;
    }
    return 0;
}
static void nfs_save(FILE *f) {
    fprintf(f, "# Serveurs du reseau (onglet FICHIERS)\n");
    for (int p = 0; p < NFS_GCS; p++) {
        const nfs_cfg_t *c = &nfs_cfg[p];
        fprintf(f, "%s_adresse = %s\n", nfs_key[p], c->host);
        if (p == NFS_SMB) fprintf(f, "%s_partage = %s\n", nfs_key[p], c->share);
        if (p != NFS_FSP) fprintf(f, "%s_utilisateur = %s\n", nfs_key[p], c->user);
        fprintf(f, "%s_mot_de_passe = %s\n%s_port = %d\n", nfs_key[p], c->pass, nfs_key[p], c->port);
    }
}


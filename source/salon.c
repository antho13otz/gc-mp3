// GC Station v138 : le Salon : plusieurs GC Station sur le meme reseau. Inclus dans main.c (apres tab_phone.c).
//  - decouverte : chaque console avec le Salon actif s'annonce en UDP (port 51234, a tout le reseau local)
//    toutes les 5 s, et ecoute les autres ;
//  - les autres consoles apparaissent dans l'onglet FICHIERS (liste des appareils) : leur carte SD se parcourt
//    comme un serveur (netfs.c, gcs:/) : musique, photos, videos, copie vers la carte (sauvegardes .gci comprises) ;
//  - "Ecouter avec" (Y sur une console) : le morceau que joue l'autre console, a la meme position ;
//  - cote serveur (fil du serveur du telephone) : /s/list, /s/stat, /s/get, /s/now, seulement Salon actif.

#define SALON_PORT 51234
/* (salon_peers, salon_n, salon_on, salon_name : declares dans netfs.c) */
static s32 salon_fd = -1;
static u64 salon_sent = 0;
static char salon_now_txt[NAME_LEN + 160] = "";           /* reponse de /s/now, faite par la boucle (pas par le fil du serveur) */
static char salon_now_title[80] = "";

static const char *salon_myname(void) {
    static char d[40];
    if (salon_name[0]) return salon_name;
    const char *dot = strrchr(ip_str, '.');
    snprintf(d, sizeof(d), "GC Station %s", dot && dot[1] ? dot + 1 : "");
    return d;
}
/* chemin demande (relatif a la carte SD) -> chemin complet ; refuse ".." et ce qui n'est pas sur la carte */
static int salon_path(const char *rel, char *full, int cap) {
    if (!sd_ok || sd_virtual) return 0;
    while (*rel == '/') rel++;
    for (const char *p = rel; *p; p++) if (p[0] == '.' && p[1] == '.' && (p == rel || p[-1] == '/') && (p[2] == '/' || !p[2])) return 0;
    if (strchr(rel, ':') || strchr(rel, '\\')) return 0;
    int n = snprintf(full, cap, "%s/%s", SD_ROOT, rel);
    while (n > (int)strlen(SD_ROOT) + 1 && full[n - 1] == '/') full[--n] = 0;
    return n > 0 && n < cap;
}
/* (fil du serveur du telephone) une demande /s/... ; 1 = traitee */
static int salon_route(s32 c, const char *target, const char *q, char *req) {
    if (strncmp(target, "/s/", 3)) return 0;
    if (!salon_on) { tp_reply(c, 403, "text/plain", "", 0); return 1; }
    const char *w = target + 3;
    char rel[NAME_LEN], full[NAME_LEN + 8];
    tp_param(q ? q : "", "p", rel, sizeof(rel));
    if (!strcmp(w, "now")) {
        char t[sizeof(salon_now_txt)];
        snprintf(t, sizeof(t), "%s", salon_now_txt);
        tp_reply(c, 200, "text/plain; charset=utf-8", t, (int)strlen(t));
        return 1;
    }
    if (!salon_path(rel, full, sizeof(full))) { tp_reply(c, 404, "text/plain", "", 0); return 1; }
    if (!strcmp(w, "get")) { sh_send_file(c, full, 0, req); return 1; }
    struct stat st;
    if (stat(full, &st) != 0) { tp_reply(c, 404, "text/plain", "", 0); return 1; }
    if (!strcmp(w, "stat")) {
        char t[48];
        int n = snprintf(t, sizeof(t), "%c %ld", S_ISDIR(st.st_mode) ? 'd' : 'f', S_ISDIR(st.st_mode) ? 0L : (long)st.st_size);
        tp_reply(c, 200, "text/plain", t, n);
        return 1;
    }
    if (!strcmp(w, "list") && S_ISDIR(st.st_mode)) {     /* "d\t0\tnom" ou "f\ttaille\tnom", une ligne par entree */
        DIR *d = opendir(full);
        if (!d) { tp_reply(c, 404, "text/plain", "", 0); return 1; }
        sbuf_t s = { NULL, 0, 0 };
        struct dirent *e;
        char p[NAME_LEN * 2], line[NAME_LEN + 32];
        int n = 0;
        while ((e = readdir(d)) != NULL && n < 4000) {
            if (e->d_name[0] == '.') continue;
            snprintf(p, sizeof(p), "%s/%s", full, e->d_name);
            struct stat es;
            int dir = is_dir(p, e);
            long sz = 0;
            if (!dir && stat(p, &es) == 0) sz = (long)es.st_size;
            snprintf(line, sizeof(line), "%c\t%ld\t%s\n", dir ? 'd' : 'f', sz, e->d_name);
            sb_add(&s, line);
            n++;
        }
        closedir(d);
        tp_reply(c, 200, "text/plain; charset=utf-8", s.b ? s.b : "", s.b ? s.n : 0);
        free(s.b);
        return 1;
    }
    tp_reply(c, 404, "text/plain", "", 0);
    return 1;
}

/* ---------- decouverte (boucle principale) ---------- */
static void salon_peer_seen(const char *ip, const char *name, const char *now) {
    if (!strcmp(ip, ip_str)) return;                      /* (soi-meme) */
    int k = -1;
    for (int i = 0; i < salon_n; i++) if (!strcmp(salon_peers[i].ip, ip)) k = i;
    if (k < 0) {
        if (salon_n >= SALON_MAX) return;
        k = salon_n++;
        salon_gen++;
        memset(&salon_peers[k], 0, sizeof(salon_peers[k]));
        snprintf(salon_peers[k].ip, sizeof(salon_peers[k].ip), "%s", ip);
        crumb("salon : %s (%s)", name, ip);
    }
    snprintf(salon_peers[k].name, sizeof(salon_peers[k].name), "%s", name);
    snprintf(salon_peers[k].now, sizeof(salon_peers[k].now), "%s", now);
    salon_peers[k].seen = now_ms();
}
static void salon_now_update(void) {                      /* ce que joue cette console (pour /s/now) */
    np_t n;
    np_get(&n);
    salon_now_title[0] = 0;
    if (src == SRC_FILE && playing && cur_track >= 0 && cur_track < nfiles && !strncmp(names[cur_track], SD_ROOT "/", strlen(SD_ROOT) + 1)) {
        snprintf(salon_now_txt, sizeof(salon_now_txt), "%s\t%d\t%d\t%s\n", names[cur_track] + strlen(SD_ROOT) + 1, n.active ? n.elapsed : 0, !paused, n.title);
        snprintf(salon_now_title, sizeof(salon_now_title), "%s", n.title);
    } else snprintf(salon_now_txt, sizeof(salon_now_txt), "\t0\t0\t\n");
}
static void salon_pump(void) {
    if (!salon_on) {
        if (salon_fd >= 0) { net_close(salon_fd); salon_fd = -1; salon_n = 0; salon_gen++; }
        return;
    }
    if (net_phase != NET_DONE) { net_start_async(); return; }
    if (!net_ok) return;
    if (tp_state == PS_OFF) tp_wanted = 1;                /* (les autres consoles passent par son serveur) */
    if (salon_fd < 0) {
        salon_fd = net_socket(AF_INET, SOCK_DGRAM, 0);
        if (salon_fd < 0) return;
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_port = htons(SALON_PORT); a.sin_addr.s_addr = INADDR_ANY;
        if (net_bind(salon_fd, (struct sockaddr *)&a, sizeof(a)) < 0) { net_close(salon_fd); salon_fd = -1; return; }
        set_nonblock(salon_fd, 1);
        salon_sent = 0;
    }
    u64 now = now_ms();
    if (!salon_sent || now - salon_sent >= 5000) {        /* annonce : a tout le reseau local, et a son sous-reseau */
        salon_sent = now;
        salon_now_update();
        char m[160];
        int ml = snprintf(m, sizeof(m), "GCST1\t%s\t%d\t%s\n", salon_myname(), PHONE_PORT, salon_now_title);
        struct sockaddr_in b;
        memset(&b, 0, sizeof(b));
        b.sin_family = AF_INET; b.sin_port = htons(SALON_PORT);
        b.sin_addr.s_addr = htonl(0xFFFFFFFFu);
        net_sendto(salon_fd, m, ml, 0, (struct sockaddr *)&b, sizeof(b));
        unsigned a1, a2, a3, a4;
        if (sscanf(ip_str, "%u.%u.%u.%u", &a1, &a2, &a3, &a4) == 4) {
            b.sin_addr.s_addr = htonl((u32)a1 << 24 | (u32)a2 << 16 | (u32)a3 << 8 | 255u);
            net_sendto(salon_fd, m, ml, 0, (struct sockaddr *)&b, sizeof(b));
        }
    }
    for (int k = 0; k < 8; k++) {                         /* annonces recues */
        char m[200];
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        s32 r = net_recvfrom(salon_fd, m, sizeof(m) - 1, 0, (struct sockaddr *)&from, &fl);
        if (r <= 0) break;
        m[r] = 0;
        if (strncmp(m, "GCST1\t", 6)) continue;
        char *f[4] = { m + 6, NULL, NULL, NULL };
        for (int i = 1; i < 4; i++) { f[i] = f[i - 1] ? strchr(f[i - 1], '\t') : NULL; if (f[i]) *f[i]++ = 0; }
        if (f[3]) f[3][strcspn(f[3], "\r\n")] = 0;
        u32 ip = ntohl(from.sin_addr.s_addr);
        char ips[16];
        snprintf(ips, sizeof(ips), "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)(ip >> 16 & 255), (unsigned)(ip >> 8 & 255), (unsigned)(ip & 255));
        salon_peer_seen(ips, f[0][0] ? f[0] : ips, f[3] ? f[3] : "");
    }
    for (int i = 0; i < salon_n; ) {                      /* plus d'annonce depuis 20 s : partie */
        if (now - salon_peers[i].seen > 20000) { salon_peers[i] = salon_peers[--salon_n]; salon_gen++; continue; }
        i++;
    }
}

/* ---------- "Ecouter avec" : le morceau de l'autre console, a la meme position ---------- */
static const char *salon_listen(int k) {
    if (k < 0 || k >= salon_n) return "Console introuvable";
    snprintf(nfs_cfg[NFS_GCS].host, sizeof(nfs_cfg[NFS_GCS].host), "%s", salon_peers[k].ip);
    nfs_cfg[NFS_GCS].port = PHONE_PORT;
    static char t[NAME_LEN + 200];
    u64 t0 = now_ms();
    int len = gcs_http("now", "", 0, 0, (u8 *)t, sizeof(t) - 1, NULL);
    if (len <= 0) return nfs_err[0] ? nfs_err : "L'autre console ne r\xC3\xA9pond pas";
    t[len] = 0;
    char *f1 = strchr(t, '\t'), *f2 = f1 ? strchr(f1 + 1, '\t') : NULL;
    if (!f1 || f1 == t || !f2) return "L'autre console ne joue pas de morceau de sa carte SD";
    *f1 = 0;
    int pos = atoi(f1 + 1), play = atoi(f2 + 1);
    char path[NAME_LEN];
    snprintf(path, sizeof(path), "gcs:/%s", t);
    int tr = -1;
    for (int i = 0; i < nfiles; i++) if (!strcmp(names[i], path)) tr = i;
    if (tr < 0) { music_add_file(path); for (int i = 0; i < nfiles; i++) if (!strcmp(names[i], path)) tr = i; }
    if (tr < 0) return "Biblioth\xC3\xA8que pleine";
    queue_build(tr);
    if (!start_track(tr)) return track_err ? track_err : "Lecture impossible";
    int lag = (int)((now_ms() - t0) / 1000);              /* (le temps de la demande et de l'ouverture) */
    if (play && pos + lag > 1) track_seek(pos + lag);
    return NULL;
}

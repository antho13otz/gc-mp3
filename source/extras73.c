// GC-MP3 v73 : nouveautes (inclus a la fin de main.c, apres natif.c)
//   - podcasts : vitesse x1,25 / x1,5 et blancs sautes (reglages ; le traitement est dans out_put)
//   - radio : historique des titres passes, avec pochette (iTunes), ligne "Titres passes" de RADIO
//   - radio : enregistrement programme (heure + duree, la radio qui jouait quand on l'a regle)
//   - economiseur "Horloge + meteo + pochette"
//   - sauvegarde / restauration de tous les reglages en un fichier (SD:/gc-mp3/reglages/gc-mp3-sauvegarde.txt)
//   - cartes memoire virtuelles du Resident : plusieurs cartes, sauvegardes listees, copie
//     depuis / vers la SD (.gci) et la vraie carte, suppression, formatage
//   - telephone : liste des jeux et lancement (tab_phone.c appelle x73_games_json / x73_launch)

/* =================== reglages =================== */
static int rp_on = 0, rp_hm = 20 * 60, rp_len = 60;        /* enregistrement programme : heure (minutes), duree */
static char rp_url[URL_MAX] = "", rp_name[48] = "";
static int x73_setting(const char *k, const char *v) {
    if (!strcasecmp(k, "podcast_vitesse")) { int s = atoi(v); ts_speed = s >= 140 ? 150 : s >= 115 ? 125 : 100; return 1; }
    if (!strcasecmp(k, "podcast_blancs")) { ts_sil = yes(v); return 1; }
    if (!strcasecmp(k, "rythme_touches")) { rg_lanes = atoi(v) == 5 ? 5 : 4; return 1; }
    if (!strcasecmp(k, "radio_prog")) {                      /* "21:30" ou "non" */
        int h, m;
        rp_on = sscanf(v, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60;
        if (rp_on) rp_hm = h * 60 + m;
        return 1;
    }
    if (!strcasecmp(k, "radio_prog_duree")) { int d = atoi(v); rp_len = d < 5 ? 5 : d > 600 ? 600 : d; return 1; }
    if (!strcasecmp(k, "radio_prog_url")) { snprintf(rp_url, sizeof(rp_url), "%s", v); return 1; }
    if (!strcasecmp(k, "radio_prog_nom")) { snprintf(rp_name, sizeof(rp_name), "%s", v); return 1; }
    if (!strcasecmp(k, "reseau_dhcp")) { net_dhcp = yes(v); net_cfg_seen = 1; return 1; }        /* v75 : reseau de GC Station */
    if (!strcasecmp(k, "reseau_ip")) { snprintf(net_cfg_ip, sizeof(net_cfg_ip), "%.15s", v); return 1; }
    if (!strcasecmp(k, "reseau_passerelle")) { snprintf(net_cfg_gw, sizeof(net_cfg_gw), "%.15s", v); return 1; }
    if (!strcasecmp(k, "reseau_masque")) { int b = atoi(v); net_cfg_bits = b >= 8 && b <= 30 ? b : 24; return 1; }
    if (!strcasecmp(k, "fichiers_caches")) { fe_show_hidden = yes(v); return 1; }
    if (!strcasecmp(k, "fichiers_inconnus_caches")) { fe_hide_unknown = yes(v); return 1; }
    if (!strcasecmp(k, "retour_langue_console")) { int l = atoi(v); sram_back_lang = v[0] && l >= 0 && l < 6 ? l : -1; return 1; }
    if (!strcasecmp(k, "retour_son_console")) { sram_back_sound = v[0] ? atoi(v) != 0 : -1; if (!v[0] || v[0] == '-') sram_back_sound = -1; return 1; }
    if (!strcasecmp(k, "resident_carte_fichier")) {
        int ok = v[0] && strlen(v) < sizeof(vc_active) - 1 && !strchr(v, '/') && !strchr(v, '\\') && strstr(v, "..") == NULL;
        if (ok) snprintf(vc_active, sizeof(vc_active), "%s", v);
        return 1;
    }
    return 0;
}
static char pj_main_card[64];                          /* (profils.c) */
static void x73_save(FILE *f) {
    fprintf(f, "# Podcasts : vitesse (100 / 125 / 150) et blancs sautes (oui / non)\npodcast_vitesse = %d\npodcast_blancs = %s\n", ts_speed, ts_sil ? "oui" : "non");
    if (rp_on) fprintf(f, "# Enregistrement programme de la radio : heure (HH:MM ou non), duree en minutes, radio\nradio_prog = %02d:%02d\n", rp_hm / 60, rp_hm % 60);
    else fprintf(f, "# Enregistrement programme de la radio : heure (HH:MM ou non), duree en minutes, radio\nradio_prog = non\n");
    fprintf(f, "radio_prog_duree = %d\nradio_prog_url = %s\nradio_prog_nom = %s\n", rp_len, rp_url, rp_name);
    fprintf(f, "# Jeu de rythme : 4 touches (A B X Y) ou 5 (+ R)\nrythme_touches = %d\n", rg_lanes);
    fprintf(f, "# Reseau : adresse automatique (oui) ou fixe (non : ip, masque en bits, passerelle)\nreseau_dhcp = %s\nreseau_ip = %s\nreseau_masque = %d\nreseau_passerelle = %s\n",
            net_dhcp ? "oui" : "non", net_cfg_ip, net_cfg_bits, net_cfg_gw);
    fprintf(f, "# Onglet FICHIERS : fichiers caches affiches, fichiers inconnus caches (oui / non)\nfichiers_caches = %s\nfichiers_inconnus_caches = %s\n",
            fe_show_hidden ? "oui" : "non", fe_hide_unknown ? "oui" : "non");
    if (sram_back_lang >= 0) fprintf(f, "retour_langue_console = %d\n", sram_back_lang);
    if (sram_back_sound >= 0) fprintf(f, "retour_son_console = %d\n", sram_back_sound);
    fprintf(f, "# Pulse : carte memoire virtuelle utilisee (fichier de gc-mp3/resident)\nresident_carte_fichier = %s\n", gp_name[0] ? (pj_main_card[0] ? pj_main_card : "carte-A.raw") : vc_keep[0] ? vc_keep : vc_active);   /* (v191 : profil : sa carte est dans profil.txt) */   /* (v76 : pas la carte d'un jeu) */
}
static void x73_menu_value(int i, char *out, int cap) {
    switch (i) {
    case MI_PODSPEED: snprintf(out, cap, "%s", ts_speed == 150 ? "x1,5" : ts_speed == 125 ? "x1,25" : "Normale"); break;
    case MI_PODSIL:   snprintf(out, cap, "%s", ts_sil ? "Oui" : "Non"); break;
    case MI_RECAT:
        if (!rp_on) snprintf(out, cap, "Non");
        else snprintf(out, cap, "%02d:%02d  (%.28s)", rp_hm / 60, rp_hm % 60, rp_name[0] ? rp_name : "?");
        break;
    case MI_RECLEN:   snprintf(out, cap, rp_len >= 60 && rp_len % 60 == 0 ? "%d h" : "%d min", rp_len >= 60 && rp_len % 60 == 0 ? rp_len / 60 : rp_len); break;
    default: out[0] = 0;
    }
}
static int rp_state = 0;                                   /* 0 attend, 1 enregistre, 2 fini pour aujourd'hui */
static u32 rp_day = 0;
static void x73_menu_change(int i, int d) {
    switch (i) {
    case MI_PODSPEED: ts_speed = d > 0 ? (ts_speed == 100 ? 125 : ts_speed == 125 ? 150 : 100) : (ts_speed == 100 ? 150 : ts_speed == 150 ? 125 : 100); break;
    case MI_PODSIL:   ts_sil = !ts_sil; break;
    case MI_RECAT:                                         /* Non, puis 00:00 a 23:30 par demi-heures */
        if (!rp_on) {
            rp_on = 1;
            time_t t = time(NULL); struct tm *tm = localtime(&t);
            rp_hm = tm ? ((tm->tm_hour * 60 + tm->tm_min) / 30 * 30 + 30) % 1440 : 20 * 60;
            if (d < 0) rp_hm = (rp_hm + 1440 - 60) % 1440;
        } else {
            int n = rp_hm / 30 * 30 + d * 30;
            if (n < 0 || n >= 1440) rp_on = 0; else rp_hm = n;
        }
        if (rp_on && src == SRC_RADIO && cur_station >= 0 && cur_station <= nstations && !pod_on) {   /* la radio qui joue */
            snprintf(rp_url, sizeof(rp_url), "%s", stations[cur_station].url);
            snprintf(rp_name, sizeof(rp_name), "%s", stations[cur_station].name);
        } else if (rp_on && !rp_url[0] && radio_last_url[0]) {
            snprintf(rp_url, sizeof(rp_url), "%s", radio_last_url);
            snprintf(rp_name, sizeof(rp_name), "%s", radio_last_name);
        } else if (rp_on && !rp_url[0] && nstations > 0) {
            snprintf(rp_url, sizeof(rp_url), "%s", stations[0].url);
            snprintf(rp_name, sizeof(rp_name), "%s", stations[0].name);
        }
        rp_state = 0; rp_day = 0;
        break;
    case MI_RECLEN: {
        static const int L[] = { 15, 30, 45, 60, 90, 120, 180, 240 };
        int k = 0;
        for (int j = 0; j < 8; j++) if (L[j] <= rp_len) k = j;
        rp_len = L[(k + d + 8) % 8];
        break;
    }
    }
}

/* =================== enregistrement programme =================== */
/* minute du jour et jour (pour ne le faire qu'une fois par jour) */
static int rp_now(u32 *day) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm || tm->tm_year < 100) return -1;
    *day = (u32)(tm->tm_year * 400 + tm->tm_yday);
    return tm->tm_hour * 60 + tm->tm_min;
}
static int rp_station(void) {                               /* station a lancer (ajoutee dans la case libre si besoin) */
    for (int i = 0; i < nstations; i++) if (!strcmp(stations[i].url, rp_url)) return i;
    if (nstations >= MAX_STATIONS) return -1;
    snprintf(stations[nstations].url, sizeof(stations[0].url), "%s", rp_url);
    snprintf(stations[nstations].name, sizeof(stations[0].name), "%s", rp_name[0] ? rp_name : "Radio");
    return nstations;
}
static int rp_playing(void) {
    return src == SRC_RADIO && !pod_on && cur_station >= 0 && cur_station <= nstations && !strcmp(stations[cur_station].url, rp_url);
}
/* m = minute du jour (-1 = heure inconnue) */
static void rp_tick(int m, u32 day) {
    if (!rp_on || !rp_url[0] || m < 0) { if (rp_state == 1) { rec_stop(); rp_state = 0; } return; }
    int el = (m - rp_hm + 1440) % 1440;                     /* minutes depuis le debut */
    int inside = el < rp_len;
    if (rp_state == 2 && (!inside || day != rp_day)) rp_state = 0;
    if (rp_state == 0 && inside && el <= 2 && day != rp_day) {   /* (dans les 2 minutes : on ne rattrape pas une heure passee) */
        rp_day = day;
        rp_state = 1;
        crumb("radio : enregistrement programme %s", rp_name);
        if (!rp_playing()) {
            if (src == SRC_FILE) stop_track();
            int k = rp_station();
            if (k >= 0) start_station(k, 0);
        }
        snprintf(radio_note, sizeof(radio_note), "Enregistrement programm\xC3\xA9 : %s (%d min)", rp_name[0] ? rp_name : "radio", rp_len);
        radio_note_t = now_ms();
    }
    if (rp_state == 1) {
        if (!inside) { rec_stop(); rp_state = 2; return; }
        if (rp_playing() && state == ST_PLAY && !rec_fp && net_ok) rec_start(rp_name[0] ? rp_name : stations[cur_station].name);
        if (!rp_playing() && rec_fp) rec_stop();                 /* (on a change de radio : fin) */
    }
}

/* =================== historique des titres de la radio =================== */
#define RH_FILE GCD "/radio/historique.txt"
#define RH_CV   GCD "/cache/radio-titres"
#define RH_MAX  200
#define RH_TS   64                                            /* pochettes 64 x 64 */
typedef struct { char when[20], st[48], ti[160]; u16 *tex; s8 cs; GXTexObj obj; } rh_ent;
static rh_ent *rh = NULL;
static int rh_n = 0, rh_sel = 0, rh_top = 0, rh_confirm = 0;
static char rh_last[160] = "";
static u64 rh_last_t = 0;

static void rh_key(const char *ti, char *out, int cap) {        /* nom du fichier de la pochette */
    u32 h1 = 2166136261u, h2 = 5381;
    for (const unsigned char *p = (const unsigned char *)ti; *p; p++) { int c = tolower(*p); h1 = (h1 ^ c) * 16777619u; h2 = h2 * 33 + c; }
    snprintf(out, cap, RH_CV "/%08x%08x", (unsigned)h1, (unsigned)h2);
}
/* titre qui ne vaut pas la peine (vide, nom de la radio, pub...) */
static int rh_skip(const char *ti, const char *st) {
    if (!ti[0] || strlen(ti) < 3) return 1;
    if (!strcasecmp(ti, st)) return 1;
    int letters = 0;
    for (const char *p = ti; *p; p++) if (isalpha((unsigned char)*p)) letters++;
    return letters < 2;
}
/* v195 : la ligne est preparee dans la boucle, ecrite par le fil (rh_append_do) */
static void rh_append(const char *st, const char *ti) {
    if (sx_quiet) return;                                  /* v263 : (radio du test de la console : pas dans l'historique) */
    if (!sd_ok || sd_virtual) return;
    char name[100], t2[200], line[360];
    text_display(st, name, sizeof(name));
    text_display(ti, t2, sizeof(t2));
    for (char *p = name; *p; p++) if (*p == '|') *p = '/';
    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    if (tm && tm->tm_year >= 100) snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d | %s | %s\n", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, name, t2);
    else snprintf(line, sizeof(line), "- | %s | %s\n", name, t2);
    if (bgf_put_mode(BGF_RH, 0, RH_FILE, line, (long)strlen(line) + 1)) return;
    rh_append_do(line);
}
static void rh_append_do(const char *line) {               /* (fil lsc, ou boucle sans fil) */
    mkdir(GCD "/radio", 0777);
    struct stat sb;
    if (stat(RH_FILE, &sb) == 0 && sb.st_size > 96 * 1024) {    /* trop long : on garde les 300 derniers */
        long len = 0;
        char *b = (char *)read_file(RH_FILE, 256 * 1024, &len);
        if (b) {
            int lines = 0; long cut = len;
            while (cut > 0 && lines < 300) { cut--; if (b[cut] == '\n' && cut != len - 1) lines++; }
            FILE *f = fopen(RH_FILE ".part", "wb");
            int ok = f && fwrite(b + (cut ? cut + 1 : 0), 1, len - (cut ? cut + 1 : 0), f) == (size_t)(len - (cut ? cut + 1 : 0));
            if (f && fclose(f) != 0) ok = 0;
            if (ok) { remove(RH_FILE); rename(RH_FILE ".part", RH_FILE); } else remove(RH_FILE ".part");
            free(b);
        }
    }
    FILE *f = fopen(RH_FILE, "a");
    if (!f) return;
    fputs(line, f);
    fclose(f);
}

/* ---- pochettes : recherche iTunes (une a la fois), image 100 x 100 -> 64 x 64 RGB5A3 en cache ---- */
#ifndef ITUNES_BASE
#define ITUNES_BASE "https://itunes.apple.com"
#endif
static hfetch_t rhcf = { .c = { -1, 0, 0 } };
static int rhc_state = 0;                                    /* 0 libre, 1 recherche, 2 image */
static char rhc_q[8][160];                                   /* titres en attente */
static int rhc_n = 0;
static char rhc_cur[160];
static u64 rhc_t = 0;
static void rh_tile(const unsigned char *px, int w, int h, u16 *out) {   /* RGB -> RGB5A3 en tuiles 4 x 4 */
    for (int y = 0; y < RH_TS; y++) for (int x = 0; x < RH_TS; x++) {
        int x0 = x * w / RH_TS, x1 = (x + 1) * w / RH_TS, y0 = y * h / RH_TS, y1 = (y + 1) * h / RH_TS;
        if (x1 <= x0) x1 = x0 + 1;
        if (y1 <= y0) y1 = y0 + 1;
        u32 r = 0, g = 0, b = 0, n = 0;
        for (int yy = y0; yy < y1 && yy < h; yy++) for (int xx = x0; xx < x1 && xx < w; xx++) {
            const unsigned char *p = px + ((long)yy * w + xx) * 3;
            r += p[0]; g += p[1]; b += p[2]; n++;
        }
        if (!n) n = 1;
        u16 v = (u16)(0x8000 | ((r / n) >> 3) << 10 | ((g / n) >> 3) << 5 | ((b / n) >> 3));
        out[((y >> 2) * (RH_TS / 4) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)] = v;
    }
}
static void rhc_mark(const char *ti, const u16 *tex) {       /* tex NULL : pas de pochette pour ce titre */
    if (!sd_ok || sd_virtual) return;
    char p[NAME_LEN];
    mkdir(GCD "/cache", 0777);
    mkdir(RH_CV, 0777);
    rh_key(ti, p, sizeof(p) - 8);
    strcat(p, tex ? ".tex" : ".no");
    if (tex ? bgf_put_mode(BGF_REPL, 1, p, tex, RH_TS * RH_TS * 2) : bgf_put_mode(BGF_REPL, 1, p, "0", 1)) return;   /* v195 : par le fil */
    FILE *f = fopen(p, "wb");
    if (!f) return;
    if (tex) fwrite(tex, 2, RH_TS * RH_TS, f);
    else fputc('0', f);
    fclose(f);
}
static int rhc_known(const char *ti) {
    char p[NAME_LEN], q[NAME_LEN];
    struct stat sb;
    rh_key(ti, p, sizeof(p) - 8);
    snprintf(q, sizeof(q), "%s.tex", p);
    if (stat(q, &sb) == 0) return 1;
    snprintf(q, sizeof(q), "%s.no", p);
    return stat(q, &sb) == 0 ? 2 : 0;
}
static void rhc_want(const char *ti) {
    if (!covers_auto || !sd_ok || sd_virtual || rhc_known(ti)) return;
    if (!strcmp(rhc_cur, ti) && rhc_state) return;
    for (int i = 0; i < rhc_n; i++) if (!strcmp(rhc_q[i], ti)) return;
    if (rhc_n == 8) { memmove(rhc_q[0], rhc_q[1], sizeof(rhc_q[0]) * 7); rhc_n--; }
    snprintf(rhc_q[rhc_n++], sizeof(rhc_q[0]), "%s", ti);
}
static void rh_refresh_tex(const char *ti);
/* "artworkUrl100":"https://...100x100bb.jpg" */
static int rhc_art(const char *js, char *out, int cap) {
    const char *p = strstr(js, "\"artworkUrl100\"");
    if (!p) return 0;
    p = strchr(p + 15, '"');
    if (!p) return 0;
    p++;
    int n = 0;
    while (*p && *p != '"' && n < cap - 1) { if (*p == '\\' && p[1] == '/') p++; out[n++] = *p++; }
    out[n] = 0;
    return n > 10 && !strncmp(out, "http", 4);
}
static void rhc_pump(void) {
    if (rhc_state == 0) {
        if (!rhc_n || net_phase != NET_DONE || !net_ok || now_ms() - rhc_t < 1500) return;
        snprintf(rhc_cur, sizeof(rhc_cur), "%s", rhc_q[0]);
        memmove(rhc_q[0], rhc_q[1], sizeof(rhc_q[0]) * (rhc_n - 1)); rhc_n--;
        char q[200], d[200], enc[600], url[URL_MAX];
        text_display(rhc_cur, d, sizeof(d));
        int n = 0;                                          /* "Artiste - Titre (Radio Edit)" -> "Artiste Titre" */
        for (const char *p = d; *p && n < (int)sizeof(q) - 1; p++) {
            if (*p == '(' || *p == '[') { while (*p && *p != ')' && *p != ']') p++; if (!*p) break; continue; }
            q[n++] = (*p == '-' || *p == '/' || *p == '|') ? ' ' : *p;
        }
        q[n] = 0;
        if (!url_encode(trim(q), enc, sizeof(enc))) { rhc_mark(rhc_cur, NULL); return; }
        snprintf(url, sizeof(url), ITUNES_BASE "/search?media=music&entity=song&limit=1&term=%s", enc);
        rhcf.image = 0; rhcf.maxbytes = 256 * 1024; rhcf.accept = "application/json";
        hf_start(&rhcf, url, NULL);
        rhc_state = 1;
        rhc_t = now_ms();
        return;
    }
    hf_pump(&rhcf);
    if (rhcf.st == HF_FAIL) { hf_drop(&rhcf); rhc_state = 0; rhc_t = now_ms(); return; }   /* (on reessaiera une autre fois) */
    if (rhcf.st != HF_DONE) { if (now_ms() - rhc_t > 20000) { hf_abort(&rhcf); rhc_state = 0; } return; }
    rhcf.st = HF_IDLE;
    if (rhc_state == 1) {
        char *js = (char *)malloc(rhcf.len + 1), art[URL_MAX];
        int ok = 0;
        if (js) { memcpy(js, rhcf.buf, rhcf.len); js[rhcf.len] = 0; ok = rhc_art(js, art, sizeof(art)); free(js); }
        hf_drop(&rhcf);
        if (!ok) { rhc_mark(rhc_cur, NULL); rhc_state = 0; rhc_t = now_ms(); return; }
        rhcf.image = 1; rhcf.maxbytes = 512 * 1024; rhcf.accept = "image/jpeg,image/png,*/*";
        hf_start(&rhcf, art, NULL);
        rhc_state = 2;
        rhc_t = now_ms();
        return;
    }
    int w, h, nc;
    unsigned char *px = rhcf.len > 0 && stbi_info_from_memory(rhcf.buf, rhcf.len, &w, &h, &nc) && w > 0 && h > 0 && (long)w * h <= 1200L * 1200L
                        ? stbi_load_from_memory(rhcf.buf, rhcf.len, &w, &h, &nc, 3) : NULL;
    hf_drop(&rhcf);
    if (px) {
        static u16 t[RH_TS * RH_TS];
        rh_tile(px, w, h, t);
        stbi_image_free(px);
        rhc_mark(rhc_cur, t);
        rh_refresh_tex(rhc_cur);
    } else rhc_mark(rhc_cur, NULL);
    rhc_state = 0;
    rhc_t = now_ms();
}

/* le titre qui passe : dans l'historique (une fois), et sa pochette cherchee */
static void rh_capture(void) {
    if (src != SRC_RADIO || state != ST_PLAY || pod_on || cur_station < 0 || cur_station > nstations) return;
    if (!now_title[0] || !strcmp(now_title, rh_last)) return;
    if (now_ms() - rh_last_t < 4000 && rh_last_t) return;        /* (titre qui change vite : on attend qu'il se pose) */
    snprintf(rh_last, sizeof(rh_last), "%s", now_title);
    rh_last_t = now_ms();
    if (rh_skip(now_title, stations[cur_station].name)) return;
    rh_append(stations[cur_station].name, now_title);
    char d[200];
    text_display(now_title, d, sizeof(d));
    rhc_want(d);
}

static void rh_free(void) {
    if (!rh) return;
    for (int i = 0; i < rh_n; i++) if (rh[i].tex) { free(rh[i].tex); rh[i].tex = NULL; }
    rh_n = 0;
}
static void rh_load_tex(rh_ent *e) {
    if (e->cs) return;
    char p[NAME_LEN], q[NAME_LEN];
    rh_key(e->ti, p, sizeof(p) - 8);
    snprintf(q, sizeof(q), "%s.tex", p);
    e->cs = 2;
    FILE *f = fopen(q, "rb");
    if (!f) { if (!rhc_known(e->ti)) { e->cs = 0; rhc_want(e->ti); e->cs = 3; } return; }
    u16 *t = (u16 *)memalign(32, RH_TS * RH_TS * 2);
    if (t && fread(t, 2, RH_TS * RH_TS, f) == RH_TS * RH_TS) {
        DCFlushRange(t, RH_TS * RH_TS * 2);
        GX_InitTexObj(&e->obj, t, RH_TS, RH_TS, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InvalidateTexAll();                 /* v155 : (nouvelle image a une adresse deja servie : le cache de textures du GPU gardait des morceaux de l'ancienne) */
        GX_InitTexObjFilterMode(&e->obj, GX_LINEAR, GX_LINEAR);
        e->tex = t; e->cs = 1;
    } else free(t);
    fclose(f);
}
static void rh_refresh_tex(const char *ti) {
    if (!rh) return;
    for (int i = 0; i < rh_n; i++) if (rh[i].cs == 3 && !strcmp(rh[i].ti, ti)) rh[i].cs = 0;
}
static void rh_open(void) {
    if (!rh) rh = (rh_ent *)calloc(RH_MAX, sizeof(rh_ent));
    if (!rh) return;
    rh_free();
    long len = 0;
    char *b = sd_ok ? (char *)read_file(RH_FILE, 256 * 1024, &len) : NULL;
    if (b) {
        char *s = b, *end = b + len;
        static char lines[RH_MAX][240];
        int n = 0;
        while (s < end) {                                     /* (les plus recents d'abord) */
            char *e = memchr(s, '\n', end - s);
            if (!e) e = end;
            int l = (int)(e - s);
            if (l > 0 && l < 240) {
                if (n == RH_MAX) { memmove(lines[0], lines[1], sizeof(lines[0]) * (RH_MAX - 1)); n--; }
                memcpy(lines[n], s, l); lines[n][l] = 0; n++;
            }
            s = e + 1;
        }
        for (int i = n - 1; i >= 0; i--) {
            char *b1 = strstr(lines[i], " | "), *b2 = b1 ? strstr(b1 + 3, " | ") : NULL;
            if (!b2) continue;
            *b1 = 0; *b2 = 0;
            rh_ent *r = &rh[rh_n];
            memset(r, 0, sizeof(*r));
            snprintf(r->when, sizeof(r->when), "%s", trim(lines[i]));
            snprintf(r->st, sizeof(r->st), "%s", trim(b1 + 3));
            snprintf(r->ti, sizeof(r->ti), "%s", trim(b2 + 3));
            rh_n++;
        }
        free(b);
    }
    rh_sel = 0; rh_top = 0; rh_confirm = 0;
    tl_screen = TL_RHIST;
}
static void rh_input(u32 down, u32 nav, int fire) {
    if (rh_confirm) {
        if (down & PAD_BUTTON_A) { remove(RH_FILE); rh_free(); rh_confirm = 0; tl_note("Historique effac\xC3\xA9"); }
        if (down & PAD_BUTTON_B) rh_confirm = 0;
        return;
    }
    if (fire && (nav & PAD_BUTTON_DOWN) && rh_sel < rh_n - 1) rh_sel++;
    if (fire && (nav & PAD_BUTTON_UP) && rh_sel > 0) rh_sel--;
    if (fire && (nav & PAD_BUTTON_RIGHT)) { rh_sel += 7; if (rh_sel > rh_n - 1) rh_sel = rh_n - 1; if (rh_sel < 0) rh_sel = 0; }
    if (fire && (nav & PAD_BUTTON_LEFT)) { rh_sel -= 7; if (rh_sel < 0) rh_sel = 0; }
    if ((down & PAD_BUTTON_A) && rh_n) {                      /* chercher dans ma musique */
        char q[200];
        snprintf(q, sizeof(q), "%s", rh[rh_sel].ti);
        rh_free(); tl_screen = TL_NONE;
        titles_search(q);
        return;
    }
    if ((down & PAD_BUTTON_X) && rh_n && sd_ok) {             /* garder ce titre (liste des titres gardes) */
        FILE *f = fopen(TITLES_FILE, "a");
        if (f) { fprintf(f, "%s | %s | %s\n", rh[rh_sel].when, rh[rh_sel].st, rh[rh_sel].ti); fclose(f); tl_note("Titre gard\xC3\xA9"); }
    }
    if ((down & PAD_BUTTON_Y) && rh_n) rh_confirm = 1;
    if (down & PAD_BUTTON_B) { rh_free(); tl_screen = TL_NONE; }
}
static void draw_rhist(void) {
    char buf[200], fv[200];
    snprintf(buf, sizeof(buf), rh_n == 1 ? "1 titre" : "%d titres", rh_n);
    tl_frame("Titres pass\xC3\xA9s \xC3\xA0 la radio", buf);
    const int vis = 7;
    const f32 rh_h = 44;
    if (rh_sel < rh_top) rh_top = rh_sel;
    if (rh_sel >= rh_top + vis) rh_top = rh_sel - vis + 1;
    if (rh_top > rh_n - vis) rh_top = rh_n - vis;
    if (rh_top < 0) rh_top = 0;
    int loads = 0;
    for (int i = 0; i < vis && rh_top + i < rh_n; i++) {
        int r = rh_top + i, on = r == rh_sel;
        rh_ent *e = &rh[r];
        f32 ry = 88 + i * rh_h;
        gp_zone(52, ry, 536, rh_h - 4, &rh_sel, r, GPZ_SEL);
        if (on) gx_rrect_fill(52, ry, 536, rh_h - 4, 6, TL_SEL1, TL_SEL2);
        if (!e->cs && loads < 2) { rh_load_tex(e); loads++; }        /* (deux par image) */
        if (e->cs == 1 && e->tex) draw_tex_fit(&e->obj, RH_TS, RH_TS, RH_TS, RH_TS, 58, ry + 2, 36, 36);
        else { gx_rrect_fill(58, ry + 2, 36, 36, 5, RGBA(60, 40, 120, 255), RGBA(40, 26, 90, 255)); draw_radio_icon(76, ry + 20, RGBA(170, 150, 235, 255)); }
        fit_text(F_MED, e->ti, 470, fv, sizeof(fv));
        gx_text1(F_MED, 104, ry + 17, fv, on ? RGBA(255, 255, 255, 255) : RGBA(232, 224, 255, 255));
        snprintf(buf, sizeof(buf), "%s  -  %s", e->st, e->when);
        fit_text(F_SMALL, buf, 470, fv, sizeof(fv));
        gx_text1(F_SMALL, 104, ry + 34, fv, on ? RGBA(230, 220, 255, 255) : RGBA(150, 135, 210, 255));
    }
    if (!rh_n) {
        gx_text1(F_MED, 64, 130, "Aucun titre pour l'instant.", RGBA(230, 222, 255, 255));
        gx_text1(F_SMALL, 64, 156, "Les titres annonc\xC3\xA9s par les radios s'ajoutent ici tout seuls.", RGBA(170, 155, 225, 255));
    }
    if (rh_confirm) tl_confirm("Effacer tout l'historique ?", "");
    tl_msgbox();
    static const char *const k[] = { "A", "X", "Y", "B" }, *const l[] = { "CHERCHER", "GARDER", "EFFACER", "RETOUR" };
    tl_legend(k, l, 4);
}

/* =================== economiseur : horloge + meteo + pochette =================== */
static void saver_tableau(float t) {
    char hm[16], date[64], ti[200], sub[200], buf[200];
    int sec = saver_clock(hm, sizeof(hm), date, sizeof(date));
    f32 cx = 320 + 70 * sinf(t * 0.041f), cy = 220 + 50 * sinf(t * 0.029f + 1.1f);
    uxsave_t uz = ux_native(cx);
    /* horloge (a gauche) */
    const f32 dw = 46, dh = 84, gap = 10, colon = 26;
    f32 x = cx - 270, y = cy - 120;
    col_t top = RGBA(200, 170, 255, 235), bot = RGBA(110, 70, 240, 235);
    if (hm[0]) {
        seg_digit(x, y, dw, dh, hm[0] - '0', top, bot);
        seg_digit(x + dw + gap, y, dw, dh, hm[1] - '0', top, bot);
        f32 kx = x + 2 * dw + gap + colon / 2 - 4;
        u8 a = (sec & 1) ? 90 : 230;
        gx_rrect_fill(kx, y + dh * 0.30f, 8, 8, 3, RGBA(200, 170, 255, a), RGBA(110, 70, 240, a));
        gx_rrect_fill(kx, y + dh * 0.62f, 8, 8, 3, RGBA(200, 170, 255, a), RGBA(110, 70, 240, a));
        seg_digit(x + 2 * dw + gap + colon, y, dw, dh, hm[3] - '0', top, bot);
        seg_digit(x + 3 * dw + 2 * gap + colon, y, dw, dh, hm[4] - '0', top, bot);
    }
    f32 clw = 4 * dw + 2 * gap + colon;
    saver_line(F_MED, x + clw / 2, y + dh + 30, date, 300, RGBA(220, 210, 255, 220));
    /* meteo (sous l'horloge) */
    f32 my = y + dh + 62;
    if (meteo_ok) {
        meteo_icon(x + 36, my + 34, 64, meteo_code);
        snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lrintf(meteo_temp));
        fun_shadow_text(F_LARGE, x + 118, my + 46, buf, 1.5f, RGBA(255, 255, 255, 240));
        gx_text1(F_MED, x + 160, my + 30, TR(meteo_text(meteo_code)), RGBA(220, 210, 255, 220));
        fit_text(F_SMALL, geo_name[0] ? geo_name : city, 150, buf, sizeof(buf));
        gx_text1(F_SMALL, x + 160, my + 50, buf, RGBA(170, 155, 225, 200));
        if (ndays > 1) {                                     /* demain */
            snprintf(buf, sizeof(buf), "%s %d\xC2\xB0 / %d\xC2\xB0", TR("Demain"), day_min[1], day_max[1]);
            gx_text1(F_SMALL, x + 4, my + 86, buf, RGBA(170, 155, 225, 200));
        }
    } else if (city[0]) gx_text1(F_SMALL, x, my + 30, TR("M\xC3\xA9t\xC3\xA9o en cours de chargement..."), RGBA(170, 155, 225, 200));
    else gx_text1(F_SMALL, x, my + 30, TR("M\xC3\xA9t\xC3\xA9o : choisis ta ville (onglet INFOS)"), RGBA(170, 155, 225, 200));
    /* pochette et titre (a droite) */
    const f32 sz = 180;
    f32 bx = cx + 70, by = cy - 120;
    if (saver_has_cover()) {
        gx_set_cover();
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(UX(bx), by);           GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 0.0f);
        GX_Position2f32(UX(bx + sz), by);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 0.0f);
        GX_Position2f32(UX(bx + sz), by + sz); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(1.0f, 1.0f);
        GX_Position2f32(UX(bx), by + sz);      GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f);
        GX_End();
        gx_rrect_stroke(bx, by, sz, sz, 2, 1.2f, RGBA(124, 80, 240, 200));
    } else draw_logo_cube(bx + sz / 2, by + sz / 2, 56);
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    if (ti[0]) {
        saver_line(F_MED, bx + sz / 2, by + sz + 30, ti, 250, RGBA(240, 232, 255, 230));
        saver_line(F_SMALL, bx + sz / 2, by + sz + 52, sub, 250, RGBA(170, 155, 225, 200));
    }
    ux_restore(uz);
}

/* =================== sauvegarde / restauration des reglages =================== */
#define BK_FILE GCD_REG "/gc-mp3-sauvegarde.txt"
static const char *const bk_files[] = { "gc-mp3.txt", "reglages/radio.txt", "reglages/favoris.txt", "reglages/radio-favoris.txt",
    "reglages/podcasts.txt", "podcasts/auto.txt", "podcasts/positions.txt", "mes-parties/gc-mp3-jeux.txt", "mes-parties/jeux_stats.txt",
    "mes-parties/jeux_recents.txt", "bibliotheque/gc-mp3-ecoutes.txt", "bibliotheque/gc-mp3-titres.txt", "reglages/volumes.txt",
    "radio/historique.txt", "mes-parties/jeux-favoris.txt", "mes-parties/succes.txt", "mes-parties/soiree.txt",
    "reglages/compagnon.txt", "bibliotheque/gc-mp3-notes.txt", "mes-parties/compat.txt", "web/settings.ini", "web/bookmarks.txt", NULL };   /* (v182 : ranges ; v238 : compagnon, notes, compatibilite, Cube Browser. Jamais les cles : assistant.txt, identifiants.txt, cookies) */
static int bk_add(FILE *o, const char *rel) {                    /* "@@@ fichier <nom> <taille>\n" + contenu */
    char p[NAME_LEN];
    snprintf(p, sizeof(p), GCD "/%s", rel);
    long len = 0;
    unsigned char *d = read_file(p, 4L * 1024 * 1024, &len);
    if (!d) return 0;
    fprintf(o, "@@@ fichier %s %ld\n", rel, len);
    int ok = fwrite(d, 1, len, o) == (size_t)len;
    fputc('\n', o);
    free(d);
    service_audio();
    return ok ? 1 : -1;
}
static int bk_save(char *msg, int cap) {
    if (!sd_ok) { snprintf(msg, cap, "Pas de carte SD"); return 0; }
    if (settings_dirty) save_settings();
    cfgw_wait();                                          /* v194 : (enregistrement de fond fini) */
    if (stats_dirty) stats_save();
    FILE *o = fopen(BK_FILE ".part", "wb");
    if (!o) { snprintf(msg, cap, "\xC3\x89" "criture sur la carte SD impossible"); return 0; }
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    fprintf(o, "# GC Station %s : sauvegarde de tous les reglages", GCMP3_VERSION);
    if (tm && tm->tm_year >= 100) fprintf(o, " (%04d-%02d-%02d %02d:%02d)", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
    fprintf(o, "\n# Restauration : START > Outils > Restaurer les r\xC3\xA9glages. Ne pas modifier les lignes @@@.\n");
    int n = 0, bad = 0;
    for (int i = 0; bk_files[i]; i++) { int r = bk_add(o, bk_files[i]); if (r > 0) n++; if (r < 0) bad = 1; }
    DIR *d = opendir(PL_DIR);                                      /* listes de lecture */
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        int l = (int)strlen(e->d_name);
        if (l < 5 || l > 120 || strcasecmp(e->d_name + l - 4, ".m3u")) continue;
        char rel[160];
        snprintf(rel, sizeof(rel), "listes/%s", e->d_name);
        int r = bk_add(o, rel);
        if (r > 0) n++;
        if (r < 0) bad = 1;
    }
    if (d) closedir(d);
    d = opendir(GCD_PROF);                                         /* v238 : profils (profils/<nom>/profil.txt) */
    while (d && (e = readdir(d)) != NULL) {
        int l = (int)strlen(e->d_name);
        if (e->d_name[0] == '.' || l > 100 || strchr(e->d_name, '/')) continue;
        char rel[160], pp[NAME_LEN];
        snprintf(rel, sizeof(rel), "profils/%s/profil.txt", e->d_name);
        snprintf(pp, sizeof(pp), GCD "/%s", rel);
        struct stat st;
        if (stat(pp, &st) != 0) continue;
        int r = bk_add(o, rel);
        if (r > 0) n++;
        if (r < 0) bad = 1;
    }
    if (d) closedir(d);
    fprintf(o, "@@@ fin\n");
    if (fclose(o) != 0) bad = 1;
    if (bad || !n) { remove(BK_FILE ".part"); snprintf(msg, cap, "\xC3\x89" "chec de la sauvegarde"); return 0; }
    remove(BK_FILE);
    rename(BK_FILE ".part", BK_FILE);
    snprintf(msg, cap, "%d fichiers sauvegard\xC3\xA9s dans SD:/gc-mp3/reglages/gc-mp3-sauvegarde.txt", n);
    return n;
}
/* nom de fichier acceptable : dans gc-mp3, sans "..", lettres simples */
static int bk_name_ok(const char *rel) {
    if (!rel[0] || strlen(rel) > 140 || strstr(rel, "..") || rel[0] == '/' || strchr(rel, '\\') || strchr(rel, ':')) return 0;
    for (int i = 0; bk_files[i]; i++) if (!strcmp(rel, bk_files[i])) return 1;
    if (!strncmp(rel, "listes/", 7) && !strchr(rel + 7, '/')) { int l = (int)strlen(rel); return l > 11 && !strcasecmp(rel + l - 4, ".m3u"); }
    if (!strncmp(rel, "profils/", 8)) {                              /* v238 : profils/<nom>/profil.txt */
        const char *nm = rel + 8, *sl = strchr(nm, '/');
        return sl && sl > nm && nm[0] != '.' && !strcmp(sl, "/profil.txt");
    }
    return 0;
}
/* lit la sauvegarde ; dry = 1 : verifie seulement */
static int bk_restore_buf(const char *b, long len, int dry, char *msg, int cap) {
    long p = 0;
    int n = 0;
    while (p < len) {
        const char *nl = memchr(b + p, '\n', len - p);
        long le = nl ? nl - b : len;
        if (le - p > 12 && !memcmp(b + p, "@@@ fichier ", 12)) {
            char line[200], rel[160];
            long ll = le - p < (long)sizeof(line) - 1 ? le - p : (long)sizeof(line) - 1;
            memcpy(line, b + p, ll); line[ll] = 0;
            char *sp = strrchr(line + 12, ' ');
            if (!sp) { snprintf(msg, cap, "Sauvegarde ab\xC3\xAEm\xC3\xA9" "e"); return -1; }
            long sz = atol(sp + 1);
            *sp = 0;
            snprintf(rel, sizeof(rel), "%s", gcd_newrel(line + 12));   /* (v182 : sauvegarde d'avant le rangement) */
            long start = le + 1;
            if (sz < 0 || sz > len || start + sz > len) { snprintf(msg, cap, "Sauvegarde ab\xC3\xAEm\xC3\xA9" "e (%s)", rel); return -1; }
            if (bk_name_ok(rel) && !dry) {
                char path[NAME_LEN], part[NAME_LEN + 8];
                snprintf(path, sizeof(path), GCD "/%s", rel);
                if (strchr(rel, '/')) {                           /* (sous-dossier) */
                    char dir[NAME_LEN];
                    snprintf(dir, sizeof(dir), "%s", path);
                    char *sl = strrchr(dir, '/');
                    if (sl) *sl = 0;
                    for (char *q = dir + strlen(GCD) + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(dir, 0777); *q = '/'; }   /* (v238 : profils/<nom>) */
                    mkdir(dir, 0777);
                }
                snprintf(part, sizeof(part), "%s.part", path);
                FILE *f = fopen(part, "wb");
                int ok = f && fwrite(b + start, 1, sz, f) == (size_t)sz;
                if (f && fclose(f) != 0) ok = 0;
                if (ok) { remove(path); rename(part, path); n++; } else remove(part);
                service_audio();
            } else if (bk_name_ok(rel)) n++;
            p = start + sz;
            if (p < len && b[p] == '\n') p++;
            continue;
        }
        if (le - p >= 7 && !memcmp(b + p, "@@@ fin", 7)) break;
        p = le + 1;
    }
    if (!n) { snprintf(msg, cap, "Aucun r\xC3\xA9glage dans cette sauvegarde"); return 0; }
    snprintf(msg, cap, dry ? "%d fichiers dans la sauvegarde" : "%d fichiers restaur\xC3\xA9s : red\xC3\xA9marre GC Station pour tout reprendre", n);
    return n;
}
static int bk_restore(char *msg, int cap) {
    long len = 0;
    char *b = sd_ok ? (char *)read_file(BK_FILE, 32L * 1024 * 1024, &len) : NULL;
    if (!b) { snprintf(msg, cap, "Pas de sauvegarde : SD:/gc-mp3/reglages/gc-mp3-sauvegarde.txt"); return 0; }
    int r = bk_restore_buf(b, len, 1, msg, cap);              /* (tout est verifie avant d'ecrire) */
    if (r > 0) r = bk_restore_buf(b, len, 0, msg, cap);
    free(b);
    if (r > 0) { load_settings(); eq_setup(); settings_dirty = 0; snprintf(msg, cap, "%d fichiers restaur\xC3\xA9s (Cube Browser : au prochain d\xC3\xA9marrage)", r); }
    return r;
}

/* =================== cartes memoire virtuelles (Resident) =================== */
/* Image brute d'une carte GameCube : blocs de 8 Ko ; 0 = en-tete, 1 et 2 = repertoire (2 copies),
   3 et 4 = table des blocs (2 copies), puis les donnees. La copie a jour = celle dont le compteur
   est le plus grand (et la somme de controle juste). */
#define VC_B 8192
typedef struct { int slot; u8 de[64]; char title[64], sub[64]; int blocks; } vc_ent;
static char vc_files[VC_MAXF][64];
static int vc_nf = 0, vc_cur = 0;                     /* fichiers de cartes, carte affichee */
static u8 *vc_sys = NULL;                             /* les 5 premiers blocs de la carte affichee */
static int vc_ok = 0, vc_blank = 0, vc_dir = 1, vc_bat = 3;   /* (copies a jour) */
static u32 vc_size = 0;
static vc_ent vc_e[127];
static int vc_n = 0, vc_sel = 0, vc_top = 0, vc_pop = 0, vc_psel = 0, vc_pconfirm = 0;
static int vc_mode = 0;                               /* 0 liste, 1 .gci de la SD, 2 vraie carte (slot vc_rslot) */
static int vc_rslot = 0, vc_gsel = 0, vc_rsel = 0;
static char vc_why[96] = "";

static u16 vc_be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static void vc_put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void vc_sum(const u8 *b, int bytes, u16 *c1, u16 *c2) {
    u16 a = 0, x = 0;
    for (int i = 0; i + 1 < bytes; i += 2) { u16 w = vc_be16(b + i); a += w; x += (u16)(w ^ 0xFFFF); }
    if (a == 0xFFFF) a = 0;
    if (x == 0xFFFF) x = 0;
    *c1 = a; *c2 = x;
}
static int vc_dir_ok(const u8 *d) { u16 a, x; vc_sum(d, 0x1FFC, &a, &x); return a == vc_be16(d + 0x1FFC) && x == vc_be16(d + 0x1FFE); }
static int vc_bat_ok(const u8 *t) { u16 a, x; vc_sum(t + 4, 0x1FFC, &a, &x); return a == vc_be16(t) && x == vc_be16(t + 2); }
static int vc_hdr_ok(const u8 *h) { u16 a, x; vc_sum(h, 0x1FC, &a, &x); return a == vc_be16(h + 0x1FC) && x == vc_be16(h + 0x1FE); }
static const char *vc_path(int i, char *out, int cap) { snprintf(out, cap, NA_CARD_DIR "/%s", vc_files[i]); return out; }
static int vc_nblocks(void) { return (int)(vc_size / VC_B); }

static int vc_fcmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
/* v239 : carte d'un seul jeu (carte-A-GALE01.raw) : son titre a la place du nom du fichier */
static int vc_is_game_card(const char *f) {
    int l = (int)strlen(f);
    return l >= 16 && (!strncasecmp(f, "carte-A-", 8) || !strncasecmp(f, "carte-B-", 8)) && !strcasecmp(f + l - 4, ".raw") &&
           strcasecmp(f + 8, "EUR.raw") && strcasecmp(f + 8, "USA.raw") && strcasecmp(f + 8, "JAP.raw");
}
static const char *vc_label(const char *f, char *out, int cap) {
    snprintf(out, cap, "%s", f);
    if (!vc_is_game_card(f)) return out;
    char id[8]; int n = 0;
    for (const char *q = f + 8; *q && *q != '.' && n < 6; q++) id[n++] = *q;
    id[n] = 0;
    static char last_f[64], last_t[96];                 /* (dessine a chaque image : le dernier titre trouve est garde) */
    if (!strcmp(last_f, f)) { snprintf(out, cap, "%s", last_t[0] ? last_t : f); return out; }
    char title[80] = "";
    for (int k = 0; k < gl_n && !title[0]; k++) if (!strncasecmp(gl[k].id, id, n) && gl[k].title[0]) snprintf(title, sizeof(title), "%s", gl[k].title);
    if (!title[0] && sd_ok) {                           /* (liste des jeux pas encore chargee : celle gardee sur la carte) */
        FILE *lf = fopen(GL_LIST, "r");
        static char line[640];
        while (lf && !title[0] && fgets(line, sizeof(line), lf)) {
            char *c[6]; int nc = 0;
            c[nc++] = line;
            for (char *q = line; *q && nc < 6; q++) if (*q == '\t') { *q = 0; c[nc++] = q + 1; }
            if (nc >= 6 && !strncasecmp(c[2], id, n)) { char *e = strchr(c[5], '\t'); if (e) *e = 0; e = strchr(c[5], '\n'); if (e) *e = 0; snprintf(title, sizeof(title), "%s", c[5]); }
        }
        if (lf) fclose(lf);
    }
    snprintf(last_f, sizeof(last_f), "%s", f);
    last_t[0] = 0;
    if (title[0]) snprintf(last_t, sizeof(last_t), "%s%s", title, f[6] == 'B' ? " (slot B)" : "");
    snprintf(out, cap, "%s", last_t[0] ? last_t : f);
    return out;
}
static void vc_scan(void) {
    vc_nf = 0;
    DIR *d = sd_ok ? opendir(NA_CARD_DIR) : NULL;
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL && vc_nf < VC_MAXF) {
        int l = (int)strlen(e->d_name);
        if (l < 5 || l >= 60 || strcasecmp(e->d_name + l - 4, ".raw")) continue;
        snprintf(vc_files[vc_nf++], sizeof(vc_files[0]), "%s", e->d_name);
    }
    if (d) closedir(d);
    int found = 0;
    for (int i = 0; i < vc_nf; i++) if (!strcasecmp(vc_files[i], vc_active)) found = 1;
    if (!found && vc_nf < VC_MAXF) snprintf(vc_files[vc_nf++], sizeof(vc_files[0]), "%s", vc_active);   /* (pas encore creee) */
    qsort(vc_files, vc_nf, sizeof(vc_files[0]), vc_fcmp);
    if (vc_cur >= vc_nf) vc_cur = 0;
}
static void vc_latin(const u8 *s, int n, char *out, int cap) { gl_latin(s, n, out, cap); }
/* lit les sauvegardes de la carte vc_cur (repertoire + table des blocs) */
static int vc_load(void) {
    vc_ok = 0; vc_blank = 0; vc_n = 0; vc_why[0] = 0; vc_size = 0;
    if (!vc_sys) vc_sys = (u8 *)malloc(5 * VC_B);
    if (!vc_sys || !vc_nf) { snprintf(vc_why, sizeof(vc_why), "Pas de carte virtuelle"); return 0; }
    char p[NAME_LEN];
    FILE *f = fopen(vc_path(vc_cur, p, sizeof(p)), "rb");
    if (!f) { snprintf(vc_why, sizeof(vc_why), "Pas encore cr\xC3\xA9\xC3\xA9" "e (Pulse la cr\xC3\xA9" "e au premier jeu)"); return 0; }
    struct stat sb;
    if (fstat(fileno(f), &sb) != 0 || sb.st_size < 0x80000 || sb.st_size > 0x1000000 || (sb.st_size & (sb.st_size - 1))) {
        fclose(f); snprintf(vc_why, sizeof(vc_why), "Ce fichier n'est pas une image de carte (512 Ko \xC3\xA0 16 Mo)"); return 0;
    }
    vc_size = (u32)sb.st_size;
    int rd = fread(vc_sys, 1, 5 * VC_B, f) == 5 * VC_B;
    fclose(f);
    if (!rd) { snprintf(vc_why, sizeof(vc_why), "Lecture impossible"); return 0; }
    int blank = 1;
    for (int i = 0; i < 0x200 && blank; i++) if (vc_sys[i] != 0xFF) blank = 0;
    if (blank) { vc_blank = 1; snprintf(vc_why, sizeof(vc_why), "Carte vierge (pas encore format\xC3\xA9" "e)"); return 0; }
    const u8 *d1 = vc_sys + VC_B, *d2 = vc_sys + 2 * VC_B, *t1 = vc_sys + 3 * VC_B, *t2 = vc_sys + 4 * VC_B;
    int o1 = vc_dir_ok(d1), o2 = vc_dir_ok(d2), b1 = vc_bat_ok(t1), b2 = vc_bat_ok(t2);
    if (!(o1 || o2) || !(b1 || b2)) { snprintf(vc_why, sizeof(vc_why), "Carte ab\xC3\xAEm\xC3\xA9" "e (r\xC3\xA9pertoire illisible)"); return 0; }
    vc_dir = o1 && (!o2 || (s16)(vc_be16(d1 + 0x1FFA) - vc_be16(d2 + 0x1FFA)) >= 0) ? 1 : 2;
    vc_bat = b1 && (!b2 || (s16)(vc_be16(t1 + 4) - vc_be16(t2 + 4)) >= 0) ? 3 : 4;
    const u8 *dir = vc_sys + vc_dir * VC_B;
    for (int i = 0; i < 127; i++) {
        const u8 *de = dir + i * 64;
        if (de[0] == 0xFF && de[1] == 0xFF && de[2] == 0xFF && de[3] == 0xFF) continue;
        vc_ent *v = &vc_e[vc_n++];
        memset(v, 0, sizeof(*v));
        v->slot = i;
        memcpy(v->de, de, 64);
        v->blocks = vc_be16(de + 0x38);
        vc_latin(de + 8, 32, v->title, sizeof(v->title));
    }
    vc_ok = 1;
    return 1;
}
static u16 vc_map(int blk) { return vc_be16(vc_sys + vc_bat * VC_B + 0x0A + (blk - 5) * 2); }
static void vc_setmap(u8 *bat, int blk, u16 v) { vc_put16(bat + 0x0A + (blk - 5) * 2, v); }
/* lit les blocs d'une sauvegarde (dans l'ordre de la chaine) */
static u8 *vc_read_save(const vc_ent *v, long *len) {
    int n = v->blocks, nb = vc_nblocks();
    if (n <= 0 || n > nb - 5) return NULL;
    u8 *buf = (u8 *)malloc((long)n * VC_B);
    if (!buf) return NULL;
    char p[NAME_LEN];
    FILE *f = fopen(vc_path(vc_cur, p, sizeof(p)), "rb");
    if (!f) { free(buf); return NULL; }
    int blk = vc_be16(v->de + 0x36), ok = 1;
    for (int k = 0; k < n && ok; k++) {
        if (blk < 5 || blk >= nb) { ok = 0; break; }
        if (fseek(f, (long)blk * VC_B, SEEK_SET) != 0 || fread(buf + (long)k * VC_B, 1, VC_B, f) != VC_B) ok = 0;
        u16 nx = vc_map(blk);
        if (k < n - 1) { if (nx == 0xFFFF || nx == 0) ok = 0; blk = nx; }
        if ((k & 7) == 7) service_audio();
    }
    fclose(f);
    if (!ok) { free(buf); return NULL; }
    *len = (long)n * VC_B;
    return buf;
}
/* commentaire de la sauvegarde (2 lignes de 32) */
static void vc_meta(vc_ent *v) {
    u32 cmt = (u32)v->de[0x3C] << 24 | (u32)v->de[0x3D] << 16 | (u32)v->de[0x3E] << 8 | v->de[0x3F];
    if (cmt == 0xFFFFFFFFu || cmt + 64 > (u32)v->blocks * VC_B) return;
    long len = 0;
    u8 *d = vc_read_save(v, &len);
    if (!d) { snprintf(v->sub, sizeof(v->sub), "(illisible)"); return; }
    char t[64];
    vc_latin(d + cmt, 32, t, sizeof(t));
    if (t[0]) snprintf(v->title, sizeof(v->title), "%s", t);
    vc_latin(d + cmt + 32, 32, v->sub, sizeof(v->sub));
    free(d);
}
/* ecrit les 5 blocs systeme (repertoire et table : les 2 copies, compteurs +1) */
static int vc_commit(const u8 *dir, const u8 *bat) {
    static u8 nd[VC_B], nt[VC_B];
    memcpy(nd, dir, VC_B); memcpy(nt, bat, VC_B);
    u16 dc = (u16)(vc_be16(vc_sys + vc_dir * VC_B + 0x1FFA) + 1), bc = (u16)(vc_be16(vc_sys + vc_bat * VC_B + 4) + 1);
    vc_put16(nd + 0x1FFA, dc);
    u16 a, x;
    vc_sum(nd, 0x1FFC, &a, &x); vc_put16(nd + 0x1FFC, a); vc_put16(nd + 0x1FFE, x);
    vc_put16(nt + 4, bc);
    vc_sum(nt + 4, 0x1FFC, &a, &x); vc_put16(nt, a); vc_put16(nt + 2, x);
    char p[NAME_LEN];
    FILE *f = fopen(vc_path(vc_cur, p, sizeof(p)), "r+b");
    if (!f) return 0;
    int ok = 1;
    for (int c = 0; c < 2 && ok; c++) {
        if (fseek(f, (long)(1 + c) * VC_B, SEEK_SET) != 0 || fwrite(nd, 1, VC_B, f) != VC_B) ok = 0;
        if (fseek(f, (long)(3 + c) * VC_B, SEEK_SET) != 0 || fwrite(nt, 1, VC_B, f) != VC_B) ok = 0;
    }
    if (fclose(f) != 0) ok = 0;
    vc_load();
    return ok;
}
#ifdef GEKKO
extern syssram *__SYS_LockSram(void);
extern syssramex *__SYS_LockSramEx(void);
extern u32 __SYS_UnlockSram(u32 write);
extern u32 __SYS_UnlockSramEx(u32 write);
#endif
/* formate la carte affichee (comme le fait la GameCube : numero de serie tire de la SRAM du slot A) */
static int vc_format(void) {
    if (!vc_sys || !vc_size) return 0;
    u8 *h = vc_sys;
    memset(vc_sys, 0xFF, 3 * VC_B);
    memset(vc_sys + 3 * VC_B, 0, 2 * VC_B);
    u64 tm = gettime(), rnd = tm;
    u8 fid[12] = { 0 };
    u32 bias = 0, lang = 0, enc = 0;
#ifdef GEKKO
    syssramex *sx = __SYS_LockSramEx();
    memcpy(fid, sx->flash_id[0], 12);
    __SYS_UnlockSramEx(0);
    syssram *sr = __SYS_LockSram();
    bias = sr->counter_bias; lang = sr->lang;
    __SYS_UnlockSram(0);
    enc = SYS_GetFontEncoding();
#endif
    for (int i = 0; i < 12; i++) {
        rnd = (rnd * 0x41c64e6dULL + 0x3039ULL) >> 16;
        h[i] = (u8)(fid[i] + (u32)rnd);
        rnd = ((rnd * 0x41c64e6dULL + 0x3039ULL) >> 16) & 0x7FFF;
    }
    for (int i = 0; i < 8; i++) h[12 + i] = (u8)(tm >> (56 - 8 * i));
    h[0x14] = (u8)(bias >> 24); h[0x15] = (u8)(bias >> 16); h[0x16] = (u8)(bias >> 8); h[0x17] = (u8)bias;
    h[0x18] = 0; h[0x19] = 0; h[0x1A] = 0; h[0x1B] = (u8)lang;
    memset(h + 0x1C, 0, 4);
    vc_put16(h + 0x20, 0);
    vc_put16(h + 0x22, (u16)(vc_size >> 17));
    vc_put16(h + 0x24, (u16)enc);
    u16 a, x;
    vc_sum(h, 0x1FC, &a, &x); vc_put16(h + 0x1FC, a); vc_put16(h + 0x1FE, x);
    for (int c = 0; c < 2; c++) {
        u8 *d = vc_sys + (1 + c) * VC_B;
        vc_sum(d, 0x1FFC, &a, &x); vc_put16(d + 0x1FFC, a); vc_put16(d + 0x1FFE, x);
        u8 *t = vc_sys + (3 + c) * VC_B;
        vc_put16(t + 4, (u16)c); vc_put16(t + 6, (u16)(vc_nblocks() - 5)); vc_put16(t + 8, 4);
        vc_sum(t + 4, 0x1FFC, &a, &x); vc_put16(t, a); vc_put16(t + 2, x);
    }
    char p[NAME_LEN];
    FILE *f = fopen(vc_path(vc_cur, p, sizeof(p)), "r+b");
    if (!f) return 0;
    int ok = fwrite(vc_sys, 1, 5 * VC_B, f) == 5 * VC_B;
    if (fclose(f) != 0) ok = 0;
    vc_load();
    return ok && vc_ok;
}
static int vc_free_blocks(void) { return vc_be16(vc_sys + vc_bat * VC_B + 6); }
/* .gci (64 octets + donnees) -> carte affichee. Retourne un message d'erreur ou NULL */
static const char *vc_import(const u8 *g, long len) {
    if (!vc_ok) return vc_blank ? "Formate d'abord la carte (X)" : "Carte illisible";
    if (len < 64 + VC_B || (len - 64) % VC_B) return "Ce fichier n'est pas une sauvegarde GameCube valide";
    if (g[0] == 0xFF && g[1] == 0xFF) return "Ce fichier n'est pas une sauvegarde GameCube valide";
    int n = (int)((len - 64) / VC_B), nb = vc_nblocks();
    for (int i = 0; i < vc_n; i++) if (!memcmp(vc_e[i].de, g, 6) && !memcmp(vc_e[i].de + 8, g + 8, 32)) return "Cette sauvegarde existe d\xC3\xA9j\xC3\xA0 sur la carte (supprime-la d'abord)";
    if (vc_n >= 127) return "La carte a d\xC3\xA9j\xC3\xA0 127 sauvegardes";
    if (n > vc_free_blocks()) return "Pas assez de place sur la carte";
    static u8 dir[VC_B], bat[VC_B];
    memcpy(dir, vc_sys + vc_dir * VC_B, VC_B);
    memcpy(bat, vc_sys + vc_bat * VC_B, VC_B);
    int slot = -1;
    for (int i = 0; i < 127 && slot < 0; i++) if (dir[i * 64] == 0xFF && dir[i * 64 + 1] == 0xFF && dir[i * 64 + 2] == 0xFF && dir[i * 64 + 3] == 0xFF) slot = i;
    if (slot < 0) return "La carte a d\xC3\xA9j\xC3\xA0 127 sauvegardes";
    int last = vc_be16(bat + 8), first = -1, prev = -1, got = 0;
    static u16 chain[4096];
    for (int k = 0; k < nb - 5 && got < n; k++) {             /* blocs libres, apres le dernier donne */
        int b = 5 + (last - 5 + 1 + k + (nb - 5)) % (nb - 5);
        if (vc_be16(bat + 0x0A + (b - 5) * 2) != 0) continue;
        chain[got++] = (u16)b;
    }
    if (got < n) return "Pas assez de place sur la carte";
    char p[NAME_LEN];
    FILE *f = fopen(vc_path(vc_cur, p, sizeof(p)), "r+b");
    if (!f) return "\xC3\x89" "criture sur la carte SD impossible";
    int ok = 1;
    for (int k = 0; k < n && ok; k++) {
        if (fseek(f, (long)chain[k] * VC_B, SEEK_SET) != 0 || fwrite(g + 64 + (long)k * VC_B, 1, VC_B, f) != VC_B) ok = 0;
        if ((k & 7) == 7) service_audio();
    }
    if (fclose(f) != 0) ok = 0;
    if (!ok) return "\xC3\x89" "criture sur la carte SD impossible";
    for (int k = 0; k < n; k++) {
        vc_setmap(bat, chain[k], k == n - 1 ? 0xFFFF : chain[k + 1]);
        if (first < 0) first = chain[k];
        prev = chain[k];
    }
    vc_put16(bat + 6, (u16)(vc_be16(bat + 6) - n));
    vc_put16(bat + 8, (u16)prev);
    u8 *de = dir + slot * 64;
    memcpy(de, g, 64);
    vc_put16(de + 0x36, (u16)first);
    vc_put16(de + 0x38, (u16)n);
    if (!vc_commit(dir, bat)) return "\xC3\x89" "criture sur la carte SD impossible";
    return NULL;
}
static const char *vc_delete(int i) {
    if (!vc_ok || i < 0 || i >= vc_n) return "Sauvegarde introuvable";
    static u8 dir[VC_B], bat[VC_B];
    memcpy(dir, vc_sys + vc_dir * VC_B, VC_B);
    memcpy(bat, vc_sys + vc_bat * VC_B, VC_B);
    int blk = vc_be16(vc_e[i].de + 0x36), n = vc_e[i].blocks, nb = vc_nblocks(), freed = 0;
    for (int k = 0; k < n && blk >= 5 && blk < nb; k++) {
        u16 nx = vc_be16(bat + 0x0A + (blk - 5) * 2);
        if (nx == 0) break;
        vc_setmap(bat, blk, 0);
        freed++;
        if (nx == 0xFFFF) break;
        blk = nx;
    }
    vc_put16(bat + 6, (u16)(vc_be16(bat + 6) + freed));
    memset(dir + vc_e[i].slot * 64, 0xFF, 64);
    return vc_commit(dir, bat) ? NULL : "\xC3\x89" "criture sur la carte SD impossible";
}
static u8 *vc_export(int i, long *glen) {
    long n = 0;
    u8 *d = vc_read_save(&vc_e[i], &n);
    if (!d) return NULL;
    u8 *g = (u8 *)malloc(64 + n);
    if (!g) { free(d); return NULL; }
    memcpy(g, vc_e[i].de, 64);
    memcpy(g + 64, d, n);
    free(d);
    *glen = 64 + n;
    return g;
}
/* nouvelle carte : carte-2.raw, carte-3.raw... (2 Mo, 251 blocs, formatee) */
static int vc_new(void) {
    char name[64], p[NAME_LEN];
    struct stat sb;
    mkdir(NA_CARD_DIR, 0777);
    int k = 2;
    for (; k < 100; k++) { snprintf(name, sizeof(name), "carte-%d.raw", k); snprintf(p, sizeof(p), NA_CARD_DIR "/%s", name); if (stat(p, &sb) != 0) break; }
    FILE *f = fopen(p, "wb");
    if (!f) return 0;
    static u8 ff[32768];
    memset(ff, 0xFF, sizeof(ff));
    int ok = 1;
    for (u32 o = 0; ok && o < (2u << 20); o += sizeof(ff)) { if (fwrite(ff, 1, sizeof(ff), f) != sizeof(ff)) ok = 0; service_audio(); }
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(p); return 0; }
    vc_scan();
    for (int i = 0; i < vc_nf; i++) if (!strcmp(vc_files[i], name)) vc_cur = i;
    vc_load();
    return vc_format();
}
static void vc_open(void) {
    tl_screen = TL_VCARD; vc_mode = 0; vc_pop = 0; vc_sel = 0; vc_top = 0; vc_hub = 0;
    vc_scan();
    for (int i = 0; i < vc_nf; i++) if (!strcasecmp(vc_files[i], vc_active)) vc_cur = i;
    vc_load();
}
/* v145 : la carte virtuelle de Pulse chargee (pour y copier) ; rend NULL ou la raison */
static const char *vc_load_active(void) {
    vc_scan();
    for (int i = 0; i < vc_nf; i++) if (!strcasecmp(vc_files[i], vc_active)) vc_cur = i;
    if (vc_load()) return NULL;
    if (vc_blank && vc_format() && vc_load()) return NULL;   /* (carte jamais servie : rien a perdre, formatee ici) */
    if (vc_blank) return "Carte virtuelle pas format\xC3\xA9" "e : START > Outils > Cartes m\xC3\xA9moire virtuelles, X";
    return vc_why[0] ? vc_why : "Carte virtuelle illisible";
}
enum { VCP_SD, VCP_REALA, VCP_REALB, VCP_QR, VCP_ALLA, VCP_ALLB, VCP_ALLSD, VCP_ALLQR, VCP_DEL, VCP_COUNT };
static const char *const vcp_names[VCP_COUNT] = { "Copier sur la carte SD (.gci)", "Copier vers la vraie carte (slot A)", "Copier vers la vraie carte (slot B)",
    "Envoyer au t\xC3\xA9l\xC3\xA9phone (QR code)",
    "Tout copier vers la vraie carte (slot A)", "Tout copier vers la vraie carte (slot B)", "Tout copier sur la carte SD (.gci)",
    "Tout envoyer au t\xC3\xA9l\xC3\xA9phone (QR code)", "Supprimer" };
/* v147 : partage par QR code (une sauvegarde, ou toutes : SD:/gci/sauvegardes) */
static void vc_to_qr(int i, int all) {
    if (!all) { long n = 0; u8 *g = vc_export(i, &n); if (!g) { tl_note("Lecture de la sauvegarde impossible"); return; } mc_qr_one(g, n); free(g); return; }
    mc_qr_clear();
    int ok = 0;
    for (int k = 0; k < vc_n; k++) { long n = 0; u8 *g = vc_export(k, &n); if (g && mc_gci_write_in(MC_QR_DIR, g, n, NULL, 0)) ok++; free(g); }
    if (!ok) { tl_note("Aucune sauvegarde copi\xC3\xA9" "e"); return; }
    share_open(MC_QR_DIR, 1);
}
static void vc_to_sd(int i) {
    long n = 0;
    u8 *g = vc_export(i, &n);
    if (!g) { tl_note("Lecture de la sauvegarde impossible"); return; }
    char nm[96];
    int ok = mc_gci_write(g, n, nm, sizeof(nm));
    free(g);
    if (!ok) { tl_note("\xC3\x89" "criture sur la carte SD impossible"); return; }
    snprintf(tl_msg, sizeof(tl_msg), "Copi\xC3\xA9" "e : SD:/gci/%s", nm); tl_msg_t = now_ms();
}
/* v145 : toutes les sauvegardes de la carte virtuelle -> vraie carte (slot) ou SD:/gci (slot < 0) */
static void vc_all_to(int slot) {
    if (slot >= 0 && !mc_mount(slot)) { tl_note(mc_errtxt(mc_err)); return; }
    int ok = 0, dup = 0, bad = 0;
    const char *err = NULL;
    for (int i = 0; i < vc_n; i++) {
        tl_status(slot >= 0 ? "Copie sur la carte m\xC3\xA9moire..." : "Copie sur la carte SD...");
        long n = 0;
        u8 *g = vc_export(i, &n);
        if (!g) { bad++; if (!err) err = "lecture impossible"; continue; }
        if (slot < 0) { if (mc_gci_write(g, n, NULL, 0)) ok++; else { bad++; if (!err) err = "\xC3\xA9" "criture"; } }
        else {
            int r = mc_import(slot, g, n);
            if (r >= 0) ok++;
            else if (r == CARD_ERROR_EXIST) dup++;
            else { bad++; if (!err) err = mc_errtxt(r); }
        }
        free(g);
    }
    gl_busy[0] = 0;
    if (slot >= 0) mc_unmount();
    mc_bilan(ok, dup, bad, err);
}
static void vc_to_real(int i, int slot) {
    long n = 0;
    u8 *g = vc_export(i, &n);
    if (!g) { tl_note("Lecture de la sauvegarde impossible"); return; }
    if (!mc_mount(slot)) { free(g); tl_note(mc_errtxt(mc_err)); return; }
    tl_status("Copie sur la carte m\xC3\xA9moire...");
    int r = mc_import(slot, g, n);
    gl_busy[0] = 0;
    free(g);
    mc_unmount();
    if (r < 0) tl_note(mc_errtxt(r));
    else tl_note(slot ? "Copi\xC3\xA9" "e sur la carte du slot B" : "Copi\xC3\xA9" "e sur la carte du slot A");
}
static void vc_input(u32 down, u32 nav, int fire) {
    int rows = vc_n + 3;
    if (vc_hub && !vc_pop && vc_mode == 0) {                   /* v145 : vue Cartes de l'onglet JEUX */
        if (gm_hub_input()) return;
        if (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) { vc_hub = 0; mc_slot = (down & PAD_TRIGGER_R) ? 0 : 1; tl_screen = TL_CARDS; mc_gci = 0; mc_pop = 0; mc_sel = 0; mc_top = 0; mc_mount(mc_slot); return; }
        if (down & PAD_BUTTON_B) { vc_hub = 0; tl_screen = TL_NONE; return; }
    }
    if (vc_pop) {
        if (fire && (nav & PAD_BUTTON_DOWN)) { vc_psel = (vc_psel + 1) % VCP_COUNT; vc_pconfirm = 0; }
        if (fire && (nav & PAD_BUTTON_UP)) { vc_psel = (vc_psel + VCP_COUNT - 1) % VCP_COUNT; vc_pconfirm = 0; }
        if (down & PAD_BUTTON_A) {
            int i = vc_sel - 3;
            if (i < 0 || i >= vc_n) { vc_pop = 0; return; }
            if (vc_psel == VCP_DEL && !vc_pconfirm) { vc_pconfirm = 1; return; }
            vc_pop = 0; vc_pconfirm = 0;
            if (vc_psel == VCP_SD) vc_to_sd(i);
            else if (vc_psel == VCP_REALA || vc_psel == VCP_REALB) vc_to_real(i, vc_psel == VCP_REALB);
            else if (vc_psel == VCP_ALLA || vc_psel == VCP_ALLB) vc_all_to(vc_psel == VCP_ALLB);
            else if (vc_psel == VCP_ALLSD) vc_all_to(-1);
            else if (vc_psel == VCP_QR || vc_psel == VCP_ALLQR) vc_to_qr(i, vc_psel == VCP_ALLQR);   /* v147 */
            else { const char *e = vc_delete(i); tl_note(e ? e : "Sauvegarde supprim\xC3\xA9" "e"); if (vc_sel >= vc_n + 3) vc_sel = vc_n + 2; }
        }
        if (down & (PAD_BUTTON_B | PAD_BUTTON_X)) { vc_pop = 0; vc_pconfirm = 0; }
        return;
    }
    if (vc_mode == 1) {                                        /* .gci de la SD */
        if (fire && (nav & PAD_BUTTON_DOWN) && vc_gsel < mc_gn - 1) vc_gsel++;
        if (fire && (nav & PAD_BUTTON_UP) && vc_gsel > 0) vc_gsel--;
        if ((down & PAD_BUTTON_A) && mc_gn) {
            char p[NAME_LEN];
            snprintf(p, sizeof(p), "%s/%s", MC_DIR, mc_gfiles[vc_gsel]);
            long len = 0;
            u8 *d = read_file(p, 8L * 1024 * 1024 + 0x200, &len);
            const char *dot = strrchr(p, '.');
            long skip = dot && !strcasecmp(dot, ".gcs") ? 0x110 : dot && !strcasecmp(dot, ".sav") ? 0x80 : 0;
            if (d && skip == 0x80 && len > skip + 64) for (int i = 0x06; i < 0x40; i += 2) { u8 t = d[skip + i]; d[skip + i] = d[skip + i + 1]; d[skip + i + 1] = t; }
            const char *e = !d ? "Lecture du fichier impossible" : len < skip + 64 + VC_B ? "Ce fichier n'est pas une sauvegarde GameCube valide" : vc_import(d + skip, len - skip);
            free(d);
            tl_note(e ? e : "Sauvegarde copi\xC3\xA9" "e sur la carte virtuelle");
            if (!e) vc_mode = 0;
        }
        if (down & PAD_BUTTON_B) vc_mode = 0;
        return;
    }
    if (vc_mode == 2) {                                        /* sauvegardes de la vraie carte */
        if (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) { vc_rslot = 1 - vc_rslot; vc_rsel = 0; mc_mount(vc_rslot); return; }
        if (fire && (nav & PAD_BUTTON_DOWN) && vc_rsel < mc_n - 1) vc_rsel++;
        if (fire && (nav & PAD_BUTTON_UP) && vc_rsel > 0) vc_rsel--;
        if ((down & PAD_BUTTON_A) && mc_mounted >= 0 && vc_rsel < mc_n) {
            tl_status("Copie sur la carte virtuelle...");
            long n = 0;
            u8 *g = mc_export(vc_rsel, &n);
            gl_busy[0] = 0;
            const char *e = g ? vc_import(g, n) : "Lecture de la sauvegarde impossible";
            free(g);
            tl_note(e ? e : "Sauvegarde copi\xC3\xA9" "e sur la carte virtuelle");
        }
        if (down & PAD_BUTTON_B) { mc_unmount(); vc_mode = 0; }
        return;
    }
    if (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) {              /* carte suivante / precedente */
        if (vc_nf > 1) { vc_cur = (vc_cur + ((down & PAD_TRIGGER_R) ? 1 : vc_nf - 1)) % vc_nf; vc_sel = 0; vc_top = 0; vc_load(); }
        return;
    }
    if (fire && (nav & PAD_BUTTON_DOWN) && vc_sel < rows - 1) vc_sel++;
    if (fire && (nav & PAD_BUTTON_UP) && vc_sel > 0) vc_sel--;
    if (fire && (nav & PAD_BUTTON_RIGHT)) { vc_sel += 7; if (vc_sel > rows - 1) vc_sel = rows - 1; }
    if (fire && (nav & PAD_BUTTON_LEFT)) { vc_sel -= 7; if (vc_sel < 0) vc_sel = 0; }
    if (down & PAD_BUTTON_A) {
        if (vc_sel == 0) { mc_gci_scan(); vc_gsel = 0; vc_mode = 1; if (!mc_gn) tl_note("Aucun fichier .gci dans SD:/gci"); }
        else if (vc_sel == 1) { vc_mode = 2; vc_rsel = 0; mc_mount(vc_rslot); }
        else if (vc_sel == 2) { tl_status("Cr\xC3\xA9" "ation de la carte..."); int ok = vc_new(); gl_busy[0] = 0; tl_note(ok ? "Nouvelle carte cr\xC3\xA9\xC3\xA9" "e (Y pour l'utiliser avec Pulse)" : "Cr\xC3\xA9" "ation impossible"); }
        else if (vc_ok) { vc_pop = 1; vc_psel = 0; vc_pconfirm = 0; }
    }
    if ((down & PAD_BUTTON_X) && (vc_blank || (!vc_ok && vc_size))) {                   /* formater */
        if (!vc_pconfirm) { vc_pconfirm = 1; tl_note("X encore une fois pour formater cette carte (tout est effac\xC3\xA9)"); }
        else { vc_pconfirm = 0; tl_note(vc_format() ? "Carte format\xC3\xA9" "e" : "Formatage impossible"); }
        return;
    }
    if ((down & PAD_BUTTON_Y) && vc_nf) {                      /* carte utilisee par Resident */
        snprintf(vc_active, sizeof(vc_active), "%s", vc_files[vc_cur]);
        settings_dirty = 1;
        tl_note("Cette carte sera la carte du slot A des jeux lanc\xC3\xA9s avec Pulse");
    }
    if (down & PAD_BUTTON_B) { vc_pconfirm = 0; tl_screen = TL_NONE; }
}
static void draw_vcard(void) {
    char buf[200], fv[200];
    if (vc_hub && tab == TAB_GAMES) { tl_frame("", NULL); gm_hub_bar(58, 54); mc_pills(2); }   /* v145 */
    else tl_frame("Cartes m\xC3\xA9moire virtuelles", NULL);
    if (vc_mode == 1) {
        gx_text1(F_SMALL, 58, 98, "Copier une sauvegarde de SD:/gci sur la carte virtuelle :", RGBA(200, 190, 240, 255));
        const int vis = 11;
        int start = vc_gsel - vis / 2; if (start > mc_gn - vis) start = mc_gn - vis; if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < mc_gn; i++) {
            int r = start + i; f32 ry = 108 + i * 24;
            gp_zone(52, ry, 536, 22, &vc_gsel, r, GPZ_SEL);
            if (r == vc_gsel) gx_rrect_fill(52, ry, 536, 22, 5, TL_SEL1, TL_SEL2);
            text_display(mc_gfiles[r], buf, sizeof(buf)); fit_text(F_MED, buf, 510, fv, sizeof(fv));
            gx_text1(F_MED, 64, ry + 16, fv, RGBA(235, 225, 255, 255));
        }
        tl_msgbox();
        static const char *const k[] = { "A", "B" }, *const l[] = { "COPIER", "RETOUR" };
        tl_legend(k, l, 2);
        return;
    }
    if (vc_mode == 2) {
        snprintf(buf, sizeof(buf), "Vraie carte du slot %c : choisis la sauvegarde \xC3\xA0 copier", 'A' + vc_rslot);
        gx_text1(F_SMALL, 58, 98, buf, RGBA(200, 190, 240, 255));
        if (mc_mounted < 0) gx_text1(F_MED, 64, 130, mc_errtxt(mc_err), RGBA(255, 170, 170, 255));
        const int vis = 11;
        int start = vc_rsel - vis / 2; if (start > mc_n - vis) start = mc_n - vis; if (start < 0) start = 0;
        for (int i = 0; mc_mounted >= 0 && i < vis && start + i < mc_n; i++) {
            int r = start + i; f32 ry = 108 + i * 24;
            gp_zone(52, ry, 536, 22, &vc_rsel, r, GPZ_SEL);
            if (r == vc_rsel) gx_rrect_fill(52, ry, 536, 22, 5, TL_SEL1, TL_SEL2);
            text_display(mc[r].title, buf, sizeof(buf)); fit_text(F_MED, buf, 430, fv, sizeof(fv));
            gx_text1(F_MED, 64, ry + 16, fv, RGBA(235, 225, 255, 255));
            snprintf(buf, sizeof(buf), mc[r].blocks == 1 ? "1 bloc" : "%d blocs", mc[r].blocks);
            gx_text_r(F_SMALL, 578, ry + 16, buf, RGBA(170, 155, 225, 255));
        }
        tl_msgbox();
        static const char *const k[] = { "A", "B", "L", "R" }, *const l[] = { "COPIER", "RETOUR", "", "SLOT" };
        tl_legend(k, l, 4);
        return;
    }
    /* carte affichee */
    { char lb[96]; snprintf(buf, sizeof(buf), "%s%s", vc_nf ? vc_label(vc_files[vc_cur], lb, sizeof(lb)) : "-", vc_nf && !strcasecmp(vc_files[vc_cur], vc_active) ? "   (Pulse)" : ""); }
    if (!vc_hub) gx_text_r(F_SMALL, 582, 70, buf, RGBA(190, 160, 255, 255));
    if (vc_ok) snprintf(buf, sizeof(buf), "%d sauvegarde%s, %d blocs libres sur %d", vc_n, vc_n > 1 ? "s" : "", vc_free_blocks(), vc_nblocks() - 5);
    else snprintf(buf, sizeof(buf), "%s%s", vc_why, vc_blank || (vc_size && !vc_ok) ? "  -  X : formater" : "");
    gx_text1(F_SMALL, 58, 96, buf, vc_ok ? RGBA(200, 190, 240, 255) : RGBA(255, 200, 150, 255));
    const int vis = 8;
    const f32 rh = 34;
    int rows = vc_n + 3;
    if (vc_sel < vc_top) vc_top = vc_sel;
    if (vc_sel >= vc_top + vis) vc_top = vc_sel - vis + 1;
    if (vc_top > rows - vis) vc_top = rows - vis;
    if (vc_top < 0) vc_top = 0;
    int loads = 0;
    static const char *const add[3] = { "+ Copier une sauvegarde depuis la SD (.gci)", "+ Copier depuis la vraie carte", "+ Nouvelle carte virtuelle" };
    for (int i = 0; i < vis && vc_top + i < rows; i++) {
        int r = vc_top + i, on = r == vc_sel;
        f32 ry = 104 + i * rh;
        gp_zone(52, ry, 536, rh - 3, &vc_sel, r, GPZ_SEL);
        if (on) gx_rrect_fill(52, ry, 536, rh - 3, 6, TL_SEL1, TL_SEL2);
        if (r < 3) { gx_text1(F_MED, 66, ry + 21, add[r], on ? RGBA(255, 255, 255, 255) : RGBA(255, 210, 120, 255)); continue; }
        vc_ent *v = &vc_e[r - 3];
        if (!v->sub[0] && loads < 1) { vc_meta(v); if (!v->sub[0]) snprintf(v->sub, sizeof(v->sub), " "); loads++; }
        text_display(v->title, buf, sizeof(buf)); fit_text(F_MED, buf, 400, fv, sizeof(fv));
        gx_text1(F_MED, 66, ry + 15, fv, on ? RGBA(255, 255, 255, 255) : RGBA(230, 222, 255, 255));
        text_display(v->sub, buf, sizeof(buf)); fit_text(F_SMALL, buf, 400, fv, sizeof(fv));
        gx_text1(F_SMALL, 66, ry + 29, fv, on ? RGBA(230, 220, 255, 255) : RGBA(150, 135, 210, 255));
        snprintf(buf, sizeof(buf), v->blocks == 1 ? "1 bloc" : "%d blocs", v->blocks);
        gx_text_r(F_SMALL, 578, ry + 15, buf, on ? RGBA(235, 225, 255, 255) : RGBA(150, 135, 210, 255));
        snprintf(buf, sizeof(buf), "%.4s%.2s", (const char *)v->de, (const char *)v->de + 4);
        gx_text_r(F_SMALL, 578, ry + 29, buf, RGBA(130, 115, 185, 255));
    }
    if (vc_pop) {
        const f32 w = 380, h = 46 + VCP_COUNT * 24, x = 220, y = 108;
        gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
        draw_panel(x, y, w, h);
        for (int a = 0; a < VCP_COUNT; a++) {
            f32 ry = y + 14 + a * 24;
            gp_zone(x + 10, ry, w - 20, 22, &vc_psel, a, GPZ_SEL);
            if (a == vc_psel) gx_rrect_fill(x + 10, ry, w - 20, 22, 5, TL_SEL1, TL_SEL2);
            int conf = a == VCP_DEL && vc_pconfirm && a == vc_psel;
            gx_text1(F_MED, x + 22, ry + 17, conf ? "Confirmer : A pour supprimer" : vcp_names[a], conf ? RGBA(255, 160, 160, 255) : RGBA(240, 232, 255, 255));
        }
        gx_text_c(F_SMALL, x + w / 2, y + h - 8, "A : valider   B : fermer", RGBA(255, 210, 120, 255));
    }
    tl_msgbox();
    static const char *const k[] = { "A", "Y", "B", "L", "R" }, *const l[] = { "CHOISIR", "POUR PULSE", "RETOUR", "", "CARTE" }, *const lh[] = { "CHOISIR", "POUR PULSE", "RETOUR", "", "SLOT" };
    tl_legend(k, vc_hub ? lh : l, 5);
}

/* =================== telephone : jeux (construit par la boucle principale) =================== */
/* v301 : les jeux et "jaquette de la liste deja la ?" (pack de Swiss ou ajoutee), pour /api/jaqliste */
static void x73_jaq_build(void) {
    if (!gl_scanned) gl_scan_full(0);
    sbuf_t s = { NULL, 0, 0 };
    char m[16];
    for (int k = 0; k < gl_n; k++) {
        if (gl[k].dvd || strlen(gl[k].id) < 6) continue;
        int dup = 0;
        for (int j = 0; j < k && !dup; j++) if (!gl[j].dvd && !strncmp(gl[j].id, gl[k].id, 6)) dup = 1;
        if (dup) continue;
        snprintf(m, sizeof(m), "%.6s%d\n", gl[k].id, gl[k].post >= 0 ? 1 : 0);
        sb_add(&s, m);
    }
    char *old = x73_jq;
    x73_jq = s.b ? s.b : strdup("");
    x73_jq_ready = 1;
    free(old);
}
/* v313 : le jeu demande par l'appli (/api/discfile) : son fichier sur la carte SD (1er disque) */
static void x73_df_find(void) {
    if (!gl_scanned) gl_scan_full(0);
    x73_df_path[0] = 0;
    for (int k = 0; k < gl_n; k++)
        if (!gl[k].dvd && !strncmp(gl[k].id, x73_df_id, 6) && (!x73_df_path[0] || gl[k].disc == 0)) snprintf(x73_df_path, sizeof(x73_df_path), "%s", gl[k].path);
    x73_df_ready = 1;
}
/* v313 : un fichier du disque (chemin "/res/Fmap/Fmap.arc") : trouve dans la table des fichiers (FST), envoye par
   morceaux de 32 Ko (pas tout en memoire) ; 0 : jeu illisible ou fichier absent (rien envoye) */
static int x73_disc_send(s32 c, const char *path, const char *f) {
    gdisc_t d;
    if (!gd_open(&d, path)) return 0;
    u8 h[0x440], *fst = NULL;
    int sent = 0;
    u32 off = 0, sz = 0;
    if (!gd_read(&d, 0, h, sizeof(h)) || be32(h + 0x1C) != 0xC2339F3Du) goto fin;
    if (!strcasecmp(f, "/main.dol") || !strcasecmp(f, "/sys/main.dol")) {   /* v314 : le programme du jeu (en-tete 0x420), pas dans la FST */
        u8 dh[0x100];
        off = be32(h + 0x420);
        if (!off || !gd_read(&d, off, dh, sizeof(dh))) goto fin;
        for (int k = 0; k < 18; k++) {                         /* 7 sections de code, 11 de donnees : la fin la plus loin */
            u32 so = be32(dh + 4 * k), sl = be32(dh + 0x90 + 4 * k);
            if (so && sl && so + sl > sz) sz = so + sl;
        }
        if (sz < 0x100 || sz > (16u << 20)) { sz = 0; goto fin; }
        goto envoi;
    }
    u32 fo = be32(h + 0x424), fs = be32(h + 0x428);
    if (fs < 12 || fs > (4u << 20) || !(fst = (u8 *)malloc(fs)) || !gd_read(&d, fo, fst, fs)) goto fin;
    u32 ne = be32(fst + 8);
    if (!ne || ne > fs / 12) goto fin;
    const char *names = (const char *)fst + ne * 12;
    u32 nlen = fs - ne * 12, dir = 0;
    const char *p = f;
    while (*p == '/') p++;
    while (*p) {
        char comp[64];
        size_t n = strcspn(p, "/");
        if (!n || n >= sizeof(comp)) goto fin;
        memcpy(comp, p, n); comp[n] = 0;
        p += n;
        while (*p == '/') p++;
        u32 end = be32(fst + dir * 12 + 8), found = 0;
        if (end > ne) end = ne;
        for (u32 i = dir + 1; i < end; ) {
            const u8 *e = fst + i * 12;
            u32 no = be32(e) & 0xFFFFFFu;
            if (no < nlen && memchr(names + no, 0, nlen - no) && !strcasecmp(names + no, comp)) { found = i; break; }
            u32 nx = e[0] ? be32(e + 8) : i + 1;
            if (nx <= i) break;
            i = nx;
        }
        if (!found) goto fin;
        const u8 *e = fst + found * 12;
        if (*p) { if (!e[0]) goto fin; dir = found; }
        else { if (e[0]) goto fin; off = be32(e + 4); sz = be32(e + 8); }
    }
    if (!sz || sz > (32u << 20)) goto fin;
    free(fst); fst = NULL;
envoi:
    {
        char hd[200];
        int hn = snprintf(hd, sizeof(hd), "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", (unsigned)sz);
        sent = 1;
        if (tp_send(c, hd, hn)) {
            static u8 fb[32768];
            while (sz > 0) {
                u32 n = sz > sizeof(fb) ? (u32)sizeof(fb) : sz;
                if (!gd_read(&d, off, fb, n) || !tp_send(c, fb, (int)n)) break;
                off += n; sz -= n;
                tp_last_req = now_ms();
            }
        }
    }
fin:
    free(fst);
    gd_close(&d);
    return sent;
}
static void x73_games_build(void) {
    if (!gl_scanned) gl_scan_full(0);                         /* v241 : liste gardee (avant : toute la carte SD relue, console figee) */
    sbuf_t s = { NULL, 0, 0 };
    char d[200], m[64];
    sb_add(&s, "[");
    for (int k = 0; k < gl_n; k++) {
        if (k) sb_add(&s, ",");
        sb_add(&s, "[");
        text_display(gl[k].title, d, sizeof(d)); x73_json_raw(&s, d); sb_add(&s, ",");
        snprintf(m, sizeof(m), "%.6s", gl[k].id); x73_json_raw(&s, m);
        snprintf(m, sizeof(m), ",%u,%u,%u,%u]", (unsigned)gl[k].runs, (unsigned)(gl[k].play / 60), (unsigned)gl[k].fav, (unsigned)gl[k].last);   /* (v190 : favori, dernier lancement) */
        sb_add(&s, m);
    }
    sb_add(&s, "]");
    char *old = x73_gj;
    x73_gj = s.b ? s.b : strdup("[]");
    x73_gj_ready = 1;
    free(old);
}
/* v190 : telephone : cartes memoire virtuelles (liste, une sauvegarde en .gci, .gci ajoute, carte remplacee) */
static int x73_vc_find(const char *card) {
    vc_scan();
    for (int i = 0; i < vc_nf; i++) if (!strcasecmp(vc_files[i], card)) return i;
    return -1;
}
static void x73_cards_build(void) {
    int keep = vc_cur;
    sbuf_t s = { NULL, 0, 0 };
    char m[200];
    vc_scan();
    sb_add(&s, "[");
    for (int c = 0; c < vc_nf; c++) {
        vc_cur = c;
        int ok = vc_load();
        char p[NAME_LEN]; struct stat st;
        int exists = stat(vc_path(c, p, sizeof(p)), &st) == 0;
        if (c) sb_add(&s, ",");
        sb_add(&s, "{\"f\":"); x73_json_raw(&s, vc_files[c]);
        snprintf(m, sizeof(m), ",\"a\":%d,\"x\":%d,\"ok\":%d,\"bl\":%d,\"b\":%d,\"free\":%d,\"w\":", !strcasecmp(vc_files[c], vc_active), exists, ok, !ok && vc_blank,
                 ok ? vc_nblocks() - 5 : 0, ok ? vc_free_blocks() : 0);
        sb_add(&s, m); x73_json_raw(&s, ok ? "" : vc_why);
        sb_add(&s, ",\"e\":[");
        for (int i = 0; ok && i < vc_n; i++) {
            vc_ent *v = &vc_e[i];
            vc_meta(v);
            if (i) sb_add(&s, ",");
            sb_add(&s, "["); x73_json_raw(&s, v->title); sb_add(&s, ","); x73_json_raw(&s, v->sub);
            char id[8]; snprintf(id, sizeof(id), "%c%c%c%c%c%c", v->de[0], v->de[1], v->de[2], v->de[3], v->de[4], v->de[5]);
            for (int k = 0; k < 6; k++) if ((u8)id[k] < 32 || (u8)id[k] > 126) id[k] = '?';
            sb_add(&s, ","); x73_json_raw(&s, id);
            char nm[80]; mc_safe_name(v->de, nm, sizeof(nm));
            sb_add(&s, ","); x73_json_raw(&s, nm);
            snprintf(m, sizeof(m), ",%d]", v->blocks); sb_add(&s, m);
        }
        sb_add(&s, "]}");
        service_audio();
    }
    sb_add(&s, "]");
    vc_cur = keep < vc_nf ? keep : 0;
    if (tl_screen == TL_VCARD) vc_load();                     /* (l'ecran des cartes reste comme avant) */
    char *old = x73_cj;
    x73_cj = s.b ? s.b : strdup("[]");
    x73_cj_ready = 1;
    free(old);
}
/* v315 : les vraies cartes memoire (slots A et B) pour le telephone (/api/vraiecarte) : x73_vj_card = "A" ou "B" ;
   op 10 liste (JSON dans x73_mcj), 11 une sauvegarde -> SD:/gci/telephone/<nom>.gci, 12 un .gci envoye (SD:/gci) -> la
   carte, 13 effacer (nom verifie : la liste a pu changer), 14 copier sur la carte virtuelle de Pulse du jeu */
static char *x73_mcj = NULL;
static void x73_mc_pump(int op) {
    int slot = x73_vj_card[0] == 'B' ? 1 : 0;
    x73_vj_ok = 0;
    if (!mc_mount(slot)) {
        snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", mc_err ? TR(mc_errtxt(mc_err)) : TR("Carte illisible"));
        if (op == 10) { sbuf_t s = { NULL, 0, 0 }; sb_add(&s, "{\"ok\":0,\"m\":"); x73_json_raw(&s, x73_vj_res); sb_add(&s, "}");
                        free(x73_mcj); x73_mcj = s.b ? s.b : strdup("{\"ok\":0}"); }
        return;
    }
    int i = x73_vj_i;
    char nm[80] = "";
    if (i >= 0 && i < mc_n) mc_safe_name(mc[i].de, nm, sizeof(nm));
    if (op == 10) {
        sbuf_t s = { NULL, 0, 0 };
        char m[160];
        snprintf(m, sizeof(m), "{\"ok\":1,\"libre\":%u,\"bloc\":%u,\"l\":[", (unsigned)mc_free, (unsigned)mc_sector);
        sb_add(&s, m);
        for (int k = 0; k < mc_n; k++) {
            if (!mc[k].loaded) mc_load_meta(k);
            char id[8], f[34], sn[80];
            memcpy(id, mc[k].de, 6); id[6] = 0;
            for (int j = 0; j < 6; j++) if (!isalnum((u8)id[j])) id[j] = '_';
            memcpy(f, mc[k].de + 8, 32); f[32] = 0;
            for (int j = 0; f[j]; j++) if ((u8)f[j] < 32 || (u8)f[j] > 126) f[j] = '?';
            mc_safe_name(mc[k].de, sn, sizeof(sn));
            snprintf(m, sizeof(m), "%s{\"i\":%d,\"b\":%d,\"jeu\":", k ? "," : "", k, mc[k].blocks);
            sb_add(&s, m);
            x73_json_raw(&s, id); sb_add(&s, ",\"f\":"); x73_json_raw(&s, f);
            sb_add(&s, ",\"n\":"); x73_json_raw(&s, sn);
            sb_add(&s, ",\"t\":"); x73_json_raw(&s, mc[k].title); sb_add(&s, ",\"s\":"); x73_json_raw(&s, mc[k].sub); sb_add(&s, "}");
        }
        sb_add(&s, "]}");
        free(x73_mcj); x73_mcj = s.b ? s.b : strdup("{\"ok\":0}");
        x73_vj_ok = 1;
    } else if (op == 12) {
        char p[NAME_LEN];
        snprintf(p, sizeof(p), "%s/%s", MC_DIR, x73_vj_name);
        long n = 0;
        u8 *g = (!strchr(x73_vj_name, '/') && !strstr(x73_vj_name, "..")) ? read_file(p, 16L * 1024 * 1024, &n) : NULL;
        int r = g ? mc_import(slot, g, n) : CARD_ERROR_NOFILE;
        free(g);
        if (r < 0) snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", g ? TR(mc_errtxt(r)) : TR("Fichier .gci introuvable"));
        else { snprintf(x73_vj_res, sizeof(x73_vj_res), "%s %c", TR("Sauvegarde ajout\xC3\xA9" "e sur la vraie carte du slot"), 'A' + slot); x73_vj_ok = 1; remove(p); mc_mount(slot); }
    } else if (i < 0 || i >= mc_n || (x73_vj_name[0] && strcmp(nm, x73_vj_name))) {
        snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR("Sauvegarde introuvable (liste a relire)"));
    } else if (op == 11) {
        long n = 0; u8 *g = mc_export(i, &n);
        char dir[NAME_LEN], out[96], p[NAME_LEN];
        snprintf(dir, sizeof(dir), "%s/telephone", MC_DIR);
        mkdir(MC_DIR, 0777);
        snprintf(p, sizeof(p), "%s/%s.gci", dir, nm); remove(p);
        if (g && mc_gci_write_in(dir, g, n, out, sizeof(out))) { snprintf(x73_vj_res, sizeof(x73_vj_res), "%s/%s", dir, out); x73_vj_ok = 1; }
        else snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR("Lecture de la sauvegarde impossible"));
        free(g);
    } else if (op == 13) {
        int r = CARD_DeleteEntry(slot, &mc[i].dir);
        if (r < 0) snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR(mc_errtxt(r)));
        else { snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR("Sauvegarde supprim\xC3\xA9" "e")); x73_vj_ok = 1; mc_mount(slot); }
    } else if (op == 14) {
        const char *e = vc_load_active();
        if (!e) { long n = 0; u8 *g = mc_export(i, &n); e = g ? vc_import(g, n) : "Lecture de la sauvegarde impossible"; free(g); }
        if (e) snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR(e));
        else { snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR("Copi\xC3\xA9" "e sur la carte virtuelle de Pulse")); x73_vj_ok = 1; }
    }
    gl_busy[0] = 0;
}
static void x73_vj_pump(void) {
    int op = x73_vj_op;
    if (!op || x73_vj_done) return;
    if (op >= 10) { x73_mc_pump(op); x73_vj_op = 0; x73_vj_done = 1; return; }   /* v315 : vraies cartes */
    int keep = vc_cur, c = x73_vc_find(x73_vj_card);
    x73_vj_ok = 0;
    if (c < 0) snprintf(x73_vj_res, sizeof(x73_vj_res), "Carte introuvable");
    else if (op == 1 || op == 2) {
        vc_cur = c;
        int okc = vc_load();
        if (!okc && op == 2 && vc_blank && vc_format()) okc = vc_load();   /* (carte jamais servie : formatee pour y ajouter) */
        if (!okc) snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", vc_why[0] ? vc_why : "Carte illisible");
        else if (op == 1) {                                   /* une sauvegarde -> SD:/gci/telephone/<nom>.gci (envoyee au telephone) */
            long n = 0; u8 *g = x73_vj_i < vc_n ? vc_export(x73_vj_i, &n) : NULL;
            char nm[96], dir[NAME_LEN];
            snprintf(dir, sizeof(dir), "%s/telephone", MC_DIR);
            mkdir(MC_DIR, 0777);
            if (g) { char b[80]; mc_safe_name(g, b, sizeof(b)); snprintf(nm, sizeof(nm), "%s/%s.gci", dir, b); remove(nm); }
            if (g && mc_gci_write_in(dir, g, n, nm, sizeof(nm))) { snprintf(x73_vj_res, sizeof(x73_vj_res), "%s/%s", dir, nm); x73_vj_ok = 1; }
            else snprintf(x73_vj_res, sizeof(x73_vj_res), "Lecture de la sauvegarde impossible");
            free(g);
        } else {                                              /* .gci envoye dans SD:/gci -> la carte */
            char p[NAME_LEN];
            snprintf(p, sizeof(p), "%s/%s", MC_DIR, x73_vj_name);
            long n = 0;
            u8 *g = (!strchr(x73_vj_name, '/') && !strstr(x73_vj_name, "..")) ? read_file(p, 16L * 1024 * 1024, &n) : NULL;
            const char *e = g ? vc_import(g, n) : "Fichier .gci introuvable";
            free(g);
            if (e) snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR(e));
            else { snprintf(x73_vj_res, sizeof(x73_vj_res), "%s %s", TR("Sauvegarde ajout\xC3\xA9" "e sur"), vc_files[c]); x73_vj_ok = 1; remove(p); }
        }
    } else if (op == 4) {                                     /* v262 : une sauvegarde effacee (son nom est verifie : la liste a pu changer) */
        vc_cur = c;
        if (!vc_load()) snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", vc_why[0] ? vc_why : "Carte illisible");
        else {
            char nm[80] = "";
            if (x73_vj_i >= 0 && x73_vj_i < vc_n) mc_safe_name(vc_e[x73_vj_i].de, nm, sizeof(nm));
            const char *e = x73_vj_i < 0 || x73_vj_i >= vc_n || (x73_vj_name[0] && strcmp(nm, x73_vj_name)) ? "Sauvegarde introuvable (liste a relire)" : vc_delete(x73_vj_i);
            if (e) snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR(e));
            else { snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR("Sauvegarde supprim\xC3\xA9" "e")); x73_vj_ok = 1; }
        }
    } else {                                                  /* op 3 : la carte remplacee par un .raw envoye (l'ancienne : .bak) */
        char src[NAME_LEN], dst[NAME_LEN], bak[NAME_LEN + 8];
        snprintf(src, sizeof(src), "%s/%s", MC_DIR, x73_vj_name);
        vc_path(c, dst, sizeof(dst));
        snprintf(bak, sizeof(bak), "%s.bak", dst);
        struct stat st;
        if (strchr(x73_vj_name, '/') || strstr(x73_vj_name, "..") || stat(src, &st) != 0) snprintf(x73_vj_res, sizeof(x73_vj_res), "Fichier introuvable");
        else if (st.st_size < 0x80000 || st.st_size > 0x1000000 || (st.st_size & (st.st_size - 1))) { snprintf(x73_vj_res, sizeof(x73_vj_res), "%s", TR("Ce fichier n'est pas une image de carte (512 Ko \xC3\xA0 16 Mo)")); remove(src); }
        else {
            mkdir(NA_CARD_DIR, 0777);
            remove(bak);
            struct stat sd;
            int had = stat(dst, &sd) == 0;
            if (had && rename(dst, bak) != 0) snprintf(x73_vj_res, sizeof(x73_vj_res), "\xC3\x89" "criture sur la carte SD impossible");
            else if (rename(src, dst) != 0) { if (had) rename(bak, dst); snprintf(x73_vj_res, sizeof(x73_vj_res), "\xC3\x89" "criture sur la carte SD impossible"); }
            else { snprintf(x73_vj_res, sizeof(x73_vj_res), "%s %s%s", TR("Carte remplac\xC3\xA9" "e :"), vc_files[c], had ? TR(" (l'ancienne : .bak)") : ""); x73_vj_ok = 1; }
        }
    }
    vc_cur = keep < vc_nf ? keep : 0;
    if (tl_screen == TL_VCARD) vc_load();
    x73_vj_op = 0; x73_vj_done = 1;
}
/* v242 : demandes du telephone (tab_phone.c : x73_mo_run) */
static int x73_game_of(const char *id, int i) {               /* le jeu : son numero s'il a bien cet ID, sinon le premier avec cet ID */
    if (!gl_scanned) gl_scan_full(0);
    if (i >= 0 && i < gl_n && !strncmp(gl[i].id, id, 6)) return i;
    for (int k = 0; k < gl_n; k++) if (!strncmp(gl[k].id, id, 6)) return k;
    return -1;
}
static void x73_misc_pump(void) {
    int op = x73_mo_op;
    if (!op) return;
    char *r = NULL, m[220];
    if (op == 1) {                                            /* cheats : ajouter, renommer, supprimer, codes */
        ch_phone_edit(x73_mo_id, x73_mo_n, x73_mo_i, x73_mo_name, x73_mo_codes);
        r = ch_phone_json(x73_mo_id);
    } else if (op == 2) {                                     /* reglages du jeu */
        int k = x73_game_of(x73_mo_id, x73_mo_i);
        r = jc_phone(x73_mo_id, k >= 0 ? gl[k].title : "", x73_mo_n, x73_mo_d > 0 ? 1 : x73_mo_d < 0 ? -1 : 0);
    } else if (op == 3) {                                     /* favori (la liste n'est pas retriee : les numeros restent) */
        int k = x73_game_of(x73_mo_id, x73_mo_i);
        if (k >= 0) { gl[k].fav = x73_mo_d >= 0 ? (x73_mo_d ? 1 : 0) : !gl[k].fav; gl_fav_save(); }
        snprintf(m, sizeof(m), "{\"ok\":%d,\"fav\":%d}", k >= 0, k >= 0 ? gl[k].fav : 0);
        r = strdup(m);
    } else if (op == 4) {                                     /* paroles du morceau en cours (toutes, avec leurs temps) */
        sbuf_t s = { NULL, 0, 0 };
        int on = lrc_n > 0 && lrc_track == cur_track && src == SRC_FILE && cur_track >= 0;
        u32 pos = (u32)((u64)consumed_heard() * 1000u / 48000u);
        snprintf(m, sizeof(m), "{\"t\":%d,\"pos\":%u,\"play\":%d,\"l\":[", on ? cur_track : -1, (unsigned)pos, playing && !paused);
        sb_add(&s, m);
        for (int i = 0; on && i < lrc_n; i++) {
            char d[300];
            text_display(lyrics_line(i), d, sizeof(d));
            snprintf(m, sizeof(m), "%s[%u,", i ? "," : "", (unsigned)lrc_lines[i].ms);
            sb_add(&s, m); x73_json_raw(&s, d); sb_add(&s, "]");
        }
        sb_add(&s, "]}");
        r = s.b;
    } else if (op == 5) {                                     /* morceau ajoute a la file : a la suite (d = 1) ou a la fin */
        int t = x73_mo_i, ok = t >= 0 && t < nfiles;
        if (ok) {
            if (qn == 0 || qpos < 0 || qpos >= qn || (cur_track >= 0 && queue[qpos] != cur_track)) {
                if (cur_track >= 0 && src == SRC_FILE) queue_build(cur_track); else { qn = 0; qpos = -1; }
            }
            if (qn >= MAX_FILES) ok = 0;
            else if (qpos < 0) { queue[0] = (s16)t; qn = 1; qpos = 0; play_queue_pos(0); }
            else {
                int at = x73_mo_d > 0 ? qpos + 1 : qn;
                memmove(&queue[at + 1], &queue[at], (size_t)(qn - at) * sizeof(queue[0]));
                queue[at] = (s16)t; qn++;
            }
        }
        snprintf(m, sizeof(m), "{\"ok\":%d}", ok);
        r = strdup(m);
    } else if (op == 6) {                                     /* copie de tous les reglages (le fichier de START > Outils) */
        char msg[200];
        r = bk_save(msg, sizeof(msg)) > 0 ? strdup("@" BK_FILE) : strdup("{}");
    } else if (op == 7) {                                     /* restauration d'une copie envoyee par le telephone */
        char msg[200];
        long len = 0;
        char *b = (char *)read_file(TP_REGTEL, 32L * 1024 * 1024, &len);
        int n = 0;
        if (!b) snprintf(msg, sizeof(msg), "Copie introuvable");
        else {
            n = bk_restore_buf(b, len, 1, msg, sizeof(msg));
            if (n > 0) n = bk_restore_buf(b, len, 0, msg, sizeof(msg));
            free(b);
            if (n > 0) { load_settings(); eq_setup(); settings_dirty = 0; snprintf(msg, sizeof(msg), "%d fichiers restaur\xC3\xA9s", n); lib_note(msg); }
        }
        remove(TP_REGTEL);
        sbuf_t s = { NULL, 0, 0 };
        snprintf(m, sizeof(m), "{\"ok\":%d,\"msg\":", n > 0); sb_add(&s, m); x73_json_raw(&s, msg); sb_add(&s, "}");
        r = s.b;
    }
    else if (op == 8) r = cp_json();                          /* v255 : pastilles des jeux */
    else if (op == 10) r = sj_add(x73_mo_id, x73_mo_codes);   /* v306 : succes cree par le chercheur de codes (codes = la ligne) */
    else if (op == 9) { cp_set_manual(x73_mo_id, x73_mo_d >= 0 && x73_mo_d < CP_N ? x73_mo_d : 0); r = cp_json(); }
    free(x73_mo_res);
    x73_mo_res = r;
    x73_mo_op = 0; x73_mo_ready = 1;
}
static void x73_launch_pump(void) {
    if (x73_launch_k < 0) return;
    int k = x73_launch_k, m = x73_launch_m;
    x73_launch_k = -1;
    if (!gl_scanned) gl_scan_full(0);
    if (x73_launch_id[0] && (k < 0 || k >= gl_n || strncmp(gl[k].id, x73_launch_id, 6))) {   /* v242 : par l'ID (raccourci du telephone) */
        int f = -1;
        for (int i = 0; i < gl_n && f < 0; i++) if (!strncmp(gl[i].id, x73_launch_id, 6)) f = i;
        k = f;
    }
    x73_launch_id[0] = 0;
    if (k < 0 || k >= gl_n) return;
    crumb("telephone : lancement du jeu %d (%s)", k, m ? "Pulse" : "lanceur");
    menu_open = 0; kb_open = 0; tl_screen = TL_NONE;
    tab = TAB_GAMES; tab_enter(TAB_GAMES);
    gl_sel = k;
    const char *e;
    snprintf(jc_fx, sizeof(jc_fx), "%s", m ? x73_launch_fx : "");   /* v250 : patchs de secours (pour ce lancement) */
    x73_launch_fx[0] = 0;
    if (m) e = game_boot_auto(k);                       /* (ne revient pas si ca marche ; v190 : comme A dans la fiche) */
    else { gl_status("Lancement du jeu..."); e = game_boot(k, 0); }
    gl_busy[0] = 0;
    jc_fx[0] = 0;                                         /* (pas lance : les patchs ne restent pas pour la prochaine fois) */
    if (e) gl_note(e);
}

static void rf_pump(void);
static void rcp_json(void);                             /* v191 (recap.c) */
/* =================== boucle principale =================== */
static void x73_pump(void) {
    ts_want = (ts_speed != 100 || ts_sil) && ((pod_on && src == SRC_RADIO) || (pod_local_k >= 0 && src == SRC_FILE && cur_track == pod_local_k));
    static u64 t1 = 0;
    u64 now = now_ms();
    if (now - t1 >= 1000) {                                   /* une fois par seconde */
        t1 = now;
        u32 day = 0;
        int m = rp_now(&day);
        rp_tick(m, day);
        if (saver_on && saver_style == SV_TABLEAU && city[0] && meteo_t && now - meteo_t > 30 * 60000 && if_state == IF_IDLE) meteo_t = 0;
    }
    rf_pump();                                                /* v74 : titres de Radio France */
    rh_capture();
    if (fun_mode == FUN_OFF || fun_mode == FUN_KARAOKE || fun_mode == FUN_WAKE) rhc_pump();
    if (x73_gj_want) { x73_gj_want = 0; x73_games_build(); }
    if (x73_df_want) { x73_df_want = 0; x73_df_find(); }   /* v313 */
    if (x73_jq_want) { x73_jq_want = 0; x73_jaq_build(); }   /* v301 */
    if (x73_cj_want) { x73_cj_want = 0; x73_cards_build(); }   /* v190 */
    if (x73_ch_want) {                                        /* v240 : cheats depuis le telephone */
        if (x73_ch_want == 2) ch_phone_set(x73_ch_id, x73_ch_set);
        char *j = ch_phone_json(x73_ch_id), *old = x73_chj;
        x73_chj = j ? j : strdup("{}");
        free(old);
        x73_ch_want = 0; x73_ch_ready = 1;
    }
    if (x73_rj_want) { x73_rj_want = 0; rcp_json(); }        /* v191 : recap */
    x73_vj_pump();
    x73_misc_pump();                                          /* v242 */
    x73_launch_pump();
}

/* =================== v74 : titres des radios de Radio France ===================
   Les flux de Radio France (FIP, France Inter...) n'annoncent pas leurs titres (pas d'ICY) :
   on les demande a leur service "livemeta" (le morceau en cours : levels[0].items[position]). */
#ifndef RF_LIVE
#define RF_LIVE "https://api.radiofrance.fr/livemeta/pull/"
#endif
static hfetch_t rff = { .c = { -1, 0, 0 } };
static int rf_state = 0, rf_station = -2;
static u64 rf_next = 0;
static int rf_id_of(const char *url) {                  /* radio de Radio France -> numero de station */
    if (!strstr(url, "radiofrance.fr") && !strstr(url, "radiofrance.com")) return -1;
    static const struct { const char *k; int id; } m[] = {
        { "fip", 7 }, { "franceinter", 1 }, { "franceinfo", 2 }, { "francemusique", 4 }, { "franceculture", 5 }, { "mouv", 6 } };
    const char *p = strstr(url, "://");
    p = p ? strchr(p + 3, '/') : NULL;
    if (!p) return -1;
    p++;
    char slug[40]; int n = 0;
    while (p[n] && n < 39 && (isalpha((unsigned char)p[n]))) { slug[n] = (char)tolower((unsigned char)p[n]); n++; }
    slug[n] = 0;
    if (p[n] && p[n] != '-' && p[n] != '/' && p[n] != '_' && p[n] != '.') return -1;   /* (fip-midfi, fip/fip_hifi, fip.mp3) */
    for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]); i++) if (!strcmp(slug, m[i].k)) return m[i].id;
    return -1;
}
/* le morceau en cours dans la reponse : "Artiste - Titre" (ou le nom de l'emission) */
static int rf_parse(const char *js, int len, char *out, int cap) {
    int maxt = len / 6 + 64;
    if (maxt > 40000) maxt = 40000;
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * maxt);
    if (!t) return 0;
    int n = json_tokens(js, len, t, maxt), ok = 0;
    out[0] = 0;
    if (n > 0 && t[0].type == JT_OBJ) {
        int lv = jget(js, t, n, 0, "levels"), steps = jget(js, t, n, 0, "steps");
        if (lv >= 0 && t[lv].type == JT_ARR && lv + 1 < n && steps >= 0) {
            int l0 = lv + 1;
            char ps[16], id[96];
            jstr(js, t, jget(js, t, n, l0, "position"), ps, sizeof(ps));
            int items = jget(js, t, n, l0, "items"), pos = atoi(ps), k = 0;
            if (items >= 0 && t[items].type == JT_ARR) {
                int it = -1;
                for (int i = items + 1; i < n && t[i].start < t[items].end; i = jskip(t, n, i), k++) if (k == pos) it = i;
                if (it >= 0) {
                    jstr(js, t, it, id, sizeof(id));
                    int st = jget(js, t, n, steps, id);
                    if (st >= 0) {
                        char ti[160], au[120], ty[24];
                        jstr(js, t, jget(js, t, n, st, "title"), ti, sizeof(ti));
                        jstr(js, t, jget(js, t, n, st, "embedType"), ty, sizeof(ty));
                        int ha = jget(js, t, n, st, "highlightedArtists");
                        au[0] = 0;
                        if (ha >= 0 && t[ha].type == JT_ARR && ha + 1 < n && t[ha + 1].start < t[ha].end) jstr(js, t, ha + 1, au, sizeof(au));
                        if (!au[0]) jstr(js, t, jget(js, t, n, st, "authors"), au, sizeof(au));
                        if (ti[0] && au[0] && !strcmp(ty, "song")) snprintf(out, cap, "%s - %s", au, ti);
                        else if (ti[0]) snprintf(out, cap, "%s", ti);
                        ok = out[0] != 0;
                    }
                }
            }
        }
    }
    free(t);
    return ok;
}
static void rf_pump(void) {
    int id = (src == SRC_RADIO && !pod_on && state == ST_PLAY && cur_station >= 0 && cur_station <= nstations) ? rf_id_of(stations[cur_station].url) : -1;
    if (id < 0 && cur_station >= 0 && cur_station <= nstations && src == SRC_RADIO && !pod_on && state == ST_PLAY) id = rf_id_of(http_final_url);
    if (id != rf_station) {                                   /* autre radio : on repart de zero */
        if (rf_state) hf_abort(&rff);
        rf_state = 0; rf_station = id; rf_next = now_ms() + 1500;
    }
    if (id < 0) return;
    if (rf_state == 0) {
        if (now_ms() < rf_next || net_phase != NET_DONE || !net_ok) return;
        char url[160];
        snprintf(url, sizeof(url), RF_LIVE "%d", id);
        rff.image = 0; rff.maxbytes = 256 * 1024; rff.accept = "application/json";
        hf_start(&rff, url, NULL);
        rf_state = 1;
        return;
    }
    hf_pump(&rff);
    if (rff.st == HF_FAIL) { hf_drop(&rff); rf_state = 0; rf_next = now_ms() + 60000; return; }
    if (rff.st != HF_DONE) return;
    rff.st = HF_IDLE;
    char ti[160];
    char *js = (char *)malloc(rff.len + 1);
    if (js) {
        memcpy(js, rff.buf, rff.len); js[rff.len] = 0;
        if (rf_parse(js, rff.len, ti, sizeof(ti))) {
            snprintf(now_title, sizeof(now_title), "%s", ti);   /* (UTF-8 : l'affichage le reconnait) */
        }
        free(js);
    }
    hf_drop(&rff);
    rf_state = 0;
    rf_next = now_ms() + 20000;                               /* (toutes les 20 s) */
}

/* =================== v75 : reseau de GC Station (section Reseau des reglages) =================== */
static int net_ip_ok(const char *t) {
    int a, b, c, d; char x;
    return sscanf(t, "%d.%d.%d.%d%c", &a, &b, &c, &d, &x) == 4 && a >= 0 && a < 256 && b >= 0 && b < 256 && c >= 0 && c < 256 && d >= 0 && d < 256;
}
static void net_kb_done(const char *t) {
    char v[32];
    snprintf(v, sizeof(v), "%s", t);
    char *p = trim(v);
    if (p[0] && !net_ip_ok(p)) { lib_note("Adresse invalide (exemple : 192.168.1.32)"); return; }
    if (kb_field == MI_GW) snprintf(net_cfg_gw, sizeof(net_cfg_gw), "%.15s", p);
    else snprintf(net_cfg_ip, sizeof(net_cfg_ip), "%.15s", p);
    settings_dirty = 1;
    lib_note("Pris en compte au prochain d\xC3\xA9marrage de GC Station");
}
/* au demarrage, avant la connexion : adresse fixe si DHCP = non */
static void net_cfg_apply(void) {
    if (net_phase != NET_NONE) return;
    net_static = !net_dhcp && net_ip_ok(net_cfg_ip);
    if (!net_static) return;
    u32 m = net_cfg_bits ? 0xFFFFFFFFu << (32 - net_cfg_bits) : 0;
    snprintf(ip_str, sizeof(ip_str), "%.15s", net_cfg_ip);
    snprintf(mask_str, sizeof(mask_str), "%u.%u.%u.%u", m >> 24, (m >> 16) & 255, (m >> 8) & 255, m & 255);
    snprintf(gw_str, sizeof(gw_str), "%.15s", net_cfg_gw);
}
/* v315 : la liste des vraies cartes pour le serveur (copie : le fil du serveur la libere) */
char *x73_mcj_get(void) { return x73_mcj ? strdup(x73_mcj) : NULL; }

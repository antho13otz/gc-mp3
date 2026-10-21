// GC-MP3 : onglet PODCASTS (inclus dans main.c)
// Abonnements dans sd:/podcasts.txt (Nom | adresse du flux RSS). Les episodes
// s'ecoutent en direct (comme une radio) ou se telechargent sur la carte SD.
// La position d'ecoute de chaque episode est retenue (sd:/podcasts/positions.txt).
#define POD_FILE   SD_ROOT "/podcasts.txt"
#define POD_POS    SD_ROOT "/podcasts/positions.txt"
#define POD_MAX    64
#define EP_MAX     120
#ifndef ITUNES_BASE
#define ITUNES_BASE "https://itunes.apple.com"
#endif

static const char *POD_DEFAULT =
    "# Podcasts de GC-MP3 : un abonnement par ligne, au format   Nom | adresse du flux RSS\n"
    "# (ou utilise \"Rechercher un podcast\" dans l'onglet PODCASTS)\n"
    "La M\xC3\xA9thode scientifique | https://radiofrance-podcast.net/podcast09/podcast_423b51d8-3f44-4b9a-b906-e0d14bdf0516.xml\n"
    "Le Cours de l'histoire | https://radiofrance-podcast.net/podcast09/podcast_c951e8a9-6121-4400-a3ef-5f9c958d36c3.xml\n"
    "Les odyss\xC3\xA9" "es | https://radiofrance-podcast.net/podcast09/podcast_c361798b-d6e3-4282-ba0a-ebb051b9e424.xml\n"
    "Interception | https://radiofrance-podcast.net/podcast09/podcast_886cc3b6-cd86-11e0-b8ee-842b2b72cd1d.xml\n";

typedef struct { char title[160]; char url[URL_MAX]; char date[24]; int dur; long bytes; } ep_t;
static pod_t pods[POD_MAX];
static int npods = 0, pods_loaded = 0;
static ep_t *eps = NULL;                    /* episodes du podcast ouvert */
static int neps = 0;
static int pod_view = 0;                    /* 0 = abonnements, 1 = episodes, 2 = resultats de recherche */
static int pod_sel = 0, pod_open = -1;
static char pod_msg[160];
static u64 pod_msg_t = 0;

/* lecture en cours */
static int pod_on = 0, pod_dur = 0;
static int pod_offset = 0;                  /* secondes sautees au debut (reprise, avance) */
static int pod_starting = 0;
static char pod_ep_url[URL_MAX], pod_ep_title[160];
static long pod_ep_bytes = 0;
static u64 pod_save_t = 0;

/* recherche (iTunes) */
static hfetch_t pdf = { .c = { -1, 0, 0 } };
enum { PF_IDLE, PF_FEED, PF_SEARCH };
static int pf_state = PF_IDLE;
static pod_t *psr = NULL;                   /* resultats de recherche */
static int npsr = 0;

static void pod_note(const char *m) { snprintf(pod_msg, sizeof(pod_msg), "%s", m); pod_msg_t = now_ms(); }

static int pod_pos_sec(void) { return pod_offset + (int)(consumed / 48000u); }

/* --- abonnements --- */
static void pods_load(void) {
    npods = 0;
    pods_loaded = 1;
    if (!sd_ok) return;
    FILE *f = fopen(POD_FILE, "r");
    if (!f) {
        f = fopen(POD_FILE, "w");
        if (f) { fputs(POD_DEFAULT, f); fclose(f); }
        f = fopen(POD_FILE, "r");
        if (!f) return;
    }
    static char line[URL_MAX + 64];
    while (fgets(line, sizeof(line), f) && npods < POD_MAX) {
        char *t = trim(line);
        if (!t[0] || t[0] == '#') continue;
        char *bar = strchr(t, '|');
        char *u = bar ? trim(bar + 1) : t;
        if (bar) *bar = 0;
        if (strncasecmp(u, "http", 4) != 0) continue;
        snprintf(pods[npods].name, sizeof(pods[npods].name), "%s", bar ? trim(t) : "Podcast");
        snprintf(pods[npods].url, sizeof(pods[npods].url), "%s", u);
        npods++;
    }
    fclose(f);
}

static void pods_add(const char *name, const char *url) {
    for (int i = 0; i < npods; i++) if (!strcmp(pods[i].url, url)) { pod_note("D\xC3\xA9j\xC3\xA0 dans tes abonnements"); return; }
    if (npods >= POD_MAX || !sd_ok) return;
    FILE *f = fopen(POD_FILE, "a");
    if (!f) { pod_note("Impossible d'\xC3\xA9" "crire SD:/podcasts.txt"); return; }
    char nm[96];
    snprintf(nm, sizeof(nm), "%s", name);
    for (char *p = nm; *p; p++) if (*p == '|' || *p == '\n' || *p == '\r') *p = '-';
    fprintf(f, "%s | %s\n", nm, url);
    fclose(f);
    snprintf(pods[npods].name, sizeof(pods[npods].name), "%s", nm);
    snprintf(pods[npods].url, sizeof(pods[npods].url), "%s", url);
    npods++;
    pod_note("Abonnement ajout\xC3\xA9");
}

/* --- positions d'ecoute --- */
static int pod_get_pos(const char *url) {
    FILE *f = sd_ok ? fopen(POD_POS, "r") : NULL;
    if (!f) return 0;
    static char line[URL_MAX + 64];
    int pos = 0;
    while (fgets(line, sizeof(line), f)) {
        char *bar = strchr(line, '|');
        if (!bar) continue;
        char *u = trim(bar + 1);
        if (!strcmp(u, url)) pos = atoi(line);
    }
    fclose(f);
    return pos;
}

static void pod_set_pos(const char *url, int sec) {
    if (!sd_ok || !url[0]) return;
    mkdir(SD_ROOT "/podcasts", 0777);
    /* on reecrit le fichier (au plus 200 episodes retenus) */
    enum { KEEP_LEN = URL_MAX + 16 };
    char (*keep)[KEEP_LEN] = malloc(200 * KEEP_LEN);
    if (!keep) return;
    int nk = 0;
    FILE *f = fopen(POD_POS, "r");
    if (f) {
        static char line[URL_MAX + 64];
        while (fgets(line, sizeof(line), f) && nk < 199) {
            char *bar = strchr(line, '|');
            if (!bar) continue;
            char *u = trim(bar + 1);
            if (!strcmp(u, url)) continue;
            snprintf(keep[nk++], KEEP_LEN, "%d | %s", atoi(line), u);
        }
        fclose(f);
    }
    f = fopen(POD_POS, "w");
    if (!f) { free(keep); return; }
    int start = nk > 198 ? nk - 198 : 0;
    for (int i = start; i < nk; i++) fprintf(f, "%s\n", keep[i]);
    fprintf(f, "%d | %s\n", sec, url);
    fclose(f);
    free(keep);
}

static void podcast_save_pos(void) {
    if (!pod_on || !pod_ep_url[0]) return;
    int p = pod_pos_sec();
    if (pod_dur > 0 && p > pod_dur - 30) p = 0;                 /* episode termine : on repartira du debut */
    pod_set_pos(pod_ep_url, p);
}

/* quitte le mode podcast (autre radio, musique, arret) */
static void podcast_leave(void) {
    if (pod_starting) return;
    if (pod_on) podcast_save_pos();
    pod_on = 0;
}

/* --- RSS --- */
/* texte XML -> UTF-8 (CDATA, entites) */
static void xml_text(const char *a, const char *b, char *out, int cap) {
    int n = 0;
    while (a < b && (*a == ' ' || *a == '\n' || *a == '\r' || *a == '\t')) a++;
    if (b - a > 9 && !strncmp(a, "<![CDATA[", 9)) {
        a += 9;
        const char *e = strstr(a, "]]>");
        if (e && e < b) b = e;
    }
    while (a < b && n < cap - 4) {
        if (*a == '&') {
            const char *sc = (const char *)memchr(a, ';', b - a);
            if (sc && sc - a < 10) {
                unsigned cp = 0;
                if (!strncmp(a, "&amp;", 5)) cp = '&';
                else if (!strncmp(a, "&lt;", 4)) cp = '<';
                else if (!strncmp(a, "&gt;", 4)) cp = '>';
                else if (!strncmp(a, "&quot;", 6)) cp = '"';
                else if (!strncmp(a, "&apos;", 6)) cp = '\'';
                else if (a[1] == '#') cp = (a[2] == 'x' || a[2] == 'X') ? (unsigned)strtoul(a + 3, NULL, 16) : (unsigned)strtoul(a + 2, NULL, 10);
                if (cp) {
                    if (cp < 0x80) out[n++] = (char)cp;
                    else if (cp < 0x800) { out[n++] = (char)(0xC0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { out[n++] = (char)(0xE0 | (cp >> 12)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63)); }
                    else out[n++] = '?';
                    a = sc + 1;
                    continue;
                }
            }
        }
        char c = *a++;
        out[n++] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
}

/* contenu de la balise <tag> entre a et b (premiere occurrence) */
static int xml_tag(const char *a, const char *b, const char *tag, char *out, int cap) {
    char open[48];
    snprintf(open, sizeof(open), "<%s", tag);
    int ol = (int)strlen(open);
    out[0] = 0;
    for (const char *p = a; p && p < b; ) {
        p = strstr(p, open);
        if (!p || p >= b) return 0;
        char c = p[ol];
        if (c != '>' && c != ' ' && c != '/') { p += ol; continue; }
        const char *gt = strchr(p, '>');
        if (!gt || gt >= b) return 0;
        if (gt[-1] == '/') return 0;
        char close[52];
        snprintf(close, sizeof(close), "</%s>", tag);
        const char *e = strstr(gt + 1, close);
        if (!e || e > b) return 0;
        xml_text(gt + 1, e, out, cap);
        return 1;
    }
    return 0;
}

/* attribut name="..." d'une balise ouvrante <tag ...> */
static int xml_attr(const char *a, const char *b, const char *tag, const char *attr, char *out, int cap) {
    char open[48], key[48];
    snprintf(open, sizeof(open), "<%s", tag);
    snprintf(key, sizeof(key), " %s=", attr);
    out[0] = 0;
    const char *p = strstr(a, open);
    if (!p || p >= b) return 0;
    const char *gt = strchr(p, '>');
    if (!gt || gt > b) return 0;
    const char *k = strstr(p, key);
    if (!k || k > gt) return 0;
    k += strlen(key);
    char q = *k;
    if (q != '"' && q != '\'') return 0;
    const char *e = strchr(k + 1, q);
    if (!e || e > gt) return 0;
    xml_text(k + 1, e, out, cap);
    return 1;
}

static int parse_hms(const char *s) {
    int h = 0, m = 0, sec = 0;
    if (sscanf(s, "%d:%d:%d", &h, &m, &sec) == 3) return h * 3600 + m * 60 + sec;
    if (sscanf(s, "%d:%d", &m, &sec) == 2) return m * 60 + sec;
    return atoi(s);
}

static void rss_parse(const char *x, int len) {
    if (!eps) eps = (ep_t *)malloc(sizeof(ep_t) * EP_MAX);
    neps = 0;
    if (!eps) return;
    const char *end = x + len;
    const char *p = x;
    while (neps < EP_MAX && (p = strstr(p, "<item")) != NULL && p < end) {
        const char *e = strstr(p, "</item>");
        if (!e || e > end) break;
        ep_t *ep = &eps[neps];
        char tmp[64];
        xml_tag(p, e, "title", ep->title, sizeof(ep->title));
        xml_attr(p, e, "enclosure", "url", ep->url, sizeof(ep->url));
        ep->bytes = xml_attr(p, e, "enclosure", "length", tmp, sizeof(tmp)) ? atol(tmp) : 0;
        ep->dur = xml_tag(p, e, "itunes:duration", tmp, sizeof(tmp)) ? parse_hms(tmp) : 0;
        ep->date[0] = 0;
        if (xml_tag(p, e, "pubDate", tmp, sizeof(tmp))) {                 /* "Tue, 20 Oct 2026 ..." -> "20 Oct 2026" */
            const char *c = strchr(tmp, ',');
            snprintf(ep->date, sizeof(ep->date), "%.11s", c ? c + 2 : tmp);
        }
        if (ep->url[0] && strncasecmp(ep->url, "http", 4) == 0) neps++;
        p = e + 7;
    }
}

static void pod_open_feed(int i) {
    if (i < 0 || i >= npods) return;
    if (net_phase != NET_DONE || !net_ok) { pod_note("Pas de r\xC3\xA9seau"); return; }
    pod_open = i;
    neps = 0;
    pod_view = 1;
    pod_sel = 0;
    pdf.image = 0;
    pdf.maxbytes = 6 * 1024 * 1024;
    pdf.accept = "application/rss+xml,application/xml,text/xml,*/*";
    hf_start(&pdf, pods[i].url, NULL);
    pf_state = PF_FEED;
}

static void podcast_search(const char *q) {
    char enc[400], url[URL_MAX];
    if (!url_encode(q, enc, sizeof(enc))) return;
    net_start_async();
    snprintf(url, sizeof(url), ITUNES_BASE "/search?media=podcast&entity=podcast&country=%s&limit=40&term=%s", lang_country(), enc);
    pdf.image = 0;
    pdf.maxbytes = 1024 * 1024;
    pdf.accept = "application/json";
    hf_start(&pdf, url, NULL);
    pf_state = PF_SEARCH;
    pod_view = 2;
    pod_sel = 0;
    npsr = 0;
}

static void search_parse(const char *js, int len) {
    int maxt = len / 6 + 64;
    if (maxt > 60000) maxt = 60000;
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * maxt);
    if (!psr) psr = (pod_t *)malloc(sizeof(pod_t) * 40);
    npsr = 0;
    if (!t || !psr) { free(t); return; }
    int n = json_tokens(js, len, t, maxt);
    int res = (n > 0 && t[0].type == JT_OBJ) ? jget(js, t, n, 0, "results") : -1;
    if (res >= 0 && t[res].type == JT_ARR)
        for (int r = res + 1; r < n && t[r].start < t[res].end && npsr < 40; r = jskip(t, n, r)) {
            jstr(js, t, jget(js, t, n, r, "collectionName"), psr[npsr].name, sizeof(psr[npsr].name));
            jstr(js, t, jget(js, t, n, r, "feedUrl"), psr[npsr].url, sizeof(psr[npsr].url));
            if (psr[npsr].url[0]) npsr++;
        }
    free(t);
}

/* --- lecture --- */
static void podcast_play_at(int sec) {
    if (!pod_ep_url[0]) return;
    pod_starting = 1;
    if (sec < 0) sec = 0;
    if (pod_dur > 0 && sec > pod_dur - 5) sec = pod_dur - 5;
    long bps = (pod_ep_bytes > 0 && pod_dur > 0) ? pod_ep_bytes / pod_dur : 16000;     /* octets par seconde */
    http_range = sec > 0 ? (long)sec * bps : 0;
    pod_offset = sec;
    /* M4A : la position exacte vient de la table (lue au premier passage) */
    ms.want = -1;
    ms.want_sec = sec > 0 ? sec : -1;
    if (ms_have_table(pod_ep_url)) {
        u32 s = mp4_sample_at(&ms.m, sec), off = 0, size;
        mp4it_t it;
        mp4_it_seek(&ms.m, &it, s);
        if (sec > 0 && mp4_it_peek(&ms.m, &it, &off, &size)) {
            http_range = (long)off;
            ms.want = (int)s;
            ms.want_sec = -1;
            pod_offset = (int)(mp4_time_of(&ms.m, s) + 0.5);
        } else { http_range = 0; ms.want_sec = -1; }
    } else if (sec > 0 && mp4_url_looks(pod_ep_url, "")) http_range = 0;     /* il faut d'abord la table */
    snprintf(stations[nstations].name, sizeof(stations[nstations].name), "%s", pod_ep_title);
    snprintf(stations[nstations].url, sizeof(stations[nstations].url), "%s", pod_ep_url);
    net_start_async();
    start_station(nstations, 0);
    http_range = 0;
    pod_on = 1;
    pod_starting = 0;
    pod_save_t = now_ms();
}

static void podcast_play(const ep_t *ep) {
    crumb("podcast : %.80s", ep->title);
    podcast_leave();
    if (strlen(ep->url) >= sizeof(stations[0].url) - 1) { pod_note("Adresse de l'\xC3\xA9pisode trop longue"); return; }
    snprintf(pod_ep_url, sizeof(pod_ep_url), "%s", ep->url);
    snprintf(pod_ep_title, sizeof(pod_ep_title), "%s", ep->title);
    pod_ep_bytes = ep->bytes;
    pod_dur = ep->dur;
    int resume = pod_get_pos(ep->url);
    podcast_play_at(resume > 10 ? resume - 3 : 0);
    if (resume > 10) pod_note("Reprise l\xC3\xA0 o\xC3\xB9 tu t'\xC3\xA9tais arr\xC3\xAAt\xC3\xA9");
}

/* fichier audio ouvert depuis une page web : lu comme un episode (avance, recul, reprise) */
static void podcast_play_url(const char *title, const char *url, long bytes) {
    ep_t ep;
    memset(&ep, 0, sizeof(ep));
    snprintf(ep.title, sizeof(ep.title), "%s", title);
    snprintf(ep.url, sizeof(ep.url), "%s", url);
    ep.bytes = bytes > 0 ? bytes : 0;
    podcast_play(&ep);
}

/* L / R pendant un podcast : -15 s / +30 s */
static void podcast_skip(int s) {
    if (!pod_on) return;
    podcast_play_at(pod_pos_sec() + s);
}

/* fin de l'episode (le flux s'est termine) */
static void podcast_finished(void) {
    pod_set_pos(pod_ep_url, 0);
    pod_on = 0;
    stop_station();
    pod_note("\xC3\x89pisode termin\xC3\xA9");
}

static void podcast_download(const ep_t *ep) {
    if (dl_active) { pod_note("Un t\xC3\xA9l\xC3\xA9" "chargement est d\xC3\xA9j\xC3\xA0 en cours"); return; }
    snprintf(dl_name_hint, sizeof(dl_name_hint), "%s", ep->title);
    snprintf(dl_dest_dir, sizeof(dl_dest_dir), SD_ROOT "/podcasts");
    mkdir(dl_dest_dir, 0777);
    web_fetch(ep->url, NAV_PUSH, 0);
    pod_note("T\xC3\xA9l\xC3\xA9" "chargement demand\xC3\xA9 (SD:/podcasts)");
}

static void podcast_enter(void) { if (!pods_loaded) pods_load(); }

static int pod_rows(void) {
    if (pod_view == 0) return npods + 1;                 /* + "Rechercher un podcast" */
    if (pod_view == 1) return neps + 1;                  /* + retour */
    return npsr + 1;
}

static void podcast_input(u32 down, u32 nav, int fire) {
    int rows = pod_rows();
    if (fire && rows > 0) {
        if (nav & PAD_BUTTON_DOWN) pod_sel++;
        if (nav & PAD_BUTTON_UP) pod_sel--;
        if (nav & PAD_BUTTON_RIGHT) pod_sel += MUSIC_ROWS;
        if (nav & PAD_BUTTON_LEFT) pod_sel -= MUSIC_ROWS;
        if (pod_sel < 0) pod_sel = 0;
        if (pod_sel > rows - 1) pod_sel = rows - 1;
    }
    if (down & PAD_BUTTON_A) {
        if (pod_view == 0) {
            if (pod_sel == 0) kb_start(KB_PODCAST, "", -1, -1);
            else pod_open_feed(pod_sel - 1);
        } else if (pod_view == 1) {
            if (pod_sel == 0) { pod_view = 0; pod_sel = pod_open + 1; hf_abort(&pdf); pf_state = PF_IDLE; }
            else if (pod_sel - 1 < neps) podcast_play(&eps[pod_sel - 1]);
        } else {
            if (pod_sel == 0) { pod_view = 0; pod_sel = 0; hf_abort(&pdf); pf_state = PF_IDLE; }
            else if (pod_sel - 1 < npsr) pods_add(psr[pod_sel - 1].name, psr[pod_sel - 1].url);
        }
    }
    if ((down & PAD_BUTTON_X) && pod_view == 1 && pod_sel > 0 && pod_sel - 1 < neps) podcast_download(&eps[pod_sel - 1]);
    if (down & PAD_BUTTON_B) {
        if (pod_view != 0 && !(src == SRC_RADIO && state != ST_IDLE)) { pod_view = 0; pod_sel = pod_open >= 0 ? pod_open + 1 : 0; }
        else { podcast_leave(); stop_station(); stop_track(); }
    }
    if (down & PAD_BUTTON_Y) {
        if (src == SRC_FILE && playing) paused = !paused;
        else if (src == SRC_RADIO) muted = !muted;
    }
}

static void podcast_pump(void) {
    if (pod_on && now_ms() - pod_save_t > 15000) { pod_save_t = now_ms(); podcast_save_pos(); }
    if (pf_state == PF_IDLE) return;
    hf_pump(&pdf);
    if (pdf.st == HF_FAIL) { pod_note(pf_state == PF_FEED ? "Flux du podcast introuvable" : "Recherche impossible"); pf_state = PF_IDLE; hf_drop(&pdf); return; }
    if (pdf.st != HF_DONE) return;
    pdf.st = HF_IDLE;
    char *txt = (char *)malloc(pdf.len + 1);
    if (txt) {
        memcpy(txt, pdf.buf, pdf.len);
        txt[pdf.len] = 0;
        if (pf_state == PF_FEED) { rss_parse(txt, pdf.len); if (neps == 0) pod_note("Aucun \xC3\xA9pisode lisible dans ce flux"); }
        else { search_parse(txt, pdf.len); if (npsr == 0) pod_note("Aucun podcast trouv\xC3\xA9"); }
        free(txt);
    }
    hf_drop(&pdf);
    pf_state = PF_IDLE;
}

static void draw_list_podcast(void) {
    char buf[300], d[300], t[40];
    draw_panel(11, 73, 271, 385);
    int rows = pod_rows();
    snprintf(buf, sizeof(buf), "%d", rows - 1);
    draw_tabs(buf);
    int start = pod_sel - MUSIC_ROWS / 2;
    if (start > rows - MUSIC_ROWS) start = rows - MUSIC_ROWS;
    if (start < 0) start = 0;
    for (int i = 0; i < MUSIC_ROWS; i++) {
        int r = start + i;
        if (r >= rows) break;
        f32 ry = 114.0f + i * 24.0f, cy = ry + 12.0f;
        int is_sel = (r == pod_sel);
        if (is_sel) gx_rrect_fill(20, ry + 1, 244, 22, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        col_t txt = is_sel ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
        col_t dim = is_sel ? RGBA(235, 225, 255, 255) : RGBA(140, 125, 200, 255);
        const char *label;
        t[0] = 0;
        if (pod_view == 0) {
            if (r == 0) { label = "Rechercher un podcast..."; txt = is_sel ? txt : RGBA(190, 160, 255, 255); }
            else label = pods[r - 1].name;
        } else if (pod_view == 1) {
            if (r == 0) { snprintf(d, sizeof(d), "< %s", pod_open >= 0 ? pods[pod_open].name : ""); label = d; }
            else {
                ep_t *ep = &eps[r - 1];
                label = ep->title;
                if (ep->dur > 0) snprintf(t, sizeof(t), "%d min", (ep->dur + 30) / 60);
                if (pod_on && !strcmp(ep->url, pod_ep_url)) gx_tri(23, cy - 5, 23, cy + 5, 31, cy, RGBA(255, 255, 255, 255));
            }
        } else {
            if (r == 0) label = "< Abonnements";
            else label = psr[r - 1].name;
        }
        if (label != d) text_display(label, d, sizeof(d));
        int tw = t[0] ? text_width(F_SMALL, t, 0) + 6 : 0;
        if (t[0]) gx_text_r(F_SMALL, 260, cy + 4, t, dim);
        fit_text(F_MED, d, 222 - tw, buf, sizeof(buf));
        gx_text1(F_MED, 34, cy + 5, buf, txt);
    }
    gx_rect(25, 430, 241, 1, RGBA(76, 52, 150, 200));
    const char *foot;
    if (pf_state != PF_IDLE) foot = pf_state == PF_FEED ? "Chargement des \xC3\xA9pisodes..." : "Recherche...";
    else if (now_ms() - pod_msg_t < 4000 && pod_msg[0]) foot = pod_msg;
    else if (dl_active && !strcmp(dl_dest_dir_used, SD_ROOT "/podcasts")) {
        snprintf(buf, sizeof(buf), "T\xC3\xA9l\xC3\xA9" "chargement %ld %%", dl_total > 0 ? dl_got * 100 / dl_total : 0);
        foot = buf;
    } else foot = pod_view == 1 ? "A : \xC3\xA9" "couter   X : t\xC3\xA9l\xC3\xA9" "charger" :
                  pod_view == 2 ? "A : s'abonner   B : retour" : "A : ouvrir";
    fit_text(F_SMALL, foot, 240, d, sizeof(d));
    gx_text1(F_SMALL, 26, 447, d, RGBA(170, 155, 225, 255));
}

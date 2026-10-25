// GC-MP3 : onglet INFOS (inclus dans main.c)
// Heure, meteo (open-meteo.com, gratuit et sans compte), actualites (flux RSS)
// et reveil : a l'heure choisie, une radio ou ta musique demarre doucement.
#ifndef METEO_BASE
#define METEO_BASE "https://api.open-meteo.com"
#endif
#ifndef GEO_BASE
#define GEO_BASE "https://geocoding-api.open-meteo.com"
#endif
#define NEWS_DEFAULT "https://www.francetvinfo.fr/titres.rss"
#define NEWS_MAX 10

static char city[64] = "";                  /* reglage : ville */
static char news_url[256] = NEWS_DEFAULT;   /* reglage : flux des actualites */
static int alarm_on = 0, alarm_min = 7 * 60;   /* reveil : heure en minutes */
static int alarm_src = 0;                   /* 0 = musique au hasard, 1.. = station */
/* alarm_days declare dans main.c : 0 = tous les jours, 1 = semaine (lun-ven), 2 = week-end */
static const char *const alarm_days_names[3] = { "tous les jours", "du lundi au vendredi", "le week-end" };
static u64 alarm_snooze_at = 0;             /* v76 : report (+10 min) */
static int alarm_fired_day = -1;
static int alarm_fade = 256;                /* volume qui monte au reveil */
static u64 alarm_t0 = 0;

static hfetch_t ihf2 = { .c = { -1, 0, 0 } };
enum { IF_IDLE, IF_GEO, IF_METEO, IF_NEWS };
static int if_state = IF_IDLE;
static double geo_lat = 0, geo_lon = 0;
static char geo_name[96] = "";
static int meteo_ok = 0, meteo_code = -1, meteo_wind = 0, meteo_hum = 0;
static float meteo_temp = 0;
static int day_code[5], day_min[5], day_max[5], ndays = 0;
static char day_date[5][16];
static u64 meteo_t = 0, news_t = 0;
static char news_title[NEWS_MAX][160], news_link[NEWS_MAX][256], news_time[NEWS_MAX][8];
static int nnews = 0;
static int inf_sel = 0;
static char inf_msg[120];
static u64 inf_msg_t = 0;

static void inf_note(const char *m) { snprintf(inf_msg, sizeof(inf_msg), "%s", m); inf_msg_t = now_ms(); }

/* code meteo WMO -> texte */
static const char *meteo_text(int c) {
    if (c == 0) return "Ciel d\xC3\xA9gag\xC3\xA9";
    if (c <= 2) return "Peu nuageux";
    if (c == 3) return "Couvert";
    if (c == 45 || c == 48) return "Brouillard";
    if (c >= 51 && c <= 57) return "Bruine";
    if (c >= 61 && c <= 67) return "Pluie";
    if (c >= 71 && c <= 77) return "Neige";
    if (c >= 80 && c <= 82) return "Averses";
    if (c >= 85 && c <= 86) return "Averses de neige";
    if (c >= 95) return "Orage";
    return "";
}

/* petite icone meteo dessinee */
static void meteo_icon(f32 cx, f32 cy, f32 s, int c) {
    col_t sun = RGBA(255, 200, 70, 255), cloud = RGBA(220, 220, 240, 255), dark = RGBA(150, 150, 175, 255);
    int is_sun = c <= 2, has_cloud = c >= 1;
    if (is_sun) {
        f32 r = s * (c == 0 ? 0.32f : 0.24f);
        f32 sx = c == 0 ? cx : cx - s * 0.18f, sy = c == 0 ? cy : cy - s * 0.15f;
        for (int i = 0; i < 8; i++) {
            float a = i * PI_F / 4;
            gx_poly4(sx + cosf(a) * r * 1.3f, sy + sinf(a) * r * 1.3f, sx + cosf(a + 0.12f) * r * 1.3f, sy + sinf(a + 0.12f) * r * 1.3f,
                     sx + cosf(a + 0.06f) * r * 1.75f, sy + sinf(a + 0.06f) * r * 1.75f, sx + cosf(a + 0.06f) * r * 1.75f, sy + sinf(a + 0.06f) * r * 1.75f,
                     sun, sun, sun, sun);
        }
        gx_circle(sx, sy, r, sun, RGBA(255, 160, 40, 255));
    }
    if (has_cloud) {
        col_t cc = (c >= 51) ? dark : cloud;
        f32 by = cy + s * 0.08f;
        gx_circle(cx - s * 0.18f, by, s * 0.18f, cc, cc);
        gx_circle(cx + s * 0.05f, by - s * 0.1f, s * 0.24f, cc, cc);
        gx_circle(cx + s * 0.25f, by + s * 0.02f, s * 0.16f, cc, cc);
        gx_rect(cx - s * 0.18f, by, s * 0.43f, s * 0.18f, cc);
    }
    f32 ly = cy + s * 0.32f;
    if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82))
        for (int i = 0; i < 3; i++) gx_poly4(cx - s * 0.2f + i * s * 0.18f, ly, cx - s * 0.17f + i * s * 0.18f, ly,
                                            cx - s * 0.23f + i * s * 0.18f, ly + s * 0.16f, cx - s * 0.26f + i * s * 0.18f, ly + s * 0.16f,
                                            RGBA(110, 170, 255, 255), RGBA(110, 170, 255, 255), RGBA(110, 170, 255, 255), RGBA(110, 170, 255, 255));
    if ((c >= 71 && c <= 77) || c == 85 || c == 86)
        for (int i = 0; i < 3; i++) gx_circle(cx - s * 0.18f + i * s * 0.18f, ly + s * 0.08f, s * 0.04f, RGBA(255, 255, 255, 255), RGBA(255, 255, 255, 255));
    if (c >= 95) gx_poly4(cx, ly - s * 0.02f, cx + s * 0.1f, ly - s * 0.02f, cx - s * 0.02f, ly + s * 0.22f, cx - s * 0.04f, ly + s * 0.22f,
                          sun, sun, sun, sun);
    if (c == 45 || c == 48)
        for (int i = 0; i < 3; i++) gx_rect(cx - s * 0.3f, cy - s * 0.1f + i * s * 0.12f, s * 0.6f, s * 0.04f, cloud);
}

static void inf_fetch(int what, const char *url) {
    ihf2.image = 0;
    ihf2.maxbytes = 2 * 1024 * 1024;
    ihf2.accept = what == IF_NEWS ? "application/rss+xml,text/xml,*/*" : "application/json";
    hf_start(&ihf2, url, NULL);
    if_state = what;
}

static void meteo_start(void) {
    char url[URL_MAX], enc[200];
    if (!city[0]) return;
    if (geo_name[0] && geo_lat != 0) {
        snprintf(url, sizeof(url), METEO_BASE "/v1/forecast?latitude=%.4f&longitude=%.4f"
                 "&current=temperature_2m,weather_code,wind_speed_10m,relative_humidity_2m"
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=5", geo_lat, geo_lon);
        inf_fetch(IF_METEO, url);
        return;
    }
    if (!url_encode(city, enc, sizeof(enc))) return;
    snprintf(url, sizeof(url), GEO_BASE "/v1/search?name=%s&count=1&language=%s&format=json", enc, lang_code());
    inf_fetch(IF_GEO, url);
}

static void infos_set_city(const char *c) {
    snprintf(city, sizeof(city), "%s", c);
    geo_name[0] = 0;
    geo_lat = geo_lon = 0;
    meteo_ok = 0;
    meteo_t = 0;
    settings_dirty = 1;
}

static void news_parse(const char *x, int len) {
    nnews = 0;
    const char *end = x + len, *p = x;
    while (nnews < NEWS_MAX && (p = xml_find(p, end, "<item")) != NULL) {
        const char *e = xml_find(p, end, "</item>");
        if (!e) break;
        char d[64];
        xml_tag(p, e, "title", news_title[nnews], sizeof(news_title[0]));
        xml_tag(p, e, "link", news_link[nnews], sizeof(news_link[0]));
        news_time[nnews][0] = 0;
        if (xml_tag(p, e, "pubDate", d, sizeof(d))) {               /* "..., 20 Oct 2026 14:05:00 +0200" */
            const char *c = strchr(d, ':');
            if (c && c - d >= 2) snprintf(news_time[nnews], sizeof(news_time[0]), "%.5s", c - 2);
        }
        if (news_title[nnews][0]) nnews++;
        p = e + 7;
    }
}

static void meteo_parse(const char *js, int len) {
    int maxt = 4000;
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * maxt);
    if (!t) return;
    int n = json_tokens(js, len, t, maxt);
    char v[64];
    if (n > 0 && t[0].type == JT_OBJ) {
        int cur = jget(js, t, n, 0, "current");
        if (cur >= 0) {
            jstr(js, t, jget(js, t, n, cur, "temperature_2m"), v, sizeof(v)); meteo_temp = (float)atof(v);
            jstr(js, t, jget(js, t, n, cur, "weather_code"), v, sizeof(v)); meteo_code = v[0] ? atoi(v) : -1;
            jstr(js, t, jget(js, t, n, cur, "wind_speed_10m"), v, sizeof(v)); meteo_wind = (int)(atof(v) + 0.5);
            jstr(js, t, jget(js, t, n, cur, "relative_humidity_2m"), v, sizeof(v)); meteo_hum = atoi(v);
            meteo_ok = meteo_code >= 0;
        }
        int daily = jget(js, t, n, 0, "daily");
        ndays = 0;
        if (daily >= 0) {
            int tm = jget(js, t, n, daily, "time"), wc = jget(js, t, n, daily, "weather_code");
            int mx = jget(js, t, n, daily, "temperature_2m_max"), mn = jget(js, t, n, daily, "temperature_2m_min");
            if (tm >= 0 && wc >= 0 && mx >= 0 && mn >= 0)
                for (int i = 0; i < 5; i++) {
                    if (tm + 1 + i >= n || t[tm + 1 + i].start >= t[tm].end) break;
                    jstr(js, t, tm + 1 + i, day_date[i], sizeof(day_date[i]));
                    jstr(js, t, wc + 1 + i, v, sizeof(v)); day_code[i] = atoi(v);
                    jstr(js, t, mx + 1 + i, v, sizeof(v)); day_max[i] = (int)floorf((float)atof(v) + 0.5f);
                    jstr(js, t, mn + 1 + i, v, sizeof(v)); day_min[i] = (int)floorf((float)atof(v) + 0.5f);
                    ndays = i + 1;
                }
        }
    }
    free(t);
}

static void geo_parse(const char *js, int len) {
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * 4000);
    if (!t) return;
    int n = json_tokens(js, len, t, 4000);
    char v[96];
    int res = (n > 0 && t[0].type == JT_OBJ) ? jget(js, t, n, 0, "results") : -1;
    if (res >= 0 && t[res].type == JT_ARR && res + 1 < n && t[res + 1].start < t[res].end) {
        int r = res + 1;
        jstr(js, t, jget(js, t, n, r, "latitude"), v, sizeof(v)); geo_lat = atof(v);
        jstr(js, t, jget(js, t, n, r, "longitude"), v, sizeof(v)); geo_lon = atof(v);
        jstr(js, t, jget(js, t, n, r, "name"), geo_name, sizeof(geo_name));
    }
    free(t);
}

static void infos_enter(void) { if (!city[0]) inf_sel = 0; }

/* lignes de l'onglet : 0 ville, 1 reveil, 2 ce que joue le reveil, puis les actualites */
static void infos_input(u32 down, u32 nav, int fire) {
    int rows = 4 + nnews;
    if (fire && (nav & PAD_BUTTON_DOWN)) inf_sel = (inf_sel + 1) % rows;
    if (fire && (nav & PAD_BUTTON_UP)) inf_sel = (inf_sel + rows - 1) % rows;
    int lr = (fire && (nav & PAD_BUTTON_RIGHT)) ? 1 : (fire && (nav & PAD_BUTTON_LEFT)) ? -1 : 0;
    if (inf_sel == 1 && lr) { alarm_min = (alarm_min + lr * 5 + 1440) % 1440; alarm_on = 1; settings_dirty = 1; }
    if (inf_sel == 2 && lr) { alarm_days = (alarm_days + lr + 3) % 3; settings_dirty = 1; }
    if (inf_sel == 3 && lr) { alarm_src = (alarm_src + lr + nstations + 1) % (nstations + 1); settings_dirty = 1; }
    if (down & PAD_BUTTON_A) {
        if (inf_sel == 0) kb_start(KB_CITY, city, -1, -1);
        else if (inf_sel == 1) { alarm_on = !alarm_on; settings_dirty = 1; }
        else if (inf_sel >= 4 && inf_sel - 4 < nnews && news_link[inf_sel - 4][0]) {   /* l'article dans l'onglet WEB */
            tab = TAB_WEB;
            web_enter_tab();
            web_fetch(news_link[inf_sel - 4], NAV_PUSH, 0);
        }
    }
    if (down & PAD_BUTTON_X) { meteo_t = news_t = 0; inf_note("Actualisation..."); }
    if ((down & PAD_BUTTON_Y) && src == SRC_FILE && playing) paused = !paused;
}

/* reveil : a l'heure choisie, une fois par jour */
static void alarm_fire(void);
static void alarm_check(void) {
    if (alarm_t0) {                                       /* volume qui monte tout doucement (3 min) */
        u64 dt = now_ms() - alarm_t0;
        float x = dt >= 180000 ? 1.0f : (float)dt / 180000.0f;
        alarm_fade = 8 + (int)(248.0f * x * x);
        if (dt >= 180000) { alarm_t0 = 0; alarm_fade = 256; }
    }
    if (alarm_snooze_at && now_ms() >= alarm_snooze_at) { alarm_snooze_at = 0; alarm_fire(); return; }   /* v76 : report */
    if (!alarm_on) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (!tm || tm->tm_year < 100) return;
    if (tm->tm_hour * 60 + tm->tm_min != alarm_min || tm->tm_yday == alarm_fired_day) return;
    int wd = tm->tm_wday;                                   /* 0 = dimanche */
    int weekend = (wd == 0 || wd == 6);
    if ((alarm_days == 1 && weekend) || (alarm_days == 2 && !weekend)) { alarm_fired_day = tm->tm_yday; return; }   /* pas aujourd'hui */
    alarm_fired_day = tm->tm_yday;
    alarm_fire();
}
static void alarm_fire(void) {
    saver_on = 0;
    party_on = 0;
    last_input_ms = now_ms();
    alarm_t0 = now_ms();
    alarm_fade = 8;
    if (alarm_src > 0 && alarm_src <= nstations) { net_start_async(); start_station(alarm_src - 1, 0); tab = TAB_RADIO; }
    else if (nfiles > 0) {
        set_shuffle(1);
        int k = (int)(lib_rand() % (u32)nfiles);
        for (int i = 0; i < nfiles && is_retro[k]; i++) k = (k + 1) % nfiles;
        queue_build(k);
        start_track(k);
        tab = TAB_MUSIC;
    }
    wake_start();                                         /* l'heure et la meteo en grand */
}

static void infos_pump(void) {
    alarm_check();
    if (if_state == IF_IDLE) {
        if (net_phase != NET_DONE || !net_ok) return;
        u64 now = now_ms();
        if (tab != TAB_INFO && !(meteo_t == 0 && city[0])) return;   /* (seulement quand l'onglet est ouvert) */
        if (city[0] && (meteo_t == 0 || now - meteo_t > 30 * 60000)) { meteo_t = now; meteo_start(); return; }
        if (tab == TAB_INFO && (news_t == 0 || now - news_t > 15 * 60000)) { news_t = now; inf_fetch(IF_NEWS, news_url); return; }
        return;
    }
    hf_pump(&ihf2);
    if (ihf2.st == HF_FAIL) {
        inf_note(if_state == IF_NEWS ? "Actualit\xC3\xA9s indisponibles" : "M\xC3\xA9t\xC3\xA9o indisponible");
        if_state = IF_IDLE;
        hf_drop(&ihf2);
        return;
    }
    if (ihf2.st != HF_DONE) return;
    ihf2.st = HF_IDLE;
    char *txt = (char *)malloc(ihf2.len + 1);
    int st = if_state;
    if_state = IF_IDLE;
    if (txt) {
        memcpy(txt, ihf2.buf, ihf2.len);
        txt[ihf2.len] = 0;
        if (st == IF_GEO) {
            geo_parse(txt, ihf2.len);
            if (geo_name[0]) { free(txt); hf_drop(&ihf2); meteo_start(); return; }
            inf_note("Ville introuvable");
        } else if (st == IF_METEO) meteo_parse(txt, ihf2.len);
        else news_parse(txt, ihf2.len);
        free(txt);
    }
    hf_drop(&ihf2);
}

static void draw_tab_infos(void) {
    char buf[300], d[300];
    draw_panel(11, 73, 618, 338);
    draw_tabs(NULL);
    /* heure et date */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) {
        static const char *const jours[7] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };
        static const char *const mois[12] = { "janvier", "f\xC3\xA9vrier", "mars", "avril", "mai", "juin", "juillet",
                                              "ao\xC3\xBBt", "septembre", "octobre", "novembre", "d\xC3\xA9" "cembre" };
        snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
        txt_scale = 2.2f;
        gx_text1(F_LARGE, 32, 176, buf, RGBA(255, 255, 255, 255));
        txt_scale = 1.0f;
        snprintf(buf, sizeof(buf), "%s %d %s", jours[tm->tm_wday % 7], tm->tm_mday, mois[tm->tm_mon % 12]);
        gx_text1(F_MED, 34, 200, buf, RGBA(200, 185, 250, 255));
    }
    /* meteo */
    if (meteo_ok) {
        meteo_icon(70, 252, 64, meteo_code);
        snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)floorf(meteo_temp + 0.5f));
        txt_scale = 1.6f;
        gx_text1(F_LARGE, 112, 266, buf, RGBA(255, 255, 255, 255));
        txt_scale = 1.0f;
        text_display(geo_name, d, sizeof(d));
        gx_text1(F_MED, 190, 246, d, RGBA(220, 210, 255, 255));
        gx_text1(F_SMALL, 190, 264, meteo_text(meteo_code), RGBA(180, 165, 235, 255));
        snprintf(buf, sizeof(buf), "Vent %d km/h   Humidit\xC3\xA9 %d %%", meteo_wind, meteo_hum);
        gx_text1(F_SMALL, 40, 300, buf, RGBA(160, 145, 215, 255));
        static const char *const js[7] = { "dim", "lun", "mar", "mer", "jeu", "ven", "sam" };
        for (int i = 0; i < ndays; i++) {
            f32 x = 46 + i * 52;
            int y = 0, m = 0, dd = 0;
            if (sscanf(day_date[i], "%d-%d-%d", &y, &m, &dd) == 3) {
                struct tm t2;
                memset(&t2, 0, sizeof(t2));
                t2.tm_year = y - 1900; t2.tm_mon = m - 1; t2.tm_mday = dd; t2.tm_hour = 12;
                mktime(&t2);
                gx_text_c(F_SMALL, x, 322, i == 0 ? "auj." : js[t2.tm_wday % 7], RGBA(170, 155, 225, 255));
            }
            meteo_icon(x, 344, 26, day_code[i]);
            snprintf(buf, sizeof(buf), "%d/%d", day_min[i], day_max[i]);
            gx_text_c(F_SMALL, x, 374, buf, RGBA(210, 200, 245, 255));
        }
    } else {
        gx_text1(F_MED, 34, 250, city[0] ? (if_state != IF_IDLE ? "M\xC3\xA9t\xC3\xA9o..." : "M\xC3\xA9t\xC3\xA9o indisponible")
                                       : "M\xC3\xA9t\xC3\xA9o : choisis ta ville (A)", RGBA(255, 210, 120, 255));
    }
    /* reglages et actualites, a droite */
    gx_rect(306, 120, 1, 270, RGBA(76, 52, 150, 200));
    const f32 rx = 318, rw = 296;
    for (int i = 0; i < 4 + nnews; i++) {
        f32 ry = i < 4 ? 118 + i * 24 : 216 + (i - 4) * 21;
        if (ry > 382) break;
        int on = (i == inf_sel);
        gp_zone(rx - 6, ry + 1, rw + 6, 21, &inf_sel, i, GPZ_SEL);
        if (on) gx_rrect_fill(rx - 6, ry + 1, rw + 6, 21, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        col_t c = on ? RGBA(255, 255, 255, 255) : RGBA(210, 200, 240, 255);
        if (i == 0) snprintf(buf, sizeof(buf), "Ville : %s", city[0] ? city : "(choisir)");
        else if (i == 1) { if (alarm_on) snprintf(buf, sizeof(buf), "R\xC3\xA9veil : %02d:%02d", alarm_min / 60, alarm_min % 60); else snprintf(buf, sizeof(buf), "R\xC3\xA9veil : non"); }
        else if (i == 2) snprintf(buf, sizeof(buf), "Jours : %s", TR(alarm_days_names[alarm_days % 3]));
        else if (i == 3) snprintf(buf, sizeof(buf), "Le r\xC3\xA9veil joue : %s", alarm_src > 0 && alarm_src <= nstations ? stations[alarm_src - 1].name : TR("ma musique (al\xC3\xA9" "atoire)"));
        else {
            text_display(news_title[i - 4], d, sizeof(d));
            fit_text_sel(on, F_SMALL, d, (int)rw - 46, buf, sizeof(buf));
            gx_text1(F_SMALL, rx + 40, ry + 15, buf, c);
            gx_text1(F_SMALL, rx, ry + 15, news_time[i - 4], on ? c : RGBA(150, 135, 210, 255));
            continue;
        }
        text_display(buf, d, sizeof(d));
        fit_text_sel(on, F_MED, d, (int)rw, buf, sizeof(buf));
        gx_text1(F_MED, rx, ry + 17, buf, c);
        if (on && (i == 1 || i == 2 || i == 3)) {
            gx_tri(rx + rw - 18, ry + 6, rx + rw - 18, ry + 16, rx + rw - 12, ry + 11, RGBA(255, 255, 255, 200));
            gx_tri(rx + rw - 24, ry + 6, rx + rw - 24, ry + 16, rx + rw - 30, ry + 11, RGBA(255, 255, 255, 200));
        }
    }
    gx_text(F_SMALL, rx, 210, "ACTUALIT\xC3\x89S", RGBA(140, 122, 210, 255), RGBA(140, 122, 210, 255), 3);
    if (nnews == 0) gx_text1(F_SMALL, rx, 230, if_state == IF_NEWS ? "Chargement..." : "Pas encore d'actualit\xC3\xA9s", RGBA(170, 155, 225, 255));
    if (now_ms() - inf_msg_t < 3000) gx_text1(F_SMALL, 34, 398, inf_msg, RGBA(255, 210, 120, 255));
    static const char *const k[] = { "A", "X", "Y" };
    const char *ll[3] = { inf_sel >= 3 ? "LIRE" : "CHOISIR", "ACTUALISER", "PAUSE" };
    if (inf_sel == 1 || inf_sel == 2) ll[0] = inf_sel == 1 ? "OUI / NON" : "CHOISIR";
    draw_legend_keys(k, ll, 3);
}

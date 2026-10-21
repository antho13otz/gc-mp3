// Mode veilleuse : grande horloge tres peu lumineuse, la musique ou la radio
// continue. START > Mode veilleuse (ou F2 sur le clavier GameCube).
//   haut / bas : luminosite (5 niveaux)   X : minuterie   Y : pause / muet
//   L / R : morceau ou radio precedent / suivant           B ou START : sortir

static void night_start(void) {
    night_on = 1;
    party_on = 0;
    saver_on = 0;
    crumb("veilleuse");
    /* rien ne joue : la derniere radio (ou la premiere de la liste) */
    if (night_radio && src == SRC_NONE && nstations > 0 && net_ok) {
        int st = cur_station >= 0 && cur_station < nstations ? cur_station : 0;
        start_station(st, 0);
    } else if (night_radio && src == SRC_NONE && nstations > 0) {
        net_start_async();
        night_pending_radio = 1;
    }
}

static void night_pump(void) {
    if (!night_on || !night_pending_radio) return;
    if (net_phase == NET_DONE) {
        night_pending_radio = 0;
        if (net_ok && src == SRC_NONE && nstations > 0)
            start_station(cur_station >= 0 && cur_station < nstations ? cur_station : 0, 0);
    }
}

static void night_input(u32 down, u32 nav, int fire) {
    if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) { night_on = 0; settings_dirty = 1; return; }
    if (fire && (nav & PAD_BUTTON_UP) && night_lum < 5) { night_lum++; night_hint_until = now_ms() + 2000; }
    if (fire && (nav & PAD_BUTTON_DOWN) && night_lum > 1) { night_lum--; night_hint_until = now_ms() + 2000; }
    if (down & PAD_BUTTON_X) {
        static const int sl[] = { 0, 15, 30, 45, 60, 90, 120 };
        int k = 0;
        for (int j = 0; j < 7; j++) if (sl[j] == sleep_min) k = j;
        sleep_min = sl[(k + 1) % 7];
        sleep_at = sleep_min ? now_ms() + (u64)sleep_min * 60000 : 0;
        sleep_fade = 256;
        night_hint_until = now_ms() + 2500;
    }
    if (down & PAD_BUTTON_Y) {
        if (src == SRC_FILE && playing) paused = !paused;
        else if (src == SRC_RADIO) muted = !muted;
    }
    if (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) transport_step((down & PAD_TRIGGER_R) ? 1 : -1);
}

static void draw_night(void) {
    scr_viewport();
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    gx_rect(0, 0, 640, 480, RGBA(0, 0, 0, 255));
    static const u8 lum[6] = { 0, 26, 48, 80, 130, 200 };
    u8 a = lum[night_lum < 1 ? 1 : night_lum > 5 ? 5 : night_lum];
    /* tres lent deplacement (pas d'image fixe sur l'ecran) */
    f32 t = (f32)(now_ms() % 3600000) / 1000.0f;
    f32 cx = 320 + 70 * sinf(t * 0.011f), cy = 200 + 40 * sinf(t * 0.008f + 1.1f);
    char hm[16], date[64], ti[200], sub[200];
    int sec = saver_clock(hm, sizeof(hm), date, sizeof(date));
    const f32 dw = 84, dh = 150, gap = 18, colon = 44;
    const f32 total = 4 * dw + 2 * gap + colon;
    f32 x = cx - total / 2, y = cy - dh / 2;
    col_t top = RGBA(255, 120, 60, a), bot = RGBA(200, 60, 30, a);    /* orange : moins fatigant la nuit */
    if (hm[0]) {
        seg_digit(x, y, dw, dh, hm[0] - '0', top, bot);
        seg_digit(x + dw + gap, y, dw, dh, hm[1] - '0', top, bot);
        f32 kx = x + 2 * dw + gap + colon / 2 - 6;
        u8 ka = (sec & 1) ? a / 3 : a;
        gx_rrect_fill(kx, y + dh * 0.30f, 12, 12, 4, RGBA(255, 120, 60, ka), RGBA(200, 60, 30, ka));
        gx_rrect_fill(kx, y + dh * 0.62f, 12, 12, 4, RGBA(255, 120, 60, ka), RGBA(200, 60, 30, ka));
        seg_digit(x + 2 * dw + gap + colon, y, dw, dh, hm[3] - '0', top, bot);
        seg_digit(x + 3 * dw + 2 * gap + colon, y, dw, dh, hm[4] - '0', top, bot);
    }
    u8 ta = (u8)(a * 3 / 4);
    saver_line(F_MED, cx, y + dh + 40, date, 520, RGBA(230, 140, 90, ta));
    saver_title(ti, sizeof(ti), sub, sizeof(sub));
    if (ti[0]) saver_line(F_SMALL, cx, y + dh + 66, ti, 520, RGBA(210, 120, 80, ta));
    if (sleep_at) {
        char m[64];
        u64 n = now_ms();
        snprintf(m, sizeof(m), "Arr\xC3\xAAt dans %d min", n >= sleep_at ? 0 : (int)((sleep_at - n + 59999) / 60000));
        saver_line(F_SMALL, cx, y + dh + 86, m, 520, RGBA(210, 120, 80, ta));
    }
    if (now_ms() < night_hint_until) {
        char h[120];
        snprintf(h, sizeof(h), "Luminosit\xC3\xA9 %d / 5 (haut / bas)   X : minuterie   B : sortir", night_lum);
        gx_text_c(F_SMALL, 320, 456, h, RGBA(220, 130, 90, 200));
    }
    service_audio();
    present_frame();
}

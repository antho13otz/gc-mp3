// GC Station v203 : musiques MIDI (.mid, .midi, .kar, .rmi) - lecteur et petit synthetiseur integres
// (inclus dans main.c, avant tab_retro.c). Pas de banque de sons a fournir : chaque famille d'instruments
// General MIDI (piano, orgue, guitare, basse, cordes, cuivres, flutes, synthes...) a sa forme d'onde et son
// enveloppe ; la batterie (canal 10) est synthetisee (grosse caisse, caisse claire, charleston, toms, cymbales).
// 32 voix, 48 kHz, volume / expression / panoramique / pedale / pitch bend par canal.

#define MI_RATE      48000
#define MI_VOICES    32
#define MI_MAX_EV    300000
#define MI_TBL       256

typedef struct { u32 t; u8 st, d1, d2, pad; } mi_ev_t;      /* t : en echantillons a 48 kHz */
typedef struct {
    u8 on, ch, note, vel, drum, rel, sus_held, wave;
    u32 phase, inc;                             /* phase 16.16 dans la table (256) */
    s32 env;                                    /* 0..1<<24 */
    s32 att, dec, sus, relr;                    /* pas par echantillon, niveau de maintien */
    u8 stage;                                   /* 0 attaque, 1 decroissance, 2 maintien, 3 relache */
    u32 age;
    /* batterie */
    u32 lfsr; s32 lp; s32 tone_inc_end; u8 dkind;
} mi_voice_t;
typedef struct { u8 prog, vol, expr, pan, sus; s16 bend; } mi_chan_t;

static mi_ev_t *mi_ev; static int mi_nev, mi_pos;
static u32 mi_now, mi_len;
static mi_voice_t mi_v[MI_VOICES];
static mi_chan_t mi_ch[16];
static s16 mi_tbl[8][MI_TBL];                   /* formes d'onde */
static u32 mi_note_inc[128];
static int mi_tbl_ok = 0;
static int mi_on = 0;
static char mi_title[96];

enum { MW_SINE, MW_SQUARE, MW_SAW, MW_TRI, MW_ORGAN, MW_PIANO, MW_SOFTSAW, MW_PULSE };
/* familles General MIDI (programme / 8) : onde, attaque, decroissance (ms), maintien (0..100), relache (ms), percussif */
static const struct { u8 wave; u16 a, d; u8 s; u16 r; u8 perc; } mi_fam[16] = {
    { MW_PIANO,   4,  900, 20,  300, 1 },        /* pianos */
    { MW_SINE,    2,  500,  0,  300, 1 },        /* percussions chromatiques (celesta, vibraphone, xylophone...) */
    { MW_ORGAN,  10,   50, 90,   80, 0 },        /* orgues */
    { MW_SAW,     3,  700, 10,  200, 1 },        /* guitares */
    { MW_TRI,     4,  600, 40,  150, 0 },        /* basses */
    { MW_SOFTSAW, 80, 200, 85,  400, 0 },        /* cordes */
    { MW_SOFTSAW, 120, 300, 80, 600, 0 },        /* ensembles, choeurs */
    { MW_SAW,    30,  200, 75,  200, 0 },        /* cuivres */
    { MW_SQUARE, 20,  200, 75,  150, 0 },        /* anches */
    { MW_SINE,   40,  200, 80,  200, 0 },        /* flutes */
    { MW_SQUARE,  5,  200, 70,  150, 0 },        /* synthe solo */
    { MW_SOFTSAW, 300, 500, 80, 900, 0 },        /* nappes */
    { MW_TRI,    50,  500, 60,  600, 0 },        /* effets */
    { MW_SAW,     3,  800, 10,  300, 1 },        /* ethniques (sitar, banjo...) */
    { MW_TRI,     2,  300,  0,  200, 1 },        /* percussions */
    { MW_TRI,    10,  400, 40,  300, 0 },        /* bruitages */
};

static void mi_tables(void) {
    if (mi_tbl_ok) return;
    for (int i = 0; i < MI_TBL; i++) {
        f32 ph = (f32)i / MI_TBL, s = sinf(2 * 3.14159265f * ph);
        mi_tbl[MW_SINE][i] = (s16)(s * 30000);
        mi_tbl[MW_SQUARE][i] = (s16)(ph < 0.5f ? 22000 : -22000);
        mi_tbl[MW_PULSE][i] = (s16)(ph < 0.25f ? 22000 : -22000);
        mi_tbl[MW_TRI][i] = (s16)((ph < 0.5f ? ph * 4 - 1 : 3 - ph * 4) * 30000);
        /* scie avec moins d'harmoniques (sinon : crissements) ; douce : encore moins */
        f32 sw = 0, ssw = 0, org = 0, pia = 0;
        for (int h = 1; h <= 12; h++) sw += sinf(2 * 3.14159265f * ph * h) / h;
        for (int h = 1; h <= 6; h++) ssw += sinf(2 * 3.14159265f * ph * h) / (h * h * 0.6f + 0.4f);
        org = sinf(2 * 3.14159265f * ph) + 0.5f * sinf(4 * 3.14159265f * ph) + 0.3f * sinf(8 * 3.14159265f * ph) + 0.2f * sinf(6 * 3.14159265f * ph);
        pia = sinf(2 * 3.14159265f * ph) + 0.45f * sinf(4 * 3.14159265f * ph) + 0.25f * sinf(6 * 3.14159265f * ph) + 0.12f * sinf(8 * 3.14159265f * ph) + 0.06f * sinf(10 * 3.14159265f * ph);
        mi_tbl[MW_SAW][i] = (s16)(sw * 17000);
        mi_tbl[MW_SOFTSAW][i] = (s16)(ssw * 17000);
        mi_tbl[MW_ORGAN][i] = (s16)(org * 15000);
        mi_tbl[MW_PIANO][i] = (s16)(pia * 16000);
    }
    for (int n = 0; n < 128; n++) {
        f64 f = 440.0 * pow(2.0, (n - 69) / 12.0);
        mi_note_inc[n] = (u32)(f * MI_TBL * 65536.0 / MI_RATE);
    }
    mi_tbl_ok = 1;
}

static u32 mi_vlq(const u8 *d, u32 n, u32 *p) {
    u32 v = 0;
    for (int i = 0; i < 4 && *p < n; i++) { u8 c = d[(*p)++]; v = (v << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
    return v;
}
typedef struct { u32 tick; u8 st, d1, d2, trk; u32 tempo; } mi_raw_t;   /* tempo : evenement de tempo (us par noire) */
static void midi_close(void) {
    free(mi_ev); mi_ev = NULL; mi_nev = 0; mi_pos = 0; mi_on = 0;
}

/* fichier -> evenements dates (en echantillons) ; 0 si illisible */
static int mi_parse(const u8 *d, u32 n) {
    u32 p = 0;
    if (n >= 20 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "RMID", 4)) {         /* .rmi : chercher le bloc "data" */
        u32 q = 12;
        while (q + 8 <= n) { u32 l = (u32)d[q + 4] | d[q + 5] << 8 | d[q + 6] << 16 | (u32)d[q + 7] << 24; if (!memcmp(d + q, "data", 4)) { d += q + 8; n = l < n - q - 8 ? l : n - q - 8; break; } q += 8 + l + (l & 1); }
    }
    if (n < 14 || memcmp(d, "MThd", 4)) return 0;
    u32 hl = (u32)d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7];
    int fmt = d[8] << 8 | d[9], ntrk = d[10] << 8 | d[11];
    int div = d[12] << 8 | d[13];
    (void)fmt;
    if (div == 0) return 0;
    p = 8 + hl;
    int cap = 4096, nr = 0;
    mi_raw_t *raw = (mi_raw_t *)malloc(sizeof(mi_raw_t) * cap);
    if (!raw) return 0;
    mi_title[0] = 0;
    for (int t = 0; t < ntrk && t < 256 && p + 8 <= n; t++) {
        if (memcmp(d + p, "MTrk", 4)) { u32 l = (u32)d[p + 4] << 24 | d[p + 5] << 16 | d[p + 6] << 8 | d[p + 7]; p += 8 + l; t--; if (l > n) break; continue; }
        u32 l = (u32)d[p + 4] << 24 | d[p + 5] << 16 | d[p + 6] << 8 | d[p + 7];
        u32 e = p + 8 + l; if (e > n) e = n;
        u32 q = p + 8, tick = 0;
        u8 rs = 0;
        while (q < e) {
            tick += mi_vlq(d, e, &q);
            if (q >= e) break;
            u8 st = d[q];
            if (st >= 0x80) q++; else st = rs;
            if (st == 0xFF) {                   /* meta */
                if (q >= e) break;
                u8 ty = d[q++];
                u32 ml = mi_vlq(d, e, &q);
                if (ty == 0x51 && ml == 3 && q + 3 <= e) {
                    if (nr >= cap) { cap *= 2; mi_raw_t *nr2 = (mi_raw_t *)realloc(raw, sizeof(mi_raw_t) * cap); if (!nr2) break; raw = nr2; }
                    raw[nr].tick = tick; raw[nr].st = 0xFF; raw[nr].d1 = raw[nr].d2 = 0; raw[nr].trk = (u8)t;
                    raw[nr].tempo = (u32)d[q] << 16 | d[q + 1] << 8 | d[q + 2]; nr++;
                } else if ((ty == 0x03 || ty == 0x01) && !mi_title[0] && t <= 1 && ml > 1 && ml < 90 && q + ml <= e) {
                    int k = 0; for (u32 i = 0; i < ml && k < 90; i++) { u8 c = d[q + i]; if (c >= 32 && c < 127) mi_title[k++] = (char)c; } mi_title[k] = 0;
                } else if (ty == 0x2F) { q = e; break; }
                q += ml;
                continue;
            }
            if (st == 0xF0 || st == 0xF7) { u32 sl = mi_vlq(d, e, &q); q += sl; continue; }
            if (st < 0x80) break;                /* (fichier abime) */
            rs = st;
            int nd = (st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0 ? 1 : 2;
            if (q + nd > e) break;
            u8 a = d[q], b = nd > 1 ? d[q + 1] : 0;
            q += nd;
            int ty = st & 0xF0;
            if (ty == 0xA0 || ty == 0xD0) continue;   /* (pression : ignoree) */
            if (nr >= cap) { if (cap >= MI_MAX_EV) break; cap *= 2; mi_raw_t *nr2 = (mi_raw_t *)realloc(raw, sizeof(mi_raw_t) * cap); if (!nr2) break; raw = nr2; }
            raw[nr].tick = tick; raw[nr].st = st; raw[nr].d1 = a & 0x7F; raw[nr].d2 = b & 0x7F; raw[nr].trk = (u8)t; raw[nr].tempo = 0; nr++;
        }
        p = p + 8 + l;
    }
    /* tri stable par tick (les pistes gardent leur ordre) */
    {   /* tri par fusion (stable) */
        mi_raw_t *tmp = (mi_raw_t *)malloc(sizeof(mi_raw_t) * (nr > 0 ? nr : 1));
        if (!tmp) { free(raw); return 0; }
        for (int w = 1; w < nr; w *= 2)
            for (int a = 0; a < nr; a += 2 * w) {
                int m = a + w < nr ? a + w : nr, bb = a + 2 * w < nr ? a + 2 * w : nr, i = a, j = m, k = a;
                while (i < m && j < bb) tmp[k++] = raw[j].tick < raw[i].tick ? raw[j++] : raw[i++];
                while (i < m) tmp[k++] = raw[i++];
                while (j < bb) tmp[k++] = raw[j++];
                memcpy(raw + a, tmp + a, sizeof(mi_raw_t) * (bb - a));
            }
        free(tmp);
    }
    mi_ev = (mi_ev_t *)malloc(sizeof(mi_ev_t) * (nr > 0 ? nr : 1));
    if (!mi_ev) { free(raw); return 0; }
    /* ticks -> echantillons (changements de tempo) */
    f64 us_per_q = 500000.0, tsec = 0;
    u32 last_tick = 0;
    f64 tick_s = div & 0x8000 ? 1.0 / ((f64)(-(s8)(div >> 8)) * (div & 0xFF)) : us_per_q / 1e6 / div;
    mi_nev = 0;
    for (int i = 0; i < nr; i++) {
        tsec += (raw[i].tick - last_tick) * tick_s;
        last_tick = raw[i].tick;
        if (raw[i].st == 0xFF) {
            if (!(div & 0x8000)) { us_per_q = raw[i].tempo ? raw[i].tempo : 500000; tick_s = us_per_q / 1e6 / div; }
            continue;
        }
        mi_ev_t *ev = &mi_ev[mi_nev++];
        ev->t = (u32)(tsec * MI_RATE);
        ev->st = raw[i].st; ev->d1 = raw[i].d1; ev->d2 = raw[i].d2;
        if (tsec > 3600) break;                  /* (une heure au plus) */
    }
    free(raw);
    mi_len = mi_nev ? mi_ev[mi_nev - 1].t + MI_RATE * 2 : 0;
    return mi_nev > 0;
}

static void mi_reset(void) {
    memset(mi_v, 0, sizeof(mi_v));
    for (int c = 0; c < 16; c++) { mi_ch[c].prog = 0; mi_ch[c].vol = 100; mi_ch[c].expr = 127; mi_ch[c].pan = 64; mi_ch[c].sus = 0; mi_ch[c].bend = 0; }
    mi_pos = 0; mi_now = 0;
}
static u32 mi_bend_inc(u32 inc, s16 bend) {     /* +-2 demi-tons */
    if (!bend) return inc;
    f32 k = powf(2.0f, (bend / 8192.0f) * 2.0f / 12.0f);
    return (u32)(inc * k);
}
static s32 mi_step(u32 ms, s32 span) { u32 n = ms * (MI_RATE / 1000); if (n < 1) n = 1; s32 s = span / (s32)n; return s > 0 ? s : 1; }
static void mi_note_on(int ch, int note, int vel) {
    /* voix libre, sinon la plus vieille en relache, sinon la plus vieille */
    int best = -1; u32 bage = 0;
    for (int i = 0; i < MI_VOICES; i++) if (!mi_v[i].on) { best = i; break; }
    if (best < 0) for (int i = 0; i < MI_VOICES; i++) if (mi_v[i].stage == 3 && mi_v[i].age >= bage) { best = i; bage = mi_v[i].age; }
    if (best < 0) for (int i = 0; i < MI_VOICES; i++) if (mi_v[i].age >= bage) { best = i; bage = mi_v[i].age; }
    mi_voice_t *v = &mi_v[best];
    memset(v, 0, sizeof(*v));
    v->on = 1; v->ch = (u8)ch; v->note = (u8)note; v->vel = (u8)vel;
    if (ch == 9) {                               /* batterie */
        v->drum = 1;
        int k = note;
        u8 kind = (k == 35 || k == 36) ? 1 : (k == 38 || k == 40 || k == 37 || k == 39) ? 2 : (k == 42 || k == 44) ? 3 : (k == 46) ? 4 :
                  (k == 49 || k == 57 || k == 52 || k == 55 || k == 51 || k == 59 || k == 53) ? 5 : (k == 41 || k == 43 || k == 45 || k == 47 || k == 48 || k == 50) ? 6 : 7;
        v->dkind = kind;
        v->lfsr = 0xACE1u ^ (u32)note * 2654435761u;
        u32 base = kind == 1 ? mi_note_inc[36] : kind == 6 ? mi_note_inc[k + 5] : mi_note_inc[60];
        v->inc = kind == 1 ? base * 3 : base;
        v->tone_inc_end = (s32)base;
        u16 dec = kind == 1 ? 260 : kind == 2 ? 180 : kind == 3 ? 60 : kind == 4 ? 300 : kind == 5 ? 900 : kind == 6 ? 300 : 120;
        v->env = 1 << 24; v->stage = 1; v->dec = mi_step(dec, 1 << 24); v->sus = 0; v->relr = v->dec;
        return;
    }
    const mi_chan_t *c = &mi_ch[ch];
    int fam = c->prog >> 3;
    v->wave = mi_fam[fam].wave;
    if (c->prog >= 80 && c->prog <= 81) v->wave = c->prog == 80 ? MW_SQUARE : MW_SAW;
    if (c->prog == 19 || c->prog == 16) v->wave = MW_ORGAN;
    v->inc = mi_bend_inc(mi_note_inc[note], c->bend);
    v->att = mi_step(mi_fam[fam].a, 1 << 24);
    s32 suslev = (s32)((1 << 24) / 100 * mi_fam[fam].s);
    v->sus = suslev;
    v->dec = mi_step(mi_fam[fam].d + (mi_fam[fam].perc ? (127 - note) * 12 : 0), (1 << 24) - suslev > 0 ? (1 << 24) - suslev : 1);
    v->relr = mi_step(mi_fam[fam].r, 1 << 24);
    v->stage = 0;
}
static void mi_note_off(int ch, int note) {
    for (int i = 0; i < MI_VOICES; i++) {
        mi_voice_t *v = &mi_v[i];
        if (!v->on || v->ch != ch || v->note != note || v->stage == 3 || v->drum) continue;
        if (mi_ch[ch].sus) v->sus_held = 1; else v->stage = 3;
    }
}
static void mi_event(const mi_ev_t *e) {
    int ch = e->st & 15, ty = e->st & 0xF0;
    switch (ty) {
    case 0x90: if (e->d2) { mi_note_on(ch, e->d1, e->d2); break; }   /* (vitesse 0 : relache) */
    /* fallthrough */
    case 0x80: mi_note_off(ch, e->d1); break;
    case 0xC0: mi_ch[ch].prog = e->d1; break;
    case 0xE0: {
        mi_ch[ch].bend = (s16)(((e->d2 << 7) | e->d1) - 8192);
        for (int i = 0; i < MI_VOICES; i++) if (mi_v[i].on && mi_v[i].ch == ch && !mi_v[i].drum) mi_v[i].inc = mi_bend_inc(mi_note_inc[mi_v[i].note], mi_ch[ch].bend);
        break;
    }
    case 0xB0:
        switch (e->d1) {
        case 7: mi_ch[ch].vol = e->d2; break;
        case 10: mi_ch[ch].pan = e->d2; break;
        case 11: mi_ch[ch].expr = e->d2; break;
        case 64:
            mi_ch[ch].sus = e->d2 >= 64;
            if (!mi_ch[ch].sus) for (int i = 0; i < MI_VOICES; i++) if (mi_v[i].on && mi_v[i].ch == ch && mi_v[i].sus_held) { mi_v[i].sus_held = 0; mi_v[i].stage = 3; }
            break;
        case 120: case 123:                     /* tout s'arrete */
            for (int i = 0; i < MI_VOICES; i++) if (mi_v[i].on && mi_v[i].ch == ch) mi_v[i].stage = 3;
            break;
        case 121: mi_ch[ch].vol = 100; mi_ch[ch].expr = 127; mi_ch[ch].pan = 64; mi_ch[ch].bend = 0; mi_ch[ch].sus = 0; break;
        }
        break;
    }
}
/* n echantillons stereo ; 0 a la fin du morceau */
static int midi_render(s16 *L, s16 *R, int n) {
    if (!mi_on || mi_now >= mi_len) return 0;
    static s32 accl[2048], accr[2048];
    if (n > 2048) n = 2048;
    int done = 0;
    while (done < n) {
        /* evenements a l'heure */
        while (mi_pos < mi_nev && mi_ev[mi_pos].t <= mi_now + done) mi_event(&mi_ev[mi_pos++]);
        u32 next = mi_pos < mi_nev ? mi_ev[mi_pos].t : 0xFFFFFFFFu;
        int len = n - done;
        if (next != 0xFFFFFFFFu && next > mi_now + done && next - (mi_now + done) < (u32)len) len = (int)(next - (mi_now + done));
        if (len <= 0) len = 1;
        memset(accl + done, 0, len * 4); memset(accr + done, 0, len * 4);
        for (int vi = 0; vi < MI_VOICES; vi++) {
            mi_voice_t *v = &mi_v[vi];
            if (!v->on) continue;
            const mi_chan_t *c = &mi_ch[v->ch];
            /* gain : vitesse^2 x volume x expression ; panoramique */
            s32 g = (s32)v->vel * v->vel / 127;          /* 0..127 */
            g = g * c->vol / 127 * c->expr / 127;         /* 0..127 */
            s32 gl = g * (127 - (c->pan > 64 ? (c->pan - 64) * 2 : 0)) / 127, gr = g * (127 - (c->pan < 64 ? (64 - c->pan) * 2 : 0)) / 127;
            for (int i = 0; i < len; i++) {
                /* enveloppe */
                if (v->stage == 0) { v->env += v->att; if (v->env >= (1 << 24)) { v->env = 1 << 24; v->stage = 1; } }
                else if (v->stage == 1) { v->env -= v->dec; if (v->env <= v->sus) { v->env = v->sus; v->stage = 2; } }
                else if (v->stage == 3) { v->env -= v->relr; }
                if (v->env <= 0) { v->on = 0; break; }
                s32 sm;
                if (v->drum) {
                    u32 bit = ((v->lfsr >> 0) ^ (v->lfsr >> 2) ^ (v->lfsr >> 3) ^ (v->lfsr >> 5)) & 1;
                    v->lfsr = (v->lfsr >> 1) | (bit << 15);
                    s32 noise = (s32)(v->lfsr & 0xFFFF) - 32768;
                    v->phase += v->inc;
                    s32 tone = mi_tbl[MW_SINE][(v->phase >> 16) & (MI_TBL - 1)];
                    switch (v->dkind) {
                    case 1: sm = tone; if ((s32)v->inc > v->tone_inc_end) v->inc -= (v->inc - v->tone_inc_end) / 600 + 1; break;   /* grosse caisse : sinus qui descend */
                    case 2: v->lp += (noise - v->lp) >> 1; sm = (v->lp * 3 + tone) / 4; break;                                   /* caisse claire */
                    case 3: case 4: v->lp += (noise - v->lp) >> 1; sm = (noise - v->lp) * 3 / 4; break;                         /* charleston : aigu */
                    case 5: v->lp += (noise - v->lp) >> 2; sm = (noise - v->lp) / 2; break;                                     /* cymbales */
                    case 6: sm = tone; if ((s32)v->inc > v->tone_inc_end * 2 / 3) v->inc -= 1 + v->inc / 30000; break;          /* toms */
                    default: v->lp += (noise - v->lp) >> 2; sm = v->lp / 2; break;
                    }
                } else {
                    v->phase += v->inc;
                    u32 ph = v->phase;
                    s32 a = mi_tbl[v->wave][(ph >> 16) & (MI_TBL - 1)], b = mi_tbl[v->wave][((ph >> 16) + 1) & (MI_TBL - 1)];
                    sm = a + (((b - a) * (s32)((ph >> 8) & 0xFF)) >> 8);
                }
                s32 e = v->env >> 9;                       /* 0..32768 */
                s32 x = (sm * (e >> 2)) >> 13;              /* (environ +-32767 x env) */
                accl[done + i] += x * gl >> 7;
                accr[done + i] += x * gr >> 7;
            }
            v->age += len;
        }
        done += len;
    }
    for (int i = 0; i < n; i++) {
        /* (plusieurs voix : on baisse un peu et on adoucit les crêtes) */
        s32 l = accl[i] / 3, r = accr[i] / 3;
        if (l > 24000) l = 24000 + (l - 24000) / 4; else if (l < -24000) l = -24000 + (l + 24000) / 4;
        if (r > 24000) r = 24000 + (r - 24000) / 4; else if (r < -24000) r = -24000 + (r + 24000) / 4;
        L[i] = (s16)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        R[i] = (s16)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
    mi_now += n;
    return n;
}
/* aller a sec : les evenements jusque la sont rejoues sans son (instruments, volumes) */
static void midi_seek(int sec) {
    u32 t = (u32)sec * MI_RATE;
    mi_reset();
    while (mi_pos < mi_nev && mi_ev[mi_pos].t <= t) {
        int ty = mi_ev[mi_pos].st & 0xF0;
        if (ty == 0xB0 || ty == 0xC0 || ty == 0xE0) mi_event(&mi_ev[mi_pos]);
        mi_pos++;
    }
    for (int i = 0; i < MI_VOICES; i++) mi_v[i].on = 0;
    mi_now = t;
}
static int midi_open(const char *path, char *title, int tcap, const char **err) {
    midi_close();
    mi_tables();
    long n = 0;
    unsigned char *d = read_file(path, 4 * 1024 * 1024, &n);
    if (!d) { *err = "Fichier illisible"; return 0; }
    int ok = mi_parse(d, (u32)n);
    free(d);
    if (!ok) { midi_close(); *err = "Fichier MIDI invalide"; return 0; }
    mi_reset();
    mi_on = 1;
    if (title) snprintf(title, tcap, "%s", mi_title);
    return 1;
}
static int midi_len_ms(void) { return (int)((u64)mi_len * 1000 / MI_RATE); }

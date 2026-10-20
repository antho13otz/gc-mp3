// GC-MP3 : fichiers MP4 / M4A (son AAC), inclus dans main.c.
// Le "moov" (table des echantillons) est lu en memoire ; chaque echantillon
// est une trame AAC brute, decodee par Helix (AAC-LC et HE-AAC).
//  - carte SD : lecture par blocs, saut, duree, etiquettes, pochette integree ;
//  - en direct (podcasts, liens web) : le flux HTTP est parcouru boite par boite ;
//    si le moov est a la fin du fichier, on le recupere avec une requete "Range"
//    puis on revient au debut du son. Les sauts (L / R, reprise) utilisent aussi
//    "Range" : le moov reste en memoire pour l'adresse en cours.

#define MP4_MOOV_MAX  (6 * 1024 * 1024)

static u32 be16(const u8 *p) { return (u32)p[0] << 8 | p[1]; }
static u64 be64(const u8 *p) { return (u64)be32(p) << 32 | be32(p + 4); }

typedef struct {
    u8 *moov;                    /* copie du contenu de la boite moov (alloue) */
    u32 moov_len;
    const u8 *stsz, *stco, *stsc, *stts;
    u32 nsamples, stsz_const, stco_n, stsc_n, stts_n;
    int co64;
    u32 timescale;
    u64 duration;                /* en unites de timescale */
    int ch, rate_core, sbr;
    u32 max_size;                /* plus gros echantillon */
    u64 total_bytes;
    tags_t tg;
    const u8 *covr; u32 covr_len;
    const char *err;
} mp4_t;

typedef struct { u32 s, chunk, left, stsc_i; u32 off; } mp4it_t;

/* enfant 'type' dans [p, p+len[ : contenu et longueur, NULL s'il n'y est pas */
static const u8 *mp4_child(const u8 *p, u32 len, const char *type, u32 *clen) {
    u32 i = 0;
    while (i + 8 <= len) {
        u32 sz = be32(p + i), hl = 8;
        if (sz == 1) {
            if (i + 16 > len) return NULL;
            u64 s64 = be64(p + i + 8);
            if (s64 > len - i) return NULL;
            sz = (u32)s64;
            hl = 16;
        } else if (sz == 0) sz = len - i;
        if (sz < hl || sz > len - i) return NULL;
        if (!memcmp(p + i + 4, type, 4)) { *clen = sz - hl; return p + i + hl; }
        i += sz;
    }
    return NULL;
}

/* chemin "a/b/c" depuis [p, p+len[ */
static const u8 *mp4_path(const u8 *p, u32 len, const char *path, u32 *clen) {
    while (p && *path) {
        p = mp4_child(p, len, path, &len);
        path += 4;
        if (*path == '/') path++;
    }
    if (p) *clen = len;
    return p;
}

/* longueur d'un descripteur MPEG-4 (1 a 4 octets) */
static int mp4_desc(const u8 **pp, const u8 *end, int *tag, u32 *len) {
    const u8 *p = *pp;
    if (p >= end) return 0;
    *tag = *p++;
    u32 l = 0;
    for (int k = 0; k < 4; k++) {
        if (p >= end) return 0;
        u8 b = *p++;
        l = (l << 7) | (b & 0x7f);
        if (!(b & 0x80)) break;
    }
    if (l > (u32)(end - p)) return 0;
    *len = l;
    *pp = p;
    return 1;
}

static const int mp4_rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };

typedef struct { const u8 *p; u32 n, bit; } mp4bits_t;
static u32 mp4_getbits(mp4bits_t *b, int k) {
    u32 v = 0;
    while (k-- > 0) {
        u32 byte = b->bit >> 3;
        int bitv = byte < b->n ? (b->p[byte] >> (7 - (b->bit & 7))) & 1 : 0;
        v = (v << 1) | (u32)bitv;
        b->bit++;
    }
    return v;
}
static int mp4_rate(mp4bits_t *b) {
    u32 i = mp4_getbits(b, 4);
    if (i == 15) return (int)mp4_getbits(b, 24);
    return i < 13 ? mp4_rates[i] : 0;
}

/* esds -> AudioSpecificConfig */
static void mp4_esds(mp4_t *m, const u8 *e, u32 el) {
    if (el < 4) return;
    const u8 *p = e + 4, *end = e + el;
    int tag; u32 len;
    if (!mp4_desc(&p, end, &tag, &len) || tag != 3) return;
    end = p + len;
    if (end - p < 3) return;
    u8 fl = p[2];
    p += 3;
    if (fl & 0x80) p += 2;
    if (fl & 0x40) { if (p >= end) return; p += 1 + *p; }
    if (fl & 0x20) p += 2;
    if (p >= end || !mp4_desc(&p, end, &tag, &len) || tag != 4 || len < 13) return;
    u8 oti = p[0];
    const u8 *dend = p + len;
    if (oti != 0x40 && oti != 0x66 && oti != 0x67 && oti != 0x68) { m->err = "Codec du M4A non pris en charge (AAC seulement)"; return; }
    p += 13;
    if (!mp4_desc(&p, dend, &tag, &len) || tag != 5 || len < 2) {
        if (oti != 0x40) { m->rate_core = m->rate_core ? m->rate_core : 44100; }   /* AAC MPEG-2 : valeurs du stsd */
        return;
    }
    mp4bits_t b = { p, len, 0 };
    int aot = (int)mp4_getbits(&b, 5);
    if (aot == 31) aot = 32 + (int)mp4_getbits(&b, 6);
    int rate = mp4_rate(&b);
    int ch = (int)mp4_getbits(&b, 4);
    if (aot == 5 || aot == 29) {                       /* HE-AAC signale : le coeur est en AAC-LC */
        mp4_rate(&b);
        m->sbr = 1;
        aot = (int)mp4_getbits(&b, 5);
    }
    if (aot != 2 && aot != 1) { m->err = "Profil AAC non pris en charge (AAC-LC et HE-AAC seulement)"; return; }
    if (rate > 0) m->rate_core = rate;
    if (ch > 0) m->ch = ch;
}

static void mp4_text(const u8 *ilst, u32 il, const char *type, char *out, int cap) {
    u32 l;
    const u8 *it = mp4_child(ilst, il, type, &l);
    if (!it) return;
    const u8 *d = mp4_child(it, l, "data", &l);
    if (!d || l < 8) return;
    int n = (int)l - 8;
    if (n > cap - 1) n = cap - 1;
    memcpy(out, d + 8, n);
    out[n] = 0;
    for (int i = 0; i < n; i++) if ((u8)out[i] < 0x20) out[i] = ' ';
}

static void mp4_tags(mp4_t *m, const u8 *mv, u32 ml) {
    u32 ul, ml2, il;
    const u8 *u = mp4_child(mv, ml, "udta", &ul);
    if (!u) return;
    const u8 *meta = mp4_child(u, ul, "meta", &ml2);
    if (!meta) return;
    if (ml2 >= 4 && be32(meta) == 0) { meta += 4; ml2 -= 4; }          /* boite "pleine" (version + options) */
    const u8 *ilst = mp4_child(meta, ml2, "ilst", &il);
    if (!ilst) return;
    mp4_text(ilst, il, "\xA9nam", m->tg.title, sizeof(m->tg.title));
    mp4_text(ilst, il, "\xA9" "ART", m->tg.artist, sizeof(m->tg.artist));
    if (!m->tg.artist[0]) mp4_text(ilst, il, "aART", m->tg.artist, sizeof(m->tg.artist));
    mp4_text(ilst, il, "\xA9" "alb", m->tg.album, sizeof(m->tg.album));
    char y[16] = "";
    mp4_text(ilst, il, "\xA9" "day", y, sizeof(y));
    snprintf(m->tg.year, sizeof(m->tg.year), "%.4s", y);
    u32 cl;
    const u8 *c = mp4_child(ilst, il, "covr", &cl);
    if (c && (c = mp4_child(c, cl, "data", &cl)) != NULL && cl > 8) { m->covr = c + 8; m->covr_len = cl - 8; }
}

static void mp4_free(mp4_t *m) {
    free(m->moov);
    memset(m, 0, sizeof(*m));
}

/* analyse le contenu d'une boite moov (pris en charge par m, libere par mp4_free) */
static int mp4_parse(mp4_t *m, u8 *mv, u32 ml) {
    memset(m, 0, sizeof(*m));
    tags_clear(&m->tg);
    m->moov = mv;
    m->moov_len = ml;
    const char *err = NULL;
    for (u32 i = 0; i + 8 <= ml; ) {                       /* chaque piste : la premiere piste son */
        u32 sz = be32(mv + i);
        if (sz < 8 || sz > ml - i) break;
        if (memcmp(mv + i + 4, "trak", 4) != 0) { i += sz; continue; }
        const u8 *tk = mv + i + 8;
        u32 tl = sz - 8, l;
        i += sz;
        const u8 *mdia = mp4_child(tk, tl, "mdia", &l);
        if (!mdia) continue;
        u32 mdl = l;
        const u8 *h = mp4_child(mdia, mdl, "hdlr", &l);
        if (!h || l < 12 || memcmp(h + 8, "soun", 4) != 0) continue;
        const u8 *md = mp4_child(mdia, mdl, "mdhd", &l);
        if (!md || l < 24) continue;
        if (md[0] == 1) { if (l < 32) continue; m->timescale = be32(md + 20); m->duration = be64(md + 24); }
        else { m->timescale = be32(md + 12); m->duration = be32(md + 16); }
        u32 sl;
        const u8 *stbl = mp4_path(mdia, mdl, "minf/stbl", &sl);
        if (!stbl) continue;
        const u8 *sd = mp4_child(stbl, sl, "stsd", &l);
        if (!sd || l < 16) continue;
        u32 el = be32(sd + 8);
        if (el < 8 || el > l - 8) continue;
        const u8 *ent = sd + 16;                            /* contenu de la premiere description */
        el -= 8;
        if (!memcmp(sd + 12, "alac", 4)) { err = "ALAC (Apple Lossless) non pris en charge"; continue; }
        if (memcmp(sd + 12, "mp4a", 4) != 0) { err = "Codec du M4A non pris en charge (AAC seulement)"; continue; }
        if (el < 28) continue;
        int ver = (int)be16(ent + 8);
        m->ch = (int)be16(ent + 16);
        m->rate_core = (int)(be32(ent + 24) >> 16);
        u32 skip = 28 + (ver == 1 ? 16 : ver == 2 ? 36 : 0);
        m->err = NULL;
        if (skip < el) {
            const u8 *es = mp4_child(ent + skip, el - skip, "esds", &l);
            if (!es) { u32 wl; const u8 *w = mp4_child(ent + skip, el - skip, "wave", &wl); if (w) es = mp4_child(w, wl, "esds", &l); }
            if (es) mp4_esds(m, es, l);
        }
        if (m->err) { err = m->err; m->err = NULL; continue; }
        const u8 *t;
        if ((t = mp4_child(stbl, sl, "stsz", &l)) == NULL || l < 12) { err = "Table du M4A illisible"; continue; }
        m->stsz_const = be32(t + 4);
        m->nsamples = be32(t + 8);
        m->stsz = t + 12;
        if (!m->stsz_const && m->nsamples > (l - 12) / 4) { err = "Table du M4A illisible"; continue; }
        m->co64 = 0;
        if ((t = mp4_child(stbl, sl, "stco", &l)) == NULL) { t = mp4_child(stbl, sl, "co64", &l); m->co64 = 1; }
        if (!t || l < 8) { err = "Table du M4A illisible"; continue; }
        m->stco_n = be32(t + 4);
        m->stco = t + 8;
        if (m->stco_n > (l - 8) / (m->co64 ? 8 : 4)) { err = "Table du M4A illisible"; continue; }
        if ((t = mp4_child(stbl, sl, "stsc", &l)) == NULL || l < 8) { err = "Table du M4A illisible"; continue; }
        m->stsc_n = be32(t + 4);
        m->stsc = t + 8;
        if (m->stsc_n > (l - 8) / 12 || m->stsc_n == 0) { err = "Table du M4A illisible"; continue; }
        if ((t = mp4_child(stbl, sl, "stts", &l)) != NULL && l >= 8) {
            m->stts_n = be32(t + 4);
            m->stts = t + 8;
            if (m->stts_n > (l - 8) / 8) m->stts_n = (l - 8) / 8;
        }
        if (m->nsamples == 0 || m->stco_n == 0) { err = "M4A vide"; continue; }
        if (m->ch < 1 || m->ch > 2) { err = "M4A en plus de 2 canaux non pris en charge"; continue; }
        if (m->rate_core < 8000 || m->rate_core > 96000) { err = "Fr\xC3\xA9quence du M4A non prise en charge"; continue; }
        if (!m->timescale) m->timescale = (u32)m->rate_core;
        if (!m->duration && m->stts) for (u32 k = 0; k < m->stts_n; k++) m->duration += (u64)be32(m->stts + k * 8) * be32(m->stts + k * 8 + 4);
        m->max_size = m->stsz_const;
        m->total_bytes = (u64)m->stsz_const * m->nsamples;
        if (!m->stsz_const)
            for (u32 k = 0; k < m->nsamples; k++) { u32 s = be32(m->stsz + k * 4); if (s > m->max_size) m->max_size = s; m->total_bytes += s; }
        if (m->max_size == 0 || m->max_size > 65536) { err = "Trame du M4A trop grande"; continue; }
        mp4_tags(m, mv, ml);
        return 1;
    }
    m->err = err ? err : "Pas de son AAC dans ce fichier";
    return 0;
}

static u32 mp4_size(const mp4_t *m, u32 s) { return m->stsz_const ? m->stsz_const : be32(m->stsz + s * 4); }
static u32 mp4_chunk(const mp4_t *m, u32 c) { return m->co64 ? (u32)be64(m->stco + c * 8) : be32(m->stco + c * 4); }

/* echantillons par morceau ("chunk") c, en avancant dans la table stsc */
static u32 mp4_spc(const mp4_t *m, u32 c, u32 *si) {
    while (*si + 1 < m->stsc_n && be32(m->stsc + (*si + 1) * 12) - 1 <= c) (*si)++;
    return be32(m->stsc + *si * 12 + 4);
}

static void mp4_it_init(const mp4_t *m, mp4it_t *it) {
    memset(it, 0, sizeof(*it));
    it->left = mp4_spc(m, 0, &it->stsc_i);
    it->off = mp4_chunk(m, 0);
}

/* echantillon suivant : position dans le fichier et taille ; 0 a la fin */
static int mp4_it_peek(const mp4_t *m, mp4it_t *it, u32 *off, u32 *size) {
    if (it->s >= m->nsamples) return 0;
    while (it->left == 0) {
        if (++it->chunk >= m->stco_n) { it->s = m->nsamples; return 0; }
        it->left = mp4_spc(m, it->chunk, &it->stsc_i);
        it->off = mp4_chunk(m, it->chunk);
    }
    *off = it->off;
    *size = mp4_size(m, it->s);
    return 1;
}
static void mp4_it_step(const mp4_t *m, mp4it_t *it) {
    if (it->s >= m->nsamples || !it->left) return;
    it->off += mp4_size(m, it->s);
    it->s++;
    it->left--;
}

/* premier echantillon a 'sec' secondes */
static u32 mp4_sample_at(const mp4_t *m, double sec) {
    if (sec <= 0 || !m->stts) return 0;
    u64 t = (u64)(sec * m->timescale), acc = 0;
    u32 s = 0;
    for (u32 k = 0; k < m->stts_n; k++) {
        u32 cnt = be32(m->stts + k * 8), d = be32(m->stts + k * 8 + 4);
        if (d && acc + (u64)cnt * d > t) { s += (u32)((t - acc) / d); return s < m->nsamples ? s : m->nsamples; }
        acc += (u64)cnt * d;
        s += cnt;
    }
    return m->nsamples;
}
static double mp4_time_of(const mp4_t *m, u32 s) {
    if (!m->stts || !m->timescale) return 0;
    u64 acc = 0;
    for (u32 k = 0; k < m->stts_n && s > 0; k++) {
        u32 cnt = be32(m->stts + k * 8), d = be32(m->stts + k * 8 + 4);
        u32 take = cnt < s ? cnt : s;
        acc += (u64)take * d;
        s -= take;
    }
    return (double)acc / m->timescale;
}
static int mp4_seconds(const mp4_t *m) { return m->timescale ? (int)(m->duration / m->timescale) : 0; }

static void mp4_it_seek(const mp4_t *m, mp4it_t *it, u32 s) {
    mp4_it_init(m, it);
    u32 off, size;
    while (it->s < s && mp4_it_peek(m, it, &off, &size)) mp4_it_step(m, it);
}

/* configure un decodeur Helix pour les trames brutes de ce fichier */
static int mp4_aac_setup(HAACDecoder *d, const mp4_t *m) {
    if (!*d) *d = AACInitDecoder();
    if (!*d) return 0;
    AACFlushCodec(*d);
    AACFrameInfo fi;
    memset(&fi, 0, sizeof(fi));
    fi.nChans = m->ch;
    fi.sampRateCore = m->rate_core;
    fi.profile = AAC_PROFILE_LC;
    return AACSetRawBlockParams(*d, 0, &fi) == 0;
}

/* decode une trame brute et l'envoie vers la sortie ; 0 si illisible */
static int mp4_decode_frame(HAACDecoder d, const u8 *data, int n, int *hz, int *chs) {
    static short out[AAC_MAX_NCHANS * AAC_MAX_NSAMPS * 2];
    static s16 L[2048], R[2048];
    unsigned char *ptr = (unsigned char *)data;
    int left = n;
    if (AACDecode(d, &ptr, &left, out) != 0) return 0;
    AACFrameInfo fi;
    AACGetLastFrameInfo(d, &fi);
    int ch = fi.nChans > 0 ? fi.nChans : 1;
    int k = fi.outputSamps / ch;
    if (k <= 0 || fi.sampRateOut <= 0) return 1;
    if (k > 2048) k = 2048;
    for (int i = 0; i < k; i++) {
        L[i] = out[i * ch];
        R[i] = ch > 1 ? out[i * ch + 1] : L[i];
    }
    *hz = fi.sampRateOut;
    *chs = ch;
    push_frame(L, R, k, (unsigned)fi.sampRateOut);
    return 1;
}

/* ================= fichiers de la carte SD ================= */

/* boite de premier niveau 'type' : debut du contenu et longueur */
static int mp4_file_find(FILE *fp, long fsz, const char *type, long *cstart, long *clen) {
    long pos = 0;
    u8 h[16];
    while (pos + 8 <= fsz) {
        if (fseek(fp, pos, SEEK_SET) != 0 || fread(h, 1, 16, fp) < 8) return 0;
        long sz = (long)be32(h), hl = 8;
        if (sz == 1) { u64 s = be64(h + 8); if (s > (u64)(fsz - pos)) return 0; sz = (long)s; hl = 16; }
        else if (sz == 0) sz = fsz - pos;
        if (sz < hl || sz > fsz - pos) return 0;
        if (!memcmp(h + 4, type, 4)) { *cstart = pos + hl; *clen = sz - hl; return 1; }
        pos += sz;
    }
    return 0;
}

/* lit et analyse le moov d'un fichier */
static int mp4_file_open(FILE *fp, long fsz, mp4_t *m) {
    long ms, ml;
    memset(m, 0, sizeof(*m));
    if (!mp4_file_find(fp, fsz, "moov", &ms, &ml)) { m->err = "M4A illisible (pas de table moov)"; return 0; }
    if (ml <= 0 || ml > MP4_MOOV_MAX) { m->err = "M4A trop gros"; return 0; }
    u8 *b = (u8 *)malloc(ml);
    if (!b) { m->err = "M\xC3\xA9moire insuffisante"; return 0; }
    if (fseek(fp, ms, SEEK_SET) != 0 || fread(b, 1, ml, fp) != (size_t)ml) { free(b); m->err = "Lecture impossible"; return 0; }
    if (!mp4_parse(m, b, (u32)ml)) { const char *e = m->err; mp4_free(m); m->err = e; return 0; }
    return 1;
}

/* duree (s) sans lire toute la table : mvhd */
static int m4a_duration(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    long fsz = -1, ms, ml;
    if (fseek(fp, 0, SEEK_END) == 0) fsz = ftell(fp);
    int d = -1;
    if (fsz > 0 && mp4_file_find(fp, fsz, "moov", &ms, &ml)) {
        long pos = ms;
        u8 h[32];
        while (pos + 8 <= ms + ml) {
            if (fseek(fp, pos, SEEK_SET) != 0 || fread(h, 1, 32, fp) < 8) break;
            long sz = (long)be32(h);
            if (sz < 8 || sz > ms + ml - pos) break;
            if (!memcmp(h + 4, "mvhd", 4)) {
                u32 ts, du;
                if (h[8] == 1) { u8 g[36]; if (fseek(fp, pos + 8, SEEK_SET) != 0 || fread(g, 1, 36, fp) != 36) break; ts = be32(g + 20); du = (u32)(be64(g + 24) / (ts ? ts : 1)); d = (int)du; }
                else { ts = be32(h + 20); du = be32(h + 24); d = ts ? (int)(du / ts) : -1; }
                break;
            }
            pos += sz;
        }
    }
    fclose(fp);
    return d;
}

/* etiquettes (et pochette si want_cover) d'un fichier, sans garder la table */
static int m4a_meta(const char *path, tags_t *tg, int want_cover) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    long fsz = -1, ms, ml;
    if (fseek(fp, 0, SEEK_END) == 0) fsz = ftell(fp);
    int ok = 0;
    if (fsz > 0 && mp4_file_find(fp, fsz, "moov", &ms, &ml)) {
        /* seulement udta (les pistes, souvent grosses, sont sautees) */
        long pos = ms;
        u8 h[8];
        while (pos + 8 <= ms + ml) {
            if (fseek(fp, pos, SEEK_SET) != 0 || fread(h, 1, 8, fp) != 8) break;
            long sz = (long)be32(h);
            if (sz < 8 || sz > ms + ml - pos) break;
            if (!memcmp(h + 4, "udta", 4) && sz <= MP4_MOOV_MAX) {
                u8 *b = (u8 *)malloc(sz);
                if (b && fseek(fp, pos, SEEK_SET) == 0 && fread(b, 1, sz, fp) == (size_t)sz) {
                    mp4_t m;
                    memset(&m, 0, sizeof(m));
                    tags_clear(&m.tg);
                    mp4_tags(&m, b, (u32)sz);
                    if (tg) *tg = m.tg;
                    if (want_cover && m.covr) ok = cover_from_memory(m.covr, m.covr_len);
                    else ok = !want_cover;
                }
                free(b);
                break;
            }
            pos += sz;
        }
    }
    fclose(fp);
    return ok;
}

static mp4_t fm4;                       /* fichier M4A en cours */
static mp4it_t fm4_it;
static HAACDecoder fm4_dec = NULL;
static long fm4_base = 0;               /* position dans le fichier de fbuf[0] */

static void m4a_close(void) { mp4_free(&fm4); }

static int m4a_open(FILE *fp, long fsz, tags_t *tg, const char **err) {
    m4a_close();
    if (!mp4_file_open(fp, fsz, &fm4)) { *err = fm4.err; return 0; }
    if (!mp4_aac_setup(&fm4_dec, &fm4)) { m4a_close(); *err = "D\xC3\xA9" "codeur AAC indisponible"; return 0; }
    *tg = fm4.tg;
    mp4_it_init(&fm4, &fm4_it);
    fm4_base = 0;
    fb_start = fb_end = 0;
    return 1;
}

/* une trame du fichier M4A */
static void m4a_decode_one(void) {
    u32 off, size;
    if (!fm4.moov || !mp4_it_peek(&fm4, &fm4_it, &off, &size)) { eof = 1; return; }
    if (size == 0) { mp4_it_step(&fm4, &fm4_it); return; }
    long rel = (long)off - fm4_base;
    if (rel < 0 || rel + (long)size > fb_end) {             /* hors du bloc en memoire : on relit */
        if (fseek(ffp, off, SEEK_SET) != 0) { eof = 1; return; }
        size_t n = fread(fbuf, 1, FBUF_SIZE, ffp);
        fm4_base = (long)off;
        fb_start = 0;
        fb_end = (int)n;
        rel = 0;
        if ((long)size > fb_end) { eof = 1; return; }       /* fichier coupe */
    }
    mp4_it_step(&fm4, &fm4_it);
    int hz = 0, ch = 0;
    if (!mp4_decode_frame(fm4_dec, fbuf + rel, (int)size, &hz, &ch)) AACFlushCodec(fm4_dec);
}

static int m4a_seek(int sec) {
    if (!fm4.moov) return 0;
    u32 s = mp4_sample_at(&fm4, sec);
    if (s >= fm4.nsamples) return 0;
    mp4_it_seek(&fm4, &fm4_it, s);
    AACFlushCodec(fm4_dec);
    fb_start = fb_end = 0;
    fm4_base = 0;
    return 1;
}

/* ================= en direct (HTTP) ================= */

enum { MS_BOX, MS_MOOV, MS_SKIP, MS_PLAY, MS_END };
static struct {
    int phase;
    long pos;                    /* position dans le fichier du premier octet de cbuf */
    long box_end;                /* fin de la boite (moov en cours, saut) */
    u8 *mv; u32 mv_len, mv_got;
    mp4_t m;                     /* table (gardee pour la meme adresse) */
    char key[URL_MAX];           /* adresse a laquelle correspond la table */
    mp4it_t it;
    int want;                    /* echantillon voulu (-1 = aucun) */
    double want_sec;             /* position demandee avant d'avoir la table (-1 = aucune) */
    long reopen_at;              /* >= 0 : rouvrir la connexion a cette position */
    int reopens, norange;        /* norange : le serveur ignore "Range" */
} ms = { .want = -1, .want_sec = -1, .reopen_at = -1 };
static int ms_reopening = 0;

static int mp4_url_looks(const char *path, const char *ctype) {
    if (strstr(ctype, "mp4") || strstr(ctype, "m4a") || strstr(ctype, "m4b")) return 1;
    const char *q = path + strcspn(path, "?#");
    const char *d = q;
    while (d > path && d[-1] != '.' && d[-1] != '/') d--;
    if (d > path && d[-1] == '.' && q - d == 3 && (!strncasecmp(d, "m4a", 3) || !strncasecmp(d, "m4b", 3) || !strncasecmp(d, "mp4", 3))) return 1;
    return 0;
}

/* table connue pour l'adresse de la station en cours ? */
static int ms_have_table(const char *key) { return ms.m.moov && !strcmp(ms.key, key); }

/* appele par http_open quand le flux est un MP4 ; pos = position du premier octet recu */
static void mp4s_begin(long pos, const char *key) {
    if (!ms_reopening) {
        if (!ms_have_table(key)) { mp4_free(&ms.m); snprintf(ms.key, sizeof(ms.key), "%s", key); ms.norange = 0; }
        ms.reopens = 0;
    } else if (http_range > 0 && pos == 0) ms.norange = 1;
    free(ms.mv);
    ms.mv = NULL;
    ms.mv_len = ms.mv_got = 0;
    ms.pos = pos;
    ms.reopen_at = -1;
    ms.phase = MS_BOX;
    if (ms.m.moov) {
        int sec = mp4_seconds(&ms.m);
        if (sec > 0) stream_kbps = (int)(ms.m.total_bytes * 8 / sec / 1000);
        if (ms.want_sec >= 0) { ms.want = (int)mp4_sample_at(&ms.m, ms.want_sec); ms.want_sec = -1; }
        u32 off = 0, size;
        mp4_it_seek(&ms.m, &ms.it, ms.want > 0 ? (u32)ms.want : 0);
        ms.want = -1;
        if (pos > 0) {                                      /* reprise au milieu du son */
            if (mp4_it_peek(&ms.m, &ms.it, &off, &size) && (long)off >= pos) ms.phase = MS_PLAY;
            else ms.reopen_at = 0;
        }
    } else if (pos != 0 && !ms_reopening) ms.reopen_at = 0;   /* il faut d'abord la table */
}

static void ms_want_clear(void) { ms.want = -1; ms.want_sec = -1; }

/* la table vient d'etre lue : configure le decodeur et la position */
static void mp4s_table_ready(void) {
    if (ms.want_sec >= 0) { ms.want = (int)mp4_sample_at(&ms.m, ms.want_sec); ms.want_sec = -1; }
    mp4_it_seek(&ms.m, &ms.it, ms.want > 0 ? (u32)ms.want : 0);
    ms.want = -1;
    int sec = mp4_seconds(&ms.m);
    if (sec > 0) stream_kbps = (int)(ms.m.total_bytes * 8 / sec / 1000);
    if (pod_on && pod_dur <= 0) pod_dur = sec;
}

static int mp4s_reopen(long off) {
    ms.reopens++;
    stream_close();
    cb_start = cb_end = 0;
    icy_metaint = 0;
    stream_closed = 0;
    http_range = off;
    ms_reopening = 1;
    int was = state;
    state = ST_CONNECT;                      /* (pendant l'attente : la boucle ne lit pas ce flux) */
    static char u[URL_MAX];
    snprintf(u, sizeof(u), "%s", http_final_url[0] ? http_final_url : stations[cur_station].url);
    int ok = http_open(u, 0);
    ms_reopening = 0;
    if (ok) state = was == ST_PLAY && f_count > 0 ? ST_PLAY : ST_BUFFER;
    http_range = 0;
    last_data_ms = now_ms();
    if (!ok) return 0;
    if (stream_codec != 3) { set_error("Le serveur a chang\xC3\xA9 de format"); return 0; }
    return 1;
}

static int decode_mp4_stream_one(int closed) {
    if (state == ST_ERROR || ms_reopening) return 0;
    if (ms.reopen_at >= 0) {
        long off = ms.reopen_at;
        ms.reopen_at = -1;
        if (ms.reopens > 6) { set_error("M4A : trop de reprises de connexion"); return 0; }
        return mp4s_reopen(off);
    }
    int avail = cbuf_avail();
    u8 *p = cbuf + cb_start;
    switch (ms.phase) {
    case MS_BOX: {
        if (avail < 16 && !closed) return 0;
        if (avail < 8) { if (closed) cb_start += avail; return 0; }
        long sz = (long)be32(p), hl = 8;
        if (sz == 1) { u64 s = be64(p + 8); sz = s > 0x7fffffffULL ? 0x7fffffffL : (long)s; hl = 16; }
        else if (sz == 0) sz = 0x7fffffffL - ms.pos;        /* jusqu'a la fin */
        if (sz < hl) { set_error("M4A illisible"); return 0; }
        if (!memcmp(p + 4, "moov", 4)) {
            if (ms.m.moov) { ms.phase = MS_SKIP; ms.box_end = ms.pos + sz; return 1; }   /* deja lue */
            if (sz - hl > MP4_MOOV_MAX) { set_error("M4A trop gros"); return 0; }
            ms.mv_len = (u32)(sz - hl);
            ms.mv_got = 0;
            ms.mv = (u8 *)malloc(ms.mv_len ? ms.mv_len : 1);
            if (!ms.mv) { set_error("M\xC3\xA9moire insuffisante"); return 0; }
            cb_start += hl; ms.pos += hl;
            ms.phase = MS_MOOV;
            return 1;
        }
        if (!memcmp(p + 4, "moof", 4)) { set_error("M4A fragment\xC3\xA9 non pris en charge"); return 0; }
        if (!memcmp(p + 4, "mdat", 4)) {
            if (ms.m.moov) {                                /* le son : lecture */
                cb_start += hl; ms.pos += hl;
                ms.phase = MS_PLAY;
                return 1;
            }
            /* table a la fin du fichier : on va la chercher, puis on revient */
            if (!ms.norange && ms.reopens < 3 && sz < 0x7fff0000L) { ms.reopen_at = ms.pos + sz; return 1; }
            ms.phase = MS_SKIP;
            ms.box_end = ms.pos + sz;
            return 1;
        }
        ms.phase = MS_SKIP;                                 /* ftyp, free, ... */
        ms.box_end = ms.pos + sz;
        return 1;
    }
    case MS_SKIP: {
        long need = ms.box_end - ms.pos;
        int take = need < avail ? (int)need : avail;
        cb_start += take; ms.pos += take;
        if (ms.pos >= ms.box_end) { ms.phase = MS_BOX; return 1; }
        return take > 0;
    }
    case MS_MOOV: {
        u32 need = ms.mv_len - ms.mv_got;
        int take = need < (u32)avail ? (int)need : avail;
        memcpy(ms.mv + ms.mv_got, p, take);
        ms.mv_got += take;
        cb_start += take; ms.pos += take;
        if (ms.mv_got < ms.mv_len) { if (closed && take == 0) set_error("M4A coup\xC3\xA9"); return take > 0; }
        u8 *mv = ms.mv;
        ms.mv = NULL;
        if (!mp4_parse(&ms.m, mv, ms.mv_len)) { const char *e = ms.m.err; mp4_free(&ms.m); set_error(e ? e : "M4A illisible"); return 0; }
        snprintf(ms.key, sizeof(ms.key), "%s", stations[cur_station].url);
        if (!mp4_aac_setup(&aac_dec, &ms.m)) { set_error("D\xC3\xA9" "codeur AAC indisponible"); return 0; }
        mp4s_table_ready();
        u32 off, size;
        if (mp4_it_peek(&ms.m, &ms.it, &off, &size) && (long)off < ms.pos) {
            ms.reopen_at = ms.norange ? 0 : (long)off;      /* table a la fin : retour au son */
            return 1;
        }
        ms.phase = MS_BOX;
        return 1;
    }
    case MS_PLAY: {
        u32 off, size;
        if (!mp4_it_peek(&ms.m, &ms.it, &off, &size)) {     /* fini : le reste (table...) ne sert plus */
            ms.phase = MS_END;
            cb_start = cb_end;
            stream_closed = 1;
            stream_close();
            return 0;
        }
        if ((long)off < ms.pos) { mp4_it_step(&ms.m, &ms.it); return 1; }
        if ((long)off > ms.pos) {                           /* trou (autre piste, saut) */
            long gap = (long)off - ms.pos;
            if (gap > 512 * 1024 && !ms.norange && ms.reopens < 6) { ms.reopen_at = (long)off; return 1; }
            int take = gap < avail ? (int)gap : avail;
            cb_start += take; ms.pos += take;
            return take > 0;
        }
        if (avail < (int)size) {
            if (closed) { cb_start = cb_end; ms.phase = MS_END; }
            return 0;
        }
        if (!stream_kbps && mp4_seconds(&ms.m) > 0) stream_kbps = (int)(ms.m.total_bytes * 8 / mp4_seconds(&ms.m) / 1000);
        if (!aac_dec && !mp4_aac_setup(&aac_dec, &ms.m)) { set_error("D\xC3\xA9" "codeur AAC indisponible"); return 0; }
        int hz = 0, ch = 0;
        if (!mp4_decode_frame(aac_dec, p, (int)size, &hz, &ch)) AACFlushCodec(aac_dec);
        else if (hz) { stream_hz = hz; stream_ch = ch; }
        cb_start += size; ms.pos += size;
        mp4_it_step(&ms.m, &ms.it);
        return 1;
    }
    default:
        cb_start = cb_end;
        return 0;
    }
}

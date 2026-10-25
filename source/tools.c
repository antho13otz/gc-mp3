// GC-MP3 v35 : outils (inclus dans main.c, apres gclib.c, avant indigo_cfg.c)
// Section "Outils" de la page Reglages (START) :
//   - cartes memoire : sauvegardes des slots A / B, copie sur la SD (.gci), restauration,
//     copie vers l'autre carte, suppression
//   - applis : les .dol de la carte SD (racine, apps/), lances directement
//   - derniers jeux lances (GC Station + liste "recent" du lanceur de secours)
//   - infos sur la console
// Chaque outil s'ouvre en fenetre par-dessus l'ecran (tl_screen).

static void indigo_game_open(const char *id, const char *title);
enum { TL_NONE, TL_CARDS, TL_APPS, TL_RECENT, TL_INFO, TL_VCARD, TL_RHIST, TL_NETSRV, TL_SAVES };   /* v141 : historique des sauvegardes */
static void sv_open(void);
static void sv_input(u32 down, u32 nav, int fire);
static void draw_saves(void);   /* v86 : serveur SMB / FTP / FSP */   /* v73 : cartes virtuelles, historique radio */
static void vc_open(void);
static void vc_input(u32 down, u32 nav, int fire);
static void draw_vcard(void);
static void rh_input(u32 down, u32 nav, int fire);
static void draw_rhist(void);
static int bk_save(char *msg, int cap);
static int bk_restore(char *msg, int cap);
static int tl_screen = TL_NONE;
static char tl_msg[200];
static u64 tl_msg_t = 0;
static void tl_note(const char *m) { snprintf(tl_msg, sizeof(tl_msg), "%s", TR(m)); tl_msg_t = now_ms(); }
static void tl_status(const char *m) { gl_status(m); }   /* (ecran "patiente" pendant une operation longue) */

/* =================== cartes memoire =================== */
#define MC_DIR SD_ROOT "/gci"                   /* sauvegardes copiees sur la carte SD */
#define MC_MAX 127
typedef struct {
    card_dir dir;
    u8 de[64];                                  /* entree du repertoire (format .gci, gros-boutiste) */
    char title[64], sub[64];                    /* commentaire de la sauvegarde (2 lignes de 32) */
    int blocks, loaded;
    u16 *icon;                                  /* icone 32x32 RGB5A3 (texture), ou NULL */
    GXTexObj obj;
} mc_ent;
static mc_ent *mc = NULL;
static int mc_n = 0, mc_slot = 0, mc_sel = 0, mc_top = 0, mc_mounted = -1, mc_err = 0, mc_inited = 0;
static u32 mc_sector = 8192;
static u16 mc_free = 0;
static u8 *mc_work = NULL;                      /* zone de travail de la carte (40 Ko, alignee) */
static int mc_pop = 0, mc_psel = 0, mc_pconfirm = 0;
static int mc_gci = 0;                          /* 1 = liste des .gci de la SD (restaurer) */
static char (*mc_gfiles)[96] = NULL;
static int mc_gn = 0, mc_gsel = 0;
static int vc_hub = 0;                          /* v145 : carte virtuelle ouverte depuis l'onglet JEUX (3e choix de la vue Cartes) */
static void vc_open(void);
static const char *vc_load_active(void);
static const char *vc_import(const u8 *g, long len);

static u16 mc_be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 mc_be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void mc_put16(u8 *p, u16 v) { p[0] = v >> 8; p[1] = (u8)v; }
static void mc_put32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (u8)v; }
/* card_direntry <-> 64 octets du .gci (meme disposition, champs en gros-boutiste) */
static void mc_de_to_bytes(const card_direntry *e, u8 *b) {
    memcpy(b, e->gamecode, 4); memcpy(b + 4, e->company, 2); b[6] = e->pad_00; b[7] = e->banner_fmt;
    memcpy(b + 8, e->filename, 32);
    mc_put32(b + 0x28, e->last_modified); mc_put32(b + 0x2C, e->icon_addr);
    mc_put16(b + 0x30, e->icon_fmt); mc_put16(b + 0x32, e->icon_speed);
    b[0x34] = e->permission; b[0x35] = e->copy_times;
    mc_put16(b + 0x36, e->block); mc_put16(b + 0x38, e->length); mc_put16(b + 0x3A, e->pad_01);
    mc_put32(b + 0x3C, e->comment_addr);
}
static void mc_bytes_to_de(const u8 *b, card_direntry *e) {
    memcpy(e->gamecode, b, 4); memcpy(e->company, b + 4, 2); e->pad_00 = b[6]; e->banner_fmt = b[7];
    memcpy(e->filename, b + 8, 32);
    e->last_modified = mc_be32(b + 0x28); e->icon_addr = mc_be32(b + 0x2C);
    e->icon_fmt = mc_be16(b + 0x30); e->icon_speed = mc_be16(b + 0x32);
    e->permission = b[0x34]; e->copy_times = b[0x35];
    e->block = mc_be16(b + 0x36); e->length = mc_be16(b + 0x38); e->pad_01 = mc_be16(b + 0x3A);
    e->comment_addr = mc_be32(b + 0x3C);
}

static const char *mc_errtxt(int r) {
    switch (r) {
    case CARD_ERROR_NOCARD: return "Pas de carte m\xC3\xA9moire dans ce slot";
    case CARD_ERROR_WRONGDEVICE: return "Ce slot contient autre chose qu'une carte m\xC3\xA9moire (adaptateur SD ?)";
    case CARD_ERROR_ENCODING: return "Carte d'une autre r\xC3\xA9gion (japonaise / europ\xC3\xA9" "enne)";
    case CARD_ERROR_BROKEN: return "Carte ab\xC3\xAEm\xC3\xA9" "e ou pas format\xC3\xA9" "e";
    case CARD_ERROR_EXIST: return "Cette sauvegarde existe d\xC3\xA9j\xC3\xA0 sur la carte (supprime-la d'abord)";
    case CARD_ERROR_NOENT: return "La carte a d\xC3\xA9j\xC3\xA0 127 sauvegardes";
    case CARD_ERROR_INSSPACE: return "Pas assez de place sur la carte";
    case CARD_ERROR_NOPERM: return "Sauvegarde prot\xC3\xA9g\xC3\xA9" "e (copie interdite par le jeu)";
    case CARD_ERROR_IOERROR: return "Erreur de lecture / \xC3\xA9" "criture de la carte";
    default: return "Erreur de la carte m\xC3\xA9moire";
    }
}

static void mc_free_list(void) {
    if (!mc) return;
    for (int i = 0; i < mc_n; i++) if (mc[i].icon) { free(mc[i].icon); mc[i].icon = NULL; }
    mc_n = 0;
}
static void mc_unmount(void) {
    mc_free_list();
    if (mc_mounted >= 0) CARD_Unmount(mc_mounted);
    mc_mounted = -1;
}
/* monte la carte du slot et lit la liste des sauvegardes */
static int mc_mount(int slot) {
    mc_unmount();
    if (!mc) mc = (mc_ent *)calloc(MC_MAX, sizeof(mc_ent));
    if (!mc_work) mc_work = (u8 *)memalign(32, CARD_WORKAREA_SIZE);
    if (!mc || !mc_work) { mc_err = CARD_ERROR_FATAL_ERROR; return 0; }
    if (!mc_inited) { CARD_Init("GMP3", "GC"); mc_inited = 1; }
    tl_status(slot ? "Lecture de la carte du slot B..." : "Lecture de la carte du slot A...");
    int r = CARD_ERROR_BUSY;
    for (int t = 0; t < 10 && r == CARD_ERROR_BUSY; t++) { r = CARD_Mount(slot, mc_work, NULL); service_audio(); }
    gl_busy[0] = 0;
    if (r < 0) { mc_err = r; crumb("carte %c : erreur %d", 'A' + slot, r); return 0; }
    mc_mounted = slot; mc_err = 0;
    CARD_GetSectorSize(slot, &mc_sector);
    if (mc_sector < 8192 || mc_sector > 65536) mc_sector = 8192;
    mc_free = 0; CARD_GetFreeBlocks(slot, &mc_free);
    card_dir d;
    memset(&d, 0, sizeof(d));
    int k = CARD_FindFirst(slot, &d, true);
    while (k == CARD_ERROR_READY && mc_n < MC_MAX) {
        mc_ent *m = &mc[mc_n];
        memset(m, 0, sizeof(*m));
        m->dir = d;
        card_direntry e;
        if (CARD_GetStatusEx(slot, d.fileno, &e) == CARD_ERROR_READY) mc_de_to_bytes(&e, m->de);
        m->blocks = mc_be16(m->de + 0x38);
        if (!m->blocks) m->blocks = (int)(d.filelen / mc_sector);
        char nm[33]; memcpy(nm, d.filename, 32); nm[32] = 0;
        snprintf(m->title, sizeof(m->title), "%s", nm);       /* (en attendant le commentaire) */
        mc_n++;
        k = CARD_FindNext(&d);
    }
    if (mc_sel >= mc_n + 1) mc_sel = mc_n;
    crumb("carte %c : %d sauvegardes, %u blocs libres", 'A' + slot, mc_n, mc_free);
    return 1;
}
/* lit tout le contenu d'une sauvegarde (multiple de la taille d'un secteur) */
static u8 *mc_read_all(int i, long *len) {
    mc_ent *m = &mc[i];
    long n = (long)m->blocks * mc_sector;
    if (n <= 0 || n > 8L * 1024 * 1024) return NULL;
    u8 *buf = (u8 *)memalign(32, n);
    if (!buf) return NULL;
    card_file f;
    if (CARD_OpenEntry(mc_mounted, &m->dir, &f) != CARD_ERROR_READY) { free(buf); return NULL; }
    for (long o = 0; o < n; o += mc_sector) {
        if (CARD_Read(&f, buf + o, mc_sector, o) != CARD_ERROR_READY) { CARD_Close(&f); free(buf); return NULL; }
        service_audio();
    }
    CARD_Close(&f);
    *len = n;
    return buf;
}
/* commentaire et icone : lit seulement le debut de la sauvegarde */
static void mc_load_meta(int i) {
    mc_ent *m = &mc[i];
    m->loaded = 1;
    u32 cmt = mc_be32(m->de + 0x3C), iaddr = mc_be32(m->de + 0x2C);
    u16 ifmt = mc_be16(m->de + 0x30), ispd = mc_be16(m->de + 0x32);
    u32 need = 0;
    if (cmt != 0xFFFFFFFFu) need = cmt + 64;
    /* icone 0 : apres la banniere (format des cartes memoire GameCube) */
    int bfmt = m->de[7] & 3;
    u32 bsize = bfmt == 1 ? 96 * 32 + 512 : bfmt == 2 ? 96 * 32 * 2 : 0;
    int f0 = ifmt & 3;
    u32 ioff = 0, itl = 0;
    if (iaddr != 0xFFFFFFFFu && f0) {
        ioff = iaddr + bsize;
        if (f0 == 3) itl = ioff + 1024;
        else if (f0 == 1) {                               /* palette commune, apres toutes les icones */
            u32 o = ioff;
            for (int k = 0; k < 8; k++) {
                int fk = (ifmt >> (2 * k)) & 3, sk = (ispd >> (2 * k)) & 3;
                if (!sk) break;
                o += fk == 2 ? 2048 : fk == 1 ? 1024 : fk == 3 ? 1024 + 512 : 0;
            }
            itl = o;
        }
        u32 e = f0 == 2 ? ioff + 2048 : (itl > ioff + 1024 ? itl : ioff + 1024) + 512;
        if (e > need) need = e;
    }
    if (!need) return;
    u32 nsec = (need + mc_sector - 1) / mc_sector;
    if (nsec > (u32)m->blocks || nsec > 4) return;
    u8 *buf = (u8 *)memalign(32, nsec * mc_sector);
    if (!buf) return;
    card_file f;
    int ok = CARD_OpenEntry(mc_mounted, &m->dir, &f) == CARD_ERROR_READY;
    for (u32 s = 0; ok && s < nsec; s++) ok = CARD_Read(&f, buf + s * mc_sector, mc_sector, s * mc_sector) == CARD_ERROR_READY;
    if (ok || nsec) CARD_Close(&f);
    if (ok) {
        if (cmt != 0xFFFFFFFFu) {
            gl_latin(buf + cmt, 32, m->title, sizeof(m->title));
            gl_latin(buf + cmt + 32, 32, m->sub, sizeof(m->sub));
            if (!m->title[0]) { char nm[33]; memcpy(nm, m->dir.filename, 32); nm[32] = 0; snprintf(m->title, sizeof(m->title), "%s", nm); }
        }
        if (ioff && f0) {
            u16 *t = (u16 *)memalign(32, 32 * 32 * 2);
            if (t) {
                if (f0 == 2) for (int p = 0; p < 32 * 32; p++) t[p] = mc_be16(buf + ioff + p * 2);   /* deja en tuiles RGB5A3 */
                else {
                    /* CI8 (tuiles 8x4) -> RGB5A3 (tuiles 4x4) */
                    u16 lin[32 * 32];
                    for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 4; tx++) for (int y = 0; y < 4; y++) for (int x = 0; x < 8; x++) {
                        u8 idx = buf[ioff + ((ty * 4 + tx) * 32) + y * 8 + x];
                        lin[(ty * 4 + y) * 32 + tx * 8 + x] = mc_be16(buf + itl + idx * 2);
                    }
                    int o = 0;
                    for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
                        t[o++] = lin[(ty * 4 + y) * 32 + tx * 4 + x];
                }
                DCFlushRange(t, 32 * 32 * 2);
                m->icon = t;
                GX_InitTexObj(&m->obj, t, 32, 32, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
                GX_InitTexObjFilterMode(&m->obj, GX_LINEAR, GX_LINEAR);
            }
        }
    }
    free(buf);
}
/* ecrit une sauvegarde (.gci en memoire) sur la carte du slot */
static int mc_import(int slot, const u8 *g, long len) {
    if (len < 64 + 8192) return CARD_ERROR_FATAL_ERROR;
    card_direntry src;
    mc_bytes_to_de(g, &src);
    long data = len - 64;
    if (data % mc_sector) return CARD_ERROR_FATAL_ERROR;
    char gc[5], co[3], nm[33];
    memcpy(gc, src.gamecode, 4); gc[4] = 0;
    memcpy(co, src.company, 2); co[2] = 0;
    memcpy(nm, src.filename, 32); nm[32] = 0;
    CARD_SetGamecode(gc); CARD_SetCompany(co);
    card_file f;
    int r = CARD_Create(slot, nm, (u32)data, &f);
    CARD_SetGamecode("GMP3"); CARD_SetCompany("GC");
    if (r < 0) return r;
    u8 *sec = (u8 *)memalign(32, mc_sector);
    if (!sec) { CARD_Close(&f); return CARD_ERROR_FATAL_ERROR; }
    for (long o = 0; o < data && r >= 0; o += mc_sector) {
        memcpy(sec, g + 64 + o, mc_sector);
        DCFlushRange(sec, mc_sector);
        r = CARD_Write(&f, sec, mc_sector, o);
        service_audio();
    }
    free(sec);
    int fileno = f.filenum;
    CARD_Close(&f);
    if (r < 0) return r;
    /* icone, banniere, commentaire, date, droits : comme dans le .gci */
    card_direntry e;
    if (CARD_GetStatusEx(slot, fileno, &e) == CARD_ERROR_READY) {
        e.banner_fmt = src.banner_fmt; e.last_modified = src.last_modified; e.icon_addr = src.icon_addr;
        e.icon_fmt = src.icon_fmt; e.icon_speed = src.icon_speed; e.permission = src.permission;
        e.copy_times = src.copy_times; e.comment_addr = src.comment_addr;
        CARD_SetStatusEx(slot, fileno, &e);
    }
    return 0;
}
/* sauvegarde i -> .gci en memoire (64 octets d'en-tete + donnees) */
static u8 *mc_export(int i, long *glen) {
    long n = 0;
    u8 *d = mc_read_all(i, &n);
    if (!d) return NULL;
    u8 *g = (u8 *)malloc(64 + n);
    if (!g) { free(d); return NULL; }
    memcpy(g, mc[i].de, 64);
    memcpy(g + 64, d, n);
    free(d);
    *glen = 64 + n;
    return g;
}
static void mc_safe_name(const u8 *de, char *out, int cap) {
    char nm[33]; memcpy(nm, de + 8, 32); nm[32] = 0;
    char clean[40]; int n = 0;
    for (int i = 0; nm[i] && n < 32; i++) { char c = nm[i]; clean[n++] = (isalnum((u8)c) || c == '-' || c == '_' || c == '.') ? c : '_'; }
    clean[n] = 0;
    snprintf(out, cap, "%c%c%c%c%c%c-%s", de[0], de[1], de[2], de[3], de[4], de[5], clean);
    for (int i = 0; out[i]; i++) if (!isalnum((u8)out[i]) && out[i] != '-' && out[i] != '_' && out[i] != '.') out[i] = '_';
}
/* v145 : .gci en memoire -> SD:/gci (nom libre) ; rend 1 si ecrit (nom dans out) ; v147 : ou un autre dossier */
static int mc_gci_write_in(const char *dir, const u8 *g, long n, char *out, int cap) {
    mkdir(dir, 0777);
    char base[80], p[NAME_LEN];
    mc_safe_name(g, base, sizeof(base));
    snprintf(p, sizeof(p), "%s/%s.gci", dir, base);
    struct stat st;
    for (int k = 2; stat(p, &st) == 0 && k < 100; k++) snprintf(p, sizeof(p), "%s/%s_%d.gci", dir, base, k);
    FILE *f = fopen(p, "wb");
    int ok = f && fwrite(g, 1, n, f) == (size_t)n;
    if (f && fclose(f) != 0) ok = 0;
    if (!ok) { remove(p); return 0; }
    if (out) snprintf(out, cap, "%s", base_of(p));
    return 1;
}
static int mc_gci_write(const u8 *g, long n, char *out, int cap) { mkdir(MC_DIR, 0777); return mc_gci_write_in(MC_DIR, g, n, out, cap); }
static void mc_to_sd(int i) {
    tl_status("Copie sur la carte SD...");
    long n = 0;
    u8 *g = mc_export(i, &n);
    gl_busy[0] = 0;
    if (!g) { tl_note("Lecture de la sauvegarde impossible"); return; }
    char nm[96];
    int ok = mc_gci_write(g, n, nm, sizeof(nm));
    free(g);
    if (!ok) { tl_note("\xC3\x89" "criture sur la carte SD impossible"); return; }
    snprintf(tl_msg, sizeof(tl_msg), "Copi\xC3\xA9" "e : SD:/gci/%s", nm); tl_msg_t = now_ms();
}
/* v145 : bilan d'une copie de plusieurs sauvegardes */
static void mc_bilan(int ok, int dup, int bad, const char *err) {
    char m[200];
    int l = snprintf(m, sizeof(m), ok == 1 ? "1 sauvegarde copi\xC3\xA9" "e" : "%d sauvegardes copi\xC3\xA9" "es", ok);
    if (dup) l += snprintf(m + l, sizeof(m) - l, ", %d d\xC3\xA9j\xC3\xA0 l\xC3\xA0", dup);
    if (bad && l < (int)sizeof(m)) snprintf(m + l, sizeof(m) - l, ", %d impossible%s (%s)", bad, bad > 1 ? "s" : "", err ? err : "?");
    snprintf(tl_msg, sizeof(tl_msg), "%s", m); tl_msg_t = now_ms();
}
/* v145 : une sauvegarde (ou toutes) de la vraie carte -> carte virtuelle de Pulse */
static void mc_to_vc(int i, int all) {
    const char *e = vc_load_active();
    if (e) { tl_note(e); return; }
    int ok = 0, dup = 0, bad = 0;
    const char *err = NULL;
    for (int k = all ? 0 : i; k < (all ? mc_n : i + 1); k++) {
        tl_status("Copie sur la carte virtuelle...");
        long n = 0;
        u8 *g = mc_export(k, &n);
        const char *r = g ? vc_import(g, n) : "lecture impossible";
        free(g);
        if (!r) ok++;
        else if (strstr(r, "existe d")) dup++;
        else { bad++; if (!err) err = r; }
    }
    gl_busy[0] = 0;
    if (!all) { tl_note(ok ? "Sauvegarde copi\xC3\xA9" "e sur la carte virtuelle de Pulse" : dup ? "Cette sauvegarde est d\xC3\xA9j\xC3\xA0 sur la carte virtuelle" : err); return; }
    mc_bilan(ok, dup, bad, err);
}
/* v147 : partage par QR code : une sauvegarde (.gci dans SD:/gci) ou toutes (dossier SD:/gci/sauvegardes, vide avant) */
#define MC_QR_DIR MC_DIR "/sauvegardes"
static void mc_qr_clear(void) {
    mkdir(MC_DIR, 0777); mkdir(MC_QR_DIR, 0777);
    DIR *d = opendir(MC_QR_DIR);
    struct dirent *e;
    char p[NAME_LEN];
    while (d && (e = readdir(d)) != NULL) { if (e->d_name[0] == '.') continue; snprintf(p, sizeof(p), "%s/%s", MC_QR_DIR, e->d_name); remove(p); }
    if (d) closedir(d);
}
static int mc_gci_write_in(const char *dir, const u8 *g, long n, char *out, int cap);
static void mc_qr_one(const u8 *g, long n) {
    char nm[96], p[NAME_LEN];
    mc_qr_clear();
    if (!g || !mc_gci_write_in(MC_QR_DIR, g, n, nm, sizeof(nm))) { tl_note("\xC3\x89" "criture sur la carte SD impossible"); return; }
    snprintf(p, sizeof(p), "%s/%s", MC_QR_DIR, nm);
    share_open(p, 0);
}
static void mc_to_qr(int i, int all) {
    if (!all) { tl_status("Pr\xC3\xA9paration..."); long n = 0; u8 *g = mc_export(i, &n); gl_busy[0] = 0; if (!g) { tl_note("Lecture de la sauvegarde impossible"); return; } mc_qr_one(g, n); free(g); return; }
    mc_qr_clear();
    int ok = 0;
    for (int k = 0; k < mc_n; k++) {
        tl_status("Pr\xC3\xA9paration...");
        long n = 0;
        u8 *g = mc_export(k, &n);
        if (g && mc_gci_write_in(MC_QR_DIR, g, n, NULL, 0)) ok++;
        free(g);
    }
    gl_busy[0] = 0;
    if (!ok) { tl_note("Aucune sauvegarde copi\xC3\xA9" "e"); return; }
    share_open(MC_QR_DIR, 1);
}
static void mc_all_to_sd(void) {
    int ok = 0, bad = 0;
    for (int k = 0; k < mc_n; k++) {
        tl_status("Copie sur la carte SD...");
        long n = 0;
        u8 *g = mc_export(k, &n);
        if (g && mc_gci_write(g, n, NULL, 0)) ok++; else bad++;
        free(g);
    }
    gl_busy[0] = 0;
    mc_bilan(ok, 0, bad, "\xC3\xA9" "criture");
}
static void mc_to_other(int i) {
    tl_status("Copie vers l'autre carte...");
    long n = 0;
    u8 *g = mc_export(i, &n);
    if (!g) { gl_busy[0] = 0; tl_note("Lecture de la sauvegarde impossible"); return; }
    int from = mc_slot, to = 1 - mc_slot;
    if (!mc_mount(to)) { free(g); tl_note(mc_errtxt(mc_err)); mc_mount(from); return; }
    tl_status("Copie vers l'autre carte...");
    int r = mc_import(to, g, n);
    free(g);
    gl_busy[0] = 0;
    mc_mount(from);
    if (r < 0) tl_note(mc_errtxt(r));
    else tl_note(to ? "Copi\xC3\xA9" "e sur la carte du slot B" : "Copi\xC3\xA9" "e sur la carte du slot A");
}
static void mc_delete(int i) {
    int r = CARD_DeleteEntry(mc_mounted, &mc[i].dir);
    if (r < 0) { tl_note(mc_errtxt(r)); return; }
    int keep = mc_sel;
    mc_mount(mc_slot);
    mc_sel = keep < mc_n + 1 ? keep : mc_n;
    tl_note("Sauvegarde supprim\xC3\xA9" "e");
}
static int mc_gcmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static void mc_gci_scan(void) {
    if (!mc_gfiles) mc_gfiles = malloc(sizeof(*mc_gfiles) * 300);
    mc_gn = 0;
    if (!mc_gfiles) return;
    DIR *d = opendir(MC_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && mc_gn < 300) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || (strcasecmp(dot, ".gci") && strcasecmp(dot, ".gcs") && strcasecmp(dot, ".sav")) || strlen(e->d_name) >= 96) continue;
        snprintf(mc_gfiles[mc_gn++], 96, "%s", e->d_name);
    }
    closedir(d);
    qsort(mc_gfiles, mc_gn, 96, mc_gcmp);
    if (mc_gsel >= mc_gn) mc_gsel = mc_gn ? mc_gn - 1 : 0;
}
/* .gcs (GameShark) et .sav (MaxDrive) : en-tete different avant les memes 64 octets */
static void mc_restore(int gi) {
    char p[NAME_LEN];
    snprintf(p, sizeof(p), "%s/%s", MC_DIR, mc_gfiles[gi]);
    long len = 0;
    u8 *d = read_file(p, 8L * 1024 * 1024 + 0x200, &len);
    if (!d) { tl_note("Lecture du fichier impossible"); return; }
    const char *dot = strrchr(p, '.');
    long skip = 0;
    if (!strcasecmp(dot, ".gcs")) skip = 0x110;
    else if (!strcasecmp(dot, ".sav")) {
        skip = 0x80;                                          /* (MaxDrive : octets de l'en-tete inverses par paires) */
        if (len > skip + 64) for (int i = 0x06; i < 0x40; i += 2) { u8 t = d[skip + i]; d[skip + i] = d[skip + i + 1]; d[skip + i + 1] = t; }
    }
    if (len < skip + 64 + 8192 || (len - skip - 64) % 8192) { free(d); tl_note("Ce fichier n'est pas une sauvegarde GameCube valide"); return; }
    tl_status("Copie sur la carte m\xC3\xA9moire...");
    int r = mc_import(mc_slot, d + skip, len - skip);
    gl_busy[0] = 0;
    free(d);
    if (r < 0) { tl_note(mc_errtxt(r)); return; }
    mc_gci = 0;
    mc_mount(mc_slot);
    tl_note("Sauvegarde copi\xC3\xA9" "e sur la carte m\xC3\xA9moire");
}

static void mc_open(void) {
    tl_screen = TL_CARDS; mc_gci = 0; mc_pop = 0; mc_sel = 0; mc_top = 0;
    mc_mount(mc_slot);
}
enum { MCP_SD, MCP_OTHER, MCP_VC, MCP_QR, MCP_ALLVC, MCP_ALLSD, MCP_ALLQR, MCP_DEL, MCP_COUNT };
static const char *const mcp_names[MCP_COUNT] = { "Copier sur la carte SD (.gci)", "Copier vers l'autre carte", "Copier vers la carte virtuelle (Pulse)",
    "Envoyer au t\xC3\xA9l\xC3\xA9phone (QR code)", "Tout copier vers la carte virtuelle", "Tout copier sur la carte SD (.gci)",
    "Tout envoyer au t\xC3\xA9l\xC3\xA9phone (QR code)", "Supprimer" };
static int gm_hub_input(void);
static void mc_input(u32 down, u32 nav, int fire) {
    if (!mc_pop && !mc_gci && gm_hub_input()) return;     /* v84 : stick C : autre vue */
    if (mc_pop) {
        if (fire && (nav & PAD_BUTTON_DOWN)) { mc_psel = (mc_psel + 1) % MCP_COUNT; mc_pconfirm = 0; }
        if (fire && (nav & PAD_BUTTON_UP)) { mc_psel = (mc_psel + MCP_COUNT - 1) % MCP_COUNT; mc_pconfirm = 0; }
        if (down & PAD_BUTTON_A) {
            int i = mc_sel - 1;
            if (i < 0 || i >= mc_n) { mc_pop = 0; return; }
            if (mc_psel == MCP_DEL && !mc_pconfirm) { mc_pconfirm = 1; return; }
            mc_pop = 0; mc_pconfirm = 0;
            if (mc_psel == MCP_SD) mc_to_sd(i);
            else if (mc_psel == MCP_OTHER) mc_to_other(i);
            else if (mc_psel == MCP_VC || mc_psel == MCP_ALLVC) mc_to_vc(i, mc_psel == MCP_ALLVC);
            else if (mc_psel == MCP_ALLSD) mc_all_to_sd();
            else if (mc_psel == MCP_QR || mc_psel == MCP_ALLQR) mc_to_qr(i, mc_psel == MCP_ALLQR);   /* v147 */
            else mc_delete(i);
        }
        if (down & (PAD_BUTTON_B | PAD_BUTTON_X)) { mc_pop = 0; mc_pconfirm = 0; }
        return;
    }
    if (mc_gci) {                                              /* choisir un .gci a remettre */
        if (fire && (nav & PAD_BUTTON_DOWN) && mc_gsel < mc_gn - 1) mc_gsel++;
        if (fire && (nav & PAD_BUTTON_UP) && mc_gsel > 0) mc_gsel--;
        if (fire && (nav & PAD_BUTTON_RIGHT)) { mc_gsel += 10; if (mc_gsel > mc_gn - 1) mc_gsel = mc_gn - 1; if (mc_gsel < 0) mc_gsel = 0; }
        if (fire && (nav & PAD_BUTTON_LEFT)) { mc_gsel -= 10; if (mc_gsel < 0) mc_gsel = 0; }
        if ((down & PAD_BUTTON_A) && mc_gn) { if (mc_mounted < 0) tl_note(mc_errtxt(mc_err)); else mc_restore(mc_gsel); }
        if (down & PAD_BUTTON_B) mc_gci = 0;
        return;
    }
    if (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) {              /* slot A, slot B, v145 : carte virtuelle (onglet JEUX) */
        int to = (mc_slot + ((down & PAD_TRIGGER_R) ? 1 : 2)) % 3;
        if (to == 2 && tab != TAB_GAMES) to = 1 - mc_slot;
        if (to == 2) { mc_unmount(); vc_open(); vc_hub = 1; return; }
        mc_slot = to; mc_sel = 0; mc_top = 0; mc_mount(mc_slot); return;
    }
    if (fire && (nav & PAD_BUTTON_DOWN) && mc_sel < mc_n) mc_sel++;
    if (fire && (nav & PAD_BUTTON_UP) && mc_sel > 0) mc_sel--;
    if (fire && (nav & PAD_BUTTON_RIGHT)) { mc_sel += 7; if (mc_sel > mc_n) mc_sel = mc_n; }
    if (fire && (nav & PAD_BUTTON_LEFT)) { mc_sel -= 7; if (mc_sel < 0) mc_sel = 0; }
    if (down & PAD_BUTTON_A) {
        if (mc_sel == 0) { mc_gci = 1; mc_gci_scan(); if (!mc_gn) tl_note("Aucun fichier .gci dans SD:/gci"); }
        else if (mc_mounted >= 0) { mc_pop = 1; mc_psel = 0; mc_pconfirm = 0; }
    }
    if ((down & PAD_BUTTON_X) && mc_sel > 0 && mc_sel <= mc_n) { mc_pop = 1; mc_psel = 0; mc_pconfirm = 0; }
    if (down & PAD_BUTTON_Y) mc_mount(mc_slot);
    if (down & PAD_BUTTON_B) { mc_unmount(); tl_screen = TL_NONE; }
}

/* =================== applis (.dol) =================== */
#define AP_MAX 200
typedef struct { char path[NAME_LEN]; char name[64]; char desc[96]; long size; char icon[NAME_LEN]; s8 istate; u16 *tex; GXTexObj obj; } ap_ent;
/* v52 : chaque appli a une vraie "pochette" (portrait 3:4, comme les jeux) :
   - cover.png / pochette.png / boxart.png dans son dossier : utilisee telle quelle (recadree)
   - sinon icon.png : en fond, l'icone agrandie et floutee (assombrie) ; devant, l'icone entiere, en grand */
#define AP_IW 120
#define AP_IH 160
/* moyenne (ponderee par l'alpha) de px sur le rectangle [x0,x1[ x [y0,y1[ */
static void ap_avg(const unsigned char *px, int w, int h, f32 x0, f32 y0, f32 x1, f32 y1, u32 *o) {
    int sx0 = (int)x0, sy0 = (int)y0, sx1 = (int)ceilf(x1), sy1 = (int)ceilf(y1);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx0 > w - 1) sx0 = w - 1;
    if (sy0 > h - 1) sy0 = h - 1;
    if (sx1 > w) sx1 = w;
    if (sy1 > h) sy1 = h;
    if (sx1 <= sx0) sx1 = sx0 + 1;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    u32 r = 0, g = 0, b = 0, al = 0, c = 0;
    int step = (sx1 - sx0) * (sy1 - sy0) > 4096 ? 4 : 1;     /* (grands blocs : un pixel sur 4 suffit) */
    for (int yy = sy0; yy < sy1; yy += step)
        for (int xx = sx0; xx < sx1; xx += step) {
            const unsigned char *p = px + ((size_t)yy * w + xx) * 4;
            r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; al += p[3]; c++;
        }
    if (al) { r /= al; g /= al; b /= al; }
    o[0] = r; o[1] = g; o[2] = b; o[3] = c ? al / c : 0;
}
static unsigned char *ap_png(const char *path, int *w, int *h) {
    long len = 0;
    unsigned char *d = read_file(path, 3 * 1024 * 1024, &len);
    if (!d) return NULL;
    int n = 0;
    unsigned char *px = stbi_load_from_memory(d, (int)len, w, h, &n, 4);
    free(d);
    if (px && (*w <= 0 || *h <= 0)) { stbi_image_free(px); px = NULL; }
    return px;
}
/* v68 : pochettes deja calculees, gardees sur la carte SD (SD:/gc-mp3/cache/applis) : la 2e fois,
   une simple lecture de 38 Ko au lieu de decoder le PNG et de tout recalculer (l'onglet ramait) */
#define AP_CACHE GCD "/cache/applis"
static void ap_cache_name(const ap_ent *a, char *out, int cap) {
    char dir[NAME_LEN], p[NAME_LEN];
    snprintf(dir, sizeof(dir), "%s", a->icon);
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0;
    static const char *const cn[] = { "cover.png", "pochette.png", "boxart.png", "cover.jpg", "pochette.jpg" };
    u32 hsh = 2166136261u;
    struct stat st;
    for (int i = 0; i < 6; i++) {
        if (i < 5) snprintf(p, sizeof(p), "%s/%s", dir, cn[i]); else snprintf(p, sizeof(p), "%s", a->icon);
        if (stat(p, &st) == 0) {
            for (const char *q = p; *q; q++) hsh = (hsh ^ (u8)*q) * 16777619u;
            hsh ^= (u32)st.st_size * 2654435761u ^ (u32)st.st_mtime;
            break;
        }
    }
    snprintf(out, cap, AP_CACHE "/%08x.tex", (unsigned)hsh);
}
static int ap_cache_read(ap_ent *a, const char *cp) {
    FILE *f = fopen(cp, "rb");
    if (!f) return 0;
    u16 *t = (u16 *)memalign(32, AP_IW * AP_IH * 2);
    int ok = t && fread(t, 1, AP_IW * AP_IH * 2, f) == AP_IW * AP_IH * 2;
    fclose(f);
    if (!ok) { free(t); return 0; }
    DCFlushRange(t, AP_IW * AP_IH * 2);
    a->tex = t;
    GX_InitTexObj(&a->obj, t, AP_IW, AP_IH, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&a->obj, GX_LINEAR, GX_LINEAR);
    a->istate = 1;
    return 1;
}
/* v110 : la pochette est calculee dans le thread de fond (avant : dans la boucle principale, jusqu'a une
   demi-seconde par appli pour decoder un grand PNG : l'onglet Applis ramait beaucoup). ap_build ne touche
   qu'a son propre tampon ; la boucle principale l'installe ensuite (ap_job_done). */
static u16 *ap_build(const char *icon);
static void ap_icon_load(ap_ent *a) {
    a->istate = -1;
    if (!a->icon[0]) return;
    u16 *t = ap_build(a->icon);
    if (!t) return;
    a->tex = t;
    GX_InitTexObj(&a->obj, t, AP_IW, AP_IH, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&a->obj, GX_LINEAR, GX_LINEAR);
    a->istate = 1;
}
static u16 *ap_build(const char *icon) {
    ap_ent tmp, *a = &tmp;
    memset(a, 0, sizeof(*a));
    snprintf(a->icon, sizeof(a->icon), "%s", icon);
    char cachep[NAME_LEN];
    ap_cache_name(a, cachep, sizeof(cachep));
    if (ap_cache_read(a, cachep)) return a->tex;
    char dir[NAME_LEN], p[NAME_LEN];
    snprintf(dir, sizeof(dir), "%s", a->icon);
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0;
    int w = 0, h = 0, cover = 0;
    unsigned char *px = NULL;
    static const char *const cn[] = { "cover.png", "pochette.png", "boxart.png", "cover.jpg", "pochette.jpg" };
    for (int i = 0; i < 5 && !px; i++) {
        snprintf(p, sizeof(p), "%s/%s", dir, cn[i]);
        struct stat st;
        if (stat(p, &st) == 0 && (px = ap_png(p, &w, &h)) != NULL) cover = 1;
    }
    if (!px) px = ap_png(a->icon, &w, &h);
    if (!px) return NULL;
    u16 *t = (u16 *)memalign(32, AP_IW * AP_IH * 2);
    if (!t) { stbi_image_free(px); return NULL; }
    /* le fond : l'image en "remplissage" (recadree), en 15 x 20 blocs, agrandie en douceur */
    enum { BW = 15, BH = 20 };
    static u32 bg[BH][BW][4];
    f32 fs = (f32)w / AP_IW > (f32)h / AP_IH ? (f32)h / AP_IH : (f32)w / AP_IW;   /* (source par pixel de la pochette) */
    f32 cx0 = (w - AP_IW * fs) / 2, cy0 = (h - AP_IH * fs) / 2, bs = fs * AP_IW / BW;
    for (int by = 0; by < BH; by++)
        for (int bx = 0; bx < BW; bx++)
            ap_avg(px, w, h, cx0 + bx * bs, cy0 + by * bs, cx0 + (bx + 1) * bs, cy0 + (by + 1) * bs, bg[by][bx]);
    /* devant : l'icone entiere (marge 6), centree un peu vers le haut */
    f32 sc = (f32)(AP_IW - 12) / w < (f32)(AP_IH - 12) / h ? (f32)(AP_IW - 12) / w : (f32)(AP_IH - 12) / h;
    int dw = (int)(w * sc + 0.5f), dh = (int)(h * sc + 0.5f);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ox = (AP_IW - dw) / 2, oy = (AP_IH - dh) * 2 / 5;
    for (int y = 0; y < AP_IH; y++) {
        for (int x = 0; x < AP_IW; x++) {
            u32 c[4];
            int R, G, B;
            if (cover) {                                                /* la pochette fournie : recadree */
                ap_avg(px, w, h, cx0 + x * fs, cy0 + y * fs, cx0 + (x + 1) * fs, cy0 + (y + 1) * fs, c);
                R = (int)(c[0] * c[3] / 255); G = (int)(c[1] * c[3] / 255); B = (int)(c[2] * c[3] / 255);
            } else {
                /* fond flou : interpolation des blocs, assombri, un peu plus sombre en bas */
                f32 fx = (x + 0.5f) * BW / AP_IW - 0.5f, fy = (y + 0.5f) * BH / AP_IH - 0.5f;
                int ix = (int)floorf(fx), iy = (int)floorf(fy);
                f32 ax = fx - ix, ay = fy - iy;
                int x1 = ix + 1, y1 = iy + 1;
                if (ix < 0) ix = 0;
                if (iy < 0) iy = 0;
                if (x1 > BW - 1) x1 = BW - 1;
                if (y1 > BH - 1) y1 = BH - 1;
                f32 v[3];
                for (int k = 0; k < 3; k++) {
                    f32 a0 = bg[iy][ix][k] * bg[iy][ix][3] / 255.0f, a1 = bg[iy][x1][k] * bg[iy][x1][3] / 255.0f;
                    f32 b0 = bg[y1][ix][k] * bg[y1][ix][3] / 255.0f, b1 = bg[y1][x1][k] * bg[y1][x1][3] / 255.0f;
                    v[k] = (a0 + (a1 - a0) * ax) * (1 - ay) + (b0 + (b1 - b0) * ax) * ay;
                }
                f32 dk = 0.42f - 0.16f * y / AP_IH;
                static const f32 base[3] = { 34, 20, 84 };                      /* (violet GC Station dessous) */
                R = (int)(base[0] * 0.6f + v[0] * dk); G = (int)(base[1] * 0.6f + v[1] * dk); B = (int)(base[2] * 0.6f + v[2] * dk);
                int dx = x - ox, dy = y - oy;
                if (dx >= 0 && dy >= 0 && dx < dw && dy < dh) {
                    ap_avg(px, w, h, dx * (f32)w / dw, dy * (f32)h / dh, (dx + 1) * (f32)w / dw, (dy + 1) * (f32)h / dh, c);
                    R += (int)(((int)c[0] - R) * (int)c[3] / 255); G += (int)(((int)c[1] - G) * (int)c[3] / 255); B += (int)(((int)c[2] - B) * (int)c[3] / 255);
                }
            }
            if (R > 255) R = 255;
            if (G > 255) G = 255;
            if (B > 255) B = 255;
            t[((y >> 2) * (AP_IW >> 2) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)] = (u16)(0x8000 | ((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3));
        }
        if ((y & 31) == 31) usleep(200);                         /* (thread de fond : laisse la main) */
    }
    stbi_image_free(px);
    if (sd_ok && !sd_virtual) {                                         /* v68 : gardee pour la prochaine fois */
        mkdir(GCD, 0777); mkdir(GCD "/cache", 0777); mkdir(AP_CACHE, 0777);
        FILE *cf = fopen(cachep, "wb");
        if (cf) { int okw = fwrite(t, 1, AP_IW * AP_IH * 2, cf) == AP_IW * AP_IH * 2; if (fclose(cf) || !okw) remove(cachep); }
    }
    DCFlushRange(t, AP_IW * AP_IH * 2);
    return t;
}
static ap_ent *ap = NULL;
static int ap_n = 0, ap_sel = 0, ap_confirm = -1;
/* <name> et <short_description> de apps/xxx/meta.xml (format Homebrew Channel) */
static void ap_meta(const char *dir, ap_ent *a) {
    char p[NAME_LEN];
    snprintf(p, sizeof(p), "%s/meta.xml", dir);
    long len = 0;
    char *d = (char *)read_file(p, 16384, &len);
    if (!d) return;
    char *t = (char *)realloc(d, len + 1); if (!t) { free(d); return; } d = t; d[len] = 0;
    static const char *const tags[2] = { "name", "short_description" };
    for (int k = 0; k < 2; k++) {
        char o[48], c[48];
        snprintf(o, sizeof(o), "<%s>", tags[k]); snprintf(c, sizeof(c), "</%s>", tags[k]);
        char *s = strstr(d, o), *e = s ? strstr(s, c) : NULL;
        if (!s || !e) continue;
        s += strlen(o);
        int n = (int)(e - s);
        char v[128];
        if (n > (int)sizeof(v) - 1) n = sizeof(v) - 1;
        memcpy(v, s, n); v[n] = 0;
        if (k == 0 && v[0]) snprintf(a->name, sizeof(a->name), "%s", v);
        if (k == 1 && v[0]) snprintf(a->desc, sizeof(a->desc), "%s", v);
    }
    free(d);
}
static void ap_add(const char *path, const char *dir) {
    if (!ap || ap_n >= AP_MAX) return;
    for (int i = 0; i < ap_n; i++) if (!strcasecmp(ap[i].path, path)) return;
    ap_ent *a = &ap[ap_n];
    memset(a, 0, sizeof(*a));
    snprintf(a->path, sizeof(a->path), "%s", path);
    struct stat st;
    a->size = stat(path, &st) == 0 ? (long)st.st_size : 0;
    if (a->size < 0x100) return;
    const char *b = base_of(path);
    if (dir) snprintf(a->name, sizeof(a->name), "%s", base_of(dir));     /* (v41 : nom du dossier, ex. VBA-GX au lieu de default) */
    else { snprintf(a->name, sizeof(a->name), "%s", b); char *dot = strrchr(a->name, '.'); if (dot) *dot = 0; }
    if (dir) { ap_meta(dir, a); snprintf(a->icon, sizeof(a->icon), "%s/icon.png", dir); }
    {   /* v110 : seulement les applis qui ont une image (icon.png, ou une pochette dans leur dossier) */
        static const char *const cn[] = { "icon.png", "cover.png", "pochette.png", "boxart.png", "cover.jpg", "pochette.jpg" };
        int img = 0;
        char ip[NAME_LEN];
        for (int i = 0; dir && i < 6 && !img; i++) { snprintf(ip, sizeof(ip), "%s/%s", dir, cn[i]); img = stat(ip, &st) == 0; }
        if (!img) return;
    }
    ap_n++;
}
static void ap_scan_dir(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && ap_n < AP_MAX) {
        if (e->d_name[0] == '.') continue;
        char p[NAME_LEN];
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcasecmp(e->d_name + l - 4, ".dol")) { ap_add(p, depth == 0 && strcasecmp(dir, SD_ROOT) ? dir : NULL); continue; }
        if (depth > 0 && is_dir(p, e)) ap_scan_dir(p, depth - 1);
    }
    closedir(d);
    service_audio();
}
static int ap_cmp(const void *a, const void *b) { return strcasecmp(((const ap_ent *)a)->name, ((const ap_ent *)b)->name); }
static int ap_scanned = 0;                            /* v84 : liste gardee (Y : relire la carte) */
static void ap_rescan(void);
static void ap_open(void) {
    ap_confirm = -1;
    tl_screen = TL_APPS;
    if (!ap_scanned) ap_rescan();
}
static void ap_rescan(void) {
    if (!ap) ap = (ap_ent *)malloc(sizeof(ap_ent) * AP_MAX);
    for (int i = 0; ap && i < ap_n; i++) if (ap[i].tex) { free(ap[i].tex); ap[i].tex = NULL; }
    ap_n = 0; ap_confirm = -1;
    if (!ap || !sd_ok) return;
    ap_scanned = 1;
    ap_scan_dir(SD_ROOT "/apps", 1);
    ap_scan_dir(SD_ROOT, 0);
    qsort(ap, ap_n, sizeof(ap_ent), ap_cmp);
    if (ap_sel >= ap_n) ap_sel = ap_n ? ap_n - 1 : 0;
}
/* v110 : pochette d'une appli calculee en fond */
static struct { int k; char icon[NAME_LEN]; u16 *res; } apj = { -1, "", NULL };
static void ap_job_fn(void) { apj.res = ap_build(apj.icon); }
static void ap_job_done(void) {
    u16 *t = apj.res;
    int k = apj.k;
    apj.res = NULL; apj.k = -1;
    if (!ap || k < 0 || k >= ap_n || strcmp(ap[k].icon, apj.icon) || ap[k].istate != 2) { free(t); return; }
    ap_ent *a = &ap[k];
    if (!t) { a->istate = -1; return; }
    a->tex = t;
    GX_InitTexObj(&a->obj, t, AP_IW, AP_IH, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&a->obj, GX_LINEAR, GX_LINEAR);
    a->istate = 1;
}
static int ap_post(int k) {                              /* 1 = demande envoyee */
    if (!ap[k].icon[0]) { ap[k].istate = -1; return 0; }
    apj.k = k; snprintf(apj.icon, sizeof(apj.icon), "%s", ap[k].icon); apj.res = NULL;
    if (!gj_post_fn(ap_job_fn, ap_job_done)) { apj.k = -1; return 0; }
    ap[k].istate = 2;                                    /* (en cours) */
    return 1;
}
#define AP_COLS 4
static void ap_input(u32 down, u32 nav, int fire) {
    if (ap_confirm >= 0) {
        if (down & PAD_BUTTON_A) {
            int k = ap_confirm;
            ap_confirm = -1;
            if (settings_dirty) save_settings();
            const char *e = gl_boot_dol(ap[k].path, NULL, NULL, ap[k].name);   /* (ne revient pas si ca marche) */
            gl_busy[0] = 0;
            if (e) tl_note(e);
        }
        if (down & PAD_BUTTON_B) ap_confirm = -1;
        return;
    }
    /* v42 : grille comme Mes jeux, en boucle */
    if (fire && ap_n) {
        if (nav & PAD_BUTTON_RIGHT) ap_sel = nav_wrap(ap_sel, ap_n, AP_COLS, 1, 0);
        if (nav & PAD_BUTTON_LEFT) ap_sel = nav_wrap(ap_sel, ap_n, AP_COLS, -1, 0);
        if (nav & PAD_BUTTON_DOWN) ap_sel = nav_wrap(ap_sel, ap_n, AP_COLS, 0, 1);
        if (nav & PAD_BUTTON_UP) ap_sel = nav_wrap(ap_sel, ap_n, AP_COLS, 0, -1);
    }
    if ((down & PAD_BUTTON_A) && ap_n) ap_confirm = ap_sel;
    if (down & PAD_BUTTON_Y) { ap_rescan(); tl_note("Liste actualis\xC3\xA9" "e"); }
    if (down & PAD_BUTTON_B) tl_screen = TL_NONE;
    gm_hub_input();
}

/* ---------- v84 : onglet Jeux / Apps : passer directement d'une vue a l'autre ----------
   Jeux GameCube, Mes applis, Cartes memoire : une barre en haut de chaque vue montre ou l'on
   est, et le stick C (gauche / droite) passe a la vue voisine, sans revenir a l'onglet
   (avant : B, croix, A). Les listes sont gardees : le passage est immediat. */
static int gm_hub_cur(void) { return tl_screen == TL_APPS ? 1 : tl_screen == TL_CARDS || (tl_screen == TL_VCARD && vc_hub) ? 2 : (tl_screen == TL_NONE && gl_on) ? 0 : -1; }
static void gm_hub_go(int k) {
    int cur = gm_hub_cur();
    if (cur < 0 || k == cur) return;
    if (cur == 0) { gl_on = 0; if (gl_list_dirty) { gl_list_dirty = 0; gl_list_save(); } }
    tl_screen = TL_NONE; vc_hub = 0;
    gm_sel = GM_COUNT + k;                                 /* (B : retour sur la bonne barre de l'onglet) */
    if (k == 0) gclib_open(); else if (k == 1) ap_open(); else mc_open();
}
static int gm_hub_input(void) {                            /* 1 = on a change de vue */
    static int armed = 1;
    int sx = PAD_SubStickX(0);
    if (abs(sx) < 30) { armed = 1; return 0; }
    if (!armed || abs(sx) < 55 || tab != TAB_GAMES || gm_hub_cur() < 0) return 0;
    armed = 0;
    gm_hub_go((gm_hub_cur() + (sx > 0 ? 1 : 2)) % 3);
    return 1;
}
static f32 gm_hub_bar(f32 x, f32 y) {                      /* rend la fin (x) ; y = haut de la barre (20 de haut) */
    static const char *const nm[3] = { "Jeux", "Applis", "Cartes" };   /* (courts : tient a cote de l'affichage / des slots) */
    int cur = gm_hub_cur();
    for (int k = 0; k < 3; k++) {
        const char *t = TR(nm[k]);
        f32 w = (f32)text_width(F_SMALL, t, 1) + 22;
        int on = k == cur;
        gx_rrect_fill(x, y, w, 20, 6, on ? RGBA(116, 46, 236, 255) : RGBA(34, 20, 84, 230), on ? RGBA(74, 22, 172, 255) : RGBA(20, 12, 52, 230));
        if (!on) gx_rrect_stroke(x, y, w, 20, 6, 1.0f, RGBA(90, 64, 170, 255));
        gx_text1(F_SMALL, x + 11, y + 15, t, on ? RGBA(255, 255, 255, 255) : RGBA(170, 155, 225, 255));
        x += w + 4;
    }
    draw_mini_badge(x + 10, y + 10, "C");
    gx_text1(F_SMALL, x + 22, y + 15, "< >", RGBA(170, 155, 225, 255));
    return x + 22 + text_width(F_SMALL, "< >", 1);
}

/* =================== derniers jeux lances =================== */
#define RC_FILE GCD "/jeux_recents.txt"
#define RC_MAX 12
static char rc_path[RC_MAX * 2][NAME_LEN];
static int rc_n = 0, rc_sel = 0, rc_confirm = -1;
/* GC-MP3 : retient un jeu lance depuis l'onglet JEUX (le plus recent en premier) */
static void tl_recent_add(const char *path) {
    char old[RC_MAX][NAME_LEN];
    int n = 0;
    long len = 0;
    char *d = (char *)read_file(RC_FILE, 32768, &len);
    if (d) {
        long i = 0;
        while (i < len && n < RC_MAX) {
            long s = i;
            while (i < len && d[i] != '\n' && d[i] != '\r') i++;
            int l = (int)(i - s);
            while (i < len && (d[i] == '\n' || d[i] == '\r')) i++;
            if (l <= 0 || l >= NAME_LEN) continue;
            memcpy(old[n], d + s, l); old[n][l] = 0;
            if (strcmp(old[n], path)) n++;
        }
        free(d);
    }
    mkdir(GCD, 0777);
    FILE *f = fopen(RC_FILE, "wb");
    if (!f) return;
    fprintf(f, "%s\n", path);
    for (int i = 0; i < n && i < RC_MAX - 1; i++) fprintf(f, "%s\n", old[i]);
    fclose(f);
}
static void rc_push(const char *p) {
    if (rc_n >= RC_MAX * 2 || !p[0]) return;
    for (int i = 0; i < rc_n; i++) if (!strcasecmp(rc_path[i], p)) return;
    if (access(p, R_OK) != 0) return;
    snprintf(rc_path[rc_n++], NAME_LEN, "%s", p);
}
static void rc_open(void) {
    rc_n = 0; rc_confirm = -1;
    tl_screen = TL_RECENT;
    long len = 0;
    char *d = (char *)read_file(RC_FILE, 32768, &len);
    if (d) {
        long i = 0;
        while (i < len) {
            long s = i;
            while (i < len && d[i] != '\n' && d[i] != '\r') i++;
            int l = (int)(i - s);
            while (i < len && (d[i] == '\n' || d[i] == '\r')) i++;
            if (l <= 0 || l >= NAME_LEN) continue;
            char p[NAME_LEN]; memcpy(p, d + s, l); p[l] = 0;
            rc_push(p);
        }
        free(d);
    }
    /* liste du lanceur de secours : Recent_0=sdc:/games/xxx.iso (v90 : SD:/gc-mp3/lanceur) */
    d = (char *)read_file(LD_DIR "/settings/recent.ini", 32768, &len);
    if (d) {
        long i = 0;
        while (i < len) {
            long s = i;
            while (i < len && d[i] != '\n' && d[i] != '\r') i++;
            int l = (int)(i - s);
            while (i < len && (d[i] == '\n' || d[i] == '\r')) i++;
            if (l < 10 || l >= NAME_LEN || memcmp(d + s, "Recent_", 7)) continue;
            char v[NAME_LEN]; memcpy(v, d + s, l); v[l] = 0;
            char *eq = strchr(v, '=');
            if (!eq || !eq[1]) continue;
            char *c = strchr(eq + 1, ':');
            if (!c || c - eq > 6) continue;
            char p[NAME_LEN];
            snprintf(p, sizeof(p), "%s%s", SD_ROOT, c + 1);
            rc_push(p);
        }
        free(d);
    }
    if (!gl_scanned) gl_scan();                              /* (titres et ID des jeux) */
    if (rc_sel >= rc_n) rc_sel = rc_n ? rc_n - 1 : 0;
}
static int rc_game(int i) { for (int k = 0; k < gl_n; k++) if (!strcasecmp(gl[k].path, rc_path[i])) return k; return -1; }
static void rc_input(u32 down, u32 nav, int fire) {
    if (rc_confirm >= 0) {
        if (down & PAD_BUTTON_A) {
            int i = rc_confirm;
            rc_confirm = -1;
            if (settings_dirty) save_settings();
            int k = rc_game(i);
            const char *e;
            if (k >= 0) e = game_boot_auto(k);
            else {
                size_t l = strlen(rc_path[i]);
                if (l > 4 && !strcasecmp(rc_path[i] + l - 4, ".dol")) e = gl_boot_dol(rc_path[i], NULL, NULL, base_of(rc_path[i]));
                else { tl_recent_add(rc_path[i]); e = gl_boot_loader(rc_path[i]); }
            }
            gl_busy[0] = 0;
            if (e) tl_note(e);
        }
        if (down & PAD_BUTTON_B) rc_confirm = -1;
        return;
    }
    if (fire && (nav & PAD_BUTTON_DOWN) && rc_sel < rc_n - 1) rc_sel++;
    if (fire && (nav & PAD_BUTTON_UP) && rc_sel > 0) rc_sel--;
    if ((down & PAD_BUTTON_A) && rc_n) rc_confirm = rc_sel;
    if ((down & PAD_BUTTON_Y) && rc_n) { int k = rc_game(rc_sel); if (k >= 0) indigo_game_open(gl[k].id, gl[k].title); }
    if (down & PAD_BUTTON_B) tl_screen = TL_NONE;
}

/* =================== infos console =================== */
static int tl_info_fresh = 1;                         /* v45 : lignes a recalculer (ecran ouvert) */
static unsigned long long tl_sd_free = 0, tl_sd_tot = 0;
static int tl_sd_known = 0;
static void info_open(void) { tl_screen = TL_INFO; tl_info_fresh = 1; tl_sd_known = 0; }
static void info_lines(char (*l)[2][120], int *n) {
    int k = 0;
    #define IL(a, ...) do { snprintf(l[k][0], 120, "%s", TR(a)); snprintf(l[k][1], 120, __VA_ARGS__); k++; } while (0)
    IL("GC Station", "%s (build %d)", GCMP3_VERSION, GCMP3_BUILD);
    u32 tv = VIDEO_GetCurrentTvMode();
    IL("Console", "%s", tv == VI_PAL ? "PAL (50 Hz)" : tv == VI_EURGB60 ? "PAL (60 Hz)" : tv == VI_MPAL ? "PAL-M" : "NTSC");
    IL("Vid\xC3\xA9o", "%s%s", vid_active == VID_576P ? "576p" : vid_active == VID_576I ? "576i" : vid_active == VID_480P ? "480p" : "480i", VIDEO_HaveComponentCable() ? TR(" (c\xC3\xA2" "ble composante)") : "");
    static const char *const ln[] = { "English", "Deutsch", "Fran\xC3\xA7" "ais", "Espa\xC3\xB1ol", "Italiano", "Nederlands" };
    int lg = SYS_GetLanguage();
    IL("Langue de la console", "%s", lg >= 0 && lg < 6 ? ln[lg] : "?");
    IL("Son de la console", "%s", SYS_GetSoundMode() == SYS_SOUND_MONO ? TR("Mono") : TR("St\xC3\xA9r\xC3\xA9o"));
    IL("Position de l'\xC3\xA9" "cran", "%+d", (int)SYS_GetDisplayOffsetH());
    struct mallinfo mi = mallinfo();
    IL("M\xC3\xA9moire", ("%d Ko utilis\xC3\xA9s, %d Ko libres"), (int)(mi.uordblks / 1024), (int)(((u32)SYS_GetArena1Hi() - (u32)SYS_GetArena1Lo() + mi.fordblks) / 1024));
    static const char *const sl[3] = { "SD2SP2 (port s\xC3\xA9rie 2)", "SD Gecko slot A", "SD Gecko slot B" };
    if (sd_ok && !tl_sd_known && sd_space(&tl_sd_tot, &tl_sd_free)) tl_sd_known = 1;   /* v97 : FSInfo (instantane) */
    if (sd_ok && tl_sd_known) {
        unsigned long long tot = tl_sd_tot, fr = tl_sd_free;
        IL("Carte SD", ("%s : %llu Mo libres sur %llu Mo"), TR(sl[sd_slot >= 0 && sd_slot < 3 ? sd_slot : 0]), fr >> 20, tot >> 20);
    } else IL("Carte SD", "%s", TR("absente"));
    IL("R\xC3\xA9seau", "%s", net_phase != NET_DONE ? TR("pas encore connect\xC3\xA9") : net_ok ? ip_str : TR("pas de r\xC3\xA9seau"));
    if (net_ok && net_static) IL("", "%s", TR("adresse fixe"));
    if (ld_build() > 0) IL("Lanceur de secours", "build %d", ld_build());
    else IL("Lanceur de secours", "%s", TR("pas encore t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9"));
    IL("D\xC3\xA9marrage", "%s", access(SD_ROOT "/ipl.dol", R_OK) == 0 ? TR("ipl.dol pr\xC3\xA9sent \xC3\xA0 la racine") : TR("pas d'ipl.dol"));
    u64 s = now_ms() / 1000;
    IL("Allum\xC3\xA9" "e depuis", "%d h %02d min", (int)(s / 3600), (int)(s / 60 % 60));
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (tmv) IL("Horloge", "%02d/%02d/%04d %02d:%02d", tmv->tm_mday, tmv->tm_mon + 1, tmv->tm_year + 1900, tmv->tm_hour, tmv->tm_min);
    #undef IL
    *n = k;
}
static void info_input(u32 down, u32 nav, int fire) {
    (void)nav; (void)fire;
    if (down & (PAD_BUTTON_B | PAD_BUTTON_A)) tl_screen = TL_NONE;
}

/* =================== affichage =================== */
static void tl_legend(const char *const *K, const char *const *L, int cnt) {
    const f32 lx = 11, lw = 618, cy = 438.5f;
    if (ui_skin < 2 || tl_screen != TL_APPS || tab != TAB_GAMES) gx_rrect_fill(lx, 419, lw, 39, 9, RGBA(10, 6, 30, 255), RGBA(10, 6, 30, 255));
    legend_box(lx, lw);
    f32 total = 0;
    for (int i = 0; i < cnt; i++) total += (L[i][0] ? 25 : 22) + text_width(F_SMALL, L[i], 1);
    int gaps = 0; for (int i = 0; i < cnt; i++) if (L[i][0]) gaps++;
    f32 space = gaps > 1 ? (lw - 40 - total) / (gaps - 1) : 0, cx = lx + 20;
    for (int i = 0; i < cnt; i++) {
        draw_mini_badge(cx + 10, cy, K[i]);
        cx += L[i][0] ? 25 : 22;
        if (L[i][0]) cx += gx_text(F_SMALL, cx, cy + 4.5f, L[i], RGBA(208, 198, 240, 255), RGBA(208, 198, 240, 255), 1) + space;
    }
}
static void tl_frame(const char *title, const char *right) {
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
    gx_rrect_fill(40, 40, 560, 384, 10, RGBA(14, 8, 44, 252), RGBA(8, 5, 26, 252));
    draw_panel(40, 40, 560, 384);
    gx_text1(F_LARGE, 58, 72, title, RGBA(255, 255, 255, 255));
    if (right) gx_text_r(F_SMALL, 582, 70, right, RGBA(190, 160, 255, 255));
}
static void tl_msgbox(void) {
    if (now_ms() - tl_msg_t >= 4500) return;
    char fv[220];
    fit_text(F_SMALL, tl_msg, 516, fv, sizeof(fv));
    gx_rrect_fill(54, 384, 532, 24, 6, RGBA(40, 20, 90, 250), RGBA(30, 14, 70, 250));
    gx_text1(F_SMALL, 62, 401, fv, RGBA(255, 210, 120, 255));
}
static void tl_confirm(const char *q, const char *name) {
    gx_rrect_fill(120, 170, 400, 110, 10, RGBA(20, 10, 56, 252), RGBA(12, 6, 36, 252));
    draw_panel(120, 170, 400, 110);
    char fv[160];
    gx_text_c(F_MED, 320, 204, q, RGBA(255, 255, 255, 255));
    fit_text(F_MED, name, 360, fv, sizeof(fv));
    gx_text_c(F_MED, 320, 232, fv, RGBA(255, 210, 120, 255));
    gx_text_c(F_SMALL, 320, 264, "A : oui   B : non", RGBA(190, 180, 235, 255));
}
#define TL_SEL1 RGBA(116, 46, 236, 255)
#define TL_SEL2 RGBA(74, 22, 172, 255)

/* slots A / B (v145 : + carte virtuelle de Pulse, dans l'onglet JEUX) : L / R */
static void mc_pills(int cur) {
    int n = tab == TAB_GAMES ? 3 : 2;
    for (int s = 0; s < n; s++) {
        f32 px = (n == 3 ? 312 : 400) + s * 92;
        if (s == cur) gx_rrect_fill(px, 52, 86, 22, 6, TL_SEL1, TL_SEL2);
        gx_text_c(F_SMALL, px + 43, 68, s == 2 ? "Virtuelle" : s ? "Slot B" : "Slot A", s == cur ? RGBA(255, 255, 255, 255) : RGBA(170, 155, 225, 255));
    }
}
static void draw_cards(void) {
    char buf[160], fv[160];
    if (tab == TAB_GAMES) { tl_frame("", NULL); gm_hub_bar(58, 54); }   /* v84 */
    else tl_frame("Cartes m\xC3\xA9moire", NULL);
    mc_pills(mc_slot);
    if (mc_gci) {
        gx_text1(F_SMALL, 58, 98, "Remettre une sauvegarde de SD:/gci sur la carte :", RGBA(200, 190, 240, 255));
        const int vis = 11;
        int start = mc_gsel - vis / 2; if (start > mc_gn - vis) start = mc_gn - vis; if (start < 0) start = 0;
        for (int i = 0; i < vis && start + i < mc_gn; i++) {
            int r = start + i; f32 ry = 108 + i * 24;
            gp_zone(52, ry, 536, 22, &mc_gsel, r, GPZ_SEL);
            if (r == mc_gsel) gx_rrect_fill(52, ry, 536, 22, 5, TL_SEL1, TL_SEL2);
            text_display(mc_gfiles[r], buf, sizeof(buf)); fit_text(F_MED, buf, 510, fv, sizeof(fv));
            gx_text1(F_MED, 64, ry + 16, fv, RGBA(235, 225, 255, 255));
        }
        if (!mc_gn) gx_text1(F_SMALL, 64, 130, "Copie d'abord des sauvegardes (.gci) dans le dossier gci de la carte SD.", RGBA(170, 155, 225, 255));
        tl_msgbox();
        static const char *const k[] = { "A", "B" }, *const l[] = { "COPIER SUR LA CARTE", "RETOUR" };
        tl_legend(k, l, 2);
        return;
    }
    if (mc_mounted < 0) {
        gx_text1(F_MED, 64, 130, mc_errtxt(mc_err), RGBA(255, 170, 170, 255));
        gx_text1(F_SMALL, 64, 156, "L / R : changer de slot    Y : r\xC3\xA9" "essayer", RGBA(170, 155, 225, 255));
    } else {
        snprintf(buf, sizeof(buf), mc_n == 1 ? "1 sauvegarde, %u blocs libres" : "%d sauvegardes, %u blocs libres", mc_n, (unsigned)mc_free);
        if (mc_n == 1) snprintf(buf, sizeof(buf), "1 sauvegarde, %u blocs libres", (unsigned)mc_free);
        gx_text1(F_SMALL, 58, 96, buf, RGBA(200, 190, 240, 255));
    }
    if (mc_mounted < 0) {
        tl_msgbox();
        static const char *const k0[] = { "Y", "B", "L", "R" }, *const l0[] = { "R\xC3\x89" "ESSAYER", "RETOUR", "", "SLOT" };
        tl_legend(k0, l0, 4);
        return;
    }
    const int vis = 7;
    const f32 rh = 38;
    int rows = mc_n + 1;
    if (mc_sel < mc_top) mc_top = mc_sel;
    if (mc_sel >= mc_top + vis) mc_top = mc_sel - vis + 1;
    if (mc_top > rows - vis) mc_top = rows - vis;
    if (mc_top < 0) mc_top = 0;
    int loads = 0;
    for (int i = 0; i < vis && mc_top + i < rows; i++) {
        int r = mc_top + i; f32 ry = 104 + i * rh;
        int on = r == mc_sel;
        gp_zone(52, ry, 536, rh - 3, &mc_sel, r, GPZ_SEL);
        if (on) gx_rrect_fill(52, ry, 536, rh - 3, 6, TL_SEL1, TL_SEL2);
        if (r == 0) { gx_text1(F_MED, 66, ry + 23, "+ Remettre une sauvegarde depuis la SD (.gci)", on ? RGBA(255, 255, 255, 255) : RGBA(255, 210, 120, 255)); continue; }
        mc_ent *m = &mc[r - 1];
        if (!m->loaded && loads < 1 && mc_mounted >= 0) { mc_load_meta(r - 1); loads++; }   /* (une par image) */
        if (m->icon) draw_tex_fit(&m->obj, 32, 32, 32, 32, 60, ry + 2, 31, 31);
        else gx_rrect_fill(62, ry + 4, 28, 28, 5, RGBA(60, 40, 120, 255), RGBA(40, 26, 90, 255));
        text_display(m->title, buf, sizeof(buf)); fit_text_sel(on, F_MED, buf, 380, fv, sizeof(fv));
        gx_text1(F_MED, 100, ry + 16, fv, on ? RGBA(255, 255, 255, 255) : RGBA(230, 222, 255, 255));
        text_display(m->sub, buf, sizeof(buf)); fit_text_sel(on, F_SMALL, buf, 380, fv, sizeof(fv));
        gx_text1(F_SMALL, 100, ry + 32, fv, on ? RGBA(230, 220, 255, 255) : RGBA(150, 135, 210, 255));
        snprintf(buf, sizeof(buf), m->blocks == 1 ? "1 bloc" : "%d blocs", m->blocks);
        gx_text_r(F_SMALL, 578, ry + 16, buf, on ? RGBA(235, 225, 255, 255) : RGBA(150, 135, 210, 255));
        snprintf(buf, sizeof(buf), "%.4s%.2s", (const char *)m->de, (const char *)m->de + 4);
        gx_text_r(F_SMALL, 578, ry + 32, buf, RGBA(130, 115, 185, 255));
    }
    if (mc_pop) {
        const f32 w = 370, h = 46 + MCP_COUNT * 24, x = 230, y = 118;
        gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
        draw_panel(x, y, w, h);
        for (int a = 0; a < MCP_COUNT; a++) {
            f32 ry = y + 14 + a * 24;
            gp_zone(x + 10, ry, w - 20, 22, &mc_psel, a, GPZ_SEL);
            if (a == mc_psel) gx_rrect_fill(x + 10, ry, w - 20, 22, 5, TL_SEL1, TL_SEL2);
            int conf = a == MCP_DEL && mc_pconfirm && a == mc_psel;
            gx_text1(F_MED, x + 22, ry + 17, conf ? "Confirmer : A pour supprimer" : mcp_names[a], conf ? RGBA(255, 160, 160, 255) : RGBA(240, 232, 255, 255));
        }
        gx_text_c(F_SMALL, x + w / 2, y + h - 8, "A : valider   B : fermer", RGBA(255, 210, 120, 255));
    }
    tl_msgbox();
    static const char *const k[] = { "A", "Y", "B", "L", "R" }, *const l[] = { "CHOISIR", "RELIRE", "RETOUR", "", "SLOT" };
    tl_legend(k, l, 5);
}
/* v110 : styles Moderne, Nouvelle, Aurore : les applis en rangee de grandes pochettes qui defile, comme
   les jeux (Aurore : reflet et halo ; Nouvelle : cartes en verre ; Moderne : ombre). Classique : grille. */
static void ap_draw_row(void) {
    char buf[160], fv[160];
    static f32 pos = -10000.0f;
    int st = ui_skin >= 3 ? 3 : ui_skin, loop = ap_n >= 7;   /* (v131 : Vitrine, Ilots = Aurore) */
    if (!ap_n) return;
    if (pos < -9000.0f || !gl_anim) pos = (f32)ap_sel;
    else {
        f32 diff = (f32)ap_sel - pos;
        if (loop) { while (diff > ap_n / 2.0f) diff -= ap_n; while (diff < -ap_n / 2.0f) diff += ap_n; }
        if (fabsf(diff) > 6.0f) diff = diff > 0 ? 6.0f : -6.0f, pos = (f32)ap_sel - diff;
        pos += diff * (1.0f - expf(-ui_dt * 12.0f));
        if (fabsf((f32)ap_sel - pos) < 0.003f) pos = (f32)ap_sel;
        if (loop) { while (pos < 0) pos += ap_n; while (pos >= ap_n) pos -= ap_n; }
    }
    static const f32 scl[5] = { 1.0f, 0.70f, 0.52f, 0.40f, 0.31f }, off[5] = { 0, 150, 246, 308, 350 };
    const f32 CW = 150, CH = 200, top = 116;
    int base = (int)floorf(pos + 0.5f);
    /* pochettes : la choisie d'abord, puis ses voisines, une a la fois, dans le thread de fond */
    for (int k = 0; k < ap_n; k++) {
        int d = abs(k - ap_sel); if (loop && ap_n - d < d) d = ap_n - d;
        if (ap[k].tex && d > 6) { free(ap[k].tex); ap[k].tex = NULL; ap[k].istate = 0; }
    }
    if (gj_state == 0) {
        int idle = now_ms() - last_input_ms > 150;
        for (int dd = 0; dd <= (idle ? 4 : 0); dd++) {
            int done = 0;
            for (int sg = -1; sg <= 1 && !done; sg += 2) {
                int k = ap_sel + sg * dd;
                if (loop) k = (k % ap_n + ap_n) % ap_n; else if (k < 0 || k >= ap_n) continue;
                if (!ap[k].istate && ap_post(k)) done = 1;
                if (!dd) break;
            }
            if (done) break;
        }
    }
    int order[11], no = 0;
    for (int i = -5; i <= 5; i++) order[no++] = i;
    for (int a2 = 0; a2 < no; a2++)
        for (int b2 = a2 + 1; b2 < no; b2++) {
            f32 da = fabsf(base + order[a2] - pos), db = fabsf(base + order[b2] - pos);
            if (db > da) { int t = order[a2]; order[a2] = order[b2]; order[b2] = t; }
        }
    scr_scissor(14, 112, 612, 262);
    if (st == 3) { nv_raw = 1; gx_ellipse(320, top + CH + 6, 150, 24, au_mix(0.4f, 70), au_mix(0.4f, 0)); nv_raw = 0; }
    for (int oi = 0; oi < no; oi++) {
        int i = order[oi];
        f32 p = base + i - pos, ad = fabsf(p);
        if (ad > 4.4f) continue;
        int k = base + i;
        if (loop) k = (k % ap_n + ap_n) % ap_n; else if (k < 0 || k >= ap_n) continue;
        ap_ent *a = &ap[k];
        f32 sc = gl_tab_at(scl, ad), w = CW * sc, h = CH * sc;
        f32 cx = 320 + (p < 0 ? -1 : 1) * gl_tab_at(off, ad), y = st == 3 ? top + (CH - h) : top + (CH - h) / 2;
        f32 near = ad < 1 ? 1 - ad : 0;
        nv_raw = 1;
        if (st == 2) {
            gx_rrect_fill(cx - w / 2 - 7, y - 7, w + 14, h + 14, 14, RGBA(40, 44, 72, 230), RGBA(26, 29, 50, 236));
            gx_rrect_stroke(cx - w / 2 - 7, y - 7, w + 14, h + 14, 14, 1.0f, RGBA(255, 255, 255, 26));
            if (near > 0.05f) gx_glow(cx - w / 2 - 7, y - 7, w + 14, h + 14, 14, nv_acc((u8)(120 * near)), 4);
        } else if (st == 1) gx_rrect_fill(cx - w / 2 + 3, y + 5, w, h, 6, RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 90));
        else if (near > 0.05f) gx_glow(cx - w / 2, y, w, h, 8, au_mix(0.5f, (u8)(150 * near)), 5);
        nv_raw = 0;
        if (a->istate > 0) {
            draw_tex_fit(&a->obj, AP_IW, AP_IH, AP_IW, AP_IH, cx - w / 2, y, w, h);
            if (st == 3) gl_tex_refl(&a->obj, AP_IW, AP_IH, AP_IW, AP_IH, cx - w / 2, y, w, h, h * 0.30f, (u8)(70 + 40 * near));
        } else {                                                /* (en cours de chargement) */
            nv_raw = 1;
            f32 sh = 0.5f + 0.5f * sinf(now_ms() / 180.0f + k);
            gx_rrect_fill(cx - w / 2, y, w, h, 8, RGBA(255, 255, 255, (u8)(14 + 10 * sh)), RGBA(255, 255, 255, 6));
            nv_raw = 0;
        }
        nv_raw = 1;
        f32 dm = ad < 1 ? ad * (st == 3 ? 130.0f : 90.0f) : (st == 3 ? 130.0f : 90.0f) + (ad - 1) * 40.0f;
        u8 dim = (u8)(dm > 210 ? 210 : dm);
        col_t bg = st == 3 ? RGBA(4, 4, 8, dim) : st == 2 ? RGBA(16, 18, 34, dim) : RGBA(40, 40, 44, dim);
        if (dim) gx_rect(cx - w / 2, y, w, h, bg);
        if (st == 1 && near > 0.05f) gx_rrect_fill(cx - 22, y + h + 8, 44, 3, 1.5f, RGBA(mod_acc[0], mod_acc[1], mod_acc[2], (u8)(255 * near)), RGBA(mod_acc[0], mod_acc[1], mod_acc[2], (u8)(255 * near)));
        nv_raw = 0;
        gp_zone(cx - w / 2, y, w, h, &ap_sel, k, GPZ_SEL);
    }
    scr_viewport();
    ap_ent *a = &ap[ap_sel];
    nv_raw = 1;
    text_display(a->name, buf, sizeof(buf)); fit_text(F_LARGE, buf, 520, fv, sizeof(fv));
    f32 ty = st == 3 ? 368 : 360;
    gx_text_c(F_LARGE, 320, ty, fv, RGBA(255, 255, 255, 255));
    const char *sub = a->desc[0] ? a->desc : a->path + strlen(SD_ROOT);
    text_display(sub, buf, sizeof(buf)); fit_text(F_SMALL, buf, 520, fv, sizeof(fv));
    col_t subc = st == 3 ? RGBA(170, 170, 195, 255) : st == 2 ? RGBA(160, 168, 200, 255) : RGBA(170, 170, 178, 255);
    gx_text_c(F_SMALL, 320, ty + 19, fv, subc);
    fmt_size(buf, sizeof(buf), a->size);
    snprintf(fv, sizeof(fv), "%d / %d  \xC2\xB7  %s", ap_sel + 1, ap_n, buf);
    gx_text_r(F_SMALL, 614, 404, fv, subc);
    nv_raw = 0;
}
static void draw_apps(void) {
    char buf[160], fv[160];
    gl_job_done();                                              /* v110 : pochette calculee en fond */
    snprintf(buf, sizeof(buf), ap_n == 1 ? "1 appli" : "%d applis", ap_n);
    if (tab == TAB_GAMES && ui_skin >= 2) {                     /* v110 : dans l'onglet, comme les jeux (pas de fenetre) */
        draw_panel(11, 73, 618, 338);
        gm_hub_bar(26, 85);
        nv_raw = 1; gx_text_r(F_SMALL, 614, 99, buf, RGBA(160, 160, 190, 255)); nv_raw = 0;
    } else if (tab == TAB_GAMES) { tl_frame("", buf); gm_hub_bar(58, 54); }   /* v84 */
    else tl_frame("Mes applis", buf);
    if (ui_skin >= 1 && ap_n) {
        ap_draw_row();
        if (ap_confirm >= 0) tl_confirm("Lancer cette appli ?", ap[ap_confirm].name);
        tl_msgbox();
        static const char *const k2[] = { "A", "Y", "B" }, *const l2[] = { "LANCER", "ACTUALISER", "RETOUR" };
        tl_legend(k2, l2, 3);
        return;
    }
    /* v52 : de grandes pochettes 3:4 comme les jeux (126 x 168), 4 par rangee, le nom dessous */
    const int cols = AP_COLS, rows = 1;
    const f32 cw = 126, ch = 168, sw = 142, rh = 216, y0 = 104;
    const f32 x0 = 320 - (cols * sw - (sw - cw)) / 2;
    int nrow = (ap_n + cols - 1) / cols, top = ap_sel / cols - rows / 2;
    if (top > nrow - rows) top = nrow - rows;
    if (top < 0) top = 0;
    static f32 topf = -2000.0f;
    f32 tf = ease_rows(&topf, (f32)top);
    int r0 = (int)floorf(tf), loads = 0;
    /* (memoire : on ne garde que les pochettes proches de l'ecran) */
    for (int k = 0; k < ap_n; k++) {
        int r = k / cols;
        if (ap[k].tex && (r < top - 4 || r > top + rows + 4)) { free(ap[k].tex); ap[k].tex = NULL; ap[k].istate = 0; }
    }
    static f32 hx = -10000.0f, hy = -10000.0f;
    f32 tx = x0 + (ap_n ? ap_sel % cols : 0) * sw, ty = y0 + ((ap_n ? ap_sel / cols : 0) - tf) * rh;
    if (fabsf(tx - hx) > sw * 2.5f || fabsf(ty - hy) > rh * 1.6f) { hx = tx; hy = ty; }
    gl_ease(&hx, tx, 20.0f); gl_ease(&hy, ty, 20.0f);
    f32 pop = gl_pop(1000 + ap_sel), gr = 1.0f + 0.06f * pop, ex = cw * (gr - 1) / 2, ey = ch * (gr - 1) / 2;
    scr_scissor(0, y0 - 12, 640, rows * rh - 6);
    if (ap_n) gx_glow(hx - ex, hy - ey, cw + 2 * ex, ch + 2 * ey, 8, RGBA(150, 80, 255, 150), 3);
    for (int r = r0; r <= r0 + rows; r++)
        for (int c = 0; c < cols; c++) {
            int k = r * cols + c;
            if (r < 0 || k >= ap_n) break;
            ap_ent *a = &ap[k];
            f32 x = x0 + c * sw, y = y0 + (r - tf) * rh;
            int on = k == ap_sel;
            f32 bx = on ? x - ex : x, by = on ? y - ey : y, bw = on ? cw + 2 * ex : cw, bh = on ? ch + 2 * ey : ch;
            gp_zone(x, y, cw, ch + 18, &ap_sel, k, GPZ_SEL);
            /* la selection d'abord, puis ses voisines : on voit vite celle qu'on regarde */
            int idle = now_ms() - last_input_ms > 150;           /* v68 : en navigation, seulement la selection */
            if (!a->istate && loads < 1 && gj_state == 0 && r >= top && r < top + rows && (on || (idle && (ap_sel >= ap_n || ap[ap_sel].istate)))) { if (ap_post(k)) loads++; }   /* v110 : en fond */
            if (a->istate > 0) draw_tex_fit(&a->obj, AP_IW, AP_IH, AP_IW, AP_IH, bx, by, bw, bh);
            else {                                              /* pas d'icone : une pochette avec les initiales */
                gx_rrect_fill(bx, by, bw, bh, 6, on ? RGBA(70, 36, 160, 255) : RGBA(40, 24, 98, 255), on ? RGBA(30, 14, 80, 255) : RGBA(18, 10, 46, 255));
                char ini[3] = { 0, 0, 0 };
                ini[0] = (char)toupper((u8)a->name[0]);
                if (a->name[0] && a->name[1]) ini[1] = (char)tolower((u8)a->name[1]);
                txt_scale = 2.0f;
                gx_text_c(F_LARGE, bx + bw / 2, by + bh * 0.45f + 16, ini, RGBA(210, 195, 255, 255));
                txt_scale = 1.0f;
                text_display(a->name, buf, sizeof(buf)); fit_text_sel(on, F_SMALL, buf, (int)bw - 12, fv, sizeof(fv));
                gx_text_c(F_SMALL, bx + bw / 2, by + bh - 14, fv, RGBA(190, 170, 250, 255));
            }
            text_display(a->name, buf, sizeof(buf)); fit_text_sel(on, F_SMALL, buf, (int)sw - 6, fv, sizeof(fv));
            gx_text_c(F_SMALL, x + cw / 2, y + ch + 18 + (on ? ey : 0), fv, on ? RGBA(255, 255, 255, 255) : RGBA(200, 188, 240, 255));
        }
    if (ap_n) gx_rrect_stroke(hx - ex - 2, hy - ey - 2, cw + 2 * ex + 4, ch + 2 * ey + 4, 7, 2.0f, RGBA(255, 210, 120, 255));
    scr_viewport();
    if (nrow > rows) {                                          /* barre de defilement */
        f32 th = ch * rows / nrow, ty = y0 + (ch - th) * tf / (f32)(nrow - rows);
        gx_rrect_fill(616, ty, 3, th, 1.5f, RGBA(150, 96, 255, 255), RGBA(110, 60, 230, 255));
    }
    /* l'appli choisie */
    if (ap_n) {
        ap_ent *a = &ap[ap_sel];
        gx_rect(58, 344, 524, 1, RGBA(76, 52, 150, 200));
        text_display(a->name, buf, sizeof(buf)); fit_text(F_MED, buf, 380, fv, sizeof(fv));
        gx_text1(F_MED, 60, 366, fv, RGBA(255, 255, 255, 255));
        fmt_size(buf, sizeof(buf), a->size);
        gx_text_r(F_SMALL, 580, 364, buf, RGBA(190, 160, 255, 255));
        const char *sub = a->desc[0] ? a->desc : a->path + strlen(SD_ROOT);
        text_display(sub, buf, sizeof(buf)); fit_text(F_SMALL, buf, 520, fv, sizeof(fv));
        gx_text1(F_SMALL, 60, 384, fv, RGBA(190, 180, 235, 255));
    } else gx_text1(F_SMALL, 64, 120, "Aucun .dol trouv\xC3\xA9 (racine de la carte SD et dossier apps).", RGBA(170, 155, 225, 255));
    if (ap_confirm >= 0) tl_confirm("Lancer cette appli ?", ap[ap_confirm].name);
    tl_msgbox();
    static const char *const k[] = { "A", "Y", "B" }, *const l[] = { "LANCER", "ACTUALISER", "RETOUR" };
    tl_legend(k, l, 3);
}
static void draw_recent(void) {
    char buf[200], fv[200];
    tl_frame("Derniers jeux lanc\xC3\xA9s", NULL);
    const int vis = 9;
    const f32 rh = 32;
    for (int i = 0; i < vis && i < rc_n; i++) {
        int r = i; f32 ry = 88 + i * rh;
        int on = r == rc_sel;
        gp_zone(52, ry, 536, rh - 3, &rc_sel, r, GPZ_SEL);
        if (on) gx_rrect_fill(52, ry, 536, rh - 3, 6, TL_SEL1, TL_SEL2);
        int k = rc_game(r);
        const char *t = k >= 0 ? gl[k].title : base_of(rc_path[r]);
        text_display(t, buf, sizeof(buf)); fit_text_sel(on, F_MED, buf, 420, fv, sizeof(fv));
        gx_text1(F_MED, 64, ry + 15, fv, on ? RGBA(255, 255, 255, 255) : RGBA(230, 222, 255, 255));
        text_display(rc_path[r] + strlen(SD_ROOT), buf, sizeof(buf)); fit_text_sel(on, F_SMALL, buf, 440, fv, sizeof(fv));
        gx_text1(F_SMALL, 64, ry + 28, fv, on ? RGBA(230, 220, 255, 255) : RGBA(150, 135, 210, 255));
        if (k >= 0) gx_text_r(F_SMALL, 578, ry + 15, gl[k].id, RGBA(150, 135, 210, 255));
    }
    if (!rc_n) gx_text1(F_SMALL, 64, 120, "Aucun jeu lanc\xC3\xA9 pour l'instant.", RGBA(170, 155, 225, 255));
    if (rc_confirm >= 0) { int k = rc_game(rc_confirm); tl_confirm("Lancer ce jeu ?", k >= 0 ? gl[k].title : base_of(rc_path[rc_confirm])); }
    tl_msgbox();
    static const char *const k[] = { "A", "Y", "B" }, *const l[] = { "LANCER", "R\xC3\x89GLAGES DU JEU", "RETOUR" };
    tl_legend(k, l, 3);
}
static void draw_info(void) {
    /* v45 : lignes recalculees toutes les 2 s (avant : a chaque image, avec le calcul de la place
       libre de la carte SD, tres lent : l'ecran ramait) */
    static char l[24][2][120];
    static int n = 0;
    static u64 t = 0;
    if (!t || now_ms() - t > 2000 || tl_info_fresh) { info_lines(l, &n); t = now_ms(); tl_info_fresh = 0; }
    tl_frame("Infos console", NULL);
    char fv[160];
    for (int i = 0; i < n; i++) {
        f32 ry = 100 + i * 21;
        gx_text1(F_SMALL, 64, ry, l[i][0], RGBA(170, 155, 225, 255));
        fit_text(F_SMALL, l[i][1], 330, fv, sizeof(fv));
        gx_text1(F_SMALL, 250, ry, fv, RGBA(240, 232, 255, 255));
    }
    static const char *const k[] = { "B" }, *const lb[] = { "RETOUR" };
    tl_legend(k, lb, 1);
}

/* =================== v86 : serveur du reseau (SMB / FTP / FSP) : START > Reglages > Reseau =================== */
enum { NF_HOST, NF_SHARE, NF_USER, NF_PASS, NF_PORT, NF_TEST, NF_OPEN };
static int nfs_ui_p = 0, nfs_ui_sel = 0;
static char nfs_ui_res[140] = "";
static int nfs_ui_rows(int *r) {                           /* lignes affichees pour ce protocole */
    int n = 0;
    r[n++] = NF_HOST;
    if (nfs_ui_p == NFS_SMB) r[n++] = NF_SHARE;
    if (nfs_ui_p != NFS_FSP) r[n++] = NF_USER;
    r[n++] = NF_PASS; r[n++] = NF_PORT; r[n++] = NF_TEST; r[n++] = NF_OPEN;
    return n;
}
static void nfs_menu_value(int p, char *out, int cap) {
    const nfs_cfg_t *c = &nfs_cfg[p];
    if (!c->host[0]) snprintf(out, cap, "%s", TR("(A : r\xC3\xA9gler)"));
    else if (p == NFS_SMB) snprintf(out, cap, "\\\\%s\\%s", c->host, c->share[0] ? c->share : "?");
    else snprintf(out, cap, "%s", c->host);
}
static void nfs_ui_open(int p) {
    nfs_ui_p = p; nfs_ui_sel = 0; nfs_ui_res[0] = 0;
    tl_screen = TL_NETSRV;
    menu_open = 0;                                         /* (B : retour aux Reglages) */
}
static void nfs_kb_done(const char *t) {
    nfs_cfg_t *c = &nfs_cfg[nfs_ui_p];
    char v[64];
    snprintf(v, sizeof(v), "%s", t);
    switch (kb_field) {
    case NF_HOST: snprintf(c->host, sizeof(c->host), "%s", trim(v)); break;
    case NF_SHARE: snprintf(c->share, sizeof(c->share), "%s", trim(v)); break;
    case NF_USER: snprintf(c->user, sizeof(c->user), "%s", v); break;
    case NF_PASS: snprintf(c->pass, sizeof(c->pass), "%s", v); break;
    case NF_PORT: if (atoi(v) > 0 && atoi(v) < 65536) c->port = atoi(v); break;
    }
    if (nfs_ui_p == NFS_SMB) smb_close_c();                /* (nouveaux reglages : nouvelle connexion) */
    if (nfs_ui_p == NFS_FSP) fsp_close_s();
    nfs_cache_put("", NULL, 0);
    settings_dirty = 1;
    tl_screen = TL_NETSRV;
}
static void nfs_ui_act(int f) {
    nfs_cfg_t *c = &nfs_cfg[nfs_ui_p];
    char cur[64] = "";
    switch (f) {
    case NF_HOST: snprintf(cur, sizeof(cur), "%s", c->host); break;
    case NF_SHARE: snprintf(cur, sizeof(cur), "%s", c->share); break;
    case NF_USER: snprintf(cur, sizeof(cur), "%s", c->user); break;
    case NF_PASS: snprintf(cur, sizeof(cur), "%s", c->pass); break;
    case NF_PORT: snprintf(cur, sizeof(cur), "%d", c->port); break;
    case NF_TEST: {
        if (!nfs_cfg_ok(nfs_ui_p)) { snprintf(nfs_ui_res, sizeof(nfs_ui_res), "%s", TR(nfs_ui_p == NFS_SMB ? "R\xC3\xA8gle d'abord l'adresse et le partage" : "R\xC3\xA8gle d'abord l'adresse")); return; }
        if (net_phase != NET_DONE) { net_start_async(); snprintf(nfs_ui_res, sizeof(nfs_ui_res), "%s", TR("R\xC3\xA9seau en cours de connexion, r\xC3\xA9" "essaie dans un instant")); return; }
        char root[8];
        snprintf(root, sizeof(root), "%s:/", nfs_scheme[nfs_ui_p]);
        if (nfs_ui_p == NFS_SMB) smb_close_c();
        nfs_err[0] = 0;
        tl_status("Connexion au serveur...");
        u64 t0 = now_ms();
        nfs_ent_t *e = NULL; int n = 0;
        int ok = nfs_list(root, &e, &n);
        gl_busy[0] = 0;
        free(e);
        if (ok) snprintf(nfs_ui_res, sizeof(nfs_ui_res), "OK : %d %s (%u ms)", n, n > 1 ? "\xC3\xA9l\xC3\xA9ments" : "\xC3\xA9l\xC3\xA9ment", (unsigned)(now_ms() - t0));
        else snprintf(nfs_ui_res, sizeof(nfs_ui_res), "%s", nfs_err[0] ? nfs_err : TR("Connexion impossible"));
        return; }
    case NF_OPEN:
        if (!nfs_cfg_ok(nfs_ui_p)) { snprintf(nfs_ui_res, sizeof(nfs_ui_res), "%s", TR("R\xC3\xA8gle d'abord l'adresse")); return; }
        if (settings_dirty) save_settings();
        tl_screen = TL_NONE; menu_open = 0;
        snprintf(fe_cwd, sizeof(fe_cwd), "%s:", nfs_scheme[nfs_ui_p]);
        fe_devs = 0; fe_sel = 0;
        tab = TAB_FILES;
        fe_load();
        return;
    }
    kb_start(KB_NFS, cur, f, -1);
}
static void nfs_ui_input(u32 down, u32 nav, int fire) {
    int r[8], n = nfs_ui_rows(r);
    if (nfs_ui_sel >= n) nfs_ui_sel = n - 1;
    if (fire && (nav & PAD_BUTTON_DOWN)) nfs_ui_sel = (nfs_ui_sel + 1) % n;
    if (fire && (nav & PAD_BUTTON_UP)) nfs_ui_sel = (nfs_ui_sel + n - 1) % n;
    if (down & PAD_BUTTON_A) nfs_ui_act(r[nfs_ui_sel]);
    if (down & PAD_BUTTON_B) { tl_screen = TL_NONE; menu_open = 1; if (settings_dirty) save_settings(); }
}
static void nfs_ui_draw(void) {
    static const char *const lab[] = { "Adresse du serveur", "Nom du partage", "Utilisateur", "Mot de passe", "Port", "Tester la connexion", "Ouvrir dans FICHIERS" };
    const nfs_cfg_t *c = &nfs_cfg[nfs_ui_p];
    tl_frame(TR(nfs_label[nfs_ui_p]), nfs_ui_p == NFS_SMB ? "SMB2 (Windows, Samba, NAS)" : nfs_ui_p == NFS_FTP ? "FTP (mode passif)" : "FSP (UDP)");
    int r[8], n = nfs_ui_rows(r);
    char v[96];
    for (int i = 0; i < n; i++) {
        f32 ry = 100 + i * 30;
        int on = i == nfs_ui_sel;
        gp_zone(52, ry, 536, 26, &nfs_ui_sel, i, GPZ_SEL);
        if (on) gx_rrect_fill(52, ry, 536, 26, 6, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        int act = r[i] >= NF_TEST;
        gx_text1(F_MED, 64, ry + 19, TR(lab[r[i]]), act ? RGBA(255, 210, 120, 255) : RGBA(240, 232, 255, 255));
        v[0] = 0;
        switch (r[i]) {
        case NF_HOST: snprintf(v, sizeof(v), "%s", c->host[0] ? c->host : TR("(A : taper, ex. 192.168.1.20)")); break;
        case NF_SHARE: snprintf(v, sizeof(v), "%s", c->share[0] ? c->share : TR("(A : taper)")); break;
        case NF_USER: snprintf(v, sizeof(v), "%s", c->user[0] ? c->user : TR(nfs_ui_p == NFS_FTP ? "(anonyme)" : "(invit\xC3\xA9)")); break;
        case NF_PASS: { int k = (int)strlen(c->pass); if (!k) snprintf(v, sizeof(v), "%s", TR("(aucun)")); else { if (k > 12) k = 12; memset(v, '*', k); v[k] = 0; } break; }
        case NF_PORT: snprintf(v, sizeof(v), "%d", c->port); break;
        }
        if (v[0]) { char fv[96]; fit_text_sel(on, F_MED, v, 250, fv, sizeof(fv)); gx_text_r(F_MED, 576, ry + 19, fv, on ? RGBA(255, 255, 255, 255) : RGBA(190, 175, 240, 255)); }
    }
    if (nfs_ui_res[0]) {
        char fv[160];
        fit_text(F_SMALL, nfs_ui_res, 520, fv, sizeof(fv));
        gx_text1(F_SMALL, 60, 112 + n * 30, fv, !strncmp(nfs_ui_res, "OK", 2) ? RGBA(140, 255, 160, 255) : RGBA(255, 160, 140, 255));
    }
    static const char *const k[] = { "A", "B" }, *const lb[] = { "MODIFIER / LANCER", "RETOUR" };
    tl_legend(k, lb, 2);
}

static void tools_input(u32 down, u32 nav, int fire) {
    switch (tl_screen) {
    case TL_CARDS: mc_input(down, nav, fire); break;
    case TL_APPS: ap_input(down, nav, fire); break;
    case TL_RECENT: rc_input(down, nav, fire); break;
    case TL_INFO: info_input(down, nav, fire); break;
    case TL_VCARD: vc_input(down, nav, fire); break;
    case TL_RHIST: rh_input(down, nav, fire); break;
    case TL_NETSRV: nfs_ui_input(down, nav, fire); break;
    case TL_SAVES: sv_input(down, nav, fire); break;
    }
}
static void draw_tools(void) {
    switch (tl_screen) {
    case TL_CARDS: draw_cards(); break;
    case TL_APPS: draw_apps(); break;
    case TL_RECENT: draw_recent(); break;
    case TL_INFO: draw_info(); break;
    case TL_VCARD: draw_vcard(); break;
    case TL_RHIST: draw_rhist(); break;
    case TL_NETSRV: nfs_ui_draw(); break;
    case TL_SAVES: draw_saves(); break;
    }
}

/* ---------- section "Outils" de la page Reglages ---------- */
enum { TO_RECENT, TO_GAMES, TO_CHEATS, TO_VCARD, TO_SAVES, TO_BACKUP, TO_RESTORE, TO_INFO, TO_QUIT, TO_COUNT };
static const char *const to_names[TO_COUNT] = { "Derniers jeux lanc\xC3\xA9s",
    "Mes jeux, applis et cartes m\xC3\xA9moire", "Cheats (\xC3\xA9" "diteur)", "Cartes m\xC3\xA9moire virtuelles (Pulse)", "Historique des sauvegardes",
    "Sauvegarder tous les r\xC3\xA9glages", "Restaurer les r\xC3\xA9glages", "Infos console", "Quitter GC Station" };
static const char *const to_help[TO_COUNT] = {
    "Relancer un jeu en un clic.",
    "Onglet JEUX / APPS. Dans la fiche d'un jeu : Y = r\xC3\xA9glages du jeu.",
    "Modifier et activer les cheats de tes jeux (dossier gc-mp3/cheats).",
    "Plusieurs cartes, sauvegardes copi\xC3\xA9" "es depuis / vers la SD et la vraie carte.",
    "Copies faites apr\xC3\xA8s chaque partie (et celles des autres consoles du Salon) : revenir en arri\xC3\xA8re, reprendre ailleurs.",
    "R\xC3\xA9glages, radios, favoris, listes, podcasts... dans SD:/gc-mp3-sauvegarde.txt",
    "Remet les r\xC3\xA9glages de SD:/gc-mp3-sauvegarde.txt (A deux fois pour confirmer).",
    "Version, vid\xC3\xA9o, m\xC3\xA9moire, carte SD, r\xC3\xA9seau.",
    "Ouvre le menu de la GameCube (lecteur de disque). Bouton Reset = relancer GC Station" };
static int to_sel = 0;
static void tools_menu_input(u32 down, u32 nav, int fire) {
    if (fire && (nav & PAD_BUTTON_DOWN)) to_sel = (to_sel + 1) % TO_COUNT;
    if (fire && (nav & PAD_BUTTON_UP)) to_sel = (to_sel + TO_COUNT - 1) % TO_COUNT;
    static int to_confirm = 0;
    if (nav) to_confirm = 0;
    if (!(down & PAD_BUTTON_A)) return;
    if (to_sel == TO_BACKUP || to_sel == TO_RESTORE) {           /* v73 : sans quitter les reglages */
        char m[200];
        if (to_sel == TO_RESTORE && !to_confirm) { to_confirm = 1; lib_note("A encore une fois : les r\xC3\xA9glages actuels seront remplac\xC3\xA9s"); return; }
        to_confirm = 0;
        ui_status(to_sel == TO_BACKUP ? "Sauvegarde des r\xC3\xA9glages..." : "Restauration des r\xC3\xA9glages...");
        if (to_sel == TO_BACKUP) bk_save(m, sizeof(m)); else bk_restore(m, sizeof(m));
        lib_note(m);
        return;
    }
    menu_open = 0;
    if (settings_dirty) save_settings();
    crumb("outils : %d", to_sel);
    switch (to_sel) {
    case TO_RECENT: rc_open(); break;
    case TO_INFO: info_open(); break;
    case TO_QUIT: quit_requested = 1; break;
    case TO_GAMES: tab = TAB_GAMES; tab_enter(TAB_GAMES); gm_sel = GM_COUNT; break;
    case TO_CHEATS: cheats_open(); break;
    case TO_VCARD: vc_open(); break;
    case TO_SAVES: sv_open(); break;
    }
}
static void tools_menu_draw(f32 x, f32 y, f32 w, int vis, f32 rh, f32 help_y) {
    (void)vis;
    for (int i = 0; i < TO_COUNT; i++) {
        f32 ry = y + i * (rh + 4);
        int on = i == to_sel;
        gp_zone(x, ry, w, rh, &to_sel, i, GPZ_SEL);
        if (on) gx_rrect_fill(x, ry, w, rh, 6, TL_SEL1, TL_SEL2);
        gx_text1(F_MED, x + 14, ry + 17, to_names[i], on ? RGBA(255, 255, 255, 255) : i == TO_QUIT ? RGBA(255, 170, 170, 255) : RGBA(230, 222, 255, 255));
        gx_text_r(F_MED, x + w - 14, ry + 17, "(A)", on ? RGBA(255, 255, 255, 255) : RGBA(190, 160, 255, 255));
    }
    char fv[200];
    fit_text(F_SMALL, to_help[to_sel], (int)w - 16, fv, sizeof(fv));
    gx_text1(F_SMALL, x + 8, help_y + 8, fv, RGBA(210, 200, 240, 255));
}

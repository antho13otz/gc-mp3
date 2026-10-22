// GC-MP3 v35 : outils (inclus dans main.c, apres gclib.c, avant indigo_cfg.c)
// Section "Outils" de la page Reglages (START) :
//   - cartes memoire : sauvegardes des slots A / B, copie sur la SD (.gci), restauration,
//     copie vers l'autre carte, suppression
//   - applis : les .dol de la carte SD (racine, apps/), lances directement
//   - derniers jeux lances (GC-MP3 + liste "recent" d'Indigo / Swiss)
//   - infos sur la console
// Chaque outil s'ouvre en fenetre par-dessus l'ecran (tl_screen).

static void indigo_game_open(const char *id, const char *title);
enum { TL_NONE, TL_CARDS, TL_APPS, TL_RECENT, TL_INFO };
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
static void mc_to_sd(int i) {
    tl_status("Copie sur la carte SD...");
    long n = 0;
    u8 *g = mc_export(i, &n);
    gl_busy[0] = 0;
    if (!g) { tl_note("Lecture de la sauvegarde impossible"); return; }
    mkdir(MC_DIR, 0777);
    char base[80], p[NAME_LEN];
    mc_safe_name(mc[i].de, base, sizeof(base));
    snprintf(p, sizeof(p), "%s/%s.gci", MC_DIR, base);
    struct stat st;
    for (int k = 2; stat(p, &st) == 0 && k < 100; k++) snprintf(p, sizeof(p), "%s/%s_%d.gci", MC_DIR, base, k);
    FILE *f = fopen(p, "wb");
    int ok = f && fwrite(g, 1, n, f) == (size_t)n;
    if (f && fclose(f) != 0) ok = 0;
    free(g);
    if (!ok) { remove(p); tl_note("\xC3\x89" "criture sur la carte SD impossible"); return; }
    char m[200];
    snprintf(m, sizeof(m), "Copi\xC3\xA9" "e : SD:/gci/%s", base_of(p));
    snprintf(tl_msg, sizeof(tl_msg), "%s", m); tl_msg_t = now_ms();
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
enum { MCP_SD, MCP_OTHER, MCP_DEL, MCP_COUNT };
static const char *const mcp_names[MCP_COUNT] = { "Copier sur la carte SD (.gci)", "Copier vers l'autre carte", "Supprimer" };
static void mc_input(u32 down, u32 nav, int fire) {
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
    if (down & (PAD_TRIGGER_L | PAD_TRIGGER_R)) { mc_slot = 1 - mc_slot; mc_sel = 0; mc_top = 0; mc_mount(mc_slot); return; }
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
/* v41 : icon.png a cote du .dol (format Homebrew Channel, 128 x 48) -> texture RGB5A3 de 128 x 48 */
#define AP_IW 128
#define AP_IH 48
static void ap_icon_load(ap_ent *a) {
    a->istate = -1;
    if (!a->icon[0]) return;
    long len = 0;
    unsigned char *d = read_file(a->icon, 2 * 1024 * 1024, &len);
    if (!d) return;
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load_from_memory(d, (int)len, &w, &h, &n, 4);
    free(d);
    if (!px || w <= 0 || h <= 0) { if (px) stbi_image_free(px); return; }
    u16 *t = (u16 *)memalign(32, AP_IW * AP_IH * 2);
    if (!t) { stbi_image_free(px); return; }
    /* garde les proportions : l'image tient dans 128 x 48, centree, le reste transparent */
    f32 sc = (f32)AP_IW / w < (f32)AP_IH / h ? (f32)AP_IW / w : (f32)AP_IH / h;
    int dw = (int)(w * sc + 0.5f), dh = (int)(h * sc + 0.5f);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ox = (AP_IW - dw) / 2, oy = (AP_IH - dh) / 2;
    for (int y = 0; y < AP_IH; y++)
        for (int x = 0; x < AP_IW; x++) {
            u16 v = 0;                                          /* (transparent) */
            int dx = x - ox, dy = y - oy;
            if (dx >= 0 && dy >= 0 && dx < dw && dy < dh) {
                int sx0 = dx * w / dw, sx1 = (dx + 1) * w / dw, sy0 = dy * h / dh, sy1 = (dy + 1) * h / dh;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sy1 <= sy0) sy1 = sy0 + 1;
                u32 r = 0, g = 0, b = 0, al = 0, c = 0;
                for (int yy = sy0; yy < sy1 && yy < h; yy++)
                    for (int xx = sx0; xx < sx1 && xx < w; xx++) {
                        const unsigned char *p = px + ((size_t)yy * w + xx) * 4;
                        r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; al += p[3]; c++;
                    }
                if (al) { r /= al; g /= al; b /= al; }
                al = c ? al / c : 0;
                if (al >= 224) v = (u16)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
                else v = (u16)(((al >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
            }
            t[((y >> 2) * (AP_IW >> 2) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3)] = v;
        }
    stbi_image_free(px);
    DCFlushRange(t, AP_IW * AP_IH * 2);
    a->tex = t;
    GX_InitTexObj(&a->obj, t, AP_IW, AP_IH, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&a->obj, GX_LINEAR, GX_LINEAR);
    a->istate = 1;
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
static void ap_open(void) {
    if (!ap) ap = (ap_ent *)malloc(sizeof(ap_ent) * AP_MAX);
    for (int i = 0; ap && i < ap_n; i++) if (ap[i].tex) { free(ap[i].tex); ap[i].tex = NULL; }
    ap_n = 0; ap_confirm = -1;
    tl_screen = TL_APPS;
    if (!ap || !sd_ok) return;
    ap_scan_dir(SD_ROOT "/apps", 1);
    ap_scan_dir(SD_ROOT, 0);
    qsort(ap, ap_n, sizeof(ap_ent), ap_cmp);
    if (ap_sel >= ap_n) ap_sel = ap_n ? ap_n - 1 : 0;
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
    if (down & PAD_BUTTON_Y) { ap_open(); tl_note("Liste actualis\xC3\xA9" "e"); }
    if (down & PAD_BUTTON_B) tl_screen = TL_NONE;
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
    /* liste d'Indigo / Swiss : Recent_0=sdc:/games/xxx.iso */
    d = (char *)read_file(SD_ROOT "/swiss/settings/recent.ini", 32768, &len);
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
            if (k >= 0) e = gl_launch(k);
            else {
                size_t l = strlen(rc_path[i]);
                if (l > 4 && !strcasecmp(rc_path[i] + l - 4, ".dol")) e = gl_boot_dol(rc_path[i], NULL, NULL, base_of(rc_path[i]));
                else { tl_recent_add(rc_path[i]); e = gl_boot_swiss(rc_path[i]); }
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
    IL("Vid\xC3\xA9o", "%s%s", vid_active == VID_480P ? "480p" : "480i", VIDEO_HaveComponentCable() ? TR(" (c\xC3\xA2" "ble composante)") : "");
    static const char *const ln[] = { "English", "Deutsch", "Fran\xC3\xA7" "ais", "Espa\xC3\xB1ol", "Italiano", "Nederlands" };
    int lg = SYS_GetLanguage();
    IL("Langue de la console", "%s", lg >= 0 && lg < 6 ? ln[lg] : "?");
    IL("Son de la console", "%s", SYS_GetSoundMode() == SYS_SOUND_MONO ? TR("Mono") : TR("St\xC3\xA9r\xC3\xA9o"));
    IL("Position de l'\xC3\xA9" "cran", "%+d", (int)SYS_GetDisplayOffsetH());
    struct mallinfo mi = mallinfo();
    IL("M\xC3\xA9moire", ("%d Ko utilis\xC3\xA9s, %d Ko libres"), (int)(mi.uordblks / 1024), (int)(((u32)SYS_GetArena1Hi() - (u32)SYS_GetArena1Lo() + mi.fordblks) / 1024));
    static const char *const sl[3] = { "SD2SP2 (port s\xC3\xA9rie 2)", "SD Gecko slot A", "SD Gecko slot B" };
    struct statvfs sv;
    if (sd_ok && !tl_sd_known && statvfs(SD_ROOT "/", &sv) == 0) {        /* (une fois par ouverture : lent) */
        tl_sd_tot = (unsigned long long)sv.f_blocks * sv.f_frsize; tl_sd_free = (unsigned long long)sv.f_bavail * sv.f_frsize;
        tl_sd_known = 1;
    }
    if (sd_ok && tl_sd_known) {
        unsigned long long tot = tl_sd_tot, fr = tl_sd_free;
        IL("Carte SD", ("%s : %llu Mo libres sur %llu Mo"), TR(sl[sd_slot >= 0 && sd_slot < 3 ? sd_slot : 0]), fr >> 20, tot >> 20);
    } else IL("Carte SD", "%s", TR("absente"));
    IL("R\xC3\xA9seau", "%s", net_phase != NET_DONE ? TR("pas encore connect\xC3\xA9") : net_ok ? ip_str : TR("pas de r\xC3\xA9seau"));
    if (net_ok && net_static) IL("", "%s", TR("adresse fixe (r\xC3\xA9glages d'Indigo)"));
    IL("Indigo / Swiss", "%s", gl_find_swiss() ? base_of(gl_swiss) : TR("introuvable"));
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
    gx_rrect_fill(lx, 419, lw, 39, 9, RGBA(10, 6, 30, 255), RGBA(10, 6, 30, 255));
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

static void draw_cards(void) {
    char buf[160], fv[160];
    tl_frame("Cartes m\xC3\xA9moire", NULL);
    /* slots A / B */
    for (int s = 0; s < 2; s++) {
        f32 px = 400 + s * 92;
        if (s == mc_slot) gx_rrect_fill(px, 52, 86, 22, 6, TL_SEL1, TL_SEL2);
        gx_text_c(F_SMALL, px + 43, 68, s ? "Slot B" : "Slot A", s == mc_slot ? RGBA(255, 255, 255, 255) : RGBA(170, 155, 225, 255));
    }
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
        text_display(m->title, buf, sizeof(buf)); fit_text(F_MED, buf, 380, fv, sizeof(fv));
        gx_text1(F_MED, 100, ry + 16, fv, on ? RGBA(255, 255, 255, 255) : RGBA(230, 222, 255, 255));
        text_display(m->sub, buf, sizeof(buf)); fit_text(F_SMALL, buf, 380, fv, sizeof(fv));
        gx_text1(F_SMALL, 100, ry + 32, fv, on ? RGBA(230, 220, 255, 255) : RGBA(150, 135, 210, 255));
        snprintf(buf, sizeof(buf), m->blocks == 1 ? "1 bloc" : "%d blocs", m->blocks);
        gx_text_r(F_SMALL, 578, ry + 16, buf, on ? RGBA(235, 225, 255, 255) : RGBA(150, 135, 210, 255));
        snprintf(buf, sizeof(buf), "%.4s%.2s", (const char *)m->de, (const char *)m->de + 4);
        gx_text_r(F_SMALL, 578, ry + 32, buf, RGBA(130, 115, 185, 255));
    }
    if (mc_pop) {
        const f32 w = 300, h = 46 + MCP_COUNT * 24, x = 290, y = 170;
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
static void draw_apps(void) {
    char buf[160], fv[160];
    snprintf(buf, sizeof(buf), ap_n == 1 ? "1 appli" : "%d applis", ap_n);
    tl_frame("Mes applis", buf);
    /* v42 : grille d'icones (comme Mes jeux), defilement doux et en boucle */
    const int cols = AP_COLS, rows = 3;
    const f32 cw = 128, ch = 48, sw = 134, rh = 82, y0 = 92;
    const f32 x0 = 320 - (cols * sw - (sw - cw)) / 2;
    int nrow = (ap_n + cols - 1) / cols, top = ap_sel / cols - rows / 2;
    if (top > nrow - rows) top = nrow - rows;
    if (top < 0) top = 0;
    static f32 topf = -2000.0f;
    f32 tf = ease_rows(&topf, (f32)top);
    int r0 = (int)floorf(tf), loads = 0;
    /* v45 : le cadre de selection glisse d'une appli a l'autre */
    static f32 hx = -10000.0f, hy = -10000.0f;
    f32 tx = x0 + (ap_n ? ap_sel % cols : 0) * sw, ty = y0 + ((ap_n ? ap_sel / cols : 0) - tf) * rh;
    if (fabsf(tx - hx) > sw * 2.5f || fabsf(ty - hy) > rh * 1.6f) { hx = tx; hy = ty; }
    gl_ease(&hx, tx, 20.0f); gl_ease(&hy, ty, 20.0f);
    scr_scissor(0, y0 - 6, 640, rows * rh + 2);
    if (ap_n) {
        gx_glow(hx - 3, hy - 3, cw + 6, rh - 6, 8, RGBA(150, 80, 255, 150), 3);
        gx_rrect_fill(hx - 3, hy - 3, cw + 6, rh - 6, 7, TL_SEL1, TL_SEL2);
    }
    for (int r = r0; r <= r0 + rows; r++)
        for (int c = 0; c < cols; c++) {
            int k = r * cols + c;
            if (r < 0 || k >= ap_n) break;
            ap_ent *a = &ap[k];
            f32 x = x0 + c * sw, y = y0 + (r - tf) * rh;
            int on = k == ap_sel;
            gp_zone(x - 3, y - 3, cw + 6, rh - 4, &ap_sel, k, GPZ_SEL);
            if (!on) gx_rrect_fill(x - 3, y - 3, cw + 6, rh - 6, 7, RGBA(30, 18, 72, 150), RGBA(18, 10, 46, 150));
            if (!a->istate && loads < 1 && r >= top && r < top + rows) { ap_icon_load(a); loads++; }   /* (une icone par image) */
            if (a->istate > 0) draw_tex_fit(&a->obj, AP_IW, AP_IH, AP_IW, AP_IH, x, y, cw, ch);
            else {
                gx_rrect_fill(x + 4, y + 4, cw - 8, ch - 8, 6, RGBA(70, 46, 140, 255), RGBA(42, 26, 96, 255));
                char ini[3] = { 0, 0, 0 };
                ini[0] = (char)toupper((u8)a->name[0]);
                if (a->name[0] && a->name[1]) ini[1] = (char)tolower((u8)a->name[1]);
                gx_text_c(F_LARGE, x + cw / 2, y + 34, ini, RGBA(210, 195, 255, 255));
            }
            text_display(a->name, buf, sizeof(buf)); fit_text(F_SMALL, buf, (int)cw, fv, sizeof(fv));
            gx_text_c(F_SMALL, x + cw / 2, y + ch + 18, fv, on ? RGBA(255, 255, 255, 255) : RGBA(200, 188, 240, 255));
        }
    if (ap_n) gx_rrect_stroke(hx - 3, hy - 3, cw + 6, rh - 6, 7, 2.0f, RGBA(255, 210, 120, 255));
    scr_viewport();
    if (nrow > rows) {                                          /* barre de defilement */
        f32 th = 240.0f * rows / nrow, ty = y0 + (240.0f - th) * tf / (f32)(nrow - rows);
        gx_rrect_fill(590, ty, 3, th, 1.5f, RGBA(150, 96, 255, 255), RGBA(110, 60, 230, 255));
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
        text_display(t, buf, sizeof(buf)); fit_text(F_MED, buf, 420, fv, sizeof(fv));
        gx_text1(F_MED, 64, ry + 15, fv, on ? RGBA(255, 255, 255, 255) : RGBA(230, 222, 255, 255));
        text_display(rc_path[r] + strlen(SD_ROOT), buf, sizeof(buf)); fit_text(F_SMALL, buf, 440, fv, sizeof(fv));
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

static void tools_input(u32 down, u32 nav, int fire) {
    switch (tl_screen) {
    case TL_CARDS: mc_input(down, nav, fire); break;
    case TL_APPS: ap_input(down, nav, fire); break;
    case TL_RECENT: rc_input(down, nav, fire); break;
    case TL_INFO: info_input(down, nav, fire); break;
    }
}
static void draw_tools(void) {
    switch (tl_screen) {
    case TL_CARDS: draw_cards(); break;
    case TL_APPS: draw_apps(); break;
    case TL_RECENT: draw_recent(); break;
    case TL_INFO: draw_info(); break;
    }
}

/* ---------- section "Outils" de la page Reglages ---------- */
enum { TO_RECENT, TO_GAMES, TO_CHEATS, TO_INFO, TO_SWISS, TO_COUNT };   /* (v43 : cartes memoire -> onglet JEUX / APPS) */
static const char *const to_names[TO_COUNT] = { "Derniers jeux lanc\xC3\xA9s",
    "Mes jeux, applis et cartes m\xC3\xA9moire", "Cheats Swiss (\xC3\xA9" "diteur)", "Infos console", "Quitter vers Indigo / Swiss" };
static const char *const to_help[TO_COUNT] = {
    "Relancer un jeu en un clic (jeux lanc\xC3\xA9s depuis GC Station et depuis Indigo).",
    "Onglet JEUX / APPS. Dans la fiche d'un jeu : Y = r\xC3\xA9glages du jeu.",
    "Modifier et activer les cheats de tes jeux (dossier swiss/cheats).",
    "Version, vid\xC3\xA9o, m\xC3\xA9moire, carte SD, r\xC3\xA9seau.",
    "Ouvre le menu d'Indigo / Swiss." };
static int to_sel = 0;
static void tools_menu_input(u32 down, u32 nav, int fire) {
    if (fire && (nav & PAD_BUTTON_DOWN)) to_sel = (to_sel + 1) % TO_COUNT;
    if (fire && (nav & PAD_BUTTON_UP)) to_sel = (to_sel + TO_COUNT - 1) % TO_COUNT;
    if (!(down & PAD_BUTTON_A)) return;
    menu_open = 0;
    if (settings_dirty) save_settings();
    crumb("outils : %d", to_sel);
    switch (to_sel) {
    case TO_RECENT: rc_open(); break;
    case TO_INFO: info_open(); break;
    case TO_GAMES: tab = TAB_GAMES; tab_enter(TAB_GAMES); gm_sel = GM_COUNT; break;
    case TO_CHEATS: cheats_open(); break;
    case TO_SWISS: {
        ui_status("Lancement de Swiss...");
        const char *e = gl_boot_swiss(NULL);                   /* (ne revient pas si ca marche) */
        if (e) lib_note(e);
        break; }
    }
}
static void tools_menu_draw(f32 x, f32 y, f32 w, int vis, f32 rh, f32 help_y) {
    (void)vis;
    gx_text1(F_SMALL, x + 8, y - 16, ui_en ? "Tools from Indigo / Swiss, inside GC Station" : "Les outils d'Indigo / Swiss, dans GC Station", RGBA(150, 135, 210, 255));
    for (int i = 0; i < TO_COUNT; i++) {
        f32 ry = y + i * (rh + 4);
        int on = i == to_sel;
        gp_zone(x, ry, w, rh, &to_sel, i, GPZ_SEL);
        if (on) gx_rrect_fill(x, ry, w, rh, 6, TL_SEL1, TL_SEL2);
        gx_text1(F_MED, x + 14, ry + 17, to_names[i], on ? RGBA(255, 255, 255, 255) : i == TO_SWISS ? RGBA(255, 190, 190, 255) : RGBA(230, 222, 255, 255));
        gx_text_r(F_MED, x + w - 14, ry + 17, "(A)", on ? RGBA(255, 255, 255, 255) : RGBA(190, 160, 255, 255));
    }
    char fv[200];
    fit_text(F_SMALL, to_help[to_sel], (int)w - 16, fv, sizeof(fv));
    gx_text1(F_SMALL, x + 8, help_y + 8, fv, RGBA(210, 200, 240, 255));
}

// GC Station v121 : GC Loader (remplace le lecteur DVD : les jeux sont sur sa propre carte SD).
// Inclus dans main.c apres natif.c. Code de GC Station ; protocole public du GC Loader (commandes
// du lecteur B0 = identifiant, B3 = choix du fichier), comme le decrivent ses outils libres.
//  - sa carte SD se lit par le lecteur (commande B2, lecture seule) : montee en "gcl:", ses jeux sont dans la liste ;
//  - pour lancer un jeu : la position du fichier sur sa carte (morceaux : position dans le fichier, taille,
//    secteur), pour le disque 1 et, s'il existe, le disque 2 (le GC Loader change lui-meme de disque) ;
//  - puis le jeu demarre comme un vrai disque (natif.c, nat_boot_drive), sans Pulse.

#define GCL_ROOT "gcl:"
#define GCL_MAXFRAGS 40                              /* (par disque : limite du GC Loader) */
static int gcl_state = 0;                            /* 0 = pas cherche, 1 = GC Loader monte (gcl:), -1 = absent */
static char gcl_msg[120] = "";

#ifdef GEKKO
/* carte SD du GC Loader, lue par le lecteur : commande B2 (secteur, taille), DMA ; lecture seule */
static u8 gcl_bounce[4096] ATTRIBUTE_ALIGN(32);
static int gcl_dma(u32 sec, u32 len, void *buf) {
    volatile u32 *di = (volatile u32 *)0xCC006000u;
    di[0] = 0x54u;
    DCInvalidateRange(buf, len);
    di[2] = 0xB2000000u; di[3] = sec; di[4] = len;
    di[5] = (u32)MEM_VIRTUAL_TO_PHYSICAL(buf); di[6] = len;
    di[7] = 3;
    u64 t0 = now_ms();
    while (di[7] & 1) if (now_ms() - t0 > 5000) { di[0] = 1u; u64 t1 = now_ms(); while ((di[7] & 1) && now_ms() - t1 < 200) ; di[0] = 0x54u; return -1; }
    u32 sr = di[0];
    di[0] = 0x54u;
    DCInvalidateRange(buf, len);
    return sr & 0x04u ? -1 : 0;
}
static bool gcl_io_read(sec_t sector, sec_t num, void *buf) {
    u8 *o = (u8 *)buf;
    while (num) {
        if (((u32)o & 31) == 0) {                        /* (adresse alignee : directement, 1 Mo au plus par commande) */
            u32 n = num > 2048 ? 2048 : (u32)num;
            if (gcl_dma((u32)sector, n * 512, o)) return false;
            o += n * 512; sector += n; num -= n;
        } else {
            u32 n = num > 8 ? 8 : (u32)num;
            if (gcl_dma((u32)sector, n * 512, gcl_bounce)) return false;
            memcpy(o, gcl_bounce, n * 512);
            o += n * 512; sector += n; num -= n;
        }
    }
    return true;
}
static bool gcl_io_ok(void) { return true; }
static bool gcl_io_write(sec_t sector, sec_t num, const void *buf) { (void)sector; (void)num; (void)buf; return false; }
static DISC_INTERFACE gcl_io = { 0x47434C44u, FEATURE_MEDIUM_CANREAD, gcl_io_ok, gcl_io_ok, gcl_io_read, gcl_io_write, gcl_io_ok, gcl_io_ok };
static int gcl_rawsec(u32 sec, void *buf) { return gcl_io_read(sec, 1, buf) ? 0 : -1; }
/* une commande "immediate" du GC Loader : rend la reponse (DIIMMBUF), ou 0xFFFFFFFF si rien */
static u32 gcl_imm(u32 c0, u32 c1, u32 c2) {
    volatile u32 *di = (volatile u32 *)0xCC006000u;
    di[0] = 0x54u;
    di[2] = c0; di[3] = c1; di[4] = c2; di[6] = 0; di[8] = 0;
    di[7] = 1;
    u64 t0 = now_ms();
    while (di[7] & 1) if (now_ms() - t0 > 1000) return 0xFFFFFFFFu;
    u32 r = di[8];
    di[0] = 0x54u;
    return r;
}
#endif

/* cherche le GC Loader (une fois, ou quand on recherche les jeux) ; 1 = monte en gcl: */
static int gcl_probe(void) {
    if (gcl_state == 1) return 1;
#ifdef GEKKO
    volatile u32 *di = (volatile u32 *)0xCC006000u, *pi = (volatile u32 *)0xCC003000u;
    static u8 inq[32] ATTRIBUTE_ALIGN(32);
    if (!(pi[9] & 4u)) {                              /* (lecteur tenu en reset par GC Station : on le libere) */
        u32 level = IRQ_Disable();
        pi[9] = (pi[9] & ~4u) | 1u;
        u64 t = now_ms(); while (now_ms() - t < 2) ;
        pi[9] = pi[9] | 5u;
        IRQ_Restore(level);
    }
    int r = -1;
    u64 t0 = now_ms();
    do { r = na_inq(di, inq, 600); service_audio(); } while (r < 0 && now_ms() - t0 < 3000);
    di[1] = 4u;
    u32 date = (u32)inq[4] << 24 | (u32)inq[5] << 16 | (u32)inq[6] << 8 | inq[7];
    if (r < 0 || date != 0x20196C64u) {
        gcl_state = -1;
        snprintf(gcl_msg, sizeof(gcl_msg), r < 0 ? "pas de lecteur" : "lecteur Nintendo (pas un GC Loader)");
        return 0;
    }
    if (!fatMountSimple("gcl", &gcl_io)) {
        gcl_state = -1;
        snprintf(gcl_msg, sizeof(gcl_msg), "GC Loader trouv\xC3\xA9, mais sa carte SD ne se monte pas");
        return 0;
    }
    u32 id = gcl_imm(0xB0000000u, 0, 0);
    gcl_state = 1;
    snprintf(gcl_msg, sizeof(gcl_msg), "GC Loader pr\xC3\xAAt%s", id == 0xAAAAAAAAu ? "" : " (compatible)");
    crumb("GC Loader : %s", gcl_msg);
    return 1;
#else
    gcl_state = -1;
    snprintf(gcl_msg, sizeof(gcl_msg), "(simulateur : pas de GC Loader)");
    return 0;
#endif
}
static int gcl_path(const char *p) { return !strncmp(p, GCL_ROOT, 4); }

/* morceaux du fichier sur la carte du GC Loader, envoyes pour le disque n (0 / 1) ; NULL = OK */
static const char *gcl_send(const char *path, u32 disc) {
#ifdef GEKKO
    static natres_ext_t fr[GCL_MAXFRAGS];
    u32 nf = 0, size = 0;
    if (path) {
        char e[120];
        struct stat st;
        if (stat(path, &st) != 0) return "Jeu introuvable sur le GC Loader";
        size = (u32)st.st_size;
        na_dev_rd = gcl_rawsec;
        int r = nat_map(path, e, sizeof(e));
        na_dev_rd = NULL;
        if (r) { static char m[140]; snprintf(m, sizeof(m), "GC Loader : %s", e); return m; }
        if (na_next > GCL_MAXFRAGS) return "Jeu trop fragment\xC3\xA9 sur la carte du GC Loader (d\xC3\xA9" "fragmente-la)";
        memcpy(fr, na_ext, na_next * sizeof(natres_ext_t)); nf = na_next;
    }
    if (gcl_imm(0xB3000001u, disc, nf) != 0) return "Le GC Loader refuse le jeu (version trop ancienne ?)";
    u32 r = 0;
    for (u32 i = 0; i < nf; i++) {
        u32 off = fr[i].iso * 512u, len = fr[i].cnt * 512u;
        if (off + len > size) len = size - off;
        r = gcl_imm(off, len, fr[i].sd);
        if (r == 0) break;
    }
    if (nf && r != 0) return "Le GC Loader n'a pas pris la position du jeu";
    return NULL;
#else
    (void)path; (void)disc;
    return "(simulateur : pas de GC Loader)";
#endif
}
static const char *gcl_boot(int k) {
    if (k < 0 || k >= gl_n) return "Jeu introuvable";
    if (gl[k].ciso) return "GC Loader : les .ciso ne sont pas lus par le GC Loader (garde-le en .iso)";
    snprintf(nat_ld_title, sizeof(nat_ld_title), "%s", gl[k].title);
    nat_ld_k = k;
    nat_ld("GC Loader", 2);
    if (!gcl_probe()) { static char m[140]; snprintf(m, sizeof(m), "GC Loader : %s", gcl_msg); return m; }
#ifndef NATIF_BENCH
    gl_stats_bump(k);
    tl_recent_add(gl[k].path);
#endif
    int k2 = na_disc2(k);
    if (k2 >= 0 && !gcl_path(gl[k2].path)) k2 = -1;  /* (l'autre disque doit etre sur la meme carte) */
    nat_ld("Position du jeu sur le GC Loader", 6);
    const char *e = gcl_send(k2 >= 0 ? gl[k2].path : NULL, 1);
    if (!e) e = gcl_send(gl[k].path, 0);
#ifdef GEKKO
    if (!e && gcl_imm(0xB3000002u, 0, 0) != 0) e = "Le GC Loader ne choisit pas le disque 1";
#endif
    if (e) return e;
    return nat_boot_drive(k, 0);                      /* (le jeu se lance comme un vrai disque) */
}

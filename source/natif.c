// GC Station v50 : lanceur natif, etape 1 = analyseur (inclus dans main.c apres gclib.c)
// Code original de GC Station (aucun code de Swiss). Base : la documentation publique
// du materiel GameCube (YAGCD, Dolphin).
//
// Ce que fait un lanceur de jeux, et ce que cette etape prepare :
//  1. lire l'en-tete du disque (boot.bin 0x000, bi2.bin 0x440, apploader 0x2440) ;
//  2. charger main.dol et la table des fichiers (FST) en memoire, comme l'apploader ;
//  3. remplir la memoire basse (0x80000000...) comme le BIOS ;
//  4. rediriger les lectures du lecteur DVD (registres 0xCC006000) vers la carte SD.
// Ici on ne lance rien : on verifie 1 et 2, on prevoit 3, et on repere dans main.dol
// le code qui touche aux registres du lecteur (4). Rapport : SD:/gc-mp3/natif/<ID>.txt
//
// Fiche d'un jeu (onglet JEUX) : R = analyse native.

#define NA_DIR   GCD "/natif"
#define NA_DI    0xCC006000u                 /* registres du lecteur DVD (DI) */
#define NA_MAXHIT 48
static void nat_qr_open(const char *id);      /* v51 : QR code vers le rapport (plus bas) */

typedef struct {
    char id[7], title[72];
    u8 disc, ver, region, stream;
    u32 dol_off, fst_off, fst_sz, fst_max;
    char al_date[17];
    u32 al_entry, al_size, al_trailer;
    /* main.dol */
    u32 dol_len, entry, lo, hi, bss, bsz;
    int nsec;
    u32 sec_addr[18], sec_sz[18];
    /* FST */
    u32 nfiles, ndirs, fst_dest;
    /* acces aux registres du lecteur */
    int nhit;
    u32 hit_addr[NA_MAXHIT];
    u16 hit_reg[NA_MAXHIT];
    u8 hit_store[NA_MAXHIT];
    int nfunc;
    u32 func_addr[NA_MAXHIT];
    int problems;
    char err[160];
} nat_info_t;

static const char *na_reg_name(u32 r) {
    switch (r) {
    case 0x00: return "DISR (etat)";
    case 0x04: return "DICVR (couvercle)";
    case 0x08: return "DICMDBUF0 (commande)";
    case 0x0C: return "DICMDBUF1 (position)";
    case 0x10: return "DICMDBUF2 (longueur)";
    case 0x14: return "DIMAR (adresse memoire)";
    case 0x18: return "DILENGTH (taille DMA)";
    case 0x1C: return "DICR (demarrage)";
    case 0x20: return "DIIMMBUF (reponse)";
    case 0x24: return "DICFG (config)";
    }
    return "?";
}
static const char *na_region(u8 r) {
    return r == 0 ? "Japon (NTSC-J)" : r == 1 ? "Amerique (NTSC-U)" : r == 2 ? "Europe (PAL)" : r == 4 ? "Coree (NTSC-K)" : "inconnue";
}

/* debut de la fonction qui contient addr : on remonte jusqu'a "blr" ou "mflr r0" / "stwu r1" */
static u32 na_func_start(const u8 *txt, u32 base, u32 addr) {
    u32 a = addr;
    for (int n = 0; n < 2048; n++) {
        u32 cur = be32(txt + (a - base));
        if ((cur & 0xFFFF0000u) == 0x94210000u && (cur & 0x8000u)) return a;   /* stwu r1,-N(r1) : prologue */
        if (a == base) return a;                              /* debut de la section */
        if (be32(txt + (a - 4 - base)) == 0x4E800020u) return a;   /* blr : la fonction d'avant finit la */
        a -= 4;
    }
    return addr;
}

/* Cherche les acces aux registres du lecteur. On suit, instruction par instruction,
   les registres qui contiennent une adresse connue (lis rX,0xCC00 puis addi / ori),
   et on note les lectures / ecritures dont l'adresse tombe dans 0xCC006000..0xCC00603F.
   L'etat est oublie a chaque branchement (simple, mais suffisant pour un rapport). */
static void na_scan_text(nat_info_t *in, const u8 *txt, u32 base, u32 len) {
    u32 val[32]; u32 known = 0;
    for (u32 o = 0; o + 4 <= len; o += 4) {
        if ((o & 0x3FFFF) == 0) service_audio();                  /* (v50 : la musique continue pendant l'analyse) */
        u32 ins = be32(txt + o);
        u32 op = ins >> 26, rd = (ins >> 21) & 31, ra = (ins >> 16) & 31;
        s32 simm = (s16)(ins & 0xFFFF);
        u32 uimm = ins & 0xFFFF;
        if (op == 15) {                                       /* addis / lis */
            if (ra == 0) { val[rd] = uimm << 16; known |= 1u << rd; }
            else if (known & (1u << ra)) { val[rd] = val[ra] + (uimm << 16); known |= 1u << rd; }
            else known &= ~(1u << rd);
            continue;
        }
        if (op == 14) {                                       /* addi / li */
            if (ra == 0) { val[rd] = (u32)simm; known |= 1u << rd; }
            else if (known & (1u << ra)) { val[rd] = val[ra] + (u32)simm; known |= 1u << rd; }
            else known &= ~(1u << rd);
            continue;
        }
        if (op == 24) {                                       /* ori rA,rS,imm */
            u32 rs = rd, rdst = ra;
            if (known & (1u << rs)) { val[rdst] = val[rs] | uimm; known |= 1u << rdst; }
            else known &= ~(1u << rdst);
            continue;
        }
        /* lwz 32, lwzu 33, stw 36, stwu 37, lhz 40, sth 44 */
        if (op == 32 || op == 33 || op == 36 || op == 37 || op == 40 || op == 44) {
            int store = op >= 36 && op != 40;
            if (ra && (known & (1u << ra))) {
                u32 ea = val[ra] + (u32)simm;
                if (ea >= NA_DI && ea < NA_DI + 0x40 && in->nhit < NA_MAXHIT) {
                    u32 at = base + o;
                    in->hit_addr[in->nhit] = at;
                    in->hit_reg[in->nhit] = (u16)(ea - NA_DI);
                    in->hit_store[in->nhit] = (u8)store;
                    in->nhit++;
                    u32 f = na_func_start(txt, base, at);
                    int seen = 0;
                    for (int i = 0; i < in->nfunc; i++) if (in->func_addr[i] == f) seen = 1;
                    if (!seen && in->nfunc < NA_MAXHIT) in->func_addr[in->nfunc++] = f;
                }
                if (op == 33 || op == 37) val[ra] = ea;       /* (forme "u" : rA avance) */
            }
            if (!store) known &= ~(1u << rd);
            continue;
        }
        if (op == 16 || op == 18 || ins == 0x4E800020u || (op == 19 && ((ins >> 1) & 0x3FF) == 528)) { known = 0; continue; }
        /* toute autre instruction qui ecrit un registre general : on l'oublie (approximation) */
        if (op == 31) known &= ~((1u << rd) | (1u << ra));                                     /* (or, and, mr : destination = rA ; add, lwzx : rD) */
        else if (op == 7 || op == 8 || op == 12 || op == 13) known &= ~(1u << rd);              /* destination = rD */
        else if (op == 46) known = 0;                                                          /* lmw : plusieurs registres */
        else if ((op >= 20 && op <= 29) || op == 34 || op == 35 || op == 41 || op == 42 || op == 43)  /* rotations, logiques ; lbz, lha */
            known &= ~((1u << ra) | (1u << rd));
    }
}

/* analyse complete ; retourne 0 si le disque est illisible */
static int nat_analyse(const char *path, nat_info_t *in) {
    memset(in, 0, sizeof(*in));
    gdisc_t d;
    if (!gd_open(&d, path)) { snprintf(in->err, sizeof(in->err), "Fichier illisible"); return 0; }
    u8 h[0x440], b2[0x40], al[0x20];
    if (!gd_read(&d, 0, h, sizeof(h)) || be32(h + 0x1C) != 0xC2339F3Du) { gd_close(&d); snprintf(in->err, sizeof(in->err), "Pas un disque GameCube"); return 0; }
    memcpy(in->id, h, 6);
    in->disc = h[6]; in->ver = h[7]; in->stream = h[8];
    gl_latin(h + 0x20, 0x40, in->title, sizeof(in->title));
    in->dol_off = be32(h + 0x420);
    in->fst_off = be32(h + 0x424);
    in->fst_sz  = be32(h + 0x428);
    in->fst_max = be32(h + 0x42C);
    if (gd_read(&d, 0x440, b2, sizeof(b2))) in->region = (u8)be32(b2 + 0x18);
    if (gd_read(&d, 0x2440, al, sizeof(al))) {
        memcpy(in->al_date, al, 16); in->al_date[16] = 0;
        for (int i = 0; i < 16; i++) if ((u8)in->al_date[i] < 0x20 || (u8)in->al_date[i] > 0x7E) in->al_date[i] = 0;
        in->al_entry = be32(al + 0x10); in->al_size = be32(al + 0x14); in->al_trailer = be32(al + 0x18);
        if (in->al_entry < 0x81200000u || in->al_entry >= 0x81300000u || in->al_size == 0 || in->al_size > 0x100000) {
            in->problems++; snprintf(in->err, sizeof(in->err), "Apploader inattendu");
        }
    }
    /* main.dol : en-tete puis sections */
    u8 dh[0x100];
    if (!in->dol_off || !gd_read(&d, in->dol_off, dh, sizeof(dh))) { gd_close(&d); snprintf(in->err, sizeof(in->err), "main.dol introuvable"); return 0; }
    in->entry = be32(dh + 0xE0); in->bss = be32(dh + 0xD8); in->bsz = be32(dh + 0xDC);
    in->lo = 0xFFFFFFFFu; in->hi = 0;
    u32 dol_end = 0x100;
    for (int i = 0; i < 18; i++) {
        u32 off = be32(dh + i * 4), addr = be32(dh + 0x48 + i * 4), sz = be32(dh + 0x90 + i * 4);
        if (!sz) continue;
        if (addr < 0x80003000u || addr + (u64)sz > 0x81800000u) { in->problems++; snprintf(in->err, sizeof(in->err), "Section %d hors memoire (%08X)", i, (unsigned)addr); continue; }
        in->sec_addr[in->nsec] = addr; in->sec_sz[in->nsec] = sz; in->nsec++;
        if (addr < in->lo) in->lo = addr;
        if (addr + sz > in->hi) in->hi = addr + sz;
        if (off + sz > dol_end) dol_end = off + sz;
        if (i < 7) {                                         /* code : on le parcourt */
            u8 *t = (u8 *)malloc(sz);
            int okr = t != NULL;
            for (u32 o = 0; okr && o < sz; o += 0x20000) {      /* (v50 : lu par morceaux de 128 Ko, sans couper la musique) */
                u32 k = sz - o < 0x20000 ? sz - o : 0x20000;
                okr = gd_read(&d, in->dol_off + off + o, t + o, k);
                service_audio();
            }
            if (okr) na_scan_text(in, t, addr, sz);
            else { in->problems++; snprintf(in->err, sizeof(in->err), "Section %d illisible (m\xC3\xA9moire ?)", i); }
            free(t);
            service_audio();
        }
    }
    in->dol_len = dol_end;
    if (in->bss + (u64)in->bsz > in->hi) in->hi = in->bss + in->bsz;
    if (!in->nsec || in->entry < in->lo || in->entry >= in->hi) { in->problems++; snprintf(in->err, sizeof(in->err), "Point d'entree invalide"); }
    /* FST : l'apploader la range en haut de la memoire (24 Mo), sous la taille maximale annoncee */
    in->fst_dest = (0x81800000u - in->fst_max) & ~31u;
    if (in->fst_sz && in->fst_sz <= 4u << 20) {
        u8 *f = (u8 *)malloc(in->fst_sz);
        if (f && gd_read(&d, in->fst_off, f, in->fst_sz)) {
            u32 n = be32(f + 8);
            if (n && n * 12 <= in->fst_sz)
                for (u32 i = 1; i < n; i++) { if (f[i * 12]) in->ndirs++; else in->nfiles++; }
        }
        free(f);
    } else { in->problems++; snprintf(in->err, sizeof(in->err), "FST invalide"); }
    if (in->hi > in->fst_dest) { in->problems++; snprintf(in->err, sizeof(in->err), "main.dol chevauche la FST"); }
    gd_close(&d);
    return 1;
}

static void nat_write_report(const nat_info_t *in, const char *path, char *out, int cap) {
    mkdir(NA_DIR, 0777);
    snprintf(out, cap, "%s/%s.txt", NA_DIR, in->id[0] ? in->id : "inconnu");
    FILE *f = fopen(out, "w");
    if (!f) { out[0] = 0; return; }
    fprintf(f, "GC Station - analyse native (etape 1 du lanceur maison)\n");
    fprintf(f, "Fichier : %s\n\n", path);
    fprintf(f, "[Disque]\nID : %s   disque %u   version %u\nNom : %s\nRegion : %s\nAudio en streaming : %s\n\n",
            in->id, in->disc + 1, in->ver, in->title, na_region(in->region), in->stream ? "oui" : "non");
    fprintf(f, "[Apploader] 0x2440\nDate : %s\nPoint d'entree : %08X   taille : %u   suite : %u\n\n",
            in->al_date, (unsigned)in->al_entry, (unsigned)in->al_size, (unsigned)in->al_trailer);
    fprintf(f, "[main.dol] position sur le disque : %08X   taille : %u octets\n", (unsigned)in->dol_off, (unsigned)in->dol_len);
    fprintf(f, "Point d'entree : %08X\nMemoire utilisee : %08X - %08X (BSS %08X, %u octets)\n",
            (unsigned)in->entry, (unsigned)in->lo, (unsigned)in->hi, (unsigned)in->bss, (unsigned)in->bsz);
    for (int i = 0; i < in->nsec; i++) fprintf(f, "  section %2d : %08X  %7u octets\n", i, (unsigned)in->sec_addr[i], (unsigned)in->sec_sz[i]);
    fprintf(f, "\n[FST] position %08X  taille %u  maximum %u\n", (unsigned)in->fst_off, (unsigned)in->fst_sz, (unsigned)in->fst_max);
    fprintf(f, "Fichiers : %u   dossiers : %u\nEmplacement prevu en memoire : %08X\n\n", (unsigned)in->nfiles, (unsigned)in->ndirs, (unsigned)in->fst_dest);
    fprintf(f, "[Memoire basse a preparer]\n");
    fprintf(f, "80000000 : 32 premiers octets du disque (ID...)\n");
    fprintf(f, "80000020 : 0D15EA5E (demarrage normal)   80000028 : 01800000 (24 Mo)\n");
    fprintf(f, "80000034 : %08X (haut de l'arene)   80000038 : %08X (FST)   8000003C : %08X\n",
            (unsigned)in->fst_dest, (unsigned)in->fst_dest, (unsigned)in->fst_max);
    fprintf(f, "800000F4 : copie de bi2.bin   800000F8 : 09A7EC80 (bus)   800000FC : 1CF7C580 (processeur)\n\n");
    fprintf(f, "[Lecteur DVD] acces aux registres CC006000 trouves dans main.dol : %d\n", in->nhit);
    for (int i = 0; i < in->nhit; i++)
        fprintf(f, "  %08X  %s %s\n", (unsigned)in->hit_addr[i], in->hit_store[i] ? "ecrit " : "lit   ", na_reg_name(in->hit_reg[i]));
    fprintf(f, "Fonctions concernees : %d\n", in->nfunc);
    for (int i = 0; i < in->nfunc; i++) fprintf(f, "  %08X\n", (unsigned)in->func_addr[i]);
    fprintf(f, "\n(Ce sont elles qu'il faudra detourner pour lire le jeu sur la carte SD.)\n\n");
    fprintf(f, "Bilan : %s%s\n", in->problems ? "probleme : " : "OK, ce jeu est pret pour l'etape 2", in->problems ? in->err : "");
    fclose(f);
}

/* fiche d'un jeu : R */
static void nat_run(int k) {
    if (k < 0 || k >= gl_n) return;
    gl_status("Analyse native du jeu...");
    static nat_info_t in;
    char rep[NAME_LEN];
    int ok = nat_analyse(gl[k].path, &in);
    gl_busy[0] = 0;
    if (!ok) { gl_note(in.err); crumb("natif : %s", in.err); return; }
    nat_write_report(&in, gl[k].path, rep, sizeof(rep));
    char m[200];
    if (in.problems) snprintf(m, sizeof(m), "Analyse : %s", in.err);
    else snprintf(m, sizeof(m), "Analyse OK : %d acc\xC3\xA8s au lecteur, %u fichiers (rapport : gc-mp3/natif/%s.txt)",
                  in.nhit, (unsigned)in.nfiles, in.id);
    gl_note(m);
    if (rep[0]) nat_qr_open(in.id);                          /* v51 : QR code vers le rapport */
    crumb("natif %s : %d acces DI, %d fonctions, %d problemes", in.id, in.nhit, in.nfunc, in.problems);
}

/* ---------- v51 : le rapport sur le telephone (QR code) ----------
   Le serveur de la telecommande (onglet TELEPHONE) donne /natif (liste) et /natif/<ID>.txt.
   Apres une analyse, la fiche montre un QR code vers le rapport. */
static int na_qr_on = 0, na_qr_ok = 0;
static char na_qr_id[8] = "", na_qr_url[96] = "";
static u8 na_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
static void nat_qr_open(const char *id) {
    snprintf(na_qr_id, sizeof(na_qr_id), "%s", id);
    na_qr_on = 1; na_qr_ok = 0; na_qr_url[0] = 0;
    if (tp_state != PS_ON) { tp_wanted = 1; net_start_async(); }      /* (le serveur demarre tout seul) */
}
static int nat_qr_input(u32 down) {                     /* 1 = touche prise par la fenetre */
    if (!na_qr_on) return 0;
    if (down & (PAD_BUTTON_B | PAD_BUTTON_A | PAD_TRIGGER_R)) na_qr_on = 0;
    return 1;
}
static void nat_draw_qr(void) {
    if (!na_qr_on) return;
    if (!na_qr_ok && tp_state == PS_ON && ip_str[0]) {
        if (PHONE_PORT == 80) snprintf(na_qr_url, sizeof(na_qr_url), "http://%s/natif/%s.txt", ip_str, na_qr_id);
        else snprintf(na_qr_url, sizeof(na_qr_url), "http://%s:%d/natif/%s.txt", ip_str, PHONE_PORT, na_qr_id);
        static u8 tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(8)];
        na_qr_ok = qrcodegen_encodeText(na_qr_url, tmp, na_qr, qrcodegen_Ecc_MEDIUM, 1, 8, qrcodegen_Mask_AUTO, true) ? 1 : -1;
    }
    const f32 w = 440, h = 300, x = 100, y = 96;
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
    gx_rrect_fill(x, y, w, h, 12, RGBA(14, 8, 44, 250), RGBA(8, 5, 26, 250));
    draw_panel(x, y, w, h);
    gx_text_c(F_MED, 320, y + 30, "Rapport d'analyse sur ton t\xC3\xA9l\xC3\xA9phone", RGBA(255, 255, 255, 255));
    if (na_qr_ok > 0) {
        draw_qr_buf(na_qr, 320 - 90, y + 44, 180);
        gx_text_c(F_SMALL, 320, y + 246, na_qr_url, RGBA(255, 210, 120, 255));
        gx_text_c(F_SMALL, 320, y + 266, "Scanne le QR code (m\xC3\xAAme Wi-Fi que la GameCube)", RGBA(190, 180, 235, 255));
    } else if (tp_state == PS_ERR || (net_phase == NET_DONE && !net_ok)) {
        gx_text_c(F_SMALL, 320, y + 140, "Pas de r\xC3\xA9seau : le rapport est dans SD:/gc-mp3/natif", RGBA(255, 160, 120, 255));
    } else {
        gx_text_c(F_SMALL, 320, y + 140, "D\xC3\xA9marrage du serveur...", RGBA(190, 180, 235, 255));
    }
    gx_text_c(F_SMALL, 320, y + h - 10, "B : fermer", RGBA(255, 210, 120, 255));
}

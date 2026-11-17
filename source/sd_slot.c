// GC Station v469 : carte SD dans un slot de carte memoire (A, B) : notre propre pilote quand celui de libogc la refuse.
// Inclus dans main.c (apres sd_mount_cnt, avant mount_sd). Ecrit d'apres la norme SD (mode SPI), pas d'apres un autre pilote.
//
// Le pilote de libogc (sdgecko) refuse d'emblee un adaptateur dont l'identifiant EXI n'est pas FFFFFFFF, et demarre la
// carte directement a 16 MHz (la norme demande un demarrage lent). Swiss et Cubiboot lisaient la carte d'un testeur
// (GC2SD, DOL-101) que GC Station ne voyait pas. Ici : pas d'exigence sur l'identifiant, demarrage a 1 MHz (la vitesse
// la plus basse du bus EXI), puis la vitesse la plus haute qui relit le secteur 0 a l'identique (16, 8, 4 ou 1 MHz).
// Une commande a la fois (lecture et ecriture secteur par secteur) : simple et sur, la vitesse compte peu ici.
// sdx_diag[k] : ce que chaque slot a donne (affiche au demarrage si aucune carte n'est trouvee ; journal de demarrage).

static char sdx_diag[2][120];
static int sdx_hc[2], sdx_frq[2];
/* v512 : vitesse sure (16, 8, 4 ou 1 MHz : relit le secteur 0 a l'identique, comme avant la v505), pour les ecritures et pour
   Pulse. Depuis la v505, les ecritures partaient a 16 MHz sans verification : un GC2SD (Platter, builds 1033-1039) qui ne
   suit pas a 16 MHz voyait ses ecritures acceptees mais perdues (fichiers du lancement jamais mis a jour, carte virtuelle
   lue « non formatee » en jeu). Chaque ecriture est maintenant relue ; si elle ne se relit pas a l'identique, vitesse plus
   basse et nouvel essai. */
static int sdx_sfrq[2];
/* build 1051 : slot B, comme le pilote SD de libogc2 qu'utilise Swiss (principe relu, code ecrit ici) : la carte n'est choisie
   (CS) que pour envoyer (commande, donnees ecrites) ; tout ce qui se lit (reponse, jeton, donnees, CRC, reponse aux donnees
   ecrites, attente pendant l'ecriture) se lit horloge seule, carte pas choisie. Avant, ces lectures carte choisie a 32 MHz
   etaient fausses sur le GC2SD de Platter : ecritures ratees a 32 MHz (vitesse sure 16 MHz). Controle d'erreurs de la carte
   active (CMD59, CRC des commandes et des donnees ecrites) ; ecritures essayees a la vitesse des lectures (sdx_wfrq), relues
   comme avant, plus lentes si fausses. sdx_nc[k] : 1 = cette facon, gardee si la carte relit juste au demarrage. */
static int sdx_wfrq[2], sdx_nc[2], sdx_crcon[2];
static int sdx_cur_ch = -1, sdx_cur_frq = 0;
static int sdx_used = 0;                                   /* v471 : 1 / 2 = carte du slot A / B lue par ce pilote */

#ifdef GEKKO
static u8 sdx_ffb[512] ATTRIBUTE_ALIGN(32);
static u8 sdx_rd8(int ch) {
    u8 b[4] ATTRIBUTE_ALIGN(32) = { 0xFF, 0xFF, 0xFF, 0xFF };
    EXI_ImmEx(ch, b, 1, EXI_READWRITE);                    /* (0xFF envoye pendant la lecture, comme le veut la carte) */
    return b[0];
}
static void sdx_rdn(int ch, u8 *d, u32 n) {                /* n octets lus (0xFF envoyes) */
    memcpy(d, sdx_ffb, n);
    EXI_ImmEx(ch, d, n, EXI_READWRITE);
}
/* build 1051 : CRC7 des commandes (norme SD : polynome x^7 + x^3 + 1), exige une fois le controle d'erreurs active */
static u8 sdx_crc7(const u8 *d, int n) {
    u8 c = 0;
    for (int i = 0; i < n; i++) for (int b = 7; b >= 0; b--) { int x = ((c >> 6) ^ (d[i] >> b)) & 1; c = (u8)((c << 1) & 0x7F); if (x) c ^= 0x09; }
    return (u8)(c << 1 | 1);
}
/* build 1051 : slot B : passer a la lecture (horloge seule) ou a l'envoi (carte choisie), meme vitesse, verrou garde */
static void sdx_rdmode(int ch) { if (ch == sdx_cur_ch && sdx_nc[ch]) { EXI_Deselect(ch); EXI_SelectSD(ch, EXI_DEVICE_0, sdx_cur_frq); } }
static void sdx_wrmode(int ch) { if (ch == sdx_cur_ch && sdx_nc[ch]) { EXI_Deselect(ch); EXI_Select(ch, EXI_DEVICE_0, sdx_cur_frq); } }
/* commande (carte deja selectionnee) ; rend la reponse R1 (0xFF : pas de reponse) */
static u8 sdx_cmd(int ch, int cmd, u32 arg, u8 crc) {
    u8 c[8] ATTRIBUTE_ALIGN(32) = { 0xFF, (u8)(0x40 | cmd), (u8)(arg >> 24), (u8)(arg >> 16), (u8)(arg >> 8), (u8)arg, (u8)(crc | 1), 0xFF };
    c[6] = sdx_crc7(c + 1, 5);                             /* (build 1051 : toujours le bon CRC7) */
    sdx_wrmode(ch);
    EXI_ImmEx(ch, c, 7, EXI_WRITE);
    sdx_rdmode(ch);
    u8 r = 0xFF;
    for (int i = 0; i < 16; i++) { r = sdx_rd8(ch); if (!(r & 0x80)) break; }
    return r;
}
static int sdx_sel(int ch, int frq) {
    if (!EXI_Lock(ch, EXI_DEVICE_0, NULL)) return 0;
    if (!EXI_Select(ch, EXI_DEVICE_0, frq)) { EXI_Unlock(ch); return 0; }
    sdx_cur_ch = ch; sdx_cur_frq = frq;
    return 1;
}
static void sdx_desel(int ch) {
    EXI_Deselect(ch);
    EXI_Unlock(ch);
    sdx_cur_ch = -1;
}
/* v505 : CRC16 des donnees (norme SD : polynome x^16 + x^12 + x^5 + 1), pour verifier chaque secteur lu a 32 MHz */
static u16 sdx_crc16(const u8 *d, int n) {
    u16 c = 0;
    for (int i = 0; i < n; i++) { c ^= (u16)d[i] << 8; for (int b = 0; b < 8; b++) c = c & 0x8000 ? (u16)(c << 1) ^ 0x1021 : (u16)(c << 1); }
    return c;
}
/* un secteur lu (lba) ; 1 = OK (v505 : a 32 MHz, CRC verifie ; faux : retour definitif a 16 MHz et nouvel essai) */
static int sdx_read1(int k, u32 lba, u8 *d) {
    int ch = k, ok = 0, fast = sdx_frq[k] == EXI_SPEED32MHZ;
    if (!sdx_sel(ch, sdx_frq[k])) return 0;
    if (sdx_cmd(ch, 17, sdx_hc[k] ? lba : lba << 9, 0) == 0) {
        u8 t = 0xFF;
        for (u32 i = 0; i < 200000u && t == 0xFF; i++) t = sdx_rd8(ch);   /* (jeton de debut des donnees : FE) */
        if (t == 0xFE) {
            u8 crc[4] ATTRIBUTE_ALIGN(32);
            sdx_rdn(ch, d, 512);
            sdx_rdn(ch, crc, 2);
            ok = !fast || ((u16)(crc[0] << 8 | crc[1]) == sdx_crc16(d, 512));
        }
    }
    sdx_desel(ch);
    if (fast && !ok) { sdx_frq[k] = sdx_sfrq[k]; return sdx_read1(k, lba, d); }   /* v505 : la carte ne suit pas (v512 : vitesse sure, plus 16 MHz d'office) */
    return ok;
}
static int sdx_write1(int k, u32 lba, const u8 *s) {
    int ch = k, ok = 0;
    static u8 buf[512] ATTRIBUTE_ALIGN(32);
    if (!sdx_sel(ch, sdx_wfrq[k])) return 0;              /* (v505 : jamais a 32 MHz ; v512 : a la vitesse sure ; build 1051 : sdx_wfrq) */
    if (sdx_cmd(ch, 24, sdx_hc[k] ? lba : lba << 9, 0) == 0) {
        u8 h[4] ATTRIBUTE_ALIGN(32) = { 0xFF, 0xFE, 0, 0 };
        sdx_wrmode(ch);                                    /* (build 1051 : slot B : carte choisie pour envoyer) */
        EXI_ImmEx(ch, h, 2, EXI_WRITE);                    /* (un octet d'attente, puis le jeton de debut) */
        memcpy(buf, s, 512);
        EXI_ImmEx(ch, buf, 512, EXI_WRITE);
        u16 cs = sdx_crc16(buf, 512);                      /* (build 1051 : le vrai CRC : la carte le verifie si CMD59) */
        u8 crc[4] ATTRIBUTE_ALIGN(32) = { (u8)(cs >> 8), (u8)cs, 0, 0 };
        EXI_ImmEx(ch, crc, 2, EXI_WRITE);
        sdx_rdmode(ch);                                    /* (build 1051 : reponse et attente : horloge seule) */
        u8 r = 0xFF;
        for (int i = 0; i < 16 && r == 0xFF; i++) r = sdx_rd8(ch);
        if ((r & 0x1F) == 0x05) {                          /* (donnees acceptees) puis la carte ecrit : 00 tant qu'occupee */
            for (u32 i = 0; i < 2000000u; i++) if (sdx_rd8(ch) == 0xFF) { ok = 1; break; }
        }
    }
    sdx_desel(ch);
    return ok;
}
/* demarrage de la carte du slot k (0 = A, 1 = B) ; 1 = prete. Note dans sdx_diag[k] ce qui s'est passe. */
static int sdx_init(int k) {
    int ch = k;
    char *dg = sdx_diag[k];
    memset(sdx_ffb, 0xFF, sizeof(sdx_ffb));
    /* v470 : 0 = un appareil est la, mais la console attend encore (300 ms apres l'avoir vu ; ou son identifiant ne se lit pas) :
       attendre jusqu'a 1,5 s, puis essayer quand meme. -1 seulement = rien de branche. (v469 : 100 ms puis abandon : "probe 0") */
    s32 pr = 0;
    for (int i = 0; i < 1500 && (pr = EXI_ProbeEx(ch)) == 0; i++) usleep(1000);
    u32 id = 0;
    int idok = EXI_GetID(ch, EXI_DEVICE_0, &id);
    if (pr < 0) { snprintf(dg, 120, "nothing plugged in (probe %d)", (int)pr); return 0; }
    if (idok && id > 0 && id <= 0x1000u) { snprintf(dg, 120, "memory card (ID %08X), not an SD adapter", (unsigned)id); return 0; }   /* (pas de commandes SD a une carte memoire) */
    /* 80 coups d'horloge carte deselectionnee, a 1 MHz, puis CMD0 (remise a zero, passage en SPI) */
    if (!EXI_Lock(ch, EXI_DEVICE_0, NULL)) { snprintf(dg, 120, "ID %08X (probe %d); port busy", (unsigned)id, (int)pr); return 0; }
    if (EXI_SelectSD(ch, EXI_DEVICE_0, EXI_SPEED1MHZ)) { u8 z[12] ATTRIBUTE_ALIGN(32); memset(z, 0xFF, 10); EXI_ImmEx(ch, z, 10, EXI_WRITE); EXI_Deselect(ch); }
    EXI_Unlock(ch);
    sdx_frq[k] = EXI_SPEED1MHZ; sdx_hc[k] = 0;
    u8 r = 0xFF;
    for (int t = 0; t < 10 && r != 0x01; t++) {
        if (!sdx_sel(ch, EXI_SPEED1MHZ)) break;
        r = sdx_cmd(ch, 0, 0, 0x94);
        sdx_desel(ch);
    }
    if (r != 0x01) { snprintf(dg, 120, "ID %08X%s (probe %d); libogc refused it; card does not answer (CMD0: %02X)", (unsigned)id, idok ? "" : " (?)", (int)pr, r); return 0; }
    /* CMD8 : carte de 2e generation (SDHC / SDXC) ? */
    int v2 = 0;
    if (sdx_sel(ch, EXI_SPEED1MHZ)) {
        r = sdx_cmd(ch, 8, 0x1AA, 0x86);
        u8 o[4] ATTRIBUTE_ALIGN(32);
        if (r == 0x01) { sdx_rdn(ch, o, 4); v2 = (o[2] & 0x0F) == 0x01 && o[3] == 0xAA; }
        sdx_desel(ch);
    }
    /* ACMD41 jusqu'a ce que la carte soit prete (1 s au plus) */
    r = 0xFF;
    for (int t = 0; t < 1000 && r != 0x00; t++) {
        if (!sdx_sel(ch, EXI_SPEED1MHZ)) break;
        r = sdx_cmd(ch, 55, 0, 0);
        if (r <= 1) r = sdx_cmd(ch, 41, v2 ? 0x40000000u : 0, 0);
        sdx_desel(ch);
        if (r) usleep(1000);
    }
    if (r != 0x00) { snprintf(dg, 120, "ID %08X; libogc refused it; card does not start (ACMD41: %02X%s)", (unsigned)id, r, v2 ? ", SDHC" : ""); return 0; }
    if (v2 && sdx_sel(ch, EXI_SPEED1MHZ)) {                /* CMD58 : adressage par secteur (SDHC) ? */
        u8 o[4] ATTRIBUTE_ALIGN(32);
        if (sdx_cmd(ch, 58, 0, 0) == 0) { sdx_rdn(ch, o, 4); sdx_hc[k] = (o[0] & 0x40) != 0; }
        sdx_desel(ch);
    }
    if (!sdx_hc[k] && sdx_sel(ch, EXI_SPEED1MHZ)) { sdx_cmd(ch, 16, 512, 0); sdx_desel(ch); }   /* (blocs de 512 octets) */
    /* vitesse : la plus haute qui relit le secteur 0 comme a 1 MHz */
    static u8 a[512] ATTRIBUTE_ALIGN(32), b[512] ATTRIBUTE_ALIGN(32);
    if (!sdx_read1(k, 0, a)) { snprintf(dg, 120, "ID %08X; libogc refused it; %s card started but sector 0 unreadable", (unsigned)id, sdx_hc[k] ? "SDHC" : "SD"); return 0; }
    /* v505 : 32 MHz essaye d'abord, comme Swiss (le GC2SD est donne pour 32 MHz) : 4 lectures justes et au bon CRC */
    static const int fr[4] = { EXI_SPEED32MHZ, EXI_SPEED16MHZ, EXI_SPEED8MHZ, EXI_SPEED4MHZ };
    static const int mhz[4] = { 32, 16, 8, 4 };
    int got = 1;
    for (int i = 0; i < 4; i++) {
        sdx_frq[k] = fr[i];
        int ok = 1;
        for (int t = 0; t < (i ? 2 : 4) && ok; t++) { ok = sdx_read1(k, 0, b) && !memcmp(a, b, 512) && sdx_frq[k] == fr[i]; }
        if (ok) { got = mhz[i]; break; }
        sdx_frq[k] = EXI_SPEED1MHZ;
    }
    /* build 1051 : slot B : essai de la lecture horloge seule (comme Swiss) ; gardee si le secteur 0 se relit juste a la
       meilleure vitesse (4 fois, CRC verifie a 32 MHz) ; puis controle d'erreurs de la carte (CMD59) */
    if (k == 1) {
        int rf0 = sdx_frq[k], gf = got;
        sdx_nc[k] = 1;
        static const int fr2[2] = { EXI_SPEED32MHZ, EXI_SPEED16MHZ };
        int ok2 = 0;
        for (int i = 0; i < 2 && !ok2; i++) {
            if (fr2[i] < rf0 && fr2[i] != EXI_SPEED32MHZ) break;   /* (pas plus lent que la facon d'avant) */
            sdx_frq[k] = fr2[i]; ok2 = 1;
            for (int t = 0; t < 4 && ok2; t++) ok2 = sdx_read1(k, 0, b) && !memcmp(a, b, 512) && sdx_frq[k] == fr2[i];
            if (ok2) gf = fr2[i] == EXI_SPEED32MHZ ? 32 : 16;
        }
        if (ok2 && gf >= got) { got = gf; }
        else { sdx_nc[k] = 0; sdx_frq[k] = rf0; }
        if (sdx_nc[k] && sdx_sel(ch, sdx_frq[k])) {        /* CMD59 : controle d'erreurs de la carte */
            u8 r59 = sdx_cmd(ch, 59, 1, 0);
            sdx_desel(ch);
            sdx_crcon[k] = r59 == 0;
            if (!(sdx_read1(k, 0, b) && !memcmp(a, b, 512))) {   /* (relit encore juste ?) sinon : sans controle d'erreurs */
                if (sdx_sel(ch, sdx_frq[k])) { sdx_cmd(ch, 59, 0, 0); sdx_desel(ch); }
                sdx_crcon[k] = 0;
            }
        }
    }
    /* v512 : vitesse sure (sans CRC : 2 relectures identiques), pour les ecritures et Pulse */
    int rf = sdx_frq[k], wm = 1;
    sdx_sfrq[k] = EXI_SPEED1MHZ;
    for (int i = 1; i < 4; i++) {
        sdx_frq[k] = fr[i];
        if (sdx_read1(k, 0, b) && !memcmp(a, b, 512) && sdx_read1(k, 0, b) && !memcmp(a, b, 512)) { sdx_sfrq[k] = fr[i]; wm = mhz[i]; break; }
    }
    sdx_frq[k] = rf;
    if (rf != EXI_SPEED32MHZ && rf > sdx_sfrq[k]) sdx_frq[k] = sdx_sfrq[k];
    sdx_wfrq[k] = sdx_nc[k] ? sdx_frq[k] : sdx_sfrq[k];   /* (build 1051 : slot B, lecture horloge seule : ecritures essayees a la vitesse des lectures) */
    snprintf(dg, 120, "ID %08X; libogc refused it; read by GC Station driver (%s, %d MHz, writes %d MHz%s%s)", (unsigned)id, sdx_hc[k] ? "SDHC" : "SD", got, wm,
             sdx_nc[k] ? ", reads CS off" : "", sdx_crcon[k] ? ", CRC on" : "");
    return 1;
}
static bool sdx_rd(int k, sec_t s, sec_t n, void *buf) {
    u8 *d = (u8 *)buf;
    for (sec_t i = 0; i < n; i++) {
        int ok = sdx_read1(k, s + i, d + i * 512);
        if (!ok) ok = sdx_read1(k, s + i, d + i * 512);   /* (une seconde chance) */
        if (!ok) return false;
    }
    return true;
}
static int sdx_wbad = 0;                                   /* v512 : ecritures qui ne se relisaient pas (journal de demarrage) */
static bool sdx_wr(int k, sec_t s, sec_t n, const void *buf) {
    static u8 vb[512] ATTRIBUTE_ALIGN(32);
    const u8 *q = (const u8 *)buf;
    for (sec_t i = 0; i < n; i++) {
        int ok = 0;
        for (int t = 0; t < 6 && !ok; t++) {             /* v512 : ecrit, puis relu ; faux : vitesse plus basse et nouvel essai */
            ok = sdx_write1(k, s + i, q + i * 512) && sdx_read1(k, s + i, vb) && !memcmp(vb, q + i * 512, 512);
            if (!ok) {
                sdx_wbad++;
                if (t && sdx_wfrq[k] > EXI_SPEED1MHZ) sdx_wfrq[k]--;   /* (1er rate : un accident ; ensuite plus lent ; build 1051 : sdx_wfrq) */
                if (sdx_wfrq[k] < sdx_sfrq[k]) sdx_sfrq[k] = sdx_wfrq[k];   /* (la vitesse sure, pour Pulse, pas au-dessus des ecritures qui passent) */
                if (sdx_frq[k] != EXI_SPEED32MHZ && sdx_frq[k] > sdx_sfrq[k]) sdx_frq[k] = sdx_sfrq[k];
            }
        }
        if (!ok) return false;
    }
    return true;
}
static bool sdxa_startup(void) { return true; }            /* (deja demarree par sdx_init) */
static bool sdxa_inserted(void) { return true; }
static bool sdxa_read(sec_t s, sec_t n, void *b) { return sdx_rd(0, s, n, b); }
static bool sdxa_write(sec_t s, sec_t n, const void *b) { return sdx_wr(0, s, n, b); }
static bool sdxb_read(sec_t s, sec_t n, void *b) { return sdx_rd(1, s, n, b); }
static bool sdxb_write(sec_t s, sec_t n, const void *b) { return sdx_wr(1, s, n, b); }
static bool sdx_clear(void) { return true; }
static bool sdx_shut(void) { return true; }
static const DISC_INTERFACE sdx_if[2] = {
    { 0x47435841, FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_GAMECUBE_SLOTA, sdxa_startup, sdxa_inserted, sdxa_read, sdxa_write, sdx_clear, sdx_shut },   /* "GCXA" */
    { 0x47435842, FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_GAMECUBE_SLOTB, sdxa_startup, sdxa_inserted, sdxb_read, sdxb_write, sdx_clear, sdx_shut },   /* "GCXB" */
};
#endif

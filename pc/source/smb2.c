// GC Station v86 : client SMB2 (partages Windows / Samba / NAS), ecrit pour GC Station.
// (inclus dans netfs.c) Dialectes 2.0.2 et 2.1, identification NTLMv2 (ou invite),
// signature HMAC-SHA256 si le serveur l'exige (Windows 11). Lecture seulement.
// Le vieux SMB1 (celui de Swiss) est desactive par defaut sur Windows 10 / 11 et Samba.

/* ---------- MD4 (pour le mot de passe NTLM : absent de mbedTLS 3) ---------- */
#define MD4_F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define MD4_G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define MD4_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD4_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
static void md4_block(u32 st[4], const u8 *p) {
    u32 x[16], a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 16; i++) x[i] = (u32)p[i * 4] | (u32)p[i * 4 + 1] << 8 | (u32)p[i * 4 + 2] << 16 | (u32)p[i * 4 + 3] << 24;
    static const int s1[4] = { 3, 7, 11, 19 }, s2[4] = { 3, 5, 9, 13 }, s3[4] = { 3, 9, 11, 15 };
    static const int o3[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
    for (int i = 0; i < 16; i++) {
        u32 t = a + MD4_F(b, c, d) + x[i];
        a = d; d = c; c = b; b = MD4_ROL(t, s1[i & 3]);
    }
    for (int i = 0; i < 16; i++) {
        u32 t = a + MD4_G(b, c, d) + x[(i & 3) * 4 + (i >> 2)] + 0x5A827999u;
        a = d; d = c; c = b; b = MD4_ROL(t, s2[i & 3]);
    }
    for (int i = 0; i < 16; i++) {
        u32 t = a + MD4_H(b, c, d) + x[o3[i]] + 0x6ED9EBA1u;
        a = d; d = c; c = b; b = MD4_ROL(t, s3[i & 3]);
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}
static void md4(const u8 *m, int len, u8 out[16]) {
    u32 st[4] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u };
    u8 blk[64];
    int i = 0;
    for (; i + 64 <= len; i += 64) md4_block(st, m + i);
    int r = len - i;
    memset(blk, 0, 64); memcpy(blk, m + i, r); blk[r] = 0x80;
    if (r >= 56) { md4_block(st, blk); memset(blk, 0, 64); }
    u64 bits = (u64)len * 8;
    for (int k = 0; k < 8; k++) blk[56 + k] = (u8)(bits >> (8 * k));
    md4_block(st, blk);
    for (int k = 0; k < 4; k++) { out[k * 4] = (u8)st[k]; out[k * 4 + 1] = (u8)(st[k] >> 8); out[k * 4 + 2] = (u8)(st[k] >> 16); out[k * 4 + 3] = (u8)(st[k] >> 24); }
}
static void hmac_md5(const u8 *key, int kl, const u8 *m, int ml, u8 out[16]) {
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_MD5), key, kl, m, ml, out);
}

/* ---------- UTF-8 <-> UTF-16LE ---------- */
static int u8_to_u16(const char *s, u8 *out, int cap, int upper, int bslash) {   /* rend les octets ecrits */
    int n = 0;
    const u8 *p = (const u8 *)s;
    while (*p && n + 4 <= cap) {
        u32 c = *p++;
        if (c >= 0xC0 && c < 0xE0 && *p) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        else if (c >= 0xE0 && c < 0xF0 && p[0] && p[1]) { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
        else if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        if (upper && c >= 'a' && c <= 'z') c -= 32;
        if (upper && c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 32;
        if (bslash && c == '/') c = '\\';
        if (c >= 0x10000) {
            c -= 0x10000;
            u32 h = 0xD800 | (c >> 10), l = 0xDC00 | (c & 0x3FF);
            out[n++] = (u8)h; out[n++] = (u8)(h >> 8); out[n++] = (u8)l; out[n++] = (u8)(l >> 8);
        } else { out[n++] = (u8)c; out[n++] = (u8)(c >> 8); }
    }
    return n;
}
static void u16_to_u8(const u8 *s, int bytes, char *out, int cap) {
    int n = 0;
    for (int i = 0; i + 1 < bytes && n < cap - 4; i += 2) {
        u32 c = (u32)s[i] | (u32)s[i + 1] << 8;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes) {
            u32 l = (u32)s[i + 2] | (u32)s[i + 3] << 8;
            c = 0x10000 + ((c - 0xD800) << 10) + (l - 0xDC00); i += 2;
        }
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { out[n++] = (char)(0xC0 | (c >> 6)); out[n++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out[n++] = (char)(0xE0 | (c >> 12)); out[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[n++] = (char)(0x80 | (c & 0x3F)); }
        else { out[n++] = (char)(0xF0 | (c >> 18)); out[n++] = (char)(0x80 | ((c >> 12) & 0x3F)); out[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[n++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[n] = 0;
}

/* ---------- connexion ---------- */
#define SMB_BUF (64 * 1024 + 512)
static void sle16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void sle32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void sle64(u8 *p, u64 v) { sle32(p, (u32)v); sle32(p + 4, (u32)(v >> 32)); }
static u32 srl16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 srl32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u64 srl64(const u8 *p) { return (u64)srl32(p) | (u64)srl32(p + 4) << 32; }
typedef struct {
    s32 fd;
    u64 mid, sid;
    u32 tid;
    u16 dialect;
    int sign;                                   /* 1 = signer les demandes */
    u8 key[16];                                 /* cle de session */
    u32 maxread;
    u8 *buf;                                    /* (reponse : en-tete NetBIOS retire) */
    int blen;
    u32 status;
    u64 used;                                   /* (derniere utilisation) */
} smb_t;
static smb_t smb_c = { -1, 0, 0, 0, 0, 0, { 0 }, 0, NULL, 0, 0, 0 };
static const char *smb_status_txt(u32 st) {
    switch (st) {
    case 0xC000006D: return "SMB : utilisateur ou mot de passe refus\xC3\xA9";
    case 0xC0000022: return "SMB : acc\xC3\xA8s refus\xC3\xA9";
    case 0xC00000CC: return "SMB : partage introuvable (nom du partage)";
    case 0xC0000034: case 0xC000003A: return "SMB : fichier ou dossier introuvable";
    case 0xC000006E: return "SMB : compte limit\xC3\xA9 (restrictions du compte)";
    case 0xC0000072: return "SMB : compte d\xC3\xA9sactiv\xC3\xA9";
    case 0xC00000BB: return "SMB : demande non prise en charge par le serveur";
    default: return "SMB : erreur du serveur";
    }
}
static void smb_close_c(void) {
    if (smb_c.fd >= 0) net_close(smb_c.fd);
    smb_c.fd = -1; smb_c.tid = 0; smb_c.sid = 0;
}
/* envoie une demande (en-tete rempli ici), attend la reponse de meme numero ; rend 1 si recue */
static int smb_rpc(u16 cmd, u8 *msg, int len) {               /* msg : 4 octets NetBIOS + 64 en-tete + corps */
    u8 *h = msg + 4;
    memset(h, 0, 64);
    h[0] = 0xFE; h[1] = 'S'; h[2] = 'M'; h[3] = 'B';
    sle16(h + 4, 64);
    sle16(h + 6, smb_c.dialect >= 0x0210 ? 1 : 0);              /* (CreditCharge) */
    sle16(h + 12, cmd);
    sle16(h + 14, 64);                                          /* (credits demandes) */
    u64 mid = smb_c.mid++;
    sle64(h + 24, mid);
    sle32(h + 36, smb_c.tid);
    sle64(h + 40, smb_c.sid);
    msg[0] = 0; msg[1] = (u8)(len >> 16); msg[2] = (u8)(len >> 8); msg[3] = (u8)len;
    if (smb_c.sign && cmd != 0x0000 && cmd != 0x0001) {        /* signature : HMAC-SHA256(cle, message), 16 octets */
        sle32(h + 16, srl32(h + 16) | 0x08);
        u8 mac[32];
        mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), smb_c.key, 16, h, len, mac);
        memcpy(h + 48, mac, 16);
    }
    if (!nfs_send(smb_c.fd, msg, len + 4)) { nfs_seterr("SMB : envoi impossible"); smb_close_c(); return 0; }
    for (;;) {
        u8 nb[4];
        int got = 0;
        while (got < 4) { int r = nfs_recv(smb_c.fd, nb + got, 4 - got, 15000); if (r <= 0) { nfs_seterr("Le serveur SMB ne r\xC3\xA9pond plus"); smb_close_c(); return 0; } got += r; }
        int n = nb[1] << 16 | nb[2] << 8 | nb[3];
        if (n < 64 || n > SMB_BUF) { nfs_seterr("SMB : r\xC3\xA9ponse invalide"); smb_close_c(); return 0; }
        got = 0;
        while (got < n) { int r = nfs_recv(smb_c.fd, smb_c.buf + got, n - got, 15000); if (r <= 0) { nfs_seterr("Le serveur SMB ne r\xC3\xA9pond plus"); smb_close_c(); return 0; } got += r; }
        smb_c.blen = n;
        const u8 *r = smb_c.buf;
        if (r[0] != 0xFE || r[1] != 'S') { nfs_seterr("SMB : r\xC3\xA9ponse invalide"); smb_close_c(); return 0; }
        if (srl64(r + 24) != mid) continue;                      /* (autre reponse) */
        smb_c.status = srl32(r + 8);
        if (smb_c.status == 0x00000103 && (srl32(r + 16) & 0x02)) continue;   /* STATUS_PENDING : la vraie reponse suit */
        return 1;
    }
}
/* ---------- NTLMSSP ---------- */
static int smb_session(const nfs_cfg_t *c, u8 *msg) {
    /* 1. NEGOTIATE */
    u8 *b = msg + 4 + 64;
    const u32 fl = 0x00000001 | 0x00000004 | 0x00000010 | 0x00000200 | 0x00008000 | 0x00080000 | 0x00800000 | 0x20000000 | 0x80000000u;
    u8 neg[32];
    memset(neg, 0, sizeof(neg));
    memcpy(neg, "NTLMSSP", 8); sle32(neg + 8, 1); sle32(neg + 12, fl);
    memset(b, 0, 24);
    sle16(b, 25); b[3] = 1;                                     /* (SecurityMode : signature possible) */
    sle16(b + 12, 64 + 24); sle16(b + 14, 32);
    memcpy(b + 24, neg, 32);
    if (!smb_rpc(0x0001, msg, 64 + 24 + 32)) return 0;
    if (smb_c.status != 0xC0000016) { nfs_seterr(smb_status_txt(smb_c.status)); return 0; }
    smb_c.sid = srl64(smb_c.buf + 40);
    const u8 *rb = smb_c.buf + 64;
    u32 so = srl16(rb + 4), sl = srl16(rb + 6);
    if (so + sl > (u32)smb_c.blen || sl < 48) { nfs_seterr("SMB : r\xC3\xA9ponse NTLM invalide"); return 0; }
    static u8 chal[2048];
    if (sl > sizeof(chal)) sl = sizeof(chal);
    memcpy(chal, smb_c.buf + so, sl);
    if (memcmp(chal, "NTLMSSP", 8) || srl32(chal + 8) != 2) { nfs_seterr("SMB : identification NTLM refus\xC3\xA9" "e"); return 0; }
    const u8 *sc = chal + 24;                                  /* (defi du serveur) */
    u32 tio = srl32(chal + 44), til = srl16(chal + 40);
    if (tio + til > sl) til = 0;
    const u8 *ti = chal + tio;
    /* 2. AUTHENTICATE (NTLMv2) */
    char user[64], dom[64] = "";
    snprintf(user, sizeof(user), "%s", c->user);
    char *bs = strchr(user, '\\');
    if (bs) { *bs = 0; snprintf(dom, sizeof(dom), "%s", user); memmove(user, bs + 1, strlen(bs + 1) + 1); }
    int anon = !user[0];
    u8 nth[16], ntowf[16], u16b[256], tmp[1400], ntresp[1500], lmresp[24], key[16];
    int ntlen = 0;
    memset(lmresp, 0, sizeof(lmresp));
    if (!anon) {
        int pl = u8_to_u16(c->pass, u16b, sizeof(u16b), 0, 0);
        md4(u16b, pl, nth);
        int ul = u8_to_u16(user, u16b, sizeof(u16b), 1, 0);
        ul += u8_to_u16(dom, u16b + ul, sizeof(u16b) - ul, 0, 0);
        hmac_md5(nth, 16, u16b, ul, ntowf);
        u64 ts = 0;                                            /* horodatage : celui du serveur (MsvAvTimestamp), sinon l'heure */
        for (u32 i = 0; i + 4 <= til;) {
            u32 id = srl16(ti + i), ln = srl16(ti + i + 2);
            if (id == 0) break;
            if (id == 7 && ln == 8 && i + 12 <= til) ts = srl64(ti + i + 4);
            i += 4 + ln;
        }
        if (!ts) ts = ((u64)time(NULL) + 11644473600ull) * 10000000ull;
        u8 cc[8];
        for (int i = 0; i < 8; i++) cc[i] = (u8)(lib_rand() >> 13);
        int tl = 0;
        if (til > sizeof(tmp) - 40) til = sizeof(tmp) - 40;
        tmp[tl++] = 1; tmp[tl++] = 1; tmp[tl++] = 0; tmp[tl++] = 0;
        sle32(tmp + tl, 0); tl += 4;
        sle64(tmp + tl, ts); tl += 8;
        memcpy(tmp + tl, cc, 8); tl += 8;
        sle32(tmp + tl, 0); tl += 4;
        memcpy(tmp + tl, ti, til); tl += til;
        sle32(tmp + tl, 0); tl += 4;
        u8 sct[1500];
        memcpy(sct, sc, 8); memcpy(sct + 8, tmp, tl);
        u8 proof[16];
        hmac_md5(ntowf, 16, sct, 8 + tl, proof);
        memcpy(ntresp, proof, 16); memcpy(ntresp + 16, tmp, tl);
        ntlen = 16 + tl;
        hmac_md5(ntowf, 16, proof, 16, key);                   /* cle de session (sans echange de cle) */
        u8 lm[16];
        memcpy(sct + 8, cc, 8);
        hmac_md5(ntowf, 16, sct, 16, lm);                      /* LMv2 */
        memcpy(lmresp, lm, 16); memcpy(lmresp + 16, cc, 8);
    }
    u8 du[128], uu[128];
    int dl = u8_to_u16(dom, du, sizeof(du), 0, 0), ul2 = u8_to_u16(user, uu, sizeof(uu), 0, 0);
    static u8 auth[1800];
    int lml = anon ? 0 : 24, off = 64;
    memset(auth, 0, 64);
    memcpy(auth, "NTLMSSP", 8); sle32(auth + 8, 3);
    #define NT_FIELD(o, l) do { sle16(auth + (o), (l)); sle16(auth + (o) + 2, (l)); sle32(auth + (o) + 4, off); } while (0)
    NT_FIELD(12, lml); memcpy(auth + off, lmresp, lml); off += lml;
    NT_FIELD(20, ntlen); memcpy(auth + off, ntresp, ntlen); off += ntlen;
    NT_FIELD(28, dl); memcpy(auth + off, du, dl); off += dl;
    NT_FIELD(36, ul2); memcpy(auth + off, uu, ul2); off += ul2;
    NT_FIELD(44, 0);
    NT_FIELD(52, 0);
    #undef NT_FIELD
    sle32(auth + 60, (srl32(chal + 20) & fl) | (anon ? 0x00000800 : 0));
    memset(b, 0, 24);
    sle16(b, 25); b[3] = 1;
    sle16(b + 12, 64 + 24); sle16(b + 14, off);
    memcpy(b + 24, auth, off);
    if (!smb_rpc(0x0001, msg, 64 + 24 + off)) return 0;
    if (smb_c.status != 0) { nfs_seterr(smb_status_txt(smb_c.status)); return 0; }
    u32 sflags = srl16(smb_c.buf + 64 + 2);
    memcpy(smb_c.key, key, 16);
    smb_c.sign = !anon && !(sflags & 0x03) && smb_c.sign;    /* (invite / anonyme : pas de signature possible) */
    return 1;
}
static int smb_connect(void) {
    if (smb_c.fd >= 0 && smb_c.tid) return 1;                  /* (connexion gardee ; refaite si coupee) */
    smb_close_c();
    const nfs_cfg_t *c = &nfs_cfg[NFS_SMB];
    if (!smb_c.buf) smb_c.buf = (u8 *)malloc(SMB_BUF);
    static u8 *msg = NULL;
    if (!msg) msg = (u8 *)malloc(4 + 64 + 1024 + 2048);
    if (!smb_c.buf || !msg) { nfs_seterr("M\xC3\xA9moire pleine"); return 0; }
    smb_c.fd = nfs_tcp(c->host, c->port > 0 ? c->port : 445);
    if (smb_c.fd < 0) return 0;
    smb_c.mid = 0; smb_c.sid = 0; smb_c.tid = 0; smb_c.sign = 0; smb_c.dialect = 0x0202;
    /* NEGOTIATE : 2.0.2 et 2.1 */
    u8 *b = msg + 4 + 64;
    memset(b, 0, 36 + 4);
    sle16(b, 36); sle16(b + 2, 2); sle16(b + 4, 1);               /* (2 dialectes ; signature possible) */
    for (int i = 0; i < 16; i++) b[12 + i] = (u8)(lib_rand() >> 9);
    sle16(b + 36, 0x0202); sle16(b + 38, 0x0210);
    if (!smb_rpc(0x0000, msg, 64 + 40)) return 0;
    if (smb_c.status != 0) { nfs_seterr(smb_c.status == 0xC00000BB ? "SMB : version non prise en charge par le serveur" : smb_status_txt(smb_c.status)); smb_close_c(); return 0; }
    const u8 *rb = smb_c.buf + 64;
    smb_c.dialect = (u16)srl16(rb + 4);
    if (smb_c.dialect != 0x0202 && smb_c.dialect != 0x0210) { nfs_seterr("SMB : version du serveur non prise en charge"); smb_close_c(); return 0; }
    smb_c.sign = (srl16(rb + 2) & 0x02) != 0;                   /* (signature exigee par le serveur) */
    smb_c.maxread = srl32(rb + 32);
    if (smb_c.maxread > 65536 || !smb_c.maxread) smb_c.maxread = 65536;
    if (!smb_session(c, msg)) { smb_close_c(); return 0; }
    /* TREE_CONNECT \\serveur\partage */
    char unc[160];
    snprintf(unc, sizeof(unc), "\\\\%s\\%s", c->host, c->share[0] == '/' || c->share[0] == '\\' ? c->share + 1 : c->share);
    memset(b, 0, 8);
    sle16(b, 9);
    int pl = u8_to_u16(unc, b + 8, 600, 0, 1);
    sle16(b + 4, 64 + 8); sle16(b + 6, pl);
    if (!smb_rpc(0x0003, msg, 64 + 8 + pl)) return 0;
    if (smb_c.status != 0) { nfs_seterr(smb_status_txt(smb_c.status)); smb_close_c(); return 0; }
    smb_c.tid = srl32(smb_c.buf + 36);
    smb_c.used = now_ms();
    return 1;
}
/* CREATE : ouvre un fichier ou un dossier ; rend 1 et l'identifiant (16 octets), taille, dossier */
static u8 smb_msg[4 + 64 + 1024];
static int smb_create(const char *rel, int want_dir, u8 fid[16], u64 *size, int *isdir) {
    const char *p = rel;
    while (*p == '/') p++;
    u8 *b = smb_msg + 4 + 64;
    memset(b, 0, 56);
    sle16(b, 57);
    sle32(b + 4, 2);                                            /* (impersonation) */
    sle32(b + 24, want_dir == 1 ? 0x00100081u : 0x00120089u);   /* (lire la liste / lire le fichier) */
    sle32(b + 32, 7);                                           /* (partage : lecture, ecriture, suppression) */
    sle32(b + 36, 1);                                           /* FILE_OPEN */
    sle32(b + 40, want_dir == 1 ? 0x01 : want_dir == 0 ? 0x40 : 0);
    int nl = u8_to_u16(p, b + 56, 900, 0, 1);
    sle16(b + 44, 64 + 56); sle16(b + 46, nl);
    if (!nl) b[56] = 0;
    for (int tries = 0; tries < 2; tries++) {
        if (!smb_connect()) return 0;
        sle16(b + 44, 64 + 56);
        if (smb_rpc(0x0005, smb_msg, 64 + 56 + (nl ? nl : 1))) break;
        if (tries) return 0;                                   /* (connexion coupee : on la refait une fois) */
    }
    if (smb_c.status != 0) { nfs_seterr(smb_status_txt(smb_c.status)); if (smb_c.status == 0xC0000203 || smb_c.status == 0xC000035C) smb_close_c(); return 0; }
    const u8 *r = smb_c.buf + 64;
    if (size) *size = srl64(r + 48);
    if (isdir) *isdir = (srl32(r + 56) & 0x10) != 0;
    memcpy(fid, r + 64, 16);
    smb_c.used = now_ms();
    return 1;
}
static void smb_close_fid(const u8 fid[16]) {
    if (smb_c.fd < 0) return;
    u8 *b = smb_msg + 4 + 64;
    memset(b, 0, 24);
    sle16(b, 24);
    memcpy(b + 8, fid, 16);
    smb_rpc(0x0006, smb_msg, 64 + 24);
}
static int smb_list(const char *rel, nfs_ent_t **out, int *n) {
    u8 fid[16];
    *out = NULL; *n = 0;
    if (!smb_create(rel, 1, fid, NULL, NULL)) return 0;
    int cap = 0, first = 1;
    for (int guard = 0; guard < 1000; guard++) {
        u8 *b = smb_msg + 4 + 64;
        memset(b, 0, 32);
        sle16(b, 33);
        b[2] = 0x01;                                           /* FileDirectoryInformation */
        b[3] = first ? 0x01 : 0;                               /* (RESTART_SCANS) */
        memcpy(b + 8, fid, 16);
        sle16(b + 24, 64 + 32); sle16(b + 26, 2);
        sle32(b + 28, smb_c.maxread < 65536 ? smb_c.maxread : 65536);
        b[32] = '*'; b[33] = 0;
        first = 0;
        if (!smb_rpc(0x000E, smb_msg, 64 + 34)) { free(*out); *out = NULL; *n = 0; return 0; }
        if (smb_c.status == 0x80000006) break;                 /* STATUS_NO_MORE_FILES */
        if (smb_c.status != 0) { nfs_seterr(smb_status_txt(smb_c.status)); smb_close_fid(fid); free(*out); *out = NULL; *n = 0; return 0; }
        const u8 *r = smb_c.buf + 64;
        u32 bo = srl16(r + 2), bl = srl32(r + 4);
        if (bo + bl > (u32)smb_c.blen) break;
        const u8 *e = smb_c.buf + bo;
        u32 i = 0;
        for (;;) {
            if (i + 64 > bl) break;
            u32 next = srl32(e + i), fnl = srl32(e + i + 60);
            if (i + 64 + fnl > bl) break;
            char nm[NFS_NAME];
            u16_to_u8(e + i + 64, (int)fnl, nm, sizeof(nm));
            if (strcmp(nm, ".") && strcmp(nm, "..")) {
                if (*n >= cap) { int nc = cap ? cap * 2 : 64; nfs_ent_t *ne = (nfs_ent_t *)realloc(*out, sizeof(nfs_ent_t) * nc); if (!ne) { smb_close_fid(fid); return 1; } *out = ne; cap = nc; }
                nfs_ent_t *x = &(*out)[(*n)++];
                memset(x, 0, sizeof(*x));
                snprintf(x->name, sizeof(x->name), "%s", nm);
                x->dir = (srl32(e + i + 56) & 0x10) != 0;
                u64 sz = srl64(e + i + 40);
                x->size = sz > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)sz;
                u64 wt = srl64(e + i + 24);
                x->mtime = wt > 116444736000000000ull ? (u32)((wt - 116444736000000000ull) / 10000000ull) : 0;
            }
            if (!next) break;
            i += next;
        }
        service_audio();
    }
    smb_close_fid(fid);
    return 1;
}
static int smb_stat(const char *rel, int *dir, u32 *size) {
    u8 fid[16];
    u64 sz = 0;
    if (!smb_create(rel, 2, fid, &sz, dir)) return 0;
    smb_close_fid(fid);
    *size = sz > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)sz;
    return 1;
}
/* fichier ouvert : lectures de 64 Ko gardees (la musique lit par 8 Ko) */
typedef struct { u8 fid[16]; u32 size, pos, cpos; int clen; u8 *cache; u64 sid; char rel[NAME_LEN]; } smb_file_t;
static int smb_fopen(smb_file_t *h, const char *rel) {
    memset(h, 0, sizeof(*h));
    snprintf(h->rel, sizeof(h->rel), "%s", rel);
    u64 sz = 0;
    int dir = 0;
    if (!smb_create(rel, 0, h->fid, &sz, &dir)) return 0;
    if (dir) { smb_close_fid(h->fid); nfs_seterr("C'est un dossier"); return 0; }
    h->size = sz > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)sz;
    h->sid = smb_c.sid;
    h->cache = (u8 *)malloc(65536);
    if (!h->cache) { smb_close_fid(h->fid); nfs_seterr("M\xC3\xA9moire pleine"); return 0; }
    return 1;
}
static int smb_fread(smb_file_t *h, u8 *dst, int len) {
    int got = 0;
    while (got < len && h->pos < h->size) {
        if (h->clen > 0 && h->pos >= h->cpos && h->pos < h->cpos + (u32)h->clen) {
            int k = (int)(h->cpos + h->clen - h->pos);
            if (k > len - got) k = len - got;
            memcpy(dst + got, h->cache + (h->pos - h->cpos), k);
            got += k; h->pos += k;
            continue;
        }
        int ok = 0;
        for (int tries = 0; tries < 2 && !ok; tries++) {
            if (smb_c.fd < 0 || smb_c.sid != h->sid) {             /* (connexion coupee ou refaite : on rouvre le fichier) */
                u64 sz = 0; int dir = 0;
                if (!smb_create(h->rel, 0, h->fid, &sz, &dir)) return got ? got : -1;
                h->sid = smb_c.sid;
            }
            u32 want = smb_c.maxread < 65536 ? smb_c.maxread : 65536;
            u8 *b = smb_msg + 4 + 64;
            memset(b, 0, 49);
            sle16(b, 49); b[2] = 0x50;
            sle32(b + 4, want);
            sle64(b + 8, h->pos);
            memcpy(b + 16, h->fid, 16);
            ok = smb_rpc(0x0008, smb_msg, 64 + 49);
            if (ok && smb_c.status == 0xC000035C) { smb_close_c(); ok = 0; }   /* (session expiree) */
        }
        if (!ok) return got ? got : -1;
        if (smb_c.status == 0xC0000011) break;                  /* fin du fichier */
        if (smb_c.status != 0) { nfs_seterr(smb_status_txt(smb_c.status)); return got ? got : -1; }
        const u8 *r = smb_c.buf + 64;
        u32 dofs = r[2], dl = srl32(r + 4);
        if (dofs + dl > (u32)smb_c.blen || !dl) break;
        if (dl > 65536) dl = 65536;
        memcpy(h->cache, smb_c.buf + dofs, dl);
        h->cpos = h->pos; h->clen = (int)dl;
        smb_c.used = now_ms();
    }
    return got;
}
static void smb_fclose(smb_file_t *h) {
    if (smb_c.sid == h->sid) smb_close_fid(h->fid);
    free(h->cache); h->cache = NULL;
}

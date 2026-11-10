/* GC Station v393 : RetroAchievements (succes du site retroachievements.org), cote console.
   La console ne parle jamais au site : c'est l'appli du telephone qui le fait (connexion au compte, succes du jeu,
   deblocages), avec la memoire du jeu que Pulse lui envoie a chaque image (le direct, "GCMS"). La console calcule
   seulement l'EMPREINTE du jeu, qui dit au site de quel jeu il s'agit, et la donne a l'appli dans le message de
   lancement ("GCSG1", 8e champ).
   Empreinte : la meme que rcheevos (bibliotheque de RetroAchievements, licence MIT : rc_hash_gamecube) : MD5 de
   l'en-tete du disque jusqu'a la fin de l'apploader (0x2440 + 0x20 + corps + fin de l'apploader, 1 Mo au plus), puis
   des 18 sections de main.dol (7 de code, 11 de donnees) dans l'ordre (a la position ecrite dans l'en-tete de main.dol,
   comptee depuis le debut du disque, comme rcheevos). Lue par gd_read (.iso / .gcm / .ciso : le
   contenu du vrai disque). Gardee dans GCD "/ra-empreintes.txt" (taille du fichier + chemin) : calculee une seule fois
   par jeu (quelques secondes : ~4 Mo lus). Seulement quand l'appli peut lire la memoire du jeu (na_mem_on). */
#include "mbedtls/md.h"
#define RA_CACHE GCD "/ra-empreintes.txt"
static int ra_md5_file(const char *path, char out[33]) {
    gdisc_t d;
    if (!gd_open(&d, path)) return 0;
    u8 h[0x440], q[8];
    int ok = gd_read(&d, 0, h, sizeof(h)) && be32(h + 0x1C) == 0xC2339F3Du && gd_read(&d, 0x2440 + 0x14, q, 8);
    if (!ok) { gd_close(&d); return 0; }
    u32 hs = 0x2440u + 0x20u + be32(q) + be32(q + 4);
    if (hs > (1u << 20) || hs < 0x2460u) hs = 1u << 20;
    u32 dol = be32(h + 0x420);
    u8 dh[0xD8];
    const u32 CH = 64u << 10;
    u8 *b = (u8 *)malloc(CH);
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    ok = b && dol && gd_read(&d, dol, dh, sizeof(dh)) && mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0) == 0 && mbedtls_md_starts(&md) == 0;
    for (u32 o = 0; ok && o < hs; o += CH) {             /* en-tete + apploader */
        u32 n = hs - o < CH ? hs - o : CH;
        ok = gd_read(&d, o, b, n) && mbedtls_md_update(&md, b, n) == 0;
        service_audio();
    }
    for (int i = 0; ok && i < 18; i++) {                  /* sections de main.dol */
        u32 off = be32(dh + i * 4), sz = be32(dh + 0x90 + i * 4);
        for (u32 o = 0; ok && o < sz; o += CH) {
            u32 n = sz - o < CH ? sz - o : CH;
            ok = gd_read(&d, off + o, b, n) && mbedtls_md_update(&md, b, n) == 0;   /* (comme rcheevos : position de la
                                                                                     section lue depuis le debut du disque) */
            service_audio();
        }
    }
    u8 r[16];
    if (ok) ok = mbedtls_md_finish(&md, r) == 0;
    mbedtls_md_free(&md);
    free(b);
    gd_close(&d);
    if (ok) for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", r[i]);
    return ok;
}
/* empreinte d'un jeu (cache d'abord) ; "" si illisible */
static void ra_hash_game(const char *path, char out[33]) {
    out[0] = 0;
    struct stat st;
    if (!path || stat(path, &st) != 0) return;
    unsigned long long sz = (unsigned long long)st.st_size;
    char l[600];
    FILE *f = fopen(RA_CACHE, "r");
    if (f) {
        while (fgets(l, sizeof(l), f)) {                 /* "empreinte taille chemin" */
            char hx[40]; unsigned long long s2; int p = 0;
            if (sscanf(l, "%39s %llu %n", hx, &s2, &p) == 2 && p > 0 && strlen(hx) == 32 && s2 == sz) {
                char *e = l + p; e[strcspn(e, "\r\n")] = 0;
                if (!strcmp(e, path)) { memcpy(out, hx, 33); fclose(f); return; }
            }
        }
        fclose(f);
    }
    if (!ra_md5_file(path, out)) { out[0] = 0; return; }
    f = fopen(RA_CACHE, "a");
    if (f) { fprintf(f, "%s %llu %s\n", out, sz, path); fclose(f); }
}
/* juste avant le message de lancement a l'appli (jeux_cfg.c) */
static void ra_prepare(int k) {
    ra_cur_hash[0] = 0;
    if (k < 0 || k >= gl_n || !app_envoi || !na_mem_on()) return;
    ra_hash_game(gl[k].path, ra_cur_hash);
}

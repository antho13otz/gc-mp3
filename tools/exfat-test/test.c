/* v326 : banc d'essai sur PC du montage exFAT (source/exfat_sd.c + FatFs) : une image de carte exFAT est creee,
   des fichiers fragmentes y sont ecrits (noms longs, accents), puis on verifie la lecture, les dossiers, le
   renommage, et surtout exf_extents (les secteurs que Pulse lira pendant le jeu) contre l'image brute.
   Usage : sh tools/exfat-test/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
typedef uint32_t u32; typedef uint64_t u64; typedef uint8_t u8;
static void fatUnmount(const char *n) { (void)n; }
#include "../../source/exfat_sd.c"

static FILE *img; static int fails = 0;
#define OK(c, m) do { if (!(c)) { printf("ECHEC : %s\n", m); fails++; } else printf("ok   : %s\n", m); } while (0)
static bool rd(u32 s, u32 n, void *b) { return fseek(img, (long)s * 512, SEEK_SET) == 0 && fread(b, 512, n, img) == n; }
static bool wr(u32 s, u32 n, const void *b) { return fseek(img, (long)s * 512, SEEK_SET) == 0 && fwrite(b, 512, n, img) == n; }
static u8 motif(u32 f, u64 i) { return (u8)(f * 31 + i * 7 + (i >> 9)); }
static u64 got = 0; static u32 cur_f; static int runs = 0;
static int cb(void *ctx, u32 sec, u32 n) {
    (void)ctx; static u8 b[512]; runs++;
    for (u32 k = 0; k < n; k++) {
        if (!rd(sec + k, 1, b)) return -1;
        for (int j = 0; j < 512; j++) { u64 i = got * 512 + j; if (i >= *(u64 *)ctx) break; if (b[j] != motif(cur_f, i)) { printf("  octet %llu faux\n", (unsigned long long)i); fails++; return -1; } }
        got++;
    }
    return 0;
}
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "exfat.img";
    img = fopen(path, "w+b"); if (!img) return 1;
    const u32 nsec = 128u * 2048u;                                  /* 128 Mo */
    fseek(img, (long)nsec * 512 - 1, SEEK_SET); fputc(0, img); fflush(img);
    exf_rd = rd; exf_wr = wr; exf_nsec = nsec;
    static BYTE work[64 * 1024];
    MKFS_PARM opt = { FM_EXFAT, 0, 0, 0, 32768 };
    FRESULT fr = f_mkfs("", &opt, work, sizeof(work));
    OK(fr == FR_OK, "image exFAT creee (128 Mo, clusters de 32 Ko)");
    exf_rd = NULL; exf_wr = NULL;
    OK(exf_mount_io(rd, wr), "montage par FatFs");
    OK(!strcmp(exf_type(), "exFAT"), "systeme vu : exFAT");
    OK(f_mkdir("/gc-mp3") == FR_OK && f_mkdir("/gc-mp3/r\xC3\xA9glages") == FR_OK, "dossiers (avec accent)");
    /* trois jeux ecrits en alternance par blocs : fichiers fragmentes */
    static FIL f[3]; const char *nom[3] = { "/Jeu tr\xC3\xA8s long nom A.iso", "/gc-mp3/B.iso", "/gc-mp3/r\xC3\xA9glages/C.iso" };
    u64 taille[3] = { 9u * 1000 * 1000 + 123, 5u * 1000 * 1000 + 7, 3u * 1000 * 1000 };
    int okw = 1;
    for (int i = 0; i < 3; i++) okw &= f_open(&f[i], nom[i], FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
    static u8 buf[100000]; u64 pos[3] = { 0, 0, 0 };
    for (int tour = 0; okw && tour < 200; tour++)
        for (int i = 0; i < 3; i++) {
            u64 n = taille[i] - pos[i]; if (n > 70000) n = 70000; if (!n) continue;
            for (u64 j = 0; j < n; j++) buf[j] = motif(i, pos[i] + j);
            UINT w = 0; okw &= f_write(&f[i], buf, (UINT)n, &w) == FR_OK && w == n; pos[i] += n;
        }
    for (int i = 0; i < 3; i++) okw &= f_close(&f[i]) == FR_OK;
    OK(okw, "3 fichiers ecrits en alternance (fragmentes)");
    for (int i = 0; i < 3; i++) {
        got = 0; runs = 0; cur_f = (u32)i; u64 sz = taille[i];
        int e = exf_extents(nom[i], cb, &sz);
        char m[128]; snprintf(m, sizeof(m), "morceaux du fichier %d sur la carte = son contenu (%llu secteurs, %d morceaux)", i, (unsigned long long)got, runs);
        OK(e == 0 && got == (sz + 511) / 512 && runs > 1, m);
    }
    /* lecture par FatFs, a un decalage quelconque */
    FIL r; UINT g = 0; int okr = f_open(&r, nom[1], FA_READ) == FR_OK && f_lseek(&r, 1234567) == FR_OK && f_read(&r, buf, 50000, &g) == FR_OK && g == 50000;
    for (UINT j = 0; okr && j < g; j++) if (buf[j] != motif(1, 1234567 + j)) okr = 0;
    f_close(&r);
    OK(okr, "lecture au milieu d'un fichier");
    FF_DIR d; FILINFO fi; int n = 0, acc = 0;
    if (f_opendir(&d, "/gc-mp3") == FR_OK) { while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) { n++; if (!strcmp(fi.fname, "r\xC3\xA9glages")) acc = 1; } f_closedir(&d); }
    OK(n == 2 && acc, "contenu d'un dossier (noms en UTF-8)");
    OK(f_rename("/gc-mp3/B.iso", "/gc-mp3/B2.iso") == FR_OK && f_stat("/gc-mp3/B2.iso", &fi) == FR_OK && fi.fsize == taille[1], "renommage");
    OK(f_unlink("/gc-mp3/B2.iso") == FR_OK && f_stat("/gc-mp3/B2.iso", &fi) == FR_NO_FILE, "effacement");
    DWORD fre = 0; FATFS *fs; OK(f_getfree("", &fre, &fs) == FR_OK && fre > 0, "place libre");
    /* demontage puis remontage : tout est encore la */
    exf_umount();
    OK(exf_mount_io(rd, wr) && f_stat(nom[0], &fi) == FR_OK && fi.fsize == taille[0], "remontage : fichiers toujours la");
    exf_umount(); fclose(img);
    /* v326 : carte de 1 To en exFAT (image creuse) : montage, place libre, et un jeu tout au bout de la carte
       (secteurs proches de 1 953 000 000 : les calculs de secteurs en 32 bits tiennent) */
    if (argc > 2) {
        img = fopen(argv[2], "w+b"); if (!img) return 1;
        const u32 big = 1953125000u;                                     /* (1 To = 10^12 octets) */
        fseek(img, (long)big * 512 - 1, SEEK_SET); fputc(0, img); fflush(img);
        exf_rd = rd; exf_wr = wr; exf_nsec = big;
        MKFS_PARM o2 = { FM_EXFAT, 0, 0, 0, 0 };                          /* (taille des clusters choisie par FatFs) */
        OK(f_mkfs("", &o2, work, sizeof(work)) == FR_OK, "1 To : image exFAT creee");
        exf_rd = NULL; exf_wr = NULL;
        OK(exf_mount_io(rd, wr), "1 To : montage");
        char m[160];
        snprintf(m, sizeof(m), "1 To : %u clusters de %u Ko", (unsigned)(exf_fs.n_fatent - 2), (unsigned)(exf_fs.csize / 2));
        OK(exf_fs.n_fatent > 1000, m);
        DWORD fr2 = 0; FATFS *fs2; clock_t c0 = clock();
        int okf = f_getfree("", &fr2, &fs2) == FR_OK && fr2 > exf_fs.n_fatent - 100;
        snprintf(m, sizeof(m), "1 To : place libre lue (%.0f Go, %.2f s sur PC)", (double)fr2 * exf_fs.csize * 512 / 1e9, (double)(clock() - c0) / CLOCKS_PER_SEC);
        OK(okf, m);
        exf_fs.last_clst = exf_fs.n_fatent - 5000;                       /* (le prochain fichier : au bout de la carte) */
        static FIL g; UINT w; int okw2 = f_open(&g, "/Jeu au bout.iso", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
        u64 tg = 40u * 1000 * 1000;
        for (u64 at = 0; okw2 && at < tg; at += sizeof(buf)) {
            u64 n = tg - at < sizeof(buf) ? tg - at : sizeof(buf);
            for (u64 j = 0; j < n; j++) buf[j] = motif(7, at + j);
            okw2 = f_write(&g, buf, (UINT)n, &w) == FR_OK && w == n;
        }
        okw2 &= f_close(&g) == FR_OK;
        OK(okw2, "1 To : 40 Mo ecrits au bout de la carte");
        got = 0; runs = 0; cur_f = 7; u64 sz = tg; u32 first = 0;
        int e = exf_extents("/Jeu au bout.iso", cb, &sz);
        { static FIL h; DWORD t2[8]; h.cltbl = NULL; if (f_open(&h, "/Jeu au bout.iso", FA_READ) == FR_OK) { h.cltbl = t2; t2[0] = 8; if (f_lseek(&h, CREATE_LINKMAP) == FR_OK) first = (u32)(exf_fs.database + (LBA_t)(t2[2] - 2) * exf_fs.csize); h.cltbl = NULL; f_close(&h); } }
        snprintf(m, sizeof(m), "1 To : morceaux du fichier = son contenu (1er secteur %u)", first);
        OK(e == 0 && got == (tg + 511) / 512 && first > 1900000000u, m);
        exf_umount(); fclose(img); remove(argv[2]);
    }
    printf(fails ? "DES TESTS ONT ECHOUE (%d)\n" : "TOUT OK\n", fails);
    return fails != 0;
}

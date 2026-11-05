/* GC Station PC : un faux Dolphin, pour l'essai automatique sur Windows (.github/workflows/windows.yml).
   Lance par GC Station a la place de Dolphin.exe ; comme le vrai, il se connecte au "tapserver" donne par
   -C Dolphin.Core.BBA_TAPSERVER_DESTINATION=127.0.0.1:port, puis echange des trames Ethernet (2 octets de longueur,
   petit-boutiste, puis la trame). Il en envoie une toutes les 100 ms pendant 20 s, compte celles venues d'un AUTRE faux
   Dolphin (par l'autre GC Station, par Internet simule), et ecrit resultat.txt a cote de lui. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SOCKET s = INVALID_SOCKET;
static volatile int recues = 0, autres = 0, moi = 0;
static int recv_all(char *d, int n) { while (n > 0) { int r = recv(s, d, n, 0); if (r <= 0) return 0; d += r; n -= r; } return 1; }
static DWORD WINAPI lire(LPVOID a) {
    (void)a;
    char b[2048];
    for (;;) {
        unsigned char h[2];
        if (!recv_all((char *)h, 2)) return 0;
        int n = h[0] | h[1] << 8;
        if (n > (int)sizeof(b) || !recv_all(b, n)) return 0;
        recues++;
        int id = -1;
        if (n >= 14 + 16 && !memcmp(b + 14, "FAUX-DOLPHIN ", 13)) id = atoi(b + 27);
        if (id >= 0 && id != moi) autres++;
    }
}
int main(int argc, char **argv) {
    char jeu[1024] = "", dest[128] = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-e") && i + 1 < argc) snprintf(jeu, sizeof(jeu), "%s", argv[++i]);
        const char *k = strstr(argv[i], "BBA_TAPSERVER_DESTINATION=");
        if (k) snprintf(dest, sizeof(dest), "%s", k + 26);
    }
    char dir[MAX_PATH]; GetModuleFileNameA(NULL, dir, MAX_PATH); char *sl = strrchr(dir, '\\'); if (sl) *sl = 0;
    char res[MAX_PATH + 16]; snprintf(res, sizeof(res), "%s\\resultat.txt", dir);
    int envoyees = 0, connecte = 0;
    char *col = strrchr(dest, ':');
    if (dest[0] && col) {
        WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
        moi = atoi(col + 1) % 1000;
        struct sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_port = htons((unsigned short)atoi(col + 1)); *col = 0; a.sin_addr.s_addr = inet_addr(dest); *col = ':';
        for (int t = 0; t < 50 && !connecte; t++) {
            s = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(s, (struct sockaddr *)&a, sizeof(a)) == 0) connecte = 1; else { closesocket(s); Sleep(100); }
        }
        if (connecte) {
            CreateThread(NULL, 0, lire, NULL, 0, NULL);
            for (int i = 0; i < 200; i++) {
                unsigned char f[2 + 60]; memset(f, 0, sizeof(f));
                f[0] = 60; f[1] = 0;
                memset(f + 2, 0xFF, 6);                            /* (a tous) */
                f[8] = 0x00; f[9] = 0x09; f[10] = 0xBF; f[11] = 0; f[12] = 0; f[13] = (unsigned char)moi;
                f[14] = 0x88; f[15] = 0xB5;                         /* (type d'essai) */
                snprintf((char *)f + 16, 40, "FAUX-DOLPHIN %d %d", moi, i);
                if (send(s, (const char *)f, sizeof(f), 0) == (int)sizeof(f)) envoyees++;
                Sleep(100);
            }
            Sleep(1500);
        }
    }
    FILE *o = fopen(res, "w");
    if (o) { fprintf(o, "jeu=%s\nreseau=%s\nconnecte=%d\nenvoyees=%d\nrecues=%d\nde_l_autre=%d\n", jeu, dest, connecte, envoyees, recues, autres); fclose(o); }
    return 0;
}

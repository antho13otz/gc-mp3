/* GC Station PC (Windows) : les jeux se lancent dans Dolphin (l'emulateur, installe a part), et le jeu en reseau local
   (LAN : Mario Kart Double Dash, Kirby Air Ride...) passe par Internet.
   - Dolphin : trouve a cote de GC Station (dossier Dolphin, Dolphin-x64), dans Program Files, ou dans dolphin.txt (le
     chemin de Dolphin.exe) ; lance avec le jeu (-e) et son adaptateur reseau en mode "tapserver" : Dolphin se connecte
     en TCP a GC Station (127.0.0.1:51241) et echange ses trames Ethernet, chacune precedee de sa longueur (2 octets,
     petit-boutiste). Seul ce protocole est utilise (aucun code de Dolphin repris).
   - Internet : les trames partent en UDP (port 51240) vers l'autre GC Station PC (adresse et code du Salon par Internet),
     au format de tools/lan-relais.py ("GCLR1" + 4 octets de la partie + trame ; partie = SHA-256 du code, 4 octets) :
     un relais lan-relais.py peut aussi servir. Celui qui heberge ouvre le port UDP 51240 de sa box ; il renvoie a
     chaque machine qui lui a parle (comme le relais). Signe de vie toutes les 5 s (la box garde le passage ouvert). */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef unsigned char u8; typedef unsigned int u32;
int mbedtls_sha256(const unsigned char *input, size_t ilen, unsigned char *output, int is224);
const wchar_t *pcw_exe_dir(void);
const wchar_t *pcw_sd_dir(void);

#define TAP_PORT 51241
#define LAN_PORT 51240
static HANDLE dol_proc = NULL;
static CRITICAL_SECTION lk;
static SOCKET tap_ls = INVALID_SOCKET, tap_c = INVALID_SOCKET, lan_u = INVALID_SOCKET;
static u8 room[4];
static int lan_on = 0;
static struct { struct sockaddr_in a; DWORD t; } peer[8];
static int npeer = 0;
static struct sockaddr_in ami;
static int ami_ok = 0;
static volatile u32 n_out = 0, n_in = 0;

void pcw_dolphin_init(void) { InitializeCriticalSection(&lk); }
void pcw_dolphin_stop(void) { if (dol_proc) { TerminateProcess(dol_proc, 0); CloseHandle(dol_proc); dol_proc = NULL; } }

static int find_dolphin(wchar_t *o) {
    const wchar_t *ex = pcw_exe_dir();
    wchar_t t[MAX_PATH];
    _snwprintf(t, MAX_PATH, L"%ls\\dolphin.txt", ex);     /* (le chemin donne par l'utilisateur) */
    FILE *f = _wfopen(t, L"rb");
    if (f) {
        char l[1024] = "";
        if (fgets(l, sizeof(l), f)) {
            l[strcspn(l, "\r\n")] = 0;
            char *s = l; if (!strncmp(s, "\xEF\xBB\xBF", 3)) s += 3;
            if (*s == '"') { s++; s[strcspn(s, "\"")] = 0; }
            MultiByteToWideChar(CP_UTF8, 0, s, -1, o, MAX_PATH);
            if (GetFileAttributesW(o) != INVALID_FILE_ATTRIBUTES) { fclose(f); return 1; }
        }
        fclose(f);
    }
    const char *e = getenv("GCSTATION_DOLPHIN");
    if (e && *e) { MultiByteToWideChar(CP_UTF8, 0, e, -1, o, MAX_PATH); if (GetFileAttributesW(o) != INVALID_FILE_ATTRIBUTES) return 1; }
    static const wchar_t *const sub[] = { L"Dolphin\\Dolphin.exe", L"Dolphin-x64\\Dolphin.exe", L"Dolphin.exe" };
    for (int i = 0; i < 3; i++) { _snwprintf(o, MAX_PATH, L"%ls\\%ls", ex, sub[i]); if (GetFileAttributesW(o) != INVALID_FILE_ATTRIBUTES) return 1; }
    static const wchar_t *const env[] = { L"ProgramFiles", L"ProgramW6432", L"ProgramFiles(x86)", L"LOCALAPPDATA" };
    for (int i = 0; i < 4; i++) {
        wchar_t b[MAX_PATH];
        if (!GetEnvironmentVariableW(env[i], b, MAX_PATH)) continue;
        for (int j = 0; j < 2; j++) {
            _snwprintf(o, MAX_PATH, i == 3 ? L"%ls\\Programs\\%ls" : L"%ls\\%ls", b, sub[j]);
            if (GetFileAttributesW(o) != INVALID_FILE_ATTRIBUTES) return 1;
        }
    }
    return 0;
}

/* ---------- le reseau local de Dolphin <-> Internet ---------- */
static void peer_seen(const struct sockaddr_in *a) {
    DWORD now = GetTickCount();
    for (int i = 0; i < npeer; i++) if (peer[i].a.sin_addr.s_addr == a->sin_addr.s_addr && peer[i].a.sin_port == a->sin_port) { peer[i].t = now; return; }
    int k = npeer < 8 ? npeer++ : 0;
    for (int i = 0; i < npeer; i++) if (now - peer[i].t > now - peer[k].t) k = i;
    peer[k].a = *a; peer[k].t = now;
    char ip[32]; inet_ntop(AF_INET, (void *)&a->sin_addr, ip, sizeof(ip));
    printf("[dolphin] l'autre cote : %s:%d\n", ip, ntohs(a->sin_port));
}
static void lan_send(const u8 *d, int n) {                /* vers l'ami, et tous ceux qui nous ont parle depuis 30 s */
    DWORD now = GetTickCount();
    int sent_ami = 0;
    EnterCriticalSection(&lk);
    for (int i = 0; i < npeer; i++) {
        if (now - peer[i].t > 30000) continue;
        sendto(lan_u, (const char *)d, n, 0, (struct sockaddr *)&peer[i].a, sizeof(peer[i].a));
        if (ami_ok && peer[i].a.sin_addr.s_addr == ami.sin_addr.s_addr && peer[i].a.sin_port == ami.sin_port) sent_ami = 1;
    }
    if (ami_ok && !sent_ami) sendto(lan_u, (const char *)d, n, 0, (struct sockaddr *)&ami, sizeof(ami));
    LeaveCriticalSection(&lk);
}
static int send_all(SOCKET s, const u8 *d, int n) { while (n > 0) { int r = send(s, (const char *)d, n, 0); if (r <= 0) return 0; d += r; n -= r; } return 1; }
static DWORD WINAPI lan_rx(LPVOID a) {                    /* Internet -> Dolphin */
    (void)a;
    static u8 b[2048], o[2048];
    for (;;) {
        struct sockaddr_in f; int fl = sizeof(f);
        int r = recvfrom(lan_u, (char *)b, sizeof(b), 0, (struct sockaddr *)&f, &fl);
        if (r < 0) { Sleep(5); continue; }
        if (r < 9 || memcmp(b, "GCLR1", 5) || memcmp(b + 5, room, 4)) continue;
        EnterCriticalSection(&lk); peer_seen(&f); LeaveCriticalSection(&lk);
        if (r == 9) continue;                              /* (signe de vie) */
        int n = r - 9;
        if (n < 14 || n > 1600) continue;
        o[0] = (u8)n; o[1] = (u8)(n >> 8); memcpy(o + 2, b + 9, n);
        EnterCriticalSection(&lk);
        SOCKET c = tap_c;
        LeaveCriticalSection(&lk);
        if (c != INVALID_SOCKET) { send_all(c, o, n + 2); n_in++; }
    }
    return 0;
}
static DWORD WINAPI lan_alive(LPVOID a) {
    (void)a;
    for (;;) {
        u8 m[9]; memcpy(m, "GCLR1", 5); memcpy(m + 5, room, 4);
        lan_send(m, 9);
        Sleep(5000);
    }
    return 0;
}
static int recv_all(SOCKET s, u8 *d, int n) { while (n > 0) { int r = recv(s, (char *)d, n, 0); if (r <= 0) return 0; d += r; n -= r; } return 1; }
static DWORD WINAPI tap_srv(LPVOID a) {                   /* Dolphin -> Internet */
    (void)a;
    static u8 b[2048 + 9];
    for (;;) {
        SOCKET c = accept(tap_ls, NULL, NULL);
        if (c == INVALID_SOCKET) { Sleep(50); continue; }
        int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
        printf("[dolphin] adaptateur reseau de Dolphin branche\n");
        EnterCriticalSection(&lk); if (tap_c != INVALID_SOCKET) closesocket(tap_c); tap_c = c; LeaveCriticalSection(&lk);
        for (;;) {
            u8 h[2];
            if (!recv_all(c, h, 2)) break;
            int n = h[0] | h[1] << 8;
            if (n > 2048) break;
            if (!recv_all(c, b + 9, n)) break;
            if (n < 14 || !lan_on) continue;
            memcpy(b, "GCLR1", 5); memcpy(b + 5, room, 4);
            lan_send(b, n + 9); n_out++;
        }
        printf("[dolphin] adaptateur reseau de Dolphin debranche (%u trames envoyees, %u recues)\n", n_out, n_in);
        EnterCriticalSection(&lk); if (tap_c == c) tap_c = INVALID_SOCKET; LeaveCriticalSection(&lk);
        closesocket(c);
    }
    return 0;
}
static int net_start(const char *amiaddr, const char *code) {
    if (tap_ls != INVALID_SOCKET) return 1;
    unsigned char h[32];
    char up[64]; int n = 0;
    for (const char *p = code; *p && n < 63; p++) up[n++] = *p;
    up[n] = 0;
    mbedtls_sha256((const unsigned char *)up, (size_t)n, h, 0);
    memcpy(room, h, 4);
    tap_ls = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(TAP_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (tap_ls == INVALID_SOCKET || bind(tap_ls, (struct sockaddr *)&a, sizeof(a)) || listen(tap_ls, 1)) { if (tap_ls != INVALID_SOCKET) closesocket(tap_ls); tap_ls = INVALID_SOCKET; return 0; }
    lan_u = socket(AF_INET, SOCK_DGRAM, 0);
    a.sin_port = htons(LAN_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (lan_u == INVALID_SOCKET || bind(lan_u, (struct sockaddr *)&a, sizeof(a))) {   /* (port pris : un port au hasard, on peut encore joindre l'hote) */
        a.sin_port = 0;
        if (lan_u == INVALID_SOCKET || bind(lan_u, (struct sockaddr *)&a, sizeof(a))) return 0;
    }
    if (amiaddr && *amiaddr) {                             /* l'ami : son nom ou son adresse (sans le port du Salon) */
        char host[128]; snprintf(host, sizeof(host), "%s", amiaddr);
        char *col = strrchr(host, ':'); if (col) *col = 0;
        struct addrinfo hi, *r = NULL; memset(&hi, 0, sizeof(hi)); hi.ai_family = AF_INET; hi.ai_socktype = SOCK_DGRAM;
        if (!getaddrinfo(host, NULL, &hi, &r) && r) { ami = *(struct sockaddr_in *)r->ai_addr; ami.sin_port = htons(LAN_PORT); ami_ok = 1; freeaddrinfo(r); }
        printf("[dolphin] ami %s : %s\n", host, ami_ok ? "trouve" : "introuvable (DNS)");
    }
    lan_on = 1;
    CreateThread(NULL, 0, tap_srv, NULL, 0, NULL);
    CreateThread(NULL, 0, lan_rx, NULL, 0, NULL);
    CreateThread(NULL, 0, lan_alive, NULL, 0, NULL);
    return 1;
}

/* path : chemin de GC Station ("sd:/..."), ou rien pour Dolphin seul. Rend un message pour l'ecran. */
int pcw_wpath_pub(const char *p, wchar_t *o, int cap);
const char *pcw_dolphin_launch(const char *path, const char *title, const char *amiaddr, const char *code) {
    static char msg[200];
    wchar_t dol[MAX_PATH], game[2048];
    if (!find_dolphin(dol)) return "Dolphin introuvable : installe-le, ou mets son dossier \xC3\xA0 c\xC3\xB4t\xC3\xA9 de GC Station (dossier Dolphin), ou son chemin dans dolphin.txt";
    if (dol_proc && WaitForSingleObject(dol_proc, 0) == WAIT_TIMEOUT) return "Dolphin est d\xC3\xA9j\xC3\xA0 ouvert : ferme-le d'abord";
    if (dol_proc) { CloseHandle(dol_proc); dol_proc = NULL; }
    if (!path || !pcw_wpath_pub(path, game, 2048)) return "Jeu introuvable";
    int net = code && strlen(code) >= 4;
    if (net && !net_start(amiaddr, code)) net = 0;
    wchar_t cmd[4096];
    if (net) _snwprintf(cmd, 4096, L"\"%ls\" -e \"%ls\" -C Dolphin.Core.SerialPort1=11 -C Dolphin.Core.BBA_TAPSERVER_DESTINATION=127.0.0.1:%d", dol, game, TAP_PORT);
    else _snwprintf(cmd, 4096, L"\"%ls\" -e \"%ls\"", dol, game);
    wchar_t dir[MAX_PATH]; wcscpy(dir, dol); wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si); memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(dol, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) return "Dolphin n'a pas pu d\xC3\xA9marrer";
    CloseHandle(pi.hThread);
    dol_proc = pi.hProcess;
    printf("[dolphin] %s : lance%s\n", title ? title : "jeu", net ? " (reseau local par Internet)" : "");
    snprintf(msg, sizeof(msg), net ? "%s : dans Dolphin, jeu en r\xC3\xA9seau local par Internet pr\xC3\xAAt" : "%s : lanc\xC3\xA9 dans Dolphin", title ? title : "Jeu");
    return msg;
}
#endif

/* GC Station PC (Windows) : mise a jour depuis GitHub, comme l'appli (AppUpdate.java).
   GC Station PC n'est pas public : il est seulement joint aux compilations (windows.yml, artefact "gc-station-pc").
   GitHub demande un jeton pour le telecharger : un "fine-grained token" en lecture seule (Actions : Read-only) sur le
   depot antho13otz/gc-mp3, dans le fichier jeton-github.txt a cote du programme.
   Au demarrage (puis F9 a tout moment) : derniere compilation reussie de GC Station PC sur main ; si son numero est plus
   grand que celui de ce programme (GCMP3_BUILD = numero de la compilation windows.yml), on propose de l'installer :
   l'artefact (zip, contenant GC-Station-PC.zip) est telecharge, decompresse (Expand-Archive de Windows), le programme
   actuel est renomme en "GC Station.exe.ancien", le nouveau prend sa place et redemarre. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifndef GCMP3_BUILD
#define GCMP3_BUILD 0
#endif
#define REPO "antho13otz/gc-mp3"
#define ARTEFACT "gc-station-pc"
const wchar_t *pcw_exe_dir(void);
void pcw_dolphin_stop(void);
HWND pcw_window(void);

/* ---------- HTTPS : curl.exe de Windows (Windows 10 et 11 l'ont ; WinHTTP refusait l'envoi sur certains Windows) ----------
   Le jeton passe par un fichier d'en-tetes (pas sur la ligne de commande) ; les redirections sont suivies, et curl ne
   donne pas le jeton a un autre site (le stockage des fichiers de GitHub n'en veut pas). */
typedef struct { char *b; size_t n; } buf_t;
static int run_wait(const wchar_t *cmd);
static int curl_get(const char *url, const char *tok, int *code, const wchar_t *out) {
    wchar_t tmp[MAX_PATH], hdr[MAX_PATH], cf[MAX_PATH], c[4096], wu[2048];
    GetTempPathW(MAX_PATH, tmp);
    _snwprintf(hdr, MAX_PATH, L"%lsgcs-maj-h.txt", tmp);
    _snwprintf(cf, MAX_PATH, L"%lsgcs-maj-code.txt", tmp);
    FILE *h = _wfopen(hdr, L"wb");
    if (!h) return 0;
    fprintf(h, "Accept: application/vnd.github+json\n");
    if (tok && *tok) fprintf(h, "Authorization: Bearer %s\n", tok);
    fclose(h);
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wu, 2048);
    _snwprintf(c, 4096, L"cmd.exe /c curl.exe -sS -L --max-time 600 -A gc-station-pc -H @\"%ls\" -o \"%ls\" -w %%{http_code} \"%ls\" > \"%ls\"", hdr, out, wu, cf);
    DeleteFileW(cf);
    int r = run_wait(c);
    DeleteFileW(hdr);
    char t[16] = "";
    FILE *k = _wfopen(cf, L"rb");
    if (k) { if (!fgets(t, sizeof(t), k)) t[0] = 0; fclose(k); }
    DeleteFileW(cf);
    *code = atoi(t);
    if (r != 0 && !*code) printf("[maj] curl : code %d\n", r);
    return *code > 0;
}
static int api(const char *path, const char *tok, buf_t *o, char *err, int ecap) {
    char u8[1024];
    snprintf(u8, sizeof(u8), "https://api.github.com/repos/%s%s", REPO, path);
    int code = 0;
    o->b = NULL; o->n = 0;
    wchar_t tmp[MAX_PATH], of[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp); _snwprintf(of, MAX_PATH, L"%lsgcs-maj-api.json", tmp);
    DeleteFileW(of);
    if (!curl_get(u8, tok, &code, of)) { snprintf(err, ecap, "GitHub injoignable (pas d'Internet ?)"); return 0; }
    FILE *f = _wfopen(of, L"rb");
    if (f) {
        fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
        o->b = (char *)malloc(n > 0 ? (size_t)n + 1 : 1);
        if (o->b) { o->n = n > 0 ? fread(o->b, 1, (size_t)n, f) : 0; o->b[o->n] = 0; }
        fclose(f);
    }
    DeleteFileW(of);
    if (code == 401) { free(o->b); o->b = NULL; snprintf(err, ecap, "Jeton refus\xC3\xA9 par GitHub (expir\xC3\xA9 ou mal copi\xC3\xA9 ?)"); return 0; }
    if (code == 403 || code == 404) { free(o->b); o->b = NULL; snprintf(err, ecap, "GitHub refuse (%d) : le jeton a-t-il \xC2\xAB Actions : Read-only \xC2\xBB sur " REPO " ?", code); return 0; }
    if (code != 200 || !o->b) { free(o->b); o->b = NULL; snprintf(err, ecap, "GitHub : erreur %d", code); return 0; }
    return 1;
}

/* ---------- JSON : objets d'un tableau, cles du premier niveau seulement ---------- */
/* p : debut d'un objet ({) ; rend la fin (apres }) ; vals : valeurs (texte brut) des cles demandees */
static const char *obj_keys(const char *p, const char *const *keys, int nk, char vals[][96]) {
    for (int i = 0; i < nk; i++) vals[i][0] = 0;
    if (*p != '{') return NULL;
    int depth = 0;
    const char *q = p;
    for (; *q; q++) {
        if (*q == '"') {
            const char *s = ++q;
            while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
            if (!*q) return NULL;
            const char *e = q;
            if (depth == 1) {                             /* (une cle du premier niveau ?) */
                const char *a = e + 1; while (*a == ' ' || *a == '\n' || *a == '\r' || *a == '\t') a++;
                if (*a == ':') {
                    for (int i = 0; i < nk; i++) if ((size_t)(e - s) == strlen(keys[i]) && !strncmp(s, keys[i], e - s)) {
                        a++; while (*a == ' ' || *a == '\n' || *a == '\r' || *a == '\t') a++;
                        int n = 0;
                        if (*a == '"') { a++; while (*a && *a != '"' && n < 95) vals[i][n++] = *a++; }
                        else while (*a && *a != ',' && *a != '}' && *a != ' ' && *a != '\n' && *a != '\r' && n < 95) vals[i][n++] = *a++;
                        vals[i][n] = 0;
                    }
                }
            }
            continue;
        }
        if (*q == '{' || *q == '[') depth++;
        else if (*q == '}' || *q == ']') { if (--depth == 0) return q + 1; }
    }
    return NULL;
}
/* les objets du tableau "name" : appelle cb pour chacun */
static void each(const char *json, const char *name, const char *const *keys, int nk, int (*cb)(char vals[][96], void *), void *ud) {
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\"", name);
    const char *p = strstr(json, pat);
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    p++;
    char vals[8][96];
    for (;;) {
        while (*p && *p != '{' && *p != ']') p++;
        if (*p != '{') return;
        const char *e = obj_keys(p, keys, nk, vals);
        if (!e) return;
        if (cb(vals, ud)) return;
        p = e;
    }
}
typedef struct { long num; char id[32]; } run_t;
static int cb_run(char v[][96], void *ud) {
    run_t *b = (run_t *)ud;
    long n = atol(v[1]);
    if (n > b->num) { b->num = n; snprintf(b->id, sizeof(b->id), "%s", v[0]); }
    return 0;
}
typedef struct { char id[32]; } art_t;
static int cb_art(char v[][96], void *ud) {
    art_t *a = (art_t *)ud;
    if (!strcmp(v[1], ARTEFACT) && strcmp(v[2], "true")) { snprintf(a->id, sizeof(a->id), "%s", v[0]); return 1; }
    return 0;
}

static int read_token(char *t, int cap) {
    t[0] = 0;
    const char *e = getenv("GCSTATION_JETON");
    if (e && *e) { snprintf(t, cap, "%s", e); return 1; }
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%ls\\jeton-github.txt", pcw_exe_dir());
    FILE *f = _wfopen(p, L"rb");
    if (!f) return 0;
    char l[400] = "";
    if (!fgets(l, sizeof(l), f)) l[0] = 0;
    fclose(f);
    char *s = l; if (!strncmp(s, "\xEF\xBB\xBF", 3)) s += 3;
    while (*s == ' ' || *s == '\t') s++;
    s[strcspn(s, " \t\r\n")] = 0;
    snprintf(t, cap, "%s", s);
    return t[0] != 0;
}
static int essai = 0;                                     /* (GCSTATION_MAJ_ESSAI : essai automatique : pas de boite, rien de remplace) */
static int msg(const char *t, UINT fl) {                  /* boite de Windows (texte UTF-8) */
    if (essai) { printf("[maj] (boite) %s\n", t); return IDYES; }
    wchar_t w[1200]; MultiByteToWideChar(CP_UTF8, 0, t, -1, w, 1200);
    return MessageBoxW(pcw_window(), w, L"GC Station PC : mise à jour", fl | MB_SETFOREGROUND);
}
static int run_wait(const wchar_t *cmd) {
    STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof(si)); si.cb = sizeof(si); memset(&pi, 0, sizeof(pi));
    wchar_t c[4096]; wcsncpy(c, cmd, 4095); c[4095] = 0;
    if (!CreateProcessW(NULL, c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 180000);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}
static int unzip(const wchar_t *zip, const wchar_t *dir) {
    wchar_t c[3000];
    _snwprintf(c, 3000, L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '%ls' -DestinationPath '%ls' -Force\"", zip, dir);
    return run_wait(c) == 0;
}
static void del_tree(const wchar_t *dir) {
    wchar_t c[1200]; _snwprintf(c, 1200, L"cmd.exe /c rmdir /s /q \"%ls\"", dir);
    run_wait(c);
}
static volatile LONG busy = 0;
/* manual : 1 = demande (F9) : on dit aussi "a jour" et les erreurs ; 0 = au demarrage : silencieux si rien */
static DWORD WINAPI check(LPVOID a) {
    int manual = (int)(INT_PTR)a;
    char tok[400], err[300] = "", t[600];
    if (!read_token(tok, sizeof(tok))) {
        if (manual) msg("Pas de jeton GitHub : mets-le dans le fichier jeton-github.txt, \xC3\xA0 c\xC3\xB4t\xC3\xA9 de GC Station.exe "
                        "(un \xC2\xAB fine-grained token \xC2\xBB, Actions : Read-only, sur le d\xC3\xA9p\xC3\xB4t " REPO ").", MB_ICONINFORMATION);
        InterlockedExchange(&busy, 0); return 0;
    }
    buf_t b;
    run_t best = { 0, "" };
    static const char *const rk[] = { "id", "run_number" };
    {
        const char *br = essai && getenv("GCSTATION_MAJ_BRANCHE") ? getenv("GCSTATION_MAJ_BRANCHE") : "main";   /* (essai : une autre branche) */
        char q[200]; snprintf(q, sizeof(q), "/actions/workflows/windows.yml/runs?branch=%s&status=success&event=push&per_page=10", br);
        if (!api(q, tok, &b, err, sizeof(err))) goto fail;
    }
    each(b.b, "workflow_runs", rk, 2, cb_run, &best);
    free(b.b);
    printf("[maj] ce programme : build %d ; derniere compilation : %ld\n", (int)GCMP3_BUILD, best.num);
    if (essai && best.num) printf("[maj] essai : compilation %ld (%s) prise meme si pas plus recente\n", best.num, best.id);
    if (!best.num || (best.num <= GCMP3_BUILD && !essai)) {
        if (manual) { snprintf(t, sizeof(t), "GC Station PC est \xC3\xA0 jour (build %d).", (int)GCMP3_BUILD); msg(t, MB_ICONINFORMATION); }
        InterlockedExchange(&busy, 0); return 0;
    }
    snprintf(t, sizeof(t), "Une nouvelle version de GC Station PC est pr\xC3\xAAte (build %ld ; celle-ci : build %d).\n\nL'installer maintenant ? GC Station red\xC3\xA9marrera.", best.num, (int)GCMP3_BUILD);
    if (msg(t, MB_YESNO | MB_ICONQUESTION) != IDYES) { InterlockedExchange(&busy, 0); return 0; }
    {
        char path[128]; snprintf(path, sizeof(path), "/actions/runs/%s/artifacts", best.id);
        art_t art = { "" };
        static const char *const ak[] = { "id", "name", "expired" };
        if (!api(path, tok, &b, err, sizeof(err))) goto fail;
        each(b.b, "artifacts", ak, 3, cb_art, &art);
        free(b.b);
        if (!art.id[0]) { snprintf(err, sizeof(err), "Fichier de la compilation %ld introuvable (expir\xC3\xA9 ?)", best.num); goto fail; }
        /* l'artefact (zip) : GitHub renvoie vers un stockage a part (curl suit, sans lui donner le jeton) */
        wchar_t tmp[MAX_PATH], dir[MAX_PATH], zip[MAX_PATH], z2[MAX_PATH], d2[MAX_PATH];
        char u8[256]; snprintf(u8, sizeof(u8), "https://api.github.com/repos/" REPO "/actions/artifacts/%s/zip", art.id);
        GetTempPathW(MAX_PATH, tmp);
        _snwprintf(dir, MAX_PATH, L"%lsgcs-maj", tmp);
        del_tree(dir);
        CreateDirectoryW(dir, NULL);
        _snwprintf(zip, MAX_PATH, L"%ls\\artefact.zip", dir);
        int code = 0;
        if (!curl_get(u8, tok, &code, zip) || code != 200) { snprintf(err, sizeof(err), "T\xC3\xA9l\xC3\xA9" "chargement : erreur %d", code); goto fail; }
        _snwprintf(d2, MAX_PATH, L"%ls\\a", dir);
        _snwprintf(z2, MAX_PATH, L"%ls\\a\\GC-Station-PC.zip", dir);
        if (!unzip(zip, d2) || GetFileAttributesW(z2) == INVALID_FILE_ATTRIBUTES) { snprintf(err, sizeof(err), "Fichier t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9 illisible"); goto fail; }
        _snwprintf(d2, MAX_PATH, L"%ls\\b", dir);
        wchar_t nexe[MAX_PATH], me[MAX_PATH], old[MAX_PATH], lis[MAX_PATH], lis2[MAX_PATH];
        _snwprintf(nexe, MAX_PATH, L"%ls\\GC Station.exe", d2);
        if (!unzip(z2, d2) || GetFileAttributesW(nexe) == INVALID_FILE_ATTRIBUTES) { snprintf(err, sizeof(err), "Nouvelle version illisible"); goto fail; }
        if (essai) {
            WIN32_FILE_ATTRIBUTE_DATA fa; GetFileAttributesExW(nexe, GetFileExInfoStandard, &fa);
            printf("[maj] essai : nouvelle version telechargee et lisible (GC Station.exe : %lu octets) ; rien remplace\n", (unsigned long)fa.nFileSizeLow);
            del_tree(dir);
            InterlockedExchange(&busy, 0); return 0;
        }
        GetModuleFileNameW(NULL, me, MAX_PATH);
        _snwprintf(old, MAX_PATH, L"%ls.ancien", me);
        DeleteFileW(old);
        if (!MoveFileW(me, old)) { snprintf(err, sizeof(err), "Impossible de remplacer le programme (dossier en lecture seule ?)"); goto fail; }
        if (!CopyFileW(nexe, me, FALSE)) { MoveFileW(old, me); snprintf(err, sizeof(err), "Impossible d'installer la nouvelle version"); goto fail; }
        _snwprintf(lis, MAX_PATH, L"%ls\\LISEZMOI.txt", d2); _snwprintf(lis2, MAX_PATH, L"%ls\\LISEZMOI.txt", pcw_exe_dir());
        CopyFileW(lis, lis2, FALSE);
        del_tree(dir);
        printf("[maj] build %ld installe : redemarrage\n", best.num);
        STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
        wchar_t cmd[MAX_PATH + 4]; _snwprintf(cmd, MAX_PATH + 4, L"\"%ls\"", me);
        if (CreateProcessW(me, cmd, NULL, NULL, FALSE, 0, NULL, pcw_exe_dir(), &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
        pcw_dolphin_stop();
        ExitProcess(0);
    }
fail:
    printf("[maj] %s\n", err);
    if (manual || err[0]) { snprintf(t, sizeof(t), "Mise \xC3\xA0 jour impossible : %s", err); if (manual) msg(t, MB_ICONWARNING); }
    InterlockedExchange(&busy, 0);
    return 0;
}
/* au demarrage (apres 8 s) ou avec F9 */
static DWORD WINAPI later(LPVOID a) { (void)a; Sleep(8000); return check((LPVOID)0); }
static DWORD WINAPI essai_thr(LPVOID a) { (void)a; check((LPVOID)1); printf("[maj] essai fini\n"); fflush(stdout); ExitProcess(0); return 0; }
void pcw_maj_start(int manual) {
    essai = getenv("GCSTATION_MAJ_ESSAI") != NULL;
    if (essai) { InterlockedExchange(&busy, 1); CreateThread(NULL, 0, essai_thr, NULL, 0, NULL); return; }
    if (InterlockedExchange(&busy, 1)) return;
    CreateThread(NULL, 0, manual ? check : later, (LPVOID)(INT_PTR)manual, 0, NULL);
}
#endif

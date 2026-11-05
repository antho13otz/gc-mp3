/* GC Station PC (Windows) : ce qui remplace Linux pour le simulateur (pcsim.c), et fait de lui un vrai programme :
   - une fenetre (l'image de GC Station, agrandie ; F11 : plein ecran), le clavier et les manettes XInput (Xbox et
     compatibles) a la place de la manette GameCube, le son vers la carte son (waveOut) ;
   - les fichiers : "sd:/..." = le dossier "sd" a cote du programme (noms en UTF-8 -> Windows) ; dossiers (d_type),
     place libre, open_memstream, renommer en remplacant (comme POSIX) ;
   - le reseau : Winsock (le reste est dans pcsim.c).
   Compile seulement pour Windows (MinGW 32 bits : les adresses tiennent sur 32 bits, comme sur la console). */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>
#include <io.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/statvfs.h>
#undef fopen
#undef fclose
#undef stat
#undef opendir
#undef readdir
#undef closedir
#undef mkdir
#undef remove
#undef rename
#undef rmdir
#undef unlink
#undef access
#undef ftruncate
#undef statvfs
#undef memalign
#undef mallinfo
#undef open_memstream
#undef strcasestr
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef signed char s8;
extern int pcsim_live;

/* ======================= chemins ======================= */
static wchar_t root_w[MAX_PATH];                          /* le dossier "sd" (carte SD de GC Station PC) */
static wchar_t exe_w[MAX_PATH];                           /* le dossier du programme */
const wchar_t *pcw_exe_dir(void) { return exe_w; }
const wchar_t *pcw_sd_dir(void) { return root_w; }
static void root_init(void) {
    if (root_w[0]) return;
    GetModuleFileNameW(NULL, exe_w, MAX_PATH);
    wchar_t *sl = wcsrchr(exe_w, L'\\');
    if (sl) *sl = 0;
    const char *e = getenv("GCSTATION_SD");               /* (un autre dossier pour la carte, si besoin) */
    if (e && *e) MultiByteToWideChar(CP_UTF8, 0, e, -1, root_w, MAX_PATH);
    else _snwprintf(root_w, MAX_PATH, L"%ls\\sd", exe_w);
    CreateDirectoryW(root_w, NULL);
}
/* "sd:/a/b" -> <dossier sd>\a\b ; autres chemins : tels quels (relatifs au dossier du programme) */
static int wpath(const char *p, wchar_t *o, int cap) {
    root_init();
    char tmp[2048];
    const char *rest = NULL;
    if (!p) return 0;
    if (!strncmp(p, "sd:", 3)) rest = p + 3;
    else if ((!strncmp(p, "sda:", 4) || !strncmp(p, "sdb:", 4) || !strncmp(p, "sdc:", 4) || !strncmp(p, "sdg:", 4))) rest = p + 4;
    if (rest) {
        while (*rest == '/' || *rest == '\\') rest++;
        int n = WideCharToMultiByte(CP_UTF8, 0, root_w, -1, tmp, sizeof(tmp), NULL, NULL);
        if (n <= 0) return 0;
        snprintf(tmp + n - 1, sizeof(tmp) - (n - 1), "%s%s", *rest ? "\\" : "", rest);
    } else snprintf(tmp, sizeof(tmp), "%s", p);
    for (char *q = tmp; *q; q++) if (*q == '/') *q = '\\';
    size_t l = strlen(tmp);
    while (l > 3 && tmp[l - 1] == '\\') tmp[--l] = 0;     /* (pas de \ a la fin : stat refuserait) */
    return MultiByteToWideChar(CP_UTF8, 0, tmp, -1, o, cap) > 0;
}

int pcw_wpath_pub(const char *p, wchar_t *o, int cap) { return wpath(p, o, cap); }

/* ======================= fichiers ======================= */
FILE *__real_fopen(const char *p, const char *m) {
    wchar_t w[2048], wm[16];
    if (!wpath(p, w, 2048)) { errno = ENOENT; return NULL; }
    char mm[16]; snprintf(mm, sizeof(mm), "%s", m);
    if (!strchr(mm, 'b') && !strchr(mm, 't') && strlen(mm) < 14) strcat(mm, "b");   /* (POSIX : jamais de conversion des fins de ligne) */
    MultiByteToWideChar(CP_UTF8, 0, mm, -1, wm, 16);
    return _wfopen(w, wm);
}
static time_t ft_time(FILETIME f) {
    unsigned long long t = (unsigned long long)f.dwHighDateTime << 32 | f.dwLowDateTime;
    return t < 116444736000000000ull ? 0 : (time_t)((t - 116444736000000000ull) / 10000000ull);
}
int __real_stat(const char *p, struct stat *s) {
    wchar_t w[2048];
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!wpath(p, w, 2048) || !GetFileAttributesExW(w, GetFileExInfoStandard, &a)) { errno = ENOENT; return -1; }
    memset(s, 0, sizeof(*s));
    int dir = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    s->st_mode = dir ? (_S_IFDIR | 0777) : (_S_IFREG | 0666);
    unsigned long long sz = (unsigned long long)a.nFileSizeHigh << 32 | a.nFileSizeLow;
    s->st_size = dir ? 0 : (sz > 0x7FFFFFFFull ? 0x7FFFFFFF : (long)sz);   /* (fichiers de plus de 2 Go : taille plafonnee) */
    s->st_mtime = ft_time(a.ftLastWriteTime); s->st_atime = ft_time(a.ftLastAccessTime); s->st_ctime = ft_time(a.ftCreationTime);
    s->st_nlink = 1;
    return 0;
}
struct pcw_dir { HANDLE h; WIN32_FIND_DATAW fd; int first; struct dirent de; };
DIR *__real_opendir(const char *p) {
    wchar_t w[2048];
    if (!wpath(p, w, 2040)) { errno = ENOENT; return NULL; }
    wcscat(w, L"\\*");
    struct pcw_dir *d = (struct pcw_dir *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->h = FindFirstFileW(w, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) { free(d); errno = ENOENT; return NULL; }
    d->first = 1;
    return (DIR *)d;
}
struct dirent *__real_readdir(DIR *dd) {
    struct pcw_dir *d = (struct pcw_dir *)dd;
    for (;;) {
        if (!d->first && !FindNextFileW(d->h, &d->fd)) return NULL;
        d->first = 0;
        if (!wcscmp(d->fd.cFileName, L".") || !wcscmp(d->fd.cFileName, L"..")) continue;
        if (WideCharToMultiByte(CP_UTF8, 0, d->fd.cFileName, -1, d->de.d_name, sizeof(d->de.d_name), NULL, NULL) <= 0) continue;
        d->de.d_type = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
        d->de.d_reclen = sizeof(d->de);
        return &d->de;
    }
}
int __real_closedir(DIR *dd) { struct pcw_dir *d = (struct pcw_dir *)dd; if (!d) return -1; FindClose(d->h); free(d); return 0; }
int pcw_mkdir(const char *p) { wchar_t w[2048]; if (!wpath(p, w, 2048)) return -1; if (CreateDirectoryW(w, NULL)) return 0; errno = GetLastError() == ERROR_ALREADY_EXISTS ? EEXIST : ENOENT; return -1; }
int pcw_rmdir(const char *p) { wchar_t w[2048]; if (!wpath(p, w, 2048)) return -1; return RemoveDirectoryW(w) ? 0 : (errno = ENOTEMPTY, -1); }
int pcw_unlink(const char *p) { wchar_t w[2048]; if (!wpath(p, w, 2048)) return -1; SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL); return DeleteFileW(w) ? 0 : (errno = ENOENT, -1); }
int pcw_remove(const char *p) {                           /* (POSIX : un fichier, ou un dossier vide) */
    wchar_t w[2048]; if (!wpath(p, w, 2048)) return -1;
    DWORD a = GetFileAttributesW(w);
    if (a == INVALID_FILE_ATTRIBUTES) { errno = ENOENT; return -1; }
    if (a & FILE_ATTRIBUTE_DIRECTORY) return RemoveDirectoryW(w) ? 0 : (errno = ENOTEMPTY, -1);
    SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL);
    return DeleteFileW(w) ? 0 : (errno = EACCES, -1);
}
int pcw_rename(const char *a, const char *b) {            /* (POSIX : remplace la destination) */
    wchar_t wa[2048], wb[2048];
    if (!wpath(a, wa, 2048) || !wpath(b, wb, 2048)) return -1;
    return MoveFileExW(wa, wb, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) ? 0 : (errno = ENOENT, -1);
}
int pcw_access(const char *p, int m) { wchar_t w[2048]; if (!wpath(p, w, 2048)) return -1; return _waccess(w, m & 6); }
int pcw_truncate(int fd, long n) { return _chsize(fd, n); }
int pcw_statvfs(const char *p, struct statvfs *s) {
    wchar_t w[2048];
    ULARGE_INTEGER fr, tot, tfr;
    if (!wpath(p, w, 2048) || !GetDiskFreeSpaceExW(w, &fr, &tot, &tfr)) { errno = ENOENT; return -1; }
    memset(s, 0, sizeof(*s));
    s->f_bsize = s->f_frsize = 32768;                      /* (blocs de 32 Ko : les tailles tiennent sur 32 bits) */
    s->f_blocks = (fsblkcnt_t)(tot.QuadPart / 32768); s->f_bfree = (fsblkcnt_t)(tfr.QuadPart / 32768); s->f_bavail = (fsblkcnt_t)(fr.QuadPart / 32768);
    s->f_namemax = 255;
    return 0;
}
/* fichier temporaire efface a la fermeture */
static FILE *temp_file(void) {
    wchar_t d[MAX_PATH], f[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, d) || !GetTempFileNameW(d, L"gcs", 0, f)) return NULL;
    return _wfopen(f, L"w+bD");
}
/* un fichier du reseau (smb: ftp: gcs: ...) : copie entiere dans un fichier temporaire (pas de fopencookie ici) */
int gcs_nfs_fread(void *f, void *b, int n);
FILE *pcw_nfs_copy(void *h) {
    FILE *t = temp_file();
    if (!t) return NULL;
    static char buf[65536];
    for (;;) {
        int r = gcs_nfs_fread(h, buf, sizeof(buf));
        if (r < 0) { fclose(t); return NULL; }
        if (r == 0) break;
        fwrite(buf, 1, (size_t)r, t);
    }
    rewind(t);
    return t;
}
/* open_memstream : un fichier temporaire, relu a la fermeture */
#define NMS 16
static struct { FILE *f; char **b; size_t *n; } ms[NMS];
static CRITICAL_SECTION ms_cs;
static int ms_ok = 0;
FILE *pcw_open_memstream(char **b, size_t *n) {
    FILE *f = temp_file();
    if (!f) return NULL;
    if (!ms_ok) { InitializeCriticalSection(&ms_cs); ms_ok = 1; }
    EnterCriticalSection(&ms_cs);
    int k = -1;
    for (int i = 0; i < NMS; i++) if (!ms[i].f) { k = i; break; }
    if (k >= 0) { ms[k].f = f; ms[k].b = b; ms[k].n = n; }
    LeaveCriticalSection(&ms_cs);
    if (k < 0) { fclose(f); return NULL; }
    *b = NULL; *n = 0;
    return f;
}
int pcw_fclose(FILE *f) {
    if (!f) return EOF;
    int k = -1;
    if (ms_ok) { EnterCriticalSection(&ms_cs); for (int i = 0; i < NMS; i++) if (ms[i].f == f) { k = i; break; } LeaveCriticalSection(&ms_cs); }
    if (k < 0) return fclose(f);
    fflush(f);
    long len = ftell(f);
    char *b = (char *)malloc(len > 0 ? (size_t)len + 1 : 1);
    int r = 0;
    if (!b) r = EOF;
    else { rewind(f); size_t got = len > 0 ? fread(b, 1, (size_t)len, f) : 0; b[got] = 0; *ms[k].b = b; *ms[k].n = got; }
    EnterCriticalSection(&ms_cs); ms[k].f = NULL; LeaveCriticalSection(&ms_cs);
    fclose(f);
    return r;
}
char *pcw_strcasestr(const char *h, const char *n) {
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (!_strnicmp(h, n, l)) return (char *)h;
    return NULL;
}
struct mallinfo pcw_mallinfo(void) {                      /* (memoire libre affichee par GC Station : celle du PC, jusqu'a 1 Go) */
    struct mallinfo m; memset(&m, 0, sizeof(m));
    MEMORYSTATUSEX x; x.dwLength = sizeof(x);
    unsigned long long fr = GlobalMemoryStatusEx(&x) ? x.ullAvailVirtual : 256u << 20;
    if (fr > (1u << 30)) fr = 1u << 30;
    m.arena = 64 << 20; m.uordblks = 16 << 20; m.fordblks = (int)(fr > (48u << 20) ? fr - (48u << 20) : 0);
    return m;
}
/* (le simulateur appelle aussi les fonctions "reelles" : sous Linux, celles de --wrap) */
void *__real_malloc(size_t n) { return malloc(n); }
void __real_free(void *p) { free(p); }
void *__real_calloc(size_t a, size_t b) { return calloc(a, b); }
void *__real_realloc(void *p, size_t n) { return realloc(p, n); }
void *__real_memalign(size_t a, size_t n) { (void)a; return malloc(n); }
void *__real_aligned_alloc(size_t a, size_t n) { (void)a; return malloc(n); }
int __real_posix_memalign(void **o, size_t a, size_t n) { (void)a; *o = malloc(n); return *o ? 0 : ENOMEM; }
int __real_fileno(FILE *f) { return _fileno(f); }
size_t malloc_usable_size(void *p) { return p ? _msize(p) : 0; }   /* (pcsim_limits.c : comptes du tas, inutilises ici) */
int backtrace(void **b, int n) { (void)b; (void)n; return 0; }

/* ======================= fenetre ======================= */
static HWND win = NULL;
static CRITICAL_SECTION fb_cs;
static u32 *fb_front = NULL;                               /* derniere image (BGRA, 640 x fb_h) */
static int fb_h = 480, fb_new = 0;
static volatile u8 keys[256];
static int full = 0;
static RECT win_rect;
static void toggle_full(void) {
    full = !full;
    if (full) {
        GetWindowRect(win, &win_rect);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(win, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(win, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(win, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(win, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(win, NULL, win_rect.left, win_rect.top, win_rect.right - win_rect.left, win_rect.bottom - win_rect.top, SWP_FRAMECHANGED | SWP_NOZORDER);
    }
}
void pcw_dolphin_stop(void);
void pcw_maj_start(int manual);
HWND pcw_window(void) { return win; }
static LRESULT CALLBACK wproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (w == VK_F11 || (w == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))) { if (!(l & (1 << 30))) toggle_full(); return 0; }
        if (w == VK_F9) { if (!(l & (1 << 30))) pcw_maj_start(1); return 0; }   /* F9 : chercher une mise a jour (pcsim_maj.c) */
        if (w < 256) keys[w] = 1;
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP: if (w < 256) keys[w] = 0; return 0;
    case WM_KILLFOCUS: memset((void *)keys, 0, sizeof(keys)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r; GetClientRect(h, &r);
        int cw = r.right, ch = r.bottom;
        EnterCriticalSection(&fb_cs);
        if (fb_front) {
            int ih = fb_h, dw = cw, dh = cw * 3 / 4;       /* (image 4:3, bandes noires autour) */
            if (dh > ch) { dh = ch; dw = ch * 4 / 3; }
            int x0 = (cw - dw) / 2, y0 = (ch - dh) / 2;
            BITMAPINFO bi; memset(&bi, 0, sizeof(bi));
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = 640; bi.bmiHeader.biHeight = -ih;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE);
            StretchDIBits(dc, x0, y0, dw, dh, 0, 0, 640, ih, fb_front, &bi, DIB_RGB_COLORS, SRCCOPY);
            HBRUSH bk = (HBRUSH)GetStockObject(BLACK_BRUSH);
            RECT a = { 0, 0, cw, y0 }, b = { 0, y0 + dh, cw, ch }, c = { 0, y0, x0, y0 + dh }, d = { x0 + dw, y0, cw, y0 + dh };
            FillRect(dc, &a, bk); FillRect(dc, &b, bk); FillRect(dc, &c, bk); FillRect(dc, &d, bk);
        } else FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        fb_new = 0;
        LeaveCriticalSection(&fb_cs);
        EndPaint(h, &ps);
        return 0; }
    case WM_CLOSE: pcw_dolphin_stop(); ExitProcess(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static DWORD WINAPI win_thread(LPVOID a) {
    (void)a;
    WNDCLASSW wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"GCStationPC";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    RECT r = { 0, 0, 960, 720 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    win = CreateWindowW(L"GCStationPC", L"GC Station", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    ExitProcess(0);
    return 0;
}
/* une image de GC Station (couleurs 0..1) -> la fenetre */
void pcsim_present(const float *efb, int stride, int w, int h) {
    static u32 *back = NULL;
    if (!win) return;
    if (!back) back = (u32 *)malloc(640 * 576 * 4);
    if (!back || w != 640 || h <= 0 || h > 576) return;
    for (int y = 0; y < h; y++) {
        const float *s = efb + (size_t)y * stride * 4;
        u32 *d = back + (size_t)y * 640;
        for (int x = 0; x < 640; x++, s += 4) {
            int r = (int)(s[0] * 255.0f + 0.5f), g = (int)(s[1] * 255.0f + 0.5f), b = (int)(s[2] * 255.0f + 0.5f);
            r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
            d[x] = (u32)b | (u32)g << 8 | (u32)r << 16;
        }
    }
    EnterCriticalSection(&fb_cs);
    u32 *t = fb_front; fb_front = back; back = t; fb_h = h; fb_new = 1;
    LeaveCriticalSection(&fb_cs);
    if (!back) back = (u32 *)malloc(640 * 576 * 4);
    InvalidateRect(win, NULL, FALSE);
}

/* ======================= manettes : clavier + XInput ======================= */
typedef struct { WORD b; BYTE lt, rt; SHORT lx, ly, rx, ry; } xpad_t;
typedef struct { DWORD pkt; xpad_t g; } xstate_t;
typedef DWORD (WINAPI *xget_t)(DWORD, xstate_t *);
static xget_t xget = NULL;
static int x_tried = 0;
#define PB_LEFT 0x0001
#define PB_RIGHT 0x0002
#define PB_DOWN 0x0004
#define PB_UP 0x0008
#define PB_Z 0x0010
#define PB_R 0x0020
#define PB_L 0x0040
#define PB_A 0x0100
#define PB_B 0x0200
#define PB_X 0x0400
#define PB_Y 0x0800
#define PB_START 0x1000
static s8 axis(SHORT v) { int a = v / 256; if (a > -24 && a < 24) a = 0; return (s8)(a < -128 ? -128 : a > 127 ? 127 : a); }
void pcsim_win_pad(u16 *b, s8 *sx, s8 *sy, s8 *cx, s8 *cy) {
    u16 o = 0; int x = 0, y = 0, rx = 0, ry = 0;
    int fg = win && GetForegroundWindow() == win;
    if (fg) {                                              /* clavier (touches gardees par la fenetre) */
        if (keys[VK_LEFT]) o |= PB_LEFT;
        if (keys[VK_RIGHT]) o |= PB_RIGHT;
        if (keys[VK_UP]) o |= PB_UP;
        if (keys[VK_DOWN]) o |= PB_DOWN;
        if (keys[VK_RETURN] || keys[VK_SPACE]) o |= PB_A;   /* Entree / Espace : A */
        if (keys[VK_BACK]) o |= PB_B;                      /* Retour arriere : B */
        if (keys['X']) o |= PB_X;
        if (keys['Y']) o |= PB_Y;
        if (keys[VK_TAB] || keys['Z'] || keys['W']) o |= PB_Z;   /* Tab : onglet suivant (Z) */
        if (keys[VK_PRIOR] || keys['L']) o |= PB_L;        /* Page precedente : L */
        if (keys[VK_NEXT] || keys['R']) o |= PB_R;         /* Page suivante : R */
        if (keys[VK_ESCAPE] || keys[VK_F1]) o |= PB_START; /* Echap : START (menu) */
        if (keys[VK_NUMPAD4]) rx = -100;
        if (keys[VK_NUMPAD6]) rx = 100;
        if (keys[VK_NUMPAD8]) ry = 100;
        if (keys[VK_NUMPAD2]) ry = -100;
    }
    if (!x_tried) {
        x_tried = 1;
        static const wchar_t *const dl[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
        for (int i = 0; i < 3 && !xget; i++) { HMODULE m = LoadLibraryW(dl[i]); if (m) xget = (xget_t)(void *)GetProcAddress(m, "XInputGetState"); }
    }
    if (xget && fg) {
        static DWORD skip = 0;                             /* (pas de manette : on ne redemande qu'une fois par seconde) */
        if (GetTickCount() >= skip) {
            xstate_t st;
            if (xget(0, &st) == 0) {
                WORD w = st.g.b;
                if (w & 0x0001) o |= PB_UP;
                if (w & 0x0002) o |= PB_DOWN;
                if (w & 0x0004) o |= PB_LEFT;
                if (w & 0x0008) o |= PB_RIGHT;
                if (w & 0x0010) o |= PB_START;             /* Start */
                if (w & 0x0020) o |= PB_Z;                 /* Back */
                if (w & 0x0100) o |= PB_L;                 /* LB */
                if (w & 0x0200) o |= PB_R;                 /* RB */
                if (w & 0x1000) o |= PB_A;
                if (w & 0x2000) o |= PB_B;
                if (w & 0x4000) o |= PB_X;
                if (w & 0x8000) o |= PB_Y;
                if (st.g.lt > 200) o |= PB_L;
                if (st.g.rt > 200) o |= PB_R;
                if (!x) x = axis(st.g.lx);
                if (!y) y = axis(st.g.ly);
                if (!rx) rx = axis(st.g.rx);
                if (!ry) ry = axis(st.g.ry);
            } else skip = GetTickCount() + 1000;
        }
    }
    *b = o; *sx = (s8)x; *sy = (s8)y; *cx = (s8)rx; *cy = (s8)ry;
}

/* ======================= son (waveOut) ======================= */
#define NAB 4
static HWAVEOUT wo = NULL;
static WAVEHDR wh[NAB];
static char *wbuf[NAB];
static u32 wo_rate = 0;
void pcsim_win_audio(const void *b, u32 n, u32 rate) {
    if (!n) return;
    if (rate != wo_rate || !wo) {
        if (wo) { waveOutReset(wo); for (int i = 0; i < NAB; i++) if (wh[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wo, &wh[i], sizeof(WAVEHDR)); waveOutClose(wo); wo = NULL; }
        WAVEFORMATEX f; memset(&f, 0, sizeof(f));
        f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 2; f.nSamplesPerSec = rate; f.wBitsPerSample = 16; f.nBlockAlign = 4; f.nAvgBytesPerSec = rate * 4;
        if (waveOutOpen(&wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { wo = NULL; Sleep(n * 1000 / (rate * 4) + 1); return; }   /* (pas de carte son : au rythme de l'horloge) */
        wo_rate = rate;
        memset(wh, 0, sizeof(wh));
        for (int i = 0; i < NAB; i++) if (!wbuf[i]) wbuf[i] = (char *)malloc(65536);
    }
    for (;;) {                                             /* un tampon libre (au plus NAB - 1 en attente : ~3 x 21 ms d'avance) */
        int busy = 0, k = -1;
        for (int i = 0; i < NAB; i++) {
            if ((wh[i].dwFlags & WHDR_PREPARED) && !(wh[i].dwFlags & WHDR_DONE)) busy++;
            else if (k < 0) k = i;
        }
        if (k >= 0 && busy < NAB - 1) {
            if (wh[k].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wo, &wh[k], sizeof(WAVEHDR));
            if (n > 65536) n = 65536;
            memcpy(wbuf[k], b, n);
            memset(&wh[k], 0, sizeof(WAVEHDR));
            wh[k].lpData = wbuf[k]; wh[k].dwBufferLength = n;
            waveOutPrepareHeader(wo, &wh[k], sizeof(WAVEHDR));
            waveOutWrite(wo, &wh[k], sizeof(WAVEHDR));
            return;
        }
        Sleep(1);
    }
}

/* ======================= plantage : ou, et les dernieres actions ======================= */
void gcs_crumbs_dump(void) __attribute__((weak));
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *e) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_EXECUTE_HANDLER;
    DWORD base = (DWORD)(uintptr_t)GetModuleHandleW(NULL);
    DWORD pc = (DWORD)(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    printf("[plantage] exception %08lX a %08lX (programme +%08lX), adresse %08lX\n", (unsigned long)e->ExceptionRecord->ExceptionCode, (unsigned long)pc, (unsigned long)(pc - base),
           e->ExceptionRecord->NumberParameters > 1 ? (unsigned long)e->ExceptionRecord->ExceptionInformation[1] : 0ul);
    printf("[plantage] pile (+decalage dans le programme) : +%08lX", (unsigned long)(pc - base));
    DWORD *fp = (DWORD *)(uintptr_t)e->ContextRecord->Ebp;
    for (int i = 0; i < 24 && fp && !IsBadReadPtr(fp, 8); i++) {   /* (chaine des cadres : -fno-omit-frame-pointer) */
        DWORD ret = fp[1];
        if (ret >= base && ret < base + 0x4000000u) printf(" +%08lX", (unsigned long)(ret - base));
        DWORD *nx = (DWORD *)(uintptr_t)fp[0];
        if (nx <= fp) break;
        fp = nx;
    }
    printf("\n");
    if (gcs_crumbs_dump) gcs_crumbs_dump();
    fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

/* ======================= demarrage ======================= */
void pcw_dolphin_init(void);
void pcsim_win_boot(void) {
    timeBeginPeriod(1);                                    /* (attentes de 1 ms : sinon 15 ms, GC Station bien trop lent) */
    SetUnhandledExceptionFilter(crash_filter);
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    root_init();
    SetCurrentDirectoryW(exe_w);
    pcsim_live = getenv("PCSIM_SCRIPT") == NULL;
    const char *lg = getenv("PCSIM_LOG");
    if (pcsim_live || lg) {                                /* (programme sans console : le journal dans un fichier) */
        wchar_t p[MAX_PATH];
        if (lg) MultiByteToWideChar(CP_UTF8, 0, lg, -1, p, MAX_PATH); else _snwprintf(p, MAX_PATH, L"%ls\\journal.txt", exe_w);
        _wfreopen(p, L"w", stdout);
        _wfreopen(p, L"a", stderr);
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    InitializeCriticalSection(&fb_cs);
    pcw_dolphin_init();
    if (pcsim_live) { CreateThread(NULL, 0, win_thread, NULL, 0, NULL); pcw_maj_start(0); }
    else if (getenv("GCSTATION_MAJ_ESSAI")) pcw_maj_start(1);   /* (essai automatique de la mise a jour : windows.yml) */   /* (mise a jour : au demarrage, si un jeton est la) */
}
#endif

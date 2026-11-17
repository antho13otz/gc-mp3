/* Recherche d'un fichier par son nom sans la table des noms en memoire (GC Station v507).
   Bloc a FstStart + N*12 (FstStart = *(0x80000038), N = FstStart[2]) :
     +0 magic 'FSTH', +4 decalage r13 de currentDirectory (signe), +8 nombre de dossiers D, +12 nombre d'entrees E, +16 base FNV
     +20 : D x (entree, etat FNV apres "chemin/"), puis E x mot trie ((hash >> 12) << 12 | entree)
   Rend l'entree, ou -1. Pas d'appel, pas de donnee globale : copiable n'importe ou. */
typedef unsigned int u32; typedef int s32;
s32 fsth(const char *p) {
    const u32 *fst = *(const u32 *const *)0x80000038;
    u32 n = fst[2];
    const u32 *h = fst + n * 3;
    char *r13; __asm__("mr %0,13" : "=r"(r13));
    u32 d = *(const u32 *)(r13 + (s32)h[1]), nd = h[2], ne = h[3];
    const u32 *dt = h + 5, *ft = dt + nd * 2;
    u32 st = h[4];
    if (*p == '/') { d = 0; while (*p == '/') p++; }
    else if (d) { u32 i; for (i = 0; i < nd && dt[i * 2] != d; i++) ; if (i == nd) return -1; st = dt[i * 2 + 1]; }
    for (;;) {
        if (!*p) return (s32)d;
        if (p[0] == '.' && (p[1] == '/' || !p[1])) { p += p[1] ? 2 : 1; continue; }
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || !p[2])) {
            p += p[2] ? 3 : 2;
            d = d ? fst[d * 3 + 1] : 0;
            if (!d) st = h[4];
            else { u32 i; for (i = 0; i < nd && dt[i * 2] != d; i++) ; if (i == nd) return -1; st = dt[i * 2 + 1]; }
            continue;
        }
        u32 x = st;
        while (*p && *p != '/') { u32 c = (unsigned char)*p++; if (c >= 'A' && c <= 'Z') c += 32; x = (x ^ c) * 16777619u; }
        u32 k = x >> 12, lo = 0, hi = ne, e = 0xFFFFFFFFu;
        while (lo < hi) { u32 m = (lo + hi) >> 1, v = ft[m] >> 12; if (v == k) { e = ft[m] & 0xFFFu; break; } if (v < k) lo = m + 1; else hi = m; }
        if (e == 0xFFFFFFFFu) return -1;
        if (!*p) return (s32)e;
        while (*p == '/') p++;
        if (!(fst[e * 3] >> 24)) return -1;                /* (pas un dossier) */
        d = e; st = (x ^ '/') * 16777619u;
    }
}

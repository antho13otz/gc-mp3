// GC Station v220 : Outils > Memoire en direct (inclus dans main.c, apres pet.c).
// La memoire libre de la console, le plus grand bloc d'un seul tenant (ce qu'il faut pour ouvrir une grosse
// page, lancer un .dol, relancer apres une mise a jour), une courbe des 2 dernieres minutes, et pour chaque
// onglet visite le moins de memoire libre vu (pour trouver ce qui en prend). Mesure toutes les 3 s en fond
// (chaque seconde quand l'ecran est ouvert).

#define MV_HIST 120
static u16 mv_hf[MV_HIST];      /* (Ko / 16 : jusqu'a 1 Go) */
static int mv_n = 0, mv_pos = 0;
static u32 mv_tab_min[TAB_COUNT];
static u8 mv_tab_seen[TAB_COUNT];
static u32 mv_free = 0, mv_big = 0, mv_used = 0;
static u64 mv_t = 0;

static u32 mv_largest(void) {                    /* plus grand bloc : essais de reservation (dichotomie) */
    u32 lo = 0, hi = 24u << 20;
    while (hi - lo > 16384) {
        u32 mid = (lo + hi) / 2;
        void *p = malloc(mid);
        if (p) { free(p); lo = mid; } else hi = mid;
    }
    return lo;
}
static void mem_tick(void) {
    u64 now = now_ms();
    if (mv_t && now - mv_t < (tl_screen == TL_MEM ? 1000u : 3000u)) return;
    mv_t = now;
    struct mallinfo mi = mallinfo();
    mv_free = (u32)((uintptr_t)SYS_GetArena1Hi() - (uintptr_t)SYS_GetArena1Lo() + mi.fordblks);
    mv_used = (u32)mi.uordblks;
    if (tl_screen == TL_MEM) mv_big = mv_largest();       /* (seulement ecran ouvert : la mesure reserve presque tout un instant) */
    mv_hf[mv_pos] = (u16)(mv_free / 16384u);
    mv_pos = (mv_pos + 1) % MV_HIST;
    if (mv_n < MV_HIST) mv_n++;
    int t = (tl_screen || menu_open) ? -1 : tab;
    if (t >= 0 && t < TAB_COUNT) {
        if (!mv_tab_seen[t] || mv_free < mv_tab_min[t]) mv_tab_min[t] = mv_free;

        mv_tab_seen[t] = 1;
    }
}
static void mem_open(void) { tl_screen = TL_MEM; mv_t = 0; }
static void mem_input(u32 down, u32 nav, int fire) {
    (void)nav; (void)fire;
    if (down & (PAD_BUTTON_B | PAD_BUTTON_A)) tl_screen = TL_NONE;
}
static void mv_mo(char *o, int cap, u32 b) { snprintf(o, cap, "%u,%u Mo", (unsigned)(b >> 20), (unsigned)(((b & 0xFFFFF) * 10) >> 20)); }

static void draw_mem(void) {
    mem_tick();
    tl_frame("M\xC3\xA9moire", "en direct");
    char a[40], b[40], c[40];
    mv_mo(a, sizeof(a), mv_free); mv_mo(b, sizeof(b), mv_big); mv_mo(c, sizeof(c), mv_used);
    /* trois chiffres */
    const char *lab[3] = { "Libre", "Plus grand bloc", "Utilis\xC3\xA9" "e" };
    const char *val[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        f32 x = 64 + i * 176;
        gx_text1(F_SMALL, x, 104, lab[i], RGBA(170, 155, 225, 255));
        gx_text1(F_LARGE, x, 132, val[i], i == 1 ? RGBA(255, 210, 120, 255) : RGBA(255, 255, 255, 255));
    }
    /* barre : utilisee / libre */
    f32 bx = 64, bw = 512, by = 146;
    u32 tot = mv_free + mv_used;
    f32 fu = tot ? (f32)mv_used / tot : 0;
    gx_rrect_fill(bx, by, bw, 10, 5, RGBA(60, 50, 110, 255), RGBA(50, 40, 96, 255));
    gx_rrect_fill(bx, by, bw * fu, 10, 5, RGBA(255, 120, 190, 255), RGBA(120, 110, 255, 255));
    /* mise a jour : relance directe possible ? */
    int ok = mv_big >= 6u * 1024 * 1024;
    gx_text1(F_SMALL, 64, 178, ok ? "Relance directe apr\xC3\xA8s une mise \xC3\xA0 jour : possible (bloc de 6 Mo libre)"
                                  : "Relance apr\xC3\xA8s une mise \xC3\xA0 jour : par le lanceur de secours (pas de bloc de 6 Mo)",
             ok ? RGBA(150, 230, 170, 255) : RGBA(255, 200, 140, 255));
    /* courbe : 2 dernieres minutes */
    f32 gx0 = 64, gy0 = 196, gw = 512, gh = 84;
    gx_rrect_fill(gx0, gy0, gw, gh, 6, RGBA(20, 14, 50, 255), RGBA(14, 10, 38, 255));
    u32 mx = 1;
    for (int i = 0; i < mv_n; i++) { int k = (mv_pos - mv_n + i + MV_HIST) % MV_HIST; if (mv_hf[k] > mx) mx = mv_hf[k]; }
    mx += mx / 8 + 1;
    f32 py0 = gy0 + 24, ph = gh - 30;                       /* (zone de la courbe, sous la legende) */
    gx_set_flat();
    gx_rect(gx0 + 4, py0 + ph, gw - 8, 1, RGBA(70, 60, 120, 255));
    for (int i = 1; i < mv_n; i++) {                         /* (la plus recente a droite) */
        int k0 = (mv_pos - mv_n + i - 1 + MV_HIST) % MV_HIST, k1 = (mv_pos - mv_n + i + MV_HIST) % MV_HIST;
        int j = MV_HIST - mv_n + i;
        f32 x0 = gx0 + 4 + (gw - 8) * (j - 1) / (MV_HIST - 1), x1 = gx0 + 4 + (gw - 8) * j / (MV_HIST - 1);
        f32 y0 = py0 + ph - ph * mv_hf[k0] / mx, y1 = py0 + ph - ph * mv_hf[k1] / mx;
        gx_rect(x0, y0 < y1 ? y0 : y1, x1 - x0 + 0.8f, (y0 > y1 ? y0 - y1 : y1 - y0) + 2.0f, RGBA(150, 200, 255, 240));
    }
    gx_text1(F_SMALL, gx0 + 8, gy0 + 16, "M\xC3\xA9moire libre", RGBA(150, 200, 255, 255));
    gx_text_r(F_SMALL, gx0 + gw - 8, gy0 + 16, "2 derni\xC3\xA8res minutes", RGBA(130, 120, 180, 255));
    /* par onglet : le moins vu */
    gx_text1(F_SMALL, 64, 302, "Par onglet (le moins de m\xC3\xA9moire vu)", RGBA(170, 155, 225, 255));
    gx_text_r(F_SMALL, 576, 302, "libre (minimum)", RGBA(130, 120, 180, 255));
    int row = 0;
    for (int t = 0; t < TAB_COUNT && row < 5; t++) {
        int tt = tab_order[t];
        if (!mv_tab_seen[tt]) continue;
        f32 ry = 322 + row * 18;
        mv_mo(a, sizeof(a), mv_tab_min[tt]);
        gx_text1(F_SMALL, 64, ry, TR(nv_tab_names[tt]), RGBA(240, 232, 255, 255));
        f32 fr = mv_tab_min[tt] / (f32)(mv_free + mv_used ? mv_free + mv_used : 1);
        gx_rrect_fill(250, ry - 9, 230, 8, 4, RGBA(60, 50, 110, 255), RGBA(50, 40, 96, 255));
        gx_rrect_fill(250, ry - 9, 230 * (fr > 1 ? 1 : fr), 8, 4, RGBA(150, 200, 255, 255), RGBA(110, 160, 255, 255));
        gx_text_r(F_SMALL, 576, ry, a, RGBA(240, 232, 255, 255));
        row++;
    }
    if (!row) gx_text1(F_SMALL, 64, 322, "(les onglets visit\xC3\xA9s s'affichent ici)", RGBA(130, 120, 180, 255));
    static const char *const k[] = { "B" }, *const lb[] = { "RETOUR" };
    tl_legend(k, lb, 1);
}

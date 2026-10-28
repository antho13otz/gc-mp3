/* home.c - see home.h. Flat, angular early-2000s console-site look: solid
 * colour blocks with thin dark outlines, dark navy bars with gold capitals,
 * cut corners, dotted dividers and square site tiles with caption bars.
 * Everything is drawn from shapes (no picture files) and fits the content
 * area in 4:3 and 16:9. */
#include "lang.h"
#include "home.h"
#include "ui.h"
#include "gfx.h"
#include "theme.h"
#include "input.h"
#include "settings.h"
#include "version.h"
#include "update.h"
#include "fetch.h"
#include "browser.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { IC_SEARCH, IC_BOOK, IC_NEWS, IC_RETRO, IC_CHAT, IC_GLOBE, IC_PENCIL };

typedef struct {
    const char *name, *blurb, *url;
    int icon;
    unsigned char r, g, b;
} Site;

static const Site sites[] = {
    { "DUCKDUCKGO",  "Private web search, light version",        "https://lite.duckduckgo.com/lite/",       IC_SEARCH, 30, 122, 170 },
    { "ASSISTANT IA", "GC Station : poser une question a une IA", "gc:ia",                                 IC_CHAT,   122, 80, 220 },
    { "WIKIPEDIA",   "L'encyclopedie libre (pages mobiles)",     "https://fr.m.wikipedia.org/wiki/GameCube", IC_BOOK,   78, 92, 178 },
    { "NINTENDO",    "Nintendo.com as it looked in 2001", "https://web.archive.org/web/20010822153959if_/http://www.nintendo.com/", IC_GLOBE, 70, 146, 46 },
    { "MUSIQUE LIBRE", "GC Station : musique gratuite d'archive.org a telecharger", "gc:archive",          IC_GLOBE,  222, 104, 30 },
    { "68K.NEWS",    "Headlines made for vintage computers",     "http://68k.news/",                        IC_RETRO,  112, 66, 168 },
    { "NPR TEXT",    "News headlines, text only",                "https://text.npr.org/",                   IC_NEWS,   184, 50, 62 },
    { "CNN LITE",    "World news, text only",                    "https://lite.cnn.com/",                   IC_NEWS,   46, 132, 82 },
    { "FRANCEINFO",  "L'actualite en francais",                  "https://www.francetvinfo.fr/",            IC_NEWS,   214, 132, 20 },
};
#define NSITES ((int)(sizeof(sites) / sizeof(sites[0])))

static const char about_text[] =
    "CubeBrowser (by Arial-Bold) is GC Station's web browser: secure sites, pictures, forms and downloads "
    "to the SD card. Point with the Control Stick and press A to open a link. X types an address or "
    "searches. Z changes tab, START opens GC Station's menu. Light, text-based sites work best.";

static const char *const tips[] = {
    "Z changes tab, like everywhere in GC Station.",
    "Downloaded songs show up in the MUSIC tab right away.",
    "The D-Pad jumps between links.",
    "Y opens bookmarks. The star saves a page.",
    "The gear opens the browser settings: themes, text size...",
    "AI ASSISTANT: questions from the GameCube (key: phone page /ia).",
};
#define NTIPS ((int)(sizeof(tips) / sizeof(tips[0])))

static float intro_start = -100;

static bool au_place = true;                       /* (v214 : le pointeur va sur la ligne choisie) */
void home_reset(void) { intro_start = ui_time(); au_place = true; }

/* 0..1 entrance progress for item i (staggered fade) */
static float intro(int i) {
    if (!g_set.animations) return 1;
    float k = (ui_time() - intro_start - 0.04f * i) / 0.25f;
    if (k <= 0) return 0;
    if (k >= 1) return 1;
    return k;
}

/* Tile colouring:
 *   0 = each site its own colour
 *   1 = one periwinkle for every tile, white emblems (like the old site's panels)
 *   2 = one pale lavender for every tile, purple emblems (like its news rows) */
#ifndef HOME_TILE_STYLE
#define HOME_TILE_STYLE 1
#endif

static Color site_col(const Site *s, float m, float a) {
    float r = s->r * m, g = s->g * m, b = s->b * m;
    return rgba(r > 255 ? 255 : (u8)r, g > 255 ? 255 : (u8)g, b > 255 ? 255 : (u8)b, (u8)(255 * a));
}

static Color opaque(Color c) { c.a = 255; return c; }

/* ---- shapes */

/* Rectangle with the top-right and bottom-left corners cut at 45 degrees
 * (cut = 0 gives a plain rectangle). Drawn as a fan of triangles. */
static void cut_rect(float x, float y, float w, float h, float cut, Color c) {
    float p[12] = { x, y, x + w - cut, y, x + w, y + cut, x + w, y + h, x + cut, y + h, x, y + h - cut };
    float cx = x + w / 2, cy = y + h / 2;
    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        gfx_tri(cx, cy, p[i * 2], p[i * 2 + 1], p[j * 2], p[j * 2 + 1], c);
    }
}

static void cut_outline(float x, float y, float w, float h, float cut, float t, Color c) {
    float p[12] = { x, y, x + w - cut, y, x + w, y + cut, x + w, y + h, x + cut, y + h, x, y + h - cut };
    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        gfx_line(p[i * 2], p[i * 2 + 1], p[j * 2], p[j * 2 + 1], t, c);
    }
    for (int i = 0; i < 6; i++) gfx_rect(p[i * 2] - t / 2, p[i * 2 + 1] - t / 2, t, t, c);   /* square joints */
}

static void dotted_hline(float x, float y, float w, Color c) {
    for (float d = 0; d < w; d += 4) gfx_rect(x + d, y, 1.6f, 1.6f, c);
}

static void dashed_rect(float x, float y, float w, float h, Color c) {
    for (float d = 0; d < w; d += 6) {
        float e = fminf(d + 3, w);
        gfx_rect(x + d, y, e - d, 1.5f, c);
        gfx_rect(x + d, y + h - 1.5f, e - d, 1.5f, c);
    }
    for (float d = 0; d < h; d += 6) {
        float e = fminf(d + 3, h);
        gfx_rect(x, y + d, 1.5f, e - d, c);
        gfx_rect(x + w - 1.5f, y + d, 1.5f, e - d, c);
    }
}

/* ---- spaced capitals */
static float caps_width(float size, float spacing, const char *s) {
    float w = 0;
    for (const char *p = s; *p; p++) w += gfx_text_width(FONT_UI, size, p, 1) + spacing;
    return *s ? w - spacing : 0;
}

/* shadow.a == 0 means no shadow; the shadow is a hard 1 px offset (flat look) */
static void caps(float x, float y, float size, float spacing, Color c, Color shadow, const char *s) {
    for (int pass = shadow.a ? 0 : 1; pass < 2; pass++) {
        float cx = x + (pass ? 0 : 1), cy = y + (pass ? 0 : 1);
        Color col = pass ? c : shadow;
        for (const char *p = s; *p; p++) {
            gfx_text(FONT_UI, size, cx, cy, col, p, 1);
            gfx_text(FONT_UI, size, cx + 0.5f, cy, col, p, 1);   /* a touch bolder */
            cx += gfx_text_width(FONT_UI, size, p, 1) + spacing;
        }
    }
}

/* Word-wrapped text; returns the height used. With draw false it only measures. */
static float wrap(FontId f, float size, float x, float y, float w, Color c, const char *s, bool draw) {
    float lh = gfx_line_height(f, size) * 1.05f, yy = y;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        int n = gfx_text_fit(f, size, s, -1, w);
        if (s[n]) {                       /* break at the last space that fits */
            int b = n;
            while (b > 0 && s[b] != ' ') b--;
            if (b > 0) n = b;
        }
        if (n <= 0) n = 1;
        if (draw) gfx_text(f, size, x, yy, c, s, n);
        yy += lh;
        s += n;
    }
    return yy - y;
}

/* ---- emblems, flat single colour (plus a darker colour for details) */
static void draw_emblem(int icon, float cx, float cy, float s, Color c, Color dark) {
    switch (icon) {
    case IC_SEARCH:
        gfx_ring(cx - s * 0.08f, cy - s * 0.08f, s * 0.28f, s * 0.1f, 0, 6.2832f, c);
        gfx_line(cx + s * 0.12f, cy + s * 0.12f, cx + s * 0.38f, cy + s * 0.38f, s * 0.13f, c);
        break;
    case IC_BOOK: {
        float w = s * 0.42f, h = s * 0.56f;
        float l[8] = { cx - w, cy - h * 0.42f, cx - 1, cy - h * 0.5f, cx - 1, cy + h * 0.5f, cx - w, cy + h * 0.58f };
        float r[8] = { cx + 1, cy - h * 0.5f, cx + w, cy - h * 0.42f, cx + w, cy + h * 0.58f, cx + 1, cy + h * 0.5f };
        gfx_quad(l, c);
        gfx_quad(r, c);
        for (int i = 0; i < 3; i++) {
            float yy = cy - h * 0.22f + i * h * 0.22f;
            gfx_line(cx - w * 0.78f, yy, cx - w * 0.2f, yy - 2, 1.6f, dark);
            gfx_line(cx + w * 0.2f, yy - 2, cx + w * 0.78f, yy, 1.6f, dark);
        }
        break;
    }
    case IC_NEWS: {
        float w = s * 0.8f, h = s * 0.62f, x = cx - w / 2, y = cy - h / 2;
        gfx_rect(x, y, w, h, c);
        gfx_rect(x + w * 0.1f, y + h * 0.14f, w * 0.8f, h * 0.18f, dark);
        gfx_rect(x + w * 0.1f, y + h * 0.42f, w * 0.36f, h * 0.42f, dark);
        for (int i = 0; i < 3; i++) gfx_rect(x + w * 0.54f, y + h * (0.44f + i * 0.15f), w * 0.36f, 2, dark);
        break;
    }
    case IC_GLOBE: {
        float R = s * 0.36f;
        gfx_circle(cx, cy, R, c);
        for (int i = -1; i <= 1; i++) {   /* latitude lines */
            float yy = cy + i * R * 0.5f, half = sqrtf(R * R - (i * R * 0.5f) * (i * R * 0.5f)) * 0.92f;
            gfx_line(cx - half, yy, cx + half, yy, 1.6f, dark);
        }
        for (int e = 0; e < 2; e++) {     /* meridians */
            float rx = e ? R * 0.45f : R * 0.92f;
            for (int side = -1; side <= 1; side += 2) {
                float px = cx, py = cy - R * 0.92f;
                for (int k = 1; k <= 20; k++) {
                    float a = -1.5708f + side * 3.1416f * k / 20, nx = cx + cosf(a) * rx, ny = cy + sinf(a) * R * 0.92f;
                    gfx_line(px, py, nx, ny, 1.6f, dark);
                    px = nx;
                    py = ny;
                }
            }
        }
        break;
    }
    case IC_PENCIL: {   /* a single pencil, writing: the blog */
        float dx = 0.7071f, dy = -0.7071f, R = s * 0.11f, L = s * 0.86f;
        float tx = cx - dx * L / 2, ty = cy - dy * L / 2;          /* tip, lower left */
        float nx = -dy * R, ny = dx * R;
        float bx = tx + dx * s * 0.2f, by = ty + dy * s * 0.2f;    /* where the sharpened part starts */
        float ex = tx + dx * L, ey = ty + dy * L;                  /* top end */
        float ux = ex - dx * s * 0.13f, uy = ey - dy * s * 0.13f;  /* eraser band */
        float body[8] = { bx + nx, by + ny, ux + nx, uy + ny, ux - nx, uy - ny, bx - nx, by - ny };
        float eraser[8] = { ux + nx, uy + ny, ex + nx, ey + ny, ex - nx, ey - ny, ux - nx, uy - ny };
        gfx_quad(body, c);
        gfx_quad(eraser, dark);
        gfx_tri(tx, ty, bx + nx, by + ny, bx - nx, by - ny, c);
        float lx = tx + dx * s * 0.08f, ly = ty + dy * s * 0.08f;  /* lead */
        gfx_tri(tx, ty, lx + nx * 0.4f, ly + ny * 0.4f, lx - nx * 0.4f, ly - ny * 0.4f, dark);
        break;
    }
    case IC_RETRO: {
        float w = s * 0.72f, h = s * 0.52f, x = cx - w / 2, y = cy - h / 2 - s * 0.06f;
        gfx_rect(x, y, w, h, c);
        gfx_rect(x + 4, y + 4, w - 8, h - 8, dark);
        gfx_text(FONT_MONO, s * 0.22f, x + 8, y + h * 0.22f, c, ">_", 2);
        gfx_rect(cx - s * 0.08f, y + h, s * 0.16f, s * 0.08f, c);
        gfx_rect(cx - s * 0.26f, y + h + s * 0.08f, s * 0.52f, s * 0.07f, c);
        break;
    }
    case IC_CHAT: {
        float w = s * 0.74f, h = s * 0.46f, x = cx - w / 2, y = cy - h / 2 - s * 0.06f;
        gfx_rect(x, y, w, h, c);
        gfx_tri(x + w * 0.22f, y + h - 1, x + w * 0.44f, y + h - 1, x + w * 0.16f, y + h + s * 0.16f, c);
        for (int i = 0; i < 3; i++) gfx_rect(cx - s * 0.2f + i * s * 0.16f, y + h * 0.42f, s * 0.08f, s * 0.08f, dark);
        break;
    }
    }
}

static bool clicked(bool hov, bool input_ok) {
    if (hov && input_ok && input_pressed(ACT_CLICK)) {
        input_consume(ACT_CLICK);
        input_rumble(3);
        return true;
    }
    return false;
}

/* ================================================ GC Station v214 : Aurore
 * L'accueil dans GC Station, comme l'onglet RADIO : a gauche la liste (actions, puis les sites), a droite la
 * fiche de la ligne choisie et les derniers favoris. Croix : de ligne en ligne ; A : ouvrir. */
static const char *const au_site_names[] = {
    "DuckDuckGo", "Assistant IA", "Wikip\xE9" "dia", "Nintendo 2001", "Musique libre", "68k.news", "NPR Text", "CNN Lite", "Franceinfo",
};
enum { AU_ABOUT, AU_SEARCH, AU_BOOKMARKS, AU_SETTINGS, AU_HELP, AU_NACT };   /* (AU_ABOUT : Cube Browser et son auteur, en premier) */
static int au_sel_i = AU_ABOUT;
extern bool ui_cursor_rest;

static void au_icon(int row, float cx, float cy, float s, Color c, Color dark) {
    if (row == AU_ABOUT) {                                   /* un petit cube (trois faces) */
        float e = s * 0.42f;
        float top[8] = { cx, cy - e, cx + e, cy - e * 0.5f, cx, cy, cx - e, cy - e * 0.5f };
        float left[8] = { cx - e, cy - e * 0.5f, cx, cy, cx, cy + e, cx - e, cy + e * 0.5f };
        float right[8] = { cx, cy, cx + e, cy - e * 0.5f, cx + e, cy + e * 0.5f, cx, cy + e };
        gfx_quad(top, c);
        gfx_quad(left, color_alpha(c, 0.75f));
        gfx_quad(right, color_alpha(c, 0.5f));
    } else if (row == AU_BOOKMARKS) ui_draw_icon(ICON_STAR, cx, cy, s * 1.15f, c);
    else if (row == AU_SETTINGS) ui_draw_icon(ICON_GEAR, cx, cy, s * 1.1f, c);
    else if (row == AU_SEARCH) draw_emblem(IC_SEARCH, cx, cy, s, c, dark);
    else if (row == AU_HELP) draw_emblem(IC_BOOK, cx, cy, s, c, dark);
    else draw_emblem(sites[row - AU_NACT].icon, cx, cy, s, c, dark);
}
static const char *au_row_name(int row) {
    switch (row) {
    case AU_ABOUT: return "Cube Browser";
    case AU_SEARCH: return tr("Address or search");
    case AU_BOOKMARKS: return tr("Bookmarks");
    case AU_SETTINGS: return tr("Browser settings");
    case AU_HELP: return tr("Controls & Tips");
    default: return au_site_names[row - AU_NACT];
    }
}
static float au_fit_size(FontId f, float size, const char *s, float w) {
    float tw = gfx_text_width(f, size, s, -1);
    return tw > w ? fmaxf(size * 0.6f, size * w / tw) : size;
}

static HomeAction home_draw_au(float x, float y, float w, float h, bool input_ok, const char **url, const char **hover_url) {
    const Theme *t = theme();
    HomeAction act = HOME_NONE;
    float k = au_kx();
    const int nrows = AU_NACT + NSITES;
    if (au_sel_i >= nrows) au_sel_i = AU_NACT;
    (void)w;

    /* ---- liste (a gauche) */
    float lx = x, lw = 271 * k, ly = y, lh = h;
    static int last_frame = -100;
    static bool was_input = false;
    if (ui_frame() - last_frame > 2 || (input_ok && !was_input)) au_place = true;   /* (revenu sur l'accueil : le pointeur retourne sur la ligne choisie) */
    last_frame = ui_frame(); was_input = input_ok;
    if (au_place && input_ok) {
        float sy = ly + 11 + au_sel_i * 26 + (au_sel_i >= AU_NACT ? 9 : 0);
        input_set_cursor(lx + 7 + (lw - 14) - 26, sy + 12);
        au_place = false;
    }
    au_glass(lx, ly, lw, lh, 22);
    float row_h = 26, ry = ly + 11;
    for (int i = 0; i < nrows; i++) {
        if (i == AU_NACT) {                                   /* separation actions / sites */
            gfx_rect(lx + 16, ry + 3, lw - 32, 1, rgba(255, 255, 255, 26));
            ry += 9;
        }
        float rx = lx + 7, rw = lw - 14, rh = 24;
        UiId id = ui_id("au-row", i);
        bool hov = ui_hover(id, rx, ry, rw, rh);
        if (hov) { au_sel_i = i; ui_cursor_rest = true; }   /* (comme les listes de GC Station : sans pointeur) */
        bool on = i == au_sel_i;
        float a = ui_anim(id, on ? 1.0f : 0.0f, 0.3f);
        au_sel(rx, ry, rw, rh, 12, a);
        Color tc = on ? rgba(255, 255, 255, 255) : (i < AU_NACT ? rgba(240, 238, 250, 255) : rgba(214, 212, 230, 255));
        au_icon(i, rx + 16, ry + rh * 0.5f, 16, on ? rgba(255, 255, 255, 255) : rgba(255, 255, 255, i < AU_NACT ? 235 : 170),
                on ? au_ca(255) : rgba(14, 12, 28, 255));
        const char *nm = au_row_name(i);
        float ts = au_fit_size(FONT_UI, 14, nm, rw - 46);
        gfx_text(FONT_UI, ts, rx + 34, ry + (rh - gfx_line_height(FONT_UI, ts)) * 0.5f, tc, nm, -1);
        if (hov && i >= AU_NACT) *hover_url = sites[i - AU_NACT].url;
        if (input_ok && hov && input_pressed(ACT_CLICK)) {
            input_consume(ACT_CLICK);
            input_rumble(3);
            if (i == AU_ABOUT) { act = HOME_OPEN_URL; *url = "https://github.com/Arial-Bold/Cubebrowser"; }
            else if (i == AU_SEARCH) act = HOME_SEARCH;
            else if (i == AU_BOOKMARKS) act = HOME_BOOKMARKS;
            else if (i == AU_SETTINGS) act = HOME_SETTINGS;
            else if (i == AU_HELP) act = HOME_HELP;
            else { act = HOME_OPEN_URL; *url = sites[i - AU_NACT].url; }
        }
        ry += row_h;
    }

    /* ---- fiche (a droite) */
    float px = x + 278 * k, pw = 341 * k, py = y, ph = 338;
    au_glass(px, py, pw, ph, 22);
    int sel = au_sel_i;
    const char *cap = sel == AU_ABOUT ? tr("THE BROWSER OF THIS TAB") : sel >= AU_NACT ? tr("FEATURED SITE") : tr("BROWSER");
    gfx_circle(px + 22, py + 21, 3.5f, au_ca(255));
    gfx_text(FONT_UI, 11, px + 32, py + 14, t->text_dim, cap, -1);
    float tx = px + 18, ty = py + 38, ts = 96;
    gfx_rrect(tx - 4, ty - 4, ts + 8, ts + 8, 26, au_ca(40));                 /* lueur */
    gfx_rrect_vgrad(tx, ty, ts, ts, 22, au_ca(255), au_cb(255));
    gfx_rrect_vgrad(tx + 4, ty + 3, ts - 8, ts * 0.42f, 18, rgba(255, 255, 255, 50), rgba(255, 255, 255, 0));
    au_icon(sel, tx + ts * 0.5f, ty + ts * 0.5f, 58, rgba(255, 255, 255, 255), au_ca(255));
    float cx = tx + ts + 16, cw = px + pw - 16 - cx;
    if (sel == AU_ABOUT) {                                   /* Cube Browser : son auteur, et merci */
        gfx_text(FONT_UI, 22, cx, ty + 4, rgba(255, 255, 255, 255), "Cube Browser", -1);
        gfx_text(FONT_UI, 13, cx, ty + 36, au_ca(255), tr("by Arial-Bold"), -1);
        float dy = wrap(FONT_BODY, 12, cx, ty + 58, cw, t->text_dim, tr("Free software (MIT licence), made for the GameCube."), true);
        (void)dy;
        float my = ty + ts + 16;
        gfx_rect(px + 18, my, pw - 36, 1, rgba(255, 255, 255, 26));
        my += 12;
        float mh = wrap(FONT_BODY, 13.5f, px + 18, my, pw - 36, rgba(232, 230, 246, 255),
                        tr("This tab is Arial-Bold's web browser, Cube Browser, which GC Station includes and adapts. "
                           "Pages, pictures, secure sites, forms and downloads: it's all their work. Thank you, Arial-Bold!"), true);
        my += mh + 10;
        const char *gh = "github.com/Arial-Bold/Cubebrowser";
        float uw = gfx_text_width(FONT_UI, 11, gh, -1) + 20;
        gfx_rrect(px + 18, my, uw, 20, 10, rgba(255, 255, 255, 18));
        gfx_rrect_outline(px + 18, my, uw, 20, 10, 1.0f, rgba(255, 255, 255, 36));
        gfx_text(FONT_UI, 11, px + 28, my + (20 - gfx_line_height(FONT_UI, 11)) * 0.5f, rgba(220, 218, 236, 255), gh, -1);
        gfx_text(FONT_UI, 11, px + 18 + uw + 10, my + (20 - gfx_line_height(FONT_UI, 11)) * 0.5f, t->text_dim, tr("A: open"), -1);
        return act;
    }
    const char *title = au_row_name(sel);
    float tsz = au_fit_size(FONT_UI, 22, title, cw);
    gfx_text(FONT_UI, tsz, cx, ty + 4, rgba(255, 255, 255, 255), title, -1);
    const char *desc = sel == AU_SEARCH ? tr("Type an address (www...) or words to search for.")
                     : sel == AU_BOOKMARKS ? tr("The pages you keep. Y opens them from anywhere.")
                     : sel == AU_SETTINGS ? tr("Language, text size, pictures, cookies...")
                     : sel == AU_HELP ? tr("Every button of the controller and the keyboard.")
                     : tr(sites[sel - AU_NACT].blurb);
    float dh = wrap(FONT_BODY, 13, cx, ty + 36, cw, t->text_dim, desc, true);
    if (sel >= AU_NACT) {                                    /* l'adresse, en petite pastille */
        char u[64];
        const char *su = sites[sel - AU_NACT].url;
        if (!strncmp(su, "https://", 8)) su += 8; else if (!strncmp(su, "http://", 7)) su += 7;
        snprintf(u, sizeof(u), "%s", su);
        char *sl = strchr(u, '/');
        if (sl && strncmp(u, "web.archive", 11)) *sl = 0;
        if (!strncmp(u, "web.archive", 11)) snprintf(u, sizeof(u), "web.archive.org");
        float uw = gfx_text_width(FONT_UI, 11, u, -1) + 20, uy = ty + 42 + dh;
        gfx_rrect(cx, uy, uw, 20, 10, rgba(255, 255, 255, 18));
        gfx_rrect_outline(cx, uy, uw, 20, 10, 1.0f, rgba(255, 255, 255, 36));
        gfx_text(FONT_UI, 11, cx + 10, uy + (20 - gfx_line_height(FONT_UI, 11)) * 0.5f, rgba(220, 218, 236, 255), u, -1);
    }

    /* derniers favoris */
    float by = ty + ts + 22;
    gfx_rect(px + 18, by, pw - 36, 1, rgba(255, 255, 255, 26));
    gfx_text(FONT_UI, 11, px + 18, by + 10, t->text_dim, tr("RECENT BOOKMARKS"), -1);
    int nb = browser_bookmark_count();
    float fy = by + 32;
    if (nb == 0)
        wrap(FONT_BODY, 13, px + 18, fy + 4, pw - 36, rgba(150, 148, 176, 255), tr("No bookmarks yet: press Y on a page to keep it."), true);
    for (int i = 0; i < 3 && i < nb; i++) {
        const char *bt, *bu;
        browser_bookmark(nb - 1 - i, &bt, &bu);
        float rh = 34, rw = pw - 36, rx = px + 18;
        UiId id = ui_id("au-bm", i);
        bool hov = ui_hover(id, rx, fy, rw, rh);
        float a = ui_anim(id, hov ? 1.0f : 0.0f, 0.3f);
        gfx_rrect(rx, fy, rw, rh, 11, rgba(255, 255, 255, 10));
        gfx_rrect_outline(rx, fy, rw, rh, 11, 1.0f, a > 0.01f ? color_lerp(rgba(255, 255, 255, 20), au_ca(220), a) : rgba(255, 255, 255, 20));
        int n1 = gfx_text_fit(FONT_UI, 13, bt, -1, rw - 24);
        gfx_text(FONT_UI, 13, rx + 12, fy + 3, rgba(255, 255, 255, 255), bt, n1);
        const char *bs = bu;
        if (!strncmp(bs, "https://", 8)) bs += 8; else if (!strncmp(bs, "http://", 7)) bs += 7;
        int n2 = gfx_text_fit(FONT_BODY, 10.5f, bs, -1, rw - 24);
        gfx_text(FONT_BODY, 10.5f, rx + 12, fy + 18, rgba(130, 128, 156, 255), bs, n2);
        if (hov) *hover_url = bu;
        if (input_ok && hov && input_pressed(ACT_CLICK)) { input_consume(ACT_CLICK); act = HOME_OPEN_URL; *url = bu; }
        fy += rh + 6;
    }
    return act;
}

HomeAction home_draw(float x, float y, float w, float h, bool input_ok, const char **url, const char **hover_url) {
    if (au_on()) { *url = NULL; *hover_url = NULL; return home_draw_au(x, y, w, h, input_ok, url, hover_url); }
    const Theme *t = theme();
    HomeAction act = HOME_NONE;
    *url = NULL;
    *hover_url = NULL;
    const char *hover_blurb = NULL;
    float now = ui_time();

    /* palette: flat versions of the theme colours plus fixed navy and orange */
    Color base = opaque(color_lerp(t->panel_bottom, t->bg_top, 0.25f));   /* page background */
    Color panel = opaque(t->panel_top);                                     /* lighter blocks */
    Color navy = rgba(26, 24, 66, 255);                                     /* bars, outlines */
    Color gold = opaque(t->accent);
    Color orange = rgba(240, 132, 24, 255);
    Color white = rgba(255, 255, 255, 255);
    Color shadow = rgba(20, 16, 50, 200);

    gfx_rrect(x, y, w, h, 14, base);
    gfx_set_clip(x, y, w, h);

    float m = 10, hx = x + m, hw = w - 2 * m, cy = y + m;

    /* ---- header bar: navy band, gold title, welcome bubble */
    float a0 = intro(0);
    float bar_h = 34;
    cut_rect(hx, cy, hw, bar_h, 12, color_alpha(navy, a0));
    /* emblem: a small flat cube (three faces) */
    {
        float ex = hx + 22, ey = cy + bar_h / 2, s = 11;
        float top[8] = { ex, ey - s, ex + s, ey - s * 0.5f, ex, ey, ex - s, ey - s * 0.5f };
        float left[8] = { ex - s, ey - s * 0.5f, ex, ey, ex, ey + s, ex - s, ey + s * 0.5f };
        float right[8] = { ex, ey, ex + s, ey - s * 0.5f, ex + s, ey + s * 0.5f, ex, ey + s };
        gfx_quad(top, color_alpha(rgba(170, 160, 240, 255), a0));
        gfx_quad(left, color_alpha(rgba(110, 96, 210, 255), a0));
        gfx_quad(right, color_alpha(rgba(76, 62, 168, 255), a0));
    }
    float tsz = w < 700 ? 21 : 23;
    caps(hx + 42, cy + (bar_h - gfx_line_height(FONT_UI, tsz)) / 2, tsz, 0.8f, color_alpha(gold, a0),
         color_alpha(rgba(0, 0, 0, 255), a0), "CubeBrowser");
    float title_end = hx + 42 + caps_width(tsz, 0.8f, "CubeBrowser");
    /* the welcome bubble, or a clickable notice when an update is ready */
    char bubble[48];
    UpdateState us = update_state();
    bool upd = us == UPD_AVAILABLE || us == UPD_INSTALLING || us == UPD_INSTALLED;
    if (us == UPD_INSTALLED) snprintf(bubble, sizeof(bubble), tr("EXIT TO SWISS FOR v%s"), update_latest_version());
    else if (upd) snprintf(bubble, sizeof(bubble), tr("UPDATE AVAILABLE: v%s"), update_latest_version());
    else snprintf(bubble, sizeof(bubble), tr("WELCOME TO v%s"), CB_VERSION);
    float bub_w = caps_width(9.5f, 1.1f, bubble) + 20, bub_h = 22;
    float bub_x = hx + hw - 22 - bub_w, bub_y = cy + (bar_h - bub_h) / 2;
    if (bub_x > title_end + 20) {
        bool bhov = upd && ui_hover(ui_id("home-update", 0), bub_x - 8, bub_y, bub_w + 8, bub_h);
        Color bg = upd ? (bhov ? white : gold) : white;
        if (upd && g_set.animations && !bhov) bg = color_lerp(gold, white, 0.25f + 0.25f * sinf(ui_time() * 3));   /* gentle pulse */
        gfx_rect(bub_x, bub_y, bub_w, bub_h, color_alpha(bg, a0));
        gfx_tri(bub_x + 1, bub_y + 6, bub_x + 1, bub_y + 15, bub_x - 8, bub_y + 12, color_alpha(bg, a0));
        dashed_rect(bub_x + 3, bub_y + 3, bub_w - 6, bub_h - 6, color_alpha(navy, a0));
        caps(bub_x + 10, bub_y + (bub_h - gfx_line_height(FONT_UI, 9.5f)) / 2, 9.5f, 1.1f, color_alpha(navy, a0),
             rgba(0, 0, 0, 0), bubble);
        if (upd && clicked(bhov, input_ok)) { act = HOME_OPEN_URL; *url = "about:update"; }
        if (bhov) *hover_url = "about:update";
    }
    cy += bar_h + 8;

    /* ---- about panel */
    float a1 = intro(1);
    float tsz_body = w < 700 ? 13.0f : 14.5f;
    float text_w = hw - 24;
    float text_h = wrap(FONT_BODY, tsz_body, 0, 0, text_w - 4, white, tr(about_text), false);   /* same width as drawn */
    float about_h = text_h + 14;
    cut_rect(hx, cy, hw, about_h, 8, color_alpha(color_lerp(panel, white, 0.62f), a1));   /* pale, for easy reading */
    cut_outline(hx, cy, hw, about_h, 8, 1.5f, color_alpha(navy, a1));
    gfx_rect(hx + 1, cy + 1, 5, about_h - 2 - 8, color_alpha(orange, a1));   /* accent edge */
    wrap(FONT_BODY, tsz_body, hx + 15, cy + 7, text_w - 4, color_alpha(navy, a1), tr(about_text), true);
    cy += about_h + 7;

    /* ---- featured sites */
    float a2 = intro(2);
    caps(hx + 2, cy, 15, 1.0f, color_alpha(white, a2), color_alpha(shadow, a2), tr("FEATURED SITES"));
    float dots_x = hx + 2 + caps_width(15, 1.0f, tr("FEATURED SITES")) + 14;
    dotted_hline(dots_x, cy + 9, hx + hw - dots_x, color_alpha(panel, 0.8f * a2));
    cy += 21;

    float row_h = 24, gap = 8;
    int cols = 4, rows = (NSITES + cols - 1) / cols;
    float bottom = y + h - m;
    float grid_bottom = bottom - row_h - 8;
    float th = (grid_bottom - cy - gap * (rows - 1)) / rows;
    if (th > 100) th = 100;
    if (th < 44) th = 44;
    float tw = (hw - gap * (cols - 1)) / cols;
    float label_h = fminf(22, th * 0.36f);

    for (int i = 0; i < NSITES; i++) {
        const Site *s = &sites[i];
        float ap = intro(3 + i);
        if (ap <= 0) continue;
        float bx = hx + (i % cols) * (tw + gap), by = cy + (i / cols) * (th + gap);
        UiId id = ui_id("home-tile", i);
        bool hov = ui_hover(id, bx, by, tw, th);
        float art_h = th - label_h;
        /* hard drop shadow, then the flat art block */
        gfx_rect(bx + 3, by + 3, tw, th, color_alpha(rgba(10, 8, 30, 140), ap));
        Color fill, edge, em, em_dark;
#if HOME_TILE_STYLE == 1
        fill = hov ? rgba(146, 154, 222, 255) : rgba(122, 130, 202, 255);
        edge = rgba(176, 182, 236, 255);
        em = white;
        em_dark = rgba(70, 74, 150, 255);
#elif HOME_TILE_STYLE == 2
        fill = hov ? rgba(255, 255, 255, 255) : rgba(228, 229, 244, 255);
        edge = rgba(255, 255, 255, 255);
        em = rgba(88, 76, 170, 255);
        em_dark = rgba(228, 229, 244, 255);
#else
        fill = site_col(s, hov ? 1.15f : 1.0f, 1);
        edge = site_col(s, 1.35f, 1);
        em = white;
        em_dark = site_col(s, 0.6f, 1);
#endif
        gfx_rect(bx, by, tw, art_h, color_alpha(fill, ap));
        gfx_rect(bx, by, tw, 3, color_alpha(edge, ap));                       /* light top edge */
        float es = fminf(art_h * 0.9f, tw * 0.45f);
        draw_emblem(s->icon, bx + tw / 2, by + art_h / 2 + 1, es, color_alpha(em, ap), color_alpha(em_dark, ap));
        /* caption bar with the orange arrow square */
        gfx_rect(bx, by + art_h, tw, label_h, color_alpha(navy, ap));
        float as = label_h - 6, ax = bx + tw - as - 3, ay = by + art_h + 3;
        gfx_rect(ax, ay, as, as, color_alpha(hov ? gold : orange, ap));
        gfx_tri(ax + as * 0.34f, ay + as * 0.22f, ax + as * 0.34f, ay + as * 0.78f, ax + as * 0.76f, ay + as * 0.5f,
                color_alpha(navy, ap));
        float ns = 11, nsp = 1.0f, room = tw - 8 - as - 8;
        while (ns > 8 && caps_width(ns, nsp, s->name) > room) ns -= 0.5f;
        caps(bx + 7, by + art_h + (label_h - gfx_line_height(FONT_UI, ns)) / 2, ns, nsp,
             color_alpha(hov ? gold : white, ap), rgba(0, 0, 0, 0), s->name);
        /* outline: thin navy, or a thick gold frame when pointed at */
        if (hov) {
            float k = 3;
            gfx_rect(bx - k, by - k, tw + 2 * k, k, gold);
            gfx_rect(bx - k, by + th, tw + 2 * k, k, gold);
            gfx_rect(bx - k, by, k, th, gold);
            gfx_rect(bx + tw, by, k, th, gold);
        } else {
            gfx_rect(bx, by, tw, 1.5f, color_alpha(navy, ap));
            gfx_rect(bx, by + th - 1.5f, tw, 1.5f, color_alpha(navy, ap));
            gfx_rect(bx, by, 1.5f, th, color_alpha(navy, ap));
            gfx_rect(bx + tw - 1.5f, by, 1.5f, th, color_alpha(navy, ap));
        }
        if (hov) { *hover_url = s->url; hover_blurb = tr(s->blurb); }
        if (clicked(hov, input_ok)) { act = HOME_OPEN_URL; *url = s->url; }
    }

    /* ---- bottom row: Controls & Tips button, then the tip line */
    float a3 = intro(3 + NSITES);
    float ry = bottom - row_h;
    const char *blabel = tr("CONTROLS & TIPS");
    float bw = caps_width(10.5f, 1.2f, blabel) + 40;
    UiId bid = ui_id("home-help", 0);
    bool bhov = ui_hover(bid, hx, ry, bw, row_h);
    cut_rect(hx, ry, bw, row_h, 7, color_alpha(bhov ? gold : panel, a3));
    cut_outline(hx, ry, bw, row_h, 7, 1.5f, color_alpha(navy, a3));
    {   /* arrow in a navy circle, as on the old "take our survey" buttons */
        float ax = hx + 14, ay = ry + row_h / 2;
        gfx_circle(ax, ay, 7, color_alpha(navy, a3));
        gfx_tri(ax - 2.5f, ay - 4, ax - 2.5f, ay + 4, ax + 4, ay, color_alpha(bhov ? navy : white, a3));
        if (!bhov) gfx_tri(ax - 2.5f, ay - 4, ax - 2.5f, ay + 4, ax + 4, ay, color_alpha(gold, a3));
    }
    caps(hx + 28, ry + (row_h - gfx_line_height(FONT_UI, 10.5f)) / 2, 10.5f, 1.2f, color_alpha(navy, a3), rgba(0, 0, 0, 0), blabel);
    if (clicked(bhov, input_ok)) act = HOME_HELP;

    float lx = hx + bw + 10, lw = hw - bw - 10;
    gfx_rect(lx, ry, lw, row_h, color_alpha(navy, 0.85f * a3));
    const char *tag = hover_blurb ? tr("SITE") : tr("TIP");
    float tag_w = caps_width(9.5f, 1.2f, tag) + 14;
    gfx_rect(lx + 3, ry + 3, tag_w, row_h - 6, color_alpha(orange, a3));
    caps(lx + 10, ry + (row_h - gfx_line_height(FONT_UI, 9.5f)) / 2, 9.5f, 1.2f, color_alpha(navy, a3), rgba(0, 0, 0, 0), tag);
    char line[200];
    float fade = 1;
    if (hover_blurb) {
        snprintf(line, sizeof(line), "%s", hover_blurb);
    } else {
        float period = 6.0f, ph = fmodf(now, period);
        snprintf(line, sizeof(line), "%s", tr(tips[(int)(now / period) % NTIPS]));
        if (g_set.animations) fade = fminf(1, fminf(ph / 0.4f, (period - ph) / 0.4f));
    }
    float txs = 13;
    float tx = lx + 3 + tag_w + 8, tw2 = lw - (tx - lx) - 8;
    float lw0 = gfx_text_width(FONT_UI, txs, line, -1);       /* (GC Station : astuces plus longues en francais) */
    if (lw0 > tw2) txs = fmaxf(10.0f, txs * tw2 / lw0);
    gfx_text(FONT_UI, txs, tx, ry + (row_h - gfx_line_height(FONT_UI, txs)) / 2, color_alpha(white, a3 * fade), line,
             gfx_text_fit(FONT_UI, txs, line, -1, tw2));

    gfx_clear_clip();
    return act;
}

/* ======================================================== Controls & Tips
 * about:help, drawn in the same style as the start page. Badges show the
 * buttons as currently assigned in Settings > Controller. Taller than the
 * screen, so it scrolls with the C-Stick, L/R and the D-Pad. */

static float help_scroll, help_scroll_target;

void home_help_reset(void) { intro_start = ui_time(); help_scroll = help_scroll_target = 0; }

typedef struct { const char *badge; const char *text; } HelpRow;

static const char *bind_name(int act) {
    int b = g_set.bind[act];
    return (b >= 0 && b < BTN_COUNT) ? button_names[b] : "?";
}

/* One control: navy badge with the button in gold, then the description. */
static void help_row(float x, float y, float w, float h, const char *badge, const char *text, float a) {
    if (au_style()) {                                   /* v214 : Aurore : rangee de verre, bouton en pastille */
        float bw = fminf(84, w * 0.3f);
        gfx_rrect(x, y, w, h, h * 0.5f, rgba(255, 255, 255, (u8)(12 * a)));
        au_pill(x, y, bw, h, au_ca((u8)(210 * a)), au_cb((u8)(210 * a)));
        float bs = 11.5f;
        while (bs > 8 && caps_width(bs, 1.0f, badge) > bw - 12) bs -= 0.5f;
        caps(x + (bw - caps_width(bs, 1.0f, badge)) / 2, y + (h - gfx_line_height(FONT_UI, bs)) / 2, bs, 1.0f,
             rgba(255, 255, 255, (u8)(255 * a)), rgba(0, 0, 0, 0), badge);
        float ts = 13.5f, room = w - bw - 16;
        while (ts > 10 && gfx_text_width(FONT_BODY, ts, text, -1) > room) ts -= 0.5f;
        gfx_text(FONT_BODY, ts, x + bw + 11, y + (h - gfx_line_height(FONT_BODY, ts)) / 2, rgba(236, 234, 248, (u8)(255 * a)), text,
                 gfx_text_fit(FONT_BODY, ts, text, -1, room));
        return;
    }
    Color navy = rgba(26, 24, 66, 255), gold = opaque(theme()->accent);
    Color pale = opaque(color_lerp(theme()->panel_top, rgba(255, 255, 255, 255), 0.62f));
    float bw = fminf(84, w * 0.3f);
    gfx_rect(x + 2, y + 2, w, h, color_alpha(rgba(10, 8, 30, 255), 0.45f * a));    /* hard shadow */
    gfx_rect(x, y, bw, h, color_alpha(navy, a));
    gfx_rect(x + bw, y, w - bw, h, color_alpha(pale, a));
    gfx_rect(x + bw, y, 4, h, color_alpha(rgba(240, 132, 24, 255), a));              /* orange edge */
    float bs = 11.5f;
    while (bs > 8 && caps_width(bs, 1.0f, badge) > bw - 10) bs -= 0.5f;
    caps(x + (bw - caps_width(bs, 1.0f, badge)) / 2, y + (h - gfx_line_height(FONT_UI, bs)) / 2, bs, 1.0f,
         color_alpha(gold, a), rgba(0, 0, 0, 0), badge);
    float ts = 13.5f, room = w - bw - 16;
    while (ts > 10 && gfx_text_width(FONT_BODY, ts, text, -1) > room) ts -= 0.5f;
    gfx_text(FONT_BODY, ts, x + bw + 11, y + (h - gfx_line_height(FONT_BODY, ts)) / 2, color_alpha(navy, a), text,
             gfx_text_fit(FONT_BODY, ts, text, -1, room));
}

/* Section heading with a dotted rule, as on the start page. */
static void help_heading(float x, float y, float w, const char *text, float a) {
    Color white = rgba(255, 255, 255, 255), shadow = rgba(20, 16, 50, 200);
    if (au_style()) {
        caps(x + 2, y, 13, 1.6f, rgba(169, 167, 194, (u8)(255 * a)), rgba(0, 0, 0, 0), text);
        float dx = x + 2 + caps_width(13, 1.6f, text) + 12;
        gfx_rect_hgrad(dx, y + 8, (x + w - dx) * 0.5f, 1, au_ca((u8)(140 * a)), au_cb((u8)(50 * a)));
        return;
    }
    caps(x + 2, y, 15, 1.0f, color_alpha(white, a), color_alpha(shadow, a), text);
    float dx = x + 2 + caps_width(15, 1.0f, text) + 14;
    dotted_hline(dx, y + 9, x + w - dx, color_alpha(opaque(theme()->panel_top), 0.8f * a));
}

/* Lays out (and with draw set, draws) the scrolling part; returns its height. */
static float help_body(float x, float y, float w, bool draw) {
    char zbadge[40], lr[40];
    snprintf(zbadge, sizeof(zbadge), "%s + %s", bind_name(ACT_SLOW), bind_name(ACT_CLICK));
    snprintf(lr, sizeof(lr), "%s / %s", bind_name(ACT_PAGE_UP), bind_name(ACT_PAGE_DOWN));
    HelpRow pad[] = {
        { "STICK", "Move the pointer" },
        { bind_name(ACT_CLICK), tr("Click a link or button") },
        { bind_name(ACT_SLOW), tr("Hold for a slow pointer") },
        { bind_name(ACT_BACK), tr("Go back a page") },
        { bind_name(ACT_ADDRESS), tr("Type an address or search") },
        { bind_name(ACT_BOOKMARKS), tr("Bookmarks") },
        { bind_name(ACT_MENU), tr("Settings") },
        { "C-STICK", "Scroll the page" },
        { lr, tr("Page up / page down") },
        { "D-PAD", "Jump between links" },
        { zbadge, tr("Save a link to the SD card") },
    };
    HelpRow kb[] = {
        { "B", "Delete" }, { "Y", "Space" }, { "X", "Shift" },
        { "L / R", "Move the text cursor" }, { "START", "Accept the text" },
    };
    static const char *const tips_text[] = {
        "Downloads: links to files such as .dol, .iso or .zip offer to save them to the SD card. Zip files "
        "can be unzipped right away, and an interrupted download continues where it stopped.",
        "What works: text, links, pictures, tables, lists and forms such as search boxes. JavaScript isn't "
        "supported, so sites are shown in a simpler layout. Light, text-based sites work best.",
        "Music: put .mid files in a folder called midi on your SD card and press the note button in the "
        "toolbar. The music keeps playing while you browse. Add a SoundFont (.sf2) there for real instruments.",
        "Every button except the D-Pad can be changed in Settings > Controls.",
    };
    float gap = 8, rh = 26, cw = (w - gap) / 2, yy = y;
    int n = 0;
    for (int i = 0; i < (int)(sizeof(pad) / sizeof(pad[0])); i++) {   /* (GC Station garde Z et START : lignes retirees) */
        int act = i == 2 ? ACT_SLOW : i == 6 ? ACT_MENU : i == 10 ? ACT_SLOW : -1;
        if (act >= 0 && (g_set.bind[act] == BTN_Z || g_set.bind[act] == BTN_START)) continue;
        pad[n++] = pad[i];
    }
    if (draw) help_heading(x, yy, w, tr("CONTROLLER"), intro(1));
    yy += 22;
    for (int i = 0; i < n; i++) {
        float rx = x + (i % 2) * (cw + gap), ry = yy + (i / 2) * (rh + 6);
        if (draw) help_row(rx, ry, cw, rh, tr(pad[i].badge), tr(pad[i].text), intro(2 + i / 2));
    }
    yy += ((n + 1) / 2) * (rh + 6) + 8;
    n = (int)(sizeof(kb) / sizeof(kb[0]));
    if (draw) help_heading(x, yy, w, tr("ON-SCREEN KEYBOARD"), intro(8));
    yy += 22;
    for (int i = 0; i < n; i++) {
        float rx = x + (i % 2) * (cw + gap), ry = yy + (i / 2) * (rh + 6);
        if (draw) help_row(rx, ry, cw, rh, tr(kb[i].badge), tr(kb[i].text), intro(9));
    }
    yy += ((n + 1) / 2) * (rh + 6) + 8;
    if (draw) help_heading(x, yy, w, tr("TIPS"), intro(10));
    yy += 22;
    float ts = w < 700 ? 13.0f : 14.0f, tw = w - 28, th = 0;
    for (int i = 0; i < 3; i++) th += wrap(FONT_BODY, ts, 0, 0, tw, rgba(0, 0, 0, 255), tr(tips_text[i]), false) + 6;
    float ph = th + 10;
    if (draw) {
        float a = intro(11);
        Color navy = rgba(26, 24, 66, 255);
        Color pale = opaque(color_lerp(theme()->panel_top, rgba(255, 255, 255, 255), 0.62f));
        if (au_style()) {
            navy = rgba(214, 212, 230, 255);
            gfx_rrect(x, yy, w, ph, 14, rgba(255, 255, 255, (u8)(10 * a)));
            gfx_rect_hgrad(x + 14, yy, w * 0.4f, 1.2f, au_ca((u8)(150 * a)), au_cb(0));
        } else {
        cut_rect(x, yy, w, ph, 8, color_alpha(pale, a));
        cut_outline(x, yy, w, ph, 8, 1.5f, color_alpha(navy, a));
        gfx_rect(x + 1, yy + 1, 5, ph - 2 - 8, color_alpha(rgba(240, 132, 24, 255), a));
        }
        float ty = yy + 7;
        for (int i = 0; i < 3; i++) ty += wrap(FONT_BODY, ts, x + 15, ty, tw, color_alpha(navy, a), tr(tips_text[i]), true) + 6;
    }
    yy += ph + 10;
    return yy - y;
}

HomeAction home_help_draw(float x, float y, float w, float h, bool input_ok) {
    const Theme *t = theme();
    HomeAction act = HOME_NONE;
    Color base = opaque(color_lerp(t->panel_bottom, t->bg_top, 0.25f));
    Color panel = opaque(t->panel_top);
    Color navy = rgba(26, 24, 66, 255), gold = opaque(t->accent), white = rgba(255, 255, 255, 255);
    const bool au = au_style();
    if (!au) gfx_rrect(x, y, w, h, 14, base);
    gfx_set_clip(x, y, w, h);
    float m = 10, hx = x + m, hw = w - 2 * m, cy = y + m;

    /* header bar, as on the start page */
    float a0 = intro(0), bar_h = 34;
    if (au) {                                            /* v214 : Aurore : titre, et B pour revenir */
        gfx_text(FONT_UI, 20, hx + 6, cy + 4, white, tr("Controls & Tips"), -1);
        gfx_rect_hgrad(hx + 6, cy + 32, 46, 3, au_ca(255), au_cb(255));
        cy += bar_h + 8;
        goto au_hdr_done;
    }
    cut_rect(hx, cy, hw, bar_h, 12, color_alpha(navy, a0));
    {
        float ex = hx + 22, ey = cy + bar_h / 2, s = 11;
        float top[8] = { ex, ey - s, ex + s, ey - s * 0.5f, ex, ey, ex - s, ey - s * 0.5f };
        float left[8] = { ex - s, ey - s * 0.5f, ex, ey, ex, ey + s, ex - s, ey + s * 0.5f };
        float right[8] = { ex, ey, ex + s, ey - s * 0.5f, ex + s, ey + s * 0.5f, ex, ey + s };
        gfx_quad(top, color_alpha(rgba(170, 160, 240, 255), a0));
        gfx_quad(left, color_alpha(rgba(110, 96, 210, 255), a0));
        gfx_quad(right, color_alpha(rgba(76, 62, 168, 255), a0));
    }
    float tsz = w < 700 ? 21 : 23;
    caps(hx + 42, cy + (bar_h - gfx_line_height(FONT_UI, tsz)) / 2, tsz, 0.8f, color_alpha(gold, a0),
         color_alpha(rgba(0, 0, 0, 255), a0), tr("Controls & Tips"));
    float title_end = hx + 42 + caps_width(tsz, 0.8f, tr("Controls & Tips"));
    char bubble[48];
    snprintf(bubble, sizeof(bubble), tr("PRESS %s TO GO BACK"), bind_name(ACT_BACK));
    float bub_w = caps_width(9.5f, 1.1f, bubble) + 20, bub_h = 22;
    float bub_x = hx + hw - 22 - bub_w, bub_y = cy + (bar_h - bub_h) / 2;
    if (bub_x > title_end + 20) {
        gfx_rect(bub_x, bub_y, bub_w, bub_h, color_alpha(white, a0));
        gfx_tri(bub_x + 1, bub_y + 6, bub_x + 1, bub_y + 15, bub_x - 8, bub_y + 12, color_alpha(white, a0));
        dashed_rect(bub_x + 3, bub_y + 3, bub_w - 6, bub_h - 6, color_alpha(navy, a0));
        caps(bub_x + 10, bub_y + (bub_h - gfx_line_height(FONT_UI, 9.5f)) / 2, 9.5f, 1.1f, color_alpha(navy, a0),
             rgba(0, 0, 0, 0), bubble);
    }
    cy += bar_h + 8;
au_hdr_done:

    /* back button pinned at the bottom */
    float row_h = 24, by = y + h - m - row_h;
    const char *blabel = tr("BACK TO START PAGE");
    float bw = caps_width(10.5f, 1.2f, blabel) + 40;
    float a3 = intro(12);
    UiId bid = ui_id("help-back", 0);
    if (au) {
        if (ui_button(bid, hx, by - 4, bw, row_h + 4, tr("Back to the start page"), 13, UIB_NORMAL)) act = HOME_OPEN_URL;
        goto au_btn_done;
    }
    bool bhov = ui_hover(bid, hx, by, bw, row_h);
    cut_rect(hx, by, bw, row_h, 7, color_alpha(bhov ? gold : panel, a3));
    cut_outline(hx, by, bw, row_h, 7, 1.5f, color_alpha(navy, a3));
    {
        float ax = hx + 14, ay = by + row_h / 2;
        gfx_circle(ax, ay, 7, color_alpha(navy, a3));
        gfx_tri(ax + 2.5f, ay - 4, ax + 2.5f, ay + 4, ax - 4, ay, color_alpha(bhov ? white : gold, a3));   /* points back */
    }
    caps(hx + 28, by + (row_h - gfx_line_height(FONT_UI, 10.5f)) / 2, 10.5f, 1.2f, color_alpha(navy, a3), rgba(0, 0, 0, 0), blabel);
    if (clicked(bhov, input_ok)) act = HOME_OPEN_URL;
au_btn_done:

    /* scrolling body between the header and the button */
    float view_y = cy, view_h = by - 8 - cy;
    float content_h = help_body(hx, 0, hw, false);
    if (input_ok) {
        help_scroll_target += input_scroll_delta();
        int d = ui_dpad_unhandled();
        if (d == DIR_DOWN) help_scroll_target += 60;
        if (d == DIR_UP) help_scroll_target -= 60;
    }
    float max_s = content_h - view_h;
    if (max_s < 0) max_s = 0;
    if (help_scroll_target > max_s) help_scroll_target = max_s;
    if (help_scroll_target < 0) help_scroll_target = 0;
    help_scroll += (help_scroll_target - help_scroll) * (g_set.animations ? 0.3f : 1.0f);
    gfx_set_clip(x, view_y, w, view_h);
    help_body(hx, view_y - help_scroll, hw - (max_s > 0 ? 10 : 0), true);
    gfx_set_clip(x, y, w, h);
    if (max_s > 0) {   /* scroll bar */
        float sh = fmaxf(24, view_h * view_h / content_h), sy = view_y + (view_h - sh) * (help_scroll / max_s);
        gfx_rect(hx + hw - 4, view_y, 4, view_h, au ? rgba(255, 255, 255, 20) : color_alpha(navy, 0.5f));
        gfx_rect(hx + hw - 4, sy, 4, sh, au ? au_ca(255) : gold);
    }
    gfx_clear_clip();
    return act;
}


/* ========================================================= Software Update
 * about:update: checks GitHub, shows the release notes, installs, restarts. */

static float notes_scroll, notes_scroll_target;

void home_update_reset(void) { intro_start = ui_time(); notes_scroll = notes_scroll_target = 0; }

/* Lays out (and with draw set, draws) the release notes line by line, so
 * their own line breaks are kept. Returns their height. */
static float notes_body(const char *notes, float x, float y, float tw, float ts, Color c, bool draw) {
    float ny = y;
    const char *p = notes;
    char ln[400];
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        size_t k = n < sizeof(ln) ? n : sizeof(ln) - 1;
        memcpy(ln, p, k);
        ln[k] = 0;
        if (ln[0]) ny += wrap(FONT_BODY, ts, x, ny, tw, c, ln, draw);
        else ny += gfx_line_height(FONT_BODY, ts) * 0.5f;
        p = e ? e + 1 : p + n;
    }
    return ny - y;
}

/* Flat button as on the old console sites; returns true when chosen. */
static bool flat_button(const char *id, float x, float y, float h, const char *label, bool back, bool input_ok, float a,
                        float *w_out) {
    Color navy = rgba(26, 24, 66, 255), gold = opaque(theme()->accent), white = rgba(255, 255, 255, 255);
    Color panel = opaque(theme()->panel_top);
    float w = caps_width(10.5f, 1.2f, label) + 40;
    if (w_out) *w_out = w;
    UiId uid = ui_id(id, 0);
    bool hov = ui_hover(uid, x, y, w, h);
    cut_rect(x, y, w, h, 7, color_alpha(hov ? gold : panel, a));
    cut_outline(x, y, w, h, 7, 1.5f, color_alpha(navy, a));
    float ax = x + 14, ay = y + h / 2;
    gfx_circle(ax, ay, 7, color_alpha(navy, a));
    if (back) gfx_tri(ax + 2.5f, ay - 4, ax + 2.5f, ay + 4, ax - 4, ay, color_alpha(hov ? white : gold, a));
    else gfx_tri(ax - 2.5f, ay - 4, ax - 2.5f, ay + 4, ax + 4, ay, color_alpha(hov ? white : gold, a));
    caps(x + 28, y + (h - gfx_line_height(FONT_UI, 10.5f)) / 2, 10.5f, 1.2f, color_alpha(navy, a), rgba(0, 0, 0, 0), label);
    return clicked(hov, input_ok);
}

HomeAction home_update_draw(float x, float y, float w, float h, bool input_ok, const UpdView *v) {
    const Theme *t = theme();
    HomeAction act = HOME_NONE;
    Color base = opaque(color_lerp(t->panel_bottom, t->bg_top, 0.25f));
    Color navy = rgba(26, 24, 66, 255), gold = opaque(t->accent), white = rgba(255, 255, 255, 255);
    Color pale = opaque(color_lerp(t->panel_top, white, 0.62f)), orange = rgba(240, 132, 24, 255);
    gfx_rrect(x, y, w, h, 14, base);
    gfx_set_clip(x, y, w, h);
    float m = 10, hx = x + m, hw = w - 2 * m, cy = y + m;
    UpdateState us = v->state;

    /* header bar */
    float a0 = intro(0), bar_h = 34;
    cut_rect(hx, cy, hw, bar_h, 12, color_alpha(navy, a0));
    {
        float ex = hx + 22, ey = cy + bar_h / 2, s = 11;
        float top[8] = { ex, ey - s, ex + s, ey - s * 0.5f, ex, ey, ex - s, ey - s * 0.5f };
        float left[8] = { ex - s, ey - s * 0.5f, ex, ey, ex, ey + s, ex - s, ey + s * 0.5f };
        float right[8] = { ex, ey, ex + s, ey - s * 0.5f, ex + s, ey + s * 0.5f, ex, ey + s };
        gfx_quad(top, color_alpha(rgba(170, 160, 240, 255), a0));
        gfx_quad(left, color_alpha(rgba(110, 96, 210, 255), a0));
        gfx_quad(right, color_alpha(rgba(76, 62, 168, 255), a0));
    }
    float tsz = w < 700 ? 21 : 23;
    caps(hx + 42, cy + (bar_h - gfx_line_height(FONT_UI, tsz)) / 2, tsz, 0.8f, color_alpha(gold, a0),
         color_alpha(rgba(0, 0, 0, 255), a0), v->title);
    float title_end = hx + 42 + caps_width(tsz, 0.8f, v->title);
    char bubble[48];
    snprintf(bubble, sizeof(bubble), tr("YOU HAVE %s"), v->have[0] ? v->have : tr("UNKNOWN"));
    float bub_w = caps_width(9.5f, 1.1f, bubble) + 20, bub_h = 22;
    float bub_x = hx + hw - 22 - bub_w, bub_y = cy + (bar_h - bub_h) / 2;
    if (bub_x > title_end + 20) {
        gfx_rect(bub_x, bub_y, bub_w, bub_h, color_alpha(white, a0));
        gfx_tri(bub_x + 1, bub_y + 6, bub_x + 1, bub_y + 15, bub_x - 8, bub_y + 12, color_alpha(white, a0));
        dashed_rect(bub_x + 3, bub_y + 3, bub_w - 6, bub_h - 6, color_alpha(navy, a0));
        caps(bub_x + 10, bub_y + (bub_h - gfx_line_height(FONT_UI, 9.5f)) / 2, 9.5f, 1.1f, color_alpha(navy, a0),
             rgba(0, 0, 0, 0), bubble);
    }
    cy += bar_h + 8;

    /* status panel */
    float a1 = intro(1);
    char line1[160] = "", line2[300] = "";
    float progress = -1;   /* 0..1 shows a bar */
    switch (us) {
    case UPD_NONE:
    case UPD_CHECKING:
        snprintf(line1, sizeof(line1), tr("Checking GitHub for a newer version..."));
        snprintf(line2, sizeof(line2), tr("Releases come from github.com/%s"), v->repo);
        break;
    case UPD_LATEST:
        snprintf(line1, sizeof(line1), tr("You have the latest version."));
        snprintf(line2, sizeof(line2), tr("%s %s is up to date."), v->product, v->have);
        break;
    case UPD_AVAILABLE:
        snprintf(line1, sizeof(line1), tr("Version %s is available!"), v->latest);
        snprintf(line2, sizeof(line2), "%s", v->available_text);
        break;
    case UPD_CHECK_FAILED:
        snprintf(line1, sizeof(line1), tr("Couldn't check for updates."));
        snprintf(line2, sizeof(line2), "%s", v->error);
        break;
    case UPD_INSTALLING: {
        const DownloadProgress *dp = fetch_download_progress();
        const UnzipProgress *up = fetch_unzip_progress();
        UpdateStep st = v->step;
        snprintf(line1, sizeof(line1), tr("Installing version %s..."), v->latest);
        if (st == UPD_STEP_DOWNLOAD) {
            if (dp->total > 0) {
                progress = (float)dp->done / (float)dp->total * 0.8f;
                snprintf(line2, sizeof(line2), tr("Downloading: %lld of %lld KB"), dp->done / 1024, dp->total / 1024);
            } else {
                progress = 0;
                snprintf(line2, sizeof(line2), tr("Connecting to GitHub..."));
            }
        } else if (st == UPD_STEP_UNPACK) {
            progress = 0.8f + (up->total ? 0.15f * (float)up->done / (float)up->total : 0);
            snprintf(line2, sizeof(line2), tr("Unpacking and checking the new version..."));
        } else {
            progress = 0.97f;
            snprintf(line2, sizeof(line2), tr("Putting the new version in place..."));
        }
        break;
    }
    case UPD_INSTALLED:
        snprintf(line1, sizeof(line1), tr("Version %s is installed."), v->latest);
        snprintf(line2, sizeof(line2), "%s", v->installed_text);
        progress = 1;
        break;
    case UPD_FAILED:
        snprintf(line1, sizeof(line1), tr("The update couldn't be installed."));
        snprintf(line2, sizeof(line2), "%s", v->error);
        break;
    }
    float ts = w < 700 ? 13.0f : 14.0f, tw = hw - 28;
    float l2h = wrap(FONT_BODY, ts, 0, 0, tw, white, line2, false);
    float ph = 12 + gfx_line_height(FONT_UI, 16) + 4 + l2h + (progress >= 0 ? 22 : 0) + 8;
    cut_rect(hx, cy, hw, ph, 8, color_alpha(pale, a1));
    cut_outline(hx, cy, hw, ph, 8, 1.5f, color_alpha(navy, a1));
    gfx_rect(hx + 1, cy + 1, 5, ph - 2 - 8, color_alpha(orange, a1));
    float ty = cy + 9;
    if (us == UPD_CHECKING || us == UPD_INSTALLING || us == UPD_NONE) {
        ui_spinner(hx + hw - 24, cy + 20, 18);
    }
    caps(hx + 15, ty, 16, 0.6f, color_alpha(navy, a1), rgba(0, 0, 0, 0), line1);
    ty += gfx_line_height(FONT_UI, 16) + 4;
    ty += wrap(FONT_BODY, ts, hx + 15, ty, tw, color_alpha(navy, a1), line2, true);
    if (progress >= 0) {
        float bx = hx + 15, bw = hw - 30, by = ty + 6;
        gfx_rect(bx, by, bw, 10, color_alpha(navy, a1));
        gfx_rect(bx + 2, by + 2, (bw - 4) * fminf(1, progress), 6, color_alpha(gold, a1));
    }
    cy += ph + 9;

    /* buttons pinned at the bottom */
    float row_h = 24, by = y + h - m - row_h, bx = hx, bw = 0;
    float a3 = intro(3);
    if (us == UPD_AVAILABLE || us == UPD_FAILED) {
        if (flat_button("upd-install", bx, by, row_h, us == UPD_FAILED ? tr("TRY AGAIN") : tr("DOWNLOAD AND INSTALL"), false, input_ok, a3, &bw))
            act = HOME_UPDATE_INSTALL;
        bx += bw + 10;
    }
    if (us == UPD_INSTALLED) {
        if (flat_button("upd-restart", bx, by, row_h, v->done_button, false, input_ok, a3, &bw)) act = HOME_UPDATE_RESTART;
        bx += bw + 10;
    }
    if (us == UPD_LATEST || us == UPD_CHECK_FAILED) {
        if (flat_button("upd-check", bx, by, row_h, tr("CHECK AGAIN"), false, input_ok, a3, &bw)) act = HOME_UPDATE_CHECK;
        bx += bw + 10;
    }
    if (us != UPD_INSTALLING) {
        if (flat_button("upd-back", bx, by, row_h, tr("BACK TO START PAGE"), true, input_ok, a3, &bw)) act = HOME_OPEN_URL;
    } else {
        if (flat_button("upd-stop", bx, by, row_h, tr("STOP"), true, input_ok, a3, &bw)) act = HOME_UPDATE_STOP;
    }

    /* release notes in the space between */
    const char *notes = v->notes;
    if ((us == UPD_AVAILABLE || us == UPD_INSTALLING || us == UPD_INSTALLED || us == UPD_FAILED) && notes[0]) {
        float a2 = intro(2);
        caps(hx + 2, cy, 15, 1.0f, color_alpha(white, a2), color_alpha(rgba(20, 16, 50, 200), a2), tr("WHAT'S NEW"));
        float dx = hx + 2 + caps_width(15, 1.0f, tr("WHAT'S NEW")) + 14;
        dotted_hline(dx, cy + 9, hx + hw - dx, color_alpha(opaque(t->panel_top), 0.8f * a2));
        cy += 21;
        float nh = by - 8 - cy;
        if (nh > 30) {
            cut_rect(hx, cy, hw, nh, 8, color_alpha(pale, a2));
            cut_outline(hx, cy, hw, nh, 8, 1.5f, color_alpha(navy, a2));
            /* scrolls with the C-Stick, L/R and the D-Pad when it doesn't fit */
            float view_y = cy + 7, view_h = nh - 14;
            float content_h = notes_body(notes, 0, 0, tw - 10, ts, navy, false);
            float max_s = content_h - view_h;
            if (max_s < 0) max_s = 0;
            if (input_ok && max_s > 0) {
                notes_scroll_target += input_scroll_delta();
                int d = ui_dpad_unhandled();
                if (d == DIR_DOWN) notes_scroll_target += 40;
                if (d == DIR_UP) notes_scroll_target -= 40;
            }
            if (notes_scroll_target > max_s) notes_scroll_target = max_s;
            if (notes_scroll_target < 0) notes_scroll_target = 0;
            notes_scroll += (notes_scroll_target - notes_scroll) * (g_set.animations ? 0.3f : 1.0f);
            gfx_set_clip(hx + 4, cy + 4, hw - 8, nh - 8);
            notes_body(notes, hx + 15, view_y - notes_scroll, max_s > 0 ? tw - 10 : tw, ts, color_alpha(navy, a2), true);
            if (max_s > 0) {   /* scroll bar */
                float sh = fmaxf(20, view_h * view_h / content_h), sy = view_y + (view_h - sh) * (notes_scroll / max_s);
                gfx_rect(hx + hw - 12, view_y, 4, view_h, color_alpha(navy, 0.35f * a2));
                gfx_rect(hx + hw - 12, sy, 4, sh, color_alpha(gold, a2));
            }
            gfx_set_clip(x, y, w, h);
        }
    }
    gfx_clear_clip();
    return act;
}

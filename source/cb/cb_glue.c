/* cb_glue.c - GC Station : Cube Browser (Arial-Bold, licence MIT) dans l'onglet WEB.
 * Reprend la boucle de main.c de Cube Browser (navigateur, reglages, passage en cube tournant, clavier,
 * pointeur), sans la mise en route de l'ecran, du reseau ni de la carte SD : GC Station les a deja. */
#include <gccore.h>
#include <math.h>
#include <stdio.h>
#include "gfx.h"
#include "theme.h"
#include "input.h"
#include "settings.h"
#include "settings_ui.h"
#include "storage.h"
#include "gc_entropy.h"
#include "fetch.h"
#include "ui.h"
#include "osk.h"
#include "browser.h"
#include "dom.h"
#include "cb_host.h"

enum { SCREEN_BROWSER, SCREEN_SETTINGS };
static int screen = SCREEN_BROWSER, target_screen = SCREEN_BROWSER;
static float trans = 1.0f;
static int inited = 0;

static void draw_screen(int which, bool interactive) {
    if (which == SCREEN_BROWSER) {
        browser_frame(interactive);
    } else if (settings_ui_frame(interactive) && interactive) {
        target_screen = SCREEN_BROWSER;
        trans = g_set.animations ? 0.0f : 1.0f;
        if (!g_set.animations) screen = SCREEN_BROWSER;
    }
}

/* les deux ecrans comme deux faces d'un cube qui tourne */
static void draw_transition(int from, int to, float p) {
    float e = p * p * (3 - 2 * p);
    float th = e * (float)M_PI * 0.5f;
    float c = cosf(th), s = sinf(th);
    float k = 1.0f / (c + s);
    float W = gfx_width();
    float ty = 240.0f * (1.0f - k);
    bool forward = (to == SCREEN_SETTINGS);
    float out_sx = k * c, in_sx = k * s;
    float out_tx = forward ? 0 : W * k * s;
    float in_tx = forward ? W * k * c : 0;
    const Theme *t = theme();
    gfx_set_transform(out_sx, k, out_tx, ty);
    draw_screen(from, false);
    gfx_rect(0, 0, W, 480, color_alpha(t->shadow, s * 0.6f));
    gfx_set_transform(in_sx, k, in_tx, ty);
    draw_screen(to, false);
    gfx_rect(0, 0, W, 480, color_alpha(t->shadow, c * 0.6f));
    gfx_reset_transform();
}

void cb_init(void) {
    if (inited) return;
    inited = 1;
    storage_init(NULL);
    settings_load();
    gfx_init(0, cb_host_wide() != 0, false);
    input_init();
    theme_set(g_set.theme);
    entropy_init();
    fetch_init();
    browser_init();
}

int cb_ready(void) { return inited; }

void cb_frame(int interactive) {
    if (!inited) cb_init();
    int au = cb_host_integrated();                        /* v214 : dans GC Station, style Aurore */
    if (au || g_set.theme == THEME_AURORE) { u8 a[3], b[3]; cb_host_accents(a, b); theme_accents(a, b); }
    theme_set(au ? THEME_AURORE : g_set.theme);
    if (au && trans < 1.0f) { trans = 1.0f; screen = target_screen; }   /* (pas de cube tournant : l'en-tete de GC Station reste) */
    input_update();
    ui_begin_frame();
    gfx_begin_frame();
    ui_draw_background();
    if (trans < 1.0f) {
        draw_transition(screen, target_screen, trans);
        trans += 1.0f / 18.0f;                                /* (50 images/s : ~0,36 s) */
        if (trans >= 1.0f) { trans = 1.0f; screen = target_screen; }
    } else {
        draw_screen(screen, interactive != 0);
        if (screen == SCREEN_BROWSER && browser_take_settings_request()) {
            settings_ui_open();
            target_screen = SCREEN_SETTINGS;
            if (g_set.animations) trans = 0.0f;
            else screen = SCREEN_SETTINGS;
        }
    }
    osk_frame();
    ui_draw_toast();
    if (interactive) ui_draw_cursor();
    ui_end_frame();
    gfx_end_frame();
    cb_host_restore_gx();
}

int cb_view(void) {
    if (!inited) return CBV_HOME;
    if (osk_active()) return CBV_KEYBOARD;
    if (screen == SCREEN_SETTINGS || target_screen == SCREEN_SETTINGS) return CBV_SETTINGS;
    return browser_view();
}

void cb_navigate(const char *url) {
    if (!inited) cb_init();
    if (screen != SCREEN_BROWSER) { screen = target_screen = SCREEN_BROWSER; trans = 1.0f; }
    browser_navigate(url);
}

int cb_wants_start(void) { return inited && osk_active(); }
const char *cb_current_url(void) { return inited ? browser_current_url() : NULL; }
void cb_reload_internal(const char *url) {
    if (!inited) return;
    static char u[2048];                              /* (copie : l'adresse peut etre celle de la page qu'on remplace) */
    snprintf(u, sizeof(u), "%s", url);
    browser_navigate_replace(u);
}
void cb_toast(const char *msg) {                 /* (GC Station ecrit en UTF-8 : lettres simples pour la police du navigateur) */
    if (!inited) return;
    static char t[300];
    snprintf(t, sizeof(t), "%s", msg);
    dom_decode_inplace(t, sizeof(t));
    ui_toast(t);
}

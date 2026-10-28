/* cb_host.h - GC Station : ce que Cube Browser (source/cb, code MIT d'Arial-Bold, adapte) demande a GC Station,
 * et ce que GC Station appelle pour l'afficher dans l'onglet WEB. */
#pragma once
#include <stddef.h>
#include <gccore.h>

/* --- fourni par GC Station (main.c) --- */
int  cb_host_net_ready(void);                 /* reseau pret (GC Station l'a deja demarre) ; -1 = en panne */
const char *cb_host_ip(void);
const char *cb_host_gateway(void);
const char *cb_host_cacert(size_t *len);      /* certificats racines (cacert.h de GC Station) */
void cb_host_screen(float *ox, float *oy, float *sx, float *sy, int *fb_w, int *efb_h, float *virt_x0);   /* zone de dessin */
int  cb_host_wide(void);                      /* 16:9 */
u32  cb_host_reserved_buttons(void);          /* boutons gardes par GC Station (Z : onglets ; START : menu) */
int  cb_host_input_enabled(void);             /* 0 : un menu de GC Station est ouvert */
void cb_host_restore_gx(void);                /* apres le dessin du navigateur : l'etat GX de GC Station */
void cb_host_open_menu(void);                 /* roue dentee : menu de GC Station */
void cb_host_download_done(const char *path); /* fichier telecharge : la musique le voit tout de suite */
char *cb_host_internal(const char *url, const char *post, size_t *len);   /* pages "gc:" (malloc'ees) ; NULL = inconnue */
const char *cb_host_data_dir(void);           /* dossier des reglages du navigateur */
const char *cb_host_download_dir(void);
void cb_host_note(const char *msg);
void cb_host_exit(void);                      /* porte de la barre : quitter le navigateur (onglet MUSIQUE) */
int  cb_host_integrated(void);                /* v214 : style Aurore : le navigateur vit entre l'en-tete et la legende de GC Station */
void cb_host_accents(u8 *a, u8 *b);           /* v214 : les deux couleurs de l'Aurore (rose, bleu : theme de GC Station) */

/* --- fourni par le navigateur (cb_glue.c) --- */
void cb_init(void);                           /* une fois, apres le demarrage de GC Station */
void cb_frame(int interactive);               /* dessin + manette, une fois par image (onglet WEB) */
void cb_navigate(const char *url);            /* ouvrir une page (telephone, assistant...) */
int  cb_wants_start(void);                    /* le clavier a l'ecran est ouvert : START = valider */
int  cb_ready(void);
const char *cb_current_url(void);
void cb_reload_internal(const char *url);     /* une page "gc:" a change (reponse de l'assistant...) : la remontrer */
void cb_toast(const char *msg);
enum { CBV_HOME, CBV_PAGE, CBV_SETTINGS, CBV_KEYBOARD, CBV_BOOKMARKS, CBV_DIALOG };
int  cb_view(void);                           /* v214 : ce que montre le navigateur (legende de GC Station) */
void cb_pointer(float *x, float *y);          /* v218 : pointeur du navigateur (coordonnees de GC Station, 640 x 480) */
void cb_overlay_cursor(void);                 /* v218 : redessiner le pointeur par-dessus la barre des onglets */

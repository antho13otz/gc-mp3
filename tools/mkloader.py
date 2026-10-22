#!/usr/bin/env python3
# Lanceur de jeux GC-MP3 (v44) : le moteur de Swiss (swiss-gc, licence GPL v2),
# modifie pour lancer directement le jeu demande par GC-MP3, sans le menu de Swiss :
#  - "Autoload=sdX:/jeu.iso" (argument) : le jeu demarre tout seul (pas d'ecran d'infos ;
#    B maintenu au lancement = l'ecran du jeu de Swiss, avec ses reglages et cheats) ;
#  - "GCMP3=sdX:/chemin/gc-mp3.dol" : en cas d'erreur, ou apres B, retour sur GC-MP3 ;
#  - sans argument (retour en jeu A + Z + START : ce lanceur est aussi dans apploader.img) :
#    retour sur GC-MP3 (chemin lu dans SD:/gc-mp3/retour.txt, ecrit par GC-MP3) ;
#  - journal dans SD:/gc-mp3/lanceur.log (GC-MP3 affiche l'erreur) ;
#  - pas de questions au demarrage (reglages video / appareil de config) ;
#  - fond et barre de titre aux couleurs de GC-MP3 (tools/loader/backdrop.png).
# Les reglages restent ceux de Swiss / Indigo (SD:/swiss/settings) : les reglages par jeu
# faits dans GC-MP3 s'appliquent.
# Usage : python3 tools/mkloader.py <dossier swiss-gc> <dossier tools/loader>
import os, shutil, sys

root, assets = sys.argv[1], sys.argv[2]
sw = os.path.join(root, "cube", "swiss")


def patch(rel, pairs):
    p = os.path.join(sw, rel)
    s = open(p, encoding="latin-1").read()
    for old, new in pairs:
        n = s.count(old)
        if n != 1:
            sys.exit("ERREUR %s : %d fois [%s]" % (rel, n, old[:80]))
        s = s.replace(old, new)
    open(p, "w", encoding="latin-1").write(s)
    print("ok", rel)


# 1. option de compilation
patch("Makefile", [("-DLFS_MULTIVERSION", "-DLFS_MULTIVERSION -DGCMP3_LOADER")])

# 2. demarrage : jeu direct, pas de questions, retour sur GC-MP3, journal
patch("source/main.c", [
    ("int main(int argc, char *argv[])\n{",
     """#ifdef GCMP3_LOADER
bool config_set_device();
void config_unset_device();
char* config_file_read(char* filename);
int config_file_write(char* filename, char* contents);
void gcmp3_logf(const char *fmt, ...);
char *gcmp3_log_text(void);
extern char gcmp3_title[128];
static int gcmp3_home_tried = 0;
static const char *gcmp3_arg(int argc, char *argv[], const char *key)
{
	for(int a = 1; a < argc && argv; a++)
		if(argv[a] && !strncmp(argv[a], key, strlen(key)) && argv[a][strlen(key)])
			return argv[a] + strlen(key);
	return NULL;
}
/* journal pour GC-MP3 (SD:/gc-mp3/lanceur.log) : ce qui s'est passe, et le message d'erreur */
static void gcmp3_save_log(void)
{
	if(config_set_device()) {
		config_file_write("gc-mp3/lanceur.log", gcmp3_log_text());
		config_unset_device();
	}
}
/* retour sur GC-MP3 : chemin donne en argument (GCMP3=...), sinon SD:/gc-mp3/retour.txt
   (ecrit par GC-MP3 ; sert aussi au retour en jeu A + Z + START, qui n'a pas d'arguments) */
static void gcmp3_return(int argc, char *argv[])
{
	if(gcmp3_home_tried) return;
	gcmp3_home_tried = 1;
	char path[PATHNAME_MAX] = "";
	const char *a = gcmp3_arg(argc, argv, "GCMP3=");
	if(a) strlcpy(path, a, sizeof(path));
	else if(config_set_device()) {
		char *t = config_file_read("gc-mp3/retour.txt");
		config_unset_device();
		if(t) {
			char *e = t + strcspn(t, "\\r\\n");
			*e = 0;
			strlcpy(path, t, sizeof(path));
			free(t);
		}
	}
	gcmp3_logf("retour sur GC-MP3 : [%s]\\n", path);
	gcmp3_save_log();
	if(!path[0]) return;
	uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Retour sur GC Station\\205"));
	int r = find_existing_entry(path, true);
	DrawDispose(msgBox);
	gcmp3_logf("retour impossible (%d)\\n", r);
	gcmp3_save_log();
}
#define GCMP3_ASK(x) do { } while(0)
#else
#define GCMP3_ASK(x) x
#endif

int main(int argc, char *argv[])
{"""),
    ("""	if(swissSettings.initNetworkAtStart |= bba_requires_init()) {""",
     """#ifdef GCMP3_LOADER
	if(swissSettings.emulateEthernet && bba_requires_init()) {	// GC-MP3 : reseau seulement pour l'emulation du BBA (lancement plus rapide)
#else
	if(swissSettings.initNetworkAtStart |= bba_requires_init()) {
#endif"""),
    ("""	memset(&swissSettings, 0 , sizeof(SwissSettings));""",
     """	memset(&swissSettings, 0 , sizeof(SwissSettings));
#ifdef GCMP3_LOADER
	{ const char *t = gcmp3_arg(argc, argv, "GCMP3TITLE="); if(t) strlcpy(gcmp3_title, t, sizeof(gcmp3_title)); }
	gcmp3_logf("GC-MP3 lanceur : %d argument(s)\\n", argc);
	for(int a = 0; a < argc && argv; a++) gcmp3_logf("  arg %d : %s\\n", a, argv[a] ? argv[a] : "(null)");
#endif"""),
    ("""	else if(swissSettings.aveCompat != AVE_RVL_COMPAT && swissSettings.lastDTVStatus != getRawDTVStatus()) {
		show_settings(PAGE_GLOBAL, SET_AVE_COMPAT, NULL);
	}
	// If there's no default config device""",
     """	else if(swissSettings.aveCompat != AVE_RVL_COMPAT && swissSettings.lastDTVStatus != getRawDTVStatus()) {
		GCMP3_ASK(show_settings(PAGE_GLOBAL, SET_AVE_COMPAT, NULL));
	}
	// If there's no default config device"""),
    ("""					show_settings(PAGE_GLOBAL, SET_CONFIG_DEV, NULL);
				}
				else if(swissSettings.aveCompat != AVE_RVL_COMPAT && swissSettings.lastDTVStatus != getRawDTVStatus()) {
					show_settings(PAGE_GLOBAL, SET_AVE_COMPAT, NULL);
				}""",
     """					GCMP3_ASK(show_settings(PAGE_GLOBAL, SET_CONFIG_DEV, NULL));
				}
				else if(swissSettings.aveCompat != AVE_RVL_COMPAT && swissSettings.lastDTVStatus != getRawDTVStatus()) {
					GCMP3_ASK(show_settings(PAGE_GLOBAL, SET_AVE_COMPAT, NULL));
				}"""),
    ("""	// Check for autoload entry
	if(swissSettings.autoload[0]) {""",
     """#ifdef GCMP3_LOADER
	// GC-MP3 : sans jeu demande (retour en jeu A + Z + START), on revient sur GC-MP3
	if(gcmp3_arg(argc, argv, "Autoload=") == NULL) {
		gcmp3_logf("pas de jeu demande : retour en jeu\\n");
		gcmp3_return(argc, argv);
	}
	gcmp3_logf("jeu : [%s]\\n", swissSettings.autoload);
#endif
	// Check for autoload entry
	if(swissSettings.autoload[0]) {"""),
    ("""		find_existing_entry(&swissSettings.autoload[0], true);
	}""",
     """		int gcmp3_r = find_existing_entry(&swissSettings.autoload[0], true);
#ifdef GCMP3_LOADER
		gcmp3_logf("jeu non lance (%d)\\n", gcmp3_r);
#endif
		(void)gcmp3_r;
	}"""),
    ("""	if(swissSettings.cubebootInvoked) {
		return 0;
	}
	while(1) {
		menu_loop();""",
     """	if(swissSettings.cubebootInvoked) {
		return 0;
	}
#ifdef GCMP3_LOADER
	gcmp3_home_tried = 0;
	gcmp3_return(argc, argv);
#endif
	while(1) {
		menu_loop();"""),
])

# 2a. pas de .dol lance tout seul depuis la racine de la carte (ipl.dol, boot.dol...) :
#     c'etait GC-MP3 lui-meme (ipl.dol), d'ou le retour immediat sur GC-MP3 (v44)
patch("source/util.c", [
    ("""void load_auto_dol(int argc, char *argv[]) {
""",
     """void load_auto_dol(int argc, char *argv[]) {
#ifdef GCMP3_LOADER
	return;
#endif
"""),
    ("""/* Print over USB Gecko if enabled */
void print_debug(const char *fmt, ...)
{""",
     """#ifdef GCMP3_LOADER
/* GC-MP3 : journal en memoire (ecrit dans SD:/gc-mp3/lanceur.log avant le retour sur GC-MP3) */
static char gcmp3_log[12288];
static int gcmp3_log_n = 0;
static void gcmp3_logv(const char *fmt, va_list ap)
{
	if(gcmp3_log_n > (int)sizeof(gcmp3_log) - 512) {		// plein : on garde la fin
		memmove(gcmp3_log, gcmp3_log + sizeof(gcmp3_log) / 2, gcmp3_log_n - sizeof(gcmp3_log) / 2);
		gcmp3_log_n -= sizeof(gcmp3_log) / 2;
	}
	int n = vsnprintf(gcmp3_log + gcmp3_log_n, sizeof(gcmp3_log) - gcmp3_log_n, fmt, ap);
	if(n > 0) gcmp3_log_n += n < (int)sizeof(gcmp3_log) - gcmp3_log_n ? n : (int)sizeof(gcmp3_log) - gcmp3_log_n - 1;
}
void gcmp3_logf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	gcmp3_logv(fmt, ap);
	va_end(ap);
}
char *gcmp3_log_text(void) { gcmp3_log[gcmp3_log_n] = 0; return gcmp3_log; }
char gcmp3_title[128] = "";			// nom du jeu (argument GCMP3TITLE=), pour l'ecran de chargement
char gcmp3_status[96] = "";			// etape en cours (barres de progression de Swiss)
int gcmp3_pct = -1;				// avancement connu (0..100), -1 = inconnu
#endif
/* Print over USB Gecko if enabled */
void print_debug(const char *fmt, ...)
{
#ifdef GCMP3_LOADER
	{ va_list ap; va_start(ap, fmt); gcmp3_logv(fmt, ap); va_end(ap); }
#endif"""),
])

# 2b. pas d'ecran d'infos du jeu : il demarre tout de suite (B maintenu : l'ecran de Swiss,
#     avec les reglages du jeu et les cheats). (Le reglage AutoBoot de Swiss n'est pas touche.)
patch("source/swiss.c", [
    ("""int info_game(ConfigEntry *config)
{
""",
     """int info_game(ConfigEntry *config)
{
#ifdef GCMP3_LOADER
	if(!(padsButtonsHeld() & BUTTON_B)) {
		return 1;
	}
	while(padsButtonsHeld() & BUTTON_B) { VIDEO_WaitVSync(); }
#endif
"""),
])

# 3. apparence : barre de titre GC-MP3, fond integre (pas celui de la carte SD)
patch("source/gui/FrameBufferMagic.c", [
    ("""#define GUI_MSGBOX_ALPHA 225""",
     """#define GUI_MSGBOX_ALPHA 225
#ifdef GCMP3_LOADER
#include <math.h>
extern char gcmp3_title[128];
extern char gcmp3_status[96];
extern int gcmp3_pct;
void gcmp3_logf(const char *fmt, ...);
#endif"""),
    ("""// Internal
static void _DrawTitleBar(uiDrawObj_t *evt) {
""",
     """#ifdef GCMP3_LOADER
/* GC-MP3 : ecran de chargement (a la place de la barre de titre, du fond et des boutons de Swiss) */
static const char *gcmp3_fr(const char *m)
{
	static const struct { const char *en, *fr; } t[] = {
		{ "Reading", "Lecture du jeu" }, { "Checking Game", "V\\351rification du jeu" },
		{ "Detecting devices", "Recherche de la carte SD" }, { "Initialising Network", "Connexion au r\\351seau" },
		{ "Saving recent list", "Pr\\351paration" }, { "Setup base offset", "Pr\\351paration" },
		{ "Retour sur GC Station", "Retour sur GC Station" }, { "Applied", "Cheats activ\\351s" }, { "Loading", "Chargement" },
	};
	for(unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if(!strncmp(m, t[i].en, strlen(t[i].en))) {
			if(!strncmp(m, "Applied", 7)) { static char b[48]; snprintf(b, sizeof(b), "%d cheat(s) activ\\351(s)", atoi(m + 8)); return b; }
			return t[i].fr;
		}
	return m;
}
static void _DrawGcmp3Loading(void)
{
	static u32 frame = 0;
	frame++;
	GXColor none = (GXColor) {0,0,0,0};
	for(int i = 0; i < 24; i++) {					// fond : degrade violet
		int r = 30 - i, g = 12 - i / 3, b = 72 - i * 2;
		_DrawSimpleBox(0, i * 20, 640, 20, 0, (GXColor) {r < 4 ? 4 : r, g < 2 ? 2 : g, b < 18 ? 18 : b, 255}, none);
	}
	for(int i = 0; i < 40; i++) {					// petit spectre anime en bas
		float v = 0.5f + 0.5f * sinf(frame * 0.07f + i * 0.45f) * cosf(frame * 0.031f + i * 0.17f);
		int h = 6 + (int)(v * 46);
		_DrawSimpleBox(40 + i * 14, 430 - h, 10, h, 0, (GXColor) {150 + i * 2, 80, 255 - i * 2, 70}, none);
	}
	drawString(320, 170, "GC STATION", 2.2f, ALIGN_CENTER, (GXColor) {225,205,255,255});
	if(gcmp3_title[0]) drawStringEllipsis(320, 228, gcmp3_title, 1.0f, ALIGN_CENTER, (GXColor) {255,255,255,255}, false, 560);
	const char *st = gcmp3_status[0] ? gcmp3_fr(gcmp3_status) : "Chargement";
	char line[128];
	int dots = (frame / 20) % 4;
	snprintf(line, sizeof(line), "%s%.*s", st, dots, "...");
	drawString(320, 282, line, 0.75f, ALIGN_CENTER, (GXColor) {200,185,250,255});
	_DrawSimpleBox(170, 302, 300, 8, 0, (GXColor) {40,24,96,255}, none);	// barre
	if(gcmp3_pct >= 0) _DrawSimpleBox(170, 302, 3 * (gcmp3_pct > 100 ? 100 : gcmp3_pct), 8, 0, (GXColor) {170,110,255,255}, none);
	else { int p = (frame * 4) % 360; int x = 170 + (p < 300 ? p : 300) - 60; int w = 60;
		if(x < 170) { w -= 170 - x; x = 170; } if(x + w > 470) w = 470 - x;
		if(w > 0) _DrawSimpleBox(x, 302, w, 8, 0, (GXColor) {170,110,255,255}, none); }
	drawString(320, 452, "B maintenu : \\351cran du jeu (r\\351glages, cheats)     \\267     moteur Swiss", 0.5f, ALIGN_CENTER, (GXColor) {150,135,210,255});
}
#endif
// Internal
static void _DrawTitleBar(uiDrawObj_t *evt) {
#ifdef GCMP3_LOADER
	_DrawGcmp3Loading();
	return;
#endif
"""),
    ("""		DrawAddChild(container, DrawImage(TEX_BACKDROP, 0, 0, 640, 480, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0));
		DrawAddChild(container, DrawTitleBar());
		buttonPanel = DrawMenuButtons(MENU_NOSELECT);
		DrawAddChild(container, buttonPanel);""",
     """#ifdef GCMP3_LOADER
		DrawAddChild(container, DrawTitleBar());		// GC-MP3 : seulement l'ecran de chargement
		buttonPanel = DrawMenuButtons(MENU_NOSELECT);
#else
		DrawAddChild(container, DrawImage(TEX_BACKDROP, 0, 0, 640, 480, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0));
		DrawAddChild(container, DrawTitleBar());
		buttonPanel = DrawMenuButtons(MENU_NOSELECT);
		DrawAddChild(container, buttonPanel);
#endif"""),
    ("""static void _DrawProgressBar(uiDrawObj_t *evt) {
	
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;""",
     """static void _DrawProgressBar(uiDrawObj_t *evt) {
	
	drawProgressEvent_t *data = (drawProgressEvent_t*)evt->data;
#ifdef GCMP3_LOADER
	gcmp3_pct = data->indeterminate || data->miniMode ? -1 : data->percent;	// (l'ecran de chargement l'affiche)
	return;
#endif"""),
    ("""uiDrawObj_t* DrawProgressBar(bool indeterminate, int percent, const char *message) {""",
     """uiDrawObj_t* DrawProgressBar(bool indeterminate, int percent, const char *message) {
#ifdef GCMP3_LOADER
	if(message && message[0]) {
		strlcpy(gcmp3_status, message, sizeof(gcmp3_status));
		char *nl = strchr(gcmp3_status, '\\n'); if(nl) *nl = 0;
		gcmp3_logf("... %s\\n", message);
	}
	message = NULL;
#endif"""),
    ("""uiDrawObj_t* DrawMessageBox(int type, const char *msg)
{
""",
     """uiDrawObj_t* DrawMessageBox(int type, const char *msg)
{
#ifdef GCMP3_LOADER
	if(type == D_INFO && !strncmp(msg, "Applied", 7)) return DrawProgressBar(true, 0, msg);	// GC-MP3 : info dans l'ecran de chargement
	gcmp3_logf("MSG: %s\\n", msg);
#endif
"""),
    ("""void DrawLoadBackdrop(DEVICEHANDLER_INTERFACE *device) {
""",
     """void DrawLoadBackdrop(DEVICEHANDLER_INTERFACE *device) {
#ifdef GCMP3_LOADER
	return;						// GC-MP3 : toujours le fond integre
#endif
"""),
])

# 4. fond : image en couleurs (palette RGB565 au lieu de niveaux de gris teintes)
shutil.copy(os.path.join(assets, "backdrop.png"), os.path.join(sw, "source", "images", "backdrop.png"))
patch("source/images/images.scf", [('<filepath="backdrop.png" id="backdrop" colfmt=8 palfmt=0 magfilt=0 />',
                                    '<filepath="backdrop.png" id="backdrop" colfmt=8 palfmt=1 magfilt=0 />')])
print("lanceur GC-MP3 pret")

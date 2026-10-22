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
			char *e = t + strcspn(t, "\r\n");
			*e = 0;
			strlcpy(path, t, sizeof(path));
			free(t);
		}
	}
	gcmp3_logf("retour sur GC-MP3 : [%s]\n", path);
	gcmp3_save_log();
	if(!path[0]) return;
	uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Retour sur GC-MP3\205"));
	int r = find_existing_entry(path, true);
	DrawDispose(msgBox);
	gcmp3_logf("retour impossible (%d)\n", r);
	gcmp3_save_log();
}
#define GCMP3_ASK(x) do { } while(0)
#else
#define GCMP3_ASK(x) x
#endif

int main(int argc, char *argv[])
{"""),
    ("""	memset(&swissSettings, 0 , sizeof(SwissSettings));""",
     """	memset(&swissSettings, 0 , sizeof(SwissSettings));
#ifdef GCMP3_LOADER
	gcmp3_logf("GC-MP3 lanceur : %d argument(s)\n", argc);
	for(int a = 0; a < argc && argv; a++) gcmp3_logf("  arg %d : %s\n", a, argv[a] ? argv[a] : "(null)");
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
		gcmp3_logf("pas de jeu demande : retour en jeu\n");
		gcmp3_return(argc, argv);
	}
	gcmp3_logf("jeu : [%s]\n", swissSettings.autoload);
#endif
	// Check for autoload entry
	if(swissSettings.autoload[0]) {"""),
    ("""		find_existing_entry(&swissSettings.autoload[0], true);
	}""",
     """		int gcmp3_r = find_existing_entry(&swissSettings.autoload[0], true);
#ifdef GCMP3_LOADER
		gcmp3_logf("jeu non lance (%d)\n", gcmp3_r);
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
    ("""	_DrawImageNow(TEX_SWISS, 36, 32, 96, 32, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
	drawString(142, 39, "for GameCube", 0.55f, ALIGN_LEFT, defaultColor);
	drawString(142, 56, "version 0.6", 0.55f, ALIGN_LEFT, defaultColor);
	
	sprintf(fbTextBuffer, "commit: %s \\267 revision: %s", GIT_COMMIT, GIT_REVISION);""",
     """#ifdef GCMP3_LOADER
	drawString(36, 52, "GC-MP3", 1.1f, ALIGN_LEFT, (GXColor) {225,205,255,255});
	drawString(150, 39, "lanceur de jeux", 0.55f, ALIGN_LEFT, defaultColor);
	drawString(150, 56, "moteur Swiss 0.6", 0.55f, ALIGN_LEFT, defaultColor);
	sprintf(fbTextBuffer, "Swiss %s \\267 r%s", GIT_COMMIT, GIT_REVISION);
#else
	_DrawImageNow(TEX_SWISS, 36, 32, 96, 32, 0, 0.0f, 1.0f, 0.0f, 1.0f, 0);
	drawString(142, 39, "for GameCube", 0.55f, ALIGN_LEFT, defaultColor);
	drawString(142, 56, "version 0.6", 0.55f, ALIGN_LEFT, defaultColor);
	
	sprintf(fbTextBuffer, "commit: %s \\267 revision: %s", GIT_COMMIT, GIT_REVISION);
#endif"""),
    ("""uiDrawObj_t* DrawMessageBox(int type, const char *msg)
{
""",
     """#ifdef GCMP3_LOADER
void gcmp3_logf(const char *fmt, ...);
#endif
uiDrawObj_t* DrawMessageBox(int type, const char *msg)
{
#ifdef GCMP3_LOADER
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

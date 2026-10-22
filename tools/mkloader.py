#!/usr/bin/env python3
# Lanceur de jeux GC-MP3 (v44) : le moteur de Swiss (swiss-gc, licence GPL v2),
# modifie pour lancer directement le jeu demande par GC-MP3, sans le menu de Swiss :
#  - "Autoload=sdX:/jeu.iso" (argument) : le jeu demarre tout seul (pas d'ecran d'infos ;
#    B maintenu au lancement = l'ecran du jeu de Swiss, avec ses reglages et cheats) ;
#  - "GCMP3=sdX:/chemin/gc-mp3.dol" : en cas d'erreur, ou apres B, retour sur GC-MP3 ;
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

# 2. demarrage : jeu direct, pas de questions, retour sur GC-MP3
patch("source/main.c", [
    ("int main(int argc, char *argv[])\n{",
     """#ifdef GCMP3_LOADER
/* GC-MP3 : le jeu n'a pas demarre (introuvable, erreur, ou B) : retour sur GC-MP3 */
static void gcmp3_return(int argc, char *argv[])
{
	for(int a = 1; a < argc && argv; a++) {
		if(argv[a] && !strncmp(argv[a], "GCMP3=", 6) && argv[a][6]) {
			uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Retour sur GC-MP3\\205"));
			find_existing_entry(argv[a] + 6, true);
			DrawDispose(msgBox);
		}
	}
}
#define GCMP3_ASK(x) do { } while(0)
#else
#define GCMP3_ASK(x) x
#endif

int main(int argc, char *argv[])
{"""),
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
    ("""	if(swissSettings.cubebootInvoked) {
		return 0;
	}
	while(1) {
		menu_loop();""",
     """	if(swissSettings.cubebootInvoked) {
		return 0;
	}
#ifdef GCMP3_LOADER
	gcmp3_return(argc, argv);
#endif
	while(1) {
		menu_loop();"""),
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

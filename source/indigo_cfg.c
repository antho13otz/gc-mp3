// GC-MP3 v32 : reglages d'Indigo / Swiss dans GC-MP3 (inclus dans main.c, apres gclib.c)
// Fenetre par-dessus l'ecran en cours. Lit et ecrit SD:/swiss/settings/global.ini,
// le fichier de reglages d'Indigo / Swiss (memes noms et memes valeurs que dans leur code).
// Seules les lignes modifiees changent : le reste du fichier (Autoload, lignes inconnues,
// commentaires) reste tel quel. Un reglage absent du fichier = valeur par defaut d'Indigo.

enum { ICT_LIST, ICT_INT, ICT_TEXT, ICT_NUM, ICT_PASS };
typedef struct {
    const char *key;                    /* nom dans global.ini */
    const char *fr, *en;                /* nom affiche */
    int type;
    const char *vals;                   /* ICT_LIST : valeurs separees par | */
    int lo, hi, step;                   /* ICT_INT */
    const char *dfr, *den;              /* aide */
} ic_item;

#define YN "No|Yes"
static const ic_item ic_items[] = {
    /* ---- page 0 : systeme ---- */
    { "SD/IDE Speed", "Vitesse SD / IDE", "SD/IDE Speed", ICT_LIST, "16MHz|32MHz", 0, 0, 0,
      "Vitesse du bus pour les cartes SD. 32 MHz ne marche pas avec tous les adaptateurs.", "EXI bus speed for SD cards. 32 MHz may not work with every adapter." },
    { "System Boot Mode", "Mode de d\xC3\xA9marrage", "System Boot Mode", ICT_LIST, "Default|Production", 0, 0, 0,
      "Production : passe le logo GameCube (GC Loader, PicoLoader).", "Production: skips the GameCube logo (GC Loader, PicoLoader)." },
    { "System Sound", "Son de la console", "System Sound", ICT_LIST, "Mono|Stereo", 0, 0, 0,
      "Sortie audio utilis\xC3\xA9" "e par la plupart des jeux.", "Audio output used by most games." },
    { "System Video", "Vid\xC3\xA9o de la console", "System Video", ICT_LIST, "NTSC|PAL|PAL-M", 0, 0, 0,
      "Surtout pour choisir NTSC ou PAL-M (console br\xC3\xA9silienne).", "Mainly to pick NTSC or PAL-M (Brazilian console)." },
    { "Screen Position", "Position de l'\xC3\xA9" "cran", "Screen Position", ICT_INT, NULL, -32, 32, 1,
      "D\xC3\xA9" "cale l'image des jeux \xC3\xA0 gauche ou \xC3\xA0 droite.", "Moves the game picture left or right." },
    { "System Language", "Langue de la console", "System Language", ICT_LIST, "English|German|French|Spanish|Italian|Dutch|Japanese|English (US)|Default", 0, 0, 0,
      "Langue des jeux (surtout les jeux PAL multilingues).", "Game language (mostly multi-language PAL games)." },
    { "Swiss Video Mode", "Mode vid\xC3\xA9o d'Indigo", "Swiss Video Mode", ICT_LIST, "Auto|480i|480sf|240p|480p|576i|576sf|288p|576p", 0, 0, 0,
      "Mode vid\xC3\xA9o du menu d'Indigo / Swiss.", "Video mode of the Indigo / Swiss menu." },
    { "Apps Video Mode", "Mode vid\xC3\xA9o des applis", "Apps Video Mode", ICT_LIST, "Auto|480i|480sf|240p|480p|576i|576sf|288p|576p", 0, 0, 0,
      "Mode vid\xC3\xA9o des homebrews lanc\xC3\xA9s (libogc2).", "Video mode for launched homebrew (libogc2)." },
    { "AVECompat", "Compatibilit\xC3\xA9 vid\xC3\xA9o", "AVE Compatibility", ICT_LIST, "AVE N-DOL|AVE P-DOL|CMPV-DOL|GCDigital|GCVideo|AVE-RVL", 0, 0, 0,
      "Puce vid\xC3\xA9o ou mod HDMI : GCVideo pour la plupart des mods HDMI.", "Video encoder or HDMI mod: GCVideo for most HDMI mods." },
    { "Force DTV Status", "Forcer la sortie num\xC3\xA9rique", "Force DTV Status", ICT_LIST, "No|Yes|Region Switch", 0, 0, 0,
      "Force la d\xC3\xA9tection du c\xC3\xA2" "ble composante / num\xC3\xA9rique.", "Forces detection of the digital AV cable." },
    { "RT4KOptim", "Entr\xC3\xA9" "e HDMI RetroTINK-4K", "RetroTINK-4K HDMI Input", ICT_LIST, YN, 0, 0, 0,
      "R\xC3\xA9glages adapt\xC3\xA9s au RetroTINK-4K.", "Settings tuned for the RetroTINK-4K." },
    { "Disable PAD Recalibration", "Pas de recalibrage manette", "Disable PAD Recalibration", ICT_LIST, YN, 0, 0, 0,
      "\xC3\x89vite les soucis avec certaines manettes non officielles.", "Avoids issues with some third-party controllers." },
    { "Disable PAD Rumble", "Couper les vibrations", "Disable PAD Rumble", ICT_LIST, YN, 0, 0, 0,
      "Plus de vibrations dans les jeux.", "No rumble in games." },
    { "Init DVD Drive at startup", "Lecteur DVD au d\xC3\xA9marrage", "Init DVD Drive at startup", ICT_LIST, YN, 0, 0, 0,
      "N\xC3\xA9" "cessaire pour le bouton d'\xC3\xA9jection (Panasonic Q) en IPL.", "Needed for the eject button (Panasonic Q) as IPL." },
    { "Stop DVD Drive motor", "Arr\xC3\xAAter le moteur DVD", "Stop DVD Drive motor", ICT_LIST, YN, 0, 0, 0,
      "Arr\xC3\xAAte le disque qui tourne au d\xC3\xA9marrage.", "Stops the spinning disc at startup." },
    { "Configure Audio Buffer", "Tampon audio du lecteur", "Configure Audio Buffer", ICT_LIST, "Off|Auto|On", 0, 0, 0,
      "Musique en streaming des disques. Off = chargements plus rapides.", "Disc audio streaming. Off = faster loading." },
    { "Enable USB Gecko", "USB Gecko", "Enable USB Gecko", ICT_LIST, "No|Slot A|Slot B|Serial Port 2", 0, 0, 0,
      "Messages de d\xC3\xA9" "bogage vers un USB Gecko.", "Debug messages to a USB Gecko." },
    { "Wait for USB Gecko", "Attendre l'USB Gecko", "Wait for USB Gecko", ICT_LIST, YN, 0, 0, 0,
      "Attend que le PC lise les messages.", "Waits for the PC to read messages." },
    { "Simulated MRAM Size", "M\xC3\xA9moire simul\xC3\xA9" "e", "Simulated MRAM Size", ICT_LIST, "None|16 MiB|24 MiB|32 MiB|48 MiB|64 MiB", 0, 0, 0,
      "Pour les consoles de d\xC3\xA9veloppement seulement.", "Development hardware only." },
    /* ---- page 1 : interface ---- */
    { "FileBrowserType", "Affichage des fichiers", "File Browser Type", ICT_LIST, "Standard|Fullwidth|Carousel", 0, 0, 0,
      "Standard, plein \xC3\xA9" "cran ou carrousel (jaquettes).", "Standard, full width or carousel (covers)." },
    { "GameBrowserType", "Affichage du dossier games", "Game Browser Type", ICT_LIST, "Standard|Fullwidth|Carousel", 0, 0, 0,
      "Affichage du dossier /games.", "Display of the /games folder." },
    { "AppsBrowserType", "Affichage du dossier apps", "Apps Browser Type", ICT_LIST, "Standard|Fullwidth|Carousel", 0, 0, 0,
      "Affichage du dossier /apps.", "Display of the /apps folder." },
    { "Enable File Management", "Gestion des fichiers (Z)", "File Management", ICT_LIST, YN, 0, 0, 0,
      "Z sur un fichier pour le copier, d\xC3\xA9placer, supprimer.", "Z on a file to copy, move or delete it." },
    { "RecentListLevel", "Liste des r\xC3\xA9" "cents", "Recent List", ICT_LIST, "Off|Lazy|On", 0, 0, 0,
      "START dans les fichiers : derniers jeux lanc\xC3\xA9s.", "START in the browser: recently launched games." },
    { "Hide Unknown file types", "Cacher les fichiers inconnus", "Hide unknown file types", ICT_LIST, YN, 0, 0, 0,
      "N'affiche que les jeux, .dol, MP3 et sauvegardes.", "Only shows games, .dol, MP3 and saves." },
    { "ShowHiddenFiles", "Fichiers cach\xC3\xA9s", "Show hidden files", ICT_LIST, YN, 0, 0, 0,
      "Affiche les fichiers et dossiers cach\xC3\xA9s.", "Shows hidden files and folders." },
    { "FlattenDir", "Aplatir les dossiers", "Flatten directory", ICT_TEXT, NULL, 0, 0, 0,
      "Motif de dossiers \xC3\xA0 afficher \xC3\xA0 plat (ex. /games/*).", "Folder pattern shown flat (e.g. /games/*)." },
    { "AutoBoot", "Lancement automatique", "AutoBoot", ICT_LIST, YN, 0, 0, 0,
      "Lance tout seul le disque ou le fichier de d\xC3\xA9marrage.", "Boots the disc or boot file automatically." },
    /* ---- page 2 : jeux ---- */
    { "IGRType", "Retour en jeu (A+Z+START)", "In-Game Reset", ICT_LIST, "Disabled|Reboot|Apploader", 0, 0, 0,
      "Reboot : A+Z+START en jeu red\xC3\xA9marre (sur GC Station si c'est ton ipl.dol).", "Reboot: A+Z+START in game restarts (into GC Station if it is your ipl.dol)." },
    { "BS2Boot", "Menu GameCube avant le jeu", "Load GameCube Main Menu", ICT_LIST, "No|Yes|Sound 1|Sound 2", 0, 0, 0,
      "Lance les jeux avec le logo et le menu GameCube.", "Boots games with the GameCube logo and menu." },
    { "AutoCheats", "Cheats automatiques", "Auto-load cheats", ICT_LIST, YN, 0, 0, 0,
      "Active tout seul les cheats coch\xC3\xA9s (swiss/cheats).", "Enables checked cheats automatically (swiss/cheats)." },
    { "Enable WiiRD debug", "D\xC3\xA9" "bogueur WiiRD", "WiiRD debugging", ICT_LIST, YN, 0, 0, 0,
      "Lance le jeu en pause avec le d\xC3\xA9" "bogueur (plus de m\xC3\xA9moire).", "Starts the game paused with the debugger." },
    { "Force Video Active", "Forcer la vid\xC3\xA9o active", "Force Video Active", ICT_LIST, YN, 0, 0, 0,
      "Contournement pour GCVideo-DVI 3.0 (inutile apr\xC3\xA8s 3.1).", "Workaround for GCVideo-DVI 3.0 (not needed after 3.1)." },
    { "Pause for resolution change", "Pause au changement de r\xC3\xA9solution", "Pause for resolution change", ICT_LIST, YN, 0, 0, 0,
      "Met le jeu en pause 2 s quand la r\xC3\xA9solution change.", "Pauses the game for 2 s when the resolution changes." },
    { "Disable MemCard PRO GameID", "MemCard PRO : pas d'ID de jeu", "Disable MemCard PRO GameID", ICT_LIST, "No|Slot A|Slot B|Slot A&B", 0, 0, 0,
      "N'envoie pas l'ID du jeu \xC3\xA0 la MemCard PRO.", "Doesn't send the game ID to the MemCard PRO." },
    { "Disable Video Patches", "Couper les patchs vid\xC3\xA9o", "Disable Video Patches", ICT_LIST, "None|Game|All", 0, 0, 0,
      "None : patchs vid\xC3\xA9o actifs. All : aucun patch.", "None: video patches on. All: no patch." },
    /* ---- page 3 : jeux par defaut ---- */
    { "Force NTSC Video Mode", "Mode vid\xC3\xA9o jeux NTSC", "Force NTSC Video Mode", ICT_LIST, "Auto|480i|480sf|240p|960i|480p|1080i60|540p60", 0, 0, 0,
      "Mode vid\xC3\xA9o forc\xC3\xA9 pour les jeux NTSC.", "Video mode forced for NTSC games." },
    { "Force PAL Video Mode", "Mode vid\xC3\xA9o jeux PAL", "Force PAL Video Mode", ICT_LIST, "Auto|480i|480sf|240p|960i|480p|1080i60|540p60|576i|576sf|288p|1152i|576p|1080i50|540p50", 0, 0, 0,
      "Mode vid\xC3\xA9o forc\xC3\xA9 pour les jeux PAL (480p = 60 Hz).", "Video mode forced for PAL games." },
    { "Force Horizontal Scale", "\xC3\x89" "chelle horizontale", "Force Horizontal Scale", ICT_LIST, "Auto|1:1|11:10|9:8|640px|656px|672px|704px|720px", 0, 0, 0,
      "Largeur de l'image des jeux.", "Width of the game picture." },
    { "Force Vertical Offset", "D\xC3\xA9" "calage vertical", "Force Vertical Offset", ICT_INT, NULL, -40, 40, 1,
      "Monte ou descend l'image des jeux.", "Moves the game picture up or down." },
    { "Force Vertical Filter", "Filtre vertical", "Force Vertical Filter", ICT_LIST, "Auto|0|1|2", 0, 0, 0,
      "2 = moins de scintillement (flou l\xC3\xA9ger).", "2 = less flicker (slight blur)." },
    { "Force Field Rendering", "Rendu des trames", "Force Field Rendering", ICT_LIST, "Auto|On|Off|TAA", 0, 0, 0,
      "Rendu entrelac\xC3\xA9 des jeux.", "Interlaced rendering of games." },
    { "Fix Pixel Center", "Corriger le centre des pixels", "Fix Pixel Center", ICT_LIST, "No|1/24|1/12", 0, 0, 0,
      "Corrige un petit d\xC3\xA9" "calage de l'image.", "Fixes a small picture offset." },
    { "Disable Alpha Dithering", "Sans tramage", "Disable Alpha Dithering", ICT_LIST, YN, 0, 0, 0,
      "Enl\xC3\xA8ve la trame (points) dans les d\xC3\xA9grad\xC3\xA9s.", "Removes dithering dots from gradients." },
    { "Force Anisotropic Filter", "Filtre anisotrope", "Force Anisotropic Filter", ICT_LIST, YN, 0, 0, 0,
      "Textures plus nettes, mais peut ralentir les jeux.", "Sharper textures, can slow games down." },
    { "Force Widescreen", "Forcer le 16/9", "Force Widescreen", ICT_LIST, "No|3D|2D+3D", 0, 0, 0,
      "Image \xC3\xA9tir\xC3\xA9" "e en 16/9 dans les jeux.", "16:9 picture in games." },
    { "Force Polling Rate", "Lecture des manettes", "Force Polling Rate", ICT_LIST, "No|VSync|1000Hz|500Hz|350Hz|300Hz|250Hz|200Hz|150Hz|120Hz|100Hz", 0, 0, 0,
      "VSync = compatible. 1000Hz = moins de latence.", "VSync = compatible. 1000Hz = lowest latency." },
    { "Invert Camera Stick", "Inverser le stick C", "Invert Camera Stick", ICT_LIST, "No|X|Y|X&Y", 0, 0, 0,
      "Inverse un ou deux axes du stick C.", "Inverts one or both C stick axes." },
    { "Swap Camera Stick", "\xC3\x89" "changer les sticks", "Swap Camera Stick", ICT_LIST, "No|X|Y|X&Y", 0, 0, 0,
      "\xC3\x89" "change des axes du stick C et du stick principal.", "Swaps C stick axes with the main stick." },
    { "Digital Trigger Level", "Seuil des g\xC3\xA2" "chettes L / R", "Digital Trigger Level", ICT_INT, NULL, 0, 200, 10,
      "Enfoncement \xC3\xA0 partir duquel L / R compte comme appuy\xC3\xA9.", "Threshold where L / R counts as fully pressed." },
    { "Emulate Audio Streaming", "\xC3\x89muler le son du disque", "Emulate Audio Streaming", ICT_LIST, "Off|Auto|On", 0, 0, 0,
      "N\xC3\xA9" "cessaire pour la musique de certains jeux sur carte SD.", "Needed for music in some games from SD." },
    { "Emulate Read Speed", "Vitesse du lecteur", "Emulate Read Speed", ICT_LIST, "No|Yes|Wii", 0, 0, 0,
      "Yes : imite la lenteur du lecteur (jeux qui buguent sinon).", "Yes: mimics the disc drive speed (for buggy games)." },
    { "Emulate Broadband Adapter", "\xC3\x89muler l'adaptateur r\xC3\xA9seau", "Emulate Broadband Adapter", ICT_LIST, YN, 0, 0, 0,
      "Jeu en r\xC3\xA9seau sans adaptateur (avec un appareil compatible).", "Network play without a BBA (compatible device)." },
    { "Emulate Memory Card", "\xC3\x89muler la carte m\xC3\xA9moire", "Emulate Memory Card", ICT_LIST, YN, 0, 0, 0,
      "Sauvegardes sur la carte SD au lieu de la carte m\xC3\xA9moire.", "Saves on the SD card instead of a memory card." },
    { "Disable Memory Card", "Couper la carte m\xC3\xA9moire", "Disable Memory Card", ICT_LIST, "No|Slot A|Slot B", 0, 0, 0,
      "Le jeu ne voit pas la carte m\xC3\xA9moire de ce port.", "The game doesn't see the memory card in this slot." },
    { "Disable Hypervisor", "Couper l'hyperviseur", "Disable Hypervisor", ICT_LIST, YN, 0, 0, 0,
      "Lancement brut : beaucoup d'options ne marchent plus.", "Raw boot: many options stop working." },
    { "Prefer Clean Boot", "D\xC3\xA9marrage propre", "Prefer Clean Boot", ICT_LIST, YN, 0, 0, 0,
      "Lance le jeu sans patchs quand c'est possible.", "Boots the game without patches when possible." },
    { "RetroTINK-4K Profile", "Profil RetroTINK-4K", "RetroTINK-4K Profile", ICT_INT, NULL, 0, 12, 1,
      "Profil charg\xC3\xA9 sur le RetroTINK-4K (0 = aucun).", "Profile loaded on the RetroTINK-4K (0 = none)." },
    /* ---- page 4 : reseau ---- */
    { "InitNetwork", "R\xC3\xA9seau au d\xC3\xA9marrage", "Init network at startup", ICT_LIST, YN, 0, 0, 0,
      "Allume l'adaptateur r\xC3\xA9seau (BBA) au d\xC3\xA9marrage.", "Starts the broadband adapter at startup." },
    { "BBAUseDHCP", "Adresse automatique (DHCP)", "Use DHCP", ICT_LIST, YN, 0, 0, 0,
      "Yes : la box donne l'adresse IP toute seule.", "Yes: the router gives the IP address." },
    { "BBALocalIP", "Adresse IP de la console", "Local IP", ICT_TEXT, NULL, 0, 0, 0,
      "Si DHCP = No (ex. 192.168.1.32).", "If DHCP = No (e.g. 192.168.1.32)." },
    { "BBANetmask", "Masque (bits)", "Netmask (bits)", ICT_INT, NULL, 0, 32, 1,
      "24 = 255.255.255.0 (le plus courant).", "24 = 255.255.255.0 (most common)." },
    { "BBAGateway", "Passerelle (box)", "Gateway", ICT_TEXT, NULL, 0, 0, 0,
      "Adresse de la box (ex. 192.168.1.1).", "Router address (e.g. 192.168.1.1)." },
    { "SMBHostIP", "SMB : adresse du PC", "SMB host IP", ICT_TEXT, NULL, 0, 0, 0,
      "Partage Windows : adresse IP du PC.", "Windows share: PC IP address." },
    { "SMBShareName", "SMB : nom du partage", "SMB share", ICT_TEXT, NULL, 0, 0, 0, "", "" },
    { "SMBUserName", "SMB : utilisateur", "SMB user", ICT_TEXT, NULL, 0, 0, 0, "", "" },
    { "SMBPassword", "SMB : mot de passe", "SMB password", ICT_PASS, NULL, 0, 0, 0, "", "" },
    { "FTPHostIP", "FTP : adresse du serveur", "FTP host IP", ICT_TEXT, NULL, 0, 0, 0, "", "" },
    { "FTPPort", "FTP : port", "FTP port", ICT_NUM, NULL, 0, 0, 0, "21 en g\xC3\xA9n\xC3\xA9ral.", "Usually 21." },
    { "FTPUserName", "FTP : utilisateur", "FTP user", ICT_TEXT, NULL, 0, 0, 0, "", "" },
    { "FTPPassword", "FTP : mot de passe", "FTP password", ICT_PASS, NULL, 0, 0, 0, "", "" },
    { "FTPUsePasv", "FTP : mode passif", "FTP passive mode", ICT_LIST, YN, 0, 0, 0, "", "" },
    { "FSPHostIP", "FSP : adresse du serveur", "FSP host IP", ICT_TEXT, NULL, 0, 0, 0, "", "" },
    { "FSPPort", "FSP : port", "FSP port", ICT_NUM, NULL, 0, 0, 0, "", "" },
    { "FSPPassword", "FSP : mot de passe", "FSP password", ICT_PASS, NULL, 0, 0, 0, "", "" },
    { "FSPPathMTU", "FSP : taille des paquets", "FSP path MTU", ICT_INT, NULL, 576, 2030, 2,
      "Entre 576 et 2030 (1500 par d\xC3\xA9" "faut).", "Between 576 and 2030 (default 1500)." },
    { "RT4KHostIP", "RetroTINK-4K : adresse", "RetroTINK-4K host IP", ICT_TEXT, NULL, 0, 0, 0, "", "" },
    { "RT4KPort", "RetroTINK-4K : port", "RetroTINK-4K port", ICT_NUM, NULL, 0, 0, 0, "", "" },
    /* ---- v35 : seulement dans les reglages d'un jeu (swiss/settings/game/<ID>.ini) ---- */
    { "Game Language", "Langue du jeu", "Game Language", ICT_LIST, "English|German|French|Spanish|Italian|Dutch|Japanese|English (US)|Default", 0, 0, 0,
      "Langue de ce jeu (jeux PAL multilingues surtout).", "Language for this game (mostly multi-language PAL games)." },
    { "Force Video Mode", "Mode vid\xC3\xA9o du jeu", "Force Video Mode", ICT_LIST, "Auto|480i|480sf|240p|960i|480p|1080i60|540p60|576i|576sf|288p|1152i|576p|1080i50|540p50", 0, 0, 0,
      "Mode vid\xC3\xA9o forc\xC3\xA9 pour ce jeu.", "Video mode forced for this game." },
};
#define IC_N ((int)(sizeof(ic_items) / sizeof(ic_items[0])))
#define IC_GLOBAL_N 78                          /* (les suivants : reglages d'un jeu seulement) */
static const int ic_page_start[] = { 0, 19, 28, 36, 58, IC_GLOBAL_N };
#define IC_PAGES 5
static const char *const ic_page_fr[IC_PAGES] = { "Syst\xC3\xA8me", "Interface", "Jeux", "Jeux : vid\xC3\xA9o, manette, \xC3\xA9mulation", "R\xC3\xA9seau" };
static const char *const ic_page_en[IC_PAGES] = { "System", "Interface", "Games", "Games: video, pad, emulation", "Network" };

#define IC_VLEN 96
static int ic_on = 0, ic_dirty = 0, ic_quit_warn = 0, ic_kb_item = -1, ic_reveal = -1;
static char ic_val[IC_N][IC_VLEN];      /* valeur actuelle */
static unsigned char ic_has[IC_N];      /* present dans le fichier (ou modifie) */
static unsigned char ic_chg[IC_N];      /* modifie */
static unsigned char ic_del[IC_N];      /* v35 : a enlever du fichier (retour a la valeur par defaut) */
static char ic_msg[160];
static u64 ic_msg_t = 0;
static int ic_file_ok = 0;
/* v35 : fichier edite (global.ini, ou swiss/settings/game/<ID>.ini pour un jeu) */
static char ic_path[NAME_LEN] = GL_SWISS_INI;
static char ic_head[200] = "";                  /* debut d'un nouveau fichier de jeu */

static void ic_note(const char *m) { snprintf(ic_msg, sizeof(ic_msg), "%s", TR(m)); ic_msg_t = now_ms(); }

/* n-ieme valeur d'une liste "a|b|c" */
static int ic_nvals(const char *v) { int n = 1; for (; *v; v++) if (*v == '|') n++; return n; }
static void ic_valn(const char *v, int k, char *out, int cap) {
    while (k > 0 && *v) { if (*v == '|') k--; v++; }
    int n = 0;
    while (v[n] && v[n] != '|') n++;
    if (n > cap - 1) n = cap - 1;
    memcpy(out, v, n); out[n] = 0;
}
static int ic_vidx(const char *v, const char *s) {
    char b[48];
    int n = ic_nvals(v);
    for (int k = 0; k < n; k++) { ic_valn(v, k, b, sizeof(b)); if (!strcmp(b, s)) return k; }
    return -1;
}

static void ic_load(void) {
    memset(ic_has, 0, sizeof(ic_has)); memset(ic_chg, 0, sizeof(ic_chg)); memset(ic_del, 0, sizeof(ic_del));
    for (int i = 0; i < IC_N; i++) ic_val[i][0] = 0;
    ic_dirty = 0; ic_quit_warn = 0; ic_file_ok = 0;
    long len = 0;
    unsigned char *d = read_file(ic_path, 256 * 1024, &len);
    if (!d) return;
    ic_file_ok = 1;
    long i = 0;
    while (i < len) {
        long s = i;
        while (i < len && d[i] != '\n' && d[i] != '\r') i++;
        long n = i - s;
        while (i < len && (d[i] == '\n' || d[i] == '\r')) i++;
        if (n <= 0 || d[s] == '#') continue;
        const unsigned char *eq = memchr(d + s, '=', n);
        if (!eq) continue;
        int kl = (int)(eq - (d + s));
        for (int k = 0; k < IC_N; k++) {
            if ((int)strlen(ic_items[k].key) != kl || memcmp(ic_items[k].key, d + s, kl)) continue;
            int vl = (int)(n - kl - 1);
            if (vl > IC_VLEN - 1) vl = IC_VLEN - 1;
            memcpy(ic_val[k], eq + 1, vl); ic_val[k][vl] = 0;
            ic_has[k] = 1;                 /* (la derniere ligne gagne, comme dans Swiss) */
        }
    }
    free(d);
}

/* ecrit global.ini : remplace les lignes modifiees, ajoute les nouvelles avant la fin */
static int ic_save(void) {
    long len = 0;
    unsigned char *d = read_file(ic_path, 256 * 1024, &len);
    int crlf = 1;
    if (d) { crlf = 0; for (long i = 0; i < len; i++) if (d[i] == '\n') { crlf = i > 0 && d[i - 1] == '\r'; break; } }
    const char *nl = crlf ? "\r\n" : "\n";
    mkdir(SD_ROOT "/swiss", 0777); mkdir(SD_ROOT "/swiss/settings", 0777); mkdir(SD_ROOT "/swiss/settings/game", 0777);
    char tmp[NAME_LEN + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", ic_path);
    FILE *f = fopen(tmp, "wb");
    if (f && !d && ic_head[0]) fprintf(f, "%s", ic_head);       /* (nouveau fichier de jeu : en-tete comme Swiss) */
    if (!f) { free(d); ic_note("Impossible d'\xC3\xA9" "crire sur la carte SD"); return 0; }
    unsigned char done[IC_N];
    memset(done, 0, sizeof(done));
    int ok = 1;
    long i = 0;
    while (d && i < len && ok) {
        long s = i;
        while (i < len && d[i] != '\n' && d[i] != '\r') i++;
        long n = i - s;
        long e = i;
        while (i < len && (d[i] == '\n' || d[i] == '\r') && i - e < 2) { i++; if (d[i - 1] == '\n') break; }
        int rep = -1;
        if (n > 0 && d[s] != '#') {
            const unsigned char *eq = memchr(d + s, '=', n);
            if (eq) {
                int kl = (int)(eq - (d + s));
                for (int k = 0; k < IC_N; k++)
                    if ((ic_chg[k] || ic_del[k]) && (int)strlen(ic_items[k].key) == kl && !memcmp(ic_items[k].key, d + s, kl)) { rep = k; break; }
            }
        }
        if (rep >= 0 && !ic_chg[rep]) continue;                  /* (reglage remis par defaut : ligne enlevee) */
        /* nouvelles lignes : avant la fin des reglages de Swiss */
        if (n >= 23 && !memcmp(d + s, "#!!Swiss Settings End!!", 23)) {
            for (int k = 0; k < IC_N && ok; k++)
                if (ic_chg[k] && !done[k]) { if (fprintf(f, "%s=%s%s", ic_items[k].key, ic_val[k], nl) < 0) ok = 0; done[k] = 1; }
        }
        if (rep >= 0) {
            if (!done[rep]) { if (fprintf(f, "%s=%s", ic_items[rep].key, ic_val[rep]) < 0) ok = 0; done[rep] = 1; }
            else continue;                                    /* (doublon : on ne garde que la premiere) */
        } else if (n > 0 && fwrite(d + s, 1, n, f) != (size_t)n) ok = 0;
        if (i > e && fwrite(d + e, 1, i - e, f) != (size_t)(i - e)) ok = 0;
    }
    if (d && len > 0 && d[len - 1] != '\n' && d[len - 1] != '\r') { /* (pas de retour a la ligne final) */
        int more = 0; for (int k = 0; k < IC_N; k++) if (ic_chg[k] && !done[k]) more = 1;
        if (more && fputs(nl, f) < 0) ok = 0;
    }
    for (int k = 0; k < IC_N && ok; k++)
        if (ic_chg[k] && !done[k]) { if (fprintf(f, "%s=%s%s", ic_items[k].key, ic_val[k], nl) < 0) ok = 0; done[k] = 1; }
    free(d);
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); ic_note("\xC3\x89" "criture incompl\xC3\xA8te (carte pleine ?)"); return 0; }
    remove(ic_path);
    if (rename(tmp, ic_path) != 0) { ic_note("Impossible de remplacer le fichier"); return 0; }
    as_cache = -1;                                           /* (demarrage auto : relire) */
    memset(ic_chg, 0, sizeof(ic_chg)); memset(ic_del, 0, sizeof(ic_del));
    ic_dirty = 0; ic_quit_warn = 0; ic_file_ok = 1;
    ic_note("Enregistr\xC3\xA9 : utilis\xC3\xA9 au prochain lancement d'un jeu");
    return 1;
}

/* ---- v34 : reglages qui valent aussi pour GC-MP3 ---- */
static const char *const ic_gc_keys[] = { "Disable PAD Rumble", "Apps Video Mode", "ShowHiddenFiles", "Hide Unknown file types",
    "BBAUseDHCP", "BBALocalIP", "BBANetmask", "BBAGateway", "Screen Position", "System Sound", "System Language", NULL };
static int ic_is_gc(const char *k) { for (int i = 0; ic_gc_keys[i]; i++) if (!strcmp(ic_gc_keys[i], k)) return 1; return 0; }
static int ic_find(const char *k) { for (int i = 0; i < IC_N; i++) if (!strcmp(ic_items[i].key, k)) return i; return -1; }
static const char *ic_get(const char *k) { int i = ic_find(k); return i >= 0 && ic_has[i] ? ic_val[i] : NULL; }
static int ic_restart_note = 0;                /* 1 = un reglage ne compte qu'au prochain lancement */
static void ic_apply(int boot) {
    const char *v;
    ic_restart_note = 0;
    if ((v = ic_get("Disable PAD Rumble"))) rumble_on = strcmp(v, "Yes") != 0;
    if ((v = ic_get("Apps Video Mode"))) {
        int vp = !strcmp(v, "480i") ? VID_480I : !strcmp(v, "480p") ? VID_480P : !strcmp(v, "Auto") ? VID_AUTO : -1;
        if (vp >= 0 && vp != video_pref) { video_pref = vp; if (vp != VID_480I) tv_crt = 0; if (!boot) ic_restart_note = 1; settings_dirty = 1; }
    }
    v = ic_get("ShowHiddenFiles");
    int sh = v ? !strcmp(v, "Yes") : 1;
    v = ic_get("Hide Unknown file types");
    int hu = v && !strcmp(v, "Yes");
    if (sh != fe_show_hidden || hu != fe_hide_unknown) { fe_show_hidden = sh; fe_hide_unknown = hu; if (!boot && fe) fe_load(); }
    /* reseau : adresse fixe si DHCP = No */
    v = ic_get("BBAUseDHCP");
    const char *ip = ic_get("BBALocalIP"), *gw = ic_get("BBAGateway"), *nm = ic_get("BBANetmask");
    int st = v && !strcmp(v, "No") && ip && ip[0];
    if (net_phase == NET_NONE) {
        net_static = st;
        if (st) {
            int bits = nm && nm[0] ? atoi(nm) : 24;
            if (bits < 0 || bits > 32) bits = 24;
            u32 m = bits ? 0xFFFFFFFFu << (32 - bits) : 0;
            snprintf(ip_str, sizeof(ip_str), "%.15s", ip);
            snprintf(mask_str, sizeof(mask_str), "%u.%u.%u.%u", m >> 24, (m >> 16) & 255, (m >> 8) & 255, m & 255);
            snprintf(gw_str, sizeof(gw_str), "%.15s", gw ? gw : "");
        }
    } else if (st != net_static) ic_restart_note = 1;
    /* reglages de la console (SRAM), comme le fait Swiss a son demarrage */
    if ((v = ic_get("Screen Position"))) {
        int h = atoi(v);
        if (h < -32) h = -32;
        if (h > 32) h = 32;
        if (h != SYS_GetDisplayOffsetH()) SYS_SetDisplayOffsetH((s8)h);
        if (!boot && rmode) { scr_hlive = h - scr_hboot; scr_vi(); }   /* (libogc ne relit la position qu'au demarrage) */
    }
    if ((v = ic_get("System Sound"))) {
        u8 m = !strcmp(v, "Stereo") ? SYS_SOUND_STEREO : SYS_SOUND_MONO;
        if (m != SYS_GetSoundMode()) SYS_SetSoundMode(m);
    }
    audio_mono = SYS_GetSoundMode() == SYS_SOUND_MONO;
    if ((v = ic_get("System Language"))) {
        static const char *const ln[] = { "English", "German", "French", "Spanish", "Italian", "Dutch" };
        for (int i = 0; i < 6; i++) if (!strcmp(v, ln[i])) { if (SYS_GetLanguage() != i) SYS_SetLanguage((u8)i); break; }
        ui_lang_apply();
    }
}
static void ic_use_global(void) { snprintf(ic_path, sizeof(ic_path), "%s", GL_SWISS_INI); ic_head[0] = 0; }
static void indigo_cfg_boot(void) { ic_use_global(); ic_load(); if (ic_file_ok) ic_apply(1); else audio_mono = SYS_GetSoundMode() == SYS_SOUND_MONO; }
/* un reglage de GC-MP3 change (START) : meme valeur dans global.ini, si le reglage y est deja */
static int ic_save(void);
static void ic_set(int k, const char *v);
static int ic_menu_loaded;
static void ic_put(const char *key, const char *val) {
    if (!sd_ok || ic_on) return;
    if (ic_menu_loaded) {                                    /* v37 : page Reglages ouverte : ecrit en la fermant (pas a chaque appui) */
        int k = ic_find(key);
        if (k >= 0 && ic_has[k] && strcmp(ic_val[k], val)) ic_set(k, val);
        return;
    }
    ic_use_global();
    ic_load();
    int k = ic_find(key);
    if (!ic_file_ok || k < 0 || !ic_has[k] || !strcmp(ic_val[k], val)) return;
    ic_set(k, val);
    ic_save();
}


static void ic_set(int k, const char *v) {
    if (strcmp(ic_val[k], v) || !ic_has[k]) {
        snprintf(ic_val[k], IC_VLEN, "%s", v);
        ic_has[k] = 1; ic_chg[k] = 1; ic_del[k] = 0; ic_dirty = 1; ic_quit_warn = 0;
    }
}
static void ic_change(int k, int d) {
    const ic_item *it = &ic_items[k];
    char b[48];
    if (it->type == ICT_LIST) {
        int n = ic_nvals(it->vals), c = ic_has[k] ? ic_vidx(it->vals, ic_val[k]) : -1;
        c = c < 0 ? (d > 0 ? 0 : n - 1) : (c + d + n) % n;
        ic_valn(it->vals, c, b, sizeof(b));
        ic_set(k, b);
    } else if (it->type == ICT_INT) {
        int v = ic_has[k] && ic_val[k][0] ? atoi(ic_val[k]) : !strcmp(it->key, "FSPPathMTU") ? 1500 : (it->lo <= 0 && it->hi >= 0 ? 0 : it->lo);
        if (ic_has[k] && ic_val[k][0]) v += d * it->step;
        if (v < it->lo) v = it->hi; else if (v > it->hi) v = it->lo;
        if (it->lo < 0) snprintf(b, sizeof(b), "%+d", v); else snprintf(b, sizeof(b), "%d", v);
        ic_set(k, b);
    }
}
static void ic_kb_done(const char *t) {
    int k = ic_kb_item;
    ic_kb_item = -1;
    if (k < 0 || k >= IC_N) return;
    if (ic_items[k].type == ICT_NUM) {
        for (const char *p = t; *p; p++) if (*p < '0' || *p > '9') { ic_note("Chiffres seulement"); return; }
        if (*t && atol(t) > 65535) { ic_note("Port entre 0 et 65535"); return; }
    }
    for (const char *p = t; *p; p++) if (*p == '\r' || *p == '\n') return;
    ic_set(k, t);
}

/* valeur affichee (Yes / No en francais) */
static void ic_shown(int k, char *out, int cap) {
    const ic_item *it = &ic_items[k];
    if (!ic_has[k]) { snprintf(out, cap, "%s", ui_en ? "(default)" : "(par d\xC3\xA9" "faut)"); return; }
    const char *v = ic_val[k];
    if (it->type == ICT_PASS && ic_reveal != k) {
        int n = (int)strlen(v); if (n > 12) n = 12;
        int i = 0; for (; i < n && i < cap - 1; i++) out[i] = '*'; out[i] = 0;
        if (!n) snprintf(out, cap, "%s", ui_en ? "(empty)" : "(vide)");
        return;
    }
    if (!v[0]) { snprintf(out, cap, "%s", ui_en ? "(empty)" : "(vide)"); return; }
    if (!ui_en && it->type == ICT_LIST) {
        static const char *const m[][2] = { { "Yes", "Oui" }, { "No", "Non" }, { "Off", "Non" }, { "On", "Oui" }, { "Disabled", "D\xC3\xA9sactiv\xC3\xA9" },
                                           { "None", "Aucun" }, { "Default", "Par d\xC3\xA9" "faut" }, { "Lazy", "Paresseux" }, { "Fullwidth", "Plein \xC3\xA9" "cran" },
                                           { "Carousel", "Carrousel" }, { "Wii", "Wii" }, { "Region Switch", "Selon la r\xC3\xA9gion" } };
        for (unsigned j = 0; j < sizeof(m) / sizeof(m[0]); j++) if (!strcmp(v, m[j][0])) { snprintf(out, cap, "%s", m[j][1]); return; }
    }
    snprintf(out, cap, "%s", v);
}

/* ---------- v35 : listes de reglages (avec titres de groupe) ---------- */
#define IC_HDR(h) (-1 - (h))                    /* ligne de titre */
enum { ICH_SYS, ICH_UI, ICH_GAMES, ICH_DEF, ICH_NET, ICH_GAME, ICH_VIDEO, ICH_VGAME, ICH_PAD, ICH_EMU, ICH_COUNT };
static const char *const ic_hdr_fr[ICH_COUNT] = { "Mat\xC3\xA9riel", "Interface", "Lancement des jeux",
    "Jeux par d\xC3\xA9" "faut : vid\xC3\xA9o, manette, \xC3\xA9mulation", "R\xC3\xA9seau", "Ce jeu seulement",
    "Vid\xC3\xA9o de la console", "Vid\xC3\xA9o des jeux", "Manettes (dans les jeux)", "\xC3\x89mulation (dans les jeux)" };
static const char *const ic_hdr_en[ICH_COUNT] = { "Hardware", "Interface", "Launching games",
    "Game defaults: video, pad, emulation", "Network", "This game only",
    "Console video", "Game video", "Controllers (in games)", "Emulation (in games)" };
typedef struct { short r[128]; int n, sel, top; } ic_list;
/* v57 : sections Video / Manettes / Emulation / Jeux / Reseau (plus de sections Indigo et Swiss ;
   les reglages qui ne servaient qu'au menu d'Indigo ne sont plus montres, le fichier les garde) */
#define IC_NSEC 5
static ic_list ic_lsec[IC_NSEC], ic_lgame;
static void ic_list_add_page(ic_list *L, int hdr, int page) {
    L->r[L->n++] = (short)IC_HDR(hdr);
    for (int i = ic_page_start[page]; i < ic_page_start[page + 1] && L->n < 128; i++) L->r[L->n++] = (short)i;
}
static void ic_lists_build(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    /* (H_(ICH_x) = titre de groupe, code par ICH_x + 1 ; NULL = fin) */
    #define H_(h) ((const char *)(long)((h) + 1))
    static const char *const sk[IC_NSEC][40] = {
        { H_(ICH_VIDEO), "System Video", "Screen Position", "Apps Video Mode", "AVECompat", "Force DTV Status", "RT4KOptim",
          H_(ICH_VGAME), "Force NTSC Video Mode", "Force PAL Video Mode", "Force Horizontal Scale", "Force Vertical Offset", "Force Vertical Filter",
          "Force Field Rendering", "Fix Pixel Center", "Disable Alpha Dithering", "Force Anisotropic Filter", "Force Widescreen",
          "Force Video Active", "Pause for resolution change", "Disable Video Patches", "RetroTINK-4K Profile", NULL },
        { H_(ICH_PAD), "Disable PAD Rumble", "Disable PAD Recalibration", "Force Polling Rate", "Invert Camera Stick", "Swap Camera Stick",
          "Digital Trigger Level", NULL },
        { H_(ICH_EMU), "Emulate Audio Streaming", "Emulate Read Speed", "Emulate Memory Card", "Disable Memory Card", "Disable MemCard PRO GameID",
          "Emulate Broadband Adapter", "Disable Hypervisor", "Simulated MRAM Size", "Configure Audio Buffer", NULL },
        { H_(ICH_GAMES), "IGRType", "BS2Boot", "AutoCheats", "Prefer Clean Boot", "System Language", "System Sound", "System Boot Mode",
          H_(ICH_SYS), "SD/IDE Speed", "Init DVD Drive at startup", "Stop DVD Drive motor", "Enable USB Gecko", "Wait for USB Gecko", "Enable WiiRD debug", NULL },
        { H_(ICH_NET), "InitNetwork", "BBAUseDHCP", "BBALocalIP", "BBANetmask", "BBAGateway", "SMBHostIP", "SMBShareName", "SMBUserName", "SMBPassword",
          "FTPHostIP", "FTPPort", "FTPUserName", "FTPPassword", "FTPUsePasv", "FSPHostIP", "FSPPort", "FSPPassword", "FSPPathMTU", "RT4KHostIP", "RT4KPort", NULL },
    };
    for (int s = 0; s < IC_NSEC; s++) {
        ic_list *L = &ic_lsec[s];
        L->n = 0;
        for (int i = 0; sk[s][i] && L->n < 128; i++) {
            long h = (long)sk[s][i] - 1;
            if (h >= 0 && h < ICH_COUNT) { L->r[L->n++] = (short)IC_HDR(h); continue; }
            int k = ic_find(sk[s][i]);
            if (k >= 0) L->r[L->n++] = (short)k;
        }
        L->sel = 1; L->top = 0;
    }
    #undef H_
    static const char *const gk[] = { "Game Language", "Force Video Mode", "Force Horizontal Scale", "Force Vertical Offset", "Force Vertical Filter",
        "Force Field Rendering", "Fix Pixel Center", "Disable Alpha Dithering", "Force Anisotropic Filter", "Force Widescreen", "Force Polling Rate",
        "Invert Camera Stick", "Swap Camera Stick", "Digital Trigger Level", "Emulate Audio Streaming", "Emulate Read Speed",
        "Emulate Broadband Adapter", "Disable Memory Card", "Disable Hypervisor", "Prefer Clean Boot", "RetroTINK-4K Profile", NULL };
    ic_lgame.n = 0; ic_lgame.r[ic_lgame.n++] = (short)IC_HDR(ICH_GAME);
    for (int i = 0; gk[i]; i++) { int k = ic_find(gk[i]); if (k >= 0) ic_lgame.r[ic_lgame.n++] = (short)k; }
    ic_lgame.sel = 1;
}
static int ic_list_item(ic_list *L) { return L->sel >= 0 && L->sel < L->n && L->r[L->sel] >= 0 ? L->r[L->sel] : -1; }
static void ic_list_move(ic_list *L, int d) {
    for (int g = 0; g < L->n; g++) {
        L->sel = (L->sel + d + L->n) % L->n;
        if (L->r[L->sel] >= 0) break;
    }
    ic_reveal = -1;
}
/* touches sur la ligne choisie (Gauche / Droite, A, X) */
static void ic_list_input(ic_list *L, u32 down, u32 nav, int fire) {
    if (fire && (nav & PAD_BUTTON_DOWN)) ic_list_move(L, 1);
    if (fire && (nav & PAD_BUTTON_UP)) ic_list_move(L, -1);
    int k = ic_list_item(L);
    if (k < 0) { ic_list_move(L, 1); return; }
    const ic_item *it = &ic_items[k];
    if (fire && (nav & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) && (it->type == ICT_LIST || it->type == ICT_INT))
        ic_change(k, (nav & PAD_BUTTON_RIGHT) ? 1 : -1);
    if (down & PAD_BUTTON_A) {
        if (it->type == ICT_LIST || it->type == ICT_INT) ic_change(k, 1);
        else { ic_kb_item = k; kb_start(KB_ICTEXT, ic_has[k] ? ic_val[k] : "", -1, -1); }
    }
    if ((down & PAD_BUTTON_X) && it->type == ICT_PASS) ic_reveal = ic_reveal == k ? -1 : k;
}
static void ic_list_draw(ic_list *L, f32 x, f32 y, f32 w, int vis, f32 rh) {
    char buf[160], v[IC_VLEN + 16], fv[IC_VLEN + 16];
    if (L->sel < L->top + 1) L->top = L->sel - 1;
    if (L->sel >= L->top + vis) L->top = L->sel - vis + 1;
    if (L->top > L->n - vis) L->top = L->n - vis;
    if (L->top < 0) L->top = 0;
    for (int i = L->top; i < L->n && i < L->top + vis; i++) {
        f32 ry = y + (i - L->top) * rh;
        int r = L->r[i];
        if (r < 0) {                                              /* titre de groupe */
            const char *h = ui_en ? ic_hdr_en[-1 - r] : ic_hdr_fr[-1 - r];
            gx_text1(F_SMALL, x + 12, ry + 16, h, RGBA(255, 210, 120, 255));
            int tw = text_width(F_SMALL, h, 0);
            gx_rect(x + 20 + tw, ry + 11, w - 32 - tw, 1, RGBA(110, 80, 190, 200));
            continue;
        }
        int on = i == L->sel;
        gp_zone(x, ry, w, rh - 2, &L->sel, i, GPZ_SEL);
        if (on) gx_rrect_fill(x, ry, w, rh - 2, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        const ic_item *it = &ic_items[r];
        col_t lc = on ? RGBA(255, 255, 255, 255) : RGBA(225, 215, 255, 255);
        if (ic_is_gc(it->key)) gx_rrect_fill(x + 4, ry + 8, 6, 6, 3, RGBA(90, 230, 130, 255), RGBA(50, 180, 90, 255));   /* (vaut aussi pour GC-MP3) */
        fit_text(F_MED, ui_en ? it->en : it->fr, (int)w - 240, buf, sizeof(buf));
        gx_text1(F_MED, x + 14, ry + 16, buf, lc);
        ic_shown(r, v, sizeof(v));
        fit_text(F_MED, v, 200, fv, sizeof(fv));
        int vw = text_width(F_MED, fv, 0);
        f32 vx = x + w - 22 - vw;
        col_t vc = !ic_has[r] ? RGBA(130, 115, 185, 255) : ic_chg[r] ? RGBA(255, 210, 120, 255) : on ? RGBA(255, 255, 255, 255) : RGBA(190, 160, 255, 255);
        gx_text1(F_MED, vx, ry + 16, fv, vc);
        if (on && (it->type == ICT_LIST || it->type == ICT_INT)) {
            gx_tri(vx - 12, ry + 11, vx - 6, ry + 6, vx - 6, ry + 16, RGBA(255, 255, 255, 200));
            gx_tri(x + w - 16, ry + 6, x + w - 16, ry + 16, x + w - 10, ry + 11, RGBA(255, 255, 255, 200));
        }
    }
    if (L->top > 0) gx_tri(x + w / 2 - 7, y - 2, x + w / 2 + 7, y - 2, x + w / 2, y - 8, RGBA(190, 160, 255, 220));
    if (L->top + vis < L->n) gx_tri(x + w / 2 - 7, y + vis * rh, x + w / 2 + 7, y + vis * rh, x + w / 2, y + vis * rh + 6, RGBA(190, 160, 255, 220));
}
/* aide du reglage choisi (2 lignes en bas) */
static void ic_help_draw(int k, f32 x, f32 y, f32 w) {
    char fv[IC_VLEN + 16];
    if (k < 0) return;
    const ic_item *it = &ic_items[k];
    gx_text1(F_SMALL, x, y, it->key, RGBA(150, 135, 210, 255));
    if (ic_is_gc(it->key)) {
        gx_rrect_fill(x + w - 150, y - 9, 6, 6, 3, RGBA(90, 230, 130, 255), RGBA(50, 180, 90, 255));
        gx_text_r(F_SMALL, x + w, y, ui_en ? "Also used by GC Station" : "Vaut aussi pour GC Station", RGBA(120, 230, 150, 255));
    }
    const char *help = ui_en ? it->den : it->dfr;
    if (help && help[0]) { fit_text(F_SMALL, help, (int)w, fv, sizeof(fv)); gx_text1(F_SMALL, x, y + 18, fv, RGBA(210, 200, 240, 255)); }
}

/* ---------- sections Indigo et Swiss de la page Reglages (START) ---------- */
static int ic_menu_loaded = 0;
static void ic_menu_ensure(void) {
    if (ic_menu_loaded) return;
    ic_lists_build();
    ic_use_global();
    ic_load();
    ic_menu_loaded = 1;
}
static ic_list *ic_menu_list(int sec) { return &ic_lsec[sec >= 0 && sec < IC_NSEC ? sec : 0]; }
static void ic_menu_input(int sec, u32 down, u32 nav, int fire) {
    ic_menu_ensure();
    ic_list_input(ic_menu_list(sec), down, nav, fire);
    if (down & PAD_BUTTON_Y) {
        if (!ic_dirty) ic_note("Rien \xC3\xA0 enregistrer");
        else if (ic_save()) {
            ic_apply(0);
            if (ic_restart_note) ic_note("Enregistr\xC3\xA9 : vid\xC3\xA9o / r\xC3\xA9seau de GC Station au prochain lancement");
            else ic_note("Enregistr\xC3\xA9 : utilis\xC3\xA9 au prochain lancement d'un jeu");
        }
    }
}
/* fermeture de la page : les reglages d'Indigo changes sont enregistres tout seuls */
static void ic_menu_close(void) {
    if (ic_menu_loaded && ic_dirty && ic_save()) ic_apply(0);
    ic_menu_loaded = 0;
}
static void ic_menu_draw(int sec, f32 x, f32 y, f32 w, int vis, f32 rh, f32 help_y) {
    ic_menu_ensure();
    ic_list *L = ic_menu_list(sec);
    ic_list_draw(L, x, y, w, vis, rh);
    ic_help_draw(ic_list_item(L), x + 8, help_y, w - 16);
    if (!ic_file_ok && !ic_dirty) gx_text_r(F_SMALL, x + w - 4, y - 54, ui_en ? "default settings" : "r\xC3\xA9glages par d\xC3\xA9" "faut", RGBA(150, 135, 210, 255));
}
static int ic_menu_dirty(void) { return ic_menu_loaded && ic_dirty; }

/* ---------- v35 : reglages d'un jeu (fenetre ouverte depuis Mes jeux) ---------- */
static char ic_game_title[96];
static void indigo_game_open(const char *id, const char *title) {
    ic_lists_build();
    snprintf(ic_path, sizeof(ic_path), "%s/swiss/settings/game/%.4s.ini", SD_ROOT, id);
    snprintf(ic_head, sizeof(ic_head), "# Game specific configuration file. Created by Swiss\r\nID=%.4s\r\nName=%.64s\r\n", id, title);
    ic_load();
    snprintf(ic_game_title, sizeof(ic_game_title), "%s", title);
    ic_lgame.sel = 1; ic_lgame.top = 0;
    ic_on = 1; ic_kb_item = -1; ic_reveal = -1;
    crumb("reglages du jeu %.4s", id);
}
static void ic_game_close(void) {
    int saved = ic_dirty ? ic_save() : 0;
    ic_on = 0;
    ic_use_global();
    ic_menu_loaded = 0;
    if (saved) gl_note("R\xC3\xA9glages du jeu enregistr\xC3\xA9s");
}
static void indigo_cfg_input(u32 down, u32 nav, int fire) {
    ic_list_input(&ic_lgame, down, nav, fire);
    if (down & PAD_BUTTON_Y) {                                       /* tout remettre par defaut */
        int any = 0;
        for (int i = 0; i < ic_lgame.n; i++) { int k = ic_lgame.r[i]; if (k >= 0 && ic_has[k]) any = 1; }
        if (any) {
            for (int i = 0; i < ic_lgame.n; i++) { int k = ic_lgame.r[i]; if (k >= 0 && ic_has[k]) { ic_has[k] = 0; ic_val[k][0] = 0; ic_chg[k] = 0; ic_del[k] = 1; } }
            ic_dirty = 1;
            ic_note("Tout est revenu aux r\xC3\xA9glages par d\xC3\xA9" "faut (B pour enregistrer)");
        }
    }
    if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) ic_game_close();
}
static void draw_indigo_cfg(void) {
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
    const f32 x = 40, y = 40, w = 560, h = 384;
    gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 252), RGBA(8, 5, 26, 252));
    draw_panel(x, y, w, h);
    char t[160], ft[160];
    snprintf(t, sizeof(t), "%s", ic_game_title);
    gx_text1(F_LARGE, x + 18, y + 32, ui_en ? "Game settings" : "R\xC3\xA9glages du jeu", RGBA(255, 255, 255, 255));
    fit_text(F_SMALL, t, 300, ft, sizeof(ft));
    gx_text_r(F_SMALL, x + w - 18, y + 30, ft, RGBA(190, 160, 255, 255));
    ic_list_draw(&ic_lgame, x + 12, y + 50, w - 24, 11, 23);
    gx_rect(x + 18, y + h - 62, w - 36, 1, RGBA(76, 52, 150, 200));
    ic_help_draw(ic_list_item(&ic_lgame), x + 22, y + h - 44, w - 44);
    if (now_ms() - ic_msg_t < 4000) {
        fit_text(F_SMALL, ic_msg, (int)w - 44, ft, sizeof(ft));
        gx_rrect_fill(x + 14, y + h - 40, w - 28, 22, 6, RGBA(40, 20, 90, 250), RGBA(30, 14, 70, 250));
        gx_text1(F_SMALL, x + 22, y + h - 24, ft, RGBA(255, 210, 120, 255));
    }
    /* legende */
    const char *K[5], *L[5];
    int cnt = 0;
    int k = ic_list_item(&ic_lgame);
    K[cnt] = "A"; L[cnt++] = k >= 0 && (ic_items[k].type == ICT_LIST || ic_items[k].type == ICT_INT) ? "CHANGER" : "MODIFIER";
    K[cnt] = "Y"; L[cnt++] = "PAR D\xC3\x89" "FAUT";
    K[cnt] = "B"; L[cnt++] = "ENREGISTRER ET FERMER";
    const f32 lx = 11, lw = 618, cy = 438.5f;
    gx_rrect_fill(lx, 419, lw, 39, 9, RGBA(10, 6, 30, 255), RGBA(10, 6, 30, 255));
    legend_box(lx, lw);
    f32 total = 0;
    for (int i = 0; i < cnt; i++) total += 25 + text_width(F_SMALL, L[i], 1);
    f32 space = (lw - 40 - total) / (cnt - 1), cx = lx + 20;
    for (int i = 0; i < cnt; i++) {
        draw_mini_badge(cx + 10, cy, K[i]);
        cx += 25;
        cx += gx_text(F_SMALL, cx, cy + 4.5f, L[i], RGBA(208, 198, 240, 255), RGBA(208, 198, 240, 255), 1) + space;
    }
}

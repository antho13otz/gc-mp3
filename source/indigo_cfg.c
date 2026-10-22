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
      "Reboot : A+Z+START en jeu red\xC3\xA9marre (sur GC-MP3 si c'est ton ipl.dol).", "Reboot: A+Z+START in game restarts (into GC-MP3 if it is your ipl.dol)." },
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
};
#define IC_N ((int)(sizeof(ic_items) / sizeof(ic_items[0])))
static const int ic_page_start[] = { 0, 19, 28, 36, 58, IC_N };
#define IC_PAGES 5
static const char *const ic_page_fr[IC_PAGES] = { "Syst\xC3\xA8me", "Interface", "Jeux", "Jeux : vid\xC3\xA9o, manette, \xC3\xA9mulation", "R\xC3\xA9seau" };
static const char *const ic_page_en[IC_PAGES] = { "System", "Interface", "Games", "Games: video, pad, emulation", "Network" };

#define IC_VLEN 96
static int ic_on = 0, ic_page = 0, ic_sel = 0, ic_dirty = 0, ic_quit_warn = 0, ic_kb_item = -1, ic_reveal = -1;
static char ic_val[IC_N][IC_VLEN];      /* valeur actuelle */
static unsigned char ic_has[IC_N];      /* present dans le fichier (ou modifie) */
static unsigned char ic_chg[IC_N];      /* modifie */
static char ic_msg[160];
static u64 ic_msg_t = 0;
static int ic_file_ok = 0;

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
    memset(ic_has, 0, sizeof(ic_has)); memset(ic_chg, 0, sizeof(ic_chg));
    for (int i = 0; i < IC_N; i++) ic_val[i][0] = 0;
    ic_dirty = 0; ic_quit_warn = 0; ic_file_ok = 0;
    long len = 0;
    unsigned char *d = read_file(GL_SWISS_INI, 256 * 1024, &len);
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
    unsigned char *d = read_file(GL_SWISS_INI, 256 * 1024, &len);
    int crlf = 1;
    if (d) { crlf = 0; for (long i = 0; i < len; i++) if (d[i] == '\n') { crlf = i > 0 && d[i - 1] == '\r'; break; } }
    const char *nl = crlf ? "\r\n" : "\n";
    mkdir(SD_ROOT "/swiss", 0777); mkdir(SD_ROOT "/swiss/settings", 0777);
    char tmp[NAME_LEN];
    snprintf(tmp, sizeof(tmp), "%s.tmp", GL_SWISS_INI);
    FILE *f = fopen(tmp, "wb");
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
                    if (ic_chg[k] && (int)strlen(ic_items[k].key) == kl && !memcmp(ic_items[k].key, d + s, kl)) { rep = k; break; }
            }
        }
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
    remove(GL_SWISS_INI);
    if (rename(tmp, GL_SWISS_INI) != 0) { ic_note("Impossible de remplacer le fichier"); return 0; }
    as_cache = -1;                                           /* (demarrage auto : relire) */
    memset(ic_chg, 0, sizeof(ic_chg));
    ic_dirty = 0; ic_quit_warn = 0; ic_file_ok = 1;
    ic_note("Enregistr\xC3\xA9 : Indigo / Swiss l'utilisera \xC3\xA0 son prochain d\xC3\xA9marrage");
    return 1;
}

static void indigo_cfg_open(void) {
    ic_load();
    ic_on = 1; ic_sel = ic_page_start[ic_page]; ic_kb_item = -1; ic_reveal = -1;
    if (!ic_file_ok) ic_note("Pas encore de global.ini : il sera cr\xC3\xA9\xC3\xA9 \xC3\xA0 l'enregistrement");
    crumb("reglages indigo");
}

static void ic_set(int k, const char *v) {
    if (strcmp(ic_val[k], v) || !ic_has[k]) {
        snprintf(ic_val[k], IC_VLEN, "%s", v);
        ic_has[k] = 1; ic_chg[k] = 1; ic_dirty = 1; ic_quit_warn = 0;
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

static void ic_close(void) {
    if (ic_dirty && !ic_quit_warn) { ic_quit_warn = 1; ic_note("Pas enregistr\xC3\xA9 : Y pour enregistrer, B encore pour quitter sans enregistrer"); return; }
    ic_on = 0; ic_dirty = 0; ic_quit_warn = 0;
}

static void indigo_cfg_input(u32 down, u32 nav, int fire) {
    int a = ic_page_start[ic_page], b = ic_page_start[ic_page + 1];
    if (fire && (nav & PAD_BUTTON_DOWN)) { ic_sel++; if (ic_sel >= b) ic_sel = a; ic_reveal = -1; }
    if (fire && (nav & PAD_BUTTON_UP)) { ic_sel--; if (ic_sel < a) ic_sel = b - 1; ic_reveal = -1; }
    if (down & (PAD_TRIGGER_R | PAD_TRIGGER_L)) {
        ic_page = (ic_page + ((down & PAD_TRIGGER_R) ? 1 : IC_PAGES - 1)) % IC_PAGES;
        ic_sel = ic_page_start[ic_page]; ic_reveal = -1;
        return;
    }
    if (ic_sel < a || ic_sel >= b) ic_sel = a;
    const ic_item *it = &ic_items[ic_sel];
    if (fire && (nav & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) && (it->type == ICT_LIST || it->type == ICT_INT))
        ic_change(ic_sel, (nav & PAD_BUTTON_RIGHT) ? 1 : -1);
    if (down & PAD_BUTTON_A) {
        if (it->type == ICT_LIST || it->type == ICT_INT) ic_change(ic_sel, 1);
        else { ic_kb_item = ic_sel; kb_start(KB_ICTEXT, ic_has[ic_sel] ? ic_val[ic_sel] : "", -1, -1); }
    }
    if ((down & PAD_BUTTON_X) && it->type == ICT_PASS) ic_reveal = ic_reveal == ic_sel ? -1 : ic_sel;
    if (down & PAD_BUTTON_Y) ic_save();
    if (down & (PAD_BUTTON_B | PAD_BUTTON_START)) ic_close();
}

static void draw_indigo_cfg(void) {
    gx_quad4(0, 0, 640, 480, RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150), RGBA(0, 0, 0, 150));
    const f32 x = 40, y = 40, w = 560, h = 384, rh = 23;
    gx_rrect_fill(x, y, w, h, 10, RGBA(14, 8, 44, 252), RGBA(8, 5, 26, 252));
    draw_panel(x, y, w, h);
    gx_text1(F_LARGE, x + 18, y + 32, ui_en ? "Indigo / Swiss settings" : "R\xC3\xA9glages Indigo / Swiss", RGBA(255, 255, 255, 255));
    if (ic_dirty) gx_text_r(F_SMALL, x + w - 18, y + 30, "modifi\xC3\xA9", RGBA(255, 210, 120, 255));
    /* pages (L / R) */
    char buf[160], v[IC_VLEN + 16], fv[IC_VLEN + 16];
    f32 px = x + 18;
    for (int p = 0; p < IC_PAGES; p++) {
        const char *nm = ui_en ? ic_page_en[p] : ic_page_fr[p];
        if (p == 3) nm = ui_en ? "Game defaults" : "Jeux (d\xC3\xA9" "faut)";
        int tw = text_width(F_SMALL, nm, 0) + 16;
        if (p == ic_page) gx_rrect_fill(px, y + 42, tw, 20, 6, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        gp_zone(px, y + 42, tw, 20, &ic_page, p, GPZ_SEL);
        gx_text1(F_SMALL, px + 8, y + 57, nm, p == ic_page ? RGBA(255, 255, 255, 255) : RGBA(170, 155, 225, 255));
        px += tw + 6;
    }
    gx_text_r(F_SMALL, x + w - 18, y + 57, "L / R", RGBA(150, 135, 210, 255));
    int a = ic_page_start[ic_page], b = ic_page_start[ic_page + 1];
    const int vis = 10;
    int start = ic_sel - vis / 2;
    if (start > b - vis) start = b - vis;
    if (start < a) start = a;
    for (int i = start; i < b && i < start + vis; i++) {
        f32 ry = y + 72 + (i - start) * rh;
        int on = i == ic_sel;
        gp_zone(x + 12, ry, w - 24, rh - 2, &ic_sel, i, GPZ_SEL);
        if (on) gx_rrect_fill(x + 12, ry, w - 24, rh - 2, 5, RGBA(116, 46, 236, 255), RGBA(74, 22, 172, 255));
        const ic_item *it = &ic_items[i];
        col_t lc = on ? RGBA(255, 255, 255, 255) : RGBA(225, 215, 255, 255);
        fit_text(F_MED, ui_en ? it->en : it->fr, 300, buf, sizeof(buf));
        gx_text1(F_MED, x + 24, ry + 16, buf, lc);
        ic_shown(i, v, sizeof(v));
        fit_text(F_MED, v, 190, fv, sizeof(fv));
        int vw = text_width(F_MED, fv, 0);
        f32 vx = x + w - 34 - vw;
        col_t vc = !ic_has[i] ? RGBA(130, 115, 185, 255) : ic_chg[i] ? RGBA(255, 210, 120, 255) : on ? RGBA(255, 255, 255, 255) : RGBA(190, 160, 255, 255);
        gx_text1(F_MED, vx, ry + 16, fv, vc);
        if (on && (it->type == ICT_LIST || it->type == ICT_INT)) {
            gx_tri(vx - 12, ry + 11, vx - 6, ry + 6, vx - 6, ry + 16, RGBA(255, 255, 255, 200));
            gx_tri(x + w - 28, ry + 6, x + w - 28, ry + 16, x + w - 22, ry + 11, RGBA(255, 255, 255, 200));
        }
    }
    if (start > a) gx_tri(x + w / 2 - 7, y + 71, x + w / 2 + 7, y + 71, x + w / 2, y + 65, RGBA(190, 160, 255, 220));
    if (start + vis < b) gx_tri(x + w / 2 - 7, y + 72 + vis * rh, x + w / 2 + 7, y + 72 + vis * rh, x + w / 2, y + 78 + vis * rh, RGBA(190, 160, 255, 220));
    /* aide du reglage choisi */
    const ic_item *it = &ic_items[ic_sel >= a && ic_sel < b ? ic_sel : a];
    const char *help = ui_en ? it->den : it->dfr;
    gx_rect(x + 18, y + h - 62, w - 36, 1, RGBA(76, 52, 150, 200));
    snprintf(buf, sizeof(buf), "%s", it->key);
    gx_text1(F_SMALL, x + 22, y + h - 44, buf, RGBA(150, 135, 210, 255));
    if (help && help[0]) { fit_text(F_SMALL, help, (int)w - 44, fv, sizeof(fv)); gx_text1(F_SMALL, x + 22, y + h - 26, fv, RGBA(210, 200, 240, 255)); }
    if (now_ms() - ic_msg_t < 4000) {
        fit_text(F_SMALL, ic_msg, (int)w - 44, fv, sizeof(fv));
        gx_rrect_fill(x + 14, y + h - 40, w - 28, 22, 6, RGBA(40, 20, 90, 250), RGBA(30, 14, 70, 250));
        gx_text1(F_SMALL, x + 22, y + h - 24, fv, RGBA(255, 210, 120, 255));
    }
    /* legende (sans Z / START : ils ne servent pas ici) */
    const char *K[6], *L[6];
    int cnt = 0;
    K[cnt] = "A"; L[cnt++] = (it->type == ICT_LIST || it->type == ICT_INT) ? "CHANGER" : "MODIFIER";
    if (it->type == ICT_PASS) { K[cnt] = "X"; L[cnt++] = "VOIR"; }
    K[cnt] = "Y"; L[cnt++] = "ENREGISTRER";
    K[cnt] = "B"; L[cnt++] = "FERMER";
    K[cnt] = "L"; L[cnt++] = "";
    K[cnt] = "R"; L[cnt++] = "PAGE";
    const f32 lx = 11, lw = 618, cy = 438.5f;
    gx_rrect_fill(lx, 419, lw, 39, 9, RGBA(10, 6, 30, 255), RGBA(10, 6, 30, 255));   /* (cache la legende de l'onglet) */
    legend_box(lx, lw);
    f32 total = 0;
    for (int i = 0; i < cnt; i++) total += 20 + 5 + text_width(F_SMALL, L[i], 1);
    f32 space = (lw - 40 - total) / (cnt - 2), cx = lx + 20;
    for (int i = 0; i < cnt; i++) {
        draw_mini_badge(cx + 10, cy, K[i]);
        cx += L[i][0] ? 25 : 22;
        if (L[i][0]) cx += gx_text(F_SMALL, cx, cy + 4.5f, L[i], RGBA(208, 198, 240, 255), RGBA(208, 198, 240, 255), 1) + space;
    }
}

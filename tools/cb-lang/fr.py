# GC Station : traduction francaise de Cube Browser (source/cb).
# Cle = texte anglais tel qu'il est ecrit dans le code C (echappements compris, morceaux colles bout a bout).
# python3 tools/cb-lang/gen.py ecrit source/cb/lang_fr.h (textes en Latin-1 pour la police du navigateur).
TR = {
# --- reglages : categories
"Network": "Réseau", "Controls": "Commandes", "Display": "Affichage", "Music": "Musique",
"Browsing": "Navigation", "Security": "Sécurité", "Updates": "Mises à jour", "Data": "Données",
# --- reglages : choix
"Auto": "Auto", "Standard (640)": "Standard (640)", "Full (704)": "Plein (704)",
"Jet Black": "Noir de jais", "Platinum": "Platine", "Spice": "Épices",
"Small": "Petite", "Medium": "Moyenne", "Large": "Grande", "Off": "Non", "On": "Oui",
"Keep its name": "Garder son nom", "swiss_r + number": "swiss_r + numéro",
"boot.dol (card root)": "boot.dol (racine de la carte)", "ipl.dol (card root)": "ipl.dol (racine de la carte)",
"Load Swiss .dol": "Lancer le .dol de Swiss", "Restart console": "Redémarrer la console",
"Custom": "Personnalisé", "Lynx (text mode)": "Lynx (mode texte)", "Classic phone": "Vieux téléphone",
"(blank)": "(vide)",
# --- reglages : lignes et aides
"Automatic IP (DHCP)": "IP automatique (DHCP)",
"Get an address from your router. Changes apply next launch.": "Prend une adresse donnée par la box. Appliqué au prochain lancement.",
"IP address": "Adresse IP", "Used only when DHCP is off.": "Utilisé seulement si le DHCP est coupé.",
"Subnet mask": "Masque de sous-réseau", "Gateway": "Passerelle",
"Your router's address. Used only when DHCP is off.": "L'adresse de ta box. Utilisé seulement si le DHCP est coupé.",
"DNS server": "Serveur DNS",
"Leave blank to use the router's DNS. Example: 1.1.1.1": "Laisse vide pour utiliser le DNS de la box. Exemple : 1.1.1.1",
"Time server": "Serveur d'heure",
"Sets the clock at startup so secure sites can be checked.": "Règle l'horloge au démarrage pour pouvoir vérifier les sites sécurisés.",
"Time zone": "Fuseau horaire", "Offset from UTC. Used for the clock display.": "Décalage par rapport à UTC. Sert à l'affichage de l'heure.",
"Daylight saving": "Heure d'été", "Adds one hour to the displayed time.": "Ajoute une heure à l'heure affichée.",
"Stick dead zone": "Zone morte du stick", "Raise this if the pointer drifts on its own.": "Augmente-la si le pointeur bouge tout seul.",
"Pointer speed": "Vitesse du pointeur", "How fast the pointer moves at full tilt.": "Vitesse du pointeur avec le stick à fond.",
"Acceleration": "Accélération", "Higher values give finer control on small stick movements.": "Plus c'est haut, plus les petits mouvements du stick sont précis.",
"Scroll speed": "Vitesse de défilement", "C-Stick and L/R page scrolling speed.": "Vitesse de défilement avec le stick C et L/R.",
"Pointer snap": "Pointeur aimanté", "Gently pulls the pointer onto nearby buttons.": "Attire doucement le pointeur sur les boutons proches.",
"Rumble": "Vibrations", "Light rumble feedback when pointing at and pressing buttons.": "Petite vibration quand on survole ou appuie sur un bouton.",
"Click": "Cliquer", "Select a binding, then press the button you want.": "Choisis une commande, puis appuie sur le bouton voulu.",
"Back": "Retour", "Address bar": "Barre d'adresse", "Bookmarks": "Favoris", "Settings menu": "Menu des réglages",
"Slow pointer": "Pointeur lent", "Page up": "Page précédente", "Page down": "Page suivante",
"Key layout": "Disposition des touches", "Arrangement of letters on the on-screen keyboard.": "Ordre des lettres sur le clavier à l'écran.",
"Key repeat speed": "Vitesse de répétition", "How fast held Backspace and arrow shortcuts repeat.": "Vitesse de répétition quand on garde Effacer ou les flèches appuyés.",
"Aspect ratio": "Format d'écran", "Choose 16:9 if your TV stretches the picture to widescreen.": "Choisis 16:9 si ta télé étire l'image en écran large.",
"Video mode": "Mode vidéo", "480p needs component cables.": "Le 480p demande un câble composante.",
"Picture width": "Largeur de l'image", "Full uses the whole width of the TV picture. Standard leaves thin borders at the sides.": "Plein utilise toute la largeur de la télé. Standard laisse de fines bandes sur les côtés.",
"Screen margin": "Marge de l'écran", "Raise this if the edges are cut off on your TV.": "Augmente-la si les bords sont coupés sur ta télé.",
"Page text size": "Taille du texte", "Size of text on web pages.": "Taille du texte sur les pages web.",
"Theme": "Thème", "Colour scheme for menus.": "Couleurs des menus.",
"Animations": "Animations", "Menu motion and screen transitions.": "Mouvements des menus et passages d'un écran à l'autre.",
"Loading screen": "Écran de chargement", "An underwater scene covers a page while it loads, so it appears all at once.": "Un décor sous-marin cache la page pendant son chargement, pour qu'elle apparaisse d'un coup.",
"Sound effects": "Bruitages", "Volume": "Volume",
"Open the music player": "Ouvrir le lecteur de musique",
"Plays .mid files from the midi folder on your SD card while you browse.": "Joue les fichiers .mid du dossier midi de la carte SD pendant la navigation.",
"Instruments": "Instruments", "Choose a SoundFont (.sf2) from the midi folder, or the built-in instruments.": "Choisis une SoundFont (.sf2) du dossier midi, ou les instruments intégrés.",
"Music volume": "Volume de la musique", "Shuffle": "Aléatoire", "Plays the songs in a random order.": "Joue les morceaux dans le désordre.",
"Repeat": "Répéter", "Starts the list again after the last song.": "Recommence la liste après le dernier morceau.",
"Glowing note button": "Bouton note lumineux", "The toolbar's note button lights up and pulses while music plays.": "Le bouton note de la barre s'allume et pulse pendant la musique.",
"Game Boy Advance player": "Lecteur Game Boy Advance",
"A Game Boy Advance on controller port 2 (on, no game in it) becomes the music player's screen and remote.": "Une Game Boy Advance sur le port manette 2 (allumée, sans jeu) devient l'écran et la télécommande du lecteur.",
"Home page": "Page d'accueil", "Use about:home for the built-in start page.": "Mets about:home pour la page d'accueil intégrée.",
"Search engine": "Moteur de recherche", "Lightweight engines work best on GameCube.": "Les moteurs légers marchent mieux sur GameCube.",
"Custom search URL": "Adresse de recherche perso", "Your words are added to the end, e.g. https://site/search?q=": "Tes mots sont ajoutés à la fin, par ex. https://site/search?q=",
"Identify as": "Se présenter comme", "Some sites send simpler pages to text browsers.": "Certains sites envoient des pages plus simples aux navigateurs texte.",
"Page styles": "Styles des pages", "Use each site's own colours, fonts and layout. Off shows plain pages.": "Utilise les couleurs, polices et mise en page de chaque site. Non : pages toutes simples.",
"Load images": "Charger les images", "Show pictures on pages. Turn off for faster loading.": "Affiche les images des pages. Coupe-le pour charger plus vite.",
"Picture size": "Taille des images", "Limits how tall pictures can be. Smaller means less scrolling and faster loading.": "Limite la hauteur des images. Plus petit : moins de défilement et chargement plus rapide.",
"Image helper": "Aide pour les images", "Shrinks pictures on images.weserv.nl. Faster, but that site sees image addresses.": "Réduit les images via images.weserv.nl. Plus rapide, mais ce site voit les adresses des images.",
"Picture & style cache": "Cache images et styles",
"Keeps pictures and page styles in gcbrowser/webcache so pages you've seen before load faster. The oldest go when it's full.": "Garde les images et styles sur la carte SD pour que les pages déjà vues chargent plus vite. Les plus vieux partent quand c'est plein.",
"Show image labels": "Légendes des images", "Show descriptions for pictures that can't be shown.": "Affiche la description des images qui ne peuvent pas s'afficher.",
"Max page size": "Taille max d'une page", "Larger pages are cut off to protect memory.": "Les pages plus grosses sont coupées pour protéger la mémoire.",
"Verify certificates": "Vérifier les certificats", "Keep this on. Turning it off removes HTTPS protection.": "Laisse-le activé. Le couper retire la protection HTTPS.",
"Cookies": "Cookies", "Lets sites remember you, which logins need. Off: no site can recognise you.": "Permet aux sites de se souvenir de toi (nécessaire pour se connecter). Non : aucun site ne te reconnaît.",
"Keep logins after restart": "Rester connecté après redémarrage", "Saves cookies to the SD card. Anyone with the card could use your logins.": "Enregistre les cookies sur la carte SD. Quelqu'un qui a la carte pourrait utiliser tes connexions.",
"Certificate file": "Fichier de certificats", "Trusted root certificates (PEM). Applies next launch.": "Certificats racine de confiance (PEM). Appliqué au prochain lancement.",
"Check for CubeBrowser updates": "Chercher une mise à jour de CubeBrowser",
"Looks for a newer CubeBrowser on GitHub and can install it for you.": "Cherche un CubeBrowser plus récent sur GitHub et peut l'installer.",
"Check for CubeBrowser updates at start": "Chercher les mises à jour au démarrage",
"A quick check when the network connects. The start page says when there's a new version.": "Petite vérification quand le réseau se connecte. La page d'accueil prévient s'il y a une nouvelle version.",
"Check for Swiss updates": "Chercher une mise à jour de Swiss",
"Looks for a newer Swiss on GitHub and can replace the Swiss .dol the Exit button starts.": "Cherche un Swiss plus récent sur GitHub et peut remplacer le .dol lancé par le bouton Quitter.",
"Save new Swiss as": "Enregistrer le nouveau Swiss sous",
"Use boot.dol or ipl.dol if your console starts Swiss from that file. The file it replaces is kept as .old.": "Choisis boot.dol ou ipl.dol si ta console démarre Swiss depuis ce fichier. L'ancien est gardé en .old.",
"Exit button": "Bouton Quitter", "Restart console suits setups where Swiss starts automatically at power-on.": "Redémarrer convient si Swiss se lance tout seul à l'allumage.",
"Swiss .dol file": "Fichier .dol de Swiss", "The program the Exit button starts. Use Find .dol files to pick it.": "Le programme lancé par le bouton Quitter.",
"Find .dol files on SD card": "Chercher les .dol sur la carte SD", "Lists the programs on your card so you can choose Swiss.": "Liste les programmes de la carte pour choisir Swiss.",
"Download folder": "Dossier des téléchargements", "Where downloaded files are saved. Swiss can open them from here.": "Là où les fichiers téléchargés sont enregistrés.",
"Delete zip after unzipping": "Supprimer le zip après extraction", "Saves space by removing the .zip once its files are extracted.": "Gagne de la place en supprimant le .zip une fois ses fichiers extraits.",
"Move data folder": "Déplacer le dossier des données", "Where settings, bookmarks and cookies are kept. Press twice to confirm.": "Là où sont gardés réglages, favoris et cookies. Appuie deux fois pour confirmer.",
"Clear cookies and logins": "Effacer cookies et connexions", "Signs you out of every site. Press twice to confirm.": "Te déconnecte de tous les sites. Appuie deux fois pour confirmer.",
"Clear picture & style cache": "Vider le cache images et styles",
"Removes the pictures and page styles saved on the SD card. Pages download them again as needed.": "Supprime les images et styles gardés sur la carte SD. Les pages les retéléchargent au besoin.",
"Clear history": "Effacer l'historique", "Forget Back/Forward pages.": "Oublie les pages Précédent/Suivant.",
"Delete all bookmarks": "Supprimer tous les favoris", "Press twice to confirm.": "Appuie deux fois pour confirmer.",
"Reset all settings": "Remettre tous les réglages à zéro", "Press twice to confirm. Hold L+R+START at boot to do this too.": "Appuie deux fois pour confirmer.",
"Save settings now": "Enregistrer les réglages", "Settings also save when you leave this screen.": "Les réglages s'enregistrent aussi en quittant cet écran.",
# --- ecran des reglages
"Automatic (first .sf2 in the midi folder)": "Automatique (premier .sf2 du dossier midi)", "Built-in instruments": "Instruments intégrés",
"480p needs component cables - using the default mode": "Le 480p demande un câble composante : mode par défaut",
"480p needs component cables": "Le 480p demande un câble composante",
"That isn't a valid address (example: 192.168.1.20)": "Adresse incorrecte (exemple : 192.168.1.20)",
"Press again to confirm": "Appuie encore pour confirmer", "History cleared": "Historique effacé", "Bookmarks deleted": "Favoris supprimés",
"All settings reset": "Réglages remis à zéro", "Signed out of all sites": "Déconnecté de tous les sites", "No SD card found": "Pas de carte SD",
"Cache cleared (%.1f MB freed)": "Cache vidé (%.1f Mo libérés)", "No .dol files found on the SD card": "Aucun .dol sur la carte SD",
"Settings saved": "Réglages enregistrés", "Couldn't save - is the SD card inserted?": "Enregistrement impossible : la carte SD est-elle là ?",
"offline": "hors ligne", "none": "aucune",
"Choose the instruments": "Choisis les instruments", "Choose the Swiss program": "Choisis le programme Swiss",
"These instruments will be used for the next song": "Ces instruments serviront au prochain morceau",
"Loading the new instruments...": "Chargement des nouveaux instruments...", "Exit button will start this program": "Le bouton Quitter lancera ce programme",
"Settings": "Réglages", "Press a button...": "Appuie sur un bouton...",
"Move data folder next to CubeBrowser": "Mettre les données à côté de CubeBrowser", "Move data folder to SD card root": "Mettre les données à la racine de la carte",
"Settings, bookmarks and cookies are in %s. Press twice to confirm.": "Réglages, favoris et cookies sont dans %s. Appuie deux fois pour confirmer.",
"(no SD card)": "(pas de carte SD)", "Couldn't save settings - is the SD card inserted?": "Réglages non enregistrés : la carte SD est-elle là ?",
"Language": "Langue",
"Language of the browser's menus and messages.": "Langue des menus et messages du navigateur.",
# --- navigateur
"Couldn't save bookmarks to the SD card": "Impossible d'enregistrer les favoris sur la carte SD",
"Bookmark list is full": "La liste des favoris est pleine", "Bookmark added": "Favori ajouté",
"<title>Problem loading page</title><h2>This page couldn't be opened</h2><p>%s</p>"
"<p>Address: %s</p><p><a href=\"%s\">Try again</a> &nbsp; <a href=\"about:home\">Start page</a></p>":
"<title>Problème de chargement</title><h2>Impossible d'ouvrir cette page</h2><p>%s</p>"
"<p>Adresse : %s</p><p><a href=\"%s\">Réessayer</a> &nbsp; <a href=\"about:home\">Page d'accueil</a></p>",
"There's no built-in page with that name.": "Aucune page intégrée ne porte ce nom.",
"Only web (http/https) links can be saved": "Seuls les liens web (http/https) peuvent être enregistrés",
"Busy - try again in a moment": "Occupé : réessaie dans un instant", "The network isn't ready yet": "Le réseau n'est pas encore prêt",
"unknown size": "taille inconnue", "%.2f GB": "%.2f Go", "%.1f MB": "%.1f Mo", "%lld KB": "%lld Ko", "%lld bytes": "%lld octets",
"No SD card was found.": "Aucune carte SD trouvée.",
"The network is busy or not connected. Try again in a moment.": "Le réseau est occupé ou pas connecté. Réessaie dans un instant.",
"Couldn't start unzipping. Try again in a moment.": "Impossible de lancer l'extraction. Réessaie dans un instant.",
"Download cancelled": "Téléchargement annulé", "Unzipping cancelled": "Extraction annulée",
"Save to SD card?": "Enregistrer sur la carte SD ?", "Downloading": "Téléchargement", "Download complete": "Téléchargement terminé",
"Download stopped": "Téléchargement arrêté", "Unzipping": "Extraction", "Unzipped": "Extrait", "Couldn't unzip": "Extraction impossible",
"Size: %s": "Taille : %s", "Saves to: %s": "Enregistré dans : %s",
"This is a big file, so it may take a while. Keep the GameCube on until it finishes.": "C'est un gros fichier, ça peut être long. Laisse la GameCube allumée jusqu'à la fin.",
"The music will pause during the download and resume after.": "La musique se met en pause pendant le téléchargement et reprend après.",
"Download": "Télécharger", "Download & unzip": "Télécharger et extraire", "Cancel": "Annuler",
"   %d connections": "   %d connexions",
"About %lld hr %lld min left": "Encore environ %lld h %lld min", "About %lld min left": "Encore environ %lld min", "Less than a minute left": "Moins d'une minute",
"Continuing where it stopped before": "Reprise là où ça s'était arrêté", "File %d of %d: %s": "Fichier %d sur %d : %s",
"Press again to cancel": "Appuie encore pour annuler", "Saved to %s": "Enregistré dans %s",
"It's in the music player's song list.": "Il est dans la liste du lecteur de musique.",
"Choose it under Settings > Music > Instruments.": "Choisis-la dans Réglages > Musique > Instruments.",
"GC Station sees it right away (music, photos, files tabs).": "GC Station le voit tout de suite (onglets musique, photos, fichiers).",
"Play now": "Écouter", "Unzip": "Extraire", "OK": "OK", "Retry": "Réessayer", "Close": "Fermer",
"%d file%s extracted to %s": "%d fichier%s extrait(s) dans %s",
"Low on memory: some pictures were skipped (stopping the music frees some)": "Mémoire presque pleine : des images n'ont pas été chargées (couper la musique en libère)",
"Low on memory: some pictures were skipped": "Mémoire presque pleine : des images n'ont pas été chargées",
"This is a \"%s\" file, which this browser can't show.": "C'est un fichier \"%s\", que ce navigateur ne sait pas afficher.",
"This page was too big and has been cut short": "Cette page était trop grosse, elle a été coupée",
"This page moves on in %d seconds": "Cette page continue dans %d secondes",
"Address or search": "Adresse ou recherche", "Password": "Mot de passe", "Username": "Identifiant", "Text": "Texte",
"No bookmarks yet.": "Pas encore de favori.", "Bookmark this page": "Ajouter cette page aux favoris", "Bookmark deleted": "Favori supprimé",
"Press again (or X) to delete": "Appuie encore (ou X) pour supprimer", "%d-%d of %d": "%d-%d sur %d",
"This menu has no choices": "Ce menu n'a aucun choix", "Choose one": "Choisis",
"Press again to restart the console": "Appuie encore pour redémarrer la console", "Press again to exit to Swiss": "Appuie encore pour revenir à Swiss",
"Swiss .dol not found - choose it in Settings > Data": "Swiss .dol introuvable",
"This site builds its pages with scripts, which CubeBrowser can't run, so some or all of it "
"may be missing. Text mode asks the site for a simpler version, which some sites offer.":
"Ce site fabrique ses pages avec des scripts, que CubeBrowser ne sait pas lancer : il peut manquer une partie ou tout. "
"Le mode texte demande au site une version plus simple, que certains sites proposent.",
"This page needs JavaScript": "Cette page a besoin de JavaScript", "Try text mode": "Essayer le mode texte", "Go back": "Revenir", "Hide": "Masquer",
"Loading page styles...": "Chargement des styles...", "Loading pictures (%d left)": "Chargement des images (encore %d)", "Ready": "Prêt",
"Page de GC Station inconnue.": "Page de GC Station inconnue.",
# --- page d'accueil
"Private web search, light version": "Recherche web privée, version légère",
"GC Station : poser une question a une IA": "GC Station : poser une question à une IA",
"L'encyclopedie libre (pages mobiles)": "L'encyclopédie libre (pages mobiles)",
"Nintendo.com as it looked in 2001": "Nintendo.com comme en 2001",
"GC Station : musique gratuite d'archive.org a telecharger": "GC Station : musique gratuite d'archive.org à télécharger",
"Headlines made for vintage computers": "Les titres de l'actu pour vieux ordinateurs",
"News headlines, text only": "Titres de l'actu (anglais), texte seul", "World news, text only": "Actu du monde (anglais), texte seul",
"L'actualite en francais": "L'actualité en français",
"CubeBrowser (by Arial-Bold) is GC Station's web browser: secure sites, pictures, forms and downloads "
"to the SD card. Point with the Control Stick and press A to open a link. X types an address or "
"searches. Z changes tab, START opens GC Station's menu. Light, text-based sites work best.":
"CubeBrowser (par Arial-Bold) est le navigateur de GC Station : sites sécurisés, images, formulaires et téléchargements "
"sur la carte SD. Vise avec le stick et appuie sur A pour ouvrir un lien. X pour taper une adresse ou "
"chercher. Z change d'onglet, START ouvre le menu de GC Station. Les sites légers, surtout du texte, marchent le mieux.",
"Z changes tab, like everywhere in GC Station.": "Z change d'onglet, comme partout.",
"Downloaded songs show up in the MUSIC tab right away.": "Les morceaux téléchargés arrivent tout de suite dans l'onglet MUSIQUE.",
"The D-Pad jumps between links.": "La croix saute d'un lien à l'autre.",
"Y opens bookmarks. The star saves a page.": "Y ouvre les favoris. L'étoile garde une page.",
"The gear opens the browser settings: themes, text size...": "La roue ouvre les réglages du navigateur : langue, thèmes, taille du texte...",
"AI ASSISTANT: questions from the GameCube (key: phone page /ia).": "ASSISTANT IA : des questions depuis la GameCube (clé : page /ia du téléphone).",
"EXIT TO SWISS FOR v%s": "QUITTER POUR LA v%s", "UPDATE AVAILABLE: v%s": "MISE À JOUR DISPONIBLE : v%s", "WELCOME TO v%s": "BIENVENUE DANS LA v%s",
"FEATURED SITES": "SITES CONSEILLÉS", "CONTROLS & TIPS": "COMMANDES ET ASTUCES", "SITE": "SITE", "TIP": "ASTUCE",
"STICK": "STICK", "Move the pointer": "Bouger le pointeur", "Click a link or button": "Cliquer un lien ou un bouton",
"Hold for a slow pointer": "Garder appuyé : pointeur lent", "Go back a page": "Page précédente",
"Type an address or search": "Taper une adresse ou chercher", "C-STICK": "STICK C", "Scroll the page": "Faire défiler la page",
"Page up / page down": "Page précédente / suivante", "D-PAD": "CROIX", "Jump between links": "Sauter d'un lien à l'autre",
"Save a link to the SD card": "Enregistrer un lien sur la carte SD",
"Delete": "Effacer", "Space": "Espace", "Shift": "Maj", "Move the text cursor": "Déplacer le curseur", "Accept the text": "Valider le texte",
"Downloads: links to files such as .dol, .iso or .zip offer to save them to the SD card. Zip files "
"can be unzipped right away, and an interrupted download continues where it stopped.":
"Téléchargements : les liens vers des fichiers (.dol, .iso, .zip...) proposent de les enregistrer sur la carte SD. Les zip "
"peuvent être extraits tout de suite, et un téléchargement coupé reprend là où il s'était arrêté.",
"What works: text, links, pictures, tables, lists and forms such as search boxes. JavaScript isn't "
"supported, so sites are shown in a simpler layout. Light, text-based sites work best.":
"Ce qui marche : texte, liens, images, tableaux, listes et formulaires comme les cases de recherche. JavaScript n'est pas "
"pris en charge, donc les sites sont affichés plus simplement. Les sites légers marchent le mieux.",
"Music: put .mid files in a folder called midi on your SD card and press the note button in the "
"toolbar. The music keeps playing while you browse. Add a SoundFont (.sf2) there for real instruments.":
"Les fichiers .mid téléchargés vont dans le dossier retro de la carte SD : GC Station les joue dans l'onglet RETRO.",
"Every button except the D-Pad can be changed in Settings > Controls.": "Les boutons (sauf la croix) se changent dans Réglages > Commandes.",
"CONTROLLER": "MANETTE", "ON-SCREEN KEYBOARD": "CLAVIER À L'ÉCRAN", "TIPS": "ASTUCES", "Controls & Tips": "Commandes et astuces",
"PRESS %s TO GO BACK": "APPUIE SUR %s POUR REVENIR", "BACK TO START PAGE": "RETOUR À L'ACCUEIL",
"YOU HAVE %s": "TU AS %s", "UNKNOWN": "INCONNU",
"Checking GitHub for a newer version...": "Recherche d'une nouvelle version sur GitHub...",
"Releases come from github.com/%s": "Les versions viennent de github.com/%s",
"You have the latest version.": "Tu as la dernière version.", "%s %s is up to date.": "%s %s est à jour.",
"Version %s is available!": "La version %s est disponible !", "Couldn't check for updates.": "Impossible de chercher les mises à jour.",
"Installing version %s...": "Installation de la version %s...", "Downloading: %lld of %lld KB": "Téléchargement : %lld sur %lld Ko",
"Connecting to GitHub...": "Connexion à GitHub...", "Unpacking and checking the new version...": "Décompression et vérification...",
"Putting the new version in place...": "Mise en place de la nouvelle version...", "Version %s is installed.": "La version %s est installée.",
"The update couldn't be installed.": "La mise à jour n'a pas pu être installée.",
"TRY AGAIN": "RÉESSAYER", "DOWNLOAD AND INSTALL": "TÉLÉCHARGER ET INSTALLER", "CHECK AGAIN": "VÉRIFIER À NOUVEAU", "STOP": "ARRÊTER",
"WHAT'S NEW": "NOUVEAUTÉS",
# --- clavier
"B: Delete  Y: Space  X: Shift  L/R: Move  START: OK": "B : effacer  Y : espace  X : maj  START : OK",
"CAPS": "MAJ",
# --- telechargements, zip, connexion
"The connection kept failing.": "La connexion n'arrête pas d'échouer.",
"Couldn't create the file on the SD card.": "Impossible de créer le fichier sur la carte SD.",
"Nothing was received for a minute.": "Rien reçu depuis une minute.", "Stopped.": "Arrêté.",
"Couldn't write to the SD card. It may be full.": "Impossible d'écrire sur la carte SD. Elle est peut-être pleine.",
"%s Choose Retry to continue where it stopped.": "%s Choisis Réessayer pour reprendre là où ça s'est arrêté.",
"The download ended early.": "Le téléchargement s'est arrêté trop tôt.",
"This file is larger than 4 GB, the limit for FAT32 SD cards.": "Ce fichier dépasse 4 Go, la limite des cartes SD en FAT32.",
"Not enough space on the SD card (needs %lld MB more).": "Pas assez de place sur la carte SD (il manque %lld Mo).",
"Couldn't finish saving the file.": "Impossible de finir d'enregistrer le fichier.", "Out of memory.": "Plus de mémoire.",
"The connection was lost. Choose Retry to continue where it stopped.": "La connexion a été perdue. Choisis Réessayer pour reprendre là où ça s'est arrêté.",
"The download ended early. Choose Retry to continue where it stopped.": "Le téléchargement s'est arrêté trop tôt. Choisis Réessayer pour reprendre.",
"Out of memory": "Plus de mémoire", "Couldn't read the zip file": "Impossible de lire le zip",
"This isn't a zip file, or it's incomplete": "Ce n'est pas un zip, ou il est incomplet", "The zip file is damaged": "Le zip est abîmé",
"Couldn't open the zip file": "Impossible d'ouvrir le zip", "This isn't a zip file": "Ce n'est pas un zip", "This zip has too many files": "Ce zip a trop de fichiers",
"%s is password-protected": "%s est protégé par un mot de passe", "%s uses a compression type that isn't supported": "%s utilise une compression non prise en charge",
"%s is too large for this SD card format": "%s est trop gros pour le format de cette carte SD",
"Couldn't create %s (is the SD card full?)": "Impossible de créer %s (carte SD pleine ?)",
"Couldn't write %s (is the SD card full?)": "Impossible d'écrire %s (carte SD pleine ?)", "%s is damaged inside the zip": "%s est abîmé dans le zip",
"The site took too long to respond.": "Le site a mis trop de temps à répondre.", "Secure connection failed: %s": "La connexion sécurisée a échoué : %s",
"Looking up site...": "Recherche du site...", "Couldn't find \"%s\". Check the address or DNS setting.": "\"%s\" introuvable. Vérifie l'adresse.",
"Connecting...": "Connexion...", "Couldn't connect to %s.": "Impossible de se connecter à %s.",
"No trusted certificates loaded from %s, so secure sites can't be checked.": "Aucun certificat chargé depuis %s : les sites sécurisés ne peuvent pas être vérifiés.",
"Securing connection...": "Sécurisation de la connexion...",
" The console clock wasn't set from the network, which can cause this.": " L'horloge de la console n'a pas été réglée par le réseau, ça peut venir de là.",
"This site's certificate couldn't be verified: %s.%s": "Le certificat de ce site n'a pas pu être vérifié : %s.%s",
"Setting the clock...": "Réglage de l'horloge...", "Loading certificates...": "Chargement des certificats...", "Starting...": "Démarrage...",
"server error %d": "erreur du serveur %d", "not enough memory": "pas assez de mémoire",
"Loading...": "Chargement...", "Loading": "Chargement",
"Submit": "Envoyer", "[image]": "[image]", "Open part %d: %s": "Ouvrir la partie %d : %s",
"This page is split into frames, which are shown one at a time here.": "Cette page est découpée en cadres, affichés un par un ici.",
# --- pages integrees
"Unknown error.": "Erreur inconnue.",
"<title>Start</title><h1>CubeBrowser</h1><p>Press <b>X</b> to type an address or search.</p>":
"<title>Accueil</title><h1>CubeBrowser</h1><p>Appuie sur <b>X</b> pour taper une adresse ou chercher.</p>",
"<title>Controls</title><h1>Controls</h1><ul>"
"<li><b>Control Stick</b> - move the pointer (hold Z for fine control)</li>"
"<li><b>A</b> - click</li><li><b>B</b> - back</li>"
"<li><b>X</b> - type an address or search</li><li><b>Y</b> - bookmarks</li>"
"<li><b>START</b> - settings</li><li><b>C-Stick</b> - scroll</li>"
"<li><b>L / R</b> - page up / page down</li>"
"<li><b>D-Pad</b> - jump between links and buttons</li>"
"<li><b>Hold Z + A</b> on a link - save it to the SD card instead of opening it</li></ul>"
"<p>All buttons except the D-Pad can be changed in Settings &gt; Controller.</p>"
"<h3>On-screen keyboard</h3><ul><li><b>B</b> delete, <b>Y</b> space, <b>X</b> shift</li>"
"<li><b>L / R</b> move the text cursor, <b>START</b> accept</li></ul>"
"<h3>Downloads</h3><p>Links to files (such as .dol, .iso or .zip) ask to save them to the SD card. "
"Zip files can be unzipped automatically. Interrupted downloads continue where they stopped.</p>"
"<h3>What works</h3><p>Text, links, pictures, tables, lists, forms such as search boxes, and most "
"page colours, fonts and spacing. JavaScript and modern multi-column layouts aren't supported, so "
"sites are shown in a simpler, phone-style layout. Lightweight sites work best.</p>"
"<p><a href=\"about:home\">Back to the start page</a></p>":
"<title>Commandes</title><h1>Commandes</h1><ul>"
"<li><b>Stick</b> : bouger le pointeur</li>"
"<li><b>A</b> : cliquer</li><li><b>B</b> : retour</li>"
"<li><b>X</b> : taper une adresse ou chercher</li><li><b>Y</b> : favoris</li>"
"<li><b>Stick C</b> : faire défiler</li>"
"<li><b>L / R</b> : page précédente / suivante</li>"
"<li><b>Croix</b> : sauter d'un lien ou d'un bouton à l'autre</li>"
"<li><b>Z</b> : changer d'onglet, <b>START</b> : menu de GC Station</li></ul>"
"<p>Les boutons (sauf la croix) se changent dans Réglages &gt; Commandes.</p>"
"<h3>Clavier à l'écran</h3><ul><li><b>B</b> effacer, <b>Y</b> espace, <b>X</b> majuscules</li>"
"<li><b>L / R</b> déplacer le curseur, <b>START</b> valider</li></ul>"
"<h3>Téléchargements</h3><p>Les liens vers des fichiers (.dol, .iso, .zip...) proposent de les enregistrer sur la carte SD. "
"Les zip peuvent être extraits tout de suite. Un téléchargement coupé reprend là où il s'était arrêté.</p>"
"<h3>Ce qui marche</h3><p>Texte, liens, images, tableaux, listes, formulaires comme les cases de recherche, et la plupart "
"des couleurs, polices et espacements. JavaScript et les mises en page modernes en colonnes ne sont pas pris en charge : "
"les sites sont affichés plus simplement, comme sur un téléphone. Les sites légers marchent le mieux.</p>"
"<p><a href=\"about:home\">Retour à l'accueil</a></p>",
# --- v214 : GC Station, style Aurore
"Browser settings": "Réglages du navigateur", "FEATURED SITE": "SITE CONSEILLÉ", "BROWSER": "NAVIGATEUR",
"RECENT BOOKMARKS": "DERNIERS FAVORIS", "No bookmarks yet: press Y on a page to keep it.": "Pas encore de favori : Y sur une page pour la garder.",
"Type an address (www...) or words to search for.": "Tape une adresse (www...) ou des mots à chercher.",
"The pages you keep. Y opens them from anywhere.": "Tes pages gardées. Y les ouvre de partout.",
"Language, text size, pictures, cookies...": "Langue, taille du texte, images, cookies...",
"Every button of the controller and the keyboard.": "Toutes les commandes de la manette et du clavier.",
"Home": "Accueil", "Back to the start page": "Retour à l'accueil",
}

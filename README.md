# GC-MP3 — Indigo Audio Player

Lecteur audio pour GameCube : **ta musique** (carte SD), des **webradios**, un **navigateur web**,
la **musique de jeux rétro**, des **podcasts**, tes **photos et vidéos**, un **explorateur de fichiers**,
la **météo / actualités / réveil**, et une **télécommande sur téléphone** (Broadband Adapter),
dans une interface GX style Indigo. Lancer `gc-mp3player.dol` depuis Swiss / Indigo.

## Où sont les fichiers sur la carte SD
- Tes fichiers : `SD:/mp3` (musique), `SD:/retro`, `SD:/photos`, `SD:/videos`, `SD:/ecran`, `SD:/downloads`.
- Tout ce que GC-MP3 crée est rangé dans **`SD:/gc-mp3`** : réglages (`gc-mp3.txt`), radios
  (`radio.txt`, logos et enregistrements dans `radio/`), podcasts (`podcasts.txt`, épisodes dans
  `podcasts/`), favoris et cookies du web, listes de lecture (`listes/`), écoutes, records des jeux,
  titres radio gardés, cache des pochettes et des vignettes, journal de plantage.
- Si ces fichiers étaient encore à la racine (version 21 et avant), GC-MP3 les y range tout seul au
  démarrage. Les autres fichiers de la racine (Swiss, `ipl.dol`, `config.ini`…) ne sont pas touchés.

## Onglets
**Z** passe à l'onglet suivant : MUSIQUE, RADIO, WEB, RÉTRO, PODCASTS, PHOTOS, FICHIERS, INFOS,
TÉLÉPHONE, JEUX, TÉLÉCHARGEMENTS. Le nom de l'onglet et des petits points (position) s'affichent en
haut de la liste. Pour cacher les onglets dont tu ne te sers pas : START > « Onglets affichés ».

## Nouveautés v41

- **Onglet JEUX / APPS** (avant : JEUX) : à côté de « Mes jeux GameCube », une barre **« Mes applis »** ouvre la liste des homebrews (.dol). L'accès aux applis n'est plus dans START > Outils.
- **Icônes des applis** : le fichier `icon.png` à côté du .dol (dossier `apps/NomDeLAppli/`, format Homebrew Channel 128 x 48) s'affiche dans la liste. Sans icône : les initiales.
- Le nom d'une appli est celui de son dossier (ex. « VBA-GX » au lieu de « default »), ou celui de `meta.xml` s'il existe.

## Nouveautés v40

- **Couleurs du spectrogramme** (START > Réglages > GC-MP3) : Feu (par défaut, moins violet), Violet (l'ancien), Arc-en-ciel, Océan, Vert, Noir et blanc. Pour le spectrogramme plein écran (économiseur, mode soirée) et le style « Spectrogramme » du lecteur. Changer de couleurs recolore aussi ce qui est déjà affiché.

## Nouveautés v39

- **Spectre amélioré** : mouvement plus fluide (lissé à chaque image), crêtes qui retombent doucement, et **7 styles** : LED (classique), Barres, Miroir, Courbe, Spectrogramme (défilant, couleurs de chaleur), Oscilloscope (forme d'onde), Cercle. START > Réglages > GC-MP3 > « Style du spectre ».
- **A + Z + START en jeu ramène sur GC-MP3** : START > Réglages > GC-MP3 > « Retour en jeu (A+Z+START) » = GC-MP3. GC-MP3 télécharge son propre `apploader.img` (fabriqué à chaque compilation, même format que celui d'Indigo), le met dans `swiss/patches/` et règle Indigo sur « Apploader ». Celui d'Indigo est gardé (`apploader-indigo.img`) : remettre « Indigo » le rétablit. Le fichier est remis à jour tout seul après chaque mise à jour de GC-MP3.
- Merci à Swiss (Extrems, emu_kidid) : le petit programme de démarrage de `apploader.img` (`tools/igr_stub.bin`, GPL v2) vient du packer de Swiss.

## Nouveautés v38

- **Après une mise à jour : A relance GC-MP3 directement** (la nouvelle version est chargée en mémoire). Avant, il fallait quitter, ce qui redémarrait la console sur le menu de la GameCube / le lecteur de disque.
- **Bouton Reset de la console** pendant GC-MP3 : relance GC-MP3 au lieu d'ouvrir le menu de la GameCube.
- « Quitter GC-MP3 » ouvre toujours le menu de la GameCube (lecteur de disque).

## Nouveautés v37

- **Cheats beaucoup plus rapides** : cocher / décocher est instantané. Les choix sont écrits sur la carte SD une seconde après (ou en quittant le fichier), plus à chaque appui. La lecture des codes n'utilise plus `sscanf` (20 fois plus rapide, même résultat que Swiss). La liste des fichiers de cheats s'ouvre sans lire chaque fichier.
- **Page Réglages plus stable** : changer Vibrations ou Vidéo dans la section GC-MP3 n'écrit plus le fichier d'Indigo à chaque appui (écrit en fermant la page), et ne perd plus les changements faits dans les sections Indigo / Swiss.

## Nouveautés v36

- **Mise à jour bloquée à 99 %** corrigée : la recherche des autres copies de GC-MP3 regardait chaque fichier de la carte (très long avec des milliers de ROM). Elle ne regarde plus que la racine et le dossier `apps`, par leur nom.

## Nouveautés v35

- **Nouvelle page Réglages (START)** avec des sections en haut, **L / R** pour changer :
  - **GC-MP3** : les réglages de l'appli.
  - **Indigo** : système de la console et interface d'Indigo.
  - **Swiss** : lancement des jeux, réglages par défaut des jeux, réseau.
  - **Outils** : les outils ci-dessous.
  Les réglages d'Indigo / Swiss changés sont enregistrés en fermant la page (ou Y tout de suite).
- **Cartes mémoire** : sauvegardes des slots A et B (icône, nom, blocs), copie sur la carte SD en `.gci` (dossier `SD:/gci`), remise depuis un `.gci` / `.gcs` / `.sav`, copie vers l'autre carte, suppression.
- **Applis (.dol)** : tous les `.dol` de la racine et du dossier `apps` (nom et description de `meta.xml`), lancés directement depuis GC-MP3.
- **Derniers jeux lancés** : depuis GC-MP3 et depuis Indigo (`recent.ini`), relancés en un clic.
- **Réglages par jeu** : dans Mes jeux, fiche d'un jeu > **Y** (fichier `swiss/settings/game/<ID>.ini` d'Indigo). Y dans la fenêtre = tout remettre par défaut.
- **Infos console** : version, vidéo, langue, son, mémoire, carte SD, réseau, horloge.

## Nouveautés v34

- **Réglages d'Indigo appliqués à GC-MP3** (point vert dans START > Réglages Indigo / Swiss) :
  - Son de la console Mono / Stereo : GC-MP3 joue en mono si Mono.
  - Position de l'écran : décale aussi l'image de GC-MP3.
  - Langue de la console : GC-MP3 la suit (si sa langue est sur Auto).
  - Couper les vibrations ↔ Vibrations de la manette de GC-MP3 (synchronisés dans les deux sens).
  - Mode vidéo des applis (Auto / 480i / 480p) ↔ Vidéo de GC-MP3 (synchronisés, au prochain lancement).
  - Fichiers cachés / Cacher les fichiers inconnus : onglet FICHIERS.
  - Réseau : DHCP = No + adresse IP / masque / passerelle → GC-MP3 utilise cette adresse fixe.
- Son, position et langue sont écrits dans la mémoire de la console, comme le fait Indigo à son démarrage.

## Nouveautés v33

- **Mise à jour de toutes les copies** : la mise à jour remplace aussi les autres copies de GC-MP3 sur la carte (ex. `ipl.dol` à la racine ET `gc-mp3player.dol` dans `apps`). GC-MP3 les reconnaît à leur contenu, même renommées.

## Nouveautés v32

- **Réglages Indigo / Swiss dans GC-MP3** : START > « Réglages Indigo / Swiss ». Une fenêtre par-dessus l'écran en cours, avec tous les réglages d'Indigo / Swiss rangés en 5 pages (L / R) : Système, Interface, Jeux, Jeux (défaut : vidéo, manette, émulation), Réseau.
- Gauche / Droite ou A change la valeur, A ouvre le clavier pour les adresses et les noms, X montre un mot de passe, Y enregistre, B ferme.
- GC-MP3 modifie seulement les lignes changées de `SD:/swiss/settings/global.ini` (le reste, comme `Autoload=`, ne bouge pas). « (par défaut) » = pas écrit dans le fichier, Indigo garde sa valeur par défaut.

## Nouveautés v31

- **Activer / désactiver les cheats** (éditeur de cheats Swiss) : dans un fichier, **A** coche ou décoche un cheat (case verte = activé). GC-MP3 écrit `SD:/swiss/cheats/<ID>.chtsel`, le même fichier que Swiss / Indigo : tes choix se retrouvent aussi dans leur menu Cheats.
- Le fichier de cheats doit s'appeler comme l'ID du jeu (ex. `GALE01.txt`). Les cheats avec des « XX » ne sont pas pris en charge par Swiss (case barrée).
- X > « Voir / modifier les codes » pour éditer les lignes d'un cheat. Les choix restent cochés après une modification enregistrée.
- Un jeu lancé depuis l'onglet JEUX démarre avec ses cheats cochés (GC-MP3 envoie `AutoCheats=Yes` à Indigo). Pour un jeu lancé directement depuis Indigo, active « Auto-load cheats » dans ses réglages.

## Nouveautés v30

- **Démarrer GC-MP3 avec Indigo / Swiss** : START > Réglages > « Démarrer avec Indigo / Swiss » = Oui. GC-MP3 écrit `Autoload=` dans `SD:/swiss/settings/global.ini` : à chaque démarrage, Indigo ouvre GC-MP3 tout seul. Non = Indigo redevient normal.
- **Quitter vers Swiss** (START > Réglages) : retour au menu d'Indigo / Swiss, même quand GC-MP3 démarre tout seul.
- **Z tenu au démarrage de GC-MP3** : passe directement au menu d'Indigo / Swiss (secours).

## Nouveautés v29 (retours de testeurs)

- **Plus de coupures** : dans un dossier, pendant la lecture, les noms (« Artiste - Titre ») sont lus un par un et seulement quand il reste au moins 2 s de son d'avance.
- **Analyseur de spectre** plus fin : vraie FFT sur 2048 points (avant : 512), échelle « mel » de 40 Hz à 16 kHz (les basses prennent peu de place, les médiums et aigus sont étalés), niveau automatique (plus de spectrogramme tout blanc sur une musique forte).
- **Grosse caisse** détectée pour de vrai (montée des basses 40-140 Hz) : le mode soirée, les vibrations, le jeu de rythme et l'aquarium suivent les coups de batterie au lieu de se déclencher au hasard.

## Nouveautés v28

- **Deux interfaces au choix** : START > Réglages > « Style de l'interface » : **Indigo** (d'origine) ou **Moderne** (gris sombre, cartes plates, tous les onglets en haut, une couleur d'accent). En Moderne, « Thème » choisit la couleur d'accent (cyan, bleu, vert, rouge, orange, rose, blanc).
- **Mes jeux GameCube : 6 affichages** (R / L pour changer, ou START > Réglages > « Affichage des jeux GameCube ») : Grille, Carrousel, Liste, Mur de jaquettes, Vitrine (capture + résumé + bande de jaquettes), Étagère.

## Nouveautés v27

- Fin de téléchargement (web, podcasts, téléphone) : plus de blocage de quelques secondes à 99 % (le morceau est ajouté à la liste sans relire toute la carte SD ; il apparaît en bas de « Tous les morceaux » jusqu'au prochain démarrage).
- Mise à jour : le nouveau .dol est écrit petit à petit (« Installation NN % »), l'écran ne se fige plus.
- Mes jeux GameCube : la liste est gardée (`SD:/gc-mp3/cache/jeux/liste.txt`) et s'ouvre tout de suite ; Y pour chercher à nouveau.
- Lancement d'un jeu : Swiss trouvé une seule fois puis retenu, chargement affiché en %.

## Nouveautés v26

- **Lancer un jeu** : onglet JEUX > Mes jeux GameCube > A (fiche) > A : GC-MP3 se ferme et Swiss démarre directement le jeu (argument `Autoload`). GC-MP3 trouve Swiss tout seul (un .dol avec « swiss » ou « indigo » dans le nom, ou `ipl.dol` / `boot.dol`, à la racine ou dans `swiss/`) ; pour en forcer un : `swiss = sd:/chemin/swiss.dol` dans `gc-mp3.txt`.
- **Rétro** : libxmp complète, plus de 50 formats de modules (MOD et variantes, XM, IT, S3M, MED/OctaMED, MTM, 669, OKT, DBM, DIGI, DSYM, FAR, ULT, PTM, STM, MDL, IMF, GDM, AMF, LIQ, RTM, SYM, UMX...). Noms à l'Amiga acceptés (`mod.titre`, `med.titre`).
- **Rétro** : les **.zip** posés dans `SD:/retro` sont décompressés tout seuls dans un dossier du même nom (une seule fois).
- **Fichiers** : les noms avec accents, tirets longs, etc. s'affichent correctement (lecture des noms en UTF-8).

## Mes jeux GameCube (v25)

- Onglet JEUX > « Mes jeux GameCube » (la barre du haut) : tes jeux de la carte SD (.iso, .gcm, .ciso, .nkit.iso, dans n'importe quel dossier, 3 niveaux).
- Chaque jeu montre sa **jaquette** si tu as le pack de Swiss Indigo : `SD:/swiss/ui/posters.pak` (et `stills.pak` pour une capture, `descriptions.txt` pour le résumé). Sans jaquette : la bannière du disque et l'ID du jeu.
- A = fiche du jeu (grande jaquette, région, taille, résumé, capture), X = ses cheats (crée `SD:/swiss/cheats/<ID>.txt` si besoin), Y = actualiser la liste.
- Les jeux se lancent toujours depuis Swiss. Les bannières lues sont gardées dans `SD:/gc-mp3/cache/jeux` (plus rapide la fois suivante).

## Éditeur de cheats Swiss (v25)

- Ouvre-le avec START > « Cheats Swiss (éditeur) », ou onglet FICHIERS > X > « Cheats Swiss », ou en ouvrant un .txt de `SD:/swiss/cheats`.
- Swiss lit un fichier par jeu : `SD:/swiss/cheats/<ID du jeu>.txt` (ex. `GALE01.txt`). Dedans : le nom du cheat, ses lignes de code `XXXXXXXX YYYYYYYY`, puis une ligne vide avant le cheat suivant.
- « + Nouveau fichier » : tape l'ID du jeu (6 caractères, affiché par Swiss).
- Dans un fichier : A ouvre un cheat, X = renommer / dupliquer / supprimer / monter / descendre, « + Ajouter un cheat », Y = enregistrer.
- Dans un cheat : A sur une ligne = modifier chiffre par chiffre (gauche / droite : chiffre, haut / bas : valeur), X = ajouter / taper au clavier / supprimer / dupliquer une ligne.
- Les lignes qui ne sont pas au bon format s'affichent en rouge. Le reste du fichier (en-tête, lignes inconnues) est gardé tel quel.

## Langue / Language (v24)

- START > Réglages > « Langue / Language » : **Auto** (langue de la console), **Français** ou **English**.
- Tout passe en anglais : onglets, menus, messages, jeux, page web d'accueil et page du téléphone.
- Les fichiers de réglages sur la carte SD restent les mêmes (on peut changer de langue sans rien perdre).
- In English: START > Settings > "Langue / Language" > English.

## Bibliothèque plus grande et réseau plus solide (v24)

- Jusqu'à **2000 morceaux** dans l'onglet MUSIQUE (avant : 512).
- Connexion : un 2e essai automatique si le premier échoue (paquet perdu, adresse périmée), et la connexion sécurisée (https) réessaie une fois avant d'afficher une erreur.

## Curseur dans tous les onglets
Comme dans l'onglet WEB, le **stick principal** déplace une flèche partout : listes, boutons du
lecteur (précédent, lecture / pause, suivant, stop), menu START, fenêtres, grille des photos, cartes
des jeux, et le bandeau des onglets (après Z, ou en cliquant sur le nom de l'onglet en haut de la
liste). La zone survolée est entourée en jaune ; **A** la choisit. La flèche disparaît après 6 s.

## Onglet JEUX
Tous les mini-jeux en un coup d'œil, avec leur record : Karaoké, Jeu de rythme (solo ou duel), Quiz
musical, Snake, **Snake sans murs**, Casse-briques, **Pong** (contre la GameCube, ou à deux avec la
manette 2), **Note volante** (A pour voler entre des murs qui arrivent en rythme) et **Simon**
(retiens la suite de boutons, de plus en plus longue et rapide). Croix ou curseur pour choisir, A
pour jouer. Dans le casse-briques, une brique dorée donne maintenant une **grande raquette** 8 s.

## Onglet TÉLÉCHARGEMENTS
Tout ce qui arrive sur la carte SD : téléchargements de l'onglet WEB, épisodes de podcasts (à la
main ou automatiques) et fichiers envoyés depuis le téléphone. En haut, ce qui est en cours (avec
la vitesse) et les épisodes en attente ; en dessous, l'historique (plus récent en haut, avec
l'origine, la taille et l'heure). A = écouter (musique) ou voir (photo), X deux fois = effacer le
fichier, B = annuler le téléchargement en cours, Y = aller dans l'onglet WEB. Historique :
`SD:/gc-mp3/telechargements.txt`.

## Disposer le lecteur comme tu veux
- **Stick C vers le bas** (ou START > « Disposition du lecteur ») change la disposition de MUSIQUE et RADIO :
  - *Classique* : liste à gauche, lecteur à droite (comme avant) ;
  - *Pochette géante* : grande pochette, titre, barre de progression, boutons, « À suivre » ;
  - *Liste étendue* : la liste sur toute la largeur, un lecteur compact en bas ;
  - *Minimaliste* : seulement le titre, la progression et l'heure, en grand.
  Dans les dispositions sans liste, **A** = pause, L / R = morceau précédent / suivant.
- START > « Éléments du lecteur » : affiche ou cache la **pochette**, les **paroles / égaliseur**,
  les **boutons**, les **infos** (album, année, format) et l'**horloge**.

## Onglet MUSIQUE
- Formats : **MP3, FLAC, OGG Vorbis, WAV et M4A** (`.mp3`, `.flac`, `.ogg`, `.wav`, `.m4a`, `.m4b` en AAC-LC
  ou HE-AAC, avec titre, artiste et pochette intégrée), lus dans `SD:/mp3`
  et `SD:/downloads`, sous-dossiers compris (3 niveaux, 2000 fichiers maximum). Les FLAC haute
  résolution (24 bits, 96 kHz) passent ; les fichiers Opus et ALAC ne sont pas gérés. Ils sont lus au fur et à mesure depuis la carte : **pas de limite de taille**
  (les longs mix passent).
- **Stick C gauche / droite** : change la liste — *Tous les morceaux*, *Dossiers*, *Artistes*, *Albums*,
  *Mes listes*.
- **X : menu d'actions** sur le morceau choisi : *Ajouter à une liste…*, *Nouvelle liste…*, *Chercher un
  morceau…*, *Actualiser la bibliothèque*. Dans une liste : *Retirer*, *Monter*, *Descendre* ; sur une liste :
  *Supprimer*.
- **Mes listes** : tes listes de lecture (`SD:/gc-mp3/listes/<Nom>.m3u`, aussi lisibles sur PC), plus deux listes
  automatiques : **Les plus écoutés** et **Écoutés récemment** (un morceau compte après 30 s d'écoute ;
  `SD:/gc-mp3/gc-mp3-ecoutes.txt`).
- **Recherche** : X > *Chercher un morceau…* (ou tape une lettre sur le clavier GameCube) ; titre, artiste ou
  album, sans se soucier des accents ni des majuscules. B efface la recherche.
  Dans un dossier / artiste / album, la première ligne sert à revenir en arrière (A).
  Les vues Artistes et Albums lisent d'abord les étiquettes des MP3 (quelques secondes).
- La lecture enchaîne les morceaux de la liste ouverte. **Aléatoire** et **répétition**
  (tout / un morceau) se règlent dans le menu (START) ; « ALÉA » et « RÉP. » s'affichent en bas de la liste.
- **X** actualise la liste sans couper le morceau en cours.
- **Reprise** : au démarrage, GC-MP3 reprend le dernier morceau là où tu l'avais laissé
  (position approchée ; désactivable dans le menu).
- **Paroles** : un fichier `Titre.lrc` à côté de `Titre.mp3` (format LRC synchronisé,
  `[01:23.45]paroles`) s'affiche ligne par ligne à la place de l'égaliseur. S'il n'y en a pas,
  avec le Broadband Adapter branché, les paroles synchronisées sont cherchées sur **LRCLIB**
  (lrclib.net, gratuit) et enregistrées en `.lrc` à côté du morceau (START > Paroles automatiques).
- **Fondu enchaîné** (START) : 2, 4, 6 ou 8 secondes de mélange entre deux morceaux qui
  s'enchaînent tout seuls (pas quand on change avec L / R).
- **Volume égalisé** (START) : utilise les étiquettes **ReplayGain** des fichiers (par morceau ou
  par album) pour qu'aucun morceau ne soit beaucoup plus fort qu'un autre. Les fichiers sans ces
  étiquettes restent tels quels (des logiciels comme foobar2000 ou MusicBrainz Picard les ajoutent).
- **Égaliseur** (START) : graves et aigus, de -12 à +12 dB (aussi pour la radio).
- **Noms propres** : la liste affiche « Artiste - Titre » lu dans les étiquettes du fichier, sinon le
  nom du fichier sans numéro de piste (« 01 - », « 1-03 », « 07_ »…), sans extension ni « _ ».
  Dans une vue Artiste ou Album, seulement le titre.
- Pochettes : image du même nom que le MP3, pochette intégrée, ou `cover.jpg` / `folder.jpg` du dossier.
  Sinon, avec le Broadband Adapter branché, elle est cherchée sur MusicBrainz / Cover Art Archive
  et enregistrée à côté du MP3. L'album et l'année s'affichent sous l'artiste.

## Mode soirée
**Stick C vers le haut** (onglets MUSIQUE et RADIO), ou START > Mode soirée : visualiseur plein écran.
Les basses donnent le rythme (pulsations, éclairs, anneaux, particules), les couleurs tournent avec
la musique, et les **paroles s'affichent en grand** (sinon le titre et l'artiste).
Quatre styles : *Pulsation*, *Barres*, *Tunnel*, *Spectrogramme* (stick C gauche / droite pour changer).
L / R et Y marchent comme d'habitude ; B ou START pour sortir.
**La manette vibre** sur les gros coups de basse (START > Vibrations de la manette pour l'arrêter).
En écoutant une radio, X garde le titre qui passe (voir l'onglet RADIO, *Titres de la radio*).

## Jeux et karaoké
Dans MUSIQUE, **X sur un morceau** (ou START > Jeux et karaoké) :
- **Karaoké** : les paroles en très grand sur toute la télé. La ligne chantée se colore de gauche à
  droite, la précédente et les suivantes sont affichées autour. L / R = morceau, Y = pause, B = sortir.
  Les paroles viennent d'un fichier `.lrc` à côté du morceau ou de LRCLIB (paroles automatiques).
- **Jeu de rythme** : comme Guitar Hero, sur tes propres morceaux. GC-MP3 écoute les basses, trouve
  le tempo, et fait tomber des notes A / B / X / Y sur les temps. Appuie quand la note touche la ligne :
  *Parfait*, *Bien*, *OK*… Le combo multiplie les points (jusqu'à x4). Trois difficultés (Facile, Normal,
  Difficile). Z = pause, START = arrêter. Le record de chaque morceau est gardé dans
  `SD:/gc-mp3/gc-mp3-jeux.txt`. Les morceaux au rythme bien marqué marchent le mieux.
  **Duel** (haut / bas sur l'écran de départ) : deux joueurs côte à côte, manettes 1 et 2, mêmes
  notes pour les deux ; à la fin, le gagnant s'affiche (A = revanche).
- **Quiz musical** : 10 secondes d'un de tes morceaux (pris au milieu), 4 réponses : trouve lequel !
  De 1 à 4 joueurs, chacun avec sa manette (A, B, X ou Y). Plus on répond vite, plus on gagne de points ;
  le premier qui trouve a un bonus. 10 manches, puis le classement. Il faut au moins 4 morceaux.
- **Snake musical** : mange les notes ; le serpent file plus vite quand la musique s'emballe (refrain,
  montée, drop) et des **notes dorées** (50 points) apparaissent sur les coups de basse. Croix ou stick
  pour diriger, Y = pause, START = sortir.
- **Casse-briques musical** : chaque colonne de briques s'allume avec une bande du spectre, la balle
  accélère avec la musique (« TURBO ! »), et des briques deviennent dorées sur le rythme (50 points).
  Stick ou croix = raquette, A = lancer la balle, 3 vies, un niveau plus rapide à chaque mur cassé.
  Si rien ne joue, un morceau au hasard est lancé. Les records sont gardés (`SD:/gc-mp3/gc-mp3-jeux.txt`).

## Onglet RÉTRO
Musique de jeux vidéo, rangée à part dans `SD:/retro` (sous-dossiers compris), avec le nom de la console :
- **SPC** (Super Nintendo), **NSF / NSFE** (NES), **GBS** (Game Boy), **VGM / VGZ / GYM** (Mega Drive,
  Master System), **HES** (PC Engine), **KSS** (MSX), **AY** (ZX Spectrum), **SAP** (Atari) ;
- **modules** de la scène démo et des jeux PC : **MOD, XM, IT, S3M** ;
- **GameCube** : `.dsp` (une paire `nom_L.dsp` / `nom_R.dsp` est jouée en stéréo), `.hps` (HALPST)
  et `.adp` (flux du lecteur de disque).
Les morceaux qui bouclent sont joués deux fois puis s'éteignent en fondu. Dans un NSF / GBS
à plusieurs pistes, **L / R** changent de piste. A = lire, B = arrêter, X = actualiser, Y = pause.

## Onglet PODCASTS
- Abonnements dans `SD:/gc-mp3/podcasts.txt` (`Nom | adresse du flux RSS`) ; quelques podcasts de
  Radio France sont proposés au départ.
- **Rechercher un podcast...** (première ligne) : recherche dans l'annuaire d'Apple Podcasts,
  A sur un résultat = s'abonner.
- Dans un podcast : A = écouter un épisode, **X = le télécharger** dans `SD:/gc-mp3/podcasts` (il apparaît
  ensuite dans MUSIQUE), B = retour. **L / R = reculer de 15 s / avancer de 30 s**.
- La position de chaque épisode est retenue (`SD:/gc-mp3/podcasts/positions.txt`) : on reprend où on s'était arrêté.
- Épisodes en **MP3 ou M4A** (Radio France publie souvent en M4A), adresses longues acceptées
  (2048 caractères). En M4A, les sauts L / R et la reprise sont exacts.
- La recherche utilise le catalogue du pays de la **langue du contenu** (menu START).
- **Nouveaux épisodes automatiques** : GC-MP3 regarde tes abonnements un peu après le démarrage,
  puis toutes les 3 heures. Quand un podcast a un nouvel épisode, il est téléchargé tout seul dans
  `SD:/gc-mp3/podcasts` (un message le dit en bas de l'écran). La première fois, il note seulement le
  dernier épisode (rien n'est téléchargé). START > Nouveaux podcasts pour l'arrêter.

## Onglet PHOTOS
- Les images `.jpg` / `.png` de `SD:/photos`, `SD:/ecran` et `SD:/downloads` en vignettes.
  A = plein écran (croix = photo précédente / suivante, A = diaporama), Y = diaporama direct.
  La musique continue pendant ce temps.
- **Fond d'écran** : en plein écran, **X** met la photo en fond de GC-MP3 à la place du fond violet
  (assombrie pour que le texte reste lisible ; les panneaux deviennent un peu transparents).
  START > Fond d'écran pour revenir au violet.
- **Vidéos** `.avi` (MJPEG + son PCM, 640 × 480 maximum) dans `SD:/videos`. Pour convertir une vidéo
  sur ordinateur :
  `ffmpeg -i film.mp4 -vf scale=320:-2 -r 20 -c:v mjpeg -q:v 5 -c:a pcm_s16le -ar 32000 film.avi`

## Onglet FICHIERS
Explorateur de la carte SD : A = ouvrir un dossier (ou lire un morceau, voir une photo, lire un
fichier texte), B = dossier parent, **X = actions** : renommer, supprimer (avec confirmation),
copier, couper, coller, nouveau dossier. L'espace libre s'affiche en bas.

## Onglet INFOS
- **Heure et date**, **météo** de ta ville (open-meteo.com, gratuit) : température, vent,
  humidité et 5 jours. A sur la ville pour la changer.
- **Actualités** : les titres d'un flux RSS (France Info par défaut, réglage `actualites` dans
  `gc-mp3.txt`) ; A ouvre l'article dans l'onglet WEB. X = actualiser.
- **Réveil** : A l'active, gauche / droite règle l'heure (par 5 min) ; la ligne suivante choisit
  ce qu'il joue (ta musique ou une de tes radios). **Réveil en douceur** : le son part de presque
  rien et monte pendant 3 minutes, et l'écran affiche en grand l'heure, la date et **la météo du
  jour** (chargée 5 minutes avant). A = garder la musique, B = arrêter le réveil.
  La GameCube doit rester allumée sur GC-MP3, et l'heure vient de son horloge.

## Onglet TÉLÉPHONE
Avec le Broadband Adapter : l'écran montre un **QR code**. Scanne-le avec le téléphone (même Wi-Fi /
même box) pour ouvrir la page de GC-MP3 dans le navigateur du téléphone :
- **télécommande** : lecture / pause, précédent / suivant, volume, stop, mode soirée, disposition,
  pochette et titre en cours ;
- choisir un **morceau** ou une **radio** dans la liste ;
- **ouvrir une page web sur la télé** (carte « Ouvrir sur la télé ») ;
- **tous les réglages** (onglet « Réglages » de la page) : ceux du menu START avec ◀ ▶, les éléments du
  lecteur et les onglets affichés (cases à cocher), et la ville, le flux d'actualités, le réveil, etc. ;
- **envoyer des fichiers sur la carte SD** : musique, rétro, photos, vidéos, podcasts… (barre de
  progression sur le téléphone et sur la télé). Les nouveaux morceaux apparaissent tout de suite.
Le petit serveur démarre tout seul quand le réseau est prêt (réglage `telephone`). Il n'y a pas de
mot de passe : n'importe qui sur ton réseau local peut s'en servir, et il ne peut écrire que dans
les dossiers prévus.

## Onglet RADIO
- Mes radios : `SD:/gc-mp3/radio.txt`, une station par ligne : `Nom de la radio | http://adresse/du/flux`.
- **Rechercher une radio...** (première ligne) : annuaire libre radio-browser.info, recherche par nom.
  **Les plus écoutées en France** : la liste des stations populaires.
  Dans les résultats : A = écouter, **X = ajouter à mes radios** (écrit dans `radio.txt`).
- Flux **MP3**, **AAC / HE-AAC** (aacPlus), **OGG Vorbis** et fichiers **M4A**, en http:// ou https://. Les listes
  `.m3u` / `.pls` et les redirections sont suivies. Radios **HLS** (`.m3u8`, segments MPEG-TS ou
  AAC / MP3) gérées aussi. Pas d'Opus, ni de HLS chiffré ou au format fMP4.
- **Enregistrer la radio** : START > Enregistrer la radio. Le flux est enregistré tel quel
  dans `SD:/gc-mp3/radio/enregistrements/` (« REC » s'affiche en haut de l'écran).
- Préférer des serveurs proches (France / Europe) : avec les serveurs lointains, le réseau
  de la GameCube peut être trop lent pour du 128 kbps.
- Logo facultatif : `SD:/gc-mp3/radio/<Nom>.jpg` ou `.png`.
- **Titres de la radio** : quand une chanson te plaît, **X** (dans l'onglet RADIO ou en mode soirée)
  garde son titre dans `SD:/gc-mp3/gc-mp3-titres.txt`, avec la radio, la date et l'heure. START > Titres
  radio gardés pour les revoir (A = le chercher dans ta musique). Sur le téléphone (onglet Radios
  de la page du QR code), toucher un titre le cherche sur YouTube.
- Les certificats https sont vérifiés (liste Mozilla), sauf les dates d'expiration (horloge
  de la GameCube souvent fausse). L'aléa du chiffrement vient de l'horloge du processeur
  (pas de générateur matériel) : suffisant pour la radio, pas pour des mots de passe.

## Onglet WEB
- Navigateur simple : texte, titres, listes, liens, formulaires et images (JPEG, PNG, GIF, **WebP**).
  Pas de JavaScript ni de vidéo : les sites simples marchent (Wikipédia, archive.org…),
  pas YouTube, Spotify, SoundCloud ni Deezer. SVG ne s'affiche pas.
- **Le texte s'affiche tout de suite** pendant que la page se charge, puis se complète.
- **Flèche au stick principal** : pointe un lien ou un champ, A pour l'ouvrir ; elle disparaît
  toute seule (ou à la croix). Le stick C fait défiler la page.
- **Formulaires complets** : champs texte et mot de passe (masqué), cases à cocher, boutons radio,
  listes déroulantes, envoi GET et POST. Avec plusieurs champs, remplis-les puis choisis le bouton.
- **Cookies** : les sites peuvent garder ta connexion et tes préférences. Ils sont enregistrés dans
  `SD:/gc-mp3/cookies.txt` (les cookies permanents seulement). Ce fichier contient tes connexions :
  ne le partage pas. Accueil > « Effacer les cookies » pour tout supprimer.
- Les **images déjà vues** restent en mémoire (3 Mo) : revenir sur une page est instantané.
- Un lien vers un **MP3, un M4A ou une radio** (mp3, m4a, ogg, aac, m3u, pls…) propose : A = écouter tout de
  suite (sans télécharger), X = télécharger, B = annuler.
- **Depuis le téléphone** : carte « Ouvrir sur la télé » de la page TÉLÉPHONE, colle une adresse,
  la page s'ouvre sur la GameCube.
- Accueil (X) : recherche web, **musique libre sur archive.org** (liste directe des MP3 de chaque album), favoris
  (`SD:/gc-mp3/favoris.txt`, une ligne par site : `Nom | adresse`).
- Un lien vers un autre fichier est téléchargé dans `SD:/downloads` en tâche de fond ; les MP3 apparaissent
  ensuite dans l'onglet MUSIQUE.

## Dolphin (émulateur, PC et Android)
GC-MP3 se lance aussi dans Dolphin (ouvre le fichier `.dol`). Dolphin n'émule pas d'adaptateur SD
pour la GameCube : GC-MP3 utilise alors une **carte SD virtuelle en mémoire** (6 Mo).
- Les **réglages** (radios, favoris, podcasts, positions d'écoute, gc-mp3.txt…) sont gardés sur la
  **carte mémoire** de l'emplacement A (ou B), dans le fichier « GC-MP3 donnees ».
- Pour Internet (radio, podcasts, web, météo) : Dolphin > Configuration > GameCube > **SP1 =
  Broadband Adapter (HLE)** (« Built In » sur Android).
- La musique téléchargée reste jusqu'à la fermeture de GC-MP3. La page du téléphone ne peut pas
  joindre Dolphin (le réseau émulé n'accepte pas les connexions entrantes), et la mise à jour en un
  clic est désactivée (télécharge le `.dol` à la main).

## Fluidité
- La barre de sélection glisse d'une ligne à l'autre ; la croix maintenue va de plus en plus vite.
- **Bibliothèque en cache** (`SD:/gc-mp3/gc-mp3-bib.txt`) : les durées et les étiquettes déjà lues sont
  gardées ; au démarrage suivant, la liste défile sans relire chaque fichier et les vues Artistes /
  Albums sont prêtes tout de suite. Le fichier peut être supprimé sans risque (il se refait).
- Les durées ne sont pas lues pendant qu'on fait défiler la liste (pas d'à-coups).
- **Changement de morceau instantané** : la pochette est décodée en tâche de fond (le son démarre
  tout de suite), puis gardée toute prête dans `SD:/gc-mp3/cache/pochettes` : la fois suivante, elle
  s'affiche immédiatement. Pareil pour les logos des radios et les pochettes trouvées sur Internet.
- **Onglet PHOTOS rapide** : pour les photos d'appareil photo ou de téléphone, GC-MP3 prend la petite
  image déjà rangée dans la photo (EXIF) au lieu de décoder toute la photo — et les très grandes
  photos ont enfin leur vignette. Les vignettes sont gardées dans `SD:/gc-mp3/cache/vignettes`.
- **Connexions https plus rapides** : la session sécurisée des derniers sites est réutilisée
  (paroles, pochettes, météo, podcasts, radios en https) : plus besoin de refaire les longs calculs
  de chiffrement à chaque fois. Les adresses des sites sont aussi gardées 30 minutes.
- **Son sans coupure** : environ 0,2 s de son est préparé d'avance ; quand GC-MP3 est occupé un
  instant (carte SD, réseau, image à décoder), la musique ne saute plus (MP3 à 320 kb/s compris).
- Une seule attente du processeur graphique par image, et pas de calcul du spectre quand rien ne joue.
- Le dossier `SD:/gc-mp3/cache` peut être supprimé sans risque : il se refait tout seul.

## Menu (START)
Lecture aléatoire, répétition, **fondu enchaîné**, liste MUSIQUE, **volume**, **volume égalisé**,
**égaliseur** (réglages tout prêts : Rap, Rock, Pop, Basses, Voix, Électro, Classique, Jazz, ou graves /
médiums / aigus à la main), **thème de couleurs** (Indigo, Océan,
Émeraude, Rubis, Ambre, Couleurs douces, Graphite — « Couleurs douces » si l'image paraît trop vive
sur ta télé), **économiseur d'écran** (2 / 5 / 10 / 30 min ou jamais, n'importe quelle touche le quitte), pochettes et paroles automatiques, reprise au démarrage,
**type de télé** (*Écran plat*, ou *Cathodique* : 480i et 4:3, pour une télé à tube ou un moniteur PVM),
**vidéo** (auto = 480p avec un câble composante ou un GCVideo ; B maintenu au démarrage = 480i ;
le 480i est maintenant bien gardé même si la console était réglée en progressif ; un changement de
mode vidéo s'applique au prochain démarrage),
**taille de l'écran** (80 à 112 % : en dessous de 100, l'image rétrécit si les bords sont coupés ;
au-dessus, elle s'élargit pour supprimer les bandes noires des côtés — 110 % conseillé en 16:9),
**hauteur de l'écran** (80 à 100 %) et **position verticale** (plus haut / plus bas) si le haut ou le
bas de l'image est coupé par la télé,
**format de l'écran** (4:3 ou 16:9 : en 16:9, règle aussi la télé en mode large / plein écran ;
l'interface s'étale sur toute la largeur, le texte, les pochettes, les photos et le QR code
gardent leurs vraies proportions, et les pages web sont mises en page plus larges),
**langue du contenu** (Auto = langue de la console réglée dans l'IPL, ou français, anglais, allemand,
espagnol, italien, néerlandais : pays des podcasts et des « radios les plus écoutées », langue demandée
aux sites web et noms de villes de la météo ; l'interface reste en français),
**fond d'écran** (violet ou ta photo), **décor de saison** (chauves-souris et citrouilles à Halloween,
guirlande et neige à Noël, soleil en été, selon la date), **nouveaux podcasts** (téléchargés tout seuls),
**jeux et karaoké**, **titres radio gardés**, **mode soirée**, **vibrations de la manette**, **minuterie** (arrêt après 15 à 120 min, le son baisse doucement les 30 dernières
secondes), **disposition du lecteur**, **éléments du lecteur**, **onglets affichés**,
enregistrement de la radio, et **Quitter**. Tout est enregistré dans `SD:/gc-mp3/gc-mp3.txt`.

### Style de l'économiseur d'écran
START > « Style de l'économiseur » : Gauche / Droite pour choisir, **A pour un aperçu**.
- **Spectre** : le visualiseur qui se déplace lentement.
- **Pochette** : la pochette (ou le logo de la radio) en grand, le titre, l'artiste et l'heure.
- **Horloge** : grande horloge, la date et le morceau en cours.
- **Étoiles** : champ d'étoiles qui accélère avec la musique.
- **Photos** : diaporama de tes images. Mets des `.jpg` / `.png` dans `SD:/ecran` (le dossier est créé
  tout seul la première fois). Taille maxi 1920 × 1080 : réduis les photos du téléphone avant de les copier.
  Chaque photo reste 12 s avec un léger zoom et un fondu, dans le désordre.
- **Écran noir** : rien du tout (la musique continue).
- **Spectrogramme** : les fréquences défilent de droite à gauche (graves en bas, aigus en haut),
  la couleur montre leur force. Aussi dans le mode soirée (4e style).
- **Aquarium** : des poissons qui nagent plus vite quand la musique est forte, des algues qui dansent
  sur les basses et des bulles qui montent sur chaque coup de basse.
Tout bouge lentement pour ne pas marquer l'écran.

## Commandes
| Touche | MUSIQUE | RADIO | WEB |
|---|---|---|---|
| Croix | Choisir / page | Choisir / page | Lien précédent / suivant, page |
| A | Lire, ouvrir un dossier | Écouter | Ouvrir le lien |
| B | Arrêter | Arrêter | Page précédente |
| X | Actualiser la liste | Ajouter aux radios (résultats) | Accueil |
| Y | Pause | Couper le son | Adresse / recherche |
| L / R | Morceau précédent / suivant | Station précédente / suivante | — |
| Stick C | Vue de la liste (← →), ↑ mode soirée, ↓ disposition | ↑ mode soirée, ↓ disposition | Défiler |
| Z | Onglet suivant | Onglet suivant | Onglet suivant |
| START | Menu | Menu | Menu |

Clavier : A = taper, B = effacer, X = espace, START = valider, Y = annuler.

## Mode veilleuse
START > « Mode veilleuse » (A pour lancer, ou F2 sur le clavier GameCube) : une grande horloge
orange très peu lumineuse sur fond noir, avec la date et ce qui joue. Si rien ne joue, la dernière
radio est lancée (réglage `veilleuse_radio`). Haut / bas : luminosité (5 niveaux, retenue),
X : minuterie (15 à 120 min), Y : pause, L / R : morceau ou radio précédent / suivant,
B ou START : sortir. L'économiseur d'écran ne se déclenche pas pendant la veilleuse.

## Clavier GameCube (ASCII, celui de Phantasy Star Online)
Branché sur n'importe quel port manette, il est reconnu tout seul.
- Dès que le clavier à l'écran est ouvert (adresse web, recherche, ville, podcasts…), tape
  directement : Entrée = valider, Échap = annuler, Retour arrière = effacer.
- Dans l'onglet WEB, taper une lettre ouvre la barre d'adresse.
- Ailleurs : flèches = croix, Entrée = A, Échap = B, Espace = pause, Tab = onglet suivant,
  F1 = menu, F2 = veilleuse, Page préc. / suiv. = morceau précédent / suivant.
Disposition japonaise : les symboles suivent les inscriptions des touches.

## Mise à jour en un clic
À chaque compilation réussie, le workflow publie le `.dol` dans la release « dernier » de ton
dépôt GitHub. Dans GC-MP3 : START > « Mise à jour » affiche ton numéro de version ; A vérifie, puis
A installe la nouvelle version. Le fichier est vérifié, l'ancien est gardé à côté
(`gc-mp3player.dol.ancien`), puis tu quittes et relances GC-MP3. Au démarrage, GC-MP3 vérifie tout
seul et affiche un bandeau vert si une version plus récente existe (réglage `maj_auto`).
Le fichier remplacé est celui qui a été lancé, sinon le premier `gc-mp3player.dol` trouvé sur la
carte ; tu peux l'imposer avec `maj_fichier = sd:/chemin/fichier.dol` dans `gc-mp3.txt`.

## Journal de plantage
Si GC-MP3 plante (écran rouge) ou se bloque plus de 20 secondes, il écrit un rapport dans
`SD:/gc-mp3/gc-mp3-crash.txt` : le type d'erreur, la fonction en cause, la pile d'appels, l'onglet, le
morceau ou la radio en cours, la mémoire libre et les 16 dernières actions. Au démarrage suivant, un
message le signale et le fichier devient `SD:/gc-mp3/gc-mp3-crash-ancien.txt`. Depuis le téléphone, le lien
« Journal de plantage » en bas de la page (adresse `/crash.txt`) l'affiche pour le copier facilement.
Le rapport n'est pas écrit si le plantage a eu lieu pendant un accès à la carte SD (pour ne pas
l'abîmer) : l'écran rouge reste alors la seule trace.

## Compilation (GitHub Actions)
**Réseau rapide** : la pile réseau de libogc n'accepte qu'un seul paquet (1,4 Ko) à la fois avant
d'attendre la réponse du serveur, ce qui limite fortement la vitesse (quelques dizaines de Ko/s
avec un serveur sur Internet). Le workflow la recompile (`tools/fast_net.py`, même version de
libogc) avec 16 paquets à la fois. L'onglet TÉLÉPHONE affiche « Réseau rapide » quand c'est actif ;
si cette étape échoue, le `.dol` est quand même construit avec le réseau normal. La vitesse
moyenne s'affiche à la fin de chaque téléchargement.

Le workflow télécharge minimp3, stb_image, mbedTLS 3.6.7, le décodeur AAC Helix, dr_flac,
stb_vorbis, game-music-emu 0.6.5, libxmp 4.7.4 (partie « lite »), qrcodegen, le décodeur WebP de libwebp 1.6.0 (versions figées,
empreintes vérifiées ; `tools/fetch_libs.py` extrait les fichiers utiles) et la liste de
certificats de curl.se, puis compile le `.dol`.

## Crédits
minimp3 (lieff, CC0), stb_image et stb_vorbis (Sean Barrett, domaine public), dr_flac (David Reid,
domaine public), game-music-emu (Shay Green et autres, LGPL 2.1), libxmp (Claudio Matsuoka et autres,
licence MIT), qrcodegen (Nayuki, MIT), libwebp (Google, licence BSD), mbedTLS (Apache 2.0),
décodeur AAC Helix (RealNetworks, licence RPSL, via ESP8266Audio), MusicBrainz, Cover Art Archive
et radio-browser.info (données libres), open-meteo.com (météo, CC BY 4.0), LRCLIB, certificats racines Mozilla (MPL 2.0, via curl.se),
police DejaVu Sans, libogc / devkitPro.

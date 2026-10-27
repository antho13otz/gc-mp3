# GC Station — GameCube Media Center

*(anciennement GC-MP3 : les fichiers gardent leur nom, `gc-mp3player.dol` et `SD:/gc-mp3`, pour que les mises à jour continuent de marcher)*

Centre multimédia pour GameCube : ta musique, des webradios, tes jeux GameCube, tes photos et vidéos,
le web, des podcasts, la météo, une télécommande sur téléphone… dans une seule application, sans Swiss.

## Installation et mise à jour
- Copie `gc-mp3player.dol` sur la carte SD (SD2SP2 ou SD Gecko) et lance-le (ou START > Réglages >
  Jeux > « Démarrer la console sur GC Station » pour qu'il s'ouvre à l'allumage, via `ipl.dol`).
- **Mise à jour en un clic** : START > Mise à jour (version publiée dans la release
  [« dernier »](https://github.com/antho13otz/gc-mp3/releases/tag/dernier) à chaque compilation réussie).
- **Disque de démarrage** (consoles avec puce) : grave `gc-station-disque-<région>-bN.iso` sur un
  mini-DVD. Au démarrage, la version plus récente de la carte SD est lancée s'il y en a une
  (B maintenu : rester sur celle du disque).

## Nouveautés de la version actuelle (v185)
- **Captures d'écran en jeu (L + R + Z) et « ta musique dans les jeux » enlevées** : elles ne marchaient pas sur la
  console. Pulse revient à son code d'avant ces deux fonctions ; les succès « Photographe » et « Album photo » sont
  enlevés (12 succès). Le fichier `gc-mp3/natif/captures.bin` est effacé au démarrage ; ton dossier `musique-jeux`
  n'est pas touché (tu peux l'effacer).

## Version précédente (v184)
- **Supprimer un jeu** : fiche du jeu > Y (Réglages du jeu) > dernière ligne « Supprimer ce jeu de la carte SD »,
  puis A deux fois. Le fichier du jeu est effacé (et son dossier s'il devient vide) ; ses sauvegardes, réglages et
  cheats restent. Seulement pour les jeux de la carte SD (pas le GC Loader, le disque ou le réseau).

## Fonctionnalités principales
- **Musique** (`SD:/mp3`) : MP3, FLAC, OGG, AAC… pochettes, paroles synchronisées, vues Artistes /
  Albums, listes de lecture, égaliseur, fondu enchaîné, enchaînement sans blanc, reprise au démarrage.
- **Radio** : webradios (recherche radio-browser, HLS), favoris, enregistrement, titres gardés.
- **Jeux GameCube** (`.iso`, `.gcm`, `.ciso`) avec jaquettes, bannières, favoris, temps de jeu :
  - **Disque de la console** : le jeu du vrai lecteur, lancé depuis la même liste ;
  - **Pulse** (A), le lanceur de GC Station (ancien nom : Resident) : faux lecteur DVD sur la carte SD (.iso, .gcm,
    .ciso), musique du disque, jeux à 2 disques, carte mémoire virtuelle, cheats, 16:9 forcé, A+Z+START tenus 1 s =
    retour à GC Station, journal en cas de problème ;
  - **GC Loader** : ses jeux dans la même liste ; **jeux du réseau** copiés puis lancés ;
  - **lanceur de secours** (X, et les jeux avec cheats) ; réglages par jeu (langue, son, carte) ;
  - **cheats** (`SD:/gc-mp3/cheats`), **cartes mémoire virtuelles**.
- **Applis** : les homebrews `.dol` de la carte SD.
- **Fichiers** : carte SD, **serveurs du réseau SMB, FTP, FSP** et **autres GC Station** (Salon) (musique, photos, vidéos, copie).
- **Photos** et **vidéos**, **Web** (navigateur), **Podcasts**, **Rétro** (musique de jeux NSF, SPC…),
  **Infos** (météo, actualités, réveil), **Téléphone** (télécommande et envoi de fichiers par QR code),
  **Téléchargements**.

## Fonctionnalités secondaires
- Mode soirée, karaoké et mini-jeux (jeu de rythme, quiz musical, snake, casse-briques, pong…).
- Économiseur d'écran (8 styles), mode veilleuse, minuterie, réveil.
- Thèmes de couleurs, style Classique, Aurore, Vitrine ou Îlots (animés), fond d'écran, décor de saison (8 décors, automatique ou au choix), dispositions du lecteur.
- Réglages vidéo (480i / 480p, 4:3 / 16:9, taille et position de l'image), français / anglais.
- Clavier GameCube ASCII, curseur au stick dans tous les onglets (en haut : tous les onglets ; la croix le cache).
- Fonctionne aussi dans Dolphin (carte SD virtuelle, réglages sur la carte mémoire).
- Journal de plantage (`SD:/gc-mp3/journaux/gc-mp3-crash.txt`), temps de démarrage (`SD:/gc-mp3/journaux/demarrage.txt`).

## Commandes
**Z** : onglet suivant · **START** : menu et réglages · **A** : choisir / lire · **B** : retour / arrêter ·
**Y** : pause · **L / R** : précédent / suivant · **stick C** : vues, mode soirée.

## Fichiers sur la carte SD
- Tes fichiers : `SD:/mp3`, `SD:/retro`, `SD:/photos`, `SD:/videos`, `SD:/ecran`, `SD:/downloads`, tes jeux où tu veux.
- Tout ce que GC Station crée est dans **`SD:/gc-mp3`**, rangé (v182) :
  `gc-mp3.txt` (tes réglages, seul fichier à la racine du dossier) ; `reglages/` (radios, favoris, podcasts,
  volumes, cookies, sauvegarde des réglages) ; `bibliotheque/` (bibliothèque, écoutes, titres gardés,
  téléchargements) ; `mes-parties/` (jeux lancés, temps de jeu, favoris, succès, soirée, compatibilité) ;
  `journaux/` (plantage, démarrage, fluidité, réparation, lanceur) ; `lanceur/` (lanceur de secours) ;
  `natif/`, `resident/` (Pulse) ; `cheats/`, `listes/`,
  `radio/`, `podcasts/`, `jaquettes/`, `cache/`… Les fichiers d'avant sont rangés tout seuls au démarrage.

## Pour les développeurs
- Compilation : GitHub Actions (devkitPPC), voir `.github/workflows/build.yml`.
- Simulateur sur PC : `tools/pc-sim` ; banc d'essai de Pulse : `tools/natif-test`.
- Historique complet des versions et détails : [`docs/HISTORIQUE.md`](docs/HISTORIQUE.md).

## Crédits
minimp3, stb_image, stb_vorbis, dr_flac, game-music-emu, libxmp, qrcodegen, libwebp, mbedTLS,
décodeur AAC Helix, MusicBrainz, Cover Art Archive, radio-browser.info, open-meteo.com, LRCLIB,
certificats Mozilla, police DejaVu Sans, libogc / devkitPro. Lanceur de secours et en-têtes du
disque de démarrage : swiss-gc (GPL v2).

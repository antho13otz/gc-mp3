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

## Nouveautés de la version actuelle (v129)
- **Deux nouveaux styles** (START > Réglages > Affichage > Style) :
  - **Nébula** : la suite d'Aurore : grandes lueurs floues rose, violette et cyan qui dérivent, cadres en verre
    avec reflet, sélection en dégradé rose → cyan, légende dans une pastille de verre ;
  - **Halo** : noir profond et contrastes forts : **une couleur par onglet** (violet Musique, cyan Radio, orange
    Rétro, rose Podcasts, vert Photos, rouge Jeux…), contours lumineux, nom de l'onglet en grand avec les
    icônes des onglets.
- Les styles **Moderne** et **Nouvelle** sont retirés (ceux qui les utilisaient passent à Aurore).
- (v128) Onglet enlevé = coupé ; 576i / 576p ; début de morceau plus fluide.

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
- **Fichiers** : carte SD et **serveurs du réseau SMB, FTP, FSP** (musique, photos, vidéos, copie).
- **Photos** et **vidéos**, **Web** (navigateur), **Podcasts**, **Rétro** (musique de jeux NSF, SPC…),
  **Infos** (météo, actualités, réveil), **Téléphone** (télécommande et envoi de fichiers par QR code),
  **Téléchargements**.

## Fonctionnalités secondaires
- Mode soirée, karaoké et mini-jeux (jeu de rythme, quiz musical, snake, casse-briques, pong…).
- Économiseur d'écran (8 styles), mode veilleuse, minuterie, réveil.
- Thèmes de couleurs, style Classique, Aurore, Nébula ou Halo (animés), fond d'écran, décor de saison (8 décors, automatique ou au choix), dispositions du lecteur.
- Réglages vidéo (480i / 480p, 4:3 / 16:9, taille et position de l'image), français / anglais.
- Clavier GameCube ASCII, curseur au stick dans tous les onglets (en haut : tous les onglets ; la croix le cache).
- Fonctionne aussi dans Dolphin (carte SD virtuelle, réglages sur la carte mémoire).
- Journal de plantage (`SD:/gc-mp3/gc-mp3-crash.txt`), temps de démarrage (`SD:/gc-mp3/demarrage.txt`).

## Commandes
**Z** : onglet suivant · **START** : menu et réglages · **A** : choisir / lire · **B** : retour / arrêter ·
**Y** : pause · **L / R** : précédent / suivant · **stick C** : vues, mode soirée.

## Fichiers sur la carte SD
- Tes fichiers : `SD:/mp3`, `SD:/retro`, `SD:/photos`, `SD:/videos`, `SD:/ecran`, `SD:/downloads`, tes jeux où tu veux.
- Tout ce que GC Station crée est dans **`SD:/gc-mp3`** : réglages (`gc-mp3.txt`), radios, podcasts,
  listes, caches, jaquettes, cheats, lanceur de secours (`lanceur/`), Pulse (`natif/`, `resident/`).

## Pour les développeurs
- Compilation : GitHub Actions (devkitPPC), voir `.github/workflows/build.yml`.
- Simulateur sur PC : `tools/pc-sim` ; banc d'essai de Pulse : `tools/natif-test`.
- Historique complet des versions et détails : [`docs/HISTORIQUE.md`](docs/HISTORIQUE.md).

## Crédits
minimp3, stb_image, stb_vorbis, dr_flac, game-music-emu, libxmp, qrcodegen, libwebp, mbedTLS,
décodeur AAC Helix, MusicBrainz, Cover Art Archive, radio-browser.info, open-meteo.com, LRCLIB,
certificats Mozilla, police DejaVu Sans, libogc / devkitPro. Lanceur de secours et en-têtes du
disque de démarrage : swiss-gc (GPL v2).

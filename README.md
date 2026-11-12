# GC Station — GameCube Media Center

Centre multimédia et lanceur de jeux pour **Nintendo GameCube**, avec **Pulse**, le compagnon qui reste en mémoire pendant
tes parties, et **GC Remote Lite**, l'appli Android qui pilote tout depuis ton téléphone.
*(Anciennement GC-MP3 : les fichiers gardent leur nom, `gc-mp3player.dol` et `SD:/gc-mp3`.)*

**📥 Télécharger : [les Releases](https://github.com/antho13otz/gc-mp3/releases)** — la dernière console (`gc-mp3player-bNNN.dol`)
et la dernière appli (`gc-remote-lite-NNN.apk`).

> **English** — GC Station is a homebrew media center and game launcher for the Nintendo GameCube (music, web radios,
> photos, videos, web, podcasts, GameCube games), with *Pulse*, a resident companion that stays in memory while you play
> (cheats, achievements shown on the TV, live 3D view on your phone), and *GC Remote Lite*, its Android companion app.
> Grab the latest `.dol` and `.apk` from the [Releases](https://github.com/antho13otz/gc-mp3/releases) page. The interface is in
> French and English.
>
> **RetroAchievements testers:** see the [tester guide](https://github.com/antho13otz/gc-mp3/blob/main/docs/RA-TESTER-GUIDE.md).

## Installation
- **Console** : copie `gc-mp3player-bNNN.dol` sur la carte SD (SD2SP2 ou SD Gecko) et lance-le comme tes autres homebrew.
  GC Station et Pulse s'occupent ensuite de tout, jeux compris.
  La version `gc-station-compresse-bNNN.bin` est la même, plus petite.
- **Mises à jour** : sur la console, START > Mise à jour. Elle se met à jour toute seule depuis les publications du projet.
- **Téléphone** : installe `gc-remote-lite-NNN.apk` (Android). L'appli se met ensuite à jour toute seule.
  Console et téléphone sur le même réseau (Broadband Adapter ou adaptateur W5500 sur la console).

## La console
- **Musique** (`SD:/mp3`) : MP3, FLAC, OGG, AAC, pochettes, paroles, listes, égaliseur ; **webradios** et globe des radios en 3D.
- **Jeux GameCube** (`.iso`, `.gcm`, `.ciso`) : lancement avec **Pulse**, lanceur de secours, cartes mémoire virtuelles,
  cheats, profils, temps de jeu, succès GC Station.
- **Photos, vidéos, web, podcasts, rétro** (NSF, SPC…), **infos** (météo, actualités, réveil), **fichiers** (carte SD, SMB, FTP).
- Vidéo 480i, 480p, 576i, 576p, 4:3 / 16:9. Français / anglais. Fonctionne aussi dans Dolphin.

## Pulse, pendant tes parties
- Reste en mémoire pendant le jeu : cheats, retour au menu (A + Z + START), carte mémoire virtuelle.
- **Succès sur la télé** : un bandeau en bas de l'image quand un succès tombe (GC Station ou RetroAchievements).
- Mode **Pulse universel** pour les jeux qui le demandent.
- Le téléphone voit la partie en direct : compagnon de jeu, chercheur de codes, **vue 3D** de Wind Waker, Mario Kart et
  Rayman 3 (avec les packs que tu installes toi-même).

## GC Remote Lite (Android)
- **Jeux** : ta bibliothèque, jaquettes, étagère 3D, fiche de chaque jeu, lancement en un geste.
- **Pulse** : la partie en cours, le compagnon, les codes, les succès, le bandeau de la télé, les mini-jeux.
- **Musique** et **Télé** : la télécommande, et l'image de la télé sur le téléphone.
- **Console** : fichiers, photos, réglages, cartes mémoire, outils, rapport de bug.
- **RetroAchievements** (mode normal), fiches des jeux, mode hors-ligne.

## Commandes de la console
**L / R** : onglet précédent / suivant · **START** : menu · **A** : choisir · **B** : retour · **Y** : pause ·
**Z** : morceau suivant (deux appuis : précédent) · **stick C** : vues, mode soirée.
Réglages et données : `SD:/gc-mp3` (réglages dans `gc-mp3.txt`).

## Pour les développeurs
- Le projet vit sur GitLab ([gc-station-group/gc-station](https://gitlab.com/gc-station-group/gc-station)) ; ce dépôt GitHub
  en reçoit une copie, et chaque publication y est recopiée dans les Releases.
- Compilation : CircleCI (`.circleci/config.yml` : devkitPPC pour la console, Android SDK pour l'appli).
- Simulateur PC : `tools/pc-sim` · banc d'essai de Pulse (émulateur PowerPC) : `tools/natif-test`.
- Historique des versions : [`docs/HISTORIQUE.md`](docs/HISTORIQUE.md).
- Licence MIT (`LICENSE`) pour la console seulement (`source/`, `tools/`) ; l'appli Android et la version PC ne sont pas
  libres. Crédits : [`CREDITS.md`](CREDITS.md).

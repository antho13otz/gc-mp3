# GC Station — GameCube Media Center

Centre multimédia pour GameCube : musique, webradios, jeux GameCube, photos et vidéos, web, podcasts,
météo et télécommande sur téléphone, dans une seule application.
*(Anciennement GC-MP3 : les fichiers gardent leur nom, `gc-mp3player.dol` et `SD:/gc-mp3`.)*

## Installation
- Copie `gc-mp3player.dol` de la release [« dernier »](https://github.com/antho13otz/gc-mp3/releases/tag/dernier)
  sur la carte SD (SD2SP2 ou SD Gecko) et lance-le.
- Mise à jour : START > Mise à jour.
- Consoles avec puce : grave `gc-station-disque-<région>-bN.iso` sur un mini-DVD.

## Fonctionnalités
- **Musique** (`SD:/mp3`) : MP3, FLAC, OGG, AAC, pochettes, paroles, listes, égaliseur.
- **Radio** : webradios, favoris, enregistrement.
- **Jeux GameCube** (`.iso`, `.gcm`, `.ciso`) : lanceur Pulse (A), lanceur de secours (X), disque de la console,
  GC Loader, cartes mémoire virtuelles, cheats, profils de joueurs.
- **Photos, vidéos, web, podcasts, rétro** (NSF, SPC…), **infos** (météo, actualités, réveil), **fichiers**
  (carte SD et réseau SMB / FTP), **téléphone** (télécommande et envoi de fichiers par QR code).
- **Jukebox des jeux** (musiques lues dans tes images .iso), **globe des radios** en 3D, **compagnon** qui grandit avec ta musique.
- **Assistant IA** (onglet WEB) : questions à Claude, à un service compatible OpenAI ou à Ollama sur un PC (clé API à toi).
- Vidéo : 480i, 480p, 576i et 576p (50 Hz), 4:3 / 16:9. Français / anglais. Fonctionne aussi dans Dolphin.

## Commandes
**Z** : onglet suivant · **START** : menu · **A** : choisir · **B** : retour · **Y** : pause ·
**L / R** : précédent / suivant · **stick C** : vues, mode soirée.

Réglages et données de GC Station : `SD:/gc-mp3` (réglages dans `gc-mp3.txt`).

## Pour les développeurs
- Compilation : GitHub Actions (devkitPPC), `.github/workflows/build.yml`.
- Simulateur PC : `tools/pc-sim` · banc d'essai de Pulse : `tools/natif-test`.
- Historique des versions : [`docs/HISTORIQUE.md`](docs/HISTORIQUE.md) · licence MIT (`LICENSE`) ·
  crédits : [`CREDITS.md`](CREDITS.md).

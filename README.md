# GC-MP3 — Indigo Audio Player

Lecteur audio pour GameCube : **ta musique** (carte SD), des **webradios**, un **navigateur web**,
la **musique de jeux rétro**, des **podcasts**, tes **photos et vidéos**, un **explorateur de fichiers**,
la **météo / actualités / réveil**, et une **télécommande sur téléphone** (Broadband Adapter),
dans une interface GX style Indigo. Lancer `gc-mp3player.dol` depuis Swiss / Indigo.

## Onglets
**Z** passe à l'onglet suivant : MUSIQUE, RADIO, WEB, RÉTRO, PODCASTS, PHOTOS, FICHIERS, INFOS,
TÉLÉPHONE. Le nom de l'onglet et des petits points (position) s'affichent en haut de la liste.
Pour cacher les onglets dont tu ne te sers pas : START > « Onglets affichés » (A coche / décoche).

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
- Formats : **MP3, FLAC, OGG Vorbis et WAV** (`.mp3`, `.flac`, `.ogg`, `.wav`), lus dans `SD:/mp3`
  et `SD:/downloads`, sous-dossiers compris (3 niveaux, 512 fichiers maximum). Les FLAC haute
  résolution (24 bits, 96 kHz) passent ; les fichiers Opus ne sont pas gérés. Ils sont lus au fur et à mesure depuis la carte : **pas de limite de taille**
  (les longs mix passent).
- **Stick C gauche / droite** : change la liste — *Tous les morceaux*, *Dossiers*, *Artistes*, *Albums*.
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
- Pochettes : image du même nom que le MP3, pochette intégrée, ou `cover.jpg` / `folder.jpg` du dossier.
  Sinon, avec le Broadband Adapter branché, elle est cherchée sur MusicBrainz / Cover Art Archive
  et enregistrée à côté du MP3. L'album et l'année s'affichent sous l'artiste.

## Mode soirée
**Stick C vers le haut** (onglets MUSIQUE et RADIO), ou START > Mode soirée : visualiseur plein écran.
Les basses donnent le rythme (pulsations, éclairs, anneaux, particules), les couleurs tournent avec
la musique, et les **paroles s'affichent en grand** (sinon le titre et l'artiste).
Trois styles : *Pulsation*, *Barres*, *Tunnel* (stick C gauche / droite pour changer).
L / R et Y marchent comme d'habitude ; B ou START pour sortir.

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
- Abonnements dans `SD:/podcasts.txt` (`Nom | adresse du flux RSS`) ; quelques podcasts de
  Radio France sont proposés au départ.
- **Rechercher un podcast...** (première ligne) : recherche dans l'annuaire d'Apple Podcasts,
  A sur un résultat = s'abonner.
- Dans un podcast : A = écouter un épisode, **X = le télécharger** dans `SD:/podcasts` (il apparaît
  ensuite dans MUSIQUE), B = retour. **L / R = reculer de 15 s / avancer de 30 s**.
- La position de chaque épisode est retenue (`SD:/podcasts/positions.txt`) : on reprend où on s'était arrêté.

## Onglet PHOTOS
- Les images `.jpg` / `.png` de `SD:/photos`, `SD:/ecran` et `SD:/downloads` en vignettes.
  A = plein écran (croix = photo précédente / suivante, A = diaporama), Y = diaporama direct.
  La musique continue pendant ce temps.
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
  ce qu'il joue (ta musique ou une de tes radios). Le son monte doucement pendant 30 s.
  La GameCube doit rester allumée sur GC-MP3, et l'heure vient de son horloge.

## Onglet TÉLÉPHONE
Avec le Broadband Adapter : l'écran montre un **QR code**. Scanne-le avec le téléphone (même Wi-Fi /
même box) pour ouvrir la page de GC-MP3 dans le navigateur du téléphone :
- **télécommande** : lecture / pause, précédent / suivant, volume, stop, mode soirée, disposition,
  pochette et titre en cours ;
- choisir un **morceau** ou une **radio** dans la liste ;
- **envoyer des fichiers sur la carte SD** : musique, rétro, photos, vidéos, podcasts… (barre de
  progression sur le téléphone et sur la télé). Les nouveaux morceaux apparaissent tout de suite.
Le petit serveur démarre tout seul quand le réseau est prêt (réglage `telephone`). Il n'y a pas de
mot de passe : n'importe qui sur ton réseau local peut s'en servir, et il ne peut écrire que dans
les dossiers prévus.

## Onglet RADIO
- Mes radios : `SD:/radio.txt`, une station par ligne : `Nom de la radio | http://adresse/du/flux`.
- **Rechercher une radio...** (première ligne) : annuaire libre radio-browser.info, recherche par nom.
  **Les plus écoutées en France** : la liste des stations populaires.
  Dans les résultats : A = écouter, **X = ajouter à mes radios** (écrit dans `radio.txt`).
- Flux **MP3**, **AAC / HE-AAC** (aacPlus) et **OGG Vorbis**, en http:// ou https://. Les listes
  `.m3u` / `.pls` et les redirections sont suivies. Radios **HLS** (`.m3u8`, segments MPEG-TS ou
  AAC / MP3) gérées aussi. Pas d'Opus, ni de HLS chiffré ou au format fMP4.
- **Enregistrer la radio** : START > Enregistrer la radio. Le flux est enregistré tel quel
  dans `SD:/radio/enregistrements/` (« REC » s'affiche en haut de l'écran).
- Préférer des serveurs proches (France / Europe) : avec les serveurs lointains, le réseau
  de la GameCube peut être trop lent pour du 128 kbps.
- Logo facultatif : `SD:/radio/<Nom>.jpg` ou `.png`.
- Les certificats https sont vérifiés (liste Mozilla), sauf les dates d'expiration (horloge
  de la GameCube souvent fausse). L'aléa du chiffrement vient de l'horloge du processeur
  (pas de générateur matériel) : suffisant pour la radio, pas pour des mots de passe.

## Onglet WEB
- Navigateur simple : texte, titres, listes, liens et images (JPEG, PNG, GIF).
  Pas de JavaScript ni de vidéo : les sites simples marchent (Wikipédia, archive.org…),
  pas YouTube, Spotify, SoundCloud ni Deezer. WebP et SVG ne s'affichent pas.
- Accueil (X) : recherche web, **musique libre sur archive.org** (liste directe des MP3 de chaque album), favoris
  (`SD:/favoris.txt`, une ligne par site : `Nom | adresse`).
- Un lien vers un fichier est téléchargé dans `SD:/downloads` en tâche de fond ; les MP3 apparaissent
  ensuite dans l'onglet MUSIQUE.

## Menu (START)
Lecture aléatoire, répétition, **fondu enchaîné**, liste MUSIQUE, **volume**, **volume égalisé**,
**graves / aigus**, **thème de couleurs** (Indigo, Océan,
Émeraude, Rubis, Ambre, Couleurs douces, Graphite — « Couleurs douces » si l'image paraît trop vive
sur ta télé), **économiseur d'écran** (2 / 5 / 10 / 30 min ou jamais, n'importe quelle touche le quitte), pochettes et paroles automatiques, reprise au démarrage,
**vidéo** (auto = 480p avec un câble composante ou un GCVideo ; B maintenu au démarrage = 480i),
**mode soirée**, **minuterie** (arrêt après 15 à 120 min, le son baisse doucement les 30 dernières
secondes), **disposition du lecteur**, **éléments du lecteur**, **onglets affichés**,
enregistrement de la radio, et **Quitter**. Tout est enregistré dans `SD:/gc-mp3.txt`.

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

## Journal de plantage
Si GC-MP3 plante (écran rouge) ou se bloque plus de 20 secondes, il écrit un rapport dans
`SD:/gc-mp3-crash.txt` : le type d'erreur, la fonction en cause, la pile d'appels, l'onglet, le
morceau ou la radio en cours, la mémoire libre et les 16 dernières actions. Au démarrage suivant, un
message le signale et le fichier devient `SD:/gc-mp3-crash-ancien.txt`. Depuis le téléphone, le lien
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
stb_vorbis, game-music-emu 0.6.5, libxmp 4.7.4 (partie « lite »), qrcodegen (versions figées,
empreintes vérifiées ; `tools/fetch_libs.py` extrait les fichiers utiles) et la liste de
certificats de curl.se, puis compile le `.dol`.

## Crédits
minimp3 (lieff, CC0), stb_image et stb_vorbis (Sean Barrett, domaine public), dr_flac (David Reid,
domaine public), game-music-emu (Shay Green et autres, LGPL 2.1), libxmp (Claudio Matsuoka et autres,
licence MIT), qrcodegen (Nayuki, MIT), mbedTLS (Apache 2.0),
décodeur AAC Helix (RealNetworks, licence RPSL, via ESP8266Audio), MusicBrainz, Cover Art Archive
et radio-browser.info (données libres), open-meteo.com (météo, CC BY 4.0), LRCLIB, certificats racines Mozilla (MPL 2.0, via curl.se),
police DejaVu Sans, libogc / devkitPro.

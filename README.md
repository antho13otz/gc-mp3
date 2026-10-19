# GC-MP3 — Indigo Audio Player

Lecteur audio pour GameCube : **ta musique** (carte SD), des **webradios** et un **navigateur web**
pour télécharger de la musique (Broadband Adapter), dans une interface GX style Indigo.
Lancer `gc-mp3player.dol` depuis Swiss / Indigo.

## Onglet MUSIQUE
- Les MP3 sont lus dans `SD:/mp3` et `SD:/downloads`, sous-dossiers compris (3 niveaux,
  512 fichiers maximum). Ils sont lus au fur et à mesure depuis la carte : **pas de limite de taille**
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
  `[01:23.45]paroles`) s'affiche ligne par ligne à la place de l'égaliseur.
- Pochettes : image du même nom que le MP3, pochette intégrée, ou `cover.jpg` / `folder.jpg` du dossier.
  Sinon, avec le Broadband Adapter branché, elle est cherchée sur MusicBrainz / Cover Art Archive
  et enregistrée à côté du MP3. L'album et l'année s'affichent sous l'artiste.

## Onglet RADIO
- Mes radios : `SD:/radio.txt`, une station par ligne : `Nom de la radio | http://adresse/du/flux`.
- **Rechercher une radio...** (première ligne) : annuaire libre radio-browser.info, recherche par nom.
  **Les plus écoutées en France** : la liste des stations populaires.
  Dans les résultats : A = écouter, **X = ajouter à mes radios** (écrit dans `radio.txt`).
- Flux **MP3** et **AAC / HE-AAC** (aacPlus), en http:// ou https://. Les listes `.m3u` / `.pls`
  et les redirections sont suivies. Pas d'OGG / Opus / HLS.
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
Lecture aléatoire, répétition, liste MUSIQUE, **volume**, **thème de couleurs** (Indigo, Océan,
Émeraude, Rubis, Ambre, Couleurs douces, Graphite — « Couleurs douces » si l'image paraît trop vive
sur ta télé), **économiseur d'écran** (2 / 5 / 10 / 30 min ou jamais : un visualiseur qui bouge
lentement, n'importe quelle touche le quitte), pochettes automatiques, reprise au démarrage,
**vidéo** (auto = 480p avec un câble composante ou un GCVideo ; B maintenu au démarrage = 480i),
enregistrement de la radio, et **Quitter**. Tout est enregistré dans `SD:/gc-mp3.txt`.

## Commandes
| Touche | MUSIQUE | RADIO | WEB |
|---|---|---|---|
| Croix | Choisir / page | Choisir / page | Lien précédent / suivant, page |
| A | Lire, ouvrir un dossier | Écouter | Ouvrir le lien |
| B | Arrêter | Arrêter | Page précédente |
| X | Actualiser la liste | Ajouter aux radios (résultats) | Accueil |
| Y | Pause | Couper le son | Adresse / recherche |
| L / R | Morceau précédent / suivant | Station précédente / suivante | — |
| Stick C | Vue de la liste (← →) | — | Défiler |
| Z | Onglet suivant | Onglet suivant | Onglet suivant |
| START | Menu | Menu | Menu |

Clavier : A = taper, B = effacer, X = espace, START = valider, Y = annuler.

## Compilation (GitHub Actions)
Le workflow télécharge minimp3, stb_image, mbedTLS 3.6.7, le décodeur AAC Helix (version figée,
empreinte vérifiée) et la liste de certificats de curl.se, puis compile le `.dol`.

## Crédits
minimp3 (lieff, CC0), stb_image (Sean Barrett, domaine public), mbedTLS (Apache 2.0),
décodeur AAC Helix (RealNetworks, licence RPSL, via ESP8266Audio), MusicBrainz, Cover Art Archive
et radio-browser.info (données libres), certificats racines Mozilla (MPL 2.0, via curl.se),
police DejaVu Sans, libogc / devkitPro.

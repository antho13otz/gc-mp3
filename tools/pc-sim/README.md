# GC Station sur PC (simulateur)

Le **vrai code de GC Station** (`source/main.c` et tous les onglets), compilé pour Linux.
Il sert à essayer une modification **sans la console** : voir l'écran, lire le journal,
écouter le son, tester le réseau. La compilation GameCube reste celle de GitHub (devkitPPC).

```sh
sh tools/pc-sim/run.sh                       # tour de tous les onglets
sh tools/pc-sim/run.sh scenarios/tour.txt    # (même chose)
```

Résultats dans `tools/pc-sim/work/run/` : `out/*.png` (captures 640×480), `planche.png`
(toutes les captures), `out/son.wav` (le son sorti par GC Station), `log.txt`.

Besoins : gcc, g++, make, git, python3 (+ Pillow pour la planche), ffmpeg (carte SD de démo),
clang + ld.lld (programme résident du lanceur maison). Rien d'autre à installer.

## Comment ça marche
| Sur la GameCube | Sur PC |
|---|---|
| libogc | `pcsim.c` + `include/` (mêmes noms de fonctions) |
| GX (processeur graphique) | `pcsim_gx.c` : dessin logiciel (triangles, textures GameCube, TEV, transparence), seulement pour les images capturées |
| un seul processeur | un seul thread à la fois (verrou global), qui cède la main quand il attend |
| écran 60 Hz | rappel de balayage à 60 Hz (temps réel) |
| manette | **scénario** (voir plus bas) |
| son (DMA) | consommé au rythme réel, enregistré dans `PCSIM_WAV` |
| carte SD | le dossier `sd:` du dossier courant (`demo-sd.sh` en crée une : musique MP3 / FLAC / OGG avec pochette, photos, un faux jeu GameCube) |
| carte mémoire | absente |
| réseau (BBA) | vrais sockets du PC ; les requêtes DNS de GC Station sont traitées par le simulateur |

Réseau (`PCSIM_NET`) : `proxy` (par défaut si `HTTPS_PROXY` existe, sessions cloud : les
connexions passent par le proxy, les sites interdits sont refusés tout de suite), `direct`,
ou `off` (hors ligne). `PCSIM_HOSTS="nom=127.0.0.1"` : pour un serveur de test local.
`PCSIM_NETLOG=1` : affiche les noms demandés et les connexions.

## Comme sur la vraie GameCube
Par défaut, le simulateur a les **limites de la console** (`pcsim_limits.c`), pour que ce qui
passe ici passe aussi sur le matériel officiel :

| | GameCube | Réglage |
|---|---|---|
| Mémoire | 24 Mo, dont ~10,3 Mo pris par le programme lui-même (v79) : il reste **~12,7 Mo** de tas. Au-delà, `malloc` rend NULL, comme sur la console. La mémoire libre affichée par GC Station suit ce tas. | `PCSIM_DOL=gc.dol` (calcul exact ; `run.sh` lit le .dol de la release), `PCSIM_HEAP=octets` |
| Processeur | Gekko 486 MHz : le calcul de GC Station est **12 fois plus lent** que sur le PC de test (Xeon 2,1 GHz). Le dessin (processeur graphique) n'est pas compté. | `PCSIM_CPU=12` |
| Son | interruption DMA en temps réel : si GC Station calcule trop lentement, le son coupe (audible dans `son.wav`) | |
| Carte SD | SD2SP2 / SD Gecko avec libfat : **1,5 Mo/s** en lecture, **0,6 Mo/s** en écriture, + temps d'accès (ouverture 3 ms, stat 1,5 ms, dossier 2 ms) | `PCSIM_SD_KBS`, `PCSIM_SD_WKBS` |
| Réseau | Broadband Adapter : **500 Ko/s** en réception | `PCSIM_NET_KBS` |
| Écran | **60 Hz** (EURGB60) par défaut ; **50 Hz** dès que GC Station choisit un mode PAL (576i, 576p, 528i). Le câble composante (nécessaire au 480p / 576p) est absent par défaut. | `PCSIM_PAL=1` (console PAL), `PCSIM_COMPOSANTE=1` (câble composante) |

À la fin, `log.txt` donne : le temps de calcul « GameCube » utilisé, le pic de mémoire (et les
réservations refusées), la fluidité (images/s, images ratées et à quel moment du scénario).
Pour essayer le **576p 50 Hz** : `video = 576p` dans `work/run/sd:/gc-mp3/gc-mp3.txt`, puis
`PCSIM_PAL=1 PCSIM_COMPOSANTE=1 sh tools/pc-sim/run.sh` (le journal affiche « ecran : 640x576 lignes progressif, 50 Hz »).

`PCSIM_FAST=1` enlève toutes les limites. `PCSIM_MEMLOG=1` signale chaque réservation refusée.

Le facteur processeur est une estimation : pour le recaler, il suffit de comparer
`SD:/gc-mp3/journaux/fluidite.txt` (START > « Mesure de fluidité ») de la vraie console avec le simulateur.

## Scénarios
Un fichier texte, une commande par ligne (`#` = commentaire) :
```
wait 2s | wait 500ms | wait 30f     attendre (secondes, millisecondes, images)
press A [n]                         appuyer (n images, 3 par défaut) puis relâcher
combo A+Z+START                     plusieurs boutons ensemble
hold START 2s                       maintenir
stick main 0 100 1s                 stick principal (ou c) : x y durée
shot nom                            capture de l'image suivante -> out/nom.png
log texte                           message dans log.txt
quit                                fin
```
Boutons : `A B X Y Z L R START UP DOWN LEFT RIGHT`.

## Limites
- Pas de lanceur de jeux (Swiss / Resident) ni de carte mémoire : ils parlent au matériel.
  Le lanceur maison a son propre banc d'essai : `tools/natif-test/run.sh`.
- Les textures 16 bits et la police sont lues dans l'ordre d'octets du PC (le programme les
  écrit avec le processeur) ; une texture copiée telle quelle depuis un fichier GameCube
  aurait des couleurs fausses ici (pas sur la console).
- Le temps est réel : un scénario de 20 s dure 20 s.
- Le processeur graphique n'est pas limité (le dessin de GC Station est léger pour Flipper).
- Réseau des sessions cloud : chaque connexion passe par un proxy (~200 ms de plus qu'Internet
  depuis chez toi), et beaucoup de sites y sont interdits (refusés tout de suite ici, mais pas
  sur ta console).

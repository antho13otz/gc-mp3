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

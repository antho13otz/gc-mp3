# GC Station PC (Windows)

Un programme à part, séparé de la console : **sa propre copie du code**, sa propre numérotation
(`PC 1`, builds de « GC Station PC (Windows) »), ses propres essais. Rien ici n'est compilé pour la
GameCube, et rien de `source/` (la console) n'est compilé ici. Les deux restent liés par ce qu'ils
partagent à l'usage (Salon, Salon par Internet, appli du téléphone), pas par le code.

Une nouveauté de la console ne passe sur le PC que quand on la reporte ici, exprès.

## Dossiers
- `source/` : la copie du code de GC Station pour le PC (faite depuis la console v357), avec ce qui
  est propre au PC : les jeux lancés dans Dolphin, la souris, le clavier qui tape (`tab_phone.c`, à la fin).
- `plateforme/` : ce qui remplace le matériel de la GameCube : fenêtre, image, clavier, souris,
  manettes, son (`pcsim_win.c`), Dolphin et le réseau local par Internet (`pcsim_dolphin.c`),
  mises à jour par GitHub (`pcsim_maj.c`), réseau, fichiers (`pcsim.c`, `pcsim_limits.c`), dessin (`pcsim_gx.c`).
- `win/` : en-têtes pour Windows, `LISEZMOI.txt` (donné avec le programme), scénarios d'essai, faux Dolphin.
- `essais/` : essai du Salon par Internet. `outils/` : outils de compilation et d'essai.

## Compiler (depuis Linux)
```
apt install clang lld llvm gcc-mingw-w64-i686 g++-mingw-w64-i686
sh pc/fetch.sh            # une fois : les bibliothèques, dans pc/work/libs
make -C pc BUILD=123      # -> pc/work/gcstation.exe
```
Sur GitHub : `.github/workflows/windows.yml` compile et essaye sur un vrai Windows, à chaque
changement de `pc/`. Le paquet `gc-station-pc` est privé (artefact, 90 jours), jamais publié.
La compilation de la console (`build.yml`) ignore `pc/` : un changement du PC ne fait jamais de
version de la console.

## Lancer
Dézipper, double-cliquer `GC Station.exe`. Le dossier `sd` à côté est sa carte SD. Pas de limites de
la GameCube (mémoire, vitesse, carte SD lente : rien n'est imité).

Entrées : clavier, souris (la flèche de GC Station suit la souris ; clic gauche A, droit B, molette),
le clavier du PC tape dans les zones de texte (clavier de GC Station et pages web), Ctrl+F, Espace
et touches multimédia (lecture/pause…), manettes Xbox (XInput) et autres (joyGetPosEx, boutons
réglables dans `manette.txt`), net sur les écrans à 125/150 %, Maj+F11 pour changer d'écran
(gardé dans `fenetre.txt`). Le détail des touches : `win/LISEZMOI.txt`.

## Ce qui manque par rapport à la console
- les jeux ne tournent pas dans GC Station : ils sont lancés dans Dolphin (pas de Swiss, pas de
  lanceur maison, pas de carte mémoire physique) ;
- les touches multimédia ne marchent que si la fenêtre a la main ;
- la fréquence de l'écran et le thème clair / sombre de Windows ne sont pas suivis (60 images/s, 4:3) ;
- tout ce qui parle au matériel de la GameCube (BBA, carte SD Gecko, lecteur de disque).

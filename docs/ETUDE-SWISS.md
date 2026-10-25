# Étude : ce que Swiss règle, et comment l'adapter à Pulse

*(v133, octobre 2026. Swiss : dépôt public `emukidid/swiss-gc`, licence GPL 2. On lit son code pour comprendre
le problème et la méthode ; Pulse garde son propre code, réécrit ici : rien n'est recopié tel quel.)*

## 1. Les deux façons de faire un faux lecteur

| | Swiss | Pulse |
|---|---|---|
| Accès du jeu au lecteur | le jeu est **piégé** : chaque accès aux registres matériels (lecteur, EXI, PI, DSP, VI) déclenche une exception, et Swiss **exécute l'instruction à sa place** (`emulator.c` : `ppc_step`) | les accès trouvés **dans le code** (recherche `lis 0xCC00` + décalage) sont remplacés par des sauts vers Pulse (83 pour Mario Superstar Baseball) |
| Réveil du jeu (fin de commande) | **interruption fabriquée sans le lecteur** : une lecture à une adresse invalide déclenche une interruption « erreur PI », et Swiss fait croire au jeu que c'est celle du lecteur (registres PI virtuels) | une **vraie commande au vrai lecteur** (« inquiry ») : sa fin réveille le jeu, Pulse appelle alors la fonction du jeu |
| Bibliothèque du jeu | des fonctions du SDK sont reconnues par leur **signature** (taille, nombre de chaque instruction) puis remplacées : lecture, alarmes de délai, réinitialisation, couvercle, interruptions, EXI | non : Pulse travaille au niveau des registres |
| Ce qui est patché | le programme principal, **et** les autres `.dol`, `.elf`, `switcher.prs`, `appl2.img`, `exec.img`, `.tgc` | le programme principal, et ce qui est chargé ensuite par OSExec (v95) |

Conséquences pour Pulse :
- un accès au lecteur que la recherche ne trouve pas (adresse calculée autrement) n'est pas branché ;
- le réveil dépend du vrai lecteur : s'il est occupé, absent, ou si le jeu remet sa fonction d'interruption, une
  fin de commande peut se perdre, et le jeu attend pour toujours.

## 2. Ce que Swiss corrige, par catégorie

### Générique (tous les jeux)
1. **Fins de commande jamais perdues** : interruption fabriquée, état du lecteur tenu par Swiss.
2. **Délais du lecteur** : les alarmes de délai de la bibliothèque du jeu (`SetTimeoutAlarm`,
   `AlarmHandlerForTimeout`) sont gérées pour qu'une lecture lente ne soit pas prise pour une panne.
3. **Erreurs et couvercle** : `DVDLowRequestError`, `DVDLowWaitCoverClose`, `DVDLowReset`, `DVDCheckDisk`…
   rendent des réponses cohérentes, sans vrai lecteur.
4. **Partage du bus EXI** : la carte SD et la carte mémoire sont sur le même bus ; Swiss passe par
   `EXILock` / `EXISync` / `EXIAttach` du jeu pour ne jamais parler en même temps que lui.
5. **Musique du disque** (DTK) décodée et mélangée au son.
6. **Programmes multiples** : tous les exécutables du disque sont patchés avant le lancement.
7. **Mémoire** : Swiss vit en mémoire basse ; il déplace donc les structures de certains jeux.
8. **Vidéo** : forcer un mode, le progressif, le 16:9, le filtrage des textures, la langue des polices.
9. **Retour au menu** (A+Z+START), **cheats**.

### Jeux précis (`Patch_GameSpecific`)
- **Courses entre une lecture et son rappel** (une lecture instantanée arrive avant que le jeu soit prêt) :
  `GS2` (rappel du lecteur), `GDR`, `GHA`, `GLE` (transfert ARAM).
- **Initialisation de l'image** (écran noir au démarrage) : `GGIJ`, `GON`, `GOY`, `GR8`, `GRZJ`.
- **Carte mémoire** : `GDK`, `GDX`, `GWT` refusent les cartes de plus de 507 blocs ; Swiss leur fait accepter les
  cartes 1019 et 2043 blocs (la carte virtuelle de Pulse a 251 blocs : pas concernée).
- **Anti-débogage** retiré (démos, quelques jeux).
- Langue forcée, structures déplacées (propre à la place qu'occupe Swiss en mémoire basse : pas utile à Pulse).

## 3. État de Pulse et plan

| Problème | Pulse aujourd'hui | Plan |
|---|---|---|
| Fin de commande perdue | **v134 : réveil sans le lecteur** (console sans lecteur, lecteur qui ne répond plus) ; **v133 : secours** à chaque image (lecture en fond relancée, fin donnée au jeu après 3 images sans réveil, si le jeu n'a pas coupé l'interruption du lecteur) ; la fonction du jeu est rebranchée même pendant une lecture en fond | fait en v134, depuis les interruptions du jeu plutôt qu'avec l'erreur PI de Swiss (voir l'historique) |
| Jeu figé, écran noir, sans plantage | **v133** : A+Z+START écrit où en était le jeu (code, appels, pile, mémoire) dans le journal | lire ces journaux jeu par jeu |
| Accès au lecteur pas trouvés | **v134** : recherche large (après un saut, adresses calculées), branchés en secours | piéger les accès restants par exception (table de pages : risqué) |
| Délais de la bibliothèque | **v136** : rien à faire : l'alarme du SDK est annulée par chaque fin de commande (quelques ms, 3 images au pire) ; chez Swiss, `SetTimeoutAlarm` sert à reconnaître la lecture du SDK qu'il remplace | — |
| Courses lecture / rappel | **v134** : vitesse « Auto » (vrai lecteur pour `GS2`, `GDR`, `GHA`, `GLE`) ; **v135** : + `GYQ` ; corrections ARAM de `GDR`, `GHA`, `GLE` dans la table | compléter la liste avec les journaux |
| Initialisation de l'image | **v135** : table `na_fixes` (`GGIJ`, `GON`, `GOY`, `GR8`, `GRZJ`) quand l'exécutable est le main.dol | les mêmes dans les exécutables secondaires |
| Bus EXI partagé (SD2SP2 : port série 2, pas le bus des cartes mémoire) | pas de conflit en SD2SP2 ; **v145** : avec un SD Gecko, Pulse attend que le jeu libère le bus | — |
| Exécutables secondaires | OSExec suivi (v95) ; **v136** : corrections de la table appliquées au passage dans les `.dol`, `.elf`, `.tgc` du disque | `switcher.prs` (compressé) si un jeu en a besoin |

## 4. Mario Superstar Baseball (GYQE01) : écran noir figé
Journal reçu : le jeu tournait (1956 images, 1915 dessinées, son actif) et ne demandait plus rien au lecteur.
Sa **dernière commande était une lecture en fond** (4160 octets, `DISR 2A` : pas encore finie). Hypothèse :
la lecture en fond s'est arrêtée au milieu (réveil perdu, ou fonction d'interruption du jeu remise pendant
la lecture : Pulse ne la rebranchait pas tant que la lecture n'était pas finie). Le jeu attendait donc une
fin qui ne venait jamais. Corrigé en v133 (secours + rebranchement) ; si l'écran reste noir, le journal
du retour (A+Z+START) montrera désormais l'endroit exact où le jeu attend.

## 5. v135 : table des corrections par jeu
`na_fixes` (`source/natif.c`) : transfert ARAM, cartes 1019 / 2043, image au démarrage, anti-débogage, langue.
Appliquée au main.dol avant le lancement (code du jeu + taille exacte du main.dol). Restent : les exécutables
secondaires (`.elf`, `.tgc`, `switcher.prs`, chargés par le jeu lui-même : il faudrait les corriger au moment où
le jeu les lit) et les alarmes de délai (`SetTimeoutAlarm`, à reconnaître par signature).

Mario Superstar Baseball (build 487) : boucle sans fin dans le lecteur vidéo HVQM4 (anneau plein, lectures
sorties de la boucle par le compilateur) : course due aux lectures trop rapides ; vitesse d'un vrai lecteur
automatique pour `GYQ` en v135.

## 6. v136
- Corrections dans les exécutables secondaires : appliquées à la lecture (le résident change les octets visés
  dans chaque lecture du jeu), positions calculées par GC Station depuis la FST, les sections / segments.
- Mario Superstar Baseball : boucle d'attente du lecteur HVQM4 corrigée dans le code du jeu (pointeurs relus).

## 7. v146 : tout Swiss passé en revue (dépôt du 24/10/2026)
Swiss applique à chaque jeu, dans l'ordre (`Patch_ExecutableFile`) : son hyperviseur (faux lecteur, faux matériel),
les corrections par jeu, puis les réglages (manettes, carte mémoire, vidéo, police). Voici chaque partie et ce que
Pulse en fait.

### Corrections par jeu (`Patch_GameSpecific`, 77 codes de jeu)
| Jeux | Problème | Pulse |
|---|---|---|
| RE2, RE3, Disney's Dinosaur (`GHA`, `GLE`, `GDR`) | course du transfert ARAM | v135 (main.dol) ; **v146 : aussi dans les modules `.rel`** (`claire.rel`, `leon.rel`, `eng.rel`…) |
| `GDK`, `GDX`, `GWT` | cartes 1019 / 2043 blocs | v135 |
| `GGI`, `GON`, `GOY`, `GR8`, `GRZ` | image au démarrage | v135 |
| démos `D58`… `D89`, `E22`, `E23`, `GC6` | anti-débogage | v135 |
| **Pokémon XD (`GXX`), Colosseum japonais (`NXX`, `PCK`, `PCS`)** | anti-débogage | **v146** |
| **F-Zero GX / AX (`GFZ`, `UFZ`)** | le jeu remplace les vecteurs d'exception | **v146** |
| **Hulk (`GH9`), Ultimate Spider-Man (`GUM`), `GWJ`** | haut de la mémoire écrit en dur (0x81800000) | **v146** : lu à 0x800000EC, que Pulse remplit maintenant |
| `GS2` (2 courses lecture / son) | lecture instantanée | v134 : vitesse d'un vrai lecteur automatique |
| `GMS` (langue) | — | v135 |
| PSO (`GPO`, `GPS`), Pokémon Box (`GPX`) | tampons en 0x80001800 | **pas utile** : c'est là que vit Swiss, pas Pulse |
| Wind Waker (`GZL`) sur Wii | — | pas une GameCube |
| BIOS (`case 1435200`…) | démarrage par le BIOS | Pulse ne lance pas le BIOS |

### Fichiers du disque (`Patch_GameSpecificFile`)
- `boot.bin` / `bi2.bin` : **norme des manettes écrite pour tous les jeux (v146)** ; `DPOJ8P` : mémoire simulée ;
  **Star Wars : The Clone Wars (`GSX`) : programme et langue en arguments (v146)**.
- `GCC` (`ffcc_cli.bin`, programme de la GBA pour le lien GBA) : pas repris (lié au câble GBA émulé par Swiss).
- `GS8P7D` (hauteur d'image) : réglage vidéo, pas un blocage.

### Mémoire basse au lancement (`load_app`) — v146
Swiss écrit toujours : type de console, taille de l'ARAM, mode vidéo, **moniteur de débogage 0xE8 / 0xEC** (sa zone
réservée en haut), mémoire simulée, vitesses, 0x30E0, options 0x30E3, **code du lecteur 0x30E6**, **norme des manettes
0x30E9**, et l'instant d'insertion des cartes mémoire émulées (0x30C0 / 0x30C4). Pulse ne les écrivait pas : si
GC Station avait démarré sans le BIOS, certaines restaient à zéro. Pulse les écrit maintenant comme Swiss
(`na_lowmem`), avec sa propre zone réservée (FST, bi2, résident) comme moniteur de débogage.

### Programme compressé
Star Wars Rogue Leader (`GLR`) et `GWXJ13` : l'essentiel du code est compressé (zlib, en 0x80200000) ; Swiss le
décompresse pour le corriger puis le recompresse. Pulse ne voit pas ces accès avant le lancement : **v146 : Pulse
universel (accès pris au vol) activé automatiquement pour ces jeux.**

### Hyperviseur (`Patch_Hypervisor`, `Patch_GameSpecificHypervisor`)
Le cœur de Swiss : faux lecteur, interruptions, EXI, arène. Pulse fait la même chose à sa façon (registres branchés,
résident) ; les jeux de `Patch_GameSpecificHypervisor` (`G3F`, `GHB`, `GIS`, `GMI`, `GSX`, `GT6`, `GXX`, démos) ont
leur propre gestionnaire d'erreur d'accès mémoire (DSI) : à reprendre seulement pour Pulse universel (à faire si un
journal le montre).

### Réglages (`Patch_Miscellaneous`) : reconnus par signature
Fonctions du SDK reconnues à leur « empreinte » (nombre d'instructions de chaque sorte) : manettes (stick C inversé,
fréquence : Pulse le fait au niveau des registres), bouton reset, police, et la carte mémoire :
`VerifyID` (accepter une carte formatée sur une autre console), `DoMount`, délais des commandes de la carte allongés.
Pas encore repris : la carte virtuelle de Pulse est formatée avec l'identifiant de la console, donc `VerifyID` passe ;
ce serait utile pour une carte venue d'une autre console. À faire si un jeu refuse la carte.

### Vidéo (`Patch_GameSpecificVideo`, `Patch_Video`)
Largeur et position de l'image par jeu, modes forcés : du confort, pas des blocages (Pulse a ses propres réglages v122).

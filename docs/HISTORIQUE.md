# GC Station — historique des versions et détails (ancien README)

*(anciennement GC-MP3 : les fichiers gardent leur nom, `gc-mp3player.dol` et `SD:/gc-mp3`, pour que les mises à jour continuent de marcher)*

Lecteur audio pour GameCube : **ta musique** (carte SD), des **webradios**, un **navigateur web**,
la **musique de jeux rétro**, des **podcasts**, tes **photos et vidéos**, un **explorateur de fichiers**,
la **météo / actualités / réveil**, et une **télécommande sur téléphone** (Broadband Adapter),
dans une interface GX style Indigo. Lancer `gc-mp3player.dol` depuis Swiss / Indigo.

## Où sont les fichiers sur la carte SD
- Tes fichiers : `SD:/mp3` (musique), `SD:/retro`, `SD:/photos`, `SD:/videos`, `SD:/ecran`, `SD:/downloads`.
- Tout ce que GC Station crée est dans **`SD:/gc-mp3`**, rangé (v182) :
  `gc-mp3.txt` (tes réglages, seul fichier à la racine du dossier) ; `reglages/` (radios, favoris, podcasts,
  volumes, cookies, sauvegarde des réglages) ; `bibliotheque/` (bibliothèque, écoutes, titres gardés,
  téléchargements) ; `mes-parties/` (jeux lancés, temps de jeu, favoris, succès, soirée, compatibilité) ;
  `journaux/` (plantage, démarrage, fluidité, réparation, lanceur) ; `lanceur/` (lanceur de secours) ;
  `musique-jeux/` (ta musique dans les jeux) ; `natif/`, `resident/` (Pulse) ; `cheats/`, `listes/`,
  `radio/`, `podcasts/`, `jaquettes/`, `cache/`… Les fichiers d'avant sont rangés tout seuls au démarrage.
- Version 21 et avant : les fichiers de la racine sont rangés dans `SD:/gc-mp3` ; v182 : puis dans les sous-dossiers
  (`gcd_moves` dans main.c). Les autres fichiers de la racine (Swiss, `ipl.dol`, `config.ini`…) ne sont pas touchés.

## Onglets
**Z** passe à l'onglet suivant : MUSIQUE, RADIO, WEB, RÉTRO, PODCASTS, PHOTOS, FICHIERS, INFOS,
TÉLÉPHONE, JEUX, TÉLÉCHARGEMENTS. Le nom de l'onglet et des petits points (position) s'affichent en
haut de la liste. Pour cacher les onglets dont tu ne te sers pas : START > « Onglets affichés ».

## Nouveautés appli build 213 — Rayman 3 : ta progression lue dans la sauvegarde
- Plus > **Rayman 3** : lit la sauvegarde GameCube (« RAYMAN 3 Save Data ») sur la carte de la console (slot A ou B, hors
  partie) ou un .gci du téléphone (Dolphin…). Pour chaque partie : cages /60, tampons /9, score, le détail des 9 mondes
  (cages trouvées, score, seuil du tampon), ce qu'il manque pour finir à 100 % et les bonus GameCube (niveaux 2D, mini-jeux).

## Nouveautés appli build 211 — Assistant « 3 photos de la mémoire »
- Plus > Chercheur de codes (jeu lancé) > **Trouver les adresses d'un jeu (3 photos)** : trois étapes guidées (avant,
  juste après, encore après une action), avec ce que tu vois à l'écran (cages, score…) noté dans le nom du fichier, puis
  **Partager les photos** envoie les trois d'un coup. Pour trouver où un nouveau jeu range sa progression (Rayman 3…).

## Nouveautés appli build 209 — Kirby Air Ride dans le compagnon de jeu
- Compagnon de jeu > **Kirby Air Ride** (version européenne) : les 3 listes de défis (Air Ride, Top Ride, City Trial,
  120 cases chacune), le nombre de cases cochées par mode et sur 360, et chaque case (cochée en couleur, manquante en gris).
  Adresse trouvée dans le main.dol européen.

## Nouveautés appli build 207 — Extraire main.dol d'un .iso rangé sur le téléphone
- Plus > **Extraire d'un .iso** : choisis l'image d'un jeu GameCube (.iso ou .gcm) rangée sur le téléphone. L'appli liste
  ses fichiers (main.dol en premier, avec un filtre) et enregistre celui que tu touches où tu veux, sans passer par la console.
  Les formats compressés (.rvz, .ciso, .nkit) ne sont pas lus.

## Nouveautés appli build 205 — Super Mario Sunshine : chaque soleil et chaque pièce bleue manquants par leur nom
- Compagnon de jeu > Sunshine > « Pour finir à 100 % » : zone par zone (Place Delfino et Mont Corona compris), le nom de
  chaque soleil manquant (épisode, niveau secret, 100 pièces, les 18 soleils de la place) et l'emplacement de chaque pièce
  bleue manquante (les 240). Noms traduits d'après la décompilation, pas les titres officiels du jeu.

## Nouveautés appli build 203 — Twilight Princess : insectes dorés et équipement dans le 100 %
- Compagnon de jeu > Twilight Princess > « Pour finir à 100 % » : nouveau bloc **Insectes dorés** (les 24, mâle et femelle
  de chaque espèce, ceux qui manquent en gris), et l'équipement pas encore obtenu (épées, boucliers, Armure magique,
  Tunique Zora, bourses), lus dans les objets déjà obtenus de la sauvegarde.

## Nouveautés appli build 200 — Extraire un fichier du jeu dans le dossier de son choix
- Fiche d'un jeu > « Extraire un fichier du jeu (main.dol…) » : nouveau bouton **Choisir où** : le fichier est enregistré
  dans le dossier choisi (tes fichiers, Drive…) au lieu de Téléchargements/GC Remote. « Extraire » garde l'ancien chemin.

## Nouveautés appli build 198 — Star Fox Adventures : adresse fixe (européen)
- Européen (GSAP01) : la partie en cours est à une adresse fixe, tirée du main.dol (803A4C08 : le code la construit
  51 fois avant de lire les champs de la partie : vie, magie, Tricky, nom, personnage, avancement, temps de jeu). Plus
  besoin de la recherche dans la mémoire : la fiche s'affiche dès que la partie est chargée. Américain v1.1 : 803A3F08
  (codes publics). La recherche reste pour les autres versions.

## Nouveautés appli build 196 — Star Fox Adventures dans le compagnon de jeu
- **Star Fox Adventures** (GSAP01, GSAE01, GSAJ01) : pas de liste de symboles pour la version européenne, donc la partie
  en cours est **cherchée une fois dans la mémoire** (bouton « Trouver ma partie dans la mémoire », jeu en pause une
  trentaine de secondes, comme Ocarina of Time), d'après la structure publiée par RenaKunisaki/StarFoxAdventures
  (notes/savedata.txt : seulement des décalages). Ensuite, en direct : cœurs, magie, scarabs et BafomDads de Krystal et
  de Fox (celui en jeu en premier), énergie de Tricky, avancement (/ 187), temps de jeu, nom du fichier. Si plusieurs
  endroits ressemblent à une partie, l'appli garde celui dont le temps de jeu avance.
- Pas encore essayé sur le vrai jeu.

## Nouveautés v319 (build 826, appli build 193) — GC Station en anglais complétée ; 100 % Paper Mario et Twilight Princess
- Console : **GC Station en anglais** (Réglages > Langue / Language = English, ou console en anglais). Tous les textes
  affichés relevés au simulateur en anglais (tour des onglets, menus, réglages, outils, jeux, test total) : 117 textes
  traduits en plus. L'aide en bas des réglages est traduite avant d'être coupée en deux lignes (sinon elle restait en
  français) ; la vidéo, la langue automatique et la ville de la météo aussi. Restent en français : les noms (radios,
  podcasts, fichiers) et le rapport du test de la console.
- Appli : « **Pour finir à 100 %** » pour **Paper Mario** (étoiles de cristal, partenaires à recruter, bottes et marteau,
  pouvoirs étoile, rang, Puits des 100 épreuves, sortes de badges et badges manquants en gris) et **Twilight Princess**
  (fragments de cœur, âmes, bourse, ombres, éclats du miroir, flacons, objets pas encore trouvés).
- (L'appli elle-même a déjà l'anglais dans Plus > Apparence ; les écrans du compagnon de jeu ne sont pas encore traduits.)

## Nouveautés appli build 191 — Pour finir à 100 % (Wind Waker, Sunshine) ; photo de la mémoire à partager
- **Wind Waker** : une carte « Pour finir à 100 % » d'après la sauvegarde en cours : cœurs (quarts de cœur manquants),
  emplacements d'objets vides, bourse, carquois, sac de bombes, magie, chants, perles, Triforce, cartes obtenues et
  trésors pas encore repêchés, îles jamais visitées (nom et case de la carte marine). (Build 189.)
- **Sunshine** : la même carte, zone par zone : les épisodes, soleils cachés et soleil des 100 pièces qui manquent (avec
  le record de pièces), les pièces bleues qui restent, Delfino, le Mont Corona, les soleils à obtenir contre des pièces
  bleues. (Build 189.)
- **Chercheur de codes** : « Photo de la mémoire (fichier à partager) » : toute la mémoire du jeu (jeu en pause, une
  trentaine de secondes), compressée en .bin.gz dans Téléchargements/GC Remote, puis le menu de partage. Pour trouver les
  adresses de nouveaux jeux (F-Zero GX, Star Fox Adventures, Pokémon XD, 1080°…), avec des valeurs notées à l'écran.

## Nouveautés appli build 187 — Fin de partie Mario Party 5, statistiques de jeu, jeu en cours sur le widget
- **Mario Party 5, fin de partie** : quand les résultats commencent (tour = nombre de tours + 1), un récapitulatif en haut
  de la fiche : classement final, étoiles bonus de chacun (étoiles gagnées pendant les résultats), meilleur aux mini-jeux
  (pièces gagnées aux mini-jeux) ; et pendant toute la partie, **les pièces tour par tour en courbes**. Chaque partie
  finie est gardée sur le téléphone (30 dernières) : hors partie, la fiche montre la dernière en détail, la liste des
  parties et les victoires de chacun.
- **Statistiques de jeu** (Plus > Statistiques de jeu) : temps total, jeux lancés, lancements ; temps de jeu par semaine
  (8 dernières semaines) et cette semaine par jeu ; les plus joués et les plus lancés ; joués dernièrement ; dernières
  parties. Les totaux viennent de la console (temps mesuré par Pulse), le temps par semaine des parties notées par le
  téléphone (du lancement au retour à GC Station) à partir de cette version.
- **Widget** : sous le morceau en cours, le jeu en cours et le temps de la partie.

## Nouveautés appli build 184 — Mario Party 5 : mini-jeux, historique, alertes ; Paper Mario : noms français, plan de la zone
- **Mario Party 5** : le nom du mini-jeu (noms anglais officiels, avec le type : 4 joueurs, 1 contre 3, 2 contre 2,
  bataille, duel, Bowser, DK) ; rangés dans l'ordre de la table du jeu (m501 = Coney Island, vérifié), les autres à
  confirmer en jeu. **Historique des tours** : à chaque nouveau tour, les pièces et étoiles gagnées ou perdues par chaque
  joueur, le mini-jeu joué et le meilleur du tour (gardé tant que l'écran est ouvert). **Alertes** en haut de la fiche :
  quand quelqu'un passe devant toi (toi = le premier joueur qui n'est pas un ordi) et quand quelqu'un arrive à 10 cases
  de l'étoile.
- **Paper Mario** : les ennemis des combats en français (noms habituels de la série : Maskass, Frère Marto, Skelerex,
  Hériss, Bloups, Plante Piranha…, sinon le nom anglais). **Plan de la zone** : le chemin de Mario sur la carte en cours
  (il se dessine en avançant), Mario et les personnages de la carte, avec leurs noms (liste des personnages : vérifiée
  dans le main.dol européen).

## Nouveautés v318 (build 825) — Tour de tous les réglages
- Chaque réglage du menu START (69, toutes les sections sauf les actions) essayé au simulateur : changé, enregistré dans
  gc-mp3.txt, relu, remis comme avant ; et une image de chaque valeur des réglages qui se voient (thèmes, styles,
  dispositions, spectres, décors de saison, économiseurs, langue, format et taille de l'écran…). Tout est gardé et se
  voit comme prévu, sauf ce qui est voulu (minuterie et liste MUSIQUE ne sont pas gardées ; langue des jeux, son et
  position horizontale sont dans la mémoire de la console).
- **Outils** : la liste défile (avant : 20 lignes serrées dans la place de 10, les unes sur les autres).
- L'aide en bas des réglages et des outils passe sur deux lignes au lieu d'être coupée au bord du panneau.
- **Limite de jeu par jour** et **Carte mémoire virtuelle (Pulse)** : gauche revient en arrière (avant : toujours en avant).
- Simulateur : `PCSIM_REGLAGES=1` (essai de chaque réglage) et `PCSIM_REGLAGES=2` (une image par valeur).

## Nouveautés v317 (build 824, appli build 181) — Plus de question après chaque jeu ; Mario Party 5 plus détaillé
- Console : la question « comment ça s'est passé ? » au retour d'un jeu ne s'affiche plus. Le lancement reste dans le
  journal (gc-mp3/natif), un plantage est toujours noté tout seul, et la pastille se pose depuis le téléphone.
- Appli, Mario Party 5 : la case exacte de chaque joueur (type et numéro), l'étoile entourée là où elle est (et sa case
  dans le titre), la distance de chaque joueur à l'étoile (en cases, en suivant les chemins), les capsules tenues (noms)
  et celles posées sur le plateau (petit carré blanc), le classement, les cases qui restent au joueur qui avance, les
  pièces du dernier mini-jeu, les équipes ; les passages (cases grises) en petit sur le plan.

## Nouveautés appli build 179 — Mario Party 5 : le plateau et la case de chaque joueur
- **Plateau vu de dessus** : les vraies positions des cases, lues dans la mémoire du jeu (tableau des cases de masu.c,
  européen en 8026C518, retrouvé dans le main.dol), avec les chemins entre les cases, la couleur de chaque case (bleue,
  rouge, ?, étoile, VS, DK, Bowser) et **le pion de chaque joueur sur sa case**. Lu une fois par plateau.
- **Corrigé** (build 174) : pièces, classement, capsules, compteurs de cases et records étaient lus 2 octets trop loin.
  Champs vérifiés dans le main.dol européen (la fonction du jeu qui rend les pièces lit 0x20, celle des étoiles 0x34).
- (Build 177 : le plateau, avec les anciens champs ; remplacée par la 179.)

## Nouveautés appli build 174 — Compagnon de jeu : adresses tirées des main.dol (Paper Mario PAL, Mario Party 5), Wind Waker, Mario Kart
- **Paper Mario : La Porte Millénaire européen (G8MP01)** : l'inventaire n'est plus cherché au chargement, l'adresse est
  fixe (tirée du main.dol : le pointeur en 8042B420, lu 240 fois par le code de l'inventaire). En plus : objets portés et
  badges équipés (noms français), étoiles de cristal, bottes et marteau, chapitre et nom du lieu, carte, avancée de
  l'histoire (GSW0 / 405), personnage joué (Mario, Peach, Bowser), position, temps de jeu, nombre de sauvegardes, et
  **le combat en cours** (tour, PV de Mario, des partenaires et de chaque ennemi). Américain et japonais : les pointeurs
  sont lus dans le code du jeu (mêmes fonctions, adresses de ttyd-tools), l'inventaire toujours cherché une fois.
- **Mario Party 5 (GP5P01, GP5E01)** : nouvelle fiche : plateau, tour / nombre de tours, à qui de jouer, mini-jeu, banque ;
  pour chaque joueur : personnage (ordi et niveau), étoiles, pièces, classement, capsules, pièces gagnées, records, cases
  visitées. Européen : adresses tirées du main.dol (les trois blocs GwPlayer, GwSystem, GwCommon de la décompilation).
- **Wind Waker** : cartes obtenues / déchiffrées / trésors repêchés, cartes de la Triforce.
- **Mario Kart Double Dash!! (européen)** : vitesse de ton kart, charge du mini-turbo en dérapage, turbo.
- Pas encore : F-Zero GX, Star Fox Adventures, Pokémon XD, 1080° (pas de liste de symboles pour ces versions) et Mario
  Kart américain (main.dol pas reçu) : il faut des photos de la mémoire en pleine partie avec des valeurs notées.
- Rien d'essayé sur un vrai jeu : chaque valeur est bornée, une version inconnue n'affiche rien.

## Nouveautés v315 (build 822, appli build 172) — Les vraies cartes mémoire depuis le téléphone
- Appli : **Plus > Vraies cartes** : les cartes mémoire branchées sur la console (**slots A et B**) : la liste des
  sauvegardes (jeu, titre, commentaire, blocs, blocs libres), et pour chacune : **la télécharger** sur le téléphone en
  .gci (Téléchargements/GC Remote, puis l'envoyer où on veut), **la copier sur la carte virtuelle de Pulse** du jeu, ou
  **l'effacer** (confirmation). Et **« Ajouter un .gci du téléphone sur cette carte »**.
- GC Station : nouvelle demande `/api/vraiecarte?s=A|B&op=liste|gci|imp|del|vc` : la boucle de GC Station fait le
  travail avec les fonctions de START > Outils > Cartes mémoire ; une sauvegarde n'est effacée ou envoyée que si son
  nom correspond encore (la liste a pu changer). Hors partie seulement (pendant un jeu, la carte est au jeu).
- Essai au simulateur (cartes simulées) : liste des deux slots, .gci téléchargé (en-tête du jeu), .gci envoyé sur le
  slot B, effacement, refus si le nom ne correspond plus.

## Nouveautés v314 (build 821, appli build 170) — Extraire un fichier d'un jeu ; Wind Waker et Sunshine américains
- **Extraire un fichier du jeu** (fiche d'un jeu dans l'appli) : `/main.dol` (le programme du jeu) ou n'importe quel
  fichier du disque par son chemin (ex. `/rel/aaa.rel`), enregistré dans Téléchargements/GC Remote, puis partagé.
  GC Station v314 : `/api/discfile` sait aussi sortir `main.dol` (pas dans la table des fichiers : son adresse est
  dans l'en-tête du disque, sa taille d'après ses sections). Hors partie, jeu sur la carte SD. Essai au simulateur :
  identique à l'octet.
- Compagnon : **Wind Waker américain (GZLE01)** et **Sunshine américain (GMSE01)**, mêmes écrans que les européens
  (adresses : zeldaret/tww et doldecomp/sms). Mario Kart Double Dash américain : pas encore (le projet public ne
  couvre que la version européenne et une version de démonstration américaine).

## Nouveautés appli build 168 — Compagnon : tous les jeux (valeurs épinglées), Paper Mario, Ocarina of Time
- **Tous les jeux** : dans le chercheur de codes, une adresse trouvée > **« Épingler au compagnon de jeu »** (nom, et
  un maximum pour avoir une jauge) : la valeur s'affiche en direct dans le compagnon de ce jeu, à chaque partie
  (gardé sur le téléphone ; liste « Mes valeurs épinglées » pour en retirer). Le bouton « Compagnon de jeu et chrono »
  de la partie en cours est là pour tous les jeux. Les jeux qui ont un écran dédié montrent aussi leurs valeurs épinglées.
- **Paper Mario : La Porte Millénaire** (européen, américain, japonais) : PV, PF, pouvoir étoile, points de badge,
  points étoile, niveau, rang, bottes et marteau, public, partenaires (PV, niveau), pièces, éclats d'étoile, soleils,
  objets, badges, record du Puits des 100 épreuves. La structure de l'inventaire vient de doldecomp/ttyd ; pour la
  version européenne, son adresse est retrouvée dans le code du jeu au chargement d'une partie (getter `pouchGetPtr`,
  base r13 lue dans `__init_registers`), et rien n'est affiché tant que les valeurs ne sont pas vraisemblables.
- **Ocarina of Time** dans *Collector's Edition* et *Ocarina Multi Pack* : le jeu N64 tourne dans un émulateur ; un
  bouton lance **une recherche unique** de la sauvegarde en cours dans la mémoire (jeu en pause, comme la photo du
  chercheur de codes), gardée sur le téléphone. Ensuite : cœurs, magie, rubis et bourse, âge, quarts de cœur, double
  défense, Skulltulas d'or, médaillons, pierres, carte Gerudo, chants, inventaire, munitions, épées, boucliers,
  tuniques, bottes, heure, jours, morts (structure : zeldaret/oot, save.h). Majora's Mask et les Zelda NES : non.

## Nouveautés appli build 165 — Compagnon : Twilight Princess, Pikmin 1 et 2, Melee, Mario Superstar Baseball
- L'écran compagnon (onglet Plus > Compagnon de jeu, ou bouton de la partie en cours) reconnaît 5 jeux de plus, avec
  une **fiche commune** (grands chiffres, jauges, compteurs, pastilles), en plus de Wind Waker, Sunshine et Mario Kart :
  - **Twilight Princess** (européen et américain) : cœurs, huile, rubis et bourse, forme (Hylien / loup), flèches,
    bombes, graines, âmes d'Esprits, ombres fusionnées, éclats du miroir, larmes de lumière, poissons, inventaire,
    donjon (petites clés, carte, boussole, clé du boss, boss vaincu), heure, temps de jeu, K.O., lieu, vitesse ;
  - **Pikmin** (européen, américain 1.0 et 1.1) : jour et heure, Pikmin de chaque couleur sur la carte, avec Olimar,
    au travail, dans les Oignons, pièces du vaisseau, Pikmin nés et perdus ;
  - **Pikmin 2** (européen et américain) : santé d'Olimar et de Louie, escouades, Pikmin de chaque couleur en vie et
    dans les Oignons, pokos, trésors, sprays et baies, jour et heure, grotte ;
  - **Super Smash Bros. Melee** (américain 1.02, et les mods qui en partent comme Akaneia) : pour chaque joueur, le
    personnage, les dégâts, les vies, les éjections, les chutes, les autodestructions, les pièces, l'équipe, l'ordinateur ;
  - **Mario Superstar Baseball** (américain, adresses de Project Rio) : score et score par manche, balles, prises,
    retraits, frappeur, lanceur et son endurance, vitesse et type de lancer, étoiles, coureurs sur les bases, et pour
    chaque joueur des deux équipes : coups sûrs, passages au bâton, coups de circuit, points produits, retraits.
- Adresses tirées des tables de symboles publiques (zeldaret/tp, projectPiki/pikmin et pikmin2, doldecomp/melee) et de
  Project Rio ; seulement des adresses, aucun code repris. Lectures limitées comme avant (le jeu ne le sent pas).

## Nouveautés v313 (build 819, appli build 162) — Wind Waker : la vraie carte marine, le plan des intérieurs
- **La vraie carte marine du jeu** dans le compagnon de l'appli : chaque case connue est dessinée avec l'image que
  le jeu utilise pour sa propre carte (`/res/Fmap/Fmap.arc`, une image par case et par variante du fichier de
  sauvegarde). Pendant la partie, ces images ne sont pas dans la mémoire que Pulse lit (le jeu les met dans l'ARAM) :
  l'appli les demande donc à GC Station **hors partie**, une seule fois, et les garde. Les images viennent du jeu de la
  carte SD de l'utilisateur : rien de Nintendo n'est dans l'appli ni dans le dépôt. Sans elles : la carte dessinée
  d'avant.
- GC Station : nouvelle demande du téléphone `/api/discfile?id=<jeu>&f=<chemin>` : un fichier du disque d'un jeu de la
  liste (table des fichiers du disque, .iso et .ciso), envoyé par morceaux de 32 Ko (rien de gros en mémoire). Essai
  au simulateur : fichier identique à l'octet, fichier, dossier ou jeu absent refusés, chemins avec « .. » refusés.
- **Plan des intérieurs** (donjons, maisons, îles) : le chemin de Link se dessine au fil des pas, salle par salle, gardé
  sur le téléphone ; l'étage en cours net, les autres pâles, la salle en cours en rose ; bouton pour l'effacer.
- Carte marine : **toucher une case** affiche le nom de l'île ; la **rose des vents** est sous la carte (elle cachait
  F7 et G7).

## Nouveautés appli build 160 — Ma jaquette
- **« Ma jaquette »** : la jaquette de TA boîte (ta région, ta langue, en haute définition) à la place de celle de
  GameTDB. Pour l'avant, le dos et la tranche : une photo (galerie, appareil photo, capture d'une annonce…), les 4 coins
  posés sur ceux de la jaquette ; l'appli redresse la photo (perspective) et assemble la jaquette complète aux
  proportions de GameTDB. Une face pas donnée : celle de GameTDB. La boîte 3D et l'étagère s'en servent.

## Nouveautés appli build 158 — Compagnon de jeu bien plus riche
- **The Wind Waker** : inventaire (21 emplacements, flacons), épée, bouclier, bracelets, bourse, maximum de flèches et
  de bombes, sacs (butin, appâts, courrier), heure et jour, K.O., îles déjà sur la carte, clés et objets du donjon, cap
  et vitesse de Link, vent en cours ; carte marine illustrée.
- **Super Mario Sunshine** : chaque soleil de chaque zone, pièces bleues et record de pièces par zone, pièces rouges,
  buse en cours et buses débloquées, Yoshi, position, vitesse et altitude de Mario.
- **Mario Kart Double Dash!!** : mode, cylindrée, coupe, manche, miroir, pilotes et kart de chacun, objets tenus,
  classement et progression de chaque kart (écarts en secondes), ballons et points des batailles, records du circuit.
- **Sans gêner le jeu** : au plus 24 blocs de 1 Ko par lecture, demandés un par un (environ un par image du jeu) ; le
  tracé d'un circuit est lu lentement.
- Jaquettes : plus d'IA sur les jaquettes (seulement sur le disque) ; les jaquettes déjà agrandies sont effacées.

## Nouveautés appli build 154 — Mario Kart : le circuit vu du dessus, avec les karts
- Écran compagnon de **Mario Kart Double Dash!!** : le **nom du circuit** et le **circuit dessiné vu du dessus** (la piste,
  ses bords et la ligne d'arrivée), avec **les 8 karts en direct** et la place de chacun ; le kart 1 (toi) en rose.
- Le tracé est lu une fois par circuit dans la mémoire du jeu : les tronçons de la course (bords gauche et droit de
  chaque point de passage), suivis d'un tronçon au suivant jusqu'à refaire le tour. Rien à changer sur la console.

## Nouveautés v312 (build 818) — Chercheur de codes et compagnon dans Mario Kart Double Dash
- **Pulse cache le Broadband Adapter au jeu aussi quand « Chercheur de codes (appli) » est sur Oui** (avant : seulement
  avec les manettes du téléphone). Mario Kart Double Dash cherche l'adaptateur au démarrage pour son mode réseau local ;
  quand on le lui laissait, il pouvait s'en servir sans arrêt et Pulse ne plus le reprendre : le chercheur de codes et
  l'écran compagnon de l'appli risquaient de rester sans réponse dans ce jeu. Contrepartie : tant que ce réglage est sur Oui, le mode
  réseau local de Mario Kart (et des autres jeux qui se servent de l'adaptateur) n'est pas proposé ; le remettre sur Non
  pour une partie en réseau entre consoles.
- Banc d'essai de Pulse (`emu_mem.py`) : le jeu lit « aucun périphérique » avec le seul chercheur de codes, et le vrai
  adaptateur sans option (la version d'avant échoue à ce test).

## Nouveautés appli build 152 — Compagnon de jeu et chrono de speedrun
- **Compagnon de jeu** (onglet Plus, ou bouton « Compagnon de jeu et chrono » de la partie en cours) : pendant une partie
  lancée avec Pulse (réglage « Chercheur de codes (appli) » = Oui), l'appli lit le jeu en direct, 5 fois par seconde,
  sans le mettre en pause. Versions européennes :
  - **The Wind Waker** (GZLP01) : cœurs (par quarts), magie, rubis, flèches, bombes, perles, Triforce, chants, le lieu ;
    sur la Grande Mer, la **carte marine** 7 x 7 avec la position de Link, son sillage et le nom de l'île de la case ;
  - **Super Mario Sunshine** (GMSP01) : soleils, pièces bleues, pièces, vies, santé, eau de JET, zone et épisode ;
  - **Mario Kart Double Dash!!** (GM4P01) : place, tour, avancée dans le tour, temps de chaque tour (au millième).
- **Chrono de speedrun** : part tout seul (nouveau fichier de Wind Waker, arrivée sur l'aéroport de Sunshine, départ de
  la première course de Mario Kart), avec des étapes automatiques (lieux, perles et Triforce ; chaque soleil ; chaque
  course) et la fin (soleil du Mont Corona ; dernière course d'une coupe ou des 16 courses ; bouton Fin pour Zelda).
  Record et meilleurs segments gardés sur le téléphone : écart vert / rouge, or pour un meilleur segment. Mario Kart :
  temps du jeu (somme des courses). Mode « à la main » aussi.
- Adresses tirées des tables de symboles publiques des projets de décompilation (zeldaret/tww, doldecomp/sms,
  doldecomp/mkdd) ; aucun code repris. Chaque valeur est vérifiée : une autre version du jeu n'affiche rien de faux.

## Nouveautés v311 (build 817, appli build 150) — File « à suivre », vagues nettes, notice sans clignotement
- **Console : les pages de la notice ne clignotent plus.** En tournant une page, la console préchargeait la feuille
  suivante en retirant de la mémoire une page encore affichée : elle était rechargée, retirée, rechargée… (éclairs
  blancs). Une page à l'écran n'est plus jamais retirée, et le dos d'une page caché n'est plus chargé pour rien.
- **Appli : vagues lisses, sans marches ni pixels.** Les rubans sont calculés pixel par pixel par la puce graphique du
  téléphone (Android 13 et plus ; avant : bandes posées côte à côte, visibles sur les grands écrans).
- Appli, onglet Lecture > À suivre : toucher un morceau de la file y **saute sans effacer la file** (avant : le morceau
  était lancé seul et la console refaisait sa liste : les morceaux ajoutés disparaissaient). GC Station :
  `/api/cmd?c=qplay&i=<place>`.
- « Lire juste après » / « Ajouter à la fin de la file » : l'appli attend la réponse de la console jusqu'à 10 s (avant
  2,5 s : « Pas possible » s'affichait alors que c'était fait, quand la console était occupée).

## Nouveautés v310 (build 814, appli build 147) — Notice sans déformation, vagues de la console dans l'appli
- **Console : la notice ne se déforme plus quand on la fait tourner.** Chaque page était un seul quadrilatère projeté :
  l'image se pliait le long de sa diagonale. Elle est maintenant découpée en 8 x 8 carreaux projetés un par un
  (comme les faces de la boîte 3D) : la page suit la perspective.
- **Appli : les rubans de lumière qui ondulent**, comme dans le fond de la console (mêmes hauteurs, ondulations et
  vitesses), rose → violet → cyan, derrière tout l'écran.

## Nouveautés v309 (build 812, appli build 145) — Pochette et spectre de la console dans l'appli
- Onglet Lecture de l'appli : la pochette s'affiche **comme sur la télé** (carrée, fin liseré clair, ombre portée, au
  lieu du cadre néon arrondi) et, juste dessous, **le spectre de la console** : style LED, cases empilées, crête et
  reflet, rose → violet → bleu, les vraies barres de la musique qui joue.
- GC Station : `/api/viz` donne les 55 barres du spectre (0..255) ; l'appli les lit ~20 fois par seconde et les lisse
  à chaque image de l'écran (120 Hz). Console pas à jour : pas de spectre, le reste ne change pas.

## Nouveautés appli build 142 — Un seul fond, du haut en bas de l'écran
- Le fond de l'appli est maintenant **un seul fond pour tout l'écran**, derrière l'en-tête, la barre d'onglets et les
  barres d'Android : l'aurore qui bouge et, sur l'onglet Lecture, la pochette du morceau floue qui dérive doucement
  (zoom et glissement lents, 22 s), avec un léger voile en haut et en bas. Avant, la pochette floue ne couvrait que le
  milieu : une bande plus sombre restait en bas, derrière les onglets.

## Nouveautés appli build 139 — Titre GC REMOTE
- Le titre en haut de l'appli (et sur l'écran de connexion) dit maintenant **GC REMOTE** ; oublié au changement de nom.

## Nouveautés v308 (build 810, appli build 137) — Photo du téléphone sur la télé : corrigé
- Photos (appli) : toucher une photo pour l'afficher sur la télé ne marchait pas quand l'**économiseur d'écran** (ou la
  veilleuse) de la console était lancé : la photo s'ouvrait derrière. Il est maintenant coupé, comme pour une photo
  envoyée. Si l'onglet PHOTOS est caché sur la console, l'appli le dit (avant : « Sur la télé » sans rien à l'écran).

## Nouveautés appli build 135 — Disque sorti : manipulation corrigée
- Boîte 3D de l'appli, disque sorti : lancé du doigt, il partait **dans l'autre sens** que le doigt (et trop vite :
  il semblait se retourner au hasard). Il tourne maintenant dans le sens du doigt, 540°/s au plus, et s'arrête dès
  qu'on le touche. Sorti, il se présente toujours de face, l'étiquette vers nous ; rangé, il revient sur l'ergot sans
  à-coup.

## Nouveautés appli build 133 — Titre et boutons au-dessus de la boîte zoomée
- Boîte 3D, notice 3D, étagère : le titre (en haut) et les consignes et boutons (en bas) sont posés sur un fond qui
  se fond dans l'écran ; la boîte zoomée passe dessous au lieu de les recouvrir, les textes restent lisibles.

## Nouveautés appli build 131 — Jaquettes HD sans IA (textes lisibles)
- Retour en arrière sur la build 129 : les jaquettes déjà en HD (plus de 600 px) restent **telles quelles**. Agrandies
  par IA, leurs petits textes (dos de la boîte, mentions légales) devenaient des gribouillis illisibles : le modèle
  redessine les lettres. L'IA reste pour les disques et les jaquettes en basse définition. Les versions HD agrandies
  déjà gardées sur le téléphone sont effacées à la prochaine ouverture de la boîte.

## Nouveautés appli build 129 — Jaquettes HD agrandies par IA aussi
- Boîte 3D : **toutes** les jaquettes passent maintenant par l'IA, HD comprise : la jaquette complète 1024 x 680 est
  agrandie par morceaux de 256 px (raccords invisibles) jusqu'à **4096 x 2720** ; les autres x4, 4096 px au plus.
  Une seule fois par jeu (la 1re ouverture de la boîte montre l'avancement, quelques dizaines de secondes), gardée en
  JPEG. La ligne du haut dit « agrandie par IA ». « Tout garder pour hors-ligne » ne lance pas l'IA des jaquettes
  (trop long pour toute la collection) ; l'étagère lit les versions agrandies déjà faites, réduites à 1024 px.

## Nouveautés appli build 126 — Mise à jour de l'appli : toujours la plus récente
- « Mettre à jour l'appli » prend la compilation au plus grand numéro, et non plus la première de la liste de GitHub
  (qui ne les range pas toujours dans l'ordre : l'appli 1.122 se croyait à jour alors que la 1.124 existait).

## Nouveautés appli build 124 — Jaquettes en basse définition agrandies par IA
- Boîte 3D : quand GameTDB n'a pas la jaquette en HD (seulement la jaquette complète normale, ~512 px, ou seulement la
  petite jaquette avant), elle est **agrandie par IA** comme les disques (Real-ESRGAN x4, puis 1024 px au plus),
  une seule fois par jeu (gardée : `jaquettes/coverfullia`, `jaquettes/coveria`). Les jaquettes déjà en HD ne
  changent pas. L'étagère 3D utilise la version agrandie dès qu'une boîte l'a faite (elle ne la calcule pas elle-même :
  trop long pour toute une étagère).

## Nouveautés appli build 122 — Le disque sorti ne tourne plus tout seul
- Boîte 3D de l'appli : le disque sorti de la boîte reste immobile, de face ; il ne tourne que si on le lance du doigt
  (et ralentit tout seul).

## Nouveautés appli build 120 — Télé du téléphone au bon format
- **Onglet Télé : l'image a la forme de la vraie télé.** Avant, elle prenait les proportions des pixels envoyés par la
  console (640 x 574 en PAL : image étirée en hauteur, presque carrée). Maintenant GC Remote lit le réglage de la
  console « Format de l'écran » (4:3 ou 16:9, START > Réglages) par le réseau, toutes les 15 s, et l'image remplit
  exactement ce format (avec ou sans upscaling, en plein écran avec la manette aussi). Rien à changer sur la console.
- La capture de l'écran du jeu suit le même réglage (4:3 ou 16:9).

## Nouveautés v307 (build 808, appli build 117) — Capture d'écran en plein jeu, valeurs bloquées en direct
- **Capture de l'écran du jeu** (avec le chercheur de codes activé) : un bouton dans « Partie en cours » et dans le
  chercheur de codes. Le jeu se met en pause une seconde, GC Remote lit l'image affichée dans la mémoire vidéo de la
  console (YUV 4:2:2, un ou deux champs selon le mode), la convertit et l'enregistre dans **Images > GC Remote** ;
  aperçu avec « Partager ». Aucun boîtier de capture.
- **Bloquer une valeur en direct** : sur une adresse trouvée, « Bloquer en direct » ; Pulse la réécrit à chaque image,
  tout de suite (sans revenir à GC Station), jusqu'à ce que tu la libères (✕) ou la fin de la partie. 8 au plus.
- Pulse : la réponse « GCMA » donne aussi les registres de la vidéo (où est l'image affichée, sa taille) et le nombre
  de valeurs bloquées ; nouvelle demande « GCMF » (bloquer, libérer, tout libérer). Banc d'essai `emu_mem.py` complété.

## Nouveautés appli build 115 — GC Remote partout dans l'appli
- Le nom **GC Remote** remplace GC Station partout où il s'agit de l'appli : titre du widget, lecture en cours
  (notification et commandes multimédia d'Android quand rien ne joue), dossiers du téléphone (**Images > GC Remote**
  pour les captures de la télé, **Téléchargements > GC Remote** pour les journaux), fichier de mise à jour.
  « GC Station » reste là où il s'agit de la console (réglages, retour avec A + Z + START, journaux, rapports).
- Le nom interne de l'appli (fr.gcstation.telecommande) ne change pas : sinon Android la prendrait pour une autre appli
  (réglages perdus, plus de mise à jour par-dessus).

## Nouveautés appli build 113 — L'appli s'appelle GC Remote
- **Nouveau nom : GC Remote** (sous l'icône, dans la liste des applis, le widget, la tuile des réglages rapides, le
  fond d'écran animé). La console garde son nom, GC Station : GC Station sur la télé, GC Remote dans la main.
- **Nouvelle icône** : le téléphone, le disque et les ondes (cyan, rose et violet sur bleu nuit), en icône adaptative
  Android (formes de chaque téléphone, et version monochrome pour les icônes à thème d'Android 13+).

## Nouveautés v306 (build 806, appli build 111) — Chercheur de codes : tes propres cheats et succès
- **Réglage de la console** : START > Réglages > *Chercheur de codes (appli)* = **Oui** (Broadband Adapter branché).
  Pendant une partie lancée avec Pulse, l'appli peut alors **lire et écrire la mémoire du jeu** par le réseau.
- **Appli : Plus > Chercheur de codes** (ou le bouton de l'écran « Partie en cours ») :
  - **recherche** sur 8, 16 ou 32 bits : valeur connue (ex. 3 vies) ou inconnue (barre d'énergie), puis comparaisons
    entre deux « photos » de la mémoire : *égal à*, *plus grand*, *plus petit*, *a changé*, *pas changé*,
    *augmenté de*, *diminué de*, jusqu'à quelques adresses (valeurs en direct sous 40 adresses) ;
  - pendant une photo (24 Mo), **le jeu est mis en pause** (image figée, son coupé), puis repart tout seul (au plus
    tard 20 s sans nouvelles du téléphone) ;
  - une adresse trouvée : **changer sa valeur** tout de suite, **créer un cheat** (valeur bloquée, code Action Replay
    / Gecko 00/02/04) ou **créer un succès** (Titre | Description | m8/m16/m32(adresse) >= valeur) ; gardés sur le
    téléphone et **envoyés à GC Station au retour du jeu** (A + Z + START) ;
  - **succès obtenus en direct** : notification sur le téléphone pendant la partie (avec son titre).
- Pulse (résident) : paquets UDP « GCM? » sur le port des manettes du téléphone (51237) : état (`I`), pause (`P`),
  écriture (`W`), lecture par blocs de 1 Ko (`R` : mots nuls pas envoyés, 64 blocs par demande en pause, 2 sinon),
  annonce des succès (« GCAC ») ; section « PPAD » et BBA aussi sans manette du téléphone (`NRO_MEM`). Banc d'essai :
  `tools/natif-test/emu_mem.py` (émulateur PowerPC, faux Broadband Adapter).
- GC Station : `/api/succesadd` (ligne vérifiée puis ajoutée à `SD:/gc-mp3/succes/<ID>.txt`) ; le lancement d'un jeu
  dit au téléphone si le chercheur est actif. Appli : les titres des sections (en majuscules) se traduisent aussi en
  anglais.

## Nouveautés appli build 107 — L'appli en anglais
- **Plus > Apparence > Langue** : *Langue du téléphone* (par défaut : anglais si le téléphone n'est pas en français),
  *Français* ou *English*. Tout change tout de suite (l'appli se recharge pour les anciens écrans).
- Toute l'appli : onglets, fiches de jeux, étagère, notice, télé, manette, jeu de cartes, globe des radios, fichiers,
  cartes mémoire, copies, outils, notifications, messages. Les noms de jeux, morceaux et fichiers ne changent pas.
- Fonctionnement (`Lang.kt`) : les textes restent écrits en français dans le code ; en anglais, chaque texte affiché
  passe par `Lang.t`, qui cherche dans `assets/en.tsv` (≈ 780 textes traduits à la main) : le texte entier, puis un
  modèle avec des valeurs (« Lire la notice (12 pages) »), puis les morceaux connus (textes composés à l'exécution).
  Le `Text` de Compose est remplacé par celui du paquet (même signature), les toasts passent par `Lang.toast`, les
  vues Java par `Lang.t` (setText, setHint, notifications). Les textes venus de la console restent en français.

## Nouveautés appli build 105 — Traduction en français dans l'appli
- Fiche du jeu, « À propos » : un **résumé dans une autre langue** (GameTDB n'a pas toujours le français) est **traduit en
  français** sur le téléphone (Google ML Kit, hors ligne : le texte ne part pas sur Internet). « Traduit de l'anglais ·
  voir l'original » passe de l'un à l'autre. La première fois, le téléphone télécharge la langue (environ 30 Mo) ;
  ensuite tout est instantané et les traductions sont gardées.

## Nouveautés appli build 103 — Thèmes, animations, fiche enrichie
- **Plus > Apparence** : l'**ambiance** de toute l'appli (Néon d'origine, Sombre sobre, GameCube indigo, ou **Couleurs du
  jeu** : tirées de la jaquette du jeu en cours, sinon de la dernière fiche ouverte), la **taille du texte** (Petit à Très
  grand), les **vibrations** des boutons et le **120 Hz**. Tout change tout de suite et est gardé.
- **Animations** : l'appli demande au téléphone sa fréquence la plus haute (**120 Hz**) ; les onglets glissent dans le
  sens choisi avec un léger ressort ; dans la fiche, la jaquette arrive en s'agrandissant avec un rebond ; le résumé
  se déplie en douceur.
- **Fiche du jeu enrichie** : « À propos » d'après GameTDB : genre, nombre de joueurs, classification (PEGI), éditeur,
  développeur, année de sortie et résumé (« Lire la suite »).

## Nouveautés v305 (build 804) — Le disque en main sur la console
- Boîte ouverte, **↓** sort le disque : il vient **au milieu de l'écran, en grand**, sur un voile sombre (par-dessus
  les textes de la fiche, même boîte zoomée), et on le **manipule** : **stick** : le tourner dans tous les sens (de
  côté on voit sa tranche ; retourné : la **face gravée**, argentée avec l'arc-en-ciel qui change selon l'angle) ;
  **C haut / bas** : zoom ; **C gauche / droite** : le lancer en rotation (il ralentit tout seul) ; **Y** : de face ;
  **B** ou **↓** : le ranger ; **A** : jouer. Pendant ce temps le stick ne bouge plus la flèche ni la boîte.

## Nouveautés appli build 100 — Disque agrandi par IA, jaquettes depuis GameTDB
- **Image du disque bien plus nette** : GameTDB ne la donne qu'en ~160 px ; elle est maintenant agrandie **par IA** sur
  le téléphone (Real-ESRGAN « realesr-general-x4v3 », réseau compact, licence BSD-3, exécuté par ONNX Runtime) : 4 fois
  plus grande, contours, logos et textes redessinés nets, puis 1024 px ; une seule fois par jeu (gardée). Si l'IA ne
  marche pas, l'agrandissement d'avant prend le relais. (Modèle converti au format ONNX sans changer ses poids ; l'appli
  ne garde que les versions pour téléphones ARM.)
- **Jaquettes de l'appli depuis GameTDB** : les vignettes (onglet Jeux, mur, fiche) sont téléchargées par le téléphone
  sur GameTDB, en HD (sinon normales), quatre à la fois, et gardées ; celle de la console ne sert plus que si GameTDB
  n'a pas le jeu ou sans Internet.

## Nouveautés v304 (build 802, appli build 98) — Sortir le disque sur la console, fond d'écran à 120 Hz, étagère sans intercalaires
- **Console : sortir le disque** : fiche du jeu, boîte ouverte, **↓** : le disque se soulève du bac, bascule, vient devant
  en grand et tourne (avec un reflet) ; **↓** encore : il retourne dans le bac ; fermer la boîte (↑) le range aussi.
  La barre d'aide dit « Sortir le disque » / « Ranger le disque ».
- **Appli : fond d'écran animé fluide** : dessiné par la puce graphique (Canvas matériel, jaquettes en mémoire
  graphique), à chaque rafraîchissement de l'écran au lieu de 30 fois par seconde, en demandant **120 Hz** ; plus rien
  n'est recréé à chaque image.
- **Appli : étagère** : plus d'intercalaires violets ; le rayon de la boîte du milieu reste écrit en haut.

## Nouveautés v303 (build 800, appli build 96) — Télé tactile et clavier du téléphone partout
- **Télé tactile** (appli, onglet Télé, puce « Tactile », activée) : toucher l'image de la télé **clique à cet endroit**
  sur la console : la flèche de GC Station y va et choisit ce qui est dessous (onglet, ligne, bouton) ; dans Cube
  Browser, le pointeur y saute et clique. Appui long : retour (B) ; glisser : la flèche suit le doigt. Toucher là où
  rien n'est cliquable ne fait rien (la barre des onglets qui apparaît en haut est prise dès qu'elle est là).
  Console : `/api/toucher?u=&v=&c=` (u, v : 0 à 1 sur l'image ; c : 0 déplacer, 1 cliquer, 2 retour), 16:9 compris.
- **Clavier du téléphone partout** (appli) : dès qu'un champ de texte s'ouvre sur la télé, un panneau monte en bas de
  l'appli, quel que soit l'onglet, et le clavier du téléphone s'ouvre tout seul ; le texte part en direct, OK (ou
  Entrée) valide, le micro dicte. « Ouvrir tout seul » se coupe dans le panneau ; × le cache jusqu'au champ suivant.
  L'appli regarde la console plus souvent pendant qu'un clavier est ouvert.

## Nouveautés v302 (build 797, appli build 94) — Notice en 3D sur la console, étagère triée, mur de jaquettes, fond d'écran animé
- **Console : la notice est un vrai livret en 3D** (fiche du jeu, boîte ouverte, X), qu'on manipule fermée comme
  ouverte : **stick** : la tourner dans tous les sens ; **C** : zoom (zoomée, le stick la déplace) ; **gauche / droite** :
  tourner une feuille (elle se courbe autour de la reliure) ; **A** : ouvrir / refermer ; **Y** : de face ; **B** : la
  ranger. Ouverte : deux pages face à face ; vue de derrière : la couverture et le dos. Nombre impair de pages : la
  dernière est le dos de la notice (aussi dans l'appli). Pages un peu moins grandes en mémoire (4 lues à la fois).
- **Appli : étagère triée** par Nom, Éditeur, Année ou Temps de jeu (le choix est gardé), avec un **intercalaire** à
  chaque rayon (lettre, éditeur, année, temps de jeu) qui dépasse des boîtes, son nom sur la tranche. Éditeur et année :
  la base GameTDB de la console (`fiches-gc.txt`), téléchargée par le téléphone et gardée 30 jours. Sous la boîte du
  milieu : éditeur, année, genre.
- **Appli : mur de jaquettes** (onglet Jeux) : les jaquettes en grand, 3 par ligne, qui s'inclinent quand on bouge le
  téléphone, avec un reflet qui glisse ; toucher : la fiche.
- **Appli : fond d'écran animé** « GC Station : ma collection » (bouton « En fond d'écran » du mur) : les jaquettes
  gardées sur le téléphone défilent en 3D sur quatre rangées, parallaxe entre les écrans d'accueil.

## Nouveautés appli build 92 — Étagère : plus de boîtes noires, plus grande
- Les boîtes loin du milieu restaient noires (jaquettes chargées seulement 9 de chaque côté, alors qu'on en voyait une
  vingtaine) : maintenant tout ce qui se voit est chargé, avec de la marge (jaquettes réduites pour l'étagère).
- L'étagère est plus grande : une dizaine de tranches en largeur au lieu d'une vingtaine de petites. La boîte sortie
  avance assez pour ne plus rester coincée entre ses voisines.

## Nouveautés appli build 90 — Étagère de collection et intérieur du bac refait
- **Étagère 3D** (onglet Jeux, « Étagère 3D ») : toutes les boîtes des jeux affichés (recherche et filtre compris),
  rangées côte à côte la tranche vers toi sur une planche. Glisser : parcourir (avec élan, la boîte la plus proche se
  cale au milieu) ; toucher une boîte (ou « Sortir la boîte ») : elle sort de l'étagère et se tourne, jaquette avant
  vers toi ; la retoucher (ou « Ouvrir ») : la boîte 3D s'ouvre ; « Fiche du jeu » ; toucher ailleurs : elle se range.
  Deux doigts : zoomer. Jaquettes chargées au fur et à mesure (téléphone, sinon GameTDB).
- **Intérieur du bac**, d'après la photo : hexagone pointes en haut et en bas avec ses quatre pans clairs inclinés,
  anneau en relief où se pose le disque, fin cercle intérieur, fente du centre avec ses deux barres et le bouton en
  « S » (PUSH HERE) sur sa base claire, cadre « MEMORY CARD STORAGE » ouvert en bas avec ses deux languettes, et les deux
  dessins en creux ✓ / ✗ en bas à droite.

## Nouveautés v301 (build 795, appli build 88) — Jaquettes et disques envoyés par le téléphone
- **Appli** : onglet Jeux, **« Envoyer jaquettes et disques à la console »** : le téléphone télécharge sur GameTDB ce
  qui manque à la GameCube (jaquette de la liste, jaquette complète de la boîte, en option la HD pour le zoom, image du
  disque) et le lui envoie, jeu par jeu, avec l'avancement et un bouton Arrêter. Ce que la console a déjà n'est pas
  renvoyé. Les images téléchargées restent aussi sur le téléphone (boîte 3D, hors-ligne).
- **Console** : `/api/jaqliste` (pour chaque jeu : jaquette, complète, HD, disque déjà là ou non) ; réception
  `PUT /upload?dir=jaquette&nid=<ID>&kind=c|f|h|d` rangée en PNG dans `SD:/gc-mp3/jaquettes/{ajoutees,completes,disques}/`
  (l'ancienne image convertie est enlevée), convertie à la première vue comme une image déposée à la main ; la liste des
  jeux et la boîte ouverte la prennent tout de suite.

## Nouveautés appli build 86 — Copies des cartes mémoire à la main
- La **copie automatique** (une fois par jour à la connexion : cartes mémoire virtuelles et réglages copiés sur le
  téléphone) est maintenant **désactivée par défaut**, y compris si elle l'était avant sans avoir été choisie. Les copies
  se font à la main : Plus > Copies > « Faire une copie maintenant ». L'interrupteur « Copie automatique » la remet.

## Nouveautés appli build 84 — La notice en 3D, disques nets et script de notice
- **La notice en 3D** (« Lire la notice », dans la boîte ou la fiche du jeu) : un vrai livret, feuille par feuille, qu'on
  manipule **fermé comme ouvert** : un doigt le fait tourner dans tous les sens (avec élan), deux doigts zooment où l'on
  veut et déplacent. Toucher à droite / à gauche (ou ◀ ▶) tourne une page : la feuille pivote sur la reliure en se
  courbant (couverture et dos plus rigides) ; la tranche montre l'épaisseur. Double toucher : revenir de face (ou
  ouvrir / refermer). « À plat » : l'ancienne lecture page par page.
- **Disques plus nets** : l'image du disque de GameTDB (environ 160 px) est agrandie proprement à 1024 px sur le
  téléphone (bicubique puis passes de netteté bornées : contours raides, texte lisible, sans halo), une seule fois par
  jeu (gardée dans `jaquettes/disc1024/`). Le bord du disque, l'anneau transparent et le trou du centre sont redessinés nets.
- **Script de notice** (`android/outils/notice.py`) : à partir d'un PDF, de pages scannées ou de **captures d'écran** du
  site de modes d'emploi, il retrouve la page dans chaque capture (barres du téléphone et du navigateur, fond du site
  enlevés), retire les captures en double, coupe les doubles pages, découpe les planches d'impression, et range tout dans
  `notice-<ID>.zip` (`notices/<ID>/01.jpg`…, au format de l'appli) à installer depuis TÉLÉCHARGEMENTS. `--apercu` :
  planche-contact pour vérifier ; `--garder 1,3-9` : choisir les pages ; `--haut`/`--bas` : rogner des barres collées.

## Nouveautés appli build 80 — Bandeau « hors-ligne » sur une ligne
- Quand la console ne répond pas, le bandeau du haut tient sur **une seule ligne** (« Hors-ligne · tes jeux gardés restent
  là ») avec trois petits boutons : chercher la console, manette, masquer. Il revient à la prochaine coupure.

## Nouveautés appli build 79 — L'appli hors-ligne (sans la console)
- **« Utiliser hors-ligne »** sur l'écran de connexion : l'appli s'ouvre sur **Jeux** sans la console. Tes jeux (temps de
  jeu, favoris, pastilles), jaquettes, boîtes 3D, disques et notices gardés sur le téléphone restent disponibles.
- La liste des jeux est gardée à chaque connexion ; hors-ligne, l'écran Jeux dit de quand elle date.
- **« Tout garder pour hors-ligne »** (Jeux, avec la console) : télécharge d'un coup, pour tous tes jeux, la jaquette, la
  boîte 3D et le disque (GameTDB) et toutes les pages des notices ; avancement et bouton Arrêter.
- Tout est rangé dans la mémoire de l'appli (plus dans le cache, qu'Android peut vider) ; ce qui était déjà gardé y est
  déplacé tout seul.

## Nouveautés v300 (build 792, appli build 78) — La notice s'ouvre et ses pages tournent
- **Console** : X (boîte ouverte) : la notice **sort du couvercle**, de biais, et vient se poser à plat au milieu ; B : elle
  y retourne. Gauche / droite : la page **tourne comme dans un vrai livret** (elle se soulève par la droite en pivotant
  sur la reliure, avec un reflet et l'ombre sur la page de dessous) ; la page suivante est lue d'avance.
- **Appli** : même chose dans « Lire la notice » : arrivée de biais qui se pose à plat, et pages qui tournent sur la
  reliure en suivant le doigt (la page de dessous s'assombrit).

## Nouveautés v299 (build 791) — Un .zip de notice remplace l'ancienne
- Installer un .zip de notice (TÉLÉCHARGEMENTS, A) efface d'abord l'ancienne notice du même jeu : plus de pages en trop
  quand la nouvelle en a moins.

## Nouveautés appli build 77 — Notice : planches reconnues toutes seules
- « Ajouter une notice » : plus d'option « Planches d'impression ». L'appli reconnaît seule une planche (grandes marges
  blanches, 2 doubles pages) et la découpe ; une page seule est gardée telle quelle. Avant, des pages déjà découpées
  avec l'option activée étaient recoupées en 4 (environ 140 fichiers envoyés au lieu de 38).

## Nouveautés v298 (build 790, appli build 76) — Ajouter une notice depuis l'appli
- **Appli** : dans la fiche d'un jeu, **« Ajouter une notice… »** (ou « Remplacer la notice… ») : choisis un **PDF** ou les
  **images des pages** (rangées dans l'ordre de leurs noms). Option **« Planches d'impression »** : des images avec
  2 doubles pages et des traits de coupe sont découpées et remises dans l'ordre du livret (couverture en premier,
  dos en dernier). Les pages sont mises au bon format (JPEG 480 × 816) et envoyées à la console, avec l'avancement.
- **Console** : reçoit les pages directement dans `SD:/gc-mp3/notices/<ID>/` (envoi `dir=notice&nid=<ID>`) ;
  `/api/notice?id=<ID>&del=1` efface l'ancienne notice, `&fin=1` la fait relire.

## Nouveautés v297 (build 789) — Installer une notice depuis le téléphone
- Un **.zip de notice** (dossier `notices/<ID>/` dedans, comme `notice-GYQE01.zip`) envoyé depuis le téléphone : dans
  **TÉLÉCHARGEMENTS**, **A** l'installe directement dans `SD:/gc-mp3/notices/` (avant : décompressé dans `SD:/downloads`,
  et la console ne trouvait pas la notice). La fiche ouverte la voit tout de suite.

## Nouveautés appli build 75 — La notice dans la boîte 3D de l'appli
- Dans la **boîte 3D de l'appli**, la notice du jeu (si la console en a une, `SD:/gc-mp3/notices/<ID>/`) est posée au fond
  du couvercle, sous les attaches, comme dans le vrai boîtier : on voit sa couverture en ouvrant la boîte.
- Boîte ouverte : bouton **« Lire la notice »** (pages, zoom à deux doigts).

## Nouveautés v296 (build 788, appli build 74) — La notice du jeu dans la boîte
- **Console** : la notice se range dans le **couvercle de la boîte 3D** (tenue par les clips). Boîte ouverte, **X** :
  la notice en grand ; gauche / droite : pages ; stick C : zoom ; stick : se déplacer ; B : refermer. Pages en JPEG sur la
  carte : `SD:/gc-mp3/notices/<ID>/01.jpg` (01 = couverture), `02.jpg`… Lues et décodées en fond (`source/notice.c`).
- **Appli** : dans la fiche d'un jeu, **« Lire la notice »** quand la console en a une : glisser pour tourner les pages,
  deux doigts pour zoomer, double toucher. Les pages sont gardées sur le téléphone ensuite.
- Pour l'appli : `/api/notice?id=<ID>` (nombre de pages) et `&p=<n>` (la page).

## Nouveautés appli build 73 — L'intérieur de la boîte 3D à l'identique

- **Bac** : plateau hexagonal à peine en relief, bordé d'un fin rebord ; anneau fin où se pose le disque ; 8 nervures sombres autour ; au centre, le vrai motif blanc (deux barres et un « S ») ; en bas, le cadre fin de l'attache avec ses deux languettes claires.
- **Couvercle** : les attaches de la notice sont de vrais crochets courbes qui partent du bord ; encoches noires dans le rebord et fentes près de la charnière.

## Nouveautés appli build 71 — L'intérieur de la boîte 3D comme le vrai
- Intérieur **gris moyen** (plus noir), plastique mat ; **grand plateau hexagonal** en relief (pointes à gauche et à
  droite) avec son **berceau** rond et ses encoches ; **ergot central clair** en « H » avec ses dents ;
  **crochets de la notice** en plastique clair en haut et en bas du couvercle ; petite attache rectangulaire en bas
  près de la charnière ; fentes, comme sur un vrai boîtier GameCube.

## Nouveautés appli build 69 — Drapeau du pays des jeux ; le disque sorti tourne seul
- **Jeux** : le **drapeau du pays** du jeu (d'après la 4e lettre de son code : P Europe, E États-Unis, J Japon,
  F France, D Allemagne…) en bas à gauche de la jaquette, à côté du code, et dans la fiche avec le nom du pays.
- **Boîte 3D** : quand le disque est sorti, le lancer du doigt ne fait plus tourner la boîte derrière (le disque
  garde son élan, la boîte reste en place).

## Nouveautés v295 (build 787, appli build 67) — Télé sur le téléphone : moins de saccades
- **Console : l'image suivante préparée d'avance** : dès qu'une image part vers le téléphone, la console prépare la
  suivante (capture et compression) pendant que le téléphone la reçoit ; à la demande suivante, elle part tout de
  suite. Le téléphone n'attend plus la console entre deux images : plus d'images par seconde.
- **Appli : arrivées lissées** : les images de la console arrivent à intervalles irréguliers ; l'appli les affiche
  avec un léger retard régulier (environ 1,3 intervalle) et calcule les images intermédiaires entre les deux qui
  encadrent l'instant affiché : plus de sauts quand une image arrive en avance ou en retard (mode **Fluide 120 Hz**).
- Le badge de l'image indique aussi le temps de préparation par la console et la taille reçue (diagnostic).

## Nouveautés appli build 65 — Télé en plein écran : bonnes proportions
- En plein écran (paysage), l'image de la télé était écrasée en largeur (étirée sur toute la hauteur) : elle est
  maintenant toujours en 4/3, et l'image agrandie garde ses proportions quoi qu'il arrive.

## Nouveautés v294 (build 785, appli build 63) — La télé sur le téléphone : plus fluide, plus nette, 120 Hz
- **Console : seulement ce qui change** (`/api/ecranz`) : la console garde ce que le téléphone a déjà et n'envoie que
  les blocs de 16 × 16 qui ont changé (souvent 2 à 5 fois moins lourd) ; rien de changé : rien d'envoyé. Plus
  d'images par seconde, surtout en **Net** (640 × 480).
- **Appli : trois qualités** sous l'image : **Rapide** (320 × 240), **Net** (640 × 480), **Max** (640 × 480,
  compression très légère).
- **Upscaling qui suit les contours** : l'agrandissement regarde la direction des bords (noyau de Lanczos étiré le
  long des contours, resserré en travers) : textes et lignes nets sans escaliers, aplats doux, pas de halo.
- **Fluide 120 Hz** : le téléphone calcule les images intermédiaires entre deux images de la console, en suivant ce
  qui bouge (mouvement estimé par blocs) : les défilements sont fluides à 120 images par seconde. L'image a une
  image de retard (environ 50 ms) ; bouton **Fluide 120 Hz** pour l'enlever.

## Nouveautés appli build 61 — Zoom libre sur la boîte 3D, upscaling de l'image de la télé
- **Boîte 3D** : deux doigts pour **zoomer à l'endroit pincé** (jusqu'à 6 fois) et **déplacer la vue dans tous les
  sens** ; un doigt la tourne toujours ; double toucher quand elle est zoomée : retour au cadrage normal.
- **Télé : upscaling** (Android 13 et plus) : l'image reçue de la console est agrandie par la puce graphique du
  téléphone (interpolation bicubique Catmull-Rom sur 16 points, netteté sans halo, blocs du JPEG adoucis dans les
  aplats) au lieu du simple lissage. Bouton **Upscaling** sous l'image (allumé par défaut).

## Nouveautés appli build 59 — Disque réaliste et boîte encore plus vraie
- **Le disque** : la face gravée est un **miroir argenté** avec les **reflets arc-en-ciel** des sillons, qui changent
  quand il tourne (calculés comme la vraie diffraction d'un DVD) ; le **centre en plastique transparent** (on voit
  l'ergot à travers), l'anneau d'empilage, la bande miroir avec son texte gravé, le bord transparent. L'autre face :
  l'image du jeu, imprimée et brillante.
- **La boîte** : reflets de l'environnement (lumières de studio) sur le plastique noir brillant et sur le film de la
  jaquette ; intérieur en plastique mat et grenu ; berceau circulaire autour du disque ; ergot en anneau ; coins plus
  ronds ; disque plus fin (256 segments).

## Nouveautés appli build 57 — Boîte 3D comme la vraie, avec le disque du jeu
- La boîte 3D du téléphone ressemble au **vrai boîtier GameCube** : plastique noir à coins arrondis, jaquette sous
  le film transparent (reflets), tranche, dos ; dedans, le **support hexagonal** et son **ergot**, les attaches de la
  notice dans le couvercle. Il **s'ouvre à plat**, comme un livre.
- Le **disque du jeu** est posé dedans avec sa **vraie image** (disque de GameTDB, téléchargé par le téléphone) ;
  **Sortir le disque** : il vient devant toi et tourne ; glisse pour le faire tourner.
- Navigation plus simple : glisser la tourne, et **lâchée, elle s'arrête sur la face la plus proche** (avant,
  tranche, dos) ; boutons **Avant / Tranche / Dos** ; ouverte, elle se met bien de face.

## Nouveautés appli build 55 — La boîte du jeu en 3D sur le téléphone
- Dans la fiche d'un jeu : touche la jaquette (badge **3D**) ou **Voir la boîte en 3D** : la boîte GameCube en vraie
  3D, en plein écran. **Glisser** : la tourner (elle continue sur sa lancée) ; **deux doigts** : zoomer ;
  **double toucher** ou **Ouvrir le boîtier** : le couvercle s'ouvre comme un livre, le disque est dedans ; **Jouer**.
- Qualité maximale : jaquette complète **HQ** de GameTDB (dos, tranche, avant) téléchargée par le téléphone et
  gardée dans l'appli, lissage 4x, filtrage anisotrope, lumière et reflets du plastique, ombre, à la fréquence de
  l'écran (120 Hz). Sans jaquette complète : la jaquette avant HQ (ou celle de la console).

## Nouveautés appli build 53 — Télé : l'image seulement quand tu la demandes
- L'onglet **Télé** s'ouvre **sans image** : la console n'envoie rien tant que tu n'as pas touché **Afficher la télé**.
  Le bouton du dessous l'arrête à nouveau ; changer d'onglet ou quitter l'appli l'arrête aussi.

## Nouveautés appli build 51 — Nouvelle appli : tout refait, et la télé sur le téléphone
- **L'appli est refaite** (Kotlin + Jetpack Compose) : nouveau look, animations, onglets **Lecture, Musique, Jeux,
  Télé, Plus**. Elle s'installe par-dessus l'ancienne : réglages, adresse de la console et jeton GitHub gardés.
- **Lecture** : grande pochette sur fond flou, paroles synchronisées (toucher : toutes les paroles), commandes, volume.
- **Musique** : morceaux, radios (et le globe) et file « À suivre » au même endroit ; appui long : lire juste après.
- **Jeux** : grille de jaquettes, filtres **Favoris** et **Déjà joués**, fiche complète (lancer avec Pulse, patchs de
  secours, cheats, pastille, réglages du jeu, sauvegardes, raccourci sur l'écran d'accueil).
- **Télé (nouveau)** : ce qu'affiche la télé, en direct sur le téléphone (console build 774 ou plus) ; toucher = A,
  appui long = B, glisser = la croix, pointeur sur l'onglet WEB ; manette complète et clavier dessous.
  **Plein écran** en paysage : l'image sur toute la hauteur, les touches de chaque côté.
- **Plus** : Fichiers, Photos, Réglages, Cartes mémoire, Recherche, Salon, Outils, Copies, Succès, Jeu de cartes,
  Mini-jeux, Manette en jeu, Globe, Mettre à jour. Réglages, Cartes mémoire, Salon, Outils, Recherche et Copies
  gardent pour l'instant l'ancien look.

## Nouveautés v293 (build 783) — « Toutes les jaquettes » retiré de la console
- Le réglage « Toutes les jaquettes (boîte 3D, HD) » est **retiré** (trop lent sur la console) : pour tout avoir d'un coup,
  télécharger les images sur un téléphone ou un PC (script Termux) et les copier dans `jaquettes/completes/` et
  `jaquettes/disques/`. La console continue de télécharger **jeu par jeu**, quand tu ouvres une fiche.

## Nouveautés v292 (build 782) — Jaquettes : tour des bugs
- **Un seul téléchargement d'images à la fois** (jaquettes, complètes, HD, disques, fiches, « toutes les jaquettes ») :
  avant, deux pouvaient partir en même temps, se ralentir, et l'un pouvait écraser l'image que l'autre convertissait
  (jaquette fausse ou perdue).
- **Plus de jaquettes perdues pour rien** : une image qui n'a pas pu être convertie (souvent faute de mémoire sur le
  moment) n'est plus notée « absente » pour toujours : elle est refaite plus tard.
- Une jaquette téléchargée qui n'a pas pu être lue une fois sur la carte ne disparaît plus jusqu'au redémarrage :
  3 nouveaux essais, comme pour celles du pack.
- « Toutes les jaquettes » vérifie moins de fichiers par image (moins d'à-coups dans les menus).

## Nouveautés v291 (build 781) — « Toutes les jaquettes » plus rapide
- Le téléchargement de toutes les jaquettes lit le réseau 5 fois plus vite (64 Ko par image au lieu de 12) : il ne tourne
  que quand tu ne touches pas à la manette, il n'y a donc pas d'animation à protéger.

## Nouveautés v290 (build 780) — Boîte 3D comme un vrai boîtier, avec le disque du jeu
- **Aspect d'un vrai boîtier GameCube** : plastique noir dehors ; dedans, les **clips de la notice** dans le couvercle,
  le **support hexagonal** du disque, la **place de la carte mémoire**, les rebords. L'ouverture reste comme avant (↑).
- **Le vrai disque du jeu** dans la boîte ouverte : image du disque de GameTDB (`jaquettes/disques/<ID>.tex`, ou un
  `<ID>.png` déposé à la main). Sans image : un disque dessiné avec le nom du jeu.
- « Toutes les jaquettes » prend aussi les disques. Correction : le réglage affichait « En cours : 0 / 0 » quand la
  liste des jeux n'avait pas encore été lue (onglet JEUX pas encore ouvert) : elle est lue tout de suite.

## Nouveautés v289 (build 779) — Toutes les jaquettes d'un coup
- **START > réglages > Jeux > « Toutes les jaquettes (boîte 3D, HD) »** : télécharge en fond, jeu par jeu, la jaquette
  complète et la jaquette HD de **tous tes jeux** (GameTDB), dans `jaquettes/completes/`. Seulement quand tu ne touches
  pas à la manette depuis 1,5 s ; avancement affiché (« En cours : 12 / 40 ») ; A pour arrêter. Les jeux déjà faits
  (ou que GameTDB n'a pas) sont sautés. Ensuite, plus aucun chargement en ouvrant une fiche.
- **Boîte zoomée** : la barre des commandes est maintenant opaque, toujours lisible par-dessus la boîte.
  « Stick C : tourner / zoomer » reste affiché sous la boîte, même ouverte.

## Nouveautés v288 (build 778) — Moins de saccades pendant le chargement HD
- Les téléchargements en fond (jaquettes, jaquettes complètes et HD, fiches) lisent le réseau **en douceur** : au plus
  12 Ko par image (avant : jusqu'à 256 Ko). Le réseau prend beaucoup moins le processeur : la boîte tourne sans à-coups.
- Pendant le chargement HD : **pourcentage** affiché, puis « préparation ».

## Nouveautés v287 (build 776) — Lire la boîte zoomée (jaquette en HD)
- Quand tu **zoomes** sur la boîte 3D, la console prend la **jaquette complète en haute définition** de GameTDB
  (1024 × 680) : les textes du dos deviennent **lisibles**. « Chargement de la jaquette en HD... » pendant qu'elle arrive.
- Rangée compressée comme le pack (340 Ko) dans `completes/<ID>-hd.tex` ; tu peux aussi déposer un `<ID>-hd.png`.
- La PNG est décompressée **en flux** (`source/tinfl.c`, de miniz) : jamais l'image entière en mémoire.
- Zoomée, la boîte ne se remet plus à tourner toute seule.

## Nouveautés v286 (build 775) — Plus fluide pendant les téléchargements de jaquettes
- La transformation des images téléchargées (jaquettes, jaquettes complètes) se fait maintenant **en fond** : l'écran
  ne saute plus quand une jaquette arrive. La vérification « jaquette complète déjà sur la carte ? » aussi.
- Les téléchargements (jaquettes, fiches) **attendent que tu ne touches plus à la manette** depuis 1,5 s : rien ne se
  télécharge pendant que tu passes de jeu en jeu.
- La jaquette complète n'est téléchargée que si tu **restes au moins 1 s** sur la fiche du jeu.
- Les images de GameTDB passent par http (plus de chiffrement à calculer pour la console : connexion plus légère).

## Nouveautés v285 (build 774) — La télé sur le téléphone (second écran)
- La console peut maintenant **envoyer ce qu'elle affiche** au téléphone, en direct : `GET /api/ecran?w=320|640&q=10..95`
  rend une image **JPEG** de la télé (320×240 rapide, ou 640×480 net).
- Encodeur JPEG écrit pour GC Station (`source/ecran_jpeg.c`) : l'image est lue directement dans le tampon vidéo,
  sans conversion ni grosse réservation de mémoire ; le travail se fait sur le fil du serveur du téléphone, en
  dessous de l'interface (pas de saccade sur la télé).
- Pour l'appli **GC Station 2** (dépôt privé, onglet Télé) : la télé sur le téléphone, toucher = A, appui long = B,
  glisser = la croix, pointeur sur l'onglet WEB. Seulement dans les menus de GC Station (pas pendant un jeu).
- Simulateur : les tests automatiques demandent aussi l'image de la télé (petite et nette) et vérifient qu'elle se lit.

## Nouveautés v284 (build 771) — Zoom sur la boîte 3D
- **Stick C haut / bas : zoom** sur la boîte (jusqu'à 3 fois) : elle passe au milieu de l'écran, par-dessus la fiche,
  pour lire le dos. **Stick C gauche / droite** : tourner. **Stick** : déplacer la boîte zoomée. **↑** : ouvrir / fermer.
  **A** : jouer. **B** : revenir à la fiche.
- Le stick C ne penche plus la boîte (il zoome à la place).

## Nouveautés v283 (build 770) — Boîte 3D : les vraies jaquettes devant, derrière et sur la tranche
- La boîte 3D de la fiche prend maintenant la **jaquette complète** du jeu sur GameTDB (dos | tranche | avant) :
  le **dos** et la **tranche** sont les vrais, comme le boîtier du commerce.
- Téléchargée **seulement pour le jeu dont tu ouvres la fiche** (avec le réseau), une seule fois, rangée toute prête
  dans `SD:/gc-mp3/jaquettes/completes/<ID>.tex`. Tu peux aussi y déposer toi-même un `<ID>.png` (ou `.jpg`) : une image
  à plat, dos à gauche, tranche au milieu, avant à droite. Les jeux que GameTDB n'a pas sont notés dans `absentes.txt`.
- Sans jaquette complète : la boîte reste comme avant (jaquette devant, résumé au dos).

## Nouveautés v282 (build 769) — La boîte du jeu en 3D
- **Dans la fiche d'un jeu** (vitrine), la jaquette devient une **vraie boîte GameCube en 3D**, aux proportions des
  jaquettes (3 × 4) : la jaquette devant, la tranche, le dessus. Elle se balance doucement toute seule.
- **Stick C** : tourner la boîte dans tous les sens. **Le dos** affiche le titre, la capture du jeu et le résumé.
- **↑** : la boîte **s'ouvre** (le couvercle pivote sur la tranche) et le **disque** apparaît dedans ; **A** : jouer ;
  **↓** ou **↑** : refermer. Sans jaquette : une boîte violette avec la bannière, l'ID et le nom.
- Code : `source/box3d.c` (projection faite par le processeur, comme le globe).

## Nouveautés v281 (build 768) — Fiches des jeux complètes (GameTDB)
- **La fiche d'un jeu** (vitrine, détail, liste) affiche maintenant : genre, nombre de joueurs, année, classification
  (PEGI…), développeur / éditeur, et le **résumé** (en français quand GameTDB l'a, sinon en anglais) à la place de la
  courte description du pack.
- **D'où ça vient** : un nouveau travail du dépôt (`fiches.yml`, le 1er de chaque mois) lit la base de GameTDB
  (wiitdb, français puis anglais), garde les 1 709 jeux GameCube et publie `fiches-gc.txt` (1,1 Mo) dans la release
  « fiches ». GC Station le télécharge en fond quand la liste des jeux est ouverte (refait après 30 jours), puis ne
  garde que les lignes de tes jeux dans `jaquettes/fiches-mes-jeux.txt` (lu vite, refait si la liste change). Un jeu
  d'une autre région prend la fiche du même jeu.
- Les travaux du catalogue et des fiches ne relancent plus la compilation de la console.
- Simulateur : fiche du faux jeu de test avec genre, joueurs, année, PEGI, éditeur et résumé.

## Nouveautés v280 (build 766) — Jaquettes manquantes téléchargées toutes seules
- Un jeu sans jaquette dans posters.pak (le pack de Swiss) : GC Station la télécharge sur **GameTDB**
  (art.gametdb.com, la base des lanceurs de jeux : `wii/cover/<région>/<ID>.png`), en fond, une à la fois, quand la
  liste des jeux est ouverte et que le réseau est là. Région d'après l'ID (P : FR puis EN, E : US, J : JA, D, S, I…),
  puis les autres. « Jaquette ajoutée : … » s'affiche, et elle apparaît dans la vitrine, la grille, la liste, l'économiseur.
- Rangée toute prête dans `SD:/gc-mp3/jaquettes/ajoutees/<ID>.tex` (texture 128 × 128, comme les petites jaquettes du
  pack). On peut aussi y **déposer soi-même** un `<ID>.png` ou `.jpg` (n'importe quelle taille) : converti à la
  première vue. Les jeux que GameTDB n'a pas : `absentes.txt` (plus redemandés ; effacer le fichier pour réessayer).
- Pas de grande jaquette pour ces jeux (la petite est agrandie) ; pas de téléchargement pendant le test des touches.
- Simulateur : un `.png` déposé pour le faux jeu de test → converti et affiché dans la vitrine. (Le téléchargement
  lui-même n'a pas pu être essayé ici : GameTDB n'est pas joignable depuis la machine de compilation.)

## Nouveautés v279 (build 765, appli build 49) — 4 jeux en duel, et le téléphone comme manette
- **Motos lumineuses** (2 à 4) : chacun laisse un mur de lumière derrière lui, de plus en plus vite ; le dernier en vie
  gagne la manche, en 3. **Air hockey** (2) : maillets au stick, palet qui rebondit, le premier à 7 buts.
  **Puissance 4** (2) : le jeton tombe, les 4 alignés brillent, en 3 parties. **Sumo** (2 à 4) : pousser les autres
  hors du ring, A = élan, en 3 manches. Seul : contre la console (elle évite les murs, bloque tes alignements, fonce
  sur toi). Onglet JEUX : 16 jeux, sur 4 colonnes.
- **Le téléphone devient la manette d'un joueur dans GC Station** (pas seulement dans les jeux Pulse) : l'appli envoie
  ses touches en UDP (port 51238) ; le téléphone du joueur N s'ajoute à la manette du port N, dans tous les mini-jeux
  (anciens compris) et, pour le joueur 1, dans les menus.
- **Appli > Plus > Mini-jeux** : les 16 jeux en cartes ; toucher un jeu le lance sur la télé et ouvre la manette ; on
  choisit son joueur (1 à 4) ; « Juste la manette ». (`/api/minijeux`, `?g=N` : lancer.)
- Simulateur : deux téléphones simulés (UDP) jouent aux motos, à l'air hockey (buts marqués), au Puissance 4 et au sumo.

## Nouveautés v278 (build 764, appli build 48) — Test des touches : chaque touche dans chaque écran
- **Outils > Test de la console > Z** (ou l'appli : Outils > Test de la console > Touches) : GC Station se manipule
  tout seul pendant 3 h au plus. Chaque écran (chaque onglet, le menu START et ses sections, chaque outil, la table de
  cartes, les 6 affichages des jeux, chaque filtre de la vitrine, la fiche d'un jeu, ses cheats, ses réglages, ses
  musiques, une photo, une vidéo, le clavier, chaque mini-jeu, le mode soirée, l'économiseur) reçoit chaque touche :
  A B X Y Z L R START, la croix, le stick C dans les 4 sens, A et B maintenus. Quand une touche ouvre autre chose,
  chaque touche y est essayée aussi (2 niveaux, au rythme du temps qui reste). Le reste du temps : touches au hasard.
- **Rien d'abîmé** : pendant le test, écrire / effacer / renommer un fichier (sauf les journaux), écrire sur une carte
  mémoire, lancer un jeu ou une appli, analyser un jeu, mettre à jour, redémarrer ou quitter sont bloqués et comptés.
  Le navigateur garde son petit journal et sa graine. Les réglages d'avant sont remis à la fin.
- **Rapport** (test-console.txt, et le téléphone) : par écran (actions, images ratées, pire image, mémoire gardée,
  fermés de force, bloqués), les actions les plus lentes, ce que B ne refermait pas, les actions bloquées. Chaque
  action est notée dans le journal de plantage avant d'être faite : si la console plante ou se fige, on sait sur quoi.
- **Lancer un test depuis l'appli** : Outils > « Test de la console » : Rapide, Complet, Total, Touches ; où en est le
  test ; « Voir le dernier rapport » (`/api/test?m=0..3`, sans m : l'état).
- Simulateur (contrôle mémoire) : 10 min de test des touches, aucune erreur, aucun fichier de la carte changé à part
  ceux du navigateur.

## Nouveautés appli build 47 — 8 américain : plus fluide, plus moderne
- **Fluide** : l'écran n'est plus reconstruit toutes les 0,6 s (saccades, la main revenait au début) ; il ne change
  que si la partie change, et l'état est demandé toutes les 0,35 s.
- **Main en éventail** dessinée et animée à chaque image : les cartes glissent à leur place, celle que tu poses
  s'envole vers la table, celle que tu pioches arrive d'en haut ; les cartes jouables sont levées avec un liseré rose ;
  le doigt posé soulève la carte ; un coup refusé secoue la main.
- **Table sur un tapis vert** : la pioche (la toucher = piocher, elle s'allume quand c'est à toi), le tas avec les
  dernières cartes un peu tournées et la nouvelle qui tombe dessus, la couleur demandée par un 8, les +2, le sens.
- **Plus de transparence** : cartes, ombres, panneaux et joueurs pleins ; les cartes pas jouables sont grises mais
  pleines. Le 8 : quatre grosses touches de couleur. « À toi de jouer ! » rebondit et le téléphone vibre.

## Nouveautés v277 (build 763) — Jaquettes qui manquaient dans la vitrine
- **Un seul accès à la fois à la carte SD** : depuis la v270, les jaquettes étaient lues directement par leurs secteurs,
  par la boucle de l'écran et par le fil de fond en même temps, sans le verrou de libfat : les lectures se croisaient
  sur la carte et ratées, le jeu restait sans jaquette (un disque avec son ID, « GM4P01 », à la place de l'image).
  Toutes les lectures et écritures de la carte passent maintenant par un même verrou.
- **Une lecture ratée n'est plus définitive** : la jaquette est redemandée (jusqu'à 4 fois, toutes les 1,5 s) au lieu
  de disparaître jusqu'au prochain parcours de la liste.

## Nouveautés v276 (build 762, appli build 46) — Jeu de cartes entre potes : le 8 américain
- **Chacun sa main sur son téléphone, la table sur la télé** : appli > Plus > « Jeu de cartes ». Chaque joueur tape son
  nom ; la table s'ouvre toute seule sur la télé (tas, pioche, joueurs et leurs cartes, à qui le tour, sens, couleur
  demandée, +2, scores). Un joueur appuie sur « Commencer » (2 à 6 joueurs).
- **Règles** : même couleur ou même valeur ; le 8 se pose sur tout et fait choisir la couleur ; le 2 fait piocher 2
  (les 2 s'ajoutent) ; le valet fait passer le suivant ; l'as change le sens. Pas de carte : on pioche 1, puis on la pose
  ou on passe. Fin de manche : les autres marquent leurs cartes (8 : 50, figures : 10, as : 1) ; le moins de points mène.
- **Sur le téléphone** : la main rangée par couleur, les cartes jouables levées, le reste grisé ; vibration quand c'est
  ton tour ; « Piocher », « Passer », « Manche suivante » ; « Sur la télé » rouvre la table.
- La console décide de tout (`/api/cartes`) : un coup interdit est refusé avec la raison. Joueur muet depuis 90 s :
  retiré de la table, ses cartes sous la pioche. Télé : B ferme (la partie continue), X arrête la partie.
- Simulateur : 3 joueurs, partie jouée jusqu'au bout par l'API (coups refusés compris), captures du salon, de la partie
  et de la fin de manche.
- Tes potes installent l'appli avec l'APK que tu leur envoies (elle n'est pas publique).

## Nouveautés v275 (build 761, appli build 45) — La vibration du jeu arrive sur le téléphone
- **Console** : quand le jeu allume le moteur de vibration de la manette d'un port du téléphone (commande de lecture
  0x4003xx, bit 0), Pulse l'envoie à l'appli : UDP « GCRB » {port, marche / arrêt} vers l'adresse d'où viennent ses
  touches, au changement puis toutes les 8 images tant que ça dure. Journal : nombre de vibrations demandées.
- **Appli** : le téléphone vibre tant que la console dit « marche » (arrêt tout seul après 350 ms sans nouvelles : un
  paquet perdu ne le laisse pas vibrer). Réglage dans la manette : « Vibration du jeu ».

## Nouveautés appli build 44 — Croix de l'onglet Manette centrée
- Onglet Manette (et plein écran) : le carré du milieu de la croix descendait (cases alignées sur la ligne du texte des
  flèches) ; les 9 cases sont maintenant centrées.

## Nouveautés appli build 43 — Manette : nouveau look
- Touches en verre : reflet en haut, anneau de couleur ; appuyées : pleines, lumineuses (halo), un peu plus petites.
- Sticks : socle creusé avec le cran octogonal de la GameCube, tête bombée avec ombre ; stick C jaune marqué « C ».
- Fond en dégradé sombre avec une lueur violette ; L, R, Z et la croix dans le même style ; gâchettes progressives :
  barre d'enfoncement dans la touche.

## Nouveautés appli build 42 — Manette : réglages sans jouer
- **⚙ > Manette : réglages et disposition** : la manette s'ouvre sans jeu (rien n'est envoyé à la console) : réglages,
  disposition des touches, essai des touches et des sticks.
- **Réglages lisibles** : la fenêtre des réglages et la barre de la disposition sont opaques, sur un voile sombre (la
  manette se voyait à travers, les textes se mélangeaient).

## Nouveautés appli build 41 — Le téléphone devient une manette dans les jeux
- **🎮 Manette dans le jeu** : pendant un jeu lancé avec Pulse (réglage console ⚙ > Manettes du téléphone), l'appli
  devient une manette GameCube plein écran : sticks, A B X Y, Z, L, R, START, croix ; envoyée à la console 60 fois par
  seconde (UDP 51237). Port choisi en haut ; ouvrable aussi quand la console ne répond plus (elle est en jeu).
- **Commandes modernes** (⚙ dans la manette) : stick flottant (il se place sous le pouce) ; diriger en inclinant le
  téléphone comme un volant (doux / normal / fort) ; gâchettes progressives (glisser vers le bas, clic au fond avec
  vibration) ; manette Bluetooth ou USB branchée au téléphone (sticks, gâchettes, croix ; touches modifiables) ;
  opacité des touches, vibration.
- **Disposition modifiable** : déplacer, agrandir, changer la fonction (A ↔ B…) ou masquer chaque touche ; la croix
  bouge en entier ; gardée dans l'appli, « Remettre la disposition d'origine ».

## Nouveautés v274 (build 760) — Images d'applis en .bnr
- **Bannière GameCube (.bnr) comme image d'appli** : une appli sans icon.png ni pochette prend sa bannière :
  `<nom du .dol>.bnr` à côté du .dol (aussi à la racine de la carte), sinon le premier `.bnr` de son dossier
  (`opening.bnr`, `banner.bnr`…). L'image 96 × 32 (RGB5A3, « BNR1 » ou « BNR2 ») est mise devant, sur fond flou,
  comme une icône. Les textes de la bannière ne sont pas repris (souvent ceux du modèle : « Game Title »).
  Simulateur : appli avec seulement boot.dol + opening.bnr (bannière de Swiss) → image affichée dans la vitrine Applis.

## Nouveautés v273 (build 759) — Manettes du téléphone dans les jeux (Pulse), retour A+Z+START plus sûr
- **⚙ > Manettes du téléphone** : un téléphone devient la manette d'un port (2, 2-3, 2-4 ou 1) dans les jeux lancés
  avec Pulse. Pulse lit le Broadband Adapter à chaque image (UDP, port 51237, paquets « GCPD ») et donne les touches au
  jeu comme une vraie manette : touches, sticks, gâchettes, réponses « type » et « origine ».
- **Le jeu ne voit plus le Broadband Adapter** quand les manettes du téléphone sont actives (Mario Kart Double Dash le
  prenait sans arrêt pour le jeu en réseau local : aucun paquet n'arrivait).
- **Port vide** : rien n'est branché sur le port du téléphone, donc la vraie console finissait chaque demande du jeu en
  erreur, et le jeu redemandait « qu'est-ce qui est branché ? » sans fin. Les demandes à ce port passent maintenant par
  une vraie manette lue en ce moment (port 1) : elles finissent sans erreur, et le jeu lit les réponses de Pulse.
  Essai : Mario Kart Double Dash, port 2 reconnu et lu à chaque image, commandes du téléphone dans le jeu.
  Il faut une vraie manette branchée (port 1).
- **Journal du jeu** : 7e secteur (« GCSP ») : ce que le jeu demandait aux ports du téléphone (lectures, SIPOLL,
  commandes, réponses, événements).
- **Retour A+Z+START** : quand Swiss ne donne pas le chemin du .dol, GC Station cherche sur la carte (racine, apps) le
  .dol de ce build, au lieu de relancer ipl.dol (une autre version).
- **Résident Pulse 8 Ko plus bas** (0x817EE000 au lieu de 0x817F0000) : il était plein ; la table des pages reste à
  0x817F0000 (NATRES_HTAB). Les jeux ont 8 Ko de mémoire en moins.
- L'écran manette de l'appli : appli build 41 (ci-dessus).

## Nouveautés v269 (build 751) — L'appli se met à jour toute seule
- **⚙ > Mettre à jour l'appli** : l'appli cherche sa dernière version (compilation « Telecommande Android » réussie sur
  main), la télécharge et la donne à l'installateur d'Android (confirmation, puis installée par-dessus : réglages gardés).
  Toutes les 6 h, une notification si une nouvelle version existe.
- L'APK reste privé : GitHub demande un jeton pour le télécharger. À faire une fois : un « fine-grained token » en lecture
  seule (Repository access : antho13otz/gc-mp3 ; Permissions > Actions : Read-only), collé dans l'appli (bouton
  « Créer le jeton » : la bonne page de GitHub). Il reste sur le téléphone.
- L'APK est gardé 90 jours sur GitHub (avant : 7).

## Nouveautés v272 (build 758, appli build 40) — Envois de fichiers refaits : par morceaux, reprise, barre synchronisée
- **Par morceaux de 256 Ko** : l'appli envoie chaque fichier en petits morceaux (un PUT court chacun). Un morceau qui
  échoue est renvoyé seul, après avoir demandé à la console ce qu'elle a déjà (`/api/upstat`) ; ce qui était arrivé de
  ce morceau est enlevé. Le fichier s'écrit dans `<nom>.part` et ne prend son nom qu'une fois complet. Avant : tout le
  fichier d'un coup, et à la moindre coupure, tout était perdu (« l'envoi n'arrive presque jamais à 100 % »).
  Simulateur : 4 Mo avec un morceau coupé au milieu → reprise au bon endroit, fichier identique.
- **Barre de l'appli = barre de la télé** : l'appli comptait ce qu'elle mettait dans sa mémoire tampon (en avance : 57 %
  sur le téléphone pour ~40 % reçus) ; elle demande maintenant à la console ce qu'elle a vraiment reçu (`/api/up`).
- **Réception prioritaire** : le fil des envois de la console passe au-dessus de la boucle de l'écran (priorité 66 au lieu
  de 30) ; il dort tant que rien n'arrive et prend les données dès qu'elles sont là (avant : seulement entre deux images).
- Consoles plus anciennes : l'appli envoie d'un coup, comme avant.

## Nouveautés v271 (build 755) — Envois de fichiers : la console répond pendant l'envoi, progression dans l'appli
- **Console** : l'envoi d'un fichier par le téléphone se fait dans un fil à lui. Avant, le serveur du téléphone ne
  répondait plus à rien pendant tout l'envoi (63 s pour 30 Mo dans le simulateur) : l'appli affichait « Console
  injoignable », cherchait la console… Maintenant l'état, les commandes et le reste continuent de répondre. Un
  2e envoi en même temps est refusé (503) : l'appli réessaie. L'envoi s'arrête proprement (fichier effacé) si la
  carte SD doit être démontée pendant l'envoi (lancement d'un jeu).
- **Appli** : barre de progression des envois (fichier, %, débit) sous le mini-lecteur, et dans une notification
  (aussi appli fermée) ; 3 essais par fichier ; pendant un envoi, le téléphone garde le Wi-Fi et le processeur (écran
  éteint : l'envoi continue). Nouvelle version de l'appli : notification dès l'ouverture de l'appli (avant : toutes
  les 6 h seulement).

## Nouveautés v270 (build 752) — jaquettes lues directement sur la carte SD
- Test de la console (build 749) : la plupart des images bloquées (200 à 620 ms, « manette » ou « dessin : jeux »)
  lisaient 200 à 770 Ko sur la carte SD pendant l'image. C'est libfat qui, pour aller à un endroit d'un gros fichier,
  suit toute la chaîne des clusters depuis le début (4 Ko au hasard : 540 ms sur la carte de 64 Go).
- posters.pak et stills.pak : leurs morceaux sur la carte sont trouvés une fois à l'ouverture (comme pour Pulse),
  puis chaque jaquette est lue directement par ses secteurs (`fm_build`, `fm_read`, `fm_runs` dans gclib.c), aussi
  dans le fil de fond et pour le téléphone. Le fichier est revu toutes les 3 s (taille, 1er cluster) : s'il a
  changé, retour à la lecture normale. Vérifié sur une image FAT32 fragmentée (5000 lectures au hasard, 0 erreur).
- Démarrage : « journal Pulse » mesuré à part de « guide / qui joue » (le test comptait l'attente de « Qui joue ? »).

## Nouveautés v268 (build 749) — Succès dans les jeux GameCube
- **Pulse vérifie des succès pendant le jeu** : une fois par image, il lit quelques valeurs dans la mémoire du jeu
  (vies, rubis, pièces…) et les compare à la condition de chaque succès. Un succès n'est obtenu qu'après avoir vu sa
  condition fausse au moins une fois (pas d'un coup au démarrage, mémoire pas encore remplie), puis vraie pendant
  30 images de suite (réglable). Les succès obtenus sont écrits tout de suite dans un 6e secteur du journal du jeu.
- **Au retour sur GC Station** : « Succès obtenu : … », la liste dans le journal du jeu (obtenu, après combien de
  temps, nouveau ou pas), et la notification sur le téléphone. **START > Outils > Succès** montre les succès des jeux
  à la suite des succès généraux ; dans l'appli : onglet Jeux > Dernier lancement > Succès.
- **Un fichier par jeu**, modifiable : `SD:/gc-mp3/succes/<ID>.txt`, une ligne par succès :
  `Titre | Description | m16(0x803C4C0C) >= 200` (tailles m8 / m16 / m32, comparaisons == != >= <= > <,
  plusieurs conditions avec &&, pointeur `m16(p32(0x80xxxxxx)+0x10)`, `| images 60` pour 1 s). Les lignes fausses
  sont ignorées et signalées dans `dernier-lancement.txt`.
- **Fournis pour 3 jeux** (USA), d'après les adresses des codes de triche connus, **pas encore vérifiés sur console** :
  Zelda Wind Waker (rubis, portefeuille, cœurs), Mario Party 4 (pièces et étoiles du joueur 1), Mario Sunshine (vies).
- Les définitions de RetroAchievements ne sont pas utilisables : leur API publique ne donne pas les conditions, et
  le canal qui les donne est réservé aux émulateurs agréés.
- Côté Pulse : la vérification est écrite en assembleur PowerPC (même taille avec le compilateur de la console et
  celui du banc d'essai) : le résident n'avait plus que 576 octets libres, **il est maintenant plein**
  (fin à 0x817FFF00, la limite) ; toute nouvelle fonction de Pulse devra d'abord libérer de la place. Pas de succès
  pour un jeu qui a des corrections « FIXT » (Pulse ne chercherait pas la section) : c'est écrit dans
  `dernier-lancement.txt`.
- Banc de Pulse : la carte SD émulée accepte les écritures ; test des succès (armement, 3 images, tailles 1 / 2 / 4,
  pointeur, secteur écrit). Le journal du jeu passe à 6 secteurs (3 Ko).

## Nouveautés v267 (build 743) — Journal du jeu complet
- **`journal-jeu.txt` montre tout ce que Pulse note** :
  - un **résumé en tête** : jeu, résultat (quitté, planté, figé…), temps de jeu, version de Pulse, build de GC Station,
    date de la traduction ;
  - **toutes les valeurs de Pulse** : les 124 compteurs et adresses de l'en-tête, avec leur nom ;
  - le **journal brut** : les 5 secteurs (en-tête, plantage, pile, fils du jeu, périphériques EXI) en hexadécimal et en
    texte, les suites de zéros résumées en une ligne.
- **Photo de la mémoire au plantage** :
  - la **mémoire basse du jeu expliquée** : ID du disque, signatures, mémoire, arène du jeu, mode TV, fil en cours,
    horloges ;
  - **toutes les zones** de la photo (pile, variables r13, début de l'arène… : 2 Ko de chacune) ; avant, seulement
    r3, r4 et r30.
- **Correction** : la photo de la mémoire n'apparaissait jamais (« photo incomplète » à chaque fois depuis v83). GC Station
  lisait son 1er secteur avec `read_file` limité à 512 octets, qui refuse le fichier entier de 352 Ko.
- Rien n'a changé dans Pulse : tout se passe dans la traduction faite par GC Station au démarrage suivant (`natif.c`).

## Nouveautés v266 (build 741) — L'appli du téléphone en direct
- **Console trouvée tout de suite** : l'appli demande à tout le Wi-Fi « Y a-t-il une GC Station ? » (UDP, port 51236)
  et la console répond elle-même (nom, port, build) : moins d'une seconde, au lieu d'essayer les 254 adresses. Plusieurs
  consoles : l'appli demande laquelle. Le balayage d'avant reste en secours (consoles plus anciennes).
- **Mises à jour en direct** : l'appli s'abonne (toutes les 10 s, valable 30 s) et la console la prévient dès que l'état
  change (morceau, pause, volume, paroles, position sautée). L'état n'est plus relu chaque seconde mais sur avis
  (et toutes les 5 s au cas où ; 15 s quand l'appli est en fond) ; la position avance sur le téléphone. Réaction plus
  rapide, moins de batterie et moins de travail pour la console.
- **Console injoignable** : plus d'écran bloquant ; un bandeau, un nouvel essai toutes les 3 s, la console recherchée si
  son adresse a changé, et « Console de retour » quand elle répond.
- **Partie en cours** : juste avant de lancer un jeu, la console prévient l'appli (ID, nom, profil, temps de jeu restant
  du jour). L'appli montre la jaquette, le temps de jeu et le temps restant ; la notification a un chronomètre et
  prévient 5 min avant la limite du jour, puis à la limite. « Partie terminée » (avec la durée) au retour de la console.
  Pendant le jeu, la console ne peut pas répondre (le jeu a toute la machine) : pas de bouton « revenir » dans l'appli,
  c'est toujours A + Z + START.
- Banc de Pulse (`tools/natif-test`) : il ne compilait plus depuis v239 / v262 (natif.c prend des fonctions à main.c et
  tab_phone.c) ; bouchons ajoutés (`v262.inc`).

## Nouveautés v265 (build 735) — Test de la console : cause des blocages, réseau en vrai
- **Cause de chaque image bloquée** : pendant chaque image, le test compte maintenant les fins de bloc du son
  (interruptions), les Ko lus ou écrits sur la carte SD (interface de la carte montée à travers un compteur) et
  les Ko reçus du réseau. Une image « processeur pris » est classée : interruptions coupées (le son s'est arrêté
  aussi), réseau (Ko reçus), carte SD (Ko lus / écrits), ou un fil plus prioritaire (rien de tout ça). Chaque
  image bloquée du rapport donne ces chiffres.
- **Réseau en vrai** (test total, musique en fond) : téléchargement de 3,5 Mo écrit sur la carte SD (puis effacé),
  une page web dans Cube Browser (15 s), une radio en AAC (FIP) ; pour chacun : débit, trous du son, images bloquées.
  (build 735 : noms des éléments plus longs, 96 caractères au lieu de 64 : les chiffres étaient coupés.)

## Nouveautés v264 (build 732) — « Derniers jeux lancés » s'ouvre tout de suite
- Test de la console (build 731) : l'outil bloquait l'écran 10,8 s à l'ouverture. Il relisait toute la carte SD pour
  retrouver les jeux (`gl_scan`, liste refaite) ; il prend maintenant la liste gardée (`gl_scan_full(0)`), comme le téléphone.

## Nouveautés v262 (build 727) — appli : dernier lancement, sauvegardes du jeu, notifications
- **Carte « Dernier lancement »** en haut de l'onglet Jeux de l'appli : le jeu, le résultat (quitté avec A+Z+START,
  planté, figé repéré par Pulse, aucune nouvelle, pas de retour), il y a combien de temps, le temps joué, l'endroit
  (PC) ; boutons **Journal** et **Partager le journal** (fichier .txt dans Téléchargements / GC Station).
  Console : `resume-jeu.txt` écrit à la traduction du journal, `/api/pulse` et `/api/pulse.txt`, `pj` / `pjr` dans l'état.
- Le journal traduit n'est plus remis à zéro mais marqué « DONE » : avant, un 2e démarrage de GC Station sans jeu
  remplaçait le bon journal par « aucune nouvelle du jeu ».
- **Notifications** du téléphone : jeu planté, figé ou sans nouvelles ; téléchargement fini (`dl` / `dln` dans l'état).
- **Fiche du jeu** : nombre de lancements, dernière fois, dernier essai avec Pulse (gardé par le téléphone) ;
  section **Sauvegardes (carte virtuelle)** : les sauvegardes du jeu, à télécharger (.gci) ou à effacer.
- **Onglet Cartes** : toucher une sauvegarde propose de la télécharger ou de l'**effacer** (`/api/gcidel`, nom vérifié).
- Patchs de secours renommés : « Lecteur lent (comme un vrai) », « Lecteur rapide (normal) ».

## Nouveautés v260 (build 725) — journal de Pulse : police, horloge et réglages de la console (canal EXI 0)
- Soulcalibur II (GRSPAF) avec la carte virtuelle du slot A : plus de blocage (build 722), mais textes des menus
  absents, barre de titre abîmée, pas de question 50/60 Hz. Sans la carte virtuelle : tout est normal.
- Le slot A partage le canal EXI 0 avec la police, l'horloge et les réglages de la console, que Pulse laisse passer
  au vrai matériel. Un 5e secteur du journal (« GCSX ») garde les 15 premiers échanges avec ces périphériques
  (commande, adresse, taille, premiers mots reçus), au retour A+Z+START ou au plantage. Notés directement dans le
  tampon des comptes rendus (libre pendant le jeu) : le build 724 dépassait la place du résident. La compilation
  affiche maintenant où finit le résident (annotation), et pourquoi s'il est trop gros.

## Nouveautés v258 (build 722) — Pulse : la carte virtuelle respecte les masques du jeu
- Soulcalibur II (GRSPAF) se figeait en relisant sa sauvegarde sur la carte virtuelle (le fichier de la carte était
  intact, sommes de contrôle bonnes). La fin d'un transfert de la carte était donnée au jeu même quand il l'avait
  masquée dans son système (masques du SDK 0x800000C4 / 0x800000C8, journal : F07F8360, EXI0 masqué).
- Désormais, comme une vraie console, la fin attend que le jeu la démasque (`vc_want` dans natres.c), pour les
  slots A et B. Peut aussi aider d'autres jeux qui se figeaient pendant une sauvegarde avec Pulse.

## Nouveautés v257 (build 721) — journal de Pulse : l'image réglée par le jeu
- Soulcalibur II (GRSPAF) : image tassée en haut, bande noire puis bande répétée en bas avec Pulse (normale avec
  Swiss). « PAL 60 Hz » forcé : le jeu se fige dès sa 1re image (à laisser sur « Comme le jeu »).
- Le 4e secteur du journal garde les registres de l'image (VI 0xCC002000-0x5F) au retour A+Z+START : format,
  entrelacé / progressif, lignes actives, mise à l'échelle horizontale, largeur lue (12 fils au plus au lieu de 15).

## Nouveautés v256 (build 720) — mise à jour : toujours la version compressée quand elle est choisie
- Avec « Version compressée » à Oui, la recherche de mise à jour proposait la version normale quand la compressée
  du même build n'était pas encore publiée (tâche `compresse` de la compilation, quelques minutes après le .dol).
  Maintenant elle attend : « Build N : version compressée en préparation, réessaie dans quelques minutes ».

## Nouveautés v255 (build 718) — pastilles des jeux posées à la main (appli)
- Confirmé : Phantasy Star Online Episode I & II (GPOP8P) marche avec Pulse (v252 / v254). Ses sauvegardes vont sur
  la vraie carte du port A (la carte virtuelle n'est branchée que dans le chargeur, pas dans PsoV3.dol).
- Fiche d'un jeu dans l'appli : « PASTILLE » : aucune, jouable (vert), marche mal (jaune), écran noir (orange),
  plantage (rouge). La pastille s'affiche aussi sur la jaquette dans l'appli et sur la console (`compat.txt`).
- Console : `/api/compat` (toutes les pastilles) et `/api/compatset?id=&d=0..4` (`cp_set_manual` : sans lancer la
  réparation automatique ; 0 = enlevée).

## Nouveautés v254 (build 716) — PSO : plus de place pour brancher PsoV3.dol
- Journal v252 : PsoV3.dol suivi, mais seulement 14 accès au lecteur branchés dedans (97 en tout) : la place des
  aiguillages posés pendant le jeu était sans doute pleine. 32 Ko de plus pour PSO (`GPO`, `GPS`).
- Journal : « NON BRANCHÉS faute de place : N » (16 bits hauts du compteur des programmes suivants).

## Nouveautés v252 (build 714) — Pulse : un .dol lu en plusieurs morceaux par le jeu (PsoV3.dol)
- Journal v251 de PSO : le chargeur lit d'abord un petit programme (branché en v251), puis **PsoV3.dol** (5,2 Mo) par
  morceaux de 512 Ko. Le résident suit maintenant ce .dol hors d'OSExec : en-tête reconnu dans la 1re lecture
  (`nx_dol`), puis chaque morceau de ses sections de code branché à son adresse finale (`nx_dol_sections`), y compris
  la suite de la lecture qui contient l'en-tête. Un .dol relu alors qu'il tourne déjà (son propre main.dol) est ignoré.

## Nouveautés v251 (build 713) — Pulse : programme lu par le jeu lui-même (Phantasy Star Online)
- PSO (GPOP8P) : le main.dol n'est qu'un chargeur. Il lit son vrai programme (environ 200 Ko, au bout du disque),
  le recopie lui-même à sa place et y saute : ce programme parlait au vrai lecteur (erreur « An error has occurred »).
  Pulse universel refusé (2e BAT déjà pris par le jeu).
- Résident : une lecture qui contient un **.dol entier** (en-tête valable, toutes les sections dans la lecture) voit
  ses sections de code branchées dans le tampon, avec leur adresse finale (`nx_whole_dol`). Journal : « PROGRAMME LU
  PAR LE JEU … accès au lecteur branchés ».
- Journal : le **fichier du disque** lu en dernier (nom, position, taille, 32 premiers octets) sous « Dernière
  commande » (`na_iso_name_at`, table des fichiers de l'ISO). Si le programme de PSO est compressé, on le verra là.

## Nouveautés v250 (build 712) — patchs de secours de Pulse choisis sur le téléphone
- Fiche d'un jeu dans l'appli : « Pulse + patchs de secours… » : Pulse universel, vitesse d'un vrai lecteur, lecteur
  rapide, sans carte virtuelle, sans cheats, sans 16:9, vidéo d'origine, pour **ce lancement seulement**.
- Console : `/api/launch?...&fx=TS…` (lettres des essais de repair.c) ; `jeux_cfg_apply` les applique à la place de
  l'essai de la réparation automatique (`jc_fx`), le journal du lancement le dit (« choisi sur le téléphone : … »).

## Nouveautés v248 (build 711) — LEGO Star Wars II : essai d'une correction du jeu
- Journal v245 : interruption « fin de dessin » levée et non masquée, mais le fil principal reste endormi dans
  `GXWaitDrawDone` (drapeau jamais remis). Correction propre à GL7P64 (`na_fixes`) : la boucle d'attente saute
  (0x801EA84C : `beq` → `nop`, instruction vérifiée avant). À confirmer sur la console.

## Nouveautés v247 (build 710) — plus d'écran rouge en lançant un jeu depuis le téléphone
- Écran rouge (DSI, adresse 0000001C) en lançant un jeu depuis l'appli : le fil du serveur du téléphone lisait encore
  une jaquette (`fseek` dans `tp_handle`) pendant que la carte SD était démontée pour le jeu. `bg_quiesce` appelle
  maintenant `tp_park` : la demande en cours finit d'abord (3 s au plus), et les suivantes sont refusées 20 s.
- Nouveau workflow « Adresses du plantage » (`.github/workflows/adresses.yml`) : retrouve les fonctions d'un écran
  rouge à partir des adresses (addr2line sur le .elf de la compilation).

## Nouveautés v246 (build 708) — onglet TÉLÉPHONE caché : la télécommande reste active
- Réglages > Apparence > Onglets affichés : décocher TÉLÉPHONE cache seulement l'onglet. Le serveur du téléphone
  continue de tourner (avant : coupé, et les demandes refusées en 403). Avec « Télécommande au démarrage » à Oui,
  il démarre même si l'onglet est caché.

## Nouveautés v245 (build 706) — journal de Pulse : interruptions et dessin au retour
- Le crochet de la fin de dessin n'était pas la cause (LEGO Star Wars II reste noir en v244). Le 4e secteur du
  journal note aussi, au retour A+Z+START : cause et masque des interruptions (PI), contrôle du dessin (PE : fin de
  dessin autorisée, en attente), état du processeur graphique (CP : au repos, FIFO débordée) et masques du jeu ;
  3 appels montrés par fil en attente (avant : 2).

## Nouveautés v244 (build 705) — Pulse ne branche plus la fin de dessin
- Journal de LEGO Star Wars II (GL7P64, écran noir après le choix de la langue) : le fil principal du jeu attendait
  pour toujours la **fin de dessin** du processeur graphique (`GXWaitDrawDone`, fil 80315ED0), pendant qu'un autre
  fil affichait l'écran de chargement. Pulse avait un crochet sur cette interruption (19, signes de vie) : il est
  retiré ; le jeton de dessin (18) garde le compte « dessin » du journal.

## Nouveautés v243 (build 703) — journal de Pulse : les fils du jeu
- Au retour **A+Z+START** (ou à un plantage), Pulse note maintenant **tous les fils (threads) du jeu** dans un
  4e secteur du journal (`thr_dump` dans natres.c, `NRF_THREADS`) : état (prêt, en cours, en attente), priorité,
  PC, LR et 3 adresses de retour, et le code qui attend pour les fils en attente (`na_thr_print`).
  Utile pour les écrans noirs où le fil principal tourne à vide en attendant un autre fil (LEGO Star Wars II).

## Nouveautés v242 (build 700) — la télécommande fait (presque) tout
- **Serveur du téléphone**, nouvelles adresses (travail fait par la boucle principale, `x73_misc_pump`) :
  - `/api/cheatedit?id=&op=a|r|c|d&n=&name=&codes=` : ajouter, renommer, changer les codes ou supprimer un cheat de
    `SD:/gc-mp3/cheats/<ID>.txt` (`ch_phone_edit`) ; le choix des cheats actifs suit chaque cheat. `/api/cheats` donne
    aussi les codes ;
  - `/api/gamecfg?id=&i=&row=&d=` : les réglages du jeu (menu Y de la fiche), lus ou changés d'un cran ; row 13 = tout
    comme la console (`jc_phone`, `jc_step`, `jc_value_text`) ;
  - `/api/gamefav?id=&i=` : favori (la liste n'est pas retriée, pour garder les numéros) ;
  - `/api/launch` accepte `id=` : le bon jeu même si la liste a changé d'ordre (raccourcis du téléphone) ;
  - `/api/lyrics` : toutes les paroles du morceau avec leurs temps, et la position de lecture ;
  - `/api/queueadd?i=&d=1` : un morceau joué juste après (ou à la fin de la file avec d=0) ;
  - `/api/regpack` : la sauvegarde de tous les réglages ; `PUT /upload?dir=regtel` puis `/api/regrestore` : la remettre.
- **Télécommande Android** : fiche du jeu avec favori, raccourci sur l'écran d'accueil, cheats modifiables et réglages
  du jeu ; paroles synchronisées (et plein écran karaoké) ; recherche partout ; file d'attente par appui long ; tuile
  des réglages rapides ; raccourcis de l'icône ; dictée vocale ; manette plein écran en paysage ; copie automatique
  des cartes mémoire et des réglages sur le téléphone (5 gardées), avec restauration.

## Nouveautés v241 (build 699) — l'écran Jeux du téléphone ne fige plus la console
- `/api/games` et le lancement depuis le téléphone relisaient **toute la carte SD** (et le GC Loader) quand la
  liste des jeux n'était pas encore chargée : la console restait figée pendant ce temps. Ils prennent maintenant
  la liste gardée (`gl_scan_full(0)`), comme l'onglet JEUX.
- **Télécommande Android** : les jaquettes sont demandées une par une (plus 4 à la fois), seulement pour les jeux
  à l'écran, et gardées sur le téléphone : la console ne les renvoie plus aux ouvertures suivantes.

## Nouveautés v240 (build 698) — cheats choisis depuis le téléphone
- **Serveur du téléphone** : `/api/cheats?id=GALE01` donne les cheats utilisables de `SD:/gc-mp3/cheats/<ID>.txt`
  (nom, activé ou non, nombre de lignes de code) ; `/api/cheatset?id=GALE01&s=0101…` enregistre le choix dans
  `<ID>.chtsel`, au même format que Swiss et l'éditeur de cheats (un octet par cheat + empreinte des codes).
  Le travail est fait par la boucle principale (`ch_phone_json`, `ch_phone_set` dans cheats.c) ; l'éditeur
  ouvert sur ce jeu relit le choix, et le nombre de cheats actifs de Mes jeux est remis à jour.
- **Télécommande Android** : la fiche d'un jeu (jaquette, ID, temps de jeu) montre ses cheats avec des
  interrupteurs, à activer avant de lancer avec Pulse ou le lanceur de secours. Les fenêtres de l'appli passent
  au thème Néon rétro. La barre de volume d'Android suit le vrai volume de la console (11 crans, pas de 10 %).

## Nouveautés v239 (build 683) — une carte mémoire virtuelle par jeu
- **Réglages du jeu** (Y sur un jeu > Carte mémoire) : nouveau choix **« Rien qu'à ce jeu »** : le jeu a sa propre
  carte virtuelle (`gc-mp3/resident/carte-A-GALE01.raw`), créée à son premier lancement avec Pulse. Les autres
  choix restent : réglage général, une carte précise, ou « Aucune (vraie carte) ».
- **Réglages > Jeux > Pulse > Une carte par jeu** (Non par défaut) : la même chose pour tous les jeux d'un coup.
  Le réglage d'un jeu passe toujours avant.
- **Cartes mémoire virtuelles** (START > Outils) : une carte d'un jeu affiche le **titre du jeu** au lieu du nom du
  fichier ; jusqu'à 64 cartes (avant 16).
- Simulateur : scénario `cartes-virtuelles`.

## Nouveautés v238 (build 682) — démarrage plus rapide, sauvegarde complète, ancienne version
- **Liste de la musique gardée** (`SD:/gc-mp3/cache/musique.txt`) : au démarrage, elle est relue d'un coup au lieu
  de parcourir tous les dossiers. Quelques secondes après, un fil en fond reparcourt les dossiers sans bloquer
  l'écran ; si quelque chose a changé (morceau ajouté ou supprimé à la main, nouveau `.zip` rétro), la liste est
  relue comme avant.
- **Sauvegarde des réglages** (START > Outils > Sauvegarder / Restaurer les réglages) : en plus, le compagnon, les
  notes des morceaux, la compatibilité des jeux, les profils, et les réglages et favoris de Cube Browser (pris en
  compte au démarrage suivant). Jamais les clés (assistant IA, identifiants, cookies).
- **Réglages > Réseau > Mises à jour > Ancienne version (.ancien)** : taille de la copie de secours gardée par la
  dernière mise à jour (il n'y en a qu'une) ; A deux fois pour la supprimer. Refusé tant que le garde-fou d'une
  mise à jour récente peut encore s'en servir.

## Nouveautés v237 (build 681) — « plus grand bloc » corrigé
- Outils > Mémoire (et la page `/memoire` du téléphone) : le **plus grand bloc** affichait toujours environ
  24 Mo, plus que la mémoire libre. Le compilateur supprimait la mesure (réserver puis libérer aussitôt) et
  la croyait réussie. La mesure est réelle maintenant, et la ligne « Relance directe après une mise à jour »
  aussi.

## Nouveautés v236 (build 680) — 1,3 Mo de mémoire en plus, la carte SD depuis le téléphone
- **Mémoire de la console** : environ 1,3 Mo de moins réservés au démarrage (8 184 → 7 137 Ko de zones
  réservées, 1 868 → 1 570 Ko de données) :
  - noms des morceaux rangés dans une réserve qui grandit selon la bibliothèque (avant : 2 000 × 256 octets
    fixes = 500 Ko) ;
  - adresse d'une radio limitée à 768 caractères (avant 2 048 : 262 Ko pour 128 radios) ; une radio à
    l'adresse plus longue est ignorée ;
  - tri des radios favorites et regroupement par artiste / album dans une mémoire temporaire (avant : 387 Ko
    réservés en permanence) ;
  - plus de tables de déroulement du compilateur pour le code C (~300 Ko de données inutiles).
- **Gérer la carte SD depuis le téléphone** (page du téléphone > Fichiers) : bouton **⋯** sur chaque fichier
  ou dossier (Renommer, Couper, Supprimer), **Nouveau dossier**, **Coller ici** pour déplacer. Protégés : la
  racine, le dossier `gc-mp3` et le morceau en cours. Si la liste a changé entre-temps, l'opération est
  refusée (le nom est vérifié). Les listes (musique, photos, rétro, applis) sont relues après la série.
- Simulateur : scénario `vues-biblio` (vues dossiers / artistes / albums).

## Nouveautés v228 — retour à l'accueil de Cube Browser
- En style Aurore (pas de barre d'outils), **B sans page précédente ramène à l'accueil de Cube Browser**
  (« Réglages du navigateur », favoris, sites…). Avant, si Cube Browser rouvrait directement un site, on
  ne pouvait plus revenir à l'accueil ni aux réglages, sauf par **Y** (Favoris : boutons Accueil et Réglages).

## Nouveautés v227 — toutes les applis de SD:/apps
- **Applis** : un .dol posé seul dans `SD:/apps` (par exemple envoyé depuis le téléphone), ou une appli sans
  image, apparaît maintenant dans la liste (carte avec son nom). Avant, seules les applis avec une image
  étaient montrées. À la racine de la carte, il faut toujours une image (pour ne pas montrer `ipl.dol`, Swiss…).
- Nouveau réglage **Réglages > Jeux > Liste des jeux > Applis sans image** : Montrer (par défaut) ou Cacher.
- Une appli dont l'image s'appelle `cover.png`, `pochette.jpg`… (et pas `icon.png`) affiche bien cette image.

## Nouveautés v226 — mise à jour en version compressée
- Nouveau réglage **Réglages > Réseau > Mises à jour > Version compressée** (Non par défaut). Sur **Oui**, la
  mise à jour télécharge le .dol compressé (2,2 Mo au lieu de 5,5 Mo, démarrage plus rapide) au lieu du
  normal. Il s'installe exactement pareil : à la place du fichier lancé (`ipl.dol`, `boot.dol`…), l'ancien
  gardé en `.ancien`, les autres copies de GC Station remplacées aussi. Le message devient « Build N
  compressé disponible ».
- La compilation en ligne publie aussi ce fichier dans la release « dernier » (`gc-station-compresse-bN.bin`,
  ajouté à `release.json`). S'il n'est pas encore prêt (quelques minutes après le .dol normal), la mise à jour
  prend le .dol normal.

## Nouveautés v225 — lancer une appli depuis Fichiers
- Dans l'onglet **FICHIERS**, **A** sur un fichier `.dol` de la carte SD lance l'appli, comme depuis la liste
  Applis (retour à GC Station quand elle se ferme).

## Nouveautés v224 — envoi depuis le téléphone
- **Les .dol envoyés depuis le téléphone vont dans `SD:/apps`**, quel que soit le dossier choisi, et
  apparaissent tout de suite dans la liste Applis (onglet Jeux). La page du téléphone indique où le fichier
  a été rangé (« envoyé dans SD:/apps (Applis) »).
- **Moins de saccades pendant les envois** : un fichier qui n'est pas de la musique (.zip, jeu…) ne fait plus
  relire toute la carte SD (musique + rétro) à chaque fichier. Un jeu envoyé dans Rétro ne relit que la liste
  RÉTRO, et les relectures attendent la fin de la série d'envois (2 s sans nouvel envoi).

## Nouveautés v223 — 1,1 Mo de mémoire libérée
- **Tampon audio de 6 s** (au lieu de 10 s) : environ 750 Ko de mémoire en plus pour le reste.
- **Fondu enchaîné limité à 4 s** (réglage : 0, 2 ou 4 s ; une ancienne valeur plus grande passe à 4 s).
- **Ancien moteur web retiré** (`web_layout.c`, `web_css.c`, `web_dom.c`, `web_style.c`, `web_box.c`) :
  toutes les pages passent par Cube Browser depuis longtemps. Environ 360 Ko de code en moins.
  Une page longue s'affiche toujours en partie pendant le chargement, puis en entier à la fin.
- **Essai : .dol compressé** (`gc-station-compresse.dol`, release « essai-compresse » du dépôt) : le même
  programme compressé en xz par le « packer » de Swiss, décompressé en mémoire au démarrage. Deux fois
  moins à lire sur la carte SD. À lancer depuis Swiss pour comparer ; jamais utilisé par la mise à jour.

## Nouveautés v222 — le téléphone sert de clavier
- **Clavier sur le téléphone** : quand un clavier est ouvert sur la télé (celui de GC Station ou celui de
  Cube Browser), la page du téléphone affiche une carte « clavier » en haut. Le texte tapé apparaît en
  direct sur la télé, **OK** valide (commande `/api/type?t=…&go=0|1` ; champs `kb`, `kbh`, `kbt` dans
  `/api/state`). Le clavier de Cube Browser n'est proposé que dans son onglet, hors menus.
- **Photo du téléphone sur la télé** : dans l'onglet Photos de la page du téléphone, un bouton réduit la
  photo à 1280 px, l'envoie dans `SD:/photos` et l'affiche aussitôt en plein écran (`/api/showname`).
  Afficher une photo ferme le clavier ouvert.
- L'adresse de la page ouverte dans **Cube Browser** est envoyée au téléphone (champ `web`).
- Correction : le réseau démarre aussi quand « Télécommande au démarrage » est sur Oui.

## Nouveautés v221 — démarrage plus rapide
- Le rangement de la carte SD (anciens fichiers déplacés dans `SD:/gc-mp3`, dossiers créés) ne se fait plus
  qu'une fois : un repère `SD:/gc-mp3/reglages/.range` évite environ 80 recherches sur la carte SD à chaque
  démarrage (dans le simulateur : 123 ms → 16 ms, démarrage 2 fois plus court).
- L'écran **Mémoire en direct** affiche aussi la durée du dernier démarrage et son étape la plus longue,
  et ne garde que les 3 onglets qui laissent le moins de mémoire.

## Nouveautés v220 — mémoire en direct
- **START > Réglages > Outils > Mémoire en direct** : la mémoire libre, le plus grand bloc d'un seul tenant
  et la mémoire utilisée, mis à jour chaque seconde, avec une courbe des 2 dernières minutes.
- Dit si la relance directe après une mise à jour est possible (il faut un bloc de 6 Mo) ou si elle passera
  par le lanceur de secours.
- Pour chaque onglet visité, le moins de mémoire libre vu : pratique pour trouver ce qui en prend.

## Nouveautés v219 — relance après une mise à jour (par le lanceur)
- Même par morceaux (v217), le nouveau programme (près de 6 Mo) ne tenait pas toujours dans la mémoire libre de
  GC Station en marche : « Installée : relance impossible ici (Lecture du .dol impossible) ». La relance essaie
  maintenant, dans l'ordre : d'un seul bloc (si la mémoire le permet) ; puis **par le lanceur de secours**
  (SD:/gc-mp3/lanceur/lanceur.dol, ~0,9 Mo) : un programme neuf, avec toute la mémoire, qui relance la nouvelle
  version (comme son retour sur GC Station après un jeu) ; puis par morceaux. Pareil pour le bouton Reset.
- Si rien ne marche : « Installée ! Relance GC Station pour l'utiliser » (GC Station reste ouvert).
- Build GitHub : une note « Plus gros éléments » liste ce qui prend de la place dans le programme.

## Nouveautés v218 — la barre des onglets dans Cube Browser
- Comme dans les autres onglets, approcher le pointeur du haut de l'écran fait descendre la barre de tous les
  onglets (icône + nom) ; dans Cube Browser, c'est le pointeur du navigateur qui la fait venir. L'onglet visé
  s'encadre, A l'ouvre ; le pointeur reste visible par-dessus la barre.

## Nouveautés v217 — relance après une mise à jour
- Après une mise à jour, GC Station se relançait en lisant le nouveau programme (près de 6 Mo) d'un seul bloc en
  mémoire. Avec Cube Browser et le globe, la mémoire n'a souvent plus un seul bloc aussi grand : la relance
  échouait et GC Station quittait, la console revenait au menu du lecteur de disque. Maintenant, si un seul bloc
  ne suffit pas, le .dol est lu par morceaux de 256 Ko, et le petit programme de copie remet chaque morceau à sa
  place (tous les morceaux sont au-dessus des sections du nouveau programme : rien n'est écrasé pendant la copie).
  Même chose pour le bouton Reset et pour lancer n'importe quel gros .dol.
- Si la relance est quand même impossible, GC Station ne quitte plus : il reste ouvert et dit de le relancer.
- Simulateur : `PCSIM_PIECES=1` (lancement par morceaux), `PCSIM_RESET_AT=ms` (bouton Reset).

## Nouveautés v216 — Cube Browser : plus d'écran de chargement
- Style Aurore : l'écran « Chargement… » qui couvrait la page est supprimé. Il attendait les images et les styles
  de la page avant de la montrer ; maintenant la page s'affiche dès que son texte est reçu (les images arrivent
  ensuite, à leur place).
- En choisissant un site sur l'accueil, l'onglet passe tout de suite à la vue page : ligne d'adresse (avec l'état
  du chargement et le trait rose → bleu qui file) et cadre de la page, où la page apparaît dès qu'elle arrive.
- Réglages du navigateur : « Écran de chargement » est caché en Aurore (il ne sert plus).

## Nouveautés v215 — merci à Arial-Bold
- Onglet WEB (style Aurore) : la première ligne de l'accueil est **Cube Browser**, choisie en arrivant. Sa fiche dit
  que l'onglet est le navigateur créé par **Arial-Bold** (logiciel libre, licence MIT), que GC Station l'intègre et
  l'adapte, et le remercie ; A ouvre la page du projet (github.com/Arial-Bold/Cubebrowser).
- Pendant le chargement d'une page : la signature « Cube Browser, par Arial-Bold » sous « Chargement… ».
- L'onglet WEB s'appelle maintenant **Cube Browser** (en-tête, barre des onglets, menus).

## Nouveautés v214 — Cube Browser dans l'Aurore de GC Station
- Avec le style **Aurore**, l'onglet WEB n'est plus un écran à part : Cube Browser se place entre l'en-tête de
  GC Station (« Radio · Web · Rétro ») et sa légende de boutons, avec les mêmes cadres de verre, le même filet
  lumineux et la même sélection en dégradé rose → bleu (couleurs du thème de GC Station).
- **Accueil** façon onglet RADIO : à gauche la liste (Adresse ou recherche, Favoris, Réglages du navigateur,
  Commandes et astuces, puis les sites conseillés), à droite la fiche de la ligne choisie (grande icône,
  description, adresse) et les derniers favoris. Croix : de ligne en ligne (sans pointeur, comme les autres
  listes) ; A : ouvrir.
- **Pages** : une ligne d'adresse sous l'en-tête (cadenas, adresse ; à droite le titre de la page ou l'état du
  chargement, avec un trait rose → bleu qui file), la page dans un cadre de verre sombre, et la légende
  A cliquer · B retour · X adresse · Y favoris · C défiler · Z onglet. Plus de barre d'outils : l'accueil et les
  réglages du navigateur sont dans le panneau des favoris (Y).
- **Réglages du navigateur, clavier, favoris, menus déroulants, messages, aide** : tous en Aurore (verre,
  pastilles en dégradé, interrupteurs roses). Le chargement d'une page montre des voiles de lumière au lieu de la
  scène sous-marine. Dans les réglages, Thème et Marge sont cachés (c'est GC Station qui décide).
- Pages sombres (assistant IA, musique libre…) : la page de l'assistant IA passe en sombre, et sur une page sombre les
  champs de saisie et les boutons des formulaires sont en verre (bouton pointé : pastille rose → bleu).
- Les autres styles de GC Station gardent Cube Browser en plein écran ; son thème peut aussi être « Aurore »
  (réglages du navigateur > Affichage > Thème).

## Nouveautés v213 — le globe des radios, plus rapide et plus doux
- **Chargement bien plus rapide** : l'annuaire est lu en une seule passe (avant : chaque champ était recherché depuis
  le début de la station, 10 fois plus lent) ; la page suivante est demandée pendant qu'on lit la précédente ; la
  lecture prend au plus 5 ms par image (le globe reste à 50 images/s). Pourcentage et barre de chargement.
- **Prêt d'avance** : en entrant dans l'onglet RADIO, les radios gardées sur la carte SD sont relues en fond : le globe
  s'ouvre déjà rempli. Une liste de plus d'une semaine reste affichée pendant sa mise à jour (les radios disparues
  de l'annuaire sont enlevées à la fin).
- **Doublons enlevés** : même adresse, ou même nom au même endroit (une radio en plusieurs débits) : un seul point.
- **Navigation** : inertie (démarrages et arrêts en douceur, réponse progressive du stick), même vitesse à l'écran
  près des pôles, vol en arc d'un point à l'autre (la Terre recule un peu pendant les longs vols), viseur aimanté
  (la radio la plus proche se place au centre quand on lâche le stick), noms des radios proches quand on zoome.
- **Z : chercher** une radio, une ville ou un pays (aussi en français : « Japon », « Allemagne »… sans accents
  ni majuscules à respecter) ; le globe y vole ; L / R : résultat précédent / suivant.
- **État de la radio lancée** : connexion, mise en mémoire, en écoute, ou « ne répond pas (R : une autre radio ici) ».
- **Jour et nuit** : d'après l'heure de la console, la face de nuit est plus sombre et les radios y brillent comme
  des villes.
- **Après le globe** (interface qui accrochait en quittant le globe avec une radio lancée) : en le fermant, tout ce
  qu'il occupait est rendu (carte, points, tables) ; et tant qu'une radio joue, le globe ne charge plus rien en fond
  (réseau et mémoire pour la radio) : le chargement reprend à la même page à la prochaine ouverture.

## Nouveautés v212 — Cube Browser en français, mise à jour réparée
- **Mise à jour (Réglages > Réseau)** : depuis Cube Browser, le programme fait près de 6 Mo et le téléchargement
  le gardait en entier en mémoire avant de l'écrire : il ne restait plus assez de mémoire d'un seul bloc, chaque
  essai échouait (« Téléchargement interrompu (réseau) »). Le fichier est maintenant écrit sur la carte SD au fur
  et à mesure (`.part`), repris là où il s'est arrêté si la connexion coupe, vérifié, puis mis à la place de l'ancien
  (gardé en `.ancien`). Sans carte SD (Dolphin), l'ancienne méthode en mémoire reste.
- **Cube Browser en français** : menus, réglages, messages, aide, clavier, téléchargements (traduction :
  `tools/cb-lang/fr.py` → `source/cb/lang_fr.h`). Réglages du navigateur (roue) > Affichage > **Langue** :
  Français (par défaut) ou English.
- **Accents** : les polices du navigateur ont maintenant les lettres accentuées (Latin-1 : é, è, à, ç, ô, ü, «, °…),
  sans mémoire en plus (même texture 512 × 256 ; `tools/cb-font/make_font.py`). Les pages web gardent leurs accents
  (avant : « e » à la place de « é »), y compris les pages en windows-1252 et les entités HTML (`&ocirc;`…).
- **Clavier** : la touche « #+é » donne une ligne de lettres accentuées (X : majuscules). Le texte tapé part en
  UTF-8 aux sites (recherches, formulaires, assistant IA).
- Réglages du navigateur : les sections qui ne servent pas dans GC Station (réseau, écran, musique MIDI, mises à
  jour de Cube Browser, Swiss) sont cachées ; l'aide n'affiche plus Z et START (pris par GC Station).

## Nouveautés v211 — pochette sans bande noire (16:9)
- Style Aurore, lecteur MUSIQUE, en 16:9 : l'ombre et le liseré de la pochette étaient étirés en largeur (et
  l'ombre décalée vers le bas) alors que la pochette reste carrée : une bande sombre apparaissait autour et
  en haut du cadre. Ils suivent maintenant la pochette.

## Nouveautés v210 — l'onglet WEB devient Cube Browser ; globe : radios plus vite
- **Le navigateur de l'onglet WEB est maintenant Cube Browser** (d'Arial-Bold, licence MIT, `source/cb`),
  adapté pour vivre dans GC Station : même écran, même réseau, mêmes certificats, même carte SD
  (réglages du navigateur : SD:/gc-mp3/web). Pointeur au stick, A : cliquer, B : retour, X : adresse ou
  recherche, Y : favoris, stick C ou L / R : défiler, la croix saute de lien en lien, la roue dentée : les
  réglages du navigateur. **Z change d'onglet et START ouvre le menu de GC Station**, comme partout (START
  valide quand le clavier du navigateur est ouvert). La porte de la barre ramène à l'onglet MUSIQUE.
- Les pages de GC Station s'ouvrent dans Cube Browser : **Assistant IA** (`gc:ia`, la réponse s'affiche
  toute seule en haut de la page) et **Musique libre** d'archive.org (`gc:archive`) ; ce sont deux tuiles de
  la page d'accueil. « Ouvrir sur la télé » depuis le téléphone ouvre la page dans Cube Browser.
- Les fichiers téléchargés vont dans SD:/downloads (les MIDI dans SD:/retro) et la musique les voit tout
  de suite.
- Pas repris de Cube Browser : son lecteur MIDI, le Game Boy Advance, ses mises à jour et celles de Swiss
  (GC Station a les siens). Le texte de Cube Browser est en anglais et ses polices n'ont pas les accents
  (« é » s'affiche « e ») : à faire.
- **Globe des radios** : l'annuaire arrive par petites pages (300 radios) ; on peut viser et écouter dès la
  première page, la suite continue (et reprend où elle en était si on ferme le globe). Le curseur au stick
  de GC Station ne s'affiche plus par-dessus le globe (les « cases vides » en haut).

## Nouveautés v209 — globe : bonne forme en 16:9, il tourne pendant le chargement
- En 16:9, la carte débordait de la Terre (continents dessinés en dehors du disque) : la Terre, la carte, les
  méridiens et les radios utilisent maintenant la même correction que les autres ronds de GC Station.
- Le globe tourne et zoome tout de suite, même pendant le chargement de l'annuaire (avant : bloqué tant que les
  radios n'étaient pas arrivées).
- Annuaire demandé compressé (gzip : 5 à 8 fois moins à recevoir) ; la ligne de chargement montre la page en
  cours et les Ko reçus.

## Nouveautés v208 — carte du monde sur le globe, globe plus fluide
- **Le globe des radios a une vraie carte du monde** : continents et lacs (Natural Earth 1:50m, domaine public,
  `source/globe_map.h` fait par `tools/carte/globe_map.py`), éclairés de face, avec les radios en points dorés
  par-dessus. La carte pèse 62 Ko dans le programme (1 Mo une fois ouverte, rendu à la fermeture).
- Plus fluide, mesuré en 576p 50 Hz (simulateur) : la carte et les stations gardées sur la carte SD sont lues
  par un fil à part (ouverture : 239 ms figés avant, plus rien maintenant) ; grille et méridiens calculés une
  seule fois ; radios visées trouvées pendant le dessin des points ; étoiles dessinées d'un coup ; l'espace
  n'est plus dessiné quand la Terre remplit l'écran. Dessin du globe : 5,4 ms par image en moyenne (6,5 avant),
  99,85 % des images à l'heure en tournant et en zoomant.

## Nouveautés v207 — jukebox des jeux, globe des radios, compagnon
- **Jukebox des musiques de tes jeux GameCube** : dans la fiche d'un jeu (onglet JEUX), **bas** sur la croix.
  Les musiques rangées dans l'image du jeu (.iso, .gcm, .ciso) s'affichent dans l'onglet RÉTRO, avec le jeu en
  album, et se jouent directement depuis l'image (rien n'est copié). Formats : **AST** (nouveau : Mario Kart
  Double Dash, Zelda Twilight Princess…), **HPS** (Smash Melee, Kirby Air Ride), **DSP** (paires _L/_R) et
  **ADP**. X dans RÉTRO : retour à toutes les musiques rétro. Les jeux dont la musique est jouée note par note
  (séquences) n'ont rien à lire : un message le dit. Les .ast se lisent aussi depuis SD:/retro.
- **Globe des radios** : onglet RADIO > « Globe des radios (le monde) ». La Terre en 3D ; chaque point lumineux
  est une radio (environ 4000 de l'annuaire radio-browser, placées à leur ville), les continents se dessinent
  avec elles. Stick : tourner ; stick C : zoom ; A : écouter la radio visée ; L / R : les autres radios du même
  endroit ; X : une radio au hasard (le globe y voyage puis elle joue) ; Y : la garder dans tes radios.
  Les stations sont gardées sur la carte SD (relues chaque mois) ; lecture de l'annuaire par petits morceaux,
  sans figer l'écran.
- **Mon compagnon** (START > Outils) : un petit être qui éclôt puis grandit avec ce que tu écoutes (œuf, bébé,
  petit, grand, légende), prend la couleur et l'accessoire de ce que tu écoutes le plus (casque : musique,
  antenne : radio, lunettes pixel : rétro, manette : musiques de jeux, nœud papillon : podcasts), danse sur la
  musique en cours, dort la nuit et s'ennuie après des jours sans musique. A : caresser ; X : le renommer.
  Une bulle prévient quand il éclot ou grandit. Gardé dans SD:/gc-mp3/reglages/compagnon.txt.
- Essais (simulateur) : faux jeu avec AST, HPS, DSP en paire et ADP (`tools/pc-sim/demo-jukebox.py`), son
  vérifié (bonnes notes à gauche et à droite) ; globe avec un faux annuaire de 3600 radios ; AddressSanitizer
  sans erreur. Pas encore essayé avec de vraies images de jeux ni le vrai annuaire depuis la GameCube.

## Nouveautés v206 — assistant IA
- **Assistant IA** : onglet WEB > accueil > « Assistant IA » (page `gc:ia`). Tu poses une question (clavier, ou
  depuis le téléphone : onglet TÉLÉPHONE > page `/ia`), la réponse s'affiche comme une page (titres, listes, gras,
  code) ; la conversation suit (les questions précédentes sont envoyées avec). La demande part en tâche de fond :
  la musique et l'écran continuent. Si tu changes d'onglet, une bulle prévient quand la réponse est arrivée.
- Fournisseurs : **Anthropic** (Claude, modèle par défaut `claude-haiku-5-5`) ou tout service **compatible OpenAI**
  (OpenAI, Mistral, OpenRouter…), et **Ollama / LM Studio** sur un PC de la maison (pas de clé, juste l'adresse).
  Il faut une clé API à toi : elle se colle depuis le téléphone (page `/ia`) ou dans
  SD:/gc-mp3/reglages/assistant.txt (`fournisseur`, `cle`, `modele`, `adresse`).
- Erreurs expliquées (clé refusée, plus de crédit, service surchargé, serveur muet) avec le message du service.
- Le client web sait maintenant envoyer des requêtes POST avec un corps JSON et des en-têtes en plus.
- Essai (simulateur, faux service local) : formats OpenAI et Anthropic, 4 questions à la suite, erreur 401, AddressSanitizer sans erreur.
  Pas encore essayé contre le vrai service depuis la GameCube.

## Nouveautés v205 — banque de sons MIDI, page qui ne saute plus
- **MIDI avec une banque de sons (SoundFont .sf2)**, en option : mets-la dans SD:/gc-mp3/soundfont.sf2 (ou un
  .sf2 dans SD:/retro), 3 Mo au plus. Les instruments sont alors de vrais sons enregistrés (TinySoundFont, MIT,
  `source/tsf.h`) ; sans banque, le synthétiseur intégré. La banque est libérée dès qu'une autre musique joue.
  Essai : 15 instruments + batterie avec une banque de piano, aucun trou dans le son.
- Web : quand la page se remet en page (images qui arrivent, feuilles de style), ce qu'on lit reste à sa place
  (chaque mot connaît sa place dans le document ; le nouveau moteur l'avait perdue).

## Nouveautés v204 — téléchargements qui reprennent
- Un téléchargement coupé (connexion perdue, serveur muet 30 s) **reprend où il s'était arrêté** au lieu de
  repartir de zéro (en-tête `Range`, jusqu'à 5 reprises), si le serveur le permet ; sinon le message d'avant.
  Essai : fichier de 3 Mo coupé à 40 % par le serveur de test, reçu en entier et identique à l'octet près.
- Banc d'essai : le serveur de test gère `Range` (réponse 206) et `/coupe/...` (connexion coupée à 40 %).

## Nouveautés v203 — musiques MIDI, finitions du web
- **MIDI** (.mid, .midi, .kar, .rmi) dans l'onglet RÉTRO, avec un synthétiseur intégré (`source/midi.c`, pas de
  banque de sons à fournir) : une forme d'onde et une enveloppe par famille d'instruments General MIDI, batterie
  synthétisée (canal 10), 32 voix, volume / expression / panoramique / pédale / pitch bend, avance rapide. Les
  .mid téléchargés depuis le web arrivent aussi dans RÉTRO. Essai : 15 instruments + batterie, 32 voix, aucun trou
  dans le son, image à 50 / s.
- Web : les marges d'un bloc qui a son propre contexte (grille, flex, cadre avec `overflow`) ne disparaissent
  plus ; les boutons `<button>` ont l'aspect d'un bouton ; `<ol type="I">`, `type="a"`... ; les inline-block
  gardent leur marge de gauche ; les champs de saisie ne dépassent plus de leur colonne ; les cases de tableau
  en CSS (`display: table-cell`) sont alignées en haut, comme dans les navigateurs.
- Simulateur : `PCSIM_MIDI=fichier.mid` (lecture complète sans son, pour les essais).

## Nouveautés v202 — identifiants, .zip, liens dans l'ordre de l'écran
- **Identifiants enregistrés** : après l'envoi d'un formulaire avec un mot de passe, la page suivante propose
  « Enregistrer l'identifiant … pour site ? Oui / Non ». La fois suivante, le formulaire de connexion de ce site
  est déjà rempli. Liste dans SD:/gc-mp3/reglages/identifiants.txt (mot de passe brouillé, pas chiffré : la
  carte SD reste à protéger) ; « Effacer les identifiants » sur l'accueil du web.
- **Archives .zip** : dans l'onglet TÉLÉCHARGEMENTS, A sur un .zip le décompresse à côté, dans un dossier du même
  nom (dossiers de l'archive gardés, « .. » neutralisé ; fichiers jusqu'à 12 Mo).
- **Croix** : les liens se suivent dans l'ordre où on les voit (haut → bas, gauche → droite), même quand la page
  les place autrement que dans son HTML (colonnes, flottants).
- Banc d'essai : le serveur de test répond aux formulaires envoyés (POST).

## Nouveautés v201 — nouveau moteur de pages web
Le navigateur (onglet WEB) a un nouveau moteur, écrit pour GC Station (l'affichage, le curseur et les touches ne
changent pas) :
- **La page est lue en arbre** comme dans un vrai navigateur (`source/web_dom.c`) : fermetures implicites
  (`<p>`, `<li>`, cases de tableau…), contenu de `<noscript>` affiché (pas de JavaScript ici).
- **CSS bien plus complet** (`source/web_style.c`) : sélecteurs enfant `>`, frères `+` `~`, attributs `[a^=b]`…,
  `:first-child`, `:nth-child(2n+1)`, `:not()`, `:is()`, `::before` / `::after` ; marges, padding, bordures,
  largeurs (px, em, rem, %, vw…), `min-width` / `max-width`, `calc()`, `min()`, `max()`, `clamp()`, variables
  `var(--x)`, `box-sizing`, `white-space`, italique, chasse fixe, majuscules, exposant / indice.
- **Mise en page en boîtes** (`source/web_box.c`) : marges qui se fondent comme dans les navigateurs, centrage
  (`margin: auto`), **tableaux à largeur automatique** (les colonnes prennent la place de leur contenu), tableaux
  dans les tableaux, cases sur plusieurs colonnes / lignes, **flottants** (le texte contourne les images),
  **inline-block**, **flex** (lignes, retour à la ligne, grow / shrink, justify / align, gap), **grilles**
  (colonnes en `fr` / px), puces et numéros de liste (chiffres, lettres, romains), images des cadres à
  proportions fixes.
- **Polices** : italique (lettres penchées), barré, police à chasse fixe pour `<pre>` / `<code>`, une taille
  intermédiaire (18) pour les sous-titres ; caractères en plus partout : lettres latines accentuées (ā, ł, ő,
  č…), vrais guillemets “ ”, tirets – —, points de suspension …, puces •, €, ™, flèches, coches.
- **Page faite pour JavaScript** (presque vide ici) : un bandeau le dit et propose le **mode texte** (la page
  sans la mise en forme du site, ce qui montre souvent le texte caché) ; le même bandeau ramène à la page normale.
- Si la mémoire manque, l'ancien moteur prend le relais.
- Pile du fil de mise en page en fond : 128 Ko (la mise en page est récursive).
- Banc d'essai : `tools/pc-sim/websrv/vraies-pages.sh` (130 vraies pages : Wikipedia, Le Monde, BBC, Guardian,
  NYTimes…, mises en page sans écran, rendu en PNG) ; `PCSIM_WEBDUMP=page.html` sur le simulateur. Toutes les
  pages et 170 pages abîmées exprès passent sous AddressSanitizer sans erreur.

## Nouveautés v200 — finitions de l'interface (retours de r/GamecubeHacks)
- Traits fins : au moins un point de l'écran d'épaisseur. Avec l'image réduite (Affichage > taille de l'image sous
  100 %), un trait de 1 faisait moins d'un point et des morceaux des cadres et des séparateurs disparaissaient.
- Lecteur (Aurore) : l'ombre de la pochette est douce et centrée (avant : un bloc noir décalé à droite) ; le spectre
  sous la pochette a la largeur de la pochette.
- Barre des onglets (Aurore) : la rangée est placée entre le logo et l'heure ; l'onglet de gauche ne passe plus
  sous « GAMECUBE MEDIA CENTER ».
- Simulateur : une arête partagée par deux triangles n'est dessinée qu'une fois, comme sur la console (il montrait
  des traits en croix et des lignes verticales qui n'existent pas sur la GameCube).

## Nouveautés v199 — petits défauts de l'espace jeux
- Un jeu qui sort du filtre (retiré des favoris depuis la fiche, par exemple) : la vitrine se place sur son voisin
  au lieu de revenir au premier jeu.
- Filtre vide (« Aucun favori ») : la barre d'aide ne montre plus que Z Filtre, R Liste et B Retour.

## Nouveautés v198 — disque de la console dans l'espace jeux
- Sur la case « Disque de la console » (filtre Tout), la barre d'aide n'affiche plus X Cheats ni L Favori, qui ne
  servent pas pour le vrai lecteur. Fiche du disque et « Lancer le disque de la console ? » vérifiées.

## Nouveautés v197 — fiche d'un jeu dans l'espace jeux
- Y dans l'espace Jeux GameCube : la fiche reste dans l'espace (même barre en haut, couleurs d'Aurore). À gauche la
  grande jaquette (lueur, reflet) ; à droite le nom, des étiquettes (ID, région, taille, disque, CISO, favori), la
  description, la capture du jeu, les lancements, la dernière partie, le temps de jeu et les cheats.
- Gauche / droite : jeu suivant ou précédent **du filtre**, la fiche glisse. Touches : A jouer, X cheats,
  Y réglages, L favori, R analyse, B retour à la vitrine.

## Nouveautés v196 — espace Jeux GameCube (vitrine 3D)
- Onglet JEUX / APPS, A sur « Jeux GameCube » : un espace à part, aux couleurs d'Aurore. Les jaquettes sont en
  perspective, avec un reflet au sol et une lueur rose → bleu ; le nom du jeu et ses infos (ID, région, taille,
  temps de jeu, favori) en dessous ; un petit logo en haut à gauche.
- En haut, la même barre que les onglets, mais avec les filtres : Récents, Tout, Favoris, Plus joués, A à Z,
  Applis. **Z** : filtre suivant (gardé dans `jeux_filtre`).
- Touches : A jouer (« Lancer ce jeu ? »), Y fiche, X cheats, L favori, R liste, B retour.
- Les applis homebrew sont présentées comme les jeux (filtre Applis : A lancer, Y actualiser) ; « Mes applis »
  n'est plus sur l'accueil de JEUX / APPS (deux barres : Jeux GameCube, Cartes mémoire).

## Nouveautés v195 — radio et musique fluides (la boucle ne touche plus la carte SD)
- Radio : ouverture par étapes sans attendre (connexion, requête, en-têtes, listes .m3u / .pls) : la boucle principale
  n'attend plus la réponse du serveur (avant : jusqu'à 1,5 s, « radio »).
- Musique : durées et étiquettes lues par un fil de fond (avant : pendant le dessin de la liste, jusqu'à 3,9 s,
  « dessin ») ; pochettes et logos cherchés et décodés par ce fil ; changement de morceau à la main (R / L / A)
  préparé par le fil de décodage pendant que le morceau en cours continue (avant : « manette » jusqu'à 1,6 s).
- Écritures sur la carte SD par le fil (avant : dans la boucle) : position de reprise (toutes les 30 s),
  bibliothèque, statistiques, notes, historique des titres de radio, pochettes et paroles téléchargées,
  cache des pochettes, lanceur de jeux et retour en jeu mis à jour.
- Mode soirée / karaoké : la durée du morceau n'est plus relue sur la carte SD à chaque image.
- `fluidite.txt` : nouvelles lignes « processeur pris (réseau) » (temps où la boucle n'a pas eu le processeur)
  et « attente du décodeur ».
- Simulateur : banc d'essai radio / musique avec un faux Internet local (`tools/pc-sim/net-test`).

## Nouveautés v194 — moins de pauses (réglages, connexions HTTPS)
- Réglages (`gc-mp3.txt`) : le texte est préparé en mémoire et n'est réécrit que s'il a changé ; l'enregistrement
  automatique (2 s après un changement, par exemple la dernière radio écoutée) est écrit par un fil de fond
  (fichier `gc-mp3.tmp` puis renommé ; repris au démarrage si la console s'est arrêtée en route).
  `profil.txt` n'est réécrit que s'il change. Avant : jusqu'à 860 ms d'écran figé (« réglages »).
- Connexions HTTPS : la boucle principale n'attend plus le verrou de mbedTLS pendant qu'une poignée de main
  se calcule en arrière-plan (fermeture d'une connexion remise à plus tard, reprise d'une connexion prête et
  tickets de session sans verrou). Avant : pauses d'environ 1 s (« radio », « attente de l'écran »).

## Nouveautés v193 — radio sans saccades
- La connexion à une radio (recherche DNS, connexion, poignée de main HTTPS, redirections) se fait en tâche de fond,
  comme les pochettes et les paroles : l'écran ne se fige plus quand on lance une station ni pendant les reconnexions
  automatiques (fluidite.txt : jusqu'à 1,7 s avant). Les podcasts et les fichiers M4A repris à une position gardent
  la connexion directe.

## Nouveautés v192 — écran 50 Hz (576p / 576i)
- Compteur de fluidité (START > performances) adapté au 50 Hz : une image ratée = plus de 24 ms (au lieu de 20 ms,
  qui comptait presque toutes les images comme ratées en 50 Hz) ; vert à 48 images/s ; `fluidite.txt` indique la fréquence.
- Barres de chargement (web, téléchargements) et curseur du clavier à la même vitesse en 50 et 60 Hz.
- Simulateur PC : écran à 50 Hz en mode PAL, `PCSIM_COMPOSANTE=1` pour le câble composante (576p).

## Nouveautés v191 — profils, limite de jeu, récap, économiseur, collection
- main.c : `gp_path` (fichiers du profil : gc-mp3/mes-parties pour le principal, gc-mp3/profils/<nom> sinon) pour
  jeux_stats, jeux-favoris, jeux_recents, succes, gc-mp3-jeux (records). profils.c : liste, « Qui joue ? »,
  profil.txt (carte, limite), changement de profil (stats, favoris, succès relus), `KB_PROFILE`.
- Limite : jour.txt du profil (date|secondes, ajouté dans `gl_stats_add_play`) ; `pj_launch_check` dans `game_boot` et
  `game_boot_auto` ; ligne du temps restant dans « Lancer ce jeu ? ».
- recap.c : écran `TL_RECAP` (2 pages), `/api/recap` + page `/recap` ; page `/collection` (jaquettes CMPR décodées dans
  la page, tri, impression).
- Économiseur `SV_JAQUETTES` (`saver_covers`, gclib.c) : colonnes qui défilent, une jaquette lue par image, fondu par
  l'alpha des sommets (`draw_tex_fit_shade`).
- Simulateur : profil choisi au démarrage, limite (2 min restantes, puis lancement refusé), récap (console et page),
  économiseur, collection ; banc natif : 33 TOUT OK.

## Nouveautés v190 — téléphone (cartes, jeux), guide, licence
- tab_phone.c : `/api/cards`, `/api/cardfile/<carte>`, `/api/gci/<carte>/<n>/<nom>.gci`, `/api/gciimp`, `/api/cardput` ;
  envoi par `/upload?dir=gci` (rend le nom gardé). Travail fait par la boucle (extras73.c : `x73_cards_build`,
  `x73_vj_pump`) : vc_load / vc_export / vc_import ; carte vierge formatée avant d'y ajouter ; carte remplacée : taille
  vérifiée (512 Ko à 16 Mo), l'ancienne en `.bak`. Page : onglet « Cartes ».
- Onglet Jeux du téléphone : JSON avec favori et dernier lancement ; recherche et tri dans la page ; Pulse =
  `game_boot_auto` (comme la fiche).
- tools.c : écran `TL_GUIDE` (4 pages), START > Outils > Guide de démarrage ; main.c : `guide_vu`, montré seulement
  sans gc-mp3.txt (carte SD neuve).
- LICENSE (MIT, code de GC Station), CREDITS.md (Swiss GPL-2.0 pour le lanceur de secours, bibliothèques).
- Simulateur : API testées (ajout, liste, téléchargement .gci / .raw, remplacement), page du téléphone et guide vus.

## Nouveautés v189 — applis : lancement réparé
- gclib.c : avec le retour (v187), table des sections en 0x80002D00 et arguments en 0x80002E00 (avant : 0x80002C80,
  sous les 132 octets du programme de copie en 0x80002C00 : sa fin écrasait la 1re ligne de la table → plantage,
  redémarrage). Vérification à la compilation de la taille du programme de copie.
- Banc natif : `emu_appstub.py` refait le lancement (copie, table, arguments, retour en 0x80001800) d'après les adresses
  de gclib.c : la v188 plante, la v189 lance l'appli. 33 TOUT OK.

## Nouveautés v188 — réglage de la vérification des jeux
- main.c : `gl_auto` (`jeux_actualiser`), ligne `MI_GLAUTO` dans Réglages > Jeux ; gclib.c : `gl_quick_changed` seulement
  si `gl_auto`. Simulateur : réglage changé et gardé dans gc-mp3.txt.

## Nouveautés v187 — retour à GC Station en quittant une appli
- source/natres/natret.c (tools/natret.ld, 0x80001800 - 0x80002C00, compilé en -Os) : signature « STUBHAXX » en
  0x80001804, que libogc cherche dans `exit()` / `__reload` avant de redémarrer la console. Arrête le son, le DSP,
  le dessin, redémarre la carte SD (SPI, comme Pulse), relit le .dol de GC Station d'après ses morceaux (au plus 48,
  préparés par `nat_ret_prep`) et le lance ; sinon redémarrage.
- gclib.c : `gl_ret_app` (applis et .dol des derniers lancés) ; avec le retour, programme de copie, table et
  arguments en 0x80002C00 / 0x80002C80 / 0x80002D80 (avant : 0x80001800, inchangé pour le reste et pour Pulse).
- Banc natif : `emu_natret.py` (carte SDHC sur SD2SP2, carte SD au slot A, slot B ; .dol en 3 morceaux) : 32 TOUT OK.

## Nouveautés v186 — nouveaux jeux vus tout seuls
- gclib.c : `gl_quick_changed` à chaque ouverture de la liste : même parcours que `gl_walk` (mêmes dossiers sautés), mais
  seulement nom + taille (stat), comparés à la liste gardée ; nouveau fichier ou jeu disparu → `gl_scan`. Les .iso qui ne
  sont pas des jeux GameCube sont notés (`#R` dans cache/jeux/liste.txt) pour ne pas tout refaire à chaque fois.
- Simulateur : jeu ajouté → liste refaite (122), rien de changé → rien refait, jeu enlevé → (121).

## Nouveautés v185 — captures et musique en jeu enlevées
- Pas de résultat sur la console : natres.c / natres.h remis tels qu'en v171 (avant v175 : `shot_take`, L + R + Z dans
  `igr_poll2`, `mus_block` dans `st_block`, `mpos` dans `stsnap_t`, en-tête 0x1F0 de nouveau en entier dans le journal).
- natif.c : `nat_shot_prep`, `nat_shots_convert` (PNG), `na_mus_*` (conversion ADP) enlevés ; main.c : `pcm_file_decode`,
  `pf_hook`, `PCSIM_MUSCONV` enlevés (`trk_open` gardé) ; `natif/captures.bin` effacé au démarrage.
- tools.c : succès photo1 / photo20 enlevés. Banc natif remis comme en v179 : 29 TOUT OK.

## Nouveautés v184 — supprimer un jeu
- jeux_cfg.c : ligne `JC_N` (en plus des réglages) quand le jeu est sur la carte SD (`jc_can_del` : pas le lecteur,
  pas le GC Loader, pas la SD en mémoire) ; A deux fois en 6 s (`jc_del_arm`) ; `jc_delete` : fichier effacé, dossier
  parent enlevé s'il est vide (pas un dossier de la racine), liste refaite (`gl_scan`), message.
- Simulateur : Crash.iso supprimé depuis la fiche, liste 122 → 121.

## Nouveautés v183 — lanceur rangé reconnu
- lanceur.c : `ld_new_paths` d'après le numéro du lanceur installé (`lanceur.txt` ≥ 597) ; la v182 cherchait le texte
  « gc-mp3/lanceur/retour.txt » dans lanceur.dol, mais le lanceur est compressé (packer de Swiss) : jamais trouvé.
  `lanceur/chemins.txt` (v182) effacé.

## Nouveautés v182 — dossier gc-mp3 rangé
- main.c : `GCD_REG`, `GCD_BIB`, `GCD_PARTIES`, `GCD_LOG`, `GCD_LD` ; table `gcd_moves` (ancien → nouveau) appliquée au
  démarrage par `gcd_tidy` (si les deux existent : le plus récent gardé) ; `SD:/gc-mp3-sauvegarde.txt` → `reglages/`.
- Restauration d'une ancienne sauvegarde : noms d'avant traduits (`gcd_newrel`) ; Salon : `/s/get` d'un ancien chemin
  (console d'avant) servi depuis le nouveau ; Dolphin (SD en mémoire) : réglages des sous-dossiers gardés sur la carte.
- Lanceur de secours (mkloader.py) : `gc-mp3/lanceur/retour.txt` (sinon l'ancien) et `gc-mp3/lanceur/lanceur.log`.
  GC Station regarde si le lanceur installé connaît le nouveau chemin (résultat gardé dans `lanceur/chemins.txt`) ;
  sinon il écrit aussi `gc-mp3/retour.txt`. Journal du lanceur lu aux deux endroits.
- Simulateur : carte d'avant rangée au démarrage ; radios, podcasts, succès, dernier jeu, records relus.

## Nouveautés v181 — ta musique dans les jeux : tous les formats
- main.c : l'ouverture d'un morceau (`trk_open`) est sortie de `start_track_u` ; `pcm_file_decode(chemin, hook, tick)`
  décode tout un fichier avec les décodeurs du lecteur (minimp3, dr_flac, stb_vorbis, WAV, Helix AAC, GME, libxmp,
  formats GameCube) et envoie les échantillons à `hook` (`pf_hook` dans `push_frame`) au lieu de les jouer.
- natif.c : `na_mus_find` prend `musique-jeux/<ID>.<n'importe quel format audio>` (pas le .adp produit) ;
  rééchantillonnage vers 48 kHz pour toute fréquence (96 / 192 kHz compris) ; au plus 15 min ; `<ID>.src` (nom, taille,
  date) : reconverti si le fichier change. Mesuré au simulateur (`PCSIM_MUSCONV=<ID>`) : FLAC / WAV 33,6 dB, OGG 32,9,
  M4A 31,1, MP3 25,3 ; module MOD converti ; lecture normale de l'onglet MUSIQUE inchangée (0 trou sur 162 s).

## Nouveautés v180 — tests des nouveautés, anglais
- Banc natif (`tools/natif-test/run.sh`, 31 tests) : mode `shot` (registres VI simulés, image 640 × 480 en 0x00700000,
  L + R + Z sur la manette, place de `natif/captures.bin` relue : en-tête SHOT et image identique) et mode `mus`
  (`musique-jeux/GE2E01.adp` joué à la place du disque, échantillons recalculés depuis le fichier). `mkfat.py` :
  `MKFAT_SHOTS`, `MKFAT_MUS` ; émulateur : mémoire aussi en 0xC0000000 (sans cache), comme sur la console.
- lang_tab.h : succès, mode soirée, accueil JEUX / APPS, banc d'essai, Outils (catalogue, historique, cheats),
  message de mise à jour ; « 0 wins » au pluriel en anglais. Succès « Arcade » : texte raccourci (coupé avant).
- Simulateur : accueil JEUX / APPS, Outils, Succès, Mode soirée vérifiés dans les 4 styles et en anglais.

## Nouveautés v179 — ta musique à la place de la musique du disque
- natif.c : `gc-mp3/musique-jeux/<ID>.mp3` → `<ID>.adp` (minimp3, rééchantillonné à 48 kHz, ADPCM du disque : pour chaque
  voie et bloc de 28 échantillons, les 4 filtres et le décalage estimé ± 1 essayés en boucle fermée, le meilleur
  gardé ; ~25 dB sur un balayage de fréquences) ; refait si le .mp3 est plus récent ; table des morceaux dans le
  résident (`mus_tab`, `mus_n`, `mus_size`), ligne dans dernier-lancement.txt.
- Résident : `st_block` prend le bloc suivant du fichier (en boucle) à la place de celui du disque ; la position du
  disque avance comme avant (le jeu voit sa musique continuer) ; `mus_pos` gardé dans l'état sauvé du décodeur.
  En-tête du résident : 0x210.

## Nouveautés v178 — mode soirée
- tools.c : écran TL_SOIREE ; jeu tiré parmi les favoris (sinon tous les jeux de la liste), manettes mélangées ;
  victoires dans `gc-mp3/soiree.txt` (« joueurs|v1|v2|v3|v4 ») ; « Lancer ce jeu » passe par `gclib_open_recent`.

## Nouveautés v177 — succès
- tools.c : `sc_defs` (14 succès : lancements, jeux différents, temps de jeu total et sur un jeu, captures dans
  photos/captures, favoris, records des mini-jeux) ; débloqués dans `gc-mp3/succes.txt` (« id|date ») ; vérifiés en
  ouvrant JEUX / APPS (`sc_check`, message) et dans l'écran START > Outils > Succès (progression).

## Nouveautés v176 — dernier jeu et temps de jeu sur l'accueil de JEUX / APPS
- tabs_extra.c : `jeux_recents.txt` (1re ligne : dernier jeu lancé) et `jeux_stats.txt` (temps de jeu par jeu) relus à
  l'ouverture de l'onglet ; ligne sous les cartes ; X → `gclib_open_recent` (gclib.c) : la liste des jeux, la fiche
  et « Lancer ce jeu ? » du dernier jeu.

## Nouveautés v175 — captures d'écran en jeu
- Résident : L + R + Z tenus (sans A + START) → l'image affichée (XFB : adresse dans TFBL, largeur et pas d'après PICCONF,
  lignes d'après VTR ; lue sans le cache) est écrite dans la place suivante de `natif/captures.bin` (8 places de
  1 + 1440 secteurs, d'un seul morceau, préparé par GC Station au lancement), en-tête « SHOT » écrit en dernier.
  En-tête du résident agrandi à 0x200 (`shot_sec`, `shot_max`, `shot_n`, `shot_last`) ; le journal n'en écrit que les
  0x1F0 premiers octets (`NATRES_JHDR`), format du journal inchangé.
- GC Station : au démarrage, chaque place remplie devient `photos/captures/<ID>-<n>.png` (YUV 4:2:2 → RGB, PNG
  compressé par deflate.c), puis la place est vidée.
- Banc (tools) : police de la console faite ici (romfont.py, glyphes vides, Yay0) servie sur l'EXI comme la ROM ;
  lectures hors export notées avec le nom du fichier ; message d'erreur du jeu (OSPanic) ; mode vidéo du jeu ; jeux
  en parallèle ; cache verrouillé du Gekko.

## Nouveautés v173 — banc d'essai : rapide pour tous, complet pour un jeu
- Options > « Banc d'essai rapide (tous) » : en-tête, apploader, main.dol et FST seulement (la taille du disque est
  quand même calculée d'après la FST) ; l'export complet (petits fichiers, début des gros, 16 Mo au plus) est fait
  par R sur la fiche d'un jeu, avec l'analyse Pulse. Fenêtre Options un peu plus haute (l'aide touchait la 5e ligne).

## Nouveautés v172 — banc d'essai automatique des jeux
- GC Station : Jeux > Options > « Banc d'essai (export) » écrit `gc-mp3/banc/<ID><disque>.gcb` pour chaque jeu
  (format GCB1, gros-boutiste : en-tête 0x5C — « GCB1 », version, ID sur 8, taille de l'ISO, nombre de morceaux,
  octets de données, titre sur 64 — puis {position, longueur} par morceau, puis les données) : en-tête et apploader,
  main.dol, FST, fichiers de 256 Ko au plus en entier, puis 64 Ko du début des autres, 16 Mo au plus ; `index.txt`.
- PC : `tools/banc/banc.py dossier [secondes]` : ISO creuse (gcb.py), carte SD FAT32 creuse avec une carte mémoire
  formatée comme le SDK (mkfat_sparse.py, clusters de 32 Ko, ISO au cluster 70000), lancement préparé par h2, puis
  `emu_game.py` ; rapport.txt (images, commandes de la carte, arrêt du jeu) et le détail par jeu dans `essais/`.
- emu_game.py : images lues à la demande (mmap), SRAM de la console émulée (canal 0, périphérique 1 : ID des cartes à
  0 et sommes justes), `__OSInitAudioSystem` et PPCHalt (arrêt du jeu : OSPanic / OSFatal) reconnus dans tout jeu.

## Nouveautés v171 — Crash : la carte virtuelle dans le journal du jeu figé
- Nouveau banc `tools/natif-test/emu_game.py` : le VRAI programme d'un jeu (préparé par GC Station, avec Pulse)
  tourne dans l'émulateur PowerPC : système du jeu, interruptions (PI, image, dessin, DSP, son), horloge et
  décrémenteur, boîtes aux lettres du DSP, mémoire ARAM, instructions « paired single » du Gekko faites à part,
  registres propres au Gekko. `EMU_CALL` fait appeler une fonction du jeu dans sa tâche, `EMU_WATCH` suit des
  fonctions. Unicorn corrigé (`uc_patch.py`, variable `UC_NO_MEM_EXIT`) : un arrêt juste après un accès mémoire
  refaisait l'instruction (lwz r5,0(r5) relu avec r5 déjà chargé).
- Crash GCBP7D (main.dol) : le montage de la carte virtuelle par le SDK du jeu (CARDMount 800F2034, DoMount 800F18E8,
  __CARDRead 800F5360, CARDInit 800EFD54) réussit dans le banc : 84 commandes, 5 blocs, vérification ; aussi avec un
  vrai lecteur lent, une grosse lecture du disque en même temps, et sans le lecteur. Le blocage vu sur la console
  dépend donc de ce qui n'est pas dans le banc.
- Jeu figé (v169) : le 2e secteur du journal porte à la place des textes l'état de la carte virtuelle (« VCRD » en
  0x110 : CSR, interruptions en attente, CR, commande, adresse, réveil demandé, lecture en fond, vrai lecteur,
  masques du jeu, PI, vrais registres EXI, fonctions d'interruption), écrit dans `journal-jeu.txt`.

## Nouveautés v170 — copie du main.dol à l'analyse
- Le journal figé de la v169 (Crash Bandicoot GCBP7D) : tâche principale (priorité 16) en attente dans la
  synchronisation de la carte mémoire du SDK (800F009C : tableau de 2 blocs de 0x110 en 802372A0, résultat -1 =
  occupé, -128 pour un mauvais slot), appelée par 800F2038 ; carte virtuelle : 38 commandes, dernière 52 (lecture) :
  le montage (5 blocs système de 8 Ko lus par morceaux de 512 octets) s'arrête vers la 3e. Essai avec la carte
  virtuelle coupée : le jeu passe le logo et joue (1281 commandes au lecteur, musique, A+Z+START).
- L'analyse Pulse (R sur la fiche) écrit `natif/<ID>-main.dol` : le code de la carte mémoire de ce SDK, pour
  trouver la fin de lecture jamais donnée au jeu.

## Nouveautés v169 — jeu figé noté automatiquement par Pulse
- Crash Bandicoot GCBP7D (v167 : il démarre) se fige sur son logo, toujours au même point : 569 commandes au lecteur,
  25775 secteurs, 73055 blocs de musique du disque (42,6 s), dernière commande E2 01 (position de la musique) ; puis
  plus de lecture des manettes (interruptions SI arrêtées vers 3300) ni de commande, l'image (VI) continue.
  A+Z+START n'a pas pu servir (Z jamais vu).
- `freeze_check` (natres.c, depuis le crochet de l'image) : SI et commandes au lecteur inchangés depuis 6 s alors
  qu'il y avait eu au moins 600 lectures des manettes et 300 images : une seule fois, la tâche du SDK en attente la
  plus prioritaire (file des tâches actives en 0x800000DC ; OSThread : état 0x2C8, priorité 0x2D0, suivante 0x2FC)
  est notée comme un plantage (exception FE : registres, appels, pile, photo de la mémoire) ; sinon le code
  interrompu par l'image. Journal : « Jeu figé : où en était-il ».

## Nouveautés v168 — version d'essai de la mise à jour
- Aucun changement de code : nouveau numéro de build pour essayer START > Mise à jour sur une autre console.

## Nouveautés v167 — BSS remis à zéro avant le saut (Crash Bandicoot GCBP7D)
- Code du point d'entrée (journal de la v162) désassemblé : le `__start` de GCBP7D appelle la remise à zéro
  (`__fill_mem` en 800DB25C : r3 = destination, r4 = valeur, r5 = taille) avec r4 = début, r5 = 0, r6 = taille :
  taille nulle, rien n'est effacé (801703C0..803D6D00 et 803D8680..803D9674). Le jeu marche avec Swiss et la console
  parce que leur mémoire est vide ; avec Pulse, il restait la mémoire de GC Station. `OSInit` (800DD1C8) commence par
  `lwz r0, -0x6370(r13)` (803D8990, dans le 2e morceau) : non nul = « déjà fait », il saute tout (exceptions,
  interruptions, alarmes) : pas d'image, pas de manettes, pas de DVDInit, aucune nouvelle du jeu : exactement le
  journal vide de v161 à v166.
- `na_bss_gaps` (natif.c) : morceaux du BSS du .dol hors de ses sections (jamais sous 80003100 ni au-dessus de la
  FST) ; `na_zstub` : comme `gl_stub` (copies puis saut), source 0 = mise à zéro. Le journal du lancement montre
  les lignes « zero … (BSS) ». Sans effet pour les autres jeux (leur `__start` efface déjà ce BSS).
- Banc : `t167.c` (morceaux sur la disposition exacte de GCBP7D : 801703C0 + 2517312, 803D8680 + 4084) et
  `t167stub.py` (le programme de saut dans l'émulateur : mémoire remplie de restes, BSS à zéro, section copiée,
  saut) : 29 TOUT OK.
- Version affichée dans le journal de plantage remise à jour (restait « v159 »).

## Nouveautés v166 — journal d'un jeu figé avant ses interruptions
- `early_note` (résident) dans `di_load`, `di_store`, `exi_load`, `exi_store` : seulement tant que `vi_in` et `si_in`
  valent 0 ; 3 s après le 1er accès aiguillé, puis toutes les 3 s : entrée `0xEE` dans le journal (nombre d'accès,
  lecture / écriture, registre, valeur, commandes au lecteur) et `nr_log_flush`. Journal texte : « JEU SANS IMAGE ».
- Crash Bandicoot GCBP7D (v164) : drapeau du débogueur du bi2 à 0, ce n'était pas la cause.

## Nouveautés v165 — morceau suivant préparé d'avance, onglet Photos
- Mesure (simulateur, scénario court par onglet, `PCSIM_PERF=1`) : 4 styles × 7 spectres × 5 dispositions, paroles
  « les deux » : le lecteur visible ne rate presque aucune image (dessin 3 à 4 ms en moyenne, spectre 0,3 ms). Les
  saccades venaient de `start_track_u` (~40 ms : fopen, ID3v1, ID3v2, LAME), de `load_cover` à l'image suivante
  (~30 ms) et de l'entrée dans PHOTOS (`photo_rescan` ~15 ms, `pj_start` 2 à 3 fopen).
- `nx_t nx` : 2 s après le début d'un morceau, `nx_request` (boucle principale) demande le suivant prévu (comme
  `gapless_next_ok`) ; `nx_work` (thread de décodage, sans variable de la boucle principale, lectures de 4 Ko) ouvre
  le fichier, lit les étiquettes, l'en-tête LAME (`mp3_lame_parse`, sans variable partagée), `drflac_open` ou
  `stb_vorbis_open_pushdata`, et la pochette du cache. `start_track_u` reprend tout si chemin et numéro sont les mêmes ;
  `load_cover` reprend la pochette (`cvpre_tex`). Ouverture : MP3 ~7-12 ms, FLAC < 1 ms, OGG ~7 ms (avant 35-70 ms).
- PHOTOS : `photo_scan_step` (un dossier par image à l'entrée de l'onglet), `stat` seulement pour le tri par date
  (dates lues au passage au tri par date), taille gardée dans `pfile_t.size`, vignette du cache ouverte sans ouvrir la
  photo ; lecture de la vignette à l'image suivante de son ouverture.
- Scénario `tools/pc-sim/scenarios/enchainements.txt`. AddressSanitizer : enchainements, son-sauts, son-formats,
  stress-photos, fluidite-onglets, tour : 0 erreur. Son : aucune coupure de plus (sans blanc, fondu 3 s, aléatoire,
  répétition d'un morceau).

## Nouveautés v164 — bi2 : drapeau du débogueur, crochets plus tôt, 7z
- `na_bi2_fix` : bi2 + 0x0C (drapeau du débogueur) remis à 0. Le `__start` du SDK lit `*(0x800000F4) + 0x0C` et, à
  2 ou plus, appelle l'initialisation de MetroTRK avant `OSInit` (code de Crash Bandicoot GCBP7D vu par v162 :
  80003178-80003194). Valeur d'origine notée dans `dernier-lancement.txt`.
- `hooks_lazy` : `hooks_check()` aux 64 premiers accès aiguillés (avant : 1 sur 256).
- `tools/catalogue.py` : archive .7z que py7zr ne lit pas → `7z x` (cube64-1.4.4.7z).

## Nouveautés v163 — émulateurs dans le catalogue d'applis
- `catalogue/applis.txt` : fceugx, snes9xgx, vbagx (dborth, archives `-GameCube.zip`), genplusgx (ekeeke, sans
  release), cubesx (emukidid/pcsxgc, `CubeSX-x.zip`), cube64 (emukidid/Wii64, `cube64-x.7z`).
- `tools/catalogue.py` : archive `dossier:<chemin>/<expression>` = fichier pris dans un dossier du dépôt (API contents),
  version = date de son dernier commit (Genesis Plus GX publie ses programmes dans `builds/`).

## Nouveautés v162 — code de démarrage du jeu dans le journal
- `na_dump_start` : `dernier-lancement.txt` montre 128 mots au point d'entrée du main.dol (pris dans le .dol déjà
  chargé) et 48 mots au début des 8 premières fonctions appelées (`bl`), plus l'adresse et la taille du BSS.
  Crash Bandicoot : The Wrath of Cortex (GCBP7D) : point d'entrée 80003100 dans une section de 416 octets (démarrage
  qui n'est pas celui du SDK Nintendo) ; le journal du jeu reste vide (v161 : « aucune nouvelle du jeu »).

## Nouveautés v161 — journal d'un jeu bloqué tôt
- `nr_log_flush` (résident) : si la carte n'est pas encore démarrée (aucune commande du jeu au lecteur), `nr_init()`
  est fait là (pas en réseau, pas si le jeu se sert du bus EXI) : les crochets image / manettes / son écrivent le
  journal toutes les 3 s dès qu'ils tournent. Avant, `di_ready` à 0 empêchait toute écriture.
- `nat_journal_convert` : `journal-jeu.bin` resté à zéro (remis à zéro au lancement) → `journal-jeu.txt` « AUCUNE
  NOUVELLE DU JEU » + `dernier-lancement.txt` ; avant, le texte du jeu précédent restait (Crash Bandicoot : écran
  noir, journal de Mario Superstar Baseball).

## Nouveautés v160 — place des aiguillages de Pulse, disque du lecteur, logo, réglages du lecteur partout
- Pulse : nouvelle disposition du haut de la mémoire `[ jeu | FST | bi2 | annexe | résident ]`. L'annexe (aiguillages,
  tables des accès EXI) est calculée par jeu (`nhit*80 + 0x2400` + tables) et placée juste sous `NATRES_AT` ; en-tête
  v19 : `tramp_end` (0x1EC) remplace `rsv145`. Le résident faisait 0xED20 octets : il ne restait qu'environ 4,5 Ko
  d'aiguillages ; Mario Superstar Baseball en demande ~12 Ko (83 accès DI + 93 EXI). Au retour à GC Station, la
  table des retours est utilisée directement. Banc `tools/natif-test` : 27 TOUT OK (vérifications adaptées).
- Mémoire basse : 0x30E0 / 0x30E6 / 0x30E9 seulement s'ils sont vides, et 0x30E3 n'est plus forcé (erreur de disque
  de Mario Kart Double Dash!! lancé depuis le vrai lecteur).
- Logo : `tools/gen_logo.py` convertit `docs/logo/icone-{128,64,32}.png` en `source/logo_gcs.h` (RGB5A3) ;
  `draw_brand()` = en-tête Classique, utilisé par Classique, moderne, Aurore et Nouvelle.
- Aurore (`au_player`), Vitrine (`vi_music`, `vi_player`), Îlots (`il_np`, `il_spectrum`) : `lyr_show`, `viz_style`
  (`draw_viz`), `EL_COVER`, `EL_VIZ`, `EL_BUTTONS`, `EL_INFO` comme le style Classique (Nouvelle utilise déjà le
  lecteur Classique).

## Nouveautés v159 — fuzz et UBSan au simulateur
- `tools/pc-sim/fuzz.py <minutes> [graine]` : tous les boutons au hasard (A, B, X, Y, Z, L, R, START, croix, sticks,
  appuis longs) ; jamais A dans JEUX / FICHIERS / TÉLÉCHARGEMENTS ni dans le menu START (redémarrer, mise à jour) ; à
  lancer sur une copie de la carte SD du simulateur (des réglages changent). Graines 2 et 3 (15 min) et 7 (23 min,
  arrêtée par le temps limite) sous AddressSanitizer : 0 erreur, 0 plantage ; mémoire utilisée stable à 5,05 Mo à
  partir de la 13e minute.
- UndefinedBehaviorSanitizer (`O=work/obj-ubsan BIN=gcstation-ubsan SAN="-fsanitize=undefined,bounds
  -fno-sanitize=alignment,vptr"`) : 38 scénarios + son-formats + son-sauts + endurance : 0 erreur.
- Le simulateur peut tourner dans un 2e dossier (`work/run2`, binaire lancé directement) pendant qu'un long test
  occupe `work/run`.

## Nouveautés v158 — ouverture des morceaux, pochettes, AddressSanitizer
- Mesure (`PCSIM_DRAWPROF` : « ouverture du morceau … : N ms ») : 33 à 41 ms (MP3, FLAC, WAV), 120 ms (OGG), dont
  ~30 ms de lecture des en-têtes. `ogg_open` commence à 16 Ko (au lieu de 64 Ko, agrandi si besoin) : 85 ms.
- `load_cover` : un `opendir` du dossier du morceau, puis seulement les fichiers présents (nom du morceau .jpg /
  .jpeg / .png, cover / folder .jpg / .png, sans tenir compte des majuscules) ; avant, jusqu'à 7 `fopen` ratés.
- `cur_on_sd` fixé à l'ouverture (sous le verrou) : `mdec_ok_file` ne lit plus `names[cur_track]` depuis le thread.
- Rapport de fluidité : temps du processeur graphique borné (le simulateur pouvait donner 4294964 ms).
- `tools/pc-sim/asan.sh` : binaire `gcstation-asan` à part (`O=work/obj-asan BIN=gcstation-asan`), scénarios sans
  les limites de la GameCube. 38 scénarios + son-formats + son-sauts + endurance 13 min : 0 erreur.

## Nouveautés v157 — thread de décodage, cache de textures et endurance au simulateur
- Mesure (PCSIM_PERF, `fluidite-onglets`) : dans les onglets Infos, Téléphone, Jeux, Téléchargements, 8 à 15 %
  d'images ratées, presque toutes à cause du décodage (moyenne 3,7 ms par image, pire 21 ms : une trame OGG / FLAC
  ne se coupe pas) fait par `music_pump` avant le dessin ; le décodage pendant l'attente de l'écran
  (`music_idle_decode`) ne passait presque jamais (estimation du coût prudente).
- `mdec_fn` : thread de décodage (priorité 55, sous la boucle principale 64) pour les morceaux de la carte SD
  (`mdec_ok_file` : pas les fichiers du réseau, dont les connexions sont partagées). Il décode une trame à la fois
  jusqu'à `FILE_LEAD` d'avance, quand la boucle principale attend ; elle le préempte dès qu'elle repart. Verrou
  récursif `dec_mx` (`DEC_LOCK`) : `start_track`, `stop_track`, `track_seek`, `stop_station`, `video_open`,
  `video_stop`, `retro_subsong_step` (enveloppes `*_u`) et les décodages faits par la boucle principale ; `dec_wait`
  : le thread laisse passer la boucle principale qui attend le verrou. `music_pump` ne décode plus qu'en secours
  (sous 0,5 s) ; `music_idle_decode` ne sert plus avec le thread ; `bg_tick` ne fait rien s'il est appelé depuis le
  thread. Résultat : 265 → 55 images ratées (`fluidite-onglets`), 1054 → 495 sur 13 min d'endurance, son sans coupure.
- Simulateur : `GX_LoadTexObj` retient chaque texture utilisée (adresse, taille, empreinte) jusqu'à
  `GX_InvalidateTexAll` ; contenu changé entre-temps = « cache de textures perime » (+ ligne du dessin). Retrouve le
  bug de la v155 sur l'ancien code (11 alertes), 0 sur les 38 scénarios depuis.
- Simulateur : `IRQ_Disable` / `IRQ_Restore` sans préemption au milieu (un thread interrompu en tenant le verrou des
  interruptions bloquait tout) ; « mark » affiche la mémoire utilisée ; `tools/pc-sim/endurance.py` : scénario au
  hasard de N minutes (49 min : mémoire stable à 3,00 Mo dès la 7e minute, aucun plantage, 0 trou dans le son).
- Simulateur : une demande de préemption arrivée pendant le paiement de la « dette » de calcul d'un thread était
  oubliée (le moins prioritaire gardait le processeur 300 à 600 ms) ; `PCSIM_NODEC=1` : sans le thread (comparaison) ;
  `PCSIM_DRAWPROF` : attente du processeur par la boucle principale.
- Rapport de fluidité : plus de « 4294967 ms » d'attente de l'écran (mesure commencée en route).

## Nouveautés v156 — enchaînement sans blanc d'un album à l'autre
- `gapless_next_ok` ne demande plus le même dossier / album : le morceau suivant (fin automatique) est ouvert pendant
  que la fin du précédent joue encore (`xf_request = -1`), quel que soit l'album. Restent exclus : la fin de la file
  en aléatoire + répétition (nouveau tirage), les morceaux rétro. Mesure (simulateur) : le blanc de 53 ms au passage
  de « Lagon » (Demo Album) à « Vent du nord » (autre dossier) a disparu.

## Nouveautés v155 — jaquettes sans morceaux d'une autre
- Vu sur la console (vidéo) : un rectangle d'une autre jaquette dans le coin de la grande jaquette, qui disparaît
  ensuite. Cause : une nouvelle image chargée à une adresse mémoire déjà servie par une image libérée ; le cache de
  textures du processeur graphique (TMEM) gardait des morceaux de l'ancienne. `GX_InvalidateTexAll()` après chaque
  `GX_InitTexObj` qui n'en avait pas : jaquettes, grande jaquette, bannières (gclib.c), icônes des cartes mémoire et
  des applis (tools.c), images du tableau rétro (extras73.c). Le simulateur n'imite pas ce cache : pas vérifiable ici.

## Nouveautés v154 — fluidité de la lecture (tous formats)
- Mesure : `au_gap` / `au_gap_ev` (échantillons joués en silence pendant un morceau non fini, hors 0,5 s du début) dans
  `fill_buf` (file vide) et `dma_cb` (aucun bloc prêt) ; simulateur : `[pcsim] son : N trous` à la fin du journal et
  `[pcsim] trou dans le son a ...` à chaque fois. `tools/pc-sim/formats-sd.sh` : un morceau difficile par format (bruit
  rose : MP3 320k, FLAC 24/96, OGG q8, WAV, AAC 256k) ; scénarios `son-formats` et `son-sauts`.
- Constat : au début de chaque morceau, les blocs du son étaient remplis dès qu'une trame était décodée (~1100
  échantillons sur 2048) : 3 à 5 coupures de 16 à 40 ms hachées. `au_hold` : après une file vide (début de morceau,
  retard), `service_audio` ne remplit plus de blocs avant `AU_PRE` (12000 échantillons) d'avance ; `dma_cb` rejoue
  dès 5 blocs. Pas pour la vidéo, ni après la fin du fichier (`eof`).
- `music_pump` : un bloc par image tant que l'avance est sous 1,5 s (avant 0,5 s) ; le rattrapage (plusieurs blocs)
  reste sous 0,25 s.
- Résultat (6 scénarios, réglages remis à zéro) : 34 coupures / 1520 ms → 19 / 786 ms ; celles qui restent sont le
  changement de morceau lui-même (un blanc net de ~43 ms).

## Nouveautés v153 — LAN local vérifié, LAN par Internet
- Banc : mode `lan` du faux jeu (`game.s`) : identifiant du BBA, anneau de réception, trame de 64 octets dans la file
  d'envoi et envoi (NCRA), une lecture du disque au milieu, trame renvoyée par le réseau simulé lue dans la page 1.
  Les accès du jeu au canal 0 passent par les aiguillages EXI de la carte virtuelle (`exi_load` / `exi_store`) :
  périphérique 2 laissé au vrai matériel, la vraie carte du slot A jamais choisie.
- `tools/lan-relais.py` : `relais` (UDP 51240, parties par nom, pont muet 30 s = parti, signe de vie toutes les 5 s)
  et `pont` (AF_PACKET sous Linux, scapy sinon) : trames dont l'adresse source est d'une GameCube (00:09:BF…) envoyées
  au relais ; trames reçues remises sur le réseau ; adresses de l'autre côté jamais renvoyées, empreintes des trames
  (2 s) : pas de boucle. `tools/lan-relais-test.py` : deux réseaux simulés, un PC filtré, pas de boucle.

## Nouveautés v152 — programme compressé de Rogue Squadron III
- Méthode de Swiss comprise et réécrite : en 0x80200000 du main.dol (`GLR*`, `GWXJ13`) : {taille compressée, taille,
  flux zlib d'un 2e programme .dol}. `na_zload` (natif.c, seulement pour un lancement : `na_z_want`) le décompresse
  (`stbi_zlib_decode_buffer`) et cherche ses accès au lecteur (`na_scan_text`) ; au lancement, main.dol et ce
  programme sont branchés ensemble (`na_patch` sur un tampon commun, sections du 2e décalées) : aiguillages et
  couverture de la carte virtuelle communs. Puis `df_zlib` (source/deflate.c, écrit pour GC Station : chaînes de
  hachage, choix différé, blocs à codes calculés, Adler-32) recompresse ; s'il tient dans la place d'origine, il est
  écrit avec sa nouvelle taille et Pulse universel est retiré des options du résident ; sinon tout est remis comme
  avant (résident restauré, Pulse universel). Ligne « Programme compresse » dans `dernier-lancement.txt`.
- `deflate.c` : 3 Mo de GC Station -> 1 674 707 octets (zlib -9 : 1 682 753), relu par zlib.
- `nat_info_t` : 36 sections. Banc : `mkiso_z.py` / `check_z.py` (faux `GLRE64`, 4 accès dans le programme
  compressé : branchés, flux relu par zlib, Pulse universel coupé) ; `h2` et `t146` liés à zlib (-lz).
- Catalogue : paquets de 8 Mo au plus (tenus en mémoire), Neverball retiré ; GCMM : `gcmm_GC.dol`.

## Nouveautés v151 — catalogue d'applis
- `catalogue/applis.txt` : `id | nom | dépôt GitHub | archive (regex) | programme dans l'archive (regex) | fichiers en
  plus (regex) | description` (pas de `|` dans les expressions).
- `tools/catalogue.py` (workflow `catalogue.yml`, chaque lundi, à la demande, ou quand la liste change) : dernière
  release de chaque dépôt, archive (.dol, .zip, .tar.xz/.gz, .7z), programme -> `apps/<id>/boot.dol`, fichiers en plus,
  `meta.xml` ; paquet `<id>-<version>.gcsa` = zlib("GCSA1\n" + chemin, taille, contenu…) ; `catalogue.json` (version,
  taille, taille sur la carte, SHA-256, dépôt). Une appli qui échoue garde l'entrée précédente. Release `catalogue`
  (paquets retirés du catalogue effacés).
- `source/catalog.c` (Outils, `TL_CATA`) : liste (`hfetch`), version installée dans `SD:/apps/<id>/gcstation.txt`
  (sinon « ? » si un `boot.dol` existe), paquet vérifié (taille, SHA-256 mbedtls), décompressé
  (`stbi_zlib_decode_malloc`), chaque chemin contrôlé (seulement `apps/<id>/`, pas de `..`), écrit en `.part` puis
  renommé ; place libre vérifiée. Simulateur : `PCSIM_CATA=<adresse>`.
- Outils : 10 lignes, resserrées pour tenir au-dessus de l'aide.
- Testé : `catalogue.py` contre une fausse API GitHub (tar.xz, zip, deux applis dans une même release) ; simulateur :
  catalogue lu, Swiss et Neverball installés (fichiers vérifiés), Neverball supprimé.

## Nouveautés v150 — heure : décalage au lieu de changer l'horloge du processeur
- `clock_settime` de libogc réécrit le registre de temps du processeur (base de tous les minuteurs, des fils et de
  `gettime`) : remplacé par `clk_ofs`, ajouté par `gc_time` (macro `time()` dans tout GC Station). Le décalage SRAM
  (`SYS_SetCounterBias`) est toujours écrit : au démarrage suivant, libogc part de la bonne heure et `clk_ofs` = 0.

## Nouveautés v149 — date et heure
- `main.c` : `MI_CLOCK` (Jeux > Console). `clk_set` : décalage de l'horloge (SRAM, `SYS_SetCounterBias` = heure voulue
  - 2000 - RTC lue par `__SYS_GetRTC`), puis `clock_settime` pour GC Station. Édition par champ (`clk_edit`, `clk_f`),
  jours du mois et années bissextiles. Scénario `heure.txt` (le simulateur ne change pas l'heure du PC).

## Nouveautés v148 — réglages en groupes
- `main.c` : `sec_items` (32 places) avec des sous-titres (valeur -2 - k : `sec_heads[k]`), sautés par la navigation
  (`sec_step`, `sec_first`) ; le sous-titre du groupe reste visible en remontant. Réglages déplacés : minuterie et reprise
  dans Lecture, éclair sur les basses dans Fête, onglets affichés dans Apparence, mesure de fluidité dans Diagnostic,
  démarrage sur GC Station dans Jeux > Console. Scénario `reglages-groupes.txt`.

## Nouveautés v147 — sauvegardes par QR code
- `tools.c` / `extras73.c` : menus des sauvegardes (vraie carte, carte virtuelle) : « Envoyer au téléphone (QR code) »
  (.gci écrit dans SD:/gci/sauvegardes, vidé avant) et « Tout envoyer » (le dossier partagé : `share_open`, v55).
  `saves.c` : une copie de l'historique (dossier) par QR code. `main.c` : la fenêtre du QR code passe avant les outils
  (touches). Scénario `cartes-qr.txt` (`PCSIM_CARTE=1`).

## Nouveautés v146 — Pulse rapproché de Swiss
- Étude complète de Swiss : docs/ETUDE-SWISS.md, partie 7 (chaque partie de `patcher.c` et de `load_app`, ce que Pulse
  en reprend, ce qui ne s'applique pas).
- `na_lowmem` (lancement Pulse et disque de la console) : 0x2C, 0xD0 si absents ; 0xE8 / 0xEC = zone réservée (FST,
  bi2, résident ; disque : 0 / 0x81800000) ; 0x30E0 = 6 ; 0x30E3 = 0 ; 0x30E6 = code du lecteur (`na_inq_note` :
  0x8000 | code de l'inquiry, 1 sans lecteur, inchangé pour un lecteur optique remplacé) ; 0x30E9 = 5 si vide ;
  0x30C0 / 0x30C4 = instant d'insertion des cartes virtuelles.
- `na_bi2_fix` : bi2 0x24 = norme des manettes (tous les jeux) ; `DPOJ8P` : mémoire simulée ; `GSX*` : arguments
  (`Clonewars.elf`, `/pal`, `/language`, n).
- Table `na_fixes` : + F-Zero GX / AX (vecteurs d'exception), `GH9` / `GUM` / `GWJ` (haut de la mémoire lu en 0xEC),
  `GXX*`, `NXXJ01`, `PCKJ01`, `PCSJ01` (anti-débogage). Table `na_ffixes` : corrections dans des fichiers du disque par
  nom (modules `.rel` de `GHA*`, `GLEP08`), appliquées par le résident à la lecture ; `NA_FXMAX` 96.
- `GLR*`, `GWXJ13` : Pulse universel automatique (programme compressé).
- Banc : `t146.c` (bi2, arguments, table des `.rel`) ; e2e : bi2 0x24 = 5.

## Nouveautés v145 — sauvegardes, compatibilité partagée, jeux en réseau local
- Cartes (`tools.c`, `extras73.c`) : 3e choix « Virtuelle » dans la vue Cartes de l'onglet JEUX (L / R, stick C) ;
  menus : copier vers la carte virtuelle, tout copier (carte virtuelle, autre carte, SD:/gci). `vc_load_active`
  formate une carte virtuelle vierge. Simulateur : `PCSIM_CARTE=1` = cartes mémoire en mémoire (scénario `cartes-copie.txt`).
- Compatibilité (`compat.c`, `repair.c`) : `compat.txt` a un 7e champ (réglage gardé par la réparation). Fil `cpp_thread` :
  `/s/get?p=gc-mp3/compat.txt` de chaque console du Salon (toutes les 10 min au plus, onglet JEUX). Jeu jamais essayé
  ici et qui marche ailleurs avec un réglage : essai de ce réglage au 1er lancement (`rep_t.base = 0`), gardé s'il
  marche, sinon réparation normale. Tri `jeux_tri = etat`. Scénario `compat-salon.txt` (avec `tools/pulse-serveur.py`,
  `PULSE_IP=127.0.0.2 PULSE_ANNONCE=127.0.0.1`).
- Résident v19 (`natres_hdr_t` 0x1F0, anneau du journal : 1 entrée) : `bba_n` (le jeu choisit le BBA, canal 0
  périphérique 2), `exi_wait` / `exi_sync`. `exi_busy` : un autre périphérique choisi sur le canal de la carte SD, ou un
  transfert en cours -> la lecture en fond attend (au plus 1 s de suite), une petite lecture passe en fond, le journal
  attend. Banc : `EMU_EXIBUSY=2000`.
- `na_bba_quiet` : BBA remis à zéro, sans interruptions, avant le saut vers le jeu (Pulse et disque de la console).
  `na_lan_game` (GKY, GM4, GTE, GPO, GPS) : refusés en « Jouer par le réseau ».

## Nouveautés v144 — accents et serveurs absents
- `source/main.c` : 64 lignes de textes passées par un double décodage UTF-8 -> Latin-1 (commit b45433f, v134) remises
  en UTF-8 (page d'accueil du web, erreurs TLS / connexion, téléchargements, favoris par défaut, et le test
  `strstr(err, "sécurisée")` qui décide du repli en HTTP). `utf8_unmangle` répare les lignes de `favoris.txt`
  écrites par v134 à v143 (C3 83 / C3 82 suivis d'un octet de continuation, jusqu'à 8 passes).
- `source/netfs.c` : FSP, 4 essais à délai court tant que le serveur n'a jamais répondu (`alive`), sinon 7 avec
  délai doublé (3 s au plus) : 4 s au lieu de 14,8 s de gel quand le serveur est éteint. FTP / SMB : connexion
  refusée (`conn_last_cr == -ECONNREFUSED`) -> « Serveur absent », sans nouvel essai (pire image 1043 -> 52 ms).
- CI : la construction échoue si `source/` contient la signature d'un double encodage (C3 83 C2 / C3 82 C2).
- Limite connue : la connexion TCP vers une machine absente du réseau reste bloquante (net_connect de libogc).

## Nouveautés v143 — Pulse par le réseau (Broadband Adapter)
- Résident : pilote minimal du BBA (EXI canal 0, périphérique 2, 32 MHz) écrit pour GC Station : commandes EXI de
  2 octets (registres du BBA) et de 4 octets (registres du contrôleur Ethernet, 0x80000000 | écriture 0x40000000 |
  adresse << 8), anneau de réception pages 1 à 15 (descripteur de 4 octets petit-boutiste : page suivante, longueur),
  envoi par la file (TXFIFOCNT, WRTXFIFOD, NCRA ST1). ARP (réponse pour notre adresse, recherche du serveur), IPv4,
  UDP. `pn_read` remplace la lecture de la carte quand `natres_hdr_t.net` est réglé (aussi pour la musique du disque).
- Protocole : `/s/open?p=` (HTTP, Salon) -> « numéro taille port » ; puis UDP 51235 : "PNR1" {numéro, fichier,
  position, longueur <= 2048} -> "PNA1" {numéro, position, longueur <= 1024, données}. Délai 25 ms, 120 essais.
- GC Station : serveur des ISO dans le Salon (`pns_thread`, salon.c) ; `nat_boot_net` (natif.c) : entrée provisoire
  dans la liste des jeux, en-tête lu par `gcs:`, réglages `natres_net_t` rangés dans la place libre du résident.
- `tools/pulse-serveur.py` : le même serveur sur un PC (annonce du Salon, port HTTP annoncé : 8080).
- En-tête du résident : version 18, 0x1E0 octets (anneau du journal : 2 commandes) ; `n_rescue` compte les fins de
  commande données en secours. Place gardée pour les aiguillages avant la table du 2e disque : 8 Ko.
- Tests : banc d'essai, nouveau test `net` (BBA et serveur simulés : ARP dans les deux sens, une réponse perdue
  exprès, tout l'ISO et la musique du disque lus par le réseau) ; simulateur : serveur des ISO de GC Station
  interrogé par un client Python (4 lectures identiques), lancement d'un jeu de `pulse-serveur.py` par l'onglet FICHIERS.

## Nouveautés v142 — téléphone : manette, jaquettes, photos, Salon, file de lecture
- `tab_phone.c` : `/api/key?k=` (boutons ajoutés à ceux de la manette pour une image, `tp_keys`), `/api/cover?i=`
  (jaquette en CMPR telle qu'elle est dans posters.pak, décodée par la page dans un canvas ; `X-Size` : taille de la
  texture et de l'image), `/api/photos`, `/api/photo?i=`, `/api/show?i=` (onglet PHOTOS, `pv_open`), `/api/queue`,
  `/api/salon`, `/api/listen?i=` (`salon_listen`).
- `phone_page.h` (français et anglais) : onglets Manette, Photos, Salon ; jeux en grille avec jaquettes (chargées une
  par une) ; « À suivre ».
- Testé : page ouverte dans Chromium contre le simulateur (aucune erreur JS), jaquette décodée, photo affichée sur la
  télé, START envoyé depuis la page ouvre les réglages.

## Nouveautés v141 — copies des sauvegardes, historique, reprise sur une autre console
- `source/saves.c` : `sv_after_game` (appelé par `nat_journal_convert`) lit dans `dernier-lancement.txt` la carte
  virtuelle utilisée (slots A et B) et copie en `.gci` les sauvegardes du jeu (code + éditeur) avec les fonctions
  des cartes virtuelles (extras73.c) ; copie sautée si identique à la dernière ; 10 copies gardées par jeu.
- Outils > Historique des sauvegardes (`TL_SAVES`) : jeux, puis copies locales et copies des consoles du Salon
  (`gcs:/gc-mp3/sauvegardes/<ID>`, routes `/s/list` et `/s/get` de v138). Remettre : la sauvegarde de même nom est
  d'abord effacée, puis la copie est écrite (carte virtuelle du jeu, `na_card_name`, ou vraie carte, `mc_import`).
- Testé dans le simulateur : 2 parties -> 2 copies, 3e identique sautée ; copie locale et copie d'une 2e console
  simulée remises sur la carte virtuelle (contenu vérifié).

## Nouveautés v140 — réparation automatique des jeux
- `source/repair.c` : `rep_diag_set` (appelé par `nat_journal_convert`) retient ce que disent les journaux : plantage,
  aucune image, bloqué dans la fonction d'image, accès au lecteur non branchés, cheats, 16:9, carte virtuelle,
  mode vidéo, vitesse, Pulse universel.
- Au verdict (`cp_set`, compat.c) : `rep_verdict` crée la liste d'essais (`rep_plan`, lettres S F T C H W V X), ou
  passe à l'essai suivant, ou garde le réglage qui a marché (`rep_keep` -> `SD:/gc-mp3/jeux/<ID>.txt`). « Marche
  mal » après un écran noir compte comme un progrès : le réglage est gardé et de nouveaux essais suivent.
- `SD:/gc-mp3/reparation.txt` : `code|essais|essai en cours|essayé au dernier lancement|verdict de départ`.
- `rep_apply` (dans `jeux_cfg_apply`, Pulse seulement) applique l'essai pour ce lancement ; `rep_pre` (avant le
  choix Pulse / lanceur de secours) pour les cheats. Ligne « Réparation automatique » dans `dernier-lancement.txt`.
- Réglages du jeu : `cheats_coupes`, `carte = -` (aucune carte virtuelle).

## Nouveautés v139 — Pulse universel (accès au lecteur pris par exception)
- Option par jeu `acces_exception = 1` (`SD:/gc-mp3/jeux/<ID>.txt`) -> `NRO_TRAP` dans `natres_hdr_t.opt`.
- Le résident, dès que le jeu a installé ses fonctions d'interruption : BAT de 256 Mo en 0xC0000000 réduit à
  32 Mo (la mémoire), 2e BAT pour l'image (EFB, 0xC8000000), table des pages de 64 Ko (SDR1 = 0x017F0000) avec
  le segment 0xC sur un VSID qui fait tomber les pages 0xCC000-0xCC008 dans 0x817FFC00 (place laissée libre par
  GC Station). La page 0xCC006 (DI, SI, EXI, AI) n'y est pas : chaque accès du jeu provoque une DSI.
- Vecteur 0x300 : 1re instruction du SDK remplacée par `ba trap_vec` (adresse physique). `trap_vec` (sans
  traduction) vérifie DSISR (pas de traduction), DAR (page 0xCC006) et SRR0 (code du jeu), sauve les registres,
  puis `trap_c` (avec traduction, sur la pile du jeu) décode l'instruction (lwz, lhz, lha, lbz, stw, sth, stb,
  formes x, u, ux, lwbrx...), appelle les mêmes fonctions que les aiguillages (`di_load`, `exi_load`, `si_load`...)
  ou fait l'accès réel, et repart à l'instruction suivante. Le reste repart dans le vecteur du SDK.
- Le résident passe par une 2e adresse de la page (0xCC406000, `hw_d`). BAT ou vecteur remis par un autre
  programme du disque : vu à chaque entrée dans le résident (`trap_guard`), l'option est réarmée. Retour à GC
  Station : tout est remis (`trap_disarm`).
- Refus notés dans le journal (`trap_state`) : BAT inattendu, 2e BAT utilisé, jeu avec sa table des pages, etc.
- Journal : en-tête de 0x1D0 octets (version 17), anneau de 3 commandes.
- Banc d'essai : nouveau test `trap` (l'émulateur provoque l'exception DSI quand la page n'est plus traduite) :
  lecture complète faite par des accès dont l'adresse est lue en mémoire (stw, stwx, stwu, sth, lhz, lwz, lbz).
- Place des aiguillages : 12 Ko gardés quand la table du 2e disque est rangée (16 avant).
- Onglet JEUX : « (pas de jaquettes) » placé à gauche de « R : affichage ».

## Nouveautés v138 — le Salon (plusieurs GC Station sur le réseau)
- `source/salon.c` : annonce UDP (port 51234, `GCST1\tnom\tport\ttitre`) toutes les 5 s à 255.255.255.255 et au
  sous-réseau ; les annonces des autres consoles remplissent `salon_peers` (partie après 20 s sans annonce).
- Le serveur du téléphone répond à `/s/list`, `/s/stat`, `/s/get` (avec Range) et `/s/now`, seulement Salon actif
  (403 sinon), chemins limités à la carte SD (`..` refusé).
- `netfs.c` : 4e système `gcs:` (client de ces routes) : les autres consoles se parcourent et se copient comme un
  serveur SMB/FTP. La liste des appareils (onglet FICHIERS) se refait quand une console arrive ou part.
- « Écouter avec » (Y) : `/s/now` donne le chemin et la position ; le morceau est lu en `gcs:/…` et avancé.
- Réglages `salon` et `salon_nom` dans `gc-mp3.txt`.
- Mario Superstar Baseball (v136) confirmé sur la console.

## Nouveautés v137 — base de compatibilité des jeux
- `source/compat.c` : après la conversion du journal de Pulse (`nat_journal_convert`), `cp_after_game` note un
  plantage tout seul (exception notée par le résident), sinon une question est posée 3 s après le démarrage
  (menu d'actions : marche, marche mal, écran noir, envoyer le journal, plus tard). Un jeu pas lancé (pas de
  « Saut vers le jeu » dans `dernier-lancement.txt`) n'est pas demandé.
- `SD:/gc-mp3/compat.txt` : une ligne par jeu (`code|verdict|build|date|vitesse vrai lecteur|mode vidéo`).
- Pastille sur les jaquettes (`gl_draw_cover`), ligne « Dernier essai » dans la fenêtre de lancement.
- La fenêtre de partage (QR code, v55) s'affiche et se ferme dans tous les onglets ; les partages passent même
  si l'onglet TÉLÉPHONE est enlevé (le reste de la télécommande reste coupé, v128).

## Nouveautés v136 — Pulse : Mario Superstar Baseball, corrections dans les autres exécutables
- **Mario Superstar Baseball (GYQE01)** : 2e journal (build 489, vitesse d'un vrai lecteur) : même boucle
  (PC 800AF558), autres valeurs : r30+0x158 et r30+0x160 sont les pointeurs de lecture et d'écriture d'un tampon
  presque plein (r30+0x15C = fin) ; la boucle 800AF558-800AF570 attend de la place mais les pointeurs sont lus
  avant elle (800AF540-800AF54C) : boucle sans fin dès que le tampon est plein en arrivant. Correction
  (`na_fixes`) : le saut de fin de boucle en 800AF570 (`bge 800AF558`, 4080FFE8) repart en 800AF540
  (4080FFD0) : les pointeurs sont relus à chaque tour, le son de la vidéo libère la place, le jeu continue.
  Écartés : rendre 2 (« erreur » pour l'appelant 800AED9C : message et arrêt de la vidéo) ou 1 (le paquet déjà
  lu serait perdu). Appliquée seulement si l'instruction d'origine est 4080FFE8 (taille du main.dol libre :
  `len` 0 + `orig`). GYQ retiré de `jc_slow_known` (la vitesse ne suffisait pas).
- **Autres exécutables du disque** : `na_fx_scan` parcourt la table des fichiers (FST) : `.dol` (sections),
  `.elf` (segments PT_LOAD), `.tgc` (son `default.dol` et les fichiers de sa propre FST, positions recalées :
  base du .tgc + userStart - gcmUserStart). Pour chaque correction de la table dont la taille est celle du
  fichier, la position dans l'ISO est calculée ; la liste {position, valeur, taille} est passée au résident à la
  suite des cheats (`"FIXT"`, `chts`), et `fx_apply` change ces octets dans chaque lecture du jeu qui les
  contient (aussi à cheval sur deux morceaux ; essai à part : OK). Compteur dans le journal du jeu (16 bits
  hauts de `si_fixes`). `switcher.prs` (compressé) : pas possible ainsi ; aucune correction de la table ne le vise.
- **Alarmes de délai du lecteur** (`SetTimeoutAlarm`) : chez Swiss, ces signatures servent à reconnaître la
  lecture du SDK, que Swiss remplace par la sienne. Pulse garde la bibliothèque du jeu : l'alarme est armée et
  annulée par la fin de chaque commande, que Pulse donne en quelques millisecondes (au pire en 3 images, secours
  v133) : rien à changer.
- Banc d'essai de Pulse : 21 « TOUT OK ».

## Nouveautés v135 — Pulse : Mario Superstar Baseball, corrections par jeu, journal plus complet
- **GYQ (Mario Superstar Baseball)** ajouté à `jc_slow_known` (vitesse d'un vrai lecteur en « Auto »). Le journal
  (build 487) : PC 800AF56C dans une boucle 800AF558-800AF570 qui compare des champs de la structure du lecteur
  vidéo HVQM4 (r30 + 0x158 / 0x15C / 0x160) chargés AVANT la boucle : le jeu attend qu'il y ait de la place
  dans un anneau de 0xF00 octets (0x10B0 demandés) que l'autre côté (le son de la vidéo) libère, mais le
  compilateur a sorti les lectures de la boucle : si l'anneau est plein en arrivant, c'est une boucle sans fin.
  Sur une vraie console le lecteur est lent et l'anneau n'est jamais plein ici ; avec Pulse en rapide, tout le
  fichier vidéo était lu d'un coup (lectures de 16 Ko jusqu'à la fin, puis plus rien).
- **Corrections par jeu** (`na_fixes` dans `natif.c`) : code du jeu + taille exacte du main.dol + adresse +
  valeur, l'adresse convertie par les sections du .dol (hors sections : ignorée). Les endroits viennent de
  `Patch_GameSpecific` de Swiss (GPL 2 : méthode et données, pas son code) : transfert ARAM (nop), cartes
  1019 / 2043 (taille acceptée = celle de la vraie carte du slot A ; la carte virtuelle de Pulse, 251 blocs,
  est déjà acceptée), initialisation de l'image (exécutables de 200-340 Ko : ceux chargés comme main.dol),
  anti-débogage (D58-D62, D78/D86/D89, E22/E23, GC6 E/J/P), langue (GMSP01 : `li r3, langue`).
  Ligne « Corrections du jeu » dans `dernier-lancement.txt`.
- **Journal au retour A+Z+START** : `na_func_dump` imprime la fonction entière (du `stwu r1` au `blr`, 200 mots
  au plus) autour de PC, LR et des 2 premiers appels, lue dans le main.dol de l'ISO ; la photo de la mémoire
  (`mem_snap`) ajoute 0x800 octets autour de r3, r4 et r30 (seulement si ce sont des adresses ; l'arène passe
  de 0x40000 à 0x3E800 pour rester dans 704 secteurs), avec le genre de chaque zone dans l'en-tête ; ces trois
  zones sont imprimées en hexadécimal dans le journal.
- Pas encore fait (voir `docs/ETUDE-SWISS.md`) : corriger les autres exécutables du disque à la volée (`.elf`,
  `.tgc`, `switcher.prs`) et gérer les alarmes de délai du lecteur (`SetTimeoutAlarm`).
- Banc d'essai de Pulse : 21 « TOUT OK » (la photo de la mémoire accepte 4 à 7 zones).

## Nouveautés v134 — Pulse sans le vrai lecteur, vitesse auto, accès en secours, mise à jour qui reprend
- `natres.c` : `wk_tick` (0 = réveil par une vraie commande « inquiry », 1 = sans le lecteur). Sans lecteur
  (`NRF_REAL_IRQ` absent : GC Station ne le pose plus si le lecteur ne répond pas au lancement), `di_real_kick`
  ne fait rien et `di_tick`, appelé après la fonction du jeu dans les crochets VI, AI, DSP, SI et dessin, fait
  avancer la lecture en fond (`rd_pump`, budget doublé) et donne chaque fin au jeu (ainsi que les interruptions
  de la carte virtuelle). Avec lecteur : secours à l'image (3 images sans réveil), et passage définitif sans lui
  après 3 secours ou un lecteur occupé plus de 2 s (entrées `DE` dans le journal). Pas de réveil si le jeu a
  masqué l'interruption du lecteur. Banc d'essai : test `tick` (sans lecteur, et réveils perdus).
- Pourquoi pas l'interruption fabriquée de Swiss (erreur PI) : elle arrive aussitôt, avant que la bibliothèque
  du jeu ait posé son alarme de délai (Swiss modifie ces fonctions) ; depuis les interruptions du jeu, l'ordre
  reste celui d'un vrai lecteur.
- `jeux_cfg.c` : vitesse du lecteur 0 = Auto (`jc_slow_known` : GS2, GDR, GHA, GLE), 1 = vrai lecteur, 2 = rapide.
- `natif.c` : `na_xscan` note aussi les accès au lecteur (mots) ; ceux que `na_scan_text` n'a pas trouvés sont
  branchés avec un aiguillage à adresse calculée (`NA_DDYN`, fonctions EXI du résident qui renvoient la plage
  du lecteur vers `di_load` / `di_store`) ; un échec ne bloque pas le lancement.
- `update.c` / `hfetch_t` : `range` (en-tête Range, réponse 206) et `keep` (ce qui est arrivé est gardé) :
  téléchargement repris jusqu'à 6 fois de suite, taille finale vérifiée. Simulateur : `PCSIM_NETCUT` (coupe une
  connexion après N octets), `PCSIM_UPD` (serveur de mise à jour local), `PCSIM_CA`.

## Nouveautés v133 — Pulse : fins de commande de secours, journal au retour
- `natres.c` : `rd_pump` (morceaux d'une lecture en fond, sorti de `di_hook`) ; `di_watchdog`, appelé à chaque image
  (VI) : si le vrai lecteur est au repos, que l'interruption du lecteur n'est pas masquée par le jeu (masques du
  SDK en 0x800000C4 / 0x800000C8) et que rien n'a avancé depuis 3 images, la lecture en fond continue d'ici
  (puis un nouveau réveil) et la fin est donnée à la fonction du jeu (`t[21]`, ou celle qu'on chaîne). Entrées
  `DD` dans le journal (« SECOURS »). `hooks_check` rebranche la fonction du lecteur même pendant une lecture en fond.
- Retour A+Z+START : `crash_dump(0xFF, contexte interrompu)` (crochets VI / AI / SI) : PC, LR, appels, pile et
  photo de la mémoire, comme un plantage ; GC Station l'affiche « Où en était le jeu au retour ».
- `docs/ETUDE-SWISS.md` : comparaison avec Swiss et plan.

## Nouveautés v132 — nouveau logo, tous les morceaux d'emblée, pochettes dans la rangée
- `draw_logo_cube` dessine le nouveau logo (cube isométrique violet, filets clairs entre les faces, ovale et
  trois barres sur la face gauche, dessinés dans le repère de la face) ; couleurs exactes dans tous les styles
  (`nv_raw`). En-tête d'Aurore : logo de rayon 17 et « GC Station » en moyen ; rail de Vitrine : logo en grand.
- Réglage `affichage` : MUSIQUE démarre toujours sur « Tous les morceaux » (la vue reste changeable au stick C).
- `vignettes.c` : 40 vignettes 64x64 (RGB565), une par album (album + artiste) sinon par morceau ; le fil de
  décodage (`tj_run`) lit le cache des pochettes ou cherche l'image comme `load_cover` (image à côté, APIC
  du MP3 via `id3_pic`, image du FLAC via `flac_meta_pic`, cover / folder du dossier), la réduit et la range
  dans le cache (le lecteur la retrouve aussitôt). Une demande par image, seulement si le son a de
  l'avance (`io_safe`) ; `bg_quiesce` attend aussi cette lecture avant de démonter la carte.

## Nouveautés v131 — styles Vitrine et Îlots (à la place de Nébula et Halo)
- `ui_skin` 4 = **Vitrine**, 5 = **Îlots** (`skin_keys` : `vitrine`, `ilots` ; `nebula` → Vitrine, `halo` → Îlots).
  Ils gardent le moteur d'Aurore (famille `>= 3`). Le code de Nébula et Halo est retiré (`halo_col`,
  `nb_background`, `hl_header`…). Nouveaux fichiers : `ui_styles.c` (cadre : fonds, rail, dock, cadres,
  sélections, zone de la page) et `ui_styles2.c` (pages propres : MUSIQUE, lecteurs, INFOS).
- **Zone de la page** (`st_page_map` / `st_page_unmap`, appelés par `nv_page_begin` / `nv_page_end`) :
  Vitrine resserre la page à droite du rail avec le mécanisme du 16:9 (`ux_k`, `ux_o` : positions et largeurs
  resserrées, pas le texte ni les images) et la remonte de 22 points. Îlots la remonte de 58 points
  (matrice de position du GX) pour laisser la place au dock. Les zones du curseur (`gp_zone` →
  `st_zone_fix`), les découpes (`scr_scissor`) et `scr_viewport` (qui ne remet plus le repère pendant la
  page) suivent.
- Vitrine, MUSIQUE : rangée de pochettes qui glisse (`vi_music`) ; pochette du morceau qui joue, sinon une
  pochette dessinée (dégradé choisi par album / artiste, lueur, icône) ; ← / → = ligne précédente / suivante.
- Îlots : dock d'icônes (`il_dock`, dessiné après la page) ; tuiles (`il_tile`, `il_np`, `il_clock`,
  `il_meteo`, `il_list`, `il_spectrum`, `il_infos`).
- Les sélections violettes classiques n'ont plus de lueur ni de contour en Vitrine / Îlots ;
  `au_pill_h` est une seule forme (plus de bande foncée aux bouts quand elle est transparente).
- Web : la page est mise en page pour la place à côté du rail (Vitrine) ; le curseur du web utilise
  l'échelle réelle de la page (`web_kx` : corrige aussi le 16:9). Téléphone : QR code et texte adaptés à
  une page plus étroite.

## Nouveautés v130 — 576 lignes seulement dans GC Station
- Pulse, le disque de la console et GC Loader remettaient déjà la vidéo dans le mode du jeu avant de le lancer
  (`na_vi_bootrom` : NTSC 480i, PAL 576i ou PAL 60). Restaient : les programmes lancés par `gl_boot_mem`
  (applis, lanceur de secours, relance de GC Station) qui partaient avec la sortie 576 de GC Station :
  la vidéo est remise en mode standard (PAL 576i entrelacé ou PAL 60 480i) juste avant ; et le réglage
  « Apps Video Mode » du lanceur de secours, qui recevait 576i / 576p depuis la v128 : il reçoit « Auto »
  (et un ancien 576 est remplacé au démarrage).
- 576 lignes à 60 Hz n'existe pas comme mode vidéo standard (576 lignes = 50 Hz, 60 Hz = 480 lignes) :
  pour du 60 Hz progressif, c'est 480p (sur une console PAL : PAL 60).

## Nouveautés v129 — styles Nébula et Halo, fin de Moderne et Nouvelle
- **Nébula** (`ui_skin` 4) et **Halo** (5) reprennent le moteur d'Aurore (sélection qui glisse, transitions,
  lecteur de MUSIQUE, menu START) : partout où Aurore était testé (`ui_skin == 3`), c'est maintenant la
  famille Aurore (`>= 3`) ; chaque style change son fond, ses cadres, sa légende et sa sélection
  (`ui_aurore.c` : `nb_background`, `hl_background`, `au_panel`, `au_legend_box`, `au_sel_paint`).
- Nébula : trois lueurs (cercles en dégradé jusqu'au transparent) qui dérivent ; verre plus présent (bord,
  reflet) ; rose #FF4FB0 → cyan #2FD3FF.
- Halo : une couleur par onglet (`halo_tab_rgb`) ; les violets de l'interface prennent cette couleur
  (`halo_col`, cache des couleurs vidé au changement d'onglet), le reste en graphite sombre ; la grande carte
  (à droite ou pleine largeur) a le contour lumineux ; en-tête maison (`hl_header`) : nom de l'onglet en
  grand, point de sa couleur, icônes des onglets (l'onglet choisi en pastille lumineuse).
- Moderne et Nouvelle ne sont plus proposés : un réglage `style = moderne / nouvelle` devient Aurore
  (leur code reste : Aurore s'appuie sur celui de Nouvelle).

## Nouveautés v128 — onglets coupés, 576i / 576p, début de morceau plus fluide
- **Onglets** : enlever un onglet (START > Onglets affichés) arrête aussi tout ce qu'il fait en fond : leurs
  tâches de chaque image ne tournent plus (web, podcasts et téléchargements automatiques d'épisodes, photos /
  vidéo, fichiers, infos : météo et réveil, téléphone), la reprise de la radio au démarrage est sautée, la
  télécommande refuse les connexions, et ce qui était en lecture depuis cet onglet s'arrête (`tab_disable`).
- **576i / 576p** : modes `TVPal576IntDfScale` / `TVPal576ProgScale` de libogc : l'image reste dessinée en
  640 × 480 et la copie vers l'écran l'étire sur 576 lignes (le dessin ne change pas). 576p seulement avec le
  câble composante (sinon 576i). Aussi transmis au lanceur de secours (Apps Video Mode).
- **Banc d'essai complet** (simulateur, limites de la GameCube) : 25 scénarios, aucun plantage ni manque de
  mémoire ; même temps de calcul et mêmes images/s que la v124. Seule saccade régulière trouvée : ~86 ms au
  début de chaque morceau (pochette ~30 ms + étiquettes ~40 ms dans la même image) ; la pochette est
  maintenant lue à l'image suivante (~59 ms au pire).

## Nouveautés v127 — textes qui défilent, Dolphin PC / Android
- **Textes trop longs** : `fit_text` fait défiler le texte (1,5 s de pause, une lettre toutes les 0,18 s, 1,5 s
  de pause à la fin, puis on recommence) au lieu de le couper. Dans une liste (le même endroit du programme
  dessine plusieurs lignes par image), seule la ligne choisie défile (`fit_text_sel`) ; elle repart du début
  quand on y revient.
- **Mise à jour automatique** : une fois la nouvelle version téléchargée (et enregistrée, ou gardée en mémoire
  sans carte SD), GC Station redémarre tout seul au bout de 3 s ; un bandeau l'annonce dans tous les onglets.
- **Réglages sur Dolphin sans carte SD** : depuis la v22, les réglages sont dans `SD:/gc-mp3/`, mais la SD en
  mémoire ne gardait sur la carte mémoire que les fichiers de la racine : rien n'était gardé. Les fichiers de
  `gc-mp3/` le sont de nouveau, `gc-mp3.txt` en premier.
- **Dossiers** : `mp3`, `retro`, `photos`, `videos`, `ecran`, `downloads`, `games` et ceux de `gc-mp3/` sont
  créés au démarrage (carte neuve, carte SD de Dolphin, SD en mémoire).

## Nouveautés v126 — Pulse : plus de place pour la carte mémoire virtuelle
- Mario Superstar Baseball : écran noir juste avant le menu principal. Le journal montrait « carte mémoire
  virtuelle coupée pour ce jeu : plus de place » (83 accès au lecteur + 93 accès EXI à brancher) : le jeu
  parlait donc à la vraie fente A au moment de vérifier la carte, et attendait sans fin.
- Chaque aiguillage recopiait toute la sauvegarde / restauration des registres (stmw, cr, ctr, xer, lmw…) et
  était arrondi à 32 octets. Maintenant deux petites routines communes (posées une fois au début de la place
  libre, appelées par bl) et des aiguillages serrés : ~72 à 84 octets au lieu de 128 à 160.

## Nouveautés v125 — le web s'ouvre de nouveau sur la console
- Sur la console, toutes les pages échouaient juste après l'envoi de la demande (archive.org : « Connexion
  coupée », Wikipédia : « Le serveur ne répond pas »), alors que tout marchait dans le simulateur (qui
  n'utilise pas la pile réseau de la GameCube). Seule différence côté console depuis la veille : la pile
  réseau recompilée en v117 avec une fenêtre TCP de 44 paquets et 256 tampons. Retour aux réglages
  d'avant (16 paquets, 128 tampons) : avec 44 paquets, le serveur envoie jusqu'à 64 Ko d'un coup, bien
  plus que ce que l'adaptateur réseau peut garder en attendant d'être lu.

## Nouveautés v124 — erreurs du navigateur visibles
- Une page qui ne s'ouvre pas : bandeau rouge sur la page avec la raison (8 s).

## Nouveautés v123 — Pulse : vidéo, manettes, vitesse du lecteur, slot B, régions, .nkit
- **Modes vidéo** : les « GXRenderModeObj » du jeu (0x3C octets dans main.dol) sont reconnus à leur contenu
  (valeurs possibles, filtre vertical dont la somme fait 64) et modifiés avant le lancement : 480p (mode
  progressif, une trame, filtre neutre ; refusé sans câble composante), PAL 50 Hz -> EURGB60 480 lignes,
  240p (entrelacé 480 lignes -> non entrelacé 240), sans flou (filtre 0 0 21 22 21 0 0), image large
  (704 points, origine 8). Mémoire basse 0xCC = EURGB60 pour un jeu PAL passé en 60 Hz.
- **Manettes** : les accès du jeu aux registres SI (0xCC006400-3F, adresse calculée comme le SDK) sont branchés
  sur le résident quand un réglage de manette est choisi : stick C inversé / échangé (mode analogique 3),
  L / R numériques au-delà d'un seuil, SIPOLL (lectures par image x2 / x4).
- **Vitesse d'un vrai lecteur** : toutes les lectures en fond, fin pas avant ~1 ms (60 ms si la tête doit
  aller loin) + 1 ms par 3 Ko.
- **Cartes mémoire** : émulation du slot B (canal EXI 1, interruptions 12 / 13), fichier `carte-B.raw` ;
  une carte par région du jeu (réglage). Réglage `resident_carte_virtuelle = a+b`.
- **.nkit.iso** : morceau « zéro » après la fin du fichier, jusqu'à la taille d'un disque.
- Résident v16 (en-tête 0x1C0, journal à 4 entrées). Banc d'essai natif : + slot B, manettes, lecture
  après la fin, modes vidéo (20 tests).

## Nouveautés v122 — cases à cocher lisibles
- Listes à cocher (onglets affichés, éléments du lecteur) : coche verte bien visible, nom grisé quand c'est décoché.

## Nouveautés v121 — Pulse : .ciso, 2 disques, 16:9, cheats ; GC Loader ; jeux du réseau
- **.ciso** : la table des morceaux de Pulse est faite à partir de la carte du .ciso (blocs présents = leurs
  secteurs dans le fichier, blocs absents = morceaux « zéro », jusqu'à la taille d'un disque). Vérifications
  (3 endroits, test du lecteur SD maison) par la même lecture que l'analyse.
- **2 disques** : GC Station cherche l'autre disque (même ID, autre numéro) et range sa table sous celle du
  disque en cours. Quand le jeu arrête le moteur (E3) puis attend le couvercle (masque DICVR), Pulse échange
  les tables au bout d'une seconde et donne l'interruption « couvercle refermé » ; le jeu relit l'ID.
  Journal : « CHANGEMENT DE DISQUE », nombre de changements.
- **16:9 forcé** : GXSetProjection est reconnue à ses lectures de la matrice ; un aiguillage multiplie par 3/4
  la 1re ligne des matrices en perspective, sur une copie. Réglage par jeu (`SD:/gc-mp3/jeux/<ID>.txt` :
  `ecran169 = oui`).
- **Cheats** : interpréteur de codes Gecko dans Pulse, appelé à chaque image : 00-08 (+10), 20-2E (+30),
  40/42/48/4A, C0, C2, CE/DE, E0, E2, F0. Codes choisis comme Swiss (`<ID>.txt` + `<ID>.chtsel`).
- **GC Loader** : détecté en cherchant les jeux (inquiry : date 2019-6C-64), carte montée en `gcl:` ;
  lancement : morceaux des fichiers (disque 1 et 2) envoyés par les commandes B3, puis démarrage comme un
  vrai disque (sans Pulse : pas de A+Z+START).
- **Réseau** : copie dans `SD:/jeux-reseau`, place vérifiée, puis lancement par Pulse.
- Copie de fichiers : morceaux de 16 Ko, au plus ~10 ms par image.
- Résident v15 (en-tête 0x1A0, journal à 6 entrées). Banc d'essai natif : + .ciso, changement de disque,
  cheats, 16:9 (17 tests).

## Nouveautés v120 — fluidité du style Aurore
- Couleurs du thème : la conversion (Aurore, Nouvelle, Moderne, thèmes de couleur) était refaite à
  chaque sommet ; elle est gardée (256 cases), recalculée seulement si le style ou le thème change.
- Textes d'une seule couleur : plus de mélange de couleurs lettre par lettre ; rectangles arrondis unis
  idem. Largeurs des noms d'onglets d'Aurore gardées.
- Petits ronds et petits anneaux (points, curseurs, boutons) avec 10 ou 20 côtés au lieu de 40.
- Fond d'Aurore : chaque ruban en un seul envoi (avant : 48), points calculés une fois.
- Décors de saison (Halloween) : lueurs, citrouilles, fantômes, chauves-souris, araignée et brume
  envoyés ensemble (avant : un cercle de 42 sommets et un envoi chacun).
- Photos : vignettes du cache lues par morceaux de 4 Ko, nouvelles vignettes écrites 2 Ko par image ;
  photos lues par morceaux de 4 Ko (8 Ko dépassaient le temps d'une image).
- Musique : dans la boucle, un seul bloc décodé tant qu'il reste 0,25 s d'avance (le reste pendant
  l'attente de l'écran).
- Simulateur : le comptage du travail du processeur graphique n'est plus compté comme calcul de la
  GameCube ; mesure du dessin par partie (`PCSIM_DRAWPROF=<ms>`), scénario `aurore-perf.txt`.
  Les à-coups de 100 à 460 ms vus aux changements d'onglet venaient des captures d'écran du simulateur.

## Nouveautés v119 — FTP, clavier, mise à jour sans écriture possible
- FTP (`source/netfs.c`) : 2 sessions gardées ouvertes (50 s) et réutilisées, une de plus seulement si
  les deux sont prises ; une session gardée que le serveur a fermée est refaite une fois. Listes des 8
  derniers dossiers gardées 60 s : stat() / fopen() d'un fichier répondent sans le réseau (taille déjà
  connue : pas de SIZE). FTP et FSP : fichiers lus par morceaux de 32 Ko (4 gardés par fichier) : lire
  les étiquettes, la fin puis le début ne relance plus le transfert. Connexion : 3 essais (300 ms,
  700 ms) ; délais d'attente 10 s -> 6 s.
- Mesuré au simulateur (serveur à 30 ms par commande, `tools/pc-sim/ftp-run.sh`, scénario
  `ftp-test.txt`) : 15 connexions -> 2 ; pire blocage 2,4 s -> 0,28 s ; début de lecture 0,9 s -> 0,25 s.
  Avec un serveur limité à 2 connexions (`FTP_MAXCONN=2`, comme sur un téléphone) : avant, connexion
  refusée ; maintenant aucune.
- Clavier virtuel : curseur dans le texte (L / R, répétition en maintenant), saisie et effacement au
  curseur, la partie visible suit le curseur.
- Mise à jour : écriture sur la carte impossible (lecture seule, pleine, fichier non remplaçable) ->
  la version téléchargée reste en mémoire et A la lance (`up_to_memory`).

## Nouveautés v118 — navigateur : affichage classique, images entières, fluidité
- **CSS** (`source/web_css.c`) : feuilles `<style>`, `<link rel=stylesheet>` (lues après la page, gardées
  en mémoire, compressées gzip) et `style=""` ; sélecteurs balise / .classe / #id / combinés /
  descendants ; @media évalué pour un écran de 640 points (souvent la version « téléphone » du site).
  Propriétés : display (none / bloc / en ligne), visibility, opacity 0, clip, texte hors de l'écran
  (cachés), color, background(-color) (aussi la 1re couleur d'un dégradé), font-weight, font-size
  (px, em, rem, %, calc), text-align, text-decoration, border(-*), list-style. Attributs d'autrefois :
  bgcolor, color, align, `<center>`, `<font>`, `hidden`. Contraste vérifié (texte toujours lisible).
- **Mise en page** (`source/web_layout.c`) : pile des éléments ouverts (fermetures implicites de `<p>`,
  `<li>`, cases de tableau), fond blanc et texte foncé par défaut, liens bleus soulignés, encadrés
  colorés avec marge, lignes centrées / à droite, gras (texte dessiné deux fois), tableaux de 2 à 8
  colonnes en vraies colonnes (bordures, fonds de cases, largeur donnée), menus horizontaux
  (`li` en ligne), `<picture>` / `srcset` (la taille qui convient à l'écran).
- **Images** : JPEG décodés directement réduits (TJpgDec, `source/tjpgd`, 1/2 à 1/8) jusqu'à 40 millions
  de points ; WebP réduits au décodage ; JPEG progressifs trop grands pour stb_image : premier passage
  seulement (image 8 fois plus petite, agrandie en douceur) ; jusqu'à 600 points de haut (320 avant) ;
  3 téléchargements à la fois ; petite barre qui avance sur les images en cours.
- **Pages compressées** (gzip) : 5 à 10 fois moins de données ; les pages de plus de 1,5 Mo sont coupées.
- **Fluidité** : la remise en page (image qui donne sa taille, feuille de style lue) se fait dans le fil
  de décodage pour les pages de plus de 60 Ko : page de 600 Ko dans le simulateur, plus aucune image
  ratée en faisant défiler (avant : 90 à 170 ms de blocage à chaque image arrivée). Mémoire : sur une
  très grande page, moins d'images gardées.
- **Plein écran** : R (la page sur 446 points de haut au lieu de 272) ; dans le navigateur, R ne change
  plus de morceau (L oui).
- Banc d'essai : `tools/pc-sim/websrv/run.sh page.html [scénario]` (serveur de test avec gzip et délai,
  pages et images de test, scénarios `web-*.txt`).

## Nouveautés v117 — réseau et mise à jour sans carte SD
- Pile réseau recompilée (`tools/fast_net.py`) avec une fenêtre TCP de 44 paquets (64 240 octets, le
  maximum sans option TCP ; 16 avant) et 256 tampons de réception (128) : avec un serveur à 30 ms, la
  limite passe d'environ 780 Ko/s à environ 2 Mo/s (web, téléchargements, radios, mise à jour).
  En réseau local (SMB, FTP), la limite reste l'adaptateur réseau lui-même. La compilation dit
  maintenant si ce réseau rapide est bien inclus (note dans le résumé GitHub).
- Essayé puis écarté : lectures SMB de 128 Ko (SMB 2.1 « large MTU ») ; dans le simulateur, l'écran se
  figeait jusqu'à 409 ms à l'ouverture d'un morceau (54 ms avec 64 Ko). Le serveur de test SMB
  (`tools/pc-sim/netsrv.py`) vérifie désormais les numéros de demande et les crédits.
- Déjà en place : noms de serveurs gardés en cache, écritures sur la carte par blocs de 64 Ko, copies
  par blocs de 64 Ko. FSP : une demande à la fois par nature (le serveur donne une clé pour la suivante).
- Sans carte SD (Dolphin, ou pas d'adaptateur) : la mise à jour n'était pas possible ; maintenant le .dol
  est téléchargé en mémoire et lancé directement (`gl_boot_mem`), sans être enregistré.

## Nouveautés v116 — décors de saison testés dans tous les onglets
- Essai de chaque décor sur le tour complet du simulateur (12 onglets + menu START) : Noël coûtait
  13,3 s de calcul GameCube sur 21 s (84 images ratées), Hiver 10,2 s. Cause : chaque flocon = 2 cercles
  de 42 sommets, chacun envoyé à part. Maintenant les petits ronds (neige, lumières, étincelles, feux
  d'artifice) partent ensemble (10 triangles chacun, 128 par envoi) : Noël 7,7 s, Hiver 7,4 s (comme
  sans décor, ~7 s).
- Été (styles Nouvelle et Aurore) : soleil plus petit dans le coin (il cachait « En ligne ») ; Noël :
  sapin plus petit dans le coin (il cachait « < Tous les morceaux » et les boutons).

## Nouveautés v115 — décors de saison au choix
- Réglage « Décor de saison » : Non, Auto (selon la date), ou un décor imposé (gauche / droite ou A pour
  changer). gc-mp3.txt : `decor_saison = non / auto / printemps / ete / automne / halloween / hiver /
  noel / valentin / nouvelan` (l'ancien « oui » vaut « auto »).
- Dates en Auto : Nouvel An (31 déc.–1er janv.), Noël (déc.–6 janv.), Saint-Valentin (10–15 fév.),
  Halloween (15 oct.–2 nov.), Hiver, Printemps (dès le 20 mars), Été (dès le 21 juin), Automne (dès le 22 sept.).
- Décors (`source/saisons.c`), surtout sur les bords de l'écran : Halloween (chauves-souris, araignée qui
  descend, fantômes, brume, citrouilles), Noël (guirlande, neige sur deux plans, neige au sol, sapin
  illuminé, étoile filante), Hiver (stalactites, givre, neige), Printemps (pétales, fleurs, papillons),
  Été (soleil, mouettes, vagues, éclats), Automne (feuilles qui tombent et tas de feuilles), Saint-Valentin
  (cœurs qui montent en battant, guirlande de cœurs), Nouvel An (feux d'artifice, confettis).
- Simulateur : `PCSIM_SAISON=1..8` impose un décor (scénario `saisons.txt`).

## Nouveautés v114 — écran Minimal avec le logo complet
- L'écran de chargement Minimal affiche le logo de Pulse à l'identique (cube, hexagone ouvert, battement
  animé) au lieu du seul cube ; l'avancement passe dans une fine ligne en dégradé sous le nom du jeu.

## Nouveautés v113 — nouveau logo de Pulse
- Logo choisi parmi plusieurs maquettes : cube GameCube aux trois faces écartées (rose dessus, violet à
  gauche, bleu à droite) avec une lueur rose au centre, hexagone en dégradé diagonal (rose → cyan) et
  deux contours ; battement de type cardio (petite bosse, grand pic, petite bosse) blanc sur le cube,
  qui sort de l'hexagone en dégradé et s'efface ; l'hexagone passe par-dessus avec une ouverture en
  dégradé là où passe le battement.
- Dessiné en GX (`source/pulse_logo.c` : `pl_hexo`, `pl_cube`, `pl_beat`) : écrans de chargement Aurore,
  Cœur, Cinéma, la fenêtre de lancement des jeux ; l'écran Minimal montre le cube. Animation : la lueur
  et le battement s'allument à chaque battement, un contour s'éloigne.

## Nouveautés v112 — mise à jour sans « GitHub ne répond pas »
- L'API de GitHub n'accepte que 60 demandes par heure et par connexion sans compte ; chaque démarrage
  de GC Station en faisait jusqu'à 3 (mise à jour, retour en jeu, lanceur de secours) : après beaucoup
  de redémarrages (A+Z+START), « GitHub ne répond pas ».
- La publication met maintenant aussi `release.json` dans la release (texte + liste des fichiers) ; les
  consoles le lisent sur github.com (pas de limite), puis par l'API seulement s'il manque. Message clair
  si la limite est atteinte. Les anciens .dol ne sont supprimés qu'après release.json.
- `tools/verif_maj.py` : vérifie la release comme une console (release.json, numéro, chaque fichier
  téléchargé en entier et le numéro inscrit dans le .dol), lancé après chaque publication.

## Nouveautés v111 — 480p avec Pulse, 4 écrans de chargement
- 480p / mode progressif avec Pulse (Mario Superstar Baseball : écran noir après « Oui ») : le SDK des jeux
  n'applique un changement de mode qu'à l'interruption d'écran de la 1re ligne (DI1, getCurrentFieldEvenOdd
  dans VISetRegs). Le BIOS l'active, libogc (GC Station) non : avec la seule interruption du milieu
  d'image, le changement attendait pour toujours, écran déjà au noir. Pulse programme maintenant DI0 et
  DI1 comme le BIOS (__VIInit) juste avant de lancer le jeu (aussi pour le disque de la console).
  Journal du jeu : état de DI1.
- Réglages > Jeux > Écran de chargement (Pulse) : Aurore, Cœur (logo en grand qui bat), Cinéma (grande
  jaquette, ligne de battement qui se dessine), Minimal (l'hexagone se remplit), Au hasard.

## Nouveautés v110 — Mes applis fluide, seulement les applis avec image, Jeux / Apps façon Aurore
- Les pochettes des applis sont calculées dans le thread de fond (avant : dans la boucle principale,
  jusqu'à une demi-seconde par grand PNG : l'écran ramait). Travail générique du thread de fond
  (`gj_post_fn`).
- Seules les applis qui ont une image (icon.png, cover.png, pochette.png, boxart.png…) sont listées.
- Moderne, Nouvelle, Aurore : Mes applis en rangée de grandes pochettes qui défile (Aurore : reflet et
  halo), dans l'onglet (plus de fenêtre par-dessus l'en-tête). Aurore : l'onglet Jeux / Apps en cartes
  de verre, la sélection en dégradé qui glisse.

## Nouveautés v109 — Resident devient Pulse
- Le lanceur de GC Station s'appelle **Pulse**. Logo (`source/pulse_logo.c`, dessiné en GX) :
  hexagone du cube en néon (rose → violet → bleu → cyan), cœur en dégradé qui bat, trait de battement
  blanc, contours qui s'éloignent à chaque battement.
- Nouvel écran de chargement : noir et aurores, logo animé, jaquette avec halo, panneau en verre,
  barre et étapes en dégradé. Le nom remplace « Resident » dans tous les textes (réglages, outils,
  fiche du jeu, journaux, page du téléphone, traductions). Les fichiers et réglages gardent leurs
  noms (`SD:/gc-mp3/resident`, `resident_carte_virtuelle`…) pour ne rien perdre.
- Simulateur : `PCSIM_CHARGEMENT=1` affiche l'écran de chargement.

## Nouveautés v108 — retour par A+Z+START : plus de « GX ne répond plus » au redémarrage
- Après A+Z+START (Cocoto), GC Station se bloquait parfois à sa 1re image (« GX ne répond plus,
  copie de l'image vers l'écran », underflow=1) : le processeur graphique avait été arrêté au milieu
  d'une commande du jeu et prenait les premières commandes de GC Station pour la suite.
- Resident remet maintenant le processeur graphique à zéro avant de relancer GC Station (registre
  d'abandon du PI, comme GX_AbortFrame), et GC Station le fait aussi avant GX_Init.

## Nouveautés v107 — musique du disque sans grésillement
- La musique du disque (48 kHz) est ajoutée au son du jeu (32 kHz) : avant, un échantillon sur 1,5
  était simplement gardé, et les aigus se repliaient en grésillement. Maintenant chaque échantillon de
  sortie est la moyenne des échantillons du disque qu'il remplace : repli divisé par 7 à 100
  (18-22 kHz), la musique elle-même ne change pas (3 kHz : -0,3 dB). Même fréquence : inchangé.

## Nouveautés v106 — musique du disque de Cocoto Kart Racer sans saccades
- Journal de Cocoto Kart Racer (v105) : 16 452 tampons complétés pour 13 902 interruptions du son,
  dont 7 639 depuis le DSP : le jeu programme le tampon suivant, PUIS le DSP le calcule. La musique
  ajoutée était effacée par le DSP, puis ajoutée une 2e fois avec les échantillons suivants : elle
  avançait deux fois trop vite et un morceau sur deux manquait.
- Resident garde l'état du décodeur avant chaque ajout ; si le DSP réécrit ce même tampon, il revient à
  cet état et remet les mêmes échantillons. Banc d'essai : nouveau cas (échoue avec la v105).

## Nouveautés v105 — correctif : écran « BLOQUÉ, GX ne répond plus » en lançant un morceau (Aurore)
- Le lecteur Musique du style Aurore envoyait 8 sommets (pochette + reflet) au processeur graphique
  après en avoir annoncé 4 : la vraie console se bloquait. Corrigé.
- Simulateur : il s'arrête désormais sur toute erreur de ce genre (nombre de sommets différent de
  celui annoncé), comme la console ; tous les scénarios passent dans les 4 styles.

## Nouveautés v104 — curseur : tous les onglets en haut, la croix le cache
- Curseur (stick) tout en haut de l'écran : un bandeau descend avec tous les onglets (icône + nom),
  dans le style choisi ; A ouvre l'onglet survolé. Le bandeau remonte quand le curseur s'en va.
- Un appui sur la croix cache le curseur aussitôt ; le stick le fait revenir.

## Nouveautés v103 — Aurore : Musique, Jeux, menu START ; jeux dans chaque style
- Aurore, onglet Musique (`source/ui_aurore2.c`) : grande pochette avec reflet, lumière aux couleurs
  de la pochette qui respire avec la musique, titre en grand, barre d'avancement en dégradé, trois
  boutons (aussi au curseur). La liste des morceaux reste à gauche.
- Onglet Jeux : en Moderne, Nouvelle et Aurore, les jeux s'affichent en rangée de grandes jaquettes qui
  défile, dessinée dans le style choisi (Aurore : reflet et halo ; Nouvelle : cartes en verre ;
  Moderne : cartes plates avec ombre). R : jaquettes ↔ liste. Classique : inchangé (6 vues).
- Aurore, menu START : il monte et grandit en s'ouvrant, le fond s'assombrit.

## Nouveautés v102 — style Aurore
- START > Réglages > Affichage > Style : Classique, Moderne, Nouvelle, **Aurore**.
- Aurore (`source/ui_aurore.c`) : noir profond avec des aurores (rubans rose, violet, bleu) qui
  ondulent ; le nom de l'onglet en grand au centre de l'en-tête, les voisins s'effacent vers les bords
  et défilent quand on change d'onglet ; points d'avancement ; cadres presque invisibles avec un filet
  lumineux ; sélection en dégradé rose → bleu qui glisse ; la page arrive en zoom doux.

## Nouveautés v101 — Nouvelle interface
- START > Réglages > Affichage > Style : **Classique** (inchangé), Moderne, **Nouvelle**.
- Nouvelle (`source/ui_nova.c`) : seul le visuel change. Fond sombre bleuté avec des lumières
  douces qui bougent ; en-tête avec un dock d'icônes des onglets (l'onglet choisi s'ouvre en
  pastille avec son nom, les autres glissent) ; cartes en verre ; légende en pastille flottante ;
  la page glisse et apparaît en fondu en changeant d'onglet ; toutes les sélections (listes,
  grilles, réglages) glissent d'une ligne à l'autre, en couleur d'accent du thème.
- Les animations suivent le réglage « Animations » de l'onglet Jeux (Y > Animations : Non = tout de suite).

## Nouveautés v100 — tests de la musique, journal de l'écran
- Banc d'essai : la musique du disque est vérifiée quand le jeu programme le tampon du son dans
  l'interruption du DSP (Cocoto) et quand un tampon est rejoué (ces deux cas échouent avec la v97).
- Journal du jeu (Resident, en-tête v14) : état de l'écran (actif ou coupé, 480i / 480p, horloge
  27 / 54 MHz, câble numérique), nombre de changements de mode et l'image du dernier : pour l'écran
  noir de Mario Superstar Baseball après le choix du 480p.
- Lanceur de secours vérifié : son A+Z+START (code de Swiss) teste chaque manette à chaque lecture,
  il n'a pas le problème des ports vides corrigé dans Resident en v99.

## Nouveautés v99 — A+Z+START (Resident)
- Un port de manette vide que le jeu n'interroge pas garde 00000000, sans le bit d'erreur : Resident le
  prenait pour une manette sans A+Z+START et remettait le compte à zéro plusieurs fois par seconde
  (chaque fois que la manette 1 était sautée parce que le jeu n'avait pas encore pris ses données).
  Maintenant : une manette doit avoir le bit 0x0080, et un port sauté garde son dernier état.
- Banc d'essai : nouveau cas « ports vides à 0 » (échouait avant, passe maintenant).

## Nouveautés v98 — musique du disque sans saccades (Cocoto)
- Resident ajoute la musique du disque au tampon du son au moment où le jeu le programme, que ce soit
  dans l'interruption du son ou dans celle du DSP (Cocoto : avant, la musique tombait dans le tampon
  déjà en train d'être joué, donc à moitié). Un tampon rejoué tel quel ne reçoit plus une 2e couche.
- Musique lue par 4 secteurs (37 ms) au lieu d'un (9 ms) : 4 fois moins d'attentes de la carte.
- Journal du jeu : tampons complétés depuis le DSP, tampons rejoués.

## Nouveautés v97 — disque de la console, grosses cartes SD, Cocoto Kart Racer
- Onglet Jeux : case « Disque de la console » (avant le premier jeu) : lance le jeu du vrai lecteur
  comme le BIOS (reset du lecteur, ID du disque, musique en streaming, main.dol et FST lus sur le
  disque), sans Resident. Journal : `SD:/gc-mp3/natif/disque-console.txt`.
- Cartes SD de 1 To : la place libre est lue dans le secteur FSInfo de la FAT32 (avant : toute la FAT
  relue à chaque dossier de l'onglet Fichiers, dans Téléphone et Infos console) ; cache de libfat doublé.
- Resident : la table des morceaux de l'ISO est rangée tout en haut, à la taille utile : plus de place
  pour les aiguillages (Cocoto Kart Racer : « 3 accès au lecteur non branchés » depuis la v94).

## Nouveautés v96 — écran rouge corrigé
- Une lecture en fond de l'onglet Jeux finit avant que la carte SD soit libérée pour lancer un jeu.
- Reprise des fichiers de Swiss : gros fichiers copiés petit à petit, plus jamais déplacés.
- Journal du jeu : touches de la manette vues par Resident.

## Nouveautés v95 / v94 — disques à plusieurs programmes, lecture SD plus rapide
- v95 : OSExec (Zelda Collector's Edition : film, jeux du disque) suivi par Resident.
- v94 : lecture de la carte par DMA et à 32 MHz quand l'adaptateur le permet (CRC vérifié).

## Nouveautés v93 — A+Z+START enfin réparé, moins de saccades
- **A+Z+START (Resident)** ne marchait dans aucun jeu depuis la v89 : chaque fois que Resident regardait les manettes alors que le jeu n'avait pas encore pris leurs nouvelles données (très souvent, surtout depuis l'interruption du son), le compte des « 1 seconde tenue » repartait à zéro. Maintenant, une lecture impossible ne compte plus comme « boutons relâchés ». Le banc d'essai simule ce cas (données neuves une fois sur trois) : l'ancienne version échoue, la nouvelle passe.
- **Mises à jour du lanceur de secours et du retour en jeu** (~900 Ko chacun) : écrites sur la carte par morceaux de 32 Ko, un par image, au lieu d'un coup (~1,5 s d'écran figé, une fois après chaque nouvelle version, environ 25 s après le démarrage).
- **Flux RSS** (actualités de l'onglet INFOS, podcasts) : les recherches de balises restent dans l'épisode lu ; avant, une balise absente faisait relire tout le reste du document à chaque épisode (écran figé pendant le chargement des infos ou d'un gros podcast).

## Nouveautés v92 — vitesse, onglet Jeux fluide, disque de démarrage
- **Démarrage plus rapide** (environ 2 s de moins si la console démarre sur `ipl.dol`) : GC Station relisait tout son propre `.dol` (plusieurs Mo) à chaque démarrage pour se reconnaître, et une deuxième fois à la première ouverture des Réglages. La réponse est maintenant gardée (`SD:/gc-mp3/cache/dol-id.txt`).
- **Onglet Jeux** (simulateur, 120 jeux) : 42 → 58 images/s, pire image 2,4 s → 55 ms au deuxième passage.
  - Les jaquettes sont envoyées telles quelles au processeur graphique (format CMPR), sans conversion : 4 fois moins de mémoire, 96 jaquettes gardées au lieu de 32.
  - Les grandes images de la fiche (jaquette, capture) et les bannières des jeux sont lues **en fond** : l'écran ne s'arrête plus en passant d'un jeu à l'autre. En attendant la grande jaquette, la petite est affichée.
  - La bannière d'un jeu est trouvée sans lire toute la table des fichiers du disque.
- **Disque de démarrage** (mini-DVD gravé, consoles avec puce) : `gc-station-disque-pal / ntsc-u / ntsc-j-bN.iso` dans la release « dernier ». Au démarrage, si la carte SD a une version plus récente de GC Station (numéro écrit par GitHub à la fin du `.dol`), c'est elle qui est lancée : on grave le disque une fois, les mises à jour passent par la carte SD. B maintenu : rester sur la version du disque.
- `SD:/gc-mp3/demarrage.txt` : le temps de chaque étape du dernier démarrage.

## Nouveautés v91 — correctif : écran figé au premier démarrage de la v90
- La reprise des fichiers de `SD:/swiss` se faisait au démarrage, avant que l'écran réponde : la console restait figée. Elle se fait maintenant **en fond**, un fichier à la fois, quelques secondes après le démarrage (la musique et les menus marchent pendant ce temps).
- `SD:/gc-mp3/lanceur/migration.txt` est écrit **avant** de commencer : si quelque chose bloque, la reprise ne recommence jamais au démarrage suivant.
- Les fichiers de plus de 2 Mo (sauvegardes, jaquettes) sont **déplacés** (instantané) au lieu d'être copiés.

## Nouveautés v90 — plus rien de Swiss
- **Resident lance les jeux** (A dans la fiche d'un jeu). L'ancien lanceur devient le **lanceur de secours** (X, et les jeux avec des cheats activés, car Resident ne les applique pas encore). L'option « Swiss / Indigo » et la recherche de Swiss sur la carte SD disparaissent, ainsi que Z au démarrage.
- **Tout est dans SD:/gc-mp3** : le lanceur de secours lit maintenant ses réglages, sauvegardes et patches dans `SD:/gc-mp3/lanceur`, et ses cheats dans `SD:/gc-mp3/cheats` (compilé ainsi par GitHub : il se met à jour tout seul). Langue, son, vibrations, mode vidéo et position de l'image y sont recopiés, plus jamais dans `SD:/swiss`.
- **Reprise automatique, une seule fois** (`SD:/gc-mp3/lanceur/migration.txt`) : les réglages du lanceur (sans la ligne Autoload), les réglages par jeu, la liste des derniers jeux, les cheats, les sauvegardes et le retour en jeu sont copiés depuis `SD:/swiss`. Les jaquettes (`posters.pak`, `stills.pak`, `descriptions.txt`) sont déplacées dans `SD:/gc-mp3/jaquettes`, et `ipl-swiss.dol` devient `ipl-avant-gcstation.dol`.
- **Réglages > Jeux** : A+Z+START (Resident) et la carte virtuelle en premier ; « Lanceur de secours » et « A+Z+START (lanceur de secours) » (Oui / Non). **Réseau** : plus de reprise des serveurs de Swiss (ils sont déjà dans gc-mp3.txt). Le style classique s'appelle « Classique ».

## Nouveautés v89 — Resident : A+Z+START (Wind Waker) et couleurs (Paper Mario)
- **A+Z+START** ne marchait toujours pas dans Wind Waker : à l'image (VI), le jeu n'a pas encore pris les nouvelles données des manettes, et Resident ne doit pas les lire avant lui (sinon le jeu croit la manette débranchée, 1080). Il regarde maintenant aussi pendant l'interruption du son (~ toutes les 5 ms), où le jeu les a déjà prises. Retour après environ 1 s.
- **Couleurs** (Paper Mario : rouge et bleu inversés, avec un adaptateur HDMI sur la prise numérique) : comme Swiss, Resident met maintenant l'écran dans le mode du jeu avant de le lancer (PAL 576i, PAL 60 ou NTSC 480i). Avant, le mode de GC Station (souvent 480p) restait, et certains jeux le reprenaient tel quel.

## Nouveautés v88 — Resident : le son des jeux (1080, Wind Waker)
- **Son qui grésillait, coupait ou hachait avec Resident** (parfait avec Swiss) : les jeux lisent le disque pendant qu'ils jouent (musique, chargements), et Resident lisait la carte SD **8 Ko d'un coup dans l'interruption du lecteur**, interruptions coupées, un secteur à la fois : 10 à 20 ms pendant lesquelles le son du jeu n'était pas nourri (la sortie audio en demande toutes les ~5 ms).
- Maintenant :
  - **Plusieurs secteurs par commande** de la carte SD (CMD18 au lieu d'une commande CMD17 par secteur) : bien plus rapide.
  - **Au plus ~1,5 ms par interruption** (morceaux de 2 Ko, 8 Ko au plus), puis la suite à l'interruption suivante.
  - Les lectures de plus de 2 Ko (avant : 8 Ko) passent en fond.
- Banc natif-test : la fausse carte SD sait lire plusieurs secteurs (CMD18 / CMD12).

## Nouveautés v87 — Fichiers sur le réseau : SMB, FTP, FSP
- **Onglet FICHIERS** : B depuis la racine de la carte SD affiche les **appareils** (carte SD, partage SMB, serveur FTP, serveur FSP). On parcourt les dossiers du PC ou du NAS. **Musique** : A joue le morceau, et tout le dossier passe dans la file. **Photos et vidéos** s'ouvrent en plein écran. Les textes s'affichent. Copier / Coller vers la carte SD fonctionne. Accès en lecture seule.
- **START > Réglages > Réseau** : *Partage SMB*, *Serveur FTP*, *Serveur FSP* : adresse, partage, utilisateur, mot de passe, port, **Tester la connexion**, **Reprendre les réglages de Swiss**, **Ouvrir dans FICHIERS**. Au premier démarrage, les réglages de Swiss (`SD:/swiss/settings/global.ini`) sont repris tout seuls. Ils sont gardés dans `gc-mp3.txt` (`smb_…`, `ftp_…`, `fsp_…`).
- Clients écrits pour GC Station (`source/netfs.c`, `source/smb2.c`) :
  - **SMB2** (2.0.2 / 2.1, Windows 10 / 11, Samba, NAS) : identification NTLMv2 (MD4 et NTOWFv2 vérifiés avec les vecteurs officiels RFC 1320 / MS-NLMP), signature HMAC-SHA256 quand le serveur l'exige (Windows 11), invité possible. Le vieux SMB1 de Swiss est désactivé par défaut sur Windows récent.
  - **FTP** (EPSV / PASV, MLSD ou LIST Unix / Windows, reprise avec REST).
  - **FSP** (UDP, comme Swiss, mot de passe).
- Sur la console, chaque serveur est un disque comme la carte SD (`smb:/`, `ftp:/`, `fsp:/`). Simulateur : serveurs de test `tools/pc-sim/netsrv.py`, scénarios `reseau*.txt`.

## Nouveautés v86 — Resident : A+Z+START marche dans les jeux récents
- **Wind Waker démarre avec Resident** depuis la v84, sans Swiss.
- **A+Z+START** (retour à GC Station) ne faisait rien dans Wind Waker : Resident ne regardait les manettes que dans l'interruption des manettes (SI), et les jeux faits avec un kit Nintendo récent lisent les manettes à chaque image, sans presque plus d'interruption SI. Resident regarde maintenant aussi à chaque image, après le jeu. Le retour se fait après environ 1 s (ou 60 lectures des manettes de suite).

## Nouveautés v85 — Fluidité pendant la musique
- **La musique se décode pendant le temps libre de chaque image** (l'attente de l'écran) au lieu de passer avant le dessin. Un bloc n'est décodé que s'il tient avant l'image suivante (durée mesurée, FLAC haute résolution compris). Avant l'image, on ne décode qu'en secours, quand il reste moins de 0,5 s de son d'avance. Simulateur limité à la GameCube : images ratées pendant la lecture de 100–106 → 10–21, son identique.
- **Lecture des MP3 sur la carte SD par 8 Ko** (avant : jusqu'à 48 Ko d'un coup, des dizaines de ms bloquées).
- **Étiquettes des MP3 sans la pochette intégrée** : les cadres ID3 sont lus un par un, et la grosse image est sautée. Lancer un morceau avec une pochette intégrée de 300 Ko : 499 ms figées → 308 ms la première fois (le reste, c'est l'image elle-même ; ensuite elle vient du cache). La bibliothèque (vues Artistes / Albums) trouve maintenant l'artiste et l'album même quand la pochette est rangée avant eux.
- Simulateur : l'horloge vue par GC Station compte le calcul de la GameCube au moment où il a lieu (avant, la dette n'était comptée qu'au prochain appel bloquant : durées mesurées fausses) ; scénario `id3-pochette.txt`.

## Nouveautés v84 — Wind Waker (et les jeux PAL) avec Resident ; onglet Jeux / Apps
- **Resident : les jeux PAL démarrent.** Grâce aux vrais registres de la v83, le journal de Wind Waker a enfin donné son message : `vi.c` ligne 1884, « VIConfigure(): Tried to change mode ... which is forbidden ». La console doit indiquer au jeu son mode vidéo (adresse `800000CC`), ce que fait normalement son menu. GC Station démarre sans ce menu : la valeur restait 0 (NTSC) et tout jeu PAL s'arrêtait exprès à son démarrage. Resident donne maintenant PAL aux jeux PAL (EURGB60 si GC Station est déjà en PAL 60 Hz) et NTSC aux jeux NTSC. `dernier-lancement.txt` indique le mode donné.
- **Onglet Jeux / Apps** : une barre **Jeux | Applis | Cartes** en haut des trois vues, et le **stick C (gauche / droite)** passe directement de l'une à l'autre, sans revenir à l'onglet. B ramène sur la bonne barre de l'onglet. La liste des applis est gardée (avant : la carte SD était relue à chaque ouverture) ; Y la relit.
- Simulateur : la durée de `stick` est bien lue ; scénario `jeux-apps.txt`.

## Nouveautés v83 — Fluidité (réseau) et Resident : photo de la mémoire au plantage
- **Fluidité** : les paroles, pochettes, météo, infos, podcasts et la vérification de mise à jour ne figent plus l'écran. La recherche du site, la connexion et la négociation HTTPS (jusqu'à 1 s de calcul pour la GameCube) se font maintenant dans un fil de faible priorité, qui ne prend que le temps libre, après l'affichage et le décodage du son. Mesuré dans le simulateur limité à la GameCube : pire image 250–300 ms → 60 ms, tous les onglets et les Réglages à 60 images/s.
- **Resident** : au plantage d'un jeu, les registres notés sont maintenant les vrais. Avant, r0–r2 et r6–r31 étaient d'anciennes valeurs, donc la pile était lue au mauvais endroit.
- **Resident** : au plantage, une **photo de la mémoire du jeu** est enregistrée dans `gc-mp3/natif/memoire-jeu.bin` (352 Ko : mémoire basse, variables du jeu, pile, début de l'arène avec les tas du jeu). Pour Wind Waker, elle dira pourquoi le jeu estime manquer de mémoire (`mDoMch_Create`, `m_Do_machine.cpp` ligne 1143).
- Simulateur : commande `mark` dans les scénarios (fluidité par portion), `PCSIM_PERF=1` (rapport `fluidite.txt` de GC Station), scénario `fluidite-onglets.txt` (tous les onglets et les Réglages).

## Nouveautés v82 — Resident : le message d'erreur des jeux qui s'arrêtent
- Wind Waker s'arrête **exprès** avec Resident : il appelle sa propre fonction d'erreur (qui écrit à l'adresse `01234567`) avant sa première image, sans avoir rien lu sur le disque. Son message n'allait nulle part.
- Au plantage, Resident enregistre maintenant **la pile du jeu** : `journal-jeu.txt` a une partie « Pile du jeu au plantage » avec les **textes** trouvés (le nom du fichier du jeu, la ligne et le message de l'erreur, en général) et les endroits du jeu qui ont appelé la fonction d'erreur.
- Le journal du jeu passe à 3 secteurs (les 2 derniers ne sont écrits qu'en cas de plantage).

## Nouveautés v81 — mesure de la mémoire corrigée
- Le contrôle au démarrage (v76) mesurait la mémoire libre en essayant de réserver jusqu'à 12 Mo d'un coup. Ça ne gênait pas l'utilisation, mais ça faussait la mesure du simulateur `tools/pc-sim` (pic à 12,4 Mo et une réservation refusée, uniquement à cause de ce test). Il lit maintenant la mémoire libre sans rien réserver, comme Outils > Infos console. Mesure réelle dans pc-sim pendant le tour de tous les onglets : **2,4 Mo utilisés sur 12,7 Mo**, aucune réservation refusée.

- **Le lanceur de jeux et le fichier du retour en jeu ne sont plus retéléchargés à chaque mise à jour** : sur GitHub, ils gardent le numéro de la version où ils ont vraiment changé (et ne sont pas renvoyés s'ils sont identiques). GC Station ne les télécharge que si GitHub en a un plus récent. Une mise à jour ne télécharge donc plus que le `.dol`. Réactiver le retour en jeu dans le menu réutilise la copie gardée sur la carte, sans réseau.

## Nouveautés v80 — optimisée pour la vraie GameCube
GC Station a été mesurée dans un nouveau simulateur qui a les limites de la console (12,7 Mo de tas, processeur Gekko, carte SD et Broadband Adapter à leur vrai débit) : voir `tools/pc-sim`.
- **Le son ne coupe plus quand l'interface ralentit** (chargement de photos, pages web, carte SD lente) : sous 2 s de son d'avance, GC Station rattrape en décodant autant que possible en 8 ms, au lieu de 2 trames par image seulement. Mesure (FLAC 24 bits / 96 kHz pendant le diaporama de photos 1920 × 1080) : 125 coupures avant, aucune après.
- **Photos plus fluides au chargement** : elles sont lues sur la carte SD par morceaux de 8 Ko, au plus 6 ms par image (avant : 32 Ko d'un coup, plus long qu'une image entière sur une carte SD de GameCube). Images très lentes pendant le même essai : 164 avant, 62 après.
- Encore lourd pour la console : le FLAC **24 bits / 96 kHz** (environ 35 % du processeur, par blocs) : préfère le FLAC 16 bits / 44,1 kHz ou le MP3 si l'image saccade.

## Nouveautés v79 — Resident : pourquoi un jeu plante
- Quand un jeu plante avec Resident, `SD:/gc-mp3/natif/journal-jeu.txt` contient maintenant une partie **« Détails du plantage »** : tous les registres du jeu, la **chaîne des appels** (quelles fonctions du jeu ont mené au plantage), les **textes que le jeu avait en main** (souvent le message ou le nom de fichier de son erreur), la mémoire que le jeu a reçue (et combien il en aurait eu sur une vraie console), et le code du jeu autour de chaque adresse.
- Une adresse visée `01234567` est signalée : c'est un jeu qui s'arrête **exprès** après avoir détecté une erreur (Wind Waker au démarrage).
- Le journal du jeu passe à 2 secteurs (le 2e n'est écrit qu'en cas de plantage). Si le jeu plante avant sa 1re commande au lecteur, la carte SD est démarrée pour l'écrire quand même.
- Banc d'essai : le faux jeu plante comme Wind Waker (message en main, adresse bidon, 2 appels sur la pile) et le test vérifie les détails écrits sur la carte.

## Nouveautés v78 — la mise à jour se télécharge de nouveau
- **Correction** : START > « Mise à jour » affichait « Téléchargement interrompu ». GitHub répond maintenant avec plus de 11 Ko d'en-têtes avant de donner le fichier, et GC Station n'en acceptait que 8 Ko. Les lignes inutiles sont maintenant oubliées : le téléchargement passe, même avec des en-têtes bien plus gros.
- **Important** : une console qui a encore une build plus ancienne ne peut pas se mettre à jour toute seule une dernière fois. Il faut copier une fois `gc-mp3player-b…dol` (release « dernier ») sur la carte SD, à la place de l'ancien fichier. Ensuite, START > « Mise à jour » refonctionne.

## Nouveautés v77 — cartes mémoire virtuelles vérifiées
- **Correction** : quand un jeu avait sa propre carte virtuelle (fiche du jeu, Y > « Carte mémoire (Resident) ») **et** une langue ou un son à lui, la carte de ce jeu devenait par erreur la carte de tous les autres jeux après son lancement. La carte générale (START > Outils > Cartes mémoire virtuelles) est maintenant toujours gardée.
- Le message d'erreur « carte trop morcelée » donne le nom de la carte réellement utilisée.
- Tout le reste a été revérifié : carte neuve et formatage, copie de sauvegardes (vers et depuis la SD, vers et depuis la vraie carte), suppression, carte pleine, plusieurs cartes, 400 cartes abîmées relues sans plantage.

## Nouveautés v76 — notes, réveil malin, favoris
- **Notes (étoiles) sur les morceaux** : dans MUSIQUE, touche **X** sur un morceau → ligne « Note » (A ou gauche/droite pour mettre 0 à 5 étoiles). Les étoiles s'affichent dans la liste, et une nouvelle liste **« Mes préférés »** (vue « Mes listes ») réunit les morceaux notés, du mieux noté au moins bien.
- **Minuterie plus fine** : START > « Minuterie » propose maintenant **à la fin du morceau**, 5, 10, 20, 30, 45, 60, 90 et 120 minutes.
- **Réveil malin** (onglet INFOS) : choix des **jours** (tous les jours / du lundi au vendredi / le week-end) et, quand le réveil sonne, touche **X = +10 minutes** (report).
- **Photos triées par date** : dans l'onglet PHOTOS, **L / R** change le tri (par nom ou par date, les plus récentes d'abord).
- **Jeux favoris** : dans la fiche d'un jeu (JEUX / APPS), touche **L** pour mettre le jeu en favori. Les favoris remontent en haut de la liste. (Le temps de jeu total par jeu, déjà mesuré par Resident, reste affiché dans la fiche.)
- **Trois thèmes de couleur en plus** : Lavande, Forêt, Crépuscule (START > « Thème »).
- **Contrôle au démarrage** : GC Station note dans son journal la mémoire libre, l'état de la carte SD et le temps de démarrage, et prévient brièvement si la carte est en lecture seule ou si la mémoire est presque pleine.

## Nouveautés v75 — fini les pages Indigo / Swiss : les réglages de GC Station
Les pages de réglages qui n'étaient en fait que des réglages de Swiss (`swiss/settings/global.ini`) sont supprimées. À la place, des sections propres à GC Station (START, L / R) : **Musique, Radio, Affichage, Vidéo, Jeux, Réseau, Outils**.
- **Vidéo** : nouvelle ligne « Position horizontale » (réglage de la console : décale l'image de GC Station et des jeux).
- **Jeux** : « Langue des jeux (console) » et « Son (console) » : réglages de la vraie console, utilisés par tous les jeux (Resident compris), plus vibrations, lanceur, IGR…
- **Réglages d'un jeu** : dans la fiche d'un jeu, **Y** : langue du jeu, son, carte mémoire virtuelle (Resident), pour ce jeu seulement (`SD:/gc-mp3/jeux/<ID>.txt`). La console reprend ses réglages après le jeu.
- **Réseau** : adresse automatique (DHCP) ou fixe (adresse, masque, passerelle, tapées au clavier et vérifiées), télécommande et vérification des mises à jour au démarrage.
- **Affichage** : fichiers cachés / fichiers inconnus de l'onglet FICHIERS.
- **Outils** : « Quitter GC Station ».
- Au premier démarrage, l'adresse réseau fixe et l'affichage des fichiers cachés déjà réglés dans Swiss sont repris. Les jeux lancés avec Swiss ou le lanceur gardent les mêmes vibrations, mode vidéo et IGR (GC Station les recopie pour eux).

## Nouveautés v74 — tout le réseau vérifié en vrai
Chaque fonction réseau a été essayée pour de vrai sur Internet (le programme lui-même, dans un simulateur sur un serveur GitHub) : 79 essais. Ce qui ne marchait plus a été réparé :
- **Podcasts : l'écoute ne marchait plus** (les serveurs de Radio France refusent maintenant les vieilles requêtes HTTP/1.0 : erreur 426). GC Station repasse tout seul en HTTP/1.1 (avec les réponses « en morceaux »), pour les podcasts comme pour les radios ; le navigateur web passe aussi en HTTP/1.1.
- **Recherche de podcasts : 0 résultat** pour beaucoup de podcasts français (iTunes ne donne plus l'adresse de leur flux). GC Station cherche maintenant aussi dans les annuaires libres fyyd.de et gpodder.net.
- **Recherche web : Mojeek refusait (erreur 403)**. Le moteur par défaut est maintenant DuckDuckGo ; si un moteur refuse, GC Station essaie tout seul Bing, puis Mojeek, puis Marginalia. Les liens des résultats Bing mènent directement au site. L'ancien favori « Recherche web (Mojeek) » est remplacé.
- **Titres des radios de Radio France** (FIP, France Inter, franceinfo, France Culture, France Musique, Mouv') : leurs flux n'annoncent rien ; les titres sont maintenant demandés à Radio France (le morceau en cours, toutes les 20 s) : affichés, gardés dans l'historique avec leur pochette.
- **Pochettes automatiques** : MusicBrainz donnait souvent la pochette d'une compilation (« Now! 6 », « Boom 2001 »…) ; il compare maintenant 25 résultats, écarte les compilations, mixes et albums d'autres artistes, et demande à iTunes quand il n'est pas sûr. MusicBrainz et LRCLIB occupés (erreur 503) : nouvel essai quelques secondes après.
- **Annuaire des radios** : un des serveurs n'existe plus (retiré) ; les radios en Opus ou FLAC (illisibles sur GameCube) ne sont plus proposées.
- **Vérifiés et qui marchent** : les 8 radios de la liste, l'annuaire (MP3, AAC, HLS, https), la réparation des adresses mortes, les podcasts (flux, écoute, avance, ×1,5, téléchargement), paroles (LRCLIB), météo, actualités, mise à jour (vérification et téléchargement), pages web, archive.org, certificats HTTPS de tous les sites utilisés.
- **Réglages** : chaque réglage du menu START et de `gc-mp3.txt` a été vérifié (lu, enregistré, et utilisé) : aucun n'était factice. Seule limite : les réglages de la section Indigo / Swiss s'appliquent aux jeux lancés avec Swiss ou le lanceur GC Station, pas avec Resident.

## Nouveautés v73
- **Listes intelligentes** (MUSIQUE, vue « Mes listes ») : **« Jamais écoutés »** (mélangés, un autre ordre chaque jour) et **« Ajoutés récemment »** (date d'arrivée des fichiers sur la carte, lue une fois puis gardée dans `SD:/gc-mp3/cache/ajouts.txt`).
- **Égaliseur 5 bandes** : graves, **bas-médiums** (350 Hz), médiums, **présence** (4 kHz), aigus ; 15 réglages tout prêts par genre (nouveaux : Métal, Reggae, Acoustique, Soirée, R&B / Soul, Petit volume).
- **Radio** : ligne **« Titres passés (historique) »** en haut de la liste RADIO : chaque titre annoncé est noté tout seul (`SD:/gc-mp3/radio/historique.txt`) **avec sa pochette** (cherchée sur iTunes, gardée en cache) ; A : chercher dans ma musique, X : garder, Y : effacer.
- **Enregistrement programmé** : START > « Enregistrement programmé » (heure, par demi-heures) et « Durée » (15 min à 4 h) : à l'heure dite, la radio qui jouait quand tu as réglé l'heure est lancée et enregistrée dans `SD:/gc-mp3/radio/enregistrements`, une fois par jour.
- **Podcasts** : **vitesse ×1,25 / ×1,5** sans voix aiguë (étirement du temps WSOLA) et **blancs sautés** (plus d'un quart de seconde) ; la position et la reprise suivent la vitesse.
- **Fiche d'un jeu** : nombre de lancements, dernière date **et temps de jeu** (mesuré par Resident à chaque partie).
- **Cartes mémoire virtuelles** (START > Outils) : plusieurs cartes pour Resident (Y : celle du slot A), liste des sauvegardes, **copie depuis / vers la SD (.gci) et la vraie carte** (slot A ou B), suppression, formatage, nouvelle carte.
- **Jeu de rythme à 5 touches** : Y dans l'écran de choix : A B X Y + **R** (aussi en duel).
- **Téléphone** : onglets **Fichiers** (parcourir la carte SD, toucher un fichier pour le télécharger) et **Jeux** (liste des jeux GameCube, bouton Lancer ou Resident).
- **Sauvegarde de tous les réglages** en un fichier : START > Outils > « Sauvegarder tous les réglages » (`SD:/gc-mp3-sauvegarde.txt` : réglages, radios, favoris, listes, podcasts, statistiques…) et « Restaurer » (A deux fois).
- **Économiseur « Horloge + météo + pochette »**.
- Les **paroles automatiques** existaient déjà (LRCLIB, START > « Paroles automatiques »).
- **Tests** : nouveau test qui prépare tout seul sa carte SD et son serveur (radio avec titres, pochettes) : égaliseur, vitesse et blancs, listes, sauvegarde (+ 3000 sauvegardes abîmées), cartes virtuelles (+ 400 cartes abîmées), rythme, historique, enregistrement programmé, téléphone.

## Nouveautés v72
- **Musique sans blanc** : les morceaux d'un même album (même dossier) s'enchaînent sans silence ; pour les MP3, le silence ajouté par l'encodeur (en-tête LAME) est retiré au début et à la fin. Réglage START > « Enchaînement sans blanc ».
- **Volume égalisé plus précis** : sans étiquette ReplayGain, GC Station mesure la sonie de chaque morceau pendant qu'il joue (norme EBU R128, comme ReplayGain 2) et l'applique dès l'écoute suivante (`SD:/gc-mp3/volumes.txt`) ; le gain calculé par l'encodeur LAME est aussi utilisé.
- **Radio** : **favorites** (X maintenu sur une radio : étoile, en haut de la liste ; X court garde le titre comme avant) ; **la dernière radio reprend au démarrage** (réglage) ; **moins d'erreurs 404** : si une de tes radios ne répond plus, GC Station cherche sa nouvelle adresse dans l'annuaire (même nom), l'essaie, et corrige `radio.txt` tout seul si elle marche ; redirections relatives et adresses avec espaces acceptées.
- **Fichiers** : **Y coche** plusieurs fichiers ou dossiers, puis X : copier, couper/coller, supprimer, ou **renommer en lot** (« Nom 01 », « Nom 02 »…) ; la copie prend aussi les dossiers entiers ; « Tout sélectionner ».
- **Écran de chargement de Resident plus fluide** : une image à chaque balayage de l'écran sans freiner le chargement, le jeu est lu par morceaux de 128 Ko (l'écran bouge pendant la lecture et l'analyse), la barre glisse en douceur.
- **Sécurité** : nouveau test qui abîme volontairement des milliers de fichiers (MP3 avec en-tête LAME, pages web, RSS, listes de lecture, en-têtes HTTP, WAV, OGG, ISO GameCube) : aucun plantage ; tous les autres tests de formats repassés.

## Nouveautés v71
- **Radio** : démarre plus vite (1 s de tampon au lieu de 2 ; plus seulement si le flux a déjà coupé). Si la radio s'arrête (réseau, serveur), GC Station **se reconnecte tout seul**, de plus en plus espacé (1, 2, 4, 8, 15 puis 30 s), au lieu de rester sur une erreur ; la reconnexion reprend directement l'adresse finale (sans redirections ni liste .m3u).
- **Podcasts** : un épisode déjà téléchargé (à la main ou tout seul) est **lu depuis la carte SD** (sans réseau ni attente), avec la même reprise là où tu t'étais arrêté ; la position est gardée toutes les 5 s.
- **Mini-jeux** (Snake, casse-briques, Pong, note volante) : les effets et les notes dorées tombent sur le coup **entendu** ; dans la note volante, chaque mur arrive sur l'oiseau **pile sur un coup** du morceau.
- **Quiz musical** : questions « Quel morceau ? » et « Qui chante ? » (si les étiquettes le disent), extraits de plus en plus courts (10 s, puis 6 s, puis 3 s), et les morceaux des parties d'avant ne retombent pas tout de suite.
- **Corrections** (analyse de tout le code) : plantage possible quand une radio donnée par son adresse IP ne répondait pas ; division par zéro dans les photos en plein écran sans photo.

## Nouveautés v70
- **Jeu de rythme refait** : les coups (grosse caisse, caisse claire, charleston) sont cherchés dans le son **déjà décodé, avant qu'on l'entende** : chaque note tombe exactement sur un vrai coup du morceau (avant : une grille de tempo devinée, qui se décalait au moindre retard du son). Les notes suivent l'horloge du son entendu (pause, petit retard : elles restent calées). Facile / Normal : surtout les coups graves ; Difficile : tous. **L / R** dans l'écran de choix : décalage des notes (±10 ms) si ta télé ajoute un retard ; il est gardé.

## Nouveautés v69
- **Mesure de fluidité** (START > « Mesure de fluidité ») : images par seconde, pire image et tâche la plus lente affichées en haut de l'écran. En coupant la mesure, GC Station écrit `SD:/gc-mp3/fluidite.txt` : pour chaque onglet, le nombre d'images ratées et ce qui les a causées (réseau, pochettes, décodage, dessin, processeur graphique…). À envoyer pour corriger les saccades à coup sûr.

## Nouveautés v68
- **Onglet JEUX / APPS plus fluide** : les pochettes des applis sont calculées une seule fois puis gardées dans `SD:/gc-mp3/cache/applis` (avant, le PNG était décodé et recalculé à chaque passage : ça saccadait). Pendant qu'on navigue, seule la pochette choisie se charge (jeux et applis), les autres quand on s'arrête ; une seule lecture par image au lieu de 3 ; plus de jaquettes gardées en mémoire.
- Resident : un **« machine check »** (accès matériel invalide) du jeu est maintenant attrapé comme un plantage : journal + retour à GC Station au lieu d'un écran noir figé.

## Nouveautés v67
- Écran noir (Paper Mario) : tant que le jeu n'a pas affiché sa 1re image, Resident regarde régulièrement **où en est le jeu** (adresse du code, état du DSP). Au démarrage suivant, `journal-jeu.txt` montre ces adresses et le code du jeu autour (lu dans l'ISO), pour trouver ce qu'il attend.

## Nouveautés v66
- **Paper Mario (écran noir)** : la commande « infos lecteur » du jeu reçoit maintenant la **vraie réponse du lecteur** (date, révision) au lieu d'une réponse inventée. Le journal du jeu dit aussi quelles fonctions du jeu Resident a branchées et si le jeu reste bloqué dans sa fonction d'image (pour trouver la suite si l'écran reste noir).
- **Nouvel écran de chargement de Resident**, dans le style de GC Station : jaquette du jeu avec halo, panneau arrondi, barre de progression lumineuse, étapes cochées une à une, rappel « A + Z + START = retour à GC Station ».
- **Carte mémoire virtuelle** pour les jeux lancés avec Resident : le slot A est émulé et les sauvegardes vont dans `SD:/gc-mp3/resident/carte-A.raw` (2 Mo, 251 blocs). Le fichier est créé vide au 1er lancement : le jeu propose alors de formater la carte, comme une carte neuve. Réglage START > « Carte mémoire virtuelle (Resident) ». Le slot B, l'horloge et la SRAM restent les vrais. Si un jeu parle à la carte d'une façon que Resident ne sait pas encore brancher, la carte virtuelle est coupée pour ce jeu (le journal dit pourquoi) et le jeu garde la vraie carte du slot A.
- **Musique du disque (audio en streaming)** : Resident lit l'ADPCM dans l'ISO, le décode et l'ajoute au son du jeu, au volume et à la fréquence choisis par le jeu (1080° Avalanche, Zelda, F-Zero…).
- Journal du jeu : blocs de musique décodés, commandes et écritures de la carte virtuelle. Rapport d'analyse : accès EXI (carte mémoire) trouvés et branchés.
- Banc d'essai : le faux jeu teste aussi la musique du disque et la carte mémoire (accès EXI écrits comme ceux du SDK, adresse calculée pendant le jeu).

## Nouveautés v64
- **Écran de chargement de Resident** (style « Minimal ») : nom du jeu, ligne de progression, étape en cours, « GC STATION · RESIDENT ».
- **A + Z + START (et un plantage) ramènent directement sur GC Station**, sans redémarrer la console : Resident relit le `.dol` de GC Station sur la carte et le lance (avant, le redémarrage passait par le menu de la GameCube).
- **Le lecteur reste silencieux** : après son réveil (nécessaire pour Resident), son moteur est arrêté. Quand on revient d'un jeu Resident, il n'a même plus besoin d'être redémarré.

## Nouveautés v63
- Le lanceur maison s'appelle maintenant **Resident** (dans « Lancer ce jeu ? » : **X : Resident**).
- **A+Z+START avec Resident remarche** et la manette du jeu aussi (1080° Avalanche : retour à GC Station en jeu). Réglage « A+Z+START (Resident) », de nouveau actif par défaut.

## Nouveautés v62
- Lanceur maison : les « signes de vie » et la détection de plantage se branchent aussi quand le jeu n'a fait qu'une commande au lecteur (Paper Mario : écran noir juste après la 1re commande, le journal ne disait rien de la suite).

## Nouveautés v61
- **Lanceur maison : 1080° Avalanche est jouable !** (sans Swiss, l'ISO lue sur la carte SD par le programme résident de GC Station).
- **Son** : les grosses lectures (512 Ko) se faisaient d'un coup, interruptions coupées pendant ~0,5 s, ce qui faisait craquer le son. Elles se font maintenant **en fond, par morceaux de 8 Ko**, entre lesquels la console continue de servir le son et l'image, comme avec un vrai lecteur. Le journal du jeu s'écrit aussi moins souvent (moins de micro-coupures).
- Pas encore : la **musique lue directement sur le disque** (« audio en streaming », comme dans 1080°) n'est pas jouée.

## Nouveautés v60
- **Lanceur maison : si le jeu plante, la console redémarre toute seule** (retour sur GC Station si c'est ton `ipl.dol`), et `journal-jeu.txt` dit où et comment il a planté.
- **Signes de vie** dans le journal : image, son, DSP et dessin. Le journal est réécrit toutes les 3 s tant que le jeu vit, même sans lecture du disque : on sait s'il est figé ou s'il attend quelque chose.
- A+Z+START dans le lanceur maison est **coupé par défaut** : il bloquait la manette de 1080° Avalanche. Réglage « A+Z+START (lanceur maison, test) ».

## Nouveautés v59
- **Lanceur maison : manette.** La surveillance de A+Z+START lit maintenant la manette exactement comme le jeu (haut puis bas du registre), et seulement après lui. Nouveau réglage **GC Station > « A+Z+START (lanceur maison) »** pour la couper.
- `journal-jeu.txt` dit ce que la surveillance a vu de la manette (touches, changements).

## Nouveautés v58
- **Lanceur maison : la manette remarche dans le jeu.** En v57, la surveillance de A+Z+START lisait la manette avant le jeu : le jeu la croyait débranchée (1080° Avalanche restait figé sur la question 60 Hz). Elle ne lit maintenant que ce que le jeu a déjà lu.

## Nouveautés v57
- **Réglages rangés par thème** : sections **Vidéo**, **Manettes**, **Émulation**, **Jeux** (lancement des jeux, matériel) et **Réseau**, à côté de **GC Station** et **Outils**. Plus de sections « Indigo » ni « Swiss » ; les options qui ne servaient qu'au menu d'Indigo ne sont plus montrées (le fichier `swiss/settings/global.ini` les garde).
- **Démarrer la console sur GC Station** (section GC Station) : GC Station se copie dans `SD:/ipl.dol` (celui que lance PicoLoader) ; l'ancien est gardé sous `ipl-swiss.dol`. Le bouton **Reset** ramène alors sur GC Station ; **Z** tenu au démarrage de GC Station ouvre toujours Swiss. « Quitter vers Swiss » n'est plus dans les menus.
- **Lanceur maison : A + Z + START** tenus 1 s en jeu = retour à GC Station (la console redémarre).
- 1080° Avalanche démarre (choix 60 Hz) avec le lanceur maison. Le journal du jeu est maintenant écrit 2 fois par seconde et toutes les 3 s même si le jeu bloque, avec les commandes spéciales (musique du disque, etc.).

## Nouveautés v56
- **Lanceur maison : le vrai lecteur est réveillé avant le jeu.** Le journal de 1080° Avalanche montrait que le jeu envoie sa 1re commande, puis attend un « réveil » qui ne vient jamais (10 s, puis « Une erreur est survenue »). Ce réveil vient du vrai lecteur DVD, que Swiss peut laisser endormi (en reset). GC Station le libère maintenant et vérifie qu'il répond avant de lancer le jeu ; le résultat est noté (« Vrai lecteur : … »).
- `journal-jeu.txt` montre aussi quand le jeu se réveille (« jeu : écrit DISR ») et contient à la fin le journal du lancement.

## Nouveautés v55
- **Partager un fichier par QR code** : onglet FICHIERS, **X > Partager (QR code)** sur un fichier → un QR code s'affiche ; scanne-le avec ton téléphone (même Wi-Fi que la GameCube) pour ouvrir une page avec le nom, la taille, un aperçu (photo, musique, vidéo, texte) et le bouton **Télécharger**.
- Marche aussi sur un **dossier** (ou, sur la ligne du haut, le dossier ouvert) : la page liste ses fichiers, chacun téléchargeable.
- Chaque partage a un lien secret ; il marche tant que GC Station est allumé. Les vidéos se lisent directement dans le navigateur du téléphone (lecture par morceaux).

## Nouveautés v54
- **Lanceur maison : 1080° Avalanche démarre !** (les 2 accès `lwzu` sont branchés depuis la v53). Le jeu affiche ensuite « Une erreur est survenue » : il faut savoir ce que le faux lecteur lui a répondu.
- **Journal pendant le jeu** : le programme résident écrit son état sur la carte SD pendant le jeu (compteurs + les 24 dernières commandes du jeu au lecteur). Au démarrage suivant de GC Station, il devient `SD:/gc-mp3/natif/journal-jeu.txt` (aussi sur le téléphone, page « Analyses des jeux »).
- Le programme résident redémarre la carte SD et réessaie (jusqu'à 3 fois) si une lecture échoue, et attend le vrai lecteur s'il est occupé au lieu d'abandonner le réveil du jeu.

## Nouveautés v53
- **Lanceur maison : plus d'accès au lecteur branchés.** Le faux lecteur prend maintenant aussi les instructions `lhz` / `sth` (demi-registre) et `lwzu` / `stwu` : c'est ce qui bloquait 1080° Avalanche (« 2 accès au lecteur non branchés »).
- Si un jeu est quand même refusé, `SD:/gc-mp3/natif/dernier-lancement.txt` dit **lesquels et pourquoi** ; le rapport de l'analyse (**R**) marque aussi les accès « NON BRANCHABLE ».

## Nouveautés v52
- **Mes applis en grandes pochettes**, comme les jeux : 4 grandes jaquettes (format 3:4) par rangée, l'icône de l'appli en entier et en grand, sur un fond flouté fait à partir de l'icône. Une vraie pochette est aussi possible : mets `cover.png` (ou `pochette.png`, `boxart.png`, `.jpg`) dans le dossier de l'appli.
- **Lanceur natif, étape 3 (test) : le jeu lit l'ISO sur la carte SD.** Avec **X** dans « Lancer ce jeu ? », GC Station installe en haut de la mémoire un petit **programme résident** qui lit la carte SD tout seul (sans système de fichiers : la position de l'ISO sur la carte est calculée avant), et branche sur lui les accès du jeu au lecteur DVD (**faux lecteur**). Jusqu'à 160 accès au lecteur (1080° Avalanche en a plus de 48) ; si un seul ne peut pas être branché, le jeu n'est pas lancé. Les `.ciso` et la musique en streaming ne sont pas encore pris en charge.
- L'analyse (**R**) teste aussi la position du jeu sur la carte et le lecteur SD maison, sans lancer de jeu.
- Banc d'essai sur PC (émulateur PowerPC, fausse carte SD) : `sh tools/natif-test/run.sh`.

## Nouveautés v51
- **Rapport d'analyse sur le téléphone** : après une analyse (fiche d'un jeu, R), un **QR code** s'affiche ; scanne-le pour ouvrir le rapport sur ton téléphone (même Wi-Fi que la GameCube). Tous les rapports : `http://<IP de la GameCube>/natif` (lien « Analyses des jeux » en bas de la page de la télécommande).
- **Lanceur natif, étape 2 (test) : le démarrage maison.** Dans « Lancer ce jeu ? », **X** démarre le jeu sans Swiss : GC Station lit lui-même `main.dol`, `bi2.bin` et la table des fichiers, prépare la mémoire et saute dans le jeu. Le lecteur n'est pas encore redirigé vers la carte SD : si le jeu affiche son message d'erreur de disque, c'est réussi. Journal : `SD:/gc-mp3/natif/dernier-lancement.txt` (aussi sur le téléphone). **A** lance toujours le jeu comme avant.

## Nouveautés v50
- **Lanceur natif, étape 1 : l'analyse.** Dans la fiche d'un jeu, **R** décortique le disque comme le fera un futur lanceur 100 % GC Station (sans Swiss) : en-tête, région, apploader, `main.dol`, table des fichiers, mémoire basse à préparer, et les endroits du jeu qui pilotent le lecteur DVD. Rapport : `SD:/gc-mp3/natif/<ID>.txt`. Les jeux se lancent toujours avec le lanceur actuel.

## Nouveautés v49
- **Mes applis comme Mes jeux** : grandes tuiles carrées, l'icône `icon.png` entière (proportions gardées, quelle que soit sa taille), le nom dessous, cadre de sélection qui glisse.
- **Mini-jeux plus fluides** : pendant une partie, plus aucun travail lourd en fond (connexions https, pochettes, paroles, lecture des étiquettes, sauvegardes) ; le serpent glisse d'une case à l'autre au lieu de sauter.

## Nouveautés v48
- **Écran de chargement « Minimal »** pour les jeux : fond presque noir, nom du jeu, une ligne fine qui avance, le pourcentage (ou l'étape), « GC STATION » discret en bas à droite.

## Nouveautés v47
- **GC-MP3 devient GC Station** : nouveau nom partout (logo « GC STATION », sous-titre « INDIGO MEDIA CENTER », menus, messages, écran de chargement des jeux, télécommande sur téléphone).
- Rien ne change sur la carte SD : mêmes fichiers et dossiers, la mise à jour automatique continue.

## Nouveautés v46
- **Écran de chargement GC-MP3** pendant le lancement d'un jeu (nom du jeu, étape, barre d'avancement) : plus du tout l'interface de Swiss.
- Lancement plus rapide : le lanceur n'attend plus le réseau (sauf émulation du Broadband Adapter).
- **Cheats** : la fiche du jeu indique combien de cheats sont activés (X pour les cocher directement dans GC-MP3) ; ils sont appliqués automatiquement au lancement, sans passer par le menu de Swiss.

## Nouveautés v45
- **Lanceur de jeux corrigé** : il ne relance plus GC-MP3 (`ipl.dol`) au lieu du jeu. S'il ne peut pas lancer un jeu, GC-MP3 affiche pourquoi (journal : `SD:/gc-mp3/lanceur-dernier.log`).
- **Retour en jeu (A + Z + START)** : l'`apploader.img` contient maintenant le lanceur GC-MP3, fabriqué avec les outils officiels de Swiss. Il ramène sur GC-MP3 (chemin dans `SD:/gc-mp3/retour.txt`) au lieu du lecteur de disque.
- **JEUX / APPS plus fluide** : le cadre de sélection glisse d'un jeu à l'autre, carrousel continu, la fiche glisse en changeant de jeu, la vitrine et l'étagère défilent en douceur.
- **Animations désactivables** : Y > Options, ou START > Réglages > Animations (jeux / applis).
- **Options (Y)** : trier les jeux (A → Z, derniers lancés, taille, région), jeu au hasard, actualiser la liste. La fiche affiche le nombre de lancements et la date du dernier.
- **Moins de ralentissements** : écran Infos console (place libre de la SD calculée une seule fois), records des mini-jeux, télécommande du téléphone, sauvegarde de la reprise, téléchargements écrits par morceaux.

## Nouveautés v44
- **Lanceur de jeux GC-MP3** : les jeux (ISO / GCM) démarrent directement depuis GC-MP3, sans passer par le menu de Swiss / Indigo, avec un écran de lancement aux couleurs de GC-MP3.
  - Il est téléchargé tout seul (`SD:/gc-mp3/lanceur.dol`) et tenu à jour ; START > Réglages > **Lanceur de jeux** (A : mettre à jour, Gauche / Droite : GC-MP3 ou Swiss).
  - Maintenir **B** pendant le lancement : écran du jeu (réglages du jeu, cheats). En cas d'erreur, retour sur GC-MP3.
  - Les réglages par jeu et les cheats faits dans GC-MP3 s'appliquent (mêmes fichiers que Swiss / Indigo).
  - C'est le moteur de [Swiss](https://github.com/emukidid/swiss-gc) (licence GPL v2), modifié par `tools/mkloader.py` et compilé par GitHub Actions : merci à l'équipe de Swiss.

## Nouveautés v43
- **Style du spectre partout** : le style choisi s'affiche aussi dans le mini-spectre en haut de l'écran et dans les dispositions Pochette géante / Minimaliste.
- **Une disposition par onglet** : MUSIQUE, RADIO, RÉTRO et PODCASTS ont chacun la leur (START > Réglages > Disposition du lecteur, ou stick C vers le bas). Nouvelle disposition **Spectre géant**.
- **Ordre des onglets** : START > Réglages > Onglets affichés, X / Y pour descendre / monter un onglet.
- **Paroles dans le lecteur** : spectre seulement, paroles (sinon spectre) ou paroles + spectre.
- **Éclair sur les basses** (mode soirée) : partout, sauf spectrogramme, ou jamais.
- **Cartes mémoire** déplacées dans l'onglet JEUX / APPS (troisième bouton en haut).

## Nouveautés v42

- **Plus de coupure de la musique** pendant la vérification de mise à jour (ou tout autre moment où GC-MP3 est occupé) : si la boucle principale prend du retard, le son est rempli directement depuis ce qui est déjà décodé (jusqu'à 10 s d'avance).
- **Mes applis en grille d'icônes**, comme Mes jeux, avec l'appli choisie détaillée en bas.
- **Défilement infini** dans Mes jeux et Mes applis (du dernier au premier et inversement, carrousel en boucle) et **défilement doux** des lignes (grille, liste, applis).
- **Page Réglages et fenêtres plus fluides** : l'onglet caché derrière n'est plus redessiné à chaque image.

## Nouveautés v41

- **Onglet JEUX / APPS** (avant : JEUX) : à côté de « Mes jeux GameCube », une barre **« Mes applis »** ouvre la liste des homebrews (.dol). L'accès aux applis n'est plus dans START > Outils.
- **Icônes des applis** : le fichier `icon.png` à côté du .dol (dossier `apps/NomDeLAppli/`, format Homebrew Channel 128 x 48) s'affiche dans la liste. Sans icône : les initiales.
- Le nom d'une appli est celui de son dossier (ex. « VBA-GX » au lieu de « default »), ou celui de `meta.xml` s'il existe.

## Nouveautés v40

- **Couleurs du spectrogramme** (START > Réglages > GC-MP3) : Feu (par défaut, moins violet), Violet (l'ancien), Arc-en-ciel, Océan, Vert, Noir et blanc. Pour le spectrogramme plein écran (économiseur, mode soirée) et le style « Spectrogramme » du lecteur. Changer de couleurs recolore aussi ce qui est déjà affiché.

## Nouveautés v39

- **Spectre amélioré** : mouvement plus fluide (lissé à chaque image), crêtes qui retombent doucement, et **7 styles** : LED (classique), Barres, Miroir, Courbe, Spectrogramme (défilant, couleurs de chaleur), Oscilloscope (forme d'onde), Cercle. START > Réglages > GC-MP3 > « Style du spectre ».
- **A + Z + START en jeu ramène sur GC-MP3** : START > Réglages > GC-MP3 > « Retour en jeu (A+Z+START) » = GC-MP3. GC-MP3 télécharge son propre `apploader.img` (fabriqué à chaque compilation, même format que celui d'Indigo), le met dans `swiss/patches/` et règle Indigo sur « Apploader ». Celui d'Indigo est gardé (`apploader-indigo.img`) : remettre « Indigo » le rétablit. Le fichier est remis à jour tout seul après chaque mise à jour de GC-MP3.
- Merci à Swiss (Extrems, emu_kidid) : le petit programme de démarrage de `apploader.img` (`tools/igr_stub.bin`, GPL v2) vient du packer de Swiss.

## Nouveautés v38

- **Après une mise à jour : A relance GC-MP3 directement** (la nouvelle version est chargée en mémoire). Avant, il fallait quitter, ce qui redémarrait la console sur le menu de la GameCube / le lecteur de disque.
- **Bouton Reset de la console** pendant GC-MP3 : relance GC-MP3 au lieu d'ouvrir le menu de la GameCube.
- « Quitter GC-MP3 » ouvre toujours le menu de la GameCube (lecteur de disque).

## Nouveautés v37

- **Cheats beaucoup plus rapides** : cocher / décocher est instantané. Les choix sont écrits sur la carte SD une seconde après (ou en quittant le fichier), plus à chaque appui. La lecture des codes n'utilise plus `sscanf` (20 fois plus rapide, même résultat que Swiss). La liste des fichiers de cheats s'ouvre sans lire chaque fichier.
- **Page Réglages plus stable** : changer Vibrations ou Vidéo dans la section GC-MP3 n'écrit plus le fichier d'Indigo à chaque appui (écrit en fermant la page), et ne perd plus les changements faits dans les sections Indigo / Swiss.

## Nouveautés v36

- **Mise à jour bloquée à 99 %** corrigée : la recherche des autres copies de GC-MP3 regardait chaque fichier de la carte (très long avec des milliers de ROM). Elle ne regarde plus que la racine et le dossier `apps`, par leur nom.

## Nouveautés v35

- **Nouvelle page Réglages (START)** avec des sections en haut, **L / R** pour changer :
  - **GC-MP3** : les réglages de l'appli.
  - **Indigo** : système de la console et interface d'Indigo.
  - **Swiss** : lancement des jeux, réglages par défaut des jeux, réseau.
  - **Outils** : les outils ci-dessous.
  Les réglages d'Indigo / Swiss changés sont enregistrés en fermant la page (ou Y tout de suite).
- **Cartes mémoire** : sauvegardes des slots A et B (icône, nom, blocs), copie sur la carte SD en `.gci` (dossier `SD:/gci`), remise depuis un `.gci` / `.gcs` / `.sav`, copie vers l'autre carte, suppression.
- **Applis (.dol)** : tous les `.dol` de la racine et du dossier `apps` (nom et description de `meta.xml`), lancés directement depuis GC-MP3.
- **Derniers jeux lancés** : depuis GC-MP3 et depuis Indigo (`recent.ini`), relancés en un clic.
- **Réglages par jeu** : dans Mes jeux, fiche d'un jeu > **Y** (fichier `swiss/settings/game/<ID>.ini` d'Indigo). Y dans la fenêtre = tout remettre par défaut.
- **Infos console** : version, vidéo, langue, son, mémoire, carte SD, réseau, horloge.

## Nouveautés v34

- **Réglages d'Indigo appliqués à GC-MP3** (point vert dans START > Réglages Indigo / Swiss) :
  - Son de la console Mono / Stereo : GC-MP3 joue en mono si Mono.
  - Position de l'écran : décale aussi l'image de GC-MP3.
  - Langue de la console : GC-MP3 la suit (si sa langue est sur Auto).
  - Couper les vibrations ↔ Vibrations de la manette de GC-MP3 (synchronisés dans les deux sens).
  - Mode vidéo des applis (Auto / 480i / 480p) ↔ Vidéo de GC-MP3 (synchronisés, au prochain lancement).
  - Fichiers cachés / Cacher les fichiers inconnus : onglet FICHIERS.
  - Réseau : DHCP = No + adresse IP / masque / passerelle → GC-MP3 utilise cette adresse fixe.
- Son, position et langue sont écrits dans la mémoire de la console, comme le fait Indigo à son démarrage.

## Nouveautés v33

- **Mise à jour de toutes les copies** : la mise à jour remplace aussi les autres copies de GC-MP3 sur la carte (ex. `ipl.dol` à la racine ET `gc-mp3player.dol` dans `apps`). GC-MP3 les reconnaît à leur contenu, même renommées.

## Nouveautés v32

- **Réglages Indigo / Swiss dans GC-MP3** : START > « Réglages Indigo / Swiss ». Une fenêtre par-dessus l'écran en cours, avec tous les réglages d'Indigo / Swiss rangés en 5 pages (L / R) : Système, Interface, Jeux, Jeux (défaut : vidéo, manette, émulation), Réseau.
- Gauche / Droite ou A change la valeur, A ouvre le clavier pour les adresses et les noms, X montre un mot de passe, Y enregistre, B ferme.
- GC-MP3 modifie seulement les lignes changées de `SD:/swiss/settings/global.ini` (le reste, comme `Autoload=`, ne bouge pas). « (par défaut) » = pas écrit dans le fichier, Indigo garde sa valeur par défaut.

## Nouveautés v31

- **Activer / désactiver les cheats** (éditeur de cheats Swiss) : dans un fichier, **A** coche ou décoche un cheat (case verte = activé). GC-MP3 écrit `SD:/swiss/cheats/<ID>.chtsel`, le même fichier que Swiss / Indigo : tes choix se retrouvent aussi dans leur menu Cheats.
- Le fichier de cheats doit s'appeler comme l'ID du jeu (ex. `GALE01.txt`). Les cheats avec des « XX » ne sont pas pris en charge par Swiss (case barrée).
- X > « Voir / modifier les codes » pour éditer les lignes d'un cheat. Les choix restent cochés après une modification enregistrée.
- Un jeu lancé depuis l'onglet JEUX démarre avec ses cheats cochés (GC-MP3 envoie `AutoCheats=Yes` à Indigo). Pour un jeu lancé directement depuis Indigo, active « Auto-load cheats » dans ses réglages.

## Nouveautés v30

- **Démarrer GC-MP3 avec Indigo / Swiss** : START > Réglages > « Démarrer avec Indigo / Swiss » = Oui. GC-MP3 écrit `Autoload=` dans `SD:/swiss/settings/global.ini` : à chaque démarrage, Indigo ouvre GC-MP3 tout seul. Non = Indigo redevient normal.
- **Quitter vers Swiss** (START > Réglages) : retour au menu d'Indigo / Swiss, même quand GC-MP3 démarre tout seul.
- **Z tenu au démarrage de GC-MP3** : passe directement au menu d'Indigo / Swiss (secours).

## Nouveautés v29 (retours de testeurs)

- **Plus de coupures** : dans un dossier, pendant la lecture, les noms (« Artiste - Titre ») sont lus un par un et seulement quand il reste au moins 2 s de son d'avance.
- **Analyseur de spectre** plus fin : vraie FFT sur 2048 points (avant : 512), échelle « mel » de 40 Hz à 16 kHz (les basses prennent peu de place, les médiums et aigus sont étalés), niveau automatique (plus de spectrogramme tout blanc sur une musique forte).
- **Grosse caisse** détectée pour de vrai (montée des basses 40-140 Hz) : le mode soirée, les vibrations, le jeu de rythme et l'aquarium suivent les coups de batterie au lieu de se déclencher au hasard.

## Nouveautés v28

- **Deux interfaces au choix** : START > Réglages > « Style de l'interface » : **Indigo** (d'origine) ou **Moderne** (gris sombre, cartes plates, tous les onglets en haut, une couleur d'accent). En Moderne, « Thème » choisit la couleur d'accent (cyan, bleu, vert, rouge, orange, rose, blanc).
- **Mes jeux GameCube : 6 affichages** (R / L pour changer, ou START > Réglages > « Affichage des jeux GameCube ») : Grille, Carrousel, Liste, Mur de jaquettes, Vitrine (capture + résumé + bande de jaquettes), Étagère.

## Nouveautés v27

- Fin de téléchargement (web, podcasts, téléphone) : plus de blocage de quelques secondes à 99 % (le morceau est ajouté à la liste sans relire toute la carte SD ; il apparaît en bas de « Tous les morceaux » jusqu'au prochain démarrage).
- Mise à jour : le nouveau .dol est écrit petit à petit (« Installation NN % »), l'écran ne se fige plus.
- Mes jeux GameCube : la liste est gardée (`SD:/gc-mp3/cache/jeux/liste.txt`) et s'ouvre tout de suite ; Y pour chercher à nouveau.
- Lancement d'un jeu : Swiss trouvé une seule fois puis retenu, chargement affiché en %.

## Nouveautés v26

- **Lancer un jeu** : onglet JEUX > Mes jeux GameCube > A (fiche) > A : GC-MP3 se ferme et Swiss démarre directement le jeu (argument `Autoload`). GC-MP3 trouve Swiss tout seul (un .dol avec « swiss » ou « indigo » dans le nom, ou `ipl.dol` / `boot.dol`, à la racine ou dans `swiss/`) ; pour en forcer un : `swiss = sd:/chemin/swiss.dol` dans `gc-mp3.txt`.
- **Rétro** : libxmp complète, plus de 50 formats de modules (MOD et variantes, XM, IT, S3M, MED/OctaMED, MTM, 669, OKT, DBM, DIGI, DSYM, FAR, ULT, PTM, STM, MDL, IMF, GDM, AMF, LIQ, RTM, SYM, UMX...). Noms à l'Amiga acceptés (`mod.titre`, `med.titre`).
- **Rétro** : les **.zip** posés dans `SD:/retro` sont décompressés tout seuls dans un dossier du même nom (une seule fois).
- **Fichiers** : les noms avec accents, tirets longs, etc. s'affichent correctement (lecture des noms en UTF-8).

## Mes jeux GameCube (v25)

- Onglet JEUX > « Mes jeux GameCube » (la barre du haut) : tes jeux de la carte SD (.iso, .gcm, .ciso, .nkit.iso, dans n'importe quel dossier, 3 niveaux).
- Chaque jeu montre sa **jaquette** si tu as le pack de Swiss Indigo : `SD:/swiss/ui/posters.pak` (et `stills.pak` pour une capture, `descriptions.txt` pour le résumé). Sans jaquette : la bannière du disque et l'ID du jeu.
- A = fiche du jeu (grande jaquette, région, taille, résumé, capture), X = ses cheats (crée `SD:/swiss/cheats/<ID>.txt` si besoin), Y = actualiser la liste.
- Les jeux se lancent toujours depuis Swiss. Les bannières lues sont gardées dans `SD:/gc-mp3/cache/jeux` (plus rapide la fois suivante).

## Éditeur de cheats Swiss (v25)

- Ouvre-le avec START > « Cheats Swiss (éditeur) », ou onglet FICHIERS > X > « Cheats Swiss », ou en ouvrant un .txt de `SD:/swiss/cheats`.
- Swiss lit un fichier par jeu : `SD:/swiss/cheats/<ID du jeu>.txt` (ex. `GALE01.txt`). Dedans : le nom du cheat, ses lignes de code `XXXXXXXX YYYYYYYY`, puis une ligne vide avant le cheat suivant.
- « + Nouveau fichier » : tape l'ID du jeu (6 caractères, affiché par Swiss).
- Dans un fichier : A ouvre un cheat, X = renommer / dupliquer / supprimer / monter / descendre, « + Ajouter un cheat », Y = enregistrer.
- Dans un cheat : A sur une ligne = modifier chiffre par chiffre (gauche / droite : chiffre, haut / bas : valeur), X = ajouter / taper au clavier / supprimer / dupliquer une ligne.
- Les lignes qui ne sont pas au bon format s'affichent en rouge. Le reste du fichier (en-tête, lignes inconnues) est gardé tel quel.

## Langue / Language (v24)

- START > Réglages > « Langue / Language » : **Auto** (langue de la console), **Français** ou **English**.
- Tout passe en anglais : onglets, menus, messages, jeux, page web d'accueil et page du téléphone.
- Les fichiers de réglages sur la carte SD restent les mêmes (on peut changer de langue sans rien perdre).
- In English: START > Settings > "Langue / Language" > English.

## Bibliothèque plus grande et réseau plus solide (v24)

- Jusqu'à **2000 morceaux** dans l'onglet MUSIQUE (avant : 512).
- Connexion : un 2e essai automatique si le premier échoue (paquet perdu, adresse périmée), et la connexion sécurisée (https) réessaie une fois avant d'afficher une erreur.

## Curseur dans tous les onglets
Comme dans l'onglet WEB, le **stick principal** déplace une flèche partout : listes, boutons du
lecteur (précédent, lecture / pause, suivant, stop), menu START, fenêtres, grille des photos, cartes
des jeux, et le bandeau des onglets (après Z, ou en cliquant sur le nom de l'onglet en haut de la
liste). La zone survolée est entourée en jaune ; **A** la choisit. La flèche disparaît après 6 s.

## Onglet JEUX
Tous les mini-jeux en un coup d'œil, avec leur record : Karaoké, Jeu de rythme (solo ou duel), Quiz
musical, Snake, **Snake sans murs**, Casse-briques, **Pong** (contre la GameCube, ou à deux avec la
manette 2), **Note volante** (A pour voler entre des murs qui arrivent en rythme) et **Simon**
(retiens la suite de boutons, de plus en plus longue et rapide). Croix ou curseur pour choisir, A
pour jouer. Dans le casse-briques, une brique dorée donne maintenant une **grande raquette** 8 s.

## Onglet TÉLÉCHARGEMENTS
Tout ce qui arrive sur la carte SD : téléchargements de l'onglet WEB, épisodes de podcasts (à la
main ou automatiques) et fichiers envoyés depuis le téléphone. En haut, ce qui est en cours (avec
la vitesse) et les épisodes en attente ; en dessous, l'historique (plus récent en haut, avec
l'origine, la taille et l'heure). A = écouter (musique) ou voir (photo), X deux fois = effacer le
fichier, B = annuler le téléchargement en cours, Y = aller dans l'onglet WEB. Historique :
`SD:/gc-mp3/telechargements.txt`.

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
- Formats : **MP3, FLAC, OGG Vorbis, WAV et M4A** (`.mp3`, `.flac`, `.ogg`, `.wav`, `.m4a`, `.m4b` en AAC-LC
  ou HE-AAC, avec titre, artiste et pochette intégrée), lus dans `SD:/mp3`
  et `SD:/downloads`, sous-dossiers compris (3 niveaux, 2000 fichiers maximum). Les FLAC haute
  résolution (24 bits, 96 kHz) passent ; les fichiers Opus et ALAC ne sont pas gérés. Ils sont lus au fur et à mesure depuis la carte : **pas de limite de taille**
  (les longs mix passent).
- **Stick C gauche / droite** : change la liste — *Tous les morceaux*, *Dossiers*, *Artistes*, *Albums*,
  *Mes listes*.
- **X : menu d'actions** sur le morceau choisi : *Ajouter à une liste…*, *Nouvelle liste…*, *Chercher un
  morceau…*, *Actualiser la bibliothèque*. Dans une liste : *Retirer*, *Monter*, *Descendre* ; sur une liste :
  *Supprimer*.
- **Mes listes** : tes listes de lecture (`SD:/gc-mp3/listes/<Nom>.m3u`, aussi lisibles sur PC), plus deux listes
  automatiques : **Les plus écoutés** et **Écoutés récemment** (un morceau compte après 30 s d'écoute ;
  `SD:/gc-mp3/gc-mp3-ecoutes.txt`).
- **Recherche** : X > *Chercher un morceau…* (ou tape une lettre sur le clavier GameCube) ; titre, artiste ou
  album, sans se soucier des accents ni des majuscules. B efface la recherche.
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
- **Noms propres** : la liste affiche « Artiste - Titre » lu dans les étiquettes du fichier, sinon le
  nom du fichier sans numéro de piste (« 01 - », « 1-03 », « 07_ »…), sans extension ni « _ ».
  Dans une vue Artiste ou Album, seulement le titre.
- Pochettes : image du même nom que le MP3, pochette intégrée, ou `cover.jpg` / `folder.jpg` du dossier.
  Sinon, avec le Broadband Adapter branché, elle est cherchée sur MusicBrainz / Cover Art Archive
  et enregistrée à côté du MP3. L'album et l'année s'affichent sous l'artiste.

## Mode soirée
**Stick C vers le haut** (onglets MUSIQUE et RADIO), ou START > Mode soirée : visualiseur plein écran.
Les basses donnent le rythme (pulsations, éclairs, anneaux, particules), les couleurs tournent avec
la musique, et les **paroles s'affichent en grand** (sinon le titre et l'artiste).
Quatre styles : *Pulsation*, *Barres*, *Tunnel*, *Spectrogramme* (stick C gauche / droite pour changer).
L / R et Y marchent comme d'habitude ; B ou START pour sortir.
**La manette vibre** sur les gros coups de basse (START > Vibrations de la manette pour l'arrêter).
En écoutant une radio, X garde le titre qui passe (voir l'onglet RADIO, *Titres de la radio*).

## Jeux et karaoké
Dans MUSIQUE, **X sur un morceau** (ou START > Jeux et karaoké) :
- **Karaoké** : les paroles en très grand sur toute la télé. La ligne chantée se colore de gauche à
  droite, la précédente et les suivantes sont affichées autour. L / R = morceau, Y = pause, B = sortir.
  Les paroles viennent d'un fichier `.lrc` à côté du morceau ou de LRCLIB (paroles automatiques).
- **Jeu de rythme** : comme Guitar Hero, sur tes propres morceaux. GC-MP3 écoute les basses, trouve
  le tempo, et fait tomber des notes A / B / X / Y sur les temps. Appuie quand la note touche la ligne :
  *Parfait*, *Bien*, *OK*… Le combo multiplie les points (jusqu'à x4). Trois difficultés (Facile, Normal,
  Difficile). Z = pause, START = arrêter. Le record de chaque morceau est gardé dans
  `SD:/gc-mp3/gc-mp3-jeux.txt`. Les morceaux au rythme bien marqué marchent le mieux.
  **Duel** (haut / bas sur l'écran de départ) : deux joueurs côte à côte, manettes 1 et 2, mêmes
  notes pour les deux ; à la fin, le gagnant s'affiche (A = revanche).
- **Quiz musical** : 10 secondes d'un de tes morceaux (pris au milieu), 4 réponses : trouve lequel !
  De 1 à 4 joueurs, chacun avec sa manette (A, B, X ou Y). Plus on répond vite, plus on gagne de points ;
  le premier qui trouve a un bonus. 10 manches, puis le classement. Il faut au moins 4 morceaux.
- **Snake musical** : mange les notes ; le serpent file plus vite quand la musique s'emballe (refrain,
  montée, drop) et des **notes dorées** (50 points) apparaissent sur les coups de basse. Croix ou stick
  pour diriger, Y = pause, START = sortir.
- **Casse-briques musical** : chaque colonne de briques s'allume avec une bande du spectre, la balle
  accélère avec la musique (« TURBO ! »), et des briques deviennent dorées sur le rythme (50 points).
  Stick ou croix = raquette, A = lancer la balle, 3 vies, un niveau plus rapide à chaque mur cassé.
  Si rien ne joue, un morceau au hasard est lancé. Les records sont gardés (`SD:/gc-mp3/gc-mp3-jeux.txt`).

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
- Abonnements dans `SD:/gc-mp3/podcasts.txt` (`Nom | adresse du flux RSS`) ; quelques podcasts de
  Radio France sont proposés au départ.
- **Rechercher un podcast...** (première ligne) : recherche dans l'annuaire d'Apple Podcasts,
  A sur un résultat = s'abonner.
- Dans un podcast : A = écouter un épisode, **X = le télécharger** dans `SD:/gc-mp3/podcasts` (il apparaît
  ensuite dans MUSIQUE), B = retour. **L / R = reculer de 15 s / avancer de 30 s**.
- La position de chaque épisode est retenue (`SD:/gc-mp3/podcasts/positions.txt`) : on reprend où on s'était arrêté.
- Épisodes en **MP3 ou M4A** (Radio France publie souvent en M4A), adresses longues acceptées
  (2048 caractères). En M4A, les sauts L / R et la reprise sont exacts.
- La recherche utilise le catalogue du pays de la **langue du contenu** (menu START).
- **Nouveaux épisodes automatiques** : GC-MP3 regarde tes abonnements un peu après le démarrage,
  puis toutes les 3 heures. Quand un podcast a un nouvel épisode, il est téléchargé tout seul dans
  `SD:/gc-mp3/podcasts` (un message le dit en bas de l'écran). La première fois, il note seulement le
  dernier épisode (rien n'est téléchargé). START > Nouveaux podcasts pour l'arrêter.

## Onglet PHOTOS
- Les images `.jpg` / `.png` de `SD:/photos`, `SD:/ecran` et `SD:/downloads` en vignettes.
  A = plein écran (croix = photo précédente / suivante, A = diaporama), Y = diaporama direct.
  La musique continue pendant ce temps.
- **Fond d'écran** : en plein écran, **X** met la photo en fond de GC-MP3 à la place du fond violet
  (assombrie pour que le texte reste lisible ; les panneaux deviennent un peu transparents).
  START > Fond d'écran pour revenir au violet.
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
  ce qu'il joue (ta musique ou une de tes radios). **Réveil en douceur** : le son part de presque
  rien et monte pendant 3 minutes, et l'écran affiche en grand l'heure, la date et **la météo du
  jour** (chargée 5 minutes avant). A = garder la musique, B = arrêter le réveil.
  La GameCube doit rester allumée sur GC-MP3, et l'heure vient de son horloge.

## Onglet TÉLÉPHONE
Avec le Broadband Adapter : l'écran montre un **QR code**. Scanne-le avec le téléphone (même Wi-Fi /
même box) pour ouvrir la page de GC-MP3 dans le navigateur du téléphone :
- **télécommande** : lecture / pause, précédent / suivant, volume, stop, mode soirée, disposition,
  pochette et titre en cours ;
- choisir un **morceau** ou une **radio** dans la liste ;
- **ouvrir une page web sur la télé** (carte « Ouvrir sur la télé ») ;
- **tous les réglages** (onglet « Réglages » de la page) : ceux du menu START avec ◀ ▶, les éléments du
  lecteur et les onglets affichés (cases à cocher), et la ville, le flux d'actualités, le réveil, etc. ;
- **envoyer des fichiers sur la carte SD** : musique, rétro, photos, vidéos, podcasts… (barre de
  progression sur le téléphone et sur la télé). Les nouveaux morceaux apparaissent tout de suite.
Le petit serveur démarre tout seul quand le réseau est prêt (réglage `telephone`). Il n'y a pas de
mot de passe : n'importe qui sur ton réseau local peut s'en servir, et il ne peut écrire que dans
les dossiers prévus.

## Onglet RADIO
- Mes radios : `SD:/gc-mp3/radio.txt`, une station par ligne : `Nom de la radio | http://adresse/du/flux`.
- **Rechercher une radio...** (première ligne) : annuaire libre radio-browser.info, recherche par nom.
  **Les plus écoutées en France** : la liste des stations populaires.
  Dans les résultats : A = écouter, **X = ajouter à mes radios** (écrit dans `radio.txt`).
- Flux **MP3**, **AAC / HE-AAC** (aacPlus), **OGG Vorbis** et fichiers **M4A**, en http:// ou https://. Les listes
  `.m3u` / `.pls` et les redirections sont suivies. Radios **HLS** (`.m3u8`, segments MPEG-TS ou
  AAC / MP3) gérées aussi. Pas d'Opus, ni de HLS chiffré ou au format fMP4.
- **Enregistrer la radio** : START > Enregistrer la radio. Le flux est enregistré tel quel
  dans `SD:/gc-mp3/radio/enregistrements/` (« REC » s'affiche en haut de l'écran).
- Préférer des serveurs proches (France / Europe) : avec les serveurs lointains, le réseau
  de la GameCube peut être trop lent pour du 128 kbps.
- Logo facultatif : `SD:/gc-mp3/radio/<Nom>.jpg` ou `.png`.
- **Titres de la radio** : quand une chanson te plaît, **X** (dans l'onglet RADIO ou en mode soirée)
  garde son titre dans `SD:/gc-mp3/gc-mp3-titres.txt`, avec la radio, la date et l'heure. START > Titres
  radio gardés pour les revoir (A = le chercher dans ta musique). Sur le téléphone (onglet Radios
  de la page du QR code), toucher un titre le cherche sur YouTube.
- Les certificats https sont vérifiés (liste Mozilla), sauf les dates d'expiration (horloge
  de la GameCube souvent fausse). L'aléa du chiffrement vient de l'horloge du processeur
  (pas de générateur matériel) : suffisant pour la radio, pas pour des mots de passe.

## Onglet WEB
- Navigateur simple : texte, titres, listes, liens, formulaires et images (JPEG, PNG, GIF, **WebP**).
  Pas de JavaScript ni de vidéo : les sites simples marchent (Wikipédia, archive.org…),
  pas YouTube, Spotify, SoundCloud ni Deezer. SVG ne s'affiche pas.
- **Le texte s'affiche tout de suite** pendant que la page se charge, puis se complète.
- **Flèche au stick principal** : pointe un lien ou un champ, A pour l'ouvrir ; elle disparaît
  toute seule (ou à la croix). Le stick C fait défiler la page.
- **Formulaires complets** : champs texte et mot de passe (masqué), cases à cocher, boutons radio,
  listes déroulantes, envoi GET et POST. Avec plusieurs champs, remplis-les puis choisis le bouton.
- **Cookies** : les sites peuvent garder ta connexion et tes préférences. Ils sont enregistrés dans
  `SD:/gc-mp3/cookies.txt` (les cookies permanents seulement). Ce fichier contient tes connexions :
  ne le partage pas. Accueil > « Effacer les cookies » pour tout supprimer.
- Les **images déjà vues** restent en mémoire (3 Mo) : revenir sur une page est instantané.
- Un lien vers un **MP3, un M4A ou une radio** (mp3, m4a, ogg, aac, m3u, pls…) propose : A = écouter tout de
  suite (sans télécharger), X = télécharger, B = annuler.
- **Depuis le téléphone** : carte « Ouvrir sur la télé » de la page TÉLÉPHONE, colle une adresse,
  la page s'ouvre sur la GameCube.
- Accueil (X) : recherche web, **musique libre sur archive.org** (liste directe des MP3 de chaque album), favoris
  (`SD:/gc-mp3/favoris.txt`, une ligne par site : `Nom | adresse`).
- Un lien vers un autre fichier est téléchargé dans `SD:/downloads` en tâche de fond ; les MP3 apparaissent
  ensuite dans l'onglet MUSIQUE.

## Dolphin (émulateur, PC et Android)
GC-MP3 se lance aussi dans Dolphin (ouvre le fichier `.dol`). Dolphin n'émule pas d'adaptateur SD
pour la GameCube : GC-MP3 utilise alors une **carte SD virtuelle en mémoire** (6 Mo).
- Les **réglages** (radios, favoris, podcasts, positions d'écoute, gc-mp3.txt…) sont gardés sur la
  **carte mémoire** de l'emplacement A (ou B), dans le fichier « GC-MP3 donnees ».
- Pour Internet (radio, podcasts, web, météo) : Dolphin > Configuration > GameCube > **SP1 =
  Broadband Adapter (HLE)** (« Built In » sur Android).
- La musique téléchargée reste jusqu'à la fermeture de GC-MP3. La page du téléphone ne peut pas
  joindre Dolphin (le réseau émulé n'accepte pas les connexions entrantes), et la mise à jour en un
  clic est désactivée (télécharge le `.dol` à la main).

## Fluidité
- La barre de sélection glisse d'une ligne à l'autre ; la croix maintenue va de plus en plus vite.
- **Bibliothèque en cache** (`SD:/gc-mp3/gc-mp3-bib.txt`) : les durées et les étiquettes déjà lues sont
  gardées ; au démarrage suivant, la liste défile sans relire chaque fichier et les vues Artistes /
  Albums sont prêtes tout de suite. Le fichier peut être supprimé sans risque (il se refait).
- Les durées ne sont pas lues pendant qu'on fait défiler la liste (pas d'à-coups).
- **Changement de morceau instantané** : la pochette est décodée en tâche de fond (le son démarre
  tout de suite), puis gardée toute prête dans `SD:/gc-mp3/cache/pochettes` : la fois suivante, elle
  s'affiche immédiatement. Pareil pour les logos des radios et les pochettes trouvées sur Internet.
- **Onglet PHOTOS rapide** : pour les photos d'appareil photo ou de téléphone, GC-MP3 prend la petite
  image déjà rangée dans la photo (EXIF) au lieu de décoder toute la photo — et les très grandes
  photos ont enfin leur vignette. Les vignettes sont gardées dans `SD:/gc-mp3/cache/vignettes`.
- **Connexions https plus rapides** : la session sécurisée des derniers sites est réutilisée
  (paroles, pochettes, météo, podcasts, radios en https) : plus besoin de refaire les longs calculs
  de chiffrement à chaque fois. Les adresses des sites sont aussi gardées 30 minutes.
- **Son sans coupure** : environ 0,2 s de son est préparé d'avance ; quand GC-MP3 est occupé un
  instant (carte SD, réseau, image à décoder), la musique ne saute plus (MP3 à 320 kb/s compris).
- Une seule attente du processeur graphique par image, et pas de calcul du spectre quand rien ne joue.
- Le dossier `SD:/gc-mp3/cache` peut être supprimé sans risque : il se refait tout seul.

## Menu (START)
Lecture aléatoire, répétition, **fondu enchaîné**, liste MUSIQUE, **volume**, **volume égalisé**,
**égaliseur** (réglages tout prêts : Rap, Rock, Pop, Basses, Voix, Électro, Classique, Jazz, ou graves /
médiums / aigus à la main), **thème de couleurs** (Indigo, Océan,
Émeraude, Rubis, Ambre, Couleurs douces, Graphite — « Couleurs douces » si l'image paraît trop vive
sur ta télé), **économiseur d'écran** (2 / 5 / 10 / 30 min ou jamais, n'importe quelle touche le quitte), pochettes et paroles automatiques, reprise au démarrage,
**type de télé** (*Écran plat*, ou *Cathodique* : 480i et 4:3, pour une télé à tube ou un moniteur PVM),
**vidéo** (auto = 480p avec un câble composante ou un GCVideo ; B maintenu au démarrage = 480i ;
le 480i est maintenant bien gardé même si la console était réglée en progressif ; un changement de
mode vidéo s'applique au prochain démarrage),
**taille de l'écran** (80 à 112 % : en dessous de 100, l'image rétrécit si les bords sont coupés ;
au-dessus, elle s'élargit pour supprimer les bandes noires des côtés — 110 % conseillé en 16:9),
**hauteur de l'écran** (80 à 100 %) et **position verticale** (plus haut / plus bas) si le haut ou le
bas de l'image est coupé par la télé,
**format de l'écran** (4:3 ou 16:9 : en 16:9, règle aussi la télé en mode large / plein écran ;
l'interface s'étale sur toute la largeur, le texte, les pochettes, les photos et le QR code
gardent leurs vraies proportions, et les pages web sont mises en page plus larges),
**langue du contenu** (Auto = langue de la console réglée dans l'IPL, ou français, anglais, allemand,
espagnol, italien, néerlandais : pays des podcasts et des « radios les plus écoutées », langue demandée
aux sites web et noms de villes de la météo ; l'interface reste en français),
**fond d'écran** (violet ou ta photo), **décor de saison** (chauves-souris et citrouilles à Halloween,
guirlande et neige à Noël, soleil en été, selon la date), **nouveaux podcasts** (téléchargés tout seuls),
**jeux et karaoké**, **titres radio gardés**, **mode soirée**, **vibrations de la manette**, **minuterie** (arrêt après 15 à 120 min, le son baisse doucement les 30 dernières
secondes), **disposition du lecteur**, **éléments du lecteur**, **onglets affichés**,
enregistrement de la radio, et **Quitter**. Tout est enregistré dans `SD:/gc-mp3/gc-mp3.txt`.

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
- **Spectrogramme** : les fréquences défilent de droite à gauche (graves en bas, aigus en haut),
  la couleur montre leur force. Aussi dans le mode soirée (4e style).
- **Aquarium** : des poissons qui nagent plus vite quand la musique est forte, des algues qui dansent
  sur les basses et des bulles qui montent sur chaque coup de basse.
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

## Mode veilleuse
START > « Mode veilleuse » (A pour lancer, ou F2 sur le clavier GameCube) : une grande horloge
orange très peu lumineuse sur fond noir, avec la date et ce qui joue. Si rien ne joue, la dernière
radio est lancée (réglage `veilleuse_radio`). Haut / bas : luminosité (5 niveaux, retenue),
X : minuterie (15 à 120 min), Y : pause, L / R : morceau ou radio précédent / suivant,
B ou START : sortir. L'économiseur d'écran ne se déclenche pas pendant la veilleuse.

## Clavier GameCube (ASCII, celui de Phantasy Star Online)
Branché sur n'importe quel port manette, il est reconnu tout seul.
- Dès que le clavier à l'écran est ouvert (adresse web, recherche, ville, podcasts…), tape
  directement : Entrée = valider, Échap = annuler, Retour arrière = effacer.
- Dans l'onglet WEB, taper une lettre ouvre la barre d'adresse.
- Ailleurs : flèches = croix, Entrée = A, Échap = B, Espace = pause, Tab = onglet suivant,
  F1 = menu, F2 = veilleuse, Page préc. / suiv. = morceau précédent / suivant.
Disposition japonaise : les symboles suivent les inscriptions des touches.

## Mise à jour en un clic
À chaque compilation réussie, le workflow publie le `.dol` dans la release « dernier » de ton
dépôt GitHub. Dans GC-MP3 : START > « Mise à jour » affiche ton numéro de version ; A vérifie, puis
A installe la nouvelle version. Le fichier est vérifié, l'ancien est gardé à côté
(`gc-mp3player.dol.ancien`), puis tu quittes et relances GC-MP3. Au démarrage, GC-MP3 vérifie tout
seul et affiche un bandeau vert si une version plus récente existe (réglage `maj_auto`).
Le fichier remplacé est celui qui a été lancé, sinon le premier `gc-mp3player.dol` trouvé sur la
carte ; tu peux l'imposer avec `maj_fichier = sd:/chemin/fichier.dol` dans `gc-mp3.txt`.

## Journal de plantage
Si GC-MP3 plante (écran rouge) ou se bloque plus de 20 secondes, il écrit un rapport dans
`SD:/gc-mp3/gc-mp3-crash.txt` : le type d'erreur, la fonction en cause, la pile d'appels, l'onglet, le
morceau ou la radio en cours, la mémoire libre et les 16 dernières actions. Au démarrage suivant, un
message le signale et le fichier devient `SD:/gc-mp3/gc-mp3-crash-ancien.txt`. Depuis le téléphone, le lien
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
stb_vorbis, game-music-emu 0.6.5, libxmp 4.7.4 (partie « lite »), qrcodegen, le décodeur WebP de libwebp 1.6.0 (versions figées,
empreintes vérifiées ; `tools/fetch_libs.py` extrait les fichiers utiles) et la liste de
certificats de curl.se, puis compile le `.dol`.

## Crédits
minimp3 (lieff, CC0), stb_image et stb_vorbis (Sean Barrett, domaine public), dr_flac (David Reid,
domaine public), game-music-emu (Shay Green et autres, LGPL 2.1), libxmp (Claudio Matsuoka et autres,
licence MIT), qrcodegen (Nayuki, MIT), libwebp (Google, licence BSD), mbedTLS (Apache 2.0),
décodeur AAC Helix (RealNetworks, licence RPSL, via ESP8266Audio), MusicBrainz, Cover Art Archive
et radio-browser.info (données libres), open-meteo.com (météo, CC BY 4.0), LRCLIB, certificats racines Mozilla (MPL 2.0, via curl.se),
police DejaVu Sans, libogc / devkitPro.

# GC Station — Indigo Media Center

*(anciennement GC-MP3 : les fichiers gardent leur nom, `gc-mp3player.dol` et `SD:/gc-mp3`, pour que les mises à jour continuent de marcher)*

Lecteur audio pour GameCube : **ta musique** (carte SD), des **webradios**, un **navigateur web**,
la **musique de jeux rétro**, des **podcasts**, tes **photos et vidéos**, un **explorateur de fichiers**,
la **météo / actualités / réveil**, et une **télécommande sur téléphone** (Broadband Adapter),
dans une interface GX style Indigo. Lancer `gc-mp3player.dol` depuis Swiss / Indigo.

## Où sont les fichiers sur la carte SD
- Tes fichiers : `SD:/mp3` (musique), `SD:/retro`, `SD:/photos`, `SD:/videos`, `SD:/ecran`, `SD:/downloads`.
- Tout ce que GC-MP3 crée est rangé dans **`SD:/gc-mp3`** : réglages (`gc-mp3.txt`), radios
  (`radio.txt`, logos et enregistrements dans `radio/`), podcasts (`podcasts.txt`, épisodes dans
  `podcasts/`), favoris et cookies du web, listes de lecture (`listes/`), écoutes, records des jeux,
  titres radio gardés, cache des pochettes et des vignettes, journal de plantage.
- Si ces fichiers étaient encore à la racine (version 21 et avant), GC-MP3 les y range tout seul au
  démarrage. Les autres fichiers de la racine (Swiss, `ipl.dol`, `config.ini`…) ne sont pas touchés.

## Onglets
**Z** passe à l'onglet suivant : MUSIQUE, RADIO, WEB, RÉTRO, PODCASTS, PHOTOS, FICHIERS, INFOS,
TÉLÉPHONE, JEUX, TÉLÉCHARGEMENTS. Le nom de l'onglet et des petits points (position) s'affichent en
haut de la liste. Pour cacher les onglets dont tu ne te sers pas : START > « Onglets affichés ».

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

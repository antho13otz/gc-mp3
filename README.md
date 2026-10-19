GC-MP3 — Indigo Audio Player
Lecteur audio pour GameCube : ta musique (carte SD), des webradios et un navigateur web
pour télécharger de la musique (Broadband Adapter), dans une interface GX style Indigo. Lancer gc-mp3player.dol depuis Swiss / Indigo.
Onglet MUSIQUE
Les MP3 sont lus dans SD:/mp3 et dans SD:/downloads (où l'onglet WEB enregistre
ses téléchargements), sous-dossiers compris (3 niveaux, 512 fichiers maximum, 16 Mo maximum par fichier).
Les morceaux de SD:/mp3 sont listés en premier.
X actualise la liste (par exemple après un téléchargement) sans couper le morceau en cours.
Pochettes, dans l'ordre : image du même nom que le MP3 (Stan.jpg pour Stan.mp3),
pochette intégrée au MP3, ou cover.jpg / folder.jpg (ou .png) dans le dossier du MP3.
Une seule image cover.jpg dans le dossier d'un album suffit donc pour tout l'album.
Les numéros en tête des noms de fichiers (01. , 01 - ) sont masqués à l'affichage.
Onglet RADIO
Le réseau (Broadband Adapter, adresse IP automatique) s'active au premier passage sur l'onglet.
Liste des radios : SD:/radio.txt, créée au premier lancement. Une station par ligne :
Nom de la radio | http://adresse/du/flux.mp3
Autre radio     | https://adresse/du/flux.mp3
Flux MP3 en http:// ou https:// (pas d'AAC/OGG).
Les listes .m3u / .pls et les redirections (y compris http ↔ https) sont suivies.
En https, la connexion sécurisée prend quelques secondes de plus à démarrer
(le processeur de la GameCube est lent pour le chiffrement). Si une station marche
aussi en http://, c'est plus léger.
Les certificats des sites sont vérifiés avec la liste de Mozilla (intégrée au moment
de la compilation). Les dates d'expiration ne sont pas vérifiées, car la GameCube
n'a pas forcément la bonne heure.
Limite de sécurité : la GameCube n'a pas de générateur aléatoire matériel ; l'aléa
du chiffrement vient des variations de l'horloge du processeur. C'est suffisant pour
écouter la radio, mais ne pas réutiliser ce code pour envoyer des mots de passe.
Préférer des serveurs proches (France / Europe) : avec les serveurs lointains, le réseau
de la GameCube peut être trop lent pour du 128 kbps.
Logo facultatif : SD:/radio/<Nom>.jpg ou .png.
Onglet WEB
Navigateur simple : il affiche le texte, les titres, les listes, les liens et les images
(JPEG, PNG, GIF). Pas de JavaScript ni de vidéo : les sites simples marchent
(Wikipédia, archive.org, pages de téléchargement), pas YouTube, Spotify, SoundCloud ni Deezer.
Les images se chargent après le texte, en commençant par celles qui sont à l'écran ; la page
reste utilisable pendant ce temps. Non affichées : WebP, SVG et photos de plus de 1,2 mégapixel
(leur description s'affiche à la place).
Page d'accueil (touche X) : recherche web, recherche de musique libre sur archive.org, favoris.
Y ouvre le clavier : tape une adresse (fr.m.wikipedia.org) ou des mots à chercher.
Sans http://, le navigateur essaie d'abord en https, puis en http.
Un lien vers un fichier (MP3, ZIP...) est téléchargé dans SD:/downloads en tâche de fond :
on peut continuer à naviguer et à écouter. Les MP3 apparaissent tout seuls dans l'onglet MUSIQUE.
Musique libre : « Chercher de la musique libre sur archive.org » sur l'accueil, puis ouvrir un
résultat et choisir un fichier .mp3.
Favoris : SD:/favoris.txt (créé au premier lancement), une ligne par site : Nom | adresse.
L'accueil propose aussi d'ajouter la dernière page visitée.
Formulaires : les recherches simples (envoi GET) marchent ; les formulaires de connexion
(envoi POST) ne sont pas gérés.
Les pages de plus de 1,5 Mo sont coupées. Avec les serveurs lointains, le réseau de la
GameCube est lent : le téléchargement d'un MP3 peut prendre plusieurs minutes.
Commandes
Touche
Action
Z
Changer d'onglet (la lecture continue)
Croix haut / bas
Choisir
Croix gauche / droite
Page précédente / suivante
A
Lire la piste / écouter la station
B
Arrêter
X
Actualiser la liste des morceaux (onglet MUSIQUE)
Y
Pause (musique) / couper le son (radio)
L / R
Piste ou station précédente / suivante
START
Quitter
Onglet WEB :
Touche
Action
Croix haut / bas
Lien précédent / suivant
Croix gauche / droite, stick C
Faire défiler la page
A
Ouvrir le lien (ou remplir un champ)
B
Page précédente (ou annuler le chargement)
Y
Taper une adresse ou une recherche
X
Accueil (favoris, recherche, téléchargement en cours)
Clavier : A = taper la touche, B = effacer, X = espace, START = valider, Y = annuler.
Compilation (GitHub Actions)
Le workflow télécharge tout seul minimp3, stb_image, mbedTLS 3.6.7 (archive officielle,
empreinte SHA-256 vérifiée) et la liste de certificats de curl.se, puis compile le .dol.
Crédits
minimp3 (lieff, CC0), stb_image (Sean Barrett, domaine public), mbedTLS (Apache 2.0),
certificats racines Mozilla (MPL 2.0, via curl.se), police DejaVu Sans, libogc / devkitPro.

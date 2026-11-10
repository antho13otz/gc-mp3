# À faire (GC Station)

Liste tenue à jour à chaque session (un seul envoi par session). Le plus récent en haut.

## Fait (7 novembre 2026, nuit) — console 911, appli 457 (2e envoi de la session, accord du propriétaire)

- Console v382 (build 911) : Pulse, direct jusqu'à 96 morceaux et 16 Ko par image (mode Détaillé).
- Appli 457 : Détaillé plus riche avec la console 911 ; variantes privée / publique (code privé retiré à la compilation),
  carte simplifiée de Wind Waker (point 12), mise à jour publique sans jeton, job `appli-publique`.

## À faire par le propriétaire (version publique)

- Ajouter dans GitLab (Settings > CI/CD > Variables, **protégées**) : `ANDROID_PUBLIC_KEYSTORE_B64` (le fichier
  `gc-remote-lite.jks` en base64, envoyé dans la session du 7 novembre) et `ANDROID_PUBLIC_KEYSTORE_PASS` (son mot de
  passe, « masquée »). Garder une copie de la clé : sans elle, plus de mise à jour de la version publique.
- Puis lancer à la main le job `appli-publique` (≈ 7 min) : il publie `gc-remote-lite-<N>.apk` dans le paquet public
  `appli-publique` (lien : Deploy > Package registry). Avant de diffuser : l'essayer.
- mGBA (MPL 2.0) : ses fichiers sont repris tels quels (version 0.10.5) ; le lien vers leur code est affiché dans l'appli.
  Si un jour ils sont modifiés, publier ces fichiers modifiés.

## Fait (7 novembre 2026, soir) — appli 456

- Licences : `android/LICENSE`, `pc/LICENSE` « Tous droits réservés » ; MIT pour la console seulement.
- Fantôme d'un ami (MQTT public, code à partager), équipement de Link, mode du direct retenu par jeu, connexion faible
  (proposer le Léger), rapport du direct en graphiques + comparaison, plus de noir au plein écran, export des packs avec
  les réglages, 4e mode « Détaillé » (appli seule, plafonds de Pulse inchangés). Détails : HISTORIQUE.md.

## À essayer par le propriétaire (appli 456)

- **Fantôme** : deux téléphones, même code, Wind Waker lancé des deux côtés (ou un seul pour commencer : « En attente de
  ton ami… », « Connecté (broker.hivemq.com) »). Vérifier : Link translucide, bateau, « Aller le voir » (même île, puis
  une autre île).
- **Équipement** : vue 3D de Wind Waker, épée / bouclier visibles sur Link ? Sinon : 🐞 Bug en plein écran → le journal
  liste « équipement de Link : N modèles accrochés ; dans le pack : M ». Si M = 0, le pack ne contient pas encore ces
  modèles (voir plus bas).
- **Rapports du direct** dans chaque mode (même endroit, sans bouger), « Garder », puis « Comparer deux rapports » :
  coût du mode Classique enrichi, de l'équipement et du mode Détaillé (objectif Classique ≤ ~1,2 ms en moyenne).
- Plein écran de la vue 3D : plus de noir ? Mode retenu par jeu ; bandeau « connexion faible » (loin de la box) ;
  export des réglages avec les packs, puis import.

## Appli : suite

- **Pack Wind Waker : modèles de l'équipement de Link** (à ajouter au pack, fabriqué à part depuis la copie du jeu du
  propriétaire, Link.arc). Signatures vues dans la copie de la mémoire du 7 novembre (taille du fichier du modèle,
  nombre d'os ; « porté » = à côté de Link à ce moment, à la mer, épée de légende réveillée + bouclier miroir) :
  +0x330 : 9984 / 1 (porté) ; +0x334 : 20768 / 1 (porté) ; +0x954 : 92800 / 11 ; +0x958 : 26464 / 4 (porté) ;
  +0x95C : 9504 / 1 ; +0x97C : 9056 / 1 (porté) ; +0x980 : 30752 / 6 (porté) ; +0x984 : 24768 / 6 ;
  +0x2E7C : 19168 / 1 (porté) ; +0x2E84 et +0x2E88 : 17568 / 4 ; +0x2E8C : 9888 / 3 ; +0x2E98 : 15552 / 5 (porté) ;
  +0x2EE8 : 19328 / 5 (porté) ; +0x2EEC : 6432 / 1 ; +0x2F14 : 8032 / 1 ; +0x2F1C : 18464 / 4 ; +0x2F38 : 14752 / 4 ;
  +0x2F40 à +0x2F68 : 8896 / 1 (six) ; +0x2F80 : 49664 / 42. Il suffit qu'ils soient dans la bibliothèque `<lieu>_vif.r3p`
  (comme la bombe) : l'appli les reconnaît seule.
- **États de Link** : vie, épée, bouclier, magie affichés sous la vue (sans rien demander de plus à Pulse). Reste :
  l'objet en main et la cible visée (adresses à trouver dans une copie de la mémoire prise en visant un ennemi, objet en main).
- **Mode Détaillé, suite : fait** (console 911, appli 457). À régler avec des « Rapports du direct » (Détaillé avec la 911).
- **Séparer en deux projets GitLab (proposé, attend l'accord du propriétaire)** : un public (console, ses mises à jour,
  catalogue, fiches) et un privé (appli, PC). Attention : `maj_depot` de la console et l'appli pointent sur
  `gitlab:gc-station-group/gc-station` : garder la console dans ce projet-là (les paquets publics y restent), et déplacer
  `android/` et `pc/` dans un nouveau projet privé (son propre `.gitlab-ci.yml`, la clé de signature, la mise à jour de
  l'appli depuis ses paquets privés avec jeton). Rien ne casse pour les consoles déjà installées.
- **Version publique allégée de l'appli : commencée le 7 novembre (appli 457)**, à essayer avant diffusion ; plan d'origine : deux variantes Android (privée complète, publique
  allégée : noms et identifiants différents, clé de signature à part) ; les fonctions privées retirées du code de la
  version publique à la compilation (pas seulement cachées). Choix du propriétaire (7 novembre, 17 h 25) :
  - dans la version publique : télécommande et musique (radios, podcasts, lecture) ; jeux : étagère et fiches ; fichiers
    et sauvegardes (envoi, cartes mémoire, copies) ; globe des radios ; GBA / Tingle Tuner ; chercheur de codes et
    succès ; mini-jeux et jeu de cartes ; statistiques de jeu ;
  - **point 12 (ajouté le 7 novembre, soir)** : à la place de la vue 3D, une **carte simplifiée faite maison**, sans aucun
    fichier du jeu : la Grande Mer en grille de 7 × 7 cases, les îles en formes simples avec leur nom, et **seulement la
    position de Link** en direct, avec sa direction (Pulse ne lit que sa position : le direct le plus léger) ;
  - gardé pour le propriétaire (retiré du code public) : manette du téléphone en jeu ; vue 3D en direct (Wind Waker, Mario
    Kart, Rayman 3, packs, compagnon, fantôme, équipement de Link, modes du direct) ; Salon ; extraire d'un .iso ;
  - diffusion : APK public sur GitLab (lien public, paquet générique public) ; la version publique se met à jour seule
    depuis ce paquet, sans jeton. Ne jamais publier l'APK privé. GBA : mGBA sous MPL 2.0 (publier le code de ses fichiers,
    déjà téléchargés tels quels) ; mentionner la licence dans l'appli publique ;
  - règle du propriétaire : la version publique ne contient rien qui extrait, convertit ou distribue des fichiers de jeux
    (pas d'extraction d'ISO, pas de packs 3D ni d'outil pour en faire, aucun fichier de jeu ni BIOS fourni).
- **Mise à jour de la console lente depuis GitLab** (signalé le 7 novembre, 15 h 58) : toujours à regarder (durée avant /
  maintenant, journal de la console après une mise à jour).

## Fait (7 novembre 2026, après-midi)

- Console v380 (build 909) et appli 454 : Pulse « 2 » (objets un envoi sur deux) ; Pulse « 3 », 1re étape (essai).

## Fait (7 novembre 2026, midi)

- Appli 453 : plein écran de la vue 3D depuis la fiche d'un jeu.
- GitLab : job manuel `console-disque` (disque de démarrage, fichiers privés).
- Workflows GitHub : appli : une compilation plus récente arrête l'ancienne ; fichiers de l'appli et du PC gardés 7 jours.

## Fait (session du 7 novembre 2026, nuit)

- Console v379 (build 908) : deux Pulse (BBA / W5500, étape 1) ; jaquette à plat sans Internet.
- Appli 452 : mise à jour depuis GitLab (jeton), fiches et avis console sur GitLab, export / import des réglages, noms `sea_t` / `sea_e`.
- Catalogue d'applis et fiches des jeux publiés sur GitLab.

## Appli

- **Trois modes du direct : faits** (appli 455), retenus par jeu et 4e mode « Détaillé » (appli 456).
- **Fantôme d'un ami : fait** (appli 456, à essayer).
- **Plein écran de la vue 3D depuis la fiche d'un jeu : corrigé dans l'appli 453** ; plus de noir pendant le chargement
  (appli 456 : l'image d'avant reste affichée).
- Direct de Wind Waker (une image sur deux, build 451) : demander un « Rapport du direct » pour comparer avec le build 440
  (3,6 Ko par envoi, 1,62 ms).

## Console

- **Pulse « 2 » : fait** (console 909, appli 454 ; activé par défaut). À juger à l'œil par le propriétaire : objets assez
  fluides ? Sinon : vue 3D > « Ce que la console envoie » > « Objets : un envoi sur deux ».
- **Pulse « 3 », 1re étape : faite, à essayer** (console 909, appli 454 : « Essai : direct sans recopie », coupé par défaut).
  À essayer par le propriétaire : l'activer, jouer à Wind Waker avec la vue 3D, vérifier que le jeu ne fige pas et que la vue
  suit, puis « Rapport du direct » (comparer avec et sans). Étapes suivantes si ça marche : morceaux compacts sans tampon
  intermédiaire ; puis l'activer par défaut. Si le jeu fige : couper l'essai dans l'appli (pas besoin de nouveau build).
- Fantôme d'un ami : fait dans l'appli seule (456), rien sur la console.
- À vérifier sur la console du propriétaire (builds 908 / 909) : direct, manette du téléphone, carte SD, Pulse LAN, comme avant.
  La version W5500 n'a été essayée que dans le banc d'essai (le propriétaire n'a qu'un BBA) : il faudra quelqu'un avec un ETH2GC.
- Banc d'essai de Pulse : le test du micro (`emu_mic.py`) et les essais de bout en bout (`emu_e2e.py`, qui ne tournaient plus
  depuis v359 : fichier de Pulse LAN manquant) échouent déjà avec le code d'avant le build 908 (mêmes fichiers) : à remettre en état.

## Compilations

- **Le jour du retour sur GitHub** (promis dans la réponse au support de GitHub, 7 novembre 2026), en plus de
  docs/RETOUR-GITHUB.md :
  - (fait au build 453 : une compilation plus récente annule l'ancienne pour l'appli, comme la console, le PC et le
    simulateur ; fichiers de l'appli et du PC gardés 7 jours) ; seulement les derniers fichiers dans les releases ;
  - l'assistant ne fusionne plus rien tout seul : le propriétaire relit et fusionne chaque changement ; jeton de
    l'assistant limité à ce seul dépôt, avec seulement les droits nécessaires ;
  - un seul lot de changements par session, pas de tâche planifiée, pas de tests longs, une seule session à la fois.
- Pas encore repris sur GitLab : l'essai du PC sur un vrai Windows (pas de Windows gratuit sur GitLab).

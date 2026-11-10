/* v397 : bandeau des succes sur la tele, pendant les jeux (source/natres/bandeau.c).
   Un petit programme a part, pose par GC Station dans la place libre sous Pulse (avec les aiguillages et les cheats),
   appele a chaque image par Pulse au moyen d'un code "C0" mis en tete de la liste des cheats (Pulse execute deja ces
   codes, juste apres le jeu : rien n'a change dans Pulse). Il dessine un bandeau en bas de l'image que la console envoie a
   la tele, pendant ~4 s, quand :
     - un succes de GC Station vient d'etre obtenu (Pulse les verifie deja : section "ACH1") ;
     - l'appli du telephone a ecrit un texte (succes RetroAchievements, ou le bouton de test), par l'ecriture "W" deja
       connue de Pulse.
   Le code est sans donnees a lui (ni variables globales, ni tables) : tout est dans la zone de donnees ci-dessous, dont
   l'adresse lui est donnee a chaque appel. Il peut donc etre pose a n'importe quelle adresse.

   Zone de donnees (mots de 32 bits ; adresses = adresses dans la memoire de la console). L'appli la trouve ainsi :
   en-tete de Pulse (0x817EE000) + 0x19C = adresse de la liste des cheats ; mot 0 = nombre de mots ; mots 1-2 = C0000000
   00000003 ; mots 3-4 = "lis r3,haut" "ori r3,r3,bas" : l'adresse de cette zone (premier mot : "BAN1").
   v399 : "BAN2" : le telephone peut aussi donner l'etiquette (1re ligne, BAN_O_RAETIQ) et l'icone (BAN_O_RAICONE) avec le
   type 4 (notification : message, appel, alarme, ami...) ; les donnees qui suivent sont 0x40 plus loin (BAN_DONNEES). */
#ifndef BANDEAU_H
#define BANDEAU_H

#define BAN_MAGIC     0x42414E33u      /* "BAN3" (v401 : icones en image ; "BAN2" : v399-v400 ; "BAN1" : v397-v398) */
#define BAN_O_MAGIC   0x00
#define BAN_O_VI      0x04             /* registres de l'image (0xCC002000) */
#define BAN_O_ACH     0x08             /* section "ACH1" de Pulse (0 : pas de succes GC Station dans ce jeu) */
#define BAN_O_TITRES  0x0C             /* titres des succes : n x BAN_TITRE octets (codes de la police, 0 a la fin) */
#define BAN_O_NTIT    0x10
#define BAN_O_VUS0    0x14             /* succes deja annonces (ou obtenus avant cette partie) : bits 0-31 */
#define BAN_O_VUS1    0x18             /* bits 32-63 */
#define BAN_O_INIT    0x1C
#define BAN_O_RASEQ   0x20             /* (telephone) augmente apres chaque texte ecrit en BAN_O_RATXT */
#define BAN_O_RATYPE  0x24             /* (telephone) 2 : succes RetroAchievements, 3 : test, 4 : notification (v399) */
#define BAN_O_RAVU    0x28
#define BAN_O_RESTE   0x2C             /* images restantes du bandeau affiche */
#define BAN_O_TYPE    0x30             /* 1 : GC Station, 2 : RetroAchievements, 3 : test, 4 : notification */
#define BAN_O_POLICE  0x34             /* police : BAN_NG x BAN_LH lignes de 16 bits */
#define BAN_O_TOUR    0x38
#define BAN_O_COUL    0x3C             /* couleurs : 6 x {Y, Cb, Cr, 0} (fond, bord, etiquette, texte, trophee, ombre) */
#define BAN_O_RATXT   0x60             /* (telephone) 64 octets : 2e ligne (codes de la police, 0 a la fin) */
#define BAN_O_LIGNE   0xA0             /* 64 octets : 2e ligne affichee */
#define BAN_O_ETIQ    0xE0             /* 3 x 32 octets : 1re ligne (GC Station, RetroAchievements, test) */
#define BAN_O_RANG    0x140            /* 736 octets : une ligne de pixels du bandeau (numeros de couleur) */
#define BAN_O_RAETIQ  0x420            /* (telephone, v399) 32 octets : 1re ligne du type 4 (codes de la police, 0 a la fin) */
#define BAN_O_RAICONE 0x440            /* (telephone, v399) icone du type 4 : code de la police (1 trophee, 2 message, 3 appel,
                                          4 alarme, 5 ami, 6 info) ; v401 : 16 a 19 : l'image de la case 0 a 3 (BAN_O_IMG) */
#define BAN_O_ETIQ4   0x444            /* 32 octets : 1re ligne affichee pour le type 4 (copie de RAETIQ) */
#define BAN_O_ICONE   0x464            /* icone affichee pour le type 4 (copie de RAICONE) */
#define BAN_O_BAS     0x468            /* v400 : 1 : en bas de l'image (reglage) ; 0 : en haut, au milieu */
/* v401 : 4 cases d'icone en image (celle de l'application du telephone : WhatsApp, Snapchat...) : 16 x 16 pixels en YUYV
   (comme l'image de la tele : 2 pixels par mot, Y0 Cb Y1 Cr), deja posees sur la couleur du fond du bandeau par l'appli.
   Icone 16 a 19 (BAN_O_RAICONE) : la case 0 a 3. L'appli les remplit une fois par application et par partie. */
#define BAN_O_IMG     0x480
#define BAN_IMG_W     16
#define BAN_IMG_N     4
#define BAN_IMG_T     (BAN_IMG_W * BAN_IMG_W * 2)
#define BAN_DONNEES   (BAN_O_IMG + BAN_IMG_N * BAN_IMG_T)   /* 0xC80 */
#define BAN_TITRE     48
#define BAN_TXT       64
#define BAN_DUREE     220              /* images (~4 s) */
/* v398 : forme du bandeau (pixels) ; BAN_LW / BAN_LH : taille d'un caractere (meme valeur dans bandeau_police.h) */
#define BAN_LW        11               /* (v400 : un peu plus petit ; v398 : 12 x 24) */
#define BAN_LH        22
#define BAN_HAUT      58               /* hauteur : bord, 1re ligne, 2e ligne, bord (v398 : 70) */
#define BAN_L1        6                /* 1re ligne (etiquette et trophee) */
#define BAN_L2        30               /* 2e ligne (titre du succes) */
#define BAN_TX        38               /* debut du texte (apres le trophee) */
#define BAN_RAYON     13               /* coins arrondis */
#define BAN_BORD      2                /* epaisseur du bord rose */
#define BAN_VIDE      0xFF             /* (dans le rang : pixel du jeu, hors du coin) */
#define BAN_ENTREE    14               /* images pour arriver */
#define BAN_SORTIE    10               /* images pour repartir */

#endif

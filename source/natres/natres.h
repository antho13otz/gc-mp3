// GC Station v49 : lanceur natif, programme resident (partage entre GC Station et natres.c)
// Le programme resident vit tout en haut de la memoire (NATRES_AT) pendant le jeu et lit
// l'ISO directement sur la carte SD, sans systeme de fichiers : GC Station lui donne la
// liste des morceaux de l'ISO sur la carte (secteurs), calculee avant le lancement.
#ifndef NATRES_H
#define NATRES_H

#define NATRES_AT     0x817EE000u        /* adresse fixe (liee par tools/natres.ld) ; (v273 : 8 Ko plus bas que 0x817F0000,
                                            pour les manettes du telephone : le resident etait plein) */
#define NATRES_SIZE   0x00011F00u        /* place reservee (le haut, 817FFF00+, est laisse a la console) */
#define NATRES_HTAB   0x817F0000u        /* (NRO_TRAP) table des pages : alignee sur 64 Ko, quelques groupes en haut du resident */
#define NATRES_MAGIC  0x4743534Eu        /* "GCSN" */
#define NATRES_VER    19
#define NATRES_MAXEXT 1500               /* morceaux de l'ISO sur la carte (fichier fragmente) ; v60 : 2000 (place pour les aiguillages) */

/* erreurs (champ err) */
enum { NR_OK = 0, NR_ENOCARD = 1, NR_EINIT = 2, NR_ECMD = 3, NR_ETOKEN = 4, NR_ERANGE = 5, NR_ETIMEOUT = 6 };

typedef struct { unsigned int iso, sd, cnt; } natres_ext_t;   /* secteurs de 512 octets */

typedef int (*natres_init_fn)(void);
typedef int (*natres_read_fn)(void *dst, unsigned int off, unsigned int len);

typedef struct {
    unsigned int magic, version;
    /* adresses des fonctions (entiers 32 bits : meme taille partout) */
    unsigned int init;                                    /* int init(void) : 0 = carte prete */
    unsigned int read;                                    /* int read(void *dst, u32 off, u32 len) : lit l'ISO ; 0 = OK */
    unsigned int read_sd;                                 /* int read_sd(u32 secteur, void *dst) : secteur brut (tests) */
    /* rempli par GC Station */
    unsigned int chan, dev, freq;                         /* EXI : canal, peripherique, vitesse */
    unsigned int n_ext;
    unsigned int ext;                                     /* adresse de la table des morceaux */
    /* rempli par le programme resident */
    unsigned int sdhc, err, r1, stage, nread;
    unsigned int end;                                     /* fin de la place utilisee (verification) */
    /* faux lecteur DVD (etape 3b) : appele par les aiguillages poses dans main.dol */
    unsigned int di_load;                                 /* u32 di_load(u32 registre) */
    unsigned int di_store;                                /* void di_store(u32 registre, u32 valeur) */
    unsigned int flags;                                   /* NRF_... (rempli par GC Station) */
    unsigned int ncmd, last_cmd, last_off, last_len;      /* suivi (journal) */
    unsigned int pad;                                     /* v55 : ecritures de DISR par le jeu (acquittements) */
    /* v54 : journal pendant le jeu, ecrit sur la carte (un secteur d'un fichier prepare) */
    unsigned int log_sec;                                 /* secteur de la carte (0 = pas de journal) */
    unsigned int nkick, nskip;                            /* vraies commandes "inquiry" lancees / lecteur occupe */
    unsigned int nrderr, nretry;                          /* erreurs de lecture SD / reprises reussies */
    unsigned int nack_tc, last_real_disr;                 /* acquittements avec vraie fin (TCINT) */
    unsigned int nlog;                                    /* nombre d'entrees ecrites dans l'anneau */
    /* v57 : commandes speciales, crochets d'interruptions (image / manettes), retour a GC Station */
    unsigned int n_e1, n_e2, n_ab, n_misc;                /* audio : jouer / etat ; chercher ; autres */
    unsigned int vi_count, si_count;                      /* passages dans les crochets (0 = pas branches) */
    unsigned int igr;                                     /* 1 = A+Z+START : retour a GC Station demande */
    unsigned int flush_tb;                                /* (interne : dernier journal ecrit) */
    /* v59 : manettes vues par le crochet SI (diagnostic) */
    unsigned int si_reads, si_skip;                       /* manettes lues / laissees (donnees pas encore prises par le jeu) */
    unsigned int si_last, si_changes;                     /* dernieres touches vues (manette 1), nombre de changements */
    /* v60 : plantage du jeu (exception), signes de vie (interruptions son / DSP / dessin) */
    unsigned int exc_num, exc_srr0, exc_lr, exc_dar, exc_dsisr;
    unsigned int cnt_ai, cnt_dsp, cnt_pe;
    /* v61 : lectures en fond (par morceaux, rythmees par le vrai lecteur) : le son ne coupe plus */
    unsigned int n_async, n_chunks, kick_us, kick_max_us;
    /* v64 : retour a GC Station sans redemarrer la console (A+Z+START, plantage) : le resident
       recharge le .dol de GC Station depuis la carte (morceaux prepares par GC Station) */
    unsigned int ret_n, ret_tab, ret_size, ret_state;     /* ret_state : 1 = retour en cours */
    /* v65 : musique du disque (streaming) et carte memoire virtuelle (slot A) */
    unsigned int exi_load, exi_store;                     /* aiguillages des registres EXI du canal 0 */
    unsigned int card_n, card_tab, card_size, card_id;    /* carte virtuelle : morceaux du fichier, taille, ID (Mbits) */
    unsigned int st_blocks, st_mix;                       /* musique : blocs decodes, tampons du son completes */
    unsigned int card_cmds, card_writes, card_last, card_irqs;
    /* v66 : diagnostic des ecrans noirs (Paper Mario) : crochets poses, entrees dans les fonctions du jeu */
    unsigned int n_inq, hook_mask, vi_in, si_in;          /* vraies inquiry, bits = interruptions branchees, VI / SI entrees */
    /* v67 : ou en est le jeu tant qu'il n'affiche rien : adresse du code interrompu (echantillons
       pris aux fins de commande du vrai lecteur, relance aussitot), etat du DSP */
    unsigned int n_samp, samp_lr, samp_pc[4], dsp_csr, dsp_mbox;
    /* v83 : photo de la memoire du jeu au plantage (fichier natif/memoire-jeu.bin, d'un seul morceau) */
    unsigned int snap_sec, snap_cnt;                      /* 1er secteur du fichier, nombre de secteurs */
    unsigned int snap_hi;                                 /* fin du programme du jeu (main.dol + bss) : debut de son arene */
    unsigned int snap_done;                               /* secteurs ecrits */
    /* v94 : lecture plus rapide de la carte (DMA du bus EXI, 32 MHz verifie par le CRC des blocs) */
    unsigned int xfer;                                    /* bit 0 = DMA, bit 1 = 32 MHz, bit 2 = 32 MHz abandonne (CRC faux) */
    unsigned int crc_err;                                 /* blocs au CRC faux (relus) */
    unsigned int exec_n, exec_last;                       /* v94 : redemarrages du jeu sur un autre programme du disque */
    /* v95 : OSExec (disques a plusieurs programmes : Zelda Collector's Edition) : le code de
       redemarrage de l'apploader (lu par le jeu en 0x81300000) et les programmes qu'il charge sont
       branches au faux lecteur pendant le jeu, avec des aiguillages ecrits par le resident */
    unsigned int reboot_off, reboot_len;                  /* (GC Station) position du code de redemarrage dans l'ISO, taille (0 = apploader recent) */
    unsigned int tramp_at, tramp_n;                       /* place libre pour les aiguillages (GC Station) ; acces branches pendant le jeu */
    /* v99 : l'ecran (ecran noir apres le choix 480p : jeu fige, ou image envoyee mais pas affichee ?) */
    unsigned int vi_now;                                  /* DCR (0xCC002002) << 16 | horloge VI (0xCC00206C : 1 = 54 MHz, 480p) */
    unsigned int vi_dtv;                                  /* cable numerique / composante (0xCC00206E) << 16 | DI1 (0xCC002034, v111) */
    unsigned int vi_chg;                                  /* changements de mode vus (DCR sans le bit "actif", horloge) */
    unsigned int vi_chg_at;                               /* image (VI) du dernier changement */
    /* v121 : 2e disque (jeux a 2 disques) : table des morceaux de l'autre disque, echangee avec ext / n_ext
       quand le jeu demande un changement de disque (moteur arrete, E3) ; cheats (codes Gecko) */
    unsigned int ext2, n_ext2;                            /* (GC Station) l'autre disque : table, nombre de morceaux (0 = aucun) */
    unsigned int disc;                                    /* disque en cours (0 / 1) | changements faits << 8 */
    unsigned int chts;                                    /* (GC Station) codes Gecko : adresse de {nombre de mots, mots...} (0 = aucun) */
    /* v122 : carte memoire virtuelle du slot B ; manettes ; vitesse d'un vrai lecteur */
    unsigned int card2_n, card2_tab, card2_size, card2_id;  /* comme card_* (slot A) */
    unsigned int si_load, si_store;                       /* aiguillages des registres des manettes (SI) */
    unsigned int opt;                                     /* (GC Station) NRO_... */
    unsigned int si_fixes;                                /* lectures des manettes modifiees (journal) */
    /* v139 : Pulse universel (option NRO_TRAP) : acces du jeu a la page DI / SI / EXI / AI pris par exception */
    unsigned int trap_state;                              /* 0 = pas demande, 1 = actif, 2.. = refuse (NRT_...) */
    unsigned int trap_n;                                  /* acces pris */
    unsigned int trap_pc;                                 /* code du dernier acces pris */
    unsigned int trap_bad;                                /* derniere instruction pas prise en charge (sautee) */
    /* v143 : Pulse par le reseau (ISO lue sur une autre GC Station ou un PC, par le Broadband Adapter) */
    unsigned int net;                                     /* (GC Station) adresse d'un natres_net_t (0 = ISO sur la carte SD) */
    unsigned int net_pk;                                  /* paquets de donnees recus */
    unsigned int net_rt;                                  /* demandes refaites (paquet perdu, delai) | etat << 24 (NRN_...) */
    unsigned int n_rescue;                                /* fins de commande donnees en secours (depuis l'image, v133) */
    /* v145 : jeux qui se servent du Broadband Adapter (jeux en reseau local) ; partage du bus EXI */
    unsigned int bba_n;                                   /* le jeu a choisi le BBA (canal 0, peripherique 2) : nombre de fois */
    unsigned int exi_wait;                                /* lectures de la carte SD remises a plus tard : le jeu se servait du bus */
    unsigned int exi_sync;                                /* lectures faites en attendant que le jeu libere le bus (lecture immediate) */
    unsigned int tramp_end;                               /* v159 : (GC Station) fin de la place des aiguillages (0 = sous la table des morceaux) */
} natres_hdr_t;
/* v143 : reglages du reseau (ranges par GC Station dans la place libre du resident) */
typedef struct {
    unsigned int magic;                                   /* "PNET" */
    unsigned int ip, srv, port;                           /* adresse de la console, du serveur (IPv4), port UDP du serveur */
    unsigned int tok[2];                                  /* fichier de chaque disque chez le serveur */
    unsigned int size[2];                                 /* taille de chaque disque (octets) */
    unsigned int gw;                                      /* passerelle (serveur sur un autre reseau : rarement) */
    unsigned int mask;
} natres_net_t;
#define NATRES_NET_MAGIC 0x504E4554u                    /* "PNET" */
/* manettes du telephone : section "PPAD" a la suite des cheats (et de "FIXT" / "ACH1") : un telephone envoie
   ses touches en UDP (port 51237, "GCPD" {version, port de manette 0-3, numero, INBUFH, INBUFL}) ; Pulse lit le
   Broadband Adapter a chaque image et les donne au jeu comme une manette branchee sur ce port (registres SI). */
#define NATRES_PAD_MAGIC 0x50504144u                    /* "PPAD" */
#define NATRES_PAD_PORT  51237
typedef struct {
    unsigned int magic;                                   /* "PPAD" */
    unsigned int len;                                     /* (mots de la section, magic compris) */
    natres_net_t net;                                     /* (GC Station) adresse de la console (srv = 0), masque, passerelle */
    unsigned int chmask;                                  /* (GC Station) ports donnes aux telephones (bit n = port n + 1) */
    unsigned int state;                                   /* (resident) 0 pas demarre, 1 pret, 2 pas de BBA, 3 BBA pris par le jeu */
    unsigned int rx, bad, arp;                            /* (resident) paquets de manette recus, refuses, demandes ARP repondues */
    unsigned int seq[4], h[4], l[4], tb[4];               /* (resident) dernier paquet par port : numero, INBUFH, INBUFL, horloge */
    unsigned int xch, xcmd, types, origins;               /* (resident) dernier transfert SI vers un port telephone ; reponses faites */
    /* (resident, journal : 7e secteur "GCSP") ce que le jeu demande aux manettes : derniers transferts SI (port << 24 |
       commande << 16 | octets envoyes << 8 | octets attendus), dernier SIPOLL ecrit, lectures de SISR, de COMCSR et du
       tampon de reponse sur un port du telephone, lectures de INBUF par port */
    unsigned int ntr, tr[8], poll, nsr, ncom, nbuf, ninb[4];
    /* (resident, journal) ce qui touche les ports du telephone, dans l'ordre : les 20 premiers evenements puis les 20 derniers
       (type << 24 | donnee) : 1 transfert (port << 8 | commande), 2 SIPOLL ecrit (bits 0-23), 3 commande de lecture du
       port ecrite (SICnOUTBUF, bits 0-23), 4 reponse lue (commande, mot), 5 fin de transfert vue (COMCSR, bits 24-31) */
    unsigned int nev, ev[40];
    unsigned int redir;                                   /* (resident, b9000015) transferts envoyes vers une vraie manette a la place */
    /* (resident, v275) vibration : ce que le jeu demande au moteur de chaque port du telephone (commande de lecture, bit 0),
       renvoye a l'appli (UDP "GCRB" vers l'adresse d'ou viennent ses touches) ; nrmb : vibrations demandees (journal) */
    unsigned int rmb[4], rsent[4], pip[4], pport[4], nrmb;
    unsigned char pmac[4][8];
} natres_pad_t;
#define NRF_PADLOG 1024u                                /* 7e secteur du journal : la section "PPAD" au retour */
#define NATRES_PADLOG_MAGIC 0x47435350u                 /* "GCSP" */
#define NRO_PHONE  0x04000000u              /* manettes du telephone (registres SI branches, section "PPAD") */
enum { NRN_OFF = 0, NRN_ON = 1, NRN_NOBBA = 2, NRN_NOLINK = 3, NRN_NOARP = 4, NRN_TIMEOUT = 5 };

#define NATRES_LOGN 1                     /* anneau : {commande, position, longueur, infos} (v145 : 0x1F0 + 1 x 16 = 0x200) */
/* v122 : opt : bit 0 stick C haut/bas inverses, 1 gauche/droite inverses, 2 sticks echanges, bits 8-15 seuil des
   gachettes (0 = rien), bits 16-19 manettes lues N fois plus souvent, bit 24 vitesse d'un vrai lecteur */
#define NRO_SPEED  0x01000000u
#define NRO_TRAP   0x02000000u              /* v139 : acces au lecteur pris par exception (experimental) */
#define NATRES_PTEG 0xFC00u                 /* v139 : (NRO_TRAP) groupes de la table des pages (NATRES_HTAB + ...) : GC Station laisse 0x817FFC00-0x817FFEFF libre */
/* v139 : trap_state : pourquoi l'option n'a pas pu etre activee */
enum { NRT_OFF = 0, NRT_ON = 1, NRT_BAT = 2, NRT_BAT2 = 3, NRT_MMU = 4, NRT_VEC = 5, NRT_PTE = 6, NRT_LOST = 7 };
#define NRF_CARDB  64u                    /* v122 : carte memoire virtuelle du slot B active */
#define NATRES_ZERO 0xFFFFFFFFu           /* v121 : morceau sans donnees (bloc absent d'un .ciso) : lu comme des zeros */
#define NATRES_CARDMAX 64                 /* morceaux du fichier de la carte virtuelle */
#define NRF_CARD      4u                  /* v65 : carte memoire virtuelle active */
#define NATRES_RETMAX 64                  /* morceaux du .dol de GC Station */

#define NRF_REAL_IRQ  1u   /* reveiller le jeu par une vraie commande "inquiry" du lecteur */
#define NRF_IGR       2u   /* v59 : A+Z+START = retour a GC Station (crochet des manettes) */
#define NRF_CRASH     8u   /* v79 : le secteur juste apres le journal est libre pour les details d'un plantage */

/* v79 : details d'un plantage (2e secteur du journal, ecrit seulement si le jeu plante).
   Mots de 32 bits, big-endian :
   0x00 "GCSC"   0x04 version   0x08 exception   0x0C srr0 (PC)   0x10 srr1   0x14 LR
   0x18 CR       0x1C CTR       0x20 DAR         0x24 DSISR       0x28 contexte 0x2C nb d'appels
   0x30 r0..r31 (32 mots)
   0xB0 memoire basse : 28 2C 30 34 38 3C F0 F4 (8 mots)
   0xD0 chaine des appels : adresses de retour lues sur la pile du jeu (16 mots)
   0x110 textes : 30 octets lus a l'adresse contenue dans r3, r4 ... r10 (8 x 30) */
#define NATRES_CRASH_MAGIC 0x47435343u      /* "GCSC" */
#define NATRES_CRASH_VER   1
#define NATRES_CRASH_NBT   16
#define NATRES_CRASH_STR   30
#define NRF_STACK     16u  /* v82 : un 3e secteur du journal est libre pour la pile du jeu au moment du plantage */
/* v82 : 3e secteur du journal (pile du jeu au plantage) :
   0x00 "GCSS"   0x04 r1 (pile)   0x08 nombre de mots   0x0C nombre de textes
   0x10 80 mots lus a partir de r1 (0x140 octets : la fonction d'erreur y range fichier, ligne, message)
   0x150 6 textes : adresse (4 octets) + 28 caracteres, pointes par des mots de la pile (0x200 en tout) */
#define NATRES_STACK_MAGIC 0x47435353u      /* "GCSS" */
#define NATRES_STACK_WORDS 80
#define NATRES_STACK_NSTR  6
#define NATRES_STACK_SLEN  28

/* v83 : photo de la memoire au plantage. 1er secteur : 0x00 "GCSM" 0x04 nombre de zones
   0x08 + 16 * i : adresse, taille, 1er secteur (relatif au fichier), 0 ; puis les zones, a la suite.
   Zones : memoire basse (0x80000000, 0x3200), variables du jeu (r13 +/- 32 Ko), pile (r1),
   debut de l'arene (fin du programme, 256 Ko : en-tetes des tas du jeu). */
#define NRF_SNAP      32u
/* v243 : 4e secteur du journal : les fils (threads) du jeu au retour A+Z+START ou au plantage, pour un ecran
   noir ou le fil principal attend un autre fil. 0x00 "GCST" 0x04 nombre 0x08 fil en cours (0x800000E4)
   0x10 + 32 * i (15 au plus) : adresse du fil, etat << 16 | priorite, srr0 (PC), LR, r1, 3 adresses de retour */
#define NRF_THREADS   128u
#define NATRES_THR_MAGIC 0x47435354u        /* "GCST" */
#define NATRES_THR_MAX   12                  /* v257 : 0x190 - 0x1EF : registres de l'image (VI 0xCC002000 - 0xCC00205F, 48 demi-mots) */
#define NATRES_SNAP_MAGIC 0x4743534Du       /* "GCSM" */
/* v260 : 5e secteur du journal : echanges du jeu avec les autres peripheriques du canal EXI 0 (police et horloge /
   reglages de la console : peripherique 1 ; port serie 1 : 2) quand la carte virtuelle du slot A les fait passer.
   0x00 "GCSX" 0x04 transferts
   0x20 + 32 * i (15 premiers) : CSR << 16 | CR, commande (DATA), MAR, LEN, resultat (DATA lu / 1er mot du DMA),
   2e mot, mot +0x10, mot +0x14 (les mots du DMA sont relus a la fin, quand le jeu deselectionne) */
#define NRF_EXILOG    256u
#define NATRES_EXI_MAGIC 0x47435358u        /* "GCSX" */
#define NATRES_EXI_MAX   15
/* v268 : succes (section "ACH1" a la suite des cheats, voir natres.c) ; 6e secteur du journal : le debut de la
   section : 0x00 "ACH1" 0x04 n  0x08 + 8 * i (63 au plus) : etat (1 arme, 2 obtenu), image (VI) du deblocage */
#define NRF_ACH       512u
#define NATRES_ACHS_MAGIC 0x41434831u       /* "ACH1" */
#define NATRES_SNAP_SECS  704               /* taille du fichier : 704 secteurs (352 Ko) */

typedef char natres_hdr_size_ok[sizeof(natres_hdr_t) == 0x1F0 ? 1 : -1];   /* v145 : 0x1F0 + 1 x 16 = 0x200 */

#endif

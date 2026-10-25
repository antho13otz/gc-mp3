// GC Station v49 : lanceur natif, programme resident (partage entre GC Station et natres.c)
// Le programme resident vit tout en haut de la memoire (NATRES_AT) pendant le jeu et lit
// l'ISO directement sur la carte SD, sans systeme de fichiers : GC Station lui donne la
// liste des morceaux de l'ISO sur la carte (secteurs), calculee avant le lancement.
#ifndef NATRES_H
#define NATRES_H

#define NATRES_AT     0x817F0000u        /* adresse fixe (liee par tools/natres.ld) */
#define NATRES_SIZE   0x0000FF00u        /* place reservee (le haut, 817FFF00+, est laisse a la console) */
#define NATRES_MAGIC  0x4743534Eu        /* "GCSN" */
#define NATRES_VER    15
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
} natres_hdr_t;

#define NATRES_LOGN 6                     /* anneau : {commande, position, longueur, infos} (v121 : 0x1A0 + 6 x 16 = 0x200) */
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
#define NATRES_SNAP_MAGIC 0x4743534Du       /* "GCSM" */
#define NATRES_SNAP_SECS  704               /* taille du fichier : 704 secteurs (352 Ko) */

typedef char natres_hdr_size_ok[sizeof(natres_hdr_t) == 0x1A0 ? 1 : -1];

#endif

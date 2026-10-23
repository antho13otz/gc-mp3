// GC Station v49 : lanceur natif, programme resident (partage entre GC Station et natres.c)
// Le programme resident vit tout en haut de la memoire (NATRES_AT) pendant le jeu et lit
// l'ISO directement sur la carte SD, sans systeme de fichiers : GC Station lui donne la
// liste des morceaux de l'ISO sur la carte (secteurs), calculee avant le lancement.
#ifndef NATRES_H
#define NATRES_H

#define NATRES_AT     0x817F0000u        /* adresse fixe (liee par tools/natres.ld) */
#define NATRES_SIZE   0x0000FF00u        /* place reservee (le haut, 817FFF00+, est laisse a la console) */
#define NATRES_MAGIC  0x4743534Eu        /* "GCSN" */
#define NATRES_VER    6
#define NATRES_MAXEXT 2000               /* morceaux de l'ISO sur la carte (fichier fragmente) ; v60 : 2000 (place pour les aiguillages) */

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
} natres_hdr_t;

#define NATRES_LOGN 18                    /* anneau : {commande, position, longueur, infos} (0xE0 + 18 x 16 = 512) */

#define NRF_REAL_IRQ  1u   /* reveiller le jeu par une vraie commande "inquiry" du lecteur */
#define NRF_IGR       2u   /* v59 : A+Z+START = retour a GC Station (crochet des manettes) */

typedef char natres_hdr_size_ok[sizeof(natres_hdr_t) == 0xE0 ? 1 : -1];

#endif

// GC Station v49 : lanceur natif, programme resident (partage entre GC Station et natres.c)
// Le programme resident vit tout en haut de la memoire (NATRES_AT) pendant le jeu et lit
// l'ISO directement sur la carte SD, sans systeme de fichiers : GC Station lui donne la
// liste des morceaux de l'ISO sur la carte (secteurs), calculee avant le lancement.
#ifndef NATRES_H
#define NATRES_H

#define NATRES_AT     0x817F0000u        /* adresse fixe (liee par tools/natres.ld) */
#define NATRES_SIZE   0x0000FF00u        /* place reservee (le haut, 817FFF00+, est laisse a la console) */
#define NATRES_MAGIC  0x4743534Eu        /* "GCSN" */
#define NATRES_VER    1
#define NATRES_MAXEXT 3000               /* morceaux de l'ISO sur la carte (fichier fragmente) */

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
    unsigned int pad;
} natres_hdr_t;

#define NRF_REAL_IRQ  1u   /* reveiller le jeu par une vraie commande "inquiry" du lecteur */

typedef char natres_hdr_size_ok[sizeof(natres_hdr_t) == 0x60 ? 1 : -1];

#endif

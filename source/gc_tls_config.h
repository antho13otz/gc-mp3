/* Reglages mbedTLS pour la GameCube (libogc), appliques par-dessus la
   configuration par defaut de mbedTLS 3.6 (MBEDTLS_USER_CONFIG_FILE).
   Utilise uniquement pour ecouter des webradios en https://. */

/* Pas de sockets POSIX ni de minuterie POSIX : GC-MP3 fournit les siens. */
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C

/* Pas de stockage de cles sur fichier. */
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C

/* La GameCube n'a pas de generateur aleatoire materiel : GC-MP3 fournit une
   source d'entropie (gigue de l'horloge du processeur, voir mbedtls_hardware_poll). */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* Client seulement, sans messages de debogage : programme plus petit. */
#undef MBEDTLS_SSL_SRV_C
#undef MBEDTLS_DEBUG_C
#undef MBEDTLS_SELF_TEST

/* Horloge en millisecondes fournie par GC-MP3 (compteur de temps du processeur). */
#define MBEDTLS_PLATFORM_MS_TIME_ALT

/* v209 : navigateur Cube Browser (source/cb) : son horloge (SNTP) pour verifier les certificats, et les
   certificats racines cherches au besoin (castore.c) au lieu d'etre tous lus en memoire au demarrage. */
#define MBEDTLS_PLATFORM_TIME_ALT
#define MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK

// Decodeurs FLAC (dr_flac) et OGG Vorbis (stb_vorbis), domaine public.
// Compiles a part de main.c ; les deux fichiers sont telecharges par le workflow.
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_WCHAR
#define DR_FLAC_NO_SIMD
#include "dr_flac.h"

/* memoire remise a zero : les chemins d'erreur de stb_vorbis liberent parfois
   des pointeurs jamais remplis */
#include <stdlib.h>
#define malloc(sz) calloc(1, (sz))
#define STB_VORBIS_NO_PULLDATA_API
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

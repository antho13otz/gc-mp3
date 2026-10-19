/* Sur GameCube les tables restent en memoire normale. */
#ifndef GC_PGMSPACE_STUB_H
#define GC_PGMSPACE_STUB_H
#define PROGMEM
#define pgm_read_byte(p) (*(const unsigned char *)(p))
#define pgm_read_word(p) (*(const unsigned short *)(p))
#endif

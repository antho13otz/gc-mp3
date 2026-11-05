/* GC Station PC (Windows) : inclus avant chaque fichier C (-include win/pcwin.h).
   Le meme code que le simulateur (pcsim.c) : ce qui manque a Windows (MinGW) est ici, et les chemins "sd:/..."
   deviennent le dossier "sd" a cote du programme (voir pcsim_win.c). Rien de Windows (windows.h) ici : seulement
   dans pcsim_win.c, pour ne pas melanger ses noms avec ceux de GC Station. */
#ifndef PCWIN_H
#define PCWIN_H
#ifdef _WIN32
#ifndef __cplusplus
#define _POSIX_THREAD_SAFE_FUNCTIONS 200112L                /* (localtime_r, gmtime_r) */
#define eof pcw_io_eof                                     /* (io.h de Windows a une fonction eof : GC Station, une variable) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <io.h>
#undef eof
/* memoire : pas de limite GameCube sur PC ; memalign (alignement inutile ici) */
#define memalign(a, n) malloc(n)
struct mallinfo { int arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; };
struct mallinfo pcw_mallinfo(void);
#define mallinfo() pcw_mallinfo()
/* chaines */
char *pcw_strcasestr(const char *h, const char *n);
#define strcasestr(h, n) pcw_strcasestr(h, n)
#define random() rand()
#define srandom(s) srand(s)
FILE *pcw_open_memstream(char **b, size_t *n);
#define open_memstream(b, n) pcw_open_memstream(b, n)
int pcw_fclose(FILE *f);
#define fclose(f) pcw_fclose(f)
/* fichiers : chemins traduits ("sd:/x" -> sd\x, UTF-8 -> Windows) */
FILE *__wrap_fopen(const char *p, const char *m);
int __wrap_stat(const char *p, struct stat *s);
DIR *__wrap_opendir(const char *p);
struct dirent *__wrap_readdir(DIR *d);
int __wrap_closedir(DIR *d);
#define fopen(p, m) __wrap_fopen(p, m)
#define stat(p, s) __wrap_stat(p, s)
#define opendir(p) __wrap_opendir(p)
#define readdir(d) __wrap_readdir(d)
#define closedir(d) __wrap_closedir(d)
int pcw_mkdir(const char *p);
int pcw_remove(const char *p);
int pcw_rename(const char *a, const char *b);
int pcw_rmdir(const char *p);
int pcw_unlink(const char *p);
int pcw_access(const char *p, int m);
int pcw_truncate(int fd, long n);
#define mkdir(p, m) pcw_mkdir(p)
#define remove(p) pcw_remove(p)
#define rename(a, b) pcw_rename(a, b)
#define rmdir(p) pcw_rmdir(p)
#define unlink(p) pcw_unlink(p)
#define access(p, m) pcw_access(p, m)
#define ftruncate(fd, n) pcw_truncate(fd, n)
#define S_ISLNK(m) 0
#endif
#endif
#endif

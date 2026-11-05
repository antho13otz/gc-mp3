/* GC Station PC (Windows) : dossiers (comme POSIX, avec d_type) ; fonctions dans pcsim_win.c */
#ifndef PCW_DIRENT_H
#define PCW_DIRENT_H
#ifdef __cplusplus
extern "C" {
#endif
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
struct dirent { unsigned long d_ino; unsigned char d_type; unsigned short d_reclen; char d_name[1024]; };
typedef struct pcw_dir DIR;
DIR *__real_opendir(const char *p);
struct dirent *__real_readdir(DIR *d);
int __real_closedir(DIR *d);
#ifdef __cplusplus
}
#endif
#endif

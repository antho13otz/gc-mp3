/* GC Station PC (Windows) : place libre d'un disque ; fonction dans pcsim_win.c */
#ifndef PCW_STATVFS_H
#define PCW_STATVFS_H
typedef unsigned long fsblkcnt_t, fsfilcnt_t;
struct statvfs { unsigned long f_bsize, f_frsize; fsblkcnt_t f_blocks, f_bfree, f_bavail; fsfilcnt_t f_files, f_ffree, f_favail; unsigned long f_fsid, f_flag, f_namemax; };
int pcw_statvfs(const char *p, struct statvfs *s);
#define statvfs(p, s) pcw_statvfs(p, s)
#endif

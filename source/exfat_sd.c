/* GC Station v326 : cartes SD en exFAT (inclus dans main.c, juste avant mount_sd).
   libfat ne lit que le FAT12/16/32. Quand elle ne monte pas la carte (cartes de 64 Go et plus, vendues en exFAT),
   la carte est montee avec FatFs (ChaN, licence BSD a 1 clause, source/fatfs) sous le meme nom "sd:" : le reste
   du programme (fopen, opendir, stat, rename...) ne change pas. Les cartes FAT32 restent sur libfat, comme avant.
   - les acces passent par sd_io_read / sd_io_write (meme verrou que libfat et les lectures directes) ;
   - un verrou global (exf_mx) : un seul fil a la fois dans FatFs (musique, jaquettes et ecran lisent en meme temps) ;
   - exf_extents : les morceaux d'un fichier sur la carte (secteurs), pour Pulse (le jeu lit la carte sans FatFs).
   Cartes jusqu'a 2 To (secteurs en 32 bits) ; essai sur PC avec une carte exFAT de 1 To (tools/exfat-test).
   Le coeur (exf_*) se compile aussi sur PC (tools/exfat-test) ; la table "sd:" (devoptab) seulement sur la console. */
#define DIR FF_DIR                                         /* (le DIR de FatFs : DIR existe deja, dirent.h) */
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#undef DIR

static FATFS exf_fs;
static int exf_on = 0;                                    /* 1 : la carte est montee par FatFs (exFAT) */
static bool (*exf_rd)(u32 s, u32 n, void *b) = NULL;      /* lecture / ecriture de secteurs (carte de la console) */
static bool (*exf_wr)(u32 s, u32 n, const void *b) = NULL;
static LBA_t exf_nsec = 0;                                /* (taille du disque : seulement pour le banc d'essai) */

/* ---- disque de FatFs (diskio.h) ; tampon aligne : le pilote de la carte veut des adresses multiples de 32 ---- */
#define EXF_BOUNCE 16
static u8 exf_bb[EXF_BOUNCE * 512] __attribute__((aligned(32)));
DSTATUS disk_status(BYTE pd) { (void)pd; return exf_rd ? 0 : STA_NOINIT; }
DSTATUS disk_initialize(BYTE pd) { (void)pd; return exf_rd ? 0 : STA_NOINIT; }
DRESULT disk_read(BYTE pd, BYTE *buf, LBA_t sec, UINT n) {
    (void)pd;
    if (!exf_rd) return RES_NOTRDY;
    if (((uintptr_t)buf & 31) == 0) return exf_rd((u32)sec, n, buf) ? RES_OK : RES_ERROR;
    while (n) {
        UINT k = n > EXF_BOUNCE ? EXF_BOUNCE : n;
        if (!exf_rd((u32)sec, k, exf_bb)) return RES_ERROR;
        memcpy(buf, exf_bb, k * 512); buf += k * 512; sec += k; n -= k;
    }
    return RES_OK;
}
DRESULT disk_write(BYTE pd, const BYTE *buf, LBA_t sec, UINT n) {
    (void)pd;
    if (!exf_wr) return RES_NOTRDY;
    if (((uintptr_t)buf & 31) == 0) return exf_wr((u32)sec, n, buf) ? RES_OK : RES_ERROR;
    while (n) {
        UINT k = n > EXF_BOUNCE ? EXF_BOUNCE : n;
        memcpy(exf_bb, buf, k * 512);
        if (!exf_wr((u32)sec, k, exf_bb)) return RES_ERROR;
        buf += k * 512; sec += k; n -= k;
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE pd, BYTE cmd, void *b) {
    (void)pd;
    if (cmd == CTRL_SYNC) return RES_OK;
    if (cmd == GET_SECTOR_SIZE) { *(WORD *)b = 512; return RES_OK; }
    if (cmd == GET_BLOCK_SIZE) { *(DWORD *)b = 1; return RES_OK; }
    if (cmd == GET_SECTOR_COUNT && exf_nsec) { *(LBA_t *)b = exf_nsec; return RES_OK; }   /* (banc d'essai : f_mkfs) */
    return RES_PARERR;
}
DWORD get_fattime(void) {
    time_t t = time(NULL); struct tm *m = localtime(&t);
    if (!m || m->tm_year < 80) return (DWORD)(45u << 25 | 1u << 21 | 1u << 16);      /* (pas d'heure : 1er janvier 2025) */
    return (DWORD)((m->tm_year - 80) << 25 | (m->tm_mon + 1) << 21 | m->tm_mday << 16 | m->tm_hour << 11 | m->tm_min << 5 | m->tm_sec / 2);
}

/* ---- verrou : un seul fil dans FatFs ---- */
#ifdef GEKKO
static mutex_t exf_mx; static int exf_mx_ok = 0;
static void exf_lock(void) { if (!exf_mx_ok && LWP_MutexInit(&exf_mx, true) >= 0) exf_mx_ok = 1; if (exf_mx_ok) LWP_MutexLock(exf_mx); }
static void exf_unlock(void) { if (exf_mx_ok) LWP_MutexUnlock(exf_mx); }
#else
static void exf_lock(void) { }
static void exf_unlock(void) { }
#endif

/* "sd:/a/b" -> "/a/b" (FatFs : un seul volume) */
static const char *exf_path(const char *p) {
    const char *c = p ? strchr(p, ':') : NULL;
    if (c) p = c + 1;
    return (p && *p) ? p : "/";
}
static int exf_errno(FRESULT r) {
    switch (r) {
    case FR_OK: return 0;
    case FR_NO_FILE: case FR_NO_PATH: case FR_INVALID_NAME: return ENOENT;
    case FR_DENIED: case FR_WRITE_PROTECTED: return EACCES;
    case FR_EXIST: return EEXIST;
    case FR_INVALID_OBJECT: return EBADF;
    case FR_NOT_ENOUGH_CORE: return ENOMEM;
    case FR_TOO_MANY_OPEN_FILES: return EMFILE;
    case FR_NOT_READY: case FR_DISK_ERR: case FR_INT_ERR: default: return EIO;
    }
}
/* date FAT -> time_t (heure locale) */
static time_t exf_time(WORD d, WORD t) {
    struct tm m; memset(&m, 0, sizeof(m));
    m.tm_year = (d >> 9) + 80; m.tm_mon = ((d >> 5) & 15) - 1; m.tm_mday = d & 31;
    m.tm_hour = t >> 11; m.tm_min = (t >> 5) & 63; m.tm_sec = (t & 31) * 2; m.tm_isdst = -1;
    return m.tm_mon < 0 ? 0 : mktime(&m);
}
static void exf_fill_stat(const FILINFO *fi, struct stat *st) {
    memset(st, 0, sizeof(*st));
    st->st_mode = (fi->fattrib & AM_DIR) ? (S_IFDIR | 0777) : (S_IFREG | 0666);
    st->st_size = (off_t)fi->fsize;
    st->st_nlink = 1; st->st_blksize = 512;
    st->st_blocks = (blkcnt_t)((fi->fsize + 511) / 512);
    st->st_mtime = st->st_atime = st->st_ctime = exf_time(fi->fdate, fi->ftime);
    st->st_ino = (ino_t)(((u32)fi->fdate << 16 | fi->ftime) ^ (u32)fi->fsize);   /* (change si le fichier change) */
}

/* monte la carte avec FatFs ; 1 = ok (exFAT, ou un FAT que libfat n'a pas su lire) */
static int exf_mount_io(bool (*rd)(u32, u32, void *), bool (*wr)(u32, u32, const void *)) {
    exf_rd = rd; exf_wr = wr;
    exf_lock();
    FRESULT r = f_mount(&exf_fs, "", 1);
    exf_unlock();
    if (r != FR_OK) { exf_rd = NULL; exf_wr = NULL; return 0; }
    exf_on = 1;
    return 1;
}
static void exf_umount(void) {
    if (!exf_on) return;
    exf_lock(); f_unmount(""); exf_unlock();
    exf_on = 0; exf_rd = NULL; exf_wr = NULL;
}
static const char *exf_type(void) {
    return !exf_on ? "" : exf_fs.fs_type == FS_EXFAT ? "exFAT" : exf_fs.fs_type == FS_FAT32 ? "FAT32" : exf_fs.fs_type == FS_FAT16 ? "FAT16" : "FAT12";
}

/* morceaux d'un fichier sur la carte : cb(premier secteur, nombre de secteurs) dans l'ordre du fichier ;
   0 = ok, -1 = introuvable, -2 = trop fragmente, -3 = erreur de lecture */
static int exf_extents(const char *path, int (*cb)(void *ctx, u32 sec, u32 n), void *ctx) {
    static FIL f; static DWORD tbl[2 * 4096 + 2];
    int ret = 0;
    exf_lock();
    if (f_open(&f, exf_path(path), FA_READ) != FR_OK) { exf_unlock(); return -1; }
    f.cltbl = tbl; tbl[0] = sizeof(tbl) / sizeof(tbl[0]);
    FRESULT r = f_lseek(&f, CREATE_LINKMAP);
    if (r == FR_NOT_ENOUGH_CORE) ret = -2;
    else if (r != FR_OK) ret = -3;
    else {
        u64 left = ((u64)f_size(&f) + 511) / 512;
        u32 cs = exf_fs.csize;
        for (DWORD *p = tbl + 1; *p && left; p += 2) {
            u64 n = (u64)p[0] * cs;
            if (n > left) n = left;
            u32 sec = (u32)(exf_fs.database + (LBA_t)(p[1] - 2) * cs);
            if (cb(ctx, sec, (u32)n)) { ret = -2; break; }
            left -= n;
        }
        if (!ret && left) ret = -3;
    }
    f.cltbl = NULL; f_close(&f);
    exf_unlock();
    return ret;
}

#ifdef GEKKO
/* ---- la table "sd:" de newlib (devoptab) ---- */
#include <sys/iosupport.h>
#include <fcntl.h>
#include <sys/statvfs.h>
typedef struct { FIL f; } exf_file_t;
typedef struct { FF_DIR d; } exf_dir_t;
#define EXF_RET(r, v) do { int _e = exf_errno(r); exf_unlock(); if (_e) { rr->_errno = _e; return -1; } return (v); } while (0)

static int exf_open_r(struct _reent *rr, void *fs, const char *path, int flags, int mode) {
    (void)mode;
    exf_file_t *x = (exf_file_t *)fs; BYTE m = 0;
    int acc = flags & O_ACCMODE;
    if (acc == O_RDONLY) m = FA_READ; else if (acc == O_WRONLY) m = FA_WRITE; else m = FA_READ | FA_WRITE;
    if (flags & O_CREAT) m |= (flags & O_EXCL) ? FA_CREATE_NEW : (flags & O_TRUNC) ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    else if (flags & O_TRUNC) m |= FA_CREATE_ALWAYS;
    exf_lock();
    FRESULT r = f_open(&x->f, exf_path(path), m);
    if (r == FR_OK && (flags & O_APPEND)) r = f_lseek(&x->f, f_size(&x->f));
    EXF_RET(r, (int)(intptr_t)fs);
}
static int exf_close_r(struct _reent *rr, void *fd) { exf_lock(); FRESULT r = f_close(&((exf_file_t *)fd)->f); EXF_RET(r, 0); }
static ssize_t exf_write_r(struct _reent *rr, void *fd, const char *p, size_t n) {
    UINT w = 0; exf_lock(); FRESULT r = f_write(&((exf_file_t *)fd)->f, p, (UINT)n, &w); EXF_RET(r, (ssize_t)w);
}
static ssize_t exf_read_r(struct _reent *rr, void *fd, char *p, size_t n) {
    UINT g = 0; exf_lock(); FRESULT r = f_read(&((exf_file_t *)fd)->f, p, (UINT)n, &g); EXF_RET(r, (ssize_t)g);
}
static off_t exf_seek_r(struct _reent *rr, void *fd, off_t pos, int dir) {
    FIL *f = &((exf_file_t *)fd)->f;
    exf_lock();
    FSIZE_t base = dir == SEEK_CUR ? f_tell(f) : dir == SEEK_END ? f_size(f) : 0;
    if ((off_t)base + pos < 0) { exf_unlock(); rr->_errno = EINVAL; return -1; }
    FRESULT r = f_lseek(f, base + pos);
    off_t at = (off_t)f_tell(f);
    EXF_RET(r, at);
}
static int exf_fstat_r(struct _reent *rr, void *fd, struct stat *st) {
    FIL *f = &((exf_file_t *)fd)->f;
    memset(st, 0, sizeof(*st));
    exf_lock();
    st->st_mode = S_IFREG | 0666; st->st_size = (off_t)f_size(f); st->st_nlink = 1; st->st_blksize = 512;
    st->st_blocks = (blkcnt_t)((f_size(f) + 511) / 512);
    exf_unlock();
    return 0;
}
static int exf_stat_r(struct _reent *rr, const char *path, struct stat *st) {
    const char *p = exf_path(path);
    if (!strcmp(p, "/")) { memset(st, 0, sizeof(*st)); st->st_mode = S_IFDIR | 0777; st->st_nlink = 1; return 0; }
    FILINFO fi; exf_lock(); FRESULT r = f_stat(p, &fi);
    if (r == FR_OK) exf_fill_stat(&fi, st);
    EXF_RET(r, 0);
}
static int exf_unlink_r(struct _reent *rr, const char *path) { exf_lock(); FRESULT r = f_unlink(exf_path(path)); EXF_RET(r, 0); }
static int exf_rename_r(struct _reent *rr, const char *a, const char *b) {
    exf_lock();
    FRESULT r = f_rename(exf_path(a), exf_path(b));
    if (r == FR_EXIST && f_unlink(exf_path(b)) == FR_OK) r = f_rename(exf_path(a), exf_path(b));   /* (rename remplace, comme libfat) */
    EXF_RET(r, 0);
}
static int exf_mkdir_r(struct _reent *rr, const char *path, int mode) { (void)mode; exf_lock(); FRESULT r = f_mkdir(exf_path(path)); EXF_RET(r, 0); }
static DIR_ITER *exf_diropen_r(struct _reent *rr, DIR_ITER *it, const char *path) {
    exf_lock();
    FRESULT r = f_opendir(&((exf_dir_t *)it->dirStruct)->d, exf_path(path));
    int e = exf_errno(r); exf_unlock();
    if (e) { rr->_errno = e; return NULL; }
    return it;
}
static int exf_dirreset_r(struct _reent *rr, DIR_ITER *it) { exf_lock(); FRESULT r = f_rewinddir(&((exf_dir_t *)it->dirStruct)->d); EXF_RET(r, 0); }
static int exf_dirnext_r(struct _reent *rr, DIR_ITER *it, char *name, struct stat *st) {
    FILINFO fi; exf_lock();
    FRESULT r = f_readdir(&((exf_dir_t *)it->dirStruct)->d, &fi);
    if (r == FR_OK && !fi.fname[0]) { exf_unlock(); rr->_errno = ENOENT; return -1; }   /* (fin du dossier) */
    if (r == FR_OK) { snprintf(name, 256, "%s", fi.fname); if (st) exf_fill_stat(&fi, st); }
    EXF_RET(r, 0);
}
static int exf_dirclose_r(struct _reent *rr, DIR_ITER *it) { exf_lock(); FRESULT r = f_closedir(&((exf_dir_t *)it->dirStruct)->d); EXF_RET(r, 0); }
static int exf_statvfs_r(struct _reent *rr, const char *path, struct statvfs *b) {
    (void)path; DWORD fre = 0; FATFS *fs = NULL;
    exf_lock();
    FRESULT r = f_getfree("", &fre, &fs);
    if (r == FR_OK) {
        memset(b, 0, sizeof(*b));
        b->f_bsize = b->f_frsize = (unsigned long)exf_fs.csize * 512;
        b->f_blocks = exf_fs.n_fatent - 2; b->f_bfree = b->f_bavail = fre;
        b->f_namemax = FF_MAX_LFN;
    }
    EXF_RET(r, 0);
}
static int exf_ftruncate_r(struct _reent *rr, void *fd, off_t len) {
    FIL *f = &((exf_file_t *)fd)->f;
    exf_lock();
    FSIZE_t at = f_tell(f);
    FRESULT r = f_lseek(f, (FSIZE_t)len);
    if (r == FR_OK) r = f_truncate(f);
    if (r == FR_OK && at < (FSIZE_t)len) r = f_lseek(f, at);
    EXF_RET(r, 0);
}
static int exf_fsync_r(struct _reent *rr, void *fd) { exf_lock(); FRESULT r = f_sync(&((exf_file_t *)fd)->f); EXF_RET(r, 0); }

static const devoptab_t exf_dotab = {
    .name = "sd", .structSize = sizeof(exf_file_t),
    .open_r = exf_open_r, .close_r = exf_close_r, .write_r = exf_write_r, .read_r = exf_read_r, .seek_r = exf_seek_r,
    .fstat_r = exf_fstat_r, .stat_r = exf_stat_r, .unlink_r = exf_unlink_r, .rename_r = exf_rename_r, .mkdir_r = exf_mkdir_r,
    .dirStateSize = sizeof(exf_dir_t), .diropen_r = exf_diropen_r, .dirreset_r = exf_dirreset_r, .dirnext_r = exf_dirnext_r,
    .dirclose_r = exf_dirclose_r, .statvfs_r = exf_statvfs_r, .ftruncate_r = exf_ftruncate_r, .fsync_r = exf_fsync_r,
    .rmdir_r = exf_unlink_r, .lstat_r = exf_stat_r,
};
static bool exf_io_rd(u32 s, u32 n, void *b) { sd_rd_sec += n; return sd_io_read(s, n, b); }
static bool exf_io_wr(u32 s, u32 n, const void *b) {
    sd_wr_sec += n;
    if (sd_mx_ok) LWP_MutexLock(sd_mx);
    bool r = sd_real_if->writeSectors(s, n, b);
    if (sd_mx_ok) LWP_MutexUnlock(sd_mx);
    return r;
}
/* la carte de cette interface, montee par FatFs sous "sd:" ; 1 = ok */
static int exf_mount_sd(const DISC_INTERFACE *i) {
    if (!sd_mx_ok && LWP_MutexInit(&sd_mx, true) >= 0) sd_mx_ok = 1;
    if (!i->startup() || !i->isInserted()) return 0;
    sd_real_if = i;
    if (!exf_mount_io(exf_io_rd, exf_io_wr)) return 0;
    if (AddDevice(&exf_dotab) < 0) { exf_umount(); return 0; }
    return 1;
}
#endif

/* demonte la carte (avant de lancer un jeu) : libfat ou FatFs */
static void sd_umount(void) {
#ifdef GEKKO
    if (exf_on) { exf_umount(); RemoveDevice("sd:"); return; }
#endif
    fatUnmount("sd");
}

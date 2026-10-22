// GC-MP3 : carte SD "virtuelle" en memoire, quand aucune vraie carte SD n'est
// trouvee (emulateur Dolphin, ou GameCube sans adaptateur SD). Inclus dans main.c.
//  - un peripherique "sd:" en memoire : tout le reste de GC-MP3 marche sans
//    changement (reglages, radios, podcasts, telechargements, envoi depuis le
//    telephone...), dans la limite de RF_MAX octets ;
//  - les petits fichiers de reglages (.txt a la racine, positions des podcasts)
//    sont gardes sur la carte memoire (fichier "GC Station donnees") et relus au
//    demarrage suivant. Le reste (musique telechargee) est perdu en quittant.

#define RF_MAX        (6 * 1024 * 1024)      /* memoire maximale des fichiers */
#define RF_SAVE_MAX   (96 * 1024)            /* donnees gardees sur la carte memoire */
#define RF_CARD_NAME  "GC-MP3 donnees"

typedef struct rf_node {
    struct rf_node *next;
    char *path;                              /* "/dossier/fichier" */
    u8 *data;
    u32 len, cap;
    u8 dir, gone;
    int opens;
} rf_node;

typedef struct { rf_node *n; u32 pos; int flags; } rf_file;

static rf_node *rf_head = NULL;
static u32 rf_total = 0;
static int sd_virtual = 0;                   /* 1 = pas de vraie carte SD */
static volatile int rf_dirty = 0;            /* reglages a recopier sur la carte memoire */
static u64 rf_dirty_t = 0;
static int rf_card_chan = -1;                /* port de la carte memoire (0 = A, 1 = B) */
static char rf_card_msg[96] = "";

/* "sd:/a/b" ou "/a/b" -> "/a/b" (sans '/' final) */
static void rf_norm(const char *in, char *out, int cap) {
    const char *c = strchr(in, ':');
    if (c) in = c + 1;
    int n = 0;
    if (*in != '/' && n < cap - 1) out[n++] = '/';
    for (; *in && n < cap - 1; in++) {
        if (*in == '/' && n > 0 && out[n - 1] == '/') continue;
        out[n++] = *in;
    }
    while (n > 1 && out[n - 1] == '/') n--;
    out[n] = 0;
}

static rf_node *rf_find(const char *norm) {
    for (rf_node *n = rf_head; n; n = n->next)
        if (!n->gone && !strcasecmp(n->path, norm)) return n;
    return NULL;
}

static int rf_persist(const char *p) {           /* garde sur la carte memoire ? */
    if (!strcasecmp(p, "/podcasts/positions.txt")) return 1;
    if (strchr(p + 1, '/')) return 0;
    size_t l = strlen(p);
    return l > 4 && !strcasecmp(p + l - 4, ".txt") && strcasecmp(p, "/gc-mp3-bib.txt") != 0 && strncasecmp(p, "/gc-mp3-crash", 13) != 0;
}

static void rf_mark(rf_node *n) {
    if (n && rf_persist(n->path)) { rf_dirty = 1; rf_dirty_t = now_ms(); }
}

static rf_node *rf_new(const char *norm, int dir) {
    rf_node *n = (rf_node *)calloc(1, sizeof(rf_node));
    if (!n) return NULL;
    n->path = strdup(norm);
    if (!n->path) { free(n); return NULL; }
    n->dir = (u8)dir;
    n->next = rf_head;
    rf_head = n;
    return n;
}

/* cree les dossiers parents qui manquent */
static void rf_parents(const char *norm) {
    char p[NAME_LEN];
    snprintf(p, sizeof(p), "%s", norm);
    for (char *s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (!rf_find(p)) rf_new(p, 1);
        *s = '/';
    }
}

static void rf_free_node(rf_node *n) {
    rf_total -= n->cap;
    free(n->data);
    free(n->path);
    free(n);
}

/* retire de la liste (libere tout de suite, ou a la fermeture s'il est ouvert) */
static void rf_remove(rf_node *n) {
    rf_node **pp = &rf_head;
    while (*pp && *pp != n) pp = &(*pp)->next;
    if (!*pp) return;
    if (n->opens > 0) { n->gone = 1; return; }
    *pp = n->next;
    rf_free_node(n);
}

static int rf_grow(rf_node *n, u32 need) {
    if (need <= n->cap) return 1;
    u32 nc = n->cap ? n->cap : 1024;
    while (nc < need) nc = nc < 1024 * 1024 ? nc * 2 : nc + 1024 * 1024;
    if (rf_total - n->cap + nc > RF_MAX) {
        nc = need;
        if (rf_total - n->cap + nc > RF_MAX) return 0;
    }
    u8 *d = (u8 *)realloc(n->data, nc);
    if (!d) return 0;
    rf_total = rf_total - n->cap + nc;
    n->data = d;
    n->cap = nc;
    return 1;
}

/* ---------- operations du peripherique ---------- */
#include <fcntl.h>
#ifdef GEKKO
#include <sys/iosupport.h>
#include <ogc/card.h>
static mutex_t rf_mx = LWP_MUTEX_NULL;
#define RF_LOCK()   LWP_MutexLock(rf_mx)
#define RF_UNLOCK() LWP_MutexUnlock(rf_mx)
#else
#define RF_LOCK()
#define RF_UNLOCK()
#endif

#if defined(GEKKO) || defined(RF_TEST)
static int rf_open(struct _reent *r, void *fs, const char *path, int flags, int mode) {
    (void)mode;
    char p[NAME_LEN];
    rf_norm(path, p, sizeof(p));
    RF_LOCK();
    rf_node *n = rf_find(p);
    if (n && n->dir) { RF_UNLOCK(); r->_errno = EISDIR; return -1; }
    if (!n) {
        if (!(flags & O_CREAT)) { RF_UNLOCK(); r->_errno = ENOENT; return -1; }
        rf_parents(p);
        n = rf_new(p, 0);
        if (!n) { RF_UNLOCK(); r->_errno = ENOMEM; return -1; }
        rf_mark(n);
    } else if ((flags & O_CREAT) && (flags & O_EXCL)) { RF_UNLOCK(); r->_errno = EEXIST; return -1; }
    if ((flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY) { n->len = 0; rf_mark(n); }
    rf_file *f = (rf_file *)fs;
    f->n = n;
    f->pos = 0;
    f->flags = flags;
    n->opens++;
    RF_UNLOCK();
    return (int)(intptr_t)f;
}

static int rf_close(struct _reent *r, void *fd) {
    (void)r;
    rf_file *f = (rf_file *)fd;
    RF_LOCK();
    if (f->n) {
        f->n->opens--;
        if (f->n->gone && f->n->opens <= 0) { f->n->opens = 0; rf_remove(f->n); }
        f->n = NULL;
    }
    RF_UNLOCK();
    return 0;
}

static ssize_t rf_write(struct _reent *r, void *fd, const char *ptr, size_t len) {
    rf_file *f = (rf_file *)fd;
    if ((f->flags & O_ACCMODE) == O_RDONLY) { r->_errno = EBADF; return -1; }
    RF_LOCK();
    rf_node *n = f->n;
    if (f->flags & O_APPEND) f->pos = n->len;
    if ((u64)f->pos + len > 0x7fffffffULL || !rf_grow(n, f->pos + (u32)len)) { RF_UNLOCK(); r->_errno = ENOSPC; return -1; }
    if (f->pos > n->len) memset(n->data + n->len, 0, f->pos - n->len);
    memcpy(n->data + f->pos, ptr, len);
    f->pos += (u32)len;
    if (f->pos > n->len) n->len = f->pos;
    rf_mark(n);
    RF_UNLOCK();
    return (ssize_t)len;
}

static ssize_t rf_read(struct _reent *r, void *fd, char *ptr, size_t len) {
    (void)r;
    rf_file *f = (rf_file *)fd;
    RF_LOCK();
    rf_node *n = f->n;
    size_t k = f->pos < n->len ? n->len - f->pos : 0;
    if (k > len) k = len;
    if (k) memcpy(ptr, n->data + f->pos, k);
    f->pos += (u32)k;
    RF_UNLOCK();
    return (ssize_t)k;
}

static off_t rf_seek(struct _reent *r, void *fd, off_t pos, int dir) {
    rf_file *f = (rf_file *)fd;
    off_t base = dir == SEEK_CUR ? (off_t)f->pos : dir == SEEK_END ? (off_t)f->n->len : 0;
    off_t np = base + pos;
    if (np < 0 || np > 0x7fffffffL) { r->_errno = EINVAL; return -1; }
    f->pos = (u32)np;
    return np;
}

static void rf_fill_stat(rf_node *n, struct stat *st) {
    memset(st, 0, sizeof(*st));
    st->st_mode = n->dir ? (S_IFDIR | 0777) : (S_IFREG | 0666);
    st->st_size = n->dir ? 0 : n->len;
    st->st_nlink = 1;
    st->st_blksize = 512;
    st->st_blocks = (st->st_size + 511) / 512;
}

static int rf_fstat(struct _reent *r, void *fd, struct stat *st) {
    (void)r;
    rf_fill_stat(((rf_file *)fd)->n, st);
    return 0;
}

static int rf_stat(struct _reent *r, const char *path, struct stat *st) {
    char p[NAME_LEN];
    rf_norm(path, p, sizeof(p));
    if (!strcmp(p, "/")) { memset(st, 0, sizeof(*st)); st->st_mode = S_IFDIR | 0777; return 0; }
    RF_LOCK();
    rf_node *n = rf_find(p);
    if (n) rf_fill_stat(n, st);
    RF_UNLOCK();
    if (!n) { r->_errno = ENOENT; return -1; }
    return 0;
}

static int rf_unlink(struct _reent *r, const char *path) {
    char p[NAME_LEN];
    rf_norm(path, p, sizeof(p));
    RF_LOCK();
    rf_node *n = rf_find(p);
    if (!n) { RF_UNLOCK(); r->_errno = ENOENT; return -1; }
    if (n->dir) {                                 /* dossier : seulement s'il est vide */
        size_t l = strlen(p);
        for (rf_node *m = rf_head; m; m = m->next)
            if (!m->gone && m != n && !strncasecmp(m->path, p, l) && m->path[l] == '/') { RF_UNLOCK(); r->_errno = ENOTEMPTY; return -1; }
    }
    if (rf_persist(n->path)) { rf_dirty = 1; rf_dirty_t = now_ms(); }
    rf_remove(n);
    RF_UNLOCK();
    return 0;
}

static int rf_rename(struct _reent *r, const char *from, const char *to) {
    char a[NAME_LEN], b[NAME_LEN];
    rf_norm(from, a, sizeof(a));
    rf_norm(to, b, sizeof(b));
    RF_LOCK();
    rf_node *n = rf_find(a);
    if (!n) { RF_UNLOCK(); r->_errno = ENOENT; return -1; }
    if (rf_find(b)) { RF_UNLOCK(); r->_errno = EEXIST; return -1; }
    size_t al = strlen(a);
    int persist = rf_persist(a) || rf_persist(b);
    for (rf_node *m = rf_head; m; m = m->next) {        /* le noeud et, pour un dossier, son contenu */
        if (m->gone) continue;
        if (m == n || (n->dir && !strncasecmp(m->path, a, al) && m->path[al] == '/')) {
            char np[NAME_LEN * 2];
            snprintf(np, sizeof(np), "%s%s", b, m->path + al);
            char *d = strdup(np);
            if (!d) continue;
            free(m->path);
            m->path = d;
        }
    }
    rf_parents(b);
    if (persist) { rf_dirty = 1; rf_dirty_t = now_ms(); }
    RF_UNLOCK();
    return 0;
}

static int rf_mkdir(struct _reent *r, const char *path, int mode) {
    (void)mode;
    char p[NAME_LEN];
    rf_norm(path, p, sizeof(p));
    RF_LOCK();
    if (rf_find(p) || !strcmp(p, "/")) { RF_UNLOCK(); r->_errno = EEXIST; return -1; }
    rf_parents(p);
    rf_node *n = rf_new(p, 1);
    RF_UNLOCK();
    if (!n) { r->_errno = ENOMEM; return -1; }
    return 0;
}

typedef struct { char dir[NAME_LEN]; int idx; } rf_dir_t;
#endif

#ifdef GEKKO
static DIR_ITER *rf_diropen(struct _reent *r, DIR_ITER *ds, const char *path) {
    rf_dir_t *d = (rf_dir_t *)ds->dirStruct;
    rf_norm(path, d->dir, sizeof(d->dir));
    d->idx = 0;
    if (strcmp(d->dir, "/")) {
        RF_LOCK();
        rf_node *n = rf_find(d->dir);
        RF_UNLOCK();
        if (!n || !n->dir) { r->_errno = ENOENT; return NULL; }
    }
    return ds;
}
static int rf_dirreset(struct _reent *r, DIR_ITER *ds) { (void)r; ((rf_dir_t *)ds->dirStruct)->idx = 0; return 0; }
#endif

#if defined(GEKKO) || defined(RF_TEST)
/* k-ieme element direct du dossier (0 = premier) ; 0 s'il n'y en a plus */
static int rf_dir_nth(const char *dir, int k, char *name, struct stat *st) {
    size_t l = strcmp(dir, "/") ? strlen(dir) : 0;
    int i = 0;
    for (rf_node *m = rf_head; m; m = m->next) {
        if (m->gone) continue;
        if (l && (strncasecmp(m->path, dir, l) || m->path[l] != '/')) continue;
        const char *rest = m->path + l + 1;
        if (!*rest || strchr(rest, '/')) continue;
        if (i++ < k) continue;
        snprintf(name, 256, "%s", rest);
        if (st) rf_fill_stat(m, st);
        return 1;
    }
    return 0;
}
#endif

#ifdef GEKKO
static int rf_dirnext(struct _reent *r, DIR_ITER *ds, char *filename, struct stat *st) {
    rf_dir_t *d = (rf_dir_t *)ds->dirStruct;
    RF_LOCK();
    int ok = rf_dir_nth(d->dir, d->idx, filename, st);
    RF_UNLOCK();
    if (!ok) { r->_errno = ENOENT; return -1; }
    d->idx++;
    return 0;
}
static int rf_dirclose(struct _reent *r, DIR_ITER *ds) { (void)r; (void)ds; return 0; }

static int rf_statvfs(struct _reent *r, const char *path, struct statvfs *buf) {
    (void)r; (void)path;
    memset(buf, 0, sizeof(*buf));
    buf->f_bsize = buf->f_frsize = 1024;
    buf->f_blocks = RF_MAX / 1024;
    buf->f_bfree = buf->f_bavail = (RF_MAX - rf_total) / 1024;
    buf->f_namemax = 255;
    return 0;
}
static int rf_ftruncate(struct _reent *r, void *fd, off_t len) {
    rf_file *f = (rf_file *)fd;
    if (len < 0) { r->_errno = EINVAL; return -1; }
    RF_LOCK();
    if ((u32)len > f->n->len && !rf_grow(f->n, (u32)len)) { RF_UNLOCK(); r->_errno = ENOSPC; return -1; }
    if ((u32)len > f->n->len) memset(f->n->data + f->n->len, 0, (u32)len - f->n->len);
    f->n->len = (u32)len;
    rf_mark(f->n);
    RF_UNLOCK();
    return 0;
}
static int rf_fsync(struct _reent *r, void *fd) { (void)r; (void)fd; return 0; }

static const devoptab_t rf_devoptab = {
    .name = "sd",
    .structSize = sizeof(rf_file),
    .open_r = rf_open,
    .close_r = rf_close,
    .write_r = rf_write,
    .read_r = rf_read,
    .seek_r = rf_seek,
    .fstat_r = rf_fstat,
    .stat_r = rf_stat,
    .unlink_r = rf_unlink,
    .rename_r = rf_rename,
    .mkdir_r = rf_mkdir,
    .dirStateSize = sizeof(rf_dir_t),
    .diropen_r = rf_diropen,
    .dirreset_r = rf_dirreset,
    .dirnext_r = rf_dirnext,
    .dirclose_r = rf_dirclose,
    .statvfs_r = rf_statvfs,
    .ftruncate_r = rf_ftruncate,
    .fsync_r = rf_fsync,
    .rmdir_r = rf_unlink,
    .lstat_r = rf_stat,
};
#endif

/* ---------- sauvegarde des reglages sur la carte memoire ----------
   format : "GCMPRAM1" puis, pour chaque fichier : longueur du chemin (2 octets),
   chemin, longueur (4 octets), contenu ; termine par une longueur de chemin 0. */
static u32 rf_pack(u8 *out, u32 cap) {
    u32 o = 8;
    if (cap < 10) return 0;
    memcpy(out, "GCMPRAM1", 8);
    for (rf_node *n = rf_head; n; n = n->next) {
        if (n->gone || n->dir || !rf_persist(n->path)) continue;
        u32 pl = (u32)strlen(n->path);
        if (o + 2 + pl + 4 + n->len + 2 > cap) continue;        /* (trop gros : ignore) */
        out[o++] = (u8)(pl >> 8); out[o++] = (u8)pl;
        memcpy(out + o, n->path, pl); o += pl;
        out[o++] = (u8)(n->len >> 24); out[o++] = (u8)(n->len >> 16); out[o++] = (u8)(n->len >> 8); out[o++] = (u8)n->len;
        if (n->len) memcpy(out + o, n->data, n->len);
        o += n->len;
    }
    out[o++] = 0; out[o++] = 0;
    return o;
}

static int rf_unpack(const u8 *in, u32 len) {
    if (len < 10 || memcmp(in, "GCMPRAM1", 8)) return 0;
    u32 o = 8;
    int files = 0;
    while (o + 2 <= len) {
        u32 pl = (u32)in[o] << 8 | in[o + 1];
        o += 2;
        if (pl == 0 || pl >= NAME_LEN || o + pl + 4 > len) break;
        char p[NAME_LEN], np[NAME_LEN];
        memcpy(p, in + o, pl); p[pl] = 0;
        o += pl;
        u32 fl = (u32)in[o] << 24 | (u32)in[o + 1] << 16 | (u32)in[o + 2] << 8 | in[o + 3];
        o += 4;
        if (fl > len - o) break;
        rf_norm(p, np, sizeof(np));
        if (!rf_persist(np) || rf_find(np)) { o += fl; continue; }
        rf_parents(np);
        rf_node *n = rf_new(np, 0);
        if (!n || (fl && !rf_grow(n, fl))) break;
        if (fl) memcpy(n->data, in + o, fl);
        n->len = fl;
        o += fl;
        files++;
    }
    return files;
}

#ifdef GEKKO
static u8 rf_card_work[CARD_WORKAREA_SIZE] ATTRIBUTE_ALIGN(32);
static u32 rf_sector = 8192;

/* une carte memoire branchee (A puis B) ; 1 si montee */
static int rf_card_mount(void) {
    static int inited = 0;
    if (!inited) { CARD_Init("GCMP", "00"); inited = 1; }
    for (int ch = 0; ch < 2; ch++) {
        s32 r = CARD_Mount(ch, rf_card_work, NULL);
        if (r == CARD_ERROR_READY || r == CARD_ERROR_BROKEN) {           /* (abimee : on essaie quand meme) */
            if (CARD_GetSectorSize(ch, &rf_sector) < 0 || rf_sector == 0) rf_sector = 8192;
            rf_card_chan = ch;
            return 1;
        }
    }
    rf_card_chan = -1;
    return 0;
}

static void rf_card_load(void) {
    if (!rf_card_mount()) { snprintf(rf_card_msg, sizeof(rf_card_msg), "Pas de carte m\xC3\xA9moire : r\xC3\xA9glages non gard\xC3\xA9s"); return; }
    card_file cf;
    if (CARD_Open(rf_card_chan, RF_CARD_NAME, &cf) == CARD_ERROR_READY) {
        u32 len = cf.len;
        if (len > RF_SAVE_MAX + 65536) len = RF_SAVE_MAX + 65536;
        len -= len % rf_sector;
        u8 *b = (u8 *)memalign(32, len ? len : 32);
        if (b && len && CARD_Read(&cf, b, len, 0) == CARD_ERROR_READY) {
            int n = rf_unpack(b, len);
            snprintf(rf_card_msg, sizeof(rf_card_msg), "R\xC3\xA9glages lus sur la carte m\xC3\xA9moire %c (%d fichiers)", 'A' + rf_card_chan, n);
        }
        free(b);
        CARD_Close(&cf);
    } else snprintf(rf_card_msg, sizeof(rf_card_msg), "R\xC3\xA9glages gard\xC3\xA9s sur la carte m\xC3\xA9moire %c", 'A' + rf_card_chan);
    CARD_Unmount(rf_card_chan);
}

static void rf_card_save(void) {
    rf_dirty = 0;
    static u8 *pack = NULL;
    if (!pack) pack = (u8 *)memalign(32, RF_SAVE_MAX);
    if (!pack) return;
    RF_LOCK();
    u32 len = rf_pack(pack, RF_SAVE_MAX);
    RF_UNLOCK();
    if (!len || !rf_card_mount()) return;
    u32 want = (len + rf_sector - 1) / rf_sector * rf_sector;
    card_file cf;
    s32 r = CARD_Open(rf_card_chan, RF_CARD_NAME, &cf);
    if (r == CARD_ERROR_READY && cf.len < want) { CARD_Close(&cf); CARD_Delete(rf_card_chan, RF_CARD_NAME); r = CARD_ERROR_NOFILE; }
    if (r != CARD_ERROR_READY) {
        u32 sz = want < 4 * rf_sector ? 4 * rf_sector : want;       /* un peu de marge */
        r = CARD_Create(rf_card_chan, RF_CARD_NAME, sz, &cf);
        if (r != CARD_ERROR_READY) r = CARD_Create(rf_card_chan, RF_CARD_NAME, want, &cf);
    }
    if (r == CARD_ERROR_READY) {
        memset(pack + len, 0, want - len);
        if (CARD_Write(&cf, pack, want, 0) != CARD_ERROR_READY)
            snprintf(rf_card_msg, sizeof(rf_card_msg), "\xC3\x89" "criture sur la carte m\xC3\xA9moire impossible");
        CARD_Close(&cf);
    } else snprintf(rf_card_msg, sizeof(rf_card_msg), "Carte m\xC3\xA9moire pleine : r\xC3\xA9glages non gard\xC3\xA9s");
    CARD_Unmount(rf_card_chan);
}

/* pas de vraie carte SD : "sd:" en memoire ; 1 si pret */
static int ramfs_mount(void) {
    if (LWP_MutexInit(&rf_mx, true) < 0) return 0;
    if (AddDevice(&rf_devoptab) < 0) return 0;
    sd_virtual = 1;
    rf_card_load();
    return 1;
}
#else
static void rf_card_save(void) { rf_dirty = 0; }
static int ramfs_mount(void) { return 0; }
#endif

/* boucle principale : recopie les reglages modifies (3 s apres le dernier changement) */
static void ramfs_pump(void) {
    if (!sd_virtual || !rf_dirty) return;
    if (now_ms() - rf_dirty_t < 3000) return;
    rf_card_save();
}
